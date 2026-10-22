#pragma once

#include "CoreMinimal.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/StrongObjectPtr.h"

class FJsonObject;
class FJsonValue;
class UMaterialInstanceDynamic;
class USceneCaptureComponent2D;
class UTextureRenderTarget2D;
struct FPostProcessSettings;

/**
 * The live portrait of a story frame (ok-story's "portrait"): an orthographic capture of one character only, its
 * colour and its coverage, put together by a UI material whose background is transparent. Used by the OK Studio story
 * player and by the street's conversations.
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
	TStrongObjectPtr<UTextureRenderTarget2D> ColorTarget, CoverageTarget;
	TStrongObjectPtr<UMaterialInstanceDynamic> Mid;
};

/**
 * Lines of sight ok-story asks for (a frame's "probes"): a narrow depth capture from each start towards its end, the
 * answer how far it could see. Answers go back in the next facts ("probes").
 */
class OKSTUDIO_API FOKStoryProbes
{
public:
	/** `Hide` adds what must not block a view (the characters) to the capture's hidden lists. */
	void Run(UObject* Outer, UWorld* World, const TArray<TSharedPtr<FJsonValue>>& Probes, TFunctionRef<void(USceneCaptureComponent2D*)> Hide);
	/** The answers since the last call, as the facts' "probes" array. */
	TArray<TSharedPtr<FJsonValue>> TakeAnswers();
	void Release();

private:
	TStrongObjectPtr<USceneCaptureComponent2D> Capture;
	TStrongObjectPtr<UTextureRenderTarget2D> Target;
	TArray<TPair<uint64, double>> Answers;
};
