#include "KKTextureRuntime.h"
#include "ok_extract.h"
#include "Engine/Texture2D.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/GarbageCollection.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKKRuntimeTextureTest,"OpenKoikatsu.Material.RuntimeTextureMips",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FKKRuntimeTextureTest::RunTest(const FString&)
{
    // A 127.5 alpha mean must stay below the 0.5 masked-material cutoff.
    // The native Nanami runtime RenderTexture writes 127 at this boundary.
    TArray<uint8> ClipBoundary = {
        0,0,0,0, 255,255,255,255,
        255,255,255,255, 0,0,0,0};
    const auto BoundaryMips = OKBuildEncodedMipChain(ClipBoundary,2,2);
    if (!TestEqual(TEXT("alpha clip boundary mip count"),BoundaryMips.Num(),2)) return false;
    for (int32 C=0;C<4;++C) TestEqual(TEXT("alpha clip boundary truncates"),int32(BoundaryMips[1].Bytes[C]),127);
    const FString Root=FPaths::ProjectSavedDir()/TEXT("Tests/TextureMips308NativeR6");
    struct FCase {const TCHAR* Name;int32 Width,Height;};
    int32 Steps=0,Maximum=0,RecursiveMaximum=0;
    for (const FCase& C:{FCase{TEXT("renderer50.mat0._MainTex"),4096,4096},
        FCase{TEXT("renderer27.mat0._MainTex"),4096,4096},FCase{TEXT("renderer50.mat0._NormalMap"),1024,1024},
        FCase{TEXT("mip-control.5x3"),5,3},FCase{TEXT("mip-control.1x7"),1,7},
        FCase{TEXT("mip-control.7x1"),7,1},FCase{TEXT("mip-control.4x4"),4,4},FCase{TEXT("mip-control.8x4"),8,4}}) {
        auto Read=[&](int32 Level,TArray<uint8>& Bytes) {
            const FString Path=Root/(FString(TEXT("Card0.json."))+C.Name+
                (FString(C.Name).StartsWith(TEXT("mip-control."))?TEXT(".direct"):TEXT(""))+FString::Printf(TEXT(".mip%d.rgba8"),Level));
            if (!FFileHelper::LoadFileToArray(Bytes,*Path)) {AddError(TEXT("Missing native mip ")+Path);return false;}
            // Readback RGBA -> production BGRA. Vertical orientation has no
            // effect on this symmetric kernel; preserve the fixture's rows.
            for (int32 I=0;I<Bytes.Num();I+=4) Swap(Bytes[I],Bytes[I+2]);return true;
        };
        TArray<uint8> Previous;if(!Read(0,Previous))return false;
        auto Full=OKBuildEncodedMipChain(Previous,C.Width,C.Height);
        int32 W=C.Width,H=C.Height;
        for (int32 Level=1;Level<Full.Num();++Level) {
            TArray<uint8> Expected;if(!Read(Level,Expected))return false;
            const auto Step=OKBuildEncodedMipChain(Previous,W,H);
            if(!TestEqual(TEXT("mip byte count"),Step[1].Bytes.Num(),Expected.Num()))return false;
            int32 Local=0,Recursive=0;
            for(int32 I=0;I<Expected.Num();++I){Local=FMath::Max(Local,FMath::Abs(int32(Step[1].Bytes[I])-Expected[I]));
                Recursive=FMath::Max(Recursive,FMath::Abs(int32(Full[Level].Bytes[I])-Expected[I]));}
            Maximum=FMath::Max(Maximum,Local);RecursiveMaximum=FMath::Max(RecursiveMaximum,Recursive);++Steps;
            TestTrue(FString::Printf(TEXT("native encoded kernel %s mip%d max%d"),C.Name,Level,Local),Local<=1);
            // Native UNORM quantization accumulates over the chain. The independent
            // all-level capture establishes max3; this is not pixel identity.
            TestTrue(FString::Printf(TEXT("native recursive chain %s mip%d max%d"),C.Name,Level,Recursive),Recursive<=3);
            Previous=MoveTemp(Expected);W=FMath::Max(1,W/2);H=FMath::Max(1,H/2);
        }
        OkTextureSettings Settings{1,1,1,1,1};
        UTexture2D* Texture=OKCreateRuntimeTexture(Full[0].Bytes,C.Width,C.Height,&Settings,false);
        if(!TestNotNull(TEXT("production transient"),Texture))return false;
        TestEqual(TEXT("all platform mips"),Texture->GetPlatformData()->Mips.Num(),Full.Num());
        TestEqual(TEXT("bilinear"),int32(Texture->Filter),int32(TF_Bilinear));
        TestEqual(TEXT("source Clamp U"),int32(Texture->AddressX),int32(TA_Clamp));
        TestTrue(TEXT("sRGB storage"),Texture->SRGB);
        for(int32 L=0;L<Full.Num();++L){const auto& M=Texture->GetPlatformData()->Mips[L];
            const uint8* Bytes=static_cast<const uint8*>(M.BulkData.LockReadOnly());
            TestTrue(TEXT("uploaded bytes preserve generated mip"),FMemory::Memcmp(Bytes,Full[L].Bytes.GetData(),Full[L].Bytes.Num())==0);
            M.BulkData.Unlock();}
        Texture->MarkAsGarbage();
    }
    AddInfo(FString::Printf(TEXT("native controls=8 steps=%d kernel_max=%d recursive_max=%d"),Steps,Maximum,RecursiveMaximum));
    return !HasAnyErrors();
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKKReadonlyTextureReuseTest,"OpenKoikatsu.Material.ReadonlyTextureReuse",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FKKReadonlyTextureReuseTest::RunTest(const FString&)
{
    OKClearReadonlyRuntimeTextureCache();
    auto& Module=FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
    auto Wrapper=Module.CreateImageWrapper(EImageFormat::PNG);
    const TArray<uint8> Pixels={1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16};
    if(!Wrapper->SetRaw(Pixels.GetData(),Pixels.Num(),2,2,ERGBFormat::BGRA,8))return false;
    const TArray<uint8> Png(Wrapper->GetCompressed());
    OkTextureSettings Settings{};
    Settings.filter=1;Settings.wrap_u=0;Settings.wrap_v=1;Settings.srgb=1;Settings.mip_policy=1;
    bool Hit=false;double Decode=0.;
    TStrongObjectPtr<UTexture2D> A(OKAcquireReadonlyRuntimeTextureFromPng(Png,&Settings,false,Hit,Decode));
    TestNotNull(TEXT("Readonly resource created"),A.Get());TestFalse(TEXT("First GPU lookup misses"),Hit);
    auto* Again=OKAcquireReadonlyRuntimeTextureFromPng(Png,&Settings,false,Hit,Decode);
    TestTrue(TEXT("Unchanged image/settings reuse GPU resource"),Hit&&Again==A.Get());
    OkTextureSettings Other{};
    Other.filter=2;Other.wrap_u=1;Other.wrap_v=2;Other.srgb=0;Other.mip_policy=0;
    TStrongObjectPtr<UTexture2D> B(OKAcquireReadonlyRuntimeTextureFromPng(Png,&Other,false,Hit,Decode));
    TestTrue(TEXT("Different filter/wrap/sRGB/mips get separate resource"),B.Get()!=A.Get());
    TestTrue(TEXT("Different source settings preserved"),!B->SRGB&&B->Filter==TF_Trilinear&&B->AddressX==TA_Clamp&&B->AddressY==TA_Mirror&&B->GetPlatformData()->Mips.Num()==1);
    bool LegacyHit=false;
    TStrongObjectPtr<UTexture2D> Mutable(OKCreateRuntimeTextureFromPng(Png,&Settings,false,LegacyHit,Decode));
    Mutable->SRGB=false;TestTrue(TEXT("Mutable factory never mutates shared readonly texture"),A->SRGB);
    // Defensive metadata rejection also covers legacy violations of readonly ownership.
    A->AddressX=TA_Mirror;
    TStrongObjectPtr<UTexture2D> Fresh(OKAcquireReadonlyRuntimeTextureFromPng(Png,&Settings,false,Hit,Decode));
    TestTrue(TEXT("Mutated sampler rejected instead of leaking to another actor"),!Hit&&Fresh.Get()!=A.Get()&&Fresh->AddressX==TA_Wrap);
    TestEqual(TEXT("Rejection counted"),OKReadonlyRuntimeTextureCacheStats().MetadataRejections,uint64(1));
    const auto& Mip=Fresh->GetPlatformData()->Mips[0];const auto* Bytes=static_cast<const uint8*>(Mip.BulkData.LockReadOnly());
    TestTrue(TEXT("Exact source pixels retained"),FMemory::Memcmp(Bytes,Pixels.GetData(),Pixels.Num())==0);Mip.BulkData.Unlock();
    TWeakObjectPtr<UTexture2D> Weak(Fresh.Get());Fresh.Reset();
    CollectGarbage(RF_NoFlags);
    TestFalse(TEXT("Cache holds no UObject/GPU ownership after actor releases texture"),Weak.IsValid());
    TestTrue(TEXT("Retained identity data has bounded budget"),OKReadonlyRuntimeTextureCacheStats().SourceBytes<=64ll*1024*1024);
    OKClearReadonlyRuntimeTextureCache();return true;
}
#endif
