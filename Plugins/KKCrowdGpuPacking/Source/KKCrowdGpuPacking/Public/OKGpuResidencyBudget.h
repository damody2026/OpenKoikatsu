#pragma once
#include "CoreMinimal.h"
#include "RHIResources.h"
#include "RenderGraphResources.h"

struct FOKGpuResidencyStats {uint64 ChargedBytes=0,RetiringBytes=0,PeakBytes=0,Rejected=0;uint32 LiveBanks=0,RetiringBanks=0;};
// Render-thread-only aggregate logical bank capacity, not physical driver VRAM.
// Retire after the last graph using the bank has executed; no later submissions
// may use the retired bank. Current compute path uses the graphics queue.
class KKCROWDGPUPACKING_API FOKGpuResidencyBudget {
 struct FEntry {uint64 Bytes=0,Parent=0;uint32 Dependents=0;FGPUFenceRHIRef Fence;TArray<TRefCountPtr<FRDGPooledBuffer>> Buffers;};
 TMap<uint64,FEntry> Entries;uint64 Limit=0,NextTicket=0;FOKGpuResidencyStats Stats;
public:
 explicit FOKGpuResidencyBudget(uint64 MaxBytes):Limit(MaxBytes){}
 // Fixed at construction: readable from any thread.
 uint64 GetLimit()const{return Limit;}
 bool Reserve(uint64 Bytes,uint64& Ticket,FString& Error);
 bool ReserveShared(uint64 SourceTicket,uint64 PrivateBytes,uint64& Ticket,FString& Error);
 // Only for reservations for which no upload/dispatch was submitted.
 bool CancelUnused(uint64 Ticket);
 bool Retire(uint64 Ticket,TArray<TRefCountPtr<FRDGPooledBuffer>> Buffers,FRHICommandListImmediate& Commands);
 // Nonblocking, call on subsequent render frames / admission attempts.
 void Collect();
 FOKGpuResidencyStats GetStats()const;
};

// Game-thread accessor. The module owns this process-wide crowd budget and
// collects retirement fences at render-frame end, including after scene teardown.
KKCROWDGPUPACKING_API TSharedRef<FOKGpuResidencyBudget,ESPMode::ThreadSafe> OKGetCrowdGpuResidencyBudget();
