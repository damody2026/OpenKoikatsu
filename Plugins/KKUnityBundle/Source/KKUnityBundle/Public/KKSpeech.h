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
namespace KKSpeech {
    KKUNITYBUNDLE_API bool LoadLines(const FString& Root,int32 Personality,TArray<FKKSpeechLine>& Lines,FString& Error);
    KKUNITYBUNDLE_API bool DecodeOgg(const FString& Path,TSharedPtr<const FKKSpeechPcm,ESPMode::ThreadSafe>& Out,FString& Error);
    KKUNITYBUNDLE_API float MouthRate(float Rms,float& Previous);
    KKUNITYBUNDLE_API ISoundGeneratorPtr MakeGenerator(TSharedPtr<const FKKSpeechPcm,ESPMode::ThreadSafe> Pcm,TSharedPtr<FKKSpeechMeter,ESPMode::ThreadSafe> Meter,float StartSeconds=0.f,int32 OutputRate=0);
    // First-time H breath (experience bucket 0) for one animator state.
    KKUNITYBUNDLE_API bool CollectHBreath(const FString& Root,int32 Personality,int32 Mode,const FString& State,int32 AnimId,bool bMoving,TArray<FKKHBreathCue>& Out,FString& Error);
    KKUNITYBUNDLE_API bool PrepareHBreathIndex(const FString& Root,FString& Error);
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
