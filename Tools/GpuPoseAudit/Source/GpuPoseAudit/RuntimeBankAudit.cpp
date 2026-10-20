#include "OKGpuAnimationManifest.h"
#include "Misc/FileHelper.h"
#include "Json.h"

// File input supplies an already audited fixture only. Every test below calls
// the production in-memory runtime entry; no file is required by that entry.
bool RunRuntimeBankAudit(const FString& Cache,const FString& Output){
 TArray<uint8> Bytes;FString Error;
 if(!FFileHelper::LoadFileToArray(Bytes,*Cache)||Bytes.Num()<40)return false;
 FSHAHash Identity;FMemory::Memcpy(Identity.Hash,Bytes.GetData()+20,20);Bytes.Reset();
 auto Loaded=OKLoadGpuAnimationBundleAsync(Cache,Cache+TEXT(".manifest"),Identity).Consume();
 if(!Loaded.Bundle)return false;
 TArray<FString> Selected;for(const auto& Actor:Loaded.Bundle->Manifest.Actors)Selected.Add(Actor.CharacterId);
 auto Copy=[&](){auto B=MakeUnique<FOKGpuAnimationBundle>();*B=*Loaded.Bundle;return B;};
 FOKRuntimeGpuClip Clip;Clip.Id=Loaded.Bundle->Manifest.Actors[0].Clips[0].Id;Clip.Duration=Loaded.Bundle->Manifest.Actors[0].Clips[0].Duration;Clip.Keys=Loaded.Bundle->Data.Pose.Keys;Clip.Tracks=Loaded.Bundle->Data.Pose.Tracks;
 auto Second=Clip;Second.Id+=TEXT("/second-private-clip");FOKGpuAnimationBundle Built;
 const bool Assembled=OKBuildRuntimeGpuActorBank(Loaded.Bundle->Manifest.Actors[0],{Clip,Second},Loaded.Bundle->Data.Palette,Built,Error);
 const bool Rebased=Assembled&&Built.Data.OwnedClips.Num()==1&&Built.Data.OwnedClips[0].Num()==2&&Built.Data.OwnedClips[0][1]==uint32(Clip.Tracks.Num())&&Built.Data.Pose.Tracks[Clip.Tracks.Num()].X==uint32(Clip.Keys.Num()/3)+Clip.Tracks[0].X;
 auto BadClip=Clip;BadClip.Tracks[0].Z=0;auto Sentinel=Copy();const int32 SentinelKeys=Sentinel->Data.Pose.Keys.Num();
 const bool BuildRejected=!OKBuildRuntimeGpuActorBank(Loaded.Bundle->Manifest.Actors[0],{BadClip},Loaded.Bundle->Data.Palette,*Sentinel,Error)&&Sentinel->Data.Pose.Keys.Num()==SentinelKeys;
 auto Good=OKPrepareRuntimeGpuAnimationBundleAsync(Copy(),Selected).Consume();
 auto StaleIds=Selected;StaleIds[0]+=TEXT("/stale");
 auto Stale=OKPrepareRuntimeGpuAnimationBundleAsync(Copy(),StaleIds).Consume();
 auto DuplicateIds=Selected;DuplicateIds.Add(Selected[0]);
 auto Duplicate=OKPrepareRuntimeGpuAnimationBundleAsync(Copy(),DuplicateIds).Consume();
 auto Budget=OKPrepareRuntimeGpuAnimationBundleAsync(Copy(),Selected,1).Consume();
 auto Malformed=Copy();Malformed->Data.Pose.Tracks[0].X=MAX_uint32;
 auto Invalid=OKPrepareRuntimeGpuAnimationBundleAsync(MoveTemp(Malformed),Selected).Consume();
 const bool Passed=Assembled&&Rebased&&BuildRejected&&Good.Bundle&&Good.WorkerThread&&!Good.FromIndex&&Good.Error.IsEmpty()&&!Stale.Bundle&&!Stale.Error.IsEmpty()&&!Duplicate.Bundle&&!Duplicate.Error.IsEmpty()&&!Budget.Bundle&&!Budget.Error.IsEmpty()&&!Invalid.Bundle&&!Invalid.Error.IsEmpty();
 auto Report=MakeShared<FJsonObject>();Report->SetBoolField(TEXT("passed"),Passed);
 Report->SetBoolField(TEXT("worker_validation"),Good.WorkerThread);Report->SetBoolField(TEXT("in_memory_bank_adopted"),Good.Bundle.IsValid());
 Report->SetBoolField(TEXT("private_multi_clip_assembly"),Assembled);Report->SetBoolField(TEXT("private_clip_offsets_rebased"),Rebased);Report->SetBoolField(TEXT("bad_hierarchy_preserves_previous_bank"),BuildRejected);
 Report->SetBoolField(TEXT("stale_selection_rejected"),!Stale.Bundle);Report->SetBoolField(TEXT("duplicate_selection_rejected"),!Duplicate.Bundle);
 Report->SetBoolField(TEXT("byte_budget_rejected"),!Budget.Bundle);Report->SetBoolField(TEXT("malformed_track_rejected"),!Invalid.Bundle);
 Report->SetNumberField(TEXT("prepare_ms"),Good.LoadMs);Report->SetNumberField(TEXT("actors"),Selected.Num());
 Report->SetBoolField(TEXT("apng_end_to_end_verified"),false);
 FString Json;return FJsonSerializer::Serialize(Report,TJsonWriterFactory<>::Create(&Json))&&FFileHelper::SaveStringToFile(Json,*Output)&&Passed;
}
