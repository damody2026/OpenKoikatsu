#include "KKPrivateGpuBounds.h"

namespace {
struct FPrivateBoundsKey {
 FUintVector4 Bones;
 bool operator==(const FPrivateBoundsKey& Other)const{return Bones==Other.Bones;}
 friend uint32 GetTypeHash(const FPrivateBoundsKey& K){return HashCombine(HashCombine(K.Bones.X,K.Bones.Y),HashCombine(K.Bones.Z,K.Bones.W));}
};
void TransformInterval(const FVector4d& Lo,const FVector4d& Hi,const FMatrix44f& M,FVector& OutLo,FVector& OutHi){
 for(int32 A=0;A<3;++A){OutLo[A]=OutHi[A]=0.;for(int32 C=0;C<4;++C){const double V=M.M[C][A];OutLo[A]+=V*(V>=0?Lo[C]:Hi[C]);OutHi[A]+=V*(V>=0?Hi[C]:Lo[C]);}}
}
}
bool OKBuildPrivateGpuBounds(const FKKPrivateGpuSource& Source,TConstArrayView<FOKGpuDeformDraw> Draw,FKKPrivateGpuBounds& Out,FString& Error){
 if(!Source.Revision||!OKValidateGpuDeformSource(Source.Vertices,Draw,Source.PaletteCount,Source.FallbackSlot,Error))return false;
 FKKPrivateGpuBounds Next;Next.Revision=Source.Revision;Next.PaletteCount=Source.PaletteCount;
 TMap<FPrivateBoundsKey,int32> Map;
 for(const auto& V:Source.Vertices){
  float Total=0;FUintVector4 Bones=V.Bones;bool Signed=false;
  for(int32 S=0;S<4;++S)if(FMath::Abs(V.Weights[S])>=1.e-6f){Total+=V.Weights[S];Signed|=V.Weights[S]<0;}
  const bool Fallback=Total<=1.e-5f;
  for(int32 S=0;S<4;++S)if(Fallback||FMath::Abs(V.Weights[S])<1.e-6f)Bones[S]=Source.FallbackSlot;
  auto Accumulate=[&](FUintVector4 GroupBones,const FVector4d& Weights){
  const FPrivateBoundsKey Key{GroupBones};int32* Found=Map.Find(Key);int32 Index;
  if(Found)Index=*Found;else{Index=Next.Groups.Num();Map.Add(Key,Index);auto& G=Next.Groups.AddDefaulted_GetRef();G.Bones=GroupBones;
   for(int32 S=0;S<4;++S){G.Min[S]=FVector4d(DBL_MAX,DBL_MAX,DBL_MAX,DBL_MAX);G.Max[S]=-G.Min[S];}}
  auto& G=Next.Groups[Index];
  for(int32 S=0;S<4;++S){const double W=Weights[S];
   for(int32 C=0;C<4;++C){const double X=(C==3?1.:double(V.Position[C]))*W;if(!FMath::IsFinite(X)){Error=TEXT("Private GPU bounds coefficient overflow");return false;}
    G.Min[S][C]=FMath::Min(G.Min[S][C],X);G.Max[S][C]=FMath::Max(G.Max[S][C],X);}}return true;};
  if(!Fallback&&!Signed){
   // Normal authored weights form a convex combination. Union the transformed
   // per-bone source boxes, reducing the common update to O(bones). Float total
   // roundoff is covered by the outward arithmetic margin below.
   for(int32 S=0;S<4;++S)if(V.Weights[S]>=1.e-6f)
    if(!Accumulate(FUintVector4(V.Bones[S],Source.FallbackSlot,Source.FallbackSlot,Source.FallbackSlot),FVector4d(1,0,0,0)))return false;
  }else{FVector4d Weights(0,0,0,0);
   for(int32 S=0;S<4;++S)Weights[S]=Fallback?(S==0?1.:0.):(FMath::Abs(V.Weights[S])>=1.e-6f?double(V.Weights[S])/Total:0.);
   if(!Accumulate(Bones,Weights))return false;
  }
 }
 for(const auto& D:Draw)Next.MaxOffset=FMath::Max(Next.MaxOffset,FVector(D.Offset.X,D.Offset.Y,D.Offset.Z).Length());
 Out=MoveTemp(Next);Error.Reset();return true;
}
bool OKEvaluatePrivateGpuBounds(const FKKPrivateGpuBounds& Bounds,const FKKPrivateGpuPose& Pose,FBox& Out,FString& Error){
 if(!Bounds.Revision||Bounds.Revision!=Pose.Revision||Bounds.PaletteCount!=uint32(Pose.Palette.Num())||Bounds.Groups.IsEmpty()){Error=TEXT("Private GPU bounds revision/layout mismatch");return false;}
 TArray<FVector4f> Rows;if(!OKPrepareGpuDeformPalette(Pose.Palette,Rows,Error)||!OKPrepareGpuDeformPalette(MakeArrayView(&Pose.PostCorrection,1),Rows,Error))return false;
 FBox Next(ForceInit);double MaxArithmetic=0.;
 for(const auto& G:Bounds.Groups){FVector Lo=FVector::ZeroVector,Hi=Lo;double Absolute=0.;
  for(int32 S=0;S<4;++S){if(G.Bones[S]>=Bounds.PaletteCount){Error=TEXT("Private GPU bounds bone invalid");return false;}
   if(G.Min[S]==FVector4d(0,0,0,0)&&G.Max[S]==FVector4d(0,0,0,0))continue;
   FVector L,H;TransformInterval(G.Min[S],G.Max[S],Pose.Palette[G.Bones[S]],L,H);Lo+=L;Hi+=H;
   for(int32 A=0;A<3;++A)for(int32 C=0;C<4;++C)Absolute+=FMath::Abs(double(Pose.Palette[G.Bones[S]].M[C][A]))*FMath::Max(FMath::Abs(G.Min[S][C]),FMath::Abs(G.Max[S][C]));}
  FVector L,H;TransformInterval(FVector4d(Lo,1),FVector4d(Hi,1),Pose.PostCorrection,L,H);
  double PostScale=0.;for(int32 A=0;A<3;++A){PostScale+=FMath::Abs(double(Pose.PostCorrection.M[3][A]));for(int32 C=0;C<3;++C)PostScale+=FMath::Abs(double(Pose.PostCorrection.M[C][A]))*(1.+Absolute);}
  MaxArithmetic=FMath::Max(MaxArithmetic,PostScale*100.);
  // Unity (-X,Z,Y) -> UE centimeters, including the sign reversal.
  Next+=FVector(-H.X,L.Z,L.Y)*100.;Next+=FVector(-L.X,H.Z,H.Y)*100.;
 }
 // Outward margin covers float multiply/accumulate/divide/post rounding, even
 // for cancellation in signed weights; secondary offsets undergo rotation.
 Next=Next.ExpandBy(Bounds.MaxOffset+.01+MaxArithmetic*1.e-5);
 if(!Next.IsValid||Next.Min.ContainsNaN()||Next.Max.ContainsNaN()){Error=TEXT("Private GPU bounds nonfinite");return false;}
 Out=Next;Error.Reset();return true;
}
