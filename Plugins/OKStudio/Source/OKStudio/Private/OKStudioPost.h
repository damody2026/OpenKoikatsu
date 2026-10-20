#pragma once

#include "CoreMinimal.h"

struct FOKScenePost;
struct FPostProcessSettings;

/**
 * Studio's fog, ambient occlusion and sun shafts as one post-process material (OKStudioPost.cpp), added to `Settings`'
 * blendables when any of them is on (and taken out again when none is). Editor builds only: the material graph is made
 * at run time.
 */
void OKStudioApplyScenePostMaterial(const FOKScenePost& Post, FPostProcessSettings& Settings);

/** The post material's state: "ready", "not made", "compiling", "no shader map" or its compile errors. */
FString OKStudioScenePostMaterialState();
