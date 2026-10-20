#include "OKGpuResidencyBudget.h"
#include "RenderGraphResources.h"
#include "RenderingThread.h"
#include "RHICommandList.h"
#include "DynamicRHI.h"
bool FOKGpuResidencyBudget::Reserve(uint64 Bytes,uint64& Ticket,FString& Error){
 check(IsInRenderingThread());Ticket=0;Collect();
 if(!Bytes||Bytes>Limit||Stats.ChargedBytes>Limit-Bytes){++Stats.Rejected;Error=TEXT("Aggregate GPU bank capacity unavailable; retain active playback");return false;}
 Ticket=++NextTicket;FEntry E;E.Bytes=Bytes;Entries.Add(Ticket,MoveTemp(E));Stats.ChargedBytes+=Bytes;++Stats.LiveBanks;Stats.PeakBytes=FMath::Max(Stats.PeakBytes,Stats.ChargedBytes);return true;
}
bool FOKGpuResidencyBudget::ReserveShared(uint64 Source,uint64 Bytes,uint64& Ticket,FString& Error){
 check(IsInRenderingThread());Ticket=0;auto* Parent=Entries.Find(Source);if(!Parent||Parent->Fence){Error=TEXT("Shared GPU bank source is not active");return false;}
 if(!Reserve(Bytes,Ticket,Error))return false;
 // Reserve may rehash the map; reacquire the source pointer.
 Entries.FindChecked(Source).Dependents++;Entries.FindChecked(Ticket).Parent=Source;return true;
}
bool FOKGpuResidencyBudget::CancelUnused(uint64 Ticket){
 check(IsInRenderingThread());auto* E=Entries.Find(Ticket);if(!E||E->Fence||E->Dependents)return false;if(E->Parent)--Entries.FindChecked(E->Parent).Dependents;Stats.ChargedBytes-=E->Bytes;--Stats.LiveBanks;Entries.Remove(Ticket);return true;
}
bool FOKGpuResidencyBudget::Retire(uint64 Ticket,TArray<TRefCountPtr<FRDGPooledBuffer>> Buffers,FRHICommandListImmediate& Commands){
 check(IsInRenderingThread());auto* E=Entries.Find(Ticket);if(!E||E->Fence)return false;
 E->Fence=RHICreateGPUFence(TEXT("Crowd.BankRetirement"));E->Buffers=MoveTemp(Buffers);Commands.WriteGPUFence(E->Fence);Stats.RetiringBytes+=E->Bytes;--Stats.LiveBanks;++Stats.RetiringBanks;return true;
}
void FOKGpuResidencyBudget::Collect(){
 check(IsInRenderingThread());for(auto I=Entries.CreateIterator();I;++I){auto& E=I.Value();if(E.Fence&&!E.Dependents&&E.Fence->Poll()){if(E.Parent)--Entries.FindChecked(E.Parent).Dependents;Stats.ChargedBytes-=E.Bytes;Stats.RetiringBytes-=E.Bytes;--Stats.RetiringBanks;I.RemoveCurrent();}}
}
FOKGpuResidencyStats FOKGpuResidencyBudget::GetStats()const{check(IsInRenderingThread());return Stats;}
