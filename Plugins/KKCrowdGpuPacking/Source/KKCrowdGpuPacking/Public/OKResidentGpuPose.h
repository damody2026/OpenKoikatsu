#pragma once
#include "OKIndependentGpuPose.h"
class FOKGpuResidencyBudget;
// CPU-side immutable validation shared by import/cache and GPU initialization.
KKCROWDGPUPACKING_API bool OKValidateOwnedGpuPose(const FOKGpuPoseBatch&,const TArray<TArray<uint32>>&,FString&);

struct FOKGpuAnimationBundle;
// Worker-created exclusive immutable owner. No mutable pose alias survives
// factory admission, so render uploads can reuse the completed integrity check.
class KKCROWDGPUPACKING_API FOKValidatedGpuBank {
public:
 ~FOKValidatedGpuBank();
 static TSharedPtr<const FOKValidatedGpuBank,ESPMode::ThreadSafe> Create(TUniquePtr<FOKGpuAnimationBundle>,FString&);
 const FOKGpuAnimationBundle& Get() const;
private:
 FOKValidatedGpuBank();TUniquePtr<FOKGpuAnimationBundle> Owned;
};

struct FOKGpuPosePlayback {uint32 ClipA=0,ClipB=1;FVector4f Times=FVector4f(0,0,0,0);};
struct FOKResidentPoseFrame {FRDGBufferRef Current=nullptr,Previous=nullptr;bool Reused=false;uint64 Generation=0;};
struct FOKResidentPoseStats {uint64 Generation=0,Initializations=0,ImmutableUploadBytes=0,ParameterUploadBytes=0,Evaluations=0,ReusedFrames=0,ResidentBufferBytes=0,ReservedPeakBufferBytes=0,BudgetRejections=0;};
// All operations except construction/destruction are render-thread-only.
// Buffer budget counts logical GPU buffer bytes, including old/new replacement overlap
// and two pose outputs. It excludes driver allocations and unrelated renderer resources.
// Execute each graph before evaluating the next frame or releasing/reinitializing.
class KKCROWDGPUPACKING_API FOKResidentGpuPose {
public:
 FOKResidentGpuPose();~FOKResidentGpuPose();
 bool Initialize(FRDGBuilder& Graph,const FOKGpuPoseBatch& Batch,const TArray<TArray<uint32>>& OwnedClipOffsets,FString& Error,uint64 MaxBufferBytes=4ull*1024*1024*1024);
 // Shared aggregate admission. Hold Budget until retirement collection completes.
 bool InitializeManaged(FRDGBuilder& Graph,const FOKGpuPoseBatch& Batch,const TArray<TArray<uint32>>& OwnedClipOffsets,TSharedRef<FOKGpuResidencyBudget,ESPMode::ThreadSafe> Budget,FString& Error);
 // Incremental first admission only. Upload at most ChunkBytes per call;
 // Ready becomes true only after every immutable byte was queued in order.
 bool UploadManagedChunk(FRDGBuilder&,TSharedRef<const FOKValidatedGpuBank,ESPMode::ThreadSafe>,TSharedRef<FOKGpuResidencyBudget,ESPMode::ThreadSafe>,uint64 ChunkBytes,bool& Ready,FString&);
 // Share immutable GPU resources, but own selection, clocks and pose output.
 // The source must be initialized and have no evaluated frame yet.
 bool InitializeShared(const FOKResidentGpuPose& Source,FString& Error);
 // Exact cache query; does not advance playback/history or mutate the cache.
 // A miss leaves Result empty. Register before evaluating the next current pose.
 bool TryReuseCurrent(FRDGBuilder& Graph,TConstArrayView<FOKGpuPosePlayback> Playback,uint64 ExpectedGeneration,FOKResidentPoseFrame& Result) const;
 bool Evaluate(FRDGBuilder& Graph,TConstArrayView<FOKGpuPosePlayback> Playback,uint64 FrameIndex,uint64 ExpectedGeneration,bool ResetHistory,FOKResidentPoseFrame& Result,FString& Error);
 void Release();
 FOKResidentPoseStats GetStats() const;
private:
 struct FState;TUniquePtr<FState> State;
 TSharedPtr<const FOKValidatedGpuBank,ESPMode::ThreadSafe> UploadBank;uint32 UploadArray=0;uint64 UploadOffset=0;bool UploadPending=false;
 static void RetireState(FState&);
 FOKResidentGpuPose(const FOKResidentGpuPose&)=delete;
 FOKResidentGpuPose& operator=(const FOKResidentGpuPose&)=delete;
};
// Diagnostic only, evaluates persistent resources and reads current/previous rows.
KKCROWDGPUPACKING_API bool OKReadbackResidentGpuPose(FOKResidentGpuPose& Pose,const FOKGpuPoseBatch* Initialization,const TArray<TArray<uint32>>& OwnedClips,TConstArrayView<FOKGpuPosePlayback> Playback,uint64 FrameIndex,bool ResetHistory,TArray<FVector4f>& Current,TArray<FVector4f>& Previous,FOKResidentPoseStats& Stats,FString& Error,TSharedPtr<FOKGpuResidencyBudget,ESPMode::ThreadSafe> Budget=nullptr);
