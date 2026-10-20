#pragma once

#include "CoreMinimal.h"
#include "Toolkits/AssetEditorToolkit.h"
#include "TickableEditorObject.h"
#include "EditorUndoClient.h"
#include "UObject/GCObject.h"
#include "OKUnrealSkeleton.h"
#include "OKAnimationKeys.h"
#include "OKAnimationAssets.h"
#include "OKAnimationEditorToolkit.generated.h"

class FOkAnimPlayer;
class FAdvancedPreviewScene;
class FCurveEditor;
class IDetailsView;
class SOKAnimationViewport;
class UOKAnimationAsset;
class UOKAnimSequence;
class UPoseableMeshComponent;
class UAnimSequence;
class USkeletalMesh;
class UOKRetargetProfile;

/** Preview options of the OK animation editor (not saved with the asset). */
UCLASS(Transient)
class UOKAnimationPreviewSettings : public UObject
{
	GENERATED_BODY()

public:
	/** Mesh the animation is previewed and posed on (any skeleton a profile or a built-in preset maps). */
	UPROPERTY(EditAnywhere, Category = "Preview")
	TObjectPtr<USkeletalMesh> Mesh;
	UPROPERTY(EditAnywhere, Category = "Preview")
	TObjectPtr<UOKRetargetProfile> Profile;
	/** Role of a group shown on the mesh. */
	UPROPERTY(EditAnywhere, Category = "Preview", meta = (ClampMin = "0"))
	int32 Role = 0;
	UPROPERTY(EditAnywhere, Category = "Preview")
	float X = 0.f;
	UPROPERTY(EditAnywhere, Category = "Preview")
	float Y = 0.f;
	UPROPERTY(EditAnywhere, Category = "Preview", meta = (ClampMin = "0"))
	float PlayRate = 1.f;
	/** Group roles: body height of each role's preview (1 = the mesh as is), to see contacts adapt to sizes. */
	UPROPERTY(EditAnywhere, Category = "Preview", meta = (ClampMin = "0.5", ClampMax = "1.5"))
	TArray<float> RoleHeight;
	/** Retarget preview: an Unreal animation converted live (with RetargetProfile, or a built-in preset) and shown on the
	 *  preview mesh, its source playing beside it on the source skeleton's preview mesh. Editing the profile updates both. */
	UPROPERTY(EditAnywhere, Category = "Retarget")
	TObjectPtr<UAnimSequence> RetargetSource;
	UPROPERTY(EditAnywhere, Category = "Retarget")
	TObjectPtr<UOKRetargetProfile> RetargetProfile;
	/** Character cards previewed with the OK animation (the game's own character pipeline), side by side. */
	UPROPERTY(EditAnywhere, Category = "Cards", meta = (FilePathFilter = "png"))
	TArray<FFilePath> Cards;
	/** Pose library the Poses tab stores into and applies from (a transient one when empty). */
	UPROPERTY(EditAnywhere, Category = "Poses")
	TObjectPtr<UOKPoseLibrary> PoseLibrary;
	/** Weight of an applied pose (1 = the stored pose, less = blended from the current one). */
	UPROPERTY(EditAnywhere, Category = "Poses", meta = (ClampMin = "0", ClampMax = "1"))
	float PoseWeight = 1.f;
};

/**
 * Editor of OK animation assets, after Persona, with a Maya-style animate mode for OK animation sequences: pick
 * bones in the viewport (or the outline), pose them with the gizmo (rotate any bone, move the hips, move a limb end
 * with two-bone IK), key them into the animation's key layer (auto key or S), scrub and move keys on the timeline,
 * type values in the channel box, shape the curves in the graph editor; undo / redo throughout. The key layer sits
 * on top of the base motion and is baked into the animation; Collapse makes it the new base.
 */
class FOKAnimationEditor : public FAssetEditorToolkit, public FTickableEditorObject, public FGCObject, public FSelfRegisteringEditorUndoClient
{
public:
	void InitEditor(EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost>& Host, UOKAnimationAsset* Asset);
	virtual ~FOKAnimationEditor() override;

	// FAssetEditorToolkit
	virtual FName GetToolkitFName() const override { return TEXT("OKAnimationEditor"); }
	virtual FText GetBaseToolkitName() const override;
	virtual FString GetWorldCentricTabPrefix() const override { return TEXT("OK Animation "); }
	virtual FLinearColor GetWorldCentricTabColorScale() const override { return FLinearColor(0.85f, 0.35f, 0.55f, 0.5f); }
	virtual void RegisterTabSpawners(const TSharedRef<FTabManager>& TabManager) override;
	virtual void UnregisterTabSpawners(const TSharedRef<FTabManager>& TabManager) override;

	// FTickableEditorObject
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(FOKAnimationEditor, STATGROUP_Tickables); }
	virtual ETickableTickType GetTickableTickType() const override { return ETickableTickType::Always; }

	// FGCObject
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override { return TEXT("FOKAnimationEditor"); }

	// FEditorUndoClient
	virtual void PostUndo(bool bSuccess) override;
	virtual void PostRedo(bool bSuccess) override { PostUndo(bSuccess); }

	// --- Animate mode (viewport edit mode, timeline, channel box) ---
	bool CanAnimate() const;
	bool HasPreviewPose() const;
	const FOKUnrealSkeleton& GetBinding() const { return Binding; }
	const TArray<FTransform>& GetComponentPose() const;
	FTransform BoneTransform(int32 Bone) const;
	const TArray<int32>& GetSelection() const { return Selection; }
	void SelectBone(int32 Bone, bool bToggle);
	void ClearSelection();
	void PickWalk(bool bUp);
	void BeginEdit(const FText& Description);
	void EndEdit();
	void RotateSelection(const FQuat& WorldDelta);
	void TranslateSelection(const FVector& WorldDelta);
	void KeySelection(bool bRotation, bool bPosition);
	void KeyAllBones();
	void DeleteKeysAtCurrent();
	void CopyPose();
	void PastePose();
	void JumpKey(int32 Direction);
	void StepFrame(int32 Direction);
	void JumpEnd(bool bEnd);
	void TogglePlay();
	bool IsPlaying() const { return bPlaying; }
	bool IsAutoKey() const { return bAutoKey; }
	void SetAutoKey(bool bOn);
	/** Recording (R4.13): while playing, the bones being dragged hold their on-screen pose and are keyed on every frame passed. */
	void SetRecording(bool bOn);
	bool IsRecording() const { return bRecording; }
	bool HasKeyAtCurrentFrame(int32 Bone) const;

	/** Drives this editor through the animate tools and measures the results (OKAnim.EditorSelfTest). */
	void RunSelfTest(bool bExitWhenDone);
	static FOKAnimationEditor* LastOpened;

	/** Mirror the pose at this frame (left <-> right, lateral axis X of OK space): the selected bones' opposite side
	 * takes their mirrored pose; nothing selected = the whole pose mirrored (sides swapped). */
	void MirrorPose();
	/** A whole-motion modifier (OK op JSON) on the edited sequence, undoable. */
	void ApplyModifier(const FString& OpJson);
	/** Interpolation of the keys of the selected bones (all keyed bones when none) at this frame. */
	void SetKeyInterpolation(ERichCurveInterpMode Mode);
	void SetLength(int32 Frames);
	bool bShowGhosts = false;
	bool bShowTrail = true;
	int32 GhostStep = 5;
	/** Component-space positions of the mapped bones at another frame (ghosts) and of a bone over all frames (trail). */
	bool PosePositionsAt(float Seconds, TArray<FVector>& Out) const;
	const TArray<FVector>& GetTrail(int32 Bone);

	// Timeline
	int32 NumFrames() const;
	int32 CurrentFrame() const;
	UOKAnimSequence* Sequence() const;
	UOKAnimationAsset* GetEditedAsset() const { return Asset; }
	// --- Contacts (R4.1): the sequence's contacts as editable bars, drawn in the viewport ---
	int32 SelectedContact = INDEX_NONE;
	/** Selected bones -> a contact from the current frame for 0.5 s: two bones = the first reaches the last (same body),
	 *  one bone = it plants on the ground at its current height. Returns the new contact's index. */
	int32 AddContactFromSelection();
	void SetContactRange(int32 Index, float Start, float End);
	void DeleteContact(int32 Index);
	/** Contacts changed: written into the animation data (undoable through the caller's transaction). */
	void CommitContacts();
	// --- Pose library (R4.8) ---
	UOKPoseLibrary* PoseLibrary();
	/** The current pose of the selected bones (all when none) stored under Name (replacing a pose of that name). */
	bool StorePose(FName Name);
	/** Keys a stored pose at the current frame: every stored bone, or only the selected ones; blended by Weight. */
	bool ApplyPose(FName Name, bool bSelectedOnly, float Weight);
	// --- Multi-role (R4.6): a group's role slot is edited like a sequence; other roles stand where the group puts them ---
	/** Component-space placement of a role's preview relative to the edited role (offset, yaw, height). */
	FTransform RolePlacement(int32 Role) const;
	/** World positions of every mapped bone of a role's preview (the edited role: its component pose). */
	bool RoleBonePositions(int32 Role, TArray<FVector>& Out) const;
	void SetEditedRole(int32 Role);
	// --- Family mode (R4.7): the skeleton family edited from its rest animation ---
	TWeakObjectPtr<UOKSkeletonFamily> EditedFamily;
	UOKSkeletonFamily* FamilyForTab() const;
	/** Adds an IK chain from the selection (three bones, root to end) or the selected end bone's two parents. */
	bool AddIKChainFromSelection(FName Name);
	// --- Character card preview (R4.11) ---
	/** Loads Settings->Cards into the preview world (replacing earlier ones); returns how many loaded. */
	int32 LoadCardPreviews(FString& OutError);
	TArray<TWeakObjectPtr<class AKKPreviewActor>> CardActors;
	void RefreshCardAnimation();
	void TickCardPreviews();
	// --- Retarget panel (R4.12) ---
	bool IsRetargetPreview() const { return Settings && Settings->RetargetSource; }
	TObjectPtr<UPoseableMeshComponent> SourceMesh;
	TObjectPtr<UOKAnimSequence> RetargetResult;
	FOKUnrealSkeleton SourceBinding;
	// --- OK assets panel (R4.9) ---
	TSharedPtr<class STableViewBase> AssetsList;
	/** Quality problems of the edited sequence (frames), shown in the validation list and as red bars on the timeline. */
	TArray<TSharedPtr<UOKAnimSequence::FQualityIssue>> QualityIssues;

	float FrameRate() const;
	void SetFrame(int32 Frame);
	void SetTimeContinuous(float Seconds);
	float GetTime() const { return Time; }
	float Duration() const;
	void GetTimelineKeys(TArray<int32>& OutFrames) const;
	void MoveKeys(const TArray<int32>& Frames, int32 Delta);
	void DeleteKeys(const TArray<int32>& Frames);
	/** The same for given bones (dope sheet rows). */
	void MoveKeysOf(const TArray<FName>& Bones, const TArray<int32>& Frames, int32 Delta);
	void DeleteKeysOf(const TArray<FName>& Bones, const TArray<int32>& Frames);
	/** Keyed bones (dope sheet rows) in skeleton order, and the frames of one bone's keys. */
	TArray<FName> KeyedBones() const;
	void GetBoneKeyFrames(FName Bone, TArray<int32>& OutFrames) const;
	void SelectBoneByName(FName Bone, bool bToggle);

	// Blend space panel
	class UOKBlendSpace* BlendSpace() const;
	FVector2D GetBlendParams() const;
	void SetBlendParams(const FVector2D& Params);
	void MoveBlendSample(int32 Sample, const FVector2D& Position, bool bFinal);

	// Channel box
	FText PrimaryBoneName() const;
	bool PrimaryIsHips() const;
	float GetChannel(int32 Channel) const;
	void SetChannel(int32 Channel, float Value);
	bool ChannelKeyedNow(int32 Channel) const;

private:
	void Rebuild();
	void RebindMesh();
	void RefreshPlayer();
	void UpdatePose();
	void RunValidation();
	void FillToolbar(class FToolBarBuilder& Builder);
	void RefreshOutline();
	void RefreshCurves();
	uint32 LayerSignature() const;
	uint32 CurveShape() const;
	/** After any change of the key layer: bake, replay, repose (synchronous so the next gizmo step sees it). */
	void AfterLayerEdit();
	void SnapToFrame();
	void Remember(FName Bone);
	void RevertPending();
	TArray<int32> RootsOfSelection() const;
	TArray<FName> KeyedBonesForTimeline() const;
	void ApplyRotation(int32 Bone, const FQuat& WorldDelta);
	void ApplyHipsTranslation(const FVector& WorldDelta);
	void ApplyLimbIK(int32 Bone, const FVector& WorldDelta);

	TObjectPtr<UOKAnimationAsset> Asset;
	TObjectPtr<UOKAnimationPreviewSettings> Settings;
	TObjectPtr<UPoseableMeshComponent> PreviewMesh;
	/** The other roles of a group, on the same mesh (shown, not edited). */
	TArray<TObjectPtr<UPoseableMeshComponent>> RoleMeshes;
	TSharedPtr<FAdvancedPreviewScene> Scene;
	TSharedPtr<SOKAnimationViewport> Viewport;
	TSharedPtr<IDetailsView> AssetDetails, SettingsDetails;
	TSharedPtr<FOkAnimPlayer> Player;
	TSharedPtr<FCurveEditor> CurveEditor;
	FOKUnrealSkeleton Binding;
	TArray<TSharedPtr<FString>> Outline;
	TSharedPtr<class SListView<TSharedPtr<FString>>> OutlineView;
	FString ValidationText, Status;
	float Time = 0.f;
	bool bPlaying = true;
	bool bDirty = true;
	bool bNeedFocus = true;
	bool bAutoKey = true;
	bool bEditing = false;
	bool bRecording = false;
	bool bPosed = false;
	TArray<int32> Selection;
	/** Auto key off: the keys of bones edited at this frame before the edit (restored when the frame changes unless keyed). */
	TMap<FName, FOKBoneKeys> Pending;
	TMap<FName, TPair<FQuat, FVector>> Clipboard;
	uint32 LastSignature = 0;
	uint32 LastCurveShape = 0;
	TArray<FVector> Trail;
	uint32 TrailKey = 0;
	TOptional<FVector2D> SampleDragOrigin;
	FDelegateHandle ObjectChanged;
};
