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
 TArray<FUintVector4> Tasks;TArray<FVector4f> Terms; // 4 rows per term
 uint32 ActorCount=0,ClipSlotsPerActor=0,BoneCount=0;
 bool Validate(FString& Error) const;
};

// Render-thread only, like FOKResidentGpuPose.
class KKCROWDGPUPACKING_API FOKGpuRig {
public:
 bool Initialize(FRDGBuilder& Graph,TSharedRef<const FOKGpuRigBatch,ESPMode::ThreadSafe> Batch,FString& Error);
 bool IsInitialized() const {return Static.IsValid();}
 // Playback: one per actor (clip slots, times, head look turns). Writes Current (and Previous: last frame's,
 // or Current itself after a reset) in the bank's row layout.
 bool Evaluate(FRDGBuilder& Graph,TConstArrayView<FOKGpuPosePlayback> Playback,bool ResetHistory,FOKResidentPoseFrame& Out,FString& Error);
 uint64 Evaluations=0;
private:
 TSharedPtr<const FOKGpuRigBatch,ESPMode::ThreadSafe> Static;
 TRefCountPtr<FRDGPooledBuffer> Actors,Bones,Lists,Abm,Named,ClipSlots,ClipRates,Channels,Samples,Tasks,Terms,Locals,LocalsB,Worlds,Current;
 bool HasFrame=false;
};
KKCROWDGPUPACKING_API bool OKGpuRigAvailable();
