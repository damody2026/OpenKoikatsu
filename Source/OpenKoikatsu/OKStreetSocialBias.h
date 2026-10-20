#pragma once
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "OKStreetSocialRules.h"

// Plan §4.2a traits, §10 distortion and §10.4 tsundere. The rules (gains, daily drift, mood, the bias and its inner
// line) are in Tools/ok-rules (src/bias.rs, src/mind.rs); FOKStreetBias holds an answer of theirs.

inline float* OKStreetAxis(FOKStreetRelation& R, const FString& Name) {
 for (const auto& A : OKStreetAxisFields(R)) if (A.Key == Name) return A.Value;
 return nullptr;
}

// The strongest jealousy she feels: toward the player, or toward one rival (who it is, empty for the player).
inline float OKStreetPeakJealousy(const FOKStreetRelation& R, FString* Rival = nullptr) {
 float Peak = R.Jealousy;
 if (Rival) Rival->Reset();
 for (const auto& Pair : R.JealousyToward) if (Pair.Value > Peak) { Peak = Pair.Value; if (Rival) *Rival = Pair.Key; }
 return Peak;
}

struct FOKStreetBias {
 // Perception and recall (plan §10.1): how she reads events and what comes to mind first.
 float Suspicion = 0.f, IdealizePlayer = 0.f, RivalNegativity = 0.f;
 // Decision leanings, 0..1, read by the rules that make choices (pending choices, plans, initiative).
 float DeferToPlayer = 0.f, Compete = 0.f, Surveil = 0.f, PossessiveDisplay = 0.f, Withdraw = 0.f;
 TArray<FString> Combos;
 // What she shows: equal to the real values unless the tsundere mask applies (plan §10.4).
 float TrueLiking = 0.f, ShownAffection = 0.f, ShownAdmiration = 0.f, ShownJealousy = 0.f; // TrueLiking: her liking (from favor), what a mask hides
 bool Masked = false;
 // Fixed-phrase line for the prompt (never written by the model).
 FString InnerLine;
 // The mood it was computed with, the strongest jealousy and toward whom (empty: him).
 float Mood = 0.f, PeakJealousy = 0.f;
 FString Rival;
};
