#include "KKSpeech.h"
#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"
#include "HAL/FileManager.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace {
struct FBreathInfo {TArray<int32> Ids,Cond,Voices;};
struct FBreathPattern {int32 Mode=0;FString Anim;TArray<FBreathInfo> Info;};
struct FBreathIndex {
    FString Root;
    TArray<FBreathPattern> Patterns;
    TMap<FString,FString> Files;
    TMap<FString,TArray<int32>> Faces;
    TMap<FString,int32> Mouths;
    TMap<FString,float> OpenMins;
    TMap<FString,int32> MainVoice;
};
FString Key3(int32 A,int32 B,int32 C){return FString::Printf(TEXT("%d|%d|%d"),A,B,C);}
FString Key2(int32 A,int32 B){return FString::Printf(TEXT("%d|%d"),A,B);}
bool ReadInts(const TSharedPtr<FJsonObject>& Row,const TCHAR* Field,TArray<int32>& Out){
    const TArray<TSharedPtr<FJsonValue>>* Values=nullptr;if(!Row->TryGetArrayField(Field,Values))return false;
    for(const TSharedPtr<FJsonValue>& Value:*Values){double Number=0.;if(!Value.IsValid()||!Value->TryGetNumber(Number))return false;Out.Add(int32(Number));}
    return true;
}
bool LoadIndex(const FString& Root,TSharedPtr<const FBreathIndex,ESPMode::ThreadSafe>& Index,FString& Error){
    struct FEntry {int64 Size;FDateTime Modified;TSharedPtr<const FBreathIndex,ESPMode::ThreadSafe> Value;};
    static FCriticalSection Lock;static TMap<FString,FEntry> Cache;
    FString CachePath=FPaths::ConvertRelativePathToFull(Root/TEXT("h_breath_index.json"));FPaths::NormalizeFilename(CachePath);
    const int64 Size=IFileManager::Get().FileSize(*CachePath);const auto Modified=IFileManager::Get().GetTimeStamp(*CachePath);
    {FScopeLock Guard(&Lock);if(const auto* Entry=Cache.Find(CachePath);Entry&&Entry->Size==Size&&Entry->Modified==Modified){Index=Entry->Value;return true;}}
    FString Source;TSharedPtr<FJsonObject> Json;
    const FString Path=Root/TEXT("h_breath_index.json");
    if(!FFileHelper::LoadFileToString(Source,*Path)||!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Source),Json)||!Json.IsValid()){
        Error=TEXT("找不到性愛喘息索引 h_breath_index.json");return false;
    }
    FBreathIndex Fresh;Fresh.Root=Root;
    const TArray<TSharedPtr<FJsonValue>>* Patterns=nullptr;
    if(!Json->TryGetArrayField(TEXT("patterns"),Patterns)){Error=TEXT("性愛喘息索引沒有動作表");return false;}
    for(const TSharedPtr<FJsonValue>& Value:*Patterns){
        const TSharedPtr<FJsonObject> Row=Value.IsValid()?Value->AsObject():nullptr;if(!Row.IsValid())continue;
        FBreathPattern Pattern;double Mode=0.;FString Anim;
        if(!Row->TryGetNumberField(TEXT("m"),Mode)||!Row->TryGetStringField(TEXT("anim"),Anim))continue;
        Pattern.Mode=int32(Mode);Pattern.Anim=Anim;
        const TArray<TSharedPtr<FJsonValue>>* Info=nullptr;if(!Row->TryGetArrayField(TEXT("info"),Info))continue;
        for(const TSharedPtr<FJsonValue>& InfoValue:*Info){
            const TSharedPtr<FJsonObject> InfoRow=InfoValue.IsValid()?InfoValue->AsObject():nullptr;if(!InfoRow.IsValid())continue;
            FBreathInfo Item;if(!ReadInts(InfoRow,TEXT("ids"),Item.Ids)||!ReadInts(InfoRow,TEXT("cond"),Item.Cond)||!ReadInts(InfoRow,TEXT("voices"),Item.Voices))continue;
            Pattern.Info.Add(MoveTemp(Item));
        }
        Fresh.Patterns.Add(MoveTemp(Pattern));
    }
    const TArray<TSharedPtr<FJsonValue>>* Clips=nullptr;
    if(!Json->TryGetArrayField(TEXT("clips"),Clips)){Error=TEXT("性愛喘息索引沒有音檔表");return false;}
    for(const TSharedPtr<FJsonValue>& Value:*Clips){
        const TSharedPtr<FJsonObject> Row=Value.IsValid()?Value->AsObject():nullptr;if(!Row.IsValid())continue;
        double P=0.,M=0.,Id=0.;FString File;
        if(!Row->TryGetNumberField(TEXT("p"),P)||!Row->TryGetNumberField(TEXT("m"),M)||!Row->TryGetNumberField(TEXT("id"),Id)||!Row->TryGetStringField(TEXT("file"),File))continue;
        const FString Key=Key3(int32(P),int32(M),int32(Id));
        Fresh.Files.Add(Key,File);TArray<int32> FaceIds;ReadInts(Row,TEXT("faces"),FaceIds);Fresh.Faces.Add(Key,MoveTemp(FaceIds));
    }
    const TArray<TSharedPtr<FJsonValue>>* Mouths=nullptr;
    if(Json->TryGetArrayField(TEXT("mouths"),Mouths))for(const TSharedPtr<FJsonValue>& Value:*Mouths){
        const TSharedPtr<FJsonObject> Row=Value.IsValid()?Value->AsObject():nullptr;if(!Row.IsValid())continue;
        double P=0.,M=0.,Id=0.,Mouth=0.,Open=0.;
        if(!Row->TryGetNumberField(TEXT("p"),P)||!Row->TryGetNumberField(TEXT("m"),M)||!Row->TryGetNumberField(TEXT("id"),Id)||!Row->TryGetNumberField(TEXT("mouth"),Mouth)||!Row->TryGetNumberField(TEXT("open"),Open))continue;
        const FString Key=Key3(int32(P),int32(M),int32(Id));Fresh.Mouths.Add(Key,int32(Mouth));Fresh.OpenMins.Add(Key,float(Open));
    }
    const TArray<TSharedPtr<FJsonValue>>* Voices=nullptr;
    if(Json->TryGetArrayField(TEXT("mainVoice"),Voices))for(const TSharedPtr<FJsonValue>& Value:*Voices){
        const TSharedPtr<FJsonObject> Row=Value.IsValid()?Value->AsObject():nullptr;if(!Row.IsValid())continue;
        double M=0.,Id=0.,Voice=-1.;
        if(!Row->TryGetNumberField(TEXT("m"),M)||!Row->TryGetNumberField(TEXT("id"),Id)||!Row->TryGetNumberField(TEXT("v"),Voice))continue;
        Fresh.MainVoice.Add(Key2(int32(M),int32(Id)),int32(Voice));
    }
    if(Fresh.Patterns.IsEmpty()||Fresh.Files.IsEmpty()){Error=TEXT("性愛喘息索引是空的");return false;}
    Index=MakeShared<FBreathIndex,ESPMode::ThreadSafe>(MoveTemp(Fresh));
    {FScopeLock Guard(&Lock);if(Cache.Num()>=8)Cache.Reset();Cache.Add(CachePath,FEntry{Size,Modified,Index});}return true;
}
bool Allows(int32 Condition,bool bMoving,int32 MainVoice){
    // Gallery has no pleasure gauge, hand, kiss or speed-motion toggle.
    // Those stay at the scene-start defaults HVoiceCtrl uses before any of them are set.
    if(Condition>=90)return false;
    switch(Condition){
    case 0:return false;
    case 1:case 3:case 4:case 6:case 9:case 10:case 15:case 16:return true;
    case 2:case 5:case 7:case 8:case 11:case 14:case 17:return false;
    case 12:return bMoving;
    case 13:return !bMoving;
    case 18:return MainVoice<0||MainVoice==0;
    case 19:return MainVoice<0||MainVoice==1;
    default:return false;
    }
}
}
bool KKSpeech::PrepareHBreathIndex(const FString& Root,FString& Error){
    TSharedPtr<const FBreathIndex,ESPMode::ThreadSafe> Index;Error.Reset();return LoadIndex(Root,Index,Error);
}
bool KKSpeech::CollectHBreath(const FString& Root,int32 Personality,int32 Mode,const FString& State,int32 AnimId,bool bMoving,TArray<FKKHBreathCue>& Out,FString& Error){
    Out.Reset();Error.Reset();
    if(Personality<0||Personality>39||Mode<0||Mode>7||State.IsEmpty()){Error=TEXT("沒有對應的女角性愛喘息");return false;}
    TSharedPtr<const FBreathIndex,ESPMode::ThreadSafe> SharedIndex;if(!LoadIndex(Root,SharedIndex,Error))return false;const FBreathIndex& Index=*SharedIndex;
    const FBreathPattern* Pattern=Index.Patterns.FindByPredicate([&](const FBreathPattern& Item){return Item.Mode==Mode&&Item.Anim==State;});
    if(!Pattern){Error=FString::Printf(TEXT("動作 %s 沒有喘息表"),*State);return false;}
    const int32* MainVoiceValue=Index.MainVoice.Find(Key2(Mode,AnimId));
    const int32 MainVoice=MainVoiceValue?*MainVoiceValue:-1;
    // Modes 6 and 7 reuse the houshi / sonyu clip banks. Face lists stay on the real mode.
    const int32 ClipMode=Mode==6?1:Mode==7?2:Mode;
    TSet<int32> Seen;
    for(const FBreathInfo& Info:Pattern->Info){
        if(Info.Ids.Num()&&!Info.Ids.Contains(-1)&&!Info.Ids.Contains(AnimId))continue;
        bool bAllowed=true;for(const int32 Condition:Info.Cond)if(!Allows(Condition,bMoving,MainVoice)){bAllowed=false;break;}
        if(!bAllowed)continue;
        for(const int32 Voice:Info.Voices){
            if(Seen.Contains(Voice))continue;
            const FString* File=Index.Files.Find(Key3(Personality,ClipMode,Voice));if(!File)continue;
            FKKHBreathCue Cue;Cue.Name=*File;Cue.Path=Root/FString::Printf(TEXT("audio/c%02d/h/%s.ogg"),Personality,**File);
            if(!IFileManager::Get().FileExists(*Cue.Path))continue;
            if(const TArray<int32>* FaceIds=Index.Faces.Find(Key3(Personality,ClipMode,Voice)))for(const int32 Face:*FaceIds){
                const FString FaceKey=Key3(Personality,Mode,Face);
                if(const int32* Mouth=Index.Mouths.Find(FaceKey)){Cue.Mouths.Add(*Mouth);Cue.OpenMins.Add(Index.OpenMins.FindRef(FaceKey));}
            }
            Seen.Add(Voice);Out.Add(MoveTemp(Cue));
        }
    }
    Out.Sort([](const FKKHBreathCue& A,const FKKHBreathCue& B){return A.Name<B.Name;});
    if(Out.IsEmpty()){Error=FString::Printf(TEXT("%s 在這個條件下沒有已擷取的喘息"),*State);return false;}
    return true;
}
