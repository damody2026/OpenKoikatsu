#include "OKSceneActor.h"
#include "OKStudioImport.h"
#include "OKStudioImport.h"
#include "Camera/CameraComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "HAL/PlatformMisc.h"
#include "KKPreviewActor.h"
#include "KKExactSkinning.h"
#include "KKRustExtract.h"
#include "ok_extract.h"
#include "ImageUtils.h"
#include "Engine/Engine.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/Texture2D.h"
#include "Misc/Paths.h"
#include "OKAnimationAssets.h"
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
	/** The character state as ok_session_open_status takes it (empty: the card's own). */
	FString StatusJson(const FOKSceneCharacter& C)
	{
		const TSharedRef<FJsonObject> J = MakeShared<FJsonObject>();
		if (C.TearsLevel >= 0) J->SetNumberField(TEXT("tears_lv"), C.TearsLevel);
		if (C.Blush >= 0.f) J->SetNumberField(TEXT("hoho_aka_rate"), C.Blush);
		if (C.NipStand >= 0.f) J->SetNumberField(TEXT("nip_stand_rate"), C.NipStand);
		if (C.SkinGloss >= 0.f) J->SetNumberField(TEXT("skin_tuya_rate"), C.SkinGloss);
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
		return Out;
	}

	AKKPreviewActor* SpawnPreview(UWorld* World, FOkSession&& Session, FString& OutError, const TArray<FLinearColor>& ItemColors = {}, const TArray<FOKMaterialOverride>& Overrides = {},
		const FOKSceneItem* Item = nullptr, bool bMap = false, const TMap<FName, FLinearColor>& ShaderGlobals = {}, int32 Ramp = -1)
	{
		FActorSpawnParameters Params;
		Params.ObjectFlags = RF_Transient;
		AKKPreviewActor* Actor = World->SpawnActor<AKKPreviewActor>(FVector::ZeroVector, FRotator::ZeroRotator, Params);
		if (!Actor) { OutError = TEXT("preview actor not spawned"); return nullptr; }
		Actor->SetActorTickEnabled(false);
		Actor->Session = MoveTemp(Session);
		Actor->ItemColorOverrides = ItemColors;
		// Maps draw with their own material graphs (their lightmaps and emission are there). Items draw with the game's
		// own item program (the shared parents), with their colours as material overrides and Studio's toon shading.
		Actor->bUseSharedParents = !bMap;
		Actor->bStudioItemShading = Item != nullptr;
		// Items draw from the Studio root: the street's parents plus M_ForgeStudioOpaque, the depth-writing variant of the
		// game's item program (opaque items overlapping each other sort by depth, not by distance).
		if (Item) Actor->SharedMaterialRootOverride = TEXT("/Game/IndependentResources/RuntimeMaterials/StudioDisplay26");
		// The scene's shading globals (Studio's _ambientshadowG, _LineColorG, _linewidthG): Studio sets them for everything
		// it draws, characters as well as items.
		if (!bMap) Actor->StudioShaderGlobals = ShaderGlobals;
		// The scene's toon ramp (Studio's global _RampG, an mt_ramp of the game; OKHGShading's kk_<n>).
		if (!bMap && Ramp >= 0)
		{
			FString RampError;
			Actor->RampOverride = OKLoadRenderRamp(FString::Printf(TEXT("kk_%d"), Ramp), RampError);
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
		if (Object.Character.StudioAnimeNo >= 0)
		{
			// A Studio animation of the OK animation library (no Unity bundles).
			const FOKSceneCharacter& C = Object.Character;
			const FOKStudioAnim* Entry = FOKStudioCatalog::Get().FindAnim(C.StudioAnimeKind, C.StudioAnimeGroup, C.StudioAnimeCategory, C.StudioAnimeNo);
			FString AnimError;
			UOKAnimSequence* Seq = Entry && !Entry->File.IsEmpty() ? UOKAnimationLibrary::LoadOkAnimFile(LibraryDirectory() / Entry->File, nullptr, NAME_None, AnimError) : nullptr;
			const TSharedPtr<FOkAnimPlayer> Player = Seq ? Seq->CreatePlayer(AnimError) : nullptr;
			if (Player && Actor->SetOkAnimation(Player, false)) { Actor->PoseMode = 3; Out.StudioAnim.Reset(Seq); }
			else Errors.Add(Object.Id, FString::Printf(TEXT("studio animation %d/%d/%d/%d not playable: %s"), C.StudioAnimeKind, C.StudioAnimeGroup, C.StudioAnimeCategory, C.StudioAnimeNo, Entry ? (Entry->File.IsEmpty() ? TEXT("not converted") : *AnimError) : TEXT("not in the library")));
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
		// Item FK turns the item's bones before its meshes are baked.
		TArray<TSharedPtr<FJsonValue>> Fk;
		for (const FOKSceneFkBone& B : Object.Item.Fk)
			Fk.Add(MakeShared<FJsonValueArray>(TArray<TSharedPtr<FJsonValue>>{ MakeShared<FJsonValueString>(B.Bone),
				MakeShared<FJsonValueArray>(TArray<TSharedPtr<FJsonValue>>{ MakeShared<FJsonValueNumber>(B.Euler.X), MakeShared<FJsonValueNumber>(B.Euler.Y), MakeShared<FJsonValueNumber>(B.Euler.Z) }) }));
		FString FkJson;
		if (Fk.Num()) { const auto Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&FkJson); FJsonSerializer::Serialize(Fk, Writer); }
		FOkSession Session;
		if (!Session.OpenOkModelFk(LibraryDirectory(), Model, Prefab, FkJson, OutError)) return false;
		AKKPreviewActor* Actor = SpawnPreview(World, MoveTemp(Session), OutError, Object.Item.Colors, PatternOverrides(Object.Item), &Object.Item, false,
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
		return true;
	}
	case EOKSceneObjectKind::Light:
	{
		ULightComponent* Light = nullptr;
		switch (Object.Light.Type)
		{
		case EOKSceneLightType::Directional: Light = NewObject<UDirectionalLightComponent>(this); break;
		case EOKSceneLightType::Spot:
		{
			USpotLightComponent* Spot = NewObject<USpotLightComponent>(this);
			Spot->SetOuterConeAngle(Object.Light.SpotAngle);
			Spot->SetInnerConeAngle(Object.Light.SpotAngle * 0.8f);
			Spot->SetAttenuationRadius(Object.Light.Range);
			Light = Spot;
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
		// Lighting channel 2 is the map's: every light but Studio's character light reaches it.
		Light->LightingChannels.bChannel0 = true;
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

void AOKSceneActor::Release(FShown& Item)
{
	if (Item.Preview.IsValid()) { Item.Preview->Destroy(); ++ReleasedSinceCollect; }
	if (Item.Component.IsValid()) Item.Component->DestroyComponent();
	Item.Preview = nullptr;
	Item.Component = nullptr;
}

void AOKSceneActor::ApplyCharacterState(const FOKSceneObject& Object, FShown& Item)
{
	AKKPreviewActor* Actor = Item.Preview.Get();
	if (!Actor) return;
	const FOKSceneCharacter& C = Object.Character;
	// The eyes pattern is a cha_eyeset row: its FaceBlendShape pattern is the row's EyesPtn (ChaControl.ChangeEyesPtn).
	if (C.EyesPattern >= 0)
	{
		const int32 Blend = FOKStudioCatalog::Get().EyeSet(C.EyesPattern).BlendPattern;
		Actor->SetFacePattern(1, Blend >= 0 ? Blend : C.EyesPattern, false);
	}
	if (C.EyebrowPattern >= 0) Actor->SetFacePattern(2, C.EyebrowPattern, false);
	if (C.MouthPattern >= 0) Actor->SetMouthPattern(C.MouthPattern, false);
	if (C.MouthOpen >= 0.f) Actor->SetMouthOpen(C.MouthOpen);
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
	if (C.Fk != Item.AppliedFk)
	{
		TArray<FString> Bones;
		TArray<FVector> Euler;
		for (const FOKSceneFkBone& B : C.Fk) { Bones.Add(B.Bone); Euler.Add(B.Euler); }
		if (Actor->SetUserFk(Bones, Euler)) Item.AppliedFk = C.Fk;
		else Errors.Add(Object.Id, TEXT("FK not applied"));
	}
	if (C.Ik != Item.AppliedIk)
	{
		TArray<int32> Effectors;
		TArray<FVector> Positions;
		TArray<FQuat> Rotations;
        TArray<uint8> UseRotations;
        for (const FOKSceneIkTarget& T : C.Ik)
        {
            Effectors.Add(T.Effector);
            Positions.Add(AKKPreviewActor::ActorLocalToUnity(T.Position));
            const FQuat U = FOKStudioImport::UnityToUe(FVector::ZeroVector,T.Euler,FVector::OneVector).GetRotation();
            Rotations.Add(FQuat(-U.X,U.Z,U.Y,U.W));
            UseRotations.Add(T.bUseRotation ? 1 : 0);
        }
		FString Error;
		if (Actor->SetStudioIk(Effectors, Positions, Error, Rotations, UseRotations)) Item.AppliedIk = C.Ik;
		else Errors.Add(Object.Id, TEXT("IK not applied: ") + Error);
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
		else if (Object.Kind == EOKSceneObjectKind::Character) ApplyCharacterState(Object, *Item);
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
				Effect->SetVariableLinearColor(TEXT("User.Color"), FLinearColor(Color.R * Intensity, Color.G * Intensity, Color.B * Intensity, 1.f));
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
			Preview->SetActorTransform(Item->RenderCorrection * World);
			Preview->SetActorHiddenInGame(!bShown);
			if (USceneComponent* Root = Preview->GetRootComponent()) Root->SetVisibility(bShown, true);
		}
		if (USceneComponent* Component = Item->Component.Get())
		{
			Component->SetWorldTransform(World);
			Component->SetVisibility(bShown, true);
		}
	}
	for (auto It = Shown.CreateIterator(); It; ++It)
	{
		if (!Present.Contains(It.Key())) { Release(It.Value()); Errors.Remove(It.Key()); It.RemoveCurrent(); }
	}
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
		// The map is on its own lighting channel: Studio's character light (it turns with the camera) does not light it.
		TInlineComponentArray<UPrimitiveComponent*> Primitives(Actor);
		for (UPrimitiveComponent* P : Primitives) P->SetLightingChannels(false, false, !P->ComponentHasTag(TEXT("OKSunExcluded")));
		ShownMapPreviews.Add(Actor);
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
	const auto Linear = [](float C) { return C <= .04045f ? C / 12.92f : FMath::Pow((C + .055f) / 1.055f, 2.4f); };
	MapSun->SetLightColor(FLinearColor(Linear(Color.R), Linear(Color.G), Linear(Color.B), 1.f));
	MapSun->SetIntensity(Color.A * UE_PI);
	UE_LOG(LogTemp, Display, TEXT("OK studio: map time %d sun angle=(%.1f,%.1f,%.1f) colour=(%.2f,%.2f,%.2f) intensity=%.2f"), Time, Angle.R, Angle.G, Angle.B, Color.R, Color.G, Color.B, Color.A);
}

int32 AOKSceneActor::MapPartCount() const
{
	int32 Parts = 0;
	for (const TWeakObjectPtr<AKKPreviewActor>& P : ShownMapPreviews) if (P.IsValid()) Parts += P->Session.PartCount();
	return Parts;
}

void AOKSceneActor::AdvanceAnimations(float DeltaSeconds)
{
	if (!Document) return;
    Document->ItemAnimationSeconds += DeltaSeconds;
	for (auto& Pair : Shown)
	{
		AKKPreviewActor* Actor = Pair.Value.Preview.Get();
		const FOKSceneObject* Object = Document->Find(Pair.Key);
		if (!Actor || !Object || Object->Kind != EOKSceneObjectKind::Character || !Actor->OkPlayer) continue;
		Pair.Value.Time += DeltaSeconds * Object->Character.Speed;
		Actor->AnimPhase = Pair.Value.Time;
		Actor->ApplyPoseFrame(0.f, false);
	}
}

void AOKSceneActor::SetAnimationTime(int32 Id, float Time)
{
	FShown* Item = Shown.Find(Id);
	const FOKSceneObject* Object = Document ? Document->Find(Id) : nullptr;
	AKKPreviewActor* Actor = Item ? Item->Preview.Get() : nullptr;
	if (!Actor || !Object || Object->Kind != EOKSceneObjectKind::Character) return;
	if (Actor->OkPlayer)
	{
		Item->Time = Object->Character.Time + Time * Object->Character.Speed;
		Actor->AnimPhase = Item->Time;
	}
	Actor->ApplyPoseFrame(0.f, false);
}

void AOKSceneActor::Rebuild(int32 Id)
{
	if (FShown* Item = Shown.Find(Id)) Item->Kind = EOKSceneObjectKind::Folder == Item->Kind ? EOKSceneObjectKind::Character : EOKSceneObjectKind::Folder;
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

USceneComponent* AOKSceneActor::ComponentOf(int32 Id) const
{
	const FShown* Item = Shown.Find(Id);
	return Item ? Item->Component.Get() : nullptr;
}

UCameraComponent* AOKSceneActor::ActiveCamera() const
{
	return Document ? Cast<UCameraComponent>(ComponentOf(Document->ActiveCamera)) : nullptr;
}

void AOKSceneActor::UpdateLooks(float DeltaSeconds)
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
			return false;
		};
		FVector Look;
		if (Target(C.EyesLook, Look)) { Actor->SetEyeLookTarget(Look, 1, true); Pair.Value.bEyesTracking = true; }
		else if (Pair.Value.bEyesTracking) { Actor->StopEyeLookTracking(); Pair.Value.bEyesTracking = false; }
		Actor->AdvanceEyeLook(DeltaSeconds);
		// Head look runs in the pose frame: a static character re-poses for it, an animated one gets it on its next frame.
		const bool bNeck = Target(C.NeckLook, Look);
		if (bNeck || Pair.Value.bNeckLooking)
		{
			const FVector Local = Actor->GetActorTransform().InverseTransformPosition(Look);
			Actor->Session.SetHeadLook(AKKPreviewActor::ActorLocalToUnity(Local), bNeck);
			Pair.Value.bNeckLooking = bNeck;
			Actor->bHeadLookPoseDirty = true;
			if (Actor->PoseMode == 0) Actor->ApplyPoseFrame(DeltaSeconds, false);
		}
	}
}

void AOKSceneActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (bBuildsPending && Document) Sync();
	// A sequence-driven scene shows SequenceTime instead of playing on its own.
	if (!SceneJson.IsEmpty()) ShowSequenceTime();
	else if (!bPauseAnimations) AdvanceAnimations(DeltaSeconds);
	UpdateLooks(DeltaSeconds);
	PlaceAttached();
	UpdateItemLighting();
}

void AOKSceneActor::UpdateItemLighting()
{
	if (!Document) return;
	// Studio's character light (CameraLightCtrl: a child of the camera, turned by charaLight.rot) lights the Chara layer,
	// where Studio's characters and items are: its direction towards the light in Unity space, and its colour times
	// intensity (a dim character light leaves the characters in their toon shadow, as in Studio).
	const ULightComponent* Light = nullptr;
	float Intensity = 1.f;
	for (const FOKSceneObject& Object : Document->Objects)
		if (Object.Kind == EOKSceneObjectKind::Light && Object.Light.bCharactersOnly && Object.Light.Type == EOKSceneLightType::Directional)
			if (const FShown* S = Shown.Find(Object.Id); S && S->Component.IsValid())
				if ((Light = Cast<ULightComponent>(S->Component.Get()))) { Intensity = Object.Light.Intensity / 4.f; break; }
	// The sky's ambient (Studio's ambient mode is Skybox: the sky controller updates the ambient probe) on the
	// characters' unity_SHA*; without a sky, the character display's own ambient.
	if (SkyShown != AmbientSky) { AmbientSky = SkyShown; AmbientLit.Reset(); }
	FLinearColor SH[3];
	if (AmbientSky < 0 || !OKStudioSkyAmbient(AmbientSky, SH[0], SH[1], SH[2]))
	{
		FKKRuntimeMaterialScene Maker;
		OKApplyCharacterMakerLighting(Maker);
		SH[0] = Maker.Uniforms.FindRef(TEXT("unity_SHAr"));
		SH[1] = Maker.Uniforms.FindRef(TEXT("unity_SHAg"));
		SH[2] = Maker.Uniforms.FindRef(TEXT("unity_SHAb"));
	}
	for (const TPair<int32, FShown>& It : Shown)
	{
		AKKPreviewActor* Preview = It.Value.Kind == EOKSceneObjectKind::Character ? It.Value.Preview.Get() : nullptr;
		if (!Preview || AmbientLit.Contains(Preview)) continue;
		Preview->SetSharedVectorParameter(TEXT("unity_SHAr"), SH[0]);
		Preview->SetSharedVectorParameter(TEXT("unity_SHAg"), SH[1]);
		Preview->SetSharedVectorParameter(TEXT("unity_SHAb"), SH[2]);
		AmbientLit.Add(Preview);
	}
	if (!Light) return;
	const FVector F = Light->GetForwardVector();
	// Unreal (x, y, z) is Unity (-x, z, y); towards the light is against the light's forward.
	const FVector Towards = FVector(F.X, -F.Z, -F.Y).GetSafeNormal();
	const FLinearColor Direction(Towards.X, Towards.Y, Towards.Z, 0.f);
	const FLinearColor Color = Light->GetLightColor() * Intensity;
	if (!Direction.Equals(ItemLightDirection, 1e-4f) || !Color.Equals(ItemLightColor, 1e-4f)) { ItemLightDirection = Direction; ItemLightColor = Color; ItemsLit.Reset(); }
	for (const TPair<int32, FShown>& It : Shown)
	{
		const bool bLit = It.Value.Kind == EOKSceneObjectKind::Item || It.Value.Kind == EOKSceneObjectKind::Character;
		AKKPreviewActor* Preview = bLit ? It.Value.Preview.Get() : nullptr;
		if (!Preview || (It.Value.Kind == EOKSceneObjectKind::Item && !Preview->bStudioItemShading) || ItemsLit.Contains(Preview)) continue;
		Preview->SetSharedVectorParameter(TEXT("_WorldSpaceLightPos0"), ItemLightDirection);
		Preview->SetSharedVectorParameter(TEXT("_LightColor0"), ItemLightColor);
		ItemsLit.Add(Preview);
	}
}

void AOKSceneActor::PlaceAttached()
{
	if (!Document) return;
	for (const FOKSceneObject& Object : Document->Objects)
	{
		bool bAttached = false;
		for (int32 Steps = 0, Cur = Object.Id; Cur >= 0 && Steps < 256 && !bAttached; ++Steps)
		{
			const FOKSceneObject* O = Document->Find(Cur);
			if (!O) break;
			bAttached = !O->AttachBone.IsEmpty() || O->Kind == EOKSceneObjectKind::Item;
			Cur = O->Parent;
		}
		const FShown* Item = bAttached ? Shown.Find(Object.Id) : nullptr;
		if (!Item) continue;
		const FTransform World = Document->WorldTransform(Object.Id) * GetActorTransform();
		if (AKKPreviewActor* Preview = Item->Preview.Get()) Preview->SetActorTransform(Item->RenderCorrection * World);
		if (USceneComponent* Component = Item->Component.Get()) Component->SetWorldTransform(World);
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
		// The plane is 100 cm square in its XY (X up, Y right here).
		P.Key->Plane->SetWorldScale3D(FVector(Width / FMath::Max(Aspect, 0.1f) / 100.0, Width / 100.0, 1.0));
	}
	if (Frame.Plane.IsValid()) Frame.Plane->SetTranslucentSortPriority(1000);
	PlaceSky(View);
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
