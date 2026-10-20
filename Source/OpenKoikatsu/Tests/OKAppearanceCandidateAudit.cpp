#include "../OKGalleryWidget.h"
#include "../OKAppearanceCardJob.h"
#include "KKCardReader.h"
#include "KKCardWriter.h"
#include "KKPreviewActor.h"
#include "KKRustExtract.h"
#include "ok_extract.h"
#include "Async/Async.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/Crc.h"
#include "RenderCommandFence.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/UObjectIterator.h"
#include "UnrealClient.h"

#if WITH_DEV_AUTOMATION_TESTS
// Diagnostic for the native -> actual actor endpoint. This deliberately does
// not masquerade as the required real UI callback/saved-card sweep.
namespace OKAppearanceCandidateAudit {
struct FSelection{FString Target,Item;};
struct FRun:TSharedFromThis<FRun>{
 TWeakObjectPtr<UWorld> World;TWeakObjectPtr<UOKGalleryWidget> Gallery;
 FString Card,Output,Error;int32 Stage=0,Index=0;double Start=FPlatformTime::Seconds(),At=0,NextLog=0,WorkerMs=0,CommitMs=0;uint64 ReadyFrame=0;
 TSharedPtr<FJsonObject> PreparationInfo;
 TArray<uint8> LastCandidateCard;
 TSharedPtr<FOkAppearanceContext,ESPMode::ThreadSafe> Context;
 TSharedPtr<FOkAppearanceCandidate,ESPMode::ThreadSafe> Candidate;
 TArray<FSelection> Selections;TArray<TSharedPtr<FJsonValue>> Cases;FRenderCommandFence Fence;bool Done=false,CheckpointVerified=false,BackgroundResumed=false;uint32 WarmResumeCRC=0;
 uint32 TextureCRC(const FOkSession& Session){uint32 CRC=0;for(int32 I=0;I<Session.PartCount();++I){OkMeshView V{};if(Session.PartAt(I,V)&&V.png&&V.png_len>0)CRC=HashCombine(CRC,FCrc::MemCrc32(V.png,V.png_len));}return CRC;}
 void Write(const FString& Failure=FString()){
  auto R=MakeShared<FJsonObject>();R->SetBoolField(TEXT("valid"),false);R->SetBoolField(TEXT("complete"),false);R->SetBoolField(TEXT("diagnostic_subset"),true);
  R->SetBoolField(TEXT("uses_ui_callback"),false);R->SetBoolField(TEXT("checkpoint_verified"),CheckpointVerified);R->SetStringField(TEXT("error"),Failure);
  R->SetBoolField(TEXT("background_clothing_resumed"),BackgroundResumed);
  R->SetBoolField(TEXT("checkpoint_saved_through_card_writer"),CheckpointVerified);
  R->SetNumberField(TEXT("stage"),Stage);R->SetNumberField(TEXT("limit_ms"),300);R->SetArrayField(TEXT("cases"),Cases);
  FString Text;FJsonSerializer::Serialize(R,TJsonWriterFactory<>::Create(&Text));FFileHelper::SaveStringToFile(Text,*Output,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
 }
 bool Stop(const FString& Failure){Write(Failure);FPlatformMisc::RequestExitWithStatus(false,Failure.IsEmpty()?0:1);return false;}
 bool Tick(float){
  auto* W=World.Get();if(!W)return Stop(TEXT("World disappeared"));if(FPlatformTime::Seconds()-Start>1200)return Stop(TEXT("Candidate diagnostic timed out"));
  auto* G=Gallery.Get();if(!G){for(TObjectIterator<UOKGalleryWidget> It;It;++It)if(It->GetWorld()==W&&It->PreviewImage){Gallery=*It;G=*It;break;}if(!G)return true;}
  if(FPlatformTime::Seconds()>NextLog){NextLog=FPlatformTime::Seconds()+10;UE_LOG(LogTemp,Display,TEXT("Appearance candidate diagnostic stage=%d selection=%d/%d"),Stage,Index,Selections.Num());Write();}
  if(Stage==0){
   FViewport* View=GEngine&&GEngine->GameViewport?GEngine->GameViewport->Viewport:nullptr;if(!View)return true;
   if(View->GetSizeXY()!=FIntPoint(1600,1000)){GEngine->Exec(W,TEXT("r.SetRes 1600x1000w"));return true;}
   G->bIndependentCards=true;G->Entries.Reset();FKKCardListEntry Entry;Entry.Path=Card;Entry.FileName=FPaths::GetCleanFilename(Card);G->Entries.Add(Entry);G->SelectIndex(0);G->OnLoad3D();Stage=1;return true;
  }
  if(G->bLoadInFlight||G->bAppearanceBusy||G->bAppearanceThumbBusy||!G->PreviewActor||!G->PreviewActor->PendingPreviewMaterials.IsEmpty()||!G->PreviewActor->PreparePlaybackMaterials(false))return true;
  if(Stage==1){G->OnOpenAppearance();Stage=2;return true;}
  if(Stage==2){
   if(G->AppearanceItems.IsEmpty())return Stop(TEXT("Appearance catalog unavailable"));
   for(auto Target:G->AppearanceTargets){const FString Key=Target->GetStringField(TEXT("key"));const int32 Category=Target->GetIntegerField(TEXT("category"));if(Category<406||Category>414||Category==408||Category==409)continue;
    int32 Count=0;for(auto Item:G->AppearanceItems){if(Item->GetObjectField(TEXT("entry"))->GetIntegerField(TEXT("category_no"))!=Category)continue;Selections.Add({Key,Item->GetStringField(TEXT("key"))});if(++Count==10)break;}
    if(Count!=10)return Stop(TEXT("Candidate target has fewer than ten sources"));
   }
   if(Selections.Num()!=70)return Stop(TEXT("Candidate registry coverage changed; review the diagnostic"));
   // Match the seven-target native/GUI baseline in actual registry order.
   // Iris/gradient composition and the full registry remain separate work.
   Context=MakeShared<FOkAppearanceContext,ESPMode::ThreadSafe>();auto Self=AsShared();const FString Root=G->AppearanceRoot;Stage=3;
   Async(EAsyncExecution::ThreadPool,[Self,Root]{FString Failure;Self->Context->Open(Root,Self->Card,FPaths::ProjectContentDir()/TEXT("IndependentResources/v1"),FPaths::ProjectSavedDir()/TEXT("AppearanceEditor/cache"),0,Failure);AsyncTask(ENamedThreads::GameThread,[Self,Failure]{Self->Error=Failure;Self->Done=true;});});return true;
  }
  if(Stage==3){if(!Done)return true;if(!Error.IsEmpty())return Stop(Error);Done=false;Stage=4;}
  if(Stage==4){
   if(Index==Selections.Num()){
    FString Failure;auto Checkpoint=Context->Capture(Failure);if(!Checkpoint)return Stop(Failure);
    auto Self=AsShared();const FString Path=FPaths::ChangeExtension(Output,TEXT("checkpoint.png"));Stage=8;
    Async(EAsyncExecution::ThreadPool,[Self,Checkpoint,Path]{FString Failure;TSharedPtr<FOkSession,ESPMode::ThreadSafe> Saved;
     FKKCharacterFile Working;
     if(UKKCardReader::LoadFromBytes(Self->LastCandidateCard,Working,Failure)){
      Working.DeferredPreview=MakeShared<FOKAppearanceDeferredPreview,ESPMode::ThreadSafe>(Checkpoint);
      const TArray<uint8> Custom=Working.RawBlocks.FindRef(TEXT("Custom"));
      const TArray<uint8> Extended=Working.RawBlocks.FindRef(TEXT("KKEx"));
      if(UKKCardWriter::SaveWithEdits(Working,Path,Failure)){
       FKKCharacterFile Reloaded;
       if(UKKCardReader::LoadFromFile(Path,Reloaded,Failure)&&Reloaded.RawBlocks.FindRef(TEXT("Custom"))==Custom&&Reloaded.RawBlocks.FindRef(TEXT("KKEx"))==Extended){
        Saved=MakeShared<FOkSession,ESPMode::ThreadSafe>();if(!Saved->Open(FString(),Path,0,Failure))Saved.Reset();
       }else if(Failure.IsEmpty())Failure=TEXT("Deferred save changed candidate metadata");
      }
     }
     AsyncTask(ENamedThreads::GameThread,[Self,Saved,Failure]{Self->Error=Failure;if(auto* Gallery=Self->Gallery.Get())Self->CheckpointVerified=Saved&&Self->TextureCRC(*Saved)==Self->TextureCRC(Gallery->PreviewActor->Session);Self->Done=true;});});return true;
   }
   At=FPlatformTime::Seconds();auto Self=AsShared();const auto Selection=Selections[Index];Stage=5;
   Async(EAsyncExecution::ThreadPool,[Self,Selection]{FString Failure;const double Begin=FPlatformTime::Seconds();auto Prepared=Self->Context->Prepare(Selection.Target,Selection.Item,Failure);const double Ms=(FPlatformTime::Seconds()-Begin)*1000.;AsyncTask(ENamedThreads::GameThread,[Self,Prepared,Failure,Ms]{Self->Candidate=Prepared;Self->WorkerMs=Ms;Self->Error=Failure;Self->Done=true;});});return true;
  }
  if(Stage==5){if(!Done)return true;Done=false;if(!Candidate)return Stop(Error);FString Info;PreparationInfo.Reset();if(Candidate->Info(Info))FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Info),PreparationInfo);if(!Candidate->CardMetadata(LastCandidateCard))return Stop(TEXT("Candidate card metadata unavailable"));const double Begin=FPlatformTime::Seconds();if(!G->PreviewActor->ApplyHeadTextureCandidate(*Context,*Candidate,Error))return Stop(Error);CommitMs=(FPlatformTime::Seconds()-Begin)*1000.;Candidate.Reset();Fence.BeginFence();ReadyFrame=GFrameCounter;Stage=6;return true;}
  if(Stage==6){if(!Fence.IsFenceComplete()||GFrameCounter<ReadyFrame+2)return true;
   const double Ms=(FPlatformTime::Seconds()-At)*1000.;auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("target"),Selections[Index].Target);Row->SetStringField(TEXT("item"),Selections[Index].Item);
   Row->SetNumberField(TEXT("prepare_to_render_ready_ms"),Ms);Row->SetBoolField(TEXT("under_300ms"),Ms<300);Row->SetBoolField(TEXT("actor_applied"),true);Row->SetStringField(TEXT("texture_crc"),FString::Printf(TEXT("%08x"),TextureCRC(G->PreviewActor->Session)));
   Row->SetNumberField(TEXT("worker_prepare_ms"),WorkerMs);Row->SetNumberField(TEXT("actor_commit_ms"),CommitMs);Row->SetNumberField(TEXT("queue_dispatch_render_wait_ms"),Ms-WorkerMs-CommitMs);if(PreparationInfo)Row->SetObjectField(TEXT("native_preparation"),PreparationInfo);
   Row->SetNumberField(TEXT("viewport_width"),GEngine->GameViewport->Viewport->GetSizeXY().X);Row->SetNumberField(TEXT("viewport_height"),GEngine->GameViewport->Viewport->GetSizeXY().Y);Cases.Add(MakeShared<FJsonValueObject>(Row));
   if(Index==0||Index+1==Selections.Num())FScreenshotRequest::RequestScreenshot(FPaths::ChangeExtension(Output,TEXT(""))+FString::Printf(TEXT("_item%03d.png"),Index),true,false);
   ++Index;Stage=4;Write();return true;
  }
  if(Stage==8){if(!Done)return true;if(!Error.IsEmpty())return Stop(Error);if(!CheckpointVerified)return Stop(TEXT("Checkpoint renderer textures differ from live actor"));WarmResumeCRC=TextureCRC(G->PreviewActor->Session);G->OnCloseAppearance();G->WarmClothingPreviews();At=FPlatformTime::Seconds();Stage=9;Write();return true;}
  if(Stage==9){
   if(TextureCRC(G->PreviewActor->Session)!=WarmResumeCRC)return Stop(TEXT("Background clothing preparation changed the active appearance"));
   if(G->ClothingPreviews.Num()>0){BackgroundResumed=true;return Stop(FString());}
   if(FPlatformTime::Seconds()-At>60)return Stop(TEXT("Clothing preparation did not resume after closing the appearance panel"));
  }
  return true;
 }
};
}
static FAutoConsoleCommandWithWorld CandidateAudit(TEXT("ok.AuditAppearanceCandidates"),TEXT("Diagnostic native candidate -> actor endpoint; not UI acceptance"),FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World){
 const FString Card=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_GUI_RELOAD_CARD")),Output=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_GUI_RELOAD_OUTPUT"));if(!World||Card.IsEmpty()||Output.IsEmpty())return;
 auto Run=MakeShared<OKAppearanceCandidateAudit::FRun>();Run->World=World;Run->Card=Card;Run->Output=Output;FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Run](float D){return Run->Tick(D);}));
}));
#endif
