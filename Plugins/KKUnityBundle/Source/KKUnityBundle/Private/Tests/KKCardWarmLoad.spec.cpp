#include "KKPreviewActor.h"
#include "KKRustExtract.h"
#include "KKSkeletalBuilder.h"
#include "ok_extract.h"
#include "Async/Async.h"
#include "Modules/ModuleManager.h"
#include "IImageWrapperModule.h"
#include "KKTextureRuntime.h"
#include "Components/SkeletalMeshComponent.h"
#include "KKCardReader.h"
#include "Engine/Texture2D.h"
#include "Misc/SecureHash.h"
#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "RenderingThread.h"
#include "RHICommandList.h"
#include "HAL/Event.h"
#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKKLiveSkinRequestTest,"OpenKoikatsu.Preview.LiveSkinRequest",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FKKLiveSkinRequestTest::RunTest(const FString&){
 const FString Before=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_CPU_SKIN"));
 const uint8 State=OKCaptureSkinEnvironmentState();OKRestoreSkinEnvironmentState(0);
 FPlatformMisc::SetEnvironmentVar(TEXT("OK_CPU_SKIN"),TEXT("0"));
 TestTrue(TEXT("First live load requests reference preparation before Open initializes renderer mode"),OKLiveGpuSkinRequested());
 TestEqual(TEXT("Reading request does not initialize shared renderer state"),OKCaptureSkinEnvironmentState(),uint8(0));
 FPlatformMisc::SetEnvironmentVar(TEXT("OK_CPU_SKIN"),TEXT("1"));
 TestFalse(TEXT("Explicit CPU renderer skips GPU reference preparation"),OKLiveGpuSkinRequested());
 FPlatformMisc::SetEnvironmentVar(TEXT("OK_CPU_SKIN"),*Before);OKRestoreSkinEnvironmentState(State);
 return !HasAnyErrors();
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKKNativeReferencePreparationTest,"OpenKoikatsu.Preview.NativeReferencePreparation",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FKKNativeReferencePreparationTest::RunTest(const FString&){
 const FString Root=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_TEST_WARM_ROOT")),Card=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_TEST_WARM_CARD"));
 if(Root.IsEmpty()||Card.IsEmpty()){AddError(TEXT("Set real source fixture root and card"));return false;}
 FOkSession Baseline,Prepared;FString Error;
 if(!TestTrue(TEXT("Baseline source opens"),Baseline.Open(Root,Card,0,Error))||!TestTrue(TEXT("Worker source opens independently"),Prepared.Open(Root,Card,0,Error)))return false;
 FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
 TestTrue(TEXT("Reference preparation runs on exclusive worker"),Async(EAsyncExecution::Thread,[&](){AKKPreviewActor::PrecacheSessionTextures(Prepared,true);return !IsInGameThread();}).Get());
 const int32 Bones=Baseline.SkelBoneCount();
 TestTrue(TEXT("Fixture contains full rig"),Bones>0);TestEqual(TEXT("Worker preserves bone count"),Prepared.SkelBoneCount(),Bones);
 for(int32 I=0;I<Bones;++I){OkBoneView A{},B{};
  if(!TestTrue(TEXT("Both bone views available"),Baseline.SkelBoneAt(I,A)&&Prepared.SkelBoneAt(I,B)))return false;
  TestTrue(TEXT("Reference names and parents exact"),A.name&&B.name&&FCStringAnsi::Strcmp(A.name,B.name)==0&&A.parent==B.parent);
  TestTrue(TEXT("Reference and live local transforms exact"),FMemory::Memcmp(A.rest_pos,B.rest_pos,sizeof(A.rest_pos))==0&&FMemory::Memcmp(A.rest_rot,B.rest_rot,sizeof(A.rest_rot))==0&&FMemory::Memcmp(A.rest_scl,B.rest_scl,sizeof(A.rest_scl))==0&&FMemory::Memcmp(A.pose_pos,B.pose_pos,sizeof(A.pose_pos))==0&&FMemory::Memcmp(A.pose_rot,B.pose_rot,sizeof(A.pose_rot))==0&&FMemory::Memcmp(A.pose_scl,B.pose_scl,sizeof(A.pose_scl))==0);
 }
 TestEqual(TEXT("Every renderer preserved"),Prepared.PartCount(),Baseline.PartCount());
 int32 ReferenceParts=0;
 auto EqualBytes=[&](const TCHAR* Label,const void* A,const void* B,int64 Size){TestTrue(Label,(!A&&!B)||(A&&B&&FMemory::Memcmp(A,B,Size)==0));};
 for(int32 I=0;I<Baseline.PartCount();++I){OkMeshView A{},B{};
  if(!TestTrue(TEXT("Both renderer views available"),Baseline.PartAt(I,A)&&Prepared.PartAt(I,B)))return false;
  if(!TestEqual(TEXT("Vertex count exact"),A.n_verts,B.n_verts))return false;
  EqualBytes(TEXT("Reference positions exact"),A.rest_positions,B.rest_positions,int64(A.n_verts)*3*sizeof(float));
  EqualBytes(TEXT("Reference normals exact"),A.reference_normals,B.reference_normals,int64(A.n_verts)*3*sizeof(float));
  EqualBytes(TEXT("Reference tangents exact"),A.rest_tangents,B.rest_tangents,int64(A.n_verts)*4*sizeof(float));
  EqualBytes(TEXT("Bone indices exact"),A.bone_idx,B.bone_idx,int64(A.n_verts)*4*sizeof(uint16));
  EqualBytes(TEXT("Current positions unchanged"),A.positions,B.positions,int64(A.n_verts)*3*sizeof(float));
  if(TestEqual(TEXT("PNG length unchanged"),A.png_len,B.png_len))EqualBytes(TEXT("PNG bytes unchanged"),A.png,B.png,A.png_len);
  ReferenceParts+=A.rest_positions?1:0;
 }
 TestTrue(TEXT("Real reference mesh arrays exercised"),ReferenceParts>0);
 TestTrue(TEXT("Preparation does not change shape values"),Baseline.ShapeBody()==Prepared.ShapeBody()&&Baseline.ShapeFace()==Prepared.ShapeFace());
 return !HasAnyErrors();
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKKRenderReadinessTest,"OpenKoikatsu.Preview.RenderReadiness",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FKKRenderReadinessTest::RunTest(const FString&){
 const FString Root=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_TEST_WARM_ROOT")),Card=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_TEST_WARM_CARD"));
 if(Root.IsEmpty()||Card.IsEmpty()||!GIsThreadedRendering){AddError(TEXT("Real source fixture and threaded renderer required"));return false;}
 const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).CreateNavigation(false).CreateAISystem(false);
 auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
 auto* Actor=World->SpawnActor<AKKPreviewActor>();FString Error;
 bool Good=TestTrue(TEXT("Real display and shaders prepared"),Actor->Session.Open(Root,Card,0,Error)&&Actor->LoadSession(Error)&&Actor->PreparePlaybackMaterials(true));
 if(Good){
  FEvent* Entered=FPlatformProcess::GetSynchEventFromPool(true);FEvent* Release=FPlatformProcess::GetSynchEventFromPool(true);TAtomic<bool> TimedOut(false);
  ENQUEUE_RENDER_COMMAND(KKReadinessTestGate)([Entered,Release,&TimedOut](FRHICommandListImmediate&){Entered->Trigger();if(!Release->Wait(2000))TimedOut.Store(true);});
  Good=TestTrue(TEXT("Render gate entered"),Entered->Wait(2000))&&Good;
  Actor->bPlaybackMaterialsReady=false;const float Phase=Actor->AnimPhase;const double Clock=Actor->FaceClock;
  const bool Ready=Actor->PreparePlaybackMaterials(false);
  Good=TestFalse(TEXT("Nonblocking submission cannot publish before render work completes"),Ready)&&Good;
  Good=TestTrue(TEXT("Pending RHI fence tracked"),Actor->bPlaybackRenderFencePending)&&Good;
  Good=TestFalse(TEXT("Game thread returned while render gate remained closed"),TimedOut.Load())&&Good;
  Release->Trigger();
  Good=TestTrue(TEXT("Completion publishes after RHI fence"),Actor->PreparePlaybackMaterials(true))&&Good;
  Good=TestFalse(TEXT("Completed fence no longer pending"),Actor->bPlaybackRenderFencePending)&&Good;
  Good=TestTrue(TEXT("Resource readiness never advances playback"),Actor->AnimPhase==Phase&&Actor->FaceClock==Clock)&&Good;
  FlushRenderingCommands();FPlatformProcess::ReturnSynchEventToPool(Entered);FPlatformProcess::ReturnSynchEventToPool(Release);
  Actor->bPlaybackMaterialsReady=false;Actor->PreparePlaybackMaterials(false);Actor->DestroyAllParts();
  Good=TestFalse(TEXT("Cancellation clears pending publication"),Actor->bPlaybackRenderFencePending)&&Good;
  Good=TestFalse(TEXT("Cancellation cannot retain ready flag"),Actor->bPlaybackMaterialsReady)&&Good;
 }
 Actor->Destroy();World->DestroyWorld(false);GEngine->DestroyWorldContext(World);return Good&&!HasAnyErrors();
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKKSetupCancellationTest,"OpenKoikatsu.Preview.IncrementalSetupCancellation",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FKKSetupCancellationTest::RunTest(const FString&){
 const FString Root=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_TEST_WARM_ROOT")),Card=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_TEST_WARM_CARD"));
 if(Root.IsEmpty()||Card.IsEmpty()){AddError(TEXT("Real source fixture required"));return false;}
 const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).CreateNavigation(false).CreateAISystem(false);
 auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
 auto* Actor=World->SpawnActor<AKKPreviewActor>();FString Error;
 bool Good=TestTrue(TEXT("Open actual source"),Actor->Session.Open(Root,Card,0,Error));
 if(Good)for(int32 Stage=0;Stage<=AKKPreviewActor::SessionLoadSetupStages;++Stage){
  Good=TestTrue(TEXT("Restart initializes clean staged transaction"),Actor->BeginSessionLoad(Error))&&Good;
  Good=TestEqual(TEXT("Begin queues no shadow target"),Actor->ToonShadowTargets.Num(),0)&&Good;
  for(int32 Step=0;Step<Stage;++Step)Good=TestTrue(TEXT("Advance one setup stage"),Actor->ContinueSessionLoad(Error))&&Good;
  Good=TestEqual(TEXT("Shadow targets created one per stage"),Actor->ToonShadowTargets.Num(),FMath::Min(Stage,2))&&Good;
  Good=TestEqual(TEXT("Capture follows both targets"),Actor->ToonShadowCapture!=nullptr,Stage>=3)&&Good;
  Good=TestEqual(TEXT("Setup publishes no source renderer"),Actor->Parts.Num(),0)&&Good;
  Good=TestFalse(TEXT("Setup cannot publish completed display"),Actor->IsSessionLoadComplete())&&Good;
  Actor->DestroyAllParts();
  Good=TestEqual(TEXT("Cancellation releases shadow targets"),Actor->ToonShadowTargets.Num(),0)&&Good;
  Good=TestTrue(TEXT("Cancellation releases capture and rooted textures"),!Actor->ToonShadowCapture&&Actor->LoadedTextures.IsEmpty())&&Good;
  Good=TestFalse(TEXT("Cancelled setup cannot resume"),Actor->ContinueSessionLoad(Error))&&Good;Error.Reset();
 }
 Good=TestTrue(TEXT("Synchronous load still completes after every cancelled stage"),Actor->LoadSession(Error)&&Actor->IsSessionLoadComplete())&&Good;
 Actor->Destroy();World->DestroyWorld(false);GEngine->DestroyWorldContext(World);return Good&&!HasAnyErrors();
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKKCardWarmLoadTest,"OpenKoikatsu.Preview.WarmLoad",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FKKCardWarmLoadTest::RunTest(const FString& Parameters){
 const FString Root=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_TEST_WARM_ROOT"));
 const FString Card=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_TEST_WARM_CARD"));
 if(Root.IsEmpty()||Card.IsEmpty()){AddError(TEXT("Set OK_TEST_WARM_ROOT and OK_TEST_WARM_CARD"));return false;}
 const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).CreateNavigation(false).CreateAISystem(false);
 auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
 FKKCharacterFile Selected;FString ReadError;
 bool Good=TestTrue(TEXT("GUI card parser"),UKKCardReader::LoadFromFile(Card,Selected,ReadError));
 const FString OtherCard=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_TEST_WARM_OTHER_CARD"));
 const bool Switching=!OtherCard.IsEmpty();
 TArray<FString> Cards={Card};TArray<int32> Outfits={0};
 if(Switching){Cards.Add(OtherCard);Cards.Add(OtherCard);Outfits.Add(0);Outfits.Add(1);}
 OKClearRuntimePngCache();TArray<FString> FirstTextureData;TMap<int32,TArray<FString>> ReferenceTextures;uint64 FirstMisses=0;
 auto TextureData=[](UTexture2D* Texture){FString Description=FString::Printf(TEXT("%d/%d/%d/%d/%d/%d/"),Texture->GetSizeX(),Texture->GetSizeY(),Texture->SRGB?1:0,int32(Texture->Filter),int32(Texture->AddressX),int32(Texture->AddressY));
  for(const auto& Mip:Texture->GetPlatformData()->Mips){FSHAHash Hash;const auto Size=Mip.BulkData.GetBulkDataSize();const auto* Bytes=Mip.BulkData.LockReadOnly();if(Bytes)FSHA1::HashBuffer(Bytes,Size,Hash.Hash);Mip.BulkData.Unlock();Description+=FString::Printf(TEXT("%d:%d:%lld:"),Mip.SizeX,Mip.SizeY,Size)+Hash.ToString()+TEXT("/");}return Description;};
 for(int32 Run=0;Run<Cards.Num()*2;++Run){FString Error;FOkSession Session;
  const int32 Slot=Run%Cards.Num();const bool Replay=Run>=Cards.Num();
  Good=TestTrue(TEXT("Parse switched card metadata"),UKKCardReader::LoadFromFile(Cards[Slot],Selected,Error))&&Good;
  const double Begin=FPlatformTime::Seconds();bool Loaded=Session.Open(Root,Cards[Slot],Outfits[Slot],Error);const double Opened=FPlatformTime::Seconds();
  auto* Actor=World->SpawnActor<AKKPreviewActor>();Actor->Session=MoveTemp(Session);
  if(!Replay)Loaded=Loaded&&Actor->LoadSession(Error);
  else {
   Good=TestFalse(TEXT("Unstarted incremental load cannot publish"),Actor->ContinueSessionLoad(Error))&&Good;Error.Reset();
   Loaded=Loaded&&Actor->BeginSessionLoad(Error);
   Good=TestEqual(TEXT("Setup publishes no partial renderers"),Actor->Parts.Num(),0)&&Good;
   int32 Steps=0;double WorstBatchMs=0.;
   while(Loaded&&!Actor->IsSessionLoadComplete()&&Steps<=Actor->Session.PartCount()+AKKPreviewActor::SessionLoadSetupStages){
    const int32 Before=Actor->Parts.Num();const double BatchStart=FPlatformTime::Seconds();
    Loaded=Actor->ContinueSessionLoad(Error);WorstBatchMs=FMath::Max(WorstBatchMs,(FPlatformTime::Seconds()-BatchStart)*1000.);++Steps;
    Good=TestTrue(TEXT("Each batch adds at most one source renderer"),Actor->Parts.Num()-Before<=1)&&Good;
   }
   Good=TestTrue(TEXT("Incremental load reaches completion without skipping source slots"),Loaded&&Actor->IsSessionLoadComplete())&&Good;
   UE_LOG(LogTemp,Display,TEXT("Incremental preview audit steps=%d maximum_batch_ms=%.3f"),Steps,WorstBatchMs);
  }
  const double Built=FPlatformTime::Seconds();
  const bool ShapeChanged=Loaded&&(Selected.Face.ShapeValueFace!=Actor->Session.ShapeFace()||Selected.Body.ShapeValueBody!=Actor->Session.ShapeBody());
  if(ShapeChanged)Actor->ApplyShapes(Selected.Face.ShapeValueFace,Selected.Body.ShapeValueBody);const double Shaped=FPlatformTime::Seconds();
  if(Loaded){Actor->Session.ClipSources();Actor->Session.ClipNames();}const double Catalog=FPlatformTime::Seconds();
  UE_LOG(LogTemp,Display,TEXT("Warm card audit run=%d success=%d open_ms=%.3f build_ms=%.3f shape_ms=%.3f catalog_ms=%.3f total_ms=%.3f parts=%d error=%s"),Run+1,Loaded?1:0,(Opened-Begin)*1000.,(Built-Opened)*1000.,(Shaped-Built)*1000.,(Catalog-Shaped)*1000.,(Catalog-Begin)*1000.,Actor->Parts.Num(),*Error);
  Good=TestTrue(TEXT("Native card opens and renders"),Loaded)&&Good;
  if(Loaded){
   TInlineComponentArray<UMeshComponent*> Renderers(Actor);TMap<UMeshComponent*,bool> BeforeTicks;int32 ActiveTicks=0;
   for(auto* Component:Renderers){BeforeTicks.Add(Component,Component->IsComponentTickEnabled());ActiveTicks+=Component->IsComponentTickEnabled()?1:0;}
   Good=TestTrue(TEXT("Real preview has independently ticking renderers to suspend"),ActiveTicks>0)&&Good;
   const bool ActorTick=Actor->IsActorTickEnabled();Actor->SuspendDisplayTicks(true);
   for(auto* Component:Renderers)Good=TestFalse(TEXT("Inactive preview suspends every owned renderer tick"),Component->IsComponentTickEnabled())&&Good;
   auto* Late=NewObject<USkeletalMeshComponent>(Actor);Actor->AddInstanceComponent(Late);Late->SetComponentTickEnabled(true);
   Actor->SuspendDisplayTicks(true);
   Good=TestFalse(TEXT("Repeated suspension includes late incremental renderers"),Late->IsComponentTickEnabled())&&Good;
   Actor->SuspendDisplayTicks(false);
   for(const auto& Entry:BeforeTicks)Good=TestEqual(TEXT("Returning preview restores exact original component tick state"),Entry.Key->IsComponentTickEnabled(),Entry.Value)&&Good;
   Good=TestTrue(TEXT("Late renderer original state restored"),Late->IsComponentTickEnabled())&&Good;
   Good=TestEqual(TEXT("Display suspension preserves actor playback state"),Actor->IsActorTickEnabled(),ActorTick)&&Good;
   Late->DestroyComponent();
  }
  TArray<FString> Textures;for(UTexture2D* Texture:Actor->LoadedTextures)if(Texture)Textures.Add(TextureData(Texture));
  if(!Replay){ReferenceTextures.Add(Slot,Textures);if(Slot==0)FirstTextureData=Textures;}
  else Good=TestTrue(TEXT("Warm textures preserve every uploaded mip and sampler/color setting after card/outfit switches"),Textures==ReferenceTextures.FindChecked(Slot))&&Good;
  if(Switching&&Run==0&&Loaded){auto Body=Selected.Body.ShapeValueBody;if(!Body.IsEmpty()){
   Body[0]=Body[0]<.5f?.8f:.2f;Actor->ApplyShapes(Selected.Face.ShapeValueFace,Body);
   Good=TestTrue(TEXT("Changed height reaches native session"),Actor->Session.ShapeBody()==Body)&&Good;
   Actor->ApplyShapes(Selected.Face.ShapeValueFace,Selected.Body.ShapeValueBody);
   Good=TestTrue(TEXT("Saved height restored"),Actor->Session.ShapeBody()==Selected.Body.ShapeValueBody)&&Good;
   TArray<FString> AfterShape;for(UTexture2D* Texture:Actor->LoadedTextures)if(Texture)AfterShape.Add(TextureData(Texture));
   Good=TestTrue(TEXT("Shape edits preserve uploaded texture data"),AfterShape==Textures)&&Good;
  }}
  UE_LOG(LogTemp,Display,TEXT("Warm switch audit run=%d slot=%d outfit=%d replay=%d textures=%d"),Run+1,Slot,Outfits[Slot],Replay?1:0,Textures.Num());
  const auto Stats=OKRuntimePngCacheStats();if(!Replay)FirstMisses=Stats.Misses;else Good=TestEqual(TEXT("Fully warm load decodes no PNG or regenerates mips"),Stats.Misses,FirstMisses)&&Good;UE_LOG(LogTemp,Display,TEXT("Warm GUI shape unchanged=%d"),ShapeChanged?0:1);UE_LOG(LogTemp,Display,TEXT("Warm PNG cache run=%d hits=%llu misses=%llu bytes=%lld entries=%d"),Run+1,Stats.Hits,Stats.Misses,Stats.Bytes,Stats.Entries);if(Replay){Actor->DestroyAllParts();Good=TestFalse(TEXT("Cancellation clears published state"),Actor->IsSessionLoadComplete())&&Good;Good=TestFalse(TEXT("Cancelled load cannot resume stale resources"),Actor->ContinueSessionLoad(Error))&&Good;}
  Actor->Destroy();
 }
 UE_LOG(LogTemp,Display,TEXT("Warm texture parity valid=%d textures=%d"),Good?1:0,FirstTextureData.Num());
 World->DestroyWorld(false);GEngine->DestroyWorldContext(World);return Good&&!HasAnyErrors();
}
#endif
