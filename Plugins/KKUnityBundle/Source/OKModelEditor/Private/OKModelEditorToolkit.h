#pragma once

#include "CoreMinimal.h"
#include "Toolkits/AssetEditorToolkit.h"
#include "Tickable.h"
#include "UObject/GCObject.h"
#include "Widgets/Views/SListView.h"

class UOKModelAsset;
class AKKPreviewActor;
class FAdvancedPreviewScene;
class IDetailsView;
class SOKModelViewport;
class STextBlock;
class SImage;
struct FSlateBrush;
class UTexture2D;

/** One row of the contents list: a prefab, one of its meshes, a material or a texture. */
struct FOKModelRow
{
	enum class EKind : uint8 { Prefab, Mesh, Material, Texture };
	EKind Kind = EKind::Prefab;
	FString Name;
	FString Prefab;
	FString Detail;
	TSharedPtr<class FJsonObject> Json;
};

/**
 * Editor of OK base library models: contents list (prefabs, meshes, materials, textures), a preview of the
 * selected prefab rendered like a character part, and the edits overlay (material values and texture slots,
 * texture replacement, mesh visibility and transform) written next to the model and applied wherever it is used.
 */
class FOKModelEditor : public FAssetEditorToolkit, public FTickableEditorObject, public FGCObject
{
public:
	void InitEditor(EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost>& Host, UOKModelAsset* Asset);
	virtual ~FOKModelEditor() override;

	// FAssetEditorToolkit
	virtual FName GetToolkitFName() const override { return TEXT("OKModelEditor"); }
	virtual FText GetBaseToolkitName() const override;
	virtual FString GetWorldCentricTabPrefix() const override { return TEXT("OK Model "); }
	virtual FLinearColor GetWorldCentricTabColorScale() const override { return FLinearColor(0.35f, 0.85f, 0.55f, 0.5f); }
	virtual void RegisterTabSpawners(const TSharedRef<FTabManager>& TabManager) override;
	virtual void UnregisterTabSpawners(const TSharedRef<FTabManager>& TabManager) override;
	virtual void SaveAsset_Execute() override;

	// FTickableEditorObject
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(FOKModelEditor, STATGROUP_Tickables); }
	virtual ETickableTickType GetTickableTickType() const override { return ETickableTickType::Always; }

	// FGCObject
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override { return TEXT("FOKModelEditor"); }

	/** Loads the prefab into the preview (empty: keep the current one). */
	void ShowPrefab(const FString& Prefab);
	/** Writes the edits overlay and reloads the preview. */
	void ApplyEdits();
	void RevertEdits();
	/** Saves the model with its edits as a new library model and opens it (the original is unchanged). Empty on failure. */
	FString SaveAsNewModel();
	void RefreshContents();

	/** Last editor opened (self test). */
	static FOKModelEditor* LastOpened;
	/** Preview ready: the prefab session is loaded into the scene. */
	bool HasPreview() const;
	/** World bounds of the previewed model's vertices. */
	FBox PreviewBounds() const;
	/**
	 * Self test: preview has parts, a material colour edit reaches the preview session, revert restores it,
	 * a viewport capture is written; results go to Saved/OKModel/editor_selftest.txt.
	 */
	void RunSelfTest(bool bExit);

private:
	void FillToolbar(class FToolBarBuilder& ToolbarBuilder);
	void OnRowSelected(TSharedPtr<FOKModelRow> Row, ESelectInfo::Type Info);
	TSharedRef<class ITableRow> MakeRow(TSharedPtr<FOKModelRow> Row, const TSharedRef<class STableViewBase>& Owner);
	void ShowTexture(const FString& Name);

	TObjectPtr<UOKModelAsset> Asset = nullptr;
	TSharedPtr<FAdvancedPreviewScene> Scene;
	TSharedPtr<SOKModelViewport> Viewport;
	TSharedPtr<IDetailsView> Details;
	TSharedPtr<SListView<TSharedPtr<FOKModelRow>>> List;
	TArray<TSharedPtr<FOKModelRow>> Rows;
	TSharedPtr<STextBlock> Status;
	TSharedPtr<SImage> TextureImage;
	TSharedPtr<FSlateBrush> TextureBrush;
	TObjectPtr<UTexture2D> TextureShown = nullptr;
	TWeakObjectPtr<AKKPreviewActor> Preview;
	FString CurrentPrefab;
	bool bShowDisabled = false;
	bool bDirty = false;
	bool bFocusPending = false;
	double DirtyAt = 0.0;
	FDelegateHandle ObjectChanged;

	static const FName ViewportTab;
	static const FName ContentsTab;
	static const FName DetailsTab;
	static const FName TextureTab;
};
