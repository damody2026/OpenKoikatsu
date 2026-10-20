#include "KKRuntimePoseScheduler.h"

FKKRuntimePoseScheduler::FKKRuntimePoseScheduler(uint64 Bytes,int32 Workers,int32 Queue,
 TFunction<TFuture<FKKRuntimePoseResult>(FKKRuntimePoseRequest)> Worker)
 :Cache(MakeShared<FKKRuntimePoseCache>(Bytes)),Launch(MoveTemp(Worker)),Budget(Bytes),MaxRunning(FMath::Max(1,Workers)),MaxQueued(FMath::Max(1,Queue)){
 if(!Launch)Launch=[](FKKRuntimePoseRequest R){return OKPrepareRuntimePoseBankAsync(MoveTemp(R));};
}
uint64 FKKRuntimePoseScheduler::BeginSelection(){
 check(IsInGameThread());++Selection;Completed.Reset();
 for(auto& J:Jobs)J->Cancelled=true;
 Tick();return Selection;
}
bool FKKRuntimePoseScheduler::Submit(FKKRuntimePoseRequest R,uint64& Ticket,FString& Error){
 check(IsInGameThread());const uint64 Bytes=FKKRuntimePoseCache::Estimate(R);
 if(!Bytes||Bytes>Budget||R.CharacterIdentity.IsEmpty()||R.ClipIdentity.IsEmpty()){Error=TEXT("Invalid or oversized runtime pose job");return false;}
 const auto S=GetStats();if(S.Queued+Completed.Num()>=MaxQueued){Error=TEXT("Runtime pose queue full; defer submission");return false;}
 FKKRuntimePoseCacheKey Key{R.CharacterIdentity,R.ClipIdentity,OKRuntimePoseRecipeIdentity(R)};
 for(const auto& J:Jobs)if(!J->Cancelled&&J->Selection==Selection&&J->Key==Key){Error=TEXT("Runtime pose job already queued");return false;}
 Ticket=++Serial;
 if(auto Hit=Cache->Acquire(Key)){Completed.Add({Ticket,Selection,Hit,{}});++Stats.CacheHits;Error.Reset();return true;}
 auto J=MakeUnique<FJob>();J->Ticket=Ticket;J->Selection=Selection;J->Key=MoveTemp(Key);J->Request=MoveTemp(R);Jobs.Add(MoveTemp(J));Error.Reset();return true;
}
bool FKKRuntimePoseScheduler::Cancel(uint64 Ticket){
 check(IsInGameThread());for(auto& J:Jobs)if(J->Ticket==Ticket){J->Cancelled=true;return true;}
 for(int32 I=0;I<Completed.Num();++I)if(Completed[I].Ticket==Ticket){Completed.RemoveAt(I);return true;}return false;
}
void FKKRuntimePoseScheduler::Tick(){
 check(IsInGameThread());
 for(int32 I=Jobs.Num()-1;I>=0;--I){auto& J=*Jobs[I];
  if(!J.Future.IsValid()){if(J.Cancelled){Jobs.RemoveAt(I);++Stats.Discarded;}continue;}
  if(!J.Future.IsReady())continue;
  auto R=J.Future.Consume();
  if(J.Cancelled||J.Selection!=Selection){Cache->Cancel(J.Reservation);++Stats.Discarded;}
  else {FKKRuntimePoseCompletion C;C.Ticket=J.Ticket;C.Selection=J.Selection;
   if(R.Passed()&&Cache->Publish(J.Reservation,MakeUnique<FKKRuntimePoseResult>(MoveTemp(R)),C.Error))C.Data=Cache->Acquire(J.Key);
   else {if(C.Error.IsEmpty())C.Error=R.Error;Cache->Cancel(J.Reservation);}
   Completed.Add(MoveTemp(C));
  }Jobs.RemoveAt(I);
 }
 int32 Running=GetStats().Running;
 for(auto& P:Jobs){auto& J=*P;if(Running>=MaxRunning)break;if(J.Future.IsValid()||J.Cancelled)continue;
  FString Error;if(!Cache->Reserve(J.Key,FKKRuntimePoseCache::Estimate(J.Request),J.Reservation,Error))continue;
  J.Future=Launch(MoveTemp(J.Request));
  if(!J.Future.IsValid()){Cache->Cancel(J.Reservation);Completed.Add({J.Ticket,J.Selection,nullptr,TEXT("Runtime worker returned invalid future")});J.Cancelled=true;continue;}
  ++Running;++Stats.Started;Stats.PeakRunning=FMath::Max(Stats.PeakRunning,Running);
 }
}
bool FKKRuntimePoseScheduler::Take(uint64 Ticket,FKKRuntimePoseCompletion& Out){
 check(IsInGameThread());for(int32 I=0;I<Completed.Num();++I)if(Completed[I].Ticket==Ticket){Out=MoveTemp(Completed[I]);Completed.RemoveAt(I);return true;}return false;
}
FKKRuntimePoseSchedulerStats FKKRuntimePoseScheduler::GetStats()const{
 check(IsInGameThread());auto S=Stats;S.Running=0;S.Queued=0;for(const auto& J:Jobs){if(J->Future.IsValid())++S.Running;else if(!J->Cancelled)++S.Queued;}return S;
}
