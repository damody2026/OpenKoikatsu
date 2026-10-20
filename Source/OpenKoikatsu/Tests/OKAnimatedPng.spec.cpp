#include "Misc/AutomationTest.h"
#include "KKAnimatedPng.h"
#include "KKCardReader.h"
#include "KKCardWriter.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Modules/ModuleManager.h"
#include "Misc/Paths.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOKApngRoundTrip,"OpenKoikatsu.APNG.RoundTrip",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FOKApngRoundTrip::RunTest(const FString&)
{
    TArray<TArray<uint8>> Views;TArray<FColor> Pixels;Pixels.SetNum(768*1024);
    for(int I=0;I<4;++I)
    {
        for(auto& P:Pixels)P=FColor(I*60,220-I*50,30+I*40,255);
        auto W=FModuleManager::LoadModuleChecked<IImageWrapperModule>("ImageWrapper").CreateImageWrapper(EImageFormat::PNG);
        W->SetRaw(Pixels.GetData(),Pixels.Num()*4,768,1024,ERGBFormat::BGRA,8);
        const auto& Png=W->GetCompressed();Views.Emplace();Views.Last().Append(Png.GetData(),int32(Png.Num()));
    }
    FString Error;TArray<uint8> Png;
    if(!TestTrue("Build",FKKAnimatedPng::BuildPreview(Views[0],Views,Png,Error))){AddError(Error);return false;}
    TArray<FKKPngFrame> Frames;uint32 Plays;
    if(!TestTrue("Decode",FKKAnimatedPng::Read(Png,Frames,Plays,Error))){AddError(Error);return false;}
    TestEqual("Infinite loop",Plays,0u);TestEqual("Four views",Frames.Num(),4);
    TArray<double> Durations;
    for(int I=0;I<Frames.Num();++I)
    {
        Durations.Add(Frames[I].Seconds);TestEqual("Duration",Frames[I].Seconds,I==0?2.:1.);
        auto W=FModuleManager::LoadModuleChecked<IImageWrapperModule>("ImageWrapper").CreateImageWrapper(EImageFormat::PNG);
        TestTrue("Standalone PNG",W->SetCompressed(Frames[I].Png.GetData(),Frames[I].Png.Num()));
        TestEqual("Width",W->GetWidth(),768);TestEqual("Height",W->GetHeight(),1024);
        TArray64<uint8> Raw;TestTrue("Decode pixels",W->GetRaw(ERGBFormat::BGRA,8,Raw));
        if(Raw.Num()==768*1024*4){const FColor* Color=reinterpret_cast<const FColor*>(Raw.GetData());TestEqual("View pixels",Color[100],FColor(I*60,220-I*50,30+I*40,255));}
    }
    const double Times[]={0,1.999,2,3,4,4.999,5,7,10};const int32 Expected[]={0,0,1,2,3,3,0,1,0};
    for(int I=0;I<UE_ARRAY_COUNT(Times);++I)TestEqual("Frame boundary",FKKAnimatedPng::FrameAt(Durations,Times[I]),Expected[I]);
    TestEqual("Finite playback ends",FKKAnimatedPng::FrameAt(Durations,10.,2),3);
    TArray<uint8> Bad=Png;Bad[60]^=1;TestFalse("CRC corruption rejected",FKKAnimatedPng::Read(Bad,Frames,Plays,Error));
    Bad=Png;Bad.SetNum(Bad.Num()-8);TestFalse("Truncation rejected",FKKAnimatedPng::Read(Bad,Frames,Plays,Error));
    Views.Pop();TestFalse("Missing view rejected",FKKAnimatedPng::BuildPreview(Png,Views,Bad,Error));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOKApngOkCardProfile,"OpenKoikatsu.APNG.OkCardProfile",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FOKApngOkCardProfile::RunTest(const FString&)
{
    auto& Module=FModuleManager::LoadModuleChecked<IImageWrapperModule>("ImageWrapper");
    auto Encode=[&](int32 W,int32 H,FColor C){TArray<FColor> P;P.Init(C,W*H);auto E=Module.CreateImageWrapper(EImageFormat::PNG);
        E->SetRaw(P.GetData(),P.Num()*4,W,H,ERGBFormat::BGRA,8);const auto& Png=E->GetCompressed();TArray<uint8> Out;Out.Append(Png.GetData(),int32(Png.Num()));return Out;};
    // A KKS-sized card image (252x352) and three T-pose views.
    const TArray<uint8> CardImage=Encode(252,352,FColor(200,40,40,255));
    TArray<TArray<uint8>> Views;for(int I=0;I<3;++I)Views.Add(Encode(768,1024,FColor(20+I*60,90,160,255)));
    FString Error;TArray<uint8> Png;
    if(!TestTrue("Build OK card APNG",FKKAnimatedPng::BuildOkCardPreview(CardImage,Views,Png,Error))){AddError(Error);return false;}
    TArray<FKKPngFrame> Frames;uint32 Plays;
    if(!TestTrue("Decode",FKKAnimatedPng::Read(Png,Frames,Plays,Error))){AddError(Error);return false;}
    TestEqual("Card image + three views",Frames.Num(),4);TestEqual("Infinite loop",Plays,0u);
    for(int I=0;I<Frames.Num();++I)TestEqual("Duration",Frames[I].Seconds,I==0?3.:1.);
    auto W=Module.CreateImageWrapper(EImageFormat::PNG);TArray64<uint8> Raw;
    if(TestTrue("First frame PNG",W->SetCompressed(Frames[0].Png.GetData(),Frames[0].Png.Num()))&&TestTrue("Pixels",W->GetRaw(ERGBFormat::BGRA,8,Raw)))
    {
        TestEqual("Canvas width",W->GetWidth(),768);TestEqual("Canvas height",W->GetHeight(),1024);
        const FColor* C=reinterpret_cast<const FColor*>(Raw.GetData());
        TestEqual("Card image at the centre",C[512*768+384],FColor(200,40,40,255));
        TestEqual("Backdrop beside the fitted image",C[512*768+2],FColor(118,118,118,255));
    }
    // Saving again from an earlier OK card APNG keeps its first frame as the card image.
    TArray<uint8> Again;
    if(TestTrue("Rebuild from the APNG",FKKAnimatedPng::BuildOkCardPreview(Png,Views,Again,Error)))
    {
        TArray<FKKPngFrame> F2;TArray64<uint8> Raw2;auto W2=Module.CreateImageWrapper(EImageFormat::PNG);
        if(TestTrue("Decode rebuilt",FKKAnimatedPng::Read(Again,F2,Plays,Error))&&TestEqual("Rebuilt frames",F2.Num(),4)
            &&W2->SetCompressed(F2[0].Png.GetData(),F2[0].Png.Num())&&W2->GetRaw(ERGBFormat::BGRA,8,Raw2))
            TestTrue("Same card image pixels",Raw2==Raw);
    }
    Views.Pop();TestFalse("Two views rejected",FKKAnimatedPng::BuildOkCardPreview(CardImage,Views,Png,Error));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOKApngCardSave,"OpenKoikatsu.APNG.CardSave",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FOKApngCardSave::RunTest(const FString&)
{
    const FString Source=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_APNG_OUTPUT"));
    if(Source.IsEmpty()){AddInfo(TEXT("Set OK_APNG_OUTPUT to test a real four-view card"));return true;}
    FKKCharacterFile Card;FString Error;
    if(!TestTrue("Read APNG card",UKKCardReader::LoadFromFile(Source,Card,Error))){AddError(Error);return false;}
    TArray<FKKPngFrame> Frames;uint32 Plays;
    if(!TestTrue("Four views",FKKAnimatedPng::Read(Card.Header.PreviewPng,Frames,Plays,Error))){AddError(Error);return false;}
    const TArray<uint8> OriginalPreview=Card.Header.PreviewPng;
    Card.Body.ShapeValueBody[0]=.123f;Card.Body.ShapeValueBody[4]=.789f;Card.Nickname=TEXT("APNG roundtrip");
    const FString Output=FPaths::ProjectSavedDir()/TEXT("APNGCards/KK_346095_apng_edited.png");
    if(!TestTrue("Save body and nickname",UKKCardWriter::SaveWithEdits(Card,Output,Error))){AddError(Error);return false;}
    FKKCharacterFile Reload;
    if(!TestTrue("Reload edited card",UKKCardReader::LoadFromFile(Output,Reload,Error))){AddError(Error);return false;}
    TestTrue("Animation and unique resources unchanged",Reload.Header.PreviewPng==OriginalPreview);
    TestEqual("Height",Reload.Body.ShapeValueBody[0],.123f);TestEqual("Bust",Reload.Body.ShapeValueBody[4],.789f);
    TestEqual("Nickname",Reload.Nickname,FString(TEXT("APNG roundtrip")));
    return true;
}
#endif
