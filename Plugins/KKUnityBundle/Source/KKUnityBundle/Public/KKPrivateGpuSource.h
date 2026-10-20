#pragma once
#include "OKRuntimeGpuDeform.h"
class FOkSession;

// Value-owned immutable native streams, in Unity coordinates/meters. Never
// retain the borrowed native views. Revision is local to this live session.
struct FKKPrivateGpuSource {
 uint64 Revision=0;uint32 PaletteCount=0,FallbackSlot=0;
 TArray<FOKGpuDeformSource> Vertices;
};
struct FKKPrivateGpuPose {
 uint64 Revision=0;TArray<FMatrix44f> Palette;
 FMatrix44f PostCorrection=FMatrix44f::Identity;
};
// Capture on the game thread. Consume inside a blocking operation before the
// actor's next pose/source publication; workers never inspect UObjects.
struct FKKPrivateGpuSurfaceView {
 const FKKPrivateGpuSource* Source=nullptr;
 const FKKPrivateGpuPose* Pose=nullptr;
 TConstArrayView<FOKGpuDeformDraw> Draw;
 FMatrix Render=FMatrix::Identity;
};
// Failures leave caller-owned outputs untouched. Missing directions preserve
// the original CPU route; this adapter never drops unsupported geometry.
KKUNITYBUNDLE_API bool OKCapturePrivateGpuSource(const FOkSession&,int32 Part,FKKPrivateGpuSource&,FKKPrivateGpuPose&,FString& Error);
// Palette-only update; does not recopy/validate all source vertices.
KKUNITYBUNDLE_API bool OKReadPrivateGpuPose(const FOkSession&,int32 Part,const FKKPrivateGpuSource&,FKKPrivateGpuPose&,FString& Error);
KKUNITYBUNDLE_API bool OKReadPrivateGpuPoses(const FOkSession&,TConstArrayView<int32> Parts,TConstArrayView<const FKKPrivateGpuSource*> Sources,TArray<FKKPrivateGpuPose>& Poses,FString& Error);
// Sparse CPU consumers evaluate the same resident skin/post stages. No native
// full-mesh query or GPU readback; indices/palette/revision remain validated.
KKUNITYBUNDLE_API bool OKEvaluatePrivateGpuPosition(const FKKPrivateGpuSource&,const FKKPrivateGpuPose&,const FOKGpuDeformDraw&,FVector& Out);
// Draw index order/secondary offsets and normals follow the original component
// in UE coordinates. Empty Indices means a direct surface/source index order.
KKUNITYBUNDLE_API bool OKBuildPrivateGpuDraw(const FKKPrivateGpuSource&,TConstArrayView<int32> Indices,uint32 Section,
 TConstArrayView<FVector> Offsets,TConstArrayView<FVector> SectionNormals,TConstArrayView<FVector> RestNormals,
 TArray<FOKGpuDeformDraw>& Out,FString& Error);
