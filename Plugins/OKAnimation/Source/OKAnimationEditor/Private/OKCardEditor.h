#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "Tickable.h"
#include "Toolkits/AssetEditorToolkit.h"
#include "UObject/GCObject.h"
#include "OKCardEditor.generated.h"

class AKKPreviewActor;
class FAdvancedPreviewScene;
class IDetailsView;
class SOKCardViewport;
class STextBlock;
class UOKAnimationAsset;

/** One body or face shape parameter of the card being edited. */
USTRUCT()
struct FOKCardShapeValue
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category = "Shape")
	FString Label;

	UPROPERTY(EditAnywhere, Category = "Shape", meta = (UIMin = "-1", UIMax = "2", ClampMin = "-1", ClampMax = "2"))
	float Value = 0.5f;

	UPROPERTY()
	int32 Index = 0;
};

/** What the OK card editor edits: the card's body and face parameters, its outfit, clothes to wear and an animation to preview. */
UCLASS(Transient)
class UOKCardEditorState : public UObject
{
	GENERATED_BODY()

public:
	/** The card shown (an OK card, or an original KKS card; saving always writes an OK card). */
	UPROPERTY(VisibleAnywhere, Category = "Card")
	FString Card;

	UPROPERTY(VisibleAnywhere, Category = "Card")
	FString Name;

	UPROPERTY(EditAnywhere, Category = "Card", meta = (ClampMin = "0", ClampMax = "6"))
	int32 Outfit = 0;

	UPROPERTY(EditAnywhere, EditFixedSize, Category = "Body", meta = (TitleProperty = "Label"))
	TArray<FOKCardShapeValue> Body;

	UPROPERTY(EditAnywhere, EditFixedSize, Category = "Face", meta = (TitleProperty = "Label"))
	TArray<FOKCardShapeValue> Face;

	/** .okclothes to wear as the current outfit (toolbar: Wear Clothes). */
	UPROPERTY(EditAnywhere, Category = "Clothes", meta = (FilePathFilter = "okclothes"))
	FFilePath WearClothes;

	/** OK animation played on the card (toolbar: Play Animation). */
	UPROPERTY(EditAnywhere, Category = "Animation")
	TObjectPtr<UOKAnimationAsset> Animation;
};

/**
 * Visual editor of an OK card inside Unreal: the character opened from the OK base library and the card (no original
 * game needed for an OK card), its body and face parameters applied live, outfits, .okclothes export and wear, and OK
 * animation playback. Saving writes the card data and makes it an OK card again (OK base library + the card's pack).
 */
class FOKCardEditor : public FAssetEditorToolkit, public FTickableEditorObject, public FGCObject
{
public:
	/** Opens a card in a new OK card editor window. */
	static TSharedPtr<FOKCardEditor> Open(const FString& Card);
	virtual ~FOKCardEditor() override;

	// FAssetEditorToolkit
	virtual FName GetToolkitFName() const override { return TEXT("OKCardEditor"); }
	virtual FText GetBaseToolkitName() const override;
	virtual FString GetWorldCentricTabPrefix() const override { return TEXT("OK Card "); }
	virtual FLinearColor GetWorldCentricTabColorScale() const override { return FLinearColor(0.9f, 0.55f, 0.75f, 0.5f); }
	virtual void RegisterTabSpawners(const TSharedRef<FTabManager>& TabManager) override;
	virtual void UnregisterTabSpawners(const TSharedRef<FTabManager>& TabManager) override;

	// FTickableEditorObject
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(FOKCardEditor, STATGROUP_Tickables); }
	virtual ETickableTickType GetTickableTickType() const override { return ETickableTickType::Always; }

	// FGCObject
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override { return TEXT("FOKCardEditor"); }

	/** Loads a card (keeping the edited parameters when bKeepShapes). */
	bool Load(const FString& Card, bool bKeepShapes, FString& OutError);
	/** Writes the edited card to Out and makes it an OK card. */
	bool SaveTo(const FString& Out, FString& OutError);
	bool WearClothes(const FString& OkClothes, FString& OutError);
	/** Exports the current outfit as .okclothes; the file path, or empty. */
	FString ExportClothes(FString& OutError);
	bool PlayAnimation(FString& OutError);
	void StopAnimation();

	bool HasPreview() const;
	int32 PreviewParts() const;
	/** Body/face values the preview session currently uses. */
	TArray<float> PreviewShape(bool bBody) const;

	/** Last editor opened (self test). */
	static FOKCardEditor* LastOpened;
	/** Self test: preview, live parameter edit, outfit export and wear, animation, Save As OK card and reopen. */
	void RunSelfTest(bool bExit);

private:
	void Init();
	void FillToolbar(class FToolBarBuilder& ToolbarBuilder);
	bool OpenPreview(FString& OutError);
	void ApplyShapes();
	void SetStatus(const FString& Text);
	FString WorkDirectory() const;

	static const FName ViewportTab;
	static const FName DetailsTab;

	TObjectPtr<UOKCardEditorState> State;
	TSharedPtr<FAdvancedPreviewScene> Scene;
	TSharedPtr<SOKCardViewport> Viewport;
	TSharedPtr<IDetailsView> Details;
	TSharedPtr<STextBlock> Status;
	TWeakObjectPtr<AKKPreviewActor> Preview;
	/** The card file the editor works from (the card, or a worn copy until saved). */
	FString Source;
	int32 LoadedOutfit = 0;
	bool bShapesDirty = false;
	bool bPlaying = false;
	float AnimTime = 0.f;
	FDelegateHandle ObjectChanged;
};
