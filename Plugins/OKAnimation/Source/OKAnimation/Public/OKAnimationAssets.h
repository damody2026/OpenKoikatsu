#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Engine/DeveloperSettings.h"
#include "OKAnimationTypes.h"
#include "OKAnimationKeys.h"
#include "OKAnimationAssets.generated.h"

class FOkAnimPlayer;
class USkeletalMesh;
class UAnimSequence;
class UOKRetargetProfile;

/** Collects the animation blobs of a spec tree (each sequence once) and detects cycles. */
struct OKANIMATION_API FOKSpecBuilder
{
	TArray<TArray<uint8>> Blobs;
	TMap<const UObject*, int32> BlobIndex;
	TArray<const UObject*> Stack;
	FString Error;

	int32 AddBlob(const UObject* Owner, const TArray<uint8>& Data);
	/** Spec JSON of an asset (recursing into referenced assets). Null on error (see Error). */
	TSharedPtr<FJsonObject> Build(const UOKAnimationAsset* Asset);
	/** Switching from one asset (playing at FromTime) to another over Duration seconds. */
	TSharedPtr<FJsonObject> BuildCrossfade(const UOKAnimationAsset* From, float FromTime, const UOKAnimationAsset* To, float Duration);
};

/**
 * Anything an OK player can play: one animation, a 1..N role group, a 1D/2D blend space or a montage. Assets nest
 * freely (a blend space of groups, a montage of blend spaces...). Not bound to any skeleton: the player carries the
 * motion onto whatever OK skeleton plays it.
 */
UCLASS(Abstract, BlueprintType)
class OKANIMATION_API UOKAnimationAsset : public UObject
{
	GENERATED_BODY()

public:
	/** This asset's spec node; referenced assets go through Builder.Build. */
	virtual TSharedPtr<FJsonObject> BuildSpecNode(FOKSpecBuilder& Builder) const PURE_VIRTUAL(UOKAnimationAsset::BuildSpecNode, return nullptr;);
	virtual int32 GetRoleCount() const { return 1; }

	/** Full spec JSON plus the animation blobs it references. */
	bool BuildSpec(FString& OutSpecJson, TArray<TArray<uint8>>& OutBlobs, FString& OutError) const;
	/** A ready player (fresh state: feet, placement history). */
	TSharedPtr<FOkAnimPlayer> CreatePlayer(FString& OutError) const;
	/** A player switching from another asset (playing at FromTime) to this one over BlendTime. */
	TSharedPtr<FOkAnimPlayer> CreateCrossfadePlayer(const UOKAnimationAsset* From, float FromTime, float BlendTime, FString& OutError) const;

	/** The default family's bone aliases onto a player (bodies naming bones differently), and the walk cycle. */
	static void ApplyFamilyAliases(FOkAnimPlayer& Player);
	/** The walk cycle group players mix in when the placement moves a role fast: ok.Anim.WalkClip (an .okanim), else the
	 *  first looping "walk" clip of the converted KK library. Empty when there is none. */
	static const TArray<uint8>& DefaultWalk();

	UFUNCTION(BlueprintPure, Category = "OK Animation")
	float GetPlayLength() const;
	UFUNCTION(BlueprintPure, Category = "OK Animation")
	int32 GetNumRoles() const { return GetRoleCount(); }
	/** Names of the player parameters (blend space axes), in parameter order. */
	UFUNCTION(BlueprintPure, Category = "OK Animation")
	virtual TArray<FName> GetParameterNames() const;
};

/** One OK animation (OKANIM02 bytes) with its editable parameters. */
UCLASS(BlueprintType, hidecategories = Object)
class OKANIMATION_API UOKAnimSequence : public UOKAnimationAsset
{
	GENERATED_BODY()

public:
	UPROPERTY()
	TArray<uint8> Data;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Info")
	FName Family;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Info")
	float Fps = 30.f;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Info")
	int32 Frames = 0;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Info")
	float Duration = 0.f;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Info")
	TArray<FName> Bones;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Info")
	TMap<FName, FName> BoneParents;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Playback")
	bool bLooping = true;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Playback")
	EOKSourceScene Scene = EOKSourceScene::Auto;
	/** Playback rate inside specs that reference this sequence. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Playback", meta = (ClampMin = "0.01"))
	float RateScale = 1.f;
	/** The animation's own contacts (ground, objects; role 0 = the character playing it). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Contacts")
	TArray<FOKContact> Contacts;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Curves")
	TArray<FOKCurveTrack> Curves;
	/** Category, tags, licence, fades, root motion, IK weights (kept in Meta under fixed keys). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Meta")
	FOKAnimationMeta Info;
	/** Free key / value pairs (source keys, conversion details...). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Meta")
	TMap<FString, FString> Meta;
#if WITH_EDITORONLY_DATA
	/** The AnimSequence this animation was expanded to for editing in Unreal's tools (written back from it). */
	UPROPERTY(VisibleAnywhere, Category = "Editing")
	TSoftObjectPtr<UAnimSequence> EditProxy;
	/** Skeleton mapping used for the expansion / write-back. */
	UPROPERTY(EditAnywhere, Category = "Editing")
	TObjectPtr<UOKRetargetProfile> EditProfile;
#endif

	/** Base motion under the key layer (empty while there is no layer: Data is the base). */
	UPROPERTY()
	TArray<uint8> SourceData;
	/** Keyed edits on top of the base motion (the OK animation editor's animate mode); baked into Data. */
	UPROPERTY()
	TArray<FOKBoneKeys> KeyLayer;

	const TArray<uint8>& GetBaseData() const { return SourceData.IsEmpty() ? Data : SourceData; }
	/** Rebuilds Data = base + key layer. */
	bool Rebake(FString& OutError);
	/** Makes the current result the new base (the layer's keys are removed). */
	bool CollapseLayer(FString& OutError);
	/** A modifier on the motion ({"op": "trim_rest_start" | "find_loop" | "auto_loop" | "close_loop" | "in_place" | "smooth" |
	 *  "renormalize" | "foot_contacts", params}); a key layer is collapsed first. Frames, contacts and meta follow the result. */
	bool ApplyModifier(const FString& OpJson, FString& OutError);
	/** Frame-by-frame quality problems (joint extremes, knees backwards, foot sliding, pops / finger flips, hands in the body, feet under the ground). */
	struct FQualityIssue { FString Kind; FName Bone; int32 Start = 0; int32 End = 0; float Value = 0.f; };
	bool CheckQuality(TArray<FQualityIssue>& OutIssues, FString& OutError) const;
	/** Lengthens (holding the last frame) or shortens the animation; keys past the new end are removed. */
	bool SetFrameCount(int32 NewFrames, FString& OutError);
	FOKBoneKeys* FindKeys(FName Bone);
	FOKBoneKeys& FindOrAddKeys(FName Bone);

	/** Replaces the bone data (and every parameter) with OK animation bytes. */
	UFUNCTION(BlueprintCallable, Category = "OK Animation")
	bool SetData(const TArray<uint8>& Bytes, FString& OutError);
	/** Writes the edited parameters (looping, scene, contacts, curves, meta) into the bytes. */
	UFUNCTION(BlueprintCallable, Category = "OK Animation")
	bool ApplyEdits(FString& OutError);
	/** Pose of this animation at Time on a target skeleton header (see FOkAnimBridge::SampleOnto). */
	bool SampleOnto(const FString& TargetJson, float Time, TArray<float>& Out) const;

	virtual TSharedPtr<FJsonObject> BuildSpecNode(FOKSpecBuilder& Builder) const override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& Event) override;
	virtual void PostEditUndo() override;
#endif
	virtual void GetAssetRegistryTags(FAssetRegistryTagsContext Context) const override;

	/** The decoded base motion (cached while the base bytes stay the same). */
	TSharedPtr<const FOKAnimTracks> GetBaseTracks(FString& OutError) const;

private:
	bool ReadInfo(FString& OutError, bool bParameters);
	mutable TSharedPtr<const FOKAnimTracks> BaseTracks;
	mutable uint32 BaseTracksCrc = 0;
	mutable int32 BaseTracksSize = -1;
};

/** 1..N roles played together, with contacts between them that adapt to every role's body size. */
UCLASS(BlueprintType, hidecategories = Object)
class OKANIMATION_API UOKGroupAnimSequence : public UOKAnimationAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Group")
	TArray<FOKGroupRole> Roles;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Group")
	TArray<FOKContact> Contacts;

	/** Fills this group from an OKGROUP1 package (KK paired state conversion); creates the role sequences in Outer. */
	bool SetFromPackage(const TArray<uint8>& Package, UObject* SequenceOuter, FString& OutError);

	virtual TSharedPtr<FJsonObject> BuildSpecNode(FOKSpecBuilder& Builder) const override;
	virtual int32 GetRoleCount() const override { return FMath::Max(1, Roles.Num()); }
};

/** 1D or 2D blend space over OK assets (single animations or whole groups), phase synchronised. */
UCLASS(BlueprintType, hidecategories = Object)
class OKANIMATION_API UOKBlendSpace : public UOKAnimationAsset
{
	GENERATED_BODY()

public:
	/** One axis = 1D, two = 2D (Delaunay triangulation of the samples). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blend Space")
	TArray<FOKBlendAxis> Axes;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blend Space")
	TArray<FOKBlendSample> Samples;
	/** Every sample at the same fraction of its own cycle (walk/run). Off: all at the same time. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blend Space")
	bool bSyncPhase = true;

	virtual TSharedPtr<FJsonObject> BuildSpecNode(FOKSpecBuilder& Builder) const override;
	virtual int32 GetRoleCount() const override;
	virtual TArray<FName> GetParameterNames() const override;
};

/** Sections laid on one timeline, each cross-fading in over the previous ones. */
UCLASS(BlueprintType, hidecategories = Object)
class OKANIMATION_API UOKAnimMontage : public UOKAnimationAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Montage")
	TArray<FOKMontageSection> Sections;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Montage")
	bool bLooping = false;

	/** Timeline start of a section (resolving automatic starts). */
	UFUNCTION(BlueprintPure, Category = "OK Animation")
	float GetSectionStart(FName Section) const;
	/** Timeline range [start, end) of every section, in section order. */
	TArray<TPair<float, float>> GetSectionRanges() const;

	virtual TSharedPtr<FJsonObject> BuildSpecNode(FOKSpecBuilder& Builder) const override;
	virtual int32 GetRoleCount() const override;
	virtual TArray<FName> GetParameterNames() const override;
};

/**
 * An OK skeleton family: the standard bone names and rest every OK animation of the family is authored against
 * (KK: the cf_j_* Animator layer). Its rest drives the automatic rest alignment of other skeletons (A-pose / T-pose).
 */
UCLASS(BlueprintType, hidecategories = Object)
class OKANIMATION_API UOKSkeletonFamily : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Family")
	FName FamilyId = TEXT("kk.cf_j");
	/** Standard rest as an OK skeleton header JSON (bones: name, parent, rest_local, rest_world, rest_pos). */
	UPROPERTY(EditAnywhere, Category = "Family", meta = (MultiLine = true))
	FString RestHeaderJson;
	/** Optional mesh to preview the family in the OK animation editor. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Preview")
	TSoftObjectPtr<USkeletalMesh> PreviewMesh;
	/** Bones a body names differently (character bone -> family bone), e.g. mod bodies. Applied when OK animations play on characters. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Family")
	TMap<FName, FName> Aliases;
	/** Version of the standard rest (raised when the rest is re-captured; animations record the version they were made on). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Family")
	int32 RestVersion = 1;
	/** Other standard rests of the family (e.g. "male": KK men have their own rest), same header format. */
	UPROPERTY(EditAnywhere, Category = "Family", meta = (MultiLine = true))
	TMap<FName, FString> RestVariants;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Preview")
	TMap<FName, TSoftObjectPtr<USkeletalMesh>> VariantPreviewMeshes;
	/** Left / right bone pairs (each pair once, either way round) for mirroring. Empty: matched by _L / _R names. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Family")
	TMap<FName, FName> MirrorPairs;
	/** Two-bone IK chains (root, mid, end) the animate tools and contacts bend. Empty: an end bone's two parents. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Family")
	TArray<FOKIKChain> IKChains;

	/** The mirror partner of a bone (pairs first, then the _L / _R naming rule); none when it is a centre bone. */
	FName MirrorOf(FName Bone) const;
	/** Pairs for every _L / _R bone whose partner exists; returns how many were added. */
	int32 AutoMirrorPairs();

	UFUNCTION(BlueprintPure, Category = "OK Animation")
	TArray<FName> GetBoneNames() const;
};

/** Maps a non-OK skeleton (Unreal Mannequin, 3ds Max Biped, Mixamo...) onto an OK family. */
UCLASS(BlueprintType, hidecategories = Object)
class OKANIMATION_API UOKRetargetProfile : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Retarget")
	TObjectPtr<UOKSkeletonFamily> Family;
	/** Source bone -> family bone. Bones not listed fold into the next mapped bone below them. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Retarget")
	TMap<FName, FName> BoneMap;
	/** Align this skeleton's rest to the family rest (A-pose / T-pose differences). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Retarget")
	bool bAlignRest = true;
	/** Source hips bone (family hips: cf_j_hips). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Retarget")
	FName FamilyHips = TEXT("cf_j_hips");

	/** Fills BoneMap from a built-in preset ("UE5Mannequin", "UE4Mannequin", "Biped", "Mixamo", "KK"). */
	UFUNCTION(BlueprintCallable, Category = "OK Animation")
	bool ApplyPreset(FName Preset);
	/** Best preset for a skeleton (by matching bone names), NAME_None if none matches. */
	static FName DetectPreset(const TArray<FName>& BoneNames);
	static const TMap<FName, FName>* GetPreset(FName Preset);
	static TArray<FName> GetPresetNames();
};

/** One stored pose: family bones and their final rotations (OK animation deltas), the hips offset when stored. */
USTRUCT(BlueprintType)
struct OKANIMATION_API FOKPose
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pose")
	FName Name;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pose")
	TArray<FName> Bones;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pose")
	TArray<FQuat> Rotations;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pose")
	bool bHasHips = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pose")
	FVector Hips = FVector::ZeroVector;
};

/** A library of poses (hand shapes, faces of the body, key poses) to store from and apply to OK animations. */
UCLASS(BlueprintType, hidecategories = Object)
class OKANIMATION_API UOKPoseLibrary : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Poses")
	TArray<FOKPose> Poses;

	int32 FindPose(FName Name) const { return Poses.IndexOfByPredicate([Name](const FOKPose& P) { return P.Name == Name; }); }
};

UCLASS(config = Game, defaultconfig, meta = (DisplayName = "OK Animation"))
class OKANIMATION_API UOKAnimationSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UPROPERTY(config, EditAnywhere, Category = "Defaults")
	TSoftObjectPtr<UOKSkeletonFamily> DefaultFamily;
	UPROPERTY(config, EditAnywhere, Category = "Defaults")
	TSoftObjectPtr<UOKRetargetProfile> DefaultUnrealProfile;
	UPROPERTY(config, EditAnywhere, Category = "Defaults", meta = (ClampMin = "1", ClampMax = "240"))
	float ImportFps = 30.f;
	/** Where batch conversions write their assets. */
	UPROPERTY(config, EditAnywhere, Category = "Defaults")
	FString AssetRoot = TEXT("/Game/OKAnimation");
	/** Dropping / importing an .fbx makes OK animations of its animations (the FBX import runs first, then the conversion).
	 *  Off: .fbx files import as usual (use Import FBX as OK Animation, or the OKBatchConvert -Fbx= commandlet). */
	UPROPERTY(config, EditAnywhere, Category = "Import")
	bool bImportFbxAsOK = false;

	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }
};
