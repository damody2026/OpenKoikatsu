#include "AnimNode_OKPlayer.h"
#include "OKAnimationAssets.h"
#include "OKAnimationLibrary.h"
#include "KKOkAnimation.h"
#include "Animation/AnimInstanceProxy.h"

bool FAnimNode_OKPlayer::EnsurePlayer()
{
	if (!Animation) { Player.Reset(); PlayerAsset.Reset(); return false; }
	if (Player && PlayerAsset.Get() == Animation) return true;
	FString Error;
	Player = Animation->CreatePlayer(Error);
	PlayerAsset = Animation;
	if (!Player) UE_LOG(LogTemp, Warning, TEXT("OK player node: %s: %s"), *Animation->GetName(), *Error);
	return Player.IsValid();
}

void FAnimNode_OKPlayer::Initialize_AnyThread(const FAnimationInitializeContext& Context)
{
	FAnimNode_Base::Initialize_AnyThread(Context);
	GetEvaluateGraphExposedInputs().Execute(Context);
	Time = StartTime;
	Player.Reset();
	PlayerAsset.Reset();
}

void FAnimNode_OKPlayer::CacheBones_AnyThread(const FAnimationCacheBonesContext& Context)
{
	const FReferenceSkeleton& Ref = Context.AnimInstanceProxy->GetRequiredBones().GetReferenceSkeleton();
	if (BoundRef == &Ref && Binding.IsValid()) return;
	BoundRef = &Ref;
	FString Error;
	const UOKSkeletonFamily* Family = Profile && Profile->Family ? Profile->Family.Get() : UOKAnimationLibrary::GetDefaultFamily();
	if (!Binding.Build(Ref, Profile, Family, Error)) UE_LOG(LogTemp, Warning, TEXT("OK player node: skeleton not mapped: %s"), *Error);
}

void FAnimNode_OKPlayer::Update_AnyThread(const FAnimationUpdateContext& Context)
{
	GetEvaluateGraphExposedInputs().Execute(Context);
	Time += Context.GetDeltaTime() * PlayRate;
	if (EnsurePlayer())
	{
		Player->SetParam(0, X);
		Player->SetParam(1, Y);
	}
}

void FAnimNode_OKPlayer::Evaluate_AnyThread(FPoseContext& Output)
{
	TArray<float> Sample;
	if (!Binding.IsValid() || !EnsurePlayer() || !Player->SampleOnto(Role, Binding.HeaderJson, Time, Sample) || Sample.Num() != Binding.BoneIndex.Num() * 4 + 3)
	{
		Output.ResetToRefPose();
		return;
	}
	Scratch = Binding.Ref.GetRefBonePose();
	Binding.LocalsToPose(Sample.GetData(), Scratch);
	const FBoneContainer& Bones = Output.Pose.GetBoneContainer();
	for (FCompactPoseBoneIndex I : Output.Pose.ForEachBoneIndex())
	{
		const int32 Mesh = Bones.MakeMeshPoseIndex(I).GetInt();
		Output.Pose[I] = Scratch.IsValidIndex(Mesh) ? Scratch[Mesh] : Bones.GetRefPoseTransform(I);
	}
}

void FAnimNode_OKPlayer::GatherDebugData(FNodeDebugData& DebugData)
{
	DebugData.AddDebugItem(FString::Printf(TEXT("OK Player (%s t=%.2f role=%d bones=%d)"), Animation ? *Animation->GetName() : TEXT("none"), Time, Role, Binding.BoneIndex.Num()));
}
