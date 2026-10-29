#include "OKStudioCatalog.h"
#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "OKSceneActor.h"
#include "KKRustExtract.h"
#include "ok_extract.h"
#include "ImageUtils.h"
#include "Engine/Texture2D.h"
#include "UObject/StrongObjectPtr.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	TUniquePtr<FOKStudioCatalog> GCatalog;

	int32 Int(const TSharedPtr<FJsonObject>& O, const TCHAR* Key) { double V = 0.; return O->TryGetNumberField(Key, V) ? int32(V) : 0; }
	FString Str(const TSharedPtr<FJsonObject>& O, const TCHAR* Key) { FString V; O->TryGetStringField(Key, V); return V; }
	void Flags(const TSharedPtr<FJsonObject>& O, const TCHAR* Key, bool (&Out)[3])
	{
		const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
		if (O->TryGetArrayField(Key, A)) for (int32 I = 0; I < 3 && I < A->Num(); ++I) Out[I] = (*A)[I]->AsBool();
	}
	const TArray<TSharedPtr<FJsonValue>>& Array(const TSharedPtr<FJsonObject>& Root, const TCHAR* Key)
	{
		static const TArray<TSharedPtr<FJsonValue>> Empty;
		const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
		return Root->TryGetArrayField(Key, A) ? *A : Empty;
	}
}

const FOKStudioCatalog& FOKStudioCatalog::Get()
{
	if (!GCatalog)
	{
		GCatalog = MakeUnique<FOKStudioCatalog>();
		GCatalog->Load(AOKSceneActor::LibraryDirectory() / TEXT("studio_items.json"));
		GCatalog->LoadModItems(AOKSceneActor::LibraryDirectory() / TEXT("studio_mod_items.json"));
		GCatalog->LoadAnims(AOKSceneActor::LibraryDirectory() / TEXT("studio_anims.json"));
		GCatalog->LoadEffects(EffectsFile());
        GCatalog->LoadChildAnimations(AOKSceneActor::LibraryDirectory() / TEXT("studio_child_animations.json"));
	}
	return *GCatalog;
}

void FOKStudioCatalog::Reload() { GCatalog.Reset(); }

bool FOKStudioCatalog::Load(const FString& File)
{
	FString Text;
	TSharedPtr<FJsonObject> Root;
	if (!FFileHelper::LoadFileToString(Text, *File)) { Error = TEXT("no studio catalogue: ") + File; return false; }
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root) { Error = TEXT("bad studio catalogue: ") + File; return false; }
	for (const TSharedPtr<FJsonValue>& V : Array(Root, TEXT("groups"))) { const auto O = V->AsObject(); Groups.Add({ -1, Int(O, TEXT("id")), Str(O, TEXT("name")) }); }
	for (const TSharedPtr<FJsonValue>& V : Array(Root, TEXT("categories"))) { const auto O = V->AsObject(); Categories.Add({ Int(O, TEXT("group")), Int(O, TEXT("id")), Str(O, TEXT("name")) }); }
	for (const TSharedPtr<FJsonValue>& V : Array(Root, TEXT("map_categories"))) { const auto O = V->AsObject(); MapCategories.Add({ -1, Int(O, TEXT("id")), Str(O, TEXT("name")) }); }
	for (const TSharedPtr<FJsonValue>& V : Array(Root, TEXT("items")))
	{
		const auto O = V->AsObject();
		FOKStudioCatalogItem I;
		I.Group = Int(O, TEXT("group")); I.Category = Int(O, TEXT("category")); I.No = Int(O, TEXT("no"));
		I.Name = Str(O, TEXT("name")); I.Model = Str(O, TEXT("model")); I.Prefab = Str(O, TEXT("prefab")); I.Child = Str(O, TEXT("child"));
		O->TryGetBoolField(TEXT("anime"), I.bAnime); O->TryGetBoolField(TEXT("scale"), I.bScale);
		Flags(O, TEXT("colors"), I.Colors); Flags(O, TEXT("patterns"), I.Patterns);
		double LineWidth = -1.;
		if (O->TryGetNumberField(TEXT("line_width"), LineWidth)) I.LineWidth = float(LineWidth);
		O->TryGetBoolField(TEXT("alpha"), I.bAlpha);
		Items.Add(MoveTemp(I));
	}
	for (const TSharedPtr<FJsonValue>& V : Array(Root, TEXT("maps")))
	{
		const auto O = V->AsObject();
		FOKStudioCatalogMap M{ Int(O, TEXT("no")), Int(O, TEXT("category")), Str(O, TEXT("name")), Str(O, TEXT("model")), Str(O, TEXT("prefab")) };
		for (const TSharedPtr<FJsonValue>& R : Array(O, TEXT("roots"))) M.Roots.Add(R->AsString());
		if (M.Roots.IsEmpty()) M.Roots.Add(M.Prefab);
		Maps.Add(MoveTemp(M));
	}
	for (const TSharedPtr<FJsonValue>& V : Array(Root, TEXT("fk_bones")))
	{
		const auto O = V->AsObject();
		FkBones.Add({ Int(O, TEXT("no")), Str(O, TEXT("bone")), Str(O, TEXT("name")), Int(O, TEXT("group")), Int(O, TEXT("op")) });
	}
	for (const TSharedPtr<FJsonValue>& V : Array(Root, TEXT("filters")))
	{
		const auto O = V->AsObject();
		Filters.Add({ Int(O, TEXT("no")), Str(O, TEXT("name")), Str(O, TEXT("bundle")), Str(O, TEXT("file")) });
	}
	for (const TSharedPtr<FJsonValue>& V : Array(Root, TEXT("accessory_points")))
	{
		const auto O = V->AsObject();
		AccessoryPoints.Add({ Int(O, TEXT("no")), Int(O, TEXT("group")), Str(O, TEXT("key")), Str(O, TEXT("name")), Str(O, TEXT("bone")) });
	}
	// Pattern names (ok-extract --oklib studio-names): eyes from the character list, hands from Studio's HandAnime.
	for (const TSharedPtr<FJsonValue>& V : Array(Root, TEXT("eye_names"))) { const auto O = V->AsObject(); EyeNames.Add(Int(O, TEXT("id")), Str(O, TEXT("name"))); }
	for (const TSharedPtr<FJsonValue>& V : Array(Root, TEXT("hand_names")))
	{
		const auto O = V->AsObject();
		HandNames[FMath::Clamp(Int(O, TEXT("group")), 0, 1)].Add(Int(O, TEXT("id")), Str(O, TEXT("name")));
	}
	bLoaded = true;
	return true;
}

void FOKStudioCatalog::LoadModItems(const FString& File)
{
	FString Text;
	TSharedPtr<FJsonObject> Root;
	if (!FFileHelper::LoadFileToString(Text, *File) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root) return;
	for (const TSharedPtr<FJsonValue>& V : Array(Root, TEXT("items")))
	{
		const auto O = V->AsObject();
		FOKStudioCatalogItem I;
		I.Model = Str(O, TEXT("model"));
		if (I.Model.IsEmpty()) continue;
		I.Guid = Str(O, TEXT("guid"));
		I.Group = Int(O, TEXT("group")); I.Category = Int(O, TEXT("category")); I.No = Int(O, TEXT("no"));
		I.Name = Str(O, TEXT("name")); I.Prefab = Str(O, TEXT("prefab")); I.Child = Str(O, TEXT("child"));
		O->TryGetBoolField(TEXT("anime"), I.bAnime); O->TryGetBoolField(TEXT("scale"), I.bScale);
		Flags(O, TEXT("colors"), I.Colors); Flags(O, TEXT("patterns"), I.Patterns);
		// Two mods may use the same group, category and number: the first keeps the key, both stay findable by GUID.
		const TPair<FString, int32> Key(I.Guid.ToLower(), I.No);
		if (!ModItems.Contains(Key)) ModItems.Add(Key, Items.Num());
		Items.Add(MoveTemp(I));
	}
	// The mods' group and category names for the item browser: only ones the vanilla lists lack (mods reuse vanilla
	// groups) and that hold an item of the library.
	TSet<int32> UsedGroups;
	TSet<FIntPoint> UsedCategories;
	for (const TPair<TPair<FString, int32>, int32>& M : ModItems) { const FOKStudioCatalogItem& I = Items[M.Value]; UsedGroups.Add(I.Group); UsedCategories.Add(FIntPoint(I.Group, I.Category)); }
	for (const TSharedPtr<FJsonValue>& V : Array(Root, TEXT("groups")))
	{
		const auto O = V->AsObject();
		const int32 Id = Int(O, TEXT("id"));
		if (UsedGroups.Contains(Id) && !Groups.ContainsByPredicate([Id](const FOKStudioCatalogGroup& G) { return G.Id == Id; })) Groups.Add({ -1, Id, Str(O, TEXT("name")) });
	}
	for (const TSharedPtr<FJsonValue>& V : Array(Root, TEXT("categories")))
	{
		const auto O = V->AsObject();
		const int32 G = Int(O, TEXT("group")), Id = Int(O, TEXT("id"));
		if (UsedCategories.Contains(FIntPoint(G, Id)) && !Categories.ContainsByPredicate([G, Id](const FOKStudioCatalogGroup& C) { return C.Group == G && C.Id == Id; }))
			Categories.Add({ G, Id, Str(O, TEXT("name")) });
	}
}

const FOKStudioCatalogItem* FOKStudioCatalog::FindModItem(const FString& Guid, int32 Slot) const
{
	const int32* Index = ModItems.Find(TPair<FString, int32>(Guid.ToLower(), Slot));
	return Index && Items.IsValidIndex(*Index) ? &Items[*Index] : nullptr;
}

void FOKStudioCatalog::LoadAnims(const FString& File)
{
	FString Text;
	TSharedPtr<FJsonObject> Root;
	if (!FFileHelper::LoadFileToString(Text, *File) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root) return;
	for (const TSharedPtr<FJsonValue>& V : Array(Root, TEXT("groups"))) { const auto O = V->AsObject(); AnimGroups.Add({ -1, Int(O, TEXT("id")), Str(O, TEXT("name")) }); }
	for (const TSharedPtr<FJsonValue>& V : Array(Root, TEXT("categories"))) { const auto O = V->AsObject(); AnimCategories.Add({ Int(O, TEXT("group")), Int(O, TEXT("id")), Str(O, TEXT("name")) }); }
	for (const TSharedPtr<FJsonValue>& V : Array(Root, TEXT("anims")))
	{
		const auto O = V->AsObject();
		FOKStudioAnim A;
		A.Kind = Str(O, TEXT("kind")) == TEXT("hanime") ? 1 : 0;
		A.Group = Int(O, TEXT("group")); A.Category = Int(O, TEXT("category")); A.No = Int(O, TEXT("no"));
		A.Name = Str(O, TEXT("name")); A.File = Str(O, TEXT("file"));
		double D = 0.; O->TryGetNumberField(TEXT("duration"), D); A.Duration = float(D);
		O->TryGetBoolField(TEXT("looping"), A.bLooping);
		Anims.Add(MoveTemp(A));
	}
}

FString FOKStudioCatalog::EffectsFile()
{
	return FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir() / TEXT("IndependentResources/OKStudio/effects.json"));
}

void FOKStudioCatalog::LoadEffects(const FString& File)
{
	FString Text;
	TSharedPtr<FJsonObject> Root;
	if (!FFileHelper::LoadFileToString(Text, *File) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root) return;
	for (const TSharedPtr<FJsonValue>& V : Array(Root, TEXT("categories"))) EffectCategories.Add(V->AsString());
	for (const TSharedPtr<FJsonValue>& V : Array(Root, TEXT("effects")))
	{
		const auto O = V->AsObject();
		FOKStudioEffect E;
		E.Id = Str(O, TEXT("id")); E.Name = Str(O, TEXT("name")); E.Category = Str(O, TEXT("category"));
		E.Asset = Str(O, TEXT("asset")); E.Template = Str(O, TEXT("template"));
		const TArray<TSharedPtr<FJsonValue>> Rgb = Array(O, TEXT("color"));
		if (Rgb.Num() >= 3) E.Color = FLinearColor(float(Rgb[0]->AsNumber()), float(Rgb[1]->AsNumber()), float(Rgb[2]->AsNumber()), 1.f);
		for (const TSharedPtr<FJsonValue>& K : Array(O, TEXT("kks")))
		{
			const TArray<TSharedPtr<FJsonValue>>& A = K->AsArray();
			if (A.Num() == 3) E.Kks.Add(FIntVector(int32(A[0]->AsNumber()), int32(A[1]->AsNumber()), int32(A[2]->AsNumber())));
		}
		if (!E.Id.IsEmpty()) Effects.Add(MoveTemp(E));
	}
}

const FOKStudioEffect* FOKStudioCatalog::FindEffect(const FString& Id) const
{
	return Effects.FindByPredicate([&](const FOKStudioEffect& E) { return E.Id == Id; });
}

const FOKStudioEffect* FOKStudioCatalog::EffectForKks(int32 Group, int32 Category, int32 No) const
{
	const FIntVector K(Group, Category, No);
	return Effects.FindByPredicate([&](const FOKStudioEffect& E) { return E.Kks.Contains(K); });
}

const FOKStudioAnim* FOKStudioCatalog::FindAnim(int32 Kind, int32 Group, int32 Category, int32 No) const
{
	return Anims.FindByPredicate([&](const FOKStudioAnim& A) { return A.Kind == Kind && A.Group == Group && A.Category == Category && A.No == No; });
}

const FOKStudioAnim* FOKStudioCatalog::FindAnimAnyKind(int32 Group, int32 Category, int32 No) const
{
	const FOKStudioAnim* A = FindAnim(0, Group, Category, No);
	return A ? A : FindAnim(1, Group, Category, No);
}

const FOKStudioFilter* FOKStudioCatalog::FindFilter(int32 No) const
{
	return Filters.FindByPredicate([No](const FOKStudioFilter& F) { return F.No == No; });
}

FOKStudioCatalog::FEyeSet FOKStudioCatalog::EyeSet(int32 Ptn) const
{
	static TMap<int32, FEyeSet> Cache;
	if (const FEyeSet* Hit = Cache.Find(Ptn)) return *Hit;
	FEyeSet Set;
	const FTCHARToUTF8 Lib(*AOKSceneActor::LibraryDirectory());
	const int32 Size = ok_eye_set_json(Lib.Get(), Ptn, nullptr, 0);
	if (Size > 0)
	{
		TArray<uint8> Bytes;
		Bytes.SetNumZeroed(Size + 1);
		ok_eye_set_json(Lib.Get(), Ptn, Bytes.GetData(), Size);
		TSharedPtr<FJsonObject> Root;
		if (FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(UTF8_TO_TCHAR(reinterpret_cast<const char*>(Bytes.GetData()))), Root) && Root)
		{
			Set.BlendPattern = int32(Root->GetNumberField(TEXT("eyes_ptn")));
			Root->TryGetBoolField(TEXT("changes_parts"), Set.bChangesParts);
		}
	}
	Cache.Add(Ptn, Set);
	return Set;
}

UTexture* FOKStudioCatalog::SourceFilterLut(int32 No) const
{
	static TMap<int32, TStrongObjectPtr<UTexture2D>> Cache;
	if (const auto* Hit = Cache.Find(No)) return Hit->Get();
	UTexture2D* Lut = nullptr;
	if (const FOKStudioFilter* F = FindFilter(No))
	{
		const FTCHARToUTF8 Lib(*AOKSceneActor::LibraryDirectory()), Bundle(*F->Bundle), File(*F->File);
		const int32 Size = ok_studio_texture_png(Lib.Get(), Bundle.Get(), File.Get(), nullptr, 0);
		TArray<uint8> Png; FImage Source;
		if (Size > 0) { Png.SetNumUninitialized(Size); ok_studio_texture_png(Lib.Get(), Bundle.Get(), File.Get(), Png.GetData(), Size); }
		if (Size > 0 && FImageUtils::DecompressImage(Png.GetData(), Png.Num(), Source) && Source.SizeX == 1024 && Source.SizeY == 32)
		{
			FImage Src; Source.CopyTo(Src, ERawImageFormat::BGRA8, EGammaSpace::sRGB);
			Lut = UTexture2D::CreateTransient(1024, 32, PF_B8G8R8A8);
			void* Out = Lut->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE);
			FMemory::Memcpy(Out, Src.RawData.GetData(), Src.RawData.Num());
			Lut->GetPlatformData()->Mips[0].BulkData.Unlock();
			Lut->SRGB = false; Lut->NeverStream = true; Lut->Filter = TF_Bilinear;
			Lut->AddressX = Lut->AddressY = TA_Clamp; Lut->UpdateResource();
		}
	}
	Cache.Add(No, TStrongObjectPtr<UTexture2D>(Lut)); return Lut;
}

UTexture* FOKStudioCatalog::FilterLut(int32 No) const
{
	static TMap<int32, TStrongObjectPtr<UTexture2D>> Cache;
	if (const TStrongObjectPtr<UTexture2D>* Hit = Cache.Find(No)) return Hit->Get();
	UTexture2D* Lut = nullptr;
	if (const FOKStudioFilter* F = FindFilter(No))
	{
		const FTCHARToUTF8 Lib(*AOKSceneActor::LibraryDirectory()), Bundle(*F->Bundle), File(*F->File);
		const int32 Size = ok_studio_texture_png(Lib.Get(), Bundle.Get(), File.Get(), nullptr, 0);
		TArray<uint8> Png;
		FImage Source;
		if (Size > 0) { Png.SetNumUninitialized(Size); ok_studio_texture_png(Lib.Get(), Bundle.Get(), File.Get(), Png.GetData(), Size); }
		if (Size > 0 && FImageUtils::DecompressImage(Png.GetData(), Png.Num(), Source) && Source.SizeX == 1024 && Source.SizeY == 32)
		{
			// Amplify Color: 32 blue slices of 32x32 side by side (x = r + 32 b, y = g from the top). Unreal: 16 slices of 16x16.
			FImage Src;
			Source.CopyTo(Src, ERawImageFormat::BGRA8, EGammaSpace::sRGB);
			const TArrayView64<FColor> In = Src.AsBGRA8();
			auto At = [&In](int32 R, int32 G, int32 B) { return FLinearColor(In[int64(G) * 1024 + B * 32 + R].ReinterpretAsLinear()); };
			auto Sample = [&At](float R, float G, float B)
			{
				const int32 R0 = FMath::FloorToInt(R), G0 = FMath::FloorToInt(G), B0 = FMath::FloorToInt(B);
				const int32 R1 = FMath::Min(R0 + 1, 31), G1 = FMath::Min(G0 + 1, 31), B1 = FMath::Min(B0 + 1, 31);
				const float Fr = R - R0, Fg = G - G0, Fb = B - B0;
				auto Plane = [&](int32 Bi) { return FMath::Lerp(FMath::Lerp(At(R0, G0, Bi), At(R1, G0, Bi), Fr), FMath::Lerp(At(R0, G1, Bi), At(R1, G1, Bi), Fr), Fg); };
				return FMath::Lerp(Plane(B0), Plane(B1), Fb);
			};
			Lut = UTexture2D::CreateTransient(256, 16, PF_B8G8R8A8);
			FColor* Out = static_cast<FColor*>(Lut->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE));
			for (int32 G = 0; G < 16; ++G)
				for (int32 B = 0; B < 16; ++B)
					for (int32 R = 0; R < 16; ++R)
						Out[G * 256 + B * 16 + R] = Sample(R * 31.f / 15.f, G * 31.f / 15.f, B * 31.f / 15.f).QuantizeRound();
			Lut->GetPlatformData()->Mips[0].BulkData.Unlock();
			// The LUT holds display-encoded colours indexed by display-encoded colours: sampled as stored, not linearised.
			Lut->SRGB = false;
			Lut->LODGroup = TEXTUREGROUP_ColorLookupTable;
			Lut->Filter = TF_Bilinear;
			Lut->AddressX = Lut->AddressY = TA_Clamp;
			Lut->UpdateResource();
		}
	}
	Cache.Add(No, TStrongObjectPtr<UTexture2D>(Lut));
	return Lut;
}

const FOKStudioAccessoryPoint* FOKStudioCatalog::FindAccessoryPoint(int32 No) const
{
	return AccessoryPoints.FindByPredicate([No](const FOKStudioAccessoryPoint& P) { return P.No == No; });
}

const FOKStudioFkBone* FOKStudioCatalog::FindFkBone(const FString& Bone) const
{
	return FkBones.FindByPredicate([&](const FOKStudioFkBone& B) { return B.Bone.Equals(Bone, ESearchCase::IgnoreCase); });
}

const FOKStudioCatalogItem* FOKStudioCatalog::FindItem(int32 Group, int32 Category, int32 No) const
{
	// Indexed on first use (the catalogue with mod items holds tens of thousands; scenes look up thousands).
	if (ItemIndex.Num() == 0 && Items.Num() > 0)
		for (int32 K = 0; K < Items.Num(); ++K) ItemIndex.FindOrAdd(FIntVector(Items[K].Group, Items[K].Category, Items[K].No), K);
	const int32* K = ItemIndex.Find(FIntVector(Group, Category, No));
	return K ? &Items[*K] : nullptr;
}

FTransform FOKStudioCatalog::ItemChildPoint(int32 Group, int32 Category, int32 No) const
{
	if (No < 0) return FTransform::Identity;
	const FIntVector Key(Group, Category, No);
	if (const FTransform* Found = ChildPoints.Find(Key)) return *Found;
	FTransform Point = FTransform::Identity;
	const FOKStudioCatalogItem* I = FindItem(Group, Category, No);
	FVector P = FVector::ZeroVector, S = FVector::OneVector; FQuat Q = FQuat::Identity;
	// Unity prefab space to UE (the converter's basis change: x -> -x, y <-> z; metres to cm).
	if (I && !I->Child.IsEmpty() && FOkSession::OkModelNodeWorld(AOKSceneActor::LibraryDirectory(), I->Model, I->Prefab, I->Child, P, Q, S))
	{
		Point = FTransform(FQuat(-Q.X, Q.Z, Q.Y, Q.W).GetNormalized(), FVector(-P.X, P.Z, P.Y) * 100.0, FVector(S.X, S.Z, S.Y));
		if (FOkSession::OkModelNodeWorld(AOKSceneActor::LibraryDirectory(), I->Model, I->Prefab, I->Prefab, P, Q, S))
		{
			const FTransform Root(FQuat(-Q.X, Q.Z, Q.Y, Q.W).GetNormalized(), FVector(-P.X, P.Z, P.Y) * 100.0, FVector(S.X, S.Z, S.Y));
			Point = Point.GetRelativeTransform(Root);
		}
	}
	ChildPoints.Add(Key, Point);
	return Point;
}

const FOKStudioCatalogMap* FOKStudioCatalog::FindMap(int32 No) const
{
	return Maps.FindByPredicate([&](const FOKStudioCatalogMap& M) { return M.No == No; });
}

TArray<const FOKStudioCatalogItem*> FOKStudioCatalog::ItemsIn(int32 Group, int32 Category) const
{
	TArray<const FOKStudioCatalogItem*> Out;
	for (const FOKStudioCatalogItem& I : Items) if (I.Group == Group && I.Category == Category) Out.Add(&I);
	return Out;
}

TArray<const FOKStudioCatalogGroup*> FOKStudioCatalog::CategoriesOf(int32 Group) const
{
	TArray<const FOKStudioCatalogGroup*> Out;
	for (const FOKStudioCatalogGroup& C : Categories) if (C.Group == Group) Out.Add(&C);
	return Out;
}

void FOKStudioCatalog::LoadChildAnimations(const FString& File)
{
    FString Text;
    TSharedPtr<FJsonObject> Root;
    if (!FFileHelper::LoadFileToString(Text, *File) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root) return;
    for (const auto& Value : Array(Root, TEXT("animations")))
    {
        const auto O = Value->AsObject();
        if (!O || Int(O, TEXT("pattern")) != 0) continue;
        FOKStudioChildAnimation A;
        double D = 0.;
        O->TryGetNumberField(TEXT("duration"), D);
        A.Duration = float(D);
        O->TryGetBoolField(TEXT("looping"), A.bLooping);
        for (const auto& Frame : Array(O, TEXT("frames")))
        {
            const auto F = Frame->AsObject();
            const auto& P = Array(F, TEXT("pos"));
            const auto& R = Array(F, TEXT("rot"));
            const auto& S = Array(F, TEXT("scale"));
            if (P.Num()!=3 || R.Num()!=4 || S.Num()!=3) { A.Frames.Reset(); break; }
            A.Frames.Add(FTransform(FQuat(-R[0]->AsNumber(), R[2]->AsNumber(), R[1]->AsNumber(), R[3]->AsNumber()).GetNormalized(),
                FVector(-P[0]->AsNumber(), P[2]->AsNumber(), P[1]->AsNumber()) * 100., FVector(S[0]->AsNumber(), S[2]->AsNumber(), S[1]->AsNumber())));
        }
        if (A.Duration > 0.f && A.Frames.Num() >= 2) ChildAnimations.Add(FIntVector(Int(O,TEXT("group")),Int(O,TEXT("category")),Int(O,TEXT("no"))), MoveTemp(A));
    }
}

FTransform FOKStudioCatalog::AnimatedItemChildPoint(int32 Group, int32 Category, int32 No, int32 Pattern, float NormalizedTime, float ElapsedSeconds, float Speed) const
{
    const FOKStudioChildAnimation* A = Pattern == 0 ? ChildAnimations.Find(FIntVector(Group, Category, No)) : nullptr;
    if (!A || !FMath::IsFinite(NormalizedTime) || !FMath::IsFinite(ElapsedSeconds) || !FMath::IsFinite(Speed)) return ItemChildPoint(Group, Category, No);
    const double Clock = double(NormalizedTime) + double(ElapsedSeconds) * double(Speed) / double(A->Duration);
    const double Phase = A->bLooping ? Clock - FMath::FloorToDouble(Clock) : FMath::Clamp(Clock, 0., 1.);
    const double Index = Phase * (A->Frames.Num() - 1);
    const int32 Lo = FMath::Clamp(FMath::FloorToInt(Index), 0, A->Frames.Num() - 1), Hi = FMath::Min(Lo + 1, A->Frames.Num() - 1);
    FTransform Out;
    Out.Blend(A->Frames[Lo], A->Frames[Hi], float(Index - Lo));
    return Out;
}
