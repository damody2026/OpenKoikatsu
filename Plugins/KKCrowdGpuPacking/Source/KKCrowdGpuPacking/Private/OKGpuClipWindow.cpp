#include "OKGpuAnimationManifest.h"
#include "Async/Async.h"
#include "Containers/StringConv.h"

bool OKSelectGpuClipWindow(const FOKGpuAnimationBundle& S,TConstArrayView<FOKGpuClipWindow> Selection,FOKGpuAnimationBundle& Out,FString& Error,uint64 MaxBytes){
 auto Fail=[&](const TCHAR* Why){Error=Why;return false;};
 if(Selection.Num()!=S.Manifest.Actors.Num()||Selection.IsEmpty())return Fail(TEXT("Clip window requires exact selected character identities"));
 if(!S.Manifest.Validate(S.Data,Error))return false;
 TMap<FString,const FOKGpuClipWindow*> Windows;
 for(const auto& W:Selection){if(W.CharacterId.IsEmpty()||Windows.Contains(W.CharacterId)||W.Clips.IsEmpty()||W.Clips.Num()>3)return Fail(TEXT("Clip window identity/count invalid"));Windows.Add(W.CharacterId,&W);}
 const auto& P=S.Data.Pose;
 uint64 Bytes=16ull*(uint64(P.Actors.Num())+P.Times.Num()+P.Tasks.Num()+P.InverseBindRows.Num()+S.Data.Palette.Items.Num()+S.Data.Palette.InverseBindRows.Num());
 uint64 KeyVectors=0,TrackCount=0;TArray<TArray<int32>> Chosen;
 for(const auto& A:S.Manifest.Actors){const auto* Found=Windows.Find(A.CharacterId);if(!Found)return Fail(TEXT("Clip window contains a stale character"));const auto& W=**Found;TSet<FString> Seen;auto& Indices=Chosen.AddDefaulted_GetRef();
  for(const auto& Id:W.Clips){if(Seen.Contains(Id))return Fail(TEXT("Clip window duplicates a private clip"));Seen.Add(Id);const int32 C=A.Clips.IndexOfByPredicate([&](const FOKGpuClipRecord& Clip){return Clip.Id==Id;});if(C==INDEX_NONE)return Fail(TEXT("Requested private clip has not been prepared"));Indices.Add(C);Bytes+=4;
   for(uint32 B=0;B<A.PoseCount;++B){const auto& T=P.Tracks[A.Clips[C].TrackOffset+B];Bytes+=16+uint64(T.Y)*48;KeyVectors+=uint64(T.Y)*3;++TrackCount;}
  }
 }
 if(!MaxBytes||Bytes>MaxBytes)return Fail(TEXT("Clip window exceeds preallocation byte budget"));
 FOKGpuAnimationBundle Next;auto& D=Next.Data;D.Pose.Keys.Reserve(int32(KeyVectors));D.Pose.Tracks.Reserve(int32(TrackCount));
 D.Palette=S.Data.Palette;D.Pose.Tasks=P.Tasks;D.Pose.InverseBindRows=P.InverseBindRows;D.Pose.Actors=P.Actors;D.Pose.Times.Init(FVector4f(0,0,0,0),P.Actors.Num());
 FSHA1 Identity;
 auto HashString=[&](const FString& Value){FTCHARToUTF8 Text(*Value);uint32 Size=Text.Length();Identity.Update(reinterpret_cast<const uint8*>(&Size),4);Identity.Update(reinterpret_cast<const uint8*>(Text.Get()),Size);};
 for(int32 I=0;I<S.Manifest.Actors.Num();++I){auto A=S.Manifest.Actors[I];HashString(A.CharacterId);const uint32 Count=Chosen[I].Num();Identity.Update(reinterpret_cast<const uint8*>(&Count),4);A.Clips.Reset();auto& Offsets=D.OwnedClips.AddDefaulted_GetRef();
  for(int32 Index:Chosen[I]){const auto& Clip=S.Manifest.Actors[I].Clips[Index];const uint32 Base=D.Pose.Tracks.Num();Offsets.Add(Base);A.Clips.Add({Clip.Id,Base,Clip.Duration});HashString(Clip.Id);Identity.Update(reinterpret_cast<const uint8*>(&Clip.Duration),4);
   for(uint32 B=0;B<A.PoseCount;++B){auto T=P.Tracks[Clip.TrackOffset+B];OKCopyGpuTrackKeys(D.Pose.Keys,P.Keys,T);D.Pose.Tracks.Add(T);}
  }
  D.Pose.Actors[I].X=D.Pose.Actors[I].Y=Offsets[0];Next.Manifest.Actors.Add(MoveTemp(A));
 }
 for(const auto& A:Next.Manifest.Actors){for(const auto& Bone:A.PoseBones){HashString(Bone.Name);Identity.Update(reinterpret_cast<const uint8*>(&Bone.Parent),4);Identity.Update(Bone.ReferenceIdentity.Hash,20);}for(const auto& Mesh:A.Meshes){HashString(Mesh.PartId);HashString(Mesh.AssetPath);Identity.Update(reinterpret_cast<const uint8*>(&Mesh.Bones),sizeof(Mesh.Bones));for(const auto& Section:Mesh.Sections)Identity.Update(reinterpret_cast<const uint8*>(&Section),sizeof(Section));}}
 auto HashArray=[&](const auto& Values){const uint64 Bytes=uint64(Values.Num())*Values.GetTypeSize();Identity.Update(reinterpret_cast<const uint8*>(&Bytes),8);if(Bytes)Identity.Update(reinterpret_cast<const uint8*>(Values.GetData()),uint32(Bytes));};
 HashArray(D.Pose.Keys);HashArray(D.Pose.Tracks);HashArray(D.Pose.Actors);HashArray(D.Pose.Tasks);HashArray(D.Pose.InverseBindRows);HashArray(D.Pose.Times);HashArray(D.Palette.Items);HashArray(D.Palette.InverseBindRows);
 Identity.Final();Identity.GetHash(Next.Manifest.CacheIdentity.Hash);
 if(!Next.Manifest.Validate(D,Error))return false;
 Out=MoveTemp(Next);Error.Reset();return true;
}
TFuture<FOKGpuBundleLoadResult> OKSelectGpuClipWindowAsync(TSharedRef<const FOKGpuAnimationBundle,ESPMode::ThreadSafe> Source,TArray<FOKGpuClipWindow> Selection,uint64 MaxBytes){
 return Async(EAsyncExecution::ThreadPool,[Source,Selection=MoveTemp(Selection),MaxBytes]()mutable{
  const double Start=FPlatformTime::Seconds();FOKGpuBundleLoadResult R;R.WorkerThread=!IsInGameThread();auto Next=MakeUnique<FOKGpuAnimationBundle>();
  if(OKSelectGpuClipWindow(*Source,Selection,*Next,R.Error,MaxBytes))R.Bundle=MoveTemp(Next);
  R.LoadMs=(FPlatformTime::Seconds()-Start)*1000;R.Window.SelectionMs=R.LoadMs;R.Window.SourceKeyBytes=uint64(Source->Data.Pose.Keys.Num())*16;R.Window.SelectedKeyBytes=R.Bundle?uint64(R.Bundle->Data.Pose.Keys.Num())*16:0;return R;
 });
}

bool OKRemapGpuClipWindowPlayback(const FOKGpuAnimationManifest& Source,const FOKGpuAnimationManifest& Window,TConstArrayView<FOKGpuPosePlayback> Playback,TArray<FOKGpuPosePlayback>& Out,FString& Error){
 auto Fail=[&](const TCHAR* Why){Error=Why;return false;};
 if(Playback.Num()!=Source.Actors.Num()||Window.Actors.Num()!=Source.Actors.Num()||Source.Actors.IsEmpty())return Fail(TEXT("Clip window playback actor count mismatch"));
 TMap<FString,int32> SourceIds;for(int32 I=0;I<Source.Actors.Num();++I){if(SourceIds.Contains(Source.Actors[I].CharacterId))return Fail(TEXT("Source private actor identity duplicated"));SourceIds.Add(Source.Actors[I].CharacterId,I);}
 TSet<FString> Seen;TArray<FOKGpuPosePlayback> Next;Next.Reserve(Window.Actors.Num());
 for(const auto& Target:Window.Actors){const int32* Found=SourceIds.Find(Target.CharacterId);if(!Found||Seen.Contains(Target.CharacterId))return Fail(TEXT("Window playback contains stale private identity"));Seen.Add(Target.CharacterId);const auto& Original=Source.Actors[*Found];auto P=Playback[*Found];
  if(Original.PoseCount!=Target.PoseCount||Original.PoseBones.Num()!=Target.PoseBones.Num())return Fail(TEXT("Window playback private rig differs"));
  for(int32 B=0;B<Original.PoseBones.Num();++B){const auto& A=Original.PoseBones[B];const auto& Z=Target.PoseBones[B];if(A.Name!=Z.Name||A.Parent!=Z.Parent||FMemory::Memcmp(A.ReferenceIdentity.Hash,Z.ReferenceIdentity.Hash,20))return Fail(TEXT("Window playback private reference differs"));}
  if(P.ClipA>=uint32(Original.Clips.Num())||P.ClipB>=uint32(Original.Clips.Num())||P.Times.Z<0||P.Times.Z>1)return Fail(TEXT("Window playback clip/blend invalid"));for(int32 C=0;C<4;++C)if(!FMath::IsFinite(P.Times[C]))return Fail(TEXT("Window playback time nonfinite"));
  auto Slot=[&](uint32 Index){const auto& Clip=Original.Clips[Index];return Target.Clips.IndexOfByPredicate([&](const FOKGpuClipRecord& C){return C.Id==Clip.Id&&C.Duration==Clip.Duration;});};
  int32 A=Slot(P.ClipA),B=Slot(P.ClipB);
  if((P.Times.Z<1&&A==INDEX_NONE)||(P.Times.Z>0&&B==INDEX_NONE))return Fail(TEXT("Active private clip absent from resident window"));
  if(A==INDEX_NONE)A=B;if(B==INDEX_NONE)B=A;P.ClipA=A;P.ClipB=B;Next.Add(P);
 }
 Out=MoveTemp(Next);Error.Reset();return true;
}
