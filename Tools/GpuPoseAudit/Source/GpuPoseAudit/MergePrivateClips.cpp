#include "CoreMinimal.h"
#include "OKGpuAnimationManifest.h"
#include "Misc/FileHelper.h"
#include "Json.h"
#include "UObject/StrongObjectPtr.h"
bool MergePrivateClips(const FString& Input,const FString& Output){
 FString Text,Error;TSharedPtr<FJsonObject> Request;if(!FFileHelper::LoadFileToString(Text,*Input)||!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Request)||!Request)return false;
 const TArray<TSharedPtr<FJsonValue>>* Clips=nullptr;if(!Request->TryGetArrayField(TEXT("clips"),Clips)||Clips->Num()<2||Clips->Num()>100)return false;
 FOKGpuAnimationCacheData Data;FOKGpuAnimationManifest Manifest;int32 Count=0;TArray<TSharedPtr<FJsonValue>> Sources;
 for(const auto& Value:*Clips){auto Row=Value->AsObject();if(!Row)return false;const FString AuditPath=Row->GetStringField(TEXT("audit")),Cache=Row->GetStringField(TEXT("cache"));
  FString AuditText;TSharedPtr<FJsonObject> Audit;if(!FFileHelper::LoadFileToString(AuditText,*AuditPath)||!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(AuditText),Audit)||!Audit||!Audit->GetBoolField(TEXT("passed"))||!Audit->GetBoolField(TEXT("manifest_passed"))||!Audit->GetBoolField(TEXT("cache_roundtrip_passed")))return false;
  TArray<uint8> Bytes;if(!FFileHelper::LoadFileToArray(Bytes,*Cache)||Bytes.Num()<40)return false;FSHAHash Identity;FMemory::Memcpy(Identity.Hash,Bytes.GetData()+20,20);Bytes.Reset();
  auto Loaded=OKLoadGpuAnimationBundleAsync(Cache,Cache+TEXT(".manifest"),Identity).Consume();if(!Loaded.Bundle){UE_LOG(LogTemp,Error,TEXT("Clip bundle load: %s"),*Loaded.Error);return false;}auto& Other=Loaded.Bundle->Data;auto& OtherManifest=Loaded.Bundle->Manifest;
  if(OtherManifest.Actors.Num()!=1||OtherManifest.Actors[0].Clips.Num()!=1||Other.OwnedClips.Num()!=1||Other.OwnedClips[0].Num()!=1)return false;
  if(!Count){Data=MoveTemp(Other);Manifest=MoveTemp(OtherManifest);}
  else {
   if(Data.Pose.Tasks!=Other.Pose.Tasks||Data.Pose.InverseBindRows!=Other.Pose.InverseBindRows||Data.Palette.Items!=Other.Palette.Items||Data.Palette.InverseBindRows!=Other.Palette.InverseBindRows||Data.Palette.PoseCount!=Other.Palette.PoseCount)return false;
   auto Expected=Manifest,Actual=OtherManifest;Expected.CacheIdentity={};Actual.CacheIdentity={};Actual.Actors[0].Clips=Expected.Actors[0].Clips;
   TArray<uint8> A,B;if(!OKEncodeGpuAnimationManifest(Expected,A,Error)||!OKEncodeGpuAnimationManifest(Actual,B,Error)||A!=B)return false;
   auto Clip=OtherManifest.Actors[0].Clips[0];if(Manifest.Actors[0].Clips.ContainsByPredicate([&](const auto& C){return C.Id==Clip.Id;}))return false;
   const uint32 Tracks=Data.Pose.Tracks.Num(),Keys=Data.Pose.Keys.Num()/3;
   for(auto Track:Other.Pose.Tracks){Track.X+=Keys;Data.Pose.Tracks.Add(Track);}Data.Pose.Keys.Append(Other.Pose.Keys);Clip.TrackOffset+=Tracks;Manifest.Actors[0].Clips.Add(Clip);Data.OwnedClips[0].Add(Clip.TrackOffset);
  }
  Sources.Add(MakeShared<FJsonValueString>(AuditPath));++Count;
 }
 if(!Data.Validate(Error)||!Manifest.Validate(Data,Error)){UE_LOG(LogTemp,Error,TEXT("Private clip composition rejected: %s"),*Error);return false;}
 TArray<uint8> Encoded;FSHAHash Zero;if(!OKEncodeGpuAnimationCache(Data,Zero,Encoded,Error))return false;const auto Identity=FSHA1::HashBuffer(Encoded.GetData(),Encoded.Num());Manifest.CacheIdentity=Identity;
 const FString Cache=Output+TEXT(".gpuanim");if(IFileManager::Get().FileExists(*Cache)||!OKSaveGpuAnimationCache(Cache,Data,Identity,Error)||!OKEncodeGpuAnimationManifest(Manifest,Encoded,Error)||!FFileHelper::SaveArrayToFile(Encoded,*(Cache+TEXT(".manifest"))))return false;
 auto Reload=OKLoadGpuAnimationBundleAsync(Cache,Cache+TEXT(".manifest"),Identity).Consume();if(!Reload.Bundle||!Reload.Bundle->Manifest.Validate(Reload.Bundle->Data,Error))return false;
 auto Report=MakeShared<FJsonObject>();Report->SetBoolField(TEXT("passed"),true);Report->SetBoolField(TEXT("private_actor_identity_preserved"),true);Report->SetBoolField(TEXT("all_input_clips_gpu_audited"),true);Report->SetNumberField(TEXT("clips"),Count);Report->SetStringField(TEXT("character"),Manifest.Actors[0].CharacterId);Report->SetStringField(TEXT("identity"),Identity.ToString());Report->SetStringField(TEXT("cache"),Cache);Report->SetArrayField(TEXT("source_audits"),Sources);FString Json;
 return FJsonSerializer::Serialize(Report,TJsonWriterFactory<>::Create(&Json))&&FFileHelper::SaveStringToFile(Json,*Output);
}
