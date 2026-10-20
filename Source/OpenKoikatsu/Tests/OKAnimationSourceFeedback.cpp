#include "../OKGalleryWidget.h"
#include "KKPreviewActor.h"
#include "KKGameRoot.h"
#include "Components/ComboBoxString.h"
#include "Components/EditableTextBox.h"
#include "Components/TextBlock.h"
#include "Components/PanelWidget.h"
#include "HAL/IConsoleManager.h"
#include "HAL/FileManager.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/UObjectIterator.h"
#if WITH_DEV_AUTOMATION_TESTS
namespace OKSourceFeedback392 {
struct FRun : TSharedFromThis<FRun> {
 TWeakObjectPtr<UWorld> World;TWeakObjectPtr<UOKGalleryWidget> Gallery;FTimerHandle Timer;
 FString Card,Output,PreviousClip,PreviousSource;TArray<FString> PreviousNames;TArray<TSharedPtr<FJsonValue>> Results;
 int32 Step=0;bool Waiting=false;double Start=FPlatformTime::Seconds();float Phase=0;int32 Samples=0;
 void Stop(const FString& Error){auto Keep=AsShared();if(auto* W=World.Get())W->GetTimerManager().ClearTimer(Timer);auto J=MakeShared<FJsonObject>();J->SetBoolField(TEXT("valid"),Error.IsEmpty());J->SetStringField(TEXT("error"),Error);J->SetArrayField(TEXT("cases"),Results);FString Text;FJsonSerializer::Serialize(J,TJsonWriterFactory<>::Create(&Text));bool Saved=FFileHelper::SaveStringToFile(Text,*Output);UE_LOG(LogTemp,Display,TEXT("Animation source feedback valid=%d cases=%d error=%s"),Error.IsEmpty()&&Saved?1:0,Results.Num(),*Error);FPlatformMisc::RequestExitWithStatus(false,Error.IsEmpty()&&Saved?0:1);}
 TArray<FString> Names(UOKGalleryWidget* G){TArray<FString> R;for(int32 I=0;I<G->GameClipCombo->GetOptionCount();++I)R.Add(G->GameClipCombo->GetOptionAtIndex(I));return R;}
 void Tick(){auto Keep=AsShared();auto* W=World.Get();if(!W||FPlatformTime::Seconds()-Start>540){Stop(TEXT("Timeout/world closed"));return;}auto* G=Gallery.Get();if(!G){for(TObjectIterator<UOKGalleryWidget> It;It;++It)if(It->GetWorld()==W&&It->PreviewImage){Gallery=*It;G=*It;break;}if(!G)return;}
  if(Step==0){G->bIndependentCards=false;G->Entries.Reset();FKKCardListEntry E;E.Path=Card;E.FileName=FPaths::GetCleanFilename(Card);G->Entries.Add(E);G->OutfitIndex=0;G->SelectIndex(0);G->OnLoad3D();Step=1;return;}
  if(G->bLoadInFlight)return;auto* A=G->PreviewActor.Get();if(!A||!A->Session.IsOpen()||!G->AnimationStatus){Stop(TEXT("Missing native session/status"));return;}
  if(Waiting){
   if(A->PoseMode==2&&A->AnimPhase!=Phase){Phase=A->AnimPhase;++Samples;}
   if(G->bAnimationPreparing||G->PendingAnimationKind>=0)return;
   const FString Status=G->AnimationStatus->GetText().ToString();
   if(!G->GameClipCombo->GetIsEnabled()){Stop(TEXT("Clip list stayed disabled"));return;}
   if((Step==2||Step==3)&&Samples<2)return;
   bool Valid=!A->GetActorTransform().ContainsNaN();
   if(Step==1||Step==4||Step==5||Step==8)Valid&=G->GameClipCombo->GetOptionCount()>1&&Status.Contains(TEXT("已載入"))&&G->GameClipCombo->GetSelectedIndex()==0;
   if(Step==1)Valid&=A->PoseMode==0;
   if(Step==2||Step==3)Valid&=A->PoseMode==2&&!G->SelectedGameClip.IsEmpty()&&Status.Contains(Step==2?TEXT("正在播放"):TEXT("正在重播"));
   if(Step>=4&&Step<=8)Valid&=G->SelectedGameClip==PreviousClip&&A->PoseMode==2;
   if(Step==5)Valid&=Names(G)==PreviousNames&&G->SelectedGameSource==PreviousSource;
   if(Step==6||Step==7)Valid&=Names(G)==PreviousNames&&G->SelectedGameSource==PreviousSource&&Status.Contains(TEXT("沒有可播放"));
   if(Step==7)Valid&=G->GameSourceCombo->GetSelectedOption()==PreviousSource;
   if(Step==9)Valid&=A->PoseMode==0&&Status.Contains(TEXT("已取消"));
   if(!Valid){Stop(FString::Printf(TEXT("Case %d failed: %s"),Step,*Status));return;}
   auto R=MakeShared<FJsonObject>();R->SetNumberField(TEXT("case"),Step);R->SetStringField(TEXT("status"),Status);R->SetNumberField(TEXT("clips"),G->GameClipCombo->GetOptionCount()-1);R->SetNumberField(TEXT("moving_samples"),Samples);Results.Add(MakeShared<FJsonValueObject>(R));Waiting=false;++Step;if(Step==10){Stop(FString());return;}
  }
  switch(Step){
   case 1:{auto* Parent=G->GameSourceCombo->GetParent();if(!Parent||Parent!=G->GameClipCombo->GetParent()||Parent->GetChildIndex(G->GameClipCombo)!=Parent->GetChildIndex(G->GameSourceCombo)+1){Stop(TEXT("Source and clips not adjacent"));return;}G->OnGameSourceSelected(TEXT("基本動作"),ESelectInfo::Direct);break;}
   case 2:G->GameClipCombo->SetSelectedIndex(1);break;
   case 3:G->OnReplayPlayback();break;
   case 4:PreviousClip=G->SelectedGameClip;G->GameSourceCombo->SetSelectedOption(TEXT("studio/anime/00.unity3d"));break;
   case 5:PreviousSource=G->SelectedGameSource;PreviousNames=Names(G);G->GameClipCombo->ClearOptions();G->GameClipCombo->AddOption(TEXT("stale fixture"));G->GameSourceInput->SetText(FText::FromString(PreviousSource));G->OnLoadGameSource();break;
   case 6:G->GameSourceInput->SetText(FText::FromString(TEXT("missing392/no-controller.unity3d")));G->OnLoadGameSource();break;
   case 7:G->GameSourceCombo->AddOption(TEXT("missing392/no-controller.unity3d"));G->GameSourceCombo->SetSelectedOption(TEXT("missing392/no-controller.unity3d"));break;
   case 8:{G->GameSourceCombo->SetSelectedIndex(0);const int32 Generation=G->AnimationPrepareGeneration;G->OnGameClipSelected(G->GameClipCombo->GetOptionAtIndex(1),ESelectInfo::Direct);if(G->AnimationPrepareGeneration!=Generation){Stop(TEXT("Old clip selection superseded pending source"));return;}G->GameSourceCombo->SetSelectedOption(PreviousSource);break;}
   case 9:G->OnLoadGameSource();G->OnPoseTpose();break;
  }
  if(!G->bAnimationPreparing||G->GameClipCombo->GetIsEnabled()||!G->AnimationStatus->GetText().ToString().Contains(TEXT("正在"))){Stop(TEXT("No visible pending feedback/old clip guard"));return;}
  Waiting=true;
 }
};
}
static FAutoConsoleCommandWithWorld SourceFeedback392(TEXT("ok.TestAnimationSourceFeedback"),TEXT("Exercise source selection feedback/reload/failure through real GUI"),FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* W){
 FString Card=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_GUI_RELOAD_CARD")),Root=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_GUI_RELOAD_ROOT")),Output=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_GUI_RELOAD_OUTPUT"));
 if(!W||Card.IsEmpty()||Root.IsEmpty()||Output.IsEmpty()||IFileManager::Get().FileExists(*Output)){FPlatformMisc::RequestExitWithStatus(false,1);return;}UKKGameRoot::Get()->Root=Root;auto Run=MakeShared<OKSourceFeedback392::FRun>();Run->World=W;Run->Card=Card;Run->Output=Output;W->GetTimerManager().SetTimer(Run->Timer,FTimerDelegate::CreateLambda([Run](){Run->Tick();}),.05f,true);
}));
#endif
