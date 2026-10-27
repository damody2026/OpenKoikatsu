#include "KKRuntimePoseBankTypes.h"
#include "KKRuntimeGpuDiskCache.h"
#include "KKRuntimeNativeSession.h"
#include "KKRuntimePoseMath.h"
#include "Async/Async.h"
#include "ok_extract.h"
#include "Misc/SecureHash.h"
#include "Serialization/BufferArchive.h"

static FKKRuntimePoseResult OKPrepareRuntimePoseBankNow(FKKRuntimePoseRequest& R){
 {
  const double Start=FPlatformTime::Seconds();FKKRuntimePoseResult Out;Out.WorkerThread=!IsInGameThread();
  Out.CharacterIdentity=R.CharacterIdentity;Out.ClipIdentity=R.ClipIdentity;Out.Duration=R.Duration;
  auto Fail=[&](const TCHAR* Why){Out.Error=Why;Out.Keys.Reset();Out.Tracks.Reset();Out.PrepareMs=(FPlatformTime::Seconds()-Start)*1000;return MoveTemp(Out);};
  if(R.CharacterIdentity.IsEmpty()||R.ClipIdentity.IsEmpty()||R.Snapshot.IsEmpty()||R.AuthoredCurves.IsEmpty()||R.Bones.IsEmpty()||R.Bones.Num()>65536||R.SampleHz!=60||!FMath::IsFinite(R.Duration)||R.Duration<=0||R.Duration>34.f)return Fail(TEXT("Invalid runtime private pose request"));
  const uint32 Frames=FMath::CeilToInt(R.Duration*R.SampleHz),Samples=Frames+1;
  const uint64 KeyRows=uint64(R.Bones.Num())*Samples*3,Bytes=KeyRows*sizeof(FVector4f);
  if(!R.MaxKeyBytes||Bytes>R.MaxKeyBytes||KeyRows>MAX_int32)return Fail(*FString::Printf(TEXT("Runtime pose keys exceed preallocation budget: %d bones x %u samples = %llu MB (budget %llu MB), clip %s"),R.Bones.Num(),Samples,Bytes>>20,R.MaxKeyBytes>>20,*R.ClipIdentity));
  Out.PreparationIdentity=OKRuntimePoseRecipeIdentity(R);
  FKKRuntimeNativeSession Session;if(!Session.OpenAnimationSnapshot(R.Snapshot)||!Session.InstallSharedClip(R.AuthoredCurves))return Fail(TEXT("Runtime snapshot/curve contract rejected"));
  const float AuthoredDuration=Session.AnimDuration();
  if(!FMath::IsFinite(AuthoredDuration)||AuthoredDuration<=0||(R.AuthoredPrefix?R.Duration>AuthoredDuration+1e-6f:!FMath::IsNearlyEqual(AuthoredDuration,R.Duration,1e-6f))||!Session.ApplyAnimFrame(0,0))return Fail(TEXT("Runtime snapshot/curve duration contract rejected"));
  // The solved first frame keys the disk cache (FirstFrameDigest): the APNG's identity alone let a bank solved by
  // another build or library, with another bust, be drawn on this mesh (a pose bank's palettes carry the shape).
  {FSHA1 H;for(int32 B=0;B<Session.SkelBoneCount();++B){OkBoneView V{};if(!Session.SkelBoneAt(B,V))return Fail(TEXT("Runtime native pose missing"));
    H.Update(reinterpret_cast<const uint8*>(V.pose_pos),sizeof(V.pose_pos));H.Update(reinterpret_cast<const uint8*>(V.pose_rot),sizeof(V.pose_rot));H.Update(reinterpret_cast<const uint8*>(V.pose_scl),sizeof(V.pose_scl));}
   OkSkinPaletteView Raw{};TArray<float> Matrices;
   for(const auto& Part:R.Parts){if(!Part.RigidFollow.IsEmpty()){float World[16];if(!Session.BoneWorldMatrix(Part.RigidFollow,World))return Fail(TEXT("Runtime rigid attachment bone missing"));H.Update(reinterpret_cast<const uint8*>(World),sizeof(World));continue;}
    if(Session.SkinPalette(Part.NativePart,Raw,Matrices)<=0)return Fail(TEXT("Runtime native palette missing"));H.Update(reinterpret_cast<const uint8*>(Matrices.GetData()),Matrices.Num()*sizeof(float));}
   H.Final();FSHAHash Digest;H.GetHash(Digest.Hash);R.FirstFrameDigest=Digest.ToString();}
  if(R.PackedGpuCache&&OKLoadRuntimeGpuPoseCache(R,Out)){Out.PrepareMs=(FPlatformTime::Seconds()-Start)*1000;return Out;}
  struct FMapping {int32 Native=INDEX_NONE,Part=INDEX_NONE,Slot=0,Axis=0,SlotIndex=INDEX_NONE;};
  TMap<FName,int32> Native;for(int32 B=0;B<Session.SkelBoneCount();++B){OkBoneView V{};if(!Session.SkelBoneAt(B,V)||!V.name)return Fail(TEXT("Native runtime bone unavailable"));Native.Add(FName(UTF8_TO_TCHAR(V.name)),B);}
  TMap<FName,FMapping> Palettes;TSet<int32> Parts;
  for(int32 P=0;P<R.Parts.Num();++P){const auto& Part=R.Parts[P];if(Part.NativePart<0||Parts.Contains(Part.NativePart)||Part.RigidBindWorld.ContainsNaN())return Fail(TEXT("Invalid runtime palette part identity"));Parts.Add(Part.NativePart);
   for(int32 SI=0;SI<Part.Slots.Num();++SI)for(int32 Axis=0;Axis<3;++Axis){const int32 Slot=Part.Slots[SI];const FName Name=OKPaletteBoneName(Part.NativePart,Slot,Axis);if(Palettes.Contains(Name))return Fail(TEXT("Duplicate runtime palette slot"));FMapping M;M.Part=P;M.Slot=Slot;M.SlotIndex=SI;M.Axis=Axis;Palettes.Add(Name,M);}
  }
  TArray<FMapping> Mapping;TArray<FQuat> Previous;TSet<FName> Names;
  for(int32 B=0;B<R.Bones.Num();++B){const auto& Bone=R.Bones[B];if(Bone.Name.IsNone()||Names.Contains(Bone.Name)||Bone.Parent<INDEX_NONE||Bone.Parent>=B||Bone.Reference.ContainsNaN())return Fail(TEXT("Invalid runtime private hierarchy"));Names.Add(Bone.Name);FMapping M;
   if(const auto* Palette=Palettes.Find(Bone.Name))M=*Palette;
   else if(const auto* N=Native.Find(Bone.Name))M.Native=*N;
   else if(B!=0||Bone.Name!=TEXT("__ok_scene")||Bone.Parent!=INDEX_NONE)return Fail(TEXT("Runtime private bone absent from snapshot"));
   Mapping.Add(M);Previous.Add(Bone.Reference.GetRotation());
  }
  Out.Keys.Reserve(int32(KeyRows));Out.Keys.SetNumUninitialized(int32(KeyRows));Out.Samples=Samples;
  // Task-private storage survives samples. Raw native views are refreshed after
  // every animation frame; only copied matrices and capacity are retained.
  struct FPartFrame {OkSkinPaletteView Raw{};TArray<float> Matrices;TArray<FMatrix> Slots;TArray<uint8> SlotReady;FMatrix Rigid=FMatrix::Identity,InverseBind=FMatrix::Identity;bool Ready=false;};
  TArray<FPartFrame> PartFrames;PartFrames.SetNum(R.Parts.Num());
  for(int32 P=0;P<R.Parts.Num();++P){auto& F=PartFrames[P];F.Slots.SetNum(R.Parts[P].Slots.Num());F.SlotReady.SetNumZeroed(F.Slots.Num());if(!R.Parts[P].RigidFollow.IsEmpty())F.InverseBind=R.Parts[P].RigidBindWorld.Inverse();}
  TArray<uint8> Contact;if(!R.ContactRecipe.IsEmpty()){FTCHARToUTF8 Utf8(*R.ContactRecipe);Contact.Append(reinterpret_cast<const uint8*>(Utf8.Get()),Utf8.Length());if(Contact.Num()>16384)return Fail(TEXT("Contact recipe exceeds limit"));}
  const float Step=R.Duration/Frames;
  for(uint32 K=0;K<Samples;++K){const float Time=K==Frames?R.Duration:Step*K;if(Contact.IsEmpty()?(K&&!Session.ApplyAnimFrame(Time,Step)):!Session.ApplyContactFrame(Time,K?Step:0,Contact))return Fail(TEXT("Runtime authored frame evaluation failed"));
   for(auto& F:PartFrames){F.Ready=false;F.Raw={};if(!F.SlotReady.IsEmpty())FMemory::Memzero(F.SlotReady.GetData(),F.SlotReady.Num());}
   for(int32 B=0;B<Mapping.Num();++B){const auto& M=Mapping[B];FTransform T=R.Bones[B].Reference;
    if(M.Native!=INDEX_NONE){OkBoneView V{};if(!Session.SkelBoneAt(M.Native,V))return Fail(TEXT("Runtime native pose missing"));T=OKUnityLocalToUnreal(V.pose_pos,V.pose_rot,V.pose_scl);}
    else if(M.Part!=INDEX_NONE&&R.NativeOnly&&K>0){const int32 First=B*Samples*3,I=(B*Samples+K)*3;Out.Keys[I]=FVector4f(Out.Keys[First].X,Out.Keys[First].Y,Out.Keys[First].Z,Time);Out.Keys[I+1]=Out.Keys[First+1];Out.Keys[I+2]=Out.Keys[First+2];continue;}
    else if(M.Part!=INDEX_NONE){auto& F=PartFrames[M.Part];const auto& Part=R.Parts[M.Part];if(!F.Ready){
      if(Part.RigidFollow.IsEmpty()){if(Session.SkinPalette(Part.NativePart,F.Raw,F.Matrices)<=0)return Fail(TEXT("Runtime native palette missing"));}
      else {float World[16];if(!Session.BoneWorldMatrix(Part.RigidFollow,World))return Fail(TEXT("Runtime rigid attachment bone missing"));F.Rigid=F.InverseBind*OKSkinPaletteToUnreal(World);}F.Ready=true;}
     if(!F.SlotReady[M.SlotIndex]){auto& Matrix=F.Slots[M.SlotIndex];Matrix=F.Rigid;if(Part.RigidFollow.IsEmpty()&&!OKExactPaletteMatrix(F.Raw,F.Matrices,M.Slot,Matrix))return Fail(TEXT("Runtime signed palette slot missing"));F.SlotReady[M.SlotIndex]=1;}
     T=OKSkinPaletteColumnContinuous(F.Slots[M.SlotIndex],M.Axis,Previous[B]);
    }
    if(T.ContainsNaN())return Fail(TEXT("Runtime pose is nonfinite"));FQuat Q=T.GetRotation().GetNormalized();if((Q|Previous[B])<0)Q=Q*-1.;Previous[B]=Q;
    const auto P=T.GetTranslation(),S=T.GetScale3D();const int32 I=(B*Samples+K)*3;
    Out.Keys[I]=FVector4f(P.X,P.Y,P.Z,Time);Out.Keys[I+1]=FVector4f(Q.X,Q.Y,Q.Z,Q.W);Out.Keys[I+2]=FVector4f(S.X,S.Y,S.Z,0);
   }
  }
  // Collapse only bitwise identical channels; no lossy key simplification.
  Out.Tracks.Reserve(R.Bones.Num());
  int32 Write=0;for(int32 B=0;B<R.Bones.Num();++B){const int32 Source=B*Samples*3;bool Constant=true;
   for(uint32 K=1;K<Samples&&Constant;++K){const auto& P=Out.Keys[Source+K*3];const auto& P0=Out.Keys[Source];Constant=P.X==P0.X&&P.Y==P0.Y&&P.Z==P0.Z&&Out.Keys[Source+K*3+1]==Out.Keys[Source+1]&&Out.Keys[Source+K*3+2]==Out.Keys[Source+2];}
   const uint32 Count=Constant?1:Samples;Out.ConstantTracks+=Constant?1:0;Out.Tracks.Add(FUintVector4(Write/3,Count,R.Bones[B].Parent<0?MAX_uint32:uint32(R.Bones[B].Parent),Mapping[B].Part!=INDEX_NONE?(33u|(uint32(Mapping[B].Axis)<<6)):1u));
   for(uint32 K=0;K<Count;++K)for(int32 C=0;C<3;++C)Out.Keys[Write++]=Out.Keys[Source+K*3+C];
  }
  Out.Keys.SetNum(Write,EAllowShrinking::Yes);FString PackingError;if(R.PackedGpuCache&&!OKPackAndSaveRuntimeGpuPoseCache(R,Out,PackingError))return Fail(*PackingError);Out.PrepareMs=(FPlatformTime::Seconds()-Start)*1000;return Out;
 }
}
TFuture<FKKRuntimePoseResult> OKPrepareRuntimePoseBankAsync(FKKRuntimePoseRequest R){
 return Async(EAsyncExecution::ThreadPool,[R=MoveTemp(R)]() mutable {return OKPrepareRuntimePoseBankNow(R);});
}
TFuture<FKKRuntimePoseResult> OKPrepareRuntimePoseBankAsync(TFunction<FKKRuntimePoseRequest()> Make){
 return Async(EAsyncExecution::ThreadPool,[Make=MoveTemp(Make)]() mutable {FKKRuntimePoseRequest R=Make();return OKPrepareRuntimePoseBankNow(R);});
}

FString OKRuntimePoseRecipeIdentity(const FKKRuntimePoseRequest& R){
 FSHA1 H;auto Feed=[&](const TArray<uint8>& B){const uint64 N=B.Num();H.Update(reinterpret_cast<const uint8*>(&N),sizeof(N));if(N)H.Update(B.GetData(),N);};if(R.PackedGpuCache&&!R.PersistentSourceIdentity.IsEmpty()){FTCHARToUTF8 Stable(*R.PersistentSourceIdentity);TArray<uint8> Source;Source.Append(reinterpret_cast<const uint8*>(Stable.Get()),Stable.Length());Feed(Source);}else Feed(R.Snapshot);Feed(R.AuthoredCurves);
 FBufferArchive Metadata;uint32 Version=4,Hz=R.SampleHz;float Duration=R.Duration;Metadata<<Version<<Hz<<Duration;bool Packed=R.PackedGpuCache;Metadata<<Packed;
 if(R.AuthoredPrefix){uint32 PrefixVersion=1;FString Mode=TEXT("authored-prefix");Metadata<<Mode<<PrefixVersion;}
 if(R.NativeOnly){uint32 NativeVersion=1;FString Mode=TEXT("native-only");Metadata<<Mode<<NativeVersion;}
 int32 N=R.Bones.Num();Metadata<<N;for(auto B:R.Bones){FString Name=B.Name.ToString();Metadata<<Name<<B.Parent<<B.Reference;}
 if(!R.ContactRecipe.IsEmpty()){uint32 ContactVersion=1;Metadata<<ContactVersion;FString Contact=R.ContactRecipe;Metadata<<Contact;}
 N=R.Parts.Num();Metadata<<N;for(auto P:R.Parts)Metadata<<P.NativePart<<P.Slots<<P.RigidFollow<<P.RigidBindWorld;Feed(Metadata);H.Final();FSHAHash Hash;H.GetHash(Hash.Hash);return Hash.ToString();
}
