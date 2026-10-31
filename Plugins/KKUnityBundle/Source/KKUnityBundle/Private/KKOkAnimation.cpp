#include "KKOkAnimation.h"
#include "KKRustExtract.h"
#include "KKPreviewActor.h"
#include "ok_extract.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonReader.h"
#include "Dom/JsonObject.h"
#include "Misc/Paths.h"

// File-local helpers with file-specific names: unity builds merge this file with KKRustExtract.cpp (its own Utf8).
namespace
{
	template <typename F>
	bool OkAnimReadBytes(F&& Call, TArray<uint8>& Out)
	{
		Out.Reset();
		const int32 Size = Call(nullptr, 0);
		if (Size <= 0) return false;
		Out.SetNumUninitialized(Size);
		if (Call(Out.GetData(), Size) != Size) { Out.Reset(); return false; }
		return true;
	}

	FString OkAnimUtf8(const TArray<uint8>& Bytes)
	{
		const FUTF8ToTCHAR C(reinterpret_cast<const ANSICHAR*>(Bytes.GetData()), Bytes.Num());
		return FString(C.Length(), C.Get());
	}
}

FString FOkAnimBridge::LastError()
{
	ANSICHAR Buffer[1024] = {};
	ok_okanim_last_error(Buffer, sizeof(Buffer));
	return UTF8_TO_TCHAR(Buffer);
}

bool FOkAnimBridge::FromClip(FOkSession& Session, const FString& Key, float Fps, TArray<uint8>& Out)
{
	if (!Session.Handle) return false;
	const FTCHARToUTF8 K(*Key);
	return OkAnimReadBytes([&](uint8* Buf, int32 Cap) { return ok_okanim_from_clip(Session.Handle, K.Get(), Fps, Buf, Cap); }, Out);
}

bool FOkAnimBridge::FromLocals(const FString& HeaderJson, const TArray<float>& Rotations, const TArray<float>& Hips, TArray<uint8>& Out)
{
	const FTCHARToUTF8 H(*HeaderJson);
	return OkAnimReadBytes([&](uint8* Buf, int32 Cap) {
		return ok_okanim_from_locals(H.Get(), Rotations.GetData(), Rotations.Num(), Hips.GetData(), Hips.Num(), Buf, Cap);
	}, Out);
}

void* FOkAnimBridge::RecordStart(FOkSession& Session)
{
	return Session.Handle ? ok_okanim_record_start(Session.Handle) : nullptr;
}

bool FOkAnimBridge::RecordFrame(void* Recorder, FOkSession& Session)
{
	return Recorder && Session.Handle && ok_okanim_record_frame(static_cast<OkPoseRecorder*>(Recorder), Session.Handle) > 0;
}

bool FOkAnimBridge::RecordFinish(void* Recorder, float Fps, const FString& Name, TArray<uint8>& Out)
{
	if (!Recorder) return false;
	const FTCHARToUTF8 N(*Name);
	return OkAnimReadBytes([&](uint8* Buf, int32 Cap) { return ok_okanim_record_finish(static_cast<OkPoseRecorder*>(Recorder), Fps, 0, N.Get(), Buf, Cap); }, Out);
}

void FOkAnimBridge::RecordFree(void* Recorder)
{
	if (Recorder) ok_okanim_record_free(static_cast<OkPoseRecorder*>(Recorder));
}

bool FOkAnimBridge::Info(const TArray<uint8>& Anim, FString& OutJson)
{
	TArray<uint8> Bytes;
	if (!OkAnimReadBytes([&](uint8* Buf, int32 Cap) { return ok_okanim_info(Anim.GetData(), Anim.Num(), Buf, Cap); }, Bytes)) return false;
	OutJson = OkAnimUtf8(Bytes);
	return true;
}

bool FOkAnimBridge::Edit(const TArray<uint8>& Anim, const FString& PatchJson, TArray<uint8>& Out)
{
	const FTCHARToUTF8 P(*PatchJson);
	return OkAnimReadBytes([&](uint8* Buf, int32 Cap) { return ok_okanim_edit(Anim.GetData(), Anim.Num(), P.Get(), Buf, Cap); }, Out);
}

bool FOkAnimBridge::TargetRest(const FString& TargetJson, TArray<FQuat4f>& Out)
{
	const FTCHARToUTF8 J(*TargetJson);
	const int32 Need = ok_okanim_target_rest(J.Get(), nullptr, 0);
	if (Need <= 0 || Need % 4) return false;
	TArray<float> Raw;
	Raw.SetNumUninitialized(Need);
	if (ok_okanim_target_rest(J.Get(), Raw.GetData(), Need) != Need) return false;
	Out.SetNum(Need / 4);
	for (int32 I = 0; I < Out.Num(); ++I) Out[I] = FQuat4f(Raw[I * 4], Raw[I * 4 + 1], Raw[I * 4 + 2], Raw[I * 4 + 3]);
	return true;
}

bool FOkAnimBridge::GetTracks(const TArray<uint8>& Anim, TArray<float>& Out)
{
	const int32 Need = ok_okanim_get_tracks(Anim.GetData(), Anim.Num(), nullptr, 0);
	if (Need <= 0) return false;
	Out.SetNumUninitialized(Need);
	return ok_okanim_get_tracks(Anim.GetData(), Anim.Num(), Out.GetData(), Need) == Need;
}

bool FOkAnimBridge::SetTracks(const TArray<uint8>& Anim, const TArray<float>& Motion, TArray<uint8>& Out)
{
	return OkAnimReadBytes([&](uint8* Buf, int32 Cap) { return ok_okanim_set_tracks(Anim.GetData(), Anim.Num(), Motion.GetData(), Motion.Num(), Buf, Cap); }, Out);
}

bool FOkAnimBridge::SampleOnto(const TArray<uint8>& Anim, const FString& TargetJson, float T, TArray<float>& Out)
{
	const FTCHARToUTF8 J(*TargetJson);
	const int32 Need = ok_okanim_sample_onto(Anim.GetData(), Anim.Num(), J.Get(), T, nullptr, 0);
	if (Need <= 0) return false;
	Out.SetNumUninitialized(Need);
	return ok_okanim_sample_onto(Anim.GetData(), Anim.Num(), J.Get(), T, Out.GetData(), Need) == Need;
}

bool FOkAnimBridge::GroupFromPair(FOkSession& Female, FOkSession& Male, const FString& State, const FString& FemaleMotion, const FString& MaleMotion, float Fps, TArray<uint8>& Out)
{
	if (!Female.Handle || !Male.Handle) return false;
	const FTCHARToUTF8 S(*State), F(*FemaleMotion), M(*MaleMotion);
	return OkAnimReadBytes([&](uint8* Buf, int32 Cap) { return ok_okgroup_from_pair(Female.Handle, Male.Handle, S.Get(), F.Get(), M.Get(), Fps, Buf, Cap); }, Out);
}

TArray<FString> FOkAnimBridge::DynamicBones(FOkSession& Session)
{
	TArray<FString> Out;
	TArray<uint8> Bytes;
	if (!Session.Handle || !OkAnimReadBytes([&](uint8* Buf, int32 Cap) { return ok_session_dynamic_bones(Session.Handle, Buf, Cap); }, Bytes)) return Out;
	TArray<TSharedPtr<FJsonValue>> Values;
	const auto Reader = TJsonReaderFactory<>::Create(OkAnimUtf8(Bytes));
	if (FJsonSerializer::Deserialize(Reader, Values)) for (const auto& V : Values) Out.Add(V->AsString());
	return Out;
}

FOkAnimBatch::~FOkAnimBatch() { if (Handle) ok_okanim_batch_free(Handle); }

bool FOkAnimBatch::Start(FOkSession& Session, const FString& ItemsJson, float Fps, int32 Threads)
{
	if (Handle) { ok_okanim_batch_free(Handle); Handle = nullptr; }
	if (!Session.Handle) return false;
	const FTCHARToUTF8 J(*ItemsJson);
	Handle = ok_okanim_batch_start(Session.Handle, J.Get(), Fps, Threads);
	return Handle != nullptr;
}

bool FOkAnimBatch::Poll(int32 From, FString& OutJson) const
{
	TArray<uint8> Bytes;
	if (!Handle || !OkAnimReadBytes([&](uint8* Buf, int32 Cap) { return ok_okanim_batch_poll(Handle, From, Buf, Cap); }, Bytes)) return false;
	OutJson = OkAnimUtf8(Bytes);
	return true;
}

void FOkAnimBatch::Cancel() { if (Handle) ok_okanim_batch_cancel(Handle); }

bool FOkAnimBridge::Quality(const TArray<uint8>& Anim, const FString& HeaderJson, FString& OutJson)
{
	const FTCHARToUTF8 H(*HeaderJson);
	TArray<uint8> Bytes;
	if (!OkAnimReadBytes([&](uint8* Buf, int32 Cap) { return ok_okanim_quality(Anim.GetData(), Anim.Num(), H.Get(), Buf, Cap); }, Bytes)) return false;
	OutJson = OkAnimUtf8(Bytes);
	return true;
}

FString FOkAnimBridge::Profile(bool bReset)
{
	TArray<uint8> Bytes;
	return OkAnimReadBytes([&](uint8* Buf, int32 Cap) { return ok_okanim_profile(bReset ? 1 : 0, Buf, Cap); }, Bytes) ? OkAnimUtf8(Bytes) : FString();
}

bool FOkAnimBridge::SetPosture(bool bOn) { return ok_okanim_set_posture(bOn ? 1 : 0) != 0; }
bool FOkAnimBridge::SetParallelFinish(bool bOn) { return ok_okanim_set_parallel_finish(bOn ? 1 : 0) != 0; }

bool FOkAnimBridge::Modify(const TArray<uint8>& Anim, const FString& OpJson, const FString& HeaderJson, TArray<uint8>& Out)
{
	const FTCHARToUTF8 O(*OpJson);
	const FTCHARToUTF8 H(*HeaderJson);
	return OkAnimReadBytes([&](uint8* Buf, int32 Cap) { return ok_okanim_modify(Anim.GetData(), Anim.Num(), O.Get(), HeaderJson.IsEmpty() ? nullptr : H.Get(), Buf, Cap); }, Out);
}

bool FOkAnimBridge::ParseGroupPackage(const TArray<uint8>& Package, FString& OutSpecJson, TArray<TArray<uint8>>& OutAnims)
{
	OutAnims.Reset();
	auto U32 = [&](int32 At, uint32& V) { if (At + 4 > Package.Num()) return false; FMemory::Memcpy(&V, Package.GetData() + At, 4); return true; };
	if (Package.Num() < 16 || FMemory::Memcmp(Package.GetData(), "OKGROUP1", 8) != 0) return false;
	uint32 SpecLen = 0;
	if (!U32(8, SpecLen) || 12 + int64(SpecLen) > Package.Num()) return false;
	const FUTF8ToTCHAR Spec(reinterpret_cast<const ANSICHAR*>(Package.GetData() + 12), int32(SpecLen));
	OutSpecJson = FString(Spec.Length(), Spec.Get());
	int32 At = 12 + int32(SpecLen);
	uint32 Count = 0;
	if (!U32(At, Count)) return false;
	At += 4;
	for (uint32 i = 0; i < Count; ++i)
	{
		uint32 Len = 0;
		if (!U32(At, Len) || At + 4 + int64(Len) > Package.Num()) return false;
		OutAnims.Emplace(Package.GetData() + At + 4, int32(Len));
		At += 4 + int32(Len);
	}
	return true;
}

float FOkAnimBridge::SessionHipsLength(FOkSession& Session)
{
	FString Json;
	if (!SessionRestHeader(Session, Json)) return 1.f;
	// "name":"cf_j_hips" ... "rest_pos":[x,y,z]
	const int32 At = Json.Find(TEXT("\"name\":\"cf_j_hips\""));
	const int32 Pos = At == INDEX_NONE ? INDEX_NONE : Json.Find(TEXT("\"rest_pos\":["), ESearchCase::CaseSensitive, ESearchDir::FromStart, At);
	if (Pos == INDEX_NONE) return 1.f;
	TArray<FString> Parts;
	Json.Mid(Pos + 12, Json.Find(TEXT("]"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Pos) - Pos - 12).ParseIntoArray(Parts, TEXT(","));
	if (Parts.Num() != 3) return 1.f;
	return FMath::Max(1.e-3f, FVector(FCString::Atof(*Parts[0]), FCString::Atof(*Parts[1]), FCString::Atof(*Parts[2])).Size());
}

FString FOkAnimBridge::ClipFileStem(const FString& Key)
{
	FString Name;
	if (!Key.Split(TEXT("|"), nullptr, &Name, ESearchCase::IgnoreCase, ESearchDir::FromEnd)) Name = Key;
	Name.Split(TEXT("."), nullptr, &Name, ESearchCase::IgnoreCase, ESearchDir::FromEnd);
	for (const TCHAR* C : { TEXT("|"), TEXT("/"), TEXT("\\"), TEXT(":"), TEXT("."), TEXT(" "), TEXT("*"), TEXT("?"), TEXT("\""), TEXT("<"), TEXT(">"), TEXT("["), TEXT("]") }) Name.ReplaceInline(C, TEXT("_"));
	return FString::Printf(TEXT("%s_%08x"), *Name.Left(80), FCrc::StrCrc32(*Key));
}

FString FOkAnimBridge::PairPackagePath(const FString& FemaleClip, const FString& MaleClip, const FString& State)
{
	const FString Key = FemaleClip + TEXT("#") + MaleClip + TEXT("#") + State;
	return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("OKAnim"), TEXT("Pairs"),
		ClipFileStem(FemaleClip) + FString::Printf(TEXT("_%08x.okgroup"), FCrc::StrCrc32(*Key))));
}

bool FOkAnimBridge::SessionRestHeader(FOkSession& Session, FString& OutJson)
{
	if (!Session.Handle) return false;
	TArray<uint8> Bytes;
	if (!OkAnimReadBytes([&](uint8* Buf, int32 Cap) { return ok_okanim_session_rest(Session.Handle, Buf, Cap); }, Bytes)) return false;
	OutJson = OkAnimUtf8(Bytes);
	return true;
}

FOkAnimPlayer::~FOkAnimPlayer() { Reset(); }

void FOkAnimPlayer::Reset()
{
	if (Handle) { ok_player_free(Handle); Handle = nullptr; }
}

bool FOkAnimPlayer::Create(const FString& SpecJson, const TArray<TArray<uint8>>& Anims)
{
	Reset();
	TArray<const uint8*> Ptrs;
	TArray<int32> Lens;
	for (const TArray<uint8>& A : Anims) { Ptrs.Add(A.GetData()); Lens.Add(A.Num()); }
	const FTCHARToUTF8 S(*SpecJson);
	Handle = ok_player_create(S.Get(), Ptrs.GetData(), Lens.GetData(), Ptrs.Num());
	return Handle != nullptr;
}

bool FOkAnimPlayer::SetWalk(const TArray<uint8>& WalkAnim)
{
	return Handle && ok_player_set_walk(Handle, WalkAnim.Num() ? WalkAnim.GetData() : nullptr, WalkAnim.Num()) == 0;
}

bool FOkAnimPlayer::SetAliases(const TMap<FName, FName>& FamilyToCharacter)
{
	if (!Handle) return false;
	FString Json = TEXT("{");
	for (const auto& P : FamilyToCharacter) Json += FString::Printf(TEXT("%s\"%s\":\"%s\""), Json.Len() > 1 ? TEXT(",") : TEXT(""), *P.Key.ToString(), *P.Value.ToString());
	Json += TEXT("}");
	const FTCHARToUTF8 J(*Json);
	return ok_player_set_aliases(Handle, J.Get()) == 0;
}

void FOkAnimPlayer::SetParam(int32 Index, float Value) { if (Handle) ok_player_set_param(Handle, Index, Value); }
float FOkAnimPlayer::Duration() const { return Handle ? ok_player_duration(Handle) : 0.f; }
int32 FOkAnimPlayer::Roles() const { return Handle ? ok_player_roles(Handle) : 0; }

bool FOkAnimPlayer::SampleOnto(int32 Role, const FString& TargetJson, float T, TArray<float>& Out) const
{
	if (!Handle) return false;
	const FTCHARToUTF8 J(*TargetJson);
	const int32 Need = ok_player_sample_onto(Handle, Role, J.Get(), T, nullptr, 0);
	if (Need <= 0) return false;
	Out.SetNumUninitialized(Need);
	return ok_player_sample_onto(Handle, Role, J.Get(), T, Out.GetData(), Need) == Need;
}

FVector FOkAnimPlayer::RootDrift(int32 Role, float T) const
{
	float D[3] = { 0.f, 0.f, 0.f };
	if (Handle) ok_player_root_drift(Handle, Role, T, D);
	return FVector(D[0], D[1], D[2]);
}

TMap<FString, float> FOkAnimPlayer::Curves(int32 Role, float T) const
{
	TMap<FString, float> Out;
	if (!Handle) return Out;
	TArray<uint8> Bytes;
	if (!OkAnimReadBytes([&](uint8* Buf, int32 Cap) { return ok_player_curves(Handle, Role, T, Buf, Cap); }, Bytes)) return Out;
	TSharedPtr<FJsonObject> J;
	const auto Reader = TJsonReaderFactory<>::Create(OkAnimUtf8(Bytes));
	if (FJsonSerializer::Deserialize(Reader, J) && J)
		for (const auto& P : J->Values) Out.Add(FString(P.Key), float(P.Value->AsNumber()));
	return Out;
}

void FOkAnimPlayer::ApplyFaceCurves(int32 Role, float T, AKKPreviewActor& Actor) const
{
	ApplyFaceCurveValues(Curves(Role, T), Actor);
}

void FOkAnimPlayer::ApplyFaceCurveValues(const TMap<FString, float>& C, AKKPreviewActor& Actor)
{
	if (C.IsEmpty()) return;
	Actor.BeginFaceDisplayUpdate();
	ON_SCOPE_EXIT { Actor.EndFaceDisplayUpdate(); };
	if (const float* V = C.Find(TEXT("face.mouth_pattern")))
	{
		const int32 Pattern = FMath::RoundToInt(*V);
		if (Pattern != Actor.MouthPattern) Actor.SetMouthPattern(Pattern, false);
	}
	// An unchanged opening does not rebuild the mouth morphs.
	if (const float* V = C.Find(TEXT("face.mouth_open"))) if (!FMath::IsNearlyEqual(FMath::Clamp(*V, 0.f, 1.f), Actor.MouthOpen01, 1.e-4f)) Actor.SetMouthOpen(*V);
	const float* EyePattern = C.Find(TEXT("face.eye_pattern"));
	const float* EyeOpen = C.Find(TEXT("face.eye_open"));
	if (EyePattern || EyeOpen)
	{
		const int32 Pattern = EyePattern ? FMath::RoundToInt(*EyePattern) : Actor.FacePattern[0];
		const float Open = EyeOpen ? *EyeOpen : Actor.FaceOpen01[0];
		if (Pattern != Actor.FacePattern[0] || !FMath::IsNearlyEqual(Open, Actor.FaceOpen01[0], 1.e-3f)) Actor.SetFaceControl(1, Pattern, Open, false);
	}
	if (const float* V = C.Find(TEXT("face.brow_pattern")))
	{
		const int32 Pattern = FMath::RoundToInt(*V);
		if (Pattern != Actor.FacePattern[1]) Actor.SetFacePattern(2, Pattern, false); } if (!Actor.bEyeLookTracking) { const float *GL = C.Find(TEXT("face.eye_gaze_lh")), *GR = C.Find(TEXT("face.eye_gaze_rh")), *GV = C.Find(TEXT("face.eye_gaze_v")); if (GL || GR || GV) { const float LH = FMath::Clamp(GL ? *GL : Actor.EyeGaze[0], -1.f, 1.f), RH = FMath::Clamp(GR ? *GR : Actor.EyeGaze[1], -1.f, 1.f), V = FMath::Clamp(GV ? *GV : Actor.EyeGaze[2], -1.f, 1.f); if (!FMath::IsNearlyEqual(LH, Actor.EyeGaze[0], 1.e-3f) || !FMath::IsNearlyEqual(RH, Actor.EyeGaze[1], 1.e-3f) || !FMath::IsNearlyEqual(V, Actor.EyeGaze[2], 1.e-3f)) Actor.SetEyeGaze(LH, RH, V); }
	}
}

TMap<FString, float> FOkAnimPlayer::FaceCurvesOf(const AKKPreviewActor& Actor)
{
	return { { TEXT("face.mouth_pattern"), float(Actor.MouthPattern) }, { TEXT("face.mouth_open"), Actor.MouthOpen01 },
		{ TEXT("face.eye_pattern"), float(Actor.FacePattern[0]) }, { TEXT("face.eye_open"), Actor.FaceOpen01[0] }, { TEXT("face.brow_pattern"), float(Actor.FacePattern[1]) } };
}

bool FOkAnimPlayer::ApplySessions(const TArray<FOkSession*>& Sessions, float T, float Dt, FString* OutReport)
{
	if (!Handle || Sessions.IsEmpty()) return false;
	TArray<OkSession*> Raw;
	for (FOkSession* S : Sessions) { if (!S || !S->Handle) return false; Raw.Add(S->Handle); }
	ANSICHAR Report[2048] = {};
	const bool bOk = ok_player_apply(Handle, Raw.GetData(), Raw.Num(), T, Dt, reinterpret_cast<uint8*>(Report), sizeof(Report)) == 0;
	if (OutReport) *OutReport = bOk ? FString(UTF8_TO_TCHAR(Report)) : FOkAnimBridge::LastError();
	return bOk;
}

bool FOkAnimPlayer::Apply(const TArray<AKKPreviewActor*>& Actors, float T, float Dt, FString* OutReport)
{
	TArray<FOkSession*> Sessions;
	for (AKKPreviewActor* A : Actors)
	{
		if (!A || !A->Session.IsOpen()) return false;
		// Where each character's skeleton space sits in the world: the hair/cloth solver collides across characters.
		A->PublishPhysicsFrame();
		Sessions.Add(&A->Session);
	}
	if (!ApplySessions(Sessions, T, Dt, OutReport)) return false;
	// Gaze, face and shadow follow in each actor's own tick (PoseMode 4), after this driver's.
	for (int32 I = 0; I < Actors.Num(); ++I) { Actors[I]->AnimPhase = T; Actors[I]->RefreshSkinnedDisplay(); ApplyFaceCurves(I, T, *Actors[I]); }
	return true;
}
