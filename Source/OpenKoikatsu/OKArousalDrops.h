// What runs out of her (user, 2026-11-01), shared by the street (OKStreetSocialArousal.inl) and the one-on-one room
// (OKHScene.cpp): how often a drop comes for her desire and bust, and the drops themselves. A drop swells where it
// comes out (following her), falls, and lies where it lands as a bead; ten seconds after it came out it is gone. Where
// it landed stays wet (a mark, grown by the drops after it) until she has slept or bathed.
#pragma once

#include "CoreMinimal.h"

class AActor;
class UStaticMeshComponent;
class UMaterialInstanceDynamic;

namespace OKArousal{
 // Desire (relation sex_desire, up to 200) past which she flushes and runs clear; past which her nipples leak on the
 // street (with a bust past 40; in a scene with him past DesireFlush and any bust).
 constexpr float DesireFlush=100.f,DesireMilk=150.f,BustMilk=.4f;
 constexpr float DropLife=10.f;
 enum EColour:uint8{Clear,White,Red,Colours};
 // Her blush for a desire past 100: half at 100, full at 200.
 inline float Blush(float Desire){return Desire>DesireFlush?.5f+.5f*FMath::Clamp((Desire-DesireFlush)/100.f,0.f,1.f):0.f;}
 // How far past 100 her desire is (0 at 100, 1 at 200).
 inline float Past(float Desire){return FMath::Clamp((Desire-DesireFlush)/100.f,0.f,1.f);}
 // Seconds between drops at her crotch: one every 1.6 s just past 100, four a second at 200.
 inline float CrotchInterval(float Desire){return FMath::Lerp(1.6f,.25f,Past(Desire));}
 // Seconds between drops at one nipple: more the higher her desire and the bigger her bust (shapeBody 4, 0..1): every
 // 6 s at 100 with no bust to speak of, every 0.3 s at 200 with the biggest.
 inline float NippleInterval(float Desire,float Bust){return 4.f/(1.f+4.f*Past(Desire))/(.5f+1.5f*FMath::Clamp(Bust,0.f,1.f))*.75f;}
 inline FLinearColor Tint(uint8 C){return C==White?FLinearColor(.93f,.9f,.82f,.8f):C==Red?FLinearColor(.75f,.05f,.07f,.75f):FLinearColor(.85f,.92f,1.f,.45f);}
 // The mark it leaves: the same colour, fainter.
 inline FLinearColor MarkTint(uint8 C){FLinearColor T=Tint(C);T.A*=.45f;return T;}
}

struct FOKArousalDrops{
 // Where her bone is now (Who: her index, the caller's own numbering).
 using FWhere=TFunctionRef<bool(int32 Who,FName Bone,FVector& At)>;
 // A new drop of Colour at At (her Bone), landing on Ground (world Z). False when there are too many.
 bool Spawn(AActor* Owner,int32 Who,FName Bone,const FVector& At,float Ground,uint8 Colour);
 // A drop shot out at Velocity (cm/s; as she comes): no swelling, it flies and falls, the same ten seconds and mark.
 bool Squirt(AActor* Owner,int32 Who,const FVector& At,const FVector& Velocity,float Ground,uint8 Colour);
 void Tick(float Dt,FWhere Where);
 // She slept or bathed: her marks are gone.
 void ClearMarks(int32 Who);
 // Everything, drops and marks (the room closes).
 void Reset();
 // The floor under At: the first thing a line down meets, not Ignore; Fallback when nothing within 3 m.
 static float GroundBelow(const AActor* Owner,const FVector& At,TArrayView<const AActor* const> Ignore,float Fallback);
private:
 // Glint: the light it catches (user, 2026-11-01: it has to shine to look like water), a white highlight on the side
 // toward the camera, on each drop and each wet mark.
 struct FDrop{TWeakObjectPtr<UStaticMeshComponent> Mesh,Glint;FName Bone;int32 Who=0;uint8 Phase=0,Colour=0;float Age=0.f,Ground=0.f;FVector Velocity=FVector::ZeroVector;};
 struct FMark{TWeakObjectPtr<UStaticMeshComponent> Mesh,Glint;int32 Who=0;uint8 Colour=0;FVector At=FVector::ZeroVector;float Radius=0.f;};
 UMaterialInstanceDynamic* Material(AActor* Owner,uint8 Colour,bool bMark);
 UStaticMeshComponent* NewGlint(AActor* Owner);
 void PlaceGlints(const FVector& Eye);
 void Land(AActor* Owner,const FDrop& D,const FVector& At);
 TArray<FDrop> Drops;TArray<FMark> Marks;
 TWeakObjectPtr<UMaterialInstanceDynamic> Materials[OKArousal::Colours*2],GlintMaterial;
 TWeakObjectPtr<AActor> MarkOwner;
};
