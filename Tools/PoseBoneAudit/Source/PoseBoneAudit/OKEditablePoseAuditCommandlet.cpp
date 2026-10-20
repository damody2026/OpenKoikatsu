#include "OKEditablePoseAuditCommandlet.h"
#include "Modules/ModuleManager.h"
#include "KKImportedCharacter.h"
#include "Engine/SkeletalMesh.h"
#include "SkeletalMeshAttributes.h"
#include "Animation/Skeleton.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimCompressionTypes.h"
#include "Animation/AnimBoneCompressionCodec.h"
#include "Animation/AnimSequenceDecompressionContext.h"
#include "AnimEncoding.h"
#include "HAL/IConsoleManager.h"
#include "Animation/AnimData/IAnimationDataModel.h"
#include "Engine/SkeletalMeshSocket.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "HAL/FileManager.h"
#include "UObject/StrongObjectPtr.h"
#include "Json.h"
IMPLEMENT_MODULE(FDefaultModuleImpl,PoseBoneAudit)
namespace {
bool CompareConstantKeyDecoder(UAnimSequence* Clip,const FCompressedAnimSequence& Data,TSharedRef<FJsonObject> Stats){
 // Compare existing implementations without changing assets or adopting a mode.
 auto* Variable=IConsoleManager::Get().FindConsoleVariable(TEXT("a.ConstantKeyLerp.ISPC"));
 if(!Variable||!Data.BoneCompressionCodec||Data.BoneCompressionCodec->GetClass()->GetName()!=TEXT("AnimCompress_BitwiseCompressOnly"))return false;
 const auto& Encoded=static_cast<const FUECompressedAnimData&>(*Data.CompressedDataStructure);
 if(Encoded.KeyEncodingFormat!=AKF_ConstantKeyLerp)return false;
 const auto& RefPoses=Clip->GetSkeleton()->GetRefLocalPoses();BoneTrackArray Pairs;
 int32 ConstantTranslation=0,ConstantRotation=0,ConstantScale=0;
 for(int32 Track=0;Track<Data.CompressedTrackToSkeletonMapTable.Num();++Track){
  const int32 Bone=Data.CompressedTrackToSkeletonMapTable[Track].BoneTreeIndex;
  if(!RefPoses.IsValidIndex(Bone)||!Encoded.CompressedTrackOffsets.IsValidIndex(Track*4+3))return false;
  Pairs.Add(BoneTrackPair(Bone,Track));ConstantTranslation+=Encoded.CompressedTrackOffsets[Track*4+1]==1;ConstantRotation+=Encoded.CompressedTrackOffsets[Track*4+3]==1;
  if(Encoded.CompressedScaleOffsets.IsValid())ConstantScale+=Encoded.CompressedScaleOffsets.GetOffsetData(Track,1)==1;
 }
 const int32 OriginalMode=Variable->GetInt();int64 Compared=0,Different=0;double MaxError=0;bool Finite=true;
 const auto Rate=Clip->GetSamplingFrameRate();const int32 Frames=Rate.AsFrameTime(Clip->GetPlayLength()).RoundToFrame().Value;
 FAnimSequenceDecompressionContext Context(Rate,Frames,Clip->Interpolation,NAME_None,*Data.CompressedDataStructure,RefPoses,Data.CompressedTrackToSkeletonMapTable,Clip->GetSkeleton(),Clip->IsValidAdditive(),Clip->GetAdditiveAnimType());
 // Includes start, interpolation points, just-before-end and loop endpoint.
 for(double Phase:{0.,.137,.37,.731,.99,1.}){
  Context.Seek(Clip->GetPlayLength()*Phase);TArray<FTransform> Expected=RefPoses,Actual=RefPoses;TArrayView<FTransform> ExpectedView(Expected),ActualView(Actual);
  Variable->Set(1,ECVF_SetByConsole);Data.BoneCompressionCodec->DecompressPose(Context,Pairs,Pairs,Pairs,ExpectedView);
  Variable->Set(0,ECVF_SetByConsole);Data.BoneCompressionCodec->DecompressPose(Context,Pairs,Pairs,Pairs,ActualView);
  for(const auto& Pair:Pairs){++Compared;const FMatrix44f A(Expected[Pair.AtomIndex].ToMatrixWithScale()),B(Actual[Pair.AtomIndex].ToMatrixWithScale());Different+=FMemory::Memcmp(&A,&B,sizeof(A))!=0;
   for(int32 Row=0;Row<4;++Row)for(int32 Column=0;Column<4;++Column){Finite&=FMath::IsFinite(A.M[Row][Column])&&FMath::IsFinite(B.M[Row][Column]);MaxError=FMath::Max(MaxError,FMath::Abs(double(A.M[Row][Column])-double(B.M[Row][Column])));}
  }
 }
 Variable->Set(OriginalMode,ECVF_SetByConsole);
 Stats->SetNumberField(TEXT("constant_translation_tracks"),ConstantTranslation);Stats->SetNumberField(TEXT("constant_rotation_tracks"),ConstantRotation);Stats->SetNumberField(TEXT("constant_scale_tracks"),ConstantScale);
 Stats->SetNumberField(TEXT("decoder_compared_matrices"),double(Compared));Stats->SetNumberField(TEXT("decoder_different_matrices"),double(Different));Stats->SetNumberField(TEXT("decoder_max_matrix_error"),MaxError);Stats->SetBoolField(TEXT("decoder_finite"),Finite);Stats->SetBoolField(TEXT("decoder_bit_exact"),Finite&&Different==0);
 Stats->SetBoolField(TEXT("decoder_mode_adopted"),false);return true;
}
}
UOKEditablePoseAuditCommandlet::UOKEditablePoseAuditCommandlet(){IsEditor=true;IsClient=false;IsServer=false;LogToConsole=true;}
int32 UOKEditablePoseAuditCommandlet::Main(const FString& Params){
 FString AssetRoot,Input,Output,Text;
 if(!FParse::Value(*Params,TEXT("AssetRoot="),AssetRoot)||!FParse::Value(*Params,TEXT("Input="),Input)||!FParse::Value(*Params,TEXT("Output="),Output)||IFileManager::Get().FileExists(*Output))return 1;
 AssetRoot=FPaths::ConvertRelativePathToFull(AssetRoot);if(!IFileManager::Get().DirectoryExists(*AssetRoot))return 1;
 FPackageName::RegisterMountPoint(TEXT("/Game/"),AssetRoot/TEXT(""));
 TSharedPtr<FJsonObject> Request;const TArray<TSharedPtr<FJsonValue>>* Cards=nullptr;
 if(!FFileHelper::LoadFileToString(Text,*Input)||!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Request)||!Request||!Request->TryGetArrayField(TEXT("cards"),Cards)||Cards->IsEmpty())return 1;
 TArray<TSharedPtr<FJsonValue>> Rows;int64 FullTotal=0,ClosureTotal=0,LowTotal=0;int32 Failures=0;
 for(const auto& Value:*Cards){
  {auto Item=Value->AsObject();FString LowPath,NearPath;if(!Item||!Item->TryGetStringField(TEXT("low"),LowPath)||!LowPath.StartsWith(TEXT("/Game/")))return 1;Item->TryGetStringField(TEXT("near"),NearPath);
   TStrongObjectPtr<UKKImportedCharacter> Low(LoadObject<UKKImportedCharacter>(nullptr,*LowPath));TStrongObjectPtr<UKKImportedCharacter> Near(NearPath.IsEmpty()?nullptr:LoadObject<UKKImportedCharacter>(nullptr,*NearPath));
   if(!Low||Low->Outfits.IsEmpty()||(!NearPath.IsEmpty()&&(!Near||Near->SourceHash!=Low->SourceHash)))return 1;
   const FKKImportedPart* Leader=nullptr;for(const auto& Part:Low->Outfits[0].Parts)if(Part.SkinnedModel){Leader=&Part;break;}if(!Leader)return 1;
   const auto& Ref=Leader->SkinnedModel->GetRefSkeleton();TSet<int32> LowBones,AllBones;TArray<TSharedPtr<FJsonValue>> Parts;int32 Missing=0,Unsupported=0;
   auto Include=[&](TSet<int32>& Bones,FName Name){int32 Bone=Ref.FindBoneIndex(Name);if(Bone<0){++Missing;return;}while(Bone>=0&&!Bones.Contains(Bone)){Bones.Add(Bone);Bone=Ref.GetParentIndex(Bone);}};
   auto Scan=[&](const UKKImportedCharacter* Character,bool IsNear){for(const auto& Outfit:Character->Outfits)for(const auto& Part:Outfit.Parts)if(auto* Mesh=Part.SkinnedModel.Get()){
    auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("mesh"),Mesh->GetPathName());Row->SetBoolField(TEXT("near"),IsNear);TSet<FName> Used;int64 Positive=0;const auto& PartRef=Mesh->GetRefSkeleton();const auto* Desc=Mesh->GetMeshDescription(0);
    if(!Desc){++Unsupported;}else{FSkeletalMeshConstAttributes Attributes(*Desc);for(auto Vertex:Desc->Vertices().GetElementIDs())for(auto Weight:Attributes.GetVertexSkinWeights().Get(Vertex))if(Weight.GetRawWeight()>0){const int32 Bone=Weight.GetBoneIndex();if(Bone<0||Bone>=PartRef.GetNum()){++Missing;continue;}Used.Add(PartRef.GetBoneName(Bone));++Positive;}}
    for(FName Name:Used){Include(AllBones,Name);if(!IsNear)Include(LowBones,Name);}
    // Explicitly expose non-render requirements. No pruning is allowed if these
    // exist without a complete component/runtime policy audit.
    Row->SetBoolField(TEXT("physics_asset"),Mesh->GetPhysicsAsset()!=nullptr);Row->SetNumberField(TEXT("sockets"),Mesh->GetActiveSocketList().Num());Row->SetNumberField(TEXT("virtual_bones"),PartRef.GetRequiredVirtualBones().Num());
    Row->SetNumberField(TEXT("skeleton_bones"),PartRef.GetNum());Row->SetNumberField(TEXT("positive_weight_bones"),Used.Num());Row->SetNumberField(TEXT("positive_influences"),double(Positive));Parts.Add(MakeShared<FJsonValueObject>(Row));
   }};
   Scan(Low.Get(),false);if(Near)Scan(Near.Get(),true);
   auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("source_hash"),Low->SourceHash);Row->SetStringField(TEXT("low"),LowPath);Row->SetStringField(TEXT("near"),NearPath);Row->SetNumberField(TEXT("leader_bones"),Ref.GetNum());Row->SetNumberField(TEXT("low_weight_parent_closure"),LowBones.Num());Row->SetNumberField(TEXT("low_near_weight_parent_closure"),AllBones.Num());Row->SetNumberField(TEXT("mapping_gaps"),Missing);Row->SetNumberField(TEXT("missing_editable_parts"),Unsupported);Row->SetArrayField(TEXT("parts"),Parts);
   Row->SetStringField(TEXT("leader_mesh"),Leader->SkinnedModel->GetPathName());TArray<TSharedPtr<FJsonValue>> Names,Parents,Needed;
   for(int32 Bone=0;Bone<Ref.GetNum();++Bone){Names.Add(MakeShared<FJsonValueString>(Ref.GetBoneName(Bone).ToString()));Parents.Add(MakeShared<FJsonValueNumber>(Ref.GetParentIndex(Bone)));if(AllBones.Contains(Bone))Needed.Add(MakeShared<FJsonValueNumber>(Bone));}
   Row->SetArrayField(TEXT("leader_names"),Names);Row->SetArrayField(TEXT("leader_parents"),Parents);Row->SetArrayField(TEXT("required_indices"),Needed);
   if(FParse::Param(*Params,TEXT("AuditAnimation"))){
    FString WalkPath;if(!Item->TryGetStringField(TEXT("walk"),WalkPath))WalkPath=FPaths::GetPath(FPackageName::ObjectPathToPackageName(LowPath))/TEXT("Walk/Animations.Animations");
    TStrongObjectPtr<UKKImportedAnimationLibrary> Walk(LoadObject<UKKImportedAnimationLibrary>(nullptr,*WalkPath));if(!Walk||Walk->Character!=Low.Get())return 1;TArray<TSharedPtr<FJsonValue>> Clips;
    for(const auto& Entry:Walk->Animations){auto* Clip=Entry.Sequence.Get();if(!Clip||!Clip->GetSkeleton())return 1;Clip->CacheDerivedDataForCurrentPlatform();Clip->WaitOnExistingCompression();const auto Data=Clip->GetCompressedData();const auto* Model=Clip->GetDataModelInterface().GetInterface();if(!Model||!Data.Get().IsValid(Clip))return 1;
     auto Stats=MakeShared<FJsonObject>();Stats->SetStringField(TEXT("sequence"),Clip->GetPathName());Stats->SetStringField(TEXT("codec"),Data.Get().BoneCompressionCodec?Data.Get().BoneCompressionCodec->GetClass()->GetName():TEXT("none"));Stats->SetNumberField(TEXT("skeleton_bones"),Clip->GetSkeleton()->GetReferenceSkeleton().GetNum());Stats->SetNumberField(TEXT("raw_tracks"),Model->GetBoneAnimationTracks().Num());Stats->SetNumberField(TEXT("compressed_tracks"),Data.Get().CompressedTrackToSkeletonMapTable.Num());Stats->SetNumberField(TEXT("compressed_bytes"),Data.Get().CompressedByteStream.Num());
     int32 LeaderTracks=0;for(const auto& Track:Model->GetBoneAnimationTracks())if(Ref.FindBoneIndex(Track.Name)!=INDEX_NONE)++LeaderTracks;Stats->SetNumberField(TEXT("tracks_in_leader"),LeaderTracks);Clips.Add(MakeShared<FJsonValueObject>(Stats));
     Stats->SetBoolField(TEXT("raw_track_view_available"),!Model->GetBoneAnimationTracks().IsEmpty());Stats->SetStringField(TEXT("model_class"),Clip->GetDataModelInterface().GetObject()->GetClass()->GetName());int32 CompressedLeaderTracks=0;const auto& ClipRef=Clip->GetSkeleton()->GetReferenceSkeleton();for(const auto& Map:Data.Get().CompressedTrackToSkeletonMapTable)if(Map.BoneTreeIndex>=0&&Map.BoneTreeIndex<ClipRef.GetNum()&&Ref.FindBoneIndex(ClipRef.GetBoneName(Map.BoneTreeIndex))!=INDEX_NONE)++CompressedLeaderTracks;Stats->SetNumberField(TEXT("compressed_tracks_in_leader"),CompressedLeaderTracks);
     if(FParse::Param(*Params,TEXT("CompareConstantKeyDecoder"))&&!CompareConstantKeyDecoder(Clip,Data.Get(),Stats)){UE_LOG(LogTemp,Error,TEXT("Unsupported decoder comparison for %s"),*Clip->GetPathName());return 1;}
    }Row->SetArrayField(TEXT("animations"),Clips);
   }
   Rows.Add(MakeShared<FJsonValueObject>(Row));FullTotal+=Ref.GetNum();LowTotal+=LowBones.Num();ClosureTotal+=AllBones.Num();Failures+=Missing+Unsupported;
   UE_LOG(LogTemp,Display,TEXT("Editable pose audit card=%d full=%d low=%d low_near=%d gaps=%d unavailable=%d"),Rows.Num(),Ref.GetNum(),LowBones.Num(),AllBones.Num(),Missing,Unsupported);
  }CollectGarbage(RF_NoFlags);
 }
 auto Report=MakeShared<FJsonObject>();Report->SetNumberField(TEXT("schema"),1);Report->SetNumberField(TEXT("leader_bones"),double(FullTotal));Report->SetNumberField(TEXT("low_weight_parent_closure"),double(LowTotal));Report->SetNumberField(TEXT("low_near_weight_parent_closure"),double(ClosureTotal));Report->SetNumberField(TEXT("failures"),Failures);Report->SetArrayField(TEXT("cards"),Rows);Report->SetBoolField(TEXT("assets_modified"),false);Report->SetBoolField(TEXT("safe_pruning_proven"),false);
 FString Json;FJsonSerializer::Serialize(Report,TJsonWriterFactory<>::Create(&Json));IFileManager::Get().MakeDirectory(*FPaths::GetPath(Output),true);if(!FFileHelper::SaveStringToFile(Json,*Output))return 1;return Failures?2:0;
}
