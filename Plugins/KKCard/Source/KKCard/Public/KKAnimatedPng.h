#pragma once
#include "CoreMinimal.h"

struct FKKPngFrame
{
    TArray<uint8> Png;
    double Seconds = 1.0;
};

// New-card profile: complete canvas frames, SOURCE blending, no disposal.
// The first frame remains a normal PNG image for static readers.
class KKCARD_API FKKAnimatedPng
{
public:
    static bool BuildPreview(const TArray<uint8>& SourcePreview,
        const TArray<TArray<uint8>>& FourViews, TArray<uint8>& Output, FString& Error);
    // Frames of one size (the first view's; all views alike) with whole-second durations (one per view), infinite loop.
    // Private chunks of SourcePreview (lowercase first two letters, e.g. okLb, okSc) are carried over verbatim.
    static bool BuildFrames(const TArray<uint8>& SourcePreview, const TArray<TArray<uint8>>& Views,
        const TArray<uint16>& Seconds, TArray<uint8>& Output, FString& Error);
    // OK card profile: the original card image (fitted into 768x1024) for 3 seconds, then the T-pose front,
    // side and back views for 1 second each. SourcePreview is the card's current image (PNG, or an earlier
    // APNG whose first frame is the original image).
    static bool BuildOkCardPreview(const TArray<uint8>& SourcePreview,
        const TArray<TArray<uint8>>& TPoseViews, TArray<uint8>& Output, FString& Error);
    // The original image of a card preview, fitted (aspect kept) onto a 768x1024 RGBA canvas as a PNG.
    static bool FitCardImage(const TArray<uint8>& SourcePreview, TArray<uint8>& Output, FString& Error);
    static bool Read(const TArray<uint8>& Bytes, TArray<FKKPngFrame>& Frames,
        uint32& Plays, FString& Error);
    static int32 FrameAt(const TArray<double>& Durations, double Elapsed, uint32 Plays = 0);
};
