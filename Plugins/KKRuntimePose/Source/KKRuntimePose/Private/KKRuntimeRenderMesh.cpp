#include "KKRuntimeRenderMesh.h"
#include "Async/Async.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/Skeleton.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Materials/Material.h"
#include "UObject/StrongObjectPtr.h"
#include "RenderingThread.h"
#include "RHICommandList.h"
#include "Async/ParallelFor.h"
#include "Tasks/Pipe.h"

static bool ValidateRuntimeFace(const FKKRuntimeMouthStream& Stream,const FKKRuntimeFaceState& State,FString& Error){
 auto OpeningOk=[&](float Value){return FMath::IsFinite(Value)&&(Value==-1.f||(Value>=0.f&&Value<=1.f));};
 if(!OpeningOk(State.MouthOpening)||!OpeningOk(State.EyeOpening)||!OpeningOk(State.BrowOpening)){Error=TEXT("Runtime face opening invalid");return false;}
 const int32 Patterns[]={State.MouthPattern,State.EyePattern,State.BrowPattern};
 for(int32 Control=0;Control<3;++Control){const int32 Pattern=Patterns[Control],Count=Stream.PatternCount[Control];
  if(Pattern<-1||(Pattern>=0&&(Count<=0||Pattern>=Count))){Error=TEXT("Runtime face pattern invalid");return false;}}
 Error.Reset();return true;
}
static bool RuntimeFaceRangesOk(const FKKRuntimeMouthStream& Stream,FString& Error){
 if(Stream.Vertices.IsEmpty()||Stream.Ranges.IsEmpty()){Error=TEXT("Runtime face range data invalid");return false;}
 uint64 Covered=0;for(const auto& Range:Stream.Ranges){if(!Range.Count||Range.DataOffset!=Covered||uint64(Range.DataOffset)+Range.Count>uint64(Stream.Vertices.Num())){Error=TEXT("Runtime face range data invalid");return false;}Covered+=Range.Count;}
 if(Covered!=uint64(Stream.Vertices.Num())){Error=TEXT("Runtime face ranges incomplete");return false;}return true;
}
// Brow, then eyes, then mouth. A shared native morph keeps the later controller only.
struct FFaceAcc{int32 Native;float Weight;FVector3f Delta;};
static void AccumulateFaceControl(TArray<FFaceAcc,TInlineAllocator<16>>& Channels,const FKKRuntimeMouthVertex& V,int32 Control,int32 Pattern,float OpenRate,float Scale){
 if(Pattern<0||Scale==0.f||!V.FaceCatalog)return;
 const auto& C=V.FaceCatalog->Control[Control];if(!C.Close.IsValidIndex(Pattern)||!C.Open.IsValidIndex(Pattern))return;
 const int16 Close=C.Close[Pattern],Open=C.Open[Pattern];
 for(const auto& Sample:V.Face)if(Sample.Control==uint8(Control)){
  float Desired=0.f;if(Close>=0&&int16(Sample.Local)==Close)Desired+=1.f-OpenRate;if(Open>=0&&int16(Sample.Local)==Open)Desired+=OpenRate;
  const float Baseline=C.Baseline.IsValidIndex(Sample.Local)?C.Baseline[Sample.Local]:0.f;const float Weight=(Desired-Baseline)*Scale;bool Found=false;
  for(auto& Channel:Channels)if(Channel.Native==Sample.Native){Channel.Weight=Weight;Channel.Delta=Sample.Delta;Found=true;break;}
  if(!Found)Channels.Add({Sample.Native,Weight,Sample.Delta});
 }
}
static void AccumulateAuthoredFace(TArray<FFaceAcc,TInlineAllocator<16>>& Channels,const FKKRuntimeMouthVertex& V,int32 Control,int32 Pattern,float Opening,float Audio,float Gain,float Blink){
 if(Control==2)AccumulateFaceControl(Channels,V,2,Pattern,Opening>=0.f?Opening:1.f,1.f);
 else if(Control==1){if(Pattern<0)return;if(Opening<0.f)AccumulateFaceControl(Channels,V,1,Pattern,0.f,Blink);else AccumulateFaceControl(Channels,V,1,Pattern,Opening*(1.f-Blink),1.f);}
 else if(Pattern>=0&&Gain>0.f)AccumulateFaceControl(Channels,V,0,Pattern,Audio*(Opening>=0.f?Opening:1.f),Gain);
}
static FVector3f RuntimeFaceDelta(const FKKRuntimeMouthVertex& V,float Opening,float Gain,float Blink,const FKKRuntimeFaceState& State){
 TArray<FFaceAcc,TInlineAllocator<16>> Channels;
 AccumulateAuthoredFace(Channels,V,2,State.BrowPattern,State.BrowOpening,Opening,Gain,Blink);
 AccumulateAuthoredFace(Channels,V,1,State.EyePattern,State.EyeOpening,Opening,Gain,Blink);
 AccumulateAuthoredFace(Channels,V,0,State.MouthPattern,State.MouthOpening,Opening,Gain,Blink);
 FVector3f Delta=FVector3f::ZeroVector;for(const auto& Channel:Channels)Delta+=Channel.Delta*Channel.Weight;return Delta;
}
// Face vertices are independent of each other: evaluated across the task threads in chunks. On one core a face upload
// took the game thread 3.6 ms, a blink every few seconds for each of ten heroines and every mouth frame while one speaks.
static TAutoConsoleVariable<int32> CVarOKFaceParallel(TEXT("ok.Face.Parallel"),1,TEXT("1: evaluate face vertices across the task threads; 0: on the calling thread (for comparing)."));
template<typename FEval> static bool EvaluateFaceVertices(const FKKRuntimeMouthStream& Stream,TArray<FVector3f>& Out,FString& Error,FEval&& Eval){
 constexpr int32 Chunk=1024;const int32 N=Stream.Vertices.Num();TArray<FVector3f> Positions;Positions.SetNumUninitialized(N);TAtomic<bool> Bad{false};
 const EParallelForFlags Flags=CVarOKFaceParallel.GetValueOnAnyThread()?EParallelForFlags::None:EParallelForFlags::ForceSingleThread;
 ParallelFor(FMath::DivideAndRoundUp(N,Chunk),[&](int32 C){const int32 End=FMath::Min(N,(C+1)*Chunk);for(int32 I=C*Chunk;I<End;++I){const FVector3f P=Eval(Stream.Vertices[I]);if(P.ContainsNaN()){Bad=true;return;}Positions[I]=P;}},Flags);
 if(Bad){Error=TEXT("Runtime face position nonfinite");return false;}
 Out=MoveTemp(Positions);Error.Reset();return true;
}
bool OKValidateRuntimeFaceState(TSharedPtr<const FKKRuntimeMouthStream,ESPMode::ThreadSafe> Stream,const FKKRuntimeFaceState& State,FString& Error){
 if(!Stream){Error=TEXT("Runtime face stream unavailable");return false;}return ValidateRuntimeFace(*Stream,State,Error);
}
bool OKEvaluateRuntimeFacePositions(const FKKRuntimeMouthStream& Stream,float Opening,float Gain,float Blink,const FKKRuntimeFaceState* Face,TArray<FVector3f>& Out,FString& Error){
 if(!FMath::IsFinite(Opening)||!FMath::IsFinite(Gain)||Opening<0.f||Opening>1.f||Gain<0.f||Gain>1.f||!FMath::IsFinite(Blink)||Blink<0.f||Blink>1.f){Error=TEXT("Runtime mouth update invalid");return false;}
 if(!RuntimeFaceRangesOk(Stream,Error))return false;
 if(Face&&!ValidateRuntimeFace(Stream,*Face,Error))return false;
 return EvaluateFaceVertices(Stream,Out,Error,[&](const FKKRuntimeMouthVertex& V){
  return Face?V.Base+RuntimeFaceDelta(V,Opening,Gain,Blink,*Face):V.Base+FMath::Lerp(V.Closed,V.Open,Opening)*Gain+V.Blink*Blink;});
}
TSharedPtr<const FKKRuntimeMouthStream,ESPMode::ThreadSafe> OKPrepareRuntimeMouthStream(const FKKRuntimeRenderMeshData& D){
 auto Stream=MakeShared<FKKRuntimeMouthStream,ESPMode::ThreadSafe>();int32 Last=INDEX_NONE;int32 MinCount[3]={MAX_int32,MAX_int32,MAX_int32};bool Seen[3]={};
 for(int32 I=0;I<D.Vertices.Num();++I){const auto& V=D.Vertices[I];const bool Mouth=!V.MouthClosed.IsNearlyZero()||!V.MouthOpen.IsNearlyZero(),Blink=!V.BlinkClosed.IsNearlyZero(),Face=V.Face.Num()>0;if(!Mouth&&!Blink&&!Face)continue;
  // Retain short unchanged gaps to bound upload commands, never bridge distant
  // mesh sections just because their face vertices share the same controller.
  if(Last==INDEX_NONE||I-Last>64){Stream->Ranges.Add({uint32(I),uint32(Stream->Vertices.Num()),0});if(Last==INDEX_NONE)Stream->FirstVertex=I;Last=I-1;}
  for(int32 K=Last+1;K<=I;++K){const auto& P=D.Vertices[K];FKKRuntimeMouthVertex Vertex;Vertex.Base=P.Position;Vertex.Closed=P.MouthClosed;Vertex.Open=P.MouthOpen;Vertex.Blink=P.BlinkClosed;Vertex.Face=P.Face;Vertex.FaceCatalog=P.FaceCatalog;Stream->Vertices.Add(MoveTemp(Vertex));++Stream->Ranges.Last().Count;
   if(P.FaceCatalog)for(int32 Control=0;Control<3;++Control){const int32 Count=P.FaceCatalog->Control[Control].Close.Num();if(Count>0){Seen[Control]=true;MinCount[Control]=FMath::Min(MinCount[Control],Count);}}
  }
  Stream->AffectedVertices+=Mouth;Stream->BlinkVertices+=Blink;Last=I;
 }
 for(int32 Control=0;Control<3;++Control)Stream->PatternCount[Control]=Seen[Control]?MinCount[Control]:0;
 if(Stream->Ranges.IsEmpty())return nullptr;return Stream;
}
static TAutoConsoleVariable<int32> CVarOKFaceAsync(TEXT("ok.Face.Async"),1,TEXT("1: evaluate and upload face vertices on a task (a frame later at most); 0: on the game thread (for comparing)."));
static UE::Tasks::FPipe& OKRuntimeFacePipe(){static UE::Tasks::FPipe Pipe(TEXT("OKRuntimeFace"));return Pipe;}
bool OKUpdateRuntimeMouthStream(USkeletalMesh* Main,USkeletalMesh* Depth,TSharedPtr<const FKKRuntimeMouthStream,ESPMode::ThreadSafe> Stream,float Opening,float Gain,FString& Error,float Blink,const FKKRuntimeFaceState* Face,const FKKRuntimeFaceTransition* Transition){
 check(IsInGameThread());if(!Stream||!Main||!FMath::IsFinite(Opening)||!FMath::IsFinite(Gain)||Opening<0.f||Opening>1.f||Gain<0.f||Gain>1.f||!FMath::IsFinite(Blink)||Blink<0.f||Blink>1.f){Error=TEXT("Runtime mouth update invalid");return false;}
 TArray<FBufferRHIRef> Buffers;for(auto* Mesh:{Main,Depth})if(Mesh){auto* R=Mesh->GetResourceForRendering();if(!R||R->LODRenderData.IsEmpty()){Error=TEXT("Runtime mouth mesh resource missing");return false;}const auto& V=R->LODRenderData[0].StaticVertexBuffers.PositionVertexBuffer;if(V.GetStride()!=sizeof(FVector3f)||Stream->Ranges.IsEmpty()||!V.VertexBufferRHI){Error=TEXT("Runtime mouth vertex layout not ready");return false;}Buffers.Add(V.VertexBufferRHI);}
 uint64 Covered=0;for(const auto& Range:Stream->Ranges){if(!Range.Count||Range.DataOffset!=Covered||uint64(Range.DataOffset)+Range.Count>uint64(Stream->Vertices.Num())){Error=TEXT("Runtime face range data invalid");return false;}for(auto* Mesh:{Main,Depth})if(Mesh&&uint64(Range.FirstVertex)+Range.Count>Mesh->GetResourceForRendering()->LODRenderData[0].StaticVertexBuffers.PositionVertexBuffer.GetNumVertices()){Error=TEXT("Runtime face range outside mesh");return false;}Covered+=Range.Count;}if(Covered!=uint64(Stream->Vertices.Num())){Error=TEXT("Runtime face ranges incomplete");return false;}
 auto Ranges=Stream->Ranges;
 // Off the game thread: the vertices are evaluated by a task and uploaded from there (a frame later at most). The
 // inputs are checked here so a bad request still fails the caller; the tasks run one after another (one pipe), so an
 // older face never lands over a newer one. Evaluated here, each update held the game thread ~1.8 ms, many a minute.
 if(CVarOKFaceAsync.GetValueOnGameThread()){
  if(!RuntimeFaceRangesOk(*Stream,Error)||(Face&&!ValidateRuntimeFace(*Stream,*Face,Error)))return false;
  TOptional<FKKRuntimeFaceState> FaceCopy;if(Face)FaceCopy=*Face;TOptional<FKKRuntimeFaceTransition> TransitionCopy;if(Transition)TransitionCopy=*Transition;
  OKRuntimeFacePipe().Launch(TEXT("OKRuntimeFace"),[Stream,Opening,Gain,Blink,FaceCopy=MoveTemp(FaceCopy),TransitionCopy=MoveTemp(TransitionCopy),Buffers=MoveTemp(Buffers),Ranges=MoveTemp(Ranges)]()mutable{
   TArray<FVector3f> Positions;FString TaskError;
   const bool Ok=TransitionCopy?OKEvaluateRuntimeFaceTransition(*Stream,Opening,Gain,Blink,*TransitionCopy,Positions,TaskError):OKEvaluateRuntimeFacePositions(*Stream,Opening,Gain,Blink,FaceCopy?FaceCopy.GetPtrOrNull():nullptr,Positions,TaskError);
   if(!Ok){UE_LOG(LogTemp,Warning,TEXT("Runtime face update dropped: %s"),*TaskError);return;}
   ENQUEUE_RENDER_COMMAND(UpdateRuntimeSpeechMouth)([Buffers=MoveTemp(Buffers),Positions=MoveTemp(Positions),Ranges=MoveTemp(Ranges)](FRHICommandListImmediate& Cmd){
    for(const auto& Buffer:Buffers)for(const auto& Range:Ranges){const uint32 Bytes=Range.Count*sizeof(FVector3f);void* Destination=Cmd.LockBuffer(Buffer,Range.FirstVertex*sizeof(FVector3f),Bytes,RLM_WriteOnly);FMemory::Memcpy(Destination,Positions.GetData()+Range.DataOffset,Bytes);Cmd.UnlockBuffer(Buffer);}
   });
  });
  Error.Reset();return true;
 }
 TArray<FVector3f> Positions;if(Transition){if(!OKEvaluateRuntimeFaceTransition(*Stream,Opening,Gain,Blink,*Transition,Positions,Error))return false;}else if(!OKEvaluateRuntimeFacePositions(*Stream,Opening,Gain,Blink,Face,Positions,Error))return false;
 ENQUEUE_RENDER_COMMAND(UpdateRuntimeSpeechMouth)([Buffers=MoveTemp(Buffers),Positions=MoveTemp(Positions),Ranges=MoveTemp(Ranges)](FRHICommandListImmediate& Cmd){
  for(const auto& Buffer:Buffers)for(const auto& Range:Ranges){const uint32 Bytes=Range.Count*sizeof(FVector3f);void* Destination=Cmd.LockBuffer(Buffer,Range.FirstVertex*sizeof(FVector3f),Bytes,RLM_WriteOnly);FMemory::Memcpy(Destination,Positions.GetData()+Range.DataOffset,Bytes);Cmd.UnlockBuffer(Buffer);}
 });Error.Reset();return true;
}

static void CollectFaceCatalogs(const FKKRuntimeMouthStream& Stream,TArray<TSharedPtr<const FKKRuntimeFaceCatalog,ESPMode::ThreadSafe>>& Out){
 Out.Reset();for(const auto& V:Stream.Vertices){if(!V.FaceCatalog)continue;bool Found=false;for(const auto& C:Out)if(C.Get()==V.FaceCatalog.Get()){Found=true;break;}if(!Found)Out.Add(V.FaceCatalog);}
}
static bool SameFaceCatalogs(const TArray<TSharedPtr<const FKKRuntimeFaceCatalog,ESPMode::ThreadSafe>>& A,const TArray<TSharedPtr<const FKKRuntimeFaceCatalog,ESPMode::ThreadSafe>>& B){
 if(A.Num()!=B.Num())return false;for(int32 I=0;I<A.Num();++I)if(A[I].Get()!=B[I].Get())return false;return true;
}
static bool BuildFaceWeights(const FKKRuntimeFaceControl& Control,int32 Pattern,float Opening,int32 Kind,FKKRuntimeFaceWeightSet& Out,FString& Error){
 Out={};if(Pattern<0||!Control.Close.IsValidIndex(Pattern)||!Control.Open.IsValidIndex(Pattern))return true;
 const int32 N=Control.Baseline.Num();if(N>OKRuntimeFaceMorphLimit){Error=TEXT("Runtime face morph set invalid");return false;}if(N<=0)return true;
 const int16 Close=Control.Close[Pattern],Open=Control.Open[Pattern];if((Close>=0&&Close>=N)||(Open>=0&&Open>=N)){Error=TEXT("Runtime face morph set invalid");return false;}
 TArray<float> C0,C1;C0.SetNumZeroed(N);C1.SetNumZeroed(N);if(Close>=0)C0[Close]+=1.f;if(Open>=0)C1[Open]+=1.f;
 for(int32 I=0;I<N;++I){const float CloseW=C0[I],OpenW=C1[I];C1[I]=OpenW-CloseW;C0[I]=CloseW;}
 Out.A.SetNumZeroed(N);Out.B.SetNumZeroed(N);
 if(Kind==0){const float Expr=Opening>=0.f?Opening:1.f;for(int32 I=0;I<N;++I){Out.A[I]=C0[I]-Control.Baseline[I];Out.B[I]=Expr*C1[I];}}
 else if(Kind==1){if(Opening<0.f){for(int32 I=0;I<N;++I){Out.A[I]=0.f;Out.B[I]=C0[I]-Control.Baseline[I];}}else for(int32 I=0;I<N;++I){Out.A[I]=C0[I]+Opening*C1[I]-Control.Baseline[I];Out.B[I]=-Opening*C1[I];}}
 else{const float Rate=Opening>=0.f?Opening:1.f;for(int32 I=0;I<N;++I){Out.A[I]=C0[I]+Rate*C1[I]-Control.Baseline[I];Out.B[I]=0.f;}}
 return true;
}
static FKKRuntimeFaceWeightSet LerpFaceWeights(const FKKRuntimeFaceWeightSet* From,const FKKRuntimeFaceWeightSet* To,float U){
 const int32 N=FMath::Max(From?From->A.Num():0,To?To->A.Num():0);FKKRuntimeFaceWeightSet Out;if(!N)return Out;Out.A.SetNumZeroed(N);Out.B.SetNumZeroed(N);
 for(int32 I=0;I<N;++I){const float FA=From&&From->A.IsValidIndex(I)?From->A[I]:0.f,FB=From&&From->B.IsValidIndex(I)?From->B[I]:0.f,TA=To&&To->A.IsValidIndex(I)?To->A[I]:0.f,TB=To&&To->B.IsValidIndex(I)?To->B[I]:0.f;Out.A[I]=FMath::Lerp(FA,TA,U);Out.B[I]=FMath::Lerp(FB,TB,U);}return Out;
}
static bool FaceWeightsMatch(const TArray<FKKRuntimeFaceWeightSet>& A,const TArray<FKKRuntimeFaceWeightSet>& B){
 const int32 N=FMath::Max(A.Num(),B.Num());
 for(int32 I=0;I<N;++I){const int32 M=FMath::Max(A.IsValidIndex(I)?FMath::Max(A[I].A.Num(),A[I].B.Num()):0,B.IsValidIndex(I)?FMath::Max(B[I].A.Num(),B[I].B.Num()):0);
  for(int32 K=0;K<M;++K){const float AA=A.IsValidIndex(I)&&A[I].A.IsValidIndex(K)?A[I].A[K]:0.f,AB=A.IsValidIndex(I)&&A[I].B.IsValidIndex(K)?A[I].B[K]:0.f,BA=B.IsValidIndex(I)&&B[I].A.IsValidIndex(K)?B[I].A[K]:0.f,BB=B.IsValidIndex(I)&&B[I].B.IsValidIndex(K)?B[I].B[K]:0.f;if(FMath::Abs(AA-BA)>1e-5f||FMath::Abs(AB-BB)>1e-5f)return false;}}
 return true;
}
static int32 FindFaceCatalog(const FKKRuntimeFaceTransition& T,const FKKRuntimeFaceCatalog* Catalog){
 if(!Catalog)return INDEX_NONE;for(int32 I=0;I<T.Catalogs.Num();++I)if(T.Catalogs[I].Get()==Catalog)return I;return INDEX_NONE;
}
static void AccumulateBlendedFace(TArray<FFaceAcc,TInlineAllocator<16>>& Channels,const FKKRuntimeMouthVertex& V,const FKKRuntimeFaceTransition& T,int32 Control,int32 Catalog,float U,float L,float Scale){
 if(Scale==0.f||!V.FaceCatalog||Catalog==INDEX_NONE)return;const auto& Ch=T.Channel[Control];
 const FKKRuntimeFaceWeightSet* From=Ch.From.IsValidIndex(Catalog)?&Ch.From[Catalog]:nullptr;const FKKRuntimeFaceWeightSet* To=Ch.To.IsValidIndex(Catalog)?&Ch.To[Catalog]:nullptr;
 auto Coeff=[&](const FKKRuntimeFaceWeightSet* W,int32 Local){if(!W||!W->A.IsValidIndex(Local))return 0.f;return W->A[Local]+L*(W->B.IsValidIndex(Local)?W->B[Local]:0.f);};
 for(const auto& Sample:V.Face)if(Sample.Control==uint8(Control)){const float Weight=((1.f-U)*Coeff(From,Sample.Local)+U*Coeff(To,Sample.Local))*Scale;bool Found=false;
  for(auto& Channel:Channels)if(Channel.Native==Sample.Native){Channel.Weight=Weight;Channel.Delta=Sample.Delta;Found=true;break;}if(!Found)Channels.Add({Sample.Native,Weight,Sample.Delta});}
}
bool OKApplyRuntimeFaceTransition(const FKKRuntimeMouthStream& Stream,const FKKRuntimeFaceTransition& Current,const FKKRuntimeFaceState& Request,bool LegacyPose,FKKRuntimeFaceTransition& Out,bool& Changed,int32& Started,FString& Error){
 if(!ValidateRuntimeFace(Stream,Request,Error))return false;
 TArray<TSharedPtr<const FKKRuntimeFaceCatalog,ESPMode::ThreadSafe>> Catalogs;CollectFaceCatalogs(Stream,Catalogs);
 const bool CatalogsMatch=SameFaceCatalogs(Current.Catalogs,Catalogs);
 const int32 Patterns[]={Request.MouthPattern,Request.EyePattern,Request.BrowPattern};
 const float Openings[]={Request.MouthOpening,Request.EyeOpening,Request.BrowOpening};
 const bool Blends[]={Request.MouthBlend,Request.EyeBlend,Request.BrowBlend};
 FKKRuntimeFaceTransition Next=Current;Next.Catalogs=Catalogs;bool AnyChange=false;int32 AnyStart=0;
 for(int32 Control=0;Control<3;++Control){const auto& Src=Current.Channel[Control];
  if(!LegacyPose&&CatalogsMatch&&Src.Pattern==Patterns[Control]&&Src.Opening==Openings[Control]&&Src.Blend==Blends[Control]&&Src.From.Num()==Catalogs.Num())continue;
  TArray<FKKRuntimeFaceWeightSet> Target;Target.SetNum(Catalogs.Num());
  for(int32 I=0;I<Catalogs.Num();++I)if(!BuildFaceWeights(Catalogs[I]->Control[Control],Patterns[Control],Openings[Control],Control,Target[I],Error))return false;
  TArray<FKKRuntimeFaceWeightSet> CurrentMorph;CurrentMorph.SetNum(Catalogs.Num());float CurrentLegacy=0.f;
  if(LegacyPose)CurrentLegacy=Control==2?0.f:1.f;
  else if(CatalogsMatch){const float U=Src.Timing?FMath::Clamp(Src.Elapsed/OKRuntimeFaceTransitionSeconds,0.f,1.f):1.f;CurrentLegacy=Src.Timing?Src.Legacy*(1.f-U):Src.Legacy;
   for(int32 I=0;I<Catalogs.Num();++I)CurrentMorph[I]=LerpFaceWeights(Src.From.IsValidIndex(I)?&Src.From[I]:nullptr,Src.To.IsValidIndex(I)?&Src.To[I]:nullptr,Src.Timing?U:1.f);}
  auto& Ch=Next.Channel[Control];Ch.Pattern=Patterns[Control];Ch.Opening=Openings[Control];Ch.Blend=Blends[Control];AnyChange=true;
  if(!Blends[Control]||(CurrentLegacy==0.f&&FaceWeightsMatch(CurrentMorph,Target))){Ch.Legacy=0.f;Ch.From=Target;Ch.To=MoveTemp(Target);Ch.Timing=false;Ch.Elapsed=OKRuntimeFaceTransitionSeconds;}
  else{Ch.Legacy=CurrentLegacy;Ch.From=MoveTemp(CurrentMorph);Ch.To=MoveTemp(Target);Ch.Timing=true;Ch.Elapsed=0.f;++AnyStart;}
 }
 Out=MoveTemp(Next);Changed=AnyChange;Started=AnyStart;Error.Reset();return true;
}
bool OKAdvanceRuntimeFaceTransition(FKKRuntimeFaceTransition& Transition,float Dt,int32& CompletedChannels,FString& Error){
 if(!FMath::IsFinite(Dt)||Dt<0.f){Error=TEXT("Runtime face transition dt invalid");return false;}
 const float Step=FMath::Min(Dt,1.f);CompletedChannels=0;
 for(auto& Ch:Transition.Channel)if(Ch.Timing){const float Next=Ch.Elapsed+Step;
  if(Next>=OKRuntimeFaceTransitionSeconds||Ch.Elapsed>=OKRuntimeFaceTransitionSeconds){if(Ch.Elapsed<OKRuntimeFaceTransitionSeconds)++CompletedChannels;Ch.Timing=false;Ch.Elapsed=OKRuntimeFaceTransitionSeconds;Ch.Legacy=0.f;Ch.From=Ch.To;}
  else Ch.Elapsed=Next;}
 Error.Reset();return true;
}
bool OKEvaluateRuntimeFaceTransition(const FKKRuntimeMouthStream& Stream,float Opening,float Gain,float Blink,const FKKRuntimeFaceTransition& Transition,TArray<FVector3f>& Out,FString& Error){
 if(!FMath::IsFinite(Opening)||!FMath::IsFinite(Gain)||Opening<0.f||Opening>1.f||Gain<0.f||Gain>1.f||!FMath::IsFinite(Blink)||Blink<0.f||Blink>1.f){Error=TEXT("Runtime mouth update invalid");return false;}
 if(!RuntimeFaceRangesOk(Stream,Error))return false;
 if(!Transition.Active()){FKKRuntimeFaceState Face;Face.MouthPattern=Transition.Channel[0].Pattern;Face.EyePattern=Transition.Channel[1].Pattern;Face.BrowPattern=Transition.Channel[2].Pattern;Face.MouthOpening=Transition.Channel[0].Opening;Face.EyeOpening=Transition.Channel[1].Opening;Face.BrowOpening=Transition.Channel[2].Opening;return OKEvaluateRuntimeFacePositions(Stream,Opening,Gain,Blink,&Face,Out,Error);}
 return EvaluateFaceVertices(Stream,Out,Error,[&](const FKKRuntimeMouthVertex& V){TArray<FFaceAcc,TInlineAllocator<16>> Channels;float LegacyMouth=0.f,LegacyEye=0.f;const int32 Cat=FindFaceCatalog(Transition,V.FaceCatalog.Get());
  const int32 Order[]={2,1,0};for(const int32 Control:Order){const auto& Ch=Transition.Channel[Control];if(!Ch.Timing)AccumulateAuthoredFace(Channels,V,Control,Ch.Pattern,Ch.Opening,Opening,Gain,Blink);
   else{const float U=FMath::Clamp(Ch.Elapsed/OKRuntimeFaceTransitionSeconds,0.f,1.f);if(Control==0)LegacyMouth=Ch.Legacy*(1.f-U);else if(Control==1)LegacyEye=Ch.Legacy*(1.f-U);
    AccumulateBlendedFace(Channels,V,Transition,Control,Cat,U,Control==0?Opening:Control==1?Blink:0.f,Control==0?Gain:1.f);}}
  FVector3f Delta=FVector3f::ZeroVector;for(const auto& Channel:Channels)Delta+=Channel.Delta*Channel.Weight;
  if(LegacyMouth!=0.f)Delta+=FMath::Lerp(V.Closed,V.Open,Opening)*Gain*LegacyMouth;if(LegacyEye!=0.f)Delta+=V.Blink*Blink*LegacyEye;
  return V.Base+Delta;});
}
bool OKRequestRuntimeFace(const FKKRuntimeMouthStream& Stream,FOKRuntimeFacePlayback& Playback,const FKKRuntimeFaceState& Request,bool AllowBlink,FString& Error){
 FKKRuntimeFaceTransition Next;bool Changed=false;int32 Started=0;
 if(!OKApplyRuntimeFaceTransition(Stream,Playback.Transition,Request,!Playback.Enabled,Next,Changed,Started,Error))return false;
 if(!Changed&&Playback.Enabled&&Playback.Blink==AllowBlink){Error.Reset();return true;}
 Playback.Transition=MoveTemp(Next);Playback.Face=Request;Playback.Enabled=true;Playback.Blink=AllowBlink;Playback.Dirty=true;++Playback.Requests;Playback.Started+=uint64(FMath::Max(0,Started));Error.Reset();return true;
}
bool OKResetRuntimeFace(FOKRuntimeFacePlayback& Playback,FString& Error){
 if(!Playback.Enabled){Error.Reset();return true;}const bool Timing=Playback.Transition.Active();
 Playback.Face={};Playback.Enabled=false;Playback.Blink=true;Playback.Transition={};Playback.Dirty=true;++Playback.Restores;if(Timing)++Playback.Cancelled;Error.Reset();return true;
}
bool OKPlanRuntimeFaceUpload(FOKRuntimeFacePlayback& Playback,float Dt,int64 Bucket,int64 LastBucket,bool MouthMoved,bool BlinkMoved,bool GainChanged,FOKRuntimeFaceUploadPlan& Plan,FString& Error){
 if(!FMath::IsFinite(Dt)||Dt<0.f){Error=TEXT("Runtime face transition dt invalid");return false;}
 if(!Playback.Enabled||!Playback.Transition.Active()){FOKRuntimeFaceUploadPlan Next;const bool Endpoint=Playback.Enabled&&Playback.Transition.EndpointPending;const bool Gate=Playback.Dirty||Bucket!=LastBucket||GainChanged||Endpoint,Need=Playback.Dirty||MouthMoved||BlinkMoved||GainChanged||Endpoint;Next.Upload=Gate&&Need;Plan=Next;Error.Reset();return true;}
 FKKRuntimeFaceTransition Next=Playback.Transition;int32 Finished=0;if(!OKAdvanceRuntimeFaceTransition(Next,Dt,Finished,Error))return false;if(Finished>0)Next.EndpointPending=true;
 FOKRuntimeFaceUploadPlan Planned;const bool Timing=Next.Active(),Endpoint=Next.EndpointPending,Step=Timing&&Bucket!=LastBucket;
 const bool Gate=Playback.Dirty||Bucket!=LastBucket||GainChanged||Endpoint||Step,Need=Playback.Dirty||MouthMoved||BlinkMoved||GainChanged||Endpoint||Step;
 Planned.Upload=Gate&&Need;if(Planned.Upload&&Timing)for(const auto& Ch:Next.Channel)if(Ch.Timing&&Ch.Elapsed>0.f)Planned.Intermediate=true;Planned.Completed=Finished;
 Playback.Transition=MoveTemp(Next);Playback.Completed+=uint64(FMath::Max(0,Finished));Plan=Planned;Error.Reset();return true;
}
void OKNoteRuntimeFaceUpload(FOKRuntimeFacePlayback& Playback,const FOKRuntimeFaceUploadPlan& Plan){
 if(!Plan.Upload)return;if(Playback.Dirty){++Playback.Updates;Playback.Dirty=false;}if(Plan.Intermediate)++Playback.Intermediate;Playback.Transition.EndpointPending=false;
}

TFuture<FKKRuntimeRenderMeshData> OKPrepareRuntimeRenderMeshAsync(TArray<FKKRuntimeMeshPart> Meshes,TArray<FKKRuntimePoseBone> Bones,uint32 Limit,uint64 Budget){
 return Async(EAsyncExecution::ThreadPool,[Meshes=MoveTemp(Meshes),Bones=MoveTemp(Bones),Limit,Budget]()mutable{
  const double Start=FPlatformTime::Seconds();FKKRuntimeRenderMeshData O;O.WorkerThread=!IsInGameThread();
  auto Fail=[&](const TCHAR* Why){O.Error=Why;O.Vertices.Reset();O.Weights.Reset();O.Indices.Reset();O.Sections.Reset();O.PrepareMs=(FPlatformTime::Seconds()-Start)*1000;return MoveTemp(O);};
  if(Meshes.IsEmpty()||Bones.IsEmpty()||Bones.Num()>65536||Limit<1||Limit>65536)return Fail(TEXT("Invalid runtime render mesh request"));
  TSet<FName> Names;for(int32 B=0;B<Bones.Num();++B){const auto& Bone=Bones[B];if(Bone.Name.IsNone()||Names.Contains(Bone.Name)||Bone.Parent<INDEX_NONE||Bone.Parent>=B||(B&&Bone.Parent==INDEX_NONE)||Bone.Reference.ContainsNaN())return Fail(TEXT("Invalid runtime render mesh rig"));Names.Add(Bone.Name);}
  uint64 Indices=0,FaceBytes=0;TSet<const FKKRuntimeFaceCatalog*> Catalogs;for(const auto& P:Meshes){if(P.Indices.IsEmpty()||P.Indices.Num()%3||P.Vertices.IsEmpty())return Fail(TEXT("Invalid runtime render mesh topology"));Indices+=P.Indices.Num();for(uint32 Index:P.Indices){if(Index>=uint32(P.Vertices.Num()))return Fail(TEXT("Runtime render triangle vertex out of range"));const auto& V=P.Vertices[Index];FaceBytes+=uint64(V.Face.GetAllocatedSize());if(V.FaceCatalog&&!Catalogs.Contains(V.FaceCatalog.Get())){Catalogs.Add(V.FaceCatalog.Get());for(const auto& Control:V.FaceCatalog->Control)FaceBytes+=uint64(Control.Close.GetAllocatedSize()+Control.Open.GetAllocatedSize()+Control.Baseline.GetAllocatedSize());}}}
  if(Indices>MAX_int32)return Fail(TEXT("Runtime render mesh index capacity exceeded"));
  // Worst case every triangle has three private vertices. Actual array capacity
  // and allocator quantization are admitted before background packing starts.
  const uint64 Bytes=uint64(DefaultCalculateSlackReserve(int32(Indices),sizeof(FKKRuntimeMeshVertex),true,alignof(FKKRuntimeMeshVertex)))*sizeof(FKKRuntimeMeshVertex)
   +uint64(DefaultCalculateSlackReserve(int32(Indices),sizeof(FSkinWeightInfo),true,alignof(FSkinWeightInfo)))*sizeof(FSkinWeightInfo)
   +uint64(DefaultCalculateSlackReserve(int32(Indices),sizeof(uint32),true,alignof(uint32)))*sizeof(uint32);
  if(!Budget||Bytes>Budget||FaceBytes>Budget-Bytes)return Fail(TEXT("Runtime packed mesh arrays exceed preallocation budget"));
  O.Vertices.Reserve(Indices);O.Weights.Reserve(Indices);O.Indices.Reserve(Indices);O.Materials=Meshes.Num();O.Bones=MoveTemp(Bones);
  for(int32 Part=0;Part<Meshes.Num();++Part){const auto& P=Meshes[Part];TMap<uint32,uint32> Vertices;TMap<uint16,uint16> BoneMap;FKKRuntimeRenderSection S;
   auto Begin=[&](){Vertices.Reset();BoneMap.Reset();S={};S.Material=Part;S.BaseVertex=O.Vertices.Num();S.BaseIndex=O.Indices.Num();};
   auto End=[&](){if(S.Triangles){S.NumVertices=O.Vertices.Num()-S.BaseVertex;O.Sections.Add(MoveTemp(S));}};Begin();
   for(int32 I=0;I<P.Indices.Num();I+=3){TSet<uint16> Needed;
    for(int32 C=0;C<3;++C){const uint32 Index=P.Indices[I+C];if(Index>=uint32(P.Vertices.Num()))return Fail(TEXT("Runtime render triangle vertex out of range"));const auto& V=P.Vertices[Index];if(!V.Influences||V.Influences>12||V.Influences>MAX_TOTAL_INFLUENCES||V.Position.ContainsNaN()||V.Normal.ContainsNaN())return Fail(TEXT("Runtime render vertex stream invalid"));uint32 Total=0;
     for(int32 K=0;K<V.Influences;++K){if(V.Skin[K].Bone>=O.Bones.Num())return Fail(TEXT("Runtime render vertex bone out of range"));Total+=V.Skin[K].Weight;if(V.Skin[K].Weight)Needed.Add(V.Skin[K].Bone);}if(Total!=65535)return Fail(TEXT("Runtime render vertex weights not normalized"));
    }if(uint32(Needed.Num())>Limit)return Fail(TEXT("Runtime triangle alone exceeds section bone limit"));
    uint32 Added=0;for(uint16 B:Needed)Added+=!BoneMap.Contains(B);if(uint32(BoneMap.Num())+Added>Limit){End();Begin();}
    for(int32 C=0;C<3;++C){const uint32 Source=P.Indices[I+C];if(const auto* Existing=Vertices.Find(Source)){O.Indices.Add(*Existing);continue;}
     const auto& V=P.Vertices[Source];FSkinWeightInfo Weight{};
     for(int32 K=0;K<V.Influences;++K){const auto W=V.Skin[K];if(!W.Weight)continue;auto* Local=BoneMap.Find(W.Bone);if(!Local){const uint16 L=S.BoneMap.Num();S.BoneMap.Add(W.Bone);BoneMap.Add(W.Bone,L);Local=BoneMap.Find(W.Bone);}Weight.InfluenceBones[K]=*Local;Weight.InfluenceWeights[K]=W.Weight;}
     const uint32 Index=O.Vertices.Num();Vertices.Add(Source,Index);O.Vertices.Add(V);O.Weights.Add(Weight);O.Indices.Add(Index);
    }++S.Triangles;
   }End();
  }
  O.Vertices.Shrink();O.Weights.Shrink();O.Indices.Shrink();O.PrepareMs=(FPlatformTime::Seconds()-Start)*1000;return O;
 });
}
USkeletalMesh* OKCreateRuntimeRenderMesh(const FKKRuntimeRenderMeshData& D,TConstArrayView<UMaterialInterface*> Materials,FString& Error,bool CPU){
 check(IsInGameThread());if(!D.Error.IsEmpty()||D.Bones.IsEmpty()||D.Vertices.IsEmpty()||D.Weights.Num()!=D.Vertices.Num()||D.Sections.IsEmpty()||(!Materials.IsEmpty()&&Materials.Num()!=D.Materials)){Error=TEXT("Invalid prepared runtime render mesh");return nullptr;}
 const double BuildBegin=FPlatformTime::Seconds();
 auto* Mesh=NewObject<USkeletalMesh>(GetTransientPackage(),NAME_None,RF_Transient);TStrongObjectPtr<USkeletalMesh> Hold(Mesh);
 auto* Skeleton=NewObject<USkeleton>(Mesh,NAME_None,RF_Transient);FReferenceSkeleton Ref;{FReferenceSkeletonModifier Modifier(Ref,Skeleton);
 for(const auto& B:D.Bones)Modifier.Add(FMeshBoneInfo(B.Name,B.Name.ToString(),B.Parent),B.Reference);}
 Mesh->SetRefSkeleton(Ref);Mesh->SetSkeleton(Skeleton);if(!Skeleton->MergeAllBonesToBoneTree(Mesh,false)){Error=TEXT("Runtime skeleton merge rejected");return nullptr;}Mesh->CalculateInvRefMatrices();Mesh->AddLODInfo();Mesh->SetHasVertexColors(true);
 for(int32 M=0;M<D.Materials;++M)Mesh->GetMaterials().Add(FSkeletalMaterial(Materials.IsEmpty()?UMaterial::GetDefaultMaterial(MD_Surface):Materials[M]));
 Mesh->AllocateResourceForRendering();auto* R=Mesh->GetResourceForRendering();auto* L=new FSkeletalMeshLODRenderData();R->LODRenderData.Add(L);R->NumInlinedLODs=R->NumNonOptionalLODs=1;R->CurrentFirstLODIdx=R->PendingFirstLODIdx=0;
 const double RigEnd=FPlatformTime::Seconds();
 TArray<FVector3f> Positions;TArray<FColor> Colors;Positions.Reserve(D.Vertices.Num());Colors.Reserve(D.Vertices.Num());FBox Bounds(ForceInit);
 for(const auto& V:D.Vertices){Positions.Add(V.Position);Colors.Add(V.Color);Bounds+=FVector(V.InitialPosition);Bounds+=FVector(V.Position);}
 L->StaticVertexBuffers.PositionVertexBuffer.Init(Positions,CPU);L->StaticVertexBuffers.ColorVertexBuffer.InitFromColorArray(Colors);L->StaticVertexBuffers.StaticMeshVertexBuffer.SetUseFullPrecisionUVs(true);L->StaticVertexBuffers.StaticMeshVertexBuffer.Init(D.Vertices.Num(),4,CPU);
 for(int32 I=0;I<D.Vertices.Num();++I){const auto& V=D.Vertices[I];FVector3f N=V.Normal.GetSafeNormal(),T=V.Tangent-N*FVector3f::DotProduct(N,V.Tangent);if(T.IsNearlyZero())T=FVector3f::CrossProduct(N,FMath::Abs(N.Z)<.99f?FVector3f(0,0,1):FVector3f(1,0,0));T.Normalize();L->StaticVertexBuffers.StaticMeshVertexBuffer.SetVertexTangents(I,T,FVector3f::CrossProduct(N,T)*V.TangentSign,N);for(int32 U=0;U<4;++U)L->StaticVertexBuffers.StaticMeshVertexBuffer.SetVertexUV(I,U,V.UV[U]);}
 L->SkinWeightVertexBuffer.SetMaxBoneInfluences(12);if(L->SkinWeightVertexBuffer.GetMaxBoneInfluences()!=12){Error=TEXT("Runtime exact skin requires cooked unlimited bone influence support; truncation rejected");return nullptr;}L->SkinWeightVertexBuffer.SetUse16BitBoneIndex(true);L->SkinWeightVertexBuffer.SetUse16BitBoneWeight(true);L->SkinWeightVertexBuffer.SetNeedsCPUAccess(CPU);L->SkinWeightVertexBuffer=D.Weights;
 L->MultiSizeIndexContainer.CreateIndexBuffer(sizeof(uint32));L->MultiSizeIndexContainer.CopyIndexBuffer(D.Indices);
 const double BuffersEnd=FPlatformTime::Seconds();
 TSet<uint16> Active;for(const auto& S:D.Sections){FSkelMeshRenderSection Section;Section.MaterialIndex=S.Material;Section.BaseIndex=S.BaseIndex;Section.NumTriangles=S.Triangles;Section.BaseVertexIndex=S.BaseVertex;Section.NumVertices=S.NumVertices;Section.MaxBoneInfluences=12;Section.bCastShadow=true;Section.BoneMap=S.BoneMap;L->RenderSections.Add(MoveTemp(Section));for(uint16 B:S.BoneMap)Active.Add(B);}
 L->ActiveBoneIndices=Active.Array();L->ActiveBoneIndices.Sort();for(int32 B=0;B<D.Bones.Num();++B)L->RequiredBones.Add(B);
 Mesh->SetImportedBounds(FBoxSphereBounds(Bounds));Mesh->InitResources();UE_LOG(LogTemp,Display,TEXT("Runtime mesh GT build: bones=%d vertices=%d rig_ms=%.3f buffers_ms=%.3f finalize_ms=%.3f"),D.Bones.Num(),D.Vertices.Num(),(RigEnd-BuildBegin)*1000,(BuffersEnd-RigEnd)*1000,(FPlatformTime::Seconds()-BuffersEnd)*1000);Error.Reset();return Mesh;
}
