#include "OKGpuAnimationManifest.h"
#include "Async/Async.h"
#include "HAL/FileManager.h"
#include "RenderingThread.h"
#include "Misc/FileHelper.h"
#include "Json.h"
namespace {
bool ReadBounded(const FString& Path,uint64 Limit,TArray<uint8>& Bytes,FString& Error){
 TUniquePtr<FArchive> Reader(IFileManager::Get().CreateFileReader(*Path,FILEREAD_Silent));if(!Reader){Error=TEXT("GPU shard file absent/unreadable");return false;}
 const int64 Size=Reader->TotalSize();if(Size<=0||uint64(Size)>Limit||Size>MAX_int32){Error=TEXT("GPU shard file exceeds bounded size");return false;}
 Bytes.SetNumUninitialized(Size);Reader->Serialize(Bytes.GetData(),Size);if(Reader->IsError()||!Reader->Close()){Error=TEXT("GPU shard read failed");return false;}return true;
}
bool LoadShard(const FString& Cache,const FString& Manifest,const FSHAHash& Identity,uint64 Budget,FOKGpuAnimationBundle& Out,FString& Error){
 TArray<uint8> Bytes;if(!ReadBounded(Manifest,OKGpuAnimationManifestMaxBytes,Bytes,Error))return false;
 return OKDecodeGpuAnimationManifest(Bytes,Identity,Out.Manifest,Error)&&OKLoadGpuAnimationCache(Cache,Identity,Out.Data,Error,Budget)&&Out.Manifest.Validate(Out.Data,Error);
}
bool ParseIdentity(const FString& Text,FSHAHash& Identity){if(Text.Len()!=40)return false;for(TCHAR C:Text)if(!FChar::IsHexDigit(C))return false;Identity.FromString(Text);return true;}
}
TFuture<FOKGpuBundleLoadResult> OKLoadGpuAnimationBundleAsync(FString CachePath,FString ManifestPath,FSHAHash Identity,uint64 MaxBytes){
 return Async(EAsyncExecution::ThreadPool,[CachePath=MoveTemp(CachePath),ManifestPath=MoveTemp(ManifestPath),Identity,MaxBytes]{
  FOKGpuBundleLoadResult Result;Result.WorkerThread=!IsInGameThread()&&!IsInRenderingThread();const double Start=FPlatformTime::Seconds();
  auto Finish=[&]() -> FOKGpuBundleLoadResult {Result.LoadMs=(FPlatformTime::Seconds()-Start)*1000;return MoveTemp(Result);};
  auto Bundle=MakeUnique<FOKGpuAnimationBundle>();
  TUniquePtr<FArchive> Reader(IFileManager::Get().CreateFileReader(*ManifestPath,FILEREAD_Silent));
  if(!Reader){Result.Error=TEXT("Animation manifest absent/unreadable");return Finish();}
  const int64 Size=Reader->TotalSize();if(Size<52||uint64(Size)>OKGpuAnimationManifestMaxBytes){Result.Error=TEXT("Animation manifest size exceeds bounded format");return Finish();}
  TArray<uint8> Bytes;Bytes.SetNumUninitialized(Size);Reader->Serialize(Bytes.GetData(),Size);
  if(Reader->IsError()||!Reader->Close()){Result.Error=TEXT("Animation manifest read failed");return Finish();}
  if(!OKDecodeGpuAnimationManifest(Bytes,Identity,Bundle->Manifest,Result.Error)||!OKLoadGpuAnimationCache(CachePath,Identity,Bundle->Data,Result.Error,MaxBytes)||!Bundle->Manifest.Validate(Bundle->Data,Result.Error))return Finish();
  Result.Bundle=MoveTemp(Bundle);return Finish();
 });
}
TFuture<FOKGpuBundleLoadResult> OKLoadSelectedGpuAnimationShardsAsync(FString IndexPath,FSHAHash IndexIdentity,TArray<FString> Selected,uint64 MaxBytes){
 return Async(EAsyncExecution::ThreadPool,[IndexPath=MoveTemp(IndexPath),IndexIdentity,Selected=MoveTemp(Selected),MaxBytes]{
  FOKGpuBundleLoadResult Result;Result.FromIndex=true;Result.WorkerThread=!IsInGameThread()&&!IsInRenderingThread();const double Start=FPlatformTime::Seconds();
  auto Finish=[&]() -> FOKGpuBundleLoadResult {Result.LoadMs=(FPlatformTime::Seconds()-Start)*1000;return MoveTemp(Result);};
  if(Selected.IsEmpty()||Selected.Num()>100||MaxBytes==0||MaxBytes>4ull*1024*1024*1024){Result.Error=TEXT("GPU shard selection or byte budget invalid");return Finish();}
  TSet<FString> Wanted;for(const auto& Id:Selected){if(!Id.StartsWith(TEXT("/Game/"))||Wanted.Contains(Id)){Result.Error=TEXT("GPU shard selected identity invalid/duplicated");return Finish();}Wanted.Add(Id);}
  TArray<uint8> IndexBytes;if(!ReadBounded(IndexPath,4ull*1024*1024,IndexBytes,Result.Error))return Finish();
  const auto Actual=FSHA1::HashBuffer(IndexBytes.GetData(),IndexBytes.Num());if(FMemory::Memcmp(Actual.Hash,IndexIdentity.Hash,20)){Result.Error=TEXT("GPU shard index identity mismatch");return Finish();}
  FString Text;FFileHelper::BufferToString(Text,IndexBytes.GetData(),IndexBytes.Num());TSharedPtr<FJsonObject> Index;const TArray<TSharedPtr<FJsonValue>>* Rows=nullptr;double Schema=0;
  if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Index)||!Index||!Index->TryGetNumberField(TEXT("schema"),Schema)||Schema!=1||!Index->TryGetArrayField(TEXT("characters"),Rows)||Rows->IsEmpty()||Rows->Num()>100){Result.Error=TEXT("GPU shard index schema/count invalid");return Finish();}
  TMap<FString,TSharedPtr<FJsonObject>> ById;for(const auto& Value:*Rows){if(!Value||Value->Type!=EJson::Object){Result.Error=TEXT("GPU shard index row is not an object");return Finish();}auto Row=Value->AsObject();FString Id;if(!Row||!Row->TryGetStringField(TEXT("gpu_character"),Id)||!Id.StartsWith(TEXT("/Game/"))||ById.Contains(Id)){Result.Error=TEXT("GPU shard index identity missing/duplicated");return Finish();}ById.Add(Id,Row);}
  struct FInput {FString Id,Cache,Manifest;FSHAHash Identity;uint64 CacheBytes=0,ManifestBytes=0;};TArray<FInput> Inputs;uint64 Total=0;
  for(const auto& Id:Selected){const auto* Found=ById.Find(Id);if(!Found){++Result.MissingSelectedActors;continue;}FInput Input;Input.Id=Id;FString Identity;
   if(!(*Found)->TryGetStringField(TEXT("cache"),Input.Cache)||!(*Found)->TryGetStringField(TEXT("manifest"),Input.Manifest)||!(*Found)->TryGetStringField(TEXT("identity"),Identity)||!ParseIdentity(Identity,Input.Identity)){Result.Error=TEXT("GPU shard paths/identity invalid");return Finish();}
   const int64 CacheBytes=IFileManager::Get().FileSize(*Input.Cache),ManifestBytes=IFileManager::Get().FileSize(*Input.Manifest);
   if(CacheBytes<60||ManifestBytes<52||uint64(CacheBytes)>512ull*1024*1024||uint64(ManifestBytes)>OKGpuAnimationManifestMaxBytes||uint64(CacheBytes)+uint64(ManifestBytes)>MaxBytes-Total){Result.Error=TEXT("GPU selected shards missing or exceed aggregate budget");return Finish();}
   Input.CacheBytes=CacheBytes;Input.ManifestBytes=ManifestBytes;Total+=uint64(CacheBytes)+uint64(ManifestBytes);Inputs.Add(MoveTemp(Input));
  }
  if(Inputs.IsEmpty()){Result.Error=TEXT("GPU shard index contains no selected characters; retain CPU");return Finish();}
  TArray<TUniquePtr<FOKGpuAnimationBundle>> Owned;TArray<const FOKGpuAnimationCacheData*> Data;TArray<const FOKGpuAnimationManifest*> Manifests;
  for(const auto& Input:Inputs){auto Shard=MakeUnique<FOKGpuAnimationBundle>();if(uint64(IFileManager::Get().FileSize(*Input.Cache))!=Input.CacheBytes||uint64(IFileManager::Get().FileSize(*Input.Manifest))!=Input.ManifestBytes){Result.Error=TEXT("GPU shard files changed during load");return Finish();}if(!LoadShard(Input.Cache,Input.Manifest,Input.Identity,Input.CacheBytes,*Shard,Result.Error))return Finish();
   if(Shard->Manifest.Actors.Num()!=1||Shard->Manifest.Actors[0].CharacterId!=Input.Id){Result.Error=TEXT("GPU shard manifest does not match selected character");return Finish();}
   Data.Add(&Shard->Data);Manifests.Add(&Shard->Manifest);Owned.Add(MoveTemp(Shard));
  }
  auto Bundle=MakeUnique<FOKGpuAnimationBundle>();if(!OKComposeGpuAnimationCaches(Data,Manifests,Bundle->Data,Bundle->Manifest,Result.Error,MaxBytes))return Finish();
  Result.LoadedShards=Owned.Num();Result.Bundle=MoveTemp(Bundle);return Finish();
 });
}
