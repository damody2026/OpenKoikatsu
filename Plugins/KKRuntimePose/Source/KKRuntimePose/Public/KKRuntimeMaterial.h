#pragma once
#include "CoreMinimal.h"
class UTexture2D;
inline constexpr int32 OKRuntimeTextureMaxDimension=1024;
struct FKKRuntimeTextureSettings {
 int32 Filter=1,WrapU=0,WrapV=0;bool SRGB=true,EncodedMips=false;
 bool operator==(const FKKRuntimeTextureSettings& B)const{return Filter==B.Filter&&WrapU==B.WrapU&&WrapV==B.WrapV&&SRGB==B.SRGB&&EncodedMips==B.EncodedMips;}
};
struct FKKRuntimeTextureMip {int32 Width=0,Height=0;TArray<uint8> BGRA;};
struct FKKRuntimePreparedTexture {
 FString SourceHash;FKKRuntimeTextureSettings Settings;TArray<FKKRuntimeTextureMip> Mips;
 // Hash of the settings and every mip's pixels (the GPU texture cache key), computed where the mips are built: hashing
 // them again on the game thread cost 20-40 ms per large texture, for every character the reader built.
 FString PixelIdentity;
 int32 SourceWidth=0,SourceHeight=0;
};
struct FKKRuntimeMaterialTexture {FString Property;int32 Texture=INDEX_NONE;};
struct FKKRuntimeMaterialPart {
 int32 NativePart=INDEX_NONE,ShaderKind=0,Unlit=0,ItemDraw=0,EyeDraw=0;bool Outline=false;
 FString Name,Role,Blend,SourceMetadataJson;
 TMap<FString,FVector4f> Values;TArray<FKKRuntimeMaterialTexture> Textures;
 // Material properties the host sets over the source material's (Unity names and values, as Material.SetColor /
 // SetFloat would: OK Studio item colours). Empty for card parts.
 TMap<FString,FLinearColor> Overrides;
};
struct FKKRuntimeMaterialData {
 TArray<FKKRuntimeMaterialPart> Parts;TArray<FKKRuntimePreparedTexture> Textures;
 uint64 EncodedBytes=0,PixelBytes=0;double PrepareMs=0;bool WorkerThread=false;
};
struct OkSession;
struct FKKRuntimePalettePart;
KKRUNTIMEPOSE_API bool OKCaptureRuntimeMaterials(OkSession*,TConstArrayView<FKKRuntimePalettePart>,FKKRuntimeMaterialData&,FString& Error);
// The same, every distinct texture decoded and mipped in parallel (what OK_FIRSTLOAD=C5 makes the default): for a load
// the player waits on, off the game thread (the one-on-one scene's high model).
KKRUNTIMEPOSE_API bool OKCaptureRuntimeMaterialsParallel(OkSession*,TConstArrayView<FKKRuntimePalettePart>,FKKRuntimeMaterialData&,FString& Error);
// One part's description (source metadata and card-resolved values) without decoding its textures: what an own-graph
// material needs to run the part's installed programs (OKMakeRuntimeOutlineVertexOffset).
KKRUNTIMEPOSE_API bool OKDescribeRuntimeMaterialPart(OkSession*,int32 NativePart,FKKRuntimeMaterialPart&,FString& Error);
// Game thread, no image decoding or material compilation. Caller retains UObject.
// Returned textures are immutable and may be shared across characters. A caller
// needing editable pixels must create a private texture rather than mutate these.
KKRUNTIMEPOSE_API UTexture2D* OKCreateRuntimePreparedTexture(const FKKRuntimePreparedTexture&,FString& Error);
struct FKKRuntimeTextureCacheStats {uint64 Requests=0,Hits=0,Created=0,AvoidedPixelBytes=0,PreparedCacheHits=0,PreparedDecodeJobs=0,PreparedJoinedJobs=0;uint32 Entries=0;};
KKRUNTIMEPOSE_API FKKRuntimeTextureCacheStats OKRuntimeTextureCacheStats();

// Plain-data mip preparation; encoded native render textures keep byte-space averaging.
KKRUNTIMEPOSE_API bool OKBuildRuntimeTextureMips(TArray<uint8> BGRA,int32 Width,int32 Height,
 const FKKRuntimeTextureSettings&,TArray<FKKRuntimeTextureMip>& Out,FString& Error);
struct FKKRuntimeMaterialStats {uint32 Parts=0,Slots=0,Textures=0,Mips=0,SharedInstances=0,UnsupportedParts=0,OpaqueInstances=0,AlphaMaskedInstances=0,AlphaTranslucentInstances=0,ItemInstances=0,HairInstances=0,HairFrontInstances=0,StudioAlphaInstances=0,EyeWhiteInstances=0,EyeInstances=0,KoikanoEyeWhiteInstances=0,KoikanoEyeInstances=0,NoseInstances=0,AdditionalInstances=0;bool SharedParentUsed=false,SourceRasterVerified=false,SourceMeshShadersVerified=false,SourceMeshPixelsVerified=false,SourceMeshRasterPixelsVerified=false;uint64 PixelBytes=0;double PrepareMs=0;};
