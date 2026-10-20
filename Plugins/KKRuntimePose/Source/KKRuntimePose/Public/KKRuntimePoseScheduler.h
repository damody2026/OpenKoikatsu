#pragma once
#include "KKRuntimePoseCache.h"

struct FKKRuntimePoseCompletion {
 uint64 Ticket=0,Selection=0;
 TSharedPtr<const FKKRuntimePoseResult,ESPMode::ThreadSafe> Data;
 FString Error;
};
struct FKKRuntimePoseSchedulerStats {int32 Queued=0,Running=0,PeakRunning=0;uint64 Started=0,Discarded=0,CacheHits=0;};
// Game-thread owner; Tick never waits. Submit only active/blending/preload clips.
// Output reservations remain charged until cancelled workers actually finish.
// Input/native-session memory and GPU residency are separate budgets.
class KKRUNTIMEPOSE_API FKKRuntimePoseScheduler {
 struct FJob {
  uint64 Ticket=0,Selection=0;bool Cancelled=false;
  FKKRuntimePoseRequest Request;FKKRuntimePoseCacheKey Key;
  FKKRuntimePoseReservation Reservation;TFuture<FKKRuntimePoseResult> Future;
 };
 TSharedRef<FKKRuntimePoseCache> Cache;
 TArray<TUniquePtr<FJob>> Jobs;TArray<FKKRuntimePoseCompletion> Completed;
 TFunction<TFuture<FKKRuntimePoseResult>(FKKRuntimePoseRequest)> Launch;
 uint64 Budget,Selection=1,Serial=0;int32 MaxRunning,MaxQueued;
 FKKRuntimePoseSchedulerStats Stats;
public:
 FKKRuntimePoseScheduler(const FKKRuntimePoseScheduler&)=delete;
 FKKRuntimePoseScheduler& operator=(const FKKRuntimePoseScheduler&)=delete;
 FKKRuntimePoseScheduler(uint64 BufferBudget,int32 ConcurrentJobs=2,int32 QueueLimit=8,
  TFunction<TFuture<FKKRuntimePoseResult>(FKKRuntimePoseRequest)> Worker={});
 uint64 BeginSelection();
 bool Submit(FKKRuntimePoseRequest Request,uint64& Ticket,FString& Error);
 bool Cancel(uint64 Ticket);
 void Tick();
 bool Take(uint64 Ticket,FKKRuntimePoseCompletion& Out);
 FKKRuntimePoseSchedulerStats GetStats()const;
 FKKRuntimePoseCacheStats GetCacheStats()const{return Cache->GetStats();}
};
