#include "../OKGalleryWidget.h"
#include "KKPreviewActor.h"
#include "Components/ComboBoxString.h"
#include "HAL/IConsoleManager.h"
#include "HAL/FileManager.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/UObjectIterator.h"
#if WITH_DEV_AUTOMATION_TESTS
namespace OKDemoPreparationAudit {
struct FRun:TSharedFromThis<FRun> {
 TWeakObjectPtr<UWorld> World;TWeakObjectPtr<UOKGalleryWidget> Gallery;FTimerHandle Timer;
 FString Card,Output,Target,Other,Expected;int32 Stage=0;uint64 Samples=0;
 double Started=FPlatformTime::Seconds(),Requested=0.;TArray<TSharedPtr<FJsonValue>> Cases;
 void Stop(const FString& Error){auto Keep=AsShared();if(auto* W=World.Get())W->GetTimerManager().ClearTimer(Timer);
  auto J=MakeShared<FJsonObject>();J->SetBoolField(TEXT("valid"),Error.IsEmpty());J->SetStringField(TEXT("error"),Error);J->SetArrayField(TEXT("cases"),Cases);
  FString Text;FJsonSerializer::Serialize(J,TJsonWriterFactory<>::Create(&Text));const bool Saved=FFileHelper::SaveStringToFile(Text,*Output);
  UE_LOG(LogTemp,Display,TEXT("Demo preparation audit valid=%d cases=%d error=%s"),Error.IsEmpty()&&Saved,Cases.Num(),*Error);FPlatformMisc::RequestExitWithStatus(false,Error.IsEmpty()&&Saved?0:1);}
 void Request(UOKGalleryWidget* G,const FString& Option){Expected=G->GameRawClipKeys.FindRef(Option);Samples=G->PreviewActor->AnimationTickSamples;Requested=FPlatformTime::Seconds();G->GameClipCombo->SetSelectedIndex(0);G->GameClipCombo->SetSelectedOption(Option);}
 void Add(const FString& Name){auto J=MakeShared<FJsonObject>();J->SetStringField(TEXT("case"),Name);J->SetNumberField(TEXT("elapsed_ms"),(FPlatformTime::Seconds()-Requested)*1000.);J->SetStringField(TEXT("key"),Expected);Cases.Add(MakeShared<FJsonValueObject>(J));}
 bool Played(UOKGalleryWidget* G){auto* A=G->PreviewActor.Get();return A&&G->SelectedGameClip==Expected&&A->PoseMode==2&&A->AnimationTickSamples>=Samples+3&&FMath::IsFinite(A->AnimDuration)&&A->AnimDuration>0&&!A->GetActorTransform().ContainsNaN();}
 void Tick(){auto Keep=AsShared();auto* W=World.Get();if(!W||FPlatformTime::Seconds()-Started>180){Stop(TEXT("Audit timeout"));return;}
  auto* G=Gallery.Get();if(!G){for(TObjectIterator<UOKGalleryWidget> It;It;++It)if(It->GetWorld()==W&&It->PreviewImage){Gallery=*It;G=*It;break;}if(!G)return;}
  if(Stage==0){G->bIndependentCards=true;G->Entries.Reset();FKKCardListEntry E;E.Path=Card;E.FileName=FPaths::GetCleanFilename(Card);G->Entries.Add(E);G->OutfitIndex=0;G->SelectIndex(0);G->OnLoad3D();Stage=1;return;}
  if(Requested>0&&FPlatformTime::Seconds()-Requested>30){Stop(TEXT("Animation request exceeded 30 seconds"));return;}
  if(G->bAnimationPreparing&&!G->GameClipCombo->GetIsEnabled()){Stop(TEXT("Animation selector locked during preparation"));return;}
  if(G->bLoadInFlight||G->bAnimationPreparing||G->PendingAnimationKind>=0)return;
  if(!G->PreviewActor){Stop(TEXT("Preview missing"));return;}
  switch(Stage){
   case 1:
    for(const FString& Option:G->DemoAnimationOptions)if(Option==TEXT("Demo：打水漂"))Target=Option;
    if(Target.IsEmpty()||G->DemoAnimationOptions.Num()<2){Stop(TEXT("Target demo missing"));return;}
    Other=G->DemoAnimationOptions[1];Request(G,Target);Stage=2;return;
   case 2:
    if(!Played(G))return;Add(TEXT("cold_skipping_stones_playback"));
    Request(G,Other);if(!G->GameClipCombo->GetIsEnabled()){Stop(TEXT("Cannot supersede request"));return;}
    Request(G,Target);Stage=3;return;
   case 3:
    if(!Played(G))return;Add(TEXT("latest_selection_supersedes_inflight"));
    G->GameRawClipKeys.Add(TEXT("驗收無效動作"),TEXT("states:missing.unity3d|missing"));G->GameClipCombo->AddOption(TEXT("驗收無效動作"));
    Requested=FPlatformTime::Seconds();G->GameClipCombo->SetSelectedOption(TEXT("驗收無效動作"));Stage=4;return;
   case 4:
    if(!Played(G)||!G->GameClipCombo->GetIsEnabled()){Stop(TEXT("Failed request did not preserve playback and recover selector"));return;}
    Add(TEXT("invalid_animation_preserves_playback"));Request(G,Other);Stage=5;return;
   case 5:
    if(!Played(G))return;Add(TEXT("switch_to_other_demo_after_failure"));Request(G,Target);Stage=6;return;
   case 6:
    if(!Played(G))return;Add(TEXT("return_to_skipping_stones"));Stop(FString());return;
  }
 }
};
}
static FAutoConsoleCommandWithWorld AuditDemoPreparation(TEXT("ok.AuditEditorDemoPreparation"),TEXT("Reproduce cold Demo preparation, superseding and error recovery"),FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* W){
 const FString Card=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_GUI_RELOAD_CARD")),Output=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_GUI_RELOAD_OUTPUT"));
 if(!W||Card.IsEmpty()||Output.IsEmpty()||IFileManager::Get().FileExists(*Output)){FPlatformMisc::RequestExitWithStatus(false,1);return;}
 auto Run=MakeShared<OKDemoPreparationAudit::FRun>();Run->World=W;Run->Card=Card;Run->Output=Output;W->GetTimerManager().SetTimer(Run->Timer,FTimerDelegate::CreateLambda([Run](){Run->Tick();}),.05f,true);
}));
#endif
