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

/** A pass's post material state (0: fog and occlusion, 1: sun shafts): "ready", "not made", "compiling", "no shader
 *  map" or its compile errors. */
FString OKStudioScenePostMaterialState(int32 Pass = 0);

/** Original 32-cube Amplify grading after DOF, before UE device encoding. */
bool OKStudioApplySourceGrade(const FOKScenePost& Post, FPostProcessSettings& Settings);
// The colour (sRGB 0..1) the source grade turns into Shown at a screen position (VignetteDistance2: squared distance
// from the centre, 2 at a corner): a picture's colour back to what the scene drew.
FLinearColor OKStudioUngradeSourceColor(const FOKScenePost& Post, const FLinearColor& Shown, float VignetteDistance2);
// The CPU grade against a native probe grade dump (<prefix>.grade-linear-*.rgba32f): an error summary line.
FString OKStudioCheckSourceGrade(const FOKScenePost& Post, const FString& Prefix);
class UTexture* OKStudioIdentityGradeLut();
