#include "KKImportSkeletalAnimationsCommandlet.h"
#include "KKImportDestination.h"
#include "Misc/AutomationTest.h"

#include "KKAnimationImporter.h"
#include "KKImportedCharacter.h"
#include "KKImportedAnimSequence.h"
#include "Animation/AnimData/AnimDataModel.h"
#include "KKRustExtract.h"
#include "KKCardSource.h"
#include "KKExactSkinning.h"
#include "KKPreviewActor.h"
#include "ProceduralMeshComponent.h"
#include "Serialization/JsonSerializer.h"
#include "KKSkeletalBuilder.h"
#include "ok_extract.h"
#include <cmath>
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "Animation/AnimCompressionTypes.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Rendering/SkeletalMeshModel.h"
#include "SkeletalMeshAttributes.h"
#include "StaticMeshAttributes.h"
#include "Engine/StaticMesh.h"
#include "Misc/FileHelper.h"
#include "AssetCompilingManager.h"
#include "SkeletalRenderPublic.h"
#include "Misc/Paths.h"
#include "Misc/PackageName.h"
#include "Misc/SecureHash.h"
#if WITH_EDITOR
#include "Animation/AnimData/IAnimationDataController.h"
#include "Animation/AnimData/IAnimationDataModel.h"
#include "Animation/AnimBoneCompressionSettings.h"
#include "Animation/AnimCompress_BitwiseCompressOnly.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/SavePackage.h"
#include "UObject/Package.h"
#endif
namespace {
double RotationDistance(const FQuat& A,const FQuat& B){
 // UE's SIMD Normalize uses an approximate inverse square root. Remove any
 // residual length from the dot product before acos (including source Q).
 const double Norm=std::sqrt(A.SizeSquared()*B.SizeSquared());
 return Norm>1e-12?2.*std::acos(FMath::Clamp(FMath::Abs(A|B)/Norm,0.,1.)):PI;
}
bool Fail(FString& Error,const FString& Message){Error=Message;return false;}
bool AuditPrivatePalette(FOkSession& Session,int32 Index,TSharedPtr<FJsonObject> Row,FString& Error){
 OkMeshView V{};OkSkinPaletteView Raw{};TArray<float> Matrices;
 const int32 Count=Session.SkinPalette(Index,Raw,Matrices);
 if(Count<=0||!Session.PartAt(Index,V)||Raw.n_verts!=V.n_verts)return Fail(Error,TEXT("Exact palette unavailable: ")+Row->GetStringField(TEXT("source_name")));
 double MaxError=0.,Shear=0.;
 for(int32 B=0;B<Count;++B){const float* M=Matrices.GetData()+B*16;
  for(int32 A=0;A<3;++A)for(int32 C=A+1;C<3;++C){FVector X(M[A],M[4+A],M[8+A]),Y(M[C],M[4+C],M[8+C]);const double D=X.Size()*Y.Size();if(D>1e-12)Shear=FMath::Max(Shear,FMath::Abs(X|Y)/D);}}
 for(int32 I=0;I<Raw.n_verts;++I){const float* P=Raw.positions+I*3;FVector Position=FVector::ZeroVector;double Sum=0.;
  for(int32 K=0;K<4;++K){const double W=Raw.weights[I*4+K];if(W<1e-6)continue;const float* M=Matrices.GetData()+Raw.bone_idx[I*4+K]*16;
   Position+=W*FVector(M[0]*P[0]+M[1]*P[1]+M[2]*P[2]+M[3],M[4]*P[0]+M[5]*P[1]+M[6]*P[2]+M[7],M[8]*P[0]+M[9]*P[1]+M[10]*P[2]+M[11]);Sum+=W;}
  Position/=Sum;const FVector Native(V.positions[I*3],V.positions[I*3+1],V.positions[I*3+2]);const double E=FVector::Distance(Position,Native);
  if(Position.ContainsNaN()||!FMath::IsFinite(E))return Fail(Error,TEXT("Nonfinite palette vertex"));MaxError=FMath::Max(MaxError,E);
 }
 double PreviousError=0.,PreviousShear=0.;Row->TryGetNumberField(TEXT("palette_error_m"),PreviousError);Row->TryGetNumberField(TEXT("palette_shear"),PreviousShear);
 Row->SetNumberField(TEXT("palette_slots"),Count);Row->SetNumberField(TEXT("palette_error_m"),FMath::Max(MaxError,PreviousError));Row->SetNumberField(TEXT("palette_shear"),FMath::Max(Shear,PreviousShear));
 return MaxError<=0.00005||Fail(Error,FString::Printf(TEXT("Native palette mismatch %s %.9fm"),*Row->GetStringField(TEXT("source_name")),MaxError));
}
bool CheckRig(FOkSession& Session,USkeleton* Skeleton,FString& Error,TArray<int32>* Mapping=nullptr,const FKKImportedOutfit* Outfit=nullptr){
 if(!Session.ApplyAnimFrame(0.f,0.f)){Error=TEXT("Cannot initialize source renderer rigs");return false;}
 if(!Skeleton)return Fail(Error,TEXT("Target outfit has no skeleton"));
 const auto& Ref=Skeleton->GetReferenceSkeleton();const int32 Count=Session.SkelBoneCount();
 const bool Exact=Ref.FindBoneIndex(TEXT("__ok_scene"))==0;TSet<FName> Extras;
 if(Exact){if(!Outfit)return Fail(Error,TEXT("Exact rig requires renderer metadata"));Extras.Add(TEXT("__ok_scene"));
  if(Outfit->Parts.ContainsByPredicate([](const FKKImportedPart& P){return P.SkinSourceHashV2.IsEmpty();}))UE_LOG(LogTemp,Warning,TEXT("Legacy source hashes: reimport character to validate private rig/assignment changes"));
  for(const auto& P:Outfit->Parts){OkMeshView V{};if(!P.SkinnedModel||!Session.PartAt(P.NativePartIndex,V))return Fail(Error,TEXT("Missing renderer: ")+P.Name);
   const FString Renderer=V.name?UTF8_TO_TCHAR(V.name):TEXT(""),Role=V.role?UTF8_TO_TCHAR(V.role):TEXT(""),Hash=Session.SkinSourceHash(P.NativePartIndex);
   if(P.NativeRenderer!=Renderer||P.Role!=Role||Hash!=P.SkinSourceHash)return Fail(Error,FString::Printf(TEXT("Source renderer/bind/geometry changed: %s renderer=%s/%s role=%s/%s hash=%s/%s"),*P.Name,*P.NativeRenderer,*Renderer,*P.Role,*Role,*P.SkinSourceHash,*Hash));
   if(!P.SkinSourceHashV2.IsEmpty()&&P.SkinSourceHashV2!=Session.SkinSourceHashV2(P.NativePartIndex))return Fail(Error,TEXT("Source private rig/assignment changed: ")+P.Name);
   if(P.RigidFollow.IsEmpty()){OkSkinPaletteView Raw{};TArray<float> Matrices;const int32 Slots=Session.SkinPalette(P.NativePartIndex,Raw,Matrices);TSet<int32> Used;if(Slots<=0)return Fail(Error,TEXT("Source palette missing"));
    for(int32 I=0;I<Raw.n_verts;++I){if(OKHasSignedSkinVertex(Raw,I))Used.Add(-I-1);else for(int32 K=0;K<4;++K)if(Raw.weights[I*4+K]>=1e-6)Used.Add(Raw.bone_idx[I*4+K]);}TArray<int32> Selected=Used.Array();Selected.Sort();if(Selected!=P.PaletteSlots)return Fail(Error,TEXT("Source skin slots changed"));
   }else{float World[16];if(P.RigidReference.Num()!=16||P.PaletteSlots!=TArray<int32>{0}||P.RigidFollow!=(V.rigid_follow_bone?UTF8_TO_TCHAR(V.rigid_follow_bone):TEXT(""))||!Session.BoneWorldMatrix(P.RigidFollow,World))return Fail(Error,TEXT("Source rigid socket changed"));}
   for(int32 Slot:P.PaletteSlots)for(int32 Axis=0;Axis<3;++Axis)Extras.Add(OKPaletteBoneName(P.NativePartIndex,Slot,Axis));
  }
 }
 if(Count+Extras.Num()!=Ref.GetNum())return Fail(Error,FString::Printf(TEXT("Source/target bone counts differ (%d+%d/%d)"),Count,Extras.Num(),Ref.GetNum()));
 TMap<FName,int32> Source;TArray<OkBoneView> Bones;Bones.SetNum(Count);
 for(int32 B=0;B<Count;++B){auto& V=Bones[B];if(!Session.SkelBoneAt(B,V)||!V.name)return Fail(Error,TEXT("Invalid source bone"));
  const FName Name(UTF8_TO_TCHAR(V.name));if(Source.Contains(Name))return Fail(Error,TEXT("Duplicate source bone name: ")+Name.ToString());Source.Add(Name,B);}
 if(Mapping)Mapping->Init(INDEX_NONE,Ref.GetNum());
 for(int32 B=0;B<Ref.GetNum();++B){if(Extras.Contains(Ref.GetBoneName(B))){if(Ref.GetParentIndex(B)!=(B==0?INDEX_NONE:0)||!Ref.GetRefBonePose()[B].Equals(FTransform::Identity,.0001))return Fail(Error,TEXT("Exact palette reference changed"));continue;}
  const int32* Index=Source.Find(Ref.GetBoneName(B));if(!Index)return Fail(Error,TEXT("Target bone missing from source: ")+Ref.GetBoneName(B).ToString());
  const auto& V=Bones[*Index];if(V.parent< -1||V.parent>=Count)return Fail(Error,TEXT("Invalid source parent"));
  const FName SourceParent=V.parent<0?(Exact?FName(TEXT("__ok_scene")):NAME_None):FName(UTF8_TO_TCHAR(Bones[V.parent].name));
  const FName TargetParent=Ref.GetParentIndex(B)<0?NAME_None:Ref.GetBoneName(Ref.GetParentIndex(B));
  if(SourceParent!=TargetParent)return Fail(Error,TEXT("Source/target parent differs: ")+Ref.GetBoneName(B).ToString()+TEXT(" source=")+SourceParent.ToString()+TEXT(" target=")+TargetParent.ToString());
  const FTransform Rest=OKUnityLocalToUnreal(V.rest_pos,V.rest_rot,V.rest_scl);
  if(!Rest.Equals(Ref.GetRefBonePose()[B],0.001))return Fail(Error,TEXT("Source/target bind pose differs: ")+Ref.GetBoneName(B).ToString());
  if(Mapping)(*Mapping)[B]=*Index;
 }
 return true;
}
#if WITH_EDITOR
bool Save(UObject* Asset,FString& Error){
 FAssetRegistryModule::AssetCreated(Asset);Asset->MarkPackageDirty();
 FSavePackageArgs Args;Args.TopLevelFlags=RF_Public|RF_Standalone;Args.SaveFlags=SAVE_NoError;
 const FString Filename=FPackageName::LongPackageNameToFilename(Asset->GetOutermost()->GetName(),FPackageName::GetAssetPackageExtension());
 return UPackage::SavePackage(Asset->GetOutermost(),Asset,*Filename,Args) || Fail(Error,TEXT("Failed saving ")+Filename);
}
struct FTrack {TArray<FVector> Positions,Scales;TArray<FQuat> Rotations;};
UAnimBoneCompressionSettings* ExactPaletteCompression(UAnimSequence* Sequence){
 const FString Name=Sequence->GetName()+TEXT("_Compression");
 auto* Settings=NewObject<UAnimBoneCompressionSettings>(CreatePackage(*(Sequence->GetOutermost()->GetName()+TEXT("_Compression"))),*Name,RF_Public|RF_Standalone);auto* Codec=NewObject<UAnimCompress_BitwiseCompressOnly>(Settings);
 Codec->TranslationCompressionFormat=ACF_None;Codec->RotationCompressionFormat=ACF_None;Codec->ScaleCompressionFormat=ACF_None;
 Settings->Codecs={Codec};Settings->ErrorThreshold=.00001f;Settings->bForceBelowThreshold=true;Sequence->BoneCompressionSettings=Settings;return Settings;
}
bool NativeAnimationAudit(UKKImportedAnimationLibrary* Library,const FString& Card,const FString& Root,FString& Error,bool SharePose=false){
 FKKScopedNativeSkinSource NativeSource;
 if(!Library||!Library->Character||Library->Character->Schema!=2)return Fail(Error,TEXT("Native geometry audit requires a complete schema2 character"));
 const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).CreateNavigation(false).CreateAISystem(false);
 auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
 auto* Actor=World->SpawnActor<AKKImportedCharacterActor>();Actor->Character=Library->Character;Actor->AnimationLibrary=Library;Actor->PlaySkeletalAnimation=true;Actor->ShareBakedPose=SharePose;
 bool Good=true;int64 Compared=0;double Maximum=0.;
 for(int32 C=0;C<Library->Animations.Num()&&Good;++C){const auto& Entry=Library->Animations[C];const auto* Outfit=Library->Character->Outfits.FindByPredicate([&](const auto& O){return O.Index==Entry.Outfit;});FOkSession Native;
  if(!Outfit||!Native.Open(Root,Card,Entry.Outfit,Error)||!CheckRig(Native,Outfit->Skeleton,Error,nullptr,Outfit)){Good=false;break;}
  Native.Skin();TArray<TArray<FVector>> Offsets,Normals;Offsets.SetNum(Outfit->Parts.Num());Normals.SetNum(Outfit->Parts.Num());
  for(int32 P=0;P<Outfit->Parts.Num()&&Good;++P){const auto& Part=Outfit->Parts[P];OkMeshView V{};if(!Native.PartAt(Part.NativePartIndex,V)||!V.positions||!V.normals||V.n_verts<=0){Good=false;Error=TEXT("Missing native geometry");break;}
   const auto* Desc=Part.Model->GetMeshDescription(0);FStaticMeshConstAttributes A(*Desc);Offsets[P].SetNum(Desc->Vertices().Num());Normals[P].SetNum(V.n_verts);
   for(int32 I=0;I<V.n_verts;++I){const auto Instances=Desc->GetVertexVertexInstanceIDs(FVertexID(I));if(Instances.IsEmpty()){Normals[P][I]=FVector::ZeroVector;continue;}Normals[P][I]=FVector(A.GetVertexInstanceNormals()[Instances[0]]).GetSafeNormal();}
   // A section offset is relative to its renderer's main surface, not to a
   // separately sampled default animation pose (which can differ from bind).
   for(auto Id:Desc->Vertices().GetElementIDs()){const int32 I=Id.GetValue()%V.n_verts;Offsets[P][Id.GetValue()]=FVector(A.GetVertexPositions()[Id])-FVector(A.GetVertexPositions()[FVertexID(I)]);}
  }
  if(!Good||!Native.SetAnimClip(Entry.Source+TEXT("|")+Entry.Clip)){Good=false;break;}
  Entry.Sequence->WaitOnExistingCompression();Actor->Outfit=Entry.Outfit;Actor->AnimationIndex=C;Actor->RebuildCharacter();TArray<USkeletalMeshComponent*> Components;Actor->GetComponents(Components);
  if(Components.Num()!=Outfit->Parts.Num()){Good=false;Error=TEXT("Full renderer animation coverage lost");break;}
  if(SharePose){int32 Evaluators=0;for(const auto* Component:Components)if(!Component->LeaderPoseComponent.IsValid())++Evaluators;
   if(!Actor->ConfigureBakedPoseSharing()||Evaluators!=1){Good=false;Error=TEXT("Shared baked pose must use exactly one complete evaluator");break;}
   UE_LOG(LogTemp,Display,TEXT("Native audit shared baked pose evaluators=%d parts=%d"),Evaluators,Components.Num());}
  const auto* Model=Entry.Sequence->GetDataModelInterface().GetInterface();const int32 Frames=Model->GetNumberOfFrames();const double Step=Model->GetFrameRate().AsInterval();
  TSet<int32> Samples;for(int32 K=0;K<=5;++K)Samples.Add(Frames*K/5);
  double ClipMaximum=0.;
  for(int32 F=0;F<=Frames&&Good;++F){const double Time=F*Step;if(!Native.ApplyAnimTime(Time)){Good=false;break;}if(!Samples.Contains(F))continue;
   Native.Skin();if(!Actor->SetAnimationPosition(Time)){Good=false;break;}
   for(int32 P=0;P<Outfit->Parts.Num()&&Good;++P){const auto& Part=Outfit->Parts[P];auto** Match=Components.FindByPredicate([&](auto* Comp){return Comp->GetSkeletalMeshAsset()==Part.SkinnedModel;});OkMeshView V{};
    if(!Match||!Native.PartAt(Part.NativePartIndex,V)){Good=false;break;}TArray<FFinalSkinVertex> Actual;
    // A source card can hide mouth/alternate renderers. UE does not create a
    // MeshObject for these; enable them only in this offscreen geometry audit.
    const bool Visible=(*Match)->IsVisible(),Hidden=(*Match)->bHiddenInGame;
    (*Match)->SetVisibility(true);(*Match)->SetHiddenInGame(false);
    (*Match)->GetCPUSkinnedVertices(Actual,0);
    (*Match)->SetVisibility(Visible);(*Match)->SetHiddenInGame(Hidden);
    const auto& Map=Part.SkinnedModel->GetImportedModel()->LODModels[0].GetRawPointIndices();
    FMatrix Rigid=FMatrix::Identity;if(!Part.RigidFollow.IsEmpty()){float M[16];if(!Native.BoneWorldMatrix(Part.RigidFollow,M)){Good=false;break;}Rigid=OKSkinPaletteToUnreal(Part.RigidReference.GetData()).Inverse()*OKSkinPaletteToUnreal(M);}
    const auto* Desc=Part.Model->GetMeshDescription(0);FStaticMeshConstAttributes Static(*Desc);
    if(Map.Num()!=Actual.Num()){Good=false;Error=TEXT("Missing renderer vertex mapping");break;}
    for(int32 R=0;R<Actual.Num();++R){const int32 Id=Map[R],I=Id%V.n_verts;if(!Offsets[P].IsValidIndex(Id)){Good=false;break;}
     FVector Expected;if(!Part.RigidFollow.IsEmpty())Expected=FVector(Rigid.TransformPosition(FVector(Static.GetVertexPositions()[FVertexID(Id)])));
     else{const FVector Normal(-V.normals[I*3],V.normals[I*3+2],V.normals[I*3+1]);const FQuat Motion=Normals[P][I].IsNearlyZero()||Normal.IsNearlyZero()?FQuat::Identity:FQuat::FindBetweenNormals(Normals[P][I],Normal.GetSafeNormal());Expected=FVector(-V.positions[I*3],V.positions[I*3+2],V.positions[I*3+1])*100.+Motion.RotateVector(Offsets[P][Id]);}
     const double Distance=FVector::Distance(Expected,FVector(Actual[R].Position));++Compared;ClipMaximum=FMath::Max(ClipMaximum,Distance);
     if(!FMath::IsFinite(Distance)||Distance>.05){Good=false;Error=FString::Printf(TEXT("Native geometry mismatch clip=%s part=%s frame=%d vertex=%d error=%.9fcm expected=%s actual=%s initial_offset=%s"),*Entry.Clip,*Part.NativeRenderer,F,R,Distance,*Expected.ToString(),*FVector(Actual[R].Position).ToString(),*Offsets[P][Id].ToString());break;}
    }
   }
  }
  Maximum=FMath::Max(Maximum,ClipMaximum);UE_LOG(LogTemp,Display,TEXT("Original animation native geometry clip=%s parts=%d max_error_cm=%.9f valid=%d"),*Entry.Clip,Outfit->Parts.Num(),ClipMaximum,Good?1:0);
 }
 Actor->Destroy();World->DestroyWorld(false);GEngine->DestroyWorldContext(World);
 if(!Good)return false;UE_LOG(LogTemp,Display,TEXT("Original animations native verified vertices=%lld max_error_cm=%.9f all_sections=1"),Compared,Maximum);return true;
}
#endif
}
UKKImportedAnimationLibrary* KKAnimationImporter::Import(const FString& Card,const FString& CharacterPath,
 const FString& Source,const TArray<FString>& Clips,const FString& Destination,int32 Outfit,int32 FPS,FString& Error,const FString& NativeRoot){
#if WITH_EDITOR
 Error.Reset();
 FKKScopedNativeSkinSource NativeSource;
 if(FPS<1||FPS>240 || Clips.IsEmpty() || Source.IsEmpty() || !Destination.StartsWith(TEXT("/Game/")) || !FPackageName::IsValidLongPackageName(Destination)){
  Fail(Error,TEXT("Require Card, Character, Source, Clips, fresh /Game/ destination and FPS 1..240"));return nullptr;}
 if(!OKValidateImportDestination(Destination,Error))return nullptr;
 auto* Character=LoadObject<UKKImportedCharacter>(nullptr,*CharacterPath);
 if(!Character){Fail(Error,TEXT("Target character not found"));return nullptr;}
 TArray<uint8> Bytes;if(!FFileHelper::LoadFileToArray(Bytes,*Card)){Fail(Error,TEXT("Cannot read original card"));return nullptr;}
 uint8 Hash[20];FSHA1::HashBuffer(Bytes.GetData(),Bytes.Num(),Hash);
 if(BytesToHex(Hash,20)!=Character->SourceHash){Fail(Error,TEXT("Original card SHA1 differs from target character; reimport the original card before baking"));return nullptr;}
 FString Root=NativeRoot;if(Root.IsEmpty()&&!OKResolveCardSource(Card,Root,Error))return nullptr;
 TArray<const FKKImportedOutfit*> Outfits;
 for(const auto& O:Character->Outfits)if(Outfit<0||Outfit==O.Index)Outfits.Add(&O);
 if(Outfits.IsEmpty()){Fail(Error,TEXT("Requested outfit not imported"));return nullptr;}
 // Validate every selection before creating a persistent package.
 TSet<FString> Unique;
 for(const FString& Clip:Clips){if(Clip.IsEmpty()||Unique.Contains(Clip)){Fail(Error,TEXT("Empty/duplicate clip selection"));return nullptr;}Unique.Add(Clip);}
 for(const auto* O:Outfits){FOkSession Session;
  if(!Session.Open(Root,Card,O->Index,Error)||Session.IsPortable()||!CheckRig(Session,O->Skeleton,Error,nullptr,O)){if(Error.IsEmpty())Error=TEXT("Animation baking requires an original card source");return nullptr;}
  const auto Names=Session.ClipNamesForSource(Source);
  for(const FString& Clip:Clips)if(!Names.Contains(Clip)){Fail(Error,TEXT("Unknown qualified clip: ")+Source+TEXT("|")+Clip);return nullptr;}
 }
 auto* Library=NewObject<UKKImportedAnimationLibrary>(CreatePackage(*(Destination/TEXT("Animations"))),TEXT("Animations"),RF_Public|RF_Standalone);Library->Character=Character;
 TArray<UObject*> Assets;
 for(const auto* O:Outfits)for(int32 C=0;C<Clips.Num();++C){FOkSession Session;TArray<int32> Mapping;
  if(!Session.Open(Root,Card,O->Index,Error)||!CheckRig(Session,O->Skeleton,Error,&Mapping,O)||!Session.SetAnimClip(Source+TEXT("|")+Clips[C])){
   if(Error.IsEmpty())Error=TEXT("Selecting clip failed");return nullptr;}
  const double Duration=Session.AnimDuration();
  if(!FMath::IsFinite(Duration)||Duration<=0.||Duration>3600.){Fail(Error,TEXT("Invalid/oversized clip duration"));return nullptr;}
  const int32 Frames=FMath::Max(1,FMath::CeilToInt(Duration*FPS));
  if(int64(Frames+1)*O->Skeleton->GetReferenceSkeleton().GetNum()>10000000){Fail(Error,TEXT("Clip exceeds 10 million bone keys; lower FPS or import fewer frames externally"));return nullptr;}
  // A rational sample rate preserves the source length (within 1e-7 seconds).
  const int32 Denominator=1000000;
  const int32 Numerator=FMath::RoundToInt(Frames/Duration*Denominator);
  const FFrameRate Rate(Numerator,Denominator);
  const double Step=Rate.AsInterval();
  const auto& Ref=O->Skeleton->GetReferenceSkeleton();TSet<FName> ZeroScaleBones;TArray<FTrack> Tracks;Tracks.SetNum(Ref.GetNum());
  for(auto& T:Tracks){T.Positions.Reserve(Frames+1);T.Rotations.Reserve(Frames+1);T.Scales.Reserve(Frames+1);}
  for(int32 F=0;F<=Frames;++F){
   if(!Session.ApplyAnimTime(F*Step)){Fail(Error,TEXT("Native sampling failed"));return nullptr;}
   TArray<FTransform> Pose;Pose.Init(FTransform::Identity,Ref.GetNum());
   for(int32 B=0;B<Ref.GetNum();++B)if(Mapping[B]!=INDEX_NONE){OkBoneView V{};if(!Session.SkelBoneAt(Mapping[B],V)){Fail(Error,TEXT("Native bone disappeared"));return nullptr;}Pose[B]=OKUnityLocalToUnreal(V.pose_pos,V.pose_rot,V.pose_scl);}
   if(Ref.FindBoneIndex(TEXT("__ok_scene"))==0)for(const auto& P:O->Parts){OkSkinPaletteView Raw{};TArray<float> Matrices;FMatrix Rigid=FMatrix::Identity;
    if(P.RigidFollow.IsEmpty()){if(Session.SkinPalette(P.NativePartIndex,Raw,Matrices)<=0){Fail(Error,TEXT("Sampling exact palette failed"));return nullptr;}}
    else{float World[16];if(!Session.BoneWorldMatrix(P.RigidFollow,World)){Fail(Error,TEXT("Sampling rigid socket failed"));return nullptr;}Rigid=OKSkinPaletteToUnreal(P.RigidReference.GetData()).Inverse()*OKSkinPaletteToUnreal(World);}
    for(int32 Slot:P.PaletteSlots){FMatrix Matrix=Rigid;if(P.RigidFollow.IsEmpty()&&!OKExactPaletteMatrix(Raw,Matrices,Slot,Matrix)){Fail(Error,TEXT("Sampled palette slot disappeared"));return nullptr;}
     for(int32 Axis=0;Axis<3;++Axis){const int32 B=Ref.FindBoneIndex(OKPaletteBoneName(P.NativePartIndex,Slot,Axis));if(B==INDEX_NONE){Fail(Error,TEXT("Palette target bone disappeared"));return nullptr;}Pose[B]=Tracks[B].Rotations.IsEmpty()?OKSkinPaletteColumn(Matrix,Axis):OKSkinPaletteColumnContinuous(Matrix,Axis,Tracks[B].Rotations.Last());}}
   }
   for(int32 B=0;B<Ref.GetNum();++B){const FTransform& X=Pose[B];
    if(X.ContainsNaN()){Fail(Error,TEXT("Nonfinite sampled bone TRS: ")+Ref.GetBoneName(B).ToString());return nullptr;}
    if(X.GetScale3D().GetAbsMin()<1e-8)ZeroScaleBones.Add(Ref.GetBoneName(B));
    auto& T=Tracks[B];FQuat Q=X.GetRotation();Q.Normalize();if(!T.Rotations.IsEmpty()&&(Q|T.Rotations.Last())<0.)Q=Q*-1.;
    T.Positions.Add(X.GetTranslation());T.Rotations.Add(Q);T.Scales.Add(X.GetScale3D());
   }
  }
  for(const FName Bone:ZeroScaleBones)if(!Bone.ToString().StartsWith(TEXT("__ok_palette_")))UE_LOG(LogTemp,Display,TEXT("Original animation preserves zero scale bone=%s clip=%s"),*Bone.ToString(),*Clips[C]);
  UE_LOG(LogTemp,Display,TEXT("Original animation zero-scale tracks=%d (includes rank-one palette axes)"),ZeroScaleBones.Num());
  const FString Name=FString::Printf(TEXT("A_Outfit%d_%04d"),O->Index,C);
  auto* Seq=NewObject<UKKImportedAnimSequence>(CreatePackage(*(Destination/Name)),*Name,RF_Public|RF_Standalone);Seq->SetSkeleton(O->Skeleton);
  if(Character->Schema==2)Assets.Add(ExactPaletteCompression(Seq));
  for(const auto& Part:O->Parts)if(Part.SkinnedModel){Seq->SetPreviewMesh(Part.SkinnedModel);break;}
  if(!Seq->GetDataModelInterface().GetObject()->IsA<UAnimDataModel>()){Fail(Error,TEXT("Quaternion source model provider unavailable"));return nullptr;}
  auto& Ctrl=Seq->GetController();Ctrl.OpenBracket(NSLOCTEXT("KK","ImportOriginalAnimation","Import original skeletal animation"),false);Ctrl.InitializeModel();
  Ctrl.SetFrameRate(Rate,false);Ctrl.SetNumberOfFrames(FFrameNumber(Frames),false);
  for(int32 B=0;B<Tracks.Num();++B){const auto& T=Tracks[B];
   if(!Ctrl.AddBoneCurve(Ref.GetBoneName(B),false)||!Ctrl.SetBoneTrackKeys(Ref.GetBoneName(B),T.Positions,T.Rotations,T.Scales,false)){
    Ctrl.CloseBracket(false);Fail(Error,TEXT("Writing animation bone track failed"));return nullptr;}}
  Ctrl.NotifyPopulated();Ctrl.CloseBracket(false);Seq->PostEditChange();
  if(Seq->GetDataModelInterface()->GetNumBoneTracks()!=Ref.GetNum()){Fail(Error,TEXT("Animation tracks were lost"));return nullptr;}
  // Independently read the authored tracks at their sample times before saving.
  double MaxPosition=0.,MaxAngle=0.,MaxScale=0.,MaxEulerLoss=0.;
  for(int32 B=0;B<Ref.GetNum();++B)for(int32 F=0;F<=Frames;++F){const FTransform X=Seq->GetDataModelInterface()->GetBoneTrackTransform(Ref.GetBoneName(B),FFrameNumber(F));
   MaxPosition=FMath::Max(MaxPosition,FVector::Distance(X.GetTranslation(),Tracks[B].Positions[F]));
   MaxAngle=FMath::Max(MaxAngle,RotationDistance(X.GetRotation(),Tracks[B].Rotations[F]));
   MaxScale=FMath::Max(MaxScale,FVector::Distance(X.GetScale3D(),Tracks[B].Scales[F]));
   const FQuat EulerRoundTrip(FQuat4f(Tracks[B].Rotations[F]).Rotator().Quaternion());MaxEulerLoss=FMath::Max(MaxEulerLoss,RotationDistance(EulerRoundTrip,Tracks[B].Rotations[F]));}
  if(MaxPosition>0.01||MaxAngle>0.001||MaxScale>0.001){Fail(Error,FString::Printf(TEXT("Authored tracks differ from native samples position=%.9f angle=%.9f scale=%.9f"),MaxPosition,MaxAngle,MaxScale));return nullptr;}
  FKKImportedAnimation Entry;Entry.Source=Source;Entry.Clip=Clips[C];Entry.Outfit=O->Index;Entry.Loop=Session.IsAnimLooping();Entry.SourceDuration=Duration;Entry.Sequence=Seq;Library->Animations.Add(Entry);Assets.Add(Seq);
  UE_LOG(LogTemp,Display,TEXT("Original animation baked source=%s clip=%s outfit=%d bones=%d frames=%d duration=%.9f position_error=%.9f angle_error=%.9f scale_error=%.9f default_euler_loss=%.9f"),*Source,*Clips[C],O->Index,Ref.GetNum(),Frames,Seq->GetPlayLength(),MaxPosition,MaxAngle,MaxScale,MaxEulerLoss);
 }
 for(auto* Asset:Assets){if(auto* Seq=Cast<UAnimSequence>(Asset))Seq->WaitOnExistingCompression();if(!Save(Asset,Error))return nullptr;}
 if(!Save(Library,Error))return nullptr;
 UE_LOG(LogTemp,Display,TEXT("Original animations saved library=%s clips=%d"),*Library->GetPathName(),Library->Animations.Num());return Library;
#else
 Error=TEXT("Animation import requires an editor build");return nullptr;
#endif
}
UKKImportSkeletalAnimationsCommandlet::UKKImportSkeletalAnimationsCommandlet(){IsClient=true;IsServer=false;IsEditor=true;LogToConsole=true;}
int32 UKKImportSkeletalAnimationsCommandlet::Main(const FString& Params){
#if WITH_EDITOR

 if(FParse::Param(*Params,TEXT("Shared"))){FString Root,Source,List,Destination,Error;
  FParse::Value(*Params,TEXT("GameRoot="),Root);FParse::Value(*Params,TEXT("Source="),Source);
  FParse::Value(*Params,TEXT("Clips="),List,false);FParse::Value(*Params,TEXT("Destination="),Destination);
  TArray<FString> Clips;List.ParseIntoArray(Clips,TEXT(","),true);TArray<UKKSharedAnimation*> Out;
  if(!KKAnimationImporter::ImportShared(Root,Source,Clips,Destination,Out,Error)){UE_LOG(LogTemp,Error,TEXT("%s"),*Error);return 1;}return 0;
 }

#if WITH_EDITOR
 FString Recompress;if(FParse::Value(*Params,TEXT("Recompress="),Recompress)){FString Destination,Error;FParse::Value(*Params,TEXT("Destination="),Destination);
  auto* Original=LoadObject<UKKImportedAnimationLibrary>(nullptr,*Recompress);if(!Original||!Original->Character||Original->Character->Schema!=2||Original->Animations.IsEmpty()||!Destination.StartsWith(TEXT("/Game/"))||!FPackageName::IsValidLongPackageName(Destination)||IFileManager::Get().DirectoryExists(*FPackageName::LongPackageNameToFilename(Destination))){UE_LOG(LogTemp,Error,TEXT("Require schema2 library and fresh destination"));return 1;}
  if(!OKValidateImportDestination(Destination,Error)){UE_LOG(LogTemp,Error,TEXT("%s"),*Error);return 1;}
  auto* Library=NewObject<UKKImportedAnimationLibrary>(CreatePackage(*(Destination/TEXT("Animations"))),TEXT("Animations"),RF_Public|RF_Standalone);Library->Character=Original->Character;
  for(int32 C=0;C<Original->Animations.Num();++C){auto Entry=Original->Animations[C];const FString Name=FString::Printf(TEXT("A_Outfit%d_%04d"),Entry.Outfit,C);auto* Seq=NewObject<UKKImportedAnimSequence>(CreatePackage(*(Destination/Name)),*Name,RF_Public|RF_Standalone);Seq->SetSkeleton(Entry.Sequence->GetSkeleton());
   if(!Seq->GetDataModelInterface().GetObject()->IsA<UAnimDataModel>()){UE_LOG(LogTemp,Error,TEXT("Quaternion source provider unavailable"));return 1;}
   const auto* Model=Entry.Sequence->GetDataModelInterface().GetInterface();TArray<FName> Names;Model->GetBoneTrackNames(Names);auto& Ctrl=Seq->GetController();Ctrl.OpenBracket(NSLOCTEXT("KK","ReencodeTracks","Reencode quaternion source tracks"),false);Ctrl.InitializeModel();Ctrl.SetFrameRate(Model->GetFrameRate(),false);Ctrl.SetNumberOfFrames(FFrameNumber(Model->GetNumberOfFrames()),false);
   for(FName Bone:Names){TArray<FTransform> Poses;Model->GetBoneTrackTransforms(Bone,Poses);TArray<FVector> Positions,Scales;TArray<FQuat> Rotations;for(const auto& Pose:Poses){Positions.Add(Pose.GetTranslation());Rotations.Add(Pose.GetRotation());Scales.Add(Pose.GetScale3D());}if(!Ctrl.AddBoneCurve(Bone,false)||!Ctrl.SetBoneTrackKeys(Bone,Positions,Rotations,Scales,false)){Ctrl.CloseBracket(false);UE_LOG(LogTemp,Error,TEXT("Reencoding quaternion tracks failed"));return 1;}}
   Ctrl.NotifyPopulated();Ctrl.CloseBracket(false);if(Seq->GetDataModelInterface()->GetNumBoneTracks()!=Names.Num()){UE_LOG(LogTemp,Error,TEXT("Recompression lost source tracks"));return 1;}
   auto* Settings=ExactPaletteCompression(Seq);Seq->PostEditChange();Seq->CacheDerivedDataForCurrentPlatform();Seq->WaitOnExistingCompression();if(!Save(Settings,Error)||!Save(Seq,Error)){UE_LOG(LogTemp,Error,TEXT("%s"),*Error);return 1;}Entry.Sequence=Seq;Library->Animations.Add(Entry);
  }
  if(!Save(Library,Error)){UE_LOG(LogTemp,Error,TEXT("%s"),*Error);return 1;}UE_LOG(LogTemp,Display,TEXT("Original animations saved library=%s clips=%d float_tracks=preserved"),*Library->GetPathName(),Library->Animations.Num());return 0;
 }
#endif
 if(FParse::Param(*Params,TEXT("Inventory"))){
  FString Card,CharacterPath,Root,Output,Error;int32 Outfit=0;
  FParse::Value(*Params,TEXT("Card="),Card);FParse::Value(*Params,TEXT("Character="),CharacterPath);
  FParse::Value(*Params,TEXT("GameRoot="),Root);FParse::Value(*Params,TEXT("Output="),Output);FParse::Value(*Params,TEXT("Outfit="),Outfit);
  auto* Character=LoadObject<UKKImportedCharacter>(nullptr,*CharacterPath);TArray<uint8> Bytes;
  if(!Character||Output.IsEmpty()||IFileManager::Get().FileExists(*Output)||!FFileHelper::LoadFileToArray(Bytes,*Card))return 1;
  uint8 Hash[20];FSHA1::HashBuffer(Bytes.GetData(),Bytes.Num(),Hash);
  if(BytesToHex(Hash,20)!=Character->SourceHash)return 1;
  const auto* Saved=Character->Outfits.FindByPredicate([&](const auto& O){return O.Index==Outfit;});if(!Saved)return 1;
  if(Root.IsEmpty()&&!OKResolveCardSource(Card,Root,Error))return 1;
  const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).CreateNavigation(false).CreateAISystem(false);
  auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
  const FString Previous=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_CPU_SKIN"));FPlatformMisc::SetEnvironmentVar(TEXT("OK_CPU_SKIN"),TEXT("1"));
  auto* Actor=World->SpawnActor<AKKPreviewActor>();bool Good=Actor->Session.Open(Root,Card,Outfit,Error)&&Actor->LoadSession(Error);Actor->SetActorTickEnabled(false);
  TArray<TSharedPtr<FJsonValue>> Rows;int32 StaticOnly=0;
  if(Good)for(const auto& P:Saved->Parts){
   const auto* Live=Actor->Parts.FindByPredicate([&](const auto& L){return L.Comp&&L.Comp->GetName()==P.Name;});OkMeshView V{};
   if(!Live||!Actor->Session.PartAt(Live->SessionIndex,V)||P.Role!=Live->Role){Error=TEXT("Saved/source part identity differs: ")+P.Name;Good=false;break;}
   auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("saved_part"),P.Name);Row->SetStringField(TEXT("source_name"),V.name?UTF8_TO_TCHAR(V.name):TEXT(""));Row->SetStringField(TEXT("role"),P.Role);
   Row->SetNumberField(TEXT("session_index"),Live->SessionIndex);Row->SetNumberField(TEXT("skel_kind"),V.skel_kind);Row->SetNumberField(TEXT("vertices"),V.n_verts);Row->SetNumberField(TEXT("indices"),V.n_idx);
   Row->SetNumberField(TEXT("sections"),Live->Comp->GetNumSections());Row->SetBoolField(TEXT("has_saved_skin"),P.SkinnedModel!=nullptr);Row->SetBoolField(TEXT("source_weights"),V.weights&&V.bone_idx);
   Row->SetBoolField(TEXT("runtime_accessory"),Live->bRuntimeAccessory);Row->SetBoolField(TEXT("slot_attached"),Live->bSlotAttached);
   Row->SetStringField(TEXT("parent_bone"),V.parent_bone?UTF8_TO_TCHAR(V.parent_bone):TEXT(""));Row->SetStringField(TEXT("source_rigid_follow"),V.rigid_follow_bone?UTF8_TO_TCHAR(V.rigid_follow_bone):TEXT(""));Row->SetStringField(TEXT("preview_rigid_follow"),Live->RigidFollowBone.ToString());
   if(!P.SkinnedModel)++StaticOnly;Rows.Add(MakeShared<FJsonValueObject>(Row));
  }
  FString AuditClip;FParse::Value(*Params,TEXT("AuditClip="),AuditClip);
  if(Good&&!AuditClip.IsEmpty()){
   Good=Actor->Session.SetAnimClip(TEXT("studio/anime/00.unity3d|")+AuditClip);
   if(!Good)Error=TEXT("Inventory audit clip unavailable");
   const double Duration=Actor->Session.AnimDuration();
   for(int32 F=0;F<5&&Good;++F){Good=Actor->Session.ApplyAnimTime(Duration*F/5.);
    for(const auto& Value:Rows){auto Row=Value->AsObject();if(Row->GetIntegerField(TEXT("skel_kind"))!=4)continue;
     if(!AuditPrivatePalette(Actor->Session,Row->GetIntegerField(TEXT("session_index")),Row,Error)){Good=false;break;}}
   }
  }
  Actor->Destroy();World->DestroyWorld(false);GEngine->DestroyWorldContext(World);FPlatformMisc::SetEnvironmentVar(TEXT("OK_CPU_SKIN"),*Previous);
  if(Good){auto Report=MakeShared<FJsonObject>();Report->SetStringField(TEXT("character"),CharacterPath);Report->SetStringField(TEXT("source_hash"),Character->SourceHash);Report->SetNumberField(TEXT("outfit"),Outfit);Report->SetNumberField(TEXT("static_only"),StaticOnly);Report->SetArrayField(TEXT("parts"),Rows);FString Json;Good=FJsonSerializer::Serialize(Report,TJsonWriterFactory<>::Create(&Json))&&FFileHelper::SaveStringToFile(Json,*Output,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);}
  if(!Good){UE_LOG(LogTemp,Error,TEXT("Original animation inventory failed: %s"),*Error);return 1;}
  UE_LOG(LogTemp,Display,TEXT("Original animation inventory parts=%d static_only=%d output=%s"),Rows.Num(),StaticOnly,*Output);return 0;
 }
#if WITH_EDITOR
 if(FParse::Param(*Params,TEXT("ModelProbe"))){
  auto* Seq=NewObject<UKKImportedAnimSequence>();auto* Model=Seq->GetDataModelInterface().GetObject();
  const bool Good=Model && Model->IsA<UAnimDataModel>();
  UE_LOG(LogTemp,Display,TEXT("Original animation model probe class=%s valid=%d"),Model?*Model->GetClass()->GetName():TEXT("none"),Good?1:0);return Good?0:1;
 }
#endif
 FString Verify;
 if(FParse::Value(*Params,TEXT("Verify="),Verify)){
  if(FParse::Param(*Params,TEXT("NativeVerify"))){FString Card,Root,Error;FParse::Value(*Params,TEXT("Card="),Card);FParse::Value(*Params,TEXT("GameRoot="),Root);
   if(Root.IsEmpty()&&!OKResolveCardSource(Card,Root,Error)){UE_LOG(LogTemp,Error,TEXT("%s"),*Error);return 1;}
   auto* Library=LoadObject<UKKImportedAnimationLibrary>(nullptr,*Verify);FAssetCompilingManager::Get().FinishAllCompilation();
   if(!NativeAnimationAudit(Library,Card,Root,Error,FParse::Param(*Params,TEXT("ShareBakedPose")))){UE_LOG(LogTemp,Error,TEXT("Native animation audit failed: %s"),*Error);return 1;}return 0;}
  auto* Library=LoadObject<UKKImportedAnimationLibrary>(nullptr,*Verify);
  if(!Library||!Library->Character||Library->Animations.IsEmpty()){UE_LOG(LogTemp,Error,TEXT("Original animations verify: missing library"));return 1;}
  FAssetCompilingManager::Get().FinishAllCompilation();
  const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).CreateNavigation(false).CreateAISystem(false);
  auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
  auto* Actor=World->SpawnActor<AKKImportedCharacterActor>();Actor->Character=Library->Character;Actor->AnimationLibrary=Library;Actor->PlaySkeletalAnimation=true;
  bool Good=true;int32 TotalSkins=0,TotalVertices=0,Moving=0;double MaxBoneError=0.;
  for(int32 I=0;I<Library->Animations.Num()&&Good;++I){const auto& Entry=Library->Animations[I];Entry.Sequence->CacheDerivedDataForCurrentPlatform();Entry.Sequence->WaitOnExistingCompression();{const auto Data=Entry.Sequence->GetCompressedData();if(!Data.Get().IsValid(Entry.Sequence)){Good=false;break;}}
   Actor->Outfit=Entry.Outfit;Actor->AnimationIndex=I;Actor->RebuildCharacter();
   if(!Actor->PlayImportedAnimation(I)||!Actor->SetAnimationPosition(0.f)){Good=false;break;}
   TArray<USkeletalMeshComponent*> Skins;Actor->GetComponents(Skins);TArray<TArray<FFinalSkinVertex>> Baselines;Baselines.SetNum(Skins.Num());
   for(int32 K=0;K<Skins.Num();++K){Skins[K]->GetCPUSkinnedVertices(Baselines[K],0);TotalVertices+=Baselines[K].Num();++TotalSkins;}
   double Motion=0.;
   for(int32 F=1;F<=4&&Good;++F){const double Time=Entry.Sequence->GetPlayLength()*F/5.;if(!Actor->SetAnimationPosition(Time)){Good=false;break;}
    for(int32 K=0;K<Skins.Num()&&Good;++K){auto* Comp=Skins[K];const auto Local=Comp->GetBoneSpaceTransforms();const auto& Ref=Entry.Sequence->GetSkeleton()->GetReferenceSkeleton();
     if(Local.Num()!=Ref.GetNum()){Good=false;break;}
     for(FBoneIndexType B:Comp->RequiredBones){const int32 Slot=Ref.FindBoneIndex(Comp->GetSkeletalMeshAsset()->GetRefSkeleton().GetBoneName(B));if(Slot==INDEX_NONE){Good=false;break;}FTransform Expected=Ref.GetRefBonePose()[Slot];FAnimExtractContext Context(Time);Context.bEnforceCompressedDataSampling=true;Entry.Sequence->GetBoneTransform(Expected,FSkeletonPoseBoneIndex(Slot),Context,false);
      const double E=FVector::Distance(Expected.GetTranslation(),Local[B].GetTranslation());MaxBoneError=FMath::Max(MaxBoneError,E);
      if(Local[B].ContainsNaN()||E>0.05||RotationDistance(Expected.GetRotation(),Local[B].GetRotation())>0.005||!Expected.GetScale3D().Equals(Local[B].GetScale3D(),0.005)){Good=false;break;}}
     TArray<FFinalSkinVertex> Vertices;Comp->GetCPUSkinnedVertices(Vertices,0);if(Vertices.Num()!=Baselines[K].Num()){Good=false;break;}
     for(int32 V=0;V<Vertices.Num();++V){if(Vertices[V].Position.ContainsNaN()){Good=false;break;}Motion=FMath::Max(Motion,double((Vertices[V].Position-Baselines[K][V].Position).Size()));}
    }
   }
   if(Motion>0.001)++Moving;
   UE_LOG(LogTemp,Display,TEXT("Original animation runtime clip=%s outfit=%d skins=%d max_vertex_motion=%.6f bone_error=%.6f valid=%d"),*Entry.Clip,Entry.Outfit,Skins.Num(),Motion,MaxBoneError,Good?1:0);
  }
  Actor->Destroy();World->DestroyWorld(false);GEngine->DestroyWorldContext(World);
  if(!Good||!TotalSkins||Moving!=Library->Animations.Num()){UE_LOG(LogTemp,Error,TEXT("Original animations verify failed skins=%d moving=%d"),TotalSkins,Moving);return 1;}
  UE_LOG(LogTemp,Display,TEXT("Original animations verified clips=%d skin_instances=%d vertices=%d bone_error=%.9f compressed=valid"),Moving,TotalSkins,TotalVertices,MaxBoneError);return 0;
 }
 FString Card,Character,Source,ClipText,Destination,Error,NativeRoot;int32 Outfit=-1,FPS=60;
 FParse::Value(*Params,TEXT("GameRoot="),NativeRoot);
 FParse::Value(*Params,TEXT("Card="),Card);FParse::Value(*Params,TEXT("Character="),Character);FParse::Value(*Params,TEXT("Source="),Source);
 FParse::Value(*Params,TEXT("Clips="),ClipText);FParse::Value(*Params,TEXT("Destination="),Destination);FParse::Value(*Params,TEXT("Outfit="),Outfit);FParse::Value(*Params,TEXT("FPS="),FPS);
 if(FParse::Param(*Params,TEXT("List"))){FString Root;FOkSession Session;if(!OKResolveCardSource(Card,Root,Error)||!Session.Open(Root,Card,FMath::Max(0,Outfit),Error)){UE_LOG(LogTemp,Error,TEXT("Animation list: %s"),*Error);return 1;}
  for(const auto& Name:Session.ClipNamesForSource(Source))UE_LOG(LogTemp,Display,TEXT("Original animation label=%s"),*Name);return 0;}
 TArray<FString> Clips;ClipText.ParseIntoArray(Clips,TEXT(";"),true);
 if(!KKAnimationImporter::Import(Card,Character,Source,Clips,Destination,Outfit,FPS,Error,NativeRoot)){UE_LOG(LogTemp,Error,TEXT("Original animation import failed: %s"),*Error);return 1;}return 0;

#else
UE_LOG(LogTemp,Error,TEXT("Animation import requires the editor build"));return 1;
#endif
}

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKKSourceContractTest,"OpenKoikatsu.Animation.SourceContract",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FKKSourceContractTest::RunTest(const FString& Parameters){
 const FString CharacterPath=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_SOURCE_CONTRACT_CHARACTER"));
 const FString CardPath=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_SOURCE_CONTRACT_CARD"));
 const FString Root=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_SOURCE_CONTRACT_ROOT"));
 auto* Character=LoadObject<UKKImportedCharacter>(nullptr,*CharacterPath);
 if(!TestNotNull(TEXT("Persisted character"),Character)||Character->Outfits.IsEmpty())return false;
 FKKScopedNativeSkinSource Scope;FOkSession Session;FString Error;
 const auto& Outfit=Character->Outfits[0];
 if(!TestTrue(TEXT("Open native source"),Session.Open(Root,CardPath,Outfit.Index,Error)))return false;
 if(!TestTrue(TEXT("Persisted source contract matches"),CheckRig(Session,Outfit.Skeleton,Error,nullptr,&Outfit)))return false;
 TArray<FString> Before;
 for(const auto& P:Outfit.Parts){TestEqual(TEXT("Persisted v2 hash length"),P.SkinSourceHashV2.Len(),64);Before.Add(Session.SkinSourceHashV2(P.NativePartIndex));}
 const FString Clip=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_SOURCE_CONTRACT_CLIP"));
 if(!TestTrue(TEXT("Set original clip"),Session.SetAnimClip(Clip))||!TestTrue(TEXT("Advance native frame"),Session.ApplyAnimFrame(1.f,1.f)))return false;
 for(int32 I=0;I<Outfit.Parts.Num();++I)TestEqual(TEXT("Source contract invariant during animation"),Session.SkinSourceHashV2(Outfit.Parts[I].NativePartIndex),Before[I]);
 FKKImportedOutfit Changed=Outfit;
 if(!TestTrue(TEXT("Nonempty parts"),!Changed.Parts.IsEmpty()))return false;
 Changed.Parts[0].SkinSourceHashV2=FString::ChrN(64,TEXT('0'));
 TestFalse(TEXT("Reject changed private rig contract with unchanged v1"),CheckRig(Session,Changed.Skeleton,Error,nullptr,&Changed));
 TestTrue(TEXT("Specific mismatch diagnostic"),Error.StartsWith(TEXT("Source private rig/assignment changed:")));
 TestEqual(TEXT("Loaded asset metadata preserved"),Character->Outfits[0].Parts[0].SkinSourceHashV2,Before[0]);
 return true;
}
#endif
