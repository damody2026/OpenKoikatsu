#pragma once

#include "CoreMinimal.h"
#include "Misc/CString.h"
#include "Dom/JsonObject.h"
#include "OKStreetRules.h"
#include <initializer_list>

// The scene's own words in the player's language (Docs/STREET_I18N_PLAN_20261014.md). A sentence is a message id in the
// text tables (StreetSocialData/Localization/<lang>/*.toml, ICU MessageFormat):
//   OKT(TEXT("ui.bank.deposit_min"))                       a message without arguments (const TCHAR*, valid all run)
//   OKMsg(TEXT("ui.bank.matured"), {{TEXT("amount"), A}})   with named arguments (FString)
// One implementation: the rules (ok-rules locale.rs "text", tr.rs, icu.rs) hold the tables, the ICU formatter, the
// region's money and the zh-TW lookup; Unreal only asks. OKT also takes a Traditional Chinese text that is a message of
// zh-TW (labels kept as data in static tables) and gives it in the current language. Before the rules are open a key
// shows as itself (nothing is cached then).

struct FOKArg {
 enum class EKind : uint8 { Str, Int, Num };
 EKind Kind = EKind::Str;
 FString S;
 int64 I = 0;
 double N = 0;
 FOKArg() = default;
 FOKArg(const TCHAR* V) : S(V ? V : TEXT("")) {}
 FOKArg(const FString& V) : S(V) {}
 FOKArg(FString&& V) : S(MoveTemp(V)) {}
 FOKArg(const FText& V) : S(V.ToString()) {}
 FOKArg(int32 V) : Kind(EKind::Int), I(V) {}
 FOKArg(uint32 V) : Kind(EKind::Int), I(V) {}
 FOKArg(int64 V) : Kind(EKind::Int), I(V) {}
 FOKArg(uint64 V) : Kind(EKind::Int), I(int64(V)) {}
 FOKArg(long V) : Kind(EKind::Int), I(V) {}
 FOKArg(unsigned long V) : Kind(EKind::Int), I(int64(V)) {}
 FOKArg(double V) : Kind(EKind::Num), N(V) {}
 FOKArg(float V) : Kind(EKind::Num), N(V) {}
};

struct FOKNamedArg {
 const TCHAR* Name;
 FOKArg Value;
 template <typename T> FOKNamedArg(const TCHAR* InName, T&& InValue) : Name(InName), Value(Forward<T>(InValue)) {}
};

struct FOKStreetCppText {
 FString Lang = TEXT("zh-TW"), Region = TEXT("CN");
 int32 Generation = -1; // the rules' generation the cache was filled from (a reload starts it again)
 TMap<FString, TMap<FString, FString>*> Plain; // "lang|region" -> key -> text OKT handed out (never freed)
};
inline FOKStreetCppText& OKStreetCppText() { static FOKStreetCppText Text; return Text; }

// The language and region the words are in (the region picks the money in {x, number, ::currency}, {yuan_N}).
inline void OKStreetUseCppLanguage(const FString& Lang, const FString& Region = FString()) {
 FOKStreetCppText& Text = OKStreetCppText(); Text.Lang = Lang; if (!Region.IsEmpty()) Text.Region = Region;
}

inline TSharedRef<FJsonObject> OKStreetTextArgs(std::initializer_list<FOKNamedArg> Args) {
 auto O = MakeShared<FJsonObject>();
 for (const FOKNamedArg& A : Args) {
  if (A.Value.Kind == FOKArg::EKind::Int) O->SetNumberField(A.Name, double(A.Value.I));
  else if (A.Value.Kind == FOKArg::EKind::Num) O->SetNumberField(A.Name, A.Value.N);
  else O->SetStringField(A.Name, A.Value.S);
 }
 return O;
}

// One message from the rules; false when they are not open or did not answer.
inline bool OKStreetRulesText(const TCHAR* Key, const TSharedPtr<FJsonObject>& Args, FString& Out) {
 FOKStreetRules* Rules = OKStreetActiveRules(); if (!Rules || !Rules->IsOpen()) return false;
 const FOKStreetCppText& Text = OKStreetCppText();
 auto Request = MakeShared<FJsonObject>();
 Request->SetStringField(TEXT("rule"), TEXT("text")); Request->SetStringField(TEXT("lang"), Text.Lang); Request->SetStringField(TEXT("region"), Text.Region);
 Request->SetStringField(TEXT("id"), Key); if (Args.IsValid()) Request->SetObjectField(TEXT("args"), Args);
 FString Error; const TSharedPtr<FJsonObject> Answer = Rules->Eval(Request, Error);
 return Answer.IsValid() && Answer->TryGetStringField(TEXT("text"), Out);
}

// A message without arguments, or a zh-TW text that is one, in the current language; anything else as it is.
inline const TCHAR* OKT(const TCHAR* Key) {
 if (!Key || !*Key) return Key;
 FOKStreetCppText& Text = OKStreetCppText(); FOKStreetRules* Rules = OKStreetActiveRules();
 if (!Rules || !Rules->IsOpen()) return Key;
 if (Text.Generation != Rules->Generation()) { Text.Generation = Rules->Generation(); Text.Plain.Reset(); } // old maps stay (pointers handed out)
 TMap<FString, FString>*& Map = Text.Plain.FindOrAdd(Text.Lang + TEXT("|") + Text.Region);
 if (!Map) Map = new TMap<FString, FString>(); // leaked on purpose (see above)
 if (const FString* Hit = Map->Find(Key)) return **Hit;
 FString Out; if (!OKStreetRulesText(Key, nullptr, Out)) return Key;
 return *Map->Add(Key, MoveTemp(Out));
}

inline FString OKMsg(const TCHAR* Key, std::initializer_list<FOKNamedArg> Args) {
 FString Out; if (OKStreetRulesText(Key, OKStreetTextArgs(Args), Out)) return Out;
 return Key;
}

// An amount in the region's money ("NT$1,200", "¥45", "$1.99"), for code that shows money outside a message.
inline FString OKStreetMoneyText(int64 Amount) { return OKMsg(TEXT("{amount, number, ::currency}"), {{TEXT("amount"), Amount}}); }

// printf for the few formats that are not messages (kept from before the tables).
inline FString OKFormat(const TCHAR* Fmt, ...) {
 TCHAR Buffer[8192];
 va_list Args;
 va_start(Args, Fmt);
 const TCHAR* Format = Fmt;
 const int32 N = FCString::GetVarArgs(Buffer, UE_ARRAY_COUNT(Buffer), Format, Args);
 va_end(Args);
 if (N < 0) Buffer[UE_ARRAY_COUNT(Buffer) - 1] = 0;
 return FString(Buffer);
}
