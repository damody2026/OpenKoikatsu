#pragma once
#include "CoreMinimal.h"
#include "Async/Future.h"

// Plain input copied from the runtime character builder on the game thread.
// Workers never inspect skeletal mesh UObjects or save imported assets.
struct FKKRuntimePoseBone {FName Name;int32 Parent=INDEX_NONE;FTransform Reference;};
// The source head's tear renderers (ChaControl objTears: cf_O_namida_S/M/L for tearsLv 1/2/3); 0 for any other.
inline int32 OKRuntimeTearLevel(const FString& Name){
 if(!Name.StartsWith(TEXT("cf_O_namida_"),ESearchCase::IgnoreCase))return 0;
 const TCHAR Size=FChar::ToUpper(Name.Len()>12?Name[12]:TEXT(' '));return Size==TEXT('S')?1:Size==TEXT('M')?2:Size==TEXT('L')?3:0;
}
inline bool OKRuntimeIsTearRenderer(const char* Name){return Name&&OKRuntimeTearLevel(UTF8_TO_TCHAR(Name))>0;}
struct FKKRuntimePalettePart {
 int32 NativePart=INDEX_NONE;TArray<int32> Slots;
 FString RigidFollow;FMatrix RigidBindWorld=FMatrix::Identity;
};
struct FKKRuntimePoseRequest {
 TArray<uint8> Snapshot,AuthoredCurves;
 TArray<FKKRuntimePoseBone> Bones;TArray<FKKRuntimePalettePart> Parts;
 FString CharacterIdentity,ClipIdentity;
 // Ceiling for one clip's CPU keys (bones x samples x 3 rows x 16 B; allocated at their exact size). Rigs with many
 // accessory bones need ~150 MB for a long clip at 60 Hz (R3-10: 972 palette slots); the shared 4 GiB GPU residency
 // budget is separate.
 float Duration=0;uint32 SampleHz=60;uint64 MaxKeyBytes=512ull*1024*1024;
 // Opt in to packed GPU-native results/disk reuse. Legacy CPU probes retain
 // their original interleaved quaternion/scale layout.
 bool PackedGpuCache=false;
 // Explicit prefix sampling; Duration remains bounded by the authored clip.
 bool AuthoredPrefix=false;
 // Stable APNG content/outfit identity from a verified capture. Snapshot byte
 // order is not canonical because native serialization contains hash maps.
 FString PersistentSourceIdentity;
 // Optional bounded common contact recipe; never contains per-card geometry.
 FString ContactRecipe;
};
struct FKKRuntimePoseResult {
 // Same immutable key/track layout consumed by IndependentPose.usf.
 TArray<FVector4f> Keys;TArray<FUintVector4> Tracks;
 FString CharacterIdentity,ClipIdentity,PreparationIdentity,Error;
 float Duration=0;uint32 Samples=0,ConstantTracks=0;
 double PrepareMs=0;bool WorkerThread=false;
 bool PersistentCacheHit=false;uint64 PersistentCacheBytes=0;
 bool Passed()const{return Error.IsEmpty()&&!Tracks.IsEmpty()&&!Keys.IsEmpty();}
};
// One independent native session per job. No environment overrides, UObject
// animation controller, compression/DDC, SavePackage or render work.
KKRUNTIMEPOSE_API TFuture<FKKRuntimePoseResult> OKPrepareRuntimePoseBankAsync(FKKRuntimePoseRequest Request);

KKRUNTIMEPOSE_API FString OKRuntimePoseRecipeIdentity(const FKKRuntimePoseRequest& Request);
