#include "ok_extract.h"
#include "KKCardWriter.h"
#include "ProfilingDebugging/MiscTrace.h"
#include "Components/EditableTextBox.h"
#include "Components/CheckBox.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/Material.h"
#include "../OKGalleryWidget.h"
#include "../OKShapeSlider.h"
#include "Components/TextBlock.h"
#include "Components/InvalidationBox.h"
#include "../OpenKoikatsuPlayerController.h"
#include "../OKPaintSprayer.h"
#include "../OKPaintSkinning.h"
#include "KKPreviewActor.h"
#include "KKAffineProceduralMeshComponent.h"
#include "KKSkeletalBuilder.h"
#include "KKExactSkinning.h"
#include "KKGameRoot.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "InputKeyEventArgs.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Camera/CameraActor.h"
#include "Camera/PlayerCameraManager.h"
#include "ProceduralMeshComponent.h"
#include "UObject/UObjectIterator.h"
#include "TimerManager.h"
#include "HAL/IConsoleManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HighResScreenshot.h"
#include "ShaderCompiler.h"
#include "Components/SkinnedMeshComponent.h"
#include "SkeletalRenderPublic.h"
#include "Components/PoseableMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Rendering/SkinWeightVertexBuffer.h"
#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
namespace OKReaderAudit {
static constexpr float GazeCases[][3]={{0,0,0},{-1,-1,0},{1,1,0},{0,0,-1},{0,0,1},{.8f,-.6f,.7f},{-.8f,.6f,-.7f},{0,0,0},{.8f,-.6f,.7f}};
struct FRun:TSharedFromThis<FRun>{
 TWeakObjectPtr<UProceduralMeshComponent> AimMesh;int32 AimTriangle=INDEX_NONE;
 double ActivePaintCpu=0.,ActiveWall=0.,OperationWall=0.;uint64 ActivePaintSamples=0,ActiveFrames=0,OperationFrames=0;int32 OperationNext=0;FString OperationLabel;
 double OperationWarmWall=0.,OperationFirstMs=-1.;uint64 OperationWarmFrames=0;
 double OperationActorCpuBase=0.;uint64 OperationActorSamplesBase=0;
 FString OperationRegionName,ProfileStageName;
 void EndProfileStage(){if(!ProfileStageName.IsEmpty()){TRACE_END_REGION(*ProfileStageName);ProfileStageName.Empty();}}
 void BeginProfileStage(const TCHAR* Label){EndProfileStage();ProfileStageName=FString::Printf(TEXT("Reader_C%03d_%s"),Card,Label);TRACE_BEGIN_REGION(*ProfileStageName);}
 bool Budget(double Ms,const TCHAR* Label){const FString Limit=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_READER_FRAME_BUDGET_MS"));if(!Limit.IsEmpty()&&Ms>FCString::Atod(*Limit)){Stop(FString::Printf(TEXT("%s mean %.6fms exceeded frame budget"),Label,Ms));return false;}return true;}
 bool BeginOperation(AKKPreviewActor* A,const TCHAR* Label,int32 Next){
  FString Clip=TEXT("studio/anime/00.unity3d|f_aruki_00_00_01");
  if(FString(Label)==TEXT("removed_loaded")&&FPlatformMisc::GetEnvironmentVariable(TEXT("OK_READER_ALTERNATE_CLIP"))==TEXT("1")){
   const auto Names=A->Session.ClipNamesForSource(TEXT("studio/anime/00.unity3d"));bool Different=false;
   for(const auto& Name:Names)if(Name!=TEXT("f_aruki_00_00_01")){Clip=TEXT("studio/anime/00.unity3d|")+Name;Different=true;break;}
   if(!Different){Stop(TEXT("Alternate imported animation unavailable"));return false;}
  }
  if(!A->SetGameAnimation(Clip)){Stop(TEXT("Operation playback source unavailable"));return false;}
  Rows+=FString::Printf(TEXT("operation_animation=%s clip=%s\n"),Label,*Clip);OperationLabel=Label;OperationWall=FPlatformTime::Seconds();OperationFrames=GFrameCounter;OperationWarmWall=0.;OperationWarmFrames=0;OperationFirstMs=-1.;OperationActorCpuBase=A->AnimationTickTotalMs;OperationActorSamplesBase=A->AnimationTickSamples;OperationNext=Next;Stage=15;OperationRegionName=FString::Printf(TEXT("Reader_C%03d_%s"),Card,Label);TRACE_BEGIN_REGION(*OperationRegionName);return true;
 }
 bool StreamParity(AKKPreviewActor* A){
  auto* Variable=IConsoleManager::Get().FindConsoleVariable(TEXT("ok.Animation.StreamParallelMinVertices"));
  if(!Variable){Stop(TEXT("Missing procedural stream scheduling control"));return false;}
  const int32 Saved=Variable->GetInt();const float Phase=A->AnimPhase;
  auto ForceRefresh=[&](){for(auto& P:A->Parts)if(P.Comp&&!P.bSlotAttached&&P.SessionIndex!=INDEX_NONE&&(P.bRuntimeAccessory||A->SkelFollowers.IsEmpty())&&!P.BaseVertices.IsEmpty()){OkMeshView V{};if(A->Session.PartAt(P.SessionIndex,V)&&V.positions&&V.n_verts==P.BaseVertices.Num()){P.RuntimeSourceGeometry.Reset();P.BaseVertices[0].X+=1.;}}A->RefreshSkinnedDisplay();};
  Variable->Set(1,ECVF_SetByCode);ForceRefresh();
  TArray<TArray<TArray<FProcMeshVertex>>> Reference;Reference.SetNum(A->Parts.Num());
  for(int32 I=0;I<A->Parts.Num();++I)if(auto* C=A->Parts[I].Comp.Get()){Reference[I].SetNum(C->GetNumSections());for(int32 S=0;S<C->GetNumSections();++S)if(const auto* Section=C->GetProcMeshSection(S))Reference[I][S]=Section->ProcVertexBuffer;}
  Variable->Set(MAX_int32,ECVF_SetByCode);ForceRefresh();Variable->Set(Saved,ECVF_SetByCode);
  int32 Checked=0;bool Same=A->AnimPhase==Phase;
  for(int32 I=0;I<A->Parts.Num();++I)if(auto* C=A->Parts[I].Comp.Get())for(int32 S=0;S<C->GetNumSections();++S){const auto* Section=C->GetProcMeshSection(S);if(!Section)continue;
   if(!Reference[I].IsValidIndex(S)||Reference[I][S].Num()!=Section->ProcVertexBuffer.Num()){Same=false;continue;}
   for(int32 V=0;V<Reference[I][S].Num();++V){const auto& X=Reference[I][S][V];const auto& Y=Section->ProcVertexBuffer[V];Same&=X.Position.Equals(Y.Position,1.e-12)&&X.Normal.Equals(Y.Normal,1.e-12)&&X.Tangent.TangentX.Equals(Y.Tangent.TangentX,1.e-12)&&X.Tangent.bFlipTangentY==Y.Tangent.bFlipTangentY&&X.Color==Y.Color&&X.UV0==Y.UV0&&X.UV1==Y.UV1&&X.UV2==Y.UV2&&X.UV3==Y.UV3;++Checked;}
  }
  Rows+=FString::Printf(TEXT("stream_parallel_serial_equal=%d checked_draw_vertices=%d clock_unchanged=%d\n"),Same,Checked,A->AnimPhase==Phase);
  if(!Same||!Checked){Stop(TEXT("Serial procedural stream differs from parallel draw oracle"));return false;}return true;
 }
 bool MorphPruneParity(AKKPreviewActor* A){
  auto* Variable=IConsoleManager::Get().FindConsoleVariable(TEXT("ok.Face.PruneInactiveMorphs"));
  if(!Variable){Stop(TEXT("Missing inactive morph reference control"));return false;}
  const int32 Saved=Variable->GetInt();const float Phase=A->AnimPhase,FaceTime=A->FaceClock;
  struct FSnapshot{UProceduralMeshComponent* Component;TArray<TArray<FProcMeshVertex>> Sections;};
  TArray<FSnapshot> Snapshots;
  Variable->Set(0,ECVF_SetByCode);A->ApplyMouth();
  for(const auto& Slot:A->MouthSlots)if(Slot.CpuComp){FSnapshot S;S.Component=Slot.CpuComp;for(int32 I=0;I<Slot.CpuComp->GetNumSections();++I)S.Sections.Add(Slot.CpuComp->GetProcMeshSection(I)->ProcVertexBuffer);Snapshots.Add(MoveTemp(S));}
  Variable->Set(1,ECVF_SetByCode);A->ApplyMouth();
  bool Same=A->AnimPhase==Phase&&A->FaceClock==FaceTime;int32 Checked=0;
  for(const auto& S:Snapshots){Same&=S.Component->GetNumSections()==S.Sections.Num();for(int32 I=0;I<S.Sections.Num();++I){const auto* Section=S.Component->GetProcMeshSection(I);if(!Section||Section->ProcVertexBuffer.Num()!=S.Sections[I].Num()){Same=false;continue;}for(int32 V=0;V<S.Sections[I].Num();++V){const auto& X=S.Sections[I][V];const auto& Y=Section->ProcVertexBuffer[V];Same&=X.Position.Equals(Y.Position,1.e-12)&&X.Normal.Equals(Y.Normal,1.e-12)&&X.Tangent.TangentX.Equals(Y.Tangent.TangentX,1.e-12)&&X.Tangent.bFlipTangentY==Y.Tangent.bFlipTangentY&&X.Color==Y.Color&&X.UV0==Y.UV0&&X.UV1==Y.UV1&&X.UV2==Y.UV2&&X.UV3==Y.UV3;++Checked;}}}
  Variable->Set(Saved,ECVF_SetByCode);
  Rows+=FString::Printf(TEXT("inactive_morph_reference_equal=%d checked_draw_vertices=%d clocks_unchanged=%d\n"),Same,Checked,A->AnimPhase==Phase&&A->FaceClock==FaceTime);
  if(!Same||!Checked){Stop(TEXT("Pruned face morph differs from unpruned reference"));return false;}return true;
 }
 double ShapeNextAt=0.;uint64 ShapeProbeRequests=0;TArray<float> ShapeProbeOriginal;
 double AnimationStart=0.,AnimationCpuBase=0.;uint64 AnimationSampleBase=0,AnimationFrameBase=0,AnimationUploadBase=0,AnimationSkipBase=0,AnimationAimBase=0;float AnimationPhaseBase=0;
 TWeakObjectPtr<UWorld> World;FTimerHandle Timer;FString Directory,Output,PreviousRoot,EditedCard;
 TArray<int32> Cards{0,1};int32 CardPosition=0;bool WornOnly=false,AllOutfits=false;int32 Outfit=0,GazeCapture=0;
 double Started=FPlatformTime::Seconds(),SprayAt=0,ViewReadyAt=0,LoadStartedAt=0;uint64 ViewReadyFrame=0;int32 Stage=0,Card=0,HairParts=0;uint64 WornTriangles=0,HalfTriangles=0,RemovedTriangles=0;double Deposit=0;int32 ClothingCoatings=0;FString Rows;bool JetCaptured=false;FDelegateHandle ScreenshotHandle;FVector FlowStart,FlowEnd;double FlowAt=0,FlowVolumeStart=0,FlowVolumeEnd=0,PaintCpuSum=0,PaintCpuMax=0,PaintFrameSum=0;int32 PaintSamples=0,PeakPackets=0,PeakFX=0;FVector ArmTarget=FVector::ZeroVector;double ReleasedCpuBase=0,ReleasedWallStart=0;uint64 ReleasedSampleBase=0,ReleasedFrameBase=0;
 uint64 ClothingTriangles(AKKPreviewActor* A){uint64 N=0;for(const auto& P:A->Parts)if(P.Role.StartsWith(TEXT("clothes"))){OkMeshView V{};if(A->Session.PartAt(P.SessionIndex,V)&&V.render_visible)N+=P.Triangles.Num()/3;}return N;}
 void Stop(const FString& Error){auto Keep=AsShared();EndProfileStage();FScreenshotRequest::OnScreenshotRequestProcessed().Remove(ScreenshotHandle);if(auto* W=World.Get())W->GetTimerManager().ClearTimer(Timer);UKKGameRoot::Get()->Root=PreviousRoot;FFileHelper::SaveStringToFile(FString::Printf(TEXT("valid=%d error=%s\n%s"),Error.IsEmpty(),*Error,*Rows),*(Output/TEXT("report.txt")));UE_LOG(LogTemp,Display,TEXT("Reader audit valid=%d error=%s %s"),Error.IsEmpty(),*Error,*Rows);FPlatformMisc::RequestExitWithStatus(false,Error.IsEmpty()?0:1);}
 uint64 LastAuditFrame=MAX_uint64;
 void Tick(){if(LastAuditFrame==GFrameCounter)return;LastAuditFrame=GFrameCounter;auto Keep=AsShared();auto* W=World.Get();if(!W||FPlatformTime::Seconds()-Started>900){Stop(TEXT("Timeout"));return;}
 UOKGalleryWidget* G=nullptr;for(TObjectIterator<UOKGalleryWidget> It;It;++It)if(It->GetWorld()==W&&It->CardSourceCombo){G=*It;break;}if(!G)return;
 auto* PC=Cast<AOpenKoikatsuPlayerController>(W->GetFirstPlayerController());if(!PC)return;
 if(Stage==0){G->bIndependentCards=true;G->Entries.Reset();FKKCardListEntry E;E.Path=Directory/FString::Printf(TEXT("C%03d.png"),Card);E.FileName=FPaths::GetCleanFilename(E.Path);G->Entries.Add(E);G->OutfitIndex=0;for(uint8& S:G->ClothesStates)S=0;G->SelectIndex(0);G->OutfitIndex=Outfit;G->RefreshOutfitList();LoadStartedAt=FPlatformTime::Seconds();G->OnLoad3D();Stage=1;return;}
 if(G->bLoadInFlight)return;if(PC->PlayerCameraManager)PC->PlayerCameraManager->UpdateCamera(0);auto* A=G->PreviewActor.Get();if(!IsValid(A)||!A->Session.IsPortable()||!PC->IsPaintPreviewActive()){Stop(TEXT("Portable actor/sprayer not active"));return;}
 if(Stage==15){const double Now=FPlatformTime::Seconds(),Elapsed=Now-OperationWall;
  if(OperationFirstMs<0.)OperationFirstMs=Elapsed*1000.;
  if(!OperationWarmFrames&&Elapsed>=.5){OperationWarmFrames=GFrameCounter;OperationWarmWall=Now;}
  if(Elapsed<5.)return;const double Mean=Elapsed*1000./FMath::Max(uint64(1),GFrameCounter-OperationFrames);Rows+=FString::Printf(TEXT("loaded_world_delta_rebases=%llu discarded_seconds=%.6f\n"),A->AnimationReadyClock.Rebases,A->AnimationReadyClock.DiscardedSeconds);Rows+=FString::Printf(TEXT("operation=%s loaded_playback_seconds=%.6f frame_mean_ms=%.6f phase=%.6f\n"),*OperationLabel,Elapsed,Mean,A->AnimPhase);
  Rows+=FString::Printf(TEXT("operation_diagnostic=%s first_observed_ms=%.6f after_half_second_frame_mean_ms=%.6f\n"),*OperationLabel,OperationFirstMs,(Now-OperationWarmWall)*1000./FMath::Max(uint64(1),GFrameCounter-OperationWarmFrames));
  Rows+=FString::Printf(TEXT("operation_actor=%s actor_tick_mean_ms=%.6f actor_samples=%llu\n"),*OperationLabel,(A->AnimationTickTotalMs-OperationActorCpuBase)/FMath::Max(uint64(1),A->AnimationTickSamples-OperationActorSamplesBase),A->AnimationTickSamples-OperationActorSamplesBase);
  TRACE_END_REGION(*OperationRegionName);OperationRegionName.Empty();
  // The diagnostic does not replace the complete post-load frame budget.
  if(!Budget(Mean,*OperationLabel))return;
  // Explicit sampling must remain exact even when called on a later frame
  // inside a registered, playing game world. This is outside the frame probe.
  const float Before=A->AnimPhase;A->AnimationReadyClock.Ready(GFrameCounter-1);A->Tick(.05f);
  const bool Exact=FMath::IsNearlyEqual(A->AnimPhase-Before,.05f,1.e-5f);
  Rows+=FString::Printf(TEXT("later_manual_tick_exact=%d operation=%s\n"),Exact,*OperationLabel);
  A->AnimationReadyClock.Stop();
  if(!Exact){Stop(TEXT("Explicit later-frame Tick was rebased as engine time"));return;}
  Stage=OperationNext;return;}
 if(Stage==1){
  const double LoadSeconds=FPlatformTime::Seconds()-LoadStartedAt;Rows+=FString::Printf(TEXT("card=C%03d outfit=%d load_seconds=%.6f cpu_skin=%d\n"),Card,Outfit,LoadSeconds,FPlatformMisc::GetEnvironmentVariable(TEXT("OK_CPU_SKIN"))==TEXT("1"));
  HairParts=0;double MinHair=1.e30;for(const auto& P:A->Parts)if(P.bRuntimeAccessory){OkMeshView View{};if(!A->Session.PartAt(P.SessionIndex,View))continue;const FString Name=View.name?UTF8_TO_TCHAR(View.name):TEXT("");if(!P.Role.Contains(TEXT("hair"))&&!Name.Contains(TEXT("hair"))&&!Name.Contains(TEXT("kami")))continue;++HairParts;for(const auto& V:P.BaseVertices)MinHair=FMath::Min(MinHair,V.Z);}
  if(Card==1&&(!HairParts||MinHair<40.)){Stop(FString::Printf(TEXT("Private hair remains near ground: parts=%d min_z=%.3f"),HairParts,MinHair));return;}
  WornTriangles=ClothingTriangles(A);if(!WornTriangles&&!WornOnly){Stop(TEXT("Missing worn clothing"));return;}
  A->SetPoseMode(0);
  const FString WalkProbe=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_READER_WALK_PROBE_TIME"));
  if(!WalkProbe.IsEmpty()){
   if(!A->SetGameAnimation(TEXT("studio/anime/00.unity3d|f_aruki_00_00_01"))){Stop(TEXT("Source walk probe clip unavailable"));return;}
   const float Time=FCString::Atof(*WalkProbe);if(!FMath::IsFinite(Time)||Time<0){Stop(TEXT("Invalid walk probe time"));return;}
   // Exercise repeated changing poses on one loaded actor; no display reload.
   for(int32 Step=0;Step<12;++Step)A->Tick(Time/12.f);
   if(!FMath::IsNearlyEqual(A->AnimPhase,Time,1.e-5f)){Stop(TEXT("Source walk probe did not advance animation clock"));return;}
   Rows+=FString::Printf(TEXT("source_walk_probe_seconds=%.6f actual_phase_seconds=%.6f steps=12\n"),Time,A->AnimPhase);
  }
  if(FPlatformMisc::GetEnvironmentVariable(TEXT("OK_READER_STREAM_PARITY"))==TEXT("1")&&!StreamParity(A))return;
  if(FPlatformMisc::GetEnvironmentVariable(TEXT("OK_READER_FREEZE_FACE"))==TEXT("1")){A->SetAutomaticBlink(false);A->SetActorTickEnabled(false);}
  if(FPlatformMisc::GetEnvironmentVariable(TEXT("OK_READER_EXPRESSION_PROBE"))==TEXT("1")){
   A->SetAutomaticBlink(false);A->SetMouthOpen(.65f);A->SetFaceControl(1,0,.35f,false);A->SetFaceControl(2,0,.6f,false);A->SetActorTickEnabled(false);
  }
  const FString GazeProbe=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_READER_GAZE_PROBE"));
  GazeCapture=0;
  if(!GazeProbe.IsEmpty()){
   TArray<FString> Values;GazeProbe.ParseIntoArray(Values,TEXT(","));if(Values.Num()!=3){Stop(TEXT("Gaze probe needs left,right,vertical"));return;}
   TInlineComponentArray<USkinnedMeshComponent*> Displays;A->GetComponents(Displays);TArray<USkinnedAsset*> Before;for(auto* C:Displays)Before.Add(C->GetSkinnedAsset());
   for(int32 I=0;I<8;++I){const auto& Rates=GazeCases[I];if(!A->SetEyeGaze(Rates[0],Rates[1],Rates[2])){Stop(TEXT("Gaze parameter update failed"));return;}}
   if(!A->SetEyeGaze(FCString::Atof(*Values[0]),FCString::Atof(*Values[1]),FCString::Atof(*Values[2]))){Stop(TEXT("Final gaze update failed"));return;}
   for(int32 I=0;I<Displays.Num();++I)if(Displays[I]->GetSkinnedAsset()!=Before[I]){Stop(TEXT("Gaze rebuilt a skeletal mesh"));return;}
   Rows+=TEXT("gaze_parameter_cases=9 skinned_assets_unchanged=1\n");
  }
  if(FPlatformMisc::GetEnvironmentVariable(TEXT("OK_READER_GAZE_SEQUENCE"))==TEXT("1"))
   if(!A->SetEyeGaze(0,0,0)){Stop(TEXT("Initial gaze capture failed"));return;}
  A->FrameCurrentPose();
  if(FPlatformMisc::GetEnvironmentVariable(TEXT("OK_READER_FACE_PORTRAIT"))==TEXT("1")){
   for(const auto& Part:A->Parts){
    OkMeshView Face{};if(!A->Session.PartAt(Part.SessionIndex,Face)||!Face.name||FCStringAnsi::Stricmp(Face.name,"cf_O_face"))continue;
    // CPU mesh views have prefab-local vertices/indices; GPU views have
    // shared-skeleton reference vertices/indices. Never feed the CPU view
    // through the shared-skeleton oracle to choose a comparison camera.
    OkSkinPaletteView Raw{};TArray<float> Palette;FBox Box(ForceInit);
    if(A->Session.SkinPalette(Part.SessionIndex,Raw,Palette)<=0||!Raw.positions){Stop(TEXT("Portrait source palette unavailable"));return;}
    for(int32 V=0;V<Raw.n_verts;++V){
     FMatrix Matrix;if(!OKExactPaletteMatrix(Raw,Palette,-V-1,Matrix)){Stop(TEXT("Portrait source palette invalid"));return;}
     const float* P=Raw.positions+V*3;Box+=FVector(Matrix.TransformPosition(FVector(-P[0],P[2],P[1])*100.));
    }
    if(!Box.IsValid){Stop(TEXT("Portrait source face envelope empty"));return;}
    A->OrbitFocus=A->GetActorTransform().TransformPosition(Box.GetCenter());A->OrbitDistance=55.f;A->OrbitPitch=0.f;A->ApplyOrbitCamera();
    Rows+=FString::Printf(TEXT("portrait_source_focus=%s distance_cm=%.6f yaw=%.6f\n"),*A->OrbitFocus.ToString(),A->OrbitDistance,A->OrbitYaw);break;
   }
  }
  else if(FPlatformMisc::GetEnvironmentVariable(TEXT("OK_READER_CLOSEUP"))==TEXT("1")){A->OrbitDistance*=.45f;A->ApplyOrbitCamera();}if(PC->PlayerCameraManager)PC->PlayerCameraManager->UpdateCamera(0);ViewReadyAt=W->GetTimeSeconds()+.5;ViewReadyFrame=GFrameCounter+4;Stage=11;
  if(!WalkProbe.IsEmpty()){if(FPlatformMisc::GetEnvironmentVariable(TEXT("OK_READER_SHAPE_TPOSE"))==TEXT("1"))A->SetPoseMode(0);ShapeProbeRequests=0;AnimationStart=FPlatformTime::Seconds();AnimationCpuBase=A->AnimationTickTotalMs;AnimationSampleBase=A->AnimationTickSamples;AnimationFrameBase=GFrameCounter;AnimationPhaseBase=A->AnimPhase;AnimationUploadBase=A->AnimationUploadedVertices;AnimationSkipBase=A->AnimationSkippedVertices;AnimationAimBase=PC->PaintAimQueries;A->AnimationTickMaxMs=0.;for(auto& Bin:A->AnimationTickHistogram)Bin=0;BeginProfileStage(TEXT("dry_edit"));UE_LOG(LogTemp,Display,TEXT("OK animation probe begin card=%d outfit=%d"),Card,Outfit);Stage=14;}return;
 }
 if(Stage==14){if(FPlatformMisc::GetEnvironmentVariable(TEXT("OK_READER_SHAPE_PROBE"))==TEXT("1")&&ShapeProbeRequests<uint64(G->Selected.Body.ShapeValueBody.Num()*6)){
    if(ShapeProbeRequests==0){ShapeProbeOriginal=G->Selected.Body.ShapeValueBody;A->ShapeUpdateTotalMs=0.;A->ShapeUpdateMaxMs=0.;A->ShapeLatencyMaxMs=0.;A->ShapeUpdateSamples=0;}
    if(!A->bShapeDirty&&FPlatformTime::Seconds()>=ShapeNextAt){
     ShapeNextAt=FPlatformTime::Seconds()+.05;const int32 Index=(ShapeProbeRequests/6)%G->Selected.Body.ShapeValueBody.Num();const float Value=.3f+.4f*float(ShapeProbeRequests%6)/5.f;
     auto* Match=G->ShapeSliders.FindByPredicate([Index](const auto& S){return S&&S->bBody&&S->Index==Index&&S->AnimationParameter.IsEmpty();});
     if(!Match||!(*Match)->ValueLabel){Stop(TEXT("Actual body slider/value label missing"));return;}
     (*Match)->SetValue(Value);(*Match)->OnValueChanged.Broadcast(Value);
     if(!(*Match)->ValueLabel->GetText().EqualTo(FText::AsNumber(FMath::RoundToInt(Value*100.f)))){Stop(TEXT("Cached reader body label did not update"));return;}
     ++ShapeProbeRequests;
    }
   }
   const FString ProbeSeconds=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_READER_ANIMATION_SECONDS"));const double Duration=ProbeSeconds.IsEmpty()?5.:FMath::Clamp(FCString::Atod(*ProbeSeconds),5.,120.);if(FPlatformTime::Seconds()-AnimationStart<Duration)return;
  EndProfileStage();UE_LOG(LogTemp,Display,TEXT("OK animation probe end card=%d outfit=%d"),Card,Outfit);
  Rows+=FString::Printf(TEXT("idle_paint_aim_queries=%llu\n"),PC->PaintAimQueries-AnimationAimBase);
  if(PC->PaintAimQueries!=AnimationAimBase){Stop(TEXT("Idle animation still queries paint aim surfaces"));return;}
  if(ShapeProbeRequests){Rows+=FString::Printf(TEXT("shape_requests=%llu shape_updates=%llu shape_update_mean_ms=%.6f shape_update_max_ms=%.6f shape_latency_max_ms=%.6f final_shape_matches=%d\n"),ShapeProbeRequests,A->ShapeUpdateSamples,A->ShapeUpdateTotalMs/FMath::Max(uint64(1),A->ShapeUpdateSamples),A->ShapeUpdateMaxMs,A->ShapeLatencyMaxMs,A->Session.ShapeBody()==G->Selected.Body.ShapeValueBody);
   auto* Cache=Cast<UInvalidationBox>(G->GetRootWidget());if(!Cache||!Cache->GetCanCache()){Stop(TEXT("Reader controls cache missing"));return;}
   Rows+=FString::Printf(TEXT("reader_controls_cache=1 actual_slider_events=%llu value_labels_match=1\n"),ShapeProbeRequests);
   if(ShapeProbeRequests<uint64(G->Selected.Body.ShapeValueBody.Num()*6)||A->bShapeDirty||A->Session.ShapeBody()!=G->Selected.Body.ShapeValueBody){Stop(TEXT("Shape edit did not publish final GUI parameters"));return;}
   const FString Budget=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_READER_SHAPE_BUDGET_MS"));if(!Budget.IsEmpty()&&A->ShapeUpdateTotalMs/FMath::Max(uint64(1),A->ShapeUpdateSamples)>FCString::Atod(*Budget)){Stop(TEXT("Shape update exceeded budget"));return;}
  }
  const uint64 Samples=A->AnimationTickSamples-AnimationSampleBase;
  if(Samples<20||(A->PoseMode!=0&&A->AnimPhase<=AnimationPhaseBase)){Stop(TEXT("Continuous animation did not advance"));return;}
  auto Percentile=[&](double P){uint64 Sum=0,Target=FMath::Max(uint64(1),uint64(FMath::CeilToDouble(Samples*P)));for(int32 I=0;I<256;++I){Sum+=A->AnimationTickHistogram[I];if(Sum>=Target)return I+1;}return 256;};uint64 Over33=0,Over50=0,Over100=0;for(int32 I=0;I<256;++I){if(I>=34)Over33+=A->AnimationTickHistogram[I];if(I>=50)Over50+=A->AnimationTickHistogram[I];if(I>=100)Over100+=A->AnimationTickHistogram[I];}
  Rows+=FString::Printf(TEXT("animation_histogram_ms=1 overflow_bin_ms=255 p50_upper_ms=%d p95_upper_ms=%d p99_upper_ms=%d ticks_ge_34ms=%llu ticks_ge_50ms=%llu ticks_ge_100ms=%llu\n"),Percentile(.5),Percentile(.95),Percentile(.99),Over33,Over50,Over100);
  int32 CpuSkinnedParts=0;
  if(!A->BodySkel){for(const auto& Part:A->Parts){OkMeshView View{};if(!Part.Comp||Part.bSlotAttached||!A->Session.PartAt(Part.SessionIndex,View))continue;
   if(View.skel_kind>=1&&View.skel_kind<=3){++CpuSkinnedParts;if(!Part.RigidFollowBone.IsNone()||!Part.Comp->GetComponentTransform().GetRelativeTransform(A->GetActorTransform()).Equals(FTransform::Identity,1e-6)){Rows+=FString::Printf(TEXT("bad_cpu_part=%d kind=%d role=%s rigid=%s relative=%s\n"),Part.SessionIndex,View.skel_kind,*Part.Role,*Part.RigidFollowBone.ToString(),*Part.Comp->GetRelativeTransform().ToHumanReadableString());Stop(TEXT("CPU skinned surface received a second rigid bone transform"));return;}}
  }}
  Rows+=FString::Printf(TEXT("cpu_skinned_parts=%d double_rigid_transform=0\n"),CpuSkinnedParts);
  int32 RigidParts=0,ArmRigidParts=0;double RigidError=0.,RigidMotion=0.;
  if(!A->BodySkel){for(const auto& Part:A->Parts){
   if(!Part.Comp||Part.RigidFollowBone.IsNone()||Part.bRuntimeAccessory||Part.bSlotAttached)continue;
   const FMatrix* Neutral=A->CpuRigidNeutralBones.Find(Part.RigidFollowBone);float Current[16];
   if(!Neutral||!A->Session.BoneWorldMatrix(Part.RigidFollowBone.ToString(),Current)){Stop(TEXT("CPU rigid accessory lost authored socket"));return;}
   const FMatrix Expected=Neutral->Inverse()*OKSkinPaletteToUnreal(Current);
   const auto* Section=Part.Comp->GetProcMeshSection(0);if(!Section){Stop(TEXT("CPU rigid draw section missing"));return;}
   const TArray<int32>* Map=Part.RuntimeDrawVertices.IsValidIndex(0)&&!Part.RuntimeDrawVertices[0].IsEmpty()?&Part.RuntimeDrawVertices[0]:nullptr;
   for(int32 D=0;D<Section->ProcVertexBuffer.Num();D+=17){const int32 V=Map?(*Map)[D]:D;const FVector P=Part.BaseVertices[V];const FVector Wanted=Expected.TransformPosition(P);RigidError=FMath::Max(RigidError,FVector::Dist(Wanted,A->GetActorTransform().InverseTransformPosition(Part.Comp->GetRenderMatrix().TransformPosition(Section->ProcVertexBuffer[D].Position))));RigidMotion=FMath::Max(RigidMotion,FVector::Dist(Wanted,P));}
   ++RigidParts;if(Part.RigidFollowBone.ToString().Contains(TEXT("a_n_arm_")))++ArmRigidParts;
  }}
  Rows+=FString::Printf(TEXT("cpu_rigid_parts=%d arm_rigid_parts=%d rigid_socket_error_cm=%.9f rigid_motion_cm=%.6f\n"),RigidParts,ArmRigidParts,RigidError,RigidMotion);
  if(RigidError>.01){Stop(TEXT("CPU rigid accessory diverged from animated socket"));return;}
  double Error=0.;int32 Vertices=0;
  for(const auto& Part:A->Parts){if(!Part.Comp||Part.bSlotAttached||A->Session.MorphCount(Part.SessionIndex)>0)continue;const auto* Section=Part.Comp->GetProcMeshSection(0);if(!Section)continue;const TArray<int32>* Map=Part.RuntimeDrawVertices.IsValidIndex(0)&&!Part.RuntimeDrawVertices[0].IsEmpty()?&Part.RuntimeDrawVertices[0]:nullptr;
   for(int32 Draw=0;Draw<Section->ProcVertexBuffer.Num();Draw+=17){const int32 Source=Map?(*Map)[Draw]:Draw;if(!Part.BaseVertices.IsValidIndex(Source)||!Part.RuntimeSectionOffsets.IsValidIndex(0)||!Part.RuntimeSectionOffsets[0].IsValidIndex(Source)||Part.RuntimeSectionOffsets[0][Source]!=FVector::ZeroVector)continue;Error=FMath::Max(Error,FVector::Dist(Section->ProcVertexBuffer[Draw].Position,Part.BaseVertices[Source]));++Vertices;}}
  if(!Vertices||Error>.001){Stop(TEXT("Live animation draw mapping diverged from canonical source vertices"));return;}
  Rows+=FString::Printf(TEXT("dry_animation_seconds=%.6f animation_samples=%llu animation_tick_mean_ms=%.6f animation_tick_max_ms=%.6f animation_frame_mean_ms=%.6f phase_start=%.6f phase_end=%.6f uploaded_vertices=%llu skipped_unchanged_vertices=%llu checked_vertices=%d draw_mapping_error_cm=%.9f\n"),FPlatformTime::Seconds()-AnimationStart,Samples,(A->AnimationTickTotalMs-AnimationCpuBase)/Samples,A->AnimationTickMaxMs,(FPlatformTime::Seconds()-AnimationStart)*1000/FMath::Max(uint64(1),GFrameCounter-AnimationFrameBase),AnimationPhaseBase,A->AnimPhase,A->AnimationUploadedVertices-AnimationUploadBase,A->AnimationSkippedVertices-AnimationSkipBase,Vertices,Error);if(!Budget((FPlatformTime::Seconds()-AnimationStart)*1000./FMath::Max(uint64(1),GFrameCounter-AnimationFrameBase),TEXT("Animation/editing frame")))return;if(FPlatformMisc::GetEnvironmentVariable(TEXT("OK_READER_STREAM_PARITY"))==TEXT("1")&&!MorphPruneParity(A))return;Stage=11;return;}
 if(Stage==11){if(GShaderCompilingManager){GShaderCompilingManager->ProcessAsyncResults(true,false);if(GShaderCompilingManager->IsCompiling())return;}if(W->GetTimeSeconds()<ViewReadyAt||GFrameCounter<ViewReadyFrame)return;
 const FString HidePart=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_READER_HIDE_PART"));
 const FString FaceDiagnostic=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_READER_GPU_FACE_DIAG"));
 if(!FaceDiagnostic.IsEmpty()){
  TInlineComponentArray<USkinnedMeshComponent*> Displays;A->GetComponents(Displays);
  for(auto& Part:A->Parts){if(Part.Role!=TEXT("head"))continue;
   OkMeshView DiagnosticView{};A->Session.PartAt(Part.SessionIndex,DiagnosticView);const bool IsFace=DiagnosticView.name&&FCStringAnsi::Stricmp(DiagnosticView.name,"cf_O_face")==0;
   if((FaceDiagnostic==TEXT("proxyface")&&!IsFace)||(FaceDiagnostic==TEXT("proxyfeatures")&&IsFace))continue;
   auto** Found=Displays.FindByPredicate([&](auto* C){return C->IsVisible()&&(C->GetName()==FString::Printf(TEXT("Mouth_%d"),Part.SessionIndex)||C->GetName()==FString::Printf(TEXT("Skel_%d"),Part.SessionIndex));});if(!Found)continue;auto* Skin=*Found;
   Rows+=FString::Printf(TEXT("face_diag=%s component=%s bounds=%s radius=%.6f\n"),*FaceDiagnostic,*Skin->GetName(),*Skin->Bounds.Origin.ToString(),Skin->Bounds.SphereRadius);
   if(FaceDiagnostic==TEXT("bounds")){Skin->SetBoundsScale(100.f);Skin->MarkRenderStateDirty();}
   if(FaceDiagnostic==TEXT("neutral"))Skin->SetMaterial(0,UMaterial::GetDefaultMaterial(MD_Surface));
   if(FaceDiagnostic==TEXT("twosided")&&!IsFace){auto* Material=Skin->GetMaterial(0)->GetMaterial();Material->TwoSided=true;Material->PostEditChange();}
   if(FaceDiagnostic==TEXT("noz")&&!IsFace){auto* Material=Skin->GetMaterial(0)->GetMaterial();Material->BlendMode=BLEND_Translucent;Material->bDisableDepthTest=true;Material->PostEditChange();}
   if(FaceDiagnostic.StartsWith(TEXT("proxy"))){FOKPaintPose Pose;if(!Pose.Prepare(Skin))continue;TArray<FVector> Positions;for(int32 I=0;I<Part.BaseVertices.Num();++I)Positions.Add(Skin->GetComponentTransform().InverseTransformPosition(Pose.Position(I)));auto* Proxy=NewObject<UProceduralMeshComponent>(A);Proxy->SetupAttachment(A->GetRootComponent());Proxy->RegisterComponent();Proxy->CreateMeshSection(0,Positions,Part.Triangles,Part.BaseNormals,Part.UVs,Part.Colors,TArray<FProcMeshTangent>(),false);Proxy->SetMaterial(0,Skin->GetMaterial(0));Skin->SetVisibility(false);Skin->SetHiddenInGame(true);}
  }
 }
 if(!HidePart.IsEmpty())for(auto& Part:A->Parts){OkMeshView M{};if(!A->Session.PartAt(Part.SessionIndex,M)||!M.name||FString(UTF8_TO_TCHAR(M.name))!=HidePart)continue;if(FPlatformMisc::GetEnvironmentVariable(TEXT("OK_READER_HIDE_OUTLINE_ONLY"))==TEXT("1")){if(Part.Comp)for(int32 Section=1;Section<Part.Comp->GetNumSections();++Section)UKKAffineProceduralMeshComponent::SetStreamSectionVisible(Part.Comp,Section,false);}else{if(Part.Comp)Part.Comp->SetVisibility(false);TInlineComponentArray<USkinnedMeshComponent*> Displays;A->GetComponents(Displays);for(auto* Display:Displays)if(Display->GetName()==FString::Printf(TEXT("Mouth_%d"),Part.SessionIndex)||Display->GetName()==FString::Printf(TEXT("Skel_%d"),Part.SessionIndex))Display->SetVisibility(false);}}
 if(FPlatformMisc::GetEnvironmentVariable(TEXT("OK_READER_SKIN_AUDIT"))==TEXT("1")){
  FString ProcData=TEXT("part\tvertex\tx\ty\tz\n");for(const auto& Part:A->Parts){if(!Part.Comp||!Part.Role.StartsWith(TEXT("clothes")))continue;auto* Section=Part.Comp->GetProcMeshSection(0);if(!Section)continue;OkMeshView M{};if(!A->Session.PartAt(Part.SessionIndex,M)||!M.name)continue;for(int32 I=0;I<Section->ProcVertexBuffer.Num();++I){const FVector P=Part.Comp->GetRenderMatrix().TransformPosition(Section->ProcVertexBuffer[I].Position);ProcData+=FString::Printf(TEXT("%s\t%d\t%.9f\t%.9f\t%.9f\n"),UTF8_TO_TCHAR(M.name),I,P.X,P.Y,P.Z);}}FFileHelper::SaveStringToFile(ProcData,*(Output/FString::Printf(TEXT("C%03d_outfit%02d_proc.tsv"),Card,Outfit)));
  FString Data=TEXT("part\tcomponent\trole\tvertices\tmax_error_cm\tmesh_object\tvisible\tbone_order_mismatches\tsource_min\tsource_max\tdisplay_min\tdisplay_max\n");TInlineComponentArray<USkinnedMeshComponent*> Skins;A->GetComponents(Skins);
  for(const auto& Part:A->Parts){auto* Match=Skins.FindByPredicate([&](auto* S){return S->GetName()==FString::Printf(TEXT("Mouth_%d"),Part.SessionIndex);});if(!Match)Match=Skins.FindByPredicate([&](auto* S){return S->GetName()==FString::Printf(TEXT("Skel_%d"),Part.SessionIndex);});if(!Match)continue;
   auto* Asset=(*Match)->GetSkinnedAsset();if(!Asset)continue;auto* RD=Asset->GetResourceForRendering();if(!RD||RD->LODRenderData.IsEmpty())continue;auto& LOD=RD->LODRenderData[0];TArray<FMatrix44f> Matrices;(*Match)->GetCurrentRefToLocalMatrices(Matrices,0);TArray<FVector3f> V;USkinnedMeshComponent::ComputeSkinnedPositions(*Match,V,Matrices,LOD,LOD.SkinWeightVertexBuffer);FBox Source(ForceInit),Display(ForceInit);double Error=0;for(const auto& P:Part.BaseVertices)Source+=P;
   double Outside=0;const FBox ActualBounds=(*Match)->Bounds.GetBox();FOKPaintPose MorphPose;const bool HaveMorphPose=MorphPose.Prepare(*Match);for(int32 I=0;I<V.Num();++I){const FVector P=HaveMorphPose?(*Match)->GetComponentTransform().InverseTransformPosition(MorphPose.Position(I)):FVector(V[I]);Display+=P;if(Part.BaseVertices.IsValidIndex(I))Error=FMath::Max(Error,FVector::Dist(P,Part.BaseVertices[I]));const FVector WorldP=(*Match)->GetComponentTransform().TransformPosition(P);Outside=FMath::Max(Outside,FVector::Dist(WorldP,ActualBounds.GetClosestPointTo(WorldP)));}
   Rows+=FString::Printf(TEXT("skin_bounds component=%s visible=%d outside_cm=%.9f radius_cm=%.9f\n"),*(*Match)->GetName(),(*Match)->IsVisible(),Outside,(*Match)->Bounds.SphereRadius);
   // Separate the source matrix skinning error from active expression morphs.
   // Compare the same reference vertices before adding either path's morphs.
   OkMeshView PaletteView{};
   if(A->Session.PartAt(Part.SessionIndex,PaletteView)){
    const auto& Ref=Asset->GetRefSkeleton();TSet<int32> UsedBones;for(const auto& Section:LOD.RenderSections)for(auto Bone:Section.BoneMap)UsedBones.Add(Bone);TArray<FMatrix44f> RestWorld,PoseWorld;RestWorld.SetNum(Ref.GetNum());PoseWorld.SetNum(Ref.GetNum());double RestError=0,PoseError=0;FString WorstBone;
    for(int32 B=0;B<Ref.GetNum();++B){OkBoneView Bone{};if(!A->Session.SkelBoneAt(B,Bone))continue;const int32 Parent=Ref.GetParentIndex(B);RestWorld[B]=FMatrix44f(OKUnityLocalToUnreal(Bone.rest_pos,Bone.rest_rot,Bone.rest_scl).ToMatrixWithScale());PoseWorld[B]=FMatrix44f(OKUnityLocalToUnreal(Bone.pose_pos,Bone.pose_rot,Bone.pose_scl).ToMatrixWithScale());if(Parent>=0){RestWorld[B]=RestWorld[B]*RestWorld[Parent];PoseWorld[B]=PoseWorld[B]*PoseWorld[Parent];}
     const auto& Inv=Asset->GetRefBasesInvMatrix();if(!UsedBones.Contains(B)||!Inv.IsValidIndex(B)||!Matrices.IsValidIndex(B))continue;const FMatrix44f SourceInv=RestWorld[B].Inverse(),SourcePalette=SourceInv*PoseWorld[B];double E=0;for(int32 R=0;R<4;++R)for(int32 C=0;C<4;++C){RestError=FMath::Max(RestError,double(FMath::Abs(Inv[B].M[R][C]-SourceInv.M[R][C])));E=FMath::Max(E,double(FMath::Abs(Matrices[B].M[R][C]-SourcePalette.M[R][C])));}if(E>PoseError){PoseError=E;WorstBone=Ref.GetBoneName(B).ToString();}}
    Rows+=FString::Printf(TEXT("skin_matrix component=%s inverse_bind_max_element_error=%.9f palette_max_element_error=%.9f worst_bone=%s\n"),*(*Match)->GetName(),RestError,PoseError,*WorstBone);
    TArray<FVector> SourceSkin;
    if(OKDeformFromRef(A->Session,PaletteView,SourceSkin)&&SourceSkin.Num()==V.Num()){
     double PaletteError=0,SourceBaselineError=0;int32 WorstVertex=INDEX_NONE;
     for(int32 I=0;I<V.Num();++I){const double E=FVector::Dist(SourceSkin[I],FVector(V[I]));if(E>PaletteError){PaletteError=E;WorstVertex=I;}if(Part.BaseVertices.IsValidIndex(I))SourceBaselineError=FMath::Max(SourceBaselineError,FVector::Dist(SourceSkin[I],Part.BaseVertices[I]));}
     Rows+=FString::Printf(TEXT("skin_palette component=%s max_error_cm=%.9f source_baseline_error_cm=%.9f worst_vertex=%d\n"),*(*Match)->GetName(),PaletteError,SourceBaselineError,WorstVertex);
     if(FPlatformMisc::GetEnvironmentVariable(TEXT("OK_READER_REQUIRE_SKIN_PALETTE"))==TEXT("1")&&PaletteError>.003){Stop(TEXT("GPU reference-vertex skinning diverged from source affine palette"));return;}
    }
   }
   if((*Match)->IsVisible()&&Outside>.05&&FPlatformMisc::GetEnvironmentVariable(TEXT("OK_READER_REQUIRE_SKIN_BOUNDS"))==TEXT("1")){Stop(TEXT("Displayed skinned vertices escaped renderer bounds"));return;}
   int32 BoneMismatch=0;const auto& Ref=(*Match)->GetSkinnedAsset()->GetRefSkeleton();for(int32 B=0;B<Ref.GetNum();++B){OkBoneView Bone{};if(!A->Session.SkelBoneAt(B,Bone)||!Bone.name||Ref.GetBoneName(B)!=FName(UTF8_TO_TCHAR(Bone.name)))++BoneMismatch;}OkMeshView M{};A->Session.PartAt(Part.SessionIndex,M);Data+=FString::Printf(TEXT("%s\t%s\t%s\t%d\t%.9f\t%d\t%d\t%d\t%s\t%s\t%s\t%s\n"),M.name?UTF8_TO_TCHAR(M.name):TEXT(""),*(*Match)->GetName(),*Part.Role,V.Num(),Error,(*Match)->GetMeshObject()!=nullptr,(*Match)->IsVisible(),BoneMismatch,*Source.Min.ToString(),*Source.Max.ToString(),*Display.Min.ToString(),*Display.Max.ToString());
   if(M.eye_draw){const auto& Buffer=LOD.StaticVertexBuffers.StaticMeshVertexBuffer;double UvError=0;const float* Channels[]={M.eye_sample_uvs?M.eye_sample_uvs:M.uvs,M.uv2,M.uv3};
    if(Buffer.GetNumTexCoords()<3){Stop(TEXT("GPU eye is missing authored highlight UV channels"));return;}
    for(int32 Channel=0;Channel<3;++Channel)if(Channels[Channel])for(int32 I=0;I<M.n_verts;++I){const FVector2f U=Buffer.GetVertexUV(I,Channel);FVector2D Sample(U);
     if(Part.GpuEyeMaterial){const auto X=Part.GpuEyeMaterial->K2_GetVectorParameterValue(*FString::Printf(TEXT("KKEyeUV%dRow0"),Channel));const auto Y=Part.GpuEyeMaterial->K2_GetVectorParameterValue(*FString::Printf(TEXT("KKEyeUV%dRow1"),Channel));Sample=FVector2D(X.R*U.X+X.G*U.Y+X.B,Y.R*U.X+Y.G*U.Y+Y.B);}
     const FVector2D Target(Channels[Channel][I*2],1.f-Channels[Channel][I*2+1]);
     UvError=FMath::Max(UvError,FVector2D::Distance(Sample,Target));}
    Rows+=FString::Printf(TEXT("eye_uv component=%s channels=%d max_error=%.9f\n"),*(*Match)->GetName(),Buffer.GetNumTexCoords(),UvError);
    if(UvError>.002){Stop(TEXT("GPU eye coordinates differ from source"));return;}
   }
  }
  // The inverted face hull must use the surface's pose and morph weights.
  // Check actual transformed vertices, not just equal parameter values.
  for(auto* Shell:Skins){if(!Shell->GetName().StartsWith(TEXT("FaceOutline_")))continue;
   const FString Index=Shell->GetName().RightChop(12);auto* Surface=Skins.FindByPredicate([&](auto* S){return S->GetName()==TEXT("Mouth_")+Index;});
   FOKPaintPose ShellPose,SurfacePose;if(!Surface||!ShellPose.Prepare(Shell)||!SurfacePose.Prepare(*Surface)){Stop(TEXT("Face outline pose unavailable"));return;}
   const auto* RD=Shell->GetSkinnedAsset()->GetResourceForRendering();const int32 N=RD->LODRenderData[0].GetNumVertices();double Error=0,Outside=0;
   const FBox Bounds=Shell->Bounds.GetBox();for(int32 I=0;I<N;++I){const FVector P=ShellPose.Position(I);Error=FMath::Max(Error,FVector::Dist(P,SurfacePose.Position(I)));Outside=FMath::Max(Outside,FVector::Dist(P,Bounds.GetClosestPointTo(P)));}
   Rows+=FString::Printf(TEXT("face_outline component=%s vertices=%d pose_error_cm=%.9f outside_cm=%.9f\n"),*Shell->GetName(),N,Error,Outside);
   if(Error>.001||Outside>.05){Stop(TEXT("Face outline escaped bounds or diverged from surface pose"));return;}
  }
  FFileHelper::SaveStringToFile(Data,*(Output/FString::Printf(TEXT("C%03d_outfit%02d_skin.tsv"),Card,Outfit)));
 }
 JetCaptured=false;TWeakPtr<FRun> Weak=AsShared();ScreenshotHandle=FScreenshotRequest::OnScreenshotRequestProcessed().AddLambda([Weak](){if(auto Run=Weak.Pin())Run->JetCaptured=true;});
 const FString CaptureName=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_READER_GAZE_SEQUENCE"))==TEXT("1")?FString::Printf(TEXT("C%03d_outfit%02d_gaze%02d_Worn.png"),Card,Outfit,GazeCapture):(AllOutfits?FString::Printf(TEXT("C%03d_outfit%02d_Worn.png"),Card,Outfit):FString::Printf(TEXT("C%03d_Worn.png"),Card));
 FScreenshotRequest::RequestScreenshot(Output/CaptureName,false,false);Stage=10;return;}
 if(Stage==10){if(!JetCaptured)return;FScreenshotRequest::OnScreenshotRequestProcessed().Remove(ScreenshotHandle);
  if(FPlatformMisc::GetEnvironmentVariable(TEXT("OK_READER_GAZE_SEQUENCE"))==TEXT("1")&&GazeCapture<8){
   const auto& Rates=GazeCases[++GazeCapture];if(!A->SetEyeGaze(Rates[0],Rates[1],Rates[2])){Stop(TEXT("Gaze capture update failed"));return;}
   ViewReadyAt=W->GetTimeSeconds()+.5;ViewReadyFrame=GFrameCounter+4;Stage=11;return;
  }
  if(!WornOnly){PC->InputKey(FInputKeyEventArgs(nullptr,INPUTDEVICEID_NONE,EKeys::A,IE_Pressed,0));if(!PC->PaintAimHeld||!PC->UpdatePaintGesture(true,true,true,false)){Stop(TEXT("A + viewport left gesture did not arm"));return;}PC->SetActorTickEnabled(false);SprayAt=W->GetTimeSeconds();AimMesh.Reset();AimTriangle=INDEX_NONE;ActiveWall=FPlatformTime::Seconds();ActiveFrames=GFrameCounter;ActivePaintCpu=PC->PaintSprayer->TotalUpdateMs;ActivePaintSamples=PC->PaintSprayer->UpdateSamples;PC->PaintSprayer->MaxUpdateMs=0;BeginProfileStage(TEXT("spray"));Stage=2;return;}
  Rows+=FString::Printf(TEXT("card=C%03d outfit=%d hair_parts=%d worn_triangles=%llu worn_only=1 offline_load=1\n"),Card,Outfit,HairParts,WornTriangles);if(AllOutfits&&++Outfit<A->Session.CoordinateCount()){LoadStartedAt=FPlatformTime::Seconds();G->OnOutfitNext();if(!G->bLoadInFlight){Stop(TEXT("Outfit UI did not dispatch a new load"));return;}Stage=1;return;}Outfit=0;if(++CardPosition<Cards.Num()){Card=Cards[CardPosition];Stage=0;return;}Stop(FString());return;}
 if(Stage==2){
  auto* Paint=PC->PaintSprayer.Get();PeakPackets=FMath::Max(PeakPackets,Paint->LiveSimulationParticles());PeakFX=FMath::Max(PeakFX,Paint->LiveImpactEffects());
  if(PeakPackets>Paint->Settings.MaxSimulationParticles||PeakFX>Paint->Settings.MaxImpactEffects){Stop(TEXT("Paint particle budget exceeded"));return;}
  FVector Origin;FRotator Rotation;PC->GetPlayerViewPoint(Origin,Rotation);if(A->StudioCam){Origin=A->StudioCam->GetActorLocation();Rotation=A->StudioCam->GetActorRotation();}
  FVector Target=A->OrbitFocus;
  const bool ArmProbe=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_READER_PAINT_ARM"))==TEXT("1");
  if(ArmProbe){FBox BodyBox(ForceInit);TArray<FVector> BodyPoints;
   TInlineComponentArray<UPrimitiveComponent*> Displays;A->GetComponents(Displays);
   for(auto* C:Displays){if(!C->IsVisible()||!C->bRenderInMainPass)continue;
    const bool Body=A->Parts.ContainsByPredicate([C](const auto& Part){return Part.Role.Contains(TEXT("body"))&&(Part.Comp==C||(Part.SurfaceMat&&Part.SurfaceMat==C->GetMaterial(0)));})||A->MouthSlots.ContainsByPredicate([C,A](const auto& Slot){return (Slot.Comp==C||Slot.CpuComp==C)&&A->Parts.ContainsByPredicate([&Slot](const auto& Part){return Part.SessionIndex==Slot.SessionPartIndex&&Part.Role.Contains(TEXT("body"));});});
    if(!Body)continue;
    if(auto* Skin=Cast<USkinnedMeshComponent>(C)){FOKPaintPose Pose;if(Pose.Prepare(Skin))for(uint32 I=0;I<Pose.LOD->GetNumVertices();++I){const FVector P=Pose.Position(I);BodyBox+=P;BodyPoints.Add(P);}}
    else if(auto* Proc=Cast<UProceduralMeshComponent>(C))if(const auto* Part=Proc->GetProcMeshSection(0))for(const auto& V:Part->ProcVertexBuffer){const FVector P=C->GetComponentTransform().TransformPosition(V.Position);BodyBox+=P;BodyPoints.Add(P);}
   }
   if(!BodyBox.IsValid){Stop(TEXT("Arm probe has no body geometry"));return;}
   const FVector Right=FRotationMatrix(Rotation).GetUnitAxis(EAxis::Y);const double Reach=FMath::Abs(Right.X)*BodyBox.GetExtent().X+FMath::Abs(Right.Y)*BodyBox.GetExtent().Y;
   FVector Desired=BodyBox.GetCenter()+Right*Reach*.65;Desired.Z=BodyBox.Min.Z+BodyBox.GetSize().Z*.84;
   double Best=DBL_MAX;for(const FVector& P:BodyPoints){const double D=FVector::DistSquared(P,Desired);if(D<Best){Best=D;Target=P;}}ArmTarget=Target;
  }
  const FString SweepMode=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_READER_PAINT_SWEEP"));
  if(!ArmProbe&&SweepMode.IsEmpty()){
   if(!AimMesh.IsValid()){
    FBox BodyBounds(ForceInit);
    for(const auto& P:A->Parts)if(P.Comp&&P.Role.Contains(TEXT("body")))for(const auto& V:P.BaseVertices)BodyBounds+=P.Comp->GetRenderMatrix().TransformPosition(V);
    FVector AimCenter=A->OrbitFocus;
    if(BodyBounds.IsValid){AimCenter=BodyBounds.GetCenter();AimCenter.Z=BodyBounds.Min.Z+BodyBounds.GetSize().Z*.72;}
    double Best=DBL_MAX;
    for(const auto& P:A->Parts)if(P.Comp&&P.Comp->IsVisible()&&P.Role.StartsWith(TEXT("clothes")))if(const auto* Section=P.Comp->GetProcMeshSection(0)){
     const FMatrix Transform=P.Comp->GetRenderMatrix();
     // Use triangle interiors rather than a moving UV/seam vertex. Source
     // normal winding is not a reliable visibility test for imported clothes.
     for(int32 I=0;I+2<Section->ProcIndexBuffer.Num();I+=3){
      const FVector V0=Section->ProcVertexBuffer[Section->ProcIndexBuffer[I]].Position,V1=Section->ProcVertexBuffer[Section->ProcIndexBuffer[I+1]].Position,V2=Section->ProcVertexBuffer[Section->ProcIndexBuffer[I+2]].Position;
      const FVector WorldNormal=FVector::CrossProduct(Transform.TransformVector(V1-V0),Transform.TransformVector(V2-V0)).GetSafeNormal();
      // The downhill probe needs a slope. A horizontal skirt panel can
      // correctly retain paint, so it cannot prove gravity-driven transport.
      if(WorldNormal.IsNearlyZero()||FMath::Abs(WorldNormal.Z)>.6)continue;
      const FVector Local=(V0+V1+V2)/3.;
      const FVector WorldPoint=Transform.TransformPosition(Local);
      const double Score=FVector::DistSquared(WorldPoint,AimCenter)+.05*FVector::DistSquared(WorldPoint,Origin);if(Score<Best){Best=Score;AimMesh=P.Comp;AimTriangle=I;}
     }
    }
   }
   if(auto* Mesh=AimMesh.Get())if(const auto* Section=Mesh->GetProcMeshSection(0))if(AimTriangle>=0&&AimTriangle+2<Section->ProcIndexBuffer.Num()){
    FVector Local=FVector::ZeroVector;for(int32 E=0;E<3;++E)Local+=Section->ProcVertexBuffer[Section->ProcIndexBuffer[AimTriangle+E]].Position/3.;Target=Mesh->GetRenderMatrix().TransformPosition(Local);
   }
  }
  if(SweepMode==TEXT("1")||SweepMode==TEXT("2"))Target+=FVector(0,0,SweepMode==TEXT("2")?70:45)+FRotationMatrix(Rotation).GetUnitAxis(EAxis::Y)*((SweepMode==TEXT("2")?8:12)*FMath::Sin((W->GetTimeSeconds()-SprayAt)*4));
  const FVector Nozzle=Origin+Rotation.Vector()*40.+FRotationMatrix(Rotation).GetUnitAxis(EAxis::Y)*12.-FRotationMatrix(Rotation).GetUnitAxis(EAxis::Z)*8.;
  auto* S=PC->PaintSprayer.Get();int32 AimSurface,AimFace;FVector AimBary;double AimTime;S->FindPaintHit(Origin,Origin+(Target-Origin).GetSafeNormal()*5000.,.05,AimSurface,AimFace,AimBary,AimTime);++PC->PaintAimQueries;S->Settings.ScatterDegrees=0;S->Settings.JetOpacity=Card==0?1.f:.35f;S->SetSpray(true,Nozzle,AOKPaintSprayer::AimDirection(Nozzle,Target,S->Settings.JetSpeed));
  TInlineComponentArray<UNiagaraComponent*> FX;S->GetComponents(FX);bool Jet=false;for(auto* C:FX)if(C->GetAsset()&&C->GetAsset()->GetName()==TEXT("NS_PaintJet"))Jet=C->IsActive()&&C->IsVisible()&&C->GetAsset()->IsReadyToRun();
  if(!Jet||S->IsHidden()){Stop(TEXT("Niagara jet missing/inactive/not compiled or sprayer actor hidden"));return;}
  if(W->GetTimeSeconds()-SprayAt>3){EndProfileStage();const double PaintMean=(S->TotalUpdateMs-ActivePaintCpu)/FMath::Max(uint64(1),S->UpdateSamples-ActivePaintSamples),FrameMean=(FPlatformTime::Seconds()-ActiveWall)*1000./FMath::Max(uint64(1),GFrameCounter-ActiveFrames);Rows+=FString::Printf(TEXT("active_spray_seconds=3 paint_mean_ms=%.6f paint_max_ms=%.6f frame_mean_ms=%.6f\n"),PaintMean,S->MaxUpdateMs,FrameMean);if(!Budget(PaintMean,TEXT("Active paint simulation"))||!Budget(FrameMean,TEXT("Active spray frame")))return;ClothingCoatings=0;TInlineComponentArray<UProceduralMeshComponent*> Layers;S->GetComponents(Layers);
   for(auto* Layer:Layers)if(Layer->ComponentHasTag(TEXT("OKPaintOverlay"))&&Layer->GetAttachParent()&&Layer->GetProcMeshSection(0))for(const auto& Part:A->Parts)if(Part.Role.StartsWith(TEXT("clothes"))&&Part.Comp==Layer->GetAttachParent())++ClothingCoatings;
   if(!ClothingCoatings&&!ArmProbe&&SweepMode!=TEXT("2")){Stop(TEXT("Jet did not generate an attached clothing paint layer"));return;}
   Deposit=S->Deposited;if(Deposit<=0||S->HitCount<=0){Stop(FString::Printf(TEXT("Offset visible jet failed: nozzle=%s target=%s stats=%s"),*Nozzle.ToString(),*Target.ToString(),*S->Statistics()));return;}FVector CameraOrigin;FRotator CameraRotation;PC->GetPlayerViewPoint(CameraOrigin,CameraRotation);FVector2D NozzleScreen,TargetScreen;PC->ProjectWorldLocationToScreen(Nozzle,NozzleScreen);PC->ProjectWorldLocationToScreen(Target,TargetScreen);UE_LOG(LogTemp,Display,TEXT("Reader jet camera=%s rot=%s nozzle=%s target=%s nozzle_screen=%s target_screen=%s sprayer_hidden=%d fx_world=%s"),*CameraOrigin.ToString(),*CameraRotation.ToString(),*Nozzle.ToString(),*Target.ToString(),*NozzleScreen.ToString(),*TargetScreen.ToString(),S->IsHidden(),FX.Num()?*FX[0]->GetComponentLocation().ToString():TEXT("none"));JetCaptured=false;TWeakPtr<FRun> Weak=AsShared();ScreenshotHandle=FScreenshotRequest::OnScreenshotRequestProcessed().AddLambda([Weak](){if(auto Run=Weak.Pin())Run->JetCaptured=true;});FScreenshotRequest::RequestScreenshot(Output/FString::Printf(TEXT("C%03d_Jet.png"),Card),false,false);Stage=3;}return;
 }
 if(Stage==3){if(!JetCaptured)return;FScreenshotRequest::OnScreenshotRequestProcessed().Remove(ScreenshotHandle);PC->InputKey(FInputKeyEventArgs(nullptr,INPUTDEVICEID_NONE,EKeys::A,IE_Released,0));PC->PaintSprayer->SetSpray(false,FVector::ZeroVector,FVector::ForwardVector);if(!PC->PaintSprayer->FilmCenterOfMass(FlowStart,FlowVolumeStart)){Stop(TEXT("Missing deposited film"));return;}if(FPlatformMisc::GetEnvironmentVariable(TEXT("OK_READER_PAINT_ARM"))==TEXT("1")){A->OrbitFocus=ArmTarget;A->OrbitDistance=55;A->ApplyOrbitCamera();}ReleasedCpuBase=PC->PaintSprayer->TotalUpdateMs;ReleasedSampleBase=PC->PaintSprayer->UpdateSamples;ReleasedFrameBase=GFrameCounter;ReleasedWallStart=FPlatformTime::Seconds();PC->PaintSprayer->MaxUpdateMs=0;FlowAt=W->GetTimeSeconds();PaintCpuSum=PaintCpuMax=PaintFrameSum=0;PaintSamples=0;BeginProfileStage(TEXT("released"));Stage=12;return;}
 if(Stage==12){auto* Paint=PC->PaintSprayer.Get();PaintCpuSum+=Paint->LastUpdateMs;PaintCpuMax=FMath::Max(PaintCpuMax,Paint->LastUpdateMs);PaintFrameSum+=W->GetDeltaSeconds()*1000;++PaintSamples;PeakPackets=FMath::Max(PeakPackets,Paint->LiveSimulationParticles());PeakFX=FMath::Max(PeakFX,Paint->LiveImpactEffects());if(PeakPackets>Paint->Settings.MaxSimulationParticles||PeakFX>Paint->Settings.MaxImpactEffects){Stop(TEXT("Released paint particle budget exceeded"));return;}const double SettleSeconds=FMath::Clamp(FCString::Atof(*FPlatformMisc::GetEnvironmentVariable(TEXT("OK_READER_PAINT_SETTLE_SECONDS"))),5.,30.);if(W->GetTimeSeconds()-FlowAt<SettleSeconds)return;EndProfileStage();Rows+=FString::Printf(TEXT("spray_seconds=3 settle_seconds=%.0f released_paint_mean_ms=%.6f released_paint_max_ms=%.6f released_frame_mean_ms=%.6f peak_packets=%d peak_impact_fx=%d jet_particle_limit=%d\n"),SettleSeconds,(Paint->TotalUpdateMs-ReleasedCpuBase)/FMath::Max(uint64(1),Paint->UpdateSamples-ReleasedSampleBase),Paint->MaxUpdateMs,(FPlatformTime::Seconds()-ReleasedWallStart)*1000/FMath::Max(uint64(1),GFrameCounter-ReleasedFrameBase),PeakPackets,PeakFX,Paint->Settings.MaxJetParticles);if(!Budget((Paint->TotalUpdateMs-ReleasedCpuBase)/FMath::Max(uint64(1),Paint->UpdateSamples-ReleasedSampleBase),TEXT("Released paint simulation"))||!Budget((FPlatformTime::Seconds()-ReleasedWallStart)*1000./FMath::Max(uint64(1),GFrameCounter-ReleasedFrameBase),TEXT("Released spray frame")))return;if(!PC->PaintSprayer->FilmCenterOfMass(FlowEnd,FlowVolumeEnd)||FlowEnd.Z>=FlowStart.Z-.01||FMath::Abs(PC->PaintSprayer->ConservationError())>1.e-5){Stop(FString::Printf(TEXT("Film did not flow down/mass mismatch: start=%s end=%s error=%.9f"),*FlowStart.ToString(),*FlowEnd.ToString(),PC->PaintSprayer->ConservationError()));return;}JetCaptured=false;TWeakPtr<FRun> Weak=AsShared();ScreenshotHandle=FScreenshotRequest::OnScreenshotRequestProcessed().AddLambda([Weak](){if(auto Run=Weak.Pin())Run->JetCaptured=true;});FScreenshotRequest::RequestScreenshot(Output/FString::Printf(TEXT("C%03d_Flow5s.png"),Card),false,false);Stage=13;return;}
 if(Stage==13){if(!JetCaptured)return;FScreenshotRequest::OnScreenshotRequestProcessed().Remove(ScreenshotHandle);Rows+=FString::Printf(TEXT("card=C%03d flow_seconds=%.0f film_z_start=%.6f film_z_end=%.6f film_volume_start=%.6f film_volume_end=%.6f conservation_error=%.9f\n"),Card,FMath::Clamp(FCString::Atof(*FPlatformMisc::GetEnvironmentVariable(TEXT("OK_READER_PAINT_SETTLE_SECONDS"))),5.,30.),FlowStart.Z,FlowEnd.Z,FlowVolumeStart,FlowVolumeEnd,PC->PaintSprayer->ConservationError());PC->PaintSprayer->ClearPaint();PC->UpdatePaintGesture(false,true,true,false);PC->SetActorTickEnabled(true);G->OnClothesState(101);if(!G->bLoadInFlight){Stop(TEXT("Half-undress reused worn actor"));return;}uint8 Pending[9];FMemory::Memcpy(Pending,G->ClothesStates,sizeof(Pending));G->OnClothesState(102);if(FMemory::Memcmp(Pending,G->ClothesStates,sizeof(Pending))!=0){Stop(TEXT("Busy clothing callback changed pending state"));return;}Stage=4;return;}
 if(Stage==4){HalfTriangles=ClothingTriangles(A);if(FPlatformMisc::GetEnvironmentVariable(TEXT("OK_READER_OPERATION_PROBE"))==TEXT("1")){BeginOperation(A,TEXT("half_loaded"),16);return;}Stage=16;return;}
 if(Stage==16){G->OnClothesState(102);if(!G->bLoadInFlight){Stop(TEXT("Full removal reused half actor"));return;}Stage=5;return;}
 if(Stage==5){RemovedTriangles=ClothingTriangles(A);if(RemovedTriangles>=WornTriangles){Stop(TEXT("Full removal did not reduce actual clothing triangles"));return;}if(FPlatformMisc::GetEnvironmentVariable(TEXT("OK_READER_OPERATION_PROBE"))==TEXT("1")){BeginOperation(A,TEXT("removed_loaded"),17);return;}Stage=17;return;}
 if(Stage==17){G->OnClothesState(100);Stage=6;return;}
 if(Stage==6){if(ClothingTriangles(A)!=WornTriangles){Stop(TEXT("Restore worn geometry mismatch"));return;}
 if(FPlatformMisc::GetEnvironmentVariable(TEXT("OK_READER_OPERATION_PROBE"))==TEXT("1")){BeginOperation(A,TEXT("restored_loaded"),18);return;}Stage=18;return;}
 if(Stage==18){auto* Same=A;G->OnLoad3D();if(G->bLoadInFlight||G->PreviewActor.Get()!=Same){Stop(TEXT("Unchanged portable card not reused"));return;}
 const FString Source=G->Entries[0].Path;EditedCard=Output/TEXT("Cards")/FString::Printf(TEXT("C%03d_edited.png"),Card);IFileManager::Get().MakeDirectory(*FPaths::GetPath(EditedCard),true);
 G->Selected.Body.ShapeValueBody[0]=.123f;G->OutputDirectoryInput->SetText(FText::FromString(FPaths::GetPath(EditedCard)));G->CopyZipmodsCheck->SetIsChecked(false);
 G->OnSave();if(!G->bPortableSaveInFlight){Stop(TEXT("GUI save did not dispatch clothing resources in background"));return;}Stage=9;return;
 }
 if(Stage==9){if(G->bPortableSaveInFlight)return;if(!FPaths::FileExists(EditedCard)){Stop(TEXT("Actual GUI save did not write expected card"));return;}G->Entries[0].Path=EditedCard;G->SelectIndex(0);G->OnClothesState(101);if(!G->bLoadInFlight){Stop(TEXT("Edited card was incorrectly reused"));return;}Stage=7;return;}
 if(Stage==7){const auto Shape=A->Session.ShapeBody();if(Shape.IsEmpty()||!FMath::IsNearlyEqual(Shape[0],.123f)||ClothingTriangles(A)!=HalfTriangles){Stop(TEXT("Edited offline state lost shape or clothing geometry"));return;}if(PC->PaintSprayer->IsHidden()){Stop(TEXT("Sprayer hidden after edited card reload"));return;}G->OnClothesState(100);Stage=8;return;}
 if(Stage==8){if(ClothingTriangles(A)!=WornTriangles){Stop(TEXT("Edited card worn restore mismatch"));return;}Rows+=FString::Printf(TEXT("card=C%03d hair_parts=%d worn_triangles=%llu half_triangles=%llu removed_triangles=%llu deposited=%.9f clothing_coatings=%d jet_opacity=%.2f background_gui_save=1 offline_clothing=1 busy_callback=1 unchanged_reuse=1 edited_saved_offline=1\n"),Card,HairParts,WornTriangles,HalfTriangles,RemovedTriangles,Deposit,ClothingCoatings,Card==0?1.:.35);if(++CardPosition<Cards.Num()){Card=Cards[CardPosition];Stage=0;return;}Stop(FString());}
 }
};}
static FAutoConsoleCommandWithWorld ReaderAudit(TEXT("ok.ReaderRegressionAudit"),TEXT("Prepared APNG private hair, watergun and clothing UI regression"),FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* W){auto R=MakeShared<OKReaderAudit::FRun>();R->World=W;R->Directory=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_READER_CARDS"));R->Output=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_PAINT_OUTPUT"));R->WornOnly=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_READER_WORN_ONLY"))==TEXT("1");R->AllOutfits=R->WornOnly&&FPlatformMisc::GetEnvironmentVariable(TEXT("OK_READER_ALL_OUTFITS"))==TEXT("1");FString CardIds=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_READER_CARD_IDS"));if(!CardIds.IsEmpty()){TArray<FString> Ids;CardIds.ParseIntoArray(Ids,TEXT(","),true);R->Cards.Reset();for(const auto& Id:Ids)R->Cards.Add(FCString::Atoi(*Id));if(R->Cards.IsEmpty()){R->Stop(TEXT("Empty card list"));return;}R->Card=R->Cards[0];}
 R->PreviousRoot=UKKGameRoot::Get()->Root;UKKGameRoot::Get()->Root.Empty();if(!W||R->Directory.IsEmpty()||R->Output.IsEmpty()){R->Stop(TEXT("Missing environment"));return;}IFileManager::Get().MakeDirectory(*R->Output,true);W->GetTimerManager().SetTimer(R->Timer,FTimerDelegate::CreateLambda([R](){R->Tick();}),.001f,true);}));
#endif
