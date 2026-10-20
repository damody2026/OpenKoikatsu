#include "KKImportedPoseComponent.h"
#include "Engine/SkeletalMesh.h"
bool UKKImportedPoseComponent::SetImportedPose(const TArray<FTransform>& Pose){
 if(!GetSkeletalMeshAsset()||Pose.Num()!=GetSkeletalMeshAsset()->GetRefSkeleton().GetNum())return false;
 for(const auto& T:Pose)if(T.ContainsNaN()||!T.IsRotationNormalized())return false;
 ImportedPose=Pose;RefreshBoneTransforms();return true;
}
void UKKImportedPoseComponent::RefreshBoneTransforms(FActorComponentTickFunction* TickFunction){
 Super::RefreshBoneTransforms(TickFunction);
 if(ImportedPose.IsEmpty()||ImportedPose.Num()!=GetEditableComponentSpaceTransforms().Num())return;
 GetEditableComponentSpaceTransforms()=ImportedPose;
 bNeedToFlipSpaceBaseBuffers=true;FinalizeBoneTransform();UpdateBounds();MarkRenderDynamicDataDirty();
}
