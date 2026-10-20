#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameFramework/Actor.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "OKAnimationTypes.h"
#include "OKAnimationPlayers.generated.h"

class FOkAnimPlayer;
class AKKPreviewActor;
class AKKPairedPlayback;
class UOKAnimationAsset;
class UOKGroupAnimSequence;

/**
 * Plays a single-role OK asset (animation, blend space, montage) on the character it is attached to (an
 * AKKPreviewActor). Parameters drive blend spaces; SequenceTime lets Sequencer scrub it with an ordinary float track.
 */
UCLASS(ClassGroup = (OKAnimation), BlueprintType, meta = (BlueprintSpawnableComponent))
class OKANIMATION_API UOKAnimationComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UOKAnimationComponent();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "OK Animation")
	TObjectPtr<UOKAnimationAsset> Animation;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "OK Animation")
	bool bAutoPlay = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OK Animation", meta = (ClampMin = "0"))
	float PlayRate = 1.f;
	/** Blend space parameters by axis name. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "OK Animation")
	TMap<FName, float> Parameters;
	/** Animation time; keyed by Sequencer (Set is called on every evaluation, which poses the character). */
	UPROPERTY(EditAnywhere, Interp, BlueprintReadWrite, BlueprintSetter = SetSequenceTime, Category = "OK Animation")
	float SequenceTime = 0.f;

	/** Plays an asset; with BlendTime > 0 the current animation cross-fades into it. */
	UFUNCTION(BlueprintCallable, Category = "OK Animation")
	bool Play(UOKAnimationAsset* Asset, float BlendTime = 0.f);  // BlendTime < 0: the asset's own fade-in
	/** Montage: jump to a section now / choose the section that follows another (itself = loop it). */
	UFUNCTION(BlueprintCallable, Category = "OK Animation")
	bool JumpToSection(FName Section);
	UFUNCTION(BlueprintCallable, Category = "OK Animation")
	void SetNextSection(FName Section, FName Next);
	UFUNCTION(BlueprintPure, Category = "OK Animation")
	FName GetCurrentSection() const;
	UFUNCTION(BlueprintCallable, Category = "OK Animation")
	void Stop();
	UFUNCTION(BlueprintCallable, Category = "OK Animation")
	void SetParameter(FName Name, float Value);
	UFUNCTION(BlueprintCallable, Category = "OK Animation")
	void SetSequenceTime(float Time);
	UFUNCTION(BlueprintPure, Category = "OK Animation")
	float GetTime() const;
	UFUNCTION(BlueprintPure, Category = "OK Animation")
	bool IsPlaying() const;

	AKKPreviewActor* GetCharacter() const;

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

private:
	void PushParameters();
	TSharedPtr<FOkAnimPlayer> Player;
	/** Montage branching: section ranges and the current "next" of each section. */
	TArray<TPair<float, float>> SectionRanges;
	TArray<FName> SectionNames;
	TArray<float> SectionEntry;
	bool bRootMotion = false;
	float HipsLength = 1.f;
	float LastDriftTime = 0.f;
	/** Playback time at the previous tick (section boundaries are crossed between two ticks). */
	float LastSectionPhase = -1.f;
	TMap<FName, FName> NextOf;
};

/**
 * Plays an N-role OK asset (group, or a blend space / montage of groups) on N characters: places the roles,
 * solves the contacts on their actual bodies (any heights), keeps feet planted and steps when needed. Ticks before
 * the characters, which show the result (PoseMode 4).
 */
UCLASS(BlueprintType)
class OKANIMATION_API AOKGroupPlayer : public AActor
{
	GENERATED_BODY()

public:
	AOKGroupPlayer();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "OK Animation")
	TObjectPtr<UOKAnimationAsset> Animation;
	/** One character per role, in role order. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "OK Animation")
	TArray<TObjectPtr<AKKPreviewActor>> Characters;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OK Animation", meta = (ClampMin = "0"))
	float PlayRate = 1.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "OK Animation")
	TMap<FName, float> Parameters;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "OK Animation")
	bool bAutoPlay = false;
	UPROPERTY(BlueprintReadOnly, Category = "OK Animation")
	FOKFrameReport LastReport;
	/** Worst contact error since the last ResetReport (m). */
	UPROPERTY(BlueprintReadOnly, Category = "OK Animation")
	float PeakContactError = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "OK Animation")
	int32 FramesPlayed = 0;

	UFUNCTION(BlueprintCallable, Category = "OK Animation")
	bool Play(UOKAnimationAsset* Asset, const TArray<AKKPreviewActor*>& InCharacters);
	UFUNCTION(BlueprintCallable, Category = "OK Animation")
	void Stop();
	UFUNCTION(BlueprintCallable, Category = "OK Animation")
	void SetParameter(FName Name, float Value);
	UFUNCTION(BlueprintCallable, Category = "OK Animation")
	void SetTime(float Time);
	UFUNCTION(BlueprintPure, Category = "OK Animation")
	float GetTime() const { return Time; }
	UFUNCTION(BlueprintCallable, Category = "OK Animation")
	void ResetReport() { PeakContactError = 0.f; FramesPlayed = 0; }
	bool IsPlaying() const { return Player.IsValid(); }
	FString LastError;

	virtual void Tick(float DeltaSeconds) override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

private:
	bool Step(float Dt);
	void PushParameters();
	TSharedPtr<FOkAnimPlayer> Player;
	float Time = 0.f;
};

/**
 * A contact authored on an Unreal AnimSequence (an OK animation expanded for editing): the notify range is the
 * contact's active time. Written back into the OK animation's contacts.
 */
UCLASS(meta = (DisplayName = "OK Contact"))
class OKANIMATION_API UOKContactNotifyState : public UAnimNotifyState
{
	GENERATED_BODY()

public:
	/** Start / End come from the notify range. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "OK Contact")
	FOKContact Contact;

	virtual FString GetNotifyName_Implementation() const override;
};
