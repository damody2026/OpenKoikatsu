#pragma once

#include "CoreMinimal.h"

struct FOKScenePost;
struct FPostProcessSettings;

/** Installed Studio HDR BloomAndFlares pipeline, before DOF. Returns true once all passes are ready and installed. */
bool OKStudioApplySourceBloom(const FOKScenePost& Post, FPostProcessSettings& Settings);
FString OKStudioSourceBloomState();
