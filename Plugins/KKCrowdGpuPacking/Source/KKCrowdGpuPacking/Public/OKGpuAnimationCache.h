#pragma once
#include "OKGpuSkinPalette.h"
#include "Misc/SecureHash.h"
#include "Async/Future.h"

// Versioned shader-data cache; source/mesh manifest identity is supplied by the
// caller. No UObject references, decompression or GPU operations during loading.
struct KKCROWDGPUPACKING_API FOKGpuAnimationCacheData {
 FOKGpuPoseBatch Pose;
 FOKGpuSkinPaletteData Palette;
 TArray<TArray<uint32>> OwnedClips;
 bool Validate(FString&) const;
};
// May be called on an IO worker. Byte budget checked before array allocations;
// failed loads preserve the caller's old data. SHA1 is integrity, not security.
KKCROWDGPUPACKING_API bool OKEncodeGpuAnimationCache(const FOKGpuAnimationCacheData&,const FSHAHash& SourceIdentity,TArray<uint8>&,FString&,uint64 MaxBytes=512ull*1024*1024);
KKCROWDGPUPACKING_API bool OKDecodeGpuAnimationCache(TConstArrayView<uint8>,const FSHAHash& ExpectedSourceIdentity,FOKGpuAnimationCacheData&,FString&,uint64 MaxBytes=512ull*1024*1024);
KKCROWDGPUPACKING_API bool OKSaveGpuAnimationCache(const FString&,const FOKGpuAnimationCacheData&,const FSHAHash&,FString&,uint64 MaxBytes=512ull*1024*1024);
KKCROWDGPUPACKING_API bool OKLoadGpuAnimationCache(const FString&,const FSHAHash&,FOKGpuAnimationCacheData&,FString&,uint64 MaxBytes=512ull*1024*1024);
struct FOKGpuCacheLoadResult {TUniquePtr<FOKGpuAnimationCacheData> Data;FString Error;double LoadMs=0;bool WorkerThread=false;};
// Value-captured request; caller checks its actor/selection generation before
// adopting the result. Never wait on this future in the gameplay frame loop.
KKCROWDGPUPACKING_API TFuture<FOKGpuCacheLoadResult> OKLoadGpuAnimationCacheAsync(FString Path,FSHAHash Identity,uint64 MaxBytes=512ull*1024*1024);
