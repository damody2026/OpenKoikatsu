#include "KKCharAnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimNodeBase.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimationPoseData.h"
#include "Animation/AnimationAsset.h"
#include "Animation/AnimTypes.h"
#include "BoneContainer.h"
#include "BonePose.h"
#include "Components/SkeletalMeshComponent.h"

struct FKKCharAnimInstanceProxy : public FAnimInstanceProxy
{
	FKKCharAnimInstanceProxy() = default;
	explicit FKKCharAnimInstanceProxy(UAnimInstance* InAnimInstance)
		: FAnimInstanceProxy(InAnimInstance)
	{
	}

	virtual void PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds) override
	{
		FAnimInstanceProxy::PreUpdate(InAnimInstance, DeltaSeconds);
		if (const UKKCharAnimInstance* Inst = Cast<UKKCharAnimInstance>(InAnimInstance))
		{
			LocalPose = Inst->PendingPose;
			BoneLayer = Inst->BoneLayer;
			ExternalSeq = Inst->ExternalSeq;
			SeqTime = Inst->SeqTime;
			bHasPose = Inst->bHasPose;
		}
	}

	virtual bool Evaluate(FPoseContext& Output) override
	{
		const FBoneContainer& Bones = Output.Pose.GetBoneContainer();
		const int32 N = Output.Pose.GetNumBones();
		if (ExternalSeq)
		{
			Output.ResetToRefPose();
			FAnimationPoseData PoseData(Output);
			FAnimExtractContext Ctx;
			Ctx.CurrentTime = SeqTime;
			ExternalSeq->GetAnimationPose(PoseData, Ctx);
			if (bHasPose)
			{
				for (int32 i = 0; i < N; ++i)
				{
					const FCompactPoseBoneIndex Compact(i);
					const int32 SkelIdx = Bones.GetSkeletonIndex(Compact);
					if (BoneLayer.IsValidIndex(SkelIdx) && BoneLayer[SkelIdx] == 2
						&& LocalPose.IsValidIndex(SkelIdx))
					{
						Output.Pose[Compact] = LocalPose[SkelIdx];
					}
				}
			}
			return true;
		}
		if (!bHasPose || LocalPose.Num() == 0)
		{
			Output.ResetToRefPose();
			return true;
		}
		for (int32 i = 0; i < N; ++i)
		{
			const FCompactPoseBoneIndex Compact(i);
			const int32 SkelIdx = Bones.GetSkeletonIndex(Compact);
			if (LocalPose.IsValidIndex(SkelIdx))
			{
				Output.Pose[Compact] = LocalPose[SkelIdx];
			}
		}
		return true;
	}

	TArray<FTransform> LocalPose;
	TArray<uint8> BoneLayer;
	TObjectPtr<UAnimSequence> ExternalSeq;
	float SeqTime = 0.f;
	bool bHasPose = false;
};

FAnimInstanceProxy* UKKCharAnimInstance::CreateAnimInstanceProxy()
{
	return new FKKCharAnimInstanceProxy(this);
}

void UKKCharAnimInstance::SetLocalPose(TArray<FTransform> InPose, TArray<uint8> InLayer)
{
	PendingPose = MoveTemp(InPose);
	BoneLayer = MoveTemp(InLayer);
	bHasPose = PendingPose.Num() > 0;
	if (USkeletalMeshComponent* SMC = GetSkelMeshComponent())
	{
		SMC->TickAnimation(0.f, false);
		SMC->RefreshBoneTransforms();
		SMC->RefreshFollowerComponents();
        // Bone/morph streams change, but the mesh, materials and vertex factory
        // contract do not. RefreshBoneTransforms already publishes the pose;
        // recreating every facial render proxy each frame stalls scene captures.
		SMC->MarkRenderDynamicDataDirty();
	}
}

void UKKCharAnimInstance::SetLocalPoseDeferred(const TArray<FTransform>& InPose)
{
	PendingPose = InPose;
	BoneLayer.Reset();
	bHasPose = PendingPose.Num() > 0;
}

void UKKCharAnimInstance::SetExternalSequence(UAnimSequence* Seq, float Time)
{
	ExternalSeq = Seq;
	SeqTime = Time;
	if (USkeletalMeshComponent* SMC = GetSkelMeshComponent())
	{
		SMC->TickAnimation(0.f, false);
		SMC->RefreshBoneTransforms();
		SMC->RefreshFollowerComponents();
		SMC->MarkRenderDynamicDataDirty();
	}
}
