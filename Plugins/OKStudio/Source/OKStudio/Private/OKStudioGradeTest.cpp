#include "OKStudioBloom.h"
#include "Materials/MaterialInstanceDynamic.h"

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


namespace {
// Installed Amplify Color HDR/Linear, tonemapper disabled, Lut_Amount at 100%.
// Values captured by WriteStudioGradeSamples; no scene/camera/image fitting.
const float NativeGrade[][6] = {
    {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f},
    {0.00560539169f, 0.00560539169f, 0.00560539169f, 0.0f, 0.0f, 0.0f},
    {0.0159962922f, 0.0159962922f, 0.0159962922f, 0.0f, 0.0f, 0.0f},
    {0.0331047624f, 0.0331047624f, 0.0331047624f, 0.0f, 0.0f, 0.0f},
    {0.0578054339f, 0.0578054339f, 0.0578054339f, 0.0f, 0.0f, 0.0f},
    {0.0908417329f, 0.0908417329f, 0.0908417329f, 0.00413141679f, 0.000705284881f, 0.00498777768f},
    {0.132868335f, 0.132868335f, 0.132868335f, 0.0402530581f, 0.0187412426f, 0.0363237709f},
    {0.18447499f, 0.18447499f, 0.18447499f, 0.0939709544f, 0.0546122231f, 0.0816266239f},
    {0.246201396f, 0.246201396f, 0.246201396f, 0.161907345f, 0.108785808f, 0.14225623f},
    {0.318546832f, 0.318546832f, 0.318546832f, 0.252530068f, 0.186900511f, 0.227863848f},
    {0.401977867f, 0.401977867f, 0.401977867f, 0.348165244f, 0.281546921f, 0.334239274f},
    {0.496933043f, 0.496933043f, 0.496933043f, 0.468034565f, 0.398626149f, 0.455897152f},
    {0.603827417f, 0.603827417f, 0.603827417f, 0.596420586f, 0.5361799f, 0.595836639f},
    {0.723055243f, 0.723055243f, 0.723055243f, 0.748295784f, 0.689358771f, 0.748965561f},
    {0.854992688f, 0.854992688f, 0.854992688f, 0.898955047f, 0.872273624f, 0.898935437f},
    {1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f},
    {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f},
    {0.246201396f, 0.0f, 0.0f, 0.16908215f, 0.0f, 0.0f},
    {1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f},
    {0.0f, 0.0f, 0.246201396f, 0.0f, 0.0f, 0.158062115f},
    {0.246201396f, 0.0f, 0.246201396f, 0.16908215f, 0.0f, 0.152785912f},
    {1.0f, 0.0f, 0.246201396f, 1.0f, 0.0f, 0.107814081f},
    {0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f},
    {0.246201396f, 0.0f, 1.0f, 0.16908215f, 0.0f, 1.0f},
    {1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f},
    {0.0f, 0.246201396f, 0.0f, 0.0f, 0.122784369f, 0.0f},
    {0.246201396f, 0.246201396f, 0.0f, 0.158123478f, 0.115656957f, 0.0f},
    {1.0f, 0.246201396f, 0.0f, 1.0f, 0.084478505f, 0.0f},
    {0.0f, 0.246201396f, 0.246201396f, 0.0f, 0.122784369f, 0.148353741f},
    {0.246201396f, 0.246201396f, 0.246201396f, 0.161907345f, 0.108785808f, 0.14225623f},
    {1.0f, 0.246201396f, 0.246201396f, 1.0f, 0.084478505f, 0.100135989f},
    {0.0f, 0.246201396f, 1.0f, 0.0f, 0.122784369f, 1.0f},
    {0.246201396f, 0.246201396f, 1.0f, 0.145507008f, 0.0968498141f, 1.0f},
    {1.0f, 0.246201396f, 1.0f, 1.0f, 0.084478505f, 1.0f},
    {0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f},
    {0.246201396f, 1.0f, 0.0f, 0.110983044f, 1.0f, 0.0f},
    {1.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f},
    {0.0f, 1.0f, 0.246201396f, 0.0f, 1.0f, 0.100135989f},
    {0.246201396f, 1.0f, 0.246201396f, 0.110983044f, 1.0f, 0.100135989f},
    {1.0f, 1.0f, 0.246201396f, 1.0f, 1.0f, 0.100135989f},
    {0.0f, 1.0f, 1.0f, 0.0f, 1.0f, 1.0f},
    {0.246201396f, 1.0f, 1.0f, 0.110983044f, 1.0f, 1.0f},
    {1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f},
};
void RunGradeTest(bool Exit) {
#if WITH_EDITOR
 UWorld* World=nullptr;
 if(GEngine) for(const FWorldContext& C:GEngine->GetWorldContexts()) if(C.WorldType==EWorldType::Game||C.WorldType==EWorldType::PIE){World=C.World();break;}
 if(!World){UE_LOG(LogTemp,Error,TEXT("OK studio grade test: RESULT FAIL no world"));return;}
 struct StateType { TWeakObjectPtr<AOKSceneActor> Scene; TWeakObjectPtr<AStaticMeshActor> Cube; TStrongObjectPtr<UMaterial> Parent; TStrongObjectPtr<UMaterialInstanceDynamic> Surface; int Frames=0; };
 auto State=MakeShared<StateType>();State->Scene=World->SpawnActor<AOKSceneActor>();
 State->Cube=World->SpawnActor<AStaticMeshActor>(FVector(100,0,0),FRotator::ZeroRotator);
 auto* Comp=State->Cube->GetStaticMeshComponent();Comp->SetMobility(EComponentMobility::Movable);State->Cube->SetActorScale3D(FVector(1,40,40));
 Comp->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));
 auto* Mat=NewObject<UMaterial>();Mat->SetShadingModel(MSM_Unlit);
 auto* Color=NewObject<UMaterialExpressionVectorParameter>(Mat);Color->ParameterName=TEXT("Color");Mat->GetExpressionCollection().AddExpression(Color);Mat->GetEditorOnlyData()->EmissiveColor.Expression=Color;Mat->PreEditChange(nullptr);Mat->PostEditChange();State->Parent.Reset(Mat);
 State->Surface.Reset(UMaterialInstanceDynamic::Create(Mat,Comp));Comp->SetMaterial(0,State->Surface.Get());
 FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([State,Exit](float){
  if(++State->Frames<1800&&State->Parent->IsCompiling())return true;
  FOKScenePost Post;Post.bBloom=false;Post.bAmbientOcclusion=false;Post.bFog=false;Post.ColorFilter=0;Post.ColorFilterIntensity=1;
  FPostProcessSettings Settings;Post.ApplyTo(Settings);
  if(Settings.WeightedBlendables.Array.IsEmpty() && State->Frames<1800)return true;
  auto* Target=NewObject<UTextureRenderTarget2D>(State->Scene.Get());Target->RenderTargetFormat=RTF_RGBA16f;Target->InitAutoFormat(32,18);Target->UpdateResourceImmediate(true);
  auto* Capture=NewObject<USceneCaptureComponent2D>(State->Scene.Get());Capture->RegisterComponentWithWorld(State->Scene->GetWorld());Capture->SetWorldTransform(FTransform::Identity);Capture->FOVAngle=60;Capture->CaptureSource=SCS_FinalToneCurveHDR;Capture->TextureTarget=Target;Capture->bCaptureEveryFrame=Capture->bCaptureOnMovement=false;Capture->PostProcessSettings=Settings;Capture->PostProcessBlendWeight=1;
  Capture->bAlwaysPersistRenderingState=true;
  bool Pass=true;float MaxError=0,MaxDisplayError=0;TArray<FColor> Swatches;FString Report=TEXT("index,input_r,input_g,input_b,expected_r,expected_g,expected_b,actual_r,actual_g,actual_b,max_error,capture_r,capture_g,capture_b,display_error\n");
  for(int I=0;I<UE_ARRAY_COUNT(NativeGrade);++I){const float* V=NativeGrade[I];State->Surface->SetVectorParameterValue(TEXT("Color"),FLinearColor(V[0],V[1],V[2]));
   Capture->CaptureScene();TArray<FLinearColor> Pixels;FReadSurfaceDataFlags Flags(RCM_MinMax);Flags.SetLinearToGamma(false);
   Pass &= Target->GameThread_GetRenderTargetResource()->ReadLinearColorPixels(Pixels,Flags)&&Pixels.Num()==32*18;
   if(Pixels.Num()!=32*18)continue;const FLinearColor A=Pixels[9*32+16];float E=FMath::Max3(FMath::Abs(A.R-V[3]),FMath::Abs(A.G-V[4]),FMath::Abs(A.B-V[5]));MaxError=FMath::Max(MaxError,E);
   TArray<FColor> Captured;FString Error;
   Pass &= FOKSceneCard::Capture(State->Scene.Get(),FTransform::Identity,60,&Settings,32,18,false,Captured,Error);
   FColor Display=Captured.Num()==32*18?Captured[9*32+16]:FColor::Black;
   const FColor Expected=FLinearColor(V[3],V[4],V[5]).ToFColorSRGB();
   const float DisplayError=FMath::Max3(FMath::Abs(int(Display.R)-int(Expected.R)),FMath::Abs(int(Display.G)-int(Expected.G)),FMath::Abs(int(Display.B)-int(Expected.B)))/255.f;MaxDisplayError=FMath::Max(MaxDisplayError,DisplayError);
   Report+=FString::Printf(TEXT("%d,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%d,%d,%d,%.9g\n"),I,V[0],V[1],V[2],V[3],V[4],V[5],A.R,A.G,A.B,E,Display.R,Display.G,Display.B,DisplayError);Swatches.Add(Display);
  }
  // Without a filter (and no vignette) Studio's picture is ungraded: the capture must return its input.
  FOKScenePost Plain=Post;Plain.ColorFilter=-1;Plain.bVignette=false;FPostProcessSettings PlainSettings;Plain.ApplyTo(PlainSettings);
  float IdentityError=0,IdentityDisplayError=0;
  for(int I=0;I<UE_ARRAY_COUNT(NativeGrade);++I){const float* V=NativeGrade[I];State->Surface->SetVectorParameterValue(TEXT("Color"),FLinearColor(V[0],V[1],V[2]));
   TArray<FColor> Captured;FString Error;
   Pass &= FOKSceneCard::Capture(State->Scene.Get(),FTransform::Identity,60,&PlainSettings,32,18,false,Captured,Error);
   const FColor Display=Captured.Num()==32*18?Captured[9*32+16]:FColor::Black;const FColor Expected=FLinearColor(V[0],V[1],V[2]).ToFColorSRGB();
   const float E=FMath::Max3(FMath::Abs(int(Display.R)-int(Expected.R)),FMath::Abs(int(Display.G)-int(Expected.G)),FMath::Abs(int(Display.B)-int(Expected.B)))/255.f;
   IdentityDisplayError=FMath::Max(IdentityDisplayError,E);
   Report+=FString::Printf(TEXT("identity %d,%.9g,%.9g,%.9g,,,,,,,,%d,%d,%d,%.9g\n"),I,V[0],V[1],V[2],Display.R,Display.G,Display.B,E);
  }
  UE_LOG(LogTemp,Display,TEXT("OK studio grade test: unfiltered_capture_max_display_error=%.9g"),IdentityDisplayError);
  Capture->DestroyComponent();Pass &= MaxError<.02f && MaxDisplayError<.04f && IdentityDisplayError<.04f;
  const FString Out=FPaths::ProjectSavedDir()/TEXT("OKStudio/GradeTest");IFileManager::Get().MakeDirectory(*Out,true);FFileHelper::SaveStringToFile(Report,*(Out/TEXT("native-comparison.csv")));
  UE_LOG(LogTemp,Display,TEXT("OK studio grade test: samples=%d max_linear_error=%.9g RESULT %s"),UE_ARRAY_COUNT(NativeGrade),MaxError,Pass?TEXT("PASS"):TEXT("FAIL"));
  UE_LOG(LogTemp,Display,TEXT("OK studio grade test: actual_capture_max_display_error=%.9g"),MaxDisplayError);
  UE_LOG(LogTemp,Display,TEXT("OK studio grade test: RESULT %s"),Pass?TEXT("PASS"):TEXT("FAIL"));
  State->Cube->Destroy();State->Scene->Destroy();if(Exit)FPlatformMisc::RequestExit(false);return false;
 }),.1f);
#else
 if(Exit)FPlatformMisc::RequestExit(false);
#endif
}
FAutoConsoleCommand GradeTest(TEXT("OKStudio.GradeTest"),TEXT("Installed native LUT GPU color oracle."),FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args){RunGradeTest(Args.Contains(TEXT("exit")));}));
}
