#pragma once

#include "CoreMinimal.h"

class FOkSession;
class AKKPreviewActor;
struct OkAnimPlayer;

/**
 * OK animation bridge: the skeleton-agnostic animation system (ok_extract src/okanim) for other modules.
 * Lives in this module because it owns the ok_extract instance whose sessions, clip caches and physics solver the
 * characters use; other modules call through here instead of linking a second copy.
 */
class KKUNITYBUNDLE_API FOkAnimBridge
{
public:
	static FString LastError();
	/** KK clip (as selectable on the session) -> OK animation bytes. */
	static bool FromClip(FOkSession& Session, const FString& Key, float Fps, TArray<uint8>& Out);
	/** Absolute locals of any named skeleton (header JSON: family, fps, frames, looping, scene, bones[name,parent,rest_local,rest_world,rest_pos], hips). */
	static bool FromLocals(const FString& HeaderJson, const TArray<float>& Rotations, const TArray<float>& Hips, TArray<uint8>& Out);
	static bool Info(const TArray<uint8>& Anim, FString& OutJson);
	/** Pose recorder: the session's final body pose frame by frame (OK Studio timeline -> OK animation). Start returns
	 *  a handle (null on failure), Frame adds the current pose, Finish writes the bytes, Free releases the handle. */
	static void* RecordStart(FOkSession& Session);
	static bool RecordFrame(void* Recorder, FOkSession& Session);
	static bool RecordFinish(void* Recorder, float Fps, const FString& Name, TArray<uint8>& Out);
	static void RecordFree(void* Recorder);
	/** Parameter edit (looping, scene, fps, meta, contacts, curves) -> new bytes; bone data unchanged. */
	static bool Edit(const TArray<uint8>& Anim, const FString& PatchJson, TArray<uint8>& Out);
	/** A modifier (trim_rest_start, find_loop, auto_loop, close_loop, in_place, smooth, renormalize, foot_contacts) -> new bytes.
	 *  HeaderJson: a skeleton header for positions (foot_contacts), may be empty otherwise. */
	/** Quality issues on a skeleton header (positions): JSON [{kind, bone, start, end, value}] in frames. */
	static bool Quality(const TArray<uint8>& Anim, const FString& HeaderJson, FString& OutJson);
	/** Posture adaptation of every OK player on / off (returns the previous setting). */
	static bool SetPosture(bool bOn);
	/** Accumulated OK playback phase timings, JSON {phase: [total_us, count]}; Reset starts a new window. */
	static FString Profile(bool bReset);
	/** Finishing passes of a group's characters on worker threads (returns the previous setting). */
	static bool SetParallelFinish(bool bOn);
	static bool Modify(const TArray<uint8>& Anim, const FString& OpJson, const FString& HeaderJson, TArray<uint8>& Out);
	/** Rest world rotation (OK space) of every bone of a target header after alignment to its family. */
	static bool TargetRest(const FString& TargetJson, TArray<FQuat4f>& Out);
	/** Dense motion: frames x tracks x 4 rotation deltas, then frames x 3 normalised hips offsets. */
	static bool GetTracks(const TArray<uint8>& Anim, TArray<float>& Out);
	static bool SetTracks(const TArray<uint8>& Anim, const TArray<float>& Motion, TArray<uint8>& Out);
	/** Pose at T on a target skeleton rest (bones x 4 local rotations, then 3 hips local coordinates; Unity space). */
	static bool SampleOnto(const TArray<uint8>& Anim, const FString& TargetJson, float T, TArray<float>& Out);
	/** KK paired state on two sessions (clips selected) -> group package (OKGROUP1: spec JSON + role animations). */
	static bool GroupFromPair(FOkSession& Female, FOkSession& Male, const FString& State, const FString& FemaleMotion, const FString& MaleMotion, float Fps, TArray<uint8>& Out);
	/** Splits an OKGROUP1 package. */
	/** Bones the character's physics dynamics move (their pose is simulated, not animated). */
	static TArray<FString> DynamicBones(FOkSession& Session);
	static bool ParseGroupPackage(const TArray<uint8>& Package, FString& OutSpecJson, TArray<TArray<uint8>>& OutAnims);
	/** Length of a session's rest hips offset (OK metres): the unit of OK hips / root motion values. */
	static float SessionHipsLength(FOkSession& Session);
	/** File stem of a converted KK clip: its state name plus a hash of the whole key. */
	static FString ClipFileStem(const FString& Key);
	/** Where the OK group of a KK paired state lives (Saved/OKAnim/Pairs). */
	static FString PairPackagePath(const FString& FemaleClip, const FString& MaleClip, const FString& State);
	/** Rest skeleton of a session's Animator layer as a target header (for SampleOnto / FromLocals). */
	static bool SessionRestHeader(FOkSession& Session, FString& OutJson);
};

/** Runtime player of an OK spec (one animation, a 1..N role group, or a 1D/2D blend space of either). */
/** KK clips of a character converted to .okanim files on Rust worker threads (the character keeps playing). */
class KKUNITYBUNDLE_API FOkAnimBatch
{
public:
	~FOkAnimBatch();
	/** ItemsJson = [{"key","file","source"}]; existing files are skipped. */
	bool Start(FOkSession& Session, const FString& ItemsJson, float Fps, int32 Threads);
	/** Progress JSON: total, processed, converted, skipped, failed, first_error, running, entries[From..]. */
	bool Poll(int32 From, FString& OutJson) const;
	void Cancel();
	bool IsValid() const { return Handle != nullptr; }
private:
	struct OkAnimBatch* Handle = nullptr;
};

class KKUNITYBUNDLE_API FOkAnimPlayer
{
public:
	FOkAnimPlayer() = default;
	~FOkAnimPlayer();
	FOkAnimPlayer(const FOkAnimPlayer&) = delete;
	FOkAnimPlayer& operator=(const FOkAnimPlayer&) = delete;

	bool Create(const FString& SpecJson, const TArray<TArray<uint8>>& Anims);
	void Reset();
	bool IsValid() const { return Handle != nullptr; }
	void SetParam(int32 Index, float Value);
	/** Bone aliases (family bone -> character bone) for bodies naming some bones differently. */
	bool SetAliases(const TMap<FName, FName>& FamilyToCharacter);
	/** A walk cycle (OK animation bytes) mixed into the legs of roles the placement moves fast; empty clears it. */
	bool SetWalk(const TArray<uint8>& WalkAnim);
	float Duration() const;
	int32 Roles() const;
	/** Poses every role's character at T (one actor per role, role order, actors in PoseMode 4) and refreshes their skinning. */
	bool Apply(const TArray<AKKPreviewActor*>& Actors, float T, float Dt, FString* OutReport = nullptr);
	/** Root travel of a role at unwrapped time T (hips heights; zero unless the animation moves by root motion). */
	FVector RootDrift(int32 Role, float T) const;
	/** One role's pose at T on any target skeleton header (no contact solving): Unreal meshes, the editor preview. */
	bool SampleOnto(int32 Role, const FString& TargetJson, float T, TArray<float>& Out) const;
	/** Curve values of a role at T (empty when the animation has none). */
	TMap<FString, float> Curves(int32 Role, float T) const;
	/** The face curves of a role (face.eye_pattern / eye_open / brow_pattern / mouth_pattern / mouth_open) onto its character. */
	void ApplyFaceCurves(int32 Role, float T, AKKPreviewActor& Actor) const;
	/** The character's current face as constant curve values (recorded into converted animations). */
	static TMap<FString, float> FaceCurvesOf(const AKKPreviewActor& Actor);
	/** Same on bare sessions (no display), e.g. for validation. */
	bool ApplySessions(const TArray<FOkSession*>& Sessions, float T, float Dt, FString* OutReport = nullptr);

private:
	OkAnimPlayer* Handle = nullptr;
};
