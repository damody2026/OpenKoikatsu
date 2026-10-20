#include "AnimNode_OKContactIK.h"
#include "OKAnimationAssets.h"
#include "OKAnimationLibrary.h"
#include "Animation/AnimInstanceProxy.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "TwoBoneIK.h"

TArray<TPair<FOKContact, float>> FAnimNode_OKContactIK::ActiveContacts(const UOKAnimationAsset* Asset, int32 InRole, float T)
{
	TArray<TPair<FOKContact, float>> Out;
	const TArray<FOKContact>* Contacts = nullptr;
	if (const UOKAnimSequence* Seq = Cast<UOKAnimSequence>(Asset)) Contacts = &Seq->Contacts;
	else if (const UOKGroupAnimSequence* Group = Cast<UOKGroupAnimSequence>(Asset)) Contacts = &Group->Contacts;
	if (!Contacts) return Out;
	const float Length = Asset->GetPlayLength();
	// Contacts are authored on one cycle: a looping asset repeats them.
	const float Local = Length > 0.f ? FMath::Fmod(FMath::Max(0.f, T), Length) : T;
	for (const FOKContact& C : *Contacts)
	{
		if (C.Role != InRole || !C.bIK || Local < C.Start || Local > C.End) continue;
		const float In = C.Blend > 0.f ? FMath::Clamp((Local - C.Start) / C.Blend, 0.f, 1.f) : 1.f;
		const float OutW = C.Blend > 0.f ? FMath::Clamp((C.End - Local) / C.Blend, 0.f, 1.f) : 1.f;
		const float W = C.Weight * FMath::Min(In, OutW);
		if (W <= 1.e-4f) continue;
		FOKContact Now = C;
		if (C.Track.Num() > 1 && C.End > C.Start)
		{
			// Sliding contact: the offset over the contact's time.
			const float U = (Local - C.Start) / (C.End - C.Start) * (C.Track.Num() - 1);
			const int32 I = FMath::Clamp(FMath::FloorToInt(U), 0, C.Track.Num() - 2);
			Now.Offset = FMath::Lerp(C.Track[I], C.Track[I + 1], FMath::Clamp(U - I, 0.f, 1.f));
		}
		Out.Emplace(Now, W);
	}
	return Out;
}

void FAnimNode_OKContactIK::Initialize_AnyThread(const FAnimationInitializeContext& Context)
{
	FAnimNode_SkeletalControlBase::Initialize_AnyThread(Context);
	Clock = 0.f;
}

void FAnimNode_OKContactIK::UpdateInternal(const FAnimationUpdateContext& Context)
{
	FAnimNode_SkeletalControlBase::UpdateInternal(Context);
	Clock += Context.GetDeltaTime() * PlayRate;
}

void FAnimNode_OKContactIK::InitializeBoneReferences(const FBoneContainer& RequiredBones) {}

bool FAnimNode_OKContactIK::IsValidToEvaluate(const USkeleton* Skeleton, const FBoneContainer& RequiredBones)
{
	return Animation != nullptr;
}

bool FAnimNode_OKContactIK::EnsureBinding(const FReferenceSkeleton& Ref)
{
	if (BoundRef == &Ref && Binding.IsValid()) return true;
	BoundRef = &Ref;
	FString Error;
	const UOKSkeletonFamily* Family = Profile && Profile->Family ? Profile->Family.Get() : UOKAnimationLibrary::GetDefaultFamily();
	if (!Family || !Binding.Build(Ref, Profile, Family, Error)) { UE_LOG(LogTemp, Warning, TEXT("OK contact IK: skeleton not mapped: %s"), *Error); return false; }
	FamilyRestWorld.Reset();
	TSharedPtr<FJsonObject> J;
	if (FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Family->RestHeaderJson), J) && J)
		for (const TSharedPtr<FJsonValue>& V : J->GetArrayField(TEXT("bones")))
		{
			const TSharedPtr<FJsonObject> B = V->AsObject();
			const TArray<TSharedPtr<FJsonValue>>* Q = nullptr;
			if (B && B->TryGetArrayField(TEXT("rest_world"), Q) && Q->Num() == 4)
				FamilyRestWorld.Add(FName(B->GetStringField(TEXT("name"))), FQuat((*Q)[0]->AsNumber(), (*Q)[1]->AsNumber(), (*Q)[2]->AsNumber(), (*Q)[3]->AsNumber()).GetNormalized());
		}
	// Mesh rest in component space.
	const TArray<FTransform>& Local = Ref.GetRefBonePose();
	MeshRestComponent.SetNum(Local.Num());
	for (int32 I = 0; I < Local.Num(); ++I)
	{
		const int32 P = Ref.GetParentIndex(I);
		MeshRestComponent[I] = P == INDEX_NONE ? Local[I] : Local[I] * MeshRestComponent[P];
	}
	return Binding.IsValid();
}

void FAnimNode_OKContactIK::EvaluateSkeletalControl_AnyThread(FComponentSpacePoseContext& Output, TArray<FBoneTransform>& OutBoneTransforms)
{
	LastSolved = 0;
	LastMaxError = 0.f;
	const FBoneContainer& Bones = Output.Pose.GetPose().GetBoneContainer();
	const FReferenceSkeleton& Ref = Bones.GetReferenceSkeleton();
	if (!Animation || !EnsureBinding(Ref)) return;
	const float T = bUseTimePin ? Time : Clock;
	const TArray<TPair<FOKContact, float>> Active = ActiveContacts(Animation, Role, T);
	if (Active.IsEmpty()) return;
	auto MeshBoneOf = [&](FName Family) -> int32
	{
		const int32 H = Binding.FamilyName.IndexOfByKey(Family);
		return H == INDEX_NONE ? INDEX_NONE : Binding.BoneIndex[H];
	};
	auto Compact = [&](int32 Mesh) { return Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(Mesh)); };
	// The authored offsets are in hips heights of the reaching body: this mesh's hips height.
	const int32 HipsMesh = Binding.BoneIndex[Binding.Hips];
	const float HipsHeight = FMath::Max(1.f, float(Output.Pose.GetComponentSpaceTransform(Compact(HipsMesh)).GetTranslation().Z - GroundHeight)) / 100.f;
	TSet<int32> Used;
	TArray<FBoneTransform> Solved;
	for (const TPair<FOKContact, float>& A : Active)
	{
		const FOKContact& C = A.Key;
		const int32 End = MeshBoneOf(C.Effector);
		const int32 Joint = End == INDEX_NONE ? INDEX_NONE : Ref.GetParentIndex(End);
		const int32 Root = Joint == INDEX_NONE ? INDEX_NONE : Ref.GetParentIndex(Joint);
		if (Root == INDEX_NONE || Used.Contains(End) || Used.Contains(Joint) || Used.Contains(Root)) continue;
		const FCompactPoseBoneIndex CEnd = Compact(End), CJoint = Compact(Joint), CRoot = Compact(Root);
		if (!CEnd.IsValid() || !CJoint.IsValid() || !CRoot.IsValid()) continue;
		FTransform RootT = Output.Pose.GetComponentSpaceTransform(CRoot);
		FTransform JointT = Output.Pose.GetComponentSpaceTransform(CJoint);
		FTransform EndT = Output.Pose.GetComponentSpaceTransform(CEnd);
		FVector Target;
		if (C.TargetRole == -1)
		{
			// Ground: the effector at the authored height above the ground, where it is horizontally.
			Target = EndT.GetTranslation();
			Target.Z = GroundHeight + C.Offset.Y * HipsHeight * 100.f;
		}
		else if (C.TargetRole == Role)
		{
			const int32 TargetMesh = MeshBoneOf(C.TargetBone);
			const FQuat* FamilyRest = FamilyRestWorld.Find(C.TargetBone);
			const FCompactPoseBoneIndex CT = TargetMesh == INDEX_NONE ? FCompactPoseBoneIndex(INDEX_NONE) : Compact(TargetMesh);
			if (!FamilyRest || !CT.IsValid()) continue;
			// The target bone's rotation as the authoring body had it: its motion on this mesh (current x mesh rest^-1),
			// applied to the family rest (OK space).
			const FTransform TargetT = Output.Pose.GetComponentSpaceTransform(CT);
			const FQuat Motion = FOKUnrealSkeleton::ToOk(TargetT.GetRotation()) * FOKUnrealSkeleton::ToOk(MeshRestComponent[TargetMesh].GetRotation()).Inverse();
			const FVector OffsetOk = (Motion * *FamilyRest).RotateVector(C.Offset * HipsHeight);
			Target = FOKUnrealSkeleton::FromOkPosition(FOKUnrealSkeleton::ToOkPosition(TargetT.GetTranslation()) + OffsetOk);
		}
		else continue;   // partner / fixed-point contacts: the group player solves them on OK characters
		Target = FMath::Lerp(EndT.GetTranslation(), Target, double(A.Value));   // the node's Alpha blends the result afterwards
		// Keep the limb's current bend direction: the joint target is the joint pushed away from the root-end line.
		const FVector Line = EndT.GetTranslation() - RootT.GetTranslation();
		const FVector Closest = RootT.GetTranslation() + Line * FMath::Clamp(FVector::DotProduct(JointT.GetTranslation() - RootT.GetTranslation(), Line) / FMath::Max(Line.SizeSquared(), 1.e-6), 0., 1.);
		const FVector Pole = JointT.GetTranslation() + (JointT.GetTranslation() - Closest).GetSafeNormal() * 50.;
		AnimationCore::SolveTwoBoneIK(RootT, JointT, EndT, Pole, Target, MaxStretch > 1.f, 1., MaxStretch);
		LastMaxError = FMath::Max(LastMaxError, float(FVector::Dist(EndT.GetTranslation(), Target)));
		Solved.Emplace(CRoot, RootT);
		Solved.Emplace(CJoint, JointT);
		Solved.Emplace(CEnd, EndT);
		Used.Append({ Root, Joint, End });
		++LastSolved;
	}
	Solved.Sort(FCompareBoneTransformIndex());
	OutBoneTransforms = MoveTemp(Solved);
}

void FAnimNode_OKContactIK::GatherDebugData(FNodeDebugData& DebugData)
{
	DebugData.AddDebugItem(FString::Printf(TEXT("OK Contact IK (%s t=%.2f solved=%d err=%.1fcm)"), Animation ? *Animation->GetName() : TEXT("none"), bUseTimePin ? Time : Clock, LastSolved, LastMaxError));
	ComponentPose.GatherDebugData(DebugData);
}
