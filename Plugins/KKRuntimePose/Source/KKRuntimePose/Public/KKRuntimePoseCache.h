#pragma once
#include "KKRuntimePoseBankTypes.h"

struct FKKRuntimePoseCacheKey {
 FString Character,Clip,Recipe;
 bool operator==(const FKKRuntimePoseCacheKey& B)const{return Character==B.Character&&Clip==B.Clip&&Recipe==B.Recipe;}
 friend uint32 GetTypeHash(const FKKRuntimePoseCacheKey& K){return HashCombine(HashCombine(GetTypeHash(K.Character),GetTypeHash(K.Clip)),GetTypeHash(K.Recipe));}
};
struct FKKRuntimePoseReservation {uint64 Ticket=0,Bytes=0;FKKRuntimePoseCacheKey Key;};
struct FKKRuntimePoseCacheStats {uint64 ResidentBytes=0,ReservedBytes=0,PeakBytes=0,Evictions=0;int32 ResidentClips=0,PendingJobs=0;};
// Game-thread admission for immutable CPU pose buffers. A retained Acquire()
// handle pins an active/blending/uploading clip until the consumer releases it.
// GPU copies, native-session/input memory and renderer fences have separate
// lifetimes/budgets; this cache does not claim to manage them.
class KKRUNTIMEPOSE_API FKKRuntimePoseCache {
 struct FEntry {TSharedPtr<const FKKRuntimePoseResult,ESPMode::ThreadSafe> Data;uint64 Bytes=0,Touch=0;};
 TMap<FKKRuntimePoseCacheKey,FEntry> Entries;
 TMap<uint64,FKKRuntimePoseReservation> Pending;
 uint64 Budget,Serial=0,Touch=0;FKKRuntimePoseCacheStats Stats;
public:
 explicit FKKRuntimePoseCache(uint64 MaxBufferBytes):Budget(MaxBufferBytes){}
 TSharedPtr<const FKKRuntimePoseResult,ESPMode::ThreadSafe> Acquire(const FKKRuntimePoseCacheKey& Key);
 bool Reserve(const FKKRuntimePoseCacheKey& Key,uint64 MaxOutputBytes,FKKRuntimePoseReservation& Out,FString& Error);
 bool Publish(const FKKRuntimePoseReservation&,TUniquePtr<FKKRuntimePoseResult>,FString& Error);
 // Only release a job reservation after its worker has completed/joined,
 // or when no worker was started. A cancellation request alone does not
 // free the reservation while that job may still allocate output buffers.
 bool Cancel(const FKKRuntimePoseReservation&);
 FKKRuntimePoseCacheStats GetStats()const;
 // Conservative key/track buffer reservation before starting the worker.
 static uint64 Estimate(const FKKRuntimePoseRequest&);
};
