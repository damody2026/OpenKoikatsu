#pragma once
#include "CoreMinimal.h"
// Own a gesture only when its first press began over the 3D viewport.
// UI drags, focus loss, and loading a new view while held cannot acquire it.
struct FOKCameraDrag {
 uint8 PreviousButtons=0;bool Owned=false;FVector2D Last=FVector2D::ZeroVector;
 FVector2D Update(uint8 Buttons,bool HasMouse,bool Focused,bool OverUI,const FVector2D& Position){
  FVector2D Delta=FVector2D::ZeroVector;
  if(!Buttons||!HasMouse||!Focused)Owned=false;
  else if(!PreviousButtons){Owned=!OverUI;}
  else if(Owned)Delta=Position-Last;
  PreviousButtons=Buttons;Last=Position;return Delta;
 }
 void Cancel(){Owned=false;}
};
