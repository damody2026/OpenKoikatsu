#include "OKGpuSkinPalette.h"
#include "GlobalShader.h"
#include "ShaderParameterStruct.h"
#include "RenderGraphUtils.h"
#include "RenderGraphResources.h"
#include "RenderingThread.h"
#include "RHIGPUReadback.h"
#include "HAL/PlatformProcess.h"

class FOKSkinPaletteCS:public FGlobalShader {
 DECLARE_GLOBAL_SHADER(FOKSkinPaletteCS);
 SHADER_USE_PARAMETER_STRUCT(FOKSkinPaletteCS,FGlobalShader);
 BEGIN_SHADER_PARAMETER_STRUCT(FParameters,)
  SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>,CurrentPose)
  SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>,PreviousPose)
  SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>,InverseBindRows)
  SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint4>,Items)
  SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint4>,OutputTransforms)
  SHADER_PARAMETER(uint32,ItemCount)
  SHADER_PARAMETER(uint32,CurrentRowOffset)
  SHADER_PARAMETER(uint32,PreviousRowOffset)
 END_SHADER_PARAMETER_STRUCT()
 static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& P){return IsFeatureLevelSupported(P.Platform,ERHIFeatureLevel::SM5);}
};
IMPLEMENT_GLOBAL_SHADER(FOKSkinPaletteCS,"/Plugin/KKCrowdGpuPacking/SkinPalette.usf","MainCS",SF_Compute);
class FOKSkinPaletteBatchCS:public FGlobalShader {
 DECLARE_GLOBAL_SHADER(FOKSkinPaletteBatchCS);
 SHADER_USE_PARAMETER_STRUCT(FOKSkinPaletteBatchCS,FGlobalShader);
 BEGIN_SHADER_PARAMETER_STRUCT(FParameters,)
  SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>,CurrentPose)
  SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>,PreviousPose)
  SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>,InverseBindRows)
  SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint4>,Items)
  SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint4>,Jobs)
  SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint4>,WorkGroups)
  SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint4>,OutputTransforms)
 END_SHADER_PARAMETER_STRUCT()
 static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& P){return IsFeatureLevelSupported(P.Platform,ERHIFeatureLevel::SM5);}
};
IMPLEMENT_GLOBAL_SHADER(FOKSkinPaletteBatchCS,"/Plugin/KKCrowdGpuPacking/SkinPalette.usf","BatchCS",SF_Compute);
bool FOKGpuSkinPaletteData::Validate(FString& Error) const {
 auto Fail=[&](const TCHAR* E){Error=E;return false;};
 if(!PoseCount||Items.IsEmpty()||Items.Num()>65535*64||InverseBindRows.IsEmpty()||InverseBindRows.Num()%3)return Fail(TEXT("Invalid palette dimensions/dispatch capacity"));
 for(const auto& V:InverseBindRows)for(int C=0;C<4;++C)if(!FMath::IsFinite(V[C]))return Fail(TEXT("Nonfinite palette inverse binding"));
 TBitArray<> Seen(false,Items.Num());
 for(const auto& I:Items){if(I.X>=PoseCount||I.Y%3||uint64(I.Y)+3>uint64(InverseBindRows.Num())||I.Z>=uint32(Items.Num())||I.W)return Fail(TEXT("Palette source/binding/destination invalid"));
  if(Seen[I.Z])return Fail(TEXT("Palette destination overlap"));Seen[I.Z]=true;}
 return true;
}
struct FOKGpuSkinPalette::FState {TRefCountPtr<FRDGPooledBuffer> Items,Bind;FOKGpuSkinPaletteData Data;uint32 PoseCount=0;FOKGpuSkinPaletteStats Stats;};
FOKGpuSkinPalette::FOKGpuSkinPalette():State(MakeUnique<FState>()){}
FOKGpuSkinPalette::~FOKGpuSkinPalette()=default;
FOKGpuSkinPaletteStats FOKGpuSkinPalette::GetStats() const{check(IsInRenderingThread());return State->Stats;}
void FOKGpuSkinPalette::Release(){check(IsInRenderingThread());auto Stats=State->Stats;++Stats.Generation;State=MakeUnique<FState>();State->Stats=Stats;}
bool FOKGpuSkinPalette::Initialize(FRDGBuilder& G,const FOKGpuSkinPaletteData& D,uint64 PoseGeneration,FString& Error){
 check(IsInRenderingThread());if(!PoseGeneration||!D.Validate(Error))return false;
 auto Next=MakeUnique<FState>();Next->Stats=State->Stats;++Next->Stats.Generation;++Next->Stats.Initializations;Next->Stats.PoseGeneration=PoseGeneration;Next->Stats.Bones=D.Items.Num();Next->PoseCount=D.PoseCount;
 Next->Items=G.ConvertToExternalBuffer(CreateStructuredBuffer(G,TEXT("SkinPalette.Items"),D.Items));Next->Bind=G.ConvertToExternalBuffer(CreateStructuredBuffer(G,TEXT("SkinPalette.Bind"),D.InverseBindRows));
 Next->Data=D;Next->Stats.ImmutableUploadBytes+=uint64(D.Items.Num())*sizeof(FUintVector4)+uint64(D.InverseBindRows.Num())*sizeof(FVector4f);State=MoveTemp(Next);return true;
}
struct FOKGpuSkinPaletteBatch::FState {
 struct FEntry {TRefCountPtr<FRDGPooledBuffer> Identity;uint32 First,Count;};
 TMap<FRDGPooledBuffer*,FEntry> Entries;
 TArray<FUintVector4> Items;TArray<FVector4f> BindRows;
 TRefCountPtr<FRDGPooledBuffer> ItemBuffer,BindBuffer;
 struct FJobIdentity {FRDGPooledBuffer* Items;uint32 Current,Previous;bool Reset;};
struct FLayout {
  TArray<FJobIdentity> LastJobs;
  TRefCountPtr<FRDGPooledBuffer> JobBuffer,GroupBuffer;
  uint32 OutputElements=0,GroupCount=0;
 };
 TArray<FLayout> Layouts;
 uint64 PoseGeneration=0;
 FOKGpuSkinPaletteBatchStats Stats;
};
FOKGpuSkinPaletteBatch::FOKGpuSkinPaletteBatch():State(MakeUnique<FState>()){}
FOKGpuSkinPaletteBatch::~FOKGpuSkinPaletteBatch()=default;
FOKGpuSkinPaletteBatchStats FOKGpuSkinPaletteBatch::GetStats() const {check(IsInRenderingThread());return State->Stats;}
bool FOKGpuSkinPaletteBatch::AddPass(FRDGBuilder& G,const FOKResidentPoseFrame& Frame,FRDGBufferRef Output,TConstArrayView<FOKGpuSkinPaletteJob> Input,FString& Error){
 check(IsInRenderingThread());
 auto Fail=[&](const TCHAR* E){Error=E;return false;};
 if(Input.IsEmpty()||!Frame.Current||!Frame.Previous||!Output||Output->Desc.BytesPerElement!=16||Output==Frame.Current||Output==Frame.Previous)return Fail(TEXT("Batch palette missing/aliased output or source"));
 // Resource identity, destination layout, history policy and output capacity
 // fully determine the immutable scatter. Output buffer addresses may change.
 int32 Cached=INDEX_NONE;
 if(State->PoseGeneration==Frame.Generation){for(int32 L=0;L<State->Layouts.Num();++L){const auto& Layout=State->Layouts[L];
  bool Same=Layout.OutputElements==Output->Desc.NumElements&&Layout.LastJobs.Num()==Input.Num();
  for(int32 I=0;Same&&I<Input.Num();++I){const auto& J=Input[I];const auto& Last=Layout.LastJobs[I];Same=J.Palette&&J.Palette->State->Items.GetReference()==Last.Items&&J.CurrentRowOffset==Last.Current&&J.PreviousRowOffset==Last.Previous&&J.ResetHistory==Last.Reset;}
  if(Same){Cached=L;break;}
 }}
 bool Reuse=Cached!=INDEX_NONE;
 struct FRange {uint64 First,End;};TArray<FRange> Ranges;
 uint64 GroupCount=0;
 // Validate the whole scatter, including cross-component writes, before any
 // catalog mutation or RDG submission. Hold resource identities in the catalog
 // so destroyed/reallocated palette pointers can never reuse stale bindings.
 for(const auto& Job:Input){
  if(!Job.Palette)return Fail(TEXT("Batch palette null job"));const auto& S=*Job.Palette->State;
  if(!S.Items||Frame.Generation!=S.Stats.PoseGeneration)return Fail(TEXT("Batch palette stale generation"));
  if(Frame.Current->Desc.BytesPerElement!=16||Frame.Previous->Desc.BytesPerElement!=16||Frame.Current->Desc.NumElements!=uint64(S.PoseCount)*3||Frame.Previous->Desc.NumElements!=uint64(S.PoseCount)*3)return Fail(TEXT("Batch palette pose layout differs"));
  // Unregistered resources cannot alias this already registered output. Query
  // identities without importing thousands of unused per-part buffers into RDG.
  if(Output==G.FindExternalBuffer(S.Items)||Output==G.FindExternalBuffer(S.Bind))return Fail(TEXT("Batch output aliases immutable palette"));
  const uint64 Rows=uint64(S.Stats.Bones)*3;
  for(uint32 Offset:{Job.CurrentRowOffset,Job.PreviousRowOffset}){if(uint64(Offset)+Rows>Output->Desc.NumElements)return Fail(TEXT("Batch palette output overflow"));if(!Reuse)Ranges.Add({Offset,uint64(Offset)+Rows});}
  GroupCount+=(uint64(S.Stats.Bones)+63)/64;
 }
 if(!GroupCount||GroupCount>65535)return Fail(TEXT("Batch palette exceeds dispatch capacity"));
 Ranges.Sort([](const FRange& A,const FRange& B){return A.First<B.First;});
 for(int32 K=1;K<Ranges.Num();++K)if(Ranges[K].First<Ranges[K-1].End)return Fail(TEXT("Batch palette overlapping output/history ranges"));
 if(State->PoseGeneration!=Frame.Generation){auto Stats=State->Stats;State=MakeUnique<FState>();State->Stats=Stats;State->PoseGeneration=Frame.Generation;}
 // Bound cached stale palettes after mesh replacement; rebuild from this
 // frame's live jobs. Normal visibility changes keep the catalog intact.
 if(State->Entries.Num()>Input.Num()*4+1024){auto Stats=State->Stats;State=MakeUnique<FState>();State->Stats=Stats;State->PoseGeneration=Frame.Generation;Reuse=false;}
 auto& S=*State;bool Changed=false;
 // UE swaps current/history destinations. Retain a bounded set of exact
 // layouts so this normal ping-pong does not invalidate the previous layout.
 if(!Reuse){if(S.Layouts.Num()==8)S.Layouts.RemoveAt(0);Cached=S.Layouts.AddDefaulted();}
 auto& Layout=S.Layouts[Cached];
 if(!Reuse){
 TArray<FUintVector4> Jobs,Groups;Jobs.Reserve(Input.Num());Groups.Reserve(GroupCount);
 Layout.LastJobs.Reset(Input.Num());
 for(const auto& Job:Input){
  Layout.LastJobs.Add({Job.Palette->State->Items.GetReference(),Job.CurrentRowOffset,Job.PreviousRowOffset,Job.ResetHistory});auto& P=*Job.Palette->State;auto* Entry=S.Entries.Find(P.Items.GetReference());
  if(!Entry){const uint32 First=S.Items.Num(),BindFirst=S.BindRows.Num();
   S.BindRows.Append(P.Data.InverseBindRows);
   for(auto Item:P.Data.Items){Item.Y+=BindFirst;S.Items.Add(Item);}
   Entry=&S.Entries.Add(P.Items.GetReference(),{P.Items,First,P.Stats.Bones});Changed=true;
  }
  const uint32 JobIndex=Jobs.Num();Jobs.Add(FUintVector4(Entry->First,Entry->Count,Job.CurrentRowOffset,Job.PreviousRowOffset));
  for(uint32 Base=0;Base<Entry->Count;Base+=64)Groups.Add(FUintVector4(JobIndex,Base,Job.ResetHistory?1:0,0));
 }
 if(Changed){S.ItemBuffer=G.ConvertToExternalBuffer(CreateStructuredBuffer(G,TEXT("SkinPalette.BatchItems"),S.Items));S.BindBuffer=G.ConvertToExternalBuffer(CreateStructuredBuffer(G,TEXT("SkinPalette.BatchBind"),S.BindRows));}
 Layout.JobBuffer=G.ConvertToExternalBuffer(CreateStructuredBuffer(G,TEXT("SkinPalette.BatchJobs"),Jobs));
 Layout.GroupBuffer=G.ConvertToExternalBuffer(CreateStructuredBuffer(G,TEXT("SkinPalette.BatchGroups"),Groups));
 Layout.OutputElements=Output->Desc.NumElements;Layout.GroupCount=Groups.Num();++S.Stats.LayoutBuilds;
 if(S.Stats.LayoutBuilds<=8)UE_LOG(LogTemp,Display,TEXT("Palette layout %llu: output=%u jobs=%d first=%u/%u reset=%d"),S.Stats.LayoutBuilds,Layout.OutputElements,Input.Num(),Input[0].CurrentRowOffset,Input[0].PreviousRowOffset,int32(Input[0].ResetHistory));
 }else ++S.Stats.LayoutReuses;
 auto* Params=G.AllocParameters<FOKSkinPaletteBatchCS::FParameters>();
 Params->CurrentPose=G.CreateSRV(Frame.Current);Params->PreviousPose=G.CreateSRV(Frame.Previous);
 Params->Items=G.CreateSRV(G.RegisterExternalBuffer(S.ItemBuffer));Params->InverseBindRows=G.CreateSRV(G.RegisterExternalBuffer(S.BindBuffer));
 Params->Jobs=G.CreateSRV(G.RegisterExternalBuffer(Layout.JobBuffer));Params->WorkGroups=G.CreateSRV(G.RegisterExternalBuffer(Layout.GroupBuffer));
 Params->OutputTransforms=G.CreateUAV(FRDGBufferUAVDesc(Output,PF_R32G32B32A32_UINT));
 TShaderMapRef<FOKSkinPaletteBatchCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
 FComputeShaderUtils::AddPass(G,RDG_EVENT_NAME("Crowd.GpuSkinPaletteBatch jobs=%d groups=%d",Input.Num(),Layout.GroupCount),Shader,Params,FIntVector(Layout.GroupCount,1,1));
 for(const auto& Job:Input)++Job.Palette->State->Stats.Passes;
 return true;
}
bool FOKGpuSkinPalette::AddPass(FRDGBuilder& G,const FOKResidentPoseFrame& Frame,uint64 Generation,FRDGBufferRef Output,uint32 C,uint32 P,FString& Error){
 check(IsInRenderingThread());const auto& S=*State;uint64 Rows=uint64(S.Stats.Bones)*3;
 auto Fail=[&](const TCHAR* E){Error=E;return false;};
 if(!S.Items||Generation!=S.Stats.Generation||Frame.Generation!=S.Stats.PoseGeneration||!Frame.Current||!Frame.Previous||!Output)return Fail(TEXT("Palette uninitialized/stale resource generation"));
 if(Frame.Current->Desc.BytesPerElement!=16||Frame.Previous->Desc.BytesPerElement!=16||Frame.Current->Desc.NumElements!=uint64(S.PoseCount)*3||Frame.Previous->Desc.NumElements!=uint64(S.PoseCount)*3)return Fail(TEXT("Palette pose buffer layout differs"));
 if(Output->Desc.BytesPerElement!=16||uint64(C)+Rows>Output->Desc.NumElements||uint64(P)+Rows>Output->Desc.NumElements||(uint64(C)<uint64(P)+Rows&&uint64(P)<uint64(C)+Rows))return Fail(TEXT("Palette output capacity/overlapping history ranges"));
 if(Output==Frame.Current||Output==Frame.Previous||Output==G.RegisterExternalBuffer(S.Bind)||Output==G.RegisterExternalBuffer(S.Items))return Fail(TEXT("Palette output aliases immutable/source buffer"));
 auto* Params=G.AllocParameters<FOKSkinPaletteCS::FParameters>();Params->CurrentPose=G.CreateSRV(Frame.Current);Params->PreviousPose=G.CreateSRV(Frame.Previous);Params->Items=G.CreateSRV(G.RegisterExternalBuffer(S.Items));Params->InverseBindRows=G.CreateSRV(G.RegisterExternalBuffer(S.Bind));
 Params->OutputTransforms=G.CreateUAV(FRDGBufferUAVDesc(Output,PF_R32G32B32A32_UINT));Params->ItemCount=S.Stats.Bones;Params->CurrentRowOffset=C;Params->PreviousRowOffset=P;
 TShaderMapRef<FOKSkinPaletteCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));FComputeShaderUtils::AddPass(G,RDG_EVENT_NAME("Crowd.GpuSkinPalette"),Shader,Params,FComputeShaderUtils::GetGroupCount(S.Stats.Bones,64));++State->Stats.Passes;return true;
}
bool OKReadbackResidentSkinPalette(FOKResidentGpuPose& Pose,FOKGpuSkinPalette& Skin,const FOKGpuPoseBatch* Initial,const TArray<TArray<uint32>>& Clips,const FOKGpuSkinPaletteData* PaletteInitial,TConstArrayView<FOKGpuPosePlayback> Playback,uint64 FrameIndex,TArray<FVector4f>& Current,TArray<FVector4f>& Previous,TArray<FVector4f>& Palette,FOKResidentPoseStats& Stats,FOKGpuSkinPaletteStats& SkinStats,FString& Error){
 auto A=MakeShared<FRHIGPUBufferReadback,ESPMode::ThreadSafe>(TEXT("SkinOracle.Current")),B=MakeShared<FRHIGPUBufferReadback,ESPMode::ThreadSafe>(TEXT("SkinOracle.Previous")),K=MakeShared<FRHIGPUBufferReadback,ESPMode::ThreadSafe>(TEXT("SkinOracle.Palette"));auto BK=MakeShared<FRHIGPUBufferReadback,ESPMode::ThreadSafe>(TEXT("SkinOracle.Batch"));bool Submitted=false;uint32 PoseBytes=0,PaletteBytes=0,BatchBytes=0;TArray<FVector4f> BatchResult;
 ENQUEUE_RENDER_COMMAND(SkinOracle)([&](FRHICommandListImmediate& R){FRDGBuilder G(R);if(Initial&&!Pose.Initialize(G,*Initial,Clips,Error)){G.Execute();return;}Stats=Pose.GetStats();if(PaletteInitial&&!Skin.Initialize(G,*PaletteInitial,Stats.Generation,Error)){G.Execute();return;}
  FOKResidentPoseFrame Frame;if(!Pose.Evaluate(G,Playback,FrameIndex,Stats.Generation,false,Frame,Error)){G.Execute();return;}SkinStats=Skin.GetStats();auto* Out=G.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(16,SkinStats.Bones*6+23),TEXT("SkinOracle.Output"));
  AddClearUAVPass(G,G.CreateUAV(FRDGBufferUAVDesc(Out,PF_R32G32B32A32_UINT)),0xa5a5a5a5u);if(!Skin.AddPass(G,Frame,SkinStats.Generation,Out,7,18+SkinStats.Bones*3,Error)){G.Execute();return;}// Compare two sparse batch jobs against the independent legacy shader:
  // job 0 retains history, job 1 resets it to current. Canary gaps surround all ranges.
  FOKGpuSkinPaletteBatch Batch;const uint32 Rows=SkinStats.Bones*3,Stride=Out->Desc.NumElements;
  auto* BatchOut=G.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(16,Stride*2+7),TEXT("SkinOracle.BatchOutput"));
  AddClearUAVPass(G,G.CreateUAV(FRDGBufferUAVDesc(BatchOut,PF_R32G32B32A32_UINT)),0xa5a5a5a5u);
  TArray<FOKGpuSkinPaletteJob> Jobs={{&Skin,7,18+Rows,false},{&Skin,Stride+7,Stride+18+Rows,true}};
  FString Rejected;auto Invalid=Jobs;Invalid[1]=Invalid[0];
  if(Batch.AddPass(G,Frame,BatchOut,Invalid,Rejected)){Error=TEXT("Batch accepted overlapping jobs");G.Execute();return;}
  Invalid=Jobs;Invalid[1].PreviousRowOffset=BatchOut->Desc.NumElements;
  if(Batch.AddPass(G,Frame,BatchOut,Invalid,Rejected)){Error=TEXT("Batch accepted overflow");G.Execute();return;}
  if(Batch.AddPass(G,Frame,BatchOut,{},Rejected)){Error=TEXT("Batch accepted empty jobs");G.Execute();return;}
  if(!Batch.AddPass(G,Frame,BatchOut,Jobs,Error)){G.Execute();return;}
  BatchBytes=BatchOut->Desc.GetSize();AddEnqueueCopyPass(G,&BK.Get(),BatchOut,BatchBytes);
  PoseBytes=Frame.Current->Desc.GetSize();PaletteBytes=Out->Desc.GetSize();AddEnqueueCopyPass(G,&A.Get(),Frame.Current,PoseBytes);AddEnqueueCopyPass(G,&B.Get(),Frame.Previous,PoseBytes);AddEnqueueCopyPass(G,&K.Get(),Out,PaletteBytes);G.Execute();Stats=Pose.GetStats();SkinStats=Skin.GetStats();Submitted=true;
 });FlushRenderingCommands();if(!Submitted)return false;
 bool Ready=false;double Start=FPlatformTime::Seconds();while(FPlatformTime::Seconds()-Start<30){ENQUEUE_RENDER_COMMAND(SkinOraclePoll)([&](FRHICommandListImmediate&){Ready=A->IsReady()&&B->IsReady()&&K->IsReady()&&BK->IsReady();});FlushRenderingCommands();if(Ready)break;FPlatformProcess::Sleep(.01f);}
 if(!Ready){Error=TEXT("GPU palette readback timed out");return false;}
 Current.SetNumUninitialized(PoseBytes/16);Previous.SetNumUninitialized(PoseBytes/16);Palette.SetNumUninitialized(PaletteBytes/16);BatchResult.SetNumUninitialized(BatchBytes/16);
 ENQUEUE_RENDER_COMMAND(SkinOracleRead)([&](FRHICommandListImmediate&){FMemory::Memcpy(Current.GetData(),A->Lock(PoseBytes),PoseBytes);A->Unlock();FMemory::Memcpy(Previous.GetData(),B->Lock(PoseBytes),PoseBytes);B->Unlock();FMemory::Memcpy(Palette.GetData(),K->Lock(PaletteBytes),PaletteBytes);K->Unlock();FMemory::Memcpy(BatchResult.GetData(),BK->Lock(BatchBytes),BatchBytes);BK->Unlock();});FlushRenderingCommands();
 const uint32 Stride=Palette.Num(),BatchRows=SkinStats.Bones*3;
 auto ExpectedBatch=Palette;ExpectedBatch.Append(Palette);
 FMemory::Memcpy(ExpectedBatch.GetData()+Stride+18+BatchRows,Palette.GetData()+7,BatchRows*16);
 for(int32 I=0;I<7;++I)ExpectedBatch.Add(FVector4f(0,0,0,0));
 FMemory::Memset(ExpectedBatch.GetData()+Stride*2,0xa5,7*16);
 if(ExpectedBatch.Num()!=BatchResult.Num()||FMemory::Memcmp(ExpectedBatch.GetData(),BatchResult.GetData(),BatchBytes)){Error=TEXT("Batched palette differs from legacy/reset history or overwrites canaries");return false;}
 uint32 Rows=SkinStats.Bones*3;const uint32* Bits=reinterpret_cast<const uint32*>(Palette.GetData());for(uint32 I=0;I<uint32(Palette.Num());++I)if(I<7||(I>=7+Rows&&I<18+Rows)||I>=18+Rows*2)for(int C=0;C<4;++C)if(Bits[I*4+C]!=0xa5a5a5a5u){Error=TEXT("Palette wrote outside destination ranges");return false;}
 TArray<FVector4f> Packed;Packed.Append(Palette.GetData()+7,Rows);Packed.Append(Palette.GetData()+18+Rows,Rows);Palette=MoveTemp(Packed);return true;
}
