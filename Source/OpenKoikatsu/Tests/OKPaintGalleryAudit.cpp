#include "../OKGalleryWidget.h"
#include "../OpenKoikatsuPlayerController.h"
#include "../OKPaintSprayer.h"
#include "KKPreviewActor.h"
#include "KKGameRoot.h"
#include "../OKPaintDemo.h"
#include "EngineUtils.h"
#include "Blueprint/UserWidget.h"
#include "Camera/CameraActor.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/PrimitiveComponent.h"
#include "ProceduralMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "SkeletalRenderPublic.h"
#include "Engine/World.h"
#include "Engine/GameViewportClient.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HighResScreenshot.h"
#include "UObject/UObjectIterator.h"
#if WITH_DEV_AUTOMATION_TESTS
namespace OKPaintGalleryAudit {
struct FRun:TSharedFromThis<FRun>{
 TWeakObjectPtr<UWorld> World;TWeakObjectPtr<UOKGalleryWidget> Gallery;FTimerHandle Timer;
 FString Card,Output;int32 Stage=0;double Started=FPlatformTime::Seconds(),PaintAt=0;FVector Origin,Direction;double Deposited=0;int32 Hits=0;FString Stats;
 void Stop(const FString& Error){const auto Keep=AsShared();if(auto* W=World.Get())W->GetTimerManager().ClearTimer(Timer);FString Report=FString::Printf(TEXT("valid=%d card=%s deposited=%.9f hits=%d error=%s\n%s\n"),Error.IsEmpty(),*Card,Deposited,Hits,*Error,*Stats);FFileHelper::SaveStringToFile(Report,*(Output/TEXT("report.txt")));UE_LOG(LogTemp,Display,TEXT("Paint Gallery audit %s"),*Report);FPlatformMisc::RequestExitWithStatus(false,Error.IsEmpty()?0:1);}
 void Tick(){const auto Keep=AsShared();auto* W=World.Get();if(!W||FPlatformTime::Seconds()-Started>540){Stop(TEXT("Gallery timeout"));return;}
  auto* G=Gallery.Get();if(!G){for(TObjectIterator<UOKGalleryWidget> It;It;++It)if(It->GetWorld()==W&&It->PreviewImage){Gallery=*It;G=*It;break;}if(!G){auto* Controller=Cast<AOpenKoikatsuPlayerController>(W->GetFirstPlayerController());if(!Controller)return;Controller->SetPaintPreviewActive(false);for(TActorIterator<AOKPaintDemoCharacter> It(W);It;++It)It->Destroy();G=CreateWidget<UOKGalleryWidget>(Controller,UOKGalleryWidget::StaticClass());if(!G)return;G->AddToViewport(100);Gallery=G;}}
  auto* PC=Cast<AOpenKoikatsuPlayerController>(W->GetFirstPlayerController());if(!PC){Stop(TEXT("Not the gallery player controller"));return;}
  if(Stage==0){if(PC->IsPaintPreviewActive()){Stop(TEXT("Watergun enabled before loading a card"));return;}G->bIndependentCards=false;G->Entries.Reset();FKKCardListEntry Entry;Entry.Path=Card;Entry.FileName=FPaths::GetCleanFilename(Card);G->Entries.Add(Entry);G->OutfitIndex=0;G->SelectIndex(0);G->OnLoad3D();Stage=1;return;}
  if(Stage==1){if(G->bLoadInFlight)return;if(!IsValid(G->PreviewActor)||!G->PreviewActor->Session.IsOpen()||!PC->IsPaintPreviewActive()||!IsValid(PC->PaintSprayer)){Stop(TEXT("Loaded card did not enable watergun"));return;}
   auto* A=G->PreviewActor.Get();A->FrameCurrentPose();PC->SetActorTickEnabled(false);PaintAt=W->GetTimeSeconds();Stage=2;return;}
  if(Stage==6){if(G->bLoadInFlight)return;auto* Reused=PC->PaintSprayer.Get();if(!IsValid(G->PreviewActor)||!PC->IsPaintPreviewActive()||!Reused||Reused->RemainingVolume()!=0){Stop(TEXT("Reload failed to enable a clean sprayer"));return;}Stop(FString());return;}
  auto* A=G->PreviewActor.Get();auto* S=PC->PaintSprayer.Get();if(!A||!S){Stop(TEXT("Missing loaded preview or watergun"));return;}
  if(PC->PlayerCameraManager)PC->PlayerCameraManager->UpdateCamera(0);
  const double Age=W->GetTimeSeconds()-PaintAt;
  if(Stage==2){FVector Target=A->GetActorLocation();bool Body=false;TInlineComponentArray<USkinnedMeshComponent*> Display;A->GetComponents(Display);
   for(auto* C:Display)if(C&&C->IsVisible()&&C->bRenderInMainPass){
    const bool BodyPart=A->Parts.ContainsByPredicate([C](const auto& P){return P.Role.Contains(TEXT("body"))&&P.SurfaceMat&&P.SurfaceMat==C->GetMaterial(0);});
    const bool BodyMorph=A->MouthSlots.ContainsByPredicate([C,A](const auto& Slot){return Slot.Comp==C&&A->Parts.ContainsByPredicate([&Slot](const auto& P){return P.SessionIndex==Slot.SessionPartIndex&&P.Role.Contains(TEXT("body"));});});
    if(!BodyPart&&!BodyMorph)continue;auto* Mesh=Cast<USkeletalMesh>(C->GetSkinnedAsset());if(!Mesh)continue;FBox Box(ForceInit);
    if(C->IsA<UPoseableMeshComponent>()){const auto& LOD=Mesh->GetResourceForRendering()->LODRenderData[0];TArray<FMatrix44f> Matrices;TArray<FVector3f> Vertices;C->GetCurrentRefToLocalMatrices(Matrices,0);auto* Weights=C->GetSkinWeightBuffer(0);if(!Weights)continue;USkinnedMeshComponent::ComputeSkinnedPositions(C,Vertices,Matrices,LOD,*Weights);for(const auto& V:Vertices)Box+=C->GetComponentTransform().TransformPosition(FVector(V));}
    else {TArray<FFinalSkinVertex> Vertices;C->GetCPUSkinnedVertices(Vertices,0);for(const auto& V:Vertices)Box+=C->GetComponentTransform().TransformPosition(FVector(V.Position));}
    if(!Box.IsValid)continue;Target=Box.GetCenter();Target.X=Box.GetCenter().X+Box.GetExtent().X*.16;Target.Z=Box.Min.Z+(Box.Max.Z-Box.Min.Z)*.23;Body=true;break;
   }
   if(!Body)for(const auto& Part:A->Parts)if(Part.Role.Contains(TEXT("body"))&&Part.Comp&&Part.Comp->IsVisible()){Target=Part.Comp->Bounds.Origin;break;}
   FRotator Rotation;PC->GetPlayerViewPoint(Origin,Rotation);if(A->StudioCam)Origin=A->StudioCam->GetActorLocation();const double Travel=FVector::Dist(Target,Origin)/S->Settings.JetSpeed;Direction=(Target+FVector(0,0,490*Travel*Travel)-Origin).GetSafeNormal();S->Settings.ScatterDegrees=.15f;S->SetSpray(true,Origin+Direction*20,Direction);
   if(Age>3){S->SetSpray(false,Origin,Direction);Deposited=S->Deposited;Hits=S->HitCount;if(Deposited<=0||Hits<=0){Stats=FString::Printf(TEXT("origin=%s direction=%s target=%s %s"),*Origin.ToString(),*Direction.ToString(),*Target.ToString(),*S->Statistics());Stop(TEXT("Watergun did not hit a real loaded character"));return;}if(FMath::Abs(S->ConservationError())>1.e-7){Stop(TEXT("Paint mass not conserved"));return;}FScreenshotRequest::RequestScreenshot(Output/TEXT("Painted.png"),false,false);A->SetPoseMode(1);Stage=3;}return;}
  if(Stage==3&&Age>6){Stats=S->Statistics();FScreenshotRequest::RequestScreenshot(Output/TEXT("Animated.png"),false,false);Stage=4;return;}
  if(Stage==4&&Age>7){PC->PaintAimHeld=true;G->OnBackToGallery();if(PC->IsPaintPreviewActive()||PC->PaintAimHeld||S->RemainingVolume()!=0||S->IsActorTickEnabled()){Stop(TEXT("Returning to gallery did not reset watergun"));return;}G->SetModelLayout(true);if(!PC->IsPaintPreviewActive()||PC->PaintSprayer!=S){Stop(TEXT("Reopening 3D duplicated/lost watergun"));return;}FScreenshotRequest::RequestScreenshot(Output/TEXT("Cleared.png"),false,false);Stage=5;return;}
  if(Stage==5&&Age>8){G->OnLoad3D();if(PC->IsPaintPreviewActive()||S->RemainingVolume()!=0){Stop(TEXT("Reload failed to suspend watergun"));return;}Stage=6;return;}
 }
};
}
static FAutoConsoleCommandWithWorld PaintGalleryAudit(TEXT("ok.PaintGalleryAudit"),TEXT("Real KKS gallery load/spray/animation/clear/reload audit; OK_PAINT_CARD/ROOT/OUTPUT"),FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* W){const FString Card=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_PAINT_CARD")),Root=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_PAINT_ROOT")),Output=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_PAINT_OUTPUT"));if(!W||Card.IsEmpty()||Root.IsEmpty()||Output.IsEmpty()){FPlatformMisc::RequestExitWithStatus(false,1);return;}IFileManager::Get().MakeDirectory(*Output,true);UKKGameRoot::Get()->Root=Root;auto Run=MakeShared<OKPaintGalleryAudit::FRun>();Run->World=W;Run->Card=Card;Run->Output=Output;W->GetTimerManager().SetTimer(Run->Timer,FTimerDelegate::CreateLambda([Run](){Run->Tick();}),.05f,true);}));
#endif
