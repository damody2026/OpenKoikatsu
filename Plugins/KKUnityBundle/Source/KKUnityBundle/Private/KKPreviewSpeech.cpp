#include "KKPreviewActor.h"
#include "Components/AudioComponent.h"
#include "AudioDevice.h"
#include "Engine/World.h"

bool AKKPreviewActor::PlaySpeechFile(const FString& Path,float Pitch,FString& Error) {
    TSharedPtr<const FKKSpeechPcm,ESPMode::ThreadSafe> Pcm;
    if(!KKSpeech::DecodeOgg(Path,Pcm,Error))return false;
    return PlaySpeechPcm(Pcm,Pitch,Error);
}
bool AKKPreviewActor::PlaySpeechPcm(TSharedPtr<const FKKSpeechPcm,ESPMode::ThreadSafe> Pcm,float Pitch,FString& Error) {
    if(!Pcm||!FMath::IsFinite(Pitch)||Pitch<=0.f){Error=TEXT("語音資料或音高無效");return false;}
    if(!GetWorld()||!GetWorld()->GetAudioDevice().IsValid()){Error=TEXT("沒有可用的音訊裝置");return false;}
    const int32 Rate=GetWorld()->GetAudioDevice()->GetSampleRate();
    const bool KeepBreath=bHBreath;const TArray<FKKHBreathCue> Kept=HBreaths;const int32 KeptIndex=HBreathIndex;
    StopSpeech();bHBreath=KeepBreath;HBreaths=Kept;HBreathIndex=KeptIndex;
    if(!SpeechAudio){SpeechAudio=NewObject<UAudioComponent>(this);SpeechAudio->SetupAttachment(RootComponent);
        SpeechAudio->bAutoActivate=false;SpeechAudio->bAutoDestroy=!bReuseSpeechAudio;SpeechAudio->bAllowSpatialization=false;SpeechAudio->RegisterComponent();}
    SpeechAudio->OnAudioFinishedNative.AddWeakLambda(this,[this](UAudioComponent* Completed){
        if(Completed==SpeechAudio){if(!bReuseSpeechAudio)SpeechAudio=nullptr;SpeechFinished();}
    });
    SpeechWave=NewObject<UKKSpeechWave>(this);SpeechWave->Init(Pcm,Rate);
    SpeechAudio->SetSound(SpeechWave);SpeechAudio->SetPitchMultiplier(Pitch);SpeechAudio->SetVolumeMultiplier(1.f);
    // Paired playback disables this actor's tick and advances the mouth itself.
    // Turning the tick back on would also step her pose off the shared clock.
    bSpeechActive=true;SpeechOpen=0.f;SpeechPitch=Pitch;SpeechAudio->Play();
    if(bSpeechPaused)SpeechAudio->SetPaused(true);
    return true;
}
bool AKKPreviewActor::LoadGeneralSpeech(const FString& Root,int32 Personality,float Pitch,FString& Error) {
    StopHBreath();SpeechLines.Reset();SpeechPrevious=0.f;
    if(!FMath::IsFinite(Pitch)||Pitch<=0.f){Error=TEXT("語音音高無效");return false;}
    if(!KKSpeech::LoadLines(Root,Personality,SpeechLines,Error))return false;
    SpeechPitch=Pitch;return true;
}
bool AKKPreviewActor::SpeakGeneralLine(const FString& Id,FString& Error) {
    Error.Reset();const FKKSpeechLine* Line=SpeechLines.FindByPredicate([&](const FKKSpeechLine& L){return L.Id==Id;});
    if(!Line){Error=TEXT("找不到已載入的一般對話語音");return false;}
    // A failed decode leaves the current line playing.
    return PlaySpeechFile(Line->AudioPath,SpeechPitch,Error);
}
bool AKKPreviewActor::BeginHBreath(const TArray<FKKHBreathCue>& Cues,float Pitch,FString& Error) {
    StopHBreath();
    if(Cues.IsEmpty()||!FMath::IsFinite(Pitch)||Pitch<=0.f){Error=TEXT("沒有可播放的性愛喘息");return false;}
    SavedMouthPattern=MouthPattern;SavedMouthOpen=MouthOpen01;HBreaths=Cues;HBreathIndex=0;bHBreath=true;SpeechPitch=Pitch;
    if(!PlayHCue(Error)){StopHBreath();return false;}
    return true;
}
void AKKPreviewActor::StopHBreath() {
    const bool bRestore=bHBreath;const int32 Mouth=SavedMouthPattern;const float Open=SavedMouthOpen;
    bHBreath=false;HBreaths.Reset();PendingHBreaths.Reset();bHBreathQueuePending=false;HBreathIndex=0;StopSpeech();
    if(bRestore){SetMouthPattern(Mouth,false);SetMouthOpen(Open);}
}
void AKKPreviewActor::SetSpeechPaused(bool bPaused) {
    bSpeechPaused=bPaused;if(SpeechAudio)SpeechAudio->SetPaused(bPaused);
}
bool AKKPreviewActor::PlayHCue(FString& Error) {
    if(!HBreaths.IsValidIndex(HBreathIndex)){Error=TEXT("性愛喘息已結束");return false;}
    const FKKHBreathCue Cue=HBreaths[HBreathIndex];
    if(SpeechTempo>1.01f){TSharedPtr<const FKKSpeechPcm,ESPMode::ThreadSafe> Pcm;
        if(!KKSpeech::DecodeOgg(Cue.Path,Pcm,Error)||!PlaySpeechPcm(KKSpeech::TimeStretch(Pcm,SpeechTempo),SpeechPitch,Error))return false;}
    else if(!PlaySpeechFile(Cue.Path,SpeechPitch,Error))return false;
    // StopSpeech inside playback clears the floor; the face list puts it back.
    if(Cue.Mouths.Num()&&!bManualSpeechPattern){
        const int32 Pick=FMath::RandRange(0,Cue.Mouths.Num()-1);
        SpeechOpenMin=FMath::Clamp(Cue.OpenMins.IsValidIndex(Pick)?Cue.OpenMins[Pick]:0.f,0.f,1.f);
        SetMouthPattern(Cue.Mouths[Pick],true);
    }else {SpeechOpenMin=0.f;ApplyMouth();}
    return true;
}
void AKKPreviewActor::StopSpeech() {
    bSpeechActive=false;SpeechOpen=0.f;SpeechOpenMin=0.f;
    if(SpeechAudio){auto* Previous=SpeechAudio.Get();Previous->OnAudioFinishedNative.Clear();Previous->Stop();if(!bReuseSpeechAudio){SpeechAudio=nullptr;Previous->DestroyComponent();}}
    SpeechWave=nullptr;ApplyMouth();
}
void AKKPreviewActor::SpeechFinished() {
    if(bHBreath&&bHBreathQueuePending){HBreaths=MoveTemp(PendingHBreaths);bHBreathQueuePending=false;HBreathIndex=-1;if(HBreaths.IsEmpty()){StopHBreath();return;}}
    if(bHBreath&&HBreaths.Num()){
        HBreathIndex=(HBreathIndex+1)%HBreaths.Num();FString Error;
        if(PlayHCue(Error))return;
        StopHBreath();
        return;
    }
    bSpeechActive=false;SpeechOpen=0.f;SpeechOpenMin=0.f;SpeechWave=nullptr;ApplyMouth();
}
void AKKPreviewActor::AdvanceSpeech() {
    if(!bSpeechActive||!SpeechWave||!SpeechWave->Meter)return;
    // Audio render progress follows pitch/pausing; never use animation phase.
    SpeechOpen=KKSpeech::MouthRate(SpeechWave->Meter->Rms.Load(),SpeechPrevious);ApplyMouth();
}
