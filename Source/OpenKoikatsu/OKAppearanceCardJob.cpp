#include "OKAppearanceCardJob.h"
#include "KKCardReader.h"
#include "KKCardWriter.h"
#include "KKRustExtract.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"

FOKAppearanceCardJob::FOKAppearanceCardJob(const FKKCharacterFile& Source)
{
    const double Start=FPlatformTime::Seconds();
    Card=Source;
    SnapshotMs=(FPlatformTime::Seconds()-Start)*1000.;
}

bool FOKAppearanceCardJob::Run(const FString& Input, const FString& Output,
    TFunctionRef<bool(FString&)> Request, FString& Error)
{
    check(!IsInGameThread());
    Error.Reset();
    double Start = FPlatformTime::Seconds();
    const bool Saved = UKKCardWriter::SaveWithEdits(Card, Input, Error);
    SaveMs = (FPlatformTime::Seconds() - Start) * 1000.;
    if (!Saved) { Error = TEXT("保存編輯快照失敗：") + Error; return false; }
    Start = FPlatformTime::Seconds();
    const bool Requested = Request(Error);
    RequestMs = (FPlatformTime::Seconds() - Start) * 1000.;
    if (!Requested) return false;
    if (Output.IsEmpty()) return true; // A property catalog only needs input.
    Start = FPlatformTime::Seconds();
    const bool Loaded = UKKCardReader::LoadFromFile(Output, Card, Error);
    ReadMs = (FPlatformTime::Seconds() - Start) * 1000.;
    if (!Loaded) { Error = TEXT("讀取新卡片失敗：") + Error; return false; }
    return true;
}

FOKAppearanceDeferredPreview::FOKAppearanceDeferredPreview(TSharedPtr<FOkAppearanceCheckpoint,ESPMode::ThreadSafe> InCheckpoint)
    :Checkpoint(MoveTemp(InCheckpoint)) {}

bool FOKAppearanceDeferredPreview::BuildPreviewPng(TArray<uint8>& OutPreview,FString& OutError) const
{
    FScopeLock Lock(&Mutex);
    OutError.Reset();
    if(!bReady)
    {
        if(!Checkpoint){OutError=TEXT("Missing pinned appearance checkpoint");return false;}
        const FString Folder=FPaths::ProjectSavedDir()/TEXT("AppearanceEditor/Checkpoints");
        if(!IFileManager::Get().MakeDirectory(*Folder,true)){OutError=TEXT("Cannot create appearance checkpoint directory");return false;}
        const FString Path=Folder/(FGuid::NewGuid().ToString(EGuidFormats::Digits)+TEXT(".png"));
        if(!Checkpoint->Export(Path,OutError)){IFileManager::Get().Delete(*Path,false,true);return false;}
        FKKCharacterFile Materialized;
        const bool Loaded=UKKCardReader::LoadFromFile(Path,Materialized,OutError);
        // This exact temporary file belongs to this call; it is never a source card.
        IFileManager::Get().Delete(*Path,false,true);
        if(!Loaded)return false;
        CachedPreview=MoveTemp(Materialized.Header.PreviewPng);
        if(CachedPreview.IsEmpty()){OutError=TEXT("Checkpoint contains no portable preview");return false;}
        bReady=true;
    }
    OutPreview=CachedPreview;
    return true;
}
