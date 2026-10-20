#include "OKAnimationKeys.h"
#include "KKOkAnimation.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#define LOCTEXT_NAMESPACE "OKAnimationKeys"

namespace
{
	void AddKey(FRichCurve& Curve, float T, float Value)
	{
		const FKeyHandle Key = Curve.UpdateOrAddKey(T, Value, false, 1.e-3f);
		// New keys are splines with automatic tangents (Maya's default); stepped keys stay stepped.
		Curve.SetKeyInterpMode(Key, Curve.GetKeyInterpMode(Key) == RCIM_Constant ? RCIM_Constant : RCIM_Cubic);
		Curve.AutoSetTangents();
	}

	/** The equivalent angle (360 degree turns) closest to Near. */
	float Unwind(float Value, float Near)
	{
		return Value + 360.f * FMath::RoundToFloat((Near - Value) / 360.f);
	}

	FQuat Quat(const TArray<TSharedPtr<FJsonValue>>& A) { return A.Num() == 4 ? FQuat(A[0]->AsNumber(), A[1]->AsNumber(), A[2]->AsNumber(), A[3]->AsNumber()) : FQuat::Identity; }
}

FRichCurve& FOKBoneKeys::Channel(int32 I)
{
	switch (I) { case 0: return RotX; case 1: return RotY; case 2: return RotZ; case 3: return PosX; case 4: return PosY; default: return PosZ; }
}
const FRichCurve& FOKBoneKeys::Channel(int32 I) const { return const_cast<FOKBoneKeys*>(this)->Channel(I); }

FText FOKBoneKeys::ChannelName(int32 I)
{
	static const FText Names[] = { LOCTEXT("RotX", "Rotate X"), LOCTEXT("RotY", "Rotate Y"), LOCTEXT("RotZ", "Rotate Z"),
		LOCTEXT("PosX", "Translate X"), LOCTEXT("PosY", "Translate Y"), LOCTEXT("PosZ", "Translate Z") };
	return Names[FMath::Clamp(I, 0, 5)];
}

bool FOKBoneKeys::IsEmpty() const
{
	for (int32 I = 0; I < NumChannels; ++I) if (Channel(I).GetNumKeys()) return false;
	return true;
}

FQuat FOKBoneKeys::EvalRotation(float T) const
{
	if (!RotX.GetNumKeys() && !RotY.GetNumKeys() && !RotZ.GetNumKeys()) return FQuat::Identity;
	return FRotator(RotY.Eval(T), RotZ.Eval(T), RotX.Eval(T)).Quaternion();
}

FVector FOKBoneKeys::EvalPosition(float T) const { return FVector(PosX.Eval(T), PosY.Eval(T), PosZ.Eval(T)); }

bool FOKBoneKeys::HasKey(float T, float Tolerance) const
{
	for (int32 I = 0; I < NumChannels; ++I)
		for (auto It = Channel(I).GetKeyIterator(); It; ++It) if (FMath::Abs(It->Time - T) <= Tolerance) return true;
	return false;
}

void FOKBoneKeys::SetRotationKey(float T, const FQuat& Rotation)
{
	const FRotator R = Rotation.GetNormalized().Rotator();
	AddKey(RotX, T, Unwind(R.Roll, RotX.GetNumKeys() ? RotX.Eval(T) : 0.f));
	AddKey(RotY, T, Unwind(R.Pitch, RotY.GetNumKeys() ? RotY.Eval(T) : 0.f));
	AddKey(RotZ, T, Unwind(R.Yaw, RotZ.GetNumKeys() ? RotZ.Eval(T) : 0.f));
}

void FOKBoneKeys::SetPositionKey(float T, const FVector& Position)
{
	AddKey(PosX, T, float(Position.X));
	AddKey(PosY, T, float(Position.Y));
	AddKey(PosZ, T, float(Position.Z));
}

void FOKBoneKeys::SetChannelKey(int32 InChannel, float T, float Value) { AddKey(Channel(InChannel), T, Value); }

void FOKBoneKeys::KeyCurrent(float T, bool bPosition)
{
	for (int32 I = 0; I < (bPosition ? 6 : 3); ++I) AddKey(Channel(I), T, Channel(I).Eval(T));
}

void FOKBoneKeys::RemoveKeys(float T, float Tolerance)
{
	for (int32 I = 0; I < NumChannels; ++I)
	{
		FRichCurve& C = Channel(I);
		TArray<FKeyHandle> Remove;
		for (auto It = C.GetKeyHandleIterator(); It; ++It) if (FMath::Abs(C.GetKeyTime(*It) - T) <= Tolerance) Remove.Add(*It);
		for (const FKeyHandle H : Remove) C.DeleteKey(H);
		C.AutoSetTangents();
	}
}

void FOKBoneKeys::GetKeyTimes(TArray<float>& Out) const
{
	for (int32 I = 0; I < NumChannels; ++I)
		for (auto It = Channel(I).GetKeyIterator(); It; ++It)
			if (!Out.ContainsByPredicate([&](float V) { return FMath::Abs(V - It->Time) < 1.e-3f; })) Out.Add(It->Time);
	Out.Sort();
}

void FOKBoneKeys::MoveKeys(float From, float To, float Tolerance)
{
	if (FMath::Abs(From - To) <= Tolerance) return;
	for (int32 I = 0; I < NumChannels; ++I)
	{
		FRichCurve& C = Channel(I);
		FKeyHandle Moving = FKeyHandle::Invalid();
		TArray<FKeyHandle> Replaced;
		for (auto It = C.GetKeyHandleIterator(); It; ++It)
		{
			const float KeyTime = C.GetKeyTime(*It);
			if (FMath::Abs(KeyTime - From) <= Tolerance) Moving = *It;
			else if (FMath::Abs(KeyTime - To) <= Tolerance) Replaced.Add(*It);
		}
		if (Moving == FKeyHandle::Invalid()) continue;
		for (const FKeyHandle H : Replaced) C.DeleteKey(H);
		C.SetKeyTime(Moving, To);
		C.AutoSetTangents();
	}
}

bool FOKAnimTracks::Load(const TArray<uint8>& Bytes, FString& OutError)
{
	FString Info;
	TArray<float> Motion;
	if (!FOkAnimBridge::Info(Bytes, Info) || !FOkAnimBridge::GetTracks(Bytes, Motion)) { OutError = FOkAnimBridge::LastError(); return false; }
	TSharedPtr<FJsonObject> J;
	const auto Reader = TJsonReaderFactory<>::Create(Info);
	if (!FJsonSerializer::Deserialize(Reader, J) || !J) { OutError = TEXT("info"); return false; }
	Frames = int32(J->GetNumberField(TEXT("frames")));
	Fps = float(J->GetNumberField(TEXT("fps")));
	HipsBone = FName(J->GetStringField(TEXT("hips_bone")));
	HipsParentRestWorld = Quat(J->GetArrayField(TEXT("hips_parent_rest_world")));
	Bones.Reset(); ParentNames.Reset(); RestLocal.Reset(); RestWorld.Reset(); Index.Reset();
	for (const TSharedPtr<FJsonValue>& V : J->GetArrayField(TEXT("tracks")))
	{
		const TSharedPtr<FJsonObject> T = V->AsObject();
		Index.Add(FName(T->GetStringField(TEXT("bone"))), Bones.Num());
		Bones.Add(FName(T->GetStringField(TEXT("bone"))));
		ParentNames.Add(FName(T->GetStringField(TEXT("parent"))));
		RestLocal.Add(Quat(T->GetArrayField(TEXT("rest_local"))));
		RestWorld.Add(Quat(T->GetArrayField(TEXT("rest_world"))));
	}
	const int32 N = Bones.Num();
	if (Motion.Num() != Frames * N * 4 + Frames * 3) { OutError = TEXT("motion size"); return false; }
	Rot.SetNum(Frames * N);
	for (int32 I = 0; I < Rot.Num(); ++I) Rot[I] = FQuat(Motion[I * 4], Motion[I * 4 + 1], Motion[I * 4 + 2], Motion[I * 4 + 3]);
	Hips.SetNum(Frames);
	const int32 Base = Frames * N * 4;
	for (int32 F = 0; F < Frames; ++F) Hips[F] = FVector(Motion[Base + F * 3], Motion[Base + F * 3 + 1], Motion[Base + F * 3 + 2]);
	return true;
}

bool FOKAnimTracks::Save(const TArray<uint8>& Template, TArray<uint8>& OutBytes, FString& OutError) const
{
	TArray<float> Motion;
	Motion.Reserve(Rot.Num() * 4 + Hips.Num() * 3);
	for (const FQuat& Q : Rot) { Motion.Add(float(Q.X)); Motion.Add(float(Q.Y)); Motion.Add(float(Q.Z)); Motion.Add(float(Q.W)); }
	for (const FVector& H : Hips) { Motion.Add(float(H.X)); Motion.Add(float(H.Y)); Motion.Add(float(H.Z)); }
	if (!FOkAnimBridge::SetTracks(Template, Motion, OutBytes)) { OutError = FOkAnimBridge::LastError(); return false; }
	return true;
}

void FOKAnimTracks::SetFrameCount(int32 NewFrames)
{
	NewFrames = FMath::Max(2, NewFrames);
	const int32 N = Bones.Num();
	if (NewFrames == Frames || Frames <= 0) return;
	TArray<FQuat> NewRot;
	NewRot.SetNum(NewFrames * N);
	TArray<FVector> NewHips;
	NewHips.SetNum(NewFrames);
	for (int32 F = 0; F < NewFrames; ++F)
	{
		const int32 From = FMath::Min(F, Frames - 1);
		for (int32 K = 0; K < N; ++K) NewRot[F * N + K] = Rot[From * N + K];
		NewHips[F] = Hips[From];
	}
	Rot = MoveTemp(NewRot);
	Hips = MoveTemp(NewHips);
	Frames = NewFrames;
}

void FOKAnimTracks::ApplyLayer(const TArray<FOKBoneKeys>& Layer)
{
	const int32 N = Bones.Num();
	for (const FOKBoneKeys& Keys : Layer)
	{
		const int32 K = Find(Keys.Bone);
		if (K == INDEX_NONE || Keys.IsEmpty()) continue;
		const bool bRot = Keys.RotX.GetNumKeys() || Keys.RotY.GetNumKeys() || Keys.RotZ.GetNumKeys();
		const bool bPos = Keys.Bone == HipsBone && (Keys.PosX.GetNumKeys() || Keys.PosY.GetNumKeys() || Keys.PosZ.GetNumKeys());
		for (int32 F = 0; F < Frames; ++F)
		{
			const float T = F / FMath::Max(Fps, 1.f);
			if (bRot) { FQuat& Q = Rot[F * N + K]; Q = (Q * Keys.EvalRotation(T)).GetNormalized(); }
			if (bPos) Hips[F] += Keys.EvalPosition(T);
		}
	}
}

#undef LOCTEXT_NAMESPACE
