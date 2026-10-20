#pragma once
#include "CoreMinimal.h"
#include "OKStreetSocialRules.h"

// Every interaction with a heroine, his or hers (Docs/STREET_SOCIAL_HEROINE_INTERACTION_MODULE.md). The scene's single
// entry AOKStreetSocialScene::Interact (OKStreetSocialHeroine.inl) runs each through the same steps: may it happen (gate)
// → does she agree (consent) → how she takes it here (reaction) → values → her history and memory → rumour → what it
// looks like. The table and those rules are the game rules' (Tools/ok-rules src/heroine.rs, acts.toml); Interact does
// what needs the scene with the answer.

// The parts of the table the scene uses itself (her panel's buttons, whether an id is an interaction), cached from the
// rules (OKRulesSyncDefaults); the full table is acts.toml.
struct FOKHeroineAct {
 FString Id, Cat, Name, Button; // Cat: act | talk | touch | relation | money | gift | activity | ...
 int32 Stage = 0;                // least relationship stage (0 stranger, 1 friend, 2 lover)
 bool FaceToFace = false;        // over the phone it stays words
};
inline TArray<FOKHeroineAct>& OKHeroineActs() { static TArray<FOKHeroineAct> All; return All; }
inline const FOKHeroineAct* OKHeroineFind(const FString& Id) {
 for (const auto& A : OKHeroineActs()) if (Id == A.Id) return &A;
 return nullptr;
}

inline int32 OKHeroineStageIndex(const FString& Stage) {
 return Stage == TEXT("lover") ? 2 : Stage == TEXT("friend") ? 1 : 0;
}

// A request to the module and what came of it.
struct FOKHeroineArgs {
 bool Hers = false;        // her idea (the model chose it, or her impulse): below the gate she just does not
 bool RecordOnly = false;  // values settled elsewhere: only her record (and rumour)
 FString Detail;           // fills the table text's "%s"
 FString Place;            // where (empty: where he is)
 FString RumorDetail;      // fills the rumour's "%s" (empty: Detail)
 float Importance = -1.f;  // overrides the table's memory importance
};
struct FOKHeroineResult {
 bool Known = false, Done = false, Refused = false;
 int32 Reaction = -1;      // EOKStreetAwkwardReaction, when the place mattered
 FString Why;              // gate | consent | remote
};

// Every number of hers the test panel shows and edits ("女主數值"), in a fixed order (the first five are the old sliders).
struct FOKHeroineStat { const TCHAR* Id; const TCHAR* Label; float Max; float (*Get)(const FOKStreetRelation&); void (*Set)(FOKStreetRelation&, float); };
inline const TArray<FOKHeroineStat>& OKHeroineStats() {
 static const TArray<FOKHeroineStat> All = {
  {TEXT("favor"), TEXT("好感度"), 200.f, [](const FOKStreetRelation& R) { return R.Favor; }, [](FOKStreetRelation& R, float V) { R.Favor = V; }},
  {TEXT("lewdness"), TEXT("興奮度"), 100.f, [](const FOKStreetRelation& R) { return R.Lewdness; }, [](FOKStreetRelation& R, float V) { R.Lewdness = V; }},
  {TEXT("sex_desire"), TEXT("欲望"), 100.f, [](const FOKStreetRelation& R) { return R.SexDesire; }, [](FOKStreetRelation& R, float V) { R.SexDesire = V; }},
  {TEXT("topic_points"), TEXT("話題點"), 100.f, [](const FOKStreetRelation& R) { return R.TopicPoints; }, [](FOKStreetRelation& R, float V) { R.TopicPoints = V; }},
  {TEXT("talks"), TEXT("可談次數"), 20.f, [](const FOKStreetRelation& R) { return float(R.TalksRemaining); }, [](FOKStreetRelation& R, float V) { R.TalksRemaining = FMath::RoundToInt32(V); }},
  {TEXT("chastity"), TEXT("貞操"), 100.f, [](const FOKStreetRelation& R) { return R.Chastity; }, [](FOKStreetRelation& R, float V) { R.Chastity = V; }},
  {TEXT("stranger_cap"), TEXT("陌生好感上限（攻略難度）"), 100.f, [](const FOKStreetRelation& R) { return float(R.StrangerCap); }, [](FOKStreetRelation& R, float V) { R.StrangerCap = FMath::Clamp(FMath::RoundToInt32(V), 10, 100); }},
  {TEXT("trust"), TEXT("信任"), 100.f, [](const FOKStreetRelation& R) { return R.Trust; }, [](FOKStreetRelation& R, float V) { R.Trust = V; }},
  {TEXT("admiration"), TEXT("崇拜"), 100.f, [](const FOKStreetRelation& R) { return R.Admiration; }, [](FOKStreetRelation& R, float V) { R.Admiration = V; }},
  {TEXT("attraction"), TEXT("吸引"), 100.f, [](const FOKStreetRelation& R) { return R.Attraction; }, [](FOKStreetRelation& R, float V) { R.Attraction = V; }},
  {TEXT("dependency"), TEXT("依賴"), 100.f, [](const FOKStreetRelation& R) { return R.Dependency; }, [](FOKStreetRelation& R, float V) { R.Dependency = V; }},
  {TEXT("mystique"), TEXT("神秘"), 100.f, [](const FOKStreetRelation& R) { return R.Mystique; }, [](FOKStreetRelation& R, float V) { R.Mystique = V; }},
  {TEXT("jealousy"), TEXT("忌妒"), 100.f, [](const FOKStreetRelation& R) { return R.Jealousy; }, [](FOKStreetRelation& R, float V) { R.Jealousy = V; }},
  {TEXT("greed"), TEXT("物慾"), 100.f, [](const FOKStreetRelation& R) { return R.Greed; }, [](FOKStreetRelation& R, float V) { R.Greed = V; }},
  {TEXT("lifestyle_desire"), TEXT("生活欲"), 100.f, [](const FOKStreetRelation& R) { return R.LifestyleDesire; }, [](FOKStreetRelation& R, float V) { R.LifestyleDesire = V; }},
  {TEXT("respect"), TEXT("尊重"), 100.f, [](const FOKStreetRelation& R) { return R.Respect; }, [](FOKStreetRelation& R, float V) { R.Respect = V; }},
  {TEXT("resentment"), TEXT("怨懟"), 100.f, [](const FOKStreetRelation& R) { return R.Resentment; }, [](FOKStreetRelation& R, float V) { R.Resentment = V; }},
  {TEXT("satiety"), TEXT("飽足"), 100.f, [](const FOKStreetRelation& R) { return R.Satiety; }, [](FOKStreetRelation& R, float V) { R.Satiety = V; }},
  {TEXT("energy"), TEXT("精神"), 100.f, [](const FOKStreetRelation& R) { return R.Energy; }, [](FOKStreetRelation& R, float V) { R.Energy = V; }},
  {TEXT("fun"), TEXT("樂趣"), 100.f, [](const FOKStreetRelation& R) { return R.Fun; }, [](FOKStreetRelation& R, float V) { R.Fun = V; }},
  {TEXT("social"), TEXT("社交"), 100.f, [](const FOKStreetRelation& R) { return R.Social; }, [](FOKStreetRelation& R, float V) { R.Social = V; }},
  {TEXT("hygiene"), TEXT("清潔"), 100.f, [](const FOKStreetRelation& R) { return R.Hygiene; }, [](FOKStreetRelation& R, float V) { R.Hygiene = V; }},
  {TEXT("perceived_wealth"), TEXT("她眼中你的財力"), 100.f, [](const FOKStreetRelation& R) { return R.PerceivedWealth; }, [](FOKStreetRelation& R, float V) { R.PerceivedWealth = V; }},
  {TEXT("cash"), TEXT("她的錢包"), 50000.f, [](const FOKStreetRelation& R) { return float(FMath::Max<int64>(0, R.Cash)); }, [](FOKStreetRelation& R, float V) { R.Cash = int64(V); }},
 };
 return All;
}
