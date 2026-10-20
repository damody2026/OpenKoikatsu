#pragma once
#include "CoreMinimal.h"
#include "OKGpuAnimationManifest.h"
class UWorld;
class AKKImportedCharacterActor;
class USkeletalMeshComponent;
class FOKCrowdIndependentGpu;
struct FOKCrowdGpuLoadStats {bool FromIndex=false,WorkerThread=false;uint32 BundleActors=0,LoadedShards=0,MissingSelectedActors=0;double LoadMs=0;};
// Whole-character production preflight, not a matrix-bridge coverage count.
struct FOKCrowdIndependentGpuStats {uint64 PaletteDispatches=0,ClockOwnedActors=0,ClockUpdates=0,ClockFallbacks=0,ClockCpuEvaluations=0;bool Ready=false,ProbeCpuPoseAbsent=false,ProbeOriginalMaterials=false;uint64 RuntimePaletteBuilds=0,RuntimePaletteRejects=0,RuntimePaletteComponents=0,CurrentPoseEvaluations=0,HistoryPoseEvaluations=0,CurrentPoseReuses=0,HistoryPoseReuses=0,HistoryImmutableUploadBytes=0,Components=0,Passes=0,Fallbacks=0,Actors=0,VerifiedComponents=0,VerifiedMatrices=0,VerifiedPreviousMatrices=0,Mismatches=0,ProbeComponents=0,ProbeMatrices=0,ProbePreviousMatrices=0,ProbeMismatches=0,ProbeMaterialSlots=0,CharacterLookups=0,ClipLookups=0,MeshLookups=0,ComponentRebuilds=0;double MaxError=0,ProbeMaxError=0;FString Status;};
// Initial renderer integration retains the native CPU pose as a correctness
// control. Only validated, matching low meshes are overridden on the GPU.
TSharedPtr<FOKCrowdIndependentGpu> OKCreateCrowdIndependentGpu(UWorld*,const FString& Cache,const FString& Identity,bool PackingExperiment,bool Verify=false,const FString& ShardIndex=FString(),TArray<FString> SelectedCharacters={});
// Game-thread entry for APNG runtime producers. Retains the same complete
// character ownership preflight, GPU clock and CPU/GPU comparison path.
TSharedPtr<FOKCrowdIndependentGpu> OKCreateCrowdIndependentGpuFromRuntime(UWorld*,TUniquePtr<FOKGpuAnimationBundle>,TArray<FString> SelectedCharacters,bool Verify=false,uint64 MaxBytes=2ull*1024*1024*1024-1);
void OKTickCrowdIndependentGpu(const TSharedPtr<FOKCrowdIndependentGpu>&,bool Enabled,TConstArrayView<TObjectPtr<AKKImportedCharacterActor>>,TConstArrayView<TObjectPtr<USkeletalMeshComponent>>);
void OKReleaseCrowdIndependentGpu(const TSharedPtr<FOKCrowdIndependentGpu>&);
FOKCrowdIndependentGpuStats OKGetCrowdIndependentGpuStats(const TSharedPtr<FOKCrowdIndependentGpu>&);
FString OKGetCrowdGpuOwnershipAudit(const TSharedPtr<FOKCrowdIndependentGpu>&);
FOKCrowdGpuLoadStats OKGetCrowdGpuLoadStats(const TSharedPtr<FOKCrowdIndependentGpu>&);
// Diagnostic captures replace exactly the supported GPU-only part. Other parts
// remain native and the main bridge is disabled so the CPU reference is real.
bool OKCaptureCrowdIndependentGpuComparison(const TSharedPtr<FOKCrowdIndependentGpu>&,const FString& Prefix,FString& Error);
// Entire native character, fixed independent clip time, identical capture state.
bool OKCaptureCrowdGpuClockComparison(const TSharedPtr<FOKCrowdIndependentGpu>&,const FString& Prefix,FString& Error);

class UAnimSequence;
bool OKCrowdGpuHasClip(const TSharedPtr<FOKCrowdIndependentGpu>&,const AKKImportedCharacterActor*,const UAnimSequence*);
