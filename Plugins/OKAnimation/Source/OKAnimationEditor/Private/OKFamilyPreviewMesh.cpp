#include "OKAnimationEditing.h"
#include "OKAnimationAssets.h"
#include "OKAnimationLibrary.h"
#include "OKUnrealSkeleton.h"
#include "Animation/Skeleton.h"
#include "Dom/JsonObject.h"
#include "Engine/SkeletalMesh.h"
#include "Materials/Material.h"
#include "Misc/PackageName.h"
#include "Misc/FileHelper.h"
#include "Interfaces/IPluginManager.h"
#include "Rendering/SkeletalMeshLODModel.h"
#include "Rendering/SkeletalMeshModel.h"
#include "ReferenceSkeleton.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "SkeletalMeshAttributes.h"
#include "BoneWeights.h"

namespace
{
	FQuat ReadQuat(const TSharedPtr<FJsonObject>& O, const TCHAR* Field)
	{
		const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
		return O->TryGetArrayField(Field, A) && A->Num() == 4 ? FQuat((*A)[0]->AsNumber(), (*A)[1]->AsNumber(), (*A)[2]->AsNumber(), (*A)[3]->AsNumber()) : FQuat::Identity;
	}
	bool ReadVec(const TSharedPtr<FJsonObject>& O, const TCHAR* Field, FVector& Out)
	{
		const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
		if (!O->TryGetArrayField(Field, A) || A->Num() != 3) return false;
		Out = FVector((*A)[0]->AsNumber(), (*A)[1]->AsNumber(), (*A)[2]->AsNumber());
		return true;
	}

	/** One box from A to B (or a cube around A when B == A), every corner skinned to Bone. */
	void AddBox(FMeshDescription& Desc, FSkeletalMeshAttributes& Attr, FPolygonGroupID Group, const FVector& A, const FVector& B, float HalfWidth, int32 Bone)
	{
		FVector Axis = B - A;
		FVector Start = A, End = B;
		if (Axis.SizeSquared() < 1.e-4)
		{
			Axis = FVector::UpVector;
			Start = A - Axis * HalfWidth;
			End = A + Axis * HalfWidth;
		}
		const FVector D = Axis.GetSafeNormal();
		const FVector U = FVector::CrossProduct(D, FMath::Abs(D.Z) < 0.9 ? FVector::UpVector : FVector::ForwardVector).GetSafeNormal() * HalfWidth;
		const FVector V = FVector::CrossProduct(D, U).GetSafeNormal() * HalfWidth;
		const FVector Corners[8] = { Start - U - V, Start + U - V, Start + U + V, Start - U + V, End - U - V, End + U - V, End + U + V, End - U + V };
		const TArray<UE::AnimationCore::FBoneWeight> Weight = { UE::AnimationCore::FBoneWeight(FBoneIndexType(Bone), uint16(65535)) };
		FVertexID Ids[8];
		for (int32 I = 0; I < 8; ++I)
		{
			Ids[I] = Desc.CreateVertex();
			Attr.GetVertexPositions()[Ids[I]] = FVector3f(Corners[I]);
			Attr.GetVertexSkinWeights().SetRaw(Ids[I], Weight);
		}
		static const int32 Faces[6][4] = { { 0, 1, 2, 3 }, { 4, 7, 6, 5 }, { 0, 4, 5, 1 }, { 1, 5, 6, 2 }, { 2, 6, 7, 3 }, { 3, 7, 4, 0 } };
		const FVector Center = (Start + End) * 0.5;
		for (const auto& F : Faces)
		{
			FVector Normal = FVector::CrossProduct(Corners[F[1]] - Corners[F[0]], Corners[F[2]] - Corners[F[0]]).GetSafeNormal();
			int32 Order[4] = { F[0], F[1], F[2], F[3] };
			// Outward winding whatever the box orientation.
			if (FVector::DotProduct(Normal, Corners[F[0]] - Center) < 0.0) { Swap(Order[1], Order[3]); Normal = -Normal; }
			for (const int32 Tri : { 0, 1 })
			{
				const int32 Idx[3] = { Order[0], Order[1 + Tri], Order[2 + Tri] };
				// Both sides (each with its own normal): visible whatever the engine's front-face winding.
				for (const int32 Side : { 0, 1 })
				{
					TArray<FVertexInstanceID> Instances;
					for (const int32 C : Idx)
					{
						const FVertexInstanceID Inst = Desc.CreateVertexInstance(Ids[C]);
						Attr.GetVertexInstanceNormals()[Inst] = FVector3f(Side ? -Normal : Normal);
						Attr.GetVertexInstanceTangents()[Inst] = FVector3f(U.GetSafeNormal());
						Attr.GetVertexInstanceBinormalSigns()[Inst] = 1.f;
						Attr.GetVertexInstanceUVs().Set(Inst, 0, FVector2f::ZeroVector);
						Attr.GetVertexInstanceColors()[Inst] = FVector4f(1.f, 1.f, 1.f, 1.f);
						Instances.Add(Inst);
					}
					if (Side) Swap(Instances[1], Instances[2]);
					Desc.CreateTriangle(Group, Instances);
				}
			}
		}
	}
}

namespace OKAnimationEditing
{
USkeletalMesh* BuildFamilyPreviewMesh(UOKSkeletonFamily* Family, const FString& PackagePath, FString& OutError)
{
	if (!Family) { OutError = TEXT("no family"); return nullptr; }
	TSharedPtr<FJsonObject> Header;
	const auto Reader = TJsonReaderFactory<>::Create(Family->RestHeaderJson);
	if (!FJsonSerializer::Deserialize(Reader, Header) || !Header) { OutError = TEXT("the family has no rest header"); return nullptr; }
	const TArray<TSharedPtr<FJsonValue>>& Bones = Header->GetArrayField(TEXT("bones"));
	if (Bones.IsEmpty()) { OutError = TEXT("the family rest has no bones"); return nullptr; }

	// Family bones in header order (parents before children), each under its parent, its anchor (nearest family
	// bone above it in the real skeleton) or, for headers without anchors, the nearest bone already placed.
	struct FBone { FName Name; int32 Parent = INDEX_NONE; FTransform Comp; };
	TArray<FBone> Out;
	TMap<FName, int32> Index;
	for (const TSharedPtr<FJsonValue>& V : Bones)
	{
		const TSharedPtr<FJsonObject> B = V->AsObject();
		FVector WorldPos;
		if (!ReadVec(B, TEXT("world_pos"), WorldPos)) { OutError = TEXT("the family rest has no world positions (save it again: OKAnim.SaveFamily)"); return nullptr; }
		FBone Bone;
		Bone.Name = FName(B->GetStringField(TEXT("name")));
		Bone.Comp = FTransform(FOKUnrealSkeleton::FromOk(ReadQuat(B, TEXT("rest_world"))), FOKUnrealSkeleton::FromOkPosition(WorldPos));
		if (const int32* P = Index.Find(FName(B->GetStringField(TEXT("parent"))))) Bone.Parent = *P;
		else if (const int32* A = B->HasField(TEXT("anchor")) ? Index.Find(FName(B->GetStringField(TEXT("anchor")))) : nullptr) Bone.Parent = *A;
		else
		{
			double Best = TNumericLimits<double>::Max();
			for (int32 I = 0; I < Out.Num(); ++I)
			{
				const double D = FVector::DistSquared(Out[I].Comp.GetLocation(), Bone.Comp.GetLocation());
				if (D < Best) { Best = D; Bone.Parent = I; }
			}
		}
		Index.Add(Bone.Name, Out.Num());
		Out.Add(Bone);
	}

	const FString Path = PackagePath.IsEmpty() ? FPackageName::GetLongPackagePath(Family->GetOutermost()->GetName()) : PackagePath;
	// Rebuilt in place when it exists (the family and its animations keep pointing at it).
	auto Existing = [&](const FString& Name) { return LoadObject<UObject>(nullptr, *(Path / Name + TEXT(".") + Name), nullptr, LOAD_NoWarn | LOAD_Quiet); };
	USkeleton* Skeleton = Cast<USkeleton>(Existing(Family->GetName() + TEXT("_Skeleton")));
	if (!Skeleton) Skeleton = Cast<USkeleton>(CreateAsset(USkeleton::StaticClass(), Path, Family->GetName() + TEXT("_Skeleton")));
	USkeletalMesh* Mesh = Cast<USkeletalMesh>(Existing(Family->GetName() + TEXT("_Preview")));
	const bool bReuse = Mesh != nullptr;
	if (bReuse) Mesh->Modify();
	else Mesh = Cast<USkeletalMesh>(CreateAsset(USkeletalMesh::StaticClass(), Path, Family->GetName() + TEXT("_Preview")));
	if (!Skeleton || !Mesh) { OutError = TEXT("cannot create the preview assets"); return nullptr; }

	// Reference skeleton: a root, then the family bones (component-space rest -> local).
	FReferenceSkeleton Ref;
	{
		FReferenceSkeletonModifier Modifier(Ref, nullptr);
		Modifier.Add(FMeshBoneInfo(TEXT("OK_Root"), TEXT("OK_Root"), INDEX_NONE), FTransform::Identity);
		for (const FBone& B : Out)
		{
			const FTransform Parent = B.Parent == INDEX_NONE ? FTransform::Identity : Out[B.Parent].Comp;
			FTransform Local = B.Comp.GetRelativeTransform(Parent);
			Local.NormalizeRotation();
			Modifier.Add(FMeshBoneInfo(B.Name, B.Name.ToString(), B.Parent == INDEX_NONE ? 0 : B.Parent + 1), Local);
		}
	}
	Mesh->SetRefSkeleton(Ref);
	Mesh->CalculateInvRefMatrices();
	Skeleton->MergeAllBonesToBoneTree(Mesh);
	Mesh->SetSkeleton(Skeleton);
	// A reused mesh keeps its LOD set-up (one LOD, one slot): the new description replaces its geometry.
	if (Mesh->GetLODNum() == 0) Mesh->AddLODInfo();
	UMaterialInterface* Material = UMaterial::GetDefaultMaterial(MD_Surface);
	if (Mesh->GetMaterials().IsEmpty()) Mesh->GetMaterials().Add(FSkeletalMaterial(Material, true, false, TEXT("Slot0")));

	FMeshDescription Desc;
	FSkeletalMeshAttributes Attr(Desc);
	Attr.Register(true);
	for (int32 B = 0; B < Ref.GetNum(); ++B)
	{
		const auto Id = Attr.CreateBone();
		Attr.GetBoneNames()[Id] = Ref.GetBoneName(B);
		Attr.GetBoneParentIndices()[Id] = Ref.GetParentIndex(B);
		Attr.GetBonePoses()[Id] = Ref.GetRefBonePose()[B];
	}
	const FPolygonGroupID Group = Desc.CreatePolygonGroup();
	Attr.GetPolygonGroupMaterialSlotNames()[Group] = TEXT("Slot0");
	TArray<TArray<int32>> Children;
	Children.SetNum(Out.Num());
	for (int32 I = 0; I < Out.Num(); ++I) if (Out[I].Parent != INDEX_NONE) Children[Out[I].Parent].Add(I);
	for (int32 I = 0; I < Out.Num(); ++I)
	{
		const FVector A = Out[I].Comp.GetLocation();
		int32 Segments = 0;
		for (const int32 C : Children[I])
		{
			// A root's offset to its children (floor -> hips) is not a body part: it would stay put when the hips move.
			if (Out[I].Parent == INDEX_NONE) continue;
			const FVector B = Out[C].Comp.GetLocation();
			const float Len = float(FVector::Dist(A, B));
			if (Len < 0.5f) continue;
			AddBox(Desc, Attr, Group, A, B, FMath::Clamp(Len * 0.1f, 0.4f, 3.5f), I + 1);
			++Segments;
		}
		if (!Segments)
		{
			const float ParentLen = Out[I].Parent == INDEX_NONE ? 5.f : float(FVector::Dist(A, Out[Out[I].Parent].Comp.GetLocation()));
			AddBox(Desc, Attr, Group, A, A, FMath::Clamp(ParentLen * 0.3f, 0.6f, 8.f), I + 1);
		}
	}
	if (Mesh->GetImportedModel()->LODModels.IsEmpty()) Mesh->GetImportedModel()->LODModels.Add(new FSkeletalMeshLODModel());
	FSkeletalMeshLODInfo* Lod = Mesh->GetLODInfo(0);
	Lod->BuildSettings.bRecomputeNormals = false;
	Lod->BuildSettings.bRecomputeTangents = false;
	Lod->BuildSettings.bRemoveDegenerates = false;
	Mesh->CreateMeshDescription(0, MoveTemp(Desc));
	Mesh->CommitMeshDescription(0);
	Mesh->Build();
	Mesh->PostEditChange();
	Skeleton->SetPreviewMesh(Mesh);
	Family->Modify();
	Family->PreviewMesh = Mesh;
	Family->MarkPackageDirty();
	return Mesh;
}

UOKSkeletonFamily* EnsureDefaultFamilyAsset(FString& OutError, bool bRebuild)
{
	UOKAnimationSettings* Settings = GetMutableDefault<UOKAnimationSettings>();
	UOKSkeletonFamily* Family = Settings->DefaultFamily.LoadSynchronous();
	const UOKSkeletonFamily* BuiltInRests = UOKAnimationLibrary::GetDefaultFamily();
	auto VariantsReady = [&]()
	{
		if (!Family) return false;
		if (!Family->VariantPreviewMeshes.Contains(TEXT("male")) || !Family->VariantPreviewMeshes[TEXT("male")].LoadSynchronous()) return false;
		for (const auto& V : Family->RestVariants) if (!Family->VariantPreviewMeshes.Contains(V.Key) || !Family->VariantPreviewMeshes[V.Key].LoadSynchronous()) return false;
		return true;
	};
	if (Family && !bRebuild && Family->PreviewMesh.LoadSynchronous() && VariantsReady()) return Family;
	if (!Family)
	{
		const UOKSkeletonFamily* BuiltIn = UOKAnimationLibrary::GetDefaultFamily();
		if (!BuiltIn) { OutError = TEXT("no built-in family rest (run OKAnim.SaveFamily in the game)"); return nullptr; }
		Family = Cast<UOKSkeletonFamily>(CreateAsset(UOKSkeletonFamily::StaticClass(), Settings->AssetRoot / TEXT("Family"), TEXT("KK_Family")));
		if (!Family) { OutError = TEXT("cannot create the family asset"); return nullptr; }
		Family->FamilyId = BuiltIn->FamilyId;
		Family->RestHeaderJson = BuiltIn->RestHeaderJson;
	}
	USkeletalMesh* Mesh = BuildFamilyPreviewMesh(Family, FString(), OutError);
	if (!Mesh) return nullptr;
	SaveAsset(Mesh->GetSkeleton());
	SaveAsset(Mesh);
	// The family's other rests (KK men) get their own preview mesh, built the same way from that rest.
	if (Family->RestVariants.IsEmpty() && BuiltInRests) Family->RestVariants = BuiltInRests->RestVariants;
	// The plugin's saved men's rest (OKAnim.SaveFamily) when the asset has none.
	if (!Family->RestVariants.Contains(TEXT("male")))
		if (const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("OKAnimation")))
		{
			FString Male;
			if (FFileHelper::LoadFileToString(Male, *FPaths::Combine(Plugin->GetBaseDir(), TEXT("Resources"), TEXT("kk_cf_j_rest_male.json")))) Family->RestVariants.Add(TEXT("male"), Male);
		}
	for (const TPair<FName, FString>& V : Family->RestVariants)
	{
		UOKSkeletonFamily* Variant = NewObject<UOKSkeletonFamily>(GetTransientPackage(), FName(*(Family->GetName() + TEXT("_") + V.Key.ToString())));
		Variant->FamilyId = Family->FamilyId;
		Variant->RestHeaderJson = V.Value;
		FString Error;
		USkeletalMesh* VariantMesh = BuildFamilyPreviewMesh(Variant, FPackageName::GetLongPackagePath(Family->GetOutermost()->GetName()), Error);
		if (!VariantMesh) { OutError += FString::Printf(TEXT("%s preview: %s; "), *V.Key.ToString(), *Error); continue; }
		SaveAsset(VariantMesh->GetSkeleton());
		SaveAsset(VariantMesh);
		Family->VariantPreviewMeshes.Add(V.Key, VariantMesh);
	}
	SaveAsset(Family);
	Settings->DefaultFamily = Family;
	Settings->TryUpdateDefaultConfigFile();
	return Family;
}
}
