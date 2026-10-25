#include "OKSourceMaterialMesh.h"
#include "MeshMaterialShader.h"
#include "MeshPassProcessor.inl"
#include "MaterialShared.h"
#include "Materials/MaterialRenderProxy.h"
#include "MeshBatch.h"
#include "VertexFactory.h"
#include "SceneView.h"
#include "SceneInterface.h"
#include "RenderGraphBuilder.h"
#include "InstanceCulling/InstanceCullingContext.h"
#include "SceneRendererInterface.h"
#include "SimpleMeshDrawCommandPass.h"
#include "PrimitiveSceneInfo.h"
#include "ScenePrivate.h"
#include "SceneRendering.h"

static bool OKShouldCompileSourceMesh(const FMeshMaterialShaderPermutationParameters& P){
 const auto& M=P.MaterialParameters;
 return M.MaterialDomain==MD_Surface&&!M.bIsSpecialEngineMaterial&&!M.bIsDefaultMaterial&&M.bIsUsedWithSkeletalMesh&&M.bIsUsedWithInstancedSkinnedMesh&&M.ShadingModels.HasShadingModel(MSM_Unlit);
}

class FOKSourceMaterialVS:public FMeshMaterialShader {
 DECLARE_SHADER_TYPE(FOKSourceMaterialVS,MeshMaterial);
public:
 FOKSourceMaterialVS()=default;
 FOKSourceMaterialVS(const ShaderMetaType::CompiledShaderInitializerType& I):FMeshMaterialShader(I){}
 static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& P){return OKShouldCompileSourceMesh(P);}
};
class FOKSourceMaterialPS:public FMeshMaterialShader {
 DECLARE_SHADER_TYPE(FOKSourceMaterialPS,MeshMaterial);
public:
 FOKSourceMaterialPS()=default;
 FOKSourceMaterialPS(const ShaderMetaType::CompiledShaderInitializerType& I):FMeshMaterialShader(I){}
 static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& P){return OKShouldCompileSourceMesh(P);}
};
IMPLEMENT_MATERIAL_SHADER_TYPE(,FOKSourceMaterialVS,TEXT("/Plugin/KKCrowdGpuPacking/SourceMaterialMesh.usf"),TEXT("MainVS"),SF_Vertex);
IMPLEMENT_MATERIAL_SHADER_TYPE(,FOKSourceMaterialPS,TEXT("/Plugin/KKCrowdGpuPacking/SourceMaterialMesh.usf"),TEXT("MainPS"),SF_Pixel);


bool OKSourceMaterialMeshShadersAvailable(const FMaterial& M,const FVertexFactoryType* VF){
 FMaterialShaderTypes Types;Types.AddShaderType<FOKSourceMaterialVS>();Types.AddShaderType<FOKSourceMaterialPS>();FMaterialShaders Shaders;return VF&&M.TryGetShaders(Types,VF,Shaders);
}
class FOKSourceMeshProcessor:public FMeshPassProcessor {
 FMeshPassProcessorRenderState State;
public:
 FOKSourceMeshProcessor(const FSceneView& View,FMeshPassDrawListContext* Context,const FOKSourceRasterBinding& B,bool Cached=false)
 :FMeshPassProcessor(EMeshPass::Num,View.Family->Scene?View.Family->Scene->GetRenderScene():nullptr,View.GetFeatureLevel(),Cached?nullptr:&View,Context){
  State.SetBlendState(RHICreateBlendState(B.Blend));State.SetDepthStencilState(RHICreateDepthStencilState(B.DepthStencil));State.SetStencilRef(B.StencilRef);State.SetDepthStencilAccess(FExclusiveDepthStencil::DepthWrite_StencilWrite);
 }
 virtual void AddMeshBatch(const FMeshBatch& Mesh,uint64 Mask,const FPrimitiveSceneProxy* Proxy,int32 StaticId=-1)override{
  const FMaterialRenderProxy* RenderProxy=Mesh.MaterialRenderProxy;if(!RenderProxy||!Mesh.VertexFactory)return;
  const FMaterial* Material=RenderProxy->GetMaterialNoFallback(FeatureLevel);if(!Material)return;
  FMaterialShaderTypes Types;Types.AddShaderType<FOKSourceMaterialVS>();Types.AddShaderType<FOKSourceMaterialPS>();FMaterialShaders Found;
  if(!Material->TryGetShaders(Types,Mesh.VertexFactory->GetType(),Found))return;
  TMeshProcessorShaders<FOKSourceMaterialVS,FOKSourceMaterialPS> Shaders;Found.TryGetVertexShader(Shaders.VertexShader);Found.TryGetPixelShader(Shaders.PixelShader);
  FMeshMaterialShaderElementData Data;Data.InitializeMeshMaterialData(ViewIfDynamicMeshCommand,Proxy,Mesh,StaticId,false);
  BuildMeshDrawCommands(Mesh,Mask,Proxy,*RenderProxy,*Material,State,Shaders,ComputeMeshFillMode(*Material,ComputeMeshOverrideSettings(Mesh)),ComputeMeshCullMode(*Material,ComputeMeshOverrideSettings(Mesh)),FMeshDrawCommandSortKey::Default,EMeshPassFeatures::Default,Data);
 }
};
BEGIN_SHADER_PARAMETER_STRUCT(FOKSourceMeshPassParameters,)
 SHADER_PARAMETER_STRUCT_INCLUDE(FViewShaderParameters,View)
 SHADER_PARAMETER_STRUCT_INCLUDE(FInstanceCullingDrawParams,InstanceCullingDrawParams)
 RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()
bool OKAddSourceMaterialScenePass(FRDGBuilder& G,const FSceneView& View,FRDGTexture* Color,FRDGTexture* Depth,const FMeshBatch& Mesh,const FPrimitiveSceneProxy* Proxy,const FOKSourceRasterBinding& B){
 check(IsInRenderingThread());
 if(!Color||!Depth||Color->Desc.Extent!=Depth->Desc.Extent||!Mesh.MaterialRenderProxy||!Mesh.VertexFactory||Mesh.Elements.IsEmpty())return false;
 const auto* Material=Mesh.MaterialRenderProxy->GetMaterialNoFallback(View.GetFeatureLevel());
 if(!Material||!OKSourceMaterialMeshShadersAvailable(*Material,Mesh.VertexFactory->GetType()))return false;
 auto* Owned=G.AllocObject<FMeshBatch>(Mesh);auto* P=G.AllocParameters<FOKSourceMeshPassParameters>();
 P->InstanceCullingDrawParams.InstanceCulling=FInstanceCullingContext::CreateDummyInstanceCullingUniformBuffer(G);if(Proxy)P->InstanceCullingDrawParams.Scene=GetSceneUniformBufferRef(G,View);
 P->View.View=View.ViewUniformBuffer;P->View.InstancedView=View.GetInstancedViewUniformBuffer();
 P->RenderTargets[0]=FRenderTargetBinding(Color,ERenderTargetLoadAction::ELoad);
 P->RenderTargets.DepthStencil=FDepthStencilBinding(Depth,ERenderTargetLoadAction::ELoad,ERenderTargetLoadAction::ELoad,FExclusiveDepthStencil::DepthWrite_StencilWrite);
 AddDrawDynamicMeshPass(G,RDG_EVENT_NAME("Crowd.SourceMaterialDiagnostic"),P,View,FIntRect(FIntPoint::ZeroValue,Color->Desc.Extent),[Owned,B,&View,Proxy](FDynamicPassMeshDrawListContext* Context){FOKSourceMeshProcessor Processor(View,Context,B);Processor.AddMeshBatch(*Owned,~0ull,Proxy);},true);
 return true;
}

bool OKAddSourceMaterialMeshDiagnosticPass(FRDGBuilder& G,const FSceneView& V,FRDGTexture* C,FRDGTexture* D,const FMeshBatch& M,const FOKSourceRasterBinding& B){return OKAddSourceMaterialScenePass(G,V,C,D,M,nullptr,B);}

// Retain only view-independent command bindings, not one-frame culling data.
// Re-finalization below refreshes scene IDs after primitive compaction, without
// evaluating material/VF shader bindings again.
class FOKSourceMaterialCommandCache {
public:
 struct FCommand {FMeshDrawCommand Draw;TUniquePtr<FGraphicsMinimalPipelineStateInitializer> Pipeline;int32 Element=0;ERasterizerFillMode Fill;ERasterizerCullMode Cull;FMeshDrawCommandSortKey Sort;EFVisibleMeshDrawCommandFlags Flags;};
 struct FEntry {const FPrimitiveSceneProxy* Proxy=nullptr;const FVertexFactory* VF=nullptr;const FMaterialRenderProxy* Material=nullptr;const FMaterial* ShaderMaterial=nullptr;int32 Serial=0;const FIndexBuffer* Indices=nullptr;uint32 First=0,Triangles=0;TArray<FCommand> Commands;uint64 UsedFrame=0;};
 TMap<const FMeshBatch*,FEntry> Entries;uint64 Builds=0,Reuses=0;FOKSourceRasterBinding Binding;bool HasBinding=false;
};
class FOKSourceCacheContext:public FMeshPassDrawListContext {
 FOKSourceMaterialCommandCache::FEntry& Entry;
public:
 explicit FOKSourceCacheContext(FOKSourceMaterialCommandCache::FEntry& E):Entry(E){}
 FMeshDrawCommand& AddCommand(FMeshDrawCommand& Initializer,uint32)override {auto& C=Entry.Commands.AddDefaulted_GetRef();C.Draw=Initializer;return C.Draw;}
 void FinalizeCommand(const FMeshBatch&,int32 Element,const FMeshDrawCommandPrimitiveIdInfo&,ERasterizerFillMode Fill,ERasterizerCullMode Cull,FMeshDrawCommandSortKey Sort,EFVisibleMeshDrawCommandFlags Flags,const FGraphicsMinimalPipelineStateInitializer& Pipeline,const FMeshProcessorShaders*,FMeshDrawCommand&)override {
  auto& C=Entry.Commands.Last();C.Element=Element;C.Fill=Fill;C.Cull=Cull;C.Sort=Sort;C.Flags=Flags;C.Pipeline=MakeUnique<FGraphicsMinimalPipelineStateInitializer>(Pipeline);
 }
};
TSharedRef<FOKSourceMaterialCommandCache,ESPMode::ThreadSafe> OKCreateSourceMaterialCommandCache(){return MakeShared<FOKSourceMaterialCommandCache,ESPMode::ThreadSafe>();}
void OKSourceMaterialCommandCacheStats(const FOKSourceMaterialCommandCache& C,uint64& Builds,uint64& Reuses){check(IsInRenderingThread());Builds=C.Builds;Reuses=C.Reuses;}
uint32 OKAddCachedSourceMaterialScenePass(FRDGBuilder& G,const FSceneView& View,FRDGTexture* Color,FRDGTexture* Depth,TConstArrayView<FOKSourceMaterialSceneDraw> Draws,const FOKSourceRasterBinding& Binding,TSharedRef<FOKSourceMaterialCommandCache,ESPMode::ThreadSafe> Cache){
 check(IsInRenderingThread());if(!Color||!Depth||!View.bIsViewInfo||Draws.IsEmpty())return 0;
 if(!Cache->HasBinding||!(Cache->Binding.Blend==Binding.Blend)||!(Cache->Binding.DepthStencil==Binding.DepthStencil)||Cache->Binding.StencilRef!=Binding.StencilRef){Cache->Entries.Reset();Cache->Binding=Binding;Cache->HasBinding=true;}
 auto* Pass=G.AllocObject<FSimpleMeshDrawCommandPass>(View,nullptr);
 auto* Context=Pass->GetDynamicPassMeshDrawListContext();uint32 Submitted=0;TSet<const FMeshBatch*> Live;
 for(const auto& D:Draws){if(!D.Mesh||!D.Proxy||!D.Proxy->GetPrimitiveSceneInfo())continue;const auto& Mesh=*D.Mesh;
  if(!Mesh.MaterialRenderProxy||!Mesh.VertexFactory||Mesh.Elements.IsEmpty())continue;
  const auto* Material=Mesh.MaterialRenderProxy->GetMaterialNoFallback(View.GetFeatureLevel());if(!Material||!OKSourceMaterialMeshShadersAvailable(*Material,Mesh.VertexFactory->GetType()))continue;
  // This scene pass uses one invariant depth state for every retained entry.
  // Native material changes invalidate bindings via the expression-cache serial.
  Live.Add(D.Mesh);auto& E=Cache->Entries.FindOrAdd(D.Mesh);const auto& First=Mesh.Elements[0];const int32 Serial=Mesh.MaterialRenderProxy->GetExpressionCacheSerialNumber();
  if(E.Proxy!=D.Proxy||E.VF!=Mesh.VertexFactory||E.Material!=Mesh.MaterialRenderProxy||E.ShaderMaterial!=Material||E.Serial!=Serial||E.Indices!=First.IndexBuffer||E.First!=First.FirstIndex||E.Triangles!=First.NumPrimitives||E.Commands.IsEmpty()){
   E={};E.Proxy=D.Proxy;E.VF=Mesh.VertexFactory;E.Material=Mesh.MaterialRenderProxy;E.ShaderMaterial=Material;E.Serial=Serial;E.Indices=First.IndexBuffer;E.First=First.FirstIndex;E.Triangles=First.NumPrimitives;
   FOKSourceCacheContext Cached(E);FOKSourceMeshProcessor Processor(View,&Cached,Binding,true);Processor.AddMeshBatch(Mesh,~0ull,D.Proxy);++Cache->Builds;
  }else ++Cache->Reuses;
  E.UsedFrame=GFrameCounterRenderThread;
  for(auto& C:E.Commands){auto& Copy=Context->AddCommand(C.Draw,Mesh.Elements.Num());Context->FinalizeCommand(Mesh,C.Element,D.Proxy->GetPrimitiveSceneInfo()->GetMDCIdInfo(),C.Fill,C.Cull,C.Sort,C.Flags,*C.Pipeline,nullptr,Copy);++Submitted;}
 }
 // Entries no view has drawn for a while go (missing or recreated primitives, a heroine gone). Not the ones this view
 // skipped: the portrait capture (one heroine, 30 Hz) and the main view share the cache, and evicting per view rebuilt
 // every heroine's commands for the main view after each capture (6-7 ms on the render thread). A reused address is
 // caught by the fields compared above, so an old entry is never drawn for a new primitive.
 for(auto It=Cache->Entries.CreateIterator();It;++It)if(!Live.Contains(It.Key())&&GFrameCounterRenderThread-It.Value().UsedFrame>600)It.RemoveCurrent();
 if(!Submitted)return 0;
 auto* P=G.AllocParameters<FOKSourceMeshPassParameters>();P->View.View=View.ViewUniformBuffer;P->View.InstancedView=View.GetInstancedViewUniformBuffer();P->InstanceCullingDrawParams.Scene=GetSceneUniformBufferRef(G,View);
 P->RenderTargets[0]=FRenderTargetBinding(Color,ERenderTargetLoadAction::ELoad);P->RenderTargets.DepthStencil=FDepthStencilBinding(Depth,ERenderTargetLoadAction::ELoad,ERenderTargetLoadAction::ELoad,FExclusiveDepthStencil::DepthWrite_StencilWrite);
 Pass->BuildRenderingCommands(G,View,*View.Family->Scene->GetRenderScene(),P->InstanceCullingDrawParams);
 // BuildRenderingCommands clears the draw-parameter struct before filling it.
 // A standalone context has no manager to restore the scene uniform itself.
 P->InstanceCullingDrawParams.Scene=GetSceneUniformBufferRef(G,View);
 const FIntRect Rect=static_cast<const FViewInfo&>(View).ViewRect;
 G.AddPass(RDG_EVENT_NAME("Crowd.CachedSourceDepth"),P,ERDGPassFlags::Raster,[Pass,P,Rect,Cache](FRHICommandList& Cmd){Cmd.SetViewport(Rect.Min.X,Rect.Min.Y,0,Rect.Max.X,Rect.Max.Y,1);Pass->SubmitDraw(Cmd,P->InstanceCullingDrawParams);});
 return Submitted;
}
