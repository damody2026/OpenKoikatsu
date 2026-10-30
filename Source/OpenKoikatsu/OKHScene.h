#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "Dom/JsonObject.h"
#include "OKHScene.generated.h"

// The street one-on-one as the original H scene (HSceneProc / HSprite, D:\KoikatuSunshine\H_scene.md): a menu of the
// authored poses by mode (service by hand, mouth, breasts; intercourse), each pose's state flow (Idle → Insert →
// InsertIdle → WLoop/SLoop → OLoop → finish → after), both gauges, speed, condom, the finish choices, her clothes; what
// happened is counted for her experience (Tools/ok-rules/src/hexp.rs). Replaces the card reader's test panel when the
// street opens the scene (UOKCharacterController::bHScene).
class UOKCharacterController;
class UOKHScenePanel;
class UTextBlock;
class UVerticalBox;
class UWrapBox;
class UProgressBar;
class USlider;
class UScrollBox;

UCLASS()
class OPENKOIKATSU_API UOKHSceneButton : public UButton {
    GENERATED_BODY()
public:
    int32 Kind=INDEX_NONE;
    UPROPERTY() TObjectPtr<UOKHScenePanel> Owner;
    UFUNCTION() void HandleClick();
};

// One authored pose (Content/IndependentResources/StreetSocialData/h_poses.json, from ok-hposes) on the pair list.
struct FOKHPose {
    int32 Pair=INDEX_NONE;      // index in the controller's NativePairs
    int32 Mode=0;               // 1 service, 2 intercourse (AnimationInfo_XX)
    int32 KindHoushi=0;         // service: 0 hand, 1 mouth, 2 breasts
    bool Anal=false;
    int32 IsExperience=0;       // 1: needs her used to it (慣れ)
    int32 StateRestriction=0;
    FString Name,Raw;          // shown (OKMotionLabelDisplay) and authored
};

UCLASS()
class OPENKOIKATSU_API UOKHScenePanel : public UUserWidget {
    GENERATED_BODY()
public:
    UPROPERTY() TObjectPtr<UOKCharacterController> Gallery;
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void NativeTick(const FGeometry& Geometry,float Delta) override;
    void Click(int32 Kind);
    // What happened, for h_session_end (counts) and her memory (poses, events).
    TSharedRef<FJsonObject> Result() const;
    // The control channel's view (/hscene): pose, state, gauges, the buttons now offered (kind and label), the list.
    TSharedRef<FJsonObject> Describe() const;
    bool SelectPoseNamed(const FString& Name);
    void SetSpeed(float Value);

    enum EKind : int32 {
        TabHand=10,TabMouth=11,TabBreast=12,TabSonyu=20,TabClothes=30,
        PoseBase=1000,
        Faster=2001,Slower=2002,Toggle=2003,Inside=2004,Outside=2005,Drink=2006,Vomit=2007,Again=2008,Insert=2009,InsertAnal=2010,Pull=2011,Condom=2012,Stop=2014,Continue=2015,
        ClothesBase=3000,
    };
private:
    void LoadPoses();
    void BuildList();
    void BuildActions();
    void SelectPose(int32 PoseIndex);
    void Want(const FString& State);
    void Chain(const TArray<FString>& States);
    bool Has(const FString& State) const;
    FString P(const FString& State) const {return bAnalPlay?TEXT("A_")+State:State;}
    void Finish(const TCHAR* Kind);
    void Note(const FString& Text);
    void ApplySpeed();
    FString Current() const;
    int32 Level() const;

    UPROPERTY() TObjectPtr<UTextBlock> Title;
    UPROPERTY() TObjectPtr<UTextBlock> Status;
    UPROPERTY() TObjectPtr<UTextBlock> Hint;
    UPROPERTY() TObjectPtr<UProgressBar> HerGauge;
    UPROPERTY() TObjectPtr<UProgressBar> HisGauge;
    UPROPERTY() TObjectPtr<USlider> Speed;
    UPROPERTY() TObjectPtr<UWrapBox> Actions;
    UPROPERTY() TObjectPtr<UWrapBox> Tabs;
    UPROPERTY() TObjectPtr<UScrollBox> List;
    TArray<FOKHPose> Poses;
    int32 Tab=TabSonyu,ActivePose=INDEX_NONE,PendingPose=INDEX_NONE;
    int32 Mode=0;               // of the active pose
    FString Wanted,LastState,ActionsKey;
    TArray<FString> Queue;      // states still to play, in order (a finish, the insertion)
    double StateSeen=0.,LastRequest=0.,OralIdleSince=0.;
    float Her=0.f,His=0.f,SpeedValue=.35f;
    bool bStrong=false,bAnalPlay=false,bCondom=true,bInsertedNow=false,bInsideFinished=false,bAnalRefused=false,bAnalAsked=false;
    // Counts for her experience.
    bool bPlayed=false,bService=false,bRaw=false,bCrotch=false,bAnal=false;
    int32 ServiceFinishes=0,CrotchClimaxes=0,AnalClimaxes=0,HerOrgasms=0,HisFinishes=0;
    TArray<FString> PosesPlayed,Events;
};
