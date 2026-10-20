#include "OKSceneDocument.h"
#include "KKRuntimeSharedMaterial.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/StaticMeshComponent.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "ImageUtils.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "TextureResource.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
// Measure transmission against two backgrounds: the unknown source shader RGB
// cancels. This checks the saved parents and GPU blending, not a duplicate alpha formula.
void RunStudioTransparencyTest(bool Exit)
{
#if WITH_EDITOR
    UWorld* World = nullptr;
    if (GEngine) for (const FWorldContext& C : GEngine->GetWorldContexts())
        if (C.WorldType == EWorldType::Game || C.WorldType == EWorldType::PIE) { World = C.World(); break; }
    if (!World) { UE_LOG(LogTemp, Error, TEXT("OK studio transparency test: RESULT FAIL no world")); if (Exit) FPlatformMisc::RequestExit(false); return; }
    struct FProbe
    {
        TWeakObjectPtr<AStaticMeshActor> Back, Front;
        TStrongObjectPtr<UMaterial> Surface;
        TStrongObjectPtr<UMaterial> AlphaControl;
        TStrongObjectPtr<UMaterialInstanceDynamic> Background, OpaqueControl;
        TArray<TStrongObjectPtr<UMaterialInstanceDynamic>> Parents;
        TStrongObjectPtr<UTexture2D> White, Clear, Half;
        int32 Frames = 0;
        int32 Phase = 0, Case = 0;
        bool Passed = true;
        FLinearColor Low, High;
    };
    auto Probe = MakeShared<FProbe>();
    auto Texture = [](FColor Color)
    {
        auto* T = UTexture2D::CreateTransient(1, 1, PF_B8G8R8A8);
        T->SRGB = false; auto& M = T->GetPlatformData()->Mips[0];
        void* Data = M.BulkData.Lock(LOCK_READ_WRITE); FMemory::Memcpy(Data, &Color, 4); M.BulkData.Unlock(); T->UpdateResource(); return T;
    };
    Probe->White.Reset(Texture(FColor::White)); Probe->Clear.Reset(Texture(FColor(0,0,0,0))); Probe->Half.Reset(Texture(FColor(255,255,255,128)));
    auto* Surface = NewObject<UMaterial>(); Surface->SetShadingModel(MSM_Unlit);
    auto* Color = NewObject<UMaterialExpressionVectorParameter>(Surface); Color->ParameterName = TEXT("Background"); Color->Material = Surface;
    Surface->GetExpressionCollection().AddExpression(Color); Surface->GetEditorOnlyData()->EmissiveColor.Expression = Color; Surface->PostEditChange();
    Probe->Surface.Reset(Surface); Probe->Background.Reset(UMaterialInstanceDynamic::Create(Surface, GetTransientPackage()));
    Probe->OpaqueControl.Reset(UMaterialInstanceDynamic::Create(Surface, GetTransientPackage()));
    Probe->OpaqueControl->SetVectorParameterValue(TEXT("Background"), FLinearColor(.1f,.1f,.1f));
    auto* Alpha=DuplicateObject<UMaterial>(Surface,GetTransientPackage()); Alpha->BlendMode=BLEND_Translucent; Alpha->TranslucencyPass=MTP_BeforeDOF;
    auto* HalfAlpha=NewObject<UMaterialExpressionConstant>(Alpha); HalfAlpha->R=.5f; HalfAlpha->Material=Alpha;
    Alpha->GetExpressionCollection().AddExpression(HalfAlpha); Alpha->GetEditorOnlyData()->Opacity.Expression=HalfAlpha;
    Alpha->PostEditChange(); Probe->AlphaControl.Reset(Alpha);
    Probe->Back = World->SpawnActor<AStaticMeshActor>(FVector(400,0,0), FRotator::ZeroRotator);
    Probe->Front = World->SpawnActor<AStaticMeshActor>(FVector(100,0,0), FRotator(0,0,90));
    for (auto Actor : {Probe->Back, Probe->Front})
    {
        Actor->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
        Actor->GetStaticMeshComponent()->SetForceDisableNanite(true);
    }
    Probe->Back->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));
    Probe->Back->SetActorScale3D(FVector(1,40,40)); Probe->Back->GetStaticMeshComponent()->SetMaterial(0, Probe->Background.Get());
    // Thin cube: culled eye/front-only glass programs draw one face; the six
    // CULLOFF glass variants draw both faces, each with independent source alpha.
    Probe->Front->SetActorRotation(FRotator::ZeroRotator); Probe->Front->SetActorScale3D(FVector(.01f,40,40));
    Probe->Front->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));
    for (const TCHAR* Root : {TEXT("StreetDisplay26"), TEXT("StudioDisplay26"), TEXT("StreetHG05")})
    {
        TArray<FString> Names;
        for (int32 I=0; I<12; ++I) Names.Add(FString::Printf(TEXT("M_KoikanoGlasses%d"),I));
        for (const TCHAR* Eye : {TEXT("M_KoikanoEye"),TEXT("M_KoikanoEyeWhite"),TEXT("M_ForgeEye"),TEXT("M_ForgeEyeWhiteLod")}) Names.Add(Eye);
        for (const FString& Name : Names)
        {
            const FString Path=FString(TEXT("/Game/IndependentResources/RuntimeMaterials/"))+Root+TEXT("/")+Name+TEXT(".")+Name;
            auto* Parent=LoadObject<UMaterialInterface>(nullptr,*Path);
            if (!Parent) { UE_LOG(LogTemp, Error, TEXT("OK studio transparency test: missing %s"),*Path); continue; }
            auto* MID=UMaterialInstanceDynamic::Create(Parent,GetTransientPackage());
            const bool Glass=Name.Contains(TEXT("Glasses"));
            FKKRuntimeMaterialPart Part; Part.Blend=TEXT("translucent"); Part.Unlit=true;
            FString Shader, Keywords;
            if (Glass)
            {
                const int32 V=FCString::Atoi(*Name.Mid(FString(TEXT("M_KoikanoGlasses")).Len()));
                Shader=TEXT("Koikano/main_clothes_item_glasses");
                Keywords=V%6/2==2?TEXT("\"_MATCAPSETTING_LINEAR\""):V%6/2==1?TEXT("\"_MATCAPSETTING_MULTIPLY\""):TEXT("\"_MATCAPSETTING_ADD\"");
                if (V%2) Keywords+=TEXT(",\"_MATCAP_ON\"");
                if (V>=6) Keywords+=TEXT(",\"_CULLOFF_ON\"");
            }
            else
            {
                Shader=Name==TEXT("M_KoikanoEye")?TEXT("Koikano/main_eye"):Name==TEXT("M_KoikanoEyeWhite")?TEXT("Koikano/main_eyew"):
                    Name==TEXT("M_ForgeEye")?TEXT("Shader Forge/toon_eye_lod0"):TEXT("Shader Forge/toon_eyew_lod0");
                Part.EyeDraw=Name==TEXT("M_KoikanoEye")?1:Name==TEXT("M_ForgeEye")?2:0;
                for (const TCHAR* N : {TEXT("_OKEyeUVPrepared"),TEXT("_OKEyeExpressionUVPrepared"),TEXT("_EyeHighlight")}) Part.Values.Add(N,FVector4f(1,0,0,0));
                Part.Values.Add(TEXT("_EyeColor"),FVector4f(1,1,1,1)); Part.Values.Add(TEXT("_MainTex_ST"),FVector4f(1,1,0,0));
                for (const TCHAR* N : {TEXT("_EyeLayerColor0"),TEXT("_EyeLayerColor1")}) Part.Values.Add(N,FVector4f(0,0,0,0));
            }
            Part.SourceMetadataJson=FString::Printf(TEXT("{\"schema\":1,\"material\":{\"shader\":\"%s\",\"keywords\":[%s]}}"),*Shader,*Keywords);
            FKKRuntimeMaterialBinding Binding; FString Error;
            if (!OKPrepareRuntimeSharedMaterialBinding(Part,FKKRuntimeMaterialScene(),Binding,Error))
            { UE_LOG(LogTemp,Error,TEXT("OK studio transparency test: binding %s: %s"),*Name,*Error); continue; }
            for (const auto& V : Binding.Vectors) MID->SetVectorParameterValue(V.Key,V.Value);
            for (const auto& V : Binding.Scalars) MID->SetScalarParameterValue(V.Key,V.Value);
            MID->SetScalarParameterValue(TEXT("__OK_RuntimeRevealOverride"),1.f);
            MID->SetTextureParameterValue(TEXT("_MainTex"),Glass?Probe->White.Get():Probe->Half.Get());
            for (const TCHAR* N : {TEXT("_overtex1"),TEXT("_overtex2"),TEXT("_expression")}) MID->SetTextureParameterValue(N,Probe->Clear.Get());
            MID->SetScalarParameterValue(TEXT("_Color4__A"),0.5f); MID->SetScalarParameterValue(TEXT("_Float3"),0.f);
            MID->SetScalarParameterValue(TEXT("_Cutoff"),0.f);
            Probe->Parents.Emplace(MID);
        }
    }
    FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Probe,Exit](float)
    {
        bool Ready=!Probe->Surface->IsCompiling() && !Probe->AlphaControl->IsCompiling();
        for (const auto& M : Probe->Parents) Ready &= !M->GetMaterial()->IsCompiling();
        if (++Probe->Frames<1800 && !Ready) return true;
        bool& Passed=Probe->Passed; Passed &= Ready && Probe->Parents.Num()==48;
        auto Picture=[&](float Value, const FString& Name)
        {
            Probe->Background->SetVectorParameterValue(TEXT("Background"),FLinearColor(Value,Value,Value));
            auto* Target=NewObject<UTextureRenderTarget2D>(); Target->RenderTargetFormat=RTF_RGBA16f; Target->InitAutoFormat(64,64); Target->UpdateResourceImmediate(true);
            auto* C=NewObject<USceneCaptureComponent2D>(Probe->Back.Get()); C->RegisterComponentWithWorld(Probe->Back->GetWorld());
            C->SetWorldTransform(FTransform::Identity); C->FOVAngle=60.f;
            C->CaptureSource=SCS_FinalColorHDR; C->TextureTarget=Target; C->bCaptureEveryFrame=C->bCaptureOnMovement=false; C->bAlwaysPersistRenderingState=true;
            FOKScenePost Post; Post.bBloom=false; Post.ApplyTo(C->PostProcessSettings); C->PostProcessBlendWeight=1.f;
            C->CaptureScene(); TArray<FLinearColor> Pixels; FReadSurfaceDataFlags Flags(RCM_MinMax); Flags.SetLinearToGamma(false);
            Passed &= Target->GameThread_GetRenderTargetResource()->ReadLinearColorPixels(Pixels,Flags) && Pixels.Num()==4096; C->DestroyComponent();
            if (Pixels.Num()!=4096) return FLinearColor::Black;
            TArray<FColor> Display; for (auto P : Pixels) { P.A=1; Display.Add(P.ToFColorSRGB()); }
            FString OutputRoot; FParse::Value(FCommandLine::Get(),TEXT("OKStudioCompareOutput="),OutputRoot);
            const FString Out=(OutputRoot.IsEmpty()?FPaths::ProjectSavedDir()/TEXT("OKStudio"):OutputRoot)/TEXT("TransparencyTest");
            IFileManager::Get().MakeDirectory(*Out,true);
            TArray64<uint8> PNG; FImageUtils::PNGCompressImageArray(64,64,TArrayView64<const FColor>(Display.GetData(),Display.Num()),PNG);
            FFileHelper::SaveArrayToFile(PNG,*(Out/(Name+TEXT(".png")))); return Pixels[32*64+32];
        };
        if (Probe->Phase==0)
        {
            Probe->Front->SetActorHiddenInGame(true);
            Probe->Low=Picture(.2f,TEXT("background-low")); Probe->High=Picture(.8f,TEXT("background-high"));
            Passed &= Probe->High.R-Probe->Low.R>.55f;
            Probe->Front->SetActorHiddenInGame(false);
            Probe->Front->GetStaticMeshComponent()->SetMaterial(0,Probe->OpaqueControl.Get());
            ++Probe->Phase; return true;
        }
        if (Probe->Phase==1)
        {
            const auto Control=Picture(.8f,TEXT("opaque-control")); const bool Fixture=FMath::Abs(Control.R-.1f)<.025f;
            Passed &= Fixture;
            UE_LOG(LogTemp,Display,TEXT("OK studio transparency test: opaque fixture=%s %s"),*Control.ToString(),Fixture?TEXT("PASS"):TEXT("FAIL"));
            Probe->Front->GetStaticMeshComponent()->SetMaterial(0,Probe->AlphaControl.Get());
            ++Probe->Phase; return true;
        }
        if (Probe->Phase==2)
        {
            const auto A=Picture(.2f,TEXT("alpha-control-low")), B=Picture(.8f,TEXT("alpha-control-high"));
            const float Transmission=(B.R-A.R)/(Probe->High.R-Probe->Low.R);
            Passed &= FMath::Abs(Transmission-.5f)<.04f;
            UE_LOG(LogTemp,Display,TEXT("OK studio transparency test: constant-alpha control transmission=%f"),Transmission);
            if (!Probe->Parents.IsEmpty()) Probe->Front->GetStaticMeshComponent()->SetMaterial(0,Probe->Parents[0].Get());
            ++Probe->Phase; return true;
        }
        if (Probe->Case<Probe->Parents.Num())
        {
            const int32 I=Probe->Case; auto* M=Probe->Parents[I].Get();
            const FLinearColor A=Picture(.2f,FString::Printf(TEXT("%02d-low"),I)), B=Picture(.8f,FString::Printf(TEXT("%02d-high"),I));
            const float Transmission=(B.R-A.R)/(Probe->High.R-Probe->Low.R);
            const int32 Variant=I%16;
            const float Expected=Variant>=6 && Variant<12 ? .25f : .5f;
            const bool Blends=M->GetBlendMode()==BLEND_Translucent || M->GetBlendMode()==BLEND_AlphaComposite;
            const bool Match=Blends && FMath::Abs(Transmission-Expected)<.04f;
            Passed &= Match;
            UE_LOG(LogTemp,Display,TEXT("OK studio transparency test: %s transmission=%.6f expected=%.2f %s"),*M->Parent->GetPathName(),Transmission,Expected,Match?TEXT("PASS"):TEXT("FAIL"));
            if (++Probe->Case<Probe->Parents.Num()) Probe->Front->GetStaticMeshComponent()->SetMaterial(0,Probe->Parents[Probe->Case].Get());
            return true;
        }
        UE_LOG(LogTemp,Display,TEXT("OK studio transparency test: RESULT %s"),Passed?TEXT("PASS"):TEXT("FAIL"));
        Probe->Front->Destroy(); Probe->Back->Destroy(); if (Exit) FPlatformMisc::RequestExit(false); return false;
    }),.1f);
#else
    UE_LOG(LogTemp,Error,TEXT("OK studio transparency test: RESULT FAIL requires editor")); if (Exit) FPlatformMisc::RequestExit(false);
#endif
}
FAutoConsoleCommand StudioTransparencyCommand(TEXT("OKStudio.TransparencyTest"),TEXT("OKStudio.TransparencyTest [exit]: saved glasses/eye parents over two backgrounds, three shipping roots."),
    FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args){ RunStudioTransparencyTest(Args.Contains(TEXT("exit"))); }));
}
