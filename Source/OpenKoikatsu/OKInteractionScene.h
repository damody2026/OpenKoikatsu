#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Blueprint/UserWidget.h"
#include "KKSpeech.h"
#include "HAL/IConsoleManager.h"
#include "OKInteractionScene.generated.h"

class UOKCharacterController;
class UPanelWidget;
class UPanelSlot;
class UVerticalBox;
class UCheckBox;
class UButton;
class USlider;
class UTextBlock;
class UComboBoxString;
class UEditableTextBox;
class UStaticMeshComponent;

USTRUCT()
struct FOKInteractionPanel {
    GENERATED_BODY()
    UPROPERTY() TObjectPtr<UPanelWidget> Parent;
    UPROPERTY() TArray<TObjectPtr<UWidget>> Children;
    UPROPERTY() TArray<TObjectPtr<UPanelSlot>> SlotTemplates;
};

UCLASS()
class OPENKOIKATSU_API AOKInteractionScene : public AActor {
    GENERATED_BODY()
public:
    AOKInteractionScene();
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void Tick(float Delta) override;
    TUniquePtr<TGuardConsoleVariable<int32>> DebugPoseGuard;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> Floor;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> Backdrop;
    TArray<uint64> FrameHistogram;
    uint64 FrameSamples=0,SlowFrames=0;
    double FrameTotal=0.,FrameMaximum=0.;
    double PreviousFrame=0.;
    double GameTotal=0.,RenderTotal=0.,RHITotal=0.,GPUTotal=0.,WaitTotal=0.,SwapTotal=0.;
    void ResetPerformance();
    FString PerformanceSummary() const;
    void SaveReport() const;
#if !UE_BUILD_SHIPPING
    TFunction<void()> ControlProfileTick;
#endif
};

UCLASS()
class OPENKOIKATSU_API UOKInteractionWidget : public UUserWidget {
    GENERATED_BODY()
public:
    UPROPERTY() TObjectPtr<UOKCharacterController> Gallery;
    UPROPERTY() TObjectPtr<AOKInteractionScene> Scene;
    UPROPERTY() TArray<FOKInteractionPanel> Panels;
    UPROPERTY() TObjectPtr<UVerticalBox> Controls;
    UPROPERTY() TObjectPtr<UCheckBox> HeadCamera;
    UPROPERTY() TObjectPtr<UCheckBox> AutomaticMouth;
    UPROPERTY() TObjectPtr<UCheckBox> BodyTouch;
    UPROPERTY() TObjectPtr<UTextBlock> BodyTouchStatus;
    UFUNCTION() void OnBodyTouch(bool Enabled);
    TWeakObjectPtr<class AKKPreviewActor> BodyPrepareAttempt;
    FString BodyPreparationError;
    UPROPERTY() TObjectPtr<UButton> BodyIdle;
    FString BodyIdleKey,BodyIdlePairKey;
    TWeakObjectPtr<class AKKPreviewActor> BodyIdleOwner;
    int32 BodyIdleGeneration=0;
    void PrepareBodyIdle();
    UFUNCTION() void OnBodyIdle();
    UPROPERTY() TObjectPtr<USlider> Belly;
    UPROPERTY() TObjectPtr<USlider> SoloSpeed;
    UPROPERTY() TObjectPtr<UTextBlock> Performance;
    UPROPERTY() TObjectPtr<UComboBoxString> ReadyMotionCombo;
    UPROPERTY() TObjectPtr<UEditableTextBox> MotionSearch;
    UPROPERTY() TObjectPtr<UTextBlock> MotionPreparation;
    UPROPERTY() TObjectPtr<UButton> SpeakButton;
    TArray<FString> ReadyMotionKeys;
    FString MotionSource;
    int32 MotionGeneration=0,MotionSelectionGeneration=0,MotionPage=0;
    double MotionSearchAt=0.;
    TSharedPtr<FThreadSafeBool,ESPMode::ThreadSafe> MotionCancelled;
    void PrepareMotions();
    UFUNCTION() void OnMotionSource(FString Item,ESelectInfo::Type Type);
    UFUNCTION() void OnReadyMotion(FString Item,ESelectInfo::Type Type);
    UFUNCTION() void OnMotionSearch(const FText& Text);
    UFUNCTION() void OnMotionPrevious();
    UFUNCTION() void OnMotionNext();
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void NativeTick(const FGeometry& Geometry,float Delta) override;
    void RestoreControls();
    UFUNCTION() void OnReturn();
    UFUNCTION() void OnHeadCamera(bool Enabled);
    UFUNCTION() void OnBelly(float Value);
    UFUNCTION() void OnAutomaticMouth(bool Enabled);
    UFUNCTION() void OnSoloSpeed(float Value);
    UFUNCTION() void OnLoadPartner();
    UPROPERTY() TObjectPtr<UEditableTextBox> PartnerPath;
    UFUNCTION() void OnPause();
    UFUNCTION() void OnReplay();
    UFUNCTION() void OnSpeak();
    UFUNCTION() void OnStopSpeech();
    double NextPerformanceUpdate=0.;
    int32 SpeechGeneration=0;
    int32 SpeechPreparationGeneration=0;
    int32 SpeechCatalogGeneration=0;
    TSharedPtr<FThreadSafeBool,ESPMode::ThreadSafe> SpeechCancelled;
    TSharedPtr<FThreadSafeBool,ESPMode::ThreadSafe> SpeechCatalogCancelled;
    TArray<FString> SpeechCacheOrder;
    TMap<FString,TSharedPtr<const FKKSpeechPcm,ESPMode::ThreadSafe>> SpeechCache;
    int64 SpeechCacheBytes=0;
    void PreloadSpeech();
    void PrepareSpeechCatalog(const FString& Root,int32 Personality,float Pitch);
    void CacheSpeech(const FString& Path,TSharedPtr<const FKKSpeechPcm,ESPMode::ThreadSafe> Pcm);
    UFUNCTION() void OnSpeechSelection(FString Item,ESelectInfo::Type Type);
};
