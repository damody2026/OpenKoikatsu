#include "OKCrowdRuntimeScene.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "OKRuntimeSkinEnvelope.h"
#include "Async/ParallelFor.h"
#include "OKCrowdScene.h"
#include "KKRuntimePoseNative.h"
#include "KKRuntimeSpeech.h"
#include "KKRuntimeGpuDiskCache.h"
#include "Components/AudioComponent.h"
#include "AudioDevice.h"
#include "KKRuntimeMeshReduction.h"
#include "KKRuntimeRenderRig.h"
#include "KKRuntimeRenderMesh.h"
#include "KKRuntimeGpuRig.h"
#include "OKGpuRig.h"
#include "KKRuntimeShadowSubmit.h"
#include "KKRuntimeSharedMaterial.h"
#include "Materials/Material.h"
#include "KKRuntimeDisplayProfile.h"
#include "Materials/MaterialRelevance.h"
#include "RHIShaderPlatform.h"
#include "OKGpuAnimationManifest.h"
#include "OKRuntimeGpuPlayback.h"
#include "OKGpuResidencyBudget.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/InstancedSkinnedMeshComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/GameViewportClient.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkinnedAssetCommon.h"
#include "Engine/Texture2D.h"
#include "Animation/Skeleton.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/StrongObjectPtr.h"
#include "Containers/Ticker.h"
#include "Async/Async.h"
#include "Async/TaskGraphInterfaces.h"
#include "Json.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "ShaderCodeLibrary.h"
#include "ScenePrivate.h"
#include "Skinning/SkinningTransformProvider.h"
#include "RenderGraphBuilder.h"
#include "UnifiedBuffer.h"
#include "RenderingThread.h"
#include "RenderCommandFence.h"
#include "SkinningSceneExtensionProxy.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "ImageUtils.h"
#include "EngineGlobals.h"
#include "HAL/IConsoleManager.h"
#include "OKHGShading.h"
#include "KKImportedCharacter.h"
#include "KKCardReader.h"
#include "UObject/UObjectIterator.h"
#include "SkeletalRenderPublic.h"
#include "SkinningDefinitions.h"
#include "RHIGPUReadback.h"
#include "RenderGraphUtils.h"
#include "SceneViewExtension.h"
#include "RendererInterface.h"
#include "SceneRendering.h"
#include "Modules/ModuleManager.h"
#include "PrimitiveSceneInfo.h"
#include "SkeletalMeshSceneProxy.h"
#include "OKSourceMaterialMesh.h"
#include "Misc/SecureHash.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "ProfilingDebugging/TraceAuxiliary.h"
#include "Serialization/BufferArchive.h"

namespace OKRuntimeScene {
// Bounded simultaneous admissions. Upload acknowledgement is not activation:
// the game thread publishes that actor's clip catalog and CPU keys first.
// Render playback then overlays only that actor's rows. Each actor keeps a
// private session so a cancelled prepare cannot publish on a later request.
// The bank is validated on the preparing worker; the render thread uploads it a chunk per frame (one whole upload in a
// single render command stalled the street 60-140 ms, "AdmitRuntimeAction"). Status 0 uploading, 1 ready, -1 failed.
// What a retired streamed action held (its prepared clips and keys, its GPU bank's CPU copy: megabytes) freed on a
// worker instead of in this frame: freed on the game thread it held the frame up to 15 ms (STAT_OKCrowd_StreamRetire).
// Only for plain data nothing else on the game thread still points into; shared pointers must be thread-safe.
// A character's components follow her transform only when it moved: SetWorldTransform recomputes bounds and marks the
// render transform dirty even for the same transform, six components for every character every frame (most stand still).
static void OKCrowdSetTransform(USceneComponent* C,const FTransform& T){if(C&&!C->GetComponentTransform().Equals(T,1e-4))C->SetWorldTransform(T);}
// How her mouth moves (each switch off gives the earlier behaviour, for comparing).
static TAutoConsoleVariable<int32> CVarOKMouthNormalize(TEXT("ok.Mouth.Normalize"),1,TEXT("1: the speaking mouth opens against the line's own loudest; 0: Rms x 3."));
// The card's open-mouth morph at full weight is not a talking mouth: the lips vanish into a white blob (tested on 三月七);
// her loudest syllable opens it this far.
static TAutoConsoleVariable<float> CVarOKMouthMaxOpen(TEXT("ok.Mouth.MaxOpen"),.5f,TEXT("How far the speaking mouth opens at her loudest (0-1 of the card's open-mouth morph)."));
static TAutoConsoleVariable<int32> CVarOKMouthSpeakFull(TEXT("ok.Mouth.SpeakFull"),1,TEXT("1: speaking, the mouth opens with the voice, not capped by the expression's mouth_open; 0: capped."));
static TAutoConsoleVariable<int32> CVarOKMouthAlwaysDriven(TEXT("ok.Mouth.AlwaysDriven"),1,TEXT("1: silent, the mouth is closed (or the expression's mouth_open); 0: left in the card's own mouth."));
// Outline LOD: a character farther from the camera than this (cm) draws without her outline shells (their sections
// hidden on her main component: no draw calls, not just transparent). 0 (default): always drawn.
static TAutoConsoleVariable<float> CVarOKCrowdOutlineRange(TEXT("ok.Crowd.OutlineRange"),0.f,TEXT("cm: farther characters draw no outline shells (0: always)"));
static TAutoConsoleVariable<int32> CVarOKCrowdLogSlowEntries(TEXT("ok.Crowd.LogSlowEntries"),0,TEXT("1: log a character's frame update taking over 1 ms, split into speech/behaviour, face look, face upload and components."));
template<typename... THeld> static void OKCrowdFreeOnWorker(THeld&&... Held){
 Async(EAsyncExecution::ThreadPool,[...Held=MoveTemp(Held)]()mutable{(Held.Reset(),...);});
}
struct FStreamInstall {
 TSharedPtr<const FOKValidatedGpuBank,ESPMode::ThreadSafe> Bank;
 TAtomic<int32> Status{0};TAtomic<bool> InFlight{false};FString Error;
 bool Started=false;uint64 Scratch=0; // render thread only
};
struct FRenderState {
 FGuid ProviderId=FGuid::NewGuid();
 const FOKGpuAnimationBundle* Bank=nullptr;TSharedPtr<const FOKValidatedGpuBank,ESPMode::ThreadSafe> Validated;TAtomic<int32> UploadStatus{0};TAtomic<uint64> UploadChunks{0};
 TSet<const FMaterialRenderProxy*> SourceIblMaterials;TMap<const USkinnedAsset*,int32> MeshActors;TArray<FPrimitiveComponentId> LateDepthProxies;TSet<FPrimitiveComponentId> CpuSourceProxies;FSceneInterface* Scene=nullptr;TAtomic<uint64> LateDepthDraws{0},SourceCommandBuilds{0},SourceCommandReuses{0};
 TArray<TArray<FBoneIndexType>> BoneMaps;
 TArray<EBoneTransformStorageMode> StorageModes;
 TMap<const USkinnedAsset*,FPrimitiveComponentId> CpuReferences;
 struct FExpected {uint32 Offset=0;FCompressedBoneTransform Matrix;};TArray<FExpected> Expected;
 TUniquePtr<FRHIGPUBufferReadback> Readback;uint32 ReadBytes=0;
 bool VerifyCapture=false,ReadPending=false;TAtomic<bool> ExpectedReady{false};
 TAtomic<uint64> VerifiedMatrices{0},MatrixMismatches{0};TAtomic<double> MatrixMaxError{0};
 TArray<FOKGpuSkinPaletteData> Sources;
 TArray<TUniquePtr<FOKGpuSkinPalette>> Palettes;
 FOKResidentGpuPose Pose;FOKGpuSkinPaletteBatch PaletteBatch;
 // GPU runtime rig (manifest gpu_rig): the bank keeps only its layout; the rig writes the pose rows every frame.
 TSharedPtr<const FOKGpuRigBatch,ESPMode::ThreadSafe> RigBatch;FOKGpuRig Rig;float RigDt=1.f/60.f;
 // Rig check (manifest gpu_rig_verify N): the banks stay full, both evaluate every frame, and N frames of their rows are
 // read back and compared (translation in cm, the 3x3 part unitless); the rig's rows are the ones shown.
 int32 RigVerifyLeft=0;FString RigDumpPath;TArray<FString> RigRowNames;TArray<uint32> RigRowActors;TArray<float> RigBlends;TAtomic<uint64> RigBlendRows{0};TUniquePtr<FRHIGPUBufferReadback> RigReadBank,RigReadRig;uint32 RigReadBytes=0;
 TAtomic<uint64> RigChecks{0},RigRows{0},RigBadRows{0};TAtomic<double> RigMaxLinear{0},RigMaxTranslation{0},RigMeanTranslation{0};TAtomic<int32> RigWorstRow{-1};
 void PollRigCheck(){
  if(!RigReadBank||!RigReadBank->IsReady()||!RigReadRig->IsReady())return;
  const float* A=static_cast<const float*>(RigReadBank->Lock(RigReadBytes));const float* B=static_cast<const float*>(RigReadRig->Lock(RigReadBytes));
  const uint32 Rows=RigReadBytes/48;double Linear=RigMaxLinear.Load(),Translation=RigMaxTranslation.Load(),Sum=0;int32 Worst=RigWorstRow.Load();uint64 Bad=0;
  // A crossfading actor is left out: the bank blends two solved clips' final matrices, the rig blends the clips'
  // locals and solves once, and neither is the native crossfade.
  uint64 Blending=0;
  for(uint32 R=0;R<Rows;++R){if(RigRowActors.IsValidIndex(int32(R))&&RigBlends.IsValidIndex(int32(RigRowActors[R]))){const float W=RigBlends[RigRowActors[R]];if(W>1e-4f&&W<1-1e-4f){++Blending;continue;}}
   double L=0,T=0;for(int32 K=0;K<12;++K){const double D=FMath::Abs(double(A[R*12+K])-double(B[R*12+K]));if(K%4==3)T=FMath::Max(T,D);else L=FMath::Max(L,D);}
   Sum+=T;if(T>0.5||L>0.01)++Bad;if(T>Translation){Translation=T;Worst=int32(R);}Linear=FMath::Max(Linear,L);}
  if(RigChecks.Load()==0&&!RigDumpPath.IsEmpty()){
   // First check: both row sets (rig.bin: bank then rig, 12 floats a row) and each row's actor/bone (rig.txt).
   TArray<uint8> Dump;Dump.Append(reinterpret_cast<const uint8*>(A),RigReadBytes);Dump.Append(reinterpret_cast<const uint8*>(B),RigReadBytes);
   FFileHelper::SaveArrayToFile(Dump,*(RigDumpPath+TEXT(".bin")));FFileHelper::SaveStringArrayToFile(RigRowNames,*(RigDumpPath+TEXT(".txt")));
   TArray<FString> Blends;for(const float W:RigBlends)Blends.Add(FString::SanitizeFloat(W));FFileHelper::SaveStringArrayToFile(Blends,*(RigDumpPath+TEXT("-blend.txt")));}
  RigReadBank->Unlock();RigReadRig->Unlock();RigReadBank.Reset();RigReadRig.Reset();
  const uint64 Checks=RigChecks.Load()+1;RigMeanTranslation.Store((RigMeanTranslation.Load()*double(Checks-1)+(Rows?Sum/Rows:0))/double(Checks));
  RigMaxLinear.Store(Linear);RigMaxTranslation.Store(Translation);RigWorstRow.Store(Worst);RigRows+=Rows-Blending;RigBadRows+=Bad;RigBlendRows+=Blending;RigChecks.Store(Checks);
  UE_LOG(LogTemp,Display,TEXT("APNG GPU rig check %llu: %u rows (%llu crossfading left out), %llu off, max translation %.4f cm (row %d), max linear %.5f, mean translation %.4f cm"),Checks,Rows,Blending,Bad,Translation,Worst,Linear,RigMeanTranslation.Load());
 }TAtomic<uint64> PaletteLayoutBuilds{0},PaletteLayoutReuses{0};
 // Fixed poses: FOKResidentGpuPose is not movable. Two overlays are the
 // crowd-wide cap; each pays its own residency charge or keeps the current pose.
 struct FStreamSlot {TUniquePtr<FOKResidentGpuPose> Pose=MakeUnique<FOKResidentGpuPose>();TSharedPtr<FStreamInstall,ESPMode::ThreadSafe> Stream;int32 Actor=INDEX_NONE;uint64 Session=0;bool Reset=true;uint64 Frame=0;uint64 ScratchLease=0;};
 FStreamSlot Streams[OKCrowdRuntimeActionSlots];TAtomic<uint64> StreamGpuDispatches{0};
 TArray<FOKGpuPosePlayback> Playback;
 TSharedRef<FOKGpuResidencyBudget,ESPMode::ThreadSafe> Budget=OKGetCrowdGpuResidencyBudget();
 TAtomic<uint64> Passes{0},CpuUploads{0},Failures{0};TAtomic<bool> Ready{false};
 bool ProviderRegistered=false,Initialized=false,Reset=true,Enabled=false,UseCpu=false;uint64 Frame=0;
 void Provide(FSkinningTransformProvider::FProviderContext& C){
#if !USE_COMPRESSED_BONE_TRANSFORM
  if(!Enabled||!Bank||Playback.Num()!=Bank->Manifest.Actors.Num())return;
  FString Error;auto& G=C.GraphBuilder;
  if(UseCpu){
   struct FCpuPlan{FPrimitiveComponentId Reference;uint32 Current,Previous,Count;TArray<FBoneIndexType> Map;EBoneTransformStorageMode Mode;};
   TArray<FCpuPlan> Plans;uint32 Total=0;
   for(const auto& I:C.Indirections){if(!C.Proxies.IsValidIndex(I.Index)||!C.Proxies[I.Index]){Fail(TEXT("CPU provider private proxy missing"));return;}const auto* Proxy=C.Proxies[I.Index];const auto* Reference=CpuReferences.Find(Proxy->GetSkinnedAsset());const uint32 Count=Proxy->GetMaxBoneTransformCount();if(!Reference||!Count||(Proxy->GetBoneTransformStorageMode()==EBoneTransformStorageMode::BoneMap&&uint32(Proxy->GetBoneMap().Num())!=Count)||I.CurrentTransformOffset%sizeof(FCompressedBoneTransform)||I.PreviousTransformOffset%sizeof(FCompressedBoneTransform)){Fail(TEXT("CPU provider palette layout invalid"));return;}auto& P=Plans.AddDefaulted_GetRef();P.Reference=*Reference;P.Current=I.CurrentTransformOffset/sizeof(FCompressedBoneTransform);P.Previous=I.PreviousTransformOffset/sizeof(FCompressedBoneTransform);P.Count=Count;P.Mode=Proxy->GetBoneTransformStorageMode();P.Map.Append(Proxy->GetBoneMap().GetData(),Proxy->GetBoneMap().Num());Total+=Count*2;}
   if(Total){auto* Upload=G.AllocObject<FRDGAsyncScatterUploadBuffer>();C.ScatterUploadBuilder.AddPass(G,*Upload,C.TransformBuffer,Total,sizeof(FCompressedBoneTransform),TEXT("Crowd.CpuPoseUpload"),[this,Plans=MoveTemp(Plans),CopyCurrent=Reset](FRDGScatterUploader& Uploader){
    for(const auto& P:Plans){const auto* Info=Scene?Scene->GetPrimitiveSceneInfo(P.Reference):nullptr;const auto* Object=Info&&Info->Proxy?static_cast<const FSkeletalMeshSceneProxy*>(Info->Proxy)->GetMeshObject():nullptr;if(!Object){Fail(TEXT("CPU provider reference unavailable"));continue;}const auto T=Object->GetBoneTransforms();auto Current=Uploader.Add_GetRef<FCompressedBoneTransform>(P.Current,P.Count),Previous=Uploader.Add_GetRef<FCompressedBoneTransform>(P.Previous,P.Count);for(uint32 K=0;K<P.Count;++K){const uint32 Bone=P.Mode==EBoneTransformStorageMode::BoneMap?P.Map[K]:K;if(Bone>=uint32(T.CurrentTransforms.Num())){Fail(TEXT("CPU provider reference bone outside palette"));Current[K]=FCompressedBoneTransform{};Previous[K]=Current[K];continue;}StoreCompressedBoneTransform(&Current[K],T.CurrentTransforms[Bone]);const bool HasPrevious=!CopyCurrent&&Bone<uint32(T.PreviousTransforms.Num());if(HasPrevious)StoreCompressedBoneTransform(&Previous[K],T.PreviousTransforms[Bone]);else Previous[K]=Current[K];}}
   });}
   Reset=false;++Frame;++CpuUploads;return;
  }
  if(!Initialized){if(!Pose.InitializeManaged(G,Bank->Data.Pose,Bank->Data.OwnedClips,Budget,Error)){Fail(Error);return;}Palettes.SetNum(Sources.Num());BoneMaps.SetNum(Sources.Num());StorageModes.SetNum(Sources.Num());Initialized=true;}
  FOKResidentPoseFrame FrameData;
  if(RigBatch){
   if(!Rig.IsInitialized()&&!Rig.Initialize(G,RigBatch.ToSharedRef(),Error)){Fail(Error);return;}
   if(!Rig.Evaluate(G,Playback,Reset,RigDt,FrameData,Error)){Fail(Error);return;}FrameData.Generation=Pose.GetStats().Generation;++Frame;
   PollRigCheck();
   if(RigVerifyLeft>0&&!RigReadBank&&!Reset){FOKResidentPoseFrame Full;if(!Pose.Evaluate(G,Playback,Frame,Pose.GetStats().Generation,false,Full,Error)){Fail(Error);return;}
    RigReadBytes=uint32(FrameData.Current->Desc.GetSize());if(Full.Current->Desc.GetSize()!=RigReadBytes){Fail(TEXT("GPU rig rows differ from the bank's"));return;}
    RigReadBank=MakeUnique<FRHIGPUBufferReadback>(TEXT("Crowd.RigCheckBank"));RigReadRig=MakeUnique<FRHIGPUBufferReadback>(TEXT("Crowd.RigCheckRig"));
    RigBlends.Reset();for(const auto& P:Playback)RigBlends.Add(P.Times.Z);AddEnqueueCopyPass(G,RigReadBank.Get(),Full.Current,RigReadBytes);AddEnqueueCopyPass(G,RigReadRig.Get(),FrameData.Current,RigReadBytes);--RigVerifyLeft;}
  }else if(!Pose.Evaluate(G,Playback,++Frame,Pose.GetStats().Generation,Reset,FrameData,Error)){Fail(Error);return;}
  // One full-frame copy, then each active actor replaces only its own rows.
  // Slots do not share history. Releasing one must not reset the other.
  struct FCopied {uint64 Offset=0,Bytes=0;};TArray<FCopied,TInlineAllocator<OKCrowdRuntimeActionSlots>> Copied;
  FRDGBufferRef OverlayCurrent=nullptr,OverlayPrevious=nullptr;
  for(auto& Slot:Streams){
   if(!Slot.Stream||Slot.Stream->Status.Load()!=1||Slot.Actor==INDEX_NONE||!Slot.Pose)continue;
   if(!Playback.IsValidIndex(Slot.Actor)||!Bank->Manifest.Actors.IsValidIndex(Slot.Actor)){Fail(TEXT("Runtime streamed actor ownership invalid"));return;}
   FOKResidentPoseFrame Private;const auto& Play=Playback[Slot.Actor];
   if(!Slot.Pose->Evaluate(G,MakeArrayView(&Play,1),++Slot.Frame,Slot.Pose->GetStats().Generation,Reset||Slot.Reset,Private,Error)){Fail(Error);return;}
   const auto& Actor=Bank->Manifest.Actors[Slot.Actor];const uint64 Offset=uint64(Actor.PoseFirst)*48,Bytes=uint64(Actor.PoseCount)*48;
   if(!Private.Current||!Private.Previous||Private.Current->Desc.GetSize()!=Bytes||Private.Previous->Desc.GetSize()!=Bytes){Fail(TEXT("Runtime streamed pose range differs from private rig"));return;}
   for(const auto& Prior:Copied)if(Offset<Prior.Offset+Prior.Bytes&&Prior.Offset<Offset+Bytes){Fail(TEXT("Runtime streamed pose ranges overlap"));return;}
   if(!OverlayCurrent){OverlayCurrent=G.CreateBuffer(FrameData.Current->Desc,TEXT("Crowd.StreamedCurrent"));OverlayPrevious=G.CreateBuffer(FrameData.Previous->Desc,TEXT("Crowd.StreamedPrevious"));AddCopyBufferPass(G,OverlayCurrent,FrameData.Current);AddCopyBufferPass(G,OverlayPrevious,FrameData.Previous);}
   AddCopyBufferPass(G,OverlayCurrent,Offset,Private.Current,0,Bytes);AddCopyBufferPass(G,OverlayPrevious,Offset,Private.Previous,0,Bytes);
   Copied.Add({Offset,Bytes});Slot.Reset=false;++StreamGpuDispatches;
  }
  if(OverlayCurrent){FrameData.Current=OverlayCurrent;FrameData.Previous=OverlayPrevious;}
  TArray<FOKGpuSkinPaletteJob> Jobs;
  struct FCheckPlan {FPrimitiveComponentId Reference;uint32 Offset;TArray<FBoneIndexType> Map;EBoneTransformStorageMode Mode;};TArray<FCheckPlan> Plans;
  const bool TakeSample=VerifyCapture&&!ReadPending&&VerifiedMatrices.Load()==0;
  for(const auto& I:C.Indirections){
   if(!C.Proxies.IsValidIndex(I.Index)||!C.Proxies[I.Index]){Fail(TEXT("Runtime provider requires private mesh proxy ownership"));return;}
   const auto* Proxy=C.Proxies[I.Index];const int32* Actor=MeshActors.Find(Proxy->GetSkinnedAsset());
   if(!Actor||Proxy->GetUniqueAnimationCount()!=1||I.CurrentTransformOffset%16||I.PreviousTransformOffset%16){Fail(TEXT("Runtime private mesh layout differs"));return;}
   const auto Mode=Proxy->GetBoneTransformStorageMode();const auto Map=Proxy->GetBoneMap();
   const bool Changed=!Palettes[*Actor]||StorageModes[*Actor]!=Mode||BoneMaps[*Actor].Num()!=Map.Num()||(Map.Num()&&FMemory::Memcmp(BoneMaps[*Actor].GetData(),Map.GetData(),Map.Num()*sizeof(FBoneIndexType)));
   if(Changed){
    const auto& Source=Sources[*Actor];const uint32 Count=Mode==EBoneTransformStorageMode::BoneMap?Map.Num():Source.Items.Num();
    if(!Count||Count!=Proxy->GetMaxBoneTransformCount()){Fail(TEXT("Runtime render bone map count differs"));return;}
    FOKGpuSkinPaletteData Mapped;Mapped.PoseCount=Source.PoseCount;
    for(uint32 K=0;K<Count;++K){const uint32 Bone=Mode==EBoneTransformStorageMode::BoneMap?Map[K]:K;if(Bone>=uint32(Source.Items.Num())){Fail(TEXT("Runtime render bone map outside private mesh"));return;}auto Item=Source.Items[Bone];const uint32 Bind=Item.Y;Item.Y=Mapped.InverseBindRows.Num();Item.Z=K;Mapped.Items.Add(Item);Mapped.InverseBindRows.Append(Source.InverseBindRows.GetData()+Bind,3);}
    auto P=MakeUnique<FOKGpuSkinPalette>();if(!P->Initialize(G,Mapped,FrameData.Generation,Error)){Fail(Error);return;}Palettes[*Actor]=MoveTemp(P);BoneMaps[*Actor].Reset();BoneMaps[*Actor].Append(Map.GetData(),Map.Num());StorageModes[*Actor]=Mode;
   }
   Jobs.Add({Palettes[*Actor].Get(),I.CurrentTransformOffset/16,I.PreviousTransformOffset/16,Reset});
   if(TakeSample){const auto* Reference=CpuReferences.Find(Proxy->GetSkinnedAsset());if(Reference){FCheckPlan Plan{*Reference,I.CurrentTransformOffset,{},Mode};Plan.Map.Append(Map.GetData(),Map.Num());Plans.Add(MoveTemp(Plan));}}
  }
  if(!Jobs.IsEmpty()&&!PaletteBatch.AddPass(G,FrameData,C.TransformBuffer,Jobs,Error)){Fail(Error);return;}
  const auto LayoutStats=PaletteBatch.GetStats();PaletteLayoutBuilds=LayoutStats.LayoutBuilds;PaletteLayoutReuses=LayoutStats.LayoutReuses;
  if(TakeSample&&!Plans.IsEmpty()){ReadBytes=C.TransformBuffer->Desc.GetSize();ReadPending=true;ExpectedReady.Store(false);Readback=MakeUnique<FRHIGPUBufferReadback>(TEXT("APNG.ScenePaletteComparison"));C.ScatterUploadBuilder.AddFunction([this,Plans](FRHICommandList&){Expected.Reset();for(const auto& Plan:Plans){const auto* Info=Scene?Scene->GetPrimitiveSceneInfo(Plan.Reference):nullptr;if(!Info||!Info->Proxy){++MatrixMismatches;continue;}const auto* Reference=static_cast<const FSkeletalMeshSceneProxy*>(Info->Proxy)->GetMeshObject();if(!Reference){++MatrixMismatches;continue;}const auto Transforms=Reference->GetBoneTransforms().CurrentTransforms;const uint32 Count=Plan.Mode==EBoneTransformStorageMode::BoneMap?Plan.Map.Num():Transforms.Num();for(uint32 K=0;K<Count;++K){const uint32 Bone=Plan.Mode==EBoneTransformStorageMode::BoneMap?Plan.Map[K]:K;if(Bone>=uint32(Transforms.Num())||uint64(Plan.Offset)+uint64(K+1)*48>ReadBytes){++MatrixMismatches;continue;}auto& E=Expected.AddDefaulted_GetRef();E.Offset=Plan.Offset+K*48;StoreCompressedBoneTransform(&E.Matrix,Transforms[Bone]);}}ExpectedReady.Store(true);});AddEnqueueCopyPass(G,Readback.Get(),C.TransformBuffer,ReadBytes);}
  Reset=false;++Passes;Ready.Store(true);
#else
  Fail(TEXT("Runtime provider does not support compressed transform storage"));
#endif
 }
 void ReleaseStreamIndex(int32 Index,FRHICommandListImmediate& Cmd){if(Index<0||Index>=OKCrowdRuntimeActionSlots)return;auto& S=Streams[Index];if(S.Pose)S.Pose->Release();S.Stream.Reset();S.Actor=INDEX_NONE;S.Session=0;S.Reset=true;S.Frame=0;if(S.ScratchLease){Budget->Retire(S.ScratchLease,{},Cmd);S.ScratchLease=0;}}
 void ReleaseStream(FRHICommandListImmediate& Cmd){for(int32 Index=0;Index<OKCrowdRuntimeActionSlots;++Index)ReleaseStreamIndex(Index,Cmd);}
 void Fail(const FString& Error){++Failures;Ready.Store(false);UE_LOG(LogTemp,Error,TEXT("APNG GPU provider: %s"),*Error);}
 void PollComparison(){if(!ReadPending||!ExpectedReady.Load()||!Readback->IsReady())return;const auto* Bytes=static_cast<const uint8*>(Readback->Lock(ReadBytes));double Max=0;uint64 Errors=0;for(const auto& E:Expected){const float* A=reinterpret_cast<const float*>(Bytes+E.Offset);const float* B=reinterpret_cast<const float*>(&E.Matrix);for(int32 K=0;K<12;++K){const double Difference=FMath::Abs(double(A[K])-B[K]);if(!FMath::IsFinite(A[K])||Difference>.001)++Errors;Max=FMath::Max(Max,Difference);}}Readback->Unlock();ReadPending=false;VerifiedMatrices.Store(Expected.Num());MatrixMismatches.Store(MatrixMismatches.Load()+Errors);MatrixMaxError.Store(Max);UE_LOG(LogTemp,Display,TEXT("APNG actual scene palette matrices=%llu max_error=%.9f mismatches=%llu"),VerifiedMatrices.Load(),Max,MatrixMismatches.Load());}
};
// Source IBL color-mask-zero depth belongs after opaque room color, not in
// Unreal's early-Z prepass, which otherwise erases the background at alpha-zero pixels.
class FLateSourceDepth:public FSceneViewExtensionBase {
 TSharedRef<FRenderState,ESPMode::ThreadSafe> State;FSceneInterface* Scene;
 TSharedRef<FOKSourceMaterialCommandCache,ESPMode::ThreadSafe> Commands=OKCreateSourceMaterialCommandCache();
 TSharedRef<FOKSourceMaterialCommandCache,ESPMode::ThreadSafe> ColorCommands=OKCreateSourceMaterialCommandCache();
 FDelegateHandle PostOpaqueHandle;
public:
 FLateSourceDepth(const FAutoRegister& Auto,TSharedRef<FRenderState,ESPMode::ThreadSafe> In,FSceneInterface* InScene):FSceneViewExtensionBase(Auto),State(In),Scene(InScene){}
 void SetupViewFamily(FSceneViewFamily&)override{}void SetupView(FSceneViewFamily&,FSceneView&)override{}void BeginRenderViewFamily(FSceneViewFamily&)override{}
 void Register(){check(IsInRenderingThread());PostOpaqueHandle=FModuleManager::GetModuleChecked<IRendererModule>(TEXT("Renderer")).RegisterPostOpaqueRenderDelegate(FPostOpaqueRenderDelegate::CreateRaw(this,&FLateSourceDepth::Draw));}
 void Unregister(){check(IsInRenderingThread());if(PostOpaqueHandle.IsValid()){FModuleManager::GetModuleChecked<IRendererModule>(TEXT("Renderer")).RemovePostOpaqueRenderDelegate(PostOpaqueHandle);PostOpaqueHandle.Reset();}}
 void Draw(FPostOpaqueRenderParameters& Parameters){
  if(!Parameters.View||!Parameters.GraphBuilder||!Parameters.ColorTexture||!Parameters.DepthTexture)return;
  const FSceneView& View=*Parameters.View;auto& G=*Parameters.GraphBuilder;
  if(View.Family->Scene!=Scene)return;
  FOKSourceRasterState Source;Source.DepthWrite=true;Source.SrcBlend=Source.SrcAlphaBlend=1;Source.DstBlend=Source.DstAlphaBlend=0;FOKSourceRasterBinding Binding;FString Error;if(!OKPrepareSourceRasterState(Source,Binding,Error))return;Binding.Blend.RenderTargets[0].ColorWriteMask=CW_NONE;
  TArray<FOKSourceMaterialSceneDraw> Draws,DynamicDraws;
  TSet<const FPrimitiveSceneProxy*> EligibleProxies;
  for(const auto Id:State->LateDepthProxies){const auto* Info=Scene->GetPrimitiveSceneInfo(Id);if(!Info||!Info->Proxy)continue;const auto* Proxy=Info->Proxy;if(!Proxy->IsShown(&View)||View.HiddenPrimitives.Contains(Id)||(View.ShowOnlyPrimitives.IsSet()&&!View.ShowOnlyPrimitives->Contains(Id)))continue;EligibleProxies.Add(Proxy);for(const auto& Mesh:Info->StaticMeshes)if(State->SourceIblMaterials.Contains(Mesh.MaterialRenderProxy))(State->CpuSourceProxies.Contains(Id)?DynamicDraws:Draws).Add({&Mesh,Proxy});}
  // Conventional skeletal components submit frame-owned dynamic batches.
  // They must participate in the same source passes as retained GPU batches,
  // but their transient mesh addresses must never enter the persistent cache.
  for(const auto& Batch:Parameters.View->DynamicMeshElements)if(Batch.Mesh&&EligibleProxies.Contains(Batch.PrimitiveSceneProxy)&&State->SourceIblMaterials.Contains(Batch.Mesh->MaterialRenderProxy))DynamicDraws.Add({Batch.Mesh,Batch.PrimitiveSceneProxy});
  if(View.bIsSceneCapture){int32 Candidates=0;for(const auto& Batch:Parameters.View->DynamicMeshElements)Candidates+=EligibleProxies.Contains(Batch.PrimitiveSceneProxy);
   // Logged once for each different capture, not every captured frame (62,000 identical lines in ten minutes). Two
   // captures take turns (the galgame portrait's colour and coverage), so "when it changes" was every frame again.
   static TSet<FIntVector4> Seen;const FIntVector4 Capture(EligibleProxies.Num(),Draws.Num(),Candidates,DynamicDraws.Num());
   if(Seen.Num()<64&&!Seen.Contains(Capture)){Seen.Add(Capture);UE_LOG(LogTemp,Display,TEXT("APNG source capture: eligible=%d retained=%d dynamic_candidates=%d dynamic_source=%d"),Capture.X,Capture.Y,Capture.Z,Capture.W);for(const auto& Draw:Draws)UE_LOG(LogTemp,Display,TEXT("APNG source mesh: component=%u vf=%s"),Draw.Proxy->GetPrimitiveComponentId().PrimIDValue,Draw.Mesh->VertexFactory->GetType()->GetName());}}
  State->LateDepthDraws+=OKAddCachedSourceMaterialScenePass(G,View,Parameters.ColorTexture,Parameters.DepthTexture,Draws,Binding,Commands);
  for(const auto& Draw:DynamicDraws)if(OKAddSourceMaterialScenePass(G,View,Parameters.ColorTexture,Parameters.DepthTexture,*Draw.Mesh,Draw.Proxy,Binding))++State->LateDepthDraws;
  // Native IBL depth and alpha color share the exact same mesh, material and
  // vertex shader. Drawing its color through a different engine permutation
  // can self-reject against the depth prepass at intermediate animation poses.
  FOKSourceRasterState ColorSource;FOKSourceRasterBinding ColorBinding;
  if(OKPrepareSourceRasterState(ColorSource,ColorBinding,Error)){OKAddCachedSourceMaterialScenePass(G,View,Parameters.ColorTexture,Parameters.DepthTexture,Draws,ColorBinding,ColorCommands);for(const auto& Draw:DynamicDraws)OKAddSourceMaterialScenePass(G,View,Parameters.ColorTexture,Parameters.DepthTexture,*Draw.Mesh,Draw.Proxy,ColorBinding);}
  uint64 Builds=0,Reuses=0,ColorBuilds=0,ColorReuses=0;OKSourceMaterialCommandCacheStats(*Commands,Builds,Reuses);OKSourceMaterialCommandCacheStats(*ColorCommands,ColorBuilds,ColorReuses);State->SourceCommandBuilds.Store(Builds+ColorBuilds);State->SourceCommandReuses.Store(Reuses+ColorReuses);
 }
};
// Coalesce only bit-identical runtime bindings in adjacent source queue order.
// No atlas, discarded faces, welding, or assumed compatibility across shaders.
static bool RepackCompatibleSections(FKKRuntimeRenderMeshData& D,TConstArrayView<int32> Canonical,FString& Error){
 FKKRuntimeRenderMeshData Result;Result.Bones=D.Bones;Result.Materials=D.Materials;Result.Vertices.Reserve(D.Vertices.Num());Result.Weights.Reserve(D.Weights.Num());Result.Indices.Reserve(D.Indices.Num());
 TMap<uint16,uint16> BoneMap;TMap<uint32,uint32> Vertices;FKKRuntimeRenderSection Section;
 auto End=[&](){if(Section.Triangles){Section.NumVertices=Result.Vertices.Num()-Section.BaseVertex;Result.Sections.Add(MoveTemp(Section));}};
 auto Begin=[&](int32 Material){Section={};Section.Material=Material;Section.BaseVertex=Result.Vertices.Num();Section.BaseIndex=Result.Indices.Num();BoneMap.Reset();Vertices.Reset();};Begin(INDEX_NONE);
 for(const auto& Source:D.Sections){if(!Canonical.IsValidIndex(Source.Material)){Error=TEXT("Runtime material remap outside source sections");return false;}const int32 Material=Canonical[Source.Material];TSet<uint16> Needed;for(uint16 Bone:Source.BoneMap)if(!BoneMap.Contains(Bone))Needed.Add(Bone);
  if(Section.Material!=Material||BoneMap.Num()+Needed.Num()>256){End();Begin(Material);}
  for(uint32 I=Source.BaseIndex;I<Source.BaseIndex+Source.Triangles*3;++I){const uint32 Old=D.Indices[I];if(const auto* Existing=Vertices.Find(Old)){Result.Indices.Add(*Existing);continue;}const auto& V=D.Vertices[Old];auto Weight=D.Weights[Old];for(int32 K=0;K<V.Influences;++K){if(!Weight.InfluenceWeights[K])continue;const uint16 Bone=Source.BoneMap[Weight.InfluenceBones[K]];auto* Local=BoneMap.Find(Bone);if(!Local){const uint16 Index=Section.BoneMap.Num();Section.BoneMap.Add(Bone);BoneMap.Add(Bone,Index);Local=BoneMap.Find(Bone);}Weight.InfluenceBones[K]=*Local;}const uint32 Index=Result.Vertices.Num();Vertices.Add(Old,Index);Result.Vertices.Add(V);Result.Weights.Add(Weight);Result.Indices.Add(Index);}
  Section.Triangles+=Source.Triangles;
 }End();if(Result.Indices.Num()!=D.Indices.Num()){Error=TEXT("Runtime section coalescing changed triangle count");return false;}D=MoveTemp(Result);return true;
}
static TArray<uint8> RuntimeBindingIdentity(const FKKRuntimeMaterialPart& P,const FKKRuntimeMaterialBinding& B,int32 Queue){
 FBufferArchive Bytes;uint8 Recipe=uint8(OKRuntimeSharedMaterialRecipe(P));Bytes<<Recipe<<Queue;bool Outline=P.Outline;Bytes<<Outline;FString Blend=P.Blend;Bytes<<Blend;
 TArray<FName> Keys;B.Scalars.GetKeys(Keys);Keys.Sort(FNameLexicalLess());for(FName Key:Keys){FString Name=Key.ToString();float Value=B.Scalars[Key];Bytes<<Name<<Value;}B.Vectors.GetKeys(Keys);Keys.Sort(FNameLexicalLess());for(FName Key:Keys){FString Name=Key.ToString();auto Value=B.Vectors[Key];Bytes<<Name<<Value;}B.Textures.GetKeys(Keys);Keys.Sort(FNameLexicalLess());for(FName Key:Keys){FString Name=Key.ToString();int32 Value=B.Textures[Key];Bytes<<Name<<Value;}
 // Dynamic raster properties need to match even when the pixel program does not reflect them.
 TSharedPtr<FJsonObject> Root;const TSharedPtr<FJsonObject>* Material=nullptr;const TSharedPtr<FJsonObject>* Floats=nullptr;if(FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(P.SourceMetadataJson),Root)&&Root&&Root->TryGetObjectField(TEXT("material"),Material)&&(*Material)->TryGetObjectField(TEXT("floats"),Floats)){const TCHAR* Names[]={TEXT("_ZWrite"),TEXT("_SrcBlend"),TEXT("_DstBlend"),TEXT("_StencilRef"),TEXT("_StencilComp"),TEXT("_StencilOp"),TEXT("_StencilReadMask"),TEXT("_StencilWriteMask")};for(const TCHAR* N:Names){double Value=0;bool Present=(*Floats)->TryGetNumberField(N,Value);Bytes<<Present<<Value;}}
 return MoveTemp(Bytes);
}
struct FPrivatePackedBank {
 FBox Bounds=FBox(ForceInit);FString Error;
 TUniquePtr<FOKGpuAnimationBundle> Bank;
 TSharedPtr<const FOKValidatedGpuBank,ESPMode::ThreadSafe> Validated;
 TSharedPtr<const TArray<FVector4f>,ESPMode::ThreadSafe> CpuKeys;
 TArray<TArray<FUintVector4>> CpuTracks;
};
struct FStreamPrepared {
 FPrivatePackedBank Packed;TSharedPtr<const FOKValidatedGpuBank,ESPMode::ThreadSafe> Validated;TArray<FKKRuntimePoseResult> Clips;FBox Bounds=FBox(ForceInit);FString Error;double Ms=0;
};
// Koikatsu's foreground eyes / eyebrows (ChaForegroundComponent): which parts its card setting moves before the front
// hair. 2 the eyebrows (foregroundEyebrow), 1 the eyes (foregroundEyes), 0 neither.
static int32 OKForegroundFacePart(const FString& Name){
 if(Name.Equals(TEXT("cf_O_mayuge"),ESearchCase::IgnoreCase))return 2;
 static const TCHAR* Eyes[]={TEXT("cf_O_eyeline"),TEXT("cf_O_eyeline_low"),TEXT("cf_Ohitomi_L"),TEXT("cf_Ohitomi_R"),TEXT("cf_Ohitomi_L02"),TEXT("cf_Ohitomi_R02"),TEXT("cf_O_gag_eye_00"),TEXT("cf_O_gag_eye_01"),TEXT("cf_O_gag_eye_02")};
 for(const TCHAR* Eye:Eyes)if(Name.Equals(Eye,ESearchCase::IgnoreCase))return 1;return 0;
}
// Front hair: the hair shaders that skip the eyes' stencil (Shader Forge/main_hair_front, Koikano/hair_main_sun_front).
static bool OKFrontHairPart(const FKKRuntimeMaterialPart& P){
 TSharedPtr<FJsonObject> Root;const TSharedPtr<FJsonObject>* Mat=nullptr;FString Shader;
 if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(P.SourceMetadataJson),Root)||!Root||!Root->TryGetObjectField(TEXT("material"),Mat)||!(*Mat)->TryGetStringField(TEXT("shader"),Shader))return false;
 return Shader.Contains(TEXT("hair"),ESearchCase::IgnoreCase)&&Shader.EndsWith(TEXT("_front"),ESearchCase::IgnoreCase);
}
struct FEntry {
 TArray<bool> ViewLitMaterials,ViewLitOutlines;
 FVector DisplayLight=FVector(999,999,999);int32 DisplayMaterialCount=-1;
 FRenderCommandFence RevealFence;bool RevealQueued=false;
 TUniquePtr<FKKRuntimeRenderMeshData> CommitData;TArray<int32> CommitQueues,CommitCanonical;TArray<TArray<uint8>> CommitIdentities;int32 MaterialCursor=0,CommitStep=0;bool CommitReady=false;TUniquePtr<FKKRuntimeRenderMeshData> CommitMain,CommitVisual;TArray<UMaterialInterface*> CommitVisualMaterials;double CommitWorkMs=0;
 uint32 OriginalSections=0,MergedSections=0,UniqueMaterialBindings=0;
 FString Path,Id,Error;int32 Stage=0,TextureCursor=0,WalkIndex=1,SelectionIndex=0;
 int32 Outfit=0; // the card's coordinate she wears (manifest outfits, her everyday clothes)
 // Her actor in the composed animation bank: entries are composed in order, skipping the ones that failed to load.
 int32 BankActor=INDEX_NONE;bool ReducedBudget=false,SourceGeometryPreserved=false;
 uint64 SourceVertices=0,SourceTriangles=0;
 double StageStarted=0;double StageMs[5]={};double SnapshotMs[4]={};
 TFuture<FKKRuntimeSnapshotResult> Snapshot;
 TFuture<FKKRuntimeMeshReductionResult> Reduction;
 // GPU runtime rig: her skeleton, terms and clips (OKPrepareRuntimeGpuRigAsync); each clip's own length. Her banks keep
 // the native bones only (NativeOnly: the CPU's sockets and head look read them, the rig writes every row).
 TFuture<FKKRuntimeGpuRigResult> RigJob;FKKRuntimeGpuRigResult Rig;TArray<float> RigDurations;
 TFuture<FKKRuntimeRenderMeshData> RenderMesh;
 TArray<int32> PoseJobSlots;int32 ConversationSource=INDEX_NONE,RunSource=INDEX_NONE,ConversationSlot=INDEX_NONE,RunSlot=INDEX_NONE;bool ConversationIdle=false,Running=false;
 // Plain standing idle (f_tachi_00_00) for stops; the reading idle (slot 0) only when the scene allows it.
 int32 StandSlot=INDEX_NONE;bool ReadingAllowed=true;
 // Standing poses of her own for stops (manifest idle_variants, a few per character): IdleVariant picks one (none: as above).
 TArray<int32> IdleVariantSlots;int32 IdleVariant=INDEX_NONE;
 // Her outline shells' material slots on the main mesh, hidden when she is past ok.Crowd.OutlineRange.
 TArray<int32> OutlineSlotIds;bool OutlinesHidden=false;TWeakObjectPtr<UPrimitiveComponent> OutlineComponent;
 double BudgetRetryAt=0;bool BudgetWaitLogged=false;
 TArray<TFuture<FKKRuntimePoseResult>> PoseJobs;TArray<FKKRuntimePoseResult> Clips;TArray<int32> ClipSources;
 TSharedPtr<const FKKRuntimePoseRequest,ESPMode::ThreadSafe> PoseRecipe; /* shared, never changed: a streamed action copies it on a worker */FOKGpuActorRecord ActorRecipe;FOKGpuSkinPaletteData PaletteRecipe;
 TSharedPtr<const FOKRuntimeSkinEnvelopeGeometry,ESPMode::ThreadSafe> SkinEnvelope;
 TMap<int32,FString> ContactRecipes;
 double PendingActionAt=0;int32 PendingAction=INDEX_NONE;uint64 PendingEpoch=0,InstalledEpoch=0,EpochClock=0,LinkedActionEpoch=0;bool PendingPlay=true,StreamInstalled=false,StreamHold=false,SpeechPaired=false;uint64 StreamRequests=0,StreamInstalls=0,StreamReleases=0,StreamErrors=0;double StreamPrepareMs=0;
 TArray<FKKRuntimePoseResult> BaseClips;TSharedPtr<const TArray<FVector4f>,ESPMode::ThreadSafe> BaseKeys;int32 BaseSource=INDEX_NONE;
 int32 ActionOverride=INDEX_NONE;uint64 ActionRequests=0,ActionCompletions=0,SpeechRequests=0,SpeechFrames=0,LastSpeechFrames=0;float SpeechMouth=0,SpeechPeak=0,LinePeak=0,LineMouth=0;double SpeechProgressAt=0;
 TFuture<FKKRuntimeSpeechResult> SpeechJob;TWeakObjectPtr<UAudioComponent> SpeechAudio;TStrongObjectPtr<UKKRuntimeSpeechWave> SpeechWave;
 FString PendingSpeechAction;uint64 SpeechEpoch=0,SpeechEpochClock=0;
 // A song (OKSpeakCrowdRuntimeSong): the mouth follows this envelope of her vocal alone, read at the audio render clock
 // (hop seconds per value), not the mix's RMS; played at pitch 1.
 TArray<float> MouthEnvelope;float EnvelopeHop=.01f,EnvelopeRate=48000.f;
 double SpeechRequestedAt=0;uint64 SpeechCancellations=0;float BlinkOverride=-1,LastBlinkClosure=0,BlinkPeak=0;uint64 BlinkUpdates=0;
 TSharedPtr<const FKKRuntimeMouthStream,ESPMode::ThreadSafe> MouthStream;float LastMouthOpening=0,LastMouthGain=0;uint64 MouthUpdates=0;float MouthPeak=0;int64 LastMouthBucket=-1;double ActionSpeechSeconds=0,ActionSpeechGpuSeconds=0;
 FOKRuntimeFacePlayback FacePlay;
 // Head look (OKSetCrowdRuntimeHeadLook): the world point she looks at and whether she does, how far she turns to it
 // (eases 0..1), her neck / head bones (-2 not looked up yet) with the face direction in the head's own frame (from the
 // reference pose), and this frame's turns of neck and head (in their parents' frames), shared by GPU and CPU poses.
 FVector LookAt=FVector::ZeroVector;bool Looking=false;float LookWeight=0;int32 LookNeck=-2,LookHead=-2;FVector HeadFace=FVector::ZeroVector;
 FQuat NeckTurn=FQuat::Identity,HeadTurn=FQuat::Identity;bool Turned=false;
 // The card's own values of what OKSetCrowdRuntimeFaceLook moves (blush, gloss, iris offset, highlight), by material
 // and parameter, so every look is applied from the card and not on top of the last one.
 TMap<FString,FLinearColor> LookBase;
 // The face look drawn now and the one it eases to: OKSetCrowdRuntimeFaceLook sets the goal, the tick moves the drawn
 // values toward it (TickFaceLook), so a blush rises, tears well up and the eyes glide instead of switching at once.
 // Blush / sweat 0..1 over the card's own, gaze in iris units, highlight 1 shown, Tear[level] visibility (1..3).
 struct FLookValues{float Blush=0.f,Sweat=0.f,Highlight=1.f;FVector2D Gaze=FVector2D::ZeroVector;float Tear[4]={0.f,0.f,0.f,0.f};};
 FLookValues LookNow,LookGoal;bool LookFading=false;float LookFadeScale=1.f;
 // What each of Materials is to the face look, kept from creation (the part data is dropped once the mesh is up):
 // 1 the face's skin, 2 other skin, 3 an iris, 4 a tear, 5 a brow, 0 anything else.
 TArray<uint8> LookKinds;
 // Her tears (cf_O_namida_S/M/L, levels 1..3): own materials, hidden (reveal override 0) unless a look shows them.
 TMap<UMaterialInstanceDynamic*,int32> Tears;
 // Koikatsu's foreground eyes / eyebrows from her card (foregroundEyes / foregroundEyebrow; 0 follows the game setting,
 // off as in its default config). Those parts take the <Parent>_Fg variants, and FrontGpu draws her front hair again
 // into custom depth / stencil only (stencil ForegroundStencil), so they show through her front hair and nothing else.
 bool ForegroundEyes=false,ForegroundEyebrow=false;uint8 ForegroundStencil=0;
 // Per part, from creation: 1 front hair, 2 a foreground eye / brow on its _Fg parent, 0 anything else (parts of
 // different classes never share a material). FrontHairSlots: the mesh material slots of her front hair (and outlines).
 TArray<uint8> FaceClasses;TArray<int32> FrontHairSlots;
 // The head parts on their _Flat parents (ok.FaceFlatten, OKRuntimeFlatFaceParent): per part whether it is, and the
 // instances to give the head and the camera every frame (UpdateFaceFlatten).
 TArray<bool> FlatParts;TArray<TWeakObjectPtr<UMaterialInstanceDynamic>> FlatMaterials;FKKFaceFlattenState FlatState;
 TWeakObjectPtr<UInstancedSkinnedMeshComponent> FrontGpu;
 // Her body without the front hair, drawn only into the foreground occlusion capture (ForegroundCapture): her face,
 // head and other hair hide her eyes / brows as in Koikatsu, where they depth-test before marking the stencil.
 TWeakObjectPtr<UInstancedSkinnedMeshComponent> OccluderGpu;
 FKKRuntimeMaterialData Material;TArray<FKKRuntimePoseBone> Bones;
 TArray<TStrongObjectPtr<UTexture2D>> Textures;
 TArray<TStrongObjectPtr<UMaterialInstanceDynamic>> Materials,OutlineMaterials;
 FBox AnimationBounds=FBox(ForceInit);
 TStrongObjectPtr<USkeletalMesh> Mesh;
 TStrongObjectPtr<USkeletalMesh> DepthMesh;
 TWeakObjectPtr<UPoseableMeshComponent> Cpu;
 TWeakObjectPtr<UInstancedSkinnedMeshComponent> Gpu;
 TWeakObjectPtr<UPoseableMeshComponent> DepthCpu;
 TWeakObjectPtr<UPoseableMeshComponent> LoadingMain;
 double VisibleMs=0;
 bool RevealStarted=false;double RevealWorldStart=0,RevealStartMs=0;
 TWeakObjectPtr<UInstancedSkinnedMeshComponent> DepthGpu;
 TFuture<FOKRuntimeShadowPrepared> ShadowJob;bool ShadowResolved=false,ShadowFaceDirty=false;
 TStrongObjectPtr<USkeletalMesh> ShadowMesh;TArray<TStrongObjectPtr<UMaterialInstanceDynamic>> ShadowProxyMaterials;
 TSharedPtr<const FKKRuntimeMouthStream,ESPMode::ThreadSafe> ShadowMouth;TWeakObjectPtr<UInstancedSkinnedMeshComponent> ShadowGpu;
 int32 ShadowSourceSections=0,ShadowProxySections=0,ShadowExcludedSections=0;TArray<FString> ShadowReasons;FString ShadowFallback;
 TSharedPtr<const FOKValidatedGpuBank,ESPMode::ThreadSafe> Bank;TFuture<FPrivatePackedBank> PackedBank;
 TSharedPtr<FRenderState,ESPMode::ThreadSafe> LoadingRender;bool LoadingGpuInstalled=false;double PrivateGpuVisibleMs=0;int32 PrivateGpuVisibleComplete=0;
 TSharedPtr<const TArray<FVector4f>,ESPMode::ThreadSafe> CpuKeys;
 FOKRuntimeGpuPlayback Clock;FOKCrowdBehavior Behavior;
 FTransform Transform=FTransform::Identity,ExternalTransform=FTransform::Identity;
 float ExternalSpeed=0,VoicePitch=1;
 uint64 Triangles=0;int64 LastCpuBucket=-1;int32 LastClip=1;
 EOKRuntimeSpeechState SpeechState=EOKRuntimeSpeechState::Idle;FString SpeechError;
 void CancelSpeech(){
  check(IsInGameThread());if(SpeechJob.IsValid()||SpeechWave.IsValid()){++SpeechCancellations;SpeechState=EOKRuntimeSpeechState::Cancelled;}
  // Decode workers own plain inputs/results. Discarding this future cannot
  // publish stale audio into the scene, and never waits on a worker.
  SpeechJob=TFuture<FKKRuntimeSpeechResult>();SpeechEpoch=0;LinkedActionEpoch=0;SpeechPaired=false;
  if(auto* Audio=SpeechAudio.Get()){Audio->Stop();Audio->SetSound(nullptr);}
  if(SpeechWave.IsValid()){const uint64 Frames=SpeechWave->Meter->Frames.Load();SpeechFrames+=Frames>=LastSpeechFrames?Frames-LastSpeechFrames:0;}
  SpeechWave.Reset();LastSpeechFrames=0;SpeechMouth=0;SpeechRequestedAt=0;PendingSpeechAction.Reset();MouthEnvelope.Reset();
 }
};
// Writes E.LookNow into her face materials; eases LookNow toward LookGoal (defined beside OKSetCrowdRuntimeFaceLook).
int32 ApplyFaceLook(FEntry& E,FString* Trace=nullptr);
void TickFaceLook(FEntry& E,float Dt);
FTransform SampleLocal(const FKKRuntimePoseResult& R,TConstArrayView<FVector4f> Keys,int32 Bone,float Time){
 const auto& T=R.Tracks[Bone];uint32 Lo=0,Hi=T.Y-1;
 while(Hi-Lo>1){const uint32 Mid=(Hi+Lo)/2;if(Keys[OKGpuTrackPositionRow(T,Mid)].W<=Time)Lo=Mid;else Hi=Mid;}
 const auto& A=Keys[OKGpuTrackPositionRow(T,Lo)];const auto& B=Keys[OKGpuTrackPositionRow(T,Hi)];const float W=B.W>A.W?FMath::Clamp((Time-A.W)/(B.W-A.W),0.f,1.f):0;
 auto V=[](const FVector4f& X){return FVector(X.X,X.Y,X.Z);};
 const auto Position=FMath::Lerp(V(A),V(B),W);
 if(OKGpuTrackColumn(T))return OKGpuColumnTransform<FVector,FQuat,FTransform>(FVector(OKGpuColumnLerp(FVector3f(A),FVector3f(B),W)),FVector(OKGpuColumnLerp(OKGpuTrackColumnKey(Keys,T,Lo),OKGpuTrackColumnKey(Keys,T,Hi),W)),(T.W>>6)&3);
 const auto& QA=Keys[OKGpuTrackRotationRow(T,Lo)];const auto& QB=Keys[OKGpuTrackRotationRow(T,Hi)];const FQuat RA(QA.X,QA.Y,QA.Z,QA.W),RB(QB.X,QB.Y,QB.Z,QB.W);
 return FTransform(((T.W&1)?FQuat::FastLerp(RA,RB,W):FQuat::Slerp(RA,RB,W)).GetNormalized(),Position,FMath::Lerp(V(Keys[OKGpuTrackScaleRow(T,Lo)]),V(Keys[OKGpuTrackScaleRow(T,Hi)]),W));
}
// The component-space transform of a bone in her clip pose (no head look).
FTransform ClipComponent(const FEntry& E,const FOKGpuPosePlayback& P,int32 Bone){
 FTransform R=FTransform::Identity;int32 Steps=0;
 for(int32 B=Bone;B!=INDEX_NONE&&E.Bones.IsValidIndex(B)&&++Steps<=E.Bones.Num();B=E.Bones[B].Parent){
  auto L=SampleLocal(E.Clips[P.ClipA],*E.CpuKeys,B,P.Times.X);if(P.ClipA!=P.ClipB)L=OKGpuBlendLocal(L,SampleLocal(E.Clips[P.ClipB],*E.CpuKeys,B,P.Times.Y),P.Times.Z,E.Clips[P.ClipA].Tracks[B]);R=R*L;}
 return R;
}
// Her head turned to E.LookAt over the clip pose: the face direction is measured in the clip's head and turned toward the
// point, within 60 degrees left / right and 25 down / 30 up of her body, two fifths by the neck and the rest by the head
// (a turn in the world, written as a turn in each bone's parent frame). Most of the clip's own head motion is replaced
// while she looks (a fifth stays, so she is not frozen); the turn eases in and out over half a second.
static TAutoConsoleVariable<int32> CVarOKHeadLook(TEXT("ok.HeadLook"),1,TEXT("1: a girl turns her head to whom she talks to (OKSetCrowdRuntimeHeadLook); 0: her head follows the clip."));
void ApplyHeadLook(FEntry& E,FOKGpuPosePlayback& P,float Dt){
 E.LookWeight=FMath::FInterpConstantTo(E.LookWeight,E.Looking&&CVarOKHeadLook.GetValueOnGameThread()?1.f:0.f,Dt,2.f);E.Turned=false;
 if(E.LookWeight<=0||!E.CpuKeys.IsValid()||!E.Clips.IsValidIndex(P.ClipA)||!E.Clips.IsValidIndex(P.ClipB))return;
 if(E.LookNeck==-2){
  E.LookNeck=E.Bones.IndexOfByPredicate([](const auto& B){return B.Name==FName(TEXT("cf_j_neck"))||B.Name==FName(TEXT("cm_j_neck"));});
  E.LookHead=E.Bones.IndexOfByPredicate([](const auto& B){return B.Name==FName(TEXT("cf_j_head"))||B.Name==FName(TEXT("cm_j_head"));});
  bool Under=false;for(int32 B=E.LookHead,Steps=0;B!=INDEX_NONE&&E.Bones.IsValidIndex(B)&&++Steps<=E.Bones.Num();B=E.Bones[B].Parent)Under|=B==E.LookNeck&&B!=E.LookHead;
  if(!Under||OKGpuTrackColumn(E.Clips[0].Tracks[E.LookNeck])||OKGpuTrackColumn(E.Clips[0].Tracks[E.LookHead]))E.LookNeck=E.LookHead=INDEX_NONE;
  else{FQuat Ref=FQuat::Identity;for(int32 B=E.LookHead,Steps=0;B!=INDEX_NONE&&++Steps<=E.Bones.Num();B=E.Bones[B].Parent)Ref=E.Bones[B].Reference.GetRotation()*Ref;E.HeadFace=Ref.Inverse().RotateVector(FVector(0,1,0)).GetSafeNormal();}
  UE_LOG(LogTemp,Display,TEXT("Runtime head look %s: neck=%d head=%d face=(%.2f,%.2f,%.2f)"),*E.Id,E.LookNeck,E.LookHead,E.HeadFace.X,E.HeadFace.Y,E.HeadFace.Z);
 }
 if(E.LookNeck==INDEX_NONE||E.LookHead==INDEX_NONE)return;
 const FTransform Head=ClipComponent(E,P,E.LookHead);
 const FQuat NeckParent=ClipComponent(E,P,E.Bones[E.LookNeck].Parent).GetRotation(),HeadParent=ClipComponent(E,P,E.Bones[E.LookHead].Parent).GetRotation();
 const FVector Face=Head.GetRotation().RotateVector(E.HeadFace).GetSafeNormal(),To=(E.Transform.InverseTransformPosition(E.LookAt)-Head.GetTranslation()).GetSafeNormal();
 if(Face.IsNearlyZero()||To.IsNearlyZero())return;
 // The body faces +Y in her component, up is +Z.
 const float Yaw=FMath::Clamp(FMath::RadiansToDegrees(FMath::Atan2(To.X,To.Y)),-60.f,60.f),Pitch=FMath::Clamp(FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(To.Z,-1.f,1.f))),-25.f,30.f);
 const float Cp=FMath::Cos(FMath::DegreesToRadians(Pitch));
 const FVector Want(FMath::Sin(FMath::DegreesToRadians(Yaw))*Cp,FMath::Cos(FMath::DegreesToRadians(Yaw))*Cp,FMath::Sin(FMath::DegreesToRadians(Pitch)));
 const float W=FMath::SmoothStep(0.f,1.f,E.LookWeight)*.8f;
 const FQuat Turn=FQuat::Slerp(FQuat::Identity,FQuat::FindBetweenNormals(Face,Want),W),Neck=FQuat::Slerp(FQuat::Identity,Turn,.4f),HeadTurn=Turn*Neck.Inverse();
 const FQuat HeadParentNow=Neck*HeadParent;
 E.NeckTurn=(NeckParent.Inverse()*Neck*NeckParent).GetNormalized();E.HeadTurn=(HeadParentNow.Inverse()*HeadTurn*HeadParentNow).GetNormalized();E.Turned=true;
 P.LookNeck=uint32(E.LookNeck);P.LookHead=uint32(E.LookHead);
 P.NeckRotation=FVector4f(E.NeckTurn.X,E.NeckTurn.Y,E.NeckTurn.Z,E.NeckTurn.W);P.HeadRotation=FVector4f(E.HeadTurn.X,E.HeadTurn.Y,E.HeadTurn.Z,E.HeadTurn.W);
}
// The look's turn of this bone's local transform (identity for the others or while she does not look).
void TurnLocal(const FEntry& E,int32 Bone,FTransform& Local){
 if(!E.Turned)return;if(Bone==E.LookNeck)Local.SetRotation(E.NeckTurn*Local.GetRotation());else if(Bone==E.LookHead)Local.SetRotation(E.HeadTurn*Local.GetRotation());
}
struct FScene : TSharedFromThis<FScene> {
 FString DisplayLUTPath;TStrongObjectPtr<UTexture2D> DisplayLUT;
 TWeakObjectPtr<AActor> Owner;TWeakObjectPtr<ACameraActor> Camera;TSharedPtr<FLateSourceDepth,ESPMode::ThreadSafe> LateDepthExtension;
 TArray<TUniquePtr<FEntry>> Entries;TArray<TStrongObjectPtr<UMaterialInterface>> Parents,OutlineParents;
 // The heroine on the phone with him (OKSetCrowdRuntimeSpeechRemote): her voice is in his ear, not where she stands.
 int32 RemoteSpeech=INDEX_NONE;
 // The lit tear parent (M_OKTearLit, Tools/prepare_face_materials.lua) when the material root has it: tears take the
 // engine's specular. Without it they keep their source glasses program.
 TStrongObjectPtr<UMaterialInterface> TearParent;
 // The foreground variants (<Parent>_Fg, Tools/prepare_face_materials.lua) by recipe, empty where the root lacks one.
 TArray<TStrongObjectPtr<UMaterialInterface>> ForegroundParents;
 // The depth of every foreground heroine's OccluderGpu seen from the player camera, sampled by the _Fg parents
 // (__OK_ForegroundOcclusion): an eye / brow shows through her front hair only where nothing else of hers is in front.
 TWeakObjectPtr<USceneCaptureComponent2D> ForegroundCapture;TStrongObjectPtr<UTextureRenderTarget2D> ForegroundDepth;
 // Shutdown when the world is cleaned up (a map load, a new game), not one ticker tick later: nothing held here may
 // keep the old world alive (UEngine::LoadMap stops the game on world leaks).
 FDelegateHandle WorldCleanup;
 TArray<TArray<uint8>> Curves;TArray<FString> ClipPaths;TArray<FString> ClipIds;
 // Clip id -> source, one lookup instead of a scan of hundreds of ids (status queries run many times a frame).
 TMap<FString,int32> ClipIndex;int32 FindClip(const FString& Id) const{const int32* I=ClipIndex.Find(Id);return I?*I:INDEX_NONE;}
 // An action's curves, read from disk the first time it is prepared (hundreds of admitted actions: the base curves only
 // are read at start). Empty when the file cannot be read or is not a valid clip.
 const TArray<uint8>& ClipBytes(int32 Source){
  check(IsInGameThread());static const TArray<uint8> None;if(!Curves.IsValidIndex(Source))return None;
  if(Curves[Source].IsEmpty()&&ClipPaths.IsValidIndex(Source)){
   if(!FFileHelper::LoadFileToArray(Curves[Source],*ClipPaths[Source])||OKRuntimeSharedClipDuration(Curves[Source])<=0){
    UE_LOG(LogTemp,Warning,TEXT("Runtime action %s could not be read"),*ClipPaths[Source]);Curves[Source].Reset();}}
  return Curves[Source];
 }
 // Whether an action's curves are in memory; the first time, the file is read on a worker (read on the game thread it
 // held the frame 7-12 ms when an action was first streamed) and this answers false until it has arrived.
 // The same bytes, shared with the workers that build a streamed action's request (never changed once read).
 TArray<TSharedPtr<const TArray<uint8>,ESPMode::ThreadSafe>> CurvesShared;
 TSharedPtr<const TArray<uint8>,ESPMode::ThreadSafe> SharedCurves(int32 Source){
  if(CurvesShared.Num()<Curves.Num())CurvesShared.SetNum(Curves.Num());
  if(!CurvesShared[Source])CurvesShared[Source]=MakeShared<const TArray<uint8>,ESPMode::ThreadSafe>(Curves[Source]); // loaded at start: once
  return CurvesShared[Source];
 }
 TMap<int32,TFuture<TPair<TArray<uint8>,TSharedPtr<const TArray<uint8>,ESPMode::ThreadSafe>>>> ClipReads;
 bool ClipBytesReady(int32 Source){
  if(!Curves.IsValidIndex(Source)||!Curves[Source].IsEmpty()||!ClipPaths.IsValidIndex(Source))return true;
  if(auto* Read=ClipReads.Find(Source)){if(!Read->IsReady())return false;auto Both=Read->Consume();Curves[Source]=MoveTemp(Both.Key);if(CurvesShared.Num()<Curves.Num())CurvesShared.SetNum(Curves.Num());CurvesShared[Source]=MoveTemp(Both.Value);ClipReads.Remove(Source);
   if(OKRuntimeSharedClipDuration(Curves[Source])<=0){UE_LOG(LogTemp,Warning,TEXT("Runtime action %s could not be read"),*ClipPaths[Source]);Curves[Source].Reset();}
   return true;}
  ClipReads.Add(Source,Async(EAsyncExecution::ThreadPool,[Path=ClipPaths[Source]]{TArray<uint8> Bytes;FFileHelper::LoadFileToArray(Bytes,*Path);auto Shared=MakeShared<const TArray<uint8>,ESPMode::ThreadSafe>(Bytes);return TPair<TArray<uint8>,TSharedPtr<const TArray<uint8>,ESPMode::ThreadSafe>>(MoveTemp(Bytes),MoveTemp(Shared));}));
  return false;
 }TArray<bool> ClipLoops;TArray<float> ClipWindows;TArray<FString> VoicePaths,VoiceIds;int32 WalkSources=0;TArray<int32> IdleVariantSources;int32 IdleVariantsPerCharacter=4;bool DemoSpeech=false,DemoSpeechStarted=false,DemoActionStarted=false;FString DemoAction;FString ReportPath,VisualPrefix,VisualError;
 int32 VisualClip=1,NpcActionPreload=0;double DemoDelay=1;double ReadyAt=0,LoadMs=0,ProfileSeconds=0,LastWall=0;bool Captured=false;int32 CaptureStage=0;
 struct FActionStreamSlot {int32 Actor=INDEX_NONE,Source=INDEX_NONE;uint64 Epoch=0;TFuture<FStreamPrepared> Job;TUniquePtr<FStreamPrepared> Prepared;TSharedPtr<FStreamInstall,ESPMode::ThreadSafe> Install;};
 FActionStreamSlot ActionSlots[OKCrowdRuntimeActionSlots];uint64 StaleActionDrops=0;
 int32 DemoCharacter=0;bool StreamTest=false,AutoActionStreaming=true;
 float VisualPhase=.37,VisualBlend=1,VisualDistance=350,VisualFocusHeight=90,VisualMouth=-1;
 double DemoCancelAfter=-1;bool DemoSpeechCancelled=false,AutoBlink=false;float VisualBlink=-1;
 struct FTiming {TArray<double> Frame,Game,Render,Rhi,Gpu;};FTiming Timings[2];
 TSharedRef<FRenderState,ESPMode::ThreadSafe> Render=MakeShared<FRenderState,ESPMode::ThreadSafe>();
 TFuture<TSharedPtr<const FOKValidatedGpuBank,ESPMode::ThreadSafe>> Composition;TSharedPtr<const FOKValidatedGpuBank,ESPMode::ThreadSafe> PendingComposition;
 double Started=FPlatformTime::Seconds(),SceneTime=0,LoadDiagnosticDeadline=60;int32 Next=0,Active=0,Complete=0,Concurrency=FMath::Clamp(FTaskGraphInterface::Get().GetNumWorkerThreads(),1,16);
 bool Suspended=false;
 bool Installed=false,GpuMode=false,RequestedGpu=true,ReportWritten=false,Stopped=false,Cleaned=false,ExitAfterReport=false;
 float CameraYaw=0,CameraPitch=-8;
 FVector PlayerPosition=FVector::ZeroVector;bool PlayerPositionInitialized=false;double NextStatusTime=0;
 bool DepthPrepassEnabled=true,PreserveSourceGeometry=false,EditorCommonMaterials=false,ExternalControl=false,CaptureFacePatterns=false;
 // HG render style (ok.RenderStyle 1 with a manifest hg_material_root): the HG parents, bound without the Koikatsu
 // self-shadow they relight (Plugins/OKHGShading, Docs/HG_RENDER_STYLE.md).
 bool HGStyle=false;
 FKKRuntimeMaterialScene MaterialScene() const{FKKRuntimeMaterialScene Scene;Scene.EnableShadows=!HGStyle;return Scene;}
 // The game's ramp render styles (ok.RenderRamp, OKHGShading): _RampG of every material, switched live. Each
 // material's own (card) ramp is remembered so "" puts it back.
 FString RampKey;TMap<TWeakObjectPtr<UMaterialInstanceDynamic>,TWeakObjectPtr<UTexture>> CardRamps;
 void ApplyRamp(UMaterialInstanceDynamic* M){
  UTexture* Current=nullptr;if(!M||!M->GetTextureParameterValue(FMaterialParameterInfo(TEXT("_RampG")),Current))return;
  if(!CardRamps.Contains(M))CardRamps.Add(M,Current);
  UTexture* Want=CardRamps[M].Get();if(!RampKey.IsEmpty()){FString Error;if(UTexture2D* Ramp=OKLoadRenderRamp(RampKey,Error))Want=Ramp;else UE_LOG(LogTemp,Warning,TEXT("%s"),*Error);}
  if(Want&&Want!=Current)M->SetTextureParameterValue(TEXT("_RampG"),Want);
 }
 // A studio key light for every material (not only the view-lit ones), HG included (its view key turned off), so
 // front / side / back light compare the same on all families.
 void SetStudioLight(const FVector& Light){
  const FVector D=Light.GetSafeNormal();if(D.IsNearlyZero())return;const FLinearColor Unity(-D.X,D.Z,D.Y,0);int32 Count=0;
  for(auto& Entry:Entries){auto& E=*Entry;E.DisplayMaterialCount=-1;
   for(auto* List:{&E.Materials,&E.OutlineMaterials})for(auto& M:*List)if(auto* I=M.Get()){I->SetVectorParameterValue(TEXT("_WorldSpaceLightPos0"),Unity);I->SetScalarParameterValue(TEXT("__OK_HG_ViewKey"),0);++Count;}}
  UE_LOG(LogTemp,Display,TEXT("Runtime studio light=(%.2f,%.2f,%.2f) materials=%d"),D.X,D.Y,D.Z,Count);
 }
 void SetRamp(const FString& Key){
  RampKey=Key;int32 Count=0;
  for(auto& Entry:Entries){for(auto& M:Entry->Materials){ApplyRamp(M.Get());++Count;}for(auto& M:Entry->OutlineMaterials){ApplyRamp(M.Get());++Count;}}
  UE_LOG(LogTemp,Display,TEXT("Runtime render ramp=%s materials=%d"),Key.IsEmpty()?TEXT("(card)"):*Key,Count);
 }
 // -CrowdViewLightAll: every character material (hair, opaque clothes, items) takes the
 // camera view as its light direction, not only clothes and skin (kinds 3, 5, 7).
 bool ViewLightAll=FParse::Param(FCommandLine::Get(),TEXT("CrowdViewLightAll"));
 // -CrowdLowPolyRules: room reduction keeps face, body and socks; hair 30000,
 // clothes plus accessories 30000 (same classes as the low-poly files).
 bool LowPolyRules=FParse::Param(FCommandLine::Get(),TEXT("CrowdLowPolyRules"));
 FString VoiceRoot;
 // Optional second root for voices generated at runtime (StreetSocial TTS cache).
 FString GeneratedVoiceRoot;
 // The face pressed flat along the view (ok.FaceFlatten): every head instance gets her head and the player camera.
 void UpdateFaceFlatten(){
  const APlayerController* PC=Owner.IsValid()&&Owner->GetWorld()?Owner->GetWorld()->GetFirstPlayerController():nullptr;
  if(!PC||!PC->PlayerCameraManager)return;const FVector View=PC->PlayerCameraManager->GetCameraLocation();
  for(int32 C=0;C<Entries.Num();++C){auto& E=*Entries[C];if(E.FlatMaterials.IsEmpty()||E.Stage!=5)continue;
   // Past 25 m a face is a few pixels: no bone sampled, nothing set (it keeps the plane it had).
   if(FVector::DistSquared((ExternalControl?E.ExternalTransform:E.Transform).GetLocation(),View)>2500.*2500.)continue;FTransform Head;
   if(!OKGetCrowdRuntimeBoneTransform(Owner.Get(),C,TEXT("cf_j_head"),Head)&&!OKGetCrowdRuntimeBoneTransform(Owner.Get(),C,TEXT("cf_s_head"),Head))continue;
   if(!OKFaceFlattenChanged(E.FlatState,Head.GetLocation(),View))continue;
   for(const auto& M:E.FlatMaterials)if(M.IsValid())OKApplyFaceFlatten(M.Get(),E.FlatState);}
 }
 void UpdateDisplayLighting(const FVector& Light){
  if(Light.ContainsNaN()||Light.IsNearlyZero())return;
  const FVector Direction=Light.GetSafeNormal();const FLinearColor Unity(-Direction.X,Direction.Z,Direction.Y,0);
  for(auto& Entry:Entries){auto& E=*Entry;const int32 Count=E.Materials.Num()+E.OutlineMaterials.Num();if(E.DisplayLight.Equals(Direction,.00001f)&&E.DisplayMaterialCount==Count)continue;
   TSet<UMaterialInstanceDynamic*> Changed;
   for(int32 I=0;I<E.Materials.Num()&&I<E.ViewLitMaterials.Num();++I){if(!E.ViewLitMaterials[I])continue;auto* M=E.Materials[I].Get();if(M&&!Changed.Contains(M)){M->SetVectorParameterValue(TEXT("_WorldSpaceLightPos0"),Unity);Changed.Add(M);}}
   // Outline lighting belongs to the same view; materials without this uniform ignore it.
   for(int32 I=0;I<E.OutlineMaterials.Num();++I)if(E.ViewLitOutlines.IsValidIndex(I)&&E.ViewLitOutlines[I]&&E.OutlineMaterials[I].IsValid())E.OutlineMaterials[I]->SetVectorParameterValue(TEXT("_WorldSpaceLightPos0"),Unity);
   E.DisplayLight=Direction;E.DisplayMaterialCount=Count;
  }
 }
 FString SourceId(int32 Source)const{return Source==INDEX_NONE?TEXT("__runtime_reserved_action__"):ClipIds[Source];}
 bool SourceLoop(int32 Source)const{return Source==INDEX_NONE||ClipLoops[Source];}
 bool ProgressiveLoading=true,ParallelLoadingPoses=true,LoadingCpuPreview=false;
 // Compose every private bank into one aggregate bank once all are loaded (manifest aggregate_gpu, default on). Off: each
 // character keeps drawing from her own private bank (no second copy of every bank while composing: the hundred-girl room).
 bool AggregateGpu=true;
 // Pose on the GPU from shared clips (FOKGpuRig) instead of per-clip banks (manifest gpu_rig; needs the aggregate bank).
 bool GpuRig=false;int32 GpuRigVerify=0;
 double LoadingPoseEvalMs=0,LoadingPoseRefreshMs=0;uint64 LoadingPoseJobs=0,LoadingBoneSamples=0;
 double FirstVisibleMs=0;TArray<int32> LaunchOrder;
 bool LoadingCaptured=false;FTransform LoadingCaptureTransform;
 void RevealOverride(FEntry& E,float Value){TSet<UMaterialInstanceDynamic*> Seen;auto Apply=[&](UMaterialInstanceDynamic* M){if(M&&!Seen.Contains(M)&&!E.Tears.Contains(M)){Seen.Add(M);M->SetScalarParameterValue(TEXT("__OK_RuntimeRevealOverride"),Value);}};for(const auto& M:E.Materials)Apply(M.Get());for(const auto& M:E.OutlineMaterials)Apply(M.Get());for(const auto& M:E.ShadowProxyMaterials)Apply(M.Get());}
 void BeginReveal(FEntry& E){
  if(E.RevealStarted)return;E.RevealStarted=true;E.RevealWorldStart=Owner->GetWorld()->GetTimeSeconds();E.RevealStartMs=(FPlatformTime::Seconds()-Started)*1000;
  TSet<UMaterialInstanceDynamic*> Seen;auto Apply=[&](UMaterialInstanceDynamic* M){if(M&&!Seen.Contains(M)){Seen.Add(M);M->SetScalarParameterValue(TEXT("__OK_RuntimeRevealStart"),E.RevealWorldStart);M->SetScalarParameterValue(TEXT("__OK_RuntimeRevealDuration"),3);}};for(const auto& M:E.Materials)Apply(M.Get());for(const auto& M:E.OutlineMaterials)Apply(M.Get());for(const auto& M:E.ShadowProxyMaterials)Apply(M.Get());
 }
 void CaptureLoading(){
  if(LoadingCaptured||VisualPrefix.IsEmpty()||Installed||Entries[0]->Stage!=5)return;
  auto& E=*Entries[0];auto* Main=E.LoadingMain.IsValid()?E.LoadingMain.Get():E.Cpu.Get();
  for(int32 B=0;B<E.Bones.Num();++B){const int32 Slot=FMath::Min(VisualClip,E.Clips.Num()-1);const auto A=SampleLocal(E.Clips[0],*E.CpuKeys,B,VisualPhase*E.Clips[0].Duration),Other=SampleLocal(E.Clips[Slot],*E.CpuKeys,B,VisualPhase*E.Clips[Slot].Duration);E.Cpu->BoneSpaceTransforms[B]=OKGpuBlendLocal(A,Other,VisualBlend,E.Clips[0].Tracks[B]);}
  E.Cpu->MarkRefreshTransformDirty();E.Cpu->RefreshBoneTransforms();E.Cpu->RefreshFollowerComponents();E.LastCpuBucket=-1;
  LoadingCaptureTransform=E.Transform;const bool Shadow=Main->CastShadow;Main->SetCastShadow(false);
  RevealOverride(E,1); // Isolate full appearance in diagnostic captures only.
  auto* World=Owner->GetWorld();World->SendAllEndOfFrameUpdates();FlushRenderingCommands();
  TStrongObjectPtr<UTextureRenderTarget2D> Target(NewObject<UTextureRenderTarget2D>(World));Target->RenderTargetFormat=RTF_RGBA8;Target->InitAutoFormat(1024,1024);Target->UpdateResourceImmediate(true);
  TStrongObjectPtr<USceneCaptureComponent2D> C(NewObject<USceneCaptureComponent2D>(World));C->bCaptureEveryFrame=false;C->bCaptureOnMovement=false;C->bAlwaysPersistRenderingState=true;C->CaptureSource=SCS_FinalColorLDR;C->TextureTarget=Target.Get();C->FOVAngle=50;C->PostProcessSettings=Camera->GetCameraComponent()->PostProcessSettings;C->PostProcessBlendWeight=1;C->ShowFlags.SetTemporalAA(false);C->ShowFlags.SetMotionBlur(false);C->RegisterComponentWithWorld(World);
  for(auto& Entry:Entries){if(auto* Other=Entry->Cpu.Get())if(Other!=E.Cpu.Get()||!E.DepthMesh.IsValid())if(Other!=Main)C->HideComponent(Other);if(auto* Other=Entry->LoadingMain.Get())if(Other!=Main)C->HideComponent(Other);if(auto* Other=Entry->Gpu.Get())C->HideComponent(Other);if(auto* Other=Entry->DepthGpu.Get())C->HideComponent(Other);if(auto* Other=Entry->ShadowGpu.Get())C->HideComponent(Other);}
  const TCHAR* Views[]={TEXT("front"),TEXT("side"),TEXT("back")};bool Good=true;const FVector Focus=E.Transform.GetTranslation()+FVector(0,0,VisualFocusHeight);
  for(int32 V=0;V<3&&Good;++V){const FVector Offset=E.Transform.GetRotation().RotateVector(FRotator(0,V*90,0).RotateVector(FVector(0,VisualDistance,0)));C->SetWorldLocationAndRotation(Focus+Offset,(-Offset).Rotation());UpdateDisplayLighting(Offset);C->CaptureScene();FlushRenderingCommands();TArray<FColor> Pixels;FReadSurfaceDataFlags Flags(RCM_UNorm);Flags.SetLinearToGamma(false);Good=Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels,Flags)&&Pixels.Num()==1024*1024;if(Good)Good=FImageUtils::SaveImageByExtension(*(VisualPrefix+TEXT(".")+Views[V]+TEXT(".loading.png")),FImageView(Pixels.GetData(),1024,1024));}
  if(Good){const FVector Offset=E.Transform.GetRotation().RotateVector(FVector(0,VisualDistance,0));C->SetWorldLocationAndRotation(Focus+Offset,(-Offset).Rotation());UpdateDisplayLighting(Offset);for(int32 Percent:{0,50,100}){RevealOverride(E,Percent*.01f);C->CaptureScene();FlushRenderingCommands();TArray<FColor> Pixels;FReadSurfaceDataFlags Flags(RCM_UNorm);Flags.SetLinearToGamma(false);Good=Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels,Flags)&&Pixels.Num()==1024*1024;if(Good)Good=FImageUtils::SaveImageByExtension(*(VisualPrefix+FString::Printf(TEXT(".reveal%d.png"),Percent)),FImageView(Pixels.GetData(),1024,1024));if(!Good)break;}}
  C->DestroyComponent();RevealOverride(E,-1);Main->SetCastShadow(Shadow);LoadingCaptured=Good;if(!Good)VisualError=TEXT("Progressive loading capture failed");
 }
 FVector SpawnPosition(int32 A)const{
  if(ExternalControl&&Entries.IsValidIndex(A))return Entries[A]->ExternalTransform.GetTranslation();
  const int32 Columns=FMath::CeilToInt(FMath::Sqrt(float(Entries.Num()))),Rows=FMath::DivideAndRoundUp(Entries.Num(),Columns);
  const double Phase=A*.47;
  return FVector((A%Columns-(Columns-1)*.5)*210+FMath::Sin(Phase)*75,(A/Columns-(Rows-1)*.5)*210+FMath::Cos(Phase)*75,0);
 }
 int32 NearestStage(int32 Stage)const{
  int32 Best=INDEX_NONE;double Distance=TNumericLimits<double>::Max();
  for(int32 A=0;A<Entries.Num();++A)if(Entries[A]->Stage==Stage){if(A==0)return A;const double D=FVector::DistSquared(SpawnPosition(A),PlayerPosition);if(D<Distance){Distance=D;Best=A;}}
  return Best;
 }
 void ShowLoadingCharacter(FEntry& E){
  if(!ProgressiveLoading||(!LoadingCpuPreview&&RequestedGpu&&E.SelectionIndex!=0))return;
  BeginReveal(E);
  auto* O=Owner.Get();auto* Main=E.Cpu.Get();
  if(E.DepthMesh.IsValid()){
   Main=NewObject<UPoseableMeshComponent>(O);O->AddInstanceComponent(Main);Main->SetSkinnedAssetAndUpdate(E.Mesh.Get());Main->SetLeaderPoseComponent(E.Cpu.Get(),true,false);Main->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;Main->SetCollisionEnabled(ECollisionEnabled::NoCollision);Main->SetCastShadow(false);Main->SetVisibility(false);Main->RegisterComponent();Main->SetComponentTickEnabled(false);E.LoadingMain=Main;
   if(!LateDepthExtension.IsValid()){LateDepthExtension=FSceneViewExtensions::NewExtension<FLateSourceDepth>(Render,O->GetWorld()->Scene);ENQUEUE_RENDER_COMMAND(RegisterProgressiveComposite)([Extension=LateDepthExtension](FRHICommandListImmediate&){Extension->Register();});}
   TArray<const FMaterialRenderProxy*> SourceMaterials;for(const auto& M:E.Materials)if(M.IsValid()&&M->GetMaterial()->GetName()==TEXT("M_IBLAlpha"))SourceMaterials.Add(M->GetRenderProxy());
   ENQUEUE_RENDER_COMMAND(AddProgressiveSource)([R=Render,Id=E.Cpu->GetPrimitiveSceneId(),SourceMaterials=MoveTemp(SourceMaterials)](FRHICommandListImmediate&){R->LateDepthProxies.Add(Id);R->CpuSourceProxies.Add(Id);for(const auto* M:SourceMaterials)R->SourceIblMaterials.Add(M);});
  }else {Main->SetRenderInMainPass(true);Main->SetRenderInDepthPass(true);}
  // Remain hidden until Tick has evaluated this actor's own first pose.
  E.VisibleMs=(FPlatformTime::Seconds()-Started)*1000;if(!FirstVisibleMs)FirstVisibleMs=E.VisibleMs;
  UE_LOG(LogTemp,Display,TEXT("APNG progressive ready: selection=%d elapsed_ms=%.3f complete=%d/%d"),E.SelectionIndex,E.VisibleMs,Complete,Entries.Num());
 }
 bool Capture(){
  if(Entries.IsEmpty()||!Render->Ready.Load())return false;auto& E=*Entries[StreamTest?DemoCharacter:0];auto* Cpu=E.Cpu.Get();auto* Gpu=E.Gpu.Get();auto* O=Owner.Get();if(!Cpu||!Gpu||!O)return false;
  const auto P=E.Clock.Parameters();const float Time=VisualPhase*E.Clips[VisualClip].Duration;
  for(auto& Entry:Entries){if(auto* Reference=Entry->Cpu.Get()){for(int32 B=0;B<Entry->Bones.Num();++B){const int32 Slot=FMath::Min(VisualClip,Entry->Clips.Num()-1);const auto A=SampleLocal(Entry->Clips[0],*Entry->CpuKeys,B,VisualPhase*Entry->Clips[0].Duration),Other=SampleLocal(Entry->Clips[Slot],*Entry->CpuKeys,B,VisualPhase*Entry->Clips[Slot].Duration);Reference->BoneSpaceTransforms[B]=OKGpuBlendLocal(A,Other,VisualBlend,Entry->Clips[0].Tracks[B]);}Reference->MarkRefreshTransformDirty();Reference->RefreshBoneTransforms();Reference->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;if(auto* Depth=Entry->DepthCpu.Get()){Depth->BoneSpaceTransforms=Reference->BoneSpaceTransforms;Depth->MarkRefreshTransformDirty();Depth->RefreshBoneTransforms();Depth->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;}}}
  if(auto* D=E.DepthCpu.Get()){D->BoneSpaceTransforms=Cpu->BoneSpaceTransforms;D->MarkRefreshTransformDirty();D->RefreshBoneTransforms();D->SetVisibility(true);}if(auto* D=E.DepthGpu.Get())D->SetVisibility(true);
  O->GetWorld()->SendAllEndOfFrameUpdates();FlushRenderingCommands();
  TArray<FOKGpuPosePlayback> Frozen;for(auto& Entry:Entries){const int32 Slot=FMath::Min(VisualClip,Entry->Clips.Num()-1);auto F=Entry->Clock.Parameters();F.ClipA=0;F.ClipB=Slot;F.Times=FVector4f(VisualPhase*Entry->Clips[0].Duration,VisualPhase*Entry->Clips[Slot].Duration,VisualBlend,0);Frozen.Add(F);}
  // First freeze and arm CPU playback; the current scene frame may already
  // have evaluated its provider. Capture only after a real frame boundary.
  if(CaptureStage==0){CaptureStage=1;ENQUEUE_RENDER_COMMAND(ArmFrozenCpuCapture)([R=Render,Frozen](FRHICommandListImmediate&){R->Playback=Frozen;R->UseCpu=true;R->Reset=true;R->Enabled=true;R->VerifyCapture=true;});return false;}
  const int32 CaptureMode=CaptureStage==1?0:1;
  ENQUEUE_RENDER_COMMAND(ApngCapturePlayback)([R=Render,Frozen,CaptureMode](FRHICommandListImmediate&){R->Playback=Frozen;R->UseCpu=CaptureMode==0;R->Reset=true;R->Enabled=true;R->VerifyCapture=true;});
  Cpu->SetVisibility(true);Gpu->SetVisibility(true);const bool CpuShadow=Cpu->CastShadow,GpuShadow=Gpu->CastShadow;Cpu->SetCastShadow(false);Gpu->SetCastShadow(false);auto* ShadowCaster=E.ShadowGpu.Get();const bool ShadowCasterWasCasting=ShadowCaster&&ShadowCaster->CastShadow;if(ShadowCaster)ShadowCaster->SetCastShadow(false);
  auto* World=O->GetWorld();TStrongObjectPtr<UTextureRenderTarget2D> Target(NewObject<UTextureRenderTarget2D>(World));Target->RenderTargetFormat=RTF_RGBA8;Target->InitAutoFormat(1024,1024);Target->UpdateResourceImmediate(true);
  TStrongObjectPtr<USceneCaptureComponent2D> C(NewObject<USceneCaptureComponent2D>(World));C->bCaptureEveryFrame=false;C->bCaptureOnMovement=false;C->bAlwaysPersistRenderingState=true;C->CaptureSource=SCS_FinalColorLDR;C->TextureTarget=Target.Get();C->FOVAngle=50;C->PostProcessSettings=Camera->GetCameraComponent()->PostProcessSettings;C->PostProcessBlendWeight=1;C->ShowFlags.SetTemporalAA(false);C->ShowFlags.SetMotionBlur(false);C->RegisterComponentWithWorld(World);
  const TCHAR* Views[]={TEXT("front"),TEXT("side"),TEXT("back")};bool Good=true;const FVector Focus=E.Transform.GetTranslation()+FVector(0,0,VisualFocusHeight);
  for(int32 V=0;V<3&&Good;++V){const FVector Offset=E.Transform.GetRotation().RotateVector(FRotator(0,V*90,0).RotateVector(FVector(0,VisualDistance,0)));C->SetWorldLocationAndRotation(Focus+Offset,(-Offset).Rotation());UpdateDisplayLighting(Offset);
   for(int32 Mode=CaptureMode;Mode==CaptureMode&&Good;++Mode){C->HiddenComponents.Reset();for(auto& Entry:Entries){if(auto* Other=Entry->Cpu.Get())C->HideComponent(Other);if(auto* Other=Entry->Gpu.Get())if(Other!=Gpu)C->HideComponent(Other);if(auto* Other=Entry->DepthCpu.Get())C->HideComponent(Other);if(auto* Other=Entry->DepthGpu.Get())if(!DepthPrepassEnabled||Entry.Get()!=&E)C->HideComponent(Other);if(auto* Other=Entry->ShadowGpu.Get())C->HideComponent(Other);}
    ENQUEUE_RENDER_COMMAND(ApngCapturePoseSource)([R=Render,CpuSource=Mode==0](FRHICommandListImmediate&){R->UseCpu=CpuSource;R->Reset=true;});
    for(int32 Warm=0;Warm<3;++Warm){C->CaptureScene();FlushRenderingCommands();}
    TArray<FColor> Pixels;FReadSurfaceDataFlags Flags(RCM_UNorm);Flags.SetLinearToGamma(false);Good=Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels,Flags)&&Pixels.Num()==1024*1024;
    const FString Path=VisualPrefix+TEXT(".")+Views[V]+(Mode?TEXT(".gpu.png"):TEXT(".cpu.png"));if(Good)Good=FImageUtils::SaveImageByExtension(*Path,FImageView(Pixels.GetData(),1024,1024));
   }
  }
  // Providers evaluate once per scene frame. Switch pose source on the next
  // real frame, with clocks/world poses frozen, rather than comparing two
  // captures that accidentally consumed the same already-evaluated palette.
  if(Good&&CaptureMode==0){C->DestroyComponent();Cpu->SetCastShadow(CpuShadow);Gpu->SetCastShadow(GpuShadow);if(ShadowCaster)ShadowCaster->SetCastShadow(ShadowCasterWasCasting);CaptureStage=2;ENQUEUE_RENDER_COMMAND(NextApngCaptureSource)([R=Render](FRHICommandListImmediate&){R->UseCpu=false;R->Reset=true;});return false;}
  const double CheckDeadline=FPlatformTime::Seconds()+10;while(Render->VerifiedMatrices.Load()==0&&FPlatformTime::Seconds()<CheckDeadline){ENQUEUE_RENDER_COMMAND(ApngPollPaletteComparison)([R=Render](FRHICommandListImmediate&){R->PollComparison();});FlushRenderingCommands();FPlatformProcess::Sleep(.001f);}
  if(Render->VerifiedMatrices.Load()==0||Render->MatrixMismatches.Load()!=0){Good=false;VisualError=TEXT("APNG actual scene skinning matrix comparison failed");}
  CaptureStage=3;C->DestroyComponent();Cpu->SetCastShadow(CpuShadow);Gpu->SetCastShadow(GpuShadow);if(ShadowCaster)ShadowCaster->SetCastShadow(ShadowCasterWasCasting);Cpu->SetVisibility(true);Gpu->SetVisibility(true);ENQUEUE_RENDER_COMMAND(RestoreApngPoseSource)([R=Render,CpuSource=!GpuMode](FRHICommandListImmediate&){R->UseCpu=CpuSource;R->Reset=true;});if(!Good&&VisualError.IsEmpty())VisualError=TEXT("APNG scene comparison capture failed");return Good;
 }
 void Fail(FEntry& E,const FString& Error){E.Error=Error;E.Stage=-1;--Active;UE_LOG(LogTemp,Error,TEXT("APNG %s: %s"),*E.Path,*Error);}
 void Shutdown(){
  if(Cleaned)return;Cleaned=true;Stopped=true;
  FWorldDelegates::OnWorldCleanup.Remove(WorldCleanup);WorldCleanup.Reset();
  if(auto* Cap=ForegroundCapture.Get()){Cap->TextureTarget=nullptr;Cap->ShowOnlyComponents.Reset();Cap->DestroyComponent();}ForegroundCapture.Reset();ForegroundDepth.Reset();
  for(auto& E:Entries){RetireProgressiveGpu(*E);if(auto* Audio=E->SpeechAudio.Get()){Audio->Stop();Audio->DestroyComponent();}E->SpeechWave.Reset();}
  for(auto& Slot:ActionSlots)ClearActionSlot(Slot);
  for(auto& E:Entries){E->PendingEpoch=0;E->InstalledEpoch=0;E->PendingAction=INDEX_NONE;E->StreamHold=false;E->CancelSpeech();}
  // Scratch leases are not inside the pose object. Drop them even if the world
  // is already gone. Provider teardown releases the same slots again; that is idempotent.
  ENQUEUE_RENDER_COMMAND(ReleaseRuntimeActionStreams)([R=Render](FRHICommandListImmediate& Cmd){R->ReleaseStream(Cmd);});
  auto* O=Owner.Get();if(!O||!O->GetWorld()->Scene)return;auto* Renderer=static_cast<::FScene*>(O->GetWorld()->Scene->GetRenderScene());const FGuid Id=Render->ProviderId;
  ENQUEUE_RENDER_COMMAND(RemoveApngProvider)([Renderer,Id,R=Render,Extension=LateDepthExtension](FRHICommandListImmediate& Cmd){if(Extension)Extension->Unregister();if(R->ProviderRegistered){if(auto* P=Renderer->GetExtensionPtr<FSkinningTransformProvider>())P->UnregisterProvider(Id);R->ProviderRegistered=false;}R->Scene=nullptr;R->LateDepthProxies.Reset();R->Pose.Release();R->ReleaseStream(Cmd);R->Palettes.Reset();});
 }
 bool Commit(FEntry& E,FKKRuntimeRenderMeshData& D,FString& Error){QUICK_SCOPE_CYCLE_COUNTER(STAT_OKCrowd_Commit);
  // A merged component does not sort its translucent sections by the Unity
  // render queue. Preserve authored queue order so eye white cannot cover iris.
  auto& Queues=E.CommitQueues;auto& Canonical=E.CommitCanonical;auto& Identities=E.CommitIdentities;
  if(Queues.IsEmpty()){
   for(const auto& P:E.Material.Parts){int32 Queue=P.Blend==TEXT("translucent")?3000:2000;TSharedPtr<FJsonObject> Root;const TSharedPtr<FJsonObject>* Mat=nullptr;double Value=-1;if(FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(P.SourceMetadataJson),Root)&&Root&&Root->TryGetObjectField(TEXT("material"),Mat)&&(*Mat)->TryGetNumberField(TEXT("render_queue"),Value)&&Value>=0)Queue=int32(Value);Queues.Add(Queue);E.ViewLitMaterials.Add(ViewLightAll||P.ShaderKind==3||P.ShaderKind==5||P.ShaderKind==7);}
   D.Sections.StableSort([&](const auto& A,const auto& B){return Queues[A.Material]<Queues[B.Material];});
  }
  TArray<UTexture2D*> Textures;for(auto& T:E.Textures)Textures.Add(T.Get());
  if(E.MaterialCursor<E.Material.Parts.Num()){
   const double Deadline=FPlatformTime::Seconds()+.0005;
   do{const int32 Part=E.MaterialCursor;const auto& P=E.Material.Parts[Part];const auto Recipe=OKRuntimeSharedMaterialRecipe(P);FKKRuntimeMaterialBinding Binding;if(!OKPrepareRuntimeSharedMaterialBinding(P,MaterialScene(),Binding,Error))return false;auto Identity=RuntimeBindingIdentity(P,Binding,Queues[Part]);int32 Match=INDEX_NONE;const int32 TearLevel=OKRuntimeTearLevel(P.Name);
    const int32 Fore=OKForegroundFacePart(P.Name);const bool WantFore=(Fore==2&&E.ForegroundEyebrow)||(Fore==1&&E.ForegroundEyes);
    UMaterialInterface* ForeParent=WantFore&&ForegroundParents.IsValidIndex(int32(Recipe)-1)?ForegroundParents[int32(Recipe)-1].Get():nullptr;
    if(WantFore&&!ForeParent)UE_LOG(LogTemp,Warning,TEXT("Runtime foreground %s: %s has no %s_Fg parent, drawn behind the hair"),*E.Id,*P.Name,*OKRuntimeSharedMaterialParentName(Recipe));
    const uint8 FaceClass=OKFrontHairPart(P)?1:ForeParent?2:0;E.FaceClasses.Add(FaceClass);
    // The head (face, eyes, brows, nose, tears) pressed flat along the view; the body keeps the plain parents.
    UMaterialInterface* Flat=P.Role==TEXT("head")?OKRuntimeFlatFaceParent(ForeParent?ForeParent:Parents[int32(Recipe)-1].Get()):nullptr;E.FlatParts.Add(Flat!=nullptr);
    // OK_RUNTIME_MATERIAL_DUMP=1: each part's recipe, textures and values (diagnosing a part drawn unlike the reader).
    if(!FPlatformMisc::GetEnvironmentVariable(TEXT("OK_RUNTIME_MATERIAL_DUMP")).IsEmpty()){FString T;for(const auto& Slot:P.Textures)T+=FString::Printf(TEXT(" %s=%d"),*Slot.Property,Slot.Texture);FString Vals;for(const auto& KV:P.Values)Vals+=FString::Printf(TEXT(" %s=(%.3g,%.3g,%.3g,%.3g)"),*KV.Key,KV.Value.X,KV.Value.Y,KV.Value.Z,KV.Value.W);
     UE_LOG(LogTemp,Display,TEXT("Runtime material dump %s part=%d name=%s role=%s blend=%s recipe=%s shader=%d textures:%s values:%s meta=%s"),*E.Id,Part,*P.Name,*P.Role,*P.Blend,*OKRuntimeSharedMaterialParentName(Recipe),P.ShaderKind,*T,*Vals,*P.SourceMetadataJson.Left(6000));}
    for(int32 I=0;I<Identities.Num()&&!TearLevel;++I)if(Identity==Identities[I]&&E.FaceClasses[I]==FaceClass&&E.FlatParts[I]==(Flat!=nullptr)){Match=I;break;}UMaterialInstanceDynamic* M=nullptr;if(Match>=0){const int32 ExistingCanonical=Canonical[Match];Canonical.Add(ExistingCanonical);M=E.Materials[Match].Get();}else{Canonical.Add(Part);M=OKCreateRuntimeSharedMaterial(P,Textures,Flat?Flat:ForeParent?ForeParent:Parents[int32(Recipe)-1].Get(),MaterialScene(),Error);if(!M)return false;if(Flat)E.FlatMaterials.Emplace(M);++E.UniqueMaterialBindings;if(ForeParent)M->SetScalarParameterValue(TEXT("__OK_ForegroundStencil"),E.ForegroundStencil);float Face=0;if(HGStyle&&P.Role==TEXT("head")&&M->GetScalarParameterValue(FMaterialParameterInfo(TEXT("__OK_HG_Face")),Face))M->SetScalarParameterValue(TEXT("__OK_HG_Face"),1);ApplyRamp(M);}// A tear drawn lit: its own texture and tiling on the lit parent, hidden until a look shows it (TearVisible).
    if(TearLevel&&TearParent.IsValid()){UMaterialInterface* TearFlat=OKRuntimeFlatFaceParent(TearParent.Get());if(auto* Lit=UMaterialInstanceDynamic::Create(TearFlat?TearFlat:TearParent.Get(),GetTransientPackage())){if(TearFlat)E.FlatMaterials.Emplace(Lit);
     for(const auto& Slot:P.Textures)if(Slot.Property==TEXT("_MainTex")&&Textures.IsValidIndex(Slot.Texture)&&Textures[Slot.Texture])Lit->SetTextureParameterValue(TEXT("TearTex"),Textures[Slot.Texture]);
     if(const FLinearColor* ST=Binding.Vectors.Find(TEXT("_MainTex_ST")))Lit->SetVectorParameterValue(TEXT("TearTex_ST"),*ST);
     M=Lit;}}
    // OK_RUNTIME_HIDE_PART=<name>: that part draws nothing (diagnosis: what shows where it was).
    {const FString Hide=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_RUNTIME_HIDE_PART"));if(!Hide.IsEmpty()&&P.Name==Hide&&TearParent.IsValid())if(auto* None=UMaterialInstanceDynamic::Create(TearParent.Get(),GetTransientPackage())){None->SetScalarParameterValue(TEXT("__OK_RuntimeRevealOverride"),0);M=None;Identity.Append(reinterpret_cast<const uint8*>(&Part),sizeof(Part));Identity.Add(0xEE);}}
    Identities.Add(MoveTemp(Identity));E.Materials.Emplace(M);
    {const bool Skin=Recipe==EKKRuntimeMaterialRecipe::KoikanoSkin||Recipe==EKKRuntimeMaterialRecipe::ForgeSkin;const bool Iris=Recipe==EKKRuntimeMaterialRecipe::ForgeEye||Recipe==EKKRuntimeMaterialRecipe::KoikanoEye;
     E.LookKinds.Add(TearLevel?4:Skin&&P.Role==TEXT("head")?1:Skin?2:Iris?3:Recipe==EKKRuntimeMaterialRecipe::KoikanoBrow?5:0);
     if(TearLevel&&M){E.Tears.Add(M,TearLevel);M->SetScalarParameterValue(TEXT("__OK_RuntimeRevealOverride"),0);}}
    ++E.MaterialCursor;}while(E.MaterialCursor<E.Material.Parts.Num()&&FPlatformTime::Seconds()<Deadline);
   return true; // Pending: yield before mesh creation, including the last material.
  }
  // The mesh stage takes three frames (it stalled the street 60-130 ms in one): 0 the main mesh, 1 the depth mesh,
  // 2 the components, the skin envelope and the bank job.
  TArray<UMaterialInterface*> Materials;for(const auto& M:E.Materials)Materials.Add(M.Get());
  TSet<int32> Ibl;for(int32 I=0;I<E.Material.Parts.Num();++I)if(OKRuntimeSharedMaterialRecipe(E.Material.Parts[I])==EKKRuntimeMaterialRecipe::IBLAlpha)Ibl.Add(Canonical[I]);
  if(E.CommitStep==0){QUICK_SCOPE_CYCLE_COUNTER(STAT_OKCrowd_CommitMainMesh);
  E.OriginalSections=D.Sections.Num();{QUICK_SCOPE_CYCLE_COUNTER(STAT_OKCrowd_Repack);if(!RepackCompatibleSections(D,Canonical,Error))return false;}E.MergedSections=D.Sections.Num();E.MouthStream=OKPrepareRuntimeMouthStream(D);
  E.CommitStep=10;return true;}
  // The draw data (the main pass, the outline shells): plain copies of the vertex data, a frame of their own.
  if(E.CommitStep==10){QUICK_SCOPE_CYCLE_COUNTER(STAT_OKCrowd_CommitVisualData);
  Ibl.Reset();for(int32 I=0;I<E.Material.Parts.Num();++I)if(OKRuntimeSharedMaterialRecipe(E.Material.Parts[I])==EKKRuntimeMaterialRecipe::IBLAlpha)Ibl.Add(Canonical[I]);
  auto MainData=D;if(DepthPrepassEnabled)MainData.Sections.RemoveAll([&](const auto& Section){return Ibl.Contains(Section.Material);});
  auto VisualData=MainData;TArray<UMaterialInterface*> VisualMaterials=Materials;TMap<int32,int32> OutlineSlots;
  for(const auto& Base:MainData.Sections){const auto& P=E.Material.Parts[Base.Material];const auto Recipe=OKRuntimeSharedMaterialRecipe(P);if(!P.Outline||OKRuntimeTearLevel(P.Name)||!OutlineParents.IsValidIndex(int32(Recipe)-1)||!OutlineParents[int32(Recipe)-1].IsValid())continue;
   int32 Slot=INDEX_NONE;if(const auto* Existing=OutlineSlots.Find(Base.Material))Slot=*Existing;else{UMaterialInterface* OutlineFlat=P.Role==TEXT("head")?OKRuntimeFlatFaceParent(OutlineParents[int32(Recipe)-1].Get()):nullptr;auto* M=OKCreateRuntimeSharedMaterial(P,Textures,OutlineFlat?OutlineFlat:OutlineParents[int32(Recipe)-1].Get(),MaterialScene(),Error,true);if(!M)return false;if(OutlineFlat)E.FlatMaterials.Emplace(M);ApplyRamp(M);Slot=VisualMaterials.Add(M);E.OutlineMaterials.Emplace(M);E.ViewLitOutlines.Add(E.ViewLitMaterials[Base.Material]);OutlineSlots.Add(Base.Material,Slot);}
   auto Shell=Base;Shell.Material=Slot;Shell.BaseIndex=VisualData.Indices.Num();
   for(uint32 I=0;I<Base.Triangles*3;I+=3){const uint32 A=MainData.Indices[Base.BaseIndex+I],B=MainData.Indices[Base.BaseIndex+I+1],C=MainData.Indices[Base.BaseIndex+I+2];VisualData.Indices.Append({A,B,C});}
   VisualData.Sections.Add(MoveTemp(Shell));
  }
  VisualData.Materials=VisualMaterials.Num();
  E.FrontHairSlots.Reset();for(int32 I=0;I<E.FaceClasses.Num()&&I<Canonical.Num();++I)if(E.FaceClasses[I]==1)E.FrontHairSlots.AddUnique(Canonical[I]);
  for(const auto& Shell:OutlineSlots)if(E.FaceClasses.IsValidIndex(Shell.Key)&&E.FaceClasses[Shell.Key]==1)E.FrontHairSlots.AddUnique(Shell.Value);
  E.OutlineSlotIds.Reset();for(const auto& Shell:OutlineSlots)E.OutlineSlotIds.AddUnique(Shell.Value);
  UE_LOG(LogTemp,Display,TEXT("Runtime source outlines %s: materials=%d sections=%d"),*E.Id,E.OutlineMaterials.Num(),VisualData.Sections.Num()-MainData.Sections.Num());
  E.CommitMain=MakeUnique<FKKRuntimeRenderMeshData>(MoveTemp(MainData));E.CommitVisual=MakeUnique<FKKRuntimeRenderMeshData>(MoveTemp(VisualData));E.CommitVisualMaterials=MoveTemp(VisualMaterials);
  E.CommitStep=11;return true;}
  if(E.CommitStep==11){QUICK_SCOPE_CYCLE_COUNTER(STAT_OKCrowd_CommitCreateMesh);
  auto& MainData=*E.CommitMain;auto& VisualData=*E.CommitVisual;auto& VisualMaterials=E.CommitVisualMaterials;
  USkeletalMesh* Mesh=nullptr;{QUICK_SCOPE_CYCLE_COUNTER(STAT_OKCrowd_CreateMainMesh);Mesh=OKCreateRuntimeRenderMesh(VisualData,VisualMaterials,Error);}if(!Mesh)return false;E.Mesh.Reset(Mesh);E.Triangles=D.Indices.Num()/3;
  TArray<FOKRuntimeShadowMaterialFacts> ShadowFacts;ShadowFacts.SetNum(E.Material.Parts.Num());const EShaderPlatform ShadowPlatform=GMaxRHIShaderPlatform;
  for(int32 Part=0;Part<E.Material.Parts.Num();++Part){auto& Fact=ShadowFacts[Part];Fact.Recipe=OKRuntimeSharedMaterialRecipe(E.Material.Parts[Part]);auto* Source=E.Materials[Part].Get();if(!Source)continue;Fact.Blend=Source->GetBlendMode();Fact.BlendObserved=true;Fact.TwoSided=Source->IsTwoSided();Fact.TwoSidedObserved=true;Fact.CastShadowAsMasked=Source->GetCastDynamicShadowAsMasked();if(!Source->GetMaterialResource(ShadowPlatform)){Fact.WorldPositionOffset=true;continue;}const FMaterialRelevance Relevance=Source->GetRelevance_Concurrent(ShadowPlatform);Fact.WorldPositionOffset=Relevance.bUsesWorldPositionOffset;Fact.PixelDepthOffset=Relevance.bUsesPixelDepthOffset;Fact.Displacement=Relevance.bUsesDisplacement;}
  {QUICK_SCOPE_CYCLE_COUNTER(STAT_OKCrowd_ShadowJobStart);E.ShadowJob=OKPrepareRuntimeShadowMeshAsync(MoveTemp(MainData),MoveTemp(ShadowFacts));}
  FOKGpuActorRecord Actor;Actor.CharacterId=E.Id;Actor.PoseCount=E.Bones.Num();
  for(const auto& B:E.Bones)Actor.PoseBones.Add({B.Name.ToString(),B.Parent,OKGpuBoneReferenceIdentity(B.Reference)});
  FOKGpuSkinPaletteData Palette;{QUICK_SCOPE_CYCLE_COUNTER(STAT_OKCrowd_Palette);if(!OKBuildGpuMeshPalette(Actor,Mesh->GetRefSkeleton(),Mesh->GetRefBasesInvMatrix(),Actor.PoseCount,Palette,Error))return false;}
  FOKGpuMeshRecord MeshRecord;MeshRecord.PartId=TEXT("runtime");MeshRecord.AssetPath=TEXT("/Runtime/APNG/")+E.Id;MeshRecord.Bones={0,uint32(Palette.Items.Num())};Actor.Meshes.Add(MoveTemp(MeshRecord));
  E.ActorRecipe=Actor;E.PaletteRecipe=Palette;
  TArray<FOKRuntimeGpuPlaybackClip> Catalog;
  for(int32 I=0;I<E.Clips.Num();++I){const int32 Source=E.ClipSources[I];Catalog.Add({SourceId(Source),GpuRig&&E.RigDurations.IsValidIndex(I)&&E.RigDurations[I]>0?E.RigDurations[I]:E.Clips[I].Duration,SourceLoop(Source)});}
  if(!E.Clock.Initialize(Catalog,1,0,Error))return false;
  E.CommitMain.Reset();E.CommitVisual.Reset();E.CommitVisualMaterials.Reset();
  E.CommitStep=1;return true;}
  auto* Mesh=E.Mesh.Get();
  if(E.CommitStep==1){QUICK_SCOPE_CYCLE_COUNTER(STAT_OKCrowd_CommitDepthMesh);
  if(!Ibl.IsEmpty()){auto Depth=D;// Color-mask-zero depth uses the identical vertex/material permutation as its color pass.
  // A different masked parent can compile non-invariant skinned positions.
  TArray<UMaterialInterface*> DepthMaterials=Materials;auto* DepthMesh=OKCreateRuntimeRenderMesh(Depth,DepthMaterials,Error);if(!DepthMesh)return false;E.DepthMesh.Reset(DepthMesh);
  // Same actor, same skeleton/binds: both render passes can consume the one
  // private CPU reference palette. Do not allocate/update a second CPU proxy.
  const auto& MainRig=Mesh->GetRefSkeleton();const auto& DepthRig=DepthMesh->GetRefSkeleton();
  const auto& MainBind=Mesh->GetRefBasesInvMatrix();const auto& DepthBind=DepthMesh->GetRefBasesInvMatrix();
  if(MainRig.GetNum()!=DepthRig.GetNum()||MainBind.Num()!=DepthBind.Num()||(MainBind.Num()&&FMemory::Memcmp(MainBind.GetData(),DepthBind.GetData(),MainBind.Num()*sizeof(FMatrix44f)))){Error=TEXT("Runtime source pass private rig bindings differ");return false;}
  for(int32 B=0;B<MainRig.GetNum();++B)if(MainRig.GetBoneName(B)!=DepthRig.GetBoneName(B)||MainRig.GetParentIndex(B)!=DepthRig.GetParentIndex(B)){Error=TEXT("Runtime source pass private rig order differs");return false;}
  }
  E.CommitStep=2;return true;}
  QUICK_SCOPE_CYCLE_COUNTER(STAT_OKCrowd_CommitComponents);
  // CPU renderer matrices are evaluated only for the asset's active bones.
  // Use the full-section source asset so source-only accessory bones are present
  // even though the main draw asset excludes those source-pass sections.
  auto* O=Owner.Get();auto* Cpu=NewObject<UPoseableMeshComponent>(O);O->AddInstanceComponent(Cpu);Cpu->SetSkinnedAssetAndUpdate(E.DepthMesh.IsValid()?E.DepthMesh.Get():Mesh);Cpu->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;Cpu->SetCollisionEnabled(ECollisionEnabled::NoCollision);Cpu->SetCastShadow(false);Cpu->SetRenderInMainPass(false);Cpu->SetRenderInDepthPass(false);Cpu->SetVisibility(true);Cpu->RegisterComponent();Cpu->SetComponentTickEnabled(false);E.Cpu=Cpu;
  // Bounds/key validation/bank assembly touch private plain data only.
  // Keep UObject creation/registration here; never borrow scene/entry pointers
  // across the worker lifetime, including cancellation or scene teardown.
  TArray<FOKRuntimeGpuClip> Clips;
  for(int32 I=0;I<E.Clips.Num();++I){auto& C=E.Clips[I];Clips.Add({SourceId(E.ClipSources[I]),C.Duration,MoveTemp(C.Keys),MoveTemp(C.Tracks)});}
  TArray<int32> EnvelopeParents;EnvelopeParents.Reserve(E.Bones.Num());for(const auto& Bone:E.Bones)EnvelopeParents.Add(Bone.Parent);
  if(!OKBuildRuntimeSkinEnvelopeGeometry(EnvelopeParents,Mesh->GetRefBasesInvMatrix(),D.Vertices,E.SkinEnvelope,Error))return false;
  auto Envelope=E.SkinEnvelope;
  E.PackedBank=Async(EAsyncExecution::ThreadPool,[Actor=E.ActorRecipe,Palette=E.PaletteRecipe,Clips=MoveTemp(Clips),Envelope]()mutable{
   FPrivatePackedBank Result;
  // Conservative envelope includes both private clips and their interpolation /
  // crossfade, without sampling every vertex on every animation frame.
  double Radius=0;TArray<FOKRuntimeSkinEnvelopeClip> EnvelopeClips;EnvelopeClips.Reserve(Clips.Num());
  for(const auto& Clip:Clips)EnvelopeClips.Add({Clip.Keys,Clip.Tracks});
  if(!Envelope.IsValid()||!OKRuntimeSkinEnvelopeRadius(*Envelope,EnvelopeClips,Radius,Result.Error)){if(Result.Error.IsEmpty())Result.Error=TEXT("Runtime animation envelope invalid");return Result;}
  Result.Bounds=OKRuntimeSkinEnvelopeBounds(Radius);

   auto Bank=MakeUnique<FOKGpuAnimationBundle>();
   if(!OKBuildRuntimeGpuActorBank(MoveTemp(Actor),MoveTemp(Clips),MoveTemp(Palette),*Bank,Result.Error)||!OKPackConstantGpuPoseChannels(Bank->Data.Pose,Result.Error)){UE_LOG(LogTemp,Error,TEXT("APNG private packing: %s"),*Result.Error);return Result;}
   Result.CpuKeys=MakeShared<TArray<FVector4f>,ESPMode::ThreadSafe>(Bank->Data.Pose.Keys);
   const auto& OwnerActor=Bank->Manifest.Actors[0];Result.CpuTracks.SetNum(OwnerActor.Clips.Num());for(int32 I=0;I<OwnerActor.Clips.Num();++I)Result.CpuTracks[I].Append(Bank->Data.Pose.Tracks.GetData()+OwnerActor.Clips[I].TrackOffset,OwnerActor.PoseCount);
   Result.Validated=FOKValidatedGpuBank::Create(MoveTemp(Bank),Result.Error);return Result;
  });
  if(!VisualPrefix.IsEmpty()){
   int32 DisplayDebug=0;if(FParse::Value(FCommandLine::Get(),TEXT("OKRuntimeDisplayDebug="),DisplayDebug)&&DisplayDebug>0)for(auto& Material:E.Materials)Material->SetScalarParameterValue(TEXT("__OK_RuntimeDisplayDebug"),DisplayDebug);
   TArray<TSharedPtr<FJsonValue>> Rows;
   for(const auto& Part:E.Material.Parts){
    auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("part"),Part.Name);Row->SetNumberField(TEXT("shader_kind"),Part.ShaderKind);Row->SetBoolField(TEXT("outline_enabled"),Part.Outline);
    TSharedPtr<FJsonObject> Source;FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Part.SourceMetadataJson),Source);if(Source)Row->SetObjectField(TEXT("source"),Source);
    auto Resolved=MakeShared<FJsonObject>();for(const auto& V:Part.Values){TArray<TSharedPtr<FJsonValue>> A;for(int32 I=0;I<4;++I)A.Add(MakeShared<FJsonValueNumber>(V.Value[I]));Resolved->SetArrayField(V.Key,A);}Row->SetObjectField(TEXT("resolved"),Resolved);
    FKKRuntimeMaterialBinding Binding;FString BindingError;if(OKPrepareRuntimeSharedMaterialBinding(Part,FKKRuntimeMaterialScene{},Binding,BindingError)){auto Scalars=MakeShared<FJsonObject>();for(const auto& V:Binding.Scalars)Scalars->SetNumberField(V.Key.ToString(),V.Value);Row->SetObjectField(TEXT("bound_scalars"),Scalars);auto Vectors=MakeShared<FJsonObject>();for(const auto& V:Binding.Vectors){TArray<TSharedPtr<FJsonValue>> A;for(int32 I=0;I<4;++I)A.Add(MakeShared<FJsonValueNumber>((&V.Value.R)[I]));Vectors->SetArrayField(V.Key.ToString(),A);}Row->SetObjectField(TEXT("bound_vectors"),Vectors);}
    TArray<TSharedPtr<FJsonValue>> DiagnosticTextures;for(const auto& Slot:Part.Textures)if(E.Material.Textures.IsValidIndex(Slot.Texture)){const auto& T=E.Material.Textures[Slot.Texture];auto R=MakeShared<FJsonObject>();R->SetStringField(TEXT("property"),Slot.Property);R->SetStringField(TEXT("sha1"),T.SourceHash);R->SetBoolField(TEXT("srgb"),T.Settings.SRGB);DiagnosticTextures.Add(MakeShared<FJsonValueObject>(R));}Row->SetArrayField(TEXT("textures"),DiagnosticTextures);Rows.Add(MakeShared<FJsonValueObject>(Row));
   }
   auto Inputs=MakeShared<FJsonObject>();Inputs->SetArrayField(TEXT("parts"),Rows);FString Json;FJsonSerializer::Serialize(Inputs,TJsonWriterFactory<>::Create(&Json));FFileHelper::SaveStringToFile(Json,*(ReportPath+TEXT(".")+E.Id+TEXT(".materials.json")));
  }
  E.Material={};E.Textures.Reset();E.CommitQueues.Reset();E.CommitCanonical.Reset();E.CommitIdentities.Reset();D={};E.RevealFence.BeginFence(FRenderCommandFence::ESyncDepth::RHIThread);E.RevealQueued=true;E.CommitReady=true;return true;
 }
 void Prepare(FEntry& E){QUICK_SCOPE_CYCLE_COUNTER(STAT_OKCrowd_PrepareEntry);
  if(E.Stage==1&&E.Snapshot.IsReady()){
   auto S=E.Snapshot.Consume();E.SnapshotMs[0]=S.NativeOpenMs;E.SnapshotMs[1]=S.RigCaptureMs;E.SnapshotMs[2]=S.MeshCaptureMs;E.SnapshotMs[3]=S.MaterialCaptureMs;if(!S.Error.IsEmpty()){Fail(E,S.Error);return;}FString Error;TArray<int32> Remap;
   TArray<FName> GameplayBones;
   if(ExternalControl)for(FName Name:{FName(TEXT("cf_j_head")),FName(TEXT("cf_j_hand_L")),FName(TEXT("cf_j_hand_R")),FName(TEXT("cf_j_bust02_L")),FName(TEXT("cf_j_bust02_R")),FName(TEXT("cm_j_head")),FName(TEXT("cm_j_hand_L")),FName(TEXT("cm_j_hand_R")),FName(TEXT("k_f_handL_00")),FName(TEXT("k_f_handR_00"))})if(S.Bones.ContainsByPredicate([Name](const auto& B){return B.Name==Name;}))GameplayBones.Add(Name);
   if(!OKCompactRuntimeRenderRig(S.Bones,S.Parts,GameplayBones,E.Bones,Remap,Error)||!OKRemapRuntimeMeshBones(S.Meshes,Remap,E.Bones.Num(),Error)){Fail(E,Error);return;}
   E.Material=MoveTemp(S.Materials);
   // Missing families must be reported before costly private animation work.
   for(const auto& P:E.Material.Parts)if(OKRuntimeSharedMaterialRecipe(P)==EKKRuntimeMaterialRecipe::None){Fail(E,FString::Printf(TEXT("Source material coverage incomplete: part %d name=%s role=%s kind=%d blend=%s %s"),P.NativePart,*P.Name,*P.Role,P.ShaderKind,*P.Blend,*P.SourceMetadataJson));return;}
   FKKRuntimePoseRequest R;R.PackedGpuCache=true;R.Snapshot=MoveTemp(S.Snapshot);R.Bones=E.Bones;R.Parts=MoveTemp(S.Parts);R.CharacterIdentity=S.RigIdentity;
   R.PersistentSourceIdentity=S.PersistentSourceIdentity;E.PoseRecipe=MakeShared<const FKKRuntimePoseRequest,ESPMode::ThreadSafe>(R);
   E.ClipSources={0,E.WalkIndex};
   const int32 ActionCount=Curves.Num()-WalkSources,Admit=E.SelectionIndex==0&&!ExternalControl?ActionCount:FMath::Min(NpcActionPreload,ActionCount);
   for(int32 I=0;I<Admit;++I)E.ClipSources.Add(WalkSources+(E.SelectionIndex+I)%ActionCount);
   // Slot 2 remains exclusively for streamed actions, including bank restoration.
   if(E.ClipSources.Num()==2&&(ActionCount>0||E.ConversationSource!=INDEX_NONE||E.RunSource!=INDEX_NONE))E.ClipSources.Add(INDEX_NONE);
   if(E.ConversationSource!=INDEX_NONE)E.ConversationSlot=E.ClipSources.Add(E.ConversationSource);
   if(E.RunSource!=INDEX_NONE)E.RunSlot=E.ClipSources.Add(E.RunSource);
   {const int32 Stand=FindClip(TEXT("conversation_neutral.okanim"));if(Stand!=INDEX_NONE)E.StandSlot=Stand==E.ConversationSource?E.ConversationSlot:E.ClipSources.Add(Stand);}
   // Her own few standing poses out of the pool, the same ones for her every time (by her card).
   {TArray<int32> Pool=IdleVariantSources;FRandomStream Pick(int32(GetTypeHash(E.Path)));const int32 Count=FMath::Min(IdleVariantsPerCharacter,Pool.Num());
    for(int32 I=0;I<Count;++I){const int32 K=Pick.RandRange(0,Pool.Num()-1);const int32 Source=Pool[K];Pool.RemoveAtSwap(K);const int32 Existing=E.ClipSources.Find(Source);E.IdleVariantSlots.Add(Existing>=2?Existing:E.ClipSources.Add(Source));}}
   E.Clips.SetNum(E.ClipSources.Num());
   for(int32 Slot=0;Slot<E.ClipSources.Num();++Slot){const int32 Source=E.ClipSources[Slot];if(Source==INDEX_NONE)continue;
    R.AuthoredCurves=ClipBytes(Source);R.Duration=OKRuntimeSharedClipDuration(R.AuthoredCurves);R.AuthoredPrefix=ClipWindows.IsValidIndex(Source)&&ClipWindows[Source]>0;if(R.AuthoredPrefix)R.Duration=FMath::Min(R.Duration,ClipWindows[Source]);R.ClipIdentity=ClipIds[Source];R.Loop=SourceLoop(Source);
    if(GpuRig){if(E.RigDurations.Num()<=Slot)E.RigDurations.SetNumZeroed(Slot+1);E.RigDurations[Slot]=R.Duration;if(GpuRigVerify<=0)R.NativeOnly=true;}
    E.PoseJobSlots.Add(Slot);E.PoseJobs.Add(OKPrepareRuntimePoseBankAsync(R));}
   if(GpuRig){TArray<TArray<uint8>> RigClips;for(const int32 Source:E.ClipSources)RigClips.Add(ClipBytes(Source==INDEX_NONE?E.ClipSources[0]:Source));E.RigJob=OKPrepareRuntimeGpuRigAsync(E.PoseRecipe,MoveTemp(RigClips));}
   for(const auto& Mesh:S.Meshes){E.SourceVertices+=Mesh.Vertices.Num();E.SourceTriangles+=Mesh.Indices.Num()/3;}
   E.StageMs[0]=(FPlatformTime::Seconds()-E.StageStarted)*1000;E.StageStarted=FPlatformTime::Seconds();E.Reduction=LowPolyRules?OKReduceRuntimeMeshesAsync(MoveTemp(S.Meshes),30000,30000,30000,PreserveSourceGeometry,true):OKReduceRuntimeMeshesAsync(MoveTemp(S.Meshes),15000,15000,30000,PreserveSourceGeometry);E.Stage=2;
  }
  if(E.Stage==2&&E.Reduction.IsReady()){
   auto D=E.Reduction.Consume();if(!D.Error.IsEmpty()){Fail(E,D.Error);return;}E.StageMs[1]=(FPlatformTime::Seconds()-E.StageStarted)*1000;E.StageStarted=FPlatformTime::Seconds();E.ReducedBudget=D.BudgetsMet;E.SourceGeometryPreserved=D.SourceGeometryPreserved;E.RenderMesh=OKPrepareRuntimeRenderMeshAsync(MoveTemp(D.Meshes),E.Bones,256);E.Stage=3;
  }
  bool PosesReady=E.Stage==3;if(PosesReady)for(const auto& Job:E.PoseJobs)PosesReady&=Job.IsReady();if(PosesReady&&GpuRig)PosesReady&=E.RigJob.IsReady();
  if(E.Stage==3&&E.RenderMesh.IsReady()&&PosesReady){
   for(int32 I=0;I<E.PoseJobs.Num();++I){const int32 Slot=E.PoseJobSlots[I];E.Clips[Slot]=E.PoseJobs[I].Consume();if(!E.Clips[Slot].Passed()){Fail(E,E.Clips[Slot].Error);return;}}
   if(GpuRig){E.Rig=E.RigJob.Consume();if(!E.Rig.Passed()){Fail(E,TEXT("GPU rig: ")+E.Rig.Error);return;}if(E.RigDurations.IsValidIndex(0)&&E.RigDurations.Num()<E.ClipSources.Num())E.RigDurations.SetNum(E.ClipSources.Num());for(int32 I=0;I<E.RigDurations.Num();++I)if(E.RigDurations[I]<=0&&E.RigDurations.IsValidIndex(0))E.RigDurations[I]=E.RigDurations[0];
    UE_LOG(LogTemp,Display,TEXT("APNG GPU rig %s: %d bones, %d render bones, %d clips, %d loose palettes, %d empirical terms, %.0f ms"),*E.Id,E.Rig.Names.Num(),E.Rig.BoneTerms.Num(),E.Rig.Clips.Num(),E.Rig.LoosePalettes,E.Rig.EmpiricalTerms,E.Rig.PrepareMs);}
   if(E.ClipSources.IsValidIndex(2)&&E.ClipSources[2]==INDEX_NONE){E.Clips[2]=E.Clips[0];E.Clips[2].PersistentCacheHit=false;E.Clips[2].PersistentCacheBytes=0;}E.StageMs[2]=(FPlatformTime::Seconds()-E.StageStarted)*1000;E.StageStarted=FPlatformTime::Seconds();E.Stage=4;
  }
 }
 FRenderCommandFence LoadingUploadFence;bool LoadingUploadPending=false;uint64 LoadingUploadDeferrals=0;
 TArray<double> LoadingFrames;double LoadingLastWall=0;
 #include "OKCrowdRuntimeProgressiveGpu.inl"
 #include "OKCrowdRuntimeShadow.inl"
 // The GPU rig's tables for every installed actor, in the aggregate bank's actor and task order (FOKGpuRigBatch).
 TArray<FString> RowNames;TArray<uint32> RowActors;
 TSharedPtr<const FOKGpuRigBatch,ESPMode::ThreadSafe> BuildRigBatch(const FOKGpuAnimationBundle& Bank,FString& Error){
  auto B=MakeShared<FOKGpuRigBatch,ESPMode::ThreadSafe>();
  TArray<FEntry*> ByActor;ByActor.SetNumZeroed(Bank.Manifest.Actors.Num());for(auto& E:Entries)if(E->BankActor!=INDEX_NONE&&ByActor.IsValidIndex(E->BankActor))ByActor[E->BankActor]=E.Get();
  uint32 Slots=1;for(auto* E:ByActor){if(!E||!E->Rig.Passed()||E->Rig.Names.IsEmpty()){Error=TEXT("GPU rig missing for an installed actor");return nullptr;}Slots=FMath::Max<uint32>(Slots,E->ClipSources.Num());}
  B->ActorCount=ByActor.Num();B->ClipSlotsPerActor=Slots;
  auto Bits=[](uint32 V){return reinterpret_cast<const float&>(V);};
  TArray<uint32> BoneFirst,NodeFirst,NameCount;TMap<int32,TPair<uint32,FKKRuntimeGpuClip>> Shared;
  for(int32 A=0;A<ByActor.Num();++A){auto& E=*ByActor[A];const auto& Rig=E.Rig;const uint32 First=B->BoneCount;BoneFirst.Add(First);
   auto Global=[&](int32 Local){return Local<0?MAX_uint32:First+uint32(Local);};
   for(int32 K=0;K<Rig.Names.Num();++K){B->Bones.Add(FVector4f(Rig.Pos[K].X,Rig.Pos[K].Y,Rig.Pos[K].Z,Bits(Global(Rig.Parent[K]))));B->Bones.Add(Rig.Rot[K]);B->Bones.Add(FVector4f(Rig.Scale[K].X,Rig.Scale[K].Y,Rig.Scale[K].Z,0));}
   B->BoneCount+=Rig.Names.Num();
   // Her DynamicBone chains, her indices made global (a node ref keeps its flag; nodes follow all bones in the worlds).
   {const auto& Ph=Rig.Physics;const uint32 NF=B->NodeCount,PF=B->PhysParticles.Num()/8,CF=B->PhysColliders.Num()/2,ChF=B->PhysChains.Num()/2,FF=B->PhysFollow.Num();
    NodeFirst.Add(NF);NameCount.Add(Rig.Names.Num());
    auto U=[](float V){return reinterpret_cast<const uint32&>(V);};
    auto Ref=[&](uint32 R)->uint32{if(R==MAX_uint32)return R;return (R&OKGpuRigNodeFlag)?(OKGpuRigPhysicsNode|(NF+(R&~OKGpuRigNodeFlag))):Global(int32(R));};
    for(int32 N=0;N<Ph.NodeCount();++N){for(int32 K=0;K<8;++K){FVector4f Row=Ph.Nodes[N*8+K];
      if(K==2)Row.W=Bits(Ref(U(Row.W)));if(K==7){const uint32 Alias=U(Row.X),From=U(Row.Y);Row.X=Bits(Alias==MAX_uint32?MAX_uint32:Global(int32(Alias)));Row.Y=Bits(From==MAX_uint32?MAX_uint32:Global(int32(From)));}B->PhysNodes.Add(Row);}}
    for(int32 C=0;C*2<Ph.Chains.Num();++C){const auto& H=Ph.Chains[C*2];const auto& K=Ph.Chains[C*2+1];
     B->PhysChains.Add(FUintVector4(NF+H.X,NF+H.Y,H.Z,H.W));B->PhysChains.Add(FUintVector4(PF+K.X,K.Y,CF+K.Z,K.W));
     for(int32 V=0;V<4;++V)B->PhysChainValues.Add(Ph.ChainValues[C*4+V]);
     for(uint32 P=K.X;P<K.X+K.Y;++P){FVector4f Row=Ph.Particles[P*8];const uint32 Node=U(Row.X);const int32 Parent=int32(U(Row.Y));
      Row.X=Bits(Node==MAX_uint32?MAX_uint32:NF+Node);Row.Y=Bits(Parent<0?MAX_uint32:PF+K.X+uint32(Parent));
      B->PhysParticles.Add(Row);for(int32 R=1;R<8;++R)B->PhysParticles.Add(Ph.Particles[P*8+R]);}}
    for(int32 C=0;C*2<Ph.Colliders.Num();++C){FVector4f Row=Ph.Colliders[C*2+1];Row.W=Bits(Ref(U(Row.W)));B->PhysColliders.Add(Ph.Colliders[C*2]);B->PhysColliders.Add(Row);}
    for(const uint32 F:Ph.Follow)B->PhysFollow.Add(Global(int32(F)));
    // The bones under the body's solved ones follow after the last set that moves them (clothes, else the body's Ver02).
    int32 Last=INDEX_NONE;for(int32 S=0;S*2<Ph.Sets.Num();++S){const uint32 Kind=Ph.Sets[S*2].X&0xffu;if(Kind!=0&&(Last==INDEX_NONE||Kind<(Ph.Sets[Last*2].X&0xffu)))Last=S;}
    for(int32 S=0;S*2<Ph.Sets.Num();++S){const auto& SetHead=Ph.Sets[S*2];const bool Follows=S==Last;
     // Each chain knows its set (local gravity .w): a chain thread skips a set stepped as a whole.
     for(uint32 C=0;C<Ph.Sets[S*2+1].X;++C)B->PhysChainValues[(ChF+SetHead.W+C)*4+2].W=Bits(uint32(B->PhysSets.Num()/2));
     B->PhysSets.Add(FUintVector4(SetHead.X,NF+SetHead.Y,SetHead.Z,ChF+SetHead.W));B->PhysSets.Add(FUintVector4(Ph.Sets[S*2+1].X,Follows?FF:0,Follows?uint32(Ph.Follow.Num()):0,0));}
    B->NodeCount+=Ph.NodeCount();}
   const uint32 AlwaysFirst=B->Lists.Num();for(const auto& P:Rig.Always)B->Lists.Add(FUintVector4(Global(P.X),uint32(P.Y),0,0));
   const uint32 ForeFirst=B->Lists.Num();for(const int32 X:Rig.Forearms)B->Lists.Add(FUintVector4(Global(X),0,0,0));
   const uint32 HandFirst=B->Lists.Num();for(const int32 X:Rig.Hands)B->Lists.Add(FUintVector4(Global(X),0,0,0));
   const uint32 PvFirst=B->Lists.Num();for(const auto& P:Rig.Pv)B->Lists.Add(FUintVector4(Global(P.X),Global(P.Y),0,0));
   const uint32 AbmFirst=B->Abm.Num()/3;for(const auto& Op:Rig.Abm){B->Abm.Add(Op.Rot);B->Abm.Add(FVector4f(Op.Pos.X,Op.Pos.Y,Op.Pos.Z,Op.Length));B->Abm.Add(FVector4f(Op.Scale.X,Op.Scale.Y,Op.Scale.Z,Bits(Global(Op.Bone))));}
   B->Actors.Add(FUintVector4(First,Rig.Names.Num(),Rig.Female?1u:0u,0));B->Actors.Add(FUintVector4(AlwaysFirst,Rig.Always.Num(),ForeFirst,Rig.Forearms.Num()));
   B->Actors.Add(FUintVector4(HandFirst,Rig.Hands.Num(),PvFirst,Rig.Pv.Num()));auto Look=[&](const TCHAR* Female,const TCHAR* Male){int32 K=Rig.Names.IndexOfByKey(FString(Female));if(K==INDEX_NONE)K=Rig.Names.IndexOfByKey(FString(Male));return Global(K);};
   B->Actors.Add(FUintVector4(AbmFirst,Rig.Abm.Num(),Look(TEXT("cf_j_neck"),TEXT("cm_j_neck")),Look(TEXT("cf_j_head"),TEXT("cm_j_head"))));
   if(Rig.Named.Num()!=66){Error=TEXT("GPU rig named table size");return nullptr;}for(const int32 N:Rig.Named)B->Named.Add(Global(N));
   TMap<FString,int32> ByName;for(int32 K=0;K<Rig.Names.Num();++K)ByName.FindOrAdd(Rig.Names[K],K);
   for(uint32 S=0;S<Slots;++S){
    if(!E.ClipSources.IsValidIndex(S)||!Rig.Clips.IsValidIndex(S)||Rig.Clips[S].IsEmpty()){B->ClipSlots.Add(FUintVector4(0,0,0,0));B->ClipRates.Add(FVector4f(60,0,0,0));continue;}
    const int32 Source=E.ClipSources[S]==INDEX_NONE?E.ClipSources[0]:E.ClipSources[S];
    auto* Clip=Shared.Find(Source);
    if(!Clip){FKKRuntimeGpuClip Parsed;if(!OKParseRuntimeGpuClip(Rig.Clips[S],Parsed)){Error=TEXT("GPU rig clip export malformed: ")+ClipIds[Source];return nullptr;}const uint32 Row=B->Samples.Num();B->Samples.Append(Parsed.Samples);Clip=&Shared.Add(Source,TPair<uint32,FKKRuntimeGpuClip>(Row,MoveTemp(Parsed)));}
    const auto& C=Clip->Value;const uint32 ChannelFirst=B->Channels.Num();
    for(int32 Ch=0;Ch<C.Bones.Num();++Ch){const int32* Local=ByName.Find(C.Bones[Ch]);B->Channels.Add(FUintVector4(Local?First+uint32(*Local):MAX_uint32,C.Mask[Ch],Clip->Key+uint32(Ch)*C.Frames*3,0));}
    B->ClipSlots.Add(FUintVector4(ChannelFirst,C.Bones.Num(),C.Frames,C.Flags));B->ClipRates.Add(FVector4f(C.Rate,C.Duration,0,0));
   }
  }
  for(const auto& Task:Bank.Data.Pose.Tasks){
   auto* E=ByActor.IsValidIndex(int32(Task.Y))?ByActor[Task.Y]:nullptr;if(!E||!E->Rig.BoneTerms.IsValidIndex(int32(Task.X))){Error=TEXT("GPU rig task outside an actor's render bones");return nullptr;}
   const auto& Terms=E->Rig.BoneTerms[Task.X];const uint32 First=B->Terms.Num()/4;
   // The bank's row is the bone's component matrix x its inverse bind (IndependentPose.usf); a palette alias bone's
   // component keeps only its axis column, three times over, and the translation (ColumnLocal, OKSkinPaletteColumn).
   // Both are linear: folded into C.
   const auto& Pose=Bank.Data.Pose;if(!Pose.Actors.IsValidIndex(int32(Task.Y))||!Pose.InverseBindRows.IsValidIndex(int32(Task.Z)+2)){Error=TEXT("GPU rig task bind outside the bank");return nullptr;}
   const uint32 TrackIndex=Pose.Actors[Task.Y].X+Task.X;if(!Pose.Tracks.IsValidIndex(int32(TrackIndex))){Error=TEXT("GPU rig task track outside the bank");return nullptr;}
   const uint32 Track=Pose.Tracks[TrackIndex].W;const int32 Axis=(Track&32)?int32((Track>>6)&3):-1;
   const FVector4f* Bind=&Pose.InverseBindRows[Task.Z];
   for(const auto& T:Terms){
    FVector4f C[3];for(int32 R=0;R<3;++R){FVector4f Row=T.C[R];if(Axis>=0){for(int32 K=0;K<3;++K)if(K!=Axis)Row[K]=0;Row[Axis]*=3.f;}
     for(int32 K=0;K<4;++K)C[R][K]=Row.X*Bind[0][K]+Row.Y*Bind[1][K]+Row.Z*Bind[2][K]+(K==3?Row.W:0.f);}
    // Her physics nodes come after her bones; in the worlds buffer after everyone's bones.
    const uint32 Node=T.Node<0?MAX_uint32:T.Node>=int32(NameCount[Task.Y])?B->BoneCount+NodeFirst[Task.Y]+uint32(T.Node-int32(NameCount[Task.Y])):BoneFirst[Task.Y]+uint32(T.Node);
    B->Terms.Add(FVector4f(Bits(Node),T.Weight,0,0));B->Terms.Add(C[0]);B->Terms.Add(C[1]);B->Terms.Add(C[2]);}
   B->Tasks.Add(FUintVector4(First,Terms.Num(),0,0));
   RowActors.Add(Task.Y);if(GpuRigVerify>0)RowNames.Add(FString::Printf(TEXT("%u %s %d"),Task.Y,*E->PoseRecipe->Bones[Task.X].Name.ToString(),Terms.Num()>0?Terms[0].Node:-2));
  }
  if(!B->Validate(Error))return nullptr;
  UE_LOG(LogTemp,Display,TEXT("APNG GPU rig batch: %u actors, %u bones, %d tasks, %d terms, %d shared clips (%d sample rows); physics %d sets, %u nodes, %d chains, %d particles, %d colliders"),B->ActorCount,B->BoneCount,B->Tasks.Num(),B->Terms.Num()/4,Shared.Num(),B->Samples.Num(),B->PhysSets.Num()/2,B->NodeCount,B->PhysChains.Num()/2,B->PhysParticles.Num()/4,B->PhysColliders.Num()/2);
  return B;
 }
 void Install(){QUICK_SCOPE_CYCLE_COUNTER(STAT_OKCrowd_Install);
  auto Verified=PendingComposition;const auto* Bank=Verified.IsValid()?&Verified->Get():nullptr;if(!Bank){UE_LOG(LogTemp,Error,TEXT("Runtime private GPU bank composition rejected"));VisualError=TEXT("Runtime private GPU bank composition rejected");WriteReport();Stopped=true;return;}
  for(auto& E:Entries){if(E->LoadingGpuInstalled){if(auto* G=E->Gpu.Get())G->DestroyComponent();if(auto* G=E->DepthGpu.Get())G->DestroyComponent();if(auto* G=E->ShadowGpu.Get())G->DestroyComponent();E->Gpu.Reset();E->DepthGpu.Reset();E->ShadowGpu.Reset();RetireProgressiveGpu(*E);}}
  auto* O=Owner.Get();auto* Renderer=static_cast<::FScene*>(O->GetWorld()->Scene->GetRenderScene());TMap<const USkinnedAsset*,int32> MeshActors;TMap<const USkinnedAsset*,FPrimitiveComponentId> References;TArray<FOKGpuSkinPaletteData> Palettes;TArray<FPrimitiveComponentId> DepthProxies;TSet<FPrimitiveComponentId> CpuSourceProxies;TSet<const FMaterialRenderProxy*> SourceIblMaterials;for(const auto& Entry:Entries)for(const auto& Material:Entry->Materials)if(Material.IsValid()&&Material->GetMaterial()->GetName()==TEXT("M_IBLAlpha"))SourceIblMaterials.Add(Material->GetRenderProxy());
  for(int32 A=0;A<Entries.Num();++A){auto& E=*Entries[A];if(E.BankActor==INDEX_NONE)continue;BeginReveal(E);FString Error;FOKGpuSkinPaletteData P;if(!OKBuildGpuMeshPalette(Bank->Manifest.Actors[E.BankActor],E.Mesh->GetRefSkeleton(),E.Mesh->GetRefBasesInvMatrix(),Bank->Data.Pose.Tasks.Num(),P,Error)){UE_LOG(LogTemp,Error,TEXT("APNG aggregate palette: %s"),*Error);Stopped=true;return;}MeshActors.Add(E.Mesh.Get(),Palettes.Num());Palettes.Add(MoveTemp(P));References.Add(E.Mesh.Get(),E.Cpu->GetPrimitiveSceneId());
   auto* Gpu=NewObject<UInstancedSkinnedMeshComponent>(O);O->AddInstanceComponent(Gpu);Gpu->SetSkinnedAssetAndUpdate(E.Mesh.Get());auto* Provider=NewObject<UOKIndependentTransformProvider>(Gpu);Provider->SetRuntimeProviderId(Render->ProviderId);Provider->SetDiagnosticBounds(E.AnimationBounds);Gpu->SetTransformProvider(Provider);Gpu->SetAnimationMinScreenSize(-1);Gpu->SetCollisionEnabled(ECollisionEnabled::NoCollision);Gpu->SetCastShadow(false);Gpu->SetLightingChannels(true,false,true); /* channel 2: the street catch light, read by the lit tears */Gpu->SetVisibility(false);Gpu->AddInstance(FTransform::Identity,0);Gpu->RegisterComponent();Gpu->SetComponentTickEnabled(false);E.Gpu=Gpu;AttachFrontHair(E,Render->ProviderId);
  }
  for(int32 A=0;A<Entries.Num();++A){auto& E=*Entries[A];if(!E.DepthMesh.IsValid()||E.BankActor==INDEX_NONE)continue;FString Error;FOKGpuSkinPaletteData P;if(!OKBuildGpuMeshPalette(Bank->Manifest.Actors[E.BankActor],E.DepthMesh->GetRefSkeleton(),E.DepthMesh->GetRefBasesInvMatrix(),Bank->Data.Pose.Tasks.Num(),P,Error)){UE_LOG(LogTemp,Error,TEXT("APNG depth palette: %s"),*Error);Stopped=true;return;}MeshActors.Add(E.DepthMesh.Get(),Palettes.Num());References.Add(E.DepthMesh.Get(),E.Cpu->GetPrimitiveSceneId());Palettes.Add(MoveTemp(P));auto* Gpu=NewObject<UInstancedSkinnedMeshComponent>(O);O->AddInstanceComponent(Gpu);Gpu->SetSkinnedAssetAndUpdate(E.DepthMesh.Get());auto* Provider=NewObject<UOKIndependentTransformProvider>(Gpu);Provider->SetRuntimeProviderId(Render->ProviderId);Provider->SetDiagnosticBounds(E.AnimationBounds);Gpu->SetTransformProvider(Provider);Gpu->SetAnimationMinScreenSize(-1);Gpu->SetCollisionEnabled(ECollisionEnabled::NoCollision);Gpu->SetRenderInMainPass(false);Gpu->SetRenderInDepthPass(false);Gpu->SetCastShadow(false);Gpu->SetVisibility(false);Gpu->AddInstance(FTransform::Identity,0);Gpu->RegisterComponent();Gpu->SetComponentTickEnabled(false);E.DepthGpu=Gpu;if(DepthPrepassEnabled){DepthProxies.Add(Gpu->GetPrimitiveSceneId());}}
  TSharedPtr<const FOKGpuRigBatch,ESPMode::ThreadSafe> RigBatch;
  if(GpuRig){FString RigError;RigBatch=BuildRigBatch(*Bank,RigError);if(!RigBatch){UE_LOG(LogTemp,Error,TEXT("APNG GPU rig: %s"),*RigError);VisualError=TEXT("GPU rig: ")+RigError;WriteReport();Stopped=true;return;}}
  ENQUEUE_RENDER_COMMAND(InstallApngProvider)([Renderer,R=Render,Bank,Verified,RigBatch,RigVerify=GpuRig?GpuRigVerify:0,RigDump=GpuRig&&GpuRigVerify>0&&!ReportPath.IsEmpty()?FPaths::GetPath(ReportPath)/TEXT("rig"):FString(),RigRows=MoveTemp(RowNames),RigActors=MoveTemp(RowActors),MeshActors=MoveTemp(MeshActors),References=MoveTemp(References),DepthProxies=MoveTemp(DepthProxies),CpuSourceProxies=MoveTemp(CpuSourceProxies),SourceIblMaterials=MoveTemp(SourceIblMaterials),Palettes=MoveTemp(Palettes)](FRHICommandListImmediate&)mutable{R->RigBatch=RigBatch;R->RigVerifyLeft=RigVerify;R->RigDumpPath=RigDump;R->RigRowNames=MoveTemp(RigRows);R->RigRowActors=MoveTemp(RigActors);R->CpuSourceProxies=MoveTemp(CpuSourceProxies);R->SourceIblMaterials=MoveTemp(SourceIblMaterials);R->Bank=Bank;R->Scene=Renderer;R->LateDepthProxies=MoveTemp(DepthProxies);R->CpuReferences=MoveTemp(References);R->MeshActors=MoveTemp(MeshActors);R->Sources=MoveTemp(Palettes);R->Validated=Verified;R->Initialized=true;R->Palettes.SetNum(R->Sources.Num());R->BoneMaps.SetNum(R->Sources.Num());R->StorageModes.SetNum(R->Sources.Num());if(auto* P=Renderer->GetExtensionPtr<FSkinningTransformProvider>()){P->RegisterProvider(R->ProviderId,FSkinningTransformProvider::FOnProvideTransforms::CreateLambda([R](auto& C){R->Provide(C);}),false);R->ProviderRegistered=true;}else R->Fail(TEXT("Skinning scene provider unavailable"));});if(!LateDepthExtension.IsValid()){LateDepthExtension=FSceneViewExtensions::NewExtension<FLateSourceDepth>(Render,O->GetWorld()->Scene);ENQUEUE_RENDER_COMMAND(RegisterApngComposite)([Extension=LateDepthExtension](FRHICommandListImmediate&){Extension->Register();});}Installed=true;for(auto& E:Entries){if(auto* C=E->LoadingMain.Get()){C->DestroyComponent();E->LoadingMain.Reset();}if(auto* C=E->Cpu.Get()){C->SetRenderInMainPass(false);C->SetRenderInDepthPass(false);}}
 }
 // Her front hair once more, into custom depth / stencil only (stencil = her ForegroundStencil), when any of her eyes /
 // brows took a _Fg parent: those test against it to show through her front hair (Tools/GpuPoseAudit
 // PrepareForegroundMaterial.cpp). Same mesh and pose provider as her Gpu, so the depths match; every other slot gets a
 // translucent material, which the custom depth pass skips.
 void AttachFrontHair(FEntry& E,const FGuid& ProviderId){
  if(auto* Old=E.FrontGpu.Get())Old->DestroyComponent();E.FrontGpu.Reset();
  if(!E.FaceClasses.Contains(2))return;
  if(E.FrontHairSlots.IsEmpty()||!TearParent.IsValid()||!E.Mesh.IsValid()){UE_LOG(LogTemp,Display,TEXT("Runtime foreground %s: no front hair copy (front hair slots=%d)"),*E.Id,E.FrontHairSlots.Num());return;}
  auto* O=Owner.Get();auto* G=NewObject<UInstancedSkinnedMeshComponent>(O);O->AddInstanceComponent(G);G->SetSkinnedAssetAndUpdate(E.Mesh.Get());
  auto* Provider=NewObject<UOKIndependentTransformProvider>(G);Provider->SetRuntimeProviderId(ProviderId);Provider->SetDiagnosticBounds(E.AnimationBounds);G->SetTransformProvider(Provider);
  G->SetAnimationMinScreenSize(-1);G->SetCollisionEnabled(ECollisionEnabled::NoCollision);G->SetCastShadow(false);
  G->SetRenderInMainPass(false);G->SetRenderInDepthPass(false);G->SetRenderCustomDepth(true);G->SetCustomDepthStencilValue(E.ForegroundStencil);
  for(int32 S=0;S<E.Mesh->GetMaterials().Num();++S)if(!E.FrontHairSlots.Contains(S))G->SetMaterial(S,TearParent.Get());
  G->SetVisibility(false);G->AddInstance(FTransform::Identity,0);G->RegisterComponent();G->SetComponentTickEnabled(false);E.FrontGpu=G;
  // The stencil half of custom depth (r.CustomDepth 3) is off by default; OKStartCrowdRuntimeScene sets it already.
  if(auto* V=IConsoleManager::Get().FindConsoleVariable(TEXT("r.CustomDepth")))if(V->GetInt()!=3)V->Set(3,ECVF_SetByCode);
  UE_LOG(LogTemp,Display,TEXT("Runtime foreground %s: front hair copy slots=%d stencil=%d"),*E.Id,E.FrontHairSlots.Num(),E.ForegroundStencil);
  // Her body again for the occlusion capture only: every slot as drawn except the front hair (translucent slots, her
  // eyes / brows / tears among them, write no depth).
  if(auto* Old=E.OccluderGpu.Get())Old->DestroyComponent();E.OccluderGpu.Reset();
  auto* C=NewObject<UInstancedSkinnedMeshComponent>(O);O->AddInstanceComponent(C);C->SetSkinnedAssetAndUpdate(E.Mesh.Get());
  auto* CProvider=NewObject<UOKIndependentTransformProvider>(C);CProvider->SetRuntimeProviderId(ProviderId);CProvider->SetDiagnosticBounds(E.AnimationBounds);C->SetTransformProvider(CProvider);
  C->SetAnimationMinScreenSize(-1);C->SetCollisionEnabled(ECollisionEnabled::NoCollision);C->SetCastShadow(false);C->SetVisibleInSceneCaptureOnly(true);
  for(int32 S=0;S<E.Mesh->GetMaterials().Num();++S)C->SetMaterial(S,E.FrontHairSlots.Contains(S)?TearParent.Get():(E.Gpu.IsValid()?E.Gpu->GetMaterial(S):nullptr));
  C->SetVisibility(false);C->AddInstance(FTransform::Identity,0);C->RegisterComponent();C->SetComponentTickEnabled(false);E.OccluderGpu=C;
  EnsureForegroundCapture();
  for(int32 I=0;I<E.FaceClasses.Num()&&I<E.Materials.Num();++I)if(E.FaceClasses[I]==2)if(auto* M=E.Materials[I].Get()){
   M->SetTextureParameterValue(TEXT("__OK_ForegroundOcclusion"),ForegroundDepth.Get());M->SetScalarParameterValue(TEXT("__OK_ForegroundOcclusionOn"),1);}
 }
 void EnsureForegroundCapture(){
  if(ForegroundCapture.IsValid())return;auto* O=Owner.Get();if(!O)return;
  // In the transient package, not under the actor: the render target must not lead back to the world.
  ForegroundDepth.Reset(NewObject<UTextureRenderTarget2D>(GetTransientPackage()));ForegroundDepth->RenderTargetFormat=RTF_R32f;ForegroundDepth->Filter=TF_Nearest;ForegroundDepth->ClearColor=FLinearColor(1e9f,0,0,0);
  ForegroundDepth->InitAutoFormat(64,64);
  auto* Cap=NewObject<USceneCaptureComponent2D>(O);O->AddInstanceComponent(Cap);Cap->CaptureSource=SCS_SceneDepth;Cap->TextureTarget=ForegroundDepth.Get();
  Cap->PrimitiveRenderMode=ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;Cap->bCaptureEveryFrame=true;Cap->bCaptureOnMovement=false;Cap->bAlwaysPersistRenderingState=true;
  Cap->ShowFlags.SetAtmosphere(false);Cap->ShowFlags.SetFog(false);Cap->ShowFlags.SetPostProcessing(false);Cap->ShowFlags.SetLighting(false);
  // A depth pass inside the main renderer instead of a second scene renderer each frame (about 2.4 ms of the render
  // thread with several heroines in view; Docs/STREET_ERRORS.md 2026-10-24).
  Cap->bRenderInMainRenderer=true;
  Cap->RegisterComponent();ForegroundCapture=Cap;
  UE_LOG(LogTemp,Display,TEXT("Runtime foreground occlusion capture created"));
 }
 // How near a girl must be for her eyes to show through her front hair (FrontGpu, OccluderGpu): cm.
 static constexpr double OKForegroundRange=1000.;
 // Each frame: the capture takes the player camera's view and the viewport's size, showing every OccluderGpu.
 void UpdateForegroundCapture(const FVector& Location,const FRotator& Rotation,float Fov){
  auto* Cap=ForegroundCapture.Get();if(!Cap||!ForegroundDepth.IsValid())return;
  FVector2D Size(1280,720);if(GEngine&&GEngine->GameViewport)GEngine->GameViewport->GetViewportSize(Size);
  const int32 W=FMath::Max(16,int32(Size.X)),H=FMath::Max(16,int32(Size.Y));
  if(ForegroundDepth->SizeX!=W||ForegroundDepth->SizeY!=H)ForegroundDepth->ResizeTarget(W,H);
  Cap->SetWorldLocationAndRotation(Location,Rotation);Cap->FOVAngle=Fov;
  // Captured only while one of them stands in front of the camera within 30 m (her eyes are a few pixels beyond).
  Cap->ShowOnlyComponents.Reset();const FVector Forward=Rotation.Vector();
  for(const auto& E:Entries)if(auto* C=E->OccluderGpu.Get()){const FVector To=E->Transform.GetTranslation()-Location;
   if(To.SizeSquared()<FMath::Square(OKForegroundRange)&&FVector::DotProduct(To,Forward)>-100.)Cap->ShowOnlyComponents.Add(C);}
  Cap->bCaptureEveryFrame=!Cap->ShowOnlyComponents.IsEmpty();
 }
 bool ActionSlotBusy(int32 Actor)const{for(const auto& Slot:ActionSlots)if(Slot.Actor==Actor)return true;return false;}
 int32 FreeActionSlot()const{for(int32 Index=0;Index<OKCrowdRuntimeActionSlots;++Index)if(ActionSlots[Index].Actor==INDEX_NONE)return Index;return INDEX_NONE;}
 int32 ActiveActionSlots()const{int32 Count=0;for(const auto& Slot:ActionSlots)Count+=Slot.Actor!=INDEX_NONE;return Count;}
 int32 QueuedActionRequests()const{int32 Count=0;for(const auto& E:Entries)Count+=E->PendingAction!=INDEX_NONE;return Count;}
 void ClearActionSlot(FActionStreamSlot& Slot){Slot.Job=TFuture<FStreamPrepared>();OKCrowdFreeOnWorker(MoveTemp(Slot.Prepared),MoveTemp(Slot.Install));Slot.Actor=INDEX_NONE;Slot.Source=INDEX_NONE;Slot.Epoch=0;}
 bool RestoreStreamCatalog(FEntry& E){
  if(!E.StreamInstalled)return true;
  if(E.BaseClips.Num()<3||!E.BaseKeys.IsValid()||!E.ClipSources.IsValidIndex(2)){++E.StreamErrors;UE_LOG(LogTemp,Warning,TEXT("Runtime action restore missing private base"));return false;}
  FString Error;if(!E.Clock.ReplaceInactiveClip(2,{SourceId(E.BaseSource),E.BaseClips[2].Duration,SourceLoop(E.BaseSource)},Error))return false;
  OKCrowdFreeOnWorker(MakeUnique<TArray<FKKRuntimePoseResult>>(MoveTemp(E.Clips)),MoveTemp(E.CpuKeys)); // the streamed action's
  E.Clips=MoveTemp(E.BaseClips);E.CpuKeys=MoveTemp(E.BaseKeys);E.ClipSources[2]=E.BaseSource;E.BaseSource=INDEX_NONE;E.StreamInstalled=false;E.InstalledEpoch=0;E.StreamHold=false;return true;
 }
 bool QueueRuntimeAction(FEntry& E,int32 Source,bool Play,FString& Error){
  if(E.PendingAction!=INDEX_NONE){if(E.PendingAction!=Source){Error=TEXT("Runtime character already has an action request pending");return false;}if(Play)E.PendingPlay=true;Error.Reset();return true;}
  if(E.StreamInstalled&&E.ClipSources.IsValidIndex(2)&&E.ClipSources[2]==Source){
   if(!Play){E.StreamHold=true;Error.Reset();return true;}
   if(!E.Clock.RequestClip(2,.3,0,Error))return false;E.StreamHold=false;E.ActionOverride=2;E.LastClip=2;++E.ActionRequests;return true;
  }
  if(QueuedActionRequests()>=OKCrowdRuntimeActionQueue){Error=TEXT("Runtime action preparation queue is full; retain current animation");return false;}
  E.PendingEpoch=++E.EpochClock;E.PendingAction=Source;E.PendingPlay=Play;E.PendingActionAt=FPlatformTime::Seconds();++E.StreamRequests;Error.Reset();return true;
 }
 #include "OKCrowdRuntimeStream.inl"
 void WriteReport(){QUICK_SCOPE_CYCLE_COUNTER(STAT_OKCrowd_WriteReport);
  if(ReportPath.IsEmpty())return;

  FOKGpuResidencyStats Residency;uint64 BankKeyBytes=0;uint64 CachedCommands=0,CachedPrimitives=0;TArray<FPrimitiveComponentId> AuditIds;for(const auto& E:Entries)if(E->Gpu.IsValid())AuditIds.Add(E->Gpu->GetPrimitiveSceneId());
  ENQUEUE_RENDER_COMMAND(AuditRuntimePersistentDraws)([Scene=Render->Scene,Ids=MoveTemp(AuditIds),&CachedCommands,&CachedPrimitives,&Residency,&BankKeyBytes,R=Render](FRHICommandListImmediate&){Residency=R->Budget->GetStats();if(R->Bank)BankKeyBytes=uint64(R->Bank->Data.Pose.Keys.Num())*16;if(!Scene)return;for(const auto Id:Ids){const auto* Info=Scene->GetPrimitiveSceneInfo(Id);if(!Info)continue;uint64 Count=0;for(const auto& Command:Info->StaticMeshCommandInfos)if(Command.CommandIndex!=INDEX_NONE||Command.StateBucketId!=INDEX_NONE)++Count;CachedCommands+=Count;CachedPrimitives+=Count>0;}});FlushRenderingCommands();
  auto J=MakeShared<FJsonObject>();J->SetStringField(TEXT("display_lut"),DisplayLUTPath);J->SetBoolField(TEXT("display_lut_applied"),DisplayLUT.IsValid());J->SetNumberField(TEXT("reveal_duration_seconds"),3);J->SetBoolField(TEXT("reveal_gpu_time"),true);J->SetNumberField(TEXT("gpu_bank_upload_chunks"),Render->UploadChunks.Load());J->SetNumberField(TEXT("gpu_bank_upload_chunk_bytes"),4ull*1024*1024);J->SetBoolField(TEXT("gpu_bank_validated_off_thread"),Render->Validated.IsValid());J->SetNumberField(TEXT("gpu_pose_charged_bytes"),Residency.ChargedBytes);J->SetNumberField(TEXT("gpu_pose_peak_charged_bytes"),Residency.PeakBytes);J->SetNumberField(TEXT("gpu_pose_key_bytes"),BankKeyBytes);J->SetNumberField(TEXT("palette_layout_builds"),Render->PaletteLayoutBuilds.Load());J->SetNumberField(TEXT("palette_layout_reuses"),Render->PaletteLayoutReuses.Load());J->SetNumberField(TEXT("source_command_builds"),Render->SourceCommandBuilds.Load());J->SetNumberField(TEXT("source_command_reuses"),Render->SourceCommandReuses.Load());J->SetNumberField(TEXT("diagnostic_load_deadline_seconds"),LoadDiagnosticDeadline);J->SetNumberField(TEXT("actual_cached_gpu_primitives"),CachedPrimitives);J->SetNumberField(TEXT("actual_cached_gpu_mesh_commands"),CachedCommands);J->SetNumberField(TEXT("npc_action_preload_count"),NpcActionPreload);J->SetNumberField(TEXT("background_prepare_concurrency"),Concurrency);J->SetBoolField(TEXT("load_deadline_passed"),LoadMs>0&&LoadMs<=60000&&Complete==Entries.Num());J->SetBoolField(TEXT("parallel_loading_poses"),ParallelLoadingPoses);J->SetNumberField(TEXT("loading_pose_eval_wall_ms"),LoadingPoseEvalMs);J->SetNumberField(TEXT("loading_pose_commit_wall_ms"),LoadingPoseRefreshMs);J->SetNumberField(TEXT("loading_pose_jobs"),LoadingPoseJobs);J->SetNumberField(TEXT("loading_bone_samples"),LoadingBoneSamples);J->SetBoolField(TEXT("loading_captures_written"),LoadingCaptured);J->SetBoolField(TEXT("loading_cpu_preview"),LoadingCpuPreview);J->SetBoolField(TEXT("progressive_loading"),ProgressiveLoading);J->SetNumberField(TEXT("first_visible_ms"),FirstVisibleMs);TArray<TSharedPtr<FJsonValue>> Order;for(int32 Index:LaunchOrder)Order.Add(MakeShared<FJsonValueNumber>(Index));J->SetArrayField(TEXT("load_priority_order"),Order);J->SetNumberField(TEXT("schema"),1);J->SetNumberField(TEXT("selected"),Entries.Num());J->SetNumberField(TEXT("complete"),Complete);J->SetNumberField(TEXT("selection_to_report_ms"),(FPlatformTime::Seconds()-Started)*1000);J->SetBoolField(TEXT("gpu_ready"),Render->Ready.Load());J->SetNumberField(TEXT("gpu_passes"),Render->Passes.Load());J->SetNumberField(TEXT("cpu_pose_uploads"),Render->CpuUploads.Load());J->SetBoolField(TEXT("common_render_path"),true);J->SetNumberField(TEXT("late_source_depth_draws"),Render->LateDepthDraws.Load());J->SetNumberField(TEXT("gpu_failures"),Render->Failures.Load());J->SetNumberField(TEXT("stream_gpu_dispatches"),Render->StreamGpuDispatches.Load());J->SetNumberField(TEXT("runtime_action_slots"),OKCrowdRuntimeActionSlots);J->SetNumberField(TEXT("runtime_action_queue_limit"),OKCrowdRuntimeActionQueue);J->SetNumberField(TEXT("runtime_action_prepare_seconds"),OKCrowdRuntimeActionPrepareSeconds);J->SetNumberField(TEXT("runtime_action_active_slots"),ActiveActionSlots());J->SetNumberField(TEXT("runtime_action_queued_requests"),QueuedActionRequests());J->SetNumberField(TEXT("runtime_action_stale_drops"),StaleActionDrops);bool CookedCharacterLoaded=false;for(TObjectIterator<UKKImportedCharacter> I;I;++I)CookedCharacterLoaded|=!I->HasAnyFlags(RF_ClassDefaultObject|RF_Transient);J->SetBoolField(TEXT("cooked_character_loaded"),CookedCharacterLoaded);const auto* CachedMdc=IConsoleManager::Get().FindConsoleVariable(TEXT("r.SkeletalMesh.UseCachedMDCs"));J->SetBoolField(TEXT("cached_mesh_draw_commands_enabled"),CachedMdc&&CachedMdc->GetInt()!=0);J->SetBoolField(TEXT("visual_equivalence_verified"),false);const auto Cache=OKRuntimeTextureCacheStats();J->SetNumberField(TEXT("texture_cache_hits"),Cache.Hits);J->SetNumberField(TEXT("texture_uploads_created"),Cache.Created);J->SetNumberField(TEXT("texture_upload_bytes_avoided"),Cache.AvoidedPixelBytes);J->SetNumberField(TEXT("texture_prepared_cache_hits"),Cache.PreparedCacheHits);J->SetNumberField(TEXT("texture_prepare_decode_jobs"),Cache.PreparedDecodeJobs);J->SetNumberField(TEXT("texture_prepare_joined_jobs"),Cache.PreparedJoinedJobs);
  J->SetNumberField(TEXT("scene_skinning_matrices_checked"),Render->VerifiedMatrices.Load());if(GpuRig){auto Rig=MakeShared<FJsonObject>();Rig->SetNumberField(TEXT("checks"),Render->RigChecks.Load());Rig->SetNumberField(TEXT("rows"),Render->RigRows.Load());Rig->SetNumberField(TEXT("rows_off"),Render->RigBadRows.Load());Rig->SetNumberField(TEXT("crossfading_rows_left_out"),Render->RigBlendRows.Load());Rig->SetNumberField(TEXT("max_translation_cm"),Render->RigMaxTranslation.Load());Rig->SetNumberField(TEXT("mean_translation_cm"),Render->RigMeanTranslation.Load());Rig->SetNumberField(TEXT("max_linear"),Render->RigMaxLinear.Load());Rig->SetNumberField(TEXT("worst_row"),Render->RigWorstRow.Load());J->SetObjectField(TEXT("gpu_rig"),Rig);}J->SetNumberField(TEXT("scene_skinning_matrix_mismatches"),Render->MatrixMismatches.Load());J->SetNumberField(TEXT("scene_skinning_matrix_max_error"),Render->MatrixMaxError.Load());J->SetNumberField(TEXT("selection_to_gpu_ready_ms"),LoadMs);J->SetBoolField(TEXT("same_phase_captures_written"),Captured);J->SetStringField(TEXT("capture_error"),VisualError);
  {TArray<double> Sorted=LoadingFrames;Sorted.Sort();double Sum=0;uint64 Over=0;for(double V:Sorted){Sum+=V;Over+=V>1000./120;}J->SetBoolField(TEXT("loading_frame_measurement_includes_diagnostic_captures"),!VisualPrefix.IsEmpty());J->SetNumberField(TEXT("loading_upload_deferrals"),LoadingUploadDeferrals);J->SetNumberField(TEXT("loading_frame_samples"),Sorted.Num());J->SetNumberField(TEXT("loading_frame_mean_ms"),Sorted.IsEmpty()?0:Sum/Sorted.Num());J->SetNumberField(TEXT("loading_frame_p95_ms"),Sorted.IsEmpty()?0:Sorted[FMath::Clamp(FMath::CeilToInt(Sorted.Num()*.95)-1,0,Sorted.Num()-1)]);J->SetNumberField(TEXT("loading_frame_max_ms"),Sorted.IsEmpty()?0:Sorted.Last());J->SetNumberField(TEXT("loading_frames_over_120fps_budget"),Over);}
  TArray<TSharedPtr<FJsonValue>> TimingRows;for(int32 Mode=0;Mode<2;++Mode){const auto& T=Timings[Mode];auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("mode"),Mode?TEXT("GPU"):TEXT("CPU"));Row->SetNumberField(TEXT("samples"),T.Frame.Num());auto Add=[&](const TCHAR* Name,const TArray<double>& Values){double Sum=0;for(double X:Values)Sum+=X;TArray<double> Sorted=Values;Sorted.Sort();Row->SetNumberField(FString(Name)+TEXT("_mean_ms"),Values.IsEmpty()?0:Sum/Values.Num());Row->SetNumberField(FString(Name)+TEXT("_p95_ms"),Sorted.IsEmpty()?0:Sorted[FMath::Clamp(FMath::CeilToInt(Sorted.Num()*.95)-1,0,Sorted.Num()-1)]);};Add(TEXT("frame"),T.Frame);Add(TEXT("game_thread"),T.Game);Add(TEXT("render_thread"),T.Render);Add(TEXT("rhi_thread"),T.Rhi);Add(TEXT("gpu"),T.Gpu);TimingRows.Add(MakeShared<FJsonValueObject>(Row));}J->SetArrayField(TEXT("timings"),TimingRows);
  bool PrivateTransient=true;TArray<TSharedPtr<FJsonValue>> Rows;for(const auto& E:Entries){PrivateTransient&=E->Mesh.IsValid()&&E->Mesh->HasAnyFlags(RF_Transient)&&E->Mesh->GetOutermost()==GetTransientPackage();auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("apng"),E->Path);Row->SetStringField(TEXT("error"),E->Error);Row->SetNumberField(TEXT("stage"),E->Stage);Row->SetBoolField(TEXT("reveal_started"),E->RevealStarted);Row->SetNumberField(TEXT("reveal_start_ms"),E->RevealStartMs);Row->SetNumberField(TEXT("reveal_opacity"),E->RevealStarted?FMath::Clamp((Owner->GetWorld()->GetTimeSeconds()-E->RevealWorldStart)/3.,0.,1.):0);Row->SetNumberField(TEXT("visible_ms"),E->VisibleMs);Row->SetNumberField(TEXT("private_gpu_visible_ms"),E->PrivateGpuVisibleMs);Row->SetNumberField(TEXT("private_gpu_visible_complete"),E->PrivateGpuVisibleComplete);Row->SetNumberField(TEXT("triangles"),E->Triangles);Row->SetNumberField(TEXT("source_triangles"),E->SourceTriangles);Row->SetNumberField(TEXT("original_sections"),E->OriginalSections);Row->SetNumberField(TEXT("merged_sections"),E->MergedSections);Row->SetNumberField(TEXT("shadow_source_sections"),E->ShadowSourceSections);Row->SetNumberField(TEXT("shadow_proxy_sections"),E->ShadowProxySections);Row->SetNumberField(TEXT("shadow_excluded_sections"),E->ShadowExcludedSections);Row->SetStringField(TEXT("shadow_fallback"),E->ShadowFallback);Row->SetStringField(TEXT("shadow_reasons"),FString::Join(E->ShadowReasons,TEXT("; ")));Row->SetBoolField(TEXT("shadow_proxy_active"),E->ShadowGpu.IsValid());Row->SetStringField(TEXT("shadow_gpu_component"),E->Gpu.IsValid()?E->Gpu->GetPathName():TEXT(""));Row->SetBoolField(TEXT("shadow_casting"),E->Gpu.IsValid()&&E->Gpu->CastShadow);Row->SetBoolField(TEXT("animation_bounds_valid"),E->AnimationBounds.IsValid!=0);if(E->AnimationBounds.IsValid){Row->SetStringField(TEXT("animation_bounds_extent_cm"),E->AnimationBounds.GetExtent().ToString());Row->SetNumberField(TEXT("animation_bounds_radius_cm"),E->AnimationBounds.GetExtent().Size());}if(E->Mesh.IsValid())Row->SetNumberField(TEXT("imported_bounds_radius_cm"),E->Mesh->GetBounds().SphereRadius);Row->SetNumberField(TEXT("unique_material_bindings"),E->UniqueMaterialBindings);Row->SetStringField(TEXT("selected_walk"),ClipIds[E->WalkIndex]);Row->SetNumberField(TEXT("runtime_clip_count"),E->Clips.Num());Row->SetBoolField(TEXT("reserved_action_slot"),E->ClipSources.Num()>2&&E->ClipSources[2]==INDEX_NONE);uint64 PoseCacheBytes=0,PoseCacheHits=0;for(const auto& Clip:E->Clips){PoseCacheBytes+=Clip.PersistentCacheBytes;PoseCacheHits+=Clip.PersistentCacheHit;}Row->SetNumberField(TEXT("pose_cache_bytes"),PoseCacheBytes);Row->SetNumberField(TEXT("pose_cache_hits"),PoseCacheHits);Row->SetNumberField(TEXT("stream_requests"),E->StreamRequests);Row->SetNumberField(TEXT("stream_installs"),E->StreamInstalls);Row->SetNumberField(TEXT("stream_releases"),E->StreamReleases);Row->SetNumberField(TEXT("stream_errors"),E->StreamErrors);Row->SetNumberField(TEXT("stream_prepare_ms"),E->StreamPrepareMs);Row->SetNumberField(TEXT("action_session"),E->InstalledEpoch?E->InstalledEpoch:E->PendingEpoch);Row->SetBoolField(TEXT("action_prewarm_held"),E->StreamHold);Row->SetBoolField(TEXT("action_stream_installed"),E->StreamInstalled);Row->SetNumberField(TEXT("action_requests"),E->ActionRequests);Row->SetNumberField(TEXT("action_completions"),E->ActionCompletions);Row->SetNumberField(TEXT("speech_requests"),E->SpeechRequests);Row->SetNumberField(TEXT("speech_cancellations"),E->SpeechCancellations);Row->SetBoolField(TEXT("speech_pending"),E->SpeechJob.IsValid());Row->SetBoolField(TEXT("speech_active"),E->SpeechWave.IsValid());Row->SetNumberField(TEXT("mouth_final_opening"),E->LastMouthOpening);Row->SetNumberField(TEXT("mouth_final_gain"),E->LastMouthGain);Row->SetNumberField(TEXT("speech_rendered_frames"),E->SpeechFrames);Row->SetNumberField(TEXT("speech_peak_rms"),E->SpeechPeak);Row->SetNumberField(TEXT("mouth_morph_vertices"),E->MouthStream?E->MouthStream->AffectedVertices:0);Row->SetNumberField(TEXT("mouth_updates"),E->MouthUpdates);Row->SetNumberField(TEXT("blink_morph_vertices"),E->MouthStream?E->MouthStream->BlinkVertices:0);Row->SetNumberField(TEXT("face_upload_span_vertices"),E->MouthStream?E->MouthStream->Vertices.Num():0);Row->SetNumberField(TEXT("face_upload_ranges"),E->MouthStream?E->MouthStream->Ranges.Num():0);Row->SetNumberField(TEXT("face_expression_requests"),E->FacePlay.Requests);Row->SetNumberField(TEXT("face_expression_updates"),E->FacePlay.Updates);Row->SetNumberField(TEXT("face_expression_restores"),E->FacePlay.Restores);Row->SetNumberField(TEXT("face_transition_started"),E->FacePlay.Started);Row->SetNumberField(TEXT("face_transition_intermediate"),E->FacePlay.Intermediate);Row->SetNumberField(TEXT("face_transition_completed"),E->FacePlay.Completed);Row->SetNumberField(TEXT("face_transition_cancelled"),E->FacePlay.Cancelled);Row->SetBoolField(TEXT("face_expression_active"),E->FacePlay.Enabled);Row->SetNumberField(TEXT("blink_updates"),E->BlinkUpdates);Row->SetNumberField(TEXT("blink_peak"),E->BlinkPeak);Row->SetNumberField(TEXT("mouth_peak_opening"),E->MouthPeak);Row->SetNumberField(TEXT("action_speech_overlap_seconds"),E->ActionSpeechSeconds);Row->SetNumberField(TEXT("action_speech_gpu_overlap_seconds"),E->ActionSpeechGpuSeconds);const TCHAR* StageNames[]={TEXT("snapshot_ms"),TEXT("reduction_ms"),TEXT("render_mesh_and_pose_wait_ms"),TEXT("upload_and_commit_ms"),TEXT("mesh_commit_ms")};for(int32 I=0;I<5;++I)Row->SetNumberField(StageNames[I],E->StageMs[I]);const TCHAR* SnapshotNames[]={TEXT("native_open_ms"),TEXT("rig_capture_ms"),TEXT("mesh_capture_ms"),TEXT("material_capture_ms")};for(int32 I=0;I<4;++I)Row->SetNumberField(SnapshotNames[I],E->SnapshotMs[I]);Row->SetBoolField(TEXT("reduction_budgets_met"),E->ReducedBudget);Row->SetBoolField(TEXT("source_geometry_preserved"),E->SourceGeometryPreserved);Row->SetNumberField(TEXT("source_vertices"),E->SourceVertices);Row->SetNumberField(TEXT("source_triangles"),E->SourceTriangles);Rows.Add(MakeShared<FJsonValueObject>(Row));}J->SetBoolField(TEXT("all_character_meshes_transient"),PrivateTransient);J->SetArrayField(TEXT("characters"),Rows);{const auto CacheStats=OKRuntimeGpuDiskCacheStats();auto DiskCache=MakeShared<FJsonObject>();DiskCache->SetStringField(TEXT("directory"),CacheStats.Directory);DiskCache->SetNumberField(TEXT("lock_wait_ms"),CacheStats.LockWaitUs/1000.);DiskCache->SetNumberField(TEXT("read_ms"),CacheStats.ReadUs/1000.);DiskCache->SetNumberField(TEXT("decode_ms"),CacheStats.DecodeUs/1000.);DiskCache->SetBoolField(TEXT("parallel_decode"),!FParse::Param(FCommandLine::Get(),TEXT("RuntimeGpuCacheSerialDecode")));DiskCache->SetNumberField(TEXT("capacity_bytes"),CacheStats.CapacityBytes);DiskCache->SetNumberField(TEXT("disk_bytes"),CacheStats.DiskBytes);DiskCache->SetNumberField(TEXT("hits"),CacheStats.Hits);DiskCache->SetNumberField(TEXT("misses"),CacheStats.Misses);DiskCache->SetNumberField(TEXT("stored"),CacheStats.Stored);DiskCache->SetNumberField(TEXT("invalid"),CacheStats.Invalid);DiskCache->SetNumberField(TEXT("evicted"),CacheStats.Evicted);DiskCache->SetNumberField(TEXT("failures"),CacheStats.Failures);DiskCache->SetNumberField(TEXT("read_bytes"),CacheStats.ReadBytes);DiskCache->SetNumberField(TEXT("written_bytes"),CacheStats.WrittenBytes);J->SetObjectField(TEXT("runtime_gpu_disk_cache"),DiskCache);}J->SetNumberField(TEXT("gpu_pose_budget_bytes"),4ull*1024*1024*1024);FString Json;FJsonSerializer::Serialize(J,TJsonWriterFactory<>::Create(&Json));const bool Saved=FFileHelper::SaveStringToFile(Json,*ReportPath);ReportWritten=Saved;if(ExitAfterReport){
 // Finish the owned diagnostic trace while engine services are still alive.
 // Interactive scenes do not stop tracing; report status remains independent.
 if(FTraceAuxiliary::IsConnected()){FTraceAuxiliary::Pause();const bool TraceStopped=FTraceAuxiliary::Stop();UE_LOG(LogTemp,Display,TEXT("APNG diagnostic trace stopped before shutdown: %d"),int32(TraceStopped));}
 FPlatformMisc::RequestExitWithStatus(false,Saved&&Complete==Entries.Num()&&Render->Ready.Load()&&Render->Failures.Load()==0&&VisualError.IsEmpty()?0:1);}
 }
 bool Tick(float Dt){
  if(Stopped||!Owner.IsValid()||Owner->IsActorBeingDestroyed()){Shutdown();return false;}
  if(Suspended)return true;
  SceneTime+=Dt;
  const double Now=FPlatformTime::Seconds();if(!ReadyAt&&LoadingLastWall>0)LoadingFrames.Add((Now-LoadingLastWall)*1000);LoadingLastWall=Now;if(!ReadyAt&&Render->Ready.Load()){ReadyAt=Now;LoadMs=(Now-Started)*1000;}
  if(ReadyAt&&ProfileSeconds>0)RequestedGpu=Now-ReadyAt>=5+ProfileSeconds;
  if(ReadyAt&&ProfileSeconds>0){const double Elapsed=Now-ReadyAt;const bool CpuWindow=Elapsed>=5&&Elapsed<5+ProfileSeconds,GpuWindow=Elapsed>=10+ProfileSeconds&&Elapsed<10+2*ProfileSeconds;if((CpuWindow||GpuWindow)&&LastWall>0){
TRACE_CPUPROFILER_EVENT_SCOPE_CONDITIONAL(CrowdGpuProfileAnchor,GpuWindow);
TRACE_CPUPROFILER_EVENT_SCOPE_CONDITIONAL(CrowdCpuProfileAnchor,CpuWindow);
auto& T=Timings[GpuWindow?1:0];T.Frame.Add((Now-LastWall)*1000);T.Game.Add(FPlatformTime::ToMilliseconds(GGameThreadTime));T.Render.Add(FPlatformTime::ToMilliseconds(GRenderThreadTime));T.Rhi.Add(FPlatformTime::ToMilliseconds(GRHIThreadTime));T.Gpu.Add(FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles()));}}LastWall=Now;
  while(Active<Concurrency&&Next<Entries.Num()){const int32 Index=NearestStage(0);if(Index==INDEX_NONE)break;LaunchOrder.Add(Index);auto& E=*Entries[Index];E.Behavior.Initialize(Index);E.StageStarted=FPlatformTime::Seconds();E.Snapshot=OKOpenRuntimeCharacterSnapshotAsync(E.Path,E.Outfit,Curves[E.WalkIndex],256ull*1024*1024,CaptureFacePatterns);E.Stage=1;++Next;++Active;}
 {QUICK_SCOPE_CYCLE_COUNTER(STAT_OKCrowd_Prepare);  for(auto& E:Entries)Prepare(*E);
 }
  const double LastGameMs=FPlatformTime::ToMilliseconds(GGameThreadTime);
  const double SubmitBudgetMs=FMath::Clamp(1000./120-LastGameMs-1.,.1,.75);
  const double CommitDeadline=FPlatformTime::Seconds()+SubmitBudgetMs/1000.;
  const bool CanSubmit=!LoadingUploadPending||LoadingUploadFence.IsFenceComplete();bool Submitted=false;
  if(!CanSubmit)++LoadingUploadDeferrals;
  while(CanSubmit){const int32 Index=NearestStage(4);if(Index==INDEX_NONE)break;auto& E=Entries[Index];FString Error;while(E->TextureCursor<E->Material.Textures.Num()&&FPlatformTime::Seconds()<CommitDeadline){Submitted=true;auto& Prepared=E->Material.Textures[E->TextureCursor++];auto* T=OKCreateRuntimePreparedTexture(Prepared,Error);if(!T){Fail(*E,Error);break;}E->Textures.Emplace(T);if(!VisualPrefix.IsEmpty()&&!Prepared.Mips.IsEmpty()){const auto& M=Prepared.Mips[0];const FString Path=ReportPath+TEXT(".texture.")+Prepared.SourceHash+TEXT(".png");FImageUtils::SaveImageByExtension(*Path,FImageView(reinterpret_cast<const FColor*>(M.BGRA.GetData()),M.Width,M.Height));}Prepared.Mips.Reset();}
   if(E->Stage==4&&E->TextureCursor==E->Material.Textures.Num()&&FPlatformTime::Seconds()<CommitDeadline){
    if(!E->CommitData.IsValid())E->CommitData=MakeUnique<FKKRuntimeRenderMeshData>(E->RenderMesh.Consume());
    auto& D=*E->CommitData;const double Begin=FPlatformTime::Seconds();Submitted=true;
    const bool Passed=D.Error.IsEmpty()&&Commit(*E,D,Error);E->CommitWorkMs+=(FPlatformTime::Seconds()-Begin)*1000;
    if(!Passed){Fail(*E,D.Error.IsEmpty()?Error:D.Error);continue;}
    if(E->CommitReady){E->StageMs[3]=(FPlatformTime::Seconds()-E->StageStarted)*1000;E->StageMs[4]=E->CommitWorkMs;E->CommitData.Reset();E->Stage=6;}
    break; // One private material/mesh batch per frame; no second actor batch.
   }
   if(FPlatformTime::Seconds()>=CommitDeadline)break;
  }
  if(Submitted){LoadingUploadFence.BeginFence(FRenderCommandFence::ESyncDepth::RHIThread);LoadingUploadPending=true;}
  for(auto& E:Entries)if(E->Stage==6&&E->PackedBank.IsReady()&&E->RevealQueued&&E->RevealFence.IsFenceComplete()){auto Prepared=E->PackedBank.Consume();E->Bank=MoveTemp(Prepared.Validated);if(!E->Bank){Fail(*E,Prepared.Error.IsEmpty()?TEXT("Background private GPU bank packing failed"):Prepared.Error);continue;}E->AnimationBounds=Prepared.Bounds;E->CpuKeys=MoveTemp(Prepared.CpuKeys);for(int32 I=0;I<E->Clips.Num();++I)E->Clips[I].Tracks=MoveTemp(Prepared.CpuTracks[I]);E->Stage=5;--Active;++Complete;ShowLoadingCharacter(*E);}
  // One character that failed to load must not hold back the others (R3-11): once every entry has either loaded or
  // failed, the loaded ones are composed and installed; a failed one keeps no actor and refuses actions.
  int32 Failed=0;for(const auto& E:Entries)Failed+=E->Stage==-1;
  if(AggregateGpu&&Complete>0&&Complete+Failed==Entries.Num()&&!Installed&&!Composition.IsValid()&&!PendingComposition.IsValid()){
   auto Banks=MakeShared<TArray<TSharedPtr<const FOKValidatedGpuBank,ESPMode::ThreadSafe>>,ESPMode::ThreadSafe>();
   for(auto& E:Entries){E->BankActor=E->Stage==5&&E->Bank?Banks->Num():INDEX_NONE;if(E->BankActor!=INDEX_NONE)Banks->Add(E->Bank);}
   if(Failed)UE_LOG(LogTemp,Warning,TEXT("APNG installing %d of %d characters; %d failed to load"),Banks->Num(),Entries.Num(),Failed);Composition=Async(EAsyncExecution::ThreadPool,[Banks]{auto Result=MakeUnique<FOKGpuAnimationBundle>();TArray<const FOKGpuAnimationCacheData*> Data;TArray<const FOKGpuAnimationManifest*> Metadata;FString Error;for(const auto& B:*Banks){UE_LOG(LogTemp,Display,TEXT("APNG packed private bank %d: key bytes=%llu bones=%d"),Data.Num(),uint64(B->Get().Data.Pose.Keys.Num())*16,B->Get().Data.Pose.Tasks.Num());Data.Add(&B->Get().Data);Metadata.Add(&B->Get().Manifest);}if(!OKComposeGpuAnimationCaches(Data,Metadata,Result->Data,Result->Manifest,Error)){UE_LOG(LogTemp,Error,TEXT("APNG composition: %s"),*Error);return TSharedPtr<const FOKValidatedGpuBank,ESPMode::ThreadSafe>{};}auto Verified=FOKValidatedGpuBank::Create(MoveTemp(Result),Error);if(!Verified)UE_LOG(LogTemp,Error,TEXT("APNG immutable GPU admission: %s"),*Error);return Verified;});
  }
  if(!Installed&&Composition.IsValid()&&Composition.IsReady()){
   PendingComposition=Composition.Consume();if(!PendingComposition){VisualError=TEXT("Immutable GPU bank validation failed");Stopped=true;WriteReport();return false;}
  }
  bool PrivateUploadsPending=false;{QUICK_SCOPE_CYCLE_COUNTER(STAT_OKCrowd_ProgressiveGpu);PrivateUploadsPending=ServiceProgressiveGpu();}
  if(!Installed&&PendingComposition.IsValid()&&!PrivateUploadsPending){
   const int32 Status=Render->UploadStatus.Load();
   if(Status<0){VisualError=TEXT("Incremental GPU bank upload failed");Stopped=true;WriteReport();return false;}
   if(Status==2)Install();
   else if(Status==0){Render->UploadStatus.Store(1);ENQUEUE_RENDER_COMMAND(UploadApngBankChunk)([R=Render,Bank=PendingComposition](FRHICommandListImmediate& Cmd){
    FRDGBuilder G(Cmd);FString Error;bool Done=false;R->Budget->Collect();
    const bool Passed=R->Pose.UploadManagedChunk(G,Bank.ToSharedRef(),R->Budget,4ull*1024*1024,Done,Error);G.Execute();++R->UploadChunks;
    if(!Passed){R->Fail(Error);R->UploadStatus.Store(-1);}else R->UploadStatus.Store(Done?2:0);
   });}
  }
  if(Camera.IsValid()&&!ExternalControl)UpdateDisplayLighting(-Camera->GetActorForwardVector());
  UpdateFaceFlatten();
 {QUICK_SCOPE_CYCLE_COUNTER(STAT_OKCrowd_ShadowSubmission);  ServiceShadowSubmission();
 }
 {QUICK_SCOPE_CYCLE_COUNTER(STAT_OKCrowd_Stream);  ServiceStream();
 }
  auto* Pc=Owner->GetWorld()->GetFirstPlayerController();if(Pc&&Pc->WasInputKeyJustPressed(EKeys::F9)){RequestedGpu=!RequestedGpu;UE_LOG(LogTemp,Display,TEXT("APNG CPU/GPU requested=%s"),RequestedGpu?TEXT("GPU"):TEXT("CPU"));}
  if(Pc&&Installed&&!ExternalControl){const FKey Keys[]={EKeys::One,EKeys::Two,EKeys::Three,EKeys::Four,EKeys::Five,EKeys::Six,EKeys::Seven,EKeys::Eight};
   for(int32 I=0;I<FMath::Min(Curves.Num()-WalkSources,int32(UE_ARRAY_COUNT(Keys)));++I)if(Pc->WasInputKeyJustPressed(Keys[I])){FString ActionError;if(!OKPlayCrowdRuntimeAction(Owner.Get(),0,ClipIds[WalkSources+I],ActionError))UE_LOG(LogTemp,Warning,TEXT("Runtime action: %s"),*ActionError);}
   if(Pc->WasInputKeyJustPressed(EKeys::BackSpace)){FString ActionError;OKResumeCrowdRuntimeBehavior(Owner.Get(),0,ActionError);}
  }
  if(ReadyAt&&!DemoActionStarted&&!DemoAction.IsEmpty()&&(!DemoSpeech||VoiceIds.IsEmpty())&&Now-ReadyAt>DemoDelay){FString ActionError;if(!OKPlayCrowdRuntimeAction(Owner.Get(),DemoCharacter,DemoAction,ActionError))UE_LOG(LogTemp,Warning,TEXT("Runtime action: %s"),*ActionError);DemoActionStarted=true;}
  if(ReadyAt&&!DemoSpeechStarted&&DemoSpeech&&Now-ReadyAt>DemoDelay&&!VoiceIds.IsEmpty()){FString SpeechError;if(!OKSpeakCrowdRuntimeActionLine(Owner.Get(),DemoCharacter,VoiceIds[0],DemoAction,SpeechError))UE_LOG(LogTemp,Warning,TEXT("Runtime voice: %s"),*SpeechError);DemoSpeechStarted=true;}
  if(Pc&&Installed&&!ExternalControl&&Pc->WasInputKeyJustPressed(EKeys::T)&&!VoiceIds.IsEmpty()){FString SpeechError;if(!OKSpeakCrowdRuntimeActionLine(Owner.Get(),0,VoiceIds[Entries[0]->SpeechRequests%VoiceIds.Num()],FString(),SpeechError))UE_LOG(LogTemp,Warning,TEXT("Runtime voice: %s"),*SpeechError);}
  if(Pc&&Installed&&!ExternalControl&&Pc->WasInputKeyJustPressed(EKeys::Y)&&!VoiceIds.IsEmpty()&&Entries.Num()>1){
   int32 Nearest=INDEX_NONE;double Distance=TNumericLimits<double>::Max();
   for(int32 A=1;A<Entries.Num();++A)if(Entries[A]->Stage==5){const double D=FVector::DistSquared(Entries[A]->Transform.GetTranslation(),Entries[0]->Transform.GetTranslation());if(D<Distance){Distance=D;Nearest=A;}}
   if(Nearest!=INDEX_NONE){FString SpeechError;if(!OKSpeakCrowdRuntimeActionLine(Owner.Get(),Nearest,VoiceIds[Entries[Nearest]->SpeechRequests%VoiceIds.Num()],FString(),SpeechError))UE_LOG(LogTemp,Warning,TEXT("Runtime NPC voice: %s"),*SpeechError);}
  }
  FVector InputMove=FVector::ZeroVector;if(Pc&&!ExternalControl){float X=0,Y=0;Pc->GetInputMouseDelta(X,Y);if(Pc->IsInputKeyDown(EKeys::RightMouseButton)){CameraYaw+=X*.2;CameraPitch=FMath::Clamp(CameraPitch-Y*.2,-65.f,50.f);}const FRotationMatrix Axes(FRotator(0,CameraYaw,0));InputMove=(Axes.GetUnitAxis(EAxis::X)*float(Pc->IsInputKeyDown(EKeys::W)-Pc->IsInputKeyDown(EKeys::S))+Axes.GetUnitAxis(EAxis::Y)*float(Pc->IsInputKeyDown(EKeys::D)-Pc->IsInputKeyDown(EKeys::A))).GetClampedToMaxSize(1);}
  if(!ExternalControl&&(ProgressiveLoading||Installed)){PlayerPosition+=InputMove*Dt*90;PlayerPosition.X=FMath::Clamp(PlayerPosition.X,-1050.,1050.);PlayerPosition.Y=FMath::Clamp(PlayerPosition.Y,-1050.,1050.);}
  // Warm GPU visibility is required to receive its first provider invocation.
  const bool Warm=Installed&&!Render->Ready.Load()&&Render->Failures.Load()==0;
  GpuMode=Installed&&RequestedGpu&&Render->Ready.Load()&&Render->Failures.Load()==0;
  TArray<FOKGpuPosePlayback> Play;int32 OutlineToggles=0;const float OutlineRange=CVarOKCrowdOutlineRange.GetValueOnGameThread();
  struct FLoadingPoseJob {FEntry* Entry;FOKGpuPosePlayback Playback;int64 Bucket;TArray<FTransform> Poses;};
  TArray<FLoadingPoseJob> LoadingJobs;
  for(int32 A=0;A<Entries.Num();++A){QUICK_SCOPE_CYCLE_COUNTER(STAT_OKCrowd_EntryFrame);const double EntryT0=FPlatformTime::Seconds();double EntryT1=EntryT0,EntryT2=EntryT0,EntryT3=EntryT0;auto& E=*Entries[A];if(E.Stage!=5||(!Installed&&!ProgressiveLoading))continue;if(CaptureStage>=1&&CaptureStage<=2){const int32 Slot=FMath::Min(VisualClip,E.Clips.Num()-1);FOKGpuPosePlayback Frozen;Frozen.ClipA=0;Frozen.ClipB=Slot;Frozen.Times=FVector4f(VisualPhase*E.Clips[0].Duration,VisualPhase*E.Clips[Slot].Duration,VisualBlend,0);Play.Add(Frozen);continue;}FString Error;
   if(E.SpeechJob.IsValid()&&!E.PendingSpeechAction.IsEmpty()&&E.LinkedActionEpoch!=0&&E.LinkedActionEpoch!=E.PendingEpoch&&E.LinkedActionEpoch!=E.InstalledEpoch){E.CancelSpeech();UE_LOG(LogTemp,Display,TEXT("Runtime voice dropped cancelled action session"));}
   if(E.SpeechJob.IsValid()&&Now-E.SpeechRequestedAt>OKCrowdRuntimeActionPrepareSeconds){E.CancelSpeech();UE_LOG(LogTemp,Warning,TEXT("Runtime speech preparation exceeded 60 seconds; canceled pending audio"));}
   if(A==DemoCharacter&&DemoSpeechStarted&&!DemoSpeechCancelled&&DemoCancelAfter>=0&&Now-E.SpeechRequestedAt>=DemoCancelAfter){FString StopError;OKStopCrowdRuntimeSpeech(Owner.Get(),A,StopError);DemoSpeechCancelled=true;}
   if(E.SpeechJob.IsValid()&&E.SpeechEpoch!=0&&E.SpeechJob.IsReady()&&(E.PendingSpeechAction.IsEmpty()||E.ClipSources.Find(FindClip(E.PendingSpeechAction))>=2)){const uint64 SpeechEpoch=E.SpeechEpoch;auto Prepared=E.SpeechJob.Consume();if(E.SpeechEpoch!=SpeechEpoch){UE_LOG(LogTemp,Display,TEXT("Runtime voice dropped stale session"));}else if(!Prepared.Error.IsEmpty()){UE_LOG(LogTemp,Warning,TEXT("Runtime voice: %s"),*Prepared.Error);FString StopError;OKStopCrowdRuntimeSpeech(Owner.Get(),A,StopError);E.SpeechState=EOKRuntimeSpeechState::Failed;E.SpeechError=Prepared.Error;}else if(auto Device=Owner->GetWorld()->GetAudioDevice();Device.IsValid()){
    auto* Audio=E.SpeechAudio.Get();if(!Audio){Audio=NewObject<UAudioComponent>(Owner.Get());Owner->AddInstanceComponent(Audio);Audio->bAutoActivate=false;Audio->bAutoDestroy=false;Audio->SetWorldLocation(E.Transform.TransformPosition(FVector(0,0,150)));Audio->bAllowSpatialization=true;Audio->bOverrideAttenuation=true;Audio->AttenuationOverrides.bAttenuate=true;Audio->AttenuationOverrides.bSpatialize=true;Audio->AttenuationOverrides.FalloffDistance=2000;Audio->RegisterComponent();E.SpeechAudio=Audio;}
    // On the phone: heard in full wherever she is (a call across the map was a whisper 20 m off). Set before each line.
    {const bool Here=A!=RemoteSpeech;Audio->bAllowSpatialization=Here;Audio->AttenuationOverrides.bAttenuate=Here;Audio->AttenuationOverrides.bSpatialize=Here;
     if(!Here)UE_LOG(LogTemp,Display,TEXT("Runtime voice: actor %d speaks on the phone (not spatialized)"),A);}
    QUICK_SCOPE_CYCLE_COUNTER(STAT_OKCrowd_SpeechBegin);
    Audio->Stop();auto* Wave=NewObject<UKKRuntimeSpeechWave>(Owner.Get());if(Prepared.Stream)Wave->InitializeStream(Prepared.Stream,Device->GetSampleRate());else Wave->Initialize(Prepared.Pcm,Device->GetSampleRate());E.SpeechWave.Reset(Wave);E.LinePeak=0;E.LineMouth=0;E.EnvelopeRate=float(Device->GetSampleRate());E.LastSpeechFrames=0;E.SpeechProgressAt=Now;Audio->SetSound(Wave);
    if(!E.PendingSpeechAction.IsEmpty()){QUICK_SCOPE_CYCLE_COUNTER(STAT_OKCrowd_SpeechAction);FString ActionError;if(!OKPlayCrowdRuntimeAction(Owner.Get(),A,E.PendingSpeechAction,ActionError))UE_LOG(LogTemp,Warning,TEXT("Runtime speech action: %s"),*ActionError);}E.PendingSpeechAction.Reset();Audio->SetWorldLocation(E.Transform.TransformPosition(FVector(0,0,150)));Audio->SetPitchMultiplier(E.MouthEnvelope.Num()?1.f:E.VoicePitch);
    {QUICK_SCOPE_CYCLE_COUNTER(STAT_OKCrowd_SpeechPlay);Audio->Play();}E.SpeechState=EOKRuntimeSpeechState::Playing;
   }else {FString StopError;OKStopCrowdRuntimeSpeech(Owner.Get(),A,StopError);E.SpeechState=EOKRuntimeSpeechState::Failed;E.SpeechError=TEXT("Runtime voice has no audio device");UE_LOG(LogTemp,Warning,TEXT("Runtime voice has no audio device"));}}
   // Instanced skinned mesh component transforms do not follow their actor instances.
   if(E.SpeechAudio.IsValid())E.SpeechAudio->SetWorldLocation(E.Transform.TransformPosition(FVector(0,0,150)));
   if(E.SpeechWave.IsValid()){const float Rms=E.SpeechWave->Meter->Rms.Load();
   const int32 EnvelopeAt=E.MouthEnvelope.Num()?FMath::Clamp(int32(double(E.SpeechWave->Meter->Frames.Load())/FMath::Max(1.f,E.EnvelopeRate)/E.EnvelopeHop),0,E.MouthEnvelope.Num()-1):INDEX_NONE;
   // Speech: the level against this line's own loudest so far (a quiet voice opened the mouth barely, Rms x 3), silence
   // under a small floor closed.
   E.LinePeak=FMath::Max(E.LinePeak,Rms);const float Level=CVarOKMouthNormalize.GetValueOnGameThread()?(Rms<.008f?0.f:FMath::Clamp(Rms/FMath::Max(E.LinePeak*.7f,.03f),0.f,1.f)*FMath::Clamp(CVarOKMouthMaxOpen.GetValueOnGameThread(),0.f,1.f)):FMath::Clamp(Rms*3.f,0.f,1.f);
   E.SpeechMouth=EnvelopeAt!=INDEX_NONE?FMath::FInterpTo(E.SpeechMouth,FMath::Clamp(E.MouthEnvelope[EnvelopeAt],0.f,1.f),Dt,30.f):FMath::FInterpTo(E.SpeechMouth,Level,Dt,22.f);E.LineMouth=FMath::Max(E.LineMouth,E.SpeechMouth);E.SpeechPeak=FMath::Max(E.SpeechPeak,Rms);const uint64 Frames=E.SpeechWave->Meter->Frames.Load();if(Frames!=E.LastSpeechFrames)E.SpeechProgressAt=Now;E.SpeechFrames+=Frames>=E.LastSpeechFrames?Frames-E.LastSpeechFrames:0;E.LastSpeechFrames=Frames;
   // A voice whose playback stops advancing (e.g. the output device was swapped under it) would stay "speaking" forever;
   // a stream still waiting on TTS gets longer before it counts as stuck.
   const bool Starving=E.SpeechWave->Stream&&!E.SpeechWave->Stream->IsComplete();const bool Stalled=Now-E.SpeechProgressAt>(Starving?15.:3.);
   if(Stalled)UE_LOG(LogTemp,Warning,TEXT("Runtime voice stopped advancing for %.1fs; treating it as finished (actor=%d)"),Now-E.SpeechProgressAt,A);
   if(Stalled&&E.SpeechAudio.IsValid())E.SpeechAudio->Stop();
   if(Stalled||(Frames&&E.SpeechAudio.IsValid()&&!E.SpeechAudio->IsPlaying())){E.SpeechAudio->SetSound(nullptr);E.SpeechWave.Reset();E.SpeechMouth=0;E.SpeechState=EOKRuntimeSpeechState::Completed;
    UE_LOG(LogTemp,Display,TEXT("Runtime mouth: actor=%d line ended (loudest rms %.3f, widest opening %.2f); now %s (expression mouth %d open %.2f)"),A,E.LinePeak,E.LineMouth,E.FacePlay.Enabled&&E.FacePlay.Face.MouthPattern>=0&&E.FacePlay.Face.MouthOpening>=0.f?TEXT("the expression's mouth"):TEXT("closed"),E.FacePlay.Enabled?E.FacePlay.Face.MouthPattern:-1,E.FacePlay.Enabled?E.FacePlay.Face.MouthOpening:-1.f);}}
   // The mouth is always driven. Speaking: it opens with the voice (the expression keeps its mouth shape, not its
   // opening: a small mouth_open kept her lips nearly still). Silent: closed, unless her expression opens it
   // (mouth_open). Undriven (gain 0) it stayed in the card's own mouth, often open, after every line.
   const bool Speaking=E.SpeechWave.IsValid();
   const bool ExpressionOpens=E.FacePlay.Enabled&&E.FacePlay.Face.MouthPattern>=0&&E.FacePlay.Face.MouthOpening>=0.f;
   const bool Driven=CVarOKMouthAlwaysDriven.GetValueOnGameThread()!=0;
   // Silent with an expression that opens it: its mouth_open scaled by the same ceiling as speech (MaxOpen): at full
   // weight the open-mouth morph is a white blob, the expression's mouth_open 1.0 included.
   const float MaxOpen=FMath::Clamp(CVarOKMouthMaxOpen.GetValueOnGameThread(),0.f,1.f);
   const float MouthOpening=VisualMouth>=0?VisualMouth:Speaking?E.SpeechMouth:ExpressionOpens?MaxOpen:0.f,MouthGain=Driven||VisualMouth>=0||Speaking?1.f:0.f;
   FKKRuntimeFaceState SpokenFace=E.FacePlay.Face;FKKRuntimeFaceTransition SpokenTransition=E.FacePlay.Transition;
   if(Speaking&&VisualMouth<0&&CVarOKMouthSpeakFull.GetValueOnGameThread()){SpokenFace.MouthOpening=-1.f;SpokenTransition.Channel[0].Opening=-1.f;}
   float Blink=VisualBlink>=0?VisualBlink:FMath::Max(0.f,E.BlinkOverride);
   if(VisualBlink<0&&E.BlinkOverride<0&&AutoBlink&&(!E.FacePlay.Enabled||E.FacePlay.Blink)){const double T=FMath::Fmod(SceneTime+A*.77,3.2+(A%7)*.29);Blink=T<.08?T/.08:(T<.20?(.20-T)/.12:0);}
   if(!E.MouthStream||!E.MouthStream->BlinkVertices||(E.FacePlay.Enabled&&!E.FacePlay.Blink))Blink=0;
   const int64 MouthBucket=int64(SceneTime*60);if(E.SpeechWave.IsValid()&&E.ActionOverride>=2){E.ActionSpeechSeconds+=Dt;if(GpuMode)E.ActionSpeechGpuSeconds+=Dt;}
   EntryT1=FPlatformTime::Seconds();{QUICK_SCOPE_CYCLE_COUNTER(STAT_OKCrowd_EntryFaceLook);TickFaceLook(E,float(Dt));}EntryT2=FPlatformTime::Seconds();
   if(E.MouthStream){FOKRuntimeFaceUploadPlan Plan;FString FaceError;const bool MouthMoved=FMath::Abs(MouthOpening-E.LastMouthOpening)>.002f,BlinkMoved=FMath::Abs(Blink-E.LastBlinkClosure)>.002f,GainChanged=MouthGain!=E.LastMouthGain;
    if(!OKPlanRuntimeFaceUpload(E.FacePlay,Dt,MouthBucket,E.LastMouthBucket,MouthMoved,BlinkMoved,GainChanged,Plan,FaceError)){const bool Gate=E.FacePlay.Dirty||MouthBucket!=E.LastMouthBucket||GainChanged,Need=E.FacePlay.Dirty||MouthMoved||BlinkMoved||GainChanged;Plan={};Plan.Upload=Gate&&Need;}
    if((Installed||E.VisibleMs>0)&&(Plan.Upload||E.ShadowFaceDirty)){
     if(!OKUpdateRuntimeMouthStream(E.Mesh.Get(),E.DepthMesh.Get(),E.MouthStream,MouthOpening,MouthGain,Error,Blink,E.FacePlay.Enabled?&SpokenFace:nullptr,E.FacePlay.Enabled?&SpokenTransition:nullptr)){Fail(E,Error);continue;}
     if(E.ShadowMouth&&E.ShadowMesh.IsValid()){
      const auto ShadowFace=OKRuntimeShadowFaceChannels(*E.ShadowMouth,E.FacePlay.Face);
      const auto* ShadowTransition=E.FacePlay.Enabled&&E.FacePlay.Transition.Active()?&E.FacePlay.Transition:nullptr;
      if(!OKUpdateRuntimeMouthStream(E.ShadowMesh.Get(),nullptr,E.ShadowMouth,MouthOpening,MouthGain,Error,Blink,E.FacePlay.Enabled?&ShadowFace:nullptr,ShadowTransition)){Fail(E,Error);continue;}
     }
     E.ShadowFaceDirty=false;
     OKNoteRuntimeFaceUpload(E.FacePlay,Plan);if(MouthMoved||GainChanged)++E.MouthUpdates;if(BlinkMoved)++E.BlinkUpdates;E.LastBlinkClosure=Blink;E.BlinkPeak=FMath::Max(E.BlinkPeak,Blink);E.LastMouthOpening=MouthOpening;E.LastMouthGain=MouthGain;E.LastMouthBucket=MouthBucket;E.MouthPeak=FMath::Max(E.MouthPeak,MouthOpening);
    }
   }
   if(ExternalControl)E.Behavior.Speed=E.ExternalSpeed;else if(A==0)E.Behavior.Speed=InputMove.Size();else if(E.ActionOverride==INDEX_NONE){const bool Changed=E.Behavior.Step(Dt);if(Installed&&Changed&&E.Behavior.State==FOKCrowdBehavior::EState::Look&&E.Clips.Num()>2&&E.PendingAction==INDEX_NONE&&!ActionSlotBusy(A)&&(AutoActionStreaming||E.ClipSources[2]!=INDEX_NONE)){const int32 Source=AutoActionStreaming?WalkSources+(A+E.Behavior.Transitions)%(Curves.Num()-WalkSources):E.ClipSources[2];if(!ClipLoops[Source]){FString ActionError;OKPlayCrowdRuntimeAction(Owner.Get(),A,ClipIds[Source],ActionError);}}}const int32 Clip=E.ActionOverride!=INDEX_NONE?E.ActionOverride:(E.Behavior.Speed<.001?(E.ConversationIdle&&E.ConversationSlot!=INDEX_NONE?E.ConversationSlot:E.IdleVariantSlots.IsValidIndex(E.IdleVariant)?E.IdleVariantSlots[E.IdleVariant]:!E.ReadingAllowed&&E.StandSlot!=INDEX_NONE?E.StandSlot:0):(E.Running&&E.RunSlot!=INDEX_NONE?E.RunSlot:1));if(Clip!=E.LastClip){E.Clock.RequestClip(Clip,.3,0,Error);E.LastClip=Clip;}E.Clock.Advance(Dt,Error);auto P=E.Clock.Parameters();if(E.ActionOverride!=INDEX_NONE&&!ClipLoops[E.ClipSources[E.ActionOverride]]&&!E.Clock.IsTransitioning()&&P.ClipA==uint32(E.ActionOverride)&&P.Times.X>=E.Clips[E.ActionOverride].Duration-1e-5f){E.ActionOverride=INDEX_NONE;++E.ActionCompletions;}P.Times.X=FMath::FloorToFloat(P.Times.X*60)/60;P.Times.Y=FMath::FloorToFloat(P.Times.Y*60)/60;ApplyHeadLook(E,P,Dt);Play.Add(P);
   const int32 Columns=FMath::CeilToInt(FMath::Sqrt(float(Entries.Num()))),Rows=FMath::DivideAndRoundUp(Entries.Num(),Columns);const double Phase=E.Behavior.RouteTime*.8+A*.47;const FVector Pos((A%Columns-(Columns-1)*.5)*210+FMath::Sin(Phase)*75,(A/Columns-(Rows-1)*.5)*210+FMath::Cos(Phase)*75,0);E.Transform=FTransform(FRotator(0,-90-FMath::RadiansToDegrees(Phase)+E.Behavior.LookYaw(),0),Pos);
   if(A==0&&!ExternalControl){if(!PlayerPositionInitialized){PlayerPosition=Pos;PlayerPositionInitialized=true;}const FRotator Rotation=InputMove.IsNearlyZero()?E.Cpu->GetComponentRotation():FRotator(0,FMath::RadiansToDegrees(FMath::Atan2(InputMove.Y,InputMove.X))-90,0);E.Transform=FTransform(Rotation,PlayerPosition);}
   if(ExternalControl){E.Transform=E.ExternalTransform;if(A==0)PlayerPosition=E.Transform.GetTranslation();}
   if(A==0&&LoadingCaptured&&!VisualPrefix.IsEmpty())E.Transform=LoadingCaptureTransform;
   EntryT3=FPlatformTime::Seconds();QUICK_SCOPE_CYCLE_COUNTER(STAT_OKCrowd_EntryTransforms);
   const FVector ShadowCamera=ExternalControl&&Pc&&Pc->PlayerCameraManager?Pc->PlayerCameraManager->GetCameraLocation():(Camera.IsValid()?Camera->GetActorLocation():PlayerPosition);
   if(A==0&&ForegroundCapture.IsValid()){
    if(Pc&&Pc->PlayerCameraManager)UpdateForegroundCapture(Pc->PlayerCameraManager->GetCameraLocation(),Pc->PlayerCameraManager->GetCameraRotation(),Pc->PlayerCameraManager->GetFOVAngle());
    else if(Camera.IsValid()&&Camera->GetCameraComponent())UpdateForegroundCapture(Camera->GetActorLocation(),Camera->GetActorRotation(),Camera->GetCameraComponent()->FieldOfView);}
   // Near and on the camera's floor: a girl in her room a storey up or down casts nothing anyone sees through the
   // floor slab, yet her shadow views cost as much as hers in front of him (the dorm in the evening, 103 FPS).
   const bool NearShadow=FVector::DistSquared(ShadowCamera,E.Transform.GetTranslation())<FMath::Square(1600.)&&(A==0||FMath::Abs(E.Transform.GetTranslation().Z-PlayerPosition.Z)<200.);
   if(auto* C=E.Cpu.Get())C->SetCastShadow(false);if(auto* G=E.Gpu.Get())G->SetCastShadow(E.ShadowGpu.IsValid()?false:NearShadow);if(auto* S=E.ShadowGpu.Get())S->SetCastShadow(NearShadow);
   const bool EntryGpu=GpuMode||(!Installed&&E.LoadingGpuInstalled&&RequestedGpu);
   if(Installed||E.VisibleMs>0)if(auto* C=E.Cpu.Get()){C->SetVisibility(true);if(!EntryGpu)OKCrowdSetTransform(C,E.Transform);const int64 Bucket=int64(SceneTime*60);if(!EntryGpu&&(!Warm||!RequestedGpu||!VisualPrefix.IsEmpty())&&(E.LastCpuBucket!=Bucket||Warm)){if(!Installed){auto& Job=LoadingJobs.AddDefaulted_GetRef();Job.Entry=&E;Job.Playback=P;Job.Bucket=Bucket;Job.Poses.SetNumUninitialized(E.Bones.Num());}else {for(int32 B=0;B<E.Bones.Num();++B){auto T=SampleLocal(E.Clips[P.ClipA],*E.CpuKeys,B,P.Times.X);if(P.ClipA!=P.ClipB){const auto Other=SampleLocal(E.Clips[P.ClipB],*E.CpuKeys,B,P.Times.Y);T=OKGpuBlendLocal(T,Other,P.Times.Z,E.Clips[P.ClipA].Tracks[B]);}C->BoneSpaceTransforms[B]=T;}if(E.Turned){TurnLocal(E,E.LookNeck,C->BoneSpaceTransforms[E.LookNeck]);TurnLocal(E,E.LookHead,C->BoneSpaceTransforms[E.LookHead]);}C->MarkRefreshTransformDirty();C->RefreshBoneTransforms();E.LastCpuBucket=Bucket;}}}
   if(!Installed&&!EntryGpu&&ProgressiveLoading&&E.VisibleMs>0){auto* Main=E.LoadingMain.IsValid()?E.LoadingMain.Get():E.Cpu.Get();OKCrowdSetTransform(Main,E.Transform);Main->SetCastShadow(NearShadow);Main->SetVisibility(true);}
   // Her eyes through her front hair need these only up close (OKForegroundRange): farther off the detail is a few
   // pixels, and each one more girl drawn into the custom depth and the capture every frame (the dorm lounge, eight
   // girls, 56 FPS).
   const bool Foreground=FVector::DistSquared(ShadowCamera,E.Transform.GetTranslation())<FMath::Square(OKForegroundRange);
   if(auto* F=E.FrontGpu.Get()){OKCrowdSetTransform(F,E.Transform);F->SetVisibility(Foreground);}
   if(auto* C=E.OccluderGpu.Get()){OKCrowdSetTransform(C,E.Transform);C->SetVisibility(Foreground);}
   // Outline LOD (a little hysteresis; at most four characters switch a frame: each change rebuilds her render proxy).
   if(auto* G=E.Gpu.Get();G&&!E.OutlineSlotIds.IsEmpty()){if(E.OutlineComponent.Get()!=G){E.OutlineComponent=G;E.OutlinesHidden=false;}
    const double D2=FVector::DistSquared(ShadowCamera,E.Transform.GetTranslation());const bool Hide=OutlineRange>0&&D2>FMath::Square(E.OutlinesHidden?OutlineRange*.9:OutlineRange);
    if(Hide!=E.OutlinesHidden&&OutlineToggles<4){++OutlineToggles;E.OutlinesHidden=Hide;for(const int32 Slot:E.OutlineSlotIds)G->ShowMaterialSection(Slot,0,!Hide,0);}}
   if(auto* G=E.Gpu.Get()){OKCrowdSetTransform(G,E.Transform);G->SetVisibility(true);if((Render->Ready.Load()||(!Installed&&E.LoadingRender&&E.LoadingRender->Ready.Load()))&&E.VisibleMs<=0)E.VisibleMs=(FPlatformTime::Seconds()-Started)*1000;}
   if(auto* S=E.ShadowGpu.Get()){OKCrowdSetTransform(S,E.Transform);S->SetVisibility(true);}
   if(!Installed&&E.LoadingGpuInstalled){ENQUEUE_RENDER_COMMAND(PrivateLoadingPlayback)([R=E.LoadingRender,P,CpuSource=!RequestedGpu](FRHICommandListImmediate&){if(R->UseCpu!=CpuSource)R->Reset=true;R->UseCpu=CpuSource;R->Playback={P};R->Enabled=true;});if(E.LoadingRender->Ready.Load()&&!E.PrivateGpuVisibleMs){E.PrivateGpuVisibleMs=(FPlatformTime::Seconds()-Started)*1000;E.PrivateGpuVisibleComplete=Complete;if(!FirstVisibleMs)FirstVisibleMs=E.PrivateGpuVisibleMs;UE_LOG(LogTemp,Display,TEXT("APNG private GPU visible: selection=%d elapsed_ms=%.3f complete=%d/%d"),A,E.PrivateGpuVisibleMs,Complete,Entries.Num());}}
   if(auto* C=E.DepthCpu.Get()){C->SetVisibility(true);OKCrowdSetTransform(C,E.Transform);if(!GpuMode){C->BoneSpaceTransforms=E.Cpu->BoneSpaceTransforms;C->MarkRefreshTransformDirty();C->RefreshBoneTransforms();}}
   if(auto* G=E.DepthGpu.Get()){OKCrowdSetTransform(G,E.Transform);G->SetVisibility(DepthPrepassEnabled);}
   // A slow entry says which part was slow: speech and behaviour, the face look, the face upload, or the components.
   if(CVarOKCrowdLogSlowEntries.GetValueOnGameThread()){const double End=FPlatformTime::Seconds();if(End-EntryT0>.001)UE_LOG(LogTemp,Display,TEXT("Crowd entry %d slow %.2f ms: speech %.2f look %.2f face %.2f components %.2f"),A,(End-EntryT0)*1000,(EntryT1-EntryT0)*1000,(EntryT2-EntryT1)*1000,(EntryT3-EntryT2)*1000,(End-EntryT3)*1000);}
  }
  if(!LoadingJobs.IsEmpty()){
   // Jobs borrow only immutable plain pose data for the duration of this joined
   // task batch. Workers never call a UObject or retain scene/entry lifetimes.
   const double Begin=FPlatformTime::Seconds();
   {TRACE_CPUPROFILER_EVENT_SCOPE(ApngLoadingPoseEvaluation);
    ParallelFor(LoadingJobs.Num(),[&LoadingJobs](int32 I){auto& J=LoadingJobs[I];const auto& E=*J.Entry;const auto& P=J.Playback;for(int32 B=0;B<J.Poses.Num();++B){auto T=SampleLocal(E.Clips[P.ClipA],*E.CpuKeys,B,P.Times.X);if(P.ClipA!=P.ClipB){const auto Other=SampleLocal(E.Clips[P.ClipB],*E.CpuKeys,B,P.Times.Y);T=OKGpuBlendLocal(T,Other,P.Times.Z,E.Clips[P.ClipA].Tracks[B]);}J.Poses[B]=T;}},ParallelLoadingPoses?EParallelForFlags::None:EParallelForFlags::ForceSingleThread);
   }LoadingPoseEvalMs+=(FPlatformTime::Seconds()-Begin)*1000;
   const double RefreshBegin=FPlatformTime::Seconds();
   {TRACE_CPUPROFILER_EVENT_SCOPE(ApngLoadingPoseCommit);
    for(auto& J:LoadingJobs){auto& E=*J.Entry;LoadingBoneSamples+=J.Poses.Num();++LoadingPoseJobs;auto* C=E.Cpu.Get();C->BoneSpaceTransforms=MoveTemp(J.Poses);C->MarkRefreshTransformDirty();C->RefreshBoneTransforms();E.LastCpuBucket=J.Bucket;
     C->RefreshFollowerComponents();
    }
   }LoadingPoseRefreshMs+=(FPlatformTime::Seconds()-RefreshBegin)*1000;
  }
  if(Installed){ENQUEUE_RENDER_COMMAND(ApngPlaybackParameters)([R=Render,Play=MoveTemp(Play),Dt,CpuSource=CaptureStage==1?true:CaptureStage==2?false:!GpuMode&&!Warm](FRHICommandListImmediate&)mutable{if(!R->Enabled||R->UseCpu!=CpuSource)R->Reset=true;R->Enabled=true;R->UseCpu=CpuSource;R->Playback=MoveTemp(Play);R->RigDt=Dt;});}
 {QUICK_SCOPE_CYCLE_COUNTER(STAT_OKCrowd_CaptureLoading);  CaptureLoading();
 }
  if(!ExternalControl&&Camera.IsValid()&&PlayerPositionInitialized){auto* C=Camera.Get();C->SetActorRotation(FRotator(CameraPitch,CameraYaw,0));C->SetActorLocation(PlayerPosition-FRotator(0,CameraYaw,0).Vector()*300+FVector(0,0,160));}
  if(!ExternalControl&&GEngine&&SceneTime>=NextStatusTime){NextStatusTime=SceneTime+.25;GEngine->AddOnScreenDebugMessage(0x41504e47,.3,FColor::White,FString::Printf(TEXT("APNG %d/%d | %s | GPU passes %llu failures %llu | F9 CPU/GPU | WASD move | 1-8 actions | T speak | Y nearby NPC | Backspace resume"),Complete,Entries.Num(),GpuMode?TEXT("GPU"):RequestedGpu?TEXT("CPU (GPU preparing)"):TEXT("CPU"),Render->Passes.Load(),Render->Failures.Load()));}
  if(ReadyAt&&!VisualPrefix.IsEmpty()&&!Captured&&VisualError.IsEmpty()&&(StreamTest?(Entries[DemoCharacter]->StreamInstalls>0&&ActionSlotBusy(DemoCharacter)):Now-ReadyAt>4))Captured=Capture();
  const auto& DemoEntry=*Entries[DemoCharacter];const bool DemoSpeechDone=!DemoSpeech||VoiceIds.IsEmpty()||(DemoSpeechStarted&&!DemoEntry.SpeechJob.IsValid()&&!DemoEntry.SpeechWave.IsValid());
  const bool DiagnosticsDone=ReadyAt&&DemoSpeechDone&&(VisualPrefix.IsEmpty()||Captured||!VisualError.IsEmpty())&&(ProfileSeconds<=0||Now-ReadyAt>=10+2*ProfileSeconds);
  if(!ReportWritten&&((Next==Entries.Num()&&Active==0&&Complete!=Entries.Num())||DiagnosticsDone||(!ReadyAt&&Now-Started>LoadDiagnosticDeadline)))WriteReport();
  return true;
 }
};
TMap<TWeakObjectPtr<AActor>,TWeakPtr<FScene>> ActiveScenes;
}
bool OKStartCrowdRuntimeScene(AActor* Owner,const FString& Manifest,FString& Error){
 using namespace OKRuntimeScene;check(IsInGameThread());FString Text;TSharedPtr<FJsonObject> J;
 if(!Owner||IFileManager::Get().FileSize(*Manifest)>1024*1024||!FFileHelper::LoadFileToString(Text,*Manifest)||!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),J)||!J){Error=TEXT("Invalid runtime APNG scene manifest");return false;}
 // The front hair copies draw into the custom depth stencil (r.CustomDepth 3, off by default). Changing it recreates the
 // render state of every component in the world: set at the first install it stalled the street 420 ms (1834 place
 // parts, 142 procedural meshes); here, before the world fills, it costs nothing.
 if(auto* V=IConsoleManager::Get().FindConsoleVariable(TEXT("r.CustomDepth")))if(V->GetInt()!=3)V->Set(3,ECVF_SetByCode);
 auto State=MakeShared<OKRuntimeScene::FScene>();State->Owner=Owner;FString Root,Library;const TArray<TSharedPtr<FJsonValue>>* Cards=nullptr;const TArray<TSharedPtr<FJsonValue>>* Curves=nullptr;
 if(!J->TryGetStringField(TEXT("material_root"),Root)||!J->TryGetStringField(TEXT("material_library"),Library)||!J->TryGetArrayField(TEXT("cards"),Cards)||Cards->IsEmpty()||Cards->Num()>100||!J->TryGetArrayField(TEXT("curves"),Curves)||Curves->Num()<2||Curves->Num()>96){Error=TEXT("Runtime manifest needs 1..100 APNGs, idle and 1..95 common base curves");return false;}
 auto Resolve=[&](FString Path){return FPaths::IsRelative(Path)?FPaths::ConvertRelativePathToFull(FPaths::GetPath(Manifest)/Path):Path;};
 {const IConsoleVariable* Style=IConsoleManager::Get().FindConsoleVariable(TEXT("ok.RenderStyle"));FString HGRoot;
  if(Style&&Style->GetInt()==1){if(J->TryGetStringField(TEXT("hg_material_root"),HGRoot)&&!HGRoot.IsEmpty()){Root=HGRoot;State->HGStyle=true;}else UE_LOG(LogTemp,Warning,TEXT("HG render style: no hg_material_root, keeping %s"),*Root);}
  UE_LOG(LogTemp,Display,TEXT("Runtime materials root=%s style=%s"),*Root,State->HGStyle?TEXT("hg"):TEXT("koikatsu"));
  FString Ramps;if(J->TryGetStringField(TEXT("ramp_catalogue"),Ramps))OKSetRenderRampCatalogue(Resolve(Ramps));State->RampKey=OKRenderRampKey();}
 if(J->TryGetStringField(TEXT("display_lut"),State->DisplayLUTPath)){State->DisplayLUTPath=Resolve(State->DisplayLUTPath);State->DisplayLUT.Reset(OKLoadRuntimeDisplayLUT(State->DisplayLUTPath,Error));if(!State->DisplayLUT.IsValid())return false;}
 J->TryGetStringField(TEXT("report"),State->ReportPath);if(!State->ReportPath.IsEmpty())State->ReportPath=Resolve(State->ReportPath);J->TryGetBoolField(TEXT("exit_after_report"),State->ExitAfterReport);J->TryGetBoolField(TEXT("depth_prepass_enabled"),State->DepthPrepassEnabled);J->TryGetBoolField(TEXT("preserve_source_geometry"),State->PreserveSourceGeometry);J->TryGetBoolField(TEXT("progressive_loading"),State->ProgressiveLoading);J->TryGetBoolField(TEXT("parallel_loading_poses"),State->ParallelLoadingPoses);J->TryGetBoolField(TEXT("loading_cpu_preview"),State->LoadingCpuPreview);
 J->TryGetStringField(TEXT("visual_prefix"),State->VisualPrefix);if(!State->VisualPrefix.IsEmpty())State->VisualPrefix=Resolve(State->VisualPrefix);double Phase=.37;J->TryGetNumberField(TEXT("visual_phase"),Phase);State->VisualPhase=FMath::Clamp(Phase,0.,.9999);double Blend=1;J->TryGetNumberField(TEXT("visual_blend"),Blend);State->VisualBlend=FMath::Clamp(Blend,0.,1.);double Distance=350,Height=90;J->TryGetNumberField(TEXT("visual_distance"),Distance);J->TryGetNumberField(TEXT("visual_focus_height"),Height);State->VisualDistance=FMath::Clamp(Distance,30.,1000.);State->VisualFocusHeight=FMath::Clamp(Height,0.,300.);J->TryGetNumberField(TEXT("profile_seconds"),State->ProfileSeconds);State->ProfileSeconds=FMath::Clamp(State->ProfileSeconds,0.,60.);
 double Workers=State->Concurrency;J->TryGetNumberField(TEXT("background_prepare_concurrency"),Workers);State->Concurrency=FMath::Clamp(int32(Workers),1,FMath::Clamp(FTaskGraphInterface::Get().GetNumWorkerThreads(),1,16));J->TryGetNumberField(TEXT("diagnostic_load_deadline_seconds"),State->LoadDiagnosticDeadline);State->LoadDiagnosticDeadline=FMath::Clamp(State->LoadDiagnosticDeadline,60.,300.);
 J->TryGetBoolField(TEXT("aggregate_gpu"),State->AggregateGpu);J->TryGetBoolField(TEXT("gpu_rig"),State->GpuRig);J->TryGetNumberField(TEXT("gpu_rig_verify"),State->GpuRigVerify);
  // The rig's shaders exist only where they were cooked and asked for (OKGpuRig.cpp); without them the solved banks draw.
  if(State->GpuRig){bool Available=false;ENQUEUE_RENDER_COMMAND(OKCheckGpuRig)([&Available](FRHICommandListImmediate&){Available=OKGpuRigAvailable();});FlushRenderingCommands();
   if(!Available){UE_LOG(LogTemp,Warning,TEXT("GPU rig requested but its shaders are not in this build's shader cache (patch without them, or no -OKGpuRig): solved banks instead"));State->GpuRig=false;State->GpuRigVerify=0;}}if(State->GpuRig)State->AggregateGpu=true;J->TryGetBoolField(TEXT("enable_face_patterns"),State->CaptureFacePatterns);J->TryGetBoolField(TEXT("external_control"),State->ExternalControl);J->TryGetStringField(TEXT("voice_root"),State->VoiceRoot);if(!State->VoiceRoot.IsEmpty())State->VoiceRoot=Resolve(State->VoiceRoot);J->TryGetStringField(TEXT("generated_voice_root"),State->GeneratedVoiceRoot);if(!State->GeneratedVoiceRoot.IsEmpty())State->GeneratedVoiceRoot=Resolve(State->GeneratedVoiceRoot);
 State->WalkSources=Curves->Num();TArray<FString> CurvePaths;for(const auto& C:*Curves)CurvePaths.Add(Resolve(C->AsString()));State->ClipLoops.Init(true,CurvePaths.Num());State->ClipWindows.Init(0,CurvePaths.Num());
 const TArray<TSharedPtr<FJsonValue>>* Actions=nullptr;if(J->TryGetArrayField(TEXT("actions"),Actions)){if(Actions->Num()>2048){Error=TEXT("At most 2048 explicitly admitted common runtime actions");return false;}for(const auto& V:*Actions){if(!V||V->Type!=EJson::Object){Error=TEXT("Runtime action must be an object");return false;}const auto Action=V->AsObject();FString Path;bool Loop=false;if(!Action||!Action->TryGetStringField(TEXT("curve"),Path)){Error=TEXT("Runtime action needs a common curve");return false;}Action->TryGetBoolField(TEXT("loop"),Loop);double Window=0;Action->TryGetNumberField(TEXT("play_seconds"),Window);if(!FMath::IsFinite(Window)||Window<0||Window>34||(Window>0&&Loop)){Error=TEXT("Invalid non-looping action playback window");return false;}CurvePaths.Add(Resolve(Path));State->ClipLoops.Add(Loop);State->ClipWindows.Add(Window);}}
 // The base curves are read now; actions only exist on disk until they are first prepared (FScene::ClipBytes).
 State->Curves.SetNum(CurvePaths.Num());State->ClipPaths=CurvePaths;State->ClipIds.SetNum(CurvePaths.Num());for(int32 I=0;I<CurvePaths.Num();++I){const FString& Path=CurvePaths[I];if(I<State->WalkSources?(!FFileHelper::LoadFileToArray(State->Curves[I],*Path)||OKRuntimeSharedClipDuration(State->Curves[I])<=0):!IFileManager::Get().FileExists(*Path)){Error=TEXT("Common runtime curve missing/invalid");return false;}State->ClipIds[I]=FPaths::GetCleanFilename(Path);if(State->ClipIndex.Contains(State->ClipIds[I])?true:(State->ClipIndex.Add(State->ClipIds[I],I),false)){Error=TEXT("Common runtime clip IDs must be unique");return false;}}
 const TArray<TSharedPtr<FJsonValue>>* Voices=nullptr;if(J->TryGetArrayField(TEXT("voices"),Voices)){if(Voices->Num()>256){Error=TEXT("Runtime voice catalogue too large");return false;}for(const auto& V:*Voices){if(!V||V->Type!=EJson::String){Error=TEXT("Runtime voice must be a common file path");return false;}const FString Path=Resolve(V->AsString()),Id=FPaths::GetCleanFilename(Path);if(State->VoiceIds.Contains(Id)||!IFileManager::Get().FileExists(*Path)){Error=TEXT("Runtime voice missing or duplicate");return false;}State->VoicePaths.Add(Path);State->VoiceIds.Add(Id);}}J->TryGetBoolField(TEXT("demo_speech"),State->DemoSpeech);J->TryGetStringField(TEXT("demo_action"),State->DemoAction);
 {const TArray<TSharedPtr<FJsonValue>>* Variants=nullptr;if(J->TryGetArrayField(TEXT("idle_variants"),Variants))for(const auto& V:*Variants){const int32 Source=State->FindClip(V->AsString());if(Source<2||Source>=State->WalkSources){Error=TEXT("Idle variant must be a declared common base curve: ")+V->AsString();return false;}State->IdleVariantSources.AddUnique(Source);}
  double PerCharacter=4;if(J->TryGetNumberField(TEXT("idle_variants_per_character"),PerCharacter))State->IdleVariantsPerCharacter=FMath::Clamp(int32(PerCharacter),0,8);}
 double VisualSlot=1;J->TryGetNumberField(TEXT("visual_clip"),VisualSlot);if(!FMath::IsFinite(VisualSlot)||VisualSlot!=FMath::FloorToDouble(VisualSlot)||VisualSlot<1||VisualSlot>=2+State->Curves.Num()-State->WalkSources){Error=TEXT("Invalid visual clip slot");return false;}State->VisualClip=int32(VisualSlot);
 double DemoDelay=1;if(J->TryGetNumberField(TEXT("demo_delay_seconds"),DemoDelay)&&(!FMath::IsFinite(DemoDelay)||DemoDelay<0||DemoDelay>120)){Error=TEXT("Invalid runtime demo delay");return false;}State->DemoDelay=DemoDelay;double CancelAfter=-1;if(J->TryGetNumberField(TEXT("demo_cancel_speech_after_seconds"),CancelAfter)&&(!FMath::IsFinite(CancelAfter)||CancelAfter<0||CancelAfter>60)){Error=TEXT("Invalid runtime speech cancel delay");return false;}State->DemoCancelAfter=CancelAfter;
 double DemoCharacter=0;J->TryGetNumberField(TEXT("demo_character"),DemoCharacter);if(!FMath::IsFinite(DemoCharacter)||DemoCharacter!=FMath::FloorToDouble(DemoCharacter)||DemoCharacter<0||DemoCharacter>=Cards->Num()){Error=TEXT("Invalid runtime demo character");return false;}State->DemoCharacter=int32(DemoCharacter);J->TryGetBoolField(TEXT("visual_streamed_action"),State->StreamTest);J->TryGetBoolField(TEXT("auto_action_streaming"),State->AutoActionStreaming);
 double ActionPreload=0;if(J->TryGetNumberField(TEXT("npc_action_preload_count"),ActionPreload)&&(!FMath::IsFinite(ActionPreload)||ActionPreload!=FMath::FloorToDouble(ActionPreload)||ActionPreload<0||ActionPreload>8)){Error=TEXT("Invalid NPC action preload count");return false;}State->NpcActionPreload=int32(ActionPreload);
 double Mouth=-1;if(J->TryGetNumberField(TEXT("visual_mouth"),Mouth)&&(!FMath::IsFinite(Mouth)||Mouth<0||Mouth>1)){Error=TEXT("Invalid visual mouth opening");return false;}State->VisualMouth=Mouth;double Blink=-1;if(J->TryGetNumberField(TEXT("visual_blink"),Blink)&&(!FMath::IsFinite(Blink)||Blink<0||Blink>1)){Error=TEXT("Invalid visual blink closure");return false;}State->VisualBlink=Blink;J->TryGetBoolField(TEXT("auto_blink"),State->AutoBlink);
 // A library opens once per process: the map started again (a load or a new game) finds it open already.
 static TSet<FString> OpenedLibraries;const bool Opened=OpenedLibraries.Contains(Library)||FShaderCodeLibrary::OpenLibrary(*Library,FPaths::ProjectContentDir());if(Opened)OpenedLibraries.Add(Library);
 if(!Opened){
#if WITH_EDITOR
  J->TryGetBoolField(TEXT("editor_common_materials"),State->EditorCommonMaterials);
#endif
  if(!State->EditorCommonMaterials){Error=TEXT("Common runtime shader library unavailable");return false;}
  UE_LOG(LogTemp,Display,TEXT("APNG diagnostic uses editor common material resources; cooked library gate remains required in game builds"));
 }
 // Every recipe needs its parent in the root this scene draws with. A recipe added later (a new parent baked into
 // another root only) must name the root and every parent it lacks, not stop at the first one: bake a fresh root
 // (Tools/prepare_runtime_shared_material.lua --tag, then Tools/prepare_face_materials.lua) and point
 // StreetSocialData/Launch/materials.json at it (Docs/ERROR_LOG.md).
 {TArray<FString> Missing;for(auto R:OKRuntimeSharedMaterialRecipes()){const FString Name=OKRuntimeSharedMaterialParentName(R);if(!StaticLoadObject(UMaterialInterface::StaticClass(),nullptr,*(Root/Name+TEXT(".")+Name),nullptr,LOAD_NoWarn|LOAD_Quiet))Missing.Add(Name);}
  if(!Missing.IsEmpty()){Error=FString::Printf(TEXT("Common runtime material root %s lacks %d of %d recipe parents: %s (bake a fresh root with Tools/prepare_runtime_shared_material.lua and Tools/prepare_face_materials.lua, then point Launch/materials.json at it)"),*Root,Missing.Num(),OKRuntimeSharedMaterialRecipes().Num(),*FString::Join(Missing,TEXT(", ")));return false;}}
 for(auto R:OKRuntimeSharedMaterialRecipes()){const FString Name=OKRuntimeSharedMaterialParentName(R);auto* Parent=LoadObject<UMaterialInterface>(nullptr,*(Root/Name+TEXT(".")+Name));if(!Parent){Error=TEXT("Common runtime material parent missing: ")+Name;return false;}float RevealStart=0;if(!Parent->GetScalarParameterValue(FMaterialParameterInfo(TEXT("__OK_RuntimeRevealStart")),RevealStart)){Error=TEXT("Common runtime material lacks reveal support: ")+Name;return false;}State->Parents.Emplace(Parent);UMaterialInterface* Outline=nullptr;if(OKRuntimeOutlineLayout(R)){const FString OutlineName=Name+TEXT("_Outline");Outline=LoadObject<UMaterialInterface>(nullptr,*(Root/OutlineName+TEXT(".")+OutlineName));}State->OutlineParents.Emplace(Outline);}
 {UMaterialInterface* Tear=Cast<UMaterialInterface>(StaticLoadObject(UMaterialInterface::StaticClass(),nullptr,*(Root/TEXT("M_OKTearLit.M_OKTearLit")),nullptr,LOAD_NoWarn|LOAD_Quiet));
  State->TearParent.Reset(Tear);UE_LOG(LogTemp,Display,TEXT("Runtime tear parent: %s"),Tear?TEXT("lit (M_OKTearLit)"):TEXT("source glasses program"));}
 {int32 Found=0;for(auto R:OKRuntimeSharedMaterialRecipes()){const FString Name=OKRuntimeSharedMaterialParentName(R)+TEXT("_Fg");if(State->ForegroundParents.Num()<int32(R))State->ForegroundParents.SetNum(int32(R));
   auto* Fg=Cast<UMaterialInterface>(StaticLoadObject(UMaterialInterface::StaticClass(),nullptr,*(Root/Name+TEXT(".")+Name),nullptr,LOAD_NoWarn|LOAD_Quiet));State->ForegroundParents[int32(R)-1].Reset(Fg);Found+=Fg!=nullptr;}
  UE_LOG(LogTemp,Display,TEXT("Runtime foreground eye/brow parents: %d"),Found);}
 for(int32 I=0;I<Cards->Num();++I){auto E=MakeUnique<FEntry>();E->Path=Resolve((*Cards)[I]->AsString());if(!IFileManager::Get().FileExists(*E->Path)){Error=TEXT("Selected APNG missing: ")+E->Path;return false;}E->SelectionIndex=I;E->Id=FString::Printf(TEXT("selection-%03d"),I);
  // Which of the card's coordinates ("outfits", one per card; absent: the first, which some cards make a bunny suit).
  {const TArray<TSharedPtr<FJsonValue>>* Outfits=nullptr;if(J->TryGetArrayField(TEXT("outfits"),Outfits)&&Outfits->IsValidIndex(I))E->Outfit=FMath::Clamp(int32((*Outfits)[I]->AsNumber()),0,31);}E->WalkIndex=1+GetTypeHash(E->Path)%(State->WalkSources-1);
  // ChaControl.SetForegroundEyesAndEyebrow: the card's 1 is off, 2 on, 0 the game setting (EtceteraSystem's default,
  // off; OK has no such setting yet).
  {FKKCharacterFile Card;FString CardError;constexpr bool GameSetting=false;
   if(UKKCardReader::LoadFromFile(E->Path,Card,CardError)){E->ForegroundEyes=Card.Face.ForegroundEyes==2||(Card.Face.ForegroundEyes!=1&&GameSetting);E->ForegroundEyebrow=Card.Face.ForegroundEyebrow==2||(Card.Face.ForegroundEyebrow!=1&&GameSetting);}
   else UE_LOG(LogTemp,Warning,TEXT("Runtime foreground: card unread (%s): %s"),*E->Path,*CardError);
   E->ForegroundStencil=uint8(1+I%254);
   UE_LOG(LogTemp,Display,TEXT("Runtime foreground %s: eyes=%d eyebrow=%d (card %d/%d)"),*FPaths::GetBaseFilename(E->Path),E->ForegroundEyes,E->ForegroundEyebrow,Card.Face.ForegroundEyes,Card.Face.ForegroundEyebrow);}
  State->Entries.Add(MoveTemp(E));}
 const TArray<TSharedPtr<FJsonValue>>* Metadata=nullptr;
 if(State->ExternalControl&&J->TryGetArrayField(TEXT("characters"),Metadata)&&Metadata->Num()==State->Entries.Num())for(int32 I=0;I<Metadata->Num();++I){
  const auto Meta=(*Metadata)[I]->AsObject();if(!Meta)continue;auto& E=*State->Entries[I];double Rate=.5;
  if(Meta->TryGetNumberField(TEXT("voice_rate"),Rate)&&FMath::IsFinite(Rate))E.VoicePitch=FMath::Lerp(.94f,1.06f,float(FMath::Clamp(Rate,0.,1.)));
  for(const TCHAR* Field:{TEXT("walk_clip"),TEXT("conversation_idle"),TEXT("run_clip")}){FString Name;if(!Meta->TryGetStringField(Field,Name))continue;
   const int32 Source=State->FindClip(Name);if(Source<0||Source>=State->WalkSources){Error=TEXT("Locomotion must use declared common base curve: ")+Name;return false;}
   if(FString(Field)==TEXT("walk_clip"))E.WalkIndex=Source;else if(FString(Field)==TEXT("conversation_idle"))E.ConversationSource=Source;else E.RunSource=Source;
  }
 }
 State->PlayerPosition=State->SpawnPosition(0);State->PlayerPositionInitialized=true;
 auto* Camera=Owner->GetWorld()->SpawnActor<ACameraActor>();Camera->SetActorLocation(FVector(Cards->Num()<=4?-600:-1400,0,160));Camera->GetCameraComponent()->SetFieldOfView(70);State->Camera=Camera;if(auto* Pc=Owner->GetWorld()->GetFirstPlayerController())Pc->SetViewTarget(Camera);
 // Unity toon programs already produce the composed game color. Use the same
 // output transfer as the original crowd scene, including copied scene captures.
 // Applying UE's film curve again shifts hues and compresses highlights to white.
 OKApplyRuntimeDisplayProfile(Camera->GetCameraComponent()->PostProcessSettings,State->DisplayLUT.Get());Camera->GetCameraComponent()->PostProcessBlendWeight=1;
 UE_LOG(LogTemp,Display,TEXT("APNG display profile: LUT=%s exposure=0 toneCurve=0 expandGamut=0"),*State->DisplayLUTPath);

 ActiveScenes.Add(Owner,State);
 State->WorldCleanup=FWorldDelegates::OnWorldCleanup.AddLambda([Weak=TWeakPtr<OKRuntimeScene::FScene>(State),World=static_cast<const UWorld*>(Owner->GetWorld())](UWorld* Cleaned,bool,bool){if(Cleaned!=World)return; /* compared, never dereferenced */if(const auto Pinned=Weak.Pin())Pinned->Shutdown();});
 FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([State](float Dt){return State->Tick(Dt);}));Error.Reset();return true;
}

bool OKApplyCrowdRuntimeDisplayProfile(AActor* Owner,FPostProcessSettings& Settings){
 check(IsInGameThread());const auto* Weak=OKRuntimeScene::ActiveScenes.Find(Owner);const auto State=Weak?Weak->Pin():nullptr;if(!State||!State->DisplayLUT.IsValid())return false;OKApplyRuntimeDisplayProfile(Settings,State->DisplayLUT.Get());return true;
}

// A studio key light (direction toward the light, UE space) for every material of the scene, HG included.
void OKSetCrowdRuntimeStudioLight(AActor* Owner,const FVector& Light){
 check(IsInGameThread());const auto* Weak=OKRuntimeScene::ActiveScenes.Find(Owner);const auto State=Weak?Weak->Pin():nullptr;if(State)State->SetStudioLight(Light);
}

// The game's ramp render style for every character of this scene, live ("" = their own card ramps).
void OKSetCrowdRuntimeRamp(AActor* Owner,const FString& Key){
 check(IsInGameThread());const auto* Weak=OKRuntimeScene::ActiveScenes.Find(Owner);const auto State=Weak?Weak->Pin():nullptr;if(State)State->SetRamp(Key);
}

void OKUpdateCrowdRuntimeDisplayLighting(AActor* Owner,const FVector& LightDirection){
 check(IsInGameThread());const auto* Weak=OKRuntimeScene::ActiveScenes.Find(Owner);const auto State=Weak?Weak->Pin():nullptr;if(State)State->UpdateDisplayLighting(LightDirection);
}

bool OKSuspendCrowdRuntimeScene(AActor* Owner,bool Suspended){
 check(IsInGameThread());const auto* Weak=OKRuntimeScene::ActiveScenes.Find(Owner);const auto State=Weak?Weak->Pin():nullptr;if(!State||State->Stopped)return false;State->Suspended=Suspended;return true;
}

bool OKPlayCrowdRuntimeAction(AActor* Owner,int32 Character,const FString& ClipId,FString& Error){
 check(IsInGameThread());const auto* Weak=OKRuntimeScene::ActiveScenes.Find(Owner);const auto State=Weak?Weak->Pin():nullptr;
 if(!State||State->Stopped||!State->Installed||!State->Entries.IsValidIndex(Character)||State->Entries[Character]->BankActor==INDEX_NONE){Error=TEXT("Runtime character not ready");return false;}
 auto& E=*State->Entries[Character];const int32 Source=State->FindClip(ClipId),Slot=E.ClipSources.Find(Source);
 if(Source<State->WalkSources||Source==INDEX_NONE){Error=TEXT("Action must be a declared common action curve");return false;}
 if(Slot<2){
  if(E.Clips.Num()<3){Error=TEXT("Runtime action window requires one initial action slot");return false;}
  return State->QueueRuntimeAction(E,Source,true,Error);
 }
 if(!E.Clock.RequestClip(Slot,.3,0,Error))return false;E.StreamHold=false;E.ActionOverride=Slot;E.LastClip=Slot;++E.ActionRequests;return true;
}
bool OKConfigureCrowdRuntimeActionContact(AActor* Owner,int32 Character,const FString& ClipId,const FString& Recipe,FString& Error){
 check(IsInGameThread());const auto* Weak=OKRuntimeScene::ActiveScenes.Find(Owner);const auto State=Weak?Weak->Pin():nullptr;
 if(!State||State->Stopped||!State->Installed||!State->Entries.IsValidIndex(Character)||State->Entries[Character]->BankActor==INDEX_NONE||Recipe.IsEmpty()||Recipe.Len()>16384){Error=TEXT("Invalid runtime contact request");return false;}
 const int32 Source=State->FindClip(ClipId);auto& E=*State->Entries[Character];
 if(Source<State->WalkSources){Error=TEXT("Contact action must be an admitted common curve");return false;}
 if(const auto* Existing=E.ContactRecipes.Find(Source)){if(*Existing==Recipe){Error.Reset();return true;}Error=TEXT("Contact recipe is immutable for an admitted action");return false;}
 if(E.ClipSources.Find(Source)>=2||E.PendingAction==Source){Error=TEXT("Contact recipe must precede action preparation");return false;}
 E.ContactRecipes.Add(Source,Recipe);Error.Reset();return true;
}
bool OKPrewarmCrowdRuntimeAction(AActor* Owner,int32 Character,const FString& ClipId,FString& Error){
 check(IsInGameThread());const auto* Weak=OKRuntimeScene::ActiveScenes.Find(Owner);const auto State=Weak?Weak->Pin():nullptr;
 if(!State||State->Stopped||!State->Installed||!State->Entries.IsValidIndex(Character)||State->Entries[Character]->BankActor==INDEX_NONE){Error=TEXT("Runtime character not ready");return false;}
 auto& E=*State->Entries[Character];const int32 Source=State->FindClip(ClipId);
 if(Source<State->WalkSources||Source==INDEX_NONE){Error=TEXT("Action must be a declared common action curve");return false;}
 if(E.ClipSources.Find(Source)>=2){E.StreamHold=true;Error.Reset();return true;}
 if(E.Clips.Num()<3){Error=TEXT("Runtime action window requires one initial action slot");return false;}
 return State->QueueRuntimeAction(E,Source,false,Error);
}
bool OKCancelCrowdRuntimeAction(AActor* Owner,int32 Character,FString& Error){
 check(IsInGameThread());const auto* Weak=OKRuntimeScene::ActiveScenes.Find(Owner);const auto State=Weak?Weak->Pin():nullptr;
 if(!State||State->Stopped||!State->Installed||!State->Entries.IsValidIndex(Character)||State->Entries[Character]->BankActor==INDEX_NONE){Error=TEXT("Runtime character not ready");return false;}
 auto& E=*State->Entries[Character];E.ActionOverride=INDEX_NONE;E.PendingAction=INDEX_NONE;E.PendingEpoch=0;E.StreamHold=false;
 if(E.SpeechPaired||E.LinkedActionEpoch!=0)E.CancelSpeech();
 Error.Reset();return true;
}
bool OKReleaseCrowdRuntimeAction(AActor* Owner,int32 Character,FString& Error){
 check(IsInGameThread());const auto* Weak=OKRuntimeScene::ActiveScenes.Find(Owner);const auto State=Weak?Weak->Pin():nullptr;
 if(!State||State->Stopped||!State->Installed||!State->Entries.IsValidIndex(Character)||State->Entries[Character]->BankActor==INDEX_NONE){Error=TEXT("Runtime character not ready");return false;}
 State->Entries[Character]->ActionOverride=INDEX_NONE;Error.Reset();return true;
}
bool OKResumeCrowdRuntimeBehavior(AActor* Owner,int32 Character,FString& Error){
 check(IsInGameThread());const auto* Weak=OKRuntimeScene::ActiveScenes.Find(Owner);const auto State=Weak?Weak->Pin():nullptr;
 if(!State||State->Stopped||!State->Installed||!State->Entries.IsValidIndex(Character)||State->Entries[Character]->BankActor==INDEX_NONE){Error=TEXT("Runtime character not ready");return false;}
 auto& E=*State->Entries[Character];E.ActionOverride=INDEX_NONE;E.PendingAction=INDEX_NONE;E.PendingEpoch=0;E.StreamHold=false;E.CancelSpeech();Error.Reset();return true;
}

bool OKSpeakCrowdRuntimeLine(AActor* Owner,int32 Character,const FString& VoiceId,FString& Error){
 check(IsInGameThread());const auto* Weak=OKRuntimeScene::ActiveScenes.Find(Owner);const auto State=Weak?Weak->Pin():nullptr;
 if(!State||State->Stopped||!State->Installed||!State->Entries.IsValidIndex(Character)||State->Entries[Character]->BankActor==INDEX_NONE){Error=TEXT("Runtime character not ready");return false;}
 const int32 Voice=State->VoiceIds.Find(VoiceId);if(Voice==INDEX_NONE){Error=TEXT("Voice must be an admitted common resource");return false;}
 auto& E=*State->Entries[Character];if(E.SpeechJob.IsValid()){Error=TEXT("Runtime voice preparation already pending");return false;}
 E.PendingSpeechAction.Reset();E.LinkedActionEpoch=0;E.SpeechPaired=false;E.SpeechEpoch=++E.SpeechEpochClock;E.SpeechJob=OKPrepareRuntimeSpeechAsync(State->VoicePaths[Voice]);E.SpeechRequestedAt=FPlatformTime::Seconds();E.SpeechState=EOKRuntimeSpeechState::Preparing;E.SpeechError.Reset();++E.SpeechRequests;Error.Reset();return true;
}
bool OKSpeakCrowdRuntimeSong(AActor* Owner,int32 Character,const FString& VoiceId,const TArray<float>& MouthEnvelope,float HopSeconds,FString& Error){
 if(MouthEnvelope.IsEmpty()||HopSeconds<=0){Error=TEXT("A song needs a mouth envelope");return false;}
 if(!OKSpeakCrowdRuntimeLine(Owner,Character,VoiceId,Error))return false;
 const auto State=OKRuntimeScene::ActiveScenes.Find(Owner)->Pin();auto& E=*State->Entries[Character];E.MouthEnvelope=MouthEnvelope;E.EnvelopeHop=HopSeconds;return true;
}
bool OKSpeakCrowdRuntimeStream(AActor* Owner,int32 Character,TSharedPtr<FKKRuntimeSpeechStream,ESPMode::ThreadSafe> Stream,FString& Error){
 check(IsInGameThread());const auto* Weak=OKRuntimeScene::ActiveScenes.Find(Owner);const auto State=Weak?Weak->Pin():nullptr;
 if(!State||State->Stopped||!State->Installed||!State->Entries.IsValidIndex(Character)||State->Entries[Character]->BankActor==INDEX_NONE){Error=TEXT("Runtime character not ready");return false;}
 if(!Stream||Stream->Channels<1||Stream->Channels>2||Stream->SampleRate<8000||Stream->SampleRate>96000){Error=TEXT("Invalid runtime speech stream");return false;}
 auto& E=*State->Entries[Character];if(E.SpeechJob.IsValid()){Error=TEXT("Runtime voice preparation already pending");return false;}
 // Already "prepared": the next tick starts playback, which then waits on the stream's samples.
 FKKRuntimeSpeechResult Ready;Ready.Stream=MoveTemp(Stream);
 E.PendingSpeechAction.Reset();E.LinkedActionEpoch=0;E.SpeechPaired=false;E.SpeechEpoch=++E.SpeechEpochClock;E.SpeechJob=MakeFulfilledPromise<FKKRuntimeSpeechResult>(MoveTemp(Ready)).GetFuture();E.SpeechRequestedAt=FPlatformTime::Seconds();E.SpeechState=EOKRuntimeSpeechState::Preparing;E.SpeechError.Reset();++E.SpeechRequests;Error.Reset();return true;
}
bool OKSpeakCrowdRuntimeActionLine(AActor* Owner,int32 Character,const FString& VoiceId,const FString& ActionId,FString& Error){
 check(IsInGameThread());const auto* Weak=OKRuntimeScene::ActiveScenes.Find(Owner);const auto State=Weak?Weak->Pin():nullptr;
 if(!State||State->Stopped||!State->Installed||!State->Entries.IsValidIndex(Character)||State->Entries[Character]->BankActor==INDEX_NONE){Error=TEXT("Runtime character not ready");return false;}
 auto& E=*State->Entries[Character];FString Action=ActionId;uint64 Linked=0;
 if(Action.IsEmpty()){const int32 Slot=E.ActionOverride>=2?E.ActionOverride:(E.ClipSources.Num()>2?2:INDEX_NONE);if(Slot!=INDEX_NONE){const int32 Source=E.ClipSources[Slot]!=INDEX_NONE?E.ClipSources[Slot]:State->WalkSources+(E.SelectionIndex%(State->Curves.Num()-State->WalkSources));Action=State->ClipIds[Source];}}
 if(!Action.IsEmpty()){
  const int32 Source=State->FindClip(Action);if(Source<State->WalkSources||Source==INDEX_NONE){Error=TEXT("Speech action must be a declared common curve");return false;}
  if(E.SpeechJob.IsValid()){Error=TEXT("Runtime voice preparation already pending");return false;}
  if(E.ClipSources.Find(Source)<2&&!OKPlayCrowdRuntimeAction(Owner,Character,Action,Error))return false;
  if(E.PendingEpoch!=0&&E.PendingAction==Source)Linked=E.PendingEpoch;
  else if(E.StreamInstalled&&E.ClipSources.IsValidIndex(2)&&E.ClipSources[2]==Source)Linked=E.InstalledEpoch;
 }
 if(!OKSpeakCrowdRuntimeLine(Owner,Character,VoiceId,Error))return false;E.PendingSpeechAction=Action;E.SpeechPaired=!Action.IsEmpty();E.LinkedActionEpoch=Linked;return true;
}
void OKSetCrowdRuntimeSpeechRemote(AActor* Owner,int32 Character){
 check(IsInGameThread());const auto* Weak=OKRuntimeScene::ActiveScenes.Find(Owner);const auto State=Weak?Weak->Pin():nullptr;
 if(State)State->RemoteSpeech=Character;
}
bool OKStopCrowdRuntimeSpeech(AActor* Owner,int32 Character,FString& Error){
 check(IsInGameThread());const auto* Weak=OKRuntimeScene::ActiveScenes.Find(Owner);const auto State=Weak?Weak->Pin():nullptr;
 if(!State||State->Stopped||!State->Installed||!State->Entries.IsValidIndex(Character)||State->Entries[Character]->BankActor==INDEX_NONE){Error=TEXT("Runtime character not ready");return false;}
 auto& E=*State->Entries[Character];if(!E.PendingSpeechAction.IsEmpty()&&E.PendingAction==State->FindClip(E.PendingSpeechAction)){E.PendingAction=INDEX_NONE;E.PendingEpoch=0;}
 E.CancelSpeech();Error.Reset();return true;
}
bool OKSetCrowdRuntimeBlink(AActor* Owner,int32 Character,float Closure,FString& Error){
 check(IsInGameThread());const auto* Weak=OKRuntimeScene::ActiveScenes.Find(Owner);const auto State=Weak?Weak->Pin():nullptr;
 if(!State||State->Stopped||!State->Installed||!State->Entries.IsValidIndex(Character)||State->Entries[Character]->BankActor==INDEX_NONE||!FMath::IsFinite(Closure)||Closure< -1||Closure>1){Error=TEXT("Runtime blink character/value invalid");return false;}
 auto& E=*State->Entries[Character];if(!E.MouthStream||!E.MouthStream->BlinkVertices){Error=TEXT("APNG has no native eyelid morphs");return false;}
 E.BlinkOverride=Closure;Error.Reset();return true;
}
float OKCrowdRuntimeSpeechMouth(AActor* Owner,int32 Character){
 check(IsInGameThread());const auto* Weak=OKRuntimeScene::ActiveScenes.Find(Owner);const auto State=Weak?Weak->Pin():nullptr;
 return State&&State->Entries.IsValidIndex(Character)?State->Entries[Character]->SpeechMouth:0.f;
}

bool OKGetCrowdRuntimeActionStatus(AActor* Owner,int32 Character,FOKRuntimeActionStreamStatus& Status){
 check(IsInGameThread());Status={};const auto* Weak=OKRuntimeScene::ActiveScenes.Find(Owner);const auto State=Weak?Weak->Pin():nullptr;
 if(!State||State->Stopped||!State->Entries.IsValidIndex(Character))return false;
 const auto& E=*State->Entries[Character];
 bool PreparingThis=false;for(const auto& Slot:State->ActionSlots)PreparingThis|=Slot.Actor==Character&&Slot.Epoch!=0&&Slot.Epoch==E.PendingEpoch;
 Status.Preparing=PreparingThis&&!E.StreamInstalled;
 Status.Pending=E.PendingAction!=INDEX_NONE&&!Status.Preparing;
 Status.Ready=E.StreamInstalled&&E.ClipSources.IsValidIndex(2);
 Status.Playing=E.ActionOverride>=2;
 const auto Playback=E.Clock.Parameters();Status.Transitioning=E.Clock.IsTransitioning();
 if(E.Clips.IsValidIndex(E.ActionOverride)){Status.DurationSeconds=E.Clips[E.ActionOverride].Duration;Status.PlaybackSeconds=Playback.ClipB==uint32(E.ActionOverride)&&Status.Transitioning?Playback.Times.Y:Playback.Times.X;}
 Status.Prewarmed=E.StreamHold;
 Status.ActiveSlots=State->ActiveActionSlots();
 Status.QueuedRequests=State->QueuedActionRequests();
 Status.Session=E.PendingEpoch?E.PendingEpoch:E.InstalledEpoch;
 if(E.PendingAction!=INDEX_NONE&&State->ClipIds.IsValidIndex(E.PendingAction))Status.ClipId=State->ClipIds[E.PendingAction];
 else if(E.StreamInstalled&&E.ClipSources.IsValidIndex(2)&&State->ClipIds.IsValidIndex(E.ClipSources[2]))Status.ClipId=State->ClipIds[E.ClipSources[2]];
 return true;
}
bool OKGetCrowdRuntimeCharacterStatus(AActor* Owner,int32 Character,FOKRuntimeCharacterStatus& Status){
 check(IsInGameThread());Status={};const auto* Weak=OKRuntimeScene::ActiveScenes.Find(Owner);const auto State=Weak?Weak->Pin():nullptr;
 if(!State||State->Stopped||!State->Entries.IsValidIndex(Character))return false;
 const auto& E=*State->Entries[Character];Status.Path=E.Path;Status.Error=E.Error;Status.Transform=State->ExternalControl?E.ExternalTransform:E.Transform;
 Status.Ready=E.Stage==5;Status.Visible=E.RevealStarted&&E.VisibleMs>0&&Owner->GetWorld()->GetTimeSeconds()>=E.RevealWorldStart+3;
 const auto Playback=E.Clock.Parameters();Status.AnimationTransitioning=E.Clock.IsTransitioning();
 if(E.ClipSources.IsValidIndex(Playback.ClipA)&&State->ClipIds.IsValidIndex(E.ClipSources[Playback.ClipA]))Status.ActiveClipId=State->ClipIds[E.ClipSources[Playback.ClipA]];
 Status.ActionsReady=State->Installed&&Status.Visible;return true;
}
bool OKSetCrowdRuntimeCharacterMotion(AActor* Owner,int32 Character,const FTransform& Transform,float Speed,FString& Error){
 check(IsInGameThread());const auto* Weak=OKRuntimeScene::ActiveScenes.Find(Owner);const auto State=Weak?Weak->Pin():nullptr;
 if(!State||State->Stopped||!State->ExternalControl||!State->Entries.IsValidIndex(Character)){Error=TEXT("External runtime control is unavailable");return false;}
 if(Transform.ContainsNaN()||!Transform.IsValid()||!FMath::IsFinite(Speed)||Speed<0||Speed>10){Error=TEXT("Invalid external character transform/speed");return false;}
 auto& E=*State->Entries[Character];E.ExternalTransform=Transform;E.ExternalSpeed=Speed;E.Transform=Transform;
 if(Character==0){State->PlayerPosition=Transform.GetTranslation();State->PlayerPositionInitialized=true;}Error.Reset();return true;
}
bool OKGetCrowdRuntimeBonePosition(AActor* Owner,int32 Character,FName Bone,FVector& World){
 FTransform Transform;if(!OKGetCrowdRuntimeBoneTransform(Owner,Character,Bone,Transform))return false;World=Transform.GetTranslation();return true;
}
bool OKGetCrowdRuntimeBoneTransform(AActor* Owner,int32 Character,FName Bone,FTransform& World){
 check(IsInGameThread());const auto* Weak=OKRuntimeScene::ActiveScenes.Find(Owner);const auto State=Weak?Weak->Pin():nullptr;
 if(!State||State->Stopped||!State->Entries.IsValidIndex(Character))return false;const auto& E=*State->Entries[Character];
 if(E.Stage!=5||!E.CpuKeys.IsValid())return false;const int32 Index=E.Bones.IndexOfByPredicate([Bone](const auto& B){return B.Name==Bone;});if(Index==INDEX_NONE)return false;
 auto P=E.Clock.Parameters();if(!E.Clips.IsValidIndex(P.ClipA)||!E.Clips.IsValidIndex(P.ClipB))return false;
 P.Times.X=FMath::FloorToFloat(P.Times.X*60)/60;P.Times.Y=FMath::FloorToFloat(P.Times.Y*60)/60;
 FTransform Result=FTransform::Identity;int32 B=Index,Steps=0;
 while(B!=INDEX_NONE){if(!E.Bones.IsValidIndex(B)||++Steps>E.Bones.Num())return false;auto Local=OKRuntimeScene::SampleLocal(E.Clips[P.ClipA],*E.CpuKeys,B,P.Times.X);if(P.ClipA!=P.ClipB)Local=OKGpuBlendLocal(Local,OKRuntimeScene::SampleLocal(E.Clips[P.ClipB],*E.CpuKeys,B,P.Times.Y),P.Times.Z,E.Clips[P.ClipA].Tracks[B]);OKRuntimeScene::TurnLocal(E,B,Local);Result=Result*Local;B=E.Bones[B].Parent;}
 World=Result*E.Transform;return World.IsValid()&&!World.ContainsNaN();
}
bool OKAdmitCrowdRuntimeVoice(AActor* Owner,const FString& Path,FString& VoiceId,FString& Error){
 check(IsInGameThread());const auto* Weak=OKRuntimeScene::ActiveScenes.Find(Owner);const auto State=Weak?Weak->Pin():nullptr;VoiceId.Reset();
 FString Full=FPaths::ConvertRelativePathToFull(Path);FPaths::NormalizeFilename(Full);FPaths::CollapseRelativeDirectories(Full);
 if(!State||State->Stopped){Error=TEXT("Voice admission requires an active runtime scene");return false;}
 const bool UnderRoot=(!State->VoiceRoot.IsEmpty()&&FPaths::IsUnderDirectory(Full,State->VoiceRoot))||(!State->GeneratedVoiceRoot.IsEmpty()&&FPaths::IsUnderDirectory(Full,State->GeneratedVoiceRoot));
 if(!UnderRoot||FPaths::GetExtension(Full).ToLower()!=TEXT("ogg")||!IFileManager::Get().FileExists(*Full)){Error=TEXT("Voice admission requires an OGG below the declared common root");return false;}
 const auto Bytes=FTCHARToUTF8(*Full);uint8 Digest[20];FSHA1::HashBuffer(Bytes.Get(),Bytes.Length(),Digest);VoiceId=TEXT("catalogue-")+BytesToHex(Digest,20);
 if(State->VoiceIds.Contains(VoiceId)){Error.Reset();return true;}
 // The catalogue is not the residency set. Admission is a bounded hot list;
 // in-flight decoders already own their immutable path and PCM independently.
 if(State->VoiceIds.Num()>=256){if(!State->ExternalControl){Error=TEXT("Voice admission window full");VoiceId.Reset();return false;}State->VoiceIds.RemoveAt(0);State->VoicePaths.RemoveAt(0);}
 State->VoiceIds.Add(VoiceId);State->VoicePaths.Add(Full);Error.Reset();return true;
}
bool OKCrowdRuntimeSpeechActive(AActor* Owner,int32 Character){
 check(IsInGameThread());const auto* Weak=OKRuntimeScene::ActiveScenes.Find(Owner);const auto State=Weak?Weak->Pin():nullptr;
 return State&&State->Entries.IsValidIndex(Character)&&(State->Entries[Character]->SpeechJob.IsValid()||State->Entries[Character]->SpeechWave.IsValid());
}
bool OKCaptureCrowdRuntimeSceneReport(AActor* Owner){
 check(IsInGameThread());const auto* Weak=OKRuntimeScene::ActiveScenes.Find(Owner);const auto State=Weak?Weak->Pin():nullptr;
 if(!State||State->Stopped)return false;const bool Exit=State->ExitAfterReport;State->ExitAfterReport=false;State->WriteReport();State->ExitAfterReport=Exit;return State->ReportWritten;
}

bool OKGetCrowdRuntimeSpeechState(AActor* Owner,int32 Character,EOKRuntimeSpeechState& Status,FString& Error){
 check(IsInGameThread());const auto* Weak=OKRuntimeScene::ActiveScenes.Find(Owner);const auto State=Weak?Weak->Pin():nullptr;
 if(!State||State->Stopped||!State->Entries.IsValidIndex(Character)){Error=TEXT("Runtime speech character unavailable");return false;}
 Status=State->Entries[Character]->SpeechState;Error=State->Entries[Character]->SpeechError;return true;
}

void OKGetCrowdRuntimeCharacterComponents(AActor* Owner,int32 Character,TArray<UPrimitiveComponent*>& Out)
{
 check(IsInGameThread());
 const auto* Weak=OKRuntimeScene::ActiveScenes.Find(Owner);
 const auto State=Weak?Weak->Pin():nullptr;
 if(!State||State->Stopped||!State->Entries.IsValidIndex(Character))return;
 const auto& E=*State->Entries[Character];
 // What draws her in the view: the GPU mesh (and its front layer) or, while loading, the CPU pose mesh.
 for(UPrimitiveComponent* C:{static_cast<UPrimitiveComponent*>(E.Gpu.Get()),static_cast<UPrimitiveComponent*>(E.FrontGpu.Get()),static_cast<UPrimitiveComponent*>(E.Cpu.Get())})
 {
  if(C&&C->IsVisible())Out.Add(C);
 }
}

bool OKGetCrowdRuntimeSpeechPlayback(AActor* Owner,int32 Character,double& Played,double& Total)
{
 check(IsInGameThread());
 const auto* Weak=OKRuntimeScene::ActiveScenes.Find(Owner);
 const auto State=Weak?Weak->Pin():nullptr;
 if(!State||State->Stopped||!State->Entries.IsValidIndex(Character))return false;
 const auto& E=*State->Entries[Character];
 const UKKRuntimeSpeechWave* Wave=E.SpeechWave.Get();
 UWorld* World=Owner?Owner->GetWorld():nullptr;
 if(!Wave||!Wave->Meter||!World||!World->GetAudioDevice().IsValid())return false;
 const double Rate=FMath::Max(1,World->GetAudioDevice()->GetSampleRate());
 Played=double(Wave->Meter->Frames.Load())/Rate;
 Total=-1.;
 if(Wave->Pcm&&Wave->Pcm->SampleRate>0&&Wave->Pcm->Channels>0)Total=double(Wave->Pcm->Samples.Num())/Wave->Pcm->Channels/Wave->Pcm->SampleRate;
 // A stream's length is known once all of it has arrived.
 else if(Wave->Stream&&Wave->Stream->IsComplete()&&Wave->Stream->SampleRate>0&&Wave->Stream->Channels>0)Total=double(Wave->Stream->Num())/Wave->Stream->Channels/Wave->Stream->SampleRate;
 return true;
}

bool OKSetCrowdRuntimeFaceExpression(AActor* Owner,int32 Character,const FKKRuntimeFaceState& Face,bool AllowBlink,FString& Error){
 check(IsInGameThread());const auto* Weak=OKRuntimeScene::ActiveScenes.Find(Owner);const auto State=Weak?Weak->Pin():nullptr;
 if(!State||State->Stopped||!State->Entries.IsValidIndex(Character)){Error=TEXT("Runtime expression character unavailable");return false;}
 auto& E=*State->Entries[Character];if(!E.MouthStream){Error=TEXT("Runtime face stream unavailable");return false;}
 return OKRequestRuntimeFace(*E.MouthStream,E.FacePlay,Face,AllowBlink,Error);
}
namespace OKRuntimeScene {
int32 ApplyFaceLook(FEntry& E,FString* Trace){
 int32 Changed=0;TSet<UMaterialInstanceDynamic*> Seen;const auto& L=E.LookNow;
 auto Base=[&](UMaterialInstanceDynamic* M,const TCHAR* Name,bool Vector,FLinearColor& Out)->bool{
  const FString Key=FString::Printf(TEXT("%p|%s"),M,Name);if(const FLinearColor* Kept=E.LookBase.Find(Key)){Out=*Kept;return true;}
  bool Has=false;if(Vector){Has=M->GetVectorParameterValue(FMaterialParameterInfo(Name),Out);float A=0;if(Has&&M->GetScalarParameterValue(FMaterialParameterInfo(*(FString(Name)+TEXT("__A"))),A))Out.A=A;}
  else{float S=0;Has=M->GetScalarParameterValue(FMaterialParameterInfo(Name),S);Out=FLinearColor(S,0,0,0);}
  if(Has)E.LookBase.Add(Key,Out);return Has;};
 // A four-wide uniform's w travels as its own scalar (name__A, KKRuntimeSharedMaterial binding): both are written.
 auto SetVec=[](UMaterialInstanceDynamic* M,const TCHAR* Name,const FLinearColor& V){M->SetVectorParameterValue(Name,V);M->SetScalarParameterValue(*(FString(Name)+TEXT("__A")),V.A);};
 for(int32 I=0;I<E.LookKinds.Num()&&I<E.Materials.Num();++I){
  auto* M=E.Materials[I].Get();if(!M||Seen.Contains(M))continue;Seen.Add(M);const uint8 Kind=E.LookKinds[I];
  const bool Skin=Kind==1||Kind==2,Iris=Kind==3;FLinearColor V;
  // Cheeks: the face's second overlay is the hohoaka layer; its alpha is how red (the source writes 0.2 x the card rate;
  // a full blush here is about twice the card maximum, the flush of an embarrassed face).
  if(Kind==1&&Base(M,TEXT("_overcolor2"),true,V)){// A card without a cheek colour of its own (rgb 0: the layer would darken) blushes the source's default pink.
   const FLinearColor Cheek=V.R+V.G+V.B<0.05f?FLinearColor(1.f,.45f,.45f):FLinearColor(V.R,V.G,V.B);
   // The colour turns to the cheek pink over the first fifth of a blush, so it rises from the card without a step.
   const FLinearColor Tint=FMath::Lerp(FLinearColor(V.R,V.G,V.B),Cheek,FMath::Clamp(L.Blush*5.f,0.f,1.f));
   SetVec(M,TEXT("_overcolor2"),FLinearColor(Tint.R,Tint.G,Tint.B,FMath::Clamp(V.A+FMath::Max(L.Blush,0.f)*0.4f,0.f,1.f)));++Changed;if(Trace)*Trace+=FString::Printf(TEXT(" cheek=(%.2f,%.2f,%.2f,%.2f)"),V.R,V.G,V.B,V.A);}
  // Sweat: wet, glossy skin. The skin program's own gloss (the nose and cheek glints of _SpecularPower 1 in a white
  // _SpecularColor; at 0.6 x the card's it did not show in the HG style).
  if(Skin&&Base(M,TEXT("_SpecularPower"),false,V)){const float W=FMath::Clamp(L.Sweat,0.f,1.f);M->SetScalarParameterValue(TEXT("_SpecularPower"),FMath::Lerp(V.R,1.f,W));++Changed;
   if(Base(M,TEXT("_SpecularColor"),true,V))SetVec(M,TEXT("_SpecularColor"),FMath::Lerp(V,FLinearColor::White,W));}
  // Gaze: the iris slides in its socket (the source's eye look moves the iris texture offset).
  if(Iris&&Base(M,TEXT("_MainTex_ST"),true,V)){SetVec(M,TEXT("_MainTex_ST"),FLinearColor(V.R,V.G,V.B-L.Gaze.X*0.12f*V.R,V.A+L.Gaze.Y*0.10f*V.G));++Changed;if(Trace)*Trace+=FString::Printf(TEXT(" iris=(%.3f,%.3f,%.3f,%.3f)"),V.R,V.G,V.B,V.A);}
  // Tears: her level's drops show (opacity, and the reveal for the unlit fallback), the others are gone.
  if(const int32* Level=E.Tears.Find(M)){const float Show=FMath::Clamp(L.Tear[FMath::Clamp(*Level,1,3)],0.f,1.f);M->SetScalarParameterValue(TEXT("__OK_RuntimeRevealOverride"),Show);M->SetScalarParameterValue(TEXT("TearVisible"),Show);++Changed;continue;}
  if(Iris&&Base(M,TEXT("_isHighLight"),false,V)){M->SetScalarParameterValue(TEXT("_isHighLight"),V.R*FMath::Clamp(L.Highlight,0.f,1.f));++Changed;}
 }
 return Changed;
}
// Each part of the look eases toward its goal on its own clock (time constants in seconds: about three of them to
// arrive): the eyes glide quickly, a blush or tears come over her more slowly, sweat slowest.
void TickFaceLook(FEntry& E,float Dt){
 if(!E.LookFading||!FMath::IsFinite(Dt)||Dt<=0.f)return;
 const float Scale=FMath::Max(E.LookFadeScale,.01f);bool Moving=false;
 auto Ease=[&](float& Now,float Goal,float Tau){const float D=Goal-Now;if(FMath::Abs(D)<.002f){Now=Goal;return;}Now+=D*(1.f-FMath::Exp(-FMath::Min(Dt,.25f)/(Tau*Scale)));Moving=true;};
 auto& N=E.LookNow;const auto& G=E.LookGoal;
 Ease(N.Blush,G.Blush,.35f);Ease(N.Sweat,G.Sweat,.5f);Ease(N.Highlight,G.Highlight,.12f);
 {float X=float(N.Gaze.X),Y=float(N.Gaze.Y);Ease(X,float(G.Gaze.X),.08f);Ease(Y,float(G.Gaze.Y),.08f);N.Gaze=FVector2D(X,Y);}for(int32 L=1;L<=3;++L)Ease(N.Tear[L],G.Tear[L],.3f);
 ApplyFaceLook(E);E.LookFading=Moving;
}
}
bool OKSetCrowdRuntimeFaceLook(AActor* Owner,int32 Character,const FOKRuntimeFaceLook& Look,FString& Error){
 check(IsInGameThread());const auto* Weak=OKRuntimeScene::ActiveScenes.Find(Owner);const auto State=Weak?Weak->Pin():nullptr;
 if(!State||State->Stopped||!State->Entries.IsValidIndex(Character)){Error=TEXT("Runtime face look character unavailable");return false;}
 auto& E=*State->Entries[Character];
 if(!E.LookKinds.ContainsByPredicate([](uint8 K){return K>=1&&K<=3;})){Error=TEXT("Runtime face look: no face, skin or iris material");return false;}
 // The goal only; TickFaceLook brings her face there. -1 leaves that part as it is.
 auto& G=E.LookGoal;if(Look.Blush>=0.f)G.Blush=Look.Blush;if(Look.Sweat>=0.f)G.Sweat=Look.Sweat;G.Gaze=Look.Gaze;
 if(Look.Highlight>=0)G.Highlight=Look.Highlight?1.f:0.f;if(Look.Tears>=0)for(int32 L=1;L<=3;++L)G.Tear[L]=L==Look.Tears?1.f:0.f;
 if(Look.FadeScale>0.f)E.LookFadeScale=Look.FadeScale;E.LookFading=true;
 {int32 K[5]={0,0,0,0,0};for(const uint8 X:E.LookKinds)++K[FMath::Min<int32>(X,4)];FString Trace;if(E.LookBase.IsEmpty())OKRuntimeScene::ApplyFaceLook(E,&Trace);
  UE_LOG(LogTemp,Display,TEXT("Runtime face look %s: materials face=%d skin=%d iris=%d tears=%d; goal blush=%.2f sweat=%.2f gaze=(%.2f,%.2f) tears=%d highlight=%.0f%s"),*E.Id,K[1],K[2],K[3],E.Tears.Num(),G.Blush,G.Sweat,G.Gaze.X,G.Gaze.Y,Look.Tears,G.Highlight,*Trace);}
 return true;
}
bool OKSetCrowdRuntimeHeadLook(AActor* Owner,int32 Character,bool Enabled,const FVector& World){
 check(IsInGameThread());const auto* Weak=OKRuntimeScene::ActiveScenes.Find(Owner);const auto State=Weak?Weak->Pin():nullptr;
 if(!State||State->Stopped||!State->Entries.IsValidIndex(Character)||(Enabled&&World.ContainsNaN()))return false;
 auto& E=*State->Entries[Character];if(Enabled)E.LookAt=World;E.Looking=Enabled;return true;
}
void OKSetCrowdRuntimeOutlinesShown(AActor* Owner,int32 Character,bool Shown){
 check(IsInGameThread());const auto* Weak=OKRuntimeScene::ActiveScenes.Find(Owner);const auto State=Weak?Weak->Pin():nullptr;
 if(!State||State->Stopped||!State->Entries.IsValidIndex(Character))return;
 for(const auto& M:State->Entries[Character]->OutlineMaterials)if(auto* I=M.Get())I->SetScalarParameterValue(TEXT("__OK_RuntimeRevealOverride"),Shown?-1.f:0.f);
}
bool OKGetCrowdRuntimeFaceLook(AActor* Owner,int32 Character,FOKRuntimeFaceLookNow& Now){
 check(IsInGameThread());const auto* Weak=OKRuntimeScene::ActiveScenes.Find(Owner);const auto State=Weak?Weak->Pin():nullptr;
 if(!State||State->Stopped||!State->Entries.IsValidIndex(Character))return false;const auto& E=*State->Entries[Character];const auto& L=E.LookNow;
 Now.Blush=L.Blush;Now.Sweat=L.Sweat;Now.Highlight=L.Highlight;Now.Gaze=L.Gaze;Now.Fading=E.LookFading;
 for(int32 Level=1;Level<=3;++Level)if(L.Tear[Level]>Now.TearShown){Now.TearShown=L.Tear[Level];Now.Tears=Level;}
 return true;
}
int32 OKSetCrowdRuntimeLookParameter(AActor* Owner,int32 Character,int32 Kind,const FString& Name,const FLinearColor& Value,bool Vector){
 check(IsInGameThread());const auto* Weak=OKRuntimeScene::ActiveScenes.Find(Owner);const auto State=Weak?Weak->Pin():nullptr;
 if(!State||State->Stopped||!State->Entries.IsValidIndex(Character))return 0;auto& E=*State->Entries[Character];int32 N=0;TSet<UMaterialInstanceDynamic*> Seen;
 for(int32 I=0;I<E.LookKinds.Num()&&I<E.Materials.Num();++I){auto* M=E.Materials[I].Get();if(!M||E.LookKinds[I]!=Kind||Seen.Contains(M))continue;Seen.Add(M);
  // "__default_material": the slot drawn with the engine's plain surface material (does the geometry show at all?).
  if(Name==TEXT("__default_material")){UMaterialInterface* Probe=UMaterial::GetDefaultMaterial(MD_Surface);
   for(UMeshComponent* C:{static_cast<UMeshComponent*>(E.Gpu.Get()),static_cast<UMeshComponent*>(E.Cpu.Get()),static_cast<UMeshComponent*>(E.LoadingMain.Get())})if(C)C->SetMaterial(I,Probe);++N;continue;}
  if(Vector){M->SetVectorParameterValue(*Name,Value);M->SetScalarParameterValue(*(Name+TEXT("__A")),Value.A);}else M->SetScalarParameterValue(*Name,Value.R);++N;}
 return N;
}
bool OKResetCrowdRuntimeFaceExpression(AActor* Owner,int32 Character,FString& Error){
 check(IsInGameThread());const auto* Weak=OKRuntimeScene::ActiveScenes.Find(Owner);const auto State=Weak?Weak->Pin():nullptr;
 if(!State||State->Stopped||!State->Entries.IsValidIndex(Character)){Error=TEXT("Runtime expression character unavailable");return false;}
 // Her blush, sweat and gaze go back to the card's with the face.
 if(!State->Entries[Character]->LookBase.IsEmpty()){FOKRuntimeFaceLook Plain;Plain.Blush=0.f;Plain.Sweat=0.f;Plain.Tears=0;FString LookError;OKSetCrowdRuntimeFaceLook(Owner,Character,Plain,LookError);}
 return OKResetRuntimeFace(State->Entries[Character]->FacePlay,Error);
}

bool OKGetCrowdRuntimeFaceStatus(AActor* Owner,int32 Character,FOKRuntimeFaceStatus& Status){
 check(IsInGameThread());Status={};const auto* Weak=OKRuntimeScene::ActiveScenes.Find(Owner);const auto State=Weak?Weak->Pin():nullptr;if(!State||State->Stopped||!State->Entries.IsValidIndex(Character))return false;
 const auto& E=*State->Entries[Character];Status.Active=E.FacePlay.Enabled;Status.Requests=E.FacePlay.Requests;Status.Updates=E.FacePlay.Updates;Status.Restores=E.FacePlay.Restores;Status.Started=E.FacePlay.Started;Status.Intermediate=E.FacePlay.Intermediate;Status.Completed=E.FacePlay.Completed;Status.Cancelled=E.FacePlay.Cancelled;return true;
}

int32 OKSetCrowdRuntimeIdleVariant(AActor* Owner,int32 Character,int32 Variant){
 check(IsInGameThread());const auto* Weak=OKRuntimeScene::ActiveScenes.Find(Owner);const auto State=Weak?Weak->Pin():nullptr;
 if(!State||State->Stopped||!State->Entries.IsValidIndex(Character))return 0;auto& E=*State->Entries[Character];E.IdleVariant=E.IdleVariantSlots.IsValidIndex(Variant)?Variant:INDEX_NONE;return E.IdleVariantSlots.Num();
}

bool OKSetCrowdRuntimeIdleReading(AActor* Owner,int32 Character,bool Reading){
 check(IsInGameThread());const auto* Weak=OKRuntimeScene::ActiveScenes.Find(Owner);const auto State=Weak?Weak->Pin():nullptr;
 if(!State||State->Stopped||!State->Entries.IsValidIndex(Character))return false;State->Entries[Character]->ReadingAllowed=Reading;return true;
}

bool OKSetCrowdRuntimeLocomotionState(AActor* Owner,int32 Character,bool Conversation,bool Running,FString& Error){
 check(IsInGameThread());const auto* Weak=OKRuntimeScene::ActiveScenes.Find(Owner);const auto State=Weak?Weak->Pin():nullptr;
 if(!State||State->Stopped||!State->Entries.IsValidIndex(Character)){Error=TEXT("Runtime locomotion actor unavailable");return false;}
 auto& E=*State->Entries[Character];E.ConversationIdle=Conversation;E.Running=Running;Error.Reset();return true;
}
