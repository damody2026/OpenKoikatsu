#include "OKStudioBloom.h"

#include "OKSceneActor.h"
#include "OKSceneCard.h"
#include "OKSceneDocument.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/Scene.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "ImageUtils.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UObject/StrongObjectPtr.h"
#include "TextureResource.h"

namespace
{
void RunBloomTest(bool bExit)
{
#if WITH_EDITOR
    UWorld* World = nullptr;
    if (GEngine) for (const FWorldContext& Context : GEngine->GetWorldContexts())
        if (Context.WorldType == EWorldType::Game || Context.WorldType == EWorldType::PIE) { World = Context.World(); break; }
    if (!World) { UE_LOG(LogTemp, Error, TEXT("OK studio bloom test: RESULT FAIL no game world")); if (bExit) FPlatformMisc::RequestExit(false); return; }
    struct FState
    {
        TWeakObjectPtr<AOKSceneActor> Scene;
        TWeakObjectPtr<AStaticMeshActor> Cube;
        TStrongObjectPtr<UMaterial> Surface;
        int32 Frames = 0;
        int32 Warm = 0;
    };
    TSharedRef<FState> State = MakeShared<FState>();
    State->Scene = World->SpawnActor<AOKSceneActor>();
    State->Cube = World->SpawnActor<AStaticMeshActor>(FVector(100, 0, 0), FRotator::ZeroRotator);
    State->Cube->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
    State->Cube->SetActorScale3D(FVector(1, 40, 40));
    State->Cube->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
    UMaterial* Surface = NewObject<UMaterial>(GetTransientPackage(), NAME_None, RF_Transient);
    Surface->SetShadingModel(MSM_Unlit);
    auto* Color = NewObject<UMaterialExpressionVectorParameter>(Surface);
    Color->Material = Surface; Color->ParameterName = TEXT("Color"); Color->DefaultValue = FLinearColor(0.1f, 0.4f, 0.7f);
    Surface->GetExpressionCollection().AddExpression(Color);
    Surface->GetEditorOnlyData()->EmissiveColor.Expression = Color;
    Surface->PreEditChange(nullptr); Surface->PostEditChange();
    State->Surface.Reset(Surface);
    State->Cube->GetStaticMeshComponent()->SetMaterial(0, Surface);
    FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([State, bExit](float)
    {
        FOKScenePost Post; Post.bBloom = true; Post.BloomThreshold = 0.3f; Post.BloomIntensity = 0.25f;
        FPostProcessSettings A;
        Post.ApplyTo(A);
        const FString Status = OKStudioSourceBloomState();
        if (++State->Frames < 1800 && (Status != TEXT("ready") || State->Surface->IsCompiling())) return true;
        bool Passed = Status == TEXT("ready") && A.BloomIntensity == 0.f && A.WeightedBlendables.Array.Num() == 8;
        const FString Out = FPaths::ProjectSavedDir() / TEXT("OKStudio/BloomTest");
        IFileManager::Get().MakeDirectory(*Out, true);
        auto Picture = [&](const FPostProcessSettings& Settings, const TCHAR* Name)
        {
            // Float HDR readback avoids testing the display tonemapper's LUT / sRGB quantization as if it were bloom.
            auto* Target = NewObject<UTextureRenderTarget2D>(State->Scene.Get());
            Target->RenderTargetFormat = RTF_RGBA16f; Target->InitAutoFormat(128, 72); Target->UpdateResourceImmediate(true);
            auto* Capture = NewObject<USceneCaptureComponent2D>(State->Scene.Get());
            Capture->RegisterComponentWithWorld(State->Scene->GetWorld());
            Capture->SetWorldTransform(FTransform::Identity); Capture->FOVAngle = 60.f;
            Capture->CaptureSource = SCS_FinalColorHDR; Capture->TextureTarget = Target;
            Capture->bCaptureEveryFrame = Capture->bCaptureOnMovement = false;
            Capture->bAlwaysPersistRenderingState = true;
            Capture->PostProcessSettings = Settings; Capture->PostProcessBlendWeight = 1.f;
            Capture->CaptureScene();
            TArray<FLinearColor> LinearPixels;
            FReadSurfaceDataFlags Flags(RCM_MinMax); Flags.SetLinearToGamma(false);
            const bool Captured = Target->GameThread_GetRenderTargetResource()->ReadLinearColorPixels(LinearPixels, Flags);
            Capture->DestroyComponent();
            Passed &= Captured && LinearPixels.Num() == 128 * 72;
            if (LinearPixels.Num() != 128 * 72) return FLinearColor::Black;
            TArray<FColor> Pixels; Pixels.Reserve(LinearPixels.Num());
            for (FLinearColor& Pixel : LinearPixels) { Pixel.A = 1.f; Pixels.Add(Pixel.ToFColorSRGB()); }
            TArray64<uint8> Png;
            FImageUtils::PNGCompressImageArray(128, 72, TArrayView64<const FColor>(Pixels.GetData(), Pixels.Num()), Png);
            FFileHelper::SaveArrayToFile(Png, *(Out / (FString(Name) + TEXT(".png"))));
            return LinearPixels[36 * 128 + 64];
        };
        if (Passed && State->Warm++ < 3) { Picture(A, TEXT("warm")); return true; }
        Post.bBloom = false; FPostProcessSettings Off; Post.ApplyTo(Off);
        const FLinearColor Base = Picture(Off, TEXT("off"));
        Passed &= Base.Equals(FLinearColor(0.1f, 0.4f, 0.7f), 0.025f);
        auto Expected = [&](float Intensity)
        {
            return FLinearColor(Base.R + Intensity * FMath::Max(Base.R - 0.3f, 0.f),
                Base.G + Intensity * FMath::Max(Base.G - 0.3f, 0.f), Base.B + Intensity * FMath::Max(Base.B - 0.3f, 0.f));
        };
        Post.bBloom = true; Post.BloomIntensity = 2.f;
        FPostProcessSettings B; Post.ApplyTo(B);
        const FLinearColor Strong = Picture(B, TEXT("strong"));
        const FLinearColor Normal = Picture(A, TEXT("normal")); // A must retain its own .25 after B was configured.
        const bool StrongMatches = Strong.Equals(Expected(2.f), 0.015f) && Strong.B > 1.4f;
        const bool NormalMatches = Normal.Equals(Expected(0.25f), 0.015f);
        Passed &= StrongMatches && NormalMatches;
        Post.bBloom = false; Post.ApplyTo(A);
        const FLinearColor Disabled = Picture(A, TEXT("disabled"));
        Passed &= A.BloomIntensity == 0.f && A.WeightedBlendables.Array.IsEmpty() && Disabled.Equals(Base, 0.01f);
        UE_LOG(LogTemp, Display, TEXT("OK studio bloom test: linear base=%s normal=%s expected=%s strong=%s expected=%s disabled=%s"),
            *Base.ToString(), *Normal.ToString(), *Expected(0.25f).ToString(), *Strong.ToString(), *Expected(2.f).ToString(), *Disabled.ToString());
        UE_LOG(LogTemp, Display, TEXT("OK studio bloom test: state=%s channel_threshold=%d camera_isolation=%d"), *Status, StrongMatches ? 1 : 0, NormalMatches ? 1 : 0);
        UE_LOG(LogTemp, Display, TEXT("OK studio bloom test: RESULT %s"), Passed ? TEXT("PASS") : TEXT("FAIL"));
        if (State->Cube.IsValid()) State->Cube->Destroy();
        if (State->Scene.IsValid()) State->Scene->Destroy();
        if (bExit) FPlatformMisc::RequestExit(false);
        return false;
    }), 0.1f);
#else
    UE_LOG(LogTemp, Error, TEXT("OK studio bloom test: RESULT FAIL requires editor graph support"));
    if (bExit) FPlatformMisc::RequestExit(false);
#endif
}
FAutoConsoleCommand BloomTest(TEXT("OKStudio.BloomTest"), TEXT("OKStudio.BloomTest [exit]: GPU channel threshold, constant-field kernel, camera isolation and effect disable checks."),
    FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args) { RunBloomTest(Args.Contains(TEXT("exit"))); }));
}
