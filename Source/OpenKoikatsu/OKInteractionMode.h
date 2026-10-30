#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "OKBodyInteraction.h"
#include "OKBodyGesture.h"
#include "OKInteractionMode.generated.h"

class UOKCharacterController;
class AKKPreviewActor;
class APlayerController;
class FJsonObject;

// One in-world lifetime for both entry points. No browser, process or IPC ownership.
UCLASS()
class OPENKOIKATSU_API AOKInteractionMode : public AActor {
    GENERATED_BODY()
public:
    AOKInteractionMode();
    static AOKInteractionMode* Enter(UOKCharacterController* Controller);
    static AOKInteractionMode* EnterReader(UOKCharacterController* Source);
    static AOKInteractionMode* EnterCards(APlayerController* Player,const FString& Female,const FString& Male,int32 FemaleOutfit=0); // FemaleOutfit: the outfit she wears (the street's)
    virtual void Tick(float Delta) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    void Exit(bool Cancelled=false);
    // The street's one-on-one: the H scene panel for her (name, relation stage, ok-rules h_experience answer).
    void BeginHScene(const FString& Name,const FString& Stage,const TSharedPtr<FJsonObject>& Experience,const FString& Player);
    // The control channel's /hscene: {kind} presses a panel button, {pose} picks a pose by name, {speed} sets it; answers the panel's view.
    TSharedPtr<FJsonObject> HSceneControl(const TSharedPtr<FJsonObject>& Body,FString& Error);
    // Talk in the H scene through the street (UOKHScenePanel::Say): his next line and what she is in the middle of;
    // her line to show; her spoken line on her high model.
    bool HSceneTakeLine(FString& Text,FString& Context,bool& System);
    bool HSceneHear(const FString& Text,FString& Context);
    void HSceneShowLine(const FString& Text);
    bool HSceneSpeak(const FString& Path);
    bool HSceneSpeaking() const;
    bool IsReady() const {return Ready;}
    bool IsFinished() const {return Finished;}
    FString Error;
    FString ReturnLabel=TEXT("返回人物編輯器");
    FString ReportPath;
    double AutoReturnSeconds=0;
    TFunction<void(TSharedRef<FJsonObject>)> Returned;
    UPROPERTY() TObjectPtr<UOKCharacterController> Controller;
private:
    void LoadFemale(const FString& Path,int32 Outfit=0);
#if !UE_BUILD_SHIPPING
    bool TickFeatureAudit();
    int32 FeatureAuditStage=0,FeatureAuditPair=INDEX_NONE;
    double FeatureAuditAt=0.;
#endif
    void Fail(const FString& Why);
    UPROPERTY() TObjectPtr<AKKPreviewActor> LoadingFemale;
    UPROPERTY() TObjectPtr<AActor> PreviousView;
    FString MalePath;
    TSharedPtr<FThreadSafeBool,ESPMode::ThreadSafe> Cancelled;
    double Started=0,ReadyAt=0;
    bool OwnController=false,Activated=false,Ready=false,Finished=false,Exiting=false;
    bool PreviousPaint=false,PreviousMouse=false,PreviousTouch=false;
    FOKBodyInteraction PreviousBodyInteraction;
    FOKBodyGesture PreviousBodyGesture;
    struct FSourceAudioState {TWeakObjectPtr<class UAudioComponent> Audio;bool Paused=false;};
    TArray<FSourceAudioState> SourceAudio;
    FVector OriginalPosition=FVector::ZeroVector;
    TSharedPtr<FJsonObject> Evidence;
    struct FSourceActorState {TWeakObjectPtr<AActor> Actor;bool Hidden=false,Collision=false,Tick=false;};
    TArray<FSourceActorState> SourceActors;
    TSharedPtr<class SWidget> PreparingOverlay;
    // The two characters of this one-to-one scene use PhysX secondary dynamics for its whole duration (solo motions
    // too, not only paired playback); released on Exit. A replaced partner is added when it appears.
    TArray<TWeakObjectPtr<class AKKPreviewActor>> PairPhysicsActors;
    void UpdatePairPhysics();
};
