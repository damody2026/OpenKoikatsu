#include "OKGpuRig.h"
#include "GlobalShader.h"
#include "ShaderParameterStruct.h"
#include "RenderGraphUtils.h"
#include "RenderingThread.h"
#include "Misc/CommandLine.h"

// A cooked game verifies every global shader it compiles in at startup and stops when one is missing from its shader
// cache, which comes from the base pak and the StreetDisplay patch. So the rig's two shaders count only in the editor
// and the cook, and in a game started with -OKGpuRig (the launchers pass it with gpu_rig): any other stage starts
// whatever its patch holds, and the scene falls back to the solved banks when they are absent (OKGpuRigAvailable).
static bool OKGpuRigCompiled(const FGlobalShaderPermutationParameters& P){
 if(!IsFeatureLevelSupported(P.Platform,ERHIFeatureLevel::SM6))return false;
#if WITH_EDITOR
 return true;
#else
 static const bool Requested=FParse::Param(FCommandLine::Get(),TEXT("OKGpuRig"));return Requested;
#endif
}

class FOKGpuRigSkeletonCS:public FGlobalShader {
 DECLARE_GLOBAL_SHADER(FOKGpuRigSkeletonCS);
 SHADER_USE_PARAMETER_STRUCT(FOKGpuRigSkeletonCS,FGlobalShader);
 BEGIN_SHADER_PARAMETER_STRUCT(FParameters,)
  SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint4>,RigActors)
  SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>,RigBones)
  SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint4>,RigLists)
  SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>,RigAbm)
  SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>,RigNamed)
  SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint4>,RigClipSlots)
  SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>,RigClipRates)
  SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint4>,RigChannels)
  SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>,RigSamples)
  SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint4>,RigSelect)
  SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>,RigTimes)
  SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>,RigLook)
  SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float4>,Locals)
  SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float4>,LocalsB)
  SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float4>,Worlds)
  SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float4>,RigRots)
  SHADER_PARAMETER(uint32,ActorCount)
  SHADER_PARAMETER(uint32,ClipSlotsPerActor)
 END_SHADER_PARAMETER_STRUCT()
 // The ancestor stacks are dynamically indexed (DXC/SM6), as IndependentPose.usf.
 static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& P){return OKGpuRigCompiled(P);}
};
IMPLEMENT_GLOBAL_SHADER(FOKGpuRigSkeletonCS,"/Plugin/KKCrowdGpuPacking/GpuRig.usf","SkeletonCS",SF_Compute);

class FOKGpuRigTaskCS:public FGlobalShader {
 DECLARE_GLOBAL_SHADER(FOKGpuRigTaskCS);
 SHADER_USE_PARAMETER_STRUCT(FOKGpuRigTaskCS,FGlobalShader);
 BEGIN_SHADER_PARAMETER_STRUCT(FParameters,)
  SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint4>,RigTasks)
  SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>,RigTerms)
  SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>,TaskWorlds)
  SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>,RigBones) // a native term's parents and scale signs
  SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float4>,OutputRows)
  SHADER_PARAMETER(uint32,TaskCount)
 END_SHADER_PARAMETER_STRUCT()
 static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& P){return OKGpuRigCompiled(P);}
};
IMPLEMENT_GLOBAL_SHADER(FOKGpuRigTaskCS,"/Plugin/KKCrowdGpuPacking/GpuRig.usf","TaskCS",SF_Compute);

class FOKGpuRigPhysicsCS:public FGlobalShader {
 DECLARE_GLOBAL_SHADER(FOKGpuRigPhysicsCS);
 SHADER_USE_PARAMETER_STRUCT(FOKGpuRigPhysicsCS,FGlobalShader);
 BEGIN_SHADER_PARAMETER_STRUCT(FParameters,)
  SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>,RigBones)
  SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint4>,PhysSets)
  SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>,PhysNodes)
  SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint4>,PhysChains)
  SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>,PhysChainValues)
  SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>,PhysParticles)
  SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>,PhysColliders)
  SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>,PhysFollow)
  SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float4>,Locals)
  SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float4>,Worlds)
  SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float4>,RigRots)
  SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float4>,NodeLocals)
  SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float4>,NodeRots)
  SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float4>,PhysState)
  SHADER_PARAMETER(uint32,SetCount)
  SHADER_PARAMETER(uint32,ClothesPass)
  SHADER_PARAMETER(uint32,Stage)
  SHADER_PARAMETER(uint32,ChainCount)
  SHADER_PARAMETER(uint32,BoneTotal)
  SHADER_PARAMETER(uint32,ParticleTotal)
  SHADER_PARAMETER(uint32,ChainTotal)
  SHADER_PARAMETER(float,DeltaTime)
  SHADER_PARAMETER(uint32,ResetAll)
  SHADER_PARAMETER(uint32,SettleSteps)
 END_SHADER_PARAMETER_STRUCT()
 static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& P){return OKGpuRigCompiled(P);}
};
IMPLEMENT_GLOBAL_SHADER(FOKGpuRigPhysicsCS,"/Plugin/KKCrowdGpuPacking/GpuRig.usf","PhysicsCS",SF_Compute);

bool OKGpuRigAvailable(){
 check(IsInRenderingThread());if(!IsFeatureLevelSupported(GMaxRHIShaderPlatform,ERHIFeatureLevel::SM6))return false;const auto* Map=GetGlobalShaderMap(GMaxRHIFeatureLevel);
 return Map&&Map->HasShader(&FOKGpuRigSkeletonCS::GetStaticType(),0)&&Map->HasShader(&FOKGpuRigTaskCS::GetStaticType(),0)&&Map->HasShader(&FOKGpuRigPhysicsCS::GetStaticType(),0);
}

bool FOKGpuRigBatch::Validate(FString& Error) const {
 auto Fail=[&](const TCHAR* Why){Error=Why;return false;};
 if(!ActorCount||Actors.Num()!=int32(ActorCount)*4||Named.Num()!=int32(ActorCount)*66||!ClipSlotsPerActor||ClipSlots.Num()!=int32(ActorCount*ClipSlotsPerActor)||ClipRates.Num()!=ClipSlots.Num())return Fail(TEXT("GPU rig actor tables malformed"));
 if(Bones.Num()!=int32(BoneCount)*3||Abm.Num()%3||Terms.Num()%4||Tasks.IsEmpty())return Fail(TEXT("GPU rig arrays malformed"));
 for(uint32 A=0;A<ActorCount;++A){const auto& H=Actors[A*4];if(uint64(H.X)+H.Y>BoneCount)return Fail(TEXT("GPU rig actor bones out of range"));}
 for(uint32 B=0;B<BoneCount;++B){const uint32 Parent=reinterpret_cast<const uint32&>(Bones[B*3].W);if(Parent!=MAX_uint32&&Parent>=B)return Fail(TEXT("GPU rig parent must precede the bone"));}
 for(const uint32 N:Named)if(N!=MAX_uint32&&N>=BoneCount)return Fail(TEXT("GPU rig named bone out of range"));
 for(const auto& S:ClipSlots)if(uint64(S.X)+S.Y>uint64(Channels.Num()))return Fail(TEXT("GPU rig clip slot channels out of range"));
 for(const auto& C:Channels)if((C.X!=MAX_uint32&&C.X>=BoneCount))return Fail(TEXT("GPU rig channel bone out of range"));
 for(const auto& T:Tasks)if(uint64(T.X)+T.Y>uint64(Terms.Num()/4)||T.Y>4)return Fail(TEXT("GPU rig task terms out of range"));
 for(int32 T=0;T<Terms.Num();T+=4){const uint32 Node=reinterpret_cast<const uint32&>(Terms[T].X);if(Node!=MAX_uint32&&Node>=BoneCount+NodeCount)return Fail(TEXT("GPU rig term bone out of range"));}
 // Physics: every reference inside the bones or the nodes, every range inside its table.
 const uint32 Chains=PhysChains.Num()/2,Particles=PhysParticles.Num()/8,Colliders=PhysColliders.Num()/2;
 if(PhysNodes.Num()!=int32(NodeCount)*8||PhysSets.Num()%2||PhysChains.Num()%2||PhysChainValues.Num()!=int32(Chains)*4||PhysParticles.Num()%8||PhysColliders.Num()%2)return Fail(TEXT("GPU rig physics tables malformed"));
 auto RefOk=[&](uint32 R){return R==MAX_uint32||((R&OKGpuRigPhysicsNode)?(R&~OKGpuRigPhysicsNode)<NodeCount:R<BoneCount);};
 for(uint32 N=0;N<NodeCount;++N){const uint32 Kind=reinterpret_cast<const uint32&>(PhysNodes[N*8].W),Parent=reinterpret_cast<const uint32&>(PhysNodes[N*8+2].W),Alias=reinterpret_cast<const uint32&>(PhysNodes[N*8+7].X);
  const uint32 From=reinterpret_cast<const uint32&>(PhysNodes[N*8+7].Y);
  if(Kind>2||!RefOk(Parent)||((Parent&OKGpuRigPhysicsNode)&&Parent!=MAX_uint32&&(Parent&~OKGpuRigPhysicsNode)>=N)||(Kind==0&&Alias>=BoneCount)||(From!=MAX_uint32&&From>=BoneCount))return Fail(TEXT("GPU rig physics node malformed"));}
 for(uint32 C=0;C<Chains;++C){const auto& H=PhysChains[C*2];const auto& K=PhysChains[C*2+1];if(H.X>=NodeCount||H.Y>=NodeCount||uint64(K.X)+K.Y>Particles||uint64(K.Z)+K.W>Colliders)return Fail(TEXT("GPU rig physics chain out of range"));}
 for(uint32 P=0;P<Particles;++P){const uint32 Node=reinterpret_cast<const uint32&>(PhysParticles[P*8].X),Parent=reinterpret_cast<const uint32&>(PhysParticles[P*8].Y);if((Node!=MAX_uint32&&Node>=NodeCount)||(Parent!=MAX_uint32&&Parent>=P))return Fail(TEXT("GPU rig physics particle out of range"));}
 for(uint32 C=0;C<Colliders;++C)if(!RefOk(reinterpret_cast<const uint32&>(PhysColliders[C*2+1].W)))return Fail(TEXT("GPU rig physics collider out of range"));
 for(int32 S=0;S<PhysSets.Num();S+=2){const auto& A=PhysSets[S];const auto& B=PhysSets[S+1];if(uint64(A.Y)+A.Z>NodeCount||uint64(A.W)+B.X>Chains||uint64(B.Y)+B.Z>uint64(PhysFollow.Num()))return Fail(TEXT("GPU rig physics set out of range"));}
 for(const uint32 F:PhysFollow)if(F>=BoneCount)return Fail(TEXT("GPU rig physics follow out of range"));
 return true;
}

template<typename T> static TRefCountPtr<FRDGPooledBuffer> OKGpuRigUpload(FRDGBuilder& G,const TCHAR* Name,const TArray<T>& Data){
 TArray<T> Safe=Data;if(Safe.IsEmpty())Safe.AddZeroed();return G.ConvertToExternalBuffer(CreateStructuredBuffer(G,Name,Safe));
}

bool FOKGpuRig::Initialize(FRDGBuilder& G,TSharedRef<const FOKGpuRigBatch,ESPMode::ThreadSafe> Batch,FString& Error){
 check(IsInRenderingThread());if(!OKGpuRigAvailable()){Error=TEXT("GPU rig shaders unavailable");return false;}
 if(!Batch->Validate(Error))return false;
 const auto& B=*Batch;
 Actors=OKGpuRigUpload(G,TEXT("GpuRig.Actors"),B.Actors);Bones=OKGpuRigUpload(G,TEXT("GpuRig.Bones"),B.Bones);Lists=OKGpuRigUpload(G,TEXT("GpuRig.Lists"),B.Lists);
 Abm=OKGpuRigUpload(G,TEXT("GpuRig.Abm"),B.Abm);Named=OKGpuRigUpload(G,TEXT("GpuRig.Named"),B.Named);ClipSlots=OKGpuRigUpload(G,TEXT("GpuRig.ClipSlots"),B.ClipSlots);
 ClipRates=OKGpuRigUpload(G,TEXT("GpuRig.ClipRates"),B.ClipRates);Channels=OKGpuRigUpload(G,TEXT("GpuRig.Channels"),B.Channels);Samples=OKGpuRigUpload(G,TEXT("GpuRig.Samples"),B.Samples);
 Tasks=OKGpuRigUpload(G,TEXT("GpuRig.Tasks"),B.Tasks);Terms=OKGpuRigUpload(G,TEXT("GpuRig.Terms"),B.Terms);
 auto Scratch=[&](const TCHAR* Name,uint32 Rows){return G.ConvertToExternalBuffer(G.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(FVector4f),FMath::Max<uint32>(1,Rows)),Name));};
 RigRots=Scratch(TEXT("GpuRig.RigRots"),B.BoneCount);Locals=Scratch(TEXT("GpuRig.Locals"),B.BoneCount*3);LocalsB=Scratch(TEXT("GpuRig.LocalsB"),B.BoneCount*3);Worlds=Scratch(TEXT("GpuRig.Worlds"),(B.BoneCount+B.NodeCount)*3);
 PhysSets=OKGpuRigUpload(G,TEXT("GpuRig.PhysSets"),B.PhysSets);PhysNodes=OKGpuRigUpload(G,TEXT("GpuRig.PhysNodes"),B.PhysNodes);
 PhysChains=OKGpuRigUpload(G,TEXT("GpuRig.PhysChains"),B.PhysChains);PhysChainValues=OKGpuRigUpload(G,TEXT("GpuRig.PhysChainValues"),B.PhysChainValues);
 PhysParticles=OKGpuRigUpload(G,TEXT("GpuRig.PhysParticles"),B.PhysParticles);PhysColliders=OKGpuRigUpload(G,TEXT("GpuRig.PhysColliders"),B.PhysColliders);
 PhysFollow=OKGpuRigUpload(G,TEXT("GpuRig.PhysFollow"),B.PhysFollow);
 NodeLocals=Scratch(TEXT("GpuRig.NodeLocals"),B.NodeCount*3);NodeRots=Scratch(TEXT("GpuRig.NodeRots"),B.NodeCount);
 // Particles (pos, prev), chains (time, component position), sets (ready): persistent, cleared once.
 PhysState=Scratch(TEXT("GpuRig.PhysState"),B.PhysParticles.Num()/8*2+B.PhysChains.Num()/2+B.PhysSets.Num()/2);PhysicsCleared=false;
 Static=Batch;HasFrame=false;Current.SafeRelease();return true;
}

bool FOKGpuRig::Evaluate(FRDGBuilder& G,TConstArrayView<FOKGpuPosePlayback> Playback,bool Reset,float DeltaSeconds,FOKResidentPoseFrame& Out,FString& Error){
 check(IsInRenderingThread());Out={};
 if(!Static){Error=TEXT("GPU rig not initialized");return false;}
 const auto& B=*Static;if(Playback.Num()!=int32(B.ActorCount)){Error=TEXT("GPU rig playback count differs");return false;}
 TArray<FUintVector4> Select;TArray<FVector4f> Times,Look;Select.Reserve(Playback.Num());Times.Reserve(Playback.Num());Look.Reserve(Playback.Num()*2);
 for(const auto& P:Playback){
  if(P.ClipA>=B.ClipSlotsPerActor||P.ClipB>=B.ClipSlotsPerActor){Error=TEXT("GPU rig clip slot outside the actor's slots");return false;}
  for(int32 C=0;C<4;++C)if(!FMath::IsFinite(P.Times[C])){Error=TEXT("GPU rig nonfinite playback");return false;}
  Select.Add(FUintVector4(P.ClipA,P.ClipB,0,0));Times.Add(FVector4f(P.Times.X,P.Times.Y,FMath::Clamp(P.Times.Z,0.f,1.f),0));
  // The look's bones are the actor's own (BuildRigBatch); these say only whether and how far she turns.
  Look.Add(P.LookNeck!=MAX_uint32?P.NeckRotation:FVector4f(0,0,0,1));Look.Add(P.LookHead!=MAX_uint32?P.HeadRotation:FVector4f(0,0,0,1));
 }
 auto* LocalsUav=G.CreateUAV(G.RegisterExternalBuffer(Locals));auto* LocalsBUav=G.CreateUAV(G.RegisterExternalBuffer(LocalsB));
 FRDGBufferRef WorldsBuffer=G.RegisterExternalBuffer(Worlds);
 {
  auto* P=G.AllocParameters<FOKGpuRigSkeletonCS::FParameters>();
  P->RigActors=G.CreateSRV(G.RegisterExternalBuffer(Actors));P->RigBones=G.CreateSRV(G.RegisterExternalBuffer(Bones));P->RigLists=G.CreateSRV(G.RegisterExternalBuffer(Lists));
  P->RigAbm=G.CreateSRV(G.RegisterExternalBuffer(Abm));P->RigNamed=G.CreateSRV(G.RegisterExternalBuffer(Named));P->RigClipSlots=G.CreateSRV(G.RegisterExternalBuffer(ClipSlots));
  P->RigClipRates=G.CreateSRV(G.RegisterExternalBuffer(ClipRates));P->RigChannels=G.CreateSRV(G.RegisterExternalBuffer(Channels));P->RigSamples=G.CreateSRV(G.RegisterExternalBuffer(Samples));
  P->RigSelect=G.CreateSRV(CreateStructuredBuffer(G,TEXT("GpuRig.Select"),Select));P->RigTimes=G.CreateSRV(CreateStructuredBuffer(G,TEXT("GpuRig.Times"),Times));P->RigLook=G.CreateSRV(CreateStructuredBuffer(G,TEXT("GpuRig.Look"),Look));
  P->Locals=LocalsUav;P->LocalsB=LocalsBUav;P->Worlds=G.CreateUAV(WorldsBuffer);P->RigRots=G.CreateUAV(G.RegisterExternalBuffer(RigRots));P->ActorCount=B.ActorCount;P->ClipSlotsPerActor=B.ClipSlotsPerActor;
  TShaderMapRef<FOKGpuRigSkeletonCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
  FComputeShaderUtils::AddPass(G,RDG_EVENT_NAME("Crowd.GpuRigSkeleton"),Shader,P,FComputeShaderUtils::GetGroupCount(int32(B.ActorCount),64));
 }
 const uint32 Sets=B.PhysSets.Num()/2;
 if(Sets&&B.NodeCount){
  FRDGBufferRef StateBuffer=G.RegisterExternalBuffer(PhysState);
  if(!PhysicsCleared){AddClearUAVPass(G,G.CreateUAV(StateBuffer),0u);PhysicsCleared=true;}
  TShaderMapRef<FOKGpuRigPhysicsCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
  // The body's Ver02 sets, the clothes sets (both move body bones), then the accessories hung on them.
  const uint32 ChainCount=B.PhysChains.Num()/2;
  for(uint32 Clothes:{2u,1u,0u})for(uint32 Stage:{0u,1u,2u}){
   auto* P=G.AllocParameters<FOKGpuRigPhysicsCS::FParameters>();P->Stage=Stage;P->ChainCount=ChainCount;P->RigRots=G.CreateUAV(G.RegisterExternalBuffer(RigRots));
   P->RigBones=G.CreateSRV(G.RegisterExternalBuffer(Bones));P->PhysSets=G.CreateSRV(G.RegisterExternalBuffer(PhysSets));P->PhysNodes=G.CreateSRV(G.RegisterExternalBuffer(PhysNodes));
   P->PhysChains=G.CreateSRV(G.RegisterExternalBuffer(PhysChains));P->PhysChainValues=G.CreateSRV(G.RegisterExternalBuffer(PhysChainValues));
   P->PhysParticles=G.CreateSRV(G.RegisterExternalBuffer(PhysParticles));P->PhysColliders=G.CreateSRV(G.RegisterExternalBuffer(PhysColliders));P->PhysFollow=G.CreateSRV(G.RegisterExternalBuffer(PhysFollow));
   P->Locals=G.CreateUAV(G.RegisterExternalBuffer(Locals));P->Worlds=G.CreateUAV(WorldsBuffer);P->NodeLocals=G.CreateUAV(G.RegisterExternalBuffer(NodeLocals));
   P->NodeRots=G.CreateUAV(G.RegisterExternalBuffer(NodeRots));P->PhysState=G.CreateUAV(StateBuffer);
   P->SetCount=Sets;P->ClothesPass=Clothes;P->BoneTotal=B.BoneCount;P->ParticleTotal=B.PhysParticles.Num()/8;P->ChainTotal=B.PhysChains.Num()/2;
   P->DeltaTime=FMath::Clamp(DeltaSeconds,0.f,0.1f);P->ResetAll=Reset?1u:0u;P->SettleSteps=30;
   FComputeShaderUtils::AddPass(G,RDG_EVENT_NAME("Crowd.GpuRigPhysics %u.%u",Clothes,Stage),Shader,P,FComputeShaderUtils::GetGroupCount(int32(Stage==1?ChainCount:Sets),64));
  }
 }
 const uint32 TaskCount=B.Tasks.Num();
 FRDGBufferRef Output=G.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(FVector4f),TaskCount*3),TEXT("GpuRig.PoseRows"));
 {
  auto* P=G.AllocParameters<FOKGpuRigTaskCS::FParameters>();
  P->RigTasks=G.CreateSRV(G.RegisterExternalBuffer(Tasks));P->RigTerms=G.CreateSRV(G.RegisterExternalBuffer(Terms));P->TaskWorlds=G.CreateSRV(WorldsBuffer);
  P->RigBones=G.CreateSRV(G.RegisterExternalBuffer(Bones));P->OutputRows=G.CreateUAV(Output);P->TaskCount=TaskCount;
  TShaderMapRef<FOKGpuRigTaskCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
  FComputeShaderUtils::AddPass(G,RDG_EVENT_NAME("Crowd.GpuRigTasks"),Shader,P,FComputeShaderUtils::GetGroupCount(int32(TaskCount),64));
 }
 Out.Current=Output;Out.Previous=HasFrame&&!Reset&&Current?G.RegisterExternalBuffer(Current):Output;Out.Generation=0;
 Current=G.ConvertToExternalBuffer(Output);HasFrame=true;++Evaluations;return true;
}
