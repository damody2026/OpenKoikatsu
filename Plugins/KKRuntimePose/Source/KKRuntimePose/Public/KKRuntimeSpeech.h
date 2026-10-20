#pragma once
#include "CoreMinimal.h"
#include "Async/Future.h"
#include "Sound/SoundWaveProcedural.h"
#include "Sound/SoundGenerator.h"
#include "HAL/CriticalSection.h"
#include "Misc/ScopeLock.h"
#include "KKRuntimeSpeech.generated.h"
struct FKKRuntimeSpeechPcm {TArray<int16> Samples;int32 SampleRate=0,Channels=0;};
struct FKKRuntimeSpeechMeter {TAtomic<float> Rms{0};TAtomic<uint64> Frames{0};};
// PCM that is still arriving (e.g. a TTS response being streamed). Writers append from any thread;
// playback outputs silence while it waits and finishes once Complete is set and everything is played.
struct KKRUNTIMEPOSE_API FKKRuntimeSpeechStream {
 int32 SampleRate=24000,Channels=1;
 void Append(const int16* Data,int32 Count){FScopeLock Lock(&Guard);Samples.Append(Data,Count);}
 void Finish(){Complete.Store(true);}
 bool IsComplete() const{return Complete.Load();}
 int32 Num() const{FScopeLock Lock(&Guard);return Samples.Num();}
 // Copies samples [From, From+Count) that have arrived; returns how many were copied.
 int32 Read(int32 From,int32 Count,int16* Out) const{FScopeLock Lock(&Guard);const int32 N=FMath::Clamp(Samples.Num()-From,0,Count);if(N)FMemory::Memcpy(Out,Samples.GetData()+From,N*sizeof(int16));return N;}
private:
 mutable FCriticalSection Guard;TArray<int16> Samples;TAtomic<bool> Complete{false};
};
struct FKKRuntimeSpeechResult {TSharedPtr<const FKKRuntimeSpeechPcm,ESPMode::ThreadSafe> Pcm;TSharedPtr<FKKRuntimeSpeechStream,ESPMode::ThreadSafe> Stream;FString Error;};
KKRUNTIMEPOSE_API TFuture<FKKRuntimeSpeechResult> OKPrepareRuntimeSpeechAsync(FString CommonAudioFile);
UCLASS()
class KKRUNTIMEPOSE_API UKKRuntimeSpeechWave:public USoundWaveProcedural {
 GENERATED_BODY()
public:
 TSharedPtr<const FKKRuntimeSpeechPcm,ESPMode::ThreadSafe> Pcm;
 TSharedPtr<FKKRuntimeSpeechMeter,ESPMode::ThreadSafe> Meter;
 TSharedPtr<FKKRuntimeSpeechStream,ESPMode::ThreadSafe> Stream;
 void Initialize(TSharedPtr<const FKKRuntimeSpeechPcm,ESPMode::ThreadSafe>,int32 OutputRate);
 void InitializeStream(TSharedPtr<FKKRuntimeSpeechStream,ESPMode::ThreadSafe>,int32 OutputRate);
 virtual ISoundGeneratorPtr CreateSoundGenerator(const FSoundGeneratorInitParams&) override;
};
