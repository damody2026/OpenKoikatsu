#pragma once
#include "Components/SkinnedMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Animation/MorphTarget.h"
#include "SkeletalRenderPublic.h"

// Read the current pose without changing rendering mode, forcing LOD, refreshing
// animation or flushing the render thread. Morph deltas precede linear skinning.
struct FOKPaintPose {
 USkinnedMeshComponent* Component=nullptr;const FSkeletalMeshLODRenderData* LOD=nullptr;FSkinWeightVertexBuffer* Weights=nullptr;
 FTransform LocalToWorld;
 TArray<FMatrix44f> Matrices;TArray<FVector3f> Morph;TArray<FFinalSkinVertex> Fallback;
 bool Prepare(USkinnedMeshComponent* C){
  // Retain allocation, never retain pose/morph content. In particular Morph
  // must have count zero before SetNumZeroed or weights would accumulate.
  LOD=nullptr;Weights=nullptr;Matrices.Reset();Morph.Reset();Fallback.Reset();
  Component=C;LocalToWorld=C->GetComponentTransform();auto* Mesh=Cast<USkeletalMesh>(C->GetSkinnedAsset());if(!Mesh||!Mesh->GetResourceForRendering()||Mesh->GetResourceForRendering()->LODRenderData.IsEmpty())return false;
  LOD=&Mesh->GetResourceForRendering()->LODRenderData[0];Weights=C->GetSkinWeightBuffer(0);
  if(!Weights||!Weights->GetDataVertexBuffer()->GetWeightData()||Weights->GetNumVertices()!=LOD->GetNumVertices()||!LOD->StaticVertexBuffers.PositionVertexBuffer.GetVertexData())return false;
  bool Unsupported=C->HasMeshDeformer(0);for(const auto& Sets:C->GetExternalMorphSetsArray())Unsupported|=!Sets.IsEmpty();
  if(auto* Skeletal=Cast<USkeletalMeshComponent>(C))if(Skeletal->ClothBlendWeight>0)for(const auto& Section:LOD->RenderSections)Unsupported|=Section.HasClothingData();
  // Preserve the previous precise path for simulation/deformation systems which
  // ordinary bone matrices and built-in morphs cannot describe.
  if(Unsupported){C->GetCPUSkinnedVertices(Fallback,0);return !Fallback.IsEmpty();}
  C->GetCurrentRefToLocalMatrices(Matrices,0);
  const float Maximum=UE::SkeletalRender::Settings::GetMorphTargetMaxBlendWeight();
  for(const auto& Item:C->ActiveMorphTargets){if(!Item.Key||!C->MorphTargetWeights.IsValidIndex(Item.Value)||!Item.Key->HasDataForLOD(0))continue;
   const float Weight=C->MorphTargetWeights[Item.Value];if(FMath::Abs(Weight)<UE_SMALL_NUMBER||FMath::Abs(Weight)>Maximum)continue;
   if(Morph.IsEmpty())Morph.SetNumZeroed(LOD->GetNumVertices());
   for(auto Delta=Item.Key->GetDeltaIteratorForLOD(0);!Delta.AtEnd();++Delta)if(Morph.IsValidIndex(Delta->SourceIdx))Morph[Delta->SourceIdx]+=Delta->PositionDelta*Weight;
  }
  return !Matrices.IsEmpty();
 }
 FVector Position(int32 Vertex) const {
  if(Fallback.IsValidIndex(Vertex))return LocalToWorld.TransformPosition(FVector(Fallback[Vertex].Position));
  if(!LOD||Vertex<0||Vertex>=int32(LOD->GetNumVertices()))return FVector::ZeroVector;
  int32 SectionIndex,Local;LOD->GetSectionFromVertexIndex(Vertex,SectionIndex,Local);if(!LOD->RenderSections.IsValidIndex(SectionIndex))return FVector::ZeroVector;
  const auto& Section=LOD->RenderSections[SectionIndex];const FVector3f Reference=LOD->StaticVertexBuffers.PositionVertexBuffer.VertexPosition(Vertex)+(Morph.IsEmpty()?FVector3f::ZeroVector:Morph[Vertex]);FVector3f Result=FVector3f::ZeroVector;
  uint32 Offset,Count;Weights->GetVertexInfluenceOffsetCount(Vertex,Offset,Count);const auto* Data=Weights->GetDataVertexBuffer();
  // GetCurrentRefToLocalMatrices already incorporates leader-pose mapping.
  for(uint32 I=0;I<Count;++I){const uint16 Raw=Data->GetBoneWeight(Offset,Count,I);if(!Raw)continue;const uint32 Map=Data->GetBoneIndex(Offset,Count,I);if(!Section.BoneMap.IsValidIndex(Map))continue;const int32 Bone=Section.BoneMap[Map];if(Matrices.IsValidIndex(Bone))Result+=Matrices[Bone].TransformPosition(Reference)*(float(Raw)/65535.f);}
  return LocalToWorld.TransformPosition(FVector(Result));
 }
};
