#include "../OKRuntimeSkinEnvelope.h"
#include "Misc/AutomationTest.h"
#include <initializer_list>
#include <limits>

#if WITH_DEV_AUTOMATION_TESTS
namespace {
struct FClip {
 TArray<FVector4f> Keys;TArray<FUintVector4> Tracks;
 void Add(FVector Position,FVector Scale,FQuat Rotation,float Time,bool Column=false,int32 Axis=0){
  const uint32 First=uint32(Keys.Num()/3);
  Keys.Add(FVector4f(Position.X,Position.Y,Position.Z,Time));
  Keys.Add(FVector4f(Rotation.X,Rotation.Y,Rotation.Z,Rotation.W));
  FVector Stored=Scale;if(Column){Stored=FVector::ZeroVector;Stored[Axis]=Scale[Axis];}
  Keys.Add(FVector4f(Stored.X,Stored.Y,Stored.Z,0));
  const uint32 Flags=Column?(32u|(uint32(Axis)<<6)):0u;
  Tracks.Add(FUintVector4(First,1,MAX_uint32,Flags));
 }
 // Extra keys must extend this bone's contiguous block. One-bone clips only.
 void Key(int32 Bone,FVector Position,FVector Scale,FQuat Rotation,float Time){
  auto& Track=Tracks[Bone];
  Keys.Add(FVector4f(Position.X,Position.Y,Position.Z,Time));
  Keys.Add(FVector4f(Rotation.X,Rotation.Y,Rotation.Z,Rotation.W));
  FVector Stored=Scale;if(OKGpuTrackColumn(Track)){const int32 Axis=(Track.W>>6)&3;Stored=FVector::ZeroVector;Stored[Axis]=Scale[Axis];}
  Keys.Add(FVector4f(Stored.X,Stored.Y,Stored.Z,0));
  ++Track.Y;
 }
 FOKRuntimeSkinEnvelopeClip View()const{return {Keys,Tracks};}
};
struct FBone {FVector Position,Scale;FQuat Rotation;bool Column=false;int32 Axis=0;};
FClip Make(std::initializer_list<FBone> Bones){FClip Clip;for(const auto& Bone:Bones)Clip.Add(Bone.Position,Bone.Scale,Bone.Rotation,0,Bone.Column,Bone.Axis);return Clip;}
FKKRuntimeMeshVertex Vertex(FVector3f Position,std::initializer_list<FOKRuntimeSkinEnvelopeInfluence> Weights){
 FKKRuntimeMeshVertex Out;Out.Position=Position;Out.Influences=uint8(Weights.size());int32 Index=0;for(const auto& Weight:Weights)Out.Skin[Index++]={Weight.Bone,Weight.Weight};return Out;
}
bool Build(const TArray<int32>& Parents,const TArray<FMatrix44f>& Bind,const TArray<FKKRuntimeMeshVertex>& Vertices,TSharedPtr<const FOKRuntimeSkinEnvelopeGeometry,ESPMode::ThreadSafe>& Out,FString& Error){
 return OKBuildRuntimeSkinEnvelopeGeometry(Parents,Bind,Vertices,Out,Error);
}
FTransform SampleLocal(const FOKRuntimeSkinEnvelopeClip& Clip,int32 Bone,float Time){
 const auto& Track=Clip.Tracks[Bone];uint32 Lo=0,Hi=Track.Y-1;
 while(Hi-Lo>1){const uint32 Mid=(Hi+Lo)/2;if(Clip.Keys[OKGpuTrackPositionRow(Track,Mid)].W<=Time)Lo=Mid;else Hi=Mid;}
 const auto& A=Clip.Keys[OKGpuTrackPositionRow(Track,Lo)];const auto& B=Clip.Keys[OKGpuTrackPositionRow(Track,Hi)];
 const float W=B.W>A.W?FMath::Clamp((Time-A.W)/(B.W-A.W),0.f,1.f):0.f;
 const FVector Position=FMath::Lerp(FVector(A.X,A.Y,A.Z),FVector(B.X,B.Y,B.Z),W);
 if(OKGpuTrackColumn(Track))return OKGpuColumnTransform<FVector,FQuat,FTransform>(Position,FVector(OKGpuColumnLerp(OKGpuTrackColumnKey(Clip.Keys,Track,Lo),OKGpuTrackColumnKey(Clip.Keys,Track,Hi),W)),(Track.W>>6)&3);
 const auto& QA=Clip.Keys[OKGpuTrackRotationRow(Track,Lo)];const auto& QB=Clip.Keys[OKGpuTrackRotationRow(Track,Hi)];
 const auto& SA=Clip.Keys[OKGpuTrackScaleRow(Track,Lo)];const auto& SB=Clip.Keys[OKGpuTrackScaleRow(Track,Hi)];
 return FTransform(FQuat::Slerp(FQuat(QA.X,QA.Y,QA.Z,QA.W),FQuat(QB.X,QB.Y,QB.Z,QB.W),W).GetNormalized(),Position,FMath::Lerp(FVector(SA.X,SA.Y,SA.Z),FVector(SB.X,SB.Y,SB.Z),W));
}
TArray<FTransform> SampleWorld(const FOKRuntimeSkinEnvelopeGeometry& Geometry,const FOKRuntimeSkinEnvelopeClip& Clip,float Time){
 TArray<FTransform> Local,World;Local.Reserve(Geometry.Parents.Num());World.SetNum(Geometry.Parents.Num());
 for(int32 Bone=0;Bone<Geometry.Parents.Num();++Bone)Local.Add(SampleLocal(Clip,Bone,Time));
 for(int32 Bone=0;Bone<Geometry.Parents.Num();++Bone)World[Bone]=Geometry.Parents[Bone]>=0?Local[Bone]*World[Geometry.Parents[Bone]]:Local[Bone];
 return World;
}
TArray<FTransform> BlendWorld(const FOKRuntimeSkinEnvelopeGeometry& Geometry,const FOKRuntimeSkinEnvelopeClip& A,const FOKRuntimeSkinEnvelopeClip& B,float TimeA,float TimeB,float Weight){
 TArray<FTransform> Local,World;World.SetNum(Geometry.Parents.Num());
 for(int32 Bone=0;Bone<Geometry.Parents.Num();++Bone)Local.Add(OKGpuBlendLocal(SampleLocal(A,Bone,TimeA),SampleLocal(B,Bone,TimeB),Weight,A.Tracks[Bone]));
 for(int32 Bone=0;Bone<Geometry.Parents.Num();++Bone)World[Bone]=Geometry.Parents[Bone]>=0?Local[Bone]*World[Geometry.Parents[Bone]]:Local[Bone];
 return World;
}
FVector Skinned(const FOKRuntimeSkinEnvelopeGeometry& Geometry,const TArray<FTransform>& World,int32 Vertex){
 const auto& Point=Geometry.Vertices[Vertex];FVector Sum=FVector::ZeroVector;
 for(uint8 Influence=0;Influence<Point.Influences;++Influence){if(!Point.Skin[Influence].Weight)continue;const int32 Bone=Point.Skin[Influence].Bone;const FVector Local(Geometry.InverseBind[Bone].TransformPosition(Point.Position));Sum+=World[Bone].TransformPosition(Local)*(double(Point.Skin[Influence].Weight)/65535.);}
 return Sum;
}
double LooseRadius(const FOKRuntimeSkinEnvelopeGeometry& Geometry,const FOKRuntimeSkinEnvelopeClip& Clip){
 const int32 Count=Geometry.Parents.Num();TArray<double> Translation,Scale,Bind;Translation.Init(0,Count);Scale.Init(0,Count);Bind.Init(0,Count);
 for(const auto& Point:Geometry.Vertices)for(uint8 Influence=0;Influence<Point.Influences;++Influence)if(Point.Skin[Influence].Weight){const int32 Bone=Point.Skin[Influence].Bone;Bind[Bone]=FMath::Max(Bind[Bone],FVector(Geometry.InverseBind[Bone].TransformPosition(Point.Position)).Size());}
 double Radius=0;
 for(int32 Bone=0;Bone<Count;++Bone){
  const auto& Track=Clip.Tracks[Bone];
  for(uint32 Key=0;Key<Track.Y;++Key){const auto& Position=Clip.Keys[OKGpuTrackPositionRow(Track,Key)];Translation[Bone]=FMath::Max(Translation[Bone],double(FVector3f(Position.X,Position.Y,Position.Z).Size()));Scale[Bone]=FMath::Max(Scale[Bone],OKRuntimeTrackScale(Clip.Keys,Track,Key).GetAbsMax());}
  const int32 Parent=Geometry.Parents[Bone];const double ParentScale=Parent>=0?Scale[Parent]:1.;
  Translation[Bone]=(Parent>=0?Translation[Parent]:0)+ParentScale*Translation[Bone];Scale[Bone]*=ParentScale;
  Radius=FMath::Max(Radius,Translation[Bone]+Scale[Bone]*Bind[Bone]);
 }
 return Radius;
}
bool Contains(FAutomationTestBase& Test,const TCHAR* Name,const TSharedPtr<const FOKRuntimeSkinEnvelopeGeometry,ESPMode::ThreadSafe>& Geometry,TConstArrayView<FOKRuntimeSkinEnvelopeClip> Clips,const FOKRuntimeSkinEnvelopeClip& SampleClip,float Time,int32 Vertex){
 if(!Geometry.IsValid()){Test.AddError(FString::Printf(TEXT("%s has no geometry"),Name));return false;}
 double Radius=0;FString Error;if(!OKRuntimeSkinEnvelopeRadius(*Geometry,Clips,Radius,Error)){Test.AddError(FString::Printf(TEXT("%s radius failed: %s"),Name,*Error));return false;}
 const double Reach=Skinned(*Geometry,SampleWorld(*Geometry,SampleClip,Time),Vertex).Size();
 if(Reach>Radius+1e-3){Test.AddError(FString::Printf(TEXT("%s skinned %.6f outside radius %.6f"),Name,Reach,Radius));return false;}
 const FBox Box=OKRuntimeSkinEnvelopeBounds(Radius);if(!FMath::IsNearlyEqual(Box.GetExtent().GetMax(),Radius+1.,1e-4)){Test.AddError(TEXT("Published box is not radius plus one centimetre"));return false;}
 return true;
}
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOKRuntimeSkinEnvelopeTest,"OpenKoikatsu.Runtime.SkinEnvelope",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FOKRuntimeSkinEnvelopeTest::RunTest(const FString& Parameters){
 const FQuat Id=FQuat::Identity;FString Error;TSharedPtr<const FOKRuntimeSkinEnvelopeGeometry,ESPMode::ThreadSafe> Geometry;
 const TArray<FMatrix44f> IdentityBind={FMatrix44f::Identity};
 const auto Rest=Vertex(FVector3f(100,80,3),{{0,65535}});
 const TArray<int32> One={INDEX_NONE};
 TestFalse(TEXT("Empty geometry is rejected"),Build(One,IdentityBind,{},Geometry,Error));
 TestFalse(TEXT("Unnormalized weights are rejected"),Build(One,IdentityBind,{Vertex(FVector3f(1,0,0),{{0,65534}})},Geometry,Error));
 TestFalse(TEXT("Bone index out of range is rejected"),Build(One,IdentityBind,{Vertex(FVector3f(1,0,0),{{1,65535}})},Geometry,Error));
 TestFalse(TEXT("A parent that is not earlier is rejected"),Build({0},IdentityBind,{Rest},Geometry,Error));
 TArray<int32> TooMany;TArray<FMatrix44f> TooManyBind;TooMany.Init(INDEX_NONE,OKRuntimeSkinEnvelopeMaxBones+1);TooManyBind.Init(FMatrix44f::Identity,TooMany.Num());
 TestFalse(TEXT("Bone cap is rejected before use"),Build(TooMany,TooManyBind,{Vertex(FVector3f(1,0,0),{{0,65535}})},Geometry,Error));
 auto Zero=Make({{FVector::ZeroVector,FVector(0,0,2),Id,false,0}});
 TestTrue(TEXT("Zero-scale pose builds"),Build(One,IdentityBind,{Rest},Geometry,Error));
 TestTrue(TEXT("Zero-scale axis contains its endpoint"),Contains(*this,TEXT("zero endpoint"),Geometry,{Zero.View()},Zero.View(),0,0));
 Zero.Key(0,FVector::ZeroVector,FVector(0,0,4),Id,1);
 double ZeroRadius=0;TestTrue(TEXT("Interpolated zero-scale clip has a radius"),OKRuntimeSkinEnvelopeRadius(*Geometry,{Zero.View()},ZeroRadius,Error));
 TestTrue(TEXT("Interpolated zero-scale pose stays inside"),Contains(*this,TEXT("zero mid"),Geometry,{Zero.View()},Zero.View(),.5f,0));
 TestTrue(TEXT("Zero-scale radius stays below the full-length sphere"),ZeroRadius+1.<LooseRadius(*Geometry,Zero.View()));
 const double ZeroReach=Skinned(*Geometry,SampleWorld(*Geometry,Zero.View(),.5f),0).Size();
 TestTrue(TEXT("Zero-scale midpoint is tighter than either endpoint's full sphere"),ZeroReach<=ZeroRadius+1e-3&&ZeroRadius<200.);

 const TArray<int32> Two={INDEX_NONE,INDEX_NONE};const TArray<FMatrix44f> TwoBind={FMatrix44f::Identity,FMatrix44f::Identity};
 const auto Partial=Vertex(FVector3f(0,0,10),{{0,60000},{1,5535}});
 auto PartialClip=Make({{FVector::ZeroVector,FVector(1,1,1),Id,false,0},{FVector::ZeroVector,FVector(10,10,10),Id,false,0}});
 TestTrue(TEXT("Partial weights build"),Build(Two,TwoBind,{Partial},Geometry,Error));
 double PartialRadius=0;TestTrue(TEXT("Partial radius"),OKRuntimeSkinEnvelopeRadius(*Geometry,{PartialClip.View()},PartialRadius,Error));
 TestTrue(TEXT("Partial weight contains the blend"),Contains(*this,TEXT("partial"),Geometry,{PartialClip.View()},PartialClip.View(),0,0));
 TestTrue(TEXT("Partial weight is below the far bone sphere"),PartialRadius+1.<LooseRadius(*Geometry,PartialClip.View()));
 const auto Ignored=Vertex(FVector3f(5,0,0),{{0,65535},{1,0}});
 auto Far=Make({{FVector::ZeroVector,FVector::OneVector,Id,false,0},{FVector(4000,0,0),FVector::OneVector,Id,false,0}});
 TestTrue(TEXT("Zero weight on a far bone builds"),Build(Two,TwoBind,{Ignored},Geometry,Error));
 double NearRadius=0;TestTrue(TEXT("Near radius"),OKRuntimeSkinEnvelopeRadius(*Geometry,{Far.View()},NearRadius,Error));
 TestTrue(TEXT("Unweighted bone does not import its translation"),NearRadius<10.&&Contains(*this,TEXT("unweighted"),Geometry,{Far.View()},Far.View(),0,0));
 TestTrue(TEXT("Unweighted bone is below the per-bone sphere"),NearRadius+1.<LooseRadius(*Geometry,Far.View()));

 const TArray<int32> Chain={INDEX_NONE,0};const auto Accessory=Vertex(FVector3f(0,0,0),{{1,65535}});
 auto AccessoryClip=Make({{FVector::ZeroVector,FVector::OneVector,Id,false,0},{FVector(0,40,0),FVector::OneVector,Id,false,0}});
 TestTrue(TEXT("Weighted accessory builds"),Build(Chain,{FMatrix44f::Identity,FMatrix44f::Identity},{Accessory},Geometry,Error));
 TestTrue(TEXT("Weighted rigid accessory stays inside its translation"),Contains(*this,TEXT("accessory"),Geometry,{AccessoryClip.View()},AccessoryClip.View(),0,0));
 const TArray<int32> ParentChain={INDEX_NONE,0};const auto ChildPoint=Vertex(FVector3f(4,0,1),{{1,65535}});
 auto ParentClip=Make({{FVector(5,0,0),FVector(0,0,2),Id,false,0},{FVector(0,3,0),FVector::OneVector,Id,false,0}});
 TestTrue(TEXT("Parent chain builds"),Build(ParentChain,{FMatrix44f::Identity,FMatrix44f::Identity},{ChildPoint},Geometry,Error));
 TestTrue(TEXT("Non-uniform parent scale still contains the child"),Contains(*this,TEXT("parent scale"),Geometry,{ParentClip.View()},ParentClip.View(),0,0));

 FMatrix44f Scaled=FMatrix44f::Identity;Scaled.M[0][0]=2;
 auto ScaledClip=Make({{FVector::ZeroVector,FVector(3,1,1),Id,false,0}});
 TestTrue(TEXT("Scaled inverse bind builds"),Build(One,{Scaled},{Vertex(FVector3f(1,0,0),{{0,65535}})},Geometry,Error));
 double ScaledRadius=0;TestTrue(TEXT("Scaled inverse bind radius"),OKRuntimeSkinEnvelopeRadius(*Geometry,{ScaledClip.View()},ScaledRadius,Error));
 TestTrue(TEXT("Inverse bind is applied before scale"),FMath::IsNearlyEqual(ScaledRadius,6.,1e-4)&&Contains(*this,TEXT("inverse"),Geometry,{ScaledClip.View()},ScaledClip.View(),0,0));
 const FQuat Turn(FVector(0,0,1),PI/2.);
 auto Turned=Make({{FVector::ZeroVector,FVector(2,0,0),Turn,false,0}});
 TestTrue(TEXT("Rotated bone builds"),Build(One,IdentityBind,{Vertex(FVector3f(3,4,0),{{0,65535}})},Geometry,Error));
 TestTrue(TEXT("Rotation does not escape the per-axis radius"),Contains(*this,TEXT("rotation"),Geometry,{Turned.View()},Turned.View(),0,0));

 const TArray<int32> PaletteParents={INDEX_NONE,0,0,0};TArray<FMatrix44f> PaletteBind;PaletteBind.Init(FMatrix44f::Identity,4);
 const auto PalettePoint=Vertex(FVector3f(4,5,6),{{1,21845},{2,21845},{3,21845}});
 FClip Palette;
 Palette.Add(FVector::ZeroVector,FVector::OneVector,Id,0,false,0);
 Palette.Add(FVector(1,0,0),FVector(6,0,0),Id,0,true,0);
 Palette.Add(FVector(0,2,0),FVector(0,9,0),Id,0,true,1);
 Palette.Add(FVector(0,0,3),FVector(0,0,12),Turn,0,true,2);
 TestTrue(TEXT("Palette columns build"),Build(PaletteParents,PaletteBind,{PalettePoint},Geometry,Error));
 double PaletteRadius=0;TestTrue(TEXT("Palette radius"),OKRuntimeSkinEnvelopeRadius(*Geometry,{Palette.View()},PaletteRadius,Error));
 TestTrue(TEXT("Palette columns contain the weighted pose"),Contains(*this,TEXT("palette"),Geometry,{Palette.View()},Palette.View(),0,0));
 TestTrue(TEXT("Palette columns are below one uniform column sphere"),PaletteRadius+1.<LooseRadius(*Geometry,Palette.View()));

 FClip Narrow=Make({{FVector::ZeroVector,FVector::OneVector,Id,false,0}});
 FClip Wide;Wide.Add(FVector::ZeroVector,FVector::OneVector,Id,0,false,0);Wide.Key(0,FVector(0,30,0),FVector::OneVector,Id,1);
 const auto WidePoint=Vertex(FVector3f(10,0,0),{{0,65535}});
 TestTrue(TEXT("Two clips build"),Build(One,IdentityBind,{WidePoint},Geometry,Error));
 double NarrowRadius=0,WideRadius=0,BothRadius=0;
 TestTrue(TEXT("Narrow clip radius"),OKRuntimeSkinEnvelopeRadius(*Geometry,{Narrow.View()},NarrowRadius,Error));
 TestTrue(TEXT("Wide clip radius"),OKRuntimeSkinEnvelopeRadius(*Geometry,{Wide.View()},WideRadius,Error));
 TestTrue(TEXT("Combined clip radius"),OKRuntimeSkinEnvelopeRadius(*Geometry,{Narrow.View(),Wide.View()},BothRadius,Error));
 TestTrue(TEXT("A later clip can be larger without changing the earlier clip"),NarrowRadius+1.<WideRadius&&FMath::IsNearlyEqual(BothRadius,FMath::Max(NarrowRadius,WideRadius),1e-6));
 TestTrue(TEXT("Wide key is outside the other clip and inside the union"),Skinned(*Geometry,SampleWorld(*Geometry,Wide.View(),1),0).Size()>NarrowRadius+1.&&Contains(*this,TEXT("wide end"),Geometry,{Wide.View()},Wide.View(),1,0));
 TestTrue(TEXT("Key interpolation stays inside its clip"),Contains(*this,TEXT("wide mid"),Geometry,{Wide.View()},Wide.View(),.5f,0));
 if(!Geometry.IsValid())AddError(TEXT("Crossfade has no geometry"));
 else for(const float Weight:{0.f,.25f,.5f,.75f,1.f}){
  const double Reach=Skinned(*Geometry,BlendWorld(*Geometry,Narrow.View(),Wide.View(),0,1,Weight),0).Size();
  if(Reach>BothRadius+1e-3)AddError(FString::Printf(TEXT("Crossfade %.2f reached %.6f outside %.6f"),Weight,Reach,BothRadius));
 }
 // A local-space crossfade is not a lerp of fully skinned endpoint vertices.
 // Complementary parent scale / child translation can exceed both endpoint spheres.
 const auto MixedPoint=Vertex(FVector3f(1,0,0),{{1,65535}});
 auto MixedA=Make({{FVector::ZeroVector,FVector(.01,.01,.01),Id,false,0},{FVector(100,0,0),FVector::OneVector,Id,false,0}});
 auto MixedB=Make({{FVector::ZeroVector,FVector(100,100,100),Id,false,0},{FVector::ZeroVector,FVector::OneVector,Id,false,0}});
 TSharedPtr<const FOKRuntimeSkinEnvelopeGeometry,ESPMode::ThreadSafe> MixedGeometry;
 TestTrue(TEXT("Complementary hierarchy builds"),Build(Chain,TwoBind,{MixedPoint},MixedGeometry,Error));
 if(MixedGeometry.IsValid()){
  double A=0,B=0,Combined=0;
  TestTrue(TEXT("Complementary first endpoint bound"),OKRuntimeSkinEnvelopeRadius(*MixedGeometry,{MixedA.View()},A,Error));
  TestTrue(TEXT("Complementary second endpoint bound"),OKRuntimeSkinEnvelopeRadius(*MixedGeometry,{MixedB.View()},B,Error));
  TestTrue(TEXT("Complementary joint local-channel bound"),OKRuntimeSkinEnvelopeRadius(*MixedGeometry,{MixedA.View(),MixedB.View()},Combined,Error));
  const double Mid=Skinned(*MixedGeometry,BlendWorld(*MixedGeometry,MixedA.View(),MixedB.View(),0,0,.5f),0).Size();
  TestTrue(TEXT("Endpoint sphere union misses a local hierarchy crossfade"),Mid>FMath::Max(A,B)+1.);
  TestTrue(TEXT("Joint channels contain hierarchy crossfade"),Mid<=Combined+1e-3);
 }
 FBox Union=OKRuntimeSkinEnvelopeBounds(NarrowRadius);Union+=OKRuntimeSkinEnvelopeBounds(WideRadius);
 const FBox WideBox=OKRuntimeSkinEnvelopeBounds(WideRadius);
 TestTrue(TEXT("Union keeps the larger action and does not shrink it"),Union.GetExtent().Equals(WideBox.GetExtent(),1e-4));
 FBox Retained=WideBox;Retained+=OKRuntimeSkinEnvelopeBounds(NarrowRadius);
 TestTrue(TEXT("A smaller later action does not shrink the retained box"),Retained.GetExtent().Equals(WideBox.GetExtent(),1e-4));
 auto Broken=Narrow;Broken.Keys[2].X=std::numeric_limits<float>::infinity();
 double Unused=0;TestFalse(TEXT("Non-finite scale fails closed"),OKRuntimeSkinEnvelopeRadius(*Geometry,{Broken.View()},Unused,Error));
 TestFalse(TEXT("Track count must match the rig"),OKRuntimeSkinEnvelopeRadius(*Geometry,{FOKRuntimeSkinEnvelopeClip{}},Unused,Error));
 return !HasAnyErrors();
}
#endif
