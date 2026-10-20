#include "KKRuntimeShadowSubmit.h"
#include "Async/Async.h"

namespace {
bool RuntimeShadowCasts(EBlendMode Blend,bool AsMasked){
 return Blend==BLEND_Opaque||Blend==BLEND_Masked||AsMasked;
}
bool RuntimeShadowSolidRecipe(EKKRuntimeMaterialRecipe Recipe){
 const int32 Id=int32(Recipe);
 return Recipe==EKKRuntimeMaterialRecipe::IBLDepth||Recipe==EKKRuntimeMaterialRecipe::IBLOpaque
  ||Recipe==EKKRuntimeMaterialRecipe::OilPainting||(Id>=35&&Id<=46);
}
void AddShadowReason(TArray<FString>& Reasons,const TCHAR* Reason){
 if(!Reason||!*Reason)return;
 for(const FString& Existing:Reasons)if(Existing==Reason)return;
 Reasons.Add(Reason);
}
}

FOKRuntimeShadowCoverageResult OKRuntimeShadowCoverage(const FOKRuntimeShadowMaterialFacts& Facts){
 const bool Known=int32(Facts.Recipe)>=1&&int32(Facts.Recipe)<=61;
 if(!Known)return {EOKRuntimeShadowCoverage::OriginalMaterial,TEXT("unclassified material")};
 const EBlendMode Expected=OKRuntimeSharedMaterialBlendMode(Facts.Recipe);
 const EBlendMode Blend=Facts.BlendObserved?Facts.Blend:Expected;
 if(!RuntimeShadowCasts(Blend,Facts.CastShadowAsMasked)){
  const bool Translucent=Blend==BLEND_Translucent||Blend==BLEND_AlphaComposite||OKRuntimeSharedMaterialTranslucent(Facts.Recipe);
  return {EOKRuntimeShadowCoverage::NotCaster,Translucent?TEXT("translucent material does not cast a masked shadow"):TEXT("material blend does not cast a shadow")};
 }
 if(Facts.Displacement)return {EOKRuntimeShadowCoverage::OriginalMaterial,TEXT("displacement")};
 if(Facts.WorldPositionOffset)return {EOKRuntimeShadowCoverage::OriginalMaterial,TEXT("world position offset")};
 if(Facts.PixelDepthOffset)return {EOKRuntimeShadowCoverage::OriginalMaterial,TEXT("pixel depth offset")};
 if(Facts.CastShadowAsMasked&&(Blend==BLEND_Translucent||Blend==BLEND_AlphaComposite||OKRuntimeSharedMaterialTranslucent(Facts.Recipe)))
  return {EOKRuntimeShadowCoverage::OriginalMaterial,TEXT("translucent material casts a masked shadow")};
 if(Facts.BlendObserved&&Blend!=Expected)return {EOKRuntimeShadowCoverage::OriginalMaterial,TEXT("material blend differs from the recipe")};
 if(RuntimeShadowSolidRecipe(Facts.Recipe)){
  const bool TwoSided=OKRuntimeSharedMaterialTwoSided(Facts.Recipe);
  if(Facts.TwoSidedObserved&&Facts.TwoSided!=TwoSided)return {EOKRuntimeShadowCoverage::OriginalMaterial,TEXT("two-sidedness differs from the recipe")};
  return {TwoSided?EOKRuntimeShadowCoverage::SolidTwoSided:EOKRuntimeShadowCoverage::SolidOneSided,TEXT("")};
 }
 return {EOKRuntimeShadowCoverage::OriginalMaterial,TEXT("shader coverage clip")};
}
EKKRuntimeMaterialRecipe OKRuntimeShadowProxyRecipe(EOKRuntimeShadowCoverage Coverage){
 if(Coverage==EOKRuntimeShadowCoverage::SolidOneSided)return EKKRuntimeMaterialRecipe::IBLDepth;
 if(Coverage==EOKRuntimeShadowCoverage::SolidTwoSided)return EKKRuntimeMaterialRecipe::IBLOpaque;
 return EKKRuntimeMaterialRecipe::None;
}

FOKRuntimeShadowPrepared OKBuildRuntimeShadowMesh(const FKKRuntimeRenderMeshData& Mesh,TConstArrayView<FOKRuntimeShadowMaterialFacts> Facts,uint32 SectionBoneLimit,uint64 MaxBytes){
 FOKRuntimeShadowPrepared Result;Result.Mesh.Bones=Mesh.Bones;
 auto Fail=[&](const TCHAR* Error){Result.Error=Error;Result.Mesh={};Result.Mesh.Bones=Mesh.Bones;Result.SectionKinds.Reset();Result.Face.Reset();return Result;};
 if(SectionBoneLimit==0||SectionBoneLimit>65535)return Fail(TEXT("Shadow section bone palette limit is invalid"));
 if(Mesh.Vertices.Num()!=Mesh.Weights.Num())return Fail(TEXT("Shadow vertex weights do not match the mesh"));
 if(Mesh.Indices.Num()%3)return Fail(TEXT("Shadow triangle list is not a multiple of three"));
 struct FBucket{EOKRuntimeShadowSectionKind Kind;int32 Material=INDEX_NONE;TArray<int32> Sections;};
 TArray<FBucket> Buckets;
 auto BucketFor=[&](EOKRuntimeShadowSectionKind Kind,int32 Material)->FBucket&{
  for(FBucket& Bucket:Buckets)if(Bucket.Kind==Kind&&(Kind!=EOKRuntimeShadowSectionKind::OriginalMaterial||Bucket.Material==Material))return Bucket;
  return Buckets.Add_GetRef({Kind,Material,{}});
 };
 for(int32 SectionIndex=0;SectionIndex<Mesh.Sections.Num();++SectionIndex){
  const FKKRuntimeRenderSection& Section=Mesh.Sections[SectionIndex];
  if(!Facts.IsValidIndex(Section.Material))return Fail(TEXT("Shadow section material outside facts"));
  const FOKRuntimeShadowCoverageResult Coverage=OKRuntimeShadowCoverage(Facts[Section.Material]);
  if(Coverage.Coverage==EOKRuntimeShadowCoverage::NotCaster){++Result.ExcludedSections;AddShadowReason(Result.Reasons,Coverage.Reason);continue;}
  ++Result.SourceCasterSections;
  if(Coverage.Coverage==EOKRuntimeShadowCoverage::SolidOneSided||Coverage.Coverage==EOKRuntimeShadowCoverage::SolidTwoSided){
   ++Result.SolidSourceSections;
   const auto Kind=Coverage.Coverage==EOKRuntimeShadowCoverage::SolidOneSided?EOKRuntimeShadowSectionKind::SolidOneSided:EOKRuntimeShadowSectionKind::SolidTwoSided;
   BucketFor(Kind,INDEX_NONE).Sections.Add(SectionIndex);
  }else{
   ++Result.OriginalSourceSections;AddShadowReason(Result.Reasons,Coverage.Reason);
   BucketFor(EOKRuntimeShadowSectionKind::OriginalMaterial,Section.Material).Sections.Add(SectionIndex);
  }
 }
 uint64 Bytes=0;
 auto Charge=[&](uint64 More)->bool{if(MaxBytes-Bytes<More)return false;Bytes+=More;return true;};
 auto SkeletonBone=[&](const FKKRuntimeRenderSection& Section,const FKKRuntimeMeshVertex& Vertex,const FSkinWeightInfo& Weight,int32 Influence)->int32{
  if(Influence>=Vertex.Influences||!Weight.InfluenceWeights[Influence])return INDEX_NONE;
  const uint16 Local=Weight.InfluenceBones[Influence];
  if(!Section.BoneMap.IsValidIndex(Local))return -2;
  return Section.BoneMap[Local];
 };
 for(const FBucket& Bucket:Buckets){
  TMap<uint16,uint16> BoneMap;TMap<uint32,uint32> Vertices;FKKRuntimeRenderSection Section;
  auto Begin=[&](){BoneMap.Reset();Vertices.Reset();Section={};Section.Material=Bucket.Material;Section.BaseVertex=Result.Mesh.Vertices.Num();Section.BaseIndex=Result.Mesh.Indices.Num();};
  auto End=[&](){if(!Section.Triangles)return;Section.NumVertices=Result.Mesh.Vertices.Num()-Section.BaseVertex;Result.Mesh.Sections.Add(MoveTemp(Section));Result.SectionKinds.Add(Bucket.Kind);};
  Begin();
  for(const int32 SourceIndex:Bucket.Sections){
   const FKKRuntimeRenderSection& Source=Mesh.Sections[SourceIndex];
   const uint32 IndexEnd=Source.BaseIndex+Source.Triangles*3;
   if(Source.Triangles&&(IndexEnd<Source.BaseIndex||IndexEnd>uint32(Mesh.Indices.Num())))return Fail(TEXT("Shadow section index range is outside the mesh"));
   TSet<uint16> SourceBones;for(uint16 Bone:Source.BoneMap)SourceBones.Add(Bone);
   if(uint32(SourceBones.Num())>SectionBoneLimit)return Fail(TEXT("Shadow source section exceeds the bone palette limit"));
   uint32 Added=0;for(uint16 Bone:SourceBones)Added+=!BoneMap.Contains(Bone);
   if(BoneMap.Num()&&uint32(BoneMap.Num())+Added>SectionBoneLimit){End();Begin();}
   for(uint32 Index=Source.BaseIndex;Index<IndexEnd;++Index){
    const uint32 Old=Mesh.Indices[Index];
    if(Old>=uint32(Mesh.Vertices.Num()))return Fail(TEXT("Shadow triangle vertex out of range"));
    if(const uint32* Existing=Vertices.Find(Old)){if(!Charge(sizeof(uint32)))return Fail(TEXT("Shadow mesh arrays exceed the preparation bound"));Result.Mesh.Indices.Add(*Existing);continue;}
    const FKKRuntimeMeshVertex& Vertex=Mesh.Vertices[Old];const FSkinWeightInfo& Weight=Mesh.Weights[Old];
    if(!Vertex.Influences||Vertex.Influences>12||Vertex.Position.ContainsNaN())return Fail(TEXT("Shadow vertex stream is invalid"));
    uint32 Total=0;FSkinWeightInfo Remapped{};
    for(int32 Influence=0;Influence<Vertex.Influences;++Influence){
     if(!Weight.InfluenceWeights[Influence])continue;
     const int32 Bone=SkeletonBone(Source,Vertex,Weight,Influence);
     if(Bone<0)return Fail(TEXT("Shadow vertex bone outside section palette"));
     uint16* Local=BoneMap.Find(uint16(Bone));
     if(!Local){if(uint32(Section.BoneMap.Num())>=SectionBoneLimit)return Fail(TEXT("Shadow section bone palette overflowed after the split check"));const uint16 AddedIndex=uint16(Section.BoneMap.Num());Section.BoneMap.Add(uint16(Bone));BoneMap.Add(uint16(Bone),AddedIndex);Local=BoneMap.Find(uint16(Bone));}
     Remapped.InfluenceBones[Influence]=*Local;Remapped.InfluenceWeights[Influence]=Weight.InfluenceWeights[Influence];Total+=Weight.InfluenceWeights[Influence];
    }
    if(Total!=65535)return Fail(TEXT("Shadow vertex weights are not normalized"));
    const uint64 VertexBytes=sizeof(FKKRuntimeMeshVertex)+sizeof(FSkinWeightInfo)+uint64(Vertex.Face.Num())*sizeof(FKKRuntimeFaceSparse);
    if(!Charge(VertexBytes+sizeof(uint32)))return Fail(TEXT("Shadow mesh arrays exceed the preparation bound"));
    const uint32 NewIndex=Result.Mesh.Vertices.Num();Vertices.Add(Old,NewIndex);Result.Mesh.Vertices.Add(Vertex);Result.Mesh.Weights.Add(Remapped);Result.Mesh.Indices.Add(NewIndex);
   }
   Section.Triangles+=Source.Triangles;
  }
  End();
 }
 uint64 CasterTriangles=0;for(const FBucket& Bucket:Buckets)for(const int32 SourceIndex:Bucket.Sections)CasterTriangles+=Mesh.Sections[SourceIndex].Triangles;
 uint64 Built=0;for(const FKKRuntimeRenderSection& Section:Result.Mesh.Sections)Built+=Section.Triangles;
 if(Built!=CasterTriangles||Result.Mesh.Indices.Num()!=int32(CasterTriangles*3)||Result.SectionKinds.Num()!=Result.Mesh.Sections.Num())return Fail(TEXT("Shadow compaction changed the caster triangle count"));
 for(const FKKRuntimeRenderSection& Section:Result.Mesh.Sections){
  if(Section.BoneMap.Num()>int32(SectionBoneLimit)||!Section.Triangles||Section.NumVertices==0)return Fail(TEXT("Shadow section topology is invalid"));
  const uint32 End=Section.BaseIndex+Section.Triangles*3;
  for(uint32 Index=Section.BaseIndex;Index<End;++Index){const uint32 Vertex=Result.Mesh.Indices[Index];if(Vertex<Section.BaseVertex||Vertex>=Section.BaseVertex+Section.NumVertices)return Fail(TEXT("Shadow section index escaped its vertex range"));}
 }
 Result.Mesh.Materials=Facts.Num();Result.Mesh.PrepareMs=0;Result.Face=OKPrepareRuntimeMouthStream(Result.Mesh);Result.Error.Reset();return Result;
}
TFuture<FOKRuntimeShadowPrepared> OKPrepareRuntimeShadowMeshAsync(FKKRuntimeRenderMeshData Mesh,TArray<FOKRuntimeShadowMaterialFacts> Facts,uint32 SectionBoneLimit,uint64 MaxBytes){
 return Async(EAsyncExecution::ThreadPool,[Mesh=MoveTemp(Mesh),Facts=MoveTemp(Facts),SectionBoneLimit,MaxBytes]() mutable{
  FOKRuntimeShadowPrepared Prepared=OKBuildRuntimeShadowMesh(Mesh,Facts,SectionBoneLimit,MaxBytes);
  Prepared.Mesh.WorkerThread=true;return Prepared;
 });
}
