#include "OKGpuAnimationManifest.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Containers/StringConv.h"
#include "ReferenceSkeleton.h"

namespace OKGpuManifestPrivate {
constexpr uint32 ManifestMagic=0x4d474b4f,ManifestVersion=2;
constexpr uint64 ManifestLimit=OKGpuAnimationManifestMaxBytes-52,ManifestHeader=52;
bool Reject(FString& E,const TCHAR* Why){E=Why;return false;}
bool ValidId(const FString& S){if(S.IsEmpty()||S.Len()>1024)return false;for(int I=0;I<S.Len();++I)if(S[I]==0||S[I]=='\n'||S[I]=='\r')return false;return true;}
// This is a mesh identity, never a LoadObject request. Runtime APNG meshes
// have no cooked package; retain an explicit namespace instead of inventing one.
bool ValidMeshIdentity(const FString& S){
 if(!ValidId(S))return false;
 if(S.StartsWith(TEXT("/Game/")))return true;
 const FString Prefix=TEXT("/Runtime/APNG/");
 if(!S.StartsWith(Prefix)||S.Len()==Prefix.Len())return false;
 for(int32 I=Prefix.Len();I<S.Len();++I){const TCHAR C=S[I];if(!((C>='a'&&C<='z')||(C>='A'&&C<='Z')||(C>='0'&&C<='9')||C=='-'||C=='_'))return false;}
 return true;
}
void PutString(FArchive& W,const FString& S){FTCHARToUTF8 U(*S);uint32 N=U.Length();W<<N;W.Serialize(const_cast<ANSICHAR*>(U.Get()),N);}
bool GetString(FMemoryReaderView& R,FString& S){uint32 N=0;R<<N;if(R.IsError()||!N||N>4096||uint64(R.Tell())+N>uint64(R.TotalSize()))return false;TArray<UTF8CHAR> U;U.SetNumUninitialized(N);R.Serialize(U.GetData(),N);FUTF8ToTCHAR T(U.GetData(),N);S=FString(T.Length(),T.Get());FTCHARToUTF8 Back(*S);return !R.IsError()&&ValidId(S)&&Back.Length()==N&&!FMemory::Memcmp(Back.Get(),U.GetData(),N);}
bool Count(FMemoryReaderView& R,uint32& N,uint32 Max,uint32 MinimumBytes){R<<N;return !R.IsError()&&N<=Max&&uint64(N)*MinimumBytes<=uint64(R.TotalSize()-R.Tell());}
bool Shape(const FOKGpuAnimationManifest& M,FString& E){if(M.Actors.IsEmpty()||M.Actors.Num()>4096)return Reject(E,TEXT("Manifest actor count invalid"));TSet<FString> Ids;
 for(const auto& A:M.Actors){if(!ValidId(A.CharacterId)||Ids.Contains(A.CharacterId)||!A.PoseCount||A.Clips.IsEmpty()||A.Clips.Num()>256||A.Meshes.IsEmpty()||A.Meshes.Num()>4096)return Reject(E,TEXT("Manifest actor/clip/mesh identity invalid"));Ids.Add(A.CharacterId);TSet<FString> Clips,Parts;
  if(!A.PoseBones.IsEmpty()){if(A.PoseBones.Num()!=A.PoseCount)return Reject(E,TEXT("Manifest bone identity count differs"));TSet<FName> Names;for(int32 B=0;B<A.PoseBones.Num();++B){const auto& Bone=A.PoseBones[B];if(!ValidId(Bone.Name)||Names.Contains(FName(*Bone.Name))||Bone.Parent<INDEX_NONE||Bone.Parent>=B)return Reject(E,TEXT("Manifest bone identity/parent invalid"));Names.Add(FName(*Bone.Name));}}
  for(const auto& C:A.Clips){if(!ValidId(C.Id)||Clips.Contains(C.Id)||!FMath::IsFinite(C.Duration)||C.Duration<=0)return Reject(E,TEXT("Manifest clip ID/duration invalid"));Clips.Add(C.Id);}
  for(const auto& Mesh:A.Meshes){if(!ValidId(Mesh.PartId)||Parts.Contains(Mesh.PartId)||!ValidMeshIdentity(Mesh.AssetPath)||!Mesh.Bones.Count||Mesh.Sections.Num()>4096)return Reject(E,TEXT("Manifest mesh identity/range invalid"));Parts.Add(Mesh.PartId);}
 }return true;}
}
bool FOKGpuAnimationManifest::Validate(const FOKGpuAnimationCacheData& D,FString& E) const{
 using namespace OKGpuManifestPrivate;if(!Shape(*this,E)||!D.Validate(E))return false;if(Actors.Num()!=D.Pose.Actors.Num())return Reject(E,TEXT("Manifest cache actor count differs"));
 TArray<int32> Dest;Dest.SetNumUninitialized(D.Palette.Items.Num());for(int I=0;I<D.Palette.Items.Num();++I)Dest[D.Palette.Items[I].Z]=I;TBitArray<> Covered(false,Dest.Num());uint32 NextPose=0;
 for(int A=0;A<Actors.Num();++A){const auto& Actor=Actors[A];const auto& GPU=D.Pose.Actors[A];if(Actor.PoseFirst!=NextPose||Actor.PoseCount!=GPU.Z||uint64(NextPose)+Actor.PoseCount>uint64(D.Pose.Tasks.Num())||Actor.Clips.Num()!=D.OwnedClips[A].Num())return Reject(E,TEXT("Manifest pose/clip ranges differ"));
  for(uint32 B=0;B<Actor.PoseCount;++B){const auto& T=D.Pose.Tasks[NextPose+B];if(T.X!=B||T.Y!=uint32(A))return Reject(E,TEXT("Manifest component pose order differs"));for(uint32 R=0;R<3;++R)for(uint32 C=0;C<4;++C)if(D.Pose.InverseBindRows[T.Z+R][C]!=(R==C?1.f:0.f))return Reject(E,TEXT("Manifest requires unbound component poses"));}NextPose+=Actor.PoseCount;
  for(int C=0;C<Actor.Clips.Num();++C){const auto& Clip=Actor.Clips[C];if(Clip.TrackOffset!=D.OwnedClips[A][C])return Reject(E,TEXT("Manifest clip ownership differs"));for(uint32 B=0;B<Actor.PoseCount;++B){const auto& T=D.Pose.Tracks[Clip.TrackOffset+B];if(!Actor.PoseBones.IsEmpty()&&T.Z!=(Actor.PoseBones[B].Parent==INDEX_NONE?MAX_uint32:uint32(Actor.PoseBones[B].Parent)))return Reject(E,TEXT("Manifest bone identity topology differs from clip"));if(D.Pose.Keys[OKGpuTrackPositionRow(T,0)].W<0||D.Pose.Keys[OKGpuTrackPositionRow(T,T.Y-1)].W>Clip.Duration+0.0001f)return Reject(E,TEXT("Manifest duration does not cover keys"));}}
  auto Range=[&](FOKGpuPaletteRange R){if(uint64(R.First)+R.Count>uint64(Dest.Num()))return false;for(uint32 I=0;I<R.Count;++I){uint32 Index=R.First+I;if(Covered[Index])return false;Covered[Index]=true;const auto& Item=D.Palette.Items[Dest[Index]];if(Item.X<Actor.PoseFirst||uint64(Item.X)>=uint64(Actor.PoseFirst)+Actor.PoseCount)return false;}return true;};
  for(const auto& Mesh:Actor.Meshes){if(!Range(Mesh.Bones))return Reject(E,TEXT("Manifest mesh palette overlap/owner invalid"));TSet<uint64> MeshBindings;for(uint32 I=0;I<Mesh.Bones.Count;++I){const auto& Item=D.Palette.Items[Dest[Mesh.Bones.First+I]];MeshBindings.Add((uint64(Item.X)<<32)|Item.Y);}
   for(const auto& Section:Mesh.Sections){if(!Range(Section))return Reject(E,TEXT("Manifest section palette range invalid"));for(uint32 I=0;I<Section.Count;++I){const auto& Item=D.Palette.Items[Dest[Section.First+I]];if(!MeshBindings.Contains((uint64(Item.X)<<32)|Item.Y))return Reject(E,TEXT("Manifest section uses another mesh binding"));}}
  }
 }if(NextPose!=uint32(D.Pose.Tasks.Num())||Covered.Find(false)!=INDEX_NONE)return Reject(E,TEXT("Manifest does not cover pose/palette output"));return true;
}
bool OKEncodeGpuAnimationManifest(const FOKGpuAnimationManifest& M,TArray<uint8>& Out,FString& E){
 using namespace OKGpuManifestPrivate;if(!PLATFORM_LITTLE_ENDIAN||!Shape(M,E))return false;TArray<uint8> Payload;FMemoryWriter W(Payload,true);uint32 N=M.Actors.Num();W<<N;
 for(const auto& A:M.Actors){PutString(W,A.CharacterId);uint32 First=A.PoseFirst,Bones=A.PoseCount,Clips=A.Clips.Num(),Meshes=A.Meshes.Num();W<<First<<Bones;uint32 Identities=A.PoseBones.Num();W<<Identities;for(const auto& Bone:A.PoseBones){PutString(W,Bone.Name);int32 Parent=Bone.Parent;W<<Parent;W.Serialize(const_cast<uint8*>(Bone.ReferenceIdentity.Hash),20);}W<<Clips;for(const auto& C:A.Clips){PutString(W,C.Id);uint32 Track=C.TrackOffset;float Duration=C.Duration;W<<Track<<Duration;}W<<Meshes;
  for(const auto& Mesh:A.Meshes){PutString(W,Mesh.PartId);PutString(W,Mesh.AssetPath);uint32 Start=Mesh.Bones.First,Count=Mesh.Bones.Count,Sections=Mesh.Sections.Num();W<<Start<<Count<<Sections;for(auto S:Mesh.Sections)W<<S.First<<S.Count;}
  if(uint64(Payload.Num())>ManifestLimit)return Reject(E,TEXT("Manifest over byte budget"));
 }if(W.IsError())return Reject(E,TEXT("Manifest write failed"));TArray<uint8> Bytes;FMemoryWriter H(Bytes,true);uint32 Magic=ManifestMagic,Version=ManifestVersion,Size=Payload.Num();auto Hash=FSHA1::HashBuffer(Payload.GetData(),Payload.Num());H<<Magic<<Version<<Size;H.Serialize(const_cast<uint8*>(M.CacheIdentity.Hash),20);H.Serialize(Hash.Hash,20);H.Serialize(Payload.GetData(),Payload.Num());Out=MoveTemp(Bytes);return true;
}
bool OKDecodeGpuAnimationManifest(TConstArrayView<uint8> Bytes,const FSHAHash& Identity,FOKGpuAnimationManifest& Out,FString& E){
 using namespace OKGpuManifestPrivate;if(!PLATFORM_LITTLE_ENDIAN||Bytes.Num()<ManifestHeader||uint64(Bytes.Num())>ManifestLimit+ManifestHeader)return Reject(E,TEXT("Manifest byte count/platform invalid"));FMemoryReaderView R(Bytes,true);uint32 Magic=0,Version=0,Size=0;FSHAHash Source,Hash;R<<Magic<<Version<<Size;R.Serialize(Source.Hash,20);R.Serialize(Hash.Hash,20);
 if(Magic!=ManifestMagic||(Version!=1&&Version!=ManifestVersion)||Size!=Bytes.Num()-ManifestHeader||FMemory::Memcmp(Source.Hash,Identity.Hash,20))return Reject(E,TEXT("Manifest version/source/length mismatch"));auto Actual=FSHA1::HashBuffer(Bytes.GetData()+ManifestHeader,Size);if(FMemory::Memcmp(Actual.Hash,Hash.Hash,20))return Reject(E,TEXT("Manifest checksum differs"));
 FOKGpuAnimationManifest M;M.CacheIdentity=Source;uint32 Actors=0;if(!Count(R,Actors,4096,20)||!Actors)return Reject(E,TEXT("Manifest actor count invalid"));M.Actors.Reserve(Actors);
 for(uint32 I=0;I<Actors;++I){FOKGpuActorRecord A;uint32 Clips=0,Meshes=0;if(!GetString(R,A.CharacterId))return Reject(E,TEXT("Manifest character string invalid"));R<<A.PoseFirst<<A.PoseCount;if(Version>=2){uint32 Bones=0;if(!Count(R,Bones,65536,29)||(Bones&&Bones!=A.PoseCount))return Reject(E,TEXT("Manifest bone identity count invalid"));for(uint32 B=0;B<Bones;++B){FOKGpuBoneIdentity Bone;if(!GetString(R,Bone.Name))return Reject(E,TEXT("Manifest bone name invalid"));R<<Bone.Parent;R.Serialize(Bone.ReferenceIdentity.Hash,20);A.PoseBones.Add(MoveTemp(Bone));}}if(!Count(R,Clips,256,13))return Reject(E,TEXT("Manifest clip count invalid"));
  for(uint32 C=0;C<Clips;++C){FOKGpuClipRecord Clip;if(!GetString(R,Clip.Id))return Reject(E,TEXT("Manifest clip string invalid"));R<<Clip.TrackOffset<<Clip.Duration;A.Clips.Add(MoveTemp(Clip));}
  if(!Count(R,Meshes,4096,22))return Reject(E,TEXT("Manifest mesh count invalid"));for(uint32 J=0;J<Meshes;++J){FOKGpuMeshRecord Mesh;uint32 Sections=0;if(!GetString(R,Mesh.PartId)||!GetString(R,Mesh.AssetPath))return Reject(E,TEXT("Manifest mesh strings invalid"));R<<Mesh.Bones.First<<Mesh.Bones.Count;if(!Count(R,Sections,4096,8))return Reject(E,TEXT("Manifest section count invalid"));for(uint32 S=0;S<Sections;++S){FOKGpuPaletteRange Range;R<<Range.First<<Range.Count;Mesh.Sections.Add(Range);}A.Meshes.Add(MoveTemp(Mesh));}M.Actors.Add(MoveTemp(A));
 }if(R.IsError()||R.Tell()!=Bytes.Num()||!Shape(M,E))return Reject(E,TEXT("Manifest trailing bytes/structure invalid"));Out=MoveTemp(M);return true;
}
bool OKComposeGpuAnimationCaches(TConstArrayView<const FOKGpuAnimationCacheData*> Inputs,TConstArrayView<const FOKGpuAnimationManifest*> Manifests,FOKGpuAnimationCacheData& Out,FOKGpuAnimationManifest& OutManifest,FString& E,uint64 MaxBytes){
 using namespace OKGpuManifestPrivate;if(Inputs.IsEmpty()||Inputs.Num()!=Manifests.Num())return Reject(E,TEXT("Composition input count differs"));uint64 Bytes=0;TSet<FString> Ids;FSHA1 Hash;
 for(int I=0;I<Inputs.Num();++I){if(!Inputs[I]||!Manifests[I]||!Manifests[I]->Validate(*Inputs[I],E))return false;const auto& D=*Inputs[I];Bytes+=16ull*(uint64(D.Pose.Keys.Num())+D.Pose.Tracks.Num()+D.Pose.Actors.Num()+D.Pose.Tasks.Num()+D.Pose.InverseBindRows.Num()+D.Pose.Times.Num()+D.Palette.Items.Num()+D.Palette.InverseBindRows.Num());for(const auto& Clips:D.OwnedClips)Bytes+=uint64(Clips.Num())*4;
  if(Bytes>MaxBytes){E=FString::Printf(TEXT("Composition exceeds byte budget: %llu bytes after %d/%d private banks; budget=%llu"),Bytes,I+1,Inputs.Num(),MaxBytes);return false;}for(const auto& A:Manifests[I]->Actors){if(Ids.Contains(A.CharacterId))return Reject(E,TEXT("Composition duplicates character identity"));Ids.Add(A.CharacterId);}Hash.Update(Manifests[I]->CacheIdentity.Hash,20);
 }
 // All banks have passed their private manifests and aggregate byte budget.
 // Allocate final capacities once instead of repeatedly copying the growing
 // hundred-actor key buffer. Address remapping and actor order stay identical.
 uint64 Counts[10]={};for(int32 I=0;I<Inputs.Num();++I){const auto& S=*Inputs[I];
  Counts[0]+=S.Pose.Keys.Num();Counts[1]+=S.Pose.Tracks.Num();Counts[2]+=S.Pose.Actors.Num();Counts[3]+=S.Pose.Tasks.Num();Counts[4]+=S.Pose.InverseBindRows.Num();Counts[5]+=S.Pose.Times.Num();Counts[6]+=S.Palette.Items.Num();Counts[7]+=S.Palette.InverseBindRows.Num();Counts[8]+=S.OwnedClips.Num();Counts[9]+=Manifests[I]->Actors.Num();
 }
 for(uint64 Count:Counts)if(Count>MAX_int32)return Reject(E,TEXT("Composition array exceeds index capacity"));
 FOKGpuAnimationCacheData D;FOKGpuAnimationManifest M;
 D.Pose.Keys.Reserve(int32(Counts[0]));D.Pose.Tracks.Reserve(int32(Counts[1]));D.Pose.Actors.Reserve(int32(Counts[2]));D.Pose.Tasks.Reserve(int32(Counts[3]));D.Pose.InverseBindRows.Reserve(int32(Counts[4]));D.Pose.Times.Reserve(int32(Counts[5]));D.Palette.Items.Reserve(int32(Counts[6]));D.Palette.InverseBindRows.Reserve(int32(Counts[7]));D.OwnedClips.Reserve(int32(Counts[8]));M.Actors.Reserve(int32(Counts[9]));
 const double ComposeStarted=FPlatformTime::Seconds();
 for(int I=0;I<Inputs.Num();++I){const auto& S=*Inputs[I];uint32 Keys=D.Pose.Keys.Num()/3,Tracks=D.Pose.Tracks.Num(),Actors=D.Pose.Actors.Num(),Tasks=D.Pose.Tasks.Num(),Bind=D.Pose.InverseBindRows.Num(),SkinBind=D.Palette.InverseBindRows.Num(),Dest=D.Palette.Items.Num();D.Pose.Keys.Append(S.Pose.Keys);D.Pose.Times.Append(S.Pose.Times);D.Pose.InverseBindRows.Append(S.Pose.InverseBindRows);D.Palette.InverseBindRows.Append(S.Palette.InverseBindRows);
  for(auto T:S.Pose.Tracks){T.X+=OKGpuTrackRowAddressed(T)?Keys*3:Keys;D.Pose.Tracks.Add(T);}for(auto A:S.Pose.Actors){A.X+=Tracks;A.Y+=Tracks;D.Pose.Actors.Add(A);}for(auto T:S.Pose.Tasks){T.Y+=Actors;T.Z+=Bind;D.Pose.Tasks.Add(T);}for(auto T:S.Palette.Items){T.X+=Tasks;T.Y+=SkinBind;T.Z+=Dest;D.Palette.Items.Add(T);}for(auto Clips:S.OwnedClips){for(auto& C:Clips)C+=Tracks;D.OwnedClips.Add(MoveTemp(Clips));}
  for(auto A:Manifests[I]->Actors){A.PoseFirst+=Tasks;for(auto& C:A.Clips)C.TrackOffset+=Tracks;for(auto& Mesh:A.Meshes){Mesh.Bones.First+=Dest;for(auto& Section:Mesh.Sections)Section.First+=Dest;}M.Actors.Add(MoveTemp(A));}
 }D.Palette.PoseCount=D.Pose.Tasks.Num();Hash.Final();Hash.GetHash(M.CacheIdentity.Hash);if(!M.Validate(D,E))return false;UE_LOG(LogTemp,Display,TEXT("GPU bank composition copy/remap/validation: actors=%d key_bytes=%llu elapsed_ms=%.3f"),M.Actors.Num(),uint64(D.Pose.Keys.Num())*16,(FPlatformTime::Seconds()-ComposeStarted)*1000);Out=MoveTemp(D);OutManifest=MoveTemp(M);return true;
}
bool OKSelectGpuAnimationActors(const FOKGpuAnimationCacheData& S,const FOKGpuAnimationManifest& Source,TConstArrayView<uint32> Selection,FOKGpuAnimationCacheData& Out,FOKGpuAnimationManifest& OutManifest,FString& E){
 using namespace OKGpuManifestPrivate;if(Selection.IsEmpty()||!Source.Validate(S,E))return false;TSet<uint32> Seen;for(uint32 I:Selection){if(I>=uint32(Source.Actors.Num())||Seen.Contains(I))return Reject(E,TEXT("Selection actor index invalid/duplicated"));Seen.Add(I);}
 uint64 Required=0;for(uint32 Index:Selection){const auto& A=Source.Actors[Index];Required+=uint64(A.PoseCount)*64+32;for(const auto& C:A.Clips)for(uint32 B=0;B<A.PoseCount;++B)Required+=16+uint64(S.Pose.Tracks[C.TrackOffset+B].Y)*48;for(const auto& Mesh:A.Meshes){Required+=uint64(Mesh.Bones.Count)*64;for(const auto& Section:Mesh.Sections)Required+=uint64(Section.Count)*64;}if(Required>4ull*1024*1024*1024)return Reject(E,TEXT("Selection exceeds data byte budget"));}
 TArray<int32> Dest;Dest.SetNumUninitialized(S.Palette.Items.Num());for(int I=0;I<S.Palette.Items.Num();++I)Dest[S.Palette.Items[I].Z]=I;
 FOKGpuAnimationCacheData D;FOKGpuAnimationManifest M;FSHA1 Hash;Hash.Update(Source.CacheIdentity.Hash,20);
 for(uint32 Index:Selection){Hash.Update(reinterpret_cast<const uint8*>(&Index),4);auto A=Source.Actors[Index];auto GPU=S.Pose.Actors[Index];uint32 NewActor=D.Pose.Actors.Num(),PoseFirst=D.Pose.Tasks.Num();TArray<uint32> Clips;TMap<uint32,uint32> Tracks;
  for(int C=0;C<A.Clips.Num();++C){uint32 Base=D.Pose.Tracks.Num(),Old=A.Clips[C].TrackOffset;Tracks.Add(Old,Base);Clips.Add(Base);for(uint32 B=0;B<A.PoseCount;++B){auto T=S.Pose.Tracks[Old+B];OKCopyGpuTrackKeys(D.Pose.Keys,S.Pose.Keys,T);D.Pose.Tracks.Add(T);}A.Clips[C].TrackOffset=Base;}
  GPU.X=Tracks[GPU.X];GPU.Y=Tracks[GPU.Y];D.Pose.Actors.Add(GPU);D.Pose.Times.Add(S.Pose.Times[Index]);D.OwnedClips.Add(MoveTemp(Clips));
  for(uint32 B=0;B<A.PoseCount;++B){auto T=S.Pose.Tasks[A.PoseFirst+B];uint32 OldBind=T.Z;T.Y=NewActor;T.Z=D.Pose.InverseBindRows.Num();D.Pose.InverseBindRows.Append(S.Pose.InverseBindRows.GetData()+OldBind,3);D.Pose.Tasks.Add(T);}
  for(auto& Mesh:A.Meshes){TMap<uint32,uint32> BindingMap;auto Copy=[&](FOKGpuPaletteRange& Range){uint32 Old=Range.First;Range.First=D.Palette.Items.Num();for(uint32 B=0;B<Range.Count;++B){auto Item=S.Palette.Items[Dest[Old+B]];uint32 Bind=Item.Y;uint32* Known=BindingMap.Find(Bind);if(Known)Item.Y=*Known;else{Item.Y=D.Palette.InverseBindRows.Num();BindingMap.Add(Bind,Item.Y);D.Palette.InverseBindRows.Append(S.Palette.InverseBindRows.GetData()+Bind,3);}Item.X=PoseFirst+(Item.X-A.PoseFirst);Item.Z=D.Palette.Items.Num();D.Palette.Items.Add(Item);}};Copy(Mesh.Bones);for(auto& Section:Mesh.Sections)Copy(Section);}
  A.PoseFirst=PoseFirst;M.Actors.Add(MoveTemp(A));
 }D.Palette.PoseCount=D.Pose.Tasks.Num();Hash.Final();Hash.GetHash(M.CacheIdentity.Hash);if(!M.Validate(D,E))return false;Out=MoveTemp(D);OutManifest=MoveTemp(M);return true;
}

FSHAHash OKGpuBoneReferenceIdentity(const FTransform& Transform){
 const FMatrix44f Local(Transform.ToMatrixWithScale());return FSHA1::HashBuffer(&Local,sizeof(Local));
}
bool OKBuildGpuMeshPalette(const FOKGpuActorRecord& Actor,const FReferenceSkeleton& Ref,TConstArrayView<FMatrix44f> Inverse,uint32 Total,FOKGpuSkinPaletteData& Out,FString& Error){
 auto Fail=[&](const TCHAR* Why){Error=Why;return false;};
 if(Actor.PoseBones.Num()!=Actor.PoseCount||!Actor.PoseCount||uint64(Actor.PoseFirst)+Actor.PoseCount>Total||Ref.GetNum()==0||Inverse.Num()!=Ref.GetNum())return Fail(TEXT("Runtime mesh requires complete pose identities and inverse binds"));
 TMap<FName,int32> Names;for(int32 B=0;B<Actor.PoseBones.Num();++B){const auto& Bone=Actor.PoseBones[B];const FName Name(*Bone.Name);if(Name.IsNone()||Names.Contains(Name)||Bone.Parent<INDEX_NONE||Bone.Parent>=B)return Fail(TEXT("Runtime pose identity is ambiguous or malformed"));Names.Add(Name,B);}
 FOKGpuSkinPaletteData Palette;Palette.PoseCount=Total;
 for(int32 B=0;B<Ref.GetNum();++B){const int32* Pose=Names.Find(Ref.GetBoneName(B));if(!Pose)return Fail(TEXT("Runtime mesh bone missing from private animation"));const auto& Bone=Actor.PoseBones[*Pose];const int32 Parent=Ref.GetParentIndex(B);
  if((Parent==INDEX_NONE)!=(Bone.Parent==INDEX_NONE)||(Parent!=INDEX_NONE&&Ref.GetBoneName(Parent)!=FName(*Actor.PoseBones[Bone.Parent].Name)))return Fail(TEXT("Runtime mesh private hierarchy differs"));
  const auto Identity=OKGpuBoneReferenceIdentity(Ref.GetRefBonePose()[B]);if(FMemory::Memcmp(Identity.Hash,Bone.ReferenceIdentity.Hash,20))return Fail(TEXT("Runtime mesh private reference pose differs"));
  for(int32 R=0;R<4;++R)for(int32 C=0;C<4;++C)if(!FMath::IsFinite(Inverse[B].M[R][C]))return Fail(TEXT("Runtime inverse bind contains nonfinite values"));
  uint32 Bind=Palette.InverseBindRows.Num();for(int32 R=0;R<3;++R)Palette.InverseBindRows.Add(FVector4f(Inverse[B].M[0][R],Inverse[B].M[1][R],Inverse[B].M[2][R],Inverse[B].M[3][R]));Palette.Items.Add(FUintVector4(Actor.PoseFirst+*Pose,Bind,B,0));
 }
 if(!Palette.Validate(Error))return false;Out=MoveTemp(Palette);return true;
}
