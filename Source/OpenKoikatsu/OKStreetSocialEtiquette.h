#pragma once
#include "OKStreetSocialText.h"
#include "CoreMinimal.h"

// Plan §9 place norms: how private, quiet and formal a place is, whether food is allowed and staff are around. The table
// is a game rule (Tools/ok-rules places.toml), cached here from the rules (OKRulesSyncDefaults) for the scene's own
// displays and checks; how awkward an act is there, the crowd and how she takes it are decided in the rules.
struct FOKStreetPlaceNorms {
 int32 Privacy = 0;      // 0 public, 1 semi-public, 2 lockable, 3 private
 float Quiet = 0.3f;
 float Formality = 0.2f;
 bool FoodAllowed = true;
 bool Staff = false;     // someone who will say something
 FString Label = OKT(TEXT("ui.etiquette.street_location"));
};
struct FOKStreetPlaceNormsTable { TMap<FString, FOKStreetPlaceNorms> Places; FOKStreetPlaceNorms Default; };
inline FOKStreetPlaceNormsTable& OKStreetNormsTable() { static FOKStreetPlaceNormsTable T; return T; }
inline FOKStreetPlaceNorms OKStreetNormsOf(const FString& Place) {
 const auto& T = OKStreetNormsTable();
 if (const auto* Found = T.Places.Find(Place)) return *Found;
 return T.Default;
}

// Plan §4.3 needs, 0..100, for heroines only. How they decay and come back, and the words she lets show, are game rules
// (Tools/ok-rules src/needs.rs, src/mind.rs).
struct FOKStreetNeeds { float Satiety = 80, Energy = 90, Fun = 60, Social = 60, Hygiene = 90; };
