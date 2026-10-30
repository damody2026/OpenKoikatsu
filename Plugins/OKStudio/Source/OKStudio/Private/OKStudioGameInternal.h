#pragma once

#include "CoreMinimal.h"

class AKKPreviewActor;

/** Helpers of OKStudioGame.cpp shared with its self tests (OKStudioGameTests.cpp). */
namespace OKStudioGameInternal
{
	bool BoneWorld(const AKKPreviewActor* Actor, const FString& Bone, FTransform& Out);
}

/** The studio's control channel (OKStudioControl.cpp): listens while a studio runs with -Control. */
namespace OKStudioControl
{
	void Start();
	void Stop();
}
