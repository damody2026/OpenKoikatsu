#include "CoreMinimal.h"
namespace {
FTransform MixPose(const FTransform& A,const FTransform& B,double T){const auto Q=FQuat::FastLerp(A.GetRotation(),B.GetRotation(),T).GetNormalized();return FTransform(Q,FMath::Lerp(A.GetTranslation(),B.GetTranslation(),T),FMath::Lerp(A.GetScale3D(),B.GetScale3D(),T));}
double PointErrorBound(const FTransform& A,const FTransform& B,double Radius){const auto X=A.ToMatrixWithScale(),Y=B.ToMatrixWithScale();double Squared=0;for(int32 R=0;R<3;++R)for(int32 C=0;C<3;++C)Squared+=FMath::Square(X.M[R][C]-Y.M[R][C]);return FVector::Distance(A.GetTranslation(),B.GetTranslation())+FMath::Sqrt(Squared)*Radius;}
}
// Keep other hierarchical bones intact. Only root-child palette columns have
// a directly bounded effect on independently weighted skin vertices.
void CompactGpuTrackFrames(const TArray<TArray<FTransform>>& Frames,int32 Bone,double Radius,TArray<int32>& Keep){
 if(Frames.Num()<3||!FMath::IsFinite(Radius)||Radius<=0){for(int32 I=0;I<Frames.Num();++I)Keep.Add(I);return;}
 Keep.Add(0);Keep.Add(Frames.Num()-1);TArray<FIntPoint> Pending;Pending.Add({0,Frames.Num()-1});
 while(!Pending.IsEmpty()){const auto Range=Pending.Pop(EAllowShrinking::No);if(Range.Y-Range.X<=1)continue;double Maximum=0;int32 Worst=Range.X+1;
  for(double K=Range.X+.25;K<Range.Y;K+=.25){const int32 A=FMath::FloorToInt(K),B=FMath::Min(A+1,Frames.Num()-1);const auto Source=MixPose(Frames[A][Bone],Frames[B][Bone],K-A);const auto Approx=MixPose(Frames[Range.X][Bone],Frames[Range.Y][Bone],(K-Range.X)/(Range.Y-Range.X));const double E=PointErrorBound(Source,Approx,Radius);if(E>Maximum){Maximum=E;Worst=FMath::Clamp(FMath::RoundToInt(K),Range.X+1,Range.Y-1);}}
  // 0.0025 cm = 0.025 mm; below the existing 0.01 cm GPU vertex gate.
  if(Maximum>.0025){Keep.Add(Worst);Pending.Add({Range.X,Worst});Pending.Add({Worst,Range.Y});}
 }
 Keep.Sort();
}
