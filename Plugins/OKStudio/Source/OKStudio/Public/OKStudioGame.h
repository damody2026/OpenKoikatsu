#pragma once

#include "CoreMinimal.h"
#include "OKSceneDocument.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "InputCoreTypes.h"
#include "Layout/Visibility.h"
#include "Tickable.h"
#include "UObject/Object.h"
#include "OKStudioGame.generated.h"

class AOKSceneActor;
class ACameraActor;
class SOKStudioPanel;
class SWidget;

/** The studio view's camera: the scene camera's near plane (Unreal has one global near plane otherwise). */
UCLASS()
class OKSTUDIO_API UOKStudioViewCamera : public UCameraComponent
{
	GENERATED_BODY()
public:
	/** Near clipping plane (cm); <= 0: the engine's. */
	float NearClip = 0.f;
	/** Orthographic view of the active scene camera (width cm; far plane cm, <= 0: far). */
	bool bOrtho = false;
	float OrthoViewWidth = 400.f, FarClip = 0.f;
	virtual void GetCameraView(float DeltaTime, FMinimalViewInfo& DesiredView) override;
};

UCLASS()
class OKSTUDIO_API AOKStudioViewer : public ACameraActor
{
	GENERATED_BODY()
public:
	AOKStudioViewer(const FObjectInitializer& Initializer);
};

/**
 * OK Studio inside the game: the shared scene tree shown in the running world, an orbit viewer camera and a Slate
 * panel (outliner, object properties, scene files). Started by `OKStudio.Play [scene.okscene]`.
 */
UCLASS()
class OKSTUDIO_API UOKStudioGame : public UObject, public FTickableGameObject
{
	GENERATED_BODY()

public:
	/** The running studio, if any. */
	static UOKStudioGame* Get();
	/** Starts the studio in a world (empty file: a new scene). */
	static UOKStudioGame* Start(UWorld* World, const FString& File, FString& OutError);
	void Stop();

	UPROPERTY()
	TObjectPtr<UOKSceneDocument> Document;

	UPROPERTY()
	TObjectPtr<AOKSceneActor> SceneActor;

	UPROPERTY()
	TObjectPtr<ACameraActor> Viewer;

	/** Orbit viewer (free camera): focus point, yaw / pitch (degrees) and distance (cm). */
	FVector Focus = FVector(0.f, 0.f, 90.f);
	float Yaw = -90.f;
	float Pitch = -8.f;
	float Distance = 380.f;

	FString File;
	/** The primary selection: the guide's pivot and the object the detail windows show (also in Selection). */
	int32 SelectedId = -1;
	/** Every selected object (the guide, delete, duplicate, hide and copy act on all of them). */
	TArray<int32> Selection;
	bool IsSelected(int32 Id) const { return Selection.Contains(Id); }
	/** Selects these objects; Primary (default: the last) becomes SelectedId. */
	void SelectMany(const TArray<int32>& Ids, int32 Primary = -1);
	/** Adds an object to the selection or takes it out (Ctrl / Shift click). */
	void ToggleSelect(int32 Id);
	/** Selected objects that are not under another selected object (a moved parent carries its children). */
	TArray<int32> SelectionRoots() const;

	/** The object under a panel position (-1: none): characters by their skeleton, other objects by their bounds; the
	 *  nearest to the camera wins. */
	int32 PickObject(const FVector2D& Position) const;
	/** World bounds of an object (a character's bones, an item's meshes; a light or camera: a small box). */
	bool WorldBounds(int32 Id, FBox& Out) const;
	/** Points the free view at the selection, far enough to see all of it; bFace: at one character's face, close up (the
	 *  look, neck and expression pages edit what the face shows). */
	void FocusSelection(bool bFace = false);
	/** Free view presets: 0 front, 1 right side, 2 top, 3 back, 4 left side, 5 the view a new scene opens with. */
	void ViewPreset(int32 Preset);
	/** View bookmarks 0..9 (kept in the scene). */
	void SaveBookmark(int32 Slot);
	bool LoadBookmark(int32 Slot);
	/** A camera object at the current view (selected); returns its id. */
	int32 CameraFromView();
	/** Moves a camera object to the current view. */
	bool StoreViewInCamera(int32 Id);
	/** Views through the next scene camera (after the last: the free view). */
	void NextCamera();

	/** Broadcast when the studio closes (the caller restores its own UI). */
	FSimpleMulticastDelegate OnStopped;
	/** Closes the studio on the next tick (safe from its own buttons). */
	void RequestStop() { bStopRequested = true; }
	/** Adds a character object for a card (OK card or KKS card) and selects it. */
	int32 AddCharacter(const FString& Card, int32 Outfit);

	/** Adds an object under the selected folder (or next to the selected object) and selects it. */
	int32 Add(EOKSceneObjectKind Kind);
	void Select(int32 Id);
	bool Save(const FString& Path, FString& OutError);
	bool Load(const FString& Path, FString& OutError);
	/** Drop the current scene and its source/image caches before another independent load. */
	void ReleaseSceneResources();
	/** Cards the character picker offers (Content/CharacterCards). */
	static TArray<FString> AvailableCards();
	/** Scene files of the in-game list: UserData/Studio/scene/*.okscene, newest first. */
	static FString SceneDirectory();
	static TArray<FString> SceneFiles();

	/** Guide object (gizmo) of the selection: 0 move, 1 rotate, 2 scale. */
	int32 GuideMode = 0;
	/** The transform bar's 主軸 switch: false hides the guide (and its handles cannot be dragged). */
	bool bShowGuide = true;
	/** HS2Studio6 視窗格線: a 10 m ground grid (50 cm cells) on the scene's floor. */
	bool bShowGrid = false;
	/** 隱藏全部輔助: no gizmo, pose points, look point, light reach, selection boxes or grid (and the gizmo cannot be grabbed). */
	bool bHideHelpers = false;

	/** The studio panel (null before the viewport has one). */
	SOKStudioPanel* GetPanel() const { return Panel.Get(); }
	/** The studio UI's command table (buttons, shortcuts and OKStudio.Cmd run the same commands). */
	bool RunUiCommand(const FString& Id, const FString& Arg);
	/** Open windows, menus, toggles, selection and the command ids, as JSON (OKStudio.UiState). */
	FString UiStateJson() const;
	/** The studio UI's widget rects in canvas units, as JSON comparable with the HS2Studio6 layout dumps (OKStudio.UiLayout). */
	FString UiLayoutJson() const;
	/** FK bone the guide turns (empty: the selected object). A bone guide is always the rotate rings on the bone's own
	 *  local axes; dragging writes the bone's FK rotation. */
	FString GuideBone;
	/** IK effector whose target the guide moves (-1: none); a move guide in world axes at the target. */
	int32 GuideIk = -1;
	/** World position of an IK target of the selected character (false: no such target). */
	bool IkTargetWorld(int32 Effector, FVector& Out) const;

	/** One guide handle in panel coordinates: axis 0..2 (X, Y, Z), 3 uniform scale, 4..6 the planes XY, YZ, XZ (move),
	 *  10..12 the character's own move axes shown small beside a bone or IK guide. */
	struct FGuideHandle
	{
		int32 Axis = 0;
		TArray<FVector2D> Points;
		bool bClosed = false;
		/** A filled plane square. */
		bool bFilled = false;
	};
	/** Handles of the selected object (false when nothing is selected or it is off screen). */
	bool GuideHandles(TArray<FGuideHandle>& Out, FVector2D& Center, double& CmPerPixel) const;
	/** Handle under a panel position (-1: none). */
	int32 HitGuide(const FVector2D& Position) const;
	/** Drags a handle by a mouse movement (panel units): moves along, rotates about or scales the selection. */
	bool ApplyGuideDrag(int32 Axis, const FVector2D& Delta);
	/** A handle drag from mouse down to up: the whole movement so far is applied to where the selection started, snapped
	 *  to the settings' steps while bSnap is on or Ctrl is held. */
	void BeginGuideDrag(int32 Axis);
	bool DragGuide(const FVector2D& Delta, bool bCtrl);
	void EndGuideDrag();
	bool IsGuideDragging() const { return DragAxis >= 0; }
	/** The guide's axes: the world's, or the object's own (local). */
	bool bLocalAxes = false;
	/** Snap moves, turns and scales to the settings' steps (Ctrl does it while held). */
	bool bSnap = false;
	/** What the drag changes now ("移動 X +12.0 cm"), for the status line. */
	FString GuideReadout;
	/** The user is dragging something in the UI (a slider, the view): outside edits wait. */
	bool bUserDragging = false;
	/** Free-fly keys held while the right button is down in the view (W A S D Q E). */
	TSet<FKey> FlyKeys;
	bool bFlying = false;
	/** Turns the free view about the camera itself (fly mode) instead of around its focus. */
	void Look(float DeltaYaw, float DeltaPitch);

	/** Unsaved changes since the scene was saved or opened. */
	bool bDirty = false;
	/** Why edits from outside (the control channel) wait now (empty: they need not). */
	FString BusyReason() const;
	/** Auto backup (Saved/OKStudio/Backup): writes one now unless the scene is unchanged since the last (bForce: always). */
	FString BackupNow(bool bForce);
	static FString BackupDirectory();
	/** Backups, newest first. */
	static TArray<FString> BackupFiles();
	/** Objects the last open could not show (id, name, why). */
	TArray<FString> LoadReport() const;

	/** Timeline time (seconds) and playback (keyed objects follow while playing or when the time is moved). */
	float TimelineTime = 0.f;
	bool bTimelinePlaying = false;
	/** Playback speed and the loop range (LoopEnd <= LoopStart: the whole timeline). */
	float TimelineSpeed = 1.f;
	float LoopStart = 0.f, LoopEnd = 0.f;
	void SetTimelineTime(float Time);

	/** Prevents mouse orbit/pan/zoom/fly in the viewport from clearing Document->ActiveCamera = -1 while following an MMD/scene camera. */
	bool bLockActiveCamera = false;
	/** Live non-destructive calibration offset applied to the active scene/MMD camera in UpdateViewer. */
	FVector MmdCameraOffset = FVector::ZeroVector;
	float MmdCameraYawOffset = 0.f;
	float MmdCameraFovOffset = 0.f;
	/** Finds the MMD system camera id in Document (-1 if none). When bOnlyMmdScene is true, only matches cameras named MMD/MMDD or tracked cameras in a VMD scene. */
	int32 FindMmdCameraId(bool bOnlyMmdScene = false) const;
	bool bMuteSound = false;
	float SoundVolume = 1.f;
	void StopSceneAudio();
	void UpdateSceneAudio();
	/** The sound menu's files: the scene's BGM (the library's Ogg) and its outside sound (relative to the scene file's
	 *  folder when not absolute); empty when there is none or the file is missing. */
	FString BgmFile() const;
	FString OutsideSoundFile() const;
	/** Whether the BGM / outside sound is audible now (still loading or ended: false). */
	bool IsBgmPlaying() const;
	bool IsOutsideSoundPlaying() const;
	/** The voice list entry character `Id` is speaking (-1: none, still loading or ended). */
	int32 VoicePlayingIndex(int32 Id) const;

	/** Screenshot through the current view into UserData/Studio/screenshot (returns the file; empty on failure). */
	FString Screenshot(int32 Width, int32 Height, bool bTransparent = false);
	/** Renders the current view (camera, FOV, scene post-processing) at a resolution and writes a PNG, synchronously. */
	/** bTransparent: only the scene's characters and items, the rest (map, sky) transparent. */
	bool CaptureToPng(const FString& Path, int32 Width, int32 Height, FString& OutError, bool bTransparent = false);

	/** Exports the timeline as numbered PNG frames (UserData/Studio/movie/<time>/frame_00000.png), one per Tick. */
	FString ExportFrames(float Fps, int32 Width, int32 Height, bool bMp4 = false, float From = 0.f, float To = -1.f, const FString& Folder = FString());
	/** Stops a frame export: the frames written so far stay, no movie is made. */
	void CancelExport();
	int32 ExportFrameIndex() const { return ExportFrame; }
	/** The folder of the last frame export. */
	FString ExportFolder() const { return ExportDir; }
	/** A character's pose over the timeline (animation, FK, IK, keys) as an OK animation in UserData/Studio/anim
	 *  (returns the file; empty on failure). */
	FString ExportOkAnim(int32 Id, float Fps, FString& OutError);
	/** Encodes a PNG frame folder (frame_00000.png…) into an H.264 mp4 with Tools/ffmpeg (the first encoder that works:
	 *  NVENC, AMF, Media Foundation, OpenH264). OutEncoder: the encoder used. */
	static bool EncodeMp4(const FString& FramesDir, float Fps, const FString& OutFile, FString& OutEncoder, FString& OutError);
	/** The ffmpeg the movie export uses (not under version control; downloaded into Tools/ffmpeg). */
	static FString FfmpegPath();
	/** Movie export: the last mp4 written, or why it was not. */
	FString LastMovie, LastMovieError;
	bool IsExporting() const { return ExportFrame >= 0; }
	int32 ExportFrameCount() const { return ExportTotal; }

	/** A story plays (OKStoryPlayer): the studio panel is hidden and the view is the story's (the orbit viewer and the
	 *  scene's active camera no longer move it). */
	void SetStoryMode(bool bOn);
	bool IsStoryMode() const { return bStoryMode; }
	/** Builds what changed in the document now (small scenes complete on return). */
	void SyncDocumentNow() { SyncNow(); }

	void Orbit(float DeltaYaw, float DeltaPitch);
	void Pan(float Right, float Up);
	void Zoom(float Steps);

	/** Undo history of the document: a change becomes one step once the document has been still for a moment (a drag or
	 *  a slider is one step); loading a scene starts a new history. Not recorded while the timeline plays. */
	void Undo();
	void Redo();
	bool CanUndo() const { return !UndoStack.IsEmpty() || bHistoryDirty; }
	bool CanRedo() const { return !RedoStack.IsEmpty(); }
	int32 UndoSteps() const { return UndoStack.Num() + (bHistoryDirty ? 1 : 0); }
	/** Records a pending change now (otherwise done on the next still tick). */
	void CommitHistory();
	void ResetHistory();
	/** Objects the last undo or redo left as they were (not rebuilt). */
	int32 LastHistoryKept = 0;

	/** Self test of the in-game studio; results in Saved/OKStudio/game_selftest.txt. */
	void RunSelfTest(bool bExit);
	/** Effect colour over real frames (OKStudio.EffectColorTest). */
	void RunEffectColorTest(bool bExit);
	/** Item pattern over real frames (OKStudio.PatternTest). */
	void RunPatternTest(bool bExit);
	/** OKStudio.EyeSetTest: a character's eyes patterns through their eye sets (gag eyes, pupil expression, blend shape). */
	void RunEyeSetTest(bool bExit);
	/** OKStudio.SkyTest: every Studio sky pattern on the view (up and towards the horizon), then the sky off. */
	void RunSkyTest(bool bExit);
	/** OKStudio.PostTest: Studio's fog, ambient occlusion and sun shafts (OKStudioPost) each change the picture. */
	void RunPostTest(bool bExit);
	/** Adds `Count` copies of one catalogue item (`Item`: group/category/no, default the houseplant 3/18/188 repeated in the big
	 * official scene) in batches, logging process memory and live object counts per batch (OKStudio.StressItems). */
	void RunStressItems(int32 Count, const FString& Item, bool bExit);
	/** KStudio scenes pictured from their Studio camera beside the scene card picture (OKStudio.CompareKStudio). */
	void RunKStudioCompare(const TArray<FString>& Scenes, int32 WaitFrames, bool bExit);
	/** Frame times of KStudio scenes once loaded, animations playing, no frame cap (OKStudio.PerfKStudio, OKStudioPerf.cpp). */
	void RunKStudioPerf(const TArray<FString>& Scenes, int32 SettleFrames, float Seconds, bool bExit);

	// FTickableGameObject
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UOKStudioGame, STATGROUP_Tickables); }
	virtual bool IsTickable() const override { return SceneActor != nullptr; }
	virtual UWorld* GetTickableGameObjectWorld() const override;

private:
	TSharedPtr<SOKStudioPanel> Panel;
	TSharedPtr<SWidget> PanelHost;
	bool bSyncPending = false;
	bool bStopRequested = false;
	bool bStoryMode = false;
	EVisibility PanelVisibility = EVisibility::Visible;
	/** Earlier document states (oldest first), undone states, and the state the history last recorded. */
	TArray<FString> UndoStack, RedoStack;
	FString HistoryState;
	/** The selection each history state was left with (restored with it; the view is not). */
	TArray<TArray<int32>> UndoSelections, RedoSelections;
	TArray<int32> HistorySelection;
	/** The timeline is posing the scene (not an edit: no history step, no unsaved mark). */
	bool bEvaluatingTimeline = false;
	/** The guide drag: its axis, the movement so far, the selection's world transforms when it started, a bone's turn. */
	int32 DragAxis = -1;
	FVector2D DragTotal = FVector2D::ZeroVector;
	TMap<int32, FTransform> DragStart;
	float DragBoneApplied = 0.f;
	double LastBackupSeconds = 0.;
	uint32 LastBackupHash = 0;
	bool bFirstSync = true;
	bool bHistoryDirty = false;
	bool bHistoryRestoring = false;
	double LastChangeSeconds = 0.;
	void RestoreHistory(const FString& State);
	FString ExportDir;
	float ExportFps = 30.f;
	float ExportFrom = 0.f;
	int32 ExportFrame = -1, ExportTotal = 0, ExportWidth = 1920, ExportHeight = 1080;
	bool bExportMp4 = false;
	bool bExportShotPending = false;
	void TickExport();
	TWeakObjectPtr<AActor> PreviousViewTarget;
	/** The world's own actors (the gallery's floor, sky, lights, character) hidden and its post-process volumes off while
	 *  the studio runs: a scene shows only itself, as in Studio and on the empty Entry map. Restored on Stop. */
	TArray<TWeakObjectPtr<AActor>> HiddenWorldActors;
	/** Hidden world actors whose tick was paused while the studio runs (restored on Stop). */
	TArray<TWeakObjectPtr<AActor>> PausedWorldActors;
	TArray<TWeakObjectPtr<USceneComponent>> HiddenWorldComponents;
	/** Every other mode's UI on the game viewport (the gallery's UMG, street / interaction / paint panels, HUD layers)
	 *  collapsed while the studio runs, with the visibility each had; the HUD's drawing too. Restored by Stop. */
	TArray<TPair<TWeakPtr<SWidget>, EVisibility>> HiddenViewportWidgets;
	TArray<TWeakObjectPtr<class AHUD>> HiddenHUDs;
	TArray<TWeakObjectPtr<class APostProcessVolume>> DisabledWorldVolumes;
	/** r.AllowOcclusionQueries before the studio turned it off (restored on Stop; INDEX_NONE: not changed). */
	int32 SavedOcclusionQueries = INDEX_NONE;
	TWeakObjectPtr<class UAudioComponent> SceneAudio;
	TSharedPtr<const struct FKKSpeechPcm, ESPMode::ThreadSafe> SceneAudioPcm;
	FString LoadedSoundFile;
	float AudioStartTimelineTime = -1000.f;
	double AudioStartWallSeconds = 0.0;
	float AudioStartSpeed = 1.f;
	/** A sound of the sound menu (FOKSceneSound): plays on its own, apart from the timeline; decoded on a worker. */
	struct FStudioSound
	{
		FString File;
		TSharedPtr<const struct FKKSpeechPcm, ESPMode::ThreadSafe> Pcm;
		/** The decode in flight (its result is taken once done). */
		TSharedPtr<struct FOKStudioSoundLoad, ESPMode::ThreadSafe> Load;
		TWeakObjectPtr<class UAudioComponent> Audio;
		bool bStarted = false;
		bool bEnded = false;
	};
	FStudioSound StudioBgm, StudioOutside;
	void UpdateStudioSound(FStudioSound& Sound, const FString& File, bool bPlay, bool bRepeat);
	void StopStudioSounds();
	/** A character's voice list playing (FOKSceneCharacter::Voices): the entry, its decoded sound and whether the
	 *  character has started saying it. */
	struct FCharacterVoice
	{
		int32 Index = 0;
		int32 Start = 0;
		FString File;
		TSharedPtr<const struct FKKSpeechPcm, ESPMode::ThreadSafe> Pcm;
		TSharedPtr<struct FOKStudioSoundLoad, ESPMode::ThreadSafe> Load;
		TWeakObjectPtr<class AKKPreviewActor> Speaker;
		bool bStarted = false;
		bool bEnded = false;
	};
	TMap<int32, FCharacterVoice> CharacterVoices;
	void UpdateCharacterVoices();
	void UpdateViewer();
	void SyncNow();
	/** Whether a shown preview still draws stand-in materials or runtime shaders are compiling (pictures would miss it). */
	bool MaterialsPending() const;
};
