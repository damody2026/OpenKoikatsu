#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Templates/Function.h"
#include "Commandlets/Commandlet.h"
#include "OKStreetSocialScene.generated.h"

struct FOKStreetSceneState;

// Independent environment, with player-selected APNGs prepared at runtime.
UCLASS()
class OPENKOIKATSU_API AOKStreetSocialScene : public AActor {
 GENERATED_BODY()
public:
 AOKStreetSocialScene();
 virtual ~AOKStreetSocialScene();
 virtual void BeginPlay() override;
 virtual void Tick(float DeltaSeconds) override;
 virtual void EndPlay(const EEndPlayReason::Type Reason) override;
 // Plan §16 P12: the whole simulated state as JSON (ok.Street.Inspect).
 FString InspectJson(const FString& Only);
 // Plan §15 apps: a button press that only produced a message (refused, not enough money...) must still show it.
 FString FinMessageNow() const;
 void FinClickDone(const FString& MessageBefore);
private:
 TSharedPtr<FOKStreetSceneState> State;
 double SceneTime() const;
 void RefreshUI(); // marks the panel dirty; it is rebuilt once at the start of the next tick, however many handlers asked
 void RebuildUI();
 void ExecuteFreeTalkAction(int32 Character,const FString& Id,const FString& Action);
 bool PartyCanJoin(int32 Girl,FString* Why=nullptr) const;
 bool PartyJoin(int32 Girl);
 void PartyLeave(int32 Girl,const FString& Why);
 void PartyFarewell(int32 Girl);
 void PartyHandOver(int32 From);
 void PartyStartLeaving(int32 Girl,bool AskOthers);
 void PartyRetain(int32 Girl);
 // The heroines' feelings for each other (OKStreetSocialBonds.inl).
 FString OrientationOf(int32 G) const;
 void SetOrientation(int32 G,const FString& Orientation);
 float BondOf(int32 A,int32 B) const;
 FString BondStageOf(int32 A,int32 B) const;
 void BondEventIn(struct FOKStreetStore& Store,int32 A,int32 B,const FString& Event);
 void BondEvent(int32 A,int32 B,const FString& Event);
 TSharedRef<class FJsonObject> BondFacts(const struct FOKStreetStore& Store,int32 G) const;
 bool BondApply(struct FOKStreetStore& Store,const class FJsonObject& Outcome);
 void TickBonds();
 int32 BondMood(int32 Girl,const TArray<int32>& Others,int32* Who) const;
 FString BondText(const TArray<int32>& Members) const;
 FString BondPanelText(int32 G) const;
 // The card game 吹牛 with the group, indoors (OKStreetSocialBluff.inl).
 TArray<int32> BluffGroup() const;
 bool BluffCanStart(FString* Why) const;
 void StartBluff();
 FString BluffSeatName(int32 Seat) const;
 void BluffPlay(int32 Seat,const TArray<int32>& Cards,int32 Claim=-1);
 void BluffPass();
 void BluffChallenge(int32 Seat);
 void BluffNextTurn();
 void BluffToggle(int32 Card);
 void BluffPlayerPlay();
 void BluffHeroinePlay(int32 Seat);
 void TickBluff();
 void EndBluff(const FString& Why,int32 Winner);
 void BluffUI(const TSharedRef<class SVerticalBox>& Box);
 void BluffSay(const TArray<int32>& Seats,const TArray<FString>& Lines);
 bool BluffSpeaking() const;
 bool BluffHear(const FString& Text);
 TSharedPtr<class FJsonObject> BluffStep(const TSharedRef<class FJsonObject>& Action); // one action through bluff.rs bluff_step
 TSharedRef<class SWidget> BluffHandWidget(); // his hand in front of him, bottom of the screen
 void TickParty(float Dt);
 void SendPartyTalk(const FString& Text);
 void FinishPartyTalk(const FString& Text,const TArray<int32>& Members,const TSharedPtr<class FJsonObject>& Reply);
 void SpeakPartyLines(const TArray<int32>& Speakers,const TArray<FString>& Lines);
 void JudgeFreeTalk(int32 Character,const FString& Id,const FString& Said,const FString& Her,const FString& HerAction,const TArray<FString>& HerOrders);
 void SetCharacterRelationship(int32 Character,const FString& Stage);
 void SetCharacterStat(int32 Character,int32 Field,float Value);
 void RunPresentationProbe(double Now);
 bool ContactReady(int32 Character) const;
 bool ContactIdle() const;
 bool ContactLover(int32 Character) const;
 TArray<FVector> ContactRoute(const FVector& From,const FVector& To) const;
 void SelectContact(int32 Character);
 void NavigateGirl(int32 Character);
 void NavigatePoint(const FVector& Goal,const FString& Name);
 void StopNavigation(const FString& Notice);
 float StepNavigation(float Dt);
 void ToggleBigMap();
 void BeginPrivate(int32 Character);
 void FinishPrivate();
 bool TickPrivate(double Now);
 void TickContacts(float Dt,double Now);
 void RunContactsProbe(double Now);
 void ApplyMouseMode(bool Focused);
 void PickTopic(int32 Index);
 void AskHerTopic();
 void UseCollectedTopic(int32 No);
 void AdvanceCollectedTopic();
 void CompleteCollectedTopic();
 void RunCollectedTopicProbe();
 int32 TouchRay(const FVector& Origin,const FVector& Ray);
 void RunTouchReactionProbe(double Now);
 void Touch(int32 Region);
 void CompleteTouch();
 bool BeginTalk(int32 Character);
 void Topic(const FString& Category);
 void Farewell();
 void SendFreeTalk();
 // Every model call goes through the heroine mind (ok-mind, Rust): a chat job, and what to do with its answer.
 uint64 SubmitMindChat(const FString& Tag,const FString& Priority,const TArray<TSharedPtr<class FJsonValue>>& Messages,int32 MaxTokens,float Temperature,TFunction<void(bool,const FString&,const FString&)> Done);
 void FinishFreeTalk(int32 Character,const FString& Id,const FString& Text,const struct FOKFreeTalkReply* Reply,const FString& Failure);
 bool SubmitFreeTalkToMind(int32 Character,const FString& Id,const FString& System,const FString& UserMessage,const FString& Text);
 void HandleMindTalk(const TSharedPtr<class FJsonObject>& Result);
 void MindObserve(int32 Character,const FString& Kind,const FString& Summary,const FString& Place,float Importance=-1.f,const TCHAR* Type=TEXT("observation"));
 void MindEndOfDay();
 void HandleReflect(const TSharedPtr<class FJsonObject>& Result);
 FString MindRecallText(const FString& Id,const FString& PlayerText);
 void MindRequestProfiles();
 void MindSummarize(int32 Character);
 TSharedPtr<class FJsonObject> MindBiasAnswer(int32 Character,bool bStatus) const;
 void FillMindStatus(int32 Character,struct FOKFreeTalkStatus& Status);
 void ApplyMindRelation(int32 Character,const TSharedPtr<class FJsonObject>& RelDelta,const FString& Emotion);
 FString OpenChoice(int32 Heroine,const FString& Kind,const FString& Label,const TArray<FString>& Options,const FString& HerLean,float HerInsist,double Seconds,TFunction<void(const FString&)> OnResolve);
 bool ResolveChoice(const FString& Id,const FString& Option,float PlayerInsist);
 void TickChoices();
 FString MindPanelText(int32 Character) const;
 void ApplyMindExpression(int32 Character,const FString& Emotion,const struct FOKFreeTalkReply* Reply=nullptr);
 void LoadCatalog();
 FDateTime StreetNow() const;
 FString ShopPlaceHere() const;
 void RecordPayAgreed(int32 Girl,const FString& How); // OKStreetSocialEconomy.inl
 FString PayAgreed(int32 Girl) const; // OKStreetSocialEconomy.inl
 FString OutingShop(int32 Girl) const; // OKStreetSocialEconomy.inl
 FString OrderShopHere(int32 Girl) const; // OKStreetSocialEconomy.inl
 bool MindProfilesReady() const; // OKStreetSocialSchedule.inl
 int32 ShiftUntil(int32 Character) const; // OKStreetSocialInvite.inl
 bool SeesHim(int32 I) const; // OKStreetSocialEncounter.inl
 void MovePromiseWith(int32 AppointmentId); // OKStreetSocialAppointments.inl
 void ToldAboutRival(int32 Character,const FString& Name); // OKStreetSocialMind.inl
 bool PlaceClosedAt(const FString& PlaceId,const FDateTime& When,FString& Why) const; // OKStreetSocialEconomy.inl
 int32 FloorHere(const FString& Place) const; // OKStreetSocialEconomy.inl
 TArray<const struct FOKStreetCatalogItem*> ItemsHere(const FString& Place) const;
 FString ResolveItemId(const FString& Id,const FString& Place) const; // OKStreetSocialEconomy.inl
 bool SoldHere(const struct FOKStreetCatalogItem& Item,const FString& Place) const;
 bool PlaceClosedNow(const FString& PlaceId,FString& Why) const;
 bool CommitStore(struct FOKStreetStore&& Candidate);
 TSharedPtr<class FJsonObject> ProfileOf(int32 Character) const;
 void AddToCart(const FString& ItemId,bool ForHer);
 int32 CartTotal() const;
 void StartCheckout(int32 ForHer=INDEX_NONE); // ForHer: the girl whose order it is (on arrival she is not his talk partner yet)
 void FinishCheckout(const FString& Option,int32 Her,const FString& AsGirl=FString()); // AsGirl: settle that girl's dishes as hers (a group table)
 void SettleGroupOrder(int32 Partner);
 bool HasDishLike(int32 Who,const struct FOKStreetCatalogItem& Item) const;
 // Languages and regions (OKStreetSocialLocale.inl).
 FString T(const TCHAR* Key,const FStringFormatNamedArguments& Args=FStringFormatNamedArguments()) const;
 FString Money(int64 Amount) const;
 FString HomeRegion(const FString& Lang) const; // OKStreetSocialLocale.inl
 void LoadUiTexts();
 void NameCollectedTopics(); // OKStreetSocialLocale.inl
 void SetLanguage(const FString& Lang,bool FollowRegion=true);
 // The characters' look (player.json "render_style": koikatsu | hg; ok.RenderStyle, Plugins/OKHGShading). Materials
 // are made when a character loads, so a change saves "auto" and starts the map again on it.
 FString RenderStyle() const;
 void SetRenderStyle(const FString& Style);
 bool ApplyRenderStyleSetting(const FString& Style); // the cvars only (startup, from player.json)
 FString RenderStyleLabel(const FString& Style) const;
 // Regions (OKStreetSocialLocale.inl; the rules decide every amount, ok-rules region.rs): an amount of the scene's own
 // in the save's region (kind: cash | wage | salary | rent | property | goods | price, or a [fixed] key), the save moved
 // to another region (all its money converted by the rules), and finance's yuan wallet amounts crossing to the region.
 int64 Local(const TCHAR* Kind,int64 Base) const;
 int64 MoneyIn(const FString& Text) const; // an amount box (major unit) -> kept amount
 FString MoneyBox(int64 Amount) const;
 void ChangeRegion(const FString& To);
 int32 FillSubtitleScripts(); // OKStreetSocialLocale.inl
 // Save slots and the start menu (OKStreetSocialSaves.inl).
 FString SaveSlotDir(const FString& Slot) const;
 FString SaveSlotName(const FString& Slot) const;
 FString SaveSlotSummary(const FString& Slot) const;
 void ApplyPendingSave();
 bool SaveToSlot(const FString& Slot);
 void RestartWithSave(const FString& Action,const FString& Slot);
 void OpenSaveMenu(const FString& Mode);
 void CloseSaveMenu();
 void TickSaves();
 TSharedRef<class SWidget> BuildSaveMenu();
 TSharedPtr<FJsonObject> FinWallet(const TCHAR* Dir,const FString& Op,const TSharedPtr<FJsonObject>& Value) const;
 FString CardTableOuting(const FString& Said); // the activity to walk to for cards agreed on outdoors (bluff.rs card_table_place) // the place he names in what he said (social.rs place_named), "" if none // Who (0: him) has one of these (a meal, a drink) on the table, unfinished, or in the cart
 void BuyForGift(const FString& ItemId);
 void StartGift(const FString& ItemId);
 void FinishGift(int32 Her,const FString& ItemId,const FString& Option,float Score);
 void StartLoan(int32 Amount);
 void InitWalletFromProfile(const FString& Id,const TSharedPtr<class FJsonObject>& Profile);
 void BuildShopPanel(const TSharedRef<class SVerticalBox>& Box);
 FString PlaceHere(const FVector& Where) const;
 FString SceneLine(int32 Character) const;
 void TickNeeds();
 void FeedHer(int32 Character,const TMap<FString,float>& Effects);
 float MoneyStressOf(const struct FOKStreetRelation& R,const TSharedPtr<class FJsonObject>& Profile,bool& Secret) const;
 bool DoAct(int32 Character,const FString& Act);
 float PlayerLook() const;
 float AppealOf(int32 Character) const;
 FString RollInviteStance(int32 Character);
 bool InviteAllowed(int32 Character,const FString& Action,FString& Why);
 void SetFollowMode(int32 Mode);
 float StepOutingLed(float Dt);
 void TickApproach();
 void StepApproach();
 void AddSecondSleeper(int32 First,const struct FOKStreetPlace* Place,const FString& FirstBed);
 void RequestIntimacy(int32 Character,bool bHerIdea);
 void TickIntimacy();
 int32 AddAppointment(const FString& Kind,int32 Heroine,const FString& Place,int64 Start,int32 Grace,const FString& Label);
 int32 CharacterOf(const FString& Id) const;
 int32 PatienceOf(int32 Character) const;
 void AddPromiseFromChat(int32 Character,const FString& Day,const FString& Time,const FString& Place,const FString& What,const FString& Words,const FString& His,bool bReplaces=false);
 void CancelPromiseWith(int32 Character,const FString& Scope=FString());
 void CancelAppointment(int32 AppointmentId); // one pending appointment, from the list (the cost: social.rs appointment_cancel)
 void BuildAppointmentRows(const TSharedRef<class SVerticalBox>& Panel);
 bool Blacklisted(const FString& Place,int32* DaysLeft=nullptr) const;
 const struct FOKStreetAppointment* AppointmentBefore(int64 UntilMinute) const;
 void TickAppointments();
 FString TaskListText() const;
 struct FSlateColor TaskListColor() const;
 void NavigateToNextAppointment();
 bool BusyWithPlayer(int32 I) const;
 FVector SpotFor(int32 I,const FString& PlaceId,const FString& Kind,float& Yaw) const;
 void PlanAll();
 const struct FOKStreetPlanBlock* BlockNow(int32 I,int32 Minute,int32* Index=nullptr) const;
 void TickSchedules();
 void PlanPromisesToday(int32 Heroine); // OKStreetSocialAppointments.inl
 FString PlanText(int32 I) const;
 FString SpokenName(int32 G) const; // OKStreetSocialParty.inl
 bool AtPlace(const FString& Id) const; // OKStreetSocialParty.inl
 int32 ManagerAt(const FString& Place) const;
 void ApplyJob(const FString& JobId);
 void Hire(const FString& JobId,bool Accepted);
 void BookShift(const FString& JobId,int32 Shift);
 void TickJobs();
 void EndShift(bool Early);
 void BuildJobRows(const TSharedRef<class SVerticalBox>& Panel);
 void NoteShiftChat();
 TCHAR DramaModeOf(int32 I) const;
 void RefreshDramaCast() const;
 TSharedRef<class FJsonObject> RulesListener(int32 I) const;
 int32 FoilOf() const;
 void MakeRumor(int32 About,const FString& Kind,const FString& Text,float Heat,bool bSecret);
 void ApplyJealousyNews(int32 Listener,const FString& AboutId,float Confidence);
 FString HeardText(int32 Character) const;
 void Encounter(int32 A,int32 B,bool bOverheard);
 void MindNote(int32 Character,const FString& Text);
 void RequestOverheard(int32 A,int32 B);
 void HandleOverheard(const TSharedPtr<class FJsonObject>& Result,int32 A,int32 B);
 void TickEncounters();
 bool RainingAt(int32 Day,int32 Minute) const;
 FString HolidayOn(int32 Day) const;
 bool BirthdayOf(int32 I,int32 Day) const;
 FString TodayText(int32 Character) const;
 void TickInitiative();
 void TickDirector();
 void CheckBirthdays();
 // Plan §15 money outside the wallet: OKStreetSocialFinance.inl and OKStreetSocialFinanceApps.inl.
 TSharedPtr<class FJsonObject> Fin(const FString& Op,TSharedPtr<class FJsonObject> Args,FString& Error);
 bool SpendCash(int64 Amount,const FString& Why);
 void GainCash(int64 Amount,const FString& Why);
 void FinRelation(const FString& Id,float Admiration,float Mystique,float Trust);
 void TickFinance();
 void HandleFinanceEvent(const TSharedPtr<class FJsonObject>& E);
 void TickHeroineFinance();
 void HandleFinanceReply(int32 Character,const TSharedPtr<class FJsonObject>& Predict,const TSharedPtr<class FJsonObject>& Trade,const TSharedPtr<class FJsonObject>& Bet);
 FString ComputerHere(FString* Why=nullptr) const;
 void TickComputer();
 FString FinanceSceneText() const;
 void OpenApp(const FString& Name,bool ReadOnly);
 void CloseApp();
 void RefreshAppData(bool Full);
 void PopulateApp();
 TSharedRef<class SWidget> BuildAppOverlay();
 TSharedRef<class SWidget> BuildLotteryApp();
 TSharedRef<class SWidget> BuildScratchApp();
 TSharedRef<class SWidget> BuildWebApp();
 TSharedRef<class SWidget> BuildAtmApp();
 TSharedRef<class SWidget> BuildTerminalApp(bool Cn);
 void BuyLotto(int32 Quick);
 void PlaceBetSlip();
 void PickHerNumbers();
 void StartScratch(const FString& Theme);
 void SubmitOrder();
 void TransferMarket(bool Deposit);
 void BuildFinanceRows(const TSharedRef<class SVerticalBox>& Panel);
 // Plan §15 P10 / P11: OKStreetSocialVenture.inl.
 int32 StartEntrust(int32 Heroine,const FString& Kind,int64 Amount,int32 Days);
 void SettleEntrust(int32 EntrustId,int64 Total,const FString& Split);
 void TickVentures(struct FOKStreetStore& Candidate);
 bool FoundCompany(const FString& Name,const FString& Office);
 void OfferJob(int32 Heroine,const FString& Role,int64 Pay,bool TitlePublic,bool Exclusive);
 void FireStaff(const FString& Id);
 TSharedRef<class SWidget> BuildVentureApp();
 bool ProbePress(const FString& Label,FString& Why,int32 Nth=0);
 void StartControlServer();
 void TickControl();
 bool ProbeAction(const FString& Label,FString& Why);
 bool AtLotteryCounter() const;
 float NpcIdleTick(struct FOKStreetAgentSim& Agent,float Walk,double Now);
 void TickCommutes(float Dt);
 void TickCounterProximity();
 void FarewellCompanion();
 // Bank, net worth, alerts, heroines' own accounts: OKStreetSocialBank.inl.
 int64 NetWorthOf(const struct FOKStreetStore& C,TArray<TPair<FString,int64>>* Parts=nullptr);
 void BankDaily(struct FOKStreetStore& C);
 TSharedPtr<class FJsonObject> BankRequest(const struct FOKStreetStore& C,const FString& What,int64 Amount,int32 Arg) const;
 bool PayByCard(struct FOKStreetStore& C,int64 Amount,const FString& Place,const FString& Why,const TCHAR* Category=TEXT("shopping"));
 void BankAction(const FString& What,int64 Amount,int32 Arg);
 bool PhoneCutOff(FString* Why=nullptr) const;
 bool DormLocked(FString* Why=nullptr) const;
 void TickHeroineInvest();
 void AdviseHeroine(int32 Heroine,const FString& Code,const FString& Side);
 void HandleAdviceResult(const TSharedPtr<class FJsonObject>& E);
 void BuildHerInvestRows(const TSharedRef<class SVerticalBox>& Box);
 TSharedRef<class SWidget> BuildBankTab();
 void AddAlert(const FString& Kind);
 void CheckBankruptcy();
 void GoBankrupt();
 TSharedRef<class FJsonObject> RulesLovers() const;
 void FatherRescue();
 TSharedRef<class SWidget> BuildBankruptApp();
 // Campus: OKStreetSocialCampus.inl.
 struct FOKStreetAcademic* AcademicOf(struct FOKStreetStore& Store,int32 Character);
 void TickAcademics();
 bool StudyPlaceHere(FString* Why=nullptr) const;
 int32 StudyPartner() const;
 void ToggleStudy();
 bool StairHere(int32& PlaceIndex,int32& Floor) const;
 void TakeStairs(int32 Dir);
 void TickCampusProximity();
 FString OfficeHere() const;
 FString AcademicText(int32 Character) const;
 void BuildCampusRows(const TSharedRef<class SVerticalBox>& Panel);
 TSharedRef<class SWidget> BuildCampusApp(bool bOffice);
 void CampusAction(const FString& Action,const FString& Dept);
 void TickUiProbe();
 void EatFromBag(const FString& ItemId,bool Feed);
 FString MenuText(float Satiety=-1.f) const;
 bool TooFullFor(int32 Girl,const struct FOKStreetCatalogItem& Item) const; // OKStreetSocialMind.inl // with her satiety: what she could not finish now is marked
 void ApplyChoiceReply(int32 Character,const struct FOKFreeTalkReply& Reply);
 void SpeakFreeTalk(int32 Character,const TArray<FString>& Lines,int64 Serial);
 void RequestFreeTalkSegment(int32 Generation,int32 Index);
 void TickFreeTalkSpeech(double Now);
 void ApplyFreeTalkFavor(int32 Character,int32 Delta,const FString& Reason);
 void CancelFreeTalkVoice();
 FString FreeTalkFilePath(const TCHAR* Name) const;
 void FreeTalkEnsureLog();
 int64 FreeTalkAddLine(int32 Character,bool Player,const FString& Text);
 void FreeTalkSaveLog();
 void FreeTalkSaveMemory();
 int32 FreeTalkSlotOf(const FString& CharacterId) const;
 bool FreeTalkSpeakVoice(int32 Character,const FString& Path);
 void FreeTalkReplayToggle();
 void FreeTalkPlayLine(int64 Serial);
 void TickFreeTalkReplay(double Now);
 int32 FreeTalkPartner() const;
 void FreeTalkClose();
 void FreeTalkLiveSet(bool On);
 void FreeTalkLiveConnect();
 void FreeTalkSendNudge(bool Leaving);
 void FreeTalkSilentLeave();
 void FreeTalkLeave(const FString& Message);
 void TickFarewell();
 void TickFaceTalkPartner(bool Late=false);
 void TickRetryAction();
 void TickEnsureFreeTalk();
 void TickPlaceClosing();
 void BeginStroll(int32 Girl,FString& Error,bool bRun=false);
 void TickLiveTalkTest(double Now);
 void TickFreeTalkLive(double Now);
 FString FreeTalkLiveStatus() const;
 void FreeTalkMicToggle();
 void FreeTalkMicRefresh();
 void FreeTalkMicSelect(const FString& Name);
 void SpawnStreetPlaces();
 void TickPlaceTour(double Now);
 void RideElevator(int32 PlaceIndex,int32 Floor);
 // Walk-in elevators (OKStreetSocialElevator.inl).
 void SpawnElevators();
 void DestroyElevators();
 void TickElevators(float Dt);
 void PlaceElevatorParts(int32 Index);
 void SetLiftDoorBlock(int32 Index,int32 Floor,bool Closed);
 bool LiftDoorwayBusy(int32 Index) const;
 void PressElevator(int32 Index,int32 Floor);
 void ElevatorDoorButton(int32 Index,bool Open);
 void LiftWalk(int32 Girl,const TArray<FVector>& Path);
 bool InElevatorCar(int32 Index,const FVector& Point) const;
 int32 ElevatorWithMale() const;
 int32 ElevatorLanding(int32 Index,const FVector& Point) const;
 int32 ElevatorOfPlace(int32 PlaceIndex) const;
 float StepLiftRide(float Dt);
 int32 AdvanceLiftRide(struct FOKStreetLiftRide& Ride,float Dt,float& Rate); // OKStreetSocialElevator.inl
 bool PlanNavLift(const FVector& Goal);
 FVector NavApproachSpot(const FVector& Her) const; // OKStreetSocialContacts.inl
 FVector PlaceInsideSpot(const struct FOKStreetPlace& Place) const; // OKStreetSocialOuting.inl
 bool NavNeedsLift(const FVector& Goal) const;
 // Lover moments (OKStreetSocialLover.inl): DoAct's kiss / embrace / hand / head pat, shown.
 void PlayLoverAct(int32 Girl,const FString& Act,int32 Reaction);
 void StartLoverReaction(int32 Girl,const FString& Clip,float Seconds,const FString& MaleClip=FString(),const TCHAR* FemaleRecipe=nullptr);
 void TickLover();
 bool S_ValidGirl(int32 Girl) const;
 bool HandleWorldAct(int32 Girl,const FString& Kind,const FString& Arg);
 FString FreeTalkExtras() const;
 // Each heroine's own wake-up, departures and early arrivals (OKStreetSocialRoutine.inl), after PlanAll.
 int32 HerWage(const struct FOKStreetPlanBlock& W) const;
 bool MaskBroken(int32 Character,bool bPublic) const;
 void UnknownAct(int32 Character,const FString& Kind);
 FString InitiativeNudge(int32 I,bool bInvite);
 void StopLifePose(int32 Only=-1);
 void StopStrayPoses();
 void TickFreeTalkTrace(double Now);
 void TickOrderNotice();
 void TickServes();
 void TickMeal(float Dt);
 FString DishTableText() const;
 bool EatActBlocked(int32 Girl,const FString& Act);
 void PlaceChatOrder(int32 Her,const FString& HerSaid=FString()); // HerSaid: her line that ordered it (her own offer to pay)
 void TickShopArrival();
 bool InLifePose(int32 I) const;
 void SpawnLifeProps(struct FOKLifePose& Pose);
 void TickLife(float Dt);
 bool EntrustReminderDone(const struct FOKStreetAppointment& A) const;
 bool InitiativeFree(int32 I) const;
 void ReleaseFromSchedule(int32 I);
 bool SkipConfirmed();
 int32 HerShiftPay(int32 I,const struct FOKStreetPlanBlock& W,int32 Wage,int32 Minute);
 bool NothingGoingOn() const;
 bool NothingGoingOnExcept(int32 Girl) const;
 bool DoActHers(int32 Character,const FString& Act);
 void TickIntroFriend();
 bool SelfTextAllowed(int32 Girl);
 bool LiftWalkNear(int32 Index) const;
 void AbortSleep(const FString& Why);
 void PhoneSave();
 int32 RemoteKind(int32 Character) const;
 bool PanelConsent(int32 Girl,const FString& Action,bool bNotify=true);
 bool PrivateConsent(int32 Girl);
 // The heroine interaction module (OKStreetSocialHeroine.inl, OKStreetHeroine.h): every interaction goes through Interact.
 struct FOKHeroineResult Interact(int32 Girl,const FString& ActId,const struct FOKHeroineArgs& Args);
 void HeroineRefuseGate(int32 Girl,const FString& ActId,const class FJsonObject& Answer);
 void HeroineApplyRecord(int32 Girl,const class FJsonObject& Answer,const FString& Place);
 bool ApplyRulesRelation(const FString& Id,const class FJsonObject& Answer);
 void ApplyRulesEffects(struct FOKStreetStore& Candidate,const class FJsonObject& Answer);
 void FlushRulesEffects();
 void ApplyCompanyEffect(struct FOKStreetStore& C,const class FJsonObject& Effect);
 void HeroineNote(int32 Girl,const TCHAR* ActId,const FString& Text,const FString& Place,float Importance=-1.f,const FString& RumorDetail=FString());
 void HeroineCheckoutLean(int32 Girl,int32 Total,FString& Lean,float& Insist);
 void BuildHeroinePanel(const TSharedRef<class SVerticalBox>& Panel,int32 Her);
 float ChastityOf(int32 Girl) const;
 void InitStrangerCap(const FString& Id,const TSharedPtr<class FJsonObject>& Profile);
 void TickAutoFriendship();
 bool AloneConsent(int32 Girl,const TCHAR* What); // one-on-one / 更親密 with someone not his lover (chastity rule)
 void ActivityAftermath(const FString& ActivityId,int32 Girl,int64 StartMinute);
 void TickActivityWatch();
 void TickLifeBlocks();
 bool StartNpcLift(int32 I,const FVector& Spot);
 void TickNpcLifts();
 struct FOKStreetBias BiasOf(int32 Girl) const;
 bool InviteAllowedBiased(int32 Character,const FString& Action,FString& Why);
 void TickBiasActions();
 void BookTable(const FString& Place);
 FString SuspicionNote(int32 Girl) const;
 // OKStreetSocialPhoneInbox.inl
 bool PrepareFreeTalk(int32 Character,const FString& Text,struct FOKFreeTalkSettings& Settings,FString& System,FString& UserMessage,FString& Error);
 TArray<struct FOKStreetHistoryEntry>& HistoryOf(const FString& Id);
 FString HistoryPath(const FString& Id) const;
 void HistoryAdd(int32 Character,const FString& Kind,const FString& Who,const FString& Text,const FString& Place=FString());
 void MindRemember(int32 Character,const FString& Kind,const FString& Summary,const FString& Place,float Importance=-1.f,const TCHAR* Type=TEXT("observation"));
 FString HistoryPromptText(int32 Character);
 bool RemoteWith(int32 Character) const;
 FString RemoteDoing(int32 Character) const;
 int32 PhoneUnread(int32 Girl);
 void PhoneLoadSeen();
 void PhoneMarkSeen(int32 Girl);
 void PhoneOpen(int32 Girl);
 void PhoneSend();
 float PhoneAnswerChance(int32 Girl) const;
 void PhoneCall(int32 Girl);
 void PhoneHangUp();
 void ReachOutByPhone(int32 Girl);
 void PhoneAnswer(bool Accept);
 void QueueHerText(int32 Girl,const FString& Why,const FString& Kind,double DelaySeconds);
 void ComposeHerText(int32 Girl,const FString& Why,const FString& Kind,int32 Tries=0);
 void TickPhone();
 void TickStageTest();
 // OKStreetSocialCompany*.inl
 void RunFindLots();
 void TickCompanyWorld();
 struct FOKStreetUnitDef UnitOf(const FString& Unit) const;
 FString HoldingPlace(const struct FOKStreetHolding& H) const;
 FString CompanyHqPlace() const;
 void CompanyEnsure(struct FOKStreetStore& Store);
 // the companies through the rules (estate.rs): the world sent, the answer replayed
 TSharedRef<class FJsonObject> CompanyWorld(const struct FOKStreetStore& C,const TCHAR* Kind) const;
 void CompanyApply(struct FOKStreetStore& C,const class FJsonObject& Answer);
 bool CompanyAct(const FString& Action,TFunctionRef<void(class FJsonObject&)> Args);
 bool CompanyActOn(struct FOKStreetStore& C,const FString& Action,TFunctionRef<void(class FJsonObject&)> Args);
 TSharedPtr<class FJsonObject> CompanyView(const struct FOKStreetStore& C) const;
 bool CompanySettleMonth(struct FOKStreetStore& C);
 bool CompanyCommit(struct FOKStreetStore&& Candidate,const FString& Message);
 void CompanyRent(const FString& UnitId);
 void CompanyBuy(const FString& UnitId,bool bLoan);
 void CompanyBuild(const FString& UnitId,const FString& BuildingId);
 void CompanySetHq(const FString& UnitId);
 void CompanyLeave(const FString& UnitId);
 void CompanySell(const FString& UnitId);
 void CompanyPayOff(const FString& UnitId);
 void CompanyHire(const FString& ApplicantId,int64 Offer,const FString& UnitId);
 void CompanyFire(const FString& WorkerId);
 void CompanyRaise(const FString& WorkerId,int64 Pay);
 void CompanyAssign(const FString& WorkerId,const FString& UnitId);
 void CompanyDaily(struct FOKStreetStore& C);
 void TickCompany();
 FString CompanyPromptLine(int32 Girl) const;
 TSharedRef<class SWidget> BuildCompanyApp();
 void BuildCompanyRow(const TSharedRef<class SVerticalBox>& Panel);
 void CompanyShowAround(int32 Girl,const FString& UnitId);
 void TickCompanyTest();
 void TickHeroCompanyTest();
 struct FOKStreetHeroCompany* HeroCompanyOf(struct FOKStreetStore& C,const FString& OwnerId);
 FString UnitUser(const struct FOKStreetStore& C,const FString& UnitId) const;
 const struct FOKStreetHolding* LotHolding(const FString& UnitId,FString* User) const;
 FString HeroCompanyPlace(int32 Girl) const;
 int32 HeroOwnerAt(const FString& Place) const;
 void SyncHeroJobs();
 struct FOKCompanyWallet HeroWallet(struct FOKStreetStore& C,struct FOKStreetHeroCompany& H);
 void HeroOfferAnswer(const FString& OwnerId,bool Accept);
 void HeroJobQuit(const FString& OwnerId);
 void HeroJobPaid(struct FOKStreetStore& C,const FString& JobId,int64 Pay,int64 Minutes);
 void CompanyLeaseOut(const FString& UnitId,int64 Rent);
 FString WithholdPlayerWage(struct FOKStreetStore& C,int64 Gross,int64 Minutes);
 // his company's money to and from him (only under a name), and the books
 void CompanyInject(int64 Amount);
 void CompanyRepayOwner(int64 Amount);
 void CompanyBonus(int64 Amount);
 void CompanyDividend(int64 Amount);
 void CompanySetOwnerPay(int64 Pay);
 void CompanyAutoFund(bool bOn);
 int64 CompanyEquity(const struct FOKStreetStore& C) const;
 FString BookPath(const FString& Book) const;
 TArray<struct FOKBookEntry>& BookOf(const FString& Book);
 void BookAppend(const FString& Book,struct FOKBookEntry&& E);
 void TickBooks();
 FString BookTitle(const FString& Book) const;
 FString ExportBook(const FString& Book);
 TSharedRef<class SWidget> BuildAccountsApp();
 TMap<FString,FString> BooksSnapshot();
 void BooksRestore(const TMap<FString,FString>& Files);
 void TickAccountsTest();
 int32 BookGaps(int64 Since);
 void BuildPhoneRow(const TSharedRef<class SVerticalBox>& Panel);
 TSharedRef<class SWidget> BuildPhoneApp();
 const struct FOKStreetPlanBlock* LifeBlock(int32 I,int32 Minute) const;
 bool NextDay();
 bool StartSleep(const struct FOKStreetModelAnchor& Bed,const struct FOKStreetPlace* Place,int32 Girl,bool bNap=false);
 void TickSleep();
 void StartCalendarAndMind();
 void TickCalendarAndMind();
 void SaveCalendar();
 int64 SkipWakeMinute() const; // OKStreetSocialLife.inl
 bool SkipToNextMorning();
 void TickSolo();
 void EndSolo();
 void TickRestroom();
 void EndRestroom(bool Returned);
 void DestroyStreetPlaces();
 TArray<FVector> PlaceRoute(const FVector& From,const FVector& To) const;
 // One walking step for him: straight, else sliding along the obstacle; never trapped inside one. Returns From if stuck.
 FVector StreetStep(const FVector& From,const FVector& Direction,float Distance) const;
 // Grid A* over the walkable street at From's floor (lazy 40 cm cells, same clearance as walking). Empty if none.
 TArray<FVector> GridRoute(const FVector& From,const FVector& To) const;
 bool NavCellFree(const FIntPoint& Cell,float Z) const;
 TArray<const struct FOKStreetPlace*> NearbyPlaces(float Radius) const;
 bool BeginOuting(int32 Girl,const FString& ActivityId,FString& Error);
 // activities.rs: whether an activity may start now (weather, who, what it uses up, once a day); bConsume takes the items.
 bool ActivityCheck(int32 Girl,const FString& ActivityId,FString& Why,bool bConsume,bool bAtEnd=false);
 TSharedRef<FJsonObject> ActivityContext(int32 Girl,const TCHAR* Rule) const;
 FString ActivityBlockedLine(int32 Character) const; // OKStreetSocialOuting.inl: what cannot be done here now, for her words
 FString ThingsFacts(int32 Character) const; // OKStreetSocialMind.inl: her things and what is lent between them (items.rs)
 void ReturnBorrowed(int32 Her,const FString& ItemId); // OKStreetSocialMind.inl: he gives back what he borrowed from her
 void CancelOuting(const FString& Message);
 float StepOuting(float Dt);
 void ArriveOuting();
 bool StartOutingActivity(int32 Girl,const FString& ActivityId,FString& Error);
 void StartActivityFromPanel(int32 Girl,const FString& ActivityId,bool bAgreed=false);
 void ResumeTalkQuiet(int32 Girl);
 FString FreeTalkDoing() const;
 FString FreeTalkNearby() const;
 void ToggleLanguage();
 void ToggleVoiceSource();
 void LoadVoiceSettings(const TSharedPtr<class FJsonObject>& Manifest);
 void SaveVoiceSettings();
 void ProbeTtsServer(int32 Attempt=0); FTimerHandle TtsProbeTimer;
 void RequestTtsLine(const FString& Text,const FString& Voice,bool Male);
 void SetMaleTone(int32 Index);
 void LoadPlayerName();
 void SetPlayerName(const FString& Name);
 void ProbeVoiceInfo();
 // Her own voice (OKStreetSocialVoicePick.inl).
 void ProbeCardVoice();
 void RequestVoiceCandidates();
 void PlayVoiceCandidate(int32 Index,bool Reference);
 void FixVoiceCandidate(int32 Index,bool AsDefault);
 void RemoveCardVoice();
 void AddVoicePickUI(TSharedRef<class SVerticalBox> Box);
 FString MaleToneLabel() const;
 bool QueueNode(int32 Node,const FString& Category,const FString& BranchId=FString());
 void SetSourcePlayerLine(const FString& Text);
 void AdvanceRelationshipDialogue();
 void RunDialogueContextProbe(double Now);
 void RelationshipEvent(const FString& Event,bool bHerYes=false); // bHerYes: she said yes in her own words (the judge's accepts)
 void BreakUpSaid(int32 Character); // OKStreetSocialLover.inl: she ended it in her own words
 void ApplyRelationship();
 void EndTalk();
 void FailTalk(const FString& Reason);
 void RunRecoveryProbe();
 void RunVoicedPreviewProbe();
 void FinishLine();
 void RequestInvitation(int32 Character,int32 Command);
 void CompleteInvitation();
 void RunInvitationProbe(double Now);
 void WriteReport(bool Passed);
 bool RunRelationshipSelfTest();
 void RunCandidateProbe();
 void RunActivityProbe();
 void BeginPhoto(int32 Character);
 void BeginPair(int32 Character,int32 Kind);
 void ResumeConversationPose(int32 Character,FString& Error);
 void RunSeatedProbe(double Now);
 void RunDrinkProbe(double Now);
 void RunPhoneUseProbe(double Now);
 void StopAmbientActivity();
 void TickAmbientActivity(double Now);
 void RunAmbientActivityProbe(double Now);
 void RunMealProbe(double Now);
 void RunWatermelonProbe(double Now);
 void RunSplashProbe(double Now);
 void RunMinimapProbe(double Now);
 void TickReadingProps();
 void RunReadingProbe(double Now);
 void CapturePhoto();
 // OKStreetSocialPhotos.inl (photos.rs): a picture filed, one she takes, one sent on the phone, the album.
 void PhotoFiled(const FString& File,const FString& Taker,const TArray<FString>& People,const FString& Topic,int32 SendTo,const FString& Album=FString());
 void PhotoWalkStop(int32 Girl); // a stop on a photo walk: he takes one of her there
 void HerPhoto(int32 Girl,const FString& Topic,const FString& Subject,bool bSend,bool bDrawing=false);
 void SendPhoto(const FString& From,int32 Girl,const TSharedPtr<FJsonValue>& Which);
 void TickPhotos();
 TSharedRef<SWidget> BuildPhotoAlbum(int32 Girl);
 FString PhotoFacts(int32 Character) const;
 // OKStreetSocialPhotos.inl (reading.rs): his books read alone or together with her; what she is told of them.
 void ReadBooks(int32 Girl,int32 Minutes,const FString& OnlyBook);
 FString ReadingFacts(int32 Character) const;
 void HelpedHerWork(int32 Girl,const FString& Place,int32 Minutes); // OKStreetSocialPhotos.inl (work.rs help_work_end)
 // OKStreetSocialAppointments.inl (series.rs): a meeting that repeats every week, the next one booked, calling it off.
 void StartSeries(int32 Character,const FString& Place,const TSharedPtr<FJsonValue>& Repeat);
 void TickSeries();
 bool HasHerNumber(int32 Girl,bool SheReachesHim=false); // OKStreetSocialPhoneInbox.inl (contacts.rs): her number is in his phone
 void HerNumberGiven(int32 Girl,const TCHAR* What=TEXT("hers"));
 void HerErrand(int32 Character,const FString& Kind);
 void GiveMoneyNow(int32 Girl,int64 Amount); // OKStreetSocialLover.inl: her act's give_money, once the judge has read the exchange // OKStreetSocialPhotos.inl (errand.rs): her student loan, her fees
 void StartEffect(const FString& Kind,int32 Girl); // OKStreetSocialPhotos.inl: an activity's effect (the explosion)
 void TickEffect();
 void TickUmbrella(); // OKStreetSocialPhotos.inl: the umbrella on a rainy walk (weather.rs walk_style)
 void FinishPhoto(bool bKeepTalking=true); // false: the conversation ends with the activity (goodbye, Esc)
 void TickPhoto(double Now);
 void RunPhotoProbe(double Now);
 void BeginCompanion(int32 Character);
 void FinishCompanion();
 float StepCompanion(float Dt);
 void RunCompanionProbe(double Now);
 void BeginFacility(int32 Kind,int32 Character);
 void FinishFacility();
 void TickFacility(double Now);
 void RunFacilityProbe(double Now);
};

UCLASS()
class OPENKOIKATSU_API UOKBuildStreetSocialMapCommandlet : public UCommandlet {
 GENERATED_BODY()
public:
 UOKBuildStreetSocialMapCommandlet();
 virtual int32 Main(const FString& Params) override;
};
