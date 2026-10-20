#pragma once
#include "OKGpuAnimationCache.h"

inline constexpr uint64 OKGpuAnimationManifestMaxBytes=64ull*1024*1024+52;
struct FOKGpuClipRecord {FString Id;uint32 TrackOffset=0;float Duration=0;};
struct FOKGpuPaletteRange {uint32 First=0,Count=0;};
struct FOKGpuMeshRecord {FString PartId,AssetPath;FOKGpuPaletteRange Bones;TArray<FOKGpuPaletteRange> Sections;};
struct FOKGpuBoneIdentity {FString Name;int32 Parent=INDEX_NONE;FSHAHash ReferenceIdentity;};
struct FOKGpuActorRecord {FString CharacterId;uint32 PoseFirst=0,PoseCount=0;TArray<FOKGpuClipRecord> Clips;TArray<FOKGpuMeshRecord> Meshes;TArray<FOKGpuBoneIdentity> PoseBones;};
struct FReferenceSkeleton;
KKCROWDGPUPACKING_API FSHAHash OKGpuBoneReferenceIdentity(const FTransform&);
// Build a private mesh palette by bone identity; legacy manifests fail closed.
// Failure leaves Out unchanged. No character/skeleton sharing is assumed.
KKCROWDGPUPACKING_API bool OKBuildGpuMeshPalette(const FOKGpuActorRecord&,const FReferenceSkeleton&,TConstArrayView<FMatrix44f> InverseBind,uint32 TotalPoseCount,FOKGpuSkinPaletteData& Out,FString&);
// Sidecar manifest has no hard UObject references. Cache identity links the
// immutable shader bytes to these IDs/ranges; validate both before adoption.
struct KKCROWDGPUPACKING_API FOKGpuAnimationManifest {
 FSHAHash CacheIdentity;
 TArray<FOKGpuActorRecord> Actors;
 bool Validate(const FOKGpuAnimationCacheData&,FString&) const;
};
KKCROWDGPUPACKING_API bool OKEncodeGpuAnimationManifest(const FOKGpuAnimationManifest&,TArray<uint8>&,FString&);
KKCROWDGPUPACKING_API bool OKDecodeGpuAnimationManifest(TConstArrayView<uint8>,const FSHAHash&,FOKGpuAnimationManifest&,FString&);
// Compose independent cache chunks in selected order, rebasing every GPU index
// and manifest range. Inputs retain ownership; failed composition preserves Out.
KKCROWDGPUPACKING_API bool OKComposeGpuAnimationCaches(TConstArrayView<const FOKGpuAnimationCacheData*> Inputs,TConstArrayView<const FOKGpuAnimationManifest*> Manifests,FOKGpuAnimationCacheData& Out,FOKGpuAnimationManifest& OutManifest,FString&,uint64 MaxBytes=4ull*1024*1024*1024);
KKCROWDGPUPACKING_API bool OKSelectGpuAnimationActors(const FOKGpuAnimationCacheData&,const FOKGpuAnimationManifest&,TConstArrayView<uint32> Selection,FOKGpuAnimationCacheData&,FOKGpuAnimationManifest&,FString&);
KKCROWDGPUPACKING_API bool OKRunGpuManifestSelectionOracle(const FString& Output);
struct FOKGpuAnimationBundle {FOKGpuAnimationCacheData Data;FOKGpuAnimationManifest Manifest;};
struct FOKRuntimeGpuClip {FString Id;float Duration=0;TArray<FVector4f> Keys;TArray<FUintVector4> Tracks;};
// Assemble one character's independent runtime clips. Palette/mesh ranges and
// bone identities are supplied by its runtime mesh builder, in local indices.
// Failed construction preserves Out. No UObject or cooked sequence dependency.
KKCROWDGPUPACKING_API bool OKBuildRuntimeGpuActorBank(FOKGpuActorRecord Actor,TArray<FOKRuntimeGpuClip> Clips,FOKGpuSkinPaletteData Palette,FOKGpuAnimationBundle& Out,FString& Error,uint64 MaxBytes=512ull*1024*1024);
// Clip IDs are private to CharacterId. Keep at most active/blending/preload.
struct FOKGpuClipWindow {FString CharacterId;TArray<FString> Clips;};
// Rebuild selected key/track ranges; preserve every rig/palette/material identity.
// Playback starts at the first selected clip/time zero. Caller remaps clocks by ID.
// Invalid input/budget failure preserves Out. This does not prepare missing clips.
KKCROWDGPUPACKING_API bool OKSelectGpuClipWindow(const FOKGpuAnimationBundle& Source,TConstArrayView<FOKGpuClipWindow> Selection,FOKGpuAnimationBundle& Out,FString& Error,uint64 MaxBytes=4ull*1024*1024*1024);
// For validated source banks and windows derived by the selector. Preserve clock
// times/weight while remapping private clip IDs; missing active inputs fail closed.
KKCROWDGPUPACKING_API bool OKRemapGpuClipWindowPlayback(const FOKGpuAnimationManifest& Source,const FOKGpuAnimationManifest& Window,TConstArrayView<FOKGpuPosePlayback> Playback,TArray<FOKGpuPosePlayback>& Out,FString& Error);
struct FOKGpuClipWindowStats {uint64 SourceKeyBytes=0,SelectedKeyBytes=0;double SelectionMs=0;};
struct FOKGpuBundleLoadResult {FOKGpuClipWindowStats Window;TUniquePtr<FOKGpuAnimationBundle> Bundle;FString Error;double LoadMs=0;uint32 LoadedShards=0,MissingSelectedActors=0;bool WorkerThread=false,FromIndex=false;};
// Worker-only plain-data selection; caller retains source until completion.
KKCROWDGPUPACKING_API TFuture<FOKGpuBundleLoadResult> OKSelectGpuClipWindowAsync(TSharedRef<const FOKGpuAnimationBundle,ESPMode::ThreadSafe> Source,TArray<FOKGpuClipWindow> Selection,uint64 MaxBytes=4ull*1024*1024*1024);
// Runtime APNG preparation can publish private banks directly from memory.
// Moves ownership; no asset saving, file reads, UObject access or GPU upload.
// Validation runs on a worker. Exact selected identities are required so stale
// selection results cannot be installed into a different crowd.
KKCROWDGPUPACKING_API TFuture<FOKGpuBundleLoadResult> OKPrepareRuntimeGpuAnimationBundleAsync(TUniquePtr<FOKGpuAnimationBundle> Bundle,TArray<FString> SelectedCharacters,uint64 MaxBytes=4ull*1024*1024*1024);
// Read the two bounded, identity-linked files on a worker, validate their joint
// semantics before publication. The caller supplies the trusted cache identity.
KKCROWDGPUPACKING_API TFuture<FOKGpuBundleLoadResult> OKLoadGpuAnimationBundleAsync(FString CachePath,FString ManifestPath,FSHAHash Identity,uint64 MaxBytes=512ull*1024*1024);
// Load only selected runtime character IDs from a trusted, bounded shard index.
// Missing selections remain native; malformed selected shards reject the batch.
KKCROWDGPUPACKING_API TFuture<FOKGpuBundleLoadResult> OKLoadSelectedGpuAnimationShardsAsync(FString IndexPath,FSHAHash IndexIdentity,TArray<FString> SelectedCharacters,uint64 MaxBytes=4ull*1024*1024*1024);
