#pragma once
#include "CoreMinimal.h"
#include "RenderGraphResources.h"
#include "OKResidentGpuPose.h"

// GPU runtime rig (Shaders/GpuRig.usf): every character's body skeleton posed each frame on the GPU from shared clips
// and her own static data, then each pose task written from "weight x world x constant" terms, in the same rows the
// resident pose bank writes (FOKResidentPoseFrame), so the palette pass and everything after it is unchanged. The
// layouts are the shader's (one entry per actor / bone / list item / ABMX op / clip slot / channel / sample / task /
// term); global indices throughout. Built once on the CPU (OKCrowdRuntimeScene), uploaded once on the render thread.
struct KKCROWDGPUPACKING_API FOKGpuRigBatch {
 TArray<FUintVector4> Actors;   // 4 per actor
 TArray<FVector4f> Bones;       // 3 per bone
 TArray<FUintVector4> Lists;    // always / forearms / hands / pv
 TArray<FVector4f> Abm;         // 3 per op
 TArray<uint32> Named;          // 66 per actor
 TArray<FUintVector4> ClipSlots;TArray<FVector4f> ClipRates; // ClipSlotsPerActor per actor
 TArray<FUintVector4> Channels;TArray<FVector4f> Samples;
 TArray<FUintVector4> Tasks;TArray<FVector4f> Terms; // 4 rows per term: (node, weight, 1: native bone, 0), C rows
 // DynamicBone chains (PhysicsCS): a reference is a bone, or OKGpuRigPhysicsNode | node; a node's world follows the
 // bones' in the worlds buffer (a term's node there is BoneCount + node).
 TArray<FUintVector4> PhysSets;      // 2 per set: (kind: 2 body, 1 clothes, 0 accessory; node first, node count, chain first), (chain count, follow first, follow count, 0)
 TArray<FVector4f> PhysNodes;        // 8 per node
 TArray<FUintVector4> PhysChains;    // 2 per chain
 TArray<FVector4f> PhysChainValues;  // 4 per chain
 TArray<FVector4f> PhysParticles;    // 8 per particle
 TArray<FVector4f> PhysColliders;    // 2 per collider
 TArray<uint32> PhysFollow;
 TArray<FUintVector4> PhysActors;    // 1 per actor: (set first, set count, 0, 0); her sets are contiguous, body then clothes then accessories
 uint32 ActorCount=0,ClipSlotsPerActor=0,BoneCount=0,NodeCount=0;
 bool Validate(FString& Error) const;
};

// Render-thread only, like FOKResidentGpuPose.
class KKCROWDGPUPACKING_API FOKGpuRig {
public:
 bool Initialize(FRDGBuilder& Graph,TSharedRef<const FOKGpuRigBatch,ESPMode::ThreadSafe> Batch,FString& Error);
 bool IsInitialized() const {return Static.IsValid();}
 // Playback: one per actor (clip slots, times, head look turns). Writes Current (and Previous: last frame's,
 // or Current itself after a reset) in the bank's row layout.
 // DeltaSeconds steps the DynamicBone chains (a reset starts their particles over at the pose and settles them).
 bool Evaluate(FRDGBuilder& Graph,TConstArrayView<FOKGpuPosePlayback> Playback,bool ResetHistory,float DeltaSeconds,FOKResidentPoseFrame& Out,FString& Error);
 uint64 Evaluations=0;
private:
 TSharedPtr<const FOKGpuRigBatch,ESPMode::ThreadSafe> Static;
 TRefCountPtr<FRDGPooledBuffer> Actors,Bones,Lists,Abm,Named,ClipSlots,ClipRates,Channels,Samples,Tasks,Terms,Locals,LocalsB,Worlds,Current;
 TRefCountPtr<FRDGPooledBuffer> RigRots,PhysSets,PhysNodes,PhysChains,PhysChainValues,PhysParticles,PhysColliders,PhysFollow,NodeLocals,NodeRots,PhysState;
 // PhysicsCS works one thread group per character (OKGpuRig.cpp OKGpuRigPhysicsGroups): per character and kind four
 // uint4, the jobs (a chain, or a serial set's chains, dearest first), the nodes and the follow bones by depth (level
 // ends, order); ColliderWorlds: every collider's capsule this frame.
 TRefCountPtr<FRDGPooledBuffer> PhysGroups,PhysJobs,PhysLevels,PhysOrder,ColliderWorlds;
 // SkeletonCS works one thread group per character: her bones by depth (OKGpuRig.cpp Initialize).
 TRefCountPtr<FRDGPooledBuffer> SkelGroups,SkelLevels,SkelOrder;
 bool PhysicsCleared=false;
 bool HasFrame=false;
};
KKCROWDGPUPACKING_API bool OKGpuRigAvailable();
inline constexpr uint32 OKGpuRigPhysicsNode=0x40000000u;
