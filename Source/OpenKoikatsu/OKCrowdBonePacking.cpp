#include "OKCrowdBonePacking.h"
#include "Engine/World.h"
#include "ScenePrivate.h"
#include "Skinning/SkinningTransformProvider.h"
#include "Skinning/SkinningSceneExtension.h"
#include "SkinningSceneExtensionProxy.h"
#include "SkeletalRenderPublic.h"
#include "UnifiedBuffer.h"
#include "Async/ParallelFor.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include <atomic>
#include "HAL/IConsoleManager.h"
#include "OKGpuBonePacking.h"
#include "Engine/SkinnedAsset.h"
#include "RenderGraphUtils.h"
#include "RHIGPUReadback.h"
namespace {
std::atomic<bool> Installed{false},Verified{false};
std::atomic<uint64> Passes{0},JobCount{0},BoneCount{0},VerifiedJobs{0},Mismatches{0};
std::atomic<uint64> WorkCount{0};
std::atomic<uint64> ReusedBones{0};
std::atomic<uint64> RawBytes{0},UniqueRawBytes{0},WrittenBytes{0},AllocatedBytes{0},FootprintSamples{0};
std::atomic<uint64> GpuPasses{0},GpuFallbacks{0};
std::atomic<uint64> GpuVerifiedProxies{0};
std::atomic<int32> VerifiedMode{-1};
TAutoConsoleVariable<int32> PackingMode(TEXT("r.OKCrowd.BonePackingMode"),1,TEXT("0: engine, 1: component jobs, 2: bone ranges, 3: duplicate-pose reuse, 4: footprint probe, 5: GPU matrix gather"),ECVF_RenderThreadSafe);
// Allocation remains serial: Add_GetRef modifies the scatter uploader's cursor.
// Workers only write disjoint, already allocated spans and use the exact engine
// compression routine, including previous-pose and reference-pose semantics.
struct FJob {FCompressedBoneTransform* Output;TConstArrayView<FMatrix44f> Source;TConstArrayView<uint16> Map;uint32 Count;};
bool SamePose(const FJob& A,const FJob& B){
 return A.Count==B.Count&&A.Map.GetData()==B.Map.GetData()&&A.Map.Num()==B.Map.Num()&&A.Source.Num()==B.Source.Num()
  &&(A.Source.GetData()==B.Source.GetData()||(A.Source.Num()&&FMemory::Memcmp(A.Source.GetData(),B.Source.GetData(),A.Source.Num()*sizeof(FMatrix44f))==0));
}
bool VerifyDuplicatePoseInputs(){
 TArray<FMatrix44f> A;A.Init(FMatrix44f::Identity,3);TArray<FMatrix44f> B=A;
 TArray<uint16> Map{2,0};TArray<uint16> OtherMap{0,2};
 FJob J{nullptr,A,Map,2},K{nullptr,B,Map,2};if(!SamePose(J,K))return false;
 B[2].M[3][0]=1.f;if(SamePose(J,K))return false;B[2]=A[2];
 K.Map=OtherMap;if(SamePose(J,K))return false;K.Map=Map;
 K.Count=1;if(SamePose(J,K))return false;K.Count=2;
 // Numeric equality is insufficient: signed zero must preserve its bits.
 B[0].M[0][1]=-0.f;if(SamePose(J,K))return false;
 J.Source={};K.Source={};return SamePose(J,K);
}
void PackRange(const FJob& J,FCompressedBoneTransform* Output,uint32 Begin,uint32 End){
 for(uint32 I=Begin;I<End;++I){if(J.Source.IsEmpty())SetCompressedBoneTransformIdentity(Output[I]);else {const auto& M=J.Source[J.Map.IsEmpty()?I:J.Map[I]];StoreCompressedBoneTransform(Output+I,M);}}
}
bool VerifyShortMapRanges(){
 // Deliberately use a shorter active map than the allocation and cross a
 // 512-bone boundary. Sentinels catch writes into unused LOD capacity.
 TArray<FMatrix44f> Source;Source.SetNum(1024);
 for(int32 I=0;I<Source.Num();++I){Source[I]=FMatrix44f::Identity;Source[I].M[3][0]=float(I);Source[I].M[1][2]=float(I)*.001f;}
 TArray<uint16> Map;for(int32 I=0;I<517;++I)Map.Add(uint16((I*37)%Source.Num()));
 TArray<FCompressedBoneTransform> Actual,Expected;Actual.SetNumUninitialized(545);Expected.SetNumUninitialized(545);
 FMemory::Memset(Actual.GetData(),0xa5,Actual.Num()*sizeof(FCompressedBoneTransform));FMemory::Memcpy(Expected.GetData(),Actual.GetData(),Actual.Num()*sizeof(FCompressedBoneTransform));
 FJob J{Actual.GetData(),Source,Map,uint32(Map.Num())};
 PackRange(J,J.Output,0,512);PackRange(J,J.Output,512,J.Count);
 uint32 D=0;for(uint16 Bone:Map)StoreCompressedBoneTransform(&Expected[D++],Source[Bone]);
 if(FMemory::Memcmp(Actual.GetData(),Expected.GetData(),Actual.Num()*sizeof(FCompressedBoneTransform)))return false;
 J.Source={};J.Map={};J.Count=Actual.Num();PackRange(J,J.Output,0,J.Count);
 for(auto& M:Expected)SetCompressedBoneTransformIdentity(M);
 return FMemory::Memcmp(Actual.GetData(),Expected.GetData(),Actual.Num()*sizeof(FCompressedBoneTransform))==0;
}
struct FGpuVerification {
 struct FRange {uint32 Offset;TArray<FCompressedBoneTransform> Expected;};
 TUniquePtr<FRHIGPUBufferReadback> Readback;
 TArray<FRange> Ranges;uint32 Bytes=0;uint64 Population=0,LastPopulation=0;
 bool Pending=false;std::atomic<bool> ExpectedReady{false};
};
void ProvideGpu(FSkinningTransformProvider::FProviderContext& C,bool Verify,const TSharedRef<FGpuVerification,ESPMode::ThreadSafe>& V){
#if USE_COMPRESSED_BONE_TRANSFORM
 ++GpuFallbacks;FSkinningSceneExtension::ProvideMeshObjectTransforms(C);return;
#else
 if(V->Pending&&V->ExpectedReady.load()&&V->Readback->IsReady()){
  const auto* Data=static_cast<const uint8*>(V->Readback->Lock(V->Bytes));uint64 Errors=0;
  for(const auto& R:V->Ranges)if(!R.Expected.IsEmpty()&&FMemory::Memcmp(Data+R.Offset,R.Expected.GetData(),R.Expected.Num()*sizeof(FCompressedBoneTransform)))++Errors;
  V->Readback->Unlock();V->Pending=false;V->LastPopulation=V->Population;V->Ranges.Reset();
  Mismatches.fetch_add(Errors);VerifiedJobs.store(V->Population);Verified.store(Mismatches.load()==0);
  GpuVerifiedProxies.store(Errors?0:V->Population);
  UE_LOG(LogTemp,Display,TEXT("Crowd GPU matrix verification population=%llu mismatches=%llu"),V->Population,Errors);
 }
 if(FRDGBuilder::IsImmediateMode()||!OKGpuBonePackingAvailable()||Mismatches.load()!=0){++GpuFallbacks;FSkinningSceneExtension::ProvideMeshObjectTransforms(C);return;}
 struct FLayout {uint32 RawOffset,RawCapacity,WorkOffset,Chunks;};
 auto L=MakeShared<TArray<FLayout>,ESPMode::ThreadSafe>();uint32 RawSize=0,LayoutGpuWorkCount=0;
 for(const auto& I:C.Indirections){const auto* P=C.Proxies[I.Index];const uint32 N=P->GetMaxBoneTransformCount();
  const uint32 Capacity=Align(FMath::Max(uint32(P->GetSkinnedAsset()->GetRefSkeleton().GetNum())*64u,N*48u),64u);
  const uint32 Chunks=FMath::DivideAndRoundUp(N,256u);
  L->Add({RawSize,Capacity,LayoutGpuWorkCount,Chunks});RawSize+=2*Capacity;LayoutGpuWorkCount+=2*Chunks;
 }
 if(!LayoutGpuWorkCount)return;
 auto Work=MakeShared<TArray<FUintVector4>,ESPMode::ThreadSafe>();Work->SetNumZeroed(LayoutGpuWorkCount);
 auto& G=C.GraphBuilder;
 auto Raw=G.CreateBuffer(FRDGBufferDesc::CreateByteAddressDesc(FMath::Max(RawSize,16u)),TEXT("Crowd.RawBoneMatrices"));
 auto Items=G.CreateBuffer(FRDGBufferDesc::CreateByteAddressDesc(LayoutGpuWorkCount*16u),TEXT("Crowd.BonePackingWork"));
 const bool Check=Verify&&!V->Pending&&uint64(C.Indirections.Num())>V->LastPopulation;
 if(Check){V->Ranges.Reset();V->Population=C.Indirections.Num();V->Bytes=C.TransformBuffer->Desc.GetSize();V->Pending=true;V->ExpectedReady.store(false);if(!V->Readback)V->Readback=MakeUnique<FRHIGPUBufferReadback>(TEXT("Crowd.BonePackingVerification"));}
 // UE 5.8 Execute waits Compile setup tasks before SubmitBufferUploads.
 // Signal from the original scatter setup task, which retains all mesh-update
 // prerequisites. Queue fills then read stable mesh data before this graph ends.
 // Immediate RDG bypasses that ordering and is explicitly rejected above.
 auto Ready=MakeShared<std::atomic<bool>,ESPMode::ThreadSafe>(false);
 auto WorkReady=MakeShared<std::atomic<bool>,ESPMode::ThreadSafe>(false);
 C.ScatterUploadBuilder.AddFunction([Ready](FRHICommandList&){Ready->store(true);});
 G.QueueBufferUpload(Raw,
  [Indices=C.Indirections,Proxies=C.Proxies,L,Work,V,Check,Ready,WorkReady](void* Data,uint32 DataSize){
   check(Ready->load());
   TRACE_CPUPROFILER_EVENT_SCOPE(CrowdBulkBoneMatrixUpload);uint64 Bones=0,Jobs=0;
   for(int32 K=0;K<Indices.Num();++K){const auto& I=Indices[K];const auto& A=(*L)[K];const auto* P=Proxies[I.Index];const uint32 N=P->GetMaxBoneTransformCount();const auto T=P->GetMeshObject()->GetBoneTransforms();
    const bool UseMap=P->GetBoneTransformStorageMode()==EBoneTransformStorageMode::BoneMap;const auto Map=UseMap?P->GetBoneMap():TConstArrayView<uint16>();
    auto Add=[&](uint32 Slot,uint32 Dst,TConstArrayView<FMatrix44f> S){
     const uint32 Written=S.IsEmpty()||!UseMap?N:uint32(Map.Num());const uint32 Base=A.RawOffset+Slot*A.RawCapacity;check(Written<=N);
     check(uint64(Base)+A.RawCapacity<=DataSize);
     const bool Fallback=uint64(S.Num())*64>A.RawCapacity;
     if(!S.IsEmpty()){
      if(Fallback){++GpuFallbacks;auto* Out=reinterpret_cast<FCompressedBoneTransform*>(static_cast<uint8*>(Data)+Base);FJob J{Out,S,Map,Written};PackRange(J,Out,0,Written);}
      else{FMemory::Memcpy(static_cast<uint8*>(Data)+Base,S.GetData(),S.Num()*64);}
     }
     for(uint32 B=0;B<Written;B+=256){const uint32 Src=S.IsEmpty()?MAX_uint32:(Fallback?Base+B*48:Base+(UseMap?0:B*64));const uint32 MapAt=Fallback?MAX_uint32-1:(UseMap?I.BoneMapOffset+B*4:MAX_uint32);
      (*Work)[A.WorkOffset+Slot*A.Chunks+B/256]=FUintVector4(Src,Dst/16+B*3,MapAt,FMath::Min(256u,Written-B));
     }
     if(Check){auto& R=V->Ranges.AddDefaulted_GetRef();R.Offset=Dst;R.Expected.SetNumUninitialized(Written);
      if(S.IsEmpty())for(auto& M:R.Expected)SetCompressedBoneTransformIdentity(M);
      else if(UseMap){uint32 D=0;for(uint16 B:Map)StoreCompressedBoneTransform(&R.Expected[D++],S[B]);}
      else for(uint32 B=0;B<Written;++B)StoreCompressedBoneTransform(&R.Expected[B],S[B]);
     }
     Bones+=Written;++Jobs;
    };
    Add(0,I.CurrentTransformOffset,T.CurrentTransforms);
    if(T.CurrentTransforms.IsEmpty())Add(1,I.PreviousTransformOffset,{});
    else if(!T.PreviousTransforms.IsEmpty()&&(T.UpdateMode!=EPreviousBoneTransformUpdateMode::None||EnumHasAnyFlags(I.DirtyBoneTransforms,EDirtyBoneTransforms::Previous)))Add(1,I.PreviousTransformOffset,T.PreviousTransforms);
   }
   BoneCount.store(Bones);JobCount.store(Jobs);if(Check)V->ExpectedReady.store(true);WorkReady->store(true);
  });
 G.QueueBufferUpload(Items,[Work,WorkReady,LayoutGpuWorkCount](void* Data,uint32 DataSize){check(WorkReady->load());check(DataSize>=LayoutGpuWorkCount*16);FMemory::Memcpy(Data,Work->GetData(),LayoutGpuWorkCount*16);});
 OKAddGpuBonePackingPass(G,Raw,Items,C.BoneMapBufferSRV,C.TransformBuffer,LayoutGpuWorkCount);
 if(Check)AddEnqueueCopyPass(G,V->Readback.Get(),C.TransformBuffer,V->Bytes);
 ++GpuPasses;WorkCount.store(Work->Num());
#endif
}
void Provide(FSkinningTransformProvider::FProviderContext& C,bool Verify,const TSharedRef<FGpuVerification,ESPMode::ThreadSafe>& V){
 const int32 Mode=PackingMode.GetValueOnRenderThread();
 if(Mode==5){ProvideGpu(C,Verify,V);return;}
 if(Mode==0){FSkinningSceneExtension::ProvideMeshObjectTransforms(C);return;}
 if(Mode==4){
  // Preserve engine packing. Read only inside the uploader setup task, after
  // the engine's mesh-update prerequisites; reading here on RT would race it.
  FSkinningSceneExtension::ProvideMeshObjectTransforms(C);
  C.ScatterUploadBuilder.AddFunction([Indices=C.Indirections,Proxies=C.Proxies](FRHICommandList&){
   TRACE_CPUPROFILER_EVENT_SCOPE(CrowdBoneUploadFootprint);
   uint64 Raw=0,Written=0,Allocated=0,Jobs=0;TMap<const FMatrix44f*,uint32> Sources;
   for(const auto& I:Indices){const auto* P=Proxies[I.Index];const uint32 N=P->GetMaxBoneTransformCount();const auto T=P->GetMeshObject()->GetBoneTransforms();
    const bool UseMap=P->GetBoneTransformStorageMode()==EBoneTransformStorageMode::BoneMap;
    auto Add=[&](TConstArrayView<FMatrix44f> S){++Jobs;Allocated+=uint64(N)*sizeof(FCompressedBoneTransform);Written+=uint64(S.IsEmpty()||!UseMap?N:P->GetBoneMap().Num())*sizeof(FCompressedBoneTransform);Raw+=uint64(S.Num())*sizeof(FMatrix44f);
     if(!S.IsEmpty()){auto& Count=Sources.FindOrAdd(S.GetData());Count=FMath::Max(Count,uint32(S.Num()));}
    };
    Add(T.CurrentTransforms);
    if(T.CurrentTransforms.IsEmpty())Add({});
    else if(!T.PreviousTransforms.IsEmpty()&&(T.UpdateMode!=EPreviousBoneTransformUpdateMode::None||EnumHasAnyFlags(I.DirtyBoneTransforms,EDirtyBoneTransforms::Previous)))Add(T.PreviousTransforms);
   }
   uint64 Unique=0;for(const auto& S:Sources)Unique+=uint64(S.Value)*sizeof(FMatrix44f);
   RawBytes.store(Raw);UniqueRawBytes.store(Unique);WrittenBytes.store(Written);AllocatedBytes.store(Allocated);JobCount.store(Jobs);BoneCount.store(Written/sizeof(FCompressedBoneTransform));++FootprintSamples;
  });
  return;
 }
 uint32 Capacity=0;for(const auto& I:C.Indirections)Capacity+=C.Proxies[I.Index]->GetMaxBoneTransformCount()*2u;
 if(!Capacity)return;
 auto& Upload=*C.GraphBuilder.AllocObject<FRDGAsyncScatterUploadBuffer>();
 C.ScatterUploadBuilder.AddPass(C.GraphBuilder,Upload,C.TransformBuffer,Capacity,sizeof(FCompressedBoneTransform),TEXT("Crowd.ParallelAnimTransforms"),
 [Indices=C.Indirections,Proxies=C.Proxies,Verify,Mode](FRDGScatterUploader& Uploader){
  TRACE_CPUPROFILER_EVENT_SCOPE(CrowdParallelBonePacking);
  TArray<FJob,TInlineAllocator<256>> Jobs;uint64 Bones=0;
  for(const auto& I:Indices){const auto* P=Proxies[I.Index];const uint32 N=P->GetMaxBoneTransformCount();const auto T=P->GetMeshObject()->GetBoneTransforms();
   const bool UseMap=P->GetBoneTransformStorageMode()==EBoneTransformStorageMode::BoneMap;
   const TConstArrayView<uint16> Map=UseMap?P->GetBoneMap():TConstArrayView<uint16>();
   // Allocation capacity can exceed this LOD's map. Match the engine's
   // map iteration rather than indexing to the maximum allocation count.
   auto Add=[&](uint32 Offset,TConstArrayView<FMatrix44f> Source){auto Out=Uploader.Add_GetRef<FCompressedBoneTransform>(Offset/sizeof(FCompressedBoneTransform),N);const uint32 Written=Source.IsEmpty()||!UseMap?N:uint32(Map.Num());check(Written<=N);Jobs.Add({Out.GetData(),Source,Source.IsEmpty()?TConstArrayView<uint16>():Map,Written});Bones+=Written;};
   Add(I.CurrentTransformOffset,T.CurrentTransforms);
   if(T.CurrentTransforms.IsEmpty())Add(I.PreviousTransformOffset,{});
   else if(!T.PreviousTransforms.IsEmpty()&&(T.UpdateMode!=EPreviousBoneTransformUpdateMode::None||EnumHasAnyFlags(I.DirtyBoneTransforms,EDirtyBoneTransforms::Previous)))Add(I.PreviousTransformOffset,T.PreviousTransforms);
  }
  // Each primitive owns different destination ranges. Do not call uploader
  // allocation APIs in ParallelFor; they are not concurrent allocators.
  if(Mode==3){
   // Only compare adjacent current/previous jobs belonging to this invocation.
   // No persistent revision cache: pointer reuse, LOD changes and render-buffer
   // reallocations cannot reuse stale data. Both GPU destinations are written.
   uint64 Reused=0;int32 Encoded=0;
   for(int32 I=0;I<Jobs.Num();++I){const auto& J=Jobs[I];bool Same=false;
    if(I>0){const auto& P=Jobs[I-1];
     Same=SamePose(J,P);
     if(Same){FMemory::Memcpy(J.Output,P.Output,J.Count*sizeof(FCompressedBoneTransform));Reused+=J.Count;}
    }
    if(!Same){PackRange(J,J.Output,0,J.Count);++Encoded;}
   }
   ReusedBones.store(Reused);WorkCount.store(Encoded);
  }else if(Mode==2){
   struct FRange {int32 Job;uint32 Begin,End;};TArray<FRange,TInlineAllocator<512>> Ranges;
   // A large private palette must not monopolize the last worker. Chunking
   // changes only scheduling; all writes retain their original destination.
   for(int32 J=0;J<Jobs.Num();++J)for(uint32 B=0;B<Jobs[J].Count;B+=512)Ranges.Add({J,B,FMath::Min(B+512,Jobs[J].Count)});
   ParallelFor(TEXT("CrowdPackBoneRanges"),Ranges.Num(),8,[&](int32 I){const auto& R=Ranges[I];const auto& J=Jobs[R.Job];PackRange(J,J.Output,R.Begin,R.End);},EParallelForFlags::Unbalanced);
   WorkCount.store(Ranges.Num());
  }else{ParallelFor(TEXT("CrowdPackBones"),Jobs.Num(),16,[&](int32 I){PackRange(Jobs[I],Jobs[I].Output,0,Jobs[I].Count);},EParallelForFlags::Unbalanced);WorkCount.store(Jobs.Num());}
  // Verify a new maximum population during warm-up, so late-loaded face/hair
  // components are included, using serial engine-equivalent output byte for byte.
  if(Verify&&(Mode!=VerifiedMode.load()||uint64(Jobs.Num())>VerifiedJobs.load())){
   uint64 Errors=0;TArray<FCompressedBoneTransform> Reference;
   for(const auto& J:Jobs){
    Reference.SetNumUninitialized(J.Count);
    // Independent serial oracle follows the engine's source iteration, not
    // the range-packing function under test (including shorter LOD maps).
    if(J.Source.IsEmpty()){for(auto& D:Reference)SetCompressedBoneTransformIdentity(D);}
    else if(!J.Map.IsEmpty()){uint32 D=0;for(uint16 Bone:J.Map)StoreCompressedBoneTransform(&Reference[D++],J.Source[Bone]);}
    else{for(uint32 D=0;D<J.Count;++D)StoreCompressedBoneTransform(&Reference[D],J.Source[D]);}
    if(J.Count&&FMemory::Memcmp(Reference.GetData(),J.Output,J.Count*sizeof(FCompressedBoneTransform))!=0)++Errors;
   }
   Mismatches.fetch_add(Errors);VerifiedJobs.store(Jobs.Num());VerifiedMode.store(Mode);Verified.store(Mismatches.load()==0);
   if(Errors)UE_LOG(LogTemp,Error,TEXT("Crowd parallel bone packing failed byte equality jobs=%llu"),Errors);
  }
  ++Passes;JobCount.store(Jobs.Num());BoneCount.store(Bones);
 });
}
}
void OKInstallCrowdBonePacking(UWorld* World,bool Verify){
 if(!World||!World->Scene)return;
 if(Verify){if(!VerifyShortMapRanges()||!VerifyDuplicatePoseInputs()){UE_LOG(LogTemp,Error,TEXT("Crowd bone range/duplicate-pose verification failed"));return;}UE_LOG(LogTemp,Display,TEXT("Crowd bone range short-map/sentinel and duplicate-pose verification passed"));}
 auto* Scene=World->Scene->GetRenderScene();
 ENQUEUE_RENDER_COMMAND(InstallCrowdBonePacking)([Scene,Verify](FRHICommandListImmediate&){
  auto* Provider=Scene->GetExtensionPtr<FSkinningTransformProvider>();if(!Provider)return;
  const auto Id=GetMeshObjectProviderId();if(!Provider->GetProviderIds().Contains(Id))return;
  // Registration is scoped to this crowd scene, never the engine globally.
  // The default provider ID and batch semantics stay intact, so scene teardown
  // unregisters it normally. The project callback remains loaded for its life.
  Provider->UnregisterProvider(Id);
  auto Verification=MakeShared<FGpuVerification,ESPMode::ThreadSafe>();
  Provider->RegisterProvider(Id,FSkinningTransformProvider::FOnProvideTransforms::CreateLambda([Verify,Verification](auto& C){Provide(C,Verify,Verification);}),false);
  Installed.store(true);UE_LOG(LogTemp,Display,TEXT("Crowd parallel bone packing provider installed verify=%d"),Verify);
 });
}
FOKCrowdBonePackingStats OKGetCrowdBonePackingStats(){return {Installed.load(),Verified.load(),Passes.load(),JobCount.load(),BoneCount.load(),VerifiedJobs.load(),Mismatches.load(),PackingMode.GetValueOnGameThread(),WorkCount.load(),ReusedBones.load(),RawBytes.load(),UniqueRawBytes.load(),WrittenBytes.load(),AllocatedBytes.load(),FootprintSamples.load(),GpuPasses.load(),GpuFallbacks.load(),GpuVerifiedProxies.load()};}
