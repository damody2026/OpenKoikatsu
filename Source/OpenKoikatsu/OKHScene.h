#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "Dom/JsonObject.h"
#include "Commandlets/Commandlet.h"
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
    int32 Prop=0;              // what the private room adds for it: 0 none (floor), 1 seat, 2 table, 3 wall
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
        Faster=2001,Slower=2002,Toggle=2003,Inside=2004,Outside=2005,Drink=2006,Vomit=2007,Again=2008,Insert=2009,InsertAnal=2010,Pull=2011,Condom=2012,Stop=2014,Continue=2015,ToggleMale=2016,LockHer=2017,LockHis=2018,MaleBottom=2019,
        ClothesBase=3000,RoomBase=3200,
    };
private:
    // Caress (AnimationInfo_00): her own animation, him hidden (HAibu); each state is prepared up front.
    struct FAibuPose {FString Name,Source;int32 Layer=0,Id=-1,Prop=0;};
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
    // She has come as many times as she will this scene (climax_limit): she wants it to stop, the panel offers no more
    // touching, moving or entering her.
    bool HadEnough() const;
    void CountHerOrgasm();

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
    // Her cheeks flush with her excitement (the face's blush overlay, ChangeHohoAkaRate's _overcolor2 alpha = lerp(0, .2, rate)):
    // it rises with her gauge and fades slowly after, so she is still flushed after she comes. Her card's own blush is the floor.
    void UpdateBlush(float Delta);void RestoreBlush();
    // Semen on her (ChaControl siruLv: face, front top, front bottom, back top, back bottom; 0..2), written to her materials'
    // _liquid* as the original does every frame; where it lands follows the finish and which side of her he is on. It is
    // cleared when the scene ends, as the original cleans her up after H.
    int32 Siru[5]={0,0,0,0,0};void AddSiru(int32 Area);void ApplySiru();int32 SiruSide() const;
    // Her materials that draw the liquid: component, parent, the liquid textures bound (diagnostic in /hscene).
    TArray<FString> LiquidReport() const;float SiruRefresh=0.f;
    TWeakObjectPtr<class AKKPreviewActor> BlushFor;float Blush=-1.f,BlushShown=-1.f;bool bBlushLogged=false;
public:
    bool RefuseWhenEnough();void BareChest();bool bToldSpent=false;
    void ClearLine();
    // Her line asked for before a climax, a finish or a new pose comes back after it ("don't stop" once he had come):
    // it is not shown or spoken. LineEpoch moves at those; SentEpoch is the epoch her last line was asked in.
    int32 LineEpoch=0,SentEpoch=0;bool bStaleLine=false;
    // When he or she last came (Finish) and who (0 he, 1 she, 2 both): the words for the state until the after pose.
    double JustCameAt=0.;int32 JustCame=0;
    bool LineIsStale() const {return bStaleLine;}
    FString JudgeFacts() const; // for the H talk judge: the pose, what is happening, the poses she could be asked into
    bool ApplyTalk(const TSharedPtr<FJsonObject>& Verdict); // its verdict done as the buttons would; true: the scene ends
    void LookAtFace(float Distance);
    void LookAtCrotch(float Distance,float Pitch=15.f,float Turn=0.f); // /hscene {"crotch_cam":cm} or {distance, pitch, turn}: where they join, from beside her body // /hscene {"face_cam":cm}: the camera in front of her face, to see what is on it
    void SetSiruTest(const int32 (&Levels)[5]); // /hscene {"siru":[...]}: set her liquid levels directly
    float BlushTest=-1.f; // /hscene {"blush":v}: hold her blush at v (0..1) to compare shots; -1 follows her
private:
    UPROPERTY() TArray<TObjectPtr<class UMaterialInstanceDynamic>> BlushMats; TArray<float> BlushBase;
    FString FaceKey;
    // A new pose frames her from the side of the line between them, turned to her front (his back no longer fills the
    // view); he can be hidden as in the original. The player's own orbit stays until the next pose.
    void FrameCamera();
    // A wall between the camera and them is hidden for the shot (after the camera moves and after props are placed).
    void HideWallsInTheWay();
    FString FramedKey;double FrameAt=0;bool bMaleHidden=false;
    // How he is drawn (the original's male display): 0 as he is, 1 one see-through colour (her and the penis seen through
    // him), 2 hidden. His bottom comes off for the scene (the penis shows) unless the player puts it back on.
    // See-through by default: where his penis is in her shows (the original's silhouette).
    int32 MaleView=1;bool bMaleBottomOff=true;TWeakObjectPtr<class AKKPreviewActor> MaleLookFor;
    UPROPERTY() TObjectPtr<class UMaterialInstanceDynamic> MaleGhost;
    UPROPERTY() TObjectPtr<class UMaterialInstanceDynamic> PenisXRay; // over her while he is see-through (M_PreviewXRay)
    UPROPERTY() TArray<TObjectPtr<class UMeshComponent>> MaleGhostComps;
    UPROPERTY() TArray<TObjectPtr<class UMaterialInterface>> MaleGhostMats; // their own materials, slot by slot, to put back
    UPROPERTY() TArray<TObjectPtr<class UMeshComponent>> MaleGhostOutlines; // his outline shells, hidden while see-through
    void ApplyMaleLook();
    // The pair's clips are made for standard bodies: with her (or him) taller or shorter the penis misses her or goes
    // through her (the original too). While he is in her, he is moved so the line of his penis passes through her entrance
    // and the entrance stays along its length; the thrust along it is the clip's. His actor stays on her origin (the pair
    // player requires one origin): the offset is on the parts under his root.
    void AlignHim(float Delta);
    // Where his glans comes out of her skin (a thin girl, a clip made for a thicker one), her skin swells there instead of
    // letting it come through: of the belly.bulge morphs made on her body (ok-extract belly_bulge.rs, a grid over her pubic
    // mound, groins, belly and back), the one that moves that skin most, out by how far he is past it. Her session tells
    // (BellyBulgePast). Tip null: he is not in her, all ease off.
    void BulgeBelly(class AKKPreviewActor* Girl,const FVector* Tip,float Off,float Delta);
    // His glans and how far it is out of her skin, read from both sessions when the pair playback says they are idle
    // (AKKPairedPlayback::OnSessionsIdle); without a playback, AlignHim and BulgeBelly read them themselves.
    void ReadPairSessions();void ReadBulge(class AKKPreviewActor* Girl,const FVector& Tip);
    TWeakObjectPtr<class AKKPairedPlayback> ReadingPlayback;FDelegateHandle ReadingHandle;
    TMap<FName,float> Bulge;int32 BulgePart=INDEX_NONE;FName BulgeTarget;float BulgePast=0.f,BulgeMoves=0.f;bool bBulgeLogged=false;TWeakObjectPtr<class AKKPreviewActor> BulgedHer;
    // The girl whose shoes were taken off on coming into her room (NativeTick): once each, she may put them back on.
    TWeakObjectPtr<class AKKPreviewActor> ShoesOffFor;
    // His glans from his penis root, from his session (the end of his penis mesh; J_dan109 draws in as he goes in): kept
    // between reads.
    FVector GlansFromRoot=FVector::ZeroVector;bool bGlans=false;
    FVector HimOffset=FVector::ZeroVector,HimApplied=FVector::ZeroVector;FString AlignedKey;bool bAlignLogged=false;TWeakObjectPtr<class AKKPreviewActor> AlignedHim;
    // The taller one bends the knees instead of sinking through the floor or floating over it: he is lowered with his feet
    // held where they were (Studio IK foot targets); when he would have to rise, she is lowered the same way.
    FVector HerOffset=FVector::ZeroVector,HerApplied=FVector::ZeroVector;TWeakObjectPtr<class AKKPreviewActor> AlignedHer;
    int32 FeetWait=0;bool bFeetRead=false,bHisStands=false,bHerStands=false,bHisIk=false,bHerIk=false;FVector HisFeet[2],HerFeet[2],HisHeld[2],HerHeld[2];
    void MoveParts(class AKKPreviewActor* A,const FVector& Want,FVector& Applied);void HoldFeet(class AKKPreviewActor* A,bool bStands,const FVector& Offset,const FVector (&Feet)[2],FVector (&Kept)[2],bool& bIk);
    // The pair solves its next frame in the background (ok.Anim.AsyncPair): wait for it before touching either session.
    void SyncPair();
    void RestoreAlign();
    float AlignFoot=0.f,AlignPushed=0.f,AlignDeep=0.f;
    float FeetMin[2]={0,0},FeetMax[2]={0,0},FeetShown[4]={0,0,0,0}; // his and her lower ankle over the floor, range in the last two seconds // his lower ankle above her floor (cm), for /hscene
    float AlignOff=-1.f,AlignAt=0.f,AlignLen=0.f,AlignTip=-1.f; // the last measure while he is in her (cm off the penis line, % of its length), for /hscene
    float DeepAlong=1e9f; // the deepest point of his thrusts lately (entrance distance from the root along the penis), easing back
    float AtMin=0.f,AtMax=0.f,AtShownMin=0.f,AtShownMax=0.f,AtWindow=0.f; // depth range over the last two seconds, for /hscene
    TSet<int32> Offered; // the action kinds BuildActions shows now
    // The original's poses on a chair, a desk or at a wall need that thing: the room puts a plain one where the pose
    // has her (or him) lean, sit or lie, measured from their bones once the pose stands (PlaceProps).
    UPROPERTY() TArray<TObjectPtr<class AStaticMeshActor>> Props;bool bPropsTable=false; // a table she leans on goes down with her
    FString PropKey;double PropAt=0;
    // A new pose or place jumps them, the props and the camera at once: the screen goes black at the change and fades in
    // once the new pose plays (the pair restarted, at most three seconds), as the original fades its position changes.
    bool bCovered=false,bEntered=false;double CoverAt=0;float CoverElapsed=0;TWeakObjectPtr<class AKKPairedPlayback> CoverPlayback;FString CoverClip,LastPoseKey;bool bPlaceOnly=false;
    // What the pair played on the previous tick: a cached pose is committed in the click itself, before this tick sees it.
    TWeakObjectPtr<class AKKPairedPlayback> SeenPlayback;FString SeenClip;float SeenElapsed=0;
    bool NewPosePlays(double Now) const;void Cover(bool bOn,double Now);
    void PlaceProps(int32 Kind);
    // Where they are (the original's map choice): 0 the floor, 1 a bed under them in the floor poses.
    int32 Room=0;
    void ClearProps();
    virtual void NativeDestruct() override;
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
    bool bStrong=false,bAnalPlay=false,bCondom=true,bInsertedNow=false,bInsideFinished=false,bAnalRefused=false,bAnalAsked=false,bServiceRefused=false,bSaidEnough=false; // bServiceRefused: she has served him as many times as she will this scene (service_rounds)
    // Counts for her experience.
    bool bPlayed=false,bService=false,bRaw=false,bCrotch=false,bAnal=false;
    int32 ServiceFinishes=0,CrotchClimaxes=0,AnalClimaxes=0,HerOrgasms=0,HisFinishes=0;
    TArray<FString> PosesPlayed,Events;
};

// The see-through material for his penis inside her (M_PreviewXRay): the translucent preview parent with the depth
// test off, drawn over her as an overlay. Run once in the editor: -run=OKBuildHXRayMaterial (PreviewMaterials is cooked).
UCLASS()
class OPENKOIKATSU_API UOKBuildHXRayMaterialCommandlet : public UCommandlet {
    GENERATED_BODY()
public:
    virtual int32 Main(const FString& Params) override;
};
