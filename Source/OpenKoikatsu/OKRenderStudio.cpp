// Render-style studio (OKRenderStudio.h). studio.json:
//   {"selection": selection.json, "output": dir, "render_style": "koikatsu" | "hg",
//    "styles": ["koikatsu", "ramp:<key>", ...], "characters": [1,2,3,4,5],
//    "lights": {"front": [x,y,z], ...}  (toward the light, UE space; the row faces -Y),
//    "spacing": 95, "distance": 1050, "focus_height": 86, "fov": 28, "width": 4800, "height": 2000,
//    "background": [128,128,128]}
// Writes <output>/<style>.<light>.png (':' as '_') and <output>/studio.json, then exits.
#include "OKRenderStudio.h"
#include "OKCrowdRuntimeScene.h"
#include "OKHGShading.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "ImageUtils.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"
#include "UObject/StrongObjectPtr.h"
#include "RenderingThread.h"

struct FOKRenderStudio {
 FString Output;TArray<FString> Styles;TArray<TPair<FString,FVector>> Lights;TArray<int32> Characters;
 double Spacing=95,Distance=1050,FocusHeight=86,Fov=28;int32 Width=4800,Height=2000;FLinearColor Background=FLinearColor(.5f,.5f,.5f);
 int32 Step=0,Settle=0;double ReadyAt=0;bool Posed=false;
 TStrongObjectPtr<UTextureRenderTarget2D> Ldr,Hdr;TWeakObjectPtr<USceneCaptureComponent2D> Capture;
 TArray<TSharedPtr<FJsonValue>> Shots;
};

static TSharedPtr<FOKRenderStudio> StartStudio(AActor* Owner,const FString& ConfigPath,FString& Error){
 FString Text;TSharedPtr<FJsonObject> J;
 if(!FFileHelper::LoadFileToString(Text,*ConfigPath)||!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),J)||!J){Error=TEXT("Studio config unreadable: ")+ConfigPath;return nullptr;}
 auto S=MakeShared<FOKRenderStudio>();FString Selection,Style;
 if(!J->TryGetStringField(TEXT("selection"),Selection)||!J->TryGetStringField(TEXT("output"),S->Output)){Error=TEXT("Studio config needs selection and output");return nullptr;}
 // A single style may come as a plain string (PowerShell unwraps one-element arrays).
 const TArray<TSharedPtr<FJsonValue>>* List=nullptr;FString One;
 if(J->TryGetArrayField(TEXT("styles"),List)){for(const auto& V:*List)S->Styles.Add(V->AsString());}else if(J->TryGetStringField(TEXT("styles"),One))S->Styles.Add(One);
 if(J->TryGetArrayField(TEXT("characters"),List))for(const auto& V:*List)S->Characters.Add(int32(V->AsNumber()));
 const TSharedPtr<FJsonObject>* Lights=nullptr;
 if(J->TryGetObjectField(TEXT("lights"),Lights))for(const auto& L:(*Lights)->Values){const auto& A=L.Value->AsArray();if(A.Num()==3)S->Lights.Add({FString(*L.Key),FVector(A[0]->AsNumber(),A[1]->AsNumber(),A[2]->AsNumber())});}
 J->TryGetNumberField(TEXT("spacing"),S->Spacing);J->TryGetNumberField(TEXT("distance"),S->Distance);J->TryGetNumberField(TEXT("focus_height"),S->FocusHeight);J->TryGetNumberField(TEXT("fov"),S->Fov);
 J->TryGetNumberField(TEXT("width"),S->Width);J->TryGetNumberField(TEXT("height"),S->Height);
 if(J->TryGetArrayField(TEXT("background"),List)&&List->Num()==3)S->Background=FLinearColor((*List)[0]->AsNumber()/255.,(*List)[1]->AsNumber()/255.,(*List)[2]->AsNumber()/255.);
 if(S->Styles.IsEmpty()||S->Lights.IsEmpty()||S->Characters.IsEmpty()){Error=TEXT("Studio config needs styles, lights and characters");return nullptr;}
 if(S->Width<64||S->Height<64||S->Width>8192||S->Height>8192){Error=TEXT("Studio size out of range");return nullptr;}
 // Materials are made at load from these (HG has its own parents; ramps swap live).
 J->TryGetStringField(TEXT("render_style"),Style);OKSetRenderStyle(Style==TEXT("hg")?1:0);OKSetRenderRampKey(FString());
 IFileManager::Get().MakeDirectory(*S->Output,true);
 if(!OKStartCrowdRuntimeScene(Owner,Selection,Error))return nullptr;
 UE_LOG(LogTemp,Display,TEXT("Render studio: %d styles x %d lights, %d characters, %dx%d -> %s"),S->Styles.Num(),S->Lights.Num(),S->Characters.Num(),S->Width,S->Height,*S->Output);
 return S;
}

// Characters face -Y (KK front is local +Y); the camera stands on -Y looking at the row.
static FTransform StudioPlace(const FOKRenderStudio& S,int32 Slot){
 return FTransform(FRotator(0,180,0),FVector((Slot-(S.Characters.Num()-1)*.5)*S.Spacing,0,0));
}

static UTextureRenderTarget2D* MakeTarget(UWorld* World,int32 W,int32 H,ETextureRenderTargetFormat Format){
 auto* T=NewObject<UTextureRenderTarget2D>(World);T->RenderTargetFormat=Format;T->ClearColor=FLinearColor::Transparent;T->InitAutoFormat(W,H);T->UpdateResourceImmediate(true);return T;
}

// False once finished (exit requested) or failed.
static bool TickStudio(AActor* Owner,FOKRenderStudio& S){
 FString Error;auto* World=Owner->GetWorld();
 // Everyone not photographed waits far behind the row.
 for(int32 C=0;C<16;++C){FOKRuntimeCharacterStatus Status;if(!OKGetCrowdRuntimeCharacterStatus(Owner,C,Status))break;
  const int32 Slot=S.Characters.Find(C);OKSetCrowdRuntimeCharacterMotion(Owner,C,Slot>=0?StudioPlace(S,Slot):FTransform(FVector(0,20000+C*300,0)),0,Error);}
 if(!S.Posed){
  for(int32 C:S.Characters){FOKRuntimeCharacterStatus Status;if(!OKGetCrowdRuntimeCharacterStatus(Owner,C,Status)||!Status.Ready||!Status.Visible){if(!Status.Error.IsEmpty()){UE_LOG(LogTemp,Error,TEXT("Render studio: character %d failed: %s"),C,*Status.Error);FPlatformMisc::RequestExitWithStatus(false,1);return false;}return true;}}
  for(int32 C:S.Characters)OKSetCrowdRuntimeBlink(Owner,C,0,Error); // eyes open in every shot
  S.Ldr.Reset(MakeTarget(World,S.Width,S.Height,RTF_RGBA8));S.Hdr.Reset(MakeTarget(World,S.Width,S.Height,RTF_RGBA16f));
  auto* C=NewObject<USceneCaptureComponent2D>(Owner);C->bCaptureEveryFrame=false;C->bCaptureOnMovement=false;C->bAlwaysPersistRenderingState=true;C->FOVAngle=S.Fov;
  OKApplyCrowdRuntimeDisplayProfile(Owner,C->PostProcessSettings);C->PostProcessBlendWeight=1;C->ShowFlags.SetTemporalAA(false);C->ShowFlags.SetMotionBlur(false);C->RegisterComponentWithWorld(World);
  const FVector Eye=FVector(0,0,S.FocusHeight)+FVector(0,-S.Distance,0);C->SetWorldLocationAndRotation(Eye,FRotator(0,90,0));S.Capture=C;
  // The window shows the same view.
  if(auto* Pc=World->GetFirstPlayerController())if(auto* Cam=World->SpawnActor<ACameraActor>(Eye,FRotator(0,90,0))){Cam->GetCameraComponent()->SetFieldOfView(S.Fov);Pc->SetViewTarget(Cam);}
  S.Posed=true;S.ReadyAt=FPlatformTime::Seconds();return true;
 }
 if(FPlatformTime::Seconds()-S.ReadyAt<3)return true; // first poses and reveal settle
 const int32 Total=S.Styles.Num()*S.Lights.Num();
 if(S.Step>=Total){
  auto R=MakeShared<FJsonObject>();R->SetArrayField(TEXT("shots"),S.Shots);R->SetBoolField(TEXT("passed"),S.Shots.Num()==Total);
  FString Out;FJsonSerializer::Serialize(R,TJsonWriterFactory<>::Create(&Out));FFileHelper::SaveStringToFile(Out,*(S.Output/TEXT("studio.json")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
  UE_LOG(LogTemp,Display,TEXT("Render studio: done, %d shots"),S.Shots.Num());FPlatformMisc::RequestExitWithStatus(false,S.Shots.Num()==Total?0:1);return false;
 }
 const FString& Style=S.Styles[S.Step/S.Lights.Num()];const auto& Light=S.Lights[S.Step%S.Lights.Num()];
 if(S.Settle==0){OKSetCrowdRuntimeRamp(Owner,Style.StartsWith(TEXT("ramp:"))?Style.Mid(5):FString());OKSetCrowdRuntimeStudioLight(Owner,Light.Value);}
 if(++S.Settle<4)return true; // parameter updates reach the render thread
 S.Settle=0;auto* C=S.Capture.Get();if(!C)return false;
 // The look (tone/LUT) from the LDR capture; coverage from the HDR capture's inverse opacity, so the empty map
 // becomes a plain backdrop and translucent edges blend into it.
 C->CaptureSource=SCS_FinalColorLDR;C->TextureTarget=S.Ldr.Get();for(int32 Warm=0;Warm<2;++Warm){C->CaptureScene();FlushRenderingCommands();}
 TArray<FColor> Color;FReadSurfaceDataFlags Flags(RCM_UNorm);Flags.SetLinearToGamma(false);
 bool Good=S.Ldr->GameThread_GetRenderTargetResource()->ReadPixels(Color,Flags)&&Color.Num()==S.Width*S.Height;
 C->CaptureSource=SCS_SceneColorHDR;C->TextureTarget=S.Hdr.Get();C->CaptureScene();FlushRenderingCommands();
 TArray<FLinearColor> Scene;Good=Good&&S.Hdr->GameThread_GetRenderTargetResource()->ReadLinearColorPixels(Scene)&&Scene.Num()==Color.Num();
 const FColor Back=S.Background.ToFColor(false);
 if(Good)for(int32 I=0;I<Color.Num();++I){const float Cover=FMath::Clamp(1.f-Scene[I].A,0.f,1.f);auto& P=Color[I];
  P=FColor(uint8(FMath::Clamp(P.R*Cover+Back.R*(1-Cover),0.f,255.f)),uint8(FMath::Clamp(P.G*Cover+Back.G*(1-Cover),0.f,255.f)),uint8(FMath::Clamp(P.B*Cover+Back.B*(1-Cover),0.f,255.f)),255);}
 const FString Name=Style.Replace(TEXT(":"),TEXT("_"))+TEXT(".")+Light.Key+TEXT(".png");
 if(Good)Good=FImageUtils::SaveImageByExtension(*(S.Output/Name),FImageView(Color.GetData(),S.Width,S.Height));
 auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("style"),Style);Row->SetStringField(TEXT("light"),Light.Key);Row->SetStringField(TEXT("file"),Name);Row->SetBoolField(TEXT("saved"),Good);
 if(Good)S.Shots.Add(MakeShared<FJsonValueObject>(Row));else UE_LOG(LogTemp,Error,TEXT("Render studio: capture failed %s"),*Name);
 UE_LOG(LogTemp,Display,TEXT("Render studio: %d/%d %s"),S.Step+1,Total,*Name);
 ++S.Step;return true;
}

AOKRenderStudio::AOKRenderStudio(){PrimaryActorTick.bCanEverTick=true;}
void AOKRenderStudio::BeginPlay(){
 Super::BeginPlay();FString Error;Studio=StartStudio(this,ConfigPath,Error);
 if(!Studio){UE_LOG(LogTemp,Error,TEXT("Render studio: %s"),*Error);FPlatformMisc::RequestExitWithStatus(false,1);}
}
void AOKRenderStudio::Tick(float Dt){Super::Tick(Dt);if(Studio&&!TickStudio(this,*Studio))Studio.Reset();}

bool UOKRenderStudioSubsystem::ShouldCreateSubsystem(UObject* Outer) const{
 FString Config;return FParse::Value(FCommandLine::Get(),TEXT("RenderStudio="),Config)&&Super::ShouldCreateSubsystem(Outer);
}
void UOKRenderStudioSubsystem::OnWorldBeginPlay(UWorld& World){
 Super::OnWorldBeginPlay(World);if(!World.IsGameWorld())return;
 FString Config;FParse::Value(FCommandLine::Get(),TEXT("RenderStudio="),Config);
 FActorSpawnParameters Spawn;Spawn.bDeferConstruction=true;
 if(auto* Studio=World.SpawnActor<AOKRenderStudio>(FVector::ZeroVector,FRotator::ZeroRotator,Spawn)){Studio->ConfigPath=Config;Studio->FinishSpawning(FTransform::Identity);}
}
