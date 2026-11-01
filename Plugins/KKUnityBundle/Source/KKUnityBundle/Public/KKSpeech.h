#pragma once
#include "CoreMinimal.h"
#include "Sound/SoundWaveProcedural.h"
#include "Sound/SoundGenerator.h"
#include <atomic>
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
    std::atomic<uint32> ClockSeq{0};
    std::atomic<double> BlockAudioSeconds{0.0};
    std::atomic<double> BlockWallSeconds{0.0};
    std::atomic<bool> bFinished{false};

    void WriteClockAnchor(double InAudioSeconds, double InWallSeconds, bool bInFinished) {
        const uint32 Seq = ClockSeq.load(std::memory_order_relaxed);
        ClockSeq.store(Seq + 1u, std::memory_order_release);
        BlockAudioSeconds.store(InAudioSeconds, std::memory_order_relaxed);
        BlockWallSeconds.store(InWallSeconds, std::memory_order_relaxed);
        bFinished.store(bInFinished, std::memory_order_relaxed);
        ClockSeq.store(Seq + 2u, std::memory_order_release);
    }

    bool ReadClockAnchor(uint32& InOutSeq, double& OutAudioSeconds, double& OutWallSeconds, bool& OutFinished) const {
        for (int32 Attempt = 0; Attempt < 4; ++Attempt) {
            const uint32 Seq1 = ClockSeq.load(std::memory_order_acquire);
            if (Seq1 == 0u || (Seq1 & 1u) != 0u) continue;
            const double AudioSec = BlockAudioSeconds.load(std::memory_order_relaxed);
            const double WallSec = BlockWallSeconds.load(std::memory_order_relaxed);
            const bool bFin = bFinished.load(std::memory_order_relaxed);
            std::atomic_thread_fence(std::memory_order_acquire);
            const uint32 Seq2 = ClockSeq.load(std::memory_order_relaxed);
            if (Seq1 == Seq2) {
                const bool bChanged = (Seq1 != InOutSeq);
                InOutSeq = Seq1;
                OutAudioSeconds = AudioSec;
                OutWallSeconds = WallSec;
                OutFinished = bFin;
                return bChanged;
            }
        }
        return false;
    }
};
struct FKKHBreathCue {
    FString Path;
    FString Name;
    TArray<int32> Mouths;
    TArray<float> OpenMins;
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
