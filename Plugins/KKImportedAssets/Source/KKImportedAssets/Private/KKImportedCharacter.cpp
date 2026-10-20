#include "KKImportedCharacter.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Curves/CurveFloat.h"
#include "KKImportedPoseComponent.h"
#include "KKSharedBakedSkinComponent.h"
#include "UObject/StrongObjectPtr.h"
AKKImportedCharacterActor::AKKImportedCharacterActor(){PrimaryActorTick.bCanEverTick=true;RootComponent=CreateDefaultSubobject<USceneComponent>(TEXT("Root"));}
void AKKImportedCharacterActor::OnConstruction(const FTransform& Transform){
 Super::OnConstruction(Transform);
 RebuildCharacter();
}
void AKKImportedCharacterActor::RebuildCharacter(){
 SharedDriver.Reset();AnimationError.Reset();
 TArray<UMeshComponent*> Old;GetComponents(Old);for(auto* Comp:Old)if(Comp->ComponentHasTag(TEXT("ImportedCard")))Comp->DestroyComponent();
 if(!Character && AnimationLibrary)Character=AnimationLibrary->Character;
 if(!Character)return;
 const auto* Coordinate=Character->Outfits.FindByPredicate([this](const FKKImportedOutfit& O){return O.Index==Outfit;});if(!Coordinate)return;
 CreateCharacterComponents(*Coordinate);
 if(SharedAnimation){SharedDriver=OKCreateSharedAnimationDriver();
  if(!SharedDriver)AnimationError=TEXT("Shared animation evaluator unavailable: enable KKUnityBundle");
  else if(!SharedDriver->Initialize(*this,AnimationError)||!SharedDriver->Evaluate(*this,SharedAnimationTime,AnimationError))SharedDriver.Reset();
 }else if(PlaySkeletalAnimation)PlayImportedAnimation(AnimationIndex);
 ApplyMouthControl();
}

void AKKImportedCharacterActor::CreateCharacterComponents(const FKKImportedOutfit& Coordinate){
 for(const auto& Part:Coordinate.Parts)if((PlaySkeletalAnimation||SharedAnimation) && Part.SkinnedModel){
  USkeletalMeshComponent* Comp=SharedAnimation?NewObject<UKKImportedPoseComponent>(this):(ShareBakedPose?NewObject<UKKSharedBakedSkinComponent>(this):NewObject<USkeletalMeshComponent>(this));Comp->ComponentTags.Add(TEXT("ImportedCard"));Comp->SetupAttachment(RootComponent);
  Comp->SetSkeletalMeshAsset(Part.SkinnedModel);Comp->SetRelativeTransform(Part.Transform);Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);Comp->SetVisibility(Part.MouthDefaultVisible);
  Comp->SetAnimationMode(EAnimationMode::AnimationSingleNode);Comp->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
  Comp->CreationMethod=EComponentCreationMethod::UserConstructionScript;Comp->RegisterComponent();
 }else if(Part.Model){auto* Comp=NewObject<UStaticMeshComponent>(this);Comp->ComponentTags.Add(TEXT("ImportedCard"));
 Comp->SetupAttachment(RootComponent);Comp->SetStaticMesh(Part.Model);Comp->SetRelativeTransform(Part.Transform);Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);Comp->SetVisibility(Part.MouthDefaultVisible);
 Comp->CreationMethod=EComponentCreationMethod::UserConstructionScript;Comp->RegisterComponent();}
}

bool AKKImportedCharacterActor::PlayImportedAnimation(int32 Index){
 if(SharedAnimation)return false;
 if(!AnimationLibrary || AnimationLibrary->Character!=Character || !AnimationLibrary->Animations.IsValidIndex(Index))return false;
 const auto& Entry=AnimationLibrary->Animations[Index];if(Entry.Outfit!=Outfit || !Entry.Sequence)return false;
 TArray<USkeletalMeshComponent*> Skins;GetComponents(Skins);int32 Count=0;
 for(auto* Comp:Skins)if(Comp->ComponentHasTag(TEXT("ImportedCard"))){
  if(!Comp->GetSkeletalMeshAsset() || Comp->GetSkeletalMeshAsset()->GetSkeleton()!=Entry.Sequence->GetSkeleton())return false;
  ++Count;
 }
 if(!Count)return false;
 for(auto* Comp:Skins)if(Comp->ComponentHasTag(TEXT("ImportedCard")))Comp->PlayAnimation(Entry.Sequence,Entry.Loop);
 AnimationIndex=Index;ApplyMouthControl();return !ShareBakedPose||ConfigureBakedPoseSharing();
}
bool AKKImportedCharacterActor::ConfigureBakedPoseSharing(){
 if(SharedAnimation||!PlaySkeletalAnimation)return false;
 TArray<USkeletalMeshComponent*> Skins;GetComponents(Skins);
 Skins.RemoveAll([](const auto* C){return !C->ComponentHasTag(TEXT("ImportedCard"));});
 if(Skins.IsEmpty())return false;
 auto* Leader=Skins[0];const auto* Mesh=Leader->GetSkeletalMeshAsset();if(!Mesh)return false;
 const auto& Reference=Mesh->GetRefSkeleton();
 // Validate before mutating: a body-only skeleton cannot drive private rigs.
 for(auto* Skin:Skins){
  if(!Cast<UKKSharedBakedSkinComponent>(Skin)||!Skin->GetSkeletalMeshAsset())return false;
  const auto& Other=Skin->GetSkeletalMeshAsset()->GetRefSkeleton();
  for(int32 Bone=0;Bone<Other.GetNum();++Bone){const int32 Mapped=Reference.FindBoneIndex(Other.GetBoneName(Bone));
   if(Mapped==INDEX_NONE||!Other.GetRefBonePose()[Bone].Equals(Reference.GetRefBonePose()[Mapped],1e-6))return false;
   const int32 Parent=Other.GetParentIndex(Bone),MasterParent=Reference.GetParentIndex(Mapped);
   if((Parent==INDEX_NONE)!=(MasterParent==INDEX_NONE)||
      (Parent!=INDEX_NONE&&Other.GetBoneName(Parent)!=Reference.GetBoneName(MasterParent)))return false;
  }
 }
 for(auto* Skin:Skins)if(Skin!=Leader)Skin->SetLeaderPoseComponent(Leader,true,false);
 Leader->bRequiredBonesUpToDate=false;
 Leader->RecalcRequiredBones(Leader->GetPredictedLODLevel());
 return true;
}
bool AKKImportedCharacterActor::SetAnimationPosition(float Seconds){
 if(!FMath::IsFinite(Seconds)||Seconds<0.f)return false;
 if(SharedAnimation){if(!SharedDriver||!SharedDriver->Evaluate(*this,Seconds,AnimationError))return false;
  SharedAnimationTime=Seconds;ApplyMouthControl();return true;}
 TArray<USkeletalMeshComponent*> Skins;GetComponents(Skins);bool Any=false;
 for(auto* Comp:Skins)if(Comp->ComponentHasTag(TEXT("ImportedCard")) && Comp->GetSingleNodeInstance()){
  Comp->SetPosition(Seconds,false);Comp->TickAnimation(0.f,false);Comp->RefreshBoneTransforms();Comp->UpdateComponentToWorld();Any=true;
 }
 ApplyMouthControl();return Any;
}

int32 AKKImportedCharacterActor::GetMouthPatternCount() const {
 if(!Character)return 0;
 const auto* Coordinate=Character->Outfits.FindByPredicate([this](const FKKImportedOutfit& O){return O.Index==Outfit;});
 int32 Count=0;if(Coordinate)for(const auto& Part:Coordinate->Parts)Count=FMath::Max(Count,Part.MouthPatterns.Num());return Count;
}
bool AKKImportedCharacterActor::SetMouthControl(int32 Pattern,float Open){
 if(!FMath::IsFinite(Open)||Pattern<0||Pattern>=GetMouthPatternCount())return false;
 TArray<USkeletalMeshComponent*> Active;GetComponents(Active);if(!Active.ContainsByPredicate([](const auto* C){return C->ComponentHasTag(TEXT("ImportedCard"));}))return false;
 MouthPattern=Pattern;MouthOpen=FMath::Clamp(Open,0.f,1.f);MouthOpenCurve=nullptr;MouthControlEnabled=true;ApplyMouthControl();return true;
}
void AKKImportedCharacterActor::ResetMouthControl(){MouthControlEnabled=false;MouthOpenCurve=nullptr;ApplyMouthControl();}
bool AKKImportedCharacterActor::PlaySharedAnimation(UKKSharedAnimation* Animation){
 if(!Animation||!Character||!FMath::IsFinite(Animation->Duration)||Animation->Duration<=0.f)return false;
 const auto* O=Character->Outfits.FindByPredicate([this](const auto& V){return V.Index==Outfit;});
 if(!O||O->AnimationSnapshot.IsEmpty())return false;
 auto Candidate=OKCreateSharedAnimationDriver();
 if(!Candidate){AnimationError=TEXT("Shared animation evaluator unavailable: enable KKUnityBundle");return false;}
 TStrongObjectPtr<UKKSharedAnimation> Previous(SharedAnimation.Get());SharedAnimation=Animation;
 if(!Candidate->Initialize(*this,AnimationError)){SharedAnimation=Previous.Get();return false;}
 // Keep the previous renderers and solver alive until the candidate's first
 // complete pose succeeds. The ImportedCard tag scopes driver pose targets.
 TArray<UMeshComponent*> PreviousComponents;GetComponents(PreviousComponents);
 PreviousComponents.RemoveAll([](const auto* C){return !C->ComponentHasTag(TEXT("ImportedCard"));});
 for(auto* C:PreviousComponents)C->ComponentTags.Remove(TEXT("ImportedCard"));
 CreateCharacterComponents(*O);
 if(!Candidate->Evaluate(*this,0.f,AnimationError)){
  TArray<UMeshComponent*> Staged;GetComponents(Staged);
  for(auto* C:Staged)if(C->ComponentHasTag(TEXT("ImportedCard")))C->DestroyComponent();
  for(auto* C:PreviousComponents)C->ComponentTags.Add(TEXT("ImportedCard"));
  SharedAnimation=Previous.Get();
  if(AnimationError.IsEmpty())AnimationError=TEXT("Shared animation candidate failed to produce its initial pose");
  return false;
 }
 SharedDriver=MoveTemp(Candidate);SharedAnimationTime=0.f;SharedAnimationPlaying=true;AnimationError.Reset();
 for(auto* C:PreviousComponents)C->DestroyComponent();
 ApplyMouthControl();return true;
}
void AKKImportedCharacterActor::Tick(float DeltaSeconds){
 Super::Tick(DeltaSeconds);
 if(SharedDriver&&SharedAnimationPlaying&&FMath::IsFinite(DeltaSeconds)&&DeltaSeconds>=0.f){
  const float Next=SharedAnimationTime+DeltaSeconds;
  if(SharedDriver->Evaluate(*this,Next,AnimationError))SharedAnimationTime=Next;else SharedAnimationPlaying=false;
 }
 if(MouthControlEnabled)ApplyMouthControl();
}
void AKKImportedCharacterActor::ApplyMouthControl(){
 if(!Character)return;
 const auto* Coordinate=Character->Outfits.FindByPredicate([this](const FKKImportedOutfit& O){return O.Index==Outfit;});if(!Coordinate)return;
 TArray<USkeletalMeshComponent*> Skins;GetComponents(Skins);
 for(auto* Comp:Skins)if(Comp->ComponentHasTag(TEXT("ImportedCard"))){
  const auto* Part=Coordinate->Parts.FindByPredicate([&](const FKKImportedPart& P){return P.SkinnedModel==Comp->GetSkeletalMeshAsset();});if(!Part||Part->MouthPatterns.IsEmpty())continue;
  float Rate=MouthOpen;if(MouthOpenCurve){if(SharedAnimation)Rate=MouthOpenCurve->GetFloatValue(SharedAnimation->GetPhase(SharedAnimationTime));else if(Comp->GetSingleNodeInstance())Rate=MouthOpenCurve->GetFloatValue(Comp->GetSingleNodeInstance()->GetCurrentTime());}
  if(!FMath::IsFinite(Rate))continue;
  const bool Enabled=MouthControlEnabled&&Part->MouthPatterns.IsValidIndex(MouthPattern);
  TMap<FName,float> Desired;for(const auto& Pair:Part->MouthPatterns){if(!Pair.Close.IsNone())Desired.Add(Pair.Close,0.f);if(!Pair.Open.IsNone())Desired.Add(Pair.Open,0.f);}
  if(Enabled){const int32 Open=FMath::TruncToInt(FMath::Clamp(Rate*100.f,0.f,100.f));const auto& Pair=Part->MouthPatterns[MouthPattern];if(!Pair.Close.IsNone())Desired.FindOrAdd(Pair.Close)+=100-Open;if(!Pair.Open.IsNone())Desired.FindOrAdd(Pair.Open)+=Open;}
  for(const auto& Weight:Desired)Comp->SetMorphTarget(Weight.Key,Enabled?(Weight.Value-Part->MouthBaseline.FindRef(Weight.Key))/100.f:0.f);
  if(Part->MouthHideWhenClosed)Comp->SetVisibility(Enabled?Rate>0.001f:Part->MouthDefaultVisible,true);
 }
}
