#pragma once

#include "CoreMinimal.h"

// The process environment the street's crowd extracts its characters under, kept across the one-on-one scene (whose
// high models switch it to the live renderer's settings): saved before the scene opens, put back when it ends.
void OKStreetPrivateEnvSave();
void OKStreetPrivateEnvRestore();
