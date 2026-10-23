#pragma once

#include "CoreMinimal.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/StrongObjectPtr.h"

class FJsonObject;
class FJsonValue;
class UMaterial;
class UTexture;
class UMaterialInstanceDynamic;
class USceneCaptureComponent2D;
class UTextureRenderTarget2D;
struct FPostProcessSettings;

/**
 * The live portrait of a story frame (ok-story's "portrait"): an orthographic capture of one character only, its
 * colour and its coverage, put together by a UI material whose background is transparent. Used by the OK Studio story
 * player and by the street's conversations.
 */
/** The portrait's UI material as a saved asset (cooked games cannot build materials at run time). */
#define OK_STORY_PORTRAIT_MATERIAL TEXT("/Game/OKStudio/Story/M_StoryPortrait.M_StoryPortrait")

#if WITH_EDITOR
/** Builds the portrait's UI material in `Outer` (sampler defaults: textures of the capture targets' formats). */
OKSTUDIO_API UMaterial* OKStoryBuildPortraitMaterial(UObject* Outer, FName Name, EObjectFlags Flags, UTexture* Color, UTexture* Depth);
#endif

/**
 * Double-buffered: the view draws the front picture while the next one (colour, then its coverage, a frame apart) is
 * rendered into the back one; the two swap once both are done. The main view never waits for a portrait.
 */
class OKSTUDIO_API FOKStoryPortraitCapture
{
public:
	/** Captures as the frame's portrait asks. `ShowOnly` fills a capture's show-only list with the speaker. */
	void Update(UObject* Outer, UWorld* World, const TSharedPtr<FJsonObject>& Portrait, TFunctionRef<void(USceneCaptureComponent2D*)> ShowOnly,
		const FPostProcessSettings* Post);
	/** What a story view draws (null before the first capture, or where materials cannot be made). */
	UMaterialInstanceDynamic* Material() const { return Mid.Get(); }
	void Release();

private:
	TStrongObjectPtr<USceneCaptureComponent2D> Color, Coverage;
	// [0] and [1]: Front is shown, the other one is being rendered.
	TStrongObjectPtr<UTextureRenderTarget2D> ColorTarget[2], CoverageTarget[2];
	int32 Front = 0;
	bool bShown = false;
	TStrongObjectPtr<UMaterialInstanceDynamic> Mid;
	// The back picture's colour is rendered; its coverage (same framing) comes on the next frame, then they swap.
	bool bCoveragePending = false;
	FVector PendingPos = FVector::ZeroVector;
	FQuat PendingRot = FQuat::Identity;
	float PendingWidth = 0.f;
};

/**
 * Lines of sight ok-story asks for (a frame's "probes"): a narrow depth capture from each start towards its end, the
 * answer how far it could see. Answers go back in the next facts ("probes").
 */
class OKSTUDIO_API FOKStoryProbes
{
public:
	/**
	 * Queues the frame's probes and renders a few of the queued ones; call it every frame (with no new probes too).
	 * `Hide` adds what must not block a view (the characters) to the capture's hidden lists. Nothing here waits for the
	 * GPU: each depth is copied back asynchronously and read on the render thread once it has arrived.
	 */
	void Run(UObject* Outer, UWorld* World, const TArray<TSharedPtr<FJsonValue>>& Probes, TFunctionRef<void(USceneCaptureComponent2D*)> Hide);
	/** The answers that arrived since the last call, as the facts' "probes" array. */
	TArray<TSharedPtr<FJsonValue>> TakeAnswers();
	void Release();

private:
	struct FProbeRequest { uint64 Id = 0; FVector From = FVector::ZeroVector, To = FVector::ZeroVector; };
	TStrongObjectPtr<USceneCaptureComponent2D> Capture;
	TStrongObjectPtr<UTextureRenderTarget2D> Target;
	TArray<FProbeRequest> Queue;
	/** Readbacks in flight (render thread) and the answers they gave (either thread, under its lock). */
	TSharedPtr<struct FOKStoryProbeReadbacks, ESPMode::ThreadSafe> Readbacks;
};
