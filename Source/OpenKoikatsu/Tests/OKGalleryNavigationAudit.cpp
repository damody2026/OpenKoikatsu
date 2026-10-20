#include "../OKGalleryWidget.h"
#include "../OKInteractionScene.h"
#include "KKPreviewActor.h"
#include "Blueprint/WidgetTree.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/ComboBoxString.h"
#include "Components/Slider.h"
#include "Engine/World.h"
#include "Engine/GameViewportClient.h"
#include "HAL/IConsoleManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HighResScreenshot.h"
#include "TimerManager.h"
#include "UObject/UObjectIterator.h"
#if !UE_BUILD_SHIPPING
namespace {
struct FOKGalleryNavigationAudit: TSharedFromThis<FOKGalleryNavigationAudit> {
 TWeakObjectPtr<UWorld> World;TWeakObjectPtr<UOKGalleryWidget> Gallery;TWeakObjectPtr<AKKPreviewActor> LoadedActor;
 FTimerHandle Timer;FString Card,Output;int32 Stage=0;double At=0.,Started=0.;bool Portable=false;TArray<float> FixedBody,FixedFace;float EditedValue=0.;
 int32 PreviousDebugPose=INDEX_NONE;
 void Finish(const FString& Error){const auto Keep=AsShared();if(auto* W=World.Get())W->GetTimerManager().ClearTimer(Timer);
  const FString Report=FString::Printf(TEXT("audit_mode=gallery_navigation\naudit_completed=%d\nfunctional_pass=%d\nerror=%s\neditor_and_interaction_separate=%d\n"),Error.IsEmpty(),Error.IsEmpty(),*Error,Error.IsEmpty());
  FFileHelper::SaveStringToFile(Report,*(Output/TEXT("report.txt")));UE_LOG(LogTemp,Display,TEXT("OK navigation audit %s"),*Report);FPlatformMisc::RequestExitWithStatus(false,Error.IsEmpty()?0:1);}
 bool VisibleButton(UOKGalleryWidget* G,UButton* Button){
  if(!Button||!Button->GetIsEnabled()||!Button->IsVisible()){UE_LOG(LogTemp,Error,TEXT("Navigation button absent/disabled/hidden"));return false;}
  const auto& Root=G->GetCachedGeometry();const auto& Geometry=Button->GetCachedGeometry();const FVector2D Size=Geometry.GetLocalSize();
  const FVector2D Min=Root.AbsoluteToLocal(Geometry.LocalToAbsolute(FVector2D::ZeroVector));const FVector2D Max=Root.AbsoluteToLocal(Geometry.LocalToAbsolute(Size));const FVector2D Bounds=Root.GetLocalSize();
  const bool Good=Size.X>20&&Size.Y>15&&Min.X>=-.5&&Min.Y>=-.5&&Max.X<=Bounds.X+.5&&Max.Y<=Bounds.Y+.5;
  if(!Good)UE_LOG(LogTemp,Error,TEXT("Navigation layout button=%s size=%s min=%s max=%s root=%s"),*Button->GetName(),*Size.ToString(),*Min.ToString(),*Max.ToString(),*Bounds.ToString());return Good;
 }
 UButton* Button(UOKGalleryWidget* G,const TCHAR* Name){return G->WidgetTree?Cast<UButton>(G->WidgetTree->FindWidget(FName(Name))):nullptr;}
 void Shot(const TCHAR* Name){FScreenshotRequest::RequestScreenshot(Output/Name,true,false);}
 void Tick(){const auto Keep=AsShared();auto* W=World.Get();if(!W)return;if(FPlatformTime::Seconds()-Started>120.){Finish(TEXT("Navigation timeout"));return;}
  if(Stage==0){for(TObjectIterator<UOKGalleryWidget> It;It;++It)if(It->GetWorld()==W&&It->IsInViewport()){Gallery=*It;break;}auto* G=Gallery.Get();if(!G||!G->Load3DButton)return;
   G->bIndependentCards=Portable;G->Entries.Reset();FKKCardListEntry Entry;Entry.Path=Card;Entry.FileName=FPaths::GetCleanFilename(Card);G->Entries.Add(Entry);G->OutfitIndex=0;G->SelectIndex(0);G->SetModelLayout(false);At=FPlatformTime::Seconds();Stage=1;return;}
  auto* G=Gallery.Get();if(!G){Finish(TEXT("Gallery destroyed"));return;}const double Age=FPlatformTime::Seconds()-At;
  if(Stage==1&&Age>.5){Shot(TEXT("Gallery.png"));At=FPlatformTime::Seconds();Stage=2;return;}
  if(Stage==2&&Age>.5){if(!VisibleButton(G,G->Load3DButton)||!VisibleButton(G,Button(G,TEXT("CardInteraction")))){Finish(TEXT("Card page entries clipped or hidden"));return;}
   G->Load3DButton->OnClicked.Broadcast();At=FPlatformTime::Seconds();Stage=3;return;}
  if(Stage==3){if(G->bLoadInFlight)return;if(!G->PreviewActor||!G->PreviewActor->Session.IsOpen()||!G->bModelLayout||G->InteractionWidget||G->InteractionScene){Finish(TEXT("Load3D did not exclusively enter the editor"));return;}
   LoadedActor=G->PreviewActor;At=FPlatformTime::Seconds();Stage=4;return;}
  if(Stage==4&&Age>.5){if(!VisibleButton(G,G->ShapeLoadButton)||!VisibleButton(G,Button(G,TEXT("EditorInteraction")))){Finish(TEXT("Editor entries clipped or hidden"));return;}
   Shot(TEXT("Editor.png"));const auto& Body=G->Selected.Body.ShapeValueBody;if(!Body.IsValidIndex(21)){Finish(TEXT("Editor has no body parameters"));return;}EditedValue=Body[21]<.6f?.7f:.3f;G->OnShapeChanged(true,21,EditedValue);At=FPlatformTime::Seconds();Stage=5;return;}
  if(Stage==5&&Age>.5){if(!FMath::IsNearlyEqual(G->PreviewActor->Session.ShapeBody()[21],EditedValue)||G->PreviewActor->bUsePrivateGpuPlayback){Finish(TEXT("Editor live shape/display path failed"));return;}
   if(auto* Variable=IConsoleManager::Get().FindConsoleVariable(TEXT("r.SkeletalMesh.CopyDebugPose")))PreviousDebugPose=Variable->GetInt();
   FixedBody=G->Selected.Body.ShapeValueBody;FixedFace=G->Selected.Face.ShapeValueFace;Button(G,TEXT("EditorInteraction"))->OnClicked.Broadcast();At=FPlatformTime::Seconds();Stage=6;return;}
  if(Stage==6&&Age>.5){if(!G->InteractionWidget||!G->InteractionScene||G->GetVisibility()!=ESlateVisibility::Collapsed||G->PreviewActor!=LoadedActor.Get()){Finish(TEXT("Separate interaction scene did not enter"));return;}
   if(!G->PreviewActor->bUsePrivateGpuPlayback){Finish(TEXT("Interaction did not opt into its GPU playback path"));return;}
   if(G->InteractionScene->DebugPoseGuard){auto* Variable=IConsoleManager::Get().FindConsoleVariable(TEXT("r.SkeletalMesh.CopyDebugPose"));auto* Setting=IConsoleManager::Get().FindConsoleVariable(TEXT("ok.Interaction.CopyDebugPose"));
    const int32 Expected=(Variable->GetFlags()&ECVF_SetByMask)>ECVF_SetByCode?PreviousDebugPose:Setting->GetInt();if(Variable->GetInt()!=Expected){Finish(TEXT("Interaction debug pose scope failed"));return;}}
   G->OnShapeChanged(true,21,EditedValue+.1f);G->OnShapeChanged(false,0,.2f);
   Shot(TEXT("Interaction.png"));At=FPlatformTime::Seconds();Stage=7;return;}
  if(Stage==7&&Age>.5){if(G->Selected.Body.ShapeValueBody!=FixedBody||G->Selected.Face.ShapeValueFace!=FixedFace||G->PreviewActor->Session.ShapeBody()!=FixedBody){Finish(TEXT("Interaction changed fixed character parameters"));return;}
   if(!G->InteractionWidget->Belly){Finish(TEXT("Interaction belly control missing"));return;}
   G->InteractionWidget->Belly->OnValueChanged.Broadcast(EditedValue+.1f);FixedBody[21]=EditedValue+.1f;At=FPlatformTime::Seconds();Stage=12;return;}
  if(Stage==12&&Age>.5){if(G->Selected.Body.ShapeValueBody!=FixedBody||G->Selected.Face.ShapeValueFace!=FixedFace||G->PreviewActor->Session.ShapeBody()!=FixedBody){Finish(TEXT("Interaction belly exception failed or changed other parameters"));return;}
   G->InteractionWidget->OnReturn();At=FPlatformTime::Seconds();Stage=8;return;}
  if(Stage==8&&Age>.5){if(G->InteractionWidget||G->InteractionScene||!G->bModelLayout||!VisibleButton(G,G->ShapeLoadButton)||!G->MouthPatternCombo->GetParent()||G->PreviewActor!=LoadedActor.Get()){Finish(TEXT("Returning failed to restore the editor"));return;}
   if(G->PreviewActor->bUsePrivateGpuPlayback){Finish(TEXT("Editor kept interaction GPU ownership"));return;}
   if(PreviousDebugPose!=INDEX_NONE)if(auto* Variable=IConsoleManager::Get().FindConsoleVariable(TEXT("r.SkeletalMesh.CopyDebugPose")))if(Variable->GetInt()!=PreviousDebugPose){Finish(TEXT("Returning did not restore debug pose setting"));return;}
   Shot(TEXT("EditorReturned.png"));G->OnShapeChanged(true,21,EditedValue+.05f);At=FPlatformTime::Seconds();Stage=9;return;}
  if(Stage==9&&Age>.5){if(!FMath::IsNearlyEqual(G->PreviewActor->Session.ShapeBody()[21],EditedValue+.05f)){Finish(TEXT("Returning did not restore live shape editing"));return;}G->OnBackToGallery();At=FPlatformTime::Seconds();Stage=10;return;}
  if(Stage==10&&Age>.5){if(G->bModelLayout||G->InteractionWidget||!VisibleButton(G,G->Load3DButton)||!VisibleButton(G,Button(G,TEXT("CardInteraction")))){Finish(TEXT("Returning lost card page Load3D"));return;}
   Shot(TEXT("GalleryReturned.png"));At=FPlatformTime::Seconds();Stage=11;return;}
  if(Stage==11&&Age>.5)Finish(FString());
 }
};
FAutoConsoleCommandWithWorld NavigationCommand(TEXT("ok.GalleryNavigationAudit"),TEXT("Actual APNG editor/interaction entry and return UI audit"),FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* W){
 auto Job=MakeShared<FOKGalleryNavigationAudit>();Job->World=W;Job->Card=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_INTERACTION_CARD"));Job->Output=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_INTERACTION_OUTPUT"));Job->Portable=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_INTERACTION_APNG"))==TEXT("1");Job->Started=FPlatformTime::Seconds();W->GetTimerManager().SetTimer(Job->Timer,FTimerDelegate::CreateLambda([Job](){Job->Tick();}),.1,true);
}));
}
#endif
