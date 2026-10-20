#include "OKCrowdPacked.h"
#include "OKCrowdCompact.h"
#include "OKCrowdQuality.h"
#include "OKCrowdHairOutline.h"
#include "OKCrowdGpuPose.h"
#include "OKCrowdScene.h"
#include "KKImportedCharacter.h"
#include "Misc/Parse.h"
#include "Misc/PackageName.h"
#include "HAL/FileManager.h"
#include "UObject/StrongObjectPtr.h"
#if WITH_EDITOR
#include "Engine/SkeletalMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimData/IAnimationDataModel.h"
#include "Animation/AnimData/IAnimationDataController.h"
#include "Animation/Skeleton.h"
#include "KKImportedAnimSequence.h"
#include "Components/SkeletalMeshComponent.h"
#include "SkeletalMeshAttributes.h"
#include "Rendering/SkeletalMeshModel.h"
#include "Rendering/SkeletalMeshLODModel.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "SkeletalRenderPublic.h"
#include "AssetCompilingManager.h"
#include "UObject/SavePackage.h"
#include "Materials/MaterialInterface.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Json.h"
#include "Async/ParallelFor.h"

namespace {
template<class T>T* FreshPackedAsset(const FString& Path){
 return FPackageName::DoesPackageExist(Path)?nullptr:NewObject<T>(CreatePackage(*Path),*FPackageName::GetLongPackageAssetName(Path),RF_Public|RF_Standalone);
}
bool SavePackedAsset(UObject* Asset){
 if(!Asset)return false;
 const FString File=FPackageName::LongPackageNameToFilename(Asset->GetOutermost()->GetName(),FPackageName::GetAssetPackageExtension());
 if(IFileManager::Get().FileExists(*File))return false;
 IFileManager::Get().MakeDirectory(*FPaths::GetPath(File),true);
 FSavePackageArgs Args;Args.TopLevelFlags=RF_Public|RF_Standalone;return UPackage::SavePackage(Asset->GetOutermost(),Asset,*File,Args);
}
struct FSourceSkin {USkeletalMesh* Mesh=nullptr;int32 MaterialOffset=0;TMap<int32,int32> Vertices;};
bool Eligible(const FKKImportedPart& Part){return Part.SkinnedModel&&Part.MouthDefaultVisible&&Part.MouthPatterns.IsEmpty()&&Part.Transform.Equals(FTransform::Identity,1e-6);}

bool SameSkinSource(USkeletalMesh* Original,USkeletalMesh* Compact,bool CompareMaterials=true){
 if(!Original||!Compact||(CompareMaterials&&Original->GetMaterials()!=Compact->GetMaterials()))return false;
 const auto* A=Original->GetMeshDescription(0);const auto* B=Compact->GetMeshDescription(0);if(!A||!B||A->Vertices().Num()!=B->Vertices().Num()||A->VertexInstances().Num()!=B->VertexInstances().Num()||A->Triangles().Num()!=B->Triangles().Num())return false;
 FSkeletalMeshConstAttributes X(*A),Y(*B);const auto& OriginalRef=Original->GetRefSkeleton();const auto& CompactRef=Compact->GetRefSkeleton();
 for(int32 Bone=0;Bone<CompactRef.GetNum();++Bone){const int32 Other=OriginalRef.FindBoneIndex(CompactRef.GetBoneName(Bone));if(Other==INDEX_NONE||!CompactRef.GetRefBonePose()[Bone].Equals(OriginalRef.GetRefBonePose()[Other],1e-6))return false;
  const int32 Parent=CompactRef.GetParentIndex(Bone),OtherParent=OriginalRef.GetParentIndex(Other);
  if((Parent==INDEX_NONE)!=(OtherParent==INDEX_NONE)||(Parent!=INDEX_NONE&&CompactRef.GetBoneName(Parent)!=OriginalRef.GetBoneName(OtherParent)))return false;
 }
 if(X.GetMorphTargetNames()!=Y.GetMorphTargetNames()||X.GetVertexInstanceUVs().GetNumChannels()!=Y.GetVertexInstanceUVs().GetNumChannels())return false;
 for(auto Vertex:A->Vertices().GetElementIDs()){
  if(!B->IsVertexValid(Vertex)||X.GetVertexPositions()[Vertex]!=Y.GetVertexPositions()[Vertex])return false;
  TMap<FName,uint16> Weights;for(const auto Weight:X.GetVertexSkinWeights().Get(Vertex))Weights.Add(OriginalRef.GetBoneName(Weight.GetBoneIndex()),Weight.GetRawWeight());
  const auto OtherWeights=Y.GetVertexSkinWeights().Get(Vertex);if(Weights.Num()!=OtherWeights.Num())return false;
  for(const auto Weight:OtherWeights){const auto* Expected=Weights.Find(CompactRef.GetBoneName(Weight.GetBoneIndex()));if(!Expected||*Expected!=Weight.GetRawWeight())return false;}
  for(const FName Morph:X.GetMorphTargetNames())if(X.GetVertexMorphPositionDelta(Morph)[Vertex]!=Y.GetVertexMorphPositionDelta(Morph)[Vertex])return false;
 }
 for(auto Instance:A->VertexInstances().GetElementIDs()){
  if(!B->IsVertexInstanceValid(Instance)||A->GetVertexInstanceVertex(Instance)!=B->GetVertexInstanceVertex(Instance)||X.GetVertexInstanceNormals()[Instance]!=Y.GetVertexInstanceNormals()[Instance]||X.GetVertexInstanceTangents()[Instance]!=Y.GetVertexInstanceTangents()[Instance]||X.GetVertexInstanceBinormalSigns()[Instance]!=Y.GetVertexInstanceBinormalSigns()[Instance]||X.GetVertexInstanceColors()[Instance]!=Y.GetVertexInstanceColors()[Instance])return false;
  for(int32 UV=0;UV<X.GetVertexInstanceUVs().GetNumChannels();++UV)if(X.GetVertexInstanceUVs().Get(Instance,UV)!=Y.GetVertexInstanceUVs().Get(Instance,UV))return false;
 }
 for(auto Triangle:A->Triangles().GetElementIDs()){
  if(!B->IsTriangleValid(Triangle)||A->GetTrianglePolygonGroup(Triangle)!=B->GetTrianglePolygonGroup(Triangle))return false;
  const auto Left=A->GetTriangleVertexInstances(Triangle),Right=B->GetTriangleVertexInstances(Triangle);for(int32 Corner=0;Corner<3;++Corner)if(Left[Corner]!=Right[Corner])return false;
 }
 return Original->GetResourceForRendering()->LODRenderData[0].GetNumVertices()==Compact->GetResourceForRendering()->LODRenderData[0].GetNumVertices();
}

USkeletalMesh* Pack(const FKKImportedOutfit& Outfit,const FString& Path,TArray<FSourceSkin>& Sources,FString& Error){
 const auto* First=Outfit.Parts.FindByPredicate([](const auto& Part){return Part.SkinnedModel!=nullptr;});
 if(!First)return nullptr;
 const auto& Reference=First->SkinnedModel->GetRefSkeleton();
 FMeshDescription Desc;FSkeletalMeshAttributes Target(Desc);Target.Register(true);Target.GetVertexInstanceUVs().SetNumChannels(4);
 for(int32 Bone=0;Bone<Reference.GetNum();++Bone){const FBoneID Id=Target.CreateBone();Target.GetBoneNames()[Id]=Reference.GetBoneName(Bone);Target.GetBoneParentIndices()[Id]=Reference.GetParentIndex(Bone);Target.GetBonePoses()[Id]=Reference.GetRefBonePose()[Bone];}
 auto* Mesh=FreshPackedAsset<USkeletalMesh>(Path);if(!Mesh){Error=TEXT("Packed mesh output exists");return nullptr;}
 int64 ExpectedVertices=0,ExpectedTriangles=0;
 TSet<FName> Morphs;
 for(const auto& Part:Outfit.Parts)if(Eligible(Part)){
  auto* Source=Part.SkinnedModel.Get();const auto* SourceDesc=Source->GetMeshDescription(0);if(!SourceDesc){Error=TEXT("Missing source mesh description");return nullptr;}
  FSkeletalMeshConstAttributes Input(*SourceDesc);const auto& Ref=Source->GetRefSkeleton();TArray<int32> BoneMap;
  for(int32 Bone=0;Bone<Ref.GetNum();++Bone){const int32 Mapped=Reference.FindBoneIndex(Ref.GetBoneName(Bone));
   if(Mapped==INDEX_NONE||!Ref.GetRefBonePose()[Bone].Equals(Reference.GetRefBonePose()[Mapped],1e-6)){Error=TEXT("Packed private reference pose differs");return nullptr;}
   const int32 Parent=Ref.GetParentIndex(Bone),MappedParent=Reference.GetParentIndex(Mapped);
   if((Parent==INDEX_NONE)!=(MappedParent==INDEX_NONE)||(Parent!=INDEX_NONE&&Ref.GetBoneName(Parent)!=Reference.GetBoneName(MappedParent))){Error=TEXT("Packed private reference hierarchy differs");return nullptr;}
   BoneMap.Add(Mapped);
  }
  for(const FName Morph:Input.GetMorphTargetNames()){
   if(Input.HasMorphTargetNormalsAttribute(Morph)){Error=TEXT("Explicit morph normal data needs a matching merge path");return nullptr;}
   if(!Morphs.Contains(Morph)){Target.RegisterMorphTargetAttribute(Morph,false);Morphs.Add(Morph);}
  }
  const int32 MaterialOffset=Mesh->GetMaterials().Num();Sources.Add({Source,MaterialOffset,{}});
  for(const auto& Material:Source->GetMaterials()){auto Copy=Material;Copy.MaterialSlotName=FName(*FString::Printf(TEXT("PackedSlot%d"),Mesh->GetMaterials().Num()));Copy.ImportedMaterialSlotName=Copy.MaterialSlotName;Mesh->GetMaterials().Add(Copy);}
  TMap<FVertexID,FVertexID> Vertices;TMap<FVertexInstanceID,FVertexInstanceID> Instances;TMap<FPolygonGroupID,FPolygonGroupID> Groups;
  for(auto Id:SourceDesc->Vertices().GetElementIDs()){
   const auto New=Desc.CreateVertex();Vertices.Add(Id,New);Sources.Last().Vertices.Add(Id.GetValue(),New.GetValue());Target.GetVertexPositions()[New]=Input.GetVertexPositions()[Id];
   TArray<UE::AnimationCore::FBoneWeight> Weights;for(const auto Weight:Input.GetVertexSkinWeights().Get(Id))Weights.Add(UE::AnimationCore::FBoneWeight(static_cast<FBoneIndexType>(BoneMap[Weight.GetBoneIndex()]),Weight.GetRawWeight()));
   Target.GetVertexSkinWeights().SetRaw(New,Weights);
   for(const FName Morph:Input.GetMorphTargetNames())Target.GetVertexMorphPositionDelta(Morph)[New]=Input.GetVertexMorphPositionDelta(Morph)[Id];
  }
  for(auto Id:SourceDesc->VertexInstances().GetElementIDs()){
   const auto New=Desc.CreateVertexInstance(Vertices[SourceDesc->GetVertexInstanceVertex(Id)]);Instances.Add(Id,New);
   Target.GetVertexInstanceNormals()[New]=Input.GetVertexInstanceNormals()[Id];Target.GetVertexInstanceTangents()[New]=Input.GetVertexInstanceTangents()[Id];
   Target.GetVertexInstanceBinormalSigns()[New]=Input.GetVertexInstanceBinormalSigns()[Id];Target.GetVertexInstanceColors()[New]=Input.GetVertexInstanceColors()[Id];
   for(int32 UV=0;UV<Input.GetVertexInstanceUVs().GetNumChannels();++UV)Target.GetVertexInstanceUVs().Set(New,UV,Input.GetVertexInstanceUVs().Get(Id,UV));
  }
  for(auto Id:SourceDesc->PolygonGroups().GetElementIDs()){
   const FName Slot=Input.GetPolygonGroupMaterialSlotNames()[Id];const int32 Material=Source->GetMaterials().IndexOfByPredicate([&](const auto& Entry){return Entry.ImportedMaterialSlotName==Slot||Entry.MaterialSlotName==Slot;});
   if(Material==INDEX_NONE){Error=TEXT("Source section material mapping is absent");return nullptr;}
   const auto New=Desc.CreatePolygonGroup();Groups.Add(Id,New);Target.GetPolygonGroupMaterialSlotNames()[New]=Mesh->GetMaterials()[MaterialOffset+Material].MaterialSlotName;
  }
  for(auto Id:SourceDesc->Triangles().GetElementIDs()){TArray<FVertexInstanceID> Corners;for(auto Corner:SourceDesc->GetTriangleVertexInstances(Id))Corners.Add(Instances[Corner]);Desc.CreateTriangle(Groups[SourceDesc->GetTrianglePolygonGroup(Id)],Corners);}
  const auto& Render=Source->GetResourceForRendering()->LODRenderData[0];ExpectedVertices+=Render.GetNumVertices();for(const auto& Section:Render.RenderSections)ExpectedTriangles+=Section.NumTriangles;
 }
 if(Sources.Num()<2){Error=TEXT("Not enough eligible parts to pack");return nullptr;}
 Mesh->SetSkeleton(First->SkinnedModel->GetSkeleton());Mesh->SetRefSkeleton(Reference);Mesh->CalculateInvRefMatrices();Mesh->SetHasVertexColors(true);Mesh->AddLODInfo();*Mesh->GetLODInfo(0)=*First->SkinnedModel->GetLODInfo(0);
 Mesh->GetImportedModel()->LODModels.Add(new FSkeletalMeshLODModel());Mesh->CreateMeshDescription(0,MoveTemp(Desc));Mesh->CommitMeshDescription(0);Mesh->Build();Mesh->PostEditChange();FAssetCompilingManager::Get().FinishAllCompilation();
 const auto& Render=Mesh->GetResourceForRendering()->LODRenderData[0];int64 Triangles=0;for(const auto& Section:Render.RenderSections)Triangles+=Section.NumTriangles;
 if(Render.GetNumVertices()!=ExpectedVertices||Triangles!=ExpectedTriangles||Mesh->GetMorphTargets().Num()!=Morphs.Num()){Error=FString::Printf(TEXT("Packed coverage changed vertices=%u/%lld triangles=%lld/%lld morphs=%d/%d"),Render.GetNumVertices(),ExpectedVertices,Triangles,ExpectedTriangles,Mesh->GetMorphTargets().Num(),Morphs.Num());return nullptr;}
 return Mesh;
}

bool Audit(UKKImportedCharacter* Original,UKKImportedAnimationLibrary* OriginalWalk,UKKImportedCharacter* Packed,UKKImportedAnimationLibrary* PackedWalk,const TArray<FSourceSkin>& Sources,USkeletalMesh* Mesh,FString& Error){
 const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).CreateNavigation(false).CreateAISystem(false);
 auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
 auto Spawn=[&](UKKImportedCharacter* Card,UKKImportedAnimationLibrary* Walk){auto* Actor=World->SpawnActorDeferred<AKKImportedCharacterActor>(AKKImportedCharacterActor::StaticClass(),FTransform::Identity);Actor->Character=Card;Actor->AnimationLibrary=Walk;Actor->PlaySkeletalAnimation=true;Actor->FinishSpawning(FTransform::Identity);return Actor;};
 auto* Before=Spawn(Original,OriginalWalk);auto* After=Spawn(Packed,PackedWalk);bool Good=true;int64 Compared=0;double Maximum=0.;
 TArray<USkeletalMeshComponent*> SourceComponents,TargetComponents;Before->GetComponents(SourceComponents);After->GetComponents(TargetComponents);
 auto** TargetComponent=TargetComponents.FindByPredicate([&](const auto* C){return C->GetSkeletalMeshAsset()==Mesh;});if(!TargetComponent)Good=false;
 const auto& TargetSections=Mesh->GetResourceForRendering()->LODRenderData[0].RenderSections;
 for(int32 Sample=0;Sample<=5&&Good;++Sample){const float Time=OriginalWalk->Animations[0].Sequence->GetPlayLength()*Sample/5.f;Good=Before->SetAnimationPosition(Time)&&After->SetAnimationPosition(Time);if(!Good)break;
  TArray<FFinalSkinVertex> TargetVertices;(*TargetComponent)->GetCPUSkinnedVertices(TargetVertices,0);
  const auto& TargetPoints=Mesh->GetImportedModel()->LODModels[0].GetRawPointIndices();
  for(const auto& Source:Sources){auto** SourceComponent=SourceComponents.FindByPredicate([&](const auto* C){return C->GetSkeletalMeshAsset()==Source.Mesh;});if(!SourceComponent){Good=false;break;}
   const bool Visible=(*SourceComponent)->IsVisible(),Hidden=(*SourceComponent)->bHiddenInGame;
   (*SourceComponent)->SetVisibility(true);(*SourceComponent)->SetHiddenInGame(false);
   TArray<FFinalSkinVertex> SourceVertices;(*SourceComponent)->GetCPUSkinnedVertices(SourceVertices,0);
   (*SourceComponent)->SetVisibility(Visible);(*SourceComponent)->SetHiddenInGame(Hidden);
   const auto& Sections=Source.Mesh->GetResourceForRendering()->LODRenderData[0].RenderSections;
   const auto& SourcePoints=Source.Mesh->GetImportedModel()->LODModels[0].GetRawPointIndices();
   TMap<int32,int32> Ordinals;
   for(const auto& Section:Sections){const int32 Ordinal=Ordinals.FindOrAdd(Section.MaterialIndex)++;TArray<const FSkelMeshRenderSection*> Matches;for(const auto& TargetSection:TargetSections)if(TargetSection.MaterialIndex==Source.MaterialOffset+Section.MaterialIndex)Matches.Add(&TargetSection);
    if(!Matches.IsValidIndex(Ordinal)||Matches[Ordinal]->NumVertices!=Section.NumVertices||Matches[Ordinal]->NumTriangles!=Section.NumTriangles){Good=false;break;}
    TMap<int32,TArray<int32>> Candidates;
    for(uint32 Vertex=0;Vertex<Matches[Ordinal]->NumVertices;++Vertex){const int32 B=Matches[Ordinal]->BaseVertexIndex+Vertex;Candidates.FindOrAdd(TargetPoints[B]).Add(B);}
    for(uint32 Vertex=0;Vertex<Section.NumVertices;++Vertex){const int32 A=Section.BaseVertexIndex+Vertex;const int32* Point=Source.Vertices.Find(SourcePoints[A]);auto* Options=Point?Candidates.Find(*Point):nullptr;
     if(!Options||!SourceVertices.IsValidIndex(A)){Good=false;break;}
     const int32 Match=Options->IndexOfByPredicate([&](int32 B){return TargetVertices.IsValidIndex(B)&&FVector3f::Distance(SourceVertices[A].Position,TargetVertices[B].Position)<=.001&&SourceVertices[A].TangentZ==TargetVertices[B].TangentZ;});
     if(Match==INDEX_NONE){Good=false;break;}
     const int32 B=(*Options)[Match];Maximum=FMath::Max(Maximum,double(FVector3f::Distance(SourceVertices[A].Position,TargetVertices[B].Position)));++Compared;Options->RemoveAtSwap(Match);
    }
    if(!Good)break;
   }
   if(!Good)break;
  }
 }
 Before->Destroy();After->Destroy();World->DestroyWorld(false);GEngine->DestroyWorldContext(World);
 UE_LOG(LogTemp,Display,TEXT("Crowd packed audit good=%d skins=%d vertices=%lld max_cm=%.9f samples=6"),Good,Sources.Num(),Compared,Maximum);
 if(!Good)Error=TEXT("Packed animated section vertices or normals differ from original");return Good;
}

bool AuditRig(UKKImportedCharacter* BeforeCard,UKKImportedAnimationLibrary* BeforeWalk,UKKImportedCharacter* AfterCard,UKKImportedAnimationLibrary* AfterWalk){
 const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).CreateNavigation(false).CreateAISystem(false);
 auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
 auto Spawn=[&](UKKImportedCharacter* Card,UKKImportedAnimationLibrary* Walk){auto* Actor=World->SpawnActorDeferred<AKKImportedCharacterActor>(AKKImportedCharacterActor::StaticClass(),FTransform::Identity);Actor->Character=Card;Actor->AnimationLibrary=Walk;Actor->PlaySkeletalAnimation=true;Actor->ShareBakedPose=true;Actor->FinishSpawning(FTransform::Identity);return Actor;};
 auto* Before=Spawn(BeforeCard,BeforeWalk);auto* After=Spawn(AfterCard,AfterWalk);bool Good=Before->ConfigureBakedPoseSharing()&&After->ConfigureBakedPoseSharing();int64 Compared=0;double Maximum=0.;
 TArray<USkeletalMeshComponent*> Left,Right;Before->GetComponents(Left);After->GetComponents(Right);
 for(int32 Sample=0;Sample<=12&&Good;++Sample){const float Time=BeforeWalk->Animations[0].Sequence->GetPlayLength()*Sample/12.f;Good=Before->SetAnimationPosition(Time)&&After->SetAnimationPosition(Time);
  for(int32 Part=0;Part<BeforeCard->Outfits[0].Parts.Num()&&Good;++Part){const auto& A=BeforeCard->Outfits[0].Parts[Part];const auto& B=AfterCard->Outfits[0].Parts[Part];if(!A.SkinnedModel)continue;
   auto** X=Left.FindByPredicate([&](const auto* C){return C->GetSkeletalMeshAsset()==A.SkinnedModel;});auto** Y=Right.FindByPredicate([&](const auto* C){return C->GetSkeletalMeshAsset()==B.SkinnedModel;});if(!X||!Y){Good=false;break;}
   (*X)->SetVisibility(true);(*X)->SetHiddenInGame(false);(*Y)->SetVisibility(true);(*Y)->SetHiddenInGame(false);
   if(FParse::Param(FCommandLine::Get(),TEXT("CompactGameLow"))){
    auto Pose=[](USkeletalMeshComponent* Skin,int32 Bone,FTransform& Out){auto* Leader=Skin->LeaderPoseComponent.Get();if(Leader){const auto& Map=Skin->GetLeaderBoneMap();if(!Map.IsValidIndex(Bone)||!Leader->GetComponentSpaceTransforms().IsValidIndex(Map[Bone]))return false;Out=Leader->GetComponentSpaceTransforms()[Map[Bone]];}else{if(!Skin->GetComponentSpaceTransforms().IsValidIndex(Bone))return false;Out=Skin->GetComponentSpaceTransforms()[Bone];}return true;};
    const auto& RefA=A.SkinnedModel->GetRefSkeleton();const auto& RefB=B.SkinnedModel->GetRefSkeleton();
    for(int32 Bone=0;Bone<RefB.GetNum()&&Good;++Bone){FTransform PA,PB;const int32 Old=RefA.FindBoneIndex(RefB.GetBoneName(Bone));Good=Old!=INDEX_NONE&&Pose(*X,Old,PA)&&Pose(*Y,Bone,PB)&&PA.Equals(PB,1e-5);if(!Good){UE_LOG(LogTemp,Error,TEXT("Low compact pose differs part=%d bone=%s sample=%d"),Part,*RefB.GetBoneName(Bone).ToString(),Sample);}else{++Compared;}}
    // SameSkinSource already proves exact per-vertex geometry and named weights.
    // Equal retained animated matrices therefore preserve the skin result without
    // asking a NullRHI component for a nonexistent render MeshObject.
    continue;
   }
   TArray<FFinalSkinVertex> Original,Compact;(*X)->GetCPUSkinnedVertices(Original,0);(*Y)->GetCPUSkinnedVertices(Compact,0);
   const auto& SourcePoints=A.SkinnedModel->GetImportedModel()->LODModels[0].GetRawPointIndices();const auto& TargetPoints=B.SkinnedModel->GetImportedModel()->LODModels[0].GetRawPointIndices();
   if(Original.Num()!=Compact.Num()||Original.Num()!=SourcePoints.Num()||Compact.Num()!=TargetPoints.Num()){Good=false;break;}
   TMap<int32,TArray<int32>> Candidates;for(int32 Vertex=0;Vertex<TargetPoints.Num();++Vertex)Candidates.FindOrAdd(TargetPoints[Vertex]).Add(Vertex);
   for(int32 Vertex=0;Vertex<Original.Num();++Vertex){auto* Options=Candidates.Find(SourcePoints[Vertex]);if(!Options){Good=false;break;}
    const int32 Match=Options->IndexOfByPredicate([&](int32 Index){return FVector3f::Distance(Original[Vertex].Position,Compact[Index].Position)<=.001&&Original[Vertex].TangentZ==Compact[Index].TangentZ;});
    if(Match==INDEX_NONE){Good=false;break;}Maximum=FMath::Max(Maximum,double(FVector3f::Distance(Original[Vertex].Position,Compact[(*Options)[Match]].Position)));++Compared;Options->RemoveAtSwap(Match);
   }
  }
 }
 Before->Destroy();After->Destroy();World->DestroyWorld(false);GEngine->DestroyWorldContext(World);
 UE_LOG(LogTemp,Display,TEXT("Crowd packed rig runtime audit good=%d vertices=%lld max_cm=%.9f samples=13 shared_pose=1"),Good,Compared,Maximum);return Good;
}
}
#endif
#if WITH_EDITOR
#include "OKCrowdHairOutlineRepair.inl"
#endif
UOKPackCrowdCommandlet::UOKPackCrowdCommandlet(){IsEditor=true;IsClient=true;LogToConsole=true;}
int32 UOKPackCrowdCommandlet::Main(const FString& Params){
#if WITH_EDITOR
 int32 Start=0,End=100;FParse::Value(*Params,TEXT("StartIndex="),Start);FParse::Value(*Params,TEXT("EndIndex="),End);if(Start<0||End>100||Start>=End)return 1;
 if(FParse::Param(*Params,TEXT("BuildHairLOD"))){
  FString SourceMesh,SourceWalk,Destination;uint32 Limit=10000;
  if(!FParse::Value(*Params,TEXT("SourceMesh="),SourceMesh)||!FParse::Value(*Params,TEXT("SourceWalk="),SourceWalk)||!FParse::Value(*Params,TEXT("Destination="),Destination)||!Destination.StartsWith(TEXT("/Game/")))return 1;
  FParse::Value(*Params,TEXT("TriangleLimit="),Limit);if(Limit<12||Limit>10000)return 1;
  TStrongObjectPtr<USkeletalMesh> Source(LoadObject<USkeletalMesh>(nullptr,*SourceMesh));TStrongObjectPtr<UKKImportedAnimationLibrary> Walk(LoadObject<UKKImportedAnimationLibrary>(nullptr,*SourceWalk));if(!Source||!Walk||Walk->Animations.IsEmpty())return 1;FAssetCompilingManager::Get().FinishAllCompilation();
  auto* Result=OKBuildCrowdNearHair(Source.Get(),Walk->Animations[0].Sequence,Destination,Limit);if(!Result)return 1;FAssetCompilingManager::Get().FinishAllCompilation();
  int64 Count=0;for(const auto& Section:Result->GetResourceForRendering()->LODRenderData[0].RenderSections)Count+=Section.NumTriangles;if(Count>Limit)return 1;
  TSet<UMaterialInterface*> Materials;for(const auto& M:Result->GetMaterials())Materials.Add(M.MaterialInterface.Get());int32 Changed=0;if(OKRepairCrowdHairOutline(Result,Walk->Animations[0].Sequence,Materials,Destination/TEXT("Idempotence"),Changed)!=Result||Changed!=0)return 1;
  UE_LOG(LogTemp,Display,TEXT("Generic hair LOD complete mesh=%s triangles=%lld limit=%u second_pass_changes=0"),*Result->GetPathName(),Count,Limit);return 0;
 }
 if(FParse::Param(*Params,TEXT("RepairHairOutlines")))return RepairHairOutlines(Start,End,Params);
 if(FParse::Param(*Params,TEXT("AuditNearGame"))){
  FString Output;if(!FParse::Value(*Params,TEXT("Output="),Output)||IFileManager::Get().FileExists(*Output))return 1;
  TArray<TSharedPtr<FJsonValue>> Rows;TSet<FString> Hashes;
  for(int32 Index=Start;Index<End;++Index){
   {const FString Base=FString::Printf(TEXT("/Game/Scenes/CrowdRoom/Characters/C%03d"),Index);
    FString AuditVariant=TEXT("GameLowBodySafeR6");FParse::Value(*Params,TEXT("QualityVariant="),AuditVariant);TStrongObjectPtr<UKKImportedCharacter> Low(LoadObject<UKKImportedCharacter>(nullptr,*(Base/AuditVariant/TEXT("Character.Character"))));
    FString AuditNearVariant=AuditVariant==TEXT("GameLowHairSafeR9")?TEXT("NearDetailR6"):TEXT("NearDetailR5");FParse::Value(*Params,TEXT("NearVariant="),AuditNearVariant);if(AuditNearVariant.IsEmpty())return 1;for(TCHAR C:AuditNearVariant)if(!FChar::IsAlnum(C)&&C!=TEXT('_'))return 1;
    TStrongObjectPtr<UOKCrowdFaceDetail> Near(LoadObject<UOKCrowdFaceDetail>(nullptr,*(Base/AuditNearVariant/TEXT("Face.Face"))));
    if(!Low||Low->Outfits.Num()!=1||!Near||!Near->Character||Near->Character->Outfits.Num()!=1||Near->SourceHash!=Low->SourceHash||Near->Character->SourceHash!=Low->SourceHash||Hashes.Contains(Low->SourceHash)||Near->NearHairTriangles>10000)return 1;
    Hashes.Add(Low->SourceHash);FAssetCompilingManager::Get().FinishAllCompilation();
    const auto* Packed=Low->Outfits[0].Parts.FindByPredicate([](const auto& P){return P.Name==TEXT("PackedVisibleParts");});if(!Packed||!Packed->SkinnedModel)return 1;const auto& Ref=Packed->SkinnedModel->GetRefSkeleton();
    int64 Triangles=0,Hair=0;int32 TextureMax=0;
    for(const auto& Part:Low->Outfits[0].Parts)if(auto* Mesh=Part.SkinnedModel.Get()){
     const auto* Data=Mesh->GetResourceForRendering();if(!Data||Data->LODRenderData.Num()!=1)return 1;for(const auto& Section:Data->LODRenderData[0].RenderSections)Triangles+=Section.NumTriangles;
     for(const auto& Slot:Mesh->GetMaterials())if(auto* Material=Slot.MaterialInterface.Get()){TSet<const UTexture*> Textures;Material->GetReferencedTexturesAndOverrides(Textures);for(auto* Texture:Textures)if(auto* T=Cast<UTexture2D>(Texture))TextureMax=FMath::Max(TextureMax,FMath::Max(T->Source.GetSizeX(),T->Source.GetSizeY()));}
    }
    if(Triangles<=0||Triangles>=30000||TextureMax<=0||TextureMax>256){UE_LOG(LogTemp,Error,TEXT("Near audit invalid index=%d triangles=%lld texture_source_max=%d"),Index,Triangles,TextureMax);return 1;}
    for(const auto& Part:Near->Character->Outfits[0].Parts){auto* Mesh=Part.SkinnedModel.Get();if(!Mesh||Part.Role==TEXT("body")||Part.Role.Contains(TEXT("clothes")))return 1;const auto& Other=Mesh->GetRefSkeleton();
     for(int32 B=0;B<Other.GetNum();++B){const int32 M=Ref.FindBoneIndex(Other.GetBoneName(B));if(M==INDEX_NONE||!Ref.GetRefBonePose()[M].Equals(Other.GetRefBonePose()[B],1e-6))return 1;const int32 Parent=Ref.GetParentIndex(M),OtherParent=Other.GetParentIndex(B);if((Parent==INDEX_NONE)!=(OtherParent==INDEX_NONE)||(Parent!=INDEX_NONE&&Ref.GetBoneName(Parent)!=Other.GetBoneName(OtherParent)))return 1;}
     if(Part.Role==TEXT("hair")){const auto* Data=Mesh->GetResourceForRendering();if(!Data||Data->LODRenderData.Num()!=1)return 1;for(const auto& Section:Data->LODRenderData[0].RenderSections)Hair+=Section.NumTriangles;}
    }
    if(Hair!=Near->NearHairTriangles||Hair>10000)return 1;
    auto Row=MakeShared<FJsonObject>();Row->SetNumberField(TEXT("index"),Index);Row->SetStringField(TEXT("source_hash"),Low->SourceHash);Row->SetNumberField(TEXT("low_triangles"),Triangles);Row->SetNumberField(TEXT("low_texture_max"),TextureMax);Row->SetNumberField(TEXT("near_hair_triangles"),Hair);Row->SetBoolField(TEXT("near_bone_hierarchy_matches"),true);Rows.Add(MakeShared<FJsonValueObject>(Row));UE_LOG(LogTemp,Display,TEXT("Crowd near game audit index=%d low=%lld texture=%d hair=%lld"),Index,Triangles,TextureMax,Hair);
   }CollectGarbage(RF_NoFlags);
  }
  auto Root=MakeShared<FJsonObject>();Root->SetArrayField(TEXT("cards"),Rows);Root->SetNumberField(TEXT("distinct_source_hashes"),Hashes.Num());FString Text;FJsonSerializer::Serialize(Root,TJsonWriterFactory<>::Create(&Text));return FFileHelper::SaveStringToFile(Text,*Output)?0:1;
 }
 if(FParse::Param(*Params,TEXT("BuildSectionBounds"))){
  FString Output;if(!FParse::Value(*Params,TEXT("Output="),Output)||IFileManager::Get().FileExists(*Output))return 1;
  FString BoundsVariant=TEXT("GameLow30kR1");FParse::Value(*Params,TEXT("QualityVariant="),BoundsVariant);if(BoundsVariant!=TEXT("GameLow30kR1")&&BoundsVariant!=TEXT("GameLowBodySafeR6")&&BoundsVariant!=TEXT("GameLowBindingSafeR7")&&BoundsVariant!=TEXT("GameLowSurfaceSafeR8")&&BoundsVariant!=TEXT("GameLowHairSafeR9"))return 1;TArray<FString> Roots;FString SelectionFile;
  if(FParse::Value(*Params,TEXT("Selection="),SelectionFile)){FString Text;TSharedPtr<FJsonObject> Selection;const TArray<TSharedPtr<FJsonValue>>* Entries=nullptr;
   if(!FFileHelper::LoadFileToString(Text,*SelectionFile)||!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Selection)||!Selection||!Selection->TryGetArrayField(TEXT("characters"),Entries)||Entries->Num()!=100)return 1;
   for(const auto& Entry:*Entries){const auto Object=Entry->AsObject();FString Character;if(!Object||!Object->TryGetStringField(TEXT("character"),Character)||!Character.StartsWith(TEXT("/Game/")))return 1;Roots.Add(FPaths::GetPath(FSoftObjectPath(Character).GetLongPackageName())/BoundsVariant);}
  }else for(int32 Index=0;Index<100;++Index)Roots.Add(FString::Printf(TEXT("/Game/Scenes/CrowdRoom/Characters/C%03d/%s"),Index,*BoundsVariant));
  TArray<TSharedPtr<FJsonValue>> Cards;
  for(int32 Index=Start;Index<End;++Index){const FString Base=Roots[Index];
   TStrongObjectPtr<UKKImportedAnimationLibrary> Library(LoadObject<UKKImportedAnimationLibrary>(nullptr,*(Base/TEXT("Walk/Animations.Animations"))));
   if(!Library||!Library->Character||Library->Character->Schema!=1005||Library->Animations.Num()!=1)return 1;
   auto* Clip=Library->Animations[0].Sequence.Get();if(!Clip)return 1;Clip->CacheDerivedDataForCurrentPlatform();Clip->WaitOnExistingCompression();const auto* Model=Clip->GetDataModelInterface().GetInterface();if(!Model||!Clip->GetSkeleton())return 1;const auto& Skeleton=Clip->GetSkeleton()->GetReferenceSkeleton();const int32 Frames=Model->GetNumberOfFrames();
   struct FPoint {FVector Position;TArray<TPair<int32,double>> Weights;int32 Material;};
   struct FPart {FString Path;FTransform Transform;int32 Materials=0,Sections=0;TArray<FPoint> Points;TArray<int64> Triangles;TArray<int32> SectionCounts;bool Eligible=false;};
   TArray<FPart> Parts;TSet<int32> Needed;
   for(const auto& Entry:Library->Character->Outfits[0].Parts)if(auto* Mesh=Entry.SkinnedModel.Get()){
    FPart Part;Part.Path=Mesh->GetPathName();Part.Transform=Entry.Transform;Part.Materials=Mesh->GetMaterials().Num();Part.Eligible=Entry.MouthDefaultVisible&&Entry.MouthPatterns.IsEmpty()&&Mesh->GetMorphTargets().IsEmpty();Part.Triangles.Init(0,Part.Materials);Part.SectionCounts.Init(0,Part.Materials);
    const auto* Data=Mesh->GetResourceForRendering();if(!Data||Data->LODRenderData.Num()!=1||!Mesh->GetLODInfo(0)->LODMaterialMap.IsEmpty())return 1;const auto& LOD=Data->LODRenderData[0];Part.Sections=LOD.RenderSections.Num();const auto& Ref=Mesh->GetRefSkeleton();
    for(const auto& Section:LOD.RenderSections){if(!Part.Triangles.IsValidIndex(Section.MaterialIndex))return 1;Part.Triangles[Section.MaterialIndex]+=Section.NumTriangles;++Part.SectionCounts[Section.MaterialIndex];
     for(uint32 V=Section.BaseVertexIndex;V<Section.BaseVertexIndex+Section.NumVertices;++V){FPoint P;P.Position=FVector(LOD.StaticVertexBuffers.PositionVertexBuffer.VertexPosition(V));P.Material=Section.MaterialIndex;const auto Raw=LOD.SkinWeightVertexBuffer.GetVertexSkinWeights(V);const double Denom=LOD.SkinWeightVertexBuffer.Use16BitBoneWeight()?65535.:255.;
      for(int32 K=0;K<MAX_TOTAL_INFLUENCES;++K)if(Raw.InfluenceWeights[K]){if(!Section.BoneMap.IsValidIndex(Raw.InfluenceBones[K]))return 1;const int32 Bone=Section.BoneMap[Raw.InfluenceBones[K]],Mapped=Skeleton.FindBoneIndex(Ref.GetBoneName(Bone));if(Mapped==INDEX_NONE||!Mesh->GetRefBasesInvMatrix()[Bone].Equals(FMatrix44f::Identity,1e-6f)){UE_LOG(LogTemp,Error,TEXT("Section bounds requires identity palette bind matrices"));return 1;}P.Weights.Add({Mapped,Raw.InfluenceWeights[K]/Denom});int32 Ancestor=Mapped;while(Ancestor!=INDEX_NONE&&!Needed.Contains(Ancestor)){Needed.Add(Ancestor);Ancestor=Skeleton.GetParentIndex(Ancestor);}}
      Part.Points.Add(MoveTemp(P));
     }
    }Parts.Add(MoveTemp(Part));
   }
   TArray<int32> Bones=Needed.Array();Bones.Sort();TMap<int32,int32> BoneMap;for(int32 B=0;B<Bones.Num();++B)BoneMap.Add(Bones[B],B);
   struct FTrack {int32 Parent=INDEX_NONE;TArray<FTransform> Keys;};TArray<FTrack> Tracks;
   for(const int32 Bone:Bones){FTrack Track;const int32 Parent=Skeleton.GetParentIndex(Bone);if(Parent!=INDEX_NONE)Track.Parent=BoneMap[Parent];for(int32 F=0;F<=Frames;++F){FTransform T;FAnimExtractContext Context(F*Model->GetFrameRate().AsInterval());Context.bEnforceCompressedDataSampling=true;Clip->GetBoneTransform(T,FSkeletonPoseBoneIndex(Bone),Context,false);if(T.ContainsNaN())return 1;Track.Keys.Add(T);}Tracks.Add(MoveTemp(Track));}
   for(auto& Part:Parts)for(auto& Point:Part.Points)for(auto& Weight:Point.Weights)Weight.Key=BoneMap[Weight.Key];
   struct FSample {TArray<TArray<double>> Radii;double ActorRadius=0;};TArray<FSample> Samples;Samples.SetNum(Frames*4+1);
   // Only immutable plain arrays enter workers. Asset reads and JSON publication
   // stay on the commandlet thread. Bounds aggregate all sections sharing a slot.
   ParallelFor(TEXT("CrowdMaterialBounds"),Samples.Num(),1,[&](int32 Sample){auto& Result=Samples[Sample];TArray<FTransform> Pose;TArray<FMatrix> Matrices;Pose.SetNum(Tracks.Num());Matrices.SetNum(Tracks.Num());const int32 Frame=FMath::Min(Sample/4,Frames),Next=FMath::Min(Frame+1,Frames);const double Alpha=(Sample%4)/4.;
    for(int32 B=0;B<Tracks.Num();++B){Pose[B].Blend(Tracks[B].Keys[Frame],Tracks[B].Keys[Next],Alpha);if(Tracks[B].Parent!=INDEX_NONE)Pose[B]=Pose[B]*Pose[Tracks[B].Parent];Matrices[B]=Pose[B].ToMatrixWithScale();}
    for(const auto& Part:Parts){TArray<FBox> Boxes;Boxes.Init(FBox(ForceInit),Part.Materials);for(const auto& Point:Part.Points){FVector P=FVector::ZeroVector;for(const auto& W:Point.Weights)P+=FVector(Matrices[W.Key].TransformPosition(Point.Position))*W.Value;P=Part.Transform.TransformPosition(P);Boxes[Point.Material]+=P;Result.ActorRadius=FMath::Max(Result.ActorRadius,P.Size());}TArray<double> Radii;for(const auto& Box:Boxes)Radii.Add(Box.IsValid?Box.GetExtent().Size():0);Result.Radii.Add(MoveTemp(Radii));}
   },EParallelForFlags::None);
   auto Card=MakeShared<FJsonObject>();Card->SetStringField(TEXT("source_hash"),Library->Character->SourceHash);TArray<TSharedPtr<FJsonValue>> Meshes;double ActorRadius=0;for(const auto& Sample:Samples)ActorRadius=FMath::Max(ActorRadius,Sample.ActorRadius);
   for(int32 P=0;P<Parts.Num();++P){const auto& Part=Parts[P];auto Mesh=MakeShared<FJsonObject>();Mesh->SetStringField(TEXT("mesh"),Part.Path);Mesh->SetNumberField(TEXT("materials"),Part.Materials);Mesh->SetNumberField(TEXT("sections"),Part.Sections);Mesh->SetBoolField(TEXT("eligible"),Part.Eligible);Mesh->SetNumberField(TEXT("actor_radius_cm"),ActorRadius*1.25+.2);TArray<TSharedPtr<FJsonValue>> Slots;
    for(int32 M=0;M<Part.Materials;++M){double Radius=0;for(const auto& Sample:Samples)Radius=FMath::Max(Radius,Sample.Radii[P][M]);auto Slot=MakeShared<FJsonObject>();Slot->SetNumberField(TEXT("slot"),M);Slot->SetNumberField(TEXT("radius_cm"),Radius*1.25+.2);Slot->SetNumberField(TEXT("triangles"),double(Part.Triangles[M]));Slot->SetNumberField(TEXT("sections"),Part.SectionCounts[M]);Slots.Add(MakeShared<FJsonValueObject>(Slot));}Mesh->SetArrayField(TEXT("slots"),Slots);Meshes.Add(MakeShared<FJsonValueObject>(Mesh));
   }Card->SetArrayField(TEXT("meshes"),Meshes);Cards.Add(MakeShared<FJsonValueObject>(Card));UE_LOG(LogTemp,Display,TEXT("Crowd section bounds complete index=%d samples=%d meshes=%d"),Index,Samples.Num(),Parts.Num());
   Library.Reset();CollectGarbage(RF_NoFlags);
  }
  auto Report=MakeShared<FJsonObject>();Report->SetNumberField(TEXT("schema"),1);Report->SetNumberField(TEXT("recipe"),1005);Report->SetArrayField(TEXT("cards"),Cards);FString Json;FJsonSerializer::Serialize(Report,TJsonWriterFactory<>::Create(&Json));return FFileHelper::SaveStringToFile(Json,*Output)?0:1;
 }
 if(FParse::Param(*Params,TEXT("VerifyLowSRT"))){
  for(int32 Index=Start;Index<End;++Index){const FString Base=FString::Printf(TEXT("/Game/Scenes/CrowdRoom/Characters/C%03d"),Index);
   TStrongObjectPtr<UKKImportedAnimationLibrary> Before(LoadObject<UKKImportedAnimationLibrary>(nullptr,*(Base/TEXT("GameLow30kR1/Walk/Animations.Animations")))),After(LoadObject<UKKImportedAnimationLibrary>(nullptr,*(Base/TEXT("GameLowSRTR1/Walk/Animations.Animations"))));
   if(!Before||!After||!Before->Character||!After->Character||After->Character->Schema!=1009||Before->Character->SourceHash!=After->Character->SourceHash||Before->Animations.Num()!=1||After->Animations.Num()!=1)return 1;
   auto* AClip=Before->Animations[0].Sequence.Get();auto* BClip=After->Animations[0].Sequence.Get();if(!AClip||!BClip)return 1;const auto* Model=AClip->GetDataModelInterface().GetInterface();const int32 Frames=Model->GetNumberOfFrames();const auto& ARef=AClip->GetSkeleton()->GetReferenceSkeleton();const auto& BRef=BClip->GetSkeleton()->GetReferenceSkeleton();double Maximum=0,MinDot=1;int64 Compared=0;
   if(Before->Character->Outfits[0].Parts.Num()!=After->Character->Outfits[0].Parts.Num())return 1;
   TArray<FName> OldNames,NewNames;for(const auto& Part:Before->Character->Outfits[0].Parts)if(Part.SkinnedModel){FSkeletalMeshConstAttributes A(*Part.SkinnedModel->GetMeshDescription(0));for(auto V:Part.SkinnedModel->GetMeshDescription(0)->Vertices().GetElementIDs())for(const auto W:A.GetVertexSkinWeights().Get(V))OldNames.AddUnique(Part.SkinnedModel->GetRefSkeleton().GetBoneName(W.GetBoneIndex()));}BClip->GetDataModelInterface()->GetBoneTrackNames(NewNames);
   for(int32 Sample=0;Sample<=Frames*4;++Sample){FAnimExtractContext Context(Model->GetFrameRate().AsInterval()*Sample/4.);Context.bEnforceCompressedDataSampling=true;TMap<FName,FMatrix> APose,BPose;
    for(const FName Bone:OldNames){FTransform T;AClip->GetBoneTransform(T,FSkeletonPoseBoneIndex(ARef.FindBoneIndex(Bone)),Context,false);APose.Add(Bone,T.ToMatrixWithScale());}for(const FName Bone:NewNames){FTransform T;BClip->GetBoneTransform(T,FSkeletonPoseBoneIndex(BRef.FindBoneIndex(Bone)),Context,false);BPose.Add(Bone,T.ToMatrixWithScale());}
    for(int32 Part=0;Part<Before->Character->Outfits[0].Parts.Num();++Part){auto* Old=Before->Character->Outfits[0].Parts[Part].SkinnedModel.Get();auto* New=After->Character->Outfits[0].Parts[Part].SkinnedModel.Get();if(!Old)continue;if(!New)return 1;const auto& DA=*Old->GetMeshDescription(0);const auto& DB=*New->GetMeshDescription(0);FSkeletalMeshConstAttributes A(DA),B(DB);if(DA.Vertices().Num()!=DB.Vertices().Num()||DA.Triangles().Num()!=DB.Triangles().Num()||A.GetMorphTargetNames()!=B.GetMorphTargetNames()||Old->GetMaterials()!=New->GetMaterials())return 1;
     for(auto V:DA.Vertices().GetElementIDs()){if(A.GetVertexPositions()[V]!=B.GetVertexPositions()[V])return 1;FMatrix MA(ForceInit),MB(ForceInit);for(const auto W:A.GetVertexSkinWeights().Get(V)){const auto& M=APose[Old->GetRefSkeleton().GetBoneName(W.GetBoneIndex())];for(int32 R=0;R<4;++R)for(int32 C=0;C<4;++C)MA.M[R][C]+=M.M[R][C]*W.GetRawWeight()/65535.;}for(const auto W:B.GetVertexSkinWeights().Get(V)){const auto& M=BPose[New->GetRefSkeleton().GetBoneName(W.GetBoneIndex())];for(int32 R=0;R<4;++R)for(int32 C=0;C<4;++C)MB.M[R][C]+=M.M[R][C]*W.GetRawWeight()/65535.;}const FVector P(A.GetVertexPositions()[V]);Maximum=FMath::Max(Maximum,FVector::Distance(FVector(MA.TransformPosition(P)),FVector(MB.TransformPosition(P))));++Compared;
      for(const auto Instance:DA.GetVertexVertexInstanceIDs(V)){const FVector N(A.GetVertexInstanceNormals()[Instance]);const FVector NA=FVector(MA.TransformVector(N)),NB=FVector(MB.TransformVector(N));if(NA.SizeSquared()>1e-12){const double Dot=FVector::DotProduct(NA.GetSafeNormal(),NB.GetSafeNormal());MinDot=FMath::Min(MinDot,Dot);}}
      if(Maximum>.1||MinDot<.99985){UE_LOG(LogTemp,Error,TEXT("Selective SRT verification failed index=%d sample=%d max_cm=%.9f normal_dot=%.9f"),Index,Sample,Maximum,MinDot);return 1;}
     }
    }
   }
   UE_LOG(LogTemp,Display,TEXT("Crowd selective SRT verified index=%d samples=%lld max_cm=%.9f minimum_normal_dot=%.9f"),Index,Compared,Maximum,MinDot);CollectGarbage(RF_NoFlags);
  }return 0;
 }
 if(FParse::Param(*Params,TEXT("AuditLowAffine"))||FParse::Param(*Params,TEXT("BuildLowSRT"))){
  FString Output;if(!FParse::Value(*Params,TEXT("Output="),Output)||IFileManager::Get().FileExists(*Output))return 1;
  double Tolerance=.05;FParse::Value(*Params,TEXT("SRTErrorCM="),Tolerance);if(!FMath::IsFinite(Tolerance)||Tolerance<0||Tolerance>.05)return 1;
  TArray<TSharedPtr<FJsonValue>> Rows;
  for(int32 Index=Start;Index<End;++Index){
   const FString Base=FString::Printf(TEXT("/Game/Scenes/CrowdRoom/Characters/C%03d/GameLow30kR1"),Index);
   TStrongObjectPtr<UKKImportedAnimationLibrary> Library(LoadObject<UKKImportedAnimationLibrary>(nullptr,*(Base/TEXT("Walk/Animations.Animations"))));
   if(!Library||!Library->Character||Library->Animations.Num()!=1)return 1;
   auto* Card=Library->Character.Get();auto* Sequence=Library->Animations[0].Sequence.Get();if(!Sequence||Card->Outfits.Num()!=1)return 1;
   const auto* Model=Sequence->GetDataModelInterface().GetInterface();if(!Model)return 1;
   struct FGroup {FName Bones[3];TArray<FTransform> Keys[3],SRT;double RadiusL1=0;double Error=0;bool Collapse=false;};
   TMap<FString,FGroup> Groups;TSet<FName> Used,Native;int64 Vertices=0;
   for(const auto& Part:Card->Outfits[0].Parts)if(auto* Mesh=Part.SkinnedModel.Get()){
    const auto* Desc=Mesh->GetMeshDescription(0);if(!Desc)return 1;FSkeletalMeshConstAttributes A(*Desc);const auto& Ref=Mesh->GetRefSkeleton();
    for(auto V:Desc->Vertices().GetElementIDs()){++Vertices;const FVector3f P=A.GetVertexPositions()[V];const double Radius=FMath::Abs(P.X)+FMath::Abs(P.Y)+FMath::Abs(P.Z);
     for(const auto W:A.GetVertexSkinWeights().Get(V)){const FName Name=Ref.GetBoneName(W.GetBoneIndex());Used.Add(Name);const FString S=Name.ToString();
      if(!S.StartsWith(TEXT("__ok_palette_"))||S.Len()<2||S[S.Len()-2]!='_'||S[S.Len()-1]<'0'||S[S.Len()-1]>'2'){Native.Add(Name);continue;}
      const int32 Axis=S[S.Len()-1]-'0';auto& G=Groups.FindOrAdd(S.LeftChop(2));G.Bones[Axis]=Name;G.RadiusL1=FMath::Max(G.RadiusL1,Radius);
      if(Ref.GetParentIndex(W.GetBoneIndex())!=0||!Ref.GetRefBonePose()[W.GetBoneIndex()].Equals(FTransform::Identity,1e-6))return 1;
     }
    }
   }
   const int32 Frames=Model->GetNumberOfFrames();int32 Eligible=0;double Worst=0;
   for(auto& Pair:Groups){auto& G=Pair.Value;
    for(int32 Axis=0;Axis<3;++Axis){if(G.Bones[Axis].IsNone())return 1;Model->GetBoneTrackTransforms(G.Bones[Axis],G.Keys[Axis]);if(G.Keys[Axis].Num()!=Frames+1)return 1;}
    for(int32 Frame=0;Frame<=Frames;++Frame){FMatrix M(ForceInit);for(int32 Axis=0;Axis<3;++Axis){const auto Column=G.Keys[Axis][Frame].ToMatrixWithScale();for(int32 R=0;R<4;++R)for(int32 C=0;C<4;++C)M.M[R][C]+=Column.M[R][C]/3.;}G.SRT.Add(FTransform(M));}
    for(int32 Frame=0;Frame<Frames;++Frame)for(int32 Sub=0;Sub<=4;++Sub){const double Alpha=Sub/4.;FMatrix M(ForceInit);
     for(int32 Axis=0;Axis<3;++Axis){FTransform Pose;Pose.Blend(G.Keys[Axis][Frame],G.Keys[Axis][Frame+1],Alpha);const FMatrix Column=Pose.ToMatrixWithScale();for(int32 R=0;R<4;++R)for(int32 C=0;C<4;++C)M.M[R][C]+=Column.M[R][C]/3.;}
     FTransform SRT;SRT.Blend(G.SRT[Frame],G.SRT[Frame+1],Alpha);if(SRT.ContainsNaN())return 1;const FMatrix Approx=SRT.ToMatrixWithScale();double Error2=0;
     for(int32 C=0;C<3;++C){double Linear=0;for(int32 R=0;R<3;++R)Linear=FMath::Max(Linear,FMath::Abs(M.M[R][C]-Approx.M[R][C]));const double E=Linear*G.RadiusL1+FMath::Abs(M.M[3][C]-Approx.M[3][C]);Error2+=E*E;}
     G.Error=FMath::Max(G.Error,FMath::Sqrt(Error2));
    }
    Worst=FMath::Max(Worst,G.Error);G.Collapse=G.Error<=Tolerance;if(G.Collapse)++Eligible;
   }
   if(FParse::Param(*Params,TEXT("BuildLowSRT"))){
    if(!Native.IsEmpty()){UE_LOG(LogTemp,Error,TEXT("Selective SRT requires palette-only weights"));return 1;}
    const FString Root=FString::Printf(TEXT("/Game/Scenes/CrowdRoom/Characters/C%03d/GameLowSRTR1"),Index);
    TArray<FString> Names;Groups.GetKeys(Names);Names.Sort();TMap<FName,FName> MappedNames;TMap<FName,TArray<FTransform>> Tracks;
    const auto& OldRootRef=Card->Outfits[0].Parts[0].SkinnedModel->GetRefSkeleton();const FName RootName=OldRootRef.GetBoneName(0);TArray<FTransform> RootKeys;Model->GetBoneTrackTransforms(RootName,RootKeys);
    if(!OldRootRef.GetRefBonePose()[0].Equals(FTransform::Identity,1e-6)||RootKeys.Num()!=Frames+1)return 1;
    for(const auto& Key:RootKeys)if(!Key.Equals(FTransform::Identity,1e-6)){UE_LOG(LogTemp,Error,TEXT("Selective SRT requires an identity palette root"));return 1;}Tracks.Add(RootName,RootKeys);
    for(const FString& Name:Names){auto& G=Groups[Name];if(G.Collapse){const FName Target(*(Name+TEXT("_srt")));Tracks.Add(Target,G.SRT);for(const FName Bone:G.Bones)MappedNames.Add(Bone,Target);}else for(int32 Axis=0;Axis<3;++Axis){MappedNames.Add(G.Bones[Axis],G.Bones[Axis]);Tracks.Add(G.Bones[Axis],G.Keys[Axis]);}}
    auto* Skeleton=FreshPackedAsset<USkeleton>(Root/TEXT("Skeleton"));auto* TargetCard=FreshPackedAsset<UKKImportedCharacter>(Root/TEXT("Character"));auto* Walk=FreshPackedAsset<UKKImportedAnimationLibrary>(Root/TEXT("Walk/Animations"));if(!Skeleton||!TargetCard||!Walk)return 1;
    TargetCard->Schema=1009;TargetCard->SourceHash=Card->SourceHash;TargetCard->Outfits=Card->Outfits;TargetCard->Outfits[0].Skeleton=Skeleton;TArray<UObject*> Assets;int64 BeforeBones=0,AfterBones=0;
    for(int32 Part=0;Part<TargetCard->Outfits[0].Parts.Num();++Part){auto& Entry=TargetCard->Outfits[0].Parts[Part];auto* Source=Entry.SkinnedModel.Get();if(!Source)continue;
     FMeshDescription Desc=*Source->GetMeshDescription(0);FSkeletalMeshAttributes A(Desc);TSet<FName> Needed;Needed.Add(RootName);
     for(auto V:Desc.Vertices().GetElementIDs())for(const auto W:A.GetVertexSkinWeights().Get(V)){const auto* Name=MappedNames.Find(Source->GetRefSkeleton().GetBoneName(W.GetBoneIndex()));if(!Name)return 1;Needed.Add(*Name);}
     // The leader includes every follower's private palette; followers retain
     // only their own mapped bones. All reference poses remain identity.
     if(Part==0)for(const auto& T:Tracks)Needed.Add(T.Key);
     TArray<FName> BoneNames=Needed.Array();BoneNames.Sort(FNameLexicalLess());BoneNames.Remove(RootName);BoneNames.Insert(RootName,0);FReferenceSkeleton Ref;
     {FReferenceSkeletonModifier Modifier(Ref,Skeleton);for(int32 B=0;B<BoneNames.Num();++B)Modifier.Add(FMeshBoneInfo(BoneNames[B],BoneNames[B].ToString(),B==0?INDEX_NONE:0),FTransform::Identity,true);}
     for(auto V:Desc.Vertices().GetElementIDs()){TMap<int32,uint32> Combined;TMap<FString,TArray<uint16>> Triples;
      for(const auto W:A.GetVertexSkinWeights().Get(V)){const FName Old=Source->GetRefSkeleton().GetBoneName(W.GetBoneIndex());const FString S=Old.ToString();if(Groups[S.LeftChop(2)].Collapse)Triples.FindOrAdd(S.LeftChop(2)).Add(W.GetRawWeight());Combined.FindOrAdd(Ref.FindBoneIndex(MappedNames[Old]))+=W.GetRawWeight();}
      for(const auto& T:Triples)if(T.Value.Num()!=3||T.Value[0]!=T.Value[1]||T.Value[0]!=T.Value[2]){UE_LOG(LogTemp,Error,TEXT("Selective SRT requires balanced three-column weights"));return 1;}
      TArray<UE::AnimationCore::FBoneWeight> Weights;for(const auto& C:Combined){if(C.Key<0||C.Value>65535)return 1;Weights.Add(UE::AnimationCore::FBoneWeight(static_cast<FBoneIndexType>(C.Key),static_cast<uint16>(C.Value)));}A.GetVertexSkinWeights().SetRaw(V,Weights);
     }
     const int32 Count=A.GetNumBones();for(int32 B=0;B<Count;++B)A.DeleteBone(FBoneID(B));for(int32 B=0;B<Ref.GetNum();++B){const FBoneID Id(B);A.CreateBone(Id);A.GetBoneNames()[Id]=Ref.GetBoneName(B);A.GetBoneParentIndices()[Id]=Ref.GetParentIndex(B);A.GetBonePoses()[Id]=FTransform::Identity;}
     auto* Mesh=FreshPackedAsset<USkeletalMesh>(Root/FString::Printf(TEXT("Skins/SK_%03d"),Part));if(!Mesh)return 1;Mesh->SetSkeleton(Skeleton);Mesh->SetRefSkeleton(Ref);Mesh->CalculateInvRefMatrices();Mesh->AddLODInfo();*Mesh->GetLODInfo(0)=*Source->GetLODInfo(0);Mesh->GetMaterials()=Source->GetMaterials();Mesh->SetHasVertexColors(Source->GetHasVertexColors());Mesh->GetImportedModel()->LODModels.Add(new FSkeletalMeshLODModel());Mesh->CreateMeshDescription(0,MoveTemp(Desc));Mesh->CommitMeshDescription(0);Mesh->Build();Mesh->PostEditChange();FAssetCompilingManager::Get().FinishAllCompilation();
     if(Part==0&&!Skeleton->MergeAllBonesToBoneTree(Mesh))return 1;BeforeBones+=Source->GetRefSkeleton().GetNum();AfterBones+=Ref.GetNum();Entry.SkinnedModel=Mesh;Assets.Add(Mesh);
    }
    auto* Clip=FreshPackedAsset<UKKImportedAnimSequence>(Root/TEXT("Walk/A_Walk"));if(!Clip)return 1;Clip->SetSkeleton(Skeleton);Clip->BoneCompressionSettings=Sequence->BoneCompressionSettings;Clip->CurveCompressionSettings=Sequence->CurveCompressionSettings;
    if(Model->GetNumberOfFloatCurves()!=0||Model->GetNumberOfTransformCurves()!=0){UE_LOG(LogTemp,Error,TEXT("Selective SRT pilot cannot discard animation curves"));return 1;}
    auto& Ctrl=Clip->GetController();Ctrl.OpenBracket(NSLOCTEXT("Crowd","SelectiveSRT","Create selective low rig"),false);Ctrl.InitializeModel();Ctrl.SetFrameRate(Model->GetFrameRate(),false);Ctrl.SetNumberOfFrames(FFrameNumber(Frames),false);
    for(const auto& T:Tracks){TArray<FVector> P,S;TArray<FQuat> Q;for(const auto& Key:T.Value){P.Add(Key.GetTranslation());S.Add(Key.GetScale3D());Q.Add(Key.GetRotation());}if(!Ctrl.AddBoneCurve(T.Key,false)||!Ctrl.SetBoneTrackKeys(T.Key,P,Q,S,false)){Ctrl.CloseBracket(false);return 1;}}Ctrl.NotifyPopulated();Ctrl.CloseBracket(false);Clip->PostEditChange();Clip->CacheDerivedDataForCurrentPlatform();Clip->WaitOnExistingCompression();
    // Validate the actual compressed clips at every key and quarter-frame.
    // Geometry/UV/morph attributes are copied verbatim; only skin weights vary.
    double Maximum=0,MinimumNormalDot=1;int64 Compared=0;const auto& OldSkeleton=Sequence->GetSkeleton()->GetReferenceSkeleton();const auto& NewSkeleton=Skeleton->GetReferenceSkeleton();
    for(int32 Sample=0;Sample<=Frames*4;++Sample){FAnimExtractContext Context(Model->GetFrameRate().AsInterval()*Sample/4.);Context.bEnforceCompressedDataSampling=true;TMap<FName,FMatrix> OldPose,NewPose;
     for(const FName Bone:Used){FTransform Pose;Sequence->GetBoneTransform(Pose,FSkeletonPoseBoneIndex(OldSkeleton.FindBoneIndex(Bone)),Context,false);OldPose.Add(Bone,Pose.ToMatrixWithScale());}
     for(const auto& T:Tracks){FTransform Pose;Clip->GetBoneTransform(Pose,FSkeletonPoseBoneIndex(NewSkeleton.FindBoneIndex(T.Key)),Context,false);if(Pose.ContainsNaN())return 1;NewPose.Add(T.Key,Pose.ToMatrixWithScale());}
     for(int32 Part=0;Part<Card->Outfits[0].Parts.Num();++Part){auto* Old=Card->Outfits[0].Parts[Part].SkinnedModel.Get();auto* New=TargetCard->Outfits[0].Parts[Part].SkinnedModel.Get();if(!Old)continue;const auto& DA=*Old->GetMeshDescription(0);const auto& DB=*New->GetMeshDescription(0);FSkeletalMeshConstAttributes A(DA),B(DB);
      for(auto V:DA.Vertices().GetElementIDs()){const FVector P(A.GetVertexPositions()[V]);FVector PA=FVector::ZeroVector,PB=FVector::ZeroVector;for(const auto W:A.GetVertexSkinWeights().Get(V))PA+=FVector(OldPose[Old->GetRefSkeleton().GetBoneName(W.GetBoneIndex())].TransformPosition(P))*W.GetRawWeight()/65535.;for(const auto W:B.GetVertexSkinWeights().Get(V))PB+=FVector(NewPose[New->GetRefSkeleton().GetBoneName(W.GetBoneIndex())].TransformPosition(P))*W.GetRawWeight()/65535.;Maximum=FMath::Max(Maximum,FVector::Distance(PA,PB));++Compared;
       FMatrix MA(ForceInit),MB(ForceInit);for(const auto W:A.GetVertexSkinWeights().Get(V)){const auto& M=OldPose[Old->GetRefSkeleton().GetBoneName(W.GetBoneIndex())];for(int32 R=0;R<3;++R)for(int32 C=0;C<3;++C)MA.M[R][C]+=M.M[R][C]*W.GetRawWeight()/65535.;}for(const auto W:B.GetVertexSkinWeights().Get(V)){const auto& M=NewPose[New->GetRefSkeleton().GetBoneName(W.GetBoneIndex())];for(int32 R=0;R<3;++R)for(int32 C=0;C<3;++C)MB.M[R][C]+=M.M[R][C]*W.GetRawWeight()/65535.;}
       for(const auto Instance:DA.GetVertexVertexInstanceIDs(V)){const FVector N(A.GetVertexInstanceNormals()[Instance]);const FVector NA=FVector(MA.TransformVector(N)),NB=FVector(MB.TransformVector(N));if(NA.SizeSquared()>1e-12)MinimumNormalDot=FMath::Min(MinimumNormalDot,FVector::DotProduct(NA.GetSafeNormal(),NB.GetSafeNormal()));}
       if(MinimumNormalDot<.99985){UE_LOG(LogTemp,Error,TEXT("Selective SRT normal differs index=%d part=%d sample=%d dot=%.9f"),Index,Part,Sample,MinimumNormalDot);return 1;}
       if(Maximum>.1){UE_LOG(LogTemp,Error,TEXT("Selective SRT compressed vertex error exceeds 0.1cm part=%d sample=%d error=%.9f"),Part,Sample,Maximum);return 1;}}
     }
    }
    Walk->Character=TargetCard;Walk->Animations=Library->Animations;Walk->Animations[0].Sequence=Clip;Assets.Add(Skeleton);Assets.Add(Clip);Assets.Add(Walk);Assets.Add(TargetCard);for(auto* Asset:Assets)if(!SavePackedAsset(Asset))return 1;
    UE_LOG(LogTemp,Display,TEXT("Crowd selective SRT complete index=%d bones=%lld/%lld tracks=%d/%d vertex_samples=%lld max_cm=%.9f normal_dot=%.9f schema=1009"),Index,AfterBones,BeforeBones,Tracks.Num(),Model->GetNumBoneTracks(),Compared,Maximum,MinimumNormalDot);
   }
   auto Row=MakeShared<FJsonObject>();Row->SetNumberField(TEXT("index"),Index);Row->SetNumberField(TEXT("used_bones"),Used.Num());Row->SetNumberField(TEXT("native_weighted_bones"),Native.Num());Row->SetNumberField(TEXT("palette_groups"),Groups.Num());Row->SetNumberField(TEXT("eligible_groups"),Eligible);Row->SetNumberField(TEXT("error_tolerance_cm"),Tolerance);Row->SetNumberField(TEXT("potential_removed_bones"),Eligible*2);Row->SetNumberField(TEXT("frames"),Frames);Row->SetNumberField(TEXT("checked_vertices"),double(Vertices));Row->SetNumberField(TEXT("worst_group_error_bound_cm"),Worst);Rows.Add(MakeShared<FJsonValueObject>(Row));
   UE_LOG(LogTemp,Display,TEXT("Crowd affine audit index=%d used=%d native=%d groups=%d eligible=%d frames=%d bound_cm=%.6f"),Index,Used.Num(),Native.Num(),Groups.Num(),Eligible,Frames,Worst);CollectGarbage(RF_NoFlags);
  }
  auto Report=MakeShared<FJsonObject>();Report->SetArrayField(TEXT("cards"),Rows);Report->SetStringField(TEXT("limit"),TEXT("Source keys and quarter-frame interpolation only; eligibility is not a runtime deformation proof."));FString Json;FJsonSerializer::Serialize(Report,TJsonWriterFactory<>::Create(&Json));return FFileHelper::SaveStringToFile(Json,*Output)?0:1;
 }
 if(FParse::Param(*Params,TEXT("MaskGameLow"))){
  for(int32 Index=Start;Index<End;++Index){
   const FString Base=FString::Printf(TEXT("/Game/Scenes/CrowdRoom/Characters/C%03d"),Index),Root=Base/TEXT("GameLow30kMaskR1");
   TStrongObjectPtr<UKKImportedAnimationLibrary> SourceWalk(LoadObject<UKKImportedAnimationLibrary>(nullptr,*(Base/TEXT("GameLow30kR1/Walk/Animations.Animations"))));
   auto* Source=SourceWalk?SourceWalk->Character.Get():nullptr;if(!Source||Source->Schema!=1005||Source->Outfits.Num()!=1)return 1;
   if(FPackageName::DoesPackageExist(Root/TEXT("Character"))){auto* Existing=LoadObject<UKKImportedCharacter>(nullptr,*(Root/TEXT("Character.Character")));if(!Existing||Existing->Schema!=1008||Existing->SourceHash!=Source->SourceHash||!FPackageName::DoesPackageExist(Root/TEXT("Walk/Animations")))return 1;UE_LOG(LogTemp,Display,TEXT("Crowd low mask reused index=%d schema=1008"),Index);SourceWalk.Reset();CollectGarbage(RF_NoFlags);continue;}
   auto* Card=DuplicateObject<UKKImportedCharacter>(Source,CreatePackage(*(Root/TEXT("Character"))),TEXT("Character"));Card->SetFlags(RF_Public|RF_Standalone);Card->Schema=1008;
   TArray<UObject*> Assets;TMap<UMaterialInterface*,UMaterialInterface*> Copies;int32 Serial=0,Changed=0,Preserved=0;
   TFunction<UMaterialInterface*(UMaterialInterface*)> CloneMasked;
   CloneMasked=[&](UMaterialInterface* Original)->UMaterialInterface*{
    if(auto** Existing=Copies.Find(Original))return *Existing;
    const FString Name=FString::Printf(TEXT("Mask_%04d"),Serial++),Path=Root/TEXT("Materials")/Name;if(FPackageName::DoesPackageExist(Path))return nullptr;
    UMaterialInterface* Result=nullptr;
    if(auto* Instance=Cast<UMaterialInstanceConstant>(Original)){
     auto* Parent=CloneMasked(Instance->Parent);if(!Parent)return nullptr;
     auto* Copy=DuplicateObject<UMaterialInstanceConstant>(Instance,CreatePackage(*Path),*Name);Copy->SetParentEditorOnly(Parent);Copy->BasePropertyOverrides.bOverride_BlendMode=true;Copy->BasePropertyOverrides.BlendMode=BLEND_Masked;Copy->BasePropertyOverrides.bOverride_OpacityMaskClipValue=true;Copy->BasePropertyOverrides.OpacityMaskClipValue=.5f;Copy->PostEditChange();Result=Copy;
    }else if(auto* Material=Cast<UMaterial>(Original)){
     auto* Copy=DuplicateObject<UMaterial>(Material,CreatePackage(*Path),*Name);Copy->BlendMode=BLEND_Masked;Copy->OpacityMaskClipValue=.5f;Copy->GetEditorOnlyData()->OpacityMask=Copy->GetEditorOnlyData()->Opacity;Copy->SetUsageByFlag(MATUSAGE_SkeletalMesh,true);Copy->UpdateCachedExpressionData();Copy->PostEditChange();Result=Copy;
    }
    if(Result){Result->ClearFlags(RF_Transient);Result->SetFlags(RF_Public|RF_Standalone);Assets.Add(Result);Copies.Add(Original,Result);}return Result;
   };
   for(int32 Part=0;Part<Card->Outfits[0].Parts.Num();++Part){auto& Entry=Card->Outfits[0].Parts[Part];auto* Original=Entry.SkinnedModel.Get();if(!Original)continue;
    const FString Name=FString::Printf(TEXT("Part_%03d"),Part);auto* Mesh=DuplicateObject<USkeletalMesh>(Original,CreatePackage(*(Root/TEXT("Skins")/Name)),*Name);Mesh->SetFlags(RF_Public|RF_Standalone);
    for(auto& Slot:Mesh->GetMaterials()){
     auto* Material=Slot.MaterialInterface.Get();if(!Material)return 1;
     if(Material->GetBlendMode()!=BLEND_Translucent||Material->GetMaterial()->bDisableDepthTest||Material->GetMaterial()->MaterialDomain!=MD_Surface){++Preserved;continue;}
     auto* Reduced=CloneMasked(Material);if(!Reduced||Reduced->GetBlendMode()!=BLEND_Masked)return 1;Slot.MaterialInterface=Reduced;++Changed;
    }
    Mesh->PostEditChange();FAssetCompilingManager::Get().FinishAllCompilation();if(!SameSkinSource(Original,Mesh,false)){UE_LOG(LogTemp,Error,TEXT("Low mask geometry changed index=%d part=%d"),Index,Part);return 1;}
    Entry.SkinnedModel=Mesh;Assets.Add(Mesh);
   }
   auto* Walk=DuplicateObject<UKKImportedAnimationLibrary>(SourceWalk.Get(),CreatePackage(*(Root/TEXT("Walk/Animations"))),TEXT("Animations"));Walk->SetFlags(RF_Public|RF_Standalone);Walk->Character=Card;
   for(auto* Asset:Assets)if(!SavePackedAsset(Asset))return 1;if(!SavePackedAsset(Walk)||!SavePackedAsset(Card))return 1;
   UE_LOG(LogTemp,Display,TEXT("Crowd low mask complete index=%d changed_slots=%d preserved_slots=%d geometry_unchanged=1 schema=1008"),Index,Changed,Preserved);
   SourceWalk.Reset();CollectGarbage(RF_NoFlags);
  }return 0;
 }
 if(FParse::Param(*Params,TEXT("CompactGameLow"))){
  for(int32 Index=Start;Index<End;++Index){
   const FString Base=FString::Printf(TEXT("/Game/Scenes/CrowdRoom/Characters/C%03d"),Index),Root=Base/TEXT("GameLow30kR2");
   TStrongObjectPtr<UKKImportedAnimationLibrary> SourceWalk(LoadObject<UKKImportedAnimationLibrary>(nullptr,*(Base/TEXT("GameLow30kR1/Walk/Animations.Animations"))));
   auto* Source=SourceWalk?SourceWalk->Character.Get():nullptr;if(!Source||Source->Schema!=1005||Source->Outfits.Num()!=1)return 1;
   if(FPackageName::DoesPackageExist(Root/TEXT("Character"))){auto* Existing=LoadObject<UKKImportedCharacter>(nullptr,*(Root/TEXT("Character.Character")));if(!Existing||Existing->Schema!=1007||Existing->SourceHash!=Source->SourceHash||!FPackageName::DoesPackageExist(Root/TEXT("Walk/Animations")))return 1;}
   else{
    auto* Card=DuplicateObject<UKKImportedCharacter>(Source,CreatePackage(*(Root/TEXT("Character"))),TEXT("Character"));Card->SetFlags(RF_Public|RF_Standalone);Card->Schema=1007;
    TArray<UObject*> Assets;TSet<FName> FollowerBones;FString Error;int32 BeforeBones=0,AfterBones=0;
    // Compact followers first, then keep their complete private bone union in the leader.
    for(int32 Part=1;Part<Card->Outfits[0].Parts.Num();++Part){auto& Entry=Card->Outfits[0].Parts[Part];if(!Entry.SkinnedModel)continue;auto* Original=Entry.SkinnedModel.Get();BeforeBones+=Original->GetRefSkeleton().GetNum();
     auto* Compact=OKCompactCrowdMesh(Original,Root/FString::Printf(TEXT("Skins/Part_%03d"),Part),Error,{});
     if(!Compact||!SameSkinSource(Original,Compact)){UE_LOG(LogTemp,Error,TEXT("Low compact follower failed index=%d part=%d %s"),Index,Part,*Error);return 1;}
     Entry.SkinnedModel=Compact;Assets.Add(Compact);AfterBones+=Compact->GetRefSkeleton().GetNum();for(int32 Bone=0;Bone<Compact->GetRefSkeleton().GetNum();++Bone)FollowerBones.Add(Compact->GetRefSkeleton().GetBoneName(Bone));
    }
    auto& Leader=Card->Outfits[0].Parts[0];if(!Leader.SkinnedModel)return 1;BeforeBones+=Leader.SkinnedModel->GetRefSkeleton().GetNum();
    auto* Compact=OKCompactCrowdMesh(Leader.SkinnedModel,Root/TEXT("Skins/Leader"),Error,FollowerBones);
    if(!Compact||!SameSkinSource(Leader.SkinnedModel,Compact)){UE_LOG(LogTemp,Error,TEXT("Low compact leader failed index=%d %s"),Index,*Error);return 1;}
    Leader.SkinnedModel=Compact;Assets.Add(Compact);AfterBones+=Compact->GetRefSkeleton().GetNum();
    auto* Walk=DuplicateObject<UKKImportedAnimationLibrary>(SourceWalk.Get(),CreatePackage(*(Root/TEXT("Walk/Animations"))),TEXT("Animations"));Walk->SetFlags(RF_Public|RF_Standalone);Walk->Character=Card;
    if(!AuditRig(Source,SourceWalk.Get(),Card,Walk))return 1;
    for(auto* Asset:Assets)if(!SavePackedAsset(Asset))return 1;if(!SavePackedAsset(Walk)||!SavePackedAsset(Card))return 1;
    UE_LOG(LogTemp,Display,TEXT("Crowd low compact complete index=%d bones=%d/%d schema=1007 geometry_unchanged=1 samples=13"),Index,AfterBones,BeforeBones);
   }
   SourceWalk.Reset();CollectGarbage(RF_NoFlags);
  }return 0;
 }
 if(FParse::Param(*Params,TEXT("PrepareGpuPose")))return OKPrepareCrowdGpuPose(Start,End,Params);
 if(FParse::Param(*Params,TEXT("OptimizeQuality")))return OKOptimizeCrowdQuality(Start,End,Params);
 if(FParse::Param(*Params,TEXT("AuditHairOutlines"))){
  FString Output;if(!FParse::Value(*Params,TEXT("Output="),Output)||IFileManager::Get().FileExists(*Output))return 1;
  TArray<TSharedPtr<FJsonValue>> Rows;
  for(int32 Index=Start;Index<End;++Index){const FString Base=FString::Printf(TEXT("/Game/Scenes/CrowdRoom/Characters/C%03d"),Index);
   for(const FString Variant:{FString(),FString(TEXT("NearDetailR5"))}){TStrongObjectPtr<UKKImportedCharacter> Card(LoadObject<UKKImportedCharacter>(nullptr,*(Base/Variant/TEXT("Character.Character"))));if(!Card||Card->Outfits.IsEmpty())return 1;FAssetCompilingManager::Get().FinishAllCompilation();
    for(const auto& Part:Card->Outfits[0].Parts)if(Part.SkinnedModel&&(Variant.IsEmpty()||Part.Role.Contains(TEXT("hair"),ESearchCase::IgnoreCase))){auto* Mesh=Part.SkinnedModel.Get();const auto* Desc=Mesh->GetMeshDescription(0);if(!Desc)return 1;FSkeletalMeshConstAttributes A(*Desc);
     for(auto Group:Desc->PolygonGroups().GetElementIDs()){const FName Slot=A.GetPolygonGroupMaterialSlotNames()[Group];const int32 M=Mesh->GetMaterials().IndexOfByPredicate([&](const auto& S){return S.MaterialSlotName==Slot||S.ImportedMaterialSlotName==Slot;});if(M<0)return 1;auto* Mat=Mesh->GetMaterials()[M].MaterialInterface.Get();FBox3f Bounds(ForceInit);int32 Count=0,Positive=0,Negative=0;
      for(auto T:Desc->Triangles().GetElementIDs())if(Desc->GetTrianglePolygonGroup(T)==Group){++Count;const auto V=Desc->GetTriangleVertices(T);for(auto P:V)Bounds+=A.GetVertexPositions()[P];const auto C=Desc->GetTriangleVertexInstances(T);const FVector3f N=FVector3f::CrossProduct(A.GetVertexPositions()[V[1]]-A.GetVertexPositions()[V[0]],A.GetVertexPositions()[V[2]]-A.GetVertexPositions()[V[0]]);const float Dot=FVector3f::DotProduct(N,A.GetVertexInstanceNormals()[C[0]]+A.GetVertexInstanceNormals()[C[1]]+A.GetVertexInstanceNormals()[C[2]]);if(Dot>0)++Positive;else if(Dot<0)++Negative;}
      auto Row=MakeShared<FJsonObject>();Row->SetNumberField(TEXT("index"),Index);Row->SetStringField(TEXT("variant"),Variant);Row->SetStringField(TEXT("part"),Part.Name);Row->SetStringField(TEXT("mesh"),Mesh->GetPathName());Row->SetNumberField(TEXT("group"),Group.GetValue());Row->SetNumberField(TEXT("material_index"),M);Row->SetStringField(TEXT("material"),Mat?Mat->GetPathName():TEXT("NULL"));Row->SetBoolField(TEXT("wpo"),Mat&&Mat->IsUsingWorldPositionOffset_Concurrent(GShaderPlatformForFeatureLevel[ERHIFeatureLevel::SM6]));Row->SetNumberField(TEXT("triangles"),Count);Row->SetNumberField(TEXT("positive_winding_normal"),Positive);Row->SetNumberField(TEXT("negative_winding_normal"),Negative);Row->SetBoolField(TEXT("two_sided"),Mat&&Mat->IsTwoSided());Row->SetBoolField(TEXT("authored_wpo"),Mat&&Mat->GetMaterial()->GetExpressionInputForProperty(MP_WorldPositionOffset)->Expression);Row->SetStringField(TEXT("bounds"),Bounds.ToString());Rows.Add(MakeShared<FJsonValueObject>(Row));
     }
    }
   }CollectGarbage(RF_NoFlags);
  }auto Root=MakeShared<FJsonObject>();Root->SetArrayField(TEXT("groups"),Rows);FString Text;FJsonSerializer::Serialize(Root,TJsonWriterFactory<>::Create(&Text));return FFileHelper::SaveStringToFile(Text,*Output)?0:1;
 }
 if(FParse::Param(*Params,TEXT("BuildBodyCoverage")))return OKBuildCrowdBodyCoverage(Start,End,*Params);
 if(FParse::Param(*Params,TEXT("RepairBindings")))return OKRepairCrowdBindings(Start,End,*Params);
 if(FParse::Param(*Params,TEXT("BuildNearHair"))){
  FString SourceRoot,NearVariant=TEXT("NearDetailR6");FParse::Value(*Params,TEXT("SourceRoot="),SourceRoot);FParse::Value(*Params,TEXT("NearVariant="),NearVariant);
  if((!SourceRoot.IsEmpty()&&(!FPackageName::IsValidLongPackageName(SourceRoot)||!SourceRoot.StartsWith(TEXT("/Game/"))||End-Start!=1))||NearVariant.IsEmpty())return 1;
  for(TCHAR C:NearVariant)if(!FChar::IsAlnum(C)&&C!=TEXT('_'))return 1;
  for(int32 Index=Start;Index<End;++Index){const FString Base=SourceRoot.IsEmpty()?FString::Printf(TEXT("/Game/Scenes/CrowdRoom/Characters/C%03d"),Index):SourceRoot,Root=Base/NearVariant;
   if(FPackageName::DoesPackageExist(Root/TEXT("Face"))){
    TStrongObjectPtr<UOKCrowdFaceDetail> Existing(LoadObject<UOKCrowdFaceDetail>(nullptr,*(Root/TEXT("Face.Face"))));TStrongObjectPtr<UKKImportedCharacter> Native(LoadObject<UKKImportedCharacter>(nullptr,*(Base/TEXT("Character.Character"))));TStrongObjectPtr<UKKImportedAnimationLibrary> Walk(LoadObject<UKKImportedAnimationLibrary>(nullptr,*(Base/TEXT("GameLow30kR1/Walk/Animations.Animations"))));
    if(!Existing||!Existing->Character||!Native||Existing->SourceHash!=Native->SourceHash||Existing->Character->SourceHash!=Native->SourceHash||!Walk||!Walk->Character||Walk->Character->SourceHash!=Native->SourceHash||Walk->Animations.IsEmpty()||!Walk->Animations[0].Sequence||Existing->Character->Outfits.Num()!=1||Existing->NearHairTriangles>10000)return 1;FAssetCompilingManager::Get().FinishAllCompilation();int32 Count=0;
    for(const auto& Part:Existing->Character->Outfits[0].Parts)if(Part.Role==TEXT("hair")){if(!Part.SkinnedModel)return 1;TSet<UMaterialInterface*> Materials;for(const auto& M:Part.SkinnedModel->GetMaterials())Materials.Add(M.MaterialInterface.Get());const int32 Issues=OKCountCrowdHairOutlineIssues(Part.SkinnedModel,Walk->Animations[0].Sequence,Materials);if(Issues!=0){UE_LOG(LogTemp,Error,TEXT("Near hair cache rejected root=%s mesh=%s outline_issues=%d; rebuild a new recipe instead of silently reusing old data"),*Root,*Part.SkinnedModel->GetPathName(),Issues);return 1;}for(const auto& S:Part.SkinnedModel->GetResourceForRendering()->LODRenderData[0].RenderSections)Count+=S.NumTriangles;}
    if(Count!=Existing->NearHairTriangles)return 1;UE_LOG(LogTemp,Display,TEXT("Near hair cache validated root=%s hair=%d outline_issues=0"),*Root,Count);CollectGarbage(RF_NoFlags);continue;
   }
   {TStrongObjectPtr<UKKImportedCharacter> Native(LoadObject<UKKImportedCharacter>(nullptr,*(Base/TEXT("Character.Character"))));TStrongObjectPtr<UKKImportedCharacter> Low(LoadObject<UKKImportedCharacter>(nullptr,*(Base/TEXT("GameLow30kR1/Character.Character"))));TStrongObjectPtr<UKKImportedAnimationLibrary> Walk(LoadObject<UKKImportedAnimationLibrary>(nullptr,*(Base/TEXT("GameLow30kR1/Walk/Animations.Animations"))));TStrongObjectPtr<UOKCrowdFaceDetail> Previous(LoadObject<UOKCrowdFaceDetail>(nullptr,*(Base/TEXT("FaceDetailR2/Face.Face"))));
    if(!Native||!Low||!Walk||!Previous||!Previous->Character||Native->Outfits.IsEmpty()||Low->Outfits.IsEmpty()||Previous->Character->Outfits.IsEmpty()||Walk->Animations.IsEmpty()||!Walk->Animations[0].Sequence||Walk->Character!=Low.Get()||Native->SourceHash!=Low->SourceHash||Previous->SourceHash!=Low->SourceHash){UE_LOG(LogTemp,Error,TEXT("Near hair generation requires matching native, low, face and walking assets under %s"),*Base);return 1;}FAssetCompilingManager::Get().FinishAllCompilation();
    auto* Face=FreshPackedAsset<UOKCrowdFaceDetail>(Root/TEXT("Face"));if(!Face)return 1;TStrongObjectPtr<UOKCrowdFaceDetail> Hold(Face);Face->SourceHash=Previous->SourceHash;Face->LowPartNames=Previous->LowPartNames;Face->LowPackedSlots=Previous->LowPackedSlots;
    auto* Card=FreshPackedAsset<UKKImportedCharacter>(Root/TEXT("Character"));if(!Card)return 1;Card->SourceHash=Previous->Character->SourceHash;Card->Schema=1015;Card->Outfits=Previous->Character->Outfits;Face->Character=Card;
    const auto* Packed=Low->Outfits[0].Parts.FindByPredicate([](const auto& P){return P.Name==TEXT("PackedVisibleParts");});if(!Packed||!Packed->SkinnedModel)return 1;
    struct FHairGroup {FKKImportedOutfit Outfit;TArray<UMaterialInterface*> Materials;int32 Source=0;};TArray<FHairGroup> Groups;Groups.AddDefaulted();int32 Offset=0,SourceTotal=0;
    for(const auto& Part:Native->Outfits[0].Parts){const bool IsPacked=Eligible(Part);const int32 Materials=Part.SkinnedModel?Part.SkinnedModel->GetMaterials().Num():0;
     if(Part.SkinnedModel&&Part.MouthDefaultVisible&&(Part.Role.Contains(TEXT("hair"))||OKCrowdIsHeadAccessory(Part,Walk->Animations[0].Sequence))){int32 Source=0,LowCount=0;for(const auto& S:Part.SkinnedModel->GetResourceForRendering()->LODRenderData[0].RenderSections)Source+=S.NumTriangles;
      const auto* Existing=Low->Outfits[0].Parts.FindByPredicate([&](const auto& P){return P.Name==Part.Name;});
      if(IsPacked){for(const auto& S:Packed->SkinnedModel->GetResourceForRendering()->LODRenderData[0].RenderSections)if(S.MaterialIndex>=Offset&&S.MaterialIndex<Offset+Materials)LowCount+=S.NumTriangles;}
      else if(Existing&&Existing->SkinnedModel)for(const auto& S:Existing->SkinnedModel->GetResourceForRendering()->LODRenderData[0].RenderSections)LowCount+=S.NumTriangles;
      if(Source>0){const int32 GroupIndex=IsPacked?0:Groups.AddDefaulted();auto& Group=Groups[GroupIndex];Group.Outfit.Parts.Add(Part);Group.Outfit.Skeleton=Native->Outfits[0].Skeleton;Group.Source+=Source;SourceTotal+=Source;Face->LowHairTriangles+=LowCount;
       for(int32 M=0;M<Materials;++M){UMaterialInterface* Material=nullptr;if(IsPacked&&Packed->SkinnedModel->GetMaterials().IsValidIndex(Offset+M))Material=Packed->SkinnedModel->GetMaterials()[Offset+M].MaterialInterface;else if(Existing&&Existing->SkinnedModel&&Existing->SkinnedModel->GetMaterials().IsValidIndex(M))Material=Existing->SkinnedModel->GetMaterials()[M].MaterialInterface;if(!Material)return 1;Group.Materials.Add(Material);}
       if(IsPacked)for(int32 M=0;M<Materials;++M)Face->LowPackedSlots.AddUnique(Offset+M);else Face->LowPartNames.AddUnique(Part.Name);
      }
     }if(IsPacked)Offset+=Materials;
    }
    Groups.RemoveAll([](const auto& G){return G.Source==0;});int32 Minimum=0;for(const auto& G:Groups)Minimum+=FMath::Min(G.Source,48);if(SourceTotal>10000&&Minimum>10000){UE_LOG(LogTemp,Error,TEXT("Hair group minimum exceeds limit index=%d"),Index);return 1;}
    for(int32 G=0;G<Groups.Num();++G){auto& Group=Groups[G];const uint32 Limit=SourceTotal<=10000?Group.Source:FMath::Min(Group.Source,FMath::Min(Group.Source,48)+int32(int64(10000-Minimum)*(Group.Source-FMath::Min(Group.Source,48))/FMath::Max(1,SourceTotal-Minimum)));
     FString Error;TArray<FSourceSkin> Sources;USkeletalMesh* Full=Group.Outfit.Parts.Num()>1?Pack(Group.Outfit,Root/FString::Printf(TEXT("HairSource%d"),G),Sources,Error):Group.Outfit.Parts[0].SkinnedModel.Get();if(!Full){UE_LOG(LogTemp,Error,TEXT("Hair pack failed index=%d %s"),Index,*Error);return 1;}
     USkeletalMesh* Geometry=OKBuildCrowdNearHair(Full,Walk->Animations[0].Sequence,Root/FString::Printf(TEXT("HairBuild%d"),G),Limit);if(!Geometry)return 1;
     auto* Mesh=OKCompactCrowdMesh(Geometry,Root/FString::Printf(TEXT("Hair%d"),G),Error,{});if(!Mesh||!SameSkinSource(Geometry,Mesh)||Mesh->GetMaterials().Num()!=Group.Materials.Num())return 1;
     for(int32 M=0;M<Group.Materials.Num();++M)Mesh->GetMaterials()[M].MaterialInterface=Group.Materials[M];if(!SavePackedAsset(Mesh))return 1;
     FKKImportedPart Near;Near.Name=FString::Printf(TEXT("NearHair%d"),G);Near.Role=TEXT("hair");Near.SkinnedModel=Mesh;Near.Transform=Group.Outfit.Parts.Num()>1?FTransform::Identity:Group.Outfit.Parts[0].Transform;Card->Outfits[0].Parts.Add(Near);
     int32 Actual=0;for(const auto& S:Mesh->GetResourceForRendering()->LODRenderData[0].RenderSections)Actual+=S.NumTriangles;Face->NearHairTriangles+=Actual;UE_LOG(LogTemp,Display,TEXT("Crowd hair group index=%d group=%d source=%d limit=%u actual=%d"),Index,G,Group.Source,Limit,Actual);
    }
    if(Face->NearHairTriangles>10000||(SourceTotal<=10000&&Face->NearHairTriangles!=SourceTotal)){UE_LOG(LogTemp,Error,TEXT("Crowd hair limit failed index=%d source=%d output=%d"),Index,SourceTotal,Face->NearHairTriangles);return 1;}
    if(!SavePackedAsset(Card)||!SavePackedAsset(Face))return 1;UE_LOG(LogTemp,Display,TEXT("Crowd near detail complete index=%d hair=%d low=%d original=%d"),Index,Face->NearHairTriangles,Face->LowHairTriangles,SourceTotal);
   }CollectGarbage(RF_NoFlags);
  }return 0;
 }
 if(FParse::Param(*Params,TEXT("BuildFaceDetail"))){
  FString SourceRoot;FParse::Value(*Params,TEXT("SourceRoot="),SourceRoot);if(!SourceRoot.IsEmpty()&&(!FPackageName::IsValidLongPackageName(SourceRoot)||!SourceRoot.StartsWith(TEXT("/Game/"))||End-Start!=1))return 1;
  for(int32 Index=Start;Index<End;++Index){
   const FString Base=SourceRoot.IsEmpty()?FString::Printf(TEXT("/Game/Scenes/CrowdRoom/Characters/C%03d"),Index):SourceRoot,Root=Base/TEXT("FaceDetailR2");
   if(FPackageName::DoesPackageExist(Root/TEXT("Face")))continue;
   {TStrongObjectPtr<UKKImportedCharacter> Native(LoadObject<UKKImportedCharacter>(nullptr,*(Base/TEXT("Character.Character"))));if(!Native||Native->Outfits.IsEmpty())return 1;
    FAssetCompilingManager::Get().FinishAllCompilation();auto* Face=FreshPackedAsset<UOKCrowdFaceDetail>(Root/TEXT("Face"));if(!Face)return 1;TStrongObjectPtr<UOKCrowdFaceDetail> Hold(Face);Face->SourceHash=Native->SourceHash;
    auto* Card=FreshPackedAsset<UKKImportedCharacter>(Root/TEXT("Character"));if(!Card)return 1;Face->Character=Card;Card->SourceHash=Native->SourceHash;Card->Schema=1011;Card->Outfits={Native->Outfits[0]};auto& Outfit=Card->Outfits[0];Outfit.Parts.Reset();Outfit.AnimationSnapshot.Reset();
    int32 Offset=0;for(const auto& Part:Native->Outfits[0].Parts){
     const bool Eye=Part.NativeRenderer.Contains(TEXT("hitomi"))||Part.NativeRenderer.Contains(TEXT("eye"))||Part.NativeRenderer.Contains(TEXT("mayuge"))||Part.NativeRenderer.Contains(TEXT("nose"));
     if(Eligible(Part)){if(Eye){Outfit.Parts.Add(Part);for(int32 M=0;M<Part.SkinnedModel->GetMaterials().Num();++M)Face->LowPackedSlots.Add(Offset+M);}Offset+=Part.SkinnedModel->GetMaterials().Num();}
     else if(Part.SkinnedModel&&Part.MouthDefaultVisible&&!Part.MouthPatterns.IsEmpty()){Outfit.Parts.Add(Part);Face->LowPartNames.Add(Part.Name);}
    }
    if(Face->LowPackedSlots.IsEmpty()||Face->LowPartNames.IsEmpty())return 1;
    // Facial translucency queues belong to individual renderers. Packing iris
    // and sclera into one component discards their authored sort priorities.
    FString Error;for(int32 P=0;P<Outfit.Parts.Num();++P){auto& Part=Outfit.Parts[P];Part.Model=nullptr;auto* Original=Part.SkinnedModel.Get();Part.SkinnedModel=OKCompactCrowdMesh(Original,Root/FString::Printf(TEXT("Part%d"),P),Error,{});if(!Part.SkinnedModel||!SameSkinSource(Original,Part.SkinnedModel)||!SavePackedAsset(Part.SkinnedModel)){UE_LOG(LogTemp,Error,TEXT("Face compact failed index=%d part=%d %s"),Index,P,*Error);return 1;}}
    if(!SavePackedAsset(Card)||!SavePackedAsset(Face))return 1;
    UE_LOG(LogTemp,Display,TEXT("Crowd face detail complete index=%d eye_slots=%d head_parts=%d"),Index,Face->LowPackedSlots.Num(),Face->LowPartNames.Num());
   }CollectGarbage(RF_NoFlags);
  }return 0;
 }
 if(FParse::Param(*Params,TEXT("AuditFaceSlots"))){
  FString Output;if(!FParse::Value(*Params,TEXT("Output="),Output)||IFileManager::Get().FileExists(*Output))return 1;
  TArray<TSharedPtr<FJsonValue>> Cards;
  for(int32 Index=Start;Index<End;++Index){
   const FString Path=FString::Printf(TEXT("/Game/Scenes/CrowdRoom/Characters/C%03d/Character.Character"),Index);
   {TStrongObjectPtr<UKKImportedCharacter> Card(LoadObject<UKKImportedCharacter>(nullptr,*Path));if(!Card||Card->Outfits.IsEmpty())return 1;
    FAssetCompilingManager::Get().FinishAllCompilation();int32 Offset=0;TArray<TSharedPtr<FJsonValue>> Rows;TArray<FString> Slots;int64 Count=0;
    for(const auto& Part:Card->Outfits[0].Parts)if(Eligible(Part)){
     const bool Face=Part.NativeRenderer.Contains(TEXT("hitomi"))||Part.NativeRenderer.Contains(TEXT("eye"))||Part.NativeRenderer.Contains(TEXT("mayuge"))||Part.NativeRenderer.Contains(TEXT("nose"));
     if(Face||FParse::Param(*Params,TEXT("AuditAllSlots"))){const auto* Data=Part.SkinnedModel->GetResourceForRendering();if(!Data||Data->LODRenderData.IsEmpty())return 1;auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("renderer"),Part.NativeRenderer);Row->SetStringField(TEXT("role"),Part.Role);Row->SetNumberField(TEXT("offset"),Offset);int64 Triangles=0;for(const auto& S:Data->LODRenderData[0].RenderSections)Triangles+=S.NumTriangles;Count+=Triangles;Row->SetNumberField(TEXT("triangles"),Triangles);Rows.Add(MakeShared<FJsonValueObject>(Row));for(int32 M=0;M<Part.SkinnedModel->GetMaterials().Num();++M)Slots.Add(FString::FromInt(Offset+M));}
     Offset+=Part.SkinnedModel->GetMaterials().Num();
    }
    auto Row=MakeShared<FJsonObject>();Row->SetNumberField(TEXT("index"),Index);Row->SetStringField(TEXT("keep_materials"),FString::Join(Slots,TEXT(",")));Row->SetNumberField(TEXT("triangles"),Count);Row->SetArrayField(TEXT("renderers"),Rows);Cards.Add(MakeShared<FJsonValueObject>(Row));
   }CollectGarbage(RF_NoFlags);
  }
  auto Root=MakeShared<FJsonObject>();Root->SetArrayField(TEXT("cards"),Cards);FString Text;FJsonSerializer::Serialize(Root,TJsonWriterFactory<>::Create(&Text));return FFileHelper::SaveStringToFile(Text,*Output)?0:1;
 }
 if(FParse::Param(*Params,TEXT("AuditMaterials"))){
  FString Tag;FParse::Value(*Params,TEXT("MaterialAuditTag="),Tag);if(Tag.IsEmpty()||Tag.Len()>64)return 1;for(TCHAR C:Tag)if(!FChar::IsAlnum(C)&&C!=TEXT('_')&&C!=TEXT('-'))return 1;
  const FString Output=FPaths::ProjectSavedDir()/TEXT("Crowd")/(Tag+TEXT(".json"));if(IFileManager::Get().FileExists(*Output))return 1;
  TArray<TSharedPtr<FJsonValue>> Cards;int32 TotalSections=0,TotalDistinct=0;
  for(int32 Index=Start;Index<End;++Index){
   const FString Path=FString::Printf(TEXT("/Game/Scenes/CrowdRoom/Characters/C%03d/PackedRig/Character.Character"),Index);
   {TStrongObjectPtr<UKKImportedCharacter> Character(LoadObject<UKKImportedCharacter>(nullptr,*Path));if(!Character||Character->Outfits.IsEmpty())return 1;
    FAssetCompilingManager::Get().FinishAllCompilation();TMap<UMaterialInterface*,int32> Uses;int32 Sections=0;
    for(const auto& Part:Character->Outfits[0].Parts)if(Part.MouthDefaultVisible&&Part.SkinnedModel){
     const auto* Data=Part.SkinnedModel->GetResourceForRendering();if(!Data||Data->LODRenderData.IsEmpty())return 1;
     for(const auto& Section:Data->LODRenderData[0].RenderSections){if(Section.bDisabled||Section.NumTriangles==0)continue;
      if(!Part.SkinnedModel->GetMaterials().IsValidIndex(Section.MaterialIndex))return 1;
      auto* Material=Part.SkinnedModel->GetMaterials()[Section.MaterialIndex].MaterialInterface.Get();if(!Material)return 1;
      ++Uses.FindOrAdd(Material);++Sections;
     }
    }
    auto Card=MakeShared<FJsonObject>();Card->SetNumberField(TEXT("index"),Index);Card->SetNumberField(TEXT("sections"),Sections);Card->SetNumberField(TEXT("distinct_material_objects"),Uses.Num());
    TArray<TSharedPtr<FJsonValue>> Materials;for(const auto& Entry:Uses){auto Item=MakeShared<FJsonObject>();Item->SetStringField(TEXT("path"),Entry.Key->GetPathName());Item->SetNumberField(TEXT("sections"),Entry.Value);Materials.Add(MakeShared<FJsonValueObject>(Item));}
    Card->SetArrayField(TEXT("materials"),Materials);Cards.Add(MakeShared<FJsonValueObject>(Card));TotalSections+=Sections;TotalDistinct+=Uses.Num();
    UE_LOG(LogTemp,Display,TEXT("Crowd material audit index=%d visible_sections=%d distinct_materials=%d"),Index,Sections,Uses.Num());
   }CollectGarbage(RF_NoFlags);
  }
  auto Report=MakeShared<FJsonObject>();Report->SetArrayField(TEXT("cards"),Cards);Report->SetNumberField(TEXT("visible_sections"),TotalSections);Report->SetNumberField(TEXT("distinct_material_objects_per_card_sum"),TotalDistinct);Report->SetBoolField(TEXT("complete"),true);
  FString Text;FJsonSerializer::Serialize(Report,TJsonWriterFactory<>::Create(&Text));return FFileHelper::SaveStringToFile(Text,*Output)?0:1;
 }
 if(FParse::Param(*Params,TEXT("VerifyRig"))){
  for(int32 Index=Start;Index<End;++Index){const FString Base=FString::Printf(TEXT("/Game/Scenes/CrowdRoom/Characters/C%03d"),Index);
   TStrongObjectPtr<UKKImportedAnimationLibrary> Before(LoadObject<UKKImportedAnimationLibrary>(nullptr,*(Base/TEXT("Packed/Walk/Animations.Animations"))));TStrongObjectPtr<UKKImportedAnimationLibrary> After(LoadObject<UKKImportedAnimationLibrary>(nullptr,*(Base/TEXT("PackedRig/Walk/Animations.Animations"))));FAssetCompilingManager::Get().FinishAllCompilation();
   if(!Before||!After||!Before->Character||!After->Character||Before->Character->SourceHash!=After->Character->SourceHash||!AuditRig(Before->Character,Before.Get(),After->Character,After.Get()))return 1;
   UE_LOG(LogTemp,Display,TEXT("Crowd packed rig runtime verified index=%d"),Index);CollectGarbage(RF_NoFlags);
  }return 0;
 }
 const bool CompactRig=FParse::Param(*Params,TEXT("CompactRig"));
 if(CompactRig||FParse::Param(*Params,TEXT("CompactFollowers"))){
  for(int32 Index=Start;Index<End;++Index){const FString Base=FString::Printf(TEXT("/Game/Scenes/CrowdRoom/Characters/C%03d"),Index),Destination=Base/(CompactRig?TEXT("PackedRig"):TEXT("PackedCompact"));
   {TStrongObjectPtr<UKKImportedCharacter> Packed(LoadObject<UKKImportedCharacter>(nullptr,*(Base/TEXT("Packed/Character.Character"))));TStrongObjectPtr<UKKImportedCharacter> Compact(LoadObject<UKKImportedCharacter>(nullptr,*(Base/TEXT("Compact/Character.Character"))));TStrongObjectPtr<UKKImportedAnimationLibrary> Walk(LoadObject<UKKImportedAnimationLibrary>(nullptr,*(Base/TEXT("Packed/Walk/Animations.Animations"))));
    if(!Packed||!Compact||!Walk||Walk->Character!=Packed.Get()||Packed->SourceHash!=Compact->SourceHash)return 1;
    if(!FPackageName::DoesPackageExist(Destination/TEXT("Character"))){
     auto* Character=FreshPackedAsset<UKKImportedCharacter>(Destination/TEXT("Character"));Character->Schema=Packed->Schema;Character->SourceHash=Packed->SourceHash;Character->Outfits=Packed->Outfits;int32 Remapped=0;
     for(auto& Part:Character->Outfits[0].Parts)if(Part.SkinnedModel&&Part.Name!=TEXT("PackedVisibleParts")){
      const auto* Other=Compact->Outfits[0].Parts.FindByPredicate([&](const auto& P){return P.Name==Part.Name;});
      if(!Other||!SameSkinSource(Part.SkinnedModel,Other->SkinnedModel)){UE_LOG(LogTemp,Error,TEXT("Crowd packed compact mismatch index=%d part=%s"),Index,*Part.Name);return 1;}
      Part.SkinnedModel=Other->SkinnedModel;++Remapped;
     }
     USkeletalMesh* LeaderMesh=nullptr;
     if(CompactRig){TSet<FName> Required;
      for(const auto& Part:Character->Outfits[0].Parts)if(Part.SkinnedModel&&Part.Name!=TEXT("PackedVisibleParts")){const auto& Ref=Part.SkinnedModel->GetRefSkeleton();for(int32 Bone=0;Bone<Ref.GetNum();++Bone)Required.Add(Ref.GetBoneName(Bone));}
      auto& Leader=Character->Outfits[0].Parts[0];FString Error;LeaderMesh=OKCompactCrowdMesh(Leader.SkinnedModel,Destination/TEXT("Skin"),Error,Required);
      if(!LeaderMesh||!SameSkinSource(Leader.SkinnedModel,LeaderMesh)){UE_LOG(LogTemp,Error,TEXT("Crowd compact leader failed index=%d %s"),Index,*Error);return 1;}Leader.SkinnedModel=LeaderMesh;
     }
     auto* Library=FreshPackedAsset<UKKImportedAnimationLibrary>(Destination/TEXT("Walk/Animations"));Library->Character=Character;Library->Animations=Walk->Animations;
     if((LeaderMesh&&!SavePackedAsset(LeaderMesh))||!SavePackedAsset(Library)||!SavePackedAsset(Character))return 1;
     UE_LOG(LogTemp,Display,TEXT("Crowd packed compact source verified index=%d followers=%d compact_leader=%d"),Index,Remapped,CompactRig);
    }
   }CollectGarbage(RF_NoFlags);UE_LOG(LogTemp,Display,TEXT("Crowd packed compact complete index=%d"),Index);
  }return 0;
 }
 for(int32 Index=Start;Index<End;++Index){const FString Base=FString::Printf(TEXT("/Game/Scenes/CrowdRoom/Characters/C%03d"),Index),Destination=Base/TEXT("Packed");
  {TStrongObjectPtr<UKKImportedCharacter> Original(LoadObject<UKKImportedCharacter>(nullptr,*(Base/TEXT("Character.Character"))));TStrongObjectPtr<UKKImportedAnimationLibrary> Walk(LoadObject<UKKImportedAnimationLibrary>(nullptr,*(Base/TEXT("Walk/Animations.Animations"))));if(!Original||!Walk||Walk->Character!=Original.Get())return 1;
   if(FPackageName::DoesPackageExist(Destination/TEXT("Character"))){auto* Existing=LoadObject<UKKImportedCharacter>(nullptr,*(Destination/TEXT("Character.Character")));if(!Existing||Existing->SourceHash!=Original->SourceHash||!FPackageName::DoesPackageExist(Destination/TEXT("Walk/Animations")))return 1;}
   else{TArray<FSourceSkin> Sources;FString Error;auto* Mesh=Pack(Original->Outfits[0],Destination/TEXT("Skin"),Sources,Error);if(!Mesh){UE_LOG(LogTemp,Error,TEXT("Crowd pack index=%d %s"),Index,*Error);return 1;}
    auto* Character=FreshPackedAsset<UKKImportedCharacter>(Destination/TEXT("Character"));Character->Schema=Original->Schema;Character->SourceHash=Original->SourceHash;Character->Outfits=Original->Outfits;auto& Parts=Character->Outfits[0].Parts;Parts.RemoveAll([](const auto& Part){return Eligible(Part);});FKKImportedPart Part;Part.Name=TEXT("PackedVisibleParts");Part.Role=TEXT("CrowdPacked");Part.SkinnedModel=Mesh;Part.Transform=FTransform::Identity;Parts.Insert(Part,0);
    auto* Library=FreshPackedAsset<UKKImportedAnimationLibrary>(Destination/TEXT("Walk/Animations"));Library->Character=Character;Library->Animations=Walk->Animations;
    if(!Audit(Original.Get(),Walk.Get(),Character,Library,Sources,Mesh,Error)){UE_LOG(LogTemp,Error,TEXT("Crowd pack index=%d %s"),Index,*Error);return 1;}
    if(!SavePackedAsset(Mesh)||!SavePackedAsset(Library)||!SavePackedAsset(Character))return 1;
   }
  }CollectGarbage(RF_NoFlags);UE_LOG(LogTemp,Display,TEXT("Crowd packed complete index=%d"),Index);
 }return 0;
#else
 return 1;
#endif
}
