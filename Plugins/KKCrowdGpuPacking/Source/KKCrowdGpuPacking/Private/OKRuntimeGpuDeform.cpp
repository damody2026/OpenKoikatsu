#include "OKRuntimeGpuDeform.h"
#include "GlobalShader.h"
#include "ShaderParameterStruct.h"
#include "RenderGraphUtils.h"

static_assert(sizeof(FOKGpuDeformSource)==80);
static_assert(sizeof(FOKGpuDeformDraw)==64);
static_assert(sizeof(FOKGpuDeformOutput)==48);

bool OKPrepareGpuDeformPalette(TConstArrayView<FMatrix44f> Matrices,TArray<FVector4f>& Rows,FString& Error){
 if(Matrices.IsEmpty()||Matrices.Num()>65535){Error=TEXT("GPU deform palette length invalid");return false;}
 TArray<FVector4f> Prepared;Prepared.Reserve(Matrices.Num()*3);
 for(const auto& M:Matrices){
  for(int32 R=0;R<4;++R)for(int32 C=0;C<4;++C)if(!FMath::IsFinite(M.M[R][C])){Error=TEXT("GPU deform palette is nonfinite");return false;}
  if(M.M[0][3]!=0||M.M[1][3]!=0||M.M[2][3]!=0||M.M[3][3]!=1){Error=TEXT("GPU deform palette must be affine");return false;}
  for(int32 Axis=0;Axis<3;++Axis)Prepared.Add(FVector4f(M.M[0][Axis],M.M[1][Axis],M.M[2][Axis],M.M[3][Axis]));
 }
 Rows=MoveTemp(Prepared);Error.Reset();return true;
}

bool OKValidateGpuDeformSource(TConstArrayView<FOKGpuDeformSource> Source,TConstArrayView<FOKGpuDeformDraw> Draw,
 uint32 PaletteCount,uint32 FallbackSlot,FString& Error){
 auto Fail=[&](const TCHAR* Message){Error=Message;return false;};
 if(Source.IsEmpty()||Draw.IsEmpty()||!PaletteCount||FallbackSlot>=PaletteCount)return Fail(TEXT("GPU deform source/draw/palette is empty or fallback invalid"));
 auto Finite=[](const FVector4f& V){return FMath::IsFinite(V.X)&&FMath::IsFinite(V.Y)&&FMath::IsFinite(V.Z)&&FMath::IsFinite(V.W);};
 for(const auto& V:Source){
  if(!Finite(V.Position)||!Finite(V.Normal)||!Finite(V.Tangent)||!Finite(V.Weights))return Fail(TEXT("GPU deform source has nonfinite attributes"));
  for(int32 I=0;I<4;++I)if(FMath::Abs(V.Weights[I])>=1.e-6f&&V.Bones[I]>=PaletteCount)return Fail(TEXT("GPU deform active bone outside palette"));
 }
 for(const auto& D:Draw)if(D.Binding.X>=uint32(Source.Num())||!Finite(D.Offset)||!Finite(D.SectionNormal)||!Finite(D.RestNormal))return Fail(TEXT("GPU deform draw mapping/attributes invalid"));
 Error.Reset();return true;
}

class FOKRuntimeGpuDeformCS:public FGlobalShader {
 DECLARE_GLOBAL_SHADER(FOKRuntimeGpuDeformCS);
 SHADER_USE_PARAMETER_STRUCT(FOKRuntimeGpuDeformCS,FGlobalShader);
 BEGIN_SHADER_PARAMETER_STRUCT(FParameters,)
  SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FSource>,Sources)
  SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FDraw>,Draws)
  SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>,PaletteRows)
  SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<FOutput>,Output)
  SHADER_PARAMETER(uint32,DrawCount)
  SHADER_PARAMETER(uint32,PaletteCount)
  SHADER_PARAMETER(uint32,FallbackSlot)
  SHADER_PARAMETER(uint32,HasPostSkinCorrection)
  SHADER_PARAMETER(uint32,NativeUnitySpace)
  SHADER_PARAMETER(FVector4f,PostSkinRow0)
  SHADER_PARAMETER(FVector4f,PostSkinRow1)
  SHADER_PARAMETER(FVector4f,PostSkinRow2)
 END_SHADER_PARAMETER_STRUCT()
 static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& P){return IsFeatureLevelSupported(P.Platform,ERHIFeatureLevel::SM5);}
};
IMPLEMENT_GLOBAL_SHADER(FOKRuntimeGpuDeformCS,"/Plugin/KKCrowdGpuPacking/RuntimeDeform.usf","MainCS",SF_Compute);
bool OKGpuDeformAvailable(){auto* Map=GetGlobalShaderMap(GMaxRHIFeatureLevel);return Map&&Map->HasShader(&FOKRuntimeGpuDeformCS::GetStaticType(),0);}
bool OKAddRuntimeGpuDeformPass(FRDGBuilder& G,FRDGBufferRef Source,FRDGBufferRef Draw,FRDGBufferRef Palette,FRDGBufferRef Output,
 uint32 DrawCount,uint32 PaletteCount,uint32 FallbackSlot,const FMatrix44f* PostSkinCorrection,bool NativeUnitySpace){
 if(!Source||!Draw||!Palette||!Output||!DrawCount||!PaletteCount||FallbackSlot>=PaletteCount)return false;
 if(Source->Desc.BytesPerElement!=sizeof(FOKGpuDeformSource)||Draw->Desc.BytesPerElement!=sizeof(FOKGpuDeformDraw)
  ||Palette->Desc.BytesPerElement!=sizeof(FVector4f)||Output->Desc.BytesPerElement!=sizeof(FOKGpuDeformOutput)
  ||!Source->Desc.NumElements||Draw->Desc.NumElements<DrawCount||Output->Desc.NumElements<DrawCount
  ||uint64(Palette->Desc.NumElements)<uint64(PaletteCount)*3||FMath::DivideAndRoundUp(DrawCount,64u)>65535)return false;
 TArray<FVector4f> Correction;FString Error;
 const FMatrix44f& Placement=PostSkinCorrection?*PostSkinCorrection:FMatrix44f::Identity;
 if(!OKPrepareGpuDeformPalette(MakeArrayView(&Placement,1),Correction,Error))return false;
 auto* P=G.AllocParameters<FOKRuntimeGpuDeformCS::FParameters>();
 P->Sources=G.CreateSRV(Source);P->Draws=G.CreateSRV(Draw);P->PaletteRows=G.CreateSRV(Palette);P->Output=G.CreateUAV(Output);
 P->DrawCount=DrawCount;P->PaletteCount=PaletteCount;P->FallbackSlot=FallbackSlot;
 P->HasPostSkinCorrection=PostSkinCorrection?1:0;
 P->NativeUnitySpace=NativeUnitySpace?1:0;
 P->PostSkinRow0=Correction[0];P->PostSkinRow1=Correction[1];P->PostSkinRow2=Correction[2];
 TShaderMapRef<FOKRuntimeGpuDeformCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
 FComputeShaderUtils::AddPass(G,RDG_EVENT_NAME("Interaction.RuntimeGpuDeform"),Shader,P,FIntVector(FMath::DivideAndRoundUp(DrawCount,64u),1,1));
 return true;
}

class FOKRuntimeGpuPackCS:public FGlobalShader {
 DECLARE_GLOBAL_SHADER(FOKRuntimeGpuPackCS);
 SHADER_USE_PARAMETER_STRUCT(FOKRuntimeGpuPackCS,FGlobalShader);
 BEGIN_SHADER_PARAMETER_STRUCT(FParameters,)
  SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FOutput>,Deformed)
  SHADER_PARAMETER_RDG_BUFFER_UAV(RWByteAddressBuffer,PositionBytes)
  SHADER_PARAMETER_RDG_BUFFER_UAV(RWByteAddressBuffer,TangentBytes)
  SHADER_PARAMETER(uint32,PackDrawCount)
 END_SHADER_PARAMETER_STRUCT()
 static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& P){return IsFeatureLevelSupported(P.Platform,ERHIFeatureLevel::SM5);}
};
IMPLEMENT_GLOBAL_SHADER(FOKRuntimeGpuPackCS,"/Plugin/KKCrowdGpuPacking/RuntimeDeform.usf","PackCS",SF_Compute);
bool OKGpuPackAvailable(){auto* Map=GetGlobalShaderMap(GMaxRHIFeatureLevel);return Map&&Map->HasShader(&FOKRuntimeGpuPackCS::GetStaticType(),0);}
bool OKAddRuntimeGpuPackPass(FRDGBuilder& G,FRDGBufferRef Deformed,FRDGBufferRef Positions,FRDGBufferRef Tangents,uint32 Count){
 if(!Deformed||!Positions||!Tangents||!Count||Positions==Tangents||Deformed==Positions||Deformed==Tangents
  ||Deformed->Desc.BytesPerElement!=sizeof(FOKGpuDeformOutput)||Deformed->Desc.NumElements<Count
  ||Positions->Desc.GetSize()<uint64(Count)*12||Tangents->Desc.GetSize()<uint64(Count)*8
  ||!EnumHasAllFlags(Positions->Desc.Usage,EBufferUsageFlags::ByteAddressBuffer|EBufferUsageFlags::VertexBuffer|EBufferUsageFlags::UnorderedAccess)
  ||!EnumHasAllFlags(Tangents->Desc.Usage,EBufferUsageFlags::ByteAddressBuffer|EBufferUsageFlags::VertexBuffer|EBufferUsageFlags::UnorderedAccess)
  ||FMath::DivideAndRoundUp(Count,64u)>65535)return false;
 auto* P=G.AllocParameters<FOKRuntimeGpuPackCS::FParameters>();P->Deformed=G.CreateSRV(Deformed);
 P->PositionBytes=G.CreateUAV(Positions);P->TangentBytes=G.CreateUAV(Tangents);P->PackDrawCount=Count;
 TShaderMapRef<FOKRuntimeGpuPackCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
 FComputeShaderUtils::AddPass(G,RDG_EVENT_NAME("Interaction.RuntimeGpuPack"),Shader,P,FIntVector(FMath::DivideAndRoundUp(Count,64u),1,1));return true;
}

class FOKRuntimeGpuDeformPackedCS:public FGlobalShader {
 DECLARE_GLOBAL_SHADER(FOKRuntimeGpuDeformPackedCS);
 SHADER_USE_PARAMETER_STRUCT(FOKRuntimeGpuDeformPackedCS,FGlobalShader);
 BEGIN_SHADER_PARAMETER_STRUCT(FParameters,)
  SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FSource>,Sources)
  SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FDraw>,Draws)
  SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>,PaletteRows)
  SHADER_PARAMETER_RDG_BUFFER_UAV(RWByteAddressBuffer,PositionBytes)
  SHADER_PARAMETER_RDG_BUFFER_UAV(RWByteAddressBuffer,TangentBytes)
  SHADER_PARAMETER(uint32,DrawCount)
  SHADER_PARAMETER(uint32,PaletteCount)
  SHADER_PARAMETER(uint32,FallbackSlot)
  SHADER_PARAMETER(uint32,HasPostSkinCorrection)
  SHADER_PARAMETER(uint32,NativeUnitySpace)
  SHADER_PARAMETER(FVector4f,PostSkinRow0)
  SHADER_PARAMETER(FVector4f,PostSkinRow1)
  SHADER_PARAMETER(FVector4f,PostSkinRow2)
 END_SHADER_PARAMETER_STRUCT()
 static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& P){return IsFeatureLevelSupported(P.Platform,ERHIFeatureLevel::SM5);}
};
IMPLEMENT_GLOBAL_SHADER(FOKRuntimeGpuDeformPackedCS,"/Plugin/KKCrowdGpuPacking/RuntimeDeform.usf","DeformPackedCS",SF_Compute);
bool OKGpuDeformPackedAvailable(){auto* Map=GetGlobalShaderMap(GMaxRHIFeatureLevel);return Map&&Map->HasShader(&FOKRuntimeGpuDeformPackedCS::GetStaticType(),0);}
bool OKAddRuntimeGpuDeformPackedPass(FRDGBuilder& G,FRDGBufferRef Source,FRDGBufferRef Draw,FRDGBufferRef Palette,FRDGBufferRef Positions,FRDGBufferRef Tangents,
 uint32 DrawCount,uint32 PaletteCount,uint32 FallbackSlot,const FMatrix44f* PostSkinCorrection,bool NativeUnitySpace){
 if(!OKGpuDeformPackedAvailable()||!Source||!Draw||!Palette||!Positions||!Tangents||!DrawCount||!PaletteCount||FallbackSlot>=PaletteCount)return false;
 if(Source->Desc.BytesPerElement!=sizeof(FOKGpuDeformSource)||Draw->Desc.BytesPerElement!=sizeof(FOKGpuDeformDraw)||Palette->Desc.BytesPerElement!=sizeof(FVector4f)
  ||!Source->Desc.NumElements||Draw->Desc.NumElements<DrawCount||uint64(Palette->Desc.NumElements)<uint64(PaletteCount)*3
  ||Positions==Tangents||Source==Positions||Source==Tangents||Draw==Positions||Draw==Tangents||Palette==Positions||Palette==Tangents
  ||Positions->Desc.GetSize()<uint64(DrawCount)*12||Tangents->Desc.GetSize()<uint64(DrawCount)*8
  ||!EnumHasAllFlags(Positions->Desc.Usage,EBufferUsageFlags::ByteAddressBuffer|EBufferUsageFlags::VertexBuffer|EBufferUsageFlags::UnorderedAccess)
  ||!EnumHasAllFlags(Tangents->Desc.Usage,EBufferUsageFlags::ByteAddressBuffer|EBufferUsageFlags::VertexBuffer|EBufferUsageFlags::UnorderedAccess)
  ||FMath::DivideAndRoundUp(DrawCount,64u)>65535)return false;
 TArray<FVector4f> Correction;FString Error;const FMatrix44f& Placement=PostSkinCorrection?*PostSkinCorrection:FMatrix44f::Identity;
 if(!OKPrepareGpuDeformPalette(MakeArrayView(&Placement,1),Correction,Error))return false;
 auto* P=G.AllocParameters<FOKRuntimeGpuDeformPackedCS::FParameters>();P->Sources=G.CreateSRV(Source);P->Draws=G.CreateSRV(Draw);P->PaletteRows=G.CreateSRV(Palette);
 P->PositionBytes=G.CreateUAV(Positions);P->TangentBytes=G.CreateUAV(Tangents);P->DrawCount=DrawCount;P->PaletteCount=PaletteCount;P->FallbackSlot=FallbackSlot;
 P->HasPostSkinCorrection=PostSkinCorrection?1:0;P->NativeUnitySpace=NativeUnitySpace?1:0;P->PostSkinRow0=Correction[0];P->PostSkinRow1=Correction[1];P->PostSkinRow2=Correction[2];
 TShaderMapRef<FOKRuntimeGpuDeformPackedCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
 FComputeShaderUtils::AddPass(G,RDG_EVENT_NAME("Interaction.RuntimeGpuDeformPacked"),Shader,P,FIntVector(FMath::DivideAndRoundUp(DrawCount,64u),1,1));return true;
}
