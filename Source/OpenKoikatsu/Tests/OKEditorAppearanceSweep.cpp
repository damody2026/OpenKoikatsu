#include "../OKGalleryWidget.h"
#include "../OKAppearanceButton.h"
#include "../OKAppearanceCardJob.h"
#include "KKPreviewActor.h"
#include "ok_extract.h"
#include "Components/ComboBoxString.h"
#include "Components/EditableTextBox.h"
#include "Containers/Ticker.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "HAL/IConsoleManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/UObjectIterator.h"
#include "ShaderCompiler.h"
#include "RenderCommandFence.h"
#include "Misc/Crc.h"
#include "Misc/SecureHash.h"
#include "HAL/FileManager.h"
#include "UnrealClient.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace OKAppearanceSweep {
struct FRun : TSharedFromThis<FRun> {
 TWeakObjectPtr<UWorld> World; TWeakObjectPtr<UOKGalleryWidget> Gallery;
 FString Card,Output,Key,BeforePath; TArray<TSharedPtr<FJsonObject>> Targets;
 TArray<FString> Keys; TArray<TSharedPtr<FJsonValue>> Cases,Types;
 TSharedPtr<FJsonObject> Row; FRenderCommandFence Fence;
 TSet<FString> OwnedOutputs;
 TArray<TSharedPtr<FOKAppearanceCardJob,ESPMode::ThreadSafe>> CapturedCards;
 TArray<TSharedPtr<FJsonObject>> CapturedRows; int32 VerifyIndex=0;
 int32 Stage=0,Type=0,Item=0,Failures=0,Limit=0,StartType=0; uint64 ReadyFrame=0;
 double At=FPlatformTime::Seconds(),ClickAt=0,TypeAt=0,NextProgress=0;
 bool Verified=false,VerifyDone=false,ResolutionRequested=false; FString VerifyError;
 bool ScreenshotPending=false;double ScreenshotAt=0.;FDelegateHandle ScreenshotHandle;
 TSharedPtr<FJsonObject> ScreenshotRow;FString ScreenshotFailure;
 void ScreenshotProcessed(){
  ScreenshotPending=false;FScreenshotRequest::OnScreenshotRequestProcessed().Remove(ScreenshotHandle);ScreenshotHandle.Reset();
  if(ScreenshotRow){
   const bool Saved=IFileManager::Get().FileSize(*ScreenshotRow->GetStringField(TEXT("screenshot")))>0;
   ScreenshotRow->SetBoolField(TEXT("screenshot_saved"),Saved);
   ScreenshotRow->SetNumberField(TEXT("audit_screenshot_ms"),(FPlatformTime::Seconds()-ScreenshotAt)*1000.);
   if(!Saved)ScreenshotFailure=TEXT("Requested visual evidence was not saved");
   ScreenshotRow.Reset();
  }
 }
 void Write(bool Complete,const FString& Error=FString()) {
  auto R=MakeShared<FJsonObject>();R->SetBoolField(TEXT("valid"),Complete&&Failures==0&&Error.IsEmpty());
  R->SetBoolField(TEXT("complete"),Complete);R->SetStringField(TEXT("error"),Error);
  R->SetBoolField(TEXT("uses_ui_callback"),true);R->SetNumberField(TEXT("required_selections_per_target"),10);
  R->SetNumberField(TEXT("limit_ms"),300);R->SetNumberField(TEXT("failures"),Failures);
  R->SetNumberField(TEXT("stage"),Stage);R->SetNumberField(TEXT("current_target"),Type);R->SetNumberField(TEXT("current_selection"),Item);
  R->SetNumberField(TEXT("target_count"),Targets.Num());R->SetNumberField(TEXT("completed_target_count"),FMath::Max(0,Type-StartType));
  R->SetBoolField(TEXT("diagnostic_subset"),Limit>0||StartType>0);R->SetNumberField(TEXT("start_target_index"),StartType);R->SetArrayField(TEXT("types"),Types);R->SetArrayField(TEXT("cases"),Cases);
  FString Text;FJsonSerializer::Serialize(R,TJsonWriterFactory<>::Create(&Text));
  FFileHelper::SaveStringToFile(Text,*Output,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
 }
 bool Stop(bool Complete,const FString& Error=FString()) {
  if(ScreenshotHandle.IsValid()){FScreenshotRequest::OnScreenshotRequestProcessed().Remove(ScreenshotHandle);ScreenshotHandle.Reset();}
  Write(Complete,Error);FPlatformMisc::RequestExitWithStatus(false,Complete&&Failures==0&&Error.IsEmpty()?0:1);return false;
 }
 bool Busy(UOKGalleryWidget* G) const {
  return G->bLoadInFlight||G->bAppearanceBusy||G->bAppearanceThumbBusy||
   (G->PreviewActor&&(!G->PreviewActor->PendingPreviewMaterials.IsEmpty()||!G->PreviewActor->PreparePlaybackMaterials(false)));
 }
 void PruneCompletedType(UOKGalleryWidget* G){
  // This sweep can produce hundreds of GB. Retain first/last and the first
  // failing selection of every type, plus hashes and verification for every
  // intermediate output. Only files created and verified by this run qualify.
  bool KeptFailure=false;
  for(auto Value:Cases){auto Case=Value->AsObject();if(Case->GetIntegerField(TEXT("target_index"))!=Type)continue;
   const int32 Index=Case->GetIntegerField(TEXT("selection_index"));const bool Failed=!Case->GetBoolField(TEXT("valid"));
   const bool Keep=Index==0||Index==Keys.Num()-1||(Failed&&!KeptFailure);if(Failed)KeptFailure=true;if(Keep)continue;
   const FString Path=Case->GetStringField(TEXT("output"));FGuid Id;
   const FString Folder=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("AppearanceEditor"));
   if(!OwnedOutputs.Contains(Path)||Path==G->Entries[G->SelectedIndex].Path||Path==Card||
      !FPaths::IsSamePath(FPaths::GetPath(FPaths::ConvertRelativePathToFull(Path)),Folder)||
      FPaths::GetBaseFilename(Path).Len()!=32||!FGuid::Parse(FPaths::GetBaseFilename(Path),Id))continue;
   const bool Deleted=IFileManager::Get().Delete(*Path,false,false);
   Case->SetBoolField(TEXT("output_retained"),!Deleted);if(Deleted)OwnedOutputs.Remove(Path);
  }
 }
 bool Tick(float) {
  if(!ScreenshotFailure.IsEmpty())return Stop(false,ScreenshotFailure);
  auto* W=World.Get();if(!W)return Stop(false,TEXT("World disappeared"));
  auto* G=Gallery.Get();if(!G){for(TObjectIterator<UOKGalleryWidget> It;It;++It)if(It->GetWorld()==W&&It->PreviewImage){Gallery=*It;G=*It;break;}if(!G)return true;}
  if(FPlatformTime::Seconds()>=NextProgress){NextProgress=FPlatformTime::Seconds()+10.;UE_LOG(LogTemp,Display,TEXT("Appearance sweep progress stage=%d target=%d item=%d load=%d appearance=%d thumbnails=%d final_materials=%d"),Stage,Type,Item,G->bLoadInFlight,G->bAppearanceBusy,G->bAppearanceThumbBusy,G->PreviewActor?G->PreviewActor->PendingPreviewMaterials.Num():-1);Write(false);}
  if(Stage==0){
   // Offscreen startup can select a fallback viewport despite -ResX/-ResY.
   // Verify actual pixels before collecting visual or timing evidence.
   int32 X=1600,Y=1000;FParse::Value(FCommandLine::Get(),TEXT("ResX="),X);FParse::Value(FCommandLine::Get(),TEXT("ResY="),Y);
   FViewport* V=GEngine&&GEngine->GameViewport?GEngine->GameViewport->Viewport:nullptr;
   if(!V)return true;
   if(V->GetSizeXY()!=FIntPoint(X,Y)){
    if(!ResolutionRequested){ResolutionRequested=true;At=FPlatformTime::Seconds();GEngine->Exec(W,*FString::Printf(TEXT("r.SetRes %dx%dw"),X,Y));}
    if(FPlatformTime::Seconds()-At>10.)return Stop(false,TEXT("Actual viewport did not reach requested audit resolution"));
    return true;
   }
   G->bIndependentCards=true;G->Entries.Reset();FKKCardListEntry E;E.Path=Card;E.FileName=FPaths::GetCleanFilename(Card);G->Entries.Add(E);G->SelectIndex(0);G->OnLoad3D();Stage=1;At=FPlatformTime::Seconds();Write(false);return true;
  }
  if(FPlatformTime::Seconds()-At>600)return Stop(false,TEXT("Operation timed out; remaining targets were not tested"));
  if(Stage==1){if(Busy(G))return true;G->OnOpenAppearance();Stage=2;At=FPlatformTime::Seconds();return true;}
  if(Stage==2){if(Busy(G))return true;Targets=G->AppearanceTargets;if(Targets.IsEmpty())return Stop(false,TEXT("No appearance targets"));Limit=FCString::Atoi(*FPlatformMisc::GetEnvironmentVariable(TEXT("OK_APPEARANCE_SWEEP_LIMIT")));StartType=FMath::Max(0,FCString::Atoi(*FPlatformMisc::GetEnvironmentVariable(TEXT("OK_APPEARANCE_SWEEP_START"))));Type=StartType;Write(false);Stage=3;}
  if(Stage==3){
   if(Type>=Targets.Num())return Stop(StartType==0&&Limit==0,StartType==0&&Limit==0?FString():TEXT("Diagnostic subset is not full acceptance"));
   if(Limit>0&&Cases.Num()>=Limit)return Stop(false,TEXT("Diagnostic subset finished; full acceptance remains required"));
   if(Busy(G))return true;
   G->AppearanceSearch->SetText(FText::GetEmpty());G->OnAppearanceSearch(FText::GetEmpty());
   TypeAt=FPlatformTime::Seconds();G->AppearanceTarget->SetSelectedOption(Targets[Type]->GetStringField(TEXT("label")));
   At=TypeAt;Stage=4;return true;
  }
  if(Stage==4){
   if(Busy(G))return true;Keys.Reset();
   for(int32 I=0;I<FMath::Min(10,G->AppearanceFiltered.Num());++I)Keys.Add(G->AppearanceItems[G->AppearanceFiltered[I]]->GetStringField(TEXT("key")));
   auto T=MakeShared<FJsonObject>();T->SetStringField(TEXT("target"),Targets[Type]->GetStringField(TEXT("key")));
   T->SetStringField(TEXT("label"),Targets[Type]->GetStringField(TEXT("label")));T->SetNumberField(TEXT("available"),G->AppearanceFiltered.Num());
   T->SetNumberField(TEXT("expected_selections"),Keys.Num());T->SetNumberField(TEXT("category_ready_ms"),(FPlatformTime::Seconds()-TypeAt)*1000.);
   Types.Add(MakeShared<FJsonValueObject>(T));Item=0;VerifyIndex=0;CapturedCards.Reset();CapturedRows.Reset();Stage=5;
   if(Keys.Num()!=10)return Stop(false,FString::Printf(TEXT("Target %s has only %d selectable sources; ten-per-type coverage is incomplete"),*Targets[Type]->GetStringField(TEXT("key")),Keys.Num()));
  }
  if(Stage==5){
   // Screenshot readback/PNG encoding is audit work. Complete it before
   // another selection, both to pin the correct image and to avoid including
   // a previous audit capture's I/O in the next production click measurement.
   // ClickAt still starts at the actual callback and is never reset mid-edit.
   if(ScreenshotPending)return true;
   if(Item>=Keys.Num()){Stage=9;At=FPlatformTime::Seconds();return true;}
   if(Limit>0&&Cases.Num()>=Limit)return Stop(false,TEXT("Diagnostic subset finished; full acceptance remains required"));
   if(Busy(G))return true;
   Key=Keys[Item];UOKAppearanceButton* Button=nullptr;for(UOKAppearanceButton* B:G->AppearanceButtons)if(B->ItemKey==Key){Button=B;break;}
   if(!Button)return Stop(false,TEXT("Expected image button is absent"));
   BeforePath=G->Entries[G->SelectedIndex].Path;Row=MakeShared<FJsonObject>();
   Row->SetStringField(TEXT("before_path"),BeforePath);
   Row->SetStringField(TEXT("target"),Targets[Type]->GetStringField(TEXT("key")));Row->SetStringField(TEXT("item"),Key);
   Row->SetNumberField(TEXT("target_index"),Type);Row->SetNumberField(TEXT("selection_index"),Item);
   ClickAt=FPlatformTime::Seconds();At=ClickAt;Button->OnClicked.Broadcast();Stage=6;return true;
  }
  if(Stage==6){if(Busy(G))return true;Fence.BeginFence();ReadyFrame=GFrameCounter;Stage=7;return true;}
  if(Stage==7){
   if(!Fence.IsFenceComplete()||GFrameCounter<ReadyFrame+2)return true;
   const double Ms=(FPlatformTime::Seconds()-ClickAt)*1000.;
   const bool Applied=G->AppearanceSelectedKeys.FindRef(Targets[Type]->GetStringField(TEXT("key")))==Key&&
    (G->Entries[G->SelectedIndex].Path!=BeforePath||G->Selected.DeferredPreview.IsValid())&&IsValid(G->PreviewActor.Get())&&G->PreviewActor->Session.IsOpen()&&G->PreviewActor->IsSessionLoadComplete();
   Row->SetNumberField(TEXT("click_to_render_ready_ms"),Ms);Row->SetBoolField(TEXT("under_300ms"),Ms<300.);
   Row->SetBoolField(TEXT("preview_and_selection_applied"),Applied);Row->SetStringField(TEXT("output"),G->Entries[G->SelectedIndex].Path);
   if(GEngine&&GEngine->GameViewport&&GEngine->GameViewport->Viewport){const FIntPoint Size=GEngine->GameViewport->Viewport->GetSizeXY();Row->SetNumberField(TEXT("viewport_width"),Size.X);Row->SetNumberField(TEXT("viewport_height"),Size.Y);}
   if(G->AppearanceLastTiming)Row->SetObjectField(TEXT("pipeline_timings"),G->AppearanceLastTiming);
   if(Item==0||Item==Keys.Num()-1){const FString Shot=FPaths::ChangeExtension(Output,TEXT(""))+FString::Printf(TEXT("_type%03d_item%02d.png"),Type,Item);
    ScreenshotPending=true;ScreenshotAt=FPlatformTime::Seconds();ScreenshotRow=Row;Row->SetStringField(TEXT("screenshot"),Shot);
    ScreenshotHandle=FScreenshotRequest::OnScreenshotRequestProcessed().AddSP(AsShared(),&FRun::ScreenshotProcessed);
    FScreenshotRequest::RequestScreenshot(Shot,true,false);}
   uint32 TextureHash=0;int64 Vertices=0;if(auto* A=G->PreviewActor.Get())for(int32 I=0;I<A->Session.PartCount();++I){OkMeshView V{};if(A->Session.PartAt(I,V)){Vertices+=V.n_verts;if(V.png&&V.png_len)TextureHash=HashCombine(TextureHash,FCrc::MemCrc32(V.png,V.png_len));}}
   Row->SetStringField(TEXT("display_texture_crc"),FString::Printf(TEXT("%08x"),TextureHash));Row->SetNumberField(TEXT("display_vertices"),double(Vertices));
   Cases.Add(MakeShared<FJsonValueObject>(Row));Row->SetBoolField(TEXT("valid"),false);
   if(!Applied){Row->SetBoolField(TEXT("saved_field_and_source_verified"),false);++Failures;return Stop(false,TEXT("Preview/selection apply failed; remaining targets were not tested"));}
   // Pin all ten immutable revisions, then save/verify them after measuring
   // the actual consecutive-click sequence. Interleaving a 100MB APNG save
   // after each click creates unrelated IO pressure absent from normal edits.
   CapturedRows.Add(Row);
   TSharedPtr<FOKAppearanceCardJob,ESPMode::ThreadSafe> Captured;
   if(G->Selected.DeferredPreview)Captured=MakeShared<FOKAppearanceCardJob,ESPMode::ThreadSafe>(G->Selected);
   CapturedCards.Add(MoveTemp(Captured));
   Write(false);++Item;Stage=5;At=FPlatformTime::Seconds();return true;
  }
  if(Stage==9){
   if(ScreenshotPending)return true;
   if(Busy(G))return true;
   if(VerifyIndex>=CapturedRows.Num()){PruneCompletedType(G);CapturedCards.Reset();CapturedRows.Reset();++Type;Stage=3;Write(false);At=FPlatformTime::Seconds();return true;}
   Row=CapturedRows[VerifyIndex];Key=Row->GetStringField(TEXT("item"));
   Verified=false;VerifyDone=false;VerifyError.Reset();Stage=8;auto Self=AsShared();
   const double SaveAt=FPlatformTime::Seconds();
   if(VerifyIndex<CapturedRows.Num()-1){
    auto Job=CapturedCards[VerifyIndex];
    const FString Path=Job?FPaths::ProjectSavedDir()/TEXT("AppearanceEditor")/(FGuid::NewGuid().ToString(EGuidFormats::Digits)+TEXT(".png")):Row->GetStringField(TEXT("output"));
    Row->SetStringField(TEXT("output"),Path);Row->SetBoolField(TEXT("production_save_barrier"),false);Row->SetBoolField(TEXT("audit_snapshot_output"),Job.IsValid());
    auto Request=MakeShared<FJsonObject>();Request->SetStringField(TEXT("action"),TEXT("verify"));Request->SetStringField(TEXT("card"),Path);
    Request->SetStringField(TEXT("target"),Row->GetStringField(TEXT("target")));Request->SetStringField(TEXT("item"),Key);Request->SetNumberField(TEXT("outfit"),G->OutfitIndex);
    G->bAppearanceBusy=true;
    G->RunAppearanceRequest(Request,[Self,SaveAt](UOKCharacterController* Widget,TSharedPtr<FJsonObject> Data,const FString& Error){
     Widget->bAppearanceBusy=false;Self->Row->SetNumberField(TEXT("checkpoint_save_and_verify_ms"),(FPlatformTime::Seconds()-SaveAt)*1000.);
     Self->Verified=Data.IsValid()&&Data->GetBoolField(TEXT("valid"));Self->VerifyDone=true;Self->VerifyError=Error;
    },Job);return true;
   }
   Row->SetBoolField(TEXT("production_save_barrier"),true);
   auto Verify=[Self,SaveAt](UOKCharacterController* Widget){
    Self->Row->SetNumberField(TEXT("save_barrier_ms"),(FPlatformTime::Seconds()-SaveAt)*1000.);
    Self->Row->SetStringField(TEXT("output"),Widget->Entries[Widget->SelectedIndex].Path);
    auto Request=MakeShared<FJsonObject>();Request->SetStringField(TEXT("action"),TEXT("verify"));Request->SetStringField(TEXT("card"),Widget->Entries[Widget->SelectedIndex].Path);
    Request->SetStringField(TEXT("target"),Self->Targets[Self->Type]->GetStringField(TEXT("key")));Request->SetStringField(TEXT("item"),Self->Key);Request->SetNumberField(TEXT("outfit"),Widget->OutfitIndex);
    Widget->RunAppearanceRequest(Request,[Self](UOKCharacterController*,TSharedPtr<FJsonObject> Data,const FString& Error){Self->Verified=Data.IsValid()&&Data->GetBoolField(TEXT("valid"));Self->VerifyDone=true;Self->VerifyError=Error;});
   };
   // Measure actual click-to-render first, then exercise the same production
   // save barrier used by save/reload/outfit actions and verify the saved card.
   if(!G->MaterializeAppearance(Verify))Verify(G);return true;
  }
  if(Stage==8){
   if(!VerifyDone&&!G->bAppearanceBusy&&G->Selected.DeferredPreview&&Row->GetBoolField(TEXT("production_save_barrier"))){VerifyDone=true;Verified=false;VerifyError=TEXT("Production save barrier failed; pending revision retained");}
   if(!VerifyDone)return true;Row->SetBoolField(TEXT("saved_field_and_source_verified"),Verified);Row->SetStringField(TEXT("error"),VerifyError);
   const bool Valid=Verified&&Row->GetBoolField(TEXT("preview_and_selection_applied"))&&Row->GetBoolField(TEXT("under_300ms"));Row->SetBoolField(TEXT("valid"),Valid);if(!Valid)++Failures;
   const FString SavedPath=Row->GetStringField(TEXT("output"));
   if(Verified&&SavedPath!=Row->GetStringField(TEXT("before_path"))&&SavedPath!=Card){
    OwnedOutputs.Add(SavedPath);Row->SetStringField(TEXT("output_md5"),LexToString(FMD5Hash::HashFile(*SavedPath)));
    Row->SetNumberField(TEXT("output_bytes"),double(IFileManager::Get().FileSize(*SavedPath)));Row->SetBoolField(TEXT("output_retained"),true);
   }
   UE_LOG(LogTemp,Display,TEXT("Appearance sweep type=%d item=%d applied=%d saved=%d ms=%.3f valid=%d"),Type,Row->GetIntegerField(TEXT("selection_index")),Row->GetBoolField(TEXT("preview_and_selection_applied")),Verified,Row->GetNumberField(TEXT("click_to_render_ready_ms")),Valid);
   Write(false);if(!Verified)return Stop(false,TEXT("Pinned revision save/source verification failed; remaining targets were not tested"));
   ++VerifyIndex;Stage=9;At=FPlatformTime::Seconds();return true;
  }
  return true;
 }
};
}
static FAutoConsoleCommandWithWorld AppearanceSweep(TEXT("ok.AuditEditorAppearanceSweep"),TEXT("Select ten different images per target in UI order, verifying saved sources and <300ms render readiness"),FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* W){
 const FString Card=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_GUI_RELOAD_CARD")),Output=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_GUI_RELOAD_OUTPUT"));if(!W||Card.IsEmpty()||Output.IsEmpty())return;
 auto R=MakeShared<OKAppearanceSweep::FRun>();R->World=W;R->Card=Card;R->Output=Output;FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([R](float D){return R->Tick(D);}));
}));
#endif
