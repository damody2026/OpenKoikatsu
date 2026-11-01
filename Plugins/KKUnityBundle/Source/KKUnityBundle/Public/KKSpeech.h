#pragma once
#include "CoreMinimal.h"
#include "Sound/SoundWaveProcedural.h"
#include "Sound/SoundGenerator.h"
#include "KKSpeech.generated.h"

USTRUCT(BlueprintType)
struct FKKSpeechLine {
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) FString Id;
    UPROPERTY(BlueprintReadOnly) FString Text;
    UPROPERTY(BlueprintReadOnly) FString AudioPath;
    UPROPERTY(BlueprintReadOnly) int32 Personality=0;
};

struct FKKSpeechPcm {
    TArray<int16> Samples;
    int32 SampleRate=0,Channels=0;
};
struct FKKSpeechMeter {
    TAtomic<float> Rms{0.f};
    TAtomic<int32> RenderedFrames{0};
};
struct FKKHBreathCue {
    FString Path;
    FString Name;
    TArray<int32> Mouths;
    TArray<float> OpenMins;
};
// What HVoiceCtrl.IsBreathPtnConditions and GetStateSelect read when they pick her breathing: her pleasure gauge (0..100;
// conditions 0 / 1 split at 50), his fast motion (voice.speedMotion, conditions 4 / 5), whether they move (12 / 13), and
// her HExperience (0 初めて .. 3 淫乱; the bank of files) with whether she has had him in her / in her behind before
// (state 8 / 9 use bank 0 until then). No hands, kiss, items or gentleness in the scenes that ask.
struct FKKHBreathContext {
    float Gauge=0.f;
    bool bFast=false;
    bool bMoving=false;
    int32 Experience=0;
    bool bHadVaginal=false;
    bool bHadAnal=false;
};
namespace KKSpeech {
    // A voice's general lines, parsed once per index file (comm_lines.json) and voice; waits for a running prefetch.
    KKUNITYBUNDLE_API bool LoadLines(const FString& Root,int32 Personality,TArray<FKKSpeechLine>& Lines,FString& Error);
    // LoadLines started on the thread pool (a card load names the voice before its speech panel asks for it).
    KKUNITYBUNDLE_API void PrefetchLines(const FString& Root,int32 Personality);
    KKUNITYBUNDLE_API bool DecodeOgg(const FString& Path,TSharedPtr<const FKKSpeechPcm,ESPMode::ThreadSafe>& Out,FString& Error);
    KKUNITYBUNDLE_API float MouthRate(float Rms,float& Previous);
    KKUNITYBUNDLE_API ISoundGeneratorPtr MakeGenerator(TSharedPtr<const FKKSpeechPcm,ESPMode::ThreadSafe> Pcm,TSharedPtr<FKKSpeechMeter,ESPMode::ThreadSafe> Meter,float StartSeconds=0.f,int32 OutputRate=0);
    // First-time H breath (experience bucket 0) for one animator state.
    KKUNITYBUNDLE_API bool CollectHBreath(const FString& Root,int32 Personality,int32 Mode,const FString& State,int32 AnimId,bool bMoving,TArray<FKKHBreathCue>& Out,FString& Error);
    // Her breathing for one animator state as the original picks it now (gauge, speed, her experience bank).
    KKUNITYBUNDLE_API bool CollectHBreath(const FString& Root,int32 Personality,int32 Mode,const FString& State,int32 AnimId,const FKKHBreathContext& Context,TArray<FKKHBreathCue>& Out,FString& Error);
    KKUNITYBUNDLE_API bool PrepareHBreathIndex(const FString& Root,FString& Error);
    // Her cries for one row of the intercourse voice table (HSonyu plays 300 + row: 25 her climax in the slow loop, 26 in
    // the fast one) for this animation, from her experience bank (h_breath_index version 2 "voices").
    KKUNITYBUNDLE_API bool CollectHVoice(const FString& Root,int32 Personality,int32 Row,int32 AnimId,const FKKHBreathContext& Context,TArray<FKKHBreathCue>& Out,FString& Error);
}

UCLASS()
class KKUNITYBUNDLE_API UKKSpeechWave : public USoundWaveProcedural {
    GENERATED_BODY()
public:
    void Init(TSharedPtr<const FKKSpeechPcm,ESPMode::ThreadSafe> InPcm,int32 PlaybackSampleRate);
    virtual ISoundGeneratorPtr CreateSoundGenerator(const FSoundGeneratorInitParams& Params) override;
    TSharedPtr<const FKKSpeechPcm,ESPMode::ThreadSafe> Pcm;
    TSharedPtr<FKKSpeechMeter,ESPMode::ThreadSafe> Meter;
};
