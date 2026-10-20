#pragma once

#include "CoreMinimal.h"
#include "KKCardTypes.h"
class FOkSession;

// Value-only transaction. The worker owns Card until completion is delivered
// to the game thread; no UObject, widget or live preview is accessed here.
struct FOKAppearanceCardJob
{
    FKKCharacterFile Card;
    TSharedPtr<FOkSession,ESPMode::ThreadSafe> AppearanceSession;
    double SnapshotMs = 0, QueueMs = 0, SaveMs = 0, RequestMs = 0, ReadMs = 0, PreviewPrepareMs=0;
    FOKAppearanceCardJob() = default;
    explicit FOKAppearanceCardJob(const FKKCharacterFile& Source);
    bool Run(const FString& Input, const FString& Output,
        TFunctionRef<bool(FString&)> Request, FString& Error);
};

class FOkAppearanceCheckpoint;
// One immutable resource revision shared by all copies of the working card.
class FOKAppearanceDeferredPreview final : public IKKDeferredCardPreview
{
public:
    explicit FOKAppearanceDeferredPreview(TSharedPtr<FOkAppearanceCheckpoint,ESPMode::ThreadSafe> InCheckpoint);
    virtual bool BuildPreviewPng(TArray<uint8>& OutPreview,FString& OutError) const override;
private:
    TSharedPtr<FOkAppearanceCheckpoint,ESPMode::ThreadSafe> Checkpoint;
    mutable FCriticalSection Mutex;
    mutable TArray<uint8> CachedPreview;
    mutable bool bReady=false;
};
