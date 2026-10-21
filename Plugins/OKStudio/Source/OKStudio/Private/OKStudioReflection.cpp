#include "OKStudioSky.h"
#include "OKSceneActor.h"
#include "Engine/TextureCube.h"
#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/StrongObjectPtr.h"

UTextureCube* OKStudioMapReflection(int32 Map,FLinearColor& OutHDR,const FString& Prefab,int32 Node)
{
    static FString Loaded;
    static TMap<FString,FString> Files;
    static TMap<FString,FLinearColor> Decode;
    static TMap<FString,TStrongObjectPtr<UTextureCube>> Cubes;
    const FString Root=AOKSceneActor::LibraryDirectory();
    if(Loaded!=Root)
    {
        Loaded=Root;Files.Reset();Decode.Reset();Cubes.Reset();
        FString Text,Format;TSharedPtr<FJsonObject> Document;const TArray<TSharedPtr<FJsonValue>>* Maps=nullptr;
        if(FFileHelper::LoadFileToString(Text,*(Root/TEXT("studio/map_reflections.json")))
            && FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Document) && Document
            && Document->TryGetStringField(TEXT("format"),Format) && Format==TEXT("ok-studio-map-reflections-1") && Document->TryGetArrayField(TEXT("maps"),Maps))
        {
            auto Binding=[&](const FString& Key,const TSharedPtr<FJsonObject>& Row){
                FString File;const TArray<TSharedPtr<FJsonValue>>* HDR=nullptr;
                if(!Row||!Row->TryGetStringField(TEXT("texture"),File)||!File.StartsWith(TEXT("studio/reflections/"))||File.Contains(TEXT(".."))||!File.EndsWith(TEXT(".okcube"))||!Row->TryGetArrayField(TEXT("hdr"),HDR)||HDR->Num()!=4)return;
                FLinearColor H;for(int32 C=0;C<4;++C){double N=0;if(!(*HDR)[C]->TryGetNumber(N)||!FMath::IsFinite(N))return;(&H.R)[C]=float(N);}
                Files.Add(Key,File);Decode.Add(Key,H);
            };
            for(const auto& V:*Maps)
            {
                const auto Row=V->AsObject();double Id=0;if(!Row||!Row->TryGetNumberField(TEXT("map"),Id))continue;
                Binding(FString::Printf(TEXT("%d||-1"),int32(Id)),Row);
                const TSharedPtr<FJsonObject>* Roots=nullptr;
                if(Row->TryGetObjectField(TEXT("roots"),Roots))for(const auto& RootRow:(*Roots)->Values)
                {
                    const TArray<TSharedPtr<FJsonValue>>* Probes=nullptr;if(!RootRow.Value->TryGetArray(Probes))continue;
                    for(const auto& Probe:*Probes){const auto Object=Probe->AsObject();double ProbeNode=0;if(Object&&Object->TryGetNumberField(TEXT("node"),ProbeNode))Binding(FString::Printf(TEXT("%d|%s|%d"),int32(Id),*RootRow.Key,int32(ProbeNode)),Object);}
                }
            }
        }
    }
    OutHDR=FLinearColor(1,1,0,0);
    const FString Key=Node<0?FString::Printf(TEXT("%d||-1"),Map):FString::Printf(TEXT("%d|%s|%d"),Map,*Prefab,Node);
    const FString* File=Files.Find(Key);if(!File)return nullptr;
    OutHDR=Decode.FindRef(Key);
    if(const auto* Found=Cubes.Find(*File))return Found->Get();
    auto Fail=[&]()->UTextureCube*{UE_LOG(LogTemp,Warning,TEXT("OK Studio reflection invalid: %s"),**File);Cubes.Add(*File,TStrongObjectPtr<UTextureCube>());return nullptr;};
    TArray<uint8> Bytes;if(!FFileHelper::LoadFileToArray(Bytes,*(Root/(*File)))||Bytes.Num()<32||FMemory::Memcmp(Bytes.GetData(),"OKCUBE01",8)!=0)return Fail();
    auto U32=[&](int32 At){uint32 V;FMemory::Memcpy(&V,Bytes.GetData()+At,4);return V;};
    const uint32 Size=U32(8),Mips=U32(12),Format=U32(16);uint64 Length;FMemory::Memcpy(&Length,Bytes.GetData()+24,8);
    if(Size<4||Size>4096||!FMath::IsPowerOfTwo(Size)||Mips<1||Mips>uint32(FMath::FloorLog2(Size)+1)||U32(20)!=0||Length!=uint64(Bytes.Num()-32))return Fail();
    const EPixelFormat Pixel=Format==95?PF_BC6H:Format==10?PF_FloatRGBA:PF_Unknown;if(Pixel==PF_Unknown)return Fail();
    TArray<int64> Offsets,Sizes;int64 FaceBytes=0;
    for(uint32 M=0;M<Mips;++M){const uint32 W=FMath::Max(1u,Size>>M);const int64 N=Format==95?int64((W+3)/4)*((W+3)/4)*16:int64(W)*W*8;Offsets.Add(FaceBytes);Sizes.Add(N);FaceBytes+=N;}
    if(uint64(FaceBytes)*6!=Length)return Fail();
    UTextureCube* Cube=UTextureCube::CreateTransient(Size,Size,Pixel);if(!Cube)return Fail();
    FTexturePlatformData* Data=Cube->GetPlatformData();Data->Mips.Empty();
    for(uint32 M=0;M<Mips;++M)
    {
        const uint32 W=FMath::Max(1u,Size>>M);auto* Mip=new FTexture2DMipMap(W,W,1);Data->Mips.Add(Mip);
        Mip->BulkData.Lock(LOCK_READ_WRITE);uint8* Dest=static_cast<uint8*>(Mip->BulkData.Realloc(Sizes[M]*6));
        for(int32 Face=0;Face<6;++Face)FMemory::Memcpy(Dest+Face*Sizes[M],Bytes.GetData()+32+Face*FaceBytes+Offsets[M],Sizes[M]);
        Mip->BulkData.Unlock();
    }
    Cube->SRGB=false;Cube->NeverStream=true;Cube->Filter=TF_Trilinear;Cube->LODGroup=TEXTUREGROUP_Skybox;Cube->UpdateResource();
    Cubes.Add(*File,TStrongObjectPtr<UTextureCube>(Cube));
    UE_LOG(LogTemp,Display,TEXT("OK Studio native reflection: map=%d size=%u mips=%u format=%u bytes=%llu"),Map,Size,Mips,Format,Length);
    return Cube;
}
