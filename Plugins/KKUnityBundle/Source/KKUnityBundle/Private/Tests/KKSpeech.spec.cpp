#include "KKSpeech.h"
#include "KKPreviewActor.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "AudioDevice.h"
#include "AudioDeviceManager.h"
#include "Components/AudioComponent.h"
#include "Misc/AutomationTest.h"
#include "HAL/PlatformTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
namespace {const FString SpeechRoot=TEXT("C:/portable/KoikatuSunshine/voice_extract");}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKKSpeechCore,"OpenKoikatsu.Speech.Core",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FKKSpeechCore::RunTest(const FString&){
    FString Error;TArray<FKKSpeechLine> Lines;
    if(!TestTrue(TEXT("general catalog"),KKSpeech::LoadLines(SpeechRoot,32,Lines,Error)))return false;
    for(int32 I=0;I<Lines.Num();++I){TestTrue(TEXT("common talk only"),Lines[I].Id.StartsWith(TEXT("sun_tk_")));TestEqual(TEXT("personality"),Lines[I].Personality,32);if(I)TestTrue(TEXT("sorted"),Lines[I-1].Id<=Lines[I].Id);}
    TestFalse(TEXT("unavailable personality"),KKSpeech::LoadLines(SpeechRoot,999,Lines,Error));
    TestFalse(TEXT("missing index"),KKSpeech::LoadLines(SpeechRoot/TEXT("missing"),32,Lines,Error));
    TSharedPtr<const FKKSpeechPcm,ESPMode::ThreadSafe> Decoded;
    if(!TestTrue(TEXT("actual OGG"),KKSpeech::DecodeOgg(SpeechRoot/TEXT("audio/c32/adv/sun_tk_32_000.ogg"),Decoded,Error)))return false;
    TestTrue(TEXT("decoded nonempty"),Decoded->Samples.Num()>0);
    TestFalse(TEXT("reject JSON as audio"),KKSpeech::DecodeOgg(SpeechRoot/TEXT("comm_lines.json"),Decoded,Error));
    for(int32 Channels:{1,2})for(int32 Rate:{44100,48000}){
        auto Pcm=MakeShared<FKKSpeechPcm,ESPMode::ThreadSafe>();Pcm->Channels=Channels;Pcm->SampleRate=Rate;Pcm->Samples.SetNumZeroed(Rate*Channels);
        for(int32 I=0;I<Rate;++I)Pcm->Samples[I*Channels]=16384;
        auto Meter=MakeShared<FKKSpeechMeter,ESPMode::ThreadSafe>();auto Gen=KKSpeech::MakeGenerator(Pcm,Meter,0.f,48000);
        int32 Total=0;TArray<float> Buffer;Buffer.SetNumZeroed(1024*Channels);
        while(!Gen->IsFinished()&&Total<100000*Channels){const int32 N=Gen->OnGenerateAudio(Buffer.GetData(),Buffer.Num());Total+=N;TestTrue(TEXT("finite buffer"),FMath::IsFinite(Buffer[0]));TestEqual(TEXT("PCM amplitude"),Buffer[0],.5f);if(Channels==2)TestEqual(TEXT("silent second channel"),Buffer[1],0.f);}
        TestTrue(TEXT("finite EOF"),Gen->IsFinished());TestTrue(TEXT("one second resample"),FMath::Abs(Total/Channels-48000)<=1);
        TestEqual(TEXT("first channel RMS"),Meter->Rms.Load(),.5f);
        TestEqual(TEXT("after EOF no samples"),Gen->OnGenerateAudio(Buffer.GetData(),Buffer.Num()),0);
        auto OtherMeter=MakeShared<FKKSpeechMeter,ESPMode::ThreadSafe>();auto Other=KKSpeech::MakeGenerator(Pcm,OtherMeter,.5f,48000);TestEqual(TEXT("independent cursor"),OtherMeter->RenderedFrames.Load(),0);
        int32 Remaining=0;while(!Other->IsFinished()){Remaining+=Other->OnGenerateAudio(Buffer.GetData(),Buffer.Num());}
        TestTrue(TEXT("seek preserves duration"),FMath::Abs(Remaining/Channels-24000)<=1);
    }
    float Previous=0.f;TestEqual(TEXT("rising smoothing"),KKSpeech::MouthRate(.2f,Previous),.3f);
    TestTrue(TEXT("falling smoothing"),FMath::IsNearlyEqual(KKSpeech::MouthRate(.1f,Previous),.54f));TestTrue(TEXT("previous raw"),FMath::IsNearlyEqual(Previous,.3f));
    TestEqual(TEXT("clamp"),KKSpeech::MouthRate(10.f,Previous),.65f);
    return !HasAnyErrors();
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKKSpeechCorpus,"OpenKoikatsu.Speech.CatalogCorpus",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FKKSpeechCorpus::RunTest(const FString&){
    FString Error,Csv=TEXT("personality,id,sample_rate,channels,frames,duration_seconds\n");int32 Count=0;int64 Frames=0;
    for(int32 Personality=0;Personality<40;++Personality){
        TArray<FKKSpeechLine> Lines;
        if(!KKSpeech::LoadLines(SpeechRoot,Personality,Lines,Error)){AddError(Error);return false;}
        TestEqual(TEXT("snapshot common lines per personality"),Lines.Num(),29);
        for(const auto& Line:Lines){
            TSharedPtr<const FKKSpeechPcm,ESPMode::ThreadSafe> Pcm;
            if(!KKSpeech::DecodeOgg(Line.AudioPath,Pcm,Error)){AddError(Line.Id+TEXT(": ")+Error);return false;}
            const int32 N=Pcm->Samples.Num()/Pcm->Channels;TestTrue(TEXT("nonempty decoded line"),N>0);
            Csv+=FString::Printf(TEXT("%d,%s,%d,%d,%d,%.9f\n"),Personality,*Line.Id,Pcm->SampleRate,Pcm->Channels,N,double(N)/Pcm->SampleRate);
            Frames+=N;++Count;
        }
    }
    TestTrue(TEXT("save corpus evidence"),FFileHelper::SaveStringToFile(Csv,*(FPaths::ProjectSavedDir()/TEXT("Tests/Speech356/Corpus.csv"))));
    AddInfo(FString::Printf(TEXT("general speech corpus personalities=40 lines=%d decoded_source_frames=%lld"),Count,Frames));
    return TestEqual(TEXT("all supplied common lines"),Count,1160)&&!HasAnyErrors();
}
namespace {
class FSpeechPlaybackCommand:public IAutomationLatentCommand {
    FAutomationTestBase* Test;UWorld* World=nullptr;AKKPreviewActor* A=nullptr;AKKPreviewActor* B=nullptr;
    TSharedPtr<FKKSpeechMeter,ESPMode::ThreadSafe> Meter;double Started=0.;bool SawFrames=false,SawMouth=false;int32 Phase=0;float ExpectedSeconds=0.f;
public:
    explicit FSpeechPlaybackCommand(FAutomationTestBase* In):Test(In){}
    bool Update()override{
        if(!World){
            const auto Values=UWorld::InitializationValues().AllowAudioPlayback(true).CreateNavigation(false).CreateAISystem(false);
            World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
            if(!World||!GEngine){Test->AddError(TEXT("no test world"));return true;}
            GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);FAudioDeviceParams Params;Params.AssociatedWorld=World;Params.Scope=EAudioDeviceScope::Shared;
            World->SetAudioDevice(FAudioDeviceManager::Get()->RequestAudioDevice(Params));World->BeginPlay();
            A=World->SpawnActor<AKKPreviewActor>();B=World->SpawnActor<AKKPreviewActor>();FString Error;
            if(!A||!B||!A->LoadGeneralSpeech(SpeechRoot,32,1.f,Error)||!B->LoadGeneralSpeech(SpeechRoot,32,1.f,Error)||!A->SpeakGeneralLine(TEXT("sun_tk_32_000"),Error)){Test->AddError(TEXT("play failed: ")+Error);Cleanup();return true;}
            // The editor is not a running PIE game: mark only this test source as UI audio
            // so the editor pause policy cannot repeatedly suspend it between latent ticks.
            A->SpeechAudio->SetUISound(true);A->MouthOpen01=.2f;B->MouthOpen01=.7f;Meter=A->SpeechWave->Meter;ExpectedSeconds=A->SpeechWave->Duration;Started=FPlatformTime::Seconds();
            Test->TestFalse(TEXT("other actor idle"),B->IsSpeaking());Test->TestFalse(TEXT("bad ID fails"),A->SpeakGeneralLine(TEXT("missing"),Error));Test->TestTrue(TEXT("failed request preserves clip"),A->IsSpeaking());
        }
        World->Tick(LEVELTICK_All,.02f);World->GetAudioDevice()->Update(true);
        A->AdvanceSpeech();
        if(A->IsSpeaking())Test->TestEqual(TEXT("voice drives effective mouth"),A->EffectiveFaceRate(0),A->SpeechOpen);
        Test->TestEqual(TEXT("other manual mouth unchanged"),B->EffectiveFaceRate(0),.7f);
        SawFrames|=Meter->RenderedFrames.Load()>0;SawMouth|=A->SpeechOpen>0.f;
        if(!A->IsSpeaking()){
            Test->TestTrue(TEXT("audio device consumed PCM"),SawFrames);Test->TestTrue(TEXT("mouth moved"),SawMouth);Test->TestEqual(TEXT("ended voice rate closed"),A->SpeechOpen,0.f);Test->TestEqual(TEXT("manual mouth restored"),A->EffectiveFaceRate(0),.2f);
            const double Elapsed=FPlatformTime::Seconds()-Started;
            Test->AddInfo(FString::Printf(TEXT("device playback pitch=%f frames=%d elapsed=%f expected=%f"),A->SpeechPitch,Meter->RenderedFrames.Load(),Elapsed,ExpectedSeconds));
            Test->TestTrue(TEXT("device playback duration"),FMath::Abs(Elapsed-ExpectedSeconds)<1.5);
            if(Phase<2){
                const float Pitch=Phase==0?.94f:1.06f;FString Error;++Phase;
                if(!A->LoadGeneralSpeech(SpeechRoot,32,Pitch,Error)||!A->SpeakGeneralLine(TEXT("sun_tk_32_000"),Error)){Test->AddError(Error);Cleanup();return true;}
                A->SpeechAudio->SetUISound(true);Meter=A->SpeechWave->Meter;ExpectedSeconds=A->SpeechWave->Duration/Pitch;
                Started=FPlatformTime::Seconds();SawFrames=false;SawMouth=false;return false;
            }
            FString Error;Test->TestTrue(TEXT("second actor plays"),B->SpeakGeneralLine(TEXT("sun_tk_32_000"),Error));Test->TestTrue(TEXT("first restarts"),A->SpeakGeneralLine(TEXT("sun_tk_32_000"),Error));
            Test->TestTrue(TEXT("replacement"),A->SpeakGeneralLine(TEXT("sun_tk_32_001"),Error));A->StopSpeech();Test->TestFalse(TEXT("stop first"),A->IsSpeaking());Test->TestTrue(TEXT("second unaffected"),B->IsSpeaking());B->Destroy();Test->TestFalse(TEXT("destroy stops speech"),B->IsSpeaking());Phase=1;Cleanup();return true;
        }
        if(FPlatformTime::Seconds()-Started>20.){Test->AddError(FString::Printf(TEXT("playback timeout: frames=%d mouth=%d"),Meter->RenderedFrames.Load(),SawMouth));Cleanup();return true;}
        return false;
    }
    void Cleanup(){if(World){World->DestroyWorld(false);GEngine->DestroyWorldContext(World);World=nullptr;}}
};}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKKSpeechPlayback,"OpenKoikatsu.Speech.Playback",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FKKSpeechPlayback::RunTest(const FString&){ADD_LATENT_AUTOMATION_COMMAND(FSpeechPlaybackCommand(this));return true;}
#endif
