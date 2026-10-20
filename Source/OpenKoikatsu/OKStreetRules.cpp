#include "OKStreetRules.h"
#include "OKStreetSocialText.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Text/STextBlock.h"

// Must match OKR_ABI_VERSION in Tools/ok-rules/include/ok_rules.h.
static constexpr uint32 OKRulesAbiVersion = 1;

struct FOKStreetRules::FApi {
 using FAbi = uint32 (*)();
 using FOpen = void* (*)(const char*);
 using FEval = const char* (*)(void*, const char*);
 using FReload = int32 (*)(void*);
 using FFree = void (*)(const char*);
 using FLastError = const char* (*)();
 using FClose = void (*)(void*);
 FAbi Abi = nullptr; FOpen Open = nullptr; FEval Eval = nullptr; FReload Reload = nullptr;
 FFree Free = nullptr; FLastError LastError = nullptr; FClose Close = nullptr;

 // Taking an owned copy of a Rust string, released immediately.
 FString Take(const char* Text) const {
  if (!Text) return FString();
  FString Out = UTF8_TO_TCHAR(Text);
  Free(Text);
  return Out;
 }
};

// A loaded DLL is never unloaded: a swapped-out copy may still be on some stack this frame, and they are small.
static FOKStreetRules::FApi* OKStreetRulesLoadApi(const FString& Path, FString& Error) {
 static TMap<FString, FOKStreetRules::FApi*> Loaded;
 if (auto** Found = Loaded.Find(Path)) return *Found;
 void* Dll = FPlatformProcess::GetDllHandle(*Path);
 if (!Dll) { Error = TEXT("ok_rules.dll could not be loaded: ") + Path; return nullptr; }
 auto* Api = new FOKStreetRules::FApi();
 auto Get = [&](const TCHAR* Name) { return FPlatformProcess::GetDllExport(Dll, Name); };
 Api->Abi = (FOKStreetRules::FApi::FAbi)Get(TEXT("okr_abi_version"));
 Api->Open = (FOKStreetRules::FApi::FOpen)Get(TEXT("okr_open"));
 Api->Eval = (FOKStreetRules::FApi::FEval)Get(TEXT("okr_eval"));
 Api->Reload = (FOKStreetRules::FApi::FReload)Get(TEXT("okr_reload"));
 Api->Free = (FOKStreetRules::FApi::FFree)Get(TEXT("okr_free"));
 Api->LastError = (FOKStreetRules::FApi::FLastError)Get(TEXT("okr_last_error"));
 Api->Close = (FOKStreetRules::FApi::FClose)Get(TEXT("okr_close"));
 if (!Api->Abi || !Api->Open || !Api->Eval || !Api->Reload || !Api->Free || !Api->LastError || !Api->Close) {
  Error = TEXT("ok_rules.dll is missing exports: ") + Path;
  delete Api;
  return nullptr;
 }
 if (Api->Abi() != OKRulesAbiVersion) {
  Error = FString::Printf(TEXT("ok_rules.dll ABI %u, expected %u: %s"), Api->Abi(), OKRulesAbiVersion, *Path);
  delete Api;
  return nullptr;
 }
 Loaded.Add(Path, Api);
 return Api;
}

static FString OKStreetRulesJson(const TSharedRef<FJsonObject>& Object) {
 FString Json;
 FJsonSerializer::Serialize(Object, TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Json));
 return Json;
}

TArray<FString> FOKStreetRules::Candidates() {
 TArray<FString> Out;
 const FString Env = FPlatformMisc::GetEnvironmentVariable(TEXT("OK_RULES_DLL"));
 if (!Env.IsEmpty()) Out.Add(Env);
 Out.Add(FPaths::ConvertRelativePathToFull(FPaths::Combine(FPlatformProcess::BaseDir(), TEXT("ok_rules.dll"))));
 Out.Add(FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("Tools/ok-rules/target/release/ok_rules.dll"))));
 return Out;
}

FString FOKStreetRules::TuningDir() {
 const FString Env = FPlatformMisc::GetEnvironmentVariable(TEXT("OK_RULES_TUNING"));
 if (!Env.IsEmpty()) return Env;
 const FString Project = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectContentDir(), TEXT("IndependentResources/StreetSocialData/rules")));
 return FPaths::DirectoryExists(Project) ? Project : FString();
}

// The cargo output Reload watches: OK_RULES_SOURCE_DLL, else the project's release build.
static FString OKStreetRulesSource() {
 const FString Env = FPlatformMisc::GetEnvironmentVariable(TEXT("OK_RULES_SOURCE_DLL"));
 return !Env.IsEmpty() ? Env : FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("Tools/ok-rules/target/release/ok_rules.dll")));
}

FOKStreetRules::~FOKStreetRules() { Close(); }

FString FOKStreetRules::TakeError() {
 return Api ? Api->Take(Api->LastError()) : FString();
}

bool FOKStreetRules::OpenWith(FApi* NewApi, const FString& Path, FString& Error) {
 TSharedRef<FJsonObject> Config = MakeShared<FJsonObject>();
 Config->SetStringField(TEXT("tuning_dir"), TuningDir());
 void* NewHandle = NewApi->Open(TCHAR_TO_UTF8(*OKStreetRulesJson(Config)));
 if (!NewHandle) { Error = TEXT("ok_rules open failed: ") + NewApi->Take(NewApi->LastError()); return false; }
 Close();
 Api = NewApi; Handle = NewHandle; LoadedFrom = Path; ++Gen;
 return true;
}

bool FOKStreetRules::Open(FString& Error) {
 Close();
 FApi* Found = nullptr; FString Path;
 for (const FString& Candidate : Candidates()) {
  if (!FPaths::FileExists(Candidate)) continue;
  Found = OKStreetRulesLoadApi(Candidate, Error);
  if (Found) { Path = Candidate; break; }
 }
 if (!Found) { if (Error.IsEmpty()) Error = TEXT("ok_rules.dll not found (build it: cargo build --release in Tools/ok-rules)"); return false; }
 if (!OpenWith(Found, Path, Error)) return false;
 SourcePath = OKStreetRulesSource();
 SourceStamp = IFileManager::Get().GetTimeStamp(*SourcePath);
 // Copies left by earlier hot reloads; the ones still loaded by another process stay.
 TArray<FString> Old;
 ReloadDir = FPaths::Combine(FPaths::GetPath(LoadedFrom), TEXT("reload"));
 IFileManager::Get().FindFiles(Old, *FPaths::Combine(ReloadDir, TEXT("*.dll")), true, false);
 for (const FString& Name : Old) IFileManager::Get().Delete(*FPaths::Combine(ReloadDir, Name), false, false, true);
 return true;
}

void FOKStreetRules::Close() {
 if (Handle && Api) Api->Close(Handle);
 Handle = nullptr;
}

TSharedPtr<FJsonObject> FOKStreetRules::Eval(const TSharedRef<FJsonObject>& Request, FString& Error) {
 if (!Handle) { Error = TEXT("ok_rules not open"); return nullptr; }
 // A request built without the locale still gets the game's: the rules would answer in their fallback language.
 if (!Request->HasField(TEXT("lang"))) Request->SetStringField(TEXT("lang"), OKStreetCppText().Lang);
 if (!Request->HasField(TEXT("region"))) Request->SetStringField(TEXT("region"), OKStreetCppText().Region);
 const char* Text = Api->Eval(Handle, TCHAR_TO_UTF8(*OKStreetRulesJson(Request)));
 if (!Text) { Error = TakeError(); return nullptr; }
 const FString Body = Api->Take(Text);
 TSharedPtr<FJsonObject> Out;
 if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Body), Out) || !Out.IsValid()) { Error = TEXT("ok_rules returned invalid JSON: ") + Body.Left(200); return nullptr; }
 return Out;
}

bool FOKStreetRules::Reload(FString& Out, FString& Error) {
 if (!Handle) { Error = TEXT("ok_rules not open"); return false; }
 const FDateTime Stamp = IFileManager::Get().GetTimeStamp(*SourcePath);
 if (Stamp != FDateTime::MinValue() && Stamp != SourceStamp && !FPaths::IsSamePath(SourcePath, LoadedFrom)) {
  // A new build: load a copy under a fresh name (Windows keeps the loaded one locked, and cargo must stay free to write).
  const FString Copy = FPaths::Combine(ReloadDir, FString::Printf(TEXT("ok_rules-%lld.dll"), FDateTime::UtcNow().GetTicks()));
  if (IFileManager::Get().Copy(*Copy, *SourcePath) != COPY_OK) { Error = TEXT("could not copy ") + SourcePath + TEXT(" to ") + Copy; return false; }
  FApi* NewApi = OKStreetRulesLoadApi(Copy, Error);
  if (!NewApi || !OpenWith(NewApi, Copy, Error)) return false;
  SourceStamp = Stamp;
  Out = TEXT("dll swapped: ") + Copy;
  return true;
 }
 if (!Api->Reload(Handle)) { Error = TakeError(); return false; }
 ++Gen;
 Out = TEXT("tuning reloaded");
 return true;
}

static FOKStreetRules* GOKStreetActiveRules = nullptr;
void OKStreetSetActiveRules(FOKStreetRules* Rules) { GOKStreetActiveRules = Rules; }
FOKStreetRules* OKStreetActiveRules() { return GOKStreetActiveRules; }

static void OKStreetRulesConsoleReload() {
 if (!GOKStreetActiveRules) { UE_LOG(LogTemp, Display, TEXT("StreetSocial rules: not open")); return; }
 FString Out, Error;
 if (GOKStreetActiveRules->Reload(Out, Error)) { UE_LOG(LogTemp, Display, TEXT("StreetSocial rules: %s"), *Out); }
 else { UE_LOG(LogTemp, Warning, TEXT("StreetSocial rules reload failed, keeping the previous rules: %s"), *Error); }
}

static void OKStreetRulesConsoleStatus() {
 if (!GOKStreetActiveRules) { UE_LOG(LogTemp, Display, TEXT("StreetSocial rules: not open")); return; }
 TSharedRef<FJsonObject> Request = MakeShared<FJsonObject>();
 Request->SetStringField(TEXT("rule"), TEXT("status"));
 FString Error;
 const TSharedPtr<FJsonObject> Status = GOKStreetActiveRules->Eval(Request, Error);
 const FString Text = Status.IsValid() ? OKStreetRulesJson(Status.ToSharedRef()) : Error;
 UE_LOG(LogTemp, Display, TEXT("StreetSocial rules: %s %s"), *GOKStreetActiveRules->DllPath(), *Text);
}

static FAutoConsoleCommand GOKStreetRulesReload(TEXT("ok.StreetRules.Reload"),
 TEXT("StreetSocial rules: swap in a newer Tools/ok-rules build, or re-read the tuning files."),
 FConsoleCommandDelegate::CreateStatic(&OKStreetRulesConsoleReload));
static FAutoConsoleCommand GOKStreetRulesStatus(TEXT("ok.StreetRules.Status"), TEXT("StreetSocial rules: DLL and tuning in use."),
 FConsoleCommandDelegate::CreateStatic(&OKStreetRulesConsoleStatus));

void OKStreetRulesFatal(const FString& Error) {
 static bool bShown = false;
 UE_LOG(LogTemp, Error, TEXT("StreetSocial rules unavailable, quitting in 5 seconds: %s"), *Error);
 if (bShown) return;
 bShown = true;
 if (GEngine && GEngine->GameViewport) {
  GEngine->GameViewport->AddViewportWidgetContent(
   SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(FLinearColor(0.f, 0.f, 0.f, 0.85f))
   .HAlign(HAlign_Center).VAlign(VAlign_Center).Padding(40)
   [SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Bold", 26)).ColorAndOpacity(FLinearColor(1.f, 0.35f, 0.3f)).AutoWrapText(true)
    .Text(FText::FromString(OKT(TEXT("ui.rules.rules_dll_load_failed")) + Error))], 10000);
 }
 FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([](float) {
  FPlatformMisc::RequestExitWithStatus(false, 1);
  return false;
 }), 5.f);
}
