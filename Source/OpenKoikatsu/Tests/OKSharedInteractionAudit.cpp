#include "../OKGalleryWidget.h"
#include "../OKInteractionMode.h"
#include "../OKInteractionScene.h"
#include "../OKCharacterController.h"
#include "KKPreviewActor.h"
#include "KKPairedPlayback.h"
#include "KKCardWorkspace.h"
#include "Camera/CameraActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "UObject/UObjectIterator.h"
#include "TimerManager.h"
#include "Components/ComboBoxString.h"
#include "HAL/IConsoleManager.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#if WITH_EDITOR
namespace OKSharedInteractionAudit {
struct FRun:TSharedFromThis<FRun>{
 TWeakObjectPtr<UWorld> World;TWeakObjectPtr<UOKGalleryWidget> Source;
 TWeakObjectPtr<AOKInteractionMode> Mode;TWeakObjectPtr<AKKPreviewActor> Original;
 FTimerHandle Timer;FString Card,Male,Output;int32 Phase=0,Round=0;double Started=FPlatformTime::Seconds();
 bool SoloChecked=false;FString SoloKey;bool ControlsChecked=false;double PlaybackCheckedAt=0;bool SwitchRequested=false;FString BeforeClip,BeforeState;
 TArray<float> Body,Face;FString Nickname,Geometry;FTransform Camera,Transform;bool TickEnabled=false;ESlateVisibility Visibility=ESlateVisibility::Visible;TArray<TSharedPtr<FJsonValue>> Cases;
 void Stop(const FString& Error){
  auto Keep=AsShared();if(auto* W=World.Get())W->GetTimerManager().ClearTimer(Timer);
  if(auto* M=Mode.Get())M->Exit(true);
  auto J=MakeShared<FJsonObject>();J->SetBoolField(TEXT("passed"),Error.IsEmpty());J->SetStringField(TEXT("error"),Error);J->SetNumberField(TEXT("process_id"),FPlatformProcess::GetCurrentProcessId());J->SetArrayField(TEXT("cases"),Cases);
  FString Json;FJsonSerializer::Serialize(J,TJsonWriterFactory<>::Create(&Json));FFileHelper::SaveStringToFile(Json,*Output,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
  UE_LOG(LogTemp,Display,TEXT("Shared reader audit passed=%d phase=%d error=%s"),Error.IsEmpty(),Phase,*Error);FPlatformMisc::RequestExitWithStatus(false,Error.IsEmpty()?0:1);
 }
 void Add(const TCHAR* Name){auto J=MakeShared<FJsonObject>();J->SetStringField(TEXT("case"),Name);J->SetBoolField(TEXT("passed"),true);Cases.Add(MakeShared<FJsonValueObject>(J));}
 bool Preserved(){auto* S=Source.Get();auto* A=Original.Get();return S&&A&&S->PreviewActor==A&&S->Selected.Body.ShapeValueBody==Body&&S->Selected.Face.ShapeValueFace==Face&&S->Selected.Nickname==Nickname&&A->Session.ShapeBody()==Body&&A->Session.ShapeFace()==Face&&A->Session.SkinSourceHashV2(0)==Geometry&&A->GetActorTransform().Equals(Transform)&&A->StudioCam&&A->StudioCam->GetActorTransform().Equals(Camera)&&A->IsActorTickEnabled()==TickEnabled&&!A->IsHidden()&&S->GetVisibility()==Visibility;}
 void Tick(){
  auto Keep=AsShared();auto* W=World.Get();if(!W)return;if(FPlatformTime::Seconds()-Started>900){Stop(TEXT("Reader mode audit timed out"));return;}
  if(Phase==0){
   for(TObjectIterator<UOKGalleryWidget> It;It;++It)if(It->GetWorld()==W){Source=*It;break;}
   auto* S=Source.Get();if(!S)return;S->bIndependentCards=!UKKCardWorkspace::PortableSourcePath(Card).IsEmpty();S->Entries.Reset();auto& Entry=S->Entries.AddDefaulted_GetRef();Entry.Path=Card;Entry.FileName=FPaths::GetCleanFilename(Card);S->SelectIndex(0);S->OnLoad3D();Phase=1;return;
  }
  auto* S=Source.Get();if(!S){Stop(TEXT("Source editor destroyed"));return;}
  if(Phase==1){
   if(S->bLoadInFlight||!IsValid(S->PreviewActor)||S->PreviewActor->bShapeDirty)return;
   auto* A=S->PreviewActor.Get();if(!A->Session.IsOpen()||!A->PreparePlaybackMaterials(false))return;
   S->Selected.Body.ShapeValueBody[0]=FMath::Clamp(S->Selected.Body.ShapeValueBody[0]+.07f,0.f,1.f);
   S->Selected.Nickname=TEXT("未儲存的模式驗收修改");A->ApplyShapes(S->Selected.Face.ShapeValueFace,S->Selected.Body.ShapeValueBody);A->OrbitZoom(.5f);Phase=2;return;
  }
  if(Phase==2){
   auto* A=S->PreviewActor.Get();if(A->bShapeDirty)return;
   Original=A;Body=S->Selected.Body.ShapeValueBody;Face=S->Selected.Face.ShapeValueFace;Nickname=S->Selected.Nickname;Geometry=A->Session.SkinSourceHashV2(0);Camera=A->StudioCam->GetActorTransform();Transform=A->GetActorTransform();TickEnabled=A->IsActorTickEnabled();Visibility=S->GetVisibility();
   S->SelectedPartnerMale=S->PartnerMalePaths.Add(Male);Phase=3;return;
  }
  if(Phase==3){
   auto* M=AOKInteractionMode::EnterReader(S);if(!M){Stop(TEXT("Reader could not enter shared mode"));return;}Mode=M;M->AutoReturnSeconds=0;ControlsChecked=false;PlaybackCheckedAt=0;SwitchRequested=false;
   const FString Folder=FPaths::GetPath(Output)/FString::Printf(TEXT("round-%d"),Round);IFileManager::Get().MakeDirectory(*Folder,true);M->ReportPath=Folder/TEXT("result.json");Phase=4;return;
  }
  if(Phase==4){
   auto* M=Mode.Get();if(M&&!M->IsFinished()){
    if(M->IsReady()){
     auto* C=M->Controller.Get();if(!C||C==S||C->PreviewActor==Original.Get()||!C->PreviewActor->Session.IsPortable()||C->PreviewActor->Session.ShapeBody()!=Body||C->Selected.Nickname!=Nickname){Stop(TEXT("Stage clone lost independent ownership or unsaved appearance"));return;}
     if(!SwitchRequested&&Round>0){
      // Metadata is available before preparation; Next must prepare its selected pose.
      if(Round==1&&C->PairCombo->GetOptionCount()<3)return;
      BeforeClip=C->PairedPlayback->GetFemaleClip();BeforeState=C->PairedPlayback->GetState();SwitchRequested=true;
      if(Round==1)C->OnPairPoseNext();
      else {
       int32 Next=1;while(Next<C->PairStateCombo->GetOptionCount()&&C->PairStateCombo->GetOptionAtIndex(Next)==BeforeState)++Next;
       if(Next>=C->PairStateCombo->GetOptionCount()){Stop(TEXT("Pair has no alternate prepared state"));return;}
       C->PairStateCombo->SetSelectedIndex(Next);
      }
      return;
     }
     if(Round>0&&(C->bPairPreparing||!C->PairedPlayback||(C->PairedPlayback->GetFemaleClip()==BeforeClip&&C->PairedPlayback->GetState()==BeforeState)))return;
     if(!ControlsChecked){
      auto* P=C->PairedPlayback.Get();FString Error;const float Prior=P->GetElapsed();P->SetPaused(true);
      if(!P->Advance(.1f,Error)||P->GetElapsed()!=Prior){Stop(TEXT("Paused pair advanced"));return;}
      P->SetPaused(false);
      if(!P->Restart(Error)||!P->SetSpeed(2.f)||!P->Advance(.1f,Error)||!FMath::IsNearlyEqual(P->GetElapsed(),.2f,.0001f)||!FMath::IsNearlyEqual(P->FemaleActor->AnimPhase,.2f,.0001f)||!FMath::IsNearlyEqual(P->MaleActor->AnimPhase,.2f,.0001f)){Stop(TEXT("Shared pair replay/speed/synchronization failed: ")+Error);return;}
      if(P->SetSpeed(-1.f)||P->Advance(-1.f,Error)||!P->SetSpeed(1.f)){Stop(TEXT("Invalid playback input altered pair"));return;}
      ControlsChecked=true;PlaybackCheckedAt=FPlatformTime::Seconds();Add(TEXT("pair_pause_replay_speed_shared_clock"));
     }
     if(ControlsChecked&&FPlatformTime::Seconds()-PlaybackCheckedAt>3.)M->Exit();
    }return;
   }
   if(!Preserved()){Stop(TEXT("Reader selection, unsaved edits, camera or source actor changed"));return;}
   const FString Path=FPaths::GetPath(Output)/FString::Printf(TEXT("round-%d/result.json"),Round);FString Text;TSharedPtr<FJsonObject> Result;
   if(!FFileHelper::LoadFileToString(Text,*Path)||!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Result)||!Result||!Result->GetBoolField(TEXT("same_process"))||Result->GetStringField(TEXT("state"))!=TEXT("returned")||!Result->GetBoolField(TEXT("partner_ready"))||Result->GetNumberField(TEXT("playback_end_seconds"))<=Result->GetNumberField(TEXT("playback_start_seconds"))){Stop(TEXT("Reader did not play and return in the same process"));return;}
   Add(TEXT("reader_high_pair_roundtrip_preserves_unsaved_source"));if(++Round<3){Phase=3;return;}Phase=5;
  }
  if(Phase==7){
   auto* M=Mode.Get();if(!M||M->IsFinished()){Stop(TEXT("Solo audit mode ended"));return;}if(!M->IsReady())return;
   auto* C=M->Controller.Get();UOKInteractionWidget* UI=nullptr;for(TObjectIterator<UOKInteractionWidget> It;It;++It)if(It->GetWorld()==W&&It->Gallery==C){UI=*It;break;}
   if(!UI||!UI->ReadyMotionCombo||UI->ReadyMotionKeys.IsEmpty())return;
   if(!C->GameSourceCombo||C->GameSourceCombo->GetOptionCount()<2){Stop(TEXT("Solo sources missing"));return;}
   SoloKey=UI->ReadyMotionKeys[0];UI->ReadyMotionCombo->SetSelectedIndex(1);Phase=8;return;
  }
  if(Phase==8){
   auto* M=Mode.Get();auto* C=M?M->Controller.Get():nullptr;if(!C){Stop(TEXT("Solo controller lost"));return;}
   if(C->SelectedGameClip!=SoloKey)return;
   if(!C->PreviewActor->IsActorTickEnabled()||C->PreviewActor->PoseMode!=2){Stop(TEXT("Solo selected without active playback"));return;}
   Add(TEXT("solo_catalogue_demand_playback"));SoloChecked=true;M->Exit();Phase=9;return;
  }
  if(Phase==9){if(!Preserved()){Stop(TEXT("Solo mode changed source editor"));return;}Phase=5;return;}
  if(Phase==5){
   if(!SoloChecked){auto* M=AOKInteractionMode::EnterReader(S);if(!M){Stop(TEXT("Solo audit entry failed"));return;}Mode=M;M->AutoReturnSeconds=0;Phase=7;return;}
   auto* M=AOKInteractionMode::EnterCards(W->GetFirstPlayerController(),Card,Male);if(!M){Stop(TEXT("Cancellation entry failed"));return;}M->Exit(true);Phase=6;return;
  }
  if(Phase==6){
   for(TActorIterator<AOKInteractionMode> It(W);It;++It)if(!It->IsFinished()){Stop(TEXT("Cancelled load published a live mode"));return;}
   if(!Preserved()){Stop(TEXT("Cancelled load changed source editor"));return;}Add(TEXT("cancel_during_loading_restores_source"));
   if(AOKInteractionMode::EnterCards(W->GetFirstPlayerController(),Card+TEXT(".missing"),Male)){Stop(TEXT("Missing card admitted"));return;}Add(TEXT("missing_resource_preserves_source"));Stop(FString());
  }
 }
};
}
static FAutoConsoleCommandWithWorld SharedInteractionAudit(TEXT("ok.AuditSharedInteraction"),TEXT("In-process reader mode playback, restoration and cancellation"),FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* W){
 auto Run=MakeShared<OKSharedInteractionAudit::FRun>();Run->World=W;Run->Card=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_SHARED_INTERACTION_CARD"));Run->Male=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_SHARED_INTERACTION_MALE"));Run->Output=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_SHARED_INTERACTION_REPORT"));
 if(!W||Run->Card.IsEmpty()||Run->Male.IsEmpty()||Run->Output.IsEmpty()){FPlatformMisc::RequestExitWithStatus(false,1);return;}
 W->GetTimerManager().SetTimer(Run->Timer,FTimerDelegate::CreateLambda([Run](){Run->Tick();}),.1f,true);
}));
#endif
