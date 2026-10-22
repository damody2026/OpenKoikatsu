#include "OKStoryLive.h"
#include "Dom/JsonObject.h"
#include "HAL/PlatformProcess.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

struct FOKStoryLive::FApi
{
	using FOpen = void* (*)(const char*);
	using FInput = int32 (*)(void*, const char*);
	using FTick = const char* (*)(void*, const char*);
	using FState = const char* (*)(void*);
	using FFree = void (*)(const char*);
	using FLastError = const char* (*)();
	using FClose = void (*)(void*);
	FOpen Open = nullptr; FInput Input = nullptr; FTick Tick = nullptr; FState State = nullptr; FFree Free = nullptr; FLastError LastError = nullptr; FClose Close = nullptr;

	FString Take(const char* Text) const
	{
		if (!Text) return FString();
		FString Out = UTF8_TO_TCHAR(Text);
		Free(Text);
		return Out;
	}
};

// Loaded once per path and kept (a swapped-out copy may still be on some stack).
static FOKStoryLive::FApi* OKStoryLiveLoad(const FString& Path, FString& Error)
{
	static TMap<FString, FOKStoryLive::FApi*> Loaded;
	if (FOKStoryLive::FApi** Found = Loaded.Find(Path)) return *Found;
	void* Dll = FPlatformProcess::GetDllHandle(*Path);
	if (!Dll) { Error = TEXT("ok_story.dll could not be loaded: ") + Path; return nullptr; }
	auto* Api = new FOKStoryLive::FApi();
	auto Get = [&](const TCHAR* Name) { return FPlatformProcess::GetDllExport(Dll, Name); };
	using FApi = FOKStoryLive::FApi;
	Api->Open = (FApi::FOpen)Get(TEXT("oks_live_open"));
	Api->Input = (FApi::FInput)Get(TEXT("oks_live_input"));
	Api->Tick = (FApi::FTick)Get(TEXT("oks_live_tick"));
	Api->State = (FApi::FState)Get(TEXT("oks_live_state"));
	Api->Free = (FApi::FFree)Get(TEXT("oks_free"));
	Api->LastError = (FApi::FLastError)Get(TEXT("oks_last_error"));
	Api->Close = (FApi::FClose)Get(TEXT("oks_live_close"));
	if (!Api->Open || !Api->Input || !Api->Tick || !Api->State || !Api->Free || !Api->LastError || !Api->Close)
	{
		Error = TEXT("ok_story.dll has no live presenter (an older build): ") + Path;
		delete Api;
		return nullptr;
	}
	Loaded.Add(Path, Api);
	return Api;
}

FOKStoryLive::~FOKStoryLive() { Close(); }

bool FOKStoryLive::Open(FString& OutError)
{
	Close();
	TArray<FString> Candidates;
	const FString Env = FPlatformMisc::GetEnvironmentVariable(TEXT("OK_STORY_DLL"));
	if (!Env.IsEmpty()) Candidates.Add(Env);
	Candidates.Add(FPaths::ConvertRelativePathToFull(FPaths::Combine(FPlatformProcess::BaseDir(), TEXT("ok_story.dll"))));
	Candidates.Add(FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("Tools/ok-story/target/release/ok_story.dll"))));
	for (const FString& Candidate : Candidates)
	{
		if (!FPaths::FileExists(Candidate)) continue;
		FApi* Found = OKStoryLiveLoad(Candidate, OutError);
		if (!Found) continue;
		TSharedRef<FJsonObject> Config = MakeShared<FJsonObject>();
		// Staged builds cannot see the project's Content folder: the launcher passes it (OK_STORY_DATA).
		FString Data = FPlatformMisc::GetEnvironmentVariable(TEXT("OK_STORY_DATA"));
		if (Data.IsEmpty()) Data = FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir() / TEXT("IndependentResources/Story"));
		Config->SetStringField(TEXT("data_dir"), Data);
		FString Json;
		FJsonSerializer::Serialize(Config, TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Json));
		void* NewHandle = Found->Open(TCHAR_TO_UTF8(*Json));
		if (!NewHandle) { OutError = Found->Take(Found->LastError()); return false; }
		Api = Found;
		Handle = NewHandle;
		Path = Candidate;
		return true;
	}
	if (OutError.IsEmpty()) OutError = TEXT("ok_story.dll not found (cargo build --release in Tools/ok-story)");
	return false;
}

void FOKStoryLive::Close()
{
	if (Handle && Api) Api->Close(Handle);
	Handle = nullptr;
}

void FOKStoryLive::Input(const FString& Json)
{
	if (Handle && !Api->Input(Handle, TCHAR_TO_UTF8(*Json))) UE_LOG(LogTemp, Warning, TEXT("OK story live input refused: %s (%s)"), *Json, *Api->Take(Api->LastError()));
}

TSharedPtr<FJsonObject> FOKStoryLive::Tick(const FString& FactsJson, FString& OutError)
{
	if (!Handle) { OutError = TEXT("not open"); return nullptr; }
	const char* Text = Api->Tick(Handle, TCHAR_TO_UTF8(*FactsJson));
	if (!Text) { OutError = Api->Take(Api->LastError()); return nullptr; }
	TSharedPtr<FJsonObject> Frame;
	FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Api->Take(Text)), Frame);
	return Frame;
}

FString FOKStoryLive::State() const
{
	return Handle ? Api->Take(Api->State(Handle)) : FString();
}
