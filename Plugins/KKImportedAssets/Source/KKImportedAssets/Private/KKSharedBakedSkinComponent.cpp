#include "KKSharedBakedSkinComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Rendering/SkeletalMeshLODRenderData.h"

void UKKSharedBakedSkinComponent::GetAdditionalRequiredBonesForLeader(int32 LODIndex,TArray<FBoneIndexType>& Bones) const {
 const auto* Mesh=GetSkeletalMeshAsset();
 const auto* Data=Mesh?Mesh->GetResourceForRendering():nullptr;
 if(!Data||Data->LODRenderData.IsEmpty())return;
 const auto& Required=Data->LODRenderData[FMath::Clamp(LODIndex,0,Data->LODRenderData.Num()-1)].RequiredBones;
 const auto& Map=GetLeaderBoneMap();
 TSet<FBoneIndexType> Union(Bones);
 for(FBoneIndexType Bone:Required)if(Map.IsValidIndex(Bone)&&Map[Bone]>=0)Union.Add(static_cast<FBoneIndexType>(Map[Bone]));
 Bones=Union.Array();Bones.Sort();
}
