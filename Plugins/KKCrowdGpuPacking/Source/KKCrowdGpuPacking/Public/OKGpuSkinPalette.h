#pragma once
#include "OKResidentGpuPose.h"

// Each item: global component-pose index, first inverse-bind row, destination
// palette index, reserved (zero). Different meshes retain their own bindings.
struct KKCROWDGPUPACKING_API FOKGpuSkinPaletteData {
 TArray<FUintVector4> Items;
 TArray<FVector4f> InverseBindRows;
 uint32 PoseCount=0;
 bool Validate(FString& Error) const;
};
struct FOKGpuSkinPaletteStats {uint64 Generation=0,PoseGeneration=0,Initializations=0,ImmutableUploadBytes=0,Passes=0;uint32 Bones=0;};
// Render-thread-only operations; execute graphs before replacement/release.
// Writes float32 transposed 3x4 matrices into UE's uncompressed float4 transform
// buffer layout. Compressed/half transform consumers require a separate path.
class KKCROWDGPUPACKING_API FOKGpuSkinPalette {
public:
 FOKGpuSkinPalette();~FOKGpuSkinPalette();
 bool Initialize(FRDGBuilder&,const FOKGpuSkinPaletteData&,uint64 PoseGeneration,FString&);
 bool AddPass(FRDGBuilder&,const FOKResidentPoseFrame&,uint64 PaletteGeneration,FRDGBufferRef Output,uint32 CurrentRowOffset,uint32 PreviousRowOffset,FString&);
 FOKGpuSkinPaletteStats GetStats() const;
 void Release();
private:
 friend class FOKGpuSkinPaletteBatch;
 struct FState;TUniquePtr<FState> State;
 FOKGpuSkinPalette(const FOKGpuSkinPalette&)=delete;
 FOKGpuSkinPalette& operator=(const FOKGpuSkinPalette&)=delete;
};
struct FOKGpuSkinPaletteJob {
 FOKGpuSkinPalette* Palette=nullptr;
 uint32 CurrentRowOffset=0,PreviousRowOffset=0;
 bool ResetHistory=false;
};
struct FOKGpuSkinPaletteBatchStats {uint64 LayoutBuilds=0,LayoutReuses=0;};
// Persistent immutable catalog; per-frame jobs only select destination ranges.
// All jobs are validated before any pass is submitted. A rejected batch may
// safely fall back to the original per-component path.
class KKCROWDGPUPACKING_API FOKGpuSkinPaletteBatch {
public:
 FOKGpuSkinPaletteBatch();~FOKGpuSkinPaletteBatch();
 FOKGpuSkinPaletteBatchStats GetStats() const;
 bool AddPass(FRDGBuilder&,const FOKResidentPoseFrame&,FRDGBufferRef,TConstArrayView<FOKGpuSkinPaletteJob>,FString&);
private:
 struct FState;TUniquePtr<FState> State;
};
// Diagnostic only: one graph evaluates persistent pose and palette, then reads
// both component poses and the current/previous render-format palettes.
KKCROWDGPUPACKING_API bool OKReadbackResidentSkinPalette(FOKResidentGpuPose&,FOKGpuSkinPalette&,
 const FOKGpuPoseBatch* Initialization,const TArray<TArray<uint32>>& OwnedClips,
 const FOKGpuSkinPaletteData* PaletteInitialization,TConstArrayView<FOKGpuPosePlayback>,uint64 FrameIndex,
 TArray<FVector4f>& Current,TArray<FVector4f>& Previous,TArray<FVector4f>& Palette,
 FOKResidentPoseStats&,FOKGpuSkinPaletteStats&,FString&);
