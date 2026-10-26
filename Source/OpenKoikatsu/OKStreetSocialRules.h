#pragma once
#include "OKStreetSocialText.h"
#include "Async/Async.h"
#include "HAL/CriticalSection.h"

// Street-social rules are pure data. They do not load Koikatsu maps, cards, or audio.
// Catalogue nodes use integer personality, a source object, and a conditions object.
// source.stage 0/1/2 is the relation filter stranger/friend/lover, not a favor promotion.
// conditions.id -1 is always and 0 is never. Every other id stays disabled.
// Only source.roles ["generic"] can be chosen by the topic list. Branch roles are not auto-selected.
// Conditions 7-12 follow GameAssist.PreferenceValue axis checks only when that personality
// triple was loaded. They do not use the caller roll. Attribute and default RandomBranch
// weights use roll 0..99. Absent card attribute uses the source not-matched weights and
// is labelled street-neutral, not exact. Absent talkTemper does not pick an index.
// Friendship and confession are explicit events. Reaching a favor limit does not promote.
// Confession uses CommunicationCondition stage 1 / condition 0 (20,70) and the caller roll.
// ConfirmEvent1 case 0 offers it only for a friend at favor/stepUpDic[1] >= 0.7.
// Missing step-up rules disable the event. Hotel, prayer, and map 9 are not added.
// Success and failure share one transaction. Dry-run does not change the store.
// Topic cooldowns are street state, not a KKS day rule. Schema 1 saves still load without them.
// voice_sequence is playable only when every slot is matched. Holes are not shortened.
// Favor clamps use rules.json stepup limits 0:75, 1:100, 2:150. There is no 100-point cap.
// Lewdness 0..100 matches the documented GameAssist clamp. Talk cost 1 is not claimed as KKS.
// Absent hotel, prayer, player stats, map 9, and attribute custom offsets contribute 0.
// That neutral result is not the exact original ChangeParam total.
// Identity retarget is explicit. Old and new ids are apng- plus 64 lowercase
// sha256 hex digits. Nothing infers them from a name, path, roster index, or
// appearance. Schema 1 omits identity_retargets when the ledger is empty.
// An active male or talk-target runtime error, or a previously ready actor
// whose status disappears, ends the talk without a reward. Loading and the
// three-second reveal are not disappearance. Visible is that reveal, not culling.
// Eligible talk targets stay inside the checks below. They are ordered by
// horizontal camera alignment, then distance, then lower slot, then input
// index. Tab locks that slot until it is ineligible or the talk ends. E
// admits only the slot the hint is already showing.

#include "CoreMinimal.h"
#include "Misc/SecureHash.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "OKStreetSocialCampus.h"
#include "OKStreetSocialProperty.h"
#include "OKStreetSocialBank.h"
#include "OKStreetSocialBooks.h"
#include <limits>

// Schema 2 adds the 2010 calendar (plan §5); schema 1 saves still load and start on day 0.
inline constexpr int32 OKStreetSocialSchema = 2;
inline constexpr int32 OKStreetSocialOldestSchema = 1;
// The most the street holds: one male and up to ten heroines (the roster, Launch/heroines.json, may name fewer).
inline constexpr int32 OKStreetCastCount = 11;
inline constexpr int32 OKStreetFemaleCount = 10;
inline constexpr int32 OKStreetMaxRetargetRequests = 10;
inline constexpr int32 OKStreetMaxChoices = 8;
inline constexpr int32 OKStreetDefaultTalkBudget = 5;
// The daily talk limit is off in the game: talking never spends the budget and nothing refuses for lack of it. The rule tests switch it on
// to keep checking the budget arithmetic itself.
inline bool GOKStreetTalkLimit = false;
inline constexpr bool OKStreetDefaultTalkBudgetIsKks = false;
inline constexpr bool OKStreetFavorCapIsKksStepUp = false;
inline constexpr bool OKStreetTopicCostIsKks = false;
// Not applied. Commit uses the step-up limits loaded from rules.json.
inline constexpr float OKStreetFavorCap = 100.f;
// Street rule over the original step-up table: a friend's favor goes on to 200; above 100 she is very likely to say yes
// to a confession or a one-on-one (Docs/STREET_SOCIAL_HEROINE_INTERACTION_MODULE.md §貞操與好感).
inline constexpr int32 OKStreetFriendFavorCap = 200;
inline constexpr float OKStreetConfessAcceptFavor = 50.f, OKStreetConfessRefuseFavor = -50.f;
inline constexpr float OKStreetLewdnessCap = 100.f;
inline constexpr float OKStreetTalkRangeCm = 250.f;
inline constexpr float OKStreetBodyClearance = 70.f;
inline constexpr float OKStreetWalkSpeed = 110.f;
inline constexpr float OKStreetMaleWalkSpeed = 160.f;
inline constexpr float OKStreetMaleRunSpeed = 420.f;
inline constexpr int32 OKStreetRelationCount = 3;
// CommunicationCondition_00 stage 1 condition 0, "告白する", cells "20,70".
// Info.GetEventADV command 6 uses branch 0, whose introduction row conditions value is 0.
// benefits_00 adds 20 confession points only for the same accommodation. Street does not.
inline constexpr int32 OKStreetConfessionChanceMin = 20;
inline constexpr int32 OKStreetConfessionChanceMax = 70;
inline constexpr bool OKStreetConfessionChanceIsExactOriginal = false;
inline constexpr bool OKStreetTopicCooldownIsKks = false;
inline constexpr bool OKStreetNeutralCorrectionIsExactOriginal = false;

enum class EOKStreetSex : uint8 { Unknown, Female, Male };
enum class EOKStreetMove : uint8 { Walking, Waiting, Looking, Resting, Reserved, Talking, Recovering };
enum class EOKStreetPoint : uint8 { Walk, Rest, Look, Seat };
enum class EOKStreetPropKind : uint8 { Ground, Block, Decor, Trunk, Canopy, Lamp, Glow };
enum class EOKStreetMesh : uint8 { Cube, Sphere, Cylinder };
enum class EOKStreetMaterial : uint8 { Checker, Plaza, Terracotta, Cream, Stripe, Glass, Leaf, Bark, Wood, Lamp, Hedge, Stone };
enum class EOKStreetLoad : uint8 { Missing, Ok, Invalid };

struct FOKStreetEffect {
 float Favor = 0.f;
 float Lewdness = 0.f;
 FString RelationEvent;
 bool ResetFavor = false;
 TArray<FString> SetFlags;
 int32 TalkCost = 0;
 FString Memory;
 int32 TopicId = INDEX_NONE;
 int32 CooldownDays = 0;
};

struct FOKStreetChoice {
 FString Id, TextJp, TextZh, Next, Source;
 TArray<FString> Conditions;
 FOKStreetEffect Effect;
};

struct FOKStreetNode {
 FString Id, Personality, Category, TextJp, TextZh, Source,FacialExpression;
 TArray<FString> VoiceFiles, Conditions;
 float Priority = 0.f;
 bool Once = false;
 FOKStreetEffect Effect;
 TArray<FOKStreetChoice> Choices;
 int32 PersonalityId = INDEX_NONE;
 int32 StageIndex = INDEX_NONE;
 int32 ConditionId = INDEX_NONE;
 bool EffectsDefined = false;
 bool VoiceSequenceComplete = false;
 FString VoiceSequenceReason, GameSpeakFilter, StreetSupport;
 TArray<FString> Roles;
 int32 PartIndex = 0;
 int32 Command = INDEX_NONE;
 int32 TopicId = INDEX_NONE;
 int32 Row = INDEX_NONE;
 TArray<int32> Containers;
 TArray<FString> BranchIds;
 FString Group;
};

struct FOKStreetBranchMember {
 int32 Row = INDEX_NONE;
 int32 PartIndex = 0;
 int32 Node = INDEX_NONE;
 FString Role;
};

struct FOKStreetBranch {
 FString Id, Category, Group, Kind, Operator;
 int32 Personality = INDEX_NONE;
 int32 Stage = INDEX_NONE;
 int32 Command = INDEX_NONE;
 int32 Container = INDEX_NONE;
 int32 ConditionId = INDEX_NONE;
 int32 ControllingRow = INDEX_NONE;
 int32 IntroductionNode = INDEX_NONE;
 int32 MatchedAttributeType = INDEX_NONE;
 int32 Axis = INDEX_NONE;
 int32 Threshold = 0;
 int32 IndexWhenTrue = INDEX_NONE;
 int32 IndexWhenFalse = INDEX_NONE;
 int32 TemperSlot = INDEX_NONE;
 TArray<int32> WeightsMatched, WeightsUnmatched, Weights;
 TArray<FOKStreetBranchMember> Indexer;
};

struct FOKStreetCatalogue {
 TArray<FOKStreetNode> Nodes;
 TArray<FOKStreetBranch> Branches;
 TMap<FString, TArray<int32>> ByPersonality;
 TMap<FString, int32> ById;
 TArray<FString> Warnings;
 FString Error;
 int32 Skipped = 0;
};

struct FOKStreetCharacterMeta {
 FString Id, Name, Personality, Sex, Fingerprint;
 EOKStreetSex SexKind = EOKStreetSex::Unknown;
 int32 PersonalityId = INDEX_NONE;
 int32 SexIndex = INDEX_NONE;
 float VoiceRate = 0.f;
 TArray<int32> InterestCategories;
};

struct FOKStreetRetargetRequest {
 FString RequestId, OldId, NewId;
};

struct FOKStreetRetargetTrace {
 FString RequestId, OldId, NewId, Time;
};

struct FOKStreetRetargetResult {
 bool Applied = false;
 int32 Moved = 0;
 int32 Replayed = 0;
 FString Reason;
};

struct FOKStreetActorPresence {
 bool SeenReady = false;
 bool SeenVisible = false;
};

struct FOKStreetPresenceSample {
 bool Queried = false;
 bool Ready = false;
 bool Visible = false;
 bool HasError = false;
};

struct FOKStreetManifestSocial {
 TArray<FOKStreetCharacterMeta> Characters;
 TArray<FOKStreetRetargetRequest> IdentityRetargets;
 FString DialogueCatalogue, SavePath, SocialReport, TalkAction, VoiceRoot, Error;
 int32 TalkBudget = OKStreetDefaultTalkBudget;
 int32 CardCount = -1;
 bool TalkBudgetSpecified = false;
 bool ExternalControl = false;
};

struct FOKStreetRelation {
 FString Id;
 float Favor = 0.f;
 float Lewdness = 0.f;
 FString Stage = TEXT("stranger");
 int32 TalksRemaining = 0;
 int32 Day = 1;
 TArray<FString> Talked, Flags, Committed, Topics;
 float SexDesire=0.f,TopicPoints=0.f;
 TArray<int32> ListenRecent;
 TArray<FString> CooldownIds;
 TArray<int32> CooldownDays;
 // Plan §4.2: relationship axes, 0..100, written only by the rules (OKStreetSocialBias.h), never by the model directly.
 float Trust = 20.f, Admiration = 0.f, Attraction = 10.f, Dependency = 0.f, Mystique = 0.f; // no liking axis: favor is the feeling
 float Jealousy = 0.f, Greed = 0.f, LifestyleDesire = 0.f, Respect = 20.f, Resentment = 0.f;
 // Jealousy toward each other heroine (stable id -> 0..100), the per-rival half of plan §4.2.
 TMap<FString, float> JealousyToward;
 // What she feels for each other heroine (stable id -> favor, 10 when unset) and what they are to each other (stranger /
 // friend / lover), and her orientation ("straight" / "bisexual"; empty: her profile's). Rules only (OKStreetSocialBonds.inl).
 TMap<FString, float> Toward;
 TMap<FString, FString> TowardStage;
 FString Orientation;
 // Plan §4.3 mood: the last emotion, its intensity when set, and the game minute it was set (half-life 2 game hours).
 FString Emotion;
 float EmotionIntensity = 0.f;
 int64 EmotionMinute = 0;
 // Plan §4.7 / §8.3: her wallet (-1 until her profile is known), how many more times he treated her than she him,
 // and what she owes him.
 int64 Cash = -1, Bank = 0, OwesPlayer = 0, Debt = 0; // Debt: bills she could not cover (rules: day.rs charge_bill)
 int32 TreatBalance = 0;
 // Plan §4.3 needs (0..100) and the game minute they were last brought up to date.
 float Satiety = 80.f, Energy = 90.f, Fun = 60.f, Social = 60.f, Hygiene = 90.f;
 int64 NeedsMinute = -1;
 // Plan §4.2b: how rich she thinks he is (0..100), from what she has seen him spend; drifts back toward 20.
 float PerceivedWealth = 20.f;
 // Chastity 0..100 (-1: not set yet, her personality decides; OKHeroineDefaultChastity). The lower it is, the less
 // the relationship matters for being alone with him: his charm against her resistance decides.
 float Chastity = -1.f;
 // Her own stranger favor cap, 10..100: how hard she is to win over (0: not set yet, the step-up table's). Reaching it
 // makes them friends (AOKStreetSocialScene::TickAutoFriendship).
 int32 StrangerCap = 0;
};

// The axes by their save / prompt names, in a fixed order (plan §4.2).
inline TArray<TPair<FString, float*>, TFixedAllocator<10>> OKStreetAxisFields(FOKStreetRelation& R) {
 return {{TEXT("trust"), &R.Trust}, {TEXT("admiration"), &R.Admiration}, {TEXT("attraction"), &R.Attraction},
  {TEXT("dependency"), &R.Dependency}, {TEXT("mystique"), &R.Mystique}, {TEXT("jealousy"), &R.Jealousy}, {TEXT("greed"), &R.Greed},
  {TEXT("lifestyle_desire"), &R.LifestyleDesire}, {TEXT("respect"), &R.Respect}, {TEXT("resentment"), &R.Resentment}};
}

// Plan §13.3 appointments: a date, a work shift, a booking, a pickup, a due date or a promised task.
struct FOKStreetAppointment {
 int32 Id = 0;
 FString Kind;     // date | shift | reservation | pickup | due | task
 FString Heroine;  // stable id, empty for shops
 FString Place;    // place id
 FString Label;    // shown in the task list
 int64 Start = 0;  // game minutes since 2010-09-01 00:00
 int32 Grace = 20; // minutes late still counted as arriving
 int32 Minutes = 0; // how long they said they would be together (a date: "the whole Saturday"); 0 is the usual 90
 FString Status = TEXT("pending"); // pending | done | late | missed | cancelled
 FString Payer;    // a date: who they said pays (him | her | split), empty when nobody said
};

// Plan §4.6 public memory: a rumour, who knows it, how hot and how sure.
struct FOKStreetRumor {
 int32 Id = 0;
 FString Text, About, Kind; // About: the heroine it is about (stable id), empty when it is only about him
 float Heat = 50.f, Confidence = 1.f;
 TArray<FString> KnownBy;
 TMap<FString, float> Heard; // how sure each listener is (second-hand is less sure); missing: Confidence
 int32 Day = 0;
 float SureOf(const FString& Who) const { const float* C = Heard.Find(Who); return C ? *C : Confidence; }
};

// Plan §15 托付 (her money in his hands) and 幫她翻身 (his money invested for her): settled with the four-way split.
struct FOKStreetEntrust {
 int32 Id = 0;
 FString Heroine, Kind; // kind: entrust | help
 int64 Principal = 0, Returned = 0;
 int32 Day = 0, Due = 0;
 FString Status = TEXT("open"), Split; // open | settled ; split: all | cut20 | half | principal | loss
 bool Overdue = false, Jealous = false;
};
// Plan §15 創業線: one company, heroines hired into roles (their schedule follows the office).
struct FOKStreetEmployee {
 FString Heroine, Role; // ceo | secretary | manager
 int64 Pay = 0;
 bool TitlePublic = true, Exclusive = false;
 int32 Since = 0, Missed = 0;
};
struct FOKStreetCompany {
 FString Name, Office;
 int32 Founded = -1, PaidMonth = -1;
 int64 Capital = 0, LastProfit = 0;
 TArray<FOKStreetEmployee> Staff;
 FOKStreetEstate Estate; // property, hired people, mortgages, ledger (OKStreetSocialProperty.h)
};

struct FOKStreetStore {
 int32 Schema = OKStreetSocialSchema;
 TArray<FOKStreetHeroCompany> HeroCompanies; // heroines' own companies (OKStreetSocialProperty.h)
 bool TalkBudgetIsKks = false;
 bool FavorCapIsKksStepUp = false;
 int32 StepUpStranger = INDEX_NONE;
 int32 StepUpFriend = INDEX_NONE;
 int32 StepUpLover = INDEX_NONE;
 int32 StepUpBeyond = INDEX_NONE;
 bool PreferenceLoaded = false;
 TMap<int32, int32> PreferenceAggressive, PreferenceDiligence, PreferenceKindness;
 int32 ConfessionChanceMin = OKStreetConfessionChanceMin;
 int32 ConfessionChanceMax = OKStreetConfessionChanceMax;
 bool ConfessionChanceOverridden = false;
 TArray<FOKStreetRelation> Relations;
 TArray<FOKStreetRetargetTrace> Retargets;
 int32 TopicDay=1;
 TMap<int32,int32> TopicStock,TopicClaims;
 // Game days since 2010-09-01 and the street clock minute at the last save (-1: never saved, a new game).
 int32 CalendarDay = 0;
 int32 CalendarMinute = -1;
 // Where he was when the game was saved (a game continues there); none in a new or an old save.
 FString Region = TEXT("CN"); // TW, CN or US: the setting, the money, the prices (ok-rules locale.rs); saves made before regions are CN
 bool HasPlayerSpot = false;
 FVector PlayerSpot = FVector::ZeroVector;
 float PlayerYaw = 0.f, ViewYaw = 90.f; // his facing, and the camera's
 // Plan §15 player start: ¥5,000 cash, allowance ¥8,000 on the 5th. Inventory holds bought gifts (item ids).
 int64 PlayerCash = 1000, PlayerBank = 0; // RMB
 FOKStreetPersonal Personal;      // his payroll year (OKStreetSocialBooks.h)
 TArray<FOKBookPost> BookPending; // changes on their way to the journal (not saved; OKStreetSocialBooks.inl)
 TArray<FString> Inventory;
 // The rules' own part of the save (ok-rules: her things and loans, photos, readings...), a JSON object kept as text: only
 // the rules read it (OKRulesPutExt / OKRulesTakeExt).
 FString Ext;
 TArray<FOKStreetAppointment> Appointments;
 int32 NextAppointment = 1;
 // Plan §13.3.4: shops he stood up, place id -> first game day he is welcome again.
 TMap<FString, int32> Blacklist;
 // Plan §8.6: jobs he holds (catalogue job ids) and each manager's rating of him (0..100).
 TArray<FString> PlayerJobs;
 TMap<FString, int32> JobRating;
 // Plan §4.6 between heroines: bond heat by "idA|idB" (sorted), and the rumours going round.
 TMap<FString, float> BondHeat;
 TArray<FOKStreetRumor> Rumors;
 TArray<FOKStreetEntrust> Entrusts;
 int32 NextEntrust = 1;
 FOKStreetCompany Company;
 // Campus: his studies, and each heroine's (by stable id), Docs/STREET_SOCIAL_CAMPUS_PLAN.md.
 FOKStreetAcademic PlayerAcademic;
 TMap<FString, FOKStreetAcademic> Academics;
 int32 NextRumor = 1;
 // His bank: savings interest, time deposits, card, student loan, bills, net-worth history (OKStreetSocialBank.h).
 FOKStreetBanking Banking;
};

struct FOKStreetCommitResult {
 bool Applied = false;
 bool Duplicate = false;
 FString Reason;
};

struct FOKStreetListItem {
 int32 Node = INDEX_NONE;
 int32 Choice = INDEX_NONE;
 bool Eligible = false;
 FString Label, Reason;
 TArray<FString> VoiceFiles;
};

struct FOKStreetBranchQuery {
 FString Category;
 int32 Stage = INDEX_NONE;
 int32 Command = INDEX_NONE;
 int32 Container = INDEX_NONE;
 int32 AttributeType = INDEX_NONE;
 int32 TalkTemper = INDEX_NONE;
};

struct FOKStreetBranchDecision {
 bool Decided = false;
 bool Playable = false;
 bool Refusal = false;
 bool ExactOriginal = false;
 int32 Index = INDEX_NONE;
 int32 Node = INDEX_NONE;
 int32 Introduction = INDEX_NONE;
 int32 ConditionId = INDEX_NONE;
 int32 Roll = INDEX_NONE;
 FString NodeId, Role, Reason, Policy;
 FOKStreetEffect Effect;
};

struct FOKStreetNeutralDelta {
 int32 Favor = 0;
 int32 Lewdness = 0;
 bool ExactOriginal = false;
 FString Policy;
};

struct FOKStreetEventResult {
 bool Applied = false;
 bool Resolved = false;
 bool ExactOriginal = false;
 bool Playable = false;
 bool Refusal = false;
 int32 LineNode = INDEX_NONE;
 int32 ChancePercent = INDEX_NONE;
 int32 Roll = INDEX_NONE;
 FString Reason, Policy, Stage;
 float Favor = 0.f;
 FOKStreetEffect Effect;
};

struct FOKStreetProp {
 EOKStreetPropKind Kind = EOKStreetPropKind::Decor;
 EOKStreetMesh Mesh = EOKStreetMesh::Cube;
 EOKStreetMaterial Material = EOKStreetMaterial::Stone;
 FVector Center = FVector::ZeroVector;
 FVector Size = FVector(100.f);
 float Yaw = 0.f;
};

struct FOKStreetPointData {
 FVector Position = FVector::ZeroVector;
 EOKStreetPoint Kind = EOKStreetPoint::Walk;
};

struct FOKStreetAgentSim {
 int32 Slot = 0;
 int32 Route = 0;
 int32 Cursor = 0;
 float Dwell = 0.f;
 int32 DwellPoint = INDEX_NONE;
 float Stuck = 0.f;
 FVector Position = FVector::ZeroVector;
 EOKStreetMove Mode = EOKStreetMove::Walking;
 bool Held = false;
};

inline FString OKStreetResolveAgainst(const FString& BaseFile, const FString& Path) {
 FString Resolved=(Path.IsEmpty() || !FPaths::IsRelative(Path)) ? Path : FPaths::ConvertRelativePathToFull(FPaths::GetPath(BaseFile) / Path);
 FPaths::NormalizeFilename(Resolved);
 return Resolved;
}

inline EOKStreetSex OKStreetParseSex(const FString& Sex) {
 if (Sex == TEXT("1")) return EOKStreetSex::Female;
 if (Sex == TEXT("0")) return EOKStreetSex::Male;
 return EOKStreetSex::Unknown;
}

inline bool OKStreetIsFemaleTarget(int32 Index, const FOKStreetCharacterMeta& Meta) {
 return Index >= 1 && Index <= OKStreetFemaleCount && Meta.SexKind == EOKStreetSex::Female;
}

// Talk hints, Tab, and E share this predicate. Distance matches the existing
// searches: strictly inside OKStreetTalkRangeCm, so an actor standing on the
// range boundary is not a second candidate. LOS is supplied by the caller.
// Offset is the horizontal male-to-NPC vector used only for view alignment.
inline constexpr int32 OKStreetMaxVoicedTopicEffects = 32;
inline constexpr float OKStreetTalkAlignEpsilon = 1.e-4f;
inline constexpr float OKStreetTalkForwardMin = 1.e-4f;
// Worse than any real dot product in [-1, 1]. Same-XY targets do not win on facing.
inline constexpr float OKStreetTalkAlignDegenerate = -2.f;

enum class EOKStreetAudioCompletion : uint8 {
 Idle,
 Preparing,
 Playing,
 Completed,
 Cancelled,
 Failed
};

inline bool OKStreetRelationshipMayCommit(bool Pending, bool HasTarget, bool LineActive, bool VoiceRequired, bool VoiceCompleted) {
 return Pending && HasTarget && !LineActive && (!VoiceRequired || VoiceCompleted);
}

inline bool OKStreetVoicedTopicMayCommit(EOKStreetAudioCompletion Completion) {
 return Completion == EOKStreetAudioCompletion::Completed;
}

inline const TCHAR* OKStreetAudioCompletionName(EOKStreetAudioCompletion Completion) {
 switch (Completion) {
  case EOKStreetAudioCompletion::Preparing: return TEXT("Preparing");
  case EOKStreetAudioCompletion::Playing: return TEXT("Playing");
  case EOKStreetAudioCompletion::Completed: return TEXT("Completed");
  case EOKStreetAudioCompletion::Cancelled: return TEXT("Cancelled");
  case EOKStreetAudioCompletion::Failed: return TEXT("Failed");
  default: return TEXT("Idle");
 }
}

inline bool OKStreetTopicCommitsFavor(const FString& Category) {
 return Category == TEXT("daily") || Category == TEXT("romance") || Category == TEXT("intimacy");
}

inline bool OKStreetFavorIncreased(float Before, float After) {
 return FMath::IsFinite(Before) && FMath::IsFinite(After) && After > Before + 0.0001f;
}

inline FString OKStreetFormatSocialNumber(float Value) {
 if (!FMath::IsFinite(Value)) return TEXT("0");
 FString Text = FString::Printf(TEXT("%.3f"), FMath::RoundToFloat(Value * 1000.f) / 1000.f);
 const int32 Dot = Text.Find(TEXT("."));
 if (Dot != INDEX_NONE) {
  int32 End = Text.Len();
  while (End > Dot && (Text[End - 1] == TEXT('0') || Text[End - 1] == TEXT('.'))) --End;
  Text = Text.Left(End);
 }
 if (Text.IsEmpty() || Text == TEXT("-") || Text == TEXT("-0")) return TEXT("0");
 return Text;
}

inline FString OKStreetFormatSignedSocialDelta(float Delta) {
 if (!FMath::IsFinite(Delta) || FMath::IsNearlyZero(Delta, 0.0001f)) return TEXT("+0");
 const FString Body = OKStreetFormatSocialNumber(Delta);
 return Delta > 0.f && !Body.StartsWith(TEXT("+")) ? TEXT("+") + Body : Body;
}

inline FString OKStreetFormatSavedTopicResult(float FavorBefore, float FavorAfter, float LewdBefore, float LewdAfter) {
 return OKMsg(TEXT("ui.rules.saved_social_change"), {{TEXT("favor_before"), FString::FromInt(FMath::RoundToInt(FavorBefore))}, {TEXT("favor_after"), FString::FromInt(FMath::RoundToInt(FavorAfter))}, {TEXT("favor_delta"), *OKStreetFormatSignedSocialDelta(float(FMath::RoundToInt(FavorAfter) - FMath::RoundToInt(FavorBefore)))}, {TEXT("lewd_before"), *OKStreetFormatSocialNumber(LewdBefore)}, {TEXT("lewd_after"), *OKStreetFormatSocialNumber(LewdAfter)}, {TEXT("lewd_delta"), *OKStreetFormatSignedSocialDelta(LewdAfter - LewdBefore)}});
}

// Control hints (keys, Esc, topic menu) live behind the bottom-left "?" so the dialogue box keeps only what was said or
// what happened. Splitting a message is language work: the rules do it (locale.rs message_hint); without them it all stays.
inline void OKStreetSplitMessageHint(const FString& Message, FString& Body, FString& Hint) {
 Body = Message; Hint.Reset();
 FOKStreetRules* Rules = OKStreetActiveRules(); if (!Rules || Message.IsEmpty()) return;
 auto Request = MakeShared<FJsonObject>(); Request->SetStringField(TEXT("rule"), TEXT("message_hint"));
 Request->SetStringField(TEXT("lang"), OKStreetCppText().Lang); Request->SetStringField(TEXT("region"), OKStreetCppText().Region); Request->SetStringField(TEXT("message"), Message);
 FString Error; if (const TSharedPtr<FJsonObject> A = Rules->Eval(Request, Error)) { A->TryGetStringField(TEXT("body"), Body); A->TryGetStringField(TEXT("hint"), Hint); }
}

// The street sun follows a clock seeded from the real local time, running 24x faster (4 h per 10 min).
constexpr double OKStreetClockSpeed = 24.0;

inline double OKStreetClockHours(double StartHours, double ElapsedSeconds) {
 const double Hours = FMath::Fmod(StartHours + ElapsedSeconds * OKStreetClockSpeed / 3600.0, 24.0);
 return Hours < 0 ? Hours + 24.0 : Hours;
}

struct FOKStreetSunState {
 FRotator Rotation;
 float Intensity = 0.f;
 FLinearColor Color = FLinearColor::White;
 bool Day = false;
};

// Daylight runs 4:00 to 20:00 (sunrise in the east, noon high in the south at 12:00, sunset in the west). At noon
// this matches the authored sun (yaw -30). From 20:00 to 4:00 the same light becomes a cool moon on the opposite
// side, bright enough that the street stays readable, and shadows keep turning. The light fades briefly at the
// horizon to hide the swap, but never to black.
inline FOKStreetSunState OKStreetSunAt(double Hours) {
 FOKStreetSunState Sun;
 const double H = FMath::Fmod(Hours + 24.0, 24.0);
 Sun.Day = H >= 4.0 && H < 20.0;
 // Position along the current half of the cycle: 0 at its start (sunrise / moonrise), 1 at its end.
 const double Progress = Sun.Day ? (H - 4.0) / 16.0 : (H >= 20.0 ? H - 20.0 : H + 4.0) / 8.0;
 const double Height = FMath::Sin(Progress * PI);
 const double Elevation = Sun.Day ? FMath::Max(10.0, 62.0 * Height) : FMath::Max(35.0, 55.0 * Height);
 // 180 degrees across the day (11.25 per hour) and across the night; the moon starts where the sun rose.
 const double Yaw = Sun.Day ? -30.0 + (H - 12.0) * 11.25 : -120.0 + Progress * 180.0;
 Sun.Rotation = FRotator(-Elevation, FRotator::NormalizeAxis(Yaw), 0.0);
 // Fade covers ~25 clock minutes around 4:00 and 20:00; Warm tints the low sun.
 const float Fade = float(FMath::Clamp(Height / 0.08, 0.0, 1.0)), Warm = float(FMath::Clamp(Height / 0.35, 0.0, 1.0));
 if (Sun.Day) {
  Sun.Intensity = FMath::Lerp(.5f, FMath::Lerp(1.4f, 2.5f, Warm), Fade);
  Sun.Color = FMath::Lerp(FLinearColor(1.f, .62f, .38f), FLinearColor::White, Warm);
 } else {
  Sun.Intensity = FMath::Lerp(.8f, 1.6f, Fade);
  Sun.Color = FLinearColor(.7f, .8f, 1.f);
 }
 return Sun;
}

struct FOKStreetTalkCandidateSample {
 int32 Slot = INDEX_NONE;
 float DistanceCm = 0.f;
 bool Queried = false;
 bool Ready = false;
 bool Visible = false;
 bool ActionsReady = false;
 bool HasError = false;
 bool Stale = false;
 bool Female = false;
 bool LineOfSightClear = false;
 FVector Offset = FVector::ZeroVector;
};

struct FOKStreetTalkViewForward {
 FVector Horizontal = FVector(1.f, 0.f, 0.f);
 bool UsedYawFallback = false;
};

struct FOKStreetTalkSelection {
 int32 Selected = INDEX_NONE;
 int32 Count = 0;
 bool Explicit = false;
};

struct FOKStreetTalkSelectionResult {
 FOKStreetTalkSelection Selection;
 int32 AdmitSlot = INDEX_NONE;
 bool RejectedInteract = false;
};

inline bool OKStreetTalkCandidateEligible(const FOKStreetTalkCandidateSample& Sample) {
 return Sample.Slot >= 1 && Sample.Slot <= OKStreetFemaleCount
  && Sample.Female
  && Sample.Queried
  && !Sample.Stale
  && !Sample.HasError
  && Sample.Ready
  && Sample.Visible
  && Sample.ActionsReady
  && Sample.DistanceCm < OKStreetTalkRangeCm
  && Sample.LineOfSightClear;
}

// Distance-only compatibility helper. Equal distance keeps the earlier sample.
// Tick, Tab, and E use OKStreetResolveTalkSelection instead.
inline int32 OKStreetSelectTalkCandidate(const TArray<FOKStreetTalkCandidateSample>& Samples) {
 int32 BestSlot = INDEX_NONE;
 float BestDistance = OKStreetTalkRangeCm;
 for (const FOKStreetTalkCandidateSample& Sample : Samples) {
  if (!OKStreetTalkCandidateEligible(Sample) || Sample.DistanceCm >= BestDistance) continue;
  BestDistance = Sample.DistanceCm;
  BestSlot = Sample.Slot;
 }
 return BestSlot;
}

// Horizontal look is CameraForward.XY. A zero, non-finite, or vertical vector
// falls back to YawDegrees, then to +X if that yaw is also unusable.
inline FOKStreetTalkViewForward OKStreetTalkCameraHorizontal(const FVector& CameraForward, float YawDegrees) {
 FOKStreetTalkViewForward Out;
 const float X = FMath::IsFinite(CameraForward.X) ? CameraForward.X : 0.f;
 const float Y = FMath::IsFinite(CameraForward.Y) ? CameraForward.Y : 0.f;
 if (X * X + Y * Y > OKStreetTalkForwardMin * OKStreetTalkForwardMin) {
  const float Inv = FMath::InvSqrt(X * X + Y * Y);
  Out.Horizontal = FVector(X * Inv, Y * Inv, 0.f);
  return Out;
 }
 Out.UsedYawFallback = true;
 const float Yaw = FMath::IsFinite(YawDegrees) ? YawDegrees : 0.f;
 const FVector FromYaw = FRotator(0.f, Yaw, 0.f).Vector();
 const float YawSq = FromYaw.X * FromYaw.X + FromYaw.Y * FromYaw.Y;
 if (YawSq > OKStreetTalkForwardMin * OKStreetTalkForwardMin) {
  const float Inv = FMath::InvSqrt(YawSq);
  Out.Horizontal = FVector(FromYaw.X * Inv, FromYaw.Y * Inv, 0.f);
  return Out;
 }
 Out.Horizontal = FVector(1.f, 0.f, 0.f);
 return Out;
}

inline float OKStreetTalkViewAlignment(const FVector& ViewHorizontalUnit, const FVector& FromMaleToNpc) {
 const float X = FMath::IsFinite(FromMaleToNpc.X) ? FromMaleToNpc.X : 0.f;
 const float Y = FMath::IsFinite(FromMaleToNpc.Y) ? FromMaleToNpc.Y : 0.f;
 if (X * X + Y * Y <= OKStreetTalkForwardMin * OKStreetTalkForwardMin) return OKStreetTalkAlignDegenerate;
 const float Inv = FMath::InvSqrt(X * X + Y * Y);
 const float Dot = ViewHorizontalUnit.X * X * Inv + ViewHorizontalUnit.Y * Y * Inv;
 return FMath::IsFinite(Dot) ? FMath::Clamp(Dot, -1.f, 1.f) : OKStreetTalkAlignDegenerate;
}

// Best first. Alignment outside OKStreetTalkAlignEpsilon wins. Tied alignment
// uses the smaller DistanceCm. The same distance keeps the lower slot, then
// the earlier input index. A repeated slot keeps only its better row.
inline void OKStreetOrderTalkCandidateSlots(const TArray<FOKStreetTalkCandidateSample>& Samples, const FVector& ViewHorizontalUnit, TArray<int32>& OutSlots) {
 struct FRow { int32 Slot = INDEX_NONE; int32 Index = 0; float Alignment = OKStreetTalkAlignDegenerate; float Distance = 0.f; };
 TArray<FRow> Rows;
 for (int32 Index = 0; Index < Samples.Num(); ++Index) {
  const FOKStreetTalkCandidateSample& Sample = Samples[Index];
  if (!OKStreetTalkCandidateEligible(Sample)) continue;
  FRow Row;
  Row.Slot = Sample.Slot;
  Row.Index = Index;
  Row.Alignment = OKStreetTalkViewAlignment(ViewHorizontalUnit, Sample.Offset);
  Row.Distance = Sample.DistanceCm;
  Rows.Add(Row);
 }
 auto Precedes = [](const FRow& A, const FRow& B) {
  if (FMath::Abs(A.Alignment - B.Alignment) > OKStreetTalkAlignEpsilon) return A.Alignment > B.Alignment;
  if (A.Distance != B.Distance) return A.Distance < B.Distance;
  if (A.Slot != B.Slot) return A.Slot < B.Slot;
  return A.Index < B.Index;
 };
 for (int32 I = 0; I < Rows.Num(); ++I) {
  int32 Best = I;
  for (int32 J = I + 1; J < Rows.Num(); ++J) if (Precedes(Rows[J], Rows[Best])) Best = J;
  if (Best != I) Swap(Rows[I], Rows[Best]);
 }
 OutSlots.Reset();
 for (const FRow& Row : Rows) if (!OutSlots.Contains(Row.Slot)) OutSlots.Add(Row.Slot);
}

inline int32 OKStreetSelectTalkCandidateByView(const TArray<FOKStreetTalkCandidateSample>& Samples, const FVector& ViewHorizontalUnit) {
 TArray<int32> Ordered;
 OKStreetOrderTalkCandidateSlots(Samples, ViewHorizontalUnit, Ordered);
 return Ordered.IsEmpty() ? INDEX_NONE : Ordered[0];
}

inline FString OKStreetTalkCandidateHint(const FString& Name, int32 EligibleCount) {
 if (EligibleCount <= 0 || Name.IsEmpty()) return FString();
 return OKMsg(TEXT("ui.rules.talk_hint"), {{TEXT("name"), Name}, {TEXT("many"), EligibleCount > 1 ? TEXT("yes") : TEXT("no")}});
}

// Tab and a plain rescan never replace an explicit slot that is still eligible.
// Interact revalidates that shown slot only. An ineligible hint refreshes the
// automatic choice and admits nobody; the same press does not also cycle.
inline FOKStreetTalkSelectionResult OKStreetResolveTalkSelection(const FOKStreetTalkSelection& Current, const TArray<FOKStreetTalkCandidateSample>& Samples, const FVector& ViewHorizontalUnit, bool TabPressed, bool InteractPressed) {
 TArray<int32> Ordered;
 OKStreetOrderTalkCandidateSlots(Samples, ViewHorizontalUnit, Ordered);
 FOKStreetTalkSelectionResult Result;
 Result.Selection.Count = Ordered.Num();
 const int32 Shown = Ordered.Find(Current.Selected);
 if (InteractPressed) {
  if (Shown != INDEX_NONE) {
   Result.AdmitSlot = Current.Selected;
   Result.Selection.Selected = Current.Selected;
   Result.Selection.Explicit = Current.Explicit;
   return Result;
  }
  Result.RejectedInteract = true;
  Result.Selection.Selected = Ordered.IsEmpty() ? INDEX_NONE : Ordered[0];
  Result.Selection.Explicit = false;
  return Result;
 }
 if (TabPressed && !Ordered.IsEmpty()) {
  const int32 Next = Shown == INDEX_NONE ? 0 : (Shown + 1) % Ordered.Num();
  Result.Selection.Selected = Ordered[Next];
  Result.Selection.Explicit = true;
  return Result;
 }
 if (Current.Explicit && Shown != INDEX_NONE) {
  Result.Selection.Selected = Current.Selected;
  Result.Selection.Explicit = true;
  return Result;
 }
 Result.Selection.Selected = Ordered.IsEmpty() ? INDEX_NONE : Ordered[0];
 Result.Selection.Explicit = false;
 return Result;
}

inline FString OKStreetStableId(const FOKStreetCharacterMeta& Meta) {
 if (!Meta.Id.IsEmpty()) return Meta.Id;
 if (!Meta.Fingerprint.IsEmpty()) return TEXT("fp:") + Meta.Fingerprint;
 return FString();
}

inline bool OKStreetIsProductionApngId(const FString& Id) {
 if (Id.Len() != 69 || !Id.StartsWith(TEXT("apng-"))) return false;
 for (int32 Index = 5; Index < Id.Len(); ++Index) {
  const TCHAR Char = Id[Index];
  if (!((Char >= TEXT('0') && Char <= TEXT('9')) || (Char >= TEXT('a') && Char <= TEXT('f')))) return false;
 }
 return true;
}

inline bool OKStreetIsRetargetRequestId(const FString& Id) {
 if (Id.IsEmpty() || Id.Len() > 64 || Id.StartsWith(TEXT("session-"))) return false;
 for (int32 Index = 0; Index < Id.Len(); ++Index) {
  const TCHAR Char = Id[Index];
  const bool Ok = (Char >= TEXT('A') && Char <= TEXT('Z')) || (Char >= TEXT('a') && Char <= TEXT('z')) || (Char >= TEXT('0') && Char <= TEXT('9')) || Char == TEXT('_') || Char == TEXT('-');
  if (!Ok) return false;
 }
 return true;
}

inline bool OKStreetActiveActorShouldEnd(const FOKStreetPresenceSample& Sample, FOKStreetActorPresence& Presence) {
 bool End = false;
 if (Sample.HasError) End = true;
 else if (!Sample.Queried) End = Presence.SeenReady;
 else {
  if (Presence.SeenReady && !Sample.Ready) End = true;
  if (Presence.SeenVisible && !Sample.Visible) End = true;
 }
 if (Sample.Queried && !Sample.HasError) {
  if (Sample.Ready) Presence.SeenReady = true;
  if (Sample.Visible) Presence.SeenVisible = true;
 }
 return End;
}

inline bool OKStreetIsApprovedCategory(const FString& Category) {
 return Category == TEXT("invitation") || Category == TEXT("daily") || Category == TEXT("romance") || Category == TEXT("intimacy") || Category == TEXT("greeting") || Category == TEXT("farewell");
}

inline int32 OKStreetTalkCost(const FString& Category) {
 return (Category == TEXT("daily") || Category == TEXT("romance") || Category == TEXT("intimacy")) ? 1 : 0;
}

inline bool OKStreetIsApprovedStage(const FString& Stage) {
 return Stage == TEXT("stranger") || Stage == TEXT("friend") || Stage == TEXT("lover");
}

// Subtitles of the original lines in the player's language (Localization/<lang>/dialogue.json, written by
// Tools/i18n_merge_dialogue.lua), by the line's key: the first 16 hex digits of SHA-1(jp + U+001F + zh).
inline TMap<FString, FString>& OKStreetSubtitles() { static TMap<FString, FString> Table; return Table; }
inline FString OKStreetSubtitleKey(const FString& Jp, const FString& Zh) {
 const FTCHARToUTF8 Utf8(*(Jp + TEXT("\x1f") + Zh));
 uint8 Hash[20]; FSHA1::HashBuffer(Utf8.Get(), Utf8.Length(), Hash);
 FString Key; for (int32 I = 0; I < 8; ++I) Key += FString::Printf(TEXT("%02x"), Hash[I]);
 return Key;
}

inline FString OKStreetLineLabel(const FString& Zh, const FString& Jp, const FString& Id) {
 if (!OKStreetSubtitles().IsEmpty()) if (const FString* Text = OKStreetSubtitles().Find(OKStreetSubtitleKey(Jp, Zh))) return *Text;
 if (!Zh.IsEmpty()) return Zh;
 if (!Jp.IsEmpty()) return Jp;
 return Id.IsEmpty() ? FString(TEXT("(no subtitle)")) : Id;
}

inline bool OKStreetJsonInt(const TSharedPtr<FJsonValue>& Value, int32& Out) {
 if (!Value.IsValid() || Value->Type != EJson::Number) return false;
 const double Number = Value->AsNumber();
 if (!FMath::IsFinite(Number) || Number != FMath::FloorToDouble(Number)) return false;
 if (Number < double(MIN_int32) || Number > double(MAX_int32)) return false;
 Out = int32(Number);
 return true;
}

inline bool OKStreetCanonicalInt(const FString& Text, int32& Out) {
 if (Text.IsEmpty()) return false;
 int32 Index = 0;
 if (Text[0] == TCHAR('-')) { if (Text.Len() == 1) return false; Index = 1; }
 for (int32 I = Index; I < Text.Len(); ++I) if (!FChar::IsDigit(Text[I])) return false;
 const int32 Value = FCString::Atoi(*Text);
 if (FString::FromInt(Value) != Text) return false;
 Out = Value;
 return true;
}

inline FString OKStreetStageName(int32 Stage) {
 if (Stage == 0) return TEXT("stranger");
 if (Stage == 1) return TEXT("friend");
 if (Stage == 2) return TEXT("lover");
 return FString();
}

inline int32 OKStreetStageIndex(const FString& Stage) {
 if (Stage == TEXT("stranger")) return 0;
 if (Stage == TEXT("friend")) return 1;
 if (Stage == TEXT("lover")) return 2;
 return INDEX_NONE;
}












inline FString OKStreetTransactionId(const FString& StableId, const FString& NodeId) {
 return StableId + TEXT("|") + NodeId;
}

inline bool OKStreetIsGenericRole(const FOKStreetNode& Node) {
 return Node.Roles.Num() == 1 && Node.Roles[0] == TEXT("generic");
}

inline bool OKStreetHasToken(const TArray<FString>& Values, const FString& Token) { return Values.Contains(Token); }

inline void OKStreetAddToken(TArray<FString>& Values, const FString& Token) {
 if (!Token.IsEmpty() && !Values.Contains(Token)) Values.Add(Token);
 Values.Sort();
}






inline FString OKStreetTopicToken(int32 TopicId) { return FString::Printf(TEXT("topic:%d"), TopicId); }




inline void OKStreetAdvanceDay(FOKStreetRelation& Relation, int32 Days = 1) { if (Days > 0) Relation.Day += Days; }

inline int32 OKStreetRelationIndex(FOKStreetStore& Store, const FString& Id, int32 Budget, bool Create) {
 for (int32 I = 0; I < Store.Relations.Num(); ++I) if (Store.Relations[I].Id == Id) return I;
 if (!Create || Id.IsEmpty()) return INDEX_NONE;
 FOKStreetRelation Relation;
 Relation.Id = Id;
 Relation.TalksRemaining = Budget;
 Store.Relations.Add(Relation);
 return Store.Relations.Num() - 1;
}




inline void OKStreetPlaybackFiles(const FOKStreetNode& Node, TArray<FString>& Out) {
 Out.Reset();
 for (const FString& File : Node.VoiceFiles) if (!File.IsEmpty()) Out.Add(File);
}

inline void OKStreetWarn(FOKStreetCatalogue& Catalogue, const FString& Warning) {
 ++Catalogue.Skipped;
 if (Catalogue.Warnings.Num() < 32) Catalogue.Warnings.Add(Warning);
}

inline bool OKStreetReadEffect(const TSharedPtr<FJsonObject>& Object, FOKStreetEffect& Effect, FString& Error) {
 if (!Object) { Error = TEXT("missing_object"); return false; }
 double Number = 0.0;
 if (Object->TryGetNumberField(TEXT("favor"), Number)) { if (!FMath::IsFinite(Number)) { Error = TEXT("bad_favor"); return false; } Effect.Favor = float(Number); }
 else if (Object->HasField(TEXT("favor"))) { Error = TEXT("bad_favor"); return false; }
 if (Object->TryGetNumberField(TEXT("lewdness"), Number)) { if (!FMath::IsFinite(Number)) { Error = TEXT("bad_lewdness"); return false; } Effect.Lewdness = float(Number); }
 else if (Object->HasField(TEXT("lewdness"))) { Error = TEXT("bad_lewdness"); return false; }
 if (Object->HasField(TEXT("relation_event"))) {
  if (!Object->TryGetStringField(TEXT("relation_event"), Effect.RelationEvent) || (!Effect.RelationEvent.IsEmpty() && !OKStreetIsApprovedStage(Effect.RelationEvent))) { Error = TEXT("unsupported_relation_event"); return false; }
 }
 if (Object->HasField(TEXT("reset_favor")) && !Object->TryGetBoolField(TEXT("reset_favor"), Effect.ResetFavor)) { Error = TEXT("bad_reset_favor"); return false; }
 if (Object->HasField(TEXT("set_flags"))) {
  const TArray<TSharedPtr<FJsonValue>>* Flags = nullptr;
  if (!Object->TryGetArrayField(TEXT("set_flags"), Flags) || !Flags) { Error = TEXT("unsupported_flag_shape"); return false; }
  for (const TSharedPtr<FJsonValue>& Flag : *Flags) {
   FString Text;
   if (!Flag.IsValid() || !Flag->TryGetString(Text) || Text.IsEmpty()) { Error = TEXT("unsupported_flag_shape"); return false; }
   Effect.SetFlags.Add(Text);
  }
 }
 return true;
}

inline bool OKStreetAppendConditionObject(const TSharedPtr<FJsonObject>& Condition, TArray<FString>& Conditions, int32& ConditionId, FString& Filter, FString& Support, FString& Error) {
 if (!Condition.IsValid()) { Error = TEXT("conditions_not_object"); return false; }
 if (!OKStreetJsonInt(Condition->TryGetField(TEXT("id")), ConditionId)) { Error = TEXT("condition_id_invalid"); return false; }
 Condition->TryGetStringField(TEXT("game_speak_filter"), Filter);
 Condition->TryGetStringField(TEXT("street_support"), Support);
 FString Context,Rule;Condition->TryGetStringField(TEXT("context"),Context);Condition->TryGetStringField(TEXT("rule"),Rule);
 // Info.GetIntroductionADV fallback rows use id 0 at lover stage; this is not Speak id 0.
 const bool IntroductionFallback=Context==TEXT("introduction")&&Rule==TEXT("fallback_shuffle_pool")&&Support==TEXT("supported");
 if ((ConditionId == -1 && Support == TEXT("supported"))||(ConditionId==0&&IntroductionFallback)) Conditions.Add(TEXT("speak=always"));
 else if (ConditionId == 0) Conditions.Add(TEXT("speak=never"));
 else Conditions.Add(FString::Printf(TEXT("street_support=unsupported:id_%d:%s"), ConditionId, *Filter));
 return true;
}

inline bool OKStreetReadConditions(const TSharedPtr<FJsonObject>& Object, TArray<FString>& Conditions, FString& Error) {
 if (!Object.IsValid() || !Object->HasField(TEXT("conditions"))) return true;
 const TSharedPtr<FJsonObject>* Condition = nullptr;
 if (!Object->TryGetObjectField(TEXT("conditions"), Condition) || !Condition || !Condition->IsValid()) { Error = TEXT("conditions_not_object"); return false; }
 int32 ConditionId = INDEX_NONE;
 FString Filter, Support;
 return OKStreetAppendConditionObject(*Condition, Conditions, ConditionId, Filter, Support, Error);
}

inline bool OKStreetReadChoice(const TSharedPtr<FJsonObject>& Object, FOKStreetChoice& Choice, FString& Error) {
 if (!Object || !Object->TryGetStringField(TEXT("id"), Choice.Id) || Choice.Id.IsEmpty()) { Error = TEXT("choice_missing_id"); return false; }
 Object->TryGetStringField(TEXT("text_jp"), Choice.TextJp);
 Object->TryGetStringField(TEXT("text_zh"), Choice.TextZh);
 Object->TryGetStringField(TEXT("next"), Choice.Next);
 Object->TryGetStringField(TEXT("source"), Choice.Source);
 if (!OKStreetReadConditions(Object, Choice.Conditions, Error)) return false;
 if (!OKStreetReadEffect(Object, Choice.Effect, Error)) return false;
 Choice.Effect.TalkCost = 0;
 Choice.Effect.Memory = Choice.Id;
 return true;
}

inline bool OKStreetReadVoiceSequence(const TSharedPtr<FJsonObject>& Source, FOKStreetNode& Node, const FString& CatalogueFile, const TSharedPtr<FJsonObject>& NodeObject = nullptr) {
 const TArray<TSharedPtr<FJsonValue>>* Rows = nullptr;
 if (!Source.IsValid() || !Source->TryGetArrayField(TEXT("voice_sequence"), Rows) || !Rows) { Node.VoiceSequenceReason = TEXT("voice_sequence_missing"); return false; }
 if (Rows->Num() == 0) { Node.VoiceSequenceComplete = true; Node.VoiceSequenceReason = TEXT("voice_sequence_empty"); return true; }
 struct FSlot { int32 Index = INDEX_NONE; FString Status, Path, Zh; };
 TArray<FSlot> Slots;
 for (const TSharedPtr<FJsonValue>& Row : *Rows) {
  const TSharedPtr<FJsonObject> SlotObject = Row.IsValid() ? Row->AsObject() : nullptr;
  if (!SlotObject.IsValid()) { Node.VoiceSequenceReason = TEXT("voice_sequence_incomplete:bad_slot"); return false; }
  FSlot Slot;
  if (!OKStreetJsonInt(SlotObject->TryGetField(TEXT("index")), Slot.Index)) { Node.VoiceSequenceReason = TEXT("voice_sequence_incomplete:bad_index"); return false; }
  SlotObject->TryGetStringField(TEXT("status"), Slot.Status);
  FString Path, Zh;
  if (SlotObject->TryGetStringField(TEXT("path"), Path)) Slot.Path = Path;
  if (SlotObject->TryGetStringField(TEXT("zh_ogg"), Zh)) Slot.Zh = OKStreetResolveAgainst(CatalogueFile, Zh);
  Slots.Add(Slot);
 }
 Slots.Sort([](const FSlot& A, const FSlot& B) { return A.Index < B.Index; });
 for (int32 Index = 0; Index < Slots.Num(); ++Index) if (Slots[Index].Index != Index) { Node.VoiceSequenceReason = TEXT("voice_sequence_incomplete:index_hole"); return false; }
 TMap<int32, FString> AudioRef;
 if (NodeObject.IsValid()) {
  const TArray<TSharedPtr<FJsonValue>>* Audio = nullptr;
  if (NodeObject->TryGetArrayField(TEXT("audio_slots"), Audio) && Audio) {
   for (const TSharedPtr<FJsonValue>& AudioRow : *Audio) {
    const TSharedPtr<FJsonObject> AudioObject = AudioRow.IsValid() ? AudioRow->AsObject() : nullptr;
    int32 AudioIndex = INDEX_NONE;
    FString Ref;
    if (!AudioObject.IsValid() || !OKStreetJsonInt(AudioObject->TryGetField(TEXT("index")), AudioIndex)) continue;
    if (!AudioObject->TryGetStringField(TEXT("zh_ref"), Ref) || Ref.IsEmpty()) AudioObject->TryGetStringField(TEXT("path"), Ref);
    if (!Ref.IsEmpty()) AudioRef.Add(AudioIndex, OKStreetResolveAgainst(CatalogueFile, Ref));
   }
  }
 }
 for (FSlot& Slot : Slots) {
  if (Slot.Status != TEXT("matched")) { Node.VoiceSequenceReason = TEXT("voice_sequence_incomplete:") + (Slot.Status.IsEmpty() ? TEXT("missing_status") : Slot.Status); return false; }
  if (Slot.Path.IsEmpty() && Slot.Zh.IsEmpty()) { if (const FString* Ref = AudioRef.Find(Slot.Index)) Slot.Zh = *Ref; }
  if (Slot.Path.IsEmpty() && Slot.Zh.IsEmpty()) { Node.VoiceSequenceReason = TEXT("voice_sequence_incomplete:missing_path"); return false; }
 }
 for (const FSlot& Slot : Slots) Node.VoiceFiles.Add(Slot.Zh.IsEmpty() ? OKStreetResolveAgainst(CatalogueFile,Slot.Path) : Slot.Zh);
 Node.VoiceSequenceComplete = true;
 Node.VoiceSequenceReason = TEXT("voice_sequence_matched");
 return true;
}

inline int32 OKStreetFindRoleRow(const FOKStreetCatalogue& Catalogue, int32 Personality, int32 Row, const FString& Role) {
 for (int32 Index = 0; Index < Catalogue.Nodes.Num(); ++Index) {
  const FOKStreetNode& Node = Catalogue.Nodes[Index];
  if (Node.PersonalityId == Personality && Node.Row == Row && Node.Roles.Num() == 1 && Node.Roles[0] == Role) return Index;
 }
 return INDEX_NONE;
}

inline void OKStreetReadWeightArray(const TSharedPtr<FJsonObject>& Object, const TCHAR* Name, TArray<int32>& Out) {
 const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
 if (!Object.IsValid() || !Object->TryGetArrayField(Name, Items) || !Items) return;
 for (const TSharedPtr<FJsonValue>& Item : *Items) { int32 Number = 0; if (OKStreetJsonInt(Item, Number)) Out.Add(Number); }
}

inline void OKStreetAttachBranches(const TSharedPtr<FJsonObject>& Root, FOKStreetCatalogue& Catalogue) {
 const TArray<TSharedPtr<FJsonValue>>* Rows = nullptr;
 if (!Root.IsValid() || !Root->TryGetArrayField(TEXT("branches"), Rows) || !Rows) return;
 for (const TSharedPtr<FJsonValue>& Value : *Rows) {
  const TSharedPtr<FJsonObject> Object = Value.IsValid() ? Value->AsObject() : nullptr;
  if (!Object.IsValid()) continue;
  FOKStreetBranch Branch;
  if (!Object->TryGetStringField(TEXT("id"), Branch.Id) || Branch.Id.IsEmpty()) continue;
  if (!OKStreetJsonInt(Object->TryGetField(TEXT("personality")), Branch.Personality)) continue;
  if (!OKStreetJsonInt(Object->TryGetField(TEXT("stage")), Branch.Stage)) continue;
  if (!OKStreetJsonInt(Object->TryGetField(TEXT("container")), Branch.Container)) continue;
  if (!OKStreetJsonInt(Object->TryGetField(TEXT("command")), Branch.Command)) Branch.Command = INDEX_NONE;
  Object->TryGetStringField(TEXT("category"), Branch.Category);
  Object->TryGetStringField(TEXT("group"), Branch.Group);
  OKStreetJsonInt(Object->TryGetField(TEXT("controlling_row")), Branch.ControllingRow);
  const TSharedPtr<FJsonObject>* Condition = nullptr;
  if (Object->TryGetObjectField(TEXT("controlling_condition"), Condition) && Condition && Condition->IsValid()) OKStreetJsonInt((*Condition)->TryGetField(TEXT("id")), Branch.ConditionId);
  const TSharedPtr<FJsonObject>* Selection = nullptr;
  if (Object->TryGetObjectField(TEXT("selection"), Selection) && Selection && Selection->IsValid()) {
   (*Selection)->TryGetStringField(TEXT("kind"), Branch.Kind);
   if (!OKStreetJsonInt((*Selection)->TryGetField(TEXT("matched_attribute_type")), Branch.MatchedAttributeType)) Branch.MatchedAttributeType = INDEX_NONE;
   if (!OKStreetJsonInt((*Selection)->TryGetField(TEXT("axis")), Branch.Axis)) Branch.Axis = INDEX_NONE;
   (*Selection)->TryGetStringField(TEXT("operator"), Branch.Operator);
   OKStreetJsonInt((*Selection)->TryGetField(TEXT("threshold")), Branch.Threshold);
   if (!OKStreetJsonInt((*Selection)->TryGetField(TEXT("index_when_true")), Branch.IndexWhenTrue)) Branch.IndexWhenTrue = INDEX_NONE;
   if (!OKStreetJsonInt((*Selection)->TryGetField(TEXT("index_when_false")), Branch.IndexWhenFalse)) Branch.IndexWhenFalse = INDEX_NONE;
   if (!OKStreetJsonInt((*Selection)->TryGetField(TEXT("slot")), Branch.TemperSlot)) Branch.TemperSlot = INDEX_NONE;
   OKStreetReadWeightArray(*Selection, TEXT("weights_when_matched"), Branch.WeightsMatched);
   OKStreetReadWeightArray(*Selection, TEXT("weights_when_not_matched"), Branch.WeightsUnmatched);
   OKStreetReadWeightArray(*Selection, TEXT("weights"), Branch.Weights);
  }
  const TArray<TSharedPtr<FJsonValue>>* Indexer = nullptr;
  if (Object->TryGetArrayField(TEXT("indexer"), Indexer) && Indexer) {
   for (const TSharedPtr<FJsonValue>& Item : *Indexer) {
    const TSharedPtr<FJsonObject> MemberObject = Item.IsValid() ? Item->AsObject() : nullptr;
    if (!MemberObject.IsValid()) continue;
    FOKStreetBranchMember Member;
    if (!OKStreetJsonInt(MemberObject->TryGetField(TEXT("row")), Member.Row)) continue;
    MemberObject->TryGetStringField(TEXT("role"), Member.Role);
    OKStreetJsonInt(MemberObject->TryGetField(TEXT("part_index")), Member.PartIndex);
    Member.Node = OKStreetFindRoleRow(Catalogue, Branch.Personality, Member.Row, Member.Role);
    Branch.Indexer.Add(Member);
   }
  }
  if (Branch.ControllingRow != INDEX_NONE) Branch.IntroductionNode = OKStreetFindRoleRow(Catalogue, Branch.Personality, Branch.ControllingRow, TEXT("introduction"));
  Catalogue.Branches.Add(MoveTemp(Branch));
 }
}

inline FOKStreetCatalogue OKStreetParseCatalogue(const FString& Json, const FString& CatalogueFile) {
 FOKStreetCatalogue Catalogue;
 TSharedPtr<FJsonObject> Root;
 if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root) || !Root) { Catalogue.Error = TEXT("catalogue_json_invalid"); return Catalogue; }
 const TArray<TSharedPtr<FJsonValue>>* Nodes = nullptr;
 if (!Root->TryGetArrayField(TEXT("nodes"), Nodes) || !Nodes) { Catalogue.Error = TEXT("catalogue_nodes_missing"); return Catalogue; }
 for (int32 Index = 0; Index < Nodes->Num(); ++Index) {
  const TSharedPtr<FJsonObject> Object = (*Nodes)[Index].IsValid() ? (*Nodes)[Index]->AsObject() : nullptr;
  if (!Object) { OKStreetWarn(Catalogue, FString::Printf(TEXT("node_%d_not_object"), Index)); continue; }
  FOKStreetNode Node;
  FString Error;
  if (!Object->TryGetStringField(TEXT("id"), Node.Id) || Node.Id.IsEmpty()) { OKStreetWarn(Catalogue, TEXT("missing_id")); continue; }
  if (Catalogue.ById.Contains(Node.Id)) { OKStreetWarn(Catalogue, TEXT("duplicate_id:") + Node.Id); continue; }
  if (!OKStreetJsonInt(Object->TryGetField(TEXT("personality")), Node.PersonalityId)) { OKStreetWarn(Catalogue, TEXT("personality_not_integer:") + Node.Id); continue; }
  Node.Personality = FString::FromInt(Node.PersonalityId);
  if (!Object->TryGetStringField(TEXT("category"), Node.Category) || !OKStreetIsApprovedCategory(Node.Category)) { OKStreetWarn(Catalogue, TEXT("unsupported_category:") + Node.Id); continue; }
  const TSharedPtr<FJsonObject>* Source = nullptr;
  if (!Object->TryGetObjectField(TEXT("source"), Source) || !Source || !Source->IsValid()) { OKStreetWarn(Catalogue, TEXT("source_not_object:") + Node.Id); continue; }
  if (!OKStreetJsonInt((*Source)->TryGetField(TEXT("stage")), Node.StageIndex)) { OKStreetWarn(Catalogue, TEXT("stage_not_integer:") + Node.Id); continue; }
  int32 Priority = 0;
  if (!OKStreetJsonInt((*Source)->TryGetField(TEXT("priority")), Priority)) { OKStreetWarn(Catalogue, TEXT("bad_priority:") + Node.Id); continue; }
  Node.Priority = float(Priority);
  const TArray<TSharedPtr<FJsonValue>>* Roles = nullptr;
  if ((*Source)->TryGetArrayField(TEXT("roles"), Roles) && Roles) for (const TSharedPtr<FJsonValue>& Role : *Roles) { FString Text; if (Role.IsValid() && Role->TryGetString(Text) && !Text.IsEmpty()) Node.Roles.Add(Text); }
  if (!(*Source)->TryGetBoolField(TEXT("effects_defined"), Node.EffectsDefined)) Node.EffectsDefined = false;
  int32 Command = 0, PartIndex = 0, Row = 0, Topic = 0;
  if (OKStreetJsonInt((*Source)->TryGetField(TEXT("command")), Command)) Node.Command = Command;
  if (OKStreetJsonInt((*Source)->TryGetField(TEXT("part_index")), PartIndex)) Node.PartIndex = PartIndex;
  if (OKStreetJsonInt((*Source)->TryGetField(TEXT("row")), Row)) Node.Row = Row;
  (*Source)->TryGetStringField(TEXT("group"), Node.Group);
  (*Source)->TryGetStringField(TEXT("facial_expression"),Node.FacialExpression);
  if (OKStreetJsonInt((*Source)->TryGetField(TEXT("topic")), Topic) && Topic >= 0) Node.TopicId = Topic;
  const TArray<TSharedPtr<FJsonValue>>* Containers = nullptr;
  if ((*Source)->TryGetArrayField(TEXT("containers"), Containers) && Containers) for (const TSharedPtr<FJsonValue>& Container : *Containers) { int32 Number = 0; if (OKStreetJsonInt(Container, Number)) Node.Containers.Add(Number); }
  const TArray<TSharedPtr<FJsonValue>>* BranchIds = nullptr;
  if (Object->TryGetArrayField(TEXT("branch_ids"), BranchIds) && BranchIds) for (const TSharedPtr<FJsonValue>& BranchId : *BranchIds) { FString Text; if (BranchId.IsValid() && BranchId->TryGetString(Text) && !Text.IsEmpty()) Node.BranchIds.Add(Text); }
  Object->TryGetStringField(TEXT("text_jp"), Node.TextJp);
  Object->TryGetStringField(TEXT("text_zh"), Node.TextZh);
  const TSharedPtr<FJsonObject>* Condition = nullptr;
  if (!Object->TryGetObjectField(TEXT("conditions"), Condition) || !OKStreetAppendConditionObject(Condition && Condition->IsValid() ? *Condition : nullptr, Node.Conditions, Node.ConditionId, Node.GameSpeakFilter, Node.StreetSupport, Error)) { OKStreetWarn(Catalogue, Error + TEXT(":") + Node.Id); continue; }
  const FString StageName = OKStreetStageName(Node.StageIndex);
  if (StageName.IsEmpty()) Node.Conditions.Add(FString::Printf(TEXT("street_support=unsupported:stage_%d"), Node.StageIndex));
  else Node.Conditions.Add(TEXT("relation=") + StageName);
  int32 Favor = 0, Lewdness = 0;
  if (Object->HasField(TEXT("favor")) && !OKStreetJsonInt(Object->TryGetField(TEXT("favor")), Favor)) { OKStreetWarn(Catalogue, TEXT("bad_favor:") + Node.Id); continue; }
  if (Object->HasField(TEXT("lewdness")) && !OKStreetJsonInt(Object->TryGetField(TEXT("lewdness")), Lewdness)) { OKStreetWarn(Catalogue, TEXT("bad_lewdness:") + Node.Id); continue; }
  Node.Effect.Favor = Node.EffectsDefined ? float(Favor) : 0.f;
  Node.Effect.Lewdness = Node.EffectsDefined ? float(Lewdness) : 0.f;
  Node.Effect.TalkCost = OKStreetTalkCost(Node.Category);
  Node.Effect.Memory = Node.Id;
  Node.Effect.TopicId = Node.TopicId;
  int32 CooldownDays = 0;
  if (OKStreetJsonInt(Object->TryGetField(TEXT("cooldown_days")), CooldownDays) && CooldownDays > 0) Node.Effect.CooldownDays = CooldownDays;
  Node.Source = FString::Printf(TEXT("stage=%d;priority=%d;role=%s;effects=%d"), Node.StageIndex, Priority, Node.Roles.Num() ? *FString::Join(Node.Roles, TEXT("+")) : TEXT("missing"), Node.EffectsDefined ? 1 : 0);
  OKStreetReadVoiceSequence(*Source, Node, CatalogueFile, Object);
  // The runtime manifest generator selects one complete language sequence.
  // Preserve that choice after validating the original slot order and holes.
  const TArray<TSharedPtr<FJsonValue>>* SelectedVoices=nullptr;
  if(Node.VoiceSequenceComplete && Object->TryGetArrayField(TEXT("voice_files"),SelectedVoices) && SelectedVoices && SelectedVoices->Num()==Node.VoiceFiles.Num()){
   TArray<FString> Selected;bool Valid=true;
   for(const auto& Value:*SelectedVoices){FString Path;if(!Value.IsValid()||!Value->TryGetString(Path)||Path.IsEmpty()){Valid=false;break;}Selected.Add(OKStreetResolveAgainst(CatalogueFile,Path));}
   // Original source catalogues use Japanese; only runtime selections have an
   // explicit language marker. Raw catalogue tests continue to prefer Chinese.
   if(Valid && Object->HasField(TEXT("voice_language")))Node.VoiceFiles=MoveTemp(Selected);
  }
  if (Object->HasField(TEXT("choices"))) {
   const TArray<TSharedPtr<FJsonValue>>* Choices = nullptr;
   if (!Object->TryGetArrayField(TEXT("choices"), Choices) || !Choices) { OKStreetWarn(Catalogue, TEXT("bad_choices:") + Node.Id); continue; }
   bool ChoiceOk = true;
   for (const TSharedPtr<FJsonValue>& ChoiceValue : *Choices) {
    FOKStreetChoice Choice;
    if (!OKStreetReadChoice(ChoiceValue.IsValid() ? ChoiceValue->AsObject() : nullptr, Choice, Error)) { ChoiceOk = false; break; }
    Node.Choices.Add(MoveTemp(Choice));
   }
   if (!ChoiceOk) { OKStreetWarn(Catalogue, Error + TEXT(":") + Node.Id); continue; }
  }
  const int32 Stored = Catalogue.Nodes.Add(MoveTemp(Node));
  Catalogue.ById.Add(Catalogue.Nodes[Stored].Id, Stored);
  Catalogue.ByPersonality.FindOrAdd(Catalogue.Nodes[Stored].Personality).Add(Stored);
 }
 OKStreetAttachBranches(Root, Catalogue);
 return Catalogue;
}

inline FOKStreetRelation OKStreetViewRelation(const FOKStreetStore& Store, const FString& StableId, int32 Budget) {
 for (const FOKStreetRelation& Relation : Store.Relations) if (Relation.Id == StableId) return Relation;
 FOKStreetRelation Created;
 Created.Id = StableId;
 Created.TalksRemaining = Budget;
 return Created;
}







inline void OKStreetWriteStringArray(TSharedRef<FJsonObject> Object, const TCHAR* Name, const TArray<FString>& Values) {
 TArray<TSharedPtr<FJsonValue>> Rows;
 for (const FString& Value : Values) if (!Value.StartsWith(TEXT("session-"))) Rows.Add(MakeShared<FJsonValueString>(Value));
 Object->SetArrayField(Name, Rows);
}

inline FString OKStreetWriteSave(const FOKStreetStore& Store) {
 TArray<int32> Order;
 for (int32 I = 0; I < Store.Relations.Num(); ++I) if (!Store.Relations[I].Id.IsEmpty() && !Store.Relations[I].Id.StartsWith(TEXT("session-"))) Order.Add(I);
 Order.Sort([&](int32 A, int32 B) { return Store.Relations[A].Id < Store.Relations[B].Id; });
 TArray<TSharedPtr<FJsonValue>> Rows;
 for (int32 Index : Order) {
  const FOKStreetRelation& Relation = Store.Relations[Index];
  TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
  Object->SetStringField(TEXT("id"), Relation.Id);
  Object->SetNumberField(TEXT("favor"), Relation.Favor);
  Object->SetNumberField(TEXT("lewdness"), Relation.Lewdness);
  Object->SetNumberField(TEXT("sex_desire"),Relation.SexDesire);Object->SetNumberField(TEXT("topic_points"),Relation.TopicPoints);
  if(Relation.Chastity>=0.f)Object->SetNumberField(TEXT("chastity"),Relation.Chastity);
  if(Relation.StrangerCap>0)Object->SetNumberField(TEXT("stranger_cap"),Relation.StrangerCap);
  {auto Axes=MakeShared<FJsonObject>();for(const auto& A:OKStreetAxisFields(const_cast<FOKStreetRelation&>(Relation)))Axes->SetNumberField(A.Key,*A.Value);Object->SetObjectField(TEXT("axes"),Axes);
   auto Toward=MakeShared<FJsonObject>();TArray<FString> Keys;Relation.JealousyToward.GetKeys(Keys);Keys.Sort();for(const FString& K:Keys)Toward->SetNumberField(K,Relation.JealousyToward.FindChecked(K));Object->SetObjectField(TEXT("jealousy_toward"),Toward);
   {auto Bonds=MakeShared<FJsonObject>();TArray<FString> BondKeys;Relation.Toward.GetKeys(BondKeys);BondKeys.Sort();for(const FString& K:BondKeys){auto B=MakeShared<FJsonObject>();B->SetNumberField(TEXT("favor"),Relation.Toward.FindChecked(K));B->SetStringField(TEXT("stage"),Relation.TowardStage.Contains(K)?Relation.TowardStage.FindChecked(K):FString(TEXT("stranger")));Bonds->SetObjectField(K,B);}
    if(BondKeys.Num())Object->SetObjectField(TEXT("toward"),Bonds);if(!Relation.Orientation.IsEmpty())Object->SetStringField(TEXT("orientation"),Relation.Orientation);}
   auto Money=MakeShared<FJsonObject>();Money->SetNumberField(TEXT("cash"),double(Relation.Cash));Money->SetNumberField(TEXT("bank"),double(Relation.Bank));Money->SetNumberField(TEXT("owes_player"),double(Relation.OwesPlayer));Money->SetNumberField(TEXT("debt"),double(Relation.Debt));Money->SetNumberField(TEXT("treat_balance"),Relation.TreatBalance);Money->SetNumberField(TEXT("perceived_wealth"),Relation.PerceivedWealth);Object->SetObjectField(TEXT("money"),Money);
   auto Needs=MakeShared<FJsonObject>();Needs->SetNumberField(TEXT("satiety"),Relation.Satiety);Needs->SetNumberField(TEXT("energy"),Relation.Energy);Needs->SetNumberField(TEXT("fun"),Relation.Fun);Needs->SetNumberField(TEXT("social"),Relation.Social);Needs->SetNumberField(TEXT("hygiene"),Relation.Hygiene);Needs->SetNumberField(TEXT("minute"),double(Relation.NeedsMinute));Object->SetObjectField(TEXT("needs"),Needs);
   auto Mood=MakeShared<FJsonObject>();Mood->SetStringField(TEXT("emotion"),Relation.Emotion);Mood->SetNumberField(TEXT("intensity"),Relation.EmotionIntensity);Mood->SetNumberField(TEXT("minute"),double(Relation.EmotionMinute));Object->SetObjectField(TEXT("mood"),Mood);}
  {TArray<TSharedPtr<FJsonValue>> Recent;for(int32 No:Relation.ListenRecent)Recent.Add(MakeShared<FJsonValueNumber>(No));Object->SetArrayField(TEXT("listen_recent"),Recent);}
  Object->SetStringField(TEXT("relation"), Relation.Stage);
  Object->SetNumberField(TEXT("talks_remaining"), Relation.TalksRemaining);
  Object->SetNumberField(TEXT("day"), Relation.Day);
  OKStreetWriteStringArray(Object, TEXT("talked"), Relation.Talked);
  OKStreetWriteStringArray(Object, TEXT("flags"), Relation.Flags);
  OKStreetWriteStringArray(Object, TEXT("committed"), Relation.Committed);
  OKStreetWriteStringArray(Object, TEXT("topics"), Relation.Topics);
  TArray<TSharedPtr<FJsonValue>> Cooldowns;
  for (int32 Cooldown = 0; Cooldown < Relation.CooldownIds.Num() && Cooldown < Relation.CooldownDays.Num(); ++Cooldown) {
   TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
   Entry->SetStringField(TEXT("id"), Relation.CooldownIds[Cooldown]);
   Entry->SetNumberField(TEXT("until_day"), Relation.CooldownDays[Cooldown]);
   Cooldowns.Add(MakeShared<FJsonValueObject>(Entry));
  }
  Object->SetArrayField(TEXT("cooldowns"), Cooldowns);
  Rows.Add(MakeShared<FJsonValueObject>(Object));
 }
 TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
 Root->SetNumberField(TEXT("schema"), OKStreetSocialSchema);
 {auto Calendar=MakeShared<FJsonObject>();Calendar->SetNumberField(TEXT("day"),Store.CalendarDay);Calendar->SetNumberField(TEXT("minute"),Store.CalendarMinute);
  if(Store.HasPlayerSpot){auto Spot=MakeShared<FJsonObject>();Spot->SetNumberField(TEXT("x"),Store.PlayerSpot.X);Spot->SetNumberField(TEXT("y"),Store.PlayerSpot.Y);Spot->SetNumberField(TEXT("z"),Store.PlayerSpot.Z);Spot->SetNumberField(TEXT("yaw"),Store.PlayerYaw);Spot->SetNumberField(TEXT("view"),Store.ViewYaw);Calendar->SetObjectField(TEXT("player"),Spot);}Root->SetObjectField(TEXT("calendar"),Calendar);}
 {TArray<TSharedPtr<FJsonValue>> ApptRows;for(const auto& A:Store.Appointments){auto O=MakeShared<FJsonObject>();O->SetNumberField(TEXT("id"),A.Id);O->SetStringField(TEXT("kind"),A.Kind);O->SetStringField(TEXT("heroine"),A.Heroine);O->SetStringField(TEXT("place"),A.Place);O->SetStringField(TEXT("label"),A.Label);O->SetNumberField(TEXT("start"),double(A.Start));O->SetNumberField(TEXT("grace"),A.Grace);if(A.Minutes>0)O->SetNumberField(TEXT("minutes"),A.Minutes);O->SetStringField(TEXT("status"),A.Status);if(!A.Payer.IsEmpty())O->SetStringField(TEXT("payer"),A.Payer);ApptRows.Add(MakeShared<FJsonValueObject>(O));}
  Root->SetArrayField(TEXT("appointments"),ApptRows);Root->SetNumberField(TEXT("next_appointment"),Store.NextAppointment);
  auto Black=MakeShared<FJsonObject>();TArray<FString> Keys;Store.Blacklist.GetKeys(Keys);Keys.Sort();for(const FString& K:Keys)Black->SetNumberField(K,Store.Blacklist[K]);Root->SetObjectField(TEXT("blacklist"),Black);}
 {auto Bonds=MakeShared<FJsonObject>();TArray<FString> Keys;Store.BondHeat.GetKeys(Keys);Keys.Sort();for(const FString& K:Keys)Bonds->SetNumberField(K,Store.BondHeat[K]);Root->SetObjectField(TEXT("bonds"),Bonds);
  TArray<TSharedPtr<FJsonValue>> RumorRows;for(const auto& R:Store.Rumors){auto O=MakeShared<FJsonObject>();O->SetNumberField(TEXT("id"),R.Id);O->SetStringField(TEXT("text"),R.Text);O->SetStringField(TEXT("about"),R.About);O->SetStringField(TEXT("kind"),R.Kind);O->SetNumberField(TEXT("heat"),R.Heat);O->SetNumberField(TEXT("confidence"),R.Confidence);O->SetNumberField(TEXT("day"),R.Day);OKStreetWriteStringArray(O,TEXT("known_by"),R.KnownBy);if(R.Heard.Num()){auto H=MakeShared<FJsonObject>();for(const auto& P:R.Heard)H->SetNumberField(P.Key,P.Value);O->SetObjectField(TEXT("heard"),H);}RumorRows.Add(MakeShared<FJsonValueObject>(O));}
  Root->SetArrayField(TEXT("rumors"),RumorRows);Root->SetNumberField(TEXT("next_rumor"),Store.NextRumor);}
 {TArray<TSharedPtr<FJsonValue>> EntrustRows;for(const auto& E:Store.Entrusts){auto O=MakeShared<FJsonObject>();O->SetNumberField(TEXT("id"),E.Id);O->SetStringField(TEXT("heroine"),E.Heroine);O->SetStringField(TEXT("kind"),E.Kind);O->SetNumberField(TEXT("principal"),double(E.Principal));O->SetNumberField(TEXT("returned"),double(E.Returned));O->SetNumberField(TEXT("day"),E.Day);O->SetNumberField(TEXT("due"),E.Due);O->SetStringField(TEXT("status"),E.Status);O->SetStringField(TEXT("split"),E.Split);O->SetBoolField(TEXT("overdue"),E.Overdue);O->SetBoolField(TEXT("jealous"),E.Jealous);EntrustRows.Add(MakeShared<FJsonValueObject>(O));}
  Root->SetArrayField(TEXT("entrusts"),EntrustRows);Root->SetNumberField(TEXT("next_entrust"),Store.NextEntrust);
  auto C=MakeShared<FJsonObject>();const auto& Co=Store.Company;C->SetStringField(TEXT("name"),Co.Name);C->SetStringField(TEXT("office"),Co.Office);C->SetNumberField(TEXT("founded"),Co.Founded);C->SetNumberField(TEXT("paid_month"),Co.PaidMonth);C->SetNumberField(TEXT("capital"),double(Co.Capital));C->SetNumberField(TEXT("last_profit"),double(Co.LastProfit));
  TArray<TSharedPtr<FJsonValue>> Staff;for(const auto& E:Co.Staff){auto O=MakeShared<FJsonObject>();O->SetStringField(TEXT("heroine"),E.Heroine);O->SetStringField(TEXT("role"),E.Role);O->SetNumberField(TEXT("pay"),double(E.Pay));O->SetBoolField(TEXT("title_public"),E.TitlePublic);O->SetBoolField(TEXT("exclusive"),E.Exclusive);O->SetNumberField(TEXT("since"),E.Since);O->SetNumberField(TEXT("missed"),E.Missed);Staff.Add(MakeShared<FJsonValueObject>(O));}
  C->SetArrayField(TEXT("staff"),Staff);C->SetObjectField(TEXT("estate"),OKStreetWriteEstate(Co.Estate));Root->SetObjectField(TEXT("company"),C);
  Root->SetObjectField(TEXT("personal"),OKStreetWritePersonal(Store.Personal));
  TArray<TSharedPtr<FJsonValue>> Heroes;for(const auto& H:Store.HeroCompanies)Heroes.Add(MakeShared<FJsonValueObject>(OKStreetWriteHeroCompany(H)));Root->SetArrayField(TEXT("hero_companies"),Heroes);}
 {auto Ac=MakeShared<FJsonObject>();Ac->SetObjectField(TEXT("player"),OKStreetAcademicJson(Store.PlayerAcademic));auto H=MakeShared<FJsonObject>();TArray<FString> Keys;Store.Academics.GetKeys(Keys);Keys.Sort();
  for(const FString& K:Keys)H->SetObjectField(K,OKStreetAcademicJson(Store.Academics[K]));Ac->SetObjectField(TEXT("heroines"),H);Root->SetObjectField(TEXT("academics"),Ac);}
 Root->SetObjectField(TEXT("banking"),OKStreetBankingJson(Store.Banking));
 {auto Jobs=MakeShared<FJsonObject>();OKStreetWriteStringArray(Jobs,TEXT("held"),Store.PlayerJobs);auto Rating=MakeShared<FJsonObject>();TArray<FString> Keys;Store.JobRating.GetKeys(Keys);Keys.Sort();for(const FString& K:Keys)Rating->SetNumberField(K,Store.JobRating[K]);Jobs->SetObjectField(TEXT("rating"),Rating);Root->SetObjectField(TEXT("jobs"),Jobs);}
 {auto Wallet=MakeShared<FJsonObject>();Wallet->SetNumberField(TEXT("cash"),double(Store.PlayerCash));Wallet->SetNumberField(TEXT("bank"),double(Store.PlayerBank));OKStreetWriteStringArray(Wallet,TEXT("inventory"),Store.Inventory);{TSharedPtr<FJsonObject> E;if(!Store.Ext.IsEmpty()&&FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Store.Ext),E)&&E.IsValid())Root->SetObjectField(TEXT("ext"),E);}Root->SetObjectField(TEXT("wallet"),Wallet);Root->SetNumberField(TEXT("feelings_model"),2);Root->SetStringField(TEXT("currency"),Store.Region==TEXT("TW")?TEXT("TWD"):Store.Region==TEXT("US")?TEXT("USD"):TEXT("CNY"));Root->SetStringField(TEXT("region"),Store.Region);}
 Root->SetBoolField(TEXT("talk_budget_is_kks"), false);
 Root->SetBoolField(TEXT("favor_cap_is_kks_stepup"), Store.FavorCapIsKksStepUp);
 if (Store.FavorCapIsKksStepUp) {
  TSharedRef<FJsonObject> Limits = MakeShared<FJsonObject>();
  Limits->SetNumberField(TEXT("0"), Store.StepUpStranger);
  Limits->SetNumberField(TEXT("1"), Store.StepUpFriend);
  Limits->SetNumberField(TEXT("2"), Store.StepUpLover);
  if (Store.StepUpBeyond != INDEX_NONE) Limits->SetNumberField(TEXT("3"), Store.StepUpBeyond);
  TSharedRef<FJsonObject> Step = MakeShared<FJsonObject>();
  Step->SetObjectField(TEXT("limits"), Limits);
  Root->SetObjectField(TEXT("stepup"), Step);
 }
 Root->SetArrayField(TEXT("relations"), Rows);
 {auto Inventory=MakeShared<FJsonObject>();Inventory->SetNumberField(TEXT("day"),Store.TopicDay);
  for(const auto& Pair:{TPair<FString,const TMap<int32,int32>*>(TEXT("stock"),&Store.TopicStock),TPair<FString,const TMap<int32,int32>*>(TEXT("claims"),&Store.TopicClaims)}){auto Values=MakeShared<FJsonObject>();TArray<int32> Keys;Pair.Value->GetKeys(Keys);Keys.Sort();for(int32 Key:Keys)Values->SetNumberField(FString::FromInt(Key),Pair.Value->FindChecked(Key));Inventory->SetObjectField(Pair.Key,Values);}Root->SetObjectField(TEXT("topic_inventory"),Inventory);
 }
 if (Store.Retargets.Num() > 0) {
  TArray<TSharedPtr<FJsonValue>> Traces;
  for (const FOKStreetRetargetTrace& Trace : Store.Retargets) {
   TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
   Entry->SetStringField(TEXT("old_id"), Trace.OldId);
   Entry->SetStringField(TEXT("new_id"), Trace.NewId);
   Entry->SetStringField(TEXT("request_id"), Trace.RequestId);
   Entry->SetStringField(TEXT("time"), Trace.Time);
   Traces.Add(MakeShared<FJsonValueObject>(Entry));
  }
  Root->SetArrayField(TEXT("identity_retargets"), Traces);
 }
 FString Out;
 FJsonSerializer::Serialize(Root, TJsonWriterFactory<>::Create(&Out));
 return Out;
}

inline bool OKStreetReadStringArray(const TSharedPtr<FJsonObject>& Object, const TCHAR* Name, TArray<FString>& Out);
inline bool OKStreetReadOptionalStringArray(const TSharedPtr<FJsonObject>& Object, const TCHAR* Name, TArray<FString>& Out) {
 if (!Object->HasField(Name)) return true;
 return OKStreetReadStringArray(Object, Name, Out);
}

inline bool OKStreetReadCooldowns(const TSharedPtr<FJsonObject>& Object, FOKStreetRelation& Relation) {
 if (!Object->HasField(TEXT("cooldowns"))) return true;
 const TArray<TSharedPtr<FJsonValue>>* Rows = nullptr;
 if (!Object->TryGetArrayField(TEXT("cooldowns"), Rows) || !Rows) return false;
 for (const TSharedPtr<FJsonValue>& Row : *Rows) {
  const TSharedPtr<FJsonObject> Cooldown = Row.IsValid() ? Row->AsObject() : nullptr;
  FString Id;
  int32 UntilDay = 0;
  if (!Cooldown.IsValid() || !Cooldown->TryGetStringField(TEXT("id"), Id) || Id.IsEmpty() || !OKStreetJsonInt(Cooldown->TryGetField(TEXT("until_day")), UntilDay) || UntilDay < 0) return false;
  const int32 Found = Relation.CooldownIds.IndexOfByKey(Id);
  if (Found != INDEX_NONE && Relation.CooldownDays.IsValidIndex(Found)) Relation.CooldownDays[Found] = UntilDay;
  else { Relation.CooldownIds.Add(Id); Relation.CooldownDays.Add(UntilDay); }
 }
 // Sorted by id, as the save writes them.
 TArray<int32> Order; for (int32 K = 0; K < Relation.CooldownIds.Num(); ++K) Order.Add(K);
 Order.Sort([&](int32 A, int32 B) { return Relation.CooldownIds[A] < Relation.CooldownIds[B]; });
 TArray<FString> Ids; TArray<int32> Days; for (const int32 K : Order) { Ids.Add(Relation.CooldownIds[K]); Days.Add(Relation.CooldownDays[K]); }
 Relation.CooldownIds = MoveTemp(Ids); Relation.CooldownDays = MoveTemp(Days);
 return true;
}

inline bool OKStreetReadStringArray(const TSharedPtr<FJsonObject>& Object, const TCHAR* Name, TArray<FString>& Out) {
 const TArray<TSharedPtr<FJsonValue>>* Rows = nullptr;
 if (!Object->TryGetArrayField(Name, Rows) || !Rows) return false;
 for (const TSharedPtr<FJsonValue>& Row : *Rows) {
  FString Text;
  if (!Row.IsValid() || !Row->TryGetString(Text)) return false;
  OKStreetAddToken(Out, Text);
 }
 return true;
}

inline EOKStreetLoad OKStreetParseSave(const FString& Json, FOKStreetStore& Store, FString& Error) {
 Store = FOKStreetStore();
 TSharedPtr<FJsonObject> Root;
 if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root) || !Root) { Error = TEXT("save_json_invalid"); return EOKStreetLoad::Invalid; }
 double Schema = 0.0;
 if (!Root->TryGetNumberField(TEXT("schema"), Schema) || int32(Schema) < OKStreetSocialOldestSchema || int32(Schema) > OKStreetSocialSchema) { Error = TEXT("save_schema_unsupported"); return EOKStreetLoad::Invalid; }
 {const TArray<TSharedPtr<FJsonValue>>* ApptRows=nullptr;
  if(Root->TryGetArrayField(TEXT("appointments"),ApptRows))for(const auto& R:*ApptRows){const auto O=R.IsValid()?R->AsObject():nullptr;if(!O)continue;FOKStreetAppointment A;double V=0;
   if(O->TryGetNumberField(TEXT("id"),V))A.Id=int32(V);O->TryGetStringField(TEXT("kind"),A.Kind);O->TryGetStringField(TEXT("heroine"),A.Heroine);O->TryGetStringField(TEXT("place"),A.Place);O->TryGetStringField(TEXT("label"),A.Label);
   if(O->TryGetNumberField(TEXT("start"),V))A.Start=int64(V);if(O->TryGetNumberField(TEXT("grace"),V))A.Grace=FMath::Clamp(int32(V),0,1440);if(O->TryGetNumberField(TEXT("minutes"),V))A.Minutes=FMath::Clamp(int32(V),0,1440);O->TryGetStringField(TEXT("status"),A.Status);O->TryGetStringField(TEXT("payer"),A.Payer);Store.Appointments.Add(A);}
  double Next=1;if(Root->TryGetNumberField(TEXT("next_appointment"),Next))Store.NextAppointment=FMath::Max(1,int32(Next));
  const TSharedPtr<FJsonObject>* Black=nullptr;if(Root->TryGetObjectField(TEXT("blacklist"),Black))for(const auto& P:(*Black)->Values){double V=0;if(P.Value.IsValid()&&P.Value->TryGetNumber(V))Store.Blacklist.Add(FString(*P.Key),int32(V));}}
 {const TSharedPtr<FJsonObject>* Bonds=nullptr;if(Root->TryGetObjectField(TEXT("bonds"),Bonds))for(const auto& P:(*Bonds)->Values){double V=0;if(P.Value.IsValid()&&P.Value->TryGetNumber(V)&&FMath::IsFinite(V))Store.BondHeat.Add(FString(*P.Key),FMath::Clamp(float(V),0.f,100.f));}
  const TArray<TSharedPtr<FJsonValue>>* RumorRows=nullptr;if(Root->TryGetArrayField(TEXT("rumors"),RumorRows))for(const auto& Row:*RumorRows){const auto O=Row.IsValid()?Row->AsObject():nullptr;if(!O)continue;FOKStreetRumor R;double V=0;
   if(O->TryGetNumberField(TEXT("id"),V))R.Id=int32(V);O->TryGetStringField(TEXT("text"),R.Text);O->TryGetStringField(TEXT("about"),R.About);O->TryGetStringField(TEXT("kind"),R.Kind);
   if(O->TryGetNumberField(TEXT("heat"),V))R.Heat=FMath::Clamp(float(V),0.f,100.f);if(O->TryGetNumberField(TEXT("confidence"),V))R.Confidence=FMath::Clamp(float(V),0.f,1.f);if(O->TryGetNumberField(TEXT("day"),V))R.Day=int32(V);
   const TArray<TSharedPtr<FJsonValue>>* Known=nullptr;if(O->TryGetArrayField(TEXT("known_by"),Known))for(const auto& K:*Known){FString S;if(K.IsValid()&&K->TryGetString(S))R.KnownBy.Add(S);}
   const TSharedPtr<FJsonObject>* Heard=nullptr;if(O->TryGetObjectField(TEXT("heard"),Heard))for(const auto& P:(*Heard)->Values){double C=0;if(P.Value.IsValid()&&P.Value->TryGetNumber(C)&&FMath::IsFinite(C))R.Heard.Add(FString(*P.Key),FMath::Clamp(float(C),0.f,1.f));}
   if(!R.Text.IsEmpty())Store.Rumors.Add(R);}
  double Next=1;if(Root->TryGetNumberField(TEXT("next_rumor"),Next))Store.NextRumor=FMath::Max(1,int32(Next));}
 {const TArray<TSharedPtr<FJsonValue>>* EntrustRows=nullptr;if(Root->TryGetArrayField(TEXT("entrusts"),EntrustRows))for(const auto& Row:*EntrustRows){const auto O=Row.IsValid()?Row->AsObject():nullptr;if(!O)continue;FOKStreetEntrust E;double V=0;
   if(O->TryGetNumberField(TEXT("id"),V))E.Id=int32(V);O->TryGetStringField(TEXT("heroine"),E.Heroine);O->TryGetStringField(TEXT("kind"),E.Kind);if(O->TryGetNumberField(TEXT("principal"),V)&&FMath::IsFinite(V))E.Principal=FMath::Max<int64>(0,int64(V));if(O->TryGetNumberField(TEXT("returned"),V)&&FMath::IsFinite(V))E.Returned=FMath::Max<int64>(0,int64(V));
   if(O->TryGetNumberField(TEXT("day"),V))E.Day=int32(V);if(O->TryGetNumberField(TEXT("due"),V))E.Due=int32(V);O->TryGetStringField(TEXT("status"),E.Status);O->TryGetStringField(TEXT("split"),E.Split);O->TryGetBoolField(TEXT("overdue"),E.Overdue);O->TryGetBoolField(TEXT("jealous"),E.Jealous);
   if(!E.Heroine.IsEmpty()&&E.Principal>0)Store.Entrusts.Add(E);}
  double Next=1;if(Root->TryGetNumberField(TEXT("next_entrust"),Next))Store.NextEntrust=FMath::Max(1,int32(Next));
  const TSharedPtr<FJsonObject>* C=nullptr;if(Root->TryGetObjectField(TEXT("company"),C)){auto& Co=Store.Company;double V=0;(*C)->TryGetStringField(TEXT("name"),Co.Name);(*C)->TryGetStringField(TEXT("office"),Co.Office);
   if((*C)->TryGetNumberField(TEXT("founded"),V))Co.Founded=int32(V);if((*C)->TryGetNumberField(TEXT("paid_month"),V))Co.PaidMonth=int32(V);if((*C)->TryGetNumberField(TEXT("capital"),V)&&FMath::IsFinite(V))Co.Capital=int64(V);if((*C)->TryGetNumberField(TEXT("last_profit"),V)&&FMath::IsFinite(V))Co.LastProfit=int64(V);
   const TArray<TSharedPtr<FJsonValue>>* Staff=nullptr;if((*C)->TryGetArrayField(TEXT("staff"),Staff))for(const auto& Row:*Staff){const auto O=Row.IsValid()?Row->AsObject():nullptr;if(!O)continue;FOKStreetEmployee E;O->TryGetStringField(TEXT("heroine"),E.Heroine);O->TryGetStringField(TEXT("role"),E.Role);
    if(O->TryGetNumberField(TEXT("pay"),V)&&FMath::IsFinite(V))E.Pay=FMath::Max<int64>(0,int64(V));O->TryGetBoolField(TEXT("title_public"),E.TitlePublic);O->TryGetBoolField(TEXT("exclusive"),E.Exclusive);if(O->TryGetNumberField(TEXT("since"),V))E.Since=int32(V);if(O->TryGetNumberField(TEXT("missed"),V))E.Missed=int32(V);if(!E.Heroine.IsEmpty())Co.Staff.Add(E);}
   const TSharedPtr<FJsonObject>* Estate=nullptr;if((*C)->TryGetObjectField(TEXT("estate"),Estate)){OKStreetReadEstate(*Estate,Co.Estate);if(!Root->HasField(TEXT("personal")))OKStreetReadPersonal(*Estate,Store.Personal);}
   // A save from before the company had its own account: the capital it was founded with is what is in it.
   if(Co.Founded>=0&&!Co.Estate.bHadCash){Co.Estate.Cash=Co.Capital;Co.Estate.Capital=Co.Capital;}}}
 {const TSharedPtr<FJsonObject>* Pe=nullptr;if(Root->TryGetObjectField(TEXT("personal"),Pe))OKStreetReadPersonal(*Pe,Store.Personal);}
 {const TArray<TSharedPtr<FJsonValue>>* Heroes=nullptr;if(Root->TryGetArrayField(TEXT("hero_companies"),Heroes))for(const auto& V:*Heroes){FOKStreetHeroCompany H;if(OKStreetReadHeroCompany(V->AsObject(),H))Store.HeroCompanies.Add(H);}}
 {const TSharedPtr<FJsonObject>* Bk=nullptr;if(Root->TryGetObjectField(TEXT("banking"),Bk))OKStreetReadBanking(*Bk,Store.Banking);}
 {const TSharedPtr<FJsonObject>* Ac=nullptr;if(Root->TryGetObjectField(TEXT("academics"),Ac)){const TSharedPtr<FJsonObject>* P=nullptr;if((*Ac)->TryGetObjectField(TEXT("player"),P))OKStreetReadAcademic(*P,Store.PlayerAcademic);
  const TSharedPtr<FJsonObject>* H=nullptr;if((*Ac)->TryGetObjectField(TEXT("heroines"),H))for(const auto& Pair:(*H)->Values){FOKStreetAcademic A;OKStreetReadAcademic(Pair.Value.IsValid()?Pair.Value->AsObject():nullptr,A);Store.Academics.Add(FString(*Pair.Key),A);}}}
 {const TSharedPtr<FJsonObject>* Jobs=nullptr;if(Root->TryGetObjectField(TEXT("jobs"),Jobs)){const TArray<TSharedPtr<FJsonValue>>* Held=nullptr;if((*Jobs)->TryGetArrayField(TEXT("held"),Held))for(const auto& V:*Held){FString J;if(V.IsValid()&&V->TryGetString(J))Store.PlayerJobs.Add(J);}
  const TSharedPtr<FJsonObject>* Rating=nullptr;if((*Jobs)->TryGetObjectField(TEXT("rating"),Rating))for(const auto& P:(*Rating)->Values){double V=0;if(P.Value.IsValid()&&P.Value->TryGetNumber(V))Store.JobRating.Add(FString(*P.Key),FMath::Clamp(int32(V),0,100));}}}
 if (Root->HasField(TEXT("wallet"))) {
  const TSharedPtr<FJsonObject>* Wallet = nullptr; double V = 0;
  if (!Root->TryGetObjectField(TEXT("wallet"), Wallet)) { Store = FOKStreetStore(); Error = TEXT("save_wallet_invalid"); return EOKStreetLoad::Invalid; }
  if ((*Wallet)->TryGetNumberField(TEXT("cash"), V) && FMath::IsFinite(V)) Store.PlayerCash = FMath::Max<int64>(0, int64(V));
  if ((*Wallet)->TryGetNumberField(TEXT("bank"), V) && FMath::IsFinite(V)) Store.PlayerBank = FMath::Max<int64>(0, int64(V));
  const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
  if ((*Wallet)->TryGetArrayField(TEXT("inventory"), Items)) for (const auto& I : *Items) { FString S; if (I.IsValid() && I->TryGetString(S)) Store.Inventory.Add(S); }
 }
 { const TSharedPtr<FJsonObject>* E = nullptr; if (Root->TryGetObjectField(TEXT("ext"), E) && E->IsValid()) { FString Out; auto W = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out); FJsonSerializer::Serialize(E->ToSharedRef(), W); Store.Ext = Out; } }
 if (Root->HasField(TEXT("calendar"))) {
  const TSharedPtr<FJsonObject>* Calendar = nullptr;
  if (!Root->TryGetObjectField(TEXT("calendar"), Calendar) || !OKStreetJsonInt((*Calendar)->TryGetField(TEXT("day")), Store.CalendarDay) || !OKStreetJsonInt((*Calendar)->TryGetField(TEXT("minute")), Store.CalendarMinute)
   || Store.CalendarDay < 0 || Store.CalendarDay > 100000 || Store.CalendarMinute < -1 || Store.CalendarMinute >= 24 * 60) { Store = FOKStreetStore(); Error = TEXT("save_calendar_invalid"); return EOKStreetLoad::Invalid; }
  const TSharedPtr<FJsonObject>* Spot = nullptr; double X = 0, Y = 0, Z = 0, SpotYaw = 0;
  if ((*Calendar)->TryGetObjectField(TEXT("player"), Spot) && (*Spot)->TryGetNumberField(TEXT("x"), X) && (*Spot)->TryGetNumberField(TEXT("y"), Y) && (*Spot)->TryGetNumberField(TEXT("z"), Z)
   && FMath::IsFinite(X) && FMath::IsFinite(Y) && FMath::IsFinite(Z) && FMath::Abs(X) < 1.e6 && FMath::Abs(Y) < 1.e6 && FMath::Abs(Z) < 1.e5) {
   (*Spot)->TryGetNumberField(TEXT("yaw"), SpotYaw);
   double View = 90; if ((*Spot)->TryGetNumberField(TEXT("view"), View) && FMath::IsFinite(View)) Store.ViewYaw = float(View);
   Store.HasPlayerSpot = true; Store.PlayerSpot = FVector(X, Y, Z); Store.PlayerYaw = FMath::IsFinite(SpotYaw) ? float(SpotYaw) : 0.f;
  }
 }
 const TArray<TSharedPtr<FJsonValue>>* Rows = nullptr;
 if (!Root->TryGetArrayField(TEXT("relations"), Rows) || !Rows) { Error = TEXT("save_relations_missing"); return EOKStreetLoad::Invalid; }
 for (const TSharedPtr<FJsonValue>& Row : *Rows) {
  const TSharedPtr<FJsonObject> Object = Row.IsValid() ? Row->AsObject() : nullptr;
  FOKStreetRelation Relation;
  double Favor = 0.0, Lewdness = 0.0, Talks = 0.0, Day = 0.0;
  if (!Object || !Object->TryGetStringField(TEXT("id"), Relation.Id) || Relation.Id.IsEmpty()) { Error = TEXT("save_relation_id_invalid"); return EOKStreetLoad::Invalid; }
  if (!Object->TryGetNumberField(TEXT("favor"), Favor) || !Object->TryGetNumberField(TEXT("lewdness"), Lewdness) || !Object->TryGetNumberField(TEXT("talks_remaining"), Talks) || !Object->TryGetNumberField(TEXT("day"), Day)) { Error = TEXT("save_relation_fields_invalid"); return EOKStreetLoad::Invalid; }
  if (!Object->TryGetStringField(TEXT("relation"), Relation.Stage) || !OKStreetIsApprovedStage(Relation.Stage)) { Error = TEXT("save_relation_stage_invalid"); return EOKStreetLoad::Invalid; }
  if (!FMath::IsFinite(Favor) || Favor < 0.0 || !FMath::IsFinite(Lewdness) || Talks < 0.0 || Talks != FMath::FloorToDouble(Talks)) { Error = TEXT("save_relation_values_invalid"); return EOKStreetLoad::Invalid; }
  Relation.Favor = float(Favor);
  Relation.Lewdness = FMath::Clamp(float(Lewdness), 0.f, OKStreetLewdnessCap);
  {double C=0;if(Object->TryGetNumberField(TEXT("chastity"),C)&&FMath::IsFinite(C))Relation.Chastity=FMath::Clamp(float(C),0.f,100.f);
   if(Object->TryGetNumberField(TEXT("stranger_cap"),C)&&FMath::IsFinite(C))Relation.StrangerCap=FMath::Clamp(int32(C),10,100);}
  Relation.TalksRemaining = GOKStreetTalkLimit ? int32(Talks) : FMath::Max(int32(Talks), OKStreetDefaultTalkBudget); // a save made with the limit on is not stuck at 0
  Relation.Day = int32(Day);
  if (!OKStreetReadStringArray(Object, TEXT("talked"), Relation.Talked) || !OKStreetReadStringArray(Object, TEXT("flags"), Relation.Flags) || !OKStreetReadStringArray(Object, TEXT("committed"), Relation.Committed) || !OKStreetReadOptionalStringArray(Object, TEXT("topics"), Relation.Topics) || !OKStreetReadCooldowns(Object, Relation)) { Error = TEXT("save_relation_arrays_invalid"); return EOKStreetLoad::Invalid; }
  {const TSharedPtr<FJsonObject>* Axes=nullptr;if(Object->HasField(TEXT("axes"))){if(!Object->TryGetObjectField(TEXT("axes"),Axes)){Error=TEXT("save_axes_invalid");return EOKStreetLoad::Invalid;}for(const auto& A:OKStreetAxisFields(Relation)){double V=0;if((*Axes)->TryGetNumberField(A.Key,V)){if(!FMath::IsFinite(V)||V<0||V>100){Error=TEXT("save_axes_invalid");return EOKStreetLoad::Invalid;}*A.Value=float(V);}}}
   const TSharedPtr<FJsonObject>* Toward=nullptr;if(Object->TryGetObjectField(TEXT("jealousy_toward"),Toward))for(const auto& P:(*Toward)->Values){double V=0;if(P.Value.IsValid()&&P.Value->TryGetNumber(V)&&FMath::IsFinite(V))Relation.JealousyToward.Add(FString(*P.Key),FMath::Clamp(float(V),0.f,100.f));}
   {const TSharedPtr<FJsonObject>* Bonds=nullptr;if(Object->TryGetObjectField(TEXT("toward"),Bonds))for(const auto& P:(*Bonds)->Values){const TSharedPtr<FJsonObject> B=P.Value.IsValid()?P.Value->AsObject():nullptr;double V=0;FString Stage;
     if(B&&B->TryGetNumberField(TEXT("favor"),V)&&FMath::IsFinite(V)){Relation.Toward.Add(FString(*P.Key),FMath::Clamp(float(V),-100.f,200.f));if(B->TryGetStringField(TEXT("stage"),Stage)&&(Stage==TEXT("friend")||Stage==TEXT("lover")))Relation.TowardStage.Add(FString(*P.Key),Stage);}}
    Object->TryGetStringField(TEXT("orientation"),Relation.Orientation);}
   const TSharedPtr<FJsonObject>* Money=nullptr;if(Object->TryGetObjectField(TEXT("money"),Money)){double V=0;if((*Money)->TryGetNumberField(TEXT("cash"),V))Relation.Cash=int64(V);if((*Money)->TryGetNumberField(TEXT("bank"),V))Relation.Bank=FMath::Max<int64>(0,int64(V));if((*Money)->TryGetNumberField(TEXT("owes_player"),V))Relation.OwesPlayer=FMath::Max<int64>(0,int64(V));if((*Money)->TryGetNumberField(TEXT("debt"),V))Relation.Debt=FMath::Max<int64>(0,int64(V));if((*Money)->TryGetNumberField(TEXT("treat_balance"),V))Relation.TreatBalance=int32(V);if((*Money)->TryGetNumberField(TEXT("perceived_wealth"),V)&&FMath::IsFinite(V))Relation.PerceivedWealth=FMath::Clamp(float(V),0.f,100.f);}
   const TSharedPtr<FJsonObject>* Needs=nullptr;if(Object->TryGetObjectField(TEXT("needs"),Needs)){double V=0;for(const auto& F:{TPair<const TCHAR*,float*>(TEXT("satiety"),&Relation.Satiety),TPair<const TCHAR*,float*>(TEXT("energy"),&Relation.Energy),TPair<const TCHAR*,float*>(TEXT("fun"),&Relation.Fun),TPair<const TCHAR*,float*>(TEXT("social"),&Relation.Social),TPair<const TCHAR*,float*>(TEXT("hygiene"),&Relation.Hygiene)})if((*Needs)->TryGetNumberField(F.Key,V)&&FMath::IsFinite(V))*F.Value=FMath::Clamp(float(V),0.f,100.f);if((*Needs)->TryGetNumberField(TEXT("minute"),V))Relation.NeedsMinute=int64(V);}
   const TSharedPtr<FJsonObject>* Mood=nullptr;if(Object->TryGetObjectField(TEXT("mood"),Mood)){(*Mood)->TryGetStringField(TEXT("emotion"),Relation.Emotion);double V=0;if((*Mood)->TryGetNumberField(TEXT("intensity"),V))Relation.EmotionIntensity=FMath::Clamp(float(V),0.f,1.f);if((*Mood)->TryGetNumberField(TEXT("minute"),V))Relation.EmotionMinute=int64(V);}}
  for(const auto& Field:{TPair<FString,float*>(TEXT("sex_desire"),&Relation.SexDesire),TPair<FString,float*>(TEXT("topic_points"),&Relation.TopicPoints)}){double Number=0;if(Object->HasField(Field.Key)&&(!Object->TryGetNumberField(Field.Key,Number)||!FMath::IsFinite(Number)||Number<0||Number>1000000)){Error=TEXT("save_topic_points_invalid");return EOKStreetLoad::Invalid;}*Field.Value=float(Number);}
  if(Object->HasField(TEXT("listen_recent"))){const TArray<TSharedPtr<FJsonValue>>* Values=nullptr;if(!Object->TryGetArrayField(TEXT("listen_recent"),Values)||Values->Num()>5){Error=TEXT("save_listen_recent_invalid");return EOKStreetLoad::Invalid;}for(const auto& Value:*Values){int32 No;if(!OKStreetJsonInt(Value,No)||No<0||No>65535){Error=TEXT("save_listen_recent_invalid");return EOKStreetLoad::Invalid;}Relation.ListenRecent.Add(No);}}
  Store.Relations.Add(MoveTemp(Relation));
 }
 if(Root->HasField(TEXT("topic_inventory"))){const TSharedPtr<FJsonObject>* Inventory=nullptr;
  if(!Root->TryGetObjectField(TEXT("topic_inventory"),Inventory)||!OKStreetJsonInt((*Inventory)->TryGetField(TEXT("day")),Store.TopicDay)||Store.TopicDay<1||Store.TopicDay>1000000){Error=TEXT("save_topic_inventory_invalid");return EOKStreetLoad::Invalid;}
  for(const auto& Pair:{TPair<FString,TMap<int32,int32>*>(TEXT("stock"),&Store.TopicStock),TPair<FString,TMap<int32,int32>*>(TEXT("claims"),&Store.TopicClaims)}){const TSharedPtr<FJsonObject>* Values=nullptr;if(!(*Inventory)->TryGetObjectField(Pair.Key,Values)||(*Values)->Values.Num()>1024){Error=TEXT("save_topic_inventory_invalid");return EOKStreetLoad::Invalid;}for(const auto& Entry:(*Values)->Values){int32 No,Count;if(!LexTryParseString(No,*Entry.Key)||FString(*Entry.Key)!=FString::FromInt(No)||No<0||No>65535||!OKStreetJsonInt(Entry.Value,Count)||Count<0||(Pair.Key==TEXT("stock")?Count>99:Count>Store.TopicDay)){Error=TEXT("save_topic_inventory_invalid");return EOKStreetLoad::Invalid;}Pair.Value->Add(No,Count);}}
 }
 // The step-up the save was played with (the rules read rules.json themselves and keep favors inside it).
 if (Root->HasField(TEXT("stepup"))) {
  const TSharedPtr<FJsonObject>* Step = nullptr; const TSharedPtr<FJsonObject>* Limits = nullptr; int32 A = 0, B = 0, C = 0, D = 0;
  if (!Root->TryGetObjectField(TEXT("stepup"), Step) || !(*Step)->TryGetObjectField(TEXT("limits"), Limits) || !OKStreetJsonInt((*Limits)->TryGetField(TEXT("0")), A) || !OKStreetJsonInt((*Limits)->TryGetField(TEXT("1")), B) || !OKStreetJsonInt((*Limits)->TryGetField(TEXT("2")), C) || A < 0 || B < 0 || C < 0) { Error = TEXT("stepup_limit_invalid"); return EOKStreetLoad::Invalid; }
  Store.FavorCapIsKksStepUp = true; Store.StepUpStranger = A; Store.StepUpFriend = B; Store.StepUpLover = C; Store.StepUpBeyond = OKStreetJsonInt((*Limits)->TryGetField(TEXT("3")), D) ? D : INDEX_NONE;
 }
 if (Root->HasField(TEXT("identity_retargets"))) {
  const TArray<TSharedPtr<FJsonValue>>* Traces = nullptr;
  if (!Root->TryGetArrayField(TEXT("identity_retargets"), Traces) || !Traces) { Store = FOKStreetStore(); Error = TEXT("save_identity_retargets_invalid"); return EOKStreetLoad::Invalid; }
  for (const TSharedPtr<FJsonValue>& Value : *Traces) {
   const TSharedPtr<FJsonObject> Object = Value.IsValid() ? Value->AsObject() : nullptr;
   if (!Object.IsValid()) { Store = FOKStreetStore(); Error = TEXT("save_identity_retargets_invalid"); return EOKStreetLoad::Invalid; }
   int32 Known = 0;
   for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Object->Values) {
    if (Pair.Key == TEXT("old_id") || Pair.Key == TEXT("new_id") || Pair.Key == TEXT("request_id") || Pair.Key == TEXT("time")) ++Known;
    else { Store = FOKStreetStore(); Error = TEXT("save_identity_retargets_invalid"); return EOKStreetLoad::Invalid; }
   }
   FOKStreetRetargetTrace Trace;
   if (Known != 4 || !Object->TryGetStringField(TEXT("old_id"), Trace.OldId) || !Object->TryGetStringField(TEXT("new_id"), Trace.NewId) || !Object->TryGetStringField(TEXT("request_id"), Trace.RequestId) || !Object->TryGetStringField(TEXT("time"), Trace.Time)) { Store = FOKStreetStore(); Error = TEXT("save_identity_retargets_invalid"); return EOKStreetLoad::Invalid; }
   if (!OKStreetIsProductionApngId(Trace.OldId) || !OKStreetIsProductionApngId(Trace.NewId) || Trace.OldId == Trace.NewId || !OKStreetIsRetargetRequestId(Trace.RequestId) || Trace.Time.IsEmpty()) { Store = FOKStreetStore(); Error = TEXT("save_identity_retargets_invalid"); return EOKStreetLoad::Invalid; }
   for (const FOKStreetRetargetTrace& Prior : Store.Retargets) if (Prior.RequestId == Trace.RequestId) { Store = FOKStreetStore(); Error = TEXT("save_identity_retarget_duplicate"); return EOKStreetLoad::Invalid; }
   Store.Retargets.Add(MoveTemp(Trace));
  }
 }
 // Saves from before 2026-10-11 hold NT$; the game now runs on RMB (the hero is Chinese): convert once at 4.7.
 {FString Region;if(Root->TryGetStringField(TEXT("region"),Region)&&(Region==TEXT("TW")||Region==TEXT("CN")||Region==TEXT("US")))Store.Region=Region;}
 // Before regions (no "region"): saves without CNY hold NT$ and are converted once.
 {FString Currency;if(!Root->HasField(TEXT("region"))&&(!Root->TryGetStringField(TEXT("currency"),Currency)||Currency!=TEXT("CNY"))){
  auto Rmb=[](int64 V){return V<=0?V:FMath::Max<int64>(1,int64(FMath::RoundToDouble(V/4.7)));};
  Store.PlayerCash=Rmb(Store.PlayerCash);Store.PlayerBank=Rmb(Store.PlayerBank);
  for(auto& R:Store.Relations){R.Cash=Rmb(R.Cash);R.Bank=Rmb(R.Bank);R.OwesPlayer=Rmb(R.OwesPlayer);}
  for(auto& E:Store.Entrusts){E.Principal=Rmb(E.Principal);E.Returned=Rmb(E.Returned);}
  Store.Company.Capital=Rmb(Store.Company.Capital);Store.Company.LastProfit=Store.Company.LastProfit>0?Rmb(Store.Company.LastProfit):-Rmb(-Store.Company.LastProfit);
  for(auto& E:Store.Company.Staff)E.Pay=Rmb(E.Pay);}}
 // Saves from before the stage bands (no feelings_model) could hold friends and lovers whose feelings had been pulled
 // toward 0 with the favor each new stage resets: once, each is lifted to the stage's floor. A save written since says
 // 2 and reads back unchanged.
 // (Save format migration, not a rule: the stage floors as they were when feelings_model 2 was introduced.)
 {double Model = 0; if (!Root->TryGetNumberField(TEXT("feelings_model"), Model) || Model < 2) for (auto& R : Store.Relations) {
  const float Low = R.Stage == TEXT("lover") ? 65.f : R.Stage == TEXT("friend") ? 35.f : 0.f;
  R.Trust = FMath::Max(R.Trust, Low * 0.8f); if (R.Stage == TEXT("lover")) R.Attraction = FMath::Max(R.Attraction, Low * 0.75f);
  R.Flags.Remove(TEXT("feelings_band")); }}
 Error.Reset();
 return EOKStreetLoad::Ok;
}

inline int32 OKStreetCountRelationId(const FOKStreetStore& Store, const FString& Id) {
 int32 Count = 0;
 for (const FOKStreetRelation& Relation : Store.Relations) if (Relation.Id == Id) ++Count;
 return Count;
}

inline bool OKStreetRetargetRelations(const FOKStreetStore& Store, const TArray<FOKStreetRetargetRequest>& Requests, const TArray<FString>& SelectedFemaleIds, const TArray<FString>& SelectedActorIds, const FString& TimeUtc, FOKStreetStore& Out, FOKStreetRetargetResult& Result) {
 Result = FOKStreetRetargetResult();
 Out = Store;
 if (Requests.Num() == 0) { Result.Applied = true; return true; }
 auto Fail = [&](const TCHAR* Reason) {
  Out = Store;
  Result = FOKStreetRetargetResult();
  Result.Reason = Reason;
  return false;
 };
 if (Requests.Num() > OKStreetMaxRetargetRequests) return Fail(TEXT("retarget_too_many"));
 TArray<FString> RequestIds, OldIds, NewIds;
 TArray<int32> Moves, Replays;
 for (int32 Index = 0; Index < Requests.Num(); ++Index) {
  const FOKStreetRetargetRequest& Request = Requests[Index];
  if (!OKStreetIsRetargetRequestId(Request.RequestId)) return Fail(TEXT("request_id_invalid"));
  if (!OKStreetIsProductionApngId(Request.OldId)) return Fail(TEXT("old_id_invalid"));
  if (!OKStreetIsProductionApngId(Request.NewId)) return Fail(TEXT("new_id_invalid"));
  if (Request.OldId == Request.NewId) return Fail(TEXT("retarget_same_id"));
  if (RequestIds.Contains(Request.RequestId)) return Fail(TEXT("retarget_duplicate_request"));
  if (OldIds.Contains(Request.OldId) || NewIds.Contains(Request.NewId) || OldIds.Contains(Request.NewId) || NewIds.Contains(Request.OldId)) return Fail(TEXT("retarget_batch_collision"));
  if (!SelectedFemaleIds.Contains(Request.NewId)) return Fail(TEXT("new_id_not_selected"));
  if (SelectedActorIds.Contains(Request.OldId)) return Fail(TEXT("old_id_still_selected"));
  const FOKStreetRetargetTrace* Prior = nullptr;
  int32 PriorCount = 0;
  for (const FOKStreetRetargetTrace& Trace : Store.Retargets) if (Trace.RequestId == Request.RequestId) { Prior = &Trace; ++PriorCount; }
  if (PriorCount > 1) return Fail(TEXT("retarget_replay_conflict"));
  if (Prior) {
   if (Prior->OldId != Request.OldId || Prior->NewId != Request.NewId) return Fail(TEXT("request_id_mismatch"));
   Replays.Add(Index);
  } else Moves.Add(Index);
  RequestIds.Add(Request.RequestId);
  OldIds.Add(Request.OldId);
  NewIds.Add(Request.NewId);
 }
 if (Moves.Num() > 0 && TimeUtc.IsEmpty()) return Fail(TEXT("retarget_time_missing"));
 for (int32 Index : Replays) {
  const FOKStreetRetargetRequest& Request = Requests[Index];
  if (OKStreetCountRelationId(Store, Request.OldId) != 0 || OKStreetCountRelationId(Store, Request.NewId) != 1) return Fail(TEXT("retarget_replay_conflict"));
 }
 for (int32 Index : Moves) {
  const FOKStreetRetargetRequest& Request = Requests[Index];
  if (OKStreetCountRelationId(Store, Request.OldId) == 0) return Fail(TEXT("unknown_old_id"));
  if (OKStreetCountRelationId(Store, Request.OldId) != 1) return Fail(TEXT("old_id_ambiguous"));
  if (OKStreetCountRelationId(Store, Request.NewId) != 0) return Fail(TEXT("destination_collision"));
 }
 FOKStreetStore Working = Store;
 for (int32 Index : Moves) {
  const FOKStreetRetargetRequest& Request = Requests[Index];
  int32 Found = INDEX_NONE;
  for (int32 Relation = 0; Relation < Working.Relations.Num(); ++Relation) if (Working.Relations[Relation].Id == Request.OldId) { Found = Relation; break; }
  if (Found == INDEX_NONE) return Fail(TEXT("unknown_old_id"));
  Working.Relations[Found].Id = Request.NewId;
  FOKStreetRetargetTrace Trace;
  Trace.RequestId = Request.RequestId;
  Trace.OldId = Request.OldId;
  Trace.NewId = Request.NewId;
  Trace.Time = TimeUtc;
  Working.Retargets.Add(MoveTemp(Trace));
 }
 Out = MoveTemp(Working);
 Result.Applied = true;
 Result.Moved = Moves.Num();
 Result.Replayed = Replays.Num();
 return true;
}

inline bool OKStreetAtomicSave(const FString& Path, const FString& Body, FString& Error) {
 IFileManager& Files = IFileManager::Get();
 if (!Files.MakeDirectory(*FPaths::GetPath(Path), true)) { Error = TEXT("save_directory_failed"); return false; }
 const FString Temp = Path + TEXT(".tmp");
 const FString Backup = Path + TEXT(".bak");
 if (!FFileHelper::SaveStringToFile(Body, *Temp, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)) { Error = TEXT("save_temp_failed"); return false; }
 if (Files.FileExists(*Path) && Files.Copy(*Backup, *Path, true, true) != COPY_OK) { Error = TEXT("save_backup_failed"); return false; }
 if (!Files.Move(*Path, *Temp, true, true)) { Error = TEXT("save_replace_failed"); return false; }
 Error.Reset();
 return true;
}

// Saves off the game thread: the newest content for each path is made and written, in order, by one background task (the
// temp file, the .bak copy and the rename stalled the street, several times a frame at worst). The content is a body, or
// a store snapshot serialized by the task (OKStreetAtomicSaveStoreAsync: the store's JSON took the game thread 4-6 ms each
// commit). OKStreetFlushSaves waits for what is queued: before a save file is copied into a slot, read back, or the game ends.
// Appends (OKStreetAppendQueued) wait in Appends, gathered per file, and are queued in Order as "+" and the path.
struct FOKStreetSaveQueue { FCriticalSection Lock; TMap<FString, TFunction<FString()>> Pending; TMap<FString, FString> Appends; TArray<FString> Order; bool Running = false; };
inline void OKStreetSaveQueueRun();
inline FOKStreetSaveQueue& OKStreetSaveQueue() { static FOKStreetSaveQueue Queue; return Queue; }
inline void OKStreetAtomicSaveQueued(const FString& Path, TFunction<FString()> Make) {
 auto& Q = OKStreetSaveQueue();
 {
  FScopeLock Locked(&Q.Lock);
  if (!Q.Pending.Contains(Path)) Q.Order.Add(Path);
  Q.Pending.Add(Path, MoveTemp(Make));
  if (Q.Running) return;
  Q.Running = true;
 }
 OKStreetSaveQueueRun();
}
// Lines added to the end of a file (her history log), in the order they came: appended there, the open-append-close of
// each line held the game thread 3-8 ms.
inline void OKStreetAppendQueued(const FString& Path, const FString& Text) {
 auto& Q = OKStreetSaveQueue();
 {
  FScopeLock Locked(&Q.Lock);
  const FString Key = TEXT("+") + Path;
  if (FString* Waiting = Q.Appends.Find(Path)) { *Waiting += Text; return; } // still queued: it goes along
  Q.Appends.Add(Path, Text); Q.Order.Add(Key);
  if (Q.Running) return;
  Q.Running = true;
 }
 OKStreetSaveQueueRun();
}
inline void OKStreetSaveQueueRun() {
 AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [] {
  auto& Q = OKStreetSaveQueue();
  for (;;) {
   FString Path;TFunction<FString()> Make;FString Appended;bool Append = false;
   {
    FScopeLock Locked(&Q.Lock);
    if (Q.Order.IsEmpty()) { Q.Running = false; return; }
    Path = Q.Order[0]; Q.Order.RemoveAt(0);
    if (Path.StartsWith(TEXT("+"))) { Path.RightChopInline(1); Appended = Q.Appends.FindAndRemoveChecked(Path); Append = true; }
    else Make = Q.Pending.FindAndRemoveChecked(Path);
   }
   if (Append) {
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
    if (!FFileHelper::SaveStringToFile(Appended, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM, &IFileManager::Get(), FILEWRITE_Append)) UE_LOG(LogTemp, Warning, TEXT("Street append %s failed"), *Path);
    continue;
   }
   const FString Body = Make();
   // A save is ~100 KB. One growing every write (a list appended to instead of replaced) is said once, before it stalls.
   {static int32 WarnedAt = 0; const int32 Kb = Body.Len() / 1024; if (Kb > 2048 && Kb > WarnedAt * 2) { WarnedAt = Kb; UE_LOG(LogTemp, Warning, TEXT("Street save %s is %d KB: something in it keeps growing"), *Path, Kb); }}
   FString Error;
   if (!OKStreetAtomicSave(Path, Body, Error)) UE_LOG(LogTemp, Warning, TEXT("Street save %s failed: %s"), *Path, *Error);
  }
 });
}
inline void OKStreetAtomicSaveAsync(const FString& Path, FString Body) {
 OKStreetAtomicSaveQueued(Path, [Body = MoveTemp(Body)]() { return Body; });
}
// The store as it is now, serialized and written by the save task: the game thread only copies it.
inline void OKStreetAtomicSaveStoreAsync(const FString& Path, const FOKStreetStore& Store) {
 TSharedRef<const FOKStreetStore, ESPMode::ThreadSafe> Snapshot = [&] { QUICK_SCOPE_CYCLE_COUNTER(STAT_Street_SaveSnapshot); return MakeShared<const FOKStreetStore, ESPMode::ThreadSafe>(Store); }();
 OKStreetAtomicSaveQueued(Path, [Snapshot]() { return OKStreetWriteSave(*Snapshot); });
}
inline void OKStreetFlushSaves() {
 auto& Q = OKStreetSaveQueue();
 for (int32 Tries = 0; Tries < 4000; ++Tries) {
  { FScopeLock Locked(&Q.Lock); if (!Q.Running && Q.Order.IsEmpty()) return; }
  FPlatformProcess::Sleep(0.002f);
 }
 UE_LOG(LogTemp, Warning, TEXT("Street saves still writing after 8 s"));
}

// Her reply's acts that are not activities (act field): the model sometimes writes them in the action field.
inline bool OKStreetReplyActOnly(const FString& Name) {
 // (leave is also an outing word: she walks off; end_talk / farewell / goodbye are handled where they are read.)
 return Name == TEXT("promise_treat") || Name == TEXT("checkout") || Name == TEXT("intimacy") || Name == TEXT("give_money") || Name == TEXT("lend");
}

inline EOKStreetLoad OKStreetLoadSaveFile(const FString& Path, FOKStreetStore& Store, FString& Error) {
 OKStreetFlushSaves();
 if (!IFileManager::Get().FileExists(*Path)) { Store = FOKStreetStore(); Error.Reset(); return EOKStreetLoad::Missing; }
 FString Json;
 if (!FFileHelper::LoadFileToString(Json, *Path)) { Error = TEXT("save_read_failed"); return EOKStreetLoad::Invalid; }
 return OKStreetParseSave(Json, Store, Error);
}

inline bool OKStreetReadManifestSocial(const FString& ManifestPath, const FString& Json, FOKStreetManifestSocial& Out) {
 TSharedPtr<FJsonObject> Root;
 if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root) || !Root) { Out.Error = TEXT("manifest_json_invalid"); return false; }
 const TArray<TSharedPtr<FJsonValue>>* Cards = nullptr;
 if (Root->TryGetArrayField(TEXT("cards"), Cards) && Cards) Out.CardCount = Cards->Num();
 FString Catalogue, SavePath, Report, TalkAction;
 if (Root->TryGetStringField(TEXT("dialogue_catalogue"), Catalogue)) Out.DialogueCatalogue = OKStreetResolveAgainst(ManifestPath, Catalogue);
 if (Root->TryGetStringField(TEXT("save_path"), SavePath)) Out.SavePath = FPaths::IsRelative(SavePath) ? FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / SavePath) : SavePath;
 if (Root->TryGetStringField(TEXT("social_report"), Report)) Out.SocialReport = OKStreetResolveAgainst(ManifestPath, Report);
 FString VoiceRoot;
 if (Root->TryGetStringField(TEXT("voice_root"), VoiceRoot)) Out.VoiceRoot = OKStreetResolveAgainst(ManifestPath, VoiceRoot);
 if (!Root->TryGetStringField(TEXT("talk_action"), TalkAction) || TalkAction.IsEmpty()) {
  const TArray<TSharedPtr<FJsonValue>>* Actions = nullptr;
  if (Root->TryGetArrayField(TEXT("actions"), Actions) && Actions && Actions->Num() > 0) {
   const TSharedPtr<FJsonObject> Action = (*Actions)[0].IsValid() ? (*Actions)[0]->AsObject() : nullptr;
   FString Curve;
   if (Action.IsValid() && Action->TryGetStringField(TEXT("curve"), Curve)) TalkAction = FPaths::GetCleanFilename(Curve);
  }
 }
 Out.TalkAction = TalkAction;
 Root->TryGetBoolField(TEXT("external_control"), Out.ExternalControl);
 double Budget = 0.0;
 if (Root->TryGetNumberField(TEXT("talk_budget"), Budget)) {
  if (!FMath::IsFinite(Budget) || Budget < 0.0 || Budget > 100.0 || Budget != FMath::FloorToDouble(Budget)) { Out.Error = TEXT("talk_budget_invalid"); return false; }
  Out.TalkBudget = int32(Budget);
  Out.TalkBudgetSpecified = true;
 }
 const TArray<TSharedPtr<FJsonValue>>* Characters = nullptr;
 if (Root->TryGetArrayField(TEXT("characters"), Characters) && Characters) {
  for (const TSharedPtr<FJsonValue>& Value : *Characters) {
   FOKStreetCharacterMeta Meta;
   const TSharedPtr<FJsonObject> Object = Value.IsValid() ? Value->AsObject() : nullptr;
   if (!Object) { Out.Error = TEXT("character_metadata_invalid"); return false; }
   Object->TryGetStringField(TEXT("id"), Meta.Id);
   Object->TryGetStringField(TEXT("name"), Meta.Name);
   if (OKStreetJsonInt(Object->TryGetField(TEXT("personality")), Meta.PersonalityId)) Meta.Personality = FString::FromInt(Meta.PersonalityId);
   else Object->TryGetStringField(TEXT("personality"), Meta.Personality);
   if (OKStreetJsonInt(Object->TryGetField(TEXT("sex")), Meta.SexIndex) && (Meta.SexIndex == 0 || Meta.SexIndex == 1)) {
    Meta.Sex = FString::FromInt(Meta.SexIndex);
    Meta.SexKind = Meta.SexIndex == 1 ? EOKStreetSex::Female : EOKStreetSex::Male;
   } else {
    Meta.SexIndex = INDEX_NONE;
    Object->TryGetStringField(TEXT("sex"), Meta.Sex);
    Meta.SexKind = EOKStreetSex::Unknown;
   }
   double VoiceRate = 0.0;
   if (Object->TryGetNumberField(TEXT("voice_rate"), VoiceRate) && FMath::IsFinite(VoiceRate)) Meta.VoiceRate = float(VoiceRate);
   if (!Object->TryGetStringField(TEXT("fingerprint"), Meta.Fingerprint)) Object->TryGetStringField(TEXT("content_fingerprint"), Meta.Fingerprint);
   if(Object->HasField(TEXT("interest_categories"))){const TArray<TSharedPtr<FJsonValue>>* Interests=nullptr;if(!Object->TryGetArrayField(TEXT("interest_categories"),Interests)||Interests->Num()>2){Out.Error=TEXT("interest_categories_invalid");return false;}for(const auto& InterestValue:*Interests){int32 Category;if(!OKStreetJsonInt(InterestValue,Category)||Category<-1||Category>4){Out.Error=TEXT("interest_categories_invalid");return false;}if(Category>=0)Meta.InterestCategories.AddUnique(Category);}}
   Out.Characters.Add(MoveTemp(Meta));
  }
 }
 if (Root->HasField(TEXT("identity_retargets"))) {
  const TArray<TSharedPtr<FJsonValue>>* Rows = nullptr;
  if (!Root->TryGetArrayField(TEXT("identity_retargets"), Rows) || !Rows) { Out.Error = TEXT("identity_retargets_invalid"); return false; }
  if (Rows->Num() > OKStreetMaxRetargetRequests) { Out.Error = TEXT("identity_retargets_too_many"); return false; }
  for (const TSharedPtr<FJsonValue>& Value : *Rows) {
   const TSharedPtr<FJsonObject> Object = Value.IsValid() ? Value->AsObject() : nullptr;
   if (!Object.IsValid()) { Out.Error = TEXT("identity_retargets_invalid"); return false; }
   int32 Known = 0;
   for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Object->Values) {
    if (Pair.Key == TEXT("request_id") || Pair.Key == TEXT("old_id") || Pair.Key == TEXT("new_id")) ++Known;
    else { Out.Error = TEXT("identity_retarget_field_unknown"); return false; }
   }
   FOKStreetRetargetRequest Request;
   if (Known != 3 || !Object->TryGetStringField(TEXT("request_id"), Request.RequestId) || !Object->TryGetStringField(TEXT("old_id"), Request.OldId) || !Object->TryGetStringField(TEXT("new_id"), Request.NewId)) { Out.Error = TEXT("identity_retargets_invalid"); return false; }
   if (!OKStreetIsRetargetRequestId(Request.RequestId)) { Out.Error = TEXT("identity_retarget_request_id_invalid"); return false; }
   if (!OKStreetIsProductionApngId(Request.OldId) || !OKStreetIsProductionApngId(Request.NewId) || Request.OldId == Request.NewId) { Out.Error = TEXT("identity_retarget_id_invalid"); return false; }
   for (const FOKStreetRetargetRequest& Prior : Out.IdentityRetargets) if (Prior.RequestId == Request.RequestId) { Out.Error = TEXT("identity_retarget_request_id_invalid"); return false; }
   Out.IdentityRetargets.Add(MoveTemp(Request));
  }
 }
 return true;
}

inline FVector OKStreetMaleSpawn() { return FVector(0.f, -700.f, 0.f); }

inline void OKStreetAddPoint(TArray<FOKStreetPointData>& Points, float X, float Y, EOKStreetPoint Kind) {
 FOKStreetPointData Point;
 Point.Position = FVector(X, Y, 0.f);
 Point.Kind = Kind;
 Points.Add(Point);
}

inline void OKStreetWaypoints(TArray<FOKStreetPointData>& Points) {
 Points.Reset();
 const float Ring[16][2] = {{-1600,-1400},{-800,-1400},{0,-1400},{800,-1400},{1600,-1400},{1600,-700},{1600,0},{1600,700},{1600,1400},{800,1400},{0,1400},{-800,1400},{-1600,1400},{-1600,700},{-1600,0},{-1600,-700}};
 for (const float* P : Ring) OKStreetAddPoint(Points, P[0], P[1], EOKStreetPoint::Walk);
 OKStreetAddPoint(Points, 2300, 50, EOKStreetPoint::Walk);
 OKStreetAddPoint(Points, 2680, 50, EOKStreetPoint::Look);
 OKStreetAddPoint(Points, 0, -1750, EOKStreetPoint::Walk);
 OKStreetAddPoint(Points, -800, -1750, EOKStreetPoint::Walk);
 OKStreetAddPoint(Points, -1500, -1750, EOKStreetPoint::Seat);
 OKStreetAddPoint(Points, 0, 500, EOKStreetPoint::Rest);
 OKStreetAddPoint(Points, -450, 720, EOKStreetPoint::Seat);
 OKStreetAddPoint(Points, 450, 720, EOKStreetPoint::Seat);
 OKStreetAddPoint(Points, 0, 1800, EOKStreetPoint::Walk);
 OKStreetAddPoint(Points, 0, 2350, EOKStreetPoint::Look);
}

inline void OKStreetRoutes(TArray<TArray<int32>>& Routes) {
 Routes.Reset();
 Routes.Add({0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15});
 Routes.Add({0,15,14,13,12,11,10,9,8,7,6,5,4,3,2,1});
 Routes.Add({6,16,17,16,6,7,8,21,23,21,9,10,11,12,13,14,15,0,1,2,3,4,5});
 Routes.Add({2,18,19,20,19,18,2,3,4,5,6,7,8,9,24,25,24,9,10,11,12,13,14,15,0,1});
}

inline float OKStreetDwell(EOKStreetPoint Kind) {
 if (Kind == EOKStreetPoint::Look) return 1.2f;
 if (Kind == EOKStreetPoint::Rest) return 2.5f;
 if (Kind == EOKStreetPoint::Seat) return 3.5f;
 return 0.f;
}

inline EOKStreetMove OKStreetDwellMode(EOKStreetPoint Kind) {
 if (Kind == EOKStreetPoint::Look) return EOKStreetMove::Looking;
 if (Kind == EOKStreetPoint::Walk) return EOKStreetMove::Waiting;
 return EOKStreetMove::Resting;
}

// One walker per heroine, slots 1..Females.
inline void OKStreetMakeAgents(TArray<FOKStreetAgentSim>& Agents, int32 Females = OKStreetFemaleCount) {
 TArray<FOKStreetPointData> Points;
 TArray<TArray<int32>> Routes;
 OKStreetWaypoints(Points);
 OKStreetRoutes(Routes);
 Agents.Reset();
 for (int32 Slot = 1; Slot <= FMath::Min(Females, OKStreetFemaleCount); ++Slot) {
  FOKStreetAgentSim Agent;
  Agent.Slot = Slot;
  Agent.Route = (Slot - 1) % Routes.Num();
  const TArray<int32>& Route = Routes[Agent.Route];
  const int32 Start = (Slot * 3) % Route.Num();
  Agent.Cursor = (Start + 1) % Route.Num();
  Agent.Position = Points[Route[Start]].Position;
  Agent.Mode = EOKStreetMove::Walking;
  Agents.Add(Agent);
 }
}

inline void OKStreetProps(TArray<FOKStreetProp>& Props) {
 Props.Reset();
 auto Add = [&](EOKStreetPropKind Kind, EOKStreetMesh Mesh, EOKStreetMaterial Material, FVector Center, FVector Size, float Yaw = 0.f) {
  FOKStreetProp Prop;
  Prop.Kind = Kind;
  Prop.Mesh = Mesh;
  Prop.Material = Material;
  Prop.Center = Center;
  Prop.Size = Size;
  Prop.Yaw = Yaw;
  Props.Add(Prop);
 };
 Add(EOKStreetPropKind::Ground, EOKStreetMesh::Cube, EOKStreetMaterial::Checker, FVector(0, 0, -20), FVector(6400, 5600, 40));
 Add(EOKStreetPropKind::Decor, EOKStreetMesh::Cube, EOKStreetMaterial::Plaza, FVector(0, 500, -3.9f), FVector(1500, 1100, 8));
 Add(EOKStreetPropKind::Block, EOKStreetMesh::Cube, EOKStreetMaterial::Terracotta, FVector(-2500, -150, 210), FVector(900, 1600, 420));
 Add(EOKStreetPropKind::Block, EOKStreetMesh::Cube, EOKStreetMaterial::Cream, FVector(2500, -700, 225), FVector(900, 1000, 450));
 Add(EOKStreetPropKind::Block, EOKStreetMesh::Cube, EOKStreetMaterial::Terracotta, FVector(2500, 900, 240), FVector(900, 1100, 480));
 Add(EOKStreetPropKind::Block, EOKStreetMesh::Cube, EOKStreetMaterial::Cream, FVector(600, -2300, 200), FVector(1400, 700, 400));
 Add(EOKStreetPropKind::Block, EOKStreetMesh::Cube, EOKStreetMaterial::Hedge, FVector(-900, 2100, 80), FVector(1200, 80, 160));
 Add(EOKStreetPropKind::Block, EOKStreetMesh::Cube, EOKStreetMaterial::Hedge, FVector(900, 2100, 80), FVector(1200, 80, 160));
 Add(EOKStreetPropKind::Block, EOKStreetMesh::Cube, EOKStreetMaterial::Wood, FVector(-1860, -450, 24), FVector(120, 48, 48));
 Add(EOKStreetPropKind::Block, EOKStreetMesh::Cube, EOKStreetMaterial::Wood, FVector(-1860, 150, 24), FVector(120, 48, 48));
 Add(EOKStreetPropKind::Block, EOKStreetMesh::Cube, EOKStreetMaterial::Wood, FVector(0, 1000, 24), FVector(140, 48, 48));
 Add(EOKStreetPropKind::Block, EOKStreetMesh::Cube, EOKStreetMaterial::Wood, FVector(400, -1050, 24), FVector(120, 48, 48));
 Add(EOKStreetPropKind::Decor, EOKStreetMesh::Cube, EOKStreetMaterial::Glass, FVector(-2042, -450, 230), FVector(8, 220, 150));
 Add(EOKStreetPropKind::Decor, EOKStreetMesh::Cube, EOKStreetMaterial::Glass, FVector(-2042, 180, 230), FVector(8, 220, 150));
 Add(EOKStreetPropKind::Decor, EOKStreetMesh::Cube, EOKStreetMaterial::Glass, FVector(2042, -700, 240), FVector(8, 260, 160));
 Add(EOKStreetPropKind::Decor, EOKStreetMesh::Cube, EOKStreetMaterial::Glass, FVector(2042, 900, 250), FVector(8, 280, 170));
 Add(EOKStreetPropKind::Decor, EOKStreetMesh::Cube, EOKStreetMaterial::Glass, FVector(600, -1946, 220), FVector(280, 8, 140));
 Add(EOKStreetPropKind::Decor, EOKStreetMesh::Cube, EOKStreetMaterial::Stripe, FVector(-1910, -150, 300), FVector(160, 1500, 14));
 Add(EOKStreetPropKind::Decor, EOKStreetMesh::Cube, EOKStreetMaterial::Stripe, FVector(1910, -700, 310), FVector(160, 900, 14));
 Add(EOKStreetPropKind::Decor, EOKStreetMesh::Cube, EOKStreetMaterial::Stone, FVector(-2048, -150, 390), FVector(12, 700, 36));
 Add(EOKStreetPropKind::Decor, EOKStreetMesh::Cube, EOKStreetMaterial::Stone, FVector(2048, 900, 430), FVector(12, 500, 36));
 Add(EOKStreetPropKind::Decor, EOKStreetMesh::Cube, EOKStreetMaterial::Leaf, FVector(-2100, -900, 28), FVector(160, 40, 28));
 Add(EOKStreetPropKind::Decor, EOKStreetMesh::Cube, EOKStreetMaterial::Leaf, FVector(400, -1900, 28), FVector(180, 40, 28));
 auto Trunk = [&](FVector At) {
  Add(EOKStreetPropKind::Trunk, EOKStreetMesh::Cylinder, EOKStreetMaterial::Bark, At + FVector(0, 0, 70), FVector(28, 28, 140));
  Add(EOKStreetPropKind::Canopy, EOKStreetMesh::Sphere, EOKStreetMaterial::Leaf, At + FVector(0, 0, 190), FVector(220, 220, 160));
 };
 Trunk(FVector(-500, 2480, 0));
 Trunk(FVector(500, 2480, 0));
 Trunk(FVector(0, 2620, 0));
 Trunk(FVector(-3100, 1100, 0));
 Trunk(FVector(3100, -1700, 0));
 auto Lamp = [&](FVector At) {
  Add(EOKStreetPropKind::Lamp, EOKStreetMesh::Cylinder, EOKStreetMaterial::Stone, At + FVector(0, 0, 160), FVector(16, 16, 320));
  Add(EOKStreetPropKind::Decor, EOKStreetMesh::Cube, EOKStreetMaterial::Lamp, At + FVector(0, 0, 330), FVector(36, 36, 18));
  Add(EOKStreetPropKind::Glow, EOKStreetMesh::Sphere, EOKStreetMaterial::Lamp, At + FVector(0, 0, 340), FVector(1));
 };
 Lamp(FVector(800, -1680, 0));
 Lamp(FVector(-800, -1900, 0));
 Lamp(FVector(1880, 700, 0));
 Lamp(FVector(1880, -700, 0));
 Lamp(FVector(-1880, 700, 0));
 Lamp(FVector(-1880, -700, 0));
 Lamp(FVector(-700, 1680, 0));
 Lamp(FVector(700, 1680, 0));
}

inline bool OKStreetSegmentHitsAabb(const FVector2D& A, const FVector2D& B, FVector2D Min, FVector2D Max) {
 const FVector2D D = B - A;
 float T0 = 0.f, T1 = 1.f;
 auto Clip = [&](float P, float Q) {
  if (FMath::Abs(P) < 1.e-6f) return Q >= 0.f;
  const float T = Q / P;
  if (P < 0.f) { if (T > T1) return false; if (T > T0) T0 = T; }
  else { if (T < T0) return false; if (T < T1) T1 = T; }
  return true;
 };
 if (!Clip(-D.X, A.X - Min.X) || !Clip(D.X, Max.X - A.X) || !Clip(-D.Y, A.Y - Min.Y) || !Clip(D.Y, Max.Y - A.Y)) return false;
 return T0 <= T1;
}

inline float OKStreetDistanceToSegment(const FVector2D& P, const FVector2D& A, const FVector2D& B) {
 const FVector2D D = B - A;
 const float Length2 = D.SizeSquared();
 const float T = Length2 < 1.e-4f ? 0.f : FMath::Clamp(FVector2D::DotProduct(P - A, D) / Length2, 0.f, 1.f);
 return FVector2D::Distance(P, A + D * T);
}

// Solid parts of buildings spawned at runtime (street places), filled by the scene. 3D: a part only blocks a
// point standing at a height it spans, so the walls and furniture of one floor do not block the floors above.
inline TArray<FBox>& OKStreetRuntimeBlocks() { static TArray<FBox> Blocks; return Blocks; }

// The street's built-in props (OKStreetProps), made once: the point tests below ran it on every call.
inline const TArray<FOKStreetProp>& OKStreetPropsOnce() { static const TArray<FOKStreetProp> Props = [] { TArray<FOKStreetProp> P; OKStreetProps(P); return P; }(); return Props; }
// The runtime blocks bucketed on a 5 m grid, so a point test looks at the blocks near it, not all of them (a route
// search tests thousands of points and each scanned every block). Rebuilt when the blocks change, which is checked once a
// frame (they are edited in place: furniture cleared from an elevator, a company's lot built).
struct FOKStreetBlockIndex { uint64 Frame = MAX_uint64; uint32 Crc = 0; int32 Num = -1; FBox2D Bounds = FBox2D(ForceInit); TMap<FIntPoint, TArray<int32>> Buckets; };
inline constexpr float OKStreetBlockBucket = 500.f;
inline const FOKStreetBlockIndex& OKStreetBlocksIndexed() {
 static FOKStreetBlockIndex Index;
 const TArray<FBox>& Blocks = OKStreetRuntimeBlocks();
 if (Index.Frame == GFrameCounter && Index.Num == Blocks.Num()) return Index;
 Index.Frame = GFrameCounter;
 const uint32 Crc = FCrc::MemCrc32(Blocks.GetData(), Blocks.Num() * sizeof(FBox));
 if (Crc == Index.Crc && Index.Num == Blocks.Num()) return Index;
 QUICK_SCOPE_CYCLE_COUNTER(STAT_Street_BlockIndexRebuild);
 Index.Crc = Crc; Index.Num = Blocks.Num(); Index.Buckets.Reset(); Index.Bounds = FBox2D(ForceInit);
 for (int32 I = 0; I < Blocks.Num(); ++I) {
  const FBox& B = Blocks[I];
  Index.Bounds += FVector2D(B.Min.X, B.Min.Y); Index.Bounds += FVector2D(B.Max.X, B.Max.Y);
  for (int32 X = FMath::FloorToInt32(B.Min.X / OKStreetBlockBucket); X <= FMath::FloorToInt32(B.Max.X / OKStreetBlockBucket); ++X)
   for (int32 Y = FMath::FloorToInt32(B.Min.Y / OKStreetBlockBucket); Y <= FMath::FloorToInt32(B.Max.Y / OKStreetBlockBucket); ++Y)
    Index.Buckets.FindOrAdd(FIntPoint(X, Y)).Add(I);
 }
 return Index;
}
// Calls Visit(block) for every runtime block in a bucket overlapping the square of half size R around P (a block may
// come more than once); Visit returns true to stop.
template <typename FVisit> inline bool OKStreetVisitBlocksNear(const FVector2D& P, float R, FVisit&& Visit) {
 const FOKStreetBlockIndex& Index = OKStreetBlocksIndexed();
 const TArray<FBox>& Blocks = OKStreetRuntimeBlocks();
 for (int32 X = FMath::FloorToInt32((P.X - R) / OKStreetBlockBucket); X <= FMath::FloorToInt32((P.X + R) / OKStreetBlockBucket); ++X)
  for (int32 Y = FMath::FloorToInt32((P.Y - R) / OKStreetBlockBucket); Y <= FMath::FloorToInt32((P.Y + R) / OKStreetBlockBucket); ++Y)
   if (const TArray<int32>* Bucket = Index.Buckets.Find(FIntPoint(X, Y)))
    for (const int32 I : *Bucket) if (Visit(Blocks[I])) return true;
 return false;
}

// The built-in solid props (blocks, trunks, lamps) on the same 5 m buckets, built once (they never change): a route
// search tests thousands of new cells, and each test went through every prop of the city (~5 ms a long route).
struct FOKStreetPropIndex { TMap<FIntPoint, TArray<int32>> Buckets; };
inline const FOKStreetPropIndex& OKStreetPropsIndexed() {
 static const FOKStreetPropIndex Index = [] {
  QUICK_SCOPE_CYCLE_COUNTER(STAT_Street_PropIndexBuild);
  FOKStreetPropIndex Out; const TArray<FOKStreetProp>& Props = OKStreetPropsOnce();
  for (int32 I = 0; I < Props.Num(); ++I) {
   const FOKStreetProp& Prop = Props[I];
   if (Prop.Kind != EOKStreetPropKind::Block && Prop.Kind != EOKStreetPropKind::Trunk && Prop.Kind != EOKStreetPropKind::Lamp) continue;
   const FVector2D Half = Prop.Kind == EOKStreetPropKind::Block ? FVector2D(Prop.Size.X * 0.5f, Prop.Size.Y * 0.5f) : FVector2D(FMath::Max(Prop.Size.X, Prop.Size.Y) * 0.5f);
   for (int32 X = FMath::FloorToInt32((Prop.Center.X - Half.X) / OKStreetBlockBucket); X <= FMath::FloorToInt32((Prop.Center.X + Half.X) / OKStreetBlockBucket); ++X)
    for (int32 Y = FMath::FloorToInt32((Prop.Center.Y - Half.Y) / OKStreetBlockBucket); Y <= FMath::FloorToInt32((Prop.Center.Y + Half.Y) / OKStreetBlockBucket); ++Y)
     Out.Buckets.FindOrAdd(FIntPoint(X, Y)).Add(I);
  }
  return Out;
 }();
 return Index;
}
// Calls Visit(prop) for every solid built-in prop whose footprint may come within R of P (a prop may come more than
// once); Visit returns true to stop.
template <typename FVisit> inline bool OKStreetVisitPropsNear(const FVector2D& P, float R, FVisit&& Visit) {
 const FOKStreetPropIndex& Index = OKStreetPropsIndexed(); const TArray<FOKStreetProp>& Props = OKStreetPropsOnce();
 for (int32 X = FMath::FloorToInt32((P.X - R) / OKStreetBlockBucket); X <= FMath::FloorToInt32((P.X + R) / OKStreetBlockBucket); ++X)
  for (int32 Y = FMath::FloorToInt32((P.Y - R) / OKStreetBlockBucket); Y <= FMath::FloorToInt32((P.Y + R) / OKStreetBlockBucket); ++Y)
   if (const TArray<int32>* Bucket = Index.Buckets.Find(FIntPoint(X, Y)))
    for (const int32 I : *Bucket) if (Visit(Props[I])) return true;
 return false;
}

inline bool OKStreetBlockedPoint(const FVector& Point, float Extra = OKStreetBodyClearance) {
 const FVector2D P(Point.X, Point.Y);
 if (OKStreetVisitPropsNear(P, FMath::Max(Extra, 0.f), [&](const FOKStreetProp& Prop) {
  if (Prop.Kind == EOKStreetPropKind::Block) {
   const FVector2D Min(Prop.Center.X - Prop.Size.X * 0.5f - Extra, Prop.Center.Y - Prop.Size.Y * 0.5f - Extra);
   const FVector2D Max(Prop.Center.X + Prop.Size.X * 0.5f + Extra, Prop.Center.Y + Prop.Size.Y * 0.5f + Extra);
   if (P.X >= Min.X && P.X <= Max.X && P.Y >= Min.Y && P.Y <= Max.Y) return true;
  } else {
   const float Radius = FMath::Max(Prop.Size.X, Prop.Size.Y) * 0.5f + Extra;
   if (FVector2D::Distance(P, FVector2D(Prop.Center.X, Prop.Center.Y)) <= Radius) return true;
  }
  return false; }))
  return true;
 return OKStreetVisitBlocksNear(P, FMath::Max(Extra, 0.f), [&](const FBox& B) {
  return Point.Z >= B.Min.Z - 60.f && Point.Z <= B.Max.Z - 20.f && P.X >= B.Min.X - Extra && P.X <= B.Max.X + Extra && P.Y >= B.Min.Y - Extra && P.Y <= B.Max.Y + Extra; });
}

// How far the point is from the nearest street obstacle that OKStreetBlockedPoint knows about (negative inside one).
// OKStreetBlockedPoint(P, Extra) == (OKStreetObstacleGap(P) <= Extra) for the same obstacles.
// Enough: the caller only compares the gap with this much, so the search stops there (a value over Enough means "more
// than Enough", not the exact gap). Without it, an open spot widened the search to the whole city: 0.1-1 ms a call.
inline float OKStreetObstacleGap(const FVector& Point, float Enough = TNumericLimits<float>::Max()) {
 const TArray<FOKStreetProp>& Props = OKStreetPropsOnce();
 const FVector2D P(Point.X, Point.Y);
 auto BoxGap = [&P](const FVector2D& Min, const FVector2D& Max) {
  const float DX = FMath::Max3(float(Min.X - P.X), 0.f, float(P.X - Max.X)), DY = FMath::Max3(float(Min.Y - P.Y), 0.f, float(P.Y - Max.Y));
  if (DX > 0.f || DY > 0.f) return FMath::Sqrt(DX * DX + DY * DY);
  return -float(FMath::Min(FMath::Min(P.X - Min.X, Max.X - P.X), FMath::Min(P.Y - Min.Y, Max.Y - P.Y)));
 };
 float Gap = TNumericLimits<float>::Max();
 auto PropGap = [&](const FOKStreetProp& Prop) {
  if (Prop.Kind == EOKStreetPropKind::Block) Gap = FMath::Min(Gap, BoxGap(FVector2D(Prop.Center.X - Prop.Size.X * 0.5f, Prop.Center.Y - Prop.Size.Y * 0.5f), FVector2D(Prop.Center.X + Prop.Size.X * 0.5f, Prop.Center.Y + Prop.Size.Y * 0.5f)));
  else if (Prop.Kind == EOKStreetPropKind::Trunk || Prop.Kind == EOKStreetPropKind::Lamp) Gap = FMath::Min(Gap, float(FVector2D::Distance(P, FVector2D(Prop.Center.X, Prop.Center.Y))) - FMath::Max(Prop.Size.X, Prop.Size.Y) * 0.5f);
  return false;
 };
 // Bounded: only the props within Enough can bring the gap under it. Unbounded: all of them.
 if (Enough < TNumericLimits<float>::Max()) OKStreetVisitPropsNear(P, Enough, PropGap);
 else for (const FOKStreetProp& Prop : Props) PropGap(Prop);
 // The runtime blocks in widening squares around the point: a block outside a square of half size R is farther than R,
 // so once the nearest found is within R it is the nearest of all (the whole set when R covers every block).
 const FOKStreetBlockIndex& Index = OKStreetBlocksIndexed();
 if (Index.Num > 0) {
  const float Reach = FMath::Max(Index.Bounds.GetExtent().GetMax() * 2.f + FVector2D::Distance(P, Index.Bounds.GetCenter()), OKStreetBlockBucket);
  for (float R = OKStreetBlockBucket;; R *= 2.f) {
   float Near = TNumericLimits<float>::Max();
   OKStreetVisitBlocksNear(P, R, [&](const FBox& B) { if (Point.Z >= B.Min.Z - 60.f && Point.Z <= B.Max.Z - 20.f) Near = FMath::Min(Near, BoxGap(FVector2D(B.Min.X, B.Min.Y), FVector2D(B.Max.X, B.Max.Y))); return false; });
   if (Near <= R || R >= Reach || R >= Enough) { Gap = FMath::Min(Gap, Near); break; }
  }
 }
 return Gap;
}

inline void OKStreetLayoutProblems(TArray<FString>& Problems) {
 Problems.Reset();
 TArray<FOKStreetPointData> Points;
 TArray<TArray<int32>> Routes;
 TArray<FOKStreetProp> Props;
 OKStreetWaypoints(Points);
 OKStreetRoutes(Routes);
 OKStreetProps(Props);
 if (Points.Num() < 8 || Routes.Num() != 4) Problems.Add(TEXT("route_graph_incomplete"));
 for (int32 PointIndex = 0; PointIndex < Points.Num(); ++PointIndex) if (Points[PointIndex].Position.Z != 0.f || OKStreetBlockedPoint(Points[PointIndex].Position)) Problems.Add(FString::Printf(TEXT("waypoint_blocked_%d"), PointIndex));
 auto SegmentBlocked = [&](const FVector& A, const FVector& B) {
  const FVector2D P(A.X, A.Y), Q(B.X, B.Y);
  if (FVector::Dist2D(A, B) < 40.f || FVector::Dist2D(A, B) > 2000.f) return true;
  for (const FOKStreetProp& Prop : Props) {
   if (Prop.Kind == EOKStreetPropKind::Block) {
    const FVector2D Min(Prop.Center.X - Prop.Size.X * 0.5f - OKStreetBodyClearance, Prop.Center.Y - Prop.Size.Y * 0.5f - OKStreetBodyClearance);
    const FVector2D Max(Prop.Center.X + Prop.Size.X * 0.5f + OKStreetBodyClearance, Prop.Center.Y + Prop.Size.Y * 0.5f + OKStreetBodyClearance);
    if (OKStreetSegmentHitsAabb(P, Q, Min, Max)) return true;
   } else if (Prop.Kind == EOKStreetPropKind::Trunk || Prop.Kind == EOKStreetPropKind::Lamp) {
    const float Radius = FMath::Max(Prop.Size.X, Prop.Size.Y) * 0.5f + OKStreetBodyClearance;
    if (OKStreetDistanceToSegment(FVector2D(Prop.Center.X, Prop.Center.Y), P, Q) <= Radius) return true;
   }
  }
  return false;
 };
 for (const TArray<int32>& Route : Routes) {
  if (Route.Num() < 2) { Problems.Add(TEXT("route_too_short")); continue; }
  for (int32 I = 0; I < Route.Num(); ++I) {
   const int32 A = Route[I], B = Route[(I + 1) % Route.Num()];
   if (!Points.IsValidIndex(A) || !Points.IsValidIndex(B) || SegmentBlocked(Points[A].Position, Points[B].Position)) Problems.Add(FString::Printf(TEXT("route_segment_blocked_%d_%d"), A, B));
  }
 }
 TArray<FOKStreetAgentSim> Agents;
 OKStreetMakeAgents(Agents);
 if (Agents.Num() != OKStreetFemaleCount) Problems.Add(TEXT("agent_count"));
 const FVector Male = OKStreetMaleSpawn();
 if (OKStreetBlockedPoint(Male)) Problems.Add(TEXT("male_spawn_blocked"));
 for (int32 I = 0; I < Agents.Num(); ++I) {
  if (FVector::Dist2D(Agents[I].Position, Male) < 150.f) Problems.Add(TEXT("male_overlaps_npc"));
  for (int32 J = I + 1; J < Agents.Num(); ++J) if (FVector::Dist2D(Agents[I].Position, Agents[J].Position) < 150.f) Problems.Add(TEXT("npc_spawn_overlap"));
 }
}

inline FVector OKStreetSeparationOffset(const FVector& Self, const TArray<FVector>& Others, float Radius = 90.f) {
 FVector Offset = FVector::ZeroVector;
 for (const FVector& Other : Others) {
  FVector Away = Self - Other;
  Away.Z = 0.f;
  const float Distance = Away.Size();
  if (Distance > 1.f && Distance < Radius) Offset += Away.GetSafeNormal() * (Radius - Distance) * 0.5f;
 }
 Offset.Z = 0.f;
 return Offset;
}

inline bool OKStreetAccumulateStuck(FOKStreetAgentSim& Agent, float MovedCm, float Dt, const TArray<TArray<int32>>& Routes) {
 if (Agent.Held || Agent.Dwell > 0.f) { Agent.Stuck = 0.f; return false; }
 if (MovedCm >= 1.f) { Agent.Stuck = 0.f; return false; }
 Agent.Stuck += Dt;
 if (Agent.Stuck < 2.f || !Routes.IsValidIndex(Agent.Route) || Routes[Agent.Route].Num() == 0) return false;
 Agent.Stuck = 0.f;
 Agent.Cursor = (Agent.Cursor + 1) % Routes[Agent.Route].Num();
 Agent.Mode = EOKStreetMove::Walking;
 return true;
}

// The stop being used is separate from Cursor, which already points to the next
// destination. Occupancy derives from current runtime agents, never card names.
inline bool OKStreetActivityPointOccupied(const FOKStreetAgentSim& Agent, int32 PointIndex, const TArray<FOKStreetPointData>& Points, const TArray<TArray<int32>>& Routes, const TArray<FOKStreetAgentSim>& Others) {
 if (!Points.IsValidIndex(PointIndex) || OKStreetDwell(Points[PointIndex].Kind) <= 0.f) return false;
 const FVector Target = Points[PointIndex].Position;
 const float Approach = OKStreetBodyClearance * 2.f;
 for (const FOKStreetAgentSim& Other : Others) {
  if (Other.Slot == Agent.Slot) continue;
  if (Other.Dwell > 0.f && Other.DwellPoint == PointIndex) return true;
  // Includes the player or a talk partner standing on the activity point.
  if (FVector::Dist2D(Other.Position, Target) < OKStreetBodyClearance) return true;
  if (Other.Held || Other.Slot >= Agent.Slot || !Routes.IsValidIndex(Other.Route)) continue;
  const TArray<int32>& OtherRoute = Routes[Other.Route];
  if (OtherRoute.IsValidIndex(Other.Cursor) && OtherRoute[Other.Cursor] == PointIndex
   && FVector::Dist2D(Other.Position, Target) <= Approach) return true;
 }
 return false;
}

inline constexpr float OKStreetFemaleRunSpeed=270.f;
inline void OKStreetStepAgent(FOKStreetAgentSim& Agent, float Dt, const TArray<FOKStreetAgentSim>& Others, float TravelSpeed=OKStreetWalkSpeed) {
 if (Agent.Held) {
  Agent.Mode = Agent.Mode == EOKStreetMove::Talking || Agent.Mode == EOKStreetMove::Recovering ? Agent.Mode : EOKStreetMove::Reserved;
  return;
 }
 TArray<FOKStreetPointData> Points;
 TArray<TArray<int32>> Routes;
 OKStreetWaypoints(Points);
 OKStreetRoutes(Routes);
 if (!Routes.IsValidIndex(Agent.Route) || Routes[Agent.Route].Num() == 0) return;
 const TArray<int32>& Route = Routes[Agent.Route];
 if (Agent.Dwell > 0.f) {
  Agent.Dwell = FMath::Max(0.f, Agent.Dwell - Dt);
  Agent.Mode = Points.IsValidIndex(Agent.DwellPoint) ? OKStreetDwellMode(Points[Agent.DwellPoint].Kind) : EOKStreetMove::Waiting;
  if (Agent.Dwell <= 0.f) { Agent.DwellPoint = INDEX_NONE; Agent.Mode = EOKStreetMove::Walking; }
  return;
 }
 const FVector Before = Agent.Position;
 const FVector Target = Points[Route[Agent.Cursor]].Position;
 FVector To = Target - Agent.Position;
 To.Z = 0.f;
 const float Distance = To.Size();
 if (Distance <= OKStreetBodyClearance * 2.f && OKStreetActivityPointOccupied(Agent, Route[Agent.Cursor], Points, Routes, Others)) {
  Agent.Mode = EOKStreetMove::Waiting;
  Agent.Stuck = 0.f; // An occupied stop is intentional waiting, not a stuck route.
  return;
 }
 if (Distance <= 12.f) {
  const int32 Arrived = Agent.Cursor;
  Agent.Dwell = OKStreetDwell(Points[Route[Arrived]].Kind);
  Agent.DwellPoint = Agent.Dwell > 0.f ? Route[Arrived] : INDEX_NONE;
  Agent.Cursor = (Agent.Cursor + 1) % Route.Num();
  Agent.Mode = Agent.Dwell > 0.f ? OKStreetDwellMode(Points[Route[Arrived]].Kind) : EOKStreetMove::Walking;
  return;
 }
 FVector Step = To.GetSafeNormal() * FMath::Min(TravelSpeed * Dt, Distance);
 TArray<FVector> OtherPositions;
 for (const FOKStreetAgentSim& Other : Others) if (Other.Slot != Agent.Slot) OtherPositions.Add(Other.Position);
 Step += OKStreetSeparationOffset(Agent.Position, OtherPositions);
 Step.Z = 0.f;
 const FVector Proposed = Agent.Position + Step;
 if (!OKStreetBlockedPoint(Proposed, 20.f)) Agent.Position = Proposed;
 Agent.Position.Z = 0.f;
 OKStreetAccumulateStuck(Agent, FVector::Dist2D(Before, Agent.Position), Dt, Routes);
 if (Agent.Dwell <= 0.f) Agent.Mode = EOKStreetMove::Walking;
}

inline FOKStreetCharacterMeta OKStreetMeta(const TCHAR* Id, const TCHAR* Name, const TCHAR* Personality, EOKStreetSex Sex) {
 FOKStreetCharacterMeta Meta;
 Meta.Id = Id;
 Meta.Name = Name;
 Meta.Personality = Personality;
 Meta.SexKind = Sex;
 Meta.SexIndex = Sex == EOKStreetSex::Female ? 1 : (Sex == EOKStreetSex::Male ? 0 : INDEX_NONE);
 Meta.Sex = Meta.SexIndex == INDEX_NONE ? FString() : FString::FromInt(Meta.SexIndex);
 int32 PersonalityId = INDEX_NONE;
 if (OKStreetCanonicalInt(Meta.Personality, PersonalityId)) Meta.PersonalityId = PersonalityId;
 return Meta;
}

inline void OKStreetExpect(TArray<FString>& Failures, bool Condition, const TCHAR* What) { if (!Condition) Failures.Add(What); }








inline bool OKStreetSingleRole(const FOKStreetNode& Node, const TCHAR* Role) {
 return Node.Roles.Num() == 1 && Node.Roles[0] == Role;
}



inline int32 OKStreetFindRelation(const FOKStreetStore& Store, const FString& Id) {
 for (int32 Index = 0; Index < Store.Relations.Num(); ++Index) if (Store.Relations[Index].Id == Id) return Index;
 return INDEX_NONE;
}


















inline const TCHAR* OKStreetFixtureCatalogue() {
 return TEXT("{\"nodes\":[")
  TEXT("{\"id\":\"low\",\"personality\":0,\"category\":\"daily\",\"text_jp\":\"a\",\"text_zh\":\"甲\",\"favor\":10,\"lewdness\":120,\"conditions\":{\"id\":-1,\"game_speak_filter\":\"always\",\"street_support\":\"supported\"},\"source\":{\"stage\":0,\"priority\":1,\"roles\":[\"generic\"],\"effects_defined\":true,\"voice_sequence\":[{\"index\":0,\"status\":\"matched\",\"token\":\"a\",\"path\":\"C:/v/a.ogg\",\"zh_ogg\":\"C:/v/zh_a.ogg\"},{\"index\":1,\"status\":\"matched\",\"token\":\"b\",\"path\":\"C:/v/b.ogg\",\"zh_ogg\":\"C:/v/zh_b.ogg\"}]}},")
  TEXT("{\"id\":\"high\",\"personality\":0,\"category\":\"daily\",\"text_jp\":\"b\",\"text_zh\":\"乙\",\"favor\":0,\"lewdness\":0,\"conditions\":{\"id\":-1,\"game_speak_filter\":\"always\",\"street_support\":\"supported\"},\"source\":{\"stage\":0,\"priority\":9,\"roles\":[\"generic\"],\"effects_defined\":true,\"voice_sequence\":[{\"index\":0,\"status\":\"matched\",\"token\":\"h\",\"path\":\"C:/v/h.ogg\",\"zh_ogg\":\"C:/v/zh_h.ogg\"}]}},")
  TEXT("{\"id\":\"hole\",\"personality\":0,\"category\":\"daily\",\"text_jp\":\"c\",\"text_zh\":\"丙\",\"favor\":1,\"lewdness\":0,\"voice_files\":[\"C:/v/zh_keep.ogg\"],\"voice_files_zh\":[\"C:/v/zh_keep.ogg\"],\"conditions\":{\"id\":-1,\"game_speak_filter\":\"always\",\"street_support\":\"supported\"},\"source\":{\"stage\":0,\"priority\":50,\"roles\":[\"generic\"],\"effects_defined\":true,\"voice_sequence\":[{\"index\":0,\"status\":\"missing_ogg\",\"token\":\"x\",\"path\":null,\"zh_ogg\":null},{\"index\":1,\"status\":\"unsupported\",\"token\":\"callName0\",\"path\":null,\"zh_ogg\":null},{\"index\":2,\"status\":\"matched\",\"token\":\"y\",\"path\":\"C:/v/keep.ogg\",\"zh_ogg\":\"C:/v/zh_keep.ogg\"}]}},")
  TEXT("{\"id\":\"branch\",\"personality\":0,\"category\":\"daily\",\"text_jp\":\"d\",\"text_zh\":\"丁\",\"favor\":4,\"lewdness\":0,\"conditions\":{\"id\":-1,\"game_speak_filter\":\"always\",\"street_support\":\"supported\"},\"source\":{\"stage\":0,\"priority\":80,\"roles\":[\"success\"],\"effects_defined\":true,\"voice_sequence\":[{\"index\":0,\"status\":\"matched\",\"token\":\"s\",\"path\":\"C:/v/s.ogg\",\"zh_ogg\":\"C:/v/zh_s.ogg\"}]}},")
  TEXT("{\"id\":\"never\",\"personality\":0,\"category\":\"farewell\",\"text_jp\":\"e\",\"text_zh\":\"戊\",\"favor\":5,\"lewdness\":0,\"conditions\":{\"id\":0,\"game_speak_filter\":\"never\",\"street_support\":\"supported\"},\"source\":{\"stage\":0,\"priority\":3,\"roles\":[\"generic\"],\"effects_defined\":true,\"voice_sequence\":[{\"index\":0,\"status\":\"matched\",\"token\":\"n\",\"path\":\"C:/v/n.ogg\",\"zh_ogg\":\"C:/v/zh_n.ogg\"}]}},")
  TEXT("{\"id\":\"otherid\",\"personality\":0,\"category\":\"greeting\",\"text_jp\":\"f\",\"text_zh\":\"己\",\"favor\":0,\"lewdness\":0,\"conditions\":{\"id\":2,\"game_speak_filter\":\"original_default_true_not_accepted\",\"street_support\":\"unsupported\"},\"source\":{\"stage\":0,\"priority\":360,\"roles\":[\"generic\"],\"effects_defined\":true,\"voice_sequence\":[{\"index\":0,\"status\":\"matched\",\"token\":\"o\",\"path\":\"C:/v/o.ogg\",\"zh_ogg\":\"C:/v/zh_o.ogg\"}]}},")
  TEXT("{\"id\":\"nofx\",\"personality\":0,\"category\":\"romance\",\"text_jp\":\"g\",\"text_zh\":\"庚\",\"favor\":40,\"lewdness\":7,\"conditions\":{\"id\":-1,\"game_speak_filter\":\"always\",\"street_support\":\"supported\"},\"source\":{\"stage\":0,\"priority\":2,\"roles\":[\"generic\"],\"effects_defined\":false,\"voice_sequence\":[{\"index\":0,\"status\":\"matched\",\"token\":\"q\",\"path\":\"C:/v/q.ogg\",\"zh_ogg\":\"C:/v/zh_q.ogg\"}]}},")
  TEXT("{\"id\":\"loverline\",\"personality\":0,\"category\":\"intimacy\",\"text_jp\":\"h\",\"text_zh\":\"辛\",\"favor\":0,\"lewdness\":0,\"conditions\":{\"id\":-1,\"game_speak_filter\":\"always\",\"street_support\":\"supported\"},\"source\":{\"stage\":2,\"priority\":4,\"roles\":[\"generic\"],\"effects_defined\":true,\"voice_sequence\":[]}},")
  TEXT("{\"id\":\"silent\",\"personality\":0,\"category\":\"greeting\",\"text_jp\":\"i\",\"text_zh\":\"壬\",\"favor\":0,\"lewdness\":0,\"conditions\":{\"id\":-1,\"game_speak_filter\":\"always\",\"street_support\":\"supported\"},\"source\":{\"stage\":0,\"priority\":1,\"roles\":[\"generic\"],\"effects_defined\":true,\"voice_sequence\":[]}},")
  TEXT("{\"id\":\"stringpers\",\"personality\":\"genki\",\"category\":\"daily\",\"text_zh\":\"x\",\"favor\":1,\"lewdness\":0,\"conditions\":{\"id\":-1,\"game_speak_filter\":\"always\",\"street_support\":\"supported\"},\"source\":{\"stage\":0,\"priority\":1,\"roles\":[\"generic\"],\"effects_defined\":true,\"voice_sequence\":[]}},")
  TEXT("{\"id\":\"badsource\",\"personality\":0,\"category\":\"daily\",\"text_zh\":\"y\",\"favor\":0,\"lewdness\":0,\"conditions\":{\"id\":-1,\"game_speak_filter\":\"always\",\"street_support\":\"supported\"},\"source\":\"fixture\"},")
  TEXT("{\"id\":\"low\",\"personality\":0,\"category\":\"daily\",\"text_zh\":\"dup\",\"favor\":0,\"lewdness\":0,\"conditions\":{\"id\":-1,\"game_speak_filter\":\"always\",\"street_support\":\"supported\"},\"source\":{\"stage\":0,\"priority\":1,\"roles\":[\"generic\"],\"effects_defined\":true,\"voice_sequence\":[]}}")
  TEXT("]}");
}

inline void OKStreetCheckCatalogue(TArray<FString>& Failures) {
 const FOKStreetCatalogue Catalogue = OKStreetParseCatalogue(OKStreetFixtureCatalogue(), TEXT("C:/fixture/catalogue.json"));
 OKStreetExpect(Failures, Catalogue.Error.IsEmpty() && !Catalogue.ById.Contains(TEXT("stringpers")) && !Catalogue.ById.Contains(TEXT("badsource")) && Catalogue.Skipped >= 2, TEXT("string personality and string source are rejected"));
 const FOKStreetNode& Low = Catalogue.Nodes[Catalogue.ById[TEXT("low")]];
 const FOKStreetNode& High = Catalogue.Nodes[Catalogue.ById[TEXT("high")]];
 const FOKStreetNode& Hole = Catalogue.Nodes[Catalogue.ById[TEXT("hole")]];
 const FOKStreetNode& Branch = Catalogue.Nodes[Catalogue.ById[TEXT("branch")]];
 const FOKStreetNode& Never = Catalogue.Nodes[Catalogue.ById[TEXT("never")]];
 const FOKStreetNode& Other = Catalogue.Nodes[Catalogue.ById[TEXT("otherid")]];
 const FOKStreetNode& NoFx = Catalogue.Nodes[Catalogue.ById[TEXT("nofx")]];
 const FOKStreetNode& Lover = Catalogue.Nodes[Catalogue.ById[TEXT("loverline")]];
 OKStreetExpect(Failures, Low.Personality == TEXT("0") && Low.PersonalityId == 0 && Low.StageIndex == 0 && Low.ConditionId == -1 && Low.EffectsDefined && Low.Priority == 1.f && Low.Effect.Favor == 10.f && Low.Effect.Lewdness == 120.f && Low.Effect.RelationEvent.IsEmpty(), TEXT("supported node keeps integer personality, stage, effects, and priority"));
 OKStreetExpect(Failures, Low.VoiceSequenceComplete && Low.VoiceFiles.Num() == 2 && Low.VoiceFiles[0] == TEXT("C:/v/zh_a.ogg") && Low.VoiceFiles[1] == TEXT("C:/v/zh_b.ogg"), TEXT("complete sequence plays every Chinese slot in order"));
 OKStreetExpect(Failures, !Hole.VoiceSequenceComplete && Hole.VoiceFiles.Num() == 0 && Hole.VoiceSequenceReason == TEXT("voice_sequence_incomplete:missing_ogg"), TEXT("callName or missing slots are not shortened away"));
 TArray<FString> HolePlay;
 OKStreetPlaybackFiles(Hole, HolePlay);
 OKStreetExpect(Failures, HolePlay.Num() == 0 && !Hole.VoiceFiles.Contains(TEXT("C:/v/zh_keep.ogg")), TEXT("playback does not keep the surviving clip"));
 OKStreetExpect(Failures, !NoFx.EffectsDefined && NoFx.Effect.Favor == 0.f && NoFx.Effect.Lewdness == 0.f, TEXT("effects_defined false contributes no favor or lewdness"));
 OKStreetExpect(Failures, Lover.VoiceSequenceComplete && Lover.VoiceSequenceReason == TEXT("voice_sequence_empty") && !Branch.Roles.IsEmpty() && Never.ConditionId == 0 && Other.ConditionId == 2 && High.Priority == 9.f, TEXT("parsed lines keep what the rules read (talk.rs decides who may say them)"));
}

inline void OKStreetCheckRoutes(TArray<FString>& Failures) {
 TArray<FString> Problems;
 OKStreetLayoutProblems(Problems);
 OKStreetExpect(Failures, Problems.Num() == 0, Problems.Num() ? *Problems[0] : TEXT("layout clear"));
 TArray<FOKStreetAgentSim> Agents;
 OKStreetMakeAgents(Agents);
 const FVector HeldAt = Agents[0].Position;
 Agents[0].Held = true;
 OKStreetStepAgent(Agents[0], 1.f, Agents);
 OKStreetExpect(Failures, Agents[0].Position.Equals(HeldAt) && Agents[0].Mode == EOKStreetMove::Reserved, TEXT("reserved female stays"));
 const FVector Before = Agents[1].Position;
 OKStreetStepAgent(Agents[1], 1.f, Agents);
 OKStreetExpect(Failures, FVector::Dist2D(Before, Agents[1].Position) > 50.f, TEXT("other females keep walking"));
 TArray<FOKStreetAgentSim> Again;
 OKStreetMakeAgents(Again);
 OKStreetExpect(Failures, Again[0].Position.Equals(Agents[0].Position), TEXT("route assignment has no card identity"));
 FOKStreetAgentSim Stuck;
 Stuck.Route = 0;
 Stuck.Cursor = 1;
 Stuck.Position = FVector(99999.f, 99999.f, 0.f);
 TArray<TArray<int32>> Routes;
 OKStreetRoutes(Routes);
 bool Advanced = false;
 for (int32 I = 0; I < 25; ++I) Advanced = OKStreetAccumulateStuck(Stuck, 0.f, 0.1f, Routes) || Advanced;
 OKStreetExpect(Failures, Advanced && Stuck.Cursor == 2, TEXT("stuck walker advances"));
 const FVector Push = OKStreetSeparationOffset(FVector(0, 0, 0), {FVector(10, 0, 0)});
 OKStreetExpect(Failures, Push.X < -1.f && FMath::IsNearlyZero(Push.Z), TEXT("generic separation pushes apart"));
 OKStreetExpect(Failures, OKStreetTalkRangeCm == 250.f, TEXT("talk range is 250cm"));
}

inline void OKStreetCheckActivityStops(TArray<FString>& Failures) {
 TArray<FOKStreetPointData> Points; TArray<TArray<int32>> Routes;
 OKStreetWaypoints(Points); OKStreetRoutes(Routes);
 auto At = [&](int32 Slot, int32 RouteIndex, int32 PointIndex) {
  FOKStreetAgentSim A; A.Slot=Slot; A.Route=RouteIndex;
  A.Cursor=Routes[RouteIndex].Find(PointIndex); A.Position=Points[PointIndex].Position; return A;
 };
 for (int32 PointIndex : {17,20,21,23,25}) {
  const int32 RouteIndex=(PointIndex==20||PointIndex==25)?3:2;
  FOKStreetAgentSim A=At(1,RouteIndex,PointIndex);
  OKStreetStepAgent(A,.01f,{});
  const int32 Next=A.Cursor; const float Before=A.Dwell;
  OKStreetExpect(Failures,A.DwellPoint==PointIndex&&A.Dwell>0&&A.Mode==OKStreetDwellMode(Points[PointIndex].Kind),TEXT("arrival records the current stop"));
  OKStreetStepAgent(A,.1f,{});
  OKStreetExpect(Failures,A.Cursor==Next&&A.DwellPoint==PointIndex&&A.Mode==OKStreetDwellMode(Points[PointIndex].Kind)&&A.Dwell<Before,TEXT("dwell uses arrived point, not next cursor"));
  A.Held=true; const float Paused=A.Dwell; const FVector Position=A.Position;
  OKStreetStepAgent(A,.5f,{});
  OKStreetExpect(Failures,A.Dwell==Paused&&A.Position.Equals(Position)&&A.DwellPoint==PointIndex,TEXT("talk reservation pauses the activity without losing destination"));
  A.Held=false; OKStreetStepAgent(A,.1f,{});
  OKStreetExpect(Failures,A.Mode==OKStreetDwellMode(Points[PointIndex].Kind)&&A.Dwell<Paused&&A.Cursor==Next,TEXT("released reservation resumes the original activity"));
  OKStreetStepAgent(A,10.f,{});
  OKStreetExpect(Failures,A.Dwell==0&&A.DwellPoint==INDEX_NONE&&A.Mode==EOKStreetMove::Walking,TEXT("elapsed stop releases occupancy"));
 }
 FOKStreetAgentSim Owner=At(1,2,17); OKStreetStepAgent(Owner,.01f,{});
 FOKStreetAgentSim Waiting=At(2,2,17); Waiting.Position.X-=100.f;
 const FVector Entry=Waiting.Position; const int32 Destination=Waiting.Cursor;
 for(int32 I=0;I<30;++I)OKStreetStepAgent(Waiting,.1f,{Owner});
 OKStreetExpect(Failures,Waiting.Mode==EOKStreetMove::Waiting&&Waiting.Position.Equals(Entry)&&Waiting.Cursor==Destination&&Waiting.Dwell==0&&Waiting.Stuck==0,TEXT("occupied stop waits without skipping destination as stuck"));
 Owner.Held=true; OKStreetStepAgent(Waiting,.1f,{Owner});
 OKStreetExpect(Failures,Waiting.Mode==EOKStreetMove::Waiting,TEXT("talking occupant keeps the stop reserved"));
 Owner.Dwell=0; Owner.DwellPoint=INDEX_NONE; Owner.Position.X+=500.f;
 OKStreetStepAgent(Waiting,1.f,{Owner}); OKStreetStepAgent(Waiting,.01f,{Owner});
 OKStreetExpect(Failures,Waiting.DwellPoint==17&&Waiting.Mode==EOKStreetMove::Looking&&Waiting.Dwell>0,TEXT("free stop admits the queued visitor"));
 FOKStreetAgentSim First=At(1,2,17),Second=At(2,2,17);
 First.Position.X-=100.f;Second.Position.X+=100.f;
 const TArray<FOKStreetAgentSim> Snapshot={First,Second};
 OKStreetStepAgent(First,.1f,Snapshot);OKStreetStepAgent(Second,.1f,Snapshot);
 OKStreetExpect(Failures,First.Mode==EOKStreetMove::Walking&&Second.Mode==EOKStreetMove::Waiting,TEXT("same snapshot uses stable slot priority for simultaneous arrivals"));
 FOKStreetAgentSim Player;Player.Slot=0;Player.Position=Points[17].Position;Player.Held=true;
 First=At(1,2,17);First.Position.X-=100.f;OKStreetStepAgent(First,.1f,{Player});
 OKStreetExpect(Failures,First.Mode==EOKStreetMove::Waiting,TEXT("player standing at stop prevents overlap"));
}

inline void OKStreetCheckSave(TArray<FString>& Failures) {
 FOKStreetStore Store;
 Store.FavorCapIsKksStepUp = true; Store.StepUpStranger = 75; Store.StepUpFriend = 100; Store.StepUpLover = 150; Store.StepUpBeyond = 150;
 for (const TCHAR* Id : {TEXT("same"), TEXT("other"), TEXT("session-temp")}) {
  FOKStreetRelation& R = Store.Relations[OKStreetRelationIndex(Store, Id, 5, true)];
  R.Favor = 6.f; R.Lewdness = 3.f; R.TalksRemaining = 4; R.Talked.Add(TEXT("d1")); R.Committed.Add(OKStreetTransactionId(Id, TEXT("d1")));
 }
 Store.Relations[1].Favor = 1.f;
 const FString Json = OKStreetWriteSave(Store);
 OKStreetExpect(Failures, Json.Contains(TEXT("75")) && Json.Contains(TEXT("favor_cap_is_kks_stepup")) && !Json.Contains(TEXT("session-temp")), TEXT("save records original step-up and omits session keys"));
 FOKStreetStore Loaded;
 FString Error;
 OKStreetExpect(Failures, OKStreetParseSave(Json, Loaded, Error) == EOKStreetLoad::Ok && Loaded.FavorCapIsKksStepUp && Loaded.StepUpStranger == 75 && Loaded.StepUpFriend == 100 && Loaded.StepUpLover == 150, TEXT("save roundtrip keeps step-up limits"));
 const FOKStreetRelation* Same = nullptr;
 const FOKStreetRelation* Other = nullptr;
 for (const FOKStreetRelation& Relation : Loaded.Relations) {
  if (Relation.Id == TEXT("same")) Same = &Relation;
  if (Relation.Id == TEXT("other")) Other = &Relation;
 }
 OKStreetExpect(Failures, Same && Other && FMath::IsNearlyEqual(Same->Favor, 6.f) && FMath::IsNearlyEqual(Other->Favor, 1.f) && Same->Committed.Contains(OKStreetTransactionId(TEXT("same"), TEXT("d1"))), TEXT("stable ids do not merge same names"));
 FOKStreetStore Wide;
 OKStreetExpect(Failures, OKStreetParseSave(TEXT("{\"schema\":1,\"talk_budget_is_kks\":false,\"favor_cap_is_kks_stepup\":false,\"relations\":[{\"id\":\"wide\",\"favor\":140,\"lewdness\":0,\"relation\":\"lover\",\"talks_remaining\":5,\"day\":1,\"talked\":[],\"flags\":[],\"committed\":[]}]}"), Wide, Error) == EOKStreetLoad::Ok && Wide.Relations[0].Favor == 140.f && !Wide.FavorCapIsKksStepUp, TEXT("a save without step-up is not clamped to 100"));
 FOKStreetStore Rejected;
 OKStreetExpect(Failures, OKStreetParseSave(TEXT("{\"schema\":3,\"relations\":[]}"), Rejected, Error) == EOKStreetLoad::Invalid, TEXT("future schema is not coerced"));
 OKStreetExpect(Failures, OKStreetParseSave(TEXT("not-json"), Rejected, Error) == EOKStreetLoad::Invalid && Error == TEXT("save_json_invalid"), TEXT("invalid save is not an empty success"));
 const FString Path = FPaths::ProjectSavedDir() / TEXT("StreetSocial") / FString::Printf(TEXT("rules-exercise-%s.json"), *FGuid::NewGuid().ToString(EGuidFormats::Digits));
 FString SaveError;
 const FString FirstBody = TEXT("{\"schema\":1,\"talk_budget_is_kks\":false,\"favor_cap_is_kks_stepup\":false,\"relations\":[]}");
 OKStreetExpect(Failures, OKStreetAtomicSave(Path, FirstBody, SaveError), TEXT("atomic save creates the exercise file"));
 OKStreetExpect(Failures, OKStreetAtomicSave(Path, Json, SaveError), TEXT("atomic save replaces through backup"));
 FString Backup;
 FOKStreetStore Preserved;
 OKStreetExpect(Failures, FFileHelper::LoadFileToString(Backup, *(Path + TEXT(".bak"))) && Backup.Contains(TEXT("\"relations\":[]")) && IFileManager::Get().FileExists(*Path) && OKStreetLoadSaveFile(Path, Preserved, Error) == EOKStreetLoad::Ok, TEXT("exercise save and backup are preserved"));
 FOKStreetManifestSocial Words;
 const bool WordsOk = OKStreetReadManifestSocial(TEXT("C:/manifest.json"), TEXT("{\"cards\":[\"a.png\"],\"characters\":[{\"id\":\"keep\",\"name\":\"A\",\"personality\":\"genki\",\"sex\":\"female\"}]}"), Words);
 OKStreetExpect(Failures, WordsOk && Words.Characters.Num() == 1 && Words.Characters[0].Id == TEXT("keep") && Words.Characters[0].PersonalityId == INDEX_NONE && Words.Characters[0].SexKind == EOKStreetSex::Unknown, TEXT("string personality and sex are not accepted"));
}



inline FString OKStreetTestApng(TCHAR Mark) {
 FString Id = TEXT("apng-");
 for (int32 Index = 0; Index < 64; ++Index) Id.AppendChar(Mark);
 return Id;
}

inline FOKStreetRelation OKStreetTestRelation(const FString& Id) {
 FOKStreetRelation Relation;
 Relation.Id = Id;
 Relation.Favor = 12.5f;
 Relation.Lewdness = 7.f;
 Relation.Stage = TEXT("friend");
 Relation.TalksRemaining = 3;
 Relation.Day = 4;
 Relation.Talked = {TEXT("node-a")};
 Relation.Flags = {TEXT("talk_event:0")};
 Relation.Committed = {TEXT("txn-1")};
 Relation.Topics = {TEXT("topic:9")};
 Relation.CooldownIds = {TEXT("topic:9")};
 Relation.CooldownDays = {6};
 return Relation;
}

inline bool OKStreetSameRelationBody(const FOKStreetRelation& Left, const FOKStreetRelation& Right) {
 return FMath::IsNearlyEqual(Left.Favor, Right.Favor) && FMath::IsNearlyEqual(Left.Lewdness, Right.Lewdness) && Left.Stage == Right.Stage && Left.TalksRemaining == Right.TalksRemaining && Left.Day == Right.Day && Left.Talked == Right.Talked && Left.Flags == Right.Flags && Left.Committed == Right.Committed && Left.Topics == Right.Topics && Left.CooldownIds == Right.CooldownIds && Left.CooldownDays == Right.CooldownDays;
}

inline void OKStreetCheckActiveActorInterrupt(TArray<FString>& Failures) {
 FOKStreetActorPresence Presence;
 FOKStreetPresenceSample Loading;
 Loading.Queried = true;
 OKStreetExpect(Failures, !OKStreetActiveActorShouldEnd(Loading, Presence) && !Presence.SeenReady && !Presence.SeenVisible, TEXT("loading is not an active-actor disappearance"));
 FOKStreetPresenceSample Fade;
 Fade.Queried = true;
 Fade.Ready = true;
 OKStreetExpect(Failures, !OKStreetActiveActorShouldEnd(Fade, Presence) && Presence.SeenReady && !Presence.SeenVisible, TEXT("reveal fade is not an active-actor disappearance"));
 FOKStreetPresenceSample Shown;
 Shown.Queried = true;
 Shown.Ready = true;
 Shown.Visible = true;
 OKStreetExpect(Failures, !OKStreetActiveActorShouldEnd(Shown, Presence) && Presence.SeenVisible, TEXT("a revealed actor staying visible is not culling"));
 OKStreetExpect(Failures, !OKStreetActiveActorShouldEnd(Shown, Presence), TEXT("a second visible sample still does not end the talk"));
 FOKStreetPresenceSample Hidden;
 Hidden.Queried = true;
 Hidden.Ready = true;
 OKStreetExpect(Failures, OKStreetActiveActorShouldEnd(Hidden, Presence), TEXT("a revealed actor losing visibility ends the talk"));
 FOKStreetActorPresence Early;
 FOKStreetPresenceSample Missing;
 OKStreetExpect(Failures, !OKStreetActiveActorShouldEnd(Missing, Early), TEXT("a status query miss before ready is not disappearance"));
 FOKStreetPresenceSample Became = Shown;
 OKStreetExpect(Failures, !OKStreetActiveActorShouldEnd(Became, Early) && Early.SeenReady, TEXT("the first ready sample only latches presence"));
 OKStreetExpect(Failures, OKStreetActiveActorShouldEnd(Missing, Early), TEXT("a status query miss after ready is disappearance"));
 FOKStreetActorPresence WasReady;
 FOKStreetPresenceSample Once;
 Once.Queried = true;
 Once.Ready = true;
 Once.Visible = true;
 OKStreetActiveActorShouldEnd(Once, WasReady);
 FOKStreetPresenceSample NotReady;
 NotReady.Queried = true;
 OKStreetExpect(Failures, OKStreetActiveActorShouldEnd(NotReady, WasReady), TEXT("a previously ready actor losing ready ends the talk"));
 FOKStreetActorPresence Fresh;
 FOKStreetPresenceSample Fault;
 Fault.Queried = true;
 Fault.Ready = true;
 Fault.Visible = true;
 Fault.HasError = true;
 int32 Commits = 0;
 const bool Ended = OKStreetActiveActorShouldEnd(Fault, Fresh);
 OKStreetExpect(Failures, Ended && Commits == 0 && !Fresh.SeenReady, TEXT("a runtime error ends the talk and does not commit"));
}

inline void OKStreetCheckIdentityRetarget(TArray<FString>& Failures) {
 const FString OldId = OKStreetTestApng(TEXT('a'));
 const FString NewId = OKStreetTestApng(TEXT('b'));
 const FString OtherFemale = OKStreetTestApng(TEXT('c'));
 const FString UnmatchedId = OKStreetTestApng(TEXT('d'));
 const FString MaleId = OKStreetTestApng(TEXT('e'));
 const FString UnknownId = OKStreetTestApng(TEXT('f'));
 const TArray<FString> Females = {NewId, OtherFemale};
 const TArray<FString> Actors = {MaleId, NewId, OtherFemale};
 const FString TimeUtc = TEXT("2026-10-07T00:00:00Z");
 auto Request = [](const FString& RequestId, const FString& Old, const FString& New) {
  FOKStreetRetargetRequest Row;
  Row.RequestId = RequestId;
  Row.OldId = Old;
  Row.NewId = New;
  return Row;
 };
 FOKStreetStore Store;
 Store.FavorCapIsKksStepUp = true;
 Store.StepUpStranger = 75;
 Store.StepUpFriend = 100;
 Store.StepUpLover = 150;
 Store.PreferenceLoaded = true;
 Store.Relations.Add(OKStreetTestRelation(OldId));
 FOKStreetRelation Unmatched = OKStreetTestRelation(UnmatchedId);
 Unmatched.Favor = 3.f;
 Unmatched.Stage = TEXT("lover");
 Unmatched.Lewdness = 1.f;
 Store.Relations.Add(Unmatched);
 const FString LegacyBody = OKStreetWriteSave(Store);
 OKStreetExpect(Failures, !LegacyBody.Contains(TEXT("identity_retargets")), TEXT("an empty retarget ledger is omitted from a schema 1 save"));
 FOKStreetStore LegacyLoaded;
 FString Error;
 OKStreetExpect(Failures, OKStreetParseSave(LegacyBody, LegacyLoaded, Error) == EOKStreetLoad::Ok && LegacyLoaded.Retargets.Num() == 0 && !OKStreetWriteSave(LegacyLoaded).Contains(TEXT("identity_retargets")), TEXT("a save without identity_retargets round-trips without inventing a ledger"));
 OKStreetExpect(Failures, OKStreetParseSave(TEXT("{\"schema\":1,\"talk_budget_is_kks\":false,\"favor_cap_is_kks_stepup\":false,\"relations\":[{\"id\":\"wide\",\"favor\":140,\"lewdness\":0,\"relation\":\"lover\",\"talks_remaining\":5,\"day\":1,\"talked\":[],\"flags\":[],\"committed\":[]}]}"), LegacyLoaded, Error) == EOKStreetLoad::Ok && LegacyLoaded.Retargets.Num() == 0 && LegacyLoaded.Relations[0].Favor == 140.f && !OKStreetWriteSave(LegacyLoaded).Contains(TEXT("identity_retargets")), TEXT("a legacy save without the optional ledger stays valid"));
 OKStreetExpect(Failures, OKStreetParseSave(TEXT("{\"schema\":1,\"relations\":[],\"identity_retargets\":[]}"), LegacyLoaded, Error) == EOKStreetLoad::Ok && LegacyLoaded.Retargets.Num() == 0, TEXT("an empty identity_retargets array is valid"));
 OKStreetExpect(Failures, OKStreetParseSave(TEXT("{\"schema\":1,\"relations\":[],\"identity_retargets\":{}}"), LegacyLoaded, Error) == EOKStreetLoad::Invalid && Error == TEXT("save_identity_retargets_invalid") && LegacyLoaded.Relations.Num() == 0 && LegacyLoaded.Retargets.Num() == 0, TEXT("a malformed retarget ledger rejects the whole save"));
 FOKStreetStore Published = Store;
 FOKStreetStore Out;
 FOKStreetRetargetResult Result;
 const TArray<FOKStreetRetargetRequest> None;
 OKStreetExpect(Failures, OKStreetRetargetRelations(Store, None, Females, Actors, TimeUtc, Out, Result) && Result.Applied && Result.Moved == 0 && Result.Replayed == 0 && OKStreetWriteSave(Out) == LegacyBody, TEXT("an empty retarget request publishes nothing"));
 const TArray<FOKStreetRetargetRequest> Move = {Request(TEXT("retarget-selftest-1"), OldId, NewId)};
 OKStreetExpect(Failures, OKStreetRetargetRelations(Store, Move, Females, Actors, TimeUtc, Out, Result) && Result.Applied && Result.Moved == 1 && Result.Replayed == 0, TEXT("an explicit retarget moves the relation"));
 OKStreetExpect(Failures, OKStreetWriteSave(Store) == LegacyBody && OKStreetWriteSave(Published) == LegacyBody, TEXT("retarget does not publish the caller's store by itself"));
 const FOKStreetRelation* Moved = nullptr;
 const FOKStreetRelation* Kept = nullptr;
 for (const FOKStreetRelation& Relation : Out.Relations) {
  if (Relation.Id == NewId) Moved = &Relation;
  if (Relation.Id == UnmatchedId) Kept = &Relation;
  if (Relation.Id == OldId) Moved = nullptr;
 }
 OKStreetExpect(Failures, Moved && Kept && Moved->Id == NewId && OKStreetSameRelationBody(*Moved, OKStreetTestRelation(NewId)) && OKStreetCountRelationId(Out, OldId) == 0 && OKStreetCountRelationId(Out, NewId) == 1, TEXT("retarget preserves every relation field and only changes id"));
 OKStreetExpect(Failures, Kept && Kept->Id == UnmatchedId && FMath::IsNearlyEqual(Kept->Favor, 3.f) && Kept->Stage == TEXT("lover") && FMath::IsNearlyEqual(Kept->Lewdness, 1.f), TEXT("an unmatched relation stays in place"));
 OKStreetExpect(Failures, Out.FavorCapIsKksStepUp && Out.StepUpFriend == 100 && Out.PreferenceLoaded && Out.Retargets.Num() == 1 && Out.Retargets[0].RequestId == TEXT("retarget-selftest-1") && Out.Retargets[0].OldId == OldId && Out.Retargets[0].NewId == NewId && Out.Retargets[0].Time == TimeUtc, TEXT("retarget keeps rules state and records one trace"));
 const FString Exercise = FPaths::ProjectSavedDir() / TEXT("StreetSocial") / FString::Printf(TEXT("retarget-exercise-%s.json"), *FGuid::NewGuid().ToString(EGuidFormats::Digits));
 FString SaveError;
 OKStreetExpect(Failures, Exercise.Contains(TEXT("retarget-exercise-")) && !Exercise.Contains(TEXT("relationships.json")) && OKStreetAtomicSave(Exercise, OKStreetWriteSave(Out), SaveError), TEXT("retarget exercise uses an isolated atomic save"));
 Published = Out;
 FOKStreetStore Reloaded;
 OKStreetExpect(Failures, OKStreetLoadSaveFile(Exercise, Reloaded, Error) == EOKStreetLoad::Ok && Reloaded.Retargets.Num() == 1 && Reloaded.Retargets[0].Time == TimeUtc && OKStreetCountRelationId(Reloaded, NewId) == 1 && OKStreetCountRelationId(Reloaded, UnmatchedId) == 1, TEXT("the retarget trace persists through the atomic save"));
 FString SavedBody;
 OKStreetExpect(Failures, FFileHelper::LoadFileToString(SavedBody, *Exercise) && SavedBody.Contains(TEXT("identity_retargets")) && SavedBody.Contains(TEXT("retarget-selftest-1")) && SavedBody.Contains(OldId) && SavedBody.Contains(NewId) && SavedBody.Contains(TimeUtc), TEXT("the saved ledger keeps old_id, new_id, request_id, and time"));
 int32 UnmatchedIndex = INDEX_NONE;
 for (int32 Index = 0; Index < Reloaded.Relations.Num(); ++Index) if (Reloaded.Relations[Index].Id == UnmatchedId) UnmatchedIndex = Index;
 if (UnmatchedIndex != INDEX_NONE) Reloaded.Relations[UnmatchedIndex].Favor = 4.f;
 const FString LaterBody = OKStreetWriteSave(Reloaded);
 OKStreetExpect(Failures, LaterBody.Contains(TEXT("identity_retargets")) && LaterBody.Contains(TEXT("retarget-selftest-1")) && LaterBody.Contains(UnmatchedId), TEXT("a later schema 1 save still preserves the ledger"));
 FOKStreetStore ReplayOut;
 FOKStreetRetargetResult Replay;
 OKStreetExpect(Failures, OKStreetRetargetRelations(Published, Move, Females, Actors, TEXT("2026-10-08T00:00:00Z"), ReplayOut, Replay) && Replay.Applied && Replay.Moved == 0 && Replay.Replayed == 1 && ReplayOut.Retargets.Num() == 1 && ReplayOut.Retargets[0].Time == TimeUtc && OKStreetWriteSave(ReplayOut) == OKStreetWriteSave(Published), TEXT("the same request id replays once without a second trace or a new time"));
 FString AfterReplay;
 OKStreetExpect(Failures, FFileHelper::LoadFileToString(AfterReplay, *Exercise) && AfterReplay == SavedBody, TEXT("an idempotent replay does not rewrite the save"));
 FOKStreetStore MismatchOut;
 FOKStreetRetargetResult Mismatch;
 const TArray<FOKStreetRetargetRequest> MismatchRequest = {Request(TEXT("retarget-selftest-1"), OldId, OtherFemale)};
 OKStreetExpect(Failures, !OKStreetRetargetRelations(Published, MismatchRequest, Females, Actors, TimeUtc, MismatchOut, Mismatch) && !Mismatch.Applied && Mismatch.Reason == TEXT("request_id_mismatch") && Mismatch.Moved == 0 && OKStreetWriteSave(MismatchOut) == OKStreetWriteSave(Published), TEXT("reusing a request id for a different pair is rejected"));
 FOKStreetStore Collision = Store;
 Collision.Relations.Add(OKStreetTestRelation(NewId));
 const FString CollisionBody = OKStreetWriteSave(Collision);
 FOKStreetStore CollisionOut;
 FOKStreetRetargetResult CollisionResult;
 OKStreetExpect(Failures, !OKStreetRetargetRelations(Collision, Move, Females, Actors, TimeUtc, CollisionOut, CollisionResult) && CollisionResult.Reason == TEXT("destination_collision") && OKStreetWriteSave(CollisionOut) == CollisionBody && OKStreetWriteSave(Collision) == CollisionBody, TEXT("an existing destination relation is not overwritten or merged"));
 const TArray<FOKStreetRetargetRequest> Unknown = {Request(TEXT("missing-old"), UnknownId, NewId)};
 FOKStreetRetargetResult UnknownResult;
 OKStreetExpect(Failures, !OKStreetRetargetRelations(Store, Unknown, Females, Actors, TimeUtc, Out, UnknownResult) && UnknownResult.Reason == TEXT("unknown_old_id") && OKStreetWriteSave(Out) == LegacyBody, TEXT("an unknown old id is rejected"));
 FString Upper = TEXT("apng-");
 for (int32 Index = 0; Index < 64; ++Index) Upper.AppendChar(TEXT('B'));
 const TArray<FOKStreetRetargetRequest> Invalid = {Request(TEXT("bad-hash"), TEXT("C000.png"), NewId), Request(TEXT("bad-upper"), Upper, NewId)};
 FOKStreetRetargetResult InvalidResult;
 OKStreetExpect(Failures, !OKStreetRetargetRelations(Store, {Invalid[0]}, Females, Actors, TimeUtc, Out, InvalidResult) && InvalidResult.Reason == TEXT("old_id_invalid"), TEXT("a path or filename is not an identity"));
 OKStreetExpect(Failures, !OKStreetRetargetRelations(Store, {Invalid[1]}, Females, Actors, TimeUtc, Out, InvalidResult) && InvalidResult.Reason == TEXT("old_id_invalid") && OKStreetWriteSave(Out) == LegacyBody, TEXT("an uppercase or short hash is not a production apng id"));
 const TArray<FOKStreetRetargetRequest> Batch = {Request(TEXT("batch-ok"), OldId, NewId), Request(TEXT("batch-missing"), UnknownId, OtherFemale)};
 FOKStreetRetargetResult BatchResult;
 OKStreetExpect(Failures, !OKStreetRetargetRelations(Store, Batch, Females, Actors, TimeUtc, Out, BatchResult) && !BatchResult.Applied && BatchResult.Moved == 0 && BatchResult.Reason == TEXT("unknown_old_id") && OKStreetWriteSave(Out) == LegacyBody && Out.Retargets.Num() == 0, TEXT("one bad request publishes none of the batch"));
 const TArray<FOKStreetRetargetRequest> Chain = {Request(TEXT("chain-a"), OldId, NewId), Request(TEXT("chain-b"), NewId, OtherFemale)};
 OKStreetExpect(Failures, !OKStreetRetargetRelations(Store, Chain, Females, Actors, TimeUtc, Out, BatchResult) && BatchResult.Reason == TEXT("retarget_batch_collision") && OKStreetWriteSave(Out) == LegacyBody, TEXT("a retarget chain is not applied as one batch"));
 OKStreetExpect(Failures, !OKStreetRetargetRelations(Store, {Request(TEXT("session-live"), OldId, NewId)}, Females, Actors, TimeUtc, Out, Result) && Result.Reason == TEXT("request_id_invalid"), TEXT("a session request id is not a stable retarget"));
 OKStreetExpect(Failures, !OKStreetRetargetRelations(Store, {Request(TEXT("still-here"), NewId, OtherFemale)}, Females, Actors, TimeUtc, Out, Result) && Result.Reason == TEXT("old_id_still_selected"), TEXT("an old id that is still selected is not retargeted"));
 OKStreetExpect(Failures, !OKStreetRetargetRelations(Store, {Request(TEXT("not-cast"), OldId, UnknownId)}, Females, Actors, TimeUtc, Out, Result) && Result.Reason == TEXT("new_id_not_selected"), TEXT("a new id outside the selected females is rejected"));
 TArray<FOKStreetRetargetRequest> TooMany;
 for (int32 Index = 0; Index < OKStreetMaxRetargetRequests + 1; ++Index) TooMany.Add(Request(FString::Printf(TEXT("many-%d"), Index), OKStreetTestApng(TEXT('1')), OKStreetTestApng(TEXT('2'))));
 OKStreetExpect(Failures, !OKStreetRetargetRelations(Store, TooMany, Females, Actors, TimeUtc, Out, Result) && Result.Reason == TEXT("retarget_too_many") && OKStreetWriteSave(Out) == LegacyBody, TEXT("more than five requests are refused before publish"));
 const FString Blocker = FPaths::ProjectSavedDir() / TEXT("StreetSocial") / FString::Printf(TEXT("retarget-blocker-%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits));
 OKStreetExpect(Failures, FFileHelper::SaveStringToFile(TEXT("x"), *Blocker), TEXT("atomic-save failure fixture is an isolated file"));
 const FString BlockedPath = Blocker / TEXT("relationships.json");
 FOKStreetStore Candidate;
 OKStreetExpect(Failures, OKStreetRetargetRelations(Store, Move, Females, Actors, TimeUtc, Candidate, Result) && Result.Applied, TEXT("a candidate can be built before the atomic save"));
 const FString Untouched = OKStreetWriteSave(Published);
 OKStreetExpect(Failures, !OKStreetAtomicSave(BlockedPath, OKStreetWriteSave(Candidate), SaveError) && !IFileManager::Get().FileExists(*BlockedPath) && OKStreetWriteSave(Published) == Untouched, TEXT("a failed atomic save does not replace the published store"));
 FString BlockerBody;
 OKStreetExpect(Failures, FFileHelper::LoadFileToString(BlockerBody, *Blocker) && BlockerBody == TEXT("x"), TEXT("a failed atomic save leaves the blocked path unchanged"));
 FOKStreetManifestSocial Manifest;
 const FString ManifestJson = FString::Printf(TEXT("{\"characters\":[{\"id\":\"%s\",\"sex\":0},{\"id\":\"%s\",\"sex\":1}],\"identity_retargets\":[{\"request_id\":\"retarget-selftest-1\",\"old_id\":\"%s\",\"new_id\":\"%s\"}]}"), *MaleId, *NewId, *OldId, *NewId);
 OKStreetExpect(Failures, OKStreetReadManifestSocial(TEXT("C:/manifest.json"), ManifestJson, Manifest) && Manifest.IdentityRetargets.Num() == 1 && Manifest.IdentityRetargets[0].RequestId == TEXT("retarget-selftest-1") && Manifest.IdentityRetargets[0].OldId == OldId && Manifest.IdentityRetargets[0].NewId == NewId, TEXT("a manifest accepts only the explicit retarget fields"));
 FOKStreetManifestSocial Absent;
 OKStreetExpect(Failures, OKStreetReadManifestSocial(TEXT("C:/manifest.json"), TEXT("{\"characters\":[]}"), Absent) && Absent.IdentityRetargets.Num() == 0 && Absent.Error.IsEmpty(), TEXT("a manifest without identity_retargets stays valid"));
 FOKStreetManifestSocial Extra;
 OKStreetExpect(Failures, !OKStreetReadManifestSocial(TEXT("C:/manifest.json"), FString::Printf(TEXT("{\"identity_retargets\":[{\"request_id\":\"r1\",\"old_id\":\"%s\",\"new_id\":\"%s\",\"path\":\"C:/card.png\"}]}"), *OldId, *NewId), Extra) && Extra.Error == TEXT("identity_retarget_field_unknown"), TEXT("a retarget request cannot smuggle a path"));
 FOKStreetManifestSocial TooManyManifest;
 OKStreetExpect(Failures, !OKStreetReadManifestSocial(TEXT("C:/manifest.json"), FString::Printf(TEXT("{\"identity_retargets\":[{\"request_id\":\"a\",\"old_id\":\"%s\",\"new_id\":\"%s\"},{\"request_id\":\"b\",\"old_id\":\"%s\",\"new_id\":\"%s\"},{\"request_id\":\"c\",\"old_id\":\"%s\",\"new_id\":\"%s\"},{\"request_id\":\"d\",\"old_id\":\"%s\",\"new_id\":\"%s\"},{\"request_id\":\"e\",\"old_id\":\"%s\",\"new_id\":\"%s\"},{\"request_id\":\"f\",\"old_id\":\"%s\",\"new_id\":\"%s\"}]}"), *OldId, *NewId, *OldId, *NewId, *OldId, *NewId, *OldId, *NewId, *OldId, *NewId, *OldId, *NewId), TooManyManifest) && TooManyManifest.Error == TEXT("identity_retargets_too_many"), TEXT("a manifest retarget array is capped at five"));
 OKStreetCheckActiveActorInterrupt(Failures);
}

inline void OKStreetCheckTalkCandidateEligibility(TArray<FString>& Failures) {
 auto Sample = [](int32 Slot, float Distance, bool Clear) {
  FOKStreetTalkCandidateSample Row;
  Row.Slot = Slot;
  Row.DistanceCm = Distance;
  Row.LineOfSightClear = Clear;
  Row.Queried = true;
  Row.Ready = true;
  Row.Visible = true;
  Row.ActionsReady = true;
  Row.Female = true;
  return Row;
 };
 const FOKStreetTalkCandidateSample Ready = Sample(1, 80.f, true);
 OKStreetExpect(Failures, OKStreetTalkCandidateEligible(Ready), TEXT("a ready visible female inside range with clear sight is eligible"));
 TArray<FOKStreetTalkCandidateSample> Blocked;
 Blocked.Add(Sample(1, 40.f, false));
 Blocked.Add(Sample(3, 180.f, true));
 OKStreetExpect(Failures, OKStreetSelectTalkCandidate(Blocked) == 3, TEXT("a nearer blocked NPC does not beat a clear one"));
 FOKStreetTalkCandidateSample Unready = Sample(1, 40.f, true);
 Unready.ActionsReady = false;
 TArray<FOKStreetTalkCandidateSample> Waiting;
 Waiting.Add(Unready);
 Waiting.Add(Sample(2, 200.f, true));
 OKStreetExpect(Failures, !OKStreetTalkCandidateEligible(Unready) && OKStreetSelectTalkCandidate(Waiting) == 2, TEXT("an actions-unready NPC is not the E target"));
 FOKStreetTalkCandidateSample Loading = Sample(1, 40.f, true);
 Loading.Ready = false;
 OKStreetExpect(Failures, !OKStreetTalkCandidateEligible(Loading), TEXT("a loading actor is not eligible"));
 FOKStreetTalkCandidateSample Hidden = Sample(1, 40.f, true);
 Hidden.Visible = false;
 OKStreetExpect(Failures, !OKStreetTalkCandidateEligible(Hidden), TEXT("a hidden or fading actor is not eligible"));
 FOKStreetTalkCandidateSample Broken = Sample(1, 40.f, true);
 Broken.HasError = true;
 OKStreetExpect(Failures, !OKStreetTalkCandidateEligible(Broken), TEXT("a runtime error is not eligible"));
 FOKStreetTalkCandidateSample Stale = Sample(1, 40.f, true);
 Stale.Stale = true;
 OKStreetExpect(Failures, !OKStreetTalkCandidateEligible(Stale), TEXT("a stale candidate sample is not eligible"));
 FOKStreetTalkCandidateSample Missing = Sample(1, 40.f, true);
 Missing.Queried = false;
 OKStreetExpect(Failures, !OKStreetTalkCandidateEligible(Missing), TEXT("a missed status query is not eligible"));
 FOKStreetTalkCandidateSample Male = Sample(0, 40.f, true);
 Male.Female = true;
 FOKStreetTalkCandidateSample NotFemale = Sample(1, 40.f, true);
 NotFemale.Female = false;
 OKStreetExpect(Failures, !OKStreetTalkCandidateEligible(Male) && !OKStreetTalkCandidateEligible(NotFemale), TEXT("invalid female metadata is not eligible"));
 OKStreetExpect(Failures, !OKStreetTalkCandidateEligible(Sample(1, OKStreetTalkRangeCm, true)) && !OKStreetTalkCandidateEligible(Sample(1, OKStreetTalkRangeCm + 1.f, true)), TEXT("the talk range stays a strict upper bound"));
 TArray<FOKStreetTalkCandidateSample> Tied;
 Tied.Add(Sample(2, 100.f, true));
 Tied.Add(Sample(4, 100.f, true));
 OKStreetExpect(Failures, OKStreetSelectTalkCandidate(Tied) == 2, TEXT("equal distance keeps the earlier candidate"));
 const FString Saved = OKStreetFormatSavedTopicResult(74.f, 75.f, 1.5f, 1.5f);
 OKStreetExpect(Failures, Saved == OKT(TEXT("ui.rules.saved_social_example_topics")) && !Saved.Contains(TEXT("+10")), TEXT("saved feedback uses the actual favor delta, not an assumed row"));
 OKStreetExpect(Failures, OKStreetFavorIncreased(74.f, 75.f) && !OKStreetFavorIncreased(75.f, 75.f) && !OKStreetFavorIncreased(10.f, 9.f), TEXT("only an actual favor increase counts"));
 OKStreetExpect(Failures, OKStreetTopicCommitsFavor(TEXT("daily")) && OKStreetTopicCommitsFavor(TEXT("romance")) && OKStreetTopicCommitsFavor(TEXT("intimacy")), TEXT("voiced topics can commit"));
 OKStreetExpect(Failures, !OKStreetTopicCommitsFavor(TEXT("greeting")) && !OKStreetTopicCommitsFavor(TEXT("farewell")) && !OKStreetTopicCommitsFavor(TEXT("relationship")) && !OKStreetTopicCommitsFavor(FString()), TEXT("greeting, farewell, and cancel categories do not commit favor"));
 const EOKStreetAudioCompletion OpenStates[] = {EOKStreetAudioCompletion::Idle, EOKStreetAudioCompletion::Preparing, EOKStreetAudioCompletion::Playing, EOKStreetAudioCompletion::Cancelled, EOKStreetAudioCompletion::Failed};
 for (EOKStreetAudioCompletion State : OpenStates) OKStreetExpect(Failures, !OKStreetVoicedTopicMayCommit(State), TEXT("only completed audio may commit a voiced topic"));
 OKStreetExpect(Failures, OKStreetVoicedTopicMayCommit(EOKStreetAudioCompletion::Completed) && FCString::Strcmp(OKStreetAudioCompletionName(EOKStreetAudioCompletion::Completed), TEXT("Completed")) == 0 && FCString::Strcmp(OKStreetAudioCompletionName(EOKStreetAudioCompletion::Cancelled), TEXT("Cancelled")) == 0, TEXT("completed audio provenance is the Completed token"));
}

inline void OKStreetCheckTalkCandidateCamera(TArray<FString>& Failures) {
 auto Sample = [](int32 Slot, float Distance, float X, float Y, bool Clear) {
  FOKStreetTalkCandidateSample Row;
  Row.Slot = Slot;
  Row.DistanceCm = Distance;
  Row.Offset = FVector(X, Y, 0.f);
  Row.LineOfSightClear = Clear;
  Row.Queried = true;
  Row.Ready = true;
  Row.Visible = true;
  Row.ActionsReady = true;
  Row.Female = true;
  return Row;
 };
 const FVector East = FVector(1.f, 0.f, 0.f);
 const FVector North = FVector(0.f, 1.f, 0.f);
 TArray<FOKStreetTalkCandidateSample> Crowd;
 Crowd.Add(Sample(5, 240.f, 240.f, 0.f, true));
 Crowd.Add(Sample(1, 30.f, 0.f, 30.f, true));
 Crowd.Add(Sample(4, 40.f, -40.f, 0.f, true));
 TArray<int32> Ordered;
 OKStreetOrderTalkCandidateSlots(Crowd, East, Ordered);
 OKStreetExpect(Failures, Ordered.Num() == 3 && Ordered[0] == 5 && Ordered[1] == 1 && Ordered[2] == 4, TEXT("camera front outranks a nearer side and a nearer behind"));
 OKStreetExpect(Failures, OKStreetSelectTalkCandidateByView(Crowd, East) == 5 && OKStreetSelectTalkCandidate(Crowd) == 1, TEXT("the nearest helper still returns the closer side while the view helper returns the front"));
 TArray<FOKStreetTalkCandidateSample> Tied;
 Tied.Add(Sample(4, 100.f, 100.f, 0.f, true));
 Tied.Add(Sample(2, 100.f, 100.f, 1.e-5f, true));
 OKStreetExpect(Failures, OKStreetSelectTalkCandidate(Tied) == 4 && OKStreetSelectTalkCandidateByView(Tied, East) == 2, TEXT("equal alignment and distance keep the lower slot, not the earlier input"));
 TArray<FOKStreetTalkCandidateSample> Epsilon;
 Epsilon.Add(Sample(3, 200.f, 200.f, 0.f, true));
 Epsilon.Add(Sample(1, 50.f, 50.f, 50.f * 1.e-6f, true));
 OKStreetExpect(Failures, OKStreetSelectTalkCandidateByView(Epsilon, East) == 1, TEXT("alignment inside the epsilon loses to the shorter distance"));
 FOKStreetTalkSelection Start;
 const FOKStreetTalkSelectionResult First = OKStreetResolveTalkSelection(Start, Crowd, East, false, false);
 OKStreetExpect(Failures, First.Selection.Selected == 5 && First.Selection.Count == 3 && !First.Selection.Explicit && First.AdmitSlot == INDEX_NONE, TEXT("the automatic choice is the front of the ordered list"));
 const FOKStreetTalkSelectionResult Cycled = OKStreetResolveTalkSelection(First.Selection, Crowd, East, true, false);
 const FOKStreetTalkSelectionResult CycledAgain = OKStreetResolveTalkSelection(Cycled.Selection, Crowd, East, true, false);
 const FOKStreetTalkSelectionResult Wrapped = OKStreetResolveTalkSelection(CycledAgain.Selection, Crowd, East, true, false);
 OKStreetExpect(Failures, Cycled.Selection.Explicit && Cycled.Selection.Selected == 1 && CycledAgain.Selection.Selected == 4 && Wrapped.Selection.Selected == 5 && Wrapped.Selection.Explicit, TEXT("Tab cycles the ordered list and wraps to the front"));
 const FOKStreetTalkSelectionResult Turned = OKStreetResolveTalkSelection(First.Selection, Crowd, North, false, false);
 const FOKStreetTalkSelectionResult Locked = OKStreetResolveTalkSelection(CycledAgain.Selection, Crowd, North, false, false);
 OKStreetExpect(Failures, !Turned.Selection.Explicit && Turned.Selection.Selected == 1 && OKStreetSelectTalkCandidateByView(Crowd, North) == 1 && Locked.Selection.Explicit && Locked.Selection.Selected == 4 && Locked.Selection.Selected != Turned.Selection.Selected, TEXT("a camera change moves only the automatic choice"));
 TArray<FOKStreetTalkCandidateSample> Dropped = Crowd;
 Dropped[0].DistanceCm = OKStreetTalkRangeCm;
 const FOKStreetTalkSelectionResult Invalid = OKStreetResolveTalkSelection(First.Selection, Dropped, East, false, false);
 TArray<int32> Remaining;
 OKStreetOrderTalkCandidateSlots(Dropped, East, Remaining);
 OKStreetExpect(Failures, !Remaining.Contains(5) && !Invalid.Selection.Explicit && Invalid.Selection.Selected == 1 && Invalid.Selection.Count == 2, TEXT("an invalidated automatic slot is excluded"));
 FOKStreetTalkSelection ExplicitFront = First.Selection;
 ExplicitFront.Explicit = true;
 const FOKStreetTalkSelectionResult InvalidExplicit = OKStreetResolveTalkSelection(ExplicitFront, Dropped, East, false, false);
 OKStreetExpect(Failures, InvalidExplicit.Selection.Selected == 1 && !InvalidExplicit.Selection.Explicit && InvalidExplicit.AdmitSlot == INDEX_NONE, TEXT("an invalidated explicit slot is excluded"));
 const FOKStreetTalkSelectionResult Rejected = OKStreetResolveTalkSelection(First.Selection, Dropped, East, true, true);
 OKStreetExpect(Failures, Rejected.RejectedInteract && Rejected.AdmitSlot == INDEX_NONE && Rejected.Selection.Selected == 1 && !Rejected.Selection.Explicit, TEXT("E on an ineligible hint refreshes and does not admit or cycle"));
 const FOKStreetTalkSelectionResult Admitted = OKStreetResolveTalkSelection(First.Selection, Crowd, East, true, true);
 OKStreetExpect(Failures, Admitted.AdmitSlot == 5 && !Admitted.RejectedInteract && Admitted.Selection.Selected == 5, TEXT("E admits the shown front target instead of the nearer side"));
 FOKStreetTalkSelection SideChoice = Cycled.Selection;
 const FOKStreetTalkSelectionResult SideAdmit = OKStreetResolveTalkSelection(SideChoice, Crowd, East, false, true);
 OKStreetExpect(Failures, SideAdmit.AdmitSlot == 1 && SideAdmit.Selection.Selected == 1, TEXT("E admits the explicit side target instead of the camera front"));
 TArray<FOKStreetTalkCandidateSample> Blocked;
 Blocked.Add(Sample(2, 20.f, 20.f, 0.f, false));
 Blocked.Add(Sample(3, 180.f, 0.f, 180.f, true));
 FOKStreetTalkCandidateSample Loading = Sample(1, 15.f, 15.f, 0.f, true);
 Loading.Ready = false;
 Blocked.Add(Loading);
 FOKStreetTalkCandidateSample Male = Sample(0, 10.f, 10.f, 0.f, true);
 Male.Female = true;
 Blocked.Add(Male);
 OKStreetExpect(Failures, OKStreetSelectTalkCandidateByView(Blocked, East) == 3 && !OKStreetTalkCandidateEligible(Loading) && !OKStreetTalkCandidateEligible(Male), TEXT("blocked, loading, and male samples lose to the clear female"));
 TArray<FOKStreetTalkCandidateSample> One;
 One.Add(Sample(4, 90.f, 0.f, -90.f, true));
 const FOKStreetTalkSelectionResult Only = OKStreetResolveTalkSelection(Start, One, East, false, false);
 const FOKStreetTalkSelectionResult OnlyTab = OKStreetResolveTalkSelection(Only.Selection, One, East, true, false);
 OKStreetExpect(Failures, Only.Selection.Selected == 4 && Only.Selection.Count == 1 && OnlyTab.Selection.Selected == 4 && OnlyTab.Selection.Explicit, TEXT("one eligible NPC stays selected and Tab wraps onto it"));
 const FOKStreetTalkSelectionResult None = OKStreetResolveTalkSelection(OnlyTab.Selection, {}, East, true, true);
 OKStreetExpect(Failures, None.Selection.Selected == INDEX_NONE && None.Selection.Count == 0 && None.RejectedInteract && None.AdmitSlot == INDEX_NONE && !None.Selection.Explicit, TEXT("no eligible NPC clears the lock and admits nobody"));
 const FOKStreetTalkViewForward Fallen = OKStreetTalkCameraHorizontal(FVector(0.f, 0.f, -4.f), 90.f);
 const float NotANumber = std::numeric_limits<float>::quiet_NaN();
 const FOKStreetTalkViewForward Unusable = OKStreetTalkCameraHorizontal(FVector(NotANumber, 0.f, 0.f), NotANumber);
 OKStreetExpect(Failures, Fallen.UsedYawFallback && Fallen.Horizontal.Y > 0.9f && FMath::Abs(Fallen.Horizontal.X) < 1.e-3f, TEXT("a vertical camera forward uses the yaw"));
 OKStreetExpect(Failures, Unusable.UsedYawFallback && Unusable.Horizontal.Equals(FVector(1.f, 0.f, 0.f)), TEXT("a non-finite look and yaw fall back to +X"));
 TArray<FOKStreetTalkCandidateSample> Stacked;
 Stacked.Add(Sample(1, 0.f, 0.f, 0.f, true));
 Stacked.Add(Sample(2, 80.f, 0.f, 80.f, true));
 OKStreetExpect(Failures, OKStreetTalkViewAlignment(North, FVector::ZeroVector) == OKStreetTalkAlignDegenerate && OKStreetSelectTalkCandidateByView(Stacked, North) == 2, TEXT("a same-point offset does not outrank a real facing"));
 OKStreetExpect(Failures, OKStreetTalkCandidateHint(TEXT("Aki"), 1) == OKT(TEXT("ui.rules.e_talk_aki")) && OKStreetTalkCandidateHint(TEXT("Aki"), 3) == OKT(TEXT("ui.rules.e_talk_aki_tab")) && OKStreetTalkCandidateHint(TEXT("Aki"), 0).IsEmpty(), TEXT("the hint names the choice and adds Tab only when several are eligible"));
}

inline void OKStreetCheckHintAndSun(TArray<FString>& Failures) {
 FString Body, Hint;
 OKStreetSplitMessageHint(OKStreetFormatSavedTopicResult(74.f, 75.f, 1.5f, 1.5f), Body, Hint);
 OKStreetExpect(Failures, Body == OKT(TEXT("ui.rules.saved_social_example")) && Hint == OKT(TEXT("ui.rules.topic_choice_menu")), TEXT("saved feedback stays in the box, the topic menu moves to the hint"));
 OKStreetSplitMessageHint(OKT(TEXT("ui.rules.topic_choice_menu_semi")), Body, Hint);
 OKStreetExpect(Failures, Body.IsEmpty() && !Hint.IsEmpty(), TEXT("a pure control line is all hint"));
 OKStreetSplitMessageHint(OKT(TEXT("ui.rules.facility_free_look_hint")), Body, Hint);
 OKStreetExpect(Failures, Body == OKT(TEXT("ui.rules.facility_no_favor_hint")) && Hint == OKT(TEXT("ui.rules.free_look_hint")), TEXT("mixed lines keep the story and drop the controls"));
 OKStreetSplitMessageHint(OKT(TEXT("ui.rules.her_topic_rain")), Body, Hint);
 OKStreetExpect(Failures, Body == OKT(TEXT("ui.rules.her_topic_rain")) && Hint.IsEmpty(), TEXT("lines without controls stay whole"));
 OKStreetExpect(Failures, FMath::IsNearlyEqual(OKStreetClockHours(8.0, 600.0), 12.0) && FMath::IsNearlyEqual(OKStreetClockHours(22.0, 1200.0), 6.0), TEXT("ten real minutes run four clock hours and wrap at midnight"));
 const FOKStreetSunState Noon = OKStreetSunAt(12.0), Morning = OKStreetSunAt(9.0), Evening = OKStreetSunAt(15.0), Night = OKStreetSunAt(0.0);
 OKStreetExpect(Failures, Noon.Day && FMath::IsNearlyEqual(Noon.Rotation.Yaw, -30.0) && Noon.Rotation.Pitch < -60.0 && Noon.Intensity > 2.4f, TEXT("noon matches the authored high sun"));
 OKStreetExpect(Failures, Morning.Day && Evening.Day && FMath::IsNearlyEqual(Morning.Rotation.Pitch, Evening.Rotation.Pitch, 1.e-3) && FMath::Abs(FRotator::NormalizeAxis(Evening.Rotation.Yaw - Morning.Rotation.Yaw)) > 60.0, TEXT("morning and afternoon cast shadows to opposite sides"));
 OKStreetExpect(Failures, !Night.Day && Night.Rotation.Pitch < -10.0 && Night.Intensity < 2.f, TEXT("night uses a dim light that still points down"));
}

inline void OKStreetRunScenarios(TArray<FString>& Failures) {
 OKStreetCheckCatalogue(Failures);
 OKStreetCheckRoutes(Failures);
 OKStreetCheckSave(Failures);
 OKStreetCheckTalkCandidateEligibility(Failures);
 OKStreetCheckHintAndSun(Failures);
}
