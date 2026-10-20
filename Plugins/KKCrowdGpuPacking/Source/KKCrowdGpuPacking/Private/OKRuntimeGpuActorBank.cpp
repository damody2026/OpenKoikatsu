#include "OKGpuAnimationManifest.h"

bool OKPackConstantGpuPoseChannels(FOKGpuPoseBatch& D,FString& Error){
 if(!D.Validate(Error))return false;
 TArray<FVector4f> Keys;Keys.Reserve(D.Keys.Num());TArray<FUintVector4> Tracks=D.Tracks;
 TMap<FString,TArray<FUintVector4>> Payloads;
 auto ReusePayload=[&](FUintVector4& T,int32 Start){const uint64 First=OKGpuTrackFirstRow(T),Rows=OKGpuTrackEndRow(T)-First;const auto Hash=FSHA1::HashBuffer(Keys.GetData()+First,Rows*sizeof(FVector4f));const FString Id=FString::Printf(TEXT("%u:%u:%s"),T.W,T.Y,*Hash.ToString());auto& Candidates=Payloads.FindOrAdd(Id);for(const auto& Old:Candidates){if(!FMemory::Memcmp(Keys.GetData()+OKGpuTrackFirstRow(Old),Keys.GetData()+First,Rows*sizeof(FVector4f))){T.X=Old.X;Keys.SetNum(Start,EAllowShrinking::No);return;}}Candidates.Add(T);};
 for(auto& T:Tracks){
  const int32 Start=Keys.Num();
  if(OKGpuTrackColumn(T)&&!OKGpuTrackVectorKeys(T)){
   const auto Old=T;TArray<FVector4f> Columns;Columns.Reserve(T.Y);bool Constant=true;
   for(uint32 K=0;K<T.Y;++K){Columns.Add(FVector4f(OKGpuTrackColumnKey(D.Keys,Old,K),0));if(K&&Columns[K]!=Columns[0])Constant=false;}
   T.X=Keys.Num();T.W=(T.W&~16u)|256u;if(Constant){T.W|=16u;Keys.Add(Columns[0]);}
   for(uint32 K=0;K<T.Y;++K){Keys.Add(D.Keys[OKGpuTrackPositionRow(Old,K)]);if(!Constant)Keys.Add(Columns[K]);}
   while(Keys.Num()%3)Keys.Add(FVector4f(0,0,0,0));ReusePayload(T,Start);continue;
  }
  if(OKGpuTrackRowAddressed(T)){OKCopyGpuTrackKeys(Keys,D.Keys,T);ReusePayload(T,Start);continue;}
  const uint32 First=T.X*3;bool Constant=T.Y>2;
  for(uint32 K=1;Constant&&K<T.Y;++K)Constant=!FMemory::Memcmp(&D.Keys[First+1],&D.Keys[(T.X+K)*3+1],sizeof(FVector4f)*2);
  if(!Constant){OKCopyGpuTrackKeys(Keys,D.Keys,T);ReusePayload(T,Start);continue;}
  const uint32 Old=T.X;T.X=Keys.Num();T.W|=16;Keys.Add(D.Keys[First+1]);Keys.Add(D.Keys[First+2]);for(uint32 K=0;K<T.Y;++K)Keys.Add(D.Keys[(Old+K)*3]);while(Keys.Num()%3)Keys.Add(FVector4f(0,0,0,0));
  ReusePayload(T,Start);
 }
 // Validate before publishing so failed packing retains the original bank.
 FOKGpuPoseBatch Next;Next.Keys=MoveTemp(Keys);Next.Tracks=MoveTemp(Tracks);Next.Actors=D.Actors;Next.Times=D.Times;Next.Tasks=D.Tasks;Next.InverseBindRows=D.InverseBindRows;
 if(!Next.Validate(Error))return false;
 // Reserve used the uncompressed size. Release that capacity before returning
 // a private bank, otherwise a hundred compact banks retain raw-sized memory.
 Next.Keys.Shrink();D.Keys=MoveTemp(Next.Keys);D.Tracks=MoveTemp(Next.Tracks);return true;
}

bool OKBuildRuntimeGpuActorBank(FOKGpuActorRecord Actor,TArray<FOKRuntimeGpuClip> Clips,FOKGpuSkinPaletteData Palette,FOKGpuAnimationBundle& Out,FString& Error,uint64 MaxBytes){
 auto Fail=[&](const TCHAR* Why){Error=Why;return false;};
 const uint32 Bones=Actor.PoseBones.Num();
 if(Actor.CharacterId.IsEmpty()||!Bones||Bones>65536||Clips.IsEmpty()||Clips.Num()>100||Palette.PoseCount!=Bones)return Fail(TEXT("Invalid runtime actor bank metadata"));
 uint64 Bytes=16ull*(uint64(Palette.Items.Num())+Palette.InverseBindRows.Num()+uint64(Bones)*4+2)+4ull*Clips.Num();
 TSet<FString> Ids;
 for(const auto& Clip:Clips){
  if(Clip.Id.IsEmpty()||Ids.Contains(Clip.Id)||!FMath::IsFinite(Clip.Duration)||Clip.Duration<=0||Clip.Tracks.Num()!=Bones||Clip.Keys.IsEmpty()||Clip.Keys.Num()%3)return Fail(TEXT("Invalid or duplicate private runtime clip"));Ids.Add(Clip.Id);
  Bytes+=16ull*(uint64(Clip.Keys.Num())+Clip.Tracks.Num());
  if(Bytes>FMath::Min(MaxBytes,uint64(MAX_int32)))return Fail(TEXT("Runtime actor bank exceeds preallocation budget"));
  for(uint32 B=0;B<Bones;++B){const auto& Track=Clip.Tracks[B];const int32 Parent=Actor.PoseBones[B].Parent;
   if(Parent<INDEX_NONE||Parent>=int32(B)||Track.Z!=(Parent<0?MAX_uint32:uint32(Parent))||!Track.Y||!OKGpuTrackModeValid(Track)||OKGpuTrackEndRow(Track)>uint64(Clip.Keys.Num()))return Fail(TEXT("Runtime clip private hierarchy or key range differs"));
  }
 }
 FOKGpuAnimationBundle Bank;auto& D=Bank.Data;D.Palette=MoveTemp(Palette);Actor.PoseFirst=0;Actor.PoseCount=Bones;Actor.Clips.Reset();D.OwnedClips.AddDefaulted();
 for(auto& Clip:Clips){const uint32 Offset=D.Pose.Tracks.Num(),KeyBase=D.Pose.Keys.Num()/3;
  for(auto Track:Clip.Tracks){Track.X+=OKGpuTrackRowAddressed(Track)?KeyBase*3:KeyBase;D.Pose.Tracks.Add(Track);}D.Pose.Keys.Append(MoveTemp(Clip.Keys));Actor.Clips.Add({Clip.Id,Offset,Clip.Duration});D.OwnedClips[0].Add(Offset);
 }
 D.Pose.Actors.Add(FUintVector4(0,0,Bones,1));D.Pose.Times.Add(FVector4f(0,0,0,0));
 for(uint32 B=0;B<Bones;++B){const uint32 Bind=D.Pose.InverseBindRows.Num();D.Pose.InverseBindRows.Append({FVector4f(1,0,0,0),FVector4f(0,1,0,0),FVector4f(0,0,1,0)});D.Pose.Tasks.Add(FUintVector4(B,0,Bind,0));}
 Bank.Manifest.Actors.Add(MoveTemp(Actor));
 if(!D.Validate(Error)||!Bank.Manifest.Validate(D,Error))return false;
 Out=MoveTemp(Bank);Error.Reset();return true;
}
