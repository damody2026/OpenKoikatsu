#pragma once

#include "CoreMinimal.h"
#include "ReferenceSkeleton.h"

class UOKRetargetProfile;
class UOKSkeletonFamily;
class UAnimSequence;
class UOKAnimSequence;

/**
 * An Unreal skeleton seen as an OK skeleton: its mapped bones under family names, their rest (relative to the nearest
 * mapped ancestor, so unmapped intermediates fold in) in OK space (Unity axes, metres), optionally aligned to the
 * family rest. Used both ways: Unreal animation -> OK animation, and OK animation -> Unreal pose (AnimGraph node,
 * "expand to AnimSequence", the editor preview).
 */
struct OKANIMATION_API FOKUnrealSkeleton
{
	/** Per header bone: Unreal bone index, family name, header index of the nearest mapped ancestor. */
	TArray<int32> BoneIndex;
	TArray<FName> FamilyName;
	TArray<int32> MappedParent;
	/** Header index of every Unreal bone (INDEX_NONE when unmapped). */
	TArray<int32> HeaderOfBone;
	int32 Hips = INDEX_NONE;
	FReferenceSkeleton Ref;
	/** Target / source header JSON (with the family rest to align to, when the profile asks for it). */
	FString HeaderJson;
	/** Rest world rotation (OK space) of every header bone after alignment: a sampled pose is relative to it. */
	TArray<FQuat> AlignedRestWorld;
	/** Hips offset length of this skeleton's rest (OK metres). */
	float HipsLength = 1.f;

	bool IsValid() const { return BoneIndex.Num() > 0 && Hips != INDEX_NONE; }
	bool Build(const FReferenceSkeleton& InRef, const UOKRetargetProfile* Profile, const UOKSkeletonFamily* Family, FString& OutError);

	/** Unreal local pose (all Ref bones) -> header locals (bones x 4, OK space) and the hips offset (3). */
	void PoseToLocals(const TArray<FTransform>& Local, float* OutRotations, float* OutHips) const;
	/** Header locals and hips (SampleOnto output) -> Unreal local pose; unmapped bones keep their pose in Local. */
	void LocalsToPose(const float* Sample, TArray<FTransform>& Local) const;

	/** Unreal local pose of an animation of this skeleton at T (bones without a track at their reference pose). */
	void SampleSource(const UAnimSequence* Sequence, double T, TArray<FTransform>& OutLocal) const;
	/** Component-space positions (OK space) of the header bones for an Unreal local pose. */
	TArray<FVector> HeaderPositions(const TArray<FTransform>& Local) const;
	/** Local poses of this skeleton (one per frame at Fps) -> OK animation bytes; Meta goes into the animation's meta. */
	bool ConvertLocals(const TArray<TArray<FTransform>>& Frames, float Fps, bool bLooping, const FString& Name, const TMap<FString, FString>& Meta, TArray<uint8>& OutBytes, FString& OutError) const;
	/** Unreal animation -> OK animation bytes (sampled at Fps). */
	bool ConvertAnimation(const UAnimSequence* Sequence, float Fps, TArray<uint8>& OutBytes, FString& OutError) const;
	/** Unreal local pose of a whole OK animation sampled at Time (for the preview and AnimSequence expansion). */
	bool SamplePose(const UOKAnimSequence* Anim, float Time, TArray<FTransform>& OutLocal) const;

	static FQuat ToOk(const FQuat& Q);
	static FQuat FromOk(const FQuat& Q);
	static FVector ToOkPosition(const FVector& V);
	static FVector FromOkPosition(const FVector& V);
	/** Normalised bone name for preset matching (case, separators and Biped / Mixamo prefixes ignored). */
	static FString NormalizeName(const FString& Name);
	/** Angle between two rotations (degrees), stable near zero (FQuat::AngularDistance's acos turns float noise into ~0.1 deg). */
	static double AngleDegrees(const FQuat& A, const FQuat& B);
};
