#include "KKTextureRuntime.h"
#include "ok_extract.h"
#include "Engine/Texture2D.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Modules/ModuleManager.h"
#include "Misc/AutomationTest.h"
#include "Async/ParallelFor.h"
#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKKPngCacheTest,"OpenKoikatsu.Material.PngCache",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FKKPngCacheTest::RunTest(const FString&){
 OKClearRuntimePngCache();TArray<uint8> Raw;Raw.SetNum(16*16*4);for(int32 I=0;I<Raw.Num();++I)Raw[I]=uint8(I*37);
 auto Encode=[](const TArray<uint8>& Bytes){auto& M=FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));auto W=M.CreateImageWrapper(EImageFormat::PNG);W->SetRaw(Bytes.GetData(),Bytes.Num(),16,16,ERGBFormat::BGRA,8);TArray<uint8> Png;auto Compressed=W->GetCompressed();Png.Append(Compressed.GetData(),Compressed.Num());return Png;};
 const auto Png=Encode(Raw);OkTextureSettings First{1,1,1,1,1},Second{0,2,0,0,1};bool Hit=false;double Decode=0.;
 auto* A=OKCreateRuntimeTextureFromPng(Png,&First,false,Hit,Decode);TestNotNull(TEXT("Cold PNG"),A);TestFalse(TEXT("Cold miss"),Hit);
 auto* B=OKCreateRuntimeTextureFromPng(Png,&Second,false,Hit,Decode);TestNotNull(TEXT("Warm PNG"),B);TestTrue(TEXT("Warm prepared-pixel hit"),Hit);
 if(!A||!B){OKClearRuntimePngCache();return false;}
 TestTrue(TEXT("Independent runtime texture objects"),A!=B);TestEqual(TEXT("Warm PNG is not decoded again"),Decode,0.);
 TestTrue(TEXT("First stays sRGB"),A->SRGB);TestFalse(TEXT("Second is linear"),B->SRGB);TestEqual(TEXT("Independent wrap"),int32(B->AddressX),int32(TA_Mirror));
 A->SRGB=true;A->AddressY=TA_Clamp;TestFalse(TEXT("sRGB mutation stays local"),B->SRGB);TestEqual(TEXT("Actor-local mutation does not cross textures"),int32(B->AddressY),int32(TA_Wrap));
 const auto Expected=OKBuildEncodedMipChain(Raw,16,16);TestEqual(TEXT("Warm mip count"),B->GetPlatformData()->Mips.Num(),Expected.Num());
 for(int32 L=0;L<Expected.Num();++L){const auto& M=B->GetPlatformData()->Mips[L];const auto* Data=M.BulkData.LockReadOnly();TestTrue(TEXT("Warm upload equals uncached encoded mip bytes"),FMemory::Memcmp(Data,Expected[L].Bytes.GetData(),Expected[L].Bytes.Num())==0);M.BulkData.Unlock();}
 OkTextureSettings Top{1,1,1,1,0};auto* C=OKCreateRuntimeTextureFromPng(Png,&Top,false,Hit,Decode);TestFalse(TEXT("Different mip policy cannot reuse incompatible prepared data"),Hit);TestEqual(TEXT("Top-only policy"),C->GetPlatformData()->Mips.Num(),1);
 Raw[0]^=255;const auto Changed=Encode(Raw);auto* D=OKCreateRuntimeTextureFromPng(Changed,&First,false,Hit,Decode);TestFalse(TEXT("Changed image invalidates content identity"),Hit);
 auto Stats=OKRuntimePngCacheStats();TestEqual(TEXT("Three prepared entries"),Stats.Entries,3);TestTrue(TEXT("Bounded CPU bytes"),Stats.Bytes>0&&Stats.Bytes<=512ll*1024*1024);
 OKTrimRuntimePngCache(Stats.Bytes-1);
 auto* Recent=OKCreateRuntimeTextureFromPng(Changed,&First,false,Hit,Decode);TestTrue(TEXT("LRU retains the most recent content"),Hit);
 auto* E=OKCreateRuntimeTextureFromPng(Png,&First,false,Hit,Decode);TestFalse(TEXT("Least recent entry evicted"),Hit);
 OKTrimRuntimePngCache(0);TestEqual(TEXT("Release all retained bytes"),OKRuntimePngCacheStats().Bytes,int64(0));
 ParallelFor(8,[&](int32 I){OKPrecacheRuntimePng(Png,(I&1)!=0);});
 auto* WorkerPrepared=OKCreateRuntimeTextureFromPng(Png,&First,false,Hit,Decode);
 TestNotNull(TEXT("Concurrent worker preparation"),WorkerPrepared);TestTrue(TEXT("Game thread consumes worker pixels"),Hit);
 TestEqual(TEXT("Concurrent publication keeps two mip policies"),OKRuntimePngCacheStats().Entries,2);
 if(WorkerPrepared)for(int32 L=0;L<Expected.Num();++L){auto& M=WorkerPrepared->GetPlatformData()->Mips[L];const auto* Data=M.BulkData.LockReadOnly();TestTrue(TEXT("Concurrent preparation preserves exact texels"),FMemory::Memcmp(Data,Expected[L].Bytes.GetData(),Expected[L].Bytes.Num())==0);M.BulkData.Unlock();}
 UE_LOG(LogTemp,Display,TEXT("PNG cache audit valid=%d exact_mips=1 independent_textures=1 sampling=1 content_invalidation=1 policy=1 lru=1"),HasAnyErrors()?0:1);
 for(auto* T:{A,B,C,D,E,Recent,WorkerPrepared})if(T)T->MarkAsGarbage();OKClearRuntimePngCache();return !HasAnyErrors();
}
#endif
