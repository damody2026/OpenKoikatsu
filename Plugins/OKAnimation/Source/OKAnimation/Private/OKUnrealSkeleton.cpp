#include "OKUnrealSkeleton.h"
#include "OKAnimationAssets.h"
#include "KKOkAnimation.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimData/IAnimationDataModel.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	// Unity <-> Unreal: a 180 degree turn about (0,1,1), with centimetres to metres (KKSkeletalBuilder's convention).
	const FQuat Basis(0., 0.7071067811865475, 0.7071067811865475, 0.);

	TArray<TSharedPtr<FJsonValue>> Numbers(std::initializer_list<double> Values)
	{
		TArray<TSharedPtr<FJsonValue>> Out;
		for (double V : Values) Out.Add(MakeShared<FJsonValueNumber>(V));
		return Out;
	}
	TSharedPtr<FJsonValue> QuatJson(const FQuat& Q) { return MakeShared<FJsonValueArray>(Numbers({ Q.X, Q.Y, Q.Z, Q.W })); }
	TSharedPtr<FJsonValue> VecJson(const FVector& V) { return MakeShared<FJsonValueArray>(Numbers({ V.X, V.Y, V.Z })); }

	TArray<FTransform> Components(const FReferenceSkeleton& Ref, const TArray<FTransform>& Local)
	{
		TArray<FTransform> Out;
		Out.SetNum(Local.Num());
		for (int32 I = 0; I < Local.Num(); ++I)
		{
			const int32 P = Ref.GetParentIndex(I);
			Out[I] = P == INDEX_NONE ? Local[I] : Local[I] * Out[P];
		}
		return Out;
	}

	FName Resolve(const FName Bone, const TMap<FName, FName>& Exact, const TMap<FString, FName>& Normalized, bool bKk)
	{
		if (const FName* F = Exact.Find(Bone)) return *F;
		if (const FName* F = Normalized.Find(FOKUnrealSkeleton::NormalizeName(Bone.ToString()))) return *F;
		return bKk && Bone.ToString().StartsWith(TEXT("cf_j_")) ? Bone : NAME_None;
	}
}

FQuat FOKUnrealSkeleton::ToOk(const FQuat& Q) { return (Basis * Q * Basis).GetNormalized(); }
FQuat FOKUnrealSkeleton::FromOk(const FQuat& Q) { return (Basis * Q * Basis).GetNormalized(); }
FVector FOKUnrealSkeleton::ToOkPosition(const FVector& V) { return FVector(-V.X, V.Z, V.Y) / 100.; }
FVector FOKUnrealSkeleton::FromOkPosition(const FVector& V) { return FVector(-V.X, V.Z, V.Y) * 100.; }

double FOKUnrealSkeleton::AngleDegrees(const FQuat& A, const FQuat& B)
{
	const FQuat D = A.GetNormalized().Inverse() * B.GetNormalized();
	return FMath::RadiansToDegrees(2.0 * FMath::Atan2(FVector(D.X, D.Y, D.Z).Size(), FMath::Abs(D.W)));
}

FString FOKUnrealSkeleton::NormalizeName(const FString& Name)
{
	FString Out = Name.ToLower();
	for (const TCHAR* Prefix : { TEXT("mixamorig:"), TEXT("mixamorig_"), TEXT("mixamorig") })
		if (Out.StartsWith(Prefix)) { Out.RightChopInline(FCString::Strlen(Prefix)); break; }
	Out.ReplaceInline(TEXT(" "), TEXT(""));
	Out.ReplaceInline(TEXT("_"), TEXT(""));
	Out.ReplaceInline(TEXT(":"), TEXT(""));
	Out.ReplaceInline(TEXT("-"), TEXT(""));
	for (const TCHAR* Prefix : { TEXT("bip001"), TEXT("bip01") })
		if (Out.StartsWith(Prefix) && Out.Len() > FCString::Strlen(Prefix)) { Out.RightChopInline(FCString::Strlen(Prefix)); break; }
	return Out;
}

bool FOKUnrealSkeleton::Build(const FReferenceSkeleton& InRef, const UOKRetargetProfile* Profile, const UOKSkeletonFamily* Family, FString& OutError)
{
	Ref = InRef;
	BoneIndex.Reset();
	FamilyName.Reset();
	MappedParent.Reset();
	HeaderOfBone.Init(INDEX_NONE, Ref.GetNum());
	Hips = INDEX_NONE;
	HeaderJson.Reset();
	if (Ref.GetNum() == 0) { OutError = TEXT("empty skeleton"); return false; }

	TArray<FName> Names;
	for (int32 I = 0; I < Ref.GetNum(); ++I) Names.Add(Ref.GetBoneName(I));
	TMap<FName, FName> Exact;
	bool bKk = false;
	if (Profile && !Profile->BoneMap.IsEmpty()) Exact = Profile->BoneMap;
	else
	{
		const FName Preset = UOKRetargetProfile::DetectPreset(Names);
		if (Preset.IsNone()) { OutError = TEXT("no retarget profile and no built-in preset matches this skeleton"); return false; }
		bKk = Preset == FName(TEXT("KK"));
		if (const TMap<FName, FName>* Map = UOKRetargetProfile::GetPreset(Preset)) Exact = *Map;
	}
	TMap<FString, FName> Normalized;
	for (const auto& Pair : Exact) Normalized.Add(NormalizeName(Pair.Key.ToString()), Pair.Value);

	const FName HipsName = Profile ? Profile->FamilyHips : FName(TEXT("cf_j_hips"));
	TSet<FName> Used;
	for (int32 I = 0; I < Ref.GetNum(); ++I)
	{
		const FName F = Resolve(Names[I], Exact, Normalized, bKk);
		if (F.IsNone() || Used.Contains(F)) continue;
		Used.Add(F);
		int32 Parent = Ref.GetParentIndex(I);
		while (Parent != INDEX_NONE && HeaderOfBone[Parent] == INDEX_NONE) Parent = Ref.GetParentIndex(Parent);
		HeaderOfBone[I] = BoneIndex.Num();
		BoneIndex.Add(I);
		FamilyName.Add(F);
		MappedParent.Add(Parent == INDEX_NONE ? INDEX_NONE : HeaderOfBone[Parent]);
		if (F == HipsName) Hips = HeaderOfBone[I];
	}
	if (Hips == INDEX_NONE) { OutError = FString::Printf(TEXT("no bone maps to the family hips %s"), *HipsName.ToString()); return false; }

	const TArray<FTransform> Comp = Components(Ref, Ref.GetRefBonePose());
	TArray<TSharedPtr<FJsonValue>> Bones;
	for (int32 H = 0; H < BoneIndex.Num(); ++H)
	{
		const FTransform& C = Comp[BoneIndex[H]];
		const int32 A = MappedParent[H];
		const FQuat Local = A == INDEX_NONE ? C.GetRotation() : Comp[BoneIndex[A]].GetRotation().Inverse() * C.GetRotation();
		const FVector Pos = A == INDEX_NONE ? C.GetTranslation() : Comp[BoneIndex[A]].InverseTransformPositionNoScale(C.GetTranslation());
		TSharedRef<FJsonObject> B = MakeShared<FJsonObject>();
		B->SetStringField(TEXT("name"), FamilyName[H].ToString());
		B->SetStringField(TEXT("parent"), A == INDEX_NONE ? FString() : FamilyName[A].ToString());
		B->SetField(TEXT("rest_local"), QuatJson(ToOk(Local)));
		B->SetField(TEXT("rest_world"), QuatJson(ToOk(C.GetRotation())));
		B->SetField(TEXT("rest_pos"), VecJson(ToOkPosition(Pos)));
		B->SetField(TEXT("world_pos"), VecJson(ToOkPosition(C.GetTranslation())));
		Bones.Add(MakeShared<FJsonValueObject>(B));
	}
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("family"), Family ? Family->FamilyId.ToString() : FString(TEXT("kk.cf_j")));
	Root->SetNumberField(TEXT("fps"), 30.);
	Root->SetNumberField(TEXT("frames"), 1.);
	Root->SetStringField(TEXT("hips"), HipsName.ToString());
	Root->SetArrayField(TEXT("bones"), Bones);
	if ((!Profile || Profile->bAlignRest) && Family && !Family->RestHeaderJson.IsEmpty())
	{
		TSharedPtr<FJsonObject> FamilyRest;
		const auto Reader = TJsonReaderFactory<>::Create(Family->RestHeaderJson);
		if (!FJsonSerializer::Deserialize(Reader, FamilyRest) || !FamilyRest) { OutError = TEXT("family rest header is not valid JSON"); return false; }
		Root->SetObjectField(TEXT("align_to"), FamilyRest);
	}
	const auto Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&HeaderJson);
	if (!FJsonSerializer::Serialize(Root, Writer)) { OutError = TEXT("header"); return false; }
	{
		const int32 A = MappedParent[Hips];
		const FTransform& C = Comp[BoneIndex[Hips]];
		HipsLength = FMath::Max(1.e-4, ToOkPosition(A == INDEX_NONE ? C.GetTranslation() : Comp[BoneIndex[A]].InverseTransformPositionNoScale(C.GetTranslation())).Size());
	}
	TArray<FQuat4f> Rest;
	if (!FOkAnimBridge::TargetRest(HeaderJson, Rest) || Rest.Num() != BoneIndex.Num()) { OutError = FOkAnimBridge::LastError(); return false; }
	AlignedRestWorld.Reset();
	for (const FQuat4f& Q : Rest) AlignedRestWorld.Add(FQuat(Q));
	return true;
}

void FOKUnrealSkeleton::PoseToLocals(const TArray<FTransform>& Local, float* OutRotations, float* OutHips) const
{
	const TArray<FTransform> Comp = Components(Ref, Local);
	for (int32 H = 0; H < BoneIndex.Num(); ++H)
	{
		const FTransform& C = Comp[BoneIndex[H]];
		const int32 A = MappedParent[H];
		const FQuat Q = ToOk(A == INDEX_NONE ? C.GetRotation() : Comp[BoneIndex[A]].GetRotation().Inverse() * C.GetRotation());
		OutRotations[H * 4 + 0] = float(Q.X);
		OutRotations[H * 4 + 1] = float(Q.Y);
		OutRotations[H * 4 + 2] = float(Q.Z);
		OutRotations[H * 4 + 3] = float(Q.W);
		if (H == Hips)
		{
			const FVector P = ToOkPosition(A == INDEX_NONE ? C.GetTranslation() : Comp[BoneIndex[A]].InverseTransformPositionNoScale(C.GetTranslation()));
			OutHips[0] = float(P.X);
			OutHips[1] = float(P.Y);
			OutHips[2] = float(P.Z);
		}
	}
}

void FOKUnrealSkeleton::LocalsToPose(const float* Sample, TArray<FTransform>& Local) const
{
	if (Local.Num() != Ref.GetNum()) Local = Ref.GetRefBonePose();
	TArray<FTransform> Comp;
	Comp.SetNum(Local.Num());
	const float* HipsOut = Sample + BoneIndex.Num() * 4;
	for (int32 I = 0; I < Local.Num(); ++I)
	{
		const int32 P = Ref.GetParentIndex(I);
		const int32 H = HeaderOfBone.IsValidIndex(I) ? HeaderOfBone[I] : INDEX_NONE;
		if (H != INDEX_NONE)
		{
			const int32 A = MappedParent[H];
			const FQuat Ancestor = A == INDEX_NONE ? FQuat::Identity : Comp[BoneIndex[A]].GetRotation();
			const FQuat Desired = Ancestor * FromOk(FQuat(Sample[H * 4], Sample[H * 4 + 1], Sample[H * 4 + 2], Sample[H * 4 + 3]).GetNormalized());
			const FQuat ParentRot = P == INDEX_NONE ? FQuat::Identity : Comp[P].GetRotation();
			Local[I].SetRotation((ParentRot.Inverse() * Desired).GetNormalized());
			if (H == Hips)
			{
				const FVector Offset = FromOkPosition(FVector(HipsOut[0], HipsOut[1], HipsOut[2]));
				const FVector Desired3 = A == INDEX_NONE ? Offset : Comp[BoneIndex[A]].TransformPositionNoScale(Offset);
				Local[I].SetTranslation(P == INDEX_NONE ? Desired3 : Comp[P].InverseTransformPositionNoScale(Desired3));
			}
		}
		Comp[I] = P == INDEX_NONE ? Local[I] : Local[I] * Comp[P];
	}
}

bool FOKUnrealSkeleton::ConvertAnimation(const UAnimSequence* Sequence, float Fps, TArray<uint8>& OutBytes, FString& OutError) const
{
	if (!IsValid()) { OutError = TEXT("skeleton binding not built"); return false; }
	if (!Sequence || !Sequence->GetSkeleton()) { OutError = TEXT("animation has no skeleton"); return false; }
	if (Sequence->GetSkeleton()->GetReferenceSkeleton().GetNum() != Ref.GetNum()) { OutError = TEXT("animation skeleton differs from the binding"); return false; }
	Fps = FMath::Clamp(Fps, 1.f, 240.f);
	const double Length = Sequence->GetPlayLength();
	const int32 Count = FMath::Max(1, FMath::RoundToInt(Length * Fps)) + 1;
	TArray<TArray<FTransform>> Frames;
	Frames.SetNum(Count);
	for (int32 F = 0; F < Count; ++F) SampleSource(Sequence, FMath::Min(double(F) / Fps, Length), Frames[F]);
	const TMap<FString, FString> Meta = { { TEXT("source"), TEXT("unreal") }, { TEXT("source_key"), Sequence->GetPathName() }, { TEXT("source_skeleton"), Sequence->GetSkeleton()->GetPathName() } };
	return ConvertLocals(Frames, Fps, Sequence->bLoop, Sequence->GetName(), Meta, OutBytes, OutError);
}

bool FOKUnrealSkeleton::ConvertLocals(const TArray<TArray<FTransform>>& InFrames, float Fps, bool bLooping, const FString& Name, const TMap<FString, FString>& InMeta, TArray<uint8>& OutBytes, FString& OutError) const
{
	if (!IsValid()) { OutError = TEXT("skeleton binding not built"); return false; }
	if (InFrames.IsEmpty()) { OutError = TEXT("no frames"); return false; }
	const int32 Frames = InFrames.Num();
	TArray<float> Rotations, HipsPos;
	Rotations.SetNumUninitialized(Frames * BoneIndex.Num() * 4);
	HipsPos.SetNumUninitialized(Frames * 3);
	for (int32 F = 0; F < Frames; ++F)
	{
		if (InFrames[F].Num() != Ref.GetNum()) { OutError = FString::Printf(TEXT("frame %d has %d bones (skeleton %d)"), F, InFrames[F].Num(), Ref.GetNum()); return false; }
		PoseToLocals(InFrames[F], Rotations.GetData() + F * BoneIndex.Num() * 4, HipsPos.GetData() + F * 3);
	}
	TSharedPtr<FJsonObject> Header;
	const auto Reader = TJsonReaderFactory<>::Create(HeaderJson);
	if (!FJsonSerializer::Deserialize(Reader, Header) || !Header) { OutError = TEXT("header"); return false; }
	Header->SetNumberField(TEXT("fps"), FMath::Clamp(Fps, 1.f, 240.f));
	Header->SetNumberField(TEXT("frames"), Frames);
	Header->SetBoolField(TEXT("looping"), bLooping);
	Header->SetNumberField(TEXT("scene"), 0);
	Header->SetStringField(TEXT("name"), Name);
	TSharedRef<FJsonObject> Meta = MakeShared<FJsonObject>();
	for (const auto& P : InMeta) Meta->SetStringField(P.Key, P.Value);
	Header->SetObjectField(TEXT("meta"), Meta);
	FString Text;
	const auto Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text);
	FJsonSerializer::Serialize(Header.ToSharedRef(), Writer);
	if (!FOkAnimBridge::FromLocals(Text, Rotations, HipsPos, OutBytes)) { OutError = FOkAnimBridge::LastError(); return false; }
	return true;
}

void FOKUnrealSkeleton::SampleSource(const UAnimSequence* Sequence, double T, TArray<FTransform>& OutLocal) const
{
	OutLocal = Ref.GetRefBonePose();
	if (!Sequence) return;
#if WITH_EDITOR
	const IAnimationDataModel* Model = Sequence->GetDataModel();
#endif
	const FAnimExtractContext Context(T, false, {}, false);
	for (int32 I = 0; I < OutLocal.Num(); ++I)
	{
#if WITH_EDITOR
		// Raw evaluation of a bone without a track is not its reference pose.
		if (Model && !Model->IsValidBoneTrackName(Ref.GetBoneName(I))) continue;
#endif
		// Raw keys when the editor has them (compressed tracks are lossy); compressed data in cooked games.
		Sequence->GetBoneTransform(OutLocal[I], FSkeletonPoseBoneIndex(I), Context, true);
	}
#if !WITH_EDITOR
	// Additive animations: compressed data holds deltas; they go onto the reference pose (the base of a local-space additive).
	if (Sequence->IsValidAdditive())
	{
		const TArray<FTransform>& Base = Ref.GetRefBonePose();
		for (int32 I = 0; I < OutLocal.Num() && I < Base.Num(); ++I)
			OutLocal[I] = FTransform(OutLocal[I].GetRotation() * Base[I].GetRotation(), Base[I].GetTranslation() + OutLocal[I].GetTranslation(), Base[I].GetScale3D() * (FVector::OneVector + OutLocal[I].GetScale3D()));
	}
#endif
}

TArray<FVector> FOKUnrealSkeleton::HeaderPositions(const TArray<FTransform>& Local) const
{
	const TArray<FTransform> Comp = Components(Ref, Local);
	TArray<FVector> Out;
	for (const int32 B : BoneIndex) Out.Add(ToOkPosition(Comp[B].GetTranslation()));
	return Out;
}

bool FOKUnrealSkeleton::SamplePose(const UOKAnimSequence* Anim, float Time, TArray<FTransform>& OutLocal) const
{
	if (!IsValid() || !Anim) return false;
	TArray<float> Sample;
	if (!Anim->SampleOnto(HeaderJson, Time, Sample) || Sample.Num() != BoneIndex.Num() * 4 + 3) return false;
	OutLocal = Ref.GetRefBonePose();
	LocalsToPose(Sample.GetData(), OutLocal);
	return true;
}

