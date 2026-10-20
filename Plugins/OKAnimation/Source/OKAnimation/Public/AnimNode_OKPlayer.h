#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNodeBase.h"
#include "OKUnrealSkeleton.h"
#include "AnimNode_OKPlayer.generated.h"

class FOkAnimPlayer;
class UOKAnimationAsset;
class UOKRetargetProfile;

/**
 * Plays any OK asset (animation, blend space, montage, one role of a group) on an Unreal skeletal mesh: the motion is
 * carried onto the mesh's own rest through a retarget profile (or a built-in preset). Contacts are not solved here
 * (no OK character body); use the OK characters' group player for interactions.
 */
USTRUCT(BlueprintInternalUseOnly)
struct OKANIMATION_API FAnimNode_OKPlayer : public FAnimNode_Base
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Settings", meta = (PinShownByDefault))
	TObjectPtr<UOKAnimationAsset> Animation;
	/** Null: a built-in preset matched by bone names, aligned to the default family rest. */
	UPROPERTY(EditAnywhere, Category = "Settings", meta = (PinHiddenByDefault))
	TObjectPtr<UOKRetargetProfile> Profile;
	UPROPERTY(EditAnywhere, Category = "Settings", meta = (PinHiddenByDefault))
	int32 Role = 0;
	UPROPERTY(EditAnywhere, Category = "Settings", meta = (PinShownByDefault))
	float PlayRate = 1.f;
	/** Blend space parameters (first and second axis). */
	UPROPERTY(EditAnywhere, Category = "Settings", meta = (PinShownByDefault))
	float X = 0.f;
	UPROPERTY(EditAnywhere, Category = "Settings", meta = (PinShownByDefault))
	float Y = 0.f;
	UPROPERTY(EditAnywhere, Category = "Settings", meta = (PinHiddenByDefault))
	float StartTime = 0.f;

	virtual void Initialize_AnyThread(const FAnimationInitializeContext& Context) override;
	virtual void CacheBones_AnyThread(const FAnimationCacheBonesContext& Context) override;
	virtual void Update_AnyThread(const FAnimationUpdateContext& Context) override;
	virtual void Evaluate_AnyThread(FPoseContext& Output) override;
	virtual void GatherDebugData(FNodeDebugData& DebugData) override;

	float GetTime() const { return Time; }

private:
	bool EnsurePlayer();
	TSharedPtr<FOkAnimPlayer> Player;
	TWeakObjectPtr<const UOKAnimationAsset> PlayerAsset;
	FOKUnrealSkeleton Binding;
	const FReferenceSkeleton* BoundRef = nullptr;
	TArray<FTransform> Scratch;
	float Time = 0.f;
};
