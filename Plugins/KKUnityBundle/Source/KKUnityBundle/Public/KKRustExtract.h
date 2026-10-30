#pragma once

#include "CoreMinimal.h"
#include "Tasks/Task.h"

struct OkSession;
struct OkMeshView;
using FOkPrivateSkinCallback=void(*)(void*,int32,int32,int32,const float*,const float*,const float*);
struct OkSkinPaletteView;
struct OkTextureSettings;
struct OkBoneView;
struct OkMorphView;
struct OkBodyPose;
struct FReferenceSkeleton;
class UAnimSequence;

bool KKUNITYBUNDLE_API OKExtractCard(
	const FString& GameRoot,
	const FString& CardPath,
	const FString& OutDir,
	FString& OutError,
	int32 OutfitIndex = 0);

/** The native session of an FOkSession. A character may solve its next game-animation frame on a worker
 *  (FOkSession::LaunchAsyncAnimFrame) while the game thread draws the current one; every other use of the handle
 *  (passing it to the native library, testing it) first waits for that solve, except on the solving thread itself. */
struct KKUNITYBUNDLE_API FOkSessionHandle
{
	OkSession* Raw = nullptr;
	mutable UE::Tasks::TTask<bool> Async;
	FOkSessionHandle() = default;
	FOkSessionHandle(OkSession* P) : Raw(P) {}
	FOkSessionHandle(const FOkSessionHandle& Other) : Raw(Other.Get()) {}
	FOkSessionHandle& operator=(const FOkSessionHandle& Other) { OkSession* P = Other.Get(); Sync(); Raw = P; return *this; }
	FOkSessionHandle& operator=(OkSession* P) { Sync(); Raw = P; return *this; }
	operator OkSession*() const { return Get(); }
	OkSession* Get() const { Sync(); return Raw; }
	void Sync() const;
};

class KKUNITYBUNDLE_API FOkSession
{
public:
	/** Solve the game-animation frame at Time on a worker; the game thread keeps drawing meanwhile. The next use of
	 *  this session waits for it; FinishAsyncAnimFrame takes its result (false: none pending, or the frame failed). */
	void LaunchAsyncAnimFrame(float Time, float DeltaSeconds);
	bool HasAsyncAnimFrame() const { return Handle.Async.IsValid(); }
	bool FinishAsyncAnimFrame();
	FOkSession() = default;
	~FOkSession();
	FOkSession(const FOkSession&) = delete;
	FOkSession& operator=(const FOkSession&) = delete;
	FOkSession(FOkSession&& Other) noexcept;
	FOkSession& operator=(FOkSession&& Other) noexcept;

	static bool InitBindings();
	/** Close() frees native sessions on a background task when called on the game thread; module shutdown waits here. */
	static void WaitForBackgroundCloses();
 static bool AppearanceRequest(const FString& Root,const FString& Request,const FString& Report,FString& Error);
 static bool ClothingTransition(const FString& Root,const FString& Card,int32 Outfit,const uint8 Previous[9],int32 Kind,uint8 Out[9],FString& Error);
 /** OK clothes (.okclothes): one outfit with its plugin data; the shared models stay in the OK base library.
  *  Import a coordinate card, export outfit Outfit of a character card, or wear one as outfit Outfit (a new card at Out).
  *  Summary gets a one-line result (with plugin data that could not be converted), or the error. */
 static bool ClothesImport(const FString& CoordinateCard,const FString& Out,FString& Summary);
 static bool ClothesExport(const FString& Card,int32 Outfit,const FString& Out,FString& Summary);
 static bool ClothesWear(const FString& Card,const FString& OkClothes,int32 Outfit,const FString& Out,FString& Summary);
 /** OK card at Out from an original KKS card (zipmods from the KKS installation) or an edited OK card (made again so
  *  its models list and pack match the card data). Summary gets "digest parts pack" or the error. */
 static bool MakeOkCard(const FString& Card,const FString& Out,FString& Summary);
 static bool MakeOkCardForOutfit(const FString& Card,const FString& Out,int32 Outfit,FString& Summary);
 static bool CopyPortableClothingStates(const FString& Source,const FString& Output,FString& Error);
 static TArray<FString> AnimationSourceNames(const FString& Root,const FString& Source);
 static bool PrepareStudioAnimation(const FString& Root,const FString& Key,const FString& Source,const FString& Owner);
 static bool PrepareAnimationSource(const FString& Root,const FString& Source);
 static bool PrepareAnimationClip(const FString& Root,const FString& Key);
 static void BeginAnimationPreparation();
 static void EndAnimationPreparation();
 static bool PrepareAnimationPair(const FString& Root,const FString& State,const FString& FemaleMotion,const FString& MaleMotion);
 bool HasAnimationClip(const FString& Key) const;
 static FString CommonPairRoot();
 /** Pair metadata of the shared PairedAnimations root (what portable/OK sessions list); memoized natively. */
 static FString CommonAnimationPairs();
 /** Builds the native per-process memos card opens share (lists, mods scans, pair metadata) for a KKS folder, or the OK
  *  base library when GameRoot is empty. Blocking: call from a background thread before the first open. */
 static bool Prewarm(const FString& GameRoot);
 /** Marks the calling thread's opens as background work that yields to foreground opens (fails while one runs). */
 static void SetThreadBackground(bool bBackground);
 /** Whether opening this OK card (outfit, 9 clothes states or null = all worn) is served by the open cache:
  *  1 yes, 0 no, -1 not an OK library card (no open cache). Cheap: hashes the card, opens nothing. */
 static int32 CacheReady(const FString& CardPath,int32 Outfit,const uint8* Clothes);
  /** Call between scene loads with no source open/prepare jobs running. Keeps live sessions and disk caches. */
  static void ReleaseSourceCaches();
 static FString CommonInteractionPoses();
 bool PrepareBodyReaction(const TArray<uint8>& Definition,FString& Error);
 bool BodyReactionHit(int32 Id,TConstArrayView<FVector3f> Forces,float Weight,uint8 ReleaseMask=0);
 void ClearBodyReaction();
 bool IsBodyReactionPlaying() const;
 static FString PairClipForRoot(const FString& Root,const FString& Key,int32 Layer);

	/** StatusJson (optional): an OK Studio character state (tears, blush, nipples, skin gloss, fluids, accessory slots; see ok_session_open_status). */
	bool Open(const FString& GameRoot, const FString& CardPath, int32 Outfit, FString& OutError, const uint8* Clothes = nullptr, const FString& StatusJson = FString());
	/** One prefab of an OK base library model (.okmodel) as a preview session (parts at their authored geometry). */
	bool OpenOkModel(const FString& Library, const FString& ModelId, const FString& Prefab, bool bIncludeDisabled, FString& OutError);
	/** OpenOkModel with Studio item FK: FkJson = [[bone, [x, y, z]], …] (local Unity Euler degrees) turned before the meshes are baked. */
	bool OpenOkModelFk(const FString& Library, const FString& ModelId, const FString& Prefab, const FString& FkJson, FString& OutError);
	/** Bones a model prefab's skinned meshes follow (what Studio item FK can turn). */
	static TArray<FString> OkModelFkBones(const FString& Library, const FString& ModelId, const FString& Prefab);
	/** Render-to-texture cameras of a model prefab (a Studio camera item) as JSON (`ok_okmodel_render_cameras`); empty when none. */
	static FString OkModelRenderCameras(const FString& Library, const FString& ModelId, const FString& Prefab);
	/** Prefab-space transform of a named node of a library model (Unity: metres, quaternion, scale); false when not recorded. */
	static bool OkModelNodeWorld(const FString& Library, const FString& ModelId, const FString& Prefab, const FString& Node, FVector& OutPosition, FQuat& OutRotation, FVector& OutScale);
 bool ExportAnimationSnapshot(TArray<uint8>& Out) const;
 bool OpenAnimationSnapshot(const TArray<uint8>& Bytes);
 bool CloneForInteraction(FOkSession& Destination) const;
 bool PrepareRuntimeMaterials(FString& Error,bool bParallel=false); // bParallel: textures decoded in parallel (OKCaptureRuntimeMaterialsParallel)
 // One part's runtime material description (OKDescribeRuntimeMaterialPart: source metadata, resolved values), no textures.
 bool DescribeRuntimeMaterialPart(int32 Index,struct FKKRuntimeMaterialPart& Out,FString& Error) const;
 bool HeadTextureChanges(const FOkSession& Source,TArray<int32>& Parts,bool Apply,FString& Error);
 TSharedPtr<struct FKKRuntimeMaterialData> RuntimeMaterials;
 bool ExportSharedClip(const FString& Key,TArray<uint8>& Out) const;
 bool InstallSharedClip(const TArray<uint8>& Bytes);
 static TArray<FString> ImportedSharedClipNames(const FString& Source=FString());
 static TArray<uint8> ImportedSharedClipBytes(const FString& Key);
 static bool PrepareSharedClipBytes(const TArray<uint8>& Bytes);
 bool SelectImportedSharedClip(const FString& Key);
 // Video-derived OKCLIP01 files (Tools/video_motion) in Saved/VideoMotions.
 // Keys are "video:|<file stem>" and play through SetAnimClip like any clip.
 static FString VideoMotionDirectory();
 static bool IsVideoClipKey(const FString& Key){return Key.StartsWith(TEXT("video:|"));}
 static TArray<FString> VideoClipNames();
 static bool LoadVideoClip(const FString& Key,TArray<uint8>& Out);
	void Close();
	bool IsOpen() const { return Handle != nullptr; }
	bool IsPortable() const { return bPortable; }
	/** Identity of this native session for the PhysX secondary-dynamics groups (the native session address). */
	uint64 PhysicsGroup() const { return reinterpret_cast<uint64>(Handle.Raw); }
	/** PhysX secondary dynamics (OKPhysXSecondary) for this character. Off by default. The gallery physics menu,
	 *  one-to-one interaction and paired playback turn it on (AKKPreviewActor::AddPairPhysics); crowd and street
	 *  NPCs keep the built-in solver. Kept across reopening the card (outfit change, snapshot, clone). */
	bool SetPhysXSecondary(bool bOn);
	bool IsPhysXSecondary() const { return bPhysXSecondary; }
	/** Whether an Unreal animation drives this body instead of the session's game animation. Per character: the
	 *  OK_UE_ANIM environment switch is process-wide, and another actor loading (a clothing warm-up) flipped it under
	 *  the visible character, which then stopped sampling its animation and showed the T-pose. Kept like the PhysX switch. */
	bool SetExternalAnimation(bool bExternal);

	int32 PartCount() const;
	bool PartAt(int32 Index, OkMeshView& Out) const;
 int32 SkinPalette(int32 Index,OkSkinPaletteView& Out,TArray<float>& Matrices) const;
 // Private GPU input uses skin then placement/normal correction separately.
 int32 PrivateSkinStages(int32 Index,OkSkinPaletteView& Out,TArray<float>& Matrices,TArray<float>& PostCorrection) const;
 uint64 PrivateSkinSourceRevision(int32 Index) const;
 int32 PrivateSkinStagesRevision(int32 Index,uint64 ExpectedRevision,OkSkinPaletteView& Out,TArray<float>& Matrices,TArray<float>& PostCorrection) const;
 bool PrivateSkinStagesBatch(TConstArrayView<int32> Parts,TConstArrayView<uint64> Revisions,TConstArrayView<int32> Counts,TArray<float>& Matrices,TArray<float>& Posts) const;
 bool BoneWorldMatrix(const FString& Name,float Out[16]) const;
 FString SkinSourceHash(int32 Part) const;
 FString SkinSourceHashV2(int32 Part) const;
 bool RawMorph(int32 Part,int32 Morph,FString& Name,TArray<uint32>& Indices,TArray<float>& Deltas) const;
	bool TextureSettings(int32 Index, const char* Property, OkTextureSettings& Out) const;
	bool OutlineEnabled(int32 Index) const;
	bool MaterialKeyword(int32 Index, const char* Keyword) const;
	bool MaterialValue(int32 Index, const char* Property, FLinearColor& Out) const;
	FString MaterialShaderName(int32 Index) const;
    FString MaterialMetadataJson(int32 Index) const;
	bool ShaderTexture(int32 Index, const char* Property, const uint8*& Out, int32& Length) const;
	/** An eye part's expression texture coordinates (Unity, one per vertex); false when the part has none. */
	bool EyeExpressionUVs(int32 Index, TArray<FVector2f>& Out) const;
	static FString DecodeEditorMods(const TArray<uint8>& Extended);
 static bool EncodeEditorMods(const TArray<uint8>& Extended,const FString& Json,TArray<uint8>& Out);
 bool ApplyBodyPhysics(float Softness,float Weight);
 bool ApplyEditorMods(const FString& Json,int32 Coordinate,bool* GeometryChanged=nullptr);
 // ApplyEditorMods followed by ApplyShape(Body,Face), the shape built once (a card's load: its mods, then its sliders).
 bool ApplyEditorModsAndShape(const FString& Json,int32 Coordinate,const TArray<float>& Body,const TArray<float>& Face,bool* GeometryChanged=nullptr);
 bool ApplyShape(const TArray<float>& Body, const TArray<float>& Face);
	bool ApplyShapeLive(const TArray<float>& Body,const TArray<float>& Face,bool DeferDynamics=false);
	bool SetHeadLook(const FVector& UnityTarget,bool Enabled);
	/** Studio FK: these bones take these local rotations (Unity Euler degrees) instead of the animated ones; empty clears. */
	bool SetFk(const TArray<FString>& Bones,const TArray<FVector>& UnityEuler);
	/** Local rotation (Unity Euler degrees) of a body bone in the current pose. */
	bool BoneEuler(const FString& Bone,FVector& OutUnityEuler) const;
	/** Studio IK (FullBodyBipedIK): effectors 0 body, 1/2 L/R shoulder, 3/4 L/R thigh, 5/6 L/R hand, 7/8 L/R foot.
	 *  Positions are skeleton space (Unity metres). */
	bool PrepareStudioIk(FString& OutError);
	bool SetStudioIk(const TArray<int32>& Effectors,const TArray<FVector>& UnityPositions);
    bool SetStudioIkPose(const TArray<int32>& Effectors,const TArray<FVector>& UnityPositions,const TArray<FQuat>& UnityRotations,const TArray<uint8>& UseRotations);
    bool SetStudioFixedNeck(const TArray<FQuat>& UnityRotations);
    bool SetStudioFixedEyes(const TArray<float>& Values, int32 Pattern, FVector& Rates);
	bool IkEffectorPosition(int32 Effector,FVector& OutUnity) const;
	// Last head-look check in degrees: face vs its limited goal, goal vs the target. False when not looking.
	bool HeadLookError(float& Residual,float& Excess) const;
	bool StaticInteractionFrame(float Delta);
	bool Skin();
	TArray<float> ShapeBody() const;
	TArray<float> ShapeFace() const;
	int32 CoordinateCount() const;
	int32 SkelBoneCount();
	bool SkelBoneAt(int32 Index, OkBoneView& Out);
	int32 MorphCount(int32 PartIndex) const;
	bool MorphAt(int32 PartIndex, int32 MorphIndex, OkMorphView& Out) const;
	bool MouthPair(int32 PartIndex, int32 Pattern, int32& Close, int32& Open) const;
	int32 MouthPatternCount(int32 PartIndex) const;
	int32 FacePatternCount(int32 PartIndex, int32 Control) const;
	bool FacePair(int32 PartIndex, int32 Control, int32 Pattern, int32& Close, int32& Open) const;
	float FaceBaselineWeight(int32 PartIndex, int32 Morph) const;
	float FaceDefaultRate(int32 Control) const;
	float FaceRuntimeSetting(int32 Setting) const;
	float EyeMaterialSetting(int32 PartIndex, int32 Setting) const;
	bool SetEyeGaze(float LeftH, float RightH, float Vertical);
	int32 EyeLookPatternCount() const;
	bool StepEyeLook(const FVector& UnityTarget, int32 Pattern, float DeltaSeconds, bool Enabled, float* Rates);
	bool ApplyAnimTime(float Time);
	bool ApplyAnimFrame(float Time, float DeltaSeconds);
    bool SkinDisplayExceptPrivate(TConstArrayView<int32> Parts,TConstArrayView<uint64> Revisions);
    /** The parts drawn on the GPU from their palettes from now on: playback no longer skins them on the CPU. */
    bool SetSceneGpuParts(TConstArrayView<int32> Parts,TConstArrayView<uint64> Revisions);
    bool QueryPrivateSkinGeometry(int32 Part);
	bool QueryPrivateSkinGeometryBatch(TConstArrayView<int32> Parts);
	bool ExternalBodyPoseAt(int32 Bone, OkBodyPose& Out);
	bool ApplyExternalBodyFrame(float Time, float DeltaSeconds, const TArray<OkBodyPose>& Poses);
	bool BuildUnrealBodyFrame(const FReferenceSkeleton& Ref, UAnimSequence* Sequence, float Time, TArray<OkBodyPose>& Out);
	bool ApplyUnrealFrame(const FReferenceSkeleton& Ref, UAnimSequence* Sequence, float Time, float DeltaSeconds);
	float AnimDuration();
	bool SetStudioAnimation(const FString& Key, bool IsH, float Height, float Breast, bool IsMotion, float Motion,
		const FString& YureSource=FString(), const FString& YureOwner=FString(), int32 PVMask=15);
	int32 StudioYureFlag(int32 Index) const;
	bool SetStudioPV(int32 Mask, bool FK);
	int32 StudioPVFlag(int32 Index) const;
	int32 StudioPVBone(int32 Index, bool Source);
	bool IsAnimLooping();
	TArray<FString> ClipNames() const;
	TArray<FString> ClipSources() const;
	TArray<FString> ClipNamesForSource(const FString& Source) const;
	// Successful selection enters source Animator playback after Open's T-pose.
	bool SetAnimClip(const FString& Name);
	bool SetAnimSceneMode(int32 Mode);
	int32 AnimationFloatCount() const;
	bool AnimationFloatAt(int32 Index, FString& Name, float& Value, float& Min, float& Max) const;
	bool SetAnimationFloat(const FString& Name, float Value);
	TArray<FString> AnimationTriggers() const;
	bool SetAnimationTrigger(const FString& Name);
	bool CrossFadeAnimation(const FString& Name,float NormalizedDuration,float NormalizedOffset);
	bool CrossFadeAnimationLayer(const FString& Name,int32 Layer,float NormalizedDuration,float NormalizedOffset);
	float AnimationElapsed() const;
	float AnimationTransitionSourceLength() const;
	float AnimationLayerTransitionSourceLength(int32 Layer) const;
	bool IsAnimationLayerTransitioning(int32 Layer) const;
	bool IsAnimationTransitioning() const;
	int32 HandShapeCount() const;
	bool SetHandShape(int32 Side, bool Enabled, int32 A, int32 B, float Blend);
	FString NativeAnimationPairs() const;
	FString NativePairClip(const FString& Key,int32 Layer) const;
	static bool ApplyPairedFrame(FOkSession& Female, FOkSession& Male,float Time,float Delta,
		const FString& State,const FString& FemaleMotion,const FString& MaleMotion,FString& Error,
        FOkPrivateSkinCallback Callback=nullptr,void* Context=nullptr);
    static bool ApplyPairedFrameGpu(FOkSession& Female,FOkSession& Male,float Time,float Delta,const FString& State,const FString& FemaleMotion,const FString& MaleMotion,
        TConstArrayView<int32> FemaleParts,TConstArrayView<uint64> FemaleRevisions,TConstArrayView<int32> MaleParts,TConstArrayView<uint64> MaleRevisions,FString& Error);

private:
	friend class FOkAppearanceContext;
	friend class FOkLowPoly;
	friend class FOkAnimBridge;
	friend class FOkAnimPlayer;
	friend class FOkAnimBatch;
	FOkSessionHandle Handle;
	// Capacity hints only: native validates and solves the current pose on every read.
	mutable TMap<int32, int32> SkinPaletteCapacityHints;
	mutable TMap<int32, int32> PrivateSkinStageCapacityHints;
	bool bPortable = false;
 bool bSharedAnimation = false;
 bool bPhysXSecondary = false;
 int8 ExternalAnimation = -1;
 /** Sends bPhysXSecondary and ExternalAnimation to a newly adopted native session. */
 void ApplyPhysXSecondary() const;
};

// Owned handles are kept alive by worker captures. Join all calls before
// destruction; commit may access the live session only on the game thread.
class KKUNITYBUNDLE_API FOkAppearanceCandidate
{
public:
 ~FOkAppearanceCandidate();
 FOkAppearanceCandidate(const FOkAppearanceCandidate&)=delete;
 FOkAppearanceCandidate& operator=(const FOkAppearanceCandidate&)=delete;
 bool Info(FString& Json) const;
 bool Texture(int32 Part,int32 Slot,TArray<uint8>& Bytes) const;
 bool CardMetadata(TArray<uint8>& Bytes) const;
private:
 friend class FOkAppearanceContext;
 FOkAppearanceCandidate()=default;
 void* Handle=nullptr;
};
class KKUNITYBUNDLE_API FOkAppearanceCheckpoint
{
public:
 ~FOkAppearanceCheckpoint();
 FOkAppearanceCheckpoint(const FOkAppearanceCheckpoint&)=delete;
 FOkAppearanceCheckpoint& operator=(const FOkAppearanceCheckpoint&)=delete;
 uint64 Revision() const;
 bool Export(const FString& Path,FString& Error) const;
private:
 friend class FOkAppearanceContext;
 FOkAppearanceCheckpoint()=default;
 void* Handle=nullptr;
};
// Exact-slot accessory transaction. Owns its prepared renderers; nothing is
// published until Commit. Not yet exposed as a supported UI target.
class KKUNITYBUNDLE_API FOkAppearanceGeometryCandidate
{
public:
 ~FOkAppearanceGeometryCandidate();
 FOkAppearanceGeometryCandidate(const FOkAppearanceGeometryCandidate&)=delete;
 FOkAppearanceGeometryCandidate& operator=(const FOkAppearanceGeometryCandidate&)=delete;
 bool Info(FString& Json) const;
 bool CardMetadata(TArray<uint8>& Bytes) const;
private:
 friend class FOkAppearanceContext;
 FOkAppearanceGeometryCandidate()=default;
 void* Handle=nullptr;
};
// Low-poly companion of an exported card: each renderer remeshed with
// instant-meshes, skin carried over onto the shared rig, written as binary
// glTF beside the card. Capture copies the session on the game thread; the
// handle owns no session state, so Export may run on a worker thread.
class KKUNITYBUNDLE_API FOkLowPoly
{
public:
 ~FOkLowPoly();
 FOkLowPoly(const FOkLowPoly&)=delete;
 FOkLowPoly& operator=(const FOkLowPoly&)=delete;
 static TSharedPtr<FOkLowPoly,ESPMode::ThreadSafe> Capture(FOkSession& Session,FString& Error);
 // Writes <card>.lowpoly.glb and <card>.lowpoly.json. Budget<=0: default.
 bool Export(const FString& CardPath,int32 Budget,FString& Summary,FString& Error) const;
 static FString GlbPath(const FString& CardPath);
private:
 FOkLowPoly()=default;
 void* Handle=nullptr;
};
class KKUNITYBUNDLE_API FOkAppearanceContext
{
public:
 FOkAppearanceContext()=default;
 ~FOkAppearanceContext();
 FOkAppearanceContext(const FOkAppearanceContext&)=delete;
 FOkAppearanceContext& operator=(const FOkAppearanceContext&)=delete;
 bool Open(const FString& Root,const FString& Card,const FString& Resources,const FString& Cache,int32 Outfit,FString& Error);
 TSharedPtr<FOkAppearanceCandidate,ESPMode::ThreadSafe> Prepare(const FString& Target,const FString& Item,FString& Error);
 bool Commit(FOkAppearanceCandidate& Candidate,FOkSession& Session,bool Apply,FString& Error);
 TSharedPtr<FOkAppearanceCheckpoint,ESPMode::ThreadSafe> Capture(FString& Error);
 TSharedPtr<FOkAppearanceCheckpoint,ESPMode::ThreadSafe> Capture(FOkAppearanceCandidate& Candidate,FString& Error);
 TSharedPtr<FOkAppearanceGeometryCandidate,ESPMode::ThreadSafe> PrepareGeometry(const FString& Target,const FString& Item,FString& Error);
 // Independent owned session (added renderers + global rig) for unregistered
 // UE staging. It is never a publication input; Commit uses the candidate.
 bool StageGeometry(const FOkAppearanceGeometryCandidate& Candidate,FOkSession& Destination,FString& Error);
 TSharedPtr<FOkAppearanceCheckpoint,ESPMode::ThreadSafe> Capture(FOkAppearanceGeometryCandidate& Candidate,FString& Error);
 // Apply=false validates only. A rejected commit retains the candidate.
 bool CommitGeometry(FOkAppearanceGeometryCandidate& Candidate,FOkSession& Session,bool Apply,FString& Error);
private:
 void* Handle=nullptr;
};

/** OK base library models (.okmodel): contents, edits (overlay applied whenever the model is read) and textures. */
struct KKUNITYBUNDLE_API FOkModel
{
	/** <library>/models/xx/<id>.okmodel -> library and id. */
	static bool Locate(const FString& ModelPath, FString& OutLibrary, FString& OutId);
	static bool Info(const FString& Library, const FString& ModelId, FString& OutJson, FString& OutError);
	static bool GetEdits(const FString& Library, const FString& ModelId, FString& OutJson);
	static bool SetEdits(const FString& Library, const FString& ModelId, const FString& Json, FString& OutError);
	/** First prefab of the model with shown meshes (cheap; Info lists the whole model). */
	static bool FirstPrefab(const FString& Library, const FString& ModelId, FString& OutPrefab, FString& OutError);
	// Save the model with its current edits as a new library model; OutModelId is the new id.
	static bool SaveAs(const FString& Library, const FString& ModelId, FString& OutModelId, FString& OutError);
	static bool TexturePng(const FString& Library, const FString& ModelId, const FString& Texture, TArray<uint8>& OutPng);
};
