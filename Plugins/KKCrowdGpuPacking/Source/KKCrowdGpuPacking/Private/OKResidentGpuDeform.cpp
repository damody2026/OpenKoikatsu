#include "OKResidentGpuDeform.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RenderingThread.h"
#include "HAL/IConsoleManager.h"

static TAutoConsoleVariable<int32> CVarResidentDeformFusedPacking(TEXT("ok.GpuDeform.FusedPacking"),1,TEXT("Fuse native GPU deformation and vertex packing; 0 retains the two-pass GPU oracle."),ECVF_RenderThreadSafe);

struct FOKResidentGpuDeform::FState {
 TRefCountPtr<FRDGPooledBuffer> Source,Draw,Positions[2],Tangents[2];
 uint32 PaletteCount=0,Fallback=0,DrawCount=0,Current=0;bool HasFrame=false,HasPrevious=false;
 bool NativeUnitySpace=false;
 FOKResidentDeformStats Stats;
};
FOKResidentGpuDeform::FOKResidentGpuDeform():State(MakeUnique<FState>()){}
FOKResidentGpuDeform::~FOKResidentGpuDeform(){
 if(State&&!IsInRenderingThread()){auto Dead=MoveTemp(State);ENQUEUE_RENDER_COMMAND(ReleaseResidentDeform)([Dead=MoveTemp(Dead)](FRHICommandListImmediate&)mutable{Dead.Reset();});}
}
FOKResidentDeformStats FOKResidentGpuDeform::GetStats() const{check(IsInRenderingThread());return State->Stats;}
void FOKResidentGpuDeform::Release(){check(IsInRenderingThread());auto Stats=State->Stats;++Stats.Generation;Stats.ResidentBytes=0;State=MakeUnique<FState>();State->Stats=Stats;}
bool FOKResidentGpuDeform::GetCurrent(FRDGBuilder& G,uint64 Generation,FOKResidentDeformFrame& Result) const{
 check(IsInRenderingThread());const auto& S=*State;if(!S.HasFrame||Generation!=S.Stats.Generation)return false;
 FOKResidentDeformFrame Next;Next.Generation=Generation;Next.DrawCount=S.DrawCount;
 Next.Positions=G.RegisterExternalBuffer(S.Positions[S.Current]);Next.Tangents=G.RegisterExternalBuffer(S.Tangents[S.Current]);
 const uint32 Previous=S.HasPrevious?1-S.Current:S.Current;
 Next.PreviousPositions=G.RegisterExternalBuffer(S.Positions[Previous]);Next.PreviousTangents=G.RegisterExternalBuffer(S.Tangents[Previous]);
 Result=Next;return true;
}
static bool ValidateCorrection(const FMatrix44f* Post,FString& Error){
 if(!Post)return true;TArray<FVector4f> Rows;return OKPrepareGpuDeformPalette(MakeArrayView(Post,1),Rows,Error);
}
bool FOKResidentGpuDeform::Dispatch(FRDGBuilder& G,FState& S,TConstArrayView<FVector4f> Rows,const FMatrix44f* Post,FOKResidentDeformFrame& Result){
 const uint32 Slot=S.HasFrame?1-S.Current:0;
 auto* Palette=CreateStructuredBuffer(G,TEXT("ResidentDeform.Palette"),sizeof(FVector4f),Rows.Num(),Rows.GetData(),Rows.Num()*sizeof(FVector4f));
 auto* Positions=G.RegisterExternalBuffer(S.Positions[Slot]);auto* Tangents=G.RegisterExternalBuffer(S.Tangents[Slot]);
 auto* Source=G.RegisterExternalBuffer(S.Source);auto* Draw=G.RegisterExternalBuffer(S.Draw);
 const bool Fused=CVarResidentDeformFusedPacking.GetValueOnRenderThread()!=0&&OKAddRuntimeGpuDeformPackedPass(G,Source,Draw,Palette,Positions,Tangents,S.DrawCount,S.PaletteCount,S.Fallback,Post,S.NativeUnitySpace);
 if(!Fused){
  auto* Deformed=G.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(FOKGpuDeformOutput),S.DrawCount),TEXT("ResidentDeform.Scratch"));
  if(!OKAddRuntimeGpuDeformPass(G,Source,Draw,Palette,Deformed,S.DrawCount,S.PaletteCount,S.Fallback,Post,S.NativeUnitySpace))return false;
  const bool Packed=OKAddRuntimeGpuPackPass(G,Deformed,Positions,Tangents,S.DrawCount);check(Packed);
 }
 G.SetBufferAccessFinal(Positions,ERHIAccess::VertexOrIndexBuffer|ERHIAccess::SRVGraphics);
 G.SetBufferAccessFinal(Tangents,ERHIAccess::VertexOrIndexBuffer|ERHIAccess::SRVGraphics);
 FOKResidentDeformFrame Next;Next.Positions=Positions;Next.Tangents=Tangents;Next.Generation=S.Stats.Generation;Next.DrawCount=S.DrawCount;
 Next.PreviousPositions=S.HasFrame?G.RegisterExternalBuffer(S.Positions[S.Current]):Positions;
 Next.PreviousTangents=S.HasFrame?G.RegisterExternalBuffer(S.Tangents[S.Current]):Tangents;
 S.Current=Slot;S.HasPrevious=S.HasFrame;S.HasFrame=true;++S.Stats.Evaluations;S.Stats.PaletteUploadBytes+=uint64(Rows.Num())*sizeof(FVector4f);Result=Next;return true;
}
bool FOKResidentGpuDeform::Install(FRDGBuilder& G,TConstArrayView<FOKGpuDeformSource> Source,TConstArrayView<FOKGpuDeformDraw> Draw,
 TConstArrayView<FMatrix44f> Matrices,uint32 Fallback,const FMatrix44f* Post,uint64 Generation,FOKResidentDeformFrame& Result,FString& Error,uint64 Budget,bool NativeUnitySpace){
 check(IsInRenderingThread());
 if(Generation!=State->Stats.Generation){Error=TEXT("Stale GPU deform installation generation");return false;}
 if(!OKGpuDeformAvailable()||!OKGpuPackAvailable()){Error=TEXT("GPU deform/pack shaders unavailable");return false;}
 TArray<FVector4f> Rows;
 if(!OKPrepareGpuDeformPalette(Matrices,Rows,Error)||!ValidateCorrection(Post,Error)
  ||!OKValidateGpuDeformSource(Source,Draw,Matrices.Num(),Fallback,Error))return false;
 if(FMath::DivideAndRoundUp(uint32(Draw.Num()),64u)>65535){Error=TEXT("GPU deform draw dispatch too large");return false;}
 const uint64 Immutable=uint64(Source.Num())*sizeof(FOKGpuDeformSource)+uint64(Draw.Num())*sizeof(FOKGpuDeformDraw);
 const uint64 Resident=Immutable+uint64(Draw.Num())*40;
 const uint64 Peak=Resident+uint64(Draw.Num())*sizeof(FOKGpuDeformOutput)+uint64(Rows.Num())*sizeof(FVector4f);
 if(!Budget||State->Stats.ResidentBytes>Budget||Peak>Budget-State->Stats.ResidentBytes){Error=TEXT("GPU deform replacement exceeds logical buffer budget");return false;}
 auto Next=MakeUnique<FState>();Next->Stats=State->Stats;++Next->Stats.Generation;++Next->Stats.Installations;
 Next->Stats.ResidentBytes=Resident;Next->Stats.PeakLogicalBytes=FMath::Max(Next->Stats.PeakLogicalBytes,State->Stats.ResidentBytes+Peak);
 Next->Stats.ImmutableUploadBytes+=Immutable;Next->PaletteCount=Matrices.Num();Next->Fallback=Fallback;Next->DrawCount=Draw.Num();
 Next->NativeUnitySpace=NativeUnitySpace;
 Next->Source=G.ConvertToExternalBuffer(CreateStructuredBuffer(G,TEXT("ResidentDeform.Source"),sizeof(FOKGpuDeformSource),Source.Num(),Source.GetData(),Source.Num()*sizeof(FOKGpuDeformSource)));
 Next->Draw=G.ConvertToExternalBuffer(CreateStructuredBuffer(G,TEXT("ResidentDeform.Draw"),sizeof(FOKGpuDeformDraw),Draw.Num(),Draw.GetData(),Draw.Num()*sizeof(FOKGpuDeformDraw)));
 for(uint32 Slot=0;Slot<2;++Slot){
  auto P=FRDGBufferDesc::CreateByteAddressDesc(Next->DrawCount*12);P.Usage|=EBufferUsageFlags::VertexBuffer;
  auto T=FRDGBufferDesc::CreateByteAddressDesc(Next->DrawCount*8);T.Usage|=EBufferUsageFlags::VertexBuffer;
  Next->Positions[Slot]=G.ConvertToExternalBuffer(G.CreateBuffer(P,TEXT("ResidentDeform.Positions")));
  Next->Tangents[Slot]=G.ConvertToExternalBuffer(G.CreateBuffer(T,TEXT("ResidentDeform.Tangents")));
 }
 FOKResidentDeformFrame Frame;
 if(!Dispatch(G,*Next,Rows,Post,Frame)){Error=TEXT("GPU deform initial dispatch rejected");return false;}
 State=MoveTemp(Next);Result=Frame;Error.Reset();return true;
}
bool FOKResidentGpuDeform::Update(FRDGBuilder& G,TConstArrayView<FMatrix44f> Matrices,const FMatrix44f* Post,uint64 Generation,FOKResidentDeformFrame& Result,FString& Error){
 check(IsInRenderingThread());
 if(!State->HasFrame||Generation!=State->Stats.Generation){Error=TEXT("Stale or uninitialized GPU deform update");return false;}
 if(Matrices.Num()!=int32(State->PaletteCount)){Error=TEXT("GPU deform palette topology changed; install new generation");return false;}
 TArray<FVector4f> Rows;if(!OKPrepareGpuDeformPalette(Matrices,Rows,Error)||!ValidateCorrection(Post,Error))return false;
 if(!Dispatch(G,*State,Rows,Post,Result)){Error=TEXT("GPU deform update rejected");return false;}Error.Reset();return true;
}
