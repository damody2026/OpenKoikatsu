#include "CoreMinimal.h"
#include "KKImportedCharacter.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "AssetCompilingManager.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Json.h"
#include "SkeletalMeshAttributes.h"
#include "ok_extract.h"
#include "UObject/StrongObjectPtr.h"

namespace {
FMatrix SocketMatrix(UAnimSequence* Sequence,FName Name,double Time,bool Rest){
 const auto& Ref=Sequence->GetSkeleton()->GetReferenceSkeleton();int32 Bone=Ref.FindBoneIndex(Name);if(Bone==INDEX_NONE)return FMatrix::Identity;
 FMatrix Result=FMatrix::Identity;FAnimExtractContext Context(Time);Context.bEnforceCompressedDataSampling=true;
 while(Bone!=INDEX_NONE){FTransform Local=Ref.GetRefBonePose()[Bone];if(!Rest)Sequence->GetBoneTransform(Local,FSkeletonPoseBoneIndex(Bone),Context,false);Result=Result*Local.ToMatrixWithScale();Bone=Ref.GetParentIndex(Bone);}return Result;
}
FMatrix UnityMatrix(const TArray<float>& Values){FMatrix Result=FMatrix::Identity;const int32 Map[3]={0,2,1};const double Sign[3]={-1.,1.,1.};for(int32 A=0;A<3;++A){for(int32 B=0;B<3;++B)Result.M[A][B]=Sign[A]*Sign[B]*Values[Map[B]*4+Map[A]];Result.M[3][A]=100.*Sign[A]*Values[Map[A]*4+3];}return Result;}
double MatrixError(const FMatrix& A,const FMatrix& B){double Max=0;for(int32 R=0;R<4;++R)for(int32 C=0;C<4;++C)Max=FMath::Max(Max,FMath::Abs(A.M[R][C]-B.M[R][C]));return Max;}
}

bool InspectImportedGeometry(const FString& Input,const FString& AssetRoot,const FString& Output){
 FString Text;if(!FFileHelper::LoadFileToString(Text,*Input))return false;
 FPackageName::RegisterMountPoint(TEXT("/Game/"),AssetRoot/TEXT(""));
 TSharedPtr<FJsonObject> Request;if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Request)||!Request)return false;
 const TArray<TSharedPtr<FJsonValue>>* Paths=nullptr;if(!Request->TryGetArrayField(TEXT("characters"),Paths))return false;
 FString AnimationPath;if(!Request->TryGetStringField(TEXT("animation"),AnimationPath))return false;
 TStrongObjectPtr<UKKImportedAnimationLibrary> Library(LoadObject<UKKImportedAnimationLibrary>(nullptr,*AnimationPath));
 if(!Library||Library->Animations.IsEmpty()||!Library->Animations[0].Sequence)return false;
 auto* Sequence=Library->Animations[0].Sequence.Get();const auto& Ref=Sequence->GetSkeleton()->GetReferenceSkeleton();
 TArray<TSharedPtr<FJsonValue>> Rows;
 for(const auto& Value:*Paths){FString Path;if(!Value->TryGetString(Path))return false;
  TStrongObjectPtr<UKKImportedCharacter> Character(LoadObject<UKKImportedCharacter>(nullptr,*Path));if(!Character)return false;
  FAssetCompilingManager::Get().FinishAllCompilation();
  OkRustBindings NativeAPI{};ok_register_bindings(OkUnrealBindings{},&NativeAPI);OkSession* Native=nullptr;if(!Character->Outfits.IsEmpty()&&!Character->Outfits[0].AnimationSnapshot.IsEmpty()){const auto& Bytes=Character->Outfits[0].AnimationSnapshot;Native=ok_session_open_animation_snapshot(Bytes.GetData(),Bytes.Num());if(Native){TArray<float> Body,Face;Body.SetNum(NativeAPI.shape_body(Native,nullptr,0));Face.SetNum(NativeAPI.shape_face(Native,nullptr,0));NativeAPI.shape_body(Native,Body.GetData(),Body.Num());NativeAPI.shape_face(Native,Face.GetData(),Face.Num());NativeAPI.apply_shape(Native,Body.GetData(),Body.Num(),Face.GetData(),Face.Num());}}auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("character"),Path);TArray<TSharedPtr<FJsonValue>> Parts;int64 Total=0;
  for(const auto& Outfit:Character->Outfits)for(const auto& Part:Outfit.Parts){auto P=MakeShared<FJsonObject>();P->SetStringField(TEXT("name"),Part.Name);P->SetStringField(TEXT("role"),Part.Role);P->SetStringField(TEXT("native_renderer"),Part.NativeRenderer);P->SetStringField(TEXT("rigid_follow"),Part.RigidFollow);P->SetNumberField(TEXT("native_part"),Part.NativePartIndex);P->SetNumberField(TEXT("outfit"),Outfit.Index);
   if(!Part.RigidFollow.IsEmpty()&&Part.RigidReference.Num()==16){const FName Socket(*Part.RigidFollow);if(Native){TArray<float> Neutral;Neutral.SetNum(16);if(ok_bone_world_matrix(Native,TCHAR_TO_UTF8(*Part.RigidFollow),Neutral.GetData())==0){P->SetNumberField(TEXT("native_neutral_vs_saved_max"),MatrixError(UnityMatrix(Neutral),UnityMatrix(Part.RigidReference)));P->SetNumberField(TEXT("native_neutral_vs_rest_max"),MatrixError(UnityMatrix(Neutral),SocketMatrix(Sequence,FName(*Part.RigidFollow),0,true)));P->SetStringField(TEXT("native_neutral_origin"),UnityMatrix(Neutral).GetOrigin().ToString());}}P->SetBoolField(TEXT("socket_in_skeleton"),Ref.FindBoneIndex(Socket)!=INDEX_NONE);P->SetNumberField(TEXT("saved_reference_vs_rest_max"),MatrixError(UnityMatrix(Part.RigidReference),SocketMatrix(Sequence,Socket,0,true)));FMatrix Blend(ForceInit);FAnimExtractContext C0(0.);C0.bEnforceCompressedDataSampling=true;for(int32 Axis=0;Axis<3;++Axis){const int32 B=Ref.FindBoneIndex(FName(*FString::Printf(TEXT("__ok_palette_%d_0_%d"),Part.NativePartIndex,Axis)));if(B!=INDEX_NONE){FTransform Pose;Sequence->GetBoneTransform(Pose,FSkeletonPoseBoneIndex(B),C0,false);const FMatrix M=Pose.ToMatrixWithScale();for(int32 R=0;R<4;++R)for(int32 C=0;C<4;++C)Blend.M[R][C]+=M.M[R][C]/3.;}}P->SetNumberField(TEXT("first_palette_vs_saved_socket_max"),MatrixError(Blend,UnityMatrix(Part.RigidReference).Inverse()*SocketMatrix(Sequence,Socket,0,false)));P->SetNumberField(TEXT("first_palette_vs_rest_socket_max"),MatrixError(Blend,SocketMatrix(Sequence,Socket,0,true).Inverse()*SocketMatrix(Sequence,Socket,0,false)));double MovingError=0;for(double Phase:{0.,.25,.5,.75,1.}){FAnimExtractContext Sample(Phase*Sequence->GetPlayLength());Sample.bEnforceCompressedDataSampling=true;FMatrix Actual(ForceInit);for(int32 Axis=0;Axis<3;++Axis){const int32 B=Ref.FindBoneIndex(FName(*FString::Printf(TEXT("__ok_palette_%d_0_%d"),Part.NativePartIndex,Axis)));if(B!=INDEX_NONE){FTransform Pose;Sequence->GetBoneTransform(Pose,FSkeletonPoseBoneIndex(B),Sample,false);const auto M=Pose.ToMatrixWithScale();for(int32 R=0;R<4;++R)for(int32 C=0;C<4;++C)Actual.M[R][C]+=M.M[R][C]/3.;}}MovingError=FMath::Max(MovingError,MatrixError(Actual,SocketMatrix(Sequence,Socket,0,true).Inverse()*SocketMatrix(Sequence,Socket,Phase*Sequence->GetPlayLength(),false)));}P->SetNumberField(TEXT("moving_palette_vs_rest_socket_max"),MovingError);if(const auto* Desc=Part.SkinnedModel?Part.SkinnedModel->GetMeshDescription(0):nullptr){FSkeletalMeshConstAttributes A(*Desc);FBox Before(ForceInit),After(ForceInit);for(auto V:Desc->Vertices().GetElementIDs()){const auto Position=FVector(A.GetVertexPositions()[V]);Before+=Position;After+=Blend.TransformPosition(Position);}P->SetStringField(TEXT("rigid_reference_center"),Before.GetCenter().ToString());P->SetStringField(TEXT("rigid_animated_center"),After.GetCenter().ToString());P->SetStringField(TEXT("rigid_animated_extent"),After.GetExtent().ToString());}}
   if(auto* Mesh=Part.SkinnedModel.Get()){const auto* Data=Mesh->GetResourceForRendering();if(!Data||Data->LODRenderData.IsEmpty())return false;int64 Triangles=0;for(const auto& S:Data->LODRenderData[0].RenderSections)Triangles+=S.NumTriangles;Total+=Triangles;P->SetStringField(TEXT("mesh"),Mesh->GetPathName());P->SetNumberField(TEXT("triangles"),double(Triangles));P->SetNumberField(TEXT("sections"),Data->LODRenderData[0].RenderSections.Num());P->SetNumberField(TEXT("vertices"),Data->LODRenderData[0].GetNumVertices());
    TArray<TSharedPtr<FJsonValue>> Tracks;
    if(!Part.RigidFollow.IsEmpty())for(int32 Axis=0;Axis<3;++Axis){const FName Name(*FString::Printf(TEXT("__ok_palette_%d_0_%d"),Part.NativePartIndex,Axis));const int32 Bone=Ref.FindBoneIndex(Name);auto T=MakeShared<FJsonObject>();T->SetStringField(TEXT("bone"),Name.ToString());T->SetBoolField(TEXT("present"),Bone!=INDEX_NONE);
     if(Bone!=INDEX_NONE){FTransform Start,Now;FAnimExtractContext C0(0.),C1(Sequence->GetPlayLength()*.37);C0.bEnforceCompressedDataSampling=true;C1.bEnforceCompressedDataSampling=true;Sequence->GetBoneTransform(Start,FSkeletonPoseBoneIndex(Bone),C0,false);Sequence->GetBoneTransform(Now,FSkeletonPoseBoneIndex(Bone),C1,false);T->SetNumberField(TEXT("translation_delta_cm"),FVector::Distance(Start.GetTranslation(),Now.GetTranslation()));T->SetNumberField(TEXT("rotation_delta_rad"),Start.GetRotation().AngularDistance(Now.GetRotation()));T->SetNumberField(TEXT("scale_delta"),FVector::Distance(Start.GetScale3D(),Now.GetScale3D()));}
     Tracks.Add(MakeShared<FJsonValueObject>(T));}
    P->SetArrayField(TEXT("rigid_palette_tracks"),Tracks);
   }else if(Part.Model)P->SetBoolField(TEXT("static_mesh"),true);
   Parts.Add(MakeShared<FJsonValueObject>(P));
  }if(Native)NativeAPI.session_free(Native);Row->SetArrayField(TEXT("parts"),Parts);Row->SetNumberField(TEXT("all_outfit_skeletal_triangles"),double(Total));Rows.Add(MakeShared<FJsonValueObject>(Row));
 }
 auto Report=MakeShared<FJsonObject>();Report->SetBoolField(TEXT("passed"),true);Report->SetBoolField(TEXT("source_assets_modified"),false);Report->SetStringField(TEXT("animation"),Sequence->GetPathName());Report->SetArrayField(TEXT("characters"),Rows);FString Json;return FJsonSerializer::Serialize(Report,TJsonWriterFactory<>::Create(&Json))&&FFileHelper::SaveStringToFile(Json,*Output);
}
