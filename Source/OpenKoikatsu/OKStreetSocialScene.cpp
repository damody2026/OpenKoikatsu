#include "OKStreetSocialScene.h"
#include "OKStreetSocialText.h"
#include "OKStreetSpokenText.h"
#include "OKStreetSocialSpeakerText.h"
#include "OKStreetSocialSongState.h"
#include "OKInteractionMode.h"
#include "OKStreetSocialRules.h"
#include "OKStreetHeroine.h"
#include "Algo/Reverse.h"
#include "OKStreetSocialInvitations.h"
#include "OKStreetSocialPresentation.h"
#include "OKStreetSocialMinimap.h"
#include "OKStreetSocialNavigation.h"
#include "Widgets/SOverlay.h"
#include "Kismet/GameplayStatics.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboBox.h"
#include "OKStreetSocialTopics.h"
#include "OKStreetSocialDialogueContext.h"
#include "OKStreetSocialFreeTalk.h"
#include "OKStreetSocialControl.h"
#include "HttpServerModule.h"
#include "IHttpRouter.h"
#include "HttpPath.h"
#include "HttpRouteHandle.h"
#include "HttpRequestHandler.h"
#include "HttpResultCallback.h"
#include "HttpServerRequest.h"
#include "HttpServerResponse.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "OKStreetSocialPlaces.h"
#include "OKStreetSocialCalendar.h"
#include "OKStreetMind.h"
#include "OKStreetRules.h"
#include "OKStreetSocialBias.h"
#include "OKStreetSocialChoice.h"
#include "OKStreetSocialEconomy.h"
#include "OKStreetSocialEtiquette.h"
#include "OKStreetSocialInvite.h"
#include "OKStreetSocialSchedule.h"
#include "OKStreetSocialFinanceUI.h"
#include "OKStreetSocialHistory.h"
#include "OKStreetSocialBluff.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "ProceduralMeshComponent.h"
#include "ProceduralMeshConversion.h"
#include "MeshDescription.h"
#include "Engine/StaticMesh.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "OKStreetSocialExpression.h"
#include "OKCrowdRuntimeScene.h"
#include "OKCrowdRoom.h"
#include "HighResScreenshot.h"
#include "KKRuntimeSpeech.h"
#include "OKCrowdScene.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "GameFramework/GameUserSettings.h"
#include "HAL/IConsoleManager.h"
#include "OKHGShading.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/PointLightComponent.h"
#include "InputCoreTypes.h"
#include "Misc/CommandLine.h"
#include "Misc/DateTime.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/StringOutputDevice.h"
#include "HAL/ThreadSafeBool.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "RHI.h"
#include "EngineGlobals.h"
#include "RenderingThread.h"
#include "UnrealClient.h"
#include "Styling/CoreStyle.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Application/IInputProcessor.h"
#include "Input/Events.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SSlider.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Brushes/SlateDynamicImageBrush.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Text/STextBlock.h"
#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/DirectionalLight.h"
#include "Components/DirectionalLightComponent.h"
#include "Engine/SkyLight.h"
#include "Components/SkyLightComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Engine/ExponentialHeightFog.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "EngineUtils.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformStackWalk.h"
#include "Widgets/SWindow.h"

// Preprocess the two mode keys so focused Slate buttons cannot swallow them.
class FOKStreetMouseModeInput final:public IInputProcessor {
public:
 TFunction<bool()> Toggle;
 TFunction<void(FVector2D)> PointerDown;
 virtual void Tick(float,FSlateApplication&,TSharedRef<ICursor>)override{}
 virtual bool HandleKeyDownEvent(FSlateApplication&,const FKeyEvent& Event)override{
  if(!Event.IsRepeat()&&(Event.GetKey()==EKeys::LeftControl||Event.GetKey()==EKeys::RightControl))return Toggle&&Toggle();return false;
 }
 virtual bool HandleMouseButtonDownEvent(FSlateApplication&,const FPointerEvent& Event)override{
  if(Event.GetEffectingButton()==EKeys::LeftMouseButton&&PointerDown)PointerDown(Event.GetScreenSpacePosition());
  return Event.GetEffectingButton()==EKeys::MiddleMouseButton&&Toggle&&Toggle();
 }
};

struct FOKStreetVoicedTopicRecord {
 int32 Slot=INDEX_NONE;
 FString Id,Node,Category,Transaction,AudioCompletion;
 TArray<FString> VoiceIds;
 float FavorBefore=0.f,FavorAfter=0.f,LewdBefore=0.f,LewdAfter=0.f;
};

struct FOKStreetCandidateProbeSnapshot {
 FVector Male=FVector::ZeroVector;
 float Yaw=90.f,Pitch=-12.f,Distance=350.f;
 TArray<FVector> Positions;
 TArray<FRotator> Rotations;
 TArray<uint8> Held,Modes;
 FString Save,SaveDisk;
 bool SaveExisted=false;
 int32 Transactions=0,Favor=0,Lines=0,Voiced=0,Greetings=0,Farewells=0,Errors=0,ExpressionsMissing=0,Branches=0,Refusals=0;
};

// Reusable two-actor reservation/preparation/capture lifecycle. No card-specific resources.
struct FOKStreetPairSession {
 int32 Partner=INDEX_NONE,Phase=0;
 FString MaleClip,FemaleClip,CapturePath;
 // At a meal: the clip each of the two (0 him, 1 her) plays now and the one asked for (eating while there is something
 // of theirs to eat, else seated idle: she ate an empty table while only he had ordered).
 FString MealNow[2],MealWant[2];
 FOKStreetAgentSim SavedAgent;
 FVector SavedMale;
 FRotator SavedMaleRotation,SavedFemaleRotation;
 float SavedYaw=0,SavedPitch=0,SavedDistance=0;
 double RequestedAt=0,PlayingAt=0,CaptureAt=0;
 bool CapturePending=false,FinishRequested=false,SavedMouseUI=false,Seated=false,Drink=false,Bath=false,Read=false,NoProps=false;
 float Yaw=0; // which way the pair layout faces (place anchors); 0 = the authored +X
 bool PhoneUse=false,Splash=false,FemaleThrows=false,Meal=false,Watermelon=false,FemaleStrikes=false,MelonHit=false;
 int32 MelonCycle=-1;float MelonPlayback=0;FVector LastStickTip=FVector::ZeroVector;bool HasStickTip=false;float MelonClosest=FLT_MAX;float MelonAxes[6]={FLT_MAX,FLT_MAX,FLT_MAX,FLT_MAX,FLT_MAX,FLT_MAX};float MelonHandHeight=FLT_MAX;float MelonForward=80;
 bool Conversation() const {return Seated||Drink||Meal||PhoneUse;}
};
// One moving part of an elevator and where it sits in the car's frame.
struct FOKStreetLiftPart {TWeakObjectPtr<AStaticMeshActor> Actor;FVector Local=FVector::ZeroVector;};
// A walk-in elevator (OKStreetSocialElevator.inl): per floor its landing spot, height, opening side and doors.
struct FOKStreetElevator {
 int32 Place=INDEX_NONE;FVector Centre=FVector::ZeroVector,Axis=FVector::ForwardVector,Side=FVector::RightVector;
 TArray<FVector> Landing,Out;TArray<float> StopZ;TArray<FString> Labels;
 float CarZ=0,Door=0;int32 Floor=0,Target=INDEX_NONE,Phase=0;double OpenUntil=0;
 bool MaleRiding=false;TArray<int32> Riders;TArray<bool> DoorBlocked;
 TArray<FOKStreetLiftPart> Car;TArray<TArray<FOKStreetLiftPart>> Doors;
};
// A short scripted walk (into or out of a car) for a girl.
struct FOKStreetLiftWalk {TArray<FVector> Path;FVector At=FVector::ZeroVector;bool WasHeld=false,KeepHeld=false,Started=false;};
// An outing riding up to its spot: call, step in, press, ride, step out.
struct FOKStreetLiftRide {int32 Elevator=INDEX_NONE,Floor=INDEX_NONE,Stage=0;double At=0;FVector Spot=FVector::ZeroVector;bool Descend=false;}; // Descend: down to the street first, then on to Spot
// A kiss or embrace held for a moment, then her reaction (OKStreetSocialLover.inl).
struct FOKStreetLoverPose {int32 Girl=INDEX_NONE;FString After;double Since=0;FString Act;};
// A heroine's own loop while her schedule holds her near him (OKStreetSocialLife.inl), with its chair/table props.
struct FOKLifePose {int32 Actor=INDEX_NONE,Phase=0;FString Clip;double Requested=0;TArray<TWeakObjectPtr<AActor>> Props;};
// A heroine riding a lift to her spot upstairs while he can see her (OKStreetSocialLife.inl).
struct FOKNpcLift {int32 Elevator=INDEX_NONE,From=0,To=0,Stage=0;double At=0;FVector Spot=FVector::ZeroVector;};
struct FOKStreetLoverReact {int32 Girl=INDEX_NONE;FString Clip,MaleClip;int32 Phase=0;double At=0;float Seconds=0;bool WasHeld=false;};
// One grid route search (AOKStreetSocialScene::GridSearchStep): GridRoute runs it to the end; a heroine's walk runs it a
// slice a frame (TickRouteJobs), since a search through cells not yet tested (each a physics overlap) took 20-37 ms.
struct FOKStreetGridSearch {
 FVector From=FVector::ZeroVector,To=FVector::ZeroVector;float Z=0;FIntPoint Start,Goal,Lo,Hi;int32 Phase=0; // 0 not begun, 1 searching, 3 pulling the string, 2 done
 // Pulling: the cells found, the corner kept last (PullI) and the farthest cell still to try from it (PullJ, -1 none yet).
 // One long route's pull took 10 ms in a frame.
 TArray<FIntPoint> Path;int32 PullI=0,PullJ=-1;
 // Over the box Lo..Hi, a cell at (X-Lo.X)+(Y-Lo.Y)*W: its cost, the cell it was reached from, and what is known of it
 // (bit 0 tested, 1 free, 2 learned blocked). Hash maps here cost 11-13 ms for one long route.
 TArray<int32> Came;TArray<float> Cost;TArray<uint8> Cell;int32 W=0;TArray<TPair<float,FIntPoint>> Open;int32 Expanded=0;
 TArray<FVector> Route; // once done: the walk, empty when there is none
};
// A heroine's walk waiting for its route (QueueHeroineWalk). Found: she walks it (to the door to leave a room, or on to
// Final upstairs). None: she is placed at Fallback facing Yaw, or (Leave) set free at the door like before.
struct FOKStreetPendingMove {
 TSharedPtr<FOKStreetGridSearch> Search;FVector Final=FVector::ZeroVector,Fallback=FVector::ZeroVector;bool HasFinal=false,Leave=false;float Yaw=0;FString Place;
};

struct FOKStreetSceneState {
 FOKStreetPairSession Pair,PhotoProbeSession,SeatedProbeSession,PhoneUseProbeSession;
 int32 AmbientActor=INDEX_NONE,AmbientCursor=0,AmbientPhase=0,AmbientAction=0,AmbientNextAction=0;FString AmbientPlaceClip;
 bool AmbientDrinkOnlyTest=false,AmbientSelfieOnlyTest=false,AmbientGroomOnlyTest=false;
 double AmbientRequested=0,AmbientPlaying=0,AmbientDuration=0;FOKStreetAgentSim AmbientSaved;
 TMap<int32,double> AmbientCooldown;TArray<TWeakObjectPtr<AStaticMeshActor>> AmbientProps;
 bool AmbientOnlyTest=false,AmbientChecked=false;
 int32 AmbientVerified=0,AmbientProbePhase=0,AmbientPrepareCancels=0,AmbientInterrupts=0,AmbientSamples=0,AmbientNaturalCompletions=0;
 double AmbientProbeAt=0;FVector AmbientProbeHand=FVector::ZeroVector,AmbientProbePosition=FVector::ZeroVector;FString AmbientProbeSave;
 bool PhoneUseOnlyTest=false,PhoneUseChecked=false,PhoneUseCancelChecked=false;int32 PhoneUseActorsVerified=0,PhoneUseProbePhase=0,PhoneUseSamples=0,PhoneUseVoices=0,PhoneUseProbeVoices=0;double PhoneUseProbeAt=0;FString PhoneUseProbeSave;FVector PhoneUseFirstHand[2];
 bool PresentationOnlyTest=false;int32 PresentationPhase=0,PresentationGirl=1,PresentationStandChecks=0,PresentationStatPhase=0,PresentationTopicButtons=0,DebugSliders=0;double PresentationAt=0;TSet<int32> PresentationRunActors;FOKStreetStore PresentationStore;
 TArray<FOKStreetRelationshipDialogue> RelationshipDialogues;TArray<FOKStreetTopicPart> RelationshipSequence;int32 RelationshipPart=0,RelationshipVoices=0;double RelationshipSilentUntil=0;
 bool ContextOnlyTest=false;int32 ContextPhase=0,ContextGirl=1,ContextCategory=0,ContextChecks=0,ContextConfessions=0,ContextRefusals=0;double ContextAt=0;FString ContextSave,ActivePromptBranch;TArray<TSharedPtr<FJsonValue>> ContextEvidence;
 bool FreeTalkOpen=false,FreeTalkBusy=false,FreeTalkListening=false,FreeTalkAsrBusy=false;FString FreeTalkDraft,FreeTalkNotice;
 // Free talk: per-girl model memory (stable id -> turns), the shared on-screen log, bottom-box captions, replay queue.
 TMap<FString,TArray<FOKFreeTalkTurn>> FreeTalkMemory;TArray<FOKFreeTalkLine> FreeTalkLog;bool FreeTalkLogLoaded=false,FreeTalkHistoryOpen=false;int64 FreeTalkSerial=0;
 // The bottom box keeps the last thing said (gal-game style); control hints wait behind the "?".
 int64 TalkStartMinute=-1; // when the face-to-face talk with Target began (her memory of it, EndTalk)
 FString DialogueText,HintText;int32 DialogueTarget=INDEX_NONE;bool HintOpen=false,HintUnread=false;FLinearColor DialogueColor=FLinearColor::White;FString DigestedMessage;int32 DigestedTarget=INDEX_NONE,DigestedNode=INDEX_NONE;
 // Street clock: real local time at start, 24x speed. The sun is driven from it.
 int32 DayRewound=-1; // the game day the hour buttons went back from (R3-225): reached again, it is not begun twice
 FVector OutingStuckPos=FVector::ZeroVector;int32 OutingStuckCount=0; // an outing held up in the same spot again and again (R3-234)
 TMap<int32,int64> WaitsForHim; // she wrote him where she is: until this minute she stays there for him (R3-230)
 // Who was with him in the save, by stable id, until each is put back (R3-238); the one who walks with him again, and until when it is tried.
 TMap<FString,FOKStreetStore::FWithHim> ResumeWithHim;int32 ResumeFollow=INDEX_NONE;double ResumeFollowUntil=0;TArray<int32> ResumeParty;
 TArray<FString> PromiseAskNotes; // "ask him what time / where" notes not sent yet: dropped once the meeting is booked (R3-242)
 double FreeTalkSentAt=0,OutingBegunAt=0; // when her reply now coming was asked for, and when the outing under way began (R3-250)
 // Walking with several: each one's place by his facing (places.rs party_formation), for the group it was asked for;
 // the one he walks with when the formation, not her trail, walks her (TickParty), and her pace for her walk clip.
 FString PartySlotsKey;TMap<int32,FVector2D> PartySlots;int32 GroupWalkPartner=INDEX_NONE;float GroupWalkPartnerSpeed=0.f;
 FVector PartyLastMale=FVector::ZeroVector;float PartyHisSpeed=0.f; // his pace, for the walking line (TickParty)
 FString ThenWaitLogged; // the "then" logged as waiting for its place to open (TickThenAction)
 FString ResumeCartPlace;int32 ResumeCartHer=INDEX_NONE; // the saved basket's shop and girl, until its bill is asked again (R3-257)
 double ClockStartHours=12,ClockStartWall=0,NextSunUpdate=0;bool ClockDrivesSun=false,ClockPaused=false;TWeakObjectPtr<ADirectionalLight> Sun;
 // 2010 calendar (OKStreetSocialMind.inl): the clock reading last tick (-1 after a manual jump) and the last saved hour.
 double CalendarLastHours=-1;int32 CalendarSavedHour=-1;
 // ok_mind.dll, when it loaded; null keeps the old behaviour.
 TUniquePtr<FOKStreetRules> Rules;int32 RulesDefaultsGeneration=-1;TArray<TSharedPtr<FJsonObject>> PendingRulesEffects;float BondStartFavor=10.f;int64 BondTickMinutes=15,NeedsTickMinutes=15;
 TUniquePtr<FOKStreetMind> Mind;TMap<uint64,FOKStreetMindTalk> MindTalks;
 // The heroine the last mind talk was with; when talk moves on, her meeting is summarised into memories.
 int32 MindTalkPartner=INDEX_NONE;
 // Heroine profiles from the mind (plan §4.1) and their traits (§4.2a), by stable id.
 TMap<FString,TSharedPtr<FJsonObject>> MindProfiles;
 // Plan §7.1 open choices (checkout, gift, invite...), resolved by chat or menu.
 TArray<FOKStreetPendingChoice> Choices;int32 NextChoice=1;
 // Plan §8: the catalogue, the order being built, and the shop he stands in.
 FOKStreetCatalog Catalog;TArray<FOKStreetCartLine> Cart;FString ShopPlace;bool ShopOpen=false;
 // Plan §9: what just happened in front of others, per heroine id, told to her on the next turn; last needs tick.
 TMap<FString,FString> MindRecent;int64 NeedsTickMinute=-1,BondTickMinute=-1;
 // OKStreetSocialLife.inl
 int32 LifeWishDay=-1;TArray<FOKLifePose> LifePoses;double LifeCheckAt=0,LifeFaceAt=0,LifeWishAt=0;TMap<int32,double> LifeCooldown;TMap<int32,FVector> LifeRestore;
 TMap<int32,FString> LifeUrgent,InitiativeReason;TMap<int32,int64> InitiativeSince;TMap<int32,int64> ArriveLater,ArriveWaiting,MeetJoinSince;TMap<int32,TArray<FString>> ActivitiesDone;int64 ActivitiesDoneDay=-1,SeriesHour=-1;TPair<int32,int32> MorningPair{INDEX_NONE,INDEX_NONE};TMap<int32,int32> LifePairRoute;TMap<int32,TSharedPtr<FOKStreetPlanBlock>> LifeNow;double SkipAskedAt=-100;bool SleepNap=false,SleepWake=false;
 // OKStreetSocialPhoneInbox.inl: her history, the phone (messages, calls), and what is apart.
 TMap<FString,TArray<FOKStreetHistoryEntry>> History;TSet<FString> HistoryLoaded;TMap<FString,int64> PhoneSeen;bool PhoneSeenLoaded=false;
 int32 RemotePeer=INDEX_NONE,RemoteMode=0,PhoneThread=INDEX_NONE,RingGirl=INDEX_NONE,PhoneComposing=0;FString PhoneTab=TEXT("msg"),PhoneDraft,RingWhy;double RingUntil=0,CallStarted=0;
 struct FPendingText{int32 Girl=INDEX_NONE;FString Why,Kind;double Due=0;int32 Tries=0;};TArray<FPendingText> PendingTexts;bool PhoneInFlight=false;double GateNudgeAt=-100;
 TMap<int32,FOKNpcLift> NpcLifts;int32 ActGirl=INDEX_NONE,LastActivityGirl=INDEX_NONE;int32 ArrivedWith=INDEX_NONE;FString ArrivedPlace;int64 ArrivedMinute=-1; /* the last walk he and a girl ended together, for appointments_tick (R5-13) */bool ActCutShort=false,ActTimeCut=false;int64 ActStart=0,LifeBlocksMinute=-1;int32 ActFirstAppointment=0; /* the appointments made during it are for later (ActivityAftermath) */ FString ActId,ActSig,LastActivity;int32 HerPaidGirl=INDEX_NONE;int64 HerPaidAmount=0,HerPaidAt=0;FString HerPaidNote;FString HerPaidShop; /* her own bill just paid (RecordPayAgreed) */ /* ActSig: what kind of activity ActId is (TickActivityWatch) */ TSet<FString> LifeDone;double BiasCheckAt=0;TMap<int32,double> PossessiveAt;
 // Round 3 (Docs/STREET_BEHAVIOR_PLAN_20261012b.md).
 TMap<int32,int32> PlanBlockSince;TMap<FString,int32> PaidShiftMinutes;bool ActIsHers=false,ActLover=false;int32 IntroFriend=INDEX_NONE;double IntroAt=0,GatePenaltyAt=-100,LastActivityAt=-100;
 TMap<int32,TArray<int64>> SelfTexts;int32 SentRemoteGirl=INDEX_NONE,SentRemoteMode=0,ReplyRemoteGirl=INDEX_NONE,ReplyRemoteMode=0;int64 LastCheckoutMinute=-1;FString NotServedNote,CheckoutNote;TMap<FString,int32> JobRejected;
 // His company (OKStreetSocialCompany*.inl).
 FString CompanyTab=TEXT("overview"),CompanyAmount,HeroJobSig,PayslipNote;TMap<int32,float> NpcLiftArrivedZ;bool FinMenuOpen=false; // the money buttons open under one "理財" (BuildFinanceRows)
 // The books (OKStreetSocialBooks.inl, OKStreetSocialAccountsApp.inl): journals by book, what each account was last seen at, posts already written.
 TMap<FString,TArray<FOKBookEntry>> Books;TSet<FString> BooksLoaded;TMap<FString,TMap<FString,int64>> BookSeen;TSet<int64> BookFlushed;double BooksTickAt=0;bool BooksStarted=false;FString BookLastBankNote;
 FString AccBook=TEXT("private"),AccTab=TEXT("journal"),AccAccount;int32 AccMonth=0;TMap<FString,FString> TestBooks;TSet<FString> CompanyVisited;double LotWorldAt=0;TMap<FString,FString> LotWorldKeys;TMap<FString,TArray<TWeakObjectPtr<AActor>>> LotActors;TMap<FString,TArray<FBox>> LotBlocks;FVector FacilityOrigin=FVector::ZeroVector;FRotator FacilityRotation=FRotator::ZeroRotator;bool FacilityInPlace=false;
 TWeakObjectPtr<AActor> Rain;TWeakObjectPtr<class UInstancedStaticMeshComponent> RainMesh;TArray<FVector> RainDrops;
 // Plan §12/§13: his look base, this turn's invitation answers, refusals (cooldown and pestering), follow mode,
 // the heroine walking over on her own, and a second sleeper.
 float PlayerLookBase=50.f;TMap<FString,TArray<bool>> InviteStance;TMap<FString,double> InviteRefusedAt;TMap<FString,int32> InviteRefusals;
 int32 FollowMode=0,LedReplans=0;double LedBlockedSince=0;FVector LedLastPos=FVector::ZeroVector,LedReplanPos=FVector::ZeroVector;float FollowGirlWalk=0;FVector FollowLead=FVector::ZeroVector;bool FollowLeadInit=false;
 TArray<double> ApproachNext;int32 ApproachGirl=INDEX_NONE;TArray<FVector> ApproachPath;double ApproachLimit=90,ApproachStarted=0,ApproachLast=-1e9,ApproachRouteAt=0;bool ApproachInvite=false;bool ApproachAppointment=false,ApproachJoin=false,ApproachSong=false,ApproachPrivate=false;int32 ApproachRetries=0;
  // A date called off on the phone (R3-268): who, how he put it (the judge's cancel_grace; -1: hers), the date the
  // "call to cancel" button is about, his lines when the call began (none by the hang-up: hung up without a word), and
  // the last cancel for each girl (her act and the judge both read the same "cancel it": one date, not two).
  int32 CancelCallGirl=INDEX_NONE,CancelCallAppointment=INDEX_NONE;double CancelCallGrace=-1;FString CancelCallSaid;int64 CallHisLines=0;float CallStartFavor=-1;TMap<int32,double> CancelledAt;FString AdmittedPlace;int32 AdmittedDay=-1;
 int32 SleepGirl2=INDEX_NONE;FVector SleepGirl2Bed=FVector::ZeroVector;float SleepGirl2Yaw=0;
 // Plan §12.4 level 4: she agreed, and they are on the way to a lockable room (or already in the private mode).
 int32 PendingIntimacy=INDEX_NONE,IntimacyGirl=INDEX_NONE;
 int64 AppointmentTickMinute=-1;
 // Plan §6: today's plans per heroine, the block each is in, walks in progress, meals eaten this block, who the
 // schedule is holding in place, and where he spends his time.
 TMap<int32,FOKStreetPendingMove> PendingMoves; // heroines whose walk is still being routed (TickRouteJobs)
 TMap<int32,float> StreetBlockedFor;TMap<int32,double> StreetDetourAt; // a free heroine held up on her street walk, and when she may be routed round again
 uint32 AutoFriendshipNext=0; // whose turn it is to be asked (TickAutoFriendship)
 bool AcademicsInFlight=false; // campus_tick asked, its answer not back yet (TickAcademics)
 // bonds_tick asked off the game thread (TickBonds); BondsSerial counts bond events, so an answer computed before one
 // (its favours are absolute) is dropped and those minutes asked again.
 bool BondsInFlight=false;uint32 BondsSerial=0;
 TSharedPtr<FOKStreetGridSearch> NavSearch;FVector NavSearchGoal=FVector::ZeroVector;FString NavSearchName; // his map navigation being routed (TickNavSearch)
 TArray<TWeakObjectPtr<AActor>> DestroyQueue; // DestroyLater
 TMap<int32,TArray<FOKStreetPlanBlock>> Plans;int32 PlanDay=-1;double PlanWaitSince=0;bool PlannedWithoutProfiles=false;TMap<int32,int64> ConfessionPending;TMap<int32,int32> PlanBlock;TSet<int32> PlanEaten,ScheduleHeld;TMap<int32,TArray<FVector>> Commutes;
 TMap<FString,int32> PlayerPlaceMinutes;double ScheduleTickAt=0;int64 PlaceCountMinute=-1;
 // Plan §8.6: the shift he is working now (appointment id, job, window in game minutes), early-leave tracking, fast-forward.
 // Plan §6.5: who spent time with him today, the day already reflected on, and tomorrow's intents.
 TSet<int32> InteractedToday;int32 ReflectedDay=-1;TMap<FString,FString> Intents;
 // Plan §5.7 / §6.4: the global drama mode (A/B/C) and the encounter clock (pair -> day+1 it last met).
 TCHAR DramaMode=TEXT('B');TMap<FString,TCHAR> DramaModes;FString DramaFoil,DramaCastKey;int64 EncounterTick=-1;TMap<FString,int32> EncounterDay;
 // Plan §6.6 / §14: the initiative slot last weighed, initiatives taken today, and the Director's day.
 int32 InitiativeSlot=-1,InitiativesToday=0,DirectorDay=-1;
 // Plan §15: the open full-screen app (lottery, scratch, web, notebook, atm, cn, us), its data and its inputs.
 FString App,FinMarket,FinCode,FinPeriod=TEXT("intraday"),FinSearchText,FinOrderSide=TEXT("buy"),FinOrderKind=TEXT("market"),FinOrderPrice,FinOrderQty=TEXT("100"),FinTransfer=TEXT("2000"),FinNotice;
 bool AppReadOnly=false,AppSavedMouse=false,FinTickWarned=false,PcWarned=false;int64 FinDueAt=0;bool FinTickInFlight=false,FinBookChanged=false;TSharedRef<FThreadSafeBool,ESPMode::ThreadSafe> FinQueryRunning=MakeShared<FThreadSafeBool,ESPMode::ThreadSafe>(false);TSharedRef<FThreadSafeBool,ESPMode::ThreadSafe> SaveCopyRunning=MakeShared<FThreadSafeBool,ESPMode::ThreadSafe>(false); /* SaveToSlot in the background */ /* TickFinance */double FinTickAt=0,AppRefreshAt=0,FinMoversAt=0,DrawShowAt=0,WatchStart=0;
 int32 FinHeroDay=-1,WatchGirl=INDEX_NONE,PcPaidHours=0,LotZone2=1,LotDraws=1;int64 FinClaims=0,ScratchTicket=0,PcStartMinute=0;FString PcPlace,LotTab=TEXT("star649"),LotGame=TEXT("star649"),WebTab=TEXT("home"),ScratchTheme,BetStake=TEXT("20");
 TSharedPtr<FJsonObject> FinLotto,FinBoard,FinTickets,FinNotebook,FinQuote,FinAccount,FinWatch,FinMovers,FinSummary,FinSearch,DrawShow;
 bool NearCounter=false;
 // NPC idles: when each last moved, how long she has stood still, and whether her current stop was lengthened.
 TMap<int32,int32> IdlePose;TMap<int32,double> LastMovedAt,StillSince;TMap<int32,float> LastWalk,PrevDwell;int32 ProbeStep=-1;double ProbeAt=0;FString ProbeWhy;TArray<TSharedPtr<FJsonValue>> ProbeRows; // OK_STREET_UI_PROBE
 // Campus (OKStreetSocialCampus.inl): upstairs commute targets, studying, stair/office proximity, attendance, exams.
 TMap<int32,FVector> CommuteFinal;bool FreeTalkLogDirty=false;double FreeTalkLogSavedAt=0; /* free-talk-log.json written at most once a second (Tick) */TArray<FIntVector> PendingEncounters; /* met pairs (a, b, overheard) waiting for their turn: one a frame (TickEncounters) */FString EntryPlace;int32 EntryMinutes=0;bool bEntryPlaceKnown=false,bEntrySettled=true;int32 EntryHer=INDEX_NONE; /* the place he is in, minutes in a row, admission asked (PayEntry), the girl with him then */FString EntryHerPlace; /* where EntryHer was last set (R3-267) */bool bAbsentCastChecked=false; /* meetings with heroines not in this cast set aside (SetAsideAbsentMeetings), once the rules are open */TMap<int32,TPair<int64,double>> PendingGiveMoney; /* her act's give_money, waiting for the judge (amount, wall time) */TSet<int32> ReleaseAtDoor; /* walking out of a place to roam the street: let go at its door */bool Studying=false,ShowTranscript=false;int32 StudyMinutes=0,StudyApplied=0,StairKey=-1;TSet<int32> StudiedWith,SeenInClass;int32 HerInvestMinute=-1;TMap<int32,int32> HerStudy;FString OfficeKey,DeptOffice,ClassClosed;TSet<FString> ExamsDone;int64 AcademicMinute=-1;
 // Bank app, price alerts, net worth chart and the heroines' own accounts (OKStreetSocialBank.inl).
 FString AtmTab=TEXT("cash"),BankAmount=TEXT("1000"),AlertValue;int32 WorthRange=30;TSharedPtr<FOKChartData> WorthChart;TSharedPtr<FJsonObject> FinAlerts,HerAccount;
 bool ShowDebug=!FPlatformMisc::GetEnvironmentVariable(TEXT("OK_STREET_DEV")).IsEmpty(); // the "女角數值" panel (F10)
 TMap<int32,FString> HerSaysNext; // what she says as soon as her reply and voice are done (TickAutoFriendship)
 FString RelationshipEventName,RelationshipShy;TMap<int32,int64> HerTreatPromise,HerTreatKept; /* HerTreatKept: when her treat was paid (R3-227) */TMap<int32,TSet<FIntPoint>> NavLearned; /* cells a walk was stopped in (by height): GridRoute avoids them */TMap<int32,double> PromiseBookedAt; /* appointment id -> when a promise in talk booked it (one exchange books once) */FString PendingGift;TArray<FString> DeferredNotes;FString PrivateTalkContext,PrivateShownLine; /* talk in the H scene (TickPrivate) */ /* game notes for her held until her last reply is spoken (SendFreeTalk) */TMap<int32,FString> HerProposed; /* an activity she proposed in a nudge reply, not started: told to the next judge */TMap<FString,int64> RecentBought; /* "item|her" / "item|him" -> minute bought (FinishCheckout): not bought again */int32 HisLines=0;bool SystemNote=false; /* SystemNote: the next turn sent is the game's, not his */ // HisLines: his lines to the girls, counted (a choice answered after it opened)
TMap<int32,TArray<FOKStreetCartLine>> CarriedOrders;TMap<int32,FString> CarriedOrderSaid; // CarriedOrders: ordered on the way to the shop (Mind.inl), placed on arrival (StartOutingActivity)
 // RelationshipShy: talk.rs shy_confession
 // she said this meal is on her: until (game minute)
int64 BankruptCheckHour=-1,BankruptLogTick=-1;double BankruptShowAt=0;TArray<FString> BankruptLines;TMap<int32,int32> HerInvestDay;TMap<FString,int64> HerInvestTotal;TMap<FString,FString> HerInvestText;
 int32 VentureHeroine=INDEX_NONE;FString VentureAmount=TEXT("5000"),VentureDays=TEXT("30");
 TSharedPtr<FOKChartData> FinChart;TSharedPtr<FOKScratchState> Scratch;TArray<TSharedPtr<SVerticalBox>> AppBoxes;TArray<FString> FinWatchCodes[2];TSet<int32> LotPicks;TArray<TSharedPtr<FJsonObject>> BetSlip;TSet<FString> FinReacted;
 int32 ShiftAppointment=0;FString ShiftJob;int64 ShiftStart=0,ShiftEnd=0,ShiftAwaySince=-1;bool ShiftFast=false;int32 ShiftChats=0;double ShiftTickWall=0;
 bool FreeTalkLive=false,FreeTalkLivePolling=false,FreeTalkLiveMuted=false,FreeTalkLiveHisMute=false,FreeTalkLiveSpeaking=false,FreeTalkLiveBaseline=false;int64 FreeTalkLiveAfter=0;double FreeTalkLiveNextPoll=0;FString FreeTalkLivePending;FString PendingTopic /* chosen while her answer was on its way */;int64 FreeTalkSent=0,FreeTalkAnswered=0; /* talk requests sent / answered (TickChoices) */int32 FreeTalkGirl=INDEX_NONE,OutingGirl=INDEX_NONE;
 bool ApproachBlockedNear=false; // she walking over is held up by something within a couple of steps of him: close enough
 int64 ApproachLastMinute=-100000; // game minute the last girl set off to come over to him (TickApproach paces by game time)
 double ApproachBlockedSince=0; // when she walking over to him was last held up (0: walking freely)
 int64 OrderDroppedMinute=-100000; // game minute an order was taken back or could not be paid (no implicit menu meal billed then)
 FString FeesDueText; // "pay tuition by 9/27" while the window is open and unpaid (bank.rs bank_daily fees_due)
 int32 FeesDueDay=-1; // the calendar day FeesDueText was worked out for (-1: not yet, e.g. right after loading a save)
 double OverlapCheckAt=0; // next check that nobody stands inside him (TickFreeTalkTrace)
 bool SkipGreeting=false; // she walked up to him and opens in her own words (InitiativeNudge): no scripted greeting line first
 TSet<int32> MeetingGreeted; // appointments she already greeted him for: a talk after the meeting is not "you came as agreed" again
 FOKStreetBluff Bluff;double ShopArrivalAt=-1000;TSet<FString> PartyMoodChecked;TMap<int32,double> PartyLeaving,PartedAt;TArray<int32> Party,FreeTalkSpeechSpeakers;TArray<FString> PartyEvents,PartyLog;TMap<int32,double> PartyGapRef,PartyGapAt;double ManualMoveAt=-100;bool ManualReplan=false;bool UiDirty=false;const char* UiDirtyFile="";uint32 UiDirtyLine=0;double UiShopCheckAt=0;FString UiShopKey;FString PlayerAskedText;FString LastPlayerSaid;double ApproachBlockLogAt=0;int32 CompanionGapFor=INDEX_NONE;double CompanionGapRef=0,CompanionGapRefAt=0;double FaceOffSince=0,FaceSetYaw=0,FaceLogAt=0;int32 HeadLookGirl=INDEX_NONE;TArray<FOKStreetDish> Table;int32 ServeBatch=0;FString MealPlace;double MealAwayAt=0,MealBookAt=0;bool StopEating=false,FullTold=false;TArray<FString> TellHer;int32 AutoPickTried=INDEX_NONE;bool FreeTalkUserClosed=false;double FreeTalkLeftAt=0;FString RetryAction;int32 RetryGirl=INDEX_NONE;double RetryUntil=0;FString ThenAction;int32 ThenGirl=INDEX_NONE;int64 ThenUntil=0,ThenStayMinutes=0,ThenReadyAt=0;TSet<int32> ArriveAfterLift;int32 FarewellAfterMeal=INDEX_NONE;double FarewellAfterMealAt=0;double TableClearedAt=-1;bool FarewellPending=false,FarewellAsked=false,FarewellFromTalk=false;double FarewellAt=0,FarewellVoiceAt=0,PlaceClosingAt=0;FString ClosedNoticePlace;double DecisionShotAt=0;FString DecisionShotTag;int32 DecisionShots=0;FString ShopArrivalPlace,ShopSeenPlace;bool ShopEnteredWith=false;TArray<FOKStreetPendingServe> Serves;double OrderNoticeDue=0;FString OrderNoticeSig;FString TraceSig;double TraceAt=0;FVector TracePos=FVector::ZeroVector;int32 TraceGirl=INDEX_NONE;double StrayPoseAt=0;TMap<int32,int32> StrayPose; float RightScrollOffset=0;FString FreeTalkHeard; bool AppointmentsOpen=false; // the right panel's list of appointments to cancel
 int32 PayAgreedWith=INDEX_NONE;FString PayAgreedHow,PayAgreedShop;int64 PayAgreedAt=0; // who pays, as the AI read what they settled (her reply's payer, the judge's)
 TMap<int32,double> ChatOrderAwaitsJudge; // her reply's order, billed once the judge says who pays (wall time it waits from; R4-3)
 TArray<FString> SaidToHer;int32 SaidToHerWith=INDEX_NONE; // his last lines to her in this conversation: "I'll treat you" said on the way still counts at the table
 // She asked to borrow before he offered (her act skipped): his next line handing it over makes the loan.
 int32 LendAskGirl=INDEX_NONE;int32 LendAskAmount=0;int64 LendAskMinute=-100000;
 // The catch light: a faint point light riding the camera on lighting channel 2. The heroines are unlit (their own
 // programs) except the lit tears, so this is the light their wet highlight takes.
 TWeakObjectPtr<class UPointLightComponent> CatchLight;
 int64 SaidToHerAt=0; // game minute of his last line to her: lines from a talk hours ago (same girl) are not this conversation
 double FreeTalkQuietUntil=0; // she asked him to keep quiet (judge): his silence is what she wanted until then
 bool ControlMicIgnored=false; // control channel: what the room microphone hears is not his (a tester talking nearby)
 bool FreeTalkMicPausedClock=false,FreeTalkMicOpen=false,FreeTalkLiveReady=false,FreeTalkLiveStarting=false,FreeTalkLeavePending=false;double FreeTalkSilenceSince=0,FreeTalkNudgeAt=0,FreeTalkLiveTickAt=0;int32 FreeTalkSilencePenalties=0;float FreeTalkSilenceScale=1;int32 LiveTestPhase=-1,LiveTestNudges=0,LiveTestPenalties=0;float LiveTestFavor=0;bool LiveTestLeft=false,LiveTestInterrupted=false,LiveTestHerReply=false;double LiveTestAt=0;FString LiveTestWav;double FreeTalkLiveRetryAt=0;int32 FreeTalkLivePollFails=0;float FreeTalkMicLevel=0;FString FreeTalkMic;TArray<FString> FreeTalkMics;
 FString OutingActivity,OutingPlace;TArray<FVector> OutingRoute;int32 OutingStep=0;double OutingProgressAt=0;bool OutingWaitDoor=false;bool OutingRun=false;FString StrollActivity;int64 StrollStartMinute=0;bool QuietTalk=false;TArray<TWeakObjectPtr<AActor>> PlaceActors;TArray<TWeakObjectPtr<ADirectionalLight>> PlaceFills;float NextPairYaw=0;FString NextPairFemaleClip,NextPairMaleClip;int32 SoloGirl=INDEX_NONE,SoloPhase=0,SoloMinutes=0;double SoloAt=0;int64 SoloStartMinute=0;FVector SoloSpot=FVector::ZeroVector;float SoloYaw=0;FString SoloFemale,SoloMale,SoloWhat,SoloId;bool SleepPaired=false;int32 SleepPhase=0,SleepGirl=INDEX_NONE;double SleepAt=0;FVector SleepBed=FVector::ZeroVector,SleepGirlBed=FVector::ZeroVector;float SleepYaw=0,SleepGirlYaw=0;FString SleepPlace;int32 RestroomGirl=INDEX_NONE;double RestroomUntil=0;FString RestroomPlace;double RestroomBlockedAt=0;struct FPlaceLayers {int32 Place=INDEX_NONE;TWeakObjectPtr<UPrimitiveComponent> Roof;TMap<int32,TWeakObjectPtr<UPrimitiveComponent>> Levels;};TArray<FPlaceLayers> PlaceRoofs;FString PlaceTourDir;int32 PlaceTourIndex=-1;double PlaceTourAt=0;bool PlaceTourShot=false;TMap<int32,FVector> CommuteShown;
 TArray<FString> FreeTalkSpeechLines,FreeTalkSpeechPaths,FreeTalkReplayParts;TArray<uint8> FreeTalkSpeechReady,FreeTalkSpeechStreamed;TArray<TSharedPtr<FKKRuntimeSpeechStream,ESPMode::ThreadSafe>> FreeTalkSpeechStreams;int32 FreeTalkSpeechGeneration=0,FreeTalkSpeechCharacter=INDEX_NONE,FreeTalkSpeechPlayed=0,FreeTalkSpeechFiles=0;int64 FreeTalkSpeechSerial=0;double FreeTalkSpeechNext=0,FreeTalkSpeechRepliedAt=0;FString FreeTalkSpeechChannel;TArray<double> FreeTalkSpeechSentAt;
 FString FreeTalkCaptionPlayer,FreeTalkCaptionHer;TArray<int64> FreeTalkReplay;int32 FreeTalkReplayTotal=0,FreeTalkReplaySpeaker=INDEX_NONE;double FreeTalkReplayNext=0;
 // The replayed line now playing, for the galgame scene (its box and portrait): bumped per line.
 // Who says it (0: him), whose conversation it belongs to, whether her voice plays it, and until when a line without a
 // voice stays up (scene time).
 // A replayed line of several clips: how many and which one plays (the galgame typewriter follows all of them).
 int32 FreeTalkReplayClip=0,FreeTalkReplayClips=0;
 int64 FreeTalkReplayShown=0;int32 FreeTalkReplayShownSlot=INDEX_NONE,FreeTalkReplaySceneSlot=INDEX_NONE;FString FreeTalkReplayShownText;bool FreeTalkReplayShownVoiced=false;double FreeTalkReplayHold=0;
 bool TopicsOpen=false;TArray<TSharedPtr<SSlider>> StatSliders;TArray<TSharedPtr<SCheckBox>> RelationChecks;int32 RelationControlsVerified=0;bool RelationControlsSync=false;TMap<int32,double> RunUntil,RunNext;
 bool ContactsOpen=false,ActivitiesOpen=false,TouchOpen=false,ContactsUI=false,ContactsOnlyTest=false,ContactsChecked=false;int32 ContactSelection=INDEX_NONE,ContactRows=0,ContactActions=0;
 TArray<FString> HighCards;TArray<int32> HighOutfits;TWeakObjectPtr<AOKInteractionMode> PrivateMode;TSharedPtr<SWidget> PrivateLeave; /* the one-on-one scene's own leave button (R3-228) */FString HighPrivateReport;double HighPrivateAt=0,PrivatePausedSeconds=0;bool PrivateClockFrozen=false; /* the street clock stood still for the private scene (it runs again on leaving, by the real minutes spent) */bool HighPrivateSeenReady=false;TSharedPtr<FJsonObject> HighPrivateResult;TArray<FOKStreetAgentSim> HighPrivateAgents;TArray<FRotator> HighPrivateRotations;FVector HighPrivateMale;
 TMap<int32,double> CommutePausedUntil; // a girl he walked up to stops her walk for a moment (StepNavigation)
 int32 NavGirl=INDEX_NONE,PrivateGirl=INDEX_NONE,ContactsProbePhase=0;FOKStreetLiftRide NavRide; // the navigation riding an elevator (PlanNavLift)
 double NavAt=0,ContactsProbeAt=0;
 int32 ContactsProbeGreeting=0;TArray<TSharedPtr<FJsonValue>> ContactsVoiceEvidence;TArray<FVector> NavPath;
 FString ContactsNotice;
 // Auto navigation: NavGirl follows her, NavPoint a spot picked on the big map (M). The male walks the
 // gold route himself; any WASD input hands control back and cancels it.
 bool NavPointActive=false,BigMapOpen=false,BigMapSavedMouse=false;FVector NavPoint=FVector::ZeroVector;FString NavPointName;int32 NavStep=1;double NavProgressAt=0;int32 NavReplans=0;TArray<FOKStreetElevator> Elevators;TMap<int32,FOKStreetLiftWalk> LiftWalks;TMap<int32,double> LiftCalledAt;TMap<int32,FVector> LiftPins;FOKStreetLiftRide LiftRide;FOKStreetLoverPose LoverPose;FOKStreetLoverReact LoverReact;TSet<FString> LoverContacts;TMap<int32,FVector> LastShown;TMap<int32,TMap<FIntPoint,bool>> NavGrid;TSharedPtr<class SOKStreetBigMap> BigMap;
 bool PrivateMouse=false;
 int32 PrivateAnyCase=0,PrivateAnyPhase=0;double PrivateAnyProfileWait=0;FString PrivateAnySave;TArray<TSharedPtr<FJsonValue>> PrivateAnyEvidence;
 FOKStreetCandidateProbeSnapshot ContactsSnapshot;FOKStreetStore ContactsProbeStore;TArray<FOKStreetAgentSim> ContactsProbeAgents;
 TArray<FRotator> ContactsProbeRotations;FVector ContactsProbeMale;
 int32 ContactsRoutes=0,ContactsPrivate=0,ContactsVoices=0;

 FString SeatedMaleClip,SeatedFemaleClip,SeatedProbeSave;
 int32 MelonCasesVerified=0,MelonProbePhase=0,MelonHits=0,MelonSamples=0;
 // Melon distance per striker (cm in front of her root), fixed at her first swing: the contact recipe is immutable.
 TMap<int32,float> MelonForward;
 bool MelonChecked=false,MelonCancelChecked=false,MelonOnlyTest=false;
 TArray<float> MelonDistances;
 double MelonProbeAt=0;FString MelonProbeSave;FOKStreetPairSession MelonProbeSession;
 FString MealProbeSave;FOKStreetPairSession MealProbeSession;
 int32 MealActorsVerified=0,MealProbePhase=0,MealSamples=0,MealVoices=0,MealProbeVoices=0;
 double MealProbeAt=0;FVector MealFirstHand[2];
 bool MealChecked=false,MealCancelChecked=false,MealOnlyTest=false;
 TArray<TPair<TWeakObjectPtr<AStaticMeshActor>,FVector>> MealHandProps;
 TMap<int32,double> MealAteAt; // when each (0 him) last ate or sipped something of theirs at the table (meal tick)
 TMap<FString,int64> OrderedAtPlace; // place -> the game minute dishes were last ordered there (a table meal is not billed again)
 TSet<int32> AppointmentBusySeen; // appointment ids: she was there while he talked with another girl (social.rs: not late)
 TMap<int32,int64> SheThereSince; // appointment id -> the game minute she got to its place (how long she waited for him)
 FString DrinkMaleClip,DrinkFemaleClip,DrinkProbeSave;
 FOKStreetPairSession DrinkProbeSession;
 int32 DrinkActorsVerified=0,DrinkProbePhase=0,DrinkSamples=0,DrinkVoices=0,DrinkProbeVoices=0;
 double DrinkProbeAt=0;FVector DrinkFirstHand[2];
 bool DrinkChecked=false,DrinkCancelChecked=false,DrinkOnlyTest=false;
 FString SplashMaleThrow,SplashMaleReceive,SplashFemaleThrow,SplashFemaleReceive,SplashProbeSave;
 int32 SplashCasesVerified=0,SplashProbePhase=0,SplashEffectSamples=0;
 bool SplashChecked=false,SplashCancelChecked=false,SplashOnlyTest=false;
 double SplashProbeAt=0;
 double SplashCycleSeconds=10.0/3.0;
 FOKStreetPairSession SplashProbeSession;
 TArray<TWeakObjectPtr<AStaticMeshActor>> PairProps;
 TMap<int32,TArray<TWeakObjectPtr<AStaticMeshActor>>> ReadingProps;
 int32 ReadingActorsVerified=0,ReadingProbePhase=0,ReadingForceWalk=INDEX_NONE;
 FOKStreetAgentSim ReadingSavedAgent;double ReadingProbeAt=0;FString ReadingProbeSave;
 bool ReadingChecked=false,ReadingOnlyTest=false;
 int32 SeatedActorsVerified=0,SeatedProbePhase=0,SeatedVoices=0,SeatedProbeVoices=0;
 double SeatedProbeAt=0;
 bool SeatedChecked=false,SeatedCancelChecked=false,SeatedOnlyTest=false;
 TMap<int32,FString> PhotoPoses;
 FString PhotoMaleClip,LastPhoto,PhotoProbeSave;
 // Motions (rules motions.toml): the two-shot pose of the photo now and one asked for; the loop each schedule block
 // shows ("actor|kind|place|start" -> clip); the clips she sits for and the ones that bring a table.
 FString PhotoPoseKey,NextPhotoPose;TMap<FString,FString> LifeClipChoice;TSet<FString> MotionSeated,MotionTable;bool MotionTableRead=false;
 // A photo she is taking (judge she_photo): saved by the screenshot, then filed (and sent, when she said so).
 struct FHerPhoto{int32 Girl=INDEX_NONE;FString Path,Topic,Subject,Album;double At=0;bool Send=false,Drawing=false,ByHim=false,Requested=false;};
 // The camera where the one taking a photo holds the phone, for the frame it is taken in (her view, not the game's).
 double PhotoViewUntil=0;FVector PhotoViewLoc=FVector::ZeroVector;FRotator PhotoViewRot=FRotator::ZeroRotator;
 // An activity's effect (activities.toml effect "explosion"): when it goes off, where, and what is shown of it.
 // Walking out in the rain (weather.rs walk_style): the umbrella shown over him (and her, when they share it).
 TArray<FOKLifePose> PartySeats;TMap<int32,double> PartySeatRetry;TMap<int32,FString> PartySeatNow; // the others with them sitting down too (TickPartySeats)
 FString WalkStyle,WalkFact;TWeakObjectPtr<AActor> Umbrella; // WalkFact: told while they are still on the way, dropped when the walk ends
 double EffectAt=0;FVector EffectWhere=FVector::ZeroVector;FString EffectKind;int32 EffectGirl=INDEX_NONE;bool EffectFizzle=false; /* her explosion spell came off or fizzled (R3-269) */TArray<TWeakObjectPtr<AActor>> EffectActors;TWeakObjectPtr<class UPointLightComponent> EffectLight;
 // A photo walk (photo_walk): the stops photographed so far, the album they go in, and the pause at a stop.
 int32 PhotoWalkStops=0;FString PhotoWalkAlbum;double PhotoWalkPauseUntil=0;TArray<FHerPhoto> HerPhotos;
 int32 PhotosSaved=0,PhotoProbePhase=0,PhotoActorsVerified=0;
 bool PhotoChecked=false,PhotoCancelChecked=false,PhotoOnlyTest=false;
 uint64 PairRestoredFrame=MAX_uint64;int32 PairRestoredActor=INDEX_NONE;
 int32 Companion=INDEX_NONE,CompanionProbePhase=0;
 bool CompanionChecked=false;
 double CompanionBlockedAt=0,CompanionProbeAt=0;
 float CompanionTravel=0,CompanionWalk=0;
 FVector CompanionLastMale=FVector::ZeroVector,CompanionProbeMale=FVector::ZeroVector;
 FOKStreetAgentSim CompanionProbeAgent;
 FRotator CompanionProbeRotation,CompanionProbeMaleRotation;
 FString CompanionProbeSave;
 TArray<FVector> CompanionTrail;
 int32 Facility=INDEX_NONE,FacilityCharacter=INDEX_NONE;
 FOKStreetAgentSim FacilitySavedAgent;
 FVector FacilitySavedMale=FVector::ZeroVector;
 FRotator FacilitySavedRotation=FRotator::ZeroRotator;
 FRotator FacilitySavedMaleRotation=FRotator::ZeroRotator;
 float FacilitySavedYaw=0,FacilitySavedPitch=0,FacilitySavedDistance=0;
 double FacilityRequestedAt=0,FacilityStartedAt=0,FacilityScreenshotAt=0;
 int32 FacilityProbePhase=0,FacilitiesVerified=0;
 bool FacilitiesChecked=false,FacilityEntered=false,FacilitySavedMouse=false,FacilityExitUI=false,FacilityOnlyTest=false;
 int32 FacilityPrepareCancels=0,FacilityClockChecks=0;
 FVector FacilityAnchor=FVector::ZeroVector;
 FString FacilityProbeSave;
 TArray<TWeakObjectPtr<AStaticMeshActor>> FacilityProps;
 TArray<TPair<TWeakObjectPtr<AStaticMeshActor>,FVector>> FacilityHandProps;
 FOKStreetManifestSocial Manifest;
 FOKStreetCatalogue Catalogue;FString TopicCataloguePath;float CollectedDesire=0;mutable TMap<FString,int32> FavorLimitCache;bool LineInterrupted=false;TMap<uint64,TFunction<void(bool,const FString&,const FString&)>> MindChats;
 TArray<FOKStreetInvitation> Invitations;
 bool InvitationActive=false,InvitationAccepted=false,InvitationOnlyTest=false;
 int32 InvitationCommand=INDEX_NONE,InvitationQueued=INDEX_NONE,InvitationGirl=INDEX_NONE;
 int32 InvitationProbeCase=0,InvitationProbePhase=0,InvitationVoices=0,InvitationAccepts=0,InvitationRefusals=0,InvitationCancels=0;
 double InvitationProbeAt=0;
 FString InvitationProbeSave;
 FOKStreetStore InvitationProbeStore;
 TArray<int32> InvitationSequence;
 int32 InvitationLedgerChecks=0;
 FOKStreetTopics CollectedTopics;
 TArray<TWeakObjectPtr<AStaticMeshActor>> TopicPickups;
 TArray<int32> PickupTopics;
 TArray<FOKStreetTopicPart> TopicSequence;
 int32 TopicPart=0,AskedTopic=INDEX_NONE,UsedTopic=INDEX_NONE,TopicProbePhase=0,CollectedCommits=0;
 bool CollectedActive=false,AwaitTopicAnswer=false,TopicIntroduction=false,CollectedProbeChecked=false;
 FOKStreetEffect CollectedEffect;
 float CollectedPoint=0;
 double SilentTopicUntil=0;
 TArray<FString> CollectedVoiceIds;
 TArray<TSharedPtr<FJsonValue>> CollectedEvidence;
 TArray<TSharedPtr<FJsonValue>> TouchEvidence;
 TMap<int32,int32> TouchCounts;
 TMap<int32,int32> TopicCommandCounts;
 FOKStreetEffect TouchEffect;
 int32 TouchRegion=INDEX_NONE,TouchStage=0,TouchRepeat=0,TouchCommits=0;
 bool TouchActive=false,TouchSee=false,TouchChecked=false,TouchMissingRecoveryChecked=false;
 bool TouchReactionOnlyTest=false,TouchReactionPlayed=false;int32 TouchReactionCase=0,TouchReactionPhase=0;double TouchReactionAt=0;float TouchReactionPeak=0;FVector TouchReactionStart,TouchReactionRoot;FString TouchReactionSave;TArray<TSharedPtr<FJsonValue>> TouchReactionEvidence;
 FOKStreetStore TopicProbeStore;
 FString TopicProbeSavePath;
 FOKStreetCandidateProbeSnapshot TopicProbeSnapshot;
 TArray<FOKStreetAgentSim> TopicProbeAgents;

 bool FaceValidationChecked=false;FOKStreetExpressions Expressions;int32 ExpressionsApplied=0,ExpressionsMissing=0;
 FOKStreetCatalogue OtherLanguage;
 bool Japanese=false;
 // Voice source: built-in clips or the local TTS server (OKStreetSocialVoice.inl).
 // The TTS Base model the launcher starts ok-tts with (voice-settings.json tts_base_size): 1.7B, or 0.6B (less VRAM).
 FString TtsBaseSize=TEXT("1.7B");
 bool TtsEnabled=false,TtsPending=false;FString TtsEndpoint=TEXT("http://127.0.0.1:47860"),TtsCacheDir,TtsSettingsPath,TtsNotice;int32 TtsAge=18,TtsLines=0,TtsFallbacks=0;uint32 TtsSerial=0;double TtsRequestedAt=0,TtsTimeout=15;bool TtsSmoke=false,TtsSmokeStarted=false;
 // Player (male) TTS line before her reply: MalePhase 0 none, 1 waiting for the clip, 2 speaking, 3 done/skipped.
 FString PlayerCardName,PlayerNameDraft,MaleTone=TEXT("m0"),MaleVoiceId,MaleLineOverride,LastBranchId,TtsVoiceInfo;bool MaleTtsPending=false,MaleAddressed=false;int32 MalePhase=0,TtsSmokeTopics=0;double MaleRequestedAt=0;uint32 MaleSerial=0;TArray<TSharedPtr<FString>> MaleToneOptions;
 // Her own voice (OKStreetSocialVoicePick.inl): candidates from ok-tts, whether her card holds a voice.
 FOKSongState Song; // her song event (OKStreetSocialSong.inl)
 // The inspection room (OKStreetSocialInspect.inl): who is in it, where he stood before, its floor, the view round her,
 // and the heroine asked for on the command line (-StreetSocialInspect=N) once everyone is loaded.
 int32 InspectSlot=INDEX_NONE,InspectAsked=INDEX_NONE;FVector InspectMaleBack=FVector::ZeroVector,InspectHerBack=FVector::ZeroVector;TArray<TWeakObjectPtr<AActor>> InspectRoom;
 float InspectYaw=0.f,InspectPitch=8.f,InspectDistance=420.f;
 struct FVoiceCandidate {FString Tag,Reference,Preview;};TArray<FVoiceCandidate> VoicePickCandidates;bool VoicePickOpen=false,VoicePickBusy=false,CardHasVoice=false;int32 CardVoiceProbedFor=INDEX_NONE; // whose card CardHasVoice answers for (R5-9)
FString VoicePickDescription,VoicePickNotice,TtsVoiceInstruct;int32 VoicePickTarget=INDEX_NONE;
 FOKStreetStore Store;
 TArray<FOKStreetAgentSim> Agents;
 TArray<FRotator> Rotations,RenderedRotations;
 bool MovementOnlyTest=false,MovementChecked=false;int32 FloorFinishesFixed=0,MovementSweepChecks=0,TurnSamples=0;double MovementProbeAt=0;float MaxFemaleTurnRate=0;
 TArray<FOKStreetAgentSim> ActivitySavedAgents;
 TArray<FRotator> ActivitySavedRotations;
 int32 ActivityProbePhase=0;
 float ActivityPausedDwell=0;
 bool ActivityStateChecked=false;
 FOKStreetCandidateProbeSnapshot ActivitySnapshot;
 TArray<FString> Errors,VoiceIds,Transactions;
 TSharedPtr<SWidget> Overlay;
 TSharedPtr<SBorder> InteractionPanel,DialoguePanel;
 // The UI shell's holders, one per region (RebuildUI), and the right panel's scroll box (it keeps its position).
 TSharedPtr<SBox> UiRightHolder,UiShopHolder,UiDebugHolder,UiBottomHolder,UiBluffHolder,UiHistoryHolder,UiAppHolder,UiSaveHolder,UiMenuHolder;TSharedPtr<SScrollBox> UiRightScroll;uint32 UiDirtyRegions=0;
 TSharedPtr<SOKStreetMinimap> Minimap;
 TSharedPtr<SWidget> MinimapPanel;
 bool MinimapShowNames=true,MinimapProbeShowNames=true;
 bool MinimapChecked=false,MinimapOnlyTest=false;
 int32 MinimapProbePhase=0;
 double MinimapProbeAt=0;
 FOKStreetStore MinimapProbeStore;
 bool WorldTouchPointerBlocked=false,TouchInputRoutingChecked=false,TouchMissingSubtitleChecked=false,ConversationControlsChecked=false;
 FString TouchNotice;
 TWeakObjectPtr<ACameraActor> Camera;
 TWeakObjectPtr<AStaticMeshActor> BackgroundGround;
 TWeakObjectPtr<AExponentialHeightFog> BackgroundFog;
 bool RuntimeBackdropReady=false;
 FVector Male=OKStreetMaleSpawn();
 FString Lang=TEXT("zh-TW");int32 MoneyDigits=0;TArray<TSharedPtr<FString>> LangOptions,RegionOptions,RenderStyleOptions; // OKStreetSocialLocale.inl
 mutable TMap<FString,int64> LocalAmounts; // Local(): region|kind|base -> amount, cleared when the region changes
 FString BluffOnArrival;double BluffDealBy=0; // ...and the time to wait for the others to sit down // the outing that takes them in to play cards: the game is dealt when it arrives (ArriveOuting)
 FVector SpotSavedMale=OKStreetMaleSpawn();double SpotSavedAt=0; // his position in the save, and when it was written (SaveCalendar)
 float Yaw=90,Pitch=-12,Distance=350,PeakMouth=0;TArray<TWeakObjectPtr<UPrimitiveComponent>> CameraHidden; // CameraHidden: whoever the follow camera is pressed into, left out of the view
 bool MouseUI=false,ViewportFocused=false,MouseToggleRequested=false,MouseModeChecked=false;
 FString SaveMenu,SaveNotice,NewGameRegion;bool SaveConfirmNew=false,SaveMenuPausedClock=false,SaveRestarting=false,StartMenuDecided=false,SaveApplied=false;double NextAutoSave=0; // OKStreetSocialSaves.inl
 int32 FacingChecks=0;
 TSet<int32> FacingActorsChecked;
 TSharedPtr<FOKStreetMouseModeInput> MouseInput;
 FString PlayerLine;
 int32 PlayerPromptsShown=0;
 FString Message=OKT(TEXT("ui.scene.loading_controls_hint")),PreviousNode;
 int32 Target=INDEX_NONE,Node=INDEX_NONE,Voice=0,TestStep=0,CompletedLines=0,Greetings=0,Farewells=0,TestNpc=1;
 FString ActiveCategory;
 FString ActiveGesture=TEXT("talk_gesture_00.okanim");
 bool ListenerStarted=false,CancelChecked=false,RelationshipChecked=false;
 double DualActionSeconds=0;
 bool BranchLine=false,RelationshipPending=false;
 bool RelationshipVoiceRequired=false,RelationshipVoiceCompleted=false;
 bool RecoveryChecked=false,RecoveryAttempted=false,ExpectedVoiceAdmissionFault=false;
 int32 ExpectedVoiceAdmissionFailures=0;
 FString TalkFailureNotice;
 int32 VoicedPreviewPhase=0,VoicedPreviewConfirmations=0;
 bool VoicedPreviewChecked=false;
 FOKStreetCandidateProbeSnapshot VoicedPreviewSnapshot;
 FOKStreetStore VoicedPreviewSavedStore;
 FString VoicedPreviewSavePath;
 TArray<FOKStreetAgentSim> VoicedPreviewSavedAgents;
 TArray<FRotator> VoicedPreviewSavedRotations;
 FOKStreetEffect PendingEffect;
 int32 BranchesCompleted=0,RefusalsCompleted=0;
 TArray<FOKStreetVoicedTopicRecord> VoicedEffects;
 int32 PositiveFavorCommits=0;
 bool FavorSaveVerified=false;
 int32 Candidate=INDEX_NONE,CandidateCount=0,CandidateProbePhase=0;
 bool CandidateExplicit=false,CandidateSelectionChecked=false;
 FOKStreetCandidateProbeSnapshot Probe;
 bool Started=false,SelfTest=false,RetargetSelfTest=false,Reported=false,ClipStarted=false,AllReady=false,ConversationShot=false;
 int32 IdentityRetargetApplied=0,IdentityRetargetReplayed=0,IdentityRetargetRejected=0,ActiveActorInterrupts=0,ActiveActorInterruptCommits=0,InterruptPhase=0,InjectFaultSlot=INDEX_NONE;
 bool InjectActiveFault=false,InterruptEndedThisTick=false;
 FString InterruptSaveBefore;
 FOKStreetActorPresence Presence[OKStreetCastCount];
 double StartedAt=0,ReadyAt=0,TestDoneAt=0,NextUI=0,LastWall=0,LineStart=0;
 FRandomStream Random{41729};
 TArray<double> Frame,Game,Render,Rhi,Gpu;
};

// Every system below asks the game rules through these (ok_rules.dll, Tools/ok-rules).
#include "OKStreetSocialRulesGlue.inl"
#include "OKStreetSocialLocale.inl"

AOKStreetSocialScene::AOKStreetSocialScene(){PrimaryActorTick.bCanEverTick=true;}
AOKStreetSocialScene::~AOKStreetSocialScene()=default;

static EOKStreetAudioCompletion OKStreetMapAudioCompletion(EOKRuntimeSpeechState State){
 switch(State){
  case EOKRuntimeSpeechState::Preparing:return EOKStreetAudioCompletion::Preparing;
  case EOKRuntimeSpeechState::Playing:return EOKStreetAudioCompletion::Playing;
  case EOKRuntimeSpeechState::Completed:return EOKStreetAudioCompletion::Completed;
  case EOKRuntimeSpeechState::Cancelled:return EOKStreetAudioCompletion::Cancelled;
  case EOKRuntimeSpeechState::Failed:return EOKStreetAudioCompletion::Failed;
  default:return EOKStreetAudioCompletion::Idle;
 }
}

static FOKStreetTalkCandidateSample OKStreetMakeTalkSample(UWorld* World,const AActor* Owner,const FVector& Male,const FOKStreetAgentSim& Agent,const FOKStreetCharacterMeta* Meta,const FOKRuntimeCharacterStatus* Status,bool Queried){
 FOKStreetTalkCandidateSample Sample;
 Sample.Slot=Agent.Slot;
 Sample.DistanceCm=FVector::Dist2D(Male,Agent.Position);
 // Another floor (a girl on the 9th floor right above him in the department store) is not in reach.
 if(FMath::Abs(Agent.Position.Z-Male.Z)>150.f)Sample.DistanceCm=1.e6f;
 Sample.Female=Meta&&OKStreetIsFemaleTarget(Agent.Slot,*Meta);
 Sample.Offset=Agent.Position-Male;Sample.Offset.Z=0.f;
 Sample.Queried=Queried&&Status!=nullptr;
 Sample.Stale=!Sample.Queried;
 if(Sample.Queried){
  Sample.Ready=Status->Ready;Sample.Visible=Status->Visible;Sample.ActionsReady=Status->ActionsReady;Sample.HasError=!Status->Error.IsEmpty();Sample.Stale=false;
 }
 if(World&&Sample.DistanceCm<OKStreetTalkRangeCm){
  FHitResult Hit;FCollisionQueryParams Query(SCENE_QUERY_STAT(StreetCandidate),false,Owner);
  // A hit right at her own position (her collision, a prop she holds, the chair she stands by) is not something between them.
  // Chest and head height: a counter or a shelf in a shop hides one of them, not both; a wall hides both.
  bool Blocked=true;
  for(const float Height:{120.f,165.f}){Blocked=World->LineTraceSingleByChannel(Hit,Male+FVector(0,0,Height),Agent.Position+FVector(0,0,Height),ECC_Visibility,Query);
   if(!Blocked||FVector::Dist(Hit.ImpactPoint,Agent.Position+FVector(0,0,Height))<80.f){Sample.LineOfSightClear=true;break;}}
  if(Blocked&&!Sample.LineOfSightClear){static double LogAt=0;if(FPlatformTime::Seconds()>LogAt){LogAt=FPlatformTime::Seconds()+3.0;UE_LOG(LogTemp,Display,TEXT("Street E line of sight: slot %d blocked by %s at %s (she is at %s)"),Agent.Slot,*GetNameSafe(Hit.GetActor()),*Hit.ImpactPoint.ToCompactString(),*Agent.Position.ToCompactString());}}
 }
 return Sample;
}

static void OKStreetBuildTalkSamples(UWorld* World,const AActor* Owner,const FVector& Male,const TArray<FOKStreetAgentSim>& Agents,const TArray<FOKStreetCharacterMeta>& Characters,const FOKRuntimeCharacterStatus* Statuses,const bool* Queried,TArray<FOKStreetTalkCandidateSample>& Samples){
 Samples.Reset();
 for(const FOKStreetAgentSim& Agent:Agents){
  const bool Known=Queried&&Statuses&&Agent.Slot>=0&&Agent.Slot<OKStreetCastCount&&Queried[Agent.Slot];
  const FOKStreetCharacterMeta* Meta=Characters.IsValidIndex(Agent.Slot)?&Characters[Agent.Slot]:nullptr;
  Samples.Add(OKStreetMakeTalkSample(World,Owner,Male,Agent,Meta,Known?&Statuses[Agent.Slot]:nullptr,Known));
 }
}

static FOKStreetTalkViewForward OKStreetSceneTalkView(const ACameraActor* Camera,float Yaw){
 return OKStreetTalkCameraHorizontal(Camera?Camera->GetActorForwardVector():FVector::ZeroVector,Yaw);
}

static bool OKStreetVoicedReloadAgrees(const TArray<FOKStreetVoicedTopicRecord>& Effects,const FOKStreetStore& Reloaded,int32 PositiveFavorCommits,int32 CompletedLines){
 if(Effects.Num()!=CompletedLines||CompletedLines!=15)return false;
 int32 LastBySlot[OKStreetCastCount];for(int32 Slot=0;Slot<OKStreetCastCount;++Slot)LastBySlot[Slot]=INDEX_NONE;
 int32 Counted=0;
 for(int32 Index=0;Index<Effects.Num();++Index){
  const FOKStreetVoicedTopicRecord& Effect=Effects[Index];
  if(Effect.AudioCompletion!=TEXT("Completed")||!OKStreetTopicCommitsFavor(Effect.Category)||Effect.VoiceIds.IsEmpty()||Effect.Node.IsEmpty()||Effect.Transaction.IsEmpty()||Effect.Slot<1||Effect.Slot>OKStreetFemaleCount)return false;
  bool Found=false;
  for(const FOKStreetRelation& Row:Reloaded.Relations)if(Row.Id==Effect.Id&&Row.Committed.Contains(Effect.Transaction))Found=true;
  if(!Found)return false;
  if(LastBySlot[Effect.Slot]!=INDEX_NONE){
   const FOKStreetVoicedTopicRecord& Previous=Effects[LastBySlot[Effect.Slot]];
   if(!FMath::IsNearlyEqual(Previous.FavorAfter,Effect.FavorBefore,0.001f)||!FMath::IsNearlyEqual(Previous.LewdAfter,Effect.LewdBefore,0.001f))return false;
  }
  LastBySlot[Effect.Slot]=Index;
  if(OKStreetFavorIncreased(Effect.FavorBefore,Effect.FavorAfter))++Counted;
 }
 for(int32 Slot=1;Slot<=OKStreetFemaleCount;++Slot){
  if(LastBySlot[Slot]==INDEX_NONE)return false;
  const FOKStreetVoicedTopicRecord& Effect=Effects[LastBySlot[Slot]];
  bool FoundRelation=false;float Favor=0.f,Lewdness=0.f;
  for(const FOKStreetRelation& Row:Reloaded.Relations)if(Row.Id==Effect.Id){Favor=Row.Favor;Lewdness=Row.Lewdness;FoundRelation=true;}
  if(!FoundRelation||!FMath::IsNearlyEqual(Favor,Effect.FavorAfter,0.001f)||!FMath::IsNearlyEqual(Lewdness,Effect.LewdAfter,0.001f))return false;
 }
 return Counted==PositiveFavorCommits;
}

// Thin decorative paving is a visual finish over the common Z=0 walk plane,
// never an obstacle. Apply also to previously cooked maps, before actor loading.
static int32 OKStreetNormalizeFloorFinishes(UWorld* World){
 TArray<FOKStreetProp> Props;OKStreetProps(Props);int32 Count=0;
 for(TActorIterator<AStaticMeshActor> It(World);It;++It){
  auto* C=It->GetStaticMeshComponent();if(!C||!C->GetStaticMesh())continue;
  const FBox Box=C->Bounds.GetBox();const FVector Size=Box.GetSize();
  for(const auto& P:Props){
   if(P.Kind!=EOKStreetPropKind::Decor||P.Mesh!=EOKStreetMesh::Cube||P.Size.Z>10.f||P.Size.X<100.f||P.Size.Y<100.f)continue;
   if(FVector::Dist2D(Box.GetCenter(),P.Center)>.5f||!Size.Equals(P.Size,.5f)||Box.Max.Z>10.f||Box.Max.Z<-.5f)continue;
   C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
   if(!FMath::IsNearlyEqual(Box.Max.Z,.1f,.01f)){C->SetMobility(EComponentMobility::Movable);It->AddActorWorldOffset(FVector(0,0,.1f-Box.Max.Z));}
   ++Count;break;
  }
 }
 return Count;
}

static bool OKStreetCreateRuntimeBackdrop(UWorld* World,AActor* Owner,FOKStreetSceneState& State,FString& Error){
 UStaticMeshComponent* Ground=nullptr;
 for(TActorIterator<AStaticMeshActor> It(World);It;++It){
  auto* Component=It->GetStaticMeshComponent();
  if(Component&&Component->GetStaticMesh()&&Component->GetMaterial(0)&&It->GetActorLocation().Z<0.f&&Component->Bounds.BoxExtent.X>1000.f&&Component->Bounds.BoxExtent.Y>1000.f&&Component->Bounds.BoxExtent.Z<50.f){Ground=Component;break;}
 }
 if(!Ground){Error=TEXT("Street background requires the shared scene ground");return false;}
 FActorSpawnParameters Spawn;Spawn.Owner=Owner;Spawn.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
 auto* Background=World->SpawnActor<AStaticMeshActor>(FVector(0,0,-60),FRotator::ZeroRotator,Spawn);
 if(!Background){Error=TEXT("Street background ground spawn failed");return false;}
 State.BackgroundGround=Background;
 auto* Component=Background->GetStaticMeshComponent();Component->SetMobility(EComponentMobility::Movable);
 Component->SetStaticMesh(Ground->GetStaticMesh());Component->SetMaterial(0,Ground->GetMaterial(0));
 Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);Component->SetCastShadow(false);
 // The shared cube is 100cm: 2km wide, top -10cm, below the original walkable ground.
 Background->SetActorScale3D(FVector(2000,2000,1));
 auto* Fog=World->SpawnActor<AExponentialHeightFog>(FVector::ZeroVector,FRotator::ZeroRotator,Spawn);
 if(!Fog){Error=TEXT("Street background fog spawn failed");return false;}
 State.BackgroundFog=Fog;
 auto* Haze=Fog->GetComponent();Haze->SetFogDensity(.002f);Haze->SetFogHeightFalloff(.1f);
 Haze->SetFogInscatteringColor(FLinearColor(.22f,.28f,.34f));Haze->SetFogMaxOpacity(.95f);Haze->SetStartDistance(6000.f);
 Haze->bEnableVolumetricFog=false;
 State.RuntimeBackdropReady=true;
 UE_LOG(LogTemp,Display,TEXT("Street runtime backdrop ready: shared ground, no collision/shadow, fog starts at 60m"));
 return true;
}

static bool OKStreetIsUserRelationshipSave(const FString& SavePath){
 if(SavePath.IsEmpty())return true;
 FString Actual=SavePath;FPaths::NormalizeFilename(Actual);Actual=FPaths::ConvertRelativePathToFull(Actual);
 FString User=FPaths::ProjectSavedDir()/TEXT("StreetSocial/relationships.json");FPaths::NormalizeFilename(User);User=FPaths::ConvertRelativePathToFull(User);
 return Actual.Equals(User,ESearchCase::IgnoreCase);
}

void AOKStreetSocialScene::BeginPlay(){
 Super::BeginPlay();
 if(!GetWorld()->IsGameWorld())return;
 // Render source comparisons in this map with its matching cooked world
 // shaders. The shared scene ticker owns diagnostic playback and capture;
 // StreetSocial gameplay remains inactive when a crowd manifest is supplied.
 FString DiagnosticManifest;
 // The hundred-girl room (CrowdRoom100.cmd): the street's characters, nothing else of the street.
 if(FParse::Value(FCommandLine::Get(),TEXT("CrowdRoom="),DiagnosticManifest)){
  FString RoomError;
  if(!OKStartCrowdRoom(this,DiagnosticManifest,RoomError)){UE_LOG(LogTemp,Error,TEXT("Crowd room: %s"),*RoomError);FPlatformMisc::RequestExitWithStatus(false,1);}
  return;
 }
 if(FParse::Value(FCommandLine::Get(),TEXT("CrowdRuntimeScene="),DiagnosticManifest)){
  FString DiagnosticError;
  if(!OKStartCrowdRuntimeScene(this,DiagnosticManifest,DiagnosticError)){
   UE_LOG(LogTemp,Error,TEXT("Street source comparison: %s"),*DiagnosticError);
   FPlatformMisc::RequestExitWithStatus(false,1);
  }
  return;
 }
 State=MakeShared<FOKStreetSceneState>();auto& S=*State;
 S.TouchReactionOnlyTest=FParse::Param(FCommandLine::Get(),TEXT("StreetSocialTouchReactionSelfTest"));
 S.ContextOnlyTest=FParse::Param(FCommandLine::Get(),TEXT("StreetSocialDialogueContextSelfTest"));
 S.PresentationOnlyTest=FParse::Param(FCommandLine::Get(),TEXT("StreetSocialPresentationSelfTest"));
 S.MovementOnlyTest=FParse::Param(FCommandLine::Get(),TEXT("StreetSocialMovementSelfTest"));
 S.FloorFinishesFixed=OKStreetNormalizeFloorFinishes(GetWorld());
 S.PhotoOnlyTest=FParse::Param(FCommandLine::Get(),TEXT("StreetSocialPhotoSelfTest"));
 S.StartedAt=SceneTime();S.RetargetSelfTest=FParse::Param(FCommandLine::Get(),TEXT("StreetSocialRetargetSelfTest"));S.SelfTest=FParse::Param(FCommandLine::Get(),TEXT("StreetSocialSelfTest"))&&!S.RetargetSelfTest;
 S.AmbientDrinkOnlyTest=FParse::Param(FCommandLine::Get(),TEXT("StreetSocialAutoDrinkSelfTest"));S.SelfTest|=S.AmbientDrinkOnlyTest||S.MovementOnlyTest||S.PresentationOnlyTest;
 S.AmbientSelfieOnlyTest=FParse::Param(FCommandLine::Get(),TEXT("StreetSocialAutoSelfieSelfTest"));S.SelfTest|=S.AmbientSelfieOnlyTest;
 S.AmbientGroomOnlyTest=FParse::Param(FCommandLine::Get(),TEXT("StreetSocialAutoGroomSelfTest"));S.SelfTest|=S.AmbientGroomOnlyTest;
 S.AmbientOnlyTest=FParse::Param(FCommandLine::Get(),TEXT("StreetSocialAutoPhoneSelfTest"));S.SelfTest|=S.AmbientOnlyTest;
 S.PhoneUseOnlyTest=FParse::Param(FCommandLine::Get(),TEXT("StreetSocialPhoneUseSelfTest"));S.SelfTest|=S.PhoneUseOnlyTest;
 S.InvitationOnlyTest=FParse::Param(FCommandLine::Get(),TEXT("StreetSocialInvitationSelfTest"));S.SelfTest|=S.InvitationOnlyTest;
 FParse::Value(FCommandLine::Get(),TEXT("StreetSocialPlaceTour="),S.PlaceTourDir);FParse::Value(FCommandLine::Get(),TEXT("StreetSocialSilenceScale="),S.FreeTalkSilenceScale);if(FParse::Value(FCommandLine::Get(),TEXT("StreetSocialLiveTalkTest="),S.LiveTestWav))S.LiveTestPhase=0;S.FacilityOnlyTest=FParse::Param(FCommandLine::Get(),TEXT("StreetSocialFacilitySelfTest"));S.SelfTest|=S.FacilityOnlyTest;
 S.ContactsOnlyTest=FParse::Param(FCommandLine::Get(),TEXT("StreetSocialContactsSelfTest"));S.SelfTest|=S.ContactsOnlyTest;
 S.SeatedOnlyTest=FParse::Param(FCommandLine::Get(),TEXT("StreetSocialSeatedSelfTest"));S.DrinkOnlyTest=FParse::Param(FCommandLine::Get(),TEXT("StreetSocialDrinkSelfTest"));S.ReadingOnlyTest=FParse::Param(FCommandLine::Get(),TEXT("StreetSocialReadingSelfTest"));S.SplashOnlyTest=FParse::Param(FCommandLine::Get(),TEXT("StreetSocialSplashSelfTest"));S.MinimapOnlyTest=FParse::Param(FCommandLine::Get(),TEXT("StreetSocialMinimapSelfTest"));S.MealOnlyTest=FParse::Param(FCommandLine::Get(),TEXT("StreetSocialMealSelfTest"));S.MelonOnlyTest=FParse::Param(FCommandLine::Get(),TEXT("StreetSocialWatermelonSelfTest"));S.SelfTest|=S.TouchReactionOnlyTest||S.ContextOnlyTest||S.MelonOnlyTest||S.MealOnlyTest||S.MinimapOnlyTest||S.SplashOnlyTest||S.PhotoOnlyTest||S.SeatedOnlyTest||S.DrinkOnlyTest||S.ReadingOnlyTest;
 {const FDateTime Local=FDateTime::Now();S.ClockStartHours=Local.GetHour()+Local.GetMinute()/60.0+Local.GetSecond()/3600.0;S.ClockStartWall=FPlatformTime::Seconds();
  FString Clock;const bool Override=FParse::Value(FCommandLine::Get(),TEXT("StreetSocialClock="),Clock);if(Override){FString H,M;if(!Clock.Split(TEXT(":"),&H,&M))H=Clock;S.ClockStartHours=FMath::Clamp(FCString::Atod(*H),0.0,23.99)+FMath::Clamp(FCString::Atod(*M),0.0,59.0)/60.0;}
  // Self-tests keep the authored sun unless a clock is asked for, so their captures stay comparable.
  S.ClockDrivesSun=Override||!S.SelfTest;
  for(TActorIterator<ADirectionalLight> It(GetWorld());It;++It){S.Sun=*It;It->GetLightComponent()->SetMobility(EComponentMobility::Movable);break;}}
 auto Quality=[](const TCHAR* Name,float Value){if(auto* C=IConsoleManager::Get().FindConsoleVariable(Name))C->Set(Value,ECVF_SetByConsole);};
 Quality(TEXT("r.AntiAliasingMethod"),2);Quality(TEXT("r.ScreenPercentage"),100);Quality(TEXT("r.SecondaryScreenPercentage.GameViewport"),100);Quality(TEXT("r.DynamicRes.OperationMode"),0);Quality(TEXT("r.TemporalAA.Quality"),2);Quality(TEXT("r.TemporalAA.HistoryScreenPercentage"),100);
 // Cascaded shadow maps, not virtual ones: the same toon shadows on screen, and every scene measured ran faster with
 // them (food court 108 -> 125, the girls' dorm 85 -> 130, the dorm lounge full of girls 56 -> 70 FPS).
 Quality(TEXT("r.Shadow.Virtual.Enable"),0);
 // No contrast-adaptive shading rate image (on by default on Windows): it saves GPU shading the toon street does not
 // need (the GPU idles at 3 ms) and its pass costs the render thread (128 -> 132 FPS walking, three runs each).
 Quality(TEXT("r.VRS.ContrastAdaptiveShading"),0);
 // No hardware occlusion queries: the render thread waited on the GPU for last frame's results (~5 ms a frame in the
 // girls' dorm, GPUBound_WaitingForGPUForOcclusionQueries), worst when another game shares the GPU. Drawing what they
 // would have culled costs the GPU ~1 ms (it idles at 3-4 ms); 127 -> 150 and 151 -> 177 FPS in the dorm, A/B in place.
 Quality(TEXT("r.AllowOcclusionQueries"),0);
 int32 ResX=0,ResY=0;
 if(FParse::Value(FCommandLine::Get(),TEXT("ResX="),ResX)&&FParse::Value(FCommandLine::Get(),TEXT("ResY="),ResY)&&ResX>=640&&ResY>=360){if(auto* Settings=GEngine->GetGameUserSettings()){Settings->SetScreenResolution(FIntPoint(ResX,ResY));Settings->SetFullscreenMode(EWindowMode::Windowed);Settings->ApplyResolutionSettings(false);}}
 FString ManifestPath,Json,Error;
 if(!FParse::Value(FCommandLine::Get(),TEXT("StreetSocialManifest="),ManifestPath)||!FFileHelper::LoadFileToString(Json,*ManifestPath)||!OKStreetReadManifestSocial(ManifestPath,Json,S.Manifest)){
  S.Errors.Add(TEXT("Invalid StreetSocial selection: ")+S.Manifest.Error);WriteReport(false);return;
 }
 TSharedPtr<FJsonObject> SelectionRoot;
 if(FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),SelectionRoot)&&SelectionRoot){
  const TArray<TSharedPtr<FJsonValue>>* HostCards=nullptr;if(SelectionRoot->TryGetArrayField(TEXT("cards"),HostCards))for(const auto& C:*HostCards)S.HighCards.Add(OKStreetResolveAgainst(ManifestPath,C->AsString()));
  // The outfit each wears in the street (the crowd reads the same list): the one-on-one scene opens her in it, not outfit 0
  // (Hu Tao walked the street in her white uniform and stood in the scene in a pink nurse's dress).
  {const TArray<TSharedPtr<FJsonValue>>* Outfits=nullptr;if(SelectionRoot->TryGetArrayField(TEXT("outfits"),Outfits))for(const auto& O:*Outfits)S.HighOutfits.Add(FMath::Clamp(int32(O->AsNumber()),0,31));}

  const TSharedPtr<FJsonObject>* Seat=nullptr;
  if(SelectionRoot->TryGetObjectField(TEXT("seated_talk"),Seat)&&Seat&&Seat->IsValid()){(*Seat)->TryGetStringField(TEXT("male"),S.SeatedMaleClip);(*Seat)->TryGetStringField(TEXT("female"),S.SeatedFemaleClip);}
  if(SelectionRoot->TryGetObjectField(TEXT("shared_drink"),Seat)&&Seat&&Seat->IsValid()){(*Seat)->TryGetStringField(TEXT("male"),S.DrinkMaleClip);(*Seat)->TryGetStringField(TEXT("female"),S.DrinkFemaleClip);}
  if(SelectionRoot->TryGetObjectField(TEXT("shared_splash"),Seat)&&Seat&&Seat->IsValid()){(*Seat)->TryGetStringField(TEXT("male_throw"),S.SplashMaleThrow);(*Seat)->TryGetStringField(TEXT("male_receive"),S.SplashMaleReceive);(*Seat)->TryGetStringField(TEXT("female_throw"),S.SplashFemaleThrow);(*Seat)->TryGetStringField(TEXT("female_receive"),S.SplashFemaleReceive);double Cycle=0;if((*Seat)->TryGetNumberField(TEXT("cycle_seconds"),Cycle)&&FMath::IsFinite(Cycle)&&Cycle>1&&Cycle<30)S.SplashCycleSeconds=Cycle;else{S.SplashMaleThrow.Reset();S.SplashMaleReceive.Reset();S.SplashFemaleThrow.Reset();S.SplashFemaleReceive.Reset();}}
  const TSharedPtr<FJsonObject>* Photo=nullptr;
  if(SelectionRoot->TryGetObjectField(TEXT("paired_photo"),Photo)&&Photo&&Photo->IsValid()){
   (*Photo)->TryGetStringField(TEXT("male"),S.PhotoMaleClip);const TArray<TSharedPtr<FJsonValue>>* Groups=nullptr;
   if((*Photo)->TryGetArrayField(TEXT("groups"),Groups))for(const auto& Value:*Groups){
    if(!Value||Value->Type!=EJson::Object)continue;const auto Group=Value->AsObject();FString Clip;const TArray<TSharedPtr<FJsonValue>>* Personalities=nullptr;
    if(Group->TryGetStringField(TEXT("clip"),Clip)&&Group->TryGetArrayField(TEXT("personalities"),Personalities))for(const auto& Personality:*Personalities){double Number=0;if(Personality&&Personality->TryGetNumber(Number)&&Number>=0&&Number<256)S.PhotoPoses.Add(int32(Number),Clip);}
   }
  }
 }
 if(S.Manifest.Characters.Num()<2||S.Manifest.Characters.Num()>OKStreetCastCount||S.Manifest.Characters[0].SexKind!=EOKStreetSex::Male){S.Errors.Add(FString::Printf(TEXT("Expected one male and 1..%d female APNGs"),OKStreetFemaleCount));WriteReport(false);return;}
 for(int32 I=1;I<S.Manifest.Characters.Num();++I)if(!OKStreetIsFemaleTarget(I,S.Manifest.Characters[I])){S.Errors.Add(TEXT("Female metadata validation failed"));WriteReport(false);return;}
 FString Body;
 if(!FFileHelper::LoadFileToString(Body,*S.Manifest.DialogueCatalogue)){S.Errors.Add(TEXT("Missing dialogue catalogue"));WriteReport(false);return;}
 S.Catalogue=OKStreetParseCatalogue(Body,S.Manifest.DialogueCatalogue);
 if(!S.Catalogue.Error.IsEmpty()||S.Catalogue.Nodes.IsEmpty()){S.Errors.Add(TEXT("Catalogue rejected: ")+S.Catalogue.Error);WriteReport(false);return;}
 {TSharedPtr<FJsonObject> ManifestObject;FString JapanesePath,JapaneseBody;
  if(FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),ManifestObject)&&ManifestObject.IsValid()&&ManifestObject->TryGetStringField(TEXT("dialogue_catalogue_ja"),JapanesePath)&&FFileHelper::LoadFileToString(JapaneseBody,*JapanesePath))S.OtherLanguage=OKStreetParseCatalogue(JapaneseBody,JapanesePath);
  LoadVoiceSettings(ManifestObject);
  LoadPlayerName();
 }
 {TSharedPtr<FJsonObject> M;FString Path;if(FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),M)&&M->TryGetStringField(TEXT("invitation_catalogue"),Path)&&!OKStreetLoadInvitations(Path,S.Catalogue,S.OtherLanguage,S.Invitations,Error)){S.Errors.Add(Error);WriteReport(false);return;}}
 if(S.SelfTest)S.Manifest.SavePath=FPaths::GetPath(S.Manifest.SocialReport)/TEXT("selftest.relationships.json");
 if(S.RetargetSelfTest&&OKStreetIsUserRelationshipSave(S.Manifest.SavePath)){S.IdentityRetargetRejected=1;S.Errors.Add(TEXT("Retarget self-test refused the user relationships save"));WriteReport(false);return;}
 ApplyPendingSave();
 if(OKStreetLoadSaveFile(S.Manifest.SavePath,S.Store,Error)==EOKStreetLoad::Invalid){S.Errors.Add(TEXT("Relationship save rejected: ")+Error);WriteReport(false);return;}
 // rules.json (favor caps, preferences, confession chance) is read by the rules with the dialogue catalogue (talk.rs).
 {TSharedPtr<FJsonObject> M;if(FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),M)&&M)M->TryGetStringField(TEXT("topic_catalogue"),S.TopicCataloguePath);}
 StartCalendarAndMind();
 {TSharedPtr<FJsonObject> ManifestObject;FString Path,ExpressionBody;bool Enabled=false;if(FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),ManifestObject)&&ManifestObject.IsValid()){ManifestObject->TryGetBoolField(TEXT("enable_face_patterns"),Enabled);if(ManifestObject->TryGetStringField(TEXT("expression_catalogue"),Path)&&FFileHelper::LoadFileToString(ExpressionBody,*Path))S.Expressions=OKStreetParseExpressions(ExpressionBody);else if(Enabled)S.Expressions.Error=TEXT("Enabled facial expressions require the common expression catalogue");if(!S.Expressions.Error.IsEmpty()){S.Errors.Add(S.Expressions.Error);WriteReport(false);return;}}}
 if(S.Manifest.IdentityRetargets.Num()>0||S.RetargetSelfTest){
  if(S.RetargetSelfTest&&S.Manifest.IdentityRetargets.Num()==0){S.IdentityRetargetRejected=1;S.Errors.Add(TEXT("Identity retarget self-test has no explicit requests"));WriteReport(false);return;}
  TArray<FString> Females,Actors;for(int32 Index=0;Index<S.Manifest.Characters.Num();++Index){const FString Id=OKStreetStableId(S.Manifest.Characters[Index]);Actors.Add(Id);if(Index>0)Females.Add(Id);}
  const FString TimeUtc=FDateTime::UtcNow().ToString(TEXT("%Y-%m-%dT%H:%M:%SZ"));
  FOKStreetStore Candidate;FOKStreetRetargetResult Retarget;
  if(!OKStreetRetargetRelations(S.Store,S.Manifest.IdentityRetargets,Females,Actors,TimeUtc,Candidate,Retarget)||!Retarget.Applied){S.IdentityRetargetRejected=1;S.Errors.Add(TEXT("Identity retarget rejected: ")+Retarget.Reason);WriteReport(false);return;}
  if(Retarget.Moved>0){FString SaveError;if(!OKStreetAtomicSave(S.Manifest.SavePath,OKStreetWriteSave(Candidate),SaveError)){S.IdentityRetargetRejected=1;S.Errors.Add(TEXT("Identity retarget save failed: ")+SaveError);WriteReport(false);return;}S.Store=MoveTemp(Candidate);S.IdentityRetargetApplied=1;}
  if(Retarget.Replayed>0)S.IdentityRetargetReplayed=1;
  if(S.RetargetSelfTest){const int32 Ledger=S.Store.Retargets.Num();FOKStreetStore ReplayStore;FOKStreetRetargetResult Replay;if(!OKStreetRetargetRelations(S.Store,S.Manifest.IdentityRetargets,Females,Actors,TimeUtc,ReplayStore,Replay)||!Replay.Applied||Replay.Moved!=0||Replay.Replayed!=S.Manifest.IdentityRetargets.Num()||ReplayStore.Retargets.Num()!=Ledger){S.IdentityRetargetRejected=1;S.Errors.Add(TEXT("Identity retarget replay rejected: ")+Replay.Reason);WriteReport(false);return;}S.IdentityRetargetReplayed=1;}
  UE_LOG(LogTemp,Display,TEXT("StreetSocial identity retarget: moved=%d replayed=%d"),Retarget.Moved,Retarget.Replayed);
 }
 {TSharedPtr<FJsonObject> M;FString TopicPath,TopicBody;
  if(FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),M)&&M&&M->TryGetStringField(TEXT("topic_catalogue"),TopicPath)){
   if(!FFileHelper::LoadFileToString(TopicBody,*TopicPath)||!OKStreetReadTopics(TopicBody,S.CollectedTopics,Error)){S.Errors.Add(TEXT("Collected topic catalogue rejected: ")+Error);WriteReport(false);return;}NameCollectedTopics();
   auto AddParts=[&](TArray<FOKStreetTopicPart>& Parts){for(auto& Part:Parts){if(Part.Line.VoiceFiles.IsEmpty())continue;Part.Line.Id=FString::Printf(TEXT("collected-p%d-%d"),Part.Line.PersonalityId,S.Catalogue.Nodes.Num());Part.Node=S.Catalogue.Nodes.Add(Part.Line);if(!S.OtherLanguage.Nodes.IsEmpty())S.OtherLanguage.Nodes.Add(Part.Line);}};
   for(auto& Profile:S.CollectedTopics.Profiles){for(auto& L:Profile.Listen){AddParts(L.Intro);AddParts(L.Success);AddParts(L.Normal);AddParts(L.Failure);}for(auto& Entry:Profile.Offers)for(auto& Seq:Entry.Value)AddParts(Seq);for(auto& T:Profile.Touch)AddParts(T.Parts);}
  }
 }
 {TSharedPtr<FJsonObject> M;FString Path;if(FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),M)&&M&&M->TryGetStringField(TEXT("relationship_catalogue"),Path)&&!(OKStreetLoadRelationshipDialogue(Path,S.Catalogue,S.OtherLanguage,S.RelationshipDialogues,Error)&&(FillSubtitleScripts(),true))){S.Errors.Add(Error);WriteReport(false);return;}}
 OKStreetMakeAgents(S.Agents,S.Manifest.Characters.Num()-1);S.Rotations.SetNumZeroed(S.Manifest.Characters.Num());S.RenderedRotations.SetNumZeroed(S.Manifest.Characters.Num());
 if(S.Store.HasPlayerSpot&&!S.SelfTest)S.Rotations[0]=S.RenderedRotations[0]=FRotator(0,S.Store.PlayerYaw,0); // facing the way he was (StartCalendarAndMind)
 if(!OKStartCrowdRuntimeScene(this,ManifestPath,Error)){S.Errors.Add(Error);WriteReport(false);return;}
 if(!OKStreetCreateRuntimeBackdrop(GetWorld(),this,S,Error)){S.Errors.Add(Error);WriteReport(false);return;}
 SpawnStreetPlaces();
 OKSetCrowdRuntimeCharacterMotion(this,0,FTransform(FRotator::ZeroRotator,S.Male),0,Error);
 for(const auto& Agent:S.Agents)OKSetCrowdRuntimeCharacterMotion(this,Agent.Slot,FTransform(FRotator::ZeroRotator,Agent.Position),0,Error);
 auto* Camera=GetWorld()->SpawnActor<ACameraActor>();S.Camera=Camera;
 auto& P=Camera->GetCameraComponent()->PostProcessSettings;
 if(!OKApplyCrowdRuntimeDisplayProfile(this,P)){S.Errors.Add(TEXT("Street display profile missing"));WriteReport(false);return;}
 Camera->GetCameraComponent()->PostProcessBlendWeight=1;
 Camera->GetCameraComponent()->FieldOfView=65;
 if(auto* PC=GetWorld()->GetFirstPlayerController()){
  PC->SetViewTarget(Camera);ApplyMouseMode(true);
 }
 S.MouseInput=MakeShared<FOKStreetMouseModeInput>();
 S.MouseInput->Toggle=[Weak=TWeakPtr<FOKStreetSceneState>(State)](){auto Live=Weak.Pin();if(!Live||!GEngine||!GEngine->GameViewport||!GEngine->GameViewport->Viewport||!GEngine->GameViewport->Viewport->IsForegroundWindow())return false;Live->MouseToggleRequested=!Live->MouseToggleRequested;return true;};
 S.MouseInput->PointerDown=[Weak=TWeakPtr<FOKStreetSceneState>(State)](FVector2D Point){if(auto Live=Weak.Pin()){Live->WorldTouchPointerBlocked=!Live->InteractionPanel.IsValid()||Live->InteractionPanel->GetCachedGeometry().GetLocalSize().IsNearlyZero()||Live->InteractionPanel->GetCachedGeometry().IsUnderLocation(Point)||(Live->DialoguePanel.IsValid()&&Live->DialoguePanel->GetVisibility()!=EVisibility::Collapsed&&Live->DialoguePanel->GetCachedGeometry().IsUnderLocation(Point))||(Live->MinimapPanel.IsValid()&&Live->MinimapPanel->GetCachedGeometry().IsUnderLocation(Point))||Live->BigMapOpen;}};
 if(FSlateApplication::IsInitialized())FSlateApplication::Get().RegisterInputPreProcessor(S.MouseInput,0);
 // Common meshes only; no APNG-private authored pickup assets.
 // Cooked street sphere + the emissive Lamp material: the engine sphere has no material and renders black under the street light.
 {TArray<FOKStreetPointData> Points;OKStreetWaypoints(Points);auto* Mesh=LoadObject<UStaticMesh>(nullptr,TEXT("/Game/Scenes/StreetSocial/Meshes/StreetSphere.StreetSphere"));if(!Mesh)Mesh=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Sphere.Sphere"));
  UMaterialInterface* Glow=LoadObject<UMaterialInterface>(nullptr,*FString::Printf(TEXT("/Game/Scenes/StreetSocial/Materials/StreetMaterial%02d.StreetMaterial%02d"),(int32)EOKStreetMaterial::Lamp,(int32)EOKStreetMaterial::Lamp));
  for(int32 I=0;I<S.CollectedTopics.Topics.Num()&&!Points.IsEmpty();++I){const int32 No=S.CollectedTopics.Topics[I].No;FVector Location=Points[I%Points.Num()].Position+FVector((I/Points.Num())*75.f,0,70);if(OKStreetBlockedPoint(Location,35))Location=Points[I%Points.Num()].Position+FVector(0,0,70+(I/Points.Num())*40.f);auto* Pickup=GetWorld()->SpawnActor<AStaticMeshActor>(Location,FRotator::ZeroRotator);if(!Pickup)continue;Pickup->SetOwner(this);auto* C=Pickup->GetStaticMeshComponent();C->SetMobility(EComponentMobility::Movable);C->SetStaticMesh(Mesh);if(Glow)C->SetMaterial(0,Glow);C->SetCollisionEnabled(ECollisionEnabled::NoCollision);C->SetCastShadow(false);Pickup->SetActorScale3D(FVector(.35));S.TopicPickups.Add(Pickup);S.PickupTopics.Add(No);}
 }
 S.Started=true;RefreshUI();
 UE_LOG(LogTemp,Display,TEXT("StreetSocial independent scene started: 1 male + 5 runtime APNG females, %d catalogue nodes"),S.Catalogue.Nodes.Num());
}

static int64 OKStreetMindNow(const FOKStreetSceneState& S); // OKStreetSocialMind.inl
bool AOKStreetSocialScene::BeginTalk(int32 Character){
 auto& S=*State;S.WaitsForHim.Remove(Character); // he found her: her waiting for him is over
 if(S.Pair.Partner!=INDEX_NONE||S.Facility!=INDEX_NONE||S.Companion!=INDEX_NONE)return false;
 if(S.PrivateGirl!=INDEX_NONE)return false; // the one-on-one scene is his conversation now; a street talk would close it
 if(!S.Agents.IsValidIndex(Character-1)||!S.Manifest.Characters.IsValidIndex(Character))return false;
 StopAmbientActivity();
 // He speaks to her while she (or he) is doing a one-person activity beside him (shopping at a shelf, watching the fish): that
 // activity ends first. Its action clips otherwise held the action slots and the greeting waited 60 s and was aborted.
 if(S.SoloGirl!=INDEX_NONE){UE_LOG(LogTemp,Display,TEXT("Street talk: ended the solo activity of actor %d before talking"),S.SoloGirl);EndSolo();}
 FOKRuntimeCharacterStatus Status;
 const bool Queried=OKGetCrowdRuntimeCharacterStatus(this,Character,Status);
 const FOKStreetTalkCandidateSample Sample=OKStreetMakeTalkSample(GetWorld(),this,S.Male,S.Agents[Character-1],&S.Manifest.Characters[Character],Queried?&Status:nullptr,Queried);
 if(!OKStreetTalkCandidateEligible(Sample)){
  if(Sample.Female&&Sample.Queried&&Sample.Visible&&!Sample.ActionsReady){S.Message=OKT(TEXT("ui.scene.action_prep_wait"));RefreshUI();}
  return false;
 }
 const auto& Agent=S.Agents[Character-1];
 EndTalk();S.TalkFailureNotice.Reset();S.MaleAddressed=false;S.Target=Character;S.TalkStartMinute=OKStreetMindNow(S);ProbeVoiceInfo();S.TopicsOpen=false;S.Agents[Character-1].Held=true;
 S.Yaw=(Agent.Position-S.Male).Rotation().Yaw;S.Pitch=-8;
 S.Rotations[Character]=OKStreetFacingMovement(S.Male-Agent.Position,S.Rotations[Character]);
 S.Rotations[0]=OKStreetFacingMovement(Agent.Position-S.Male,S.Rotations[0]);
 S.Message=OKT(TEXT("ui.scene.topic_choice_prompt"));RefreshUI();
 const bool Quiet=S.QuietTalk;
 // They meet as agreed (a date with her kept in the last two hours, or due within the hour): not a stranger's "oh, you
 // want to talk to me?" line; she greets him as the meeting it is, in her own words.
 bool Meeting=false;{const FString HerId=OKStreetStableId(S.Manifest.Characters[Character]);const int64 Now=OKStreetMindNow(S);
  for(const auto& A:S.Store.Appointments)if(A.Heroine==HerId&&A.Kind==TEXT("date")&&!S.MeetingGreeted.Contains(A.Id)&&((A.Status==TEXT("done")&&Now-A.Start<120&&Now-A.Start>-90)||(A.Status==TEXT("pending")&&A.Start-Now<=60))){Meeting=true;S.MeetingGreeted.Add(A.Id);}}
 if(S.QuietTalk)S.QuietTalk=false;
 // She walked over herself: her own opening is already asked for (it knows the meeting from the promises); a second
 // "you met as agreed, greet him" made her greet twice ("you are two minutes early" and then "sit down, I ordered").
 else if(S.SkipGreeting){S.SkipGreeting=false;UE_LOG(LogTemp,Display,TEXT("Street talk: actor=%d came over; she opens in her own words%s"),Character,Meeting?TEXT(" (the meeting)"):TEXT(""));}
 else if(Meeting){S.TellHer.Add(OKT(TEXT("ui.scene.meeting_greet_note")));UE_LOG(LogTemp,Display,TEXT("Street talk: actor=%d met as agreed; she greets him in her own words"),Character);}
 // Otherwise who opens is the rules' (social.rs greeting_route): her own words from her persona when the AI is there,
 // the card's stock line (by the original game's personality, not hers) only when it is not.
 else{FOKFreeTalkSettings Ai;FString Why;auto Ask=OKRulesRequest(TEXT("greeting_route"));Ask->SetBoolField(TEXT("ai_ready"),!S.SelfTest&&!S.RetargetSelfTest&&OKStreetFreeTalkLoadSettings(Ai,Why));
  const TSharedPtr<FJsonObject> Route=OKRulesEval(S,Ask);
  if(Route.IsValid()&&Route->GetStringField(TEXT("route"))==TEXT("ai")){S.TellHer.Add(OKT(TEXT("ui.scene.he_comes_to_talk_note")));UE_LOG(LogTemp,Display,TEXT("Street talk: actor=%d opens in her own words (persona)"),Character);}
  else Topic(TEXT("greeting"));}
 // Whoever starts the conversation (E, she walks up to him, an invitation), it is a free talk with the microphone open.
 if(!Quiet&&!S.SelfTest&&!S.RetargetSelfTest){S.FreeTalkOpen=true;FreeTalkLiveSet(true);}
 return true;
}

// The talk topic he chose: which line or branch she answers with is the rules' (talk.rs talk_topic).
void AOKStreetSocialScene::Topic(const FString& Category){
 if(!State||State->Target==INDEX_NONE||State->Node!=INDEX_NONE||State->RelationshipPending||State->CollectedActive)return;auto& S=*State;
 // Her answer is on its way (she spoke up after his silence): his topic waits for it (TickFreeTalkLive), or her "you went
 // quiet again" arrived right after his question.
 if(S.FreeTalkBusy){S.PendingTopic=Category;UE_LOG(LogTemp,Display,TEXT("Street topic %s waits for her answer in progress"),*Category);return;}
 S.BranchLine=false;S.RelationshipPending=false;S.PlayerLine.Reset();
 const auto& Meta=S.Manifest.Characters[S.Target];
 auto Request=OKRulesTalkRequest(S,TEXT("talk_topic"),OKStreetViewRelation(S.Store,OKStreetStableId(Meta),S.Manifest.TalkBudget),&Meta);
 Request->SetStringField(TEXT("category"),Category);Request->SetStringField(TEXT("previous_node"),S.PreviousNode);Request->SetStringField(TEXT("last_branch"),S.LastBranchId);Request->SetNumberField(TEXT("seed"),S.Random.RandRange(0,MAX_int32-1));
 const TSharedPtr<FJsonObject> Answer=OKRulesEval(S,Request);if(!Answer){S.Message=OKT(TEXT("ui.scene.dialog_rule_no_response"));RefreshUI();return;}
 FString NodeId;
 if(!Answer->TryGetStringField(TEXT("node"),NodeId)){S.Message=Answer->GetStringField(TEXT("message"));RefreshUI();return;}
 const int32* Node=S.Catalogue.ById.Find(NodeId);if(!Node){S.Message=OKT(TEXT("ui.scene.line_not_found"))+NodeId;RefreshUI();return;}
 FString Branch;
 if(Answer->TryGetStringField(TEXT("branch"),Branch)){
  S.PendingEffect=OKRulesReadEffect(*Answer->GetObjectField(TEXT("effect")));S.BranchLine=true;S.LastBranchId=Branch;
  UE_LOG(LogTemp,Display,TEXT("Street branch decision: branch=%s node=%s role=%s roll=%d reason=%s new_round=%d"),*Branch,*NodeId,*Answer->GetStringField(TEXT("role")),int32(Answer->GetNumberField(TEXT("roll"))),*Answer->GetStringField(TEXT("reason")),Answer->GetBoolField(TEXT("new_round"))?1:0);
  QueueNode(*Node,Category,Branch);return;
 }
 {bool NewRound=false;Answer->TryGetBoolField(TEXT("new_round"),NewRound);if(NewRound)UE_LOG(LogTemp,Display,TEXT("Street dialogue new round: category=%s"),*Category);} // not every answer has it (a farewell line): no warning
 QueueNode(*Node,Category);
}

// KKS ADV name tokens: [H...] is the heroine, [P...] the player, whoever speaks the line.
// [X名] full name, [X] / [X姓] family name (as in "[H]さん"), [Xあだ名] given name.
static FString OKStreetFillName(FString Text,const TCHAR* Prefix,const FString& CardName){
 TArray<FString> Parts;CardName.ParseIntoArrayWS(Parts);
 bool Cjk=false;for(const TCHAR C:CardName)if(C>=0x3000)Cjk=true;
 const FString Full=Parts.Num()>1&&Cjk?FString::Join(Parts,TEXT("")):CardName;
 const FString Family=Parts.Num()?Parts[0]:CardName,Given=Parts.Num()?Parts.Last():CardName;
 // The tokens are the catalogue's data (Japanese markers, kept by every subtitle table); a table's own spelling of
 // them (ui.scene.*) is accepted too.
 Text.ReplaceInline(*FString::Printf(TEXT("[%s名]"),Prefix),*Full);Text.ReplaceInline(*FString::Printf(TEXT("[%sあだ名]"),Prefix),*Given);Text.ReplaceInline(*FString::Printf(TEXT("[%s姓]"),Prefix),*Family);
 Text.ReplaceInline(*OKMsg(TEXT("ui.scene.given_name_suffix"), {{TEXT("prefix"), Prefix}}),*Full);
 Text.ReplaceInline(*OKMsg(TEXT("ui.scene.nickname_jp"), {{TEXT("prefix"), Prefix}}),*Given);
 Text.ReplaceInline(*OKMsg(TEXT("ui.scene.family_name_suffix"), {{TEXT("prefix"), Prefix}}),*Family);
 Text.ReplaceInline(*FString::Printf(TEXT("[%s]"),Prefix),*Family);
 return Text;
}
static FString OKStreetFillNames(const FString& Text,const FString& HeroineName,const FString& PlayerName){
 return OKStreetFillName(OKStreetFillName(Text,TEXT("H"),HeroineName),TEXT("P"),PlayerName);
}

// Name used when one side calls the other in TTS mode: the given name (last word of the card name),
// without version digits ("爱莉希雅1.1" -> "爱莉希雅"); card codes such as "C044" give no name.
static FString OKStreetCallName(const FString& CardName){
 TArray<FString> Parts;CardName.ParseIntoArrayWS(Parts);FString Name=Parts.Num()?Parts.Last():CardName;
 FString Clean;for(const TCHAR C:Name)if(!FChar::IsDigit(C)&&C!=TEXT('.')&&C!=TEXT('_')&&C!=TEXT('-'))Clean.AppendChar(C);
 bool Letters=false;for(const TCHAR C:Clean)if(C>=0x3000)Letters=true;
 return Clean.Len()>=2||Letters?Clean:FString();
}

// "「line」" -> "「Name，line」" unless the line already says the name; any opening quote (「『, and “‘" as the
// Simplified and English subtitles write them).
static FString OKStreetAddress(const FString& Text,const FString& Name){
 if(Name.IsEmpty()||Text.IsEmpty()||Text.Contains(Name))return Text;
 static const FString Opening=TEXT("「『“‘\"");int32 Dummy=0;
 int32 At=0;while(At<Text.Len()&&(Opening.FindChar(Text[At],Dummy)||FChar::IsWhitespace(Text[At])))++At;
 return Text.Left(At)+Name+OKT(TEXT("ui.finance.comma"))+Text.Mid(At);
}

// An original line of his (confessions, topics, touches: Chinese only, no Japanese to key a subtitle by) in the
// player's language: the subtitle table's, else (zh-CN) its own Chinese in Simplified (locale.rs script_texts), kept.
static FString OKStreetMaleLine(const FString& Text){
 if(Text.IsEmpty()||GOKStreetLang==TEXT("zh-TW"))return Text;
 const FString Key=OKStreetSubtitleKey(FString(),Text);
 if(const FString* Known=OKStreetSubtitles().Find(Key))return *Known;
 if(GOKStreetLang!=TEXT("zh-CN")||!OKStreetActiveRules())return Text;
 auto Request=MakeShared<FJsonObject>();Request->SetStringField(TEXT("kind"),TEXT("script_texts"));Request->SetStringField(TEXT("lang"),GOKStreetLang);
 Request->SetArrayField(TEXT("texts"),{MakeShared<FJsonValueString>(Text)});FString Error;
 const TSharedPtr<FJsonObject> Answer=OKStreetActiveRules()->Eval(Request,Error);const TArray<TSharedPtr<FJsonValue>>* Out=nullptr;
 if(!Answer||!Answer->TryGetArrayField(TEXT("texts"),Out)||Out->IsEmpty())return Text;
 const FString Line=(*Out)[0]->AsString();OKStreetSubtitles().Add(Key,Line);return Line;
}

// A name as the model writes it back: the request reached it in the player's script (ok-mind converts a Simplified
// game's whole request), so "九十九 過子" comes back as "九十九 过子". A Traditional game the same way round: a card named
// in Simplified ("贞德") comes back as "貞德" (her group-talk lines were dropped: no girl of that name). Kept per name.
static FString OKStreetScriptedName(const FString& Name){
 static TMap<FString,FString> Known;static FString KnownLang;if(KnownLang!=GOKStreetLang){Known.Reset();KnownLang=GOKStreetLang;}
 if((GOKStreetLang!=TEXT("zh-CN")&&GOKStreetLang!=TEXT("zh-TW"))||Name.IsEmpty()||!OKStreetActiveRules())return Name;
 if(const FString* K=Known.Find(Name))return *K;
 auto Request=MakeShared<FJsonObject>();Request->SetStringField(TEXT("kind"),TEXT("script_texts"));Request->SetStringField(TEXT("lang"),GOKStreetLang);
 Request->SetArrayField(TEXT("texts"),{MakeShared<FJsonValueString>(Name)});FString Error;const TArray<TSharedPtr<FJsonValue>>* Out=nullptr;
 const TSharedPtr<FJsonObject> Answer=OKStreetActiveRules()->Eval(Request,Error);
 const FString Scripted=Answer&&Answer->TryGetArrayField(TEXT("texts"),Out)&&Out->Num()?(*Out)[0]->AsString():Name;
 Known.Add(Name,Scripted);return Scripted;
}
// The model's "who" names this girl: the same name, or one containing the other, in either script; never an empty one.
static bool OKStreetNameMatches(const FString& Said,const FString& Name){
 const FString W=Said.TrimStartAndEnd();if(W.IsEmpty()||Name.IsEmpty())return false;
 for(const FString& N:{Name,OKStreetScriptedName(Name)})if(W==N||W.Contains(N)||N.Contains(W))return true;
 return false;
}

void AOKStreetSocialScene::SetSourcePlayerLine(const FString& Text){
 auto& S=*State;S.PlayerLine=S.Manifest.Characters.IsValidIndex(S.Target)?OKStreetFillNames(Text,S.Manifest.Characters[S.Target].Name,S.Manifest.Characters[0].Name):Text;
 if(!S.PlayerLine.IsEmpty())++S.PlayerPromptsShown;
}

// The player's voice lines go to the TTS server on a channel of their own (this game, this scene), numbered by
// MaleSerial, so a line he no longer says (timed out, replaced, the talk ended) can be dropped there with /cancel:
// one engine makes every line in turn, and a long line nobody waits for would hold up hers.
static FString OKStreetMaleTtsChannel(const UObject* Scene){
 return FString::Printf(TEXT("male-%u-%u"),FPlatformProcess::GetCurrentProcessId(),Scene->GetUniqueID());
}
static void OKStreetTtsCancel(const FString& Endpoint,const FString& Channel,uint32 Below){
 if(Endpoint.IsEmpty())return;
 auto Cancel=MakeShared<FJsonObject>();Cancel->SetStringField(TEXT("channel"),Channel);Cancel->SetNumberField(TEXT("below"),double(Below));
 FString Payload;FJsonSerializer::Serialize(Cancel,TJsonWriterFactory<>::Create(&Payload));
 auto Request=FHttpModule::Get().CreateRequest();Request->SetURL(Endpoint/TEXT("cancel"));Request->SetVerb(TEXT("POST"));
 Request->SetHeader(TEXT("Content-Type"),TEXT("application/json; charset=utf-8"));Request->SetContentAsString(Payload);Request->SetTimeout(5.f);Request->ProcessRequest();
}

bool AOKStreetSocialScene::QueueNode(int32 Chosen,const FString& Category,const FString& BranchId){
 auto& S=*State;if(!S.Catalogue.Nodes.IsValidIndex(Chosen)){FailTalk(TEXT("Dialogue node is unavailable"));return false;}
 // A scripted line takes the bottom box back from free talk.
 S.FreeTalkReplay.Reset();S.FreeTalkReplayParts.Reset();S.FreeTalkReplayHold=0;S.FreeTalkSpeechPlayed=S.FreeTalkSpeechLines.Num();S.FreeTalkCaptionPlayer.Reset();S.FreeTalkCaptionHer.Reset();
 TArray<FString> Voices;
 for(const auto& File:S.Catalogue.Nodes[Chosen].VoiceFiles){FString Id,Error;if(!OKAdmitCrowdRuntimeVoice(this,File,Id,Error)){FailTalk(TEXT("Dialogue voice admission failed: ")+Error);return false;}Voices.Add(Id);}
 if(Voices.IsEmpty()){FailTalk(TEXT("Dialogue has no admitted voice sequence"));return false;}
 // Every replacement line revokes the previous invitation transaction.
 if(Category!=TEXT("invitation")){S.InvitationActive=false;S.InvitationSequence.Reset();S.InvitationGirl=INDEX_NONE;S.InvitationCommand=INDEX_NONE;if(Category!=TEXT("greeting"))S.InvitationQueued=INDEX_NONE;}
 // Her life pose (seated with her phone) ends before she speaks: ending it later resumes her behaviour, which
 // cancels the line just started ("Dialogue did not complete audio").
 if(InLifePose(S.Target))StopLifePose(S.Target);
 S.Node=Chosen;S.Voice=0;S.VoiceIds=MoveTemp(Voices);S.ClipStarted=false;S.LineStart=SceneTime();S.LineInterrupted=false;
 S.ActiveCategory=Category;
 S.ActivePromptBranch=BranchId;SetSourcePlayerLine(OKStreetSourcePlayerPrompt(S.Catalogue,Chosen,BranchId));
 // A collected topic supplies the player's own line; it must be in place before the TTS requests below.
 if(!S.MaleLineOverride.IsEmpty()){SetSourcePlayerLine(S.MaleLineOverride);S.MaleLineOverride.Reset();}
 // Street presentation defaults, not a claim that these are each row's KKS pose.
 S.ActiveGesture=Category==TEXT("romance")?TEXT("talk_gesture_03.okanim"):Category==TEXT("intimacy")?TEXT("talk_gesture_07.okanim"):Category==TEXT("farewell")?TEXT("talk_gesture_12.okanim"):TEXT("talk_gesture_00.okanim");S.ListenerStarted=false;
 S.Message=OKStreetFillNames(OKStreetLineLabel(S.Catalogue.Nodes[Chosen].TextZh,S.Catalogue.Nodes[Chosen].TextJp,S.Catalogue.Nodes[Chosen].Id),S.Manifest.Characters[S.Target].Name,S.Manifest.Characters[0].Name);
 const auto& Line=S.Catalogue.Nodes[Chosen];if(!Line.FacialExpression.IsEmpty()&&!S.Expressions.Items.IsEmpty()){FString FaceError;if(const auto* Expression=S.Expressions.Find(Line.PersonalityId,Line.FacialExpression)){if(OKSetCrowdRuntimeFaceExpression(this,S.Target,Expression->Face,Expression->Blink,FaceError)){++S.ExpressionsApplied;UE_LOG(LogTemp,Display,TEXT("Street source expression applied: actor=%d name=%s"),S.Target,*Line.FacialExpression);}else{++S.ExpressionsMissing;S.Errors.Add(TEXT("Source expression rejected: ")+FaceError);}}else{++S.ExpressionsMissing;S.Errors.Add(TEXT("Source expression missing: ")+Line.FacialExpression);}}
 RefreshUI();
 // TTS: the player's line (male tone) first, then her reply; both play through the normal speech path.
 if(S.MaleTtsPending)OKStreetTtsCancel(S.TtsEndpoint,OKStreetMaleTtsChannel(this),S.MaleSerial+1);
 S.TtsPending=false;++S.TtsSerial;S.MaleTtsPending=false;++S.MaleSerial;S.MalePhase=0;S.MaleVoiceId.Reset();
 if(S.TtsEnabled&&!S.Japanese&&!S.TtsCacheDir.IsEmpty()&&S.Manifest.Characters.IsValidIndex(S.Target)){
  // Both sides call each other by name now and then: his first line of a talk always, then about half;
  // her greetings always, then about a third. Subtitles show the same text the voice reads. Not before they know
  // each other's names (strangers).
  const bool Acquainted=OKStreetViewRelation(S.Store,OKStreetStableId(S.Manifest.Characters[S.Target]),S.Manifest.TalkBudget).Stage!=TEXT("stranger");
  if(Acquainted&&!S.PlayerLine.IsEmpty()&&(!S.MaleAddressed||S.Random.FRand()<.5f)){S.PlayerLine=OKStreetAddress(S.PlayerLine,OKStreetCallName(S.Manifest.Characters[S.Target].Name));S.MaleAddressed=true;}
  FString Male=S.PlayerLine;Male.TrimStartAndEndInline();
  if(S.MaleTone!=TEXT("off")&&!Male.IsEmpty()){S.MalePhase=1;RequestTtsLine(Male,S.MaleTone,true);}
  // The line in the player's language (the subtitle table), the same words the subtitle shows.
  FString Spoken=OKStreetFillNames(OKStreetLineLabel(S.Catalogue.Nodes[Chosen].TextZh,S.Catalogue.Nodes[Chosen].TextJp,S.Catalogue.Nodes[Chosen].Id),S.Manifest.Characters[S.Target].Name,S.Manifest.Characters[0].Name);
  // Calling him by name is put in front Chinese-style (OKStreetAddress): only in Chinese.
  if(Acquainted&&GOKStreetLang!=TEXT("en")&&(Category==TEXT("greeting")||S.Random.FRand()<.34f)){const FString Called=OKStreetAddress(Spoken,OKStreetCallName(S.Manifest.Characters[0].Name));if(Called!=Spoken){Spoken=Called;S.Message=Called;}}
  RefreshUI();
  Spoken.TrimStartAndEndInline();if(!Spoken.IsEmpty())RequestTtsLine(Spoken,FString::FromInt(S.Manifest.Characters[S.Target].PersonalityId),false);
 }
 UE_LOG(LogTemp,Display,TEXT("Street dialogue selected: actor=%d node=%s category=%s clips=%d"),S.Target,*S.Catalogue.Nodes[Chosen].Id,*Category,S.VoiceIds.Num());
 return true;
}

#include "OKStreetSocialInvitations.inl"
#include "OKStreetSocialVoicePick.inl"
#include "OKStreetSocialVoice.inl"
#include "OKStreetSocialTouchReaction.inl"

void AOKStreetSocialScene::FinishLine(){
 auto& S=*State;
 if(!S.Catalogue.Nodes.IsValidIndex(S.Node)||!S.Manifest.Characters.IsValidIndex(S.Target)){EndTalk();return;}
 const auto& Line=S.Catalogue.Nodes[S.Node];
 EOKRuntimeSpeechState SpeechState;FString SpeechError;
 const bool Known=OKGetCrowdRuntimeSpeechState(this,S.Target,SpeechState,SpeechError);
 const EOKStreetAudioCompletion Completion=Known?OKStreetMapAudioCompletion(SpeechState):EOKStreetAudioCompletion::Idle;
 // He talked over her: the line ends where he cut in, counts as said, and the talk goes on.
 const bool Interrupted=S.LineInterrupted;S.LineInterrupted=false;
 if(!Interrupted&&!OKStreetVoicedTopicMayCommit(Completion)){FailTalk(TEXT("Dialogue did not complete audio: ")+SpeechError);return;}
 FString FaceError;OKResetCrowdRuntimeFaceExpression(this,S.Target,FaceError);
 if(S.InvitationActive){CompleteInvitation();return;}
 if(S.TouchActive){CompleteTouch();return;}
 if(S.CollectedActive){S.Node=INDEX_NONE;S.VoiceIds.Reset();S.ClipStarted=false;AdvanceCollectedTopic();return;}
 if(S.ActiveCategory==TEXT("greeting")||S.ActiveCategory==TEXT("farewell")){
  const bool Leaving=S.ActiveCategory==TEXT("farewell");if(S.Pair.PhoneUse)++S.PhoneUseVoices;else if(S.Pair.Meal)++S.MealVoices;else if(S.Pair.Seated)++S.SeatedVoices;else if(S.Pair.Drink)++S.DrinkVoices;else if(Leaving)++S.Farewells;else ++S.Greetings;
  S.Node=INDEX_NONE;S.VoiceIds.Reset();S.ClipStarted=false;S.ActiveCategory.Reset();FString Error;ResumeConversationPose(S.Target,Error);
  ResumeConversationPose(0,Error);if(Leaving){if(S.PrivateGirl!=INDEX_NONE)FinishPrivate();else EndTalk();}else if(S.InvitationQueued!=INDEX_NONE){const int32 Command=S.InvitationQueued;S.InvitationQueued=INDEX_NONE;RequestInvitation(S.Target,Command);}else{S.Message=OKT(TEXT("ui.rules.topic_choice_menu_semi"));RefreshUI();}return;
 }
 if(S.ActiveCategory==TEXT("relationship")){
  if(!S.RelationshipPending||!S.RelationshipVoiceRequired){FailTalk(TEXT("Voiced relationship lost its completion context"));return;}
  S.Node=INDEX_NONE;S.VoiceIds.Reset();S.ClipStarted=false;S.BranchLine=false;++S.RelationshipVoices;
  if(!S.RelationshipSequence.IsEmpty()){AdvanceRelationshipDialogue();return;}
  S.RelationshipVoiceCompleted=true;S.ActiveCategory.Reset();
  FString Error;ResumeConversationPose(S.Target,Error);ResumeConversationPose(0,Error);
  ApplyRelationship();return;
 }
 const FString Transaction=FGuid::NewGuid().ToString(EGuidFormats::Digits);
 const FString Stable=OKStreetStableId(S.Manifest.Characters[S.Target]);
 const FOKStreetRelation Before=OKStreetViewRelation(S.Store,Stable,S.Manifest.TalkBudget);
 auto Candidate=S.Store;
 // Street-local social reward, distinct from the original dialogue row effects.
 // Generic KKS lines have zero row favor; unsupported success/failure branches
 // must not be chosen merely to manufacture a positive original result.
 FOKStreetEffect Effect=S.BranchLine||S.RelationshipPending?S.PendingEffect:Line.Effect;if(!S.BranchLine&&!S.RelationshipPending){Effect.Favor+=1.f;Effect.Memory=Line.Id;Effect.CooldownDays=1;}
 auto Result=OKRulesTalkCommit(S,Candidate,Stable,Transaction,Effect);
 const int32 PreviousCommands=S.TopicCommandCounts.FindRef(Line.Command);
 // The line's topic, and whether it drops into his stock (talk.rs topic_drop).
 int32 DropTopic=INDEX_NONE;bool Dropped=false;
 {auto Drop=OKRulesRequest(TEXT("topic_drop"));Drop->SetStringField(TEXT("node"),Line.Id);Drop->SetNumberField(TEXT("count"),PreviousCommands+1);Drop->SetNumberField(TEXT("roll"),S.Random.RandRange(0,99));
  auto Stock=MakeShared<FJsonObject>();for(const auto& P:Candidate.TopicStock)Stock->SetNumberField(FString::FromInt(P.Key),P.Value);Drop->SetObjectField(TEXT("stock"),Stock);
  if(const TSharedPtr<FJsonObject> Out=OKRulesEval(S,Drop)){DropTopic=int32(Out->GetNumberField(TEXT("topic")));Dropped=Result.Applied&&Out->GetBoolField(TEXT("dropped"));if(Dropped)++Candidate.TopicStock.FindOrAdd(DropTopic);}}
 FString Error;
 if(!Result.Applied||!OKStreetAtomicSave(S.Manifest.SavePath,OKStreetWriteSave(Candidate),Error)){S.Errors.Add(TEXT("Dialogue save failed: ")+Result.Reason+Error);S.Message=OKT(TEXT("ui.scene.dialog_saved_failed"))+Error;}
 else{
  S.Store=MoveTemp(Candidate);
  if(DropTopic>=0)S.TopicCommandCounts.Add(Line.Command,PreviousCommands+1);
  if(OKStreetTopicCommitsFavor(S.ActiveCategory)&&!S.RelationshipPending){FOKHeroineArgs A;A.RecordOnly=true;
   A.Detail=S.ActiveCategory==TEXT("romance")?OKT(TEXT("ui.scene.love_topic_label")):S.ActiveCategory==TEXT("intimacy")?OKT(TEXT("ui.scene.private_topics_label")):OKT(TEXT("ui.scene.daily_topics_label"));Interact(S.Target,TEXT("topic"),A);}
  const FOKStreetRelation After=OKStreetViewRelation(S.Store,Stable,S.Manifest.TalkBudget);
  S.Transactions.Add(Transaction);if(!S.RelationshipPending)++S.CompletedLines;if(S.BranchLine){++S.BranchesCompleted;S.RefusalsCompleted+=Line.Roles.Contains(TEXT("failure"))||Line.Roles.Contains(TEXT("no_interested"));}S.PreviousNode=Line.Id;
  if(OKStreetTopicCommitsFavor(S.ActiveCategory)){
   if(S.VoicedEffects.Num()<OKStreetMaxVoicedTopicEffects){
    FOKStreetVoicedTopicRecord Record;Record.Slot=S.Target;Record.Id=Stable;Record.Node=Line.Id;Record.Category=S.ActiveCategory;Record.Transaction=Transaction;Record.VoiceIds=S.VoiceIds;
    Record.FavorBefore=Before.Favor;Record.FavorAfter=After.Favor;Record.LewdBefore=Before.Lewdness;Record.LewdAfter=After.Lewdness;Record.AudioCompletion=OKStreetAudioCompletionName(Completion);
    S.VoicedEffects.Add(MoveTemp(Record));if(OKStreetFavorIncreased(Before.Favor,After.Favor))++S.PositiveFavorCommits;
   }
   S.Message=OKStreetFormatSavedTopicResult(Before.Favor,After.Favor,Before.Lewdness,After.Lewdness);
  }
 }
 if(Dropped)if(const auto* T=S.CollectedTopics.Find(DropTopic))S.Message+=OKT(TEXT("ui.scene.topic_gained_label"))+T->Name+OKMsg(TEXT("ui.scene.topic_held_count"), {{TEXT("count"), S.Store.TopicStock.FindRef(T->No)}});
 S.Node=INDEX_NONE;S.VoiceIds.Reset();S.ClipStarted=false;S.RelationshipPending=false;S.BranchLine=false;S.ActiveCategory.Reset();
 ResumeConversationPose(S.Target,Error);ResumeConversationPose(0,Error);RefreshUI();
}

void AOKStreetSocialScene::Farewell(){
 if(!State||State->Target==INDEX_NONE)return;
 if(OKStreetConversationBusy(State->Node!=INDEX_NONE,State->CollectedActive,State->RelationshipPending,State->TouchActive)){if(State->PrivateGirl!=INDEX_NONE)FinishPrivate();else EndTalk();return;}
 Topic(TEXT("farewell"));if(State->Node==INDEX_NONE){if(State->PrivateGirl!=INDEX_NONE)FinishPrivate();else EndTalk();}
}

// Slot -> name, for coloring lines by who says them (OKStreetSocialSpeakerText.h).
static TArray<FString> OKStreetCharacterNames(const FOKStreetSceneState& S){TArray<FString> Out;for(const auto& C:S.Manifest.Characters)Out.Add(C.Name);return Out;}
static FString OKStreetFreeTalkCaption(const FOKFreeTalkLine& Line){return Line.Player?Line.Speaker+OKT(TEXT("ui.party.speech_open_quote"))+Line.Text+OKT(TEXT("ui.party.closing_quote_mark")):Line.Speaker+OKT(TEXT("ui.economy.colon_mark"))+Line.Text;}

// Her values as shown in the "女角數值" panel, for the last user message of a free-talk request.
static FOKFreeTalkStatus OKStreetFreeTalkStatusOf(const FOKStreetSceneState& S,const FOKStreetRelation& R){
 FOKFreeTalkStatus Status;
 Status.Relation=R.Stage==TEXT("lover")?OKT(TEXT("ui.bonds.lover_stage")):R.Stage==TEXT("friend")?OKT(TEXT("ui.bonds.friend_stage")):OKT(TEXT("ui.scene.stage_new"));
 const int32 Cap=OKRulesFavorLimit(S,R,R.Stage);
 Status.Favor=R.Favor;Status.FavorLimit=Cap==INDEX_NONE?150.f:float(Cap);
 Status.Lewdness=R.Lewdness;Status.SexDesire=R.SexDesire;Status.TalksRemaining=R.TalksRemaining;
 return Status;
}

// One turn's request for her (face to face, by phone, or a message she writes herself): settings, persona, and the
// prefix-cache friendly prompt — fixed system prompt, her append-only memory, then this turn with the values.
bool AOKStreetSocialScene::PrepareFreeTalk(int32 Character,const FString& Text,FOKFreeTalkSettings& Settings,FString& System,FString& UserMessage,FString& Error){QUICK_SCOPE_CYCLE_COUNTER(STAT_Street_PrepareFreeTalk);
 auto& S=*State;FOKFreeTalkPersona Persona;
 {QUICK_SCOPE_CYCLE_COUNTER(STAT_Street_FreeTalkLoadSettings);if(!OKStreetFreeTalkLoadSettings(Settings,Error)||!OKStreetFreeTalkLoadPersona(S.Manifest.Characters[Character].Name,Persona,Error))return false;}
 FreeTalkEnsureLog();
 const FString Id=OKStreetStableId(S.Manifest.Characters[Character]);
 const FOKStreetRelation HerRelation=OKStreetViewRelation(S.Store,Id,S.Manifest.TalkBudget);
 auto Status=OKStreetFreeTalkStatusOf(S,HerRelation);
 // His name as the game has it (the group prompt says it too), once she knows it: past a stranger, or he told her. A
 // stranger meeting him called him by name and said 爱丽丝 had told her (R4-1).
 if(S.Manifest.Characters.IsValidIndex(0)&&!S.Manifest.Characters[0].Name.IsEmpty()){const FString& HisName=S.Manifest.Characters[0].Name;
  bool Knows=HerRelation.Stage!=TEXT("stranger");
  if(!Knows)for(const FOKStreetHistoryEntry& E:HistoryOf(Id))if(E.Who==TEXT("him")&&E.Text.Contains(HisName)){Knows=true;break;}
  // Not known yet: said so, or she takes a name from around her (刻晴 greeted him "早，安可" the first time, R5-1).
  Status.Relation+=Knows?OKMsg(TEXT("ui.free_talk.his_name_note"), {{TEXT("name"), *HisName}}):FString(OKT(TEXT("ui.free_talk.his_name_unknown_note")));}
 // Apart (a phone message or call): what they are doing face to face does not apply (OKStreetSocialPhoneInbox.inl).
 {QUICK_SCOPE_CYCLE_COUNTER(STAT_Street_FreeTalkDoing);Status.Doing=RemoteWith(Character)?RemoteDoing(Character):FreeTalkDoing()+FreeTalkExtras();}Status.Nearby=RemoteWith(Character)?FString(OKT(TEXT("ui.free_talk.remote_nearby_unknown"))):FreeTalkNearby();
 // Alone with him in the private room (the H scene): not where the street left her, nobody around to see or hear.
 if(S.PrivateGirl==Character&&S.PrivateMode.IsValid()){Status.Doing=OKT(TEXT("ui.free_talk.private_doing"));Status.Nearby=OKT(TEXT("ui.free_talk.private_nearby"));} // the places around him are not around her on the phone
 {const double Hours=OKStreetClockHours(S.ClockStartHours,FPlatformTime::Seconds()-S.ClockStartWall);Status.Now=OKStreetFreeTalkDateTime(OKStreetCalendarDateTime(S.Store.CalendarDay,Hours));Status.StreetTime=OKStreetFreeTalkClock(Hours);}
 {QUICK_SCOPE_CYCLE_COUNTER(STAT_Street_FreeTalkRecall);Status.Memories=MindRecallText(Id,Text);}{QUICK_SCOPE_CYCLE_COUNTER(STAT_Street_FreeTalkHistory);Status.Memories+=HistoryPromptText(Character);}
 {QUICK_SCOPE_CYCLE_COUNTER(STAT_Street_FreeTalkMindStatus);FillMindStatus(Character,Status);}
 // The private room is not the street: no shop menu to order from, no open choices or invitations (her needs stay:
 // at satiety 23 she is hungry there too).
 // Nor the street place's scene line (the lounge, the crowd, who of hers is standing a few metres away): the room is theirs.
 if(S.PrivateGirl==Character&&S.PrivateMode.IsValid()){Status.Menu.Reset();Status.Choices.Reset();Status.Invite.Reset();Status.Scene=OKT(TEXT("ui.free_talk.private_scene"));}
 System=OKStreetFreeTalkSystemPrompt(Persona,OKStreetPlaceCatalogue());UserMessage=OKStreetFreeTalkUserMessage(Status,Text);
 return true;
}

bool AOKStreetSocialScene::HerReplyUnspoken() const{
 const auto& S=*State;return S.FreeTalkSpeechPlayed<S.FreeTalkSpeechLines.Num()&&FPlatformTime::Seconds()-S.FreeTalkSpeechRepliedAt<20.0;
}
// The held game notes, in order, once she has said her reply and is free; with the talk over they have nobody to go to.
void AOKStreetSocialScene::TickDeferredNotes(){
 auto& S=*State;if(S.DeferredNotes.IsEmpty())return;
 if(FreeTalkPartner()==INDEX_NONE){UE_LOG(LogTemp,Display,TEXT("Street free talk: %d held game notes dropped, the talk is over"),S.DeferredNotes.Num());S.DeferredNotes.Reset();return;}
 if(S.FreeTalkBusy||HerReplyUnspoken()||!S.FreeTalkDraft.IsEmpty())return;
 S.SystemNote=true;S.FreeTalkDraft=S.DeferredNotes[0];S.DeferredNotes.RemoveAt(0);SendFreeTalk();
}
void AOKStreetSocialScene::SendFreeTalk(){QUICK_SCOPE_CYCLE_COUNTER(STAT_Street_SendFreeTalk);
 const int32 Character=FreeTalkPartner();
 if(!State||Character==INDEX_NONE||State->FreeTalkBusy)return;auto& S=*State;
 const FString Text=S.FreeTalkDraft.TrimStartAndEnd();
 if(Text.IsEmpty())return;
 if(S.Bluff.Active&&!OKStreetFreeTalkIsNudge(Text)&&BluffHear(Text)){S.FreeTalkDraft.Reset();return;} // a play or a call at the card table
 // What he said, for the rules that read it (who pays: shop.rs chat_order_payer), one on one or to the group alike. A
 // turn the game wrote for her (a till note, "you just walked in", a nudge) is not his words.
 const bool His=!OKStreetFreeTalkIsNudge(Text)&&!S.SystemNote;S.SystemNote=false;
 // While she sings to him his words wait (kept in the box): her answer would be drowned by the song, unshown and
 // unheard, and still decide things ("my treat" to 惠惠 mid-song: her unheard "no, mine" made her pay, R6-34).
 if(His&&SongIsSinging()&&S.Song.Girl==Character){S.FreeTalkNotice=OKMsg(TEXT("ui.song.wait_until_sung"),{{TEXT("name"),S.Manifest.Characters[Character].Name}});RefreshUI();UE_LOG(LogTemp,Display,TEXT("Street free talk: his line waits, actor=%d is singing"),Character);return;}
 // A note the game writes for her (a dish served, a notice, a nudge) while her last reply is not yet spoken waits for
 // it: its reply would cut hers off unheard ("is your thin wallet all right?" got only "the panini is here").
 if(!His&&HerReplyUnspoken()){S.DeferredNotes.Add(Text);S.FreeTalkDraft.Reset();UE_LOG(LogTemp,Display,TEXT("Street free talk: game note held until her reply is spoken (%d held)"),S.DeferredNotes.Num());return;}
 // In the private room the street's own notes for her (a shop closing, a table served, a nudge) do not reach them:
 // only the H scene's (they carry its context). The cafe closing sent her home from the room ("I should go back to the dorm").
 if(!His&&S.PrivateGirl==Character&&S.PrivateMode.IsValid()&&S.PrivateTalkContext.IsEmpty()){UE_LOG(LogTemp,Display,TEXT("Street free talk: street note dropped in the private room: %s"),*Text.Left(60));S.FreeTalkDraft.Reset();return;}
 // In the H scene his words not from its panel (the microphone) go the same way: held while she cannot answer.
 if(His&&S.PrivateGirl==Character&&S.PrivateMode.IsValid()&&S.PrivateTalkContext.IsEmpty()){FString Context;if(!S.PrivateMode->HSceneHear(Text,Context)){S.FreeTalkDraft.Reset();return;}S.PrivateTalkContext=Context;}
 if(His){S.LastPlayerSaid=Text;S.PlayerAskedText=Text;SongCountLines(Character,1);} // his line counts toward her next song
 // His line aloud in his tone (the dropdown's x-vector voice), face to face, when the voice is TTS: her answer waits
 // until he has said it (TickFreeTalkSpeech, the galgame box).
 if(His&&S.Node==INDEX_NONE&&S.TtsEnabled&&!S.Japanese&&!S.TtsCacheDir.IsEmpty()&&S.MaleTone!=TEXT("off")&&RemoteKind(Character)==0){
  S.MalePhase=1;S.MaleVoiceId.Reset();RequestTtsLine(Text,S.MaleTone,true);}
 if(His){++S.HisLines;{const int64 Now=OKStreetMindNow(S);if(S.SaidToHerWith!=Character||Now-S.SaidToHerAt>45){S.SaidToHer.Reset();S.SaidToHerWith=Character;}S.SaidToHerAt=Now;}S.SaidToHer.Add(Text);if(S.SaidToHer.Num()>4)S.SaidToHer.RemoveAt(0);}
 // Called by another girl's name: she sees his words and her own name, and notices it herself (no name lookup here).
 // A note the game wrote for her (a till note, "you are on shift till three"): in the group prompt the model writes
 // every girl's lines, so its "you" is named; Encore took Alice's shift for his. His line goes to the galgame box first
 // (FreeTalkCaptionPlayer, set below only for one girl): in a group talk his question never showed.
 if(S.Party.ContainsByPredicate([Character](int32 G){return G!=Character;})&&RemoteKind(Character)==0){if(His)S.FreeTalkCaptionPlayer=S.Manifest.Characters[0].Name+OKT(TEXT("ui.party.speech_open_quote"))+Text+OKT(TEXT("ui.party.closing_quote_mark"));
  // The wheel rolled back in a group: every girl in it goes back to before that line; then each one's point to return to.
  if(His){TArray<int32> In={Character};for(const int32 Member:S.Party)In.AddUnique(Member);TArray<FString> Ids;for(const int32 Member:In)Ids.Add(OKStreetStableId(S.Manifest.Characters[Member]));
   GalgameRollBack(Ids);if(!OKStreetFreeTalkIsNudge(Text))for(int32 K=0;K<In.Num();++K)GalgameCheckpoint(In[K],Ids[K]);}
  SendPartyTalk(His?Text:OKMsg(TEXT("ui.party.note_for_partner"),{{TEXT("name"),*SpokenName(Character)},{TEXT("note"),Text}}));return;} // several of them: one reply for all (OKStreetSocialParty.inl)
 // Said while the scene shows an earlier line (rolled back with the wheel): what came after it is undone first; then
 // the point this line can be rolled back to.
 // Only his own words roll back: a turn the game wrote for her (her "we're friends now" once the favour reached it, a
 // till note) came while he had scrolled back and undid the lines after that point, as if he had spoken there.
 {const FString RollId=OKStreetStableId(S.Manifest.Characters[Character]);if(His)GalgameRollBack({RollId});if(!OKStreetFreeTalkIsNudge(Text))GalgameCheckpoint(Character,RollId);}
 FOKFreeTalkSettings Settings;FString Error,System,UserMessage;
 if(!PrepareFreeTalk(Character,Text,Settings,System,UserMessage,Error)){S.FreeTalkNotice=Error;RefreshUI();return;}
 // In the H scene: what she is in the middle of (pose, how fast, how close), in the turn message.
 if(S.PrivateGirl==Character&&!S.PrivateTalkContext.IsEmpty()){UserMessage+=TEXT("\n")+S.PrivateTalkContext;S.PrivateTalkContext.Reset();}
 // The reply is handled in the mode this was sent in (phone or face to face), whatever happens meanwhile.
 S.SentRemoteGirl=RemoteWith(Character)?Character:INDEX_NONE;S.SentRemoteMode=RemoteKind(Character);
 const FString Id=OKStreetStableId(S.Manifest.Characters[Character]);
 // The bottom dialogue box shows the exchange: his line now, hers when the reply arrives.
 S.FreeTalkReplay.Reset();S.FreeTalkReplayHold=0;
 // A goodbye read from what was said (the judge, or her end_talk) is off once he goes on talking: her answer to this
 // line decides again. Otherwise she walks off minutes later in the middle of whatever they agreed since.
 if(!OKStreetFreeTalkIsNudge(Text)&&S.FarewellPending&&S.FarewellFromTalk){S.FarewellPending=false;S.FarewellFromTalk=false;UE_LOG(LogTemp,Display,TEXT("Street farewell: actor=%d called off, he goes on talking"),Character);}
 if(!OKStreetFreeTalkIsNudge(Text))S.FarewellAfterMeal=INDEX_NONE; // nor after the meal: he goes on talking
 // Any line of his (typed or spoken) ends a silence: its count and a leave it led to are off.
 // A note the game wrote for her (a till note, "he just confessed") is neither: it is not shown as his line either.
 if(His){S.FreeTalkSilenceSince=FPlatformTime::Seconds();S.FreeTalkSilencePenalties=0;S.FreeTalkLeavePending=false;}
 if(!His)S.FreeTalkCaptionPlayer.Reset();else S.FreeTalkCaptionPlayer=S.Manifest.Characters[0].Name+OKT(TEXT("ui.party.speech_open_quote"))+Text+OKT(TEXT("ui.party.closing_quote_mark"));
 S.FreeTalkCaptionHer=OKStreetScriptedName(S.Manifest.Characters[Character].Name)+OKT(TEXT("ui.scene.ellipsis_colon"));
 S.FreeTalkBusy=true;S.FreeTalkSentAt=SceneTime();++S.FreeTalkSent;S.FreeTalkDraft.Reset();S.FreeTalkNotice=OKT(TEXT("ui.scene.she_is_thinking"));
 // The heroine mind keeps her whole context and the full state of every turn (plan §7.0); without it,
 // the request goes straight to DeepSeek with the trimmed local memory.
 if(!SubmitFreeTalkToMind(Character,Id,System,UserMessage,Text))FinishFreeTalk(Character,Id,Text,nullptr,OKT(TEXT("ui.scene.ai_service_off_reply")));
 RefreshUI();
}

// A chat job for the heroine mind (ok-mind): it calls the model (retrying, pacing, logging) and the answer comes back
// in TickMind, which hands it to Done (ok, content, error).
uint64 AOKStreetSocialScene::SubmitMindChat(const FString& Tag,const FString& Priority,const TArray<TSharedPtr<FJsonValue>>& Messages,int32 MaxTokens,float Temperature,TFunction<void(bool,const FString&,const FString&)> Done){
 auto& S=*State;if(!S.Mind){Done(false,FString(),OKT(TEXT("ui.scene.ai_service_off")));return 0;}
 TSharedRef<FJsonObject> Job=MakeShared<FJsonObject>();
 Job->SetStringField(TEXT("kind"),TEXT("chat"));Job->SetStringField(TEXT("tag"),Tag);Job->SetStringField(TEXT("priority"),Priority);
 Job->SetArrayField(TEXT("messages"),Messages);Job->SetNumberField(TEXT("max_tokens"),MaxTokens);Job->SetNumberField(TEXT("temperature"),Temperature);Job->SetBoolField(TEXT("json"),true);
 FString Json,Error;FJsonSerializer::Serialize(Job,TJsonWriterFactory<TCHAR,TCondensedJsonPrintPolicy<TCHAR>>::Create(&Json));
 const uint64 JobId=S.Mind->Submit(Json,Error);
 if(!JobId){Done(false,FString(),Error);return 0;}
 S.MindChats.Add(JobId,MoveTemp(Done));return JobId;
}

// Her reply arrived (directly from DeepSeek, or through the heroine mind): show it, speak it, apply it.
void AOKStreetSocialScene::FinishFreeTalk(int32 Character,const FString& Id,const FString& Text,const FOKFreeTalkReply* Reply,const FString& Failure){
 auto& S=*State;
 S.FreeTalkBusy=false;
 S.ReplyRemoteGirl=S.SentRemoteGirl;S.ReplyRemoteMode=S.SentRemoteMode;S.SentRemoteGirl=INDEX_NONE;
 if(!Reply){S.ReplyRemoteGirl=INDEX_NONE;S.FreeTalkNotice=Failure;S.FreeTalkDraft=Text;S.FreeTalkCaptionHer.Reset();RefreshUI();return;}
 // In the private room her reply is words only: the street's actions (a meal, an outing, an order, ending the talk) are
 // not hers to start from there, and no judge reads the H talk for them (the judge had her decide to go for a meal).
 FOKFreeTalkReply PrivateReply;const bool InPrivate=S.PrivateGirl==Character&&S.PrivateMode.IsValid();
 if(InPrivate){PrivateReply=*Reply;PrivateReply.Action.Reset();PrivateReply.ActKind.Reset();PrivateReply.ActArg.Reset();PrivateReply.Orders.Reset();PrivateReply.Payer.Reset();
  PrivateReply.ProposeId.Reset();PrivateReply.ResolveId.Reset();Reply=&PrivateReply;}
 S.FreeTalkAnswered=S.FreeTalkSent; // the open choices were in that request (TickChoices)
 // Her memory keeps the exchange (her turn in the same JSON form she answers in); the values travel
 // only with the newest message. With the mind open this copy is only the fallback's memory.
 {QUICK_SCOPE_CYCLE_COUNTER(STAT_Street_FinishTalkMemory);auto& Memory=S.FreeTalkMemory.FindOrAdd(Id);
 Memory.Add({TEXT("user"),Text});Memory.Add({TEXT("assistant"),OKStreetFreeTalkMemoryReply(*Reply)});
 OKStreetFreeTalkTrimMemory(Memory);
 FreeTalkSaveMemory();}
 int64 Serial=0;{QUICK_SCOPE_CYCLE_COUNTER(STAT_Street_FinishTalkLines);if(!OKStreetFreeTalkIsNudge(Text))FreeTalkAddLine(Character,true,Text);
 Serial=FreeTalkAddLine(Character,false,Reply->Reply);}
 S.FreeTalkCaptionHer=OKStreetScriptedName(S.Manifest.Characters[Character].Name)+OKT(TEXT("ui.economy.colon_mark"))+Reply->Reply;
 {QUICK_SCOPE_CYCLE_COUNTER(STAT_Street_FinishTalkFavor);ApplyFreeTalkFavor(Character,Reply->FavorDelta,Reply->Reason);}
 {QUICK_SCOPE_CYCLE_COUNTER(STAT_Street_FinishTalkRelation);ApplyMindRelation(Character,Reply->RelDelta,Reply->Emotion);}
 // Who pays is what they settled: a game note ("you just walked in, order something") is no talk between them, so her
 // "payer" on it is a guess ("him" on entering the cafe, nobody had said a word). Not dropped while a checkout
 // choice is open: then the note asks her exactly that. "her" stays: her own money is hers to offer ("到啦。要不要喝罐饮料？
 // 我请你" at the dorm, and he was charged the ¥5).
 {FOKFreeTalkReply Applied=*Reply;if(OKStreetFreeTalkIsNudge(Text)&&!Applied.Payer.IsEmpty()&&Applied.Payer!=TEXT("her")&&!S.Choices.ContainsByPredicate([](const FOKStreetPendingChoice& C){return C.Kind==TEXT("checkout");})){
   UE_LOG(LogTemp,Display,TEXT("Street pay: actor=%d payer \"%s\" on a game note, not settled by them"),Character,*Applied.Payer);Applied.Payer.Reset();}
  QUICK_SCOPE_CYCLE_COUNTER(STAT_Street_FinishTalkChoice);ApplyChoiceReply(Character,Applied);}
 {QUICK_SCOPE_CYCLE_COUNTER(STAT_Street_FinishTalkExpression);ApplyMindExpression(Character,Reply->Emotion,Reply);}
 // And her body: the gesture she makes with it (motions.toml), face to face only.
 // Every line, as long as she speaks it: with no gesture from the model, one for her mood (motions.toml mood). A still
 // pose under every line left her standing in her idle (the raised finger) through the whole talk.
 if(RemoteKind(Character)==0){QUICK_SCOPE_CYCLE_COUNTER(STAT_Street_FinishTalkGesture);int32 Chars=0;for(const FString& L:Reply->Lines)Chars+=L.Len();PlayGesture(Character,Reply->Gesture,Reply->Emotion,0.6f+0.22f*Chars);}
 // Free-talk replies have no recorded clip, so they always go to the TTS server (independent of the
 // V voice-mode toggle, which only chooses for scripted lines); the text stays on screen either way.
 SongCountLines(Character,Reply->Lines.Num()); // her lines count toward her next song (every 500, OKStreetSocialSong.inl)
 if(!S.TtsCacheDir.IsEmpty()&&RemoteKind(Character)!=1){QUICK_SCOPE_CYCLE_COUNTER(STAT_Street_FinishTalkSpeak);SpeakFreeTalk(Character,Reply->Lines,Serial);} // a text message is read, not heard
 // What happens next (going somewhere now, an act, or a promise for later) is judged by DeepSeek from his words and hers,
 // not by keyword rules (JudgeFreeTalk). Without words of his (a nudge, a notice) she does her own acts but never starts
 // an outing he has not agreed to.
 if(OKStreetFreeTalkIsNudge(Text)){
  if(S.ChatOrderAwaitsJudge.Remove(Character)>0&&!S.Cart.IsEmpty())PlaceChatOrder(Character,Reply->Reply); // no judge comes for a nudge
  const FString A=Reply->Action;
  if(!A.IsEmpty()&&!(A.StartsWith(TEXT("go:"))||A==TEXT("stroll")||A==TEXT("walk")||OKStreetFindActivity(A)||OKStreetFindPlace(A)))ExecuteFreeTalkAction(Character,Id,A);
  // Hers to propose, his to agree: kept for the judge of his next line. On arriving at the park she said "let's find a
  // bench" (park_seat); he said "sure, that bench", and the judge took them as already sitting there.
  else if(!A.IsEmpty()&&A!=TEXT("none"))S.HerProposed.Add(Character,A);
 }
 else if(!InPrivate){QUICK_SCOPE_CYCLE_COUNTER(STAT_Street_FinishTalkJudge);TArray<FString> Ordered;for(const auto& O:Reply->Orders)Ordered.Add(O.Id);JudgeFreeTalk(Character,Id,S.PlayerAskedText.IsEmpty()?Text:S.PlayerAskedText,Reply->Reply,Reply->Action,Ordered);}
 S.PlayerAskedText.Reset();
 S.ReplyRemoteGirl=INDEX_NONE;{QUICK_SCOPE_CYCLE_COUNTER(STAT_Street_FinishTalkUi);RefreshUI();}
}

// Runs what was decided after her reply: an activity or place to go to now, or an act of hers (eat, photo...).
void AOKStreetSocialScene::ExecuteFreeTalkAction(int32 Character,const FString& Id,const FString& RawAction){
 auto& S=*State;
 // The model sometimes writes its act prefix in the activity field ("act:stroll", "act:eat"): the same thing without it.
 const FString ActionNow=RawAction.StartsWith(TEXT("act:"))?RawAction.Mid(4):RawAction;
 if(ActionNow.IsEmpty())return;
 // Her acts (the treat she promised, a goodbye) are not activities: ApplyChoiceReply handles them.
 if(OKStreetReplyActOnly(ActionNow)){UE_LOG(LogTemp,Display,TEXT("Street free talk action: %s is her act, not an activity"),*ActionNow);return;}
 if(S.ActGirl==Character&&S.ActId==ActionNow){UE_LOG(LogTemp,Display,TEXT("Street free talk action: %s is what they are already doing"),*ActionNow);return;}
 // On the way somewhere together, "let's walk" is what they are doing: it does not cancel the trip for a loop nearby.
 if(S.OutingGirl==Character&&(ActionNow==TEXT("walk")||ActionNow==TEXT("stroll"))){UE_LOG(LogTemp,Display,TEXT("Street free talk action: %s while already on the way (%s)"),*ActionNow,*S.OutingActivity);return;}
 // Already on the way there, or already in that place: not again (the walk would start over).
 {const FOKStreetPlace* P=OKStreetFindPlace(ActionNow.StartsWith(TEXT("go:"))?ActionNow.Mid(3):ActionNow);
  if(S.OutingGirl!=INDEX_NONE&&(S.OutingActivity==ActionNow||(P&&S.OutingPlace==P->Name))){UE_LOG(LogTemp,Display,TEXT("Street free talk action: already on the way (%s)"),*ActionNow);return;}
  if(P&&!OKStreetFindActivity(ActionNow)&&PlaceHere(S.Male)==P->Id){UE_LOG(LogTemp,Display,TEXT("Street free talk action: already at %s"),*ActionNow);return;}}
 // On the phone, "I'm coming over to the drink shop now": a meeting there shortly, so she sets out (she said so and
 // then went to the bookshop by her plan while he waited).
 if(RemoteWith(Character)){
  const FOKStreetActivity* Act=OKStreetFindActivity(ActionNow);
  const FOKStreetPlace* Where=OKStreetFindPlace(ActionNow.StartsWith(TEXT("go:"))?ActionNow.Mid(3):Act&&Act->Place?FString(Act->Place):ActionNow);
  const FString HerId=OKStreetStableId(S.Manifest.Characters[Character]);const int64 Now=OKStreetMindNow(S);
  if(Where&&!S.Store.Appointments.ContainsByPredicate([&](const FOKStreetAppointment& A){return A.Status==TEXT("pending")&&A.Heroine==HerId&&FMath::Abs(A.Start-Now)<90;})){
   const FString Label=OKMsg(TEXT("ui.appointments.meet_with_summary"), {{TEXT("character_name"), *S.Manifest.Characters[Character].Name}, {TEXT("what"), OKT(TEXT("ui.appointments.meeting_label"))}, {TEXT("person_name"), Where->Name}});
   AddAppointment(TEXT("date"),Character,Where->Id,Now+20,PatienceOf(Character),Label);
   UE_LOG(LogTemp,Display,TEXT("Street phone: actor=%d comes over to %s now; meeting in 20 minutes"),Character,Where->Id);return;}
  S.FreeTalkNotice=OKT(TEXT("ui.scene.she_agreed_not_together"));return;}
 {
  FString Error;
  UE_LOG(LogTemp,Display,TEXT("Street free talk action: actor=%d action=%s"),Character,*ActionNow);
  FString Why;FString Action=ActionNow;
  // The model sometimes puts an act word (eat, share_food, feed) in the activity field, or offers a photo in the
  // middle of a meal. An act word means "go on with the meal": back to the table's meal when they stand in a place that has
  // one, else just the act. A photo offer is held back while they are eating, drinking, reading or on the phone.
  bool Handled=Action==TEXT("end_talk")||Action==TEXT("farewell")||Action==TEXT("goodbye")||Action==TEXT("promise")||Action==TEXT("date")||Action==TEXT("appointment"); // the goodbye is handled by TickFarewell; a promise belongs in the promise field (OKStreetSocialMind.inl)
  {
   const FOKStreetActivity* Known=OKStreetFindActivity(Action);
   const bool PlaceOrGo=Action.StartsWith(TEXT("go:"))||OKStreetFindPlace(Action)!=nullptr||Action==TEXT("leave");
   if(!Known&&!PlaceOrGo){
    const bool EatingWord=Action==TEXT("eat")||Action==TEXT("share_food")||Action==TEXT("feed")||Action==TEXT("meal");
    const FString Here=PlaceHere(S.Male);
    if(EatingWord&&!(S.Pair.Partner==Character&&(S.Pair.Meal||S.Pair.Seated||S.Pair.Drink)))for(const auto& A:OKStreetActivities())if(A.Kind==EOKStreetActivityKind::Pair&&A.Code==5&&Here==A.Place){UE_LOG(LogTemp,Display,TEXT("Street free talk action: %s means the meal here (%s)"),*Action,A.Id);Action=A.Id;Known=&A;break;}
    // Her own word put in the activity field (she cannot eat more, takes back an order): done as that act, not an outing.
    if(!Known&&(Action==TEXT("stop_eating")||Action==TEXT("cancel_order"))){Handled=HandleWorldAct(Character,Action,FString());UE_LOG(LogTemp,Display,TEXT("Street free talk action: %s is her act, not a place"),*Action);}
    else if(!Known&&EatActBlocked(Character,Action))Handled=true;
    else if(!Known&&OKHeroineFind(Action)){Handled=DoActHers(Character,Action);UE_LOG(LogTemp,Display,TEXT("Street free talk action: %s done as an act (%d)"),*Action,Handled?1:0);}
   }
   if(!Handled&&Known&&Known->Kind==EOKStreetActivityKind::Photo&&S.Pair.Partner==Character&&(S.Pair.Meal||S.Pair.Drink||S.Pair.PhoneUse||S.Pair.Read)){ // just sitting and talking is no reason: they get up for it
    Handled=true;S.FreeTalkNotice=OKT(TEXT("ui.scene.other_activity_no_photo"));S.MindRecent.Add(Id,OKT(TEXT("ui.scene.photo_later_eating")));
    S.TellHer.Add(OKT(TEXT("ui.scene.no_photo_eating_note")));
    UE_LOG(LogTemp,Display,TEXT("Street free talk action: photo held back during meal=%d seated=%d drink=%d"),S.Pair.Meal?1:0,S.Pair.Seated?1:0,S.Pair.Drink?1:0);
   }
  }
  // What she said aloud is what happens: her yes starts the walk. (The mood check used to turn a spoken yes into
  // "she did not really want to", which left the player standing there; it is only logged now.) Only looked at: a yes
  // is not a refusal, so nothing of InviteAllowed's refusal bookkeeping (her note "I said no", "he keeps pestering me",
  // the respect it costs) may happen here.
  if(!Handled){const FString Her=OKStreetStableId(S.Manifest.Characters[Character]);const int32 Kind=OKRulesInviteKind(S,Action);const TArray<bool>* Stance=S.InviteStance.Find(Her);
   if(Stance&&Stance->IsValidIndex(Kind)&&!(*Stance)[Kind])UE_LOG(LogTemp,Display,TEXT("Street free talk action: %s agreed aloud although her rolled stance was no; going"),*Action);
   S.InviteRefusals.Remove(Her);}
  // On her shift (making drinks at the shop): a spoken yes does not take her off work; she is told so, and so is he.
  if(!Handled)if(const int32 Until=ShiftUntil(Character);Until>0){Handled=true;
   const FOKStreetPlanBlock* B=BlockNow(Character,int32(OKStreetMindNow(S)%1440));const FOKStreetPlace* P=B?OKStreetFindPlace(B->Place):nullptr;
   const FString Place=P?FString(P->Name):B?B->Place:FString();const FString At=FString::Printf(TEXT("%d:%02d"),Until/60,Until%60);
   // The activity is at the shop she is working in and he is there: he is her customer (his order is the shop's), not
   // asking her out. Told "on shift, after 22:00" for the latte he ordered from 三月七 at her counter, she said "showing
   // you round has to wait till I'm off" to a man who had only asked for a coffee (R6-25).
   const FOKStreetActivity* Act=OKStreetFindActivity(Action);const FOKStreetPlace* ActAt=OKStreetFindPlace(Act&&Act->Place?FString(Act->Place):Action.StartsWith(TEXT("go:"))?Action.Mid(3):Action);
   if(B&&ActAt&&ActAt->Id==B->Place&&PlaceHere(S.Male)==B->Place){UE_LOG(LogTemp,Display,TEXT("Street free talk action: %s is him served at %s where actor=%d works, not an outing"),*Action,*B->Place,Character);}
   else{
   // Named, not "you": in a group the model writes every girl's lines, and "you are on shift till three" became his.
   S.TellHer.Add(OKMsg(TEXT("ui.invite.on_shift_tell_her"),{{TEXT("name"),SpokenName(Character)},{TEXT("place"),Place},{TEXT("until"),At}}));
   S.Message=OKMsg(TEXT("ui.invite.on_shift_player"),{{TEXT("name"),S.Manifest.Characters[Character].Name},{TEXT("place"),Place},{TEXT("until"),At}});RefreshUI();
   UE_LOG(LogTemp,Display,TEXT("Street free talk action: %s not started, actor=%d is on shift until %s"),*Action,Character,*At);}}
  if(Handled){}
  // In the middle of an activity with him, an action that names no activity or place is her word for what they are doing
  // ("take_photo" while their photo was being taken): told it "failed", she said the camera had died and they gave up
  // the photo they were taking (R3-229).
  else if(S.Pair.Partner==Character&&Action!=TEXT("leave")&&!Action.StartsWith(TEXT("go:"))&&!OKStreetFindActivity(Action)&&!OKStreetFindPlace(Action)){
   UE_LOG(LogTemp,Display,TEXT("Street free talk action: %s during the activity they are in names nothing else: taken as that activity"),*Action);}
  // Drinking or eating what is being made for them here, said as another place's drink or meal: 惠惠 agreed to two
  // vending-machine drinks at the boys' dorm, the judge called it "drink" (the snack shop's), and once the bottles came
  // the two of them walked off to the snack shop with them (R4-13).
  else if(const FOKStreetActivity* Fed=OKStreetFindActivity(Action);Fed&&Fed->Kind==EOKStreetActivityKind::Pair&&(Fed->Code==2||Fed->Code==5)&&Fed->Place&&!PlaceHere(S.Male).Equals(Fed->Place)
   &&S.Table.ContainsByPredicate([Character](const FOKStreetDish& D){return !D.Served&&(!D.ForHer||D.Her==INDEX_NONE||D.Her==Character);})){
   UE_LOG(LogTemp,Display,TEXT("Street free talk action: %s is what they ordered here, not a walk to %s"),*Action,Fed->Place);}
  else if(!BeginOuting(Character,Action,Error)){
   // Only because her scripted line is still playing: she said yes, so it starts as soon as the line ends (TickRetryAction).
   if(Error==OKT(TEXT("ui.scene.wait_her_finish_line"))){S.RetryAction=Action;S.RetryGirl=Character;S.RetryUntil=SceneTime()+25.0;UE_LOG(LogTemp,Display,TEXT("Street free talk action: %s waits for her line to end"),*Action);}
   // Only because what they ordered is still being made (his latte, when she said "let's go and eat"): they go once it
   // is served, not never (TickRetryAction).
   else if(Error==OKT(TEXT("ui.outing.wait_for_order"))){S.RetryAction=Action;S.RetryGirl=Character;S.RetryUntil=SceneTime()+180.0;S.FreeTalkNotice=Error;UE_LOG(LogTemp,Display,TEXT("Street free talk action: %s waits for the order to be served"),*Action);}
   else{
    S.FreeTalkNotice=OKT(TEXT("ui.live_talk.she_agreed_but"))+Error;
    // Which place it was ("there is closed" at the cafeteria door was taken for the cafeteria, not the restaurant).
    FString PlaceName=TEXT("none");{const FOKStreetActivity* A=OKStreetFindActivity(Action);const FString PlaceId=A&&A->Place&&*A->Place?FString(A->Place):FString(); // going somewhere: its name is the action already
     if(const FOKStreetPlace* P=PlaceId.IsEmpty()?nullptr:OKStreetFindPlace(PlaceId))PlaceName=P->Name;}
    // Where they still are: told only that the drink shop was closed while they stood in the girls' dorm, Megumin said
    // "and we walked all the way to the door".
    FString Here=TEXT("none");for(const auto& P:OKStreetPlaces())if(OKStreetInsidePlace(P,S.Male)){if(P.Name!=PlaceName)Here=P.Name;break;}
    S.TellHer.Add(OKMsg(TEXT("ui.live_talk.promise_failed_explain"), {{TEXT("action"), *OKStreetActivityName(Action)}, {TEXT("place"), *PlaceName}, {TEXT("here"), *Here}, {TEXT("error"), *Error}}));
   }UE_LOG(LogTemp,Display,TEXT("Street free talk action refused: %s (%s)"),*Action,*Error);}
  // "leave" ends the activity they are in: not an outing she agreed to ("he went to leave with her" in the rumours).
  else if(Action!=TEXT("leave"))HeroineNote(Character,TEXT("outing"),OKMsg(TEXT("ui.scene.accepted_date_activity"), {{TEXT("activity"), *OKStreetActivityName(Action)}}),TEXT(""),-1.f,OKStreetActivityName(Action)); // words, not the id (rumours repeat it)
 }
}

// DeepSeek judges, from what he said and what she answered, whether they do something now (and what) or promised to meet
// later (day, time, place); the game then does exactly that. If that request fails, her own action field is used.
static int64 OKStreetMindNow(const FOKStreetSceneState& S);
void AOKStreetSocialScene::JudgeFreeTalk(int32 Character,const FString& Id,const FString& Said,const FString& Her,const FString& HerAction,const TArray<FString>& HerOrders){
 auto& S=*State;FOKFreeTalkSettings Settings;FString Error;
 {QUICK_SCOPE_CYCLE_COUNTER(STAT_Street_JudgeSettings);if(!OKStreetFreeTalkLoadSettings(Settings,Error)){ExecuteFreeTalkAction(Character,Id,HerAction);return;}}
 // Built once per language (it is the cached prefix of every judge request); a language switch builds it again, and so
 // does a rules reload (the text tables are read again: a prompt edit applies without a restart).
 static FString System,SystemLang;static int32 SystemGeneration=-1;
 const TCHAR* JudgePrompt=OKT(TEXT("ui.scene.ai_judge_prompt"));
 if(System.IsEmpty()||SystemLang!=S.Lang||SystemGeneration!=OKStreetCppText().Generation){
  SystemLang=S.Lang;SystemGeneration=OKStreetCppText().Generation;System=JudgePrompt;
  System+=OKT(TEXT("ui.scene.activity_id_label"));for(const auto& A:OKStreetActivities())System+=OKMsg(TEXT("ui.party.menu_id_name_entry"), {{TEXT("id"), A.Id}, {TEXT("name"), A.Name}});
  System+=OKT(TEXT("ui.scene.place_id_list_header"));for(const auto& P:OKStreetPlaces())System+=OKMsg(TEXT("ui.scene.id_name_about"), {{TEXT("id"), P.Id}, {TEXT("name"), P.Name}, {TEXT("about"), *FString(P.About).Left(28)}});
 }
 QUICK_SCOPE_CYCLE_COUNTER(STAT_Street_JudgeContext);
 // The menu where they stand (empty elsewhere or when closed) and what her reply already ordered.
 FString OnTable;for(const auto& D:S.Table)if(D.ForHer&&D.Her==Character&&D.Percent>0)OnTable+=OKMsg(TEXT("ui.scene.dish_entry"), {{TEXT("dish_name"), *D.Name}, {TEXT("served_suffix"), D.Served?TEXT(""):OKT(TEXT("ui.scene.preparing_food"))}});
 FString Menu;{const FString Here=OrderShopHere(Character);const FString Shop=Here.IsEmpty()?OutingShop(Character):Here;FString Closed;if(!Shop.IsEmpty()&&!PlaceClosedNow(Shop,Closed))for(const auto* I:Here.IsEmpty()?OKStreetItemsAt(S.Catalog,Shop,StreetNow().GetMonth()):ItemsHere(Shop))Menu+=OKMsg(TEXT("ui.party.menu_id_name_entry"), {{TEXT("id"), *I->Id}, {TEXT("name"), *I->Name}});}
 // Meetings with her the game already has, so the same plan is not booked twice.
 // His own work shifts too: "see you at the shop at five" said by two who both work at the cafe was booked at the
 // convenience store they stood in, over his shift there (R6-29).
 FString Booked;{const FString HerId=OKStreetStableId(S.Manifest.Characters[Character]);for(const auto& A:S.Store.Appointments)if(A.Status==TEXT("pending")&&(A.Heroine==HerId||A.Kind==TEXT("shift")&&A.Start-OKStreetMindNow(S)<=2*1440)){
  // With the part of the day: her "10:15" pickup after a night shift was booked for the morning and the judge, reading
  // "10:15", took "ten fifteen tonight" as the same time (R6-10).
  const int32 Hour=int32(A.Start%1440)/60;const TCHAR* Part=Hour<5?OKT(TEXT("ui.free_talk.late_night_period")):Hour<8?OKT(TEXT("ui.free_talk.early_morning_period")):Hour<11?OKT(TEXT("ui.free_talk.morning_period")):Hour<13?OKT(TEXT("ui.free_talk.noon_period")):Hour<17?OKT(TEXT("ui.free_talk.afternoon_period")):Hour<19?OKT(TEXT("ui.free_talk.evening_dusk_period")):Hour<23?OKT(TEXT("ui.free_talk.night_period")):OKT(TEXT("ui.free_talk.late_night_period"));
  Booked+=OKMsg(TEXT("ui.scene.schedule_entry"), {{TEXT("when"), OKStreetCalendarLabel(int32(A.Start/1440),double(A.Start%1440)/60.0)+TEXT(" ")+Part}, {TEXT("label"), A.Kind==TEXT("shift")?OKMsg(TEXT("ui.scene.his_own_shift"),{{TEXT("label"),A.Label}}):A.Label}});}}
 FString Nearby;for(int32 G=1;G<S.Manifest.Characters.Num();++G)if(PartyCanJoin(G))Nearby+=S.Manifest.Characters[G].Name+OKT(TEXT("ui.economy.semicolon_mark"));
 const FDateTime Now=StreetNow();
 FString User=OKMsg(TEXT("ui.scene.llm_context_prompt"), {{TEXT("year"), Now.GetYear()}, {TEXT("month"), Now.GetMonth()}, {TEXT("day"), Now.GetDay()}, {TEXT("weekday"), OKMsg(TEXT("ui.calendar.weekday_name"), {{TEXT("d"), int32(Now.GetDayOfWeek())}})}, {TEXT("hour"), Now.GetHour()}, {TEXT("minute"), Now.GetMinute()}, {TEXT("together_state"), RemoteWith(Character)?OKT(TEXT("ui.scene.apart_phone")):OKT(TEXT("ui.scene.face_to_face"))}, {TEXT("her_location"), *PlaceHere(S.Agents.IsValidIndex(Character-1)?S.Agents[Character-1].Position:S.Male)}, {TEXT("his_said"), *Said}, {TEXT("her_reply"), *Her}, {TEXT("her_action"), HerAction.IsEmpty()?OKT(TEXT("ui.scene.empty_label")):*HerAction}, {TEXT("doing_now"), S.ActGirl==Character&&!S.ActId.IsEmpty()?*S.ActId:S.OutingGirl==Character?*OKMsg(TEXT("ui.scene.walking_to_place"), {{TEXT("place"), *S.OutingPlace}, {TEXT("activity"), *S.OutingActivity}}):OKT(TEXT("ui.scene.none_label"))}, {TEXT("promise"), Booked.IsEmpty()?OKT(TEXT("ui.scene.none_label")):*Booked}, {TEXT("nearby_girls"), Nearby.IsEmpty()?OKT(TEXT("ui.scene.none_label")):*Nearby}, {TEXT("menu"), Menu.IsEmpty()?OKT(TEXT("ui.scene.not_in_shop")):*Menu}, {TEXT("her_ordered"), HerOrders.IsEmpty()?OKT(TEXT("ui.scene.none_paren_label")):*FString::Join(HerOrders,OKT(TEXT("ui.bank.list_separator")))}, {TEXT("on_table"), OnTable.IsEmpty()?OKT(TEXT("ui.scene.none_paren_label")):*OnTable}});
 // He asked her out and she said she would think it over ("let me think"): her later "I've decided, yes" answers that,
 // although he did not ask again in this line (it left Keqing his friend after she said yes the next day).
 if(const int64* Asked=S.ConfessionPending.Find(Character)){if(OKStreetMindNow(S)-*Asked<=3*1440)User+=OKT(TEXT("ui.scene.judge_pending_confession"));else S.ConfessionPending.Remove(Character);}
 // What the place they are in offers now (and what is out of reach): the activity do_now names belongs here.
 {const FString Here=ActivityBlockedLine(Character);if(!Here.IsEmpty())User+=TEXT("\n")+Here;}
 // What they already did together today: "let's eat the watermelon we just split" refilled do_now with the watermelon
 // splitting and bought and paid a second one three minutes after the first (R6-33).
 if(S.ActivitiesDoneDay==OKStreetMindNow(S)/1440)if(const TArray<FString>* Done=S.ActivitiesDone.Find(Character)){TArray<FString> Names;
  for(const FString& Id:*Done)if(const FOKStreetActivity* A=OKStreetFindActivity(Id))Names.Add(A->Name);
  if(!Names.IsEmpty())User+=TEXT("\n")+OKMsg(TEXT("ui.scene.judge_done_today"),{{TEXT("list"),FString::Join(Names,OKT(TEXT("ui.bank.list_separator")))}});}
 // She offered to sing for him and waits for his answer: his words may be it (song_answer; Song.inl acts on it).
 if(S.Song.Phase==EOKSongPhase::Offered&&S.Song.Girl==Character)User+=OKT(TEXT("ui.scene.judge_song_offer"));
 // Or her song is ready and he asked her to wait (he may want it now); and a song she sang him before he may ask again.
 else if(S.Song.Phase==EOKSongPhase::Ready&&S.Song.Due&&!S.Song.Replay&&S.Song.Girl==Character)User+=OKT(TEXT("ui.scene.judge_song_ready"));
 if(!SongIsSinging()&&SongLatestMemory(Character)!=INDEX_NONE)User+=OKT(TEXT("ui.scene.judge_song_again"));
 // Her day ahead, with place ids: where "come along with me first" goes (JudgePlanLine).
 User+=JudgePlanLine(Character);
 if(FString Proposed;S.HerProposed.RemoveAndCopyValue(Character,Proposed))User+=OKMsg(TEXT("ui.scene.judge_her_proposed"),{{TEXT("action"),Proposed}});
 TArray<TSharedPtr<FJsonValue>> Messages;
 auto AddMessage=[&Messages](const TCHAR* Who,const FString& Content){TSharedRef<FJsonObject> O=MakeShared<FJsonObject>();O->SetStringField(TEXT("role"),Who);O->SetStringField(TEXT("content"),Content);Messages.Add(MakeShared<FJsonValueObject>(O));};
 // The lines before these two ("breakfast at the cafeteria?" ... "it's past seven, let's walk over"): what "there" and
 // "walk over" mean comes from them.
 // Her history, not only the face-to-face log: her morning text "I work till ten, will you walk me home? the road is
 // dark" is what makes "10:15" the night (it was booked for the morning, R6-10). The last day's talk, texts and calls.
 FString Earlier;{const FString HerId=OKStreetStableId(S.Manifest.Characters[Character]);TArray<FString> Lines;const int64 Since=OKStreetMindNow(S)-1440;
  const auto& List=HistoryOf(HerId);
  for(int32 K=List.Num()-1;K>=0&&Lines.Num()<4;--K){const auto& E=List[K];if(E.Minute<Since)break;
   if((E.Kind!=TEXT("talk")&&E.Kind!=TEXT("message")&&E.Kind!=TEXT("call"))||E.Who==TEXT("world")||E.Text.IsEmpty())continue;
   if(E.Text.Contains(Said)||Said.Contains(E.Text)||Her.Contains(E.Text)||E.Text.Contains(Her))continue;
   Lines.Insert(S.Manifest.Characters[E.Who==TEXT("him")?0:Character].Name+TEXT(": ")+E.Text,0);}
  if(Lines.Num())Earlier=FString::Join(Lines,TEXT("\n"));}
 // What he carries: the gift she accepts is one of these (it stayed in his bag when no field named it).
 FString Bag;{TArray<FString> Names;for(const FString& Item:S.Store.Inventory)if(const auto* Known=S.Catalog.Find(Item))Names.AddUnique(Known->Name);
  if(Names.Num())Bag=OKMsg(TEXT("ui.scene.judge_his_bag"),{{TEXT("items"),FString::Join(Names,OKT(TEXT("ui.bank.list_separator")))}});
  // Her things and the loans between them: what she_gives / she_lends_item / he_returns_item can name.
  const FString Things=ThingsFacts(Character);if(!Things.IsEmpty())Bag+=OKMsg(TEXT("ui.scene.judge_her_things"),{{TEXT("things"),Things}});}
 AddMessage(TEXT("system"),System);AddMessage(TEXT("user"),(Earlier.IsEmpty()?User:OKMsg(TEXT("ui.scene.judge_earlier_lines"),{{TEXT("lines"),Earlier}})+User)+Bag);
 TWeakPtr<FOKStreetSceneState> WeakState=State;TWeakObjectPtr<AOKStreetSocialScene> Weak(this);
 // On the phone with her as he said it (a call, not typing): only then does a date get called off (R3-268). Taken now,
 // the call may be over when the verdict comes.
 const bool InCall=S.RemoteMode==2&&S.RemotePeer==Character;
 // His lines so far: one more by the time the verdict comes and this exchange is not the last any more (R4-10).
 const int64 HisLinesAt=S.HisLines;
 // Room for the whole verdict: with the things, photos and series fields it runs past 200 tokens and came back cut off
 // mid-"why" (no verdict at all: a meeting agreed was never booked).
 SubmitMindChat(FString::Printf(TEXT("judge:%d"),Character),TEXT("dialogue"),Messages,600,0.f,[WeakState,Weak,Character,Id,Said,Her,HerAction,HerOrders,Earlier,InCall,HisLinesAt,HadMenu=!Menu.IsEmpty()](bool Ok,const FString& Content,const FString& Failure){
  const auto L=WeakState.Pin();if(!L||!Weak.IsValid()||Weak->State!=L)return;
  TSharedPtr<FJsonObject> Verdict;if(Ok)FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Content),Verdict);
  if(!Verdict.IsValid()){UE_LOG(LogTemp,Display,TEXT("Street judge: no verdict, her action field \"%s\" is used"),*HerAction);
   if(L->ChatOrderAwaitsJudge.Remove(Character)>0&&!L->Cart.IsEmpty())Weak->PlaceChatOrder(Character,Her);
   Weak->ExecuteFreeTalkAction(Character,Id,HerAction);return;}
  FString DoNow,Why;Verdict->TryGetStringField(TEXT("do_now"),DoNow);Verdict->TryGetStringField(TEXT("why"),Why);
  // His words answered her offer to sing ("sure, sing for me"), asked for her waiting song now, or asked her to sing
  // her last song again: the same as the panel's buttons.
  {FString Song;if(Verdict->TryGetStringField(TEXT("song_answer"),Song)){const auto& SG=L->Song;
   if(Song==TEXT("replay")){const int32 M=Weak->SongLatestMemory(Character);
    if(M!=INDEX_NONE&&!Weak->SongIsSinging()){UE_LOG(LogTemp,Display,TEXT("Street judge: actor=%d asked in his words to hear her song again"),Character);Weak->SongReplay(M);}}
   else if((Song==TEXT("accept")||Song==TEXT("later")||Song==TEXT("decline"))&&SG.Girl==Character
    &&(SG.Phase==EOKSongPhase::Offered||(SG.Phase==EOKSongPhase::Ready&&SG.Due&&!SG.Replay&&Song==TEXT("accept")))){UE_LOG(LogTemp,Display,TEXT("Street judge: actor=%d answered her song offer in his words: %s"),Character,*Song);Weak->SongAnswer(Song);}}}
  if(DoNow.StartsWith(TEXT("act:")))DoNow=DoNow.Mid(4); // "act:stroll": the stroll
  // "go:路口" (walking him to the corner): no place of the map, nothing to start. Dropped quietly: a failed outing told
  // her "it did not work" and she took her words back ("I mixed up the way").
  if(DoNow.StartsWith(TEXT("go:"))&&!OKStreetFindPlace(DoNow.Mid(3))){UE_LOG(LogTemp,Display,TEXT("Street judge: do_now %s is no place; nothing to start"),*DoNow);DoNow.Reset();}
  // "Let's go see the show — Sure, let's go!" inside the aquarium judged as going to the aquarium: they are there
  // already, so they walk around it together instead of standing still after her "let's go" (R3-111).
  if(DoNow.StartsWith(TEXT("go:"))&&DoNow.Mid(3)==Weak->PlaceHere(L->Male)){UE_LOG(LogTemp,Display,TEXT("Street judge: do_now %s is where he is; a stroll here"),*DoNow);DoNow=TEXT("stroll");}
  const TSharedPtr<FJsonObject>* Promise=nullptr;FString Day,Time,Place,What;bool PayForPromise=false;
  if(Verdict->TryGetObjectField(TEXT("promise"),Promise)&&Promise&&(*Promise).IsValid()){(*Promise)->TryGetStringField(TEXT("day"),Day);(*Promise)->TryGetStringField(TEXT("time"),Time);(*Promise)->TryGetStringField(TEXT("place"),Place);(*Promise)->TryGetStringField(TEXT("what"),What);}
  UE_LOG(LogTemp,Display,TEXT("Street judge: actor=%d do_now=\"%s\" (her field \"%s\") promise=%s %s %s %s; %s"),Character,*DoNow,*HerAction,*Day,*Time,*Place,*What,*Why);
  // What they agreed to do as soon as this is over ("after we eat I'll walk you home", "eat first, then the department
  // store"): it starts when it is over (TickThenAction), not only when he asks again. With a do_now as well (one more
  // round of the fish, then lunch), after that one.
  {FString Then;Verdict->TryGetStringField(TEXT("then"),Then);if(Then.StartsWith(TEXT("act:")))Then=Then.Mid(4);
   // Setting off somewhere now with no next step said: that walk is the plan now, and the next step queued before it is
   // gone. "After lunch, walk me to the teaching building" queued T1, then "walk me to the EE building" set off there,
   // and on arrival they walked on to T1 (R6-9).
   if(Then.IsEmpty()&&DoNow.StartsWith(TEXT("go:"))&&L->ThenGirl==Character&&!L->ThenAction.IsEmpty()){UE_LOG(LogTemp,Display,TEXT("Street judge: actor=%d then=%s dropped, they set off for %s instead"),Character,*L->ThenAction,*DoNow);L->ThenAction.Reset();}
   if(!Then.IsEmpty()&&Then!=DoNow&&(!Then.StartsWith(TEXT("go:"))||OKStreetFindPlace(Then.Mid(3)))){L->ThenAction=Then;L->ThenGirl=Character;L->ThenUntil=OKStreetMindNow(*L)+120;L->ThenReadyAt=0;
    // Going somewhere first ("a walk in the park, then lunch"): some time there before the next, not off on arrival.
    L->ThenStayMinutes=DoNow.StartsWith(TEXT("go:"))?20:0;UE_LOG(LogTemp,Display,TEXT("Street judge: actor=%d then=%s, once what they are doing is over"),Character,*Then);}}
  // "Lunch at the cafeteria? — Sure, see you there at noon" judged as both a promise and going now: the meeting is later
  // (another day, or more than half an hour from now), so the same place is not gone to now.
  if(!DoNow.IsEmpty()&&!Place.IsEmpty()){
   const FOKStreetActivity* NowAct=OKStreetFindActivity(DoNow);const FString NowPlace=DoNow.StartsWith(TEXT("go:"))?DoNow.Mid(3):NowAct&&NowAct->Place?FString(NowAct->Place):DoNow;
   int32 MeetHour=-1,MeetMinute=0;{FString MeetH,MeetM;if(Time.Split(TEXT(":"),&MeetH,&MeetM)){MeetHour=FCString::Atoi(*MeetH);MeetMinute=FCString::Atoi(*MeetM);}}
   const bool MeetToday=Day.IsEmpty()||Day==TEXT("+0")||Day==TEXT("0");
   const int64 MindNow=OKStreetMindNow(*L);const int64 MeetAt=(MindNow/1440)*1440+MeetHour*60+MeetMinute;
   if(NowPlace==Place&&(!MeetToday||(MeetHour>=0&&MeetAt-MindNow>30))){UE_LOG(LogTemp,Display,TEXT("Street judge: do_now %s dropped, it is the promise for %s %s"),*DoNow,*Day,*Time);DoNow.Reset();}}
  // Which activity and which place is the judge's reading (ui.scene.ai_judge_prompt): place names looked up in the
  // words kept overruling it and could never cover every way of saying it.
  // "Not today, tomorrow" / "make it six": the meeting with her they had moves (the judge says it replaces it).
  // Which one it replaces is promise.rs's (the same meeting said again is moved; else hers at that place or nearest).
  // A day with no place yet goes in too: she then asks when and where (AddPromiseFromChat).
  if(!Place.IsEmpty()||!Day.IsEmpty()){bool Replaces=false;Verdict->TryGetBoolField(TEXT("replaces"),Replaces);if(Replaces)UE_LOG(LogTemp,Display,TEXT("Street judge: actor=%d the new meeting replaces the one they had"),Character);
   double Hours=0;if(Promise&&(*Promise).IsValid())(*Promise)->TryGetNumberField(TEXT("hours"),Hours);
   Weak->AddPromiseFromChat(Character,Day,Time,Place,What,Said+TEXT("\n")+Her,Said,Replaces,float(Hours));
   // Who pays, said with it ("Friday seven, the restaurant, my apology treat"): kept on the meeting just booked or moved,
   // for that day (R3-216).
   FString Payer;if(Verdict->TryGetStringField(TEXT("payer"),Payer)&&(Payer==TEXT("him")||Payer==TEXT("her")||Payer==TEXT("split"))){
    const FString HerId=OKStreetStableId(L->Manifest.Characters[Character]);const double Wall=FPlatformTime::Seconds();
    for(auto& A:L->Store.Appointments)if(A.Heroine==HerId&&A.Status==TEXT("pending")&&A.Kind==TEXT("date"))if(const double* At=L->PromiseBookedAt.Find(A.Id);At&&Wall-*At<5.0){A.Payer=Payer;PayForPromise=true;
     UE_LOG(LogTemp,Display,TEXT("Street appointment %d: %s pays"),A.Id,*Payer);}}}
  // "Every Wednesday" (the judge's promise.repeat): the meeting just booked is the first of a series (series.rs).
  if(!Place.IsEmpty()&&Promise&&(*Promise).IsValid())if(const TSharedPtr<FJsonValue> Rep=(*Promise)->TryGetField(TEXT("repeat"));Rep.IsValid()&&!Rep->IsNull()&&!(Rep->Type==EJson::String&&Rep->AsString().IsEmpty()))Weak->StartSeries(Character,Place,Rep);
  {bool Ends=false,Treats=false;Verdict->TryGetBoolField(TEXT("ends"),Ends);Verdict->TryGetBoolField(TEXT("she_treats"),Treats);
   // Still walking her there ("here's the dorm, I'm home" said a street early): the walk ends at the door, and the talk
   // goes on there, so no goodbye on the way.
   if(Ends&&DoNow.IsEmpty()&&L->OutingGirl==Character)UE_LOG(LogTemp,Display,TEXT("Street farewell: actor=%d not on the way; the outing reaches %s first"),Character,*L->OutingPlace);
   // "I'll go once I've finished my tea" with her food still on the table: not now; the meal ends first (she does not
   // walk off from a full plate). He can still leave himself.
   if(Ends&&DoNow.IsEmpty()&&L->Table.ContainsByPredicate([Character](const FOKStreetDish& D){return D.Her==Character&&D.ForHer&&D.Percent>0.f;})){
    UE_LOG(LogTemp,Display,TEXT("Street farewell: actor=%d still has food on the table; the talk goes on until the meal ends"),Character);Ends=false;
    L->FarewellAfterMeal=Character;L->FarewellAfterMealAt=Weak->SceneTime();}
   // Not when he has spoken again since (his words called it off): 惠惠's "I'm going" was judged after he asked her to
   // eat and she agreed, and the late farewell took her off the walk to the cafeteria (R4-10).
   if(Ends&&DoNow.IsEmpty()&&!L->FarewellPending&&L->OutingGirl!=Character&&L->HisLines==HisLinesAt){L->FarewellPending=true;L->FarewellFromTalk=true;L->FarewellAt=FPlatformTime::Seconds();UE_LOG(LogTemp,Display,TEXT("Street farewell: actor=%d the conversation ends after her last line (judged)"),Character);}
   {const TArray<TSharedPtr<FJsonValue>>* Invite=nullptr;if(Verdict->TryGetArrayField(TEXT("invite"),Invite))for(const auto& V:*Invite){FString Name;if(V.IsValid()&&V->TryGetString(Name)){int32 Who=INDEX_NONE;
    // The exact name first, as the game writes it too ("貞德" in a zh-TW game for the card's "贞德"): otherwise the looser match
    // below took 貞德·Alter, the one he was talking to, and Jeanne was "already following him".
    for(int32 G=1;G<L->Manifest.Characters.Num()&&Who==INDEX_NONE;++G)if(Name.Equals(L->Manifest.Characters[G].Name,ESearchCase::IgnoreCase)||Name.Equals(Weak->SpokenName(G),ESearchCase::IgnoreCase))Who=G;for(int32 G=1;G<L->Manifest.Characters.Num()&&Who==INDEX_NONE;++G)if(OKStreetNameMatches(Name,L->Manifest.Characters[G].Name))Who=G;if(Who!=INDEX_NONE){UE_LOG(LogTemp,Display,TEXT("Street judge: he invites %s to join"),*Name);Weak->PartyJoin(Who);}}}}
   // He confessed in his own words: the confession the rules decide (the same roll as the button), when she is the one he
   // is talking to face to face and nothing else is being settled.
   {bool Confessed=false;Verdict->TryGetBoolField(TEXT("confessed"),Confessed);
    // Her own words turned him down ("I want to be the only one; you can't give that, so don't ask"): that is her
    // answer, not a dice roll that made her his girlfriend anyway (accepts: false).
    bool Accepts=true;const bool Answered=Confessed&&Verdict->TryGetBoolField(TEXT("accepts"),Accepts);if(Answered&&!Accepts){UE_LOG(LogTemp,Display,TEXT("Street judge: actor=%d he confessed; she said no in her own words"),Character);Confessed=false;
     // Not a yes yet ("let me think about it"): her answer may come later in another talk (the judge is told).
     if(!L->ConfessionPending.Contains(Character))L->ConfessionPending.Add(Character,OKStreetMindNow(*L));}
    // No answer in her words at all ("I won't answer yet, let me finish this pancake"): still hers to give, not a roll
    // that answered for her. 胡桃 put it off, the dice said yes, and she "ran off blushing" as his girlfriend (R6-17).
    // Any confession he says himself: only her own yes makes them lovers; the button keeps its roll.
    if(Confessed&&!Answered){UE_LOG(LogTemp,Display,TEXT("Street judge: actor=%d he confessed; no answer from her yet"),Character);Confessed=false;
     if(!L->ConfessionPending.Contains(Character))L->ConfessionPending.Add(Character,OKStreetMindNow(*L));}
    if(Confessed)L->ConfessionPending.Remove(Character);
    if(Confessed&&!Ends&&L->Target==Character&&!L->RelationshipPending&&L->Node==INDEX_NONE&&!Weak->RemoteWith(Character)
     &&OKStreetViewRelation(L->Store,OKStreetStableId(L->Manifest.Characters[Character]),L->Manifest.TalkBudget).Stage!=TEXT("lover")){
     UE_LOG(LogTemp,Display,TEXT("Street judge: actor=%d he confessed in his own words%s"),Character,Answered&&Accepts?TEXT("; she said yes"):TEXT(""));Weak->RelationshipEvent(TEXT("confession"),Answered&&Accepts);}}
   // A confession in front of others (Anko drinking at the next table of the cafe): they saw it, and how she answered.
   {bool SaidIt=false;Verdict->TryGetBoolField(TEXT("confessed"),SaidIt);
    if(SaidIt&&!Weak->RemoteWith(Character)){bool Yes=false;const bool Ans=Verdict->TryGetBoolField(TEXT("accepts"),Yes);Weak->ConfessionWitnessed(Character,Ans?(Yes?TEXT("yes"):TEXT("no")):TEXT("open"));}}
   {bool BreaksUp=false;Verdict->TryGetBoolField(TEXT("breaks_up"),BreaksUp);
    if(BreaksUp&&OKStreetViewRelation(L->Store,OKStreetStableId(L->Manifest.Characters[Character]),L->Manifest.TalkBudget).Stage==TEXT("lover"))Weak->BreakUpSaid(Character);}
   {bool Quiet=false;Verdict->TryGetBoolField(TEXT("quiet"),Quiet);
    if(Quiet&&!Ends){L->FreeTalkQuietUntil=FPlatformTime::Seconds()+OKFreeTalkQuietSeconds;L->FreeTalkSilencePenalties=0;UE_LOG(LogTemp,Display,TEXT("Street judge: actor=%d asked him to keep quiet; no silence nudges for %.0f s"),Character,OKFreeTalkQuietSeconds);}}
   // "That's enough reading for today": they stop what they sat down to and go on talking (as the leave-seat button).
   {bool StopAct=false;Verdict->TryGetBoolField(TEXT("stop_activity"),StopAct);
    if(StopAct&&!Ends&&DoNow.IsEmpty()&&L->Pair.Partner==Character&&L->Pair.Phase==1&&Weak.IsValid()){UE_LOG(LogTemp,Display,TEXT("Street judge: actor=%d they stop the activity, the talk goes on"),Character);Weak->FinishPhoto();}}
   // Her treat already paid within the hour: "I'm treating you" said over that same dango is not a new promise (it made
   // her pay at every shop for six hours; R3-227).
   if(Treats&&OKStreetMindNow(*L)-L->HerTreatKept.FindRef(Character,-100000)<60){UE_LOG(LogTemp,Display,TEXT("Street treat promise: actor=%d already treated him, not again"),Character);}
   else if(Treats){L->HerTreatPromise.Add(Character,OKStreetMindNow(*L)+6*60);UE_LOG(LogTemp,Display,TEXT("Street treat promise: actor=%d will pay for what he orders (judged)"),Character);}
   // "Let's skip lunch then, another day" — "fine": the meeting they had is called off (her own act field may forget it).
   {bool Cancel=false;Verdict->TryGetBoolField(TEXT("cancel_promise"),Cancel);
    // This one or all of a weekly meeting (cancel_scope; asked when it is not said).
    // Only on the phone, the call itself (R3-268): a message or a word in passing does not call a date off, and she hears
    // that it still stands. How he put it is scored by the judge (cancel_grace); -1, she called it off herself.
    if(Cancel&&Place.IsEmpty()&&!InCall){UE_LOG(LogTemp,Display,TEXT("Street judge: actor=%d calling a date off, not on a call: it stands"),Character);L->TellHer.Add(OKT(TEXT("ui.appointments.cancel_needs_call")));}
    else if(Cancel&&Place.IsEmpty()){FString Scope;Verdict->TryGetStringField(TEXT("cancel_scope"),Scope);double Grace=-1;Verdict->TryGetNumberField(TEXT("cancel_grace"),Grace);
     L->CancelCallGirl=Character;L->CancelCallGrace=Grace;L->CancelCallSaid.Reset();
     UE_LOG(LogTemp,Display,TEXT("Street judge: actor=%d the promise is called off on the phone (%s, grace %.0f)"),Character,*Scope,Grace);Weak->CancelPromiseWith(Character,Scope);}}
   {bool Kept=false;Verdict->TryGetBoolField(TEXT("kept_promise"),Kept);if(Kept&&Weak.IsValid())Weak->KeepPromiseNow(Character);}
   // "Take it, it was for you anyway" — "thank you": the gift is given (her own act field may forget it). Only something
   // he still carries (her act may have given it already).
   {FString Gift;Verdict->TryGetStringField(TEXT("gift"),Gift);
    if(!Gift.IsEmpty()&&L->PendingGift.IsEmpty()&&!L->Choices.ContainsByPredicate([](const FOKStreetPendingChoice& C){return C.Kind==TEXT("gift");})){
     // Not in his bag: "I'll buy this one and give it to you" at the shop that sells it buys it first (as the buy-as-gift
     // button); sold nowhere here, she is told nothing was given. 胡桃 thanked him for the 烟火图鉴 he "bought" at the
     // bookstore, and no money left his wallet and no book reached her (R6-28).
     auto Matches=[&](const FString& Id,const FOKStreetCatalogItem& I){return Id==Gift||OKStreetNameMatches(Gift,I.Name);};
     const bool Carried=L->Store.Inventory.ContainsByPredicate([&](const FString& Id){const auto* I=L->Catalog.Find(Id);return I&&Matches(Id,*I);});
     if(!Carried&&Weak.IsValid()){const FString Shop=Weak->ShopPlaceHere();const FOKStreetCatalogItem* Sold=nullptr;
      if(!Shop.IsEmpty())for(const auto* I:Weak->ItemsHere(Shop))if(I&&I->Giftable&&Matches(I->Id,*I)){Sold=I;break;}
      if(Sold){UE_LOG(LogTemp,Display,TEXT("Street judge: actor=%d gift %s not carried, bought here first"),Character,*Sold->Id);Weak->BuyForGift(Sold->Id);}
      else{L->TellHer.Add(OKMsg(TEXT("ui.economy.gift_not_carried"),{{TEXT("item"),Gift}}));UE_LOG(LogTemp,Display,TEXT("Street judge: actor=%d gift %s: he has none and none is sold here"),Character,*Gift);}}
     for(const FString& Item:L->Store.Inventory)if(const auto* Known=L->Catalog.Find(Item);Known&&(Item==Gift||OKStreetNameMatches(Gift,Known->Name))){ // either script: "月饼" for the catalogue's 月餅（單顆）
      UE_LOG(LogTemp,Display,TEXT("Street judge: actor=%d he gives her %s"),Character,*Item);Weak->HandleWorldAct(Character,TEXT("gift"),Item);break;}}}
   // She takes a photo, or sends him one (photos.rs): the picture is taken from where they stand.
   {const TSharedPtr<FJsonObject>* Shot=nullptr;bool SendToo=false;
    const TSharedPtr<FJsonValue> SendField=Verdict->TryGetField(TEXT("she_sends_photo"));const bool Sends=SendField.IsValid()&&!SendField->IsNull()&&!(SendField->Type==EJson::String&&SendField->AsString().IsEmpty())&&!(SendField->Type==EJson::Boolean&&!SendField->AsBool());
    if(Verdict->TryGetObjectField(TEXT("she_photo"),Shot)&&Shot->IsValid()){FString Topic,Subject;(*Shot)->TryGetStringField(TEXT("topic"),Topic);(*Shot)->TryGetStringField(TEXT("subject"),Subject);SendToo=Sends;
     UE_LOG(LogTemp,Display,TEXT("Street judge: actor=%d takes a photo (%s, %s)%s"),Character,*Topic,*Subject,SendToo?TEXT(", sends it"):TEXT(""));Weak->HerPhoto(Character,Topic,Subject,SendToo);}
    else if(Sends){UE_LOG(LogTemp,Display,TEXT("Street judge: actor=%d sends him a photo"),Character);Weak->SendPhoto(OKStreetStableId(L->Manifest.Characters[Character]),Character,SendField);}}
   // She gives him her number (contacts.rs): from now on in his phone.
   {bool Gives=false;Verdict->TryGetBoolField(TEXT("gives_number"),Gives);if(Gives)Weak->HerNumberGiven(Character);
    bool Takes=false;Verdict->TryGetBoolField(TEXT("takes_his_number"),Takes);if(Takes)Weak->HerNumberGiven(Character,TEXT("his"));}
   // Her own business at the office (errand.rs her_errand): a student loan, paying her fees.
   {FString Errand;Verdict->TryGetStringField(TEXT("her_errand"),Errand);if(!Errand.IsEmpty()){UE_LOG(LogTemp,Display,TEXT("Street judge: actor=%d errand %s"),Character,*Errand);Weak->HerErrand(Character,Errand);}}
   // Her things and his given, lent or returned (items.rs: matched to what they really hold, the rest refused).
   {auto Request=OKRulesRequest(TEXT("items_judge"));bool Any=false;
    for(const TCHAR* K:{TEXT("she_gives"),TEXT("she_lends_item"),TEXT("she_returns_item"),TEXT("he_lends_item"),TEXT("he_returns_item")})if(const TSharedPtr<FJsonValue> V=Verdict->TryGetField(K);V.IsValid()&&!V->IsNull()){
     FString Named;if(V->TryGetString(Named)&&Named.IsEmpty())continue;Request->SetField(K,V);Any=true;}
    if(Any){OKRulesPutExt(*L,Request);Request->SetStringField(TEXT("girl"),OKStreetStableId(L->Manifest.Characters[Character]));
     TArray<TSharedPtr<FJsonValue>> His;for(const FString& I:L->Store.Inventory)His.Add(MakeShared<FJsonValueString>(I));Request->SetArrayField(TEXT("his_items"),His);
     if(const TSharedPtr<FJsonObject> Out=OKRulesEval(*L,Request)){auto Candidate=L->Store;OKRulesTakeExt(Candidate,*Out);
      if(Weak->CommitStore(MoveTemp(Candidate))){const FString Notice=Out->GetStringField(TEXT("notice"));if(!Notice.IsEmpty()){L->FreeTalkNotice=Notice;Weak->RefreshUI();}}
      UE_LOG(LogTemp,Display,TEXT("Street judge: actor=%d things: done %d, refused %d (%s)"),Character,Out->GetArrayField(TEXT("done")).Num(),Out->GetArrayField(TEXT("refused")).Num(),*Out->GetStringField(TEXT("notice")));}}}}
  // A menu item read as the thing to do now ("let's draw a fortune slip": do_now shrine_fortune) is bought, not an
  // activity that fails as "no such activity or place" (R3-114).
  if(!DoNow.IsEmpty()&&!DoNow.StartsWith(TEXT("go:"))&&DoNow!=TEXT("stroll")&&DoNow!=TEXT("leave")&&!OKStreetFindActivity(DoNow)&&!OKStreetFindPlace(DoNow)){
   const FString Here=Weak->OrderShopHere(Character);const FString ItemId=Here.IsEmpty()?FString():Weak->ResolveItemId(DoNow,Here);
   if(const auto* Item=ItemId.IsEmpty()?nullptr:L->Catalog.Find(ItemId);Item&&Weak->SoldHere(*Item,Here)){
    TArray<TSharedPtr<FJsonValue>> Orders;if(const TArray<TSharedPtr<FJsonValue>>* Had=nullptr;Verdict->TryGetArrayField(TEXT("orders"),Had))Orders=*Had;
    auto O=MakeShared<FJsonObject>();O->SetStringField(TEXT("id"),ItemId);O->SetStringField(TEXT("for"),TEXT("him"));Orders.Add(MakeShared<FJsonValueObject>(O));
    Verdict->SetArrayField(TEXT("orders"),Orders);UE_LOG(LogTemp,Display,TEXT("Street judge: do_now %s is on the menu here; ordered instead"),*DoNow);DoNow.Reset();}}
  // Ordering at the table and a private-room meal (a facility: the dinner is skipped over, 70 minutes and its own bill)
  // together: the dinner was billed and skipped, and then the soup came. With dishes ordered, they eat them at the
  // table: the same place's table meal.
  {const TArray<TSharedPtr<FJsonValue>>* Ordered=nullptr;
   const int32 Dishes=(Verdict->TryGetArrayField(TEXT("orders"),Ordered)?Ordered->Num():0)+L->Table.Num(); // ordered now, or already on the table
   if(const FOKStreetActivity* Room=OKStreetFindActivity(DoNow);Room&&Room->Kind==EOKStreetActivityKind::Facility&&Room->Code==0&&Room->Place&&Dishes>0)
    for(const auto& Table:OKStreetActivities())if(Table.Kind==EOKStreetActivityKind::Pair&&Table.Code==5&&Table.Place&&FCString::Strcmp(Table.Place,Room->Place)==0){
     UE_LOG(LogTemp,Display,TEXT("Street judge: do_now %s with %d dishes ordered; they eat them at the table (%s)"),*DoNow,Dishes,Table.Id);DoNow=Table.Id;break;}}
  // Who pays, as the judge read what they settled ("门票各付各的", "这顿我请"); before the orders are billed below.
  // For a meeting another time and nothing now: it went with the meeting above, not to the next shop today.
  {FString Payer;if(Verdict->TryGetStringField(TEXT("payer"),Payer)&&!Payer.IsEmpty()&&!(PayForPromise&&DoNow.IsEmpty()))Weak->RecordPayAgreed(Character,Payer,DoNow);}
  // Her reply's own order waited for this (R4-3): billed now, with what was settled.
  const bool OrderWaited=L->ChatOrderAwaitsJudge.Remove(Character)>0;
  // Orders he (or she) asked for that her reply left out: to the kitchen like any chat order.
  {const TArray<TSharedPtr<FJsonValue>>* Orders=nullptr;int32 Added=0;const FString Here=Weak->OrderShopHere(Character);FString Shop=Here.IsEmpty()?Weak->OutingShop(Character):Here;const bool OnTheWay=Here.IsEmpty();
   // Outside, about to go in ("let's go in, I'll have a latte"): the outing starts below, after the orders; its shop is
   // where the orders go (an empty shop dropped his latte).
   if(Shop.IsEmpty()&&!DoNow.IsEmpty()){if(const FOKStreetActivity* Going=OKStreetFindActivity(DoNow);Going&&Going->Place&&*Going->Place)Shop=Going->Place;else if(DoNow.StartsWith(TEXT("go:")))Shop=DoNow.Mid(3);}
   if(!Shop.IsEmpty()&&Verdict->TryGetArrayField(TEXT("orders"),Orders))for(const auto& V:*Orders){
    const TSharedPtr<FJsonObject> O=V.IsValid()?V->AsObject():nullptr;if(!O)continue;FString ItemId,For;O->TryGetStringField(TEXT("id"),ItemId);O->TryGetStringField(TEXT("for"),For);
    ItemId=Weak->ResolveItemId(ItemId,Shop);
    const auto* Item=L->Catalog.Find(ItemId);if(!Item||(OnTheWay?Item->Place!=Shop:!Weak->SoldHere(*Item,Shop))||HerOrders.Contains(ItemId))continue; // her reply already ordered it
    // Whoever it is for still has one of these (a meal, or a drink) in front of them or on its way: not a second one.
    const bool ForHer=For==TEXT("her");
    // Already on the table or on its way first: "too full, the braised pork rice was not ordered" about the bowl in
    // front of her made her say it never came.
    if(Weak->HasDishLike(ForHer?Character:0,*Item)&&!Weak->SecondDishOk(Character,ForHer,ItemId)){
     if(!Weak->DishComing(ForHer?Character:0,ItemId))L->TellHer.Add(OKMsg(TEXT("ui.economy.still_has_one_order"),{{TEXT("for"),ForHer?TEXT("her"):TEXT("him")},{TEXT("item"),Item->Name}}));
     UE_LOG(LogTemp,Display,TEXT("Street judge order skipped: %s, %s still has one on the table"),*Item->Name,ForHer?TEXT("she"):TEXT("he"));continue;}
    // A thing (not food) bought for the same one minutes ago: the judge reading "I'll buy it for you" after her own reply
    // had ordered the novel for herself bought it a second time as a gift (NT$360 for one book).
    if(!Item->Consumable){const int64* At=L->RecentBought.Find(ItemId+(ForHer?TEXT("|her"):TEXT("|him")));
     if(At&&OKStreetMindNow(*L)-*At<=15){L->TellHer.Add(OKMsg(TEXT("ui.economy.already_bought_order"),{{TEXT("for"),ForHer?TEXT("her"):TEXT("him")},{TEXT("item"),Item->Name}}));UE_LOG(LogTemp,Display,TEXT("Street judge order skipped: %s, bought for %s %lld min ago"),*Item->Name,ForHer?TEXT("her"):TEXT("him"),OKStreetMindNow(*L)-*At);continue;}}
    if(ForHer&&Weak->TooFullFor(Character,*Item)){L->TellHer.Add(OKMsg(TEXT("ui.economy.too_full_to_order"),{{TEXT("item"),Item->Name}}));UE_LOG(LogTemp,Display,TEXT("Street judge order skipped: %s, she is full"),*Item->Name);continue;}
    FOKStreetCartLine Line;Line.ItemId=ItemId;Line.ForHer=ForHer;Line.Size=Item->DefaultSize();if(!Item->Options.IsEmpty()){Line.Sweetness=OKT(TEXT("ui.mind.full_sugar_label"));Line.Ice=OKT(TEXT("ui.mind.normal_state_label"));}
    if(OnTheWay){L->CarriedOrders.FindOrAdd(Character).Add(Line);UE_LOG(LogTemp,Display,TEXT("Street judge order: %s for %s, carried to %s"),*Item->Name,*For,*Shop);continue;}
    L->ShopPlace=Shop;L->Cart.Add(Line);++Added;UE_LOG(LogTemp,Display,TEXT("Street judge order: %s for %s"),*Item->Name,*For);
   }
   if(Added>0||(OrderWaited&&!L->Cart.IsEmpty()))Weak->PlaceChatOrder(Character,Her);}
  // Something agreed to be bought here that the shop does not sell (a book on the 6F of the department store, which
  // sells stationery: Jeanne Alter said "I'll pay for it myself" and believed she had it; Hu Tao "took" a book he never
  // bought, R4-23 / R5-12). Nothing was billed: she is told, so she does not go on as if it were hers.
  // Only against a menu the judge was shown: said on the way, in the very line that set off for the bookstore, there
  // was none yet, and 胡桃 was told the bookstore did not sell the book it does (R6-18). She orders it on arrival.
  {const TArray<TSharedPtr<FJsonValue>>* Unsold=nullptr;if(HadMenu&&Verdict->TryGetArrayField(TEXT("unsold"),Unsold))for(const auto& V:*Unsold){FString Name;if(!V.IsValid()||!V->TryGetString(Name)||Name.TrimStartAndEnd().IsEmpty())continue;
   L->TellHer.Add(OKMsg(TEXT("ui.economy.not_sold_here_order"),{{TEXT("item"),Name}}));L->FreeTalkNotice=OKMsg(TEXT("ui.economy.not_sold_here_notice"),{{TEXT("item"),Name}});
   UE_LOG(LogTemp,Display,TEXT("Street judge: %s is not sold here; nothing bought, she is told"),*Name);}}
  // He lends her money now ("拿去吧，五百"), as the judge read it: only he can offer it; her act alone asks.
  {double Lend=0;Verdict->TryGetNumberField(TEXT("he_lends"),Lend);Weak->SettleMoneyHandover(Character,Lend,true);}
  // He told her himself about another girl ("I'm going out with 三月七"): heard from him, not a rumour to have out (social.rs rival_named).
  {TArray<FString> Told;FString One;const TArray<TSharedPtr<FJsonValue>>* List=nullptr;if(Verdict->TryGetArrayField(TEXT("told_about"),List))for(const auto& V:*List){FString N;if(V.IsValid()&&V->TryGetString(N)&&!N.IsEmpty())Told.Add(N);}else if(Verdict->TryGetStringField(TEXT("told_about"),One)&&!One.IsEmpty())Told.Add(One);for(const FString& N:Told)Weak->ToldAboutRival(Character,N);}
  Weak->ExecuteFreeTalkAction(Character,Id,DoNow);
  Weak->RefreshUI();
 });
}

// The model's judgement of the player's line moves her favor (clamped to the stage cap, saved like the panel sliders).
void AOKStreetSocialScene::ApplyFreeTalkFavor(int32 Character,int32 Delta,const FString& Reason){
 auto& S=*State;
 const FString Id=OKStreetStableId(S.Manifest.Characters[Character]);
 const auto Before=OKStreetViewRelation(S.Store,Id,S.Manifest.TalkBudget);
 const float Limit=OKStreetFreeTalkStatusOf(S,Before).FavorLimit;
 // The rules scale a gain by how many lovers he already has (mind.rs favor_gain): one more is harder to win.
 if(Delta!=0){int32 Lovers=0;for(const auto& R:S.Store.Relations)if(R.Id!=Id&&R.Stage==TEXT("lover"))++Lovers;
  auto Request=OKRulesRequest(TEXT("favor_gain"));Request->SetStringField(TEXT("stage"),Before.Stage);Request->SetNumberField(TEXT("favor"),Before.Favor);
  Request->SetNumberField(TEXT("limit"),Limit);Request->SetNumberField(TEXT("delta"),Delta);Request->SetNumberField(TEXT("lovers"),Lovers);
  const TSharedPtr<FJsonObject> Gain=OKRulesEval(S,Request);
  SetCharacterStat(Character,0,Gain.IsValid()?float(Gain->GetNumberField(TEXT("favor"))):FMath::Clamp(Before.Favor+Delta,0.f,Limit));}
 const float After=OKStreetViewRelation(S.Store,Id,S.Manifest.TalkBudget).Favor;
 S.FreeTalkNotice=OKMsg(TEXT("ui.scene.favor_change"), {{TEXT("before"), FMath::RoundToInt(Before.Favor)}, {TEXT("after"), FMath::RoundToInt(After)}, {TEXT("delta"), FMath::RoundToInt(After)-FMath::RoundToInt(Before.Favor)}})+(Reason.IsEmpty()?FString():OKT(TEXT("ui.accounts_app.ideographic_space"))+Reason);
}

// Leaving the conversation while the microphone is open: drop the recording.
void AOKStreetSocialScene::CancelFreeTalkVoice(){
 if(!State||!State->FreeTalkListening)return;
 State->FreeTalkListening=false;
 auto Request=FHttpModule::Get().CreateRequest();
 Request->SetURL(OKStreetFreeTalkAsrEndpoint()/TEXT("record/cancel"));Request->SetVerb(TEXT("POST"));Request->SetTimeout(5.f);
 Request->ProcessRequest();
}

// Her reply is spoken line by line: line i+1 is synthesized while line i plays. Each line is streamed
// (/synthesize_stream) and starts playing once ~0.2 s has arrived, so her voice follows the text after
// one TTS step instead of one whole line. The finished line is then fetched as a cached file for the log;
// a server without streaming (or a failed stream) falls back to the file. Streams go out in order; line i+1 starts
// as soon as line i has streamed (the server writes line i's file outside its synthesis lock). A new reply cancels
// what the server still has queued or is generating for the old one (/cancel; this game's own channel).
static constexpr int32 OKFreeTalkStreamStartSamples=4800;
// Drops what the voice server still has queued or is generating on this game's channel (a new reply, or the talk
// closed with lines unspoken): later answers for them are ignored by the generation.
static void OKStreetCancelQueuedSpeech(FOKStreetSceneState& S){
 ++S.FreeTalkSpeechGeneration;if(!S.FreeTalkSpeechChannel.IsEmpty())OKStreetTtsCancel(S.TtsEndpoint,S.FreeTalkSpeechChannel,uint32(S.FreeTalkSpeechGeneration));
}
void AOKStreetSocialScene::SpeakFreeTalk(int32 Character,const TArray<FString>& Lines,int64 Serial){
 if(!State||State->TtsCacheDir.IsEmpty()||!State->Manifest.Characters.IsValidIndex(Character)||Lines.IsEmpty())return;auto& S=*State;
 if(S.FreeTalkSpeechChannel.IsEmpty())S.FreeTalkSpeechChannel=TEXT("free_talk-")+FGuid::NewGuid().ToString(EGuidFormats::Digits);
 if(S.FreeTalkSpeechLines.Num()>0)OKStreetCancelQueuedSpeech(S);else ++S.FreeTalkSpeechGeneration;
 for(const auto& Stream:S.FreeTalkSpeechStreams)if(Stream)Stream->Finish();
 S.FreeTalkSpeechCharacter=Character;S.FreeTalkSpeechSerial=Serial;S.FreeTalkSpeechLines=Lines;S.FreeTalkSpeechSpeakers.Reset();S.FreeTalkSpeechFiles=0;
 S.FreeTalkSpeechPaths.Init(FString(),Lines.Num());S.FreeTalkSpeechReady.Init(0,Lines.Num());S.FreeTalkSpeechPlayed=0;
 S.FreeTalkSpeechStreamed.Init(0,Lines.Num());S.FreeTalkSpeechStreams.Init(nullptr,Lines.Num());
 S.FreeTalkSpeechSentAt.Init(0,Lines.Num());S.FreeTalkSpeechRepliedAt=FPlatformTime::Seconds();
 RequestFreeTalkSegment(S.FreeTalkSpeechGeneration,0);
}

void AOKStreetSocialScene::RequestFreeTalkSegment(int32 Generation,int32 Index){
 auto& S=*State;
 if(Generation!=S.FreeTalkSpeechGeneration||!S.FreeTalkSpeechLines.IsValidIndex(Index))return;
 const int32 Character=S.FreeTalkSpeechSpeakers.IsValidIndex(Index)?S.FreeTalkSpeechSpeakers[Index]:S.FreeTalkSpeechCharacter;
 // What the server can speak: a line of only dots ("……") has nothing to say and is shown silent, not a "TTS failed:
 // text must be 1..400 characters" notice; a longer one is cut at its last sentence end within 400 (R3-215).
 FString Spoken=OKStreetSpokenText(S.FreeTalkSpeechLines[Index]); // no stage directions (OKStreetSpokenText.h)
 {bool Speakable=false;for(const TCHAR C:Spoken)if(FChar::IsAlnum(C)||(C>=0x3040&&C<0xFF00)){Speakable=true;break;}
  if(!Speakable){S.FreeTalkSpeechStreamed[Index]=2;if(S.FreeTalkSpeechReady[Index]==0)S.FreeTalkSpeechReady[Index]=2;return;}
  if(Spoken.Len()>400){int32 Cut=400;for(int32 I=399;I>200;--I)if(FCString::Strchr(TEXT("。！？!?…"),Spoken[I])){Cut=I+1;break;}Spoken.LeftInline(Cut);}}
 // Same synth request as scripted lines, keyed by the personality voice.
 auto Body=MakeShared<FJsonObject>();Body->SetStringField(TEXT("text"),Spoken);Body->SetStringField(TEXT("personality"),FString::FromInt(S.Manifest.Characters[Character].PersonalityId));Body->SetNumberField(TEXT("age"),S.TtsAge);Body->SetStringField(TEXT("out_dir"),S.TtsCacheDir);
 // Protocol 2 fields (an older server ignores them): which reply and line this is, for /cancel and its log.
 const bool bStreamRequest=S.FreeTalkSpeechStreamed[Index]==0;
 Body->SetStringField(TEXT("channel"),S.FreeTalkSpeechChannel);Body->SetNumberField(TEXT("generation"),Generation);Body->SetNumberField(TEXT("segment_index"),Index);
 Body->SetStringField(TEXT("request_id"),FString::Printf(TEXT("%s-%d-%d-%s"),*S.FreeTalkSpeechChannel.Right(8),Generation,Index,bStreamRequest?TEXT("s"):TEXT("f")));
 if(const FString Card=OKStreetCardFor(S,Character);!Card.IsEmpty())Body->SetStringField(TEXT("card"),Card);
 FString Payload;FJsonSerializer::Serialize(Body,TJsonWriterFactory<>::Create(&Payload));
 auto Request=FHttpModule::Get().CreateRequest();
 Request->SetVerb(TEXT("POST"));Request->SetHeader(TEXT("Content-Type"),TEXT("application/json; charset=utf-8"));Request->SetContentAsString(Payload);Request->SetTimeout(S.TtsTimeout);
 if(bStreamRequest||S.FreeTalkSpeechStreamed[Index]==2)S.FreeTalkSpeechSentAt[Index]=FPlatformTime::Seconds();
 if(bStreamRequest){
  S.FreeTalkSpeechStreamed[Index]=1;
  auto Stream=MakeShared<FKKRuntimeSpeechStream,ESPMode::ThreadSafe>();Stream->SampleRate=24000;Stream->Channels=1;S.FreeTalkSpeechStreams[Index]=Stream;
  // 16-bit little-endian mono; HTTP pieces can split a sample, so an odd byte waits for the next piece.
  struct FFeed {uint8 Carry=0;bool HasCarry=false;TArray<int16> Pcm;};auto Feed=MakeShared<FFeed,ESPMode::ThreadSafe>();
  Request->SetResponseBodyReceiveStreamDelegateV2(FHttpRequestStreamDelegateV2::CreateLambda([Stream,Feed](void* Ptr,int64& Length){
   const uint8* B=static_cast<const uint8*>(Ptr);const int64 N=Length;int64 I=0;auto& Pcm=Feed->Pcm;Pcm.Reset();
   if(Feed->HasCarry&&N>0){Pcm.Add(int16(uint16(Feed->Carry)|uint16(B[0])<<8));Feed->HasCarry=false;I=1;}
   for(;I+1<N;I+=2)Pcm.Add(int16(uint16(B[I])|uint16(B[I+1])<<8));
   if(I<N){Feed->Carry=B[I];Feed->HasCarry=true;}
   if(Pcm.Num())Stream->Append(Pcm.GetData(),Pcm.Num());
  }));
  Request->SetURL(S.TtsEndpoint/TEXT("synthesize_stream"));
  Request->OnProcessRequestComplete().BindWeakLambda(this,[this,Generation,Index,Stream](FHttpRequestPtr,FHttpResponsePtr Response,bool Connected){
   const bool Ok=Connected&&Response.IsValid()&&Response->GetResponseCode()==200&&Stream->Num()>0;
   Stream->Finish();
   if(!State||Generation!=State->FreeTalkSpeechGeneration)return;auto& L=*State;
   if(!Ok){L.FreeTalkSpeechStreamed[Index]=2;if(L.FreeTalkSpeechReady[Index]==0)L.FreeTalkSpeechStreams[Index].Reset();UE_LOG(LogTemp,Display,TEXT("Free talk: TTS stream failed (%d), using the file"),Response.IsValid()?Response->GetResponseCode():0);}
   // The streamed line's file for the log (the server answers once it is written, without generating it again),
   // and the next line's stream right away instead of after that file.
   RequestFreeTalkSegment(Generation,Index);
   // Also when a stream that is already playing failed part way (it ran dry, then timed out): its file is only for the
   // log then, and the next girl's line waited behind it (and its timeout) for half a minute in a group talk.
   if((Ok||L.FreeTalkSpeechReady[Index]==3)&&L.FreeTalkSpeechStreamed.IsValidIndex(Index+1)&&L.FreeTalkSpeechStreamed[Index+1]==0)RequestFreeTalkSegment(Generation,Index+1);
  });
  Request->ProcessRequest();
  return;
 }
 Request->SetURL(S.TtsEndpoint/TEXT("synthesize"));
 Request->OnProcessRequestComplete().BindWeakLambda(this,[this,Generation,Index](FHttpRequestPtr,FHttpResponsePtr Response,bool Connected){
  if(!State||Generation!=State->FreeTalkSpeechGeneration)return;auto& L=*State;
  FString Error=OKT(TEXT("ui.scene.tts_connect_failed")),Path;TSharedPtr<FJsonObject> J;bool Ok=false;
  if(Connected&&Response.IsValid()&&FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Response->GetContentAsString()),J)&&J.IsValid()){
   if(!(J->TryGetBoolField(TEXT("ok"),Ok)&&Ok&&J->TryGetStringField(TEXT("path"),Path))){Ok=false;J->TryGetStringField(TEXT("error"),Error);}
  }
  L.FreeTalkSpeechPaths[Index]=Ok?Path:FString();
  const bool Streaming=L.FreeTalkSpeechStreams[Index].IsValid();
  if(!Streaming&&L.FreeTalkSpeechReady[Index]==0)L.FreeTalkSpeechReady[Index]=Ok?1:2;
  if(!Ok&&!Streaming)L.FreeTalkNotice=OKT(TEXT("ui.scene.tts_failed_text_only"))+Error;
  if(L.FreeTalkSpeechStreamed.IsValidIndex(Index+1)&&L.FreeTalkSpeechStreamed[Index+1]==0)RequestFreeTalkSegment(Generation,Index+1);
  if(++L.FreeTalkSpeechFiles<L.FreeTalkSpeechLines.Num())return;
  // Every line answered: keep the clips with the log line ("a.ogg|b.ogg") for the history and replay.
  TArray<FString> Clips;for(const FString& Clip:L.FreeTalkSpeechPaths)if(!Clip.IsEmpty())Clips.Add(Clip);
  const int64 Serial=L.FreeTalkSpeechSerial;
  if(auto* Line=L.FreeTalkLog.FindByPredicate([Serial](const FOKFreeTalkLine& X){return X.Serial==Serial;})){Line->Voice=FString::Join(Clips,TEXT("|"));FreeTalkSaveLog();}
 });
 Request->ProcessRequest();
}

// His free-talk line (SendFreeTalk): MalePhase 1 waits for its clip (or gives up after the TTS timeout), 2 while he
// speaks, 3 done. Scripted lines run their own male phase inside the node.
// How long her ready reply waits for his voice line still being made.
static constexpr double OKStreetHisVoiceGraceSeconds=3.0;

bool AOKStreetSocialScene::FreeTalkHisVoiceBusy() const{
 const auto& S=*State;return S.Node==INDEX_NONE&&(S.MalePhase==1||S.MalePhase==2);
}

void AOKStreetSocialScene::TickFreeTalkHisVoice(double Now){
 auto& S=*State;if(!FreeTalkHisVoiceBusy())return;
 if(S.MalePhase==1){
  if(!S.MaleVoiceId.IsEmpty()){FString Error;S.MalePhase=OKSpeakCrowdRuntimeLine(this,0,S.MaleVoiceId,Error)?2:3;
   if(S.MalePhase==3){UE_LOG(LogTemp,Warning,TEXT("Free talk: his line not spoken: %s"),*Error);}
   else{UE_LOG(LogTemp,Display,TEXT("Free talk: he says his line (tone %s)"),*S.MaleTone);}}
  else{
   // Her reply is in and his voice is still not made (a busy GPU): a few seconds more, then he does not say it
   // aloud (his words are on screen) and the server drops it, so her lines are made next instead of after his.
   const bool HerReplyWaiting=S.FreeTalkSpeechPlayed<S.FreeTalkSpeechLines.Num()&&FPlatformTime::Seconds()-S.FreeTalkSpeechRepliedAt>OKStreetHisVoiceGraceSeconds;
   if(!S.MaleTtsPending||HerReplyWaiting||Now-S.MaleRequestedAt>S.TtsTimeout+1){
    if(S.MaleTtsPending){OKStreetTtsCancel(S.TtsEndpoint,OKStreetMaleTtsChannel(this),S.MaleSerial+1);UE_LOG(LogTemp,Display,TEXT("Free talk: his line is not spoken (%s)"),HerReplyWaiting?TEXT("her reply is ready"):TEXT("timeout"));}
    S.MaleTtsPending=false;S.MalePhase=3;}
  }
 }else if(S.MalePhase==2&&!OKCrowdRuntimeSpeechActive(this,0))S.MalePhase=3;
}

// Plays the next ready line when she is quiet. Never over a scripted line or a replay; leaving her stops it.
// Paced on the wall clock: the scene clock stands still in the one-on-one room, and with it every line after her first
// waited for a time that never came (only one line voiced per visit).
void AOKStreetSocialScene::TickFreeTalkSpeech(double Now){
 auto& S=*State;const int32 Character=S.FreeTalkSpeechSpeakers.IsValidIndex(S.FreeTalkSpeechPlayed)?S.FreeTalkSpeechSpeakers[S.FreeTalkSpeechPlayed]:S.FreeTalkSpeechCharacter;
 if(Character==INDEX_NONE||S.FreeTalkSpeechPlayed>=S.FreeTalkSpeechLines.Num()||Now<S.FreeTalkSpeechNext)return;
 if(SongOwnsVoice(Character))return; // she is singing (or saying its lines): her replies wait for the song
 if(FreeTalkHisVoiceBusy())return; // he is still saying his line
 if(FreeTalkPartner()!=Character&&!(S.FreeTalkSpeechSpeakers.Num()>0&&FreeTalkPartner()!=INDEX_NONE&&(S.Party.Contains(Character)||Character==FreeTalkPartner()))){S.FreeTalkSpeechPlayed=S.FreeTalkSpeechLines.Num();return;}
 if(S.Node!=INDEX_NONE||!S.FreeTalkReplay.IsEmpty()||!S.FreeTalkReplayParts.IsEmpty()||OKCrowdRuntimeSpeechActive(this,Character))return;
 // In the H scene her line is spoken on her high model (the street's actors are hidden there): the finished file, not
 // the stream, which plays on the street actor.
 if(S.PrivateGirl==Character&&S.PrivateMode.IsValid()){
  if(S.PrivateMode->HSceneSpeaking())return;const int32 Index=S.FreeTalkSpeechPlayed;
  if(S.FreeTalkSpeechPaths[Index].IsEmpty()&&S.FreeTalkSpeechReady[Index]!=2&&FPlatformTime::Seconds()-S.FreeTalkSpeechRepliedAt<S.TtsTimeout+5)return;
  ++S.FreeTalkSpeechPlayed;if(!S.FreeTalkSpeechPaths[Index].IsEmpty())S.PrivateMode->HSceneSpeak(S.FreeTalkSpeechPaths[Index]);
  else UE_LOG(LogTemp,Warning,TEXT("H scene: her line %d is not voiced (no file %.1fs after the reply)"),Index,FPlatformTime::Seconds()-S.FreeTalkSpeechRepliedAt);
  S.FreeTalkSpeechNext=Now+.1;return;
 }
 // Several of them (a party): one at a time. The next girl waits until whoever spoke before her is done, not only
 // until she herself is quiet (two heroines talked over each other).
 for(const int32 Other:S.FreeTalkSpeechSpeakers)if(Other!=Character&&OKCrowdRuntimeSpeechActive(this,Other))return;
 const int32 Index=S.FreeTalkSpeechPlayed;
 // A stream that has buffered enough (or finished short) is playable before its file exists.
 if(const auto& Stream=S.FreeTalkSpeechStreams[Index];Stream&&S.FreeTalkSpeechReady[Index]==0&&S.FreeTalkSpeechStreamed[Index]!=2&&(Stream->Num()>=OKFreeTalkStreamStartSamples||(Stream->IsComplete()&&Stream->Num()>0)))S.FreeTalkSpeechReady[Index]=3;
 if(S.FreeTalkSpeechReady[Index]==0)return;
 ++S.FreeTalkSpeechPlayed;FString Error;
 // Latency as the player hears it: from her reply (all lines known) and from this line's request to its first sound.
 UE_LOG(LogTemp,Display,TEXT("Free talk: TTS line %d plays %.3fs after its request, %.3fs after the reply (%s)"),Index,FPlatformTime::Seconds()-S.FreeTalkSpeechSentAt[Index],FPlatformTime::Seconds()-S.FreeTalkSpeechRepliedAt,S.FreeTalkSpeechReady[Index]==3?TEXT("stream"):TEXT("file"));
 if(S.FreeTalkSpeechReady[Index]==3&&!OKSpeakCrowdRuntimeStream(this,Character,S.FreeTalkSpeechStreams[Index],Error))S.FreeTalkNotice=OKT(TEXT("ui.scene.voice_play_failed"));
 if(S.FreeTalkSpeechReady[Index]==1&&!FreeTalkSpeakVoice(Character,S.FreeTalkSpeechPaths[Index]))S.FreeTalkNotice=OKT(TEXT("ui.scene.voice_play_failed"));
 S.FreeTalkSpeechNext=Now+.1;
}

// ---- Free-talk files beside the relationship save: the shared log (at most OKFreeTalkLogLimit lines,
// for the history window and replay) and each girl's memory (sent to the model). Self-tests keep both in memory.
FString AOKStreetSocialScene::FreeTalkFilePath(const TCHAR* Name) const{
 if(!State||State->SelfTest||State->LiveTestPhase>=0||State->Manifest.SavePath.IsEmpty())return FString();
 return FPaths::GetPath(State->Manifest.SavePath)/Name;
}

void AOKStreetSocialScene::FreeTalkEnsureLog(){
 auto& S=*State;if(S.FreeTalkLogLoaded)return;S.FreeTalkLogLoaded=true;
 const FString LogFile=FreeTalkFilePath(TEXT("free-talk-log.json")),MemoryFile=FreeTalkFilePath(TEXT("free-talk-memory.json"));
 OKStreetFlushSaves();if(!LogFile.IsEmpty())OKStreetFreeTalkLoadLog(LogFile,S.FreeTalkLog);
 if(!MemoryFile.IsEmpty())OKStreetFreeTalkLoadMemory(MemoryFile,S.FreeTalkMemory);
 for(const auto& Line:S.FreeTalkLog)S.FreeTalkSerial=FMath::Max(S.FreeTalkSerial,Line.Serial);
}

int64 AOKStreetSocialScene::FreeTalkAddLine(int32 Character,bool Player,const FString& Text){
 auto& S=*State;FreeTalkEnsureLog();
 FOKFreeTalkLine Line;Line.Serial=++S.FreeTalkSerial;Line.Player=Player;Line.Text=Text;
 Line.CharacterId=OKStreetStableId(S.Manifest.Characters[Character]);
 Line.Speaker=S.Manifest.Characters[Player?0:Character].Name;
 Line.Time=FDateTime::Now().ToString(TEXT("%m-%d %H:%M"));
 S.FreeTalkLog.Add(MoveTemp(Line));
 HistoryAdd(Character,RemoteWith(Character)?(RemoteKind(Character)==2?TEXT("call"):TEXT("message")):TEXT("talk"),Player?TEXT("him"):TEXT("her"),Text); // her own history
 if(S.FreeTalkLog.Num()>OKFreeTalkLogLimit)S.FreeTalkLog.RemoveAt(0,S.FreeTalkLog.Num()-OKFreeTalkLogLimit);
 // The whole log is rewritten on save: not once per line (an overheard chat of six lines wrote it six times in a frame).
 S.FreeTalkLogDirty=true;
 return S.FreeTalkSerial;
}

// The log and the memory are written by the save task (a snapshot serialized there): serialized and written here they
// held the frame 7 ms. Whatever reads the files back (a save slot, a load, the end of the game) flushes the task first.
void AOKStreetSocialScene::FreeTalkSaveLog(){
 const FString Path=FreeTalkFilePath(TEXT("free-talk-log.json"));if(Path.IsEmpty())return;
 const auto Snapshot=MakeShared<const TArray<FOKFreeTalkLine>,ESPMode::ThreadSafe>(State->FreeTalkLog);
 OKStreetAtomicSaveQueued(Path,[Snapshot](){return OKStreetFreeTalkLogJson(*Snapshot);});
}

void AOKStreetSocialScene::FreeTalkSaveMemory(){
 const FString Path=FreeTalkFilePath(TEXT("free-talk-memory.json"));if(Path.IsEmpty())return;
 const auto Snapshot=MakeShared<const TMap<FString,TArray<FOKFreeTalkTurn>>,ESPMode::ThreadSafe>(State->FreeTalkMemory);
 OKStreetAtomicSaveQueued(Path,[Snapshot](){return OKStreetFreeTalkMemoryJson(*Snapshot);});
}

int32 AOKStreetSocialScene::FreeTalkSlotOf(const FString& CharacterId) const{
 for(int32 I=1;I<State->Manifest.Characters.Num();++I)if(OKStreetStableId(State->Manifest.Characters[I])==CharacterId)return I;
 return INDEX_NONE;
}

bool AOKStreetSocialScene::FreeTalkSpeakVoice(int32 Character,const FString& Path){
 FString Id,Error;
 return !Path.IsEmpty()&&IFileManager::Get().FileExists(*Path)&&OKAdmitCrowdRuntimeVoice(this,Path,Id,Error)&&OKSpeakCrowdRuntimeLine(this,Character,Id,Error);
}

// Replay the current partner's last OKFreeTalkReplayCount lines through the bottom dialogue box,
// speaking her lines again; a second press stops it.
void AOKStreetSocialScene::FreeTalkReplayToggle(){
 if(!State)return;auto& S=*State;
 if(!S.FreeTalkReplay.IsEmpty()||!S.FreeTalkReplayParts.IsEmpty()){S.FreeTalkReplay.Reset();S.FreeTalkReplayParts.Reset();S.FreeTalkReplayHold=0;S.FreeTalkNotice=OKT(TEXT("ui.scene.replay_stopped"));RefreshUI();return;}
 FreeTalkEnsureLog();
 const int32 Partner=FreeTalkPartner();
 const FString Id=Partner!=INDEX_NONE?OKStreetStableId(S.Manifest.Characters[Partner]):FString();
 for(int32 I=S.FreeTalkLog.Num()-1;I>=0&&S.FreeTalkReplay.Num()<OKFreeTalkReplayCount;--I)
  if(Id.IsEmpty()||S.FreeTalkLog[I].CharacterId==Id)S.FreeTalkReplay.Insert(S.FreeTalkLog[I].Serial,0);
 S.FreeTalkReplayTotal=S.FreeTalkReplay.Num();S.FreeTalkReplayNext=0;S.FreeTalkReplaySpeaker=INDEX_NONE;
 S.FreeTalkNotice=S.FreeTalkReplay.IsEmpty()?OKT(TEXT("ui.scene.no_freetalk_records")):OKT(TEXT("ui.scene.replaying_ellipsis"));
 RefreshUI();
}

void AOKStreetSocialScene::FreeTalkPlayLine(int64 Serial){
 if(!State)return;auto& S=*State;
 S.FreeTalkReplay={Serial};S.FreeTalkReplayParts.Reset();S.FreeTalkReplayTotal=1;S.FreeTalkReplayNext=0;S.FreeTalkReplaySpeaker=INDEX_NONE;
 RefreshUI();
}

// One replay line at a time: wait for her voice (or a reading pause for text-only lines), never over a scripted line.
// A line can have several clips ("a.ogg|b.ogg"); they play back to back before the next line.
// Captions and the notice are bound to state, so this does not rebuild the UI (a rebuild drops keyboard focus).
void AOKStreetSocialScene::TickFreeTalkReplay(double Now){
 auto& S=*State;
 if((S.FreeTalkReplay.IsEmpty()&&S.FreeTalkReplayParts.IsEmpty())||Now<S.FreeTalkReplayNext||S.Node!=INDEX_NONE)return;
 if(S.FreeTalkReplaySpeaker!=INDEX_NONE&&OKCrowdRuntimeSpeechActive(this,S.FreeTalkReplaySpeaker))return;
 if(!S.FreeTalkReplayParts.IsEmpty()){
  const FString Clip=S.FreeTalkReplayParts[0];S.FreeTalkReplayParts.RemoveAt(0);
  if(S.FreeTalkReplaySpeaker==INDEX_NONE||!FreeTalkSpeakVoice(S.FreeTalkReplaySpeaker,Clip))S.FreeTalkReplayParts.Reset();else ++S.FreeTalkReplayClip;
  S.FreeTalkReplayNext=Now+.1;
  return;
 }
 const int64 Serial=S.FreeTalkReplay[0];S.FreeTalkReplay.RemoveAt(0);S.FreeTalkReplaySpeaker=INDEX_NONE;
 if(const FOKFreeTalkLine* Line=S.FreeTalkLog.FindByPredicate([Serial](const FOKFreeTalkLine& L){return L.Serial==Serial;})){
  // Her voiced lines speak again. With the galgame scene every line (his too) is shown in its box with the speaker's
  // portrait, a line without a voice for a reading time; without it the dialogue box keeps the current conversation.
  const int32 Girl=FreeTalkSlotOf(Line->CharacterId);
  bool Voiced=false;
  if(!Line->Player){
   TArray<FString> Clips;Line->Voice.ParseIntoArray(Clips,TEXT("|"));
   if(Girl!=INDEX_NONE&&!Clips.IsEmpty()&&FreeTalkSpeakVoice(Girl,Clips[0])){S.FreeTalkReplaySpeaker=Girl;S.FreeTalkReplayClip=0;S.FreeTalkReplayClips=Clips.Num();Clips.RemoveAt(0);S.FreeTalkReplayParts=MoveTemp(Clips);Voiced=true;}
  }
  S.FreeTalkReplayNext=Now+.1;
  if(Girl!=INDEX_NONE&&GalgameReplays()){
   ++S.FreeTalkReplayShown;S.FreeTalkReplayShownSlot=Line->Player?0:Girl;S.FreeTalkReplaySceneSlot=Girl;S.FreeTalkReplayShownText=Line->Text;S.FreeTalkReplayShownVoiced=Voiced;
   if(!Voiced)S.FreeTalkReplayNext=Now+FMath::Clamp(.8+.12*Line->Text.Len(),1.5,6.);
   S.FreeTalkReplayHold=S.FreeTalkReplayNext;
  }
 }
 const int32 Done=S.FreeTalkReplayTotal-S.FreeTalkReplay.Num();
 S.FreeTalkNotice=S.FreeTalkReplay.IsEmpty()?FString(OKT(TEXT("ui.scene.replay_finished"))):OKMsg(TEXT("ui.scene.replay_progress"), {{TEXT("done"), Done}, {TEXT("total"), S.FreeTalkReplayTotal}});
 if(S.FreeTalkReplay.IsEmpty())RefreshUI(); // the button goes back to "回放"
}

void AOKStreetSocialScene::ToggleLanguage(){
 if(!State||OKStreetConversationBusy(State->Node!=INDEX_NONE,State->CollectedActive,State->RelationshipPending,State->TouchActive)||State->OtherLanguage.Nodes.IsEmpty()||!State->OtherLanguage.Error.IsEmpty())return;
 Swap(State->Catalogue,State->OtherLanguage);State->Japanese=!State->Japanese;
 State->Message=State->Japanese?OKT(TEXT("ui.scene.voice_policy_ja")):OKT(TEXT("ui.scene.voice_policy_zh_first"));RefreshUI();
}

void AOKStreetSocialScene::RelationshipEvent(const FString& Event,bool bHerYes){
 if(!State)return;
 UE_LOG(LogTemp,Display,TEXT("Street relationship event requested: %s (target=%d node=%d pending=%d collected=%d)"),*Event,State->Target,State->Node,State->RelationshipPending?1:0,State->CollectedActive?1:0);
 if(State->Target==INDEX_NONE||State->Node!=INDEX_NONE||State->RelationshipPending||State->CollectedActive){
  // Not silently: say why nothing happened.
  UE_LOG(LogTemp,Display,TEXT("Street relationship event %s ignored: target=%d node=%d pending=%d collected=%d"),*Event,State->Target,State->Node,State->RelationshipPending?1:0,State->CollectedActive?1:0);
  State->Message=State->Target==INDEX_NONE?OKT(TEXT("ui.scene.confess_need_face_to_face")):OKT(TEXT("ui.scene.she_still_busy"));RefreshUI();return;
 }
 auto& S=*State;
 // Whether it happens and how is the rules' (talk.rs talk_event).
 const auto& Meta=S.Manifest.Characters[S.Target];
 auto Ask=OKRulesTalkRequest(S,TEXT("talk_event"),OKStreetViewRelation(S.Store,OKStreetStableId(Meta),S.Manifest.TalkBudget),&Meta);Ask->SetStringField(TEXT("event"),Event);Ask->SetNumberField(TEXT("roll"),S.Random.RandRange(0,99));Ask->SetBoolField(TEXT("her_yes"),bHerYes);
 // A very shy girl answers differently with people around (another girl within 10 m of her, or a busy place now).
 if(Event==TEXT("confession")&&S.Agents.IsValidIndex(S.Target-1)){
  const FVector Her=S.Agents[S.Target-1].Position;int32 Others=0;
  for(int32 G=1;G<S.Manifest.Characters.Num();++G)if(G!=S.Target&&S.Agents.IsValidIndex(G-1)&&FVector::Dist(S.Agents[G-1].Position,Her)<1000.f)++Others;
  const FDateTime Now=StreetNow();
  if(const auto P=ProfileOf(S.Target))Ask->SetObjectField(TEXT("profile"),P.ToSharedRef());
  Ask->SetNumberField(TEXT("others_near"),Others);Ask->SetStringField(TEXT("place"),PlaceHere(Her));Ask->SetNumberField(TEXT("hours"),Now.GetHour()+Now.GetMinute()/60.0);Ask->SetNumberField(TEXT("shy_roll"),S.Random.RandRange(0,99));
 }
 const TSharedPtr<FJsonObject> Answer=OKRulesEval(S,Ask);if(!Answer){S.Message=OKT(TEXT("ui.scene.dialog_rule_no_response"));RefreshUI();return;}
 struct{bool Resolved=false,Refusal=false,Playable=false;int32 ChancePercent=0,Roll=0,LineNode=INDEX_NONE;FString Reason,Policy;FOKStreetEffect Effect;} Preview;
 Preview.Resolved=Answer->GetBoolField(TEXT("resolved"));Preview.Refusal=Answer->GetBoolField(TEXT("refusal"));Preview.Playable=Answer->GetBoolField(TEXT("playable"));Preview.ChancePercent=int32(Answer->GetNumberField(TEXT("chance")));Preview.Roll=int32(Answer->GetNumberField(TEXT("roll")));
 Preview.Reason=Answer->GetStringField(TEXT("reason"));Preview.Effect=OKRulesReadEffect(*Answer->GetObjectField(TEXT("effect")));
 if(const int32* Line=S.Catalogue.ById.Find(Answer->GetStringField(TEXT("line"))))Preview.LineNode=*Line;else Preview.Playable=false;
 S.PlayerLine.Reset();S.RelationshipSequence.Reset();S.RelationshipSilentUntil=0;
 if(!Preview.Resolved)UE_LOG(LogTemp,Display,TEXT("Street relationship event %s not resolved: %s"),*Event,*Preview.Reason);
 // She said yes aloud, but they are not there yet (the favor the rules ask for): she is told, takes it back in her own
 // way, and her answer is still to come. 胡桃 said "I accept" and stayed his friend, thinking they were together (R4-22).
 if(!Preview.Resolved&&Event==TEXT("confession")&&bHerYes&&S.Target!=INDEX_NONE){S.TellHer.Add(OKT(TEXT("ui.scene.confession_not_yet_note")));S.ConfessionPending.Add(S.Target,OKStreetMindNow(S));}
 if(!Preview.Resolved){S.Message=Answer->GetStringField(TEXT("message"));RefreshUI();return;} // the rules say why, with the numbers
 S.RelationshipShy.Reset();Answer->TryGetStringField(TEXT("shy"),S.RelationshipShy);
 S.PendingEffect=Preview.Effect;S.RelationshipPending=true;S.RelationshipEventName=Event;S.BranchLine=false;S.RelationshipVoiceRequired=Preview.Playable;S.RelationshipVoiceCompleted=false;
 UE_LOG(LogTemp,Display,TEXT("Street relationship preview: event=%s actor=%d chance=%d roll=%d refusal=%d reason=%s policy=%s shy=%s"),*Event,S.Target,Preview.ChancePercent,Preview.Roll,Preview.Refusal,*Preview.Reason,*Preview.Policy,*S.RelationshipShy);
 // Too shy to say yes to his face: it is settled now, she runs off, and her yes comes by phone a minute later.
 if(Event==TEXT("confession")&&S.RelationshipShy==TEXT("runs_away")){
  const int32 Girl=S.Target;S.RelationshipVoiceRequired=false;ApplyRelationship();
  FreeTalkLeave(OKMsg(TEXT("ui.scene.confession_ran_away"),{{TEXT("name"),*S.Manifest.Characters[Girl].Name}}));
  QueueHerText(Girl,OKT(TEXT("ui.scene.confession_ran_away_why")),TEXT("confession"),60.0);return;
 }
 // He already confessed in his own words and she said yes in hers (the judge): the scripted confession would make him
 // ask again in canned lines and her act surprised. It is settled now; she is told, and the talk goes on.
 if(Event==TEXT("confession")&&bHerYes&&!Preview.Refusal){
  UE_LOG(LogTemp,Display,TEXT("Street confession: actor=%d answered in their own words, no scripted scene"),S.Target);
  S.RelationshipVoiceRequired=false;ApplyRelationship();return;
 }
 if(Event==TEXT("confession")){
  const auto* Dialogue=S.RelationshipDialogues.FindByPredicate([&](const FOKStreetRelationshipDialogue& D){return D.Personality==S.Manifest.Characters[S.Target].PersonalityId;});
  if(!Dialogue){S.RelationshipPending=false;S.Message=OKT(TEXT("ui.scene.confess_not_ready"));RefreshUI();return;}
  S.RelationshipSequence=Preview.Refusal?Dialogue->Refuse:Dialogue->Accept;S.RelationshipPart=0;S.RelationshipVoiceRequired=true;AdvanceRelationshipDialogue();return;
 }
 if(Preview.Playable){QueueNode(Preview.LineNode,TEXT("relationship"));return;}
 // Original rank-up/confession rows may open an ADV scenario instead of PCM.
 // Do not replace such scripts with an unrelated voiced reply.
 S.Message=OKT(TEXT("ui.scene.already_friends"));ApplyRelationship();
}

void AOKStreetSocialScene::AdvanceRelationshipDialogue(){
 auto& S=*State;if(!S.RelationshipPending||S.Target==INDEX_NONE)return;
 if(S.RelationshipSequence.IsValidIndex(S.RelationshipPart)){
  const auto Part=S.RelationshipSequence[S.RelationshipPart++];
  if(Part.Node==INDEX_NONE){SetSourcePlayerLine(OKStreetMaleLine(Part.MaleText));S.Message.Reset();S.RelationshipSilentUntil=SceneTime()+2;RefreshUI();return;}
  if(!QueueNode(Part.Node,TEXT("relationship")))return;SetSourcePlayerLine(OKStreetMaleLine(Part.MaleText));RefreshUI();return;
 }
 S.RelationshipVoiceCompleted=true;S.ActiveCategory.Reset();FString Error;ResumeConversationPose(S.Target,Error);ResumeConversationPose(0,Error);ApplyRelationship();
}

void AOKStreetSocialScene::ApplyRelationship(){
 if(!State||!OKStreetRelationshipMayCommit(State->RelationshipPending,State->Target!=INDEX_NONE,State->Node!=INDEX_NONE,State->RelationshipVoiceRequired,State->RelationshipVoiceCompleted))return;auto& S=*State;auto Candidate=S.Store;const FString Transaction=FGuid::NewGuid().ToString(EGuidFormats::Digits);FString Error;
 const FString StageBefore=OKStreetViewRelation(S.Store,OKStreetStableId(S.Manifest.Characters[S.Target]),S.Manifest.TalkBudget).Stage;
 const auto Result=OKRulesTalkCommit(S,Candidate,OKStreetStableId(S.Manifest.Characters[S.Target]),Transaction,S.PendingEffect);
 if(!Result.Applied||!OKStreetAtomicSave(S.Manifest.SavePath,OKStreetWriteSave(Candidate),Error)){S.Errors.Add(TEXT("Relationship save failed: ")+Result.Reason+Error);S.Message=OKT(TEXT("ui.scene.relation_not_saved"))+Result.Reason+Error;RefreshUI();return;}
 S.Store=MoveTemp(Candidate);
 {const FString StageAfter=OKStreetViewRelation(S.Store,OKStreetStableId(S.Manifest.Characters[S.Target]),S.Manifest.TalkBudget).Stage;FOKHeroineArgs A;A.RecordOnly=true;
  if(S.RelationshipEventName==TEXT("confession")){A.Detail=StageAfter==TEXT("lover")?OKT(TEXT("ui.scene.you_accepted_lovers_suffix")):OKT(TEXT("ui.scene.you_refused_suffix"));Interact(S.Target,TEXT("confession"),A);
   // The confession was the original voiced lines, not her conversation: she is told what just happened, so what she
   // says next follows from it (and her talk remembers it).
   if(S.RelationshipShy==TEXT("onlookers")){S.TellHer.Add(OKT(TEXT("ui.scene.confession_shy_refused_note")));S.Message=OKT(TEXT("ui.scene.confession_shy_onlookers_hint"));}
   else if(S.RelationshipShy!=TEXT("runs_away"))S.TellHer.Add(OKT(StageAfter==TEXT("lover")?TEXT("ui.scene.confession_accepted_note"):TEXT("ui.scene.confession_refused_note")));}
  else if(StageAfter!=StageBefore){Interact(S.Target,TEXT("friendship"),A);}}
 S.Transactions.Add(Transaction);S.RelationshipSequence.Reset();S.RelationshipSilentUntil=0;S.RelationshipPending=false;S.RelationshipVoiceRequired=false;S.RelationshipVoiceCompleted=false;RefreshUI();
}


void AOKStreetSocialScene::PickTopic(int32 Index){
 if(!State)return;auto& S=*State;if(S.Target!=INDEX_NONE||!S.PickupTopics.IsValidIndex(Index)||!S.TopicPickups[Index].IsValid()||FVector::Dist2D(S.Male,S.TopicPickups[Index]->GetActorLocation())>150)return;
 const int32 No=S.PickupTopics[Index];const auto* Topic=S.CollectedTopics.Find(No);if(!Topic)return;
 auto Candidate=S.Store;
 {auto Ask=OKRulesRequest(TEXT("topic_collect"));Ask->SetNumberField(TEXT("no"),No);Ask->SetNumberField(TEXT("stock"),Candidate.TopicStock.FindRef(No));Ask->SetNumberField(TEXT("day"),Candidate.TopicDay);if(const int32* Claimed=Candidate.TopicClaims.Find(No))Ask->SetNumberField(TEXT("claimed"),*Claimed);
  const TSharedPtr<FJsonObject> Out=OKRulesEval(S,Ask);if(!Out||!Out->GetBoolField(TEXT("ok"))){S.Message=OKT(TEXT("ui.scene.topic_pickup_limit"));return;}
  ++Candidate.TopicStock.FindOrAdd(No);Candidate.TopicClaims.Add(No,Candidate.TopicDay);}
 // Written by the save task like every other commit (picked up while walking, a synchronous write held the frame). Nothing
 // on the panel changes: the topics he holds are listed only in a conversation, and the message is bound.
 S.Store=MoveTemp(Candidate);
 if(!S.Manifest.SavePath.IsEmpty())OKStreetAtomicSaveStoreAsync(S.Manifest.SavePath,S.Store);
 S.TopicPickups[Index]->SetActorHiddenInGame(true);S.Message=OKMsg(TEXT("ui.scene.topic_acquired"), {{TEXT("topic"), *Topic->Name}, {TEXT("count"), S.Store.TopicStock.FindRef(No)}});
}
void AOKStreetSocialScene::AskHerTopic(){
 if(!State)return;auto& S=*State;if(S.Target==INDEX_NONE||S.Node!=INDEX_NONE||S.RelationshipPending||S.CollectedActive)return;
 const auto* Profile=S.CollectedTopics.Profile(S.Manifest.Characters[S.Target].PersonalityId);if(!Profile||Profile->Listen.IsEmpty())return;
 const auto Relation=OKStreetViewRelation(S.Store,OKStreetStableId(S.Manifest.Characters[S.Target]),S.Manifest.TalkBudget);if(GOKStreetTalkLimit&&Relation.TalksRemaining<1){S.Message=OKT(TEXT("ui.scene.no_talks_left_today"));RefreshUI();return;}
 TArray<int32> Pool;for(int32 I=0;I<Profile->Listen.Num();++I)if(!Relation.ListenRecent.Contains(Profile->Listen[I].Topic))Pool.Add(I);
 if(Pool.IsEmpty())return;const auto& Listen=Profile->Listen[Pool[S.Random.RandRange(0,Pool.Num()-1)]];
 S.AskedTopic=Listen.Topic;S.UsedTopic=INDEX_NONE;S.CollectedActive=true;S.TopicIntroduction=true;S.AwaitTopicAnswer=false;S.TopicSequence=Listen.Intro;S.TopicPart=0;S.CollectedVoiceIds.Reset();S.PlayerLine=OKT(TEXT("ui.scene.your_turn_topic"));AdvanceCollectedTopic();
}
void AOKStreetSocialScene::UseCollectedTopic(int32 No){
 if(!State)return;auto& S=*State;if(S.Target==INDEX_NONE||S.Node!=INDEX_NONE||S.RelationshipPending||(S.CollectedActive&&!S.AwaitTopicAnswer))return;
 const auto* Answer=S.CollectedTopics.Find(No);if((!Answer||S.Store.TopicStock.FindRef(No)<1)&&!(S.AwaitTopicAnswer&&No==INDEX_NONE))return;
 const auto* Profile=S.CollectedTopics.Profile(S.Manifest.Characters[S.Target].PersonalityId);if(!Profile)return;
 const auto Relation=OKStreetViewRelation(S.Store,OKStreetStableId(S.Manifest.Characters[S.Target]),S.Manifest.TalkBudget);if(GOKStreetTalkLimit&&Relation.TalksRemaining<1)return;
 const auto* Asked=S.AwaitTopicAnswer?S.CollectedTopics.Find(S.AskedTopic):nullptr;
 TArray<FOKStreetTopicPart> Sequence;
 // What the answer is worth and how well it matches are rules (talk.rs topic_effect).
 auto Worth=OKRulesRequest(TEXT("topic_effect"));if(Answer)Worth->SetNumberField(TEXT("answer"),Answer->No);if(Asked)Worth->SetNumberField(TEXT("asked"),Asked->No);
 {TArray<TSharedPtr<FJsonValue>> Interests;for(const int32 C:S.Manifest.Characters[S.Target].InterestCategories)Interests.Add(MakeShared<FJsonValueNumber>(C));Worth->SetArrayField(TEXT("interests"),Interests);}
 if(const auto P=ProfileOf(S.Target))Worth->SetObjectField(TEXT("profile"),P.ToSharedRef()); // her libido scales an erotic topic's desire
 const TSharedPtr<FJsonObject> Worthy=OKRulesEval(S,Worth);if(!Worthy)return;
 if(Asked){const FOKStreetTopicListen* Listen=nullptr;for(const auto& L:Profile->Listen)if(L.Topic==Asked->No)Listen=&L;if(!Listen)return;const int32 Match=int32(Worthy->GetNumberField(TEXT("match")));const auto& Choices=Match==0?Listen->Success:Match==1?Listen->Normal:Listen->Failure;if(Choices.IsEmpty())return;Sequence.Add(Choices[S.Random.RandRange(0,Choices.Num()-1)]);}
 else{if(!Answer)return;if(Answer->Rarity>=2&&Relation.Topics.Contains(OKStreetTopicToken(No))){S.Message=OKT(TEXT("ui.scene.rare_topic_used"));RefreshUI();return;}const auto* Choices=Profile->Offers.Find(No);if(!Choices||Choices->IsEmpty())return;Sequence=(*Choices)[S.Random.RandRange(0,Choices->Num()-1)];S.AskedTopic=INDEX_NONE;}
 S.CollectedEffect=OKRulesReadEffect(*Worthy->GetObjectField(TEXT("effect")));S.CollectedPoint=int32(Worthy->GetNumberField(TEXT("point")));S.CollectedDesire=float(Worthy->GetNumberField(TEXT("sex_desire")));
 S.UsedTopic=No;S.CollectedActive=true;S.TopicIntroduction=false;S.AwaitTopicAnswer=false;S.TopicSequence=MoveTemp(Sequence);S.TopicPart=0;S.CollectedVoiceIds.Reset();
 S.PlayerLine=Answer?OKT(TEXT("ui.scene.i_know_topic"))+Answer->Name:OKT(TEXT("ui.scene.dont_understand"));AdvanceCollectedTopic();
}
void AOKStreetSocialScene::AdvanceCollectedTopic(){
 auto& S=*State;if(!S.CollectedActive||S.Target==INDEX_NONE)return;
 if(S.TopicSequence.IsValidIndex(S.TopicPart)){
  const auto Part=S.TopicSequence[S.TopicPart++];
  if(Part.Node==INDEX_NONE){S.PlayerLine=OKStreetMaleLine(Part.MaleText);S.SilentTopicUntil=SceneTime()+1.5;RefreshUI();return;}
  S.MaleLineOverride=OKStreetMaleLine(Part.MaleText);
  if(!QueueNode(Part.Node,TEXT("collected"))){S.MaleLineOverride.Reset();return;}
  S.CollectedVoiceIds.Append(S.VoiceIds);
  RefreshUI();return;
 }
 if(S.TopicIntroduction){S.TopicIntroduction=false;S.AwaitTopicAnswer=true;S.Message=OKT(TEXT("ui.scene.her_proposed_topics"))+S.CollectedTopics.Find(S.AskedTopic)->Name+OKT(TEXT("ui.scene.pick_topic_tail"));S.MouseUI=true;ApplyMouseMode(S.ViewportFocused);FString Error;ResumeConversationPose(S.Target,Error);ResumeConversationPose(0,Error);RefreshUI();return;}
 CompleteCollectedTopic();
}
void AOKStreetSocialScene::CompleteCollectedTopic(){
 auto& S=*State;if(!S.CollectedActive||S.AwaitTopicAnswer||S.Target==INDEX_NONE)return;
 auto Candidate=S.Store;const FString Id=OKStreetStableId(S.Manifest.Characters[S.Target]);const FString Transaction=FGuid::NewGuid().ToString(EGuidFormats::Digits);const auto Before=OKStreetViewRelation(Candidate,Id,S.Manifest.TalkBudget);
 if(S.UsedTopic!=INDEX_NONE){int32* Stock=Candidate.TopicStock.Find(S.UsedTopic);if(!Stock||*Stock<1){FailTalk(TEXT("Topic stock disappeared before commit"));return;}--*Stock;}
 const auto Result=OKRulesTalkCommit(S,Candidate,Id,Transaction,S.CollectedEffect);if(!Result.Applied){FailTalk(TEXT("Collected topic commit rejected: ")+Result.Reason);return;}
 auto& Relation=Candidate.Relations[OKStreetRelationIndex(Candidate,Id,S.Manifest.TalkBudget,true)];Relation.TopicPoints+=S.CollectedPoint;
 if(S.AskedTopic!=INDEX_NONE){Relation.ListenRecent.Add(S.AskedTopic);if(Relation.ListenRecent.Num()>5)Relation.ListenRecent.RemoveAt(0);}
 else if(S.CollectedDesire>0){Relation.SexDesire=FMath::Min(100.f,Relation.SexDesire+S.CollectedDesire);}
 FString Error;if(!OKStreetAtomicSave(S.Manifest.SavePath,OKStreetWriteSave(Candidate),Error)){FailTalk(TEXT("Collected topic atomic save failed: ")+Error);return;}
 const auto After=Relation;
 auto Evidence=MakeShared<FJsonObject>();Evidence->SetStringField(TEXT("id"),Id);Evidence->SetStringField(TEXT("transaction"),Transaction);Evidence->SetNumberField(TEXT("personality"),S.Manifest.Characters[S.Target].PersonalityId);Evidence->SetNumberField(TEXT("asked"),S.AskedTopic);Evidence->SetNumberField(TEXT("used"),S.UsedTopic);Evidence->SetNumberField(TEXT("favor_before"),Before.Favor);Evidence->SetNumberField(TEXT("favor_after"),After.Favor);Evidence->SetNumberField(TEXT("lewd_before"),Before.Lewdness);Evidence->SetNumberField(TEXT("lewd_after"),After.Lewdness);Evidence->SetNumberField(TEXT("stock_after"),Candidate.TopicStock.FindRef(S.UsedTopic));OKStreetWriteStringArray(Evidence,TEXT("voice_ids"),S.CollectedVoiceIds);
 if(S.CollectedEvidence.Num()<32)S.CollectedEvidence.Add(MakeShared<FJsonValueObject>(Evidence));
 {FOKHeroineArgs A;A.RecordOnly=true;const int32 No=S.AskedTopic!=INDEX_NONE?S.AskedTopic:S.UsedTopic;const auto* T=S.CollectedTopics.Find(No);A.Detail=T?T->Name:FString(OKT(TEXT("ui.scene.one_topic")));
  S.Store=MoveTemp(Candidate);Interact(S.Target,TEXT("topic_collected"),A);}
 ++S.CollectedCommits;S.CollectedActive=false;S.AwaitTopicAnswer=false;S.TopicSequence.Reset();S.Node=INDEX_NONE;S.VoiceIds.Reset();S.ActiveCategory.Reset();S.ClipStarted=false;
 S.Message=OKStreetFormatSavedTopicResult(Before.Favor,After.Favor,Before.Lewdness,After.Lewdness)+OKMsg(TEXT("ui.scene.topic_stock_updated"), {{TEXT("desire"), After.SexDesire}});
 ResumeConversationPose(S.Target,Error);ResumeConversationPose(0,Error);RefreshUI();
}

void AOKStreetSocialScene::FailTalk(const FString& Reason){
 if(!State)return;auto& S=*State;
 if(S.SelfTest&&S.ExpectedVoiceAdmissionFault)++S.ExpectedVoiceAdmissionFailures;
 else S.Errors.Add(Reason);
 UE_LOG(LogTemp,Warning,TEXT("StreetSocial conversation aborted%s: %s"),S.ExpectedVoiceAdmissionFault?TEXT(" (intentional admission test)"):TEXT(""),*Reason);
 EndTalk();
 S.TalkFailureNotice=OKT(TEXT("ui.scene.dialog_aborted_prep"));RefreshUI();
}

int32 AOKStreetSocialScene::TouchRay(const FVector& Origin,const FVector& Ray){
 auto& S=*State;
   const FName Bones[]={TEXT("cf_j_head"),TEXT("cf_j_head"),TEXT("cf_j_hand_L"),TEXT("cf_j_hand_R"),TEXT("cf_j_bust02_L"),TEXT("cf_j_bust02_R")};
   int32 HitRegion=INDEX_NONE;float Nearest=MAX_flt;
   for(int32 Region=0;Region<6;++Region){if(OKStreetTouchPart(Region,S.TouchSee)==INDEX_NONE)continue;FVector Point;if(!OKGetCrowdRuntimeBonePosition(this,S.Target,Bones[Region],Point))continue;
    if(Region<2){const FVector Forward=FRotationMatrix(S.RenderedRotations[S.Target]).GetUnitAxis(EAxis::Y);Point+=FVector(0,0,Region==0?13:3)+Forward*(Region==0?0:7);}
    const float Along=FVector::DotProduct(Point-Origin,Ray),Radius=Region<2?6.f:Region<4?7.f:9.f;
    if(Along<=0||Along>=Nearest||FVector::DistSquared(Origin+Ray*Along,Point)>Radius*Radius)continue;
    FHitResult Obstacle;FCollisionQueryParams Query(SCENE_QUERY_STAT(StreetTouch),false,this);if(GetWorld()->LineTraceSingleByChannel(Obstacle,Origin,Point,ECC_Visibility,Query))continue;
    HitRegion=Region;Nearest=Along;
   }
   if(HitRegion!=INDEX_NONE)Touch(HitRegion);return HitRegion;
}

void AOKStreetSocialScene::Touch(int32 Region){
 if(!State)return;auto& S=*State;if(S.Target==INDEX_NONE||S.TouchActive)return;
 const auto Relation=OKStreetViewRelation(S.Store,OKStreetStableId(S.Manifest.Characters[S.Target]),S.Manifest.TalkBudget);
 const auto* Profile=S.CollectedTopics.Profile(S.Manifest.Characters[S.Target].PersonalityId);const int32 Part=OKStreetTouchPart(Region,S.TouchSee);
 if(!Profile||Part==INDEX_NONE||(GOKStreetTalkLimit&&Relation.TalksRemaining<=0)){S.TouchNotice=OKT(TEXT("ui.scene.interaction_unavailable"));RefreshUI();return;}
 const int32 Key=Part+(S.TouchSee?10:0),Count=OKStreetNextTouchCount(S.TouchCounts.FindRef(Key)),Repeat=OKStreetTouchRepeat(Count),Stage=Relation.Stage==TEXT("lover")?2:Relation.Stage==TEXT("friend")?1:0;
 const auto* Reaction=OKStreetFindTouch(*Profile,Stage,Part,S.TouchSee,Repeat);
 if(Reaction&&!Reaction->Available){S.TouchCounts.Add(Key,Count);S.TouchNotice=OKT(TEXT("ui.scene.reaction_no_source_effect"));RefreshUI();return;}
 if(!Reaction||Reaction->Parts.Num()!=1||Reaction->Parts[0].Node==INDEX_NONE){S.TouchNotice=OKT(TEXT("ui.scene.reaction_no_source_voice"));RefreshUI();return;}
 S.TouchNotice.Reset();
 // Invalidate the old action/audio epochs before replacing the transaction.
 // A reserved collectible remains in stock when its reply is interrupted.
 FString Error;OKStopCrowdRuntimeSpeech(this,S.Target,Error);ResumeConversationPose(S.Target,Error);ResumeConversationPose(0,Error);OKResetCrowdRuntimeFaceExpression(this,S.Target,Error);
 S.CollectedActive=false;S.AwaitTopicAnswer=false;S.TopicIntroduction=false;S.TopicSequence.Reset();S.CollectedVoiceIds.Reset();S.SilentTopicUntil=0;S.UsedTopic=INDEX_NONE;S.AskedTopic=INDEX_NONE;
 S.RelationshipSequence.Reset();S.RelationshipSilentUntil=0;S.RelationshipPending=false;S.RelationshipVoiceRequired=false;S.RelationshipVoiceCompleted=false;S.BranchLine=false;S.PendingEffect={};S.Node=INDEX_NONE;S.VoiceIds.Reset();S.ClipStarted=false;
 S.TouchRegion=Region;S.TouchStage=Stage;S.TouchRepeat=Repeat;S.TouchEffect=Reaction->Effect;
 if(!QueueNode(Reaction->Parts[0].Node,TEXT("touch")))return;
 // Reuse authored regional body curves, never translate the actor or reshape the card.
 if(!S.TouchSee){const TCHAR* Clips[]={TEXT("touch_head.okanim"),TEXT("touch_cheek.okanim"),TEXT("touch_hand.okanim"),TEXT("touch_hand.okanim"),TEXT("touch_chest.okanim"),TEXT("touch_chest.okanim")};S.ActiveGesture=Clips[Region];}
 S.TouchCounts.Add(Key,Count);S.TouchActive=true;S.PlayerLine.Reset();RefreshUI();
}

void AOKStreetSocialScene::CompleteTouch(){
 auto& S=*State;if(!S.TouchActive||S.Target==INDEX_NONE)return;
 const FString Id=OKStreetStableId(S.Manifest.Characters[S.Target]),Transaction=FGuid::NewGuid().ToString(EGuidFormats::Digits);
 const auto Before=OKStreetViewRelation(S.Store,Id,S.Manifest.TalkBudget);auto Candidate=S.Store;
 const auto Result=OKRulesTalkCommit(S,Candidate,Id,Transaction,S.TouchEffect);FString Error;
 if(!Result.Applied||!OKStreetAtomicSave(S.Manifest.SavePath,OKStreetWriteSave(Candidate),Error)){FailTalk(TEXT("Touch atomic commit failed: ")+Result.Reason+Error);return;}
 const auto After=OKStreetViewRelation(Candidate,Id,S.Manifest.TalkBudget);auto Record=MakeShared<FJsonObject>();Record->SetStringField(TEXT("id"),Id);Record->SetStringField(TEXT("transaction"),Transaction);Record->SetNumberField(TEXT("personality"),S.Manifest.Characters[S.Target].PersonalityId);Record->SetNumberField(TEXT("region"),S.TouchRegion);Record->SetNumberField(TEXT("stage"),S.TouchStage);Record->SetNumberField(TEXT("repeat"),S.TouchRepeat);Record->SetBoolField(TEXT("see"),S.TouchSee);Record->SetStringField(TEXT("reaction_clip"),S.ActiveGesture);Record->SetNumberField(TEXT("favor_before"),Before.Favor);Record->SetNumberField(TEXT("favor_after"),After.Favor);Record->SetNumberField(TEXT("lewd_before"),Before.Lewdness);Record->SetNumberField(TEXT("lewd_after"),After.Lewdness);OKStreetWriteStringArray(Record,TEXT("voice_ids"),S.VoiceIds);if(S.TouchEvidence.Num()<32)S.TouchEvidence.Add(MakeShared<FJsonValueObject>(Record));
 S.Store=MoveTemp(Candidate);++S.TouchCommits;
 {static const TCHAR* Parts[]={TEXT("頭"),TEXT("臉頰"),TEXT("左手"),TEXT("右手"),TEXT("左胸"),TEXT("右胸")};FOKHeroineArgs A;A.RecordOnly=true;
  const TCHAR* Part=OKT(Parts[FMath::Clamp(S.TouchRegion,0,5)]);A.Detail=S.TouchSee?OKMsg(TEXT("ui.scene.staring_at_part"), {{TEXT("part"), Part}}):OKMsg(TEXT("ui.scene.touched_your_part"), {{TEXT("part"), Part}});Interact(S.Target,TEXT("touch"),A);}S.TouchActive=false;S.Node=INDEX_NONE;S.VoiceIds.Reset();S.ClipStarted=false;S.ActiveCategory.Reset();S.PlayerLine.Reset();
 ResumeConversationPose(S.Target,Error);ResumeConversationPose(0,Error);S.Message=OKStreetFormatSavedTopicResult(Before.Favor,After.Favor,Before.Lewdness,After.Lewdness);RefreshUI();
}

void AOKStreetSocialScene::EndTalk(){
 if(!State)return;auto& S=*State;
 if(S.PrivateGirl!=INDEX_NONE){FinishPrivate();return;}
 S.PendingTopic.Reset(); // a topic queued behind her answer is for this talk only
 if(S.Pair.Conversation()&&S.Pair.Partner!=INDEX_NONE){FinishPhoto(false);return;} // ending the talk ends the meal too, not the other way round
 S.InvitationActive=false;S.InvitationSequence.Reset();S.InvitationQueued=INDEX_NONE;S.InvitationGirl=INDEX_NONE;S.InvitationCommand=INDEX_NONE;
 const bool Ending=S.Target!=INDEX_NONE;
 // She spoke to him and he went off without one word back (any talk: she came over at their date, or he pressed E and
 // walked away from her answer): hers to remember. A talk of under a minute left no memory at all, and 三月七, met at
 // the park for their walk, greeted him a quarter hour later as if by chance (R6-15). How she takes it is the AI's.
 // Not when the game is closing with her talk open.
 if(Ending&&S.TalkStartMinute>=0&&S.Manifest.Characters.IsValidIndex(S.Target)&&!(GetWorld()&&GetWorld()->bIsTearingDown)){bool Hers=false,His=false;const auto& List=HistoryOf(OKStreetStableId(S.Manifest.Characters[S.Target]));
  for(int32 K=List.Num()-1;K>=0;--K){const auto& E=List[K];if(E.Minute<S.TalkStartMinute)break;
   if(E.Kind==TEXT("talk")){Hers|=E.Who==TEXT("her");His|=E.Who==TEXT("him");}}
  // Her first words still on their way (she came over and he went before her line arrived: 三月七 asking about the book
  // he gave Hu Tao) count the same.
  if((Hers||S.FreeTalkBusy)&&!His){const FString Text=OKT(TEXT("ui.memory.left_without_word"));HistoryAdd(S.Target,TEXT("talk"),TEXT("world"),Text);MindRemember(S.Target,TEXT("talk"),Text,PlaceHere(S.Male),0.4f);
   UE_LOG(LogTemp,Display,TEXT("Street talk: actor=%d he left without a word to what she said"),S.Target);}}
 // Any talk he ends counts as parting, as her own goodbye does (TickApproach waits 3 minutes): 三月七 said goodnight at
 // her door, he closed the talk, and she walked straight back up to him to start another (R6-22).
 if(Ending)S.PartedAt.Add(S.Target,FPlatformTime::Seconds());
 if(Ending&&S.TalkStartMinute>=0&&OKStreetMindNow(S)>S.TalkStartMinute){const int64 Now=OKStreetMindNow(S);const int64 From=S.TalkStartMinute;S.TalkStartMinute=-1; // a talk of under a minute (re-opened at once) is not a memory
  MindRemember(S.Target,TEXT("talk"),OKMsg(TEXT("ui.memory.talk_session"),{{TEXT("place"),([&](const FString& Id){const FOKStreetPlace* P=OKStreetFindPlace(Id);return P?FString(P->Name):Id.IsEmpty()?FString(OKT(TEXT("ui.etiquette.street_location"))):Id;})(PlaceHere(S.Male))},{TEXT("minutes"),int32(FMath::Max<int64>(1,Now-From))},
   {TEXT("start"),FString::Printf(TEXT("%02d:%02d"),int32(From%1440/60),int32(From%60))},{TEXT("end"),FString::Printf(TEXT("%02d:%02d"),int32(Now%1440/60),int32(Now%60))}}),PlaceHere(S.Male),0.2f);}
 S.RelationshipSequence.Reset();S.RelationshipPart=0;S.RelationshipSilentUntil=0;S.TopicsOpen=false;S.TouchNotice.Reset();S.TouchActive=false;S.TouchCounts.Reset();S.TouchRegion=INDEX_NONE;S.TopicCommandCounts.Reset();
 if(Ending){FString Error;OKStopCrowdRuntimeSpeech(this,S.Target,Error);OKResetCrowdRuntimeFaceExpression(this,S.Target,Error);ResumeBehavior(S.Target,Error);ResumeBehavior(0,Error);if(S.Agents.IsValidIndex(S.Target-1))S.Agents[S.Target-1].Held=false;S.CandidateExplicit=false;S.Candidate=INDEX_NONE;S.CandidateCount=0;}
 S.SilentTopicUntil=0;S.CollectedVoiceIds.Reset();S.CollectedActive=false;S.AwaitTopicAnswer=false;S.TopicIntroduction=false;S.TopicSequence.Reset();S.UsedTopic=INDEX_NONE;S.AskedTopic=INDEX_NONE;S.PlayerLine.Reset();S.Target=INDEX_NONE;S.Node=INDEX_NONE;S.VoiceIds.Reset();S.ClipStarted=false;S.ActiveCategory.Reset();S.RelationshipPending=false;S.RelationshipVoiceRequired=false;S.RelationshipVoiceCompleted=false;S.BranchLine=false;S.PendingEffect=FOKStreetEffect{};
 S.Message=OKT(TEXT("ui.scene.controls_full_hint"));RefreshUI();
}

void AOKStreetSocialScene::ApplyMouseMode(bool Focused){
 if(!State||!GEngine||!GEngine->GameViewport)return;auto& S=*State;
 auto* PC=GetWorld()->GetFirstPlayerController();if(!PC)return;
 // The mouse is only taken (captured, locked, centred) while the game window is the foreground window: launched behind
 // another window, alt-tabbed away, or driven through the control channel from elsewhere, it stays free.
 const bool Foreground=S.SelfTest||S.RetargetSelfTest||(GEngine->GameViewport->Viewport&&GEngine->GameViewport->Viewport->IsForegroundWindow());
 // Played through the control channel (-Control: an AI or test driver sends the input), the mouse is never taken: the
 // person at the machine keeps using it while the game runs in front.
 const bool Show=S.MouseUI||!Focused||!Foreground||OKVirtualInput().Active;
 PC->bShowMouseCursor=Show;PC->bEnableClickEvents=Show;PC->bEnableMouseOverEvents=Show;
 auto* Viewport=GEngine->GameViewport.Get();
 if(Show){FInputModeGameAndUI Input;Input.SetHideCursorDuringCapture(false);Input.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);PC->SetInputMode(Input);Viewport->SetMouseCaptureMode(EMouseCaptureMode::NoCapture);Viewport->SetMouseLockMode(EMouseLockMode::DoNotLock);}
 else{PC->SetInputMode(FInputModeGameOnly());Viewport->SetMouseCaptureMode(EMouseCaptureMode::CapturePermanently_IncludingInitialMouseDown);Viewport->SetMouseLockMode(EMouseLockMode::LockAlways);int32 X=0,Y=0;PC->GetViewportSize(X,Y);if(X>0&&Y>0)PC->SetMouseLocation(X/2,Y/2);}
 // SetInputMode changes Slate focus; discard its accumulated delta.
 float X=0,Y=0;PC->GetInputMouseDelta(X,Y);
}

void AOKStreetSocialScene::SetCharacterStat(int32 Character,int32 Field,float Value){
 if(!State||Character<=0||!State->Manifest.Characters.IsValidIndex(Character)||!OKHeroineStats().IsValidIndex(Field)||!FMath::IsFinite(Value))return;auto& S=*State;
 // Start with the latest relation and change only this field, preserving voice effects and other updates.
 auto Candidate=S.Store;const FString Id=OKStreetStableId(S.Manifest.Characters[Character]);
 auto& R=Candidate.Relations[OKStreetRelationIndex(Candidate,Id,S.Manifest.TalkBudget,true)];
 const FOKHeroineStat& Stat=OKHeroineStats()[Field];float Max=Stat.Max;
 if(Field==0){const int32 Limit=OKRulesFavorLimit(S,R,R.Stage);if(Limit!=INDEX_NONE)Max=FMath::Min(Max,float(Limit));} // favor stays under her stage's cap
 Stat.Set(R,FMath::Clamp(Value,0.f,Max));
 // Written by the save task (serializing the whole store here held every reply's frame 5 ms); a failed write is logged there.
 S.Store=MoveTemp(Candidate);if(!S.Manifest.SavePath.IsEmpty())OKStreetAtomicSaveStoreAsync(S.Manifest.SavePath,S.Store);
 // Keep the captured Slate slider alive; all value attributes read directly from Store.
}

void AOKStreetSocialScene::SetCharacterRelationship(int32 Character,const FString& Stage){
 if(!State||Character<=0||!State->Manifest.Characters.IsValidIndex(Character)||(Stage!=TEXT("stranger")&&Stage!=TEXT("friend")&&Stage!=TEXT("lover")))return;auto& S=*State;
 auto Candidate=S.Store;const FString Id=OKStreetStableId(S.Manifest.Characters[Character]);
 auto& R=Candidate.Relations[OKStreetRelationIndex(Candidate,Id,S.Manifest.TalkBudget,true)];
 if(R.Stage==Stage)return;R.Stage=Stage;FString Error;
 if(!OKStreetAtomicSave(S.Manifest.SavePath,OKStreetWriteSave(Candidate),Error)){S.Errors.Add(TEXT("Relationship selection save failed: ")+Error);S.ContactsNotice=OKT(TEXT("ui.scene.relation_save_failed"))+Error;return;}
 S.Store=MoveTemp(Candidate);
 if(S.Target==Character){
  // Cancel old-stage speech and uncommitted effects while retaining the current pair/private scene.
  OKStopCrowdRuntimeSpeech(this,Character,Error);OKResetCrowdRuntimeFaceExpression(this,Character,Error);ResumeConversationPose(Character,Error);ResumeConversationPose(0,Error);
  S.Node=INDEX_NONE;S.VoiceIds.Reset();S.ClipStarted=false;S.ActiveCategory.Reset();S.PlayerLine.Reset();
  S.RelationshipSequence.Reset();S.RelationshipPart=0;S.RelationshipSilentUntil=0;S.RelationshipPending=false;S.RelationshipVoiceRequired=false;S.RelationshipVoiceCompleted=false;S.BranchLine=false;S.PendingEffect={};
  S.CollectedActive=false;S.AwaitTopicAnswer=false;S.TopicIntroduction=false;S.TopicSequence.Reset();S.CollectedVoiceIds.Reset();S.SilentTopicUntil=0;S.UsedTopic=INDEX_NONE;S.AskedTopic=INDEX_NONE;
  S.TouchActive=false;S.TouchRegion=INDEX_NONE;S.TouchNotice.Reset();S.TouchCounts.Reset();S.TopicCommandCounts.Reset();
  S.InvitationActive=false;S.InvitationSequence.Reset();S.InvitationQueued=INDEX_NONE;S.InvitationGirl=INDEX_NONE;S.InvitationCommand=INDEX_NONE;
  S.Message=OKT(TEXT("ui.scene.relation_updated"));
 }
 RefreshUI();
}

static const FSlateBrush* OKStreetGlassPanel(){static const FSlateRoundedBoxBrush Brush(FLinearColor(.025f,.045f,.075f,.78f),16.f);return &Brush;}
static const FButtonStyle* OKStreetGlassButton(){static const FButtonStyle Style=[](){auto B=FCoreStyle::Get().GetWidgetStyle<FButtonStyle>("Button");B.SetNormal(FSlateRoundedBoxBrush(FLinearColor(.12f,.19f,.29f,.68f),8.f));B.SetHovered(FSlateRoundedBoxBrush(FLinearColor(.24f,.40f,.53f,.85f),8.f));B.SetPressed(FSlateRoundedBoxBrush(FLinearColor(.07f,.14f,.23f,.85f),8.f));B.SetNormalPadding(FMargin(12,8));B.SetPressedPadding(FMargin(12,8));return B;}();return &Style;}

// The bottom box's text from S.Message: her words (kept while only a control hint comes in) and the control hint. Run every
// tick, and the box binds to the result, so a new message shows without rebuilding the whole UI (a rebuild costs ~10 ms:
// walking past people changed the hint every few seconds, and each change was a long frame).
static void OKStreetDigestMessage(FOKStreetSceneState& S){
 if(S.Message==S.DigestedMessage&&S.Target==S.DigestedTarget&&S.Node==S.DigestedNode)return;
 S.DigestedMessage=S.Message;S.DigestedTarget=S.Target;S.DigestedNode=S.Node;
 const FString Speaker=S.Node!=INDEX_NONE&&S.Manifest.Characters.IsValidIndex(S.Target)?OKStreetScriptedName(S.Manifest.Characters[S.Target].Name)+OKT(TEXT("ui.economy.colon_mark")):FString();
 FString Said,Hint;OKStreetSplitMessageHint(S.Message,Said,Hint);
 // A control-only message keeps her last line on screen; a new partner (or none) starts clean.
 if(S.DialogueTarget!=S.Target){S.DialogueText.Reset();S.DialogueTarget=S.Target;}
 if(!Said.IsEmpty())S.DialogueText=Speaker+Said;
 S.DialogueColor=OKStreetSpeakerColor(OKStreetLineSpeaker(S.DialogueText,OKStreetCharacterNames(S)));
 if(!Hint.IsEmpty()&&Hint!=S.HintText){S.HintText=Hint;S.HintUnread=!S.HintOpen;}
}

static TAutoConsoleVariable<int32> CVarOKStreetLogUiRebuilds(TEXT("ok.Street.LogUiRebuilds"),0,TEXT("1: log each rebuild of the right panel with the RefreshUI call that asked for it (file:line). A rebuild costs ~10 ms."));
void AOKStreetSocialScene::RefreshUI(uint32 Regions,std::source_location Where){if(State){State->UiDirty=true;State->UiDirtyRegions|=Regions;State->UiDirtyFile=Where.file_name();State->UiDirtyLine=Where.line();}}

void AOKStreetSocialScene::RebuildUI(uint32 Regions){
 if(!State||!GEngine||!GEngine->GameViewport)return;auto& S=*State;
 {// Diagnostics: a rebuild drops a button press in progress, so frequent rebuilds make buttons unclickable.
  static double Window=0;static int32 Count=0;const double T=FPlatformTime::Seconds();if(T-Window>1){if(Count>3)UE_LOG(LogTemp,Warning,TEXT("Street UI rebuilt %d times in 1 s"),Count);Window=T;Count=0;}++Count;}
 // Where a slow rebuild spends its time, by section (logged with ok.Street.LogUiRebuilds 1).
 TArray<TPair<const TCHAR*,uint64>,TInlineAllocator<24>> Marks;auto Mark=[&Marks](const TCHAR* Name){Marks.Emplace(Name,FPlatformTime::Cycles64());};
 Mark(TEXT("shell"));
 // The shell, built once (again on a language change): the layout, minimap and big map, clock, bed and elevator rows, the
 // hint corner, and a holder for each region. A refresh rebuilds only the regions it names (OKStreetUi) into their holders;
 // rebuilding the whole overlay every time cost 9-20 ms a refresh (2026-10-25: 31 rebuilds in two minutes of walking).
 if(!S.Overlay.IsValid()||(Regions&OKStreetUi::Shell)){
  Regions|=OKStreetUi::All;
  if(S.Overlay.IsValid())GEngine->GameViewport->RemoveViewportWidgetContent(S.Overlay.ToSharedRef());
  SAssignNew(S.Minimap,SOKStreetMinimap).Visibility(EVisibility::HitTestInvisible);
  auto MapData=[Weak=TWeakPtr<FOKStreetSceneState>(State),Owner=TWeakObjectPtr<AOKStreetSocialScene>(this)](int32 KeepPlaces){FOKStreetMinimapData D;if(auto Live=Weak.Pin()){if(!Owner.IsValid())return D;D.Player=Live->Male;D.ShowNames=Live->MinimapShowNames;for(const auto& Place:OKStreetPlaces())D.Places.Add(TPair<FString,FVector>(Place.Name,Place.Door));D.Navigation=Live->NavPath;D.FacingYaw=Live->Rotations.IsValidIndex(0)?Live->Rotations[0].Yaw:0;
   for(const auto& A:Live->Agents){FOKRuntimeCharacterStatus Status;if(!Live->Manifest.Characters.IsValidIndex(A.Slot)||!OKGetCrowdRuntimeCharacterStatus(Owner.Get(),A.Slot,Status)||!Status.Visible||!Status.Error.IsEmpty())continue;FOKStreetMinimapMarker M;M.Slot=A.Slot;M.Name=OKStreetScriptedName(Live->Manifest.Characters[A.Slot].Name);M.Position=A.Position;M.Selected=Live->Target==A.Slot||Live->NavGirl==A.Slot;const auto Rel=OKStreetViewRelation(Live->Store,OKStreetStableId(Live->Manifest.Characters[A.Slot]),Live->Manifest.TalkBudget);M.Favor=Rel.Favor;M.Stage=OKStreetStageIndex(Rel.Stage);const int32 Limit=OKRulesFavorLimit(*Live,Rel,Rel.Stage);M.FavorFull=Limit!=INDEX_NONE&&Rel.Favor>=Limit;D.Girls.Add(M);}
   TArray<FOKStreetPointData> Points;TArray<TArray<int32>> Routes;OKStreetWaypoints(Points);OKStreetRoutes(Routes);for(const auto& Route:Routes){TArray<FVector> Path;for(int32 I:Route)if(Points.IsValidIndex(I))Path.Add(Points[I].Position);if(!Path.IsEmpty()){const FVector First=Path[0];Path.Add(First);}D.Paths.Add(MoveTemp(Path));}
  OKStreetNearestPlaces(D.Places,D.Player,KeepPlaces);}return D;};
  S.Minimap->Read=[MapData](){return MapData(10);};
  SAssignNew(S.BigMap,SOKStreetBigMap);S.BigMap->Read=[MapData](){return MapData(MAX_int32);};
  S.BigMap->Pick=[Weak=TWeakObjectPtr<AOKStreetSocialScene>(this)](const FVector& Goal,const FString& Name){if(Weak.IsValid())Weak->NavigatePoint(Goal,Name);};
  S.MinimapPanel=SNew(SVerticalBox)+SVerticalBox::Slot().AutoHeight()[S.Minimap.ToSharedRef()]+SVerticalBox::Slot().AutoHeight()[SNew(SBorder).BorderBackgroundColor(FLinearColor(.018f,.03f,.055f,.93f))[SNew(SCheckBox).IsChecked_Lambda([Weak=TWeakPtr<FOKStreetSceneState>(State)](){auto Live=Weak.Pin();return Live&&Live->MinimapShowNames?ECheckBoxState::Checked:ECheckBoxState::Unchecked;}).OnCheckStateChanged_Lambda([Weak=TWeakPtr<FOKStreetSceneState>(State)](ECheckBoxState Value){if(auto Live=Weak.Pin())Live->MinimapShowNames=Value==ECheckBoxState::Checked;})[SNew(STextBlock).Text(FText::FromString(OKT(TEXT("ui.scene.show_names"))))]]]
   +SVerticalBox::Slot().AutoHeight()[SNew(SBorder).BorderBackgroundColor(FLinearColor(.018f,.03f,.055f,.93f)).Padding(6,2)[SNew(STextBlock).Text_Lambda([Weak=TWeakPtr<FOKStreetSceneState>(State)](){auto Live=Weak.Pin();if(!Live)return FText::GetEmpty();const double H=OKStreetClockHours(Live->ClockStartHours,FPlatformTime::Seconds()-Live->ClockStartWall);return FText::FromString(OKT(TEXT("ui.scene.block_prefix"))+OKStreetCalendarLabel(Live->Store.CalendarDay,H));})]]
   // Plan §13.3.2: appointments with countdowns (click: walk to the soonest one), and blacklisted shops.
   +SVerticalBox::Slot().AutoHeight()[SNew(SButton).ButtonStyle(OKStreetGlassButton()).OnClicked_Lambda([Owner=TWeakObjectPtr<AOKStreetSocialScene>(this)](){if(Owner.IsValid())Owner->NavigateToNextAppointment();return FReply::Handled();})
    .Visibility_Lambda([Owner=TWeakObjectPtr<AOKStreetSocialScene>(this)](){return Owner.IsValid()&&!Owner->TaskListText().IsEmpty()?EVisibility::Visible:EVisibility::Collapsed;})
    [SNew(STextBlock).AutoWrapText(true).WrapTextAt(300).ColorAndOpacity_Lambda([Owner=TWeakObjectPtr<AOKStreetSocialScene>(this)](){return Owner.IsValid()?Owner->TaskListColor():FSlateColor(FLinearColor::White);}).Text_Lambda([Owner=TWeakObjectPtr<AOKStreetSocialScene>(this)](){return FText::FromString(Owner.IsValid()?Owner->TaskListText():FString());})]];
  // Street clock on the right panel: jump the time of day (the sun follows within a second) or pause it.
  TWeakPtr<FOKStreetSceneState> ClockState=State;
  auto ClockNow=[ClockState](){auto L=ClockState.Pin();return L?OKStreetClockHours(L->ClockStartHours,FPlatformTime::Seconds()-L->ClockStartWall):12.0;};
  auto SetClock=[ClockState](double Hours){if(auto L=ClockState.Pin()){L->ClockStartHours=FMath::Fmod(Hours+48.0,24.0);L->ClockStartWall=FPlatformTime::Seconds();L->NextSunUpdate=0;L->CalendarLastHours=-1;L->ClockDrivesSun=L->Sun.IsValid();}};
  // An hour on or back is time going by: across 04:00 forward a new game day starts (as when the clock runs there), back
  // the game day is the one before. Only the clock was set, so 05:12 back to 03:23 showed the next date and the next
  // 04:00 began another day: two days on for an hour back (R3-225).
  auto StepClock=[ClockState,SetClock,ClockNow,Owner=TWeakObjectPtr<AOKStreetSocialScene>(this)](double Delta){
   auto L=ClockState.Pin();if(!L)return;const double Was=ClockNow(),To=FMath::Fmod(Was+Delta+48.0,24.0);SetClock(Was+Delta);
   // (Back over a day already begun and on again: NextDay only gives the date back.)
   if(Delta>0&&OKStreetDayStartsPassed(Was,To)>0){if(Owner.IsValid())Owner->NextDay();}
   else if(Delta<0&&OKStreetDayStartsPassed(To,Was)>0&&L->Store.CalendarDay>0){if(L->DayRewound<0)L->DayRewound=L->Store.CalendarDay;--L->Store.CalendarDay;}};
  auto ClockButton=[](const TCHAR* Label,TFunction<void()> Do)->TSharedRef<SWidget>{return SNew(SButton).ButtonStyle(OKStreetGlassButton()).HAlign(HAlign_Center).Text(FText::FromString(Label)).OnClicked_Lambda([Do](){Do();return FReply::Handled();});};
  TSharedRef<SWidget> ClockRow=SNew(SVerticalBox)
   +SVerticalBox::Slot().AutoHeight().Padding(2)[SNew(SHorizontalBox)
    +SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center).Padding(4,0)[SNew(STextBlock).ColorAndOpacity(FLinearColor::White).Text_Lambda([ClockState,ClockNow](){auto L=ClockState.Pin();if(!L)return FText::GetEmpty();const int32 M=FMath::FloorToInt32(ClockNow()*60);return FText::FromString(OKMsg(TEXT("ui.scene.clock_display"), {{TEXT("hour"), M/60}, {TEXT("minute"), M%60}, {TEXT("paused_suffix"), L->ClockPaused?OKT(TEXT("ui.scene.paused_label")):TEXT("")}}));})]
    +SHorizontalBox::Slot().AutoWidth().Padding(2,0)[ClockButton(OKT(TEXT("ui.scene.minus_one_hour")),[StepClock](){StepClock(-1);})]
    +SHorizontalBox::Slot().AutoWidth().Padding(2,0)[ClockButton(OKT(TEXT("ui.scene.plus_one_hour")),[StepClock](){StepClock(1);})]
    +SHorizontalBox::Slot().AutoWidth().Padding(2,0)[SNew(SButton).ButtonStyle(OKStreetGlassButton()).HAlign(HAlign_Center)
     .Text_Lambda([ClockState](){auto L=ClockState.Pin();return FText::FromString(L&&L->ClockPaused?OKT(TEXT("ui.scene.continue_button")):OKT(TEXT("ui.scene.pause_button")));})
     .OnClicked_Lambda([ClockState,SetClock,ClockNow](){if(auto L=ClockState.Pin()){const double Hours=ClockNow();L->ClockPaused=!L->ClockPaused;SetClock(Hours);}return FReply::Handled();})]]
   +SVerticalBox::Slot().AutoHeight().Padding(2)[SNew(SHorizontalBox)
    +SHorizontalBox::Slot().FillWidth(1).Padding(2,0)[ClockButton(OKT(TEXT("ui.scene.morning_label")),[SetClock](){SetClock(8.0);})]
    +SHorizontalBox::Slot().FillWidth(1).Padding(2,0)[ClockButton(OKT(TEXT("ui.free_talk.noon_period")),[SetClock](){SetClock(12.5);})]
    +SHorizontalBox::Slot().FillWidth(1).Padding(2,0)[ClockButton(OKT(TEXT("ui.free_talk.evening_dusk_period")),[SetClock](){SetClock(17.5);})]
    +SHorizontalBox::Slot().FillWidth(1).Padding(2,0)[ClockButton(OKT(TEXT("ui.free_talk.night_period")),[SetClock](){SetClock(21.0);})]];
  // Bed: a "sleep here" button while the player stands by one.
  TSharedRef<SWidget> SleepRow=SNew(SBox).Visibility_Lambda([ClockState](){auto L=ClockState.Pin();return L&&L->SleepPhase==0&&OKStreetNearestBed(L->Male,260,nullptr)?EVisibility::Visible:EVisibility::Collapsed;})
   [SNew(SButton).ButtonStyle(OKStreetGlassButton()).HAlign(HAlign_Center).Text(FText::FromString(OKT(TEXT("ui.scene.sleep_in_bed"))))
    .OnClicked_Lambda([Weak=TWeakObjectPtr<AOKStreetSocialScene>(this)](){if(Weak.IsValid()&&Weak->State){const FOKStreetPlace* Where=nullptr;if(const auto* Bed=OKStreetNearestBed(Weak->State->Male,260,nullptr,&Where))Weak->StartSleep(*Bed,Where,Weak->FreeTalkPartner());}return FReply::Handled();})];
  // Elevator panel: shown while the player is inside a multi-storey place, one button per floor.
  TSharedRef<SVerticalBox> LiftRows=SNew(SVerticalBox);
  for(int32 PlaceIndex=0;PlaceIndex<OKStreetPlaces().Num();++PlaceIndex){
   const auto Lifts=OKStreetPlaceLifts(OKStreetPlaces()[PlaceIndex]);if(Lifts.Num()<2)continue;
   TWeakObjectPtr<AOKStreetSocialScene> Weak(this);
   auto Lift=[Weak,PlaceIndex](){return Weak.IsValid()?Weak->ElevatorOfPlace(PlaceIndex):INDEX_NONE;};
   TSharedRef<SWrapBox> Wrap=SNew(SWrapBox).UseAllottedSize(true);
   for(int32 Floor=1;Floor<=Lifts.Num();++Floor){
    Wrap->AddSlot().Padding(2)[SNew(SButton).ButtonStyle(OKStreetGlassButton()).Text(FText::FromString(OKStreetLiftLabel(*Lifts[Floor-1])))
     .OnClicked_Lambda([Weak,PlaceIndex,Floor](){if(Weak.IsValid())Weak->RideElevator(PlaceIndex,Floor);return FReply::Handled();})];
   }
   Wrap->AddSlot().Padding(2)[SNew(SButton).ButtonStyle(OKStreetGlassButton()).Text(FText::FromString(OKT(TEXT("ui.scene.toggle_on"))))
    .OnClicked_Lambda([Weak,Lift](){if(Weak.IsValid())Weak->ElevatorDoorButton(Lift(),true);return FReply::Handled();})];
   Wrap->AddSlot().Padding(2)[SNew(SButton).ButtonStyle(OKStreetGlassButton()).Text(FText::FromString(OKT(TEXT("ui.scene.toggle_off"))))
    .OnClicked_Lambda([Weak,Lift](){if(Weak.IsValid())Weak->ElevatorDoorButton(Lift(),false);return FReply::Handled();})];
   auto Inside=[Weak,Lift](){const int32 I=Lift();return Weak.IsValid()&&I!=INDEX_NONE&&Weak->ElevatorWithMale()==I;};
   auto Near=[ClockState,PlaceIndex](){auto L=ClockState.Pin();return L&&OKStreetPlaceFloor(OKStreetPlaces()[PlaceIndex],L->Male)>0;};
   LiftRows->AddSlot().AutoHeight()[SNew(SVerticalBox).Visibility_Lambda([Near](){return Near()?EVisibility::Visible:EVisibility::Collapsed;})
    +SVerticalBox::Slot().AutoHeight().Padding(4,2)[SNew(STextBlock).ColorAndOpacity(FLinearColor::White).AutoWrapText(true).Text_Lambda([ClockState,PlaceIndex,Lift,Inside](){
      auto L=ClockState.Pin();const int32 I=Lift();const auto& Place=OKStreetPlaces()[PlaceIndex];
      if(!L||!L->Elevators.IsValidIndex(I))return FText::GetEmpty();const auto& E=L->Elevators[I];
      const FString Where=E.Phase==4?FString::Printf(TEXT("%s → %s"),*E.Labels[E.Floor],*E.Labels[E.Target]):E.Labels[E.Floor];
      return FText::FromString(Inside()?OKMsg(TEXT("ui.scene.elevator_inside"), {{TEXT("place_name"), Place.Name}, {TEXT("where"), *Where}}):OKMsg(TEXT("ui.scene.elevator_at"), {{TEXT("place_name"), Place.Name}, {TEXT("where"), *Where}}));})]
    +SVerticalBox::Slot().AutoHeight()[SNew(SBox).Visibility_Lambda([Inside](){return Inside()?EVisibility::Visible:EVisibility::Collapsed;})[Wrap]]];
  }
  S.Overlay=SNew(SOverlay)+SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Top).Padding(16)[S.MinimapPanel.ToSharedRef()]
  +SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Bottom).Padding(356,16,0,24)[SNew(SBox).MaxDesiredWidth(1140).MinDesiredWidth(1140)[SNew(SVerticalBox)
   // His cards at the card table, held in front of him right above the dialogue box, which grows upward under them (OKStreetSocialBluff.inl).
   +SVerticalBox::Slot().AutoHeight().Padding(0,0,0,6)[SAssignNew(S.UiBluffHolder,SBox).Visibility(EVisibility::SelfHitTestInvisible)]
   // Gal-game layout: the log button sits on the box's top-right edge; the box itself never hides.
   // Beside it, once she has sung for him and while he is still with her: hear that song again (OKStreetSocialSong.inl).
   +SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right).Padding(0,0,8,4)[SNew(SHorizontalBox)
    +SHorizontalBox::Slot().AutoWidth().Padding(0,0,6,0)[SNew(SButton).ButtonStyle(OKStreetGlassButton()).Text(FText::FromString(OKT(TEXT("ui.song.listen_again"))))
     .Visibility_Lambda([Weak=TWeakPtr<FOKStreetSceneState>(State)](){const auto L=Weak.Pin();if(!L)return EVisibility::Collapsed;const auto& G=L->Song;
      const auto P=G.Phase;const bool Singing=P==EOKSongPhase::Intro||P==EOKSongPhase::Singing||P==EOKSongPhase::Outro||P==EOKSongPhase::Offered;
      return !Singing&&G.JustSungGirl!=INDEX_NONE&&L->Target==G.JustSungGirl&&G.Memories.IsValidIndex(G.JustSung)?EVisibility::Visible:EVisibility::Collapsed;})
     .OnClicked_Lambda([Weak=TWeakObjectPtr<AOKStreetSocialScene>(this)](){if(Weak.IsValid())Weak->SongReplay(Weak->State->Song.JustSung);return FReply::Handled();})]
    +SHorizontalBox::Slot().AutoWidth()[SNew(SButton).ButtonStyle(OKStreetGlassButton()).Text_Lambda([Weak=TWeakPtr<FOKStreetSceneState>(State)](){const auto L=Weak.Pin();if(!L)return FText::GetEmpty();return FText::FromString(OKMsg(TEXT("ui.scene.history_toggle_count"), {{TEXT("toggle_label"), L->FreeTalkHistoryOpen?OKT(TEXT("ui.scene.close_log")):OKT(TEXT("ui.scene.history_label"))}, {TEXT("count"), L->FreeTalkLog.Num()}}));}).OnClicked_Lambda([Weak=TWeakObjectPtr<AOKStreetSocialScene>(this)](){if(Weak.IsValid()){Weak->State->FreeTalkHistoryOpen=!Weak->State->FreeTalkHistoryOpen;Weak->RefreshUI(OKStreetUi::History);}return FReply::Handled();})]]
   +SVerticalBox::Slot().AutoHeight()[SNew(SBox).MinDesiredHeight_Lambda([Owner=TWeakObjectPtr<AOKStreetSocialScene>(this)](){return FOptionalSize(Owner.IsValid()&&Owner->GalgameShowsDialogue()?0.f:96.f);})
    .Padding_Lambda([Owner=TWeakObjectPtr<AOKStreetSocialScene>(this)](){return FMargin(0,0,0,Owner.IsValid()?Owner->GalgameDialogueLift():0.f);})
    .Visibility_Lambda([Owner=TWeakObjectPtr<AOKStreetSocialScene>(this)](){return Owner.IsValid()&&Owner->GalgameHidesDialogueBox()?EVisibility::Collapsed:EVisibility::Visible;})[SAssignNew(S.DialoguePanel,SBorder).BorderImage(OKStreetGlassPanel()).Padding(20)[SAssignNew(S.UiBottomHolder,SBox)]]]]]
  +SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Center).Padding(356,80,0,220)[SAssignNew(S.UiHistoryHolder,SBox).Visibility(EVisibility::SelfHitTestInvisible)]
  +SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Bottom).Padding(16,16,16,24)[SNew(SVerticalBox)
   +SVerticalBox::Slot().AutoHeight().Padding(0,0,0,8)[SNew(SBox).MaxDesiredWidth(320).Visibility_Lambda([Weak=TWeakPtr<FOKStreetSceneState>(State)](){const auto L=Weak.Pin();return L&&L->HintOpen?EVisibility::Visible:EVisibility::Collapsed;})[SNew(SBorder).BorderImage(OKStreetGlassPanel()).Padding(14)[SNew(STextBlock).AutoWrapText(true).Text_Lambda([Weak=TWeakPtr<FOKStreetSceneState>(State)](){const auto L=Weak.Pin();if(!L)return FText::GetEmpty();return FText::FromString((L->HintText.IsEmpty()?FString():L->HintText+TEXT("\n"))+(L->MouseUI?OKT(TEXT("ui.scene.ui_mode_hint")):OKT(TEXT("ui.scene.camera_mode_hint")))+OKT(TEXT("ui.scene.map_hint_m")));})]]]
   +SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left)[SNew(SBox).WidthOverride(44).HeightOverride(44)[SNew(SButton).ButtonStyle(OKStreetGlassButton()).HAlign(HAlign_Center).VAlign(VAlign_Center).ToolTipText(FText::FromString(OKT(TEXT("ui.scene.controls_hint_title"))))
    .OnClicked_Lambda([Weak=TWeakObjectPtr<AOKStreetSocialScene>(this)](){if(Weak.IsValid()){auto& L=*Weak->State;L.HintOpen=!L.HintOpen;L.HintUnread=false;}return FReply::Handled();})
    [SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Bold",20)).ColorAndOpacity_Lambda([Weak=TWeakPtr<FOKStreetSceneState>(State)](){const auto L=Weak.Pin();return FSlateColor(L&&L->HintUnread?FLinearColor(1,.83f,.35f):FLinearColor::White);}).Text(FText::FromString(TEXT("?")))]]]]
  +SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Fill).Padding(16)[SNew(SBox).WidthOverride(400)[SAssignNew(S.InteractionPanel,SBorder).BorderImage(OKStreetGlassPanel()).Padding(8)[SNew(SVerticalBox)
   +SVerticalBox::Slot().AutoHeight().Padding(0,0,0,6)[ClockRow]
   +SVerticalBox::Slot().AutoHeight().Padding(0,0,0,6)[LiftRows]
   +SVerticalBox::Slot().AutoHeight().Padding(0,0,0,6)[SleepRow]
   +SVerticalBox::Slot().FillHeight(1)[SAssignNew(S.UiRightScroll,SScrollBox).OnUserScrolled_Lambda([Weak=TWeakPtr<FOKStreetSceneState>(State)](float Offset){if(const auto L=Weak.Pin())L->RightScrollOffset=Offset;})+SScrollBox::Slot()[SAssignNew(S.UiRightHolder,SBox)]]
   +SVerticalBox::Slot().AutoHeight()[SNew(SBox).MaxDesiredHeight_Lambda([Weak=TWeakPtr<FOKStreetSceneState>(State)](){const auto L=Weak.Pin();return FOptionalSize(L&&L->ShowDebug?560.f:330.f);})[SNew(SScrollBox)+SScrollBox::Slot()[SAssignNew(S.UiDebugHolder,SBox)]]]]]]
  +SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)[SNew(SBox).Visibility_Lambda([Weak=TWeakPtr<FOKStreetSceneState>(State)](){const auto L=Weak.Pin();return L&&L->BigMapOpen?EVisibility::Visible:EVisibility::Collapsed;})[S.BigMap.ToSharedRef()]]
  // The shop's menu card, open in the middle of the screen (BuildShopPanel fills it while the menu is open), centred in
  // the space above the dialogue box: centred on the whole screen it covered the "heard" and order rows above the box.
  +SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center).Padding(0,16,0,250)[SAssignNew(S.UiMenuHolder,SBox).Visibility(EVisibility::SelfHitTestInvisible)]
  +SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)[SAssignNew(S.UiAppHolder,SBox).Visibility(EVisibility::SelfHitTestInvisible)]
  +SOverlay::Slot()[SAssignNew(S.UiSaveHolder,SBox).Visibility(EVisibility::SelfHitTestInvisible)];
  GEngine->GameViewport->AddViewportWidgetContent(S.Overlay.ToSharedRef(),10);
 }
 Mark(TEXT("header"));
 // The right panel; its scroll box stays, so the scroll position is kept (set again in case the content got shorter).
 if(Regions&OKStreetUi::Right){
 auto Box=SNew(SVerticalBox);S.PresentationTopicButtons=0;
 // Player name, editable any time; applies from the next line (subtitles and TTS).
 if(S.Manifest.Characters.IsValidIndex(0))Box->AddSlot().AutoHeight().Padding(4)[SNew(SHorizontalBox)
  +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0,0,6,0)[SNew(STextBlock).Text(FText::FromString(OKT(TEXT("ui.scene.hero_name_label"))))]
  +SHorizontalBox::Slot().FillWidth(1)[SNew(SEditableTextBox).Text(FText::FromString(S.PlayerNameDraft.IsEmpty()?S.Manifest.Characters[0].Name:S.PlayerNameDraft)).HintText(FText::FromString(S.PlayerCardName)).ClearKeyboardFocusOnCommit(true)
   .OnTextChanged_Lambda([Weak=TWeakObjectPtr<AOKStreetSocialScene>(this)](const FText& Text){if(Weak.IsValid()&&Weak->State)Weak->State->PlayerNameDraft=Text.ToString();})
   .OnTextCommitted_Lambda([Weak=TWeakObjectPtr<AOKStreetSocialScene>(this)](const FText& Text,ETextCommit::Type){if(Weak.IsValid()&&Weak->State){Weak->State->PlayerNameDraft.Reset();Weak->SetPlayerName(Text.ToString());}})]];
 const bool Busy=OKStreetConversationBusy(S.Node!=INDEX_NONE,S.CollectedActive,S.RelationshipPending,S.TouchActive);
 auto Heading=[Weak=TWeakPtr<FOKStreetSceneState>(State)](){auto L=Weak.Pin();if(!L||L->Target==INDEX_NONE)return FText::FromString(OKT(TEXT("ui.scene.app_title_apng")));const auto Relation=OKStreetViewRelation(L->Store,OKStreetStableId(L->Manifest.Characters[L->Target]),L->Manifest.TalkBudget);const float Spirits=Relation.MoodLevel*FMath::Pow(.5f,float(FMath::Max<int64>(0,OKStreetMindNow(*L)-Relation.MoodMinute))/360.f); // her spirits (R3-269)
 return FText::FromString(OKStreetScriptedName(L->Manifest.Characters[L->Target].Name)+OKMsg(TEXT("ui.scene.stats_line"), {{TEXT("favor"), Relation.Favor}, {TEXT("arousal"), Relation.Lewdness}, {TEXT("mood"), OKStreetMoodBand(Spirits)},{TEXT("stage"), Relation.Stage==TEXT("lover")?OKT(TEXT("ui.bonds.lover_stage")):Relation.Stage==TEXT("friend")?OKT(TEXT("ui.bonds.friend_stage")):OKT(TEXT("ui.scene.stage_new"))}}));};
 Box->AddSlot().AutoHeight().Padding(8)[SNew(STextBlock).Text_Lambda(Heading).AutoWrapText(true).WrapTextAt(360).Font(FCoreStyle::GetDefaultFontStyle("Bold",20))];
 if(!S.TouchNotice.IsEmpty())Box->AddSlot().AutoHeight().Padding(8)[SNew(STextBlock).Text(FText::FromString(S.TouchNotice)).AutoWrapText(true).WrapTextAt(360)];
 if(!S.TalkFailureNotice.IsEmpty())Box->AddSlot().AutoHeight().Padding(8)[SNew(STextBlock).Text(FText::FromString(S.TalkFailureNotice)).AutoWrapText(true).WrapTextAt(360)];
 Box->AddSlot().AutoHeight().HAlign(HAlign_Left).Padding(4)[SNew(SButton).ButtonStyle(OKStreetGlassButton()).IsFocusable(S.Target!=INDEX_NONE).IsEnabled(!Busy&&!S.OtherLanguage.Nodes.IsEmpty()).Text(FText::FromString(S.Japanese?OKT(TEXT("ui.scene.voice_ja")):OKT(TEXT("ui.scene.voice_zh")))).OnClicked_Lambda([Weak=TWeakObjectPtr<AOKStreetSocialScene>(this)](){if(Weak.IsValid())Weak->ToggleLanguage();return FReply::Handled();})];
 Box->AddSlot().AutoHeight().HAlign(HAlign_Left).Padding(4)[SNew(SButton).ButtonStyle(OKStreetGlassButton()).IsFocusable(S.Target!=INDEX_NONE).IsEnabled(!S.TtsCacheDir.IsEmpty()).Text(FText::FromString(S.TtsEnabled?OKT(TEXT("ui.scene.voice_tts")):OKT(TEXT("ui.scene.voice_builtin")))).OnClicked_Lambda([Weak=TWeakObjectPtr<AOKStreetSocialScene>(this)](){if(Weak.IsValid())Weak->ToggleVoiceSource();return FReply::Handled();})];
 // The TTS model (from the next start): 1.7B sounds closer to each heroine, 0.6B takes about 1 GB less VRAM.
 if(S.TtsEnabled&&!S.TtsSettingsPath.IsEmpty())Box->AddSlot().AutoHeight().HAlign(HAlign_Left).Padding(4)[SNew(SButton).ButtonStyle(OKStreetGlassButton()).IsFocusable(S.Target!=INDEX_NONE)
  .Text(FText::FromString(OKMsg(TEXT("ui.scene.tts_model"),{{TEXT("size"),*S.TtsBaseSize}}))).OnClicked_Lambda([Weak=TWeakObjectPtr<AOKStreetSocialScene>(this)](){if(Weak.IsValid())Weak->ToggleTtsBaseSize();return FReply::Handled();})];
 {if(S.LangOptions.IsEmpty())for(const TCHAR* L:{TEXT("zh-TW"),TEXT("zh-CN"),TEXT("en")})S.LangOptions.Add(MakeShared<FString>(L));
  const int32 LangIndex=FMath::Max(0,S.LangOptions.IndexOfByPredicate([&](const TSharedPtr<FString>& L){return *L==S.Lang;}));
  Box->AddSlot().AutoHeight().HAlign(HAlign_Left).Padding(4)[SNew(SComboBox<TSharedPtr<FString>>).OptionsSource(&S.LangOptions).InitiallySelectedItem(S.LangOptions[LangIndex])
   .OnGenerateWidget_Lambda([Weak=TWeakObjectPtr<AOKStreetSocialScene>(this)](TSharedPtr<FString> Item){return SNew(STextBlock).Text(FText::FromString(Item.IsValid()&&Weak.IsValid()?Weak->T(*(TEXT("ui.settings.lang_")+*Item)):FString()));})
   .OnSelectionChanged_Lambda([Weak=TWeakObjectPtr<AOKStreetSocialScene>(this)](TSharedPtr<FString> Item,ESelectInfo::Type Info){if(Weak.IsValid()&&Item.IsValid()&&Info!=ESelectInfo::Direct)Weak->SetLanguage(*Item);})
   [SNew(STextBlock).Text_Lambda([Weak=TWeakObjectPtr<AOKStreetSocialScene>(this)](){if(!Weak.IsValid())return FText::GetEmpty();const FString Name=Weak->T(*(TEXT("ui.settings.lang_")+Weak->State->Lang));
     FStringFormatNamedArguments A;A.Add(TEXT("name"),Name);return FText::FromString(Weak->T(TEXT("ui.settings.language"),A));})]];}
 Mark(TEXT("look"));
 // The characters' look: each card's own ramp, one of the game's shadow ramps (live), or HG (reloads on "auto").
 {if(S.RenderStyleOptions.IsEmpty()){S.RenderStyleOptions.Add(MakeShared<FString>(TEXT("koikatsu")));for(const auto& R:OKRenderRamps())S.RenderStyleOptions.Add(MakeShared<FString>(TEXT("ramp:")+R.Key));S.RenderStyleOptions.Add(MakeShared<FString>(TEXT("hg")));}
  const FString Style=RenderStyle();const int32 StyleIndex=FMath::Max(0,S.RenderStyleOptions.IndexOfByPredicate([&](const TSharedPtr<FString>& R){return *R==Style;}));
  Box->AddSlot().AutoHeight().HAlign(HAlign_Left).Padding(4)[SNew(SComboBox<TSharedPtr<FString>>).OptionsSource(&S.RenderStyleOptions).InitiallySelectedItem(S.RenderStyleOptions[StyleIndex])
   .OnGenerateWidget_Lambda([Weak=TWeakObjectPtr<AOKStreetSocialScene>(this)](TSharedPtr<FString> Item){return SNew(STextBlock).Text(FText::FromString(Item.IsValid()&&Weak.IsValid()?Weak->RenderStyleLabel(*Item):FString()));})
   .OnSelectionChanged_Lambda([Weak=TWeakObjectPtr<AOKStreetSocialScene>(this)](TSharedPtr<FString> Item,ESelectInfo::Type Info){if(Weak.IsValid()&&Item.IsValid()&&Info!=ESelectInfo::Direct)Weak->SetRenderStyle(*Item);})
   [SNew(STextBlock).Text(FText::FromString(OKMsg(TEXT("ui.settings.render_style"),{{TEXT("name"),RenderStyleLabel(Style)}})))]];}
 if(!S.SelfTest)Box->AddSlot().AutoHeight().HAlign(HAlign_Left).Padding(4)[SNew(SButton).ButtonStyle(OKStreetGlassButton()).Text(FText::FromString(OKT(TEXT("ui.saves.open_button"))))
  .OnClicked_Lambda([Weak=TWeakObjectPtr<AOKStreetSocialScene>(this)](){if(Weak.IsValid())Weak->OpenSaveMenu(TEXT("game"));return FReply::Handled();})];
  // The setting (Taiwan, mainland China, a US college town): its money and prices; the save's money is converted.
 {if(S.RegionOptions.IsEmpty())for(const TCHAR* R:{TEXT("TW"),TEXT("CN"),TEXT("US")})S.RegionOptions.Add(MakeShared<FString>(R));
  const int32 RegionIndex=FMath::Max(0,S.RegionOptions.IndexOfByPredicate([&](const TSharedPtr<FString>& R){return *R==S.Store.Region;}));
  Box->AddSlot().AutoHeight().HAlign(HAlign_Left).Padding(4)[SNew(SComboBox<TSharedPtr<FString>>).OptionsSource(&S.RegionOptions).InitiallySelectedItem(S.RegionOptions[RegionIndex])
   .OnGenerateWidget_Lambda([](TSharedPtr<FString> Item){return SNew(STextBlock).Text(FText::FromString(Item.IsValid()?FString(OKT(*(TEXT("ui.region.name_")+*Item))):FString()));})
   .OnSelectionChanged_Lambda([Weak=TWeakObjectPtr<AOKStreetSocialScene>(this)](TSharedPtr<FString> Item,ESelectInfo::Type Info){if(Weak.IsValid()&&Item.IsValid()&&Info!=ESelectInfo::Direct)Weak->ChangeRegion(*Item);})
   [SNew(STextBlock).Text_Lambda([Weak=TWeakObjectPtr<AOKStreetSocialScene>(this)](){if(!Weak.IsValid())return FText::GetEmpty();
     return FText::FromString(OKMsg(TEXT("ui.region.setting"),{{TEXT("name"),OKT(*(TEXT("ui.region.name_")+Weak->State->Store.Region))}}));})]];}
 if(S.TtsEnabled&&!S.TtsCacheDir.IsEmpty()){
  if(S.MaleToneOptions.IsEmpty())for(const TCHAR* Name:OKStreetMaleToneNames)S.MaleToneOptions.Add(MakeShared<FString>(Name));
  const int32 ToneIndex=FMath::Max(0,OKStreetMaleToneIndex(S.MaleTone));
  Box->AddSlot().AutoHeight().HAlign(HAlign_Left).Padding(4)[SNew(SComboBox<TSharedPtr<FString>>).OptionsSource(&S.MaleToneOptions).InitiallySelectedItem(S.MaleToneOptions[ToneIndex])
   .OnGenerateWidget_Lambda([](TSharedPtr<FString> Item){return SNew(STextBlock).Text(FText::FromString(Item.IsValid()?*Item:FString()));})
   .OnSelectionChanged_Lambda([Weak=TWeakObjectPtr<AOKStreetSocialScene>(this)](TSharedPtr<FString> Item,ESelectInfo::Type Info){if(!Weak.IsValid()||!Item.IsValid()||Info==ESelectInfo::Direct)return;const int32 I=Weak->State->MaleToneOptions.IndexOfByKey(Item);if(I!=INDEX_NONE)Weak->SetMaleTone(I);})
   [SNew(STextBlock).Text_Lambda([Weak=TWeakObjectPtr<AOKStreetSocialScene>(this)](){return FText::FromString(OKT(TEXT("ui.scene.his_tone_label"))+(Weak.IsValid()?Weak->MaleToneLabel():FString()));})]];
  if(S.Target!=INDEX_NONE&&!S.TtsVoiceInfo.IsEmpty())Box->AddSlot().AutoHeight().Padding(8,2)[SNew(STextBlock).Text(FText::FromString(S.TtsVoiceInfo)).AutoWrapText(true).WrapTextAt(360).Font(FCoreStyle::GetDefaultFontStyle("Regular",11))];
  Mark(TEXT("voicepick"));AddVoicePickUI(Box);
  Mark(TEXT("song"));BuildSongRow(Box);
  Mark(TEXT("look2"));
 }
 if(!S.TtsNotice.IsEmpty())Box->AddSlot().AutoHeight().Padding(8)[SNew(STextBlock).Text(FText::FromString(S.TtsNotice)).AutoWrapText(true).WrapTextAt(360)];
 Box->AddSlot().AutoHeight().Padding(4)[SNew(SButton).ButtonStyle(OKStreetGlassButton()).Text(FText::FromString(S.ShowDebug?OKT(TEXT("ui.scene.collapse_heroine_stats")):OKT(TEXT("ui.scene.heroine_stats_view_edit")))).OnClicked_Lambda([Weak=TWeakObjectPtr<AOKStreetSocialScene>(this)](){if(Weak.IsValid()){Weak->State->ShowDebug=!Weak->State->ShowDebug;Weak->RefreshUI();}return FReply::Handled();})];
 Box->AddSlot().AutoHeight().Padding(4)[SNew(SButton).ButtonStyle(OKStreetGlassButton()).Text(FText::FromString(S.ContactsOpen?OKT(TEXT("ui.scene.collapse_girl_list")):OKT(TEXT("ui.scene.girl_list_nav")))).OnClicked_Lambda([Weak=TWeakObjectPtr<AOKStreetSocialScene>(this)](){if(Weak.IsValid()){Weak->State->ContactsOpen=!Weak->State->ContactsOpen;Weak->RefreshUI();}return FReply::Handled();})];
 // Navigation and its notice are built always and shown by state: starting or stopping a navigation needs no rebuild.
 Box->AddSlot().AutoHeight().Padding(4)[SNew(STextBlock).Visibility_Lambda([Weak=TWeakPtr<FOKStreetSceneState>(State)](){auto L=Weak.Pin();return L&&!L->ContactsNotice.IsEmpty()?EVisibility::Visible:EVisibility::Collapsed;}).Text_Lambda([Weak=TWeakPtr<FOKStreetSceneState>(State)](){auto L=Weak.Pin();return L?FText::FromString(L->ContactsNotice):FText::GetEmpty();}).AutoWrapText(true).WrapTextAt(360)];
 {auto NavGirlShown=[Weak=TWeakPtr<FOKStreetSceneState>(State)](){auto L=Weak.Pin();return L&&L->NavGirl!=INDEX_NONE?EVisibility::Visible:EVisibility::Collapsed;};
 Box->AddSlot().AutoHeight().Padding(4)[SNew(STextBlock).Visibility_Lambda(NavGirlShown).Text_Lambda([Weak=TWeakPtr<FOKStreetSceneState>(State)](){auto L=Weak.Pin();if(!L||!L->Agents.IsValidIndex(L->NavGirl-1))return FText::GetEmpty();return FText::FromString(OKMsg(TEXT("ui.scene.nav_distance_path"), {{TEXT("name"), *L->Manifest.Characters[L->NavGirl].Name}, {TEXT("distance"), FVector::Dist2D(L->Male,L->Agents[L->NavGirl-1].Position)/100}, {TEXT("path_hint"), L->NavPath.IsEmpty()?OKT(TEXT("ui.scene.no_route_label")):OKT(TEXT("ui.scene.follow_gold_route"))}}));}).AutoWrapText(true).WrapTextAt(360)];
 Box->AddSlot().AutoHeight().Padding(4)[SNew(SButton).Visibility_Lambda(NavGirlShown).ButtonStyle(OKStreetGlassButton()).Text(FText::FromString(OKT(TEXT("ui.scene.cancel_navigation")))).OnClicked_Lambda([Weak=TWeakObjectPtr<AOKStreetSocialScene>(this)](){if(Weak.IsValid()){Weak->StopNavigation(OKT(TEXT("ui.scene.nav_cancelled")));}return FReply::Handled();})];}
 {auto NavPointShown=[Weak=TWeakPtr<FOKStreetSceneState>(State)](){auto L=Weak.Pin();return L&&L->NavPointActive?EVisibility::Visible:EVisibility::Collapsed;};
 Box->AddSlot().AutoHeight().Padding(4)[SNew(STextBlock).Visibility_Lambda(NavPointShown).Text_Lambda([Weak=TWeakPtr<FOKStreetSceneState>(State)](){auto L=Weak.Pin();if(!L||!L->NavPointActive)return FText::GetEmpty();return FText::FromString(OKMsg(TEXT("ui.scene.nav_distance"), {{TEXT("name"), L->NavPointName.IsEmpty()?OKT(TEXT("ui.scene.map_location")):*L->NavPointName}, {TEXT("distance"), FVector::Dist2D(L->Male,L->NavPoint)/100}}));}).AutoWrapText(true).WrapTextAt(360)];
 Box->AddSlot().AutoHeight().Padding(4)[SNew(SButton).Visibility_Lambda(NavPointShown).ButtonStyle(OKStreetGlassButton()).Text(FText::FromString(OKT(TEXT("ui.scene.cancel_navigation")))).OnClicked_Lambda([Weak=TWeakObjectPtr<AOKStreetSocialScene>(this)](){if(Weak.IsValid())Weak->StopNavigation(OKT(TEXT("ui.scene.nav_cancelled")));return FReply::Handled();})];}
 if(S.PrivateGirl!=INDEX_NONE)Box->AddSlot().AutoHeight().Padding(4)[SNew(SButton).ButtonStyle(OKStreetGlassButton()).Text(FText::FromString(OKT(TEXT("ui.scene.exit_one_on_one")))).OnClicked_Lambda([Weak=TWeakObjectPtr<AOKStreetSocialScene>(this)](){if(Weak.IsValid())Weak->FinishPrivate();return FReply::Handled();})];
 S.FacilityExitUI=false;
 if(S.Facility!=INDEX_NONE){S.FacilityExitUI=true;Box->AddSlot().AutoHeight().Padding(4)[SNew(SButton).ButtonStyle(OKStreetGlassButton()).Text(FText::FromString(S.FacilityEntered?OKT(TEXT("ui.scene.leave_facility_to_street")):OKT(TEXT("ui.scene.cancel_facility_prep")))).OnClicked_Lambda([Weak=TWeakObjectPtr<AOKStreetSocialScene>(this)](){if(Weak.IsValid())Weak->FinishFacility();return FReply::Handled();})];}
 S.ContactRows=0;S.ContactActions=0;
 if(S.ContactsOpen){
  auto List=SNew(SVerticalBox);
  for(const auto& Girl:S.Agents){const int32 Slot=Girl.Slot;++S.ContactRows;
   List->AddSlot().AutoHeight().Padding(2)[SNew(SButton).ButtonStyle(OKStreetGlassButton()).Text_Lambda([Weak=TWeakObjectPtr<AOKStreetSocialScene>(this),Slot](){if(!Weak.IsValid())return FText::GetEmpty();const auto& L=*Weak->State;const auto R=OKStreetViewRelation(L.Store,OKStreetStableId(L.Manifest.Characters[Slot]),L.Manifest.TalkBudget);return FText::FromString(FString::Printf(TEXT("%s%s  ·  %s  ·  %.1fm"),L.ContactSelection==Slot?TEXT("▶ "):TEXT(""),*OKStreetScriptedName(L.Manifest.Characters[Slot].Name),R.Stage==TEXT("lover")?OKT(TEXT("ui.bonds.lover_stage")):R.Stage==TEXT("friend")?OKT(TEXT("ui.bonds.friend_stage")):OKT(TEXT("ui.scene.stage_new")),FVector::Dist2D(L.Male,L.Agents[Slot-1].Position)/100));}).OnClicked_Lambda([Weak=TWeakObjectPtr<AOKStreetSocialScene>(this),Slot](){if(Weak.IsValid())Weak->SelectContact(Slot);return FReply::Handled();})];
  }
  Box->AddSlot().AutoHeight().Padding(4)[SNew(SBox).MaxDesiredHeight(200)[SNew(SScrollBox)+SScrollBox::Slot()[List]]];
  const int32 Slot=S.ContactSelection;
  if(S.Agents.IsValidIndex(Slot-1)){
   Box->AddSlot().AutoHeight().Padding(4)[SNew(STextBlock).Text(FText::FromString(OKT(TEXT("ui.scene.target_label"))+OKStreetScriptedName(S.Manifest.Characters[Slot].Name))).AutoWrapText(true).WrapTextAt(360).ColorAndOpacity(FLinearColor(1,.83f,.35f))];
   Box->AddSlot().AutoHeight().Padding(4)[SNew(STextBlock).Text_Lambda([Weak=TWeakObjectPtr<AOKStreetSocialScene>(this),Slot](){if(!Weak.IsValid())return FText::GetEmpty();const auto& L=*Weak->State;FString Hint;if(!Weak->ContactReady(Slot))Hint=OKT(TEXT("ui.scene.character_loading"));else if(L.PrivateGirl!=INDEX_NONE)Hint=OKT(TEXT("ui.scene.exit_one_on_one_first"));else if(!Weak->ContactIdle())Hint=OKT(TEXT("ui.scene.finish_current_first"));else Hint=OKT(TEXT("ui.scene.navigate_or_scene_hint"));return FText::FromString(Hint);}).AutoWrapText(true).WrapTextAt(360)];
   for(int32 Action=0;Action<2;++Action){++S.ContactActions;const TCHAR* Labels[]={OKT(TEXT("ui.scene.navigate_to_her")),OKT(TEXT("ui.scene.enter_one_on_one"))};Box->AddSlot().AutoHeight().Padding(4)[SNew(SButton).ButtonStyle(OKStreetGlassButton()).IsEnabled_Lambda([Weak=TWeakObjectPtr<AOKStreetSocialScene>(this),Slot,Action](){return Weak.IsValid()&&Weak->ContactReady(Slot)&&(Action==0?Weak->State->PrivateGirl==INDEX_NONE:Weak->ContactIdle());}).Text(FText::FromString(Labels[Action])).OnClicked_Lambda([Weak=TWeakObjectPtr<AOKStreetSocialScene>(this),Slot,Action](){if(Weak.IsValid()){if(Action==0)Weak->NavigateGirl(Slot);else if(Weak->PrivateConsent(Slot))Weak->BeginPrivate(Slot);}return FReply::Handled();})];}
  }else Box->AddSlot().AutoHeight().Padding(4)[SNew(STextBlock).Text(FText::FromString(OKT(TEXT("ui.scene.pick_girl_first")))).AutoWrapText(true).WrapTextAt(360)];
 }
 if(S.Pair.Splash||S.Pair.Watermelon){Box->AddSlot().AutoHeight().Padding(4)[SNew(SButton).ButtonStyle(OKStreetGlassButton()).Text(FText::FromString(S.Pair.Watermelon?OKT(TEXT("ui.scene.end_watermelon")):OKT(TEXT("ui.scene.end_splash")))).OnClicked_Lambda([Weak=TWeakObjectPtr<AOKStreetSocialScene>(this)](){if(Weak.IsValid())Weak->FinishPhoto();return FReply::Handled();})];}
 if(S.Pair.Partner!=INDEX_NONE&&!S.Pair.Conversation()&&!S.Pair.Splash&&!S.Pair.Watermelon){
  Box->AddSlot().AutoHeight().Padding(4)[SNew(SButton).ButtonStyle(OKStreetGlassButton()).IsEnabled(S.Pair.Phase==1&&!S.Pair.CapturePending).Text(FText::FromString(OKT(TEXT("ui.scene.photo_save_button")))).OnClicked_Lambda([Weak=TWeakObjectPtr<AOKStreetSocialScene>(this)](){if(Weak.IsValid())Weak->CapturePhoto();return FReply::Handled();})];
  if(!S.PhotoPoseKey.IsEmpty())Box->AddSlot().AutoHeight().Padding(4)[SNew(SButton).ButtonStyle(OKStreetGlassButton()).IsEnabled(S.Pair.Phase==1&&!S.Pair.CapturePending).Text(FText::FromString(OKT(TEXT("ui.scene.photo_another_pose")))).OnClicked_Lambda([Weak=TWeakObjectPtr<AOKStreetSocialScene>(this)](){if(Weak.IsValid())Weak->SwitchPhotoPose(FString());return FReply::Handled();})];
  Box->AddSlot().AutoHeight().Padding(4)[SNew(SButton).ButtonStyle(OKStreetGlassButton()).Text(FText::FromString(OKT(TEXT("ui.scene.end_photo")))).OnClicked_Lambda([Weak=TWeakObjectPtr<AOKStreetSocialScene>(this)](){if(Weak.IsValid())Weak->FinishPhoto();return FReply::Handled();})];
 }
 if(S.Companion!=INDEX_NONE){
  Box->AddSlot().AutoHeight().Padding(4)[SNew(STextBlock).AutoWrapText(true).WrapTextAt(360).Text(FText::FromString(S.OutingGirl!=INDEX_NONE?OKMsg(TEXT("ui.scene.walking_together_hint_2"), {{TEXT("companion"), *S.Manifest.Characters[S.Companion].Name}, {TEXT("place"), *S.OutingPlace}}):S.Manifest.Characters[S.Companion].Name+OKT(TEXT("ui.scene.walking_together_hint"))))];
  Box->AddSlot().AutoHeight().Padding(4)[SNew(SButton).ButtonStyle(OKStreetGlassButton()).Text(FText::FromString(S.OutingGirl!=INDEX_NONE?OKT(TEXT("ui.scene.cancel_going")):OKT(TEXT("ui.scene.end_walking_together")))).OnClicked_Lambda([Weak=TWeakObjectPtr<AOKStreetSocialScene>(this)](){if(Weak.IsValid()){if(Weak->State->OutingGirl!=INDEX_NONE)Weak->CancelOuting(OKT(TEXT("ui.scene.go_cancelled")));else Weak->FarewellCompanion();}return FReply::Handled();})];
 }
 Mark(TEXT("party"));BluffUI(Box); // 吹牛 with the group (OKStreetSocialBluff.inl)
// Follow me: who walks with him, and the girls nearby he can call over (any number; OKStreetSocialParty.inl).
// Talking: anyone in calling distance, her too; walking alone: the girl right beside him.
 {
  auto Row=SNew(SWrapBox).UseAllottedSize(true);bool Any=false;
  // Goodbye to one of them; while she is leaving (10 s) the button keeps her instead, with the seconds left.
  auto Goodbye=[&](int32 G){Any=true;Row->AddSlot().Padding(2)[SNew(SButton).ButtonStyle(OKStreetGlassButton())
   .Text_Lambda([Weak=TWeakPtr<FOKStreetSceneState>(State),G](){auto L=Weak.Pin();if(!L||!L->Manifest.Characters.IsValidIndex(G))return FText();const double* At=L->PartyLeaving.Find(G);
    return FText::FromString(At?OKMsg(TEXT("ui.scene.retain_countdown"), {{TEXT("name"), *L->Manifest.Characters[G].Name}, {TEXT("seconds"), FMath::Max(0,int32(FMath::CeilToDouble(*At-FPlatformTime::Seconds())))}}):OKMsg(TEXT("ui.scene.say_goodbye_to_name"), {{TEXT("name"), *L->Manifest.Characters[G].Name}}));})
   .OnClicked_Lambda([Weak=TWeakObjectPtr<AOKStreetSocialScene>(this),G](){if(Weak.IsValid()){if(Weak->State->PartyLeaving.Contains(G))Weak->PartyRetain(G);else Weak->PartyFarewell(G);}return FReply::Handled();})];};
  {const int32 Partner=FreeTalkPartner();if(Partner!=INDEX_NONE&&!S.Party.Contains(Partner)&&!S.Party.IsEmpty())Goodbye(Partner);}
  for(const int32 G:S.Party)Goodbye(G);
  for(int32 G=1;G<S.Manifest.Characters.Num();++G)if(PartyCanJoin(G)&&(S.Target!=INDEX_NONE||FreeTalkPartner()!=INDEX_NONE||(S.Agents.IsValidIndex(G-1)&&FVector::Dist2D(S.Agents[G-1].Position,S.Male)<400.f))){Any=true;Row->AddSlot().Padding(2)[SNew(SButton).ButtonStyle(OKStreetGlassButton()).Text(FText::FromString(OKMsg(TEXT("ui.scene.call_follow_me"), {{TEXT("name"), *S.Manifest.Characters[G].Name}}))).OnClicked_Lambda([Weak=TWeakObjectPtr<AOKStreetSocialScene>(this),G](){if(Weak.IsValid())Weak->PartyJoin(G);return FReply::Handled();})];}
  if(Any)Box->AddSlot().AutoHeight().Padding(4)[Row];
 }
 Mark(TEXT("target"));
 if(S.Target!=INDEX_NONE){
  const int32 Partner=S.Target;
  Box->AddSlot().AutoHeight().Padding(4)[SNew(SButton).ButtonStyle(OKStreetGlassButton()).Text(FText::FromString(S.TopicsOpen?OKT(TEXT("ui.scene.collapse_topics")):OKT(TEXT("ui.scene.throw_topic")))).OnClicked_Lambda([Weak=TWeakObjectPtr<AOKStreetSocialScene>(this)](){if(Weak.IsValid()){Weak->State->TopicsOpen=!Weak->State->TopicsOpen;Weak->RefreshUI();}return FReply::Handled();})];
   auto Row=SNew(SVerticalBox);const TCHAR* Labels[]={OKT(TEXT("ui.scene.topic_daily")),OKT(TEXT("ui.scene.topic_romance")),OKT(TEXT("ui.scene.topic_sexual"))};const TCHAR* Categories[]={TEXT("daily"),TEXT("romance"),TEXT("intimacy")};
   if(S.TopicsOpen)for(int32 I=0;I<3;++I){++S.PresentationTopicButtons;const FString Category=Categories[I];Row->AddSlot().AutoHeight().Padding(4)[SNew(SButton).ButtonStyle(OKStreetGlassButton()).IsEnabled(!Busy).Text(FText::FromString(Labels[I])).OnClicked_Lambda([Weak=TWeakObjectPtr<AOKStreetSocialScene>(this),Category](){if(Weak.IsValid()){Weak->State->TopicsOpen=false;Weak->Topic(Category);}return FReply::Handled();})];}
   Row->AddSlot().AutoHeight().Padding(4)[SNew(SButton).ButtonStyle(OKStreetGlassButton()).Text(FText::FromString(!Busy?OKT(TEXT("ui.scene.farewell_label")):OKT(TEXT("ui.scene.cancel_button")))).OnClicked_Lambda([Weak=TWeakObjectPtr<AOKStreetSocialScene>(this)](){if(Weak.IsValid())Weak->Farewell();return FReply::Handled();})];
  Box->AddSlot().AutoHeight()[Row];
  if(S.PrivateGirl==INDEX_NONE){
   Box->AddSlot().AutoHeight().Padding(4)[SNew(SButton).ButtonStyle(OKStreetGlassButton()).IsEnabled(!Busy&&ContactIdle()&&ContactReady(Partner)).Text(FText::FromString(OKT(TEXT("ui.scene.enter_scene")))).OnClicked_Lambda([Weak=TWeakObjectPtr<AOKStreetSocialScene>(this),Partner](){if(Weak.IsValid()&&Weak->PrivateConsent(Partner))Weak->BeginPrivate(Partner);return FReply::Handled();})];
   Box->AddSlot().AutoHeight().Padding(4)[SNew(SButton).ButtonStyle(OKStreetGlassButton()).Text(FText::FromString(S.ActivitiesOpen?OKT(TEXT("ui.scene.collapse_activity")):OKT(TEXT("ui.scene.expand_activity")))).OnClicked_Lambda([Weak=TWeakObjectPtr<AOKStreetSocialScene>(this)](){if(Weak.IsValid()){Weak->State->ActivitiesOpen=!Weak->State->ActivitiesOpen;Weak->RefreshUI();}return FReply::Handled();})];
   if(S.ActivitiesOpen||S.Pair.Conversation()){
  for(int32 SplashKind:{3,4})Box->AddSlot().AutoHeight().Padding(4)[SNew(SButton).ButtonStyle(OKStreetGlassButton()).IsEnabled(!Busy&&S.PrivateGirl==INDEX_NONE&&S.Pair.Partner==INDEX_NONE&&!S.SplashMaleThrow.IsEmpty()&&!S.SplashMaleReceive.IsEmpty()&&!S.SplashFemaleThrow.IsEmpty()&&!S.SplashFemaleReceive.IsEmpty()).Text(FText::FromString(SplashKind==3?OKT(TEXT("ui.scene.splash_her_button")):OKT(TEXT("ui.scene.ask_her_to_splash")))).OnClicked_Lambda([Weak=TWeakObjectPtr<AOKStreetSocialScene>(this),Partner,SplashKind](){if(Weak.IsValid())Weak->StartActivityFromPanel(Partner,SplashKind==3?TEXT("splash"):TEXT("splash_her"));return FReply::Handled();})];
  Box->AddSlot().AutoHeight().Padding(4)[SNew(SButton).ButtonStyle(OKStreetGlassButton()).IsEnabled(S.Pair.PhoneUse||(!Busy&&S.PrivateGirl==INDEX_NONE&&S.Pair.Partner==INDEX_NONE)).Text(FText::FromString(S.Pair.PhoneUse?OKT(TEXT("ui.scene.end_phone_activity")):OKT(TEXT("ui.scene.use_phone_together")))).OnClicked_Lambda([Weak=TWeakObjectPtr<AOKStreetSocialScene>(this),Partner](){if(Weak.IsValid()){if(Weak->State->Pair.PhoneUse)Weak->FinishPhoto();else if(Weak->PanelConsent(Partner,TEXT("phone_together")))Weak->BeginPair(Partner,8);}return FReply::Handled();})];
  Box->AddSlot().AutoHeight().Padding(4)[SNew(SButton).ButtonStyle(OKStreetGlassButton()).IsEnabled(S.Pair.Drink||(!Busy&&S.PrivateGirl==INDEX_NONE&&S.Pair.Partner==INDEX_NONE&&!S.DrinkMaleClip.IsEmpty()&&!S.DrinkFemaleClip.IsEmpty())).Text(FText::FromString(S.Pair.Drink?OKT(TEXT("ui.scene.end_shared_drink")):OKT(TEXT("ui.scene.drink_together")))).OnClicked_Lambda([Weak=TWeakObjectPtr<AOKStreetSocialScene>(this),Partner](){if(Weak.IsValid()){if(Weak->State->Pair.Drink)Weak->FinishPhoto();else Weak->StartActivityFromPanel(Partner,TEXT("drink"));}return FReply::Handled();})];
  for(int32 Kind:{6,7})Box->AddSlot().AutoHeight().Padding(4)[SNew(SButton).ButtonStyle(OKStreetGlassButton()).IsEnabled(!Busy&&S.PrivateGirl==INDEX_NONE&&S.Pair.Partner==INDEX_NONE).Text(FText::FromString(Kind==6?OKT(TEXT("ui.scene.watermelon_you_swing")):OKT(TEXT("ui.scene.watermelon_her_swing")))).OnClicked_Lambda([Weak=TWeakObjectPtr<AOKStreetSocialScene>(this),Partner,Kind](){if(Weak.IsValid())Weak->StartActivityFromPanel(Partner,Kind==6?TEXT("watermelon"):TEXT("watermelon_her"));return FReply::Handled();})];
  Box->AddSlot().AutoHeight().Padding(4)[SNew(SButton).ButtonStyle(OKStreetGlassButton()).IsEnabled(S.Pair.Meal||(!Busy&&S.PrivateGirl==INDEX_NONE&&S.Pair.Partner==INDEX_NONE)).Text(FText::FromString(S.Pair.Meal?OKT(TEXT("ui.scene.end_shared_meal")):OKT(TEXT("ui.scene.eat_together")))).OnClicked_Lambda([Weak=TWeakObjectPtr<AOKStreetSocialScene>(this),Partner](){if(Weak.IsValid()){if(Weak->State->Pair.Meal)Weak->FinishPhoto();else Weak->RequestInvitation(Partner,3);}return FReply::Handled();})];
  Box->AddSlot().AutoHeight().Padding(4)[SNew(SButton).ButtonStyle(OKStreetGlassButton()).IsEnabled(S.Pair.Seated||(!Busy&&S.PrivateGirl==INDEX_NONE&&S.Pair.Partner==INDEX_NONE&&!S.SeatedMaleClip.IsEmpty()&&!S.SeatedFemaleClip.IsEmpty())).Text(FText::FromString(S.Pair.Seated?OKT(TEXT("ui.scene.leave_seat")):OKT(TEXT("ui.scene.sit_and_chat_button")))).OnClicked_Lambda([Weak=TWeakObjectPtr<AOKStreetSocialScene>(this),Partner](){if(Weak.IsValid()){if(Weak->State->Pair.Seated)Weak->FinishPhoto();else if(Weak->PanelConsent(Partner,TEXT("sit_talk")))Weak->BeginPair(Partner,true);}return FReply::Handled();})];
  Box->AddSlot().AutoHeight().Padding(4)[SNew(SButton).ButtonStyle(OKStreetGlassButton()).IsEnabled(!Busy&&S.PrivateGirl==INDEX_NONE&&S.Pair.Partner==INDEX_NONE&&S.Companion==INDEX_NONE&&S.Facility==INDEX_NONE).Text(FText::FromString(OKT(TEXT("ui.scene.walk_explore_topic")))).OnClicked_Lambda([Weak=TWeakObjectPtr<AOKStreetSocialScene>(this),Partner](){if(Weak.IsValid())Weak->RequestInvitation(Partner,4);return FReply::Handled();})];
  Box->AddSlot().AutoHeight().Padding(4)[SNew(SButton).ButtonStyle(OKStreetGlassButton()).IsEnabled(!Busy&&S.PrivateGirl==INDEX_NONE&&S.Pair.Partner==INDEX_NONE&&S.PhotoPoses.Contains(S.Manifest.Characters[Partner].PersonalityId)&&!S.PhotoMaleClip.IsEmpty()).Text(FText::FromString(OKT(TEXT("ui.scene.photo_together")))).OnClicked_Lambda([Weak=TWeakObjectPtr<AOKStreetSocialScene>(this),Partner](){if(Weak.IsValid()&&Weak->PanelConsent(Partner,TEXT("photo")))Weak->BeginPhoto(Partner);return FReply::Handled();})];
  auto Facilities=SNew(SVerticalBox);const TCHAR* FacilityLabels[]={OKT(TEXT("ui.scene.go_restaurant")),OKT(TEXT("ui.scene.go_rest_room")),OKT(TEXT("ui.scene.go_oil_room"))};
  for(int32 Kind=0;Kind<3;++Kind){const int32 Character=S.Target;Facilities->AddSlot().AutoHeight().Padding(4)[SNew(SButton).ButtonStyle(OKStreetGlassButton()).IsEnabled(!Busy&&S.PrivateGirl==INDEX_NONE&&S.Pair.Partner==INDEX_NONE&&S.Facility==INDEX_NONE).Text(FText::FromString(FacilityLabels[Kind])).OnClicked_Lambda([Weak=TWeakObjectPtr<AOKStreetSocialScene>(this),Kind,Character](){if(Weak.IsValid())Weak->StartActivityFromPanel(Character,Kind==0?TEXT("restaurant_room"):Kind==1?TEXT("rest"):TEXT("spa"));return FReply::Handled();})];}Box->AddSlot().AutoHeight()[Facilities];
   }
  }
  Box->AddSlot().AutoHeight().Padding(4)[SNew(SButton).ButtonStyle(OKStreetGlassButton()).Text(FText::FromString(S.TouchOpen?OKT(TEXT("ui.scene.collapse_touch")):OKT(TEXT("ui.scene.expand_touch")))).OnClicked_Lambda([Weak=TWeakObjectPtr<AOKStreetSocialScene>(this)](){if(Weak.IsValid()){Weak->State->TouchOpen=!Weak->State->TouchOpen;Weak->RefreshUI();}return FReply::Handled();})];
  if(S.TouchOpen){
  auto TouchRow=SNew(SVerticalBox);
  TouchRow->AddSlot().AutoHeight().Padding(2)[SNew(SButton).ButtonStyle(OKStreetGlassButton()).IsEnabled(!S.TouchActive).Text(FText::FromString(S.TouchSee?OKT(TEXT("ui.scene.mode_gaze")):OKT(TEXT("ui.scene.mode_touch")))).OnClicked_Lambda([Weak=TWeakObjectPtr<AOKStreetSocialScene>(this)](){if(Weak.IsValid()&&Weak->State){Weak->State->TouchSee=!Weak->State->TouchSee;Weak->RefreshUI();}return FReply::Handled();})];
  const TCHAR* TouchLabels[]={OKT(TEXT("ui.scene.head")),OKT(TEXT("ui.scene.cheek")),OKT(TEXT("ui.scene.left_hand")),OKT(TEXT("ui.scene.right_hand")),OKT(TEXT("ui.scene.left_chest")),OKT(TEXT("ui.scene.right_breast"))};
  for(int32 Region=0;Region<6;++Region)TouchRow->AddSlot().AutoHeight().Padding(2)[SNew(SButton).ButtonStyle(OKStreetGlassButton()).IsEnabled(!S.TouchActive&&OKStreetTouchPart(Region,S.TouchSee)!=INDEX_NONE).Text(FText::FromString(TouchLabels[Region])).OnClicked_Lambda([Weak=TWeakObjectPtr<AOKStreetSocialScene>(this),Region](){if(Weak.IsValid())Weak->Touch(Region);return FReply::Handled();})];
  Box->AddSlot().AutoHeight()[TouchRow];
  }
  if(S.TopicsOpen||S.AwaitTopicAnswer){
   if(!S.CollectedTopics.Topics.IsEmpty())Box->AddSlot().AutoHeight().Padding(4)[SNew(SButton).ButtonStyle(OKStreetGlassButton()).IsEnabled(S.Node==INDEX_NONE&&!S.RelationshipPending&&!S.CollectedActive).Text(FText::FromString(OKT(TEXT("ui.scene.let_her_pick_topic")))).OnClicked_Lambda([Weak=TWeakObjectPtr<AOKStreetSocialScene>(this)](){if(Weak.IsValid())Weak->AskHerTopic();return FReply::Handled();})];
  auto Bag=SNew(SVerticalBox);bool AnyStock=false;
  for(const auto& T:S.CollectedTopics.Topics){const int32 Count=S.Store.TopicStock.FindRef(T.No);if(Count<=0)continue;AnyStock=true;const int32 No=T.No;const FString Label=OKMsg(TEXT("ui.scene.item_entry"), {{TEXT("item_name"), *T.Name}, {TEXT("category"), T.Category==4?OKT(TEXT("ui.scene.erotic_label")):OKT(TEXT("ui.scene.normal_category"))}, {TEXT("rarity"), T.Rarity}, {TEXT("count"), Count}});Bag->AddSlot().AutoHeight().Padding(2)[SNew(SButton).ButtonStyle(OKStreetGlassButton()).IsEnabled(S.Node==INDEX_NONE&&!S.RelationshipPending&&(!S.CollectedActive||S.AwaitTopicAnswer)).Text(FText::FromString(Label)).OnClicked_Lambda([Weak=TWeakObjectPtr<AOKStreetSocialScene>(this),No](){if(Weak.IsValid())Weak->UseCollectedTopic(No);return FReply::Handled();})];}
  if(S.AwaitTopicAnswer)Bag->AddSlot().AutoHeight()[SNew(SButton).ButtonStyle(OKStreetGlassButton()).Text(FText::FromString(OKT(TEXT("ui.scene.vague_topic_reply")))).OnClicked_Lambda([Weak=TWeakObjectPtr<AOKStreetSocialScene>(this)](){if(Weak.IsValid())Weak->UseCollectedTopic(INDEX_NONE);return FReply::Handled();})];
  if(AnyStock||S.AwaitTopicAnswer)Box->AddSlot().AutoHeight()[SNew(SBox).MaxDesiredHeight(150)[SNew(SScrollBox)+SScrollBox::Slot()[Bag]]];
  }
  if(OKStreetViewRelation(S.Store,OKStreetStableId(S.Manifest.Characters[S.Target]),S.Manifest.TalkBudget).Stage!=TEXT("lover")){
  auto Events=SNew(SVerticalBox);for(const FString Event:{FString(TEXT("friendship")),FString(TEXT("confession"))})Events->AddSlot().AutoHeight().Padding(4)[SNew(SButton).ButtonStyle(OKStreetGlassButton()).IsEnabled(S.Node==INDEX_NONE&&!S.RelationshipPending&&!S.CollectedActive).Text(FText::FromString(Event==TEXT("friendship")?OKT(TEXT("ui.scene.friendship_progress")):OKT(TEXT("ui.scene.confess_button")))).OnClicked_Lambda([Weak=TWeakObjectPtr<AOKStreetSocialScene>(this),Event](){if(Weak.IsValid())Weak->RelationshipEvent(Event);return FReply::Handled();})];Box->AddSlot().AutoHeight()[Events];
  }

 }
 Mark(TEXT("freetalk"));
 // Free talk follows its partner: the conversation target, or the girl walking or doing an activity with the player.
 if(FreeTalkPartner()!=INDEX_NONE){
  Box->AddSlot().AutoHeight().Padding(4)[SNew(SButton).ButtonStyle(OKStreetGlassButton()).Text(FText::FromString(S.FreeTalkOpen?OKT(TEXT("ui.scene.collapse_free_talk")):OKT(TEXT("ui.scene.free_talk_label")))).OnClicked_Lambda([Weak=TWeakObjectPtr<AOKStreetSocialScene>(this)](){if(Weak.IsValid()){auto& W=*Weak->State;W.FreeTalkOpen=!W.FreeTalkOpen;W.FreeTalkUserClosed=!W.FreeTalkOpen;Weak->FreeTalkLiveSet(W.FreeTalkOpen);Weak->RefreshUI();}return FReply::Handled();})];
  if(S.FreeTalkOpen){
   auto FreeTalk=SNew(SVerticalBox);
   // Bound to state: replay and favor updates change it without a rebuild (which would drop typing focus).
   FreeTalk->AddSlot().AutoHeight().Padding(2)[SNew(STextBlock).AutoWrapText(true).WrapTextAt(360).ColorAndOpacity(FLinearColor(1,.83f,.35f)).Text_Lambda([Weak=TWeakPtr<FOKStreetSceneState>(State)](){const auto L=Weak.Pin();if(!L)return FText::GetEmpty();
    if(!L->FreeTalkNotice.IsEmpty())return FText::FromString(L->FreeTalkNotice);
    return L->FreeTalkCaptionHer.IsEmpty()?FText::FromString(OKT(TEXT("ui.scene.free_talk_hint"))):FText::GetEmpty();})];
   FreeTalk->AddSlot().AutoHeight().Padding(2)[SNew(SEditableTextBox).Text(FText::FromString(S.FreeTalkDraft)).HintText(FText::FromString(OKT(TEXT("ui.scene.chat_input_hint")))).OnTextChanged_Lambda([Weak=TWeakObjectPtr<AOKStreetSocialScene>(this)](const FText& Text){if(Weak.IsValid()&&Weak->State){Weak->State->FreeTalkDraft=Text.ToString();Weak->State->SystemNote=false;}}).OnTextCommitted_Lambda([Weak=TWeakObjectPtr<AOKStreetSocialScene>(this)](const FText&,ETextCommit::Type Kind){if(Kind==ETextCommit::OnEnter&&Weak.IsValid())Weak->SendFreeTalk();})];
   // Opening free talk is the live conversation: the microphone listens by itself, no further button.
   FreeTalk->AddSlot().AutoHeight().Padding(2)[SNew(STextBlock).AutoWrapText(true).WrapTextAt(360).ColorAndOpacity(FLinearColor(.55f,1.f,.7f))
    .Visibility_Lambda([Weak=TWeakObjectPtr<AOKStreetSocialScene>(this)](){return Weak.IsValid()&&Weak->State->FreeTalkLive?EVisibility::Visible:EVisibility::Collapsed;})
    .Text_Lambda([Weak=TWeakObjectPtr<AOKStreetSocialScene>(this)](){return Weak.IsValid()?FText::FromString(Weak->FreeTalkLiveStatus()):FText::GetEmpty();})];
   // Always visible: what the microphone hears right now (green while speech is detected).
   FreeTalk->AddSlot().AutoHeight().Padding(8,4,2,2)[SNew(SProgressBar).Percent_Lambda([Weak=TWeakObjectPtr<AOKStreetSocialScene>(this)](){if(!Weak.IsValid())return TOptional<float>(0.f);const float Level=Weak->State->FreeTalkMicLevel;return TOptional<float>(FMath::Clamp((20.f*FMath::LogX(10.f,FMath::Max(Level,1e-5f))+60.f)/60.f,0.f,1.f));})
    .FillColorAndOpacity_Lambda([Weak=TWeakObjectPtr<AOKStreetSocialScene>(this)](){return Weak.IsValid()&&Weak->State->FreeTalkLiveSpeaking?FSlateColor(FLinearColor(.3f,1.f,.45f)):FSlateColor(FLinearColor(.4f,.7f,1.f));})];
   FreeTalk->AddSlot().AutoHeight().Padding(8,0,2,2)[SNew(STextBlock).AutoWrapText(true).WrapTextAt(350).Text_Lambda([Weak=TWeakObjectPtr<AOKStreetSocialScene>(this)](){if(!Weak.IsValid())return FText::GetEmpty();const auto& L=*Weak->State;
    const float Db=20.f*FMath::LogX(10.f,FMath::Max(L.FreeTalkMicLevel,1e-5f));
    return FText::FromString(OKMsg(TEXT("ui.scene.mic_volume_level"), {{TEXT("db"), Db}, {TEXT("speaking_suffix"), L.FreeTalkLiveSpeaking?OKT(TEXT("ui.scene.speech_detected")):TEXT("")}}));})];
    // Microphone settings: pick the input ok-asr records from and watch its level (speech detection shows in green).
    FreeTalk->AddSlot().AutoHeight().Padding(2)[SNew(SButton).ButtonStyle(OKStreetGlassButton()).Text(FText::FromString(S.FreeTalkMicOpen?OKT(TEXT("ui.scene.collapse_mic_settings")):OKT(TEXT("ui.scene.mic_settings")))).OnClicked_Lambda([Weak=TWeakObjectPtr<AOKStreetSocialScene>(this)](){if(Weak.IsValid())Weak->FreeTalkMicToggle();return FReply::Handled();})];
    if(S.FreeTalkMicOpen){
     for(const FString& Mic:S.FreeTalkMics)FreeTalk->AddSlot().AutoHeight().Padding(8,1,2,1)[SNew(SButton).ButtonStyle(OKStreetGlassButton()).Text(FText::FromString((Mic==S.FreeTalkMic?TEXT("▶ "):OKT(TEXT("ui.scene.ideographic_space")))+Mic)).OnClicked_Lambda([Weak=TWeakObjectPtr<AOKStreetSocialScene>(this),Mic](){if(Weak.IsValid())Weak->FreeTalkMicSelect(Mic);return FReply::Handled();})];
     if(S.FreeTalkMics.IsEmpty())FreeTalk->AddSlot().AutoHeight().Padding(8,2)[SNew(STextBlock).Text(FText::FromString(OKT(TEXT("ui.scene.reading_mic_list"))))];
    }
   FreeTalk->AddSlot().AutoHeight().Padding(2)[SNew(SButton).ButtonStyle(OKStreetGlassButton()).IsEnabled(!S.FreeTalkBusy).Text(FText::FromString(S.FreeTalkBusy?OKT(TEXT("ui.scene.waiting_reply")):OKT(TEXT("ui.scene.send_button")))).OnClicked_Lambda([Weak=TWeakObjectPtr<AOKStreetSocialScene>(this)](){if(Weak.IsValid())Weak->SendFreeTalk();return FReply::Handled();})];
   FreeTalkEnsureLog();
   FreeTalk->AddSlot().AutoHeight().Padding(2)[SNew(SButton).ButtonStyle(OKStreetGlassButton()).IsEnabled(!S.FreeTalkBusy).Text(FText::FromString(!S.FreeTalkReplay.IsEmpty()?OKT(TEXT("ui.scene.stop_playback")):OKMsg(TEXT("ui.scene.replay_recent_count"), {{TEXT("count"), OKFreeTalkReplayCount}}))).OnClicked_Lambda([Weak=TWeakObjectPtr<AOKStreetSocialScene>(this)](){if(Weak.IsValid())Weak->FreeTalkReplayToggle();return FReply::Handled();})];
   Box->AddSlot().AutoHeight().Padding(4)[FreeTalk];
  }
 }
 // The shop rows: a region of their own (the menu changes as he walks into a shop or it closes).
 Box->AddSlot().AutoHeight()[SAssignNew(S.UiShopHolder,SBox)];
 Mark(TEXT("finance"));BuildFinanceRows(Box);Mark(TEXT("campus"));BuildCampusRows(Box);Mark(TEXT("phone"));BuildPhoneRow(Box);Mark(TEXT("company"));BuildCompanyRow(Box);Mark(TEXT("appointments"));BuildAppointmentRows(Box);
 Mark(TEXT("swap"));S.UiRightHolder->SetContent(Box);S.UiRightScroll->SetScrollOffset(S.RightScrollOffset);
 Regions|=OKStreetUi::Shop;
 }
 Mark(TEXT("shop"));
 if((Regions&OKStreetUi::Shop)&&S.UiShopHolder.IsValid()){auto Shop=SNew(SVerticalBox);BuildShopPanel(Shop);S.UiShopHolder->SetContent(Shop);}
 Mark(TEXT("debug"));
 if(Regions&OKStreetUi::Debug){
 S.DebugSliders=0;S.StatSliders.Reset();S.RelationChecks.Reset();
 auto DebugBox=SNew(SVerticalBox);
 const int32 DebugCharacter=S.Target!=INDEX_NONE?S.Target:S.ContactSelection;
 if(S.ShowDebug&&!(S.Manifest.Characters.IsValidIndex(DebugCharacter)&&DebugCharacter>0))
  DebugBox->AddSlot().AutoHeight().Padding(6)[SNew(STextBlock).AutoWrapText(true).Text(FText::FromString(OKT(TEXT("ui.scene.heroine_stats_empty_hint"))))];
 if(S.ShowDebug&&S.Manifest.Characters.IsValidIndex(DebugCharacter)&&DebugCharacter>0){
  DebugBox->AddSlot().AutoHeight().Padding(6)[SNew(STextBlock).Text(FText::FromString(OKT(TEXT("ui.scene.heroine_stats_test_label"))+S.Manifest.Characters[DebugCharacter].Name)).AutoWrapText(true).Font(FCoreStyle::GetDefaultFontStyle("Bold",15))];
  auto Relations=SNew(SHorizontalBox);
  const TCHAR* Stages[]={TEXT("stranger"),TEXT("friend"),TEXT("lover")};const TCHAR* Labels[]={OKT(TEXT("ui.minimap.stranger_label")),OKT(TEXT("ui.bonds.friend_stage")),OKT(TEXT("ui.bonds.lover_stage"))};
  for(int32 I=0;I<3;++I){const FString Stage=Stages[I];TSharedPtr<SCheckBox> Check;
   Relations->AddSlot().FillWidth(1).Padding(6,4)[SAssignNew(Check,SCheckBox)
    .IsChecked_Lambda([Weak=TWeakPtr<FOKStreetSceneState>(State),DebugCharacter,Stage](){auto L=Weak.Pin();return L&&OKStreetViewRelation(L->Store,OKStreetStableId(L->Manifest.Characters[DebugCharacter]),L->Manifest.TalkBudget).Stage==Stage?ECheckBoxState::Checked:ECheckBoxState::Unchecked;})
    .OnCheckStateChanged_Lambda([Weak=TWeakObjectPtr<AOKStreetSocialScene>(this),DebugCharacter,Stage](ECheckBoxState Value){if(Weak.IsValid()&&Value==ECheckBoxState::Checked)Weak->SetCharacterRelationship(DebugCharacter,Stage);})
    [SNew(STextBlock).Text(FText::FromString(Labels[I]))]];S.RelationChecks.Add(Check);
  }
  DebugBox->AddSlot().AutoHeight()[Relations];
  // What she feels for the other heroines, and her orientation (lovers between heroines need both bisexual; Bonds.inl).
  DebugBox->AddSlot().AutoHeight().Padding(6,2)[SNew(STextBlock).AutoWrapText(true).Text_Lambda([Weak=TWeakObjectPtr<AOKStreetSocialScene>(this),DebugCharacter](){return FText::FromString(Weak.IsValid()?Weak->BondPanelText(DebugCharacter):FString());})];
  DebugBox->AddSlot().AutoHeight().HAlign(HAlign_Left).Padding(6,2)[SNew(SButton).ButtonStyle(OKStreetGlassButton()).Text(FText::FromString(OrientationOf(DebugCharacter)==TEXT("bisexual")?OKT(TEXT("ui.scene.orientation_bi_toggle")):OKT(TEXT("ui.scene.orientation_hetero_toggle"))))
   .OnClicked_Lambda([Weak=TWeakObjectPtr<AOKStreetSocialScene>(this),DebugCharacter](){if(Weak.IsValid())Weak->SetOrientation(DebugCharacter,Weak->OrientationOf(DebugCharacter)==TEXT("bisexual")?TEXT("straight"):TEXT("bisexual"));return FReply::Handled();})];
  // Plan §10.3: the exact admiration and jealousy numbers, every axis, and what a tsundere heroine shows.
  DebugBox->AddSlot().AutoHeight().Padding(6,2)[SNew(STextBlock).AutoWrapText(true).Text_Lambda([Weak=TWeakObjectPtr<AOKStreetSocialScene>(this),DebugCharacter](){return FText::FromString(Weak.IsValid()?Weak->MindPanelText(DebugCharacter):FString());})];
  const auto& Stats=OKHeroineStats();
  for(int32 Field=0;Field<Stats.Num();++Field){++S.DebugSliders;const float Limit=Stats[Field].Max;const FString Label=OKT(Stats[Field].Label);
   auto Read=[Weak=TWeakObjectPtr<AOKStreetSocialScene>(this),DebugCharacter,Field](){if(!Weak.IsValid())return 0.f;const auto& L=*Weak->State;const auto R=OKStreetViewRelation(L.Store,OKStreetStableId(L.Manifest.Characters[DebugCharacter]),L.Manifest.TalkBudget);
    return Field==5?Weak->ChastityOf(DebugCharacter):OKHeroineStats()[Field].Get(R);}; // chastity not set yet: what her personality gives
   DebugBox->AddSlot().AutoHeight().Padding(6,2)[SNew(STextBlock).Text_Lambda([Read,Label](){return FText::FromString(FString::Printf(TEXT("%s  %.0f"),*Label,Read()));})];
   TSharedPtr<SSlider> Slider;
   DebugBox->AddSlot().AutoHeight().Padding(8,1)[SAssignNew(Slider,SSlider).Value_Lambda([Read,Limit](){return Read()/Limit;}).OnValueChanged_Lambda([Weak=TWeakObjectPtr<AOKStreetSocialScene>(this),DebugCharacter,Field,Limit](float Value){if(Weak.IsValid())Weak->SetCharacterStat(DebugCharacter,Field,Value*Limit);})];
   S.StatSliders.Add(Slider);
  }

 }
 S.UiDebugHolder->SetContent(DebugBox);
 }
 Mark(TEXT("dialogue"));
 if(Regions&OKStreetUi::Bottom){
 auto Dialogue=SNew(SVerticalBox);FreeTalkEnsureLog();
 // Free talk owns the bottom box while its captions are set and no scripted line plays. Captions are
 // bound to state so replay can change them every line without rebuilding the UI.
 // A galgame scene (OKStreetSocialGalgame.inl) shows the lines itself; this box then keeps only the table and what the microphone heard.
 auto Gal=[Owner=TWeakObjectPtr<AOKStreetSocialScene>(this)](){return Owner.IsValid()&&Owner->GalgameShowsDialogue();};
 auto FreeTalkOwns=[Gal,Weak=TWeakPtr<FOKStreetSceneState>(State)](){const auto L=Weak.Pin();return L&&!Gal()&&L->FreeTalkOpen&&L->Node==INDEX_NONE&&(!L->FreeTalkCaptionPlayer.IsEmpty()||!L->FreeTalkCaptionHer.IsEmpty());};
 auto Scripted=[Gal,FreeTalkOwns](){return Gal()||FreeTalkOwns()?EVisibility::Collapsed:EVisibility::Visible;};
 // Live free talk: the microphone level, and the words the recogniser heard (before and after they are sent).
 {
  auto LiveShown=[Gal,Weak=TWeakPtr<FOKStreetSceneState>(State)](){const auto L=Weak.Pin();return L&&!Gal()&&L->FreeTalkOpen&&L->FreeTalkLive?EVisibility::Visible:EVisibility::Collapsed;};
  Dialogue->AddSlot().AutoHeight().Padding(4,0,4,4)[SNew(SHorizontalBox).Visibility_Lambda(LiveShown)
   +SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)[SNew(SBox).HeightOverride(10)[SNew(SProgressBar).Percent_Lambda([Weak=TWeakPtr<FOKStreetSceneState>(State)](){const auto L=Weak.Pin();const float Level=L?L->FreeTalkMicLevel:0.f;return TOptional<float>(FMath::Clamp((20.f*FMath::LogX(10.f,FMath::Max(Level,1e-5f))+60.f)/60.f,0.f,1.f));})
    .FillColorAndOpacity_Lambda([Weak=TWeakPtr<FOKStreetSceneState>(State)](){const auto L=Weak.Pin();return L&&L->FreeTalkLiveSpeaking?FSlateColor(FLinearColor(.3f,1.f,.45f)):FSlateColor(FLinearColor(.4f,.7f,1.f));})]]
   +SHorizontalBox::Slot().AutoWidth().Padding(8,0,0,0).VAlign(VAlign_Center)[SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Regular",13)).ColorAndOpacity(FLinearColor(.75f,.75f,.75f))
    .Text_Lambda([Weak=TWeakPtr<FOKStreetSceneState>(State)](){const auto L=Weak.Pin();if(!L)return FText::GetEmpty();
     if(!L->FreeTalkLiveReady)return FText::FromString(OKT(TEXT("ui.scene.mic_connecting")));
     const float Db=20.f*FMath::LogX(10.f,FMath::Max(L->FreeTalkMicLevel,1e-5f));
     return FText::FromString(OKMsg(TEXT("ui.scene.mic_db_level"), {{TEXT("db"), Db}, {TEXT("speaking_suffix"), L->FreeTalkLiveSpeaking?OKT(TEXT("ui.scene.recording_now")):TEXT("")}}));})]];
  Dialogue->AddSlot().AutoHeight().Padding(4,0,4,4)[SNew(STextBlock).AutoWrapText(true).Font(FCoreStyle::GetDefaultFontStyle("Italic",16)).ColorAndOpacity(FLinearColor(.65f,.85f,1.f,.8f))
   .Visibility_Lambda([Weak=TWeakPtr<FOKStreetSceneState>(State)](){const auto L=Weak.Pin();return L&&L->FreeTalkOpen&&L->FreeTalkLive&&!L->FreeTalkHeard.IsEmpty()?EVisibility::Visible:EVisibility::Collapsed;})
   .Text_Lambda([Weak=TWeakPtr<FOKStreetSceneState>(State)](){const auto L=Weak.Pin();return L?FText::FromString(OKT(TEXT("ui.scene.heard_label"))+L->FreeTalkHeard):FText::GetEmpty();})];
 }
 // The table: every ordered dish and how much of it is left (100% while it is being prepared, when it cannot be eaten yet).
 Dialogue->AddSlot().AutoHeight().Padding(4,0,4,4)[SNew(STextBlock).AutoWrapText(true).Font(FCoreStyle::GetDefaultFontStyle("Regular",14)).ColorAndOpacity(FLinearColor(1.f,.9f,.6f))
  .Visibility_Lambda([Weak=TWeakPtr<FOKStreetSceneState>(State)](){const auto L=Weak.Pin();return L&&!L->Table.IsEmpty()?EVisibility::Visible:EVisibility::Collapsed;})
  .Text_Lambda([Owner=TWeakObjectPtr<AOKStreetSocialScene>(this)](){return Owner.IsValid()?FText::FromString(Owner->DishTableText()):FText::GetEmpty();})];
 Dialogue->AddSlot().AutoHeight().Padding(4)[SNew(STextBlock).AutoWrapText(true).Font(FCoreStyle::GetDefaultFontStyle("Regular",20)).ColorAndOpacity(FLinearColor(.65f,.85f,1.f))
  .Visibility_Lambda([FreeTalkOwns,Weak=TWeakPtr<FOKStreetSceneState>(State)](){const auto L=Weak.Pin();return L&&FreeTalkOwns()&&!L->FreeTalkCaptionPlayer.IsEmpty()?EVisibility::Visible:EVisibility::Collapsed;})
  .Text_Lambda([Weak=TWeakPtr<FOKStreetSceneState>(State)](){const auto L=Weak.Pin();return L?FText::FromString(L->FreeTalkCaptionPlayer):FText::GetEmpty();})];
 {TSharedPtr<SOKStreetSpeakerText> Caption;// each girl's lines in her minimap color
  Dialogue->AddSlot().AutoHeight().Padding(4)[SAssignNew(Caption,SOKStreetSpeakerText).FontSize(21)
  .Visibility_Lambda([FreeTalkOwns,Weak=TWeakPtr<FOKStreetSceneState>(State)](){const auto L=Weak.Pin();return L&&FreeTalkOwns()&&!L->FreeTalkCaptionHer.IsEmpty()?EVisibility::Visible:EVisibility::Collapsed;})];
  Caption->Text=[Weak=TWeakPtr<FOKStreetSceneState>(State)](){const auto L=Weak.Pin();return L?L->FreeTalkCaptionHer:FString();};
  Caption->Names=[Weak=TWeakPtr<FOKStreetSceneState>(State)](){const auto L=Weak.Pin();return L?OKStreetCharacterNames(*L):TArray<FString>();};}
 if(!S.PlayerLine.IsEmpty())Dialogue->AddSlot().AutoHeight().Padding(4)[SNew(STextBlock).Visibility_Lambda(Scripted).Text(FText::FromString(S.Manifest.Characters[0].Name+OKT(TEXT("ui.economy.colon_mark"))+S.PlayerLine)).AutoWrapText(true).Font(FCoreStyle::GetDefaultFontStyle("Regular",20)).ColorAndOpacity(FLinearColor(.65f,.85f,1.f))];
 OKStreetDigestMessage(S);
 Dialogue->AddSlot().AutoHeight().Padding(4)[SNew(STextBlock).Visibility_Lambda(Scripted).AutoWrapText(true).Font(FCoreStyle::GetDefaultFontStyle("Regular",21))
  .Text_Lambda([Weak=TWeakPtr<FOKStreetSceneState>(State)](){const auto L=Weak.Pin();return L?FText::FromString(L->DialogueText):FText::GetEmpty();})
  .ColorAndOpacity_Lambda([Weak=TWeakPtr<FOKStreetSceneState>(State)](){const auto L=Weak.Pin();return FSlateColor(L?L->DialogueColor:FLinearColor::White);})];
 S.UiBottomHolder->SetContent(Dialogue);
 S.UiBluffHolder->SetContent(S.Bluff.Active?BluffHandWidget():SNullWidget::NullWidget);
 }
 Mark(TEXT("history"));
 if(Regions&OKStreetUi::History){
 // Free-talk history window: every girl's lines, newest last, each with its voice when one was made.
 TSharedRef<SWidget> History=SNew(SBox).Visibility(EVisibility::Collapsed);
 if(S.FreeTalkHistoryOpen){
  FreeTalkEnsureLog();
  auto List=SNew(SVerticalBox);const TArray<FString> Names=OKStreetCharacterNames(S);
  for(const auto& Line:S.FreeTalkLog){
   const int64 Serial=Line.Serial;
   List->AddSlot().AutoHeight().Padding(2)[SNew(SHorizontalBox)
    +SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)[SNew(STextBlock).AutoWrapText(true).ColorAndOpacity(Line.Player?OKStreetPlayerLineColor():OKStreetSpeakerColor(OKStreetLineSpeaker(OKStreetFreeTalkCaption(Line),Names))).Text(FText::FromString(TEXT("[")+Line.Time+TEXT("] ")+OKStreetFreeTalkCaption(Line)))]
    +SHorizontalBox::Slot().AutoWidth().Padding(6,0,0,0)[SNew(SButton).ButtonStyle(OKStreetGlassButton()).Visibility(Line.Voice.IsEmpty()&&!GalgameReplays()?EVisibility::Hidden:EVisibility::Visible).Text(FText::FromString(OKT(TEXT("ui.scene.play_button")))).OnClicked_Lambda([Weak=TWeakObjectPtr<AOKStreetSocialScene>(this),Serial](){if(Weak.IsValid())Weak->FreeTalkPlayLine(Serial);return FReply::Handled();})]];
  }
  if(S.FreeTalkLog.IsEmpty())List->AddSlot().AutoHeight()[SNew(STextBlock).Text(FText::FromString(OKT(TEXT("ui.scene.no_freetalk_log_plain"))))];
  TSharedPtr<SScrollBox> Scroll;
  History=SNew(SBox).MaxDesiredWidth(1140).MinDesiredWidth(1140).MaxDesiredHeight(520)[SNew(SBorder).BorderImage(OKStreetGlassPanel()).Padding(14)[SNew(SVerticalBox)
   +SVerticalBox::Slot().AutoHeight().Padding(0,0,0,8)[SNew(SHorizontalBox)
    +SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)[SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Bold",17)).Text(FText::FromString(OKMsg(TEXT("ui.scene.freetalk_log_count"), {{TEXT("count"), S.FreeTalkLog.Num()}, {TEXT("limit"), OKFreeTalkLogLimit}})))]
    +SHorizontalBox::Slot().AutoWidth()[SNew(SButton).ButtonStyle(OKStreetGlassButton()).Text(FText::FromString(OKT(TEXT("ui.scene.close_button")))).OnClicked_Lambda([Weak=TWeakObjectPtr<AOKStreetSocialScene>(this)](){if(Weak.IsValid()){Weak->State->FreeTalkHistoryOpen=false;Weak->RefreshUI();}return FReply::Handled();})]]
   +SVerticalBox::Slot().FillHeight(1)[SAssignNew(Scroll,SScrollBox)+SScrollBox::Slot()[List]]]];
  Scroll->ScrollToEnd();
 }
 S.UiHistoryHolder->SetContent(History);
 }
 Mark(TEXT("apps"));
 if(Regions&OKStreetUi::Apps){
  S.UiAppHolder->SetContent(S.App.IsEmpty()?SNullWidget::NullWidget:BuildAppOverlay());
  S.UiSaveHolder->SetContent(S.SaveMenu.IsEmpty()?SNullWidget::NullWidget:BuildSaveMenu());
 }
 Mark(TEXT("end"));
 if(CVarOKStreetLogUiRebuilds.GetValueOnGameThread()&&FPlatformTime::ToMilliseconds64(Marks.Last().Value-Marks[0].Value)>5){FString Parts;for(int32 I=1;I<Marks.Num();++I)Parts+=FString::Printf(TEXT(" %s %.1f"),Marks[I-1].Key,FPlatformTime::ToMilliseconds64(Marks[I].Value-Marks[I-1].Value));UE_LOG(LogTemp,Display,TEXT("Street UI rebuild %.1f ms:%s"),FPlatformTime::ToMilliseconds64(Marks.Last().Value-Marks[0].Value),*Parts);}
}

#include "OKStreetSocialContacts.inl"
#include "OKStreetSocialSaves.inl"

void AOKStreetSocialScene::ResumeConversationPose(int32 Character,FString& Error){
 if(State&&State->PrivateGirl!=INDEX_NONE&&(Character==0||Character==State->PrivateGirl)){OKPlayCrowdRuntimeAction(this,Character,Character==0?TEXT("talk_gesture_03.okanim"):TEXT("talk_gesture_00.okanim"),Error);return;}
 if(State&&State->Pair.Conversation()&&State->Pair.Phase==1&&(Character==0||Character==State->Pair.Partner)){Error.Reset();return;}
 ResumeBehavior(Character,Error);
}
// Source phone prefab bounds (Unity x/z/y -> UE x/y/z), shared across APNGs.
static FTransform OKStreetPhonePartTransform(const FTransform& Hand,int32 Part){
 const FVector Size=Part==0?FVector(6.358756f,13.567981f,.703464f):Part==1?FVector(5.8f,11.4f,.06f):FVector(.7f,.7f,.08f);
 const FVector Offset=Part==0?FVector::ZeroVector:Part==1?FVector(0,0,-.39f):FVector(2,5,.4f);
 return FTransform(FRotator(0,0,180),Offset,Size/100.f)*Hand;
}
#include "OKStreetSocialPhone.inl"
#include "OKStreetSocialAmbient.inl"
#include "OKStreetSocialLiveTalk.inl"
// Defined in OKStreetSocialMind.inl; the earlier .inl files use it too.
static int64 OKStreetMindNow(const FOKStreetSceneState& S);
#include "OKStreetSocialBooks.inl"
#include "OKStreetSocialOuting.inl"
#include "OKStreetSocialElevator.inl"
#include "OKStreetSocialHeroine.inl"
#include "OKStreetSocialLover.inl"
#include "OKStreetSocialLife.inl"
#include "OKStreetSocialMind.inl"
#include "OKStreetSocialSong.inl"
#include "OKStreetSocialInspect.inl"
#include "OKStreetSocialEconomy.inl"
#include "OKStreetSocialInvite.inl"
#include "OKStreetSocialAppointments.inl"
#include "OKStreetSocialSchedule.inl"
#include "OKStreetSocialJobs.inl"
#include "OKStreetSocialEncounter.inl"
#include "OKStreetSocialParty.inl"
#include "OKStreetSocialBonds.inl"
#include "OKStreetSocialNeeds.inl"
#include "OKStreetSocialBluff.inl"
#include "OKStreetSocialDirector.inl"
#include "OKStreetSocialFinance.inl"
#include "OKStreetSocialFinanceApps.inl"
#include "OKStreetSocialGalgame.inl"
#include "OKStreetSocialPhotos.inl" // after the finance apps (their panel widgets), before the phone (its pictures)
#include "OKStreetSocialPhoneInbox.inl"
#include "OKStreetSocialCompanyWorld.inl"
#include "OKStreetSocialCompany.inl"
#include "OKStreetSocialHeroCompany.inl"
#include "OKStreetSocialCompanyApp.inl"
#include "OKStreetSocialAccountsApp.inl"
#include "OKStreetSocialVenture.inl"
#include "OKStreetSocialCampus.inl"
#include "OKStreetSocialBank.inl"
#include "OKStreetSocialInspector.inl"
#include "OKStreetSocialUiProbe.inl"
#include "OKStreetSocialControl.inl"
void AOKStreetSocialScene::BeginPhoto(int32 Character){BeginPair(Character,false);}
void AOKStreetSocialScene::BeginPair(int32 Character,int32 Kind){
 const bool PhoneUse=Kind==8,Watermelon=Kind==6||Kind==7,FemaleStrikes=Kind==7,Meal=Kind==5,Bath=Kind==9,Read=Kind==10,NoProps=Kind==11,Seated=Kind==1||Bath||NoProps,Drink=Kind==2,Splash=Kind==3||Kind==4,FemaleThrows=Kind==4;
 if(!State)return;auto& S=*State;
 if(S.PrivateGirl!=INDEX_NONE)return;
 if(S.Pair.Partner!=INDEX_NONE||S.Companion!=INDEX_NONE||S.Facility!=INDEX_NONE||!S.Agents.IsValidIndex(Character-1)||!S.Manifest.Characters.IsValidIndex(Character)||OKStreetConversationBusy(S.Node!=INDEX_NONE,S.CollectedActive,S.RelationshipPending,S.TouchActive))return;
 // One swing for both. (The other take of the original strike, action38 Leisure9, was tried for her and removed: with
 // this contact recipe its bat never reached the melon in the watermelon self-test.)
 const FString Strike=TEXT("watermelon_strike.okanim"),Observe=TEXT("watermelon_observe.okanim");const FString PhoneClip=TEXT("phone_standing.okanim"),MealClip=TEXT("facility_eat.okanim");const FString ReadClip=TEXT("idle_02.okanim");const FString* Female=Read?&ReadClip:PhoneUse?&PhoneClip:Watermelon?(FemaleStrikes?&Strike:&Observe):Meal?&MealClip:Splash?(FemaleThrows?&S.SplashFemaleThrow:&S.SplashFemaleReceive):Drink?&S.DrinkFemaleClip:Seated?&S.SeatedFemaleClip:S.PhotoPoses.Find(S.Manifest.Characters[Character].PersonalityId);FString MaleClip=Read?ReadClip:PhoneUse?PhoneClip:Watermelon?(FemaleStrikes?Observe:Strike):Meal?MealClip:Splash?(FemaleThrows?S.SplashMaleReceive:S.SplashMaleThrow):Drink?S.DrinkMaleClip:Seated?S.SeatedMaleClip:S.PhotoMaleClip;// A place can ask for its own curves (venue.manifest.json), e.g. reading or drinking on the seated benches.
 FString FemaleOverride=S.NextPairFemaleClip;const FString MaleOverride=S.NextPairMaleClip;S.NextPairFemaleClip.Reset();S.NextPairMaleClip.Reset();
 if(!FemaleOverride.IsEmpty())Female=&FemaleOverride;if(!MaleOverride.IsEmpty())MaleClip=MaleOverride;
 // A photo: one of the game's nine two-shot poses (motions.toml, photo), the one she asked for, or the next one for
 // "another pose"; the three by personality were all there was.
 FString PhotoFemale;S.PhotoPoseKey.Reset();
 if(!Read&&!PhoneUse&&!Watermelon&&!Meal&&!Splash&&!Drink&&!Seated&&FemaleOverride.IsEmpty()){
  auto Ask=OKRulesRequest(TEXT("motion_photo"));Ask->SetBoolField(TEXT("lover"),ContactLover(Character));Ask->SetStringField(TEXT("pose"),S.NextPhotoPose);Ask->SetNumberField(TEXT("seed"),S.Random.RandRange(0,1<<30));S.NextPhotoPose.Reset();
  if(const TSharedPtr<FJsonObject> Pose=OKRulesEval(S,Ask)){PhotoFemale=Pose->GetStringField(TEXT("female"));const FString Him=Pose->GetStringField(TEXT("male"));
   if(!PhotoFemale.IsEmpty()){Female=&PhotoFemale;S.PhotoPoseKey=Pose->GetStringField(TEXT("key"));if(!Him.IsEmpty())MaleClip=Him;}}
 }
 if(!Female||Female->IsEmpty()||MaleClip.IsEmpty())return;
 FOKRuntimeCharacterStatus Male,Npc;if(!OKGetCrowdRuntimeCharacterStatus(this,0,Male)||!OKGetCrowdRuntimeCharacterStatus(this,Character,Npc)||!Male.Visible||!Npc.Visible||!Male.Error.IsEmpty()||!Npc.Error.IsEmpty()||FVector::Dist2D(S.Male,S.Agents[Character-1].Position)>250)return;
 StopAmbientActivity();EndTalk();auto& P=S.Pair;P=FOKStreetPairSession();P.Partner=Character;P.PhoneUse=PhoneUse;P.Watermelon=Watermelon;P.FemaleStrikes=FemaleStrikes;P.Meal=Meal;P.Seated=Seated;P.Bath=Bath;P.Read=Read;P.NoProps=NoProps;P.Yaw=S.NextPairYaw;S.NextPairYaw=0;P.Drink=Drink;P.Splash=Splash;P.FemaleThrows=FemaleThrows;P.MaleClip=MaleClip;P.FemaleClip=*Female;P.SavedAgent=S.Agents[Character-1];P.SavedMale=S.Male;P.SavedMaleRotation=S.Rotations[0];P.SavedFemaleRotation=S.Rotations[Character];P.SavedYaw=S.Yaw;P.SavedPitch=S.Pitch;P.SavedDistance=S.Distance;P.SavedMouseUI=S.MouseUI;P.RequestedAt=SceneTime();
 S.Agents[Character-1].Held=true;
 if(P.Conversation())MakeRoomAround(Character);
 FString Error;
 // The swing was authored for a body whose shoulder stands 125.3 cm high; a shorter striker cannot reach a melon
 // 80 cm out (her stick stops ~11 cm short). Bring it closer in proportion to her shoulder height, never farther.
 if(Watermelon){const int32 Striker=FemaleStrikes?Character:0;if(!S.MelonForward.Contains(Striker)){float Reach=1;FTransform Shoulder;const FVector Root=Striker==0?S.Male:S.Agents[Character-1].Position;
  if(OKGetCrowdRuntimeBoneTransform(this,Striker,TEXT("cf_j_shoulder_R"),Shoulder)&&!Shoulder.ContainsNaN())Reach=FMath::Clamp(float(Shoulder.GetLocation().Z-Root.Z)/125.3f,.8f,1.f);
  S.MelonForward.Add(Striker,FMath::RoundToFloat(80*Reach));}P.MelonForward=S.MelonForward[Striker];}
 const FString MelonContact=FString(TEXT(R"CONTACT({"start":0.28,"peak":0.52,"end":0.78,"constraints":[{"effector":"k_f_handR_00","tip":[0,0,0.5099748],"target":[0,0.12832,{MELON}],"chain":[{"name":"cf_j_hand_R","max_degrees":90},{"name":"cf_j_forearm01_R","max_degrees":150},{"name":"cf_j_arm00_R","max_degrees":150},{"name":"cf_j_shoulder_R","max_degrees":90},{"name":"cf_j_spine03","max_degrees":45},{"name":"cf_j_spine02","max_degrees":45},{"name":"cf_j_spine01","max_degrees":45}]},{"effector":"k_f_handL_00","tip":[0,0,0],"target":[0,0,0.04],"target_bone":"k_f_handR_00","chain":[{"name":"cf_j_forearm01_L","max_degrees":150},{"name":"cf_j_arm00_L","max_degrees":150},{"name":"cf_j_shoulder_L","max_degrees":90}]}]})CONTACT")).Replace(TEXT("{MELON}"),*FString::Printf(TEXT("%g"),P.MelonForward/100.f));
 if(Watermelon&&!OKConfigureCrowdRuntimeActionContact(this,FemaleStrikes?Character:0,Strike,MelonContact,Error)){FinishPhoto();S.Message=OKT(TEXT("ui.scene.watermelon_calib_fail"));RefreshUI();return;}
 if(!OKPrewarmCrowdRuntimeAction(this,0,P.MaleClip,Error)||!OKPrewarmCrowdRuntimeAction(this,Character,P.FemaleClip,Error)){FinishPhoto();S.Message=OKT(TEXT("ui.scene.dual_activity_prep_failed"));RefreshUI();return;}
 S.Message=PhoneUse?OKT(TEXT("ui.scene.prep_phone_hint")):Watermelon?OKT(TEXT("ui.scene.prep_watermelon_hint")):Meal?OKT(TEXT("ui.scene.prep_eating_hint")):Splash?OKT(TEXT("ui.scene.prep_splash_hint")):Drink?OKT(TEXT("ui.scene.prep_drink_hint")):Bath?OKT(TEXT("ui.scene.prep_hotspring_hint")):Read?OKT(TEXT("ui.scene.prep_reading_hint")):Seated?OKT(TEXT("ui.scene.prep_sit_pose_hint")):OKT(TEXT("ui.scene.prep_photo_pose_hint"));RefreshUI();
}
void AOKStreetSocialScene::CapturePhoto(){
 if(!State)return;auto& S=*State;auto& P=S.Pair;if(P.Partner==INDEX_NONE||(P.Conversation()||P.Splash||P.Watermelon)||P.Phase!=1||P.CapturePending||FScreenshotRequest::IsScreenshotRequested())return;
 const FString Directory=(S.Manifest.SavePath.IsEmpty()?FPaths::ProjectSavedDir()/TEXT("StreetSocial"):FPaths::GetPath(S.Manifest.SavePath))/TEXT("Photos");IFileManager::Get().MakeDirectory(*Directory,true);
 P.CapturePath=Directory/(TEXT("Photo-")+FDateTime::UtcNow().ToString(TEXT("%Y%m%d-%H%M%S"))+TEXT("-")+FGuid::NewGuid().ToString(EGuidFormats::Digits)+TEXT(".png"));P.CaptureAt=SceneTime();P.CapturePending=true;
 FScreenshotRequest::RequestScreenshot(P.CapturePath,false,false);S.Message=OKT(TEXT("ui.scene.saving_photo"));RefreshUI();
}
void AOKStreetSocialScene::FinishPhoto(bool bKeepTalking){
 if(!State||State->Pair.Partner==INDEX_NONE)return;auto& S=*State;auto& P=S.Pair;
 if(P.CapturePending&&SceneTime()-P.CaptureAt<3){P.FinishRequested=true;return;}
 // A meal or seat ended within half a minute of being asked for (the dessert at the snack shop ended at once and the
 // clock skipped its fifty minutes): who ended it goes to the log.
 if(P.Conversation()&&SceneTime()-P.RequestedAt<30){ANSICHAR Stack[2048]={};FPlatformStackWalk::StackWalkAndDump(Stack,sizeof(Stack),1);
  UE_LOG(LogTemp,Warning,TEXT("Street pair: ended early (phase %d, %.1f s after it was asked for):\n%hs"),P.Phase,SceneTime()-P.RequestedAt,Stack);}
 const bool WasTalking=S.FreeTalkOpen&&FreeTalkPartner()==P.Partner;
 // Sitting over a meal, a drink or a talk ended (goodbye, standing up, their words): it lasted the minutes that went by.
 // Billed as before, but the clock is not put on to its whole length: seven minutes at the cafeteria with 三月七, his
 // dish already eaten, ended at his goodbye and the clock jumped 43 minutes (R6-1).
 if(P.Conversation()&&S.ActGirl==P.Partner)S.ActTimeCut=true;
 const auto Before=P;P=FOKStreetPairSession();if(Before.Conversation())EndTalk();
 for(auto& Prop:S.PairProps)DestroyLater(Prop.Get());S.PairProps.Reset();S.MealHandProps.Reset();
 const auto& Restore=Before;FString Error;ResumeBehavior(0,Error);ResumeBehavior(Restore.Partner,Error);
 S.PairRestoredFrame=GFrameCounter;S.PairRestoredActor=Restore.Partner;
 {auto& Agent=S.Agents[Restore.Partner-1];Agent.Held=false;Agent.Mode=EOKStreetMove::Walking;Agent.Stuck=0;}S.Yaw=Restore.SavedYaw;S.Pitch=Restore.SavedPitch;S.Distance=Restore.SavedDistance;S.MouseUI=Restore.SavedMouseUI;ApplyMouseMode(S.ViewportFocused);
 P=FOKStreetPairSession();S.Message=Before.PhoneUse?OKT(TEXT("ui.scene.phone_activity_ended")):Before.Watermelon?OKT(TEXT("ui.scene.watermelon_ended")):Before.Meal?OKT(TEXT("ui.scene.meal_ended")):Before.Splash?OKT(TEXT("ui.scene.splash_ended")):Before.Drink?OKT(TEXT("ui.scene.drink_ended")):Before.Bath?OKT(TEXT("ui.scene.hotspring_ended")):Before.Read?OKT(TEXT("ui.scene.reading_ended")):Before.Seated?OKT(TEXT("ui.scene.sit_chat_ended")):OKT(TEXT("ui.scene.photo_ended"));RefreshUI();ResumeTalkQuiet(Restore.Partner);
 // The meal (or drink, or sitting together) is over, the conversation is not: it goes on where it was, without a new greeting.
 if(bKeepTalking&&WasTalking&&Before.Conversation()){S.QuietTalk=true;if(BeginTalk(Before.Partner)){S.FreeTalkOpen=true;FreeTalkLiveSet(true);UE_LOG(LogTemp,Display,TEXT("Street pair: activity over, the talk with actor %d goes on"),Before.Partner);}S.QuietTalk=false;}
}
void AOKStreetSocialScene::TickPhoto(double Now){
 auto& S=*State;auto& P=S.Pair;if(P.Partner==INDEX_NONE)return;
 FOKRuntimeCharacterStatus Male,Npc;FOKRuntimeActionStreamStatus A,B;
 if(!OKGetCrowdRuntimeCharacterStatus(this,0,Male)||!OKGetCrowdRuntimeCharacterStatus(this,P.Partner,Npc)||!Male.Visible||!Npc.Visible||!Male.Error.IsEmpty()||!Npc.Error.IsEmpty()||!OKGetCrowdRuntimeActionStatus(this,0,A)||!OKGetCrowdRuntimeActionStatus(this,P.Partner,B)){UE_LOG(LogTemp,Display,TEXT("Street pair: ended at Scene.cpp:%d (phase %d)"),__LINE__,S.Pair.Phase);FinishPhoto();return;}
 if(P.Phase==0){
  if(Now-P.RequestedAt>60){UE_LOG(LogTemp,Display,TEXT("Street pair: ended at Scene.cpp:%d (phase %d)"),__LINE__,S.Pair.Phase);FinishPhoto();S.Message=OKT(TEXT("ui.scene.dual_activity_prep_timeout"));RefreshUI();return;}
  // Turn into the authored paired orientation before starting the clips / props.
  // Otherwise a rate-limited actor would sit sideways through a table or chair.
  const FRotator PairFacing(0,P.Yaw,0);
  // Reading on foot, they stand face to face.
  S.Rotations[P.Partner]=PairFacing+FRotator(0,(P.Conversation()||P.Splash||P.Read)?180.f:0.f,0);
  // What the minute to get ready is still waiting on, every 5 s (a park bench never got her seated clip: R3-220).
  {static double LastWait=0;if(Now-LastWait>5.0){LastWait=Now;UE_LOG(LogTemp,Display,TEXT("Street pair: getting ready %.0fs: turn %.1f, male %s ready=%d pending=%d preparing=%d (want %s), female %s ready=%d pending=%d preparing=%d (want %s)"),Now-P.RequestedAt,
   FMath::Abs(FMath::FindDeltaAngleDegrees(S.RenderedRotations[P.Partner].Yaw,S.Rotations[P.Partner].Yaw)),*A.ClipId,A.Ready?1:0,A.Pending?1:0,A.Preparing?1:0,*P.MaleClip,*B.ClipId,B.Ready?1:0,B.Pending?1:0,B.Preparing?1:0,*P.FemaleClip);}}
  // The clips were asked for once when the activity began; something else that played on her (or his) action stream in
  // the meantime (her song, a life pose) left another clip there and nobody asked again, so it waited the whole minute
  // (R3-220). A stream holding another clip and not getting one ready asks for this one again (at most every 2 s).
  {static double Reasked=0;if(Now-Reasked>2.0)for(int32 K=0;K<2;++K){const auto& St=K==0?A:B;const FString& Want=K==0?P.MaleClip:P.FemaleClip;
   if(St.ClipId!=Want&&!St.Pending&&!St.Preparing){Reasked=Now;FString Error;const bool Ok=OKPrewarmCrowdRuntimeAction(this,K==0?0:P.Partner,Want,Error);
    UE_LOG(LogTemp,Display,TEXT("Street pair: actor %d holds %s, not %s: asked again (%s)"),K==0?0:P.Partner,*St.ClipId,*Want,Ok?TEXT("ok"):*Error);}}}
  if(FMath::Abs(FMath::FindDeltaAngleDegrees(S.RenderedRotations[P.Partner].Yaw,S.Rotations[P.Partner].Yaw))>.1f)return;
  if(!A.Ready||!B.Ready||A.Pending||B.Pending||A.Preparing||B.Preparing||A.ClipId!=P.MaleClip||B.ClipId!=P.FemaleClip)return;
  // Original TwoShot starts both actors at the same world anchor. Their own curves carry the offset.
  // Everything below is laid out for +X and turned by the pair yaw (a place anchor can face any way).
  S.Rotations[0]=PairFacing;S.Rotations[P.Partner]=PairFacing+FRotator(0,(P.Conversation()||P.Splash||P.Read)?180.f:0.f,0);S.Agents[P.Partner-1].Position=S.Male+PairFacing.RotateVector(P.Read?FVector(85,0,0):P.Splash?FVector(0,160,0):P.PhoneUse?FVector(0,150,0):P.Drink?FVector(0,150,0):P.Watermelon?FVector(120,0,0):P.Meal?FVector(0,170,0):P.Seated?FVector(0,155,0):FVector::ZeroVector);if(P.Bath&&P.FemaleClip.Contains(TEXT("venue_onsen"))){float Lift=OKStreetOnsenLift;FParse::Value(FCommandLine::Get(),TEXT("StreetSocialOnsenLift="),Lift);S.Agents[P.Partner-1].Position.Z+=Lift;}
  // In the hot spring they sit in the water itself: no table or benches.
  if((P.Seated&&!P.Bath&&!P.NoProps)||P.Meal){
   auto* Cube=LoadObject<UStaticMesh>(nullptr,TEXT("/Game/Scenes/StreetSocial/Meshes/StreetCube.StreetCube"));
   if(!Cube){UE_LOG(LogTemp,Display,TEXT("Street pair: ended at Scene.cpp:%d (phase %d)"),__LINE__,S.Pair.Phase);FinishPhoto();return;}
   auto Part=[&](FVector Position,FVector Size,int32 Material=8){auto* A=GetWorld()->SpawnActor<AStaticMeshActor>(S.Male+PairFacing.RotateVector(Position),PairFacing);if(!A)return;A->SetOwner(this);A->SetActorScale3D(Size/100.f);auto* C=A->GetStaticMeshComponent();C->SetMobility(EComponentMobility::Movable);C->SetStaticMesh(Cube);C->SetCollisionEnabled(ECollisionEnabled::NoCollision);C->SetLightingChannels(true,true,false);const FString MaterialPath=FString::Printf(TEXT("/Game/Scenes/StreetSocial/Materials/StreetMaterial%02d.StreetMaterial%02d"),Material,Material);if(auto* M=LoadObject<UMaterialInterface>(nullptr,*MaterialPath))C->SetMaterial(0,M);S.PairProps.Add(A);};
   for(float Y:{-12.f,P.Meal?182.f:167.f}){Part(FVector(0,Y,40),FVector(56,48,7));for(float X:{-22.f,22.f})for(float D:{-18.f,18.f})Part(FVector(X,Y+D,19),FVector(6,6,38));}
   Part(FVector(0,P.Meal?85.f:77.f,P.Meal?69.f:62.f),FVector(80,P.Meal?100.f:48.f,4));for(float X:{-32.f,32.f})for(float Y:{P.Meal?45.f:60.f,P.Meal?125.f:94.f})Part(FVector(X,Y,30),FVector(6,6,60));
   // Tinted shapes (the engine's BasicShapeMaterial takes any colour), so every meal and book looks different.
   UMaterialInterface* Tint=LoadObject<UMaterialInterface>(nullptr,TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
   auto Srgb=[](uint8 R,uint8 G,uint8 B){return FLinearColor(FColor(R,G,B));};
   auto Shape=[&](const TCHAR* Mesh,FVector Position,FVector Size,FLinearColor Colour,float Yaw=0.f,float Roll=0.f){
    const FString Path=FString::Printf(TEXT("/Game/Scenes/StreetSocial/Meshes/%s.%s"),Mesh,Mesh);auto* M=LoadObject<UStaticMesh>(nullptr,*Path);if(!M)return;
    auto* A=GetWorld()->SpawnActor<AStaticMeshActor>(S.Male+PairFacing.RotateVector(Position),PairFacing+FRotator(0,Yaw,Roll));if(!A)return;A->SetOwner(this);A->SetActorScale3D(Size/100.f);
    auto* C=A->GetStaticMeshComponent();C->SetMobility(EComponentMobility::Movable);C->SetStaticMesh(M);C->SetCollisionEnabled(ECollisionEnabled::NoCollision);C->SetCastShadow(false);C->SetLightingChannels(true,true,false); // the indoor fill lights too
    if(Tint){auto* D=UMaterialInstanceDynamic::Create(Tint,A);D->SetVectorParameterValue(TEXT("Color"),Colour);C->SetMaterial(0,D);}S.PairProps.Add(A);};
   // One dish on a plate whose top is at Z (table frame: X across the table, Y from him to her).
   auto Dish=[&](int32 Kind,float Y,float Z){
    switch(Kind){
    case 0: // curry rice
     Shape(TEXT("StreetSphere"),FVector(-3,Y-3,Z),FVector(15,10,5),Srgb(250,248,240));Shape(TEXT("StreetSphere"),FVector(3,Y+3,Z),FVector(13,11,3.5f),Srgb(150,90,30));
     Shape(TEXT("StreetCube"),FVector(4,Y+3,Z+1.6f),FVector(3,3,2),Srgb(230,120,40));Shape(TEXT("StreetCube"),FVector(1,Y+5,Z+1.5f),FVector(2.5f,2.5f,2),Srgb(240,220,150));break;
    case 1: // ramen: bowl, broth, noodles, egg, chashu
     Shape(TEXT("StreetCylinder"),FVector(0,Y,Z+3.5f),FVector(17,17,7),Srgb(200,40,40));Shape(TEXT("StreetCylinder"),FVector(0,Y,Z+6.6f),FVector(15,15,1),Srgb(215,170,100));
     Shape(TEXT("StreetCylinder"),FVector(-3,Y+2,Z+7.2f),FVector(6,9,.6f),Srgb(240,215,120));Shape(TEXT("StreetSphere"),FVector(3,Y-3,Z+7.3f),FVector(4,4,2.5f),Srgb(255,250,240));
     Shape(TEXT("StreetSphere"),FVector(3,Y-3,Z+7.9f),FVector(2.2f,2.2f,1.2f),Srgb(250,180,40));Shape(TEXT("StreetCylinder"),FVector(4,Y+3,Z+7.3f),FVector(6,6,.6f),Srgb(170,90,60));
     Shape(TEXT("StreetCube"),FVector(-4,Y-4,Z+7.3f),FVector(2,2,.5f),Srgb(90,170,60));break;
    case 2: // hamburger and fries
     Shape(TEXT("StreetCylinder"),FVector(-3,Y,Z+1),FVector(10,10,2),Srgb(220,160,80));Shape(TEXT("StreetCylinder"),FVector(-3,Y,Z+2.6f),FVector(10.5f,10.5f,1.4f),Srgb(110,60,30));
     Shape(TEXT("StreetCylinder"),FVector(-3,Y,Z+3.5f),FVector(11,11,.5f),Srgb(110,190,60));Shape(TEXT("StreetCylinder"),FVector(-3,Y,Z+3.9f),FVector(10,10,.4f),Srgb(250,200,60));
     Shape(TEXT("StreetSphere"),FVector(-3,Y,Z+4.2f),FVector(10,10,5),Srgb(215,150,70));
     for(int32 I=0;I<5;++I)Shape(TEXT("StreetCube"),FVector(7,Y-4+I*2,Z+2.5f),FVector(1,1,6),Srgb(245,200,80),0,(I-2)*9.f);break;
    case 3: // sushi
     for(int32 I=0;I<6;++I){const float PX=I<3?-4.f:4.f,PY=Y-6+(I%3)*6;const FLinearColor Top[]={Srgb(250,130,80),Srgb(200,30,50),Srgb(250,220,80),Srgb(240,240,235)};
      Shape(TEXT("StreetCube"),FVector(PX,PY,Z+1),FVector(3.5f,5.5f,2),Srgb(250,250,245));Shape(TEXT("StreetCube"),FVector(PX,PY,Z+2.4f),FVector(3.8f,6,.8f),Top[FMath::RandHelper(4)]);}break;
    case 4: // spaghetti and meatballs
     Shape(TEXT("StreetSphere"),FVector(0,Y,Z),FVector(16,14,4),Srgb(240,200,110));Shape(TEXT("StreetSphere"),FVector(0,Y,Z+1.4f),FVector(10,9,2.5f),Srgb(200,50,30));
     for(int32 I=0;I<3;++I)Shape(TEXT("StreetSphere"),FVector(-3+I*3,Y+(I-1)*2,Z+2.4f),FVector(3,3,3),Srgb(120,60,35));break;
    case 5: // steak with vegetables
     Shape(TEXT("StreetCube"),FVector(-2,Y,Z+1.2f),FVector(12,8,2.4f),Srgb(110,55,30),12);
     for(int32 I=0;I<3;++I)Shape(TEXT("StreetSphere"),FVector(7,Y-4+I*4,Z+1.5f),FVector(3.5f,3.5f,3),Srgb(60,140,50));
     Shape(TEXT("StreetCylinder"),FVector(4,Y+6,Z+1),FVector(2,2,2),Srgb(240,130,40));Shape(TEXT("StreetCylinder"),FVector(1,Y+6,Z+1),FVector(2,2,2),Srgb(240,130,40));break;
    case 6: // omurice
     Shape(TEXT("StreetSphere"),FVector(0,Y,Z+1),FVector(15,9,6),Srgb(250,210,60));
     for(int32 I=0;I<3;++I)Shape(TEXT("StreetCube"),FVector(-4+I*4,Y,Z+3.9f),FVector(1.2f,7,.4f),Srgb(200,30,30),20);break;
    case 7: // salad bowl
     Shape(TEXT("StreetCylinder"),FVector(0,Y,Z+2.5f),FVector(16,16,5),Srgb(245,245,240));Shape(TEXT("StreetSphere"),FVector(0,Y,Z+5),FVector(13,13,4),Srgb(120,190,70));
     for(int32 I=0;I<4;++I)Shape(TEXT("StreetSphere"),FVector(-4+I*2.6f,Y+(I%2?3.f:-3.f),Z+6.2f),FVector(2.6f,2.6f,2.6f),I%2?Srgb(220,40,40):Srgb(250,220,80));break;
    default: // cake and tea
     Shape(TEXT("StreetCube"),FVector(-3,Y,Z+3),FVector(7,7,6),Srgb(255,245,235),30);Shape(TEXT("StreetCube"),FVector(-3,Y,Z+3.2f),FVector(7.2f,7.2f,.8f),Srgb(240,150,170),30);
     Shape(TEXT("StreetSphere"),FVector(-3,Y,Z+6.6f),FVector(2.6f,2.6f,2.6f),Srgb(220,30,50));Shape(TEXT("StreetCylinder"),FVector(6,Y+3,Z+2.5f),FVector(6,6,5),Srgb(255,255,255));
     Shape(TEXT("StreetCylinder"),FVector(6,Y+3,Z+4.6f),FVector(5,5,.5f),Srgb(170,100,40));break;
    }};
   // Reading or studying at the table: each has an open book in front of them (a new cover colour every time) and
   // a small stack of others beside it; studying adds a notebook and a pencil.
   const bool Study=P.FemaleClip==TEXT("venue_study_seated.okanim");
   if(P.Seated&&!P.Meal&&(Study||P.FemaleClip==TEXT("venue_read_seated.okanim"))){
    auto Cover=[](){return FLinearColor::MakeFromHSV8(uint8(FMath::RandRange(0,255)),uint8(FMath::RandRange(110,230)),uint8(FMath::RandRange(70,200)));};
    for(int32 Participant=0;Participant<2;++Participant){
     const float Y=Participant==0?63.f:91.f,Side=Participant==0?1.f:-1.f;const FLinearColor C=Cover();
     Shape(TEXT("StreetCube"),FVector(0,Y,64.25f),FVector(22,15,.5f),C);
     for(float X:{-5.4f,5.4f})Shape(TEXT("StreetCube"),FVector(X,Y,65.f),FVector(10.2f,14,1.1f),Srgb(250,247,236));
     Shape(TEXT("StreetCube"),FVector(0,Y,65.f),FVector(.6f,14.4f,1.2f),C);
     for(int32 K=0;K<2;++K)Shape(TEXT("StreetCube"),FVector(26*Side,Y+Side*2,65.5f+K*3),FVector(15,21,3),Cover(),FMath::FRandRange(-12.f,12.f));
     if(Study){Shape(TEXT("StreetCube"),FVector(-25*Side,Y,64.3f),FVector(15,21,.6f),Srgb(245,245,250),Side*8);
      Shape(TEXT("StreetCylinder"),FVector(-19*Side,Y,64.8f),FVector(.8f,.8f,14),Srgb(250,200,40),Side*8,90);}
    }
   }
   if(P.Meal){
    const int32 MealKind=FMath::RandHelper(9);
    for(int32 Participant=0;Participant<2;++Participant){
     const float PlateY=Participant==0?60.f:110.f;
     Part(FVector(0,PlateY,73),FVector(30,25,2),3);
     Dish(Participant==0||FMath::RandBool()?MealKind:FMath::RandHelper(9),PlateY,74.f); // often the same as his, sometimes her own
     for(int32 Piece=0;Piece<2;++Piece){const int32 Before=S.PairProps.Num();Part(FVector::ZeroVector,Piece==0?FVector(.8f,13,.8f):FVector(3,4,.6f));if(S.PairProps.Num()!=Before+1){UE_LOG(LogTemp,Display,TEXT("Street pair: ended at Scene.cpp:%d (phase %d)"),__LINE__,S.Pair.Phase);FinishPhoto();return;}auto* Prop=S.PairProps.Last().Get();Prop->SetActorHiddenInGame(true);if(auto* Material=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Scenes/StreetSocial/Materials/StreetMaterial11.StreetMaterial11")))Prop->GetStaticMeshComponent()->SetMaterial(0,Material);S.MealHandProps.Add({Prop,FVector(0,Piece==0?5.f:12.f,0)});}
    }
   }
  }
  if(P.PhoneUse){
   auto* Mesh=LoadObject<UStaticMesh>(nullptr,TEXT("/Game/Scenes/StreetSocial/Meshes/StreetCube.StreetCube"));if(!Mesh){UE_LOG(LogTemp,Display,TEXT("Street pair: ended at Scene.cpp:%d (phase %d)"),__LINE__,S.Pair.Phase);FinishPhoto();return;}
   for(int32 Participant=0;Participant<2;++Participant)for(int32 Part=0;Part<3;++Part){
    auto* Prop=GetWorld()->SpawnActor<AStaticMeshActor>(S.Male,FRotator::ZeroRotator);if(!Prop){UE_LOG(LogTemp,Display,TEXT("Street pair: ended at Scene.cpp:%d (phase %d)"),__LINE__,S.Pair.Phase);FinishPhoto();return;}Prop->SetOwner(this);Prop->SetActorHiddenInGame(true);auto* C=Prop->GetStaticMeshComponent();C->SetMobility(EComponentMobility::Movable);C->SetStaticMesh(Mesh);C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    const int32 Mat=Part==1?5:Part==2?7:11;const FString Path=FString::Printf(TEXT("/Game/Scenes/StreetSocial/Materials/StreetMaterial%02d.StreetMaterial%02d"),Mat,Mat);auto* Material=LoadObject<UMaterialInterface>(nullptr,*Path);if(!Material){Prop->Destroy();FinishPhoto();return;}C->SetMaterial(0,Material);S.PairProps.Add(Prop);
   }
  }
  if(P.Drink){
   auto* Cube=LoadObject<UStaticMesh>(nullptr,TEXT("/Game/Scenes/StreetSocial/Meshes/StreetCylinder.StreetCylinder"));
   if(!Cube){UE_LOG(LogTemp,Display,TEXT("Street pair: ended at Scene.cpp:%d (phase %d)"),__LINE__,S.Pair.Phase);FinishPhoto();return;}
   for(int32 Participant=0;Participant<2;++Participant)for(int32 Part=0;Part<2;++Part){
    auto* Prop=GetWorld()->SpawnActor<AStaticMeshActor>(S.Male,FRotator::ZeroRotator);if(!Prop){UE_LOG(LogTemp,Display,TEXT("Street pair: ended at Scene.cpp:%d (phase %d)"),__LINE__,S.Pair.Phase);FinishPhoto();return;}
    Prop->SetOwner(this);Prop->SetActorHiddenInGame(true);Prop->SetActorScale3D((Part==0?FVector(5.6f,5.6f,19):FVector(3.6f,3.6f,2.4f))/100.f);
    auto* C=Prop->GetStaticMeshComponent();C->SetMobility(EComponentMobility::Movable);C->SetStaticMesh(Cube);C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    if(auto* M=LoadObject<UMaterialInterface>(nullptr,Part==0?TEXT("/Game/Scenes/StreetSocial/Materials/StreetMaterial09.StreetMaterial09"):TEXT("/Game/Scenes/StreetSocial/Materials/StreetMaterial11.StreetMaterial11")))C->SetMaterial(0,M);S.PairProps.Add(Prop);
   }
  }
  if(P.Watermelon){
   const TCHAR* Shapes[]={TEXT("StreetCylinder"),TEXT("StreetCube"),TEXT("StreetSphere"),TEXT("StreetSphere"),TEXT("StreetSphere"),TEXT("StreetSphere"),TEXT("StreetSphere")};
   const FVector Sizes[]={FVector(2.83127f,2.83127f,61.88289f),FVector(16,3,5),FVector(21.8f,21.8f,23.4845f),FVector(21.8f,10.9f,23.4845f),FVector(21.8f,10.9f,23.4845f),FVector(19.4f,2,21),FVector(19.4f,2,21)};const int32 Mats[]={8,7,10,10,10,2,2};
   for(int32 I=0;I<7;++I){const FString MeshPath=FString::Printf(TEXT("/Game/Scenes/StreetSocial/Meshes/%s.%s"),Shapes[I],Shapes[I]),MatPath=FString::Printf(TEXT("/Game/Scenes/StreetSocial/Materials/StreetMaterial%02d.StreetMaterial%02d"),Mats[I],Mats[I]);auto* Mesh=LoadObject<UStaticMesh>(nullptr,*MeshPath);auto* Mat=LoadObject<UMaterialInterface>(nullptr,*MatPath);if(!Mesh||!Mat){UE_LOG(LogTemp,Display,TEXT("Street pair: ended at Scene.cpp:%d (phase %d)"),__LINE__,S.Pair.Phase);FinishPhoto();return;}auto* Prop=GetWorld()->SpawnActor<AStaticMeshActor>(S.Male,FRotator::ZeroRotator);if(!Prop){UE_LOG(LogTemp,Display,TEXT("Street pair: ended at Scene.cpp:%d (phase %d)"),__LINE__,S.Pair.Phase);FinishPhoto();return;}Prop->SetOwner(this);Prop->SetActorHiddenInGame(true);Prop->SetActorScale3D(Sizes[I]/100.f);auto* C=Prop->GetStaticMeshComponent();C->SetMobility(EComponentMobility::Movable);C->SetStaticMesh(Mesh);C->SetMaterial(0,Mat);C->SetCollisionEnabled(ECollisionEnabled::NoCollision);S.PairProps.Add(Prop);}
  }
  if(P.Splash){
   auto* Mesh=LoadObject<UStaticMesh>(nullptr,TEXT("/Game/Scenes/StreetSocial/Meshes/StreetSphere.StreetSphere"));
   auto* Material=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Scenes/StreetSocial/Materials/StreetMaterial04.StreetMaterial04"));
   if(!Mesh||!Material){UE_LOG(LogTemp,Display,TEXT("Street pair: ended at Scene.cpp:%d (phase %d)"),__LINE__,S.Pair.Phase);FinishPhoto();return;}
   for(int32 I=0;I<16;++I){auto* Drop=GetWorld()->SpawnActor<AStaticMeshActor>(S.Male,FRotator::ZeroRotator);if(!Drop){UE_LOG(LogTemp,Display,TEXT("Street pair: ended at Scene.cpp:%d (phase %d)"),__LINE__,S.Pair.Phase);FinishPhoto();return;}Drop->SetOwner(this);Drop->SetActorHiddenInGame(true);Drop->SetActorScale3D(FVector(.018f,.018f,.035f));auto* C=Drop->GetStaticMeshComponent();C->SetMobility(EComponentMobility::Movable);C->SetStaticMesh(Mesh);C->SetMaterial(0,Material);C->SetCollisionEnabled(ECollisionEnabled::NoCollision);C->SetCastShadow(false);S.PairProps.Add(Drop);}
  }
  if(P.Meal&&S.MealHandProps.Num()!=4){UE_LOG(LogTemp,Display,TEXT("Street pair: ended at Scene.cpp:%d (phase %d)"),__LINE__,S.Pair.Phase);FinishPhoto();return;}
  FString Error;if(!OKPlayCrowdRuntimeAction(this,0,P.MaleClip,Error)||!OKPlayCrowdRuntimeAction(this,P.Partner,P.FemaleClip,Error)){UE_LOG(LogTemp,Display,TEXT("Street pair: ended at Scene.cpp:%d (phase %d)"),__LINE__,S.Pair.Phase);FinishPhoto();return;}
  P.Phase=1;P.PlayingAt=Now;P.MealNow[0]=P.MaleClip;P.MealNow[1]=P.FemaleClip;S.MealAteAt.Reset();if(P.PhoneUse){S.Target=P.Partner;S.Yaw=P.Yaw+35;S.Pitch=-8;S.Distance=350;S.MouseUI=true;ApplyMouseMode(S.ViewportFocused);S.Message=OKT(TEXT("ui.scene.use_phone_together_hint"));RefreshUI();return;}if(P.Watermelon){S.Yaw=P.Yaw+(P.FemaleStrikes?-145.f:-35.f);S.Pitch=-15;S.Distance=430;S.MouseUI=true;ApplyMouseMode(S.ViewportFocused);S.Message=OKT(TEXT("ui.scene.watermelon_swing_hint"));RefreshUI();return;}if(P.Meal){S.Target=P.Partner;S.Yaw=P.Yaw+35;S.Pitch=-12;S.Distance=370;S.MouseUI=true;ApplyMouseMode(S.ViewportFocused);S.Message=OKT(TEXT("ui.scene.eat_together_hint"));RefreshUI();return;}if(P.Splash){S.Yaw=P.Yaw+35;S.Pitch=-8;S.Distance=380;S.MouseUI=true;ApplyMouseMode(S.ViewportFocused);S.Message=P.FemaleThrows?OKT(TEXT("ui.scene.her_splash_hint")):OKT(TEXT("ui.scene.splash_water_hint"));RefreshUI();return;}if(P.Drink){S.Target=P.Partner;S.Yaw=P.Yaw+35;S.Pitch=-8;S.Distance=350;S.MouseUI=true;ApplyMouseMode(S.ViewportFocused);S.Message=OKT(TEXT("ui.scene.drink_together_hint"));RefreshUI();return;}if(P.Read){/* no conversation target: the hand-held book shows only outside a talk */S.Yaw=P.Yaw+150;S.Pitch=-10;S.Distance=330;S.MouseUI=true;ApplyMouseMode(S.ViewportFocused);S.Message=OKT(TEXT("ui.scene.read_together_hint"));RefreshUI();return;}if(P.Seated){S.Target=P.Partner;S.Yaw=P.Yaw+35;S.Pitch=-12;S.Distance=320;S.MouseUI=true;ApplyMouseMode(S.ViewportFocused);S.Message=OKT(TEXT("ui.scene.sit_and_chat_hint"));RefreshUI();return;}S.Yaw=P.Yaw+-90;S.Pitch=-8;S.Distance=350;S.MouseUI=true;ApplyMouseMode(S.ViewportFocused);S.Message=OKT(TEXT("ui.scene.photo_together_hint"));RefreshUI();return;
 }
 if(P.Watermelon){
  const int32 Striker=P.FemaleStrikes?P.Partner:0;FTransform Hand,Head;
  if(S.PairProps.Num()!=7||!OKGetCrowdRuntimeBoneTransform(this,Striker,TEXT("k_f_handR_00"),Hand)||!(OKGetCrowdRuntimeBoneTransform(this,Striker,TEXT("cf_j_head"),Head)||OKGetCrowdRuntimeBoneTransform(this,Striker,TEXT("cm_j_head"),Head))||Hand.ContainsNaN()||Head.ContainsNaN()){UE_LOG(LogTemp,Display,TEXT("Street pair: ended at Scene.cpp:%d (phase %d)"),__LINE__,S.Pair.Phase);FinishPhoto();return;}
  for(const auto& Prop:S.PairProps)if(!Prop.IsValid()){UE_LOG(LogTemp,Display,TEXT("Street pair: ended at Scene.cpp:%d (phase %d)"),__LINE__,S.Pair.Phase);FinishPhoto();return;}
  const FVector Root=Striker==0?S.Male:S.Agents[Striker-1].Position;const FQuat Rotation=(Striker==0?S.Rotations[0]:S.RenderedRotations[Striker]).Quaternion();const FVector Melon=Root+Rotation.RotateVector(FVector(0,P.MelonForward,12.832f));
  const FVector Tip=Hand.TransformPosition(FVector(0,50.99748f,0));FOKRuntimeActionStreamStatus Clock;
  if(!OKGetCrowdRuntimeActionStatus(this,Striker,Clock)||!Clock.Playing||Clock.DurationSeconds<=0){UE_LOG(LogTemp,Display,TEXT("Street pair: ended at Scene.cpp:%d (phase %d)"),__LINE__,S.Pair.Phase);FinishPhoto();return;}
  const int32 Cycle=P.MelonCycle<0?0:P.MelonCycle+(Clock.PlaybackSeconds+.5f< P.MelonPlayback?1:0);P.MelonPlayback=Clock.PlaybackSeconds;
  if(Cycle!=P.MelonCycle){if(P.MelonCycle>=0&&!P.MelonHit){S.Message=OKT(TEXT("ui.scene.watermelon_miss_hint"));RefreshUI();}P.MelonCycle=Cycle;P.MelonHit=false;P.HasStickTip=false;}
  const float HitDistance=FMath::PointDistToSegment(Melon,P.HasStickTip?P.LastStickTip:Tip,Tip);P.MelonClosest=FMath::Min(P.MelonClosest,HitDistance);const bool Hit=HitDistance<12.4f;
  if(Hit&&!P.MelonHit){P.MelonHit=true;++S.MelonHits;if(S.SelfTest)FScreenshotRequest::RequestScreenshot(FPaths::GetPath(S.Manifest.SocialReport)/FString::Printf(TEXT("watermelon.contact.%d.%d.png"),P.Partner,P.FemaleStrikes?1:0),true,false);S.Message=OKT(TEXT("ui.scene.watermelon_hit_hint"));RefreshUI();}
  P.LastStickTip=Tip;P.HasStickTip=true;++S.MelonSamples;
  if(S.SelfTest){const FVector Axis[]={FVector(1,0,0),FVector(-1,0,0),FVector(0,1,0),FVector(0,-1,0),FVector(0,0,1),FVector(0,0,-1)};for(int32 I=0;I<6;++I)P.MelonAxes[I]=FMath::Min(P.MelonAxes[I],float(FVector::Dist(Melon,Hand.TransformPosition(Axis[I]*50.99748f))));P.MelonHandHeight=FMath::Min(P.MelonHandHeight,float(Hand.GetLocation().Z-Root.Z));}
  const FVector GripScale=Hand.GetScale3D();S.PairProps[0]->SetActorScale3D(FVector(2.83127f*GripScale.X,2.83127f*GripScale.Z,61.88289f*GripScale.Y)/100.f);S.PairProps[0]->SetActorHiddenInGame(false);S.PairProps[0]->SetActorLocationAndRotation(Hand.TransformPosition(FVector(0,20.05603f,0)),Hand.GetRotation()*FQuat(FVector(1,0,0),-PI/2));
  S.PairProps[1]->SetActorHiddenInGame(false);FTransform Glasses;if(OKGetCrowdRuntimeBoneTransform(this,Striker,TEXT("a_n_megane"),Glasses)&&!Glasses.ContainsNaN())S.PairProps[1]->SetActorLocationAndRotation(Glasses.GetLocation(),Glasses.GetRotation());else S.PairProps[1]->SetActorLocationAndRotation(Head.TransformPosition(FVector(0,8,8)),Head.GetRotation());
  S.PairProps[2]->SetActorLocation(Melon);S.PairProps[2]->SetActorHiddenInGame(P.MelonHit);
  for(int32 I=3;I<7;++I){S.PairProps[I]->SetActorHiddenInGame(!P.MelonHit);S.PairProps[I]->SetActorLocation(Melon+Rotation.RotateVector(FVector(I%2==1?-13.f:13.f,I<5?0.f:(I==5?-6.f:6.f),0)));}
 }
 if(P.Meal){
  if(S.MealHandProps.Num()!=4){UE_LOG(LogTemp,Display,TEXT("Street pair: ended at Scene.cpp:%d (phase %d)"),__LINE__,S.Pair.Phase);FinishPhoto();return;}
  // Eating while something of theirs is being eaten (the meal tick's MealAteAt), else seated with nothing in hand.
  if(!S.SeatedMaleClip.IsEmpty()&&!S.SeatedFemaleClip.IsEmpty())for(int32 Participant=0;Participant<2;++Participant){
   const int32 Actor=Participant==0?0:P.Partner;const double* Ate=S.MealAteAt.Find(Actor);
   const FString& Eat=Participant==0?P.MaleClip:P.FemaleClip;const FString& Idle=Participant==0?S.SeatedMaleClip:S.SeatedFemaleClip;
   // Two seconds at the start before anything counts (the first dishes are still being made then).
   const FString Want=(Ate&&FPlatformTime::Seconds()-*Ate<3.0)||Now-P.PlayingAt<2.0?Eat:Idle;
   if(Want==P.MealNow[Participant]){P.MealWant[Participant].Reset();continue;}
   // Played at once (a resident clip) or queued and played once it is streamed in (OKPlayCrowdRuntimeAction).
   FString Error;const bool Played=OKPlayCrowdRuntimeAction(this,Actor,Want,Error);P.MealNow[Participant]=Want;P.MealWant[Participant].Reset();
   if(Played){UE_LOG(LogTemp,Display,TEXT("Street meal: actor %d %s"),Actor,Want==Eat?TEXT("eats"):TEXT("sits with nothing to eat"));}
   else{UE_LOG(LogTemp,Warning,TEXT("Street meal: %s for actor %d: %s"),*Want,Actor,*Error);}
  }
  for(int32 Participant=0;Participant<2;++Participant){FTransform Hand;if(!OKGetCrowdRuntimeBoneTransform(this,Participant==0?0:P.Partner,TEXT("k_f_handR_00"),Hand)||Hand.ContainsNaN()){UE_LOG(LogTemp,Display,TEXT("Street pair: ended at Scene.cpp:%d (phase %d)"),__LINE__,S.Pair.Phase);FinishPhoto();S.Message=OKT(TEXT("ui.scene.utensil_prop_failed"));RefreshUI();return;}
   for(int32 Piece=0;Piece<2;++Piece){auto& Item=S.MealHandProps[Participant*2+Piece];if(!Item.Key.IsValid()){UE_LOG(LogTemp,Display,TEXT("Street pair: ended at Scene.cpp:%d (phase %d)"),__LINE__,S.Pair.Phase);FinishPhoto();return;}Item.Key->SetActorHiddenInGame(P.MealNow[Participant]!=(Participant==0?P.MaleClip:P.FemaleClip));Item.Key->SetActorLocationAndRotation(Hand.TransformPosition(Item.Value),Hand.GetRotation());}
  }
 }
 if(P.PhoneUse){
  if(S.PairProps.Num()!=6){UE_LOG(LogTemp,Display,TEXT("Street pair: ended at Scene.cpp:%d (phase %d)"),__LINE__,S.Pair.Phase);FinishPhoto();return;}
  for(int32 Participant=0;Participant<2;++Participant){FTransform Hand;if(!OKGetCrowdRuntimeBoneTransform(this,Participant==0?0:P.Partner,TEXT("k_f_handL_00"),Hand)||Hand.ContainsNaN()){UE_LOG(LogTemp,Display,TEXT("Street pair: ended at Scene.cpp:%d (phase %d)"),__LINE__,S.Pair.Phase);FinishPhoto();return;}
   for(int32 Part=0;Part<3;++Part){auto* Prop=S.PairProps[Participant*3+Part].Get();if(!Prop){UE_LOG(LogTemp,Display,TEXT("Street pair: ended at Scene.cpp:%d (phase %d)"),__LINE__,S.Pair.Phase);FinishPhoto();return;}Prop->SetActorTransform(OKStreetPhonePartTransform(Hand,Part));Prop->SetActorHiddenInGame(false);}
  }
  ++S.PhoneUseSamples;
 }
 if(P.Drink){
  for(int32 Participant=0;Participant<2;++Participant){
   FTransform Hand;const int32 Character=Participant==0?0:P.Partner;
   const bool Found=OKGetCrowdRuntimeBoneTransform(this,Character,TEXT("k_f_handR_00"),Hand);
   if(!Found||Hand.ContainsNaN()||S.PairProps.Num()!=4){UE_LOG(LogTemp,Display,TEXT("Street pair: ended at Scene.cpp:%d (phase %d)"),__LINE__,S.Pair.Phase);FinishPhoto();S.Message=OKT(TEXT("ui.scene.drink_prop_failed"));RefreshUI();return;}
   for(int32 Part=0;Part<2;++Part)if(S.PairProps[Participant*2+Part].IsValid()){
    auto* Prop=S.PairProps[Participant*2+Part].Get();Prop->SetActorHiddenInGame(false);
    Prop->SetActorLocationAndRotation(Hand.TransformPosition(FVector(0,0,Part==0?10.f:20.7f)),Hand.GetRotation());
   }
  }
 }
 if(P.Splash){
  FTransform Hand;const int32 Thrower=P.FemaleThrows?P.Partner:0;
  if(!OKGetCrowdRuntimeBoneTransform(this,Thrower,TEXT("k_f_handR_00"),Hand)||Hand.ContainsNaN()||S.PairProps.Num()!=16){UE_LOG(LogTemp,Display,TEXT("Street pair: ended at Scene.cpp:%d (phase %d)"),__LINE__,S.Pair.Phase);FinishPhoto();S.Message=OKT(TEXT("ui.scene.splash_prop_invalid"));RefreshUI();return;}
  // Authored droplets: each burst follows the thrower's own runtime hand, never another card's pose.
  const float Time=FMath::Fmod(float(Now-P.PlayingAt),float(S.SplashCycleSeconds));const FVector Direction=P.FemaleThrows?FVector(0,-1,0):FVector(0,1,0);
  for(int32 I=0;I<S.PairProps.Num();++I){if(!S.PairProps[I].IsValid()){UE_LOG(LogTemp,Display,TEXT("Street pair: ended at Scene.cpp:%d (phase %d)"),__LINE__,S.Pair.Phase);FinishPhoto();return;}auto* Drop=S.PairProps[I].Get();const float Age=Time-1.f-I*.015f;const bool Show=Age>=0&&Age<.65f;Drop->SetActorHiddenInGame(!Show);if(Show){Drop->SetActorLocation(Hand.GetLocation()+Direction*(Age*230)+FVector((I%4-1.5f)*Age*16,0,Age*90-220*Age*Age));++S.SplashEffectSamples;}}
 }
 if(P.CapturePending){
  if(IFileManager::Get().FileSize(*P.CapturePath)>1000&&Now-P.CaptureAt>.5){S.LastPhoto=P.CapturePath;++S.PhotosSaved;P.CapturePending=false;
   // Filed as his photo of the two of them (photos.rs), in his album.
   if(S.Manifest.Characters.IsValidIndex(P.Partner))PhotoFiled(P.CapturePath,TEXT("him"),{OKStreetStableId(S.Manifest.Characters[P.Partner]),TEXT("him")},FString(),INDEX_NONE);S.Message=OKT(TEXT("ui.scene.photo_saved_again"));RefreshUI();if(P.FinishRequested){UE_LOG(LogTemp,Display,TEXT("Street pair: ended at Scene.cpp:%d (phase %d)"),__LINE__,S.Pair.Phase);FinishPhoto();return;}}
  else if(Now-P.CaptureAt>3){P.CapturePending=false;S.Message=OKT(TEXT("ui.scene.photo_save_failed"));RefreshUI();if(P.FinishRequested){UE_LOG(LogTemp,Display,TEXT("Street pair: ended at Scene.cpp:%d (phase %d)"),__LINE__,S.Pair.Phase);FinishPhoto();return;}}
 }
 // At a meal either of the two may be sitting with nothing to eat (MealNow): that clip is theirs too, not a pose lost.
 // (Either of the two clips, whichever is playing while the other is being streamed in.)
 auto Theirs=[&](const FOKRuntimeActionStreamStatus& X,const FString& Clip,int32 Who){return X.ClipId==Clip||(P.Meal&&X.ClipId==(Who==0?S.SeatedMaleClip:S.SeatedFemaleClip));};
 // ...and while the meal's next clip is still being streamed in or blended (Jeanne's dessert ended 6 s in, the moment
 // she sat back with nothing to eat), nothing is lost either.
 auto Switching=[&](const FOKRuntimeActionStreamStatus& X){return P.Meal&&(X.Pending||X.Preparing||X.Ready||X.Transitioning);};
 const bool LostA=!(A.Playing&&Theirs(A,P.MaleClip,0))&&!Switching(A),LostB=!(B.Playing&&Theirs(B,P.FemaleClip,1))&&!Switching(B);
 if(LostA||LostB||(!P.Conversation()&&Now-P.PlayingAt>30)){UE_LOG(LogTemp,Display,TEXT("Street pair: ended, %s (%s playing=%d %s playing=%d)"),LostA||LostB?TEXT("its clips are gone"):TEXT("its 30 s pose is over"),*A.ClipId,A.Playing?1:0,*B.ClipId,B.Playing?1:0);FinishPhoto();}
}
void AOKStreetSocialScene::RunWatermelonProbe(double Now){
 auto& S=*State;auto Fail=[&](const TCHAR* Why){FinishPhoto();S.Errors.Add(Why);WriteReport(false);};
 if(S.MelonCasesVerified==10){S.MelonChecked=true;return;}
 const int32 Character=S.MelonCasesVerified/2+1,Kind=6+S.MelonCasesVerified%2;
 if(S.MelonProbePhase==0){S.MelonProbeSave=OKStreetWriteSave(S.Store);S.Agents[Character-1].Position=S.Male+FVector(150,0,0);BeginPair(Character,Kind);if(S.Pair.Partner!=Character||!S.Pair.Watermelon){Fail(TEXT("Watermelon admission failed"));return;}if(!S.MelonCancelChecked){FinishPhoto();if(S.Pair.Partner!=INDEX_NONE||!S.PairProps.IsEmpty()){Fail(TEXT("Watermelon queued cancel retained props"));return;}S.MelonCancelChecked=true;BeginPair(Character,Kind);}S.MelonProbeSession=S.Pair;S.MelonProbePhase=1;return;}
 if(S.Pair.Partner!=Character||!S.Pair.Watermelon){Fail(TEXT("Watermelon ended before verification"));return;}
 if(S.Pair.Phase!=1&&Now-S.Pair.RequestedAt>55){Fail(TEXT("Watermelon preparation timeout"));return;}if(S.Pair.Phase!=1||Now-S.Pair.PlayingAt<.25)return;
 FOKRuntimeActionStreamStatus A,B;if(!OKGetCrowdRuntimeActionStatus(this,0,A)||!OKGetCrowdRuntimeActionStatus(this,Character,B)||!A.Playing||!B.Playing||A.ClipId!=S.Pair.MaleClip||B.ClipId!=S.Pair.FemaleClip||S.PairProps.Num()!=7){Fail(TEXT("Watermelon private actions/props mismatch"));return;}
 FTransform Hand;const int32 Striker=S.Pair.FemaleStrikes?Character:0;if(!OKGetCrowdRuntimeBoneTransform(this,Striker,TEXT("k_f_handR_00"),Hand)||FVector::Dist(Hand.TransformPosition(FVector(0,20.05603f,0)),S.PairProps[0]->GetActorLocation())>.1){Fail(TEXT("Watermelon stick detached from own private hand"));return;}
 if(S.MelonProbePhase==1){const auto& Clock=S.Pair.FemaleStrikes?B:A;if(Clock.DurationSeconds<=0){Fail(TEXT("Watermelon action has no duration"));return;}if(Now-S.Pair.PlayingAt<Clock.DurationSeconds+1)return;FScreenshotRequest::RequestScreenshot(FPaths::GetPath(S.Manifest.SocialReport)/FString::Printf(TEXT("watermelon.%d.%d.png"),Character,Kind-6),true,false);S.MelonProbeAt=Now;S.MelonProbePhase=2;return;}
 if(Now-S.MelonProbeAt<.75)return;const FString Shot=FPaths::GetPath(S.Manifest.SocialReport)/FString::Printf(TEXT("watermelon.%d.%d.png"),Character,Kind-6);if(IFileManager::Get().FileSize(*Shot)<1000){Fail(TEXT("Watermelon screenshot missing"));return;}
 if(S.Pair.MelonClosest>=12.4f){UE_LOG(LogTemp,Warning,TEXT("Watermelon miss: case=%d character=%d female_strikes=%d min_cm=%.2f hand_min_z=%.2f axes=%.2f,%.2f,%.2f,%.2f,%.2f,%.2f"),S.MelonCasesVerified,Character,S.Pair.FemaleStrikes,S.Pair.MelonClosest,S.Pair.MelonHandHeight,S.Pair.MelonAxes[0],S.Pair.MelonAxes[1],S.Pair.MelonAxes[2],S.Pair.MelonAxes[3],S.Pair.MelonAxes[4],S.Pair.MelonAxes[5]);
  Fail(TEXT("Watermelon action did not physically contact target"));return;}
 UE_LOG(LogTemp,Display,TEXT("Watermelon geometry: case=%d min_cm=%.2f hand_min_z=%.2f axes=%.2f,%.2f,%.2f,%.2f,%.2f,%.2f"),S.MelonCasesVerified,S.Pair.MelonClosest,S.Pair.MelonHandHeight,S.Pair.MelonAxes[0],S.Pair.MelonAxes[1],S.Pair.MelonAxes[2],S.Pair.MelonAxes[3],S.Pair.MelonAxes[4],S.Pair.MelonAxes[5]);S.MelonDistances.Add(S.Pair.MelonClosest);const auto Before=S.MelonProbeSession;FinishPhoto();if(S.Pair.Partner!=INDEX_NONE||!S.PairProps.IsEmpty()||S.Agents[Character-1].Held!=Before.SavedAgent.Held||S.Yaw!=Before.SavedYaw||S.Pitch!=Before.SavedPitch||S.Distance!=Before.SavedDistance||S.MouseUI!=Before.SavedMouseUI||OKStreetWriteSave(S.Store)!=S.MelonProbeSave){Fail(TEXT("Watermelon cancel failed to restore actors/camera/save"));return;}++S.MelonCasesVerified;S.MelonProbePhase=0;
}
void AOKStreetSocialScene::RunDialogueContextProbe(double Now){
 auto& S=*State;auto Require=[&](bool Pass,const TCHAR* Why){if(Pass)return true;S.Errors.Add(Why);WriteReport(false);return false;};
 if(!Require(Now-S.StartedAt<360,TEXT("Dialogue context probe timed out"))||!S.AllReady)return;
 if(S.ContextPhase==0){
  if(S.ContextGirl>5){S.ContextPhase=4;WriteReport(S.Errors.IsEmpty());return;}

  EndTalk();S.Target=S.ContextGirl;S.Agents[S.Target-1].Held=true;S.Male=S.Agents[S.Target-1].Position+FVector(0,-180,0);S.Yaw=90;S.Pitch=-8;S.Distance=350;
  S.Rotations[0]=OKStreetFacingMovement(S.Agents[S.Target-1].Position-S.Male,S.Rotations[0]);S.Rotations[S.Target]=OKStreetFacingMovement(S.Male-S.Agents[S.Target-1].Position,S.Rotations[S.Target]);
  const FString Id=OKStreetStableId(S.Manifest.Characters[S.Target]);const int32 I=OKStreetRelationIndex(S.Store,Id,S.Manifest.TalkBudget,true);S.Store.Relations[I].Stage=TEXT("stranger");S.Store.Relations[I].Favor=70;S.Store.Relations[I].TalksRemaining=5;
  const TCHAR* Categories[]={TEXT("daily"),TEXT("romance"),TEXT("intimacy")};
  if(S.ContextCategory<3)Topic(Categories[S.ContextCategory]);
  else {int32 Generic=INDEX_NONE;for(int32 N=0;N<S.Catalogue.Nodes.Num();++N){const auto& L=S.Catalogue.Nodes[N];if(L.PersonalityId==S.Manifest.Characters[S.Target].PersonalityId&&L.StageIndex==0&&L.Category==TEXT("daily")&&L.VoiceSequenceComplete&&!OKStreetSourcePlayerPrompt(S.Catalogue,N).IsEmpty()){Generic=N;break;}}if(!Require(Generic!=INDEX_NONE,TEXT("Source generic pair unavailable")))return;QueueNode(Generic,TEXT("daily"));}
  if(S.Node==INDEX_NONE||S.PlayerLine.IsEmpty())UE_LOG(LogTemp,Warning,TEXT("Dialogue context miss: girl=%d category=%d node=%d player=%s message=%s"),S.ContextGirl,S.ContextCategory,S.Node,*S.PlayerLine,*S.Message);
  if(!Require(S.Node!=INDEX_NONE&&!S.PlayerLine.IsEmpty(),TEXT("Female reply lost its exact source question")))return;
  auto J=MakeShared<FJsonObject>();J->SetNumberField(TEXT("slot"),S.Target);J->SetStringField(TEXT("node"),S.Catalogue.Nodes[S.Node].Id);J->SetStringField(TEXT("branch"),S.ActivePromptBranch);J->SetStringField(TEXT("player"),S.PlayerLine);J->SetStringField(TEXT("female"),S.Message);S.ContextEvidence.Add(MakeShared<FJsonValueObject>(J));
  S.ContextAt=Now;S.ContextPhase=1;return;
 }
 if(S.ContextPhase==1){
  // Short source replies can complete between mouth samples. Confirm the real
  // audio state instead of requiring a nonzero phoneme on this exact frame.
  EOKRuntimeSpeechState Speech;FString Error;if(Now-S.ContextAt<.25||!OKGetCrowdRuntimeSpeechState(this,S.Target,Speech,Error)||(Speech!=EOKRuntimeSpeechState::Playing&&Speech!=EOKRuntimeSpeechState::Completed))return;
  ++S.ContextChecks;
  FScreenshotRequest::RequestScreenshot(FPaths::GetPath(S.Manifest.SocialReport)/FString::Printf(TEXT("context.%d.%d.png"),S.ContextGirl,S.ContextCategory),true,false);
  S.ContextPhase=5;return;
 }
 if(S.ContextPhase==5){
  EndTalk();if(++S.ContextCategory<4){S.ContextPhase=0;return;}
  S.Target=S.ContextGirl;S.Agents[S.Target-1].Held=true;const int32 I=OKStreetRelationIndex(S.Store,OKStreetStableId(S.Manifest.Characters[S.Target]),S.Manifest.TalkBudget,true);S.Store.Relations[I].Stage=TEXT("stranger");S.Store.Relations[I].Favor=100;S.Store.Relations[I].TalksRemaining=5;S.ContextSave=OKStreetWriteSave(S.Store);
  RelationshipEvent(TEXT("confession"));if(!Require(S.RelationshipPending&&S.RelationshipVoiceRequired&&!S.RelationshipVoiceCompleted&&S.PendingEffect.RelationEvent==TEXT("lover"),TEXT("Stranger at favor 100 did not start source acceptance")))return;
  ApplyRelationship();if(!Require(OKStreetWriteSave(S.Store)==S.ContextSave,TEXT("Confession committed before ordered dialogue completed")))return;
  EndTalk();if(!Require(OKStreetWriteSave(S.Store)==S.ContextSave,TEXT("Cancelled confession changed relation")))return;
  S.Target=S.ContextGirl;S.Agents[S.Target-1].Held=true;RelationshipEvent(TEXT("confession"));S.ContextPhase=2;return;
 }
 if(S.ContextPhase==2){
  if(S.RelationshipPending){Require(OKStreetWriteSave(S.Store)==S.ContextSave,TEXT("Confession committed before final source line"));return;}
  FScreenshotRequest::RequestScreenshot(FPaths::GetPath(S.Manifest.SocialReport)/FString::Printf(TEXT("confession.accepted.%d.png"),S.ContextGirl),true,false);
  S.ContextPhase=6;return;
 }
 if(S.ContextPhase==6){
  const auto R=OKStreetViewRelation(S.Store,OKStreetStableId(S.Manifest.Characters[S.Target]),S.Manifest.TalkBudget);
  if(!Require(R.Stage==TEXT("lover")&&R.Favor==0&&R.TalksRemaining==4,TEXT("Completed source confession did not save lover relation exactly once")))return;
  FOKStreetStore Reload;FString Error;if(!Require(OKStreetLoadSaveFile(S.Manifest.SavePath,Reload,Error)==EOKStreetLoad::Ok&&OKStreetWriteSave(Reload)==OKStreetWriteSave(S.Store),TEXT("Confession saved relation did not reload")))return;
  const FString Saved=OKStreetWriteSave(S.Store);ApplyRelationship();if(!Require(Saved==OKStreetWriteSave(S.Store),TEXT("Confession double confirm changed save")))return;
  ++S.ContextConfessions;EndTalk();S.ContextPhase=7;
 }
 if(S.ContextPhase==7){
  S.Target=S.ContextGirl;S.Agents[S.Target-1].Held=true;const int32 I=OKStreetRelationIndex(S.Store,OKStreetStableId(S.Manifest.Characters[S.Target]),S.Manifest.TalkBudget,true);S.Store.Relations[I].Stage=TEXT("friend");S.Store.Relations[I].Favor=70;S.Store.Relations[I].TalksRemaining=5;S.ContextSave=OKStreetWriteSave(S.Store);
  int32 Seed=0;for(;Seed<1000;++Seed){FRandomStream R(Seed);if(R.RandRange(0,99)>=55)break;}
  S.Random.Initialize(Seed);RelationshipEvent(TEXT("confession"));if(!Require(S.RelationshipPending&&S.RelationshipVoiceRequired&&S.Node!=INDEX_NONE&&S.PendingEffect.RelationEvent.IsEmpty()&&!S.PlayerLine.IsEmpty(),TEXT("Source confession refusal did not queue both speakers")))return;
  ApplyRelationship();if(!Require(OKStreetWriteSave(S.Store)==S.ContextSave,TEXT("Refusal committed before source audio")))return;EndTalk();if(!Require(OKStreetWriteSave(S.Store)==S.ContextSave,TEXT("Refusal cancellation changed relation")))return;
  S.Target=S.ContextGirl;S.Agents[S.Target-1].Held=true;S.Random.Initialize(Seed);RelationshipEvent(TEXT("confession"));S.ContextPhase=8;return;
 }
 if(S.ContextPhase==8){
  if(S.RelationshipPending){Require(OKStreetWriteSave(S.Store)==S.ContextSave,TEXT("Refusal committed before final source line"));return;}
  if(!Require(!S.Message.IsEmpty()&&!S.Message.Contains(TEXT("ADV"))&&!S.PlayerLine.IsEmpty(),TEXT("Refusal replaced source dialogue with a summary")))return;
  FScreenshotRequest::RequestScreenshot(FPaths::GetPath(S.Manifest.SocialReport)/FString::Printf(TEXT("confession.refused.%d.png"),S.ContextGirl),true,false);S.ContextPhase=9;return;
 }
 if(S.ContextPhase==9){
  const auto R=OKStreetViewRelation(S.Store,OKStreetStableId(S.Manifest.Characters[S.Target]),S.Manifest.TalkBudget);
  if(!Require(R.Stage==TEXT("friend")&&R.Favor==70&&R.TalksRemaining==4,TEXT("Completed source refusal changed relationship or budget incorrectly")))return;
  FOKStreetStore Reload;FString Error;if(!Require(OKStreetLoadSaveFile(S.Manifest.SavePath,Reload,Error)==EOKStreetLoad::Ok&&OKStreetWriteSave(Reload)==OKStreetWriteSave(S.Store),TEXT("Refusal save failed reload")))return;
  const FString Saved=OKStreetWriteSave(S.Store);ApplyRelationship();if(!Require(Saved==OKStreetWriteSave(S.Store),TEXT("Refusal double confirmation changed save")))return;
  ++S.ContextRefusals;EndTalk();++S.ContextGirl;S.ContextCategory=0;S.ContextPhase=0;
 }
}

void AOKStreetSocialScene::RunPresentationProbe(double Now){
 auto& S=*State;auto Fail=[&](const TCHAR* Why){S.Errors.Add(Why);WriteReport(false);};
 if(Now-S.StartedAt>180){Fail(TEXT("Presentation probe timeout"));return;}if(!S.AllReady)return;
 if(S.PresentationPhase==0){S.ShowDebug=true;S.PresentationStore=S.Store;S.Target=1;S.Agents[0].Held=true;S.Male=S.Agents[0].Position+FVector(0,-160,0);S.Yaw=90;S.Pitch=-8;S.Distance=350;S.TopicsOpen=false;S.PlayerLine=OKT(TEXT("ui.scene.greeting_topic_q"));S.Message=OKT(TEXT("ui.scene.greeting_stand_talk"));S.PresentationAt=Now;S.PresentationPhase=1;RefreshUI();return;}
 if(S.PresentationPhase==1){if(Now-S.PresentationAt<2)return;
  for(int32 Character:{0,S.PresentationGirl}){FOKRuntimeCharacterStatus Status;if(!OKGetCrowdRuntimeCharacterStatus(this,Character,Status)||!Status.ActiveClipId.StartsWith(TEXT("conversation_"))||Status.AnimationTransitioning){Fail(TEXT("Conversation did not select permanent standing pose"));return;}if(auto* Book=S.ReadingProps.Find(Character))for(const auto& Part:*Book)if(Part.IsValid()&&!Part->IsHidden()){Fail(TEXT("Book visible during standing conversation"));return;}}
  if(S.PresentationTopicButtons!=0||S.DebugSliders!=OKHeroineStats().Num()||!S.DialoguePanel.IsValid()||S.DialoguePanel->GetCachedGeometry().GetLocalSize().X<400){Fail(TEXT("Collapsed topics/debug/dialogue UI incomplete"));return;}
  const auto DG=S.DialoguePanel->GetCachedGeometry(),RG=S.InteractionPanel->GetCachedGeometry();if(DG.GetAbsolutePosition().Y<400||DG.GetAbsolutePosition().X+DG.GetAbsoluteSize().X>RG.GetAbsolutePosition().X){Fail(TEXT("Bottom dialogue overlaps right choices"));return;}
  FScreenshotRequest::RequestScreenshot(FPaths::GetPath(S.Manifest.SocialReport)/FString::Printf(TEXT("standing.%d.png"),S.PresentationGirl),true,false);
  ++S.PresentationStandChecks;S.Agents[S.PresentationGirl-1].Held=false;
  if(++S.PresentationGirl<=5){S.Target=S.PresentationGirl;S.Agents[S.Target-1].Held=true;S.Male=S.Agents[S.Target-1].Position+FVector(0,-160,0);S.PresentationAt=Now;RefreshUI();return;}
  S.Target=INDEX_NONE;for(auto& A:S.Agents){A.Held=false;A.Dwell=0;}S.PresentationAt=Now;S.PresentationPhase=2;RefreshUI();return;
 }
 if(S.PresentationPhase==2){for(int32 Character=0;Character<6;++Character){FOKRuntimeCharacterStatus Status;if(OKGetCrowdRuntimeCharacterStatus(this,Character,Status)&&Status.ActiveClipId==(Character==0?TEXT("run_male.okanim"):TEXT("run_female.okanim"))&&!Status.AnimationTransitioning)S.PresentationRunActors.Add(Character);}
  if(S.PresentationRunActors.Num()!=6)return;if(OKStreetFemaleRunSpeed>=OKStreetMaleRunSpeed){Fail(TEXT("Female run must be slower than male"));return;}
  FScreenshotRequest::RequestScreenshot(FPaths::GetPath(S.Manifest.SocialReport)/TEXT("running.png"),true,false);S.Target=1;S.Agents[0].Held=true;S.PresentationAt=Now;S.PresentationPhase=3;S.TopicsOpen=true;RefreshUI();return;
 }
 if(S.PresentationPhase==3){if(Now-S.PresentationAt<2)return;if(S.PresentationTopicButtons!=3){Fail(TEXT("Topic menu did not expand"));return;}
  FScreenshotRequest::RequestScreenshot(FPaths::GetPath(S.Manifest.SocialReport)/TEXT("topics.expanded.png"),true,false);
  const float Values[]={42,23,34,56,7},Limits[]={200,100,100,100,20};
  if(S.PresentationStatPhase==0){for(int32 Field=0;Field<5;++Field)SetCharacterStat(1,Field,Values[Field]);S.PresentationStatPhase=1;S.PresentationAt=Now;return;}
  if(S.PresentationStatPhase==1){
   // Slate evaluates bound attributes on its following frame.
   for(int32 Field=0;Field<5;++Field)if(!S.StatSliders.IsValidIndex(Field)||!FMath::IsNearlyEqual(S.StatSliders[Field]->GetValue(),Values[Field]/Limits[Field])){Fail(TEXT("Slider did not reflect the live relation on its next frame"));return;}
   const int32 RelationIndex=OKStreetRelationIndex(S.Store,OKStreetStableId(S.Manifest.Characters[1]),S.Manifest.TalkBudget,true);S.Store.Relations[RelationIndex].Favor=100;
   S.PresentationStatPhase=2;S.PresentationAt=Now;return;
  }
  FOKStreetStore Reload;FString Text,Error;
  if(S.PresentationStatPhase==2&&S.RelationControlsVerified==0){
  if(!FMath::IsNearlyEqual(S.StatSliders[0]->GetValue(),.5f)){Fail(TEXT("Slider did not reflect an external relation update"));return;}
  SetCharacterStat(1,0,42);
  if(!FFileHelper::LoadFileToString(Text,*S.Manifest.SavePath)||OKStreetParseSave(Text,Reload,Error)!=EOKStreetLoad::Ok){Fail(TEXT("Debug slider save could not reload"));return;}
  const auto R=OKStreetViewRelation(Reload,OKStreetStableId(S.Manifest.Characters[1]),S.Manifest.TalkBudget);if(R.Favor!=42||R.Lewdness!=23||R.SexDesire!=34||R.TopicPoints!=56||R.TalksRemaining!=7){Fail(TEXT("Debug stats roundtrip changed values"));return;}
  }
  if(S.PresentationStatPhase==2)S.PresentationStatPhase=6;
  if(S.RelationControlsVerified<15){
   const int32 Girl=1+S.RelationControlsVerified/3,Choice=S.RelationControlsVerified%3;
   const TCHAR* Stages[]={TEXT("stranger"),TEXT("friend"),TEXT("lover")};
   if(S.PresentationStatPhase==6){S.Target=Girl;RefreshUI();S.PresentationStatPhase=3;S.PresentationAt=Now;return;}
   if(S.PresentationStatPhase==3){
    // Exercise the Slate click delegate, including clicking the already selected box.
    auto Check=S.RelationChecks[Choice];Check->ToggleCheckedState();S.PresentationStatPhase=4;S.PresentationAt=Now;return;
   }
   if(S.RelationChecks.Num()!=3){Fail(TEXT("Relationship checkbox count incorrect"));return;}
   for(int32 I=0;I<3;++I)if(S.RelationChecks[I]->IsChecked()!=(I==Choice)){Fail(TEXT("Relationship checkboxes are not exclusive/live"));return;}
   if(!FFileHelper::LoadFileToString(Text,*S.Manifest.SavePath)||OKStreetParseSave(Text,Reload,Error)!=EOKStreetLoad::Ok){Fail(TEXT("Relationship checkbox save did not reload"));return;}
   const auto Selected=OKStreetViewRelation(Reload,OKStreetStableId(S.Manifest.Characters[Girl]),S.Manifest.TalkBudget);
   if(Selected.Stage!=Stages[Choice]){Fail(TEXT("Relationship checkbox did not persist selected stage"));return;}
   if(Girl==1&&(Selected.Favor!=42||Selected.Lewdness!=23||Selected.SexDesire!=34||Selected.TopicPoints!=56||Selected.TalksRemaining!=7)){Fail(TEXT("Relationship checkbox reset numeric stats"));return;}
   ++S.RelationControlsVerified;S.PresentationStatPhase=6;S.PresentationAt=Now;return;
  }
  if(!S.RelationControlsSync){
   if(S.PresentationStatPhase==6){const auto Id=OKStreetStableId(S.Manifest.Characters[S.Target]);S.Store.Relations[OKStreetRelationIndex(S.Store,Id,S.Manifest.TalkBudget,true)].Stage=TEXT("friend");S.PresentationStatPhase=5;S.PresentationAt=Now;return;}
   if(!S.RelationChecks[1]->IsChecked()||S.RelationChecks[0]->IsChecked()||S.RelationChecks[2]->IsChecked()){Fail(TEXT("Relationship controls did not reflect an external stage update"));return;}
   S.RelationControlsSync=true;
  }
  S.Store=S.PresentationStore;OKStreetAtomicSave(S.Manifest.SavePath,OKStreetWriteSave(S.Store),Error);S.PresentationPhase=4;S.PresentationAt=Now;return;
 }
 if(S.PresentationPhase==4&&Now-S.PresentationAt>1)WriteReport(S.Errors.IsEmpty());
}

void AOKStreetSocialScene::RunMinimapProbe(double Now){
 auto& S=*State;auto Fail=[&](const TCHAR* Why){if(S.MinimapProbePhase>0){S.Store=S.MinimapProbeStore;S.MinimapShowNames=S.MinimapProbeShowNames;}S.Errors.Add(Why);WriteReport(false);};
 if(Now-S.StartedAt>120){Fail(TEXT("Minimap runtime probe timeout"));return;}
 if(!S.Minimap.IsValid()||!S.Minimap->Read){Fail(TEXT("Minimap missing live data"));return;}
 if(S.MinimapProbePhase==0){
  const auto North=OKStreetMinimapOffset(S.Male+FVector(100,0,0),S.Male),East=OKStreetMinimapOffset(S.Male+FVector(0,100,0),S.Male),Far=OKStreetMinimapOffset(S.Male+FVector(100000,0,0),S.Male);
  if(North.Y>=0||East.X<=0||!FMath::IsNearlyEqual(Far.Size(),120.0,0.001)||OKStreetMinimapFavorBand(24.99f)!=1||OKStreetMinimapFavorBand(25)!=2||OKStreetMinimapFavorBand(50)!=3||OKStreetMinimapFavorBand(75)!=4){Fail(TEXT("Minimap projection/color boundaries incorrect"));return;}
  if(S.Minimap->Read().Girls.Num()!=5){Fail(TEXT("Minimap omitted ready runtime girls"));return;}
  if(OKStreetMinimapName(OKT(TEXT("ui.scene.chinese_name_test_sixth")))!=OKT(TEXT("ui.scene.chinese_name_test_short"))||OKStreetMinimapName(TEXT("ABCDEFGHIJKLM"))!=TEXT("ABCDEFGHIJ")){Fail(TEXT("Minimap Chinese/Latin prefix length incorrect"));return;}
  // Captions stay fixed beside their dot (overlap allowed) and must remain inside the map, including at map edges.
  for(const FVector2D Anchor:{FVector2D(165,155),FVector2D(45,155),FVector2D(285,155)}){const FVector2D Size(100,14);const auto P=OKStreetMinimapLabel(Anchor,Size);const FSlateRect R(P.X,P.Y,P.X+Size.X,P.Y+Size.Y);if(R.Left<14||R.Right>316||R.Top<32||R.Bottom>278){Fail(TEXT("Minimap caption escaped map bounds"));return;}}
  S.MinimapProbeStore=S.Store;S.MinimapProbeShowNames=S.MinimapShowNames;S.MinimapShowNames=true;const float Favor[]={-1,0,25,50,75};
  for(int32 I=1;I<=5;++I){const auto Id=OKStreetStableId(S.Manifest.Characters[I]);int32 R=OKStreetFindRelation(S.Store,Id);if(R==INDEX_NONE){FOKStreetRelation Relation;Relation.Id=Id;R=S.Store.Relations.Add(Relation);}S.Store.Relations[R].Favor=Favor[I-1];}
  S.MinimapProbeAt=Now;S.MinimapProbePhase=1;return;
 }
 if(Now-S.MinimapProbeAt<.25)return;
 if(S.MinimapProbePhase==1){
  const auto D=S.Minimap->Read();if(S.Minimap->PaintedGirls!=5||S.Minimap->PaintedNames!=5||S.Minimap->PaintedBands.Num()!=5)return;
  for(int32 I=0;I<5;++I)if(S.Minimap->PaintedBands[I]!=I||D.Girls[I].Position!=S.Agents[I].Position){Fail(TEXT("Minimap painted stale location/favor"));return;}
  const auto G=S.Minimap->GetCachedGeometry();if(G.GetLocalSize().IsNearlyZero())return;const auto Panel=S.InteractionPanel->GetCachedGeometry();if(G.GetAbsolutePosition().X>=Panel.GetAbsolutePosition().X||Panel.GetLocalSize().X>450){Fail(TEXT("Minimap/action sidebar layout incorrect"));return;}
  S.MouseInput->PointerDown(G.LocalToAbsolute(G.GetLocalSize()*.5f));if(!S.WorldTouchPointerBlocked){Fail(TEXT("Minimap click leaked into world touch"));return;}
  if(FScreenshotRequest::IsScreenshotRequested())return;FScreenshotRequest::RequestScreenshot(FPaths::GetPath(S.Manifest.SocialReport)/TEXT("street.minimap.colors.png"),true,false);S.MinimapProbeAt=Now;S.MinimapProbePhase=2;return;
 }
 if(Now-S.MinimapProbeAt<.75)return;
 if(S.MinimapProbePhase==2){
  if(IFileManager::Get().FileSize(*(FPaths::GetPath(S.Manifest.SocialReport)/TEXT("street.minimap.colors.png")))<1000){Fail(TEXT("Minimap color screenshot missing"));return;}
  const int32 R=OKStreetFindRelation(S.Store,OKStreetStableId(S.Manifest.Characters[1]));S.Store.Relations[R].Favor=100;S.MinimapShowNames=false;S.MinimapProbeAt=Now;S.MinimapProbePhase=3;return;
 }
 if(S.MinimapProbePhase==3){
  if(S.Minimap->PaintedBands.Num()!=5||S.Minimap->PaintedBands[0]!=4||S.Minimap->PaintedNames!=0){Fail(TEXT("Minimap favor change did not repaint"));return;}
  if(FScreenshotRequest::IsScreenshotRequested())return;FScreenshotRequest::RequestScreenshot(FPaths::GetPath(S.Manifest.SocialReport)/TEXT("street.minimap.updated.png"),true,false);S.MinimapProbeAt=Now;S.MinimapProbePhase=4;return;
 }
 if(IFileManager::Get().FileSize(*(FPaths::GetPath(S.Manifest.SocialReport)/TEXT("street.minimap.updated.png")))<1000){Fail(TEXT("Minimap updated screenshot missing"));return;}
 S.Store=S.MinimapProbeStore;S.MinimapShowNames=S.MinimapProbeShowNames;S.MinimapChecked=true;S.WorldTouchPointerBlocked=false;
}
void AOKStreetSocialScene::RunSplashProbe(double Now){
 auto& S=*State;auto Fail=[&](const TCHAR* Why){FinishPhoto();S.Errors.Add(Why);WriteReport(false);};
 if(S.SplashCasesVerified==10){S.SplashChecked=true;return;}
 const int32 Character=S.SplashCasesVerified/2+1,Kind=3+S.SplashCasesVerified%2;
 if(S.SplashProbePhase==0){
  S.SplashProbeSave=OKStreetWriteSave(S.Store);S.Agents[Character-1].Position=S.Male+FVector(150,0,0);BeginPair(Character,Kind);
  if(S.Pair.Partner!=Character||!S.Pair.Splash){Fail(TEXT("Splash admission failed"));return;}
  if(!S.SplashCancelChecked){FinishPhoto();if(S.Pair.Partner!=INDEX_NONE||!S.PairProps.IsEmpty()){Fail(TEXT("Splash queued cancel retained reservation"));return;}S.SplashCancelChecked=true;BeginPair(Character,Kind);}
  S.SplashProbePhase=1;return;
 }
 if(S.Pair.Partner!=Character||!S.Pair.Splash){Fail(TEXT("Splash ended before verification"));return;}
 if(Now-S.Pair.RequestedAt>55){Fail(TEXT("Splash private prepare timeout"));return;}
 if(S.Pair.Phase==0||Now-S.Pair.PlayingAt<1.3)return;
 FOKRuntimeCharacterStatus Male,Female;FVector A,B;
 if(!OKGetCrowdRuntimeCharacterStatus(this,0,Male)||!OKGetCrowdRuntimeCharacterStatus(this,Character,Female)||Male.ActiveClipId!=S.Pair.MaleClip||Female.ActiveClipId!=S.Pair.FemaleClip||!(OKGetCrowdRuntimeBonePosition(this,0,TEXT("cf_j_head"),A)||OKGetCrowdRuntimeBonePosition(this,0,TEXT("cm_j_head"),A))||!OKGetCrowdRuntimeBonePosition(this,Character,TEXT("cf_j_head"),B)||A.ContainsNaN()||B.ContainsNaN()||FVector::Dist(A,S.Male)>350||FVector::Dist(B,S.Agents[Character-1].Position)>350||S.PairProps.Num()!=16){Fail(TEXT("Splash did not use each actor's private source action"));return;}
 if(S.SplashProbePhase==1){
  int32 VisibleDrops=0;for(auto& Drop:S.PairProps)if(Drop.IsValid()&&!Drop->IsHidden())++VisibleDrops;
  if(VisibleDrops==0){Fail(TEXT("Splash burst did not display authored water"));return;}
  if(FScreenshotRequest::IsScreenshotRequested())return;
  S.SplashProbeSession=S.Pair;S.SplashProbeAt=Now;FScreenshotRequest::RequestScreenshot(FPaths::GetPath(S.Manifest.SocialReport)/FString::Printf(TEXT("splash.%d.%d.png"),Character,Kind-3),true,false);S.SplashProbePhase=2;return;
 }
 if(Now-S.SplashProbeAt<.75)return;
 const FString Shot=FPaths::GetPath(S.Manifest.SocialReport)/FString::Printf(TEXT("splash.%d.%d.png"),Character,Kind-3);if(IFileManager::Get().FileSize(*Shot)<1000){Fail(TEXT("Splash screenshot not saved"));return;}
 const FString LoopShot=FPaths::GetPath(S.Manifest.SocialReport)/FString::Printf(TEXT("splash.%d.%d.loop.png"),Character,Kind-3);
 if(S.SplashProbePhase==2){
  if(Now-S.Pair.PlayingAt<S.SplashCycleSeconds+1.3)return;
  int32 VisibleDrops=0;for(auto& Drop:S.PairProps)if(Drop.IsValid()&&!Drop->IsHidden())++VisibleDrops;
  if(VisibleDrops==0){Fail(TEXT("Splash second source cycle lost authored water"));return;}
  if(FScreenshotRequest::IsScreenshotRequested())return;
  FScreenshotRequest::RequestScreenshot(LoopShot,true,false);S.SplashProbeAt=Now;S.SplashProbePhase=3;return;
 }
 if(IFileManager::Get().FileSize(*LoopShot)<1000){Fail(TEXT("Splash loop screenshot not saved"));return;}
 const auto Before=S.SplashProbeSession;FinishPhoto();
 if(S.Pair.Partner!=INDEX_NONE||!S.PairProps.IsEmpty()||!S.MealHandProps.IsEmpty()||S.Agents[Character-1].Held!=Before.SavedAgent.Held||S.Yaw!=Before.SavedYaw||S.Pitch!=Before.SavedPitch||S.Distance!=Before.SavedDistance||S.MouseUI!=Before.SavedMouseUI||OKStreetWriteSave(S.Store)!=S.SplashProbeSave){Fail(TEXT("Splash cancel did not restore both actors/camera/save"));return;}
 ++S.SplashCasesVerified;S.SplashProbePhase=0;
}
void AOKStreetSocialScene::RunPhotoProbe(double Now){
 auto& S=*State;auto Fail=[&](const FString& Why){S.Pair.CapturePending=false;FinishPhoto();S.Errors.Add(Why);WriteReport(false);};
 const int32 Character=S.PhotoActorsVerified+1;
 if(Character>5){S.PhotoChecked=true;return;}
 if(S.PhotoProbePhase==0){
  S.PhotoProbeSave=OKStreetWriteSave(S.Store);S.Agents[Character-1].Position=S.Male+FVector(150,0,0);
  BeginPhoto(Character);if(S.Pair.Partner!=Character){Fail(TEXT("Paired photo admission failed"));return;}
  if(!S.PhotoCancelChecked){FinishPhoto();if(S.Pair.Partner!=INDEX_NONE){Fail(TEXT("Paired photo preparation cancellation failed"));return;}S.PhotoCancelChecked=true;BeginPhoto(Character);if(S.Pair.Partner!=Character){Fail(TEXT("Paired photo could not retry after cancellation"));return;}}
  S.PhotoProbePhase=1;return;
 }
 if(S.Pair.Partner!=Character&&!(S.PhotoProbePhase==2&&S.PhotoActorsVerified==0&&S.Pair.Partner==INDEX_NONE)){Fail(TEXT("Paired photo ended before verification"));return;}
 if(S.Pair.Partner!=INDEX_NONE&&Now-S.Pair.RequestedAt>55){Fail(TEXT("Paired photo runtime prepare timeout"));return;}
 if(S.Pair.Partner!=INDEX_NONE&&(S.Pair.Phase==0||Now-S.Pair.PlayingAt<2))return;
 if(S.PhotoProbePhase==1){
  FVector MaleHead,FemaleHead;FOKRuntimeActionStreamStatus A,B;
  if(!(OKGetCrowdRuntimeBonePosition(this,0,TEXT("cm_j_head"),MaleHead)||OKGetCrowdRuntimeBonePosition(this,0,TEXT("cf_j_head"),MaleHead))||!OKGetCrowdRuntimeBonePosition(this,Character,TEXT("cf_j_head"),FemaleHead)||MaleHead.ContainsNaN()||FemaleHead.ContainsNaN()||FVector::Dist(MaleHead,S.Male)>350||FVector::Dist(FemaleHead,S.Male)>350||!OKGetCrowdRuntimeActionStatus(this,0,A)||!OKGetCrowdRuntimeActionStatus(this,Character,B)||!A.Playing||!B.Playing||A.ClipId!=S.Pair.MaleClip||B.ClipId!=S.Pair.FemaleClip){Fail(TEXT("Paired photo did not play distinct private actions/bones"));return;}
  CapturePhoto();if(!S.Pair.CapturePending){Fail(TEXT("Paired photo screenshot not requested"));return;}S.PhotoProbeSession=S.Pair;
  if(S.PhotoActorsVerified==0){FinishPhoto();if(S.Pair.Partner!=Character||!S.Pair.FinishRequested){Fail(TEXT("Screenshot cancellation restored actors before capture"));return;}}
  S.PhotoProbePhase=2;return;
 }
 if(S.Pair.CapturePending)return;
 if(S.LastPhoto!=S.PhotoProbeSession.CapturePath||IFileManager::Get().FileSize(*S.LastPhoto)<1000){Fail(TEXT("Paired photo screenshot not saved"));return;}
 const auto Before=S.PhotoProbeSession;FinishPhoto();
 if(S.Pair.Partner!=INDEX_NONE||S.Agents[Character-1].Held!=Before.SavedAgent.Held||S.Yaw!=Before.SavedYaw||S.Pitch!=Before.SavedPitch||S.Distance!=Before.SavedDistance||S.MouseUI!=Before.SavedMouseUI||OKStreetWriteSave(S.Store)!=S.PhotoProbeSave){Fail(TEXT("Paired photo did not restore actors/camera or changed relationship"));return;}
 ++S.PhotoActorsVerified;S.PhotoProbePhase=0;
}


void AOKStreetSocialScene::TickReadingProps(){
 auto& S=*State;
 for(int32 Character=0;Character<S.Manifest.Characters.Num();++Character){
  FOKRuntimeCharacterStatus Status;FTransform Hand;
  // His standing idle is the reading clip too: he only holds a book when they read together.
  const bool Reading=Character!=0&&!(S.Target!=INDEX_NONE&&(Character==0||Character==S.Target))&&OKGetCrowdRuntimeCharacterStatus(this,Character,Status)&&Status.Visible&&Status.Error.IsEmpty()&&!Status.AnimationTransitioning&&Status.ActiveClipId==TEXT("idle_02.okanim")&&OKGetCrowdRuntimeBoneTransform(this,Character,TEXT("k_f_handR_00"),Hand);
  auto* Existing=S.ReadingProps.Find(Character);
  if(!Reading){if(Existing)for(auto& Prop:*Existing)if(Prop.IsValid())Prop->SetActorHiddenInGame(true);continue;}
  if(!Existing){
   auto* Cube=LoadObject<UStaticMesh>(nullptr,TEXT("/Game/Scenes/StreetSocial/Meshes/StreetCube.StreetCube"));if(!Cube)continue;
   auto& Parts=S.ReadingProps.Add(Character);
   for(int32 Part=0;Part<5;++Part){auto* Prop=GetWorld()->SpawnActor<AStaticMeshActor>(Status.Transform.GetLocation(),FRotator::ZeroRotator);if(!Prop)break;Prop->SetOwner(this);Prop->SetActorHiddenInGame(true);auto* C=Prop->GetStaticMeshComponent();C->SetMobility(EComponentMobility::Movable);C->SetStaticMesh(Cube);C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    const TCHAR* Material=Part<2||Part==4?TEXT("/Game/Scenes/StreetSocial/Materials/StreetMaterial04.StreetMaterial04"):TEXT("/Game/Scenes/StreetSocial/Materials/StreetMaterial03.StreetMaterial03");if(auto* M=LoadObject<UMaterialInterface>(nullptr,Material))C->SetMaterial(0,M);Parts.Add(Prop);}
   Existing=&Parts;
  }
  for(int32 Part=0;Part<Existing->Num();++Part)if((*Existing)[Part].IsValid()){
   const float Side=Part%2==0?-1.f:1.f;const FVector Offset=Part==4?FVector(0,0,.3f):FVector(Side*4.f,0,Part<2?.2f:1.f);
   const FVector Size=Part==4?FVector(.8f,15.4f,1.f):FVector(8.f,15.4f,Part<2?.4f:1.2f);
   const FTransform Local(FRotator(0,0,Part==4?0.f:Side*8.f),Offset,Size/100.f);(*Existing)[Part]->SetActorTransform(Local*Hand);(*Existing)[Part]->SetActorHiddenInGame(false);
  }
 }
}
void AOKStreetSocialScene::RunReadingProbe(double Now){
 auto& S=*State;const int32 Character=S.ReadingActorsVerified;
 auto Fail=[&](const FString& Why){S.ReadingForceWalk=INDEX_NONE;if(Character>0&&S.Agents.IsValidIndex(Character-1))S.Agents[Character-1]=S.ReadingSavedAgent;S.Errors.Add(Why);WriteReport(false);};
 if(Character>=6){S.ReadingChecked=true;return;}
 // He holds no book on his own (his standing idle is the reading clip too; TickReadingProps): the heroines are checked.
 if(Character==0&&S.ReadingProbePhase==0){S.ReadingActorsVerified=1;return;}
 if(S.ReadingProbePhase==0){S.ReadingProbeSave=OKStreetWriteSave(S.Store);if(Character>0){S.ReadingSavedAgent=S.Agents[Character-1];S.Agents[Character-1].Held=true;}FString Error;ResumeBehavior(Character,Error);S.ReadingProbeAt=Now;S.ReadingProbePhase=1;return;}
 if(Now-S.ReadingProbeAt>15){FOKRuntimeCharacterStatus Seen;const bool Got=OKGetCrowdRuntimeCharacterStatus(this,Character,Seen);const auto* Have=S.ReadingProps.Find(Character);
  Fail(FString::Printf(TEXT("Reading book visibility/transition timeout (actor %d: clip %s, transitioning %d, visible %d, error \"%s\", book parts %d, target %d)"),Character,Got?*Seen.ActiveClipId:TEXT("?"),Got&&Seen.AnimationTransitioning?1:0,Got&&Seen.Visible?1:0,Got?*Seen.Error:TEXT(""),Have?Have->Num():0,S.Target));return;}
 FOKRuntimeCharacterStatus Status;FTransform Hand;const auto* Parts=S.ReadingProps.Find(Character);
 if(!OKGetCrowdRuntimeCharacterStatus(this,Character,Status))return;
 if(S.ReadingProbePhase==1){
  if(Status.ActiveClipId!=TEXT("idle_02.okanim")||Status.AnimationTransitioning||!Parts||Parts->Num()!=5)return;
  if(!OKGetCrowdRuntimeBoneTransform(this,Character,TEXT("k_f_handR_00"),Hand)||Hand.ContainsNaN()){Fail(TEXT("Reading book hand invalid"));return;}
  for(const auto& Prop:*Parts)if(!Prop.IsValid()||Prop->IsHidden()){Fail(TEXT("Reading book pieces missing"));return;}
  if(FVector::Dist((*Parts)[4]->GetActorLocation(),Hand.TransformPosition(FVector(0,0,.3f)))>.1){Fail(TEXT("Reading book detached from private hand"));return;}
  FVector Head;if(!(OKGetCrowdRuntimeBonePosition(this,Character,TEXT("cf_j_head"),Head)||OKGetCrowdRuntimeBonePosition(this,Character,TEXT("cm_j_head"),Head))){Fail(TEXT("Reading head missing"));return;}
  if(S.Camera.IsValid()){S.Camera->SetActorLocation(Head+FVector(120,180,-15));S.Camera->SetActorRotation((Head-FVector(0,0,35)-S.Camera->GetActorLocation()).Rotation());}
  FScreenshotRequest::RequestScreenshot(FPaths::GetPath(S.Manifest.SocialReport)/FString::Printf(TEXT("street.reading.%d.png"),Character),false,false);S.ReadingProbeAt=Now;S.ReadingProbePhase=2;return;
 }
 if(S.ReadingProbePhase==2){if(Now-S.ReadingProbeAt<.75)return;S.ReadingForceWalk=Character;S.ReadingProbeAt=Now;S.ReadingProbePhase=3;return;}
 if(Status.ActiveClipId!=TEXT("walk_01.okanim")||Status.AnimationTransitioning)return;
 if(!Parts){Fail(TEXT("Reading book lifecycle lost"));return;}for(const auto& Prop:*Parts)if(!Prop.IsValid()||!Prop->IsHidden()){Fail(TEXT("Walking retained a reading book"));return;}
 S.ReadingForceWalk=INDEX_NONE;if(Character>0)S.Agents[Character-1]=S.ReadingSavedAgent;
 if(OKStreetWriteSave(S.Store)!=S.ReadingProbeSave){Fail(TEXT("Reading book changed relationship save"));return;}
 ++S.ReadingActorsVerified;S.ReadingProbePhase=0;
}
void AOKStreetSocialScene::RunDrinkProbe(double Now){
 auto& S=*State;auto Fail=[&](const FString& Why){FinishPhoto();S.Errors.Add(Why);WriteReport(false);};const int32 Character=S.DrinkActorsVerified+1;
 if(Character>5){S.DrinkChecked=true;return;}
 if(S.DrinkProbePhase==0){
  S.DrinkProbeSave=OKStreetWriteSave(S.Store);S.DrinkProbeVoices=S.DrinkVoices;S.Agents[Character-1].Position=S.Male+FVector(150,0,0);BeginPair(Character,2);
  if(S.Pair.Partner!=Character||!S.Pair.Drink){Fail(TEXT("Drink activity admission failed"));return;}
  if(!S.DrinkCancelChecked){FinishPhoto();if(S.Pair.Partner!=INDEX_NONE||S.Target!=INDEX_NONE||!S.PairProps.IsEmpty()){Fail(TEXT("Queued drink cancellation retained reservation/props"));return;}S.DrinkCancelChecked=true;BeginPair(Character,2);}
  S.DrinkProbeSession=S.Pair;S.DrinkProbePhase=1;return;
 }
 if(S.Pair.Partner!=Character||!S.Pair.Drink){Fail(TEXT("Drink activity ended before verification"));return;}
 if(Now-S.Pair.RequestedAt>55){Fail(TEXT("Drink activity preparation timeout"));return;}
 if(S.Pair.Phase!=1||Now-S.Pair.PlayingAt<1)return;
 FOKRuntimeActionStreamStatus A,B;
 if(!OKGetCrowdRuntimeActionStatus(this,0,A)||!OKGetCrowdRuntimeActionStatus(this,Character,B)||!A.Playing||!B.Playing||A.ClipId!=S.DrinkMaleClip||B.ClipId!=S.DrinkFemaleClip||S.PairProps.Num()!=4){Fail(TEXT("Drink activity private pose/props mismatch"));return;}
 for(int32 Participant=0;Participant<2;++Participant){FTransform Hand;if(!OKGetCrowdRuntimeBoneTransform(this,Participant==0?0:Character,TEXT("k_f_handR_00"),Hand)||Hand.ContainsNaN()){Fail(TEXT("Drink activity hand invalid"));return;}
  if(!S.PairProps[Participant*2].IsValid()||S.PairProps[Participant*2]->IsHidden()||FVector::Dist(S.PairProps[Participant*2]->GetActorLocation(),Hand.TransformPosition(FVector(0,0,10)))>.1){Fail(TEXT("Drink prop detached from current private hand"));return;}
  if(S.DrinkProbePhase==1)S.DrinkFirstHand[Participant]=Hand.GetLocation();
  else if(S.DrinkProbePhase==2&&Now-S.DrinkProbeAt>1.25&&FVector::Dist(S.DrinkFirstHand[Participant],Hand.GetLocation())<.5){Fail(TEXT("Drink private hand/prop did not animate"));return;}
 }
 ++S.DrinkSamples;
 auto QueueFixture=[&](){const int32 Node=OKRulesFreshGreeting(S,Character);return Node!=INDEX_NONE&&QueueNode(Node,TEXT("greeting"));};
 if(S.DrinkProbePhase==1){FScreenshotRequest::RequestScreenshot(FPaths::GetPath(S.Manifest.SocialReport)/FString::Printf(TEXT("street.drink.%d.first.png"),Character),false,false);S.DrinkProbeAt=Now;S.DrinkProbePhase=2;return;}
 if(S.DrinkProbePhase==2){if(Now-S.DrinkProbeAt<1.3)return;FScreenshotRequest::RequestScreenshot(FPaths::GetPath(S.Manifest.SocialReport)/FString::Printf(TEXT("street.drink.%d.second.png"),Character),false,false);S.DrinkProbeAt=Now;S.DrinkProbePhase=3;return;}
 if(S.DrinkProbePhase==3){if(Now-S.DrinkProbeAt<.75)return;if(!QueueFixture()){Fail(TEXT("Drink source voice fixture unavailable"));return;}S.DrinkProbePhase=4;return;}
 if(S.DrinkProbePhase==4){if(S.Node!=INDEX_NONE||S.DrinkVoices<=S.DrinkProbeVoices)return;if(!QueueFixture()){Fail(TEXT("Drink active cancellation fixture unavailable"));return;}S.DrinkProbePhase=5;return;}
 if(!OKCrowdRuntimeSpeechActive(this,Character))return;
 const auto Before=S.DrinkProbeSession;EndTalk();EOKRuntimeSpeechState Speech;FString Error;
 if(!OKGetCrowdRuntimeSpeechState(this,Character,Speech,Error)||Speech!=EOKRuntimeSpeechState::Cancelled){Fail(TEXT("Drink active speech was not cancelled"));return;}
 if(S.Pair.Partner!=INDEX_NONE||S.Target!=INDEX_NONE||!S.PairProps.IsEmpty()||!S.MealHandProps.IsEmpty()||S.Agents[Character-1].Held!=Before.SavedAgent.Held||S.Yaw!=Before.SavedYaw||S.Pitch!=Before.SavedPitch||S.Distance!=Before.SavedDistance||S.MouseUI!=Before.SavedMouseUI||OKStreetWriteSave(S.Store)!=S.DrinkProbeSave){Fail(TEXT("Drink activity failed to restore actors/camera/mouse/save"));return;}
 ++S.DrinkActorsVerified;S.DrinkProbePhase=0;
}
void AOKStreetSocialScene::RunMealProbe(double Now){
 auto& S=*State;auto Fail=[&](const FString& Why){FinishPhoto();S.Errors.Add(Why);WriteReport(false);};const int32 Character=S.MealActorsVerified+1;
 if(Character>5){S.MealChecked=true;return;}
 if(S.MealProbePhase==0){
  S.MealProbeSave=OKStreetWriteSave(S.Store);S.MealProbeVoices=S.MealVoices;S.Agents[Character-1].Position=S.Male+FVector(150,0,0);BeginPair(Character,5);
  if(S.Pair.Partner!=Character||!S.Pair.Meal){Fail(TEXT("Meal activity admission failed"));return;}
  if(!S.MealCancelChecked){FinishPhoto();if(S.Pair.Partner!=INDEX_NONE||S.Target!=INDEX_NONE||!S.PairProps.IsEmpty()){Fail(TEXT("Queued meal cancellation retained reservation/props"));return;}S.MealCancelChecked=true;BeginPair(Character,5);}
  S.MealProbeSession=S.Pair;S.MealProbePhase=1;return;
 }
 if(S.Pair.Partner!=Character||!S.Pair.Meal){Fail(TEXT("Meal activity ended before verification"));return;}
 if(Now-S.Pair.RequestedAt>55){Fail(TEXT("Meal activity preparation timeout"));return;}
 if(S.Pair.Phase!=1||Now-S.Pair.PlayingAt<1)return;
 FOKRuntimeActionStreamStatus A,B;
 if(!OKGetCrowdRuntimeActionStatus(this,0,A)||!OKGetCrowdRuntimeActionStatus(this,Character,B)||!A.Playing||!B.Playing||A.ClipId!=FString(TEXT("facility_eat.okanim"))||B.ClipId!=FString(TEXT("facility_eat.okanim"))||S.PairProps.Num()<22||S.MealHandProps.Num()!=4){Fail(TEXT("Meal activity private pose/props mismatch"));return;}
 for(int32 Participant=0;Participant<2;++Participant){FTransform Hand;if(!OKGetCrowdRuntimeBoneTransform(this,Participant==0?0:Character,TEXT("k_f_handR_00"),Hand)||Hand.ContainsNaN()){Fail(TEXT("Meal activity hand invalid"));return;}
  for(int32 Piece=0;Piece<2;++Piece){const auto& Prop=S.MealHandProps[Participant*2+Piece];if(!Prop.Key.IsValid()||Prop.Key->IsHidden()||FVector::Dist(Prop.Key->GetActorLocation(),Hand.TransformPosition(Prop.Value))>.1){Fail(TEXT("Meal prop detached from current private hand"));return;}}

  if(S.MealProbePhase==1)S.MealFirstHand[Participant]=Hand.GetLocation();
  else if(S.MealProbePhase==2&&Now-S.MealProbeAt>1.25&&FVector::Dist(S.MealFirstHand[Participant],Hand.GetLocation())<.5){Fail(TEXT("Meal private hand/prop did not animate"));return;}
 }
 ++S.MealSamples;
 auto QueueFixture=[&](){const int32 Node=OKRulesFreshGreeting(S,Character);return Node!=INDEX_NONE&&QueueNode(Node,TEXT("greeting"));};
 if(S.MealProbePhase==1){FScreenshotRequest::RequestScreenshot(FPaths::GetPath(S.Manifest.SocialReport)/FString::Printf(TEXT("street.meal.%d.first.png"),Character),false,false);S.MealProbeAt=Now;S.MealProbePhase=2;return;}
 if(S.MealProbePhase==2){if(Now-S.MealProbeAt<1.3)return;FScreenshotRequest::RequestScreenshot(FPaths::GetPath(S.Manifest.SocialReport)/FString::Printf(TEXT("street.meal.%d.second.png"),Character),false,false);S.MealProbeAt=Now;S.MealProbePhase=3;return;}
 if(S.MealProbePhase==3){if(Now-S.MealProbeAt<.75)return;if(!QueueFixture()){Fail(TEXT("Meal source voice fixture unavailable"));return;}S.MealProbePhase=4;return;}
 if(S.MealProbePhase==4){if(S.Node!=INDEX_NONE||S.MealVoices<=S.MealProbeVoices)return;if(!QueueFixture()){Fail(TEXT("Meal active cancellation fixture unavailable"));return;}S.MealProbePhase=5;return;}
 if(!OKCrowdRuntimeSpeechActive(this,Character))return;
 const auto Before=S.MealProbeSession;EndTalk();EOKRuntimeSpeechState Speech;FString Error;
 if(!OKGetCrowdRuntimeSpeechState(this,Character,Speech,Error)||Speech!=EOKRuntimeSpeechState::Cancelled){Fail(TEXT("Meal active speech was not cancelled"));return;}
 if(S.Pair.Partner!=INDEX_NONE||S.Target!=INDEX_NONE||!S.PairProps.IsEmpty()||!S.MealHandProps.IsEmpty()||S.Agents[Character-1].Held!=Before.SavedAgent.Held||S.Yaw!=Before.SavedYaw||S.Pitch!=Before.SavedPitch||S.Distance!=Before.SavedDistance||S.MouseUI!=Before.SavedMouseUI||OKStreetWriteSave(S.Store)!=S.MealProbeSave){Fail(TEXT("Meal activity failed to restore actors/camera/mouse/save"));return;}
 ++S.MealActorsVerified;S.MealProbePhase=0;
}
void AOKStreetSocialScene::RunSeatedProbe(double Now){
 auto& S=*State;auto Fail=[&](const FString& Why){FinishPhoto();S.Errors.Add(Why);WriteReport(false);};const int32 Character=S.SeatedActorsVerified+1;
 if(Character>5){S.SeatedChecked=true;return;}
 if(S.SeatedProbePhase==0){
  S.SeatedProbeSave=OKStreetWriteSave(S.Store);S.Agents[Character-1].Position=S.Male+FVector(150,0,0);S.SeatedProbeVoices=S.SeatedVoices;
  BeginPair(Character,true);if(S.Pair.Partner!=Character||!S.Pair.Seated){Fail(TEXT("Seated conversation admission failed"));return;}
  if(!S.SeatedCancelChecked){FinishPhoto();if(S.Pair.Partner!=INDEX_NONE||S.Target!=INDEX_NONE){Fail(TEXT("Queued seated cancellation retained reservation"));return;}S.SeatedCancelChecked=true;BeginPair(Character,true);}
  S.SeatedProbeSession=S.Pair;S.SeatedProbePhase=1;return;
 }
 if(S.Pair.Partner!=Character||!S.Pair.Seated){Fail(TEXT("Seated conversation ended before verification"));return;}
 if(Now-S.Pair.RequestedAt>55){Fail(TEXT("Seated conversation preparation or audio timeout"));return;}
 if(S.Pair.Phase==0||Now-S.Pair.PlayingAt<2)return;
 FOKRuntimeActionStreamStatus A,B;FVector MaleHead,FemaleHead;
 if(!OKGetCrowdRuntimeActionStatus(this,0,A)||!OKGetCrowdRuntimeActionStatus(this,Character,B)||!A.Playing||!B.Playing||A.ClipId!=S.SeatedMaleClip||B.ClipId!=S.SeatedFemaleClip||!(OKGetCrowdRuntimeBonePosition(this,0,TEXT("cm_j_head"),MaleHead)||OKGetCrowdRuntimeBonePosition(this,0,TEXT("cf_j_head"),MaleHead))||!OKGetCrowdRuntimeBonePosition(this,Character,TEXT("cf_j_head"),FemaleHead)||MaleHead.ContainsNaN()||FemaleHead.ContainsNaN()||FVector::Dist(MaleHead,S.Male)>350||FVector::Dist(FemaleHead,S.Agents[Character-1].Position)>350){Fail(TEXT("Seated voice replaced a private seated pose or invalid bones"));return;}
 // The test queues a source-matched greeting fixture, independent of an exhausted day's eligibility.
 // Production seating continues the current conversation and never forces another greeting.
 auto QueueFixture=[&](){const int32 Node=OKRulesFreshGreeting(S,Character);return Node!=INDEX_NONE&&QueueNode(Node,TEXT("greeting"));};
 if(S.SeatedProbePhase==1){
  if(S.Node==INDEX_NONE&&S.SeatedVoices==S.SeatedProbeVoices){if(!QueueFixture())Fail(TEXT("Seated source voice fixture unavailable"));return;}
  if(S.Node!=INDEX_NONE||S.SeatedVoices<=S.SeatedProbeVoices)return;
  if(OKStreetWriteSave(S.Store)!=S.SeatedProbeSave){Fail(TEXT("Seated greeting fabricated a relationship reward"));return;}
  UE_LOG(LogTemp,Display,TEXT("Seated private heads actor=%d male=%s female=%s"),Character,*MaleHead.ToString(),*FemaleHead.ToString());
  FScreenshotRequest::RequestScreenshot(FPaths::GetPath(S.Manifest.SocialReport)/FString::Printf(TEXT("street.seated.%d.png"),Character),false,false);S.SeatedProbeAt=Now;S.SeatedProbePhase=2;return;
 }
 if(S.SeatedProbePhase==2){
  if(Now-S.SeatedProbeAt<.75)return;
  if(!QueueFixture()||S.Node==INDEX_NONE){Fail(TEXT("Seated audio cancellation probe could not queue voice"));return;}S.SeatedProbePhase=3;return;
 }
 if(!OKCrowdRuntimeSpeechActive(this,Character))return;
 const auto Before=S.SeatedProbeSession;EndTalk();EOKRuntimeSpeechState Speech;FString Error;
 if(S.Pair.Partner!=INDEX_NONE||S.Target!=INDEX_NONE||S.PairProps.Num()!=0||S.Agents[Character-1].Held!=Before.SavedAgent.Held||S.Yaw!=Before.SavedYaw||S.Pitch!=Before.SavedPitch||S.Distance!=Before.SavedDistance||S.MouseUI!=Before.SavedMouseUI||OKStreetWriteSave(S.Store)!=S.SeatedProbeSave||!OKGetCrowdRuntimeSpeechState(this,Character,Speech,Error)||Speech!=EOKRuntimeSpeechState::Cancelled){Fail(TEXT("Seated audio cancellation failed to restore actors/camera/save"));return;}
 ++S.SeatedActorsVerified;S.SeatedProbePhase=0;
}

void AOKStreetSocialScene::BeginCompanion(int32 Character){
 StopAmbientActivity();
 if(!State)return;auto& S=*State;
 if(S.PrivateGirl!=INDEX_NONE)return;
 if(S.Pair.Partner!=INDEX_NONE||S.Companion!=INDEX_NONE||S.Facility!=INDEX_NONE||!S.Agents.IsValidIndex(Character-1)||OKStreetConversationBusy(S.Node!=INDEX_NONE,S.CollectedActive,S.RelationshipPending,S.TouchActive))return;
 FOKRuntimeCharacterStatus Male,Npc;
 if(!OKGetCrowdRuntimeCharacterStatus(this,0,Male)||!OKGetCrowdRuntimeCharacterStatus(this,Character,Npc)||!Male.Visible||!Npc.Visible||!Male.Error.IsEmpty()||!Npc.Error.IsEmpty()||FVector::Dist2D(S.Male,S.Agents[Character-1].Position)>250)return;
 EndTalk();S.Companion=Character;S.Agents[Character-1].Held=true;S.CompanionTrail.Reset();S.CompanionTrail.Add(S.Male);S.CompanionLastMale=S.Male;S.CompanionBlockedAt=0;S.CompanionTravel=0;S.CompanionWalk=0;
 FString Error;ResumeBehavior(0,Error);ResumeBehavior(Character,Error);
 S.MouseUI=false;ApplyMouseMode(S.ViewportFocused);S.Message=OKT(TEXT("ui.scene.walk_explore_hint"));RefreshUI();
}
void AOKStreetSocialScene::FinishCompanion(){
 if(!State||State->Companion==INDEX_NONE)return;auto& S=*State;
 MindRemember(S.Companion,TEXT("walk"),OKMsg(TEXT("ui.memory.walked_together"),{{TEXT("place"),([&](const FString& Id){const FOKStreetPlace* P=OKStreetFindPlace(Id);return P?FString(P->Name):Id.IsEmpty()?FString(OKT(TEXT("ui.etiquette.street_location"))):Id;})(PlaceHere(S.Male))}}),PlaceHere(S.Male),0.15f);
 if(S.Agents.IsValidIndex(S.Companion-1)){auto& Agent=S.Agents[S.Companion-1];Agent.Held=false;Agent.Mode=EOKStreetMove::Walking;Agent.Stuck=0;}
 FString Error;ResumeBehavior(S.Companion,Error);S.Companion=INDEX_NONE;S.CompanionTrail.Reset();S.CompanionBlockedAt=0;S.CompanionWalk=0;
 S.Message=OKT(TEXT("ui.scene.walk_ended"));RefreshUI();
}
float AOKStreetSocialScene::StepCompanion(float Dt){
 auto& S=*State;if(S.Companion==INDEX_NONE)return 0;
 // She does not give up on him: only a missing character ends the walk. Far behind, a long trail, or a character that
 // is not drawn yet never ends it; she is stood beside him, the trail is trimmed, and the walk goes on.
 if(!S.Agents.IsValidIndex(S.Companion-1)){FinishCompanion();return 0;}
 // Walking with several of them: the group's formation (TickParty) walks her, not her trail behind him.
 if(S.GroupWalkPartner==S.Companion&&!S.Party.IsEmpty()){S.CompanionTrail.Reset();S.CompanionTrail.Add(S.Male);S.CompanionLastMale=S.Male;return S.GroupWalkPartnerSpeed;}
 auto& Agent=S.Agents[S.Companion-1];
 auto StandBeside=[&]()->bool{
  for(int32 K=0;K<12;++K){
   const FVector Try=S.Male+FRotator(0,K*30.f,0).RotateVector(FVector(130,0,0));bool Free=!OKStreetBlockedPoint(Try,28);
   for(const auto& Other:S.Agents)if(Other.Slot!=Agent.Slot&&FVector::Dist2D(Try,Other.Position)<70)Free=false;
   if(Free){Agent.Position=FVector(Try.X,Try.Y,Agent.Position.Z);S.CompanionBlockedAt=0;S.CompanionTrail.Reset();S.CompanionTrail.Add(S.Male);S.CompanionLastMale=S.Male;return true;}
  }
  return false;
 };
 if(S.CompanionTrail.Num()>128)S.CompanionTrail.RemoveAt(0,S.CompanionTrail.Num()-128);
 if(FVector::Dist2D(S.Male,Agent.Position)>900&&StandBeside()){UE_LOG(LogTemp,Display,TEXT("Street companion caught up: actor=%d was too far behind, stood beside him"),S.Companion);return 0;}
 if(FVector::Dist2D(S.CompanionLastMale,S.Male)>=25){S.CompanionTrail.Add(S.Male);S.CompanionLastMale=S.Male;}
 while(S.CompanionTrail.Num()>1&&FVector::Dist2D(Agent.Position,S.CompanionTrail[0])<28)S.CompanionTrail.RemoveAt(0);
 const float Gap=FVector::Dist2D(S.Male,Agent.Position);
 {
  const double NowP=SceneTime();
  if(S.CompanionGapFor!=S.Companion||Gap<S.CompanionGapRef-30.0){S.CompanionGapFor=S.Companion;S.CompanionGapRef=Gap;S.CompanionGapRefAt=NowP;}
  else if(Gap>200.f&&NowP-S.CompanionGapRefAt>6.0){
   S.CompanionGapRefAt=NowP;
   if(StandBeside()){UE_LOG(LogTemp,Display,TEXT("Street companion unstuck: actor=%d made no progress for 6 s at gap %.0f, stood beside him"),S.Companion,Gap);return 0;}
  }
 }
 if(S.CompanionTrail.IsEmpty()||(S.CompanionTrail.Num()==1&&Gap<=95)){S.CompanionBlockedAt=0;return 0;}
 FVector Delta=S.CompanionTrail[0]-Agent.Position;Delta.Z=0;const float Distance=Delta.Size();
 if(Distance<1)return 0;
 const float Limit=S.CompanionTrail.Num()==1?FMath::Max(0.f,Distance-85):Distance;
 // Jogging together (a stroll at a run): she keeps running beside him, not walking until she falls 2 m behind.
 const float Rate=Gap>180||(S.OutingRun&&Gap>110)?OKStreetMaleRunSpeed*1.15f:OKStreetMaleWalkSpeed;const FVector Step=Delta/Distance*FMath::Min(Limit,Rate*FMath::Clamp(Dt,0.f,.05f));const FVector Candidate=Agent.Position+Step;
 FHitResult Hit;FCollisionQueryParams Query(SCENE_QUERY_STAT(StreetCompanion),false,this);
 // One step is blocked by a wall, by the player, or by someone she would walk into. Stepping away from someone she
 // already overlaps is allowed, so she never gets stuck inside another heroine.
 // Already standing against something (where she stopped when he spoke to her): steps are judged by a tighter margin and
 // without the sweep, which starts inside the geometry and would block every direction. Why: 1 grid, 2 sweep, 4 someone, 8 him.
 const bool Boxed=OKStreetBlockedPoint(Agent.Position,28);int32 Why=0;
 auto IsBlocked=[&](const FVector& To){
  Why=0;
  if(OKStreetBlockedPoint(To,Boxed?12.f:28.f))Why|=1;
  else if(!Boxed&&GetWorld()->SweepSingleByChannel(Hit,Agent.Position+FVector(0,0,85),To+FVector(0,0,85),FQuat::Identity,ECC_WorldStatic,FCollisionShape::MakeCapsule(28,78),Query))Why|=2;
  for(const auto& Other:S.Agents)if(Other.Slot!=Agent.Slot){const float After=FVector::Dist2D(To,Other.Position);if(After<55&&After<FVector::Dist2D(Agent.Position,Other.Position))Why|=4;}
  if(FVector::Dist2D(To,S.Male)<65)Why|=8;
  return Why!=0;
 };
 // Someone standing or sitting in the way (a heroine at her post on the route): go round her instead of waiting.
 FVector Chosen=Candidate;bool Blocked=IsBlocked(Candidate);const int32 BlockedWhy=Why;
 if(Blocked)for(const float Turn:{50.f,-50.f,95.f,-95.f}){const FVector Alt=Agent.Position+Step.RotateAngleAxis(Turn,FVector::UpVector);if(!IsBlocked(Alt)){Chosen=Alt;Blocked=false;break;}}
 if(Blocked){
  const double Now=SceneTime();
  if(S.CompanionBlockedAt==0){S.CompanionBlockedAt=Now;UE_LOG(LogTemp,Display,TEXT("Street companion blocked: actor=%d at (%.0f,%.0f) boxed=%d why=%d gap=%.0f"),S.Companion,Agent.Position.X,Agent.Position.Y,Boxed?1:0,BlockedWhy,Gap);}
  else if(Now-S.CompanionBlockedAt>3&&Gap>150){
   // Still stuck: stand her beside him instead of leaving both waiting for each other. She never gives up on
   // him: if no free spot is found, the next try comes in 3 s (the walk ends only by Esc or her walking off).
   S.CompanionBlockedAt=Now;
   if(StandBeside()){UE_LOG(LogTemp,Display,TEXT("Street companion unstuck: actor=%d stood beside him at (%.0f,%.0f)"),S.Companion,Agent.Position.X,Agent.Position.Y);return 0;}
  }
  return 0;
 }
 S.CompanionBlockedAt=0;const FVector Moved=Chosen-Agent.Position;Agent.Position=Chosen;S.Rotations[Agent.Slot]=OKStreetFacingMovement(Moved,S.Rotations[Agent.Slot]);S.CompanionTravel+=Moved.Size();
 return Dt>0?Step.Size()/(FMath::Min(Dt,.05f)*OKStreetWalkSpeed):0;
}
void AOKStreetSocialScene::RunCompanionProbe(double Now){
 auto& S=*State;auto Fail=[&](const FString& Why){FinishCompanion();S.Errors.Add(Why);WriteReport(false);};
 if(S.CompanionProbePhase==0){
  S.CompanionProbeMale=S.Male;S.CompanionProbeAgent=S.Agents[0];S.CompanionProbeRotation=S.Rotations[1];S.CompanionProbeMaleRotation=S.Rotations[0];S.CompanionProbeSave=OKStreetWriteSave(S.Store);
  S.Male=FVector(0,-700,0);S.Agents[0].Position=S.Male-FVector(0,130,0);S.Agents[0].Held=true;
  BeginCompanion(1);if(S.Companion!=1){Fail(TEXT("Shared walk admission failed"));return;}S.CompanionProbeAt=Now;S.CompanionProbePhase=1;return;
 }
 if(S.Companion!=1){Fail(TEXT("Shared walk ended unexpectedly"));return;}
 if(S.CompanionProbePhase==1){
  if(Now-S.CompanionProbeAt<2)return;
  FOKRuntimeCharacterStatus Npc;if(S.CompanionTravel<40||!OKGetCrowdRuntimeCharacterStatus(this,1,Npc)||FVector::Dist(Npc.Transform.GetLocation(),S.Agents[0].Position)>3){Fail(TEXT("Shared walk did not move/submit the private NPC"));return;}
  FScreenshotRequest::RequestScreenshot(FPaths::GetPath(S.Manifest.SocialReport)/TEXT("shared.walk.png"),true,false);
  S.CompanionProbeAt=Now;S.CompanionProbePhase=2;return;
 }
 if(S.CompanionProbePhase==2){
  if(Now-S.CompanionProbeAt<2)return;
  if(S.CompanionWalk>.01f||FVector::Dist2D(S.Male,S.Agents[0].Position)>100){Fail(FString::Printf(TEXT("Shared walk did not stop beside the player (walk %.3f, gap %.0f, travel %.0f, trail %d, at %s, player %s, nav %d/%d, facility %d, outing %d, follow %d)"),S.CompanionWalk,FVector::Dist2D(S.Male,S.Agents[0].Position),S.CompanionTravel,S.CompanionTrail.Num(),*S.Agents[0].Position.ToCompactString(),*S.Male.ToCompactString(),S.NavGirl,S.NavPointActive?1:0,S.Facility,S.OutingGirl,S.FollowMode));return;}
  const FVector End=S.Agents[0].Position;
  // Production collision path: another NPC occupies the next breadcrumb.
  const auto Other=S.Agents[1];const auto Trail=S.CompanionTrail;
  S.CompanionTrail.Reset();S.CompanionTrail.Add(End+FVector(0,100,0));S.CompanionTrail.Add(S.Male);
  // She may step round (the sidestep), never into the one standing there.
  const FVector Blocker=End+FVector(0,5,0);S.Agents[1].Position=Blocker;StepCompanion(.05f);const FVector After=S.Agents[0].Position;S.Agents[0].Position=End;S.Agents[1]=Other;S.CompanionTrail=Trail; // the probe step is undone
  if(FVector::Dist2D(After,Blocker)<FVector::Dist2D(End,Blocker)-.01f){Fail(FString::Printf(TEXT("Shared walk walked into an occupied path (%.1f -> %.1f)"),FVector::Dist2D(End,Blocker),FVector::Dist2D(After,Blocker)));return;}
  FinishCompanion();
  if(S.Agents[0].Held||S.Agents[0].Position!=End||!S.CompanionTrail.IsEmpty()||OKStreetWriteSave(S.Store)!=S.CompanionProbeSave){Fail(TEXT("Shared walk exit teleported actor, retained reservation or changed relationship"));return;}
  BeginCompanion(1);if(S.Companion!=1){Fail(TEXT("Shared walk could not restart after cancel"));return;}FinishCompanion();
  S.Male=S.CompanionProbeMale;S.Agents[0]=S.CompanionProbeAgent;S.Rotations[1]=S.CompanionProbeRotation;S.Rotations[0]=S.CompanionProbeMaleRotation;S.CompanionChecked=true;S.CompanionProbePhase=3;
 }
}

void AOKStreetSocialScene::BeginFacility(int32 Kind,int32 Character){
 StopAmbientActivity();
 if(!State||Kind<0||Kind>2)return;auto& S=*State;
 if(S.PrivateGirl!=INDEX_NONE)return;
 if(S.Pair.Partner!=INDEX_NONE||S.Companion!=INDEX_NONE||S.Facility!=INDEX_NONE||!S.Agents.IsValidIndex(Character-1)||OKStreetConversationBusy(S.Node!=INDEX_NONE,S.CollectedActive,S.RelationshipPending,S.TouchActive))return;
 FOKRuntimeCharacterStatus Status;if(!OKGetCrowdRuntimeCharacterStatus(this,Character,Status)||!Status.Visible)return;
 auto* Cube=LoadObject<UStaticMesh>(nullptr,TEXT("/Game/Scenes/StreetSocial/Meshes/StreetCube.StreetCube"));
 if(!Cube){S.Message=OKT(TEXT("ui.scene.facility_grid_not_ready"));RefreshUI();return;}
 EndTalk();S.Facility=Kind;S.FacilityCharacter=Character;S.FacilitySavedAgent=S.Agents[Character-1];S.FacilitySavedRotation=S.Rotations[Character];S.FacilitySavedMaleRotation=S.Rotations[0];S.FacilitySavedMale=S.Male;S.FacilitySavedYaw=S.Yaw;S.FacilitySavedPitch=S.Pitch;S.FacilitySavedDistance=S.Distance;S.FacilityRequestedAt=SceneTime();S.FacilityStartedAt=0;S.FacilityEntered=false;S.FacilitySavedMouse=S.MouseUI;S.NavGirl=INDEX_NONE;S.NavPath.Reset();
 // In a place: right where they stand, facing his way, with the props only (no room shell, no collision). Elsewhere
 // (and in the self-test probes) the independent authored room off the map.
 const bool InPlace=!S.SelfTest&&!PlaceHere(S.Male).IsEmpty();
 S.FacilityInPlace=InPlace;S.FacilityRotation=InPlace?FRotator(0,S.Rotations[0].Yaw,0):FRotator::ZeroRotator;
 S.FacilityOrigin=InPlace?FVector(S.Male.X,S.Male.Y,OKStreetFloorZ(S.Male)):FVector(7000.f,Kind*1600.f,0);
 const FVector Origin=S.FacilityOrigin;const FRotator Turn=S.FacilityRotation;
 auto Part=[&](FVector Position,FVector Size,int32 Material,bool Collision=true){
  if(InPlace)Collision=false;
  auto* Actor=GetWorld()->SpawnActor<AStaticMeshActor>(Origin+Turn.RotateVector(Position),Turn);if(!Actor)return;
  Actor->SetOwner(this);Actor->SetActorHiddenInGame(true);Actor->SetActorScale3D(Size/100.f);auto* C=Actor->GetStaticMeshComponent();C->SetMobility(EComponentMobility::Movable);C->SetStaticMesh(Cube);
  const FString Path=FString::Printf(TEXT("/Game/Scenes/StreetSocial/Materials/StreetMaterial%02d.StreetMaterial%02d"),Material,Material);
  if(auto* M=LoadObject<UMaterialInterface>(nullptr,*Path))C->SetMaterial(0,M);
  C->SetCollisionEnabled(Collision?ECollisionEnabled::QueryAndPhysics:ECollisionEnabled::NoCollision);S.FacilityProps.Add(Actor);
 };
 if(!InPlace){
 Part(FVector(0,0,-10),FVector(1100,1100,20),0);Part(FVector(0,510,180),FVector(1100,20,360),3);Part(FVector(-540,0,180),FVector(20,1100,360),3);Part(FVector(540,0,180),FVector(20,1100,360),3);
 // Rear wall panels and timber trim make each room visibly distinct from street.
 for(int32 I=-2;I<=2;++I){Part(FVector(I*190,495,170),FVector(12,12,310),8);Part(FVector(I*190,490,235),FVector(150,10,95),Kind==0?5:Kind==1?6:4,false);}
 }
 FVector Anchor=Origin;
 if(Kind==0){
  Part(FVector(0,72,69),FVector(180,100,8),8);for(float X:{-70.f,70.f})for(float Y:{37.f,107.f})Part(FVector(X,Y,33),FVector(7,7,66),8);
  Part(FVector(0,-15,41),FVector(47,45,6),8);Part(FVector(0,-40,71),FVector(47,6,60),8);Part(FVector(0,70,76),FVector(35,28,3),7,false);
  Part(FVector::ZeroVector,FVector(.8f,13.f,.8f),11,false);if(!S.FacilityProps.IsEmpty())S.FacilityHandProps.Add({S.FacilityProps.Last(),FVector(0,5,0)});
  Part(FVector::ZeroVector,FVector(3.f,4.f,.6f),11,false);if(!S.FacilityProps.IsEmpty())S.FacilityHandProps.Add({S.FacilityProps.Last(),FVector(0,12,0)});
 }else{
  const float Height=Kind==1?24.f:68.f;Part(FVector(0,15,Height-12),FVector(110,220,24),8);Part(FVector(0,15,Height+3),FVector(108,218,6),Kind==1?7:6);Part(FVector(0,95,Height+10),FVector(75,35,12),7,false);
  Anchor.Z=Origin.Z+Height+6;Part(FVector(180,95,30),FVector(65,65,60),8);Part(FVector(180,95,64),FVector(12,12,10),Kind==2?5:7,false);
 }
 S.Agents[Character-1].Held=true;S.FacilityAnchor=Anchor;
 FString Error;
 const TCHAR* Clips[]={TEXT("facility_eat.okanim"),TEXT("facility_sleep.okanim"),TEXT("facility_esthetic.okanim")};
 if(!OKPrewarmCrowdRuntimeAction(this,Character,Clips[Kind],Error)){FinishFacility();S.Message=OKT(TEXT("ui.scene.facility_action_failed_label"))+Error;RefreshUI();return;}
 const TCHAR* Names[]={OKT(TEXT("ui.scene.dining_label")),OKT(TEXT("ui.phone_inbox.activity_sleep")),OKT(TEXT("ui.scene.essential_oil_care"))};S.Message=OKMsg(TEXT("ui.scene.facility_preparing"), {{TEXT("kind_name"), Names[Kind]}});RefreshUI();
}
void AOKStreetSocialScene::FinishFacility(){
 if(!State||State->Facility==INDEX_NONE)return;auto& S=*State;FString Error;const int32 FacilityGirl=S.FacilityCharacter;
 ResumeBehavior(S.FacilityCharacter,Error);
 if(S.Agents.IsValidIndex(S.FacilityCharacter-1)){S.Agents[S.FacilityCharacter-1]=S.FacilitySavedAgent;S.Rotations[S.FacilityCharacter]=S.FacilitySavedRotation;}
 if(S.FacilityEntered){S.Male=S.FacilitySavedMale;S.Rotations[0]=S.FacilitySavedMaleRotation;S.Yaw=S.FacilitySavedYaw;S.Pitch=S.FacilitySavedPitch;S.Distance=S.FacilitySavedDistance;S.MouseUI=S.FacilitySavedMouse;ApplyMouseMode(S.ViewportFocused);}
 for(auto& Prop:S.FacilityProps)DestroyLater(Prop.Get());S.FacilityProps.Reset();S.FacilityHandProps.Reset();S.Facility=INDEX_NONE;S.FacilityCharacter=INDEX_NONE;S.FacilityStartedAt=0;S.FacilityEntered=false;
 S.Message=OKT(TEXT("ui.scene.left_facility_returned"));RefreshUI();ResumeTalkQuiet(FacilityGirl);
}
void AOKStreetSocialScene::TickFacility(double Now){
 auto& S=*State;if(S.Facility==INDEX_NONE)return;FOKRuntimeCharacterStatus Character;FOKRuntimeActionStreamStatus Action;
 if(!OKGetCrowdRuntimeCharacterStatus(this,S.FacilityCharacter,Character)||!Character.Error.IsEmpty()||!OKGetCrowdRuntimeActionStatus(this,S.FacilityCharacter,Action)||(!S.FacilityEntered&&Now-S.FacilityRequestedAt>60)){FinishFacility();return;}
 const TCHAR* Clips[]={TEXT("facility_eat.okanim"),TEXT("facility_sleep.okanim"),TEXT("facility_esthetic.okanim")};const bool Playing=Action.Playing&&!Action.Pending&&!Action.Preparing&&Action.ClipId==Clips[S.Facility];
 if(!S.FacilityEntered){
  S.Rotations[S.FacilityCharacter]=S.FacilityRotation;
  if(FMath::Abs(FMath::FindDeltaAngleDegrees(S.RenderedRotations[S.FacilityCharacter].Yaw,S.FacilityRotation.Yaw))>.1f)return;
  if(!Action.Ready||Action.Pending||Action.Preparing||Action.ClipId!=Clips[S.Facility])return;
  FString Error;if(!OKPlayCrowdRuntimeAction(this,S.FacilityCharacter,Clips[S.Facility],Error)){FinishFacility();S.Message=OKT(TEXT("ui.scene.facility_action_cancelled"));RefreshUI();return;}
  // Snapshot departure at admission: the player may move while the private pose is prepared.
  S.FacilitySavedMale=S.Male;S.FacilitySavedMaleRotation=S.Rotations[0];S.FacilitySavedYaw=S.Yaw;S.FacilitySavedPitch=S.Pitch;S.FacilitySavedDistance=S.Distance;S.FacilitySavedMouse=S.MouseUI;
  S.Agents[S.FacilityCharacter-1].Position=S.FacilityAnchor;S.Rotations[S.FacilityCharacter]=S.FacilityRotation;
  const FVector Origin=S.FacilityOrigin;S.Male=Origin+S.FacilityRotation.RotateVector(FVector(170,-260,0));S.Rotations[0]=S.FacilityRotation;S.Yaw=S.FacilityRotation.Yaw+90;S.Pitch=-10;S.Distance=350;S.MouseUI=true;
  S.FacilityEntered=true;S.FacilityStartedAt=Now;for(auto& Prop:S.FacilityProps)if(Prop.IsValid())Prop->SetActorHiddenInGame(false);
  ApplyMouseMode(S.ViewportFocused);S.Message=OKT(TEXT("ui.rules.facility_free_look_hint"));RefreshUI();return;
 }
 if(!Playing){FinishFacility();return;}
 if(!S.FacilityHandProps.IsEmpty()){
  FTransform Hand;const bool Found=OKGetCrowdRuntimeBoneTransform(this,S.FacilityCharacter,TEXT("k_f_handR_00"),Hand)||OKGetCrowdRuntimeBoneTransform(this,S.FacilityCharacter,TEXT("cf_j_hand_R"),Hand);
  for(auto& Item:S.FacilityHandProps)if(Item.Key.IsValid()){Item.Key->SetActorHiddenInGame(!Found||!Playing);if(Found)Item.Key->SetActorLocationAndRotation(Hand.TransformPosition(Item.Value),Hand.GetRotation());}
 }
 // Use continues until explicit exit or an actual actor/action failure. Preparation has its own deadline.
}
void AOKStreetSocialScene::RunFacilityProbe(double Now){
 auto& S=*State;
 auto Fail=[&](const FString& Message){S.Errors.Add(Message);FinishFacility();WriteReport(false);};
 if(S.FacilityProbePhase==6){S.FacilitiesChecked=true;return;}
 const int32 Kind=S.FacilityProbePhase/2;
 if(S.FacilityProbePhase%2==0){
  S.FacilityScreenshotAt=0;
  S.FacilityProbeSave=OKStreetWriteSave(S.Store);BeginFacility(Kind,Kind+1);RebuildUI(); // the exit button is set when the panel is built
  if(S.Facility!=Kind||S.FacilityEntered||S.Male!=S.FacilitySavedMale||S.Agents[Kind].Position!=S.FacilitySavedAgent.Position||!S.FacilityExitUI){Fail(TEXT("Facility moved actors before background preparation"));return;}
  const FVector Male=S.Male,Position=S.Agents[Kind].Position;FinishFacility();
  if(S.Facility!=INDEX_NONE||!S.FacilityProps.IsEmpty()||S.Male!=Male||S.Agents[Kind].Position!=Position||OKStreetWriteSave(S.Store)!=S.FacilityProbeSave){Fail(TEXT("Facility queued cancel moved actors or retained props"));return;}
  ++S.FacilityPrepareCancels;BeginFacility(Kind,Kind+1);if(S.Facility!=Kind||S.FacilityEntered){Fail(TEXT("Facility retry failed"));return;}
  ++S.FacilityProbePhase;return;
 }
 if(S.Facility!=Kind){Fail(TEXT("Facility ended before animation verification"));return;}
 if(!S.FacilityEntered&&Now-S.FacilityRequestedAt>60){Fail(TEXT("Facility private runtime animation timed out"));return;}
 if(S.FacilityStartedAt==0||Now-S.FacilityStartedAt<2)return;
 FVector Head;FOKRuntimeActionStreamStatus Action;
 if(!OKGetCrowdRuntimeBonePosition(this,Kind+1,TEXT("cf_j_head"),Head)||Head.ContainsNaN()||FVector::Dist(Head,S.Agents[Kind].Position)>350||!OKGetCrowdRuntimeActionStatus(this,Kind+1,Action)||!Action.Playing){Fail(TEXT("Facility animation/bone verification failed"));return;}
 const TCHAR* Names[]={TEXT("eat"),TEXT("sleep"),TEXT("esthetic")};
 if(S.FacilityScreenshotAt==0){S.FacilityRequestedAt=Now-61;TickFacility(Now);if(S.Facility!=Kind||!S.FacilityEntered||!S.FacilityExitUI){Fail(TEXT("Facility active use incorrectly expired with preparation clock"));return;}++S.FacilityClockChecks;FScreenshotRequest::RequestScreenshot(FPaths::GetPath(S.Manifest.SocialReport)/FString::Printf(TEXT("facility.%s.png"),Names[Kind]),true,false);S.FacilityScreenshotAt=Now;return;}
 if(Now-S.FacilityScreenshotAt<.5)return;
 const FVector Male=S.FacilitySavedMale,Position=S.FacilitySavedAgent.Position;const bool Held=S.FacilitySavedAgent.Held,Mouse=S.FacilitySavedMouse;FinishFacility();
 if(S.Male!=Male||S.Agents[Kind].Position!=Position||S.Agents[Kind].Held!=Held||S.MouseUI!=Mouse||!S.FacilityProps.IsEmpty()||OKStreetWriteSave(S.Store)!=S.FacilityProbeSave){Fail(TEXT("Facility exit failed to restore movement/props or changed relationship save"));return;}
 ++S.FacilitiesVerified;++S.FacilityProbePhase;
}
// Street deadlines exclude time spent in the shared interaction mode.
double AOKStreetSocialScene::SceneTime() const {
 const double Wall=FPlatformTime::Seconds();
 return State?(State->PrivateGirl!=INDEX_NONE?State->HighPrivateAt:Wall)-State->PrivatePausedSeconds:Wall;
}
void AOKStreetSocialScene::Tick(float Dt){
 Super::Tick(Dt);if(!State||!State->Started||State->Reported)return;auto& S=*State;GOKStreetTalkLimit=S.SelfTest; // the self-tests check the budget arithmetic; normal play has no daily talk limit
 // Walking into or out of a shop, or its opening hours passing: the shop part of the right panel has to follow without waiting for
// some other event to rebuild it.
 if(const double WallNow=FPlatformTime::Seconds();WallNow>=S.UiShopCheckAt){
  S.UiShopCheckAt=WallNow+.4;FString Why;const FString Shop=ShopPlaceHere();
  const FString Key=Shop.IsEmpty()?FString():Shop+(PlaceClosedNow(Shop,Why)?TEXT("|closed"):TEXT("|open"));
  if(Key!=S.UiShopKey){S.UiShopKey=Key;S.UiDirty=true;S.UiDirtyRegions|=OKStreetUi::Shop;S.UiDirtyFile=__FILE__;S.UiDirtyLine=__LINE__;}
 }
 if(S.FreeTalkLogDirty&&FPlatformTime::Seconds()-S.FreeTalkLogSavedAt>1.0){QUICK_SCOPE_CYCLE_COUNTER(STAT_Street_FreeTalkSaveLog);S.FreeTalkLogDirty=false;S.FreeTalkLogSavedAt=FPlatformTime::Seconds();FreeTalkSaveLog();}
 OKStreetDigestMessage(S);
 if(S.UiDirty){S.UiDirty=false;const uint32 Regions=S.UiDirtyRegions?S.UiDirtyRegions:uint32(OKStreetUi::All);S.UiDirtyRegions=0;if(CVarOKStreetLogUiRebuilds.GetValueOnGameThread())UE_LOG(LogTemp,Display,TEXT("Street UI rebuild asked by %s:%u (regions %u)"),*FPaths::GetCleanFilename(ANSI_TO_TCHAR(S.UiDirtyFile)),S.UiDirtyLine,Regions);{QUICK_SCOPE_CYCLE_COUNTER(STAT_Street_RebuildUI);RebuildUI(Regions);}}{QUICK_SCOPE_CYCLE_COUNTER(STAT_Street_TickControl);TickControl();}
 const double Now=SceneTime();auto* PC=GetWorld()->GetFirstPlayerController();if(!PC)return;
 if(!S.CatchLight.IsValid()){auto* L=NewObject<UPointLightComponent>(this);L->SetMobility(EComponentMobility::Movable);L->SetLightingChannels(false,false,true);L->SetCastShadows(false);
  L->SetIntensityUnits(ELightUnits::Candelas);L->SetIntensity(100.f); /* 100 cd: a clear wet shine, short of glare (300 burns out) */L->SetAttenuationRadius(800.f);L->SetSourceRadius(2.f);L->RegisterComponent();AddInstanceComponent(L);S.CatchLight=L;}
 if(PC->PlayerCameraManager)S.CatchLight->SetWorldLocation(PC->PlayerCameraManager->GetCameraLocation()+FVector(0,0,25));
 {QUICK_SCOPE_CYCLE_COUNTER(STAT_Street_TickFreeTalkReplay);TickFreeTalkReplay(Now);}{QUICK_SCOPE_CYCLE_COUNTER(STAT_Street_TickFreeTalkSpeech);TickFreeTalkHisVoice(SceneTime());TickFreeTalkSpeech(FPlatformTime::Seconds());TickDeferredNotes();}{QUICK_SCOPE_CYCLE_COUNTER(STAT_Street_TickFreeTalkLive);TickFreeTalkLive(Now);}{QUICK_SCOPE_CYCLE_COUNTER(STAT_Street_TickFreeTalkTrace);TickFreeTalkTrace(Now);}{QUICK_SCOPE_CYCLE_COUNTER(STAT_Street_TickOrderNotice);TickOrderNotice();}
 // The street clock paused: the kitchen and the eating wait too (nothing on the table is eaten while time stands still).
 if(S.ClockPaused)for(auto& P:S.Serves)P.At+=Dt;
 {QUICK_SCOPE_CYCLE_COUNTER(STAT_Street_TickServes);TickServes();}{QUICK_SCOPE_CYCLE_COUNTER(STAT_Street_TickShopArrival);TickShopArrival();}{QUICK_SCOPE_CYCLE_COUNTER(STAT_Street_TickMeal);TickMeal(S.ClockPaused?0.f:Dt);}{QUICK_SCOPE_CYCLE_COUNTER(STAT_Street_TickRetryAction);TickRetryAction();}{QUICK_SCOPE_CYCLE_COUNTER(STAT_Street_TickThenAction);TickThenAction();}{QUICK_SCOPE_CYCLE_COUNTER(STAT_Street_TickEnsureFreeTalk);TickEnsureFreeTalk();}{QUICK_SCOPE_CYCLE_COUNTER(STAT_Street_TickFaceTalkPartner);TickFaceTalkPartner();}{QUICK_SCOPE_CYCLE_COUNTER(STAT_Street_TickParty);TickParty(Dt);}{QUICK_SCOPE_CYCLE_COUNTER(STAT_Street_TickBluff);TickBluff();}{QUICK_SCOPE_CYCLE_COUNTER(STAT_Street_TickFarewell);TickFarewell();}{QUICK_SCOPE_CYCLE_COUNTER(STAT_Street_TickPlaceClosing);TickPlaceClosing();}{QUICK_SCOPE_CYCLE_COUNTER(STAT_Street_TickPlaceTour);TickPlaceTour(Now);}{QUICK_SCOPE_CYCLE_COUNTER(STAT_Street_TickLiveTalkTest);TickLiveTalkTest(Now);}
 {QUICK_SCOPE_CYCLE_COUNTER(STAT_Street_TickRestroom);TickRestroom();}{QUICK_SCOPE_CYCLE_COUNTER(STAT_Street_TickSleep);TickSleep();}{QUICK_SCOPE_CYCLE_COUNTER(STAT_Street_TickSolo);TickSolo();}{QUICK_SCOPE_CYCLE_COUNTER(STAT_Street_TickCalendarAndMind);TickCalendarAndMind();}{QUICK_SCOPE_CYCLE_COUNTER(STAT_Street_TickSaves);TickSaves();}{QUICK_SCOPE_CYCLE_COUNTER(STAT_Street_TickRouteJobs);TickRouteJobs();}{QUICK_SCOPE_CYCLE_COUNTER(STAT_Street_TickNavSearch);TickNavSearch();}{QUICK_SCOPE_CYCLE_COUNTER(STAT_Street_TickDestroyQueue);TickDestroyQueue();}{QUICK_SCOPE_CYCLE_COUNTER(STAT_Street_TickCommutes);TickCommutes(Dt);}{QUICK_SCOPE_CYCLE_COUNTER(STAT_Street_TickUiProbe);TickUiProbe();}{QUICK_SCOPE_CYCLE_COUNTER(STAT_Street_TickCounterProximity);TickCounterProximity();}{QUICK_SCOPE_CYCLE_COUNTER(STAT_Street_TickAcademics);TickAcademics();}{QUICK_SCOPE_CYCLE_COUNTER(STAT_Street_TickCampusProximity);TickCampusProximity();}
 // Reading together ends when the player walks away from her.
 // Walk-in places: the roof and ceiling disappear while the player stands inside, so the camera sees the room.
 // Multi-storey places also hide every floor above the player's.
 for(auto& PlaceLayers:S.PlaceRoofs){
  const auto& Places=OKStreetPlaces();if(!Places.IsValidIndex(PlaceLayers.Place))continue;
  const int32 Floor=OKStreetPlaceFloor(Places[PlaceLayers.Place],S.Male);
  if(PlaceLayers.Roof.IsValid()&&PlaceLayers.Roof->IsVisible()==(Floor>0))PlaceLayers.Roof->SetVisibility(Floor==0);
  for(auto& Level:PlaceLayers.Levels)if(Level.Value.IsValid()){const bool Show=Floor==0||Level.Key<=Floor;if(Level.Value->IsVisible()!=Show)Level.Value->SetVisibility(Show);}
 }
 // A paused street clock keeps its hour: the elapsed time is pinned to zero.
 if(S.ClockPaused)S.ClockStartWall=FPlatformTime::Seconds();
 // At the card table the clock runs slower (the rule's rate): each tick the real time that passed counts at that rate and
 // the start is rebased to now, so the elapsed time is never negative (moving the start by Dt could pass the wall).
 else if(S.Bluff.Active&&S.Bluff.ClockRate<1.0){const double Wall=FPlatformTime::Seconds();
  S.ClockStartHours=OKStreetClockHours(S.ClockStartHours,FMath::Max(0.0,Wall-S.ClockStartWall)*FMath::Max(0.0,S.Bluff.ClockRate));S.ClockStartWall=Wall;}
 // Once a real second is enough: the sun turns 0.1 degree per second, and each move redraws shadow maps.
 if(S.ClockDrivesSun&&S.Sun.IsValid()){const double Wall=FPlatformTime::Seconds();if(Wall>=S.NextSunUpdate){S.NextSunUpdate=Wall+1;const auto Sun=OKStreetSunAt(S.InspectSlot!=INDEX_NONE?14.0:OKStreetClockHours(S.ClockStartHours,Wall-S.ClockStartWall)); /* the inspection room is in afternoon light at any hour */S.Sun->SetActorRotation(Sun.Rotation);auto* Light=S.Sun->GetLightComponent();Light->SetIntensity(Sun.Intensity);for(auto& Fill:S.PlaceFills)if(Fill.IsValid())Fill->GetLightComponent()->SetIntensity(FMath::Max(Sun.Intensity*OKStreetPlaceFillShare,OKStreetPlaceFillNight));Light->SetLightColor(Sun.Color);}}
 if(TickPrivate(Now))return;
 {QUICK_SCOPE_CYCLE_COUNTER(STAT_Street_TickFacility);TickFacility(Now);}{QUICK_SCOPE_CYCLE_COUNTER(STAT_Street_TickPhoto);TickPhoto(Now);}{QUICK_SCOPE_CYCLE_COUNTER(STAT_Street_TickContacts);TickContacts(Dt,Now);}
 // Every actor of this roster (one male and up to OKStreetFemaleCount heroines).
 const int32 Cast=FMath::Min(S.Manifest.Characters.Num(),OKStreetCastCount);
 bool Ready=true;int32 Visible=0;FOKRuntimeCharacterStatus Statuses[OKStreetCastCount];bool Queried[OKStreetCastCount]={};
 for(int32 I=0;I<Cast;++I){Queried[I]=OKGetCrowdRuntimeCharacterStatus(this,I,Statuses[I]);Ready&=Queried[I]&&Statuses[I].Visible;if(Queried[I]&&Statuses[I].Visible)++Visible;if(Queried[I]&&!Statuses[I].Error.IsEmpty()&&!S.Errors.Contains(Statuses[I].Error))S.Errors.Add(Statuses[I].Error);}
 S.InterruptEndedThisTick=false;
 // Visible is the three-second reveal, not frustum culling. Loading is not disappearance.
 // Only the active male and the current talk target can end the talk. Other NPC errors stay logged.
 if(S.Target!=INDEX_NONE){bool Fault=false;for(int32 I=0;I<Cast;++I){FOKStreetPresenceSample Sample;Sample.Queried=Queried[I];if(Sample.Queried){Sample.Ready=Statuses[I].Ready;Sample.Visible=Statuses[I].Visible;Sample.HasError=!Statuses[I].Error.IsEmpty();}const bool Active=I==0||I==S.Target;if(Active&&S.InjectActiveFault&&I==S.InjectFaultSlot){Sample.Queried=true;Sample.HasError=true;}const bool End=OKStreetActiveActorShouldEnd(Sample,S.Presence[I]);if(Active&&End)Fault=true;}
  if(Fault){const int32 Commits=S.Transactions.Num();const bool Synthetic=S.InjectActiveFault;const int32 FaultSlot=S.InjectFaultSlot;EndTalk();++S.ActiveActorInterrupts;if(S.Transactions.Num()!=Commits)++S.ActiveActorInterruptCommits;S.InjectActiveFault=false;if(Synthetic){S.InterruptPhase=2;if(S.Agents.IsValidIndex(FaultSlot-1))S.Agents[FaultSlot-1].Held=true;}S.InterruptEndedThisTick=true;}}
 else{for(int32 I=0;I<Cast;++I){FOKStreetPresenceSample Sample;Sample.Queried=Queried[I];if(Sample.Queried){Sample.Ready=Statuses[I].Ready;Sample.Visible=Statuses[I].Visible;Sample.HasError=!Statuses[I].Error.IsEmpty();}OKStreetActiveActorShouldEnd(Sample,S.Presence[I]);}}
 if(Ready&&!S.AllReady){S.AllReady=true;S.ReadyAt=Now;S.Message=OKT(TEXT("ui.scene.all_ready_hint"));RefreshUI();
  // StreetSocial.cmd -Inspect [N]: straight into the inspection room with heroine N (1 when none is named).
  {int32 Asked=0;if(FParse::Value(FCommandLine::Get(),TEXT("StreetSocialInspect="),Asked)){FString Error;if(!BeginInspect(FMath::Max(1,Asked),Error))UE_LOG(LogTemp,Warning,TEXT("Street inspect: %s"),*Error);}}
  // -StreetSocialNavProbe: which big-map places the auto navigation can reach from the spawn.
  if(FParse::Param(FCommandLine::Get(),TEXT("StreetSocialNavProbe"))){int32 Reached=0;for(const auto& Place:OKStreetPlaces()){const auto Path=PlaceRoute(S.Male,Place.Door);Reached+=!Path.IsEmpty();UE_LOG(LogTemp,Display,TEXT("Street nav probe: %s door=%s points=%d"),Place.Name,*Place.Door.ToCompactString(),Path.Num());}UE_LOG(LogTemp,Display,TEXT("Street nav probe: %d/%d places reachable"),Reached,OKStreetPlaces().Num());}}
 if((S.SelfTest||FParse::Param(FCommandLine::Get(),TEXT("StreetSocialProfile")))&&S.AllReady&&Now-S.ReadyAt>2&&S.Frame.Num()<10000){if(S.LastWall>0){S.Frame.Add((Now-S.LastWall)*1000);S.Game.Add(FPlatformTime::ToMilliseconds(GGameThreadTime));S.Render.Add(FPlatformTime::ToMilliseconds(GRenderThreadTime));S.Rhi.Add(FPlatformTime::ToMilliseconds(GRHIThreadTime));S.Gpu.Add(FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles()));}S.LastWall=Now;}
 const bool Focused=OKControlActive()||(GEngine&&GEngine->GameViewport&&GEngine->GameViewport->Viewport&&GEngine->GameViewport->Viewport->IsForegroundWindow());
 const bool FocusChanged=Focused!=S.ViewportFocused;
 // The window coming to the front (or going behind) re-applies the mouse mode too, also when the control channel keeps
 // the game input focused.
 static bool GStreetForeground=false;const bool Foreground=GEngine&&GEngine->GameViewport&&GEngine->GameViewport->Viewport&&GEngine->GameViewport->Viewport->IsForegroundWindow();
 const bool ForegroundChanged=Foreground!=GStreetForeground;GStreetForeground=Foreground;
 if(FocusChanged||ForegroundChanged){S.ViewportFocused=Focused;ApplyMouseMode(Focused);}
 const bool Toggle=Focused&&S.MouseToggleRequested;S.MouseToggleRequested=false;
 if(Toggle){S.MouseUI=!S.MouseUI;ApplyMouseMode(true);RefreshUI();}
 const bool GameInput=Focused&&!S.MouseUI&&!S.SelfTest&&!S.RetargetSelfTest;
 if(GameInput&&!FocusChanged&&!Toggle){float X=0,Y=0;PC->GetInputMouseDelta(X,Y);S.Yaw+=X*OKStreetMouseYawSensitivity;S.Pitch=FMath::Clamp(S.Pitch+Y*OKStreetMousePitchSensitivity,-50.f,15.f);}
 // In a scene the wheel rolls the conversation back and forth (TickGalgame), not the street camera.
 if(GameInput&&!GalgameShowsDialogue()&&OKKeyPressed(PC,EKeys::MouseScrollUp))S.Distance=FMath::Max(180.f,S.Distance-35.f);
 if(GameInput&&!GalgameShowsDialogue()&&OKKeyPressed(PC,EKeys::MouseScrollDown))S.Distance=FMath::Min(800.f,S.Distance+35.f);
 int32 NearestPickup=INDEX_NONE;float PickupDistance=150.f;
 for(int32 I=0;I<S.TopicPickups.Num();++I)if(S.TopicPickups[I].IsValid()){const bool Claimed=S.Store.TopicClaims.FindRef(S.PickupTopics[I])==S.Store.TopicDay;S.TopicPickups[I]->SetActorHiddenInGame(Claimed);const float Distance=FVector::Dist2D(S.Male,S.TopicPickups[I]->GetActorLocation());if(!Claimed&&Distance<PickupDistance){PickupDistance=Distance;NearestPickup=I;}}
 // Walking past a topic picks it up (F still works). Each pickup is tried once per visit, so a full stock or a claimed
 // topic does not repeat its message every frame while he stands there.
 if(!S.SelfTest&&S.Target==INDEX_NONE&&S.Pair.Partner==INDEX_NONE&&NearestPickup!=INDEX_NONE){
  if(S.AutoPickTried!=NearestPickup){S.AutoPickTried=NearestPickup;PickTopic(NearestPickup);}
 }else if(NearestPickup==INDEX_NONE)S.AutoPickTried=INDEX_NONE;
 if(GameInput&&S.Target==INDEX_NONE&&S.Pair.Partner==INDEX_NONE&&NearestPickup!=INDEX_NONE&&OKKeyPressed(PC,EKeys::F))PickTopic(NearestPickup);
 if(Focused&&OKKeyPressed(PC,EKeys::F10)){S.ShowDebug=!S.ShowDebug;RefreshUI();}
 float Speed=0;
 if(S.PresentationOnlyTest&&S.PresentationPhase==2){Speed=OKStreetMaleRunSpeed/OKStreetWalkSpeed;S.Male.Y+=OKStreetMaleRunSpeed*FMath::Min(Dt,.05f);}
 if(S.SelfTest&&S.CompanionProbePhase==1){S.Male.Y+=OKStreetMaleWalkSpeed*FMath::Min(Dt,.05f);S.Rotations[0]=OKStreetFacingMovement(FVector(0,1,0),S.Rotations[0]);Speed=OKStreetMaleWalkSpeed/OKStreetWalkSpeed;}
 if(S.OutingGirl!=INDEX_NONE)Speed=StepOuting(Dt);
 if(S.Target==INDEX_NONE&&S.Pair.Partner==INDEX_NONE&&(S.OutingGirl==INDEX_NONE||S.FollowMode!=1)&&S.SleepPhase==0&&GameInput){FVector Direction=FVector::ZeroVector;const FRotator Heading(0,S.Yaw,0);const FVector Forward=Heading.Vector(),Right=FRotationMatrix(Heading).GetUnitAxis(EAxis::Y);
  if(OKKeyDown(PC,EKeys::W))Direction+=Forward;if(OKKeyDown(PC,EKeys::S))Direction-=Forward;if(OKKeyDown(PC,EKeys::D))Direction+=Right;if(OKKeyDown(PC,EKeys::A))Direction-=Right;
  // Moving himself pauses the autopilot (navigation, or the walk to where they are going); 3 s without input it goes on.
  if(!Direction.IsNearlyZero()&&(S.NavGirl!=INDEX_NONE||S.NavPointActive||S.OutingGirl!=INDEX_NONE)){
   if(SceneTime()-S.ManualMoveAt>3.0){S.Message=OKT(TEXT("ui.scene.walking_self_hint"));RefreshUI();}
   S.ManualMoveAt=SceneTime();S.ManualReplan=true;
  }
  if(Direction.Normalize()){const float Rate=(OKKeyDown(PC,EKeys::LeftShift)||OKKeyDown(PC,EKeys::RightShift))?OKStreetMaleRunSpeed:OKStreetMaleWalkSpeed;const FVector Candidate=StreetStep(S.Male,Direction,Rate*FMath::Min(Dt,.05f));
   if(Candidate!=S.Male){S.Rotations[0]=OKStreetFacingMovement(Candidate-S.Male,S.Rotations[0]);S.Male=Candidate;Speed=Rate/OKStreetWalkSpeed;}
  }
 }
 if(Speed==0)Speed=StepNavigation(Dt);
 if(Speed==0)Speed=StepSongStage(Dt); // her audience while she sings (OKStreetSocialSong.inl)
 // She leads or walks abreast on an outing (plan §13.2): her walk comes from that step, not the follow.
 S.CompanionWalk=S.FollowMode!=0&&S.OutingGirl!=INDEX_NONE&&S.Companion==S.OutingGirl&&SceneTime()-S.ManualMoveAt>3.0?S.FollowGirlWalk:StepCompanion(Dt);
 // Standing still he always takes his own stand (conversation_male, the game's mc_m_stand): the base idle is the
 // heroines' book-reading loop, and alone he stood holding an invisible book.
 FString Error;OKSetCrowdRuntimeLocomotionState(this,0,true,Speed>2.f,Error);OKSetCrowdRuntimeCharacterMotion(this,0,FTransform(S.Rotations[0],S.Male),S.ReadingForceWalk==0?1.f:Speed,Error);
 // A girl he is with is not on her schedule walk any more (it would keep pulling her to its route, on the ground).
 for(int32 G=1;G<=S.Agents.Num();++G)if(S.Commutes.Contains(G)&&(BusyWithPlayer(G)||S.LiftWalks.Contains(G)||S.LiftPins.Contains(G)))S.Commutes.Remove(G);
 {QUICK_SCOPE_CYCLE_COUNTER(STAT_Street_TickElevators);TickElevators(Dt);}
 {QUICK_SCOPE_CYCLE_COUNTER(STAT_Street_TickLover);TickLover();}
 {QUICK_SCOPE_CYCLE_COUNTER(STAT_Street_TickLife);TickLife(Dt);}
 {QUICK_SCOPE_CYCLE_COUNTER(STAT_Street_TickSong);TickSong(SceneTime());}
 {QUICK_SCOPE_CYCLE_COUNTER(STAT_Street_TickFaceTalkPartner);TickFaceTalkPartner(true);}
 auto Snapshot=S.Agents;FOKStreetAgentSim PlayerObstacle;PlayerObstacle.Slot=0;PlayerObstacle.Position=S.Male;PlayerObstacle.Held=true;Snapshot.Add(PlayerObstacle);
 for(auto& Agent:S.Agents){
  if(!S.SelfTest&&!S.RunNext.Contains(Agent.Slot))S.RunNext.Add(Agent.Slot,Now+S.Random.FRandRange(8,22));
  if(!S.SelfTest&&!Agent.Held&&Agent.Dwell<=0&&Now>=S.RunNext[Agent.Slot]){S.RunUntil.Add(Agent.Slot,Now+S.Random.FRandRange(2.5f,5.f));S.RunNext[Agent.Slot]=Now+S.Random.FRandRange(18,35);}
  const bool Running=!Agent.Held&&Agent.Dwell<=0&&((S.PresentationOnlyTest&&S.PresentationPhase==2)||Now<S.RunUntil.FindRef(Agent.Slot));
  const float TravelSpeed=Running?OKStreetFemaleRunSpeed:OKStreetWalkSpeed;
  // Free on the street walk while her day has her somewhere (a talk ended during her shift: its walk to the counter had
  // been dropped and the talk let her go): back to the schedule, which puts her there. Raiden was walked out of the cafe
  // on her shift by the rules below (R3-214).
  if(!Agent.Held&&!S.Commutes.Contains(Agent.Slot)&&!S.PendingMoves.Contains(Agent.Slot)&&!S.SelfTest&&!BusyWithPlayer(Agent.Slot)&&S.PlanBlock.FindRef(Agent.Slot,INDEX_NONE)!=INDEX_NONE)
   // As from a gap in her plan (INDEX_NONE): she walks there; a removed entry would put her there at once, as at dawn.
   if(const FOKStreetPlanBlock* B=BlockNow(Agent.Slot,int32(OKStreetMindNow(S)%1440));B&&!B->Place.IsEmpty()){S.PlanBlock.Add(Agent.Slot,INDEX_NONE);S.ScheduleTickAt=0;S.StreetDetourAt.Add(Agent.Slot,SceneTime()+2.0); // the schedule takes her, not the detours below
    UE_LOG(LogTemp,Display,TEXT("Street walk: actor %d roams while her day has her at %s: back to her schedule"),Agent.Slot,*B->Place);}
  // Free on the street walk but standing inside a place (summoned into the dorm lobby, a heroine with no day plan): the
  // street walk heads straight for its next stop through the wall, and the stuck rule only moves on to the next stop,
  // out there too. A wall before that stop: she walks out to it on the grid (through the door), then the street walk
  // takes her (R3-211). Looked at once a second at most.
  if(!Agent.Held&&!S.Commutes.Contains(Agent.Slot)&&!S.PendingMoves.Contains(Agent.Slot)&&!S.SelfTest&&!BusyWithPlayer(Agent.Slot)&&SceneTime()>=S.StreetDetourAt.FindRef(Agent.Slot))
   for(const auto& P:OKStreetPlaces())if(!P.Rooms.IsEmpty()&&OKStreetInsidePlace(P,Agent.Position)){
    S.StreetDetourAt.Add(Agent.Slot,SceneTime()+1.0);
    TArray<FOKStreetPointData> Points;TArray<TArray<int32>> Routes;OKStreetWaypoints(Points);OKStreetRoutes(Routes);
    if(!Routes.IsValidIndex(Agent.Route)||!Routes[Agent.Route].IsValidIndex(Agent.Cursor)||!Points.IsValidIndex(Routes[Agent.Route][Agent.Cursor]))break;
    const FVector Stop=Points[Routes[Agent.Route][Agent.Cursor]].Position;
    // Only when a wall stands between: an open place on her way (the playground) is walked across as before.
    FHitResult Hit;FCollisionQueryParams Query(SCENE_QUERY_STAT(StreetWalkWall),false,this);
    if(OKStreetInsidePlace(P,Stop)||!GetWorld()->LineTraceSingleByChannel(Hit,Agent.Position+FVector(0,0,85),FVector(Stop.X,Stop.Y,Agent.Position.Z+85),ECC_WorldStatic,Query))break;
    Agent.Held=true;Agent.Stuck=0;S.StreetDetourAt.Add(Agent.Slot,SceneTime()+8.0);QueueHeroineWalk(Agent.Slot,Agent.Position,Stop,true,false,FVector::ZeroVector,P.Door,S.Rotations[Agent.Slot].Yaw,P.Id);
    UE_LOG(LogTemp,Display,TEXT("Street walk: actor %d is inside %s, a wall before her next stop %s: out on the grid"),Agent.Slot,P.Id,*Stop.ToCompactString());
    break;}
  const FVector Before=Agent.Position;if(Agent.Slot!=S.PairRestoredActor||GFrameCounter!=S.PairRestoredFrame)OKStreetStepAgent(Agent,FMath::Min(Dt,.05f),Snapshot,TravelSpeed);
  // Her street walk heads straight for its next stop; a building in between (the block across from the dorm door) held
  // her walking at its wall, and the stuck rule only turned her to the next stop behind it too. Held up 1.5 s, she
  // walks round to that stop on the grid (its cells tested against the buildings), then the street walk goes on (R3-211).
  if(!S.SelfTest&&!Agent.Held&&!BusyWithPlayer(Agent.Slot)&&Agent.Dwell<=0.f&&Agent.Mode==EOKStreetMove::Walking&&FVector::Dist2D(Agent.Position,Before)<1.f){
   float& Blocked=S.StreetBlockedFor.FindOrAdd(Agent.Slot);Blocked+=FMath::Min(Dt,.05f);
   if(Blocked>1.5f&&SceneTime()>=S.StreetDetourAt.FindRef(Agent.Slot)){
    TArray<FOKStreetPointData> Points;TArray<TArray<int32>> Routes;OKStreetWaypoints(Points);OKStreetRoutes(Routes);
    if(Routes.IsValidIndex(Agent.Route)&&Routes[Agent.Route].IsValidIndex(Agent.Cursor)&&Points.IsValidIndex(Routes[Agent.Route][Agent.Cursor])){
     const FVector Stop=Points[Routes[Agent.Route][Agent.Cursor]].Position;Agent.Held=true;Agent.Stuck=0;S.StreetBlockedFor.Remove(Agent.Slot);S.StreetDetourAt.Add(Agent.Slot,SceneTime()+8.0);
     QueueHeroineWalk(Agent.Slot,Agent.Position,Stop,true,false,FVector::ZeroVector,Agent.Position,S.Rotations[Agent.Slot].Yaw,TEXT("street"));
     UE_LOG(LogTemp,Display,TEXT("Street walk: actor %d held up at %s, round to her next stop %s"),Agent.Slot,*Agent.Position.ToCompactString(),*Stop.ToCompactString());}}}
  else S.StreetBlockedFor.Remove(Agent.Slot);
  // In the inspection room nothing of her walk moves or turns her: a walk queued just before took her a step each frame
  // and the room pulled her back, so she turned this way and that.
  if(Agent.Slot==S.InspectSlot){Agent.Position=Before;S.Rotations[Agent.Slot]=S.RenderedRotations[Agent.Slot]=OKInspectFacing;}
  if(Agent.Position!=Before){S.Rotations[Agent.Slot]=OKStreetFacingMovement(Agent.Position-Before,S.Rotations[Agent.Slot]);}float Walk=Agent.Slot==S.Companion?S.CompanionWalk:(Agent.Held||Agent.Mode!=EOKStreetMove::Walking||Agent.Position==Before?0.f:TravelSpeed/OKStreetWalkSpeed);Walk=NpcIdleTick(Agent,Walk,Now);const FRotator PreviousFacing=S.RenderedRotations[Agent.Slot];S.RenderedRotations[Agent.Slot]=OKStreetLimitFemaleFacing(PreviousFacing,S.Rotations[Agent.Slot],Dt);const float Turn=FMath::Abs(FMath::FindDeltaAngleDegrees(PreviousFacing.Yaw,S.RenderedRotations[Agent.Slot].Yaw));if(Dt>0){S.MaxFemaleTurnRate=FMath::Max(S.MaxFemaleTurnRate,Turn/Dt);++S.TurnSamples;}
  OKSetCrowdRuntimeLocomotionState(this,Agent.Slot,S.Target==Agent.Slot,Running&&Walk>0,Error);
 // A schedule commute moves her in coarse steps: show her walking there smoothly instead of sliding in her idle.
  FVector Shown=Agent.Position;float ShownWalk=Walk;
  if(S.Commutes.Contains(Agent.Slot)||S.CommuteShown.Contains(Agent.Slot)){
   FVector& C=S.CommuteShown.FindOrAdd(Agent.Slot,Agent.Position);const FVector D=Agent.Position-C;
   if(D.Size2D()>250.f)C=Agent.Position;else C+=D.GetSafeNormal()*FMath::Min(float(D.Size()),OKStreetWalkSpeed*1.2f*Dt);
   Shown=C;ShownWalk=(S.Commutes.Contains(Agent.Slot)&&!S.CommutePausedUntil.Contains(Agent.Slot))||D.Size2D()>3.f?1.f:0.f; // walking the whole way, not between steps (standing while she waits for him)
   {int32& Last=S.IdlePose.FindOrAdd(Agent.Slot+100,-1);const int32 Now2=ShownWalk>0?0:1;if(Now2!=Last){Last=Now2;UE_LOG(LogTemp,Display,TEXT("Street commute: slot=%d %s t=%.2f left=%d"),Agent.Slot,Now2==0?TEXT("walk"):TEXT("stop"),Now,S.Commutes.Contains(Agent.Slot)?S.Commutes[Agent.Slot].Num():0);}}
   if(!S.Commutes.Contains(Agent.Slot)&&D.Size2D()<=3.f)S.CommuteShown.Remove(Agent.Slot); // arrived and caught up
  }
  else if(Agent.Held&&Walk<=0.f&&S.LiftWalks.Contains(Agent.Slot)){
   const FVector* Shown0=S.LastShown.Find(Agent.Slot);const float Rate=Shown0&&Dt>0?float(FVector::Dist2D(*Shown0,Agent.Position))/Dt:0.f;
   if(Rate>15.f&&Rate<600.f)ShownWalk=Rate/OKStreetWalkSpeed;
  }
  S.LastShown.Add(Agent.Slot,Shown);
 OKSetCrowdRuntimeCharacterMotion(this,Agent.Slot,FTransform(S.RenderedRotations[Agent.Slot],Shown),S.ReadingForceWalk==Agent.Slot?1.f:ShownWalk,Error);
  if(S.SelfTest&&Agent.Position!=Before&&!S.FacingActorsChecked.Contains(Agent.Slot)){FOKRuntimeCharacterStatus Submitted;const float BeforeError=FMath::Abs(FMath::FindDeltaAngleDegrees(PreviousFacing.Yaw,S.Rotations[Agent.Slot].Yaw));const float AfterError=FMath::Abs(FMath::FindDeltaAngleDegrees(S.RenderedRotations[Agent.Slot].Yaw,S.Rotations[Agent.Slot].Yaw));if(OKGetCrowdRuntimeCharacterStatus(this,Agent.Slot,Submitted)&&Submitted.Transform.Rotator().Equals(S.RenderedRotations[Agent.Slot],.01f)&&Turn<=OKStreetFemaleTurnDegreesPerSecond*FMath::Max(0.f,Dt)+.001f&&AfterError<=BeforeError+.001f){S.FacingActorsChecked.Add(Agent.Slot);++S.FacingChecks;}else S.Errors.Add(TEXT("NPC submitted facing exceeded turn limit or turned away from movement"));}

 }
 {QUICK_SCOPE_CYCLE_COUNTER(STAT_Street_TickAmbientActivity);TickAmbientActivity(Now);}
 {QUICK_SCOPE_CYCLE_COUNTER(STAT_Street_TickReadingProps);TickReadingProps();}
 // Development: -StreetSocialNavWalk walks the auto navigation from the plaza to every place's door and logs it.
 if(S.AllReady&&FParse::Param(FCommandLine::Get(),TEXT("StreetSocialNavWalk"))){
  static int32 Index=-1,Arrived=0;static double StartedAt=0;
  const auto& Places=OKStreetPlaces();
  if(Places.IsValidIndex(Index)&&S.NavPointActive&&SceneTime()-StartedAt>90)StopNavigation(OKT(TEXT("ui.scene.timeout_label")));
  if(Index<Places.Num()&&(Index<0||(!S.NavPointActive&&!S.NavSearch))){ // a route still being searched is not done
   if(Index>=0){const bool Ok=FVector::Dist2D(S.Male,Places[Index].Door)<=150;Arrived+=Ok;UE_LOG(LogTemp,Display,TEXT("Street nav walk: %s %s in %.1fs at %s (%s)"),Places[Index].Id,Ok?TEXT("arrived"):TEXT("FAILED"),SceneTime()-StartedAt,*S.Male.ToCompactString(),*S.Message);}
   ++Index;
   if(Places.IsValidIndex(Index)){S.Male=OKStreetMaleSpawn();NavigatePoint(Places[Index].Door,Places[Index].Name);StartedAt=SceneTime();}
   else{UE_LOG(LogTemp,Display,TEXT("Street nav walk: %d/%d places reached"),Arrived,Places.Num());FPlatformMisc::RequestExitWithStatus(false,0);}
  }
 }
 // Development: -StreetSocialLiftTest=<activity> -StreetSocialLiftShots=<dir>: from the place's door, an outing with
 // girl 1 to an upper-floor activity through the real elevator; a screenshot every 3 s, then quit.
 if(FString LiftTest;S.AllReady&&FParse::Value(FCommandLine::Get(),TEXT("StreetSocialLiftTest="),LiftTest)){
  static double Began=0,NextShot=0;static int32 Shot=0;FString Dir;FParse::Value(FCommandLine::Get(),TEXT("StreetSocialLiftShots="),Dir);
  if(Began==0){
   const FOKStreetActivity* A=OKStreetFindActivity(*LiftTest);const FOKStreetPlace* P=A?OKStreetFindPlace(A->Place):nullptr;
   if(P&&S.Agents.Num()>0){
    const FVector Out=(P->Door-P->Origin).GetSafeNormal2D();S.Male=P->Door+Out*120;S.Agents[0].Position=S.Male+FVector(0,90,0);
    FString Why;const bool Ok=BeginOuting(1,LiftTest,Why);UE_LOG(LogTemp,Display,TEXT("Street lift test: %s begin=%d %s"),*LiftTest,Ok,*Why);
   }
   Began=SceneTime();NextShot=Began+3;
  }
  if(!Dir.IsEmpty()&&SceneTime()>=NextShot){NextShot+=3;S.Yaw=S.Rotations[0].Yaw+150;S.Pitch=-62;S.Distance=650;FScreenshotRequest::RequestScreenshot(Dir/FString::Printf(TEXT("lift_%02d.png"),Shot++),false,false);
   const int32 Lift=ElevatorWithMale();UE_LOG(LogTemp,Display,TEXT("Street lift test: t=%.0f male=%s stage=%d in_car=%d girl=%s msg=%s"),SceneTime()-Began,*S.Male.ToCompactString(),S.LiftRide.Stage,Lift,S.Agents.Num()?*S.Agents[0].Position.ToCompactString():TEXT("-"),*S.Message);}
  if(SceneTime()-Began>150||(S.OutingGirl==INDEX_NONE&&SceneTime()-Began>8&&SceneTime()-Began<9e8)){static bool Done=false;if(!Done){Done=true;UE_LOG(LogTemp,Display,TEXT("Street lift test: end pair=%d male=%s %s"),S.Pair.Partner,*S.Male.ToCompactString(),*S.Message);}if(SceneTime()-Began>(S.OutingGirl==INDEX_NONE?14:150))FPlatformMisc::RequestExitWithStatus(false,0);}
 }
 // Development: -StreetSocialLoverTest=<dir>: in the plaza, girl 1 in front of him; kiss, hug, hand, head pat in turn
 // (as if she agreed shyly), a screenshot of each pose and of her reaction, then quit.
 if(FString LoverDir;S.AllReady&&FParse::Value(FCommandLine::Get(),TEXT("StreetSocialLoverTest="),LoverDir)){
  static int32 Step=-1;static double At=0;static const TCHAR* Acts[]={TEXT("kiss"),TEXT("hug"),TEXT("hold_hands"),TEXT("head_pat"),TEXT("feed"),TEXT("phone_call"),TEXT("alcohol"),TEXT("loud")};constexpr int32 ActCount=UE_ARRAY_COUNT(Acts);
  const double T=SceneTime();
  if(Step<0||T-At>16){
   ++Step;At=T;
   if(Step>=ActCount){FPlatformMisc::RequestExitWithStatus(false,0);Step=99;}
   else if(S.Agents.Num()>0){
    if(S.Pair.Partner!=INDEX_NONE)FinishPhoto();
    S.Male=OKStreetMaleSpawn();S.Rotations[0]=FRotator(0,-90,0);S.Agents[0].Position=S.Male+FVector(90,0,0);S.Agents[0].Held=true;S.Rotations[1]=FRotator(0,90,0); // facing +X / -X
    PlayLoverAct(1,Acts[Step],1);
   }
  }
  if(Step<ActCount){
   // Poses at 5 s; a touch three times while his hand goes up (side view); every reaction at 11 s.
   static int32 Shot=-1;const double Times[]={1.2,1.8,2.6,5.0,11.0};int32 Want=-1;for(int32 K=0;K<5;++K)if(T-At>Times[K])Want=Step*5+K;
   const bool Touch=Step>=2;const int32 K=Want-Step*5;
   if(Want!=Shot&&Want>=0&&(Touch?K!=3:(K>=3))&&T-At<Times[K]+.5){Shot=Want;if(Touch||K==4){S.Yaw=S.Rotations[0].Yaw+(Touch?0:30);S.Pitch=-8;S.Distance=300;}
    FScreenshotRequest::RequestScreenshot(LoverDir/FString::Printf(TEXT("%s_%d.png"),Acts[Step],K),false,false);
    UE_LOG(LogTemp,Display,TEXT("Street lover test: %s t=%.0f pair=%d react=%d"),Acts[Step],T-At,S.Pair.Partner,S.LoverReact.Girl);}
  }
 }
 // Development: -StreetSocialJumpLog logs any character whose body moves more than 40 cm in one frame.
 if(FParse::Param(FCommandLine::Get(),TEXT("StreetSocialJumpLog"))){
  static TMap<int32,TPair<FVector,FVector>> Last;
  for(int32 Character=0;Character<S.Manifest.Characters.Num();++Character){
   FOKRuntimeCharacterStatus Status;FVector Head;
   if(!OKGetCrowdRuntimeCharacterStatus(this,Character,Status)||!Status.Visible)continue;
   if(!OKGetCrowdRuntimeBonePosition(this,Character,TEXT("cf_j_head"),Head)&&!OKGetCrowdRuntimeBonePosition(this,Character,TEXT("cm_j_head"),Head))continue;
   const FVector Root=Status.Transform.GetLocation();
   if(const auto* Was=Last.Find(Character)){
    const float RootStep=FVector::Dist2D(Was->Key,Root),HeadStep=FVector::Dist2D(Was->Value,Head);
    if(RootStep>40||HeadStep>40)UE_LOG(LogTemp,Warning,TEXT("Street jump: actor=%d clip=%s transition=%d root %.0fcm head %.0fcm held=%d at %s"),Character,*Status.ActiveClipId,Status.AnimationTransitioning,RootStep,HeadStep,Character>0&&S.Agents.IsValidIndex(Character-1)?S.Agents[Character-1].Held:-1,*Root.ToCompactString());
   }
   Last.Add(Character,{Root,Head});
  }
 }
 if(S.Camera.IsValid()){FVector Focus=S.Target!=INDEX_NONE?S.Agents[S.Target-1].Position+FVector(0,0,135):S.Male+FVector(0,0,115);FVector Pivot=S.Target!=INDEX_NONE?(S.Male+S.Agents[S.Target-1].Position)*.5f+FVector(0,0,115):Focus;
  if((S.Pair.Conversation()||S.Pair.Splash)&&S.Pair.Phase==1){FVector A,B;if((OKGetCrowdRuntimeBonePosition(this,0,TEXT("cm_j_head"),A)||OKGetCrowdRuntimeBonePosition(this,0,TEXT("cf_j_head"),A))&&OKGetCrowdRuntimeBonePosition(this,S.Pair.Partner,TEXT("cf_j_head"),B)&&!A.ContainsNaN()&&!B.ContainsNaN()){Focus=(A+B)*.5f-FVector(0,0,25);Pivot=Focus;}}
  if(S.Pair.Watermelon&&S.Pair.Phase==1){Focus=(S.Male+S.Agents[S.Pair.Partner-1].Position)*.5f+FVector(0,35,80);Pivot=Focus;}
  GalgameWalkFocus(Focus,Pivot); // walking and talking: between him and her (OKStreetSocialGalgame.inl)
  const FRotator View(S.Pitch,S.Yaw,0);const FVector Wanted=Pivot-View.Vector()*S.Distance+(S.Target!=INDEX_NONE?FRotationMatrix(View).GetUnitAxis(EAxis::Y)*(S.Pair.Conversation()?-120.f:110.f):FVector::ZeroVector);FHitResult Hit;FCollisionQueryParams Query(SCENE_QUERY_STAT(StreetCamera),false,this);const bool Blocked=GetWorld()->SweepSingleByChannel(Hit,Pivot,Wanted,FQuat::Identity,ECC_WorldStatic,FCollisionShape::MakeSphere(8),Query);S.Camera->SetActorLocation(Blocked?Hit.Location:Wanted);S.Camera->SetActorRotation((Focus-S.Camera->GetActorLocation()).Rotation());
   // Right in front of the lens (in the lift she stood between the camera and him, under a metre from it: her back and
   // hair filled the view): she is left out of the view until the camera has room again; her portrait still shows her.
   // Not him (he is who is walked). Only for the street's own follow camera: the scene's shots hide what is in their way
   // themselves (TickGalgame).
   PC->HiddenPrimitiveComponents.RemoveAll([&S](const TWeakObjectPtr<UPrimitiveComponent>& P){return S.CameraHidden.Contains(P);});S.CameraHidden.Reset();
   if(S.Target==INDEX_NONE){const FVector Lens=S.Camera->GetActorLocation(),Ahead=S.Camera->GetActorForwardVector();TArray<UPrimitiveComponent*> Inside;
    for(int32 Slot=1;Slot<S.Manifest.Characters.Num();++Slot){FOKRuntimeCharacterStatus O;if(!OKGetCrowdRuntimeCharacterStatus(this,Slot,O)||!O.Visible)continue;
     bool bIn=false;for(const float Z:{60.f,100.f,140.f}){const FVector Rel=O.Transform.GetLocation()+FVector(0,0,Z*FMath::Max(O.Transform.GetScale3D().Z,.1))-Lens;const float Depth=float(Rel|Ahead);
      bIn|=Rel.Size()<55.f||(Depth>0.f&&Depth<100.f&&Depth>float(Rel.Size())*.87f);} // within 30 degrees of the middle of the view
     if(bIn)OKGetCrowdRuntimeCharacterComponents(this,Slot,Inside);}
    for(UPrimitiveComponent* Prim:Inside){PC->HiddenPrimitiveComponents.AddUnique(Prim);S.CameraHidden.Add(Prim);}}
  {FVector SongAt,SongFocus;if(SongCameraShot(SceneTime(),Dt,SongAt,SongFocus)){S.Camera->SetActorLocation(SongAt);S.Camera->SetActorRotation((SongFocus-SongAt).Rotation());S.Camera->GetCameraComponent()->SetFieldOfView(40.f);}}if(GControlCamera.Active&&!OKApplyControlCamera(this,S.Camera.Get()))GControlCamera.Active=false;OKUpdateCrowdRuntimeDisplayLighting(this,-S.Camera->GetActorForwardVector());}
 if(S.Camera.IsValid()&&SceneTime()<S.PhotoViewUntil)S.Camera->SetActorLocationAndRotation(S.PhotoViewLoc,S.PhotoViewRot); // a photo being taken
 {QUICK_SCOPE_CYCLE_COUNTER(STAT_Street_TickGalgame);TickGalgame(Dt);} // conversations as galgame scenes: dialog box, portrait, a shot per line (OKStreetSocialGalgame.inl)
 if(GControlCamera.Active&&S.Camera.IsValid())OKApplyControlCamera(this,S.Camera.Get()); // the control channel's pinned camera wins over the scene's shot (a look at a seated group mid-talk)
 if(S.InspectSlot!=INDEX_NONE)TickInspect(Dt,PC,Focused); // the inspection room's camera wins over everything
 const bool AppEscape=Focused&&!S.App.IsEmpty()&&OKKeyPressed(PC,EKeys::Escape);if(AppEscape)CloseApp();
 if(Focused&&S.App.IsEmpty()&&OKKeyPressed(PC,EKeys::M)&&S.PrivateGirl==INDEX_NONE)ToggleBigMap();
 if(Focused&&!AppEscape&&OKKeyPressed(PC,EKeys::Escape)){if(S.BigMapOpen)ToggleBigMap();else if(S.SleepNap&&S.SleepPhase==2)S.SleepWake=true;else if(S.OutingGirl!=INDEX_NONE)CancelOuting(OKT(TEXT("ui.scene.go_cancelled")));else if(S.PrivateGirl!=INDEX_NONE)FinishPrivate();else if((S.NavGirl!=INDEX_NONE||S.NavPointActive)&&S.Target==INDEX_NONE)StopNavigation(OKT(TEXT("ui.scene.nav_cancelled")));else if(S.Pair.Partner!=INDEX_NONE)FinishPhoto();else if(S.Companion!=INDEX_NONE)FarewellCompanion();else if(S.Facility!=INDEX_NONE)FinishFacility();else EndTalk();}
 if(Focused&&S.Pair.Partner!=INDEX_NONE&&OKKeyPressed(PC,EKeys::P))CapturePhoto();
 if(OKStreetWorldTouchAllowed(Focused,S.MouseUI,S.Target!=INDEX_NONE,S.TouchActive,S.WorldTouchPointerBlocked)&&OKKeyPressed(PC,EKeys::LeftMouseButton)){
  float MX=0,MY=0;FVector Origin,Ray;
  if(PC->GetMousePosition(MX,MY)&&PC->DeprojectScreenPositionToWorld(MX,MY,Origin,Ray)){
   // A click that touches no part of her skips the scene's line (SkipGalgameLine).
   if(TouchRay(Origin,Ray)==INDEX_NONE)SkipGalgameLine();
  }
 }
 // Enter or Space skips the scene's line too (the chat box keeps its Enter: the game view is not focused then).
 if(Focused&&(OKKeyPressed(PC,EKeys::Enter)||OKKeyPressed(PC,EKeys::SpaceBar)))SkipGalgameLine();
 if(Focused&&OKKeyPressed(PC,EKeys::L))ToggleLanguage();
 if(Focused&&OKKeyPressed(PC,EKeys::V))ToggleVoiceSource();
 // -StreetSocialTtsSmoke: talk to NPC 1 once with TTS forced on; read game.log for the result.
 if(S.TtsSmoke&&S.AllReady&&!S.TtsSmokeStarted&&Now-S.ReadyAt>3&&Now-S.TtsRequestedAt>2){S.TtsRequestedAt=Now;S.Agents[0].Position=S.Male+FVector(150,0,0);S.TtsSmokeStarted=BeginTalk(1);UE_LOG(LogTemp,Display,TEXT("Street TTS smoke: begin talk ok=%d"),S.TtsSmokeStarted?1:0);}
 if(S.TtsSmoke&&S.TtsSmokeTopics==25&&S.Target!=INDEX_NONE&&S.Node==INDEX_NONE){
  // Smoke only: one line with name tokens, to show what TTS is given.
  ++S.TtsSmokeTopics;for(int32 N=0;N<S.Catalogue.Nodes.Num();++N){const auto& L=S.Catalogue.Nodes[N];if(L.PersonalityId==S.Manifest.Characters[S.Target].PersonalityId&&L.VoiceSequenceComplete&&!L.VoiceFiles.IsEmpty()&&L.TextZh.Contains(TEXT("あだ名]"))){QueueNode(N,TEXT("daily"));UE_LOG(LogTemp,Display,TEXT("Street TTS smoke: names line %s -> %s"),*L.TextZh,*OKStreetFillNames(L.TextZh,S.Manifest.Characters[S.Target].Name,S.Manifest.Characters[0].Name));break;}}
 }
 if(S.TtsSmoke&&S.TtsSmokeStarted&&S.TtsSmokeTopics<25&&S.Target!=INDEX_NONE&&S.Node==INDEX_NONE&&!S.CollectedActive&&!S.RelationshipPending){
  // Smoke only (copied save): keep the budget open and talk until every daily line has been used, to exercise the new round.
  const int32 R=OKStreetRelationIndex(S.Store,OKStreetStableId(S.Manifest.Characters[S.Target]),S.Manifest.TalkBudget,true);if(S.Store.Relations.IsValidIndex(R))S.Store.Relations[R].TalksRemaining=99;
  ++S.TtsSmokeTopics;Topic(TEXT("daily"));UE_LOG(LogTemp,Display,TEXT("Street TTS smoke: daily topic %d node=%d player_line=%s message=%s"),S.TtsSmokeTopics,S.Node,*S.PlayerLine,*S.Message);}
 if(S.Target==INDEX_NONE&&S.Pair.Partner==INDEX_NONE&&S.Facility==INDEX_NONE&&S.Companion==INDEX_NONE){
  TArray<FOKStreetTalkCandidateSample> Samples;OKStreetBuildTalkSamples(GetWorld(),this,S.Male,S.Agents,S.Manifest.Characters,Statuses,Queried,Samples);
  const FOKStreetTalkViewForward View=OKStreetSceneTalkView(S.Camera.Get(),S.Yaw);
  FOKStreetTalkSelection Current;Current.Selected=S.Candidate;Current.Count=S.CandidateCount;Current.Explicit=S.CandidateExplicit;
  const bool Tab=GameInput&&OKKeyPressed(PC,EKeys::Tab);
  const bool Interact=GameInput&&OKKeyPressed(PC,EKeys::E);
  const FOKStreetTalkSelectionResult Result=OKStreetResolveTalkSelection(Current,Samples,View.Horizontal,Tab,Interact);
  S.Candidate=Result.Selection.Selected;S.CandidateCount=Result.Selection.Count;S.CandidateExplicit=Result.Selection.Explicit;
  if(Interact&&Result.AdmitSlot==INDEX_NONE&&FreeTalkPartner()!=INDEX_NONE&&!S.FreeTalkLive){S.FreeTalkOpen=true;FreeTalkLiveSet(true);} // E again: back to the live conversation
  if(Result.AdmitSlot!=INDEX_NONE){if(BeginTalk(Result.AdmitSlot)){S.FreeTalkOpen=true;FreeTalkLiveSet(true);}} // E opens the conversation and the microphone together
  else if(S.Candidate!=INDEX_NONE&&S.Manifest.Characters.IsValidIndex(S.Candidate)){const FString Hint=OKStreetTalkCandidateHint(S.Manifest.Characters[S.Candidate].Name,S.CandidateCount);if(!Hint.IsEmpty()&&Hint!=S.Message)S.Message=Hint;} // the box binds to the message (OKStreetDigestMessage): no rebuild
  else if(Interact&&Result.RejectedInteract){
   // E pressed and nobody was taken: say why for each girl in reach (distance, line of sight, readiness, angle off the view).
   FString Why;for(const auto& Sm:Samples)if(Sm.DistanceCm<OKStreetTalkRangeCm*2.f){const FVector2D Dir=FVector2D(Sm.Offset.X,Sm.Offset.Y).GetSafeNormal();const float Ang=FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(float(FVector2D::DotProduct(Dir,FVector2D(View.Horizontal.X,View.Horizontal.Y))),-1.f,1.f)));Why+=FString::Printf(TEXT(" [slot %d dist=%.0f los=%d ready=%d visible=%d actions=%d queried=%d stale=%d angle=%.0f]"),Sm.Slot,Sm.DistanceCm,Sm.LineOfSightClear?1:0,Sm.Ready?1:0,Sm.Visible?1:0,Sm.ActionsReady?1:0,Sm.Queried?1:0,Sm.Stale?1:0,Ang);}
   UE_LOG(LogTemp,Display,TEXT("Street E refused:%s"),Why.IsEmpty()?TEXT(" nobody within reach"):*Why);
   const FString Idle=OKT(TEXT("ui.scene.controls_short_hint"));if(S.Message!=Idle)S.Message=Idle; // bound to the message: no rebuild
  }
  else if(Current.Selected!=INDEX_NONE||Result.RejectedInteract||Tab){const FString Idle=OKT(TEXT("ui.scene.controls_short_hint"));if(S.Message!=Idle)S.Message=Idle;}
 }
 if(S.Target==INDEX_NONE&&S.Pair.Partner==INDEX_NONE&&NearestPickup!=INDEX_NONE){if(const auto* T=S.CollectedTopics.Find(S.PickupTopics[NearestPickup])){const FString Hint=OKT(TEXT("ui.scene.auto_pickup_topic_label"))+T->Name+OKT(TEXT("ui.scene.pickup_controls_hint"));if(S.Message!=Hint)S.Message=Hint;}} // bound to the message: no rebuild
 if(Focused&&S.Target!=INDEX_NONE){if(OKKeyPressed(PC,EKeys::Four))AskHerTopic();if(OKKeyPressed(PC,EKeys::One))Topic(TEXT("daily"));if(OKKeyPressed(PC,EKeys::Two))Topic(TEXT("romance"));if(OKKeyPressed(PC,EKeys::Three))Topic(TEXT("intimacy"));}
 // N while something is going on: say what (a call ringing, someone walking over, a talk or an activity), not nothing.
 if(Focused&&OKKeyPressed(PC,EKeys::N)&&!S.RetargetSelfTest&&!(S.Target==INDEX_NONE&&S.Pair.Partner==INDEX_NONE&&NothingGoingOn())){
  S.Message=OKT(S.RingGirl!=INDEX_NONE||S.RemoteMode!=0?TEXT("ui.scene.skip_blocked_phone"):S.ApproachGirl!=INDEX_NONE?TEXT("ui.scene.skip_blocked_approach"):S.Target!=INDEX_NONE?TEXT("ui.scene.skip_blocked_talk"):TEXT("ui.scene.skip_blocked_activity"));RefreshUI();}
 if(Focused&&OKKeyPressed(PC,EKeys::N)&&S.Target==INDEX_NONE&&S.Pair.Partner==INDEX_NONE&&!S.RetargetSelfTest&&NothingGoingOn()&&SkipConfirmed()&&SkipToNextMorning()){S.Message=OKMsg(TEXT("ui.scene.skip_to_morning"),{{TEXT("clock"),FString::Printf(TEXT("%02d:%02d"),int32(S.ClockStartHours),int32(FMath::RoundToInt(S.ClockStartHours*60))%60)}});RefreshUI();}
 if(S.RelationshipSilentUntil>0&&Now>=S.RelationshipSilentUntil){S.RelationshipSilentUntil=0;AdvanceRelationshipDialogue();}
 if(S.CollectedActive&&S.SilentTopicUntil>0&&Now>=S.SilentTopicUntil){S.SilentTopicUntil=0;AdvanceCollectedTopic();}
 if(S.Node!=INDEX_NONE){S.PeakMouth=FMath::Max(S.PeakMouth,OKCrowdRuntimeSpeechMouth(this,S.Target));
  if(!S.Pair.Conversation()&&!S.ListenerStarted&&OKCrowdRuntimeSpeechMouth(this,S.Target)>0){S.ListenerStarted=OKPlayCrowdRuntimeAction(this,0,TEXT("talk_gesture_03.okanim"),Error);}
  {FOKRuntimeActionStreamStatus MaleAction,NpcAction;if(OKGetCrowdRuntimeActionStatus(this,0,MaleAction)&&OKGetCrowdRuntimeActionStatus(this,S.Target,NpcAction)&&MaleAction.Playing&&NpcAction.Playing)S.DualActionSeconds+=Dt;}
  if(S.MalePhase==1&&!S.ClipStarted&&S.Voice==0){
   if(!S.MaleVoiceId.IsEmpty()){FString MaleError;S.MalePhase=OKSpeakCrowdRuntimeLine(this,0,S.MaleVoiceId,MaleError)?2:3;if(S.MalePhase==3)UE_LOG(LogTemp,Warning,TEXT("Street TTS male speech failed: %s"),*MaleError);}
   else if(!S.MaleTtsPending||Now-S.MaleRequestedAt>S.TtsTimeout+1){if(S.MaleTtsPending)OKStreetTtsCancel(S.TtsEndpoint,OKStreetMaleTtsChannel(this),S.MaleSerial+1);S.MaleTtsPending=false;S.MalePhase=3;}
  }else if(S.MalePhase==2&&!OKCrowdRuntimeSpeechActive(this,0)){S.MalePhase=3;if(S.TtsSmoke)UE_LOG(LogTemp,Display,TEXT("Street male voice completed: tone=%s"),*S.MaleTone);}
  if(S.MalePhase==1||S.MalePhase==2){}
  else if(!S.ClipStarted&&S.TtsPending){if(Now-S.TtsRequestedAt>S.TtsTimeout+1){S.TtsPending=false;++S.TtsFallbacks;S.TtsNotice=OKT(TEXT("ui.scene.tts_timeout_builtin"));RefreshUI();}}
  else if(!S.ClipStarted){const bool WithGesture=S.Voice==0&&(!S.Pair.Conversation()||S.TouchActive);bool Played=WithGesture?OKSpeakCrowdRuntimeActionLine(this,S.Target,S.VoiceIds[S.Voice],S.ActiveGesture,Error):OKSpeakCrowdRuntimeLine(this,S.Target,S.VoiceIds[S.Voice],Error);
   // Her gesture could not be queued (her shy gesture from the line before still waiting to play): she says it without
   // it. The confession scene 惠惠 was to answer ended the whole talk, and her shift took her off unanswered (R4-19).
   if(!Played&&WithGesture){UE_LOG(LogTemp,Display,TEXT("Street dialogue: gesture not queued (%s); the line plays without it"),*Error);Played=OKSpeakCrowdRuntimeLine(this,S.Target,S.VoiceIds[S.Voice],Error);}
   if(!Played){FailTalk(TEXT("Dialogue start failed: ")+Error);}else S.ClipStarted=true;
  }else if(!OKCrowdRuntimeSpeechActive(this,S.Target)){
   EOKRuntimeSpeechState SpeechState;FString SpeechError;
   if(S.LineInterrupted){S.Voice=S.VoiceIds.Num();S.ClipStarted=false;FinishLine();}
   else if(!OKGetCrowdRuntimeSpeechState(this,S.Target,SpeechState,SpeechError)||SpeechState!=EOKRuntimeSpeechState::Completed){FailTalk(TEXT("Dialogue did not complete audio: ")+SpeechError);}
   else{if(S.TtsSmoke)UE_LOG(LogTemp,Display,TEXT("Street voice clip completed: actor=%d id=%s mouth_peak=%.3f"),S.Target,*S.VoiceIds[S.Voice],S.PeakMouth);++S.Voice;S.ClipStarted=false;if(S.Voice>=S.VoiceIds.Num())FinishLine();}
  }
  if(S.Node!=INDEX_NONE&&Now-S.LineStart>90){FailTalk(TEXT("Dialogue playback timeout"));}
 }
 if(S.TouchReactionOnlyTest){if(S.AllReady)RunTouchReactionProbe(Now);return;}
 if(S.ContextOnlyTest){RunDialogueContextProbe(Now);return;}
 if(S.PresentationOnlyTest){RunPresentationProbe(Now);return;}
 if(S.MovementOnlyTest){
  if(Now-S.StartedAt>180){S.Errors.Add(TEXT("Movement smoke timeout"));WriteReport(false);return;}
  if(S.AllReady){
   if(!S.MovementChecked){
    auto Check=[&](bool Ok,const TCHAR* Why){if(!Ok)S.Errors.Add(Why);};
    Check(S.FloorFinishesFixed>0,TEXT("No authored floor finish found in cooked map"));
    FCollisionQueryParams Query(SCENE_QUERY_STAT(StreetMovementProbe),false,this);FHitResult Hit;
    TArray<FOKStreetProp> Props;OKStreetProps(Props);
    for(const auto& P:Props)if(P.Kind==EOKStreetPropKind::Decor&&P.Mesh==EOKStreetMesh::Cube&&P.Size.Z<=10&&P.Size.X>=100&&P.Size.Y>=100){
     for(int32 Axis=0;Axis<2;++Axis)for(int32 Sign:{-1,1})for(int32 Reverse:{-1,1}){
      FVector Edge=P.Center;Edge.Z=0;Edge[Axis]+=Sign*P.Size[Axis]*.5f;if(Axis==1)Edge.X+=P.Size.X*.2f;FVector Offset=FVector::ZeroVector;Offset[Axis]=80.f*Reverse;
      Check(!GetWorld()->SweepSingleByChannel(Hit,Edge-Offset+FVector(0,0,85),Edge+Offset+FVector(0,0,85),FQuat::Identity,ECC_WorldStatic,FCollisionShape::MakeCapsule(28,78),Query),TEXT("Floor edge still blocks player capsule"));++S.MovementSweepChecks;
     }
    }
    Check(GetWorld()->SweepSingleByChannel(Hit,FVector(-1950,-150,85),FVector(-2350,-150,85),FQuat::Identity,ECC_WorldStatic,FCollisionShape::MakeCapsule(28,78),Query),TEXT("Building collision was removed"));++S.MovementSweepChecks;
    Check(FMath::IsNearlyEqual(OKStreetMouseYawSensitivity,.5f)&&FMath::IsNearlyEqual(OKStreetMousePitchSensitivity,.2f),TEXT("Mouse sensitivity changed on wrong axis"));
    Check(FMath::IsNearlyEqual(OKStreetLimitFemaleFacing(FRotator(0,179,0),FRotator(0,-179,0),.02f).Yaw,179.9f,.001f),TEXT("Female wrap-around did not use shortest path"));
    S.MovementChecked=true;S.MovementProbeAt=Now;
   }
   if(Now-S.MovementProbeAt>6){if(S.TurnSamples<100||S.MaxFemaleTurnRate>OKStreetFemaleTurnDegreesPerSecond+.05f||S.FacingActorsChecked.Num()!=5)S.Errors.Add(TEXT("Five runtime female actors did not pass capped-facing submission checks"));WriteReport(S.Errors.IsEmpty());}
  }
  return;
 }
 if(S.SelfTest&&!S.InvitationOnlyTest&&!S.PhoneUseOnlyTest&&!S.AmbientOnlyTest&&!S.AmbientDrinkOnlyTest&&!S.AmbientSelfieOnlyTest&&!S.AmbientGroomOnlyTest&&S.AllReady&&!S.ContactsChecked){RunContactsProbe(Now);return;}
 if(S.SelfTest&&!S.InvitationOnlyTest&&!S.PhoneUseOnlyTest&&!S.AmbientOnlyTest&&!S.AmbientDrinkOnlyTest&&!S.AmbientSelfieOnlyTest&&!S.AmbientGroomOnlyTest&&S.AllReady&&!S.MinimapChecked){RunMinimapProbe(Now);return;}
 if(S.MinimapOnlyTest){if(S.MinimapChecked)WriteReport(S.Errors.IsEmpty());return;}
 if(S.RetargetSelfTest){
  if(Now-S.StartedAt>480){S.Errors.Add(TEXT("Street retarget self-test timeout"));WriteReport(false);return;}
  if(S.AllReady)WriteReport(S.Errors.IsEmpty()&&S.IdentityRetargetRejected==0&&S.IdentityRetargetReplayed==1&&S.ActiveActorInterruptCommits==0&&S.CompletedLines==0&&S.Target==INDEX_NONE);
 }else if(S.ContactsOnlyTest){
  if(S.AllReady)RunContactsProbe(Now);return;
 }else if(S.AmbientOnlyTest||S.AmbientDrinkOnlyTest||S.AmbientSelfieOnlyTest||S.AmbientGroomOnlyTest){
  if(Now-S.StartedAt>300){S.Errors.Add(TEXT("Autonomous phone smoke timeout"));WriteReport(false);return;}if(S.AllReady){RunAmbientActivityProbe(Now);if(S.AmbientChecked)WriteReport(S.Errors.IsEmpty());}return;
 }else if(S.PhoneUseOnlyTest){
  if(Now-S.StartedAt>240){S.Errors.Add(TEXT("Phone use smoke timeout"));WriteReport(false);return;}if(S.AllReady){if(!S.PhoneUseChecked)RunPhoneUseProbe(Now);else WriteReport(S.Errors.IsEmpty()&&S.PhoneUseActorsVerified==5&&S.PhoneUseCancelChecked&&S.PhoneUseSamples>10&&S.PhoneUseVoices==5);}return;
 }else if(S.InvitationOnlyTest){
  if(Now-S.StartedAt>300){S.Errors.Add(TEXT("Invitation smoke timeout"));WriteReport(false);return;}if(S.AllReady)RunInvitationProbe(Now);return;
 }else if(S.FacilityOnlyTest){
  if(Now-S.StartedAt>240){S.Errors.Add(TEXT("Facility smoke timeout"));WriteReport(false);return;}if(S.AllReady){if(!S.FacilitiesChecked)RunFacilityProbe(Now);else WriteReport(S.Errors.IsEmpty()&&S.FacilitiesVerified==3&&S.FacilityPrepareCancels==3&&S.FacilityClockChecks==3);}return;
 }else if(S.MelonOnlyTest){
  if(Now-S.StartedAt>300){S.Errors.Add(TEXT("Watermelon smoke timeout"));WriteReport(false);return;}if(S.AllReady){if(!S.MelonChecked)RunWatermelonProbe(Now);else WriteReport(S.Errors.IsEmpty()&&S.MelonCasesVerified==10&&S.MelonCancelChecked);}return;
 }else if(S.MealOnlyTest){
  if(Now-S.StartedAt>240){S.Errors.Add(TEXT("Meal smoke timeout"));WriteReport(false);return;}
  if(S.AllReady){if(!S.MealChecked)RunMealProbe(Now);else WriteReport(S.Errors.IsEmpty()&&S.MealActorsVerified==5&&S.MealVoices==5&&S.MealCancelChecked);}return;
 }else if(S.SplashOnlyTest){
  if(Now-S.StartedAt>240){S.Errors.Add(TEXT("Splash smoke timeout"));WriteReport(false);return;}
  if(S.AllReady){if(!S.SplashChecked)RunSplashProbe(Now);else WriteReport(S.Errors.IsEmpty()&&S.SplashCasesVerified==10&&S.SplashCancelChecked&&S.SplashEffectSamples>0);}return;
 }else if(S.ReadingOnlyTest){
  if(Now-S.StartedAt>180){S.Errors.Add(TEXT("Reading book smoke timeout"));WriteReport(false);return;}
  if(S.AllReady){if(!S.ReadingChecked)RunReadingProbe(Now);else WriteReport(S.Errors.IsEmpty()&&S.ReadingActorsVerified==6);}return;
 }else if(S.DrinkOnlyTest){
  if(Now-S.StartedAt>240){S.Errors.Add(TEXT("Drink smoke timeout"));WriteReport(false);return;}
  if(S.AllReady){if(!S.DrinkChecked)RunDrinkProbe(Now);else WriteReport(S.Errors.IsEmpty()&&S.DrinkActorsVerified==5&&S.DrinkVoices==5&&S.DrinkCancelChecked);}return;
 }else if(S.SeatedOnlyTest){
  if(Now-S.StartedAt>240){S.Errors.Add(TEXT("Seated conversation smoke timeout"));WriteReport(false);return;}
  if(S.AllReady){if(!S.SeatedChecked)RunSeatedProbe(Now);else WriteReport(S.Errors.IsEmpty()&&S.SeatedActorsVerified==5&&S.SeatedVoices==5&&S.SeatedCancelChecked);}return;
 }else if(S.PhotoOnlyTest){
  if(Now-S.StartedAt>180){S.Errors.Add(TEXT("Paired photo smoke timeout"));WriteReport(false);return;}
  if(S.AllReady){if(!S.PhotoChecked)RunPhotoProbe(Now);else WriteReport(S.Errors.IsEmpty()&&S.PhotoActorsVerified==5&&S.PhotosSaved==5&&S.PhotoCancelChecked);}return;
 }else if(S.SelfTest){
  if(S.AllReady&&!S.MouseModeChecked){
   auto* V=GEngine->GameViewport.Get();bool Passed=true;
   const bool WasUI=S.MouseUI;
   S.MouseUI=true;ApplyMouseMode(true);Passed&=PC->bShowMouseCursor&&V->GetMouseCaptureMode()==EMouseCaptureMode::NoCapture&&V->GetMouseLockMode()==EMouseLockMode::DoNotLock;
   const bool Takes=!OKControlActive(); // under the control channel the mouse stays free
   S.MouseUI=false;ApplyMouseMode(true);Passed&=Takes?!PC->bShowMouseCursor&&V->GetMouseCaptureMode()==EMouseCaptureMode::CapturePermanently_IncludingInitialMouseDown&&V->GetMouseLockMode()==EMouseLockMode::LockAlways:PC->bShowMouseCursor&&V->GetMouseLockMode()==EMouseLockMode::DoNotLock;
   ApplyMouseMode(false);Passed&=PC->bShowMouseCursor&&V->GetMouseCaptureMode()==EMouseCaptureMode::NoCapture;
   ApplyMouseMode(true);Passed&=Takes?!PC->bShowMouseCursor&&V->GetMouseLockMode()==EMouseLockMode::LockAlways:V->GetMouseLockMode()==EMouseLockMode::DoNotLock;
   // Read back submitted player motion in each cardinal/diagonal direction.
   for(const FVector Move:{FVector(1,0,0),FVector(-1,0,0),FVector(0,1,0),FVector(0,-1,0),FVector(1,1,0)}){
    FOKRuntimeCharacterStatus Submitted;const FRotator Rotation=OKStreetFacingMovement(Move,S.Rotations[0]);
    Passed&=OKSetCrowdRuntimeCharacterMotion(this,0,FTransform(Rotation,S.Male),1,Error)&&OKGetCrowdRuntimeCharacterStatus(this,0,Submitted)&&FVector::DotProduct(Submitted.Transform.GetRotation().RotateVector(FVector::YAxisVector),Move.GetSafeNormal())>.999f;++S.FacingChecks;
   }
   OKSetCrowdRuntimeCharacterMotion(this,0,FTransform(S.Rotations[0],S.Male),0,Error);
   S.MouseUI=WasUI;ApplyMouseMode(Focused);S.MouseModeChecked=Passed;if(!Passed){S.Errors.Add(TEXT("Player input mode/facing regression failed"));WriteReport(false);return;}
  }
  if(S.AllReady&&!S.FaceValidationChecked){for(int32 I=1;I<6;++I){FOKRuntimeFaceStatus Before,After;OKGetCrowdRuntimeFaceStatus(this,I,Before);FKKRuntimeFaceState Invalid;Invalid.EyePattern=10000;FString InvalidError;if(OKSetCrowdRuntimeFaceExpression(this,I,Invalid,true,InvalidError)||InvalidError.IsEmpty())S.Errors.Add(TEXT("Invalid face pattern was admitted"));OKGetCrowdRuntimeFaceStatus(this,I,After);if(After.Active!=Before.Active||After.Requests!=Before.Requests||After.Updates!=Before.Updates||After.Restores!=Before.Restores||After.Started!=Before.Started||After.Intermediate!=Before.Intermediate||After.Completed!=Before.Completed||After.Cancelled!=Before.Cancelled)S.Errors.Add(TEXT("Invalid face pattern mutated state"));}S.FaceValidationChecked=true;}
  if(S.InterruptPhase==0&&S.TestStep==1&&S.TestNpc==1&&!S.CancelChecked&&S.Target==1&&OKCrowdRuntimeSpeechMouth(this,S.Target)>0){S.InterruptSaveBefore=OKStreetWriteSave(S.Store);S.InjectFaultSlot=S.Target;S.InjectActiveFault=true;S.InterruptPhase=1;}
  else if(S.InterruptPhase==2&&!S.InterruptEndedThisTick){EOKRuntimeSpeechState SpeechStatus;FString CancelError;const bool Cancelled=OKGetCrowdRuntimeSpeechState(this,1,SpeechStatus,CancelError)&&SpeechStatus==EOKRuntimeSpeechState::Cancelled;if(S.Target!=INDEX_NONE||!Cancelled||OKStreetWriteSave(S.Store)!=S.InterruptSaveBefore||S.ActiveActorInterruptCommits!=0||S.ActiveActorInterrupts<1||S.VoicedEffects.Num()!=0||S.PositiveFavorCommits!=0)S.Errors.Add(TEXT("Active speech cancellation changed state or did not cancel"));S.CancelChecked=true;S.InterruptPhase=0;if(!BeginTalk(1)){S.Errors.Add(TEXT("Cancelled NPC failed restart"));WriteReport(false);return;}}
  if(S.TestStep==2&&S.PeakMouth>0&&!S.ConversationShot){FScreenshotRequest::RequestScreenshot(FPaths::GetPath(S.Manifest.SocialReport)/TEXT("street.dialogue.png"),true,false);S.ConversationShot=true;}
  if(Now-S.StartedAt>2400){S.Errors.Add(TEXT("Street self-test timeout"));WriteReport(false);return;}
  if(S.AllReady&&S.CandidateProbePhase<3)RunCandidateProbe();
  else if(S.AllReady&&!S.RecoveryAttempted)RunRecoveryProbe();
  else if(S.AllReady&&S.ActivityProbePhase<7)RunActivityProbe();
  else if(S.AllReady&&S.VoicedPreviewPhase<3)RunVoicedPreviewProbe();
  else if(S.AllReady&&S.TestStep==0){S.Male=FVector(0,-700,0);S.Agents[S.TestNpc-1].Position=S.Male+FVector(150,0,0);S.Agents[S.TestNpc-1].Held=true;if(!BeginTalk(S.TestNpc)){S.Errors.Add(TEXT("Ready NPC could not begin conversation"));WriteReport(false);return;}S.TestStep=1;}
  if(S.InterruptPhase==0&&S.TestStep>=1&&S.TestStep<=4&&S.Node==INDEX_NONE){
   if(S.TestStep>1&&S.CompletedLines<(S.TestNpc-1)*3+S.TestStep-1){S.Errors.Add(TEXT("No playable supported topic"));WriteReport(false);return;}
   if(S.TestStep==1){if(S.Greetings!=S.TestNpc)S.Errors.Add(TEXT("Greeting did not play"));Topic(TEXT("daily"));}
   else if(S.TestStep==2)Topic(TEXT("romance"));else if(S.TestStep==3)Topic(TEXT("intimacy"));else Farewell();++S.TestStep;
  }
  else if(S.TestStep==5&&S.Node==INDEX_NONE&&S.Target==INDEX_NONE){
   FOKStreetStore Reloaded;const bool Saved=OKStreetLoadSaveFile(S.Manifest.SavePath,Reloaded,Error)==EOKStreetLoad::Ok&&OKStreetWriteSave(Reloaded)==OKStreetWriteSave(S.Store);
   if(!Saved)S.Errors.Add(TEXT("Save roundtrip mismatch"));if(S.PeakMouth<=0)S.Errors.Add(TEXT("Speech never drove mouth"));if(S.Farewells!=S.TestNpc)S.Errors.Add(TEXT("Farewell did not play"));
   if(S.TestNpc==5){S.FavorSaveVerified=Saved&&OKStreetVoicedReloadAgrees(S.VoicedEffects,Reloaded,S.PositiveFavorCommits,S.CompletedLines);if(!S.FavorSaveVerified)S.Errors.Add(TEXT("Voiced favor reload disagreed"));if(S.PositiveFavorCommits<1)S.Errors.Add(TEXT("No voiced topic produced a positive favor change"));if(S.VoicedEffects.Num()!=S.CompletedLines)S.Errors.Add(TEXT("Voiced topic records do not match committed lines"));}
   if(S.TestNpc<5){++S.TestNpc;S.TestStep=0;if(S.TestNpc==5)ToggleLanguage();return;}
   S.RelationshipChecked=RunRelationshipSelfTest();FScreenshotRequest::RequestScreenshot(FPaths::GetPath(S.Manifest.SocialReport)/TEXT("street.selftest.png"),true,false);S.TestDoneAt=Now;S.TestStep=6;
  }
  else if(S.TestStep==6&&Now-S.TestDoneAt>2&&!S.CollectedProbeChecked)RunCollectedTopicProbe();
  else if(S.TestStep==6&&Now-S.TestDoneAt>2&&!S.FacilitiesChecked)RunFacilityProbe(Now);
  else if(S.TestStep==6&&Now-S.TestDoneAt>2&&!S.CompanionChecked)RunCompanionProbe(Now);
  else if(S.TestStep==6&&Now-S.TestDoneAt>2&&!S.PhotoChecked)RunPhotoProbe(Now);
  else if(S.TestStep==6&&Now-S.TestDoneAt>2&&!S.SeatedChecked)RunSeatedProbe(Now);
  else if(S.TestStep==6&&Now-S.TestDoneAt>2&&!S.ReadingChecked)RunReadingProbe(Now);
  else if(S.TestStep==6&&Now-S.TestDoneAt>2&&!S.PhoneUseChecked)RunPhoneUseProbe(Now);
  else if(S.TestStep==6&&Now-S.TestDoneAt>2&&!S.DrinkChecked)RunDrinkProbe(Now);
  else if(S.TestStep==6&&Now-S.TestDoneAt>2&&!S.SplashChecked)RunSplashProbe(Now);
  else if(S.TestStep==6&&Now-S.TestDoneAt>2&&!S.MealChecked)RunMealProbe(Now);
  else if(S.TestStep==6&&Now-S.TestDoneAt>2&&!S.MelonChecked)RunWatermelonProbe(Now);
  else if(S.TestStep==6&&Now-S.TestDoneAt>2)WriteReport(S.PhoneUseChecked&&S.MelonChecked&&S.MealChecked&&S.SplashChecked&&S.ReadingChecked&&S.DrinkChecked&&S.SeatedChecked&&S.PhotoChecked&&S.CompanionChecked&&S.Errors.IsEmpty()&&S.CompletedLines==15&&S.DualActionSeconds>0&&S.CancelChecked&&S.RelationshipChecked&&S.FaceValidationChecked&&S.ExpressionsApplied>=25&&S.ExpressionsMissing==0&&S.CandidateSelectionChecked&&S.ActivityStateChecked&&S.RecoveryChecked&&S.VoicedPreviewChecked&&S.FacilitiesVerified==3);

 }
 if(!S.AllReady&&Now>S.NextUI){S.NextUI=Now+2;S.Message=OKMsg(TEXT("ui.scene.apng_ready"), {{TEXT("visible_count"), Visible}, {TEXT("total_count"), S.Manifest.Characters.Num()}});RefreshUI();}
}


void AOKStreetSocialScene::RunCollectedTopicProbe(){
 auto& S=*State;
 auto Restore=[&](bool Passed){EndTalk();S.Store=S.TopicProbeStore;S.Manifest.SavePath=S.TopicProbeSavePath;S.Male=S.TopicProbeSnapshot.Male;S.Agents=S.TopicProbeAgents;S.Rotations=S.TopicProbeSnapshot.Rotations;S.Greetings=S.TopicProbeSnapshot.Greetings;S.CollectedProbeChecked=Passed;S.TopicProbePhase=12;if(!Passed)WriteReport(false);};
 auto Require=[&](bool Good,const TCHAR* Message){if(Good)return true;S.Errors.Add(Message);Restore(false);return false;};
 auto SaveFixture=[&](){FString Error;return OKStreetAtomicSave(S.Manifest.SavePath,OKStreetWriteSave(S.Store),Error);};
 auto InjectMissingTouch=[&](){
  // Explicit isolated fault fixture; source-complete packs must not depend on a real missing clip.
  auto* Profile=S.CollectedTopics.Profiles.FindByPredicate([&](const FOKStreetTopicProfile& P){return P.Personality==S.Manifest.Characters[1].PersonalityId;});
  auto* Row=Profile?Profile->Touch.FindByPredicate([](const FOKStreetTouch& T){return T.Stage==2&&T.Part==3&&!T.See&&T.Repeat==2;}):nullptr;
  if(!Row)return false;const bool Available=Row->Available;Row->Available=false;Touch(1);Row->Available=Available;return true;
 };
 if(S.TopicProbePhase==0){
  S.TopicProbeStore=S.Store;S.TopicProbeSavePath=S.Manifest.SavePath;S.TopicProbeSnapshot.Male=S.Male;S.TopicProbeSnapshot.Rotations=S.Rotations;S.TopicProbeSnapshot.Greetings=S.Greetings;S.TopicProbeAgents=S.Agents;
  S.Manifest.SavePath=FPaths::GetPath(S.Manifest.SocialReport)/TEXT("collected-topics.relationships.json");S.Store.Relations.Reset();S.Store.TopicStock.Reset();S.Store.TopicClaims.Reset();
  if(!Require(!S.TopicPickups.IsEmpty(),TEXT("Topic scene has no runtime pickups")))return;
  S.Male=S.TopicPickups[0]->GetActorLocation();S.Male.Z=0;PickTopic(0);const int32 No=S.PickupTopics[0];const FString Picked=OKStreetWriteSave(S.Store);PickTopic(0);
  if(!Require(S.Store.TopicStock.FindRef(No)==1&&OKStreetWriteSave(S.Store)==Picked,TEXT("Live pickup missing or repeated the same daily claim")))return;
  S.Male=FVector(0,-700,0);S.Agents[0].Position=S.Male+FVector(150,0,0);S.Agents[0].Held=true;
  if(!Require(BeginTalk(1),TEXT("Collected topic NPC was not eligible")))return;S.TopicProbePhase=1;return;
 }
 if(S.Node!=INDEX_NONE&&S.TopicProbePhase!=12)return;
 if(S.TopicProbePhase==1){AskHerTopic();if(!Require(S.CollectedActive,TEXT("Girl topic request did not start")))return;S.TopicProbePhase=2;return;}
 if(S.TopicProbePhase==2||S.TopicProbePhase==4||S.TopicProbePhase==6){
  if(!S.AwaitTopicAnswer)return;const auto* Asked=S.CollectedTopics.Find(S.AskedTopic);const FOKStreetCollectedTopic* Answer=Asked;
  if(S.TopicProbePhase!=2){Answer=nullptr;for(const auto& T:S.CollectedTopics.Topics)if(T.No!=Asked->No&&(S.TopicProbePhase==4?T.Category==Asked->Category:T.Category!=Asked->Category)){Answer=&T;break;}}
  if(!Require(Answer!=nullptr,TEXT("No reply fixture for exact/category/mismatch")))return;
  S.Store.TopicStock.Add(Answer->No,1);const FString Id=OKStreetStableId(S.Manifest.Characters[1]);const auto Before=OKStreetViewRelation(S.Store,Id,S.Manifest.TalkBudget);S.TopicProbeSnapshot.Favor=FMath::RoundToInt(Before.Favor);
  float ExpectedFavor=0;{auto Worth=OKRulesRequest(TEXT("topic_effect"));Worth->SetNumberField(TEXT("answer"),Answer->No);Worth->SetNumberField(TEXT("asked"),Asked->No);TArray<TSharedPtr<FJsonValue>> Interests;for(const int32 C:S.Manifest.Characters[1].InterestCategories)Interests.Add(MakeShared<FJsonValueNumber>(C));Worth->SetArrayField(TEXT("interests"),Interests);
   if(const TSharedPtr<FJsonObject> Out=OKRulesEval(S,Worth))ExpectedFavor=float(Out->GetObjectField(TEXT("effect"))->GetNumberField(TEXT("favor")));}
  S.TopicProbeSnapshot.Lines=FMath::RoundToInt(ExpectedFavor);S.TopicProbeSnapshot.Transactions=Answer->No;
  if(!Require(SaveFixture(),TEXT("Reply fixture save failed")))return;UseCollectedTopic(Answer->No);++S.TopicProbePhase;return;
 }
 if(S.TopicProbePhase==3||S.TopicProbePhase==5||S.TopicProbePhase==7){
  if(S.CollectedActive)return;const auto After=OKStreetViewRelation(S.Store,OKStreetStableId(S.Manifest.Characters[1]),S.Manifest.TalkBudget);
  if(!Require(S.Store.TopicStock.FindRef(S.TopicProbeSnapshot.Transactions)==0&&FMath::IsNearlyEqual(After.Favor,float(S.TopicProbeSnapshot.Favor+S.TopicProbeSnapshot.Lines)),TEXT("Collected reply effect/stock differs from exact source table")))return;
  const FString Committed=OKStreetWriteSave(S.Store);CompleteCollectedTopic();if(!Require(OKStreetWriteSave(S.Store)==Committed,TEXT("Completed topic committed twice")))return;
  AskHerTopic();++S.TopicProbePhase;return;
 }
 if(S.TopicProbePhase==8){
  if(!S.AwaitTopicAnswer)return;const FString Before=OKStreetWriteSave(S.Store),BeforeQuestion=S.Message,BeforePlayer=S.PlayerLine;const bool BeforeLanguage=S.Japanese;
  if(!Require(OKStreetConversationBusy(false,S.CollectedActive,S.RelationshipPending,S.TouchActive),TEXT("Waiting girl question was incorrectly presented as idle")))return;
  ToggleLanguage();if(!Require(S.Japanese==BeforeLanguage&&S.Message==BeforeQuestion&&S.PlayerLine==BeforePlayer&&S.AwaitTopicAnswer&&S.CollectedActive&&OKStreetWriteSave(S.Store)==Before,TEXT("Language toggle replaced a pending question or transaction")))return;
  Farewell();if(!Require(S.Target==INDEX_NONE&&!S.AwaitTopicAnswer&&!S.CollectedActive&&S.TopicSequence.IsEmpty()&&S.UsedTopic==INDEX_NONE&&OKStreetWriteSave(S.Store)==Before,TEXT("Cancel button did not release pending question without effects")))return;S.ConversationControlsChecked=true;
  S.Agents[0].Held=true;if(!Require(BeginTalk(1),TEXT("Topic cancellation did not allow new talk")))return;S.TopicProbePhase=9;return;
 }
 if(S.TopicProbePhase==9){
  const FOKStreetCollectedTopic* Erotic=nullptr;for(const auto& T:S.CollectedTopics.Topics)if(T.Category==4&&T.Rarity<=1){Erotic=&T;break;}
  if(!Require(Erotic!=nullptr,TEXT("No source erotic topic available")))return;S.Store.TopicStock.Add(Erotic->No,1);if(!Require(SaveFixture(),TEXT("Erotic fixture save failed")))return;UseCollectedTopic(Erotic->No);S.TopicProbeSnapshot.Transactions=Erotic->No;S.TopicProbePhase=10;return;
 }
 if(S.TopicProbePhase==10){
  if(S.CollectedActive)return;FOKStreetStore Loaded;FString Error;const auto Relation=OKStreetViewRelation(S.Store,OKStreetStableId(S.Manifest.Characters[1]),S.Manifest.TalkBudget);
  if(!Require(Relation.Lewdness>0&&Relation.SexDesire==10&&S.Store.TopicStock.FindRef(S.TopicProbeSnapshot.Transactions)==0&&OKStreetLoadSaveFile(S.Manifest.SavePath,Loaded,Error)==EOKStreetLoad::Ok&&OKStreetWriteSave(Loaded)==OKStreetWriteSave(S.Store),TEXT("Erotic source effect, stock, desire or save roundtrip failed")))return;
  FScreenshotRequest::RequestScreenshot(FPaths::GetPath(S.Manifest.SocialReport)/TEXT("street.collected-topics.png"),true,false);S.TopicProbePhase=11;return;
 }
 if(S.TopicProbePhase==11){
  // Interrupt a real collectible voice, then check its reserved stock survived.
  // The four preceding topic cases consume most of the production budget;
  // give this isolated touch fixture a fresh day's allowance.
  const int32 RelationIndex=OKStreetRelationIndex(S.Store,OKStreetStableId(S.Manifest.Characters[1]),S.Manifest.TalkBudget,true);S.Store.Relations[RelationIndex].TalksRemaining=S.Manifest.TalkBudget;
  S.Store.TopicStock.Add(0,1);if(!Require(SaveFixture(),TEXT("Touch interruption fixture save failed")))return;
  S.TopicProbeSnapshot.Save=OKStreetWriteSave(S.Store);UseCollectedTopic(0);if(!Require(S.CollectedActive,TEXT("Touch interrupt source reply did not start")))return;S.TopicProbePhase=12;return;
 }
 if(S.TopicProbePhase==12){
  if(OKCrowdRuntimeSpeechMouth(this,S.Target)<=0)return;
  if(!Require(S.InteractionPanel.IsValid()&&S.MouseInput.IsValid()&&S.MouseInput->PointerDown&&S.InteractionPanel->GetCachedGeometry().GetLocalSize().X>0,TEXT("Touch routing panel geometry unavailable")))return;
  const auto Geometry=S.InteractionPanel->GetCachedGeometry();S.MouseInput->PointerDown(Geometry.LocalToAbsolute(Geometry.GetLocalSize()*.5f));
  if(!Require(S.WorldTouchPointerBlocked&&!OKStreetWorldTouchAllowed(true,true,true,false,S.WorldTouchPointerBlocked),TEXT("UI press also admitted world touch")))return;
  S.MouseInput->PointerDown(Geometry.LocalToAbsolute(FVector2D(-20,-20)));
  if(!Require(!S.WorldTouchPointerBlocked&&OKStreetWorldTouchAllowed(true,true,true,false,S.WorldTouchPointerBlocked),TEXT("World press outside panel was incorrectly blocked")))return;S.TouchInputRoutingChecked=true;
  // Keep the real old audio playing while testing an unavailable source reaction.
  const int32 RelationIndex=OKStreetRelationIndex(S.Store,OKStreetStableId(S.Manifest.Characters[1]),S.Manifest.TalkBudget,true);const FString OldStage=S.Store.Relations[RelationIndex].Stage;
  S.Store.Relations[RelationIndex].Stage=TEXT("lover");S.TouchCounts.Add(3,1);const FString OldSave=OKStreetWriteSave(S.Store),OldMessage=S.Message,OldPlayer=S.PlayerLine;const int32 OldNode=S.Node;const auto OldVoices=S.VoiceIds;
  if(!Require(InjectMissingTouch(),TEXT("Missing-touch fixture unavailable")))return;if(!Require(!S.TouchNotice.IsEmpty()&&S.Message==OldMessage&&S.PlayerLine==OldPlayer&&S.Node==OldNode&&S.VoiceIds==OldVoices&&S.CollectedActive&&!S.TouchActive&&OKStreetWriteSave(S.Store)==OldSave&&OKCrowdRuntimeSpeechMouth(this,S.Target)>0,TEXT("Missing touch replaced live subtitles, speech or reserved topic")))return;
  S.Store.Relations[RelationIndex].Stage=OldStage;S.TouchMissingSubtitleChecked=true;
  FVector Hand;if(!Require(OKGetCrowdRuntimeBonePosition(this,S.Target,TEXT("cf_j_hand_L"),Hand)&&!Hand.ContainsNaN(),TEXT("Sparse private GPU bone query failed")))return;
  Touch(2);if(!Require(S.TouchActive&&!S.CollectedActive&&OKStreetWriteSave(S.Store)==S.TopicProbeSnapshot.Save,TEXT("Touch interruption consumed the old reply or changed its effects")))return;S.TopicProbePhase=13;return;
 }
 if(S.TopicProbePhase>=13&&S.TopicProbePhase<=15){
  if(S.TouchActive)return;
  if(!Require(S.TouchCommits==S.TopicProbePhase-12&&S.Store.TopicStock.FindRef(0)==1,TEXT("Touch source effect did not commit exactly once or interrupted stock changed")))return;
  if(S.TopicProbePhase<15){Touch(S.TopicProbePhase==13?3:2);if(!Require(S.TouchActive,TEXT("Repeated hand touch was rejected")))return;++S.TopicProbePhase;return;}
  S.TopicProbeSnapshot.Save=OKStreetWriteSave(S.Store);Touch(0);if(!Require(S.TouchActive,TEXT("Touch cancel reaction did not start")))return;EndTalk();
  if(!Require(OKStreetWriteSave(S.Store)==S.TopicProbeSnapshot.Save,TEXT("Cancelled touch committed")))return;
  FOKStreetStore Loaded;FString Error;if(!Require(OKStreetLoadSaveFile(S.Manifest.SavePath,Loaded,Error)==EOKStreetLoad::Ok&&OKStreetWriteSave(Loaded)==OKStreetWriteSave(S.Store),TEXT("Touch save roundtrip failed")))return;
  // Inject unavailability only in the isolated fixture; never corrupt shared source data.
  S.Target=1;S.TouchSee=false;S.Store.Relations[OKStreetRelationIndex(S.Store,OKStreetStableId(S.Manifest.Characters[1]),S.Manifest.TalkBudget,true)].Stage=TEXT("lover");S.TouchCounts.Add(3,1);
  const FString BeforeMissing=OKStreetWriteSave(S.Store);const int32 BeforeMissingCommits=S.TouchCommits;
  if(!Require(InjectMissingTouch(),TEXT("Recovery fixture unavailable")))return;if(!Require(!S.TouchActive&&S.Node==INDEX_NONE&&S.TouchCounts.FindRef(3)==2&&OKStreetWriteSave(S.Store)==BeforeMissing&&S.TouchCommits==BeforeMissingCommits,TEXT("Missing touch altered save/effects or trapped repeat counter")))return;
  Touch(1);if(!Require(S.TouchActive&&S.TouchRepeat==1&&S.TouchCounts.FindRef(3)==3,TEXT("Third cheek touch did not recover after exact missing source")))return;EndTalk();
  if(!Require(OKStreetWriteSave(S.Store)==BeforeMissing&&S.TouchCommits==BeforeMissingCommits,TEXT("Cancelled recovered touch committed")))return;
  FOKStreetStore AfterMissing;if(!Require(OKStreetLoadSaveFile(S.Manifest.SavePath,AfterMissing,Error)==EOKStreetLoad::Ok&&OKStreetWriteSave(AfterMissing)==OKStreetWriteSave(Loaded),TEXT("Missing or cancelled recovered touch wrote disk save")))return;
  S.TouchMissingRecoveryChecked=true;S.TouchChecked=true;Restore(true);
 }
}

void AOKStreetSocialScene::RunCandidateProbe(){
 auto& S=*State;
 auto Restore=[&](){
  S.Male=S.Probe.Male;S.Yaw=S.Probe.Yaw;S.Pitch=S.Probe.Pitch;S.Distance=S.Probe.Distance;
  for(int32 Index=0;Index<S.Agents.Num()&&Index<S.Probe.Positions.Num();++Index){
   S.Agents[Index].Position=S.Probe.Positions[Index];
   S.Agents[Index].Held=S.Probe.Held.IsValidIndex(Index)&&S.Probe.Held[Index]!=0;
   if(S.Probe.Modes.IsValidIndex(Index))S.Agents[Index].Mode=static_cast<EOKStreetMove>(S.Probe.Modes[Index]);
  }
  if(S.Probe.Rotations.Num()==S.Rotations.Num())S.Rotations=S.Probe.Rotations;
  S.Candidate=INDEX_NONE;S.CandidateCount=0;S.CandidateExplicit=false;
  if(OKStreetWriteSave(S.Store)!=S.Probe.Save){FString Ignored;FOKStreetStore Reloaded;if(OKStreetParseSave(S.Probe.Save,Reloaded,Ignored)==EOKStreetLoad::Ok)S.Store=MoveTemp(Reloaded);}
 };
 auto Finish=[&](bool Passed){Restore();S.CandidateSelectionChecked=Passed&&S.Errors.Num()==S.Probe.Errors;S.CandidateProbePhase=3;};
 if(S.CandidateProbePhase==0){
  S.Probe.Male=S.Male;S.Probe.Yaw=S.Yaw;S.Probe.Pitch=S.Pitch;S.Probe.Distance=S.Distance;S.Probe.Rotations=S.Rotations;S.Probe.Save=OKStreetWriteSave(S.Store);
  S.Probe.SaveExisted=!S.Manifest.SavePath.IsEmpty()&&IFileManager::Get().FileExists(*S.Manifest.SavePath);
  if(S.Probe.SaveExisted)FFileHelper::LoadFileToString(S.Probe.SaveDisk,*S.Manifest.SavePath);
  S.Probe.Transactions=S.Transactions.Num();S.Probe.Favor=S.PositiveFavorCommits;S.Probe.Lines=S.CompletedLines;S.Probe.Voiced=S.VoicedEffects.Num();S.Probe.Greetings=S.Greetings;S.Probe.Farewells=S.Farewells;S.Probe.Errors=S.Errors.Num();S.Probe.ExpressionsMissing=S.ExpressionsMissing;S.Probe.Branches=S.BranchesCompleted;S.Probe.Refusals=S.RefusalsCompleted;
  S.Probe.Positions.Reset();S.Probe.Held.Reset();S.Probe.Modes.Reset();
  for(const FOKStreetAgentSim& Agent:S.Agents){S.Probe.Positions.Add(Agent.Position);S.Probe.Held.Add(Agent.Held?1:0);S.Probe.Modes.Add(static_cast<uint8>(Agent.Mode));}
  const FVector Home=OKStreetMaleSpawn();S.Male=Home;S.Yaw=90.f;S.Pitch=0.f;S.Candidate=INDEX_NONE;S.CandidateCount=0;S.CandidateExplicit=false;
  for(FOKStreetAgentSim& Agent:S.Agents){
   Agent.Held=true;Agent.Position=FVector(0.f,2200.f+Agent.Slot*400.f,0.f);
   if(Agent.Slot==1)Agent.Position=Home+FVector(0.f,200.f,0.f);
   else if(Agent.Slot==2)Agent.Position=Home+FVector(0.f,-80.f,0.f);
   else if(Agent.Slot==3)Agent.Position=Home+FVector(80.f,0.f,0.f);
   Agent.Position.Z=0.f;
  }
  S.CandidateProbePhase=1;return;
 }
 FOKRuntimeCharacterStatus Statuses[OKStreetCastCount];bool Queried[OKStreetCastCount]={};
 for(int32 Index=0;Index<FMath::Min(S.Manifest.Characters.Num(),OKStreetCastCount);++Index)Queried[Index]=OKGetCrowdRuntimeCharacterStatus(this,Index,Statuses[Index]);
 TArray<FOKStreetTalkCandidateSample> Samples;OKStreetBuildTalkSamples(GetWorld(),this,S.Male,S.Agents,S.Manifest.Characters,Statuses,Queried,Samples);
 const FOKStreetTalkViewForward View=OKStreetSceneTalkView(S.Camera.Get(),S.Yaw);
 auto FindSlot=[&](int32 Slot)->const FOKStreetTalkCandidateSample*{for(const FOKStreetTalkCandidateSample& Sample:Samples)if(Sample.Slot==Slot)return &Sample;return nullptr;};
 const FOKStreetTalkCandidateSample* Front=FindSlot(1);const FOKStreetTalkCandidateSample* Behind=FindSlot(2);const FOKStreetTalkCandidateSample* Side=FindSlot(3);
 auto Soft=[&](const FOKStreetTalkCandidateSample* Sample){return !Sample||!Sample->Queried||!Sample->Ready||!Sample->Visible||!Sample->ActionsReady;};
 if(S.CandidateProbePhase==1){
  if(Soft(Front)||Soft(Behind)||Soft(Side))return;
  bool Passed=true;auto Fail=[&](const TCHAR* Why){S.Errors.Add(Why);Passed=false;};
  if(!S.Camera.IsValid()||View.UsedYawFallback)Fail(TEXT("Candidate probe camera forward was degenerate"));
  const FVector YawForward=FRotator(0.f,S.Yaw,0.f).Vector();
  if(View.Horizontal.IsNearlyZero()||FVector::Dist2D(View.Horizontal,FVector(YawForward.X,YawForward.Y,0.f).GetSafeNormal())>0.05f)Fail(TEXT("Candidate probe camera forward did not follow the orbit yaw"));
  if(!Front->Female||!Front->LineOfSightClear||Front->DistanceCm>=OKStreetTalkRangeCm||Front->HasError||!Behind->LineOfSightClear||!Side->LineOfSightClear||Behind->DistanceCm>=OKStreetTalkRangeCm||Side->DistanceCm>=OKStreetTalkRangeCm)Fail(TEXT("Candidate probe placement was blocked, out of range, or not female"));
  const float FrontAlign=OKStreetTalkViewAlignment(View.Horizontal,Front->Offset),SideAlign=OKStreetTalkViewAlignment(View.Horizontal,Side->Offset),BehindAlign=OKStreetTalkViewAlignment(View.Horizontal,Behind->Offset);
  if(!(FrontAlign>SideAlign+0.5f&&SideAlign>BehindAlign+0.5f))Fail(TEXT("Candidate probe did not place front, side, and behind on the camera axis"));
  FOKStreetTalkSelection Automatic;const FOKStreetTalkSelectionResult Resolved=OKStreetResolveTalkSelection(Automatic,Samples,View.Horizontal,false,false);
  if(Resolved.Selection.Selected!=1||Resolved.Selection.Count!=3||S.Candidate!=1||S.CandidateExplicit||OKStreetSelectTalkCandidate(Samples)!=2)Fail(TEXT("Live camera selection did not prefer the farther front over the nearer side and behind"));
  if(!Passed){Finish(false);return;}
  S.Candidate=1;S.CandidateCount=3;S.CandidateExplicit=true;S.Yaw=0.f;S.CandidateProbePhase=2;return;
 }
 bool Passed=true;auto Fail=[&](const TCHAR* Why){S.Errors.Add(Why);Passed=false;};
 if(Soft(Front)||Soft(Behind)||Soft(Side)||!Front->LineOfSightClear||!Side->LineOfSightClear)Fail(TEXT("Candidate probe lost an eligible NPC after the camera turn"));
 FOKStreetTalkSelection Plain;const FOKStreetTalkSelectionResult Automatic=OKStreetResolveTalkSelection(Plain,Samples,View.Horizontal,false,false);
 FOKStreetTalkSelection Locked;Locked.Selected=1;Locked.Explicit=true;const FOKStreetTalkSelectionResult Kept=OKStreetResolveTalkSelection(Locked,Samples,View.Horizontal,false,false);
 if(S.Candidate!=1||!S.CandidateExplicit||Automatic.Selection.Selected!=3||Kept.Selection.Selected!=1||!Kept.Selection.Explicit)Fail(TEXT("Explicit candidate did not survive the camera change"));
 const FOKStreetTalkSelectionResult Tab1=OKStreetResolveTalkSelection(Kept.Selection,Samples,View.Horizontal,true,false);
 const FOKStreetTalkSelectionResult Tab2=OKStreetResolveTalkSelection(Tab1.Selection,Samples,View.Horizontal,true,false);
 const FOKStreetTalkSelectionResult Tab3=OKStreetResolveTalkSelection(Tab2.Selection,Samples,View.Horizontal,true,false);
 if(!Tab1.Selection.Explicit||Tab1.Selection.Selected!=3||Tab2.Selection.Selected!=2||Tab3.Selection.Selected!=1)Fail(TEXT("Tab did not cycle the live ordered list and wrap"));
 for(FOKStreetAgentSim& Agent:S.Agents)if(Agent.Slot==1)Agent.Position=FVector(0.f,2000.f,0.f);
 TArray<FOKStreetTalkCandidateSample> Moved;OKStreetBuildTalkSamples(GetWorld(),this,S.Male,S.Agents,S.Manifest.Characters,Statuses,Queried,Moved);
 bool MovedOut=false;for(const FOKStreetTalkCandidateSample& Sample:Moved)if(Sample.Slot==1)MovedOut=!OKStreetTalkCandidateEligible(Sample)&&Sample.DistanceCm>=OKStreetTalkRangeCm;
 if(!MovedOut)Fail(TEXT("Moving the selected NPC out of range did not remove eligibility"));
 FOKStreetTalkSelection Stale;Stale.Selected=1;Stale.Explicit=true;const FOKStreetTalkSelectionResult Rejected=OKStreetResolveTalkSelection(Stale,Moved,View.Horizontal,true,true);
 if(!Rejected.RejectedInteract||Rejected.AdmitSlot!=INDEX_NONE||Rejected.Selection.Selected==1||Rejected.Selection.Explicit)Fail(TEXT("Invalidated explicit candidate was still admitted"));
 for(FOKStreetAgentSim& Agent:S.Agents)if(Agent.Slot==1)Agent.Position=OKStreetMaleSpawn()+FVector(0.f,200.f,0.f);
 TArray<FOKStreetTalkCandidateSample> Restored;OKStreetBuildTalkSamples(GetWorld(),this,S.Male,S.Agents,S.Manifest.Characters,Statuses,Queried,Restored);
 const FOKStreetTalkSelectionResult Admit=OKStreetResolveTalkSelection(Locked,Restored,View.Horizontal,false,true);
 if(Admit.AdmitSlot!=1||Admit.RejectedInteract)Fail(TEXT("E did not revalidate the shown candidate"));
 else if(!BeginTalk(Admit.AdmitSlot)||S.Target!=1)Fail(TEXT("BeginTalk did not admit the shown candidate"));
 else EndTalk();
 if(S.Target!=INDEX_NONE||S.Node!=INDEX_NONE||S.RelationshipPending||S.Greetings!=S.Probe.Greetings||S.Farewells!=S.Probe.Farewells||S.CompletedLines!=S.Probe.Lines||S.VoicedEffects.Num()!=S.Probe.Voiced||S.PositiveFavorCommits!=S.Probe.Favor||S.Transactions.Num()!=S.Probe.Transactions||S.BranchesCompleted!=S.Probe.Branches||S.RefusalsCompleted!=S.Probe.Refusals||S.ExpressionsMissing!=S.Probe.ExpressionsMissing)Fail(TEXT("Candidate selection changed dialogue, favor, or expression rejection state"));
 if(OKStreetWriteSave(S.Store)!=S.Probe.Save)Fail(TEXT("Candidate selection changed the relationship store"));
 const bool SaveNow=!S.Manifest.SavePath.IsEmpty()&&IFileManager::Get().FileExists(*S.Manifest.SavePath);
 if(SaveNow!=S.Probe.SaveExisted)Fail(TEXT("Candidate selection created or removed the relationship file"));
 else if(SaveNow){FString Disk;if(!FFileHelper::LoadFileToString(Disk,*S.Manifest.SavePath)||Disk!=S.Probe.SaveDisk)Fail(TEXT("Candidate selection rewrote the relationship file"));}
 if(S.Errors.Num()!=S.Probe.Errors)Passed=false;
 Finish(Passed);
}

void AOKStreetSocialScene::RunVoicedPreviewProbe(){
 auto& S=*State;if(!S.SelfTest)return;
 auto Finish=[&](bool Passed){
  EndTalk();S.Store=S.VoicedPreviewSavedStore;S.Manifest.SavePath=S.VoicedPreviewSavePath;
  S.Transactions.SetNum(S.VoicedPreviewSnapshot.Transactions);
  S.Agents=S.VoicedPreviewSavedAgents;S.Rotations=S.VoicedPreviewSavedRotations;
  S.Male=S.VoicedPreviewSnapshot.Male;S.Yaw=S.VoicedPreviewSnapshot.Yaw;S.Pitch=S.VoicedPreviewSnapshot.Pitch;S.Distance=S.VoicedPreviewSnapshot.Distance;
  S.Candidate=INDEX_NONE;S.CandidateExplicit=false;S.CandidateCount=0;
  S.VoicedPreviewSavedAgents.Reset();S.VoicedPreviewSavedRotations.Reset();
  S.VoicedPreviewChecked=Passed&&S.Errors.Num()==S.VoicedPreviewSnapshot.Errors;S.VoicedPreviewPhase=3;RefreshUI();
 };
 auto Require=[&](bool Value,const TCHAR* Why){if(Value)return true;S.Errors.Add(Why);Finish(false);return false;};
 auto QueueFixture=[&](){
  if(!BeginTalk(1)||S.Target!=1||!S.Catalogue.Nodes.IsValidIndex(S.Node))return false;
  // Exercise the relationship playback/automatic commit pipeline with a real, matched voice.
  // This is self-test routing only, never a replacement for a source ADV event.
  S.ActiveCategory=TEXT("relationship");S.RelationshipPending=true;S.RelationshipVoiceRequired=true;S.RelationshipVoiceCompleted=false;
  S.PendingEffect=FOKStreetEffect{};S.PendingEffect.Favor=3.f;S.BranchLine=false;
  return true;
 };
 if(S.VoicedPreviewPhase==0){
  S.VoicedPreviewSavedStore=S.Store;S.VoicedPreviewSavePath=S.Manifest.SavePath;
  S.VoicedPreviewSavedAgents=S.Agents;S.VoicedPreviewSavedRotations=S.Rotations;
  S.Store.Relations.Reset(); // Isolated fixture keeps common rules, independent of any prior favor cap.
  auto& P=S.VoicedPreviewSnapshot;P.Save=OKStreetWriteSave(S.Store);P.Transactions=S.Transactions.Num();P.Errors=S.Errors.Num();P.Lines=S.CompletedLines;P.Voiced=S.VoicedEffects.Num();P.Favor=S.PositiveFavorCommits;P.Greetings=S.Greetings;P.Farewells=S.Farewells;
  P.Male=S.Male;P.Yaw=S.Yaw;P.Pitch=S.Pitch;P.Distance=S.Distance;
  // Keep this fixture's automatic commit receipt separate from the ordinary scenario and user save.
  S.Manifest.SavePath=FPaths::GetPath(S.Manifest.SocialReport)/TEXT("voiced-auto-commit.relationships.json");
  FString Error;
  if(!Require(OKStreetAtomicSave(S.Manifest.SavePath,P.Save,Error),TEXT("Voiced preview fixture save initialization failed")))return;
  S.Male=OKStreetMaleSpawn();
  for(auto& A:S.Agents){A.Held=true;A.Position=FVector(0,4000+A.Slot*400,0);}
  S.Agents[0].Position=S.Male+FVector(0,200,0);
  if(!Require(QueueFixture(),TEXT("Voiced preview fixture failed to queue its own matched greeting")))return;
  ApplyRelationship();
  if(!Require(S.RelationshipPending&&!S.RelationshipVoiceCompleted&&S.Transactions.Num()==P.Transactions&&OKStreetWriteSave(S.Store)==P.Save,TEXT("Queued voiced preview saved before audio completion")))return;
  S.VoicedPreviewPhase=1;return;
 }
 if(!Require(S.Target==1,TEXT("Voiced relationship lost its conversation")))return;
 const auto& P=S.VoicedPreviewSnapshot;
 if(S.VoicedPreviewPhase==1){
  // Cancel while audio is queued/playing, before any completion callback.
  EndTalk();ApplyRelationship();
  if(!Require(OKStreetWriteSave(S.Store)==P.Save&&S.Transactions.Num()==P.Transactions,TEXT("Cancelled incomplete relationship committed")))return;
  if(!Require(QueueFixture(),TEXT("Cancelled relationship failed its second playback")))return;
  S.VoicedPreviewPhase=2;return;
 }
 if(S.Node!=INDEX_NONE)return;
 EOKRuntimeSpeechState Speech;FString Error;FOKStreetStore Reloaded;
 if(!Require(!S.RelationshipPending&&!S.RelationshipVoiceRequired&&!S.RelationshipVoiceCompleted&&OKGetCrowdRuntimeSpeechState(this,1,Speech,Error)&&Speech==EOKRuntimeSpeechState::Completed,TEXT("Completed relationship did not automatically commit")))return;
 const FString Id=OKStreetStableId(S.Manifest.Characters[1]);
 const auto After=OKStreetViewRelation(S.Store,Id,S.Manifest.TalkBudget);
 if(!Require(S.Transactions.Num()==P.Transactions+1&&FMath::IsNearlyEqual(After.Favor,3.f),TEXT("Automatic relationship result did not commit exactly once")))return;
 const FString Committed=OKStreetWriteSave(S.Store);ApplyRelationship();
 if(!Require(S.Transactions.Num()==P.Transactions+1&&OKStreetWriteSave(S.Store)==Committed&&OKStreetLoadSaveFile(S.Manifest.SavePath,Reloaded,Error)==EOKStreetLoad::Ok&&OKStreetWriteSave(Reloaded)==Committed,TEXT("Automatic relationship duplicated or failed persisted roundtrip")))return;
 if(!Require(S.CompletedLines==P.Lines&&S.VoicedEffects.Num()==P.Voiced&&S.PositiveFavorCommits==P.Favor&&S.Greetings==P.Greetings&&S.Farewells==P.Farewells,TEXT("Relationship fixture contaminated normal voiced-topic counters")))return;
 ++S.VoicedPreviewConfirmations;Finish(true);
}

void AOKStreetSocialScene::RunRecoveryProbe(){
 auto& S=*State;if(!S.SelfTest)return;
 const auto Agents=S.Agents;const auto Rotations=S.Rotations;const FVector Male=S.Male;
 const float Yaw=S.Yaw,Pitch=S.Pitch,Distance=S.Distance;
 const FString StoreBefore=OKStreetWriteSave(S.Store);FString DiskBefore;
 const bool FileBefore=IFileManager::Get().FileExists(*S.Manifest.SavePath);
 if(FileBefore)FFileHelper::LoadFileToString(DiskBefore,*S.Manifest.SavePath);
 const int32 Transactions=S.Transactions.Num(),Effects=S.VoicedEffects.Num(),Lines=S.CompletedLines;
 const int32 Greetings=S.Greetings,Farewells=S.Farewells,Failures=S.ExpectedVoiceAdmissionFailures,Errors=S.Errors.Num();
 auto Check=[&](bool Pass,const TCHAR* Why){if(!Pass)S.Errors.Add(Why);};
 S.Male=OKStreetMaleSpawn();
 for(auto& Agent:S.Agents){Agent.Held=true;Agent.Position=FVector(0,4000+Agent.Slot*400,0);}
 S.Agents[0].Position=S.Male+FVector(0,200,0);
 const bool Admitted=BeginTalk(1)&&S.Target==1&&S.Catalogue.Nodes.IsValidIndex(S.Node);
 Check(Admitted,TEXT("Recovery probe could not admit a ready female and real greeting"));
 if(Admitted){
  const int32 Node=S.Node;
  S.RelationshipPending=true;S.RelationshipVoiceRequired=true;S.RelationshipVoiceCompleted=false;
  ApplyRelationship();
  Check(OKStreetWriteSave(S.Store)==StoreBefore&&S.Transactions.Num()==Transactions,TEXT("Incomplete voiced relationship was allowed to save"));
  const auto Voices=S.Catalogue.Nodes[Node].VoiceFiles;
  S.Catalogue.Nodes[Node].VoiceFiles={FPaths::ProjectSavedDir()/TEXT("StreetSocial/IntentionalMissingVoice.ogg")};
  S.ExpectedVoiceAdmissionFault=true;
  const bool Queued=QueueNode(Node,TEXT("relationship"));
  S.ExpectedVoiceAdmissionFault=false;
  S.Catalogue.Nodes[Node].VoiceFiles=Voices;
  Check(!Queued&&S.ExpectedVoiceAdmissionFailures==Failures+1,TEXT("Recovery probe did not reject the intentional invalid voice"));
  Check(S.Target==INDEX_NONE&&S.Node==INDEX_NONE&&!S.RelationshipPending&&!S.RelationshipVoiceRequired&&!S.RelationshipVoiceCompleted&&!S.BranchLine&&!S.Agents[0].Held,TEXT("Failed voice admission left a relationship or actor reserved"));
  Check(!S.TalkFailureNotice.IsEmpty(),TEXT("Voice failure did not retain its independent retry notice"));
  ApplyRelationship();
  Check(OKStreetWriteSave(S.Store)==StoreBefore&&S.Transactions.Num()==Transactions,TEXT("Failed voiced relationship could still save after abort"));
  Check(BeginTalk(1)&&S.Target==1&&S.Node!=INDEX_NONE&&S.TalkFailureNotice.IsEmpty(),TEXT("Aborted conversation could not retry with its original voice"));
 }
 EndTalk();
 Check(S.CompletedLines==Lines&&S.VoicedEffects.Num()==Effects&&S.Greetings==Greetings&&S.Farewells==Farewells&&S.Transactions.Num()==Transactions&&OKStreetWriteSave(S.Store)==StoreBefore,TEXT("Admission failure or cancelled retry changed dialogue rewards"));
 const bool FileAfter=IFileManager::Get().FileExists(*S.Manifest.SavePath);FString DiskAfter;
 if(FileAfter)FFileHelper::LoadFileToString(DiskAfter,*S.Manifest.SavePath);
 Check(FileBefore==FileAfter&&DiskBefore==DiskAfter,TEXT("Admission failure or cancelled retry changed the save file"));
 S.Agents=Agents;S.Rotations=Rotations;S.Male=Male;S.Yaw=Yaw;S.Pitch=Pitch;S.Distance=Distance;
 S.Candidate=INDEX_NONE;S.CandidateExplicit=false;S.CandidateCount=0;
 S.RecoveryChecked=S.Errors.Num()==Errors;
 S.RecoveryAttempted=true;
 RefreshUI();
}

void AOKStreetSocialScene::RunActivityProbe(){
 auto& S=*State;
 TArray<FOKStreetPointData> Points;TArray<TArray<int32>> Routes;OKStreetWaypoints(Points);OKStreetRoutes(Routes);
 auto Finish=[&](bool Passed){if(S.Target!=INDEX_NONE)EndTalk();S.Male=S.ActivitySnapshot.Male;S.Yaw=S.ActivitySnapshot.Yaw;S.Pitch=S.ActivitySnapshot.Pitch;S.Distance=S.ActivitySnapshot.Distance;S.Agents=S.ActivitySavedAgents;S.Rotations=S.ActivitySavedRotations;S.ActivitySavedAgents.Reset();S.ActivitySavedRotations.Reset();S.ActivityStateChecked=Passed;S.ActivityProbePhase=7;S.Candidate=INDEX_NONE;S.CandidateExplicit=false;S.CandidateCount=0;};
 auto Require=[&](bool Value,const TCHAR* Why){if(Value)return true;S.Errors.Add(Why);Finish(false);return false;};
 if(S.ActivityProbePhase==0){
  S.ActivitySavedAgents=S.Agents;S.ActivitySavedRotations=S.Rotations;
  S.ActivitySnapshot.Male=S.Male;S.ActivitySnapshot.Yaw=S.Yaw;S.ActivitySnapshot.Pitch=S.Pitch;S.ActivitySnapshot.Distance=S.Distance;S.ActivitySnapshot.Save=OKStreetWriteSave(S.Store);
  S.Male=Points[17].Position-FVector(150,0,0);
  for(auto& A:S.Agents){A.Held=true;A.Dwell=0;A.DwellPoint=INDEX_NONE;A.Position=FVector(0,4000+A.Slot*400,0);}
  auto& A=S.Agents[0];A.Held=false;A.Route=2;A.Cursor=Routes[2].Find(17);A.Position=Points[17].Position;
  S.ActivityProbePhase=1;return;
 }
 auto& Visitor=S.Agents[0];
 if(S.ActivityProbePhase==1){
  if(!Require(Visitor.DwellPoint==17&&Visitor.Mode==EOKStreetMove::Looking&&Visitor.Dwell>0&&Routes[Visitor.Route][Visitor.Cursor]!=17,TEXT("Live arrival lost its activity point after route advancement")))return;
  S.ActivityPausedDwell=Visitor.Dwell;
  if(!Require(BeginTalk(Visitor.Slot)&&S.Target==Visitor.Slot,TEXT("Live activity visitor could not enter the production conversation")))return;
  S.ActivityProbePhase=2;return;
 }
 if(S.ActivityProbePhase==2){
  if(!Require(S.Target==Visitor.Slot&&Visitor.Held&&Visitor.Dwell==S.ActivityPausedDwell&&Visitor.DwellPoint==17&&Visitor.Mode==EOKStreetMove::Reserved,TEXT("Live reservation did not pause the current activity")))return;
  EndTalk();
  if(!Require(S.Target==INDEX_NONE&&S.Node==INDEX_NONE&&!Visitor.Held&&OKStreetWriteSave(S.Store)==S.ActivitySnapshot.Save&&S.VoicedEffects.IsEmpty()&&S.Transactions.IsEmpty(),TEXT("Live activity conversation cancel did not release without reward")))return;
  S.ActivityProbePhase=3;return;
 }
 if(S.ActivityProbePhase==3){
  if(!Require(Visitor.Mode==EOKStreetMove::Looking&&Visitor.DwellPoint==17&&Visitor.Dwell<S.ActivityPausedDwell,TEXT("Live reservation release did not resume the arrived activity")))return;
  Visitor.Dwell=0.000001f;S.ActivityProbePhase=4;return;
 }
 if(S.ActivityProbePhase==4){
  if(Visitor.Dwell>0)return;
  if(!Require(Visitor.DwellPoint==INDEX_NONE&&Visitor.Mode==EOKStreetMove::Walking,TEXT("Live elapsed activity did not release its occupancy")))return;
  Visitor.Held=true;Visitor.Position=Points[17].Position;Visitor.DwellPoint=17;Visitor.Dwell=1;
  auto& Waiting=S.Agents[1];Waiting.Held=false;Waiting.Route=2;Waiting.Cursor=Routes[2].Find(17);Waiting.Position=Points[17].Position-FVector(100,0,0);Waiting.Dwell=0;Waiting.DwellPoint=INDEX_NONE;Waiting.Stuck=0;
  S.ActivityProbePhase=5;return;
 }
 auto& Waiting=S.Agents[1];
 if(S.ActivityProbePhase==5){
  if(!Require(Waiting.Mode==EOKStreetMove::Waiting&&Waiting.Dwell==0&&Waiting.Stuck==0&&Waiting.Position.Equals(Points[17].Position-FVector(100,0,0))&&Routes[Waiting.Route][Waiting.Cursor]==17,TEXT("Live occupied stop failed to keep the visitor waiting at its destination")))return;
  Visitor.Dwell=0;Visitor.DwellPoint=INDEX_NONE;Visitor.Position=Points[17].Position+FVector(500,0,0);
  Waiting.Position=Points[17].Position;S.ActivityProbePhase=6;return;
 }
 if(!Require(Waiting.Mode==EOKStreetMove::Looking&&Waiting.DwellPoint==17&&Waiting.Dwell>0,TEXT("Live released stop did not admit the waiting visitor")))return;
 Finish(true);
}

bool AOKStreetSocialScene::RunRelationshipSelfTest(){
 auto& S=*State;if(!S.SelfTest)return false;const int32 BeforeErrors=S.Errors.Num();const auto& Meta=S.Manifest.Characters[1];const FString Id=OKStreetStableId(Meta);const int32 Index=OKStreetRelationIndex(S.Store,Id,S.Manifest.TalkBudget,true);
 auto Check=[&](bool Pass,const TCHAR* Message){if(!Pass)S.Errors.Add(Message);};
 // File-backed source catalogue scenarios run in editor automation; this packaged probe uses the scene manifest.
 S.Store.Relations[Index].Favor=200;S.Store.Relations[Index].Stage=TEXT("friend");S.Store.Relations[Index].TalksRemaining=5;
 FOKStreetEffect Accept;
 for(int32 Roll=0;Roll<100;++Roll){auto Ask=OKRulesTalkRequest(S,TEXT("talk_event"),S.Store.Relations[Index],&Meta);Ask->SetStringField(TEXT("event"),TEXT("confession"));Ask->SetNumberField(TEXT("roll"),Roll);const TSharedPtr<FJsonObject> P=OKRulesEval(S,Ask);
  Check(P&&P->GetBoolField(TEXT("resolved"))&&!P->GetBoolField(TEXT("refusal"))&&int32(P->GetNumberField(TEXT("chance")))==100&&P->GetObjectField(TEXT("effect"))->GetStringField(TEXT("relation_event"))==TEXT("lover"),TEXT("200 favor confession was blocked or refused"));if(P&&Roll==99)Accept=OKRulesReadEffect(*P->GetObjectField(TEXT("effect")));}
 const auto Result=OKRulesTalkCommit(S,S.Store,Id,TEXT("native-guaranteed-confession"),Accept);Check(Result.Applied&&S.Store.Relations[Index].Stage==TEXT("lover")&&S.Store.Relations[Index].Favor==50.f,TEXT("Certain confession did not commit at favor 50"));
 FString Error;Check(OKStreetAtomicSave(S.Manifest.SavePath,OKStreetWriteSave(S.Store),Error),TEXT("Relationship rule fixture save failed"));FOKStreetStore Reloaded;Check(OKStreetLoadSaveFile(S.Manifest.SavePath,Reloaded,Error)==EOKStreetLoad::Ok&&OKStreetWriteSave(Reloaded)==OKStreetWriteSave(S.Store),TEXT("Relationship event save reload mismatch"));EndTalk();return S.Errors.Num()==BeforeErrors;
}

void AOKStreetSocialScene::WriteReport(bool Passed){
 if(!State||State->Reported)return;auto& S=*State;S.Reported=true;
 OKCaptureCrowdRuntimeSceneReport(this);
 uint64 FaceStarted=0,FaceIntermediate=0,FaceCompleted=0,FaceCancelled=0;for(int32 Index=0;Index<S.Manifest.Characters.Num();++Index){FOKRuntimeFaceStatus FaceStatus;if(OKGetCrowdRuntimeFaceStatus(this,Index,FaceStatus)){FaceStarted+=FaceStatus.Started;FaceIntermediate+=FaceStatus.Intermediate;FaceCompleted+=FaceStatus.Completed;FaceCancelled+=FaceStatus.Cancelled;}}
 auto J=MakeShared<FJsonObject>();{const auto* Camera=S.Camera.IsValid()?S.Camera->GetCameraComponent():nullptr;const auto* Profile=Camera?&Camera->PostProcessSettings:nullptr;J->SetBoolField(TEXT("camera_display_lut_applied"),Profile&&Camera->PostProcessBlendWeight==1&&Profile->bOverride_ColorGradingLUT&&Profile->ColorGradingLUT&&Profile->bOverride_ColorGradingIntensity&&Profile->ColorGradingIntensity==1);J->SetStringField(TEXT("character_shadow_profile"),TEXT("card_reader_studio"));}J->SetBoolField(TEXT("touch_reaction_only_test"),S.TouchReactionOnlyTest);J->SetNumberField(TEXT("touch_reaction_verified"),S.TouchReactionCase);J->SetArrayField(TEXT("touch_reaction_evidence"),S.TouchReactionEvidence);J->SetBoolField(TEXT("private_high_model"),true);J->SetBoolField(TEXT("dialogue_context_only_test"),S.ContextOnlyTest);J->SetNumberField(TEXT("dialogue_context_checks"),S.ContextChecks);J->SetNumberField(TEXT("source_confessions_verified"),S.ContextConfessions);J->SetNumberField(TEXT("source_confession_refusals_verified"),S.ContextRefusals);J->SetNumberField(TEXT("private_any_verified"),S.PrivateAnyCase);J->SetArrayField(TEXT("private_any_evidence"),S.PrivateAnyEvidence);J->SetNumberField(TEXT("relationship_voice_lines"),S.RelationshipVoices);J->SetArrayField(TEXT("dialogue_context_evidence"),S.ContextEvidence);J->SetBoolField(TEXT("presentation_only_test"),S.PresentationOnlyTest);J->SetNumberField(TEXT("presentation_phase"),S.PresentationPhase);J->SetNumberField(TEXT("relationship_controls_verified"),S.RelationControlsVerified);J->SetBoolField(TEXT("relationship_controls_sync"),S.RelationControlsSync);J->SetNumberField(TEXT("standing_actors_verified"),S.PresentationStandChecks);J->SetNumberField(TEXT("running_actors_verified"),S.PresentationRunActors.Num());J->SetBoolField(TEXT("movement_only_test"),S.MovementOnlyTest);J->SetBoolField(TEXT("movement_checked"),S.MovementChecked);J->SetNumberField(TEXT("floor_finishes_fixed"),S.FloorFinishesFixed);J->SetNumberField(TEXT("movement_sweep_checks"),S.MovementSweepChecks);J->SetNumberField(TEXT("female_turn_samples"),S.TurnSamples);J->SetNumberField(TEXT("female_max_turn_degrees_per_second"),S.MaxFemaleTurnRate);J->SetNumberField(TEXT("mouse_yaw_sensitivity"),OKStreetMouseYawSensitivity);J->SetNumberField(TEXT("mouse_pitch_sensitivity"),OKStreetMousePitchSensitivity);J->SetBoolField(TEXT("contacts_ui_checked"),S.ContactsUI);J->SetBoolField(TEXT("contacts_checked"),S.ContactsChecked);J->SetNumberField(TEXT("contacts_routes"),S.ContactsRoutes);J->SetNumberField(TEXT("contacts_private"),S.ContactsPrivate);J->SetNumberField(TEXT("contacts_voices"),S.ContactsVoices);J->SetArrayField(TEXT("contacts_voice_evidence"),S.ContactsVoiceEvidence);J->SetBoolField(TEXT("passed"),Passed);J->SetBoolField(TEXT("minimap_checked"),S.MinimapChecked);J->SetBoolField(TEXT("minimap_only_test"),S.MinimapOnlyTest);J->SetBoolField(TEXT("full_acceptance"),false);J->SetBoolField(TEXT("runtime_backdrop_ready"),S.RuntimeBackdropReady);J->SetNumberField(TEXT("player_prompts_shown"),S.PlayerPromptsShown);J->SetNumberField(TEXT("topic_pickup_count"),S.TopicPickups.Num());J->SetArrayField(TEXT("collected_topic_evidence"),S.CollectedEvidence);J->SetBoolField(TEXT("collected_topics_checked"),S.CollectedProbeChecked);J->SetNumberField(TEXT("collected_topic_commits"),S.CollectedCommits);J->SetBoolField(TEXT("mouse_mode_checked"),S.MouseModeChecked);J->SetNumberField(TEXT("runtime_facing_checks"),S.FacingChecks);J->SetBoolField(TEXT("active_speech_cancel_checked"),S.CancelChecked);J->SetBoolField(TEXT("face_invalid_request_checked"),S.FaceValidationChecked);J->SetBoolField(TEXT("relationship_events_checked"),S.RelationshipChecked);J->SetBoolField(TEXT("new_map"),GetWorld()->GetOutermost()->GetName().Contains(TEXT("StreetSocial")));J->SetNumberField(TEXT("characters"),S.Manifest.Characters.Num());J->SetNumberField(TEXT("female_count"),5);J->SetNumberField(TEXT("completed_dialogue_lines"),S.CompletedLines);J->SetNumberField(TEXT("dual_action_seconds"),S.DualActionSeconds);J->SetNumberField(TEXT("expressions_applied"),S.ExpressionsApplied);J->SetNumberField(TEXT("expressions_missing"),S.ExpressionsMissing);J->SetNumberField(TEXT("face_transition_started"),FaceStarted);J->SetNumberField(TEXT("face_transition_intermediate"),FaceIntermediate);J->SetNumberField(TEXT("face_transition_completed"),FaceCompleted);J->SetNumberField(TEXT("face_transition_cancelled"),FaceCancelled);J->SetNumberField(TEXT("branches_completed"),S.BranchesCompleted);J->SetNumberField(TEXT("refusals_completed"),S.RefusalsCompleted);J->SetNumberField(TEXT("greetings_completed"),S.Greetings);J->SetNumberField(TEXT("farewells_completed"),S.Farewells);J->SetNumberField(TEXT("mouth_peak"),S.PeakMouth);J->SetNumberField(TEXT("all_visible_seconds"),S.ReadyAt>0?S.ReadyAt-S.StartedAt:0);J->SetNumberField(TEXT("identity_retarget_applied"),S.IdentityRetargetApplied);J->SetNumberField(TEXT("identity_retarget_replayed"),S.IdentityRetargetReplayed);J->SetNumberField(TEXT("identity_retarget_rejected"),S.IdentityRetargetRejected);J->SetNumberField(TEXT("active_actor_interrupts"),S.ActiveActorInterrupts);J->SetNumberField(TEXT("active_actor_interrupt_commits"),S.ActiveActorInterruptCommits);J->SetNumberField(TEXT("positive_favor_commits"),S.PositiveFavorCommits);J->SetBoolField(TEXT("favor_save_verified"),S.FavorSaveVerified);J->SetBoolField(TEXT("candidate_selection_checked"),S.CandidateSelectionChecked);J->SetBoolField(TEXT("activity_state_checked"),S.ActivityStateChecked);J->SetBoolField(TEXT("dialogue_recovery_checked"),S.RecoveryChecked);J->SetBoolField(TEXT("relationship_auto_commit_checked"),S.VoicedPreviewChecked);J->SetNumberField(TEXT("relationship_auto_commits_verified"),S.VoicedPreviewConfirmations);J->SetBoolField(TEXT("voiced_relationship_preview_checked"),S.VoicedPreviewChecked);J->SetNumberField(TEXT("voiced_relationship_probe_confirmations"),S.VoicedPreviewConfirmations);J->SetNumberField(TEXT("expected_voice_admission_failures"),S.ExpectedVoiceAdmissionFailures);{TArray<TSharedPtr<FJsonValue>> Effects;for(const FOKStreetVoicedTopicRecord& Effect:S.VoicedEffects){TSharedRef<FJsonObject> Object=MakeShared<FJsonObject>();Object->SetNumberField(TEXT("slot"),Effect.Slot);Object->SetStringField(TEXT("id"),Effect.Id);Object->SetStringField(TEXT("node"),Effect.Node);Object->SetStringField(TEXT("category"),Effect.Category);Object->SetStringField(TEXT("transaction"),Effect.Transaction);Object->SetNumberField(TEXT("favor_before"),Effect.FavorBefore);Object->SetNumberField(TEXT("favor_after"),Effect.FavorAfter);Object->SetNumberField(TEXT("lewdness_before"),Effect.LewdBefore);Object->SetNumberField(TEXT("lewdness_after"),Effect.LewdAfter);Object->SetStringField(TEXT("audio_completion"),Effect.AudioCompletion);OKStreetWriteStringArray(Object,TEXT("voice_ids"),Effect.VoiceIds);Effects.Add(MakeShared<FJsonValueObject>(Object));}J->SetArrayField(TEXT("voiced_topic_effects"),Effects);}OKStreetWriteStringArray(J,TEXT("errors"),S.Errors);OKStreetWriteStringArray(J,TEXT("transactions"),S.Transactions);
 auto Metrics=[&](const TCHAR* Name,const TArray<double>& Values){TArray<double> Sorted=Values;Sorted.Sort();double Sum=0;for(double V:Values)Sum+=V;J->SetNumberField(FString(Name)+TEXT("_mean_ms"),Values.IsEmpty()?0:Sum/Values.Num());J->SetNumberField(FString(Name)+TEXT("_p95_ms"),Sorted.IsEmpty()?0:Sorted[FMath::Min(Sorted.Num()-1,FMath::CeilToInt(Sorted.Num()*.95)-1)]);};Metrics(TEXT("frame"),S.Frame);Metrics(TEXT("game_thread"),S.Game);Metrics(TEXT("render_thread"),S.Render);Metrics(TEXT("rhi_thread"),S.Rhi);Metrics(TEXT("gpu"),S.Gpu);J->SetNumberField(TEXT("frame_samples"),S.Frame.Num());
 if(GEngine&&GEngine->GameViewport&&GEngine->GameViewport->Viewport){const auto Size=GEngine->GameViewport->Viewport->GetSizeXY();J->SetNumberField(TEXT("viewport_width"),Size.X);J->SetNumberField(TEXT("viewport_height"),Size.Y);}J->SetBoolField(TEXT("offscreen"),FParse::Param(FCommandLine::Get(),TEXT("RenderOffscreen")));
 J->SetBoolField(TEXT("conversation_controls_checked"),S.ConversationControlsChecked);J->SetBoolField(TEXT("touch_input_routing_checked"),S.TouchInputRoutingChecked);J->SetBoolField(TEXT("touch_missing_subtitle_checked"),S.TouchMissingSubtitleChecked);J->SetBoolField(TEXT("touch_missing_recovery_checked"),S.TouchMissingRecoveryChecked);J->SetBoolField(TEXT("touch_checked"),S.TouchChecked);J->SetNumberField(TEXT("touch_commits"),S.TouchCommits);J->SetArrayField(TEXT("touch_evidence"),S.TouchEvidence);
 J->SetBoolField(TEXT("shared_splash_checked"),S.SplashChecked);J->SetBoolField(TEXT("splash_only_test"),S.SplashOnlyTest);J->SetBoolField(TEXT("splash_cancel_checked"),S.SplashCancelChecked);J->SetNumberField(TEXT("splash_cases_verified"),S.SplashCasesVerified);J->SetNumberField(TEXT("splash_effect_samples"),S.SplashEffectSamples);
 J->SetBoolField(TEXT("reading_only_test"),S.ReadingOnlyTest);J->SetBoolField(TEXT("reading_props_checked"),S.ReadingChecked);J->SetNumberField(TEXT("reading_actors_verified"),S.ReadingActorsVerified);
 J->SetBoolField(TEXT("watermelon_only_test"),S.MelonOnlyTest);J->SetBoolField(TEXT("shared_watermelon_checked"),S.MelonChecked);J->SetNumberField(TEXT("watermelon_cases_verified"),S.MelonCasesVerified);J->SetBoolField(TEXT("watermelon_cancel_checked"),S.MelonCancelChecked);J->SetNumberField(TEXT("watermelon_hits"),S.MelonHits);J->SetNumberField(TEXT("watermelon_samples"),S.MelonSamples);TArray<TSharedPtr<FJsonValue>> MelonDistances;for(float V:S.MelonDistances)MelonDistances.Add(MakeShared<FJsonValueNumber>(V));J->SetArrayField(TEXT("watermelon_min_distances_cm"),MelonDistances);
 J->SetBoolField(TEXT("meal_only_test"),S.MealOnlyTest);J->SetBoolField(TEXT("shared_meal_checked"),S.MealChecked);J->SetNumberField(TEXT("meal_actors_verified"),S.MealActorsVerified);J->SetNumberField(TEXT("meal_hand_samples"),S.MealSamples);J->SetNumberField(TEXT("meal_voices_completed"),S.MealVoices);J->SetBoolField(TEXT("meal_cancel_checked"),S.MealCancelChecked);
 J->SetBoolField(TEXT("drink_only_test"),S.DrinkOnlyTest);J->SetBoolField(TEXT("shared_drink_checked"),S.DrinkChecked);J->SetNumberField(TEXT("drink_actors_verified"),S.DrinkActorsVerified);J->SetNumberField(TEXT("drink_hand_samples"),S.DrinkSamples);J->SetNumberField(TEXT("drink_voices_completed"),S.DrinkVoices);J->SetBoolField(TEXT("drink_cancel_checked"),S.DrinkCancelChecked);
 J->SetBoolField(TEXT("seated_only_test"),S.SeatedOnlyTest);J->SetBoolField(TEXT("seated_talk_checked"),S.SeatedChecked);J->SetNumberField(TEXT("seated_actors_verified"),S.SeatedActorsVerified);J->SetNumberField(TEXT("seated_voices_completed"),S.SeatedVoices);J->SetBoolField(TEXT("seated_cancel_checked"),S.SeatedCancelChecked);
 J->SetBoolField(TEXT("photo_only_test"),S.PhotoOnlyTest);J->SetBoolField(TEXT("paired_photo_checked"),S.PhotoChecked);J->SetNumberField(TEXT("photo_actors_verified"),S.PhotoActorsVerified);J->SetNumberField(TEXT("photos_saved"),S.PhotosSaved);J->SetBoolField(TEXT("photo_cancel_checked"),S.PhotoCancelChecked);
 J->SetBoolField(TEXT("shared_walk_checked"),S.CompanionChecked);
 J->SetBoolField(TEXT("auto_phone_only_test"),S.AmbientOnlyTest);J->SetBoolField(TEXT("auto_phone_checked"),S.AmbientChecked&&!S.AmbientDrinkOnlyTest&&!S.AmbientSelfieOnlyTest&&!S.AmbientGroomOnlyTest);J->SetNumberField(TEXT("auto_phone_verified"),S.AmbientVerified);J->SetNumberField(TEXT("auto_phone_prepare_cancels"),S.AmbientPrepareCancels);J->SetNumberField(TEXT("auto_phone_interrupts"),S.AmbientInterrupts);J->SetNumberField(TEXT("auto_phone_samples"),S.AmbientSamples);
 J->SetBoolField(TEXT("auto_groom_only_test"),S.AmbientGroomOnlyTest);J->SetBoolField(TEXT("auto_groom_checked"),S.AmbientGroomOnlyTest&&S.AmbientChecked);J->SetNumberField(TEXT("auto_groom_verified"),S.AmbientVerified);J->SetNumberField(TEXT("auto_groom_prepare_cancels"),S.AmbientPrepareCancels);J->SetNumberField(TEXT("auto_groom_interrupts"),S.AmbientInterrupts);J->SetNumberField(TEXT("auto_groom_samples"),S.AmbientSamples);J->SetNumberField(TEXT("auto_groom_natural_completions"),S.AmbientNaturalCompletions);
 J->SetBoolField(TEXT("auto_selfie_only_test"),S.AmbientSelfieOnlyTest);J->SetBoolField(TEXT("auto_selfie_checked"),S.AmbientSelfieOnlyTest&&S.AmbientChecked);J->SetNumberField(TEXT("auto_selfie_verified"),S.AmbientVerified);J->SetNumberField(TEXT("auto_selfie_prepare_cancels"),S.AmbientPrepareCancels);J->SetNumberField(TEXT("auto_selfie_interrupts"),S.AmbientInterrupts);J->SetNumberField(TEXT("auto_selfie_samples"),S.AmbientSamples);
 J->SetBoolField(TEXT("auto_drink_only_test"),S.AmbientDrinkOnlyTest);J->SetBoolField(TEXT("auto_drink_checked"),S.AmbientDrinkOnlyTest&&S.AmbientChecked);J->SetNumberField(TEXT("auto_drink_verified"),S.AmbientVerified);J->SetNumberField(TEXT("auto_drink_prepare_cancels"),S.AmbientPrepareCancels);J->SetNumberField(TEXT("auto_drink_interrupts"),S.AmbientInterrupts);J->SetNumberField(TEXT("auto_drink_samples"),S.AmbientSamples);
 J->SetBoolField(TEXT("phone_use_only_test"),S.PhoneUseOnlyTest);J->SetBoolField(TEXT("phone_use_checked"),S.PhoneUseChecked);J->SetNumberField(TEXT("phone_use_actors_verified"),S.PhoneUseActorsVerified);J->SetBoolField(TEXT("phone_use_cancel_checked"),S.PhoneUseCancelChecked);J->SetNumberField(TEXT("phone_use_samples"),S.PhoneUseSamples);J->SetNumberField(TEXT("phone_use_voices"),S.PhoneUseVoices);
 J->SetNumberField(TEXT("invitation_ledger_checks"),S.InvitationLedgerChecks);J->SetBoolField(TEXT("invitation_only_test"),S.InvitationOnlyTest);J->SetNumberField(TEXT("invitation_voices"),S.InvitationVoices);J->SetNumberField(TEXT("invitation_accepts"),S.InvitationAccepts);J->SetNumberField(TEXT("invitation_refusals"),S.InvitationRefusals);J->SetNumberField(TEXT("invitation_cancels"),S.InvitationCancels);J->SetNumberField(TEXT("invitation_cases"),S.InvitationProbeCase);
 J->SetBoolField(TEXT("facility_only_test"),S.FacilityOnlyTest);J->SetNumberField(TEXT("facility_prepare_cancels"),S.FacilityPrepareCancels);J->SetNumberField(TEXT("facility_clock_checks"),S.FacilityClockChecks);J->SetBoolField(TEXT("facilities_checked"),S.FacilitiesChecked);J->SetNumberField(TEXT("facilities_verified"),S.FacilitiesVerified);
 FString Body;FJsonSerializer::Serialize(J,TJsonWriterFactory<>::Create(&Body));FString Path=S.Manifest.SocialReport;if(Path.IsEmpty())Path=FPaths::ProjectSavedDir()/TEXT("StreetSocial/startup.failure.json");IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path),true);FFileHelper::SaveStringToFile(Body,*Path,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
 UE_LOG(LogTemp,Display,TEXT("StreetSocial functional report: passed=%d path=%s"),Passed,*Path);
 if(S.SelfTest||S.RetargetSelfTest||!Passed)FPlatformMisc::RequestExitWithStatus(false,Passed?0:1);
}

void AOKStreetSocialScene::EndPlay(const EEndPlayReason::Type Reason){EndGalgame();OKControlStop();if(State){WaitFinQuery();if(State->FreeTalkLogDirty)FreeTalkSaveLog();SaveCalendar();OKStreetFlushSaves();if(!State->SaveRestarting&&State->Started)SaveToSlot(TEXT("auto"));if(State->Mind)OKStreetSetActiveMind(nullptr);if(State->Rules)OKStreetSetActiveRules(nullptr);}StopAmbientActivity();DestroyStreetPlaces();if(State&&State->FreeTalkLive)OKStreetFreeTalkAsrPost(TEXT("listen/stop"),TEXT("{}"),nullptr);if(State){FinishPrivate();}if(State){State->Pair.CapturePending=false;FinishPhoto();}EndTalk();if(State){for(auto& Row:State->ReadingProps)for(auto& Prop:Row.Value)if(Prop.IsValid())Prop->Destroy();State->ReadingProps.Reset();if(State->MouseInput.IsValid()&&FSlateApplication::IsInitialized())FSlateApplication::Get().UnregisterInputPreProcessor(State->MouseInput);for(auto& Prop:State->FacilityProps)if(Prop.IsValid())Prop->Destroy();for(auto& Pickup:State->TopicPickups)if(Pickup.IsValid())Pickup->Destroy();if(State->BackgroundGround.IsValid())State->BackgroundGround->Destroy();if(State->BackgroundFog.IsValid())State->BackgroundFog->Destroy();}if(State&&State->Overlay.IsValid()&&GEngine&&GEngine->GameViewport)GEngine->GameViewport->RemoveViewportWidgetContent(State->Overlay.ToSharedRef());State.Reset();Super::EndPlay(Reason);}

#if WITH_EDITOR
#include "Materials/Material.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionLinearInterpolate.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionWorldPosition.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Engine/StaticMesh.h"
#include "UObject/SavePackage.h"
#include "Misc/PackageName.h"
#include "AssetCompilingManager.h"
#include "GameFramework/WorldSettings.h"
#endif
UOKBuildStreetSocialMapCommandlet::UOKBuildStreetSocialMapCommandlet(){IsEditor=true;IsClient=true;IsServer=false;LogToConsole=true;}
int32 UOKBuildStreetSocialMapCommandlet::Main(const FString& Params){
#if WITH_EDITOR
 const FString Map=TEXT("/Game/Scenes/StreetSocial/StreetSocial");const FString MapFile=FPackageName::LongPackageNameToFilename(Map,FPackageName::GetMapPackageExtension());
 if(FParse::Param(*Params,TEXT("NaniteEnvironment"))){
  auto* ExistingWorld=LoadObject<UWorld>(nullptr,*(Map+TEXT(".StreetSocial")));if(!ExistingWorld)return 1;ExistingWorld->GetOutermost()->FullyLoad();
  const FString Backup=FPaths::ProjectSavedDir()/TEXT("StreetSocial/Build/StreetSocial.before-environment.umap");if(!IFileManager::Get().FileExists(*Backup)&&IFileManager::Get().Copy(*Backup,*MapFile)!=COPY_OK)return 1;
  TMap<UStaticMesh*,UStaticMesh*> Copies;
  for(const TCHAR* Shape:{TEXT("Cube"),TEXT("Sphere"),TEXT("Cylinder")}){
   const FString SourcePath=FString::Printf(TEXT("/Engine/BasicShapes/%s.%s"),Shape,Shape),Name=FString(TEXT("Street"))+Shape,Path=TEXT("/Game/Scenes/StreetSocial/Meshes/")+Name;
   auto* Source=LoadObject<UStaticMesh>(nullptr,*SourcePath);if(!Source)return 1;
   auto* Mesh=LoadObject<UStaticMesh>(nullptr,*(Path+TEXT(".")+Name));
   if(!Mesh){auto* Pkg=CreatePackage(*Path);Mesh=DuplicateObject<UStaticMesh>(Source,Pkg,*Name);Mesh->SetFlags(RF_Public|RF_Standalone);}
   Mesh->GetOutermost()->FullyLoad();Mesh->GetNaniteSettings().bEnabled=true;Mesh->NotifyNaniteSettingsChanged();FAssetCompilingManager::Get().FinishAllCompilation();
   Mesh->MarkPackageDirty();FSavePackageArgs Save;Save.TopLevelFlags=RF_Public|RF_Standalone;if(!UPackage::SavePackage(Mesh->GetOutermost(),Mesh,*FPackageName::LongPackageNameToFilename(Path,FPackageName::GetAssetPackageExtension()),Save))return 1;Copies.Add(Source,Mesh);
  }
  int32 Changed=0;for(AActor* Actor:ExistingWorld->PersistentLevel->Actors)if(auto* A=Cast<AStaticMeshActor>(Actor)){auto* C=A->GetStaticMeshComponent();if(auto* Replacement=Copies.Find(C->GetStaticMesh())){C->SetStaticMesh(*Replacement);++Changed;}}
  for(int32 I=0;I<12;++I){const FString Name=FString::Printf(TEXT("StreetMaterial%02d"),I),Path=TEXT("/Game/Scenes/StreetSocial/Materials/")+Name;auto* M=LoadObject<UMaterial>(nullptr,*(Path+TEXT(".")+Name));if(!M)return 1;M->GetOutermost()->FullyLoad();M->SetMaterialUsage(MATUSAGE_Nanite);M->PostEditChange();FAssetCompilingManager::Get().FinishAllCompilation();M->MarkPackageDirty();FSavePackageArgs Save;Save.TopLevelFlags=RF_Public|RF_Standalone;if(!UPackage::SavePackage(M->GetOutermost(),M,*FPackageName::LongPackageNameToFilename(Path,FPackageName::GetAssetPackageExtension()),Save))return 1;}
  ExistingWorld->MarkPackageDirty();FSavePackageArgs Save;Save.TopLevelFlags=RF_Public|RF_Standalone;if(!UPackage::SavePackage(ExistingWorld->GetOutermost(),ExistingWorld,*MapFile,Save))return 1;
  UE_LOG(LogTemp,Display,TEXT("StreetSocial common Nanite environment: %d exact-shape replacements; source Engine assets unchanged"),Changed);return 0;
 }
 if(FParse::Param(*Params,TEXT("Relight"))){
  auto* ExistingWorld=LoadObject<UWorld>(nullptr,*(Map+TEXT(".StreetSocial")));if(!ExistingWorld)return 1;ExistingWorld->GetOutermost()->FullyLoad();
  const FString Backup=FPaths::ProjectSavedDir()/TEXT("StreetSocial/Build/StreetSocial.before-relight.umap");if(!IFileManager::Get().FileExists(*Backup)&&IFileManager::Get().Copy(*Backup,*MapFile)!=COPY_OK)return 1;
  for(AActor* Actor:ExistingWorld->PersistentLevel->Actors)if(Actor){if(auto* Sun=Cast<ADirectionalLight>(Actor))Sun->GetLightComponent()->SetMobility(EComponentMobility::Movable);if(auto* Mesh=Cast<AStaticMeshActor>(Actor))if(Mesh->GetActorLocation().Z<10)Mesh->GetStaticMeshComponent()->SetCastShadow(false);}
  for(int32 I=0;I<12;++I){const FString Name=FString::Printf(TEXT("StreetMaterial%02d"),I),Path=TEXT("/Game/Scenes/StreetSocial/Materials/")+Name;auto* M=LoadObject<UMaterial>(nullptr,*(Path+TEXT(".")+Name));if(!M)return 1;M->GetOutermost()->FullyLoad();auto* Ambient=NewObject<UMaterialExpressionMultiply>(M);Ambient->A.Expression=M->GetEditorOnlyData()->BaseColor.Expression;Ambient->ConstB=I<2?.08f:.22f;M->GetExpressionCollection().AddExpression(Ambient);M->GetEditorOnlyData()->EmissiveColor.Expression=Ambient;M->PostEditChange();M->MarkPackageDirty();FAssetCompilingManager::Get().FinishAllCompilation();FSavePackageArgs Save;Save.TopLevelFlags=RF_Public|RF_Standalone;if(!UPackage::SavePackage(M->GetOutermost(),M,*FPackageName::LongPackageNameToFilename(Path,FPackageName::GetAssetPackageExtension()),Save))return 1;}
  ExistingWorld->MarkPackageDirty();FSavePackageArgs Save;Save.TopLevelFlags=RF_Public|RF_Standalone;const bool Good=UPackage::SavePackage(ExistingWorld->GetOutermost(),ExistingWorld,*MapFile,Save);UE_LOG(LogTemp,Display,TEXT("StreetSocial relight saved: movable sun, ambient material fill, ground receives shadows"));return Good?0:1;
 }
 if(IFileManager::Get().FileExists(*MapFile)){UE_LOG(LogTemp,Error,TEXT("New StreetSocial map already exists; refusing overwrite"));return 1;}
 TArray<FOKStreetProp> Props;OKStreetProps(Props);TArray<FString> Problems;OKStreetLayoutProblems(Problems);if(!Problems.IsEmpty()){for(const auto& P:Problems)UE_LOG(LogTemp,Error,TEXT("%s"),*P);return 1;}
 auto* Package=CreatePackage(*Map);auto* World=UWorld::CreateWorld(EWorldType::Editor,false,TEXT("StreetSocial"),Package);World->SetFlags(RF_Public|RF_Standalone);World->GetWorldSettings()->DefaultGameMode=AOKCrowdGameMode::StaticClass();
 const FLinearColor Colors[]={FLinearColor(.32f,.36f,.4f),FLinearColor(.66f,.58f,.46f),FLinearColor(.62f,.26f,.16f),FLinearColor(.83f,.73f,.54f),FLinearColor(.13f,.32f,.45f),FLinearColor(.09f,.28f,.38f),FLinearColor(.14f,.32f,.12f),FLinearColor(.2f,.11f,.05f),FLinearColor(.32f,.16f,.08f),FLinearColor(.85f,.72f,.38f),FLinearColor(.1f,.25f,.09f),FLinearColor(.4f,.41f,.43f)};
 TArray<UMaterial*> Materials;
 for(int32 I=0;I<UE_ARRAY_COUNT(Colors);++I){const FString Name=FString::Printf(TEXT("StreetMaterial%02d"),I),MaterialPath=TEXT("/Game/Scenes/StreetSocial/Materials/")+Name;if(IFileManager::Get().FileExists(*FPackageName::LongPackageNameToFilename(MaterialPath,FPackageName::GetAssetPackageExtension()))){auto* Existing=LoadObject<UMaterial>(nullptr,*(MaterialPath+TEXT(".")+Name));if(!Existing)return 1;Materials.Add(Existing);continue;}auto* MP=CreatePackage(*MaterialPath);auto* M=NewObject<UMaterial>(MP,*Name,RF_Public|RF_Standalone);auto* Color=NewObject<UMaterialExpressionConstant3Vector>(M);Color->Constant=Colors[I];M->GetExpressionCollection().AddExpression(Color);M->GetEditorOnlyData()->BaseColor.Expression=Color;
  if(I==0||I==1||I==8){auto* Position=NewObject<UMaterialExpressionWorldPosition>(M);M->GetExpressionCollection().AddExpression(Position);auto* Pattern=NewObject<UMaterialExpressionCustom>(M);Pattern->OutputType=CMOT_Float3;FCustomInput Tint;Tint.InputName=TEXT("Tint");Tint.Input.Expression=Color;Pattern->Inputs.Add(Tint);FCustomInput XY;XY.InputName=TEXT("P");XY.Input.Expression=Position;Pattern->Inputs.Add(XY);Pattern->Code=I==8?TEXT("float grain=0.92+0.08*sin(P.x*0.12+sin(P.y*0.014)*2); return Tint*grain;"):TEXT("float2 t=frac(P.xy/80.0); float joint=step(0.025,t.x)*step(0.025,t.y); float alternate=fmod(abs(floor(P.x/80.0)+floor(P.y/80.0)),2.0); return lerp(Tint*0.65,Tint*(0.94+0.06*alternate),joint);");M->GetExpressionCollection().AddExpression(Pattern);M->GetEditorOnlyData()->BaseColor.Expression=Pattern;}
  auto* Rough=NewObject<UMaterialExpressionConstant>(M);Rough->R=.8f;M->GetExpressionCollection().AddExpression(Rough);M->GetEditorOnlyData()->Roughness.Expression=Rough;auto* Ambient=NewObject<UMaterialExpressionMultiply>(M);Ambient->A.Expression=M->GetEditorOnlyData()->BaseColor.Expression;Ambient->ConstB=I<2?.08f:.22f;M->GetExpressionCollection().AddExpression(Ambient);M->GetEditorOnlyData()->EmissiveColor.Expression=Ambient;
  M->PostEditChange();M->MarkPackageDirty();FAssetCompilingManager::Get().FinishAllCompilation();FSavePackageArgs Save;Save.TopLevelFlags=RF_Public|RF_Standalone;if(!UPackage::SavePackage(MP,M,*FPackageName::LongPackageNameToFilename(MaterialPath,FPackageName::GetAssetPackageExtension()),Save))return 1;Materials.Add(M);
 }
 const TCHAR* Meshes[]={TEXT("/Engine/BasicShapes/Cube.Cube"),TEXT("/Engine/BasicShapes/Sphere.Sphere"),TEXT("/Engine/BasicShapes/Cylinder.Cylinder")};
 for(const auto& Prop:Props){if(Prop.Kind==EOKStreetPropKind::Glow)continue;auto* A=World->SpawnActor<AStaticMeshActor>(Prop.Center,FRotator(0,Prop.Yaw,0));A->SetActorLabel(TEXT("StreetSocial authored prop"));auto* C=A->GetStaticMeshComponent();C->SetMobility(EComponentMobility::Static);C->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,Meshes[int32(Prop.Mesh)]));C->SetMaterial(0,Materials[int32(Prop.Material)]);A->SetActorScale3D(Prop.Size/100.f);if(Prop.Center.Z<10)C->SetCastShadow(false);C->SetCollisionEnabled(Prop.Kind==EOKStreetPropKind::Decor||Prop.Kind==EOKStreetPropKind::Canopy?ECollisionEnabled::NoCollision:ECollisionEnabled::QueryAndPhysics);}
 auto* Sun=World->SpawnActor<ADirectionalLight>(FVector(0,0,800),FRotator(-45,-30,0));Sun->GetLightComponent()->SetMobility(EComponentMobility::Movable);Sun->FindComponentByClass<UDirectionalLightComponent>()->SetIntensity(2.5f);Sun->FindComponentByClass<UDirectionalLightComponent>()->bAtmosphereSunLight=true;
 auto* Sky=World->SpawnActor<ASkyLight>();Sky->GetLightComponent()->SetIntensity(.6f);Sky->GetLightComponent()->SetMobility(EComponentMobility::Movable);Sky->GetLightComponent()->bRealTimeCapture=true;
 auto* Atmosphere=World->SpawnActor<AActor>();auto* AtmosphereComponent=NewObject<USkyAtmosphereComponent>(Atmosphere,TEXT("StreetSkyAtmosphere"));Atmosphere->AddInstanceComponent(AtmosphereComponent);Atmosphere->SetRootComponent(AtmosphereComponent);AtmosphereComponent->RegisterComponent();World->SpawnActor<AOKStreetSocialScene>();FAssetCompilingManager::Get().FinishAllCompilation();World->MarkPackageDirty();FSavePackageArgs Save;Save.TopLevelFlags=RF_Public|RF_Standalone;
 const bool Saved=UPackage::SavePackage(Package,World,*MapFile,Save);UE_LOG(LogTemp,Display,TEXT("Independent StreetSocial map saved: %d authored props, %s"),Props.Num(),*MapFile);return Saved?0:1;
#else
 return 1;
#endif
}
