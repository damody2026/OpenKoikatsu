#pragma once
#include "OKRuntimeGpuDeform.h"
#include "OKRuntimeGpuCoating.h"
#include "Templates/Atomic.h"

// Owned packet: source/draw present only for installation; updates contain only
// palette. CPU query geometry belongs to the caller. No native borrowed pointers.
struct FKKGpuStreamRequest {
 TArray<FOKGpuDeformSource> Source;TArray<FOKGpuDeformDraw> Draw;
 TArray<FMatrix44f> Palette;FMatrix44f PostCorrection=FMatrix44f::Identity;
 FBox ConservativeLocalBounds=FBox(ForceInit);
 uint64 ProxyEpoch=0,ExpectedGeneration=0;uint32 FallbackSlot=0;
 bool HasPostCorrection=false;
 bool NativeUnitySpace=false; // immutable installation property; skin/post in native, draw in UE
 bool QueryGenerationOnly=false; // reserves caller ownership; no vertex publication
};
// One producer, status published last. Read Error/Generation only after Status
// changes from Pending; the receipt is never modified afterwards.
struct FKKGpuStreamReceipt {
 enum EStatus:int32 {Pending=0,Published=1,Rejected=2};
 TAtomic<int32> Status{Pending};FString Error;uint64 Generation=0,ProxyEpoch=0;
 bool Retryable=false;
 void Complete(bool Success,uint64 InGeneration,uint64 InEpoch,FString InError,bool InRetryable=false){
  Generation=InGeneration;ProxyEpoch=InEpoch;Error=MoveTemp(InError);Retryable=InRetryable;Status.Store(Success?Published:Rejected);
 }
};
class UKKAffineProceduralMeshComponent;
// Owned runtime bindings. Empty bindings reuse the acknowledged revision.
// Both proxy identities are validated before any render-thread pointer is captured.
struct FKKGpuCoatingRequest {
 TArray<FOKGpuCoatingBinding> Bindings;
 FMatrix44f SourceToOverlay=FMatrix44f::Identity;
 FBox ConservativeLocalBounds=FBox(ForceInit);
 uint64 SourceEpoch=0,TargetEpoch=0,Revision=0,ExpectedGeneration=0;
};
struct FKKGpuStreamSectionRequest {
 UKKAffineProceduralMeshComponent* Component=nullptr;int32 Section=0;
 FKKGpuStreamRequest Request;
 TSharedPtr<FKKGpuStreamReceipt,ESPMode::ThreadSafe> Receipt;
};

struct FKKGpuStreamBatchImpl;
// Merge consecutive disjoint publications into one render graph. Repeated
// targets and nested scopes flush first, preserving original command ordering.
class KKUNITYBUNDLE_API FKKGpuStreamBatchScope {
public:
 FKKGpuStreamBatchScope();
 ~FKKGpuStreamBatchScope();
 FKKGpuStreamBatchScope(const FKKGpuStreamBatchScope&)=delete;
 FKKGpuStreamBatchScope& operator=(const FKKGpuStreamBatchScope&)=delete;
private:
 TUniquePtr<FKKGpuStreamBatchImpl> Impl;
};
struct FKKGpuCoatingBatchImpl;
class KKUNITYBUNDLE_API FKKGpuCoatingBatchScope {
public:
 FKKGpuCoatingBatchScope();
 ~FKKGpuCoatingBatchScope();
 FKKGpuCoatingBatchScope(const FKKGpuCoatingBatchScope&)=delete;
 FKKGpuCoatingBatchScope& operator=(const FKKGpuCoatingBatchScope&)=delete;
private:
 TUniquePtr<FKKGpuCoatingBatchImpl> Impl;
};
