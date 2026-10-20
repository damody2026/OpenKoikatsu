#pragma once

#include "CoreMinimal.h"

struct OkMeshView;
struct FReferenceSkeleton;
class FOkSession;
class USkeletalMesh;
class USkeleton;
class USkeletalMeshComponent;
class UPoseableMeshComponent;
class USkinnedMeshComponent;
class UObject;
class UMaterialInterface;

USkeleton* OKBuildSharedSkeleton(FOkSession& Session, UObject* Outer);
USkeletalMesh* OKBuildDummyLeaderMesh(USkeleton* Skeleton, UObject* Outer);

// bSourceWinding: an outline shell in the source winding, for the shared _Outline parents (they cull the front face
// themselves, as in a packaged build); otherwise the editor outline graph's reversed winding.
USkeletalMesh* OKBuildSkeletalMesh(FOkSession& Session, const OkMeshView& View, UObject* Outer, USkeleton* Shared, int32 PartIndex, float OutlineWidthCm = 0.f, bool bOutlinePass = false, bool bSkipFaceCavity = false, bool bSourceWinding = false);
struct FOKSkeletalMeshCacheStats { uint64 Hits=0; uint64 Misses=0; int32 Entries=0; };
FOKSkeletalMeshCacheStats KKUNITYBUNDLE_API OKSkeletalMeshCacheStats();
void KKUNITYBUNDLE_API OKClearSkeletalMeshCache();
void OKApplySkelPose(UPoseableMeshComponent* Comp, FOkSession& Session);
void KKUNITYBUNDLE_API OKBuildSkelPose(FOkSession& Session,TArray<FTransform>& Out);
void KKUNITYBUNDLE_API OKApplySkelPose(UPoseableMeshComponent* Comp,const TArray<FTransform>& Pose);
void OKOverlayUeWalk(UPoseableMeshComponent* Comp, class UAnimSequence* Seq, float Time);
int32 OKRetargetUnrealPose(TArray<FTransform>& Locals, const FReferenceSkeleton& TargetRef, class UAnimSequence* Seq, float Time);
FTransform KKUNITYBUNDLE_API OKUnityLocalToUnreal(const float Pos[3], const float Rot[4], const float Scale[3]);
void KKUNITYBUNDLE_API OKUnrealLocalToUnity(const FTransform& Local, float Pos[3], float Rot[4], float Scale[3]);
void OKApplyUeWalkDirect(UPoseableMeshComponent* Comp, float Time);
class UAnimSequence* OKBuildUeWalkSequence(UObject* Outer);
bool OKUeWalkSequenceHasStride(class UAnimSequence* Seq);
bool OKExtractSkinnedMesh(UPoseableMeshComponent* Comp, TArray<FVector>& OutVerts, TArray<int32>& OutTris, TArray<FVector2D>& OutUV, TArray<FVector>& OutNormals);
bool KKUNITYBUNDLE_API OKDeformFromRef(FOkSession& Session, const OkMeshView& View, TArray<FVector>& OutVerts, bool bTransformVectors = false);
void OKAddSlotSockets(USkeletalMesh* Mesh, FOkSession& Session);
bool OKSkelMeshEnabled();
bool KKUNITYBUNDLE_API OKLiveGpuSkinRequested();

// PIE ignores UMaterial::SetMaterialUsage, so the skeletal usage bit must be
// written directly or the scene proxy logs "missing usage flag" and swaps in
// the default material.
void OKEnsureSkeletalMaterialUsage(UMaterialInterface* Material);

// Live viewer only. Dump extract / cargo tests do not call this.
// Sets OK_GPU_SKIN, OK_SKELMESH, OK_NOPOSE, OK_UE_ANIM and overrides the
// one-shot env cache in OKSkelMeshEnabled.
void OKPrepareLiveGpuEnv();
uint8 OKCaptureSkinEnvironmentState();
void OKRestoreSkinEnvironmentState(uint8 State);
