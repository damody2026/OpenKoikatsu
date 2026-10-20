#include "CoreMinimal.h"
#include "KKImportedCharacter.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "Animation/AnimData/IAnimationDataController.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "UObject/SavePackage.h"
#include "UObject/StrongObjectPtr.h"
#include "Json.h"
#include "ok_extract.h"

namespace {
struct FNeutralSession {
 OkRustBindings API{};OkSession* Handle=nullptr;
 FNeutralSession(){ok_register_bindings(OkUnrealBindings{},&API);}
 ~FNeutralSession(){if(Handle)API.session_free(Handle);}
 bool Open(const TArray<uint8>& Bytes){Handle=ok_session_open_animation_snapshot(Bytes.GetData(),Bytes.Num());if(!Handle)return false;TArray<float> Body,Face;const int32 BodyCount=API.shape_body(Handle,nullptr,0),FaceCount=API.shape_face(Handle,nullptr,0);if(BodyCount<0||FaceCount<0)return false;Body.SetNum(BodyCount);Face.SetNum(FaceCount);return API.shape_body(Handle,Body.GetData(),Body.Num())==BodyCount&&API.shape_face(Handle,Face.GetData(),Face.Num())==FaceCount&&API.apply_shape(Handle,Body.GetData(),Body.Num(),Face.GetData(),Face.Num())==0;}
};
FMatrix SocketRest(const FReferenceSkeleton& Ref,FName Name){
 FMatrix Result=FMatrix::Identity;for(int32 B=Ref.FindBoneIndex(Name);B!=INDEX_NONE;B=Ref.GetParentIndex(B))Result=Result*Ref.GetRefBonePose()[B].ToMatrixWithScale();return Result;
}
FMatrix FromUnity(const TArray<float>& V){FMatrix M=FMatrix::Identity;const int32 Map[3]={0,2,1};const double Sign[3]={-1,1,1};for(int32 A=0;A<3;++A){for(int32 B=0;B<3;++B)M.M[A][B]=Sign[A]*Sign[B]*V[Map[B]*4+Map[A]];M.M[3][A]=100*Sign[A]*V[Map[A]*4+3];}return M;}
double MatrixDifference(const FMatrix& A,const FMatrix& B){double E=0;for(int32 R=0;R<4;++R)for(int32 C=0;C<4;++C)E=FMath::Max(E,FMath::Abs(A.M[R][C]-B.M[R][C]));return E;}
// Same rank-one affine encoding as KKExactSkinning: three equally weighted
// transforms preserve shear. Never convert the complete matrix to one TRS.
FTransform Column(const FMatrix& M,int32 Axis,const FQuat* Previous){const FVector Basis=Axis==0?FVector::XAxisVector:Axis==1?FVector::YAxisVector:FVector::ZAxisVector;const FVector C(M.M[Axis][0],M.M[Axis][1],M.M[Axis][2]);FVector Scale=FVector::ZeroVector;Scale[Axis]=3*C.Size();FQuat Q=Previous?*Previous:FQuat::Identity;if(C.Size()>1e-12)Q=Previous?(FQuat::FindBetweenNormals(Previous->RotateVector(Basis),C.GetSafeNormal())*(*Previous)).GetNormalized():FQuat::FindBetweenNormals(Basis,C.GetSafeNormal());return FTransform(Q,M.GetOrigin(),Scale);}
bool Save(UObject* Asset){const FString File=FPackageName::LongPackageNameToFilename(Asset->GetOutermost()->GetName(),FPackageName::GetAssetPackageExtension());if(IFileManager::Get().FileExists(*File))return false;IFileManager::Get().MakeDirectory(*FPaths::GetPath(File),true);FSavePackageArgs A;A.TopLevelFlags=RF_Public|RF_Standalone;return UPackage::SavePackage(Asset->GetOutermost(),Asset,*File,A);}
}

bool RepairRigidBindings(const FString& Input,const FString& AssetRoot,const FString& Output){
 FString Text;if(!FFileHelper::LoadFileToString(Text,*Input))return false;FPackageName::RegisterMountPoint(TEXT("/Game/"),AssetRoot/TEXT(""));TSharedPtr<FJsonObject> Request;if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Request)||!Request)return false;
 const TArray<TSharedPtr<FJsonValue>>* Rows=nullptr;if(!Request->TryGetArrayField(TEXT("libraries"),Rows)||Rows->IsEmpty())return false;TArray<TSharedPtr<FJsonValue>> Receipts;TArray<TStrongObjectPtr<UObject>> Assets;
 for(const auto& Value:*Rows){CollectGarbage(RF_NoFlags);const auto Row=Value->AsObject();if(!Row)return false;const FString LibraryPath=Row->GetStringField(TEXT("library")),SourcePath=Row->GetStringField(TEXT("source_character")),Destination=Row->GetStringField(TEXT("destination"));if(!Destination.StartsWith(TEXT("/Game/"))||!FPackageName::IsValidLongPackageName(Destination)||FPackageName::DoesPackageExist(Destination/TEXT("Animations")))return false;
  TStrongObjectPtr<UKKImportedAnimationLibrary> Library(LoadObject<UKKImportedAnimationLibrary>(nullptr,*LibraryPath));TStrongObjectPtr<UKKImportedCharacter> Source(LoadObject<UKKImportedCharacter>(nullptr,*SourcePath));if(!Library||!Library->Character||!Source||Source->Schema!=2||Library->Character->SourceHash!=Source->SourceHash)return false;
  auto* Fixed=NewObject<UKKImportedAnimationLibrary>(CreatePackage(*(Destination/TEXT("Animations"))),TEXT("Animations"),RF_Public|RF_Standalone);Assets.Emplace(Fixed);Fixed->Character=Library->Character;Fixed->Animations=Library->Animations;int32 Corrected=0;double MaxError=0;
  for(int32 Clip=0;Clip<Fixed->Animations.Num();++Clip){const auto& Entry=Library->Animations[Clip];if(!Entry.Sequence)return false;const auto* Outfit=Source->Outfits.FindByPredicate([&](const auto& O){return O.Index==Entry.Outfit;});if(!Outfit)return false;
   const FString Path=Destination/FString::Printf(TEXT("Clip_%04d"),Clip);if(FPackageName::DoesPackageExist(Path))return false;auto* Sequence=Cast<UAnimSequence>(StaticDuplicateObject(Entry.Sequence,CreatePackage(*Path),*FPackageName::GetLongPackageAssetName(Path)));if(!Sequence)return false;Assets.Emplace(Sequence);Sequence->SetPreviewMesh(nullptr);const auto& Ref=Sequence->GetSkeleton()->GetReferenceSkeleton();const auto* Model=Entry.Sequence->GetDataModelInterface().GetInterface();const int32 Frames=Model->GetNumberOfFrames();const double Step=Model->GetFrameRate().AsInterval();
   FNeutralSession Neutral;if(!Neutral.Open(Outfit->AnimationSnapshot))return false;
   struct FTrack{FName Names[3];int32 Bones[3];FMatrix Correction;TArray<FVector> P[3],S[3];TArray<FQuat> Q[3];TArray<FMatrix> Expected;};TArray<FTrack> Tracks;
   for(const auto& Part:Outfit->Parts)if(!Part.RigidFollow.IsEmpty()){
    if(Part.RigidReference.Num()!=16||Part.PaletteSlots!=TArray<int32>{0}||Ref.FindBoneIndex(FName(*Part.RigidFollow))==INDEX_NONE)return false;
    // The shared skeleton's rest pose omits card shape/ABMX placement. Restore
    // the native card neutral pose from its own snapshot, without an animation.
    TArray<float> Placement;Placement.SetNum(16);if(ok_bone_world_matrix(Neutral.Handle,TCHAR_TO_UTF8(*Part.RigidFollow),Placement.GetData())!=0)return false;FTrack Track;const FMatrix Rest=FromUnity(Placement);if(FMath::Abs(Rest.Determinant())<1e-10)return false;Track.Correction=Rest.Inverse()*FromUnity(Part.RigidReference);
    for(int32 Axis=0;Axis<3;++Axis){Track.Names[Axis]=FName(*FString::Printf(TEXT("__ok_palette_%d_0_%d"),Part.NativePartIndex,Axis));Track.Bones[Axis]=Ref.FindBoneIndex(Track.Names[Axis]);if(Track.Bones[Axis]==INDEX_NONE||Ref.GetParentIndex(Track.Bones[Axis])!=0||!Ref.GetRefBonePose()[Track.Bones[Axis]].Equals(FTransform::Identity))return false;}Tracks.Add(MoveTemp(Track));
   }
   for(int32 Frame=0;Frame<=Frames;++Frame){FAnimExtractContext C(Frame*Step);C.bEnforceCompressedDataSampling=true;for(auto& Track:Tracks){FMatrix Old(ForceInit);for(int32 Axis=0;Axis<3;++Axis){FTransform Pose;Entry.Sequence->GetBoneTransform(Pose,FSkeletonPoseBoneIndex(Track.Bones[Axis]),C,false);const auto M=Pose.ToMatrixWithScale();for(int32 R=0;R<4;++R)for(int32 K=0;K<4;++K)Old.M[R][K]+=M.M[R][K]/3.;}const auto Desired=Track.Correction*Old;Track.Expected.Add(Desired);for(int32 Axis=0;Axis<3;++Axis){const auto Pose=Column(Desired,Axis,Track.Q[Axis].IsEmpty()?nullptr:&Track.Q[Axis].Last());if(Pose.ContainsNaN())return false;Track.P[Axis].Add(Pose.GetTranslation());Track.Q[Axis].Add(Pose.GetRotation());Track.S[Axis].Add(Pose.GetScale3D());}}}
   auto& Controller=Sequence->GetController();Controller.OpenBracket(NSLOCTEXT("KK","RigidBindRepair","Repair rigid accessory inverse binds"),false);bool Good=true;for(const auto& Track:Tracks)for(int32 Axis=0;Axis<3;++Axis)Good=Controller.SetBoneTrackKeys(Track.Names[Axis],Track.P[Axis],Track.Q[Axis],Track.S[Axis],false)&&Good;Controller.NotifyPopulated();Controller.CloseBracket(false);if(!Good)return false;Sequence->PostEditChange();Sequence->CacheDerivedDataForCurrentPlatform();Sequence->WaitOnExistingCompression();
   for(int32 Frame=0;Frame<=Frames;++Frame){FAnimExtractContext C(Frame*Step);C.bEnforceCompressedDataSampling=true;for(const auto& Track:Tracks){FMatrix Actual(ForceInit);for(int32 Axis=0;Axis<3;++Axis){FTransform Pose;Sequence->GetBoneTransform(Pose,FSkeletonPoseBoneIndex(Track.Bones[Axis]),C,false);if(Pose.ContainsNaN())return false;const auto M=Pose.ToMatrixWithScale();for(int32 R=0;R<4;++R)for(int32 K=0;K<4;++K)Actual.M[R][K]+=M.M[R][K]/3.;}const double E=MatrixDifference(Actual,Track.Expected[Frame]);MaxError=FMath::Max(MaxError,E);if(E>.01){UE_LOG(LogTemp,Error,TEXT("Rigid binding compressed audit failed frame=%d error=%f"),Frame,E);return false;}}}Corrected+=Tracks.Num();Fixed->Animations[Clip].Sequence=Sequence;
  }
  auto Receipt=MakeShared<FJsonObject>();Receipt->SetStringField(TEXT("source_library"),LibraryPath);Receipt->SetStringField(TEXT("repaired_library"),Fixed->GetPathName());Receipt->SetNumberField(TEXT("rigid_parts_clips"),Corrected);Receipt->SetNumberField(TEXT("max_compressed_matrix_error"),MaxError);Receipt->SetNumberField(TEXT("clips"),Fixed->Animations.Num());Receipts.Add(MakeShared<FJsonValueObject>(Receipt));for(const auto& Asset:Assets)if(!Save(Asset.Get()))return false;Assets.Reset();
 }
 auto Report=MakeShared<FJsonObject>();Report->SetBoolField(TEXT("passed"),true);Report->SetBoolField(TEXT("source_assets_modified"),false);Report->SetBoolField(TEXT("mesh_geometry_modified"),false);Report->SetArrayField(TEXT("libraries"),Receipts);FString Json;return FJsonSerializer::Serialize(Report,TJsonWriterFactory<>::Create(&Json))&&FFileHelper::SaveStringToFile(Json,*Output);
}
