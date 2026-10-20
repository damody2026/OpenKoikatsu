#include "OKCrowdCompact.h"
#include "KKImportedCharacter.h"
#include "Misc/Parse.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "UObject/StrongObjectPtr.h"
#if WITH_EDITOR
#include "Engine/SkeletalMesh.h"
#include "Animation/MorphTarget.h"
#include "ReferenceSkeleton.h"
#include "SkeletalMeshAttributes.h"
#include "Rendering/SkeletalMeshModel.h"
#include "Rendering/SkeletalMeshLODModel.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "AssetCompilingManager.h"
#include "UObject/SavePackage.h"

namespace {
template<class T>T* NewAsset(const FString& Path){
 if(FPackageName::DoesPackageExist(Path))return nullptr;
 return NewObject<T>(CreatePackage(*Path),*FPackageName::GetLongPackageAssetName(Path),RF_Public|RF_Standalone);
}
bool SaveAsset(UObject* Asset){
 if(!Asset)return false;
 const FString File=FPackageName::LongPackageNameToFilename(Asset->GetOutermost()->GetName(),FPackageName::GetAssetPackageExtension());
 if(IFileManager::Get().FileExists(*File))return false;
 IFileManager::Get().MakeDirectory(*FPaths::GetPath(File),true);
 FSavePackageArgs Args;Args.TopLevelFlags=RF_Public|RF_Standalone;
 return UPackage::SavePackage(Asset->GetOutermost(),Asset,*File,Args);
}
USkeletalMesh* CompactMesh(USkeletalMesh* Source,const FString& Path,FString& Error,const TSet<FName>* AdditionalBones=nullptr){
 if(!Source||!Source->GetMeshDescription(0)){Error=TEXT("Missing editable saved skin");return nullptr;}
 FMeshDescription Desc=*Source->GetMeshDescription(0);FSkeletalMeshAttributes Attributes(Desc);
 const auto& Full=Source->GetRefSkeleton();TSet<int32> Used;Used.Add(0);
 auto Weights=Attributes.GetVertexSkinWeights();
 if(AdditionalBones)for(const FName Name:*AdditionalBones){int32 Bone=Full.FindBoneIndex(Name);if(Bone==INDEX_NONE){Error=TEXT("Missing follower private bone: ")+Name.ToString();return nullptr;}
  while(Bone!=INDEX_NONE&&!Used.Contains(Bone)){Used.Add(Bone);Bone=Full.GetParentIndex(Bone);}}
 for(auto Vertex:Desc.Vertices().GetElementIDs())for(const auto Weight:Weights.Get(Vertex)){
  int32 Bone=Weight.GetBoneIndex();
  if(Bone>=Full.GetNum()){Error=TEXT("Source skin bone outside reference");return nullptr;}
  while(Bone!=INDEX_NONE&&!Used.Contains(Bone)){Used.Add(Bone);Bone=Full.GetParentIndex(Bone);}
 }
 TArray<int32> Bones=Used.Array();Bones.Sort();TArray<int32> Remap;Remap.Init(INDEX_NONE,Full.GetNum());
 FReferenceSkeleton Compact;
 {FReferenceSkeletonModifier Modifier(Compact,Source->GetSkeleton());
  for(int32 New=0;New<Bones.Num();++New){const int32 Old=Bones[New],Parent=Full.GetParentIndex(Old);Remap[Old]=New;
   Modifier.Add(FMeshBoneInfo(Full.GetBoneName(Old),Full.GetBoneName(Old).ToString(),Parent==INDEX_NONE?INDEX_NONE:Remap[Parent]),Full.GetRefBonePose()[Old],true);
  }
 }
 for(auto Vertex:Desc.Vertices().GetElementIDs()){
  TArray<UE::AnimationCore::FBoneWeight> Mapped;
  for(const auto Weight:Weights.Get(Vertex))Mapped.Add(UE::AnimationCore::FBoneWeight(static_cast<FBoneIndexType>(Remap[Weight.GetBoneIndex()]),Weight.GetRawWeight()));
  Weights.SetRaw(Vertex,Mapped);
 }
 const int32 OriginalBones=Attributes.GetNumBones();
 for(int32 Bone=0;Bone<OriginalBones;++Bone)Attributes.DeleteBone(FBoneID(Bone));
 for(int32 Bone=0;Bone<Compact.GetNum();++Bone){const FBoneID Id(Bone);Attributes.CreateBone(Id);
  Attributes.GetBoneNames()[Id]=Compact.GetBoneName(Bone);Attributes.GetBoneParentIndices()[Id]=Compact.GetParentIndex(Bone);Attributes.GetBonePoses()[Id]=Compact.GetRefBonePose()[Bone];}
 auto* Mesh=NewAsset<USkeletalMesh>(Path);if(!Mesh){Error=TEXT("Compact mesh output already exists");return nullptr;}
 Mesh->SetSkeleton(Source->GetSkeleton());Mesh->SetRefSkeleton(Compact);Mesh->CalculateInvRefMatrices();Mesh->AddLODInfo();
 Mesh->GetMaterials()=Source->GetMaterials();Mesh->SetHasVertexColors(Source->GetHasVertexColors());
 *Mesh->GetLODInfo(0)=*Source->GetLODInfo(0);Mesh->GetImportedModel()->LODModels.Add(new FSkeletalMeshLODModel());
 Mesh->CreateMeshDescription(0,MoveTemp(Desc));Mesh->CommitMeshDescription(0);Mesh->Build();Mesh->PostEditChange();
 FAssetCompilingManager::Get().FinishAllCompilation();
 const auto& Before=Source->GetResourceForRendering()->LODRenderData[0];const auto& After=Mesh->GetResourceForRendering()->LODRenderData[0];
 int64 BeforeTriangles=0,AfterTriangles=0;for(const auto& Section:Before.RenderSections)BeforeTriangles+=Section.NumTriangles;for(const auto& Section:After.RenderSections)AfterTriangles+=Section.NumTriangles;
 if(Before.GetNumVertices()!=After.GetNumVertices()||BeforeTriangles!=AfterTriangles||Mesh->GetMorphTargets().Num()!=Source->GetMorphTargets().Num()){
  Error=FString::Printf(TEXT("Compact skin coverage changed: %s verts=%u/%u triangles=%lld/%lld morphs=%d/%d"),*Source->GetName(),Before.GetNumVertices(),After.GetNumVertices(),BeforeTriangles,AfterTriangles,Source->GetMorphTargets().Num(),Mesh->GetMorphTargets().Num());return nullptr;
 }
 UE_LOG(LogTemp,Display,TEXT("Crowd compact skin %s bones=%d/%d vertices=%u triangles=%lld"),*Source->GetName(),Compact.GetNum(),Full.GetNum(),After.GetNumVertices(),AfterTriangles);
 return Mesh;
}
}
USkeletalMesh* OKCompactCrowdMesh(USkeletalMesh* Source,const FString& Path,FString& Error,const TSet<FName>& AdditionalBones){return CompactMesh(Source,Path,Error,&AdditionalBones);}
#endif
UOKCompactCrowdCommandlet::UOKCompactCrowdCommandlet(){IsEditor=true;IsClient=true;LogToConsole=true;}
int32 UOKCompactCrowdCommandlet::Main(const FString& Params){
#if WITH_EDITOR
 int32 Start=0,End=100;FParse::Value(*Params,TEXT("StartIndex="),Start);FParse::Value(*Params,TEXT("EndIndex="),End);if(Start<0||Start>=End||End>100)return 1;
 for(int32 Index=Start;Index<End;++Index){
  const FString Base=FString::Printf(TEXT("/Game/Scenes/CrowdRoom/Characters/C%03d"),Index),Target=Base/TEXT("Compact");
  const FString CharacterPath=Target/TEXT("Character"),LibraryPath=Target/TEXT("Walk/Animations");
  {TStrongObjectPtr<UKKImportedCharacter> Original(LoadObject<UKKImportedCharacter>(nullptr,*(Base/TEXT("Character.Character"))));
   TStrongObjectPtr<UKKImportedAnimationLibrary> Walk(LoadObject<UKKImportedAnimationLibrary>(nullptr,*(Base/TEXT("Walk/Animations.Animations"))));
   if(!Original||!Walk||Walk->Character!=Original.Get()||Original->Schema!=2)return 1;
   if(FPackageName::DoesPackageExist(CharacterPath)){
    auto* Existing=LoadObject<UKKImportedCharacter>(nullptr,*(CharacterPath+TEXT(".Character")));
    if(!Existing||Existing->SourceHash!=Original->SourceHash||!FPackageName::DoesPackageExist(LibraryPath))return 1;
   }else{
    auto* Character=NewAsset<UKKImportedCharacter>(CharacterPath);Character->Schema=Original->Schema;Character->SourceHash=Original->SourceHash;Character->Outfits=Original->Outfits;
    TArray<UObject*> Assets;FString Error;
    for(auto& Outfit:Character->Outfits){bool Leader=false;
     for(int32 Part=0;Part<Outfit.Parts.Num();++Part){auto& Entry=Outfit.Parts[Part];if(!Entry.SkinnedModel)continue;
      if(!Leader){Leader=true;continue;} // The leader keeps the full private rig.
      auto* Mesh=CompactMesh(Entry.SkinnedModel,Target/FString::Printf(TEXT("Skins/SK_%03d_%03d"),Outfit.Index,Part),Error);
      if(!Mesh){UE_LOG(LogTemp,Error,TEXT("Crowd compact index=%d part=%d: %s"),Index,Part,*Error);return 1;}
      Entry.SkinnedModel=Mesh;Assets.Add(Mesh);
     }
    }
    auto* Library=NewAsset<UKKImportedAnimationLibrary>(LibraryPath);Library->Character=Character;Library->Animations=Walk->Animations;
    // Character is the completion manifest; publish it only after all resources.
    Assets.Add(Library);Assets.Add(Character);
    for(auto* Asset:Assets)if(!SaveAsset(Asset)){UE_LOG(LogTemp,Error,TEXT("Crowd compact save failed %s"),*Asset->GetPathName());return 1;}
   }
  }
  CollectGarbage(RF_NoFlags);UE_LOG(LogTemp,Display,TEXT("Crowd compact complete index=%d"),Index);
 }
 UE_LOG(LogTemp,Display,TEXT("Crowd compact batch complete start=%d end=%d"),Start,End);return 0;
#else
 return 1;
#endif
}
