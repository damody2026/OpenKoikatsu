#pragma once
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

// Each heroine's own history (Docs/STREET_BEHAVIOR_PLAN_20261012.md): everything that happened between them, in
// order, with the game time and the place — what was said face to face, phone messages and calls, what he did to
// her, outings, appointments, stage changes. One JSON object per line in <save dir>/history/<stable id>.jsonl.
struct FOKStreetHistoryEntry {
 int64 Serial = 0;  // unique and increasing (UTC ticks when written)
 int64 Minute = 0;  // game minute (day * 1440 + minute of day)
 FString Place;     // place name as shown, "" for the street
 FString Kind;      // talk | message | call | call_missed | act | act_refused | event kinds from MindObserve | stage | breakup ...
 FString Who;       // him | her | world
 FString Text;
};

inline FString OKStreetHistoryLine(const FOKStreetHistoryEntry& E) {
 TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
 O->SetStringField(TEXT("serial"), FString::Printf(TEXT("%lld"), E.Serial)); O->SetNumberField(TEXT("minute"), double(E.Minute));
 O->SetStringField(TEXT("place"), E.Place); O->SetStringField(TEXT("kind"), E.Kind); O->SetStringField(TEXT("who"), E.Who); O->SetStringField(TEXT("text"), E.Text);
 FString Out; auto W = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out); FJsonSerializer::Serialize(O, W);
 return Out;
}

inline bool OKStreetHistoryParse(const FString& Line, FOKStreetHistoryEntry& E) {
 TSharedPtr<FJsonObject> O;
 if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Line), O) || !O.IsValid()) return false;
 double V = 0; FString Serial;
 if (O->TryGetStringField(TEXT("serial"), Serial)) E.Serial = FCString::Atoi64(*Serial); else if (O->TryGetNumberField(TEXT("serial"), V)) E.Serial = int64(V);
 if (O->TryGetNumberField(TEXT("minute"), V)) E.Minute = int64(V);
 O->TryGetStringField(TEXT("place"), E.Place); O->TryGetStringField(TEXT("kind"), E.Kind); O->TryGetStringField(TEXT("who"), E.Who); O->TryGetStringField(TEXT("text"), E.Text);
 return !E.Text.IsEmpty();
}

// Kinds that are phone traffic (the phone's thread view).
inline bool OKStreetHistoryIsPhone(const FString& Kind) { return Kind == TEXT("message") || Kind == TEXT("call") || Kind == TEXT("call_missed") || Kind == TEXT("photo"); }
// A photo in the phone thread: its Text is "<photo id>|<png file>|<caption>" (photos.rs).
inline bool OKStreetPhotoEntry(const FString& Text, FString& File, FString& Caption) {
 TArray<FString> Parts; Text.ParseIntoArray(Parts, TEXT("|"), false);
 if (Parts.Num() < 3) return false;
 File = Parts[1]; Caption = FString::Join(TArrayView<const FString>(Parts).RightChop(2), TEXT("|")); return true;
}
