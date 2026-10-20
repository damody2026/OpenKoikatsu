#pragma once
#include "KKRuntimeMesh.h"
#include "OKIndependentGpuPose.h"

// Plain bind snapshot for background envelope math. Workers never touch the skeletal mesh.
// Caps reject the character; they do not clamp the radius.
inline constexpr int32 OKRuntimeSkinEnvelopeMaxBones=65536;
inline constexpr int32 OKRuntimeSkinEnvelopeMaxVertices=2000000;
inline constexpr uint64 OKRuntimeSkinEnvelopeMaxBytes=256ull*1024*1024;

struct FOKRuntimeSkinEnvelopeInfluence {uint16 Bone=0,Weight=0;};
struct FOKRuntimeSkinEnvelopeVertex {
 FVector3f Position=FVector3f::ZeroVector;
 uint8 Influences=0;
 FOKRuntimeSkinEnvelopeInfluence Skin[12];
};
struct FOKRuntimeSkinEnvelopeGeometry {
 TArray<int32> Parents;
 TArray<FMatrix44f> InverseBind;
 TArray<FOKRuntimeSkinEnvelopeVertex> Vertices;
};
struct FOKRuntimeSkinEnvelopeClip {
 TConstArrayView<FVector4f> Keys;
 TConstArrayView<FUintVector4> Tracks;
};
static_assert(uint64(OKRuntimeSkinEnvelopeMaxVertices)*sizeof(FOKRuntimeSkinEnvelopeVertex)<=OKRuntimeSkinEnvelopeMaxBytes,"vertex cap must fit the byte cap");

inline FVector3d OKRuntimeTrackScale(TConstArrayView<FVector4f> Keys,const FUintVector4& Track,uint32 K){
 if(OKGpuTrackColumn(Track)){FVector3d Scale=FVector3d::ZeroVector;Scale[(Track.W>>6)&3]=OKGpuTrackColumnKey(Keys,Track,K).Size();return Scale;}
 return FVector3d(FVector3f(Keys[OKGpuTrackScaleRow(Track,K)]));
}
inline FBox OKRuntimeSkinEnvelopeBounds(double Radius){const FVector Extent(Radius+1.);return FBox(-Extent,Extent);}

inline bool OKBuildRuntimeSkinEnvelopeGeometry(TConstArrayView<int32> Parents,TConstArrayView<FMatrix44f> InverseBind,TConstArrayView<FKKRuntimeMeshVertex> Vertices,TSharedPtr<const FOKRuntimeSkinEnvelopeGeometry,ESPMode::ThreadSafe>& Out,FString& Error){
 Out.Reset();
 if(Parents.Num()<=0||Parents.Num()>OKRuntimeSkinEnvelopeMaxBones||Parents.Num()!=InverseBind.Num()){Error=TEXT("Runtime skin envelope rig size invalid");return false;}
 if(Vertices.Num()<=0||Vertices.Num()>OKRuntimeSkinEnvelopeMaxVertices){Error=TEXT("Runtime skin envelope vertex count invalid");return false;}
 const uint64 Bytes=uint64(Parents.Num())*(sizeof(int32)+sizeof(FMatrix44f))+uint64(Vertices.Num())*sizeof(FOKRuntimeSkinEnvelopeVertex);
 if(Bytes>OKRuntimeSkinEnvelopeMaxBytes){Error=TEXT("Runtime skin envelope allocation exceeds its bound");return false;}
 for(int32 Bone=0;Bone<Parents.Num();++Bone){
  if(Parents[Bone]<INDEX_NONE||Parents[Bone]>=Bone){Error=TEXT("Runtime skin envelope parent is not topological");return false;}
  for(int32 Row=0;Row<4;++Row)for(int32 Column=0;Column<4;++Column)if(!FMath::IsFinite(InverseBind[Bone].M[Row][Column])){Error=TEXT("Runtime skin envelope inverse bind is not finite");return false;}
 }
 auto Geometry=MakeShared<FOKRuntimeSkinEnvelopeGeometry,ESPMode::ThreadSafe>();
 Geometry->Parents.Append(Parents);Geometry->InverseBind.Append(InverseBind);Geometry->Vertices.Reserve(Vertices.Num());
 for(const auto& Source:Vertices){
  if(Source.Influences>12||Source.Position.ContainsNaN()){Error=TEXT("Runtime skin envelope vertex is invalid");return false;}
  FOKRuntimeSkinEnvelopeVertex Copy;Copy.Position=Source.Position;Copy.Influences=Source.Influences;uint32 Sum=0;
  for(uint8 Influence=0;Influence<Source.Influences;++Influence){
   const auto& Weight=Source.Skin[Influence];
   if(Weight.Bone>=uint32(Parents.Num())){Error=TEXT("Runtime skin envelope vertex bone out of range");return false;}
   Copy.Skin[Influence]={Weight.Bone,Weight.Weight};Sum+=Weight.Weight;
  }
  if(Sum!=65535){Error=TEXT("Runtime skin envelope vertex weights are not normalized");return false;}
  Geometry->Vertices.Add(Copy);
 }
 Out=Geometry;Error.Reset();return true;
}

// Keywise maximum of |translation| and per-axis |scale|, then the same weighted
// vertex sum as the private bank. A zero scale axis stays zero. Parent scale is
// the parent's max absolute axis, which contains non-uniform parent scale.
// |lerp| of keys, scales, and origin-centered clip results stays inside this radius.
inline bool OKRuntimeSkinEnvelopeRadius(const FOKRuntimeSkinEnvelopeGeometry& Geometry,TConstArrayView<FOKRuntimeSkinEnvelopeClip> Clips,double& OutRadius,FString& Error){
 const int32 BoneCount=Geometry.Parents.Num();
 if(BoneCount<=0||Geometry.InverseBind.Num()!=BoneCount||Clips.Num()<=0){Error=TEXT("Runtime skin envelope input invalid");return false;}
 for(int32 Bone=0;Bone<BoneCount;++Bone)if(Geometry.Parents[Bone]<INDEX_NONE||Geometry.Parents[Bone]>=Bone){Error=TEXT("Runtime skin envelope parent is not topological");return false;}
 TArray<double> TranslationRadius,ScaleRadius;TArray<FVector3d> ScaleAxes;
 TranslationRadius.Init(0,BoneCount);ScaleRadius.Init(0,BoneCount);ScaleAxes.Init(FVector3d::ZeroVector,BoneCount);
 for(const auto& Clip:Clips){
  if(Clip.Tracks.Num()!=BoneCount){Error=TEXT("Runtime skin envelope track count differs from rig");return false;}
  for(int32 Bone=0;Bone<BoneCount;++Bone){
   const auto& Track=Clip.Tracks[Bone];
   if(!OKGpuTrackModeValid(Track)||Track.Y==0){Error=TEXT("Runtime skin envelope track is invalid");return false;}
   uint64 Need=0;
   if(OKGpuTrackVectorKeys(Track))Need=uint64(Track.X)+(OKGpuTrackPacked(Track)?1ull+Track.Y:uint64(Track.Y)*2ull);
   else if(OKGpuTrackPacked(Track))Need=uint64(Track.X)+2ull+Track.Y;
   else Need=(uint64(Track.X)+Track.Y)*3ull;
   if(Need>uint64(Clip.Keys.Num())){Error=TEXT("Runtime skin envelope key count invalid");return false;}
   for(uint32 Key=0;Key<Track.Y;++Key){
    const uint32 PositionRow=OKGpuTrackPositionRow(Track,Key);
    const auto& Position=Clip.Keys[PositionRow];
    if(!FMath::IsFinite(Position.X)||!FMath::IsFinite(Position.Y)||!FMath::IsFinite(Position.Z)||!FMath::IsFinite(Position.W)){Error=TEXT("Runtime skin envelope position key invalid");return false;}
    if(OKGpuTrackColumn(Track)){
     const uint32 RotationRow=OKGpuTrackRotationRow(Track,Key);
     if(RotationRow>=uint32(Clip.Keys.Num())){Error=TEXT("Runtime skin envelope column key invalid");return false;}
     if(!OKGpuTrackVectorKeys(Track)&&OKGpuTrackScaleRow(Track,Key)>=uint32(Clip.Keys.Num())){Error=TEXT("Runtime skin envelope column key invalid");return false;}
    }else if(OKGpuTrackScaleRow(Track,Key)>=uint32(Clip.Keys.Num())){Error=TEXT("Runtime skin envelope scale key invalid");return false;}
    const FVector3d Scale=OKRuntimeTrackScale(Clip.Keys,Track,Key);
    if(!FMath::IsFinite(Scale.X)||!FMath::IsFinite(Scale.Y)||!FMath::IsFinite(Scale.Z)){Error=TEXT("Runtime skin envelope scale key invalid");return false;}
    TranslationRadius[Bone]=FMath::Max(TranslationRadius[Bone],double(FVector3f(Position.X,Position.Y,Position.Z).Size()));
    for(int32 Axis=0;Axis<3;++Axis)ScaleAxes[Bone][Axis]=FMath::Max(ScaleAxes[Bone][Axis],double(FMath::Abs(Scale[Axis])));
   }
  }
 }
 for(int32 Bone=0;Bone<BoneCount;++Bone){
  const int32 Parent=Geometry.Parents[Bone];const double ParentScale=Parent>=0?ScaleRadius[Parent]:1;
  TranslationRadius[Bone]=(Parent>=0?TranslationRadius[Parent]:0)+ParentScale*TranslationRadius[Bone];
  ScaleAxes[Bone]*=ParentScale;ScaleRadius[Bone]=ScaleAxes[Bone].GetAbsMax();
 }
 double Radius=0;
 for(const auto& Vertex:Geometry.Vertices){
  double VertexRadius=0;
  for(uint8 Influence=0;Influence<Vertex.Influences;++Influence){
   const int32 Bone=Vertex.Skin[Influence].Bone;if(!Vertex.Skin[Influence].Weight)continue;
   if(Bone<0||Bone>=BoneCount){Error=TEXT("Runtime skin envelope vertex bone out of range");return false;}
   const FVector3d Local(Geometry.InverseBind[Bone].TransformPosition(Vertex.Position));
   const double LocalRadius=(Local*ScaleAxes[Bone]).Size();
   VertexRadius+=(TranslationRadius[Bone]+LocalRadius)*double(Vertex.Skin[Influence].Weight)/65535.;
  }
  Radius=FMath::Max(Radius,VertexRadius);
 }
 if(!FMath::IsFinite(Radius)||Radius<=0){Error=TEXT("Runtime animation envelope invalid");return false;}
 OutRadius=Radius;Error.Reset();return true;
}
