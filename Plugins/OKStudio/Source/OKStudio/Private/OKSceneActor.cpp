#include "OKSceneActor.h"
#include "Async/ParallelFor.h"
#include "Tasks/Task.h"
#include "HAL/IConsoleManager.h"
#include "OKStudioImport.h"
#include "OKStudioImport.h"
#include "Camera/CameraComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/RectLightComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "HAL/PlatformMisc.h"
#include "KKPreviewActor.h"
#include "KKOkAnimation.h"
#include "KKExactSkinning.h"
#include "KKRustExtract.h"
#include "ok_extract.h"
#include "Misc/ConfigCacheIni.h"
#include "ImageUtils.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "EngineUtils.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/PlayerController.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureCube.h"
#include "Misc/Base64.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "OKAnimationAssets.h"
#include "KKOkAnimation.h"
#include "OKStudioCatalog.h"
#include "OKStudioSky.h"
#include "OKHGShading.h"
#include "KKRuntimeDisplayProfile.h"
#include "KKRuntimeSharedMaterial.h"
#include "OKAnimationLibrary.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "UObject/StrongObjectPtr.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

AOKSceneActor::AOKSceneActor()
{
	PrimaryActorTick.bCanEverTick = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
}

FString AOKSceneActor::LibraryDirectory()
{
	const FString Configured = FPlatformMisc::GetEnvironmentVariable(TEXT("OK_BASE_LIBRARY"));
	return Configured.IsEmpty() ? FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir() / TEXT("IndependentResources/base_v1")) : Configured;
}

namespace
{
	/** One H motion variant's .okanim bytes: the loose file under the library (a conversion in progress), else its entry
	 *  in the variant pack `anims/studio_variants.okpack` (ok-extract's studio_anims::variant_pack: `OKVPACK1`, u32
	 *  count, per entry u16 name length, UTF-8 name, u64 offset, u32 length). The index is read once per pack file;
	 *  each variant is read alone at its offset. */
	bool ReadStudioVariant(const FString& Name, TArray<uint8>& Out)
	{
		const FString Lib = AOKSceneActor::LibraryDirectory();
		if (FPaths::FileExists(Lib / Name) && FFileHelper::LoadFileToArray(Out, *(Lib / Name))) return true;
		struct FPack { FString Path; FDateTime Stamp; TMap<FString, TPair<int64, int32>> Index; };
		static FPack Pack;
		static FCriticalSection Lock;
		FScopeLock Locked(&Lock);
		const FString Path = Lib / TEXT("anims/studio_variants.okpack");
		const FDateTime Stamp = IFileManager::Get().GetTimeStamp(*Path);
		if (Pack.Path != Path || Pack.Stamp != Stamp)
		{
			Pack = FPack{ Path, Stamp, {} };
			TUniquePtr<FArchive> Ar(IFileManager::Get().CreateFileReader(*Path));
			char Magic[8] = {};
			uint32 Count = 0;
			if (!Ar) return false;
			Ar->Serialize(Magic, 8);
			*Ar << Count;
			if (FMemory::Memcmp(Magic, "OKVPACK1", 8) != 0 || Ar->IsError()) return false;
			Pack.Index.Reserve(Count);
			TArray<uint8> NameBytes;
			for (uint32 I = 0; I < Count && !Ar->IsError(); ++I)
			{
				uint16 Len = 0; uint64 Offset = 0; uint32 Size = 0;
				*Ar << Len;
				NameBytes.SetNumUninitialized(Len);
				Ar->Serialize(NameBytes.GetData(), Len);
				*Ar << Offset << Size;
				Pack.Index.Add(FString(FUTF8ToTCHAR(reinterpret_cast<const ANSICHAR*>(NameBytes.GetData()), Len)), { int64(Offset), int32(Size) });
			}
		}
		const TPair<int64, int32>* At = Pack.Index.Find(Name);
		TUniquePtr<FArchive> Ar(At ? IFileManager::Get().CreateFileReader(*Path) : nullptr);
		if (!Ar) return false;
		Ar->Seek(At->Key);
		Out.SetNumUninitialized(At->Value);
		Ar->Serialize(Out.GetData(), At->Value);
		return !Ar->IsError();
	}

	/** An H motion's variants as nested 1D blends, one per Studio control (player parameter I is control I): blending
	 *  each grid line in turn is the multilinear interpolation of the sampled blend trees. Null when the variants do
	 *  not fill the grid (the plain clip plays). */
	TSharedPtr<FOkAnimPlayer> StudioBlendPlayer(const FOKStudioAnim& A, FString& OutError)
	{
		const int32 Axes = A.Controls.Num();
		int32 Points = 1;
		for (const TArray<float>& At : A.ControlValues) Points *= At.Num();
		if (Axes == 0 || Axes > 8 || A.ControlValues.Num() != Axes || Points != A.Variants.Num()) return nullptr;
		TArray<TArray<uint8>> Blobs;
		for (const FString& File : A.Variants)
		{
			TArray<uint8>& Bytes = Blobs.AddDefaulted_GetRef();
			if (!ReadStudioVariant(File, Bytes)) { OutError = TEXT("H motion variant missing: ") + File; return nullptr; }
		}
		TFunction<TSharedPtr<FJsonObject>(int32, int32)> Node = [&](int32 Axis, int32 First) -> TSharedPtr<FJsonObject>
		{
			TSharedPtr<FJsonObject> J = MakeShared<FJsonObject>();
			if (Axis == Axes) { J->SetStringField(TEXT("type"), TEXT("anim")); J->SetNumberField(TEXT("anim"), First); return J; }
			int32 Stride = 1;
			for (int32 K = Axis + 1; K < Axes; ++K) Stride *= A.ControlValues[K].Num();
			TSharedRef<FJsonObject> Range = MakeShared<FJsonObject>();
			Range->SetStringField(TEXT("name"), A.Controls[Axis]);
			Range->SetNumberField(TEXT("min"), 0.);
			Range->SetNumberField(TEXT("max"), 1.);
			Range->SetNumberField(TEXT("param"), Axis);
			TArray<TSharedPtr<FJsonValue>> Samples;
			for (int32 I = 0; I < A.ControlValues[Axis].Num(); ++I)
			{
				TSharedRef<FJsonObject> S = MakeShared<FJsonObject>();
				S->SetArrayField(TEXT("at"), { MakeShared<FJsonValueNumber>(A.ControlValues[Axis][I]) });
				S->SetObjectField(TEXT("spec"), Node(Axis + 1, First + I * Stride));
				Samples.Add(MakeShared<FJsonValueObject>(S));
			}
			J->SetStringField(TEXT("type"), TEXT("blend"));
			J->SetArrayField(TEXT("axes"), { MakeShared<FJsonValueObject>(Range) });
			J->SetArrayField(TEXT("samples"), Samples);
			J->SetBoolField(TEXT("sync"), true);
			return J;
		};
		FString Spec;
		FJsonSerializer::Serialize(Node(0, 0).ToSharedRef(), TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Spec));
		TSharedPtr<FOkAnimPlayer> Player = MakeShared<FOkAnimPlayer>();
		if (!Player->Create(Spec, Blobs)) { OutError = FOkAnimBridge::LastError(); return nullptr; }
		UOKAnimationAsset::ApplyFamilyAliases(*Player);
		return Player;
	}

	/** `Local` then `World` as matrices: FTransform's own product applies World's non-uniform scale before Local's
	 *  rotation (the prefab root's -90 X of an FBX building took the item's Y scale as its height, 127 m instead of 37). */
	FTransform Compose(const FTransform& Local, const FTransform& World)
	{
		return FTransform(Local.ToMatrixWithScale() * World.ToMatrixWithScale());
	}

	/** The character state as ok_session_open_status takes it (empty: the card's own). */
	FString StatusJson(const FOKSceneCharacter& C)
	{
		const TSharedRef<FJsonObject> J = MakeShared<FJsonObject>();
		if (C.TearsLevel >= 0) J->SetNumberField(TEXT("tears_lv"), C.TearsLevel);
		if (C.Blush >= 0.f) J->SetNumberField(TEXT("hoho_aka_rate"), C.Blush);
		if (C.NipStand >= 0.f) J->SetNumberField(TEXT("nip_stand_rate"), C.NipStand);
		if (C.SkinGloss >= 0.f) J->SetNumberField(TEXT("skin_tuya_rate"), C.SkinGloss);
		if (C.InvisibleBody >= 0) J->SetBoolField(TEXT("invisible_body"), C.InvisibleBody != 0);
		if (C.Fluids.Num() == 5)
		{
			TArray<TSharedPtr<FJsonValue>> V;
			for (uint8 L : C.Fluids) V.Add(MakeShared<FJsonValueNumber>(L));
			J->SetArrayField(TEXT("siru_lv"), V);
		}
		if (C.AccessoriesShown.Num())
		{
			TArray<TSharedPtr<FJsonValue>> V;
			for (uint8 S : C.AccessoriesShown) V.Add(MakeShared<FJsonValueBoolean>(S != 0));
			J->SetArrayField(TEXT("show_accessory"), V);
		}
		// An eyes pattern whose eye set changes parts (gag eyes, hidden eye base / pupils) is built into the character.
		if (C.EyesPattern >= 0 && FOKStudioCatalog::Get().EyeSet(C.EyesPattern).bChangesParts) J->SetNumberField(TEXT("eyes_ptn"), C.EyesPattern);
		if (!J->Values.Num()) return FString();
		FString Out;
		const auto Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
		FJsonSerializer::Serialize(J, Writer);
		return Out;
	}

	FString CardPath(const FString& Card)
	{
		return FPaths::IsRelative(Card) ? FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / Card) : Card;
	}

	FString FirstPrefab(const FString& Library, const FString& Model)
	{
		FString Prefab, Error;
		return FOkModel::FirstPrefab(Library, Model, Prefab, Error) ? Prefab : FString();
	}

	/** A Studio pattern mask (character list mt_pattern) from the OK base library, as a linear texture (cached). */
	UTexture2D* PatternTexture(int32 Id)
	{
		static TMap<int32, TStrongObjectPtr<UTexture2D>> Cache;
		if (const TStrongObjectPtr<UTexture2D>* Found = Cache.Find(Id)) return Found->Get();
		UTexture2D* Texture = nullptr;
		const FTCHARToUTF8 Lib(*AOKSceneActor::LibraryDirectory());
		const int32 Size = ok_studio_pattern_png(Lib.Get(), Id, nullptr, 0);
		TArray<uint8> Png;
		if (Size > 0) { Png.SetNumUninitialized(Size); if (ok_studio_pattern_png(Lib.Get(), Id, Png.GetData(), Size) != Size) Png.Reset(); }
		if (Png.Num()) Texture = FImageUtils::ImportBufferAsTexture2D(Png);
		if (Texture) { Texture->SRGB = false; Texture->UpdateResource(); }
		Cache.Add(Id, TStrongObjectPtr<UTexture2D>(Texture));
		return Texture;
	}

	/** A Material Editor texture (base64 PNG) as an sRGB and a linear texture (cached by content). */
	TPair<UTexture2D*, UTexture2D*> EditTexture(const FString& Base64)
	{
		static TMap<FString, TPair<TStrongObjectPtr<UTexture2D>, TStrongObjectPtr<UTexture2D>>> Cache;
		const FString Key = FMD5::HashAnsiString(*Base64);
		if (const auto* Found = Cache.Find(Key)) return { Found->Key.Get(), Found->Value.Get() };
		TArray<uint8> Png;
		UTexture2D *Srgb = nullptr, *Linear = nullptr;
		if (FBase64::Decode(Base64, Png) && Png.Num())
		{
			Srgb = FImageUtils::ImportBufferAsTexture2D(Png);
			Linear = FImageUtils::ImportBufferAsTexture2D(Png);
			if (Linear) { Linear->SRGB = false; Linear->UpdateResource(); }
		}
		Cache.Add(Key, { TStrongObjectPtr<UTexture2D>(Srgb), TStrongObjectPtr<UTexture2D>(Linear) });
		return { Srgb, Linear };
	}

	/** Material overrides of an item's pattern slots (the item_draw / main_item_studio shader's pattern inputs). */
	TArray<FOKMaterialOverride> PatternOverrides(const FOKSceneItem& Item)
	{
		TArray<FOKMaterialOverride> Out;
		auto Add = [&Out](const FString& Name, EOKMaterialOverrideKind Kind, float Scalar, const FLinearColor& Vector, UTexture* Texture)
		{
			FOKMaterialOverride O;
			O.Parameter = FName(*Name);
			O.Kind = Kind;
			O.Scalar = Scalar;
			O.Vector = Vector;
			O.Texture = Texture;
			Out.Add(O);
		};
		for (int32 K = 0; K < Item.Patterns.Num() && K < 3; ++K)
		{
			const FOKSceneItemPattern& P = Item.Patterns[K];
			UTexture2D* Mask = P.Pattern > 0 ? PatternTexture(P.Pattern) : nullptr;
			if (!Mask) continue;
			const int32 Slot = K + 1;
			Add(FString::Printf(TEXT("_PatternMask%d"), Slot), EOKMaterialOverrideKind::Texture, 0.f, FLinearColor::White, Mask);
			Add(FString::Printf(TEXT("_Patternuv%d"), Slot), EOKMaterialOverrideKind::Vector, 0.f, FLinearColor(P.Uv.X, P.Uv.Y, P.Uv.Z, P.Uv.W), nullptr);
			Add(FString::Printf(TEXT("_patternrotator%d"), Slot), EOKMaterialOverrideKind::Scalar, P.Rotation, FLinearColor::White, nullptr);
			Add(FString::Printf(TEXT("_patternclamp%d"), Slot), EOKMaterialOverrideKind::Scalar, P.bClamp ? 1.f : 0.f, FLinearColor::White, nullptr);
			Add(FString::Printf(TEXT("_Color%d_2"), Slot), EOKMaterialOverrideKind::Color, 0.f, P.Color, nullptr);
		}
		// Material Editor edits: the named material's property (Material Editor drops the leading underscore). Shader and
		// renderer edits change how the item is built (ItemBuildJson).
		for (const FOKSceneMaterialEdit& E : Item.MaterialEdits)
		{
			if (E.Property.IsEmpty()) continue;
			const FString Name = E.Property.StartsWith(TEXT("_")) ? E.Property : TEXT("_") + E.Property;
			switch (E.Kind)
			{
			case EOKSceneMaterialEditKind::Float: Add(Name, EOKMaterialOverrideKind::Scalar, E.Value.R, E.Value, nullptr); break;
			case EOKSceneMaterialEditKind::Color: Add(Name, EOKMaterialOverrideKind::Color, 0.f, E.Value, nullptr); break;
			case EOKSceneMaterialEditKind::TextureST: Add(Name + TEXT("_ST"), EOKMaterialOverrideKind::Vector, 0.f, E.Value, nullptr); break;
			case EOKSceneMaterialEditKind::Texture:
			{
				const TPair<UTexture2D*, UTexture2D*> Texture = EditTexture(E.Text);
				if (!Texture.Key) continue;
				Add(Name, EOKMaterialOverrideKind::Texture, 0.f, FLinearColor::White, Texture.Key);
				Out.Last().TextureLinear = Texture.Value;
				break;
			}
			default: continue;
			}
			Out.Last().Material = E.Material;
		}
		return Out;
	}

	/** The item open options with the edits that change how it is built: FK, Material Editor shaders and renderers. */
	FString ItemBuildJson(const FOKSceneItem& Item)
	{
		TArray<TSharedPtr<FJsonValue>> Fk, Shaders, Renderers;
		for (const FOKSceneFkBone& B : Item.Fk)
			Fk.Add(MakeShared<FJsonValueArray>(TArray<TSharedPtr<FJsonValue>>{ MakeShared<FJsonValueString>(B.Bone),
				MakeShared<FJsonValueArray>(TArray<TSharedPtr<FJsonValue>>{ MakeShared<FJsonValueNumber>(B.Euler.X), MakeShared<FJsonValueNumber>(B.Euler.Y), MakeShared<FJsonValueNumber>(B.Euler.Z) }) }));
		for (const FOKSceneMaterialEdit& E : Item.MaterialEdits)
		{
			if (E.Kind == EOKSceneMaterialEditKind::Shader && !E.Text.IsEmpty())
				Shaders.Add(MakeShared<FJsonValueArray>(TArray<TSharedPtr<FJsonValue>>{ MakeShared<FJsonValueString>(E.Material), MakeShared<FJsonValueString>(E.Text), MakeShared<FJsonValueNumber>(FMath::RoundToInt(E.Value.R)) }));
			else if (E.Kind == EOKSceneMaterialEditKind::Renderer && !E.Material.IsEmpty())
				Renderers.Add(MakeShared<FJsonValueArray>(TArray<TSharedPtr<FJsonValue>>{ MakeShared<FJsonValueString>(E.Material), MakeShared<FJsonValueString>(E.Property), MakeShared<FJsonValueNumber>(E.Value.R) }));
		}
		FString Json;
		const auto Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Json);
		if (Shaders.IsEmpty() && Renderers.IsEmpty() && Item.LightCancel < 0.f)
		{
			if (Fk.IsEmpty()) return FString();
			FJsonSerializer::Serialize(Fk, Writer);
			return Json;
		}
		const TSharedRef<FJsonObject> Material = MakeShared<FJsonObject>();
		Material->SetArrayField(TEXT("shaders"), Shaders);
		Material->SetArrayField(TEXT("renderers"), Renderers);
		// Studio's per-item light cancel overrides _LightCancel on the materials that have it (ItemComponent.SetLineAndEmission).
		if (Item.LightCancel >= 0.f) Material->SetNumberField(TEXT("light_cancel"), Item.LightCancel);
		const TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetArrayField(TEXT("fk"), Fk);
		Root->SetObjectField(TEXT("material"), Material);
		FJsonSerializer::Serialize(Root, Writer);
		return Json;
	}

	AKKPreviewActor* SpawnPreview(UWorld* World, FOkSession&& Session, FString& OutError, const TArray<FLinearColor>& ItemColors = {}, const TArray<FOKMaterialOverride>& Overrides = {},
		const FOKSceneItem* Item = nullptr, bool bMap = false, const TMap<FName, FLinearColor>& ShaderGlobals = {}, int32 Ramp = -1)
	{
		FActorSpawnParameters Params;
		Params.ObjectFlags = RF_Transient;
		AKKPreviewActor* Actor = World->SpawnActor<AKKPreviewActor>(FVector::ZeroVector, FRotator::ZeroRotator, Params);
		if (!Actor) { OutError = TEXT("preview actor not spawned"); return nullptr; }
		Actor->SetActorTickEnabled(false);
		// The scene poses its characters once a frame (AdvanceCharacterFrames, UpdateLooks); loading and animation calls
		// turn the actor's Tick back on for its face clock and shadow, which must not pose them a second time.
		Actor->bSceneDrivenPose = true;
		Actor->Session = MoveTemp(Session);
		Actor->ItemColorOverrides = ItemColors;
		// Maps draw with their own material graphs (their lightmaps and emission are there). Items draw with the game's
		// own item program (the shared parents), with their colours as material overrides and Studio's toon shading.
		Actor->bUseSharedParents = !bMap;
        Actor->bSourceMapShading=bMap;
		Actor->bStudioItemShading = Item != nullptr;
		// Gallery's unshadowed display replaces material shadow colours with
		// white. Studio must retain its authored material lighting instead.
		// Items already retain their material shadows through bStudioItemShading;
		// they must not allocate a character depth capture per scene object.
		if (!bMap) Actor->CharacterDisplayShadowsOverride = Item == nullptr;
		// Items draw from the Studio root: the street's parents plus M_ForgeStudioOpaque, the depth-writing variant of the
		// game's item program (opaque items overlapping each other sort by depth, not by distance).
		if (!bMap) Actor->SharedMaterialRootOverride = TEXT("/Game/IndependentResources/RuntimeMaterials/StudioDisplay29");
		// The scene's shading globals (Studio's _ambientshadowG, _LineColorG, _linewidthG): Studio sets them for everything
		// it draws, characters as well as items.
		if (!bMap) Actor->StudioShaderGlobals = ShaderGlobals;
		// The scene's toon ramp (Studio's global _RampG, an mt_ramp of the game; OKHGShading's kk_<n>).
		if (!bMap && Ramp >= 0)
		{
			// A mod ramp keeps its own number (MoreShadowTypes' 850): its key is <mod>_<n>.
			FString RampKey = FString::Printf(TEXT("kk_%d"), Ramp);
			if (!OKRenderRamps().ContainsByPredicate([&](const FOKRenderRamp& R) { return R.Key == RampKey; }))
				if (const FOKRenderRamp* Mod = OKRenderRamps().FindByPredicate([Suffix = FString::Printf(TEXT("_%d"), Ramp)](const FOKRenderRamp& R) { return R.Key.EndsWith(Suffix); }))
					RampKey = Mod->Key;
			FString RampError;
			Actor->RampOverride = OKLoadRenderRamp(RampKey, RampError);
			if (!Actor->RampOverride) UE_LOG(LogTemp, Warning, TEXT("OK studio: scene ramp %d not loaded: %s"), Ramp, *RampError);
		}
		if (Item)
		{
			// Studio item state the materials are made with: shadow colour, fading, outline (ItemComponent defaults).
			const FOKStudioCatalogItem* Entry = Item->No >= 0 ? FOKStudioCatalog::Get().FindItem(Item->Group, Item->Category, Item->No) : nullptr;
			Actor->ItemShadowColorOverride = Item->ShadowColor;
			Actor->ItemAlpha = !Entry || Entry->bAlpha ? Item->Alpha : 1.f;
			// The game writes the scene line width to _LineWidthS (ItemComponent.UpdateColor); 0 draws no outline.
			Actor->bItemOutline = Item->LineWidth > 0.f;
		}
		// Before the session loads: the materials are made with these parameters (AKKPreviewActor::MaterialOverrides).
		Actor->MaterialOverrides = Overrides;
		if (!Actor->LoadSession(OutError)) { Actor->Destroy(); return nullptr; }
		// The preview actor's own studio camera is not part of the scene.
		TArray<UCameraComponent*> Cameras;
		Actor->GetComponents(Cameras);
		for (UCameraComponent* Cam : Cameras) Cam->DestroyComponent();
		return Actor;
	}
}

bool AOKSceneActor::ItemModel(const FOKSceneItem& Item, FString& OutModel, FString& OutPrefab, FString& OutError)
{
	OutModel = Item.Model;
	OutPrefab = Item.Prefab;
	// A catalogue item names its library model and prefab.
	if (Item.No >= 0)
	{
		const FOKStudioCatalogItem* Entry = FOKStudioCatalog::Get().FindItem(Item.Group, Item.Category, Item.No);
		if (!Entry) { OutError = FString::Printf(TEXT("no studio item %d/%d/%d (%s)"), Item.Group, Item.Category, Item.No, *FOKStudioCatalog::Get().Error); return false; }
		OutModel = Entry->Model;
		OutPrefab = Entry->Prefab;
	}
	if (OutPrefab.IsEmpty()) OutPrefab = FirstPrefab(LibraryDirectory(), OutModel);
	if (OutPrefab.IsEmpty()) { OutError = TEXT("model has no prefab: ") + OutModel; return false; }
	return true;
}


bool AOKSceneActor::Build(const FOKSceneObject& Object, FShown& Out, FString& OutError)
{
	Out.Kind = Object.Kind;
	Out.Character = Object.Character;
	Out.Item = Object.Item;
	Out.Light = Object.Light;
	Out.Time = Object.Character.Time;
	UWorld* World = GetWorld();
	if (!World) { OutError = TEXT("no world"); return false; }
	switch (Object.Kind)
	{
	case EOKSceneObjectKind::Character:
	{
		if (Object.Character.Card.IsEmpty()) { OutError = TEXT("no card"); return false; }
		TArray<uint8> States = Object.Character.ClothesStates;
		States.SetNumZeroed(9);
		FOkSession Session;
		// An OK card opens from the OK base library and its pack; an original KKS card from the library plus its zipmods.
		if (!Session.Open(FString(), CardPath(Object.Character.Card), Object.Character.Outfit, OutError, States.GetData(), StatusJson(Object.Character))) return false;
		AKKPreviewActor* Actor = SpawnPreview(World, MoveTemp(Session), OutError, {}, {}, nullptr, false,
			Document ? Document->Post.ShaderGlobals() : TMap<FName, FLinearColor>(), Document ? Document->Post.RampG : -1);
		if (!Actor) return false;
		Out.Preview = Actor;
		// Private-rig parts (hair, skirts, accessories) skinned on the GPU as in the one-to-one interaction scene: their
		// streams cost the game thread a third of the CPU path (OK_STUDIO_GPU_PLAYBACK=0 keeps the CPU path).
		static const bool bGpuPlayback = FPlatformMisc::GetEnvironmentVariable(TEXT("OK_STUDIO_GPU_PLAYBACK")) != TEXT("0");
		Actor->bUsePrivateGpuPlayback = bGpuPlayback;
		if (Object.Character.StudioAnimeNo >= 0)
		{
			// A Studio animation of the OK animation library (no Unity bundles).
			const FOKSceneCharacter& C = Object.Character;
			const FOKStudioAnim* Entry = FOKStudioCatalog::Get().FindAnim(C.StudioAnimeKind, C.StudioAnimeGroup, C.StudioAnimeCategory, C.StudioAnimeNo);
			FString AnimError;
			UOKAnimSequence* Seq = Entry && !Entry->File.IsEmpty() ? UOKAnimationLibrary::LoadOkAnimFile(LibraryDirectory() / Entry->File, nullptr, NAME_None, AnimError) : nullptr;
			// An H motion with variants blends them at the character's Studio controls (ApplyCharacterState sets them).
			TSharedPtr<FOkAnimPlayer> Player = Seq && !Entry->Variants.IsEmpty() ? StudioBlendPlayer(*Entry, AnimError) : nullptr;
			const bool bBlend = Player.IsValid();
			if (!Player && Seq) Player = Seq->CreatePlayer(AnimError);
			if (Player && Actor->SetOkAnimation(Player, false))
			{
				Actor->PoseMode = 3;
				Out.StudioAnim.Reset(Seq);
				if (bBlend) { Out.BlendControls = Entry->Controls; Out.BlendDefaults = Entry->ControlDefaults; Out.AppliedBlend.Init(-1.f, Entry->Controls.Num()); }
			}
			else Errors.Add(Object.Id, FString::Printf(TEXT("studio animation %d/%d/%d/%d not playable: %s"), C.StudioAnimeKind, C.StudioAnimeGroup, C.StudioAnimeCategory, C.StudioAnimeNo, Entry ? (Entry->File.IsEmpty() ? TEXT("not converted") : *AnimError) : TEXT("not in the library")));
		}
		else if (!Object.Character.AnimFile.IsEmpty())
		{
			// An .okanim file of its own (a video motion, an exported timeline).
			const FString File = FPaths::IsRelative(Object.Character.AnimFile) ? FPaths::ProjectDir() / Object.Character.AnimFile : Object.Character.AnimFile;
			FString AnimError;
			// A video motion (Tools/video_motion) is a session clip (OKCLIP01), played by the character's own session
			// under its "video:|<name>" key; the OK animation library's files (OKANIM) play through an anim player.
			TArray<uint8> Magic;
			const bool bVideo = FFileHelper::LoadFileToArray(Magic, *File, FILEREAD_Silent) && Magic.Num() >= 8 && FMemory::Memcmp(Magic.GetData(), "OKCLIP01", 8) == 0;
			if (bVideo)
			{
				const bool bInVideoDir = FPaths::IsSamePath(FPaths::GetPath(FPaths::ConvertRelativePathToFull(File)), FPaths::ConvertRelativePathToFull(FOkSession::VideoMotionDirectory()));
				if (!bInVideoDir || !Actor->SetGameAnimation(TEXT("video:|") + FPaths::GetBaseFilename(File)))
					Errors.Add(Object.Id, FString::Printf(TEXT("video motion %s not playable%s"), *Object.Character.AnimFile, bInVideoDir ? TEXT("") : TEXT(" (only Saved/VideoMotions plays)")));
			}
			else
			{
				UOKAnimSequence* Seq = UOKAnimationLibrary::LoadOkAnimFile(File, nullptr, NAME_None, AnimError);
				const TSharedPtr<FOkAnimPlayer> Player = Seq ? Seq->CreatePlayer(AnimError) : nullptr;
				if (Player && Actor->SetOkAnimation(Player, false)) { Actor->PoseMode = 3; Out.StudioAnim.Reset(Seq); }
				else Errors.Add(Object.Id, FString::Printf(TEXT("animation file %s not playable: %s"), *Object.Character.AnimFile, *AnimError));
			}
			if (Errors.Contains(Object.Id)) UE_LOG(LogTemp, Warning, TEXT("OK studio: %s: %s"), *Object.Name, *Errors[Object.Id]);
		}
		else if (Object.Character.Animation.IsValid())
		{
			const UOKAnimationAsset* Anim = Cast<UOKAnimationAsset>(Object.Character.Animation.TryLoad());
			FString AnimError;
			const TSharedPtr<FOkAnimPlayer> Player = Anim ? Anim->CreatePlayer(AnimError) : nullptr;
			if (Player && Actor->SetOkAnimation(Player, false)) Actor->PoseMode = 3;
			else Errors.Add(Object.Id, TEXT("animation not playable: ") + (Anim ? AnimError : Object.Character.Animation.ToString()));
		}
		return true;
	}
	case EOKSceneObjectKind::Item:
	{
		FString Model, Prefab;
		if (!ItemModel(Object.Item, Model, Prefab, OutError)) return false;
		// Item FK turns the item's bones before its meshes are baked; Material Editor shader and renderer edits apply then too.
		FOkSession Session;
		if (!Session.OpenOkModelFk(LibraryDirectory(), Model, Prefab, ItemBuildJson(Object.Item), OutError))
		{
			// Studio's helper items draw nothing in the game either (a turning pivot "obj無し", a 3D sound emitter): only a
			// place for their children, which the document already keeps. Not an error.
			if (OutError.Contains(TEXT("has no meshes"))) { OutError.Reset(); AddRenderCamera(Model, Prefab, Out); return true; }
			return false;
		}
		// A mod item made entirely with Unity's Standard shader (buildings, ground pieces) is lit as Standard is: the
		// item program's toon shading ignores its _Color, _Metallic and the light's angle (83283571_p1's concrete ground
		// under the water drew 1.8 times the native brightness). It draws with the map materials' Standard graph instead,
		// still on the items' lighting channel (Studio's character light).
		bool bStandardItem = Session.PartCount() > 0;
		for (int32 Part = 0; Part < Session.PartCount() && bStandardItem; ++Part)
			bStandardItem = Session.MaterialShaderName(Part).StartsWith(TEXT("Standard"), ESearchCase::IgnoreCase);
		AKKPreviewActor* Actor = SpawnPreview(World, MoveTemp(Session), OutError, Object.Item.Colors, PatternOverrides(Object.Item), &Object.Item, bStandardItem,
			Document ? Document->Post.ShaderGlobals() : TMap<FName, FLinearColor>(), Document ? Document->Post.RampG : -1);
		if (!Actor) return false;
		Out.Preview = Actor;
		FVector RootPosition, RootScale;
		FQuat RootRotation;
		if (FOkSession::OkModelNodeWorld(LibraryDirectory(), Model, Prefab, Prefab, RootPosition, RootRotation, RootScale))
		{
			const FTransform Root(FQuat(-RootRotation.X, RootRotation.Z, RootRotation.Y, RootRotation.W).GetNormalized(),
				FVector(-RootPosition.X, RootPosition.Z, RootPosition.Y) * 100.0, FVector(RootScale.X, RootScale.Z, RootScale.Y));
			Out.RenderCorrection = Root.Inverse();
		}
		AddRenderCamera(Model, Prefab, Out);
		return true;
	}
	case EOKSceneObjectKind::Light:
	{
		ULightComponent* Light = nullptr;
		switch (Object.Light.Type)
		{
		case EOKSceneLightType::Directional: { UDirectionalLightComponent* Sun = NewObject<UDirectionalLightComponent>(this); Sun->ForwardShadingPriority = 2000 + Object.Id; Light = Sun; break; }
		case EOKSceneLightType::Spot:
		{
			USpotLightComponent* Spot = NewObject<USpotLightComponent>(this);
			Spot->SetOuterConeAngle(Object.Light.SpotAngle);
			Spot->SetInnerConeAngle(Object.Light.InnerConeAngle());
			Spot->SetAttenuationRadius(Object.Light.Range);
			Light = Spot;
			break;
		}
		case EOKSceneLightType::Area:
		{
			URectLightComponent* Rect = NewObject<URectLightComponent>(this);
			Rect->SetSourceWidth(Object.Light.AreaWidth);
			Rect->SetSourceHeight(Object.Light.AreaHeight);
			Rect->SetAttenuationRadius(Object.Light.Range);
			Light = Rect;
			break;
		}
		default:
		{
			UPointLightComponent* Point = NewObject<UPointLightComponent>(this);
			Point->SetAttenuationRadius(Object.Light.Range);
			Light = Point;
		}
		}
		Light->SetMobility(EComponentMobility::Movable);
		Light->SetupAttachment(RootComponent);
		Light->SetIntensity(Object.Light.Intensity);
		Light->SetLightColor(Object.Light.Color);
		Light->SetCastShadows(Object.Light.bShadows);
		// Shadow softness is the light's size: a point or spot light's radius (up to 30 cm), the sun's angle (up to 10°).
		const float Soft = FMath::Clamp(Object.Light.ShadowSoftness, 0.f, 1.f);
		if (UDirectionalLightComponent* Sun = Cast<UDirectionalLightComponent>(Light)) Sun->LightSourceAngle = FMath::Lerp(0.5357f, 10.f, Soft);
		else if (UPointLightComponent* Point = Cast<UPointLightComponent>(Light)) Point->SourceRadius = Soft * 30.f;
		// Lighting channel 2 is the map's: every light but Studio's character light reaches it; channel 0 is the items'.
		Light->LightingChannels.bChannel0 = !Object.Light.bMapOnly;
		Light->LightingChannels.bChannel2 = !Object.Light.bCharactersOnly;
		Light->RegisterComponent();
		Out.Component = Light;
		return true;
	}
	case EOKSceneObjectKind::Effect:
	{
		Out.Effect = Object.Effect.Effect;
		const FOKStudioEffect* Entry = FOKStudioCatalog::Get().FindEffect(Object.Effect.Effect);
		if (!Entry) { OutError = TEXT("no effect ") + Object.Effect.Effect + TEXT(" in effects.json"); return false; }
		UNiagaraSystem* System = LoadObject<UNiagaraSystem>(nullptr, *Entry->Asset);
		if (!System) { OutError = TEXT("effect asset missing (make it with OKStudio.MakeEffects in the editor): ") + Entry->Asset; return false; }
		UNiagaraComponent* Effect = NewObject<UNiagaraComponent>(this);
		Effect->SetupAttachment(RootComponent);
		Effect->SetAsset(System);
		Effect->SetAutoActivate(true);
		Effect->RegisterComponent();
		Effect->Activate(true);
		// The entry's layers: other effects of the library under it, each tagged with its effect for its colour.
		for (const FOKStudioEffect::FLayer& Layer : Entry->Layers)
		{
			const FOKStudioEffect* LayerEntry = FOKStudioCatalog::Get().FindEffect(Layer.Effect);
			UNiagaraSystem* LayerSystem = LayerEntry ? LoadObject<UNiagaraSystem>(nullptr, *LayerEntry->Asset) : nullptr;
			if (!LayerSystem) { UE_LOG(LogTemp, Warning, TEXT("OK studio: effect %s layer %s missing"), *Entry->Id, *Layer.Effect); continue; }
			UNiagaraComponent* Sub = NewObject<UNiagaraComponent>(this);
			Sub->SetupAttachment(Effect);
			Sub->SetRelativeLocation(Layer.Offset);
			Sub->ComponentTags.Add(*(TEXT("OKEffectLayer:") + Layer.Effect));
			Sub->SetAsset(LayerSystem);
			Sub->SetAutoActivate(true);
			Sub->RegisterComponent();
			Sub->Activate(true);
		}
		Out.Component = Effect;
		return true;
	}
	case EOKSceneObjectKind::Camera:
	{
		UCameraComponent* Camera = NewObject<UCameraComponent>(this);
		Camera->SetupAttachment(RootComponent);
		Camera->SetFieldOfView(Object.Camera.Fov);
		Camera->RegisterComponent();
		Out.Component = Camera;
		return true;
	}
	default:
		return true;
	}
}

void AOKSceneActor::AddRenderCamera(const FString& Model, const FString& Prefab, FShown& Out)
{
	const FString Json = FOkSession::OkModelRenderCameras(LibraryDirectory(), Model, Prefab);
	TArray<TSharedPtr<FJsonValue>> Cameras;
	if (Json.IsEmpty() || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Cameras) || Cameras.IsEmpty()) return;
	const TSharedPtr<FJsonObject> C = Cameras[0]->AsObject();
	if (!C || Out.Component.IsValid()) return;
	auto Floats = [&C](const TCHAR* Field, int32 N)
	{
		TArray<float> V;
		const TArray<TSharedPtr<FJsonValue>>* List = nullptr;
		if (C->TryGetArrayField(Field, List)) for (const TSharedPtr<FJsonValue>& X : *List) V.Add(X->AsNumber());
		V.SetNumZeroed(N);
		return V;
	};
	const TArray<float> P = Floats(TEXT("pos"), 3), R = Floats(TEXT("rot"), 4);
	const int32 Key = C->GetIntegerField(TEXT("key"));
	const int32 W = FMath::Clamp(int32(C->GetIntegerField(TEXT("width"))), 16, 2048), H = FMath::Clamp(int32(C->GetIntegerField(TEXT("height"))), 16, 2048);
	TObjectPtr<UTextureRenderTarget2D>& Target = SharedRenderTargets.FindOrAdd(Key);
	if (!Target)
	{
		Target = NewObject<UTextureRenderTarget2D>(this);
		Target->RenderTargetFormat = RTF_RGBA8_SRGB;
		Target->ClearColor = FLinearColor::Black;
		Target->InitAutoFormat(W, H);
		Target->UpdateResourceImmediate(true);
	}
	USceneCaptureComponent2D* Capture = NewObject<USceneCaptureComponent2D>(this);
	Capture->SetupAttachment(RootComponent);
	const float Aspect = float(Target->SizeX) / float(FMath::Max<int32>(Target->SizeY, 1));
	if (C->GetBoolField(TEXT("orthographic")))
	{
		Capture->ProjectionType = ECameraProjectionMode::Orthographic;
		Capture->OrthoWidth = 2.f * float(C->GetNumberField(TEXT("ortho_size"))) * Aspect * 100.f;
	}
	else // Unity's field of view is vertical, Unreal's horizontal.
		Capture->FOVAngle = FMath::RadiansToDegrees(2.f * FMath::Atan(FMath::Tan(FMath::DegreesToRadians(float(C->GetNumberField(TEXT("fov")))) * 0.5f) * Aspect));
	Capture->bOverride_CustomNearClippingPlane = true;
	Capture->CustomNearClippingPlane = FMath::Max(float(C->GetNumberField(TEXT("near"))) * 100.f, 0.1f);
	Capture->MaxViewDistanceOverride = float(C->GetNumberField(TEXT("far"))) * 100.f;
	Capture->CaptureSource = SCS_FinalColorLDR;
	// Studio's image effects sit on the main camera only: this camera draws the shaders' colours as they are (no film
	// curve, fixed exposure, no bloom, filter, fog or vignette). Without it the capture fell back to Unreal's auto
	// exposure and the feeds came out white.
	FOKScenePost Plain;
	Plain.bBloom = Plain.bVignette = Plain.bDepthOfField = Plain.bFog = Plain.bAmbientOcclusion = Plain.bSunShafts = false;
	Plain.ColorFilter = -1;
	Plain.Exposure = 0.f;
	Plain.ApplyTo(Capture->PostProcessSettings);
	Capture->PostProcessBlendWeight = 1.f;
	// WireRenderTextures captures it every frame while a shown screen shows it.
	Capture->bCaptureEveryFrame = false;
	Capture->bCaptureOnMovement = false;
	Capture->TextureTarget = Target;
	Capture->RegisterComponent();
	// The camera node in the item (Unity prefab space, the prefab root undone as for the meshes), looking along Unity's +Z
	// (Unreal +Y) with +Y (Unreal +Z) up.
	FTransform RootInverse = FTransform::Identity;
	FVector RootPosition, RootScale;
	FQuat RootRotation;
	if (FOkSession::OkModelNodeWorld(LibraryDirectory(), Model, Prefab, Prefab, RootPosition, RootRotation, RootScale))
		RootInverse = FTransform(FQuat(-RootRotation.X, RootRotation.Z, RootRotation.Y, RootRotation.W).GetNormalized(), FVector(-RootPosition.X, RootPosition.Z, RootPosition.Y) * 100.0).Inverse();
	const FTransform Node(FQuat(-R[0], R[2], R[1], R[3]).GetNormalized(), FVector(-P[0], P[2], P[1]) * 100.0);
	const FTransform Look(FRotationMatrix::MakeFromXZ(FVector(0, 1, 0), FVector(0, 0, 1)).ToQuat());
	Out.ComponentOffset = Look * Node * RootInverse;
	Out.Component = Capture;
	Out.RenderCameraKey = Key;
	UE_LOG(LogTemp, Display, TEXT("OK studio: camera item %s key=%d %dx%d fov=%.1f"), *Prefab, Key, Target->SizeX, Target->SizeY, float(C->GetNumberField(TEXT("fov"))));
}

void AOKSceneActor::WireRenderTextures()
{
	// The first camera item of each key draws it (Unity: every camera of a RenderTexture draws in turn, the last one stays).
	TMap<int32, USceneCaptureComponent2D*> Live;
	TArray<USceneCaptureComponent2D*> All;
	bool bAnyScreen = false;
	for (const TPair<int32, FShown>& It : Shown)
	{
		if (It.Value.RenderCameraKey == 0) { bAnyScreen = bAnyScreen || (It.Value.Preview.IsValid() && It.Value.Preview->SharedRenderTextureScreens.Num()); continue; }
		USceneCaptureComponent2D* Capture = Cast<USceneCaptureComponent2D>(It.Value.Component.Get());
		if (!Capture) continue;
		All.Add(Capture);
		// Studio hides an item by turning its renderers off (OCIItem.visible): a hidden camera item still draws.
		if (!Live.Contains(It.Value.RenderCameraKey)) Live.Add(It.Value.RenderCameraKey, Capture);
	}
	if (!bAnyScreen && All.IsEmpty()) return;
	static UTexture* Black = LoadObject<UTexture2D>(nullptr, TEXT("/Engine/EngineResources/Black.Black"));
	TSet<int32> Watched;
	for (const TPair<int32, FShown>& It : Shown)
	{
		AKKPreviewActor* Preview = It.Value.Preview.Get();
		if (!Preview) continue;
		const bool bShown = !Document || Document->IsShown(It.Key);
		for (const AKKPreviewActor::FSharedRenderTextureScreen& Screen : Preview->SharedRenderTextureScreens)
		{
			UMaterialInstanceDynamic* Material = Screen.Material.Get();
			if (!Material) continue;
			USceneCaptureComponent2D* const* Capture = Live.Find(Screen.Key);
			UTexture* Want = Capture ? (*Capture)->TextureTarget.Get() : Black;
			UTexture* Has = nullptr;
			if (!Material->GetTextureParameterValue(FName(TEXT("Screen")), Has) || Has != Want) Material->SetTextureParameterValue(TEXT("Screen"), Want);
			if (!Capture || !bShown) continue;
			Watched.Add(Screen.Key);
			// Not filmed by its own camera: the unlit screen showing itself feeds back to white.
			if (UPrimitiveComponent* Comp = Screen.Component.Get()) (*Capture)->HideComponent(Comp);
		}
	}
	for (USceneCaptureComponent2D* Capture : All)
	{
		const bool bWatched = Live.FindKey(Capture) && Watched.Contains(*Live.FindKey(Capture));
		Capture->bCaptureEveryFrame = bWatched;
	}
	// OK_RT_DUMP=1 (debugging): 5 s after the first camera item shows, each shown camera item's picture to
	// Saved/OKStudio/studio_rt_<key>.png and its pose (Unity metres, view direction) to the log.
	static const bool bDump = FPlatformMisc::GetEnvironmentVariable(TEXT("OK_RT_DUMP")) == TEXT("1");
	static bool bDumpScheduled = false;
	if (bDump && Live.Num() && !bDumpScheduled)
	{
		bDumpScheduled = true;
		TWeakObjectPtr<AOKSceneActor> Weak(this);
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Weak](float) { if (Weak.IsValid()) Weak->DumpRenderTextures(); return false; }), 5.f);
	}
}

void AOKSceneActor::DumpRenderTextures()
{
	TMap<int32, USceneCaptureComponent2D*> Live;
	for (const TPair<int32, FShown>& It : Shown)
		if (USceneCaptureComponent2D* Capture = It.Value.RenderCameraKey ? Cast<USceneCaptureComponent2D>(It.Value.Component.Get()) : nullptr)
			if (!Live.Contains(It.Value.RenderCameraKey)) Live.Add(It.Value.RenderCameraKey, Capture);
	for (const TPair<int32, USceneCaptureComponent2D*>& It : Live)
	{
			UTextureRenderTarget2D* Target = It.Value->TextureTarget.Get();
			FTextureRenderTargetResource* Resource = Target ? Target->GameThread_GetRenderTargetResource() : nullptr;
			TArray<FColor> Pixels;
			if (!Resource || !Resource->ReadPixels(Pixels)) continue;
			for (FColor& P : Pixels) P.A = 255;
			TArray64<uint8> Png;
			FImageUtils::PNGCompressImageArray(Target->SizeX, Target->SizeY, Pixels, Png);
			const FString Path = FPaths::ProjectSavedDir() / FString::Printf(TEXT("OKStudio/studio_rt_%d.png"), It.Key);
			FFileHelper::SaveArrayToFile(Png, *Path);
			const FVector L = It.Value->GetComponentLocation() / 100.0, F = It.Value->GetForwardVector();
			UE_LOG(LogTemp, Display, TEXT("OK studio: camera item key=%d at unity (%.3f,%.3f,%.3f) looking (%.3f,%.3f,%.3f) fov=%.1f -> %s"), It.Key, -L.X, L.Z, L.Y, -F.X, F.Z, F.Y, It.Value->FOVAngle, *Path);
		}
}

void AOKSceneActor::Release(FShown& Item)
{
	if (Item.Preview.IsValid()) { Item.Preview->Destroy(); ++ReleasedSinceCollect; }
	if (Item.Component.IsValid()) Item.Component->DestroyComponent();
	Item.Preview = nullptr;
	Item.bFixedEyesApplied = false;
	Item.AppliedFixedEyes.Reset();
	Item.AppliedFixedEyesPattern = -1;
	Item.EyesOpenBefore = -1.f;
	Item.Component = nullptr;
}

void AOKSceneActor::ApplyCharacterState(const FOKSceneObject& Object, FShown& Item)
{
	AKKPreviewActor* Actor = Item.Preview.Get();
	if (!Actor) return;
	const FOKSceneCharacter& C = Object.Character;
	// The eyes pattern is a cha_eyeset row: its FaceBlendShape pattern is the row's EyesPtn (ChaControl.ChangeEyesPtn).
	const bool bHasAnimFace = Actor->OkPlayer && Actor->OkPlayer->Curves(0, 0.f).Contains(TEXT("face.mouth_open"));
	if (!bHasAnimFace && C.EyesPattern >= 0)
	{
		const int32 Blend = FOKStudioCatalog::Get().EyeSet(C.EyesPattern).BlendPattern;
		Actor->SetFacePattern(1, Blend >= 0 ? Blend : C.EyesPattern, false);
	}
	if (!bHasAnimFace && C.EyebrowPattern >= 0) Actor->SetFacePattern(2, C.EyebrowPattern, false);
	if (!bHasAnimFace && C.MouthPattern >= 0) Actor->SetMouthPattern(C.MouthPattern, false);
	if (!bHasAnimFace && C.MouthOpen >= 0.f) Actor->SetMouthOpen(C.MouthOpen);
	// Eye opening: the eye control's fixed open rate on its current pattern (ChaControl.ChangeEyesOpenMax).
	if (C.EyesOpen >= 0.f)
	{
		if (Item.EyesOpenBefore < 0.f) { Item.EyesOpenBefore = Actor->FaceOpen01[0]; Item.bEyesManualBefore = Actor->bManualFaceControl[0]; }
		Actor->SetFaceControl(1, Actor->FacePattern[0], C.EyesOpen, false);
	}
	else if (Item.EyesOpenBefore >= 0.f)
	{
		// Back to the character's own opening.
		Actor->FaceOpen01[0] = Item.EyesOpenBefore;
		Actor->bManualFaceControl[0] = Item.bEyesManualBefore;
		Item.EyesOpenBefore = -1.f;
	}
	if (bHasAnimFace) { if (Actor->bAutomaticBlink) Actor->SetAutomaticBlink(false); } else if (C.Blink >= 0 && Actor->bAutomaticBlink != (C.Blink == 1)) Actor->SetAutomaticBlink(C.Blink == 1);
	// Studio controls of a blended H motion (option 1 height, option 2 breast, pattern): the scene's value, else the
	// controller's own.
	for (int32 I = 0; I < Item.BlendControls.Num() && Actor->OkPlayer; ++I)
	{
		const FOKSceneAnimFloat* Want = C.AnimFloats.FindByPredicate([&Item, I](const FOKSceneAnimFloat& F) { return F.Name == Item.BlendControls[I]; });
		const float Value = FMath::Clamp(Want ? Want->Value : Item.BlendDefaults[I], 0.f, 1.f);
		if (Item.AppliedBlend[I] != Value) { Actor->OkPlayer->SetParam(I, Value); Item.AppliedBlend[I] = Value; }
	}
	// Hand shapes: a pattern overrides the animated fingers of that hand (-1 gives them back).
	const int32 Hands[2] = { C.HandLeft, C.HandRight };
	const int32 Count = Actor->GetHandShapeCount();
	for (int32 Side = 0; Side < 2; ++Side)
	{
		const int32 Pattern = Count > 0 ? FMath::Clamp(Hands[Side], -1, Count - 1) : -1;
		if (Pattern == Item.AppliedHands[Side]) continue;
		if (Actor->SetHandShape(Side, Pattern >= 0, FMath::Max(Pattern, 0), FMath::Max(Pattern, 0), 0.f)) Item.AppliedHands[Side] = Pattern;
	}
	// FK re-poses the character (no rebuild); a fresh character starts without FK.
	if (C.Fk != Item.AppliedFk || C.FkPartsOff != Item.AppliedFkPartsOff)
	{
		Item.AppliedFkPartsOff = C.FkPartsOff;
		TArray<FString> Bones;
		TArray<FVector> Euler;
		// A switched-off part's bones follow the animation (their values are kept for when it comes back on).
		for (const FOKSceneFkBone& B : C.Fk) if (C.FkPartOn(FOKSceneCharacter::FkPartOf(B.Bone))) { Bones.Add(B.Bone); Euler.Add(B.Euler); }
		if (Actor->SetUserFk(Bones, Euler)) Item.AppliedFk = C.Fk;
		else Errors.Add(Object.Id, TEXT("FK not applied"));
	}
	if (C.Ik != Item.AppliedIk || C.IkPartsOff != Item.AppliedIkPartsOff)
	{
		Item.AppliedIkPartsOff = C.IkPartsOff;
		TArray<int32> Effectors;
		TArray<FVector> Positions;
		TArray<FQuat> Rotations;
        TArray<uint8> UseRotations;
		TArray<float> Weights;
        for (const FOKSceneIkTarget& T : C.Ik)
        {
            // A switched-off part's targets are kept but do not pull.
            if (!C.IkPartOn(FOKSceneCharacter::IkPartOf(T.Effector))) continue;
            Effectors.Add(T.Effector);
            Weights.Add(T.Weight);
            Positions.Add(AKKPreviewActor::ActorLocalToUnity(T.Position));
            const FQuat U = FOKStudioImport::UnityToUe(FVector::ZeroVector,T.Euler,FVector::OneVector).GetRotation();
            Rotations.Add(FQuat(-U.X,U.Z,U.Y,U.W));
            UseRotations.Add(T.bUseRotation ? 1 : 0);
        }
		FString Error;
		if (Actor->SetStudioIk(Effectors, Positions, Error, Rotations, UseRotations))
		{
			// Weights after the targets (setting targets resets them to 1); all-1 needs no call.
			if (Weights.ContainsByPredicate([](float W) { return W < 1.f; })) Actor->Session.SetStudioIkWeights(Weights);
			Item.AppliedIk = C.Ik;
		}
		else Errors.Add(Object.Id, TEXT("IK not applied: ") + Error);
	}
	// Joint correction: the eight Studio categories, bust (8) and accessory follow (9) always on.
	const int32 Joints = (C.JointCorrection & 0xff) | 0x300;
	if (Joints != Item.AppliedJointCorrection)
	{
		if (Actor->Session.SetExpressionMask(uint32(Joints)))
		{
			Item.AppliedJointCorrection = Joints;
			Actor->bUserPoseDirty = true;
		}
		else Errors.Add(Object.Id, TEXT("joint correction not applied"));
	}
    const TArray<FQuat> Neck = C.NeckLook == EOKSceneLook::Fixed ? C.FixedNeck : TArray<FQuat>();
    if (Neck != Item.AppliedFixedNeck)
    {
        if (Actor->Session.SetStudioFixedNeck(Neck))
        {
            Item.AppliedFixedNeck = Neck;
            Actor->bUserPoseDirty = true;
            Actor->ApplyPoseFrame(0.f,false);
        }
        else Errors.Add(Object.Id,TEXT("fixed neck not applied"));
    }
}

void AOKSceneActor::Sync()
{
	bSkyDomesDirty = true;
	++PlaceGeneration;
	if (!Document) { for (auto& Pair : Shown) Release(Pair.Value); Shown.Reset(); return; }
	SyncMap();
	if (!Document->BoneFrame)
		Document->BoneFrame = [Self = TWeakObjectPtr<AOKSceneActor>(this)](int32 Character, const FString& Bone, FTransform& Out)
		{
			const FShown* S = Self.IsValid() ? Self->Shown.Find(Character) : nullptr;
			AKKPreviewActor* Preview = S ? S->Preview.Get() : nullptr;
			float M[16];
			if (!Preview || !Preview->Session.BoneWorldMatrix(Bone, M)) return false;
			Out = FTransform(OKSkinPaletteToUnreal(M));
			return true;
		};
	// Objects are built a batch per frame: a scene of thousands built in one frame never lets the renderer upload and
	// free what it queued (memory ran past 28 GB). The rest waits for the next Tick (bBuildsPending).
	int32 Built = 0;
	bBuildsPending = false;
	TSet<int32> Present;
	for (const FOKSceneObject& Object : Document->Objects)
	{
		Present.Add(Object.Id);
		FShown* Item = Shown.Find(Object.Id);
		// Content changes rebuild the object; transform and visibility only move it.
		// An eyes pattern is set on the live face, unless its eye set changes parts (gag eyes): then it is built in.
		auto PartsEyes = [](int32 Ptn) { return Ptn >= 0 && FOKStudioCatalog::Get().EyeSet(Ptn).bChangesParts ? Ptn : -1; };
		const bool bSame = Item && Item->Kind == Object.Kind && Item->Character.SameContent(Object.Character) && Item->Item == Object.Item && Item->Light == Object.Light
			&& (Object.Kind != EOKSceneObjectKind::Effect || Item->Effect == Object.Effect.Effect)
			&& (Object.Kind != EOKSceneObjectKind::Character || PartsEyes(Item->Character.EyesPattern) == PartsEyes(Object.Character.EyesPattern));
		if (!bSame && Built >= BuildsPerFrame)
		{
			// Over this frame's budget: an object already shown keeps its old look until its turn, a new one waits.
			bBuildsPending = true;
			if (!Item) continue;
		}
		else if (!bSame)
		{
			++Built;
			if (Item) Release(*Item);
			FShown Fresh;
			FString Error;
			Errors.Remove(Object.Id);
			UE_LOG(LogTemp, Display, TEXT("OK studio: building %s (%d)"), *Object.Name, Object.Id);
			LastBuildFrame = GFrameCounter;
			if (!Build(Object, Fresh, Error))
			{
				Errors.Add(Object.Id, Error);
				UE_LOG(LogTemp, Warning, TEXT("OK studio: %s (%d) not shown: %s"), *Object.Name, Object.Id, *Error);
			}
			Item = &Shown.Add(Object.Id, Fresh);
			if (Object.Kind == EOKSceneObjectKind::Character) { ApplyCharacterState(Object, *Item); SetAnimationTime(Object.Id, 0.f); }
		}
		else if (Object.Kind == EOKSceneObjectKind::Character)
		{
			ApplyCharacterState(Object, *Item);
			// A stopped animation (speed 0) shows the frame at its time: the UI and timeline keys scrub it.
			if (Object.Character.Speed == 0.f && Item->Time != Object.Character.Time) SetAnimationTime(Object.Id, 0.f);
		}
		if (Object.Kind == EOKSceneObjectKind::Camera)
		{
			if (UCameraComponent* Camera = Cast<UCameraComponent>(Item->Component.Get())) Camera->SetFieldOfView(Object.Camera.Fov);
		}
		if (Object.Kind == EOKSceneObjectKind::Effect)
		{
			if (UNiagaraComponent* Effect = Cast<UNiagaraComponent>(Item->Component.Get()))
			{
				Effect->SetCustomTimeDilation(FMath::Max(Object.Effect.Speed, 0.f));
				Effect->SetVariableFloat(TEXT("User.Intensity"), Object.Effect.Intensity);
				// The particles' colour (the effect's own unless the object sets one), brightened by the intensity.
				const FOKStudioEffect* Def = FOKStudioCatalog::Get().FindEffect(Object.Effect.Effect);
				FLinearColor Color = Object.Effect.Color.A >= 0.f ? Object.Effect.Color : (Def ? Def->Color : FLinearColor::White);
				const float Intensity = FMath::Max(Object.Effect.Intensity, 0.f);
				const float Alpha = Def ? Def->Color.A : 1.f;
				Effect->SetVariableLinearColor(TEXT("User.Color"), FLinearColor(Color.R * Intensity, Color.G * Intensity, Color.B * Intensity, Alpha));
				// Layers keep their own effect's colour, with the object's speed and intensity.
				for (USceneComponent* Child : Effect->GetAttachChildren())
					if (UNiagaraComponent* Sub = Cast<UNiagaraComponent>(Child))
						for (const FName& Tag : Sub->ComponentTags)
							if (const FString T = Tag.ToString(); T.StartsWith(TEXT("OKEffectLayer:")))
								if (const FOKStudioEffect* LayerDef = FOKStudioCatalog::Get().FindEffect(T.Mid(14)))
								{
									const FLinearColor L = LayerDef->Color;
									Sub->SetCustomTimeDilation(FMath::Max(Object.Effect.Speed, 0.f));
									Sub->SetVariableLinearColor(TEXT("User.Color"), FLinearColor(L.R * Intensity, L.G * Intensity, L.B * Intensity, L.A));
								}
			}
		}
		const FTransform World = Document->WorldTransform(Object.Id) * GetActorTransform();
		// OK_STUDIO_ITEM_DUMP=<group/category/no>: that item's world transform (Unreal), to compare with Studio's.
		static const FString ItemDump = FPlatformMisc::GetEnvironmentVariable(TEXT("OK_STUDIO_ITEM_DUMP"));
		if (!ItemDump.IsEmpty() && Object.Kind == EOKSceneObjectKind::Item
			&& (ItemDump == TEXT("all") || ItemDump == FString::Printf(TEXT("%d/%d/%d"), Object.Item.Group, Object.Item.Category, Object.Item.No)))
		{
			const FVector Y = World.GetRotation().RotateVector(FVector(0, 0, 1)), Z = World.GetRotation().RotateVector(FVector(0, 1, 0));
			UE_LOG(LogTemp, Display, TEXT("OK studio item dump %d/%d/%d id=%d c1=(%.2f,%.2f,%.2f) unity pos=(%.3f,%.3f,%.3f) axisY=(%.3f,%.3f,%.3f) axisZ=(%.3f,%.3f,%.3f) scale=(%.3f,%.3f,%.3f)"), Object.Item.Group, Object.Item.Category, Object.Item.No, Object.Id,
				Object.Item.Colors.Num() ? Object.Item.Colors[0].R : -1.f, Object.Item.Colors.Num() ? Object.Item.Colors[0].G : -1.f, Object.Item.Colors.Num() ? Object.Item.Colors[0].B : -1.f,
				-World.GetLocation().X / 100.0, World.GetLocation().Z / 100.0, World.GetLocation().Y / 100.0,
				-Y.X * World.GetScale3D().Z, Y.Z * World.GetScale3D().Z, Y.Y * World.GetScale3D().Z, -Z.X * World.GetScale3D().Y, Z.Z * World.GetScale3D().Y, Z.Y * World.GetScale3D().Y,
				World.GetScale3D().X, World.GetScale3D().Z, World.GetScale3D().Y);
		}
		const bool bShown = Document->IsShown(Object.Id);
		if (AKKPreviewActor* Preview = Item->Preview.Get())
		{
			Preview->SetActorTransform(Compose(Item->RenderCorrection, World));
			Preview->SetActorHiddenInGame(!bShown);
			if (USceneComponent* Root = Preview->GetRootComponent()) Root->SetVisibility(bShown, true);
		}
		if (USceneComponent* Component = Item->Component.Get())
		{
			Component->SetWorldTransform(Item->ComponentOffset * World);
			// A camera item's capture keeps drawing while the item is hidden (Studio hides only renderers).
			if (Item->RenderCameraKey == 0) Component->SetVisibility(bShown, true);
		}
	}
	for (auto It = Shown.CreateIterator(); It; ++It)
	{
		if (!Present.Contains(It.Key())) { Release(It.Value()); Errors.Remove(It.Key()); It.RemoveCurrent(); }
	}
	WireRenderTextures();
	// A destroyed preview's meshes, textures and materials are freed by garbage collection, which the engine runs only
	// now and then: after another scene is loaded (or many objects go) collect now, or scenes opened one after another
	// pile up gigabytes.
	if (ReleasedSinceCollect >= 32 && GEngine)
	{
		ReleasedSinceCollect = 0;
		GEngine->ForceGarbageCollection(true);
	}
}

void AOKSceneActor::SyncMap()
{
	// The map stands at the scene's map transform (Studio's map position / rotation).
	const FTransform MapPlace = Document->MapTransform * GetActorTransform();
	if (Document->Map == ShownMap)
	{
		for (const TWeakObjectPtr<AKKPreviewActor>& P : ShownMapPreviews)
			if (P.IsValid() && !P->GetActorTransform().Equals(MapPlace)) P->SetActorTransform(MapPlace);
		ApplyMapTime(MapPlace);
		return;
	}
	ShownMapTime = -2;
	for (const TPair<TWeakObjectPtr<ULightComponent>, uint32>& L : MapLights) if (L.Key.IsValid()) L.Key->DestroyComponent();
	MapLights.Reset();
	for (const TWeakObjectPtr<AKKPreviewActor>& P : ShownMapPreviews) if (P.IsValid()) { P->Destroy(); ReleasedSinceCollect += 32; }
	ShownMapPreviews.Reset();
	ShownMap = Document->Map;
	MapError.Reset();
	if (ShownMap < 0) { ApplyMapTime(MapPlace); return; }
	const FOKStudioCatalogMap* Entry = FOKStudioCatalog::Get().FindMap(ShownMap);
	if (!Entry) { MapError = FString::Printf(TEXT("no studio map %d (%s)"), ShownMap, *FOKStudioCatalog::Get().Error); return; }
	UE_LOG(LogTemp, Display, TEXT("OK studio: building map %s (%d) at %s"), *Entry->Name, ShownMap, *Document->MapTransform.ToString());
	// A scene map shows each scene root that has meshes (controllers without renderers are skipped).
	TArray<FString> Failed;
	for (const FString& Root : Entry->Roots)
	{
		FOkSession Session;
		FString Error;
		AKKPreviewActor* Actor = Session.OpenOkModel(LibraryDirectory(), Entry->Model, Root, false, Error) ? SpawnPreview(GetWorld(), MoveTemp(Session), Error, {}, {}, nullptr, true) : nullptr;
		if (!Actor) { Failed.Add(Root + TEXT(": ") + Error); continue; }
		Actor->SetActorTransform(MapPlace);
        Actor->Tags.Add(FName(*(TEXT("OKStudioMapRoot=")+Root)));
		// The map is on its own lighting channel: Studio's character light (it turns with the camera) does not light it.
		TInlineComponentArray<UPrimitiveComponent*> Primitives(Actor);
		for (UPrimitiveComponent* P : Primitives) P->SetLightingChannels(false, false, !P->ComponentHasTag(TEXT("OKSunExcluded")));
		ShownMapPreviews.Add(Actor);
		AddMapLights(Actor, Entry->Model, Root);
	}
	if (ShownMapPreviews.IsEmpty()) MapError = FString::Join(Failed, TEXT("; "));
	ApplyMapTime(MapPlace);
}

// Preserve the source renderer shadow policy, including invisible room occluders,
// before applying the authored sun. Fully baked lights do not add a dynamic term.
static TAutoConsoleVariable<int32> CVarOkStudioMapSun(TEXT("ok.Studio.MapSun"), 1,
	TEXT("1: light a Studio map with its own sun (SunLightInfo._targetLight at the scene's time of day, casting shadows); 0: maps show only their lightmaps and emission."));

void AOKSceneActor::ApplyMapTime(const FTransform& MapPlace)
{
	AKKPreviewActor* Map = MapPreview();
	// The map's times (okmodel_view: `_OKSun<t>Color` on its parts). Map.RangeTimeZone: the scene's time when the map
	// has it, else its first.
	FLinearColor Color, Angle;
	auto Value = [&](const TCHAR* Suffix, int32 T, FLinearColor& Out)
	{
		if (!Map || Map->Session.PartCount() <= 0) return false;
		const FTCHARToUTF8 Name(*FString::Printf(TEXT("_OKSun%d%s"), T, Suffix));
		return Map->Session.MaterialValue(0, Name.Get(), Out);
	};
	int32 Time = -1;
	if (Value(TEXT("Color"), Document->Post.SunType, Color)) Time = Document->Post.SunType;
	for (int32 T = 0; T < 4 && Time < 0; ++T) if (Value(TEXT("Color"), T, Color)) Time = T;
	int32 Components = 0;
	for (const TWeakObjectPtr<AKKPreviewActor>& P : ShownMapPreviews) if (P.IsValid()) Components += P->GetComponents().Num();
	FLinearColor Lightmapping;
	const bool bBakedSun = Map && Map->Session.MaterialValue(0, "_OKSunLightmapping", Lightmapping) && FMath::RoundToInt(Lightmapping.R) == 2;
	const bool bSun = Time >= 0 && !bBakedSun && CVarOkStudioMapSun.GetValueOnGameThread() != 0;
	if (Time == ShownMapTime && Components == ShownMapComponents && (MapSun != nullptr) == bSun && (!bSun || ShownMapSunPlace.Equals(GetActorTransform()))) return;
	ShownMapTime = Time;
	ShownMapComponents = Components;
	ShownMapSunPlace = GetActorTransform();
	// SunLightInfo.Set: every time's visibleList hidden, then the current time's shown (okmodel_view::mark_sun_visible).
	for (const TWeakObjectPtr<AKKPreviewActor>& P : ShownMapPreviews)
	{
		if (!P.IsValid()) continue;
		TInlineComponentArray<UPrimitiveComponent*> Primitives(P.Get());
		for (UPrimitiveComponent* C : Primitives)
		{
			for (const FName& Tag : C->ComponentTags)
			{
				const FString S = Tag.ToString();
				if (S.StartsWith(TEXT("OKSunVisible"))) C->SetVisibility(Time >= 0 && ((FCString::Atoi(*S.Mid(12)) >> Time) & 1) != 0);
			}
			// The sun casts shadows (Unity: soft shadows), so the map shades itself: indoors it comes in by the windows.
			C->SetCastShadow(bSun && !C->ComponentHasTag(TEXT("OKShadowOff")));
			if (C->ComponentHasTag(TEXT("OKRendererDisabled"))) C->SetVisibility(false);
		}
	}
	// The map's lamps of a time of day show with that time's list (SunLightInfo.Set), the others always.
	for (const TPair<TWeakObjectPtr<ULightComponent>, uint32>& L : MapLights)
		if (L.Key.IsValid()) L.Key->SetVisibility(L.Value == 0 || (Time >= 0 && ((L.Value >> Time) & 1) != 0));
	ItemsLit.Reset();
	MapSunMask = 0;
	if (!bSun)
	{
		if (MapSun) { MapSun->DestroyComponent(); MapSun = nullptr; }
		return;
	}
	Value(TEXT("Color"), Time, Color);
	Value(TEXT("Angle"), Time, Angle);
	if (!MapSun)
	{
		MapSun = NewObject<UDirectionalLightComponent>(this, TEXT("MapSun"));
		MapSun->ForwardShadingPriority = 1000;
		MapSun->SetMobility(EComponentMobility::Movable);
		MapSun->SetupAttachment(RootComponent);
		MapSun->SetCastShadows(true);
		// The sun's culling mask is the map's layer: it lights the map (lighting channel 2), not the characters.
		MapSun->LightingChannels.bChannel0 = false;
		MapSun->LightingChannels.bChannel2 = true;
		MapSun->RegisterComponent();
	}
	// SetLight: the light's world Euler angles (Unity's light shines along its +Z, an Unreal light along +X), colour,
	// intensity. Unity Standard's Lambert diffuse is light * albedo; UE's physical
	// Lambert BRDF divides irradiance by pi. Authored Unity light Color is sRGB.
	const FQuat Turn(FVector::UpVector, UE_HALF_PI);
	MapSun->SetWorldRotation(GetActorQuat() * FOKStudioImport::UnityToUe(FVector::ZeroVector, FVector(Angle.R, Angle.G, Angle.B), FVector::OneVector).GetRotation() * Turn);
    const FLinearColor Irradiance=OKStudioLinearLightColor(FLinearColor(Color.R,Color.G,Color.B,1),Color.A);
    // ULightComponent stores an sRGB FColor; move HDR energy into intensity
    // before that quantization, preserving the source spectral ratios.
    const float Energy=FMath::Max3(Irradiance.R,Irradiance.G,Irradiance.B);
    MapSun->SetLightColor(Energy>0 ? FLinearColor(Irradiance.R/Energy,Irradiance.G/Energy,Irradiance.B/Energy,1) : FLinearColor::Black);
    MapSun->SetIntensity(Energy*UE_PI);
	// The items' forward light when it reaches their layer (UpdateItemLighting): Unity (-x, z, y) towards the light.
	FLinearColor Lo, Hi, Low24;
	if (Map->Session.MaterialValue(0, "_OKSunCullingLo", Lo) && Map->Session.MaterialValue(0, "_OKSunCullingHi", Hi))
		MapSunMask = uint32(FMath::RoundToInt(Lo.R)) | (uint32(FMath::RoundToInt(Hi.R)) << 16);
	else if (Map->Session.MaterialValue(0, "_OKSunCulling", Low24)) MapSunMask = uint32(FMath::RoundToInt(Low24.R));
	const FVector SunForward = MapSun->GetForwardVector();
	const FVector Towards = FVector(SunForward.X, -SunForward.Z, -SunForward.Y).GetSafeNormal();
	MapSunItemDirection = FLinearColor(Towards.X, Towards.Y, Towards.Z, 0.f);
	MapSunItemColor = FLinearColor(Irradiance.R, Irradiance.G, Irradiance.B, 1.f);
	UE_LOG(LogTemp, Display, TEXT("OK studio: map time %d sun angle=(%.1f,%.1f,%.1f) colour=(%.2f,%.2f,%.2f) intensity=%.2f"), Time, Angle.R, Angle.G, Angle.B, Color.R, Color.G, Color.B, Color.A);
}

// A Studio map's realtime lamps (point, spot and extra directional lights the map scene keeps unbaked): Unreal lights
// on the map's lighting channel, at their node of the map prefab. Unity's point / spot attenuation (its falloff
// texture, 1 / (1 + 25 (d / range)^2), cut at the range) is approximated by Unreal's non-inverse-squared falloff.
void AOKSceneActor::AddMapLights(AKKPreviewActor* Map, const FString& Model, const FString& Root)
{
	const FString Json = FOkSession::OkModelMapLights(LibraryDirectory(), Model, Root);
	TArray<TSharedPtr<FJsonValue>> Lights;
	if (!Map || Json.IsEmpty() || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Lights)) return;
	// Each directional light gets its own forward shading priority (the editor build reports equal ones on screen): the
	// map's lights in order, under the map sun (1000) and the scene's own lights (2000 + id).
	int32 DirectionalPriority = 0;
	for (const TSharedPtr<FJsonValue>& V : Lights)
	{
		const TSharedPtr<FJsonObject> L = V->AsObject();
		if (!L) continue;
		auto Floats = [&L](const TCHAR* Field, int32 N)
		{
			TArray<float> Out;
			const TArray<TSharedPtr<FJsonValue>>* List = nullptr;
			if (L->TryGetArrayField(Field, List)) for (const TSharedPtr<FJsonValue>& X : *List) Out.Add(X->AsNumber());
			Out.SetNumZeroed(N);
			return Out;
		};
		const TArray<float> P = Floats(TEXT("pos"), 3), R = Floats(TEXT("rot"), 4), C = Floats(TEXT("color"), 4);
		const int32 Kind = L->GetIntegerField(TEXT("kind"));
		const float Range = float(L->GetNumberField(TEXT("range"))) * 100.f;
		ULightComponent* Light = nullptr;
		if (Kind == 1) { UDirectionalLightComponent* Sun = NewObject<UDirectionalLightComponent>(Map); Sun->ForwardShadingPriority = DirectionalPriority++; Light = Sun; }
		else if (Kind == 0)
		{
			USpotLightComponent* Spot = NewObject<USpotLightComponent>(Map);
			const float Half = float(L->GetNumberField(TEXT("spot_angle"))) * 0.5f;
			Spot->SetOuterConeAngle(Half);
			Spot->SetInnerConeAngle(Half * 0.8f);
			Light = Spot;
		}
		else Light = NewObject<UPointLightComponent>(Map);
		if (UPointLightComponent* Local = Cast<UPointLightComponent>(Light))
		{
			Local->SetAttenuationRadius(Range);
			Local->bUseInverseSquaredFalloff = false;
			Local->LightFalloffExponent = 8.f;
		}
		Light->SetMobility(EComponentMobility::Movable);
		Light->SetupAttachment(Map->GetRootComponent());
		// Unity prefab space (-x, z, y); a Unity light shines along its +Z, an Unreal light along +X.
		const FQuat Node = FQuat(-R[0], R[2], R[1], R[3]).GetNormalized();
		Light->SetRelativeLocation(FVector(-P[0], P[2], P[1]) * 100.0);
		Light->SetRelativeRotation(Node * FRotationMatrix::MakeFromXZ(FVector(0, 1, 0), FVector(0, 0, 1)).ToQuat());
		const FLinearColor Irradiance = OKStudioLinearLightColor(FLinearColor(C[0], C[1], C[2], 1), float(L->GetNumberField(TEXT("intensity"))));
		const float Energy = FMath::Max3(Irradiance.R, Irradiance.G, Irradiance.B);
		Light->SetLightColor(Energy > 0 ? FLinearColor(Irradiance.R / Energy, Irradiance.G / Energy, Irradiance.B / Energy, 1) : FLinearColor::Black);
		Light->SetIntensity(Energy * UE_PI);
		Light->SetCastShadows(L->GetIntegerField(TEXT("shadows")) != 0);
		// The culling mask: the map layer (11) is the map's lighting channel, the Chara layer (10) the characters'.
		const uint32 Mask = uint32(L->GetNumberField(TEXT("culling_mask")));
		Light->LightingChannels.bChannel0 = (Mask & (1u << 10)) != 0;
		Light->LightingChannels.bChannel2 = (Mask & (1u << 11)) != 0;
		Light->RegisterComponent();
		MapLights.Emplace(Light, uint32(L->GetNumberField(TEXT("times"))));
		UE_LOG(LogTemp, Display, TEXT("OK studio: map lamp %s kind=%d intensity=%.2f range=%.1f mask=%u times=%u"), *L->GetStringField(TEXT("name")), Kind,
			float(L->GetNumberField(TEXT("intensity"))), Range / 100.f, Mask, uint32(L->GetNumberField(TEXT("times"))));
	}
}

int32 AOKSceneActor::MapPartCount() const
{
	int32 Parts = 0;
	for (const TWeakObjectPtr<AKKPreviewActor>& P : ShownMapPreviews) if (P.IsValid()) Parts += P->Session.PartCount();
	return Parts;
}

void AOKSceneActor::AdvanceAnimations(float DeltaSeconds)
{
    AdvanceCharacterFrames(DeltaSeconds, true);
}

namespace
{
	TAutoConsoleVariable<int32> CVarParallelPose(TEXT("ok.Studio.ParallelPose"), 1,
		TEXT("1: OK Studio solves its characters' poses (animation, hair and cloth) on worker threads at once, then refreshes their display on the game thread. 0: one after another on the game thread."));
	TAutoConsoleVariable<int32> CVarPipelinePose(TEXT("ok.Studio.PipelinePose"), 1,
		TEXT("With ok.Studio.ParallelPose: 1 refreshes each character's display as soon as its own solve is done (solves as tasks); 0 solves all (ParallelFor, the game thread helping) then refreshes. 1 by default: 5065 (9 characters) 67 -> 74 fps in A/B, 6964 the same."));
	TAutoConsoleVariable<int32> CVarSharePose(TEXT("ok.Studio.SharePose"), 1,
		TEXT("1: a Studio character's skeletal parts take the body's pose as their leader instead of each copying it."));
}

void AOKSceneActor::AdvanceCharacterFrames(float DeltaSeconds, bool bAdvancePhase)
{
    if (!Document || !FMath::IsFinite(DeltaSeconds) || DeltaSeconds < 0.f) return;
    const bool bHasTracks = Document->Tracks.Num() > 0;
    if (bAdvancePhase && !bHasTracks) Document->ItemAnimationSeconds += DeltaSeconds;
    TArray<AKKPreviewActor*, TInlineAllocator<16>> Actors;
    const bool bSharePose = CVarSharePose.GetValueOnGameThread() != 0;
    for (auto& Pair : Shown)
    {
        AKKPreviewActor* Actor = Pair.Value.Preview.Get();
        const FOKSceneObject* Object = Document->Find(Pair.Key);
        if (!Actor || !Object || Object->Kind != EOKSceneObjectKind::Character) continue;
        // An anim player or a session clip (a video motion, PoseMode 2) runs on the scene's clock.
        if (Actor->OkPlayer || Actor->PoseMode == 2)
        {
            if (bHasTracks)
                Pair.Value.Time = Object->Character.Time + Document->ItemAnimationSeconds * Object->Character.Speed;
            else if (bAdvancePhase)
                Pair.Value.Time += DeltaSeconds * Object->Character.Speed;
            // Forced looping starts a motion that ends over again (one that loops by itself wraps anyway).
            const float Length = Object->Character.bForceLoop && Actor->OkPlayer ? Actor->OkPlayer->Duration() : 0.f;
            Actor->AnimPhase = Length > 0.f ? FMath::Fmod(Pair.Value.Time, Length) : Pair.Value.Time;
        }
        // Scene actors have their own Tick disabled. A static FK pose still needs
        // a finishing pass so cloth receives the current limb pose and elapsed time.
        if (Actor->PoseMode == 0) Actor->bUserPoseDirty = true;
        Actor->bShareIdenticalPose = bSharePose;
        // Its face parts tick after this actor, so their own (worker) anim update takes the pose set below this frame.
        if (!Actor->bSceneTicksOrdered) Actor->OrderSceneTicks(this);
        Actors.Add(Actor);
    }
    // Each character's native solve touches only its own session: solved together on worker threads, then each
    // display refreshed here. A pose the split does not cover (a shape edit, other pose modes) runs whole here.
    // Each solve is its own task; a character's display is refreshed as soon as its solve is done, while the others
    // are still solving (the game thread's display work overlaps the workers' solves instead of following them all).
    if (CVarParallelPose.GetValueOnGameThread() != 0 && Actors.Num() > 0 && CVarPipelinePose.GetValueOnGameThread() == 0)
    {
        QUICK_SCOPE_CYCLE_COUNTER(STAT_OKScene_ParallelSolve);
        TArray<uint8, TInlineAllocator<16>> Split;
        Split.SetNumZeroed(Actors.Num());
        ParallelFor(Actors.Num(), [&](int32 I) { Split[I] = Actors[I]->SolveScenePose(DeltaSeconds, false) ? 1 : 0; });
        for (int32 I = 0; I < Actors.Num(); ++I)
        {
            if (Split[I]) Actors[I]->FinishScenePose();
            else Actors[I]->ApplyPoseFrame(DeltaSeconds, false);
        }
        return;
    }
    if (CVarParallelPose.GetValueOnGameThread() != 0 && Actors.Num() > 0)
    {
        QUICK_SCOPE_CYCLE_COUNTER(STAT_OKScene_PipelinedSolve);
        TArray<uint8, TInlineAllocator<16>> Split;
        Split.SetNumZeroed(Actors.Num());
        TArray<UE::Tasks::FTask, TInlineAllocator<16>> Solves;
        for (int32 I = 0; I < Actors.Num(); ++I)
            Solves.Add(UE::Tasks::Launch(UE_SOURCE_LOCATION, [Actor = Actors[I], &Split, I, DeltaSeconds] { Split[I] = Actor->SolveScenePose(DeltaSeconds, false) ? 1 : 0; }));
        TArray<bool, TInlineAllocator<16>> Done;
        Done.SetNumZeroed(Actors.Num());
        for (int32 Left = Actors.Num(); Left > 0;)
        {
            int32 Next = INDEX_NONE;
            for (int32 I = 0; I < Actors.Num() && Next == INDEX_NONE; ++I) if (!Done[I] && Solves[I].IsCompleted()) Next = I;
            if (Next == INDEX_NONE)
            {
                for (int32 I = 0; I < Actors.Num(); ++I) if (!Done[I]) { Solves[I].Wait(); Next = I; break; }
            }
            Done[Next] = true;
            --Left;
            if (Split[Next]) Actors[Next]->FinishScenePose();
            else Actors[Next]->ApplyPoseFrame(DeltaSeconds, false);
        }
        return;
    }
    for (AKKPreviewActor* Actor : Actors) Actor->ApplyPoseFrame(DeltaSeconds, false);
}

void AOKSceneActor::SetAnimationTime(int32 Id, float Time)
{
	FShown* Item = Shown.Find(Id);
	const FOKSceneObject* Object = Document ? Document->Find(Id) : nullptr;
	AKKPreviewActor* Actor = Item ? Item->Preview.Get() : nullptr;
	if (!Actor || !Object || Object->Kind != EOKSceneObjectKind::Character) return;
	if (Actor->OkPlayer || Actor->PoseMode == 2)
	{
		Item->Time = Object->Character.Time + Time * Object->Character.Speed;
		const float Length = Object->Character.bForceLoop && Actor->OkPlayer ? Actor->OkPlayer->Duration() : 0.f;
		Actor->AnimPhase = Length > 0.f ? FMath::Fmod(Item->Time, Length) : Item->Time;
	}
	Actor->ApplyPoseFrame(0.f, false);
}

void AOKSceneActor::Rebuild(int32 Id)
{
	if (FShown* Item = Shown.Find(Id)) Item->Kind = EOKSceneObjectKind::Folder == Item->Kind ? EOKSceneObjectKind::Character : EOKSceneObjectKind::Folder;
}

bool AOKSceneActor::SetCharacterAnimation(int32 Id, int32 Kind, int32 Group, int32 Category, int32 No, FString& OutError)
{
	FShown* Item = Shown.Find(Id);
	FOKSceneObject* Object = Document ? Document->Find(Id) : nullptr;
	AKKPreviewActor* Actor = Item ? Item->Preview.Get() : nullptr;
	if (!Object || !Actor || Item->Kind != EOKSceneObjectKind::Character) { OutError = TEXT("no shown character"); return false; }
	const FOKStudioAnim* Entry = FOKStudioCatalog::Get().FindAnim(Kind, Group, Category, No);
	if (!Entry || Entry->File.IsEmpty()) { OutError = FString::Printf(TEXT("studio animation %d/%d/%d/%d %s"), Kind, Group, Category, No, Entry ? TEXT("not converted") : TEXT("not in the library")); return false; }
	UOKAnimSequence* Seq = UOKAnimationLibrary::LoadOkAnimFile(LibraryDirectory() / Entry->File, nullptr, NAME_None, OutError);
	const TSharedPtr<FOkAnimPlayer> Player = Seq ? Seq->CreatePlayer(OutError) : nullptr;
	if (!Player || !Actor->SetOkAnimation(Player, true)) { if (OutError.IsEmpty()) OutError = TEXT("animation not playable"); return false; }
	Actor->PoseMode = 3;
	Item->StudioAnim.Reset(Seq);
	Item->Time = 0.f;
	// Both copies agree, so the next Sync keeps the character as it is.
	for (FOKSceneCharacter* C : { &Object->Character, &Item->Character })
	{
		C->StudioAnimeKind = Kind; C->StudioAnimeGroup = Group; C->StudioAnimeCategory = Category; C->StudioAnimeNo = No;
		C->Animation.Reset(); C->Time = 0.f;
	}
	return true;
}

TArray<AActor*> AOKSceneActor::ObjectPreviews() const
{
	TArray<AActor*> Out;
	for (const auto& Pair : Shown) if (AKKPreviewActor* P = Pair.Value.Preview.Get()) Out.Add(P);
	return Out;
}

AKKPreviewActor* AOKSceneActor::PreviewOf(int32 Id) const
{
	const FShown* Item = Shown.Find(Id);
	return Item ? Item->Preview.Get() : nullptr;
}

TArray<TPair<FString, float>> AOKSceneActor::BlendControlsOf(int32 Id) const
{
	TArray<TPair<FString, float>> Out;
	if (const FShown* Item = Shown.Find(Id))
		for (int32 I = 0; I < Item->BlendControls.Num(); ++I) Out.Emplace(Item->BlendControls[I], Item->AppliedBlend.IsValidIndex(I) ? Item->AppliedBlend[I] : -1.f);
	return Out;
}

USceneComponent* AOKSceneActor::ComponentOf(int32 Id) const
{
	const FShown* Item = Shown.Find(Id);
	return Item ? Item->Component.Get() : nullptr;
}

UCameraComponent* AOKSceneActor::ActiveCamera() const
{
	return Document ? Cast<UCameraComponent>(ComponentOf(Document->ActiveCamera)) : nullptr;
}

void AOKSceneActor::UpdateLooks(float DeltaSeconds, bool bApplyStaticPose)
{
	if (!Document) return;
	for (auto& Pair : Shown)
	{
		AKKPreviewActor* Actor = Pair.Value.Preview.Get();
		const FOKSceneObject* Object = Document->Find(Pair.Key);
		if (!Actor || !Object || Object->Kind != EOKSceneObjectKind::Character) continue;
		const FOKSceneCharacter& C = Object->Character;
		auto Target = [&](EOKSceneLook Mode, FVector& Out)
		{
			if (Mode == EOKSceneLook::Camera && bHasViewLocation) { Out = ViewLocation; return true; }
			if (Mode == EOKSceneLook::Point) { Out = Actor->GetActorTransform().TransformPosition(C.LookPoint); return true; }
			// Front and away in the character's space at head height (the default look point's: 140 cm, forward +Y).
			if (Mode == EOKSceneLook::Front) { Out = Actor->GetActorTransform().TransformPosition(FVector(0.f, 1000.f, 140.f)); return true; }
			if (Mode == EOKSceneLook::Away && bHasViewLocation) { Out = 2.f * Actor->GetActorTransform().TransformPosition(FVector(0.f, 0.f, 140.f)) - ViewLocation; return true; }
			return false;
		};
		FVector Look;
		if (C.EyesLook == EOKSceneLook::Fixed && (!Pair.Value.bFixedEyesApplied || Pair.Value.AppliedFixedEyes != C.FixedEyes || Pair.Value.AppliedFixedEyesPattern != C.FixedEyesPattern))
		{
			FVector Rates;
			if (Actor->Session.SetStudioFixedEyes(C.FixedEyes, C.FixedEyesPattern, Rates))
			{
				Actor->SetEyeGaze(Rates.X, Rates.Y, Rates.Z);
				Pair.Value.bFixedEyesApplied = true;
				Pair.Value.AppliedFixedEyes = C.FixedEyes;
				Pair.Value.AppliedFixedEyesPattern = C.FixedEyesPattern;
			}
			else Errors.Add(Pair.Key, TEXT("fixed eyes not applied"));
		}
		else if (C.EyesLook != EOKSceneLook::Fixed && Pair.Value.bFixedEyesApplied)
		{
			Actor->SetEyeGaze(0.f, 0.f, 0.f);
			Pair.Value.bFixedEyesApplied = false;
		}
		if (Target(C.EyesLook, Look)) { Actor->SetEyeLookTarget(Look, 1, true); Pair.Value.bEyesTracking = true; }
		else if (Pair.Value.bEyesTracking) { Actor->StopEyeLookTracking(); Pair.Value.bEyesTracking = false; }
		Actor->AdvanceEyeLook(DeltaSeconds);
		// Head look runs in the pose frame: a static character re-poses for it, an animated one gets it on its next frame.
		const bool bNeck = Target(C.NeckLook, Look);
		// Follow strength: the target's direction from the head turned only that far from straight ahead.
		if (bNeck && C.NeckFollow < 1.f)
		{
			const FTransform& T = Actor->GetActorTransform();
			const FVector Head = T.TransformPosition(FVector(0.f, 0.f, 140.f));
			const FVector Ahead = T.TransformVectorNoScale(FVector(0.f, 1.f, 0.f));
			const FVector To = Look - Head;
			const FQuat Turn = FQuat::FindBetweenNormals(Ahead, To.GetSafeNormal());
			Look = Head + FQuat::Slerp(FQuat::Identity, Turn, FMath::Clamp(C.NeckFollow, 0.f, 1.f)).RotateVector(Ahead) * FMath::Max(To.Size(), 100.f);
		}
		if (bNeck || Pair.Value.bNeckLooking)
		{
			const FVector Local = Actor->GetActorTransform().InverseTransformPosition(Look);
			Actor->Session.SetHeadLook(AKKPreviewActor::ActorLocalToUnity(Local), bNeck);
			Pair.Value.bNeckLooking = bNeck;
			Actor->bHeadLookPoseDirty = true;
			if (bApplyStaticPose && Actor->PoseMode == 0) Actor->ApplyPoseFrame(DeltaSeconds, false);
		}
	}
}

void AOKSceneActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (bBuildsPending && Document) Sync();
	// A sequence-driven scene shows SequenceTime instead of playing on its own.
    if (!SceneJson.IsEmpty()) ShowSequenceTime();
    // The passes below only read the scene: its objects by id through an index (they look every object and its
    // parents up each frame; a linear Find made that quadratic in a scene of thousands of items).
    const UOKSceneDocument::FFindScope FindScope(Document);
    // Prepare targets before the single pose/physics frame; UpdateLooks' direct
    // callers retain its standalone behavior without double-stepping static cloth.
    {QUICK_SCOPE_CYCLE_COUNTER(STAT_OKScene_UpdateLooks);UpdateLooks(DeltaSeconds, false);}
    {QUICK_SCOPE_CYCLE_COUNTER(STAT_OKScene_CharacterFrames);AdvanceCharacterFrames(DeltaSeconds, SceneJson.IsEmpty() && !bPauseAnimations);}
	{QUICK_SCOPE_CYCLE_COUNTER(STAT_OKScene_PlaceAttached);PlaceAttached();}
	{QUICK_SCOPE_CYCLE_COUNTER(STAT_OKScene_ItemLighting);UpdateItemLighting();}
	{QUICK_SCOPE_CYCLE_COUNTER(STAT_OKScene_Mirrors);UpdateMirrors();}
}

void AOKSceneActor::UpdateMirrors()
{
	TArray<const AKKPreviewActor::FMirrorPart*> Mirrors;
	for (TActorIterator<AKKPreviewActor> It(GetWorld()); It; ++It)
		for (const AKKPreviewActor::FMirrorPart& M : It->MirrorParts)
			if (M.Material.IsValid() && M.Component.IsValid()) Mirrors.Add(&M);
	// Captures of mirrors that are gone.
	for (int32 C = MirrorCaptures.Num() - 1; C >= 0; --C)
		if (!MirrorCaptureMaterials[C].IsValid() || !Mirrors.ContainsByPredicate([&](const AKKPreviewActor::FMirrorPart* M) { return M->Material == MirrorCaptureMaterials[C]; }))
		{
			if (MirrorCaptures[C]) MirrorCaptures[C]->DestroyComponent();
			MirrorCaptures.RemoveAt(C);
			MirrorCaptureMaterials.RemoveAt(C);
		}
	if (Mirrors.IsEmpty()) return;
	APlayerController* Controller = GetWorld()->GetFirstPlayerController();
	if (!Controller || !Controller->PlayerCameraManager) return;
	const FMinimalViewInfo View = Controller->PlayerCameraManager->GetCameraCacheView();
	FVector2D ViewportSize(1280.0, 720.0);
	if (GEngine && GEngine->GameViewport) GEngine->GameViewport->GetViewportSize(ViewportSize);
	const double Aspect = ViewportSize.Y > 0.0 ? ViewportSize.X / ViewportSize.Y : 16.0 / 9.0;
	// MirrorReflection.m_TextureSize (1024 in every yamadamod mirror): the reflection's resolution, stretched over the view.
	const int32 TextureSize = 1024;
	const int32 W = FMath::Clamp(FMath::Min(int32(ViewportSize.X), TextureSize), 16, TextureSize), H = FMath::Clamp(FMath::Min(int32(ViewportSize.Y), TextureSize), 16, TextureSize);
	const FMatrix Rotation = FRotationMatrix(View.Rotation);
	const FVector Forward = Rotation.GetUnitAxis(EAxis::X), Up = Rotation.GetUnitAxis(EAxis::Z);
	const double TanX = FMath::Tan(FMath::DegreesToRadians(double(View.FOV)) * 0.5), TanY = TanX / Aspect;
	for (const AKKPreviewActor::FMirrorPart* M : Mirrors)
	{
		UPrimitiveComponent* Comp = M->Component.Get();
		const int32 Index = MirrorCaptureMaterials.IndexOfByKey(M->Material);
		USceneCaptureComponent2D* Capture = Index == INDEX_NONE ? nullptr : MirrorCaptures[Index].Get();
		if (!Capture)
		{
			UTextureRenderTarget2D* Target = NewObject<UTextureRenderTarget2D>(this);
			Target->RenderTargetFormat = RTF_RGBA8_SRGB;
			Target->ClearColor = FLinearColor::Black;
			Target->InitAutoFormat(W, H);
			Target->UpdateResourceImmediate(true);
			Capture = NewObject<USceneCaptureComponent2D>(this);
			Capture->SetupAttachment(RootComponent);
			Capture->CaptureSource = SCS_FinalColorLDR;
			// The reflection camera is a copy of the main camera without its image effects (see AddRenderCamera).
			FOKScenePost Plain;
			Plain.bBloom = Plain.bVignette = Plain.bDepthOfField = Plain.bFog = Plain.bAmbientOcclusion = Plain.bSunShafts = false;
			Plain.ColorFilter = -1;
			Plain.Exposure = 0.f;
			Plain.ApplyTo(Capture->PostProcessSettings);
			Capture->PostProcessBlendWeight = 1.f;
			Capture->bCaptureOnMovement = false;
			Capture->bUseCustomProjectionMatrix = true;
			Capture->TextureTarget = Target;
			Capture->HideComponent(Comp);
			Capture->RegisterComponent();
			M->Material->SetTextureParameterValue(TEXT("_ReflectionTex"), Target);
			MirrorCaptures.Add(Capture);
			MirrorCaptureMaterials.Add(M->Material);
			UE_LOG(LogTemp, Display, TEXT("OK studio: mirror %s %dx%d"), *Comp->GetName(), W, H);
		}
		Capture->bCaptureEveryFrame = Comp->IsVisible();
		if (!Capture->bCaptureEveryFrame) continue;
		// The plane in world space (normals take the inverse scale), facing the camera.
		const FTransform& T = Comp->GetComponentTransform();
		const FVector Point = T.TransformPosition(M->LocalPoint);
		FVector Normal = T.GetRotation().RotateVector(M->LocalNormal / T.GetScale3D()).GetSafeNormal();
		if (FVector::DotProduct(View.Location - Point, Normal) < 0.0) Normal = -Normal;
		const auto Reflect = [&Normal](const FVector& V) { return V - 2.0 * FVector::DotProduct(V, Normal) * Normal; };
		const FVector Location = View.Location - 2.0 * FVector::DotProduct(View.Location - Point, Normal) * Normal;
		// A right-handed camera along the mirrored forward and up: its picture is the reflection flipped left to right
		// (the mirror material samples at 1 - u).
		const FMatrix Axes = FRotationMatrix::MakeFromXZ(Reflect(Forward), Reflect(Up));
		Capture->SetWorldLocationAndRotation(Location, Axes.Rotator());
		// Unity's CalculateObliqueMatrix in Unreal's reversed-Z projection: clip z = w - k * d, with d the signed distance
		// to the mirror plane (view space x right, y up, z forward), so points behind the mirror fall in front of the near
		// plane. k keeps the depth of every visible point at or above 0 (the far end of the frustum's corner rays).
		const FVector Right = Axes.GetUnitAxis(EAxis::Y), CamUp = Axes.GetUnitAxis(EAxis::Z), CamForward = Axes.GetUnitAxis(EAxis::X);
		const double A = FVector::DotProduct(Normal, Right), B = FVector::DotProduct(Normal, CamUp), C = FVector::DotProduct(Normal, CamForward);
		const double E = FVector::DotProduct(Location - Point, Normal);
		double MaxRatio = 0.0;
		for (const double SX : { -1.0, 1.0 }) for (const double SY : { -1.0, 1.0 }) MaxRatio = FMath::Max(MaxRatio, A * SX * TanX + B * SY * TanY + C);
		const double K = MaxRatio > 1e-6 ? 0.999 / MaxRatio : 1.0;
		FMatrix Projection(FPlane(1.0 / TanX, 0.0, -K * A, 0.0), FPlane(0.0, 1.0 / TanY, -K * B, 0.0), FPlane(0.0, 0.0, 1.0 - K * C, 1.0), FPlane(0.0, 0.0, -K * E, 0.0));
		Capture->CustomProjectionMatrix = Projection;
	}
}

void AOKSceneActor::UpdateItemLighting()
{
	if (!Document) return;
	// Studio's character light (CameraLightCtrl: a scene-root light, turned by charaLight.rot) lights the Chara layer,
	// where Studio's characters and items are: its direction towards the light in Unity space, and its colour times
	// intensity (a dim character light leaves the characters in their toon shadow, as in Studio).
	// The toon shaders take one forward light: the shown character light, else the first shown directional light. A
	// hidden (switched off) light gives none; once the scene has had a light, no light on leaves the characters in
	// their toon shadow, so switching lights on and off shows on them.
	const ULightComponent* Light = nullptr;
	const FOKSceneLight* SourceLight = nullptr;
	float Intensity = 1.f;
	bool bAnyLight = false;
	for (const int32 Pass : { 0, 1 })
	{
		for (const FOKSceneObject& Object : Document->Objects)
		{
			if (Object.Kind != EOKSceneObjectKind::Light) continue;
			bAnyLight = true;
			if (Object.Light.Type != EOKSceneLightType::Directional || Object.Light.bMapOnly || (Pass == 0 && !Object.Light.bCharactersOnly) || !Document->IsShown(Object.Id)) continue;
			if (const FShown* S = Shown.Find(Object.Id); S && S->Component.IsValid())
				if ((Light = Cast<ULightComponent>(S->Component.Get()))) { Intensity = Object.Light.Intensity / 4.f; SourceLight = &Object.Light; break; }
		}
		if (Light) break;
	}
	bCharacterLightSeen = bCharacterLightSeen || bAnyLight;
	// The sky's ambient (Studio's ambient mode is Skybox: the sky controller updates the ambient probe) on the
	// scene renderers' unity_SHA*. Without a map or sky, native Studio's
	// empty environment is zero, rather than the Maker display environment.
	if (SkyShown != AmbientSky || Document->Map != AmbientMap) { AmbientSky = SkyShown; AmbientMap = Document->Map; AmbientLit.Reset(); }
	FLinearColor SH[3];
	TArray<FLinearColor> Higher = { FLinearColor(0,0,0,0), FLinearColor(0,0,0,0), FLinearColor(0,0,0,0), FLinearColor(0,0,0,1) };
	// A loaded map's scene is the active one: its RenderSettings give the ambient. Only with ambient mode Skybox does the
	// Studio sky's controller replace it (DynamicGI.UpdateEnvironment from the sky); a Trilight or Flat map keeps its
	// colours under the sky (native bath, map 62: sky on, ambient still its trilight).
	int32 MapMode = -1;
	FLinearColor MapSH[3];
	TArray<FLinearColor> MapHigher = Higher;
	const bool bMapAmbient = Document->Map >= 0 && OKStudioMapAmbient(Document->Map, MapSH[0], MapSH[1], MapSH[2], &MapHigher, &MapMode);
	const bool bSkyLit = AmbientSky >= 0 && !(bMapAmbient && (MapMode == 1 || MapMode == 3));
	if (!bSkyLit && bMapAmbient) { for (int32 I = 0; I < 3; ++I) SH[I] = MapSH[I]; Higher = MapHigher; }
	else if (!(bSkyLit && OKStudioSkyAmbient(AmbientSky, SH[0], SH[1], SH[2]))
		&& !OKStudioMapAmbient(Document->Map, SH[0], SH[1], SH[2], &Higher))
	{
		FKKRuntimeMaterialScene Maker;
		OKApplyCharacterMakerLighting(Maker);
		SH[0] = Maker.Uniforms.FindRef(TEXT("unity_SHAr"));
		SH[1] = Maker.Uniforms.FindRef(TEXT("unity_SHAg"));
		SH[2] = Maker.Uniforms.FindRef(TEXT("unity_SHAb"));
	}
    TArray<AKKPreviewActor*> AmbientPreviews;
    for (const TPair<int32,FShown>& It:Shown)
        if ((It.Value.Kind==EOKSceneObjectKind::Character || It.Value.Kind==EOKSceneObjectKind::Item) && It.Value.Preview.IsValid())
            AmbientPreviews.Add(It.Value.Preview.Get());
    for (const auto& MapPreview:ShownMapPreviews) if(MapPreview.IsValid()) AmbientPreviews.Add(MapPreview.Get());
    for (AKKPreviewActor* Preview:AmbientPreviews)
    {
        if (AmbientLit.Contains(Preview)) continue;
		Preview->SetSharedVectorParameter(TEXT("unity_SHAr"), SH[0]);
		Preview->SetSharedVectorParameter(TEXT("unity_SHAg"), SH[1]);
		Preview->SetSharedVectorParameter(TEXT("unity_SHAb"), SH[2]);
		const FName Names[] = { TEXT("unity_SHBr"), TEXT("unity_SHBg"), TEXT("unity_SHBb"), TEXT("unity_SHC") };
		for (int32 I=0; I<4; ++I) Preview->SetSharedVectorParameter(Names[I], Higher[I]);
		// The legacy ambient (glstate_lightmodel_ambient = linear RenderSettings.ambientSkyColor / 2), which Shader Forge
		// main_StandardMDK_studio adds twice of whatever lights it. Native probe: Studio's empty scene has the sky colour
		// (0.318, 0.3405, 0.3885, 1.5); maps 15, 26 and 28 run with 0.1698 grey (Studio's SunLightInfo sets it per time
		// of day; the other maps' values are not recorded yet).
		Preview->SetSharedVectorParameter(TEXT("glstate_lightmodel_ambient"), Document->Map < 0
			? FLinearColor(0.0412349328f, 0.0474586822f, 0.0624753758f, 0.75f) : FLinearColor(0.01223285f, 0.01223285f, 0.01223285f, 0.5f));
		FLinearColor ReflectionHDR(1,1,0,0);
		// A map without a baked default reflection reflects Unity's black cube (native probe: reflection_default UnityBlackCube), not UE's sky.
		UTextureCube* Reflection = !bSkyLit ? OKStudioMapReflection(Document->Map,ReflectionHDR) : nullptr;
		if (!Reflection && !bSkyLit) Reflection = OKStudioBlackReflection();
		for(const FName Name:{FName(TEXT("unity_SpecCube0")),FName(TEXT("unity_SpecCube1"))})Preview->SetSharedTextureParameter(Name,Reflection);
		Preview->SetSharedVectorParameter(TEXT("unity_SpecCube0_HDR"),ReflectionHDR);
		Preview->SetSharedVectorParameter(TEXT("unity_SpecCube1_HDR"),ReflectionHDR);
        if(!Preview->bUseSharedParents)
        {
            FString Root;
            for(const FName Tag:Preview->Tags){const FString T=Tag.ToString();const FString Prefix=TEXT("OKStudioMapRoot=");if(T.StartsWith(Prefix))Root=T.Mid(Prefix.Len());}
            Preview->BindSourceReflectionProbes([&](int32 Node,FLinearColor& HDR)->UTexture*{if(Node<0&&bSkyLit)return nullptr;UTexture* Cube=OKStudioMapReflection(Document->Map,HDR,Root,Node);return Cube||bSkyLit?Cube:OKStudioBlackReflection();});
        }
		AmbientLit.Add(Preview);
	}
	if (!Light && !bCharacterLightSeen && MapSunMask == 0) return;
	FLinearColor Direction = ItemLightDirection.A < 0.f ? FLinearColor(0.f, 0.f, -1.f, 0.f) : ItemLightDirection;
	FLinearColor Color(0.f, 0.f, 0.f, 0.f);
	if (Light)
	{
		const FVector F = Light->GetForwardVector();
		// Unreal (x, y, z) is Unity (-x, z, y); towards the light is against the light's forward.
		const FVector Towards = FVector(F.X, -F.Z, -F.Y).GetSafeNormal();
		Direction = FLinearColor(Towards.X, Towards.Y, Towards.Z, 0.f);
		// Unity's linear renderer converts the final gamma-space light colour,
		// including intensity; values above one use its GammaToLinearSpace HDR tail.
		Color = OKStudioLinearLightColor(SourceLight->Color, Intensity);
	}
	if (!Direction.Equals(ItemLightDirection, 1e-4f) || !Color.Equals(ItemLightColor, 1e-4f)) { ItemLightDirection = Direction; ItemLightColor = Color; ItemsLit.Reset(); }
	for (const TPair<int32, FShown>& It : Shown)
	{
		const bool bLit = It.Value.Kind == EOKSceneObjectKind::Item || It.Value.Kind == EOKSceneObjectKind::Character;
		AKKPreviewActor* Preview = bLit ? It.Value.Preview.Get() : nullptr;
		if (!Preview || (It.Value.Kind == EOKSceneObjectKind::Item && !Preview->bStudioItemShading) || ItemsLit.Contains(Preview)) continue;
		Preview->SetSharedVectorParameter(TEXT("_WorldSpaceLightPos0"), ItemLightDirection);
		// The character light's culling mask is the Chara layer (10) alone: items on another layer (the landscape
		// pieces on Map, 11) get no forward light, _LightColor0 is black for them (native probe: hiding the light
		// leaves them unchanged).
		const bool bUnreached = It.Value.Kind == EOKSceneObjectKind::Item && Preview->IsOutsideLayerMask(1u << 10);
		// Unity forward rendering gives an object the brightest directional light whose culling mask has its layer:
		// items authored on the map layer (11: sand, grass, landscape pieces) are lit by the map's own sun.
		const bool bSun = It.Value.Kind == EOKSceneObjectKind::Item && MapSunMask != 0 && !Preview->IsOutsideLayerMask(MapSunMask)
			&& (bUnreached || MapSunItemColor.GetLuminance() > ItemLightColor.GetLuminance());
		if (bSun) Preview->SetSharedVectorParameter(TEXT("_WorldSpaceLightPos0"), MapSunItemDirection);
		if (FPlatformMisc::GetEnvironmentVariable(TEXT("OK_STUDIO_LIGHTING_AUDIT")) == TEXT("1"))
		{
			FString LayerTags; TInlineComponentArray<UMeshComponent*> Meshes(Preview);
			for (const UMeshComponent* M : Meshes) for (const FName& T : M->ComponentTags) if (T.ToString().StartsWith(TEXT("OKLayer"))) { LayerTags += T.ToString() + TEXT(" "); break; }
			UE_LOG(LogTemp, Display, TEXT("OK studio item light %s item=%d/%d/%d unreached=%d layers=%s"), *Preview->GetName(), Document->Find(It.Key) ? Document->Find(It.Key)->Item.Group : -1, Document->Find(It.Key) ? Document->Find(It.Key)->Item.Category : -1, Document->Find(It.Key) ? Document->Find(It.Key)->Item.No : -1, int32(bUnreached), *LayerTags.Left(60));
		}
		Preview->SetSharedVectorParameter(TEXT("_LightColor0"), bSun ? FLinearColor(MapSunItemColor.R, MapSunItemColor.G, MapSunItemColor.B, ItemLightColor.A)
			: bUnreached ? FLinearColor(0.f, 0.f, 0.f, ItemLightColor.A) : ItemLightColor);
		ItemsLit.Add(Preview);
	}
}

void AOKSceneActor::PlaceAttached()
{
	if (!Document) return;
	// A placement that hangs on no character bone and under no animated item (Studio's child animations) is the same
	// until the scene changes: placed once per placement generation (a sync, the scene actor moving) instead of every
	// frame (a scene of thousands of items spent ~1.7 ms here).
	if (!GetActorTransform().Equals(PlacedActorTransform, 0.)) { PlacedActorTransform = GetActorTransform(); ++PlaceGeneration; }
	const FOKStudioCatalog& Catalog = FOKStudioCatalog::Get();
	for (const FOKSceneObject& Object : Document->Objects)
	{
		bool bAttached = false, bMoving = false;
		for (int32 Steps = 0, Cur = Object.Id; Cur >= 0 && Steps < UOKSceneDocument::MaxDepth; ++Steps)
		{
			const FOKSceneObject* O = Document->Find(Cur);
			if (!O) break;
			bAttached = bAttached || !O->AttachBone.IsEmpty() || O->Kind == EOKSceneObjectKind::Item;
			const FOKSceneObject* P = Document->Find(O->Parent);
			bMoving = bMoving || !O->AttachBone.IsEmpty() || (P && P->Kind == EOKSceneObjectKind::Character)
				|| (P && P->Kind == EOKSceneObjectKind::Item && Catalog.HasChildAnimation(P->Item.Group, P->Item.Category, P->Item.No, P->Item.AnimePattern));
			Cur = O->Parent;
		}
		FShown* Item = bAttached ? Shown.Find(Object.Id) : nullptr;
		if (!Item) continue;
		if (!bMoving && Item->PlacedGeneration == PlaceGeneration) continue;
		Item->PlacedGeneration = PlaceGeneration;
		const FTransform World = Document->WorldTransform(Object.Id) * GetActorTransform();
		if (AKKPreviewActor* Preview = Item->Preview.Get()) Preview->SetActorTransform(Compose(Item->RenderCorrection, World));
		if (USceneComponent* Component = Item->Component.Get()) Component->SetWorldTransform(Compose(Item->ComponentOffset, World));
	}
}

void AOKSceneActor::PlaceBackground(const FTransform& View, float Fov, float Aspect)
{
	// A picture path is relative to the OK base library (the game's own pictures there) or a file of the scene.
	auto Resolve = [](const FString& Path) { return Path.IsEmpty() || !FPaths::IsRelative(Path) ? Path : LibraryDirectory() / Path; };
	// One unlit plane per picture (the pass-through materials of 3D widgets): the background opaque, the frame blended.
	auto Show = [this](FScenePicture& P, const FString& File, const TCHAR* Name, const TCHAR* Material)
	{
		if (File == P.Shown) return;
		P.Shown = File;
		if (File.StartsWith(TEXT("color:")))
		{
			// A solid colour (sRGB hex): a tiny texture of it.
			const FColor Color = FColor::FromHex(File.Mid(6));
			UTexture2D* Solid = UTexture2D::CreateTransient(4, 4, PF_B8G8R8A8);
			FColor* Texels = static_cast<FColor*>(Solid->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE));
			for (int32 K = 0; K < 16; ++K) Texels[K] = Color;
			Solid->GetPlatformData()->Mips[0].BulkData.Unlock();
			Solid->SRGB = true;
			Solid->UpdateResource();
			P.Texture.Reset(Solid);
		}
		else P.Texture.Reset(File.IsEmpty() || !FPaths::FileExists(File) ? nullptr : FImageUtils::ImportFileAsTexture2D(File));
		if (!File.IsEmpty() && !P.Texture.IsValid()) UE_LOG(LogTemp, Warning, TEXT("OK studio: picture %s not readable"), *File);
		if (P.Texture.IsValid() && !P.Plane.IsValid())
		{
			P.Plane.Reset(NewObject<UStaticMeshComponent>(this, Name));
			P.Plane->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane")));
			P.Plane->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			P.Plane->SetCastShadow(false);
			P.Plane->bReceivesDecals = false;
			P.Plane->SetupAttachment(GetRootComponent());
			P.Plane->SetUsingAbsoluteLocation(true);
			P.Plane->SetUsingAbsoluteRotation(true);
			P.Plane->SetUsingAbsoluteScale(true);
			P.Plane->RegisterComponent();
		}
		if (!P.Plane.IsValid()) return;
		UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, Material);
		UMaterialInstanceDynamic* Mid = Base && P.Texture.IsValid() ? UMaterialInstanceDynamic::Create(Base, this) : nullptr;
		if (Mid) Mid->SetTextureParameterValue(TEXT("SlateUI"), P.Texture.Get());
		P.Plane->SetMaterial(0, Mid);
		P.Plane->SetVisibility(P.Texture.IsValid());
	};
	FString BackgroundSource = Resolve(Document ? Document->Post.Background : FString());
	if (BackgroundSource.IsEmpty() && Document && Document->Post.BackgroundColor.A >= 0.f)
		BackgroundSource = TEXT("color:") + Document->Post.BackgroundColor.ToFColor(false).ToHex();
	Show(Background, BackgroundSource, TEXT("Background"), TEXT("/Engine/EngineMaterials/Widget3DPassThrough_Opaque.Widget3DPassThrough_Opaque"));
	Show(Frame, Resolve(Document ? Document->Post.Frame : FString()), TEXT("Frame"), TEXT("/Engine/EngineMaterials/Widget3DPassThrough_Translucent.Widget3DPassThrough_Translucent"));
	// Square to the view and exactly as large as the frame at its distance: the background far behind the scene, the
	// frame just past the near plane, drawn over everything.
	const FVector Forward = View.GetRotation().GetForwardVector(), Up = View.GetRotation().GetUpVector();
	const FRotator Facing = FRotationMatrix::MakeFromZX(-Forward, Up).Rotator();
	const double Span = 2.0 * FMath::Tan(FMath::DegreesToRadians(FMath::Clamp(Fov, 1.f, 170.f)) * 0.5);
	for (TPair<FScenePicture*, double> P : { TPair<FScenePicture*, double>(&Background, 100000.0), TPair<FScenePicture*, double>(&Frame, 12.0) })
	{
		if (!P.Key->Plane.IsValid() || !P.Key->Texture.IsValid()) continue;
		const double Width = Span * P.Value;
		P.Key->Plane->SetWorldLocationAndRotation(View.GetLocation() + Forward * P.Value, Facing);
		// The plane is 100 cm square in its XY (X up, Y right here); its texture runs right to left along Y, so Y is
		// mirrored to show the picture as Studio does (BackgroundCtrl: the picture's left on the screen's left).
		P.Key->Plane->SetWorldScale3D(FVector(Width / FMath::Max(Aspect, 0.1f) / 100.0, -Width / 100.0, 1.0));
	}
	if (Frame.Plane.IsValid()) Frame.Plane->SetTranslucentSortPriority(1000);
	// Sky dome items (Skybox/Cubemap on a mesh) draw before everything else translucent, without depth: Unity draws them
	// front to back by their bounds' centre, so of overlapping domes the farthest shows. Order ours the same way. The
	// domes are found again only after the shown objects change (every component of thousands of items each frame cost
	// ~2.5 ms).
	if (bSkyDomesDirty)
	{
		bSkyDomesDirty = false;
		SkyDomeParts.Reset();
		TMap<TWeakObjectPtr<UPrimitiveComponent>, TPair<int32, int32>> Ranks;
		for (const FRankedPart& Ranked : BlendedParts) if (Ranked.Part.IsValid()) Ranks.Add(Ranked.Part, {Ranked.Rank, Ranked.BasePriority});
		BlendedParts.Reset();
		if (bDepthTextureAbsentMarked)
			for (const TWeakObjectPtr<UPrimitiveComponent>& Part : DepthTextureAbsentParts)
				if (UPrimitiveComponent* P = Part.Get()) P->SetRenderCustomDepth(false);
		DepthTextureAbsentParts.Reset();
		bDepthTextureAbsentMarked = false;
		bDepthTextureReaders = false;
		for (const TPair<int32, FShown>& It : Shown)
			if (AKKPreviewActor* Preview = It.Value.Preview.Get())
			{
				TInlineComponentArray<UPrimitiveComponent*> Primitives(Preview);
				for (UPrimitiveComponent* P : Primitives) if (P->ComponentHasTag(TEXT("OKSkyDome"))) SkyDomeParts.Add(P);
				DepthTextureAbsentParts.Append(Preview->DepthTextureAbsentParts);
				bDepthTextureReaders |= Preview->bReadsUnityDepthTexture;
				for (const AKKPreviewActor::FBlendedPart& Blended : Preview->BlendedParts)
					if (Blended.Component.IsValid())
					{
						const TPair<int32, int32>* Rank = Ranks.Find(Blended.Component);
						BlendedParts.Add({Blended.Component, Blended.Queue, Blended.bZWrite, Blended.Renderer, Blended.Submesh, Rank ? Rank->Key : -1,
							Rank ? Rank->Value : Blended.Component->TranslucencySortPriority});
					}
			}
		// UE orders blended parts by sort priority, then distance; Unity by queue, then distance. The parts keep their own
		// priority (stacked copies of one surface) within their queue, and each queue above or below 3000 moves them a
		// hundred priorities (queues in order) after or before the characters and the rest, which draw at about 3000.
		TArray<int32> Queues;
		for (const FRankedPart& Ranked : BlendedParts) Queues.AddUnique(Ranked.Queue);
		Queues.AddUnique(3000);
		Queues.Sort();
		const int32 Zero = Queues.IndexOfByKey(3000);
		for (const FRankedPart& Ranked : BlendedParts)
			if (UPrimitiveComponent* P = Ranked.Part.Get())
			{
				const int32 Priority = FMath::Clamp(Ranked.BasePriority + (Queues.IndexOfByKey(Ranked.Queue) - Zero) * 100, -20000, 20000);
				if (!P->ComponentHasTag(TEXT("OKSkyDome")) && P->TranslucencySortPriority != Priority) P->SetTranslucentSortPriority(Priority);
			}
	}
	for (const TWeakObjectPtr<UPrimitiveComponent>& Part : SkyDomeParts)
		if (UPrimitiveComponent* P = Part.Get())
			P->SetTranslucentSortPriority(-30000 + FMath::Clamp(int32(FVector::Dist(View.GetLocation(), P->Bounds.Origin) / 100.0), 0, 20000));
	RankBlendedParts(View.GetLocation());
	MarkDepthTextureAbsent();
	PlaceSky(View);
}

void AOKSceneActor::MarkDepthTextureAbsent()
{
	// Only while something reads Unity's depth texture (SunShafts, StylizedWater's depth): a custom depth pass more.
	const bool bWant = Document && (Document->Post.bSunShafts || bDepthTextureReaders) && DepthTextureAbsentParts.Num();
	if (bWant == bDepthTextureAbsentMarked) return;
	bDepthTextureAbsentMarked = bWant;
	if (bWant) EnableCustomDepthStencil();
	for (const TWeakObjectPtr<UPrimitiveComponent>& Part : DepthTextureAbsentParts)
		if (UPrimitiveComponent* P = Part.Get())
		{
			P->SetRenderCustomDepth(bWant);
			// Past the blended parts' ranks (1-255 tested below their own rank), so their test never sees these.
			if (bWant) P->SetCustomDepthStencilValue(255);
		}
}

void AOKSceneActor::EnableCustomDepthStencil()
{
	if (bCustomDepthStencil) return;
	// The stencil half of custom depth (r.CustomDepth 3) is off by default.
	bCustomDepthStencil = true;
	if (IConsoleVariable* V = IConsoleManager::Get().FindConsoleVariable(TEXT("r.CustomDepth")))
		if (V->GetInt() != 3) V->Set(3, ECVF_SetByCode);
}

void AOKSceneActor::RankBlendedParts(const FVector& Eye)
{
	// Unity draws the blended queues after the opaque ones: by queue, and within a queue back to front by the distance
	// to the bounds' centre (up to queue 2500 front to back). A depth-writing blended part (ZWrite On) then hides what is
	// behind it from every part drawn after it, such as a night-sky shell around the view (drawn last, its centre being
	// near) behind a moon billboard drawn before it. UE's blended parts write no depth: each part gets its rank in that
	// order (1..255, spread in order when there are more), and the depth-writing ones draw their rank into custom depth
	// and stencil, which the parts' materials test (OKAddUnityTranslucentDepthTest).
	// A renderer's submeshes (parts of one source renderer) sort as one object, at the renderer's bounds' centre, and
	// draw in submesh order (a night-sky shell, then the moon billboard of the same mesh, over it).
	// OK_UNITY_DEPTH_TEST=0 (diagnostic): no ranks, so the materials test nothing.
	static const bool bOff = FPlatformMisc::GetEnvironmentVariable(TEXT("OK_UNITY_DEPTH_TEST")) == TEXT("0");
	if (bOff) return;
	TMap<TPair<const AActor*, int32>, FBox> Renderers;
	for (const FRankedPart& Ranked : BlendedParts)
		if (const UPrimitiveComponent* P = Ranked.Part.Get())
			if (Ranked.Renderer >= 0 && P->IsVisible())
				Renderers.FindOrAdd({P->GetOwner(), Ranked.Renderer}, FBox(ForceInit)) += P->Bounds.GetBox();
	struct FKey { int32 Index; int32 Queue; double Order; int32 Submesh; };
	TArray<FKey, TInlineAllocator<256>> Keys;
	for (int32 I = 0; I < BlendedParts.Num(); ++I)
		if (UPrimitiveComponent* P = BlendedParts[I].Part.Get())
			if (P->IsVisible())
			{
				const FRankedPart& Ranked = BlendedParts[I];
				const FBox* Renderer = Ranked.Renderer >= 0 ? Renderers.Find({P->GetOwner(), Ranked.Renderer}) : nullptr;
				const FVector Centre = Renderer ? Renderer->GetCenter() : P->Bounds.Origin;
				const double Distance = FVector::Dist(Eye, Centre);
				Keys.Add({I, Ranked.Queue, Ranked.Queue <= 2500 ? Distance : -Distance, Ranked.Submesh});
				// UE sorts blended parts back to front by each one's own bounds' centre: shift the submeshes of one
				// renderer to the renderer's distance, later submeshes a centimetre nearer, so they draw as Unity's do.
				if (Renderer)
				{
					const float Offset = float(Distance - FVector::Dist(Eye, P->Bounds.Origin)) - float(Ranked.Submesh);
					if (FMath::Abs(P->TranslucencySortDistanceOffset - Offset) > FMath::Max(0.5f, float(Distance) * 1e-4f))
						P->SetTranslucencySortDistanceOffset(Offset);
				}
			}
	if (!Keys.Num()) return;
	Keys.Sort([](const FKey& A, const FKey& B)
	{
		return A.Queue != B.Queue ? A.Queue < B.Queue : A.Order != B.Order ? A.Order < B.Order : A.Submesh < B.Submesh;
	});
	EnableCustomDepthStencil();
	const bool bEyeMoved = !RankEye.Equals(Eye, 0.25);
	if (bEyeMoved) RankEye = Eye;
	for (int32 K = 0; K < Keys.Num(); ++K)
	{
		FRankedPart& Ranked = BlendedParts[Keys[K].Index];
		const int32 Rank = Keys.Num() <= 255 ? K + 1 : 1 + int32(int64(K) * 254 / (Keys.Num() - 1));
		UPrimitiveComponent* P = Ranked.Part.Get();
		// The view the ranks are for: the materials test only there (not in mirror or screen captures).
		if (bEyeMoved || Ranked.Rank != Rank) P->SetCustomPrimitiveDataVector3(OKUnityDrawRankIndex + 1, Eye);
		if (Ranked.Rank == Rank) continue;
		Ranked.Rank = Rank;
		P->SetCustomPrimitiveDataFloat(OKUnityDrawRankIndex, float(Rank));
		if (Ranked.bZWrite)
		{
			P->SetRenderCustomDepth(true);
			P->SetCustomDepthStencilValue(Rank);
		}
	}
}

void AOKSceneActor::PlaceSky(const FTransform& View)
{
	// Studio's sky (SystemButtonCtrl.SkyInfo): with it on the camera clears to the skybox, behind the map and the
	// background picture. A sphere around the view, far past the background plane; its material draws by direction.
	// A map with its own sky (MapSkyInfo) always shows it, for the scene's time of day; the scene's sky setting only
	// counts on the other maps (Map.Reserve, SkyInfo.Apply).
	int32 MapPattern = -1;
	const int32 Want = !Document ? -1
		: OKStudioMapSky(Document->Map, Document->Post.SunType, MapPattern) ? MapPattern
		: Document->Post.bSky ? Document->Post.SkyPattern : -1;
	if (Want != SkyShown)
	{
		SkyShown = Want;
		UMaterialInterface* Material = nullptr;
		if (Want >= 0)
		{
			FString Error;
			Material = OKStudioSkyMaterial(Want, Error);
			if (!Material) UE_LOG(LogTemp, Warning, TEXT("OK studio: sky %d not shown: %s"), Want, *Error);
		}
		if (Material && !SkyDome.IsValid())
		{
			SkyDome.Reset(NewObject<UStaticMeshComponent>(this, TEXT("SkyDome")));
			SkyDome->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere")));
			SkyDome->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			SkyDome->SetCastShadow(false);
			SkyDome->bReceivesDecals = false;
			SkyDome->SetupAttachment(GetRootComponent());
			SkyDome->SetUsingAbsoluteLocation(true);
			SkyDome->SetUsingAbsoluteRotation(true);
			SkyDome->SetUsingAbsoluteScale(true);
			// The basic sphere is 100 cm across: a 10 km radius.
			SkyDome->SetWorldScale3D(FVector(20000.0));
			SkyDome->SetBoundsScale(1.f);
			SkyDome->RegisterComponent();
		}
		if (SkyDome.IsValid())
		{
			SkyDome->SetMaterial(0, Material);
			SkyDome->SetVisibility(Material != nullptr);
		}
	}
	if (SkyDome.IsValid() && SkyShown >= 0) SkyDome->SetWorldLocation(View.GetLocation());
}

void AOKSceneActor::BeginPlay()
{
	Super::BeginPlay();
	// The shader states (cull, outline, depth write by shader name) are read again in the background when the base
	// library's copy is missing, older, or read from other game files than the configured game has.
	FString GameRoot;
	GConfig->GetString(TEXT("/Script/KKSideloader.KKGameRoot"), TEXT("Root"), GameRoot, GGameIni);
	if (!GameRoot.IsEmpty()) ok_studio_shader_states_ensure(TCHAR_TO_UTF8(*FPaths::ConvertRelativePathToFull(GameRoot)), TCHAR_TO_UTF8(*LibraryDirectory()));
	if (!SceneJson.IsEmpty() || GetWorld()->IsPlayInEditor()) UE_LOG(LogTemp, Display, TEXT("OK studio: scene actor %s begins play (scene %d chars, document %d)"), *GetName(), SceneJson.Len(), Document ? 1 : 0);
	ShowSequenceTime();
}

void AOKSceneActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	ShowSequenceTime();
}

void AOKSceneActor::ShowSequenceTime()
{
	if (SceneJson.IsEmpty() || !GetWorld() || HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject)) return;
	if (!Document)
	{
		UE_LOG(LogTemp, Display, TEXT("OK studio: sequence scene %s in %s (%d chars of scene)"), *GetName(), *GetWorld()->GetName(), SceneJson.Len());
		UOKSceneDocument* Doc = NewObject<UOKSceneDocument>(this, NAME_None, RF_Transient);
		FString Error;
		if (!Doc->FromJson(SceneJson, Error)) { UE_LOG(LogTemp, Warning, TEXT("OK studio: sequence scene not readable: %s"), *Error); return; }
		Document = Doc;
		ShownSequenceTime = -1.f;
	}
	if (ShownSequenceTime == SequenceTime) return;
	ShownSequenceTime = SequenceTime;
	Document->ItemAnimationSeconds = SequenceTime;
	Document->EvaluateTimeline(SequenceTime);
	Sync();
	// Characters built this frame keep their first pose until the next one (posed once per frame, like the game).
	if (LastBuildFrame == GFrameCounter) { ShownSequenceTime = -1.f; return; }
	for (const FOKSceneObject& O : Document->Objects) if (O.Kind == EOKSceneObjectKind::Character) SetAnimationTime(O.Id, SequenceTime);
}

void AOKSceneActor::ReleaseScene()
{
	bSkyDomesDirty = true;
	for (auto& Pair : Shown) Release(Pair.Value);
	Shown.Reset();
	for (const TWeakObjectPtr<AKKPreviewActor>& P : ShownMapPreviews) if (P.IsValid()) P->Destroy();
	ShownMapPreviews.Reset();
	ShownMap = -1;
	ShownMapTime = -2;
	ShownMapComponents = 0;
	if (MapSun) { MapSun->DestroyComponent(); MapSun = nullptr; }
	for (FScenePicture* Picture : { &Background, &Frame })
	{
		if (Picture->Plane) Picture->Plane->DestroyComponent();
		Picture->Plane.Reset();
		Picture->Texture.Reset();
		Picture->Shown.Reset();
	}
	if (SkyDome) SkyDome->DestroyComponent();
	SkyDome.Reset();
	SkyShown = -1;
	ItemsLit.Reset();
	AmbientLit.Reset();
	AmbientSky = -1;
	AmbientMap = -1;
	Errors.Reset();
	MapError.Reset();
	bBuildsPending = false;
	ReleasedSinceCollect = 0;
}

void AOKSceneActor::Destroyed()
{
	ReleaseScene();
	Super::Destroyed();
}
