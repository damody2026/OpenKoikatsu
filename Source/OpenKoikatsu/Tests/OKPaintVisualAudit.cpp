#include "../OKPaintDemo.h"
#include "../OKPaintSprayer.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "Engine/GameViewportClient.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HighResScreenshot.h"
#include "Camera/CameraActor.h"
#include "GameFramework/PlayerController.h"
#if WITH_DEV_AUTOMATION_TESTS
namespace OKPaintAudit {
struct FRun:TSharedFromThis<FRun>{TWeakObjectPtr<UWorld> World;TWeakObjectPtr<AOKPaintSprayer> Spray;TWeakObjectPtr<AOKPaintDemoCharacter> Person;FTimerHandle Timer;int32 Step=0;bool Initialized=false;double Started=0,WallStart=0;uint64 FirstFrame=0;TArray<double> Samples;
void Tick(){auto* W=World.Get();if(!W)return;if(!Spray.IsValid())for(TActorIterator<AOKPaintSprayer> It(W);It;++It){Spray=*It;break;}if(!Person.IsValid())for(TActorIterator<AOKPaintDemoCharacter> It(W);It;++It){Person=*It;break;}if(!Spray.IsValid()||!Person.IsValid()){if(++Step>100){FPlatformMisc::RequestExitWithStatus(false,1);}return;}
 if(!Initialized){Initialized=true;Started=W->GetTimeSeconds();WallStart=FPlatformTime::Seconds();FirstFrame=GFrameCounter;if(auto* PC=W->GetFirstPlayerController()){PC->SetActorTickEnabled(false);auto* Camera=W->SpawnActor<ACameraActor>(FVector(-140,-190,130),(FVector(0,0,100)-FVector(-140,-190,130)).Rotation());PC->SetViewTarget(Camera);}Person->Time=0;Person->Animate=false;Person->Tick(0);Spray->Settings.Flow=12;Spray->Settings.ScatterDegrees=1;}
 const double Age=W->GetTimeSeconds()-Started;Spray->SetSpray(Age<4,FVector(-150,0,110),FVector(1,0,0));Samples.Add(Spray->LastUpdateMs);
 if(Age>2&&Step==0){FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("Tests/PaintSpray396/Painted.png"),false,false);Step=1;}
 if(Age>4&&!Person->Animate){Person->Time=8;Person->Animate=true;}
 if(Age>9&&Step==1){FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("Tests/PaintSpray396/Flowing.png"),false,false);Step=2;}
 if(Age>12&&Step==2){FString Report=FString::Printf(TEXT("valid=%d emitted=%.9f error=%.12f frames=%llu wall_seconds=%.3f measured_fps=%.3f samples=%llu mean_update_ms=%.3f peak_update_ms=%.3f\n%s\n"),Spray->Deposited>0&&FMath::Abs(Spray->ConservationError())<1.e-7,Spray->Emitted,Spray->ConservationError(),GFrameCounter-FirstFrame,FPlatformTime::Seconds()-WallStart,(GFrameCounter-FirstFrame)/(FPlatformTime::Seconds()-WallStart),Spray->UpdateSamples,Spray->TotalUpdateMs/FMath::Max(uint64(1),Spray->UpdateSamples),Spray->MaxUpdateMs,*Spray->Statistics());FFileHelper::SaveStringToFile(Report,*(FPaths::ProjectSavedDir()/TEXT("Tests/PaintSpray396/VisualAudit.txt")));UE_LOG(LogTemp,Display,TEXT("%s"),*Report);Spray->ClearPaint();FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("Tests/PaintSpray396/Cleared.png"),false,false);Step=3;}
 if(Age>13&&Step==3){W->GetTimerManager().ClearTimer(Timer);FPlatformMisc::RequestExitWithStatus(false,Spray->RemainingVolume()==0?0:1);}
}};
}
static FAutoConsoleCommandWithWorld PaintAudit(TEXT("ok.PaintAudit"),TEXT("Rendered PaintDemo audit; writes PaintSpray396 screenshots and measurements"),FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* W){if(!W)return;auto Run=MakeShared<OKPaintAudit::FRun>();Run->World=W;W->GetTimerManager().SetTimer(Run->Timer,FTimerDelegate::CreateLambda([Run](){Run->Tick();}),.05f,true);}));
#endif
