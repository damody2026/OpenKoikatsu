#include "KKRuntimeSkinBounds.h"
#include "Animation/MorphTarget.h"
#include "AnimationRuntime.h"
#include "KKCharAnimInstance.h"
#include "KKRustExtract.h"
#include "KKExactSkinning.h"
#include "ok_extract.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Engine/SkeletalMesh.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "SkeletalRenderPublic.h"
#include "BoneContainer.h"
#include "HAL/IConsoleManager.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

namespace {
TArrayView<const FTransform> GAffineInput;
const USkinnedMeshComponent* GAffineComponent=nullptr;
uint64 GAffineEpoch=0,GAffineSerial=0;
TAutoConsoleVariable<int32> CVarSharedAffinePose(TEXT("ok.Animation.AffinePoseReuse"),1,
 TEXT("Reuse exact runtime affine pose matrices across compatible display components; 0 uses the original path."));
TAutoConsoleVariable<int32> CVarAffinePoseParity(TEXT("ok.Animation.AffinePoseParity"),0,
 TEXT("Check shared affine results against independently computed original matrices."));
struct FSharedAffinePose {
 TArray<FName> Names;
 TArray<int32> Parents;
 TArray<FTransform> Locals,EngineLocals;
 TArray<FMatrix44f> World,EngineInverse;
 TArray<uint8> InverseValid,WorldValid;
 uint64 Age=0,BatchEpoch=0;
 bool Matches(const FReferenceSkeleton& Ref,TArrayView<const FTransform> Pose,uint64 Epoch=0) const {
  if(Locals.Num()!=Pose.Num()||Parents.Num()!=Ref.GetNum())return false;
  for(int32 B=0;B<Pose.Num();++B)if(Parents[B]!=Ref.GetParentIndex(B)||Names[B]!=Ref.GetBoneName(B)||(!(Epoch&&BatchEpoch==Epoch)&&!Locals[B].Equals(Pose[B],0)))return false;
  return true;
 }
 void Build(const FReferenceSkeleton& Ref,TArrayView<const FTransform> Pose){
  const int32 Count=Pose.Num();Names.SetNum(Count);Parents.SetNum(Count);Locals.SetNum(Count);World.SetNum(Count);
  EngineLocals.SetNum(Count);EngineInverse.SetNum(Count);InverseValid.SetNumZeroed(Count);FMemory::Memzero(InverseValid.GetData(),Count);
  WorldValid.SetNumZeroed(Count);FMemory::Memzero(WorldValid.GetData(),Count);BatchEpoch=0;
  for(int32 B=0;B<Count;++B){Names[B]=Ref.GetBoneName(B);Parents[B]=Ref.GetParentIndex(B);Locals[B]=Pose[B];
  }
 }
 const FMatrix44f& WorldAt(int32 Bone){
  if(!WorldValid[Bone]){
   TArray<int32,TInlineAllocator<32>> Chain;for(int32 B=Bone;B>=0&&!WorldValid[B];B=Parents[B])Chain.Add(B);
   for(int32 I=Chain.Num()-1;I>=0;--I){const int32 B=Chain[I];World[B]=FMatrix44f(Locals[B].ToMatrixWithScale());if(Parents[B]>=0)World[B]=World[B]*World[Parents[B]];WorldValid[B]=1;}
  }
  return World[Bone];
 }
 bool Inverse(int32 Bone,const FTransform& Approximate,FMatrix44f& Out){
  if(!InverseValid[Bone]||!EngineLocals[Bone].Equals(Approximate,0)){
   const FMatrix44f Matrix(Approximate.ToMatrixWithScale());if(FMath::Abs(Matrix.Determinant())<1.e-12f)return false;
   EngineInverse[Bone]=Matrix.Inverse();EngineLocals[Bone]=Approximate;InverseValid[Bone]=1;
  }
  Out=EngineInverse[Bone];return true;
 }
};
FSharedAffinePose* SharedAffinePose(const FReferenceSkeleton& Ref,TArrayView<const FTransform> Pose,const USkinnedMeshComponent* Component=nullptr){
 if(!IsInGameThread()||CVarSharedAffinePose.GetValueOnGameThread()==0||Pose.Num()!=Ref.GetNum()||Pose.Num()>16384)return nullptr;
 // Four bounded, value-owned entries. Explicit/manual updates in the same
 // engine frame are distinguished by their complete pose, never a frame ID.
 static FSharedAffinePose Entries[4];static uint64 Serial=0;
 const uint64 Epoch=Component&&Component==GAffineComponent&&Pose.Num()==GAffineInput.Num()?GAffineEpoch:0;
 for(auto& Entry:Entries)if(Entry.Matches(Ref,Pose,Epoch)){Entry.BatchEpoch=Epoch;Entry.Age=++Serial;return &Entry;}
 FSharedAffinePose* Oldest=&Entries[0];for(auto& Entry:Entries)if(Entry.Age<Oldest->Age)Oldest=&Entry;
 Oldest->Build(Ref,Pose);Oldest->BatchEpoch=Epoch;Oldest->Age=++Serial;return Oldest;
}
}

FKKRuntimeAffinePoseScope::FKKRuntimeAffinePoseScope(TArrayView<const FTransform> Pose,const USkinnedMeshComponent* Component){
 check(IsInGameThread());PreviousInput=GAffineInput;PreviousComponent=GAffineComponent;PreviousEpoch=GAffineEpoch;
 if(!Component){GAffineInput=Pose;GAffineComponent=nullptr;GAffineEpoch=++GAffineSerial;}
 else GAffineComponent=GAffineEpoch&&GAffineInput.GetData()==Pose.GetData()&&GAffineInput.Num()==Pose.Num()?Component:nullptr;
}
FKKRuntimeAffinePoseScope::~FKKRuntimeAffinePoseScope(){GAffineInput=PreviousInput;GAffineComponent=PreviousComponent;GAffineEpoch=PreviousEpoch;}

bool FKKRuntimeSkinBounds::Calculate(const USkinnedMeshComponent& Component,const FTransform& LocalToWorld,FBoxSphereBounds& Result)
{
 // Engine end-of-frame bounds updates may run on workers. This cache owns
 // mutable palette publication state; the engine's conservative bounds path
 // remains the worker-safe fallback rather than mutating that state there.
 const auto* Mesh=Cast<USkeletalMesh>(Component.GetSkinnedAsset());
 if(!IsInGameThread()){
  // The last posed result (at most a frame old), widened a little for the motion since; never the bind box.
  if(!LastLocal.IsValid||Asset.Get()!=Mesh)return false;
  Result=FBoxSphereBounds(LastLocal.ExpandBy(5.)).TransformBy(LocalToWorld);
  Result.BoxExtent*=Component.BoundsScale;Result.SphereRadius*=Component.BoundsScale;return true;
 }
 if(!Mesh||!Mesh->GetResourceForRendering()||Mesh->GetResourceForRendering()->LODRenderData.IsEmpty())return false;
 const auto& LOD=Mesh->GetResourceForRendering()->LODRenderData[0];
 const auto* Weights=Component.GetSkinWeightBuffer(0);
 if(!Weights||!Weights->GetDataVertexBuffer()->GetWeightData()||!LOD.StaticVertexBuffers.PositionVertexBuffer.GetVertexData())return false;
 const int32 NV=LOD.GetNumVertices();
 if(Weights->GetNumVertices()!=uint32(NV))return false;
 if(Asset.Get()!=Mesh||WeightBuffer!=Weights||VertexCount!=NV||MorphCount!=Mesh->GetMorphTargets().Num()){
  Asset=const_cast<USkeletalMesh*>(Mesh);WeightBuffer=Weights;VertexCount=NV;MorphCount=Mesh->GetMorphTargets().Num();Bones.Reset();Reference.Reset();Morphs.Reset();
  TMap<int32,int32> Slots;
  for(const auto& Section:LOD.RenderSections)for(auto Bone:Section.BoneMap)if(!Slots.Contains(Bone)){Slots.Add(Bone,Bones.Add(Bone));Reference.Add(FBox3f(ForceInit));}
  const auto AddVertex=[&](int32 V,const FVector3f& Point,TArray<FBox3f>& Boxes){
   int32 Section,Local;LOD.GetSectionFromVertexIndex(V,Section,Local);if(!LOD.RenderSections.IsValidIndex(Section))return;
   const auto& Map=LOD.RenderSections[Section].BoneMap;
   for(uint32 I=0;I<Weights->GetMaxBoneInfluences();++I){if(!Weights->GetBoneWeight(V,I))continue;const auto Index=Weights->GetBoneIndex(V,I);if(!Map.IsValidIndex(Index))continue;if(const int32* Slot=Slots.Find(Map[Index]))Boxes[*Slot]+=Point;}
  };
  for(int32 V=0;V<NV;++V)AddVertex(V,LOD.StaticVertexBuffers.PositionVertexBuffer.VertexPosition(V),Reference);
  for(const auto& TargetRef:Mesh->GetMorphTargets()){const UMorphTarget* Target=TargetRef.Get();if(Target&&Target->HasDataForLOD(0)){
   TArray<FBox3f> Boxes;Boxes.Init(FBox3f(FVector3f::ZeroVector,FVector3f::ZeroVector),Bones.Num());
   for(auto Delta=Target->GetDeltaIteratorForLOD(0);!Delta.AtEnd();++Delta)if(Delta->SourceIdx<uint32(NV))AddVertex(Delta->SourceIdx,Delta->PositionDelta,Boxes);
   Morphs.Add(Target,MoveTemp(Boxes));
  }}
 }
 const auto& Transforms=Component.GetComponentSpaceTransforms();const auto& Override=Component.GetRefPoseOverride();const auto& Inverse=Override.IsValid()?Override->RefBasesInvMatrix:Mesh->GetRefBasesInvMatrix();
 TArray<FMatrix44f> LeaderMatrices;
 const TArray<FMatrix44f>* SharedLeaderMatrices=nullptr;
 const auto* OwnedPoseable=Cast<UKKRuntimePoseableMeshComponent>(&Component);
 const auto* OwnedSkeletal=Cast<UKKRuntimeSkeletalMeshComponent>(&Component);
 auto* Exact=OwnedPoseable?OwnedPoseable->VerifiedExactLeader():OwnedSkeletal?OwnedSkeletal->VerifiedExactLeader():nullptr;
 if(Exact)SharedLeaderMatrices=&Exact->BoundsSkinPalette();
 else if(Component.LeaderPoseComponent.IsValid())Component.GetCurrentRefToLocalMatrices(LeaderMatrices,0);
 const float Maximum=UE::SkeletalRender::Settings::GetMorphTargetMaxBlendWeight();
 FBox3f Posed(ForceInit);
 for(int32 Slot=0;Slot<Bones.Num();++Slot){
  const int32 Bone=Bones[Slot];if(!Reference[Slot].IsValid||!Inverse.IsValidIndex(Bone))continue;
  FBox3f Envelope=Reference[Slot];
  // Zero belongs to each delta envelope: vertices not affected by a morph
  // must remain included. Negative weights reverse its interval endpoints.
  for(const auto& Active:Component.ActiveMorphTargets){
   if(!Component.MorphTargetWeights.IsValidIndex(Active.Value))continue;
   const float Weight=Component.MorphTargetWeights[Active.Value];if(!FMath::IsFinite(Weight)||FMath::Abs(Weight)<UE_SMALL_NUMBER||FMath::Abs(Weight)>Maximum)continue;
   if(const auto* Boxes=Morphs.Find(Active.Key)){
    const auto& Delta=(*Boxes)[Slot];Envelope.Min+=(Weight>=0?Delta.Min:Delta.Max)*Weight;Envelope.Max+=(Weight>=0?Delta.Max:Delta.Min)*Weight;
   }
  }
  FMatrix44f Palette;
  if(SharedLeaderMatrices){if(!SharedLeaderMatrices->IsValidIndex(Bone))return false;Palette=(*SharedLeaderMatrices)[Bone];}
  else if(!LeaderMatrices.IsEmpty()){if(!LeaderMatrices.IsValidIndex(Bone))return false;Palette=LeaderMatrices[Bone];}
  else {if(!Transforms.IsValidIndex(Bone))return false;Palette=Inverse[Bone]*FMatrix44f(Transforms[Bone].ToMatrixWithScale());}
  Posed+=Envelope.TransformBy(Palette);
 }
 if(!Posed.IsValid||Posed.Min.ContainsNaN()||Posed.Max.ContainsNaN())return false;
 // Small allowance for render-side quantization and outline extrusion.
 LastLocal=FBox(FVector(Posed.Min)-FVector(1),FVector(Posed.Max)+FVector(1));
 Result=FBoxSphereBounds(LastLocal).TransformBy(LocalToWorld);
 Result.BoxExtent*=Component.BoundsScale;Result.SphereRadius*=Component.BoundsScale;
 return true;
}

FBoxSphereBounds UKKRuntimePoseableMeshComponent::CalcBounds(const FTransform& LocalToWorld) const
{
 FBoxSphereBounds Result;return RuntimeBounds.Calculate(*this,LocalToWorld,Result)?Result:Super::CalcBounds(LocalToWorld);
}
void UKKRuntimePoseableMeshComponent::UpdateFollowerComponent()
{
 Super::UpdateFollowerComponent();
 // As UKKRuntimeSkeletalMeshComponent: posed bounds from the game thread for the worker-side transform update.
 if(IsInGameThread()){FBoxSphereBounds Posed;RuntimeBounds.Calculate(*this,GetComponentTransform(),Posed);}
}

void FKKRuntimeAffinePalette::ClearDirect(){
 if(HasDirectPalette){Snapshot.Reset();PreviousLocals.Reset();PreviousApproximate.Reset();}
 DirectAsset.Reset();HasDirectPalette=false;DirectMatrices.Reset();DirectCarriers.Reset();
}
bool FKKRuntimeAffinePalette::Publish(USkinnedMeshComponent& Component,TConstArrayView<FMatrix44f> RefToLocal,FString& Error){
 check(IsInGameThread());TRACE_CPUPROFILER_EVENT_SCOPE(KK_PublishNativeSkinPalette);
 auto* Mesh=Component.GetSkinnedAsset();
 if(!Mesh||Component.LeaderPoseComponent.IsValid()||RefToLocal.Num()!=Mesh->GetRefSkeleton().GetNum()||RefToLocal.IsEmpty()){
  Error=TEXT("Native skin palette asset/count/leader mismatch");return false;
 }
 auto& Carrier=Component.GetEditableComponentSpaceTransforms();
 if(Carrier.Num()!=RefToLocal.Num()){Error=TEXT("Native skin palette carrier not initialized");return false;}
 TArray<FTransform> NextCarrier=Carrier;
 auto Next=MakeShared<FSkelMeshRefPoseOverride>();Next->RefBonePoses=Mesh->GetRefSkeleton().GetRefBonePose();Next->RefBasesInvMatrix.SetNumUninitialized(RefToLocal.Num());
 for(int32 B=0;B<RefToLocal.Num();++B){
  const auto& Matrix=RefToLocal[B];
  for(int32 R=0;R<4;++R)for(int32 C=0;C<4;++C)if(!FMath::IsFinite(Matrix.M[R][C])){Error=TEXT("Native skin palette nonfinite matrix");return false;}
  if(Matrix.M[0][3]!=0||Matrix.M[1][3]!=0||Matrix.M[2][3]!=0||Matrix.M[3][3]!=1){Error=TEXT("Native skin palette must be affine");return false;}
  if(NextCarrier[B].ContainsNaN()){Error=TEXT("Native skin palette nonfinite carrier");return false;}
  FMatrix44f Engine(NextCarrier[B].ToMatrixWithScale());
  // Collapsed SOURCE matrices are legal. Only the engine carrier needs an
  // inverse; replace a collapsed carrier without decomposing the source.
  if(FMath::Abs(Engine.Determinant())<1.e-12f){NextCarrier[B]=FTransform::Identity;Engine=FMatrix44f::Identity;}
  Next->RefBasesInvMatrix[B]=Matrix*Engine.Inverse();
  for(int32 R=0;R<4;++R)for(int32 C=0;C<4;++C)if(!FMath::IsFinite(Next->RefBasesInvMatrix[B].M[R][C])){Error=TEXT("Native skin palette carrier inverse invalid");return false;}
 }
 // Never mutate an existing snapshot: a previous frame can still own it.
 TArray<FMatrix44f> Owned;Owned.Append(RefToLocal.GetData(),RefToLocal.Num());
 Carrier=MoveTemp(NextCarrier);DirectCarriers=Carrier;DirectMatrices=MoveTemp(Owned);DirectAsset=Mesh;HasDirectPalette=true;Snapshot=MoveTemp(Next);Error.Reset();return true;
}
bool UKKRuntimePoseableMeshComponent::PublishExactSkinPalette(TConstArrayView<FMatrix44f> RefToLocal,FString& Error){
 if(Super::GetRefPoseOverride().IsValid()){Error=TEXT("Native skin palette conflicts with explicit engine override");return false;}
 if(!AffinePalette.Publish(*this,RefToLocal,Error))return false;
 // Publish the carrier and its matching override together. Unreal may read
 // the other component-space buffer until FinalizeBoneTransform flips it.
 bNeedToFlipSpaceBaseBuffers=true;Super::FinalizeBoneTransform();++PosePublication;
 InvalidateCachedBounds();UpdateBounds();MarkRenderDynamicDataDirty();return true;
}
bool UKKRuntimePoseableMeshComponent::PublishNativeSkinPalette(const FOkSession& Session,int32 Part,TConstArrayView<int32> Slots,FString& Error){
 check(IsInGameThread());OkSkinPaletteView Raw{};TArray<float> Values;TArray<FMatrix44f> Palette;
 if(!GetSkinnedAsset()||Slots.Num()!=GetSkinnedAsset()->GetRefSkeleton().GetNum()){Error=TEXT("Native part palette rig binding count mismatch");return false;}
 if(Session.SkinPalette(Part,Raw,Values)<=0){Error=TEXT("Native part palette unavailable");return false;}
 return OKPrepareExactSkinPalette(Raw,Values,Slots,Palette,Error)&&PublishExactSkinPalette(Palette,Error);
}
void UKKRuntimePoseableMeshComponent::ClearExactSkinPalette(){
 check(IsInGameThread());AffinePalette.ClearDirect();FinalizeBoneTransform();InvalidateCachedBounds();UpdateBounds();MarkRenderDynamicDataDirty();
}
void FKKRuntimeAffinePalette::Update(USkinnedMeshComponent& Component,TArrayView<const FTransform> Locals)
{
 TRACE_CPUPROFILER_EVENT_SCOPE(KK_RuntimeAffinePalette);
 if(HasDirectPalette&&DirectAsset.IsValid()&&DirectAsset.Get()==Component.GetSkinnedAsset()&&!Component.LeaderPoseComponent.IsValid()){
  const auto& Carrier=Component.GetEditableComponentSpaceTransforms();bool Changed=Carrier.Num()!=DirectCarriers.Num();
  if(!Changed)for(int32 B=0;B<Carrier.Num();++B)if(!Carrier[B].Equals(DirectCarriers[B],0)){Changed=true;break;}
  if(!Changed)return;
  FString Error;if(Publish(Component,DirectMatrices,Error))return;
 }
 ClearDirect();
 if(!Component.GetSkinnedAsset()){Snapshot.Reset();return;}
 const auto& Ref=Component.GetSkinnedAsset()->GetRefSkeleton();auto& Approximate=Component.GetEditableComponentSpaceTransforms();
 if(Locals.Num()!=Ref.GetNum()||Approximate.Num()!=Locals.Num()||Component.LeaderPoseComponent.IsValid()){Snapshot.Reset();return;}
 const bool AssetChanged=Asset.Get()!=Component.GetSkinnedAsset()||RequiredBonesDirty;
 if(AssetChanged){
  Asset=Component.GetSkinnedAsset();ReferenceInverse.SetNumUninitialized(Ref.GetNum());World.SetNumUninitialized(Ref.GetNum());UsedBones.Reset();const auto* RenderData=Asset->GetResourceForRendering();if(RenderData)for(const auto& LOD:RenderData->LODRenderData)for(const auto& Section:LOD.RenderSections)for(auto Bone:Section.BoneMap)UsedBones.Add(Bone);
  for(int32 Bone:ExtraUsedBones)if(Bone>=0&&Bone<Ref.GetNum())UsedBones.Add(Bone);
  RequiredBonesDirty=false;
  TSet<int32> Needed;for(int32 Bone:UsedBones)for(int32 B=Bone;B>=0&&!Needed.Contains(B);B=Ref.GetParentIndex(B))Needed.Add(B);NeededBones=Needed.Array();NeededBones.Sort();
  for(int32 B=0;B<Ref.GetNum();++B){World[B]=FMatrix44f(Ref.GetRefBonePose()[B].ToMatrixWithScale());const int32 Parent=Ref.GetParentIndex(B);if(Parent>=0)World[B]=World[B]*World[Parent];ReferenceInverse[B]=World[B].Inverse();}
 }
 // A collapsed source scale is legal. Use an invertible renderer carrier;
 // the full source matrix below restores its collapsed geometry on the GPU.
 // Local bone poses and source animation data remain untouched.
 for(int32 B:UsedBones){const FVector Scale=Approximate[B].GetScale3D();if(FMath::Abs(Scale.X*Scale.Y*Scale.Z)<1.e-12)Approximate[B].SetScale3D(FVector::OneVector);}
 bool Changed=AssetChanged||PreviousLocals.Num()!=Locals.Num()||PreviousApproximate.Num()!=Approximate.Num();
 if(!Changed)for(int32 B:NeededBones)if(!PreviousLocals[B].Equals(Locals[B],0)){Changed=true;break;}
 if(!Changed)for(int32 B:UsedBones)if(!PreviousApproximate[B].Equals(Approximate[B],0)){Changed=true;break;}
 if(!Changed&&Snapshot.IsValid())return;
 auto Next=MakeShared<FSkelMeshRefPoseOverride>();Next->RefBonePoses=Ref.GetRefBonePose();Next->RefBasesInvMatrix=ReferenceInverse;
 auto* Shared=SharedAffinePose(Ref,Locals,&Component);
 TArray<FMatrix44f> Oracle;
 if(Shared&&CVarAffinePoseParity.GetValueOnGameThread()){
  Oracle.SetNumUninitialized(Locals.Num());
  for(int32 B:NeededBones){Oracle[B]=FMatrix44f(Locals[B].ToMatrixWithScale());const int32 Parent=Ref.GetParentIndex(B);if(Parent>=0)Oracle[B]=Oracle[B]*Oracle[Parent];}
 }
 for(int32 B:NeededBones){
  if(Shared)World[B]=Shared->WorldAt(B);
  else {World[B]=FMatrix44f(Locals[B].ToMatrixWithScale());const int32 Parent=Ref.GetParentIndex(B);if(Parent>=0)World[B]=World[B]*World[Parent];}
 }
 for(int32 B:UsedBones){
  FMatrix44f EngineInverse;
  if(Shared){if(!Shared->Inverse(B,Approximate[B],EngineInverse)){Snapshot.Reset();return;}}
  else {const FMatrix44f EngineWorld(Approximate[B].ToMatrixWithScale());if(FMath::Abs(EngineWorld.Determinant())<1.e-12f){Snapshot.Reset();return;}EngineInverse=EngineWorld.Inverse();}
  // Engine multiplies inverse-bind * TRS-world. Cancel its lossy TRS-world,
  // then supply the full source affine palette, including non-uniform shear.
  Next->RefBasesInvMatrix[B]=ReferenceInverse[B]*World[B]*EngineInverse;
  if(!Oracle.IsEmpty()){
   const FMatrix44f Expected=ReferenceInverse[B]*Oracle[B]*FMatrix44f(Approximate[B].ToMatrixWithScale()).Inverse();
   for(int32 Row=0;Row<4;++Row)for(int32 Col=0;Col<4;++Col)checkf(Expected.M[Row][Col]==Next->RefBasesInvMatrix[B].M[Row][Col],TEXT("Shared affine parity failed bone %d"),B);
  }
 }
 PreviousLocals.SetNumUninitialized(Locals.Num());for(int32 B:NeededBones)PreviousLocals[B]=Locals[B];PreviousApproximate.SetNumUninitialized(Approximate.Num());for(int32 B:UsedBones)PreviousApproximate[B]=Approximate[B];Snapshot=MoveTemp(Next);
}
void UKKRuntimePoseableMeshComponent::FinalizeBoneTransform()
{
 if(Super::GetRefPoseOverride().IsValid())AffinePalette.Snapshot.Reset();
 else AffinePalette.Update(*this,BoneSpaceTransforms);
 if(AffinePalette.HasDirectPalette)bNeedToFlipSpaceBaseBuffers=true;
 Super::FinalizeBoneTransform();++PosePublication;
}
UKKRuntimePoseableMeshComponent* UKKRuntimePoseableMeshComponent::VerifiedExactLeader() const {
 return CandidateCompatible&&ExactLeader.IsValid()&&LeaderPoseComponent.Get()==ExactLeader.Get()&&CandidateAsset.Get()==GetSkinnedAsset()&&CandidateLeaderAsset.Get()==ExactLeader->GetSkinnedAsset()?ExactLeader.Get():nullptr;
}
UKKRuntimePoseableMeshComponent* UKKRuntimeSkeletalMeshComponent::VerifiedExactLeader() const {
 return CandidateCompatible&&ExactLeader.IsValid()&&LeaderPoseComponent.Get()==ExactLeader.Get()&&CandidateAsset.Get()==GetSkinnedAsset()&&CandidateLeaderAsset.Get()==ExactLeader->GetSkinnedAsset()?ExactLeader.Get():nullptr;
}
const TArray<FMatrix44f>& UKKRuntimePoseableMeshComponent::BoundsSkinPalette() const {
 check(IsInGameThread());const auto* Asset=GetSkinnedAsset();const auto* Override=GetRefPoseOverride().Get();
 if(BoundsPalettePublication!=PosePublication||BoundsPaletteAsset.Get()!=Asset||BoundsPaletteOverride!=Override){
  TRACE_CPUPROFILER_EVENT_SCOPE(KK_SharedBoundsPalette);
  const int32 Count=Asset?Asset->GetRefSkeleton().GetNum():0;
  BoundsPaletteBones.SetNumUninitialized(Count);for(int32 I=0;I<Count;++I)BoundsPaletteBones[I]=I;
  BoundsPalette.Reset();if(Count)GetCurrentRefToLocalMatrices(BoundsPalette,0,&BoundsPaletteBones);
  BoundsPalettePublication=PosePublication;BoundsPaletteAsset=const_cast<USkinnedAsset*>(Asset);BoundsPaletteOverride=Override;
 }
 return BoundsPalette;
}
void UKKRuntimeSkeletalMeshComponent::FinalizeBoneTransform()
{
 if(Super::GetRefPoseOverride().IsValid())AffinePalette.Snapshot.Reset();
 else AffinePalette.Update(*this,GetBoneSpaceTransformsView());
 Super::FinalizeBoneTransform();
}
const TSharedPtr<FSkelMeshRefPoseOverride>& UKKRuntimePoseableMeshComponent::GetRefPoseOverride() const
{
 if(AffinePalette.HasDirectPalette&&AffinePalette.DirectAsset.Get()!=GetSkinnedAsset())return Super::GetRefPoseOverride();
 if(ExactLeader.IsValid()&&LeaderPoseComponent.Get()==ExactLeader.Get())return ExactLeader->GetRefPoseOverride();
 return AffinePalette.Snapshot.IsValid()?AffinePalette.Snapshot:Super::GetRefPoseOverride();
}
bool UKKRuntimePoseableMeshComponent::TryUseExactLeader(UKKRuntimePoseableMeshComponent* Leader){
 if(Leader&&(AffinePalette.HasDirectPalette||Leader->AffinePalette.HasDirectPalette))return false;
 if(!Leader||Leader==this||!GetSkinnedAsset()||!Leader->GetSkinnedAsset()||Leader->LeaderPoseComponent.IsValid()||Super::GetRefPoseOverride().IsValid()||!GetRelativeTransform().Equals(Leader->GetRelativeTransform(),0)){
  if(ExactLeader.IsValid()&&LeaderPoseComponent.Get()==ExactLeader.Get())SetLeaderPoseComponent(nullptr,true);
  ExactLeader.Reset();CandidateLeader.Reset();CandidateAsset.Reset();CandidateLeaderAsset.Reset();CandidateCompatible=false;return false;
 }
 // Builder-owned transient assets are immutable for their component lifetime.
 // Changing either asset reruns every compatibility check before sharing.
 if(CandidateLeader.Get()==Leader&&CandidateAsset.Get()==GetSkinnedAsset()&&CandidateLeaderAsset.Get()==Leader->GetSkinnedAsset()&&(!CandidateCompatible||(ExactLeader.Get()==Leader&&LeaderPoseComponent.Get()==Leader)))return CandidateCompatible;
 if(ExactLeader.IsValid()){SetLeaderPoseComponent(nullptr,true);ExactLeader.Reset();}
 CandidateLeader=Leader;CandidateAsset=GetSkinnedAsset();CandidateLeaderAsset=Leader->GetSkinnedAsset();CandidateCompatible=false;
 const auto& A=GetSkinnedAsset()->GetRefSkeleton();const auto& B=Leader->GetSkinnedAsset()->GetRefSkeleton();
 if(A.GetNum()!=B.GetNum())return false;
 for(int32 Bone=0;Bone<A.GetNum();++Bone)if(A.GetBoneName(Bone)!=B.GetBoneName(Bone)||A.GetParentIndex(Bone)!=B.GetParentIndex(Bone)||!A.GetRefBonePose()[Bone].Equals(B.GetRefBonePose()[Bone],0))return false;
 const auto& AI=GetSkinnedAsset()->GetRefBasesInvMatrix();const auto& BI=Leader->GetSkinnedAsset()->GetRefBasesInvMatrix();if(AI.Num()!=BI.Num())return false;
 for(int32 Bone=0;Bone<AI.Num();++Bone)for(int32 R=0;R<4;++R)for(int32 C=0;C<4;++C)if(AI[Bone].M[R][C]!=BI[Bone].M[R][C])return false;
 const auto* Data=GetSkinnedAsset()->GetResourceForRendering();if(!Data)return false;
 for(const auto& LOD:Data->LODRenderData)for(const auto& Section:LOD.RenderSections)for(int32 Bone:Section.BoneMap)if(!Leader->AffinePalette.ExtraUsedBones.Contains(Bone)){Leader->AffinePalette.ExtraUsedBones.Add(Bone);Leader->AffinePalette.RequiredBonesDirty=true;}
 ExactLeader=Leader;AffinePalette.Snapshot.Reset();CandidateCompatible=true;SetLeaderPoseComponent(Leader,true,false);
 const auto& Map=GetLeaderBoneMap();if(Map.Num()!=A.GetNum()){SetLeaderPoseComponent(nullptr,true);ExactLeader.Reset();CandidateCompatible=false;return false;}
 for(int32 Bone=0;Bone<Map.Num();++Bone)if(Map[Bone]!=Bone){SetLeaderPoseComponent(nullptr,true);ExactLeader.Reset();CandidateCompatible=false;return false;}
 UE_LOG(LogTemp,Display,TEXT("OK exact pose leader component=%s leader=%s"),*GetName(),*Leader->GetName());return true;
}
void UKKRuntimePoseableMeshComponent::VerifyExactLeader(TArrayView<const FTransform> Pose) const{
 if(!ExactLeader.IsValid()||!CVarAffinePoseParity.GetValueOnGameThread())return;
 const auto& Ref=GetSkinnedAsset()->GetRefSkeleton();const auto& Snapshot=GetRefPoseOverride();
 check(Pose.Num()==Ref.GetNum()&&Snapshot.IsValid()&&Snapshot->RefBasesInvMatrix.Num()==Ref.GetNum());
 const auto& Approximate=ExactLeader->GetComponentSpaceTransforms();TArray<FMatrix44f> Reference,World;Reference.SetNumUninitialized(Ref.GetNum());World.SetNumUninitialized(Ref.GetNum());
 for(int32 Bone=0;Bone<Ref.GetNum();++Bone){Reference[Bone]=FMatrix44f(Ref.GetRefBonePose()[Bone].ToMatrixWithScale());World[Bone]=FMatrix44f(Pose[Bone].ToMatrixWithScale());const int32 Parent=Ref.GetParentIndex(Bone);if(Parent>=0){Reference[Bone]=Reference[Bone]*Reference[Parent];World[Bone]=World[Bone]*World[Parent];}}
 TSet<int32> Used;for(const auto& LOD:GetSkinnedAsset()->GetResourceForRendering()->LODRenderData)for(const auto& Section:LOD.RenderSections)for(int32 Bone:Section.BoneMap)Used.Add(Bone);
 for(int32 Bone:Used){const FMatrix44f Expected=Reference[Bone].Inverse()*World[Bone]*FMatrix44f(Approximate[Bone].ToMatrixWithScale()).Inverse();
  for(int32 R=0;R<4;++R)for(int32 C=0;C<4;++C)checkf(Expected.M[R][C]==Snapshot->RefBasesInvMatrix[Bone].M[R][C],TEXT("Exact leader differs from original palette component=%s bone=%d"),*GetName(),Bone);
 }
}
bool UKKRuntimeSkeletalMeshComponent::TryUseExactLeader(UKKRuntimePoseableMeshComponent* Leader){
 if(Leader&&Leader->AffinePalette.HasDirectPalette)return false;
 if(!Leader||!GetSkinnedAsset()||!Leader->GetSkinnedAsset()||Leader->LeaderPoseComponent.IsValid()||Super::GetRefPoseOverride().IsValid()||!GetRelativeTransform().Equals(Leader->GetRelativeTransform(),0)||(GetAnimInstance()&&!Cast<UKKCharAnimInstance>(GetAnimInstance()))){
  if(ExactLeader.IsValid()&&LeaderPoseComponent.Get()==ExactLeader.Get())SetLeaderPoseComponent(nullptr,true);
  ExactLeader.Reset();CandidateLeader.Reset();CandidateAsset.Reset();CandidateLeaderAsset.Reset();CandidateCompatible=false;return false;
 }
 // Builder-owned transient assets are immutable for their component lifetime.
 // Changing either asset reruns every compatibility check before sharing.
 if(CandidateLeader.Get()==Leader&&CandidateAsset.Get()==GetSkinnedAsset()&&CandidateLeaderAsset.Get()==Leader->GetSkinnedAsset()&&(!CandidateCompatible||(ExactLeader.Get()==Leader&&LeaderPoseComponent.Get()==Leader)))return CandidateCompatible;
 if(ExactLeader.IsValid()){SetLeaderPoseComponent(nullptr,true);ExactLeader.Reset();}
 CandidateLeader=Leader;CandidateAsset=GetSkinnedAsset();CandidateLeaderAsset=Leader->GetSkinnedAsset();CandidateCompatible=false;
 const auto& A=GetSkinnedAsset()->GetRefSkeleton();const auto& B=Leader->GetSkinnedAsset()->GetRefSkeleton();
 if(A.GetNum()!=B.GetNum())return false;
 for(int32 Bone=0;Bone<A.GetNum();++Bone)if(A.GetBoneName(Bone)!=B.GetBoneName(Bone)||A.GetParentIndex(Bone)!=B.GetParentIndex(Bone)||!A.GetRefBonePose()[Bone].Equals(B.GetRefBonePose()[Bone],0))return false;
 const auto& AI=GetSkinnedAsset()->GetRefBasesInvMatrix();const auto& BI=Leader->GetSkinnedAsset()->GetRefBasesInvMatrix();if(AI.Num()!=BI.Num())return false;
 for(int32 Bone=0;Bone<AI.Num();++Bone)for(int32 R=0;R<4;++R)for(int32 C=0;C<4;++C)if(AI[Bone].M[R][C]!=BI[Bone].M[R][C])return false;
 const auto* Data=GetSkinnedAsset()->GetResourceForRendering();if(!Data)return false;
 for(const auto& LOD:Data->LODRenderData)for(const auto& Section:LOD.RenderSections)for(int32 Bone:Section.BoneMap)if(!Leader->AffinePalette.ExtraUsedBones.Contains(Bone)){Leader->AffinePalette.ExtraUsedBones.Add(Bone);Leader->AffinePalette.RequiredBonesDirty=true;}
 ExactLeader=Leader;AffinePalette.Snapshot.Reset();CandidateCompatible=true;SetLeaderPoseComponent(Leader,true,false);
 const auto& Map=GetLeaderBoneMap();if(Map.Num()!=A.GetNum()){SetLeaderPoseComponent(nullptr,true);ExactLeader.Reset();CandidateCompatible=false;return false;}
 for(int32 Bone=0;Bone<Map.Num();++Bone)if(Map[Bone]!=Bone){SetLeaderPoseComponent(nullptr,true);ExactLeader.Reset();CandidateCompatible=false;return false;}
 UE_LOG(LogTemp,Display,TEXT("OK exact pose leader component=%s leader=%s"),*GetName(),*Leader->GetName());return true;
}
const TSharedPtr<FSkelMeshRefPoseOverride>& UKKRuntimeSkeletalMeshComponent::GetRefPoseOverride() const
{
 if(ExactLeader.IsValid()&&LeaderPoseComponent.Get()==ExactLeader.Get())return ExactLeader->GetRefPoseOverride();
 return AffinePalette.Snapshot.IsValid()?AffinePalette.Snapshot:Super::GetRefPoseOverride();
}
void UKKRuntimeSkeletalMeshComponent::UpdateFollowerComponent()
{
 Super::UpdateFollowerComponent();
 // Refresh the posed bounds here on the game thread; the end-of-frame transform update reuses them on workers.
 if(IsInGameThread()){FBoxSphereBounds Posed;RuntimeBounds.Calculate(*this,GetComponentTransform(),Posed);}
 // USkeletalMeshComponent clears a follower's morph targets and appends them back only under a skeletal mesh leader;
 // under the poseable body leader the face parts' expressions (SetMorphTarget) would vanish each time the leader
 // refreshes its followers. Append them here with the same engine semantics.
 if(Cast<USkeletalMeshComponent>(LeaderPoseComponent.Get()))return;
 if(const auto* Mesh=GetSkeletalMeshAsset())
  if(MorphTargetWeights.Num()==Mesh->GetMorphTargets().Num()&&GetMorphTargetCurves().Num()>0)
   FAnimationRuntime::AppendActiveMorphTargets(Mesh,GetMorphTargetCurves(),ActiveMorphTargets,MorphTargetWeights);
}
void UKKRuntimeSkeletalMeshComponent::PublishOwnedMorphTargets(){
 // UKKCharAnimInstance supplies bone poses only. Its facial curves are owned
 // by SetMorphTarget; use the same engine append/zero semantics without
 // evaluating the identical body skeleton again on every morph renderer.
 ActiveMorphTargets.Reset();
 if(const auto* Mesh=GetSkeletalMeshAsset()){
  MorphTargetWeights.SetNumZeroed(Mesh->GetMorphTargets().Num());
  if(!MorphTargetWeights.IsEmpty())FMemory::Memzero(MorphTargetWeights.GetData(),MorphTargetWeights.Num()*sizeof(float));
  FAnimationRuntime::AppendActiveMorphTargets(Mesh,GetMorphTargetCurves(),ActiveMorphTargets,MorphTargetWeights);
 }
 InvalidateCachedBounds();UpdateBounds();MarkRenderDynamicDataDirty();
}
FBoxSphereBounds UKKRuntimeSkeletalMeshComponent::CalcBounds(const FTransform& LocalToWorld) const
{
 FBoxSphereBounds Result;return RuntimeBounds.Calculate(*this,LocalToWorld,Result)?Result:Super::CalcBounds(LocalToWorld);
}

#if WITH_DEV_AUTOMATION_TESTS
#include "KKSkeletalBuilder.h"
#include "Animation/Skeleton.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "KKPreviewActor.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include <limits>
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKKLiveNativePaletteTest,"OpenKoikatsu.AnimationCatalog.LiveNativePalette",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FKKLiveNativePaletteTest::RunTest(const FString&){
 const FString Input=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_LIVE_PALETTE_AUDIT_CARDS"));
 if(Input.IsEmpty()){AddInfo(TEXT("Live APNG palette fixture omitted; pass -PaletteCards to regression runner"));return true;}
 TArray<FString> Cards;Input.ParseIntoArray(Cards,TEXT("|"),true);FKKScopedNativeSkinSource Source;
 TArray<uint8> Curves;if(!TestTrue(TEXT("Load common runtime walking curves"),FFileHelper::LoadFileToArray(Curves,*(FPaths::ProjectContentDir()/TEXT("IndependentResources/RuntimeAnimations/walk_01.okanim")))))return false;
 bool MotionObserved=false;
 for(const auto& Card:Cards){
  FOkSession Session;FString Error;if(!TestTrue(TEXT("Open actual APNG with common resources"),Session.Open(FString(),Card,0,Error))){AddError(Error);return false;}
  if(!TestTrue(TEXT("Install common curves directly in portable session"),Session.InstallSharedClip(Curves)))return false;
  auto* Skeleton=NewObject<USkeleton>();{
   FReferenceSkeletonModifier Edit(Skeleton);for(int32 B=0;B<3;++B){const FString Name=FString::Printf(TEXT("LiveSlot%d"),B);Edit.Add(FMeshBoneInfo(FName(*Name),Name,B-1),FTransform::Identity,true);}
  }
  auto* Mesh=OKBuildDummyLeaderMesh(Skeleton,GetTransientPackage());if(!TestNotNull(TEXT("Live native carrier mesh"),Mesh))return false;
  auto& LOD=Mesh->GetResourceForRendering()->LODRenderData[0];LOD.RenderSections[0].BoneMap={0,1,2};LOD.ActiveBoneIndices={0,1,2};
  auto* Component=NewObject<UKKRuntimePoseableMeshComponent>();Component->SetSkinnedAssetAndUpdate(Mesh);OKApplySkelPose(Component,Skeleton->GetReferenceSkeleton().GetRefBonePose());
  int32 Publications=0,PrivatePublications=0,StagePublications=0,LastStagePart=INDEX_NONE;float MaximumError=0,NativeHomogeneousError=0;double StageFlattenError=0;
  OkSkinPaletteView LastStageView{};TArray<float> LastStages,LastPost;uint64 LastRevision=0;
  TMap<int32,uint64> SourceRevisions;
  TMap<int32,TArray<FMatrix44f>> Previous;
  for(int32 Frame=0;Frame<3;++Frame){
   // The renderer bridge accepts both sexes/private rigs. Head-look's native
   // cf_j_head requirement belongs to the separate female control audit.
   if(!TestTrue(TEXT("Apply real common clip and live native dynamics"),Session.ApplyAnimFrame(Frame*.17f,Frame?1.f/120.f:0.f)))return false;
   for(int32 Part=0;Part<Session.PartCount();++Part){OkSkinPaletteView Raw{};TArray<float> Values;OkMeshView View{};
    if(!Session.PartAt(Part,View)||Session.SkinPalette(Part,Raw,Values)<=0||Raw.n_verts<=0)continue;
    OkSkinPaletteView StageView{};TArray<float> Stages,Post;
    const uint64 Revision=Session.PrivateSkinSourceRevision(Part);
    const int32 StageCount=Session.PrivateSkinStagesRevision(Part,Revision,StageView,Stages,Post);
    if(StageCount>0){
      if(const uint64* PreviousRevision=SourceRevisions.Find(Part))TestEqual(TEXT("Source revision remains stable during actual native playback"),Revision,*PreviousRevision);
      SourceRevisions.Add(Part,Revision);LastRevision=Revision;
     if(!TestTrue(TEXT("Private source stage dimensions"),StageView.n_verts==Raw.n_verts&&Post.Num()==16&&Stages.Num()==StageCount*16&&StageCount>=Values.Num()/16))return false;
     const FMatrix Correction=OKSkinPaletteToUnreal(Post.GetData());
     for(int32 B=0;B<StageCount-1;++B){const FMatrix Composed=OKSkinPaletteToUnreal(Stages.GetData()+B*16)*Correction,Flat=OKSkinPaletteToUnreal(Values.GetData()+B*16);
      for(int32 R=0;R<4;++R)for(int32 C=0;C<4;++C)StageFlattenError=FMath::Max(StageFlattenError,FMath::Abs(Composed.M[R][C]-Flat.M[R][C]));}
     if(!TestTrue(TEXT("Unweighted stage is identity"),OKSkinPaletteToUnreal(Stages.GetData()+(StageCount-1)*16).Equals(FMatrix::Identity,0.)))return false;
     ++StagePublications;LastStagePart=Part;LastStageView=StageView;LastStages=MoveTemp(Stages);LastPost=MoveTemp(Post);
    }
    for(int32 B=0;B<Values.Num()/16;++B)for(int32 C=0;C<4;++C)NativeHomogeneousError=FMath::Max(NativeHomogeneousError,FMath::Abs(Values[B*16+12+C]-(C==3?1.f:0.f)));
    int32 Vertex=0;for(int32 V=0;V<Raw.n_verts;++V)if(OKHasSignedSkinVertex(Raw,V)){Vertex=V;break;}
    const TArray<int32> Slots={0,Values.Num()/16-1,-Vertex-1};
    if(!TestTrue(TEXT("Live native part palette published to Unreal carrier"),Component->PublishNativeSkinPalette(Session,Part,Slots,Error))){AddError(FString::Printf(TEXT("part=%d name=%s error=%s"),Part,View.name?UTF8_TO_TCHAR(View.name):TEXT(""),*Error));return false;}
    TArray<FMatrix44f> Actual;Component->GetCurrentRefToLocalMatrices(Actual,0);
    if(const auto* Before=Previous.Find(Part))for(int32 B=0;B<FMath::Min(Before->Num(),Actual.Num());++B)MotionObserved|=!(*Before)[B].Equals(Actual[B],1.e-6f);
    Previous.Add(Part,Actual);
    for(int32 B=0;B<3;++B){FMatrix Expected;if(!OKExactPaletteMatrix(Raw,Values,Slots[B],Expected)||!Actual.IsValidIndex(B))return false;
     const FMatrix44f Converted(Expected);for(int32 R=0;R<4;++R)for(int32 C=0;C<4;++C)MaximumError=FMath::Max(MaximumError,FMath::Abs(Actual[B].M[R][C]-Converted.M[R][C]));
    }
    ++Publications;PrivatePublications+=View.skel_kind!=1&&View.skel_kind!=2;
   }
  }
  TestTrue(TEXT("Actual APNG produced native publications"),Publications>0&&PrivatePublications>0);TestEqual(TEXT("Live native render palette coefficient parity"),MaximumError,0.f);
  AddInfo(FString::Printf(TEXT("LiveNativePalette card=%s publications=%d private=%d max_error=%g native_unused_row_error=%g"),*Card,Publications,PrivatePublications,MaximumError,NativeHomogeneousError));
  TestTrue(TEXT("Actual APNG private source stages available"),StagePublications>0);
  TestTrue(TEXT("Staged affine composition retains flattened native placement"),StageFlattenError<=.001);
  AddInfo(FString::Printf(TEXT("LivePrivateStages publications=%d max_flatten_error_cm=%.9g"),StagePublications,StageFlattenError));
  FOkSession Moved=MoveTemp(Session);const TArray<float> StableStages=LastStages,StablePost=LastPost;
  TestEqual(TEXT("Moved-from source has no revision"),Session.PrivateSkinSourceRevision(LastStagePart),uint64(0));
  TestEqual(TEXT("Source revision follows native ownership move"),Moved.PrivateSkinSourceRevision(LastStagePart),LastRevision);
  TestEqual(TEXT("Wrong revision rejected atomically"),Moved.PrivateSkinStagesRevision(LastStagePart,LastRevision+1,LastStageView,LastStages,LastPost),-1);
  TestTrue(TEXT("Wrong revision preserves all staged output"),LastStages==StableStages&&LastPost==StablePost);
  TestTrue(TEXT("Revision-qualified source read survives move"),Moved.PrivateSkinStagesRevision(LastStagePart,LastRevision,LastStageView,LastStages,LastPost)>0);
  TestEqual(TEXT("Moved-from native source rejected"),Session.PrivateSkinStages(LastStagePart,LastStageView,LastStages,LastPost),-1);
  TestTrue(TEXT("Rejected source retains all staged output"),LastStages==StableStages&&LastPost==StablePost);
  TestTrue(TEXT("Source stages survive session ownership move"),Moved.PrivateSkinStages(LastStagePart,LastStageView,LastStages,LastPost)>0);
  Moved.Close();const TArray<float> ClosedStages=LastStages,ClosedPost=LastPost;
  TestEqual(TEXT("Closed source has no revision"),Moved.PrivateSkinSourceRevision(LastStagePart),uint64(0));
  TestEqual(TEXT("Closed revision-qualified source rejected"),Moved.PrivateSkinStagesRevision(LastStagePart,LastRevision,LastStageView,LastStages,LastPost),-1);
  TestEqual(TEXT("Closed native staged source rejected"),Moved.PrivateSkinStages(LastStagePart,LastStageView,LastStages,LastPost),-1);
  TestTrue(TEXT("Closed source retains last staged data"),LastStages==ClosedStages&&LastPost==ClosedPost);
  Component->ClearExactSkinPalette();Component->SetSkinnedAssetAndUpdate(nullptr);
 }
 TestTrue(TEXT("Installed portable clip changes actual native render matrices"),MotionObserved);
 return !HasAnyErrors();
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKKLiveAffinePaletteTest,"OpenKoikatsu.AnimationCatalog.LiveAffinePalette",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FKKLiveAffinePaletteTest::RunTest(const FString&){
 auto* Skeleton=NewObject<USkeleton>();{
  FReferenceSkeletonModifier Edit(Skeleton);
  for(int32 B=0;B<3;++B){const FString Name=FString::Printf(TEXT("NativePalette%d"),B);Edit.Add(FMeshBoneInfo(FName(*Name),Name,B-1),FTransform::Identity,true);}
 }
 auto* Mesh=OKBuildDummyLeaderMesh(Skeleton,GetTransientPackage());if(!TestNotNull(TEXT("Private runtime rig"),Mesh))return false;
 auto& LOD=Mesh->GetResourceForRendering()->LODRenderData[0];LOD.RenderSections[0].BoneMap={0,1,2};LOD.ActiveBoneIndices={0,1,2};
 auto* Component=NewObject<UKKRuntimePoseableMeshComponent>();Component->SetSkinnedAssetAndUpdate(Mesh);
 TArray<FTransform> Locals=Skeleton->GetReferenceSkeleton().GetRefBonePose();OKApplySkelPose(Component,Locals);
 FString Error;TArray<FMatrix44f> Palette;Palette.Init(FMatrix44f::Identity,3);
 for(int32 Frame=0;Frame<64;++Frame){
  Palette[0].M[3][0]=Frame*.125f;Palette[1].M[0][0]=-.7f;Palette[1].M[0][1]=.35f;Palette[1].M[1][2]=-.2f;
  Palette[2].M[2][2]=Frame%2?0.f:1.3f;Palette[2].M[3][1]=Frame*.25f;
  const auto Previous=Component->GetRefPoseOverride();const FMatrix44f PreviousValue=Previous->RefBasesInvMatrix[2];
  if(!TestTrue(TEXT("Same-frame full affine publication accepted"),Component->PublishExactSkinPalette(Palette,Error)))return false;
  TestTrue(TEXT("Previous renderer snapshot immutable"),Previous->RefBasesInvMatrix[2].Equals(PreviousValue,0));
  Component->FinalizeBoneTransform();TArray<FMatrix44f> Actual;Component->GetCurrentRefToLocalMatrices(Actual,0);
  for(int32 B=0;B<3;++B)TestTrue(TEXT("Actual Unreal render palette preserves shear/reflection/zero scale"),Actual.IsValidIndex(B)&&Actual[B].Equals(Palette[B],1.e-5f));
  // Engine refreshes may change the carrier during the same frame. Native
  // matrices remain the source of truth, including collapsed source scales.
  auto& Carrier=Component->GetEditableComponentSpaceTransforms();Carrier[1]=FTransform(FQuat(FVector::UpVector,.3),FVector(1.,2.,3.),FVector(.7,1.4,.8));Carrier[2].SetScale3D(FVector::ZeroVector);
  Component->FinalizeBoneTransform();Actual.Reset();Component->GetCurrentRefToLocalMatrices(Actual,0);
  for(int32 B=0;B<3;++B)TestTrue(TEXT("Carrier refresh cannot lose native full affine matrix"),Actual.IsValidIndex(B)&&Actual[B].Equals(Palette[B],1.e-5f));
  const auto Bounds=Component->CalcBounds(FTransform::Identity);const FBox Box(Bounds.Origin-Bounds.BoxExtent,Bounds.Origin+Bounds.BoxExtent);
  const auto* Weights=Component->GetSkinWeightBuffer(0);
  for(uint32 V=0;V<LOD.GetNumVertices();++V){FVector Point=FVector::ZeroVector;
   for(uint32 K=0;K<Weights->GetMaxBoneInfluences();++K){const auto Weight=Weights->GetBoneWeight(V,K);if(!Weight)continue;
    const int32 Bone=LOD.RenderSections[0].BoneMap[Weights->GetBoneIndex(V,K)];Point+=FVector(Palette[Bone].TransformPosition(LOD.StaticVertexBuffers.PositionVertexBuffer.VertexPosition(V)))*(double(Weight)/65535.);
   }
   TestTrue(TEXT("Conservative bounds include native palette deformed vertices"),Box.IsInsideOrOn(Point));
  }
  const auto Stable=Component->GetRefPoseOverride();auto Invalid=Palette;Invalid[2].M[0][0]=std::numeric_limits<float>::infinity();
  TestFalse(TEXT("Nonfinite palette rejected"),Component->PublishExactSkinPalette(Invalid,Error));TestTrue(TEXT("Reject keeps last renderer publication"),Stable==Component->GetRefPoseOverride());
  Invalid=Palette;Invalid[0].M[1][3]=.1f;TestFalse(TEXT("Projective matrix rejected"),Component->PublishExactSkinPalette(Invalid,Error));
  TestFalse(TEXT("Truncated palette rejected"),Component->PublishExactSkinPalette(MakeArrayView(Palette.GetData(),2),Error));
 }
 auto* Leader=NewObject<UKKRuntimePoseableMeshComponent>();Leader->SetSkinnedAssetAndUpdate(Mesh);
 TestFalse(TEXT("Private palette cannot silently join shared rig"),Component->TryUseExactLeader(Leader));
 TestFalse(TEXT("Shared rig cannot silently follow private palette"),Leader->TryUseExactLeader(Component));
 FOkSession ClosedSession;const auto Stable=Component->GetRefPoseOverride();const TArray<int32> Slots={0,1,2};
 TestFalse(TEXT("Closed native session cannot replace successful publication"),Component->PublishNativeSkinPalette(ClosedSession,0,Slots,Error));TestTrue(TEXT("Closed native session retains last snapshot"),Stable==Component->GetRefPoseOverride());
 Component->ClearExactSkinPalette();OKApplySkelPose(Component,Locals);TArray<FMatrix44f> Restored;Component->GetCurrentRefToLocalMatrices(Restored,0);
 TestTrue(TEXT("Explicit clear restores ordinary skeleton path"),Restored.IsValidIndex(2)&&Restored[2].Equals(FMatrix44f::Identity,0));
 TestTrue(TEXT("Republish after clear"),Component->PublishExactSkinPalette(Palette,Error));
 auto* Other=OKBuildDummyLeaderMesh(Skeleton,GetTransientPackage());Component->SetSkinnedAssetAndUpdate(Other);OKApplySkelPose(Component,Locals);Restored.Reset();Component->GetCurrentRefToLocalMatrices(Restored,0);
 TestTrue(TEXT("Asset replacement invalidates old private publication"),Restored.IsValidIndex(0)&&Restored[0].Equals(FMatrix44f::Identity,0));
 Component->SetSkinnedAssetAndUpdate(nullptr);TestFalse(TEXT("Removed asset rejected"),Component->PublishExactSkinPalette(Palette,Error));
 return !HasAnyErrors();
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKKExactLeaderTest,"OpenKoikatsu.AnimationCatalog.ExactLeaderPose",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FKKExactLeaderTest::RunTest(const FString&){
 const int32 Prior=CVarAffinePoseParity.GetValueOnGameThread();CVarAffinePoseParity.AsVariable()->Set(1,ECVF_SetByCode);ON_SCOPE_EXIT{CVarAffinePoseParity.AsVariable()->Set(Prior,ECVF_SetByCode);};
 auto* Skeleton=NewObject<USkeleton>();{
  FReferenceSkeletonModifier Edit(Skeleton);
  for(int32 Bone=0;Bone<3;++Bone){const FString Name=FString::Printf(TEXT("LeaderBone%d"),Bone);Edit.Add(FMeshBoneInfo(FName(*Name),Name,Bone-1),FTransform(FQuat::Identity,FVector(Bone,2.,-1.),FVector(1.2,.8,1.1)),true);}
 }
 auto* RootMesh=OKBuildDummyLeaderMesh(Skeleton,GetTransientPackage());auto* DrawMesh=OKBuildDummyLeaderMesh(Skeleton,GetTransientPackage());
 if(!TestNotNull(TEXT("Root runtime mesh"),RootMesh)||!TestNotNull(TEXT("Follower runtime mesh"),DrawMesh))return false;
 auto& LOD=DrawMesh->GetResourceForRendering()->LODRenderData[0];LOD.RenderSections[0].BoneMap[0]=2;LOD.ActiveBoneIndices.Add(2);
 auto* Root=NewObject<UKKRuntimePoseableMeshComponent>();auto* Follower=NewObject<UKKRuntimePoseableMeshComponent>();auto* Original=NewObject<UKKRuntimePoseableMeshComponent>();
 Root->SetSkinnedAssetAndUpdate(RootMesh);Follower->SetSkinnedAssetAndUpdate(DrawMesh);Original->SetSkinnedAssetAndUpdate(DrawMesh);
 auto* MorphFollower=NewObject<UKKRuntimeSkeletalMeshComponent>();MorphFollower->SetSkeletalMeshAsset(DrawMesh);
 TestTrue(TEXT("Morph renderer shares only exactly compatible skeletons"),MorphFollower->TryUseExactLeader(Root));
 TestTrue(TEXT("Compatible meshes share poses"),Follower->TryUseExactLeader(Root));
 TArray<FTransform> Pose=Skeleton->GetReferenceSkeleton().GetRefBonePose();
 for(int32 Frame=0;Frame<64;++Frame){
  Pose[0].SetRotation(FQuat(FVector(1.,2.,3.).GetSafeNormal(),.01*Frame));Pose[1].SetRotation(FQuat(FVector(2.,-1.,1.).GetSafeNormal(),-.03*Frame));Pose[1].SetScale3D(FVector(-.8,1.3,Frame%7==0?0.:.7));
  OKApplySkelPose(Root,Pose);OKApplySkelPose(Original,Pose);Follower->VerifyExactLeader(Pose);
  TArray<FMatrix44f> Shared,Legacy;Follower->GetCurrentRefToLocalMatrices(Shared,0);Original->GetCurrentRefToLocalMatrices(Legacy,0);
  TArray<FMatrix44f> MorphShared;MorphFollower->GetCurrentRefToLocalMatrices(MorphShared,0);
  const auto& BoundsPalette=Root->BoundsSkinPalette();
  TestTrue(TEXT("Shared bounds palette includes follower-only bones and current manual pose"),BoundsPalette.IsValidIndex(2)&&Legacy.IsValidIndex(2)&&BoundsPalette[2].Equals(Legacy[2],0));
  TestTrue(TEXT("Morph renderer retains exact affine palette"),MorphShared.IsValidIndex(2)&&Legacy.IsValidIndex(2)&&MorphShared[2].Equals(Legacy[2],0));
  if(!Shared.IsValidIndex(2)||!Legacy.IsValidIndex(2)){AddError(TEXT("Missing drawn follower bone"));return false;}
  for(int32 R=0;R<4;++R)for(int32 C=0;C<4;++C)if(Shared[2].M[R][C]!=Legacy[2].M[R][C]){AddError(TEXT("Leader draw palette differs from independent original"));return false;}
  const auto SharedBounds=Follower->CalcBounds(FTransform::Identity),LegacyBounds=Original->CalcBounds(FTransform::Identity);
  TestTrue(TEXT("Follower retains exact independent bounds"),SharedBounds.Origin==LegacyBounds.Origin&&SharedBounds.BoxExtent==LegacyBounds.BoxExtent&&SharedBounds.SphereRadius==LegacyBounds.SphereRadius);
 }
 Follower->SetRelativeLocation(FVector(1.,0.,0.));TestFalse(TEXT("Changed placement stops sharing"),Follower->TryUseExactLeader(Root));TestFalse(TEXT("Changed placement detaches old leader"),Follower->LeaderPoseComponent.IsValid());Follower->SetRelativeLocation(FVector::ZeroVector);
 TestTrue(TEXT("Restored placement can share again"),Follower->TryUseExactLeader(Root));Follower->TryUseExactLeader(nullptr);TestFalse(TEXT("Disabling sharing restores independent ownership"),Follower->LeaderPoseComponent.IsValid());
 auto* BadBind=OKBuildDummyLeaderMesh(Skeleton,GetTransientPackage());BadBind->GetRefBasesInvMatrix()[2].M[3][0]+=1.f;Follower->SetSkinnedAssetAndUpdate(BadBind);TestFalse(TEXT("Different inverse binds rejected"),Follower->TryUseExactLeader(Root));
 auto* OtherSkeleton=NewObject<USkeleton>();{FReferenceSkeletonModifier Edit(OtherSkeleton);Edit.Add(FMeshBoneInfo(TEXT("DifferentBone"),TEXT("DifferentBone"),INDEX_NONE),FTransform::Identity,true);}
 Follower->SetSkinnedAssetAndUpdate(OKBuildDummyLeaderMesh(OtherSkeleton,GetTransientPackage()));TestFalse(TEXT("Changed hierarchy/asset rejected"),Follower->TryUseExactLeader(Root));
 const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).CreateNavigation(false).CreateAISystem(false);
 auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
 GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
 auto* Actor=World->SpawnActor<AKKPreviewActor>();
 // Preview creates its leader while loading a session, not in its constructor.
 auto* ActorLeader=NewObject<UKKRuntimePoseableMeshComponent>(Actor);
 ActorLeader->SetupAttachment(Actor->GetRootComponent());Actor->BodySkel=ActorLeader;
 if(TestNotNull(TEXT("Preview owns a runtime leader"),ActorLeader)){
  ActorLeader->SetSkinnedAssetAndUpdate(RootMesh);ActorLeader->RegisterComponent();
  auto* Attached=NewObject<UKKRuntimePoseableMeshComponent>(Actor);
  Attached->SetupAttachment(ActorLeader);Attached->SetSkinnedAssetAndUpdate(DrawMesh);Attached->RegisterComponent();
  TestTrue(TEXT("Registered preview follower shares exact leader"),Attached->TryUseExactLeader(ActorLeader));
  Actor->SkelFollowers.Add(Attached);
  Actor->DestroyAllParts();
  TestFalse(TEXT("Preview follower unregisters before leader asset removal"),Attached->IsRegistered());
  TestNull(TEXT("Persistent preview leader clears after follower teardown"),ActorLeader->GetSkinnedAsset());
  TestTrue(TEXT("Preview follower ownership cleared"),Actor->SkelFollowers.IsEmpty());
  Actor->DestroyAllParts();
 }
 World->DestroyWorld(false);GEngine->DestroyWorldContext(World);
 return !HasAnyErrors();
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKKSharedAffinePoseTest,"OpenKoikatsu.AnimationCatalog.SharedAffinePose",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FKKSharedAffinePoseTest::RunTest(const FString&){
 const int32 Previous=CVarSharedAffinePose.GetValueOnGameThread();CVarSharedAffinePose.AsVariable()->Set(1,ECVF_SetByCode);
 float MaximumError=0.f;bool Valid=true;
 for(int32 Case=0;Case<6;++Case){
  FReferenceSkeleton Ref;{
   FReferenceSkeletonModifier Edit(Ref,nullptr);
   for(int32 B=0;B<64;++B){const FString Name=FString::Printf(TEXT("Bone_%d_%d"),Case,B);Edit.Add(FMeshBoneInfo(FName(*Name),Name,B==0?INDEX_NONE:(Case&1)?(B-1)/2:B-1),FTransform::Identity,true);}
  }
  TArray<FTransform> Pose,Approx;TArray<FMatrix44f> Expected;Pose.SetNum(64);Approx.SetNum(64);Expected.SetNum(64);
  for(int32 Frame=0;Frame<120;++Frame){
   for(int32 B=0;B<64;++B){
    const FVector Scale((B+Case)%7==0?-.7:1.1,(B+Case)%5==0?.8:1.2,(B+Frame)%19==0?0.:.9);
    Pose[B]=FTransform(FQuat(FVector(1.,2.,3.).GetSafeNormal(),.002*(Frame+B)),FVector(.013*Frame,.02*B,-.01*B),Scale);
    const int32 Parent=Ref.GetParentIndex(B);Expected[B]=FMatrix44f(Pose[B].ToMatrixWithScale());Approx[B]=Pose[B];
    if(Parent>=0){Expected[B]=Expected[B]*Expected[Parent];Approx[B]=Pose[B]*Approx[Parent];}
   }
   auto* Cached=SharedAffinePose(Ref,Pose);Valid&=Cached&&Cached==SharedAffinePose(Ref,Pose);if(!Cached)break;
   for(int32 B=0;B<64;++B){
    Cached->WorldAt(B);
    for(int32 R=0;R<4;++R)for(int32 C=0;C<4;++C)MaximumError=FMath::Max(MaximumError,FMath::Abs(Cached->World[B].M[R][C]-Expected[B].M[R][C]));
    if(FMath::Abs(Approx[B].GetScale3D().X*Approx[B].GetScale3D().Y*Approx[B].GetScale3D().Z)<1.e-12)Approx[B].SetScale3D(FVector::OneVector);
    FMatrix44f Inverse;Valid&=Cached->Inverse(B,Approx[B],Inverse);const FMatrix44f Oracle=FMatrix44f(Approx[B].ToMatrixWithScale()).Inverse();
    for(int32 R=0;R<4;++R)for(int32 C=0;C<4;++C)MaximumError=FMath::Max(MaximumError,FMath::Abs(Inverse.M[R][C]-Oracle.M[R][C]));
    // A different carrier in the same engine frame must invalidate this bone.
    FTransform Changed=Approx[B];Changed.AddToTranslation(FVector(.17,.03,-.02));Valid&=Cached->Inverse(B,Changed,Inverse);
    const FMatrix44f ChangedOracle=FMatrix44f(Changed.ToMatrixWithScale()).Inverse();
    for(int32 R=0;R<4;++R)for(int32 C=0;C<4;++C)MaximumError=FMath::Max(MaximumError,FMath::Abs(Inverse.M[R][C]-ChangedOracle.M[R][C]));
   }
  }
 }
 CVarSharedAffinePose.AsVariable()->Set(Previous,ECVF_SetByCode);
 TestTrue(TEXT("Full runtime pose/topology and carrier reuse is valid"),Valid);TestEqual(TEXT("Shared affine world/inverse matches independent formula exactly"),MaximumError,0.f);
 return Valid&&MaximumError==0.f;
}
#endif

