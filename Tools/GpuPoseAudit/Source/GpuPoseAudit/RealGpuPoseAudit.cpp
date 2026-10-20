#include "OKIndependentGpuPose.h"
#include "OKResidentGpuPose.h"
#include "OKGpuSkinPalette.h"
#include "OKGpuAnimationCache.h"
#include "OKGpuAnimationManifest.h"
#include "RenderingThread.h"
#include "SkinningDefinitions.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "RenderGraphUtils.h"
#include "Algo/Reverse.h"
#include <limits>

void CompactGpuTrackFrames(const TArray<TArray<FTransform>>&,int32,double,TArray<int32>&);
namespace {
bool CacheRoundTrip(FOKGpuAnimationCacheData& Data,const FString& Path,uint64& FileBytes,double& LoadMs,int& Rejected,FString& Error,FOKGpuAnimationManifest* Manifest){
 FSHAHash Zero{};TArray<uint8> Original;if(!OKEncodeGpuAnimationCache(Data,Zero,Original,Error))return false;
 FSHAHash Identity=FSHA1::HashBuffer(Original.GetData(),Original.Num());FMemory::Memcpy(Original.GetData()+20,Identity.Hash,20);
 TArray<uint8> ManifestBytes;if(Manifest){Manifest->CacheIdentity=Identity;if(!Manifest->Validate(Data,Error)||!OKEncodeGpuAnimationManifest(*Manifest,ManifestBytes,Error)||!FFileHelper::SaveArrayToFile(ManifestBytes,*(Path+TEXT(".manifest"))))return false;}
 if(!OKSaveGpuAnimationCache(Path,Data,Identity,Error))return false;FileBytes=Original.Num();
 Data={};auto Request=OKLoadGpuAnimationCacheAsync(Path,Identity);auto Result=Request.Consume();if(!Result.Data||!Result.WorkerThread){Error=Result.Error.IsEmpty()?TEXT("Cache loader did not execute on worker"):Result.Error;return false;}LoadMs=Result.LoadMs;Data=MoveTemp(*Result.Data);
 TArray<uint8> Reloaded;if(!OKEncodeGpuAnimationCache(Data,Identity,Reloaded,Error)||Original!=Reloaded){Error=TEXT("Cache roundtrip bytes differ");return false;}
 if(Manifest){TArray<uint8> Disk;if(!FFileHelper::LoadFileToArray(Disk,*(Path+TEXT(".manifest"))))return false;FOKGpuAnimationManifest Loaded;if(!OKDecodeGpuAnimationManifest(Disk,Identity,Loaded,Error)||!Loaded.Validate(Data,Error))return false;TArray<uint8> Reencoded;if(!OKEncodeGpuAnimationManifest(Loaded,Reencoded,Error)||ManifestBytes!=Reencoded)return false;*Manifest=MoveTemp(Loaded);}
 if(Manifest){
  auto Joint=OKLoadGpuAnimationBundleAsync(Path,Path+TEXT(".manifest"),Identity).Consume();
  TArray<uint8> JointCache,JointManifest;
  if(!Joint.Bundle||!Joint.WorkerThread||!OKEncodeGpuAnimationCache(Joint.Bundle->Data,Identity,JointCache,Error)||!OKEncodeGpuAnimationManifest(Joint.Bundle->Manifest,JointManifest,Error)||JointCache!=Original||JointManifest!=ManifestBytes){Error=TEXT("Joint async animation bundle differs from exported bytes");return false;}
  for(int Case=0;Case<4;++Case){auto Expected=Identity;FString Cache=Path,Sidecar=Path+TEXT(".manifest");uint64 Budget=512ull*1024*1024;
   if(Case==0)Expected.Hash[0]^=1;if(Case==1)Sidecar+=TEXT(".missing");if(Case==2)Budget=1;if(Case==3)Cache+=TEXT(".missing");
   auto Bad=OKLoadGpuAnimationBundleAsync(Cache,Sidecar,Expected,Budget).Consume();if(Bad.Bundle||!Bad.WorkerThread||Bad.Error.IsEmpty()){Error=TEXT("Joint animation bundle failed to reject absent/stale/over-budget input");return false;}
  }
 }
 auto HashPayload=[](TArray<uint8>& B){auto H=FSHA1::HashBuffer(B.GetData()+60,B.Num()-60);FMemory::Memcpy(B.GetData()+40,H.Hash,20);};
 for(int Case=0;Case<9;++Case){auto Bad=Original;auto Expected=Identity;uint64 Budget=512ull*1024*1024;
  switch(Case){case 0:Bad.SetNum(31);break;case 1:Bad[4]^=1;break;case 2:Expected.Hash[0]^=1;break;case 3:Bad.Last()^=1;break;case 4:Budget=Bad.Num()-61;break;case 5:Bad.Add(0);break;
   case 6:{uint32 N=MAX_uint32;FMemory::Memcpy(Bad.GetData()+60,&N,4);HashPayload(Bad);break;}
   case 7:{uint64 Offset=104+uint64(Data.Pose.Keys.Num()+Data.Pose.Tracks.Num()+Data.Pose.Actors.Num()+Data.Pose.Tasks.Num()+Data.Pose.InverseBindRows.Num()+Data.Pose.Times.Num())*16;uint32 N=Data.Palette.Items.Num();FMemory::Memcpy(Bad.GetData()+Offset+8,&N,4);HashPayload(Bad);break;}
   case 8:{uint32 N=MAX_uint32;FMemory::Memcpy(Bad.GetData()+Bad.Num()-4,&N,4);HashPayload(Bad);break;}}
  FOKGpuAnimationCacheData Sentinel;Sentinel.Pose.Times.Add(FVector4f(1234,0,0,0));FString Why;
  if(!OKDecodeGpuAnimationCache(Bad,Expected,Sentinel,Why,Budget)&&Sentinel.Pose.Times.Num()==1&&Sentinel.Pose.Times[0].X==1234)++Rejected;
 }
 FString Why;if(!OKSaveGpuAnimationCache(Path,Data,Identity,Why))++Rejected;FOKGpuAnimationCacheData Missing;Missing.Pose.Times.Add(FVector4f(1234,0,0,0));if(!OKLoadGpuAnimationCache(Path+TEXT(".missing"),Identity,Missing,Why)&&Missing.Pose.Times.Num()==1)++Rejected;
 if(Rejected!=11){Error=TEXT("Cache corruption/lifecycle rejection test failed");return false;}return true;
}
bool ManifestSelectionOracle(const FOKGpuAnimationCacheData& D,const FOKGpuAnimationManifest& M,const TArray<FVector4f>& Native,int& Rejected,double& Maximum,int64& Compared,FString& Error){
 if(M.Actors.Num()==1){
  // A per-card shard has no cross-actor corruption cases. Validate selection
  // and identity rebasing without pretending those cases were exercised.
  FOKGpuAnimationCacheData One;FOKGpuAnimationManifest OneM;
  TArray<uint32> Pick{0};if(!OKSelectGpuAnimationActors(D,M,Pick,One,OneM,Error)||!OneM.Validate(One,Error))return false;
  FOKGpuAnimationCacheData Again;FOKGpuAnimationManifest AgainM;if(!OKSelectGpuAnimationActors(One,OneM,Pick,Again,AgainM,Error))return false;
  TArray<uint8> A,B;if(!OKEncodeGpuAnimationCache(One,M.CacheIdentity,A,Error)||!OKEncodeGpuAnimationCache(Again,M.CacheIdentity,B,Error)||A!=B){Error=TEXT("Single actor canonical selection is not stable");return false;}
  Compared=0;Maximum=0;Rejected=0;return true;
 }
 if(M.Actors.IsEmpty()){Error=TEXT("Manifest oracle requires an actor");return false;}TArray<uint8> Encoded;if(!OKEncodeGpuAnimationManifest(M,Encoded,Error))return false;
 for(int Case=0;Case<6;++Case){auto Bad=Encoded;auto Identity=M.CacheIdentity;switch(Case){case 0:Bad.SetNum(51);break;case 1:Bad[4]^=1;break;case 2:Identity.Hash[0]^=1;break;case 3:Bad.Last()^=1;break;case 4:Bad.Add(0);break;case 5:{uint32 N=MAX_uint32;FMemory::Memcpy(Bad.GetData()+52,&N,4);auto H=FSHA1::HashBuffer(Bad.GetData()+52,Bad.Num()-52);FMemory::Memcpy(Bad.GetData()+32,H.Hash,20);break;}}
  FOKGpuAnimationManifest Out;FOKGpuActorRecord Sentinel;Sentinel.CharacterId=TEXT("sentinel");Out.Actors.Add(Sentinel);FString Why;if(!OKDecodeGpuAnimationManifest(Bad,Identity,Out,Why)&&Out.Actors.Num()==1&&Out.Actors[0].CharacterId==TEXT("sentinel"))++Rejected;
 }
 for(int Case=0;Case<6;++Case){auto Bad=M;switch(Case){case 0:++Bad.Actors[0].PoseFirst;break;case 1:Bad.Actors[0].Clips[0].Duration=-1;break;case 2:Bad.Actors[0].Meshes[0].Bones.First=Bad.Actors[1].Meshes[0].Bones.First;break;case 3:Bad.Actors[0].Clips[0].TrackOffset=Bad.Actors[1].Clips[0].TrackOffset;break;case 4:Bad.Actors[0].CharacterId=Bad.Actors[1].CharacterId;break;case 5:Bad.Actors[0].Meshes[0].Sections[0].First=Bad.Actors[1].Meshes[0].Bones.First;break;}FString Why;if(!Bad.Validate(D,Why))++Rejected;}
 FOKGpuAnimationCacheData Out;FOKGpuAnimationManifest OutM;Out.Pose.Times.Add(FVector4f(1234,0,0,0));TArray<uint32> BadSelection{MAX_uint32};FString Why;
 if(!OKSelectGpuAnimationActors(D,M,BadSelection,Out,OutM,Why)&&Out.Pose.Times.Num()==1)++Rejected;BadSelection={0,0};if(!OKSelectGpuAnimationActors(D,M,BadSelection,Out,OutM,Why)&&Out.Pose.Times.Num()==1)++Rejected;
 TArray<const FOKGpuAnimationCacheData*> Duplicate{&D,&D};TArray<const FOKGpuAnimationManifest*> DuplicateM{&M,&M};if(!OKComposeGpuAnimationCaches(Duplicate,DuplicateM,Out,OutM,Why)&&Out.Pose.Times.Num()==1)++Rejected;
 TArray<const FOKGpuAnimationCacheData*> Single{&D};TArray<const FOKGpuAnimationManifest*> SingleM{&M};if(!OKComposeGpuAnimationCaches(Single,SingleM,Out,OutM,Why,1)&&Out.Pose.Times.Num()==1)++Rejected;
 if(Rejected!=16){Error=TEXT("Manifest selection rejection tests failed");return false;}
 TArray<uint32> Odd,Even;for(int I=M.Actors.Num()-1;I>=0;--I)(I%2?Odd:Even).Add(I);FOKGpuAnimationCacheData A,B;FOKGpuAnimationManifest AM,BM;
 if(!OKSelectGpuAnimationActors(D,M,Odd,A,AM,Error)||!OKSelectGpuAnimationActors(D,M,Even,B,BM,Error))return false;
 TArray<const FOKGpuAnimationCacheData*> Inputs{&A,&B};TArray<const FOKGpuAnimationManifest*> Manifests{&AM,&BM};if(!OKComposeGpuAnimationCaches(Inputs,Manifests,Out,OutM,Error))return false;
 TArray<FOKGpuPosePlayback> Playback;TArray<int32> OriginalActors;for(const auto& Actor:OutM.Actors){int Original=M.Actors.IndexOfByPredicate([&](const auto& Source){return Source.CharacterId==Actor.CharacterId;});if(Original<0)return false;OriginalActors.Add(Original);FOKGpuPosePlayback P;P.ClipB=0;float Time=float(double(Actor.Clips[0].Duration)*FMath::Fmod(.731+Original*.173,1.));P.Times=FVector4f(Time,Time,0,0);Playback.Add(P);}
 FOKResidentGpuPose Pose;FOKGpuSkinPalette Skin;FOKResidentPoseStats PS;FOKGpuSkinPaletteStats SS;TArray<FVector4f> Current,Previous,Palette;
 bool Read=OKReadbackResidentSkinPalette(Pose,Skin,&Out.Pose,Out.OwnedClips,&Out.Palette,Playback,0,Current,Previous,Palette,PS,SS,Error);
 ENQUEUE_RENDER_COMMAND(ReleaseSelectionOracle)([&](FRHICommandListImmediate&){Skin.Release();Pose.Release();});FlushRenderingCommands();if(!Read)return false;
 auto Compare=[&](float GPU,float CPU){if(!FMath::IsFinite(GPU)){Maximum=MAX_dbl;return;}Maximum=FMath::Max(Maximum,FMath::Abs(double(GPU)-CPU));++Compared;};
 TArray<FVector4f> SourcePalette;SourcePalette.SetNumUninitialized(D.Palette.Items.Num()*3);for(const auto& Item:D.Palette.Items){FMatrix44f Bind=FMatrix44f::Identity,Component=FMatrix44f::Identity;for(int R=0;R<3;++R)for(int C=0;C<4;++C){Bind.M[C][R]=D.Palette.InverseBindRows[Item.Y+R][C];Component.M[C][R]=Native[Item.X*3+R][C];}FCompressedBoneTransform Packed;StoreCompressedBoneTransform(Packed,Bind*Component);FMemory::Memcpy(SourcePalette.GetData()+Item.Z*3,&Packed,48);}
 for(int I=0;I<OutM.Actors.Num();++I){const auto& New=OutM.Actors[I];const auto& Old=M.Actors[OriginalActors[I]];for(uint32 Bn=0;Bn<New.PoseCount*3;++Bn)for(int C=0;C<4;++C){Compare(Current[New.PoseFirst*3+Bn][C],Native[Old.PoseFirst*3+Bn][C]);Compare(Previous[New.PoseFirst*3+Bn][C],Native[Old.PoseFirst*3+Bn][C]);}
  auto CompareRange=[&](FOKGpuPaletteRange N,FOKGpuPaletteRange O){if(N.Count!=O.Count){Maximum=MAX_dbl;return;}for(uint32 R=0;R<N.Count*3;++R)for(int C=0;C<4;++C){Compare(Palette[N.First*3+R][C],SourcePalette[O.First*3+R][C]);Compare(Palette[Out.Palette.Items.Num()*3+N.First*3+R][C],SourcePalette[O.First*3+R][C]);}};
  for(const auto& Mesh:New.Meshes){const auto* Original=Old.Meshes.FindByPredicate([&](const auto& S){return S.PartId==Mesh.PartId;});if(!Original)return false;CompareRange(Mesh.Bones,Original->Bones);if(Mesh.Sections.Num()!=Original->Sections.Num())return false;for(int S=0;S<Mesh.Sections.Num();++S)CompareRange(Mesh.Sections[S],Original->Sections[S]);}
 }return Compared>0&&Maximum<=.001;
}
}
#include "RenderingThread.h"
#include "KKImportedCharacter.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "Animation/AnimCompressionTypes.h"
#include "Animation/AnimBoneCompressionCodec.h"
#include "Animation/AnimSequenceDecompressionContext.h"
#include "AnimEncoding.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "UObject/StrongObjectPtr.h"
#include "Json.h"
#include "Engine/SkeletalMesh.h"
#include "SkeletalMeshAttributes.h"

bool RunRealGpuPoseAudit(const FString& Input,const FString& AssetRoot,const FString& Output,bool Resident,bool SkinPalette,bool Cache,bool ManifestMode){
 if(ManifestMode&&!Cache)return false;
 if(Cache&&(!Resident||!SkinPalette))return false;
 if(SkinPalette&&!Resident)return false;
 FString Text;if(!FFileHelper::LoadFileToString(Text,*Input))return false;
 TSharedPtr<FJsonObject> Request;if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Request)||!Request)return false;
 const TArray<TSharedPtr<FJsonValue>>* Paths=nullptr;if(!Request->TryGetArrayField(TEXT("libraries"),Paths)||Paths->IsEmpty())return false;
 const TArray<TSharedPtr<FJsonValue>>* ExpectedCharacters=nullptr;Request->TryGetArrayField(TEXT("expected_characters"),ExpectedCharacters);if(ExpectedCharacters&&ExpectedCharacters->Num()!=Paths->Num())return false;
 FPackageName::RegisterMountPoint(TEXT("/Game/"),FPaths::ConvertRelativePathToFull(AssetRoot)/TEXT(""));
 constexpr double Phases[]={0.,.137,.37,.731,.99,1.};auto PhaseForActor=[&](int Phase,int Actor){return Phase==0||Phase==5?Phases[Phase]:FMath::Fmod(Phases[Phase]+Actor*.173,1.);};
 struct FInfluence {uint32 Task;FVector3f Point;float Weight;uint32 PaletteIndex;FVector3f Position;};struct FProbe {TArray<FInfluence> Influences;};TArray<FProbe> Probes;
 FOKGpuSkinPaletteData PaletteData;TArray<FMatrix44f> PaletteBindMatrices;int SectionCount=0;
 FOKGpuAnimationManifest Manifest;
 FOKGpuPoseBatch Batch;TArray<TArray<FVector4f>> Expected,ExpectedLocal;Expected.SetNum(UE_ARRAY_COUNT(Phases));ExpectedLocal.SetNum(UE_ARRAY_COUNT(Phases));TArray<FString> BoneNames;
 TArray<double> Durations;TArray<TSharedPtr<FJsonValue>> Rows;bool Supported=true;int64 OriginalBytes=0;
 for(const auto& Value:*Paths){
  const FString Path=Value->AsString();TStrongObjectPtr<UKKImportedAnimationLibrary> Library(LoadObject<UKKImportedAnimationLibrary>(nullptr,*Path));
  auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("library"),Path);FString Reason;
  int32 RequestedClip=0;Request->TryGetNumberField(TEXT("clip_index"),RequestedClip);
  if(!Library||!Library->Animations.IsValidIndex(RequestedClip)||!Library->Animations[RequestedClip].Sequence)return false;
  if(ExpectedCharacters&&(!Library->Character||Library->Character->GetPathName()!=(*ExpectedCharacters)[Rows.Num()]->AsString())){UE_LOG(LogTemp,Error,TEXT("GPU shard animation library does not belong to requested character: %s"),*Path);return false;}
  Row->SetStringField(TEXT("character"),Library->Character?Library->Character->GetPathName():Path);
  auto* Clip=Library->Animations[RequestedClip].Sequence.Get();Clip->CacheDerivedDataForCurrentPlatform();Clip->WaitOnExistingCompression();auto DataScope=Clip->GetCompressedData();const auto& Data=DataScope.Get();
  Row->SetStringField(TEXT("sequence"),Clip->GetPathName());
  if(!Data.IsValid(Clip)||!Clip->GetSkeleton()||Clip->IsValidAdditive()||Clip->Interpolation!=EAnimInterpolationType::Linear||!Data.BoneCompressionCodec||Data.BoneCompressionCodec->GetClass()->GetName()!=TEXT("AnimCompress_BitwiseCompressOnly")){Reason=TEXT("Unsupported codec/additive/interpolation");}
  if(!Reason.IsEmpty()){Row->SetStringField(TEXT("unsupported_reason"),Reason);Rows.Add(MakeShared<FJsonValueObject>(Row));Supported=false;continue;}
  const auto& Encoded=static_cast<const FUECompressedAnimData&>(*Data.CompressedDataStructure);
  if(Encoded.KeyEncodingFormat!=AKF_ConstantKeyLerp){Row->SetStringField(TEXT("unsupported_reason"),TEXT("Nonuniform key encoding"));Rows.Add(MakeShared<FJsonValueObject>(Row));Supported=false;continue;}
  const auto& Ref=Clip->GetSkeleton()->GetReferenceSkeleton();const auto& RefPoses=Clip->GetSkeleton()->GetRefLocalPoses();const int Bones=RefPoses.Num();
  BoneTrackArray Pairs;int Samples=1;TArray<int> Counts;Counts.Init(1,Bones);
  for(int Track=0;Track<Data.CompressedTrackToSkeletonMapTable.Num();++Track){int Bone=Data.CompressedTrackToSkeletonMapTable[Track].BoneTreeIndex;
   if(!RefPoses.IsValidIndex(Bone)||!Encoded.CompressedTrackOffsets.IsValidIndex(Track*4+3)){Reason=TEXT("Invalid compressed mapping");break;}
   const int T=Encoded.CompressedTrackOffsets[Track*4+1],R=Encoded.CompressedTrackOffsets[Track*4+3],S=Encoded.CompressedScaleOffsets.IsValid()?Encoded.CompressedScaleOffsets.GetOffsetData(Track,1):1;
   Counts[Bone]=FMath::Max3(T,R,S);Samples=FMath::Max(Samples,Counts[Bone]);Pairs.Add(BoneTrackPair(Bone,Track));
  }
  // Restrict this initial exporter to native uniform channels on the same grid.
  // Resampling arbitrary different rotation grids would change normalized lerp.
  for(int Track=0;Reason.IsEmpty()&&Track<Pairs.Num();++Track){for(int N:{Encoded.CompressedTrackOffsets[Track*4+1],Encoded.CompressedTrackOffsets[Track*4+3],Encoded.CompressedScaleOffsets.IsValid()?Encoded.CompressedScaleOffsets.GetOffsetData(Track,1):1})if(N!=1&&N!=Samples)Reason=TEXT("Different channel grids require separate channel export");}
  if(Samples>2048||Bones>65536||Bones!=Ref.GetNum())Reason=FString::Printf(TEXT("Unsupported dimensions bones=%d ref=%d samples=%d"),Bones,Ref.GetNum(),Samples);
  if(!Reason.IsEmpty()){Row->SetStringField(TEXT("unsupported_reason"),Reason);Rows.Add(MakeShared<FJsonValueObject>(Row));Supported=false;continue;}
  const auto Rate=Clip->GetSamplingFrameRate();const int Frames=Rate.AsFrameTime(Clip->GetPlayLength()).RoundToFrame().Value;
  FAnimSequenceDecompressionContext Context(Rate,Frames,Clip->Interpolation,NAME_None,*Data.CompressedDataStructure,RefPoses,Data.CompressedTrackToSkeletonMapTable,Clip->GetSkeleton(),false,Clip->GetAdditiveAnimType());
  auto Decode=[&](double Time){Context.Seek(Time);TArray<FTransform> Pose=RefPoses;TArrayView<FTransform> View(Pose);Data.BoneCompressionCodec->DecompressPose(Context,Pairs,Pairs,Pairs,View);return Pose;};
  TArray<TArray<FTransform>> FramePoses;FramePoses.Reserve(Samples);for(int K=0;K<Samples;++K)FramePoses.Add(Decode(Clip->GetPlayLength()*K/FMath::Max(1,Samples-1)));
  bool CompactKeys=false;Request->TryGetBoolField(TEXT("compact_keys"),CompactKeys);
  TArray<double> Radii;Radii.Init(0,Bones);
  if(CompactKeys&&Library->Character&&!Library->Character->Outfits.IsEmpty())for(const auto& P:Library->Character->Outfits[0].Parts)if(auto* Mesh=P.SkinnedModel.Get()){
   const auto* Desc=Mesh->GetMeshDescription(0);if(!Desc)return false;FSkeletalMeshConstAttributes Attributes(*Desc);const auto Positions=Attributes.GetVertexPositions();const auto& Inverse=Mesh->GetRefBasesInvMatrix();const auto& MeshRef=Mesh->GetRefSkeleton();
   for(auto V:Desc->Vertices().GetElementIDs())for(auto W:Attributes.GetVertexSkinWeights().Get(V))if(W.GetRawWeight()>0){const int32 MB=W.GetBoneIndex(),SB=Ref.FindBoneIndex(MeshRef.GetBoneName(MB));if(SB<0||!Inverse.IsValidIndex(MB))return false;Radii[SB]=FMath::Max(Radii[SB],double(Inverse[MB].TransformPosition(Positions[V]).Size()));}
  }
  uint32 Base=Batch.Tracks.Num(),Actor=Batch.Actors.Num(),PoseBase=Batch.Tasks.Num();int ConstantBones=0;
  FOKGpuActorRecord ActorRecord;ActorRecord.CharacterId=Library->Character?Library->Character->GetPathName():Path;ActorRecord.PoseFirst=PoseBase;ActorRecord.PoseCount=Bones;ActorRecord.Clips.Add({Clip->GetPathName(),Base,float(Clip->GetPlayLength())});for(int B=0;B<Bones;++B)ActorRecord.PoseBones.Add({Ref.GetBoneName(B).ToString(),Ref.GetParentIndex(B),OKGpuBoneReferenceIdentity(RefPoses[B])});int PartIndex=-1;
  for(int Bone=0;Bone<Bones;++Bone){BoneNames.Add(Ref.GetBoneName(Bone).ToString());uint32 First=Batch.Keys.Num()/3;int Count=Counts[Bone]==1?1:Samples;ConstantBones+=Count==1;
   TArray<int32> Keep;
   if(Count==1)Keep.Add(0);else if(CompactKeys&&Ref.GetParentIndex(Bone)==0&&Ref.GetBoneName(Bone).ToString().StartsWith(TEXT("__ok_palette_"))&&RefPoses[0].Equals(FTransform::Identity,1e-6))CompactGpuTrackFrames(FramePoses,Bone,Radii[Bone],Keep);else for(int K=0;K<Count;++K)Keep.Add(K);
   Batch.Tracks.Add(FUintVector4(First,Keep.Num(),Ref.GetParentIndex(Bone)<0?MAX_uint32:uint32(Ref.GetParentIndex(Bone)),1));
   for(int K:Keep){const auto& P=FramePoses[K][Bone];auto T=P.GetTranslation(),S=P.GetScale3D();auto Q=P.GetRotation();Batch.Keys.Add(FVector4f(T.X,T.Y,T.Z,Clip->GetPlayLength()*K/FMath::Max(1,Samples-1)));Batch.Keys.Add(FVector4f(Q.X,Q.Y,Q.Z,Q.W));Batch.Keys.Add(FVector4f(S.X,S.Y,S.Z,0));}
   uint32 Bind=Batch.InverseBindRows.Num();Batch.InverseBindRows.Append({FVector4f(1,0,0,0),FVector4f(0,1,0,0),FVector4f(0,0,1,0)});Batch.Tasks.Add(FUintVector4(Bone,Actor,Bind,0));
  }
  if(!Library->Character||Library->Character->Outfits.IsEmpty())return false;
  int CardVertices=0;
  for(const auto& Part:Library->Character->Outfits[0].Parts)if(auto* Mesh=Part.SkinnedModel.Get()){
   ++PartIndex;FOKGpuMeshRecord MeshRecord;MeshRecord.PartId=FString::Printf(TEXT("outfit:0/skinned-part:%d"),PartIndex);MeshRecord.AssetPath=Mesh->GetPathName();MeshRecord.Bones={uint32(PaletteData.Items.Num()),uint32(Mesh->GetRefSkeleton().GetNum())};
   const auto* Desc=Mesh->GetMeshDescription(0);const auto& MeshRef=Mesh->GetRefSkeleton();const auto& Inverse=Mesh->GetRefBasesInvMatrix();
   if(!Desc||Inverse.Num()!=MeshRef.GetNum()){UE_LOG(LogTemp,Error,TEXT("Missing editable vertices or inverse binding: %s"),*Mesh->GetPathName());return false;}
   FSkeletalMeshConstAttributes Attributes(*Desc);auto Positions=Attributes.GetVertexPositions();
   uint32 PaletteBase=PaletteData.Items.Num(),BindBase=PaletteData.InverseBindRows.Num();
   if(SkinPalette){for(int MB=0;MB<MeshRef.GetNum();++MB){int SB=Ref.FindBoneIndex(MeshRef.GetBoneName(MB));if(SB<0)return false;const auto& M=Inverse[MB];uint32 Bind=PaletteData.InverseBindRows.Num();PaletteBindMatrices.Add(M);
     for(int R=0;R<3;++R)PaletteData.InverseBindRows.Add(FVector4f(M.M[0][R],M.M[1][R],M.M[2][R],M.M[3][R]));PaletteData.Items.Add(FUintVector4(PoseBase+uint32(SB),Bind,PaletteData.Items.Num(),0));}
    const auto* RenderData=Mesh->GetResourceForRendering();if(!RenderData||RenderData->LODRenderData.IsEmpty())return false;
    for(const auto& Section:RenderData->LODRenderData[0].RenderSections){MeshRecord.Sections.Add({uint32(PaletteData.Items.Num()),uint32(Section.BoneMap.Num())});++SectionCount;for(uint16 MB:Section.BoneMap){if(MB>=MeshRef.GetNum())return false;int SB=Ref.FindBoneIndex(MeshRef.GetBoneName(MB));if(SB<0)return false;PaletteData.Items.Add(FUintVector4(PoseBase+uint32(SB),BindBase+uint32(MB)*3,PaletteData.Items.Num(),0));}}
   }if(ManifestMode)ActorRecord.Meshes.Add(MoveTemp(MeshRecord));
   for(auto V:Desc->Vertices().GetElementIDs()){FProbe Probe;
    for(auto W:Attributes.GetVertexSkinWeights().Get(V))if(W.GetRawWeight()>0){int MB=W.GetBoneIndex();if(MB<0||MB>=MeshRef.GetNum())return false;int SB=Ref.FindBoneIndex(MeshRef.GetBoneName(MB));if(SB<0)return false;
     FVector3f Bound(Inverse[MB].TransformPosition(Positions[V]));Probe.Influences.Add({PoseBase+uint32(SB),Bound,W.GetWeight(),PaletteBase+uint32(MB),Positions[V]});}
    if(Probe.Influences.IsEmpty())return false;Probes.Add(MoveTemp(Probe));++CardVertices;
   }
  }
  Row->SetNumberField(TEXT("editable_low_vertices"),CardVertices);
  if(ManifestMode)Manifest.Actors.Add(MoveTemp(ActorRecord));
  Batch.Actors.Add(FUintVector4(Base,Base,Bones,1));Batch.Times.Add(FVector4f(0,0,0,0));Durations.Add(Clip->GetPlayLength());
  for(int Phase=0;Phase<UE_ARRAY_COUNT(Phases);++Phase){auto Pose=Decode(Clip->GetPlayLength()*PhaseForActor(Phase,Actor));
   for(int Bone=0;Bone<Bones;++Bone){FMatrix44f L(Pose[Bone].ToMatrixWithScale());for(int R=0;R<3;++R)ExpectedLocal[Phase].Add(FVector4f(L.M[0][R],L.M[1][R],L.M[2][R],L.M[3][R]));int Parent=Ref.GetParentIndex(Bone);if(Parent>=0)Pose[Bone]=Pose[Bone]*Pose[Parent];FMatrix44f M(Pose[Bone].ToMatrixWithScale());for(int R=0;R<3;++R)Expected[Phase].Add(FVector4f(M.M[0][R],M.M[1][R],M.M[2][R],M.M[3][R]));}
  }
  OriginalBytes+=Data.CompressedByteStream.Num();Row->SetBoolField(TEXT("compact_keys"),CompactKeys);Row->SetNumberField(TEXT("bones"),Bones);Row->SetNumberField(TEXT("samples"),Samples);Row->SetNumberField(TEXT("constant_bones"),ConstantBones);Row->SetNumberField(TEXT("duration"),Clip->GetPlayLength());Rows.Add(MakeShared<FJsonValueObject>(Row));
  UE_LOG(LogTemp,Display,TEXT("Real GPU pose prepared %s bones=%d samples=%d"),*Clip->GetPathName(),Bones,Samples);
 }
 auto Report=MakeShared<FJsonObject>();Report->SetArrayField(TEXT("cards"),Rows);Report->SetBoolField(TEXT("synthetic_only"),false);Report->SetBoolField(TEXT("fps_evidence"),false);Report->SetBoolField(TEXT("assets_modified"),false);Report->SetBoolField(TEXT("sharing"),false);Report->SetBoolField(TEXT("independent_phase"),true);Report->SetNumberField(TEXT("characters"),Batch.Actors.Num());Report->SetNumberField(TEXT("compressed_source_bytes"),double(OriginalBytes));Report->SetNumberField(TEXT("gpu_key_bytes"),double(Batch.Keys.Num())*sizeof(FVector4f));Report->SetBoolField(TEXT("all_supported"),Supported);
 bool Passed=Supported&&Batch.Actors.Num()==Paths->Num();FString Error;double Maximum=0,Squared=0;int64 Elements=0;int WorstTask=-1,WorstPhase=-1;bool Finite=true;double LocalMaximum=0,VertexMaximum=0;int64 ComparedVertices=0;
 if(Passed&&!Batch.Validate(Error))Passed=false;
 FOKGpuSkinPalette PaletteOwner;FOKGpuSkinPaletteStats PaletteStats;PaletteData.PoseCount=Batch.Tasks.Num();uint64 PaletteImmutable=0;double PaletteMaximum=0,PaletteVertexMaximum=0;int64 PaletteElements=0;bool PaletteStable=true;
 int PaletteInvalidData=0,PaletteInvalidRuntime=0;
 if(SkinPalette){if(!PaletteData.Validate(Error))Passed=false;else{
   for(int Case=0;Case<5;++Case){auto Bad=PaletteData;switch(Case){case 0:Bad.Items[0].X=Bad.PoseCount;break;case 1:Bad.Items[0].Y=Bad.InverseBindRows.Num();break;case 2:Bad.Items[0].Z=Bad.Items[1].Z;break;case 3:Bad.InverseBindRows[0].X=std::numeric_limits<float>::quiet_NaN();break;case 4:Bad.Items[0].W=1;break;}FString Why;if(!Bad.Validate(Why))++PaletteInvalidData;}
   Algo::Reverse(PaletteData.Items);
  }}
 FOKResidentGpuPose ResidentPose;FOKResidentPoseStats ResidentStats;TArray<TArray<uint32>> OwnedClips;TArray<FOKGpuPosePlayback> Playback;uint64 ImmutableBytes=0;double HistoryMaximum=0;bool ImmutableStable=true;
 if(Resident)for(const auto& Actor:Batch.Actors){OwnedClips.Add({Actor.X});FOKGpuPosePlayback P;P.ClipB=0;Playback.Add(P);}
 uint64 CacheBytes=0;double CacheLoadMs=0;int CacheRejected=0;bool CachePassed=false;int ManifestRejected=0;double SelectionMaximum=0;int64 SelectionCompared=0;bool ManifestPassed=false;
 if(Cache&&Passed){FOKGpuAnimationCacheData D;D.Pose=MoveTemp(Batch);D.Palette=MoveTemp(PaletteData);D.OwnedClips=MoveTemp(OwnedClips);CachePassed=CacheRoundTrip(D,Output+TEXT(".gpuanim"),CacheBytes,CacheLoadMs,CacheRejected,Error,ManifestMode?&Manifest:nullptr);Passed&=CachePassed;if(ManifestMode&&CachePassed){ManifestPassed=ManifestSelectionOracle(D,Manifest,Expected[3],ManifestRejected,SelectionMaximum,SelectionCompared,Error);Passed&=ManifestPassed;}Batch=MoveTemp(D.Pose);PaletteData=MoveTemp(D.Palette);OwnedClips=MoveTemp(D.OwnedClips);}
 if(Passed)for(int Phase=0;Phase<UE_ARRAY_COUNT(Phases);++Phase){for(int A=0;A<Batch.Actors.Num();++A)Batch.Times[A]=FVector4f(Durations[A]*PhaseForActor(Phase,A),Durations[A]*PhaseForActor(Phase,A),0,0);
  TArray<FVector4f> Actual,ActualPalette;
  if(Resident){for(int A=0;A<Playback.Num();++A)Playback[A].Times=Batch.Times[A];TArray<FVector4f> Previous;
   bool Read=SkinPalette?OKReadbackResidentSkinPalette(ResidentPose,PaletteOwner,Phase==0?&Batch:nullptr,OwnedClips,Phase==0?&PaletteData:nullptr,Playback,Phase,Actual,Previous,ActualPalette,ResidentStats,PaletteStats,Error):OKReadbackResidentGpuPose(ResidentPose,Phase==0?&Batch:nullptr,OwnedClips,Playback,Phase,false,Actual,Previous,ResidentStats,Error);
   if(!Read){Passed=false;break;}
   if(SkinPalette){if(Phase==0)PaletteImmutable=PaletteStats.ImmutableUploadBytes;PaletteStable&=PaletteStats.ImmutableUploadBytes==PaletteImmutable&&PaletteStats.Initializations==1;
    for(int History=0;History<2;++History){const auto& Reference=Expected[History?FMath::Max(0,Phase-1):Phase];for(const auto& Item:PaletteData.Items){FMatrix44f Component=FMatrix44f::Identity;
      for(int R=0;R<3;++R)for(int C=0;C<4;++C)Component.M[C][R]=Reference[Item.X*3+R][C];FMatrix44f Skin=PaletteBindMatrices[Item.Y/3]*Component;FCompressedBoneTransform Packed;StoreCompressedBoneTransform(Packed,Skin);
      static_assert(sizeof(FCompressedBoneTransform)==48,"Palette oracle requires uncompressed float32 transforms");const float* RefData=reinterpret_cast<const float*>(&Packed);uint32 D=(History*PaletteData.Items.Num()+Item.Z)*3;
      for(int R=0;R<3;++R)for(int C=0;C<4;++C){float Value=ActualPalette[D+R][C];Finite&=FMath::IsFinite(Value);PaletteMaximum=FMath::Max(PaletteMaximum,FMath::Abs(double(Value)-RefData[R*4+C]));++PaletteElements;}
    }}
    for(const auto& Probe:Probes){FVector3f P(0,0,0);for(const auto& W:Probe.Influences){FVector4f V(W.Position,1);P+=FVector3f(Dot4(ActualPalette[W.PaletteIndex*3],V),Dot4(ActualPalette[W.PaletteIndex*3+1],V),Dot4(ActualPalette[W.PaletteIndex*3+2],V))*W.Weight;}
     FVector3f Reference(0,0,0);for(const auto& W:Probe.Influences){FVector4f V(W.Point,1);Reference+=FVector3f(Dot4(Expected[Phase][W.Task*3],V),Dot4(Expected[Phase][W.Task*3+1],V),Dot4(Expected[Phase][W.Task*3+2],V))*W.Weight;}Finite&=!P.ContainsNaN();PaletteVertexMaximum=FMath::Max(PaletteVertexMaximum,double((P-Reference).Size()));}
   }
   if(Phase==0)ImmutableBytes=ResidentStats.ImmutableUploadBytes;ImmutableStable&=ResidentStats.ImmutableUploadBytes==ImmutableBytes&&ResidentStats.Initializations==1;
   const auto& Reference=Expected[FMath::Max(0,Phase-1)];if(Previous.Num()!=Reference.Num()){Passed=false;Error=TEXT("Previous pose dimensions differ");break;}
   for(int I=0;I<Previous.Num();++I)for(int C=0;C<4;++C){Finite&=FMath::IsFinite(Previous[I][C]);HistoryMaximum=FMath::Max(HistoryMaximum,FMath::Abs(double(Previous[I][C])-Reference[I][C]));}
  }else{
   if(!OKReadbackIndependentGpuPose(Batch,Actual,Error)){Passed=false;break;}
   auto LocalBatch=Batch;for(auto& Track:LocalBatch.Tracks)Track.Z=MAX_uint32;TArray<FVector4f> LocalActual;if(!OKReadbackIndependentGpuPose(LocalBatch,LocalActual,Error)){Passed=false;break;}for(int I=0;I<LocalActual.Num();++I)for(int C=0;C<4;++C)LocalMaximum=FMath::Max(LocalMaximum,FMath::Abs(double(LocalActual[I][C])-ExpectedLocal[Phase][I][C]));
  }
  auto Skin=[&](const FProbe& Probe,const TArray<FVector4f>& Matrices){FVector3f P(0,0,0);for(const auto& W:Probe.Influences){const FVector4f V(W.Point,1);P+=FVector3f(Dot4(Matrices[W.Task*3],V),Dot4(Matrices[W.Task*3+1],V),Dot4(Matrices[W.Task*3+2],V))*W.Weight;}return P;};
  for(const auto& Probe:Probes){auto GPU=Skin(Probe,Actual),CPU=Skin(Probe,Expected[Phase]);Finite&=!GPU.ContainsNaN()&&!CPU.ContainsNaN();VertexMaximum=FMath::Max(VertexMaximum,double((GPU-CPU).Size()));++ComparedVertices;}
  for(int I=0;I<Actual.Num();++I)for(int C=0;C<4;++C){Finite&=FMath::IsFinite(Actual[I][C]);double E=FMath::Abs(double(Actual[I][C])-Expected[Phase][I][C]);if(E>Maximum){Maximum=E;WorstTask=I/3;WorstPhase=Phase;}Squared+=E*E;++Elements;}
 }
 // Absolute matrix comparison includes cm translation. Fixed before first run.
 constexpr double Tolerance=0.001;Passed&=Finite&&Elements>0&&Maximum<=Tolerance&&ComparedVertices>0&&VertexMaximum<=0.01;
 if(SkinPalette){Passed&=PaletteStable&&PaletteElements>0&&SectionCount>0&&PaletteMaximum<=Tolerance&&PaletteVertexMaximum<=0.01&&PaletteStats.Passes==3*UE_ARRAY_COUNT(Phases);
  if(Passed){ENQUEUE_RENDER_COMMAND(InvalidPaletteRuntime)([&](FRHICommandListImmediate& R){FRDGBuilder G(R);FOKResidentPoseFrame Frame;FString Why;auto PS=ResidentPose.GetStats();auto SS=PaletteOwner.GetStats();
    if(!ResidentPose.Evaluate(G,Playback,6,PS.Generation,false,Frame,Why)){G.Execute();return;}auto* Out=G.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(16,SS.Bones*6),TEXT("InvalidPalette.Output"));
    if(!PaletteOwner.AddPass(G,Frame,SS.Generation+1,Out,0,SS.Bones*3,Why))++PaletteInvalidRuntime;
    auto Bad=Frame;++Bad.Generation;if(!PaletteOwner.AddPass(G,Bad,SS.Generation,Out,0,SS.Bones*3,Why))++PaletteInvalidRuntime;
    if(!PaletteOwner.AddPass(G,Frame,SS.Generation,Out,0,0,Why))++PaletteInvalidRuntime;
    if(!PaletteOwner.AddPass(G,Frame,SS.Generation,Out,1,SS.Bones*3+1,Why))++PaletteInvalidRuntime;
    Bad=Frame;Bad.Current=Out;if(!PaletteOwner.AddPass(G,Bad,SS.Generation,Out,0,SS.Bones*3,Why))++PaletteInvalidRuntime;
    if(!PaletteOwner.AddPass(G,Frame,SS.Generation,Frame.Current,0,SS.Bones*3,Why))++PaletteInvalidRuntime;G.Execute();
   });FlushRenderingCommands();}
  Passed&=PaletteInvalidData==5&&PaletteInvalidRuntime==6;ENQUEUE_RENDER_COMMAND(ReleasePalette)([&](FRHICommandListImmediate&){PaletteOwner.Release();});FlushRenderingCommands();}
 Report->SetNumberField(TEXT("palette_invalid_data_rejected"),PaletteInvalidData);Report->SetNumberField(TEXT("palette_invalid_runtime_rejected"),PaletteInvalidRuntime);Report->SetBoolField(TEXT("palette_guard_ranges_passed"),SkinPalette&&Passed);
 Report->SetBoolField(TEXT("manifest"),ManifestMode);Report->SetBoolField(TEXT("manifest_passed"),ManifestPassed);Report->SetNumberField(TEXT("manifest_invalid_inputs_rejected"),ManifestRejected);Report->SetNumberField(TEXT("selection_max_error"),SelectionMaximum);Report->SetNumberField(TEXT("selection_compared_elements"),double(SelectionCompared));
 Report->SetBoolField(TEXT("single_actor_manifest"),ManifestMode&&Manifest.Actors.Num()==1);
 Report->SetBoolField(TEXT("joint_bundle_loader_passed"),ManifestMode&&CachePassed);Report->SetNumberField(TEXT("joint_bundle_invalid_inputs_rejected"),ManifestMode&&CachePassed?4:0);
 Report->SetBoolField(TEXT("cache_roundtrip"),Cache);Report->SetBoolField(TEXT("cache_roundtrip_passed"),CachePassed);Report->SetNumberField(TEXT("cache_file_bytes"),double(CacheBytes));Report->SetNumberField(TEXT("cache_load_ms"),CacheLoadMs);Report->SetNumberField(TEXT("cache_invalid_inputs_rejected"),CacheRejected);Report->SetBoolField(TEXT("cache_only_loader"),CachePassed);Report->SetBoolField(TEXT("scene_load_evidence"),false);
 Report->SetBoolField(TEXT("batch_palette_matches_legacy_and_reset_history"),SkinPalette&&Passed);Report->SetNumberField(TEXT("batch_invalid_scatter_rejected_per_phase"),SkinPalette?3:0);Report->SetBoolField(TEXT("skin_palette"),SkinPalette);Report->SetNumberField(TEXT("palette_bones"),PaletteData.Items.Num());Report->SetNumberField(TEXT("palette_sections"),SectionCount);Report->SetNumberField(TEXT("palette_compared_elements"),double(PaletteElements));Report->SetNumberField(TEXT("palette_max_error"),PaletteMaximum);Report->SetNumberField(TEXT("palette_max_vertex_error_cm"),PaletteVertexMaximum);Report->SetBoolField(TEXT("palette_immutable_stable"),PaletteStable);Report->SetNumberField(TEXT("palette_immutable_bytes"),double(PaletteImmutable));Report->SetNumberField(TEXT("palette_initializations"),double(PaletteStats.Initializations));Report->SetNumberField(TEXT("palette_passes"),double(PaletteStats.Passes));
 if(Resident){Passed&=ImmutableStable&&HistoryMaximum<=Tolerance&&ResidentStats.Evaluations==UE_ARRAY_COUNT(Phases)&&ResidentStats.ParameterUploadBytes==uint64(Batch.Actors.Num())*sizeof(FVector4f)*UE_ARRAY_COUNT(Phases);ENQUEUE_RENDER_COMMAND(ReleaseRealResident)([&](FRHICommandListImmediate&){ResidentPose.Release();});FlushRenderingCommands();}
 Report->SetBoolField(TEXT("resident"),Resident);Report->SetBoolField(TEXT("immutable_uploads_stable"),ImmutableStable);Report->SetBoolField(TEXT("history_passed"),Finite&&HistoryMaximum<=Tolerance);Report->SetNumberField(TEXT("previous_max_error"),HistoryMaximum);Report->SetNumberField(TEXT("initializations"),double(ResidentStats.Initializations));Report->SetNumberField(TEXT("evaluations"),double(ResidentStats.Evaluations));Report->SetNumberField(TEXT("immutable_upload_bytes"),double(ImmutableBytes));Report->SetNumberField(TEXT("parameter_upload_bytes"),double(ResidentStats.ParameterUploadBytes));Report->SetBoolField(TEXT("local_pose_diagnostic_run"),!Resident);
 Report->SetNumberField(TEXT("compared_editable_low_vertices"),double(ComparedVertices));Report->SetNumberField(TEXT("max_vertex_error_cm"),VertexMaximum);Report->SetNumberField(TEXT("vertex_tolerance_cm"),0.01);Report->SetBoolField(TEXT("passed"),Passed);Report->SetBoolField(TEXT("finite"),Finite);Report->SetNumberField(TEXT("max_error"),Maximum);Report->SetNumberField(TEXT("rms_error"),Elements?FMath::Sqrt(Squared/Elements):0);Report->SetNumberField(TEXT("tolerance"),Tolerance);Report->SetNumberField(TEXT("compared_elements"),double(Elements));Report->SetNumberField(TEXT("local_max_error"),LocalMaximum);Report->SetStringField(TEXT("worst_bone"),BoneNames.IsValidIndex(WorstTask)?BoneNames[WorstTask]:TEXT(""));Report->SetNumberField(TEXT("worst_task"),WorstTask);Report->SetNumberField(TEXT("worst_phase"),WorstPhase);Report->SetStringField(TEXT("error"),Error);Report->SetStringField(TEXT("reference"),TEXT("Existing compressed codec + native FTransform hierarchy; component poses with identity bind; editable low vertices use actual per-mesh inverse binds/weights; no retarget/root-motion extraction/morph/near-part/render validation"));
 FString Json;FJsonSerializer::Serialize(Report,TJsonWriterFactory<>::Create(&Json));return FFileHelper::SaveStringToFile(Json,*Output)&&Passed;
}
