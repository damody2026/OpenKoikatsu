#pragma once
#include "CoreMinimal.h"

// APNG runtime characters face local +Y, unlike the camera's local +X.
inline FRotator OKStreetFacingMovement(const FVector& Move,const FRotator& Previous){
 if(FVector(Move.X,Move.Y,0).IsNearlyZero())return Previous;
 return FRotator(0,FMath::RadiansToDegrees(FMath::Atan2(Move.Y,Move.X))-90.f,0);
}

inline constexpr float OKStreetFemaleTurnDegreesPerSecond=145.f;
inline constexpr float OKStreetMouseYawSensitivity=.5f;
inline constexpr float OKStreetMousePitchSensitivity=.2f;
inline FRotator OKStreetLimitFemaleFacing(const FRotator& Previous,const FRotator& Target,float Dt){
 const float Step=OKStreetFemaleTurnDegreesPerSecond*(FMath::IsFinite(Dt)?FMath::Max(0.f,Dt):0.f);
 return FRotator(0,FMath::UnwindDegrees(Previous.Yaw+FMath::Clamp(FMath::FindDeltaAngleDegrees(Previous.Yaw,Target.Yaw),-Step,Step)),0);
}

// UI consumes its own pointer press; a world region cannot reuse that press.
inline bool OKStreetWorldTouchAllowed(bool Focused,bool MouseUI,bool HasTarget,bool TouchActive,bool OverPanel){return Focused&&MouseUI&&HasTarget&&!TouchActive&&!OverPanel;}

// An unfinished collected topic remains busy while waiting for an answer or
// presenting an unvoiced player sentence, even with no active audio node.
inline bool OKStreetConversationBusy(bool Line,bool Collected,bool Relationship,bool Touch){return Line||Collected||Relationship||Touch;}
