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
  SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint4>,RigGroups)
  SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>,RigLevels)
  SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>,RigOrder)
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
  SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint4>,PhysGroups)
  SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>,PhysJobs)
  SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>,PhysLevels)
  SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>,PhysOrder)
  SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float4>,ColliderWorlds)
  SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float4>,Locals)
  SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float4>,Worlds)
  SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float4>,RigRots)
  SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float4>,NodeLocals)
  SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float4>,NodeRots)
  SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float4>,PhysState)
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
 if(PhysActors.Num()!=int32(ActorCount))return Fail(TEXT("GPU rig physics actors malformed"));
 uint32 NextSet=0;for(const auto& A:PhysActors){if(A.X!=NextSet||uint64(A.X)+A.Y>uint64(PhysSets.Num()/2))return Fail(TEXT("GPU rig physics actor sets out of range"));NextSet+=A.Y;}
 if(NextSet!=uint32(PhysSets.Num()/2))return Fail(TEXT("GPU rig physics sets outside the actors"));
 return true;
}

// PhysicsCS runs one thread group per character, her kinds in turn (body Ver02, clothes, accessories: the first two move
// body bones the next reads). Per character and kind four uint4: (set first, set count, job first, job count), (node
// order first, level first, level count, 0), (follow order first, level first, level count, 0), (collider first, count,
// 0, 0). A job is a chain, or 0x80000000 | where its chains are listed (count, chains): chains of a set that must go one
// after another, one writing a bone another reads (that bone or one above it; OKGpuRigChainNodes), joined while any two
// meet; a group's jobs dearest first (particles x colliders), so the long ones start at once. Levels: the end of each
// depth's run in Order (nodes: their parent node is a level up; follow bones: their parent follow bone), so a level is
// one parallel step.
static void OKGpuRigChainNodes(const FOKGpuRigBatch& B,uint32 C,TArray<uint32>& Writes,TArray<uint32>& Reads){
 auto U=[](float V){return reinterpret_cast<const uint32&>(V);};
 Writes.Reset();Reads.Reset();
 const auto& H=B.PhysChains[C*2];const auto& K=B.PhysChains[C*2+1];const bool V02=(H.Z&8u)!=0;
 auto Up=[&](uint32 N){while(N!=MAX_uint32&&N<B.NodeCount){Reads.AddUnique(N);const uint32 P=U(B.PhysNodes[N*8+2].W);N=(P!=MAX_uint32&&(P&OKGpuRigPhysicsNode))?P&~OKGpuRigPhysicsNode:MAX_uint32;}};
 Up(H.X);Up(H.Y);
 for(uint32 I=K.X;I<K.X+K.Y;++I){const uint32 Node=U(B.PhysParticles[I*8].X),Parent=U(B.PhysParticles[I*8].Y);Up(Node);
  if(Parent==MAX_uint32)continue;
  if(Node!=MAX_uint32)Writes.AddUnique(Node);
  // DynamicBone turns a parent with one child; Ver02 a parent with the rotate flag.
  const bool Turns=V02?(U(B.PhysParticles[Parent*8+4].W)&4u)!=0:(U(B.PhysParticles[I*8].Z)&1u)!=0;
  const uint32 ParentNode=U(B.PhysParticles[Parent*8].X);if(Turns&&ParentNode!=MAX_uint32)Writes.AddUnique(ParentNode);}
 for(uint32 Q=K.Z;Q<K.Z+K.W;++Q){const uint32 R=U(B.PhysColliders[Q*2+1].W);if(R!=MAX_uint32&&(R&OKGpuRigPhysicsNode))Up(R&~OKGpuRigPhysicsNode);}
}
static bool OKGpuRigPhysicsGroups(const FOKGpuRigBatch& B,TArray<FUintVector4>& Groups,TArray<uint32>& Jobs,TArray<uint32>& Levels,TArray<uint32>& Order,FString& Error){
 auto U=[](float V){return reinterpret_cast<const uint32&>(V);};
 TArray<uint32> Depth;Depth.SetNumZeroed(B.NodeCount);
 for(uint32 N=0;N<B.NodeCount;++N){const uint32 P=U(B.PhysNodes[N*8+2].W);if(P!=MAX_uint32&&(P&OKGpuRigPhysicsNode))Depth[N]=Depth[P&~OKGpuRigPhysicsNode]+1;}
 auto Leveled=[&](const TArray<TPair<uint32,uint32>>& Items,FUintVector4& Row){ // (item, depth) -> order and level ends
  uint32 Deepest=0;for(const auto& I:Items)Deepest=FMath::Max(Deepest,I.Value);
  Row=FUintVector4(Order.Num(),Levels.Num(),Items.IsEmpty()?0:Deepest+1,0);if(Items.IsEmpty())return;
  for(uint32 L=0;L<=Deepest;++L){for(const auto& I:Items)if(I.Value==L)Order.Add(I.Key);Levels.Add(Order.Num());}
 };
 TSet<uint32> Following;TArray<uint32> Lists;TArray<TPair<int32,int32>> ListJobs; // (job, list offset)
 TArray<TArray<uint32>> Writes,Reads;
 for(uint32 A=0;A<B.ActorCount;++A)for(const uint32 Kind:{2u,1u,0u}){
  FUintVector4 G[4]={FUintVector4(0,0,Jobs.Num(),0),FUintVector4(Order.Num(),Levels.Num(),0,0),FUintVector4(Order.Num(),Levels.Num(),0,0),FUintVector4(0,0,0,0)};
  const auto& Actor=B.PhysActors[A];TArray<uint32> Sets;
  for(uint32 S=Actor.X;S<Actor.X+Actor.Y;++S)if((B.PhysSets[S*2].X&0xffu)==Kind)Sets.Add(S);
  if(!Sets.IsEmpty()){
   if(Sets.Last()-Sets[0]+1!=uint32(Sets.Num())){Error=TEXT("GPU rig physics sets of a kind not contiguous");return false;}
   G[0].X=Sets[0];G[0].Y=Sets.Num();
   TArray<TPair<uint32,uint32>> Nodes,Follow;TArray<TPair<uint32,uint64>> Costed;uint32 ColliderFirst=MAX_uint32,ColliderEnd=0;
   auto ChainCost=[&](uint32 C){const auto& K=B.PhysChains[C*2+1];return uint64(K.Y)*(1+K.W);};
   for(const uint32 S:Sets){
    const auto& H=B.PhysSets[S*2];const auto& T=B.PhysSets[S*2+1];
    for(uint32 N=H.Y;N<H.Y+H.Z;++N)Nodes.Add({N,Depth[N]});
    const uint32 Count=T.X;
    for(uint32 C=H.W;C<H.W+Count;++C){const auto& K=B.PhysChains[C*2+1];
     if(K.W){if(ColliderFirst!=MAX_uint32&&K.Z!=ColliderEnd){Error=TEXT("GPU rig physics colliders of a kind not contiguous");return false;}if(ColliderFirst==MAX_uint32)ColliderFirst=K.Z;ColliderEnd=K.Z+K.W;}}
    // The set's chains joined while one writes a bone another reads; each group of them a job, in the set's order.
    Writes.SetNum(Count);Reads.SetNum(Count);for(uint32 C=0;C<Count;++C)OKGpuRigChainNodes(B,H.W+C,Writes[C],Reads[C]);
    TArray<uint32> Root;Root.SetNum(Count);for(uint32 C=0;C<Count;++C)Root[C]=C;
    auto Find=[&](uint32 X){while(Root[X]!=X)X=Root[X]=Root[Root[X]];return X;};
    auto Meets=[&](const TArray<uint32>& W,const TArray<uint32>& R){for(const uint32 N:W)if(R.Contains(N))return true;return false;};
    for(uint32 I=0;I<Count;++I)for(uint32 J=I+1;J<Count;++J)if(Find(I)!=Find(J)&&(Meets(Writes[I],Reads[J])||Meets(Writes[J],Reads[I])))Root[Find(J)]=Find(I);
    for(uint32 C=0;C<Count;++C){if(Find(C)!=C)continue;
     TArray<uint32> Members;uint64 Cost=0;for(uint32 M=C;M<Count;++M)if(Find(M)==C){Members.Add(H.W+M);Cost+=ChainCost(H.W+M);}
     if(Members.Num()==1){Costed.Add({Members[0],Cost});continue;}
     ListJobs.Add({-1,Lists.Num()});Costed.Add({0x80000000u|uint32(ListJobs.Num()-1),Cost});Lists.Add(Members.Num());Lists.Append(Members);}
    // Follow bones in rig order (parents first): a level below a parent that follows too.
    Following.Reset();for(uint32 F=T.Y;F<T.Y+T.Z;++F){const uint32 Bone=B.PhysFollow[F];const uint32 P=U(B.Bones[Bone*3].W);
     uint32 D=0;if(Following.Contains(P))for(const auto& X:Follow)if(X.Key==P){D=X.Value+1;break;}
     Follow.Add({Bone,D});Following.Add(Bone);}
   }
   Costed.StableSort([](const TPair<uint32,uint64>& X,const TPair<uint32,uint64>& Y){return X.Value>Y.Value;});
   for(const auto& J:Costed){if(J.Key&0x80000000u)ListJobs[J.Key&0x7fffffffu].Key=Jobs.Num();Jobs.Add(J.Key);}
   G[0].W=Costed.Num();
   Leveled(Nodes,G[1]);Leveled(Follow,G[2]);
   if(ColliderFirst!=MAX_uint32)G[3]=FUintVector4(ColliderFirst,ColliderEnd-ColliderFirst,0,0);
  }
  for(const auto& R:G)Groups.Add(R);
 }
 // The lists after the jobs.
 const uint32 ListBase=Jobs.Num();for(const auto& L:ListJobs)Jobs[L.Key]=0x80000000u|(ListBase+uint32(L.Value));Jobs.Append(Lists);
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
 {TArray<FUintVector4> Groups;TArray<uint32> Jobs,Levels,Order;if(!OKGpuRigPhysicsGroups(B,Groups,Jobs,Levels,Order,Error))return false;
  PhysGroups=OKGpuRigUpload(G,TEXT("GpuRig.PhysGroups"),Groups);PhysJobs=OKGpuRigUpload(G,TEXT("GpuRig.PhysJobs"),Jobs);
  PhysLevels=OKGpuRigUpload(G,TEXT("GpuRig.PhysLevels"),Levels);PhysOrder=OKGpuRigUpload(G,TEXT("GpuRig.PhysOrder"),Order);
  ColliderWorlds=Scratch(TEXT("GpuRig.ColliderWorlds"),B.PhysColliders.Num());}
 // SkeletonCS: per actor (order first, level first, level count, 0), her bones by depth (a parent precedes its bones).
 {TArray<FUintVector4> Groups;TArray<uint32> Levels,Order;TArray<uint32> Depth;Depth.SetNumZeroed(B.BoneCount);
  for(uint32 A=0;A<B.ActorCount;++A){const uint32 First=B.Actors[A*4].X,Count=B.Actors[A*4].Y;uint32 Deepest=0;
   for(uint32 Bone=First;Bone<First+Count;++Bone){const uint32 P=reinterpret_cast<const uint32&>(B.Bones[Bone*3].W);Depth[Bone]=P!=MAX_uint32&&P>=First&&P<Bone?Depth[P]+1:0;Deepest=FMath::Max(Deepest,Depth[Bone]);}
   Groups.Add(FUintVector4(Order.Num(),Levels.Num(),Count?Deepest+1:0,0));
   if(Count)for(uint32 L=0;L<=Deepest;++L){for(uint32 Bone=First;Bone<First+Count;++Bone)if(Depth[Bone]==L)Order.Add(Bone);Levels.Add(Order.Num());}}
  SkelGroups=OKGpuRigUpload(G,TEXT("GpuRig.SkelGroups"),Groups);SkelLevels=OKGpuRigUpload(G,TEXT("GpuRig.SkelLevels"),Levels);SkelOrder=OKGpuRigUpload(G,TEXT("GpuRig.SkelOrder"),Order);}
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
  P->RigGroups=G.CreateSRV(G.RegisterExternalBuffer(SkelGroups));P->RigLevels=G.CreateSRV(G.RegisterExternalBuffer(SkelLevels));P->RigOrder=G.CreateSRV(G.RegisterExternalBuffer(SkelOrder));
  P->RigSelect=G.CreateSRV(CreateStructuredBuffer(G,TEXT("GpuRig.Select"),Select));P->RigTimes=G.CreateSRV(CreateStructuredBuffer(G,TEXT("GpuRig.Times"),Times));P->RigLook=G.CreateSRV(CreateStructuredBuffer(G,TEXT("GpuRig.Look"),Look));
  P->Locals=LocalsUav;P->LocalsB=LocalsBUav;P->Worlds=G.CreateUAV(WorldsBuffer);P->RigRots=G.CreateUAV(G.RegisterExternalBuffer(RigRots));P->ActorCount=B.ActorCount;P->ClipSlotsPerActor=B.ClipSlotsPerActor;
  TShaderMapRef<FOKGpuRigSkeletonCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
  // One group per character (her bones shared by its threads).
  FComputeShaderUtils::AddPass(G,RDG_EVENT_NAME("Crowd.GpuRigSkeleton"),Shader,P,FIntVector(int32(B.ActorCount),1,1));
 }
 const uint32 Sets=B.PhysSets.Num()/2;
 if(Sets&&B.NodeCount){
  FRDGBufferRef StateBuffer=G.RegisterExternalBuffer(PhysState);
  if(!PhysicsCleared){AddClearUAVPass(G,G.CreateUAV(StateBuffer),0u);PhysicsCleared=true;}
  TShaderMapRef<FOKGpuRigPhysicsCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
  // One group per character: her body's Ver02 sets, her clothes (both move body bones), then her accessories.
  auto* P=G.AllocParameters<FOKGpuRigPhysicsCS::FParameters>();P->RigRots=G.CreateUAV(G.RegisterExternalBuffer(RigRots));
  P->RigBones=G.CreateSRV(G.RegisterExternalBuffer(Bones));P->PhysSets=G.CreateSRV(G.RegisterExternalBuffer(PhysSets));P->PhysNodes=G.CreateSRV(G.RegisterExternalBuffer(PhysNodes));
  P->PhysChains=G.CreateSRV(G.RegisterExternalBuffer(PhysChains));P->PhysChainValues=G.CreateSRV(G.RegisterExternalBuffer(PhysChainValues));
  P->PhysParticles=G.CreateSRV(G.RegisterExternalBuffer(PhysParticles));P->PhysColliders=G.CreateSRV(G.RegisterExternalBuffer(PhysColliders));P->PhysFollow=G.CreateSRV(G.RegisterExternalBuffer(PhysFollow));
  P->PhysGroups=G.CreateSRV(G.RegisterExternalBuffer(PhysGroups));P->PhysJobs=G.CreateSRV(G.RegisterExternalBuffer(PhysJobs));
  P->PhysLevels=G.CreateSRV(G.RegisterExternalBuffer(PhysLevels));P->PhysOrder=G.CreateSRV(G.RegisterExternalBuffer(PhysOrder));P->ColliderWorlds=G.CreateUAV(G.RegisterExternalBuffer(ColliderWorlds));
  P->Locals=G.CreateUAV(G.RegisterExternalBuffer(Locals));P->Worlds=G.CreateUAV(WorldsBuffer);P->NodeLocals=G.CreateUAV(G.RegisterExternalBuffer(NodeLocals));
  P->NodeRots=G.CreateUAV(G.RegisterExternalBuffer(NodeRots));P->PhysState=G.CreateUAV(StateBuffer);
  P->BoneTotal=B.BoneCount;P->ParticleTotal=B.PhysParticles.Num()/8;P->ChainTotal=B.PhysChains.Num()/2;
  P->DeltaTime=FMath::Clamp(DeltaSeconds,0.f,0.1f);P->ResetAll=Reset?1u:0u;P->SettleSteps=30;
  FComputeShaderUtils::AddPass(G,RDG_EVENT_NAME("Crowd.GpuRigPhysics"),Shader,P,FIntVector(int32(B.ActorCount),1,1));
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
