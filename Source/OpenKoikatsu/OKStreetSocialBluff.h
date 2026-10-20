#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

// The card game 吹牛 (Cheat / I doubt it) played by him and 2-5 heroines indoors (OKStreetSocialBluff.inl).
// The game itself is a rule (ok-rules bluff.rs bluff_step): Table is its state, passed in with each action and replaced by
// the answer; the fields below it are copies for the table on screen. Unreal keeps the clock and the voices.
// Cards 0..51: rank = card % 13 (A..K), suit = card / 13. Seat 0 is him.
struct FOKStreetBluff {
 bool Active = false;
 TSharedPtr<FJsonObject> Table;     // bluff.rs Table
 TArray<int32> Seats;               // character index per seat (0 = him)
 TArray<TArray<int32>> Hands;       // per seat (copied from Table)
 int32 PileCount = 0;
 int32 Turn = 0;                    // seat whose turn it is
 int32 Rank = -1;                   // the round's rank to claim; -1: the one whose turn it is leads and names any rank
 int32 ClaimRank = -1;              // the rank he picked to claim when he leads
 int32 LastSeat = INDEX_NONE;       // the last play: who, what was claimed, how many
 int32 LastRank = 0;
 int32 LastCount = 0;
 int32 RoundClaimed = 0;            // cards claimed so far in this round (more than 4: somebody lied)
 bool Window = false;               // the few seconds after a play (until WindowUntil) in which the heroines may call it
 bool Open = false;                 // the last play can still be called (until the next play covers it)
 double WindowUntil = 0, NextAt = 0, WindowLength = 5; // WindowUntil 0: the claim is still being said
 TMap<int32, double> ChallengeAt;   // seat -> when that heroine calls it (decided by the rule when the play goes down)
 TSet<int32> Selected;              // his chosen cards
 TArray<FString> Log;
 TArray<int32> Caught;              // per seat: times caught lying
 FVector Where = FVector::ZeroVector;
 FString Place;
 int32 Rounds = 0;
 bool Intro = false;                // the opening lines before the first play
 double IntroUntil = 0, SpeakUntil = 0, EndedAt = -1000;
 double ClockRate = 1;              // the street clock's speed while playing (bluff.rs CLOCK_RATE)
};

inline FString OKBluffRankName(int32 Rank) {
 static const TCHAR* Names[] = { TEXT("A"), TEXT("2"), TEXT("3"), TEXT("4"), TEXT("5"), TEXT("6"), TEXT("7"), TEXT("8"), TEXT("9"), TEXT("10"), TEXT("J"), TEXT("Q"), TEXT("K") };
 return Names[((Rank % 13) + 13) % 13];
}
inline FString OKBluffSuitName(int32 Card) {
 static const TCHAR* Suits[] = { TEXT("♠"), TEXT("♥"), TEXT("♦"), TEXT("♣") };
 return Suits[(Card / 13) % 4];
}
inline FString OKBluffCardName(int32 Card) { return OKBluffSuitName(Card) + OKBluffRankName(Card % 13); }
