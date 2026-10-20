#include "KKSharedAnimationDriver.h"
#include "KKSharedAnimation.h"
#include "KKImportedCharacter.h"
#include "KKImportedPoseComponent.h"
#include "KKRustExtract.h"
#include "KKExactSkinning.h"
#include "KKSkeletalBuilder.h"
#include "Animation/Skeleton.h"
#include "ok_extract.h"

namespace {
class FNativeSharedDriver final : public IKKSharedAnimationDriver {
 FOkSession Session;
 TArray<int32> Mapping;
 TArray<FTransform> Local;
 float LastTime=-1.f;
 bool Reset(const FKKImportedOutfit& O,const UKKSharedAnimation& A,FString& Error){
  if(!Session.OpenAnimationSnapshot(O.AnimationSnapshot)||!Session.InstallSharedClip(A.AuthoredCurves)){
   Error=TEXT("Invalid independent rig snapshot or shared curve resource");return false;}
  if(!FMath::IsFinite(A.Duration)||!FMath::IsNearlyEqual(A.Duration,Session.AnimDuration(),1e-6f)){
   Error=TEXT("Shared animation duration differs from authored curves; reimport the resource");return false;}
  LastTime=-1.f;return true;
 }
public:
 bool Initialize(AKKImportedCharacterActor& Actor,FString& Error)override{
  Error.Reset();const auto* O=Actor.Character?Actor.Character->Outfits.FindByPredicate([&](const auto& V){return V.Index==Actor.Outfit;}):nullptr;
  if(!O||!O->Skeleton||!Actor.SharedAnimation||!Reset(*O,*Actor.SharedAnimation,Error))return false;
  const auto& Ref=O->Skeleton->GetReferenceSkeleton();
  if(Ref.FindBoneIndex(TEXT("__ok_scene"))!=0){Error=TEXT("Shared playback requires exact imported character rig");return false;}
  if(!Session.ApplyAnimFrame(0.f,0.f))return false;
  for(const auto& P:O->Parts)if(P.SkinSourceHashV2.IsEmpty()||P.SkinSourceHashV2!=Session.SkinSourceHashV2(P.NativePartIndex)){
   Error=TEXT("Animation snapshot source contract differs: ")+P.Name;return false;}
  TMap<FName,int32> Native;
  for(int32 B=0;B<Session.SkelBoneCount();++B){OkBoneView V{};if(!Session.SkelBoneAt(B,V)||!V.name)return false;Native.Add(FName(UTF8_TO_TCHAR(V.name)),B);}
  Mapping.Init(INDEX_NONE,Ref.GetNum());Local.Init(FTransform::Identity,Ref.GetNum());
  for(int32 B=1;B<Ref.GetNum();++B)if(!Ref.GetBoneName(B).ToString().StartsWith(TEXT("__ok_palette_"))){
   const auto* N=Native.Find(Ref.GetBoneName(B));if(!N){Error=TEXT("Native bone missing from snapshot");return false;}Mapping[B]=*N;
  }
  return true;
 }
 bool Evaluate(AKKImportedCharacterActor& Actor,float Seconds,FString& Error)override{
  if(!FMath::IsFinite(Seconds)||Seconds<0.f||Seconds>86400.f||!Actor.Character||!Actor.SharedAnimation)return false;
  const auto* O=Actor.Character->Outfits.FindByPredicate([&](const auto& V){return V.Index==Actor.Outfit;});if(!O||!O->Skeleton)return false;
  // Deterministic dynamics: timeline seeks rebuild particle history, ordinary
  // playback advances it. The shared resource never owns mutable pose state.
  if(LastTime<0.f||Seconds<LastTime){if(!Reset(*O,*Actor.SharedAnimation,Error)||!Session.ApplyAnimFrame(0.f,0.f))return false;LastTime=0.f;}
  constexpr float Step=1.f/60.f;
  const auto* Animation=Actor.SharedAnimation.Get();const float Duration=Session.AnimDuration();
  if(!FMath::IsFinite(Duration)||Duration<=0.f||!FMath::IsNearlyEqual(Animation->Duration,Duration,1e-6f)){
   Error=TEXT("Shared animation duration differs from authored curves");return false;}
  auto Phase=[&](float T){return Animation->GetPhase(T);};
  while(Seconds-LastTime>Step+1e-6f){if(!Session.ApplyAnimFrame(Phase(LastTime+Step),Step))return false;LastTime+=Step;}
  if(Seconds>LastTime){if(!Session.ApplyAnimFrame(Phase(Seconds),Seconds-LastTime))return false;LastTime=Seconds;}
  const auto& Ref=O->Skeleton->GetReferenceSkeleton();
  for(int32 B=0;B<Mapping.Num();++B)if(Mapping[B]!=INDEX_NONE){OkBoneView V{};if(!Session.SkelBoneAt(Mapping[B],V))return false;Local[B]=OKUnityLocalToUnreal(V.pose_pos,V.pose_rot,V.pose_scl);}
  for(const auto& P:O->Parts){OkSkinPaletteView Raw{};TArray<float> Matrices;FMatrix Rigid=FMatrix::Identity;
   if(P.RigidFollow.IsEmpty()){if(Session.SkinPalette(P.NativePartIndex,Raw,Matrices)<=0){Error=TEXT("Native palette missing");return false;}}
   else {float World[16];if(P.RigidReference.Num()!=16||!Session.BoneWorldMatrix(P.RigidFollow,World))return false;Rigid=OKSkinPaletteToUnreal(P.RigidReference.GetData()).Inverse()*OKSkinPaletteToUnreal(World);}
   for(int32 Slot:P.PaletteSlots){FMatrix M=Rigid;if(P.RigidFollow.IsEmpty()&&!OKExactPaletteMatrix(Raw,Matrices,Slot,M)){Error=TEXT("Native palette slot outside snapshot: ")+P.Name;return false;}
    for(int32 Axis=0;Axis<3;++Axis){const int32 B=Ref.FindBoneIndex(OKPaletteBoneName(P.NativePartIndex,Slot,Axis));if(B==INDEX_NONE){Error=TEXT("Imported palette alias missing: ")+P.Name;return false;}Local[B]=OKSkinPaletteColumnContinuous(M,Axis,Local[B].GetRotation());}
   }
  }
  TArray<FTransform> World=Local;
  for(int32 B=0;B<World.Num();++B){const int32 Parent=Ref.GetParentIndex(B);if(Parent!=INDEX_NONE)World[B]=World[B]*World[Parent];if(World[B].ContainsNaN())return false;}
  TArray<UKKImportedPoseComponent*> Skins;Actor.GetComponents(Skins);int32 Count=0;
  for(auto* C:Skins)if(C->ComponentHasTag(TEXT("ImportedCard"))){if(!C->SetImportedPose(World)){Error=TEXT("Shared pose target mismatch");return false;}++Count;}
  if(!Count){Error=TEXT("No imported skeletal renderers");return false;}Error.Reset();return true;
 }
};
}
void OKInstallNativeSharedAnimationDriver(){OKRegisterSharedAnimationDriver([](){return MakeUnique<FNativeSharedDriver>();});}
