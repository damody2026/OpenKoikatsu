#include "../OKGalleryWidget.h"
#include "KKPreviewActor.h"
#include "KKAnimationCatalog.h"
#include "KKGameRoot.h"
#include "Components/ComboBoxString.h"
#include "Components/EditableTextBox.h"
#include "HAL/IConsoleManager.h"
#include "HAL/FileManager.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/UObjectIterator.h"
#if WITH_DEV_AUTOMATION_TESTS
namespace OKAnimationMenus391 {
struct FRun : TSharedFromThis<FRun> {
 TWeakObjectPtr<UWorld> World;TWeakObjectPtr<UOKGalleryWidget> Gallery;FTimerHandle Timer;
 FString Card,Output,SourceLabel;FName Ordinary,HRow,ExpectedRow;TArray<TSharedPtr<FJsonValue>> Results;
 int32 StepIndex=0;bool Waiting=false;double Start=FPlatformTime::Seconds(),Begin=0,RequestMs=0;uint64 Frame=0;float OldPhase=0;int32 LiveFrames=0;bool Cancelled=false;
 void Stop(const FString& Error){auto Keep=AsShared();if(auto* W=World.Get())W->GetTimerManager().ClearTimer(Timer);auto Json=MakeShared<FJsonObject>();Json->SetBoolField(TEXT("valid"),Error.IsEmpty());Json->SetStringField(TEXT("error"),Error);Json->SetArrayField(TEXT("runs"),Results);FString Text;FJsonSerializer::Serialize(Json,TJsonWriterFactory<>::Create(&Text));bool Saved=FFileHelper::SaveStringToFile(Text,*Output);UE_LOG(LogTemp,Display,TEXT("Animation menus GUI valid=%d runs=%d saved=%d error=%s"),Error.IsEmpty()&&Saved?1:0,Results.Num(),Saved?1:0,*Error);FPlatformMisc::RequestExitWithStatus(false,Error.IsEmpty()&&Saved?0:1);}
 FName Row(UOKGalleryWidget* G,const FString& Group,const FString& Category){for(auto R:G->AnimationCatalog->FindRows(TEXT("studio_catalog"),TEXT("category"),Category))if(G->AnimationCatalog->GetCell(TEXT("studio_catalog"),R,TEXT("origin"))==TEXT("base")&&G->AnimationCatalog->GetCell(TEXT("studio_catalog"),R,TEXT("group"))==Group&&G->AnimationCatalog->GetCell(TEXT("studio_catalog"),R,TEXT("id"))==TEXT("0"))return R;return NAME_None;}
 bool Choose(UOKGalleryWidget* G,FName R){for(const auto& P:G->StudioCategoryRows)if(P.Value.Contains(R)){G->StudioCategoryCombo->SetSelectedOption(P.Key);int32 I=G->StudioMotionRows.Find(R);if(I<0)return false;G->StudioMotionCombo->SetSelectedIndex(0);G->StudioMotionCombo->SetSelectedIndex(I+1);return true;}return false;}
 void Tick(){auto Keep=AsShared();auto* W=World.Get();if(!W||FPlatformTime::Seconds()-Start>540){Stop(TEXT("Timeout/world closed"));return;}auto* G=Gallery.Get();if(!G){for(TObjectIterator<UOKGalleryWidget> It;It;++It)if(It->GetWorld()==W&&It->PreviewImage){Gallery=*It;G=*It;break;}if(!G)return;}
  if(StepIndex==0){G->bIndependentCards=false;G->Entries.Reset();FKKCardListEntry Entry;Entry.Path=Card;Entry.FileName=FPaths::GetCleanFilename(Card);G->Entries.Add(Entry);G->OutfitIndex=0;G->SelectIndex(0);G->OnLoad3D();StepIndex=1;return;}
  if(G->bLoadInFlight)return;auto* A=G->PreviewActor.Get();if(!A||!A->Session.IsOpen()||!G->AnimationCatalog){Stop(TEXT("GUI/native session missing"));return;}
  if(StepIndex==1){Ordinary=Row(G,TEXT("0"),TEXT("3"));HRow=Row(G,TEXT("2"),TEXT("128"));if(Ordinary.IsNone()||HRow.IsNone()){Stop(TEXT("Native Studio rows missing"));return;}StepIndex=2;}
  if(Waiting){
   if(A->PoseMode==2&&A->AnimPhase!=OldPhase){++LiveFrames;OldPhase=A->AnimPhase;}
   if(G->bAnimationPreparing||G->PendingAnimationKind>=0)return;
   if((!ExpectedRow.IsNone()&&G->SelectedStudioRow!=ExpectedRow)||(!Cancelled&&A->PoseMode!=2)||(Cancelled&&A->PoseMode!=0)||A->GetActorTransform().ContainsNaN()){Stop(TEXT("Latest selection/pose did not commit correctly"));return;}
   auto R=MakeShared<FJsonObject>();R->SetNumberField(TEXT("case"),StepIndex);R->SetNumberField(TEXT("request_ms"),RequestMs);R->SetNumberField(TEXT("commit_ms"),G->LastAnimationCommitMs);R->SetNumberField(TEXT("complete_ms"),(FPlatformTime::Seconds()-Begin)*1000.);R->SetNumberField(TEXT("frames"),double(GFrameCounter-Frame));R->SetNumberField(TEXT("live_animation_samples"),LiveFrames);Results.Add(MakeShared<FJsonValueObject>(R));
   UE_LOG(LogTemp,Display,TEXT("Animation menus GUI case=%d request_ms=%.3f frames=%llu live_samples=%d selected=%s"),StepIndex,RequestMs,GFrameCounter-Frame,LiveFrames,*G->SelectedStudioRow.ToString());
   if(RequestMs>150.||(!Cancelled&&G->LastAnimationCommitMs>150.)){Stop(TEXT("Selection request/commit still blocks >150ms"));return;}Waiting=false;++StepIndex;
   if(StepIndex==12){Stop(FString());return;}
  }
  Begin=FPlatformTime::Seconds();Frame=GFrameCounter;LiveFrames=0;OldPhase=A->AnimPhase;ExpectedRow=NAME_None;Cancelled=false;
  switch(StepIndex){
   case 2:ExpectedRow=Ordinary;if(!Choose(G,Ordinary)){Stop(TEXT("Studio selection missing"));return;}break;
   case 3:ExpectedRow=Ordinary;if(!Choose(G,Ordinary)){Stop(TEXT("Repeat Studio selection missing"));return;}break;
   case 4:SourceLabel=G->GameSourceCombo->GetSelectedOption();G->GameSourceCombo->SetSelectedIndex(0);break;
   case 5:G->GameSourceCombo->SetSelectedOption(SourceLabel);break;
   case 6:if(G->GameClipCombo->GetOptionCount()<2){Stop(TEXT("Source clips missing"));return;}G->GameClipCombo->SetSelectedIndex(1);break;
   case 7:G->GameClipCombo->SetSelectedIndex(0);G->GameClipCombo->SetSelectedIndex(1);break;
   case 8:G->OnReplayPlayback();break;
   case 9:G->GameSourceInput->SetText(FText::FromString(TEXT("studio/anime/00.unity3d")));G->OnLoadGameSource();break;
   case 10:{ExpectedRow=HRow;G->bPairPreparing=true;const int32 OldPairGeneration=G->PairPrepareGeneration;if(!Choose(G,Ordinary)||!Choose(G,HRow)){Stop(TEXT("Superseded selections missing"));return;}if(G->bPairPreparing||G->PairPrepareGeneration<=OldPairGeneration){Stop(TEXT("Single selection did not cancel pending pair generation"));return;}break;}
   case 11:G->BeginAnimationPreparation(4,G->SelectedGameClip,ESelectInfo::Direct);RequestMs=(FPlatformTime::Seconds()-Begin)*1000.;G->OnPoseTpose();Cancelled=true;break;
  }
  if(!Cancelled)RequestMs=(FPlatformTime::Seconds()-Begin)*1000.;Waiting=true;
 }
};
}
static FAutoConsoleCommandWithWorld AnimationMenus391(TEXT("ok.ProfileAnimationMenus"),TEXT("Exercise real GUI animation menus, root/card/fresh output required"),FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* W){
 FString Card=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_GUI_RELOAD_CARD")),Root=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_GUI_RELOAD_ROOT")),Output=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_GUI_RELOAD_OUTPUT"));
 if(!W||Card.IsEmpty()||Root.IsEmpty()||Output.IsEmpty()||IFileManager::Get().FileExists(*Output)){FPlatformMisc::RequestExitWithStatus(false,1);return;}UKKGameRoot::Get()->Root=Root;auto Run=MakeShared<OKAnimationMenus391::FRun>();Run->World=W;Run->Card=Card;Run->Output=Output;W->GetTimerManager().SetTimer(Run->Timer,FTimerDelegate::CreateLambda([Run](){Run->Tick();}),.05f,true);
}));
#endif
