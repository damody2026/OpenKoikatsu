#pragma once
#include "CoreMinimal.h"
#include "ok_extract.h"
static inline FName OKPaletteBoneName(int32 Part,int32 Slot,int32 Axis){return FName(*FString::Printf(TEXT("__ok_palette_%d_%d_%d"),Part,Slot,Axis));}
static inline FTransform OKUnityLocalToUnreal(const float P[3],const float Q[4],const float S[3]){const FQuat Basis(0.f,0.70710678118f,0.70710678118f,0.f);FQuat Rotation=Basis*FQuat(Q[0],Q[1],Q[2],Q[3])*Basis;Rotation.Normalize();return FTransform(Rotation,FVector(-P[0],P[2],P[1])*100.,FVector(S[0],S[2],S[1]));}
static inline FMatrix OKSkinPaletteToUnreal(const float* M){
 // Match KKSkeletalBuilder: Unity (X,Y,Z) -> UE (-X,Z,Y).
 const int32 Map[3]={0,2,1};const double Sign[3]={-1.,1.,1.};FMatrix Result=FMatrix::Identity;
 for(int32 A=0;A<3;++A){for(int32 B=0;B<3;++B)Result.M[A][B]=Sign[A]*Sign[B]*M[Map[B]*4+Map[A]];
  Result.M[3][A]=100.*Sign[A]*M[Map[A]*4+3];}
 return Result;
}
static inline bool OKExactPaletteMatrix(const OkSkinPaletteView& Raw,const TArray<float>& Matrices,int32 Slot,FMatrix& Out){
 if(Matrices.Num()<16||Matrices.Num()%16)return false;
 if(Slot>=0){if(Slot>=Matrices.Num()/16)return false;Out=OKSkinPaletteToUnreal(Matrices.GetData()+Slot*16);return !Out.ContainsNaN();}
 const int64 Vertex=-int64(Slot)-1;if(Vertex>=Raw.n_verts||!Raw.weights||!Raw.bone_idx)return false;
 double Total=0;for(int32 K=0;K<4;++K){const float W=Raw.weights[Vertex*4+K];if(!FMath::IsFinite(W))return false;Total+=W;}
 if(Total<=1e-5){if(Matrices.IsEmpty())return false;Out=OKSkinPaletteToUnreal(Matrices.GetData()+Matrices.Num()-16);return !Out.ContainsNaN();}
 FMatrix Blend(ForceInit);double Sum=0;
 for(int32 K=0;K<4;++K){const float W=Raw.weights[Vertex*4+K];if(!FMath::IsFinite(W))return false;if(FMath::Abs(W)<1e-6f)continue;
  const int32 Bone=Raw.bone_idx[Vertex*4+K];if(Bone>=Matrices.Num()/16)return false;
  const FMatrix M=OKSkinPaletteToUnreal(Matrices.GetData()+Bone*16);Sum+=W;
  for(int32 R=0;R<4;++R)for(int32 C=0;C<4;++C)Blend.M[R][C]+=M.M[R][C]*W;
 }
 if(Sum<=1e-5)return false;for(int32 R=0;R<4;++R)for(int32 C=0;C<4;++C)Blend.M[R][C]/=Sum;
 if(Blend.ContainsNaN())return false;Out=Blend;return true;
}
static inline FTransform OKSkinPaletteColumn(const FMatrix& Matrix,int32 Axis){
 check(Axis>=0&&Axis<3);const FVector Basis=Axis==0?FVector::XAxisVector:Axis==1?FVector::YAxisVector:FVector::ZAxisVector;
 const FVector Column(Matrix.M[Axis][0],Matrix.M[Axis][1],Matrix.M[Axis][2]);const double Length=Column.Size();
 FVector Scale=FVector::ZeroVector;Scale[Axis]=3.*Length;
 const FQuat Rotation=Length>1e-12?FQuat::FindBetweenNormals(Basis,Column/Length):FQuat::Identity;
 return FTransform(Rotation,Matrix.GetOrigin(),Scale);
}
static inline FTransform OKSkinPaletteColumnContinuous(const FMatrix& Matrix,int32 Axis,const FQuat& Previous){
 FTransform Result=OKSkinPaletteColumn(Matrix,Axis);const FVector Basis=Axis==0?FVector::XAxisVector:Axis==1?FVector::YAxisVector:FVector::ZAxisVector;
 const FVector Column(Matrix.M[Axis][0],Matrix.M[Axis][1],Matrix.M[Axis][2]);
 const FQuat Rotation=Column.Size()>1e-12?(FQuat::FindBetweenNormals(Previous.RotateVector(Basis),Column.GetSafeNormal())*Previous).GetNormalized():Previous;
 Result.SetRotation(Rotation);return Result;
}