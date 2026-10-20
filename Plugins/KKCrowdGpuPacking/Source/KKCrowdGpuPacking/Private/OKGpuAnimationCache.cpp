#include "OKGpuAnimationCache.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Async/Async.h"

namespace {
constexpr uint32 CacheMagic=0x41474b4f,CacheVersion=1,CacheLayout=0x00100403;
constexpr uint64 CacheHeaderBytes=60,CacheCountBytes=44;
template<typename T>void WriteArray(FArchive& A,const TArray<T>& V){A.Serialize(const_cast<T*>(V.GetData()),uint64(V.Num())*sizeof(T));}
template<typename T>void ReadArray(FArchive& A,TArray<T>& V,uint32 N){V.SetNumUninitialized(N);A.Serialize(V.GetData(),uint64(N)*sizeof(T));}
bool Fail(FString& Error,const TCHAR* Why){Error=Why;return false;}
}
bool FOKGpuAnimationCacheData::Validate(FString& Error) const {
 if(!OKValidateOwnedGpuPose(Pose,OwnedClips,Error)||!Palette.Validate(Error))return false;
 if(Palette.PoseCount!=uint32(Pose.Tasks.Num()))return Fail(Error,TEXT("Cache pose/palette bone counts differ"));return true;
}
bool OKEncodeGpuAnimationCache(const FOKGpuAnimationCacheData& D,const FSHAHash& Identity,TArray<uint8>& Out,FString& Error,uint64 MaxBytes){
 MaxBytes=FMath::Min(MaxBytes,uint64(MAX_int32)-CacheHeaderBytes);
 static_assert(sizeof(FVector4f)==16&&sizeof(FUintVector4)==16);
 if(!PLATFORM_LITTLE_ENDIAN)return Fail(Error,TEXT("Cache requires little-endian float32 platform"));
 if(!D.Validate(Error))return false;
 TArray<uint32> ClipCounts,Clips;for(const auto& Owned:D.OwnedClips){ClipCounts.Add(Owned.Num());Clips.Append(Owned);}
 uint32 Counts[]={uint32(D.Pose.Keys.Num()),uint32(D.Pose.Tracks.Num()),uint32(D.Pose.Actors.Num()),uint32(D.Pose.Tasks.Num()),uint32(D.Pose.InverseBindRows.Num()),uint32(D.Pose.Times.Num()),uint32(D.Palette.Items.Num()),uint32(D.Palette.InverseBindRows.Num()),uint32(ClipCounts.Num()),uint32(Clips.Num())};
 uint64 Size=CacheCountBytes;for(int I=0;I<10;++I)Size+=uint64(Counts[I])*(I<8?16:4);
 if(Size>MaxBytes||Size+CacheHeaderBytes>MAX_int32)return Fail(Error,TEXT("Cache exceeds byte budget"));
 TArray<uint8> Payload;Payload.Reserve(Size);FMemoryWriter P(Payload,true);P.Serialize(Counts,sizeof(Counts));uint32 Bones=D.Palette.PoseCount;P<<Bones;
 WriteArray(P,D.Pose.Keys);WriteArray(P,D.Pose.Tracks);WriteArray(P,D.Pose.Actors);WriteArray(P,D.Pose.Tasks);WriteArray(P,D.Pose.InverseBindRows);WriteArray(P,D.Pose.Times);WriteArray(P,D.Palette.Items);WriteArray(P,D.Palette.InverseBindRows);WriteArray(P,ClipCounts);WriteArray(P,Clips);
 if(P.IsError()||uint64(Payload.Num())!=Size)return Fail(Error,TEXT("Cache serialization size mismatch"));
 auto Digest=FSHA1::HashBuffer(Payload.GetData(),Payload.Num());TArray<uint8> Bytes;Bytes.Reserve(Size+CacheHeaderBytes);FMemoryWriter W(Bytes,true);const bool Columns=D.Pose.Tracks.ContainsByPredicate([](const auto& T){return OKGpuTrackColumn(T);});const bool Packed=D.Pose.Tracks.ContainsByPredicate([](const auto& T){return OKGpuTrackPacked(T);});uint32 M=CacheMagic,V=Columns?3:Packed?2:CacheVersion,L=Columns?0x00100423:Packed?0x00100413:CacheLayout;W<<M<<V<<L<<Size;W.Serialize(const_cast<uint8*>(Identity.Hash),20);W.Serialize(Digest.Hash,20);W.Serialize(Payload.GetData(),Payload.Num());if(W.IsError())return Fail(Error,TEXT("Cache header write failed"));Out=MoveTemp(Bytes);return true;
}
bool OKDecodeGpuAnimationCache(TConstArrayView<uint8> Bytes,const FSHAHash& Identity,FOKGpuAnimationCacheData& Out,FString& Error,uint64 MaxBytes){
 MaxBytes=FMath::Min(MaxBytes,uint64(MAX_int32)-CacheHeaderBytes);
 if(!PLATFORM_LITTLE_ENDIAN||Bytes.Num()<CacheHeaderBytes||uint64(Bytes.Num())>MaxBytes+CacheHeaderBytes)return Fail(Error,TEXT("Cache file size/platform invalid"));
 // FMemoryReaderView uses caller bytes directly; no array allocations yet.
 FMemoryReaderView R(Bytes,true);uint32 M=0,V=0,L=0;uint64 Size=0;FSHAHash Source,Digest;R<<M<<V<<L<<Size;R.Serialize(Source.Hash,20);R.Serialize(Digest.Hash,20);
 if(R.IsError()||M!=CacheMagic||!((V==CacheVersion&&L==CacheLayout)||(V==2&&L==0x00100413)||(V==3&&L==0x00100423))||Size<CacheCountBytes||Size>MaxBytes||Size!=uint64(Bytes.Num())-CacheHeaderBytes||FMemory::Memcmp(Source.Hash,Identity.Hash,20))return Fail(Error,TEXT("Cache version/layout/source/size mismatch"));
 auto Actual=FSHA1::HashBuffer(Bytes.GetData()+CacheHeaderBytes,Size);if(FMemory::Memcmp(Actual.Hash,Digest.Hash,20))return Fail(Error,TEXT("Cache payload checksum mismatch"));
 uint32 Counts[10],Bones=0;R.Serialize(Counts,sizeof(Counts));R<<Bones;uint64 Required=CacheCountBytes;
 for(int I=0;I<10;++I){if(Counts[I]>MAX_int32)return Fail(Error,TEXT("Cache array exceeds index capacity"));Required+=uint64(Counts[I])*(I<8?16:4);}
 if(R.IsError()||Required!=Size||!Counts[2]||Counts[2]!=Counts[5]||Counts[2]!=Counts[8]||Counts[3]!=Bones||Counts[9]<Counts[8])return Fail(Error,TEXT("Cache count table invalid"));
 FOKGpuAnimationCacheData D;ReadArray(R,D.Pose.Keys,Counts[0]);ReadArray(R,D.Pose.Tracks,Counts[1]);ReadArray(R,D.Pose.Actors,Counts[2]);ReadArray(R,D.Pose.Tasks,Counts[3]);ReadArray(R,D.Pose.InverseBindRows,Counts[4]);ReadArray(R,D.Pose.Times,Counts[5]);ReadArray(R,D.Palette.Items,Counts[6]);ReadArray(R,D.Palette.InverseBindRows,Counts[7]);D.Palette.PoseCount=Bones;
 TArray<uint32> ClipCounts,Clips;ReadArray(R,ClipCounts,Counts[8]);ReadArray(R,Clips,Counts[9]);uint64 Offset=0;D.OwnedClips.Reserve(Counts[8]);for(uint32 N:ClipCounts){if(!N||Offset+N>uint64(Clips.Num()))return Fail(Error,TEXT("Cache clip ownership lengths invalid"));TArray<uint32> Owned;Owned.Append(Clips.GetData()+Offset,N);D.OwnedClips.Add(MoveTemp(Owned));Offset+=N;}
 if(R.IsError()||R.Tell()!=Bytes.Num()||Offset!=uint64(Clips.Num())||!D.Validate(Error))return false;
 Out=MoveTemp(D);return true;
}
bool OKSaveGpuAnimationCache(const FString& Path,const FOKGpuAnimationCacheData& D,const FSHAHash& Identity,FString& Error,uint64 MaxBytes){
 TArray<uint8> Bytes;if(!OKEncodeGpuAnimationCache(D,Identity,Bytes,Error,MaxBytes))return false;
 auto& FM=IFileManager::Get();if(FM.FileExists(*Path))return Fail(Error,TEXT("Immutable cache already exists"));
 if(!FM.MakeDirectory(*FPaths::GetPath(Path),true))return Fail(Error,TEXT("Cache directory creation failed"));
 FString Temp=Path+TEXT(".")+FGuid::NewGuid().ToString(EGuidFormats::Digits)+TEXT(".tmp");
 if(!FFileHelper::SaveArrayToFile(Bytes,*Temp)){FM.Delete(*Temp);return Fail(Error,TEXT("Cache temporary write failed"));}
 if(!FM.Move(*Path,*Temp,false,false,false,true)){FM.Delete(*Temp);return Fail(Error,TEXT("Cache atomic publish failed"));}return true;
}
bool OKLoadGpuAnimationCache(const FString& Path,const FSHAHash& Identity,FOKGpuAnimationCacheData& Out,FString& Error,uint64 MaxBytes){
 MaxBytes=FMath::Min(MaxBytes,uint64(MAX_int32)-CacheHeaderBytes);
 TUniquePtr<FArchive> Reader(IFileManager::Get().CreateFileReader(*Path,FILEREAD_Silent));if(!Reader)return Fail(Error,TEXT("Cache file absent/unreadable"));
 int64 Size=Reader->TotalSize();if(Size<int64(CacheHeaderBytes)||uint64(Size)>MaxBytes+CacheHeaderBytes||Size>MAX_int32)return Fail(Error,TEXT("Cache file over budget/invalid size"));
 TArray<uint8> Bytes;Bytes.SetNumUninitialized(Size);Reader->Serialize(Bytes.GetData(),Size);if(Reader->IsError()||!Reader->Close())return Fail(Error,TEXT("Cache file read failed"));return OKDecodeGpuAnimationCache(Bytes,Identity,Out,Error,MaxBytes);
}
TFuture<FOKGpuCacheLoadResult> OKLoadGpuAnimationCacheAsync(FString Path,FSHAHash Identity,uint64 MaxBytes){
 return Async(EAsyncExecution::ThreadPool,[Path=MoveTemp(Path),Identity,MaxBytes]{FOKGpuCacheLoadResult Result;Result.WorkerThread=!IsInGameThread()&&!IsInRenderingThread();double Start=FPlatformTime::Seconds();auto Data=MakeUnique<FOKGpuAnimationCacheData>();if(OKLoadGpuAnimationCache(Path,Identity,*Data,Result.Error,MaxBytes))Result.Data=MoveTemp(Data);Result.LoadMs=(FPlatformTime::Seconds()-Start)*1000;return Result;});
}
