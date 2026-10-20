#pragma once

#include "CoreMinimal.h"
#include "OKSceneDocument.h"
#include "Tickable.h"
#include "Toolkits/AssetEditorToolkit.h"
#include "UObject/GCObject.h"
#include "Widgets/Views/STreeView.h"
#include "OKStudioEditor.generated.h"

class AOKSceneActor;
class FAdvancedPreviewScene;
class IDetailsView;
class SOKStudioViewport;
class STextBlock;

/** The selected scene object, shown in the details panel; edits are copied back into the document. */
UCLASS(Transient)
class UOKStudioSelection : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = "Selection", meta = (ShowOnlyInnerProperties))
	FOKSceneObject Object;
};

/** One row of the outliner: a scene object id. */
struct FOKStudioRow
{
	int32 Id = -1;
	TArray<TSharedPtr<FOKStudioRow>> Children;
};

/**
 * OK Studio in the Unreal editor: the scene object tree (outliner with drag and drop parenting), the selected
 * object's properties, and a preview of the scene built from OK cards and OK base library models.
 */
class FOKStudioEditor : public FAssetEditorToolkit, public FTickableEditorObject, public FGCObject
{
public:
	/** Opens a scene (empty path: a new scene). */
	static TSharedPtr<FOKStudioEditor> Open(const FString& File);
	virtual ~FOKStudioEditor() override;

	virtual FName GetToolkitFName() const override { return TEXT("OKStudioEditor"); }
	virtual FText GetBaseToolkitName() const override;
	virtual FString GetWorldCentricTabPrefix() const override { return TEXT("OK Studio "); }
	virtual FLinearColor GetWorldCentricTabColorScale() const override { return FLinearColor(0.4f, 0.6f, 0.95f, 0.5f); }
	virtual void RegisterTabSpawners(const TSharedRef<FTabManager>& TabManager) override;
	virtual void UnregisterTabSpawners(const TSharedRef<FTabManager>& TabManager) override;

	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(FOKStudioEditor, STATGROUP_Tickables); }
	virtual ETickableTickType GetTickableTickType() const override { return ETickableTickType::Always; }

	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override { return TEXT("FOKStudioEditor"); }

	UOKSceneDocument* Document() const { return Doc; }
	AOKSceneActor* SceneActor() const { return Scene.IsValid() ? SceneActorPtr.Get() : nullptr; }

	/** Viewport transform handles (the editor's move / rotate / scale widget) on the selected object. */
	bool GizmoTransform(FTransform& OutWorld) const;
	bool GizmoDelta(const FVector& Drag, const FRotator& Rot, const FVector& Scale);
	void GizmoBegin();
	void GizmoEnd();
	/** Adds an object under the selection (or the root) and selects it. */
	int32 Add(EOKSceneObjectKind Kind);
	void Select(int32 Id);
	int32 Selected() const { return SelectedId; }
	bool SaveTo(const FString& File, FString& OutError);
	bool Load(const FString& File, FString& OutError);
	/** Rebuilds the outliner and the preview from the document. */
	void Refresh();

	static FOKStudioEditor* LastOpened;
	/** Self test: a scene with two OK cards, a library model, a light, a camera and a folder child; save, load, edit. */
	void RunSelfTest(bool bExit);

private:
	void Init();
	void FillToolbar(class FToolBarBuilder& ToolbarBuilder);
	TSharedRef<ITableRow> MakeRow(TSharedPtr<FOKStudioRow> Row, const TSharedRef<STableViewBase>& Owner);
	void SetStatus(const FString& Text);
	FString PickFile(bool bSave, const FString& Title, const FString& Types, const FString& Default) const;

	static const FName OutlinerTab, DetailsTab, ViewportTab, PoseTab;

	/** Pose tab (OKStudioEditorPose.cpp): the selected character's FK bones and IK targets. */
	TSharedRef<SWidget> MakePoseTab();
	void RebuildPose();
	/** Changes the selected character in the document (and the details view's copy). */
	void EditCharacter(TFunctionRef<void(struct FOKSceneCharacter&)> Edit);
	TSharedPtr<class SVerticalBox> PoseBox;

	TObjectPtr<UOKSceneDocument> Doc;
	TObjectPtr<UOKStudioSelection> Selection;
	TSharedPtr<FAdvancedPreviewScene> Scene;
	TWeakObjectPtr<AOKSceneActor> SceneActorPtr;
	TSharedPtr<SOKStudioViewport> Viewport;
	TSharedPtr<IDetailsView> Details;
	TSharedPtr<STreeView<TSharedPtr<FOKStudioRow>>> Tree;
	TArray<TSharedPtr<FOKStudioRow>> Roots;
	TSharedPtr<STextBlock> Status;
	FString File;
	int32 SelectedId = -1;
	bool bGizmoTransaction = false;
	bool bSyncPending = false;
	FDelegateHandle SelectionChanged;
};
