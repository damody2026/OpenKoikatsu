#pragma once
#include "CoreMinimal.h"
// Independent deterministic choices: no shared pose, card names or global RNG.
struct FOKCrowdBehavior {
 enum class EState : uint8 { Walk, Rest, Look };
 FRandomStream Random;
 EState State=EState::Walk;
 double Remaining=0, RouteTime=0, StateTime=0;
 float Speed=1, WalkSpeed=1, LookAmplitude=0;
 int32 Transitions=0, Stops=0, Starts=0;
 bool IdleClipPlaying=false;
 int32 PendingClip=INDEX_NONE;
 void Initialize(int32 Index) {
  Random.Initialize(0x193A27+Index*7919); WalkSpeed=Random.FRandRange(.8f,1.15f);
  Remaining=Random.FRandRange(3.f,11.f); Speed=WalkSpeed;
 }
 bool Step(float Delta) {
  const double Dt=FMath::Max(0.f,Delta); StateTime+=Dt; Remaining-=Dt;
  bool Changed=false;
  if(Remaining<=0) {
   if(State==EState::Walk){State=Random.RandRange(0,1)?EState::Rest:EState::Look;Remaining=Random.FRandRange(2.5f,6.f);++Stops;LookAmplitude=Random.FRandRange(15.f,40.f);}
   else {State=EState::Walk;Remaining=Random.FRandRange(5.f,13.f);WalkSpeed=Random.FRandRange(.8f,1.15f);++Starts;}
   StateTime=0;++Transitions;Changed=true;
  }
  const float Target=State==EState::Walk?WalkSpeed:0.f;
  // Brake and resume over half a second. Integrate only actual walking time.
  const float Previous=Speed;
  Speed=FMath::FInterpConstantTo(Speed,Target,float(Dt),2.f);
  RouteTime+=Dt*(Previous+Speed)*.5;
  return Changed;
 }
 float LookYaw() const {const double Fade=FMath::Clamp(FMath::Min(StateTime-.6,Remaining),0.,1.);return State==EState::Look&&Speed==0?LookAmplitude*FMath::Sin(StateTime*1.3)*FMath::Square(FMath::Sin(Fade*PI*.5)):0;}
};

// Runtime functional oracle; independent of renderer/frame-rate evidence.
inline bool OKValidateCrowdBehaviors(){
 for(int32 I=0;I<100;++I){FOKCrowdBehavior B;B.Initialize(I);
  for(int32 Frame=0;Frame<120*60;++Frame){const double Before=B.RouteTime;const float Speed=B.Speed;B.Step(1.f/120);
   if(!FMath::IsFinite(B.RouteTime)||B.RouteTime<Before||B.RouteTime-Before>.01)return false;
   if(Speed==0&&B.Speed==0&&B.RouteTime!=Before)return false;
   if(!FMath::IsFinite(B.LookYaw()))return false;
  }
  if(B.Stops<2||B.Starts<2)return false;
 }
 return true;
}
