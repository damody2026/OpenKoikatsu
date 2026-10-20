#include "OKRuntimeGpuCoating.h"
#include "GlobalShader.h"
#include "ShaderParameterStruct.h"
#include "RenderGraphUtils.h"

static_assert(sizeof(FOKGpuCoatingBinding)==48);
bool OKValidateGpuCoatingBindings(TConstArrayView<FOKGpuCoatingBinding> Bindings,uint32 SourceCount,const FMatrix44f& Transform,FString& Error){
 auto Fail=[&](const TCHAR* Message){Error=Message;return false;};
 if(!SourceCount||Bindings.IsEmpty()||uint64(SourceCount)*12>MAX_uint32||FMath::DivideAndRoundUp(uint32(Bindings.Num()),64u)>65535)return Fail(TEXT("Coating source/count invalid"));
 TArray<FVector4f> Rows;if(!OKPrepareGpuDeformPalette(MakeArrayView(&Transform,1),Rows,Error))return false;
 for(const auto& B:Bindings){
  if(B.Vertices.X>=SourceCount||B.Vertices.Y>=SourceCount||B.Vertices.Z>=SourceCount||B.Vertices.W)return Fail(TEXT("Coating source binding invalid"));
  for(int32 Axis=0;Axis<4;++Axis)if(!FMath::IsFinite(B.Offset[Axis])||!FMath::IsFinite(B.Normal[Axis]))return Fail(TEXT("Coating binding is nonfinite"));
 }
 Error.Reset();return true;
}
class FOKRuntimeGpuCoatingCS:public FGlobalShader {
 DECLARE_GLOBAL_SHADER(FOKRuntimeGpuCoatingCS);
 SHADER_USE_PARAMETER_STRUCT(FOKRuntimeGpuCoatingCS,FGlobalShader);
 BEGIN_SHADER_PARAMETER_STRUCT(FParameters,)
  SHADER_PARAMETER_RDG_BUFFER_SRV(ByteAddressBuffer,CoatingSourcePositions)
  SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FCoatingBinding>,CoatingBindings)
  SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<FOutput>,CoatingOutput)
  SHADER_PARAMETER(uint32,CoatingCount)
  SHADER_PARAMETER(FVector4f,CoatingRow0)
  SHADER_PARAMETER(FVector4f,CoatingRow1)
  SHADER_PARAMETER(FVector4f,CoatingRow2)
 END_SHADER_PARAMETER_STRUCT()
 static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& P){return IsFeatureLevelSupported(P.Platform,ERHIFeatureLevel::SM5);}
};
IMPLEMENT_GLOBAL_SHADER(FOKRuntimeGpuCoatingCS,"/Plugin/KKCrowdGpuPacking/RuntimeCoating.usf","CoatingCS",SF_Compute);
bool OKGpuCoatingAvailable(){auto* Map=GetGlobalShaderMap(GMaxRHIFeatureLevel);return Map&&Map->HasShader(&FOKRuntimeGpuCoatingCS::GetStaticType(),0);}
bool OKAddRuntimeGpuCoatingPass(FRDGBuilder& G,FRDGBufferRef Source,FRDGBufferRef Bindings,FRDGBufferRef Output,uint32 SourceCount,uint32 Count,const FMatrix44f& Transform){
 if(!OKGpuCoatingAvailable()||!Source||!Bindings||!Output||!SourceCount||!Count||Source==Bindings||Source==Output||Bindings==Output
  ||uint64(SourceCount)*12>MAX_uint32||Source->Desc.GetSize()<uint64(SourceCount)*12
  ||!EnumHasAllFlags(Source->Desc.Usage,EBufferUsageFlags::ByteAddressBuffer|EBufferUsageFlags::ShaderResource)
  ||Bindings->Desc.BytesPerElement!=sizeof(FOKGpuCoatingBinding)||Bindings->Desc.NumElements<Count||!EnumHasAllFlags(Bindings->Desc.Usage,EBufferUsageFlags::StructuredBuffer|EBufferUsageFlags::ShaderResource)
  ||Output->Desc.BytesPerElement!=sizeof(FOKGpuDeformOutput)||Output->Desc.NumElements<Count||!EnumHasAllFlags(Output->Desc.Usage,EBufferUsageFlags::StructuredBuffer|EBufferUsageFlags::UnorderedAccess)
  ||(uint64(Count)+63)/64>65535)return false;
 TArray<FVector4f> Rows;FString Error;if(!OKPrepareGpuDeformPalette(MakeArrayView(&Transform,1),Rows,Error))return false;
 auto* P=G.AllocParameters<FOKRuntimeGpuCoatingCS::FParameters>();
 P->CoatingSourcePositions=G.CreateSRV(FRDGBufferSRVDesc(Source,PF_R32_UINT));P->CoatingBindings=G.CreateSRV(Bindings);P->CoatingOutput=G.CreateUAV(Output);
 P->CoatingCount=Count;P->CoatingRow0=Rows[0];P->CoatingRow1=Rows[1];P->CoatingRow2=Rows[2];
 TShaderMapRef<FOKRuntimeGpuCoatingCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
 FComputeShaderUtils::AddPass(G,RDG_EVENT_NAME("Interaction.RuntimeGpuCoating"),Shader,P,FIntVector(FMath::DivideAndRoundUp(Count,64u),1,1));return true;
}
