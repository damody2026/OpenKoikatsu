#include "OKCrowdIndependentGpu.h"
#include "OKGpuResidencyBudget.h"
#include "OKCrowdScene.h"
#include "OKGpuAnimationManifest.h"
#include "ScenePrivate.h"
#include "Skinning/SkinningTransformProvider.h"
#include "Skinning/SkinningSceneExtension.h"
#include "SkinningSceneExtensionProxy.h"
#include "Engine/World.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Animation/AnimCompressionTypes.h"
#include "Components/SkeletalMeshComponent.h"
#include "RenderGraphUtils.h"
#include "RHIGPUReadback.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Rendering/SkinWeightVertexBuffer.h"
#include "Components/InstancedSkinnedMeshComponent.h"
#include "Materials/Material.h"
#include "ShaderCodeLibrary.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "UObject/StrongObjectPtr.h"
#include "Json.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/PlayerController.h"
#include "ImageUtils.h"
#include "ImageCore.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

const FGuid& UOKIndependentTransformProvider::GetTransformProviderID() const{return OwnedProviderId;}
namespace {TMap<FGuid,FInstancedSkinningSceneExtensionProxy*> GIndependentProxyRegistry;
class FOKIndependentTransformProxy : public FTransformProviderRenderProxy {
 FGuid Id;FInstancedSkinningSceneExtensionProxy* Proxy;
 public:FOKIndependentTransformProxy(FGuid InId,FInstancedSkinningSceneExtensionProxy* InProxy):Id(InId),Proxy(InProxy){}
 void CreateRenderThreadResources(FSceneInterface&,FRHICommandListBase&) override{GIndependentProxyRegistry.Add(Id,Proxy);}
 void DestroyRenderThreadResources() override{if(GIndependentProxyRegistry.FindRef(Id)==Proxy)GIndependentProxyRegistry.Remove(Id);}
};}
FTransformProviderRenderProxy* UOKIndependentTransformProvider::CreateRenderProxy(FInstancedSkinningSceneExtensionProxy* Proxy) const{return new FOKIndependentTransformProxy(OwnedProviderId,Proxy);}
FInstancedSkinningSceneExtensionProxy* OKFindIndependentRuntimeProxy(const FGuid& Id){check(IsInRenderingThread());return GIndependentProxyRegistry.FindRef(Id);}

namespace OKCrowdIndependentPrivate {
struct FBinding {
 const USkinnedAsset* Mesh=nullptr;uint32 MeshIndex=0,ActorIndex=0;
 TUniquePtr<FOKGpuSkinPalette> Palette;TArray<uint16> Map;
 TSharedPtr<const FOKGpuSkinPaletteData> RuntimePalette;
 TArray<uint16> RenderBones;
 FPrimitiveComponentId Reference;
 EBoneTransformStorageMode Mode=EBoneTransformStorageMode::Direct;
 uint64 LastRenderFrame=MAX_uint64;
};
struct FRenderState {
 TSharedRef<FOKGpuResidencyBudget,ESPMode::ThreadSafe> Residency=OKGetCrowdGpuResidencyBudget();
 TSharedPtr<FOKGpuAnimationBundle,ESPMode::ThreadSafe> Bundle;
 FOKResidentGpuPose Pose,HistoryPose;TArray<FOKGpuPosePlayback> Playback,PreviousPlayback;
 FOKGpuSkinPaletteBatch PaletteBatch;
 bool BatchPalettes=false;TAtomic<uint64> PaletteDispatches{0};
 TMap<FPrimitiveComponentId,FBinding> Bindings;
 bool Enabled=false,Initialized=false,Reset=true;uint64 Frame=0;
 uint64 BorrowedHistoryFrames=0;
 TAtomic<uint64> RuntimePaletteComponents{0};
 TAtomic<uint64> CurrentPoseEvaluations{0},HistoryPoseEvaluations{0},CurrentPoseReuses{0},HistoryPoseReuses{0},HistoryImmutableUploadBytes{0};
 TAtomic<bool> Ready{false},Requested{false};TAtomic<uint64> Components{0},Passes{0},Fallbacks{0};
 TAtomic<uint64> Actors{0},VerifiedComponents{0},VerifiedMatrices{0},Mismatches{0},VerifiedPreviousMatrices{0};TAtomic<double> MaxError{0};
 TArray<FOKGpuPosePlayback> LastRenderedPlayback;uint32 VerificationSamples=0;
 FPrimitiveComponentId ReferenceComponent;const FSkeletalMeshObject* ReferenceObject=nullptr;
 FPrimitiveComponentId ProbeComponent;FInstancedSkinningSceneExtensionProxy* InstancedProxy=nullptr;
 FSkinningTransformProvider::FProviderContext* PendingDiagnosticContext=nullptr;
 FRDGBuilder* PendingDiagnosticGraph=nullptr;uint64 PendingDiagnosticFrame=MAX_uint64;
 TArray<FMatrix44f> PreviousReferenceTransforms;
 struct FExpected {uint32 Offset,Actor,Bone;float Time;bool Previous=false;FCompressedBoneTransform Transform;};
 TArray<FExpected> Expected;TUniquePtr<FRHIGPUBufferReadback> Readback;
 TAtomic<bool> ExpectedReady{false};bool Pending=false,Verify=false;uint32 ReadBytes=0,Population=0,VerifiedPopulation=0;
};
void Provide(FSkinningTransformProvider::FProviderContext& C,const TSharedRef<FRenderState,ESPMode::ThreadSafe>& R){
 auto NativeFallback=[&]{if(!C.Proxies.IsEmpty())FSkinningSceneExtension::ProvideMeshObjectTransforms(C);};
#if USE_COMPRESSED_BONE_TRANSFORM
 NativeFallback();return;
#else
 if(R->Pending&&R->ExpectedReady.Load()&&R->Readback->IsReady()){
  const auto* Bytes=static_cast<const uint8*>(R->Readback->Lock(R->ReadBytes));double Maximum=R->MaxError.Load();uint64 Errors=0;
  double Worst=0;const FRenderState::FExpected* WorstEntry=nullptr;
  for(const auto& E:R->Expected){const float* Actual=reinterpret_cast<const float*>(Bytes+E.Offset);const float* Expected=reinterpret_cast<const float*>(&E.Transform);for(int K=0;K<12;++K){const double Error=FMath::Abs(double(Actual[K])-Expected[K]);if(!FMath::IsFinite(Actual[K])||Error>.001)++Errors;if(Error>Worst){Worst=Error;WorstEntry=&E;}Maximum=FMath::Max(Maximum,Error);}}
  if(WorstEntry&&Worst>.001){const float* A=reinterpret_cast<const float*>(Bytes+WorstEntry->Offset);const float* E=reinterpret_cast<const float*>(&WorstEntry->Transform);UE_LOG(LogTemp,Warning,TEXT("Independent matrix mismatch actor=%u bone=%u previous=%d gpu_time=%.9f CPU=[%.6f %.6f %.6f %.6f] GPU=[%.6f %.6f %.6f %.6f]"),WorstEntry->Actor,WorstEntry->Bone,WorstEntry->Previous,WorstEntry->Time,E[8],E[9],E[10],E[11],A[8],A[9],A[10],A[11]);}
  uint64 PreviousCount=0;for(const auto& E:R->Expected)if(E.Previous)++PreviousCount;
  R->Readback->Unlock();R->Pending=false;++R->VerificationSamples;R->VerifiedPopulation=R->Population;R->VerifiedComponents.Store(FMath::Max(uint64(R->Population),R->VerifiedComponents.Load()));R->VerifiedMatrices.Store(R->VerifiedMatrices.Load()+R->Expected.Num());R->VerifiedPreviousMatrices.Store(R->VerifiedPreviousMatrices.Load()+PreviousCount);R->Mismatches.Store(R->Mismatches.Load()+Errors);R->MaxError.Store(Maximum);R->Expected.Reset();
  UE_LOG(LogTemp,Display,TEXT("Crowd independent scene buffer verification components=%u max_error=%.9f mismatches=%llu"),R->Population,Maximum,R->Mismatches.Load());
 }
 if(!R->Enabled||!R->Bundle||FRDGBuilder::IsImmediateMode()||!OKIndependentGpuPoseAvailable()||R->Mismatches.Load()!=0){
  if(R->Enabled){++R->Fallbacks;R->Ready.Store(false);}
  R->RuntimePaletteComponents.Store(0);R->Components.Store(0);R->Actors.Store(0);NativeFallback();return;
 }
 FString Error;auto& G=C.GraphBuilder;auto& D=R->Bundle->Data;
 if(!R->Initialized){
  if(!R->Pose.InitializeManaged(G,D.Pose,D.OwnedClips,R->Residency,Error)){UE_LOG(LogTemp,Warning,TEXT("Crowd independent pose initialization rejected: %s"),*Error);++R->Fallbacks;NativeFallback();return;}
  if(!R->HistoryPose.InitializeShared(R->Pose,Error)){UE_LOG(LogTemp,Warning,TEXT("Crowd independent history initialization rejected: %s"),*Error);++R->Fallbacks;NativeFallback();return;}
  const auto Residency=R->Residency->GetStats();UE_LOG(LogTemp,Display,TEXT("Crowd GPU aggregate residency admitted live=%u charged=%llu retiring=%u peak=%llu"),Residency.LiveBanks,Residency.ChargedBytes,Residency.RetiringBanks,Residency.PeakBytes);
  R->Initialized=true;R->Ready.Store(true);
 }
 FOKResidentPoseFrame PreviousFrame,PoseFrame;
 // Native history follows finalized animation poses, not renderer batch clocks.
 // Borrow only a bit-identical requested pose, before current evaluation
 // replaces its pooled buffer. RDG retains that resource for this graph.
 if(R->Pose.TryReuseCurrent(G,R->PreviousPlayback,R->Pose.GetStats().Generation,PreviousFrame))++R->BorrowedHistoryFrames;
 else if(!R->HistoryPose.Evaluate(G,R->PreviousPlayback,++R->Frame,R->HistoryPose.GetStats().Generation,true,PreviousFrame,Error)){++R->Fallbacks;R->Ready.Store(false);NativeFallback();return;}
 if(!R->Pose.Evaluate(G,R->Playback,++R->Frame,R->Pose.GetStats().Generation,R->Reset,PoseFrame,Error)){
  UE_LOG(LogTemp,Warning,TEXT("Crowd independent pose dispatch rejected: %s"),*Error);++R->Fallbacks;R->Components.Store(0);NativeFallback();return;
 }
 PoseFrame.Previous=PreviousFrame.Current;
 const auto CurrentStats=R->Pose.GetStats(),HistoryStats=R->HistoryPose.GetStats();R->CurrentPoseEvaluations.Store(CurrentStats.Evaluations);R->HistoryPoseEvaluations.Store(HistoryStats.Evaluations);R->CurrentPoseReuses.Store(CurrentStats.ReusedFrames);R->HistoryPoseReuses.Store(HistoryStats.ReusedFrames+R->BorrowedHistoryFrames);R->HistoryImmutableUploadBytes.Store(HistoryStats.ImmutableUploadBytes);
 const bool ResetHistory=R->Reset;R->Reset=false;
 // Graph-owned filtered indices survive the deferred CPU scatter setup task.
 auto* Remaining=G.AllocPODArray<FSkinningTransformProvider::FProviderIndirection>(C.Indirections.Num());uint32 Count=0,Executed=0,RuntimeExecuted=0;TSet<uint32> ExecutedActors;
 struct FCheckPlan {const FSkeletalMeshObject* Reference;uint32 Offset,PreviousOffset,Actor;float Time;bool CheckPrevious,NativePreviousDirty;TArray<uint16> Map,RenderBones;EBoneTransformStorageMode Mode;};
 auto Plans=MakeShared<TArray<FCheckPlan>,ESPMode::ThreadSafe>();
 TArray<FOKGpuSkinPaletteJob> PaletteJobs;
 for(const auto& I:C.Indirections){
  const auto* Proxy=C.Proxies.IsEmpty()?R->InstancedProxy:C.Proxies[I.Index];
  auto* B=C.Primitives.IsEmpty()?R->Bindings.Find(R->ProbeComponent):R->Bindings.Find(C.Primitives[I.Index]->Proxy->GetPrimitiveComponentId());
  if(!Proxy){++R->Fallbacks;continue;}
  bool Good=B&&B->Mesh==Proxy->GetSkinnedAsset()&&Proxy->GetLOD()==0;
  const bool ContinuousHistory=B&&B->LastRenderFrame!=MAX_uint64;
  if(Good){
   const auto Mode=Proxy->GetBoneTransformStorageMode();const auto Map=Proxy->GetBoneMap();
   bool Changed=!B->Palette||Mode!=B->Mode||Map.Num()!=B->Map.Num()||(Map.Num()&&FMemory::Memcmp(Map.GetData(),B->Map.GetData(),Map.Num()*2));
   if(Changed){
    B->Palette.Reset();const auto& Actor=R->Bundle->Manifest.Actors[B->ActorIndex];
    const auto* Mesh=B->RuntimePalette?nullptr:&Actor.Meshes[B->MeshIndex];
    const auto& Source=B->RuntimePalette?*B->RuntimePalette:D.Palette;const uint32 First=Mesh?Mesh->Bones.First:0,MeshBones=Mesh?Mesh->Bones.Count:Source.Items.Num();
    const uint32 N=Mode==EBoneTransformStorageMode::BoneMap?Map.Num():MeshBones;
    Good=N>0&&N<=Proxy->GetMaxBoneTransformCount()&&(Mode==EBoneTransformStorageMode::BoneMap||N==Proxy->GetMaxBoneTransformCount());
    FOKGpuSkinPaletteData P;P.PoseCount=D.Palette.PoseCount;
    // Destination indices were canonicalized once on load; full mesh range is
    // in mesh-reference order. Each component keeps its private inverse binds.
    for(uint32 K=0;Good&&K<N;++K){uint32 Bone=Mode==EBoneTransformStorageMode::BoneMap?Map[K]:K;
     if(Bone>=MeshBones){Good=false;break;}auto Item=Source.Items[First+Bone];uint32 Old=Item.Y;
     Item.Y=P.InverseBindRows.Num();Item.Z=K;P.InverseBindRows.Append(Source.InverseBindRows.GetData()+Old,3);P.Items.Add(Item);
    }
    if(Good){B->Palette=MakeUnique<FOKGpuSkinPalette>();Good=B->Palette->Initialize(G,P,PoseFrame.Generation,Error);B->Map.Reset();B->Map.Append(Map.GetData(),Map.Num());B->Mode=Mode;if(!Good)B->Palette.Reset();}
   }
   auto MeshFrame=PoseFrame;
   // A newly registered component starts at current. Existing components use
   // explicit finalized-pose history even across skipped render submissions.
   if(!ContinuousHistory){MeshFrame.Previous=MeshFrame.Current;R->VerificationSamples=0;}
   if(Good&&B->Palette){Good=I.CurrentTransformOffset%16==0&&I.PreviousTransformOffset%16==0;
    if(Good&&R->BatchPalettes)PaletteJobs.Add({B->Palette.Get(),I.CurrentTransformOffset/16,I.PreviousTransformOffset/16,!ContinuousHistory});
    else if(Good){Good=B->Palette->AddPass(G,MeshFrame,B->Palette->GetStats().Generation,C.TransformBuffer,I.CurrentTransformOffset/16,I.PreviousTransformOffset/16,Error);if(Good)++R->PaletteDispatches;}
   }
   else Good=false;
  }
  if(Good){RuntimeExecuted+=B->RuntimePalette.IsValid()?1:0;B->LastRenderFrame=GFrameCounterRenderThread;++Executed;ExecutedActors.Add(B->ActorIndex);if(R->Verify&&(!R->Pending||R->ReferenceComponent.IsValid())){const auto* Reference=B->Reference.IsValid()?R->ReferenceObject:Proxy->GetMeshObject();
   if(Reference)Plans->Add({Reference,I.CurrentTransformOffset,I.PreviousTransformOffset,B->ActorIndex,R->Playback[B->ActorIndex].Times.X,!ResetHistory&&ContinuousHistory,EnumHasAnyFlags(I.DirtyBoneTransforms,EDirtyBoneTransforms::Previous),B->Map,B->RenderBones,B->Mode});
  }}else{Remaining[Count++]=I;if(B)++R->Fallbacks;}
 }
 if(R->BatchPalettes&&!PaletteJobs.IsEmpty()&&!R->PaletteBatch.AddPass(G,PoseFrame,C.TransformBuffer,PaletteJobs,Error)){
  // Rejection is atomic: no batch writes have been submitted. Restore native
  // transforms for this provider context instead of leaving partially updated
  // output slots. Ownership observes the fallback counter on the next tick.
  UE_LOG(LogTemp,Warning,TEXT("Crowd palette batch rejected: %s"),*Error);
  ++R->Fallbacks;R->Ready.Store(false);R->Components.Store(0);R->Actors.Store(0);R->RuntimePaletteComponents.Store(0);
  for(auto& Pair:R->Bindings)Pair.Value.LastRenderFrame=MAX_uint64;
  NativeFallback();return;
 }
 if(R->BatchPalettes&&!PaletteJobs.IsEmpty())++R->PaletteDispatches;
 R->RuntimePaletteComponents.Store(RuntimeExecuted);R->Components.Store(Executed);R->Actors.Store(ExecutedActors.Num());if(Executed){++R->Passes;R->Ready.Store(true);}
 R->LastRenderedPlayback=R->Playback;
 // Initial samples may all precede native history availability. Keep sampling
 // diagnostic runs until previous matrices are actually compared; never count
 // a current-only check as motion-history acceptance.
 const bool TakeSample=R->Verify&&!R->Pending&&Executed&&(Executed>R->VerifiedPopulation||R->VerificationSamples<8||R->VerifiedPreviousMatrices.Load()==0);
 if(TakeSample){
  R->ReadBytes=C.TransformBuffer->Desc.GetSize();R->Population=Executed;R->Pending=true;R->ExpectedReady.Store(false);
  if(!R->Readback)R->Readback=MakeUnique<FRHIGPUBufferReadback>(TEXT("Crowd.IndependentSceneVerification"));
  // Native CPU pose is retained. Read its matrices only in the original
  // scatter setup task, after the renderer's mesh-update prerequisites.
 }
 if(R->Verify&&Executed&&(TakeSample||R->ReferenceComponent.IsValid())){
  C.ScatterUploadBuilder.AddFunction([Plans,R,TakeSample](FRHICommandList&){
   if(TakeSample)R->Expected.Reset();for(const auto& Plan:*Plans){const auto Native=Plan.Reference->GetBoneTransforms();const auto Transforms=Native.CurrentTransforms;
    const bool Probe=R->ReferenceComponent.IsValid();const TConstArrayView<FMatrix44f> Previous=Probe?TConstArrayView<FMatrix44f>(R->PreviousReferenceTransforms):Native.PreviousTransforms;
    const uint32 N=Plan.Mode==EBoneTransformStorageMode::BoneMap?Plan.Map.Num():Transforms.Num();
    for(uint32 K=0;TakeSample&&K<N;++K){const uint32 Bone=Plan.Mode==EBoneTransformStorageMode::BoneMap?Plan.Map[K]:K;if(!Plan.RenderBones.Contains(uint16(Bone)))continue;
     if(Bone>=uint32(Transforms.Num())||uint64(Plan.Offset)+uint64(K+1)*48>R->ReadBytes){R->Mismatches.Store(R->Mismatches.Load()+1);continue;}
     auto& E=R->Expected.AddDefaulted_GetRef();E.Offset=Plan.Offset+K*48;E.Actor=Plan.Actor;E.Bone=Bone;E.Time=Plan.Time;StoreCompressedBoneTransform(&E.Transform,Transforms[Bone]);
     if(Plan.CheckPrevious&&(Probe||Native.UpdateMode!=EPreviousBoneTransformUpdateMode::None||Plan.NativePreviousDirty)&&Previous.IsValidIndex(Bone)){
      if(uint64(Plan.PreviousOffset)+uint64(K+1)*48>R->ReadBytes){R->Mismatches.Store(R->Mismatches.Load()+1);continue;}
      auto& P=R->Expected.AddDefaulted_GetRef();P.Offset=Plan.PreviousOffset+K*48;P.Actor=Plan.Actor;P.Bone=Bone;P.Time=Plan.Time;P.Previous=true;StoreCompressedBoneTransform(&P.Transform,Previous[Bone]);
     }
    }
    if(Probe){R->PreviousReferenceTransforms.Reset();R->PreviousReferenceTransforms.Append(Transforms.GetData(),Transforms.Num());}
   }if(TakeSample)R->ExpectedReady.Store(true);
  });
 }
 if(TakeSample)AddEnqueueCopyPass(G,R->Readback.Get(),C.TransformBuffer,R->ReadBytes);
 FSkinningTransformProvider::FProviderContext CPU(C.Primitives,C.Proxies,{Remaining,int32(Count)},C.SkeletonBatches,C.CurrentTime,G,C.ScatterUploadBuilder,C.TransformBuffer,C.BoneMapBufferSRV);
 FSkinningSceneExtension::ProvideMeshObjectTransforms(CPU);
#endif
}
}

class FOKCrowdIndependentGpu {
public:
 FOKCrowdIndependentGpuStats FinalStats;
 struct FClock {TWeakObjectPtr<UAnimSequence> Clip;double StartWorld=0;float StartTime=0,Time=0,Rate=1;uint64 CpuEvaluations=0;TArray<TPair<TWeakObjectPtr<UOKCrowdRenderSkin>,bool>> Skins;TArray<TWeakObjectPtr<USkeletalMesh>> MeshAssets;TArray<const FSkinWeightVertexBuffer*> WeightBuffers;TArray<FTransform> RelativeTransforms;};
 TMap<TWeakObjectPtr<AKKImportedCharacterActor>,FClock> Clocks;
 uint64 ClockUpdates=0,ClockFallbacks=0,ClockRenderFallbacks=0,FinalClockActors=0,FinalClockCpuEvaluations=0;
 struct FMeshLookup {TWeakObjectPtr<USkeletalMesh> Mesh;int32 Part=INDEX_NONE;TSharedPtr<const FOKGpuSkinPaletteData> RuntimePalette;};
 struct FActorLookup {
  TWeakObjectPtr<UObject> Character;int32 Index=INDEX_NONE;
  TWeakObjectPtr<UAnimSequence> Clip;int32 ClipIndex=INDEX_NONE;float ClipDuration=-1;
  TSet<TWeakObjectPtr<UActorComponent>> OwnedComponents;
  TArray<TWeakObjectPtr<USkeletalMeshComponent>> Skins;
  TMap<TWeakObjectPtr<USkeletalMeshComponent>,FMeshLookup> Meshes;
 };
 TMap<TWeakObjectPtr<AKKImportedCharacterActor>,FActorLookup> ActorLookups;
 struct FRegisteredMesh {TWeakObjectPtr<USkeletalMesh> Mesh;uint32 Actor=MAX_uint32,Part=MAX_uint32;};
 TMap<FPrimitiveComponentId,FRegisteredMesh> RegisteredMeshes;
 uint64 RuntimePaletteBuilds=0,RuntimePaletteRejects=0,CharacterLookups=0,ClipLookups=0,MeshLookups=0,ComponentRebuilds=0;
 TSharedRef<OKCrowdIndependentPrivate::FRenderState,ESPMode::ThreadSafe> Render=MakeShared<OKCrowdIndependentPrivate::FRenderState,ESPMode::ThreadSafe>();
 TSharedRef<OKCrowdIndependentPrivate::FRenderState,ESPMode::ThreadSafe> ProbeRender=MakeShared<OKCrowdIndependentPrivate::FRenderState,ESPMode::ThreadSafe>();
 TWeakObjectPtr<UInstancedSkinnedMeshComponent> Probe;bool ProbeMode=false,VisualCompare=false;FString ProbeMesh;
 TWeakObjectPtr<USkeletalMeshComponent> ProbeSource;
 TSharedPtr<FJsonObject> MaterialManifest;bool MaterialRequested=false,MaterialLibraryOpened=false,MaterialRejected=false,ProbeOriginalMaterials=false;uint32 ProbeMaterialSlots=0;
 TArray<TStrongObjectPtr<UMaterialInterface>> ProbeMaterials;
 TFuture<FOKGpuBundleLoadResult> Pending;TSharedPtr<FOKGpuAnimationBundle,ESPMode::ThreadSafe> Bundle;
 TWeakObjectPtr<UWorld> World;TSet<FPrimitiveComponentId> Registered;
 TMap<TWeakObjectPtr<UOKCrowdRenderSkin>,FDelegateHandle> PoseCallbacks;
 TArray<uint64> Evaluations;TArray<FOKGpuPosePlayback> Playback,PreviousPlayback;
 FString Status=TEXT("Loading independent animation bundle; CPU control retained");bool Installed=false,Released=false,LastRequested=false;
 FString OwnershipAudit;double NextOwnershipAudit=0;
 FOKCrowdGpuLoadStats LoadStats;
};
#include "OKCrowdGpuClock.inl"
FString OKGetCrowdGpuOwnershipAudit(const TSharedPtr<FOKCrowdIndependentGpu>& S){return S?S->OwnershipAudit:FString();}
FOKCrowdGpuLoadStats OKGetCrowdGpuLoadStats(const TSharedPtr<FOKCrowdIndependentGpu>& S){return S?S->LoadStats:FOKCrowdGpuLoadStats();}
namespace {
void AuditWholeCrowdOwnership(FOKCrowdIndependentGpu& S,TConstArrayView<TObjectPtr<AKKImportedCharacterActor>> Actors,TConstArrayView<TObjectPtr<USkeletalMeshComponent>> Leaders){
 if(!FParse::Param(FCommandLine::Get(),TEXT("CrowdIndependentGpuOwnershipAudit"))||!S.Bundle||!S.World.IsValid())return;
 const double Now=S.World->GetTimeSeconds();if(Now<S.NextOwnershipAudit)return;S.NextOwnershipAudit=Now+1;
 auto Report=MakeShared<FJsonObject>();TArray<TSharedPtr<FJsonValue>> Rows;uint32 Ready=0;
 for(int32 A=0;A<Actors.Num();++A){auto* Actor=Actors[A].Get();if(!IsValid(Actor)||!Actor->Character)continue;
  auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("character"),Actor->Character->GetPathName());TSet<FString> Reasons;
  const int32 Index=S.Bundle->Manifest.Actors.IndexOfByPredicate([&](const auto& R){return R.CharacterId==Actor->Character->GetPathName();});
  auto* Leader=Leaders.IsValidIndex(A)?Leaders[A].Get():nullptr;auto* Instance=IsValid(Leader)?Leader->GetSingleNodeInstance():nullptr;auto* Clip=Instance?Cast<UAnimSequence>(Instance->GetCurrentAsset()):nullptr;
  if(Index==INDEX_NONE)Reasons.Add(TEXT("resident_character_missing"));
  else if(!Clip||!S.Bundle->Manifest.Actors[Index].Clips.ContainsByPredicate([&](const auto& C){return C.Id==Clip->GetPathName()&&FMath::Abs(C.Duration-Clip->GetPlayLength())<1e-6;}))Reasons.Add(TEXT("current_clip_missing"));
  uint32 Relevant=0,PaletteCompatible=0,MaterialCompatible=0;
  for(auto* Component:Actor->GetComponents())if(auto* Primitive=Cast<UPrimitiveComponent>(Component)){
   if(Primitive==S.Probe.Get()||!Primitive->IsRegistered()||(!Primitive->IsVisible()&&!Primitive->bCastHiddenShadow))continue;
   auto* Skin=Cast<USkeletalMeshComponent>(Primitive);if(!Skin){if(Cast<UMeshComponent>(Primitive))Reasons.Add(TEXT("non_skeletal_visible_or_shadow_part"));continue;}
   ++Relevant;auto* Mesh=Skin->GetSkeletalMeshAsset();if(!Mesh){Reasons.Add(TEXT("mesh_missing"));continue;}
   if(Index!=INDEX_NONE){FOKGpuSkinPaletteData Palette;FString Why;
    if(OKBuildGpuMeshPalette(S.Bundle->Manifest.Actors[Index],Mesh->GetRefSkeleton(),Mesh->GetRefBasesInvMatrix(),S.Bundle->Data.Palette.PoseCount,Palette,Why))++PaletteCompatible;
    else Reasons.Add(TEXT("private_palette_incompatible"));
   }
   const auto* Render=Mesh->GetResourceForRendering();if(!Render||Render->LODRenderData.IsEmpty())Reasons.Add(TEXT("render_data_missing"));
   if(Skin->GetPredictedLODLevel()!=0)Reasons.Add(TEXT("nonzero_mesh_lod"));
   bool MaterialsGood=Skin->GetNumMaterials()>0;
   for(int32 Slot=0;Slot<Skin->GetNumMaterials();++Slot)MaterialsGood&=Skin->GetMaterial(Slot)!=nullptr;
   if(MaterialsGood)++MaterialCompatible;else Reasons.Add(TEXT("original_material_missing"));
   bool MorphState=Skin->MorphTargetWeights.ContainsByPredicate([](float Weight){return !FMath::IsFinite(Weight)||Weight!=0.f;});
   for(const auto& Curve:Skin->GetMorphTargetCurves())MorphState|=!FMath::IsFinite(Curve.Value)||Curve.Value!=0.f;
   MorphState|=Skin->GetExternalMorphSetsArray().ContainsByPredicate([](const auto& Sets){return !Sets.IsEmpty();});
   if(MorphState)Reasons.Add(TEXT("morph_state_requires_gpu_transfer"));
   if(Skin->GetMeshDeformerInstanceForLOD(0))Reasons.Add(TEXT("mesh_deformer_requires_integration"));
   TArray<UClothingAssetBase*> Cloth;Mesh->GetClothingAssetsInUse(Cloth);if(!Cloth.IsEmpty())Reasons.Add(TEXT("cloth_requires_integration"));
  }
  if(!Relevant)Reasons.Add(TEXT("no_skeletal_parts"));
  // Ownership is granted only by the complete native-render preflight, not
  // inferred from matrix coverage or the unrelated one-part ISMC probe.
  const bool Owned=S.Clocks.Contains(Actor);
  Row->SetBoolField(TEXT("cpu_pose_disabled"),Owned);
  if(!Owned)Reasons.Add(TEXT("independent_clock_not_owned"));
  TArray<FString> Sorted=Reasons.Array();Sorted.Sort();TArray<TSharedPtr<FJsonValue>> Blockers;for(const auto& Reason:Sorted)Blockers.Add(MakeShared<FJsonValueString>(Reason));
  const bool CanOwn=Reasons.IsEmpty();Ready+=CanOwn?1:0;Row->SetBoolField(TEXT("can_disable_cpu_pose"),CanOwn);Row->SetArrayField(TEXT("blockers"),Blockers);
  Row->SetNumberField(TEXT("required_parts"),Relevant);Row->SetNumberField(TEXT("private_palette_compatible_parts"),PaletteCompatible);Row->SetNumberField(TEXT("original_material_compatible_parts"),MaterialCompatible);Rows.Add(MakeShared<FJsonValueObject>(Row));
 }
 Report->SetNumberField(TEXT("schema"),2);Report->SetBoolField(TEXT("cpu_pose_disabled"),Ready>0&&Ready==uint32(Rows.Num()));Report->SetNumberField(TEXT("ready_characters"),Ready);Report->SetArrayField(TEXT("characters"),Rows);
 S.OwnershipAudit.Reset();FJsonSerializer::Serialize(Report,TJsonWriterFactory<>::Create(&S.OwnershipAudit));
}
}
static TSharedPtr<FOKCrowdIndependentGpu> ConfigureCrowdIndependentGpu(UWorld* World,bool Verify){
 check(IsInGameThread());
 auto S=MakeShared<FOKCrowdIndependentGpu>();S->World=World;
 S->ProbeMode=FParse::Param(FCommandLine::Get(),TEXT("CrowdIndependentGpuProviderProbe"));S->ProbeRender->Verify=S->ProbeMode;
 S->VisualCompare=FParse::Param(FCommandLine::Get(),TEXT("CrowdIndependentGpuVisualCompare"));
 FParse::Value(FCommandLine::Get(),TEXT("CrowdIndependentGpuProbeMesh="),S->ProbeMesh);
 S->Render->Verify=Verify;
 S->Render->BatchPalettes=FParse::Param(FCommandLine::Get(),TEXT("CrowdIndependentGpuBatchPalettes"));
 S->ProbeRender->BatchPalettes=S->Render->BatchPalettes;
 FString MaterialFile,MaterialIdentity;
 S->MaterialRequested=FParse::Value(FCommandLine::Get(),TEXT("CrowdIndependentGpuMaterials="),MaterialFile);
 if(S->MaterialRequested){
  TArray<uint8> Bytes;FString Text;
  const int64 FileBytes=IFileManager::Get().FileSize(*MaterialFile);
  if(!S->ProbeMode||!FParse::Value(FCommandLine::Get(),TEXT("CrowdIndependentGpuMaterialsIdentity="),MaterialIdentity)||MaterialIdentity.Len()!=40||FileBytes<=0||FileBytes>16*1024*1024||!FFileHelper::LoadFileToArray(Bytes,*MaterialFile)||Bytes.Num()!=FileBytes||FSHA1::HashBuffer(Bytes.GetData(),Bytes.Num()).ToString()!=MaterialIdentity.ToUpper()){S->MaterialRejected=true;S->Status=TEXT("Prepared material manifest missing or identity mismatch; CPU fallback");return S;}
  FFileHelper::BufferToString(Text,Bytes.GetData(),Bytes.Num());
  if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),S->MaterialManifest)||!S->MaterialManifest){S->MaterialRejected=true;S->Status=TEXT("Prepared material manifest is not valid JSON; CPU fallback");return S;}
  bool Passed=false,Preserved=false,Modified=true;double Schema=0;
  if(!S->MaterialManifest->TryGetBoolField(TEXT("passed"),Passed)||!Passed||!S->MaterialManifest->TryGetBoolField(TEXT("overrides_preserved"),Preserved)||!Preserved||!S->MaterialManifest->TryGetBoolField(TEXT("source_assets_modified"),Modified)||Modified||!S->MaterialManifest->TryGetNumberField(TEXT("schema"),Schema)||Schema!=1){S->MaterialRejected=true;S->Status=TEXT("Invalid prepared material receipt; CPU fallback");return S;}
 }
 return S;
}
TSharedPtr<FOKCrowdIndependentGpu> OKCreateCrowdIndependentGpuFromRuntime(UWorld* World,TUniquePtr<FOKGpuAnimationBundle> Bundle,TArray<FString> SelectedCharacters,bool Verify,uint64 MaxBytes){
 auto S=ConfigureCrowdIndependentGpu(World,Verify);
 if(S->MaterialRejected)return S;
 S->Pending=OKPrepareRuntimeGpuAnimationBundleAsync(MoveTemp(Bundle),MoveTemp(SelectedCharacters),MaxBytes);
 S->Status=TEXT("Runtime private GPU bank validation pending");return S;
}
TSharedPtr<FOKCrowdIndependentGpu> OKCreateCrowdIndependentGpu(UWorld* World,const FString& Cache,const FString& Identity,bool PackingExperiment,bool Verify,const FString& ShardIndex,TArray<FString> SelectedCharacters){
 auto S=ConfigureCrowdIndependentGpu(World,Verify);if(S->MaterialRejected)return S;
 if(PackingExperiment||Identity.Len()!=40||(Cache.IsEmpty()&&ShardIndex.IsEmpty())||(!Cache.IsEmpty()&&!ShardIndex.IsEmpty())){S->Status=TEXT("GPU bridge unavailable: experiment or invalid cache/index identity");return S;}
 for(TCHAR C:Identity)if(!FChar::IsHexDigit(C)){S->Status=TEXT("GPU bridge cache identity is not hexadecimal");return S;}
 FSHAHash Hash;Hash.FromString(Identity);S->Pending=ShardIndex.IsEmpty()?OKLoadGpuAnimationBundleAsync(Cache,Cache+TEXT(".manifest"),Hash):OKLoadSelectedGpuAnimationShardsAsync(ShardIndex,Hash,MoveTemp(SelectedCharacters));return S;
}
void OKTickCrowdIndependentGpu(const TSharedPtr<FOKCrowdIndependentGpu>& S,bool Enabled,TConstArrayView<TObjectPtr<AKKImportedCharacterActor>> Actors,TConstArrayView<TObjectPtr<USkeletalMeshComponent>> Leaders){
 TRACE_CPUPROFILER_EVENT_SCOPE(CrowdIndependentGpuTick);
 if(!S||S->Released)return;check(IsInGameThread());
 S->Render->Requested.Store(Enabled);
 if(S->Pending.IsValid()&&S->Pending.IsReady()){
  auto Result=S->Pending.Consume();S->LoadStats={Result.FromIndex,Result.WorkerThread,Result.Bundle?uint32(Result.Bundle->Manifest.Actors.Num()):0,Result.LoadedShards,Result.MissingSelectedActors,Result.LoadMs};if(!Result.Bundle){S->Status=Result.Error;return;}
  UE_LOG(LogTemp,Display,TEXT("Independent GPU bundle loaded worker=%d index=%d shards=%u missing_selected=%u actors=%d load_ms=%.3f"),Result.WorkerThread,Result.FromIndex,Result.LoadedShards,Result.MissingSelectedActors,Result.Bundle->Manifest.Actors.Num(),Result.LoadMs);
  S->Bundle=MakeShareable(Result.Bundle.Release());auto& D=S->Bundle->Data;
  // Semantic manifest permits arbitrary destination order; canonicalize before
  // render-thread mesh-range lookup, never assume exporter item array order.
  TArray<FUintVector4> Ordered;Ordered.SetNumUninitialized(D.Palette.Items.Num());for(auto I:D.Palette.Items)Ordered[I.Z]=I;D.Palette.Items=MoveTemp(Ordered);
  S->Playback.SetNum(S->Bundle->Manifest.Actors.Num());S->PreviousPlayback.SetNum(S->Bundle->Manifest.Actors.Num());S->Evaluations.Init(MAX_uint64,S->Playback.Num());for(auto& P:S->Playback){P.ClipA=0;P.ClipB=0;}
  auto* World=S->World.Get();if(!World||!World->Scene){S->Status=TEXT("Scene expired during animation load");return;}auto* Scene=World->Scene->GetRenderScene();auto R=S->Render;auto ProbeR=S->ProbeRender;auto Bundle=S->Bundle;auto Playback=S->Playback;
  ENQUEUE_RENDER_COMMAND(AttachIndependentCrowd)([Scene,R,ProbeR,Bundle,Playback](FRHICommandListImmediate&){
   R->Bundle=Bundle;R->Playback=Playback;R->PreviousPlayback=Playback;auto* Provider=Scene->GetExtensionPtr<FSkinningTransformProvider>();if(!Provider)return;
   ProbeR->Bundle=Bundle;ProbeR->Playback=Playback;ProbeR->PreviousPlayback=Playback;
   const auto Id=GetMeshObjectProviderId();if(!Provider->GetProviderIds().Contains(Id))return;Provider->UnregisterProvider(Id);
   Provider->RegisterProvider(Id,FSkinningTransformProvider::FOnProvideTransforms::CreateLambda([R,ProbeR](auto& C){
    // Capture only valid native indirections. The diagnostic source actor is
    // scene-owned and retained until the probe is released; no capacity scan.
    if(ProbeR->ReferenceComponent.IsValid())for(const auto& I:C.Indirections)if(C.Primitives[I.Index]->Proxy->GetPrimitiveComponentId()==ProbeR->ReferenceComponent)ProbeR->ReferenceObject=C.Proxies[I.Index]->GetMeshObject();
    OKCrowdIndependentPrivate::Provide(C,R);
    // Diagnostic native reference is updated in the primitive phase, after
    // instanced batches. Compare the probe in this same graph/clock phase.
    if(ProbeR->PendingDiagnosticContext&&ProbeR->PendingDiagnosticGraph==&C.GraphBuilder&&ProbeR->PendingDiagnosticFrame==GFrameCounterRenderThread){auto* Pending=ProbeR->PendingDiagnosticContext;ProbeR->PendingDiagnosticContext=nullptr;ProbeR->Playback=R->Playback;ProbeR->PreviousPlayback=R->PreviousPlayback;OKCrowdIndependentPrivate::Provide(*Pending,ProbeR);}
    else ProbeR->PendingDiagnosticContext=nullptr;
   }),false);
  });S->Installed=true;S->Status=TEXT("Independent GPU render bridge loaded; native CPU pose retained for validation");
 }
 if(!S->Installed||!S->Bundle)return;
 AuditWholeCrowdOwnership(*S,Actors,Leaders);
 if(auto* Probe=S->Probe.Get()){
  auto* Source=S->ProbeSource.Get();Probe->SetVisibility(Enabled&&Source&&Source->IsVisible());
  // Preserve the source part's near-face/outline section visibility. A palette
  // match alone does not imply the whole mesh should be drawn.
  if(Source&&Enabled){Probe->SetCastShadow(Source->CastShadow);Probe->SetLightingChannels(Source->LightingChannels.bChannel0,Source->LightingChannels.bChannel1,Source->LightingChannels.bChannel2);
   if(auto* Mesh=Source->GetSkeletalMeshAsset())if(const auto* Data=Mesh->GetResourceForRendering())for(int32 LOD=0;LOD<Data->LODRenderData.Num();++LOD)for(int32 Section=0;Section<Data->LODRenderData[LOD].RenderSections.Num();++Section){const int32 Material=Data->LODRenderData[LOD].RenderSections[Section].MaterialIndex;const bool Show=Source->IsMaterialSectionShown(Material,LOD);if(Probe->IsMaterialSectionShown(Material,LOD)!=Show)Probe->ShowMaterialSection(Material,Section,Show,LOD);}
  }
 }
 // OFF must not scan all characters/meshes, copy playback arrays, or enqueue
 // a render update every frame. Retain registrations for the next ON scan.
 if(!Enabled){
  RestoreAllGpuClocks(*S);
  S->Status=TEXT("Independent GPU OFF; native CPU animation restored");
  if(S->LastRequested){auto R=S->Render;auto P=S->ProbeRender;ENQUEUE_RENDER_COMMAND(DisableIndependentCrowd)([R,P](FRHICommandListImmediate&){for(auto State:{R,P}){State->Enabled=false;State->Reset=true;State->Components.Store(0);State->Actors.Store(0);}});}
  S->LastRequested=false;return;
 }
 S->LastRequested=true;
 struct FRegistration {FPrimitiveComponentId Id;USkeletalMesh* Mesh;uint32 Actor,Part;TArray<uint16> RenderBones;TSharedPtr<const FOKGpuSkinPaletteData> RuntimePalette;};TArray<FRegistration> Added;TSet<FPrimitiveComponentId> Seen;TSet<TWeakObjectPtr<AKKImportedCharacterActor>> LiveActors;TSet<TWeakObjectPtr<UOKCrowdRenderSkin>> LiveLeaders;
 for(int32 A=0;A<Actors.Num();++A){auto* Actor=Actors[A].Get();auto* Leader=Leaders.IsValidIndex(A)?Leaders[A].Get():nullptr;if(!IsValid(Actor)||!Actor->Character||!IsValid(Leader))continue;
  LiveActors.Add(Actor);auto& Lookup=S->ActorLookups.FindOrAdd(Actor);
  if(Lookup.Character.Get()!=Actor->Character){
   Lookup=FOKCrowdIndependentGpu::FActorLookup();Lookup.Character=Actor->Character;
   const FString Character=Actor->Character->GetPathName();Lookup.Index=S->Bundle->Manifest.Actors.IndexOfByPredicate([&](const auto& R){return R.CharacterId==Character;});++S->CharacterLookups;
   // A leader can survive a character replacement. Its old callback captured
   // the old manifest actor, so detach it before adopting the new identity.
   if(auto* OldLeader=Cast<UOKCrowdRenderSkin>(Leader))if(const auto* Handle=S->PoseCallbacks.Find(OldLeader)){OldLeader->OnCrowdPoseFinalized.Remove(*Handle);S->PoseCallbacks.Remove(OldLeader);}
  }
  const int32 Index=Lookup.Index;if(Index==INDEX_NONE)continue;const auto& Record=S->Bundle->Manifest.Actors[Index];
  auto* Instance=Leader->GetSingleNodeInstance();auto* Clip=Instance?Cast<UAnimSequence>(Instance->GetCurrentAsset()):nullptr;
  const float Duration=Clip?Clip->GetPlayLength():-1;
  if(Lookup.Clip.Get()!=Clip||Lookup.ClipDuration!=Duration){Lookup.Clip=Clip;Lookup.ClipDuration=Duration;Lookup.ClipIndex=Clip?Record.Clips.IndexOfByPredicate([&](const auto& C){return C.Id==Clip->GetPathName()&&FMath::Abs(C.Duration-Duration)<1e-6;}):INDEX_NONE;++S->ClipLookups;}
  const int32 ClipIndex=Lookup.ClipIndex;if(ClipIndex==INDEX_NONE)continue;
  auto* CrowdLeader=Cast<UOKCrowdRenderSkin>(Leader);if(!CrowdLeader)continue;
  LiveLeaders.Add(CrowdLeader);
  if(!S->PoseCallbacks.Contains(CrowdLeader)){
   TWeakPtr<OKCrowdIndependentPrivate::FRenderState,ESPMode::ThreadSafe> Weak=S->Render;TWeakPtr<OKCrowdIndependentPrivate::FRenderState,ESPMode::ThreadSafe> ProbeWeak=S->ProbeRender;auto Clips=Record.Clips;
   S->PoseCallbacks.Add(CrowdLeader,CrowdLeader->OnCrowdPoseFinalized.AddLambda([Weak,ProbeWeak,Index,Clips](auto* Skin){
    if(Skin->GpuAnimationClockOwned)return;
    auto R=Weak.Pin();auto* Clip=Skin->FinalizedPoseClip.Get();if(!R||!R->Requested.Load()||!Clip)return;
    const int32 Selected=Clips.IndexOfByPredicate([&](const auto& C){return C.Id==Clip->GetPathName();});if(Selected==INDEX_NONE)return;
    FOKGpuPosePlayback P;P.ClipA=Selected;P.ClipB=Selected;P.Times=FVector4f(Skin->FinalizedPoseTime,Skin->FinalizedPoseTime,0,0);
    // Finalization can occur after the scene actor's tick. Queue this alongside
    // that pose's mesh upload, so the render graph sees the evaluated clock,
    // rather than the older GT snapshot or the pre-update single-node clock.
    FOKGpuPosePlayback Previous=P;const int32 PriorClip=Clips.IndexOfByPredicate([&](const auto& C){return Skin->PreviousFinalizedPoseClip.IsValid()&&C.Id==Skin->PreviousFinalizedPoseClip.Get()->GetPathName();});if(PriorClip!=INDEX_NONE){Previous.ClipA=PriorClip;Previous.ClipB=PriorClip;Previous.Times=FVector4f(Skin->PreviousFinalizedPoseTime,Skin->PreviousFinalizedPoseTime,0,0);}
    auto ProbeState=ProbeWeak.Pin();ENQUEUE_RENDER_COMMAND(FinalizedIndependentPoseClock)([R,ProbeState,Index,P,Previous](FRHICommandListImmediate&){if(R->Playback.IsValidIndex(Index)){R->Playback[Index]=P;R->PreviousPlayback[Index]=Previous;}if(ProbeState&&ProbeState->Playback.IsValidIndex(Index)){ProbeState->Playback[Index]=P;ProbeState->PreviousPlayback[Index]=Previous;}});
   }));
  }
  if(CrowdLeader->FinalizedPoseClip.Get()!=Clip)continue;
  const int32 PriorClip=Record.Clips.IndexOfByPredicate([&](const auto& C){return CrowdLeader->PreviousFinalizedPoseClip.IsValid()&&C.Id==CrowdLeader->PreviousFinalizedPoseClip.Get()->GetPathName();});auto& Prior=S->PreviousPlayback[Index];Prior.ClipA=PriorClip==INDEX_NONE?ClipIndex:PriorClip;Prior.ClipB=Prior.ClipA;Prior.Times=FVector4f(CrowdLeader->PreviousFinalizedPoseTime,CrowdLeader->PreviousFinalizedPoseTime,0,0);
  if(S->Evaluations[Index]!=CrowdLeader->FinalizedPoseEvaluation){S->Evaluations[Index]=CrowdLeader->FinalizedPoseEvaluation;S->Playback[Index].ClipA=ClipIndex;S->Playback[Index].ClipB=ClipIndex;S->Playback[Index].Times=FVector4f(CrowdLeader->FinalizedPoseTime,CrowdLeader->FinalizedPoseTime,0,0);}
  // Compare the complete owned-component set, not just its count. Replacing
  // one component with another in the same frame must invalidate the cache.
  const auto& Owned=Actor->GetComponents();bool Rebuild=Owned.Num()!=Lookup.OwnedComponents.Num();
  if(!Rebuild)for(auto* Component:Owned)if(!Lookup.OwnedComponents.Contains(Component)){Rebuild=true;break;}
  if(Rebuild){
   TRACE_CPUPROFILER_EVENT_SCOPE(CrowdIndependentComponentRebuild);
   Lookup.OwnedComponents.Reset();Lookup.Skins.Reset();Lookup.Meshes.Reset();
   for(auto* Component:Owned){Lookup.OwnedComponents.Add(Component);if(auto* Skin=Cast<USkeletalMeshComponent>(Component))Lookup.Skins.Add(Skin);}++S->ComponentRebuilds;
  }
  for(const auto& WeakSkin:Lookup.Skins){auto* Skin=WeakSkin.Get();if(!Skin||!Skin->IsRegistered())continue;auto* Mesh=Skin->GetSkeletalMeshAsset();if(!Mesh)continue;
   auto& MeshLookup=Lookup.Meshes.FindOrAdd(Skin);
   if(MeshLookup.Mesh.Get()!=Mesh){MeshLookup.Mesh=Mesh;MeshLookup.Part=Record.Meshes.IndexOfByPredicate([&](const auto& M){return M.AssetPath==Mesh->GetPathName();});++S->MeshLookups;MeshLookup.RuntimePalette.Reset();if(MeshLookup.Part==INDEX_NONE&&FParse::Param(FCommandLine::Get(),TEXT("CrowdIndependentGpuRuntimePalettes"))){auto Palette=MakeShared<FOKGpuSkinPaletteData>();FString Error;if(OKBuildGpuMeshPalette(Record,Mesh->GetRefSkeleton(),Mesh->GetRefBasesInvMatrix(),S->Bundle->Data.Palette.PoseCount,*Palette,Error)){MeshLookup.RuntimePalette=Palette;++S->RuntimePaletteBuilds;}else{++S->RuntimePaletteRejects;UE_LOG(LogTemp,Display,TEXT("Independent runtime palette retains CPU mesh=%s reason=%s"),*Mesh->GetPathName(),*Error);}}}
   const int32 Part=MeshLookup.Part;if(Part==INDEX_NONE&&!MeshLookup.RuntimePalette)continue;const auto Id=Skin->GetPrimitiveSceneId();if(!Id.IsValid())continue;Seen.Add(Id);
   const auto* Registered=S->RegisteredMeshes.Find(Id);
   const bool AlreadyRegistered=S->Registered.Contains(Id)&&Registered&&Registered->Mesh.Get()==Mesh&&Registered->Actor==uint32(Index)&&Registered->Part==uint32(Part);
   // A cached native registration may become visible after a detail transition.
   // It must still be considered for the requested diagnostic probe.
   const bool ProbeCandidate=S->ProbeMode&&!S->Probe.IsValid()&&!S->MaterialRejected&&Skin->IsVisible()&&(S->ProbeMesh.IsEmpty()||S->ProbeMesh==Mesh->GetPathName());
   if(AlreadyRegistered&&!ProbeCandidate)continue;
   const auto* M=Part==INDEX_NONE?nullptr:&Record.Meshes[Part];const auto& Inverse=Mesh->GetRefBasesInvMatrix();bool Match=M?uint32(Inverse.Num())==M->Bones.Count:MeshLookup.RuntimePalette.IsValid();
   for(int B=0;M&&Match&&B<Inverse.Num();++B){const auto& Item=S->Bundle->Data.Palette.Items[M->Bones.First+B];for(int Row=0;Row<3;++Row){const auto& Expected=S->Bundle->Data.Palette.InverseBindRows[Item.Y+Row];FVector4f Actual(Inverse[B].M[0][Row],Inverse[B].M[1][Row],Inverse[B].M[2][Row],Inverse[B].M[3][Row]);Match&=FMemory::Memcmp(&Actual,&Expected,16)==0;}}
   const auto* RenderData=Mesh->GetResourceForRendering();Match&=RenderData&&!RenderData->LODRenderData.IsEmpty();
   if(Match){TArray<uint16> Bones;for(const auto& Section:RenderData->LODRenderData[0].RenderSections)for(uint16 Bone:Section.BoneMap)Bones.AddUnique(Bone);
    if(ProbeCandidate){
     TArray<UMaterialInterface*> Materials;
     if(S->MaterialRequested){
      const TArray<TSharedPtr<FJsonValue>>* MeshRows=nullptr;const TArray<TSharedPtr<FJsonValue>>* Bindings=nullptr;
      if(S->MaterialManifest->TryGetArrayField(TEXT("meshes"),MeshRows))for(const auto& Row:*MeshRows){const auto Object=Row->AsObject();FString Path;if(Object&&Object->TryGetStringField(TEXT("mesh"),Path)&&Path==Mesh->GetPathName()){Object->TryGetArrayField(TEXT("bindings"),Bindings);break;}}
      // A partial fixture is allowed to skip unmatched meshes. A matched mesh
      // must retain every slot and its exact source material identity.
      if(!Bindings)continue;
      if(!S->MaterialLibraryOpened)S->MaterialLibraryOpened=FShaderCodeLibrary::OpenLibrary(TEXT("GpuPoseAudit"),FPaths::ProjectContentDir());
      bool Valid=S->MaterialLibraryOpened&&Bindings->Num()==Skin->GetNumMaterials();Materials.SetNumZeroed(Skin->GetNumMaterials());
      for(int32 Slot=0;Valid&&Slot<Bindings->Num();++Slot){const auto Binding=(*Bindings)[Slot]->AsObject();double SavedSlot=-1;FString SourcePath,PreparedPath;auto* SourceMaterial=Skin->GetMaterial(Slot);
       Valid=Binding&&Binding->TryGetNumberField(TEXT("slot"),SavedSlot)&&SavedSlot==Slot&&Binding->TryGetStringField(TEXT("source"),SourcePath)&&SourceMaterial&&SourceMaterial->GetPathName()==SourcePath&&Binding->TryGetStringField(TEXT("prepared"),PreparedPath)&&PreparedPath.StartsWith(TEXT("/Game/Scenes/CrowdRoom/GpuMaterials/"));
       if(Valid){Materials[Slot]=LoadObject<UMaterialInterface>(nullptr,*PreparedPath);Valid=Materials[Slot]&&Materials[Slot]->GetUsageByFlag(MATUSAGE_InstancedSkinnedMesh)&&Materials[Slot]->GetBlendMode()==SourceMaterial->GetBlendMode()&&Materials[Slot]->IsTwoSided()==SourceMaterial->IsTwoSided()&&Materials[Slot]->GetShadingModels()==SourceMaterial->GetShadingModels();}
      }
      if(!Valid){S->MaterialRejected=true;S->Status=TEXT("Prepared material shader/slot/source validation failed; original CPU character retained");continue;}
      for(auto* Material:Materials)S->ProbeMaterials.Emplace(Material);S->ProbeOriginalMaterials=true;S->ProbeMaterialSlots=Materials.Num();
     }
     auto* Probe=NewObject<UInstancedSkinnedMeshComponent>(Actor);Actor->AddInstanceComponent(Probe);Probe->SetupAttachment(Skin);Probe->SetSkinnedAssetAndUpdate(Mesh);Probe->SetTransformProvider(NewObject<UOKIndependentTransformProvider>(Probe));Probe->SetAnimationMinScreenSize(-1);Probe->SetCollisionEnabled(ECollisionEnabled::NoCollision);Probe->SetCastShadow(false);Probe->SetComponentTickEnabled(false);
     Probe->SetTranslucentSortPriority(Skin->TranslucencySortPriority);
     auto* ProbeProvider=CastChecked<UOKIndependentTransformProvider>(Probe->GetTransformProvider());
     const FBox ReferenceBounds=Skin->Bounds.GetBox().TransformBy(Skin->GetComponentTransform().ToInverseMatrixWithScale());
     ProbeProvider->SetDiagnosticBounds(ReferenceBounds);
     // Diagnostic only: no original material mutation or production adoption.
     for(int MaterialIndex=0;MaterialIndex<Skin->GetNumMaterials();++MaterialIndex)Probe->SetMaterial(MaterialIndex,S->MaterialRequested?Materials[MaterialIndex]:UMaterial::GetDefaultMaterial(MD_Surface));
     Probe->AddInstance(FTransform(FVector(0,0,0)),0);Probe->RegisterComponent();Probe->SetComponentTickEnabled(false);S->Probe=Probe;S->ProbeSource=Skin;if(auto* ReferenceSkin=Cast<UOKCrowdRenderSkin>(Skin))ReferenceSkin->ForceDiagnosticRenderUpload=true;
     auto P=S->ProbeRender;const auto ProbeId=Probe->GetPrimitiveSceneId();const auto OwnedId=Probe->GetTransformProvider()->GetTransformProviderID();auto* RenderScene=S->World->Scene->GetRenderScene();auto ProbeBones=Bones;
     ENQUEUE_RENDER_COMMAND(RegisterIndependentProbe)([P,RenderScene,OwnedId,ProbeId,Id,Mesh,Index,Part,RuntimePalette=MeshLookup.RuntimePalette,ProbeBones=MoveTemp(ProbeBones)](FRHICommandListImmediate&){
      auto* Provider=RenderScene->GetExtensionPtr<FSkinningTransformProvider>();if(!Provider)return;
      Provider->RegisterProvider(OwnedId,FSkinningTransformProvider::FOnProvideTransforms::CreateLambda([P,OwnedId](auto& C){P->InstancedProxy=GIndependentProxyRegistry.FindRef(OwnedId);
       if(P->Verify){P->PendingDiagnosticContext=C.GraphBuilder.template AllocObject<FSkinningTransformProvider::FProviderContext>(C);P->PendingDiagnosticGraph=&C.GraphBuilder;P->PendingDiagnosticFrame=GFrameCounterRenderThread;}
       else OKCrowdIndependentPrivate::Provide(C,P);
      }),true);
      P->ReferenceComponent=Id;P->ProbeComponent=ProbeId;auto& B=P->Bindings.FindOrAdd(ProbeId);B.Mesh=Mesh;B.ActorIndex=Index;B.MeshIndex=Part;B.RuntimePalette=RuntimePalette;B.RenderBones=ProbeBones;B.Reference=Id;
     });
    }
    if(!AlreadyRegistered)Added.Add({Id,Mesh,uint32(Index),uint32(Part),MoveTemp(Bones),MeshLookup.RuntimePalette});}
  }
 }
 for(auto It=S->ActorLookups.CreateIterator();It;++It)if(!LiveActors.Contains(It.Key()))It.RemoveCurrent();
 for(auto It=S->PoseCallbacks.CreateIterator();It;++It)if(!LiveLeaders.Contains(It.Key())){if(auto* Leader=It.Key().Get())Leader->OnCrowdPoseFinalized.Remove(It.Value());It.RemoveCurrent();}
 TArray<FPrimitiveComponentId> Removed;for(const auto Id:S->Registered)if(!Seen.Contains(Id))Removed.Add(Id);for(const auto Id:Removed){S->Registered.Remove(Id);S->RegisteredMeshes.Remove(Id);}for(const auto& B:Added){S->Registered.Add(B.Id);S->RegisteredMeshes.Add(B.Id,{B.Mesh,B.Actor,B.Part});}
 TickOwnedGpuClocks(*S,Actors,Leaders);
 if(!S->Clocks.IsEmpty())S->Status=FString::Printf(TEXT("Independent GPU pose and clock active: %d actors; original native mesh/material rendering"),S->Clocks.Num());
 else S->Status=TEXT("Independent GPU matrix bridge active; CPU animation retained");
 auto R=S->Render;auto P=S->ProbeRender;auto Playback=S->Playback;auto PreviousPlayback=S->PreviousPlayback;const bool NativeOverride=Enabled&&!S->VisualCompare;
 ENQUEUE_RENDER_COMMAND(UpdateIndependentCrowd)([R,P,Enabled,NativeOverride,Added=MoveTemp(Added),Removed=MoveTemp(Removed),Playback=MoveTemp(Playback),PreviousPlayback=MoveTemp(PreviousPlayback)](FRHICommandListImmediate&){
  if(P->Enabled!=Enabled){P->Reset=true;}P->Enabled=Enabled;P->Playback=Playback;P->PreviousPlayback=PreviousPlayback;
  if(R->Enabled!=NativeOverride){R->Reset=true;R->Components.Store(0);R->Actors.Store(0);}R->Enabled=NativeOverride;R->Playback=Playback;R->PreviousPlayback=PreviousPlayback;
  for(auto Id:Removed)R->Bindings.Remove(Id);for(const auto& B:Added){auto& Binding=R->Bindings.FindOrAdd(B.Id);if(Binding.Mesh!=B.Mesh||Binding.ActorIndex!=B.Actor||Binding.MeshIndex!=B.Part){Binding.Palette.Reset();Binding.LastRenderFrame=MAX_uint64;}Binding.RuntimePalette=B.RuntimePalette;Binding.Mesh=B.Mesh;Binding.ActorIndex=B.Actor;Binding.MeshIndex=B.Part;Binding.RenderBones=B.RenderBones;}
 });
}
bool OKCaptureCrowdIndependentGpuComparison(const TSharedPtr<FOKCrowdIndependentGpu>& S,const FString& Prefix,FString& Error){
 if(!S||!S->VisualCompare||!S->ProbeOriginalMaterials||S->ProbeRender->Components.Load()==0){Error=TEXT("Original-material GPU-only probe not ready");return false;}
 auto* World=S->World.Get();auto* Probe=S->Probe.Get();auto* Source=S->ProbeSource.Get();if(!World||!Probe||!Source||!Source->IsVisible()){Error=TEXT("Comparison source expired or invisible");return false;}
 auto* Actor=Source->GetOwner();auto* PC=World->GetFirstPlayerController();auto* Camera=PC?Cast<ACameraActor>(PC->GetViewTarget()):nullptr;if(!Actor||!Camera){Error=TEXT("Comparison camera unavailable");return false;}
 const TCHAR* Views[]={TEXT("front"),TEXT("side"),TEXT("back")};constexpr int32 Size=1024;
 for(const TCHAR* View:Views)for(const TCHAR* Mode:{TEXT("cpu"),TEXT("gpu")})if(IFileManager::Get().FileExists(*(Prefix+TEXT(".")+View+TEXT(".")+Mode+TEXT(".png")))){Error=TEXT("Fresh visual comparison prefix required");return false;}
 TStrongObjectPtr<UTextureRenderTarget2D> Target(NewObject<UTextureRenderTarget2D>(World));Target->RenderTargetFormat=RTF_RGBA8;Target->InitAutoFormat(Size,Size);Target->UpdateResourceImmediate(true);
 TStrongObjectPtr<USceneCaptureComponent2D> Capture(NewObject<USceneCaptureComponent2D>(World));Capture->bCaptureEveryFrame=false;Capture->bCaptureOnMovement=false;Capture->bAlwaysPersistRenderingState=true;Capture->CaptureSource=SCS_FinalColorLDR;Capture->TextureTarget=Target.Get();Capture->FOVAngle=50;Capture->PostProcessSettings=Camera->GetCameraComponent()->PostProcessSettings;Capture->PostProcessBlendWeight=1;
 // Both sides use the same non-temporal capture AA. This isolates skin/material
 // differences from different TAA history; full viewport TAA acceptance is later.
 Capture->ShowFlags.SetTemporalAA(false);Capture->ShowFlags.SetMotionBlur(false);Capture->RegisterComponentWithWorld(World);
 float FocusHeight=90,Distance=350;FParse::Value(FCommandLine::Get(),TEXT("CrowdVisualFocusHeight="),FocusHeight);FParse::Value(FCommandLine::Get(),TEXT("CrowdVisualDistance="),Distance);
 if(!FMath::IsFinite(FocusHeight)||!FMath::IsFinite(Distance)||Distance<30||Distance>1000){Error=TEXT("Invalid diagnostic capture camera parameters");return false;}
 bool Good=true;const FVector Focus=Actor->GetActorLocation()+FVector(0,0,FocusHeight);
 for(int32 View=0;Good&&View<3;++View){const FVector Offset=Actor->GetActorRotation().RotateVector(FRotator(0,View*90,0).RotateVector(FVector(0,Distance,0)));Capture->SetWorldLocationAndRotation(Focus+Offset,(-Offset).Rotation());
  for(int32 Mode=0;Good&&Mode<2;++Mode){Capture->HiddenComponents.Reset();Capture->HideComponent(Mode==0?static_cast<UPrimitiveComponent*>(Probe):static_cast<UPrimitiveComponent*>(Source));
   for(int Warm=0;Warm<3;++Warm){Capture->CaptureScene();FlushRenderingCommands();}
   TArray<FColor> Pixels;FReadSurfaceDataFlags Flags(RCM_UNorm);Flags.SetLinearToGamma(false);Good=Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels,Flags)&&Pixels.Num()==Size*Size;
   const FString File=Prefix+TEXT(".")+Views[View]+(Mode==0?TEXT(".cpu.png"):TEXT(".gpu.png"));if(Good)Good=FImageUtils::SaveImageByExtension(*File,FImageView(Pixels.GetData(),Size,Size));
  }
 }
 Capture->DestroyComponent();if(!Good){Error=TEXT("Visual capture/readback/save failed");return false;}
 auto Report=MakeShared<FJsonObject>();Report->SetBoolField(TEXT("captured"),true);Report->SetBoolField(TEXT("full_gpu_character"),false);Report->SetBoolField(TEXT("native_bridge_disabled"),!S->Render->Enabled);Report->SetBoolField(TEXT("same_frame"),true);Report->SetBoolField(TEXT("temporal_aa"),false);Report->SetStringField(TEXT("source_mesh"),Source->GetSkeletalMeshAsset()->GetPathName());Report->SetNumberField(TEXT("material_slots"),S->ProbeMaterialSlots);Report->SetNumberField(TEXT("native_gpu_override_components"),S->Render->Components.Load());Report->SetNumberField(TEXT("gpu_probe_components"),S->ProbeRender->Components.Load());FString Json;
 return FJsonSerializer::Serialize(Report,TJsonWriterFactory<>::Create(&Json))&&FFileHelper::SaveStringToFile(Json,*(Prefix+TEXT(".capture.json")));
}
void OKReleaseCrowdIndependentGpu(const TSharedPtr<FOKCrowdIndependentGpu>& S){if(!S)return;auto ClockStats=OKGetCrowdIndependentGpuStats(S);S->FinalStats=ClockStats;S->FinalClockActors=ClockStats.ClockOwnedActors;S->FinalClockCpuEvaluations=ClockStats.ClockCpuEvaluations;RestoreAllGpuClocks(*S);S->Released=true;if(auto* ReferenceSkin=Cast<UOKCrowdRenderSkin>(S->ProbeSource.Get()))ReferenceSkin->ForceDiagnosticRenderUpload=false;if(auto* Probe=S->Probe.Get())Probe->DestroyComponent();for(const auto& Callback:S->PoseCallbacks)if(auto* Skin=Callback.Key.Get())Skin->OnCrowdPoseFinalized.Remove(Callback.Value);S->PoseCallbacks.Reset();auto R=S->Render;auto P=S->ProbeRender;ENQUEUE_RENDER_COMMAND(ReleaseIndependentCrowd)([R,P](FRHICommandListImmediate&){P->Enabled=false;P->Bindings.Reset();P->HistoryPose.Release();P->Pose.Release();P->Bundle.Reset();R->Enabled=false;R->Components.Store(0);R->Actors.Store(0);R->Ready.Store(false);R->Bindings.Reset();R->HistoryPose.Release();R->Pose.Release();R->Bundle.Reset();});}
FOKCrowdIndependentGpuStats OKGetCrowdIndependentGpuStats(const TSharedPtr<FOKCrowdIndependentGpu>& S){FOKCrowdIndependentGpuStats Stats;if(!S){Stats.Status=TEXT("No independent animation cache selected; CPU fallback");return Stats;}if(S->Released)return S->FinalStats;Stats.ClockOwnedActors=S->Released?S->FinalClockActors:S->Clocks.Num();Stats.ClockUpdates=S->ClockUpdates;Stats.ClockFallbacks=S->ClockFallbacks;Stats.ClockCpuEvaluations=S->Released?S->FinalClockCpuEvaluations:0;for(const auto& Pair:S->Clocks)for(const auto& Saved:Pair.Value.Skins)if(auto* Skin=Saved.Key.Get())if(!Skin->LeaderPoseComponent.IsValid())Stats.ClockCpuEvaluations+=Skin->PoseEvaluations-Pair.Value.CpuEvaluations;Stats.RuntimePaletteBuilds=S->RuntimePaletteBuilds;Stats.RuntimePaletteRejects=S->RuntimePaletteRejects;Stats.RuntimePaletteComponents=S->Render->RuntimePaletteComponents.Load();Stats.CharacterLookups=S->CharacterLookups;Stats.ClipLookups=S->ClipLookups;Stats.MeshLookups=S->MeshLookups;Stats.ComponentRebuilds=S->ComponentRebuilds;auto& R=*S->Render;Stats.PaletteDispatches=R.PaletteDispatches.Load();Stats.CurrentPoseEvaluations=R.CurrentPoseEvaluations.Load();Stats.HistoryPoseEvaluations=R.HistoryPoseEvaluations.Load();Stats.CurrentPoseReuses=R.CurrentPoseReuses.Load();Stats.HistoryPoseReuses=R.HistoryPoseReuses.Load();Stats.HistoryImmutableUploadBytes=R.HistoryImmutableUploadBytes.Load();Stats.Ready=R.Ready.Load();Stats.Components=R.Components.Load();Stats.Passes=R.Passes.Load();Stats.Fallbacks=R.Fallbacks.Load();Stats.Actors=R.Actors.Load();Stats.VerifiedComponents=R.VerifiedComponents.Load();Stats.VerifiedMatrices=R.VerifiedMatrices.Load();Stats.VerifiedPreviousMatrices=R.VerifiedPreviousMatrices.Load();Stats.Mismatches=R.Mismatches.Load();Stats.MaxError=R.MaxError.Load();if(auto* Probe=S->Probe.Get())Stats.ProbeCpuPoseAbsent=!Probe->GetClass()->IsChildOf(USkeletalMeshComponent::StaticClass())&&!Probe->IsComponentTickEnabled()&&Probe->GetTransformProvider()&&Probe->GetTransformProvider()->IsGpuOnly();Stats.ProbeOriginalMaterials=S->ProbeOriginalMaterials;Stats.ProbeMaterialSlots=S->ProbeMaterialSlots;Stats.ProbeComponents=S->ProbeRender->Components.Load();Stats.ProbeMatrices=S->ProbeRender->VerifiedMatrices.Load();Stats.ProbePreviousMatrices=S->ProbeRender->VerifiedPreviousMatrices.Load();Stats.ProbeMismatches=S->ProbeRender->Mismatches.Load();Stats.ProbeMaxError=S->ProbeRender->MaxError.Load();Stats.Status=S->Status;return Stats;}

bool OKCrowdGpuHasClip(const TSharedPtr<FOKCrowdIndependentGpu>& S,const AKKImportedCharacterActor* Actor,const UAnimSequence* Clip){
 if(!S||!S->Bundle||!Actor||!Actor->Character||!Clip)return false;
 const auto* Record=S->Bundle->Manifest.Actors.FindByPredicate([&](const auto& A){return A.CharacterId==Actor->Character->GetPathName();});
 return Record&&Record->Clips.ContainsByPredicate([&](const auto& C){return C.Id==Clip->GetPathName()&&FMath::Abs(C.Duration-Clip->GetPlayLength())<1e-6;});
}
