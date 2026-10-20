#include "OKSourceMaterialMesh.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialRenderProxy.h"
#include "SceneViewExtension.h"
#include "UnrealClient.h"
#include "LocalVertexFactory.h"
#include "StaticMeshResources.h"
#include "DynamicMeshBuilder.h"
#include "MeshMaterialShader.h"
#include "MeshPassProcessor.h"
#include "PrimitiveUniformShaderParameters.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RHIGPUReadback.h"
#include "RenderingThread.h"
#include "HAL/PlatformProcess.h"

// Isolated oracle: the normal local-VF shader code, with a private primitive UB
// instead of the scene's GPUScene primitive IDs. Production VFs are unchanged.
class FOKSourceOracleVF:public FLocalVertexFactory {
 DECLARE_VERTEX_FACTORY_TYPE(FOKSourceOracleVF);
public:
 FOKSourceOracleVF():FLocalVertexFactory(GMaxRHIFeatureLevel,"CrowdSourceMaterialOracle"){}
 static bool ShouldCompilePermutation(const FVertexFactoryShaderPermutationParameters& P){
  const auto& M=P.MaterialParameters;
  return M.MaterialDomain==MD_Surface&&!M.bIsDefaultMaterial&&!M.bIsSpecialEngineMaterial&&M.bIsUsedWithSkeletalMesh&&M.bIsUsedWithInstancedSkinnedMesh&&M.ShadingModels.HasShadingModel(MSM_Unlit);
 }
};
class FOKSourceOracleVFParameters:public FLocalVertexFactoryShaderParametersBase {
 DECLARE_TYPE_LAYOUT(FOKSourceOracleVFParameters,NonVirtual);
public:
 void GetElementShaderBindings(const FSceneInterface* Scene,const FSceneView* View,const FMeshMaterialShader* Shader,EVertexInputStreamType Stream,ERHIFeatureLevel::Type Level,const FVertexFactory* Factory,const FMeshBatchElement& Element,FMeshDrawSingleShaderBindings& Bindings,FVertexInputStreamArray& Streams)const{
  GetElementShaderBindingsBase(Scene,View,Shader,Stream,Level,Factory,Element,static_cast<const FLocalVertexFactory*>(Factory)->GetUniformBuffer(),Bindings,Streams);
 }
};
IMPLEMENT_TYPE_LAYOUT(FOKSourceOracleVFParameters);
IMPLEMENT_VERTEX_FACTORY_PARAMETER_TYPE(FOKSourceOracleVF,SF_Vertex,FOKSourceOracleVFParameters);
IMPLEMENT_VERTEX_FACTORY_TYPE(FOKSourceOracleVF,"/Engine/Private/LocalVertexFactory.ush",EVertexFactoryFlags::UsedWithMaterials);

class FOKSourceMaterialOracleView:public FSceneViewExtensionBase {
 const FMaterialRenderProxy* Material;
 FOKSourceOracleVF Factory;
 FStaticMeshVertexBuffers Buffers;
 FDynamicMeshIndexBuffer32 Indices;
 TUniformBufferRef<FPrimitiveUniformShaderParameters> Primitive;
 bool Requested=true,Initialized=false;
 const int32 Cases;
public:
 bool Submitted=false;
 TArray<TSharedPtr<FRHIGPUTextureReadback,ESPMode::ThreadSafe>> Readbacks;
 FOKSourceMaterialOracleView(const FAutoRegister& Auto,const FMaterialRenderProxy* InMaterial,bool IncludeRasterCases)
 :FSceneViewExtensionBase(Auto),Material(InMaterial),Cases(IncludeRasterCases?4:1){
  for(int32 I=0;I<Cases;++I)Readbacks.Add(MakeShared<FRHIGPUTextureReadback,ESPMode::ThreadSafe>(TEXT("CrowdSourceMaterialPixels")));
 }
 void PostRenderView_RenderThread(FRDGBuilder& G,FSceneView& View)override{
  if(!Requested)return;Requested=false;
  auto& R=G.RHICmdList;
  TArray<FDynamicMeshVertex> Vertices;
  const FVector4 Clip[]={FVector4(-1,1,.5,1),FVector4(-1,-3,.5,1),FVector4(3,1,.5,1)};
  const FVector2f UV[]={FVector2f(0,0),FVector2f(0,2),FVector2f(2,0)};
  for(int32 Layer=0;Layer<2;++Layer)for(int32 I=0;I<3;++I){FVector4 Position=Clip[I];Position.Z=Layer?.4:.8;const FVector4 World=View.ViewMatrices.GetInvViewProjectionMatrix().TransformFVector4(Position);
   if(!FMath::IsFinite(World.W)||FMath::Abs(World.W)<UE_SMALL_NUMBER)return;
   FDynamicMeshVertex V(FVector3f(FVector(World)/World.W));V.Color=FColor::White;V.SetTangents(FVector3f(1,0,0),FVector3f(0,1,0),FVector3f(0,0,1));
   for(int32 T=0;T<4;++T)V.TextureCoordinate[T]=UV[I];Vertices.Add(V);
  }
  Buffers.InitFromDynamicVertex(R,&Factory,Vertices,4);
  Indices.Indices={0,1,2,3,4,5};Indices.InitResource(R);Initialized=true;
  Primitive=TUniformBufferRef<FPrimitiveUniformShaderParameters>::CreateUniformBufferImmediate(GetIdentityPrimitiveParameters(),UniformBuffer_MultiFrame);
  Material->UpdateUniformExpressionCacheIfNeeded(View.GetFeatureLevel());
  FMeshBatch Mesh;Mesh.VertexFactory=&Factory;Mesh.MaterialRenderProxy=Material;Mesh.Type=PT_TriangleList;Mesh.bDisableBackfaceCulling=true;
  auto& Element=Mesh.Elements[0];Element.IndexBuffer=&Indices;Element.FirstIndex=0;Element.NumPrimitives=1;Element.MinVertexIndex=0;Element.MaxVertexIndex=2;Element.PrimitiveUniformBuffer=Primitive.GetReference();Element.PrimitiveIdMode=PrimID_ForceZero;
  Submitted=true;
  for(int32 Case=0;Case<Cases;++Case){
   auto* Color=G.CreateTexture(FRDGTextureDesc::Create2D(FIntPoint(32,8),PF_B8G8R8A8,FClearValueBinding::Transparent,TexCreate_RenderTargetable|TexCreate_ShaderResource),TEXT("Crowd.SourceMaterialColor"));
   auto* Depth=G.CreateTexture(FRDGTextureDesc::Create2D(FIntPoint(32,8),PF_DepthStencil,FClearValueBinding(0.f,0),TexCreate_DepthStencilTargetable),TEXT("Crowd.SourceMaterialPrivateDepth"));
   AddClearRenderTargetPass(G,Color,FLinearColor::Transparent);AddClearDepthStencilPass(G,Depth,true,0.f,true,0);
   auto Draw=[&](FMeshBatch Batch,const FOKSourceRasterState& State){FOKSourceRasterBinding Binding;FString Why;return OKPrepareSourceRasterState(State,Binding,Why)&&OKAddSourceMaterialMeshDiagnosticPass(G,View,Color,Depth,Batch,Binding);};
   FOKSourceRasterState Source;Source.DepthWrite=Case!=3;Source.Stencil=Case==1;Source.StencilPass=2;Source.StencilRef=2;
   Submitted=Draw(Mesh,Source)&&Submitted;
   if(Case==1){
    Source.DepthWrite=false;Source.StencilPass=0;Source.StencilCompare=3;
    Submitted=Draw(Mesh,Source)&&Submitted; // Equal 2 must pass, adding a second alpha layer.
    Source.StencilRef=3;Submitted=Draw(Mesh,Source)&&Submitted; // Equal 3 must reject.
   }else if(Case>=2){
    FMeshBatch Far=Mesh;Far.Elements[0].FirstIndex=3;Far.Elements[0].MinVertexIndex=3;Far.Elements[0].MaxVertexIndex=5;
    Source.DepthWrite=false;Submitted=Draw(Far,Source)&&Submitted;
   }
   if(Submitted)AddEnqueueCopyPass(G,Readbacks[Case].Get(),Color);
  }
 }
 void Release(){check(IsInRenderingThread());if(Initialized){Factory.ReleaseResource();Buffers.PositionVertexBuffer.ReleaseResource();Buffers.StaticMeshVertexBuffer.ReleaseResource();Buffers.ColorVertexBuffer.ReleaseResource();Indices.ReleaseResource();Initialized=false;}Primitive.SafeRelease();}
};

bool OKVerifySourceMaterialMeshPixels(UMaterialInterface* Material,TArray<FColor>& Pixels,FString& Error,bool IncludeRasterCases){
 check(IsInGameThread());
 if(!Material||!GEngine||!GEngine->GameViewport||!GEngine->GameViewport->Viewport){Error=TEXT("Source material oracle needs an active game viewport");return false;}
 auto Extension=FSceneViewExtensions::NewExtension<FOKSourceMaterialOracleView>(Material->GetRenderProxy(),IncludeRasterCases);
 GEngine->GameViewport->Viewport->Draw(false);FlushRenderingCommands();
 bool Ready=false;const double Start=FPlatformTime::Seconds();
 if(Extension->Submitted)while(FPlatformTime::Seconds()-Start<30){ENQUEUE_RENDER_COMMAND(SourceMaterialPixelPoll)([Extension,&Ready](FRHICommandListImmediate&){Ready=true;for(const auto& Readback:Extension->Readbacks)Ready=Readback->IsReady()&&Ready;});FlushRenderingCommands();if(Ready)break;FPlatformProcess::Sleep(.01f);}
 TArray<FColor> Actual;Actual.SetNum(4*Extension->Readbacks.Num());bool Layout=false;
 if(Ready){ENQUEUE_RENDER_COMMAND(SourceMaterialPixelRead)([&](FRHICommandListImmediate&){Layout=true;for(int32 Case=0;Case<Extension->Readbacks.Num();++Case){auto& Readback=Extension->Readbacks[Case];int32 Pitch=0,Height=0;const auto* Data=static_cast<const FColor*>(Readback->Lock(Pitch,&Height));const bool Valid=Data&&Pitch>=32&&Height>=8;Layout=Valid&&Layout;if(Valid)for(int32 I=0;I<4;++I)Actual[4*Case+I]=Data[4*Pitch+4+8*I];Readback->Unlock();}});FlushRenderingCommands();}
 ENQUEUE_RENDER_COMMAND(SourceMaterialOracleRelease)([Extension](FRHICommandListImmediate&){Extension->Release();});FlushRenderingCommands();
 if(!Ready||!Layout){Error=Extension->Submitted?TEXT("Source material pixel readback timeout/layout invalid"):TEXT("Source material oracle mesh was not submitted by the rendered view");return false;}
 Pixels=MoveTemp(Actual);Error.Reset();return true;
}
