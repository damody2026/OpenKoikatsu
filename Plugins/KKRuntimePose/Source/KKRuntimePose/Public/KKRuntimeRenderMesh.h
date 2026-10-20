#pragma once
#include "KKRuntimeMesh.h"
#include "Rendering/SkinWeightVertexBuffer.h"
class USkeletalMesh;class UMaterialInterface;
struct FKKRuntimeRenderSection {
 int32 Material=0;uint32 BaseVertex=0,NumVertices=0,BaseIndex=0,Triangles=0;
 TArray<uint16> BoneMap;
};
struct FKKRuntimeRenderMeshData {
 TArray<FKKRuntimeMeshVertex> Vertices;TArray<FSkinWeightInfo> Weights;TArray<uint32> Indices;
 TArray<FKKRuntimeRenderSection> Sections;
 TArray<FKKRuntimePoseBone> Bones;int32 Materials=0;FString Error;
 double PrepareMs=0;bool WorkerThread=false;
};
// Background triangle partitioning: no geometric welding and no discarded faces.
KKRUNTIMEPOSE_API TFuture<FKKRuntimeRenderMeshData> OKPrepareRuntimeRenderMeshAsync(
 TArray<FKKRuntimeMeshPart> Meshes,TArray<FKKRuntimePoseBone> Bones,
 uint32 SectionBoneLimit,uint64 MaxArrayBytes=256ull*1024*1024);
// Game thread only. Direct transient render buffers; no imported/editor model.
// Caller keeps the UObject alive until all components/uploads release it.
KKRUNTIMEPOSE_API USkeletalMesh* OKCreateRuntimeRenderMesh(const FKKRuntimeRenderMeshData&,
 TConstArrayView<UMaterialInterface*> Materials,FString& Error,bool KeepCPUData=false);

struct FKKRuntimeRenderMeshStats {bool Created=false;uint32 Sections=0,Vertices=0,Triangles=0;double PrepareMs=0,ReductionMs=0;bool Reduced=false,BudgetsMet=false;uint32 RetainedParts=0;uint64 Groups[4]={};};

// -1 pattern preserves that card-authored channel. -1 opening means the caller did not supply one.
// Blend flags default false so existing callers stay immediate. True requests a 0.15s morph blend.
struct FKKRuntimeFaceState {
 int32 MouthPattern=-1,EyePattern=-1,BrowPattern=-1;
 float MouthOpening=-1.f,EyeOpening=-1.f,BrowOpening=-1.f;
 bool MouthBlend=false,EyeBlend=false,BrowBlend=false;
};
struct FKKRuntimeMouthVertex {
 FVector3f Base,Closed,Open;FVector3f Blink=FVector3f::ZeroVector;
 TArray<FKKRuntimeFaceSparse> Face;
 TSharedPtr<const FKKRuntimeFaceCatalog,ESPMode::ThreadSafe> FaceCatalog;
};
struct FKKRuntimeFaceRange {uint32 FirstVertex=0,DataOffset=0,Count=0;};
struct FKKRuntimeMouthStream {
 uint32 FirstVertex=0,AffectedVertices=0;
 uint32 BlinkVertices=0;
 // Minimum pattern count among included parts that actually have that control. 0 if none do.
 int32 PatternCount[3]={0,0,0};
 TArray<FKKRuntimeMouthVertex> Vertices;
 TArray<FKKRuntimeFaceRange> Ranges;
};
KKRUNTIMEPOSE_API TSharedPtr<const FKKRuntimeMouthStream,ESPMode::ThreadSafe> OKPrepareRuntimeMouthStream(const FKKRuntimeRenderMeshData&);
// Reject unknown patterns and nonfinite openings. Does not write a mesh.
KKRUNTIMEPOSE_API bool OKValidateRuntimeFaceState(TSharedPtr<const FKKRuntimeMouthStream,ESPMode::ThreadSafe> Stream,const FKKRuntimeFaceState& State,FString& Error);
// Pure position composition. On failure Out is left unchanged. Blend flags are ignored here.
KKRUNTIMEPOSE_API bool OKEvaluateRuntimeFacePositions(const FKKRuntimeMouthStream& Stream,float Opening,float Gain,float Blink,const FKKRuntimeFaceState* Face,TArray<FVector3f>& Out,FString& Error);
inline constexpr float OKRuntimeFaceTransitionSeconds=0.15f;
inline constexpr int32 OKRuntimeFaceMorphLimit=512;
struct FKKRuntimeFaceWeightSet {TArray<float> A,B;};
// Channel order is mouth, eyes, brow. Retarget replaces From; it does not keep earlier endpoints.
struct FKKRuntimeFaceChannelTransition {
 int32 Pattern=-1;float Opening=-1.f;bool Blend=false,Timing=false;float Elapsed=0.f,Legacy=0.f;
 TArray<FKKRuntimeFaceWeightSet> From,To;
};
struct FKKRuntimeFaceTransition {
 TArray<TSharedPtr<const FKKRuntimeFaceCatalog,ESPMode::ThreadSafe>> Catalogs;
 FKKRuntimeFaceChannelTransition Channel[3];bool EndpointPending=false;
 bool Active() const {return Channel[0].Timing||Channel[1].Timing||Channel[2].Timing;}
};
struct FOKRuntimeFacePlayback {
 bool Enabled=false,Blink=true,Dirty=false;
 FKKRuntimeFaceState Face;FKKRuntimeFaceTransition Transition;
 uint64 Requests=0,Updates=0,Restores=0,Started=0,Intermediate=0,Completed=0,Cancelled=0;
};
struct FOKRuntimeFaceUploadPlan {bool Upload=false,Intermediate=false;int32 Completed=0;};
// LegacyPose is the nullptr mouth route. Started counts channels that enter the 0.15s clock.
// Failure leaves Out, Changed and Started untouched.
KKRUNTIMEPOSE_API bool OKApplyRuntimeFaceTransition(const FKKRuntimeMouthStream& Stream,const FKKRuntimeFaceTransition& Current,const FKKRuntimeFaceState& Request,bool LegacyPose,FKKRuntimeFaceTransition& Out,bool& Changed,int32& Started,FString& Error);
// Nonfinite or negative Dt leaves the transition unchanged. A hitch is capped at one second.
KKRUNTIMEPOSE_API bool OKAdvanceRuntimeFaceTransition(FKKRuntimeFaceTransition& Transition,float Dt,int32& CompletedChannels,FString& Error);
KKRUNTIMEPOSE_API bool OKEvaluateRuntimeFaceTransition(const FKKRuntimeMouthStream& Stream,float Opening,float Gain,float Blink,const FKKRuntimeFaceTransition& Transition,TArray<FVector3f>& Out,FString& Error);
KKRUNTIMEPOSE_API bool OKRequestRuntimeFace(const FKKRuntimeMouthStream& Stream,FOKRuntimeFacePlayback& Playback,const FKKRuntimeFaceState& Request,bool AllowBlink,FString& Error);
KKRUNTIMEPOSE_API bool OKResetRuntimeFace(FOKRuntimeFacePlayback& Playback,FString& Error);
// Bucket is SceneTime*60. Settled playback does not upload when mouth, blink and gain are unchanged.
KKRUNTIMEPOSE_API bool OKPlanRuntimeFaceUpload(FOKRuntimeFacePlayback& Playback,float Dt,int64 Bucket,int64 LastBucket,bool MouthMoved,bool BlinkMoved,bool GainChanged,FOKRuntimeFaceUploadPlan& Plan,FString& Error);
KKRUNTIMEPOSE_API void OKNoteRuntimeFaceUpload(FOKRuntimeFacePlayback& Playback,const FOKRuntimeFaceUploadPlan& Plan);
// Update private face positions for the color mesh and, when present, the depth mesh.
// Face==nullptr keeps historical mouth pattern 0 plus eyelid closure. Body pose stays on the GPU path.
// Transition, when set, supplies the private morph blend. Both meshes receive the same positions.
KKRUNTIMEPOSE_API bool OKUpdateRuntimeMouthStream(USkeletalMesh* Main,USkeletalMesh* Depth,
 TSharedPtr<const FKKRuntimeMouthStream,ESPMode::ThreadSafe>,float Opening,float Gain,FString& Error,float Blink=0,const FKKRuntimeFaceState* Face=nullptr,const FKKRuntimeFaceTransition* Transition=nullptr);
