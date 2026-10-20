#pragma once
#include "CoreMinimal.h"
class UTexture2D;
struct OkTextureSettings;
struct FOKEncodedMip {
    int32 Width = 0, Height = 0;
    TArray<uint8> Bytes;
};
// Unity/D3D runtime GenerateMips samples encoded channels, including alpha.
TArray<FOKEncodedMip> OKBuildEncodedMipChain(const TArray<uint8>& BGRA, int32 Width, int32 Height);
UTexture2D* OKCreateRuntimeTexture(const TArray<uint8>& BGRA, int32 Width, int32 Height,
    const OkTextureSettings* Settings, bool bFallbackClamp);

struct FOKPngCacheStats { uint64 Hits=0,Misses=0; int64 Bytes=0; int32 Entries=0; };
// Cache immutable decoded texels/mips only. Each caller gets an independent
// UTexture2D, so actor-specific sampler/color changes cannot leak across cards.
UTexture2D* OKCreateRuntimeTextureFromPng(const TArray<uint8>& Png,const OkTextureSettings* Settings,
    bool bFallbackClamp,bool& bCacheHit,double& DecodeSeconds);
FOKPngCacheStats OKRuntimePngCacheStats();
// Game-thread cost of OKCreateRuntimeTextureFromPng split into the prepared-entry lookup (hash) and the texture
// object creation (allocation, mip copies, UpdateResource). Reset per load by the profiler.
struct FOKTextureCreateStats { double LookupSeconds = 0, CreateSeconds = 0, UploadSeconds = 0; int32 Count = 0; };
FOKTextureCreateStats& OKTextureCreateStats();
// What a texture from OKCreateRuntimeTextureFromPng was made of (source hash, size, sampler settings); empty for
// other textures. Equal keys mean equal texels and sampling (KKMaterialDedupe reuses materials by it).
FString OKRuntimeTextureContentKey(const class UTexture* Texture);
void OKClearRuntimePngCache();
void OKTrimRuntimePngCache(int64 MaxBytes);
// Worker-safe immutable pixel preparation; never creates or edits UObjects.
void OKPrecacheRuntimePng(const TArray<uint8>& Png, bool Mipmapped);
// Same, reading the bytes in place (they must stay valid during the call; the cache keeps its own copy).
void OKPrecacheRuntimePngView(const uint8* Bytes, int32 Length, bool Mipmapped);

// Opt-in immutable GPU-resource reuse. Mutable callers must keep using the
// independent factory above. The cache owns no UObjects/GPU memory.
UTexture2D* OKAcquireReadonlyRuntimeTextureFromPng(const TArray<uint8>& Png,
    const OkTextureSettings* Settings,bool bFallbackClamp,bool& bCacheHit,double& DecodeSeconds);
struct FOKReadonlyTextureCacheStats {uint64 Hits=0,Misses=0,MetadataRejections=0;int64 SourceBytes=0;int32 Entries=0;};
FOKReadonlyTextureCacheStats OKReadonlyRuntimeTextureCacheStats();
void OKClearReadonlyRuntimeTextureCache();
