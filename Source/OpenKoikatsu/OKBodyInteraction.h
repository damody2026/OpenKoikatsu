#pragma once
#include "CoreMinimal.h"
class AKKPreviewActor;
class FJsonObject;

// Original authored primitive colliders for ordinary HitReaction. Surface
// interactions use separate skinned colliders and must not guess these tags.
struct FOKBodyTouchCollider {
 FString Bone,Tag;
 FVector Center=FVector::ZeroVector;
 float Radius=0,Height=0;
 int32 Axis=1;
 bool Sphere=false;
};
class FOKBodyInteraction {
public:
 bool Prepare(AKKPreviewActor* Actor,FString& Error);
 void SetContext(const FString& Pair,const FString& State);
 int32 Query(const FVector& Origin,const FVector& Direction,double& Distance) const;
 bool Press(int32 Collider);
 void Cancel();
 bool IsReady()const{return Owner.IsValid()&&Catalog.IsValid();}
 bool IsAvailable()const;
 uint64 Revision=1;
 TWeakObjectPtr<AKKPreviewActor> Owner;
private:
 const TSharedPtr<FJsonObject> Reaction(int32 Collider)const;
 TSharedPtr<FJsonObject> Catalog;
 TArray<FOKBodyTouchCollider> Colliders;
 TArray<TSharedPtr<FJsonObject>> ActiveReactions;
 FString PairKey,StateName;
};
