#pragma once

#include "CoreMinimal.h"
#include "OKSceneDocument.h"
#include "Layout/Visibility.h"
#include "Tickable.h"
#include "UObject/Object.h"
#include "OKStudioGame.generated.h"

class AOKSceneActor;
class ACameraActor;
class SOKStudioPanel;
class SWidget;

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
	int32 SelectedId = -1;

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
	/** FK bone the guide turns (empty: the selected object). A bone guide is always the rotate rings on the bone's own
	 *  local axes; dragging writes the bone's FK rotation. */
	FString GuideBone;
	/** IK effector whose target the guide moves (-1: none); a move guide in world axes at the target. */
	int32 GuideIk = -1;
	/** World position of an IK target of the selected character (false: no such target). */
	bool IkTargetWorld(int32 Effector, FVector& Out) const;

	/** One guide handle in panel coordinates: axis 0..2 (X, Y, Z), 3 uniform scale. */
	struct FGuideHandle
	{
		int32 Axis = 0;
		TArray<FVector2D> Points;
		bool bClosed = false;
	};
	/** Handles of the selected object (false when nothing is selected or it is off screen). */
	bool GuideHandles(TArray<FGuideHandle>& Out, FVector2D& Center, double& CmPerPixel) const;
	/** Handle under a panel position (-1: none). */
	int32 HitGuide(const FVector2D& Position) const;
	/** Drags a handle by a mouse movement (panel units): moves along, rotates about or scales the selected object. */
	bool ApplyGuideDrag(int32 Axis, const FVector2D& Delta);

	/** Timeline time (seconds) and playback (keyed objects follow while playing or when the time is moved). */
	float TimelineTime = 0.f;
	bool bTimelinePlaying = false;
	void SetTimelineTime(float Time);

	/** Screenshot through the current view into UserData/Studio/screenshot (returns the file; empty on failure). */
	FString Screenshot(int32 Width, int32 Height, bool bTransparent = false);
	/** Renders the current view (camera, FOV, scene post-processing) at a resolution and writes a PNG, synchronously. */
	/** bTransparent: only the scene's characters and items, the rest (map, sky) transparent. */
	bool CaptureToPng(const FString& Path, int32 Width, int32 Height, FString& OutError, bool bTransparent = false);

	/** Exports the timeline as numbered PNG frames (UserData/Studio/movie/<time>/frame_00000.png), one per Tick. */
	FString ExportFrames(float Fps, int32 Width, int32 Height, bool bMp4 = false);
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
	bool bHistoryDirty = false;
	bool bHistoryRestoring = false;
	double LastChangeSeconds = 0.;
	void RestoreHistory(const FString& State);
	FString ExportDir;
	float ExportFps = 30.f;
	int32 ExportFrame = -1, ExportTotal = 0, ExportWidth = 1920, ExportHeight = 1080;
	bool bExportMp4 = false;
	bool bExportShotPending = false;
	void TickExport();
	TWeakObjectPtr<AActor> PreviousViewTarget;
	/** The world's own actors (the gallery's floor, sky, lights, character) hidden and its post-process volumes off while
	 *  the studio runs: a scene shows only itself, as in Studio and on the empty Entry map. Restored on Stop. */
	TArray<TWeakObjectPtr<AActor>> HiddenWorldActors;
	TArray<TWeakObjectPtr<USceneComponent>> HiddenWorldComponents;
	TArray<TWeakObjectPtr<class APostProcessVolume>> DisabledWorldVolumes;
	void UpdateViewer();
	void SyncNow();
	/** Whether a shown preview still draws stand-in materials or runtime shaders are compiling (pictures would miss it). */
	bool MaterialsPending() const;
};
