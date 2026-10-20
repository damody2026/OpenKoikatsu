#include "../OKPaintSprayer.h"
#include "../OKCameraDrag.h"
#include "../OpenKoikatsuPlayerController.h"
#include "../OKGalleryWidget.h"
#include "Camera/PlayerCameraManager.h"
#include "InputKeyEventArgs.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SViewport.h"
#include "Slate/SceneViewport.h"
#include "ViewportClient.h"
#include "Components/PoseableMeshComponent.h"
#include "ProceduralMeshComponent.h"
#include "KKPreviewActor.h"
#include "KKPairedPlayback.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "KKSkeletalBuilder.h"
#include "Async/Async.h"
#include "RenderingThread.h"
#include "Camera/CameraActor.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOKCameraControlsTest,"OpenKoikatsu.Camera.KksMouse",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FOKCameraControlsTest::RunTest(const FString&){
 const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).CreateNavigation(false).CreateAISystem(false);
 auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
 auto* Actor=World->SpawnActor<AKKPreviewActor>();Actor->StudioCam=World->SpawnActor<ACameraActor>();
 auto* Backdrop=World->SpawnActor<AActor>();Backdrop->Tags.Add(TEXT("OKStudioBackdrop"));
 auto* Shell=NewObject<UProceduralMeshComponent>(Backdrop);Backdrop->SetRootComponent(Shell);Shell->ComponentTags.Add(TEXT("OKStudioBackdropShell"));Shell->RegisterComponent();
 const FVector Focus(7,2,125);const double Distance=500;
 auto Reset=[&](){Actor->ResetOrbit(Focus,Focus+FVector(0,Distance,0));Actor->ApplyOrbitCamera();};
 Reset();TestEqual(TEXT("Backdrop has six faces"),Shell->GetNumSections(),6);
 Shell->GetProcMeshSection(0)->ProcVertexBuffer[0].UV0=FVector2D(.3,.4);
 Actor->ApplyOrbitCamera();TestTrue(TEXT("Unchanged backdrop preserves section data"),Shell->GetProcMeshSection(0)->ProcVertexBuffer[0].UV0.Equals(FVector2D(.3,.4),0.));
 const FVector PriorBackdrop=Shell->GetProcMeshSection(0)->ProcVertexBuffer[0].Position;
 // Moving inside the enclosure need not change its bounds; move beyond it.
 Actor->OrbitFocus.X+=5000.;Actor->ApplyOrbitCamera();TestTrue(TEXT("Camera outside enclosure updates backdrop"),Shell->GetProcMeshSection(0)->ProcVertexBuffer[0].Position!=PriorBackdrop);
 Actor->AnimPhase=.37f;Actor->FaceClock=.42f;Actor->PoseMode=2;
 for(uint8 Mask=1;Mask<8;++Mask){
  Reset();const FVector Right=Actor->StudioCam->GetActorRightVector(),Up=Actor->StudioCam->GetActorUpVector(),Forward=Actor->StudioCam->GetActorForwardVector();
  Actor->OrbitMouse(Mask,20,-10);FVector ExpectedFocus=Focus;double ExpectedDistance=Distance;FVector ExpectedOffset(0,Distance,0);
  if((Mask&3)==3)ExpectedFocus+=Right*10.+Forward*5.;
  else if(Mask&1){
   // Independent native Qy(180+MouseX*5)*Qx(-MouseY*5)*(0,0,-5m),
   // transformed Unity→UE as (-x,z,y)*100; no actor orbit helper used.
   const double Y=FMath::DegreesToRadians(190.),X=FMath::DegreesToRadians(-5.);
   const FVector Unity(FMath::Sin(Y)*(-5.*FMath::Cos(X)),-FMath::Sin(X)*(-5.),FMath::Cos(Y)*(-5.*FMath::Cos(X)));
   ExpectedOffset=FVector(-Unity.X,Unity.Z,Unity.Y)*100.;
  }else if(Mask&2){ExpectedFocus.Z+=5.;ExpectedDistance+=10.;ExpectedOffset=FVector(0,ExpectedDistance,0);}
  else if(Mask&4)ExpectedFocus+=Right*10.+Up*5.;
  TestTrue(FString::Printf(TEXT("Native branch priority/focus mask=%d"),Mask),Actor->OrbitFocus.Equals(ExpectedFocus,1.e-4));
  TestTrue(FString::Printf(TEXT("Native camera location mask=%d"),Mask),Actor->StudioCam->GetActorLocation().Equals(ExpectedFocus+ExpectedOffset,1.e-3));
  TestEqual(TEXT("Native distance"),Actor->OrbitDistance,float(ExpectedDistance));
 }
 Reset();Actor->OrbitZoom(1);TestEqual(TEXT("Wheel additive .1*5m"),Actor->OrbitDistance,450.f);
 Actor->OrbitZoom(-2);TestEqual(TEXT("Wheel zoom out"),Actor->OrbitDistance,550.f);
 Actor->OrbitZoom(100);TestEqual(TEXT("Native distance clamps at zero"),Actor->OrbitDistance,0.f);TestFalse(TEXT("Zero distance transform finite"),Actor->StudioCam->GetActorTransform().ContainsNaN());
 Reset();Actor->OrbitMouse(1,0,400);TestTrue(TEXT("Orbit crosses poles without arbitrary pitch clamp"),FMath::IsNearlyEqual(Actor->OrbitPitch,200.f));TestFalse(TEXT("Pole transform finite"),Actor->StudioCam->GetActorTransform().ContainsNaN());
 Actor->OrbitMouse(4,10,-10);TestFalse(TEXT("Pan after pole finite"),Actor->StudioCam->GetActorTransform().ContainsNaN());
 TestEqual(TEXT("Camera leaves animation time"),Actor->AnimPhase,.37f);TestEqual(TEXT("Camera leaves face time"),Actor->FaceClock,.42f);TestEqual(TEXT("Camera leaves playback mode"),Actor->PoseMode,2);
 FOKCameraDrag Drag;Drag.Update(1,true,true,true,{10,10});TestFalse(TEXT("UI press not owned"),Drag.Owned);
 Drag.Update(1,true,true,false,{20,20});TestFalse(TEXT("Held UI drag cannot become camera drag"),Drag.Owned);
 Drag.Update(0,true,true,false,{20,20});const auto Start=Drag.Update(4,true,true,false,{20,20});TestTrue(TEXT("Viewport middle press owned"),Drag.Owned);TestTrue(TEXT("First press has no jump"),Start.IsZero());
 const auto Delta=Drag.Update(4,true,true,false,{30,15});TestTrue(TEXT("Raw screen pixel delta"),Delta.Equals({10,-5},0.));
 Drag.Update(4,true,false,false,{30,15});TestFalse(TEXT("Focus loss cancels"),Drag.Owned);Drag.Update(4,true,true,false,{40,15});TestFalse(TEXT("Focus regain while held cannot acquire"),Drag.Owned);
 Drag.Update(0,true,true,false,{40,15});Drag.Update(2,true,true,false,{40,15});Drag.Update(2,false,true,false,{40,15});TestFalse(TEXT("Pointer outside viewport cancels"),Drag.Owned);
 // Exercise actual controller owner selection with a stale actor sharing the camera.
 auto* PC=World->SpawnActor<AOpenKoikatsuPlayerController>();if(!PC->PlayerCameraManager)PC->PlayerCameraManager=World->SpawnActor<APlayerCameraManager>();
 PC->SetPaintPreviewActive(true);
 PC->InputKey(FInputKeyEventArgs(nullptr,INPUTDEVICEID_NONE,EKeys::A,IE_Pressed,0));
 TestTrue(TEXT("Game input A press arms spray without relying on Slate key forwarding"),PC->PaintAimHeld);
 TestTrue(TEXT("Fresh viewport left press owns spray"),PC->UpdatePaintGesture(true,true,true,false));
 PC->InputKey(FInputKeyEventArgs(nullptr,INPUTDEVICEID_NONE,EKeys::A,IE_Released,0));
 TestFalse(TEXT("A release stops held spray"),PC->UpdatePaintGesture(true,true,true,false));
 PC->UpdatePaintGesture(false,true,true,false);PC->PaintAimHeld=true;
 TestFalse(TEXT("UI left press is not spray"),PC->UpdatePaintGesture(true,true,true,true));
 TestFalse(TEXT("Held UI press cannot become spray"),PC->UpdatePaintGesture(true,true,true,false));
 PC->PaintSprayer->SetSpray(true,FVector(1,2,3),FVector::ForwardVector);
 PC->UpdatePaintAim(false);TestFalse(TEXT("Idle stops emission"),PC->PaintSprayer->IsSpraying());
 const uint64 Queries=PC->PaintAimQueries;for(int32 I=0;I<20;++I)PC->UpdatePaintAim(false);
 TestEqual(TEXT("Idle does not query animated surfaces"),PC->PaintAimQueries,Queries);
 TestEqual(TEXT("Idle preserves last nozzle instead of moving it every frame"),PC->PaintSprayer->GetActorLocation(),FVector(1,2,3));
 PC->SetPaintPreviewActive(false);
 auto* Active=World->SpawnActor<AKKPreviewActor>();Active->StudioCam=Actor->StudioCam;Active->ResetOrbit(Focus,Focus+FVector(0,500,0));
 auto* Gallery=NewObject<UOKGalleryWidget>(PC);Gallery->PreviewActor=Active;
 PC->PlayerCameraManager->InitializeFor(PC);PC->SetViewTarget(Active->StudioCam);const float StaleDistance=Actor->OrbitDistance;
 TestTrue(TEXT("Gallery belongs to test world"),Gallery->GetWorld()==World);
 auto* CameraOwner=PC->GetPreviewCameraOwner();TestTrue(TEXT("Controller selects gallery actor instead of stale actor"),CameraOwner==Active);
 Gallery->PreviewActor=Actor;TestTrue(TEXT("Cached gallery follows actor switch sharing the same camera"),PC->GetPreviewCameraOwner()==Actor);
 Gallery->PreviewActor=Active;TestTrue(TEXT("Cached gallery follows active actor restore"),PC->GetPreviewCameraOwner()==Active);
 if(CameraOwner)CameraOwner->OrbitZoom(1.f);
 TestEqual(TEXT("Controller targets active gallery camera owner"),Active->OrbitDistance,450.f);
 TestEqual(TEXT("Controller ignores stale actor sharing camera"),Actor->OrbitDistance,StaleDistance);
 // Reproduce the actual NoCapture event boundary, not just orbit arithmetic.
 struct FNoCaptureClient : FViewportClient {
  int32 Presses=0;
  virtual EMouseCaptureMode GetMouseCaptureMode() const override {return EMouseCaptureMode::NoCapture;}
  virtual bool InputKey(const FInputKeyEventArgs& Args) override {if(Args.Event==IE_Pressed)++Presses;return false;}
 };
 auto Client=MakeShared<FNoCaptureClient>();
 auto ViewWidget=SNew(SViewport);FSceneViewport View(ViewWidget);View.SetViewportClient(TStrongPtrVariant<FViewportClient>(Client));View.SetPlayInEditorViewport(true);View.SetViewportSize(320,240);
 const FGeometry Geometry=FGeometry::MakeRoot(FVector2D(320,240),FSlateLayoutTransform());
 auto& App=FSlateApplication::Get();TSet<FKey> Held;
 auto Pointer=[&](FKey Key,FVector2D Pos,FVector2D Last){return FPointerEvent(0,Pos,Last,Held,Key,0.f,FModifierKeysState());};
 auto Press=[&](FKey Key){Held.Add(Key);const auto Event=Pointer(Key,{100,100},{100,100});App.ProcessMouseButtonDownEvent(nullptr,Event);View.OnMouseButtonDown(Geometry,Event);};
 auto Release=[&](FKey Key){Held.Remove(Key);const auto Event=Pointer(Key,{120,90},{120,90});App.ProcessMouseButtonUpEvent(Event);View.OnMouseButtonUp(Geometry,Event);};
 for(uint8 Mask : {uint8(1),uint8(4),uint8(2),uint8(3)}){
  Active->ResetOrbit(Focus,Focus+FVector(0,500,0));Active->ApplyOrbitCamera();
  PC->UpdateCameraMouse(true,true,false,{100,100});
  if(Mask&1)Press(EKeys::LeftMouseButton);if(Mask&2)Press(EKeys::RightMouseButton);if(Mask&4)Press(EKeys::MiddleMouseButton);
  TestEqual(TEXT("NoCapture drops primary button press"),Client->Presses,0);
  TestFalse(TEXT("PlayerInput still has no left press"),PC->IsInputKeyDown(EKeys::LeftMouseButton));
  TestEqual(TEXT("Controller reads real Slate buttons"),PC->GetCameraMouseButtons(),Mask);
  PC->UpdateCameraMouse(true,true,false,{double(View.GetMouseX()),double(View.GetMouseY())});
  const FTransform Before=Active->StudioCam->GetActorTransform();
  const auto Move=Pointer(EKeys::Invalid,{120,90},{100,100});View.OnMouseMove(Geometry,Move);
  PC->UpdateCameraMouse(true,true,false,{double(View.GetMouseX()),double(View.GetMouseY())});
  TestFalse(FString::Printf(TEXT("NoCapture controller drag moves camera mask=%d"),Mask),Active->StudioCam->GetActorTransform().Equals(Before));
  if(Mask&1)Release(EKeys::LeftMouseButton);if(Mask&2)Release(EKeys::RightMouseButton);if(Mask&4)Release(EKeys::MiddleMouseButton);
  PC->UpdateCameraMouse(true,true,false,{120,90});TestEqual(TEXT("Release clears Slate buttons"),PC->GetCameraMouseButtons(),uint8(0));
 }
 const auto UITransform=Active->StudioCam->GetActorTransform();
 Press(EKeys::LeftMouseButton);PC->UpdateCameraMouse(true,true,true,{100,100});PC->UpdateCameraMouse(true,true,false,{140,140});
 TestTrue(TEXT("UI-owned physical drag stays rejected"),Active->StudioCam->GetActorTransform().Equals(UITransform));
 Release(EKeys::LeftMouseButton);PC->UpdateCameraMouse(true,true,false,{140,140});
 UE_LOG(LogTemp,Display,TEXT("KKS camera NoCapture routing valid=%d slate_masks=4 playerinput_presses=%d ui_preserved=1"),HasAnyErrors()?0:1,Client->Presses);

 Gallery->MarkAsGarbage();
 auto* Cancel=NewObject<UOKGalleryWidget>(PC);const int32 Request=Cancel->PairPrepareGeneration;
 Cancel->OnStopPair();TestEqual(TEXT("Stop invalidates preparation"),Cancel->PairPrepareGeneration,Request+1);
 Cancel->OnPausePlayback();TestEqual(TEXT("Pause invalidates preparation"),Cancel->PairPrepareGeneration,Request+2);
 Cancel->OnReplayPlayback();TestEqual(TEXT("Replay invalidates preparation"),Cancel->PairPrepareGeneration,Request+3);
 Cancel->RefreshPairs();TestEqual(TEXT("Catalogue refresh invalidates preparation"),Cancel->PairPrepareGeneration,Request+4);Cancel->MarkAsGarbage();
 UE_LOG(LogTemp,Display,TEXT("KKS camera mouse valid=%d masks=7 unity_oracle=1 wheel=1 pole=1 playback_preserved=1 ui_gesture=1 focus=1"),HasAnyErrors()?0:1);
 World->DestroyWorld(false);GEngine->DestroyWorldContext(World);return !HasAnyErrors();
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOKCameraNativeTest,"OpenKoikatsu.Camera.NativePlayback",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FOKCameraNativeTest::RunTest(const FString&){
 const FString Root=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_TEST_WARM_ROOT")),Card=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_TEST_WARM_CARD"));
 if(Root.IsEmpty()||Card.IsEmpty()){AddError(TEXT("Native playback camera test needs root/card"));return false;}
 const FString BeforeEnv=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_UE_ANIM"));
 const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).CreateNavigation(false).CreateAISystem(false);
 auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
 auto* Actor=World->SpawnActor<AKKPreviewActor>();FString Error;
 bool Good=TestTrue(TEXT("Real card/session"),Actor->Session.Open(Root,Card,0,Error))&&TestTrue(TEXT("Real model"),Actor->LoadSession(Error));
 if(Good){
  Good=TestTrue(TEXT("Original KKS walk controller"),Actor->SetGameAnimation(TEXT("states:studio/anime/00.unity3d|walk_run / Base Layer.f_aruki_00_00_01")))&&Good;
  Actor->StudioCam=World->SpawnActor<ACameraActor>();Actor->ResetOrbit({0,0,100},{0,500,100});Actor->ApplyOrbitCamera();Actor->Tick(.13f);
  const float Time=Actor->AnimPhase,Face=Actor->FaceClock;TArray<FTransform> Before=Actor->BodySkel?Actor->BodySkel->BoneSpaceTransforms:TArray<FTransform>();
  for(uint8 Mask=1;Mask<8;++Mask)Actor->OrbitMouse(Mask,13,-7);Actor->OrbitZoom(1);
  Good=TestEqual(TEXT("Live playback clock preserved by camera"),Actor->AnimPhase,Time)&&Good;Good=TestEqual(TEXT("Live face clock preserved by camera"),Actor->FaceClock,Face)&&Good;
  Good=TestEqual(TEXT("Live controller keeps playing"),Actor->PoseMode,2)&&Good;
  Good=TestTrue(TEXT("Loaded bones present"),!Before.IsEmpty())&&Good;
  if(Actor->BodySkel){const auto& Same=Actor->BodySkel->BoneSpaceTransforms;for(int32 I=0;I<Before.Num();++I)Good=TestTrue(TEXT("Camera never rewrites animation bone pose"),Same.IsValidIndex(I)&&Before[I].Equals(Same[I],0.f))&&Good;}
  Actor->Tick(.17f);int32 Changed=0;if(Actor->BodySkel){const auto& After=Actor->BodySkel->BoneSpaceTransforms;for(int32 I=0;I<Before.Num();++I)if(After.IsValidIndex(I)&&!Before[I].Equals(After[I],1.e-6f))++Changed;}
  Good=TestTrue(TEXT("Original animation continues advancing"),Actor->AnimPhase>Time&&Changed>0)&&Good;
  // Native playback performance regression: share one sampled pose, update
  // dynamic renderer data without recreating a proxy or changing bone values.
  auto* PoseComp=NewObject<UPoseableMeshComponent>(Actor);PoseComp->SetSkinnedAssetAndUpdate(Actor->BodySkel->GetSkinnedAsset());PoseComp->RegisterComponent();
  World->SendAllEndOfFrameUpdates();FlushRenderingCommands();TArray<FTransform> Pose;OKBuildSkelPose(Actor->Session,Pose);
  OKApplySkelPose(PoseComp,Pose);Good=TestFalse(TEXT("Pose update does not recreate render state"),PoseComp->IsRenderStateDirty())&&Good;
  for(int32 I=0;I<Pose.Num();++I)Good=TestTrue(TEXT("Sampled pose preserved exactly"),PoseComp->BoneSpaceTransforms[I].Equals(Pose[I],0.))&&Good;
  const FString PriorProfile=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_ANIM_PROFILE"));FPlatformMisc::SetEnvironmentVar(TEXT("OK_ANIM_PROFILE"),TEXT("1"));
  const FString PriorParity=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_ANIM_SKIN_PARITY"));FPlatformMisc::SetEnvironmentVar(TEXT("OK_ANIM_SKIN_PARITY"),TEXT("1"));Actor->Tick(0.f);FPlatformMisc::SetEnvironmentVar(TEXT("OK_ANIM_SKIN_PARITY"),*PriorParity);
  const double SampleStart=FPlatformTime::Seconds();for(int32 I=0;I<20;++I)Actor->Tick(1.f/60.f);
  const double FrameMs=(FPlatformTime::Seconds()-SampleStart)*1000./20.;
  FPlatformMisc::SetEnvironmentVar(TEXT("OK_ANIM_PROFILE"),*PriorProfile);
  const double QuietStart=FPlatformTime::Seconds();for(int32 I=0;I<20;++I)Actor->Tick(1.f/60.f);const double QuietMs=(FPlatformTime::Seconds()-QuietStart)*1000./20.;
  auto Prepared=Async(EAsyncExecution::Thread,[Root](){return FOkSession::PrepareAnimationSource(Root,TEXT("states:studio/anime/00.unity3d"))&&FOkSession::PrepareAnimationClip(Root,TEXT("states:studio/anime/00.unity3d|walk_run / Base Layer.f_aruki_00_00_01"));});
  Good=TestTrue(TEXT("Immutable preparation works off game thread"),Prepared.Get())&&Good;
  Good=TestTrue(TEXT("Exact source membership"),Actor->Session.HasAnimationClip(TEXT("states:studio/anime/00.unity3d|walk_run / Base Layer.f_aruki_00_00_01"))&&!Actor->Session.HasAnimationClip(TEXT("states:studio/anime/00.unity3d|missing")))&&Good;
  UE_LOG(LogTemp,Display,TEXT("OK animation perf valid=%d bones=%d followers=%d samples=20 tick_ms=%.6f quiet_tick_ms=%.6f proxy_preserved=1 worker=1"),Good&&!HasAnyErrors()?1:0,Pose.Num(),Actor->SkelFollowers.Num(),FrameMs,QuietMs);
  UE_LOG(LogTemp,Display,TEXT("KKS camera native valid=%d bones=%d changed=%d phase=%.6f"),Good&&!HasAnyErrors()?1:0,Before.Num(),Changed,Actor->AnimPhase);
  const FString OtherCard=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_TEST_WARM_OTHER_CARD"));
  if(!OtherCard.IsEmpty()) {
   auto* Other=World->SpawnActor<AKKPreviewActor>();
   Good=TestTrue(TEXT("Second real card/session"),Other->Session.Open(Root,OtherCard,0,Error))&&Good;
   if(Other->Session.IsOpen()) {
    Good=TestTrue(TEXT("Second real model"),Other->LoadSession(Error))&&Good;
    Good=TestTrue(TEXT("Second original walk"),Other->SetGameAnimation(TEXT("states:studio/anime/00.unity3d|walk_run / Base Layer.f_aruki_00_00_01")))&&Good;
    FPlatformMisc::SetEnvironmentVar(TEXT("OK_ANIM_SKIN_PARITY"),TEXT("1"));Actor->Tick(0.f);Other->Tick(0.f);FPlatformMisc::SetEnvironmentVar(TEXT("OK_ANIM_SKIN_PARITY"),*PriorParity);
    const float StartA=Actor->AnimPhase,StartB=Other->AnimPhase;
    const double DualStart=FPlatformTime::Seconds();
    for(int32 I=0;I<60;++I){Actor->Tick(1.f/60.f);Other->Tick(1.f/60.f);}
    const double DualMs=(FPlatformTime::Seconds()-DualStart)*1000./60.;
    Good=TestTrue(TEXT("Both native clocks advance independently"),FMath::IsNearlyEqual(Actor->AnimPhase-StartA,1.f,1.e-4f)&&FMath::IsNearlyEqual(Other->AnimPhase-StartB,1.f,1.e-4f))&&Good;
    for(auto* Role:{Actor,Other})if(Role->BodySkel)for(const auto& Bone:Role->BodySkel->BoneSpaceTransforms)Good=TestFalse(TEXT("Two character bone transforms finite"),Bone.ContainsNaN())&&Good;
    UE_LOG(LogTemp,Display,TEXT("OK animation dual perf valid=%d samples=60 cpu_update_ms=%.6f bones_a=%d bones_b=%d independent_clocks=1"),Good&&!HasAnyErrors()?1:0,DualMs,Actor->Session.SkelBoneCount(),Other->Session.SkelBoneCount());
    TArray<TSharedPtr<FJsonValue>> Rows;
    Good=TestTrue(TEXT("Native pair metadata"),FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Actor->Session.NativeAnimationPairs()),Rows))&&Good;
    auto* Player=World->SpawnActor<AKKPairedPlayback>();int32 Cases=0;
    for(const auto& Row:Rows) {
     const auto Info=Row->AsObject();if(!Info.IsValid()||Info->GetIntegerField(TEXT("participants"))!=2)continue;
     const FString FS=Info->GetStringField(TEXT("female_source")),MS=Info->GetStringField(TEXT("male_source"));
     const double PrepareStart=FPlatformTime::Seconds();
     int32 LiveFrames=0;double MaxLiveMs=0.;
     auto WaitWhilePlaying=[&](auto& Future){
      while(!Future.WaitFor(FTimespan::FromMilliseconds(1)))if(Cases>0){
       const double LiveStart=FPlatformTime::Seconds();Good=TestTrue(TEXT("Previous pair advances during worker preparation"),Player->Advance(1.f/60.f,Error))&&Good;
       MaxLiveMs=FMath::Max(MaxLiveMs,(FPlatformTime::Seconds()-LiveStart)*1000.);++LiveFrames;
      }
      return Future.Get();
     };
     auto SourceReady=Async(EAsyncExecution::Thread,[Root,FS,MS](){return FOkSession::PrepareAnimationSource(Root,FS)&&FOkSession::PrepareAnimationSource(Root,MS);});
     if(!TestTrue(TEXT("Pair sources prepared by worker"),WaitWhilePlaying(SourceReady))){Good=false;break;}
     const auto FemaleLabels=Actor->Session.ClipNamesForSource(FS),MaleLabels=Other->Session.ClipNamesForSource(MS);
     const FString Prefix=TEXT(" / [Controller layer 0] Base Layer.");FString FK,MK,State;
     for(const FString& Label:FemaleLabels) {
      const int32 At=Label.Find(Prefix);if(At==INDEX_NONE)continue;const FString Candidate=Label.Mid(At+Prefix.Len());if(Candidate!=TEXT("Idle"))continue;
      for(const FString& ML:MaleLabels)if(ML.EndsWith(Prefix+Candidate)){FK=FS+TEXT("|")+Label;MK=MS+TEXT("|")+ML;State=Candidate;break;}
      if(!FK.IsEmpty())break;
     }
     if(!TestFalse(TEXT("Native pair common state exists"),FK.IsEmpty())){Good=false;break;}
     const FString FM=Info->GetStringField(TEXT("female_motion")),MM=Info->GetStringField(TEXT("male_motion"));
     auto ClipReady=Async(EAsyncExecution::Thread,[Root,FK,MK,State,FM,MM](){return FOkSession::PrepareAnimationClip(Root,FK)&&FOkSession::PrepareAnimationClip(Root,MK)&&FOkSession::PrepareAnimationPair(Root,State,FM,MM);});
     if(!TestTrue(TEXT("Pair curves prepared by worker"),WaitWhilePlaying(ClipReady))){Good=false;break;}
     const double PreparedMs=(FPlatformTime::Seconds()-PrepareStart)*1000.;
     const FString PreviousPairProfile=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_PAIR_PROFILE"));FPlatformMisc::SetEnvironmentVar(TEXT("OK_PAIR_PROFILE"),TEXT("1"));
     const double CommitStart=FPlatformTime::Seconds();
     const bool Started=Player->Start(Actor,Other,FK,MK,State,Info->GetStringField(TEXT("female_motion")),Info->GetStringField(TEXT("male_motion")),Error);
     const double CommitMs=(FPlatformTime::Seconds()-CommitStart)*1000.;
     FPlatformMisc::SetEnvironmentVar(TEXT("OK_PAIR_PROFILE"),*PreviousPairProfile);
     if(!TestTrue(FString::Printf(TEXT("Native pair commits: %s"),*Error),Started)){Good=false;break;}
     Player->SetActorTickEnabled(false);
     FPlatformMisc::SetEnvironmentVar(TEXT("OK_ANIM_SKIN_PARITY"),TEXT("1"));
     Good=TestTrue(TEXT("Paired IK exact legacy oracle"),Player->Advance(0.f,Error))&&Good;
     FPlatformMisc::SetEnvironmentVar(TEXT("OK_ANIM_SKIN_PARITY"),*PriorParity);
     const double PairStart=FPlatformTime::Seconds();
     for(int32 I=0;I<30;++I){Good=TestTrue(TEXT("Pair advances with contacts/dynamics"),Player->Advance(1.f/60.f,Error))&&Good;Good=TestEqual(TEXT("Native pair shared time"),Actor->AnimPhase,Other->AnimPhase)&&Good;}
     const double PairMs=(FPlatformTime::Seconds()-PairStart)*1000./30.;
     Good=TestTrue(TEXT("Pair clock progresses"),FMath::IsNearlyEqual(Player->GetElapsed(),.5f,1.e-5f))&&Good;
     const FString PriorLegacy=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_ANIM_IK_LEGACY"));FPlatformMisc::SetEnvironmentVar(TEXT("OK_ANIM_IK_LEGACY"),TEXT("1"));
     Good=TestTrue(TEXT("Legacy IK comparison restart"),Player->Restart(Error))&&Good;
     Player->SetActorTickEnabled(false);const double LegacyStart=FPlatformTime::Seconds();
     for(int32 I=0;I<30;++I)Good=TestTrue(TEXT("Legacy IK comparison frames"),Player->Advance(1.f/60.f,Error))&&Good;
     const double LegacyMs=(FPlatformTime::Seconds()-LegacyStart)*1000./30.;FPlatformMisc::SetEnvironmentVar(TEXT("OK_ANIM_IK_LEGACY"),*PriorLegacy);
     UE_LOG(LogTemp,Display,TEXT("OK animation active IK perf valid=%d case=%d samples=30 batched_ms=%.6f legacy_ms=%.6f state=%s"),Good&&!HasAnyErrors()?1:0,Cases,PairMs,LegacyMs,*State);
     UE_LOG(LogTemp,Display,TEXT("OK animation paired perf valid=%d case=%d prepare_ms=%.6f commit_ms=%.6f samples=30 cpu_update_ms=%.6f shared_clock=1 prepare_live_frames=%d prepare_max_old_update_ms=%.6f"),Good&&!HasAnyErrors()?1:0,Cases,PreparedMs,CommitMs,PairMs,LiveFrames,MaxLiveMs);
     if(++Cases==2)break;
    }
    Good=TestEqual(TEXT("Two native paired selections exercised"),Cases,2)&&Good;Player->Stop();
   }
  }
 }
 World->DestroyWorld(false);GEngine->DestroyWorldContext(World);FPlatformMisc::SetEnvironmentVar(TEXT("OK_UE_ANIM"),*BeforeEnv);return Good&&!HasAnyErrors();
}
#endif
