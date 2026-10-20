#include "OKCrowdHairOutline.h"
#if WITH_EDITOR
#include "Engine/SkeletalMesh.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "SkeletalMeshAttributes.h"
#include "Rendering/SkeletalMeshModel.h"
#include "Rendering/SkeletalMeshLODModel.h"
#include "AssetCompilingManager.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "UObject/SavePackage.h"
namespace OKCrowdHairOutlinePrivate {
template<class T>T* Fresh(const FString& Path){return FPackageName::DoesPackageExist(Path)?nullptr:NewObject<T>(CreatePackage(*Path),*FPackageName::GetLongPackageAssetName(Path),RF_Public|RF_Standalone);}
bool Save(UObject* Asset){const FString File=FPackageName::LongPackageNameToFilename(Asset->GetOutermost()->GetName(),FPackageName::GetAssetPackageExtension());if(IFileManager::Get().FileExists(*File))return false;IFileManager::Get().MakeDirectory(*FPaths::GetPath(File),true);FSavePackageArgs Args;Args.TopLevelFlags=RF_Public|RF_Standalone;return UPackage::SavePackage(Asset->GetOutermost(),Asset,*File,Args);}
}
// Authoring only: orient hair shells in the reference walking pose. Affine
// palette bind coordinates are not a Euclidean space for testing face normals.
namespace {
bool HairShellMaterial(UMaterialInterface* Material){
 return Material&&!Material->IsTwoSided()&&Material->GetMaterial()->GetExpressionInputForProperty(MP_WorldPositionOffset)->Expression;
}
USkeletalMesh* RepairHairShell(USkeletalMesh* Source,UAnimSequence* Clip,const TSet<UMaterialInterface*>& HairMaterials,const FString& Path,int32& Changed,bool AuditOnly=false){
 Changed=0;if(!Source||!Clip||!Clip->GetSkeleton()||!Source->GetMeshDescription(0))return nullptr;
 if(!Source->GetMaterials().ContainsByPredicate([&](const auto& M){return HairMaterials.Contains(M.MaterialInterface.Get())&&HairShellMaterial(M.MaterialInterface.Get());}))return Source;
 FMeshDescription Desc=*Source->GetMeshDescription(0);FSkeletalMeshAttributes A(Desc);const auto& Ref=Source->GetRefSkeleton();const auto& Skeleton=Clip->GetSkeleton()->GetReferenceSkeleton();
 TArray<FMatrix44f> Component,Matrices;FAnimExtractContext Context(0.);Context.bEnforceCompressedDataSampling=true;
 for(int32 B=0;B<Ref.GetNum();++B){const int32 Track=Skeleton.FindBoneIndex(Ref.GetBoneName(B));if(Track==INDEX_NONE)return nullptr;FTransform Local;Clip->GetBoneTransform(Local,FSkeletonPoseBoneIndex(Track),Context,false);FMatrix44f Pose(Local.ToMatrixWithScale());if(Ref.GetParentIndex(B)!=INDEX_NONE)Pose=Pose*Component[Ref.GetParentIndex(B)];Component.Add(Pose);Matrices.Add(Source->GetRefBasesInvMatrix()[B]*Pose);}
 auto Blend=[&](FVertexID V){FMatrix44f Result(ForceInit);for(const auto W:A.GetVertexSkinWeights().Get(V)){const float Weight=float(W.GetRawWeight())/65535.f;const auto& M=Matrices[W.GetBoneIndex()];for(int32 R=0;R<4;++R)for(int32 C=0;C<4;++C)Result.M[R][C]+=M.M[R][C]*Weight;}return Result;};
 TMap<FVertexID,FVector3f> P;TMap<FVertexInstanceID,FVector3f> N;
 for(auto V:Desc.Vertices().GetElementIDs()){const auto M=Blend(V);P.Add(V,FVector3f(M.TransformPosition(A.GetVertexPositions()[V])));for(auto I:Desc.GetVertexVertexInstanceIDs(V))N.Add(I,FVector3f(M.TransformVector(A.GetVertexInstanceNormals()[I])).GetSafeNormal());}
 struct FFlip {FPolygonID Polygon;FPolygonGroupID Group;TArray<FVertexInstanceID> Corners;};TArray<FFlip> Flips;
 for(auto T:Desc.Triangles().GetElementIDs()){const auto G=Desc.GetTrianglePolygonGroup(T);const FName Slot=A.GetPolygonGroupMaterialSlotNames()[G];const int32 M=Source->GetMaterials().IndexOfByPredicate([&](const auto& S){return S.MaterialSlotName==Slot||S.ImportedMaterialSlotName==Slot;});if(M<0)return nullptr;auto* Material=Source->GetMaterials()[M].MaterialInterface.Get();if(!HairMaterials.Contains(Material)||!HairShellMaterial(Material))continue;
  const auto V=Desc.GetTriangleVertices(T);const auto I=Desc.GetTriangleVertexInstances(T);const FVector3f Geometric=FVector3f::CrossProduct(P[V[1]]-P[V[0]],P[V[2]]-P[V[0]]).GetSafeNormal();const FVector3f Normal=(N[I[0]]+N[I[1]]+N[I[2]]).GetSafeNormal();
  if(FVector3f::DotProduct(Geometric,Normal)<-.05f){if(Desc.GetPolygonTriangles(Desc.GetTrianglePolygon(T)).Num()!=1)return nullptr;Flips.Add({Desc.GetTrianglePolygon(T),G,{I[0],I[2],I[1]}});}
 }
 Changed=Flips.Num();if(AuditOnly||!Changed)return Source;
 // DeletePolygon also deletes its instances; copy corner values before deletion.
 for(const auto& F:Flips){TArray<FVertexInstanceID> New;for(auto I:F.Corners){const auto J=Desc.CreateVertexInstance(Desc.GetVertexInstanceVertex(I));New.Add(J);A.GetVertexInstanceNormals()[J]=A.GetVertexInstanceNormals()[I];A.GetVertexInstanceTangents()[J]=A.GetVertexInstanceTangents()[I];A.GetVertexInstanceBinormalSigns()[J]=A.GetVertexInstanceBinormalSigns()[I];A.GetVertexInstanceColors()[J]=A.GetVertexInstanceColors()[I];for(int32 UV=0;UV<A.GetVertexInstanceUVs().GetNumChannels();++UV)A.GetVertexInstanceUVs().Set(J,UV,A.GetVertexInstanceUVs().Get(I,UV));}Desc.DeletePolygon(F.Polygon);Desc.CreateTriangle(F.Group,New);}
 // Winding repair must not change positions, complete affine weights or morphs.
 const auto& Original=*Source->GetMeshDescription(0);FSkeletalMeshConstAttributes OriginalA(Original);
 if(Desc.Vertices().Num()!=Original.Vertices().Num())return nullptr;
 for(auto V:Original.Vertices().GetElementIDs()){if(A.GetVertexPositions()[V]!=OriginalA.GetVertexPositions()[V])return nullptr;const auto Before=OriginalA.GetVertexSkinWeights().Get(V);const auto After=A.GetVertexSkinWeights().Get(V);if(Before.Num()!=After.Num())return nullptr;for(int32 W=0;W<Before.Num();++W)if(Before[W].GetBoneIndex()!=After[W].GetBoneIndex()||Before[W].GetRawWeight()!=After[W].GetRawWeight())return nullptr;for(auto Morph:OriginalA.GetMorphTargetNames())if(A.GetVertexMorphPositionDelta(Morph)[V]!=OriginalA.GetVertexMorphPositionDelta(Morph)[V])return nullptr;}
 FElementIDRemappings Remappings;Desc.Compact(Remappings);
 auto* Mesh=OKCrowdHairOutlinePrivate::Fresh<USkeletalMesh>(Path);if(!Mesh)return nullptr;Mesh->SetSkeleton(Source->GetSkeleton());Mesh->SetRefSkeleton(Ref);Mesh->CalculateInvRefMatrices();Mesh->GetMaterials()=Source->GetMaterials();Mesh->SetHasVertexColors(Source->GetHasVertexColors());Mesh->AddLODInfo();*Mesh->GetLODInfo(0)=*Source->GetLODInfo(0);Mesh->GetImportedModel()->LODModels.Add(new FSkeletalMeshLODModel());Mesh->CreateMeshDescription(0,MoveTemp(Desc));Mesh->CommitMeshDescription(0);Mesh->Build();Mesh->PostEditChange();FAssetCompilingManager::Get().FinishAllCompilation();
 if(Mesh->GetMeshDescription(0)->Triangles().Num()!=Source->GetMeshDescription(0)->Triangles().Num()||!OKCrowdHairOutlinePrivate::Save(Mesh))return nullptr;
 UE_LOG(LogTemp,Display,TEXT("Hair shell oriented mesh=%s triangles=%d flipped=%d"),*Path,Mesh->GetMeshDescription(0)->Triangles().Num(),Changed);return Mesh;
}
}
USkeletalMesh* OKRepairCrowdHairOutline(USkeletalMesh* Source,UAnimSequence* Clip,const TSet<UMaterialInterface*>& Materials,const FString& Path,int32& Changed){return RepairHairShell(Source,Clip,Materials,Path,Changed);}
int32 OKCountCrowdHairOutlineIssues(USkeletalMesh* Source,UAnimSequence* Clip,const TSet<UMaterialInterface*>& Materials){int32 Count=0;return RepairHairShell(Source,Clip,Materials,FString(),Count,true)?Count:-1;}
#endif
