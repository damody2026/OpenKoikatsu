#pragma once
#include "CoreMinimal.h"

// One physical press belongs to one target and one input mode until release.
// Target revisions represent source/pose replacement, never animation frames.
enum class EOKBodyGestureEvent : uint8 { None, Hover, Press, Drag, Release, Cancel };
struct FOKBodyGestureResult {
 EOKBodyGestureEvent Event=EOKBodyGestureEvent::None;
 uint64 Target=0,Revision=0;
 FVector2D Delta=FVector2D::ZeroVector;
};
struct FOKBodyGesture {
 bool PreviousLeft=false,Owned=false;
 uint64 Target=0,Revision=0;
 FVector2D Last=FVector2D::ZeroVector;
 FOKBodyGestureResult Update(bool Enabled,bool Left,bool HasMouse,bool Focused,bool OverUI,
   bool CompetingGesture,uint64 Hit,uint64 HitRevision,bool AnchorValid,const FVector2D& Position){
  FOKBodyGestureResult Out;
  const bool Rising=Left&&!PreviousLeft;
  PreviousLeft=Left;
  const bool Valid=Enabled&&HasMouse&&Focused&&!OverUI&&!CompetingGesture&&!Position.ContainsNaN();
  if(Owned){
   Out.Target=Target;Out.Revision=Revision;
   if(!Valid||!AnchorValid){Out.Event=EOKBodyGestureEvent::Cancel;Owned=false;}
   else if(!Left){Out.Event=EOKBodyGestureEvent::Release;Owned=false;}
   else{Out.Delta=Position-Last;if(!Out.Delta.IsNearlyZero())Out.Event=EOKBodyGestureEvent::Drag;}
  }else if(Valid&&Hit&&HitRevision){
   Out.Target=Hit;Out.Revision=HitRevision;
   if(Rising){Target=Hit;Revision=HitRevision;Owned=true;Out.Event=EOKBodyGestureEvent::Press;}
   else if(!Left)Out.Event=EOKBodyGestureEvent::Hover;
  }
  Last=Position;
  return Out;
 }
 FOKBodyGestureResult Cancel(){
  FOKBodyGestureResult Out;
  if(Owned){Out.Event=EOKBodyGestureEvent::Cancel;Out.Target=Target;Out.Revision=Revision;}
  Owned=false;return Out;
 }
};
