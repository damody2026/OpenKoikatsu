#pragma once
#include "CoreMinimal.h"
class AKKPreviewActor;

// Fixed-size render targets keep card previews independent of window resolution.
namespace OKCardPreviewCapture
{
    using FComplete = TFunction<void(TArray<TArray<uint8>>, FString)>;
    // Four views: face close-up, front, side, back (in the actor's current pose).
    void Begin(AKKPreviewActor* Actor, FComplete Complete);
    // OK card views: front, side and back in T-pose (the actor is put in T-pose; the caller restores its animation).
    void BeginTPoseViews(AKKPreviewActor* Actor, FComplete Complete);
}
