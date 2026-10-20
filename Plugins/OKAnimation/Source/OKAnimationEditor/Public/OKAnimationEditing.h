#pragma once

#include "CoreMinimal.h"

class UAnimSequence;
class UOKAnimationAsset;
class UOKAnimSequence;
class UOKRetargetProfile;
class USkeleton;
class USkeletalMesh;
class UOKSkeletonFamily;

/** Editor operations on OK animation assets (content browser actions, the OK animation editor, the commandlet). */
class UOKAnimMontage;
class ULevelSequence;
class UOKRetargetProfile;
class USkeleton;
class USkeletalMesh;

namespace OKAnimationEditing
{
	/** A new saved-on-demand asset of Class at PackagePath/Name (unique name), registered with the asset registry. */
	OKANIMATIONEDITOR_API UObject* CreateAsset(UClass* Class, const FString& PackagePath, const FString& Name);
	OKANIMATIONEDITOR_API bool SaveAsset(UObject* Asset);

	/** Unreal animation -> new OK animation asset in DestPath (empty: next to the source). */
	OKANIMATIONEDITOR_API UOKAnimSequence* ConvertUnrealAnimation(UAnimSequence* Sequence, UOKRetargetProfile* Profile, const FString& DestPath, float Fps, FString& OutError);
	/** OK animation -> AnimSequence on Skeleton (bone tracks, contacts as OK Contact notifies, curves) for Unreal's tools. */
	OKANIMATIONEDITOR_API UAnimSequence* ExpandToAnimSequence(UOKAnimSequence* Animation, USkeleton* Skeleton, FString& OutError);
	/** Re-reads the bones, contacts and curves of the AnimSequence it was expanded to. */
	OKANIMATIONEDITOR_API bool WriteBack(UOKAnimSequence* Animation, FString& OutError);
	OKANIMATIONEDITOR_API bool ExportFile(UOKAnimSequence* Animation, const FString& Path, FString& OutError);
	/** Problems found in an asset (an empty list when it is valid). */
	OKANIMATIONEDITOR_API TArray<FString> Validate(const UOKAnimationAsset* Asset);
	/** Other Unreal animation assets -> OK assets in DestPath (their animations converted next to them):
	 *  AnimMontage -> OK montage (one section per composite section, branches kept), BlendSpace / 1D -> OK blend space
	 *  (axes and samples), LevelSequence -> OK montage of its skeletal animation sections in time order. */
	/** Expands an OK animation onto Mesh's skeleton and builds a Level Sequence (next to the asset) with Mesh spawned and
	 *  that animation on it, to edit with Sequencer / Control Rig; opens it when bOpen. */
	OKANIMATIONEDITOR_API ULevelSequence* MakeEditSequence(UOKAnimSequence* Animation, USkeletalMesh* Mesh, bool bOpen, FString& OutError);
	/** An FBX file -> OK animations in DestPath: Unreal's FBX import runs first (mesh and skeleton when the file has them,
	 *  else onto FallbackSkeleton), into DestPath/FbxSource, then every imported animation is converted. */
	OKANIMATIONEDITOR_API TArray<UOKAnimSequence*> ImportFbxAsOk(const FString& File, const FString& DestPath, USkeleton* FallbackSkeleton, FString& OutError);
	OKANIMATIONEDITOR_API UOKAnimationAsset* ConvertUnrealAsset(UObject* Source, UOKRetargetProfile* Profile, const FString& DestPath, float Fps, FString& OutError);
	/** Scores every .okanim under Dir on the family body and a long-legged variant (quality problems weighted by kind and
	 *  length): writes <Dir>/score.json (all) and <Dir>/review.txt (the worst MaxReview). Returns the files scored. */
	OKANIMATIONEDITOR_API int32 ScoreFolder(const FString& Dir, int32 MaxReview, FString& OutSummary);
	/** Montage section timing on the montage timeline (an automatic start becomes explicit); Length <= 0 keeps the animation's. */
	OKANIMATIONEDITOR_API bool SetMontageSectionTiming(UOKAnimMontage* Montage, int32 Section, float Start, float Length);
	OKANIMATIONEDITOR_API bool SetMontageSectionBlend(UOKAnimMontage* Montage, int32 Section, float BlendIn);
	/** The section played after this one (itself = loop; none = the next on the timeline). */
	OKANIMATIONEDITOR_API bool SetMontageSectionNext(UOKAnimMontage* Montage, int32 Section, FName Next);
	/**
	 * A preview mesh for a skeleton family, built from its rest alone: the family bones (helper bones in between are
	 * folded, see the header's "anchor") and one box per bone segment skinned to that bone. Any family can then be
	 * animated in the OK animation editor without a character mesh. Assets go next to the family (or PackagePath).
	 */
	OKANIMATIONEDITOR_API USkeletalMesh* BuildFamilyPreviewMesh(UOKSkeletonFamily* Family, const FString& PackagePath, FString& OutError);
	/** The default family as a saved asset with a preview mesh (created from the built-in KK rest when needed). */
	OKANIMATIONEDITOR_API UOKSkeletonFamily* EnsureDefaultFamilyAsset(FString& OutError, bool bRebuild = false);
	/** Skeleton to expand onto: the profile's family preview mesh, else the default family's. */
	OKANIMATIONEDITOR_API USkeleton* DefaultExpandSkeleton(const UOKAnimSequence* Animation);
}
