#pragma once

#include "CoreMinimal.h"
#include "Curves/RichCurve.h"
#include "OKAnimationKeys.generated.h"

/**
 * Keyed channels of one bone in an OK animation's key layer, like a Maya animation layer on top of the base motion:
 * rotation X/Y/Z (degrees, Euler of a rotation applied after the base local rotation, in the bone's own frame of the
 * animation's source rest) and, on the hips, position X/Y/Z (offset in hips heights, in the hips parent's rest frame).
 * Interpolation and tangents are the curves' own (spline by default), edited in the graph editor.
 */
USTRUCT(BlueprintType)
struct OKANIMATION_API FOKBoneKeys
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Keys")
	FName Bone;
	UPROPERTY(EditAnywhere, Category = "Keys")
	FRichCurve RotX;
	UPROPERTY(EditAnywhere, Category = "Keys")
	FRichCurve RotY;
	UPROPERTY(EditAnywhere, Category = "Keys")
	FRichCurve RotZ;
	UPROPERTY(EditAnywhere, Category = "Keys")
	FRichCurve PosX;
	UPROPERTY(EditAnywhere, Category = "Keys")
	FRichCurve PosY;
	UPROPERTY(EditAnywhere, Category = "Keys")
	FRichCurve PosZ;

	static constexpr int32 NumChannels = 6;
	FRichCurve& Channel(int32 I);
	const FRichCurve& Channel(int32 I) const;
	static FText ChannelName(int32 I);

	bool IsEmpty() const;
	FQuat EvalRotation(float T) const;
	FVector EvalPosition(float T) const;
	bool HasKey(float T, float Tolerance) const;
	/** Rotation key at T (all three channels), Euler unwound next to the curves' current values. */
	void SetRotationKey(float T, const FQuat& Rotation);
	void SetPositionKey(float T, const FVector& Position);
	/** One channel keyed at T (the channel box). */
	void SetChannelKey(int32 Channel, float T, float Value);
	/** Keys the current value of every channel that has keys (or all rotation channels when none). */
	void KeyCurrent(float T, bool bPosition);
	void RemoveKeys(float T, float Tolerance);
	void GetKeyTimes(TArray<float>& Out) const;
	/** Moves every key at From to To (replacing keys already at To). */
	void MoveKeys(float From, float To, float Tolerance);
};

/** The dense motion of an OK animation, decoded for editing (OK space, Unity axes). */
struct OKANIMATION_API FOKAnimTracks
{
	int32 Frames = 0;
	float Fps = 30.f;
	TArray<FName> Bones;
	TArray<FName> ParentNames;
	TArray<FQuat> RestLocal, RestWorld;
	/** Frames x bones rotation deltas, frames hips offsets (in hips heights). */
	TArray<FQuat> Rot;
	TArray<FVector> Hips;
	FName HipsBone;
	FQuat HipsParentRestWorld = FQuat::Identity;
	TMap<FName, int32> Index;

	bool Load(const TArray<uint8>& Bytes, FString& OutError);
	bool Save(const TArray<uint8>& Template, TArray<uint8>& OutBytes, FString& OutError) const;
	int32 Find(FName Bone) const { const int32* I = Index.Find(Bone); return I ? *I : INDEX_NONE; }
	/** New frame count: the last frame is held when lengthened, frames are cut when shortened. */
	void SetFrameCount(int32 NewFrames);
	/** Bakes a key layer on top of this motion. */
	void ApplyLayer(const TArray<FOKBoneKeys>& Layer);
};
