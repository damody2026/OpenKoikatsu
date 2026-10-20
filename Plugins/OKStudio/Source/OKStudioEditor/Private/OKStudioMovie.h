#pragma once

#include "CoreMinimal.h"

class ULevelSequence;

/**
 * Movie Render Queue for an exported OK Studio sequence (FOKStudioSequence): the scene spawnable shows the real scene,
 * frames are rendered in PIE with temporal anti-aliasing samples into a PNG sequence, then encoded to H.264 mp4 with
 * Tools/ffmpeg (UOKStudioGame::EncodeMp4).
 */
class FOKStudioMovie
{
public:
	/** Starts the render (asynchronous). Done gets the mp4 (empty on failure) and an error. */
	static bool Render(ULevelSequence* Sequence, int32 Width, int32 Height, int32 TemporalSamples, TFunction<void(const FString& Movie, const FString& Error)> Done, FString& OutError);
	static bool IsRendering();
	/** Whether the project renders skinned meshes through the GPU skin cache (Render refuses without it). */
	static bool HasGpuSkinCache();
};
