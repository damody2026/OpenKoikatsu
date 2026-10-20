#include "OKStreetMind.h"
#include "OKStreetSocialText.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

// Must match OKM_ABI_VERSION in Tools/ok-mind/include/ok_mind.h.
static constexpr uint32 OKMindAbiVersion = 1;

struct FOKStreetMind::FApi {
 using FAbi = uint32 (*)();
 using FOpen = void* (*)(const char*);
 using FSubmit = uint64 (*)(void*, const char*);
 using FQuery = const char* (*)(void*, const char*);
 using FPoll = const char* (*)(void*);
 using FFree = void (*)(const char*);
 using FLastError = const char* (*)();
 using FSave = int32 (*)(void*);
 using FClose = void (*)(void*);
 void* Dll = nullptr;
 FAbi Abi = nullptr; FOpen Open = nullptr; FSubmit Submit = nullptr; FQuery Query = nullptr; FPoll Poll = nullptr;
 FFree Free = nullptr; FLastError LastError = nullptr; FSave Save = nullptr; FClose Close = nullptr;

 // Taking an owned copy of a Rust string, released immediately.
 FString Take(const char* Text) const {
  if (!Text) return FString();
  FString Out = UTF8_TO_TCHAR(Text);
  Free(Text);
  return Out;
 }
};

// The DLL is never unloaded: worker threads may still be finishing an HTTP call after Close, and process exit
// tears everything down anyway. One copy serves every scene in the process.
static FOKStreetMind::FApi* OKStreetMindLoadApi(const FString& Path, FString& Error) {
 static TMap<FString, FOKStreetMind::FApi*> Loaded;
 if (auto** Found = Loaded.Find(Path)) return *Found;
 void* Dll = FPlatformProcess::GetDllHandle(*Path);
 if (!Dll) { Error = TEXT("ok_mind.dll could not be loaded: ") + Path; return nullptr; }
 auto* Api = new FOKStreetMind::FApi();
 Api->Dll = Dll;
 auto Get = [&](const TCHAR* Name) { return FPlatformProcess::GetDllExport(Dll, Name); };
 Api->Abi = (FOKStreetMind::FApi::FAbi)Get(TEXT("okm_abi_version"));
 Api->Open = (FOKStreetMind::FApi::FOpen)Get(TEXT("okm_open"));
 Api->Submit = (FOKStreetMind::FApi::FSubmit)Get(TEXT("okm_submit"));
 Api->Query = (FOKStreetMind::FApi::FQuery)Get(TEXT("okm_query"));
 Api->Poll = (FOKStreetMind::FApi::FPoll)Get(TEXT("okm_poll"));
 Api->Free = (FOKStreetMind::FApi::FFree)Get(TEXT("okm_free"));
 Api->LastError = (FOKStreetMind::FApi::FLastError)Get(TEXT("okm_last_error"));
 Api->Save = (FOKStreetMind::FApi::FSave)Get(TEXT("okm_save"));
 Api->Close = (FOKStreetMind::FApi::FClose)Get(TEXT("okm_close"));
 if (!Api->Abi || !Api->Open || !Api->Submit || !Api->Query || !Api->Poll || !Api->Free || !Api->LastError || !Api->Save || !Api->Close) {
  Error = TEXT("ok_mind.dll is missing exports: ") + Path;
  delete Api;
  return nullptr;
 }
 if (Api->Abi() != OKMindAbiVersion) {
  Error = FString::Printf(TEXT("ok_mind.dll ABI %u, expected %u: %s"), Api->Abi(), OKMindAbiVersion, *Path);
  delete Api;
  return nullptr;
 }
 Loaded.Add(Path, Api);
 return Api;
}

TArray<FString> FOKStreetMind::Candidates() {
 TArray<FString> Out;
 const FString Env = FPlatformMisc::GetEnvironmentVariable(TEXT("OK_MIND_DLL"));
 if (!Env.IsEmpty()) Out.Add(Env);
 Out.Add(FPaths::ConvertRelativePathToFull(FPaths::Combine(FPlatformProcess::BaseDir(), TEXT("ok_mind.dll"))));
 Out.Add(FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("Tools/ok-mind/target/release/ok_mind.dll"))));
 return Out;
}

FOKStreetMind::~FOKStreetMind() { Close(); }

FString FOKStreetMind::TakeError() {
 return Api ? Api->Take(Api->LastError()) : FString();
}

// The cargo output Reload watches: OK_MIND_SOURCE_DLL, else the project's release build.
static FString OKStreetMindSource() {
 const FString Env = FPlatformMisc::GetEnvironmentVariable(TEXT("OK_MIND_SOURCE_DLL"));
 return !Env.IsEmpty() ? Env : FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("Tools/ok-mind/target/release/ok_mind.dll")));
}

bool FOKStreetMind::OpenWith(FApi* NewApi, const FString& Path, FString& Error) {
 void* NewHandle = NewApi->Open(TCHAR_TO_UTF8(*ConfigJson));
 if (!NewHandle) { Error = TEXT("ok_mind open failed: ") + NewApi->Take(NewApi->LastError()); return false; }
 Api = NewApi; Handle = NewHandle; LoadedFrom = Path;
 return true;
}

bool FOKStreetMind::Reload(FString& Out, FString& Error) {
 if (!Handle) { Error = TEXT("ok_mind not open"); return false; }
 FString Status;
 if (!Query(TEXT("{\"kind\":\"status\"}"), Status, Error)) return false;
 TSharedPtr<FJsonObject> S;
 if (FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Status), S) && S.IsValid() && (S->GetNumberField(TEXT("queued")) > 0 || S->GetNumberField(TEXT("in_flight")) > 0)) {
  Error = FString::Printf(TEXT("busy: %d queued, %d in flight; try again when she has answered"), int32(S->GetNumberField(TEXT("queued"))), int32(S->GetNumberField(TEXT("in_flight"))));
  return false;
 }
 FApi* NewApi = Api; FString NewPath = LoadedFrom;
 const FDateTime Stamp = IFileManager::Get().GetTimeStamp(*SourcePath);
 if (Stamp != FDateTime::MinValue() && Stamp != SourceStamp && !FPaths::IsSamePath(SourcePath, LoadedFrom)) {
  // A new build: a copy under a fresh name (Windows keeps a loaded DLL locked, and cargo must stay free to write).
  const FString Copy = FPaths::Combine(ReloadDir, FString::Printf(TEXT("ok_mind-%lld.dll"), FDateTime::UtcNow().GetTicks()));
  if (IFileManager::Get().Copy(*Copy, *SourcePath) != COPY_OK) { Error = TEXT("could not copy ") + SourcePath + TEXT(" to ") + Copy; return false; }
  NewApi = OKStreetMindLoadApi(Copy, Error);
  if (!NewApi) return false;
  NewPath = Copy;
 }
 FApi* OldApi = Api; const FString OldPath = LoadedFrom;
 Close(); // saves the contexts, memories and state
 if (!OpenWith(NewApi, NewPath, Error)) {
  FString Again;
  if (!OpenWith(OldApi, OldPath, Again)) Error += TEXT("; the old one did not open again either: ") + Again;
  return false;
 }
 if (NewPath != OldPath) SourceStamp = Stamp;
 Out = NewPath != OldPath ? TEXT("dll swapped: ") + NewPath : TEXT("reopened (settings read again): ") + NewPath;
 return true;
}

bool FOKStreetMind::Open(const FString& SaveDir, const FString& Settings, bool bDebug, FString& Error, const FString& DataDir) {
 Close();
 FApi* Found = nullptr; FString FoundPath;
 for (const FString& Path : Candidates()) {
  if (!FPaths::FileExists(Path)) continue;
  Found = OKStreetMindLoadApi(Path, Error);
  if (Found) { FoundPath = Path; break; }
 }
 if (!Found) { if (Error.IsEmpty()) Error = TEXT("ok_mind.dll not found"); return false; }
 TSharedRef<FJsonObject> Config = MakeShared<FJsonObject>();
 Config->SetStringField(TEXT("save_dir"), FPaths::ConvertRelativePathToFull(SaveDir));
 if (!Settings.IsEmpty()) Config->SetStringField(TEXT("deepseek_settings"), FPaths::ConvertRelativePathToFull(Settings));
 Config->SetBoolField(TEXT("debug"), bDebug);
 // Plan §15: sports results and market data for the `fin` queries.
 if (!DataDir.IsEmpty() && FPaths::DirectoryExists(DataDir)) Config->SetStringField(TEXT("data_dir"), FPaths::ConvertRelativePathToFull(DataDir));
 Config->SetStringField(TEXT("wallet_currency"), TEXT("CNY")); // the hero is Chinese: the wallet is RMB
 ConfigJson.Reset();
 FJsonSerializer::Serialize(Config, TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&ConfigJson));
 if (!OpenWith(Found, FoundPath, Error)) return false;
 SourcePath = OKStreetMindSource();
 SourceStamp = IFileManager::Get().GetTimeStamp(*SourcePath);
 // Copies left by earlier hot reloads; the ones still loaded by another process stay.
 ReloadDir = FPaths::Combine(FPaths::GetPath(LoadedFrom), TEXT("reload"));
 TArray<FString> Old;
 IFileManager::Get().FindFiles(Old, *FPaths::Combine(ReloadDir, TEXT("*.dll")), true, false);
 for (const FString& Name : Old) IFileManager::Get().Delete(*FPaths::Combine(ReloadDir, Name), false, false, true);
 return true;
}

void FOKStreetMind::Close() {
 if (Handle && Api) Api->Close(Handle);
 Handle = nullptr;
}

uint64 FOKStreetMind::Submit(const FString& JobJson, FString& Error) {
 if (!Handle) { Error = TEXT("ok_mind not open"); return 0; }
 // Every job says the player's language: ok-mind converts her replies to its script (zh-TW, zh-CN) or leaves English.
 FString Job = JobJson;
 if (Job.StartsWith(TEXT("{")) && !Job.Contains(TEXT("\"lang\":"))) Job.InsertAt(1, FString::Printf(TEXT("\"lang\":\"%s\"%s"), *OKStreetCppText().Lang, Job.Len() > 2 ? TEXT(",") : TEXT("")));
 const uint64 Id = Api->Submit(Handle, TCHAR_TO_UTF8(*Job));
 if (!Id) Error = TakeError();
 return Id;
}

bool FOKStreetMind::Query(const FString& QueryJson, FString& Out, FString& Error) {
 if (!Handle) { Error = TEXT("ok_mind not open"); return false; }
 const char* Text = Api->Query(Handle, TCHAR_TO_UTF8(*QueryJson));
 if (!Text) { Error = TakeError(); return false; }
 Out = Api->Take(Text);
 return true;
}

void FOKStreetMind::Poll(TArray<TSharedPtr<FJsonObject>>& Out) {
 if (!Handle) return;
 const FString Text = Api->Take(Api->Poll(Handle));
 if (Text.IsEmpty()) return;
 TArray<TSharedPtr<FJsonValue>> Rows;
 if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Rows)) { UE_LOG(LogTemp, Warning, TEXT("ok_mind poll returned invalid JSON")); return; }
 for (const auto& Row : Rows) if (Row.IsValid() && Row->Type == EJson::Object) Out.Add(Row->AsObject());
}

static FOKStreetMind* GOKStreetActiveMind = nullptr;
void OKStreetSetActiveMind(FOKStreetMind* Mind) { GOKStreetActiveMind = Mind; }

static void OKStreetMindConsoleStatus() {
 if (!GOKStreetActiveMind) { UE_LOG(LogTemp, Display, TEXT("StreetSocial mind: not open")); return; }
 FString Out, Error;
 if (GOKStreetActiveMind->Query(TEXT("{\"kind\":\"status\"}"), Out, Error)) { UE_LOG(LogTemp, Display, TEXT("StreetSocial mind status: %s"), *Out); }
 else { UE_LOG(LogTemp, Warning, TEXT("StreetSocial mind status failed: %s"), *Error); }
}

static void OKStreetMindConsolePing() {
 FString Error;
 if (!GOKStreetActiveMind) { UE_LOG(LogTemp, Warning, TEXT("StreetSocial mind: not open")); return; }
 if (!GOKStreetActiveMind->Submit(TEXT("{\"kind\":\"ping\",\"echo\":\"console\"}"), Error)) { UE_LOG(LogTemp, Warning, TEXT("StreetSocial mind ping failed: %s"), *Error); }
}

static void OKStreetMindConsoleChat(const TArray<FString>& Args) {
 if (!GOKStreetActiveMind) { UE_LOG(LogTemp, Warning, TEXT("StreetSocial mind: not open")); return; }
 TSharedRef<FJsonObject> Job = MakeShared<FJsonObject>();
 Job->SetStringField(TEXT("kind"), TEXT("chat"));
 Job->SetStringField(TEXT("tag"), TEXT("console"));
 Job->SetBoolField(TEXT("json"), false);
 TArray<TSharedPtr<FJsonValue>> Messages;
 TSharedRef<FJsonObject> User = MakeShared<FJsonObject>();
 User->SetStringField(TEXT("role"), TEXT("user"));
 User->SetStringField(TEXT("content"), FString::Join(Args, TEXT(" ")));
 Messages.Add(MakeShared<FJsonValueObject>(User));
 Job->SetArrayField(TEXT("messages"), Messages);
 FString Json, Error;
 FJsonSerializer::Serialize(Job, TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Json));
 if (!GOKStreetActiveMind->Submit(Json, Error)) { UE_LOG(LogTemp, Warning, TEXT("StreetSocial mind chat failed: %s"), *Error); }
}

// The simple inspector (plan P1): her context size, memory stream and profile, into the log.
static void OKStreetMindConsoleInspect(const TArray<FString>& Args) {
 if (!GOKStreetActiveMind || Args.Num() == 0) { UE_LOG(LogTemp, Warning, TEXT("usage: ok.StreetMind.Inspect <heroine id> (ids are logged as 'StreetSocial mind heroine')")); return; }
 for (const TCHAR* Kind : {TEXT("context"), TEXT("memories"), TEXT("profile")}) {
  FString Out, Error;
  const FString Query = FString::Printf(TEXT("{\"kind\":\"%s\",\"heroine\":\"%s\"}"), Kind, *Args[0].ReplaceCharWithEscapedChar());
  if (GOKStreetActiveMind->Query(Query, Out, Error)) { UE_LOG(LogTemp, Display, TEXT("StreetSocial mind %s: %s"), Kind, *Out); }
  else { UE_LOG(LogTemp, Warning, TEXT("StreetSocial mind %s failed: %s"), Kind, *Error); }
 }
}

// ok.StreetMind.Status / ok.StreetMind.Ping / ok.StreetMind.Chat <text>: check the mind from the console.
// Results show up in the log as "StreetSocial mind result".
static FAutoConsoleCommand GOKStreetMindStatus(TEXT("ok.StreetMind.Status"), TEXT("Print the heroine mind status"), FConsoleCommandDelegate::CreateStatic(&OKStreetMindConsoleStatus));
static FAutoConsoleCommand GOKStreetMindPing(TEXT("ok.StreetMind.Ping"), TEXT("Round-trip a ping job through the heroine mind"), FConsoleCommandDelegate::CreateStatic(&OKStreetMindConsolePing));
static FAutoConsoleCommand GOKStreetMindInspect(TEXT("ok.StreetMind.Inspect"), TEXT("Log a heroine's context size, memories and profile"), FConsoleCommandWithArgsDelegate::CreateStatic(&OKStreetMindConsoleInspect));
static FAutoConsoleCommand GOKStreetMindChat(TEXT("ok.StreetMind.Chat"), TEXT("Send one line to DeepSeek through the heroine mind queue"), FConsoleCommandWithArgsDelegate::CreateStatic(&OKStreetMindConsoleChat));

bool FOKStreetMind::Save(FString& Error) {
 if (!Handle) { Error = TEXT("ok_mind not open"); return false; }
 if (Api->Save(Handle) == 1) return true;
 Error = TakeError();
 return false;
}
