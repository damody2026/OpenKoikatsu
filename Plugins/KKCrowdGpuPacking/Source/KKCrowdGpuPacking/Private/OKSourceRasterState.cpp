#include "OKSourceRasterState.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "OneColorShader.h"
#include "RenderUtils.h"
#include "PipelineStateCache.h"
#include "RHIStaticStates.h"
#include "RHIGPUReadback.h"
#include "RenderingThread.h"
#include "HAL/PlatformProcess.h"

bool OKPrepareSourceRasterState(const FOKSourceRasterState& S,FOKSourceRasterBinding& Out,FString& Error){
 auto Fail=[&](const TCHAR* Why){Error=Why;return false;};
 if(S.DepthCompare<0||S.DepthCompare>8||S.StencilCompare<1||S.StencilCompare>8||S.StencilFail<0||S.StencilFail>7||S.DepthFail<0||S.DepthFail>7||S.StencilPass<0||S.StencilPass>7||S.StencilReadMask<0||S.StencilReadMask>255||S.StencilWriteMask<0||S.StencilWriteMask>255||S.StencilRef<0||S.StencilRef>255)return Fail(TEXT("Invalid source depth/stencil enum or mask"));
 const ECompareFunction Compare[]={CF_Always,CF_Never,CF_Less,CF_Equal,CF_LessEqual,CF_Greater,CF_NotEqual,CF_GreaterEqual,CF_Always};
 const ECompareFunction Reverse[]={CF_Always,CF_Never,CF_Greater,CF_Equal,CF_GreaterEqual,CF_Less,CF_NotEqual,CF_LessEqual,CF_Always};
 const EStencilOp Ops[]={SO_Keep,SO_Zero,SO_Replace,SO_SaturatedIncrement,SO_SaturatedDecrement,SO_Invert,SO_Increment,SO_Decrement};
 const EBlendFactor Factors[]={BF_Zero,BF_One,BF_DestColor,BF_SourceColor,BF_InverseDestColor,BF_SourceAlpha,BF_InverseSourceColor,BF_DestAlpha,BF_InverseDestAlpha,BF_Zero,BF_InverseSourceAlpha};
 for(int32 F:{S.SrcBlend,S.DstBlend,S.SrcAlphaBlend,S.DstAlphaBlend})if(F<0||F>10||F==9)return Fail(TEXT("Unsupported source blend factor (including SrcAlphaSaturate)"));
 FOKSourceRasterBinding B;B.StencilRef=S.StencilRef;
 B.DepthStencil=FDepthStencilStateInitializerRHI(S.DepthWrite&&S.DepthCompare!=0,Reverse[S.DepthCompare],S.Stencil,Compare[S.StencilCompare],Ops[S.StencilFail],Ops[S.DepthFail],Ops[S.StencilPass],S.Stencil,Compare[S.StencilCompare],Ops[S.StencilFail],Ops[S.DepthFail],Ops[S.StencilPass],uint8(S.StencilReadMask),uint8(S.StencilWriteMask));
 B.Blend=FBlendStateInitializerRHI(FBlendStateInitializerRHI::FRenderTarget(BO_Add,Factors[S.SrcBlend],Factors[S.DstBlend],BO_Add,Factors[S.SrcAlphaBlend],Factors[S.DstAlphaBlend],CW_RGBA));
 Out=MoveTemp(B);Error.Reset();return true;
}

BEGIN_SHADER_PARAMETER_STRUCT(FOKSourceRasterOracleParameters,)
 RDG_BUFFER_ACCESS(Vertices,ERHIAccess::VertexOrIndexBuffer)
 RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

bool OKVerifySourceRasterGpu(TArray<FColor>& Pixels,FString& Error){
 check(IsInGameThread());
 FOKSourceRasterState Base;Base.DepthWrite=true;Base.Stencil=true;Base.StencilPass=2;Base.StencilRef=2;
 FOKSourceRasterBinding Checked;if(!OKPrepareSourceRasterState(Base,Checked,Error)||Checked.DepthStencil.DepthTest!=CF_GreaterEqual||Checked.DepthStencil.FrontFacePassStencilOp!=SO_Replace||Checked.StencilRef!=2){Error=TEXT("Source reversed-Z/stencil policy failed");return false;}
 const auto Prior=Checked;Base.StencilRef=256;if(OKPrepareSourceRasterState(Base,Checked,Error)||Checked.StencilRef!=Prior.StencilRef){Error=TEXT("Invalid source state modified binding");return false;}Base.StencilRef=2;
 Base.SrcBlend=9;if(OKPrepareSourceRasterState(Base,Checked,Error)){Error=TEXT("Unsupported source factor admitted");return false;}Base.SrcBlend=5;
 auto Readback=MakeShared<FRHIGPUTextureReadback,ESPMode::ThreadSafe>(TEXT("CrowdSourceRasterOracle"));
 ENQUEUE_RENDER_COMMAND(SourceRasterOracle)([Readback](FRHICommandListImmediate& R){
  FRDGBuilder G(R);
  auto* Color=G.CreateTexture(FRDGTextureDesc::Create2D(FIntPoint(4,1),PF_B8G8R8A8,FClearValueBinding::Transparent,TexCreate_RenderTargetable|TexCreate_ShaderResource),TEXT("Crowd.SourceRasterColor"));
  auto* Depth=G.CreateTexture(FRDGTextureDesc::Create2D(FIntPoint(4,1),PF_DepthStencil,FClearValueBinding(0.f,0),TexCreate_DepthStencilTargetable),TEXT("Crowd.PrivateSourceDepthStencil"));
  const FVector4f Triangle[]={FVector4f(-1,-1,0,1),FVector4f(-1,3,0,1),FVector4f(3,-1,0,1)};
  auto Desc=FRDGBufferDesc::CreateBufferDesc(sizeof(FVector4f),3);Desc.Usage|=BUF_VertexBuffer;
  auto* Vertices=G.CreateBuffer(Desc,TEXT("Crowd.SourceOracleVertices"));G.QueueBufferUpload(Vertices,Triangle,sizeof(Triangle));
  TShaderMapRef<TOneColorVS<true>> VS(GetGlobalShaderMap(GMaxRHIFeatureLevel));
  TShaderMapRef<FOneColorPS> PS(GetGlobalShaderMap(GMaxRHIFeatureLevel));
  bool First=true;
  auto Draw=[&](int32 Cell,float Z,FLinearColor C,FOKSourceRasterState S){
   FOKSourceRasterBinding B;FString Why;check(OKPrepareSourceRasterState(S,B,Why));
   auto* P=G.AllocParameters<FOKSourceRasterOracleParameters>();P->Vertices=Vertices;
   P->RenderTargets[0]=FRenderTargetBinding(Color,First?ERenderTargetLoadAction::EClear:ERenderTargetLoadAction::ELoad);
   P->RenderTargets.DepthStencil=FDepthStencilBinding(Depth,First?ERenderTargetLoadAction::EClear:ERenderTargetLoadAction::ELoad,First?ERenderTargetLoadAction::EClear:ERenderTargetLoadAction::ELoad,FExclusiveDepthStencil::DepthWrite_StencilWrite);First=false;
   G.AddPass(RDG_EVENT_NAME("Crowd.SourceRasterOracle"),P,ERDGPassFlags::Raster,[P,VS,PS,B,Cell,Z,C](FRHICommandList& Cmd){
    FGraphicsPipelineStateInitializer Pipeline;Cmd.ApplyCachedRenderTargets(Pipeline);Pipeline.RasterizerState=TStaticRasterizerState<FM_Solid,CM_None>::GetRHI();Pipeline.BlendState=RHICreateBlendState(B.Blend);Pipeline.DepthStencilState=RHICreateDepthStencilState(B.DepthStencil);
    Pipeline.BoundShaderState.VertexDeclarationRHI=GetVertexDeclarationFVector4();Pipeline.BoundShaderState.VertexShaderRHI=VS.GetVertexShader();Pipeline.BoundShaderState.PixelShaderRHI=PS.GetPixelShader();Pipeline.PrimitiveType=PT_TriangleList;SetGraphicsPipelineState(Cmd,Pipeline,B.StencilRef);
    SetShaderParametersLegacyVS(Cmd,VS,Z);FOneColorPS::FParameters Colors;PS->FillParameters(Colors,&C,1);SetShaderParameters(Cmd,PS,PS.GetPixelShader(),Colors);
    Cmd.SetViewport(Cell,0,0,Cell+1,1,1);Cmd.SetStreamSource(0,P->Vertices->GetRHI(),0);Cmd.DrawPrimitive(0,1,1);
   });
  };
  for(int32 Cell=0;Cell<4;++Cell){FOKSourceRasterState S;S.DepthWrite=Cell!=1;S.Stencil=Cell>=2;S.StencilPass=2;S.StencilRef=2;Draw(Cell,.8f,FLinearColor(1,0,0,.5),S);
   S.DepthWrite=false;S.SrcBlend=S.SrcAlphaBlend=1;S.DstBlend=S.DstAlphaBlend=0;S.StencilPass=0;S.StencilCompare=3;S.StencilRef=Cell==3?3:2;Draw(Cell,Cell<2?.4f:.9f,Cell<2?FLinearColor(0,0,1,1):FLinearColor(0,1,0,1),S);
  }
  AddEnqueueCopyPass(G,&Readback.Get(),Color);G.Execute();
 });
 FlushRenderingCommands();bool Ready=false;const double Start=FPlatformTime::Seconds();
 while(FPlatformTime::Seconds()-Start<30){ENQUEUE_RENDER_COMMAND(SourceRasterPoll)([Readback,&Ready](FRHICommandListImmediate&){Ready=Readback->IsReady();});FlushRenderingCommands();if(Ready)break;FPlatformProcess::Sleep(.01f);}
 if(!Ready){Error=TEXT("Source raster GPU readback timeout");return false;}
 TArray<FColor> Actual;Actual.SetNum(4);bool Layout=false;
 ENQUEUE_RENDER_COMMAND(SourceRasterRead)([&](FRHICommandListImmediate&){int32 Pitch=0,Height=0;const auto* Data=static_cast<const FColor*>(Readback->Lock(Pitch,&Height));Layout=Data&&Pitch>=4&&Height>=1;if(Layout)FMemory::Memcpy(Actual.GetData(),Data,4*sizeof(FColor));Readback->Unlock();});FlushRenderingCommands();
 const FColor Expected[]={FColor(128,0,0,64),FColor(0,0,255,255),FColor(0,255,0,255),FColor(128,0,0,64)};
 if(!Layout){Error=TEXT("Source raster readback layout invalid");return false;}
 for(int32 I=0;I<4;++I)for(int32 C=0;C<4;++C)if(FMath::Abs(int32(Actual[I].DWColor()>>(C*8)&255)-int32(Expected[I].DWColor()>>(C*8)&255))>1){Error=FString::Printf(TEXT("Source depth/blend/stencil GPU pixel mismatch cell=%d actual=%s"),I,*Actual[I].ToString());return false;}
 Pixels=MoveTemp(Actual);Error.Reset();return true;
}
