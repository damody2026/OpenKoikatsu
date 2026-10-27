#include "KKRuntimeGpuDiskCache.h"
#include "OKGpuAnimationManifest.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "HAL/CriticalSection.h"
#include "Misc/ScopeLock.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/FileHelper.h"
#include "HAL/IConsoleManager.h"

namespace OKRuntimeGpuDiskPrivate {
TAutoConsoleVariable<int32> CacheMiB(TEXT("kk.RuntimeGpuCache.MiB"),4096,TEXT("Persistent runtime GPU pose cache limit in MiB; 0 disables disk caching."));
FCriticalSection CacheMutex;
TAtomic<uint64> LockWaitUs{0},ReadUs{0},DecodeUs{0};
TAtomic<uint64> Hits{0},Misses{0},Invalid{0},Stored{0},Evicted{0},Failures{0},DiskBytes{0},ReadBytes{0},WrittenBytes{0};
FString CacheRoot(){
 FString Root;FParse::Value(FCommandLine::Get(),TEXT("RuntimeGpuCacheDir="),Root);
 if(Root.IsEmpty())Root=FPaths::Combine(FPlatformProcess::UserSettingsDir(),TEXT("OpenKoikatsu/RuntimeGpuCache/v1"));
 return FPaths::ConvertRelativePathToFull(Root);
}
uint64 Capacity(){int32 MiB=CacheMiB.GetValueOnAnyThread();FParse::Value(FCommandLine::Get(),TEXT("RuntimeGpuCacheMiB="),MiB);return uint64(FMath::Clamp(MiB,0,65536))*1024*1024;}
FSHAHash Identity(const FKKRuntimePoseRequest& R){
 // Recipe covers the exact APNG snapshot, curves, private bone references,
 // attachment palettes, sampling and duration. Native/code build stamps prevent
 // old cache reuse after a deformation or packed-format implementation change.
 static const FString SharedIdentity=[](){
  const FString Root=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_BUILTIN_RESOURCES"));
  // OK cards take their shared resources from the OK base library: its conversion record (one line per converted
  // source, ~7 MB) versions the cache like the packs' catalogs do.
  FString Library=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_BASE_LIBRARY"));if(Library.IsEmpty())Library=FPaths::ProjectContentDir()/TEXT("IndependentResources/base_v1");
  FSHA1 Hash;for(const FString& Path:{Root/TEXT("catalog.json"),Root/TEXT("renderer_defaults.json"),Library/TEXT("oklib.json"),Library/TEXT("converted.tsv")}){
   TArray<uint8> Bytes;const int64 Size=IFileManager::Get().FileSize(*Path);if(Size>0&&Size<=64ll*1024*1024&&FFileHelper::LoadFileToArray(Bytes,*Path))Hash.Update(Bytes.GetData(),Bytes.Num());const uint64 Count=Bytes.Num();Hash.Update(reinterpret_cast<const uint8*>(&Count),sizeof(Count));
  }Hash.Final();FSHAHash Result;Hash.GetHash(Result.Hash);return Result.ToString();
 }();
 check(!R.FirstFrameDigest.IsEmpty()); // set by the pose job before it reads or writes the cache
 const FString Key=OKRuntimePoseRecipeIdentity(R)+R.FirstFrameDigest+SharedIdentity+TEXT(":gpu-pose-disk-v1:")+TEXT(OK_RUNTIME_NATIVE_CACHE_BUILD)+TEXT(":")+TEXT(OK_RUNTIME_POSE_CACHE_CODE);
 FTCHARToUTF8 Bytes(*Key);return FSHA1::HashBuffer(Bytes.Get(),Bytes.Length());
}
bool OwnedName(const FString& Name){
 if(Name.Len()!=51||!Name.StartsWith(TEXT("pose-"))||!Name.EndsWith(TEXT(".okgpc")))return false;
 for(int32 I=5;I<45;++I)if(!FChar::IsHexDigit(Name[I]))return false;return true;
}
struct FFile{FString Path;uint64 Bytes=0;FDateTime Access;};
TArray<FFile> Inventory(const FString& Root,uint64& Total){
 TArray<FString> Stale;IFileManager::Get().FindFiles(Stale,*(Root/TEXT("pose-*.okgpc.*.tmp")),true,false);
 for(const auto& Name:Stale)if(Name.Len()==88&&OwnedName(Name.Left(51))&&Name[51]=='.'&&Name.Mid(84)==TEXT(".tmp")){
  bool Guid=true;for(int32 I=52;I<84;++I)Guid&=FChar::IsHexDigit(Name[I]);if(Guid)IFileManager::Get().Delete(*(Root/Name),false,false,true);
 }
 TArray<FString> Names;IFileManager::Get().FindFiles(Names,*(Root/TEXT("pose-*.okgpc")),true,false);TArray<FFile> Files;Total=0;
 for(const auto& Name:Names)if(OwnedName(Name)){const FString Path=Root/Name;const auto Stat=IFileManager::Get().GetStatData(*Path);if(Stat.bIsValid&&!Stat.bIsDirectory&&Stat.FileSize>=0){Files.Add({Path,uint64(Stat.FileSize),Stat.ModificationTime});Total+=Stat.FileSize;}}
 Files.Sort([](const FFile& A,const FFile& B){return A.Access==B.Access?A.Path<B.Path:A.Access<B.Access;});return Files;
}
bool Trim(const FString& Root,uint64 Limit,uint64 Incoming,const FString& Keep){
 uint64 Total=0;const auto Files=Inventory(Root,Total);
 for(const auto& File:Files){if(Total<=Limit&&Incoming<=Limit-Total)break;if(File.Path==Keep)continue;
  if(IFileManager::Get().Delete(*File.Path,false,false,true)){Total-=File.Bytes;++Evicted;}else ++Failures;
 }
 DiskBytes.Store(Total);return Total<=Limit&&Incoming<=Limit-Total;
}
FString LockName(const FString& Root){FTCHARToUTF8 Bytes(*Root);return TEXT("OpenKoikatsuRuntimeGpuCache_")+FSHA1::HashBuffer(Bytes.Get(),Bytes.Length()).ToString();}
FString CachePath(const FString& Root,const FSHAHash& Id){return Root/(TEXT("pose-")+Id.ToString()+TEXT(".okgpc"));}
void Warning(const FString& Error){++Failures;UE_LOG(LogTemp,Warning,TEXT("Runtime GPU disk cache retained live computation: %s"),*Error);}
}

FKKRuntimeGpuDiskCacheStats OKRuntimeGpuDiskCacheStats(){
 using namespace OKRuntimeGpuDiskPrivate;
 return {Hits.Load(),Misses.Load(),Invalid.Load(),Stored.Load(),Evicted.Load(),Failures.Load(),DiskBytes.Load(),ReadBytes.Load(),WrittenBytes.Load(),Capacity(),CacheRoot(),LockWaitUs.Load(),ReadUs.Load(),DecodeUs.Load()};
}
bool OKLoadRuntimeGpuPoseCache(const FKKRuntimePoseRequest& R,FKKRuntimePoseResult& Out){
 using namespace OKRuntimeGpuDiskPrivate;check(!IsInGameThread());const uint64 Limit=Capacity();if(!Limit){++Misses;return false;}
 const FSHAHash Id=Identity(R);const FString Root=CacheRoot(),Path=CachePath(Root,Id);
 const double WaitStart=FPlatformTime::Seconds();auto Local=MakeUnique<FScopeLock>(&CacheMutex);
 auto Shared=MakeUnique<FSystemWideCriticalSection>(LockName(Root),FTimespan::FromSeconds(2));
 LockWaitUs.AddExchange(uint64((FPlatformTime::Seconds()-WaitStart)*1000000));
 if(!Shared->IsValid()){++Misses;++Failures;return false;}
 const double ReadStart=FPlatformTime::Seconds();
 Trim(Root,Limit,0,TEXT(""));const int64 Size=IFileManager::Get().FileSize(*Path);if(Size<0){++Misses;return false;}
 const FDateTime Stamp=IFileManager::Get().GetTimeStamp(*Path);
 const uint64 MaxBytes=FMath::Min(R.MaxKeyBytes+16ull*R.Bones.Num()*8+256,uint64(MAX_int32)-60);
 TArray<uint8> Bytes;bool Read=false;
 if(uint64(Size)<=Limit&&Size>=60&&uint64(Size)<=MaxBytes+60){
  TUniquePtr<FArchive> Reader(IFileManager::Get().CreateFileReader(*Path));
  if(Reader&&Reader->TotalSize()==Size){Bytes.SetNumUninitialized(Size);Reader->Serialize(Bytes.GetData(),Size);Read=!Reader->IsError()&&Reader->Close();}
 }
 // The owned byte snapshot survives eviction. Decode and integrity checks use
 // private arrays only; filesystem mutation and LRU remain cross-process locked.
 if(Read)IFileManager::Get().SetTimeStamp(*Path,FDateTime::UtcNow());
 const FDateTime ReadStamp=IFileManager::Get().GetTimeStamp(*Path);
 ReadUs.AddExchange(uint64((FPlatformTime::Seconds()-ReadStart)*1000000));
 const bool SerialDecode=FParse::Param(FCommandLine::Get(),TEXT("RuntimeGpuCacheSerialDecode"));
 if(!SerialDecode){Shared.Reset();Local.Reset();}
 const double DecodeStart=FPlatformTime::Seconds();FOKGpuAnimationCacheData Data;FString Error;
 bool Valid=Read&&OKDecodeGpuAnimationCache(Bytes,Id,Data,Error,MaxBytes);
 Valid=Valid&&Data.Pose.Actors.Num()==1&&Data.OwnedClips.Num()==1&&Data.OwnedClips[0].Num()==1&&Data.OwnedClips[0][0]==0&&Data.Pose.Tracks.Num()==R.Bones.Num();
 if(Valid)for(int32 B=0;B<R.Bones.Num();++B){const auto& T=Data.Pose.Tracks[B];if(T.Z!=(R.Bones[B].Parent<0?MAX_uint32:uint32(R.Bones[B].Parent))||Data.Pose.Keys[OKGpuTrackPositionRow(T,0)].W<0||Data.Pose.Keys[OKGpuTrackPositionRow(T,T.Y-1)].W>R.Duration+1e-4f){Valid=false;break;}}
 DecodeUs.AddExchange(uint64((FPlatformTime::Seconds()-DecodeStart)*1000000));
 Bytes.Empty();
 if(!Valid){
  ++Invalid;++Misses;UE_LOG(LogTemp,Warning,TEXT("Runtime GPU cache invalid; rebuilding %s: %s"),*Path,*Error);
  if(!Local){Local=MakeUnique<FScopeLock>(&CacheMutex);Shared=MakeUnique<FSystemWideCriticalSection>(LockName(Root),FTimespan::FromSeconds(2));}
  // Another reader/writer may have touched or replaced it during our decode.
  // Never delete that newer generation based on this stale invalid snapshot.
  if(Shared->IsValid()&&IFileManager::Get().FileSize(*Path)==Size&&IFileManager::Get().GetTimeStamp(*Path)==(Read?ReadStamp:Stamp)){
   IFileManager::Get().Delete(*Path,false,false,true);Trim(Root,Limit,0,TEXT(""));
  }
  return false;
 }
 Out.Keys=MoveTemp(Data.Pose.Keys);Out.Tracks=MoveTemp(Data.Pose.Tracks);Out.Samples=OKRuntimePoseFrames(R.Duration,R.SampleHz)+1;Out.ConstantTracks=0;for(const auto& T:Out.Tracks)Out.ConstantTracks+=T.Y==1;
 Out.PersistentCacheHit=true;Out.PersistentCacheBytes=Size;++Hits;ReadBytes.AddExchange(Size);
 UE_LOG(LogTemp,Display,TEXT("Runtime GPU cache hit clip=%s bytes=%lld"),*R.ClipIdentity,Size);return true;
}
bool OKPackAndSaveRuntimeGpuPoseCache(const FKKRuntimePoseRequest& R,FKKRuntimePoseResult& Out,FString& Error){
 using namespace OKRuntimeGpuDiskPrivate;check(!IsInGameThread());FOKGpuAnimationCacheData D;D.Pose.Keys=MoveTemp(Out.Keys);D.Pose.Tracks=MoveTemp(Out.Tracks);const uint32 Bones=R.Bones.Num();
 D.Pose.Actors.Add(FUintVector4(0,0,Bones,1));D.Pose.Times.Add(FVector4f(0,0,0,0));D.OwnedClips.Add({0});D.Palette.PoseCount=Bones;
 for(uint32 B=0;B<Bones;++B){const uint32 Bind=D.Pose.InverseBindRows.Num();D.Pose.InverseBindRows.Append({FVector4f(1,0,0,0),FVector4f(0,1,0,0),FVector4f(0,0,1,0)});D.Pose.Tasks.Add(FUintVector4(B,0,Bind,0));D.Palette.Items.Add(FUintVector4(B,Bind,B,0));}
 D.Palette.InverseBindRows=D.Pose.InverseBindRows;
 if(!OKPackConstantGpuPoseChannels(D.Pose,Error)){Out.Keys=MoveTemp(D.Pose.Keys);Out.Tracks=MoveTemp(D.Pose.Tracks);return false;}
 const uint64 Limit=Capacity();if(Limit){
  TArray<uint8> Bytes;FString CacheError;
  if(!OKEncodeGpuAnimationCache(D,Identity(R),Bytes,CacheError,R.MaxKeyBytes+16ull*Bones*8+256))Warning(CacheError);
  else{Out.PersistentCacheBytes=Bytes.Num();const FString Root=CacheRoot(),Path=CachePath(Root,Identity(R));FScopeLock Local(&CacheMutex);
   FSystemWideCriticalSection Shared(LockName(Root),FTimespan::FromSeconds(2));
   if(!Shared.IsValid())Warning(TEXT("Cross-process cache lock busy"));
   else if(uint64(Bytes.Num())>Limit)Warning(TEXT("Single GPU pose cache exceeds disk capacity; skipped"));
   else if(!IFileManager::Get().MakeDirectory(*Root,true))Warning(TEXT("Cache directory creation failed"));
   else if(IFileManager::Get().FileExists(*Path)){IFileManager::Get().SetTimeStamp(*Path,FDateTime::UtcNow());Trim(Root,Limit,0,Path);}
   else if(Trim(Root,Limit,Bytes.Num(),TEXT(""))){
    const FString Temp=Path+TEXT(".")+FGuid::NewGuid().ToString(EGuidFormats::Digits)+TEXT(".tmp");
    if(FFileHelper::SaveArrayToFile(Bytes,*Temp)&&IFileManager::Get().Move(*Path,*Temp,false,false,false,true)){++Stored;WrittenBytes.AddExchange(Bytes.Num());Trim(Root,Limit,0,Path);}
    else{IFileManager::Get().Delete(*Temp,false,false,true);Warning(TEXT("Atomic GPU cache publish failed"));}
   }else Warning(TEXT("Old GPU cache entries could not be evicted; skipped write"));
  }
 }
 Out.Keys=MoveTemp(D.Pose.Keys);Out.Tracks=MoveTemp(D.Pose.Tracks);Error.Reset();return true;
}
