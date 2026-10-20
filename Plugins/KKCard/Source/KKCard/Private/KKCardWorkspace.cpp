#include "KKCardWorkspace.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Serialization/Archive.h"

namespace
{
    uint32 Be32(const uint8* P) { return (uint32(P[0])<<24)|(uint32(P[1])<<16)|(uint32(P[2])<<8)|P[3]; }
    bool IsSnapshot(const uint8* Type) { return FMemory::Memcmp(Type,"okPv",4)==0; }
    // okLb: an OK base library card (shared resources come from the library).
    bool IsLibraryChunk(const uint8* Type) { return FMemory::Memcmp(Type,"okLb",4)==0; }
    // Card data chunks that are no part of the image (card pack, baked open, recorded memos; tens of MB): the
    // preview decode skips them instead of reading and checksumming them through libpng.
    bool IsDataChunk(const uint8* Type) { return FMemory::Memcmp(Type,"okLc",4)==0||FMemory::Memcmp(Type,"okBk",4)==0||FMemory::Memcmp(Type,"okMm",4)==0; }
}

FString UKKCardWorkspace::PortableSourcePath(const FString& CardPath)
{
    TUniquePtr<FArchive> File(IFileManager::Get().CreateFileReader(*CardPath));
    if(File && File->TotalSize()>=20)
    {
        uint8 Signature[8];File->Serialize(Signature,8);
        if(FMemory::Memcmp(Signature,"\x89PNG\r\n\x1a\n",8)==0)
        {
            for(int64 At=8;At<=File->TotalSize()-12;)
            {
                uint8 Header[8];File->Seek(At);File->Serialize(Header,8);
                const int64 Next=At+12+Be32(Header);
                if(File->IsError()||Next>File->TotalSize()) break;
                if(IsSnapshot(Header+4)) return CardPath;
                if(IsLibraryChunk(Header+4)&&!UKKCardWorkspace::LibraryRoot().IsEmpty()) return CardPath;
                if(FMemory::Memcmp(Header+4,"IEND",4)==0) break;
                At=Next;
            }
        }
    }
    const FString Pack=FPaths::ChangeExtension(CardPath,TEXT("okpreview"));
    return FPaths::FileExists(Pack)?Pack:FString();
}

FString UKKCardWorkspace::LoadableSourcePath(const FString& CardPath)
{
    const FString Portable=PortableSourcePath(CardPath);
    if(!Portable.IsEmpty()||LibraryRoot().IsEmpty()) return Portable;
    // An original card: a PNG whose IEND is followed by the game's card data.
    TUniquePtr<FArchive> File(IFileManager::Get().CreateFileReader(*CardPath));
    if(!File || File->TotalSize()<20) return FString();
    uint8 Signature[8];File->Serialize(Signature,8);
    if(FMemory::Memcmp(Signature,"\x89PNG\r\n\x1a\n",8)!=0) return FString();
    for(int64 At=8;At<=File->TotalSize()-12;)
    {
        uint8 Header[8];File->Seek(At);File->Serialize(Header,8);
        const int64 Next=At+12+Be32(Header);
        if(File->IsError()||Next>File->TotalSize()) return FString();
        if(FMemory::Memcmp(Header+4,"IEND",4)==0) return Next<File->TotalSize()?CardPath:FString();
        At=Next;
    }
    return FString();
}

FString UKKCardWorkspace::LibraryRoot()
{
    const FString Root=FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir()/TEXT("IndependentResources/base_v1"));
    return FPaths::FileExists(Root/TEXT("oklib.json"))?Root:FString();
}

TArray<uint8> UKKCardWorkspace::PngPreviewBytes(const TArray<uint8>& Png)
{
    TArray<uint8> Out;
    if(Png.Num()<8) return Out;
    Out.Append(Png.GetData(),8);
    for(int64 At=8;At<=int64(Png.Num())-12;)
    {
        const int64 Next=At+12+Be32(Png.GetData()+At);
        if(Next>Png.Num()) return TArray<uint8>();
        if(!IsSnapshot(Png.GetData()+At+4)&&!IsLibraryChunk(Png.GetData()+At+4)&&!IsDataChunk(Png.GetData()+At+4)) Out.Append(Png.GetData()+At,int32(Next-At));
        if(FMemory::Memcmp(Png.GetData()+At+4,"IEND",4)==0) return Out;
        At=Next;
    }
    return TArray<uint8>();
}

FString UKKCardWorkspace::NewCardPath(const FString& Directory, const FString& SourcePath)
{
	const FString Stem = FPaths::GetBaseFilename(SourcePath) + TEXT("_edited");
	FString Path = FPaths::Combine(Directory, Stem + TEXT(".png"));
	for (int32 Suffix = 2; IFileManager::Get().FileExists(*Path) || FPaths::FileExists(FPaths::ChangeExtension(Path,TEXT("okpreview"))); ++Suffix)
	{
		Path = FPaths::Combine(Directory, FString::Printf(TEXT("%s_%d.png"), *Stem, Suffix));
	}
	return Path;
}
