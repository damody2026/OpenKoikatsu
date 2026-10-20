#include "KKPreviewActor.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/GameViewportClient.h"
#include "ShaderCompiler.h"
#include "TimerManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"

// Local acceptance tool. Only a converted pack path is accepted, no game root.
static void VerifyIndependentPreview()
{
    UWorld* World=nullptr;
    for(const FWorldContext& Context:GEngine->GetWorldContexts())
        if(Context.WorldType==EWorldType::Game||Context.WorldType==EWorldType::PIE) {World=Context.World();break;}
    if(!World) {UE_LOG(LogTemp,Error,TEXT("Independent preview: no game world"));return;}
    const FString Pack=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_TEST_PREVIEW_PACK"));
    AKKPreviewActor* Actor=World->SpawnActor<AKKPreviewActor>();FString Error;
    const double Start=FPlatformTime::Seconds();
    const bool Opened=Actor && Actor->Session.Open(FString(),Pack,0,Error);
    const double SessionEnd=FPlatformTime::Seconds();
    const bool Loaded=Opened && Actor->LoadSession(Error);
    const double MeshEnd=FPlatformTime::Seconds();
    if(!Loaded)
    {UE_LOG(LogTemp,Error,TEXT("Independent preview failed: %s"),*Error);FPlatformMisc::RequestExitWithStatus(false,1);return;}
    Actor->ApplyShapes(Actor->Session.ShapeFace(),Actor->Session.ShapeBody());
    Actor->SetupStudioView();
    Actor->FrameCurrentPose();
    UE_LOG(LogTemp,Display,TEXT("Independent profile session=%.6f mesh_textures_materials=%.6f shape_camera=%.6f"),
        SessionEnd-Start,MeshEnd-SessionEnd,FPlatformTime::Seconds()-MeshEnd);
    UE_LOG(LogTemp,Display,TEXT("Independent preview built parts=%d seconds=%.6f game_root=empty"),Actor->Parts.Num(),FPlatformTime::Seconds()-Start);
    const FString Folder=FPaths::ProjectSavedDir()/TEXT("IndependentCards");IFileManager::Get().MakeDirectory(*Folder,true);
    auto Step=MakeShared<int32>(0);auto Timer=MakeShared<FTimerHandle>();TWeakObjectPtr<AKKPreviewActor> Weak(Actor);
    World->GetTimerManager().SetTimer(*Timer,FTimerDelegate::CreateLambda([Weak,Step,Timer,World,Folder]()
    {
        AKKPreviewActor* Preview=Weak.Get();if(!Preview){FPlatformMisc::RequestExitWithStatus(false,1);return;}
        if(GShaderCompilingManager&&GShaderCompilingManager->IsCompiling()) return;
        if(*Step<4){++*Step;return;}
        if(*Step==4){Preview->FrameCurrentPose();FScreenshotRequest::RequestScreenshot(Folder/TEXT("portable_baseline.png"),false,false);++*Step;return;}
        if(*Step==6){TArray<float> Body=Preview->Session.ShapeBody();Body[0]=1.f;Preview->ApplyShapes(Preview->Session.ShapeFace(),Body);Preview->FrameCurrentPose();}
        if(*Step==9) FScreenshotRequest::RequestScreenshot(Folder/TEXT("portable_tall.png"),false,false);
        if(*Step==12){World->GetTimerManager().ClearTimer(*Timer);FPlatformMisc::RequestExit(false);return;}
        ++*Step;
    }),1.f,true);
}
static FAutoConsoleCommand IndependentPreviewCommand(TEXT("ok.VerifyIndependentPreview"),
    TEXT("Load OK_TEST_PREVIEW_PACK without a game root and capture baseline/tall previews"),
    FConsoleCommandDelegate::CreateStatic(&VerifyIndependentPreview));
