#include "OKGpuAnimationManifest.h"
#include "Async/Async.h"

TFuture<FOKGpuBundleLoadResult> OKPrepareRuntimeGpuAnimationBundleAsync(TUniquePtr<FOKGpuAnimationBundle> Bundle,TArray<FString> Selected,uint64 MaxBytes){
 return Async(EAsyncExecution::ThreadPool,[Bundle=MoveTemp(Bundle),Selected=MoveTemp(Selected),MaxBytes]() mutable {
  const double Start=FPlatformTime::Seconds();FOKGpuBundleLoadResult Result;Result.WorkerThread=!IsInGameThread();
  auto Reject=[&](const TCHAR* Why){Result.Error=Why;Result.LoadMs=(FPlatformTime::Seconds()-Start)*1000;return MoveTemp(Result);};
  if(!Bundle||Selected.IsEmpty())return Reject(TEXT("Runtime GPU preparation requires a bank and selected identities"));
  const auto& D=Bundle->Data;
  const uint64 Bytes=16ull*(uint64(D.Pose.Keys.Num())+D.Pose.Tracks.Num()+D.Pose.Actors.Num()+D.Pose.Tasks.Num()+D.Pose.InverseBindRows.Num()+D.Pose.Times.Num()+D.Palette.Items.Num()+D.Palette.InverseBindRows.Num());
  uint64 Total=Bytes;for(const auto& Clips:D.OwnedClips)Total+=4ull*Clips.Num();
  if(!MaxBytes||Total>FMath::Min(MaxBytes,uint64(MAX_int32)))return Reject(TEXT("Runtime GPU bank exceeds byte budget"));
  TSet<FString> Expected;
  for(const auto& Id:Selected){if(Id.IsEmpty()||Expected.Contains(Id))return Reject(TEXT("Runtime selection contains empty or duplicate identities"));Expected.Add(Id);}
  if(Expected.Num()!=Bundle->Manifest.Actors.Num())return Reject(TEXT("Runtime bank does not match selected actor count"));
  for(const auto& Actor:Bundle->Manifest.Actors)if(!Expected.Remove(Actor.CharacterId))return Reject(TEXT("Runtime bank contains stale or duplicate actor identity"));
  if(!D.Validate(Result.Error)||!Bundle->Manifest.Validate(D,Result.Error)){Result.LoadMs=(FPlatformTime::Seconds()-Start)*1000;return Result;}
  Result.Bundle=MoveTemp(Bundle);Result.LoadMs=(FPlatformTime::Seconds()-Start)*1000;return Result;
 });
}
