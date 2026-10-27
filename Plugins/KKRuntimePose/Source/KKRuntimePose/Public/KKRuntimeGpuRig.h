#pragma once
#include "CoreMinimal.h"
#include "Async/Future.h"
#include "KKRuntimePoseBankTypes.h"

// What the GPU runtime rig (KKCrowdGpuPacking FOKGpuRig, Shaders/GpuRig.usf) needs of one character, prepared on a
// worker from her pose recipe (Tools/ok-extract/src/gpu_rig/export.rs): her body skeleton (local bone indices) and,
// for every bone of her render rig, the terms its pose row is the sum of: weight x UE(world of a body bone) x C (C in
// UE column form, three rows; Node INDEX_NONE: the identity). Physics is not part of it.
// Native: a render bone that is her rig bone itself; the pose bank composes those like UE's FTransform (IndependentPose.usf
// NativeTRS, no shear), so the rig does too (GpuRig.usf TaskCS).
struct FKKRuntimeGpuRigTerm {int32 Node=INDEX_NONE;float Weight=1.f;FVector4f C[3];bool Native=false;};
struct FKKRuntimeGpuRigAbm {int32 Bone=0;FVector4f Rot;float Length=1.f;FVector3f Pos=FVector3f::ZeroVector,Scale=FVector3f::OneVector;};
// Her DynamicBone chains for the rig's verlet pass (export.rs Physics::write, Shaders/GpuRig.usf PhysicsCS), in the
// shader's rows with her own indices: a transform reference is her rig bone, or 0x40000000 | her node; a particle's
// parent is its chain's index. BoneTerms nodes at Names.Num() and above are her nodes.
struct FKKRuntimeGpuRigPhysics {
 TArray<FVector4f> Nodes;      // 8 per node: (pos, kind), rot, (scale, parent ref), matrix rows 0..2, rot offset, (alias bone, local source bone, 0, 0)
 TArray<FUintVector4> Chains;  // 2 per chain: (root node, component node, flags, max steps), (particle first, count, collider first, count)
 TArray<FVector4f> ChainValues;// 4 per chain: (gravity, update rate), (force, 0), (local gravity, 0), (end offset, reflect speed)
 TArray<FVector4f> Particles;  // 8 per particle: (node, parent, flags, damping), (end offset, elasticity), (local position, stiffness), (inert, radius, add angle scale, initial scale z),
                               // Ver02: (keep min, keep max, next length, flags), (move min, crush min), (move max, crush max), (crush add min, crush add max, initial scale xy)
 TArray<FVector4f> Colliders;  // 2 per collider: (center, radius), (height, direction, inside, ref)
 TArray<uint32> Follow;        // her rig bones under a solved alias
 TArray<FUintVector4> Sets;    // per set, stepping order: (kind: 2 body, 1 clothes, 0 accessory; node first, node count, chain first) then chain count in the next .x
 int32 NodeCount() const {return Nodes.Num()/8;}
 int32 ParticleCount() const {return Particles.Num()/8;}
};
inline constexpr uint32 OKGpuRigNodeFlag=0x40000000u;
struct FKKRuntimeGpuRigResult {
 FString Error;
 TArray<FString> Names;TArray<int32> Parent;TArray<FVector3f> Pos;TArray<FVector4f> Rot;TArray<FVector3f> Scale;
 bool Female=false;TArray<FIntPoint> Always;TArray<int32> Forearms,Hands;TArray<FIntPoint> Pv;TArray<FKKRuntimeGpuRigAbm> Abm;
 TArray<int32> Named; // 66 (export.rs NAMED_FIRST then NAMED_LAST), INDEX_NONE: missing
 TArray<TArray<FKKRuntimeGpuRigTerm>> BoneTerms; // per bone of the request's render rig
 FKKRuntimeGpuRigPhysics Physics;
 // Per requested clip: "OKGC" (export.rs SharedClip::to_bytes), empty when the clip could not be exported.
 TArray<TArray<uint8>> Clips;
 int32 LoosePalettes=0,EmpiricalTerms=0;double PrepareMs=0;
 bool Passed() const {return Error.IsEmpty();}
};
// Recipe: the character's pose request (snapshot, render rig, parts). Clips: the shared clips she plays (AuthoredCurves
// bytes); the first also tells each palette matrix's bone.
KKRUNTIMEPOSE_API TFuture<FKKRuntimeGpuRigResult> OKPrepareRuntimeGpuRigAsync(TSharedPtr<const FKKRuntimePoseRequest,ESPMode::ThreadSafe> Recipe,TArray<TArray<uint8>> Clips);

// A parsed "OKGC" shared clip (the GPU's channel layout).
struct FKKRuntimeGpuClip {
 TArray<FString> Bones;TArray<uint32> Mask;uint32 Frames=0;float Rate=60.f,Duration=0;uint32 Flags=0;
 TArray<FVector4f> Samples; // [channel][frame] (pos, 0), rot, (scale, 0)
};
KKRUNTIMEPOSE_API bool OKParseRuntimeGpuClip(const TArray<uint8>& Bytes,FKKRuntimeGpuClip& Out);
