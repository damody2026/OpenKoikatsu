#pragma once
#include "CoreMinimal.h"
#include "RenderGraphFwd.h"

// Three float4s per key: translation/time, quaternion (xyzw), scale/padding.
// Track uint4: first key, key count, local parent (UINT_MAX for root), rotation mode (0 slerp, 1 normalized lerp).
// Actor uint4: first track A, first track B, bone count, hierarchy mode (0 matrix, 1 native TRS including negative-scale reconstruction).
// Times float4: time A, time B, blend [0,1], reserved. Times are clip-local.
// Task uint4: local bone, actor index, first inverse-bind row, reserved.
// Flag 0x10: bitwise constant quaternion/scale stored once, followed by
// translation/time rows. X is then a row offset. No quantization or pose sharing.
inline bool OKGpuTrackPacked(const FUintVector4& T){return (T.W&16)!=0;}
inline bool OKGpuTrackVectorKeys(const FUintVector4& T){return (T.W&256)!=0;}
inline bool OKGpuTrackRowAddressed(const FUintVector4& T){return OKGpuTrackPacked(T)||OKGpuTrackVectorKeys(T);}
inline uint64 OKGpuTrackFirstRow(const FUintVector4& T){return OKGpuTrackRowAddressed(T)?uint64(T.X):uint64(T.X)*3;}
inline uint64 OKGpuTrackEndRow(const FUintVector4& T){return OKGpuTrackVectorKeys(T)?uint64(T.X)+(OKGpuTrackPacked(T)?1+T.Y:uint64(T.Y)*2):OKGpuTrackPacked(T)?uint64(T.X)+2+T.Y:(uint64(T.X)+T.Y)*3;}
inline uint32 OKGpuTrackPositionRow(const FUintVector4& T,uint32 K){return OKGpuTrackVectorKeys(T)?T.X+(OKGpuTrackPacked(T)?1+K:K*2):OKGpuTrackPacked(T)?T.X+2+K:(T.X+K)*3;}
inline uint32 OKGpuTrackRotationRow(const FUintVector4& T,uint32 K){return OKGpuTrackVectorKeys(T)?T.X+(OKGpuTrackPacked(T)?0:K*2+1):OKGpuTrackPacked(T)?T.X:(T.X+K)*3+1;}
inline uint32 OKGpuTrackScaleRow(const FUintVector4& T,uint32 K){check(!OKGpuTrackVectorKeys(T));return OKGpuTrackPacked(T)?T.X+1:(T.X+K)*3+2;}
inline void OKCopyGpuTrackKeys(TArray<FVector4f>& Out,TConstArrayView<FVector4f> Keys,FUintVector4& T){
 const uint64 First=OKGpuTrackFirstRow(T),Count=OKGpuTrackEndRow(T)-First;T.X=OKGpuTrackRowAddressed(T)?Out.Num():Out.Num()/3;Out.Append(Keys.GetData()+First,Count);while(Out.Num()%3)Out.Add(FVector4f(0,0,0,0));
}
// Affine-column helper tracks (0x20, axis in bits 6..7) represent only
// one matrix column. Quaternion twist is unobservable and must never be blended.
inline bool OKGpuTrackColumn(const FUintVector4& T){return (T.W&32)!=0;}
inline bool OKGpuTrackModeValid(const FUintVector4& T){return !(T.W&~497u)&&(!OKGpuTrackVectorKeys(T)||OKGpuTrackColumn(T))&&(!OKGpuTrackColumn(T)?!(T.W&192u):((T.W>>6)&3)<3);}
inline FVector3f OKGpuColumnLerp(FVector3f A,FVector3f B,float W){
 FVector3f Result;for(int32 I=0;I<3;++I){volatile float Delta=B[I]-A[I];volatile float Scaled=W*Delta;Result[I]=A[I]+Scaled;}return Result;
}
inline FVector3f OKGpuTrackColumnKey(TConstArrayView<FVector4f> Keys,const FUintVector4& Track,uint32 K){
 const auto& Q=Keys[OKGpuTrackRotationRow(Track,K)];if(OKGpuTrackVectorKeys(Track))return FVector3f(Q);
 FVector3f Basis(0,0,0);const uint32 Axis=(Track.W>>6)&3;Basis[Axis]=Keys[OKGpuTrackScaleRow(Track,K)][Axis];
 return FQuat4f(Q.X,Q.Y,Q.Z,Q.W).RotateVector(Basis);
}
template<class V,class Q,class Transform> inline Transform OKGpuColumnTransform(V Position,V Column,uint32 Axis){
 V Basis(0,0,0),Scale(0,0,0);Basis[Axis]=1;const auto Length=Column.Size();Scale[Axis]=Length;
 return Transform(Length>1e-12?Q::FindBetweenNormals(Basis,Column/Length):Q::Identity,Position,Scale);
}
template<class Transform> inline Transform OKGpuBlendLocal(const Transform& A,const Transform& B,float Weight,const FUintVector4& Track){
 const auto Position=FMath::Lerp(A.GetTranslation(),B.GetTranslation(),Weight);
 if(OKGpuTrackColumn(Track)){const uint32 Axis=(Track.W>>6)&3;auto Basis=A.GetTranslation()*0;Basis[Axis]=1;
 const auto Column=std::decay_t<decltype(Position)>(OKGpuColumnLerp(FVector3f(A.GetRotation().RotateVector(Basis)*A.GetScale3D()[Axis]),FVector3f(B.GetRotation().RotateVector(Basis)*B.GetScale3D()[Axis]),Weight));
 using V=std::decay_t<decltype(Position)>;using Q=std::decay_t<decltype(A.GetRotation())>;return OKGpuColumnTransform<V,Q,Transform>(V(OKGpuColumnLerp(FVector3f(A.GetTranslation()),FVector3f(B.GetTranslation()),Weight)),Column,Axis);}
 return Transform(std::decay_t<decltype(A.GetRotation())>::Slerp(A.GetRotation(),B.GetRotation(),Weight).GetNormalized(),Position,FMath::Lerp(A.GetScale3D(),B.GetScale3D(),Weight));
}
struct KKCROWDGPUPACKING_API FOKGpuPoseBatch {
 TArray<FVector4f> Keys, Times, InverseBindRows;
 TArray<FUintVector4> Tracks, Actors, Tasks;
 bool Validate(FString& Error) const;
};
KKCROWDGPUPACKING_API bool OKPackConstantGpuPoseChannels(FOKGpuPoseBatch&,FString& Error);
// Runtime callers can register persistent external buffers and update only Times/Actors.
// Batch upload overload below is for import/oracle setup, not every gameplay frame.
struct FOKGpuPoseBuffers {
 FRDGBufferRef Keys=nullptr, Tracks=nullptr, Actors=nullptr, Times=nullptr, Tasks=nullptr, InverseBindRows=nullptr;
 uint32 TaskCount=0;
};
// Render thread capability check; unsupported platforms retain the CPU path.
KKCROWDGPUPACKING_API bool OKIndependentGpuPoseAvailable();
KKCROWDGPUPACKING_API FRDGBufferRef OKAddIndependentGpuPosePass(FRDGBuilder& Graph,const FOKGpuPoseBuffers& Buffers);
KKCROWDGPUPACKING_API FRDGBufferRef OKAddIndependentGpuPosePass(FRDGBuilder& Graph,const FOKGpuPoseBatch& Batch);
KKCROWDGPUPACKING_API bool OKRunIndependentGpuPoseOracle(const FString& Output,bool Resident=false);

// Diagnostic only: synchronous readback for numerical oracles, never gameplay.
KKCROWDGPUPACKING_API bool OKReadbackIndependentGpuPose(const FOKGpuPoseBatch& Batch,TArray<FVector4f>& Actual,FString& Error);
