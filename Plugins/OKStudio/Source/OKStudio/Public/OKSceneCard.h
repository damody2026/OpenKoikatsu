#pragma once

#include "CoreMinimal.h"

class AActor;
class AOKSceneActor;
class UOKSceneDocument;
struct FPostProcessSettings;

/**
 * OK scene card: an APNG (1280x720; the scene view, then front, side and top views, 3 seconds each, looping) whose
 * private `okSc` chunk holds the scene (.okscene JSON) and the OK character cards it uses, so the card opens anywhere
 * the OK base library is. Opening also accepts original KStudio scene cards (imported) and plain .okscene files.
 */
struct OKSTUDIO_API FOKSceneCard
{
	enum class EKind : uint8 { None, OkScene, KStudio, Json };

	static constexpr int32 Width = 1280;
	static constexpr int32 Height = 720;
	static constexpr int32 SecondsPerView = 3;

	/** What the file is (by its contents, not its name). */
	static EKind Kind(const FString& Path);

	/** One synchronous capture of the scene actor's world through a camera, as RGBA pixels (alpha 255; transparent:
	 *  only ShowOnly actors drawn, the rest clear). */
	static bool Capture(AOKSceneActor* Scene, const FTransform& View, float FovDegrees, const FPostProcessSettings* Post,
		int32 InWidth, int32 InHeight, bool bTransparent, TArray<FColor>& OutPixels, FString& OutError);

	/** The four card views as PNGs: the given view, then front, side and top framed on the scene's objects. */
	static bool CaptureViews(AOKSceneActor* Scene, const FTransform& View, float FovDegrees, const FPostProcessSettings* Post,
		TArray<TArray<uint8>>& OutPngs, FString& OutError);

	/** Writes the scene card (views from CaptureViews). Character cards are embedded; the document is not changed. */
	static bool Write(const UOKSceneDocument* Document, const TArray<TArray<uint8>>& Views, const FString& Path, FString& OutError);

	/** Opens an OK scene card, a KStudio scene card or an .okscene into the document. Embedded / imported character
	 *  cards go to `<scene directory>/<name>_cards/`. OutNote: what was done (import report, kind). */
	static bool Open(const FString& Path, UOKSceneDocument* Document, FString& OutError, FString* OutNote = nullptr);

	/** Where the character cards of scene `Path` go when it is opened: beside it inside this project's UserData/Studio,
	 * else UserData/Studio/import/<name>_<path hash>_cards. */
	static FString CardDirectoryFor(const FString& Path);

	/** The scene JSON and embedded cards of an OK scene card (no files written). */
	static bool ReadPayload(const TArray<uint8>& Bytes, FString& OutSceneJson, TArray<TArray<uint8>>& OutCards, FString& OutError);
};
