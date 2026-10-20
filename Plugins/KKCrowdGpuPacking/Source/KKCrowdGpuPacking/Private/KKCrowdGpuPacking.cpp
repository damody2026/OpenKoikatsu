#include "OKGpuBonePacking.h"
#include "OKGpuResidencyBudget.h"
#include "Misc/CoreDelegates.h"
#include "RenderingThread.h"
#include "Modules/ModuleManager.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "GlobalShader.h"
#include "ShaderParameterStruct.h"
#include "RenderGraphUtils.h"
class FOKGpuBonePackingCS : public FGlobalShader {
 DECLARE_GLOBAL_SHADER(FOKGpuBonePackingCS);
 SHADER_USE_PARAMETER_STRUCT(FOKGpuBonePackingCS,FGlobalShader);
 BEGIN_SHADER_PARAMETER_STRUCT(FParameters,)
  SHADER_PARAMETER_RDG_BUFFER_SRV(ByteAddressBuffer,RawMatrices)
  SHADER_PARAMETER_RDG_BUFFER_SRV(ByteAddressBuffer,WorkItems)
  SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>,BoneMaps)
  SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint4>,OutputTransforms)
  SHADER_PARAMETER(uint32,WorkCount)
 END_SHADER_PARAMETER_STRUCT()
 static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& P){return IsFeatureLevelSupported(P.Platform,ERHIFeatureLevel::SM5);}
};
IMPLEMENT_GLOBAL_SHADER(FOKGpuBonePackingCS,"/Plugin/KKCrowdGpuPacking/PackBoneMatrices.usf","MainCS",SF_Compute);
bool OKGpuBonePackingAvailable(){auto* Map=GetGlobalShaderMap(GMaxRHIFeatureLevel);return Map&&Map->HasShader(&FOKGpuBonePackingCS::GetStaticType(),0);}
void OKAddGpuBonePackingPass(FRDGBuilder& G,FRDGBufferRef Raw,FRDGBufferRef Work,FRDGBufferSRVRef Maps,FRDGBufferRef Output,uint32 Count){
 auto* P=G.AllocParameters<FOKGpuBonePackingCS::FParameters>();
 P->RawMatrices=G.CreateSRV(Raw);P->WorkItems=G.CreateSRV(Work);P->BoneMaps=Maps;P->OutputTransforms=G.CreateUAV(FRDGBufferUAVDesc(Output,PF_R32G32B32A32_UINT));P->WorkCount=Count;
 TShaderMapRef<FOKGpuBonePackingCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
 FComputeShaderUtils::AddPass(G,RDG_EVENT_NAME("Crowd.GpuBoneMatrixPacking"),Shader,P,FIntVector(Count,1,1));
}
namespace {TSharedPtr<FOKGpuResidencyBudget,ESPMode::ThreadSafe> CrowdBudget;}
TSharedRef<FOKGpuResidencyBudget,ESPMode::ThreadSafe> OKGetCrowdGpuResidencyBudget(){check(IsInGameThread());check(CrowdBudget);return CrowdBudget.ToSharedRef();}
class FKKCrowdGpuPackingModule : public IModuleInterface {
 FDelegateHandle CollectHandle;
 void StartupModule() override {CrowdBudget=MakeShared<FOKGpuResidencyBudget,ESPMode::ThreadSafe>(4ull*1024*1024*1024);CollectHandle=FCoreDelegates::OnEndFrameRT.AddLambda([Budget=CrowdBudget](){Budget->Collect();});const auto Plugin=IPluginManager::Get().FindPlugin(TEXT("KKCrowdGpuPacking"));check(Plugin);AddShaderSourceDirectoryMapping(TEXT("/Plugin/KKCrowdGpuPacking"),FPaths::Combine(Plugin->GetBaseDir(),TEXT("Shaders")));}
 void ShutdownModule() override {auto Budget=MoveTemp(CrowdBudget);const auto Handle=CollectHandle;if(Budget&&IsInRenderingThread()){FCoreDelegates::OnEndFrameRT.Remove(Handle);Budget->Collect();}else if(Budget){ENQUEUE_RENDER_COMMAND(ReleaseCrowdBudget)([Budget=MoveTemp(Budget),Handle](FRHICommandListImmediate&){FCoreDelegates::OnEndFrameRT.Remove(Handle);Budget->Collect();});FlushRenderingCommands();}}
};
IMPLEMENT_MODULE(FKKCrowdGpuPackingModule,KKCrowdGpuPacking)
