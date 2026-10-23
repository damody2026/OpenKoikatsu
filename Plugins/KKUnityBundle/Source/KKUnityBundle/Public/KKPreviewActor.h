#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "KKRustExtract.h"
#include "KKFaceAnimation.h"
#include "KKSpeech.h"
#include "KKAnimationReadyClock.h"
#include "KKGpuStream.h"
#include "RenderCommandFence.h"
#include "ProceduralMeshComponent.h"
#include "Containers/Ticker.h"
class FOkAnimPlayer;

#include "KKPreviewActor.generated.h"

class UProceduralMeshComponent;
class USkeletalMeshComponent;
class UPoseableMeshComponent;
class UPrimitiveComponent;
class USkeleton;
class UTexture;
class UTexture2D;
class UAnimSequence;
class ACameraActor;
class UCameraComponent;
class USceneCaptureComponent2D;
class UTextureRenderTarget2D;
class UMaterialInstanceDynamic;
struct FKKPrivateGpuDisplay;

USTRUCT()
struct FKKPreviewMaterialSlot
{
 GENERATED_BODY()
 UPROPERTY() TObjectPtr<class UMeshComponent> Component;
 UPROPERTY() TObjectPtr<UMaterialInterface> FinalMaterial;
 UPROPERTY() TObjectPtr<UMaterialInterface> PreviewMaterial;
 int32 Slot=0;
};

UENUM()
enum class EOKMaterialOverrideKind : uint8
{
 Scalar,  // float uniform (Dim 1)
 Vector,  // float4 written as given (UV transforms, _ST)
 Color,   // Unity Color property, sRGB-authored like the card: RGB goes to linear, alpha stays
 Texture  // texture property (PNG bytes: OKCreateRuntimeTextureFromPng; pattern masks are linear, SRGB=false)
};

/** One material parameter set on matching parts (AKKPreviewActor::SetMaterialOverrides). Parameter names are the
 *  Unity shader property names (_Color, _PatternMask1, _Patternuv1 ...). Empty Role/SourceProperty/Material and
 *  Slot INDEX_NONE match any part. */
USTRUCT()
struct FOKMaterialOverride
{
 GENERATED_BODY()
 UPROPERTY() FString Role;            // FKKLoadedPart::Role ("body", "head", "accessory", ...)
 UPROPERTY() FString SourceProperty;  // the card slot an accessory came from (session part source_property)
 UPROPERTY() int32 Slot = INDEX_NONE; // session part index
 UPROPERTY() FString Material;        // source material name
 UPROPERTY() FName Parameter;
 UPROPERTY() EOKMaterialOverrideKind Kind = EOKMaterialOverrideKind::Scalar;
 UPROPERTY() float Scalar = 0.f;
 UPROPERTY() FLinearColor Vector = FLinearColor::White;
 UPROPERTY() TObjectPtr<UTexture> Texture;
 // The same image as a linear (non-sRGB) texture, used where the material's own texture is linear (its sampler reads
 // linear); empty: Texture everywhere.
 UPROPERTY() TObjectPtr<UTexture> TextureLinear;
};

/** A parameter value as it was before an override wrote it, restored when the override goes away. */
USTRUCT()
struct FOKMaterialOverrideOriginal
{
 GENERATED_BODY()
 UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> Material;
 FName Parameter;
 EOKMaterialOverrideKind Kind = EOKMaterialOverrideKind::Scalar;
 float Scalar = 0.f;
 FLinearColor Vector;
 UPROPERTY() TObjectPtr<UTexture> Texture;
};

USTRUCT()
struct FKKLoadedPart
{
	GENERATED_BODY()

	UPROPERTY()
	FString Role;

	UPROPERTY()
	TObjectPtr<UProceduralMeshComponent> Comp;

	TArray<FVector> BaseVertices;
	TArray<FVector> BaseNormals;
	TArray<int32> Triangles;
	TArray<FVector2D> UVs;
	TArray<FVector2D> UV4;
	TArray<FVector2D> UV2;
	TArray<FVector2D> UV3;
	// Immutable loaded eye coordinates; live gaze changes only shader rows.
	TArray<FVector2D> GpuEyeUV[3];
	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> GpuEyeMaterial;
	bool bNativeFaceDraw=false;
	TArray<FColor> Colors;
	FLinearColor OverColor = FLinearColor::White;
	FLinearColor ShadowColor = FLinearColor(0.628f, 0.628f, 0.628f, 1.f);
	FLinearColor LineColor = FLinearColor::Black;
	float ShadowExtend = 1.f;
	float NormalDetailScale = 0.f;
	int32 ShaderKind = 0;
	FString TexturePath;

	UPROPERTY()
	FString Blend;

	UPROPERTY()
	bool bUnlit = false;
	FName RigidFollowBone;

	// True for cf_O_face: its normals are replaced by a sphere-normal proxy
	// (StudioEditor FaceSphereNormalProxy) so lit shading stays smooth.
	bool bFaceSphereNormals = false;

	// Non-zero for the sclera/iris layers. They sit at nearly the same depth, so
	// they are drawn as sorted translucency (StudioEditor eye render queues) to
	// stop z-fighting; sclera=1 draws before iris=2.
	int32 EyeSort = 0;

	UPROPERTY()
	TObjectPtr<UTexture2D> Texture;

	UPROPERTY()
	TObjectPtr<UTexture2D> Overtex;

	UPROPERTY()
	TObjectPtr<UTexture2D> Overtex2;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> SurfaceMat;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> OverMat;

	int32 SessionIndex = INDEX_NONE;
	FName AttachSocket;
	bool bSlotAttached = false;
	bool bRuntimeAccessory = false;
	// The accessory's extra outline/overlay sections share the source vertices.
	TArray<TArray<FVector>> RuntimeSectionOffsets;
	TArray<TArray<FVector>> RuntimeSectionNormals;
	TArray<FVector> RuntimeRestNormals;
    TArray<FQuat> RuntimeSourceNormalMotion;
    TArray<uint8> RuntimeNormalMotionUses;
    bool bRuntimeNormalMotionReusable=false;
    bool bRuntimeSourceNormalMotionPrepared=false;
	// Compact draw vertex -> canonical source vertex; empty means identity.
	TArray<TArray<int32>> RuntimeDrawVertices;
	TArray<FVector4f> RuntimeSourceTangents;
	// Last uploaded native streams for large non-morph renderers.
	TArray<float> RuntimeSourceGeometry;
    // Authored rest normals are immutable for this loaded part. Built with the
    // exact legacy GetSafeNormal formula whenever RuntimeRestNormals is assigned.
    TArray<FVector> RuntimeUnitRestNormals;
    bool bNativeDisplayPrepared=false;
    TSharedPtr<FKKPrivateGpuDisplay> PrivateGpuDisplay;
    uint64 PrivateGpuRejectedEpoch=0;
    uint64 PrivateGpuRejectedRevision=0;
};

UCLASS()
class KKUNITYBUNDLE_API AKKPreviewActor : public AActor
{
	GENERATED_BODY()

public:
    /** Between scene loads with source jobs stopped: drop decoded CPU image/source caches, retaining live actor data. */
    static void ReleaseSceneCaches();
	AKKPreviewActor();

	UFUNCTION(BlueprintCallable, Category = "OpenKoikatsu")
	bool LoadConvertedPreview(FString& OutError);

	UFUNCTION(BlueprintCallable, Category = "OpenKoikatsu")
	bool LoadDirectory(const FString& Dir, FString& OutError);

	UFUNCTION(BlueprintCallable, Category = "OpenKoikatsu")
	void ApplyShapes(const TArray<float>& Face, const TArray<float>& Body);

	void MarkShapeDirty(const TArray<float>& Face, const TArray<float>& Body);
	void SetPoseMode(int32 Mode);
	bool SetGameAnimation(const FString& ClipName, bool bPublishFrame = true);
	bool PreparePlaybackMaterials(bool Wait=true);
 static bool PreparePreviewMaterialParents(bool Save=false);
 // Loads the preview parents and starts their shader maps (a reader calls it while idle, so the first card's
 // progressive materials do not wait ~0.4 s for them). True once all three are ready.
 static bool PrewarmPreviewMaterialParents(UWorld* World);
 bool BeginProgressiveMaterials();
 bool TickProgressiveMaterials();
 bool SetClothingHidden(bool Hidden);
 bool bClothingHidden=false;
 uint8 DisplayClothesStates[9]={};
 /** Item colour overrides (sRGB, Unity _Color.._Color4 of item_draw materials), set before LoadSession; alpha < 0: keep the model colour. */
 TArray<FLinearColor> ItemColorOverrides;
 /** Item shadow colour override (sRGB; the item_draw materials' _ShadowColor, Studio's item shadow colour), set before LoadSession; alpha < 0: keep the model's. */
 FLinearColor ItemShadowColorOverride=FLinearColor(1.f,1.f,1.f,-1.f);
 /** Studio item alpha (< 1: the item_draw parts blend with this opacity), set before LoadSession. */
 float ItemAlpha=1.f;
 /** false: no outline shell on any part (a Studio item made without outline, ItemComponent.defLineWidth 0). */
 bool bItemOutline=true;
 /** false (set before LoadSession): every part gets its own material graph instead of a shared parent instance
  *  (OK Studio maps: their lightmaps and emission live in those graphs). */
 bool bUseSharedParents=true;
 /** An OK Studio item (set before LoadSession): its shared parent instances (the game's own item program) shade with
  *  the toon shadow and its _ShadowColor, as Studio does, whatever the character display-shadow setting. */
 bool bStudioItemShading=false;
    /** Native map material environment; independent of Gallery world/camera ownership. */
    bool bSourceMapShading=false;
 /** Shared parent root this actor's instances come from instead of the default one (set before LoadSession; empty: the
  *  command line / manifest / editor default). OK Studio items use a root that also has M_ForgeStudioOpaque. */
 FString SharedMaterialRootOverride;
 /** Shader globals of the OK Studio scene (Unity values, as Studio's Shader.SetGlobal: _ambientshadowG, _LineColorG,
  *  _linewidthG), set before LoadSession; they go to this actor's shared parent instances. */
 TMap<FName,FLinearColor> StudioShaderGlobals;
 /** A toon ramp the host chooses instead of the character display ramp (OK Studio's scene ramp, Studio's global
  *  _RampG); set before LoadSession, kept across reloads. The texture must stay alive (OKLoadRenderRamp roots it). */
 UTexture2D* RampOverride=nullptr;
 /** Material parameter overrides for this actor's parts (replaces the whole list; empty clears and restores the
  *  original values). They are written at once to each matching part's own MID — materials are shared between parts
  *  (KKMaterialDedupe), so an override never edits a graph — and again after every rebuild and when a progressive
  *  stand-in gives way to its final material. Returns the number of parameter writes; OutUnapplied lists overrides no
  *  part material has a parameter for. In the editor a part's graph exposes an overridden uniform as a parameter only
  *  when the override is known at build time (pattern slots, Forge item colours): set those before LoadSession, or
  *  reload the actor when OutUnapplied is not empty. Cooked shared materials have every uniform as a parameter. */
 int32 SetMaterialOverrides(const TArray<FOKMaterialOverride>& Overrides, TArray<FString>* OutUnapplied = nullptr);
 int32 ApplyMaterialOverrides(TArray<FString>* OutUnapplied = nullptr);
 /** Parameter names the overrides name for one part (the editor graphs expose these as parameters). */
 TSet<FString> MaterialOverrideNames(int32 SessionIndex, const FString& PartRole, const FString& Material, const FString& SourceProperty) const;
 UPROPERTY() TArray<FOKMaterialOverride> MaterialOverrides;
 UPROPERTY() TArray<FOKMaterialOverrideOriginal> MaterialOverrideOriginals;
 /** Studio FK (bone -> Unity local Euler degrees; empty clears) and re-poses the character. */
 bool SetUserFk(const TArray<FString>& Bones, const TArray<FVector>& UnityEuler);
 /** Studio IK targets (effector index, skeleton-space Unity metres; empty clears) and re-poses the character. */
 bool SetStudioIk(const TArray<int32>& Effectors, const TArray<FVector>& UnityPositions, FString& OutError, const TArray<FQuat>& UnityRotations = {}, const TArray<uint8>& UseRotations = {});
 bool bStudioIkReady=false;
 /** Unity skeleton space (metres) <-> this actor's local space (cm), as the skeleton converter maps positions. */
 static FVector UnityToActorLocal(const FVector& U) { return FVector(-U.X, U.Z, U.Y) * 100.0; }
 static FVector ActorLocalToUnity(const FVector& L) { return FVector(-L.X, L.Z, L.Y) / 100.0; }
 bool bUserPoseDirty=false;
 UPROPERTY() TArray<FKKPreviewMaterialSlot> PendingPreviewMaterials;
 UPROPERTY() TArray<TObjectPtr<class UMeshComponent>> HiddenClothingComponents;
 UPROPERTY() TArray<TObjectPtr<UMaterialInstanceDynamic>> ClothingMaskMaterials;
 UPROPERTY() TArray<TObjectPtr<class UTexture>> ClothingOriginalMasks;
	bool bPlaybackMaterialsReady=false;
	bool bPlaybackRenderFencePending=false;
	FRenderCommandFence PlaybackRenderFence;
	UFUNCTION(BlueprintCallable, Category="Koikatsu|Animation")
	bool SetGameAnimationTrigger(const FString& Name);
	UFUNCTION(BlueprintCallable, Category="Koikatsu|Animation")
	bool CrossFadeGameAnimation(const FString& Name,float NormalizedDuration,float NormalizedOffset=0.f);
	UFUNCTION(BlueprintCallable, Category="Koikatsu|Animation")
	bool CrossFadeGameAnimationLayer(const FString& Name,int32 Layer,float NormalizedDuration,float NormalizedOffset=0.f);
	UFUNCTION(BlueprintPure, Category="Koikatsu|Animation")
	bool IsGameAnimationTransitioning() const {return PoseMode==2&&Session.IsAnimationTransitioning();}
	UFUNCTION(BlueprintPure, Category="Koikatsu|Animation")
	bool IsGameAnimationLayerTransitioning(int32 Layer) const {return PoseMode==2&&Session.IsAnimationLayerTransitioning(Layer);}
	UFUNCTION(BlueprintPure, Category="Koikatsu|Animation")
	float GetGameAnimationLayerTransitionSourceLength(int32 Layer) const {return PoseMode==2?Session.AnimationLayerTransitionSourceLength(Layer):0.f;}
	UFUNCTION(BlueprintPure, Category="Koikatsu|Animation")
	TArray<FString> GetGameAnimationTriggers() const { return PoseMode==2?Session.AnimationTriggers():TArray<FString>(); }
	bool SetStudioAnimation(class UKKAnimationCatalog* Catalog, FName Row, FString& Error,
		float Height = 0.f, float Breast = 0.f, float Motion = 0.f);
	bool SetGameSceneMode(int32 Mode,bool bPublishFrame=true);
	void SetMouthOpen(float Open01);
	UFUNCTION(BlueprintCallable, Category="Koikatsu|Speech")
	bool LoadGeneralSpeech(const FString& VoiceRoot,int32 Personality,float VoicePitch,FString& Error);
	UFUNCTION(BlueprintCallable, Category="Koikatsu|Speech")
	bool SpeakGeneralLine(const FString& Id,FString& Error);
	UFUNCTION(BlueprintCallable, Category="Koikatsu|Speech")
	void StopSpeech();
	bool BeginHBreath(const TArray<FKKHBreathCue>& Cues,float Pitch,FString& Error);
	void StopHBreath();
	void SetSpeechPaused(bool bPaused);
	bool PlaySpeechFile(const FString& Path,float Pitch,FString& Error);
	bool PlaySpeechPcm(TSharedPtr<const FKKSpeechPcm,ESPMode::ThreadSafe> Pcm,float Pitch,FString& Error);
	bool bCoordinatedPlayback=false;
	/** An owner (OK Studio's scene actor) solves this actor's pose and eye look once per frame itself; its own Tick
	 *  then keeps only the rest (face clock, speech, shadow capture) instead of solving the pose a second time. */
	bool bSceneDrivenPose=false;
	/** Set by that owner once this actor's face parts tick after it (OrderSceneTicks): their pose is then evaluated by
	 *  their own tick (on a worker) instead of at once in SyncMouthPose. */
	bool bSceneTicksOrdered=false;
	/** Makes the face parts of this actor tick after `PoseOwner` (the actor posing it); true when there were any. */
	bool OrderSceneTicks(AActor* PoseOwner);
    bool bUsePrivateGpuPlayback=false; // opt in only while the interaction scene owns playback
	bool PlayHCue(FString& Error);
	UFUNCTION(BlueprintPure, Category="Koikatsu|Speech")
	TArray<FKKSpeechLine> GetGeneralSpeechLines() const {return SpeechLines;}
	UFUNCTION(BlueprintPure, Category="Koikatsu|Speech")
	bool IsSpeaking() const {return bSpeechActive;}
	void AdvanceSpeech();
	UFUNCTION() void SpeechFinished();
	UPROPERTY() TObjectPtr<class UAudioComponent> SpeechAudio;
	UPROPERTY() TObjectPtr<UKKSpeechWave> SpeechWave;
	UPROPERTY() TArray<FKKSpeechLine> SpeechLines;
	float SpeechPitch=1.f,SpeechOpen=0.f,SpeechOpenMin=0.f,SpeechPrevious=0.f;
	bool bSpeechActive=false,bHBreath=false,bSpeechPaused=false;
	bool bSpeechDrivesMouth=true;
	bool bReuseSpeechAudio=false;
 // Opt-in for a single owner publishing every poseable part's identical pose.
 // Legacy per-component bone editing keeps independent transform storage.
 bool bShareIdenticalPose=false;
	bool bManualSpeechPattern=false;
	TArray<FKKHBreathCue> HBreaths;
	TArray<FKKHBreathCue> PendingHBreaths;
	bool bHBreathQueuePending=false;
	void QueueHBreathCues(const TArray<FKKHBreathCue>& Cues){if(bHBreath){PendingHBreaths=Cues;bHBreathQueuePending=true;}}
	int32 HBreathIndex=0,SavedMouthPattern=0;
	float SavedMouthOpen=0.f;
	void SetMouthPattern(int32 Pattern, bool bBlend = true);
	int32 GetMouthPatternCount() const;
	FString GetMouthPatternLabel(int32 Pattern) const;
	// Manual fixed-rate controls: 1=Eyes, 2=Eyebrow. Source target mappings.
	UFUNCTION(BlueprintCallable, Category="Koikatsu|Expression")
	bool SetFaceControl(int32 Control, int32 Pattern, float Open01, bool bBlend = false);
	UFUNCTION(BlueprintCallable, Category="Koikatsu|Expression")
	bool SetFacePattern(int32 Control, int32 Pattern, bool bBlend = true);
	UFUNCTION(BlueprintCallable, Category="Koikatsu|Expression")
	void SetAutomaticBlink(bool bEnabled);
	UFUNCTION(BlueprintCallable, Category="Koikatsu|Expression")
	void ForceBlink();
	// Normalized source EyeLookCalc rates, each in [-1,1].
	UFUNCTION(BlueprintCallable, Category="Koikatsu|Expression")
	bool SetEyeGaze(float LeftHorizontal, float RightHorizontal, float Vertical);
	float EyeGaze[3] = {0.f,0.f,0.f};
	UFUNCTION(BlueprintCallable, Category="Koikatsu|Face")
	bool SetEyeLookTarget(FVector WorldTarget, int32 Pattern, bool Enabled = true);
	UFUNCTION(BlueprintCallable, Category="Koikatsu|Face")
	void StopEyeLookTracking();
	UFUNCTION(BlueprintCallable, Category="Koikatsu|Face")
	bool FollowEyeLookCamera(int32 Pattern = 1);
	bool AdvanceEyeLook(float DeltaSeconds);
	bool FollowHeadLookCamera(bool Enabled);
	/** Host solver (OKPhysXSecondary) for this character. Each user adds itself and removes itself after; with no
	 *  user the character keeps the built-in DynamicBone solver. Users: the interaction scene, a paired playback,
	 *  and the gallery physics menu (UseGalleryPhysics). Crowd and street NPCs never register. */
	void AddPairPhysics();
	void RemovePairPhysics();
	/** One permanent user so the gallery physics menu applies to this character. Idempotent. */
	void UseGalleryPhysics();
	int32 PairPhysicsUsers=0;
	bool bGalleryPhysics=false;
	void UpdateHeadLookTarget();
	bool bHeadLookFollowCamera=false;
    bool bHeadLookPoseDirty=false;
    bool bBodyReactionPoseDirty=false;
	bool bEyeLookTracking = false;
	bool bEyeLookFollowCamera = false;
	int32 EyeLookPattern = 1;
	FVector EyeLookWorldTarget = FVector::ZeroVector;
	void AdvanceFaceAnimation(float DeltaSeconds);
	float EffectiveFaceRate(int32 Control) const;
	FOKFbsTransition FaceTransitions[3];
	FOKFbsBlink FaceBlink;
	float FaceClock = 0.f, BlinkEyeRate = 1.f, BlinkBrowRate = 1.f;
	bool bAutomaticBlink = false, bManualBlinkSetting = false, bBrowSyncBlink = true;
	UFUNCTION(BlueprintPure, Category="Koikatsu|Expression")
	int32 GetFacePatternCount(int32 Control) const;
	UFUNCTION(BlueprintPure, Category="Koikatsu|Expression")
	FString GetFacePatternLabel(int32 Control, int32 Pattern) const;
	float FaceOpen01[2] = {0.92f, 1.f};
	int32 FacePattern[2] = {0, 0};
	bool bManualFaceControl[2] = {false, false};
	UFUNCTION(BlueprintCallable, Category="Koikatsu|Animation")
	bool SetHandShape(int32 Side, bool Enabled, int32 A, int32 B, float Blend);
	UFUNCTION(BlueprintPure, Category="Koikatsu|Animation")
	int32 GetHandShapeCount() const;
	void BeginHandDisplayUpdate() { ++HandDisplayBatchDepth; }
	void EndHandDisplayUpdate();
	int32 HandDisplayBatchDepth = 0;
	bool bHandDisplayPending = false;
	// Whether each hand's override is on in this session (a loaded session has both off).
	bool bHandOverrideOn[2] = {false, false};
	// OK_FIRSTLOAD=C3: final materials whose shader compile jobs are still to be submitted, a few per frame.
	TArray<TWeakObjectPtr<UMaterialInterface>> DeferredCompileSubmits;
	double ProgressiveMaterialsBegin = 0;

	UFUNCTION(BlueprintCallable, Category = "OpenKoikatsu")
	void PlayAnimSequence(UAnimSequence* Seq, float Time = 0.f);

	UFUNCTION(BlueprintCallable, Category = "OpenKoikatsu")
	void StartAnimLoop();

	void SetupStudioView(bool PreserveWorld=false);
	UFUNCTION(BlueprintCallable, Category = "OpenKoikatsu")
	void FrameCurrentPose();
	void FramePairedPose(AKKPreviewActor* Partner);
	/** World transform of the component that draws the skeleton (not a rigid item that follows a bone). */
	FTransform SkeletonWorldTransform() const;
	/** Points around the main joints of the current pose (head, torso, hands, knees, feet); false if the rig has none. */
	bool AppendJointFramePoints(TArray<FVector>& Points) const;
    bool AreInteractionStreamsReady() const;

	// Orbit around the character. Yaw/pitch are degrees; wheel is notches (+ = zoom in).
	void ResetOrbit(const FVector& Focus, const FVector& CamLoc);
	// KKS button bits: left=1, right=2, middle=4; input pixels, screen Y down.
	void OrbitMouse(uint8 Buttons, float DeltaX, float DeltaY);
	void OrbitDrag(float DeltaX, float DeltaY);
	void OrbitZoom(float WheelNotches);
	void ApplyOrbitCamera();
	// Character-maker key light: parented to the camera, so the toon light and the
	// main UE directional light come from the camera side. Only the standalone
	// studio view (SetupStudioView without PreserveWorld) owns this light.
	void UpdateCameraKeyLight();
	bool bCameraKeyLight = false;
	TWeakObjectPtr<class ADirectionalLight> StudioKeyLight;
	// Face close-up: ramp (null keeps the card ramp) plus a key light that arcs from the
	// character's left, across the front, to her right. The tick runs the arc; false if one
	// is already running or no material exposes _RampG.
	bool BeginProductCloseup(const FString& RampLabel, UTexture2D* Ramp, float Seconds, FString& Error);
	void AdvanceProductSweep(float DeltaSeconds);
	int32 SetDisplayRamp(UTexture2D* Ramp);
	void LogProductLight(const TCHAR* Tag) const;
	bool bProductSweep = false;
	bool bProductLightLatched = false;
	float ProductSweepElapsed = 0.f;
	float ProductSweepSeconds = 5.f;
	float ProductBaseDistance = 0.f;
	int32 ProductRampSlots = 0;
	int32 ProductLightStage = 0;
	FString ProductRampLabel;
	TMap<TWeakObjectPtr<UMaterialInstanceDynamic>, TWeakObjectPtr<UTexture>> ProductCardRamps;

	virtual void Tick(float DeltaSeconds) override;
	virtual void TickActor(float DeltaSeconds, ELevelTick TickType, FActorTickFunction& ThisTickFunction) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Destroyed() override;
	// Render the frozen pose's depth without advancing animation/face/physics.
	bool RefreshShotShadow(bool bDeferred = false);
#if WITH_DEV_AUTOMATION_TESTS
    bool VerifyDeferredShadowTarget(FString& Error);
#endif
    // Inactive editor previews retain resources without automatically ticking renderers.
    // Repeated suspension includes renderers added by incremental loading.
    void SuspendDisplayTicks(bool bSuspend);
    TMap<TWeakObjectPtr<UActorComponent>,bool> SuspendedDisplayTicks;
    void SetCoordinatedPlayback(bool bEnabled);
    TMap<TWeakObjectPtr<UActorComponent>,bool> CoordinatedComponentTicks;

	bool LoadSession(FString& OutError);
    bool ApplyHeadTextureSession(const FOkSession& Source,FString& Error);
    bool ApplyHeadTextureCandidate(class FOkAppearanceContext& Context,class FOkAppearanceCandidate& Candidate,FString& Error);
    // Exact-slot accessory transaction: stages unregistered renderers, then
    // publishes native state and UE components together. Fails without change.
    bool ApplyAccessoryGeometryCandidate(class FOkAppearanceContext& Context,class FOkAppearanceGeometryCandidate& Candidate,FString& Error,TSharedPtr<class FJsonObject> Timing=nullptr);
	// Engine objects stay on the game thread; worker threads prepare source bytes.
	// Hidden previews consume one renderer per tick and publish only when complete.
	bool BeginSessionLoad(FString& OutError);
	bool ContinueSessionLoad(FString& OutError);
	static constexpr int32 SessionLoadSetupStages = 5;
	bool IsSessionLoadComplete() const { return bSessionLoadComplete; }
private:
	struct FSessionLoadState;
    struct FPartBuildOutput;
    bool BuildSessionPartRange(FOkSession& Source,FPartBuildOutput& Output,int32 Begin,int32 End,FString& OutError);
	TSharedPtr<FSessionLoadState> SessionLoadState;
	bool LoadSessionBatch(FString& OutError, int32 PartLimit, bool bContinue);
	int32 SessionLoadNextPart = 0, SessionLoadExtra = 0;
	bool bSessionLoadComplete = false;
	TSet<FString> SessionLoadSeenHead;
	TMap<FString, int32> SessionLoadTranslucentDepth;
	UPROPERTY(Transient) TObjectPtr<UTexture2D> SessionLoadWhiteRamp;
public:
	// Call on the session's owning worker before publishing it to the renderer.
	// The caller captures the renderer mode before handing exclusive session
	// ownership to a worker. Preparing reference data does not advance playback.
	static void PrecacheSessionTextures(FOkSession& Source, bool bPrepareSkeleton = false);
	// The same in two steps: collect the session's textures (copies, so the session may change meanwhile), then
	// decode them (any thread). A load decodes while it applies the card's mods and shape (OK_FIRSTLOAD=B1).
	static TSharedRef<struct FOKSessionTextureJobs, ESPMode::ThreadSafe> CollectSessionTextures(FOkSession& Source, bool bPrepareSkeleton = false);
	static void PrecacheTextureJobs(const struct FOKSessionTextureJobs& Jobs);
	// First-load speed options (OK_FIRSTLOAD, comma list or "all"; read once): A1 A2 A3 B1 C2 C3 C4.
	static bool FirstLoadOption(const TCHAR* Name);

	// Destroy the previously loaded part components (keeping the persistent
	// BodyMesh/HeadMesh) so loading another outfit in the same process does not
	// stack meshes on top of each other.
	void DestroyAllParts();
	// Separate penis/scrotum renderers. Hidden until the gallery asks.
	void SetGenitalVisible(bool bVisible);
	bool IsGenitalVisible() const { return bGenitalVisible; }
	bool bGenitalVisible = false;
	TArray<TObjectPtr<UPrimitiveComponent>> GenitalPrimitives;
	void ApplyGenitalVisibility();

	FOkSession Session;
	UPROPERTY()
	TObjectPtr<UTexture2D> ToonRampTex;
    bool bCharacterDisplayShadows=true;
    // A scene host can preserve authored shading independently of Gallery's
    // display preference. Unset keeps the normal character viewer policy.
    TOptional<bool> CharacterDisplayShadowsOverride;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UProceduralMeshComponent> BodyMesh;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UProceduralMeshComponent> HeadMesh;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UPoseableMeshComponent> BodySkel;

	UPROPERTY()
	TObjectPtr<USkeleton> SharedSkel;

	UPROPERTY()
	TArray<TObjectPtr<UPoseableMeshComponent>> SkelFollowers;

	UPROPERTY()
	TArray<TObjectPtr<UProceduralMeshComponent>> SkelPmcs;

	UPROPERTY()
	TObjectPtr<UAnimSequence> WalkSeq;

	UFUNCTION(BlueprintCallable, Category = "OpenKoikatsu|Animation")
	bool SetUnrealAnimation(UAnimSequence* Sequence, bool bLoop = true);
	UFUNCTION(BlueprintCallable, Category="Koikatsu|Animation")
	bool SetGameAnimationFloat(const FString& Name, float Value);

	UPROPERTY()
	TObjectPtr<ACameraActor> StudioCam;

	UPROPERTY()
	TObjectPtr<UCameraComponent> StudioLens;

	float WalkTime = 0.f;

	FVector OrbitFocus = FVector::ZeroVector;
	float OrbitYaw = 0.f;
	float OrbitPitch = 0.f;
	float OrbitDistance = 800.f;

	// 0 neutral/static, 1 Unreal AnimSequence, 2 native KK Animator, 3 own OK animation player (OkPlayer, one role),
	// 4 posed externally every frame (an OK group player drives several characters; this actor only shows them).
	int32 PoseMode = 0;
	TSharedPtr<FOkAnimPlayer> OkPlayer;
	FString OkReport;
	/** Plays an OK spec on this character alone (PoseMode 3); the player's role 0 is this character. */
	bool SetOkAnimation(TSharedPtr<FOkAnimPlayer> Player, bool bPublishFrame = true);
	/** PoseMode 4 on, or back to the neutral pose. */
	void SetExternalPoseDriver(bool bExternal);
	// FBSCtrlMouth pattern 0 at 80% open. GPU skin leaves kuti_* for these morphs.
	float MouthOpen01 = 0.f;
	int32 MouthPattern = 0;
	bool bShapeDirty = false;
	double ShapeRequestedAt=0.,ShapeUpdateTotalMs=0.,ShapeUpdateMaxMs=0.,ShapeLatencyMaxMs=0.;
	uint64 ShapeUpdateSamples=0;
	TArray<float> PendingFace;
	TArray<float> PendingBody;

	struct FOkMouthSlot
	{
		class USkeletalMeshComponent* Comp = nullptr;
		class USkeletalMeshComponent* OutlineComp = nullptr;
		UProceduralMeshComponent* CpuComp = nullptr;
		int32 PartArrayIndex = INDEX_NONE;
		int32 SessionPartIndex = INDEX_NONE;
		TArray<TPair<FName, FName>> Patterns;
		TArray<TPair<FName, FName>> EyePatterns;
		TArray<TPair<FName, FName>> BrowPatterns;
		TMap<FName, float> BaselineWeights;
        TArray<FName> MorphNames;
		bool bHideWhenClosed = false;
	};
	TArray<FOkMouthSlot> MouthSlots;
	// Native renderer morph weights, in Unity's percent units. Runtime only;
	// reload/destroy clears them so one character cannot inherit another's.
	TMap<int32, TMap<FName, float>> PartMorphWeights;
	UFUNCTION(BlueprintCallable, Category="Koikatsu|Morph")
	TArray<FName> GetPartMorphNames(int32 SessionPartIndex) const;
	UFUNCTION(BlueprintCallable, Category="Koikatsu|Morph")
	bool SetPartMorphWeight(int32 SessionPartIndex, FName Name, float Percent);
	// Insertion depth within the current pose. Authored vagina.open only; bone
	// spread is used when that morph is absent. Range is owned by paired playback.
	void DriveVaginaOpening(const AKKPreviewActor& Male, float& RangeMin, float& RangeMax, bool& bRange);
	void CloseVaginaOpening();
	void ResolveVaginaMorph();
	int32 VaginaMorphPart=INDEX_NONE;
	FName VaginaMorphName;
	bool bVaginaMorphResolved=false;
	bool bVaginaOpeningLogged=false;
	TMap<FName, FTransform> RigidNeutralBones;
	// Every leader bone at the same load-time capture, so an exact-slot
	// accessory can adopt its socket's neutral without a mid-animation capture.
	TMap<FName, FTransform> RigidNeutralAllBones;
	TMap<FName, FMatrix> CpuRigidNeutralBones;

	// Rebuild edited body vertex buffers while retaining materials and playback.
 bool RefreshEditorGeometry();
	void RefreshSkinnedDisplay();
	void RefreshRuntimeAccessories();
    // Refresh CPU query vertices from the already solved native pose without
    // publishing CPU render streams over an installed GPU generation.
    void EnsurePrivateGpuQueryGeometry(UProceduralMeshComponent* Component=nullptr,int32 Section=INDEX_NONE);
    void PreparePrivateGpuQueryGeometry(TConstArrayView<UProceduralMeshComponent*> Components);
    bool QueryPrivateGpuSurfaceVertices(UProceduralMeshComponent* Component,int32 Section,TConstArrayView<int32> Vertices,TArray<FVector>& Out) const;
    bool CapturePrivateGpuSurface(UProceduralMeshComponent* Component,int32 Section,struct FKKPrivateGpuSurfaceView& Out) const;
    void SkinPrivateDisplay();
    bool UpdatePrivateGpuDisplay(FKKLoadedPart& Part,bool AllowPrepare,TArray<FKKGpuStreamSectionRequest>& Requests);
    void PreparePrivateGpuDisplayPoses();
    bool bNativeSkinReady=false;
    /** Publishes where this character's skeleton space sits in the world to the PhysX secondary dynamics (every pose update). */
    void PublishPhysicsFrame() const;
    static bool ApplyPairedFrameWithDisplay(AKKPreviewActor& Female,AKKPreviewActor& Male,float Time,float Delta,
        const FString& State,const FString& FemaleMotion,const FString& MaleMotion,FString& Error);
	void CaptureRigidFollowerNeutral();
	void UpdateRigidFollowers();
	void SyncMouthPose();
	// Defer display rebuilds while native face setters still update their state.
	void BeginFaceDisplayUpdate() { ++FaceDisplayBatchDepth; }
	void EndFaceDisplayUpdate();
	int32 FaceDisplayBatchDepth = 0;
	bool bFaceDisplayPending = false;
	void ApplyMouth();
	void InitializeFaceControls();
	void ApplyPoseFrame(float DeltaSeconds,bool bAdvanceAnimation=true);
	/** ApplyPoseFrame in two halves for an owner posing many actors: the native solve (any thread, one actor per thread;
	 *  false: this pose needs ApplyPoseFrame on the game thread instead), then the display on the game thread. */
	bool SolveScenePose(float DeltaSeconds,bool bAdvanceAnimation);
	void FinishScenePose();
	bool bPendingSceneDisplay=false,bPendingSceneFaceCurves=false,bPendingSceneFailed=false;
	TMap<FString,float> PendingSceneFaceCurves;
	/** SolveScenePose's time so far (diagnostics: OKStudio.PerfKStudio). */
	double SceneSolveTotalMs=0.;int32 SceneSolveSamples=0;
	/** Which of the alternate frames this actor captures its toon shadow on (ok.ShadowCaptureStagger). */
	uint32 ShadowCapturePhase=MAX_uint32;
	bool AuditBodyDynamicsDisplay(const FString& TracePath, bool bExternal = false);
	bool AuditStudioPVDisplay();

	UPROPERTY(BlueprintReadOnly, Category = "OpenKoikatsu")
	TArray<FString> MissingMods;

	UPROPERTY(BlueprintReadOnly, Category = "OpenKoikatsu")
	TArray<FString> MappedSliders;

	UPROPERTY(BlueprintReadOnly, Category = "OpenKoikatsu")
	TArray<int32> UnmappedFace;

	UPROPERTY(BlueprintReadOnly, Category = "OpenKoikatsu")
	TArray<int32> UnmappedBody;

	UPROPERTY(BlueprintReadOnly, Category = "OpenKoikatsu")
	TArray<float> ShapeFace;

	UPROPERTY(BlueprintReadOnly, Category = "OpenKoikatsu")
	TArray<float> ShapeBody;

	bool bLoadBodyHeadOnly = false;
    bool bPreserveSourceWorld=false;

	UPROPERTY()
	TArray<FKKLoadedPart> Parts;

	// Per-actor scratch streams retain capacity between animation frames.
	// UpdateMeshSection copies them into its own render command before reuse.
	TArray<TArray<FVector>> RuntimeMutableVertices, RuntimeMutableNormals;
	TArray<TArray<FProcMeshTangent>> RuntimeMutableTangents;

	UPROPERTY()
	TArray<TObjectPtr<UTexture2D>> LoadedTextures;
    UPROPERTY() TArray<TObjectPtr<class UMaterialInstanceDynamic>> CookedRoleMaterials;
    UPROPERTY() TArray<TObjectPtr<class UMaterialInstanceDynamic>> CookedRoleOutlines;
    /** Sets a vector parameter (a Unity uniform such as _WorldSpaceLightPos0) on every shared parent instance of this
     *  actor, outlines included; instances without it are left alone. */
    void SetSharedVectorParameter(FName Name, const FLinearColor& Value);
    void SetSharedTextureParameter(FName Name,UTexture* Value);
    /** True when every renderer with a recorded Unity layer (component tag OKLayer<n>) is on a layer outside LayerMask
     *  (a light with that culling mask does not reach this actor); false without recorded layers. */
    bool IsOutsideLayerMask(uint32 LayerMask) const;
    void BindSourceReflectionProbes(TFunctionRef<UTexture*(int32,FLinearColor&)> Resolve);

	UPROPERTY()
	TObjectPtr<USceneCaptureComponent2D> ToonShadowCapture;
	UPROPERTY()
	TArray<TObjectPtr<UTextureRenderTarget2D>> ToonShadowTargets;
	// Studio map cameras drawing into a RenderTexture (okmodel_view::mark_render_texture), by the prefab's camera index:
	// captured when built and then every ok.RenderTextureInterval seconds (not every frame).
	UPROPERTY()
	TMap<int32, TObjectPtr<USceneCaptureComponent2D>> RenderTextureCaptures;
	FTSTicker::FDelegateHandle RenderTextureTicker;
	UTextureRenderTarget2D* RenderTextureFor(FOkSession& Source, int32 PartIndex);
	void StopRenderTextureCaptures();
	UPROPERTY()
	TArray<TObjectPtr<UMaterialInstanceDynamic>> ToonShadowMaterials;
	int32 ToonShadowReadIndex = 0;
    int32 ToonShadowBoundIndex = INDEX_NONE;
    int32 ToonShadowBoundMaterialCount = 0;
    void BindToonShadowTarget(int32 Index);
	FVector ToonShadowCenter = FVector(0.f, 0.f, 90.f);
	FVector ToonShadowLightDir = FVector(-0.25f, 0.85f, 0.45f);
	FVector ToonShadowRight = FVector::RightVector;
	FVector ToonShadowUp = FVector::UpVector;
	float ToonShadowWidth = 260.f;

	bool bAnimLoop = false;
	// Game sampler elapsed time is never wrapped by the base layer duration.
	// Each source state owns its loop/clamp and independent layer epoch.
	float AnimPhase = 0.f;
    float AnimationPlaybackSpeed = 1.f;
	FKKAnimationReadyClock AnimationReadyClock;
	bool bWorldPoseTick=false;
	float WorldPoseDelta=0.f;
	// Measured actor work, excluding rendering and asynchronous clip loading.
	double AnimationTickTotalMs=0.,AnimationTickMaxMs=0.;
	uint64 AnimationTickSamples=0;
	// Fixed 1 ms bins; last bin includes all ticks >=255 ms. No growing history.
	uint64 AnimationTickHistogram[256]{};
	uint64 AnimationUploadedVertices=0,AnimationSkippedVertices=0;
	bool AuditAnimatorElapsedClockDisplay();
	bool AuditAnimatorTriggerDisplay();
	bool AuditAnimatorLayerFadeDisplay(const FString& Source);
	bool AuditAnimatorParallelFadeDisplay(const FString& Source);
	bool AuditAnimatorCrossFadeDisplay(const FString& Source,const FString& Destination);
	float AnimDuration = 1.f;
	bool bGameAnimLooping = true;
};
