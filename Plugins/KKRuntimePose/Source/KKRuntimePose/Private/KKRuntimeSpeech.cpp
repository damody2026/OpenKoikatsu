#include "KKRuntimeSpeech.h"
#include "Decoders/VorbisAudioInfo.h"
#include "Interfaces/IAudioFormat.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "Async/Async.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"
#include "HAL/CriticalSection.h"
namespace {
struct FCachedVoice {TSharedPtr<const FKKRuntimeSpeechPcm,ESPMode::ThreadSafe> Pcm;uint64 Touch=0;};
FCriticalSection VoiceCacheLock;
TMap<FString,FCachedVoice> VoiceCache;
uint64 VoiceCacheBytes=0,VoiceCacheClock=0;
constexpr uint64 VoiceCacheCapacity=64ull*1024*1024;
FORCENOINLINE bool SpeechDecodeFailed(const ICompressedAudioInfo& Decoder){return Decoder.HasError();}
class FRuntimeSpeechGenerator:public ISoundGenerator {
 TSharedPtr<const FKKRuntimeSpeechPcm,ESPMode::ThreadSafe> Pcm;
 TSharedPtr<FKKRuntimeSpeechMeter,ESPMode::ThreadSafe> Meter;
 double Position=0,Step=1;float Window[1024]{};uint32 Ring=0;uint64 Frames=0;
public:
 FRuntimeSpeechGenerator(TSharedPtr<const FKKRuntimeSpeechPcm,ESPMode::ThreadSafe> P,TSharedPtr<FKKRuntimeSpeechMeter,ESPMode::ThreadSafe> M,int32 Rate):Pcm(MoveTemp(P)),Meter(MoveTemp(M)){Step=double(Pcm->SampleRate)/Rate;}
 int32 OnGenerateAudio(float* Out,int32 Count) override {
  FMemory::Memzero(Out,Count*sizeof(float));const int32 Total=Pcm->Samples.Num()/Pcm->Channels;int32 Taken=0;
  while(Taken+Pcm->Channels<=Count&&Position<Total){const int32 I=int32(Position),J=FMath::Min(I+1,Total-1);const float Fraction=Position-I;float Energy=0;
   for(int32 C=0;C<Pcm->Channels;++C){const float V=FMath::Lerp(float(Pcm->Samples[I*Pcm->Channels+C]),float(Pcm->Samples[J*Pcm->Channels+C]),Fraction)/32768.f;Out[Taken+C]=V;Energy+=V*V;}
   Window[Ring]=Energy/Pcm->Channels;Ring=(Ring+1)%1024;Taken+=Pcm->Channels;Position+=Step;++Frames;
  }
  float Sum=0;for(float E:Window)Sum+=E;Meter->Rms.Store(Position>=Total?0:FMath::Sqrt(Sum/1024.f));Meter->Frames.Store(Frames);return Taken;
 }
 bool IsFinished() const override{return Position>=Pcm->Samples.Num()/Pcm->Channels;}
 int32 GetNumChannels() const override{return Pcm->Channels;}
};
// Same resampling and meter as above, over PCM that is still arriving. It starts once a short lead has arrived (0.3 s),
// and when the stream runs dry mid-line waits for the same lead again before going on.
static TAutoConsoleVariable<float> CVarOKSpeechStreamLead(TEXT("ok.Speech.StreamLead"),.3f,TEXT("Seconds of streamed speech buffered before it starts playing, and again after it runs dry while playing."));
class FRuntimeSpeechStreamGenerator:public ISoundGenerator {
 TSharedPtr<FKKRuntimeSpeechStream,ESPMode::ThreadSafe> Stream;
 TSharedPtr<FKKRuntimeSpeechMeter,ESPMode::ThreadSafe> Meter;
 double Position=0,Step=1;float Window[1024]{};uint32 Ring=0;uint64 Frames=0;TArray<int16> Chunk;bool Done=false;
 bool Buffering=true;
public:
 FRuntimeSpeechStreamGenerator(TSharedPtr<FKKRuntimeSpeechStream,ESPMode::ThreadSafe> S,TSharedPtr<FKKRuntimeSpeechMeter,ESPMode::ThreadSafe> M,int32 Rate):Stream(MoveTemp(S)),Meter(MoveTemp(M)){Step=double(Stream->SampleRate)/Rate;}
 int32 OnGenerateAudio(float* Out,int32 Count) override {
  FMemory::Memzero(Out,Count*sizeof(float));const int32 Channels=Stream->Channels;
  const bool Complete=Stream->IsComplete();const int32 Total=Stream->Num()/Channels;
  if(Buffering){
   const double Lead=FMath::Clamp(double(CVarOKSpeechStreamLead.GetValueOnAnyThread()),0.,3.);
   if(!Complete&&Total-Position<Lead*Stream->SampleRate)return Count;
   Buffering=false;
  }
  // Frames needed for this callback (plus one for interpolation), read in one locked copy.
  const int32 First=int32(Position),Need=int32(Position+Step*(Count/Channels))-First+2;
  Chunk.SetNumUninitialized(Need*Channels);const int32 Got=Stream->Read(First*Channels,Need*Channels,Chunk.GetData())/Channels;
  int32 Taken=0;
  while(Taken+Channels<=Count){const int32 I=int32(Position)-First;
   if(I>=Got||(I+1>=Got&&!Complete))break;
   const int32 J=FMath::Min(I+1,Got-1);const float Fraction=Position-int32(Position);float Energy=0;
   for(int32 C=0;C<Channels;++C){const float V=FMath::Lerp(float(Chunk[I*Channels+C]),float(Chunk[J*Channels+C]),Fraction)/32768.f;Out[Taken+C]=V;Energy+=V*V;}
   Window[Ring]=Energy/Channels;Ring=(Ring+1)%1024;Taken+=Channels;Position+=Step;++Frames;
  }
  // Ran dry while playing: wait for the lead again before the rest.
  if(!Complete&&Taken+Channels<=Count){Buffering=true;UE_LOG(LogTemp,Display,TEXT("Speech stream ran dry at %.2f s"),Position/Stream->SampleRate);}
  Done=Complete&&Position>=Total;
  float Sum=0;for(float E:Window)Sum+=E;Meter->Rms.Store(Done?0:FMath::Sqrt(Sum/1024.f));Meter->Frames.Store(Frames);
  // A starved stream keeps the voice alive with silence instead of ending it.
  return Done?Taken:Count;
 }
 bool IsFinished() const override{return Done;}
 int32 GetNumChannels() const override{return Stream->Channels;}
};
}
TFuture<FKKRuntimeSpeechResult> OKPrepareRuntimeSpeechAsync(FString Path){return Async(EAsyncExecution::ThreadPool,[Path=MoveTemp(Path)]{
 FString Full=FPaths::ConvertRelativePathToFull(Path);FPaths::NormalizeFilename(Full);
 const FString Key=Full+TEXT("|")+FString::Printf(TEXT("%lld|%lld"),IFileManager::Get().FileSize(*Full),IFileManager::Get().GetTimeStamp(*Full).GetTicks());
 {FScopeLock Lock(&VoiceCacheLock);if(auto* Cached=VoiceCache.Find(Key)){Cached->Touch=++VoiceCacheClock;FKKRuntimeSpeechResult Hit;Hit.Pcm=Cached->Pcm;return Hit;}}
 FKKRuntimeSpeechResult R;const int64 Size=IFileManager::Get().FileSize(*Path);TArray<uint8> Bytes;
 if(Size<4||Size>16*1024*1024||!FFileHelper::LoadFileToArray(Bytes,*Path)||FMemory::Memcmp(Bytes.GetData(),"OggS",4)){R.Error=TEXT("Invalid common OGG voice resource");return R;}
 LoadVorbisLibraries();FVorbisAudioInfo Decoder;FSoundQualityInfo Q{};
 if(!Decoder.ReadCompressedInfo(Bytes.GetData(),Bytes.Num(),&Q)||Q.NumChannels<1||Q.NumChannels>2||Q.SampleRate<8000||Q.SampleRate>96000||!Q.SampleDataSize||Q.SampleDataSize>64*1024*1024||Q.SampleDataSize%(2*Q.NumChannels)){R.Error=TEXT("Unsupported runtime voice stream");return R;}
 auto P=MakeShared<FKKRuntimeSpeechPcm,ESPMode::ThreadSafe>();P->SampleRate=Q.SampleRate;P->Channels=Q.NumChannels;P->Samples.SetNumZeroed(Q.SampleDataSize/2);Decoder.ExpandFile(reinterpret_cast<uint8*>(P->Samples.GetData()),&Q);
 if(SpeechDecodeFailed(Decoder)){R.Error=TEXT("Runtime OGG decode failed");return R;}
 {FScopeLock Lock(&VoiceCacheLock);if(auto* Existing=VoiceCache.Find(Key)){Existing->Touch=++VoiceCacheClock;R.Pcm=Existing->Pcm;return R;}
  const uint64 DecodedBytes=uint64(P->Samples.Num())*sizeof(int16);
  while(VoiceCacheBytes+DecodedBytes>VoiceCacheCapacity&&!VoiceCache.IsEmpty()){FString Oldest;uint64 Touch=MAX_uint64;for(const auto& Pair:VoiceCache)if(Pair.Value.Pcm.GetSharedReferenceCount()==1&&Pair.Value.Touch<Touch){Touch=Pair.Value.Touch;Oldest=Pair.Key;}if(Oldest.IsEmpty())break;VoiceCacheBytes-=uint64(VoiceCache[Oldest].Pcm->Samples.Num())*sizeof(int16);VoiceCache.Remove(Oldest);}
  if(VoiceCacheBytes+DecodedBytes<=VoiceCacheCapacity){FCachedVoice Cached;Cached.Pcm=P;Cached.Touch=++VoiceCacheClock;VoiceCache.Add(Key,MoveTemp(Cached));VoiceCacheBytes+=DecodedBytes;}}
 R.Pcm=MoveTemp(P);return R;
 });}
void UKKRuntimeSpeechWave::Initialize(TSharedPtr<const FKKRuntimeSpeechPcm,ESPMode::ThreadSafe> P,int32 Rate){check(IsInGameThread());check(P&&Rate>0);Pcm=MoveTemp(P);Stream.Reset();Meter=MakeShared<FKKRuntimeSpeechMeter,ESPMode::ThreadSafe>();NumChannels=Pcm->Channels;SetSampleRate(Rate);Duration=double(Pcm->Samples.Num())/(Pcm->Channels*Pcm->SampleRate);SetNumFrames(FMath::CeilToInt(Duration*Rate));SoundGroup=SOUNDGROUP_Voice;VirtualizationMode=EVirtualizationMode::PlayWhenSilent;bLooping=false;}
void UKKRuntimeSpeechWave::InitializeStream(TSharedPtr<FKKRuntimeSpeechStream,ESPMode::ThreadSafe> S,int32 Rate){check(IsInGameThread());check(S&&Rate>0);Stream=MoveTemp(S);Pcm.Reset();Meter=MakeShared<FKKRuntimeSpeechMeter,ESPMode::ThreadSafe>();NumChannels=Stream->Channels;SetSampleRate(Rate);Duration=600;SetNumFrames(FMath::CeilToInt(Duration*Rate));SoundGroup=SOUNDGROUP_Voice;VirtualizationMode=EVirtualizationMode::PlayWhenSilent;bLooping=false;}
ISoundGeneratorPtr UKKRuntimeSpeechWave::CreateSoundGenerator(const FSoundGeneratorInitParams& Params){
 if(Stream)return MakeShared<FRuntimeSpeechStreamGenerator,ESPMode::ThreadSafe>(Stream,Meter,FMath::RoundToInt(Params.SampleRate));
 return MakeShared<FRuntimeSpeechGenerator,ESPMode::ThreadSafe>(Pcm,Meter,FMath::RoundToInt(Params.SampleRate));}
