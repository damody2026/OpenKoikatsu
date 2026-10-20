#pragma once

#include "CoreMinimal.h"
#include "Curves/CurveFloat.h"
#include "OKAnimationTypes.generated.h"

class FJsonObject;
class UOKAnimationAsset;

/** Scene whose limb corrections the character's finishing pass applies to the animation (KK's per-scene rules). */
UENUM(BlueprintType)
enum class EOKSourceScene : uint8
{
	Auto = 0,
	Maker = 1,
	Normal = 2,
	H = 3,
	Studio = 4,
};

/**
 * Semantic contact: "role R's effector bone meets role T's target bone (offset in the target bone's frame)".
 * The player places the roles and bends the effector's limb so the contact holds on the actual bodies, whatever
 * their sizes. Offsets are in OK space (Unity axes) and scale with the target's hips rest length.
 */
USTRUCT(BlueprintType)
struct OKANIMATION_API FOKContact
{
	GENERATED_BODY()

	/** Role whose bone reaches (0 in a single animation). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Contact", meta = (ClampMin = "0"))
	int32 Role = 0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Contact")
	FName Effector;
	/** Role touched; -1 = the ground, -2 = a fixed point in the group space. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Contact", meta = (ClampMin = "-2"))
	int32 TargetRole = -1;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Contact")
	FName TargetBone;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Contact")
	FVector Offset = FVector::ZeroVector;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Timing", meta = (ClampMin = "0"))
	float Start = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Timing", meta = (ClampMin = "0"))
	float End = 1.f;
	/** Fade in/out time (s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Timing", meta = (ClampMin = "0"))
	float Blend = 0.1f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Solve", meta = (ClampMin = "0", ClampMax = "1"))
	float Weight = 1.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Solve")
	float Priority = 1.f;
	/** Bend the effector's limb onto the target (hands, feet). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Solve")
	bool bIK = true;
	/** Who adapts to different body sizes: 0 = the effector's role moves, 1 = the target's role, between = both. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Solve", meta = (ClampMin = "0", ClampMax = "1"))
	float TargetMoves = 0.f;
	/** Offset over the contact's time (uniform samples, start to end) when the effector slides on the target; empty = Offset. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "Contact")
	TArray<FVector> Track;

	TSharedRef<FJsonObject> ToJson() const;
	static FOKContact FromJson(const FJsonObject& Json);
};

/** A float curve carried by an OK animation (expressions, gaze, mouth...). Sampled per frame when saved. */
USTRUCT(BlueprintType)
struct OKANIMATION_API FOKCurveTrack
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Curve")
	FName Name;
	UPROPERTY(EditAnywhere, Category = "Curve")
	FRuntimeFloatCurve Curve;
};

USTRUCT(BlueprintType)
struct OKANIMATION_API FOKBlendAxis
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Axis")
	FName Name = TEXT("Speed");
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Axis")
	float Min = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Axis")
	float Max = 1.f;
};

USTRUCT(BlueprintType)
struct OKANIMATION_API FOKBlendSample
{
	GENERATED_BODY()

	/** An OK animation, group, montage or another blend space (all samples must have the same number of roles). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sample")
	TObjectPtr<UOKAnimationAsset> Animation;
	/** Position on the axes (Y ignored in 1D). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sample")
	FVector2D Position = FVector2D::ZeroVector;
};

/** A two-bone IK chain of a skeleton family. */
USTRUCT(BlueprintType)
struct OKANIMATION_API FOKIKChain
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IK")
	FName Name;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IK")
	FName Root;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IK")
	FName Mid;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IK")
	FName End;
};

USTRUCT(BlueprintType)
struct OKANIMATION_API FOKGroupRole
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Role")
	FName Name;
	/** A single-role OK asset: an animation, or a 1-role blend space / montage. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Role")
	TObjectPtr<UOKAnimationAsset> Animation;
	/** Root offset in the group space (OK axes), in units of the role's own hips rest height. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Role")
	FVector Offset = FVector::ZeroVector;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Role")
	float Yaw = 0.f;
};

USTRUCT(BlueprintType)
struct OKANIMATION_API FOKMontageSection
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Section")
	FName Name;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Section")
	TObjectPtr<UOKAnimationAsset> Animation;
	/** Start on the montage timeline; negative = right after the previous section (minus this section's blend). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Section")
	float Start = -1.f;
	/** Length on the montage timeline; 0 = the animation's own length at Rate. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Section", meta = (ClampMin = "0"))
	float Length = 0.f;
	/** Where in the animation the section starts (s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Section", meta = (ClampMin = "0"))
	float From = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Section", meta = (ClampMin = "0.01"))
	float Rate = 1.f;
	/** Cross-fade from the previous section (s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Section", meta = (ClampMin = "0"))
	float BlendIn = 0.2f;
	/** Section played after this one (itself = loop it); none = the next on the timeline. Changed at run time with SetNextSection. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Section")
	FName Next;
};

/** Report of one played frame (group contact errors after placement and IK). */
USTRUCT(BlueprintType)
struct OKANIMATION_API FOKFrameReport
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Report")
	int32 Contacts = 0;
	/** Largest remaining contact distance (OK units, metres). */
	UPROPERTY(BlueprintReadOnly, Category = "Report")
	float MaxContactError = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "Report")
	FString Json;
};

UENUM(BlueprintType)
enum class EOKRootMotion : uint8
{
	/** The hips move as authored (looping clips snap back at the loop). */
	AsAuthored,
	/** The clip's net travel is removed: the character stays in place. */
	InPlace,
	/** The pose stays in place and the character itself moves by the travel. */
	RootMotion,
};

/** Asset parameters of an OK animation (stored in its meta, so they travel with the .okanim). */
USTRUCT(BlueprintType)
struct OKANIMATION_API FOKAnimationMeta
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Info")
	FString Category;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Info")
	TArray<FString> Tags;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Info")
	FString Source;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Info")
	FString License;
	/** Blend time when this animation starts / ends (UOKAnimationComponent::Play with a negative blend uses FadeIn). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Playback", meta = (ClampMin = "0"))
	float FadeIn = 0.2f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Playback", meta = (ClampMin = "0"))
	float FadeOut = 0.2f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Playback")
	EOKRootMotion RootMotion = EOKRootMotion::AsAuthored;
	/** Scale of the contact IK of hands / feet (body-size adaptation). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Adaptation", meta = (ClampMin = "0", ClampMax = "1"))
	float HandIK = 1.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Adaptation", meta = (ClampMin = "0", ClampMax = "1"))
	float FootIK = 1.f;
	/** Keep planted feet locked on the ground (procedural steps when the body moves away). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Adaptation")
	bool bFootLock = true;
};
