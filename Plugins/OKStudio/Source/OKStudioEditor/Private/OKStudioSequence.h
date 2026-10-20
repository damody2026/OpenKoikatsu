#pragma once

#include "CoreMinimal.h"

class UOKSceneDocument;
class ULevelSequence;

/**
 * OK Studio timeline -> Level Sequence (Sequencer / Movie Render Queue). Each object with keys becomes a spawnable
 * (camera: CineCameraActor, light: its light actor, effect: NiagaraActor; characters and items: a TargetPoint that
 * marks where they are, since they are drawn by OK sessions) with a transform track keyed like the timeline
 * (linear, constant for steps, cubic for eases). The scene's active camera gets the camera cut.
 */
struct FOKStudioSequence
{
	/** Writes `/Game/OKStudio/Sequences/<Name>`. OutReport: what was exported. */
	static ULevelSequence* Export(const UOKSceneDocument* Document, const FString& Name, FString& OutReport, FString& OutError);
};
