#pragma once
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Math/RandomStream.h"
#include "OKStreetSocialEconomy.h"

// Plan §6.1: each heroine's day (written by the rules, Tools/ok-rules src/plan.rs). Blocks are game minutes of the day (0..1440+, a block may
// run past midnight). Place "" means the street (her usual walking route).
struct FOKStreetPlanBlock {
 int32 Start = 0, End = 0;
 FString Place, Activity, Kind, Source, JobId; // kind: work|class|meal|errand|leisure|social|rest|sleep|promise|encounter
 int32 Budget = 0;
};

inline FString OKStreetProfileString(const TSharedPtr<FJsonObject>& Profile, const TCHAR* Section, const TCHAR* Field) {
 FString Out; const TSharedPtr<FJsonObject>* S = nullptr;
 if (!Profile.IsValid()) return Out;
 if (!Section) { Profile->TryGetStringField(Field, Out); return Out; }
 if (Profile->TryGetObjectField(Section, S)) (*S)->TryGetStringField(Field, Out);
 return Out;
}
