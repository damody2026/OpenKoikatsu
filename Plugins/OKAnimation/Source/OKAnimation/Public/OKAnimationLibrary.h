#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "OKAnimationLibrary.generated.h"

class AKKPreviewActor;
class AKKPairedPlayback;
class AOKGroupPlayer;
class UAnimSequence;
class UOKAnimationAsset;
class UOKAnimSequence;
class UOKGroupAnimSequence;
class UOKRetargetProfile;
class UOKSkeletonFamily;

UCLASS()
class OKANIMATION_API UOKAnimationLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** KK clip (empty: the clip the character is playing) -> OK animation. Outer null = transient. */
	UFUNCTION(BlueprintCallable, Category = "OK Animation|Convert")
	static UOKAnimSequence* ConvertKKClip(AKKPreviewActor* Character, const FString& ClipKey, float Fps, UObject* Outer, FName Name, FString& Error);
	/** The KK paired state playing on a pair -> OK group (two role animations and their detected contacts). */
	UFUNCTION(BlueprintCallable, Category = "OK Animation|Convert")
	static UOKGroupAnimSequence* ConvertKKPair(AKKPairedPlayback* Pair, float Fps, UObject* Outer, FName Name, FString& Error);
	/** Unreal animation (any skeleton a profile or a built-in preset maps) -> OK animation. */
	UFUNCTION(BlueprintCallable, Category = "OK Animation|Convert")
	static UOKAnimSequence* ConvertUnrealAnimation(UAnimSequence* Sequence, UOKRetargetProfile* Profile, UOKSkeletonFamily* Family, float Fps, UObject* Outer, FName Name, FString& Error);
	/** A family whose standard rest is this character's unshaped Animator rest. */
	UFUNCTION(BlueprintCallable, Category = "OK Animation|Convert")
	static UOKSkeletonFamily* MakeFamilyFromCharacter(AKKPreviewActor* Character, UObject* Outer, FName Name, FString& Error);

	/** A new animation holding the family's rest pose for Frames frames: a blank start to key from scratch. */
	UFUNCTION(BlueprintCallable, Category = "OK Animation|Convert")
	static UOKAnimSequence* MakeRestAnimation(UOKSkeletonFamily* Family, int32 Frames, float Fps, UObject* Outer, FName Name, FString& Error);

	/** Plays a single-role OK asset on a character (adds an OK animation component when it has none). */
	UFUNCTION(BlueprintCallable, Category = "OK Animation|Play")
	static bool PlayOnCharacter(AKKPreviewActor* Character, UOKAnimationAsset* Animation);
	/** Spawns a group player for an N-role asset on N characters. */
	UFUNCTION(BlueprintCallable, Category = "OK Animation|Play", meta = (WorldContext = "WorldContext"))
	static AOKGroupPlayer* PlayGroup(UObject* WorldContext, UOKAnimationAsset* Animation, const TArray<AKKPreviewActor*>& Characters, FString& Error);

	UFUNCTION(BlueprintCallable, Category = "OK Animation|File")
	static bool SaveOkAnimFile(UOKAnimSequence* Animation, const FString& Path);
	/** A BVH motion capture file -> OK animation (bones matched by a built-in preset: CMU, Mixamo, Biped, mannequins). */
	UFUNCTION(BlueprintCallable, Category = "OK Animation|File")
	static UOKAnimSequence* ImportBvhFile(const FString& Path, UObject* Outer, FName Name, FString& Error);
	static UOKAnimSequence* ImportBvhText(const FString& Text, const FString& SourceName, UObject* Outer, FName Name, FString& Error);
	UFUNCTION(BlueprintCallable, Category = "OK Animation|File")
	static UOKGroupAnimSequence* LoadOkGroupFile(const FString& Path, UObject* Outer, FName Name, FString& Error);
	UFUNCTION(BlueprintCallable, Category = "OK Animation|File")
	static UOKAnimSequence* LoadOkAnimFile(const FString& Path, UObject* Outer, FName Name, FString& Error);
	/** Saved/OKAnim/Live: the editor writes saved OK animations here, a running game reloads what it plays from here. */
	static FString LiveFolder();
	/** Loads the default family (settings), or the built-in KK family when none is set. */
	static UOKSkeletonFamily* GetDefaultFamily();
};
