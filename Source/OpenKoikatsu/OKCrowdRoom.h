#pragma once
#include "CoreMinimal.h"
class AActor;
// The hundred-girl room (CrowdRoom100.cmd): the StreetSocial runtime characters (OKStartCrowdRuntimeScene, external
// control, the same materials, GPU poses and shadows as the street's heroines) in an empty room up in the sky of the
// StreetSocial map. Each girl wanders on her own: she stands about in one of her standing poses for a while (more often
// than she walks), then walks to a random spot and stands there in another. A free camera (WASD, Q/E, right mouse,
// Shift) and a frame-time report (manifest "room": benchmark_seconds, warmup_seconds, report, exit_after_report).
bool OKStartCrowdRoom(AActor* Owner,const FString& Manifest,FString& Error);
