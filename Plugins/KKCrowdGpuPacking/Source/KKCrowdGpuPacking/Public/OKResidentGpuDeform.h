#pragma once
#include "OKRuntimeGpuDeform.h"

struct FOKResidentDeformFrame {
 FRDGBufferRef Positions=nullptr,Tangents=nullptr,PreviousPositions=nullptr,PreviousTangents=nullptr;
 uint64 Generation=0;uint32 DrawCount=0;
};
struct FOKResidentDeformStats {
 uint64 Generation=0,Installations=0,Evaluations=0,ImmutableUploadBytes=0,PaletteUploadBytes=0;
 uint64 ResidentBytes=0,PeakLogicalBytes=0;
};
// Render-thread only. Execute each graph before the next operation. Callers own
// CPU query geometry/bounds separately, and install new generations on changes.
// Failed installs/updates retain all current buffers and leave Result untouched.
// Old graph/RHI references remain valid after replacement/release.
// MaxLogicalBytes covers current+replacement buffers and one scratch dispatch;
// it excludes driver allocations and older generations retained by callers.
class KKCROWDGPUPACKING_API FOKResidentGpuDeform {
public:
 FOKResidentGpuDeform();~FOKResidentGpuDeform();
 bool Install(FRDGBuilder&,TConstArrayView<FOKGpuDeformSource>,TConstArrayView<FOKGpuDeformDraw>,
  TConstArrayView<FMatrix44f>,uint32 FallbackSlot,const FMatrix44f* PostCorrection,
  uint64 ExpectedGeneration,FOKResidentDeformFrame& Result,FString& Error,uint64 MaxLogicalBytes=512ull*1024*1024,bool NativeUnitySpace=false);
 bool Update(FRDGBuilder&,TConstArrayView<FMatrix44f>,const FMatrix44f* PostCorrection,
  uint64 ExpectedGeneration,FOKResidentDeformFrame& Result,FString& Error);
 bool GetCurrent(FRDGBuilder&,uint64 ExpectedGeneration,FOKResidentDeformFrame& Result) const;
 void Release();
 FOKResidentDeformStats GetStats() const;
private:
 struct FState;TUniquePtr<FState> State;
 bool Dispatch(FRDGBuilder&,FState&,TConstArrayView<FVector4f>,const FMatrix44f*,FOKResidentDeformFrame&);
 FOKResidentGpuDeform(const FOKResidentGpuDeform&)=delete;
 FOKResidentGpuDeform& operator=(const FOKResidentGpuDeform&)=delete;
};
