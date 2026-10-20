#pragma once

#include "CoreMinimal.h"
#include "BoneControllers/AnimNode_SkeletalControlBase.h"
#include "OKUnrealSkeleton.h"
#include "OKAnimationTypes.h"
#include "AnimNode_OKContactIK.generated.h"

class UOKAnimationAsset;
class UOKRetargetProfile;

/**
 * The contacts of an OK animation solved on an Unreal mesh: for each contact of the role active at the current time,
 * the effector's limb (its two mesh parents) bends onto the target with two-bone IK. Targets: the ground (feet planted
 * at the authored height) and bones of the same mesh (hand on hip, ...). Partner contacts need the group player.
 * Place it after an OK Player node playing the same asset (same play rate, started together, or drive Time).
 */
USTRUCT(BlueprintInternalUseOnly)
struct OKANIMATION_API FAnimNode_OKContactIK : public FAnimNode_SkeletalControlBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Settings", meta = (PinShownByDefault))
	TObjectPtr<UOKAnimationAsset> Animation;
	UPROPERTY(EditAnywhere, Category = "Settings", meta = (PinHiddenByDefault))
	TObjectPtr<UOKRetargetProfile> Profile;
	UPROPERTY(EditAnywhere, Category = "Settings", meta = (PinHiddenByDefault))
	int32 Role = 0;
	UPROPERTY(EditAnywhere, Category = "Settings", meta = (PinShownByDefault))
	float PlayRate = 1.f;
	/** Drive the time from a pin instead of the node's own clock. */
	UPROPERTY(EditAnywhere, Category = "Settings")
	bool bUseTimePin = false;
	UPROPERTY(EditAnywhere, Category = "Settings", meta = (PinHiddenByDefault, EditCondition = "bUseTimePin"))
	float Time = 0.f;
	/** Ground height in component space (cm). */
	UPROPERTY(EditAnywhere, Category = "Settings", meta = (PinHiddenByDefault))
	float GroundHeight = 0.f;
	/** Limbs may stretch up to this scale to reach (1 = never). */
	UPROPERTY(EditAnywhere, Category = "Settings", meta = (ClampMin = "1", ClampMax = "1.5"))
	float MaxStretch = 1.f;

	/** Contacts solved in the last evaluation, and their largest remaining distance (cm), for tests and debugging. */
	int32 LastSolved = 0;
	float LastMaxError = 0.f;

	virtual void Initialize_AnyThread(const FAnimationInitializeContext& Context) override;
	virtual void UpdateInternal(const FAnimationUpdateContext& Context) override;
	virtual void GatherDebugData(FNodeDebugData& DebugData) override;
	virtual void EvaluateSkeletalControl_AnyThread(FComponentSpacePoseContext& Output, TArray<FBoneTransform>& OutBoneTransforms) override;
	virtual bool IsValidToEvaluate(const USkeleton* Skeleton, const FBoneContainer& RequiredBones) override;

	/** The contacts of a role active at T with their weight (fade in/out x weight). */
	static TArray<TPair<FOKContact, float>> ActiveContacts(const UOKAnimationAsset* Asset, int32 Role, float T);

private:
	virtual void InitializeBoneReferences(const FBoneContainer& RequiredBones) override;
	bool EnsureBinding(const FReferenceSkeleton& Ref);
	FOKUnrealSkeleton Binding;
	const FReferenceSkeleton* BoundRef = nullptr;
	/** Family rest world rotation (OK space) per family bone name, and the mesh rest (component space) per mesh bone. */
	TMap<FName, FQuat> FamilyRestWorld;
	TArray<FTransform> MeshRestComponent;
	float Clock = 0.f;
};
