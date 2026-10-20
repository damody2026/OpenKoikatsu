// Unreal-side reader for ok-extract's raw texture container (src/teximg.rs, OK_RAW_TEXTURES=1):
// "OKRAW\0\2\0", width and height as little-endian uint32, then one LZ4 block of RGBA8 rows (top first).
// Texture bytes from a session are either this or PNG; OKDecodeSessionTexture handles both.
#pragma once
#include "CoreMinimal.h"
#include "Compression/lz4.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Modules/ModuleManager.h"

inline bool OKIsRawTexture(const uint8* Bytes,int64 Length)
{
	static const uint8 Magic[8]={'O','K','R','A','W',0,2,0};
	return Bytes&&Length>=16&&FMemory::Memcmp(Bytes,Magic,8)==0;
}

// BGRA8 pixels of a raw container.
inline bool OKDecodeRawTexture(const uint8* Bytes,int64 Length,TArray<uint8>& BGRA,int32& Width,int32& Height)
{
	if(!OKIsRawTexture(Bytes,Length))return false;
	uint32 W=0,H=0;FMemory::Memcpy(&W,Bytes+8,4);FMemory::Memcpy(&H,Bytes+12,4);
	if(W==0||H==0||uint64(W)*H>(1ull<<28))return false;
	const int32 N=int32(uint64(W)*H*4);BGRA.SetNumUninitialized(N);
	if(LZ4_decompress_safe(reinterpret_cast<const char*>(Bytes+16),reinterpret_cast<char*>(BGRA.GetData()),int32(Length-16),N)!=N)return false;
	for(uint8* P=BGRA.GetData(),*E=P+N;P<E;P+=4)Swap(P[0],P[2]);
	Width=int32(W);Height=int32(H);return true;
}

// BGRA8 pixels of session texture bytes (raw container or PNG). The ImageWrapper module must already be loaded
// when this runs off the game thread.
inline bool OKDecodeSessionTexture(const uint8* Bytes,int64 Length,TArray<uint8>& BGRA,int32& Width,int32& Height)
{
	if(OKIsRawTexture(Bytes,Length))return OKDecodeRawTexture(Bytes,Length,BGRA,Width,Height);
	auto& Module=FModuleManager::GetModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
	TSharedPtr<IImageWrapper> Wrapper=Module.CreateImageWrapper(EImageFormat::PNG);
	if(!Wrapper||!Wrapper->SetCompressed(Bytes,Length)||!Wrapper->GetRaw(ERGBFormat::BGRA,8,BGRA))return false;
	Width=Wrapper->GetWidth();Height=Wrapper->GetHeight();
	return Width>0&&Height>0&&int64(Width)*Height*4==BGRA.Num();
}
