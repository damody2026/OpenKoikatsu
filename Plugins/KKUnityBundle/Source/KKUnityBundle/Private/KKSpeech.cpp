#include "KKSpeech.h"
#include "Decoders/VorbisAudioInfo.h"
#include "Interfaces/IAudioFormat.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"
#include "Async/Async.h"
#include "Misc/ScopeLock.h"

namespace {
// HasError is a virtual interface entry, not exported as a concrete Vorbis symbol.
FORCENOINLINE bool DecodeFailed(const ICompressedAudioInfo& Decoder){return Decoder.HasError();}
bool PlainId(const FString& Id) {
    if(Id.IsEmpty())return false;
    for(TCHAR C:Id)if(!FChar::IsAlnum(C)&&C!='_'&&C!='-')return false;
    return true;
}
class FSpeechGenerator final:public ISoundGenerator {
    TSharedPtr<const FKKSpeechPcm,ESPMode::ThreadSafe> Pcm;
    TSharedPtr<FKKSpeechMeter,ESPMode::ThreadSafe> Meter;
    double Position=0.,Step=1.;
    int32 Rendered=0,RingIndex=0;
    float Window[1024]{};
public:
    FSpeechGenerator(TSharedPtr<const FKKSpeechPcm,ESPMode::ThreadSafe> In,TSharedPtr<FKKSpeechMeter,ESPMode::ThreadSafe> State,float Start,int32 Rate):Pcm(MoveTemp(In)),Meter(MoveTemp(State)) {
        Position=FMath::Clamp(double(FMath::IsFinite(Start)?Start:0.f)*Pcm->SampleRate,0.,double(Pcm->Samples.Num()/Pcm->Channels));
        Step=double(Pcm->SampleRate)/(Rate>0?Rate:Pcm->SampleRate);
    }
    int32 OnGenerateAudio(float* Out,int32 Count) override {
        FMemory::Memzero(Out,Count*sizeof(float));
        const int32 Frames=Pcm->Samples.Num()/Pcm->Channels;
        const double StartAudioSeconds=Pcm->SampleRate>0?Position/double(Pcm->SampleRate):0.;
        const double NowWallSeconds=FPlatformTime::Seconds();
        int32 Taken=0;
        while(Taken+Pcm->Channels<=Count && Position<Frames){
            const int32 I=int32(Position),J=FMath::Min(I+1,Frames-1);
            const float Fraction=float(Position-I);
            for(int32 C=0;C<Pcm->Channels;++C)
                Out[Taken+C]=FMath::Lerp(float(Pcm->Samples[I*Pcm->Channels+C]),float(Pcm->Samples[J*Pcm->Channels+C]),Fraction)/32768.f;
            Window[RingIndex]=Out[Taken];RingIndex=(RingIndex+1)%1024;
            Taken+=Pcm->Channels;Position+=Step;++Rendered;
        }
        float Sum=0.f;for(float V:Window)Sum+=V*V;
        if(Meter){
            Meter->Rms.Store(FMath::Sqrt(Sum/1024.f));Meter->RenderedFrames.Store(Rendered);
            Meter->WriteClockAnchor(StartAudioSeconds,NowWallSeconds,Position>=Frames);
        }
        return Taken;
    }
    bool IsFinished() const override{return Position>=Pcm->Samples.Num()/Pcm->Channels;}
    int32 GetNumChannels() const override{return Pcm->Channels;}
};
}

namespace {
using FLinesResult=TTuple<bool,TArray<FKKSpeechLine>,FString>;
FLinesResult LoadLinesUncached(const FString& Root,int32 Personality);
// Parsed lines by index file (path, size, time) and voice: parsing the whole index was ~0.5 s of each card load's game
// thread. Failures are not kept (the voices may be extracted later).
FCriticalSection LinesLock;
TMap<FString,TSharedFuture<FLinesResult>> LinesCache;
FString LinesKey(const FString& Root,int32 Personality) {
    const FString Index=Root/TEXT("comm_lines.json");
    return FString::Printf(TEXT("%s|%d|%lld|%s"),*Index,Personality,IFileManager::Get().FileSize(*Index),*IFileManager::Get().GetTimeStamp(*Index).ToString());
}
TSharedFuture<FLinesResult> LinesFuture(const FString& Root,int32 Personality,bool bAsync) {
    const FString Key=LinesKey(Root,Personality);
    {FScopeLock Lock(&LinesLock);if(const auto* Found=LinesCache.Find(Key))return *Found;}
    TSharedFuture<FLinesResult> Future;
    if(bAsync)Future=Async(EAsyncExecution::ThreadPool,[Root,Personality](){return LoadLinesUncached(Root,Personality);}).Share();
    else {TPromise<FLinesResult> Ready;Ready.SetValue(LoadLinesUncached(Root,Personality));Future=Ready.GetFuture().Share();}
    FScopeLock Lock(&LinesLock);
    if(const auto* Found=LinesCache.Find(Key))return *Found;
    return LinesCache.Add(Key,Future);
}
}

void KKSpeech::PrefetchLines(const FString& Root,int32 Personality) {
    if(Root.IsEmpty()||Personality<0)return;
    LinesFuture(Root,Personality,true);
}

bool KKSpeech::LoadLines(const FString& Root,int32 Personality,TArray<FKKSpeechLine>& Lines,FString& Error) {
    const FString Key=LinesKey(Root,Personality);
    const TSharedFuture<FLinesResult> Future=LinesFuture(Root,Personality,false);
    const FLinesResult& Result=Future.Get();
    Lines=Result.Get<1>();Error=Result.Get<2>();
    if(!Result.Get<0>()){FScopeLock Lock(&LinesLock);LinesCache.Remove(Key);}
    return Result.Get<0>();
}

namespace {
FLinesResult LoadLinesUncached(const FString& Root,int32 Personality) {
    TArray<FKKSpeechLine> Lines;FString Error;
    const bool Ok=[&]{
    FString Source;TSharedPtr<FJsonObject> Json;
    if(!FFileHelper::LoadFileToString(Source,*(Root/TEXT("comm_lines.json")))||!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Source),Json)||!Json.IsValid()){
        Error=TEXT("找不到或無法讀取一般對話語音索引 comm_lines.json");return false;
    }
    const FString Bundle=FString::Printf(TEXT("sound/data/pcm/c%02d/adv/00.unity3d"),Personality);
    for(const auto& Entry:Json->Values){
        if(!Entry.Value.IsValid()||Entry.Value->Type!=EJson::Object)continue;
        const TSharedPtr<FJsonObject> Row=Entry.Value->AsObject();if(!Row.IsValid())continue;
        FString Src,Asset,AssetBundle,Text;
        if(!Row->TryGetStringField(TEXT("src"),Src)||!Src.StartsWith(TEXT("TopicTalkCommon_"))||
            !Row->TryGetStringField(TEXT("file"),Asset)||!Asset.StartsWith(TEXT("sun_tk_"))||!PlainId(Asset)||
            !Row->TryGetStringField(TEXT("assetbundle"),AssetBundle)||AssetBundle!=Bundle)continue;
        Row->TryGetStringField(TEXT("text"),Text);
        FKKSpeechLine Line;Line.Id=Asset;Line.Text=Text;Line.Personality=Personality;
        Line.AudioPath=Root/FString::Printf(TEXT("audio/c%02d/adv/%s.ogg"),Personality,*Asset);
        if(IFileManager::Get().FileExists(*Line.AudioPath))Lines.Add(MoveTemp(Line));
    }
    Lines.Sort([](const FKKSpeechLine& A,const FKKSpeechLine& B){return A.Id<B.Id;});
    if(Lines.IsEmpty()){Error=FString::Printf(TEXT("性格 %d 沒有已擷取的一般話題語音"),Personality);return false;}
    return true;
    }();
    return FLinesResult(Ok,MoveTemp(Lines),MoveTemp(Error));
}
}

bool KKSpeech::DecodeOgg(const FString& Path,TSharedPtr<const FKKSpeechPcm,ESPMode::ThreadSafe>& Out,FString& Error) {
    Out.Reset();Error.Reset();const int64 Size=IFileManager::Get().FileSize(*Path);
    if(Size<4||Size>128*1024*1024){Error=TEXT("音檔不存在、為空或超過128MiB");return false;}
    TArray<uint8> Bytes;
    if(!FFileHelper::LoadFileToArray(Bytes,*Path)||FMemory::Memcmp(Bytes.GetData(),"OggS",4)!=0){Error=TEXT("音檔不是有效的OGG Vorbis");return false;}
    LoadVorbisLibraries();FVorbisAudioInfo Decoder;FSoundQualityInfo Quality{};
    if(!Decoder.ReadCompressedInfo(Bytes.GetData(),Bytes.Num(),&Quality)||Quality.NumChannels<1||Quality.NumChannels>2||Quality.SampleRate<8000||Quality.SampleRate>96000||Quality.SampleDataSize==0||Quality.SampleDataSize>512*1024*1024||Quality.SampleDataSize%(2*Quality.NumChannels)!=0){
        Error=TEXT("OGG解碼失敗或聲道、取樣率、PCM長度不受支援");return false;
    }
    auto Pcm=MakeShared<FKKSpeechPcm,ESPMode::ThreadSafe>();Pcm->SampleRate=Quality.SampleRate;Pcm->Channels=Quality.NumChannels;
    Pcm->Samples.SetNumZeroed(Quality.SampleDataSize/2);Decoder.ExpandFile(reinterpret_cast<uint8*>(Pcm->Samples.GetData()),&Quality);
    if(DecodeFailed(Decoder)){Error=TEXT("OGG PCM解碼失敗");return false;}
    Out=Pcm;return true;
}

float KKSpeech::MouthRate(float Rms,float& Previous) {
    const float Current=FMath::Clamp(FMath::IsFinite(Rms)?Rms*3.f:0.f,0.f,1.f);
    const float Result=Current<Previous?Current*.2f+Previous*.8f:(Current+Previous)*.5f;
    Previous=Current;return Result;
}
ISoundGeneratorPtr KKSpeech::MakeGenerator(TSharedPtr<const FKKSpeechPcm,ESPMode::ThreadSafe> Pcm,TSharedPtr<FKKSpeechMeter,ESPMode::ThreadSafe> Meter,float StartSeconds,int32 OutputRate) {
    return MakeShared<FSpeechGenerator,ESPMode::ThreadSafe>(MoveTemp(Pcm),MoveTemp(Meter),StartSeconds,OutputRate);
}
void UKKSpeechWave::Init(TSharedPtr<const FKKSpeechPcm,ESPMode::ThreadSafe> InPcm,int32 PlaybackSampleRate) {
    Pcm=MoveTemp(InPcm);Meter=MakeShared<FKKSpeechMeter,ESPMode::ThreadSafe>();
    NumChannels=Pcm->Channels;SetSampleRate(PlaybackSampleRate);
    Duration=float(Pcm->Samples.Num()/NumChannels)/Pcm->SampleRate;SetNumFrames(FMath::CeilToInt(Duration*PlaybackSampleRate));
    SoundGroup=SOUNDGROUP_Voice;bLooping=false;
}
ISoundGeneratorPtr UKKSpeechWave::CreateSoundGenerator(const FSoundGeneratorInitParams& Params) {
    return KKSpeech::MakeGenerator(Pcm,Meter,Params.StartTime,FMath::RoundToInt(Params.SampleRate));
}
