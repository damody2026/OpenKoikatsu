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
#include "HighResScreenshot.h"
#if WITH_DEV_AUTOMATION_TESTS
namespace OKAutoSource393 {
struct FRun : TSharedFromThis<FRun> {
 TWeakObjectPtr<UWorld> World;TWeakObjectPtr<UOKGalleryWidget> Gallery;FTimerHandle Timer;
 FString Card,Output,BasicKey,MakerKey,ActionKey,ExpectedKey;FName Ordinary,HRow,ExpectedRow;TArray<TSharedPtr<FJsonValue>> Results;int32 Options=0;
 int32 StepIndex=0;bool Waiting=false;double Start=FPlatformTime::Seconds(),Begin=0,RequestMs=0;uint64 Frame=0;float OldPhase=0;int32 LiveFrames=0;bool Cancelled=false;
 void Stop(const FString& Error){auto Keep=AsShared();if(auto* W=World.Get())W->GetTimerManager().ClearTimer(Timer);auto Json=MakeShared<FJsonObject>();Json->SetBoolField(TEXT("valid"),Error.IsEmpty());Json->SetStringField(TEXT("error"),Error);Json->SetArrayField(TEXT("runs"),Results);FString Text;FJsonSerializer::Serialize(Json,TJsonWriterFactory<>::Create(&Text));bool Saved=FFileHelper::SaveStringToFile(Text,*Output);UE_LOG(LogTemp,Display,TEXT("Animation menus GUI valid=%d runs=%d saved=%d error=%s"),Error.IsEmpty()&&Saved?1:0,Results.Num(),Saved?1:0,*Error);FPlatformMisc::RequestExitWithStatus(false,Error.IsEmpty()&&Saved?0:1);}
 FName Row(UOKGalleryWidget* G,const FString& Group,const FString& Category){for(auto R:G->AnimationCatalog->FindRows(TEXT("studio_catalog"),TEXT("category"),Category))if(G->AnimationCatalog->GetCell(TEXT("studio_catalog"),R,TEXT("origin"))==TEXT("base")&&G->AnimationCatalog->GetCell(TEXT("studio_catalog"),R,TEXT("group"))==Group&&G->AnimationCatalog->GetCell(TEXT("studio_catalog"),R,TEXT("id"))==TEXT("0"))return R;return NAME_None;}
 bool Choose(UOKGalleryWidget* G,FName R){for(const auto& Option:G->GameMotionOptions)if(G->GameMotionRows.FindRef(Option)==R){G->GameClipCombo->SetSelectedIndex(0);G->GameClipCombo->SetSelectedOption(Option);return true;}return false;}
 bool ChooseKey(UOKGalleryWidget* G,const FString& Key){const FString Option=G->GameClipLabels.FindRef(Key);if(Option.IsEmpty())return false;ExpectedKey=Key;G->GameClipCombo->SetSelectedIndex(0);G->GameClipCombo->SetSelectedOption(Option);return true;}
 void Tick(){auto Keep=AsShared();auto* W=World.Get();if(!W||FPlatformTime::Seconds()-Start>540){Stop(TEXT("Timeout/world closed"));return;}auto* G=Gallery.Get();if(!G){for(TObjectIterator<UOKGalleryWidget> It;It;++It)if(It->GetWorld()==W&&It->PreviewImage){Gallery=*It;G=*It;break;}if(!G)return;}
  if(StepIndex==0){G->bIndependentCards=false;G->Entries.Reset();FKKCardListEntry Entry;Entry.Path=Card;Entry.FileName=FPaths::GetCleanFilename(Card);G->Entries.Add(Entry);G->OutfitIndex=0;G->SelectIndex(0);G->OnLoad3D();StepIndex=1;return;}
  if(G->bLoadInFlight)return;auto* A=G->PreviewActor.Get();if(!A||!A->Session.IsOpen()||!G->AnimationCatalog){Stop(TEXT("GUI/native session missing"));return;}
  if(StepIndex==1){
   if(G->bAnimationLookupBuilding)return;
   if(G->GameSourceCombo->GetParent()||G->GameSourceInput->GetParent()||G->GameSourceCombo->GetVisibility()!=ESlateVisibility::Collapsed){Stop(TEXT("Source file controls still visible"));return;}
   if(G->GameMotionOptions.IsEmpty()||G->GameMotionOptions.Num()!=G->GameMotionRows.Num()){Stop(TEXT("Global catalog row identities missing"));return;}
   TArray<FString> Keys;G->GameClipLabels.GetKeys(Keys);Keys.Sort();for(const FString& Key:Keys){if(!Key.Contains(TEXT("|"))&&BasicKey.IsEmpty())BasicKey=Key;if(Key.StartsWith(TEXT("states:custom/custompose.unity3d|"))&&MakerKey.IsEmpty())MakerKey=Key;if(Key.StartsWith(TEXT("states:action/animator/00.unity3d|"))&&ActionKey.IsEmpty())ActionKey=Key;}
   if(BasicKey.IsEmpty()||MakerKey.IsEmpty()||ActionKey.IsEmpty()){Stop(TEXT("Unified lookup missing basic/maker/action animations"));return;}
   Options=G->GameClipCombo->GetOptionCount();
   Ordinary=Row(G,TEXT("0"),TEXT("3"));HRow=Row(G,TEXT("2"),TEXT("128"));if(Ordinary.IsNone()||HRow.IsNone()){Stop(TEXT("Native Studio rows missing"));return;}StepIndex=2;}
  if(Waiting){
   if(A->PoseMode==2&&A->AnimPhase!=OldPhase){++LiveFrames;OldPhase=A->AnimPhase;}
   if(G->bAnimationPreparing||G->PendingAnimationKind>=0)return;
   if(!Cancelled&&LiveFrames<2)return;
   if((!ExpectedRow.IsNone()&&G->SelectedStudioRow!=ExpectedRow)||(!Cancelled&&A->PoseMode!=2)||(Cancelled&&A->PoseMode!=0)||A->GetActorTransform().ContainsNaN()){Stop(TEXT("Latest selection/pose did not commit correctly"));return;}
   if(G->GameClipCombo->GetOptionCount()!=Options||(!ExpectedKey.IsEmpty()&&G->SelectedGameClip!=ExpectedKey)){Stop(TEXT("Global animation list changed or wrong lookup key committed"));return;}
   if(StepIndex==4&&(!G->SelectedGameSource.IsEmpty()||G->GameSourceCombo->GetSelectedIndex()!=0)){Stop(TEXT("Preserved raw clip inherited previous global source"));return;}
   if(!ExpectedRow.IsNone()&&(G->SelectedGameSource!=G->AnimationCatalog->StudioSource(ExpectedRow)||G->GameSourceCombo->GetSelectedIndex()<1||!G->GameMotionRows.Contains(G->GameClipCombo->GetSelectedOption())||G->GameMotionRows.FindRef(G->GameClipCombo->GetSelectedOption())!=ExpectedRow)){Stop(TEXT("Motion did not automatically synchronize exact source/row"));return;}
   auto R=MakeShared<FJsonObject>();R->SetStringField(TEXT("source"),G->SelectedGameSource);R->SetNumberField(TEXT("catalog_options"),G->GameMotionOptions.Num());R->SetNumberField(TEXT("case"),StepIndex);R->SetNumberField(TEXT("request_ms"),RequestMs);R->SetNumberField(TEXT("commit_ms"),G->LastAnimationCommitMs);R->SetNumberField(TEXT("complete_ms"),(FPlatformTime::Seconds()-Begin)*1000.);R->SetNumberField(TEXT("frames"),double(GFrameCounter-Frame));R->SetNumberField(TEXT("live_animation_samples"),LiveFrames);Results.Add(MakeShared<FJsonValueObject>(R));
   UE_LOG(LogTemp,Display,TEXT("Animation menus GUI case=%d request_ms=%.3f frames=%llu live_samples=%d selected=%s"),StepIndex,RequestMs,GFrameCounter-Frame,LiveFrames,*G->SelectedStudioRow.ToString());
   // The first native playback also creates/compiles its render materials.
   // Record that cold cost separately; subsequent switches must stay fast.
   if(RequestMs>150.||(!Cancelled&&StepIndex>2&&G->LastAnimationCommitMs>150.)){Stop(TEXT("Selection request/warm commit still blocks >150ms"));return;}Waiting=false;++StepIndex;
   if(StepIndex==12){Stop(FString());return;}
  }
  Begin=FPlatformTime::Seconds();Frame=GFrameCounter;LiveFrames=0;OldPhase=A->AnimPhase;ExpectedRow=NAME_None;ExpectedKey.Reset();Cancelled=false;
  switch(StepIndex){
   case 2:ExpectedRow=Ordinary;if(!Choose(G,Ordinary)){Stop(TEXT("Studio selection missing"));return;}break;
   case 3:ExpectedRow=Ordinary;if(!Choose(G,Ordinary)){Stop(TEXT("Repeat Studio selection missing"));return;}break;
   case 4:if(!ChooseKey(G,BasicKey)){Stop(TEXT("Basic lookup missing"));return;}break;
   case 5:if(!ChooseKey(G,MakerKey)){Stop(TEXT("Maker lookup missing"));return;}break;
   case 6:if(!ChooseKey(G,ActionKey)){Stop(TEXT("Action lookup missing"));return;}break;
   case 7:if(!ChooseKey(G,MakerKey)){Stop(TEXT("Repeated lookup missing"));return;}break;
   case 8:G->OnReplayPlayback();break;
   case 9:ExpectedRow=HRow;if(!Choose(G,HRow)){Stop(TEXT("H animation lookup missing"));return;}FScreenshotRequest::RequestScreenshot(FPaths::GetPath(Output)/TEXT("UnifiedAnimationMenu.png"),true,false);break;
   case 10:{ExpectedRow=HRow;G->bPairPreparing=true;const int32 OldPairGeneration=G->PairPrepareGeneration;if(!Choose(G,Ordinary)||!Choose(G,HRow)){Stop(TEXT("Superseded selections missing"));return;}if(G->bPairPreparing||G->PairPrepareGeneration<=OldPairGeneration){Stop(TEXT("Single selection did not cancel pending pair generation"));return;}break;}
   case 11:G->BeginAnimationPreparation(4,G->SelectedGameClip,ESelectInfo::Direct);RequestMs=(FPlatformTime::Seconds()-Begin)*1000.;G->OnPoseTpose();Cancelled=true;break;
  }
  if(!Cancelled)RequestMs=(FPlatformTime::Seconds()-Begin)*1000.;Waiting=true;
 }
};
}
static FAutoConsoleCommandWithWorld AutoSource393(TEXT("ok.ProfileAutoSource"),TEXT("Exercise real GUI animation menus, root/card/fresh output required"),FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* W){
 FString Card=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_GUI_RELOAD_CARD")),Root=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_GUI_RELOAD_ROOT")),Output=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_GUI_RELOAD_OUTPUT"));
 if(!W||Card.IsEmpty()||Root.IsEmpty()||Output.IsEmpty()||IFileManager::Get().FileExists(*Output)){FPlatformMisc::RequestExitWithStatus(false,1);return;}UKKGameRoot::Get()->Root=Root;auto Run=MakeShared<OKAutoSource393::FRun>();Run->World=W;Run->Card=Card;Run->Output=Output;W->GetTimerManager().SetTimer(Run->Timer,FTimerDelegate::CreateLambda([Run](){Run->Tick();}),.05f,true);
}));
#endif
