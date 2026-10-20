#pragma once
#include "CoreMinimal.h"
class UAnimSequence;
#if WITH_DEV_AUTOMATION_TESTS
// Diagnostic sequence with real humanoid rest geometry, pelvis translation,
// XYZ spine/neck/head rotations. Never used as a failed asset fallback.
UAnimSequence* OKMakeExternalBodyFixture(UObject* Outer);
#endif
