#include "KKTextureRuntime.h"
#include "ok_extract.h"
#include "Engine/Texture2D.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Modules/ModuleManager.h"
#include "Misc/SecureHash.h"
#include "Compression/lz4.h"
#include "Misc/ScopeLock.h"
#include "UObject/ObjectKey.h"
#include "ok_raw_texture.h"

static void ExtendEncodedMipChain(TArray<FOKEncodedMip>& Mips);
TArray<FOKEncodedMip> OKBuildEncodedMipChain(const TArray<uint8>& BGRA, int32 Width, int32 Height)
{
    TArray<FOKEncodedMip> Mips;
    if (Width <= 0 || Height <= 0 || int64(Width) * Height * 4 != BGRA.Num()) return Mips;
    FOKEncodedMip Top; Top.Width=Width; Top.Height=Height; Top.Bytes=BGRA; Mips.Add(MoveTemp(Top));
    ExtendEncodedMipChain(Mips);
    return Mips;
}

// The chain below Mips[0] (the top level already in place, moved in by the caller).
static void ExtendEncodedMipChain(TArray<FOKEncodedMip>& Mips)
{
    while (Mips.Last().Width > 1 || Mips.Last().Height > 1) {
        const FOKEncodedMip& Src=Mips.Last();
        FOKEncodedMip Dst; Dst.Width=FMath::Max(1, Src.Width/2); Dst.Height=FMath::Max(1, Src.Height/2);
        Dst.Bytes.SetNumUninitialized(Dst.Width*Dst.Height*4);
        if(Src.Width==Dst.Width*2 && Src.Height==Dst.Height*2){
            // For an exact 2:1 reduction, normalized bilinear sampling is
            // the four encoded texels' mean. Integer division preserves the
            // native floor rule, including alpha=127 at a half-coverage edge.
            for(int32 Y=0;Y<Dst.Height;++Y)for(int32 X=0;X<Dst.Width;++X){
                const int32 A=(Y*2*Src.Width+X*2)*4,B=A+Src.Width*4,D=(Y*Dst.Width+X)*4;
                for(int32 C=0;C<4;++C)Dst.Bytes[D+C]=uint8((uint32(Src.Bytes[A+C])+Src.Bytes[A+4+C]+Src.Bytes[B+C]+Src.Bytes[B+4+C])/4);
            }
            Mips.Add(MoveTemp(Dst));continue;
        }
        for (int32 Y=0; Y<Dst.Height; ++Y) for (int32 X=0; X<Dst.Width; ++X) {
            // Texel centers also handle odd and 1xN textures, matching a
            // normalized-UV linear minification blit instead of cropping edges.
            const float SX=(X+0.5f)*Src.Width/Dst.Width-0.5f, SY=(Y+0.5f)*Src.Height/Dst.Height-0.5f;
            const int32 X0=FMath::Clamp(FMath::FloorToInt(SX),0,Src.Width-1), X1=FMath::Min(X0+1,Src.Width-1);
            const int32 Y0=FMath::Clamp(FMath::FloorToInt(SY),0,Src.Height-1), Y1=FMath::Min(Y0+1,Src.Height-1);
            const float FX=SX-FMath::FloorToFloat(SX), FY=SY-FMath::FloorToFloat(SY);
            for (int32 C=0; C<4; ++C) {
                auto P=[&](int32 PX,int32 PY){return float(Src.Bytes[(PY*Src.Width+PX)*4+C]);};
                const float V=FMath::Lerp(FMath::Lerp(P(X0,Y0),P(X1,Y0),FX),FMath::Lerp(P(X0,Y1),P(X1,Y1),FX),FY);
                // The native D3D11 RenderTexture mip1 writes the encoded
                // 127.5 average as 127. Rounding to 128 changes the result
                // of a 0.5 alpha clip on masked hair and clothing.
                Dst.Bytes[(Y*Dst.Width+X)*4+C]=uint8(FMath::Clamp(FMath::FloorToInt(V),0,255));
            }
        }
        Mips.Add(MoveTemp(Dst));
    }
}

static void OKApplyRuntimeTextureSettings(UTexture2D* Tex,const OkTextureSettings* Settings,bool bFallbackClamp)
{
    auto Wrap=[](int32 Mode){return Mode==1?TA_Clamp:Mode==2?TA_Mirror:TA_Wrap;};
    Tex->SRGB=Settings?Settings->srgb!=0:true;
    Tex->Filter=Settings&&Settings->filter==0?TF_Nearest:Settings&&Settings->filter==2?TF_Trilinear:TF_Bilinear;
    Tex->AddressX=Settings?Wrap(Settings->wrap_u):(bFallbackClamp?TA_Clamp:TA_Wrap);
    Tex->AddressY=Settings?Wrap(Settings->wrap_v):(bFallbackClamp?TA_Clamp:TA_Wrap);
    Tex->NeverStream=true;
    const double UploadStart=FPlatformTime::Seconds();Tex->UpdateResource();OKTextureCreateStats().UploadSeconds+=FPlatformTime::Seconds()-UploadStart;
}

static UTexture2D* CreatePreparedTexture(const TArray<FOKEncodedMip>& Mips,
    const OkTextureSettings* Settings,bool bFallbackClamp)
{
    if(Mips.IsEmpty())return nullptr;
    UTexture2D* Tex=UTexture2D::CreateTransient(Mips[0].Width,Mips[0].Height,PF_B8G8R8A8);
    if(!Tex)return nullptr;
    for (int32 I=0; I<Mips.Num(); ++I) {
        FTexture2DMipMap* Mip=I==0 ? &Tex->GetPlatformData()->Mips[0] : new FTexture2DMipMap();
        if (I>0) Tex->GetPlatformData()->Mips.Add(Mip);
        Mip->SizeX=Mips[I].Width;Mip->SizeY=Mips[I].Height;Mip->SizeZ=1;
        Mip->BulkData.Lock(LOCK_READ_WRITE);
        void* Data=Mip->BulkData.Realloc(Mips[I].Bytes.Num());
        FMemory::Memcpy(Data,Mips[I].Bytes.GetData(),Mips[I].Bytes.Num());Mip->BulkData.Unlock();
    }
    OKApplyRuntimeTextureSettings(Tex,Settings,bFallbackClamp);return Tex;
}

UTexture2D* OKCreateRuntimeTexture(const TArray<uint8>& BGRA,int32 Width,int32 Height,
    const OkTextureSettings* Settings,bool bFallbackClamp)
{
 if(Width<=0||Height<=0||int64(Width)*Height*4!=BGRA.Num())return nullptr;
 TArray<FOKEncodedMip> Mips;
 if(Settings&&Settings->mip_policy==1)Mips=OKBuildEncodedMipChain(BGRA,Width,Height);
 else {FOKEncodedMip Top;Top.Width=Width;Top.Height=Height;Top.Bytes=BGRA;Mips.Add(MoveTemp(Top));}
 return CreatePreparedTexture(Mips,Settings,bFallbackClamp);
}
namespace {
struct FPackedMip {int32 Width=0,Height=0,RawBytes=0;bool Compressed=false;TArray<uint8> Payload;};
struct FPreparedPng {
 TArray<uint8> Source;TArray<FPackedMip> Mips;int64 Bytes=0;uint64 LastUse=0;FString Key;
};
// Kept uncompressed: LZ4 on every mip cost the worker its compression and the game thread a decompression plus an
// extra copy per texture (~15 ms for a 2048 map with mips), and memory is not what bounds the card reader.
TArray<FPackedMip> PackMips(TArray<FOKEncodedMip>&& Mips){
 TArray<FPackedMip> Packed;
 for(auto& Mip:Mips){FPackedMip P;P.Width=Mip.Width;P.Height=Mip.Height;P.RawBytes=Mip.Bytes.Num();
  P.Payload=MoveTemp(Mip.Bytes);P.Payload.Shrink();
  Packed.Add(MoveTemp(P));
 }
 return Packed;
}
UTexture2D* CreatePackedTexture(const FPreparedPng& Entry,const OkTextureSettings* Settings,bool Clamp){
 if(Entry.Mips.IsEmpty())return nullptr;
 // Uncompressed mips go straight into the texture's bulk data (one copy).
 if(!Entry.Mips.ContainsByPredicate([](const FPackedMip& P){return P.Compressed;})){
  // OK_FIRSTLOAD=C2: the top level goes in with the texture's creation (CreateTransient's image data) instead of
  // a zero-filled allocation that is then reallocated and overwritten.
  static const bool bDirectTop=[]{TArray<FString> O;FPlatformMisc::GetEnvironmentVariable(TEXT("OK_FIRSTLOAD")).ParseIntoArray(O,TEXT(","));for(FString& S:O)S.TrimStartAndEndInline();return O.Contains(TEXT("C2"))||O.Contains(TEXT("all"));}();
  UTexture2D* Tex=bDirectTop
   ?UTexture2D::CreateTransient(Entry.Mips[0].Width,Entry.Mips[0].Height,PF_B8G8R8A8,NAME_None,TConstArrayView64<uint8>(Entry.Mips[0].Payload.GetData(),Entry.Mips[0].Payload.Num()))
   :UTexture2D::CreateTransient(Entry.Mips[0].Width,Entry.Mips[0].Height,PF_B8G8R8A8);
  if(!Tex)return nullptr;
  for(int32 I=bDirectTop?1:0;I<Entry.Mips.Num();++I){
   FTexture2DMipMap* Mip=I==0?&Tex->GetPlatformData()->Mips[0]:new FTexture2DMipMap();
   if(I>0)Tex->GetPlatformData()->Mips.Add(Mip);
   Mip->SizeX=Entry.Mips[I].Width;Mip->SizeY=Entry.Mips[I].Height;Mip->SizeZ=1;
   Mip->BulkData.Lock(LOCK_READ_WRITE);
   FMemory::Memcpy(Mip->BulkData.Realloc(Entry.Mips[I].Payload.Num()),Entry.Mips[I].Payload.GetData(),Entry.Mips[I].Payload.Num());
   Mip->BulkData.Unlock();
  }
  OKApplyRuntimeTextureSettings(Tex,Settings,Clamp);
  return Tex;
 }
 TArray<FOKEncodedMip> Mips;
 for(const auto& P:Entry.Mips){FOKEncodedMip M;M.Width=P.Width;M.Height=P.Height;
  if(P.Compressed){M.Bytes.SetNumUninitialized(P.RawBytes);if(LZ4_decompress_safe(reinterpret_cast<const char*>(P.Payload.GetData()),reinterpret_cast<char*>(M.Bytes.GetData()),P.Payload.Num(),P.RawBytes)!=P.RawBytes)return nullptr;}
  else M.Bytes=P.Payload;
  Mips.Add(MoveTemp(M));
 }
 return CreatePreparedTexture(Mips,Settings,Clamp);
}
TMap<FString,TSharedPtr<FPreparedPng,ESPMode::ThreadSafe>> PreparedPngs;
FCriticalSection PngMutex;
FOKPngCacheStats PngStats;uint64 PngClock=0;
// Uncompressed mips (a heavy card holds ~2 GiB of 2048 maps with their chains): an eighth of the physical memory,
// 1..8 GiB. A budget below one card makes its own textures evict each other and decode again on the game thread.
static int64 PngCacheBudgetBytes(){static const int64 Budget=FMath::Clamp<int64>(int64(FPlatformMemory::GetConstants().TotalPhysical/8),1ll<<30,8ll<<30);return Budget;}
}
FOKTextureCreateStats& OKTextureCreateStats(){static FOKTextureCreateStats Stats;return Stats;}
FOKPngCacheStats OKRuntimePngCacheStats(){check(IsInGameThread());FScopeLock Lock(&PngMutex);auto Stats=PngStats;Stats.Entries=PreparedPngs.Num();return Stats;}
void OKClearRuntimePngCache(){check(IsInGameThread());FScopeLock Lock(&PngMutex);PreparedPngs.Reset();PngStats={};PngClock=0;}
static void TrimPngCacheLocked(int64 MaxBytes){
 MaxBytes=FMath::Max<int64>(0,MaxBytes);
 while(PngStats.Bytes>MaxBytes&&!PreparedPngs.IsEmpty()){
  FString Oldest;uint64 Age=MAX_uint64;for(const auto& Pair:PreparedPngs)if(Pair.Value->LastUse<Age){Age=Pair.Value->LastUse;Oldest=Pair.Key;}
  PngStats.Bytes-=PreparedPngs.FindChecked(Oldest)->Bytes;PreparedPngs.Remove(Oldest);
 }
}
void OKTrimRuntimePngCache(int64 MaxBytes){check(IsInGameThread());FScopeLock Lock(&PngMutex);TrimPngCacheLocked(MaxBytes);}
static TSharedPtr<FPreparedPng,ESPMode::ThreadSafe> PreparePng(TConstArrayView<uint8> Png,bool Mipmapped,
 bool& bCacheHit,double& DecodeSeconds)
{
 bCacheHit=false;DecodeSeconds=0.;if(Png.Num()<16)return nullptr;
 FSHAHash Hash;FSHA1::HashBuffer(Png.GetData(),Png.Num(),Hash.Hash);
 const FString Key=Hash.ToString()+FString::Printf(TEXT("/%d/%d"),Png.Num(),Mipmapped?1:0);
 {FScopeLock Lock(&PngMutex);
  auto Cached=PreparedPngs.FindRef(Key);
  // Full compressed-byte equality preserves identity on hash collisions.
  if(Cached&&Cached->Source.Num()==Png.Num()&&!FMemory::Memcmp(Cached->Source.GetData(),Png.GetData(),Png.Num())){bCacheHit=true;++PngStats.Hits;Cached->LastUse=++PngClock;return Cached;}
  ++PngStats.Misses;
 }
 const double Begin=FPlatformTime::Seconds();
 // The caller loads the module on the game thread before dispatching work.
 // PNG, or the raw LZ4 container with OK_RAW_TEXTURES=1.
 TArray<uint8> Raw;int32 Width=0,Height=0;
 if(!OKDecodeSessionTexture(Png.GetData(),Png.Num(),Raw,Width,Height))return nullptr;
 DecodeSeconds=FPlatformTime::Seconds()-Begin;
 auto Entry=MakeShared<FPreparedPng,ESPMode::ThreadSafe>();Entry->Source=TArray<uint8>(Png.GetData(),Png.Num());Entry->Key=Key;
 // The decoded pixels become the top level as they are (no copy), the chain is built under it.
 TArray<FOKEncodedMip> Mips;
 {FOKEncodedMip Top;Top.Width=Width;Top.Height=Height;Top.Bytes=MoveTemp(Raw);Mips.Add(MoveTemp(Top));}
 if(Mipmapped)ExtendEncodedMipChain(Mips);
 Entry->Mips=PackMips(MoveTemp(Mips));
 Entry->Bytes=Entry->Source.Num();for(const auto& Mip:Entry->Mips)Entry->Bytes+=Mip.Payload.Num();
 if(Entry->Bytes<=PngCacheBudgetBytes()){
  FScopeLock Lock(&PngMutex);Entry->LastUse=++PngClock;
  // Collision replacement and LRU eviction account for all retained pixel bytes.
  if(auto Previous=PreparedPngs.FindRef(Key)){PngStats.Bytes-=Previous->Bytes;PreparedPngs.Remove(Key);}
  TrimPngCacheLocked(PngCacheBudgetBytes()-Entry->Bytes);
  PreparedPngs.Add(Key,Entry);PngStats.Bytes+=Entry->Bytes;
 }
 return Entry;
}
void OKPrecacheRuntimePng(const TArray<uint8>& Png,bool Mipmapped){bool Hit=false;double Decode=0;PreparePng(Png,Mipmapped,Hit,Decode);}
void OKPrecacheRuntimePngView(const uint8* Bytes,int32 Length,bool Mipmapped){bool Hit=false;double Decode=0;if(Bytes&&Length>0)PreparePng(TConstArrayView<uint8>(Bytes,Length),Mipmapped,Hit,Decode);}
namespace {
// Texture -> what it was made from (source bytes and sampler settings), for content-keyed material reuse.
TMap<FObjectKey,FString> OKRuntimeTextureContentKeys;
}
FString OKRuntimeTextureContentKey(const UTexture* Texture){
 check(IsInGameThread());const FString* Key=Texture?OKRuntimeTextureContentKeys.Find(FObjectKey(Texture)):nullptr;return Key?*Key:FString();
}
UTexture2D* OKCreateRuntimeTextureFromPng(const TArray<uint8>& Png,const OkTextureSettings* Settings,
 bool bFallbackClamp,bool& bCacheHit,double& DecodeSeconds){
 check(IsInGameThread());FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
 const double LookupStart=FPlatformTime::Seconds();
 auto Entry=PreparePng(Png,Settings&&Settings->mip_policy==1,bCacheHit,DecodeSeconds);
 const double CreateStart=FPlatformTime::Seconds();
 UTexture2D* Tex=Entry?CreatePackedTexture(*Entry,Settings,bFallbackClamp):nullptr;
 auto& Stats=OKTextureCreateStats();Stats.LookupSeconds+=CreateStart-LookupStart;Stats.CreateSeconds+=FPlatformTime::Seconds()-CreateStart;++Stats.Count;
 if(Tex){
  if(OKRuntimeTextureContentKeys.Num()>16384)for(auto It=OKRuntimeTextureContentKeys.CreateIterator();It;++It)if(!It.Key().ResolveObjectPtr())It.RemoveCurrent();
  OKRuntimeTextureContentKeys.Add(FObjectKey(Tex),FString::Printf(TEXT("%s|%d|%d|%d|%d|%d"),*Entry->Key,int32(Tex->SRGB),int32(Tex->Filter),int32(Tex->AddressX),int32(Tex->AddressY),Tex->GetPlatformData()->Mips.Num()));
 }
 return Tex;
}

namespace {
struct FReadonlyTextureEntry {
    TWeakObjectPtr<UTexture2D> Texture;
    TArray<uint8> Source;
    uint64 LastUse=0;
    int32 Mips=0,Width=0,Height=0;
};
TMap<FString,FReadonlyTextureEntry> ReadonlyTextures;
FOKReadonlyTextureCacheStats ReadonlyStats;
uint64 ReadonlyClock=0;
constexpr int64 ReadonlySourceBudget=64ll*1024*1024;
void TrimReadonlyTextures(int64 IncomingBytes)
{
    for(auto It=ReadonlyTextures.CreateIterator();It;++It) if(!It.Value().Texture.IsValid())
    {ReadonlyStats.SourceBytes-=It.Value().Source.Num();It.RemoveCurrent();}
    while(!ReadonlyTextures.IsEmpty() && (ReadonlyStats.SourceBytes+IncomingBytes>ReadonlySourceBudget || ReadonlyTextures.Num()>=4096))
    {
        FString Oldest;uint64 Age=MAX_uint64;
        for(const auto& Pair:ReadonlyTextures) if(Pair.Value.LastUse<Age){Age=Pair.Value.LastUse;Oldest=Pair.Key;}
        ReadonlyStats.SourceBytes-=ReadonlyTextures.FindChecked(Oldest).Source.Num();ReadonlyTextures.Remove(Oldest);
    }
}
}
FOKReadonlyTextureCacheStats OKReadonlyRuntimeTextureCacheStats()
{check(IsInGameThread());auto Stats=ReadonlyStats;Stats.Entries=ReadonlyTextures.Num();return Stats;}
void OKClearReadonlyRuntimeTextureCache()
{check(IsInGameThread());ReadonlyTextures.Reset();ReadonlyStats={};ReadonlyClock=0;}
UTexture2D* OKAcquireReadonlyRuntimeTextureFromPng(const TArray<uint8>& Png,
    const OkTextureSettings* Settings,bool Clamp,bool& Hit,double& Decode)
{
    check(IsInGameThread());Hit=false;Decode=0.;if(Png.Num()<16)return nullptr;
    const auto Wrap=[](int32 Mode){return Mode==1?TA_Clamp:Mode==2?TA_Mirror:TA_Wrap;};
    const bool SRGB=Settings?Settings->srgb!=0:true;
    const TextureFilter Filter=Settings&&Settings->filter==0?TF_Nearest:Settings&&Settings->filter==2?TF_Trilinear:TF_Bilinear;
    const TextureAddress U=Settings?Wrap(Settings->wrap_u):(Clamp?TA_Clamp:TA_Wrap);
    const TextureAddress V=Settings?Wrap(Settings->wrap_v):(Clamp?TA_Clamp:TA_Wrap);
    const bool Mipmapped=Settings&&Settings->mip_policy==1;
    FSHAHash Hash;FSHA1::HashBuffer(Png.GetData(),Png.Num(),Hash.Hash);
    const FString Key=Hash.ToString()+FString::Printf(TEXT("/%d/%d/%d/%d/%d/%d"),Png.Num(),SRGB,int32(Filter),int32(U),int32(V),Mipmapped);
    if(auto* Entry=ReadonlyTextures.Find(Key)) if(Entry->Source==Png) if(auto* Tex=Entry->Texture.Get())
    {
        // Defensive check: a legacy mutable caller must not poison later loads.
        if(Tex->SRGB==SRGB&&Tex->Filter==Filter&&Tex->AddressX==U&&Tex->AddressY==V&&Tex->NeverStream
            &&Tex->GetPlatformData()&&Tex->GetPlatformData()->Mips.Num()==Entry->Mips
            &&Tex->GetSizeX()==Entry->Width&&Tex->GetSizeY()==Entry->Height)
        {Entry->LastUse=++ReadonlyClock;++ReadonlyStats.Hits;Hit=true;return Tex;}
        ++ReadonlyStats.MetadataRejections;
    }
    ++ReadonlyStats.Misses;
    bool DecodeHit=false;
    auto* Tex=OKCreateRuntimeTextureFromPng(Png,Settings,Clamp,DecodeHit,Decode);
    if(Tex&&Png.Num()<=ReadonlySourceBudget)
    {
        if(auto* Previous=ReadonlyTextures.Find(Key)){ReadonlyStats.SourceBytes-=Previous->Source.Num();ReadonlyTextures.Remove(Key);}
        TrimReadonlyTextures(Png.Num());
        FReadonlyTextureEntry Entry;Entry.Texture=Tex;Entry.Source=Png;Entry.LastUse=++ReadonlyClock;
        Entry.Mips=Tex->GetPlatformData()->Mips.Num();Entry.Width=Tex->GetSizeX();Entry.Height=Tex->GetSizeY();
        ReadonlyStats.SourceBytes+=Entry.Source.Num();ReadonlyTextures.Add(Key,MoveTemp(Entry));
    }
    return Tex;
}
