#pragma once
#include "CoreMinimal.h"
#include "Async/Future.h"
#include "KKRuntimePoseBankTypes.h"

// What the GPU runtime rig (KKCrowdGpuPacking FOKGpuRig, Shaders/GpuRig.usf) needs of one character, prepared on a
// worker from her pose recipe (Tools/ok-extract/src/gpu_rig/export.rs): her body skeleton (local bone indices) and,
// for every bone of her render rig, the terms its pose row is the sum of: weight x UE(world of a body bone) x C (C in
// UE column form, three rows; Node INDEX_NONE: the identity). Physics is not part of it.
struct FKKRuntimeGpuRigTerm {int32 Node=INDEX_NONE;float Weight=1.f;FVector4f C[3];};
struct FKKRuntimeGpuRigAbm {int32 Bone=0;FVector4f Rot;float Length=1.f;FVector3f Pos=FVector3f::ZeroVector,Scale=FVector3f::OneVector;};
struct FKKRuntimeGpuRigResult {
 FString Error;
 TArray<FString> Names;TArray<int32> Parent;TArray<FVector3f> Pos;TArray<FVector4f> Rot;TArray<FVector3f> Scale;
 bool Female=false;TArray<FIntPoint> Always;TArray<int32> Forearms,Hands;TArray<FIntPoint> Pv;TArray<FKKRuntimeGpuRigAbm> Abm;
 TArray<int32> Named; // 66 (export.rs NAMED_FIRST then NAMED_LAST), INDEX_NONE: missing
 TArray<TArray<FKKRuntimeGpuRigTerm>> BoneTerms; // per bone of the request's render rig
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
