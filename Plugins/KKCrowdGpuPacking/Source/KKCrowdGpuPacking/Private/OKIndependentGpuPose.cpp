#include "OKIndependentGpuPose.h"
#include "OKResidentGpuPose.h"
#include "GlobalShader.h"
#include "ShaderParameterStruct.h"
#include "RenderGraphUtils.h"
#include "RHIGPUReadback.h"
#include "RenderingThread.h"
#include "Misc/FileHelper.h"
#include "HAL/PlatformProcess.h"

class FOKIndependentPoseCS:public FGlobalShader {
 DECLARE_GLOBAL_SHADER(FOKIndependentPoseCS);
 SHADER_USE_PARAMETER_STRUCT(FOKIndependentPoseCS,FGlobalShader);
 BEGIN_SHADER_PARAMETER_STRUCT(FParameters,)
  SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>,Keys)
  SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint4>,Tracks)
  SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint4>,Actors)
  SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>,Times)
  SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint4>,Tasks)
  SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>,InverseBindRows)
  SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float4>,OutputRows)
  SHADER_PARAMETER(uint32,TaskCount)
 END_SHADER_PARAMETER_STRUCT()
 // Dynamic private hierarchy traversal requires the DXC/SM6 path. FXC/SM5
 // forces unrolling of the dynamically indexed ancestor stack and fails Cook.
 static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& P){return IsFeatureLevelSupported(P.Platform,ERHIFeatureLevel::SM6);}
};
IMPLEMENT_GLOBAL_SHADER(FOKIndependentPoseCS,"/Plugin/KKCrowdGpuPacking/IndependentPose.usf","MainCS",SF_Compute);
bool OKIndependentGpuPoseAvailable(){
 check(IsInRenderingThread());
 const auto* Map=GetGlobalShaderMap(GMaxRHIFeatureLevel);
 return Map&&Map->HasShader(&FOKIndependentPoseCS::GetStaticType(),0);
}

bool FOKGpuPoseBatch::Validate(FString& Error) const {
 auto Fail=[&](const TCHAR* Why){Error=Why;return false;};
 if(Keys.IsEmpty()||Keys.Num()%3||Actors.IsEmpty()||Actors.Num()!=Times.Num()||Tasks.IsEmpty())return Fail(TEXT("Invalid array sizes"));
 for(const FVector4f& V:Keys)for(int C=0;C<4;++C)if(!FMath::IsFinite(V[C]))return Fail(TEXT("Nonfinite key"));
 for(const FVector4f& V:InverseBindRows)for(int C=0;C<4;++C)if(!FMath::IsFinite(V[C]))return Fail(TEXT("Nonfinite binding"));
 for(int A=0;A<Actors.Num();++A){const auto& Actor=Actors[A];const auto& T=Times[A];
  if(Actor.W>1)return Fail(TEXT("Unsupported hierarchy mode"));
  if(!Actor.Z||Actor.Z>65536||uint64(Actor.X)+Actor.Z>uint64(Tracks.Num())||uint64(Actor.Y)+Actor.Z>uint64(Tracks.Num()))return Fail(TEXT("Invalid actor track range"));
  for(int C=0;C<4;++C)if(!FMath::IsFinite(T[C]))return Fail(TEXT("Nonfinite time"));
  if(T.Z<0||T.Z>1)return Fail(TEXT("Invalid blend"));
  for(uint32 B=0;B<Actor.Z;++B){for(uint32 Base:{Actor.X,Actor.Y}){const auto& Track=Tracks[Base+B];
   if(!OKGpuTrackModeValid(Track))return Fail(TEXT("Unsupported rotation interpolation"));
   if(!Track.Y||OKGpuTrackEndRow(Track)>uint64(Keys.Num()))return Fail(TEXT("Invalid key range"));
   if(Track.Z!=Tracks[Actor.X+B].Z)return Fail(TEXT("Clip hierarchy mismatch"));
   if(Actor.W==1){uint32 Depth=1,Parent=Track.Z;while(Parent!=MAX_uint32&&Parent<B&&Depth<=256){++Depth;Parent=Tracks[Base+Parent].Z;}if(Depth>256)return Fail(TEXT("Native TRS hierarchy exceeds depth limit"));}
   if(Track.Z!=MAX_uint32&&Track.Z>=B)return Fail(TEXT("Parent must precede child"));
   for(uint32 K=0;K<Track.Y;++K){const int I=OKGpuTrackPositionRow(Track,K);const auto& Q=Keys[OKGpuTrackRotationRow(Track,K)];if(!OKGpuTrackVectorKeys(Track)&&FMath::Abs(Q.SizeSquared()-1.f)>0.001f)return Fail(TEXT("Quaternion not normalized"));
    if(K&&Keys[I].W<=Keys[OKGpuTrackPositionRow(Track,K-1)].W)return Fail(TEXT("Key times must increase"));}
  }}
 }
 for(const auto& Task:Tasks)if(Task.Y>=uint32(Actors.Num())||Task.X>=Actors[Task.Y].Z||uint64(Task.Z)+3>uint64(InverseBindRows.Num()))return Fail(TEXT("Invalid output task"));
 return true;
}
FRDGBufferRef OKAddIndependentGpuPosePass(FRDGBuilder& G,const FOKGpuPoseBuffers& B){
 if(!OKIndependentGpuPoseAvailable())return nullptr;
 if(!B.Keys||!B.Tracks||!B.Actors||!B.Times||!B.Tasks||!B.InverseBindRows||!B.TaskCount||B.TaskCount>MAX_int32/3)return nullptr;
 auto* P=G.AllocParameters<FOKIndependentPoseCS::FParameters>();
 P->Keys=G.CreateSRV(B.Keys);P->Tracks=G.CreateSRV(B.Tracks);P->Actors=G.CreateSRV(B.Actors);
 P->Times=G.CreateSRV(B.Times);P->Tasks=G.CreateSRV(B.Tasks);P->InverseBindRows=G.CreateSRV(B.InverseBindRows);
 auto* Out=G.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(FVector4f),B.TaskCount*3),TEXT("Pose.Output"));
 P->OutputRows=G.CreateUAV(Out);P->TaskCount=B.TaskCount;
 TShaderMapRef<FOKIndependentPoseCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
 FComputeShaderUtils::AddPass(G,RDG_EVENT_NAME("Crowd.IndependentPose"),Shader,P,FComputeShaderUtils::GetGroupCount(B.TaskCount,64));return Out;
}
FRDGBufferRef OKAddIndependentGpuPosePass(FRDGBuilder& G,const FOKGpuPoseBatch& B){
 FString Error;if(!B.Validate(Error)){UE_LOG(LogTemp,Error,TEXT("GPU pose rejected: %s"),*Error);return nullptr;}
 FOKGpuPoseBuffers Buffers;
 Buffers.Keys=CreateStructuredBuffer(G,TEXT("Pose.Keys"),B.Keys);
 Buffers.Tracks=CreateStructuredBuffer(G,TEXT("Pose.Tracks"),B.Tracks);
 Buffers.Actors=CreateStructuredBuffer(G,TEXT("Pose.Actors"),B.Actors);
 Buffers.Times=CreateStructuredBuffer(G,TEXT("Pose.Times"),B.Times);
 Buffers.Tasks=CreateStructuredBuffer(G,TEXT("Pose.Tasks"),B.Tasks);
 Buffers.InverseBindRows=CreateStructuredBuffer(G,TEXT("Pose.Bind"),B.InverseBindRows);
 Buffers.TaskCount=B.Tasks.Num();return OKAddIndependentGpuPosePass(G,Buffers);
}
namespace {
FQuat4f Mix(FQuat4f A,FQuat4f B,float T){return FQuat4f::Slerp(A,B,T).GetNormalized();}
FTransform3f SampleCPU(const FOKGpuPoseBatch& B,uint32 Index,float Time){
 const auto& Track=B.Tracks[Index];uint32 Lo=0,Hi=Track.Y-1;
 while(Lo+1<Hi){uint32 Mid=(Lo+Hi)/2;if(B.Keys[OKGpuTrackPositionRow(Track,Mid)].W<=Time)Lo=Mid;else Hi=Mid;}
 uint32 A=OKGpuTrackPositionRow(Track,Lo),C=OKGpuTrackPositionRow(Track,Hi);float TA=B.Keys[A].W,TB=B.Keys[C].W,T=TB>TA?FMath::Clamp((Time-TA)/(TB-TA),0.f,1.f):0;
 const auto& QA=B.Keys[OKGpuTrackRotationRow(Track,Lo)];const auto& QB=B.Keys[OKGpuTrackRotationRow(Track,Hi)];
 if(OKGpuTrackColumn(Track)){return OKGpuColumnTransform<FVector3f,FQuat4f,FTransform3f>(FMath::Lerp(FVector3f(B.Keys[A]),FVector3f(B.Keys[C]),T),FMath::Lerp(OKGpuTrackColumnKey(B.Keys,Track,Lo),OKGpuTrackColumnKey(B.Keys,Track,Hi),T),(Track.W>>6)&3);}
 return FTransform3f(Mix(FQuat4f(QA.X,QA.Y,QA.Z,QA.W),FQuat4f(QB.X,QB.Y,QB.Z,QB.W),T),FMath::Lerp(FVector3f(B.Keys[A]),FVector3f(B.Keys[C]),T),FMath::Lerp(FVector3f(B.Keys[OKGpuTrackScaleRow(Track,Lo)]),FVector3f(B.Keys[OKGpuTrackScaleRow(Track,Hi)]),T));
}
TArray<FVector4f> CpuExpected(const FOKGpuPoseBatch& Batch){TArray<FVector4f> Expected; for(const auto& Task:Batch.Tasks){auto Actor=Batch.Actors[Task.Y];auto T=Batch.Times[Task.Y];auto LocalTransform=[&](uint32 B){auto A=SampleCPU(Batch,Actor.X+B,T.X),C=SampleCPU(Batch,Actor.Y+B,T.Y);return OKGpuBlendLocal(A,C,T.Z,Batch.Tracks[Actor.X+B]);};auto Local=[&](uint32 B){return LocalTransform(B).ToMatrixWithScale();};
  FMatrix44f M=Local(Task.X);uint32 Parent=Batch.Tracks[Actor.X+Task.X].Z;while(Parent!=MAX_uint32){M=M*Local(Parent);Parent=Batch.Tracks[Actor.X+Parent].Z;}
  if(Actor.W==1){TArray<uint32> Path;uint32 Node=Task.X;while(Node!=MAX_uint32){Path.Add(Node);Node=Batch.Tracks[Actor.X+Node].Z;}FTransform3f Comp=LocalTransform(Path.Pop());while(!Path.IsEmpty())Comp=LocalTransform(Path.Pop())*Comp;M=Comp.ToMatrixWithScale();}
  FMatrix44f Bind=FMatrix44f::Identity;for(int R=0;R<3;++R)for(int C=0;C<4;++C)Bind.M[C][R]=Batch.InverseBindRows[Task.Z+R][C];M=Bind*M;
  for(int R=0;R<3;++R)Expected.Add(FVector4f(M.M[0][R],M.M[1][R],M.M[2][R],M.M[3][R]));
 }
 return Expected;}
bool ResidentFixture(FOKGpuPoseBatch Batch,const FString& Output);
}
bool OKRunIndependentGpuPoseOracle(const FString& Output,bool Resident){
 // Synthetic correctness test, not cooked-card compatibility or FPS evidence.
 FOKGpuPoseBatch Batch;TArray<FVector4f> Expected;
 for(uint32 A=0;A<100;++A){uint32 Bones=3+A%17,Base=Batch.Tracks.Num();
  for(uint32 Clip=0;Clip<(Resident?20u:2u);++Clip)for(uint32 B=0;B<Bones;++B){uint32 First=Batch.Keys.Num()/3,Count=1+(A+B+Clip)%4;
   Batch.Tracks.Add(FUintVector4(First,Count,B?((A%2)?B-1:0):MAX_uint32,0));
   for(uint32 K=0;K<Count;++K){float Time=float(K)/FMath::Max(1u,Count-1);auto Q=FQuat4f(FVector3f(0,0,1),float(A+B+K+Clip)*0.071f);if(K%2)Q=Q*-1.f;
    Batch.Keys.Add(FVector4f(float(B)*.3f+Time*Clip,float(A)*.02f,float(K)*.07f,Time));Batch.Keys.Add(FVector4f(Q.X,Q.Y,Q.Z,Q.W));
    Batch.Keys.Add(FVector4f(B%4==0?-1.f:1.f,1.f+K*.02f,B%5==0?0.f:1.f,0));}
  }
  Batch.Actors.Add(FUintVector4(Base,Base+Bones,Bones,A%3==1?1:0));Batch.Times.Add(FVector4f(float(A%11)/10.f,float(A%7)/6.f,float(A%5)/4.f,0));
  for(uint32 B=0;B<Bones;++B){uint32 Bind=Batch.InverseBindRows.Num();Batch.InverseBindRows.Append({FVector4f(1,0,0,-.03f*B),FVector4f(0,1,0,.02f*B),FVector4f(0,0,1,0)});Batch.Tasks.Add(FUintVector4(B,A,Bind,0));}
 }
 int Rejected=0;for(int Case=0;Case<7;++Case){auto Invalid=Batch;switch(Case){
 case 0:Invalid.Tracks[0].Y=0;break;
 case 1:Invalid.Tracks[1].Z=1;break;
 case 2:Invalid.Keys[1]=FVector4f(0,0,0,0);break;
 case 3:Invalid.Tasks[0].Y=MAX_uint32;break;
 case 4:Invalid.Tasks[0].Z=MAX_uint32;break;
 case 5:Invalid.Times[0].Z=2;break;
 case 6:Invalid.Actors[0].Y=MAX_uint32;break;
 }FString Why;if(!Invalid.Validate(Why))++Rejected;}if(Rejected!=7){UE_LOG(LogTemp,Error,TEXT("GPU pose invalid input validation failed"));return false;}
 FString Error;if(!Batch.Validate(Error)){UE_LOG(LogTemp,Error,TEXT("GPU pose oracle: %s"),*Error);return false;}
 if(Resident)return ResidentFixture(MoveTemp(Batch),Output);
 Expected=CpuExpected(Batch);
 TArray<FVector4f> Actual;if(!OKReadbackIndependentGpuPose(Batch,Actual,Error)){UE_LOG(LogTemp,Error,TEXT("%s"),*Error);return false;}
 float Maximum=0;double Squared=0;bool Finite=true;
 for(int I=0;I<Expected.Num();++I)for(int C=0;C<4;++C){float V=Actual[I][C];Finite&=FMath::IsFinite(V);float E=FMath::Abs(V-Expected[I][C]);Maximum=FMath::Max(Maximum,E);Squared+=double(E)*E;}
 bool Passed=Finite&&Maximum<=0.0005f;FString Json=FString::Printf(TEXT("{\"passed\":%s,\"synthetic_only\":true,\"characters\":100,\"clips_per_character\":2,\"sharing\":false,\"invalid_inputs_rejected\":7,\"matrices\":%d,\"max_error\":%.9g,\"rms_error\":%.9g,\"finite\":%s,\"tolerance\":0.0005,\"fps_evidence\":false}"),Passed?TEXT("true"):TEXT("false"),Batch.Tasks.Num(),Maximum,FMath::Sqrt(Squared/(Expected.Num()*4)),Finite?TEXT("true"):TEXT("false"));
 UE_LOG(LogTemp,Display,TEXT("Independent GPU pose oracle: %s"),*Json);return FFileHelper::SaveStringToFile(Json,*Output)&&Passed;
}


bool OKReadbackIndependentGpuPose(const FOKGpuPoseBatch& Batch,TArray<FVector4f>& Actual,FString& Error){
 if(!Batch.Validate(Error))return false;
 auto Readback=MakeShared<FRHIGPUBufferReadback,ESPMode::ThreadSafe>(TEXT("IndependentPoseOracle"));uint32 Bytes=Batch.Tasks.Num()*3*sizeof(FVector4f);
 ENQUEUE_RENDER_COMMAND(IndependentPoseOracle)([Batch,Readback,Bytes](FRHICommandListImmediate& RHICmdList){FRDGBuilder G(RHICmdList);auto* Out=OKAddIndependentGpuPosePass(G,Batch);AddEnqueueCopyPass(G,&Readback.Get(),Out,Bytes);G.Execute();});
 FlushRenderingCommands();double Start=FPlatformTime::Seconds();bool Ready=false;
 while(FPlatformTime::Seconds()-Start<30){ENQUEUE_RENDER_COMMAND(PosePoll)([Readback,&Ready](FRHICommandListImmediate& R){Ready=Readback->IsReady();});FlushRenderingCommands();if(Ready)break;FPlatformProcess::Sleep(.01f);}
 if(!Ready){Error=TEXT("GPU pose readback timeout");return false;}
 Actual.SetNumUninitialized(Batch.Tasks.Num()*3);
 ENQUEUE_RENDER_COMMAND(PoseRead)([&](FRHICommandListImmediate& R){const void* Data=Readback->Lock(Bytes);FMemory::Memcpy(Actual.GetData(),Data,Bytes);Readback->Unlock();});FlushRenderingCommands();return true;
}

namespace {
bool ResidentFixture(FOKGpuPoseBatch Batch,const FString& Output){
 FOKResidentGpuPose Pose;TArray<TArray<uint32>> Clips;for(const auto& Actor:Batch.Actors){TArray<uint32> Offsets;for(uint32 C=0;C<20;++C)Offsets.Add(Actor.X+C*Actor.Z);Clips.Add(MoveTemp(Offsets));}
 TArray<FOKGpuPosePlayback> Playback;Playback.SetNum(Batch.Actors.Num());TArray<FVector4f> LastExpected;FOKResidentPoseStats Stats;FString Error;float Maximum=0;bool Passed=true;uint64 FirstImmutable=0;bool ReuseUploadsStable=false;
 for(int Frame=0;Frame<5;++Frame){int PhaseFrame=Frame==2?1:Frame;
  for(int A=0;A<Playback.Num();++A){auto& P=Playback[A];P.ClipA=(A*3+PhaseFrame*7)%20;P.ClipB=(A*7+PhaseFrame*3+1)%20;P.Times=FVector4f(FMath::Frac(A*.137f+PhaseFrame*.11f),FMath::Frac(A*.071f+PhaseFrame*.19f),float(A%5)/4,0);Batch.Actors[A].X=Clips[A][P.ClipA];Batch.Actors[A].Y=Clips[A][P.ClipB];Batch.Times[A]=P.Times;}
  auto Expected=CpuExpected(Batch);TArray<FVector4f> Current,Previous;auto Before=Stats;bool Reset=Frame==3;
  if(!OKReadbackResidentGpuPose(Pose,Frame==0?&Batch:nullptr,Clips,Playback,Frame,Reset,Current,Previous,Stats,Error)){Passed=false;break;}
  if(Frame==0)FirstImmutable=Stats.ImmutableUploadBytes;if(Stats.ImmutableUploadBytes!=FirstImmutable||Stats.Initializations!=1)Passed=false;
  if(Frame==2)ReuseUploadsStable=Stats.ParameterUploadBytes==Before.ParameterUploadBytes;
  const auto& PrevExpected=Frame==0||Frame==2||Reset?Expected:LastExpected;
  for(int I=0;I<Expected.Num();++I)for(int C=0;C<4;++C){float A=Current[I][C],B=Previous[I][C];Passed&=FMath::IsFinite(A)&&FMath::IsFinite(B);Maximum=FMath::Max(Maximum,FMath::Max(FMath::Abs(A-Expected[I][C]),FMath::Abs(B-PrevExpected[I][C])));}
  LastExpected=MoveTemp(Expected);
 }
 int Rejected=0;
 ENQUEUE_RENDER_COMMAND(ResidentBadInputs)([&](FRHICommandListImmediate& R){FRDGBuilder G(R);FOKResidentPoseFrame Out;auto Bad=Playback;auto S=Pose.GetStats();FString Why;
  if(!Pose.Evaluate(G,Playback,5,S.Generation+1,false,Out,Why))++Rejected;
  if(!Pose.Evaluate(G,Playback,4,S.Generation,false,Out,Why))++Rejected;
  Bad[0].ClipA=20;if(!Pose.Evaluate(G,Bad,5,S.Generation,false,Out,Why))++Rejected;
  Bad=Playback;Bad[0].Times.Z=2;if(!Pose.Evaluate(G,Bad,5,S.Generation,false,Out,Why))++Rejected;
  auto Wrong=Clips;Wrong[1][0]=Wrong[0][0];if(!Pose.Initialize(G,Batch,Wrong,Why))++Rejected;
  Pose.Release();if(!Pose.Evaluate(G,Playback,5,S.Generation,false,Out,Why))++Rejected;G.Execute();
 });FlushRenderingCommands();
 TArray<FVector4f> Current,Previous;if(Passed&&!OKReadbackResidentGpuPose(Pose,&Batch,Clips,Playback,0,false,Current,Previous,Stats,Error))Passed=false;
 if(Passed)for(int I=0;I<Current.Num();++I)for(int C=0;C<4;++C)if(Current[I][C]!=Previous[I][C])Passed=false;
 ENQUEUE_RENDER_COMMAND(ResidentRelease)([&](FRHICommandListImmediate& R){Pose.Release();});FlushRenderingCommands();
 Passed&=Maximum<=.0005f&&Rejected==6&&ReuseUploadsStable&&Stats.Initializations==2&&Stats.Evaluations==5&&Stats.ReusedFrames==1&&Stats.ImmutableUploadBytes==FirstImmutable*2;
 FString Json=FString::Printf(TEXT("{\"passed\":%s,\"synthetic_only\":true,\"resident\":true,\"characters\":100,\"clips_per_character\":20,\"sharing\":false,\"frames\":6,\"max_error\":%.9g,\"tolerance\":0.0005,\"initializations\":%llu,\"evaluations\":%llu,\"reused_frames\":%llu,\"immutable_bytes_per_generation\":%llu,\"parameter_upload_bytes\":%llu,\"history_passed\":%s,\"invalid_playback_rejected\":%d,\"reuse_uploads_stable\":%s,\"fps_evidence\":false}"),Passed?TEXT("true"):TEXT("false"),Maximum,Stats.Initializations,Stats.Evaluations,Stats.ReusedFrames,FirstImmutable,Stats.ParameterUploadBytes,Passed?TEXT("true"):TEXT("false"),Rejected,ReuseUploadsStable?TEXT("true"):TEXT("false"));
 UE_LOG(LogTemp,Display,TEXT("Resident GPU pose oracle %s error=%s"),*Json,*Error);return FFileHelper::SaveStringToFile(Json,*Output)&&Passed;
}
}
