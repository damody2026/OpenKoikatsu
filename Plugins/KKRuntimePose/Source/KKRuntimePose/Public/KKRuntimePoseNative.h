#pragma once
#include "CoreMinimal.h"
#include "Async/Future.h"
#include "KKRuntimePoseBankTypes.h"
#include "KKRuntimeMesh.h"
#include "KKRuntimeMaterial.h"
// Empty KKS root; the APNG and the game's shipped independent resources are
// the source. Does not switch the legacy preview renderer/environment mode.
KKRUNTIMEPOSE_API bool OKOpenRuntimePoseSnapshot(const FString& Apng,int32 Outfit,TArray<uint8>& Snapshot,FString& Error);
struct FKKRuntimeSnapshotResult {TArray<uint8> Snapshot;TArray<FKKRuntimePoseBone> Bones;TArray<FKKRuntimePalettePart> Parts;TArray<FKKRuntimeMeshPart> Meshes;FKKRuntimeMaterialData Materials;FString RigIdentity,PersistentSourceIdentity,Error;double NativeOpenMs=0,RigCaptureMs=0,MeshCaptureMs=0,MaterialCaptureMs=0,PrepareMs=0;bool WorkerThread=false;float WalkSpeed=0;};
// WalkSpeed (OKOpenRuntimeCharacterSnapshotAsync): how fast the initialization clip, a walk, carries her (m/s; 0 unknown).
// A shipped curve initializes private renderers before palette discovery.
// RigIdentity hashes the actual snapshot bytes, not a separate file read.
KKRUNTIMEPOSE_API TFuture<FKKRuntimeSnapshotResult> OKOpenRuntimePoseSnapshotAsync(FString Apng,int32 Outfit,TArray<uint8> InitializationCurves);

KKRUNTIMEPOSE_API float OKRuntimeSharedClipDuration(const TArray<uint8>& Curves);

// Combined runtime source capture; one APNG read, owned visible geometry and neutral rig snapshot.
KKRUNTIMEPOSE_API TFuture<FKKRuntimeSnapshotResult> OKOpenRuntimeCharacterSnapshotAsync(FString Apng,int32 Outfit,TArray<uint8> InitializationCurves,uint64 MaxMeshBytes=256ull*1024*1024,bool CaptureFacePatterns=false);
