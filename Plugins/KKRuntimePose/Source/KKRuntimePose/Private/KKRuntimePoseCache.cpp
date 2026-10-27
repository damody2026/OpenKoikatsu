#include "KKRuntimePoseCache.h"

uint64 FKKRuntimePoseCache::Estimate(const FKKRuntimePoseRequest& R){
 if(R.Bones.IsEmpty()||R.Bones.Num()>65536||R.SampleHz!=60||!FMath::IsFinite(R.Duration)||R.Duration<=0||R.Duration>34)return 0;
 const uint64 Rows=uint64(R.Bones.Num())*uint64(OKRuntimePoseFrames(R.Duration,60)+1)*3;if(Rows>MAX_int32)return 0;
 // Match Keys/Tracks Reserve, including allocator quantization. The exact
 // palette can remain fully animated, so no constant-key savings are assumed.
 return uint64(DefaultCalculateSlackReserve(int32(Rows),sizeof(FVector4f),true,alignof(FVector4f)))*sizeof(FVector4f)
  +uint64(DefaultCalculateSlackReserve(R.Bones.Num(),sizeof(FUintVector4),true,alignof(FUintVector4)))*sizeof(FUintVector4)+256;
}
TSharedPtr<const FKKRuntimePoseResult,ESPMode::ThreadSafe> FKKRuntimePoseCache::Acquire(const FKKRuntimePoseCacheKey& Key){
 check(IsInGameThread());if(auto* E=Entries.Find(Key)){E->Touch=++Touch;return E->Data;}return nullptr;
}
bool FKKRuntimePoseCache::Reserve(const FKKRuntimePoseCacheKey& Key,uint64 Bytes,FKKRuntimePoseReservation& Out,FString& Error){
 check(IsInGameThread());
 if(Key.Character.IsEmpty()||Key.Clip.IsEmpty()||Key.Recipe.IsEmpty()||!Bytes||Bytes>Budget||Entries.Contains(Key)){Error=TEXT("Invalid, oversized or already resident pose reservation");return false;}
 for(const auto& P:Pending)if(P.Value.Key==Key){Error=TEXT("Pose preparation already pending");return false;}
 const uint64 Used=Stats.ResidentBytes+Stats.ReservedBytes;uint64 Need=Bytes>Budget-Used?Bytes-(Budget-Used):0;
 TArray<FKKRuntimePoseCacheKey> Candidates;for(const auto& P:Entries)if(P.Value.Data.IsUnique())Candidates.Add(P.Key);
 Candidates.Sort([&](const auto& A,const auto& B){return Entries[A].Touch<Entries[B].Touch;});
 uint64 Available=0;for(const auto& K:Candidates)Available+=Entries[K].Bytes;
 if(Available<Need){Error=TEXT("Pose buffer budget is pinned or reserved; defer preparation");return false;}
 for(const auto& K:Candidates){if(!Need)break;const uint64 Freed=Entries[K].Bytes;Stats.ResidentBytes-=Freed;Need=Freed>=Need?0:Need-Freed;Entries.Remove(K);++Stats.Evictions;}
 FKKRuntimePoseReservation R;R.Ticket=++Serial;R.Bytes=Bytes;R.Key=Key;Pending.Add(R.Ticket,R);Stats.ReservedBytes+=Bytes;Stats.PeakBytes=FMath::Max(Stats.PeakBytes,Stats.ResidentBytes+Stats.ReservedBytes);Out=MoveTemp(R);Error.Reset();return true;
}
bool FKKRuntimePoseCache::Publish(const FKKRuntimePoseReservation& R,TUniquePtr<FKKRuntimePoseResult> Data,FString& Error){
 check(IsInGameThread());const auto* P=Pending.Find(R.Ticket);
 if(!P||P->Bytes!=R.Bytes||!(P->Key==R.Key)){Error=TEXT("Stale pose publication ticket");return false;}
 const uint64 Bytes=Data?uint64(Data->Keys.GetAllocatedSize())+Data->Tracks.GetAllocatedSize():0;
 if(!Data||!Data->Passed()||Data->CharacterIdentity!=R.Key.Character||Data->ClipIdentity!=R.Key.Clip||Data->PreparationIdentity!=R.Key.Recipe||Bytes>R.Bytes){Error=FString::Printf(TEXT("Prepared pose differs from reservation identity or byte bound: allocated=%llu reserved=%llu identity=%d"),Bytes,R.Bytes,Data&&Data->CharacterIdentity==R.Key.Character&&Data->ClipIdentity==R.Key.Clip&&Data->PreparationIdentity==R.Key.Recipe);return false;}
 FEntry E;E.Bytes=Bytes;E.Touch=++Touch;E.Data=TSharedPtr<const FKKRuntimePoseResult,ESPMode::ThreadSafe>(Data.Release());Entries.Add(R.Key,MoveTemp(E));Stats.ResidentBytes+=Bytes;Stats.ReservedBytes-=R.Bytes;Pending.Remove(R.Ticket);Error.Reset();return true;
}
bool FKKRuntimePoseCache::Cancel(const FKKRuntimePoseReservation& R){
 check(IsInGameThread());const auto* P=Pending.Find(R.Ticket);if(!P||P->Bytes!=R.Bytes||!(P->Key==R.Key))return false;Stats.ReservedBytes-=R.Bytes;Pending.Remove(R.Ticket);return true;
}
FKKRuntimePoseCacheStats FKKRuntimePoseCache::GetStats()const{check(IsInGameThread());auto S=Stats;S.ResidentClips=Entries.Num();S.PendingJobs=Pending.Num();return S;}
