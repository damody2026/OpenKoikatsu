#include "KKSpeech.h"
#include "Async/Async.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
namespace {const FString BreathRoot=TEXT("C:/portable/KoikatuSunshine/voice_extract");}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKKHBreathSelection,"OpenKoikatsu.Speech.HBreath",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FKKHBreathSelection::RunTest(const FString&){
    TArray<FKKHBreathCue> Cues;FString Error;
    if(!TestTrue(TEXT("sonyu WLoop breath"),KKSpeech::CollectHBreath(BreathRoot,0,2,TEXT("WLoop"),0,true,Cues,Error))){AddError(Error);return false;}
    if(!TestEqual(TEXT("two normal-speed moans"),Cues.Num(),2))return false;
    TestEqual(TEXT("first moan"),Cues[0].Name,TEXT("h_ko_00_00_075"));
    TestEqual(TEXT("second moan"),Cues[1].Name,TEXT("h_ko_00_00_076"));
    TestEqual(TEXT("mouth pattern"),Cues[0].Mouths.Num()?Cues[0].Mouths[0]:-1,14);
    TestTrue(TEXT("open floor"),Cues[0].OpenMins.Num()&&FMath::IsNearlyEqual(Cues[0].OpenMins[0],.3f,1e-4f));
    TestTrue(TEXT("personality path"),Cues[0].Path.Contains(TEXT("audio/c00/h/")));
    TSharedPtr<const FKKSpeechPcm,ESPMode::ThreadSafe> Pcm;
    if(!TestTrue(TEXT("moan decodes"),KKSpeech::DecodeOgg(Cues[0].Path,Pcm,Error))){AddError(Error);return false;}
    TestTrue(TEXT("moan samples"),Pcm->Samples.Num()>0);
    TestFalse(TEXT("animation filter"),KKSpeech::CollectHBreath(BreathRoot,0,2,TEXT("WLoop"),5,true,Cues,Error));
    TestFalse(TEXT("unknown state"),KKSpeech::CollectHBreath(BreathRoot,0,2,TEXT("NotAState"),0,true,Cues,Error));
    if(!TestTrue(TEXT("idle breath"),KKSpeech::CollectHBreath(BreathRoot,0,2,TEXT("Idle"),0,true,Cues,Error))){AddError(Error);return false;}
    TestTrue(TEXT("idle is not the thrust moan"),Cues.Num()>0&&Cues[0].Name!=TEXT("h_ko_00_00_075"));
    if(!TestTrue(TEXT("other heroine"),KKSpeech::CollectHBreath(BreathRoot,30,2,TEXT("WLoop"),0,true,Cues,Error))){AddError(Error);return false;}
    TestTrue(TEXT("c30 bank"),Cues.Num()>0&&Cues[0].Path.Contains(TEXT("audio/c30/h/")));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKKHBreathCache,"OpenKoikatsu.Speech.HBreathCache",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FKKHBreathCache::RunTest(const FString&){
    const FString Root=FPaths::ProjectSavedDir()/TEXT("Tests/HBreathCache")/FGuid::NewGuid().ToString();
    IFileManager::Get().MakeDirectory(*(Root/TEXT("audio/c00/h")),true);
    const FString IndexPath=Root/TEXT("h_breath_index.json");
    auto Write=[&](const FString& Name){const FString Json=FString::Printf(TEXT("{\"patterns\":[{\"m\":2,\"anim\":\"WLoop\",\"info\":[{\"ids\":[-1],\"cond\":[],\"voices\":[75]}]}],\"clips\":[{\"p\":0,\"m\":2,\"id\":75,\"file\":\"%s\",\"faces\":[]}]}"),*Name);
        return FFileHelper::SaveStringToFile(TEXT("fixture"),*(Root/TEXT("audio/c00/h")/(Name+TEXT(".ogg"))))&&FFileHelper::SaveStringToFile(Json,*IndexPath);};
    if(!TestTrue(TEXT("Write first index revision"),Write(TEXT("sample_a"))))return false;
    TArray<TFuture<bool>> Jobs;
    for(int32 I=0;I<8;++I)Jobs.Add(Async(EAsyncExecution::ThreadPool,[Root](){FString Error;TArray<FKKHBreathCue> Cues;
        return KKSpeech::PrepareHBreathIndex(Root,Error)&&KKSpeech::CollectHBreath(Root,0,2,TEXT("WLoop"),0,true,Cues,Error)&&Cues.Num()==1&&Cues[0].Name==TEXT("sample_a");}));
    for(auto& Job:Jobs)TestTrue(TEXT("Concurrent preparation and lookup retain immutable index"),Job.Get());
    TestTrue(TEXT("Write new index revision"),Write(TEXT("sample_b_long")));
    FString Error;TArray<FKKHBreathCue> Cues;
    TestTrue(TEXT("Changed source invalidates shared index"),KKSpeech::CollectHBreath(Root,0,2,TEXT("WLoop"),0,true,Cues,Error)&&Cues.Num()==1&&Cues[0].Name==TEXT("sample_b_long"));
    return true;
}
#endif
