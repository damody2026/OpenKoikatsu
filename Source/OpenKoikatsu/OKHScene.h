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
    // Talk while it happens (the street's free talk, OKStreetSocialContacts.inl). What he says goes out at once, or,
    // while she cannot answer (moving faster than 2x), is held and goes out at her climax; her answer shows here and is
    // spoken on her (the street plays it, then her breathing comes back).
    void Say(const FString& Text);
    bool TakeLine(FString& Text,FString& Context,bool& System);
    // His words from elsewhere (the microphone): false when she cannot answer now (held for her climax).
    bool Hear(const FString& Text,FString& Context);
    void ShowLine(const FString& Text);
    void SpokeLine(){bTalkSpoken=true;}
    UFUNCTION() void OnInputCommitted(const FText& Text,ETextCommit::Type Commit);

    enum EKind : int32 {
        TabHand=10,TabMouth=11,TabBreast=12,TabSonyu=20,TabClothes=30,TabAibu=40,
        AibuBase=500,
        TouchChest=2101,TouchCrotch=2102,TouchAnal=2103,TouchButt=2104,TouchNipple=2105,Kiss=2106,Release=2107,
        PoseBase=1000,
        Faster=2001,Slower=2002,Toggle=2003,Inside=2004,Outside=2005,Drink=2006,Vomit=2007,Again=2008,Insert=2009,InsertAnal=2010,Pull=2011,Condom=2012,Stop=2014,Continue=2015,ToggleMale=2016,LockHer=2017,LockHis=2018,
        ClothesBase=3000,
    };
private:
    // Caress (AnimationInfo_00): her own animation, him hidden (HAibu); each state is prepared up front.
    struct FAibuPose {FString Name,Source;int32 Layer=0,Id=-1;};
    TArray<FAibuPose> AibuPoses;
    TMap<FString,FString> SoloKeys;      // state -> clip key of the active caress pose
    FString SoloState;double SoloStarted=0.;
    int32 ActiveAibu=INDEX_NONE,PendingAibu=INDEX_NONE,AibuGeneration=0,TouchArea=INDEX_NONE;
    float AreaSeconds[6]={};            // kiss, chest, crotch, anal, butt, nipples (hAreaExps order)
    bool bAibuLoaded=false;
    void LoadAibuPoses();
    void SelectAibu(int32 Index);
    void PlaySolo(const FString& State);
    void Touch(int32 Area);
    float AreaExperience(int32 Area) const;
    void LoadPoses();
    void BuildList();
    void BuildActions();
    void SelectPose(int32 PoseIndex);
    void Want(const FString& State);
    void Chain(const TArray<FString>& States);
    bool Has(const FString& State) const;
    FString P(const FString& State) const {return bAnalPlay?TEXT("A_")+State:State;}
    void Finish(const TCHAR* Kind);
    void Note(const FString& Text,bool bReact=true);
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
    UPROPERTY() TObjectPtr<class UEditableTextBox> Input;
    UPROPERTY() TObjectPtr<UTextBlock> Caption;
    struct FHLine {FString Text,Note;bool System=false;};
    TArray<FHLine> Outbox;
    TArray<FString> Held;
    bool bTalkSpoken=false;
    // 0.5x to 4x while moving; past 2x she has no breath to answer.
    float PlaybackSpeed() const {return .5f+3.5f*SpeedValue;}
    bool CannotAnswer() const;
    void ReleaseHeld();
    // Her eyes and brows follow the moment (state, her gauge, how used to it she is), as the original's H face lists do.
    void UpdateFace();
    FString FaceKey;
    // A new pose frames her from the side of the line between them, turned to her front (his back no longer fills the
    // view); he can be hidden as in the original. The player's own orbit stays until the next pose.
    void FrameCamera();
    FString FramedKey;double FrameAt=0;bool bMaleHidden=false;
    TSet<int32> Offered; // the action kinds BuildActions shows now
    // The original's gauge locks: a locked gauge stops short of climax (his below the finish choices in a service, so
    // nothing happens by itself); he still finishes with the buttons.
    bool bLockHer=false,bLockHis=false;
    // A moment she reacts to in her own words (her character, how she feels now): sent as a game note, the latest only.
    void React(const FString& Event);
    void CaressBreath();
    FString Context() const;
    FString StateWords(const FString& Base) const;
    bool bRawAsked=false,bRawPressed=false;
    int32 HisInside=0,HisOutside=0,HisMouth=0,Swallowed=0;
    TArray<FOKHPose> Poses;
    int32 Tab=TabSonyu,ActivePose=INDEX_NONE,PendingPose=INDEX_NONE;
    int32 Mode=-1;              // of the active pose: 0 caress, 1 service, 2 intercourse
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
