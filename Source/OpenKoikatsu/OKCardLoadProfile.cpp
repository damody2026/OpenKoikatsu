#include "OKGalleryWidget.h"
#include "KKPreviewActor.h"
#include "KKCardReader.h"
#include "KKAnimatedPng.h"
#include "Components/Image.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Engine/Texture2D.h"
#include "HAL/IConsoleManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "ShaderCompiler.h"
#include "RenderingThread.h"
#include "TimerManager.h"
#include "UObject/UObjectIterator.h"

// Measurement only: exercise the same public entry points as the gallery.
namespace OKLoadProfile
{
struct FRun : TSharedFromThis<FRun>
{
    TWeakObjectPtr<UWorld> World;
    TWeakObjectPtr<UOKCharacterController> Gallery;
    TWeakObjectPtr<AKKPreviewActor> Actor;
    TArray<FString> Cards;
    TArray<TWeakObjectPtr<AActor>> Spawned;
    TArray<TSharedPtr<FJsonValue>> Results;
    TSharedPtr<FJsonObject> Current;
    FTimerHandle Timer;
    int32 Next=0,Total=6;
    uint64 BuildFrame=0;
    double Begin=0,ReadyStart=0;
    bool Waiting=false;
    bool ColdFiles=false;
    void Stop(const FString& Error)
    {
        const auto KeepAlive=AsShared();
        if(auto* W=World.Get())W->GetTimerManager().ClearTimer(Timer);
        if(!Error.IsEmpty()){UE_LOG(LogTemp,Error,TEXT("Card profile failed: %s"),*Error);FPlatformMisc::RequestExitWithStatus(false,1);return;}
        auto Root=MakeShared<FJsonObject>();Root->SetArrayField(TEXT("runs"),Results);
        Root->SetStringField(TEXT("conditions"),ColdFiles?TEXT("Fresh process, fresh card/builtin files created via NO_BUFFERING + WRITE_THROUGH; no gallery scan or isolated pre-read of test files. Shader/DDC retained; not a global OS cache purge or reboot. Other processes may touch files. Excludes engine startup."):TEXT("Development Editor game mode, real D3D12, empty GameRoot, OS/DDC caches not cleared, alternating static/APNG, GC between runs; excludes engine startup and gallery initial scan"));
        FString Json;FJsonSerializer::Serialize(Root,TJsonWriterFactory<>::Create(&Json));
        const FString Out=FPaths::ProjectSavedDir()/(ColdFiles?TEXT("APNGCards/load-profile-cold-files.json"):TEXT("APNGCards/load-profile.json"));
        FFileHelper::SaveStringToFile(Json,*Out);
        UE_LOG(LogTemp,Display,TEXT("Card profile PASS: %s"),*Out);FPlatformMisc::RequestExitWithStatus(false,0);
    }
    void Step()
    {
        const auto KeepAlive=AsShared();
        auto* W=World.Get();auto* G=Gallery.Get();
        if(!W||!G){Stop(TEXT("World/gallery closed"));return;}
        if(Waiting)
        {
            if(FPlatformTime::Seconds()-ReadyStart>120){Stop(TEXT("Shader/frame readiness timeout"));return;}
            if(GShaderCompilingManager&&GShaderCompilingManager->IsCompiling())return;
            if(GFrameCounter<BuildFrame+4)return;
            FlushRenderingCommands();
            Current->SetNumberField(TEXT("shader_and_frame_wait_ms"),(FPlatformTime::Seconds()-ReadyStart)*1000);
            Current->SetNumberField(TEXT("preview_plus_3d_ready_ms"),(FPlatformTime::Seconds()-Begin)*1000);
            UE_LOG(LogTemp,Display,TEXT("Card profile run=%d type=%s select=%.3f session=%.3f mesh=%.3f shape_camera=%.3f render_flush=%.3f total_cpu=%.3f ready=%.3f ms"),
                Next,*Current->GetStringField(TEXT("type")),Current->GetNumberField(TEXT("gallery_select_ms")),Current->GetNumberField(TEXT("session_ms")),
                Current->GetNumberField(TEXT("mesh_texture_material_ms")),Current->GetNumberField(TEXT("shape_camera_ms")),Current->GetNumberField(TEXT("render_flush_ms")),
                Current->GetNumberField(TEXT("preview_plus_3d_cpu_ms")),Current->GetNumberField(TEXT("preview_plus_3d_ready_ms")));
            Results.Add(MakeShared<FJsonValueObject>(Current));
            for(auto Weak:Spawned)if(auto* A=Weak.Get())A->Destroy();Spawned.Reset();Actor.Reset();
            CollectGarbage(RF_NoFlags);FlushRenderingCommands();Waiting=false;++Next;
            if(Next==Total)Stop(FString());
            return;
        }
        const int32 CardIndex=ColdFiles?1:Next%2;const FString Path=Cards[CardIndex];
        Current=MakeShared<FJsonObject>();Current->SetNumberField(TEXT("run"),Next);
        Current->SetStringField(TEXT("type"),CardIndex==0?TEXT("static"):TEXT("apng"));Current->SetStringField(TEXT("path"),Path);
        Current->SetNumberField(TEXT("file_bytes"),IFileManager::Get().FileSize(*Path));
        // Isolated parse/split estimates, outside the actual gallery timing.
        FString Error;FKKCharacterFile Card;
        if(!ColdFiles)
        {
        double At=FPlatformTime::Seconds();
        if(!UKKCardReader::LoadFromFile(Path,Card,Error)){Stop(Error);return;}
        Current->SetNumberField(TEXT("isolated_card_read_ms"),(FPlatformTime::Seconds()-At)*1000);
        TArray<FKKPngFrame> Frames;uint32 Plays;At=FPlatformTime::Seconds();
        const bool Animated=FKKAnimatedPng::Read(Card.Header.PreviewPng,Frames,Plays,Error);
        Current->SetNumberField(TEXT("isolated_apng_split_ms"),(FPlatformTime::Seconds()-At)*1000);
        if(CardIndex==1&&(!Animated||Frames.Num()!=4)){Stop(Error);return;}
        Current->SetNumberField(TEXT("preview_frames"),Animated?Frames.Num():1);
        }
        G->Entries.Reset();FKKCardListEntry Entry;Entry.Path=Path;Entry.FileName=FPaths::GetCleanFilename(Path);G->Entries.Add(Entry);
        FlushRenderingCommands();Begin=FPlatformTime::Seconds();G->SelectIndex(0);
        const double Selected=FPlatformTime::Seconds();Current->SetNumberField(TEXT("gallery_select_ms"),(Selected-Begin)*1000);
        FlushRenderingCommands();const double PreviewReady=FPlatformTime::Seconds();
        Current->SetNumberField(TEXT("gallery_preview_flush_ms"),(PreviewReady-Selected)*1000);
        // The first frame is decoded on selection, the rest on a worker (measured above); complete them for the check.
        G->DecodeRemainingPreviewFramesNow();
        if(CardIndex==1&&G->AnimatedPreviewTextures.Num()!=4){Stop(TEXT("Gallery preview failed"));return;}
        if(ColdFiles)Current->SetNumberField(TEXT("preview_frames"),G->AnimatedPreviewTextures.Num());
        TSet<AActor*> Before;for(TActorIterator<AActor> It(W);It;++It)Before.Add(*It);
        AKKPreviewActor* A=W->SpawnActor<AKKPreviewActor>();Actor=A;
        const double SessionStart=FPlatformTime::Seconds();
        if(!A||!A->Session.Open(FString(),Path,0,Error)){Stop(Error);return;}
        const double SessionEnd=FPlatformTime::Seconds();
        if(!A->LoadSession(Error)){Stop(Error);return;}
        const double MeshEnd=FPlatformTime::Seconds();
        A->ApplyShapes(G->Selected.Face.ShapeValueFace,G->Selected.Body.ShapeValueBody);A->SetupStudioView();A->FrameCurrentPose();
        A->SetActorTickEnabled(false);const double ShapeEnd=FPlatformTime::Seconds();FlushRenderingCommands();const double Flushed=FPlatformTime::Seconds();
        for(TActorIterator<AActor> It(W);It;++It)if(!Before.Contains(*It))Spawned.Add(*It);
        Current->SetNumberField(TEXT("session_ms"),(SessionEnd-SessionStart)*1000);
        Current->SetNumberField(TEXT("mesh_texture_material_ms"),(MeshEnd-SessionEnd)*1000);
        Current->SetNumberField(TEXT("shape_camera_ms"),(ShapeEnd-MeshEnd)*1000);
        Current->SetNumberField(TEXT("render_flush_ms"),(Flushed-ShapeEnd)*1000);
        Current->SetNumberField(TEXT("3d_cpu_ms"),(Flushed-SessionStart)*1000);
        Current->SetNumberField(TEXT("preview_plus_3d_cpu_ms"),(Flushed-Begin)*1000);
        Current->SetNumberField(TEXT("parts"),A->Parts.Num());
        BuildFrame=GFrameCounter;ReadyStart=Flushed;Waiting=true;
    }
};
}
static FAutoConsoleCommandWithWorld ProfilePortableCards(TEXT("ok.ProfilePortableCards"),
    TEXT("Alternating static/APNG load profiles; paths from OK_PROFILE_STATIC_CARD / OK_APNG_OUTPUT"),
    FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
    {
        UOKCharacterController* Gallery=nullptr;
        for(TObjectIterator<UOKCharacterController> It;It;++It)if(It->GetWorld()==World&&It->PreviewImage){Gallery=*It;break;}
        auto Run=MakeShared<OKLoadProfile::FRun>();Run->World=World;Run->Gallery=Gallery;
        Run->ColdFiles=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_PROFILE_COLD_FILES"))==TEXT("1");
        if(Run->ColdFiles)Run->Total=1;
        Run->Cards={FPlatformMisc::GetEnvironmentVariable(TEXT("OK_PROFILE_STATIC_CARD")),FPlatformMisc::GetEnvironmentVariable(TEXT("OK_APNG_OUTPUT"))};
        if(!World||!Gallery||Run->Cards[0].IsEmpty()||Run->Cards[1].IsEmpty()){Run->Stop(TEXT("Missing gallery/card paths"));return;}
        World->GetTimerManager().SetTimer(Run->Timer,FTimerDelegate::CreateLambda([Run](){Run->Step();}),.1f,true);
    }));
