#pragma once

#include "CoreMinimal.h"
#include "Tickable.h"
#include "UObject/Object.h"
#include "UObject/StrongObjectPtr.h"
#include "OKStoryPlayer.generated.h"

class UOKStudioGame;
class AKKPreviewActor;
class USceneCaptureComponent2D;
class UTextureRenderTarget2D;
class UMaterialInstanceDynamic;
class SOKStoryView;
class FJsonObject;
class UAudioComponent;

/**
 * Plays a galgame story (.okstory.json) on the OK Studio scene: the presentation side of ok_story.dll
 * (Tools/ok-story, Docs/GALGAME_DIALOGUE_PLAN.md). Every decision, clock and interpolation is the DLL's; this reports
 * facts (frame time, bones, voice progress), forwards input, executes the DLL's commands on the scene and draws its
 * frame (dialog box, live portrait, choices, title, panels).
 */
UCLASS()
class OKSTUDIO_API UOKStoryPlayer : public UObject, public FTickableGameObject
{
	GENERATED_BODY()

public:
	static UOKStoryPlayer* Get();
	/** Starts a story in the running studio (started on an empty scene when none runs). */
	static UOKStoryPlayer* Start(UWorld* World, const FString& StoryFile, FString& OutError);
	void Stop();

	/** Closing the story (its end, or quit): the studio comes back unless bQuitOnEnd. */
	bool bQuitOnEnd = false;
	FSimpleMulticastDelegate OnEnded;

	/** One input event for the director (JSON, include/ok_story.h). */
	void Input(const FString& Json);
	/** The director's answer to a query ({"q": "state"} ...). */
	FString Query(const FString& Json);
	/** Swaps in a newer ok_story.dll build (keeping the director's state) or re-reads the story files. */
	bool Reload(FString& Out, FString& Error);

	/** The last frame (JSON object) and the story file. */
	TSharedPtr<FJsonObject> Frame;
	FString StoryFile;
	/** The live portrait's picture (what the view draws), or null. */
	UMaterialInstanceDynamic* PortraitMaterial() const { return PortraitMid.Get(); }
	/** Save thumbnails, loaded once per file and time. */
	class UTexture2D* Thumbnail(const FString& Path);

	/** Self test (OKStory.SelfTest): clicks through the story, screenshots per line, report in Saved/Story/SelfTest. */
	void StartSelfTest(bool bExit);

	// FTickableGameObject
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UOKStoryPlayer, STATGROUP_Tickables); }
	virtual bool IsTickable() const override { return Handle != nullptr; }
	virtual UWorld* GetTickableGameObjectWorld() const override;

	struct FApi;

private:
	FApi* Api = nullptr;
	void* Handle = nullptr;
	FString DllPath, SourceDll, ReloadDir;
	FDateTime SourceStamp;
	TWeakObjectPtr<UOKStudioGame> Studio;
	TSharedPtr<SOKStoryView> View;
	bool bStartedStudio = false;

	/** Story character → scene object id (stage), and its offstage copy. */
	TMap<FString, int32> Objects;
	/** Commands waiting for something to finish before their result goes back. */
	struct FWaiting { uint64 Id = 0; FString Who; double Since = 0.0; bool bPersonality = false; };
	TArray<FWaiting> Waiting;
	TMap<FString, int32> Personalities;
	/** The voice playing: its command, speaker, start and length. */
	uint64 VoiceId = 0;
	FString VoiceWho;
	double VoiceStart = 0.0, VoiceLength = 0.0;
	bool bVoiceSeen = false;

	TStrongObjectPtr<USceneCaptureComponent2D> PortraitColor, PortraitDepth;
	TStrongObjectPtr<UTextureRenderTarget2D> PortraitColorTarget, PortraitDepthTarget;
	TStrongObjectPtr<UMaterialInstanceDynamic> PortraitMid;
	TMap<FString, TStrongObjectPtr<class UTexture2D>> Thumbnails;
	/** Lines of sight the director asked for: a narrow depth capture each, answered in the next facts. */
	TStrongObjectPtr<USceneCaptureComponent2D> Probe;
	TStrongObjectPtr<UTextureRenderTarget2D> ProbeTarget;
	TArray<TPair<uint64, double>> ProbeAnswers;
	void RunProbes(const TArray<TSharedPtr<class FJsonValue>>& Probes);
	TArray<TStrongObjectPtr<UAudioComponent>> Sounds;

	/** Self test state. */
	bool bSelfTest = false, bSelfTestExit = false;
	int32 SelfTestShots = 0, SelfTestFrames = 0;
	FString SelfTestLine, SelfTestDir;
	TArray<FString> SelfTestLog;
	double SelfTestLineSince = 0.0;
	void TickSelfTest();

	bool bEndRequested = false;
	/** Automatic blinking as the director wants it, and the preview it was applied to. */
	TMap<FString, bool> AutoBlink;
	TMap<FString, TWeakObjectPtr<AKKPreviewActor>> AutoBlinkApplied;

	bool Open(const FString& File, FString& Error);
	bool OpenWith(FApi* Found, const FString& Path, const FString& File, FString& Error);
	void Close();
	FString TakeError();
	FString FactsJson(float DeltaTime);
	void Execute(const TSharedPtr<FJsonObject>& Command);
	void Ack(uint64 Id, bool bOk, const FString& Error, TFunctionRef<void(FJsonObject&)> Extra);
	void Ack(uint64 Id, bool bOk, const FString& Error = FString());
	void CheckWaiting();
	void ApplyFrame();
	void UpdatePortrait(const TSharedPtr<FJsonObject>& Portrait);
	AKKPreviewActor* Preview(const FString& Who) const;
	int32 FindOrAddCharacter(const FString& Who, const FString& Card, int32 Outfit, int32 SceneObject);
	void PlaySound(const FString& File, float Volume, AKKPreviewActor* Speaker, uint64 Id, float Pitch);
};
