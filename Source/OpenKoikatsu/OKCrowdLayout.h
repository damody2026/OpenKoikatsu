#pragma once
#include "CoreMinimal.h"
namespace OKCrowdLayout {
inline FVector Center(int32 Index){return FVector((Index%10-4.5)*450,(Index/10-4.5)*450,0);}
inline double Angle(int32 Index,double Time){return Time*.55+Index*2.399963;}
inline FVector Position(int32 Index,double Time){const double A=Angle(Index,Time);return Center(Index)+FVector(170*FMath::Cos(A),170*FMath::Sin(A),0);}
inline FRotator Rotation(int32 Index,double Time){return FRotator(0,FMath::RadiansToDegrees(Angle(Index,Time)),0);}
// Input is relative to the horizontal camera axes; diagonal keys must not
// increase walking speed. The room's inner walls are at +/-2500 cm.
inline FVector PlayerStep(float Forward,float Right,float CameraYaw,float DeltaSeconds){
 const FRotator Facing(0,CameraYaw,0);
 const FVector Direction=(Facing.Vector()*Forward+FRotationMatrix(Facing).GetUnitAxis(EAxis::Y)*Right).GetClampedToMaxSize(1);
 return Direction*(180.f*FMath::Clamp(DeltaSeconds,0.f,.1f));
}
inline FVector ClampPlayerToRoom(FVector Position){Position.X=FMath::Clamp(Position.X,-2425.,2425.);Position.Y=FMath::Clamp(Position.Y,-2425.,2425.);return Position;}
}
