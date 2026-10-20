#include "OKCrowdScene.h"
#ifndef OK_CROWD_RUNTIME_APNG
#define OK_CROWD_RUNTIME_APNG 0
#endif
#if OK_CROWD_RUNTIME_APNG
#include "OKCrowdRuntimePoseProbe.h"
#endif
#include "ShaderCodeLibrary.h"
#include "OKCrowdBonePacking.h"
#include "OKCrowdIndependentGpu.h"
#include "OKCrowdLayout.h"
#include "OKCrowdGpuPose.h"
#include "Components/InstancedSkinnedMeshComponent.h"
#include "Animation/AnimSequenceTransformProviderData.h"
#include "Animation/AnimBank.h"
#include "KKImportedCharacter.h"
#include "KKSharedAnimation.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "Engine/World.h"
#include "Engine/LocalPlayer.h"
#include "Engine/GameViewportClient.h"
#include "SceneView.h"
#include "ConvexVolume.h"
#include "Engine/Engine.h"
#include "GameFramework/SpectatorPawn.h"
#include "GameFramework/PlayerController.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Materials/MaterialInterface.h"
#include "Misc/Paths.h"
#include "Misc/Parse.h"
#include "Misc/CommandLine.h"
#include "Misc/App.h"
#include "Misc/SecureHash.h"
#include "Misc/FileHelper.h"
#include "HAL/PlatformMemory.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "CoreGlobals.h"
#include "Async/TaskGraphInterfaces.h"
#include "Json.h"
#include "UnrealClient.h"
#include "EngineGlobals.h"
#include "TimerManager.h"
#include "RHI.h"
#include "RenderCore.h"
#include "RHIStats.h"
#include "RenderTimer.h"
#include "DynamicRHI.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Animation/AnimCompressionTypes.h"
#include "Animation/AnimBoneCompressionCodec.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Texture2D.h"
#include "UObject/UObjectIterator.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Rendering/SkinWeightVertexBuffer.h"
#include "SkeletalMeshMerge.h"
#include "UObject/StrongObjectPtr.h"
#include "ProfilingDebugging/TraceAuxiliary.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#if WITH_EDITOR
#include "ShaderCompiler.h"
#endif

#if !WITH_EDITOR && OK_CROWD_COMPONENT_MATRIX_CACHE
// Optional local Engine cpp-only optimization; no authoring Engine ABI change.
ENGINE_API void GetSkeletalComponentMatrixCacheStats(uint64& Hits,uint64& Misses,uint64& Mismatches,uint64& Verified,bool& Failed);
#endif
#if !WITH_EDITOR && OK_CROWD_FULL_REF_MATRIX_CACHE
ENGINE_API void GetSkeletalFullRefMatrixCacheStats(uint64& Hits,uint64& Misses,uint64& Verified,uint64& Mismatches,bool& Failed);
#endif
#if !WITH_EDITOR && OK_CROWD_WEIGHTED_POSE_SEED
ENGINE_API bool SetSkeletalWeightedPoseSeed(USkeletalMeshComponent* Component,const TArray<FBoneIndexType>* Bones);
#endif
#if !WITH_EDITOR && OK_CROWD_CONSTANT_SCALE_CACHE
ENGINE_API bool PrepareAnimConstantScaleCache(UAnimSequence* Clip);
ENGINE_API void GetAnimConstantScaleCacheStats(uint64& Hits,uint64& Misses,uint64& Verified,uint64& Mismatches,bool& Failed);
#endif

namespace {
TAutoConsoleVariable<int32> IndependentGpuAnimationSwitch(
 TEXT("r.OKCrowd.IndependentGpuAnimation"),0,
 TEXT("Request independent GPU animation in CrowdRoom: 0 CPU, 1 GPU request. Unsupported/unregistered components retain CPU. F8 toggles; F9 shows execution status. Distinct from legacy CrowdGpuPose and GPU matrix packing."),ECVF_Default);
const FName LeaderDrivenFollowerTag(TEXT("LeaderDrivenCrowdFollower"));
int32 WeightedSeedApplied=0,WeightedSeedVerified=0,WeightedSeedFailed=0;int64 WeightedSeedRemoved=0;
bool WeightedSeedEnabled=true;
int32 ConstantScalePrepared=0,ConstantScaleUnsupported=0;
bool WantsConstantScaleCache(){
 if(FParse::Param(FCommandLine::Get(),TEXT("CrowdDisableConstantScaleCache")))return false;
 return (!WITH_EDITOR&&OK_CROWD_CONSTANT_SCALE_CACHE)||FParse::Param(FCommandLine::Get(),TEXT("CrowdConstantScaleCache"));
}
void PrepareConstantScales(AKKImportedCharacterActor* Actor){
 if(!WantsConstantScaleCache())return;
#if !WITH_EDITOR && OK_CROWD_CONSTANT_SCALE_CACHE
 TArray<USkeletalMeshComponent*> Skins;Actor->GetComponents(Skins);for(auto* Skin:Skins)if(!Skin->LeaderPoseComponent.IsValid()&&Skin->GetSingleNodeInstance()){
  auto* Clip=Cast<UAnimSequence>(Skin->GetSingleNodeInstance()->GetCurrentAsset());if(Clip&&PrepareAnimConstantScaleCache(Clip))++ConstantScalePrepared;else ++ConstantScaleUnsupported;
 }
#else
 ++ConstantScaleUnsupported;
#endif
}
void ApplyWeightedPoseSeed(AKKImportedCharacterActor* Actor){
 FString Manifest;if(!FParse::Value(FCommandLine::Get(),TEXT("CrowdWeightedPoseSeed="),Manifest))return;
#if !WITH_EDITOR && OK_CROWD_WEIGHTED_POSE_SEED
 static TSharedPtr<FJsonObject> Metadata;static bool Loaded=false;
 if(!Loaded){Loaded=true;FString Text;if(FFileHelper::LoadFileToString(Text,*Manifest))FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Metadata);}
 auto Fail=[](){++WeightedSeedFailed;UE_LOG(LogTemp,Error,TEXT("Weighted pose seed failed validation; stopping optional experiment"));FPlatformMisc::RequestExitWithStatus(false,1);};
 const TArray<TSharedPtr<FJsonValue>>* Cards=nullptr;if(!Metadata||!Metadata->TryGetArrayField(TEXT("cards"),Cards)||Metadata->GetNumberField(TEXT("failures"))!=0){Fail();return;}
 TSharedPtr<FJsonObject> Card;for(const auto& Value:*Cards){auto Candidate=Value->AsObject();if(Candidate&&Actor->Character&&Candidate->GetStringField(TEXT("low"))==Actor->Character->GetPathName()&&Candidate->GetStringField(TEXT("source_hash"))==Actor->Character->SourceHash){Card=Candidate;break;}}
 if(!Card){Fail();return;}TArray<USkeletalMeshComponent*> Skins;Actor->GetComponents(Skins);
 for(auto* Skin:Skins){auto* Mesh=Skin->GetSkeletalMeshAsset();if(!Mesh||Skin->LeaderPoseComponent.IsValid())continue;
  if(Mesh->GetPathName()!=Card->GetStringField(TEXT("leader_mesh"))||!Skin->GetSingleNodeInstance()||Skin->GetMeshDeformerInstanceForLOD(0)){Fail();return;}
  const auto& Ref=Mesh->GetRefSkeleton();const auto& Names=Card->GetArrayField(TEXT("leader_names"));const auto& Parents=Card->GetArrayField(TEXT("leader_parents"));if(Names.Num()!=Ref.GetNum()||Parents.Num()!=Ref.GetNum()){Fail();return;}
  for(int32 B=0;B<Ref.GetNum();++B)if(Names[B]->AsString()!=Ref.GetBoneName(B).ToString()||Parents[B]->AsNumber()!=Ref.GetParentIndex(B)){Fail();return;}
  TArray<FBoneIndexType> Needed;for(const auto& Value:Card->GetArrayField(TEXT("required_indices"))){const double B=Value->AsNumber();if(B<0||B>=Ref.GetNum()||B!=FMath::FloorToDouble(B)){Fail();return;}Needed.Add(FBoneIndexType(B));}
  const int32 Before=Skin->RequiredBones.Num();const float OriginalTime=Skin->GetPosition();
  if(FParse::Param(FCommandLine::Get(),TEXT("CrowdVerifyWeightedPoseSeed"))){
   const float Length=Skin->GetSingleNodeInstance()->GetLength();
   for(float Phase:{0.f,.37f,.99f}){
    if(!SetSkeletalWeightedPoseSeed(Skin,nullptr)){Fail();return;}Skin->SetPosition(Length*Phase,false);Skin->RefreshBoneTransforms();TArray<FMatrix44f> Expected;for(const auto& Pose:Skin->GetComponentSpaceTransforms())Expected.Add(FMatrix44f(Pose.ToMatrixWithScale()));
    if(!SetSkeletalWeightedPoseSeed(Skin,&Needed)){Fail();return;}Skin->RefreshBoneTransforms();
    for(FBoneIndexType B:Skin->RequiredBones){const FMatrix44f Actual(Skin->GetComponentSpaceTransforms()[B].ToMatrixWithScale());if(!Expected.IsValidIndex(B)||FMemory::Memcmp(&Actual,&Expected[B],sizeof(Actual))!=0){SetSkeletalWeightedPoseSeed(Skin,nullptr);Fail();return;}}
   }++WeightedSeedVerified;Skin->SetPosition(OriginalTime,false);
  }else if(!SetSkeletalWeightedPoseSeed(Skin,&Needed)){Fail();return;}
  Skin->RefreshBoneTransforms();WeightedSeedRemoved+=Before-Skin->RequiredBones.Num();++WeightedSeedApplied;
 }
#else
 ++WeightedSeedFailed;UE_LOG(LogTemp,Error,TEXT("Weighted pose seed requires the optional Game Engine patch"));FPlatformMisc::RequestExitWithStatus(false,1);
#endif
}
// Read-only audit. A render closure is a lower bound, not permission to remove
// physics, virtual, socket, IK or deformer bones from the animation container.
TSharedRef<FJsonObject> AuditCrowdPoseBones(const TArray<TObjectPtr<AKKImportedCharacterActor>>& Actors){
 auto Audit=MakeShared<FJsonObject>();TArray<TSharedPtr<FJsonValue>> Rows;
 int64 RequiredTotal=0,RenderTotal=0,FillTotal=0,MissingTotal=0,UnmappedTotal=0,WeightedTotal=0,UnavailableTotal=0;
 for(const auto& Actor:Actors){TArray<USkeletalMeshComponent*> Skins;Actor->GetComponents(Skins);
  for(const auto* Leader:Skins){auto* Mesh=Leader->GetSkeletalMeshAsset();if(!Mesh||Leader->LeaderPoseComponent.IsValid())continue;
   const auto& Ref=Mesh->GetRefSkeleton();TSet<int32> RenderBones,WeightedBones;int32 Unmapped=0,Unavailable=0;
   auto Add=[&](int32 Bone){if(Bone<0||Bone>=Ref.GetNum()){++Unmapped;return;}while(Bone>=0&&!RenderBones.Contains(Bone)){RenderBones.Add(Bone);Bone=Ref.GetParentIndex(Bone);}};
   auto AddWeighted=[&](int32 Bone){if(Bone<0||Bone>=Ref.GetNum())return;while(Bone>=0&&!WeightedBones.Contains(Bone)){WeightedBones.Add(Bone);Bone=Ref.GetParentIndex(Bone);}};
   auto AddSkin=[&](const USkinnedMeshComponent* Skin){auto* Part=Cast<USkeletalMesh>(Skin->GetSkinnedAsset());auto* Data=Part?Part->GetResourceForRendering():nullptr;if(!Data||Data->LODRenderData.IsEmpty())return;
    const auto& LOD=Data->LODRenderData[FMath::Clamp(Skin->GetPredictedLODLevel(),0,Data->LODRenderData.Num()-1)];const auto& Map=Skin->GetLeaderBoneMap();
    // Include every palette entry and every follower, even invisible parts:
    // changing near-detail, sections or visibility must never undercount.
    for(const auto& Section:LOD.RenderSections)for(FBoneIndexType Bone:Section.BoneMap){if(Skin==Leader)Add(Bone);else if(Map.IsValidIndex(Bone)&&Map[Bone]>=0)Add(Map[Bone]);else ++Unmapped;}
    // Diagnostic only: Cook may discard CPU weights. Never infer zero use from
    // unavailable data, and never enable CPU resource retention for this probe.
    const auto* Weights=LOD.GetSkinWeightVertexBuffer();const bool Readable=Weights&&Weights->GetDataVertexBuffer()->GetWeightData()!=nullptr&&!Weights->GetVariableBonesPerVertex();
    if(!Readable){++Unavailable;for(const auto& Section:LOD.RenderSections)for(FBoneIndexType Bone:Section.BoneMap){if(Skin==Leader)AddWeighted(Bone);else if(Map.IsValidIndex(Bone)&&Map[Bone]>=0)AddWeighted(Map[Bone]);}return;}
    for(const auto& Section:LOD.RenderSections){const uint32 End=Section.BaseVertexIndex+Section.NumVertices;if(End>Weights->GetNumVertices()){++Unavailable;continue;}
     for(uint32 Vertex=Section.BaseVertexIndex;Vertex<End;++Vertex)for(uint32 Influence=0;Influence<Weights->GetMaxBoneInfluences();++Influence)if(Weights->GetBoneWeight(Vertex,Influence)>0){const uint32 Local=Weights->GetBoneIndex(Vertex,Influence);if(!Section.BoneMap.IsValidIndex(Local)){++Unmapped;continue;}const int32 Bone=Section.BoneMap[Local];if(Skin==Leader)AddWeighted(Bone);else if(Map.IsValidIndex(Bone)&&Map[Bone]>=0)AddWeighted(Map[Bone]);}
    }
   };
   AddSkin(Leader);for(const auto& Follower:Leader->GetFollowerPoseComponents())if(Follower.IsValid())AddSkin(Follower.Get());
   TSet<int32> Required;for(FBoneIndexType Bone:Leader->RequiredBones)Required.Add(Bone);
   int32 Missing=0;for(int32 Bone:RenderBones)if(!Required.Contains(Bone))++Missing;
   auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("actor"),Actor->GetName());Row->SetStringField(TEXT("mesh"),Mesh->GetPathName());
   Row->SetNumberField(TEXT("skeleton_bones"),Ref.GetNum());Row->SetNumberField(TEXT("required_bones"),Required.Num());Row->SetNumberField(TEXT("fill_component_bones"),Leader->FillComponentSpaceTransformsRequiredBones.Num());Row->SetNumberField(TEXT("render_parent_closure"),RenderBones.Num());Row->SetNumberField(TEXT("render_bones_not_required"),Missing);Row->SetNumberField(TEXT("unmapped_palette_entries"),Unmapped);
   Row->SetNumberField(TEXT("weighted_parent_closure"),WeightedBones.Num());Row->SetNumberField(TEXT("unavailable_weight_parts"),Unavailable);WeightedTotal+=WeightedBones.Num();UnavailableTotal+=Unavailable;
   Rows.Add(MakeShared<FJsonValueObject>(Row));RequiredTotal+=Required.Num();FillTotal+=Leader->FillComponentSpaceTransformsRequiredBones.Num();RenderTotal+=RenderBones.Num();MissingTotal+=Missing;UnmappedTotal+=Unmapped;
  }
 }
 Audit->SetNumberField(TEXT("required_bones"),double(RequiredTotal));Audit->SetNumberField(TEXT("fill_component_bones"),double(FillTotal));Audit->SetNumberField(TEXT("render_parent_closure"),double(RenderTotal));Audit->SetNumberField(TEXT("render_bones_not_required"),double(MissingTotal));Audit->SetNumberField(TEXT("unmapped_palette_entries"),double(UnmappedTotal));Audit->SetArrayField(TEXT("leaders"),Rows);
 Audit->SetNumberField(TEXT("weighted_parent_closure"),double(WeightedTotal));Audit->SetNumberField(TEXT("unavailable_weight_parts"),double(UnavailableTotal));
 Audit->SetBoolField(TEXT("weighted_closure_complete"),UnavailableTotal==0&&UnmappedTotal==0);
 Audit->SetBoolField(TEXT("fps_acceptance_eligible"),false);
 Audit->SetStringField(TEXT("limit"),TEXT("Read-only render lower bound; excess required bones may serve physics, sockets, virtual bones or animation. Not a safe pruning count or FPS acceptance."));return Audit;
}
void ConfigureLowFollowers(AKKImportedCharacterActor* Actor,bool IsLow){
 if(!IsLow||!FParse::Param(FCommandLine::Get(),TEXT("CrowdLeaderDrivenFollowers")))return;
 TArray<USkeletalMeshComponent*> Skins;Actor->GetComponents(Skins);
 for(auto* Skin:Skins){
  auto* Leader=Cast<USkeletalMeshComponent>(Skin->LeaderPoseComponent.Get());auto* Mesh=Skin->GetSkeletalMeshAsset();
  // FinalizeAnimationUpdate refreshes follower render data every leader frame.
  // Keep independent updates for morphs, physics and meshes needing LOD changes.
  if(!Leader||!Mesh||Mesh->GetLODNum()!=1||!Mesh->GetMorphTargets().IsEmpty()||Skin->IsSimulatingPhysics())continue;
  // The fast path owns no per-component morph/deformer/cloth work. Imported
  // secondary motion remains baked in each leader's private bone mapping.
  if(Skin->GetMeshDeformerInstanceForLOD(0)||Skin->GetExternalMorphSetsArray().ContainsByPredicate([](const auto& Sets){return !Sets.IsEmpty();}))continue;
  TArray<UClothingAssetBase*> Cloth;Mesh->GetClothingAssetsInUse(Cloth);if(!Cloth.IsEmpty())continue;
  const auto* Data=Mesh->GetResourceForRendering();auto* LeaderMesh=Leader->GetSkeletalMeshAsset();if(!Data||Data->LODRenderData.IsEmpty()||!LeaderMesh)continue;
  const auto& Map=Skin->GetLeaderBoneMap();bool Valid=true;
  for(const auto& Section:Data->LODRenderData[0].RenderSections)for(int32 Bone:Section.BoneMap){
   if(!Map.IsValidIndex(Bone)||!LeaderMesh->GetRefSkeleton().IsValidIndex(Map[Bone])||Mesh->GetRefSkeleton().GetBoneName(Bone)!=LeaderMesh->GetRefSkeleton().GetBoneName(Map[Bone]))Valid=false;
  }
  if(!Valid){UE_LOG(LogTemp,Warning,TEXT("Crowd follower optimization skipped invalid mapping %s"),*Skin->GetName());continue;}
  Skin->SetForcedLOD(1);Skin->UpdateLODStatus();Skin->ComponentTags.AddUnique(LeaderDrivenFollowerTag);Skin->SetComponentTickEnabled(false);
 }
}
}

AOKCrowdGameMode::AOKCrowdGameMode(){DefaultPawnClass=ASpectatorPawn::StaticClass();PlayerControllerClass=AOKCrowdController::StaticClass();HUDClass=nullptr;}
void UOKCrowdRenderSkin::RefreshBoneTransforms(FActorComponentTickFunction* TickFunction){
 if(GpuAnimationClockOwned)return;
 // TickAnimation keeps advancing every frame. Forced initialization and phase
 // changes bypass the cap; at <=60 FPS each frame enters a new time bucket.
 if(PoseRateHz==60&&TickFunction&&GetWorld()&&bRequiredBonesUpToDate){
  const int64 Bucket=FMath::FloorToInt64(double(GetWorld()->GetTimeSeconds())*60.0+PosePhaseCycles);
  if(Bucket==LastPoseBucket){++PoseSkips;return;}LastPoseBucket=Bucket;
 }
 ++PoseEvaluations;
 Super::RefreshBoneTransforms(TickFunction);
}
void UOKCrowdRenderSkin::FinalizeBoneTransform(){
 const auto OldClip=FinalizedPoseClip;const float OldTime=FinalizedPoseTime;const uint32 OldRevision=FinalizedBoneRevision;
 Super::FinalizeBoneTransform();
 auto* Instance=GetSingleNodeInstance();FinalizedPoseClip=Instance?Cast<UAnimSequence>(Instance->GetCurrentAsset()):nullptr;
 FinalizedPoseTime=Instance?Instance->GetCurrentTime():0;FinalizedPoseEvaluation=PoseEvaluations;
 FinalizedBoneRevision=GetBoneTransformRevisionNumber();
 if(OldRevision!=FinalizedBoneRevision){PreviousFinalizedPoseClip=OldClip.IsValid()?OldClip:FinalizedPoseClip;PreviousFinalizedPoseTime=OldClip.IsValid()?OldTime:FinalizedPoseTime;}
 OnCrowdPoseFinalized.Broadcast(this);
}
void UOKCrowdRenderSkin::SendRenderDynamicData_Concurrent(){
 if(ForceDiagnosticRenderUpload||CrowdRenderNeeded.Load()||CastShadow||bCastHiddenShadow||GIsEditor){Super::SendRenderDynamicData_Concurrent();return;}
 TRACE_CPUPROFILER_EVENT_SCOPE(CrowdOffscreenRenderUpload);
 // End-of-frame upload happens after this component's animation completion.
 // Restore before returning: TickAnimation/RefreshBoneTransforms continue to
 // see AlwaysTickPoseAndRefreshBones. Super retains its recently-rendered,
 // initial-upload and force-update exceptions, including buffer bookkeeping.
 TGuardValue<EVisibilityBasedAnimTickOption> Guard(VisibilityBasedAnimTickOption,EVisibilityBasedAnimTickOption::AlwaysTickPose);
 Super::SendRenderDynamicData_Concurrent();
}
FBoxSphereBounds UOKCrowdRenderSkin::CalcBounds(const FTransform& LocalToWorld) const {
 const FBoxSphereBounds Original=Super::CalcBounds(LocalToWorld);
 if(GpuAnimationBoundsRadius>0)if(const auto* Actor=GetOwner()){
  const FVector Extent(GpuAnimationBoundsRadius);
  const FBoxSphereBounds Gpu=Original+FBoxSphereBounds(FBox(-Extent,Extent)).TransformBy(Actor->GetActorTransform());
  if(ProtectFacialBounds)return Gpu+FBoxSphereBounds(FBox(FVector(-80,-80,0),FVector(80,80,300))).TransformBy(Actor->GetActorTransform());
  return Gpu;
 }
 // Affine palette bones encode matrix columns rather than anatomical joints.
 // Their calculated bounds can exclude the animated head at close range.
 // Include a conservative whole-character box for these game components.
 if(const auto* Actor=ProtectFacialBounds?GetOwner():nullptr){
  const FTransform& Transform=Actor->GetActorTransform();
  const FBoxSphereBounds Crowd(FBox(FVector(-80,-80,0),FVector(80,80,300)));
  return Original+Crowd.TransformBy(Transform);
 }
 return Original;
}
#include "OKCrowdGroupedPrimitives.inl"
void AOKCrowdRenderActor::OnConstruction(const FTransform& Transform){
 if(!Character||!PlaySkeletalAnimation||!ShareBakedPose||SharedAnimation){Super::OnConstruction(Transform);return;}
 AActor::OnConstruction(Transform);AnimationError.Reset();
 TArray<UMeshComponent*> Old;GetComponents(Old);for(auto* Skin:Old)if(Skin->ComponentHasTag(TEXT("ImportedCard")))Skin->DestroyComponent();
 const auto* Coordinate=Character->Outfits.FindByPredicate([this](const auto& Entry){return Entry.Index==Outfit;});if(!Coordinate)return;
 GroupedMeshes.Reset();GroupedSourceParts=GroupedResultParts=GroupedAcceptedGroups=GroupedRejectedGroups=0;GroupedAuditedVertices=0;GroupedBuildSeconds=0;
 const FKKImportedOutfit Grouped=FParse::Param(FCommandLine::Get(),TEXT("CrowdGroupPrimitives"))?CrowdGroupOutfit(this,*Coordinate):*Coordinate;
 for(const auto& Part:Grouped.Parts){
  if(Part.SkinnedModel){auto* Skin=NewObject<UOKCrowdRenderSkin>(this);Skin->ProtectFacialBounds=!Part.MouthPatterns.IsEmpty()||Part.Role==TEXT("head")||Part.Role.StartsWith(TEXT("hair"));const FString Layer=Part.NativeRenderer.ToLower();if(Layer.Contains(TEXT("hitomi")))Skin->TranslucencySortPriority=Layer.Contains(TEXT("02"))?102:101;else if(Layer.Contains(TEXT("eyeline")))Skin->TranslucencySortPriority=Layer.Contains(TEXT("low"))?105:(Layer.EndsWith(TEXT("_kage"))?103:104);else if(Layer.Contains(TEXT("mayuge")))Skin->TranslucencySortPriority=106;else if(Layer.Contains(TEXT("noseline")))Skin->TranslucencySortPriority=107;FParse::Value(FCommandLine::Get(),TEXT("CrowdAnimationHz="),Skin->PoseRateHz);if(FParse::Param(FCommandLine::Get(),TEXT("CrowdStaggerPose")))Skin->PosePhaseCycles=double(GetTypeHash(Character->SourceHash)%1024)/1024.;Skin->ComponentTags.Add(TEXT("ImportedCard"));Skin->SetupAttachment(RootComponent);Skin->SetSkeletalMeshAsset(Part.SkinnedModel);Skin->SetRelativeTransform(Part.Transform);Skin->SetCollisionEnabled(ECollisionEnabled::NoCollision);Skin->SetVisibility(Part.MouthDefaultVisible);Skin->SetAnimationMode(EAnimationMode::AnimationSingleNode);Skin->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;Skin->CreationMethod=EComponentCreationMethod::UserConstructionScript;Skin->RegisterComponent();}
  else if(Part.Model){auto* Skin=NewObject<UStaticMeshComponent>(this);Skin->ComponentTags.Add(TEXT("ImportedCard"));Skin->SetupAttachment(RootComponent);Skin->SetStaticMesh(Part.Model);Skin->SetRelativeTransform(Part.Transform);Skin->SetCollisionEnabled(ECollisionEnabled::NoCollision);Skin->SetVisibility(Part.MouthDefaultVisible);Skin->CreationMethod=EComponentCreationMethod::UserConstructionScript;Skin->RegisterComponent();}
 }
 // This also applies the original per-card mouth baseline and validates every
 // follower's private bone map through the imported actor implementation.
 if(!PlayImportedAnimation(AnimationIndex))AnimationError=TEXT("Crowd upload actor could not initialize original animation");
}
void AOKCrowdController::BeginPlay(){Super::BeginPlay();SetInputMode(FInputModeGameOnly());bShowMouseCursor=false;FString RuntimeManifest;RuntimeApngScene=FParse::Value(FCommandLine::Get(),TEXT("CrowdRuntimeScene="),RuntimeManifest)||FParse::Value(FCommandLine::Get(),TEXT("StreetSocialManifest="),RuntimeManifest);}
void AOKCrowdController::PlayerTick(float DeltaTime){
 Super::PlayerTick(DeltaTime);if(IsPaintPreviewActive()||RuntimeApngScene||FParse::Param(FCommandLine::Get(),TEXT("CrowdThirdPerson")))return;auto* Camera=Cast<ACameraActor>(GetViewTarget());if(!Camera)return;
 FRotator Rotation=Camera->GetActorRotation();
 if(IsInputKeyDown(EKeys::RightMouseButton)){float X,Y;GetInputMouseDelta(X,Y);Rotation.Yaw+=X*.15f;Rotation.Pitch=FMath::Clamp(Rotation.Pitch-Y*.15f,-85.f,85.f);Camera->SetActorRotation(Rotation);}
 FVector Move=FVector::ZeroVector;
 if(IsInputKeyDown(EKeys::W))Move+=Rotation.Vector();if(IsInputKeyDown(EKeys::S))Move-=Rotation.Vector();
 if(IsInputKeyDown(EKeys::D))Move+=FRotationMatrix(Rotation).GetUnitAxis(EAxis::Y);if(IsInputKeyDown(EKeys::A))Move-=FRotationMatrix(Rotation).GetUnitAxis(EAxis::Y);
 if(IsInputKeyDown(EKeys::E))Move.Z+=1;if(IsInputKeyDown(EKeys::Q))Move.Z-=1;
 Camera->AddActorWorldOffset(Move.GetClampedToMaxSize(1)*DeltaTime*(IsInputKeyDown(EKeys::LeftShift)?1800:600));
}
AOKCrowdScene::AOKCrowdScene(){PrimaryActorTick.bCanEverTick=true;PrimaryActorTick.TickGroup=TG_PostUpdateWork;}
void AOKCrowdScene::SetIndependentGpuAnimationEnabled(bool Enabled){
 check(IsInGameThread());
 IndependentGpuAnimationSwitch->Set(Enabled?1:0,ECVF_SetByConsole);
 PollIndependentGpuAnimationControl();
}
FString AOKCrowdScene::GetIndependentGpuAnimationStatus() const {
 const auto Stats=OKGetCrowdIndependentGpuStats(IndependentGpuRuntime);
 return FString::Printf(TEXT("Independent GPU animation: request %s | GPU clock actors: %llu | rendered GPU components: %llu | %s"),
  IndependentGpuAnimationRequested?TEXT("ON"):TEXT("OFF"),
  Stats.ClockOwnedActors,IndependentGpuAnimationRequested?Stats.Components:0,*Stats.Status);
}
void AOKCrowdScene::ShowIndependentGpuAnimationStatus(){
 const FString Status=GetIndependentGpuAnimationStatus();
 UE_LOG(LogTemp,Display,TEXT("%s"),*Status);
 if(GEngine)GEngine->AddOnScreenDebugMessage(uint64(GetUniqueID()),8.f,IndependentGpuAnimationRequested?FColor::Yellow:FColor::Cyan,Status);
}
void AOKCrowdScene::PollIndependentGpuAnimationControl(){
 check(IsInGameThread());
 const int32 Value=IndependentGpuAnimationSwitch.GetValueOnGameThread();
 const bool Requested=Value==1;
 if(Value!=0&&Value!=1){
  UE_LOG(LogTemp,Warning,TEXT("Independent GPU animation accepts only 0 or 1; invalid request %d reset to CPU"),Value);
  IndependentGpuAnimationSwitch->Set(0,ECVF_SetByConsole);
 }
 if(!IndependentGpuAnimationControlInitialized||Requested!=IndependentGpuAnimationRequested){
  if(IndependentGpuAnimationControlInitialized){++IndependentGpuAnimationRequestChanges;if(SamplesSeen>0)IndependentGpuAnimationChangedDuringSamples=true;}
  IndependentGpuAnimationControlInitialized=true;IndependentGpuAnimationRequested=Requested;
  ShowIndependentGpuAnimationStatus();
 }
}
bool AOKCrowdScene::LoadSectionLOD(){
 FString Path;if(!FParse::Value(FCommandLine::Get(),TEXT("CrowdSectionLODFile="),Path))return true;
 SectionLODPixels=2;FParse::Value(FCommandLine::Get(),TEXT("CrowdSectionLODPixels="),SectionLODPixels);
 FString Text;TSharedPtr<FJsonObject> Root;const TArray<TSharedPtr<FJsonValue>>* Cards=nullptr;double Schema=0,Recipe=0;
 if((AssetVariant!=TEXT("GameLow30kR1")&&AssetVariant!=TEXT("GameLowBodySafeR6")&&AssetVariant!=TEXT("GameLowBindingSafeR7")&&AssetVariant!=TEXT("GameLowSurfaceSafeR8")&&AssetVariant!=TEXT("GameLowHairSafeR9"))||!FMath::IsFinite(SectionLODPixels)||SectionLODPixels<=0||SectionLODPixels>8||!FFileHelper::LoadFileToString(Text,*Path)||!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Root)||!Root||!Root->TryGetNumberField(TEXT("schema"),Schema)||Schema!=1||!Root->TryGetNumberField(TEXT("recipe"),Recipe)||Recipe!=1005||!Root->TryGetArrayField(TEXT("cards"),Cards))return false;
 for(const auto& CardValue:*Cards){const auto Card=CardValue->AsObject();FString Hash;const TArray<TSharedPtr<FJsonValue>>* Meshes=nullptr;if(!Card||!Card->TryGetStringField(TEXT("source_hash"),Hash)||!Card->TryGetArrayField(TEXT("meshes"),Meshes))return false;
  for(const auto& Value:*Meshes){const auto Mesh=Value->AsObject();bool Eligible=false;FString Name;double Materials,Sections,Radius;const TArray<TSharedPtr<FJsonValue>>* Slots=nullptr;
   if(!Mesh||!Mesh->TryGetBoolField(TEXT("eligible"),Eligible)||!Mesh->TryGetStringField(TEXT("mesh"),Name)||!Mesh->TryGetNumberField(TEXT("materials"),Materials)||!Mesh->TryGetNumberField(TEXT("sections"),Sections)||!Mesh->TryGetNumberField(TEXT("actor_radius_cm"),Radius)||!Mesh->TryGetArrayField(TEXT("slots"),Slots)||!Name.StartsWith(TEXT("/Game/"))||!FMath::IsFinite(Materials)||Materials<1||Materials>4096||Materials!=FMath::FloorToDouble(Materials)||!FMath::IsFinite(Sections)||Sections<0||Sections>8192||!FMath::IsFinite(Radius)||Radius<=0||Radius>10000||Slots->Num()!=int32(Materials))return false;
   FSectionLODMesh Entry;Entry.SourceHash=Hash;Entry.Materials=int32(Materials);Entry.Sections=int32(Sections);Entry.ActorRadius=Radius;Entry.Radii.Init(-1,Entry.Materials);
   for(const auto& SlotValue:*Slots){const auto Slot=SlotValue->AsObject();double Index,R;if(!Slot||!Slot->TryGetNumberField(TEXT("slot"),Index)||!Slot->TryGetNumberField(TEXT("radius_cm"),R)||!FMath::IsFinite(Index)||Index!=FMath::FloorToDouble(Index)||Index<0||Index>=Entry.Materials||!FMath::IsFinite(R)||R<0||R>Radius||Entry.Radii[int32(Index)]>=0)return false;Entry.Radii[int32(Index)]=R;}
   if(Eligible){if(SectionLODMeshes.Contains(Name))return false;SectionLODMeshes.Add(Name,MoveTemp(Entry));}
  }
 }
 if(FParse::Param(FCommandLine::Get(),TEXT("CrowdBoundsOnly")))SectionLODPixels=0;
 return !SectionLODMeshes.IsEmpty();
}
void AOKCrowdScene::BindSectionLOD(AKKImportedCharacterActor* Actor,bool IsLow){
 if(!IsLow||SectionLODPixels<=0||!Actor||!Actor->Character||Actor->Character->Schema!=1005)return;
 SectionLODBindings.RemoveAll([](const auto& Entry){return !Entry.Actor.IsValid()||!Entry.Skin.IsValid();});
 TArray<USkeletalMeshComponent*> Skins;Actor->GetComponents(Skins);
 for(auto* Skin:Skins){auto* Mesh=Skin->GetSkeletalMeshAsset();if(!Mesh||!Skin->IsVisible()||!Skin->bRenderInMainPass||!Mesh->GetMorphTargets().IsEmpty())continue;
  const auto* Entry=SectionLODMeshes.Find(Mesh->GetPathName());const auto* Data=Mesh->GetResourceForRendering();if(!Entry||Entry->SourceHash!=Actor->Character->SourceHash||Mesh->GetMaterials().Num()!=Entry->Materials||!Data||Data->LODRenderData.Num()!=1||!Mesh->GetLODInfo(0)->LODMaterialMap.IsEmpty()||Data->LODRenderData[0].RenderSections.Num()!=Entry->Sections)continue;
  FSectionLODBinding Binding;Binding.Actor=Actor;Binding.Skin=Skin;Binding.Mesh=Mesh;Binding.ActorRadius=Entry->ActorRadius;const auto& Sections=Data->LODRenderData[0].RenderSections;
  for(int32 M=0;M<Entry->Materials;++M){auto* Material=Skin->GetMaterial(M);if(!Material||Material->IsUsingWorldPositionOffset_Concurrent(GShaderPlatformForFeatureLevel[GetWorld()->GetFeatureLevel()]))continue;
   FSectionLODSlot Slot;Slot.Material=M;Slot.Radius=Entry->Radii[M];for(int32 S=0;S<Sections.Num();++S)if(Sections[S].MaterialIndex==M){if(Slot.Count==0)Slot.Section=S;++Slot.Count;}if(Slot.Count>0)Binding.Slots.Add(Slot);
  }
  if(!Binding.Slots.IsEmpty())SectionLODBindings.Add(MoveTemp(Binding));
 }
}
void AOKCrowdScene::UpdateSectionLOD(APlayerController* PC){
 if(SectionLODPixels<=0||SectionLODMeshes.IsEmpty())return;
 TRACE_CPUPROFILER_EVENT_SCOPE(CrowdSectionLOD);
 auto* LocalPlayer=PC->GetLocalPlayer();auto* ViewportClient=GetWorld()->GetGameViewport();FSceneViewProjectionData ViewProjection;
 if(!LocalPlayer||!ViewportClient||!ViewportClient->Viewport||!LocalPlayer->GetProjectionData(ViewportClient->Viewport,ViewProjection)||!ViewProjection.IsPerspectiveProjection())return;
 FVector Camera;FRotator Rotation;PC->GetPlayerViewPoint(Camera,Rotation);auto* View=Cast<ACameraActor>(PC->GetViewTarget());if(!View)return;
 // Read the actual UE projection, including aspect constraints and letterbox.
 // A horizontal-FOV assumption can underestimate size in a resized window.
 const auto& Rect=ViewProjection.GetConstrainedViewRect();const double Scale=FMath::Max(FMath::Abs(ViewProjection.ProjectionMatrix.M[0][0])*Rect.Width(),FMath::Abs(ViewProjection.ProjectionMatrix.M[1][1])*Rect.Height())*.5;
 const FRotationMatrix Basis(Rotation);const FVector Forward=Basis.GetUnitAxis(EAxis::X),Right=Basis.GetUnitAxis(EAxis::Y),Up=Basis.GetUnitAxis(EAxis::Z);
 SectionLODHidden=0;SectionLODMatched=0;
 for(const auto& Binding:SectionLODBindings){auto* Actor=Binding.Actor.Get();auto* Skin=Binding.Skin.Get();if(!Actor||!Skin||!Skin->IsVisible()||Skin->GetSkeletalMeshAsset()!=Binding.Mesh.Get())continue;++SectionLODMatched;
   const FVector Delta=Actor->GetActorLocation()-Camera;const bool Far=Delta.SizeSquared()>FMath::Square(1600.);const double ActorScale=Actor->GetActorScale3D().GetAbsMax(),Radius=Binding.ActorRadius*ActorScale,Depth=FVector::DotProduct(Forward,Delta)-Radius;
   // Conservative perspective bound includes off-axis depth variation. Close,
   // near-plane, high-detail and morph-driven parts always retain their slots.
   const double OffAxis=FMath::Sqrt(FMath::Square(FVector::DotProduct(Right,Delta))+FMath::Square(FVector::DotProduct(Up,Delta)))+Radius;
   const double Projection=Depth>1?2*Scale/Depth*(1+OffAxis/Depth):DBL_MAX;
   for(const auto& Slot:Binding.Slots){
    const bool Shown=Skin->IsMaterialSectionShown(Slot.Material,0);const double Pixels=Slot.Radius*ActorScale*Projection;bool Show=!Far||Pixels>=(Shown?SectionLODPixels:SectionLODPixels*1.25);for(const auto& Face:Faces)if(Face.Value.LowPacked.Get()==Skin&&Face.Value.Asset->LowPackedSlots.Contains(Slot.Material)){Show=false;break;}
    if(Shown!=Show){Skin->ShowMaterialSection(Slot.Material,Slot.Section,Show,0);++SectionLODTransitions;if(Show)++SectionLODRestores;}
    if(!Show)SectionLODHidden+=Slot.Count;
   }
 }
 SectionLODMaxHidden=FMath::Max(SectionLODMaxHidden,SectionLODHidden);
}
void AOKCrowdScene::SetHighDetailFocus(int32 ResidentIndex){
 FocusedHighOnly=true;DetailFocus=FMath::Clamp(ResidentIndex,-1,99);DetailInteractionStarted=true;
}
void AOKCrowdScene::BeginPlay(){
#if OK_CROWD_RUNTIME_APNG
 FString RuntimeSceneManifest;
 if(FParse::Value(FCommandLine::Get(),TEXT("CrowdRuntimeScene="),RuntimeSceneManifest)){
  Super::BeginPlay();Stopping=true;FString Error;
  extern bool OKStartCrowdRuntimeScene(AActor*,const FString&,FString&);
  if(!OKStartCrowdRuntimeScene(this,RuntimeSceneManifest,Error)){UE_LOG(LogTemp,Error,TEXT("APNG scene: %s"),*Error);FPlatformMisc::RequestExitWithStatus(false,1);}return;
 }
 FString RuntimeRecipe,RuntimeApng,RuntimeOutput,RuntimeCurves;
 if(FParse::Value(FCommandLine::Get(),TEXT("CrowdRuntimePoseRecipe="),RuntimeRecipe)||FParse::Value(FCommandLine::Get(),TEXT("CrowdRuntimePoseCurves="),RuntimeCurves)){
  Super::BeginPlay();Stopping=true;
  if(!FParse::Value(FCommandLine::Get(),TEXT("CrowdRuntimePoseApng="),RuntimeApng)||!FParse::Value(FCommandLine::Get(),TEXT("CrowdRuntimePoseOutput="),RuntimeOutput)||!OKStartCrowdRuntimePoseProbe(RuntimeRecipe,RuntimeApng,RuntimeOutput,RuntimeCurves))FPlatformMisc::RequestExitWithStatus(false,1);
  return;
 }
#endif
 if(FParse::Param(FCommandLine::Get(),TEXT("CrowdRoleGeometryPatch"))&&!FShaderCodeLibrary::OpenLibrary(TEXT("GpuPoseAudit"),FPaths::ProjectContentDir())){UE_LOG(LogTemp,Error,TEXT("Missing supplementary crowd geometry shader library"));FPlatformMisc::RequestExitWithStatus(false,1);return;}
 Super::BeginPlay();
 CrowdBehaviors=FParse::Param(FCommandLine::Get(),TEXT("CrowdBehaviors"));
 if(CrowdBehaviors&&!OKValidateCrowdBehaviors()){UE_LOG(LogTemp,Error,TEXT("Crowd behavior route invariant failed"));FPlatformMisc::RequestExitWithStatus(false,1);return;}
#if !WITH_EDITOR
 // Gameplay renders from normal skin matrices, not the Development debug copy.
 // Authoring Editor and other games retain their own Engine defaults.
 if(auto* DebugVariable=IConsoleManager::Get().FindConsoleVariable(TEXT("r.SkeletalMesh.CopyDebugPose")))DebugVariable->Set(FParse::Param(FCommandLine::Get(),TEXT("CrowdKeepDebugPose"))?1:0,ECVF_SetByConsole);
#endif
 if(WantsConstantScaleCache()){
  auto* CacheVariable=IConsoleManager::Get().FindConsoleVariable(TEXT("r.Anim.ConstantScaleCache"));auto* VerifyVariable=IConsoleManager::Get().FindConsoleVariable(TEXT("r.Anim.VerifyConstantScaleCache"));
#if !WITH_EDITOR && OK_CROWD_CONSTANT_SCALE_CACHE
  if(CacheVariable&&VerifyVariable){CacheVariable->Set(1,ECVF_SetByConsole);VerifyVariable->Set(FParse::Param(FCommandLine::Get(),TEXT("CrowdVerifyConstantScaleCache"))?1:0,ECVF_SetByConsole);}else
#endif
  {UE_LOG(LogTemp,Error,TEXT("Constant-scale experiment requires matching Game Engine capability"));FPlatformMisc::RequestExitWithStatus(false,1);return;}
 }
 if(FParse::Param(FCommandLine::Get(),TEXT("CrowdParallelBonePacking")))OKInstallCrowdBonePacking(GetWorld(),FParse::Param(FCommandLine::Get(),TEXT("CrowdVerifyBonePacking")));
 // A resizable window must render at its actual viewport resolution. The
 // automatic screen-percentage heuristic can otherwise upscale a small image
 // after maximizing; FXAA alone also misses thin toon outlines and hair.
 int32 ImageAA=2;FParse::Value(FCommandLine::Get(),TEXT("CrowdAntiAliasing="),ImageAA);
 if(ImageAA!=1&&ImageAA!=2&&ImageAA!=4)ImageAA=2;
 auto ImageSetting=[](const TCHAR* Name,float Value){if(auto* CVar=IConsoleManager::Get().FindConsoleVariable(Name))CVar->Set(Value,ECVF_SetByConsole);};
 ImageSetting(TEXT("r.AntiAliasingMethod"),ImageAA);
 ImageSetting(TEXT("r.ScreenPercentage"),100);
 ImageSetting(TEXT("r.SecondaryScreenPercentage.GameViewport"),100);
 ImageSetting(TEXT("r.DynamicRes.OperationMode"),0);
 ImageSetting(TEXT("r.TemporalAA.Quality"),2);
 ImageSetting(TEXT("r.TemporalAA.HistoryScreenPercentage"),100);
 UE_LOG(LogTemp,Display,TEXT("Crowd image quality: AA=%d primary=100 secondary=100 dynamicResolution=off"),ImageAA);
 float MeasurementSeconds=0;if(FParse::Value(FCommandLine::Get(),TEXT("CrowdRunSeconds="),MeasurementSeconds)&&FMath::IsFinite(MeasurementSeconds)&&MeasurementSeconds>0)SampleCapacity=FMath::Clamp(FMath::CeilToInt(MeasurementSeconds*1000),3600,600000);
 BalancedPose=FParse::Param(FCommandLine::Get(),TEXT("CrowdBalancedPose"));FParse::Value(FCommandLine::Get(),TEXT("CrowdFrameSeries="),FrameSeriesPath);
 SuppressPeriodicReports=FParse::Param(FCommandLine::Get(),TEXT("CrowdSuppressPeriodicReports"));
 FParse::Value(FCommandLine::Get(),TEXT("CrowdTraceSeconds="),TraceDurationSeconds);
 if(!FMath::IsFinite(TraceDurationSeconds)||TraceDurationSeconds<20||TraceDurationSeconds>90){Stopping=true;FPlatformMisc::RequestExitWithStatus(false,1);return;}
 CullRenderUploads=FParse::Param(FCommandLine::Get(),TEXT("CrowdCullRenderUploads"));
 FParse::Value(FCommandLine::Get(),TEXT("CrowdAnimationHz="),AnimationHz);
 FParse::Value(FCommandLine::Get(),TEXT("CrowdOutlineRadius="),OutlineRadius);
 if(!FMath::IsFinite(OutlineRadius)||OutlineRadius<0||OutlineRadius>1600){Stopping=true;FPlatformMisc::RequestExitWithStatus(false,1);return;}
 CacheShadowPolicy=FParse::Param(FCommandLine::Get(),TEXT("CrowdCacheShadowPolicy"));FParse::Value(FCommandLine::Get(),TEXT("CrowdShadowHysteresisCm="),ShadowHysteresisCm);
 FParse::Value(FCommandLine::Get(),TEXT("CrowdShadowRadius="),ShadowRadius);FParse::Value(FCommandLine::Get(),TEXT("CrowdUploadNearRadius="),UploadNearRadius);
 if(!FMath::IsFinite(ShadowRadius)||ShadowRadius<300||ShadowRadius>1200||!FMath::IsFinite(UploadNearRadius)||UploadNearRadius<1200||UploadNearRadius>1800){Stopping=true;FPlatformMisc::RequestExitWithStatus(false,1);return;}
 if(!FMath::IsFinite(ShadowHysteresisCm)||ShadowHysteresisCm<0||ShadowHysteresisCm>200||ShadowHysteresisCm>ShadowRadius/3||(ShadowHysteresisCm>0&&!CacheShadowPolicy)){Stopping=true;FPlatformMisc::RequestExitWithStatus(false,1);return;}
 if((AnimationHz!=0&&AnimationHz!=60)||(BalancedPose&&AnimationHz!=60)){Stopping=true;FPlatformMisc::RequestExitWithStatus(false,1);return;}
 ThirdPersonCamera=FParse::Param(FCommandLine::Get(),TEXT("CrowdThirdPerson"));FParse::Value(FCommandLine::Get(),TEXT("CrowdCameraResident="),CameraResident);
 PlayerMovementTest=FParse::Param(FCommandLine::Get(),TEXT("CrowdMovementTest"));
 PlayerControl=ThirdPersonCamera&&(PlayerMovementTest||FParse::Param(FCommandLine::Get(),TEXT("CrowdPlayerControl")));
 FaceLOD=!FParse::Param(FCommandLine::Get(),TEXT("CrowdDisableFaceLOD"));
 FocusOnlyShadows=FParse::Param(FCommandLine::Get(),TEXT("CrowdFocusOnlyShadows"));FocusedHighOnly=FParse::Param(FCommandLine::Get(),TEXT("CrowdFocusedHighOnly"));DetailFocus=ThirdPersonCamera?CameraResident:INDEX_NONE;FParse::Value(FCommandLine::Get(),TEXT("CrowdFocusResident="),DetailFocus);
 if(DetailFocus<-1||DetailFocus>99){Stopping=true;FPlatformMisc::RequestExitWithStatus(false,1);return;}
 // Apply before any crowd component is created. The engine recreates render
 // states when this mode changes; never toggle it inside a benchmark window.
 if(FParse::Param(FCommandLine::Get(),TEXT("CrowdVertexFactoryBoneBuffers")))if(auto* Mode=IConsoleManager::Get().FindConsoleVariable(TEXT("r.GPUSkin.BoneTransformAllocationMode")))Mode->Set(0,ECVF_SetByCode);
 SceneStarted=FPlatformTime::Seconds();
 FString LoadingTrace;if(FParse::Value(FCommandLine::Get(),TEXT("CrowdLoadingTrace="),LoadingTrace)){FTraceAuxiliary::FOptions Options;Options.bExcludeTail=true;if(!FTraceAuxiliary::Start(FTraceAuxiliary::EConnectionType::File,*LoadingTrace,TEXT("cpu,frame,bookmark"),&Options)){UE_LOG(LogTemp,Error,TEXT("Crowd loading trace could not start"));Stopping=true;FPlatformMisc::RequestExitWithStatus(false,1);return;}}
 FString LaunchMilliseconds;if(FParse::Value(FCommandLine::Get(),TEXT("CrowdLaunchUnixMs="),LaunchMilliseconds)){const int64 Launch=FCString::Atoi64(*LaunchMilliseconds);const auto UTC=FDateTime::UtcNow();StartupSeconds=FMath::Max(0.,(double(UTC.ToUnixTimestamp())+UTC.GetMillisecond()/1000.)-Launch/1000.);}
 GpuPose=FParse::Param(FCommandLine::Get(),TEXT("CrowdGpuPose"));
 FString IndependentCache,IndependentIdentity,IndependentIndex;
 FParse::Value(FCommandLine::Get(),TEXT("CrowdIndependentGpuCache="),IndependentCache);
 FParse::Value(FCommandLine::Get(),TEXT("CrowdIndependentGpuShardIndex="),IndependentIndex);
 FParse::Value(FCommandLine::Get(),IndependentIndex.IsEmpty()?TEXT("CrowdIndependentGpuIdentity="):TEXT("CrowdIndependentGpuShardIndexIdentity="),IndependentIdentity);
 if(FParse::Param(FCommandLine::Get(),TEXT("CrowdIndependentGpuAnimation")))IndependentGpuAnimationSwitch->Set(1,ECVF_SetByConsole);
 PollIndependentGpuAnimationControl();
 if(GpuPose&&!IConsoleManager::Get().FindConsoleVariable(TEXT("r.CrowdMatrixBank.RuntimeVersion"))){UE_LOG(LogTemp,Error,TEXT("Crowd GPU matrix pose requires the isolated MatrixBank runtime"));Stopping=true;FPlatformMisc::RequestExitWithStatus(false,1);return;}
 FParse::Value(FCommandLine::Get(),TEXT("CrowdControl="),PerformanceControl);
 FString SelectionFile;
 if(FParse::Value(FCommandLine::Get(),TEXT("CrowdSelection="),SelectionFile)){
  FString Text;TSharedPtr<FJsonObject> Selection;const TArray<TSharedPtr<FJsonValue>>* Entries=nullptr;
  bool Valid=FFileHelper::LoadFileToString(Text,*SelectionFile)&&FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Selection)&&Selection.IsValid()&&Selection->TryGetArrayField(TEXT("characters"),Entries)&&Entries&&Entries->Num()==100;
  TArray<TSoftObjectPtr<UKKImportedCharacter>> Cards;TArray<TSoftObjectPtr<UKKImportedAnimationLibrary>> Libraries;TArray<FString> Names;
  if(Valid)for(const auto& Entry:*Entries){const auto Object=Entry->AsObject();FString Card,Walk,Name;if(!Object.IsValid()||!Object->TryGetStringField(TEXT("character"),Card)||!Object->TryGetStringField(TEXT("walk"),Walk)||!Card.StartsWith(TEXT("/Game/"))||!Walk.StartsWith(TEXT("/Game/"))){Valid=false;break;}Object->TryGetStringField(TEXT("name"),Name);Cards.Add(TSoftObjectPtr<UKKImportedCharacter>(FSoftObjectPath(Card)));Libraries.Add(TSoftObjectPtr<UKKImportedAnimationLibrary>(FSoftObjectPath(Walk)));Names.Add(Name);}
  if(!Valid){UE_LOG(LogTemp,Error,TEXT("Crowd selection must contain 100 valid game character/walk paths"));Stopping=true;FPlatformMisc::RequestExitWithStatus(false,1);return;}Characters=MoveTemp(Cards);Walks=MoveTemp(Libraries);CardNames=MoveTemp(Names);
 }
 int32 PilotIndex=-1;if(FParse::Value(FCommandLine::Get(),TEXT("CrowdPilotIndex="),PilotIndex)){if(!Characters.IsValidIndex(PilotIndex)||!Walks.IsValidIndex(PilotIndex)){Stopping=true;FPlatformMisc::RequestExitWithStatus(false,1);return;}const auto Card=Characters[PilotIndex];const auto Walk=Walks[PilotIndex];const auto Name=CardNames[PilotIndex];Characters={Card};Walks={Walk};CardNames={Name};QualityPilot=true;}
 for(int32 I=0;I<Characters.Num();++I){const FString Base=FPaths::GetPath(Characters[I].ToSoftObjectPath().GetLongPackageName())/TEXT("PackedRig");const bool Packed=FPackageName::DoesPackageExist(Base/TEXT("Character"))&&FPackageName::DoesPackageExist(Base/TEXT("Walk/Animations"));HighCharacters.Add(Packed?TSoftObjectPtr<UKKImportedCharacter>(FSoftObjectPath(Base/TEXT("Character.Character"))):Characters[I]);HighWalks.Add(Packed?TSoftObjectPtr<UKKImportedAnimationLibrary>(FSoftObjectPath(Base/TEXT("Walk/Animations.Animations"))):Walks[I]);}
 HighDetail.Init(false,Characters.Num());
 // Preserve the map's original selection; optimized manifests have the same
 // source and geometry and are published only after every resource is saved.
 if(!FParse::Param(FCommandLine::Get(),TEXT("CrowdFullSkinReferences"))){
  FString RequestedVariant;FParse::Value(FCommandLine::Get(),TEXT("CrowdQualityVariant="),RequestedVariant);
  if(RequestedVariant.IsEmpty()&&!FParse::Param(FCommandLine::Get(),TEXT("CrowdOriginalQuality")))RequestedVariant=TEXT("GameLowHairSafeR9");
  for(const FString Variant:{FString(TEXT("GameLowRoleBudgetRigR4")),FString(TEXT("GameLowRoleBudgetR3")),FString(TEXT("GameLowRoleBudgetRigR2")),FString(TEXT("GameLowRoleBudgetR1")),FString(TEXT("GameLowHairSafeR9")),FString(TEXT("GameLowSurfaceSafeR8")),FString(TEXT("GameLowBindingSafeR7")),FString(TEXT("GameLowBodySafeR6")),FString(TEXT("GameLowSRTR1")),FString(TEXT("GameLow30kMaskR1")),FString(TEXT("GameLow30kR2")),FString(TEXT("GameLow30kR1")),FString(TEXT("GameLow10kR1")),FString(TEXT("GameLow256R1")),FString(TEXT("InstancingBonesR1")),FString(TEXT("Performance256AtlasV2")),FString(TEXT("Performance256")),FString(TEXT("PackedRig")),FString(TEXT("PackedCompact")),FString(TEXT("Packed")),FString(TEXT("Compact"))}){
  if(Variant.StartsWith(TEXT("GameLow"))&&RequestedVariant!=Variant)continue;
  if(Variant==TEXT("InstancingBonesR1")&&RequestedVariant!=Variant)continue;
  // Offline quality experiments are never selected for arbitrary player cards.
  if(Variant.StartsWith(TEXT("Performance256"))&&RequestedVariant!=Variant)continue;
  if(!RequestedVariant.IsEmpty()&&Variant!=RequestedVariant)continue;
  if(Variant.StartsWith(TEXT("Performance256"))&&FParse::Param(FCommandLine::Get(),TEXT("CrowdOriginalQuality")))continue;
  if(Variant.StartsWith(TEXT("Packed"))&&FParse::Param(FCommandLine::Get(),TEXT("CrowdUnpacked")))continue;
  TArray<FSoftObjectPath> CompactCards,CompactWalks;bool Complete=Characters.Num()==100||QualityPilot;
  for(const auto& Character:Characters){const FString Base=FPaths::GetPath(Character.ToSoftObjectPath().GetLongPackageName())/Variant;
   const FString Card=Base/TEXT("Character"),OriginalWalk=Base/TEXT("Walk/Animations"),RepairedWalk=Base/TEXT("WalkRigidBindingsR2/Animations");
   const FString Walk=!FParse::Param(FCommandLine::Get(),TEXT("CrowdLegacyRigidBindings"))&&FPackageName::DoesPackageExist(RepairedWalk)?RepairedWalk:OriginalWalk;
   Complete&=FPackageName::DoesPackageExist(Card)&&FPackageName::DoesPackageExist(Walk);
   CompactCards.Add(FSoftObjectPath(Card+TEXT(".Character")));CompactWalks.Add(FSoftObjectPath(Walk+TEXT(".Animations")));
  }
  if(Complete){for(int32 I=0;I<Characters.Num();++I){Characters[I]=TSoftObjectPtr<UKKImportedCharacter>(CompactCards[I]);Walks[I]=TSoftObjectPtr<UKKImportedAnimationLibrary>(CompactWalks[I]);}AssetVariant=Variant;LightweightGame=Variant.StartsWith(TEXT("GameLow"));PackedSkinParts=LightweightGame||Variant==TEXT("InstancingBonesR1")||Variant.StartsWith(TEXT("Packed"))||Variant.StartsWith(TEXT("Performance256"));CompactSkinReferences=Variant!=TEXT("Packed");QualityAnimationRates=false;break;}
  }
  if(!RequestedVariant.IsEmpty()&&AssetVariant!=RequestedVariant){UE_LOG(LogTemp,Error,TEXT("Requested crowd quality variant is incomplete: %s"),*RequestedVariant);Stopping=true;FPlatformMisc::RequestExitWithStatus(false,1);return;}
 }
 if(!IndependentCache.IsEmpty()||!IndependentIndex.IsEmpty()){
  TArray<FString> SelectedGpuCharacters;for(const auto& Character:Characters)SelectedGpuCharacters.Add(Character.ToSoftObjectPath().ToString());
  IndependentGpuRuntime=OKCreateCrowdIndependentGpu(GetWorld(),IndependentCache,IndependentIdentity,GpuPose||FParse::Param(FCommandLine::Get(),TEXT("CrowdParallelBonePacking")),FParse::Param(FCommandLine::Get(),TEXT("CrowdIndependentGpuVerify")),IndependentIndex,MoveTemp(SelectedGpuCharacters));
 }
 if(!LoadSectionLOD()){UE_LOG(LogTemp,Error,TEXT("Crowd section LOD metadata/configuration invalid"));Stopping=true;FPlatformMisc::RequestExitWithStatus(false,1);return;}
 if(CullRenderUploads&&(GpuPose||(AssetVariant!=TEXT("GameLow30kR1")&&AssetVariant!=TEXT("GameLowBodySafeR6")&&AssetVariant!=TEXT("GameLowBindingSafeR7")&&AssetVariant!=TEXT("GameLowSurfaceSafeR8")&&AssetVariant!=TEXT("GameLowHairSafeR9"))||SectionLODMeshes.IsEmpty())){UE_LOG(LogTemp,Error,TEXT("Crowd upload culling requires current low bounds and CPU animation"));Stopping=true;FPlatformMisc::RequestExitWithStatus(false,1);return;}
 auto* Camera=GetWorld()->SpawnActor<ACameraActor>(FVector(0,-4300,3400),FRotator(-38,90,0));
 Camera->GetCameraComponent()->FieldOfView=72;
 // Scope the close-inspection plane to this scene; EndPlay restores it.
 PreviousNearClip=GNearClippingPlane;SetNearClipPlaneGlobals(1.f);
 {auto& Post=Camera->GetCameraComponent()->PostProcessSettings;Post.bOverride_AutoExposureMethod=true;Post.AutoExposureMethod=AEM_Manual;Post.bOverride_AutoExposureBias=true;Post.AutoExposureBias=0;Post.bOverride_AutoExposureApplyPhysicalCameraExposure=true;Post.AutoExposureApplyPhysicalCameraExposure=false;
  // Imported Unity toon shaders already compose their game color into
  // emissive. Match the studio's output transfer instead of applying a second
  // film curve, which compresses colored highlights toward white.
  const bool FilmComparison=FParse::Param(FCommandLine::Get(),TEXT("CrowdFilmColorComparison"));
  Post.bOverride_ToneCurveAmount=true;Post.ToneCurveAmount=FilmComparison?1.f:0.f;
  Post.bOverride_ExpandGamut=true;Post.ExpandGamut=FilmComparison?1.f:0.f;
  Post.bOverride_BloomIntensity=true;Post.BloomIntensity=0.f;
  UE_LOG(LogTemp,Display,TEXT("Crowd color output: film=%d exposure=0 toneCurve=%g expandGamut=%g bloom=0"),FilmComparison,Post.ToneCurveAmount,Post.ExpandGamut);
 }
 if(QualityPilot){const FVector Target=OKCrowdLayout::Center(0)+FVector(0,0,85),Location=Target+FVector(260,-420,40);Camera->SetActorLocationAndRotation(Location,(Target-Location).Rotation());auto& Post=Camera->GetCameraComponent()->PostProcessSettings;Post.bOverride_AutoExposureMethod=true;Post.AutoExposureMethod=AEM_Manual;Post.bOverride_AutoExposureApplyPhysicalCameraExposure=true;Post.AutoExposureApplyPhysicalCameraExposure=false;}
 if(ThirdPersonCamera){if(QualityPilot)CameraResident=0;if(!Characters.IsValidIndex(CameraResident)){Stopping=true;FPlatformMisc::RequestExitWithStatus(false,1);return;}const FVector Target=OKCrowdLayout::Position(CameraResident,0)+FVector(0,0,100);const FRotator Facing(ThirdPersonPitch,OKCrowdLayout::Rotation(CameraResident,0).Yaw+90,0);const FVector Location=Target-Facing.Vector()*360+FRotationMatrix(Facing).GetUnitAxis(EAxis::Y)*45;Camera->SetActorLocationAndRotation(Location,(Target-Location).Rotation());}
 if(auto* PC=GetWorld()->GetFirstPlayerController())PC->SetViewTarget(Camera);
 EntryCameraLocation=Camera->GetActorLocation();
 DeferResidentsDuringLoad=LightweightGame&&!FParse::Param(FCommandLine::Get(),TEXT("CrowdProgressiveLoad"));
 if(DeferResidentsDuringLoad){auto* Budget=IConsoleManager::Get().FindConsoleVariable(TEXT("s.AsyncLoadingTimeLimit"));auto* Full=IConsoleManager::Get().FindConsoleVariable(TEXT("s.AsyncLoadingUseFullTimeLimit"));if(Budget&&Full){PreviousLoadingBudget=Budget->GetFloat();PreviousFullLoadingBudget=Full->GetInt();Budget->Set(100.f,ECVF_SetByCode);Full->Set(1,ECVF_SetByCode);LoadingBudgetChanged=true;}}
 // Every benchmark starts from the same overview; the game controller can fly around.
 FString BehaviorProfile;if(CrowdBehaviors&&FParse::Value(FCommandLine::Get(),TEXT("CrowdBehaviorProfile="),BehaviorProfile)){
  if(BehaviorProfile.IsEmpty()||BehaviorProfile.Contains(TEXT(".."))||BehaviorProfile.Contains(TEXT("/"))||BehaviorProfile.Contains(TEXT("\\"))){UE_LOG(LogTemp,Error,TEXT("Invalid behavior profile"));FPlatformMisc::RequestExitWithStatus(false,1);return;}
  for(int32 I=0;I<Characters.Num();++I){const FString Folder=FPaths::GetPath(Characters[I].ToSoftObjectPath().GetLongPackageName());const FString Package=Folder/BehaviorProfile/TEXT("Animations");if(!FPackageName::DoesPackageExist(Package)){UE_LOG(LogTemp,Error,TEXT("Behavior profile missing character=%s library=%s"),*Characters[I].ToString(),*Package);FPlatformMisc::RequestExitWithStatus(false,1);return;}Walks[I]=TSoftObjectPtr<UKKImportedAnimationLibrary>(FSoftObjectPath(Package+TEXT(".Animations")));}
 }
 LoadNext();
 LastWall=FPlatformTime::Seconds();
}
void AOKCrowdScene::UpdateThirdPersonCamera(){
 if(!ThirdPersonCamera||ReadyAt<=0||!Residents.IsValidIndex(CameraResident))return;
 auto* PC=GetWorld()->GetFirstPlayerController();if(!PC)return;auto* Camera=Cast<ACameraActor>(PC->GetViewTarget());auto* Actor=Residents[CameraResident].Get();if(!Camera||!Actor)return;
 if(!PlayerMovementTest&&PC->IsInputKeyDown(EKeys::RightMouseButton)){float X,Y;PC->GetInputMouseDelta(X,Y);if(PlayerControl)PlayerCameraYaw+=X*.15f;else ThirdPersonYaw+=X*.15f;ThirdPersonPitch=FMath::Clamp(ThirdPersonPitch-Y*.15f,-55.f,5.f);}
 // Imported characters face local +Y; the walking path is tangent to its orbit.
 const FVector Target=Actor->GetActorLocation()+FVector(0,0,100);const FRotator Facing(ThirdPersonPitch,PlayerControl?PlayerCameraYaw:Actor->GetActorRotation().Yaw+90+ThirdPersonYaw,0);
 const FVector DesiredLocation=Target-Facing.Vector()*360+FRotationMatrix(Facing).GetUnitAxis(EAxis::Y)*45;const FVector Location=PlayerControl?OKCrowdLayout::ClampPlayerToRoom(DesiredLocation):DesiredLocation;Camera->SetActorLocationAndRotation(Location,(Target-Location).Rotation());
}
void AOKCrowdScene::UpdatePlayerMovement(float DeltaSeconds){
 if(!PlayerControl||ReadyAt<=0||!Residents.IsValidIndex(CameraResident))return;
 auto* Actor=Residents[CameraResident].Get();auto* PC=GetWorld()->GetFirstPlayerController();if(!Actor||!PC)return;
 if(!PlayerMovementInitialized){
  const double SpawnTime=PerformanceControl.IsEmpty()?TravelTime:0.;
  Actor->SetActorLocationAndRotation(OKCrowdLayout::Position(CameraResident,SpawnTime),OKCrowdLayout::Rotation(CameraResident,SpawnTime));
  PlayerCameraYaw=Actor->GetActorRotation().Yaw+90;PlayerPreviousPosition=Actor->GetActorLocation();PlayerMovementInitialized=true;
 }
 float Forward=float(PC->IsInputKeyDown(EKeys::W))-float(PC->IsInputKeyDown(EKeys::S));
 float Right=float(PC->IsInputKeyDown(EKeys::D))-float(PC->IsInputKeyDown(EKeys::A));
 int32 TestPhase=INDEX_NONE;
 if(PlayerMovementTest){
  const double Time=FPlatformTime::Seconds()-ReadyAt;Forward=Right=0;
  if(Time>=2&&Time<7){TestPhase=FMath::FloorToInt(Time)-2;Forward=(TestPhase==0||TestPhase==4)?1:TestPhase==1?-1:0;Right=(TestPhase==3||TestPhase==4)?1:TestPhase==2?-1:0;}
  else if(Time>=8&&Time<28)Forward=1;
 }
 const FVector Before=Actor->GetActorLocation();const FVector Step=OKCrowdLayout::PlayerStep(Forward,Right,PlayerCameraYaw,DeltaSeconds);
 const FVector After=OKCrowdLayout::ClampPlayerToRoom(Before+Step);const FVector ActualMove=After-Before;
 if(!After.Equals(Before+Step,.001))PlayerReachedBoundary=true;
 const bool Moving=!ActualMove.IsNearlyZero();
 if(!Moving)PlayerIdleDriftCm+=FVector::Dist(Before,PlayerPreviousPosition);
 else{const float Yaw=FMath::FixedTurn(Actor->GetActorRotation().Yaw,ActualMove.Rotation().Yaw-90,720.f*DeltaSeconds);Actor->SetActorLocationAndRotation(After,FRotator(0,Yaw,0));}
 PlayerDistanceCm+=ActualMove.Size();PlayerPreviousPosition=Actor->GetActorLocation();
 if(TestPhase>=0&&TestPhase<5)MovementTestDistances[TestPhase]+=ActualMove.Size();
 // Reapply after a high/low actor replacement, but avoid component enumeration
 // every frame. Followers keep their existing private bone mappings.
 if(PlayerAnimationActor.Get()!=Actor||Moving!=PlayerWasMoving){TArray<USkeletalMeshComponent*> Skins;Actor->GetComponents(Skins);for(auto* Skin:Skins)if(Skin->GetAnimationMode()==EAnimationMode::AnimationSingleNode)Skin->SetPlayRate((Moving||!PerformanceControl.IsEmpty())?1.f:0.f);PlayerAnimationActor=Actor;PlayerWasMoving=Moving;}
}
void AOKCrowdScene::BindRenderUploads(AKKImportedCharacterActor* Actor,bool IsLow){
 if(!CullRenderUploads||!IsLow||!Actor||Actor->Character->Schema!=1005)return;
 UploadBindings.RemoveAll([](const auto& Binding){return !Binding.Actor.IsValid();});FUploadBinding Binding;Binding.Actor=Actor;
 TArray<UOKCrowdRenderSkin*> Skins;Actor->GetComponents(Skins);
 for(auto* Skin:Skins){auto* Mesh=Skin->GetSkeletalMeshAsset();if(!Mesh)continue;const auto* Bound=SectionLODMeshes.Find(Mesh->GetPathName());if(Bound&&Bound->SourceHash==Actor->Character->SourceHash)Binding.Radius=FMath::Max(Binding.Radius,Bound->ActorRadius);
  FUploadSkin Entry;Entry.Skin=Skin;for(int32 M=0;M<Mesh->GetMaterials().Num();++M)if(auto* Material=Skin->GetMaterial(M))if(Material->IsUsingWorldPositionOffset_Concurrent(GShaderPlatformForFeatureLevel[GetWorld()->GetFeatureLevel()]))Entry.WPOSlots.Add(M);Binding.Skins.Add(MoveTemp(Entry));
 }
 if(Binding.Radius>0&&!Binding.Skins.IsEmpty())UploadBindings.Add(MoveTemp(Binding));
}
void AOKCrowdScene::UpdateRenderUploads(){
 if(!CullRenderUploads||ReadyAt<=0)return;TRACE_CPUPROFILER_EVENT_SCOPE(CrowdRenderUploadPolicy);
 auto* PC=GetWorld()->GetFirstPlayerController();auto* Player=PC?PC->GetLocalPlayer():nullptr;auto* Viewport=GetWorld()->GetGameViewport();FSceneViewProjectionData Projection;
 const bool Valid=Player&&Viewport&&Viewport->Viewport&&Player->GetProjectionData(Viewport->Viewport,Projection)&&Projection.IsPerspectiveProjection();FConvexVolume Frustum;if(Valid)GetViewFrustumBounds(Frustum,Projection.ComputeViewProjectionMatrix(),true);
 UploadBoundSkins=0;UploadSuppressedCandidates=0;
 for(const auto& Binding:UploadBindings){auto* Actor=Binding.Actor.Get();if(!Actor)continue;
  // Every-frame view test plus a 200cm halo anticipates rotation/re-entry.
  // Near models and shadow casters are never suppressed. Cached radius covers
  // the whole walk and all parts, independently of a bind-pose mesh AABB.
  const bool Needed=!Valid||FVector::DistSquared(Projection.ViewOrigin,Actor->GetActorLocation())<FMath::Square(UploadNearRadius)||Frustum.IntersectSphere(Actor->GetActorLocation(),Binding.Radius*Actor->GetActorScale3D().GetAbsMax()+200.);
  for(const auto& Entry:Binding.Skins)if(auto* Skin=Entry.Skin.Get()){bool Keep=Needed||Skin->CastShadow||Skin->bCastHiddenShadow||Skin->IsSimulatingPhysics();for(const int32 M:Entry.WPOSlots)if(Skin->IsMaterialSectionShown(M,0))Keep=true;Skin->SetCrowdRenderNeeded(Keep);++UploadBoundSkins;if(!Keep&&Skin->IsVisible())++UploadSuppressedCandidates;}
 }
}
void AOKCrowdScene::ApplyBalancedPosePhase(AKKImportedCharacterActor* Actor,int32 Index){
 if(!BalancedPose||!Actor||!BalancedPhases.IsValidIndex(Index))return;TArray<UOKCrowdRenderSkin*> Skins;Actor->GetComponents(Skins);for(auto* Skin:Skins)Skin->PosePhaseCycles=BalancedPhases[Index];
}
void AOKCrowdScene::BalancePosePhases(){
 if(!BalancedPose||Residents.IsEmpty())return;TRACE_CPUPROFILER_EVENT_SCOPE(CrowdBalancePosePhases);
 struct FWork {int32 Index;double Cost;};struct FBin {double Cost=0;TArray<FWork> Work;};TArray<FWork> Ordered;TArray<FBin> Bins;Bins.SetNum(FMath::Min(16,Residents.Num()));BalancedPhases.SetNumZeroed(Residents.Num());
 for(int32 I=0;I<Residents.Num();++I){auto* Mesh=MotionProbes.IsValidIndex(I)&&MotionProbes[I]?MotionProbes[I]->GetSkeletalMeshAsset():nullptr;Ordered.Add({I,double(Mesh?FMath::Max(1,Mesh->GetRefSkeleton().GetNum()):1)});}
 Ordered.Sort([](const FWork& A,const FWork& B){return A.Cost!=B.Cost?A.Cost>B.Cost:A.Index<B.Index;});
 for(const auto& Work:Ordered){int32 Bin=0;for(int32 B=1;B<Bins.Num();++B)if(Bins[B].Cost<Bins[Bin].Cost)Bin=B;Bins[Bin].Cost+=Work.Cost;Bins[Bin].Work.Add(Work);}
 PoseBalanceMin=DBL_MAX;PoseBalanceMax=0;
 for(int32 B=0;B<Bins.Num();++B){double Prefix=0;PoseBalanceMin=FMath::Min(PoseBalanceMin,Bins[B].Cost);PoseBalanceMax=FMath::Max(PoseBalanceMax,Bins[B].Cost);for(const auto& Work:Bins[B].Work){BalancedPhases[Work.Index]=(B+(Prefix+Work.Cost*.5)/Bins[B].Cost)/Bins.Num();Prefix+=Work.Cost;}}
 for(int32 I=0;I<Residents.Num();++I)ApplyBalancedPosePhase(Residents[I],I);
}
void AOKCrowdScene::LoadNext(){
 if(Stopping)return;
 if(Next>=Characters.Num()){
#if WITH_EDITOR
  // Uncooked Development may still be compiling material shaders after the
  // final async package callback. Finish that preparation before warm-up.
  if(GShaderCompilingManager)GShaderCompilingManager->FinishAllCompilation();
#endif
  RestoreLoadingBudget();BalancePosePhases();
  if(DeferResidentsDuringLoad)for(const auto& Actor:Residents){TArray<USkeletalMeshComponent*> Skins;Actor->GetComponents(Skins);for(auto* Skin:Skins)Skin->SetComponentTickEnabled(!Skin->LeaderPoseComponent.IsValid()||(Skin->IsVisible()&&!Skin->ComponentTags.Contains(LeaderDrivenFollowerTag)));Actor->SetActorHiddenInGame(false);}
  ReadyAt=FPlatformTime::Seconds();InitialReadyAt=ReadyAt;LastWall=ReadyAt;
  if(LightweightGame){
   InitialHighMeshCount=0;InitialLargeTextureCount=0;
   // Audit indirect dependencies without resolving the high soft references.
   TArray<FString> CardRoots,LowRoots;for(const auto& Card:Characters){const FString LowRoot=FPaths::GetPath(Card.ToSoftObjectPath().GetLongPackageName());LowRoots.AddUnique(LowRoot+TEXT("/"));CardRoots.AddUnique(FPaths::GetPath(LowRoot)+TEXT("/"));}
   auto IsCharacterResource=[&CardRoots](const FString& Path){return CardRoots.ContainsByPredicate([&Path](const FString& Root){return Path.StartsWith(Root);});};
   // BodySafe reuses the already validated low head/hair/accessory meshes.
   if((AssetVariant==TEXT("GameLowBodySafeR6")||(AssetVariant==TEXT("GameLowBindingSafeR7")||(AssetVariant==TEXT("GameLowSurfaceSafeR8")||AssetVariant==TEXT("GameLowHairSafeR9")))))for(const auto& Root:CardRoots)LowRoots.AddUnique(Root+TEXT("GameLow30kR1/"));
   if((AssetVariant==TEXT("GameLowSurfaceSafeR8")||AssetVariant==TEXT("GameLowHairSafeR9")))for(const auto& Root:CardRoots)LowRoots.AddUnique(Root+TEXT("GameLowBindingSafeR7/"));
   if(AssetVariant==TEXT("GameLowHairSafeR9"))for(const auto& Root:CardRoots)LowRoots.AddUnique(Root+TEXT("GameLowSurfaceSafeR8/"));
   TArray<FString> LowTextureRoots=LowRoots;if((AssetVariant==TEXT("GameLowBodySafeR6")||(AssetVariant==TEXT("GameLowBindingSafeR7")||(AssetVariant==TEXT("GameLowSurfaceSafeR8")||AssetVariant==TEXT("GameLowHairSafeR9"))))||AssetVariant==TEXT("GameLowSRTR1")||AssetVariant==TEXT("GameLow30kR2")||AssetVariant==TEXT("GameLow30kMaskR1"))for(const auto& Root:CardRoots)LowTextureRoots.AddUnique(Root+TEXT("GameLow30kR1/"));
   // Rig compaction shares the validated small immutable textures. Permit only
   // that resource directory for textures; original R1 meshes remain forbidden.
   if(AssetVariant==TEXT("GameLowRoleBudgetRigR4"))for(const auto& Root:CardRoots)LowTextureRoots.AddUnique(Root+TEXT("GameLowRoleBudgetR3/"));
   if(AssetVariant==TEXT("GameLowRoleBudgetRigR2"))for(const auto& Root:CardRoots)LowTextureRoots.AddUnique(Root+TEXT("GameLowRoleBudgetR1/Resources/"));
   auto IsLowTexture=[&LowTextureRoots](const FString& Path){return LowTextureRoots.ContainsByPredicate([&Path](const FString& Root){return Path.StartsWith(Root);});};
   auto IsLowResource=[&LowRoots](const FString& Path){return LowRoots.ContainsByPredicate([&Path](const FString& Root){return Path.StartsWith(Root);});};
   for(TObjectIterator<USkeletalMesh> It;It;++It){const FString Path=It->GetOutermost()->GetName();if(IsCharacterResource(Path)&&!IsLowResource(Path)){++InitialHighMeshCount;UE_LOG(LogTemp,Error,TEXT("Crowd entry unexpectedly loaded high mesh %s"),*Path);}}
   for(TObjectIterator<UTexture2D> It;It;++It){const FString Path=It->GetOutermost()->GetName();if(IsCharacterResource(Path)&&(!IsLowTexture(Path)||It->GetSizeX()>256||It->GetSizeY()>256)){++InitialLargeTextureCount;UE_LOG(LogTemp,Error,TEXT("Crowd entry unexpectedly loaded high texture %s (%dx%d)"),*Path,It->GetSizeX(),It->GetSizeY());}}
   if(InitialHighMeshCount||InitialLargeTextureCount){SaveReport();Stopping=true;FPlatformMisc::RequestExitWithStatus(false,1);return;}
  }
  UE_LOG(LogTemp,Display,TEXT("Crowd ready selected=%d loaded=%d failed=%d imported_gpu=1"),Characters.Num(),Residents.Num(),Failed);
  if(FParse::Param(FCommandLine::Get(),TEXT("CrowdLoadProfile"))){FTraceAuxiliary::Stop();UE_LOG(LogTemp,Display,TEXT("Crowd loading trace stopped at ready"));}
  SaveReport();return;
 }
 LoadStarted=FPlatformTime::Seconds();
 if(LightweightGame&&Next>=PrefetchedUntil){TArray<FSoftObjectPath> Batch;PrefetchedUntil=FMath::Min(Next+8,Characters.Num());for(int32 I=Next;I<PrefetchedUntil;++I){Batch.Add(Characters[I].ToSoftObjectPath());Batch.Add(Walks[I].ToSoftObjectPath());}Preload=UAssetManager::GetStreamableManager().RequestAsyncLoad(Batch);}
 if(!Walks.IsValidIndex(Next)){++Failed;++Next;GetWorldTimerManager().SetTimerForNextTick(this,&AOKCrowdScene::LoadNext);return;}
 TArray<FSoftObjectPath> Paths={Characters[Next].ToSoftObjectPath(),Walks[Next].ToSoftObjectPath()};
 if(GpuPose){const FString Base=FPaths::GetPath(FPaths::GetPath(Characters[Next].ToSoftObjectPath().GetLongPackageName()));Paths.Add(FSoftObjectPath(Base/TEXT("GpuMatrixR3/Pose.Pose")));}
 Pending=UAssetManager::GetStreamableManager().RequestAsyncLoad(Paths,FStreamableDelegate::CreateUObject(this,&AOKCrowdScene::FinishCharacter));
}
void AOKCrowdScene::FinishCharacter(){
 TRACE_CPUPROFILER_EVENT_SCOPE(Crowd_FinishCharacter);
 const double BuildStarted=FPlatformTime::Seconds();
 if(Stopping)return;
 PackageWaitMilliseconds.Add((BuildStarted-LoadStarted)*1000.);
 auto* Card=Characters[Next].Get();
 auto* Library=Walks[Next].Get();
 if(LightweightGame&&Card&&(Card->Schema!=((AssetVariant==TEXT("GameLowRoleBudgetR3")||AssetVariant==TEXT("GameLowRoleBudgetRigR4"))?1017:AssetVariant.StartsWith(TEXT("GameLowRoleBudget"))?1016:AssetVariant==TEXT("GameLowSRTR1")?1009:(AssetVariant==TEXT("GameLow10kR1")?1006:((AssetVariant==TEXT("GameLow30kR1")||(AssetVariant==TEXT("GameLowBodySafeR6")||(AssetVariant==TEXT("GameLowBindingSafeR7")||(AssetVariant==TEXT("GameLowSurfaceSafeR8")||AssetVariant==TEXT("GameLowHairSafeR9")))))?1005:(AssetVariant==TEXT("GameLow30kR2")?1007:(AssetVariant==TEXT("GameLow30kMaskR1")?1008:1004)))))||!Library||Library->Character!=Card)){UE_LOG(LogTemp,Error,TEXT("Crowd gameplay low cache incompatible index=%d; prepare the current recipe"),Next);Card=nullptr;}
 if(Card&&((AssetVariant==TEXT("GameLowBodySafeR6")||(AssetVariant==TEXT("GameLowBindingSafeR7")||(AssetVariant==TEXT("GameLowSurfaceSafeR8")||AssetVariant==TEXT("GameLowHairSafeR9"))))||AssetVariant==TEXT("GameLowSRTR1")||AssetVariant==TEXT("GameLow30kR1")||AssetVariant==TEXT("GameLow30kR2")||AssetVariant==TEXT("GameLow30kMaskR1")||AssetVariant==TEXT("GameLow10kR1"))){int64 Triangles=0;bool Valid=Card->Outfits.Num()==1;if(!Card->Outfits.IsEmpty())for(const auto& Part:Card->Outfits[0].Parts)if(Part.SkinnedModel){const auto* Data=Part.SkinnedModel->GetResourceForRendering();Valid&=Data&&Data->LODRenderData.Num()==1;if(Data&&!Data->LODRenderData.IsEmpty())for(const auto& Section:Data->LODRenderData[0].RenderSections)Triangles+=Section.NumTriangles;}if(!Valid||Triangles<=0||Triangles>=(AssetVariant==TEXT("GameLow10kR1")?10000:30000)){UE_LOG(LogTemp,Error,TEXT("Crowd strict low invalid index=%d triangles=%lld single_lod=%d"),Next,Triangles,Valid);Card=nullptr;}}
 UClass* ActorClass=(AnimationHz==60||(CullRenderUploads&&Card&&Card->Schema==1005))?AOKCrowdRenderActor::StaticClass():AKKImportedCharacterActor::StaticClass();
 auto* Actor=(Card&&Library)?GetWorld()->SpawnActorDeferred<AKKImportedCharacterActor>(ActorClass,FTransform::Identity):nullptr;
 if(Actor&&GpuPose){
  const FString Base=FPaths::GetPath(FPaths::GetPath(Characters[Next].ToSoftObjectPath().GetLongPackageName()));
  auto* Pose=FindObject<UOKCrowdGpuPose>(nullptr,*(Base/TEXT("GpuMatrixR3/Pose.Pose")));
  Actor->FinishSpawning(FTransform::Identity);bool Good=Pose&&Pose->SourceHash==Card->SourceHash&&!Pose->Parts.IsEmpty();
  UTransformProviderData* Probe=nullptr;int32 ProbeTrack=0;
  const float Phase=Library->Animations[0].Sequence->GetPlayLength()*FMath::Frac(Next*.618034f);
  if(Good)for(const auto& Part:Pose->Parts){
   auto* Skin=NewObject<UInstancedSkinnedMeshComponent>(Actor);Actor->AddInstanceComponent(Skin);Skin->SetupAttachment(Actor->GetRootComponent());Skin->SetRelativeTransform(Part.Transform);Skin->SetCollisionEnabled(ECollisionEnabled::NoCollision);Skin->SetSkinnedAssetAndUpdate(Part.Mesh);
   auto* Provider=Part.Provider.Get();const int32 Track=0;
   if(!Provider){Good=false;break;}
   Skin->SetTransformProvider(Provider);Skin->SetAnimationMinScreenSize(0);Skin->AddInstance(FTransform::Identity,Track);Skin->RegisterComponent();
   Good=Skin->IsEnabled()&&Provider->IsEnabled()&&Provider->IsGpuOnly();if(!Good)break;
   if(!Probe){Probe=Provider;ProbeTrack=Track;}
  }
  if(!Good||!Probe){UE_LOG(LogTemp,Error,TEXT("Crowd GPU pose index=%d invalid or missing provider"),Next);Actor->Destroy();Actor=nullptr;}
  else{Actor->SetActorTickEnabled(false);GpuMotionProbes.Add(Probe);GpuProbeTracks.Add(ProbeTrack);MotionProbes.Add(nullptr);InitialAnimationTimes.Add(Phase);AnimationAdvanced.Add(false);}
 }else if(Actor){
  Actor->Character=Card;Actor->Outfit=0;Actor->AnimationLibrary=Library;Actor->PlaySkeletalAnimation=true;
  Actor->ShareBakedPose=!FParse::Param(FCommandLine::Get(),TEXT("CrowdIndependentPoses"));
  Actor->FinishSpawning(FTransform::Identity);
  if(!Actor->PlayImportedAnimation(0)){UE_LOG(LogTemp,Error,TEXT("Crowd animation index=%d failed"),Next);Actor->Destroy();Actor=nullptr;}
  else {TArray<USkeletalMeshComponent*> Skins;Actor->GetComponents(Skins);for(auto* Skin:Skins){Skin->PlayAnimation(Library->Animations[0].Sequence,true);Skin->SetPosition(Library->Animations[0].Sequence->GetPlayLength()*((Next*0.618034f)-FMath::FloorToFloat(Next*0.618034f)),false);}
   for(auto* Skin:Skins){Skin->bEnableUpdateRateOptimizations=false;if(LightweightGame)Skin->SetCastShadow(false);}
   if(Skins.IsEmpty()){Actor->Destroy();Actor=nullptr;}
   else{auto* Leader=Skins.FindByPredicate([](const auto* C){return !C->LeaderPoseComponent.IsValid();});
    if(!Leader){Actor->Destroy();Actor=nullptr;}
    else{MotionProbes.Add(*Leader);InitialAnimationTimes.Add((*Leader)->GetPosition());AnimationAdvanced.Add(false);Actor->SetActorTickEnabled(false);
     // This scene holds outfit and mouth visibility fixed. Invisible followers
     // have no render data to submit; the leader still evaluates private bones.
     for(auto* Skin:Skins)if(Skin->LeaderPoseComponent.IsValid()&&!Skin->IsVisible())Skin->SetComponentTickEnabled(false);
     if(QualityAnimationRates)for(auto* Skin:Skins){Skin->bEnableUpdateRateOptimizations=true;if(auto* Rates=Skin->AnimUpdateRateParams){Rates->bShouldUseLodMap=true;Rates->MaxEvalRateForInterpolation=5;Rates->LODToFrameSkipMap.Add(0,0);Rates->LODToFrameSkipMap.Add(1,1);Rates->LODToFrameSkipMap.Add(2,3);}}
    }}
  }
 }
 if(Actor&&QualityPilot&&FParse::Param(FCommandLine::Get(),TEXT("CrowdMouthBaselineDiagnostic"))){
  // Diagnostic only: distinguish a neutral-base mesh from a mesh whose mouth
  // baseline was already baked. Never apply this to the production crowd.
  TArray<USkeletalMeshComponent*> Skins;Actor->GetComponents(Skins);
  for(auto* Skin:Skins){const auto* Part=Card->Outfits[0].Parts.FindByPredicate([&](const auto& P){return P.SkinnedModel==Skin->GetSkeletalMeshAsset();});if(Part&&!Part->MouthPatterns.IsEmpty()&&!Part->MouthPatterns[0].Close.IsNone())Skin->SetMorphTarget(Part->MouthPatterns[0].Close,1.f);}
 }
 if(Actor){ConfigureLowFollowers(Actor,LightweightGame);if(LightweightGame&&WeightedSeedEnabled)ApplyWeightedPoseSeed(Actor);PrepareConstantScales(Actor);BindSectionLOD(Actor,LightweightGame);BindRenderUploads(Actor,LightweightGame);}
 if(Actor&&DeferResidentsDuringLoad){Actor->SetActorHiddenInGame(true);TArray<USkeletalMeshComponent*> Skins;Actor->GetComponents(Skins);for(auto* Skin:Skins)Skin->SetComponentTickEnabled(false);}
 if(Actor){Actor->Tags.Add(FName(*FString::Printf(TEXT("CrowdIndex_%03d"),Next)));Residents.Add(Actor);}
 else ++Failed;
 LoadMilliseconds.Add((FPlatformTime::Seconds()-LoadStarted)*1000.);
 CharacterBuildMilliseconds.Add((FPlatformTime::Seconds()-BuildStarted)*1000.);
 UE_LOG(LogTemp,Display,TEXT("Crowd load index=%d valid=%d ms=%.3f"),Next,Actor?1:0,LoadMilliseconds.Last());
 ++Next;
 // Submit one character per frame; do not commit all 100 renderers in one callback.
 GetWorldTimerManager().SetTimerForNextTick(this,&AOKCrowdScene::LoadNext);
}
void AOKCrowdScene::RestoreLoadingBudget(){
 if(!LoadingBudgetChanged)return;if(auto* Budget=IConsoleManager::Get().FindConsoleVariable(TEXT("s.AsyncLoadingTimeLimit")))Budget->Set(PreviousLoadingBudget,ECVF_SetByCode);if(auto* Full=IConsoleManager::Get().FindConsoleVariable(TEXT("s.AsyncLoadingUseFullTimeLimit")))Full->Set(PreviousFullLoadingBudget,ECVF_SetByCode);LoadingBudgetChanged=false;
}
void AOKCrowdScene::UpdateShadowPolicy(const FVector& Camera){
 TRACE_CPUPROFILER_EVENT_SCOPE(CrowdShadowPolicy);
 ++ShadowPolicyUpdates;ShadowActiveResidents=0;
 if(CacheShadowPolicy)ShadowBindings.SetNum(Residents.Num());
 for(int32 I=0;I<Residents.Num();++I){
  auto* Actor=Residents[I].Get();if(!IsValid(Actor))continue;
  const double DistanceSquared=FVector::DistSquared(Camera,Actor->GetActorLocation());
  const bool Eligible=!FocusOnlyShadows||I==DetailFocus;
  if(!CacheShadowPolicy){
   const bool Cast=Faces.Contains(I)||(Eligible&&DistanceSquared<FMath::Square(ShadowRadius));ShadowActiveResidents+=Cast?1:0;
   TArray<USkeletalMeshComponent*> Skins;Actor->GetComponents(Skins);
   for(auto* Skin:Skins){const bool Wanted=Cast&&(!Skin->ComponentHasTag(TEXT("HighDetailMain"))||Skin->ComponentHasTag(TEXT("NearHairShadow"))||Skin->ComponentHasTag(TEXT("NearFaceShadow")));if(Skin->CastShadow!=Wanted){++ShadowTransitions;Skin->SetCastShadow(Wanted);}}
   continue;
  }
  auto& Binding=ShadowBindings[I];const int32 ComponentCount=Actor->GetComponents().Num();
  // Component replacement/in-place reconstruction invalidates the weak cache.
  // Preserve this resident's hysteresis state across high/low actor swaps.
  if(Binding.Actor.Get()!=Actor||Binding.ComponentCount!=ComponentCount||Binding.Skins.ContainsByPredicate([](const auto& Skin){return !Skin.IsValid();})){
   Binding.Actor=Actor;Binding.ComponentCount=ComponentCount;Binding.Skins.Reset();
   TArray<USkeletalMeshComponent*> Skins;Actor->GetComponents(Skins);for(auto* Skin:Skins)Binding.Skins.Add(Skin);++ShadowCacheRebuilds;
  }
  const double Radius=Binding.Casting?ShadowRadius:ShadowRadius-ShadowHysteresisCm;
  Binding.Casting=Faces.Contains(I)||(Eligible&&DistanceSquared<FMath::Square(Radius));ShadowActiveResidents+=Binding.Casting?1:0;
  for(const auto& WeakSkin:Binding.Skins)if(auto* Skin=WeakSkin.Get()){
   const bool Wanted=Binding.Casting&&(!Skin->ComponentHasTag(TEXT("HighDetailMain"))||Skin->ComponentHasTag(TEXT("NearHairShadow"))||Skin->ComponentHasTag(TEXT("NearFaceShadow")));
   if(Skin->CastShadow!=Wanted){++ShadowTransitions;Skin->SetCastShadow(Wanted);}
  }
 }
}
void AOKCrowdScene::RestoreLowFace(int32 Index){
 auto* Binding=Faces.Find(Index);if(!Binding)return;
 for(const auto& Weak:Binding->Components)if(auto* Skin=Weak.Get())Skin->DestroyComponent();
 for(const auto& Weak:Binding->LowParts)if(auto* Skin=Weak.Get()){Skin->SetVisibility(true);Skin->bCastHiddenShadow=false;}
 if(auto* Skin=Binding->LowPacked.Get())if(auto* Mesh=Skin->GetSkeletalMeshAsset())if(auto* Data=Mesh->GetResourceForRendering())for(int32 S=0;S<Data->LODRenderData[0].RenderSections.Num();++S){const int32 M=Data->LODRenderData[0].RenderSections[S].MaterialIndex;if(Binding->Asset->LowPackedSlots.Contains(M))Skin->ShowMaterialSection(M,S,true,0);}
 // Destroying detail followers changes the leader's required-bone union.
 // Rebuild and evaluate before the restored low meshes upload their palette,
 // just as FinishFaceLOD does when adding detail followers.
 if(auto* Actor=Binding->Actor.Get()){TArray<USkeletalMeshComponent*> Skins;Actor->GetComponents(Skins);for(auto* Leader:Skins)if(!Leader->LeaderPoseComponent.IsValid()&&Leader->GetSkeletalMeshAsset()){Leader->bRequiredBonesUpToDate=false;Leader->RecalcRequiredBones(Leader->GetPredictedLODLevel());Leader->RefreshBoneTransforms(nullptr);if(auto* CrowdLeader=Cast<UOKCrowdRenderSkin>(Leader)){CrowdLeader->PreviousFinalizedPoseClip=CrowdLeader->FinalizedPoseClip;CrowdLeader->PreviousFinalizedPoseTime=CrowdLeader->FinalizedPoseTime;CrowdLeader->OnCrowdPoseFinalized.Broadcast(CrowdLeader);}Leader->ClearMotionVector();}for(auto* Skin:Skins)Skin->ClearMotionVector();}
 FaceAssets.Remove(Binding->Asset);if(Binding->Handle)Binding->Handle->ReleaseHandle();Faces.Remove(Index);++FaceRestores;UE_LOG(LogTemp,Display,TEXT("Crowd face LOD restored index=%d body=low"),Index);
}
void AOKCrowdScene::UpdateFaceLOD(const FVector& InputOrigin){
 FVector Origin=InputOrigin;if(FParse::Param(FCommandLine::Get(),TEXT("CrowdFaceLODSwitchTest"))){const double Elapsed=FPlatformTime::Seconds()-ReadyAt;if(Elapsed>=10&&Elapsed<20)Origin+=FVector(100000,0,0);}
 TArray<int32> Remove;for(const auto& Pair:Faces)if(!Pair.Value.Actor.IsValid()||FVector::DistSquared(Origin,Pair.Value.Actor->GetActorLocation())>FMath::Square(1800.))Remove.Add(Pair.Key);for(int32 I:Remove)RestoreLowFace(I);
 if(FacePendingIndex!=INDEX_NONE)return;
 int32 Best=INDEX_NONE;double Distance=FMath::Square(1600.);
 for(int32 I=0;I<Residents.Num();++I)if(!Faces.Contains(I)&&Residents[I]&&!Residents[I]->Tags.Contains(TEXT("FaceDetailUnavailable"))){const double D=FVector::DistSquared(Origin,Residents[I]->GetActorLocation());if(D<Distance){Distance=D;Best=I;}}
 if(Best==INDEX_NONE)return;
 const FString Base=FPaths::GetPath(FPaths::GetPath(Characters[Best].ToSoftObjectPath().GetLongPackageName())),Package=Base/(AssetVariant==TEXT("GameLowHairSafeR9")?TEXT("NearDetailR6/Face"):TEXT("NearDetailR5/Face"));
 if(!FPackageName::DoesPackageExist(Package)){Residents[Best]->Tags.Add(TEXT("FaceDetailUnavailable"));UE_LOG(LogTemp,Warning,TEXT("Crowd face detail missing index=%d"),Best);return;}
 FacePendingIndex=Best;FacePending=UAssetManager::GetStreamableManager().RequestAsyncLoad(FSoftObjectPath(Package+TEXT(".Face")),FStreamableDelegate::CreateUObject(this,&AOKCrowdScene::FinishFaceLOD));
}
void AOKCrowdScene::FinishFaceLOD(){
 const int32 I=FacePendingIndex;FacePendingIndex=INDEX_NONE;if(Stopping||!Residents.IsValidIndex(I)){FacePending.Reset();return;}
 auto* Actor=Residents[I].Get();const FString Base=FPaths::GetPath(FPaths::GetPath(Characters[I].ToSoftObjectPath().GetLongPackageName()));auto* Face=FindObject<UOKCrowdFaceDetail>(nullptr,*(Base/(AssetVariant==TEXT("GameLowHairSafeR9")?TEXT("NearDetailR6/Face.Face"):TEXT("NearDetailR5/Face.Face"))));
 if(!Face||!Face->Character||Face->SourceHash!=Actor->Character->SourceHash||Face->Character->Outfits.IsEmpty()){Actor->Tags.Add(TEXT("FaceDetailUnavailable"));FacePending.Reset();return;}
 TArray<USkeletalMeshComponent*> Old;Actor->GetComponents(Old);auto* LeaderPtr=Old.FindByPredicate([](const auto* S){return !S->LeaderPoseComponent.IsValid();});if(!LeaderPtr){FacePending.Reset();return;}auto* Leader=*LeaderPtr;const auto& Ref=Leader->GetSkeletalMeshAsset()->GetRefSkeleton();
 for(const auto& Part:Face->Character->Outfits[0].Parts){if(!Part.SkinnedModel){FacePending.Reset();return;}const auto& Other=Part.SkinnedModel->GetRefSkeleton();for(int32 B=0;B<Other.GetNum();++B){const int32 M=Ref.FindBoneIndex(Other.GetBoneName(B));if(M==INDEX_NONE||!(Ref.GetRefBonePose()[M].Equals(Other.GetRefBonePose()[B],1e-6)&&((Ref.GetParentIndex(M)==INDEX_NONE&&Other.GetParentIndex(B)==INDEX_NONE)||(Ref.GetParentIndex(M)!=INDEX_NONE&&Other.GetParentIndex(B)!=INDEX_NONE&&Ref.GetBoneName(Ref.GetParentIndex(M))==Other.GetBoneName(Other.GetParentIndex(B)))))){Actor->Tags.Add(TEXT("FaceDetailUnavailable"));UE_LOG(LogTemp,Error,TEXT("Crowd face bone mismatch index=%d bone=%s"),I,*Other.GetBoneName(B).ToString());FacePending.Reset();return;}}}
 FFaceBinding Binding;Binding.Actor=Actor;Binding.Asset=Face;Binding.Handle=FacePending;
 for(const auto& Part:Face->Character->Outfits[0].Parts){auto* Skin=NewObject<UOKCrowdRenderSkin>(Actor);Actor->AddInstanceComponent(Skin);Skin->SetupAttachment(Actor->GetRootComponent());Skin->ProtectFacialBounds=true;Skin->ComponentTags.Add(TEXT("HighDetailMain"));Skin->ComponentTags.Add(TEXT("HighFaceDetail"));if(Part.Role==TEXT("hair"))Skin->ComponentTags.Add(TEXT("NearHairShadow"));if(Part.Name==TEXT("HeadMesh"))Skin->ComponentTags.Add(TEXT("NearFaceShadow"));Skin->SetSkeletalMeshAsset(Part.SkinnedModel);Skin->SetRelativeTransform(Part.Transform);Skin->SetCollisionEnabled(ECollisionEnabled::NoCollision);Skin->SetCastShadow(false);Skin->SetForcedLOD(1);const FString Layer=Part.NativeRenderer.ToLower();if(Layer.Contains(TEXT("hitomi")))Skin->TranslucencySortPriority=Layer.Contains(TEXT("02"))?102:101;else if(Layer.Contains(TEXT("eyeline")))Skin->TranslucencySortPriority=Layer.Contains(TEXT("low"))?105:(Layer.EndsWith(TEXT("_kage"))?103:104);else if(Layer.Contains(TEXT("mayuge")))Skin->TranslucencySortPriority=106;else if(Layer.Contains(TEXT("noseline")))Skin->TranslucencySortPriority=107;Skin->RegisterComponent();Skin->SetLeaderPoseComponent(Leader,true,false);Skin->SetComponentTickEnabled(false);Binding.Components.Add(Skin);}
 for(auto* Skin:Old){const auto* Part=Actor->Character->Outfits[0].Parts.FindByPredicate([&](const auto& P){return P.SkinnedModel==Skin->GetSkeletalMeshAsset();});if(!Part)continue;if(Face->LowPartNames.Contains(Part->Name)){Binding.LowParts.Add(Skin);Skin->SetVisibility(false);Skin->bCastHiddenShadow=false;}if(Part->Name==TEXT("PackedVisibleParts")){Binding.LowPacked=Skin;const auto* Data=Part->SkinnedModel->GetResourceForRendering();for(int32 S=0;S<Data->LODRenderData[0].RenderSections.Num();++S){const int32 M=Data->LODRenderData[0].RenderSections[S].MaterialIndex;if(Face->LowPackedSlots.Contains(M))Skin->ShowMaterialSection(M,S,false,0);}}}
 Leader->bRequiredBonesUpToDate=false;Leader->RecalcRequiredBones(Leader->GetPredictedLODLevel());Leader->RefreshBoneTransforms(nullptr);if(auto* CrowdLeader=Cast<UOKCrowdRenderSkin>(Leader)){CrowdLeader->PreviousFinalizedPoseClip=CrowdLeader->FinalizedPoseClip;CrowdLeader->PreviousFinalizedPoseTime=CrowdLeader->FinalizedPoseTime;CrowdLeader->OnCrowdPoseFinalized.Broadcast(CrowdLeader);}Leader->ClearMotionVector();for(auto* Skin:Old)Skin->ClearMotionVector();for(const auto& Weak:Binding.Components)if(auto* Skin=Weak.Get())Skin->ClearMotionVector();FaceAssets.Add(Face);Faces.Add(I,MoveTemp(Binding));++FaceLoads;FacePending.Reset();if(auto* PC=GetWorld()->GetFirstPlayerController())if(PC->GetViewTarget())UpdateShadowPolicy(PC->GetViewTarget()->GetActorLocation());UE_LOG(LogTemp,Display,TEXT("Crowd face LOD loaded index=%d body=low"),I);
}
void AOKCrowdScene::UpdateDetail(double Now){
 if(!LightweightGame||ReadyAt<=0||Failed>0||Stopping||Now-DetailPollAt<.25)return;
 DetailPollAt=Now;auto* PC=GetWorld()->GetFirstPlayerController();if(!PC||!PC->GetViewTarget())return;
 const FVector Camera=PC->GetViewTarget()->GetActorLocation();
 const FVector DetailOrigin=PlayerControl&&Residents.IsValidIndex(CameraResident)?Residents[CameraResident]->GetActorLocation():Camera;
 if(FaceLOD&&!AssetVariant.StartsWith(TEXT("GameLowRoleBudget")))UpdateFaceLOD(DetailOrigin);
 // Every resident displaying the near face retains its complete shadow.
 UpdateShadowPolicy(Camera);
 if(!FParse::Param(FCommandLine::Get(),TEXT("CrowdFullOutlines")))for(const auto& Actor:Residents){if(Actor->Tags.Contains(FName(TEXT("HighDetailResident"))))continue;const bool Far=FVector::DistSquared(DetailOrigin,Actor->GetActorLocation())>FMath::Square(OutlineRadius);const FName Tag(TEXT("FarOutlineLOD"));if(Actor->Tags.Contains(Tag)==Far)continue;if(Far)Actor->Tags.Add(Tag);else Actor->Tags.Remove(Tag);TArray<USkeletalMeshComponent*> Skins;Actor->GetComponents(Skins);for(auto* Skin:Skins)if(!Skin->ComponentHasTag(TEXT("HighDetailMain")))if(auto* Mesh=Skin->GetSkeletalMeshAsset())if(const auto* Data=Mesh->GetResourceForRendering())for(int32 LOD=0;LOD<Data->LODRenderData.Num();++LOD){const auto& Sections=Data->LODRenderData[LOD].RenderSections;for(int32 Section=0;Section<Sections.Num();++Section){const int32 MaterialIndex=Sections[Section].MaterialIndex;auto* Material=Skin->GetMaterial(MaterialIndex);const bool Outline=Material&&Material->IsUsingWorldPositionOffset_Concurrent(GShaderPlatformForFeatureLevel[GetWorld()->GetFeatureLevel()]);if(Outline&&Skin->IsMaterialSectionShown(MaterialIndex,LOD)!=!Far)Skin->ShowMaterialSection(MaterialIndex,Section,!Far,LOD);}}}
 UpdateSectionLOD(PC);
 if(FaceLOD)return;
 if(DetailIndex!=INDEX_NONE||FParse::Param(FCommandLine::Get(),TEXT("CrowdDisableHighDetail")))return;
 // A completed load is not a request for high detail. Wait for camera movement.
 if(!DetailInteractionStarted)DetailInteractionStarted=FVector::DistSquared(Camera,EntryCameraLocation)>FMath::Square(25.f)||FParse::Param(FCommandLine::Get(),TEXT("CrowdDetailSwitchTest"));
 if(!DetailInteractionStarted)return;
 int32 Candidate=INDEX_NONE;bool Upgrade=false;float Best=FMath::Square(650.f);
 for(int32 I=0;I<Residents.Num();++I)if(HighDetail[I]&&((FocusedHighOnly&&I!=DetailFocus)||FVector::DistSquared(Camera,Residents[I]->GetActorLocation())>FMath::Square(900.f))){Candidate=I;break;}
 if(Candidate==INDEX_NONE&&HighDetail.FilterByPredicate([](bool High){return High;}).Num()<(FocusedHighOnly?1:2))for(int32 I=0;I<Residents.Num();++I)if(!HighDetail[I]&&(!FocusedHighOnly||I==DetailFocus)){const float D=FVector::DistSquared(Camera,Residents[I]->GetActorLocation());if(D<Best){Best=D;Candidate=I;Upgrade=true;}}
 if(Candidate==INDEX_NONE)return;
 DetailIndex=Candidate;DetailRequestedHigh=Upgrade;
 const auto& Card=Upgrade?HighCharacters[Candidate]:Characters[Candidate];const auto& Walk=Upgrade?HighWalks[Candidate]:Walks[Candidate];
 DetailPending=UAssetManager::GetStreamableManager().RequestAsyncLoad({Card.ToSoftObjectPath(),Walk.ToSoftObjectPath()},FStreamableDelegate::CreateUObject(this,&AOKCrowdScene::FinishDetail));
}
void AOKCrowdScene::FinishDetail(){
 if(Stopping||!Residents.IsValidIndex(DetailIndex))return;
 const int32 I=DetailIndex;auto* Old=Residents[I].Get();auto* Card=(DetailRequestedHigh?HighCharacters[I]:Characters[I]).Get();auto* Walk=(DetailRequestedHigh?HighWalks[I]:Walks[I]).Get();
 if(!Old||!Card||!Walk||Walk->Animations.IsEmpty()||!Walk->Animations[0].Sequence||Card->SourceHash!=Old->Character->SourceHash){UE_LOG(LogTemp,Error,TEXT("Crowd detail invalid card index=%d"),I);DetailIndex=INDEX_NONE;DetailPending.Reset();return;}
 const float Phase=MotionProbes[I]?MotionProbes[I]->GetPosition():0.f;
 UClass* ActorClass=(AnimationHz==60||(CullRenderUploads&&!DetailRequestedHigh&&Card->Schema==1005))?AOKCrowdRenderActor::StaticClass():AKKImportedCharacterActor::StaticClass();
 auto* Replacement=GetWorld()->SpawnActorDeferred<AKKImportedCharacterActor>(ActorClass,Old->GetActorTransform());
 Replacement->Character=Card;Replacement->AnimationLibrary=Walk;Replacement->Outfit=0;Replacement->PlaySkeletalAnimation=true;Replacement->ShareBakedPose=true;Replacement->FinishSpawning(Old->GetActorTransform());
 TArray<USkeletalMeshComponent*> Skins;Replacement->GetComponents(Skins);auto* Leader=Skins.FindByPredicate([](const auto* Skin){return !Skin->LeaderPoseComponent.IsValid();});
 if(!Leader||!Replacement->PlayImportedAnimation(0)||!Replacement->SetAnimationPosition(FMath::Fmod(Phase,Walk->Animations[0].Sequence->GetPlayLength()))){Replacement->Destroy();UE_LOG(LogTemp,Error,TEXT("Crowd detail pose failed index=%d"),I);DetailIndex=INDEX_NONE;DetailPending.Reset();return;}
 DetailPhaseError=FMath::Max(DetailPhaseError,FMath::Abs((*Leader)->GetPosition()-FMath::Fmod(Phase,Walk->Animations[0].Sequence->GetPlayLength())));
 Replacement->SetActorTickEnabled(false);Replacement->Tags=Old->Tags;Replacement->Tags.Remove(FName(TEXT("FarOutlineLOD")));
 if(DetailRequestedHigh)Replacement->Tags.AddUnique(FName(TEXT("HighDetailResident")));else Replacement->Tags.Remove(FName(TEXT("HighDetailResident")));
 const bool CompatibleShadowRig=AssetVariant!=TEXT("GameLowSRTR1");
 for(auto* Skin:Skins){Skin->bEnableUpdateRateOptimizations=false;if(Skin->LeaderPoseComponent.IsValid()&&!Skin->IsVisible())Skin->SetComponentTickEnabled(false);if(DetailRequestedHigh&&CompatibleShadowRig){Skin->ComponentTags.Add(TEXT("HighDetailMain"));Skin->SetCastShadow(false);}}
 ApplyBalancedPosePhase(Replacement,I);ConfigureLowFollowers(Replacement,LightweightGame&&!DetailRequestedHigh);if(LightweightGame&&!DetailRequestedHigh&&WeightedSeedEnabled)ApplyWeightedPoseSeed(Replacement);
 PrepareConstantScales(Replacement);
 if(DetailRequestedHigh&&CompatibleShadowRig&&Characters[I].Get()){
  // The low mesh already exists. Reuse its private bone mapping for shadows,
  // rather than evaluate a second animation or load another proxy asset.
  for(const auto& Part:Characters[I].Get()->Outfits[0].Parts)if(Part.SkinnedModel&&Part.MouthDefaultVisible){
   auto* Proxy=NewObject<USkeletalMeshComponent>(Replacement);Replacement->AddInstanceComponent(Proxy);Proxy->SetupAttachment(Replacement->GetRootComponent());Proxy->SetRelativeTransform(Part.Transform);Proxy->SetSkeletalMeshAsset(Part.SkinnedModel);Proxy->SetCollisionEnabled(ECollisionEnabled::NoCollision);Proxy->bRenderInMainPass=false;Proxy->bRenderInDepthPass=false;Proxy->SetLeaderPoseComponent(*Leader);Proxy->RegisterComponent();Proxy->SetComponentTickEnabled(false);
  }
 }
 Residents[I]=Replacement;MotionProbes[I]=*Leader;HighDetail[I]=DetailRequestedHigh;Old->Destroy();++DetailSwitches;
 BindSectionLOD(Replacement,LightweightGame&&!DetailRequestedHigh);
 BindRenderUploads(Replacement,LightweightGame&&!DetailRequestedHigh);
 UE_LOG(LogTemp,Display,TEXT("Crowd detail switch index=%d high=%d phase=%.3f"),I,DetailRequestedHigh?1:0,Phase);
 DetailIndex=INDEX_NONE;DetailPending.Reset();
}
void AOKCrowdScene::PollPerformanceControl(double Now){
 if(PerformanceControl.IsEmpty()||ReadyAt<=0||Next!=Characters.Num()||Now-ControlPollAt<1)return;
 ControlPollAt=Now;FString Text;if(!FFileHelper::LoadFileToString(Text,*PerformanceControl)||Text.Len()>4096)return;
 TSharedPtr<FJsonObject> Request;if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Request)||!Request)return;
 int32 Revision=0;if(!Request->TryGetNumberField(TEXT("revision"),Revision)||Revision<=ControlRevision)return;
 bool Exit=false;Request->TryGetBoolField(TEXT("exit"),Exit);if(Exit&&ControlledFinished){SaveReport();Stopping=true;FPlatformMisc::RequestExit(false);return;}
 FString Tag;if(!Request->TryGetStringField(TEXT("tag"),Tag)||Tag.IsEmpty()||Tag.Len()>64)return;
 for(TCHAR C:Tag)if(!FChar::IsAlnum(C)&&C!=TEXT('_')&&C!=TEXT('-'))return;
 const FString Base=FPaths::GetPath(PerformanceControl)/Tag;
 if(IFileManager::Get().FileExists(*(Base+TEXT(".json")))){UE_LOG(LogTemp,Error,TEXT("Crowd control requires fresh tag %s"),*Tag);ControlRevision=Revision;return;}
 double Duration=60;Request->TryGetNumberField(TEXT("seconds"),Duration);if(Duration<30||Duration>600)return;
 int32 EndMode=1,Width=8,RequestedPackingMode=-1,MinDraws=-1,CopyDebugPose=-1,ComponentMatrixCache=-1;bool Shadows=false,Trace=false,ResetLayout=false,VerifyComponentMatrices=false;
 Request->TryGetNumberField(TEXT("end_frame_mode"),EndMode);Request->TryGetNumberField(TEXT("rhi_width"),Width);
 Request->TryGetBoolField(TEXT("parallel_shadows"),Shadows);Request->TryGetBoolField(TEXT("trace"),Trace);Request->TryGetNumberField(TEXT("bone_packing_mode"),RequestedPackingMode);Request->TryGetBoolField(TEXT("reset_layout"),ResetLayout);
 Request->TryGetNumberField(TEXT("min_draws"),MinDraws);
 Request->TryGetNumberField(TEXT("copy_debug_pose"),CopyDebugPose);
 int32 ConstantKeyISPC=-1;Request->TryGetNumberField(TEXT("constant_key_lerp_ispc"),ConstantKeyISPC);if(ConstantKeyISPC < -1||ConstantKeyISPC>1)return;
 int32 ConstantScaleMode=-1;bool VerifyConstantScale=false;Request->TryGetNumberField(TEXT("constant_scale_cache"),ConstantScaleMode);Request->TryGetBoolField(TEXT("verify_constant_scales"),VerifyConstantScale);if(ConstantScaleMode < -1||ConstantScaleMode>1)return;
 auto* ConstantScaleVariable=IConsoleManager::Get().FindConsoleVariable(TEXT("r.Anim.ConstantScaleCache"));auto* VerifyScaleVariable=IConsoleManager::Get().FindConsoleVariable(TEXT("r.Anim.VerifyConstantScaleCache"));
 if(ConstantScaleMode>=0&&(!ConstantScaleVariable||!VerifyScaleVariable||ConstantScalePrepared<Residents.Num()))return;
 auto* ConstantKeyVariable=IConsoleManager::Get().FindConsoleVariable(TEXT("a.ConstantKeyLerp.ISPC"));
 if(ConstantKeyISPC>=0&&!ConstantKeyVariable){UE_LOG(LogTemp,Error,TEXT("Crowd constant-key decoder experiment unavailable in this Engine build"));return;}
 if(CopyDebugPose < -1 || CopyDebugPose > 1)return;
 auto* CopyDebugVariable=IConsoleManager::Get().FindConsoleVariable(TEXT("r.SkeletalMesh.CopyDebugPose"));
 if(CopyDebugPose>=0&&!CopyDebugVariable){UE_LOG(LogTemp,Error,TEXT("Crowd debug-pose experiment needs the matching Engine patch"));return;}
 Request->TryGetNumberField(TEXT("component_matrix_cache"),ComponentMatrixCache);Request->TryGetBoolField(TEXT("verify_component_matrices"),VerifyComponentMatrices);
 if(ComponentMatrixCache < -1 || ComponentMatrixCache > 2)return;
 auto* MatrixCacheVariable=IConsoleManager::Get().FindConsoleVariable(TEXT("r.SkeletalMesh.ReuseComponentMatrices"));auto* VerifyMatrixVariable=IConsoleManager::Get().FindConsoleVariable(TEXT("r.SkeletalMesh.VerifyComponentMatrices"));
 if(ComponentMatrixCache>=0&&(!MatrixCacheVariable||!VerifyMatrixVariable)){UE_LOG(LogTemp,Error,TEXT("Crowd matrix cache needs matching Engine patch"));return;}
 int32 FullRefCache=-1;bool VerifyFullRef=false;Request->TryGetNumberField(TEXT("full_ref_matrix_cache"),FullRefCache);Request->TryGetBoolField(TEXT("verify_full_ref_matrices"),VerifyFullRef);
 if(FullRefCache < -1||FullRefCache>1)return;
 auto* FullRefVariable=IConsoleManager::Get().FindConsoleVariable(TEXT("r.SkeletalMesh.ReuseRefToLocalMatrices"));auto* FullRefVerifyVariable=IConsoleManager::Get().FindConsoleVariable(TEXT("r.SkeletalMesh.VerifyRefToLocalMatrices"));
 if(FullRefCache>=0&&(!FullRefVariable||!FullRefVerifyVariable))return;
 if(EndMode<0||EndMode>2||Width<1||Width>32||RequestedPackingMode < -1||RequestedPackingMode>5||(Trace&&Duration<45))return;
 int32 EligibilityPass=-1;Request->TryGetNumberField(TEXT("gpu_clock_eligibility_pass"),EligibilityPass);if(EligibilityPass < -1||EligibilityPass>1)return;
 auto* EligibilityVariable=IConsoleManager::Get().FindConsoleVariable(TEXT("ok.CrowdGpuEligibilityPass"));if(EligibilityPass>=0&&!EligibilityVariable)return;
 int32 WeightedSeedMode=-1;Request->TryGetNumberField(TEXT("weighted_pose_seed"),WeightedSeedMode);if(WeightedSeedMode < -1||WeightedSeedMode>1)return;
 if(WeightedSeedMode>=0){
#if !WITH_EDITOR && OK_CROWD_WEIGHTED_POSE_SEED
  FString Manifest;if(!FParse::Value(FCommandLine::Get(),TEXT("CrowdWeightedPoseSeed="),Manifest))return;
#else
  return;
#endif
 }
 if(MinDraws!=-1&&MinDraws!=64&&MinDraws!=128&&MinDraws!=256&&MinDraws!=512)return;
 if(RequestedPackingMode>=0&&!OKGetCrowdBonePackingStats().Installed){UE_LOG(LogTemp,Error,TEXT("Crowd control needs installed bone provider"));return;}
 auto* ShadowVariable=IConsoleManager::Get().FindConsoleVariable(TEXT("r.Shadow.Virtual.NonNanite.ParallelDraw"));
 if(Shadows&&!ShadowVariable){UE_LOG(LogTemp,Error,TEXT("Crowd control needs private parallel Renderer"));return;}
 SaveReport();if(ProfileStarted>0&&!ProfileTaken)FTraceAuxiliary::Stop();
 bool IndependentGpuRequest=false;
 if(Request->TryGetBoolField(TEXT("independent_gpu_animation"),IndependentGpuRequest))SetIndependentGpuAnimationEnabled(IndependentGpuRequest);
 if(OwnNamedEvents){GCycleStatsShouldEmitNamedEvents=PreviousNamedEvents;OwnNamedEvents=false;}
 IConsoleManager::Get().FindConsoleVariable((WITH_EDITOR ? TEXT("AllowAsyncRenderThreadUpdatesEditorGameWorld") : TEXT("AllowAsyncRenderThreadUpdates")))->Set(EndMode,ECVF_SetByConsole);
 IConsoleManager::Get().FindConsoleVariable(TEXT("r.RHICmdWidth"))->Set(Width,ECVF_SetByConsole);
 if(MinDraws>=0)IConsoleManager::Get().FindConsoleVariable(TEXT("r.RHICmdMinDrawsPerParallelCmdList"))->Set(MinDraws,ECVF_SetByConsole);
 if(ShadowVariable)ShadowVariable->Set(Shadows?1:0,ECVF_SetByConsole);
 if(RequestedPackingMode>=0)IConsoleManager::Get().FindConsoleVariable(TEXT("r.OKCrowd.BonePackingMode"))->Set(RequestedPackingMode,ECVF_SetByConsole);
 if(CopyDebugPose>=0)CopyDebugVariable->Set(CopyDebugPose,ECVF_SetByConsole);
 if(ConstantKeyISPC>=0)ConstantKeyVariable->Set(ConstantKeyISPC,ECVF_SetByConsole);
 if(ConstantScaleMode>=0){ConstantScaleVariable->Set(ConstantScaleMode,ECVF_SetByConsole);VerifyScaleVariable->Set(VerifyConstantScale?1:0,ECVF_SetByConsole);}
 if(FullRefCache>=0){FullRefVariable->Set(FullRefCache,ECVF_SetByConsole);FullRefVerifyVariable->Set(VerifyFullRef?1:0,ECVF_SetByConsole);}
 if(ComponentMatrixCache>=0){MatrixCacheVariable->Set(ComponentMatrixCache,ECVF_SetByConsole);VerifyMatrixVariable->Set(VerifyComponentMatrices?1:0,ECVF_SetByConsole);}
 if(EligibilityPass>=0)EligibilityVariable->Set(EligibilityPass,ECVF_SetByConsole);
 if(WeightedSeedMode>=0){
#if !WITH_EDITOR && OK_CROWD_WEIGHTED_POSE_SEED
  WeightedSeedEnabled=WeightedSeedMode!=0;
  for(int32 I=0;I<Residents.Num();++I){if(HighDetail.IsValidIndex(I)&&HighDetail[I])continue;if(WeightedSeedEnabled)ApplyWeightedPoseSeed(Residents[I]);else{TArray<USkeletalMeshComponent*> Skins;Residents[I]->GetComponents(Skins);for(auto* Skin:Skins)if(!Skin->LeaderPoseComponent.IsValid()){SetSkeletalWeightedPoseSeed(Skin,nullptr);Skin->RefreshBoneTransforms();}}}
#endif
 }
 if(ResetLayout){Behaviors.Reset();BehaviorActors.Reset();TravelTime=0;PlayerMovementInitialized=false;for(int32 I=0;I<MotionProbes.Num();++I)if(MotionProbes[I]&&InitialAnimationTimes.IsValidIndex(I))MotionProbes[I]->SetPosition(InitialAnimationTimes[I],false);}
 ControlRevision=Revision;ControlledTag=Tag;ControlledReport=Base+TEXT(".json");ControlledScreenshot=Base+TEXT(".png");
 IndependentGpuAnimationChangedDuringSamples=false;
 ControlledTrace=Trace?Base+TEXT(".utrace"):FString();ControlledDuration=Duration;ControlledFinished=false;ControlledReportWritten=false;
 Frames.Reset();GameFrames.Reset();RenderFrames.Reset();RHIFrames.Reset();GPUFrames.Reset();SampleCursor=0;SamplesSeen=0;FrameSeries.Reset();FrameSeenLeaders.Reset();FrameSeenEvaluations.Reset();FrameSeriesWritten=false;SampleCapacity=FMath::Clamp(FMath::CeilToInt(Duration*1000),3600,600000);ReadyAt=Now;LastWall=Now;LastReport=Now;
 ScreenshotTaken=false;ProfileStarted=0;ProfileTaken=false;
 UE_LOG(LogTemp,Display,TEXT("Crowd control revision=%d tag=%s end_frame=%d parallel_shadows=%d width=%d trace=%d seconds=%.0f"),Revision,*Tag,EndMode,Shadows,Width,Trace,Duration);
 SaveReport();
}
void AOKCrowdScene::UpdateBehaviors(float DeltaSeconds){
 if(!CrowdBehaviors||ReadyAt<=0||QualityPilot||GpuPose)return;
 if(Behaviors.Num()!=Residents.Num()){
  Behaviors.SetNum(Residents.Num());BehaviorActors.SetNum(Residents.Num());
  for(int32 I=0;I<Behaviors.Num();++I)Behaviors[I].Initialize(I);
 }
 for(int32 I=0;I<Residents.Num();++I){
  if(PlayerControl&&I==CameraResident)continue;
  auto* Actor=Residents[I].Get();if(!Actor||!MotionProbes.IsValidIndex(I)||!MotionProbes[I])continue;
  auto& B=Behaviors[I];const bool Changed=B.Step(DeltaSeconds);
  if(Changed||BehaviorActors[I].Get()!=Actor){
   BehaviorActors[I]=Actor;int32 Selected=B.State==FOKCrowdBehavior::EState::Walk?0:Actor->AnimationIndex;TArray<int32> Candidates;B.IdleClipPlaying=false;
   if(auto* Library=Actor->AnimationLibrary.Get()){
    for(int32 Clip=0;Clip<Library->Animations.Num();++Clip){const auto& Entry=Library->Animations[Clip];
     if(!Entry.Sequence||Entry.Outfit!=Actor->Outfit)continue;
     if(IndependentGpuAnimationRequested&&!OKCrowdGpuHasClip(IndependentGpuRuntime,Actor,Entry.Sequence))continue;
     const FString Name=(Entry.Clip+TEXT(" ")+Entry.Sequence->GetName()).ToLower();
     const bool Walk=Name.Contains(TEXT("walk"))||Name.Contains(TEXT("aruki")), Idle=Name.Contains(TEXT("idle"))||Name.Contains(TEXT("stand"))||Name.Contains(TEXT("tachi"));
     if(B.State==FOKCrowdBehavior::EState::Walk?Walk:Idle)Candidates.Add(Clip);
    }
    if(!Candidates.IsEmpty()){Selected=Candidates[B.Random.RandRange(0,Candidates.Num()-1)];B.IdleClipPlaying=B.State!=FOKCrowdBehavior::EState::Walk;}
    else if(B.State!=FOKCrowdBehavior::EState::Walk)++BehaviorMissingIdle;
    if(B.State==FOKCrowdBehavior::EState::Walk){B.PendingClip=INDEX_NONE;if(Actor->AnimationIndex!=Selected){if(Actor->PlayImportedAnimation(Selected))++BehaviorClipSwitches;}}
    else {B.PendingClip=B.IdleClipPlaying?Selected:INDEX_NONE;B.IdleClipPlaying=false;}
   }
  }
  if(B.PendingClip!=INDEX_NONE&&FMath::IsNearlyZero(B.Speed)){
   const bool ChangedClip=Actor->AnimationIndex!=B.PendingClip;
   if(!ChangedClip||Actor->PlayImportedAnimation(B.PendingClip)){if(ChangedClip)++BehaviorClipSwitches;B.IdleClipPlaying=true;}
   B.PendingClip=INDEX_NONE;
  }
  auto* Leader=MotionProbes[I].Get();auto* Instance=Leader->GetSingleNodeInstance();if(!Instance)continue;
  if(B.IdleClipPlaying&&!Instance->IsPlaying()){B.Remaining=0;}
  // An actual idle clip keeps playing; a walk-only bank explicitly pauses.
  const float Rate=B.IdleClipPlaying?1.f:B.Speed;
  if(!FMath::IsNearlyEqual(Instance->GetPlayRate(),Rate,1e-5f)){
   TInlineComponentArray<USkeletalMeshComponent*> Skins(Actor);
   for(auto* Skin:Skins)if(auto* Node=Skin->GetSingleNodeInstance())Node->SetPlayRate(Rate);
  }
 }
}

void AOKCrowdScene::Tick(float DeltaSeconds){
 TRACE_CPUPROFILER_EVENT_SCOPE(OKCrowdScene_UpdateResidents);
 Super::Tick(DeltaSeconds);
 if(auto* PC=GetWorld()->GetFirstPlayerController()){
  if(PC->WasInputKeyJustPressed(EKeys::F8))SetIndependentGpuAnimationEnabled(IndependentGpuAnimationSwitch.GetValueOnGameThread()!=1);
  if(PC->WasInputKeyJustPressed(EKeys::F9))ShowIndependentGpuAnimationStatus();
 }
 PollIndependentGpuAnimationControl();
 const bool ClockComparison=FParse::Param(FCommandLine::Get(),TEXT("CrowdGpuClockVisualCompare"));
 const bool VisualComparison=ClockComparison||FParse::Param(FCommandLine::Get(),TEXT("CrowdIndependentGpuVisualCompare"));
 if(ReadyAt>0&&!VisualComparison)TravelTime+=DeltaSeconds;
 if(ReadyAt>0&&VisualComparison){
  float Phase=.37f;FParse::Value(FCommandLine::Get(),TEXT("CrowdVisualPhase="),Phase);Phase=FMath::Clamp(Phase,0.f,.9999f);
  for(int32 Index=0;Index<MotionProbes.Num();++Index)if(auto* Leader=MotionProbes[Index].Get()){
   Leader->SetPlayRate(0);if(!VisualComparisonFrozen&&Residents.IsValidIndex(Index)&&Residents[Index]->AnimationLibrary&&!Residents[Index]->AnimationLibrary->Animations.IsEmpty())if(auto* Clip=Residents[Index]->AnimationLibrary->Animations[0].Sequence.Get()){Leader->SetPosition(Clip->GetPlayLength()*Phase,false);Leader->RefreshBoneTransforms();}
  }VisualComparisonFrozen=true;
 }
 if(!VisualComparison)UpdateBehaviors(DeltaSeconds);
 if(ReadyAt>0)OKTickCrowdIndependentGpu(IndependentGpuRuntime,IndependentGpuAnimationRequested,Residents,MotionProbes);
 for(int32 I=0;I<MotionProbes.Num();++I)if(!AnimationAdvanced[I]&&IsValid(MotionProbes[I])&&!FMath::IsNearlyEqual(MotionProbes[I]->GetPosition(),InitialAnimationTimes[I],.001f))AnimationAdvanced[I]=true;
 if(GpuPose)for(int32 I=0;I<GpuMotionProbes.Num();++I)if(IsValid(GpuMotionProbes[I])&&GpuMotionProbes[I]->IsEnabled()&&TravelTime>.1)AnimationAdvanced[I]=true;
 if(QualityAnimationRates)for(const auto& Leader:MotionProbes)if(IsValid(Leader.Get())&&Leader->AnimUpdateRateParams){auto* Rates=Leader->AnimUpdateRateParams;const int32 LOD=Leader->GetPredictedLODLevel();const int32 Skip=LOD>=2?3:LOD==1?1:0;for(int32 Level=0;Level<3;++Level)Rates->LODToFrameSkipMap.Add(Level,Skip);}
 if(!DeferResidentsDuringLoad||ReadyAt>0)for(const auto& Actor:Residents){
  if(!IsValid(Actor))continue;
  const auto* IndexTag=Actor->Tags.FindByPredicate([](FName Tag){return Tag.ToString().StartsWith(TEXT("CrowdIndex_"));});if(!IndexTag)continue;const int32 Index=FCString::Atoi(*IndexTag->ToString().Right(3));
  if(PlayerControl&&ReadyAt>0&&Index==CameraResident)continue;
  const bool HasBehavior=CrowdBehaviors&&!VisualComparison&&Behaviors.IsValidIndex(Index);
  const double RouteTime=HasBehavior?Behaviors[Index].RouteTime:TravelTime;
  FRotator Facing=OKCrowdLayout::Rotation(Index,RouteTime);
  if(HasBehavior)Facing.Yaw+=Behaviors[Index].LookYaw();
  Actor->SetActorLocationAndRotation(QualityPilot?OKCrowdLayout::Center(0):OKCrowdLayout::Position(Index,RouteTime),QualityPilot?FRotator::ZeroRotator:Facing);
 }
 const double PreviousReportMilliseconds=PendingReportMilliseconds;PendingReportMilliseconds=0;
 const double Now=FPlatformTime::Seconds();
 UpdatePlayerMovement(DeltaSeconds);
 UpdateThirdPersonCamera();
 if(QualityPilot&&ReadyAt>0&&Residents.Num()==1&&!Residents[0]->Tags.Contains(TEXT("MaterialIsolationApplied"))){FString Slots;if(FParse::Value(FCommandLine::Get(),TEXT("CrowdPilotHideSlots="),Slots)){Residents[0]->Tags.Add(TEXT("MaterialIsolationApplied"));TArray<FString> Values;Slots.ParseIntoArray(Values,TEXT(","));TArray<USkeletalMeshComponent*> Skins;Residents[0]->GetComponents(Skins);for(auto* Skin:Skins)if(auto* Mesh=Skin->GetSkeletalMeshAsset())if(const auto* Part=Residents[0]->Character->Outfits[0].Parts.FindByPredicate([&](const auto& P){return P.SkinnedModel==Mesh;}))if(Part->Name==TEXT("PackedVisibleParts"))for(int32 S=0;S<Mesh->GetResourceForRendering()->LODRenderData[0].RenderSections.Num();++S){const int32 M=Mesh->GetResourceForRendering()->LODRenderData[0].RenderSections[S].MaterialIndex;if(Values.Contains(FString::FromInt(M)))Skin->ShowMaterialSection(M,S,false,0);}UE_LOG(LogTemp,Display,TEXT("Crowd diagnostic hidden slots=%s"),*Slots);}}
 if(QualityPilot&&ReadyAt>0&&SectionLODPixels>0&&FParse::Param(FCommandLine::Get(),TEXT("CrowdSectionLODSwitchTest"))){if(auto* PC=GetWorld()->GetFirstPlayerController())if(auto* Camera=Cast<ACameraActor>(PC->GetViewTarget())){const FVector Target=Residents[0]->GetActorLocation()+FVector(0,0,85);const bool Far=Now-ReadyAt<15;const FVector Location=Target+(Far?FVector(0,-4300,3400):FVector(260,-420,40));Camera->SetActorLocationAndRotation(Location,(Target-Location).Rotation());}}
 if(QualityPilot&&ReadyAt>0&&Residents.Num()==1&&FParse::Param(FCommandLine::Get(),TEXT("CrowdPilotHighFaceDiagnostic"))&&!Residents[0]->Tags.Contains(TEXT("HighFaceDiagnosticApplied"))){
  // Diagnostic only: isolate eye-hole reduction from material/bounds failures.
  auto* Actor=Residents[0].Get();auto* Card=Actor->Character.Get();Actor->Tags.Add(TEXT("HighFaceDiagnosticApplied"));
  auto* High=HighCharacters[0].LoadSynchronous();
  const auto* Head=High&&!High->Outfits.IsEmpty()?High->Outfits[0].Parts.FindByPredicate([](const auto& P){return P.Name==TEXT("HeadMesh");}):nullptr;
  TArray<USkeletalMeshComponent*> Skins;Actor->GetComponents(Skins);
  for(auto* Skin:Skins){const auto* Part=Card->Outfits[0].Parts.FindByPredicate([&](const auto& P){return P.Name==TEXT("HeadMesh")&&P.SkinnedModel==Skin->GetSkeletalMeshAsset();});if(Part&&Head){Skin->SetSkeletalMeshAsset(Head->SkinnedModel);UE_LOG(LogTemp,Display,TEXT("Crowd diagnostic substituted high HeadMesh only"));}}
  UE_LOG(LogTemp,Display,TEXT("Crowd diagnostic high face sharing valid=%d"),Actor->ConfigureBakedPoseSharing());
 }
 if(QualityPilot&&ReadyAt>0&&Residents.Num()==1&&FParse::Param(FCommandLine::Get(),TEXT("CrowdPilotFrontCamera"))){if(auto* PC=GetWorld()->GetFirstPlayerController())if(auto* Camera=Cast<ACameraActor>(PC->GetViewTarget())){float Distance=180;FParse::Value(FCommandLine::Get(),TEXT("CrowdPilotFaceDistance="),Distance);Distance=FMath::Clamp(Distance,-2000.f,2000.f);float TargetHeight=145,ViewYaw=0;FParse::Value(FCommandLine::Get(),TEXT("CrowdPilotTargetHeight="),TargetHeight);FParse::Value(FCommandLine::Get(),TEXT("CrowdPilotViewYaw="),ViewYaw);const FVector Target=Residents[0]->GetActorLocation()+FVector(0,0,TargetHeight),Location=Target+Residents[0]->GetActorRotation().RotateVector(FRotator(0,ViewYaw,0).RotateVector(FVector(0,Distance,0)));Camera->SetActorLocationAndRotation(Location,(Target-Location).Rotation());Camera->GetCameraComponent()->SetFieldOfView(50);}}
 if(QualityPilot&&ReadyAt>0&&FParse::Param(FCommandLine::Get(),TEXT("CrowdDetailSwitchTest"))){if(auto* PC=GetWorld()->GetFirstPlayerController())if(auto* Camera=Cast<ACameraActor>(PC->GetViewTarget())){const FVector Target=Residents[0]->GetActorLocation()+FVector(0,0,85);const bool Far=DetailSwitches>=1&&Now-ReadyAt>12;const FVector Location=Target+(Far?FVector(750,-1250,250):FVector(260,-420,40));Camera->SetActorLocationAndRotation(Location,(Target-Location).Rotation());}}
 if(QualityPilot){int32 ForcedLOD=0;if(FParse::Value(FCommandLine::Get(),TEXT("CrowdPilotLOD="),ForcedLOD))for(const auto& Actor:Residents){TArray<USkeletalMeshComponent*> Skins;Actor->GetComponents(Skins);for(auto* Skin:Skins)Skin->SetForcedLOD(FMath::Clamp(ForcedLOD+1,1,3));}}
 UpdateDetail(Now);
 UpdateRenderUploads();
 PollPerformanceControl(Now);
 FString TraceFile;FParse::Value(FCommandLine::Get(),TEXT("CrowdTrace="),TraceFile);
 if(ControlRevision>0)TraceFile=ControlledTrace;
 // Trace initialization can stall for tens of milliseconds. Start during
 // warm-up; the measured frame window still begins at ready + 10 seconds.
 if(ReadyAt>0&&Now-ReadyAt>5&&!ProfileTaken&&!TraceFile.IsEmpty()){
  if(ProfileStarted==0){FTraceAuxiliary::FOptions Options;Options.bExcludeTail=true;
   if(ControlRevision>0){PreviousNamedEvents=GCycleStatsShouldEmitNamedEvents;GCycleStatsShouldEmitNamedEvents=1;OwnNamedEvents=true;}
   if(FTraceAuxiliary::Start(FTraceAuxiliary::EConnectionType::File,*TraceFile,TEXT("cpu,gpu,frame,bookmark"),&Options)){ProfileStarted=Now;TraceAnchorReadySeconds=FPlatformTime::Seconds()-ReadyAt;TRACE_CPUPROFILER_EVENT_SCOPE(CrowdWarmTraceAnchor);}
   else{ProfileTaken=true;if(OwnNamedEvents){GCycleStatsShouldEmitNamedEvents=PreviousNamedEvents;OwnNamedEvents=false;}UE_LOG(LogTemp,Error,TEXT("Crowd warm trace could not start %s"),*TraceFile);}}
  else if(Now-ProfileStarted>TraceDurationSeconds){FTraceAuxiliary::Stop();ProfileTaken=true;if(OwnNamedEvents){GCycleStatsShouldEmitNamedEvents=PreviousNamedEvents;OwnNamedEvents=false;}UE_LOG(LogTemp,Display,TEXT("Crowd warm CPU trace complete %s"),*TraceFile);}
 }
 if(!ControlledFinished&&ReadyAt>0&&Now-ReadyAt>10){
  const double Frame=(Now-LastWall)*1000.,Game=FPlatformTime::ToMilliseconds(GGameThreadTime),Render=FPlatformTime::ToMilliseconds(GRenderThreadTime),RHITime=FPlatformTime::ToMilliseconds(GRHIThreadTime),GPU=FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles());
  const int32 SnapshotIndex=Frames.Num()<SampleCapacity?Frames.Num():SampleCursor;
  if(!FrameSeriesPath.IsEmpty()){
   FFrameSample Snapshot;Snapshot.Time=Now-ReadyAt;Snapshot.Frame=Frame;Snapshot.Game=Game;Snapshot.Render=Render;Snapshot.RHI=RHITime;Snapshot.GPU=GPU;Snapshot.Shadows=ShadowTransitions;Snapshot.Details=DetailSwitches;Snapshot.ReportMilliseconds=PreviousReportMilliseconds;
   Snapshot.IdleMilliseconds=FApp::GetIdleTime()*1000.;Snapshot.IdleOvershootMilliseconds=FApp::GetIdleTimeOvershoot()*1000.;
   FrameSeenLeaders.SetNum(MotionProbes.Num());FrameSeenEvaluations.SetNumZeroed(MotionProbes.Num());
   for(int32 I=0;I<MotionProbes.Num();++I)if(auto* Leader=Cast<UOKCrowdRenderSkin>(MotionProbes[I].Get())){const uint64 Current=Leader->PoseEvaluations;if(FrameSeenLeaders[I].Get()==Leader&&Current>=FrameSeenEvaluations[I]){const uint64 Count=Current-FrameSeenEvaluations[I];Snapshot.Poses+=int32(Count);if(auto* Mesh=Leader->GetSkeletalMeshAsset())Snapshot.PoseBones+=double(Count)*Mesh->GetRefSkeleton().GetNum();}FrameSeenLeaders[I]=Leader;FrameSeenEvaluations[I]=Current;}
   if(FrameSeries.Num()==SnapshotIndex)FrameSeries.Add(Snapshot);else if(FrameSeries.IsValidIndex(SnapshotIndex))FrameSeries[SnapshotIndex]=Snapshot;
  }
  ++SamplesSeen;
  if(Frames.Num()<SampleCapacity){Frames.Add(Frame);GameFrames.Add(Game);RenderFrames.Add(Render);RHIFrames.Add(RHITime);GPUFrames.Add(GPU);}else{Frames[SampleCursor]=Frame;GameFrames[SampleCursor]=Game;RenderFrames[SampleCursor]=Render;RHIFrames[SampleCursor]=RHITime;GPUFrames[SampleCursor]=GPU;SampleCursor=(SampleCursor+1)%SampleCapacity;}
 }
 LastWall=Now;
 if(!SuppressPeriodicReports&&Now-LastReport>10){LastReport=Now;SaveReport();}
 FString Shot;FParse::Value(FCommandLine::Get(),TEXT("CrowdScreenshot="),Shot);
 if(ControlRevision>0)Shot=ControlledScreenshot;
 // Capture during warm-up: GPU readback must not contaminate the timed frame window.
 if(ReadyAt>0&&Now-ReadyAt>3&&!ScreenshotTaken&&!Shot.IsEmpty()){FScreenshotRequest::RequestScreenshot(Shot,false,false);ScreenshotTaken=true;}
 if(QualityPilot&&ReadyAt>0&&!Shot.IsEmpty()&&VisualPoseShots<7&&FParse::Param(FCommandLine::Get(),TEXT("CrowdVisualPoseSamples"))){
  double Period=2.;auto* ClipLibrary=Walks.IsEmpty()?nullptr:Walks[0].Get();if(ClipLibrary&&!ClipLibrary->Animations.IsEmpty()&&ClipLibrary->Animations[0].Sequence)Period=ClipLibrary->Animations[0].Sequence->GetPlayLength();
  if(Now-ReadyAt>=4.+Period*VisualPoseShots/7.){FScreenshotRequest::RequestScreenshot(FPaths::GetPath(Shot)/(FPaths::GetBaseFilename(Shot)+FString::Printf(TEXT(".pose%d.png"),VisualPoseShots)),false,false);++VisualPoseShots;}
 }
 if(VisualComparison&&ReadyAt>0&&Now-ReadyAt>12&&!VisualComparisonCaptured){FString Prefix;if(FParse::Value(FCommandLine::Get(),TEXT("CrowdVisualOutput="),Prefix)){FString Error;if(ClockComparison?OKCaptureCrowdGpuClockComparison(IndependentGpuRuntime,Prefix,Error):OKCaptureCrowdIndependentGpuComparison(IndependentGpuRuntime,Prefix,Error))VisualComparisonCaptured=true;else UE_LOG(LogTemp,Warning,TEXT("GPU visual capture waiting: %s"),*Error);}}
 float RunSeconds=0;FParse::Value(FCommandLine::Get(),TEXT("CrowdRunSeconds="),RunSeconds);
 if(ControlRevision>0){if(!ControlledFinished&&Now-ReadyAt>=ControlledDuration){ControlledFinished=true;SaveReport();UE_LOG(LogTemp,Display,TEXT("Crowd controlled run complete tag=%s"),*ControlledTag);}}
 else if(ReadyAt>0&&RunSeconds>0&&Now-ReadyAt>=RunSeconds){SaveReport();FPlatformMisc::RequestExit(false);}
}
void AOKCrowdScene::SaveReport(){
 if(ControlRevision>0&&ControlledFinished&&ControlledReportWritten)return;
 TRACE_CPUPROFILER_EVENT_SCOPE(CrowdSaveReport);
 const double ReportStarted=FPlatformTime::Seconds();
 auto Report=MakeShared<FJsonObject>();
 const auto Packing=OKGetCrowdBonePackingStats();Report->SetNumberField(TEXT("bone_packing_mode"),Packing.Mode);Report->SetNumberField(TEXT("bone_packing_work_items"),Packing.WorkItems);Report->SetNumberField(TEXT("bone_packing_reused_bones"),Packing.ReusedBones);Report->SetNumberField(TEXT("bone_upload_raw_source_bytes"),Packing.RawSourceBytes);Report->SetNumberField(TEXT("bone_upload_unique_raw_source_bytes"),Packing.UniqueRawSourceBytes);Report->SetNumberField(TEXT("bone_upload_written_bytes"),Packing.WrittenBytes);Report->SetNumberField(TEXT("bone_upload_allocated_bytes"),Packing.AllocatedBytes);Report->SetNumberField(TEXT("bone_upload_footprint_samples"),Packing.FootprintSamples);Report->SetNumberField(TEXT("gpu_bone_packing_passes"),Packing.GpuPasses);Report->SetNumberField(TEXT("gpu_bone_packing_fallbacks"),Packing.GpuFallbacks);Report->SetNumberField(TEXT("gpu_bone_packing_verified_proxies"),Packing.GpuVerifiedProxies);Report->SetBoolField(TEXT("parallel_bone_packing_installed"),Packing.Installed);Report->SetBoolField(TEXT("parallel_bone_packing_verified"),Packing.Verified);Report->SetNumberField(TEXT("parallel_bone_packing_passes"),Packing.Passes);Report->SetNumberField(TEXT("parallel_bone_packing_jobs"),Packing.Jobs);Report->SetNumberField(TEXT("parallel_bone_packing_bones"),Packing.Bones);Report->SetNumberField(TEXT("parallel_bone_packing_verified_jobs"),Packing.VerifiedJobs);Report->SetNumberField(TEXT("parallel_bone_packing_mismatches"),Packing.Mismatches);
 Report->SetBoolField(TEXT("periodic_reports_suppressed"),SuppressPeriodicReports);
 Report->SetNumberField(TEXT("trace_duration_seconds"),TraceDurationSeconds);
 // These totals describe completed earlier calls, excluding this serialization.
 Report->SetNumberField(TEXT("completed_report_calls"),double(CompletedReportCalls));Report->SetNumberField(TEXT("report_total_ms"),ReportTotalMilliseconds);Report->SetNumberField(TEXT("report_max_ms"),ReportMaxMilliseconds);
 float RunDuration=0;FParse::Value(FCommandLine::Get(),TEXT("CrowdRunSeconds="),RunDuration);
 const bool RunFinished=ControlRevision>0?ControlledFinished:ReadyAt>0&&RunDuration>0&&FPlatformTime::Seconds()-ReadyAt>=RunDuration;
 Report->SetNumberField(TEXT("control_revision"),ControlRevision);Report->SetStringField(TEXT("control_tag"),ControlledTag);Report->SetBoolField(TEXT("run_finished"),RunFinished);
 Report->SetBoolField(TEXT("detailed_profile"),!ControlledTrace.IsEmpty()||FParse::Param(FCommandLine::Get(),TEXT("statnamedevents")));
 Report->SetNumberField(TEXT("task_workers"),FTaskGraphInterface::Get().GetNumWorkerThreads());
 Report->SetStringField(TEXT("asset_variant"),AssetVariant);Report->SetBoolField(TEXT("animation_rate_lod"),QualityAnimationRates);
 Report->SetStringField(TEXT("camera_mode"),ThirdPersonCamera?TEXT("third_person_follow"):QualityPilot?TEXT("pilot"):TEXT("overview"));Report->SetNumberField(TEXT("camera_follow_resident"),ThirdPersonCamera?CameraResident:-1);
 Report->SetBoolField(TEXT("player_control"),PlayerControl);
 Report->SetBoolField(TEXT("crowd_behaviors"),CrowdBehaviors);
 Report->SetBoolField(TEXT("behavior_route_invariants_passed"),CrowdBehaviors);
 int32 Walking=0,Resting=0,Looking=0,Transitions=0,Stops=0,Starts=0;
 for(int32 I=0;I<Behaviors.Num();++I){if(PlayerControl&&I==CameraResident)continue;const auto& B=Behaviors[I];
  Walking+=B.State==FOKCrowdBehavior::EState::Walk;Resting+=B.State==FOKCrowdBehavior::EState::Rest;Looking+=B.State==FOKCrowdBehavior::EState::Look;
  Transitions+=B.Transitions;Stops+=B.Stops;Starts+=B.Starts;}
 Report->SetNumberField(TEXT("behavior_walking"),Walking);Report->SetNumberField(TEXT("behavior_resting"),Resting);Report->SetNumberField(TEXT("behavior_looking"),Looking);
 Report->SetNumberField(TEXT("behavior_transitions"),Transitions);Report->SetNumberField(TEXT("behavior_stops"),Stops);Report->SetNumberField(TEXT("behavior_starts"),Starts);
 Report->SetNumberField(TEXT("behavior_clip_switches"),BehaviorClipSwitches);Report->SetNumberField(TEXT("behavior_missing_idle_choices"),BehaviorMissingIdle);
Report->SetNumberField(TEXT("player_distance_cm"),PlayerDistanceCm);Report->SetNumberField(TEXT("player_idle_drift_cm"),PlayerIdleDriftCm);Report->SetBoolField(TEXT("player_reached_boundary"),PlayerReachedBoundary);
 TArray<TSharedPtr<FJsonValue>> MovementDistances;bool MovementGood=PlayerMovementTest&&PlayerMovementInitialized&&PlayerIdleDriftCm<.1&&PlayerReachedBoundary;for(double Distance:MovementTestDistances){MovementDistances.Add(MakeShared<FJsonValueNumber>(Distance));MovementGood&=Distance>100&&Distance<250;}const double StraightMean=(MovementTestDistances[0]+MovementTestDistances[1]+MovementTestDistances[2]+MovementTestDistances[3])/4;MovementGood&=MovementTestDistances[4]<StraightMean*1.25&&MovementTestDistances[4]>StraightMean*.75;Report->SetArrayField(TEXT("movement_test_distances_cm"),MovementDistances);Report->SetBoolField(TEXT("movement_test_met"),MovementGood);
 if(auto* PC=GetWorld()->GetFirstPlayerController()){FVector Location;FRotator Rotation;PC->GetPlayerViewPoint(Location,Rotation);TArray<TSharedPtr<FJsonValue>> Position;for(double Axis:{Location.X,Location.Y,Location.Z})Position.Add(MakeShared<FJsonValueNumber>(Axis));Report->SetArrayField(TEXT("camera_position_cm"),Position);Report->SetNumberField(TEXT("camera_yaw_degrees"),Rotation.Yaw);
  auto* Player=PC->GetLocalPlayer();auto* Viewport=GetWorld()->GetGameViewport();FSceneViewProjectionData Projection;if(Player&&Viewport&&Viewport->Viewport&&Player->GetProjectionData(Viewport->Viewport,Projection)){FConvexVolume Frustum;GetViewFrustumBounds(Frustum,Projection.ComputeViewProjectionMatrix(),true);int32 Candidates=0;for(const auto& Actor:Residents){FVector Origin,Extent;Actor->GetActorBounds(false,Origin,Extent);if(Frustum.IntersectBox(Origin,Extent))++Candidates;}Report->SetNumberField(TEXT("frustum_candidate_residents"),Candidates);}
 }
 Report->SetStringField(TEXT("visible_sections_scope"),TEXT("Enabled main-pass sections across scene, including outside-frustum components"));
 Report->SetBoolField(TEXT("outline_material_lod"),LightweightGame&&!FParse::Param(FCommandLine::Get(),TEXT("CrowdFullOutlines")));
 uint64 Evaluations=0,Skips=0;for(const auto& Actor:Residents){TArray<UOKCrowdRenderSkin*> Skins;Actor->GetComponents(Skins);for(auto* Skin:Skins){Evaluations+=Skin->PoseEvaluations;Skips+=Skin->PoseSkips;}}
 Report->SetBoolField(TEXT("balanced_pose_evaluation"),BalancedPose);Report->SetNumberField(TEXT("pose_balance_min_bones"),PoseBalanceMin);Report->SetNumberField(TEXT("pose_balance_max_bones"),PoseBalanceMax);Report->SetNumberField(TEXT("shadow_transitions"),ShadowTransitions);
 int32 FaceShadowMissing=0;for(const auto& Pair:Faces)if(auto* A=Pair.Value.Actor.Get()){TArray<USkeletalMeshComponent*> Skins;A->GetComponents(Skins);for(auto* S:Skins)if(S->IsVisible()&&(!S->ComponentHasTag(TEXT("HighDetailMain"))||S->ComponentHasTag(TEXT("NearHairShadow"))||S->ComponentHasTag(TEXT("NearFaceShadow")))&&!S->CastShadow)++FaceShadowMissing;}
 Report->SetNumberField(TEXT("face_shadow_missing_components"),FaceShadowMissing);Report->SetBoolField(TEXT("face_shadow_policy_met"),Faces.Num()>0&&FaceShadowMissing==0);
 Report->SetBoolField(TEXT("shadow_policy_cached"),CacheShadowPolicy);Report->SetNumberField(TEXT("shadow_hysteresis_cm"),ShadowHysteresisCm);Report->SetNumberField(TEXT("shadow_cache_rebuilds"),ShadowCacheRebuilds);Report->SetNumberField(TEXT("shadow_policy_updates"),ShadowPolicyUpdates);Report->SetNumberField(TEXT("shadow_active_residents"),ShadowActiveResidents);Report->SetBoolField(TEXT("focus_only_shadows"),FocusOnlyShadows);Report->SetBoolField(TEXT("focused_high_only"),FocusedHighOnly);Report->SetNumberField(TEXT("high_detail_focus"),DetailFocus);Report->SetNumberField(TEXT("outline_radius_cm"),OutlineRadius);Report->SetNumberField(TEXT("shadow_radius_cm"),ShadowRadius);Report->SetNumberField(TEXT("upload_near_radius_cm"),UploadNearRadius);Report->SetNumberField(TEXT("gather_main_view_tasks"),IConsoleManager::Get().FindConsoleVariable(TEXT("r.Visibility.DynamicMeshElements.NumMainViewTasks"))->GetInt());
 Report->SetBoolField(TEXT("stagger_pose_evaluation"),FParse::Param(FCommandLine::Get(),TEXT("CrowdStaggerPose")));Report->SetNumberField(TEXT("animation_pose_hz"),AnimationHz);Report->SetNumberField(TEXT("pose_evaluations"),double(Evaluations));Report->SetNumberField(TEXT("pose_skips"),double(Skips));
 int64 GroupSource=0,GroupResult=0,GroupAccepted=0,GroupRejected=0,GroupVertices=0;double GroupSeconds=0;
 for(const auto& A:Residents)if(auto* R=Cast<AOKCrowdRenderActor>(A)){GroupSource+=R->GroupedSourceParts;GroupResult+=R->GroupedResultParts;GroupAccepted+=R->GroupedAcceptedGroups;GroupRejected+=R->GroupedRejectedGroups;GroupVertices+=R->GroupedAuditedVertices;GroupSeconds+=R->GroupedBuildSeconds;}
 Report->SetBoolField(TEXT("group_primitives"),FParse::Param(FCommandLine::Get(),TEXT("CrowdGroupPrimitives")));Report->SetNumberField(TEXT("group_source_parts"),double(GroupSource));Report->SetNumberField(TEXT("group_result_parts"),double(GroupResult));Report->SetNumberField(TEXT("group_accepted"),double(GroupAccepted));Report->SetNumberField(TEXT("group_rejected"),double(GroupRejected));Report->SetNumberField(TEXT("group_audited_vertices"),double(GroupVertices));Report->SetNumberField(TEXT("group_build_seconds"),GroupSeconds);
 Report->SetBoolField(TEXT("render_upload_culling"),CullRenderUploads);Report->SetNumberField(TEXT("render_upload_bound_skins"),UploadBoundSkins);Report->SetNumberField(TEXT("render_upload_suppressed_candidates"),UploadSuppressedCandidates);
 const auto* CopyDebugVariable=IConsoleManager::Get().FindConsoleVariable(TEXT("r.SkeletalMesh.CopyDebugPose"));Report->SetNumberField(TEXT("skeletal_debug_pose_copy"),CopyDebugVariable?CopyDebugVariable->GetInt():-1);Report->SetBoolField(TEXT("skeletal_debug_pose_copy_effective"),GIsEditor||!CopyDebugVariable||CopyDebugVariable->GetInt()!=0);
 const auto* MatrixCacheVariable=IConsoleManager::Get().FindConsoleVariable(TEXT("r.SkeletalMesh.ReuseComponentMatrices"));const auto* VerifyMatrixVariable=IConsoleManager::Get().FindConsoleVariable(TEXT("r.SkeletalMesh.VerifyComponentMatrices"));Report->SetNumberField(TEXT("component_matrix_cache"),MatrixCacheVariable?MatrixCacheVariable->GetInt():-1);Report->SetBoolField(TEXT("component_matrix_cache_verification"),VerifyMatrixVariable&&VerifyMatrixVariable->GetInt()!=0);
 const auto* FullRefVariable=IConsoleManager::Get().FindConsoleVariable(TEXT("r.SkeletalMesh.ReuseRefToLocalMatrices"));Report->SetNumberField(TEXT("full_ref_matrix_cache"),FullRefVariable?FullRefVariable->GetInt():-1);
#if !WITH_EDITOR && OK_CROWD_FULL_REF_MATRIX_CACHE
 uint64 FullRefHits=0,FullRefMisses=0,FullRefVerified=0,FullRefMismatches=0;bool FullRefFailed=false;GetSkeletalFullRefMatrixCacheStats(FullRefHits,FullRefMisses,FullRefVerified,FullRefMismatches,FullRefFailed);
 Report->SetNumberField(TEXT("full_ref_matrix_hits"),double(FullRefHits));Report->SetNumberField(TEXT("full_ref_matrix_misses"),double(FullRefMisses));Report->SetNumberField(TEXT("full_ref_matrix_verified"),double(FullRefVerified));Report->SetNumberField(TEXT("full_ref_matrix_mismatches"),double(FullRefMismatches));Report->SetBoolField(TEXT("full_ref_matrix_failed"),FullRefFailed);
#endif
 const auto* ConstantKeyVariable=IConsoleManager::Get().FindConsoleVariable(TEXT("a.ConstantKeyLerp.ISPC"));Report->SetNumberField(TEXT("constant_key_lerp_ispc"),ConstantKeyVariable?ConstantKeyVariable->GetInt():-1);
 const auto* ConstantScaleVariable=IConsoleManager::Get().FindConsoleVariable(TEXT("r.Anim.ConstantScaleCache"));const auto* VerifyScaleVariable=IConsoleManager::Get().FindConsoleVariable(TEXT("r.Anim.VerifyConstantScaleCache"));Report->SetNumberField(TEXT("constant_scale_cache"),ConstantScaleVariable?ConstantScaleVariable->GetInt():-1);Report->SetBoolField(TEXT("constant_scale_cache_verification"),VerifyScaleVariable&&VerifyScaleVariable->GetInt()!=0);Report->SetNumberField(TEXT("constant_scale_cache_prepared"),ConstantScalePrepared);Report->SetNumberField(TEXT("constant_scale_cache_unsupported"),ConstantScaleUnsupported);
#if !WITH_EDITOR && OK_CROWD_CONSTANT_SCALE_CACHE
 uint64 ScaleHits=0,ScaleMisses=0,ScaleVerified=0,ScaleMismatches=0;bool ScaleFailed=false;GetAnimConstantScaleCacheStats(ScaleHits,ScaleMisses,ScaleVerified,ScaleMismatches,ScaleFailed);Report->SetNumberField(TEXT("constant_scale_cache_hits"),double(ScaleHits));Report->SetNumberField(TEXT("constant_scale_cache_misses"),double(ScaleMisses));Report->SetNumberField(TEXT("constant_scale_cache_verified"),double(ScaleVerified));Report->SetNumberField(TEXT("constant_scale_cache_mismatches"),double(ScaleMismatches));Report->SetBoolField(TEXT("constant_scale_cache_failed"),ScaleFailed);
#endif
#if !WITH_EDITOR && OK_CROWD_COMPONENT_MATRIX_CACHE
 uint64 MatrixHits=0,MatrixMisses=0,MatrixMismatches=0,MatrixVerified=0;bool MatrixCacheFailed=false;GetSkeletalComponentMatrixCacheStats(MatrixHits,MatrixMisses,MatrixMismatches,MatrixVerified,MatrixCacheFailed);Report->SetNumberField(TEXT("component_matrix_cache_hits"),double(MatrixHits));Report->SetNumberField(TEXT("component_matrix_cache_misses"),double(MatrixMisses));Report->SetNumberField(TEXT("component_matrix_cache_mismatches"),double(MatrixMismatches));Report->SetNumberField(TEXT("component_matrix_cache_verified"),double(MatrixVerified));Report->SetBoolField(TEXT("component_matrix_cache_failed"),MatrixCacheFailed);
#endif
 bool FullPose=true;for(const auto& Actor:Residents){TArray<USkeletalMeshComponent*> Skins;Actor->GetComponents(Skins);for(auto* Skin:Skins)FullPose&=Skin->VisibilityBasedAnimTickOption==EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones&&!Skin->bEnableUpdateRateOptimizations;}Report->SetBoolField(TEXT("always_tick_pose_policy"),FullPose);Report->SetBoolField(TEXT("full_pose_tick_policy"),FullPose&&AnimationHz==0);
 Report->SetNumberField(TEXT("rhi_command_width"),IConsoleManager::Get().FindConsoleVariable(TEXT("r.RHICmdWidth"))->GetInt());Report->SetBoolField(TEXT("hardware_occlusion_queries"),IConsoleManager::Get().FindConsoleVariable(TEXT("r.AllowOcclusionQueries"))->GetInt()!=0);
 Report->SetNumberField(TEXT("section_lod_pixels"),SectionLODPixels);Report->SetNumberField(TEXT("section_lod_matched_meshes"),SectionLODMatched);Report->SetNumberField(TEXT("section_lod_hidden_sections"),SectionLODHidden);Report->SetNumberField(TEXT("section_lod_max_hidden_sections"),SectionLODMaxHidden);Report->SetNumberField(TEXT("section_lod_transitions"),SectionLODTransitions);Report->SetNumberField(TEXT("section_lod_restores"),SectionLODRestores);
 Report->SetBoolField(TEXT("section_lod_switch_test_met"),QualityPilot&&FParse::Param(FCommandLine::Get(),TEXT("CrowdSectionLODSwitchTest"))&&SectionLODMaxHidden>0&&SectionLODRestores>0&&SectionLODHidden==0&&!QualityAnimationRates);
 Report->SetBoolField(TEXT("quality_pilot"),QualityPilot);Report->SetBoolField(TEXT("loading_residents_deferred"),DeferResidentsDuringLoad);Report->SetNumberField(TEXT("current_async_loading_budget_ms"),IConsoleManager::Get().FindConsoleVariable(TEXT("s.AsyncLoadingTimeLimit"))->GetFloat());
 const double SceneLoad=InitialReadyAt>0?InitialReadyAt-SceneStarted:FPlatformTime::Seconds()-SceneStarted;
 Report->SetNumberField(TEXT("load_wall_seconds"),SceneLoad);Report->SetNumberField(TEXT("load_process_seconds"),StartupSeconds+SceneLoad);Report->SetNumberField(TEXT("startup_seconds"),StartupSeconds);
 Report->SetBoolField(TEXT("process_load_60_seconds_met"),!QualityPilot&&Characters.Num()==100&&InitialReadyAt>0&&StartupSeconds+SceneLoad<=60&&Residents.Num()==100&&Failed==0);
 Report->SetBoolField(TEXT("load_60_seconds_met"),!QualityPilot&&Characters.Num()==100&&InitialReadyAt>0&&SceneLoad<=60&&Residents.Num()==Characters.Num()&&Failed==0);
 Report->SetNumberField(TEXT("high_detail_residents"),HighDetail.FilterByPredicate([](bool High){return High;}).Num());Report->SetNumberField(TEXT("detail_switches"),DetailSwitches);
 Report->SetNumberField(TEXT("initial_high_meshes"),InitialHighMeshCount);Report->SetNumberField(TEXT("initial_high_or_large_textures"),InitialLargeTextureCount);Report->SetBoolField(TEXT("initial_low_only_met"),LightweightGame&&InitialReadyAt>0&&InitialHighMeshCount==0&&InitialLargeTextureCount==0);Report->SetBoolField(TEXT("detail_interaction_started"),DetailInteractionStarted);
 Report->SetNumberField(TEXT("detail_phase_error_seconds"),DetailPhaseError);Report->SetBoolField(TEXT("detail_switch_test_met"),QualityPilot&&FParse::Param(FCommandLine::Get(),TEXT("CrowdDetailSwitchTest"))&&DetailSwitches>=2&&HighDetail.FilterByPredicate([](bool High){return High;}).IsEmpty()&&DetailPhaseError<.001);
 TArray<int32> LODCounts;LODCounts.Init(0,3);for(const auto& Probe:MotionProbes)if(IsValid(Probe.Get()))++LODCounts[FMath::Clamp(Probe->GetPredictedLODLevel(),0,2)];TArray<TSharedPtr<FJsonValue>> LODJson;for(int32 Count:LODCounts)LODJson.Add(MakeShared<FJsonValueNumber>(Count));Report->SetArrayField(TEXT("resident_lod_counts"),LODJson);
 Report->SetNumberField(TEXT("end_frame_mode"),IConsoleManager::Get().FindConsoleVariable((WITH_EDITOR ? TEXT("AllowAsyncRenderThreadUpdatesEditorGameWorld") : TEXT("AllowAsyncRenderThreadUpdates")))->GetInt());
 if(auto* V=IConsoleManager::Get().FindConsoleVariable(TEXT("r.Shadow.Virtual.NonNanite.ParallelDraw")))Report->SetNumberField(TEXT("parallel_shadow_draw"),V->GetInt());
 TSet<FString> Identities;for(const auto& Actor:Residents)if(Actor->Character)Identities.Add(Actor->Character->SourceHash);Report->SetNumberField(TEXT("distinct_source_hashes"),Identities.Num());
 Report->SetNumberField(TEXT("selected"),Characters.Num());Report->SetNumberField(TEXT("loaded"),Residents.Num());Report->SetNumberField(TEXT("failed"),Failed);
 int32 Animated=0;for(int32 I=0;I<Residents.Num();++I)if(IsValid(Residents[I])&&AnimationAdvanced.IsValidIndex(I)&&AnimationAdvanced[I]&&(GpuPose?(GpuMotionProbes.IsValidIndex(I)&&IsValid(GpuMotionProbes[I])&&GpuMotionProbes[I]->IsEnabled()):(IsValid(MotionProbes[I])&&MotionProbes[I]->IsPlaying())))++Animated;
 Report->SetBoolField(TEXT("gpu_pose"),GpuPose);Report->SetBoolField(TEXT("face_lod_enabled"),FaceLOD);Report->SetNumberField(TEXT("high_face_residents"),Faces.Num());Report->SetNumberField(TEXT("face_loads"),FaceLoads);Report->SetNumberField(TEXT("face_restores"),FaceRestores);int64 LowHair=0,NearHair=0;for(const auto& Pair:Faces){LowHair+=Pair.Value.Asset->LowHairTriangles;NearHair+=Pair.Value.Asset->NearHairTriangles;}Report->SetNumberField(TEXT("low_hair_triangles_replaced"),LowHair);Report->SetNumberField(TEXT("near_hair_triangles"),NearHair);Report->SetBoolField(TEXT("face_round_trip_met"),FaceLoads>=2&&FaceRestores>=1&&!Faces.IsEmpty());Report->SetBoolField(TEXT("body_stays_low"),FaceLOD&&HighDetail.FilterByPredicate([](bool H){return H;}).Num()==0);
 // Provider clocks are diagnostic evidence, not proof of rendered animation correctness.
 Report->SetBoolField(TEXT("gpu_pose_validation_pending"),GpuPose);
 Report->SetBoolField(TEXT("independent_gpu_animation_requested"),IndependentGpuAnimationRequested);
 const auto IndependentStats=OKGetCrowdIndependentGpuStats(IndependentGpuRuntime);
 Report->SetNumberField(TEXT("independent_gpu_character_path_lookups"),IndependentStats.CharacterLookups);
 Report->SetNumberField(TEXT("independent_gpu_clip_path_lookups"),IndependentStats.ClipLookups);
 Report->SetNumberField(TEXT("independent_gpu_mesh_path_lookups"),IndependentStats.MeshLookups);
 Report->SetNumberField(TEXT("independent_gpu_component_cache_rebuilds"),IndependentStats.ComponentRebuilds);
 Report->SetBoolField(TEXT("independent_gpu_animation_backend_ready"),IndependentStats.Ready);
 Report->SetNumberField(TEXT("independent_gpu_animation_executed_components"),IndependentGpuAnimationRequested?IndependentStats.Components:0);
 Report->SetNumberField(TEXT("independent_gpu_animation_render_passes"),IndependentStats.Passes);
 Report->SetNumberField(TEXT("independent_gpu_palette_dispatches"),IndependentStats.PaletteDispatches);
 Report->SetNumberField(TEXT("independent_gpu_animation_render_fallbacks"),IndependentStats.Fallbacks);
 Report->SetNumberField(TEXT("independent_gpu_animation_executed_actors"),IndependentGpuAnimationRequested?IndependentStats.Actors:0);
 Report->SetNumberField(TEXT("independent_gpu_animation_verified_components"),IndependentStats.VerifiedComponents);
 Report->SetNumberField(TEXT("independent_gpu_animation_verified_matrices"),IndependentStats.VerifiedMatrices);
 Report->SetNumberField(TEXT("independent_gpu_animation_verified_previous_matrices"),IndependentStats.VerifiedPreviousMatrices);
 Report->SetNumberField(TEXT("independent_gpu_provider_probe_components"),IndependentStats.ProbeComponents);
 Report->SetBoolField(TEXT("independent_gpu_provider_probe_cpu_pose_absent"),IndependentStats.ProbeCpuPoseAbsent);
 Report->SetBoolField(TEXT("independent_gpu_provider_probe_original_materials"),IndependentStats.ProbeOriginalMaterials);
 Report->SetNumberField(TEXT("independent_gpu_provider_probe_material_slots"),IndependentStats.ProbeMaterialSlots);
 Report->SetNumberField(TEXT("independent_gpu_provider_probe_matrices"),IndependentStats.ProbeMatrices);
 Report->SetNumberField(TEXT("independent_gpu_provider_probe_previous_matrices"),IndependentStats.ProbePreviousMatrices);
 Report->SetNumberField(TEXT("independent_gpu_provider_probe_mismatches"),IndependentStats.ProbeMismatches);
 Report->SetNumberField(TEXT("independent_gpu_provider_probe_max_error"),IndependentStats.ProbeMaxError);
 Report->SetNumberField(TEXT("independent_gpu_animation_mismatches"),IndependentStats.Mismatches);
 Report->SetNumberField(TEXT("independent_gpu_animation_max_error"),IndependentStats.MaxError);
 Report->SetNumberField(TEXT("independent_gpu_runtime_palette_builds"),IndependentStats.RuntimePaletteBuilds);Report->SetNumberField(TEXT("independent_gpu_runtime_palette_rejects"),IndependentStats.RuntimePaletteRejects);Report->SetNumberField(TEXT("independent_gpu_runtime_palette_components"),IndependentStats.RuntimePaletteComponents);
 Report->SetNumberField(TEXT("independent_gpu_current_pose_evaluations"),IndependentStats.CurrentPoseEvaluations);Report->SetNumberField(TEXT("independent_gpu_history_pose_evaluations"),IndependentStats.HistoryPoseEvaluations);Report->SetNumberField(TEXT("independent_gpu_current_pose_reuses"),IndependentStats.CurrentPoseReuses);Report->SetNumberField(TEXT("independent_gpu_history_pose_reuses"),IndependentStats.HistoryPoseReuses);Report->SetNumberField(TEXT("independent_gpu_history_immutable_upload_bytes"),IndependentStats.HistoryImmutableUploadBytes);
 Report->SetNumberField(TEXT("independent_gpu_clock_owned_actors"),IndependentStats.ClockOwnedActors);
 Report->SetNumberField(TEXT("independent_gpu_clock_updates"),IndependentStats.ClockUpdates);
 Report->SetNumberField(TEXT("independent_gpu_clock_fallbacks"),IndependentStats.ClockFallbacks);
 Report->SetNumberField(TEXT("independent_gpu_clock_cpu_evaluations"),IndependentStats.ClockCpuEvaluations);
 Report->SetBoolField(TEXT("independent_gpu_animation_cpu_pose_retained"),IndependentStats.ClockOwnedActors<uint64(Residents.Num()));
 {const auto Load=OKGetCrowdGpuLoadStats(IndependentGpuRuntime);auto Object=MakeShared<FJsonObject>();Object->SetBoolField(TEXT("from_shard_index"),Load.FromIndex);Object->SetBoolField(TEXT("worker_thread"),Load.WorkerThread);Object->SetNumberField(TEXT("bundle_actors"),Load.BundleActors);Object->SetNumberField(TEXT("loaded_shards"),Load.LoadedShards);Object->SetNumberField(TEXT("missing_selected_actors"),Load.MissingSelectedActors);Object->SetNumberField(TEXT("load_ms"),Load.LoadMs);Report->SetObjectField(TEXT("independent_gpu_cache_load"),Object);}
 {const FString Audit=OKGetCrowdGpuOwnershipAudit(IndependentGpuRuntime);TSharedPtr<FJsonObject> AuditObject;if(!Audit.IsEmpty()&&FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Audit),AuditObject)&&AuditObject)Report->SetObjectField(TEXT("independent_gpu_ownership_audit"),AuditObject);}
 Report->SetNumberField(TEXT("independent_gpu_animation_request_changes"),IndependentGpuAnimationRequestChanges);
 Report->SetBoolField(TEXT("independent_gpu_animation_changed_during_samples"),IndependentGpuAnimationChangedDuringSamples);
 Report->SetStringField(TEXT("independent_gpu_animation_status"),GetIndependentGpuAnimationStatus());
 // A stationary controlled player intentionally pauses its walk. Require every
 // other resident to animate; never treat an unexpectedly paused NPC as valid.
 const bool IdleControlledPlayer=PlayerControl&&PlayerMovementInitialized&&!PlayerWasMoving&&Residents.IsValidIndex(CameraResident)&&IsValid(Residents[CameraResident])&&PlayerAnimationActor.Get()==Residents[CameraResident].Get()&&MotionProbes.IsValidIndex(CameraResident)&&IsValid(MotionProbes[CameraResident])&&FMath::IsNearlyZero(MotionProbes[CameraResident]->GetPlayRate());
 const int32 RequiredAnimated=Characters.Num()-(IdleControlledPlayer?1:0);
 Report->SetBoolField(TEXT("player_animation_paused"),IdleControlledPlayer);
 Report->SetNumberField(TEXT("animation_expected_active"),RequiredAnimated);
 Report->SetNumberField(TEXT("animated"),Animated);Report->SetBoolField(TEXT("complete"),Next==Characters.Num()&&Residents.Num()==Characters.Num()&&Failed==0&&Animated==RequiredAnimated);
 Report->SetStringField(TEXT("path"),TEXT("Imported SkeletalMesh / GPU skin / baked authored walk including private bone physics"));
 Report->SetNumberField(TEXT("samples"),Frames.Num());Report->SetNumberField(TEXT("memory_mb"),double(FPlatformMemory::GetStats().UsedPhysical)/1048576.);
 auto Mean=[](const TArray<double>& Values){double Sum=0;for(double V:Values)Sum+=V;return Values.IsEmpty()?0.:Sum/Values.Num();};
 Report->SetNumberField(TEXT("sample_capacity"),SampleCapacity);Report->SetNumberField(TEXT("samples_seen"),double(SamplesSeen));Report->SetNumberField(TEXT("sample_window_seconds"),Mean(Frames)*Frames.Num()/1000.);Report->SetBoolField(TEXT("all_warm_frames_retained"),SamplesSeen==uint64(Frames.Num()));
 if(RunFinished&&!FrameSeriesWritten&&!FrameSeriesPath.IsEmpty()){
  FString CSV=TEXT("ready_time_s,frame_ms,game_ms,render_ms,rhi_ms,gpu_ms,pose_updates,pose_bones_proxy,shadow_transitions,detail_switches,report_ms,idle_ms,idle_overshoot_ms\n");CSV.Reserve(FrameSeries.Num()*170);
  for(int32 J=0;J<FrameSeries.Num();++J){const int32 I=FrameSeries.Num()==SampleCapacity?(SampleCursor+J)%SampleCapacity:J;const auto& Row=FrameSeries[I];CSV+=FString::Printf(TEXT("%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%d,%.0f,%d,%d,%.6f,%.6f,%.6f\n"),Row.Time,Row.Frame,Row.Game,Row.Render,Row.RHI,Row.GPU,Row.Poses,Row.PoseBones,Row.Shadows,Row.Details,Row.ReportMilliseconds,Row.IdleMilliseconds,Row.IdleOvershootMilliseconds);}
  FrameSeriesWritten=FFileHelper::SaveStringToFile(CSV,*FrameSeriesPath,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);if(!FrameSeriesWritten)UE_LOG(LogTemp,Error,TEXT("Crowd frame series export failed"));
 }
 Report->SetBoolField(TEXT("frame_series_written"),FrameSeriesWritten);
 TArray<double> Sorted=Frames;Sorted.Sort();
 Report->SetNumberField(TEXT("frame_mean_ms"),Mean(Frames));Report->SetNumberField(TEXT("game_thread_mean_ms"),Mean(GameFrames));Report->SetNumberField(TEXT("render_thread_mean_ms"),Mean(RenderFrames));Report->SetNumberField(TEXT("rhi_thread_mean_ms"),Mean(RHIFrames));Report->SetNumberField(TEXT("gpu_mean_ms"),Mean(GPUFrames));
 Report->SetNumberField(TEXT("frame_p95_ms"),Sorted.IsEmpty()?0.:Sorted[FMath::Clamp(FMath::CeilToInt(Sorted.Num()*.95)-1,0,Sorted.Num()-1)]);
 const double Budget=1000./120.;
 Report->SetNumberField(TEXT("target_fps"),120);Report->SetNumberField(TEXT("frame_budget_ms"),Budget);
 const bool FPSAcceptanceEligible=!GpuPose&&!IndependentGpuAnimationRequested&&!IndependentGpuAnimationChangedDuringSamples&&!QualityPilot&&!Report->GetBoolField(TEXT("detailed_profile"))&&!(VerifyScaleVariable&&VerifyScaleVariable->GetInt()!=0)&&!FParse::Param(FCommandLine::Get(),TEXT("CrowdAuditPoseBones"));
 Report->SetBoolField(TEXT("fps_acceptance_eligible"),FPSAcceptanceEligible);
 Report->SetBoolField(TEXT("target_met"),FPSAcceptanceEligible&&RunFinished&&SamplesSeen==uint64(Frames.Num())&&Characters.Num()==100&&Residents.Num()==100&&Identities.Num()==100&&Animated==RequiredAnimated&&Failed==0&&Frames.Num()>=120&&Mean(Frames)<=Budget&&!Sorted.IsEmpty()&&Sorted[FMath::Clamp(FMath::CeilToInt(Sorted.Num()*.95)-1,0,Sorted.Num()-1)]<=Budget);
 int32 Evaluators=0,SkinnedParts=0,TickingParts=0;for(const auto& Actor:Residents){TArray<USkeletalMeshComponent*> Skins;Actor->GetComponents(Skins);SkinnedParts+=Skins.Num();for(auto* Skin:Skins){if(!Skin->LeaderPoseComponent.IsValid())++Evaluators;if(Skin->IsComponentTickEnabled())++TickingParts;}}
 int64 VisibleTriangles=0;int32 VisibleSections=0,ShadowSections=0,OpaqueSections=0,MaskedSections=0,TranslucentSections=0;for(const auto& Actor:Residents){TArray<USkeletalMeshComponent*> Skins;Actor->GetComponents(Skins);for(auto* Skin:Skins)if(Skin->IsVisible()&&Skin->GetSkeletalMeshAsset())if(const auto* Data=Skin->GetSkeletalMeshAsset()->GetResourceForRendering()){const int32 LOD=FMath::Clamp(Skin->GetPredictedLODLevel(),0,Data->LODRenderData.Num()-1);for(const auto& Section:Data->LODRenderData[LOD].RenderSections)if(!Section.bDisabled&&Skin->IsMaterialSectionShown(Section.MaterialIndex,LOD)){if(Skin->bRenderInMainPass){VisibleTriangles+=Section.NumTriangles;++VisibleSections;if(auto* Material=Skin->GetMaterial(Section.MaterialIndex)){const auto Blend=Material->GetBlendMode();if(Blend==BLEND_Opaque)++OpaqueSections;else if(Blend==BLEND_Masked)++MaskedSections;else ++TranslucentSections;}}if(Skin->CastShadow)++ShadowSections;}}}
 Report->SetNumberField(TEXT("visible_triangles"),double(VisibleTriangles));Report->SetNumberField(TEXT("visible_sections"),VisibleSections);Report->SetNumberField(TEXT("shadow_sections"),ShadowSections);
 Report->SetNumberField(TEXT("visible_opaque_sections"),OpaqueSections);Report->SetNumberField(TEXT("visible_masked_sections"),MaskedSections);Report->SetNumberField(TEXT("visible_translucent_sections"),TranslucentSections);
 auto ConsoleValue=[](const TCHAR* Name){auto* Variable=IConsoleManager::Get().FindConsoleVariable(Name);return Variable?Variable->GetInt():-1;};Report->SetNumberField(TEXT("frame_rate_limit"),ConsoleValue(TEXT("t.MaxFPS")));Report->SetNumberField(TEXT("skin_cache_mode"),ConsoleValue(TEXT("r.SkinCache.Mode")));Report->SetNumberField(TEXT("skin_cache_compile_shaders"),ConsoleValue(TEXT("r.SkinCache.CompileShaders")));Report->SetNumberField(TEXT("gpu_instance_culling"),ConsoleValue(TEXT("r.CullInstances")));Report->SetNumberField(TEXT("gpu_instance_occlusion"),ConsoleValue(TEXT("r.InstanceCulling.OcclusionCull")));Report->SetNumberField(TEXT("gpu_instance_batched_commands"),ConsoleValue(TEXT("r.InstanceCulling.AllowBatchedBuildRenderingCommands")));Report->SetNumberField(TEXT("gpu_clock_eligibility_pass"),ConsoleValue(TEXT("ok.CrowdGpuEligibilityPass")));Report->SetNumberField(TEXT("skeletal_cached_draw_commands"),ConsoleValue(TEXT("r.SkeletalMesh.UseCachedMDCs")));Report->SetNumberField(TEXT("mesh_cached_draw_commands"),ConsoleValue(TEXT("r.MeshDrawCommands.UseCachedCommands")));
 Report->SetNumberField(TEXT("bone_transform_allocation_mode"),ConsoleValue(TEXT("r.GPUSkin.BoneTransformAllocationMode")));Report->SetNumberField(TEXT("gpu_skin_scene_extension"),ConsoleValue(TEXT("r.GPUSkin.UseSceneExtension")));
 Report->SetNumberField(TEXT("parallel_draw_min_draws"),ConsoleValue(TEXT("r.RHICmdMinDrawsPerParallelCmdList")));Report->SetNumberField(TEXT("trace_anchor_ready_seconds"),TraceAnchorReadySeconds);
 Report->SetNumberField(TEXT("rdg_transient_allocator"),ConsoleValue(TEXT("r.RDG.TransientAllocator")));
 Report->SetNumberField(TEXT("rhi_batch_submissions"),ConsoleValue(TEXT("r.RHICmd.ParallelTranslate.BatchSubmissions")));
 Report->SetNumberField(TEXT("present_on_submission_thread"),ConsoleValue(TEXT("r.D3D12.PresentOnSubmissionThread")));
 Report->SetNumberField(TEXT("rhi_transient_texture_cache"),ConsoleValue(TEXT("RHI.TransientAllocator.TextureCacheSize")));
 Report->SetNumberField(TEXT("anti_aliasing_method"),ConsoleValue(TEXT("r.AntiAliasingMethod")));
 Report->SetNumberField(TEXT("screen_percentage"),ConsoleValue(TEXT("r.ScreenPercentage")));
 Report->SetNumberField(TEXT("secondary_screen_percentage"),ConsoleValue(TEXT("r.SecondaryScreenPercentage.GameViewport")));
 Report->SetNumberField(TEXT("dynamic_resolution_mode"),ConsoleValue(TEXT("r.DynamicRes.OperationMode")));
 if(GEngine&&GEngine->GameViewport&&GEngine->GameViewport->Viewport){const FIntPoint Size=GEngine->GameViewport->Viewport->GetSizeXY();Report->SetNumberField(TEXT("viewport_width"),Size.X);Report->SetNumberField(TEXT("viewport_height"),Size.Y);}
 // Texture accounting is not total resident VRAM: buffers and aliased heap
 // residency need separate profiling. Query only when saving the report.
 FTextureMemoryStats TextureMemory;RHIGetTextureMemoryStats(TextureMemory);
 Report->SetNumberField(TEXT("rhi_streaming_texture_mb"),double(TextureMemory.StreamingMemorySize)/(1024.*1024.));Report->SetNumberField(TEXT("rhi_nonstreaming_texture_mb"),double(TextureMemory.NonStreamingMemorySize)/(1024.*1024.));
 Report->SetNumberField(TEXT("ticking_skin_parts"),TickingParts);
 int32 DrivenFollowers=0;for(const auto& Actor:Residents){TArray<USkeletalMeshComponent*> Skins;Actor->GetComponents(Skins);for(auto* Skin:Skins)if(Skin->ComponentTags.Contains(LeaderDrivenFollowerTag))++DrivenFollowers;}
 Report->SetBoolField(TEXT("leader_driven_followers"),FParse::Param(FCommandLine::Get(),TEXT("CrowdLeaderDrivenFollowers")));Report->SetNumberField(TEXT("leader_driven_follower_parts"),DrivenFollowers);
 Report->SetNumberField(TEXT("pose_evaluators"),Evaluators);Report->SetNumberField(TEXT("skinned_parts"),SkinnedParts);
 auto BoneCodecs=MakeShared<FJsonObject>();int64 CompressedPoseTracks=0;int32 CodecClips=0;for(const auto& Actor:Residents){TArray<USkeletalMeshComponent*> Skins;Actor->GetComponents(Skins);for(auto* Skin:Skins)if(!Skin->LeaderPoseComponent.IsValid())if(auto* Node=Skin->GetSingleNodeInstance())if(auto* Clip=Cast<UAnimSequence>(Node->GetCurrentAsset())){const auto Data=Clip->GetCompressedData();const auto* Codec=Data.Get().BoneCompressionCodec.Get();const FString Name=Codec?Codec->GetClass()->GetName():TEXT("none");double Count=0;BoneCodecs->TryGetNumberField(Name,Count);BoneCodecs->SetNumberField(Name,Count+1);CompressedPoseTracks+=Data.Get().CompressedTrackToSkeletonMapTable.Num();++CodecClips;}}Report->SetObjectField(TEXT("animation_bone_codecs"),BoneCodecs);Report->SetNumberField(TEXT("animation_compressed_tracks"),double(CompressedPoseTracks));Report->SetNumberField(TEXT("animation_codec_clips"),CodecClips);
 Report->SetNumberField(TEXT("weighted_pose_seed_applied"),WeightedSeedApplied);Report->SetNumberField(TEXT("weighted_pose_seed_verified"),WeightedSeedVerified);Report->SetNumberField(TEXT("weighted_pose_seed_failed"),WeightedSeedFailed);Report->SetNumberField(TEXT("weighted_pose_seed_removed"),double(WeightedSeedRemoved));
 FString SeedManifest;Report->SetBoolField(TEXT("weighted_pose_seed_enabled"),WeightedSeedEnabled&&FParse::Value(FCommandLine::Get(),TEXT("CrowdWeightedPoseSeed="),SeedManifest));int64 RequiredPoseBones=0;for(const auto& Actor:Residents){TArray<USkeletalMeshComponent*> Skins;Actor->GetComponents(Skins);for(auto* Skin:Skins)if(!Skin->LeaderPoseComponent.IsValid())RequiredPoseBones+=Skin->RequiredBones.Num();}Report->SetNumberField(TEXT("required_pose_bones"),double(RequiredPoseBones));
 if(FParse::Param(FCommandLine::Get(),TEXT("CrowdAuditPoseBones")))Report->SetObjectField(TEXT("pose_bone_audit"),AuditCrowdPoseBones(Residents));
 if(QualityPilot&&Residents.Num()==1&&Residents[0]->Character&&!Residents[0]->Character->Outfits.IsEmpty()){
  TArray<TSharedPtr<FJsonValue>> Parts;for(const auto& Part:Residents[0]->Character->Outfits[0].Parts)if(auto* Mesh=Part.SkinnedModel.Get()){auto Item=MakeShared<FJsonObject>();Item->SetStringField(TEXT("name"),Part.Name);Item->SetNumberField(TEXT("morph_targets"),Mesh->GetMorphTargets().Num());Item->SetNumberField(TEXT("mouth_patterns"),Part.MouthPatterns.Num());Item->SetBoolField(TEXT("default_visible"),Part.MouthDefaultVisible);Item->SetBoolField(TEXT("hide_when_closed"),Part.MouthHideWhenClosed);if(!Part.MouthPatterns.IsEmpty()){Item->SetStringField(TEXT("close_morph"),Part.MouthPatterns[0].Close.ToString());Item->SetNumberField(TEXT("close_baseline"),Part.MouthBaseline.FindRef(Part.MouthPatterns[0].Close));}Parts.Add(MakeShared<FJsonValueObject>(Item));}Report->SetArrayField(TEXT("pilot_parts"),Parts);
 }
 int32 GpuParts=0;for(const auto& Actor:Residents){TArray<UInstancedSkinnedMeshComponent*> Parts;Actor->GetComponents(Parts);GpuParts+=Parts.Num();}Report->SetNumberField(TEXT("gpu_pose_parts"),GpuParts);
 int64 BoneReferences=0;for(const auto& Actor:Residents){TArray<USkeletalMeshComponent*> Skins;Actor->GetComponents(Skins);for(const auto* Skin:Skins)if(Skin->GetSkeletalMeshAsset())BoneReferences+=Skin->GetSkeletalMeshAsset()->GetRefSkeleton().GetNum();}
 int64 SectionBoneReferences=0;int32 OptimizedMeshes=0,TotalMeshes=0;for(const auto& Actor:Residents){TArray<USkeletalMeshComponent*> Skins;Actor->GetComponents(Skins);for(const auto* Skin:Skins)if(auto* Mesh=Skin->GetSkeletalMeshAsset()){++TotalMeshes;if(Mesh->GetLODInfo(0)->BuildSettings.bOptimizeForInstancing)++OptimizedMeshes;if(auto* Render=Mesh->GetResourceForRendering())for(const auto& Section:Render->LODRenderData[0].RenderSections)SectionBoneReferences+=Section.BoneMap.Num();}}Report->SetNumberField(TEXT("section_bone_references"),double(SectionBoneReferences));Report->SetNumberField(TEXT("instancing_optimized_meshes"),OptimizedMeshes);Report->SetNumberField(TEXT("skeletal_meshes"),TotalMeshes);
 Report->SetNumberField(TEXT("skin_bone_references"),static_cast<double>(BoneReferences));Report->SetBoolField(TEXT("compact_skin_references"),CompactSkinReferences);
 Report->SetBoolField(TEXT("packed_skin_parts"),PackedSkinParts);
 Report->SetNumberField(TEXT("game_thread_current_ms"),FPlatformTime::ToMilliseconds(GGameThreadTime));Report->SetNumberField(TEXT("render_thread_current_ms"),FPlatformTime::ToMilliseconds(GRenderThreadTime));
 TArray<TSharedPtr<FJsonValue>> Names,Loads;for(const auto& N:CardNames)Names.Add(MakeShared<FJsonValueString>(N));for(double V:LoadMilliseconds)Loads.Add(MakeShared<FJsonValueNumber>(V));
 Report->SetArrayField(TEXT("ordered_cards"),Names);Report->SetArrayField(TEXT("load_ms"),Loads);
 TArray<TSharedPtr<FJsonValue>> Waits,Builds;for(double V:PackageWaitMilliseconds)Waits.Add(MakeShared<FJsonValueNumber>(V));for(double V:CharacterBuildMilliseconds)Builds.Add(MakeShared<FJsonValueNumber>(V));Report->SetArrayField(TEXT("package_wait_ms"),Waits);Report->SetArrayField(TEXT("character_build_ms"),Builds);
 FString Output;FParse::Value(FCommandLine::Get(),TEXT("CrowdReport="),Output);if(Output.IsEmpty())Output=FPaths::ProjectSavedDir()/TEXT("Crowd/Latest.json");
 if(!ControlledReport.IsEmpty())Output=ControlledReport;
 IFileManager::Get().MakeDirectory(*FPaths::GetPath(Output),true);FString Json;FJsonSerializer::Serialize(Report,TJsonWriterFactory<>::Create(&Json));if(FFileHelper::SaveStringToFile(Json,*Output)&&ControlRevision>0&&ControlledFinished)ControlledReportWritten=true;
 if(GEngine)GEngine->AddOnScreenDebugMessage(392,11,FColor::Cyan,FString::Printf(TEXT("Crowd %d/%d | failed %d | frame %.1f ms | CPU %.1f ms | memory %.0f MB"),Residents.Num(),Characters.Num(),Failed,Mean(Frames),Mean(GameFrames),double(FPlatformMemory::GetStats().UsedPhysical)/1048576.));
 const double ReportMs=(FPlatformTime::Seconds()-ReportStarted)*1000.;++CompletedReportCalls;ReportTotalMilliseconds+=ReportMs;ReportMaxMilliseconds=FMath::Max(ReportMaxMilliseconds,ReportMs);PendingReportMilliseconds+=ReportMs;
}
void AOKCrowdScene::EndPlay(const EEndPlayReason::Type Reason){OKReleaseCrowdIndependentGpu(IndependentGpuRuntime);if(FacePending)FacePending->CancelHandle();if(PreviousNearClip>0){SetNearClipPlaneGlobals(PreviousNearClip);PreviousNearClip=-1;}RestoreLoadingBudget();Stopping=true;if(DetailPending)DetailPending->CancelHandle();if(ProfileStarted>0&&!ProfileTaken)FTraceAuxiliary::Stop();if(OwnNamedEvents){GCycleStatsShouldEmitNamedEvents=PreviousNamedEvents;OwnNamedEvents=false;}if(Pending)Pending->CancelHandle();SaveReport();Super::EndPlay(Reason);}

#if WITH_EDITOR
#include "KKImportIndependentCardCommandlet.h"
#include "KKAnimationImporter.h"
#include "KKExactSkinning.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMesh.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/SkyLight.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "GameFramework/WorldSettings.h"
#include "UObject/SavePackage.h"
#include "UObject/StrongObjectPtr.h"
#include "Misc/PackageName.h"
#include "AssetCompilingManager.h"
#include "OKCrowdRoomEnvironment.inl"
#endif
UOKBuildCrowdMapCommandlet::UOKBuildCrowdMapCommandlet(){IsEditor=true;IsClient=true;IsServer=false;LogToConsole=true;}
int32 UOKBuildCrowdMapCommandlet::Main(const FString& Params){
#if WITH_EDITOR
 if(FParse::Param(*Params,TEXT("EnvironmentOnly")))return OKBuildCrowdRoomEnvironment(Params);
 // Keep metadata preflight and actual mesh import in the same native source mode.
 // A preview preflight must never populate baked-head caches for game assets.
 FKKScopedNativeSkinSource NativeSource;
 FString Root=TEXT("C:/portable/KoikatuSunshine"),Destination=TEXT("/Game/Scenes/CrowdRoom"),Error;
 FParse::Value(*Params,TEXT("GameRoot="),Root);FParse::Value(*Params,TEXT("Destination="),Destination);
 if(!Destination.StartsWith(TEXT("/Game/"))||!FPackageName::IsValidLongPackageName(Destination))return 1;
 TArray<FString> Cards;
 for(const TCHAR* Relative:{TEXT("UserData/chara/female"),TEXT("DefaultData/chara/female")}){
  const FString Folder=Root/Relative;TArray<FString> Names;IFileManager::Get().FindFiles(Names,*(Folder/TEXT("*.png")),true,false);Names.Sort();
  for(const auto& Name:Names)if(Cards.Num()<100)Cards.Add(Folder/Name);
 }
 if(Cards.Num()!=100){UE_LOG(LogTemp,Error,TEXT("Need 100 female cards; found %d"),Cards.Num());return 1;}
 FString Selection;for(const auto& Card:Cards)Selection+=Card+TEXT("\n");
 IFileManager::Get().MakeDirectory(*(FPaths::ProjectSavedDir()/TEXT("Crowd")),true);
 FFileHelper::SaveStringToFile(Selection,*(FPaths::ProjectSavedDir()/TEXT("Crowd/SelectedCards.txt")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
 int32 Start=0,End=100;FParse::Value(*Params,TEXT("StartIndex="),Start);FParse::Value(*Params,TEXT("EndIndex="),End);
 if(Start<0||End>100||Start>=End)return 1;
 TArray<TSoftObjectPtr<UKKImportedCharacter>> Assets;
 TArray<TSoftObjectPtr<UKKImportedAnimationLibrary>> Animations;
 for(int32 I=Start;I<End;++I){
  const FString Folder=Destination/FString::Printf(TEXT("Characters/C%03d"),I);
  const FString Package=Folder/TEXT("Character");
  if(!FPackageName::DoesPackageExist(Package)){
   TStrongObjectPtr<UKKImportIndependentCardCommandlet> Import(NewObject<UKKImportIndependentCardCommandlet>());
   const FString Args=FString::Printf(TEXT("-Card=\"%s\" -GameRoot=\"%s\" -Destination=%s -Outfit=0"),*Cards[I],*Root,*Folder);
   UE_LOG(LogTemp,Display,TEXT("Crowd import %d/100 %s"),I+1,*Cards[I]);
   if(Import->Main(Args)!=0){UE_LOG(LogTemp,Error,TEXT("Crowd import failed index=%d; keep evidence and repair before resuming"),I);return 1;}
  }
  // Detect stale selection instead of silently reusing a different character.
  {TStrongObjectPtr<UKKImportedCharacter> Card(LoadObject<UKKImportedCharacter>(nullptr,*(Package+TEXT(".Character"))));TArray<uint8> Bytes;uint8 Hash[20];
   if(!Card||Card->Schema!=2||!FFileHelper::LoadFileToArray(Bytes,*Cards[I]))return 1;
   FSHA1::HashBuffer(Bytes.GetData(),Bytes.Num(),Hash);
   if(Card->SourceHash!=BytesToHex(Hash,20)){UE_LOG(LogTemp,Error,TEXT("Crowd selection changed at %d; choose a fresh destination"),I);return 1;}
  }
  Assets.Add(TSoftObjectPtr<UKKImportedCharacter>(FSoftObjectPath(Package+TEXT(".Character"))));
  const FString AnimationFolder=Folder/TEXT("Walk");
  if(!FPackageName::DoesPackageExist(AnimationFolder/TEXT("Animations"))){
   if(!KKAnimationImporter::Import(Cards[I],Package+TEXT(".Character"),TEXT("studio/anime/00.unity3d"),{TEXT("f_aruki_00_00_01")},AnimationFolder,0,60,Error,Root)){UE_LOG(LogTemp,Error,TEXT("Crowd walk bake failed index=%d error=%s"),I,*Error);return 1;}
  }
  Animations.Add(TSoftObjectPtr<UKKImportedAnimationLibrary>(FSoftObjectPath(AnimationFolder/TEXT("Animations.Animations"))));
  CollectGarbage(RF_NoFlags);
 }
 if(FParse::Param(*Params,TEXT("AssetsOnly"))){UE_LOG(LogTemp,Display,TEXT("Crowd assets complete start=%d end=%d"),Start,End);return 0;}
 if(Start!=0||End!=100){UE_LOG(LogTemp,Error,TEXT("Map assembly requires all 100 indices"));return 1;}
 const FString MapPackage=Destination/TEXT("CrowdRoom");if(FPackageName::DoesPackageExist(MapPackage)){
  TStrongObjectPtr<UWorld> Existing(LoadObject<UWorld>(nullptr,*(MapPackage+TEXT(".CrowdRoom"))));if(!Existing)return 1;
  int32 Groups=0,Instances=0,Lights=0;bool Legacy=false;for(AActor* A:Existing->PersistentLevel->Actors)if(A){Legacy|=A->IsA<AStaticMeshActor>();if(A->IsA<ADirectionalLight>())++Lights;if(auto* H=A->FindComponentByClass<UHierarchicalInstancedStaticMeshComponent>()){++Groups;Instances+=H->GetInstanceCount();auto* M=H->GetMaterial(0);Legacy|=!H->GetStaticMesh()||!M||!M->GetMaterial()->GetUsageByFlag(MATUSAGE_InstancedStaticMeshes);}}
  if(!Legacy&&Groups==11&&Instances==369&&Lights==1){UE_LOG(LogTemp,Display,TEXT("Crowd atrium already complete %s"),*MapPackage);return 0;}
  const FString Upgrade=FString::Printf(TEXT("-Destination=%s -BackupTag=Upgrade_%s"),*Destination,*FGuid::NewGuid().ToString(EGuidFormats::Digits));return OKBuildCrowdRoomEnvironment(Upgrade,Existing.Get());
 }
 auto* Package=CreatePackage(*MapPackage);
 auto* World=UWorld::CreateWorld(EWorldType::Editor,false,TEXT("CrowdRoom"),Package);
 World->SetFlags(RF_Public|RF_Standalone);
 World->GetWorldSettings()->DefaultGameMode=AOKCrowdGameMode::StaticClass();
 auto* Scene=World->SpawnActor<AOKCrowdScene>();Scene->Characters=Assets;Scene->Walks=Animations;Scene->CardNames=Cards;
 FAssetCompilingManager::Get().FinishAllCompilation();World->MarkPackageDirty();FSavePackageArgs Save;Save.TopLevelFlags=RF_Public|RF_Standalone;
 const bool Good=UPackage::SavePackage(Package,World,*FPackageName::LongPackageNameToFilename(MapPackage,FPackageName::GetMapPackageExtension()),Save);
 const FString EnvironmentParams=FString::Printf(TEXT("-Destination=%s -BackupTag=NewMap_%s"),*Destination,*FGuid::NewGuid().ToString(EGuidFormats::Digits));
 const int32 Result=Good?OKBuildCrowdRoomEnvironment(EnvironmentParams,World):1;
 World->DestroyWorld(false);UE_LOG(LogTemp,Display,TEXT("Crowd map saved=%d selected=100 environment=%d path=%s"),Good,Result,*MapPackage);return Result;
#else
 return 1;
#endif
}
