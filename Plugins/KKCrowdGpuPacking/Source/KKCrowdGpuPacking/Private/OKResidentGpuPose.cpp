#include "OKResidentGpuPose.h"
#include "OKGpuAnimationManifest.h"
#include "OKGpuResidencyBudget.h"
#include "RHICommandList.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RenderGraphResources.h"
#include "RHIGPUReadback.h"
#include "RenderingThread.h"
#include "HAL/PlatformProcess.h"

FOKValidatedGpuBank::FOKValidatedGpuBank(){}
FOKValidatedGpuBank::~FOKValidatedGpuBank()=default;
const FOKGpuAnimationBundle& FOKValidatedGpuBank::Get()const{return *Owned;}
TSharedPtr<const FOKValidatedGpuBank,ESPMode::ThreadSafe> FOKValidatedGpuBank::Create(TUniquePtr<FOKGpuAnimationBundle> Bank,FString& Error){
 check(!IsInGameThread()&&!IsInRenderingThread());
 if(!Bank||!Bank->Manifest.Validate(Bank->Data,Error)||!OKValidateOwnedGpuPose(Bank->Data.Pose,Bank->Data.OwnedClips,Error))return nullptr;
 TSharedPtr<FOKValidatedGpuBank,ESPMode::ThreadSafe> Result(new FOKValidatedGpuBank());Result->Owned=MoveTemp(Bank);return Result;
}

struct FOKResidentGpuPose::FState {
 TRefCountPtr<FRDGPooledBuffer> Keys,Tracks,Actors,Tasks,Bind,Current;
 TArray<FUintVector4> Layout;
 TArray<TArray<uint32>> Clips;
 TArray<FVector4f> LastTimes;
 TArray<FUintVector4> LastLookBones;TArray<FVector4f> LastLookRotations;
 TSharedPtr<FOKGpuResidencyBudget,ESPMode::ThreadSafe> Manager;uint64 Lease=0;
 uint64 ImmutableBytes=0,OutputBytes=0,Budget=0;
 uint32 TaskCount=0;uint64 LastFrame=0;bool HasFrame=false;
 FOKResidentPoseStats Stats;
};
FOKResidentGpuPose::FOKResidentGpuPose():State(MakeUnique<FState>()){}
void FOKResidentGpuPose::RetireState(FState& S){
 if(!S.Manager||!S.Lease)return;
 TArray<TRefCountPtr<FRDGPooledBuffer>> Buffers={S.Keys,S.Tracks,S.Actors,S.Tasks,S.Bind,S.Current};
 const bool Retired=S.Manager->Retire(S.Lease,MoveTemp(Buffers),FRHICommandListExecutor::GetImmediateCommandList());check(Retired);S.Lease=0;
}
FOKResidentGpuPose::~FOKResidentGpuPose(){
 if(State&&State->Lease){if(IsInRenderingThread())RetireState(*State);else{auto Dead=MoveTemp(State);ENQUEUE_RENDER_COMMAND(RetireDestroyedPose)([Dead=MoveTemp(Dead)](FRHICommandListImmediate&)mutable{RetireState(*Dead);});}}
}
FOKResidentPoseStats FOKResidentGpuPose::GetStats() const{check(IsInRenderingThread());auto Stats=State->Stats;Stats.ResidentBufferBytes=State->ImmutableBytes+(State->Current?State->OutputBytes:0);return Stats;}
void FOKResidentGpuPose::Release(){check(IsInRenderingThread());UploadBank.Reset();UploadPending=false;UploadArray=0;UploadOffset=0;auto Stats=State->Stats;++Stats.Generation;RetireState(*State);State=MakeUnique<FState>();State->Stats=Stats;}
bool OKValidateOwnedGpuPose(const FOKGpuPoseBatch& B,const TArray<TArray<uint32>>& OwnedClips,FString& Error){
 if(!B.Validate(Error))return false;
 if(B.Tasks.Num()>65535*64){Error=TEXT("Pose dispatch exceeds one-dimensional GPU dispatch capacity");return false;}
 if(OwnedClips.Num()!=B.Actors.Num()){Error=TEXT("Owned clip table actor count mismatch");return false;}
 TArray<TPair<uint32,uint32>> Ranges;TArray<uint32> Owners;
 for(int A=0;A<B.Actors.Num();++A){const auto& Actor=B.Actors[A];if(OwnedClips[A].IsEmpty()){Error=TEXT("Actor has no owned clips");return false;}
  if(!OwnedClips[A].Contains(Actor.X)||!OwnedClips[A].Contains(Actor.Y)){Error=TEXT("Initial clips not owned by actor");return false;}
  for(uint32 Base:OwnedClips[A]){if(uint64(Base)+Actor.Z>uint64(B.Tracks.Num())){Error=TEXT("Owned clip range outside track data");return false;}
   for(int R=0;R<Ranges.Num();++R)if(Owners[R]!=uint32(A)&&Base<Ranges[R].Value&&uint64(Base)+Actor.Z>Ranges[R].Key){Error=TEXT("Clip track ranges overlap across actors");return false;}
   Ranges.Add({Base,Base+Actor.Z});Owners.Add(A);
   for(uint32 Bone=0;Bone<Actor.Z;++Bone){const auto& T=B.Tracks[Base+Bone];if(T.Z!=B.Tracks[Actor.X+Bone].Z||!OKGpuTrackModeValid(T)||!T.Y||OKGpuTrackEndRow(T)>uint64(B.Keys.Num())){Error=TEXT("Owned clip topology/key range incompatible");return false;}
    for(uint32 K=0;K<T.Y;++K){int I=OKGpuTrackPositionRow(T,K);if((!OKGpuTrackVectorKeys(T)&&FMath::Abs(B.Keys[OKGpuTrackRotationRow(T,K)].SizeSquared()-1.f)>0.001f)||(K&&B.Keys[I].W<=B.Keys[OKGpuTrackPositionRow(T,K-1)].W)){Error=TEXT("Owned clip keys invalid");return false;}}
   }
  }
 }
 return true;
}
bool FOKResidentGpuPose::Initialize(FRDGBuilder& G,const FOKGpuPoseBatch& B,const TArray<TArray<uint32>>& OwnedClips,FString& Error,uint64 MaxBufferBytes){
 check(IsInRenderingThread());if(!OKIndependentGpuPoseAvailable()){Error=TEXT("Independent pose requires a cooked SM6 shader");return false;}if(!OKValidateOwnedGpuPose(B,OwnedClips,Error))return false;
 // Reserve both pose outputs before the first upload. Old buffers remain referenced
 // through replacement/queued graph use; do not assume Release immediately frees VRAM.
 const uint64 Immutable=uint64(B.Keys.Num())*16+uint64(B.Tracks.Num())*16+uint64(B.Actors.Num())*16+uint64(B.Tasks.Num())*16+uint64(B.InverseBindRows.Num())*16;
 const uint64 Output=uint64(B.Tasks.Num())*3*16;
 const uint64 Old=State->ImmutableBytes+2*State->OutputBytes;
 const uint64 New=Immutable+2*Output+uint64(B.Actors.Num())*16;
 if(!MaxBufferBytes||Old>MaxBufferBytes||New>MaxBufferBytes-Old){++State->Stats.BudgetRejections;Error=TEXT("Resident GPU pose replacement exceeds buffer budget; retain current pose");return false;}
 auto Next=MakeUnique<FState>();Next->Stats=State->Stats;++Next->Stats.Generation;++Next->Stats.Initializations;
 Next->ImmutableBytes=Immutable;Next->OutputBytes=Output;Next->Budget=MaxBufferBytes;
 Next->Stats.ReservedPeakBufferBytes=FMath::Max(Next->Stats.ReservedPeakBufferBytes,Old+New);
 Next->Clips=OwnedClips;Next->Layout=B.Actors;Next->TaskCount=B.Tasks.Num();
 auto Upload=[&](const TCHAR* Name,const auto& Data){auto* Buffer=CreateStructuredBuffer(G,Name,Data);Next->Stats.ImmutableUploadBytes+=uint64(Data.Num())*Data.GetTypeSize();return G.ConvertToExternalBuffer(Buffer);};
 Next->Keys=Upload(TEXT("ResidentPose.Keys"),B.Keys);Next->Tracks=Upload(TEXT("ResidentPose.Tracks"),B.Tracks);Next->Actors=Upload(TEXT("ResidentPose.Actors"),B.Actors);Next->Tasks=Upload(TEXT("ResidentPose.Tasks"),B.Tasks);Next->Bind=Upload(TEXT("ResidentPose.Bind"),B.InverseBindRows);
 RetireState(*State);State=MoveTemp(Next);return true;
}
bool FOKResidentGpuPose::InitializeManaged(FRDGBuilder& G,const FOKGpuPoseBatch& B,const TArray<TArray<uint32>>& Clips,TSharedRef<FOKGpuResidencyBudget,ESPMode::ThreadSafe> Manager,FString& Error){
 check(IsInRenderingThread());
 const uint64 Bytes=uint64(B.Keys.Num())*16+uint64(B.Tracks.Num())*16+uint64(B.Actors.Num())*32+uint64(B.Tasks.Num())*112+uint64(B.InverseBindRows.Num())*16;
 uint64 Ticket=0;if(!Manager->Reserve(Bytes,Ticket,Error))return false;
 if(!Initialize(G,B,Clips,Error)){const bool Cancelled=Manager->CancelUnused(Ticket);check(Cancelled);return false;}
 State->Manager=Manager;State->Lease=Ticket;return true;
}
bool FOKResidentGpuPose::UploadManagedChunk(FRDGBuilder& G,TSharedRef<const FOKValidatedGpuBank,ESPMode::ThreadSafe> Bank,TSharedRef<FOKGpuResidencyBudget,ESPMode::ThreadSafe> Manager,uint64 ChunkBytes,bool& Ready,FString& Error){
 check(IsInRenderingThread());Ready=false;ChunkBytes=(ChunkBytes/16)*16;
 if(ChunkBytes<16||!OKIndependentGpuPoseAvailable()){Error=TEXT("Staged GPU upload budget/shader invalid");return false;}
 const auto& B=Bank->Get().Data.Pose;
 if(!UploadPending){
  if(State->Keys||State->HasFrame){Error=TEXT("Staged GPU upload requires an empty resident pose");return false;}
  const uint64 Bytes=uint64(B.Keys.Num())*16+uint64(B.Tracks.Num())*16+uint64(B.Actors.Num())*32+uint64(B.Tasks.Num())*112+uint64(B.InverseBindRows.Num())*16;
  uint64 Ticket=0;if(!Manager->Reserve(Bytes,Ticket,Error))return false;
  State->Manager=Manager;State->Lease=Ticket;State->Budget=4ull*1024*1024*1024;
  State->ImmutableBytes=uint64(B.Keys.Num()+uint64(B.Tracks.Num())+B.Actors.Num()+B.Tasks.Num()+B.InverseBindRows.Num())*16;State->OutputBytes=uint64(B.Tasks.Num())*48;
  State->Layout=B.Actors;State->Clips=Bank->Get().Data.OwnedClips;State->TaskCount=B.Tasks.Num();++State->Stats.Generation;++State->Stats.Initializations;State->Stats.ReservedPeakBufferBytes=Bytes;
  auto Allocate=[&](const TCHAR* Name,int32 Count){auto* Buffer=G.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(16,Count),Name);return G.ConvertToExternalBuffer(Buffer);};
  State->Keys=Allocate(TEXT("StagedPose.Keys"),B.Keys.Num());State->Tracks=Allocate(TEXT("StagedPose.Tracks"),B.Tracks.Num());State->Actors=Allocate(TEXT("StagedPose.Actors"),B.Actors.Num());State->Tasks=Allocate(TEXT("StagedPose.Tasks"),B.Tasks.Num());State->Bind=Allocate(TEXT("StagedPose.Bind"),B.InverseBindRows.Num());
  UploadBank=Bank;UploadArray=0;UploadOffset=0;UploadPending=true;
 }else if(UploadBank.Get()!=&Bank.Get()){Error=TEXT("Staged GPU upload owner changed");return false;}
 const uint8* Data[]={reinterpret_cast<const uint8*>(B.Keys.GetData()),reinterpret_cast<const uint8*>(B.Tracks.GetData()),reinterpret_cast<const uint8*>(B.Actors.GetData()),reinterpret_cast<const uint8*>(B.Tasks.GetData()),reinterpret_cast<const uint8*>(B.InverseBindRows.GetData())};
 const uint64 Sizes[]={uint64(B.Keys.Num())*16,uint64(B.Tracks.Num())*16,uint64(B.Actors.Num())*16,uint64(B.Tasks.Num())*16,uint64(B.InverseBindRows.Num())*16};
 TRefCountPtr<FRDGPooledBuffer>* Targets[]={&State->Keys,&State->Tracks,&State->Actors,&State->Tasks,&State->Bind};
 while(UploadArray<5&&ChunkBytes){const uint64 Count=FMath::Min(ChunkBytes,Sizes[UploadArray]-UploadOffset);
  auto* Staging=CreateStructuredBuffer(G,TEXT("StagedPose.Upload"),16,uint32(Count/16),Data[UploadArray]+UploadOffset,Count);
  auto* Target=G.RegisterExternalBuffer(*Targets[UploadArray]);AddCopyBufferPass(G,Target,UploadOffset,Staging,0,Count);
  State->Stats.ImmutableUploadBytes+=Count;UploadOffset+=Count;ChunkBytes-=Count;
  if(UploadOffset==Sizes[UploadArray]){++UploadArray;UploadOffset=0;}
 }
 Ready=UploadArray==5;if(Ready){UploadPending=false;UploadBank.Reset();}Error.Reset();return true;
}

bool FOKResidentGpuPose::InitializeShared(const FOKResidentGpuPose& Source,FString& Error){
 check(IsInRenderingThread());const auto& S=*Source.State;
 if(this==&Source||!S.Keys||S.HasFrame){Error=TEXT("Shared pose initialization requires a distinct, initialized, unevaluated source");return false;}
 const uint64 Old=State->ImmutableBytes+2*State->OutputBytes;
 const uint64 New=S.ImmutableBytes+2*S.OutputBytes+uint64(S.Layout.Num())*16;
 if(!S.Budget||Old>S.Budget||New>S.Budget-Old){++State->Stats.BudgetRejections;Error=TEXT("Shared GPU pose replacement exceeds logical buffer budget");return false;}
 uint64 Ticket=0;if(S.Manager&&!S.Manager->ReserveShared(S.Lease,2*S.OutputBytes+uint64(S.Layout.Num())*16,Ticket,Error))return false;
 auto Next=MakeUnique<FState>();Next->Manager=S.Manager;Next->Lease=Ticket;Next->Keys=S.Keys;Next->Tracks=S.Tracks;Next->Actors=S.Actors;Next->Tasks=S.Tasks;Next->Bind=S.Bind;
 Next->Layout=S.Layout;Next->Clips=S.Clips;Next->TaskCount=S.TaskCount;
 Next->ImmutableBytes=S.ImmutableBytes;Next->OutputBytes=S.OutputBytes;Next->Budget=S.Budget;
 Next->Stats=State->Stats;++Next->Stats.Generation;++Next->Stats.Initializations;
 Next->Stats.ReservedPeakBufferBytes=FMath::Max(Next->Stats.ReservedPeakBufferBytes,Old+New);
 RetireState(*State);State=MoveTemp(Next);return true;
}
bool FOKResidentGpuPose::TryReuseCurrent(FRDGBuilder& G,TConstArrayView<FOKGpuPosePlayback> Playback,uint64 Generation,FOKResidentPoseFrame& Out) const{
 check(IsInRenderingThread());Out={};if(UploadPending)return false;const auto& S=*State;
 if(!S.HasFrame||!S.Current||Generation!=S.Stats.Generation||Playback.Num()!=S.Layout.Num()||Playback.Num()!=S.LastTimes.Num())return false;
 for(int A=0;A<Playback.Num();++A){const auto& P=Playback[A];
  if(P.ClipA>=uint32(S.Clips[A].Num())||P.ClipB>=uint32(S.Clips[A].Num())||S.Clips[A][P.ClipA]!=S.Layout[A].X||S.Clips[A][P.ClipB]!=S.Layout[A].Y)return false;
  for(int C=0;C<4;++C)if(!FMath::IsFinite(P.Times[C])||P.Times[C]!=S.LastTimes[A][C])return false;
  if(!S.LastLookBones.IsValidIndex(A)||S.LastLookBones[A]!=FUintVector4(P.LookNeck,P.LookHead,0,0)||S.LastLookRotations[A*2]!=P.NeckRotation||S.LastLookRotations[A*2+1]!=P.HeadRotation)return false;
 }
 Out.Current=G.RegisterExternalBuffer(S.Current);Out.Previous=Out.Current;Out.Generation=Generation;Out.Reused=true;return true;
}
bool FOKResidentGpuPose::Evaluate(FRDGBuilder& G,TConstArrayView<FOKGpuPosePlayback> Playback,uint64 FrameIndex,uint64 Generation,bool Reset,FOKResidentPoseFrame& Out,FString& Error){
 check(IsInRenderingThread());if(UploadPending){Error=TEXT("Resident GPU pose upload pending");return false;}Out={};auto& S=*State;if(S.Manager)S.Manager->Collect();
 if(!OKIndependentGpuPoseAvailable()){Error=TEXT("Independent pose shader unavailable; retain CPU animation");return false;}
 if(!S.Keys||Generation!=S.Stats.Generation||Playback.Num()!=S.Layout.Num()||(S.HasFrame&&FrameIndex<=S.LastFrame)){Error=TEXT("Uninitialized/stale generation/frame or actor count");return false;}
 TArray<FUintVector4> Layout=S.Layout;TArray<FVector4f> Times;Times.Reserve(Playback.Num());
 TArray<FUintVector4> LookBones;TArray<FVector4f> LookRotations;LookBones.Reserve(Playback.Num());LookRotations.Reserve(Playback.Num()*2);
 for(int A=0;A<Playback.Num();++A){const auto& P=Playback[A];if(P.ClipA>=uint32(S.Clips[A].Num())||P.ClipB>=uint32(S.Clips[A].Num())){Error=TEXT("Animation ID outside actor's owned clips");return false;}
  for(int C=0;C<4;++C)if(!FMath::IsFinite(P.Times[C])){Error=TEXT("Nonfinite playback parameter");return false;}
  if(P.Times.Z<0||P.Times.Z>1){Error=TEXT("Blend outside [0,1]");return false;}
  Layout[A].X=S.Clips[A][P.ClipA];Layout[A].Y=S.Clips[A][P.ClipB];Times.Add(P.Times);
  for(int C=0;C<4;++C)if(!FMath::IsFinite(P.NeckRotation[C])||!FMath::IsFinite(P.HeadRotation[C])){Error=TEXT("Nonfinite look rotation");return false;}
  LookBones.Add(FUintVector4(P.LookNeck,P.LookHead,0,0));LookRotations.Add(P.NeckRotation);LookRotations.Add(P.HeadRotation);
 }
 bool ChangedActors=FMemory::Memcmp(Layout.GetData(),S.Layout.GetData(),Layout.Num()*sizeof(FUintVector4))!=0;
 bool Same=S.HasFrame&&!ChangedActors&&Times.Num()==S.LastTimes.Num()&&FMemory::Memcmp(Times.GetData(),S.LastTimes.GetData(),Times.Num()*sizeof(FVector4f))==0&&LookBones==S.LastLookBones&&LookRotations==S.LastLookRotations;
 const bool AnyLook=LookBones.ContainsByPredicate([](const FUintVector4& L){return L.X!=MAX_uint32||L.Y!=MAX_uint32;});
 Out.Generation=S.Stats.Generation;Out.Reused=Same;
 if(Same){Out.Current=G.RegisterExternalBuffer(S.Current);Out.Previous=Out.Current;++S.Stats.ReusedFrames;}
 else{
  FOKGpuPoseBuffers B;B.Keys=G.RegisterExternalBuffer(S.Keys);B.Tracks=G.RegisterExternalBuffer(S.Tracks);B.Tasks=G.RegisterExternalBuffer(S.Tasks);B.InverseBindRows=G.RegisterExternalBuffer(S.Bind);B.TaskCount=S.TaskCount;
  if(ChangedActors){auto* Actors=CreateStructuredBuffer(G,TEXT("ResidentPose.ActorSelection"),Layout);S.Actors=G.ConvertToExternalBuffer(Actors);S.Stats.ParameterUploadBytes+=uint64(Layout.Num())*sizeof(FUintVector4);}
  B.Actors=G.RegisterExternalBuffer(S.Actors);B.Times=CreateStructuredBuffer(G,TEXT("ResidentPose.Playback"),Times);S.Stats.ParameterUploadBytes+=uint64(Times.Num())*sizeof(FVector4f);
  if(AnyLook){B.LookBones=CreateStructuredBuffer(G,TEXT("ResidentPose.LookBones"),LookBones);B.LookRotations=CreateStructuredBuffer(G,TEXT("ResidentPose.LookRotations"),LookRotations);B.LookCount=LookBones.Num();}
  Out.Current=OKAddIndependentGpuPosePass(G,B);if(!Out.Current){Error=TEXT("Pose dispatch rejected");return false;}
  Out.Previous=S.HasFrame&&!Reset?G.RegisterExternalBuffer(S.Current):Out.Current;
  S.Current=G.ConvertToExternalBuffer(Out.Current);++S.Stats.Evaluations;
 }
 S.Layout=MoveTemp(Layout);S.LastTimes=MoveTemp(Times);S.LastLookBones=MoveTemp(LookBones);S.LastLookRotations=MoveTemp(LookRotations);S.LastFrame=FrameIndex;S.HasFrame=true;return true;
}
bool OKReadbackResidentGpuPose(FOKResidentGpuPose& Pose,const FOKGpuPoseBatch* Initial,const TArray<TArray<uint32>>& Clips,TConstArrayView<FOKGpuPosePlayback> Playback,uint64 FrameIndex,bool Reset,TArray<FVector4f>& Current,TArray<FVector4f>& Previous,FOKResidentPoseStats& Stats,FString& Error,TSharedPtr<FOKGpuResidencyBudget,ESPMode::ThreadSafe> Manager){
 auto A=MakeShared<FRHIGPUBufferReadback,ESPMode::ThreadSafe>(TEXT("Resident.Current"));auto B=MakeShared<FRHIGPUBufferReadback,ESPMode::ThreadSafe>(TEXT("Resident.Previous"));bool Submitted=false;uint32 Bytes=0;
 ENQUEUE_RENDER_COMMAND(ResidentOracle)([&](FRHICommandListImmediate& R){FRDGBuilder G(R);if(Initial&&!(Manager?Pose.InitializeManaged(G,*Initial,Clips,Manager.ToSharedRef(),Error):Pose.Initialize(G,*Initial,Clips,Error))){G.Execute();return;}FOKResidentPoseFrame Frame;Stats=Pose.GetStats();if(!Pose.Evaluate(G,Playback,FrameIndex,Stats.Generation,Reset,Frame,Error)){G.Execute();return;}Bytes=Frame.Current->Desc.GetSize();AddEnqueueCopyPass(G,&A.Get(),Frame.Current,Bytes);AddEnqueueCopyPass(G,&B.Get(),Frame.Previous,Bytes);G.Execute();Stats=Pose.GetStats();Submitted=true;});FlushRenderingCommands();if(!Submitted)return false;
 bool Ready=false;double Start=FPlatformTime::Seconds();while(FPlatformTime::Seconds()-Start<30){ENQUEUE_RENDER_COMMAND(ResidentPoll)([&](FRHICommandListImmediate& R){Ready=A->IsReady()&&B->IsReady();});FlushRenderingCommands();if(Ready)break;FPlatformProcess::Sleep(.01f);}
 if(!Ready){Error=TEXT("Resident pose readback timed out");return false;}
 Current.SetNumUninitialized(Bytes/sizeof(FVector4f));Previous.SetNumUninitialized(Current.Num());
 ENQUEUE_RENDER_COMMAND(ResidentRead)([&](FRHICommandListImmediate& R){FMemory::Memcpy(Current.GetData(),A->Lock(Bytes),Bytes);A->Unlock();FMemory::Memcpy(Previous.GetData(),B->Lock(Bytes),Bytes);B->Unlock();});FlushRenderingCommands();return true;
}
