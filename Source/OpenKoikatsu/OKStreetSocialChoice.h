#pragma once
#include "OKStreetSocialText.h"
#include "CoreMinimal.h"
#include "Templates/Function.h"

// Plan §7.1 "talking is acting": a decision both sides take part in (checkout, gift, loan, invite, enter, stay...).
// The menu and the chat resolve the same choice; the model only reports what the player said (resolve) or what
// she proposes (propose); the outcome is decided by the rules.
struct FOKStreetPendingChoice {
 FString Id;            // "c12"
 FString Kind;          // checkout | order | gift | loan | invite | enter | stay | split_food | ...
 FString Label;         // shown to the model and the player: "結帳 ¥180（2 杯飲料）"
 TArray<FString> Options;
 int32 Heroine = INDEX_NONE; // the other side
 FString HerLean;       // the option her rules prefer (empty: no preference)
 float HerInsist = 0.f; // 0..1, how hard she pushes for HerLean
 int32 Round = 0;       // pushbacks so far; at most two, then the stronger side wins
 double Deadline = 0;   // scene seconds; past it her lean is taken
 int32 HisLinesAtOpen = 0; // his lines to her when it opened: a pick of his must come from something he said after
 int64 SentAtOpen = 0;  // talk requests sent when it opened: it waits for her answer to a later one (she knew of it)
 double OpenedAt = 0;   // scene seconds
 FString Place;         // a bill: the shop it is for (left before it is settled, nothing is bought)
 TFunction<void(const FString& Option)> OnResolve;
};

// Negotiating a pick (her pushback, who wins) is a game rule (Tools/ok-rules src/mind.rs negotiate).

// The block for the prompt: one line per open choice.
inline FString OKStreetChoiceBlock(const TArray<FOKStreetPendingChoice>& Choices, double Now) {
 TArray<FString> Lines;
 for (const auto& C : Choices) {
  Lines.Add(OKMsg(TEXT("ui.choice.choice_prompt_line"), {{TEXT("choice_id"), *C.Id}, {TEXT("choice_label"), *C.Label}, {TEXT("options"), *FString::Join(C.Options, TEXT(" / "))}, {TEXT("her_lean_part"), C.HerLean.IsEmpty() ? TEXT("") : *OKMsg(TEXT("ui.choice.her_lean_suffix"), {{TEXT("lean_label"), *C.HerLean}, {TEXT("insist_percent"), C.HerInsist * 100.f}})}, {TEXT("deadline_part"), C.Deadline > Now ? *OKMsg(TEXT("ui.choice.decide_seconds_suffix"), {{TEXT("seconds"), FMath::CeilToInt32(C.Deadline - Now)}}) : TEXT("")}}));
 }
 return FString::Join(Lines, TEXT("\n"));
}
