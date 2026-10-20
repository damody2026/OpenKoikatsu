#pragma once
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

// ok_rules.dll (Tools/ok-rules), the StreetSocial game rules, loaded into this process at runtime. The rules keep no
// state: each Eval carries the facts and answers with what changes, so Reload can swap in a freshly built DLL while the
// game runs. Without the DLL the street cannot run (OKStreetRulesFatal).
class FOKStreetRules {
public:
 ~FOKStreetRules();
 bool Open(FString& Error);
 void Close();
 bool IsOpen() const { return Handle != nullptr; }
 const FString& DllPath() const { return LoadedFrom; }
 // Bumped on every open and reload: callers caching values from the rules re-read them when it changes.
 int32 Generation() const { return Gen; }
 // Request {"kind": ...}; null on failure (Error says why).
 TSharedPtr<FJsonObject> Eval(const TSharedRef<FJsonObject>& Request, FString& Error);
 // Swaps in the built DLL when it changed since it was loaded, else re-reads the tuning files. Out: what happened.
 bool Reload(FString& Out, FString& Error);

 // Where the DLL is looked for: OK_RULES_DLL, next to the executable, then the project's Tools/ok-rules build.
 static TArray<FString> Candidates();
 // The tuning folder: OK_RULES_TUNING, else the project's StreetSocialData/rules (empty: the DLL's built-in copy).
 static FString TuningDir();

 struct FApi;

private:
 bool OpenWith(FApi* NewApi, const FString& Path, FString& Error);
 FString TakeError();
 FApi* Api = nullptr;
 void* Handle = nullptr;
 // ReloadDir: where hot-reload copies go, beside the DLL first opened (not beside the last copy).
 FString LoadedFrom, SourcePath, ReloadDir;
 FDateTime SourceStamp;
 int32 Gen = 0;
};

// The running street scene's rules, for the ok.StreetRules console commands; null when none is open.
void OKStreetSetActiveRules(FOKStreetRules* Rules);
FOKStreetRules* OKStreetActiveRules();

// The rules could not be loaded: shows Error over the game and quits five seconds later.
void OKStreetRulesFatal(const FString& Error);
