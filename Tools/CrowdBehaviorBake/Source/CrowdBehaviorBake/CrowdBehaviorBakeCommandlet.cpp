#include "CrowdBehaviorBakeCommandlet.h"
#include "KKRustExtract.h"
#include "KKExactSkinning.h"
#include "KKSkeletalBuilder.h"
#include "KKAnimationImporter.h"
#include "KKImportedCharacter.h"
#include "KKImportedAnimSequence.h"
#include "KKSharedAnimation.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "Animation/AnimData/IAnimationDataController.h"
#include "Engine/SkeletalMesh.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/SavePackage.h"
#include "Misc/PackageName.h"
#include "Misc/FileHelper.h"
#include "Json.h"
#include "ok_extract.h"
#include "Modules/ModuleManager.h"
bool RunRuntimePoseAudit(const FString&,const FString&);
namespace {
int Fail(int Line){UE_LOG(LogTemp,Error,TEXT("Private snapshot behavior bake rejected at line %d"),Line);return 1;} 
bool Save(UObject* A){const auto File=FPackageName::LongPackageNameToFilename(A->GetOutermost()->GetName(),FPackageName::GetAssetPackageExtension());if(IFileManager::Get().FileExists(*File))return false;IFileManager::Get().MakeDirectory(*FPaths::GetPath(File),true);FSavePackageArgs Args;Args.TopLevelFlags=RF_Public|RF_Standalone;return UPackage::SavePackage(A->GetOutermost(),A,*File,Args);}
struct FClothPolicy {FString Previous=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_NO_CLOTH_DYNAMICS"));FClothPolicy(){FPlatformMisc::SetEnvironmentVar(TEXT("OK_NO_CLOTH_DYNAMICS"),TEXT("1"));}~FClothPolicy(){FPlatformMisc::SetEnvironmentVar(TEXT("OK_NO_CLOTH_DYNAMICS"),*Previous);}};
struct FTrack {FName Bone;int32 Native=INDEX_NONE,Part=INDEX_NONE,Slot=0,Axis=0;TArray<FVector> P,S;TArray<FQuat> Q;};
}
UCrowdBehaviorBakeCommandlet::UCrowdBehaviorBakeCommandlet(){IsEditor=true;IsClient=false;IsServer=false;LogToConsole=true;}
int32 UCrowdBehaviorBakeCommandlet::Main(const FString& Params){
 FString Input,Output,Root,SharedRoot,Source=TEXT("studio/anime/00.unity3d"),ClipText;
 FParse::Value(*Params,TEXT("Input="),Input);FParse::Value(*Params,TEXT("Output="),Output);FParse::Value(*Params,TEXT("GameRoot="),Root);FParse::Value(*Params,TEXT("SharedRoot="),SharedRoot);FParse::Value(*Params,TEXT("Clips="),ClipText);
 if(FParse::Param(*Params,TEXT("ExportRuntimeCurve"))){
  FParse::Value(*Params,TEXT("Source="),Source);
  if(Root.IsEmpty()||ClipText.IsEmpty()||Output.IsEmpty()||IFileManager::Get().FileExists(*Output))return Fail(__LINE__);
  const FString Key=Source+TEXT("|")+ClipText;FTCHARToUTF8 NativeRoot(*Root),NativeKey(*Key);
  const int32 Size=ok_export_shared_clip(NativeRoot.Get(),NativeKey.Get(),nullptr,0);if(Size<=0||Size>128*1024*1024)return Fail(__LINE__);
  TArray<uint8> Bytes;Bytes.SetNumUninitialized(Size);if(ok_export_shared_clip(NativeRoot.Get(),NativeKey.Get(),Bytes.GetData(),Size)!=Size)return Fail(__LINE__);
  return FFileHelper::SaveArrayToFile(Bytes,*Output)?0:1;
 }
 if(FParse::Param(*Params,TEXT("RuntimePoseAudit")))return RunRuntimePoseAudit(Input,Output)?0:1;
 if(Input.IsEmpty()||Output.IsEmpty()||IFileManager::Get().FileExists(*Output)||SharedRoot.IsEmpty())return Fail(__LINE__);
 FString Text;TSharedPtr<FJsonObject> Request;if(!FFileHelper::LoadFileToString(Text,*Input)||!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Request)||!Request)return Fail(__LINE__);
 TArray<FString> Clips;ClipText.ParseIntoArray(Clips,TEXT(";"),true);if(Clips.IsEmpty())return Fail(__LINE__);
 TArray<UKKSharedAnimation*> Shared;TArray<TStrongObjectPtr<UKKSharedAnimation>> SharedHolds;FString Error;
 if(FPackageName::DoesPackageExist(SharedRoot/TEXT("Shared_0000"))){for(int32 I=0;I<Clips.Num();++I){auto* A=LoadObject<UKKSharedAnimation>(nullptr,*(SharedRoot/FString::Printf(TEXT("Shared_%04d.Shared_%04d"),I,I)));if(!A||A->Source!=Source||A->Clip!=Clips[I])return Fail(__LINE__);Shared.Add(A);}}
 else if(!KKAnimationImporter::ImportShared(Root,Source,Clips,SharedRoot,Shared,Error)){UE_LOG(LogTemp,Error,TEXT("Behavior curve import: %s"),*Error);return Fail(__LINE__);}
 for(auto* A:Shared)SharedHolds.Emplace(A);
 const TArray<TSharedPtr<FJsonValue>>* Rows=nullptr;if(!Request->TryGetArrayField(TEXT("libraries"),Rows)||Rows->IsEmpty())return Fail(__LINE__);
 TArray<TSharedPtr<FJsonValue>> Receipts;FKKScopedNativeSkinSource NativeSource;FClothPolicy Policy;
 for(const auto& Value:*Rows){CollectGarbage(RF_NoFlags);auto Row=Value->AsObject();if(!Row)return Fail(__LINE__);const FString Destination=Row->GetStringField(TEXT("destination"));
  if(!Destination.StartsWith(TEXT("/Game/Scenes/CrowdRoom/Characters/"))||!FPackageName::IsValidLongPackageName(Destination)||FPackageName::DoesPackageExist(Destination/TEXT("Animations")))return Fail(__LINE__);
  TStrongObjectPtr<UKKImportedCharacter> Native(LoadObject<UKKImportedCharacter>(nullptr,*Row->GetStringField(TEXT("source_character"))));TStrongObjectPtr<UKKImportedAnimationLibrary> Walk(LoadObject<UKKImportedAnimationLibrary>(nullptr,*Row->GetStringField(TEXT("walk"))));
  if(!Native||Native->Schema!=2||Native->Outfits.IsEmpty()||!Walk||!Walk->Character||Walk->Animations.IsEmpty()||Walk->Character->Schema!=1017||Walk->Character->SourceHash!=Native->SourceHash)return Fail(__LINE__);
  auto* Character=Walk->Character.Get();const auto& Outfit=Native->Outfits[0];auto* Template=Walk->Animations[0].Sequence.Get();if(!Template||Template->GetSkeleton()!=Outfit.Skeleton||Outfit.AnimationSnapshot.IsEmpty())return Fail(__LINE__);
  const auto& Ref=Outfit.Skeleton->GetReferenceSkeleton();TSet<FName> Required;for(const auto& Part:Character->Outfits[0].Parts)if(Part.SkinnedModel)for(const auto& Info:Part.SkinnedModel->GetRefSkeleton().GetRefBoneInfo())Required.Add(Info.Name);
  auto* Library=NewObject<UKKImportedAnimationLibrary>(CreatePackage(*(Destination/TEXT("Animations"))),TEXT("Animations"),RF_Public|RF_Standalone);TStrongObjectPtr<UKKImportedAnimationLibrary> Hold(Library);Library->Character=Character;
  TArray<TStrongObjectPtr<UAnimSequence>> Sequences;double MaxError=0;int32 TotalTracks=0;
  for(int32 ClipIndex=0;ClipIndex<Shared.Num();++ClipIndex){FOkSession Session;
   if(!Session.OpenAnimationSnapshot(Outfit.AnimationSnapshot)||!Session.ApplyShape(Session.ShapeBody(),Session.ShapeFace())||!Session.InstallSharedClip(Shared[ClipIndex]->AuthoredCurves)||!Session.ApplyAnimFrame(0,0))return Fail(__LINE__);
   TMap<FName,int32> NativeBones;for(int32 B=0;B<Session.SkelBoneCount();++B){OkBoneView V{};if(!Session.SkelBoneAt(B,V)||!V.name)return Fail(__LINE__);NativeBones.Add(FName(UTF8_TO_TCHAR(V.name)),B);}
   TArray<FTrack> Tracks;
   for(FName Bone:Required){FTrack T;T.Bone=Bone;if(auto* N=NativeBones.Find(Bone))T.Native=*N;else if(Bone==TEXT("__ok_scene"))continue;else if(!Bone.ToString().StartsWith(TEXT("__ok_palette_"))){UE_LOG(LogTemp,Error,TEXT("Snapshot misses required private bone %s"),*Bone.ToString());return Fail(__LINE__);}else continue;Tracks.Add(MoveTemp(T));}
   for(const auto& Part:Outfit.Parts)for(int32 Slot:Part.PaletteSlots)for(int32 Axis=0;Axis<3;++Axis){const auto Bone=OKPaletteBoneName(Part.NativePartIndex,Slot,Axis);if(!Required.Contains(Bone))continue;FTrack T;T.Bone=Bone;T.Part=Part.NativePartIndex;T.Slot=Slot;T.Axis=Axis;Tracks.Add(MoveTemp(T));}
   for(FName Bone:Required)if(Bone!=TEXT("__ok_scene")&&!Tracks.ContainsByPredicate([&](const auto& T){return T.Bone==Bone;}))return Fail(__LINE__);
   const double Duration=Shared[ClipIndex]->Duration;const int32 Frames=FMath::Max(1,FMath::CeilToInt(Duration*60));if(Frames>2047||int64(Frames+1)*Tracks.Num()>10000000)return Fail(__LINE__);
   const FFrameRate Rate(FMath::RoundToInt(Frames/Duration*1000000),1000000);const double Step=Rate.AsInterval();
   TMap<int32,TArray<int32>> NeededSlots;for(const auto& T:Tracks)if(T.Part!=INDEX_NONE)NeededSlots.FindOrAdd(T.Part).AddUnique(T.Slot);
   for(int32 Frame=0;Frame<=Frames;++Frame){if(!Session.ApplyAnimFrame(Frame*Step,Frame?Step:0))return Fail(__LINE__);TMap<int32,TArray<FMatrix>> Palettes;
    for(auto& T:Tracks){FTransform Pose;
     if(T.Native!=INDEX_NONE){OkBoneView V{};if(!Session.SkelBoneAt(T.Native,V))return Fail(__LINE__);Pose=OKUnityLocalToUnreal(V.pose_pos,V.pose_rot,V.pose_scl);}
     else {if(!Palettes.Contains(T.Part)){const auto* Part=Outfit.Parts.FindByPredicate([&](const auto& P){return P.NativePartIndex==T.Part;});if(!Part)return Fail(__LINE__);TArray<FMatrix> Values;
       if(Part->RigidFollow.IsEmpty()){OkSkinPaletteView Raw{};TArray<float> Matrices;if(Session.SkinPalette(T.Part,Raw,Matrices)<=0)return Fail(__LINE__);for(int32 Slot:NeededSlots[T.Part]){FMatrix M;if(!OKExactPaletteMatrix(Raw,Matrices,Slot,M))return Fail(__LINE__);Values.Add(M);}}
       else {float World[16];if(Part->RigidReference.Num()!=16||!Session.BoneWorldMatrix(Part->RigidFollow,World))return Fail(__LINE__);Values.Add(OKSkinPaletteToUnreal(Part->RigidReference.GetData()).Inverse()*OKSkinPaletteToUnreal(World));}
       Palettes.Add(T.Part,MoveTemp(Values));}
      const auto* Part=Outfit.Parts.FindByPredicate([&](const auto& P){return P.NativePartIndex==T.Part;});const int32 Slot=NeededSlots[T.Part].IndexOfByKey(T.Slot);if(!Palettes[T.Part].IsValidIndex(Slot))return Fail(__LINE__);const auto& M=Palettes[T.Part][Slot];Pose=T.Q.IsEmpty()?OKSkinPaletteColumn(M,T.Axis):OKSkinPaletteColumnContinuous(M,T.Axis,T.Q.Last());}
     if(Pose.ContainsNaN())return Fail(__LINE__);auto Q=Pose.GetRotation();Q.Normalize();if(!T.Q.IsEmpty()&&(Q|T.Q.Last())<0)Q=Q*-1;T.P.Add(Pose.GetTranslation());T.Q.Add(Q);T.S.Add(Pose.GetScale3D());
    }
   }
   const FString Path=Destination/FString::Printf(TEXT("Action_%04d"),ClipIndex);auto* Sequence=NewObject<UKKImportedAnimSequence>(CreatePackage(*Path),*FPackageName::GetLongPackageAssetName(Path),RF_Public|RF_Standalone);Sequence->SetSkeleton(Template->GetSkeleton());Sequence->BoneCompressionSettings=Template->BoneCompressionSettings;if(!Sequence)return Fail(__LINE__);Sequences.Emplace(Sequence);Sequence->SetPreviewMesh(nullptr);
   auto& C=Sequence->GetController();C.OpenBracket(NSLOCTEXT("OK","BehaviorBake","Bake private snapshot behavior"),false);C.InitializeModel();C.SetFrameRate(Rate,false);C.SetNumberOfFrames(FFrameNumber(Frames),false);
   for(const auto& T:Tracks)if(!C.AddBoneCurve(T.Bone,false)||!C.SetBoneTrackKeys(T.Bone,T.P,T.Q,T.S,false))return Fail(__LINE__);C.NotifyPopulated();C.CloseBracket(false);Sequence->PostEditChange();Sequence->CacheDerivedDataForCurrentPlatform();Sequence->WaitOnExistingCompression();
   for(int32 Frame=0;Frame<=Frames;++Frame){FAnimExtractContext Context(Frame*Step);Context.bEnforceCompressedDataSampling=true;for(const auto& T:Tracks){FTransform P;Sequence->GetBoneTransform(P,FSkeletonPoseBoneIndex(Ref.FindBoneIndex(T.Bone)),Context,false);MaxError=FMath::Max(MaxError,FVector::Distance(P.GetTranslation(),T.P[Frame]));if(P.ContainsNaN()||MaxError>.01||!P.GetScale3D().Equals(T.S[Frame],.001)||P.GetRotation().AngularDistance(T.Q[Frame])>.001)return Fail(__LINE__);}}
   FKKImportedAnimation Entry;Entry.Source=Source;Entry.Clip=Shared[ClipIndex]->Clip;Entry.Sequence=Sequence;Entry.SourceDuration=Duration;Entry.Outfit=0;Entry.Loop=Shared[ClipIndex]->Loop;Library->Animations.Add(Entry);TotalTracks+=Tracks.Num();
   UE_LOG(LogTemp,Display,TEXT("Snapshot behavior baked character=%s clip=%s frames=%d tracks=%d max_error_cm=%g"),*Character->GetPathName(),*Entry.Clip,Frames,Tracks.Num(),MaxError);
  }
  for(const auto& Sequence:Sequences)if(!Save(Sequence.Get()))return Fail(__LINE__);if(!Save(Library))return Fail(__LINE__);
  auto Receipt=MakeShared<FJsonObject>();Receipt->SetStringField(TEXT("library"),Library->GetPathName());Receipt->SetNumberField(TEXT("clips"),Library->Animations.Num());Receipt->SetNumberField(TEXT("tracks"),TotalTracks);Receipt->SetNumberField(TEXT("max_compressed_position_error_cm"),MaxError);Receipts.Add(MakeShared<FJsonValueObject>(Receipt));
 }
 auto Report=MakeShared<FJsonObject>();Report->SetBoolField(TEXT("passed"),true);Report->SetBoolField(TEXT("source_assets_modified"),false);Report->SetBoolField(TEXT("clothing_springs"),false);Report->SetArrayField(TEXT("libraries"),Receipts);FString Json;return FJsonSerializer::Serialize(Report,TJsonWriterFactory<>::Create(&Json))&&FFileHelper::SaveStringToFile(Json,*Output)?0:1;
}
IMPLEMENT_PRIMARY_GAME_MODULE(FDefaultGameModuleImpl,CrowdBehaviorBake,"CrowdBehaviorBake");
