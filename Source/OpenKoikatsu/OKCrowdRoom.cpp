#include "OKCrowdRoom.h"
#include "OKCrowdRuntimeScene.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Containers/Ticker.h"
#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "InputCoreTypes.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "RHI.h"
#include "RHIStats.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UnrealClient.h"

namespace OKCrowdRoom {
// The street's walking pace and turning limit (OKStreetWalkSpeed, OKStreetLimitFemaleFacing).
constexpr float WalkSpeed=110.f,TurnRate=145.f;
// Two girls closer than this stop rather than walk into each other; spots are picked at least this far apart.
constexpr float Gap=110.f;
// After standing a while she walks somewhere else this often; otherwise she changes her pose and stays.
constexpr float WalkChance=.3f;

struct FAgent {
 FVector Pos=FVector::ZeroVector,Target=FVector::ZeroVector;float Yaw=0;
 bool Walking=false,Moving=false;double Until=0,BlockedSince=-1;
 int32 Variants=0,Variant=INDEX_NONE;bool Visible=false,Failed=false;
};

struct FRoom {
 TWeakObjectPtr<AActor> Owner;TWeakObjectPtr<ACameraActor> Camera;FRandomStream Rng{20261027};TArray<FAgent> Agents;
 float Half=2400,FloorZ=10000;double Time=0,Wall0=0;
 FVector CamPos=FVector::ZeroVector;FRotator CamRot=FRotator::ZeroRotator;
 // Frame-time report: warmup_seconds after every girl is drawn, then benchmark_seconds of frames.
 double BenchSeconds=0,Warmup=10,AllVisibleAt=-1,BenchStart=-1,NextVisibleCheck=0,NextLog=0,ExitAt=-1;int32 Visible=0,Failed=0;
 FString Report;bool ExitAfter=false,BenchDone=false;TArray<float> Frames;double GameSum=0,RenderSum=0,GpuSum=0,DrawSum=0,TriSum=0;
 // In-process A/B (manifest room.cases: [{name, exec: [console commands]}]): each case is applied, warmed up and
 // measured for benchmark_seconds in turn, the same girls and camera; the report lists every case.
 struct FCase {FString Name;TArray<FString> Exec;};TArray<FCase> Cases;int32 Case=0;TArray<TSharedPtr<FJsonValue>> Results;double CaseAppliedAt=-1;
 // A CPU trace of the (first) measurement only (room.trace_file).
 FString TraceFile;bool Tracing=false;
 TArray<float> LogFrames;

 // A free spot in the room; From given: 3..10 m from there (a short walk, not across the room).
 FVector RandomSpot(int32 Self,const FVector* From=nullptr){
  for(int32 Try=0;;++Try){
   FVector P(Rng.FRandRange(-Half+150,Half-150),Rng.FRandRange(-Half+150,Half-150),FloorZ);
   if(From){const float Angle=Rng.FRandRange(0.f,2*PI),Dist=Rng.FRandRange(300.f,1000.f);P=FVector(FMath::Clamp(From->X+FMath::Cos(Angle)*Dist,-Half+150,Half-150),FMath::Clamp(From->Y+FMath::Sin(Angle)*Dist,-Half+150,Half-150),FloorZ);}
   if(Try>=12)return P;bool Clear=true;
   for(int32 I=0;I<Agents.Num()&&Clear;++I)if(I!=Self)Clear=FVector::DistSquared2D(P,Agents[I].Pos)>FMath::Square(Gap*1.5f)&&FVector::DistSquared2D(P,Agents[I].Target)>FMath::Square(Gap*1.5f);
   if(Clear)return P;
  }
 }
 void Stand(FAgent& A,int32 Index){
  A.Walking=false;A.Moving=false;A.BlockedSince=-1;A.Until=Time+Rng.FRandRange(8.f,25.f);
  // A different one of her standing poses (now and then the plain one).
  if(A.Variants>0){int32 Next=Rng.RandRange(-1,A.Variants-1);if(Next==A.Variant)Next=(Next+2)%(A.Variants+1)-1;A.Variant=Next;OKSetCrowdRuntimeIdleVariant(Owner.Get(),Index,A.Variant);}
 }
 void Walk(FAgent& A,int32 Index){
  A.Target=RandomSpot(Index,&A.Pos);
  if(FVector::Dist2D(A.Target,A.Pos)<200){Stand(A,Index);return;}
  A.Walking=true;A.BlockedSince=-1;
 }
 static float FacingOf(const FVector& Dir){return FMath::RadiansToDegrees(FMath::Atan2(Dir.Y,Dir.X))-90.f;}

 void Build(UWorld* World,AActor* O){
  // A floor and four low walls (the engine cube, tinted), the girls' room high above the street map.
  auto* Cube=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube"));
  auto* Tint=LoadObject<UMaterialInterface>(nullptr,TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
  if(!Cube)return;
  auto Block=[&](const FVector& Center,const FVector& Size,const FLinearColor& Color){
   auto* C=NewObject<UStaticMeshComponent>(O);O->AddInstanceComponent(C);C->SetStaticMesh(Cube);C->SetMobility(EComponentMobility::Movable);C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
   if(Tint){auto* M=UMaterialInstanceDynamic::Create(Tint,O);M->SetVectorParameterValue(TEXT("Color"),Color);C->SetMaterial(0,M);}
   C->RegisterComponent();C->SetWorldTransform(FTransform(FRotator::ZeroRotator,Center,Size/100.f));};
  const float H=Half;
  Block(FVector(0,0,FloorZ-10),FVector(2*H+40,2*H+40,20),FLinearColor(.42f,.42f,.46f));
  for(int32 S=0;S<4;++S){const bool X=S<2;const float Sign=S%2?1.f:-1.f;
   Block(X?FVector(Sign*(H+10),0,FloorZ+60):FVector(0,Sign*(H+10),FloorZ+60),X?FVector(20,2*H+40,120):FVector(2*H+40,20,120),FLinearColor(.70f,.66f,.60f));}
 }

 bool Tick(float Dt){
  AActor* O=Owner.Get();if(!O||!O->GetWorld())return false;UWorld* World=O->GetWorld();Time+=Dt;FString Error;
  APlayerController* Pc=World->GetFirstPlayerController();
  // The free camera.
  if(Pc){
   if(Pc->IsInputKeyDown(EKeys::RightMouseButton)){float Dx=0,Dy=0;Pc->GetInputMouseDelta(Dx,Dy);CamRot.Yaw+=Dx*2.f;CamRot.Pitch=FMath::Clamp(CamRot.Pitch+Dy*2.f,-89.f,60.f);}
   FVector Move=FVector::ZeroVector;const FRotator Flat(0,CamRot.Yaw,0);
   if(Pc->IsInputKeyDown(EKeys::W))Move+=Flat.Vector();if(Pc->IsInputKeyDown(EKeys::S))Move-=Flat.Vector();
   if(Pc->IsInputKeyDown(EKeys::D))Move+=FRotationMatrix(Flat).GetUnitAxis(EAxis::Y);if(Pc->IsInputKeyDown(EKeys::A))Move-=FRotationMatrix(Flat).GetUnitAxis(EAxis::Y);
   if(Pc->IsInputKeyDown(EKeys::E))Move.Z+=1;if(Pc->IsInputKeyDown(EKeys::Q))Move.Z-=1;
   CamPos+=Move.GetSafeNormal()*Dt*(Pc->IsInputKeyDown(EKeys::LeftShift)?1500.f:500.f);
   if(auto* C=Camera.Get()){C->SetActorLocationAndRotation(CamPos,CamRot);if(Pc->GetViewTarget()!=C)Pc->SetViewTarget(C);}
  }
  // The girls.
  for(int32 I=0;I<Agents.Num();++I){auto& A=Agents[I];if(A.Failed)continue;
   if(A.Variants==0&&A.Visible){A.Variants=OKSetCrowdRuntimeIdleVariant(O,I,INDEX_NONE);if(A.Variants>0)Stand(A,I);}
   A.Moving=false;
   if(A.Walking){
    const FVector To=A.Target-A.Pos;const float Dist=To.Size2D();
    if(Dist<15){Stand(A,I);}
    else{
     const FVector Dir=To.GetSafeNormal2D();
     // Someone right ahead: wait for her to pass; still blocked after a while, go somewhere else.
     bool Blocked=false;for(int32 J=0;J<Agents.Num()&&!Blocked;++J)if(J!=I){const FVector D=Agents[J].Pos-A.Pos;const float Sq=D.SizeSquared2D();Blocked=Sq<Gap*Gap&&FVector::DotProduct(D.GetSafeNormal2D(),Dir)>.5f;}
     const float Want=FacingOf(Dir);A.Yaw=A.Yaw+FMath::Clamp(FMath::FindDeltaAngleDegrees(A.Yaw,Want),-TurnRate*Dt,TurnRate*Dt);
     if(Blocked){if(A.BlockedSince<0)A.BlockedSince=Time;if(Time-A.BlockedSince>3)Walk(A,I);}
     else{A.BlockedSince=-1;A.Pos+=Dir*FMath::Min(Dist,WalkSpeed*Dt);A.Moving=true;}
    }
   }else if(A.Variants>0&&Time>=A.Until){if(Rng.FRand()<WalkChance)Walk(A,I);else Stand(A,I);}
   OKSetCrowdRuntimeCharacterMotion(O,I,FTransform(FRotator(0,A.Yaw,0),A.Pos),A.Moving?1.f:0.f,Error);
  }
  // Who is drawn (once a second).
  const double Wall=FPlatformTime::Seconds();
  // A girl whose card could not load (her error set) counts as done: the others are measured without her.
  if(Wall>=NextVisibleCheck){NextVisibleCheck=Wall+1;Visible=0;Failed=0;
   for(int32 I=0;I<Agents.Num();++I){FOKRuntimeCharacterStatus St;const bool Got=OKGetCrowdRuntimeCharacterStatus(O,I,St);Agents[I].Visible=Got&&St.Visible;Agents[I].Failed=Got&&!St.Error.IsEmpty();Visible+=Agents[I].Visible;Failed+=Agents[I].Failed;}
   if(AllVisibleAt<0&&Visible+Failed==Agents.Num()){AllVisibleAt=Wall;UE_LOG(LogTemp,Display,TEXT("Crowd room: all %d girls drawn %.1f s after the start (%d could not load)"),Visible,Wall-Wall0,Failed);}}
  // Frame times (the same numbers as the street's /fps), logged every 5 s, and the report.
  const float GameMs=FPlatformTime::ToMilliseconds(GGameThreadTime),RenderMs=FPlatformTime::ToMilliseconds(GRenderThreadTime),GpuMs=FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles());
  // Draw calls and triangles of the last rendered frame (stat RHI's numbers).
  const int32 Draws=GNumDrawCallsRHI[0],Tris=GNumPrimitivesDrawnRHI[0];
  LogFrames.Add(Dt*1000.f);
  if(Wall>=NextLog){NextLog=Wall+5;LogFrames.Sort();float Sum=0;for(float F:LogFrames)Sum+=F;const float Mean=LogFrames.Num()?Sum/LogFrames.Num():0,P95=LogFrames.Num()?LogFrames[FMath::Min(LogFrames.Num()-1,int32(LogFrames.Num()*.95f))]:0;
   int32 Walking=0;for(const auto& A:Agents)Walking+=A.Moving;
   UE_LOG(LogTemp,Display,TEXT("Crowd room: %.1f FPS (frame %.2f ms, p95 %.2f) game %.2f render %.2f gpu %.2f ms, %d draws %.2fM triangles, drawn %d/%d, walking %d"),Mean>0?1000/Mean:0,Mean,P95,GameMs,RenderMs,GpuMs,Draws,Tris/1e6f,Visible,Agents.Num(),Walking);
   if(GEngine)GEngine->AddOnScreenDebugMessage(int32(0x0C10),6.f,FColor::Green,FString::Printf(TEXT("%.0f FPS  frame %.2f ms (p95 %.2f)  game %.2f  render %.2f  gpu %.2f  drawn %d/%d  walking %d"),Mean>0?1000/Mean:0,Mean,P95,GameMs,RenderMs,GpuMs,Visible,Agents.Num(),Walking));
   LogFrames.Reset();}
  if(BenchSeconds>0&&!BenchDone&&AllVisibleAt>=0&&Wall>=AllVisibleAt+Warmup){
   if(Cases.IsValidIndex(Case)&&CaseAppliedAt<0){CaseAppliedAt=Wall;for(const FString& C:Cases[Case].Exec)GEngine->Exec(World,*C);UE_LOG(LogTemp,Display,TEXT("Crowd room: case %s applied"),*Cases[Case].Name);}
   if(Cases.IsValidIndex(Case)&&Wall<CaseAppliedAt+5)return true;
   if(BenchStart<0){BenchStart=Wall;UE_LOG(LogTemp,Display,TEXT("Crowd room: benchmark starts (%.0f s)"),BenchSeconds);
    if(!TraceFile.IsEmpty()&&Results.IsEmpty()){GEngine->Exec(World,*FString::Printf(TEXT("Trace.File \"%s\" cpu,frame"),*TraceFile));Tracing=true;}}
   Frames.Add(Dt*1000.f);GameSum+=GameMs;RenderSum+=RenderMs;GpuSum+=GpuMs;DrawSum+=Draws;TriSum+=Tris;
   if(Wall-BenchStart>=BenchSeconds)FinishBench(Wall);}
  if(ExitAt>0&&Wall>=ExitAt){FPlatformMisc::RequestExit(false);return false;}
  return true;
 }

 void FinishBench(double Wall){
  if(Tracing){if(AActor* O=Owner.Get())GEngine->Exec(O->GetWorld(),TEXT("Trace.Stop"));Tracing=false;}
  TArray<float> Sorted=Frames;Sorted.Sort();double Sum=0;for(float F:Sorted)Sum+=F;const int32 N=Sorted.Num();
  const double Mean=N?Sum/N:0,P95=N?Sorted[FMath::Min(N-1,int32(N*.95))]:0,P99=N?Sorted[FMath::Min(N-1,int32(N*.99))]:0;
  int32 Over=0;for(float F:Sorted)Over+=F>1000.f/120.f;
  auto J=MakeShared<FJsonObject>();J->SetNumberField(TEXT("characters"),Agents.Num());J->SetNumberField(TEXT("drawn"),Visible);J->SetNumberField(TEXT("failed"),Failed);
  J->SetNumberField(TEXT("all_drawn_seconds"),AllVisibleAt-Wall0);J->SetNumberField(TEXT("samples"),N);J->SetNumberField(TEXT("seconds"),Wall-BenchStart);
  J->SetNumberField(TEXT("fps_mean"),Mean>0?1000/Mean:0);J->SetNumberField(TEXT("frame_mean_ms"),Mean);J->SetNumberField(TEXT("frame_p95_ms"),P95);J->SetNumberField(TEXT("frame_p99_ms"),P99);
  J->SetNumberField(TEXT("frames_over_8_33_ms"),Over);J->SetNumberField(TEXT("game_ms"),N?GameSum/N:0);J->SetNumberField(TEXT("render_ms"),N?RenderSum/N:0);J->SetNumberField(TEXT("gpu_ms"),N?GpuSum/N:0);J->SetNumberField(TEXT("draw_calls"),N?DrawSum/N:0);J->SetNumberField(TEXT("triangles"),N?TriSum/N:0);
  J->SetBoolField(TEXT("target_120_met"),N>=120&&Mean<=1000./120.&&P95<=1000./120.);
  {const IConsoleVariable* Max=IConsoleManager::Get().FindConsoleVariable(TEXT("t.MaxFPS"));J->SetNumberField(TEXT("max_fps"),Max?Max->GetFloat():0);}
  if(Cases.IsValidIndex(Case)){J->SetStringField(TEXT("case"),Cases[Case].Name);Results.Add(MakeShared<FJsonValueObject>(J));
   if(!Report.IsEmpty())FScreenshotRequest::RequestScreenshot(FPaths::GetPath(Report)/FString::Printf(TEXT("case%02d-%s.png"),Case,*Cases[Case].Name),false,false);
   UE_LOG(LogTemp,Display,TEXT("Crowd room case %s: %.1f FPS (frame %.2f ms, p95 %.2f) game %.2f render %.2f gpu %.2f, %.0f draws"),*Cases[Case].Name,Mean>0?1000/Mean:0,Mean,P95,N?GameSum/N:0,N?RenderSum/N:0,N?GpuSum/N:0,N?DrawSum/N:0);
   if(++Case<Cases.Num()){Frames.Reset();GameSum=RenderSum=GpuSum=DrawSum=TriSum=0;BenchStart=-1;CaseAppliedAt=-1;return;}
   J=MakeShared<FJsonObject>();J->SetArrayField(TEXT("cases"),Results);}
  BenchDone=true;
  FString Text;FJsonSerializer::Serialize(J,TJsonWriterFactory<>::Create(&Text));
  UE_LOG(LogTemp,Display,TEXT("Crowd room benchmark: %s"),*Text);
  if(!Report.IsEmpty()){FFileHelper::SaveStringToFile(Text,*Report,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);FScreenshotRequest::RequestScreenshot(FPaths::ChangeExtension(Report,TEXT("png")),false,false);}
  if(ExitAfter)ExitAt=Wall+3;
 }
};
}

bool OKStartCrowdRoom(AActor* Owner,const FString& Manifest,FString& Error){
 using namespace OKCrowdRoom;
 FString Text;TSharedPtr<FJsonObject> J;
 if(!Owner||!FFileHelper::LoadFileToString(Text,*Manifest)||!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),J)||!J){Error=TEXT("Invalid crowd room manifest");return false;}
 const TArray<TSharedPtr<FJsonValue>>* Cards=nullptr;if(!J->TryGetArrayField(TEXT("cards"),Cards)||Cards->IsEmpty()){Error=TEXT("Crowd room needs cards");return false;}
 if(!OKStartCrowdRuntimeScene(Owner,Manifest,Error))return false;
 auto Room=MakeShared<FRoom>();Room->Owner=Owner;Room->Wall0=FPlatformTime::Seconds();
 const TSharedPtr<FJsonObject>* Settings=nullptr;
 if(J->TryGetObjectField(TEXT("room"),Settings)&&Settings&&Settings->IsValid()){const auto& R=*Settings;double V=0;
  if(R->TryGetNumberField(TEXT("half_size"),V))Room->Half=FMath::Clamp(float(V),500.f,10000.f);
  R->TryGetNumberField(TEXT("benchmark_seconds"),Room->BenchSeconds);R->TryGetNumberField(TEXT("warmup_seconds"),Room->Warmup);
  R->TryGetStringField(TEXT("report"),Room->Report);R->TryGetBoolField(TEXT("exit_after_report"),Room->ExitAfter);
  double Seed=0;if(R->TryGetNumberField(TEXT("seed"),Seed))Room->Rng.Initialize(int32(Seed));
  R->TryGetStringField(TEXT("trace_file"),Room->TraceFile);
  const TArray<TSharedPtr<FJsonValue>>* Cases=nullptr;if(R->TryGetArrayField(TEXT("cases"),Cases))for(const auto& CaseValue:*Cases){const auto C=CaseValue->AsObject();if(!C)continue;FRoom::FCase Item;C->TryGetStringField(TEXT("name"),Item.Name);
   const TArray<TSharedPtr<FJsonValue>>* Exec=nullptr;if(C->TryGetArrayField(TEXT("exec"),Exec))for(const auto& E:*Exec)Item.Exec.Add(E->AsString());Room->Cases.Add(MoveTemp(Item));}}
 UWorld* World=Owner->GetWorld();Room->Build(World,Owner);
 Room->Agents.SetNum(Cards->Num());
 for(int32 I=0;I<Room->Agents.Num();++I){auto& A=Room->Agents[I];A.Pos=Room->RandomSpot(I);A.Target=A.Pos;A.Yaw=Room->Rng.FRandRange(-180.f,180.f);A.Until=Room->Rng.FRandRange(2.f,12.f);}
 // Overview from a corner, the whole room in view.
 Room->CamPos=FVector(-Room->Half*1.05f,-Room->Half*1.05f,Room->FloorZ+1500);Room->CamRot=FRotator(-28,45,0);
 if(auto* Camera=World->SpawnActor<ACameraActor>()){Camera->GetCameraComponent()->SetFieldOfView(70);OKApplyCrowdRuntimeDisplayProfile(Owner,Camera->GetCameraComponent()->PostProcessSettings);Camera->GetCameraComponent()->PostProcessBlendWeight=1;Camera->SetActorLocationAndRotation(Room->CamPos,Room->CamRot);Room->Camera=Camera;}
 FString Err;for(int32 I=0;I<Room->Agents.Num();++I)OKSetCrowdRuntimeCharacterMotion(Owner,I,FTransform(FRotator(0,Room->Agents[I].Yaw,0),Room->Agents[I].Pos),0,Err);
 UE_LOG(LogTemp,Display,TEXT("Crowd room: %d girls in a %.0f m room (benchmark %.0f s after a %.0f s warmup)"),Room->Agents.Num(),Room->Half*2/100,Room->BenchSeconds,Room->Warmup);
 FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Room](float Dt){return Room->Tick(Dt);}));
 return true;
}
