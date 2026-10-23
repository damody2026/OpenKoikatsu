#pragma once

#include "CoreMinimal.h"
#include "HAL/PlatformMisc.h"

// Depth (cm) a receiver may lie behind the toon shadow map's nearest surface and still count as lit.
// OK_TOON_SHADOW_BIAS overrides it (diagnostic).
inline float OKToonShadowBias()
{
	static const float Bias = []
	{
		const FString Value = FPlatformMisc::GetEnvironmentVariable(TEXT("OK_TOON_SHADOW_BIAS"));
		return Value.IsEmpty() ? 1.5f : FCString::Atof(*Value);
	}();
	return Bias;
}
