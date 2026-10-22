#include "OKStoryPlayer.h"
#include "SOKStoryView.h"
#include "OKStudioGame.h"
#include "OKSceneActor.h"
#include "OKSceneCard.h"
#include "KKPreviewActor.h"
#include "KKSpeech.h"
#include "KKCardReader.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/AudioComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformProcess.h"
#include "ImageUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionComponentMask.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionUtils.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/FileHelper.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UnrealClient.h"
#include "Containers/Ticker.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "AudioDevice.h"

// Must match OKS_ABI_VERSION in Tools/ok-story/include/ok_story.h.
static constexpr uint32 OKStoryAbiVersion = 1;

struct UOKStoryPlayer::FApi
{
	using FAbi = uint32 (*)();
	using FOpen = void* (*)(const char*);
	using FStr = const char* (*)(void*, const char*);
	using FInt = int32 (*)(void*, const char*);
	using FExport = const char* (*)(void*);
	using FReload = int32 (*)(void*);
	using FFree = void (*)(const char*);
	using FLastError = const char* (*)();
	using FClose = void (*)(void*);
	FAbi Abi = nullptr; FOpen Open = nullptr; FInt Input = nullptr; FStr Tick = nullptr; FInt Ack = nullptr; FStr Query = nullptr;
	FExport Export = nullptr; FInt Import = nullptr; FReload Reload = nullptr; FFree Free = nullptr; FLastError LastError = nullptr; FClose Close = nullptr;

	FString Take(const char* Text) const
	{
		if (!Text) return FString();
		FString Out = UTF8_TO_TCHAR(Text);
		Free(Text);
		return Out;
	}
};

// A loaded DLL is never unloaded: a swapped-out copy may still be on some stack this frame, and they are small.
static UOKStoryPlayer::FApi* OKStoryLoadApi(const FString& Path, FString& Error)
{
	static TMap<FString, UOKStoryPlayer::FApi*> Loaded;
	if (UOKStoryPlayer::FApi** Found = Loaded.Find(Path)) return *Found;
	void* Dll = FPlatformProcess::GetDllHandle(*Path);
	if (!Dll) { Error = TEXT("ok_story.dll could not be loaded: ") + Path; return nullptr; }
	auto* Api = new UOKStoryPlayer::FApi();
	auto Get = [&](const TCHAR* Name) { return FPlatformProcess::GetDllExport(Dll, Name); };
	using FApi = UOKStoryPlayer::FApi;
	Api->Abi = (FApi::FAbi)Get(TEXT("oks_abi_version"));
	Api->Open = (FApi::FOpen)Get(TEXT("oks_open"));
	Api->Input = (FApi::FInt)Get(TEXT("oks_input"));
	Api->Tick = (FApi::FStr)Get(TEXT("oks_tick"));
	Api->Ack = (FApi::FInt)Get(TEXT("oks_ack"));
	Api->Query = (FApi::FStr)Get(TEXT("oks_query"));
	Api->Export = (FApi::FExport)Get(TEXT("oks_export"));
	Api->Import = (FApi::FInt)Get(TEXT("oks_import"));
	Api->Reload = (FApi::FReload)Get(TEXT("oks_reload"));
	Api->Free = (FApi::FFree)Get(TEXT("oks_free"));
	Api->LastError = (FApi::FLastError)Get(TEXT("oks_last_error"));
	Api->Close = (FApi::FClose)Get(TEXT("oks_close"));
	if (!Api->Abi || !Api->Open || !Api->Input || !Api->Tick || !Api->Ack || !Api->Query || !Api->Export || !Api->Import || !Api->Reload
		|| !Api->Free || !Api->LastError || !Api->Close)
	{
		Error = TEXT("ok_story.dll is missing exports: ") + Path;
		delete Api;
		return nullptr;
	}
	if (Api->Abi() != OKStoryAbiVersion)
	{
		Error = FString::Printf(TEXT("ok_story.dll ABI %u, expected %u: %s"), Api->Abi(), OKStoryAbiVersion, *Path);
		delete Api;
		return nullptr;
	}
	Loaded.Add(Path, Api);
	return Api;
}

static TArray<FString> OKStoryDllCandidates()
{
	TArray<FString> Out;
	const FString Env = FPlatformMisc::GetEnvironmentVariable(TEXT("OK_STORY_DLL"));
	if (!Env.IsEmpty()) Out.Add(Env);
	Out.Add(FPaths::ConvertRelativePathToFull(FPaths::Combine(FPlatformProcess::BaseDir(), TEXT("ok_story.dll"))));
	Out.Add(FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("Tools/ok-story/target/release/ok_story.dll"))));
	return Out;
}

static FString OKStoryJson(const TSharedRef<FJsonObject>& Object)
{
	FString Json;
	FJsonSerializer::Serialize(Object, TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Json));
	return Json;
}

static TSharedPtr<FJsonObject> OKStoryParse(const FString& Text)
{
	TSharedPtr<FJsonObject> Out;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Out)) return nullptr;
	return Out;
}

static TArray<TSharedPtr<FJsonValue>> OKStoryVector(const FVector& V)
{
	return { MakeShared<FJsonValueNumber>(V.X), MakeShared<FJsonValueNumber>(V.Y), MakeShared<FJsonValueNumber>(V.Z) };
}

static TArray<TSharedPtr<FJsonValue>> OKStoryQuat(const FQuat& Q)
{
	return { MakeShared<FJsonValueNumber>(Q.X), MakeShared<FJsonValueNumber>(Q.Y), MakeShared<FJsonValueNumber>(Q.Z), MakeShared<FJsonValueNumber>(Q.W) };
}

static bool OKStoryReadVector(const TSharedPtr<FJsonObject>& O, const TCHAR* Field, FVector& Out)
{
	const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
	if (!O->TryGetArrayField(Field, A) || A->Num() < 3) return false;
	Out = FVector((*A)[0]->AsNumber(), (*A)[1]->AsNumber(), (*A)[2]->AsNumber());
	return true;
}

static bool OKStoryReadQuat(const TSharedPtr<FJsonObject>& O, const TCHAR* Field, FQuat& Out)
{
	const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
	if (!O->TryGetArrayField(Field, A) || A->Num() < 4) return false;
	Out = FQuat((*A)[0]->AsNumber(), (*A)[1]->AsNumber(), (*A)[2]->AsNumber(), (*A)[3]->AsNumber()).GetNormalized();
	return true;
}

// Unity (X,Y,Z) -> Unreal (-X,Z,Y), as the skinned display builds its palette (KKRuntimePoseMath).
static FTransform OKStoryBoneMatrix(const float* M)
{
	const int32 Map[3] = { 0, 2, 1 };
	const double Sign[3] = { -1., 1., 1. };
	FMatrix R = FMatrix::Identity;
	for (int32 A = 0; A < 3; ++A)
	{
		for (int32 B = 0; B < 3; ++B) R.M[A][B] = Sign[A] * Sign[B] * M[Map[B] * 4 + Map[A]];
		R.M[3][A] = 100. * Sign[A] * M[Map[A] * 4 + 3];
	}
	return FTransform(R);
}

static TWeakObjectPtr<UOKStoryPlayer> GOKStory;

UOKStoryPlayer* UOKStoryPlayer::Get() { return GOKStory.Get(); }

UWorld* UOKStoryPlayer::GetTickableGameObjectWorld() const
{
	return Studio.IsValid() && Studio->SceneActor ? Studio->SceneActor->GetWorld() : nullptr;
}

FString UOKStoryPlayer::TakeError() { return Api ? Api->Take(Api->LastError()) : FString(); }

UOKStoryPlayer* UOKStoryPlayer::Start(UWorld* World, const FString& InFile, FString& OutError)
{
	if (UOKStoryPlayer* Running = Get()) Running->Stop();
	FString File = InFile;
	if (FPaths::IsRelative(File)) File = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), File);
	if (!FPaths::FileExists(File)) { OutError = TEXT("story not found: ") + File; return nullptr; }
	UOKStudioGame* Game = UOKStudioGame::Get();
	bool bStarted = false;
	if (!Game)
	{
		Game = UOKStudioGame::Start(World, FString(), OutError);
		if (!Game) return nullptr;
		bStarted = true;
	}
	UOKStoryPlayer* P = NewObject<UOKStoryPlayer>();
	P->AddToRoot();
	P->Studio = Game;
	P->bStartedStudio = bStarted;
	if (!P->Open(File, OutError)) { P->RemoveFromRoot(); return nullptr; }
	GOKStory = P;
	Game->SetStoryMode(true);
	if (GEngine && GEngine->GameViewport)
	{
		FOKStoryViewSource Source;
		const TWeakObjectPtr<UOKStoryPlayer> Weak(P);
		Source.Frame = [Weak] { return Weak.IsValid() ? Weak->Frame : nullptr; };
		Source.Portrait = [Weak] { return Weak.IsValid() ? Weak->PortraitMaterial() : nullptr; };
		Source.Thumbnail = [Weak](const FString& Path) { return Weak.IsValid() ? Weak->Thumbnail(Path) : nullptr; };
		Source.Input = [Weak](const FString& Json) { if (Weak.IsValid()) Weak->Input(Json); };
		P->View = SNew(SOKStoryView).Source(Source);
		GEngine->GameViewport->AddViewportWidgetContent(P->View.ToSharedRef(), 20);
		FSlateApplication::Get().SetAllUserFocus(P->View, EFocusCause::SetDirectly);
	}
	UE_LOG(LogTemp, Display, TEXT("OK story: playing %s (%s)"), *File, *P->DllPath);
	return P;
}

bool UOKStoryPlayer::Open(const FString& File, FString& Error)
{
	FApi* Found = nullptr;
	for (const FString& Candidate : OKStoryDllCandidates())
	{
		if (!FPaths::FileExists(Candidate)) continue;
		Found = OKStoryLoadApi(Candidate, Error);
		if (Found) { DllPath = Candidate; break; }
	}
	if (!Found) { if (Error.IsEmpty()) Error = TEXT("ok_story.dll not found (build it: cargo build --release in Tools/ok-story)"); return false; }
	if (!OpenWith(Found, DllPath, File, Error)) return false;
	SourceDll = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("Tools/ok-story/target/release/ok_story.dll")));
	SourceStamp = IFileManager::Get().GetTimeStamp(*SourceDll);
	ReloadDir = FPaths::Combine(FPaths::GetPath(DllPath), TEXT("reload"));
	return true;
}

bool UOKStoryPlayer::OpenWith(FApi* Found, const FString& Path, const FString& File, FString& Error)
{
	TSharedRef<FJsonObject> Config = MakeShared<FJsonObject>();
	const FString Content = FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir());
	const FString Project = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
	FString Lang = FPlatformMisc::GetEnvironmentVariable(TEXT("OK_STORY_LANG"));
	if (Lang.IsEmpty()) Lang = TEXT("zh");
	Config->SetStringField(TEXT("story"), File);
	Config->SetStringField(TEXT("data_dir"), Content / TEXT("IndependentResources/Story"));
	Config->SetStringField(TEXT("save_dir"), Project / TEXT("UserData/Story"));
	Config->SetStringField(TEXT("expressions"), Content / TEXT("IndependentResources/StreetSocialData/RuntimeV2/expressions.json"));
	Config->SetStringField(TEXT("project_dir"), Project);
	Config->SetStringField(TEXT("lang"), Lang);
	Config->SetNumberField(TEXT("seed"), double(FDateTime::UtcNow().GetTicks() % 1000000007));
	Config->SetNumberField(TEXT("utc_offset_minutes"), FMath::RoundToDouble((FDateTime::Now() - FDateTime::UtcNow()).GetTotalMinutes()));
	void* NewHandle = Found->Open(TCHAR_TO_UTF8(*OKStoryJson(Config)));
	if (!NewHandle) { Error = Found->Take(Found->LastError()); return false; }
	Api = Found;
	Handle = NewHandle;
	DllPath = Path;
	StoryFile = File;
	return true;
}

void UOKStoryPlayer::Close()
{
	if (Handle && Api) Api->Close(Handle);
	Handle = nullptr;
}

void UOKStoryPlayer::Stop()
{
	const bool bQuit = bQuitOnEnd;
	if (View && GEngine && GEngine->GameViewport) GEngine->GameViewport->RemoveViewportWidgetContent(View.ToSharedRef());
	View.Reset();
	Close();
	for (TStrongObjectPtr<UAudioComponent>& S : Sounds) if (S) S->Stop();
	Sounds.Reset();
	PortraitCapture.Release();
	ProbeRunner.Release();
	if (UOKStudioGame* Game = Studio.Get())
	{
		// The portrait copies of offstage speakers are not part of the scene.
		for (const auto& Pair : Objects)
		{
			const FOKSceneObject* O = Game->Document ? Game->Document->Find(Pair.Value) : nullptr;
			if (O && O->Name.StartsWith(TEXT("__story_offstage_"))) Game->Document->RemoveObject(Pair.Value);
		}
		if (AKKPreviewActor* P = Preview(VoiceWho)) P->StopSpeech();
		Game->SetStoryMode(false);
	}
	Objects.Reset();
	if (GOKStory.Get() == this) GOKStory = nullptr;
	FSimpleMulticastDelegate Ended = MoveTemp(OnEnded);
	OnEnded.Clear();
	Ended.Broadcast();
	RemoveFromRoot();
	if (bQuit) FPlatformMisc::RequestExit(false);
}

void UOKStoryPlayer::Input(const FString& Json)
{
	if (Handle && !Api->Input(Handle, TCHAR_TO_UTF8(*Json))) UE_LOG(LogTemp, Warning, TEXT("OK story input refused: %s (%s)"), *Json, *TakeError());
}

FString UOKStoryPlayer::Query(const FString& Json)
{
	if (!Handle) return FString();
	const char* Text = Api->Query(Handle, TCHAR_TO_UTF8(*Json));
	return Text ? Api->Take(Text) : TakeError();
}

bool UOKStoryPlayer::Reload(FString& Out, FString& Error)
{
	if (!Handle) { Error = TEXT("no story"); return false; }
	const FDateTime Stamp = IFileManager::Get().GetTimeStamp(*SourceDll);
	if (Stamp != FDateTime::MinValue() && Stamp != SourceStamp && !FPaths::IsSamePath(SourceDll, DllPath))
	{
		// A new build: its copy under a fresh name (Windows keeps the loaded one locked), the state carried over.
		const FString Copy = FPaths::Combine(ReloadDir, FString::Printf(TEXT("ok_story-%lld.dll"), FDateTime::UtcNow().GetTicks()));
		if (IFileManager::Get().Copy(*Copy, *SourceDll) != COPY_OK) { Error = TEXT("could not copy ") + SourceDll; return false; }
		FApi* NewApi = OKStoryLoadApi(Copy, Error);
		if (!NewApi) return false;
		const FString State = Api->Take(Api->Export(Handle));
		FApi* OldApi = Api;
		void* OldHandle = Handle;
		const FString OldPath = DllPath;
		if (!OpenWith(NewApi, Copy, StoryFile, Error)) { Api = OldApi; Handle = OldHandle; DllPath = OldPath; return false; }
		if (!Api->Import(Handle, TCHAR_TO_UTF8(*State))) UE_LOG(LogTemp, Warning, TEXT("OK story: state not carried over: %s"), *TakeError());
		OldApi->Close(OldHandle);
		SourceStamp = Stamp;
		Out = TEXT("dll swapped: ") + Copy;
		return true;
	}
	if (!Api->Reload(Handle)) { Error = TakeError(); return false; }
	Out = TEXT("story, tuning and words re-read");
	return true;
}

AKKPreviewActor* UOKStoryPlayer::Preview(const FString& Who) const
{
	const int32* Id = Objects.Find(Who);
	UOKStudioGame* Game = Studio.Get();
	return Id && Game && Game->SceneActor ? Game->SceneActor->PreviewOf(*Id) : nullptr;
}

static FString OKStoryCardPath(const FString& Card)
{
	return FPaths::IsRelative(Card) ? FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), Card) : Card;
}

int32 UOKStoryPlayer::FindOrAddCharacter(const FString& Who, const FString& Card, int32 Outfit, int32 SceneObject)
{
	UOKStudioGame* Game = Studio.Get();
	if (!Game || !Game->Document) return -1;
	if (const int32* Id = Objects.Find(Who))
		if (const FOKSceneObject* O = Game->Document->Find(*Id)) if (O->Kind == EOKSceneObjectKind::Character && !O->Name.StartsWith(TEXT("__story_offstage_"))) return *Id;
	if (SceneObject >= 0)
		if (const FOKSceneObject* O = Game->Document->Find(SceneObject)) if (O->Kind == EOKSceneObjectKind::Character) { Objects.Add(Who, SceneObject); return SceneObject; }
	const FString Path = OKStoryCardPath(Card);
	for (const FOKSceneObject& O : Game->Document->Objects)
	{
		if (O.Kind != EOKSceneObjectKind::Character || O.Name.StartsWith(TEXT("__story_offstage_"))) continue;
		if (FPaths::IsSamePath(OKStoryCardPath(O.Character.Card), Path)) { Objects.Add(Who, O.Id); return O.Id; }
	}
	Game->Select(-1); // added at the scene root, not under a selected folder
	const int32 Id = Game->AddCharacter(Path, Outfit);
	if (FOKSceneObject* O = Game->Document->Find(Id)) O->Name = Who;
	Objects.Add(Who, Id);
	return Id;
}

void UOKStoryPlayer::Ack(uint64 Id, bool bOk, const FString& Error, TFunctionRef<void(FJsonObject&)> Extra)
{
	if (!Handle) return;
	TSharedRef<FJsonObject> A = MakeShared<FJsonObject>();
	A->SetNumberField(TEXT("id"), double(Id));
	A->SetBoolField(TEXT("ok"), bOk);
	if (!Error.IsEmpty()) A->SetStringField(TEXT("error"), Error);
	Extra(*A);
	Api->Ack(Handle, TCHAR_TO_UTF8(*OKStoryJson(A)));
}

void UOKStoryPlayer::Ack(uint64 Id, bool bOk, const FString& Error)
{
	Ack(Id, bOk, Error, [](FJsonObject&) {});
}

void UOKStoryPlayer::CheckWaiting()
{
	const double Now = FPlatformTime::Seconds();
	for (int32 I = Waiting.Num() - 1; I >= 0; --I)
	{
		const FWaiting W = Waiting[I];
		AKKPreviewActor* P = Preview(W.Who);
		const bool bReady = P && P->Session.IsOpen();
		if (!bReady && Now - W.Since < 25.0) continue;
		Waiting.RemoveAt(I);
		const int32* Personality = Personalities.Find(W.Who);
		Ack(W.Id, bReady, bReady ? FString() : TEXT("character not shown after 25 s"), [&](FJsonObject& A)
		{
			if (Personality) A.SetNumberField(TEXT("personality"), *Personality);
		});
	}
}

FString UOKStoryPlayer::FactsJson(float DeltaTime)
{
	TSharedRef<FJsonObject> F = MakeShared<FJsonObject>();
	F->SetNumberField(TEXT("dt"), DeltaTime);
	FVector2D Size(1920, 1080);
	if (GEngine && GEngine->GameViewport) GEngine->GameViewport->GetViewportSize(Size);
	F->SetArrayField(TEXT("viewport"), { MakeShared<FJsonValueNumber>(Size.X), MakeShared<FJsonValueNumber>(Size.Y) });
	UOKStudioGame* Game = Studio.Get();
	if (Game && Game->Viewer)
	{
		TSharedRef<FJsonObject> V = MakeShared<FJsonObject>();
		V->SetArrayField(TEXT("pos"), OKStoryVector(Game->Viewer->GetActorLocation()));
		V->SetArrayField(TEXT("rot"), OKStoryQuat(Game->Viewer->GetActorQuat()));
		F->SetObjectField(TEXT("view"), V);
	}
	TSharedRef<FJsonObject> Actors = MakeShared<FJsonObject>();
	static const TPair<const TCHAR*, const TCHAR*> Bones[] = {
		{ TEXT("head"), TEXT("cf_j_head") }, { TEXT("spine03"), TEXT("cf_j_spine03") }, { TEXT("spine02"), TEXT("cf_j_spine02") }, { TEXT("hips"), TEXT("cf_j_hips") } };
	for (const auto& Pair : Objects)
	{
		AKKPreviewActor* P = Preview(Pair.Key);
		TSharedRef<FJsonObject> A = MakeShared<FJsonObject>();
		const bool bReady = P && P->Session.IsOpen();
		A->SetBoolField(TEXT("ready"), bReady);
		if (const int32* Personality = Personalities.Find(Pair.Key)) A->SetNumberField(TEXT("personality"), *Personality);
		if (bReady)
		{
			const FTransform Root = P->GetActorTransform();
			TSharedRef<FJsonObject> R = MakeShared<FJsonObject>();
			R->SetArrayField(TEXT("pos"), OKStoryVector(Root.GetLocation()));
			R->SetArrayField(TEXT("rot"), OKStoryQuat(Root.GetRotation()));
			R->SetNumberField(TEXT("scale"), Root.GetScale3D().Z);
			A->SetObjectField(TEXT("root"), R);
			TSharedRef<FJsonObject> B = MakeShared<FJsonObject>();
			for (const auto& Bone : Bones)
			{
				float M[16];
				if (!P->Session.BoneWorldMatrix(Bone.Value, M)) continue;
				const FTransform W = OKStoryBoneMatrix(M) * Root;
				TSharedRef<FJsonObject> X = MakeShared<FJsonObject>();
				X->SetArrayField(TEXT("pos"), OKStoryVector(W.GetLocation()));
				X->SetArrayField(TEXT("rot"), OKStoryQuat(W.GetRotation()));
				B->SetObjectField(Bone.Key, X);
			}
			A->SetObjectField(TEXT("bones"), B);
		}
		Actors->SetObjectField(Pair.Key, A);
	}
	F->SetObjectField(TEXT("actors"), Actors);
	F->SetArrayField(TEXT("probes"), ProbeRunner.TakeAnswers());
	if (VoiceId)
	{
		TSharedRef<FJsonObject> V = MakeShared<FJsonObject>();
		AKKPreviewActor* P = Preview(VoiceWho);
		const bool bSpeaking = P && P->IsSpeaking();
		bVoiceSeen |= bSpeaking;
		const double Elapsed = FPlatformTime::Seconds() - VoiceStart;
		V->SetNumberField(TEXT("id"), double(VoiceId));
		V->SetBoolField(TEXT("playing"), bSpeaking);
		V->SetNumberField(TEXT("progress"), VoiceLength > 0 ? FMath::Clamp(Elapsed / VoiceLength, 0.0, 1.0) : -1.0);
		V->SetBoolField(TEXT("done"), !bSpeaking && (bVoiceSeen || Elapsed > 1.0));
		F->SetObjectField(TEXT("voice"), V);
	}
	return OKStoryJson(F);
}

static EOKSceneLook OKStoryLook(const FString& Mode)
{
	return Mode == TEXT("point") ? EOKSceneLook::Point : Mode == TEXT("animation") ? EOKSceneLook::Animation : EOKSceneLook::Camera;
}

// A 16-bit PCM WAV (what ok-tts writes) as speech samples.
static bool OKStoryLoadWav(const FString& Path, TSharedPtr<const FKKSpeechPcm, ESPMode::ThreadSafe>& Out, FString& Error)
{
	TArray<uint8> Bytes;
	if (!FFileHelper::LoadFileToArray(Bytes, *Path)) { Error = TEXT("cannot read ") + Path; return false; }
	if (Bytes.Num() < 12 || FMemory::Memcmp(Bytes.GetData(), "RIFF", 4) || FMemory::Memcmp(Bytes.GetData() + 8, "WAVE", 4)) { Error = TEXT("not a WAV file: ") + Path; return false; }
	auto U16 = [&](int32 At) { return int32(Bytes[At]) | int32(Bytes[At + 1]) << 8; };
	auto U32 = [&](int32 At) { return int32(U16(At)) | int32(U16(At + 2)) << 16; };
	int32 Channels = 0, Rate = 0, Bits = 0, Format = 0;
	for (int32 At = 12; At + 8 <= Bytes.Num();)
	{
		const int32 Size = U32(At + 4), Body = At + 8;
		if (!FMemory::Memcmp(Bytes.GetData() + At, "fmt ", 4) && Body + 16 <= Bytes.Num()) { Format = U16(Body); Channels = U16(Body + 2); Rate = U32(Body + 4); Bits = U16(Body + 14); }
		else if (!FMemory::Memcmp(Bytes.GetData() + At, "data", 4))
		{
			if (Format != 1 || Bits != 16 || Channels < 1 || Rate <= 0) { Error = TEXT("only 16-bit PCM WAV is supported: ") + Path; return false; }
			auto Pcm = MakeShared<FKKSpeechPcm, ESPMode::ThreadSafe>();
			Pcm->Channels = Channels;
			Pcm->SampleRate = Rate;
			const int32 Count = FMath::Min(Size, Bytes.Num() - Body) / 2;
			Pcm->Samples.SetNumUninitialized(Count);
			FMemory::Memcpy(Pcm->Samples.GetData(), Bytes.GetData() + Body, Count * 2);
			Out = Pcm;
			return true;
		}
		At = Body + Size + (Size & 1);
	}
	Error = TEXT("WAV without data: ") + Path;
	return false;
}

void UOKStoryPlayer::StartVoice(uint64 Id, const FString& Who, AKKPreviewActor* Speaker, TSharedPtr<const FKKSpeechPcm, ESPMode::ThreadSafe> Pcm, float Pitch, float Volume)
{
	FString Error;
	if (!Speaker || !Pcm || !Speaker->PlaySpeechPcm(Pcm, Pitch, Error)) { Ack(Id, false, Error.IsEmpty() ? TEXT("speaker not shown") : Error); return; }
	if (Speaker->SpeechAudio) Speaker->SpeechAudio->SetVolumeMultiplier(Volume);
	const double Frames = Pcm->Channels > 0 ? double(Pcm->Samples.Num()) / Pcm->Channels : 0.0;
	VoiceId = Id;
	VoiceWho = Who;
	VoiceLength = Pcm->SampleRate > 0 ? Frames / Pcm->SampleRate / FMath::Max(Pitch, 0.01f) : 0.0;
	VoiceStart = FPlatformTime::Seconds();
	bVoiceSeen = false;
	Ack(Id, true, FString(), [&](FJsonObject& A) { A.SetNumberField(TEXT("duration"), VoiceLength); });
}

void UOKStoryPlayer::RequestTts(uint64 Id, const FString& Who, const FString& Text, const FString& Voice, float Pitch, float Volume)
{
	FString Endpoint = FPlatformMisc::GetEnvironmentVariable(TEXT("OK_TTS_ENDPOINT"));
	if (Endpoint.IsEmpty()) Endpoint = TEXT("http://127.0.0.1:47860");
	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("text"), Text);
	Body->SetStringField(TEXT("personality"), Voice);
	Body->SetNumberField(TEXT("age"), 18);
	Body->SetStringField(TEXT("out_dir"), FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Story/TtsCache")));
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(Endpoint / TEXT("synthesize"));
	Request->SetVerb(TEXT("POST"));
	Request->SetHeader(TEXT("Content-Type"), TEXT("application/json; charset=utf-8"));
	Request->SetContentAsString(OKStoryJson(Body));
	Request->SetTimeout(30.f);
	Request->OnProcessRequestComplete().BindWeakLambda(this, [this, Id, Who, Pitch, Volume](FHttpRequestPtr, FHttpResponsePtr Response, bool bConnected)
	{
		if (!Handle) return;
		FString Error = TEXT("ok-tts did not answer (start it with the street launcher, or set OK_TTS_ENDPOINT)"), Path;
		TSharedPtr<FJsonObject> J = bConnected && Response.IsValid() ? OKStoryParse(Response->GetContentAsString()) : nullptr;
		bool bOk = false;
		if (J && J->TryGetBoolField(TEXT("ok"), bOk) && bOk && J->TryGetStringField(TEXT("path"), Path))
		{
			TSharedPtr<const FKKSpeechPcm, ESPMode::ThreadSafe> Pcm;
			// ok-tts answers with an Ogg (or a WAV).
			const bool bDecoded = Path.EndsWith(TEXT(".wav"), ESearchCase::IgnoreCase) ? OKStoryLoadWav(Path, Pcm, Error) : KKSpeech::DecodeOgg(Path, Pcm, Error);
			if (bDecoded) { StartVoice(Id, Who, Preview(Who), Pcm, Pitch, Volume); return; }
		}
		else if (J) J->TryGetStringField(TEXT("error"), Error);
		Ack(Id, false, Error);
	});
	Request->ProcessRequest();
}

void UOKStoryPlayer::PlaySound(const FString& File, float Volume, uint64 Id)
{
	UOKStudioGame* Game = Studio.Get();
	UWorld* World = GetTickableGameObjectWorld();
	FString Error;
	TSharedPtr<const FKKSpeechPcm, ESPMode::ThreadSafe> Pcm;
	if (!World || !KKSpeech::DecodeOgg(File, Pcm, Error)) { Ack(Id, false, Error.IsEmpty() ? TEXT("no world") : Error); return; }
	UKKSpeechWave* Wave = NewObject<UKKSpeechWave>(Game);
	Wave->Init(Pcm, World->GetAudioDevice().IsValid() ? World->GetAudioDevice()->GetSampleRate() : Pcm->SampleRate);
	if (UAudioComponent* C = UGameplayStatics::SpawnSound2D(World, Wave, Volume))
	{
		Sounds.RemoveAll([](const TStrongObjectPtr<UAudioComponent>& S) { return !S || !S->IsPlaying(); });
		Sounds.Emplace(C);
	}
	Ack(Id, true);
}

void UOKStoryPlayer::Execute(const TSharedPtr<FJsonObject>& C)
{
	UOKStudioGame* Game = Studio.Get();
	const uint64 Id = uint64(C->GetNumberField(TEXT("id")));
	const FString Op = C->GetStringField(TEXT("op"));
	FString Who;
	C->TryGetStringField(TEXT("who"), Who);
	if (!Game || !Game->Document || !Game->SceneActor) { Ack(Id, false, TEXT("studio not running")); return; }
	UOKSceneDocument* Doc = Game->Document;
	auto Character = [&](const FString& W) -> FOKSceneObject*
	{
		const int32* ObjectId = Objects.Find(W);
		FOKSceneObject* O = ObjectId ? Doc->Find(*ObjectId) : nullptr;
		return O && O->Kind == EOKSceneObjectKind::Character ? O : nullptr;
	};
	if (Op == TEXT("load_scene"))
	{
		FString Path = C->GetStringField(TEXT("path")), Error;
		if (FPaths::IsRelative(Path)) Path = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), Path);
		Objects.Reset();
		Waiting.Reset();
		const bool bOk = FPaths::FileExists(Path) && Game->Load(Path, Error);
		if (!FPaths::FileExists(Path)) Error = TEXT("scene not found: ") + Path;
		Game->SyncDocumentNow();
		Ack(Id, bOk, Error, [&](FJsonObject& A)
		{
			TArray<TSharedPtr<FJsonValue>> Cameras;
			const FTransform Scene = Game->SceneActor->GetActorTransform();
			for (const FOKSceneObject& O : Doc->Objects)
			{
				if (O.Kind != EOKSceneObjectKind::Camera) continue;
				const FTransform W = Doc->WorldTransform(O.Id) * Scene;
				TSharedRef<FJsonObject> Cam = MakeShared<FJsonObject>();
				Cam->SetNumberField(TEXT("id"), O.Id);
				Cam->SetArrayField(TEXT("pos"), OKStoryVector(W.GetLocation()));
				Cam->SetArrayField(TEXT("rot"), OKStoryQuat(W.GetRotation()));
				Cam->SetNumberField(TEXT("fov"), O.Camera.Fov);
				Cameras.Add(MakeShared<FJsonValueObject>(Cam));
			}
			A.SetArrayField(TEXT("cameras"), Cameras);
			if (Doc->ActiveCamera >= 0) A.SetNumberField(TEXT("active_camera"), Doc->ActiveCamera);
		});
		return;
	}
	if (Op == TEXT("place") || Op == TEXT("spawn_offstage"))
	{
		const bool bOffstage = Op == TEXT("spawn_offstage");
		const FString Card = C->GetStringField(TEXT("card"));
		const int32 Outfit = int32(C->GetNumberField(TEXT("outfit")));
		bool bVisible = true;
		C->TryGetBoolField(TEXT("visible"), bVisible);
		int32 SceneObject = -1;
		C->TryGetNumberField(TEXT("scene_object"), SceneObject);
		int32 ObjectId;
		if (bOffstage)
		{
			Game->Select(-1);
			ObjectId = Game->AddCharacter(OKStoryCardPath(Card), Outfit);
			if (FOKSceneObject* O = Doc->Find(ObjectId)) O->Name = TEXT("__story_offstage_") + Who;
			Objects.Add(Who, ObjectId);
		}
		else ObjectId = FindOrAddCharacter(Who, Card, Outfit, SceneObject);
		FOKSceneObject* O = Doc->Find(ObjectId);
		if (!O) { Ack(Id, false, TEXT("character could not be added")); return; }
		FVector Pos;
		if (OKStoryReadVector(C, TEXT("pos"), Pos)) O->Transform.SetLocation(Pos);
		double Yaw = 0.0, Scale = 1.0;
		if (C->TryGetNumberField(TEXT("yaw"), Yaw)) O->Transform.SetRotation(FRotator(0.0, Yaw, 0.0).Quaternion());
		if (C->TryGetNumberField(TEXT("scale"), Scale)) O->Transform.SetScale3D(FVector(Scale));
		O->bVisible = bVisible;
		Doc->NotifyChanged();
		if (!Personalities.Contains(Who))
		{
			FKKCharacterFile File;
			FString Error;
			if (UKKCardReader::LoadFromFile(OKStoryCardPath(Card), File, Error)) Personalities.Add(Who, File.Personality);
		}
		if (bVisible) Waiting.Add({ Id, Who, FPlatformTime::Seconds(), true });
		else Ack(Id, true);
		return;
	}
	if (Op == TEXT("release_offstage"))
	{
		if (const int32* ObjectId = Objects.Find(Who))
		{
			const FOKSceneObject* O = Doc->Find(*ObjectId);
			if (O && O->Name.StartsWith(TEXT("__story_offstage_"))) Doc->RemoveObject(*ObjectId);
			Objects.Remove(Who);
		}
		Ack(Id, true);
		return;
	}
	if (Op == TEXT("motion"))
	{
		FOKSceneObject* O = Character(Who);
		if (!O) { Ack(Id, false, TEXT("no character ") + Who); return; }
		const int32 Kind = int32(C->GetNumberField(TEXT("kind"))), Group = int32(C->GetNumberField(TEXT("group")));
		const int32 Category = int32(C->GetNumberField(TEXT("category"))), No = int32(C->GetNumberField(TEXT("no")));
		FString Error;
		if (Game->SceneActor->PreviewOf(O->Id))
		{
			const bool bOk = Game->SceneActor->SetCharacterAnimation(O->Id, Kind, Group, Category, No, Error);
			Ack(Id, bOk, Error);
		}
		else
		{
			// Not built yet: it is built with this animation.
			O->Character.StudioAnimeKind = Kind; O->Character.StudioAnimeGroup = Group; O->Character.StudioAnimeCategory = Category; O->Character.StudioAnimeNo = No;
			O->Character.Animation.Reset();
			Doc->NotifyChanged();
			Ack(Id, true);
		}
		return;
	}
	if (Op == TEXT("face"))
	{
		FOKSceneObject* O = Character(Who);
		if (!O) { Ack(Id, false, TEXT("no character ") + Who); return; }
		O->Character.EyebrowPattern = int32(C->GetNumberField(TEXT("eyebrow")));
		O->Character.EyesPattern = int32(C->GetNumberField(TEXT("eyes")));
		O->Character.MouthPattern = int32(C->GetNumberField(TEXT("mouth")));
		Doc->NotifyChanged();
		Ack(Id, true);
		return;
	}
	if (Op == TEXT("look"))
	{
		FOKSceneObject* O = Character(Who);
		if (!O) { Ack(Id, false, TEXT("no character ") + Who); return; }
		const FString Mode = C->GetStringField(TEXT("mode"));
		O->Character.EyesLook = OKStoryLook(Mode);
		FVector Point;
		if (Mode == TEXT("point") && OKStoryReadVector(C, TEXT("point"), Point))
		{
			// The look point is in the character's own space.
			const FTransform World = Doc->WorldTransform(O->Id) * Game->SceneActor->GetActorTransform();
			O->Character.LookPoint = World.InverseTransformPosition(Point);
		}
		Doc->NotifyChanged();
		Ack(Id, true);
		return;
	}
	if (Op == TEXT("blink") || Op == TEXT("auto_blink"))
	{
		AKKPreviewActor* P = Preview(Who);
		if (Op == TEXT("blink")) { if (P) P->ForceBlink(); }
		else
		{
			bool bOn = true;
			C->TryGetBoolField(TEXT("on"), bOn);
			AutoBlink.Add(Who, bOn);
			AutoBlinkApplied.Remove(Who);
		}
		Ack(Id, true);
		return;
	}
	if (Op == TEXT("voice"))
	{
		FString File;
		double Pitch = 1.0, Volume = 1.0;
		C->TryGetNumberField(TEXT("pitch"), Pitch);
		C->TryGetNumberField(TEXT("volume"), Volume);
		AKKPreviewActor* P = Preview(Who);
		if (!P) { Ack(Id, false, TEXT("speaker not shown: ") + Who); return; }
		if (!C->TryGetStringField(TEXT("file"), File) || File.IsEmpty())
		{
			FString Tts, Text;
			C->TryGetStringField(TEXT("text"), Text);
			if (!C->TryGetStringField(TEXT("tts"), Tts) || Tts.IsEmpty()) { Ack(Id, false, TEXT("no voice file or ok-tts voice")); return; }
			RequestTts(Id, Who, Text, Tts, float(Pitch), float(Volume));
			return;
		}
		if (FPaths::IsRelative(File)) File = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), File);
		TSharedPtr<const FKKSpeechPcm, ESPMode::ThreadSafe> Pcm;
		FString Error;
		const bool bDecoded = File.EndsWith(TEXT(".wav"), ESearchCase::IgnoreCase) ? OKStoryLoadWav(File, Pcm, Error) : KKSpeech::DecodeOgg(File, Pcm, Error);
		if (!bDecoded) { Ack(Id, false, Error); return; }
		StartVoice(Id, Who, P, Pcm, float(Pitch), float(Volume));
		return;
	}
	if (Op == TEXT("stop_voice"))
	{
		if (AKKPreviewActor* P = Preview(Who)) P->StopSpeech();
		if (VoiceWho == Who) VoiceId = 0;
		Ack(Id, true);
		return;
	}
	if (Op == TEXT("se"))
	{
		FString File = C->GetStringField(TEXT("file"));
		if (FPaths::IsRelative(File)) File = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), File);
		PlaySound(File, float(C->GetNumberField(TEXT("volume"))), Id);
		return;
	}
	if (Op == TEXT("thumbnail"))
	{
		const FString Path = C->GetStringField(TEXT("path"));
		const int32 W = int32(C->GetNumberField(TEXT("width"))), H = int32(C->GetNumberField(TEXT("height")));
		UCameraComponent* Camera = Game->Viewer ? Game->Viewer->GetCameraComponent() : nullptr;
		TArray<FColor> Pixels;
		FString Error;
		// A scene capture: the view without the story's UI.
		bool bOk = Camera && FOKSceneCard::Capture(Game->SceneActor, Camera->GetComponentTransform(), Camera->FieldOfView, &Camera->PostProcessSettings, W, H, false, Pixels, Error);
		if (bOk)
		{
			IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
			bOk = FImageUtils::SaveImageByExtension(*Path, FImageView(Pixels.GetData(), W, H));
			if (!bOk) Error = TEXT("cannot write ") + Path;
		}
		Thumbnails.Remove(Path);
		Ack(Id, bOk, Error);
		return;
	}
	if (Op == TEXT("end"))
	{
		Ack(Id, true);
		bEndRequested = true;
		return;
	}
	Ack(Id, false, TEXT("unknown command ") + Op);
}

UTexture2D* UOKStoryPlayer::Thumbnail(const FString& Path)
{
	if (Path.IsEmpty()) return nullptr;
	if (TStrongObjectPtr<UTexture2D>* Found = Thumbnails.Find(Path)) return Found->Get();
	UTexture2D* T = FPaths::FileExists(Path) ? FImageUtils::ImportFileAsTexture2D(Path) : nullptr;
	Thumbnails.Add(Path, TStrongObjectPtr<UTexture2D>(T));
	return T;
}


void UOKStoryPlayer::UpdatePortrait(const TSharedPtr<FJsonObject>& Portrait)
{
	UOKStudioGame* Game = Studio.Get();
	bool bVisible = false;
	Portrait->TryGetBoolField(TEXT("visible"), bVisible);
	FString Who;
	Portrait->TryGetStringField(TEXT("who"), Who);
	AKKPreviewActor* P = bVisible ? Preview(Who) : nullptr;
	if (!P || !Game || !Game->SceneActor || !Game->Viewer) return;
	PortraitCapture.Update(Game->SceneActor, Game->SceneActor->GetWorld(), Portrait,
		[P](USceneCaptureComponent2D* C) { C->ShowOnlyActors.Reset(); C->ShowOnlyActors.Add(P); },
		&Game->Viewer->GetCameraComponent()->PostProcessSettings);
}

void UOKStoryPlayer::ApplyFrame()
{
	UOKStudioGame* Game = Studio.Get();
	if (!Frame || !Game || !Game->Viewer) return;
	const TSharedPtr<FJsonObject>* Camera = nullptr;
	if (Frame->TryGetObjectField(TEXT("camera"), Camera))
	{
		FVector Pos;
		FQuat Rot;
		if (OKStoryReadVector(*Camera, TEXT("pos"), Pos) && OKStoryReadQuat(*Camera, TEXT("rot"), Rot))
		{
			Game->Viewer->SetActorLocationAndRotation(Pos, Rot);
			Game->Viewer->GetCameraComponent()->SetFieldOfView(float((*Camera)->GetNumberField(TEXT("fov"))));
		}
	}
	const TSharedPtr<FJsonObject>* Portrait = nullptr;
	if (Frame->TryGetObjectField(TEXT("portrait"), Portrait)) UpdatePortrait(*Portrait);
}

void UOKStoryPlayer::Tick(float DeltaTime)
{
	if (bEndRequested || !Studio.IsValid()) { Stop(); return; }
	CheckWaiting();
	// Automatic blinking stays as the director asked, also on characters built after the request.
	for (const auto& Pair : AutoBlink)
	{
		AKKPreviewActor* P = Preview(Pair.Key);
		if (P && AutoBlinkApplied.FindRef(Pair.Key) != P) { P->SetAutomaticBlink(Pair.Value); AutoBlinkApplied.Add(Pair.Key, P); }
	}
	const char* Text = Api->Tick(Handle, TCHAR_TO_UTF8(*FactsJson(DeltaTime)));
	if (!Text) { UE_LOG(LogTemp, Error, TEXT("OK story tick failed: %s"), *TakeError()); return; }
	Frame = OKStoryParse(Api->Take(Text));
	if (!Frame) return;
	const TArray<TSharedPtr<FJsonValue>>* Log = nullptr;
	if (Frame->TryGetArrayField(TEXT("log"), Log)) for (const TSharedPtr<FJsonValue>& L : *Log) { UE_LOG(LogTemp, Warning, TEXT("OK story: %s"), *L->AsString()); SelfTestLog.Add(L->AsString()); }
	const TArray<TSharedPtr<FJsonValue>>* Commands = nullptr;
	if (Frame->TryGetArrayField(TEXT("commands"), Commands)) for (const TSharedPtr<FJsonValue>& Cmd : *Commands) if (Cmd->AsObject()) Execute(Cmd->AsObject());
	ApplyFrame();
	const TArray<TSharedPtr<FJsonValue>>* Probes = nullptr;
	if (Frame->TryGetArrayField(TEXT("probes"), Probes) && Probes->Num()) RunProbes(*Probes);
	if (bSelfTest) TickSelfTest();
	PollControl();
}

void UOKStoryPlayer::PollControl()
{
	const double Now = FPlatformTime::Seconds();
	if (Now - ControlPolled < 0.2) return;
	ControlPolled = Now;
	const FString Dir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("UserData/Story/control"));
	TArray<FString> Files;
	IFileManager::Get().FindFiles(Files, *(Dir / TEXT("*.json")), true, false);
	for (const FString& Name : Files)
	{
		if (Name.EndsWith(TEXT(".out.json"))) continue;
		const FString Path = Dir / Name;
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *Path)) continue;
		IFileManager::Get().Delete(*Path);
		const TSharedPtr<FJsonObject> Request = OKStoryParse(Text);
		FString Answer = TEXT("{\"error\":\"unreadable request\"}");
		const FString Cmd = Request ? Request->GetStringField(TEXT("cmd")) : FString();
		if (Cmd == TEXT("state")) Answer = Query(TEXT("{\"q\":\"state\"}"));
		else if (Cmd == TEXT("input"))
		{
			const TSharedPtr<FJsonObject>* Event = nullptr;
			if (Request->TryGetObjectField(TEXT("event"), Event)) { Input(OKStoryJson((*Event).ToSharedRef())); Answer = TEXT("{\"ok\":true}"); }
		}
		else if (Cmd == TEXT("reload"))
		{
			FString Out, Error;
			const bool bOk = Reload(Out, Error);
			TSharedRef<FJsonObject> A = MakeShared<FJsonObject>();
			A->SetBoolField(TEXT("ok"), bOk);
			A->SetStringField(TEXT("message"), bOk ? Out : Error);
			Answer = OKStoryJson(A);
		}
		else if (Cmd == TEXT("shot"))
		{
			const FString Shot = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Story/Shots") / FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S_%s.png")));
			FScreenshotRequest::RequestScreenshot(Shot, true, false);
			TSharedRef<FJsonObject> A = MakeShared<FJsonObject>();
			A->SetStringField(TEXT("path"), Shot);
			A->SetStringField(TEXT("note"), TEXT("written at the end of this frame"));
			Answer = OKStoryJson(A);
		}
		// The answer appears whole (written aside, then renamed).
		const FString Out = Path.LeftChop(5) + TEXT(".out.json");
		FFileHelper::SaveStringToFile(Answer, *(Out + TEXT(".tmp")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
		IFileManager::Get().Move(*Out, *(Out + TEXT(".tmp")));
	}
}

void UOKStoryPlayer::RunProbes(const TArray<TSharedPtr<FJsonValue>>& Probes)
{
	UOKStudioGame* Game = Studio.Get();
	if (!Game || !Game->SceneActor || !Game->Document) return;
	// Characters never block a view of themselves or of each other: only the set (map, items) does.
	ProbeRunner.Run(Game->SceneActor, Game->SceneActor->GetWorld(), Probes, [Game](USceneCaptureComponent2D* C)
	{
		for (const FOKSceneObject& O : Game->Document->Objects)
			if (O.Kind == EOKSceneObjectKind::Character)
				if (AKKPreviewActor* A = Game->SceneActor->PreviewOf(O.Id)) C->HiddenActors.Add(A);
	});
}

// ---- self test ------------------------------------------------------------------------------------------------------

void UOKStoryPlayer::StartSelfTest(bool bExit)
{
	bSelfTest = true;
	bSelfTestExit = bExit;
	bQuitOnEnd = bExit;
	SelfTestDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Story/SelfTest") / FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S")));
	IFileManager::Get().MakeDirectory(*SelfTestDir, true);
	SelfTestFrames = 0;
}

void UOKStoryPlayer::TickSelfTest()
{
	++SelfTestFrames;
	const TSharedPtr<FJsonObject> State = OKStoryParse(Query(TEXT("{\"q\":\"state\"}")));
	if (!State) return;
	const FString Phase = State->GetStringField(TEXT("phase"));
	FString Line;
	State->TryGetStringField(TEXT("line"), Line);
	const double Now = FPlatformTime::Seconds();
	auto Shot = [&](const FString& Name)
	{
		const FString Path = SelfTestDir / FString::Printf(TEXT("%02d_%s.png"), SelfTestShots++, *Name);
		FScreenshotRequest::RequestScreenshot(Path, true, false);
		SelfTestLog.Add(TEXT("shot ") + Path);
	};
	auto Report = [&](bool bPassed)
	{
		TSharedRef<FJsonObject> R = MakeShared<FJsonObject>();
		R->SetStringField(TEXT("story"), StoryFile);
		R->SetBoolField(TEXT("passed"), bPassed);
		R->SetNumberField(TEXT("frames"), SelfTestFrames);
		TArray<TSharedPtr<FJsonValue>> Lines;
		for (const FString& L : SelfTestLog) Lines.Add(MakeShared<FJsonValueString>(L));
		R->SetArrayField(TEXT("log"), Lines);
		FString Out;
		FJsonSerializer::Serialize(R, TJsonWriterFactory<>::Create(&Out));
		FFileHelper::SaveStringToFile(Out, *(SelfTestDir / TEXT("report.json")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
		UE_LOG(LogTemp, Display, TEXT("OK story self test %s: %s"), bPassed ? TEXT("passed") : TEXT("FAILED"), *SelfTestDir);
	};
	if (SelfTestFrames > 60 * 600) { SelfTestLog.Add(TEXT("timeout")); Report(false); bSelfTest = false; Input(TEXT("{\"type\":\"button\",\"id\":\"quit\"}")); return; }
	if (Phase == TEXT("ended"))
	{
		if (SelfTestLine != TEXT("(ended)")) { SelfTestLine = TEXT("(ended)"); SelfTestLineSince = Now; return; }
		if (Now - SelfTestLineSince > 1.5)
		{
			Shot(TEXT("ended"));
			Report(true);
			bSelfTest = false;
			// The end after the screenshot is written.
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([W = TWeakObjectPtr<UOKStoryPlayer>(this)](float) { if (W.IsValid()) W->Input(TEXT("{\"type\":\"click\"}")); return false; }), 1.0f);
		}
		return;
	}
	if (Phase != TEXT("line")) return;
	if (Line != SelfTestLine) { SelfTestLine = Line; SelfTestLineSince = Now; SelfTestLog.Add(TEXT("line ") + Line + TEXT(" ") + Query(TEXT("{\"q\":\"state\"}"))); return; }
	// What the director saw when the line had settled (bones, camera): for checking framing.
	if (!SelfTestLog.Contains(TEXT("facts ") + Line) && Now - SelfTestLineSince > 2.0)
	{
		SelfTestLog.Add(TEXT("facts ") + Line);
		SelfTestLog.Add(Query(TEXT("{\"q\":\"facts\"}")));
		// The view actually shown (the player's camera) and a capture through the story's viewer, to compare.
		UOKStudioGame* Game = Studio.Get();
		APlayerController* PC = Game && Game->SceneActor ? Game->SceneActor->GetWorld()->GetFirstPlayerController() : nullptr;
		if (PC && PC->PlayerCameraManager)
			SelfTestLog.Add(FString::Printf(TEXT("view target %s camera %s rot %s fov %.1f"), *GetNameSafe(PC->GetViewTarget()),
				*PC->PlayerCameraManager->GetCameraLocation().ToString(), *PC->PlayerCameraManager->GetCameraRotation().ToString(), PC->PlayerCameraManager->GetFOVAngle()));
		if (Game)
		{
			FString Error;
			const FString Path = SelfTestDir / FString::Printf(TEXT("capture_%s.png"), *Line);
			if (Game->CaptureToPng(Path, 800, 450, Error)) SelfTestLog.Add(TEXT("capture ") + Path);
			else SelfTestLog.Add(TEXT("capture failed ") + Error);
		}
	}
	const double Age = Now - SelfTestLineSince;
	bool bRevealed = false, bMoving = false;
	State->TryGetBoolField(TEXT("revealed"), bRevealed);
	State->TryGetBoolField(TEXT("camera_moving"), bMoving);
	// Mid-typing once, then the settled line.
	if (Age > 0.25 && Age < 0.25 + 0.02 && !bRevealed) Shot(Line + TEXT("_typing"));
	if (Age < 2.5 || (bMoving && Age < 6.0)) return;
	if (!bRevealed) { Input(TEXT("{\"type\":\"click\"}")); return; }
	if (Age < 2.6) return;
	if (SelfTestShots >= 0 && !SelfTestLog.Contains(TEXT("done ") + Line))
	{
		Shot(Line);
		SelfTestLog.Add(TEXT("done ") + Line);
		SelfTestLineSince = Now - 2.5 + 0.5; // the screenshot is written at the end of the frame: wait a little
		return;
	}
	// Once, on the third line: the panels (save into slot 1, load, backlog, settings, menu), a screenshot of each.
	static const TCHAR* Tour[][2] = {
		{ TEXT("{\"type\":\"button\",\"id\":\"save\"}"), TEXT("panel_save") },
		{ TEXT("{\"type\":\"button\",\"id\":\"slot:01\"}"), nullptr },
		{ TEXT("{\"type\":\"button\",\"id\":\"load\"}"), TEXT("panel_load") },
		{ TEXT("{\"type\":\"button\",\"id\":\"close\"}"), nullptr },
		{ TEXT("{\"type\":\"button\",\"id\":\"backlog\"}"), TEXT("panel_backlog") },
		{ TEXT("{\"type\":\"button\",\"id\":\"close\"}"), nullptr },
		{ TEXT("{\"type\":\"button\",\"id\":\"settings\"}"), TEXT("panel_settings") },
		{ TEXT("{\"type\":\"button\",\"id\":\"close\"}"), nullptr },
		{ TEXT("{\"type\":\"key\",\"key\":\"F7\"}"), TEXT("panel_outline") },
		{ TEXT("{\"type\":\"key\",\"key\":\"F7\"}"), nullptr },
		{ TEXT("{\"type\":\"key\",\"key\":\"Escape\"}"), TEXT("panel_menu") },
		{ TEXT("{\"type\":\"button\",\"id\":\"close\"}"), nullptr },
	};
	int32 LinesDone = 0;
	for (const FString& L : SelfTestLog) LinesDone += L.StartsWith(TEXT("done ")) ? 1 : 0;
	if (LinesDone >= 3 && SelfTestTour < int32(UE_ARRAY_COUNT(Tour)) + 1)
	{
		if (Now - SelfTestTourAt < 1.2) return;
		if (!SelfTestTourShot.IsEmpty()) { Shot(SelfTestTourShot); SelfTestTourShot.Reset(); }
		SelfTestTour = FMath::Max(SelfTestTour, 0);
		if (SelfTestTour < int32(UE_ARRAY_COUNT(Tour)))
		{
			Input(Tour[SelfTestTour][0]);
			SelfTestLog.Add(FString(TEXT("tour ")) + Tour[SelfTestTour][0]);
			if (Tour[SelfTestTour][1]) SelfTestTourShot = Tour[SelfTestTour][1];
		}
		++SelfTestTour;
		SelfTestTourAt = Now;
		return;
	}
	const TArray<TSharedPtr<FJsonValue>>* Choices = nullptr;
	if (Frame && Frame->TryGetArrayField(TEXT("choices"), Choices) && Choices->Num())
	{
		int32 Pick = 0;
		// -OKStoryChoice=N: the Nth choice where it is enabled (other routes), else the first enabled one.
		int32 Wanted = 0;
		FParse::Value(FCommandLine::Get(), TEXT("OKStoryChoice="), Wanted);
		if (Choices->IsValidIndex(Wanted) && (*Choices)[Wanted]->AsObject()->GetBoolField(TEXT("enabled"))) Pick = Wanted;
		else for (int32 I = 0; I < Choices->Num(); ++I) if ((*Choices)[I]->AsObject()->GetBoolField(TEXT("enabled"))) { Pick = I; break; }
		Input(FString::Printf(TEXT("{\"type\":\"choose\",\"index\":%d}"), Pick));
		SelfTestLog.Add(FString::Printf(TEXT("choose %d at %s"), Pick, *Line));
		return;
	}
	Input(TEXT("{\"type\":\"click\"}"));
}

// ---- console --------------------------------------------------------------------------------------------------------

static UWorld* OKStoryWorld()
{
	if (GEngine && GEngine->GameViewport) return GEngine->GameViewport->GetWorld();
	return nullptr;
}

static FAutoConsoleCommand GOKStoryPlay(TEXT("OKStory.Play"), TEXT("OKStory.Play <story.okstory.json>: plays a galgame story on the OK Studio scene."),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
	{
		FString Error;
		if (!Args.Num() || !UOKStoryPlayer::Start(OKStoryWorld(), Args[0], Error)) UE_LOG(LogTemp, Error, TEXT("OKStory.Play: %s"), Args.Num() ? *Error : TEXT("which story?"));
	}));
static FAutoConsoleCommand GOKStoryStop(TEXT("OKStory.Stop"), TEXT("OKStory.Stop: ends the story."),
	FConsoleCommandDelegate::CreateLambda([] { if (UOKStoryPlayer* P = UOKStoryPlayer::Get()) P->Stop(); }));
static FAutoConsoleCommand GOKStoryInput(TEXT("OKStory.Input"), TEXT("OKStory.Input <json>: one input event for the story director."),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
	{
		if (UOKStoryPlayer* P = UOKStoryPlayer::Get()) P->Input(FString::Join(Args, TEXT(" ")));
	}));
static FAutoConsoleCommand GOKStoryState(TEXT("OKStory.State"), TEXT("OKStory.State: the director's state."),
	FConsoleCommandDelegate::CreateLambda([] { if (UOKStoryPlayer* P = UOKStoryPlayer::Get()) UE_LOG(LogTemp, Display, TEXT("OK story: %s"), *P->Query(TEXT("{\"q\":\"state\"}"))); }));
static FAutoConsoleCommand GOKStoryReload(TEXT("OKStory.Reload"), TEXT("OKStory.Reload: swaps in a newer ok_story.dll (state kept) or re-reads the story files."),
	FConsoleCommandDelegate::CreateLambda([]
	{
		UOKStoryPlayer* P = UOKStoryPlayer::Get();
		FString Out, Error;
		if (!P) { UE_LOG(LogTemp, Display, TEXT("OK story: none playing")); }
		else if (P->Reload(Out, Error)) { UE_LOG(LogTemp, Display, TEXT("OK story: %s"), *Out); }
		else { UE_LOG(LogTemp, Warning, TEXT("OK story reload failed, keeping the previous one: %s"), *Error); }
	}));
static FAutoConsoleCommand GOKStorySelfTest(TEXT("OKStory.SelfTest"), TEXT("OKStory.SelfTest <story> [exit]: clicks through a story with screenshots per line (Saved/Story/SelfTest)."),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
	{
		FString Error;
		UOKStoryPlayer* P = Args.Num() ? UOKStoryPlayer::Start(OKStoryWorld(), Args[0], Error) : nullptr;
		if (!P) { UE_LOG(LogTemp, Error, TEXT("OKStory.SelfTest: %s"), Args.Num() ? *Error : TEXT("which story?")); return; }
		P->StartSelfTest(Args.Contains(TEXT("exit")));
	}));
