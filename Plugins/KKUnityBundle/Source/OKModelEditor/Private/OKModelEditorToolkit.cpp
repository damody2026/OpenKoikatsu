#include "OKModelEditorToolkit.h"
#include "OKModelAsset.h"
#include "KKPreviewActor.h"
#include "KKRustExtract.h"
#include "ok_extract.h"
#include "AdvancedPreviewScene.h"
#include "AssetEditorModeManager.h"
#include "Dom/JsonObject.h"
#include "EditorViewportClient.h"
#include "Camera/CameraComponent.h"
#include "Engine/Texture2D.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "IDetailsView.h"
#include "ImageUtils.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "SEditorViewport.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Styling/SlateBrush.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/STableRow.h"

#define LOCTEXT_NAMESPACE "OKModelEditor"

FOKModelEditor* FOKModelEditor::LastOpened = nullptr;
const FName FOKModelEditor::ViewportTab(TEXT("OKModelEditor_Viewport"));
const FName FOKModelEditor::ContentsTab(TEXT("OKModelEditor_Contents"));
const FName FOKModelEditor::DetailsTab(TEXT("OKModelEditor_Details"));
const FName FOKModelEditor::TextureTab(TEXT("OKModelEditor_Texture"));

// ------------------------------------------------------------------------------------------------ viewport

class SOKModelViewport : public SEditorViewport
{
public:
	SLATE_BEGIN_ARGS(SOKModelViewport) {}
	SLATE_END_ARGS()

	void Construct(const FArguments&, TSharedPtr<FAdvancedPreviewScene> InScene)
	{
		Scene = InScene;
		SEditorViewport::Construct(SEditorViewport::FArguments());
	}
	/** False until the viewport is laid out (focusing needs its size). */
	bool Focus(const FBox& Box)
	{
		if (!Client || !Box.IsValid || !Client->Viewport || Client->Viewport->GetSizeXY().X <= 0) return false;
		Client->FocusViewportOnBox(Box, true);
		return true;
	}
	bool Capture(TArray<FColor>& Pixels, FIntPoint& Size)
	{
		FViewport* View = Client ? Client->Viewport : nullptr;
		if (!View || View->GetSizeXY().X <= 0) return false;
		Size = View->GetSizeXY();
		return View->ReadPixels(Pixels) && Pixels.Num() == Size.X * Size.Y;
	}

protected:
	virtual TSharedRef<FEditorViewportClient> MakeEditorViewportClient() override
	{
		Client = MakeShareable(new FEditorViewportClient(nullptr, Scene.Get(), SharedThis(this)));
		Client->SetRealtime(true);
		Client->SetViewLocation(FVector(120.f, 120.f, 60.f));
		Client->SetViewRotation(FRotator(-10.f, -135.f, 0.f));
		Client->EngineShowFlags.SetGrid(true);
		if (FAssetEditorModeManager* Modes = static_cast<FAssetEditorModeManager*>(Client->GetModeTools())) Modes->SetPreviewScene(Scene.Get());
		return Client.ToSharedRef();
	}

private:
	TSharedPtr<FAdvancedPreviewScene> Scene;
	TSharedPtr<FEditorViewportClient> Client;
};

// ------------------------------------------------------------------------------------------------ editor

FText FOKModelEditor::GetBaseToolkitName() const { return LOCTEXT("ToolkitName", "OK Model Editor"); }

void FOKModelEditor::InitEditor(EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost>& Host, UOKModelAsset* InAsset)
{
	Asset = InAsset;
	Asset->SetFlags(RF_Transactional);
	Scene = MakeShared<FAdvancedPreviewScene>(FPreviewScene::ConstructionValues());

	FPropertyEditorModule& PropertyEditor = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	FDetailsViewArgs Args;
	Args.bHideSelectionTip = true;
	Args.NameAreaSettings = FDetailsViewArgs::HideNameArea;
	Details = PropertyEditor.CreateDetailView(Args);
	Details->SetObject(Asset);
	TextureBrush = MakeShared<FSlateBrush>();

	// Edits apply shortly after the last change (the overlay is rewritten and the preview reloaded).
	ObjectChanged = FCoreUObjectDelegates::OnObjectPropertyChanged.AddLambda([this](UObject* Object, FPropertyChangedEvent&)
	{
		if (Object == Asset) { bDirty = true; DirtyAt = FPlatformTime::Seconds(); }
	});

	const TSharedRef<FTabManager::FLayout> Layout = FTabManager::NewLayout("OKModelEditor_Layout_v1")
		->AddArea(FTabManager::NewPrimaryArea()->SetOrientation(Orient_Horizontal)
			->Split(FTabManager::NewStack()->SetSizeCoefficient(0.22f)->AddTab(ContentsTab, ETabState::OpenedTab))
			->Split(FTabManager::NewStack()->SetSizeCoefficient(0.50f)->SetHideTabWell(true)->AddTab(ViewportTab, ETabState::OpenedTab))
			->Split(FTabManager::NewSplitter()->SetOrientation(Orient_Vertical)->SetSizeCoefficient(0.28f)
				->Split(FTabManager::NewStack()->SetSizeCoefficient(0.7f)->AddTab(DetailsTab, ETabState::OpenedTab))
				->Split(FTabManager::NewStack()->SetSizeCoefficient(0.3f)->AddTab(TextureTab, ETabState::OpenedTab))));
	InitAssetEditor(Mode, Host, TEXT("OKModelEditorApp"), Layout, true, true, Asset);

	TSharedPtr<FExtender> Extender = MakeShared<FExtender>();
	Extender->AddToolBarExtension("Asset", EExtensionHook::After, GetToolkitCommands(), FToolBarExtensionDelegate::CreateRaw(this, &FOKModelEditor::FillToolbar));
	AddToolbarExtender(Extender);
	RegenerateMenusAndToolbars();

	RefreshContents();
	if (Asset->Prefabs.Num() > 0) ShowPrefab(Asset->Prefabs[0]);
	LastOpened = this;
}

FOKModelEditor::~FOKModelEditor()
{
	if (LastOpened == this) LastOpened = nullptr;
	FCoreUObjectDelegates::OnObjectPropertyChanged.Remove(ObjectChanged);
	if (Preview.IsValid()) Preview->Destroy();
}

void FOKModelEditor::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(Asset);
	Collector.AddReferencedObject(TextureShown);
}

void FOKModelEditor::RegisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
	WorkspaceMenuCategory = InTabManager->AddLocalWorkspaceMenuCategory(LOCTEXT("Workspace", "OK Model Editor"));
	FAssetEditorToolkit::RegisterTabSpawners(InTabManager);

	InTabManager->RegisterTabSpawner(ViewportTab, FOnSpawnTab::CreateLambda([this](const FSpawnTabArgs&)
	{
		Viewport = SNew(SOKModelViewport, Scene);
		return SNew(SDockTab).Label(LOCTEXT("Viewport", "Preview"))[Viewport.ToSharedRef()];
	})).SetDisplayName(LOCTEXT("Viewport", "Preview")).SetGroup(WorkspaceMenuCategory.ToSharedRef());

	InTabManager->RegisterTabSpawner(ContentsTab, FOnSpawnTab::CreateLambda([this](const FSpawnTabArgs&)
	{
		return SNew(SDockTab).Label(LOCTEXT("Contents", "Contents"))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(4.f)[SAssignNew(Status, STextBlock).AutoWrapText(true)]
			+ SVerticalBox::Slot().FillHeight(1.f)
			[
				SAssignNew(List, SListView<TSharedPtr<FOKModelRow>>)
				.ListItemsSource(&Rows)
				.OnGenerateRow(this, &FOKModelEditor::MakeRow)
				.OnSelectionChanged(this, &FOKModelEditor::OnRowSelected)
				.SelectionMode(ESelectionMode::Single)
			]
		];
	})).SetDisplayName(LOCTEXT("Contents", "Contents")).SetGroup(WorkspaceMenuCategory.ToSharedRef());

	InTabManager->RegisterTabSpawner(DetailsTab, FOnSpawnTab::CreateLambda([this](const FSpawnTabArgs&)
	{
		return SNew(SDockTab).Label(LOCTEXT("Details", "Details"))[Details.ToSharedRef()];
	})).SetDisplayName(LOCTEXT("Details", "Details")).SetGroup(WorkspaceMenuCategory.ToSharedRef());

	InTabManager->RegisterTabSpawner(TextureTab, FOnSpawnTab::CreateLambda([this](const FSpawnTabArgs&)
	{
		return SNew(SDockTab).Label(LOCTEXT("Texture", "Texture"))
		[
			SNew(SBox).Padding(4.f)[SAssignNew(TextureImage, SImage).Image(TextureBrush.Get())]
		];
	})).SetDisplayName(LOCTEXT("Texture", "Texture")).SetGroup(WorkspaceMenuCategory.ToSharedRef());
}

void FOKModelEditor::UnregisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
	FAssetEditorToolkit::UnregisterTabSpawners(InTabManager);
	InTabManager->UnregisterTabSpawner(ViewportTab);
	InTabManager->UnregisterTabSpawner(ContentsTab);
	InTabManager->UnregisterTabSpawner(DetailsTab);
	InTabManager->UnregisterTabSpawner(TextureTab);
}

void FOKModelEditor::FillToolbar(FToolBarBuilder& ToolbarBuilder)
{
	ToolbarBuilder.BeginSection("OKModel");
	ToolbarBuilder.AddToolBarButton(FUIAction(FExecuteAction::CreateRaw(this, &FOKModelEditor::ApplyEdits)), NAME_None,
		LOCTEXT("Apply", "Apply Edits"), LOCTEXT("ApplyTip", "Write the edits next to the model (they apply wherever it is used) and reload the preview"));
	ToolbarBuilder.AddToolBarButton(FUIAction(FExecuteAction::CreateLambda([this] { SaveAsNewModel(); })), NAME_None,
		LOCTEXT("SaveAsNew", "Save As New Model"), LOCTEXT("SaveAsNewTip", "Save this model with its edits as a new model in the OK base library and open it; the original model and the cards using it stay unchanged"));
	ToolbarBuilder.AddToolBarButton(FUIAction(FExecuteAction::CreateRaw(this, &FOKModelEditor::RevertEdits)), NAME_None,
		LOCTEXT("Revert", "Revert to Converted"), LOCTEXT("RevertTip", "Remove every edit of this model"));
	ToolbarBuilder.AddToolBarButton(FUIAction(FExecuteAction::CreateLambda([this] { bShowDisabled = !bShowDisabled; ShowPrefab(CurrentPrefab); }),
		FCanExecuteAction(), FIsActionChecked::CreateLambda([this] { return bShowDisabled; })), NAME_None,
		LOCTEXT("Disabled", "Disabled Renderers"), LOCTEXT("DisabledTip", "Also show renderers the prefab ships disabled"), FSlateIcon(), EUserInterfaceActionType::ToggleButton);
	ToolbarBuilder.AddToolBarButton(FUIAction(FExecuteAction::CreateRaw(this, &FOKModelEditor::RefreshContents)), NAME_None,
		LOCTEXT("Refresh", "Refresh"), LOCTEXT("RefreshTip", "Re-read the model and its edits from the library"));
	ToolbarBuilder.EndSection();
}

void FOKModelEditor::SaveAsset_Execute()
{
	ApplyEdits();
	FAssetEditorToolkit::SaveAsset_Execute();
}

void FOKModelEditor::Tick(float)
{
	if (bDirty && FPlatformTime::Seconds() - DirtyAt > 0.6) ApplyEdits();
	if (bFocusPending && Preview.IsValid() && Viewport && Viewport->Focus(PreviewBounds())) bFocusPending = false;
}

void FOKModelEditor::RefreshContents()
{
	FString Error;
	if (!Asset->Refresh(Error))
	{
		if (Status) Status->SetText(FText::FromString(Error));
		return;
	}
	Rows.Reset();
	TSharedPtr<FJsonObject> Info;
	FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Asset->InfoJson), Info);
	if (Info)
	{
		const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
		if (Info->TryGetArrayField(TEXT("prefabs"), Items)) for (const TSharedPtr<FJsonValue>& P : *Items)
		{
			const TSharedPtr<FJsonObject> PO = P->AsObject();
			TSharedPtr<FOKModelRow> Row = MakeShared<FOKModelRow>();
			Row->Kind = FOKModelRow::EKind::Prefab;
			Row->Name = PO->GetStringField(TEXT("name"));
			Row->Json = PO;
			Rows.Add(Row);
			const TArray<TSharedPtr<FJsonValue>>* Meshes = nullptr;
			if (PO->TryGetArrayField(TEXT("meshes"), Meshes)) for (const TSharedPtr<FJsonValue>& M : *Meshes)
			{
				const TSharedPtr<FJsonObject> MO = M->AsObject();
				TSharedPtr<FOKModelRow> MeshRow = MakeShared<FOKModelRow>();
				MeshRow->Kind = FOKModelRow::EKind::Mesh;
				MeshRow->Name = MO->GetStringField(TEXT("name"));
				MeshRow->Prefab = Row->Name;
				MeshRow->Detail = FString::Printf(TEXT("%d tris  %s%s"), int32(MO->GetNumberField(TEXT("triangles"))),
					*MO->GetStringField(TEXT("material")), MO->GetBoolField(TEXT("enabled")) ? TEXT("") : TEXT("  (disabled)"));
				MeshRow->Json = MO;
				Rows.Add(MeshRow);
			}
		}
		if (Info->TryGetArrayField(TEXT("materials"), Items)) for (const TSharedPtr<FJsonValue>& M : *Items)
		{
			TSharedPtr<FOKModelRow> Row = MakeShared<FOKModelRow>();
			Row->Kind = FOKModelRow::EKind::Material;
			Row->Json = M->AsObject();
			Row->Name = Row->Json->GetStringField(TEXT("name"));
			Row->Json->TryGetStringField(TEXT("shader"), Row->Detail);
			Rows.Add(Row);
		}
		if (Info->TryGetArrayField(TEXT("textures"), Items)) for (const TSharedPtr<FJsonValue>& T : *Items)
		{
			TSharedPtr<FOKModelRow> Row = MakeShared<FOKModelRow>();
			Row->Kind = FOKModelRow::EKind::Texture;
			Row->Json = T->AsObject();
			Row->Name = Row->Json->GetStringField(TEXT("name"));
			const int32 Pixels = int32(Row->Json->GetNumberField(TEXT("pixels")));
			const int32 Side = FMath::RoundToInt(FMath::Sqrt(float(Pixels)));
			Row->Detail = FString::Printf(TEXT("~%dx%d"), Side, Side);
			Rows.Add(Row);
		}
	}
	if (List) List->RequestListRefresh();
	if (Status) Status->SetText(FText::FromString(FString::Printf(TEXT("%s\n%s"), *Asset->ModelId, Asset->Sources.Num() ? *Asset->Sources[0] : TEXT(""))));
	if (Details) Details->ForceRefresh();
}

TSharedRef<ITableRow> FOKModelEditor::MakeRow(TSharedPtr<FOKModelRow> Row, const TSharedRef<STableViewBase>& Owner)
{
	static const TCHAR* Kinds[] = { TEXT("Prefab"), TEXT("  Mesh"), TEXT("Material"), TEXT("Texture") };
	const FString Text = FString::Printf(TEXT("%s  %s"), Kinds[int32(Row->Kind)], *Row->Name);
	return SNew(STableRow<TSharedPtr<FOKModelRow>>, Owner)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.f)[SNew(STextBlock).Text(FText::FromString(Text))]
		+ SHorizontalBox::Slot().AutoWidth().Padding(8.f, 0.f)[SNew(STextBlock).Text(FText::FromString(Row->Detail)).ColorAndOpacity(FSlateColor::UseSubduedForeground())]
	];
}

void FOKModelEditor::OnRowSelected(TSharedPtr<FOKModelRow> Row, ESelectInfo::Type)
{
	if (!Row) return;
	switch (Row->Kind)
	{
	case FOKModelRow::EKind::Prefab:
		ShowPrefab(Row->Name);
		break;
	case FOKModelRow::EKind::Mesh:
	{
		if (CurrentPrefab != Row->Prefab) ShowPrefab(Row->Prefab);
		// Selecting a mesh offers its edit (visibility, transform).
		if (!Asset->MeshEdits.ContainsByPredicate([&](const FOKModelMeshEdit& E) { return E.Prefab.Equals(Row->Prefab, ESearchCase::IgnoreCase) && E.Mesh.Equals(Row->Name, ESearchCase::IgnoreCase); }))
		{
			Asset->Modify();
			FOKModelMeshEdit& E = Asset->MeshEdits.AddDefaulted_GetRef();
			E.Prefab = Row->Prefab;
			E.Mesh = Row->Name;
			Details->ForceRefresh();
		}
		FString Texture;
		if (Row->Json && Row->Json->TryGetStringField(TEXT("texture"), Texture)) ShowTexture(Texture);
		break;
	}
	case FOKModelRow::EKind::Material:
	{
		// Selecting a material adds its edit filled with the converted values.
		if (!Asset->MaterialEdits.ContainsByPredicate([&](const FOKModelMaterialEdit& E) { return E.Material.Equals(Row->Name, ESearchCase::IgnoreCase); }))
		{
			Asset->Modify();
			FOKModelMaterialEdit& E = Asset->MaterialEdits.AddDefaulted_GetRef();
			E.Material = Row->Name;
			const TSharedPtr<FJsonObject>* Obj = nullptr;
			if (Row->Json->TryGetObjectField(TEXT("colors"), Obj)) for (const auto& C : (*Obj)->Values)
			{
				const TArray<TSharedPtr<FJsonValue>>& A = C.Value->AsArray();
				if (A.Num() == 4) E.Colors.Add(FString(*C.Key), FLinearColor(A[0]->AsNumber(), A[1]->AsNumber(), A[2]->AsNumber(), A[3]->AsNumber()));
			}
			if (Row->Json->TryGetObjectField(TEXT("floats"), Obj)) for (const auto& F : (*Obj)->Values) E.Floats.Add(FString(*F.Key), F.Value->AsNumber());
			const TArray<TSharedPtr<FJsonValue>>* Slots = nullptr;
			if (Row->Json->TryGetArrayField(TEXT("textures"), Slots)) for (const TSharedPtr<FJsonValue>& S : *Slots)
			{
				const TArray<TSharedPtr<FJsonValue>>& A = S->AsArray();
				if (A.Num() == 2) E.Textures.Add(A[0]->AsString(), A[1]->AsString());
			}
			Details->ForceRefresh();
		}
		break;
	}
	case FOKModelRow::EKind::Texture:
		ShowTexture(Row->Name);
		break;
	}
}

void FOKModelEditor::ShowTexture(const FString& Name)
{
	TArray<uint8> Png;
	if (Name.IsEmpty()) return;
	// Some textures have no decodable image (e.g. placeholder masks): say so instead of keeping the last one.
	TextureShown = FOkModel::TexturePng(Asset->Library, Asset->ModelId, Name, Png) ? FImageUtils::ImportBufferAsTexture2D(Png) : nullptr;
	if (!TextureShown)
	{
		TextureBrush->SetResourceObject(nullptr);
		if (TextureImage) TextureImage->SetImage(TextureBrush.Get());
		if (Status) Status->SetText(FText::FromString(FString::Printf(TEXT("%s: no preview image"), *Name)));
		return;
	}
	TextureBrush->SetResourceObject(TextureShown);
	const float Scale = 256.f / FMath::Max(1, FMath::Max(TextureShown->GetSizeX(), TextureShown->GetSizeY()));
	TextureBrush->ImageSize = FVector2D(TextureShown->GetSizeX() * Scale, TextureShown->GetSizeY() * Scale);
	if (TextureImage) TextureImage->SetImage(TextureBrush.Get());
}

void FOKModelEditor::ShowPrefab(const FString& Prefab)
{
	if (!Prefab.IsEmpty()) CurrentPrefab = Prefab;
	if (CurrentPrefab.IsEmpty() || !Scene) return;
	if (Preview.IsValid()) Preview->Destroy();
	UWorld* World = Scene->GetWorld();
	FString Error;
	FOkSession Session;
	if (!Session.OpenOkModel(Asset->Library, Asset->ModelId, CurrentPrefab, bShowDisabled, Error))
	{
		if (Status) Status->SetText(FText::FromString(FString::Printf(TEXT("%s: %s"), *CurrentPrefab, *Error)));
		return;
	}
	FActorSpawnParameters Params;
	Params.ObjectFlags = RF_Transient;
	AKKPreviewActor* Actor = World->SpawnActor<AKKPreviewActor>(FVector::ZeroVector, FRotator::ZeroRotator, Params);
	if (!Actor) return;
	Actor->SetActorTickEnabled(false);
	Actor->Session = MoveTemp(Session);
	if (!Actor->LoadSession(Error))
	{
		if (Status) Status->SetText(FText::FromString(FString::Printf(TEXT("%s: %s"), *CurrentPrefab, *Error)));
		Actor->Destroy();
		return;
	}
	Preview = Actor;
	// The preview actor's studio camera is not part of the model (its editor mesh would show in the viewport).
	TArray<UCameraComponent*> Cameras;
	Actor->GetComponents(Cameras);
	for (UCameraComponent* Cam : Cameras) Cam->DestroyComponent();
	const FBox Box = PreviewBounds();
	UE_LOG(LogTemp, Display, TEXT("OK model preview %s: %d parts, bounds %s"), *CurrentPrefab, Actor->Session.PartCount(), *Box.ToString());
	bFocusPending = !(Viewport && Viewport->Focus(Box));
	if (Status) Status->SetText(FText::FromString(FString::Printf(TEXT("%s  (%d parts)"), *CurrentPrefab, Actor->Session.PartCount())));
}

void FOKModelEditor::ApplyEdits()
{
	bDirty = false;
	FString Error;
	if (!Asset->SaveEdits(Error))
	{
		if (Status) Status->SetText(FText::FromString(FString::Printf(TEXT("Edits not saved: %s"), *Error)));
		return;
	}
	ShowPrefab(FString());
}

FString FOKModelEditor::SaveAsNewModel()
{
	ApplyEdits();
	FString NewId, Error;
	if (!FOkModel::SaveAs(Asset->Library, Asset->ModelId, NewId, Error))
	{
		if (Status) Status->SetText(FText::FromString(FString::Printf(TEXT("Save As failed: %s"), *Error)));
		UE_LOG(LogTemp, Warning, TEXT("OK model Save As failed: %s"), *Error);
		return FString();
	}
	const FString Path = Asset->Library / TEXT("models") / NewId.Left(2) / (NewId + TEXT(".okmodel"));
	UE_LOG(LogTemp, Display, TEXT("OK model saved as new model %s (%s)"), *NewId, *Path);
	if (Status) Status->SetText(FText::FromString(FString::Printf(TEXT("Saved as new model %s"), *NewId)));
	if (GEngine) GEngine->Exec(nullptr, *FString::Printf(TEXT("OKModel.Open \"%s\""), *Path));
	return NewId;
}

void FOKModelEditor::RevertEdits()
{
	Asset->Modify();
	Asset->MaterialEdits.Reset();
	Asset->TextureEdits.Reset();
	Asset->MeshEdits.Reset();
	ApplyEdits();
	if (Details) Details->ForceRefresh();
}

FBox FOKModelEditor::PreviewBounds() const
{
	// The model's vertices (the actor's components carry fixed streaming bounds and a camera): Unity metres
	// (X Y Z) -> UE centimetres (-X Z Y), as the preview actor maps them.
	FBox Box(ForceInit);
	if (!Preview.IsValid()) return Box;
	const FTransform World = Preview->GetActorTransform();
	for (int32 I = 0; I < Preview->Session.PartCount(); ++I)
	{
		OkMeshView View;
		if (!Preview->Session.PartAt(I, View) || !View.positions) continue;
		for (int32 V = 0; V < View.n_verts; ++V)
		{
			const float* P = View.positions + V * 3;
			Box += World.TransformPosition(FVector(-P[0], P[2], P[1]) * 100.0);
		}
	}
	return Box;
}

bool FOKModelEditor::HasPreview() const
{
	return Preview.IsValid() && Viewport.IsValid() && !bFocusPending;
}

void FOKModelEditor::RunSelfTest(bool bExit)
{
	TArray<FString> Lines;
	int32 Failures = 0;
	auto Check = [&](bool bOk, const FString& What)
	{
		Lines.Add(FString::Printf(TEXT("%s  %s"), bOk ? TEXT("PASS") : TEXT("FAIL"), *What));
		if (!bOk) ++Failures;
	};
	const FString Edits = Asset->Library / TEXT("models") / Asset->ModelId.Left(2) / Asset->ModelId + TEXT(".okedit.json");

	Check(Asset->Prefabs.Num() > 0, FString::Printf(TEXT("contents: %d prefabs, %d materials, %d textures"), Asset->Prefabs.Num(), Asset->Materials.Num(), Asset->Textures.Num()));
	const int32 Parts = Preview.IsValid() ? Preview->Session.PartCount() : 0;
	Check(Parts > 0, FString::Printf(TEXT("preview %s: %d parts"), *CurrentPrefab, Parts));

	// A colour of the material of the first shown mesh: edit it, the preview session must carry the new value.
	FString Material, ColorKey, MeshName;
	TSharedPtr<FJsonObject> Info;
	FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Asset->InfoJson), Info);
	const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
	if (Info && Info->TryGetArrayField(TEXT("prefabs"), Items)) for (const TSharedPtr<FJsonValue>& P : *Items)
	{
		if (P->AsObject()->GetStringField(TEXT("name")) != CurrentPrefab) continue;
		const TArray<TSharedPtr<FJsonValue>>* Meshes = nullptr;
		if (P->AsObject()->TryGetArrayField(TEXT("meshes"), Meshes)) for (const TSharedPtr<FJsonValue>& M : *Meshes)
		{
			if (M->AsObject()->GetBoolField(TEXT("enabled")) && M->AsObject()->TryGetStringField(TEXT("material"), Material) && !Material.IsEmpty())
			{
				M->AsObject()->TryGetStringField(TEXT("name"), MeshName);
				break;
			}
		}
	}
	if (Info && Info->TryGetArrayField(TEXT("materials"), Items)) for (const TSharedPtr<FJsonValue>& M : *Items)
	{
		const TSharedPtr<FJsonObject>* Colors = nullptr;
		if (M->AsObject()->GetStringField(TEXT("name")) == Material && M->AsObject()->TryGetObjectField(TEXT("colors"), Colors))
		{
			for (const auto& C : (*Colors)->Values) { ColorKey = FString(*C.Key); if (ColorKey == TEXT("_Color")) break; }
		}
	}
	auto ReadColor = [&](FLinearColor& Out)
	{
		if (!Preview.IsValid()) return false;
		for (int32 I = 0; I < Preview->Session.PartCount(); ++I) if (Preview->Session.MaterialValue(I, TCHAR_TO_ANSI(*ColorKey), Out)) return true;
		return false;
	};
	FLinearColor Before, After, Reverted;
	const bool bHasColor = !ColorKey.IsEmpty() && ReadColor(Before);
	Check(bHasColor, FString::Printf(TEXT("material %s colour %s = %s"), *Material, *ColorKey, *Before.ToString()));
	if (bHasColor)
	{
		const FLinearColor Target(0.1f, 0.9f, 0.2f, 1.f);
		Asset->MaterialEdits.Reset();
		FOKModelMaterialEdit& E = Asset->MaterialEdits.AddDefaulted_GetRef();
		E.Material = Material;
		E.Colors.Add(ColorKey, Target);
		ApplyEdits();
		Check(IFileManager::Get().FileExists(*Edits), TEXT("edits overlay written: ") + Edits);
		const bool bEdited = ReadColor(After) && After.Equals(Target, 1e-3f);
		Check(bEdited, TEXT("edited colour in preview: ") + After.ToString());
		Check(Preview.IsValid() && Preview->Session.PartCount() == Parts, TEXT("part count unchanged after edit"));

		// Mesh edit: hiding the mesh removes its part from the preview.
		if (!MeshName.IsEmpty() && Parts > 1)
		{
			FOKModelMeshEdit& Hide = Asset->MeshEdits.AddDefaulted_GetRef();
			Hide.Prefab = CurrentPrefab;
			Hide.Mesh = MeshName;
			Hide.bHidden = true;
			ApplyEdits();
			const int32 Hidden = Preview.IsValid() ? Preview->Session.PartCount() : -1;
			Check(Hidden == Parts - 1, FString::Printf(TEXT("hidden mesh %s: %d -> %d parts"), *MeshName, Parts, Hidden));
			Asset->MeshEdits.Reset();
			ApplyEdits();
		}

		// Save As: the edited model becomes a new library model; the original keeps its own edits.
		FString NewId, SaveError, NewEdits, OldEdits;
		const bool bSaved = FOkModel::SaveAs(Asset->Library, Asset->ModelId, NewId, SaveError);
		const FString NewModel = Asset->Library / TEXT("models") / NewId.Left(2) / (NewId + TEXT(".okmodel"));
		Check(bSaved && NewId != Asset->ModelId && IFileManager::Get().FileExists(*NewModel), TEXT("save as new model ") + (bSaved ? NewId : SaveError));
		Check(bSaved && FOkModel::GetEdits(Asset->Library, NewId, NewEdits) && NewEdits.Contains(Material), TEXT("new model carries the edits"));
		RevertEdits();
		Check(bSaved && FOkModel::GetEdits(Asset->Library, NewId, NewEdits) && NewEdits.Contains(Material)
			&& FOkModel::GetEdits(Asset->Library, Asset->ModelId, OldEdits) && !OldEdits.Contains(Material), TEXT("reverting the original leaves the new model's edits"));
		if (bSaved)
		{
			IFileManager::Get().Delete(*NewModel);
			IFileManager::Get().Delete(*FPaths::ChangeExtension(NewModel, TEXT("okedit.json")));
			IFileManager::Get().DeleteDirectory(*FPaths::ChangeExtension(NewModel, TEXT("okedit")), false, true);
		}
		Check(!IFileManager::Get().FileExists(*Edits), TEXT("revert removes the overlay"));
		const bool bReverted = ReadColor(Reverted) && Reverted.Equals(Before, 1e-3f);
		Check(bReverted, TEXT("reverted colour: ") + Reverted.ToString());
	}

	// Texture preview: the first texture with an image (placeholder masks have none).
	if (Asset->Textures.Num() > 0)
	{
		TArray<uint8> Png;
		int32 Tried = 0;
		FString Shown;
		for (const FString& Name : Asset->Textures)
		{
			++Tried;
			if (FOkModel::TexturePng(Asset->Library, Asset->ModelId, Name, Png) && Png.Num() > 8) { Shown = Name; break; }
		}
		Check(!Shown.IsEmpty(), FString::Printf(TEXT("texture %s png %d bytes (%d tried)"), *Shown, Png.Num(), Tried));
		ShowTexture(Shown);
		Check(TextureShown != nullptr, TEXT("texture shown in the texture tab"));
	}

	const FBox Bounds = PreviewBounds();
	Check(Bounds.IsValid && Bounds.GetSize().GetMax() > 1.f, TEXT("preview bounds ") + Bounds.ToString());

	const FString Dir = FPaths::ProjectSavedDir() / TEXT("OKModel");
	TArray<FColor> Pixels;
	FIntPoint Size;
	if (Viewport && Viewport->Capture(Pixels, Size))
	{
		int32 Lit = 0;
		for (FColor& P : Pixels) { P.A = 255; if (P.R + P.G + P.B > 30) ++Lit; }
		TArray64<uint8> Png;
		FImageUtils::PNGCompressImageArray(Size.X, Size.Y, Pixels, Png);
		FFileHelper::SaveArrayToFile(Png, *(Dir / Asset->ModelId + TEXT(".png")));
		Lines.Add(FString::Printf(TEXT("INFO  viewport %dx%d, %d non-dark pixels"), Size.X, Size.Y, Lit));
	}
	else Lines.Add(TEXT("INFO  viewport not captured"));

	Lines.Add(Failures ? FString::Printf(TEXT("RESULT FAIL (%d)"), Failures) : FString(TEXT("RESULT PASS")));
	for (const FString& L : Lines) UE_LOG(LogTemp, Display, TEXT("OK model selftest: %s"), *L);
	FFileHelper::SaveStringArrayToFile(Lines, *(Dir / TEXT("editor_selftest.txt")));
	if (bExit) FPlatformMisc::RequestExit(false);
}

#undef LOCTEXT_NAMESPACE
