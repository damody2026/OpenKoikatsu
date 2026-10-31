#include "OKStudioEditor.h"
#include "OKSceneCard.h"
#include "OKStudioSequence.h"
#include "OKStudioMovie.h"
#include "OKStudioGame.h"
#include "ImageUtils.h"
#include "ImageCore.h"
#include "LevelSequence.h"
#include "MovieScene.h"
#include "Tracks/MovieSceneCameraCutTrack.h"
#include "AdvancedPreviewScene.h"
#include "AssetEditorModeManager.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Containers/Ticker.h"
#include "DesktopPlatformModule.h"
#include "EditorViewportClient.h"
#include "EditorModes.h"
#include "UnrealWidget.h"
#include "Editor.h"
#include "Selection.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "IDesktopPlatform.h"
#include "IDetailsView.h"
#include "KKPreviewActor.h"
#include "KKRustExtract.h"
#include "Camera/CameraComponent.h"
#include "Components/LightComponent.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "OKAnimationAssets.h"
#include "OKSceneActor.h"
#include "OKStudioCatalog.h"
#include "NiagaraEditorUtilities.h"
#include "NiagaraEmitter.h"
#include "NiagaraSystem.h"
#include "NiagaraSystemFactoryNew.h"
#include "NiagaraScriptSource.h"
#include "NiagaraGraph.h"
#include "NiagaraNodeFunctionCall.h"
#include "NiagaraRendererProperties.h"
#include "NiagaraSpriteRendererProperties.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionParticleColor.h"
#include "Materials/MaterialExpressionPixelDepth.h"
#include "Materials/MaterialExpressionSceneDepth.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "ViewModels/Stack/NiagaraStackGraphUtilities.h"
#include "ViewModels/Stack/NiagaraParameterHandle.h"
#include "Misc/PackageName.h"
#include "UObject/SavePackage.h"
#include "PropertyEditorModule.h"
#include "SEditorViewport.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/STableRow.h"

#define LOCTEXT_NAMESPACE "OKStudioEditor"

FOKStudioEditor* FOKStudioEditor::LastOpened = nullptr;
const FName FOKStudioEditor::OutlinerTab(TEXT("OKStudio_Outliner"));
const FName FOKStudioEditor::DetailsTab(TEXT("OKStudio_Details"));
const FName FOKStudioEditor::PoseTab(TEXT("OKStudio_Pose"));
const FName FOKStudioEditor::ViewportTab(TEXT("OKStudio_Viewport"));

/** The preview viewport's client: the editor's transform widget (W move / E rotate / R scale) works on the selected
 *  document object; drags are written to its transform, not to the preview actors. */
class FOKStudioViewportClient : public FEditorViewportClient
{
public:
	FOKStudioViewportClient(FOKStudioEditor* InOwner, FPreviewScene* InScene, const TSharedRef<SEditorViewport>& InViewport)
		: FEditorViewportClient(nullptr, InScene, InViewport), Owner(InOwner)
	{
		// The widget is shown for the selected document object (ShowWidget), not by an editor mode's actor selection.
		if (Widget) Widget->SetUsesEditorModeTools(nullptr);
		ShowWidget(false);
	}

	virtual FVector GetWidgetLocation() const override
	{
		FTransform World;
		return Owner && Owner->GizmoTransform(World) ? World.GetLocation() : FEditorViewportClient::GetWidgetLocation();
	}
	virtual FMatrix GetWidgetCoordSystem() const override
	{
		FTransform World;
		return Owner && GetWidgetCoordSystemSpace() == COORD_Local && Owner->GizmoTransform(World) ? FMatrix(FQuatRotationMatrix(World.GetRotation())) : FMatrix(FMatrix::Identity);
	}
	virtual bool InputWidgetDelta(FViewport* InViewport, EAxisList::Type CurrentAxis, FVector& Drag, FRotator& Rot, FVector& Scale) override
	{
		if (Owner && CurrentAxis != EAxisList::None && Owner->GizmoDelta(Drag, Rot, Scale)) return true;
		return FEditorViewportClient::InputWidgetDelta(InViewport, CurrentAxis, Drag, Rot, Scale);
	}
	virtual void TrackingStarted(const FInputEventState& InInputState, bool bIsDraggingWidget, bool bNudge) override
	{
		if (Owner && bIsDraggingWidget) Owner->GizmoBegin();
	}
	virtual void TrackingStopped() override
	{
		if (Owner) Owner->GizmoEnd();
	}

private:
	FOKStudioEditor* Owner = nullptr;
};

class SOKStudioViewport : public SEditorViewport
{
public:
	SLATE_BEGIN_ARGS(SOKStudioViewport) {}
	SLATE_END_ARGS()

	void Construct(const FArguments&, TSharedPtr<FAdvancedPreviewScene> InScene, FOKStudioEditor* InOwner)
	{
		Scene = InScene;
		Owner = InOwner;
		SEditorViewport::Construct(SEditorViewport::FArguments());
	}

	/** Shows the transform widget for an object (the scene actor stands for it in the widget's actor selection). */
	void ShowWidgetFor(AActor* SceneActor, bool bShow)
	{
		if (!Client || !Client->GetModeTools()) return;
		USelection* Actors = Client->GetModeTools()->GetSelectedActors();
		if (!Actors) return;
		Actors->BeginBatchSelectOperation();
		Actors->DeselectAll();
		if (bShow && SceneActor) Actors->Select(SceneActor);
		Actors->EndBatchSelectOperation(false);
		// The level editor sets this from its selection; this viewport has no level editor behind it.
		Client->GetModeTools()->SetSelectionHasSceneComponent(bShow && SceneActor);
		// Move handles until W / E / R picks another (an asset editor starts with no widget mode).
		if (bShow && Client->GetModeTools()->GetWidgetMode() == UE::Widget::WM_None) Client->GetModeTools()->SetWidgetMode(UE::Widget::WM_Translate);
		Client->ShowWidget(bShow);
	}
	TSharedPtr<FEditorViewportClient> GetClient() const { return Client; }

protected:
	virtual TSharedRef<FEditorViewportClient> MakeEditorViewportClient() override
	{
		Client = MakeShareable(new FOKStudioViewportClient(Owner, Scene.Get(), SharedThis(this)));
		Client->SetRealtime(true);
		Client->SetViewLocation(FVector(0.f, 420.f, 140.f));
		Client->SetViewRotation(FRotator(-10.f, -90.f, 0.f));
		Client->EngineShowFlags.SetGrid(true);
		if (FAssetEditorModeManager* Modes = static_cast<FAssetEditorModeManager*>(Client->GetModeTools()))
		{
			Modes->SetPreviewScene(Scene.Get());
			// The default mode draws the transform widget for the selection (an asset editor starts with no mode).
			Modes->SetDefaultMode(FBuiltinEditorModes::EM_Default);
			Modes->ActivateDefaultMode();
		}
		return Client.ToSharedRef();
	}

public:
	/** Camera location of the viewport (where "look at the camera" looks). */
	bool ViewLocation(FVector& Out) const { if (!Client) return false; Out = Client->GetViewLocation(); return true; }
	/** The viewport camera (scene cards are captured through it). */
	bool ViewCamera(FTransform& Out, float& OutFov) const { if (!Client) return false; Out = FTransform(Client->GetViewRotation(), Client->GetViewLocation()); OutFov = Client->ViewFOV; return true; }

private:
	TSharedPtr<FAdvancedPreviewScene> Scene;
	TSharedPtr<FEditorViewportClient> Client;
	FOKStudioEditor* Owner = nullptr;
};

/** Dragging an outliner row onto another makes it that object's child (onto the empty area: back to the root). */
class FOKStudioRowDrag : public FDragDropOperation
{
public:
	DRAG_DROP_OPERATOR_TYPE(FOKStudioRowDrag, FDragDropOperation)
	int32 Id = -1;
	static TSharedRef<FOKStudioRowDrag> New(int32 InId)
	{
		TSharedRef<FOKStudioRowDrag> Op = MakeShared<FOKStudioRowDrag>();
		Op->Id = InId;
		Op->Construct();
		return Op;
	}
};

namespace
{
	const TCHAR* KindTag(EOKSceneObjectKind Kind)
	{
		switch (Kind)
		{
		case EOKSceneObjectKind::Character: return TEXT("[角色]");
		case EOKSceneObjectKind::Item: return TEXT("[道具]");
		case EOKSceneObjectKind::Light: return TEXT("[燈光]");
		case EOKSceneObjectKind::Camera: return TEXT("[相機]");
		default: return TEXT("[資料夾]");
		}
	}
}

FText FOKStudioEditor::GetBaseToolkitName() const { return LOCTEXT("ToolkitName", "OK Studio"); }

TSharedPtr<FOKStudioEditor> FOKStudioEditor::Open(const FString& InFile)
{
	TSharedRef<FOKStudioEditor> Editor = MakeShared<FOKStudioEditor>();
	Editor->Init();
	if (!InFile.IsEmpty())
	{
		FString Error;
		if (!Editor->Load(FPaths::ConvertRelativePathToFull(InFile), Error)) Editor->SetStatus(Error);
	}
	return Editor;
}

void FOKStudioEditor::Init()
{
	Doc = NewObject<UOKSceneDocument>(GetTransientPackage(), NAME_None, RF_Transient | RF_Transactional);
	Selection = NewObject<UOKStudioSelection>(GetTransientPackage(), NAME_None, RF_Transient | RF_Transactional);
	Scene = MakeShared<FAdvancedPreviewScene>(FPreviewScene::ConstructionValues());
	if (UWorld* World = Scene->GetWorld())
	{
		FActorSpawnParameters Params;
		Params.ObjectFlags = RF_Transient;
		SceneActorPtr = World->SpawnActor<AOKSceneActor>(FVector::ZeroVector, FRotator::ZeroRotator, Params);
		if (SceneActorPtr.IsValid()) SceneActorPtr->Document = Doc;
	}
	Doc->OnChanged.AddLambda([this] { bSyncPending = true; });

	FPropertyEditorModule& PropertyEditor = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	FDetailsViewArgs Args;
	Args.bHideSelectionTip = true;
	Args.NameAreaSettings = FDetailsViewArgs::HideNameArea;
	Details = PropertyEditor.CreateDetailView(Args);
	Details->SetObject(Selection);
	SelectionChanged = FCoreUObjectDelegates::OnObjectPropertyChanged.AddLambda([this](UObject* Object, FPropertyChangedEvent&)
	{
		if (Object != Selection) return;
		if (FOKSceneObject* Target = Doc->Find(SelectedId))
		{
			// Identity and tree position stay the document's.
			FOKSceneObject Edited = Selection->Object;
			Edited.Id = Target->Id;
			Edited.Parent = Target->Parent;
			Edited.Kind = Target->Kind;
			*Target = Edited;
			Doc->NotifyChanged();
			if (Tree) Tree->RequestTreeRefresh();
		}
	});

	const TSharedRef<FTabManager::FLayout> Layout = FTabManager::NewLayout("OKStudio_Layout_v2")
		->AddArea(FTabManager::NewPrimaryArea()->SetOrientation(Orient_Horizontal)
			->Split(FTabManager::NewStack()->SetSizeCoefficient(0.2f)->AddTab(OutlinerTab, ETabState::OpenedTab))
			->Split(FTabManager::NewStack()->SetSizeCoefficient(0.55f)->SetHideTabWell(true)->AddTab(ViewportTab, ETabState::OpenedTab))
			->Split(FTabManager::NewStack()->SetSizeCoefficient(0.25f)->AddTab(DetailsTab, ETabState::OpenedTab)->AddTab(PoseTab, ETabState::OpenedTab)->SetForegroundTab(DetailsTab)));
	InitAssetEditor(EToolkitMode::Standalone, TSharedPtr<IToolkitHost>(), TEXT("OKStudioApp"), Layout, true, true, Doc);

	TSharedPtr<FExtender> Extender = MakeShared<FExtender>();
	Extender->AddToolBarExtension("Asset", EExtensionHook::After, GetToolkitCommands(), FToolBarExtensionDelegate::CreateRaw(this, &FOKStudioEditor::FillToolbar));
	AddToolbarExtender(Extender);
	RegenerateMenusAndToolbars();
	LastOpened = this;
	Refresh();
}

FOKStudioEditor::~FOKStudioEditor()
{
	if (LastOpened == this) LastOpened = nullptr;
	FCoreUObjectDelegates::OnObjectPropertyChanged.Remove(SelectionChanged);
	if (SceneActorPtr.IsValid()) SceneActorPtr->Destroy();
}

void FOKStudioEditor::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(Doc);
	Collector.AddReferencedObject(Selection);
}

void FOKStudioEditor::RegisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
	WorkspaceMenuCategory = InTabManager->AddLocalWorkspaceMenuCategory(LOCTEXT("Workspace", "OK Studio"));
	FAssetEditorToolkit::RegisterTabSpawners(InTabManager);
	InTabManager->RegisterTabSpawner(OutlinerTab, FOnSpawnTab::CreateLambda([this](const FSpawnTabArgs&)
	{
		SAssignNew(Tree, STreeView<TSharedPtr<FOKStudioRow>>)
			.TreeItemsSource(&Roots)
			.SelectionMode(ESelectionMode::Single)
			.OnGenerateRow(this, &FOKStudioEditor::MakeRow)
			.OnGetChildren_Lambda([](TSharedPtr<FOKStudioRow> Row, TArray<TSharedPtr<FOKStudioRow>>& Out) { Out = Row->Children; })
			.OnSelectionChanged_Lambda([this](TSharedPtr<FOKStudioRow> Row, ESelectInfo::Type) { if (Row) Select(Row->Id); });
		return SNew(SDockTab).Label(LOCTEXT("Outliner", "Scene"))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().FillHeight(1.f)[Tree.ToSharedRef()]
			+ SVerticalBox::Slot().AutoHeight().Padding(2.f)
			[
				SNew(STextBlock).Text(LOCTEXT("TreeHint", "拖到另一個物件上成為它的子物件；工具列 To Root 移回根層"))
			]
		];
	})).SetDisplayName(LOCTEXT("Outliner", "Scene")).SetGroup(WorkspaceMenuCategory.ToSharedRef());
	InTabManager->RegisterTabSpawner(ViewportTab, FOnSpawnTab::CreateLambda([this](const FSpawnTabArgs&)
	{
		SAssignNew(Viewport, SOKStudioViewport, Scene, this);
		return SNew(SDockTab).Label(LOCTEXT("Viewport", "Preview"))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().FillHeight(1.f)[Viewport.ToSharedRef()]
			+ SVerticalBox::Slot().AutoHeight().Padding(4.f)[SAssignNew(Status, STextBlock)]
		];
	})).SetDisplayName(LOCTEXT("Viewport", "Preview")).SetGroup(WorkspaceMenuCategory.ToSharedRef());
	InTabManager->RegisterTabSpawner(DetailsTab, FOnSpawnTab::CreateLambda([this](const FSpawnTabArgs&)
	{
		return SNew(SDockTab).Label(LOCTEXT("Details", "Object"))[Details.ToSharedRef()];
	})).SetDisplayName(LOCTEXT("Details", "Object")).SetGroup(WorkspaceMenuCategory.ToSharedRef());
	InTabManager->RegisterTabSpawner(PoseTab, FOnSpawnTab::CreateLambda([this](const FSpawnTabArgs&)
	{
		TSharedRef<SDockTab> Tab = SNew(SDockTab).Label(LOCTEXT("Pose", "Pose (FK / IK)"))[MakePoseTab()];
		RebuildPose();
		return Tab;
	})).SetDisplayName(LOCTEXT("Pose", "Pose (FK / IK)")).SetGroup(WorkspaceMenuCategory.ToSharedRef());
}

void FOKStudioEditor::UnregisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
	FAssetEditorToolkit::UnregisterTabSpawners(InTabManager);
	InTabManager->UnregisterTabSpawner(OutlinerTab);
	InTabManager->UnregisterTabSpawner(ViewportTab);
	InTabManager->UnregisterTabSpawner(DetailsTab);
	InTabManager->UnregisterTabSpawner(PoseTab);
}

TSharedRef<ITableRow> FOKStudioEditor::MakeRow(TSharedPtr<FOKStudioRow> Row, const TSharedRef<STableViewBase>& Owner)
{
	const int32 Id = Row->Id;
	return SNew(STableRow<TSharedPtr<FOKStudioRow>>, Owner)
		.OnDragDetected_Lambda([Id](const FGeometry&, const FPointerEvent&)
		{
			return FReply::Handled().BeginDragDrop(FOKStudioRowDrag::New(Id));
		})
		.OnCanAcceptDrop_Lambda([](const FDragDropEvent& Event, EItemDropZone Zone, TSharedPtr<FOKStudioRow>) -> TOptional<EItemDropZone>
		{
			return Event.GetOperationAs<FOKStudioRowDrag>().IsValid() ? TOptional<EItemDropZone>(EItemDropZone::OntoItem) : TOptional<EItemDropZone>();
		})
		.OnAcceptDrop_Lambda([this](const FDragDropEvent& Event, EItemDropZone, TSharedPtr<FOKStudioRow> Target)
		{
			const TSharedPtr<FOKStudioRowDrag> Op = Event.GetOperationAs<FOKStudioRowDrag>();
			if (!Op || !Target) return FReply::Unhandled();
			if (!Doc->Reparent(Op->Id, Op->Id == Target->Id ? -1 : Target->Id)) SetStatus(TEXT("無法移到自己的子物件底下"));
			Refresh();
			return FReply::Handled();
		})
		[
			SNew(STextBlock).Text_Lambda([this, Id]
			{
				const FOKSceneObject* O = Doc->Find(Id);
				if (!O) return FText::GetEmpty();
				const FString* Error = SceneActor() ? SceneActor()->Errors.Find(Id) : nullptr;
				return FText::FromString(FString::Printf(TEXT("%s %s%s%s"), KindTag(O->Kind), *O->Name, O->bVisible ? TEXT("") : TEXT("（隱藏）"), Error ? TEXT("  ⚠") : TEXT("")));
			})
			.ToolTipText_Lambda([this, Id]
			{
				const FString* Error = SceneActor() ? SceneActor()->Errors.Find(Id) : nullptr;
				return Error ? FText::FromString(*Error) : FText::GetEmpty();
			})
		];
}

void FOKStudioEditor::Refresh()
{
	TMap<int32, TSharedPtr<FOKStudioRow>> Rows;
	for (const FOKSceneObject& O : Doc->Objects) { TSharedPtr<FOKStudioRow> R = MakeShared<FOKStudioRow>(); R->Id = O.Id; Rows.Add(O.Id, R); }
	Roots.Reset();
	for (const FOKSceneObject& O : Doc->Objects)
	{
		if (const TSharedPtr<FOKStudioRow>* Parent = Rows.Find(O.Parent)) (*Parent)->Children.Add(Rows[O.Id]);
		else Roots.Add(Rows[O.Id]);
	}
	if (Tree)
	{
		Tree->RequestTreeRefresh();
		for (auto& Pair : Rows) Tree->SetItemExpansion(Pair.Value, true);
		if (TSharedPtr<FOKStudioRow>* Row = Rows.Find(SelectedId)) Tree->SetSelection(*Row);
	}
	if (SceneActor()) SceneActor()->Sync();
	bSyncPending = false;
}

void FOKStudioEditor::Select(int32 Id)
{
	SelectedId = Id;
	if (const FOKSceneObject* O = Doc->Find(Id)) Selection->Object = *O;
	if (Viewport) Viewport->ShowWidgetFor(SceneActor(), Doc->Find(Id) != nullptr);
	if (Details) Details->ForceRefresh();
	RebuildPose();
}

bool FOKStudioEditor::GizmoTransform(FTransform& OutWorld) const
{
	const AOKSceneActor* Actor = SceneActor();
	if (!Actor || !Doc->Find(SelectedId)) return false;
	OutWorld = Doc->WorldTransform(SelectedId) * Actor->GetActorTransform();
	return true;
}

bool FOKStudioEditor::GizmoDelta(const FVector& Drag, const FRotator& Rot, const FVector& Scale)
{
	FOKSceneObject* Object = Doc->Find(SelectedId);
	FTransform World;
	if (!Object || !GizmoTransform(World)) return false;
	if (!Drag.IsZero()) World.AddToTranslation(Drag);
	if (!Rot.IsZero()) World.SetRotation((Rot.Quaternion() * World.GetRotation()).GetNormalized());
	if (!Scale.IsZero()) World.SetScale3D((World.GetScale3D() + Scale).ComponentMax(FVector(0.01)));
	const FTransform Frame = Doc->ParentFrame(Object->Id) * SceneActor()->GetActorTransform();
	Object->Transform = World.GetRelativeTransform(Frame);
	Selection->Object.Transform = Object->Transform;
	Doc->NotifyChanged();
	return true;
}

void FOKStudioEditor::GizmoBegin()
{
	if (bGizmoTransaction || !Doc->Find(SelectedId) || !GEditor) return;
	GEditor->BeginTransaction(LOCTEXT("GizmoMove", "Move OK Studio Object"));
	Doc->Modify();
	bGizmoTransaction = true;
}

void FOKStudioEditor::GizmoEnd()
{
	if (!bGizmoTransaction) return;
	bGizmoTransaction = false;
	if (GEditor) GEditor->EndTransaction();
	if (Details) Details->ForceRefresh();
}

void FOKStudioEditor::SetStatus(const FString& Text)
{
	if (Status) Status->SetText(FText::FromString(Text));
	UE_LOG(LogTemp, Display, TEXT("OK studio: %s"), *Text);
}

FString FOKStudioEditor::PickFile(bool bSave, const FString& Title, const FString& Types, const FString& Default) const
{
	IDesktopPlatform* Platform = FDesktopPlatformModule::Get();
	if (!Platform) return FString();
	TArray<FString> Files;
	const void* Window = FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr);
	const bool bOk = bSave ? Platform->SaveFileDialog(Window, Title, Default, TEXT(""), Types, EFileDialogFlags::None, Files)
		: Platform->OpenFileDialog(Window, Title, Default, TEXT(""), Types, EFileDialogFlags::None, Files);
	return bOk && Files.Num() ? FPaths::ConvertRelativePathToFull(Files[0]) : FString();
}

int32 FOKStudioEditor::Add(EOKSceneObjectKind Kind)
{
	// Into the selected folder, or next to the selected object.
	const FOKSceneObject* Sel = Doc->Find(SelectedId);
	const int32 Parent = !Sel ? -1 : (Sel->Kind == EOKSceneObjectKind::Folder ? Sel->Id : Sel->Parent);
	const int32 Id = Doc->AddObject(Kind, FString(), Parent);
	FOKSceneObject* O = Doc->Find(Id);
	if (Kind == EOKSceneObjectKind::Light) O->Transform.SetLocation(FVector(0.f, 150.f, 220.f));
	if (Kind == EOKSceneObjectKind::Camera) O->Transform = FTransform(FRotator(-10.f, -90.f, 0.f), FVector(0.f, 400.f, 140.f));
	Refresh();
	Select(Id);
	return Id;
}

void FOKStudioEditor::FillToolbar(FToolBarBuilder& ToolbarBuilder)
{
	ToolbarBuilder.BeginSection("OKStudioFile");
	ToolbarBuilder.AddToolBarButton(FUIAction(FExecuteAction::CreateLambda([this]
	{
		Doc->Objects.Reset(); Doc->ActiveCamera = -1; Doc->NextId = 1; File.Reset(); SelectedId = -1; Refresh(); SetStatus(TEXT("新場景"));
	})), NAME_None, LOCTEXT("New", "New"), LOCTEXT("NewTip", "Start an empty scene"));
	ToolbarBuilder.AddToolBarButton(FUIAction(FExecuteAction::CreateLambda([this]
	{
		const FString Path = PickFile(false, TEXT("Open OK scene"), TEXT("Scenes: OK scene card, KStudio scene card, .okscene (*.png;*.okscene)|*.png;*.okscene"), FPaths::ProjectDir() / TEXT("UserData/Studio/scene"));
		FString Error;
		if (!Path.IsEmpty() && !Load(Path, Error)) SetStatus(Error);
	})), NAME_None, LOCTEXT("OpenScene", "Open"), LOCTEXT("OpenTip", "Open an OK scene card, a KStudio scene card (imported) or an .okscene file"));
	ToolbarBuilder.AddToolBarButton(FUIAction(FExecuteAction::CreateLambda([this]
	{
		const FString Path = File.IsEmpty() ? PickFile(true, TEXT("Save OK scene"), TEXT("OK scene card (*.png)|*.png|OK scene (*.okscene)|*.okscene"), FPaths::ProjectDir() / TEXT("UserData/Studio/scene")) : File;
		FString Error;
		if (!Path.IsEmpty()) SetStatus(SaveTo(Path, Error) ? TEXT("已存 ") + Path : Error);
	})), NAME_None, LOCTEXT("SaveScene", "Save"), LOCTEXT("SaveSceneTip", "Save the scene"));
	ToolbarBuilder.AddToolBarButton(FUIAction(FExecuteAction::CreateLambda([this]
	{
		const FString Path = PickFile(true, TEXT("Save OK scene as"), TEXT("OK scene card (*.png)|*.png|OK scene (*.okscene)|*.okscene"), FPaths::ProjectDir() / TEXT("UserData/Studio/scene"));
		FString Error;
		if (!Path.IsEmpty()) SetStatus(SaveTo(Path, Error) ? TEXT("已存 ") + Path : Error);
	})), NAME_None, LOCTEXT("SaveSceneAs", "Save As"), LOCTEXT("SaveAsTip", "Save the scene as a new file"));
	ToolbarBuilder.AddToolBarButton(FUIAction(FExecuteAction::CreateLambda([this]
	{
		FString Report, Error;
		const FString Name = File.IsEmpty() ? FString(TEXT("OKStudioScene")) : FPaths::GetBaseFilename(File);
		SetStatus(FOKStudioSequence::Export(Doc, Name, Report, Error) ? TEXT("已轉出 Level Sequence ") + Report : Error);
	})), NAME_None, LOCTEXT("ExportSequence", "Export Sequence"), LOCTEXT("ExportSequenceTip", "Export the timeline as a Level Sequence (/Game/OKStudio/Sequences) for Sequencer and Movie Render Queue"));
	ToolbarBuilder.AddToolBarButton(FUIAction(FExecuteAction::CreateLambda([this]
	{
		FString Report, Error;
		const FString Name = File.IsEmpty() ? FString(TEXT("OKStudioScene")) : FPaths::GetBaseFilename(File);
		ULevelSequence* Sequence = FOKStudioSequence::Export(Doc, Name, Report, Error);
		if (!Sequence || !FOKStudioMovie::Render(Sequence, 1920, 1080, 8, [this](const FString& Movie, const FString& MovieError) { SetStatus(Movie.IsEmpty() ? TEXT("影片失敗：") + MovieError : TEXT("影片：") + Movie); }, Error)) { SetStatus(Error); return; }
		SetStatus(TEXT("Movie Render Queue 算圖中（1920×1080，每格 8 個時間取樣）…"));
	})), NAME_None, LOCTEXT("RenderMovie", "Render Movie"), LOCTEXT("RenderMovieTip", "Export the sequence and render it with Movie Render Queue (anti-aliasing, motion blur) into an mp4 in UserData/Studio/movie. Needs the GPU skin cache turned on in Config/DefaultEngine.ini (Docs/OK_STUDIO_PLAN.md R3); otherwise use the game's mp4 export."));
	ToolbarBuilder.EndSection();

	ToolbarBuilder.BeginSection("OKStudioAdd");
	ToolbarBuilder.AddToolBarButton(FUIAction(FExecuteAction::CreateLambda([this] { Add(EOKSceneObjectKind::Folder); })), NAME_None, LOCTEXT("AddFolder", "+ Folder"), LOCTEXT("AddFolderTip", "Add a folder"));
	ToolbarBuilder.AddToolBarButton(FUIAction(FExecuteAction::CreateLambda([this]
	{
		const FString Card = PickFile(false, TEXT("Character card"), TEXT("Card (*.png)|*.png"), FPaths::ProjectContentDir() / TEXT("CharacterCards/CrowdAPNG"));
		if (Card.IsEmpty()) return;
		const int32 Id = Add(EOKSceneObjectKind::Character);
		FOKSceneObject* O = Doc->Find(Id);
		O->Character.Card = Card;
		O->Name = FPaths::GetBaseFilename(Card);
		Doc->NotifyChanged(); Refresh(); Select(Id);
	})), NAME_None, LOCTEXT("AddCharacter", "+ Character"), LOCTEXT("AddCharacterTip", "Add a character from an OK card (or an original KKS card)"));
	ToolbarBuilder.AddToolBarButton(FUIAction(FExecuteAction::CreateLambda([this]
	{
		const FString Model = PickFile(false, TEXT("OK base library model"), TEXT("OK model (*.okmodel)|*.okmodel"), AOKSceneActor::LibraryDirectory() / TEXT("models"));
		if (Model.IsEmpty()) return;
		const int32 Id = Add(EOKSceneObjectKind::Item);
		FOKSceneObject* O = Doc->Find(Id);
		O->Item.Model = FPaths::GetBaseFilename(Model);
		Doc->NotifyChanged(); Refresh(); Select(Id);
	})), NAME_None, LOCTEXT("AddItem", "+ Item"), LOCTEXT("AddItemTip", "Add an OK base library model"));
	// Studio catalogue: group > category > item, and the scene map.
	ToolbarBuilder.AddComboButton(FUIAction(), FOnGetContent::CreateLambda([this]
	{
		FMenuBuilder Menu(true, nullptr);
		const FOKStudioCatalog& C = FOKStudioCatalog::Get();
		if (!C.bLoaded) { Menu.AddWidget(SNew(STextBlock).Text(FText::FromString(C.Error)), FText::GetEmpty()); return Menu.MakeWidget(); }
		for (const FOKStudioCatalogGroup& G : C.Groups)
		{
			const int32 Group = G.Id;
			Menu.AddSubMenu(FText::FromString(G.Name), FText::GetEmpty(), FNewMenuDelegate::CreateLambda([this, Group](FMenuBuilder& Sub)
			{
				for (const FOKStudioCatalogGroup* Cat : FOKStudioCatalog::Get().CategoriesOf(Group))
				{
					const int32 Category = Cat->Id;
					Sub.AddSubMenu(FText::FromString(Cat->Name), FText::GetEmpty(), FNewMenuDelegate::CreateLambda([this, Group, Category](FMenuBuilder& Items)
					{
						for (const FOKStudioCatalogItem* I : FOKStudioCatalog::Get().ItemsIn(Group, Category))
						{
							const int32 No = I->No;
							const FString Name = I->Name;
							Items.AddMenuEntry(FText::FromString(Name), FText::FromString(I->Prefab), FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([this, Group, Category, No, Name]
							{
								const int32 Id = Add(EOKSceneObjectKind::Item);
								FOKSceneObject* O = Doc->Find(Id);
								O->Item.Group = Group; O->Item.Category = Category; O->Item.No = No;
								O->Name = Name;
								Doc->NotifyChanged(); Refresh(); Select(Id);
							})));
						}
					}));
				}
			}));
		}
		return Menu.MakeWidget();
	}), LOCTEXT("AddStudioItem", "+ Studio Item"), LOCTEXT("AddStudioItemTip", "Add a vanilla Studio item from the OK base library catalogue"));
	ToolbarBuilder.AddComboButton(FUIAction(), FOnGetContent::CreateLambda([this]
	{
		FMenuBuilder Menu(true, nullptr);
		const FOKStudioCatalog& Catalog = FOKStudioCatalog::Get();
		for (const FString& Category : Catalog.EffectCategories)
		{
			Menu.AddSubMenu(FText::FromString(Category), FText::GetEmpty(), FNewMenuDelegate::CreateLambda([this, Category](FMenuBuilder& Sub)
			{
				for (const FOKStudioEffect& E : FOKStudioCatalog::Get().Effects)
				{
					if (E.Category != Category) continue;
					const FString Id = E.Id, Name = E.Name;
					Sub.AddMenuEntry(FText::FromString(Name), FText::FromString(E.Asset), FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([this, Id, Name]
					{
						const int32 Obj = Add(EOKSceneObjectKind::Effect);
						FOKSceneObject* O = Doc->Find(Obj);
						O->Effect.Effect = Id;
						O->Name = Name;
						Doc->NotifyChanged(); Refresh(); Select(Obj);
					})));
				}
			}));
		}
		return Menu.MakeWidget();
	}), LOCTEXT("AddEffect", "+ Effect"), LOCTEXT("AddEffectTip", "Add a Niagara effect of the OK effect library (effects.json)"));
	ToolbarBuilder.AddComboButton(FUIAction(), FOnGetContent::CreateLambda([this]
	{
		FMenuBuilder Menu(true, nullptr);
		Menu.AddMenuEntry(LOCTEXT("NoMap", "(no map)"), FText::GetEmpty(), FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([this] { Doc->Map = -1; Doc->NotifyChanged(); SetStatus(TEXT("無地圖")); })));
		for (const FOKStudioCatalogMap& M : FOKStudioCatalog::Get().Maps)
		{
			const int32 No = M.No;
			const FString Name = M.Name;
			Menu.AddMenuEntry(FText::FromString(FString::Printf(TEXT("%d %s"), No, *Name)), FText::FromString(M.Prefab), FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([this, No, Name] { Doc->Map = No; Doc->NotifyChanged(); SetStatus(TEXT("地圖 ") + Name); })));
		}
		return Menu.MakeWidget();
	}), LOCTEXT("SceneMap", "Map"), LOCTEXT("SceneMapTip", "Studio map of the scene"));
	ToolbarBuilder.AddToolBarButton(FUIAction(FExecuteAction::CreateLambda([this] { Add(EOKSceneObjectKind::Light); })), NAME_None, LOCTEXT("AddLight", "+ Light"), LOCTEXT("AddLightTip", "Add a light"));
	ToolbarBuilder.AddToolBarButton(FUIAction(FExecuteAction::CreateLambda([this] { Add(EOKSceneObjectKind::Camera); })), NAME_None, LOCTEXT("AddCamera", "+ Camera"), LOCTEXT("AddCameraTip", "Add a camera object"));
	ToolbarBuilder.EndSection();

	ToolbarBuilder.BeginSection("OKStudioEdit");
	ToolbarBuilder.AddToolBarButton(FUIAction(FExecuteAction::CreateLambda([this] { const int32 Id = Doc->Duplicate(SelectedId); Refresh(); if (Id >= 0) Select(Id); })), NAME_None, LOCTEXT("Duplicate", "Duplicate"), LOCTEXT("DuplicateTip", "Copy the selected object and its children"));
	ToolbarBuilder.AddToolBarButton(FUIAction(FExecuteAction::CreateLambda([this] { Doc->Reparent(SelectedId, -1); Refresh(); })), NAME_None, LOCTEXT("ToRoot", "To Root"), LOCTEXT("ToRootTip", "Move the selected object to the scene root (keeping where it is)"));
	ToolbarBuilder.AddToolBarButton(FUIAction(FExecuteAction::CreateLambda([this] { Doc->RemoveObject(SelectedId); SelectedId = -1; Refresh(); })), NAME_None, LOCTEXT("Delete", "Delete"), LOCTEXT("DeleteTip", "Delete the selected object and its children"));
	ToolbarBuilder.AddToolBarButton(FUIAction(FExecuteAction::CreateLambda([this]
	{
		const FOKSceneObject* O = Doc->Find(SelectedId);
		if (O && O->Kind == EOKSceneObjectKind::Camera) { Doc->ActiveCamera = O->Id; SetStatus(TEXT("使用相機 ") + O->Name); }
		else { Doc->ActiveCamera = -1; SetStatus(TEXT("自由相機")); }
	})), NAME_None, LOCTEXT("UseCamera", "Use Camera"), LOCTEXT("UseCameraTip", "View the scene through the selected camera object (none selected: free camera)"));
	ToolbarBuilder.EndSection();
}

bool FOKStudioEditor::SaveTo(const FString& Path, FString& OutError)
{
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
	if (FPaths::GetExtension(Path).Equals(TEXT("png"), ESearchCase::IgnoreCase))
	{
		// OK scene card through the viewport camera.
		FTransform View;
		float Fov = 60.f;
		TArray<TArray<uint8>> Views;
		if (!SceneActor() || !Viewport.IsValid() || !Viewport->ViewCamera(View, Fov)) { OutError = TEXT("no viewport to capture the scene card"); return false; }
		Refresh();
		if (!FOKSceneCard::CaptureViews(SceneActor(), View, Fov, nullptr, Views, OutError) || !FOKSceneCard::Write(Doc, Views, Path, OutError)) return false;
	}
	else if (!Doc->SaveToFile(Path, OutError)) return false;
	File = Path;
	return true;
}

bool FOKStudioEditor::Load(const FString& Path, FString& OutError)
{
	FString Note;
	if (!FOKSceneCard::Open(Path, Doc, OutError, &Note)) return false;
	UE_LOG(LogTemp, Display, TEXT("OK studio editor: opened %s: %s"), *Path, *Note);
	File = FOKSceneCard::Kind(Path) == FOKSceneCard::EKind::KStudio ? FString() : Path;
	SelectedId = -1;
	Refresh();
	SetStatus(FString::Printf(TEXT("%s：%d 個物件"), *FPaths::GetCleanFilename(Path), Doc->Objects.Num()));
	return true;
}

void FOKStudioEditor::Tick(float)
{
	if (bSyncPending) Refresh();
	if (AOKSceneActor* Actor = SceneActor()) Actor->bHasViewLocation = Viewport.IsValid() && Viewport->ViewLocation(Actor->ViewLocation);
}

void FOKStudioEditor::RunSelfTest(bool bExit)
{
	TArray<FString> Lines;
	int32 Failures = 0;
	// Each step is logged and flushed: a step that never returns shows where.
	auto Step = [](const TCHAR* What) { UE_LOG(LogTemp, Display, TEXT("OK studio editor selftest step: %s"), What); GLog->Flush(); };
	auto Check = [&](bool bOk, const FString& What)
	{
		Lines.Add(FString::Printf(TEXT("%s  %s"), bOk ? TEXT("PASS") : TEXT("FAIL"), *What));
		if (!bOk) ++Failures;
	};
	Step(TEXT("start"));
	AOKSceneActor* Actor = SceneActor();
	Check(Actor != nullptr, TEXT("scene actor in the preview world"));
	if (!Actor) { Lines.Add(TEXT("RESULT FAIL")); for (const FString& L : Lines) UE_LOG(LogTemp, Display, TEXT("OK studio selftest: %s"), *L); if (bExit) FPlatformMisc::RequestExit(false); return; }

	Doc->Objects.Reset(); Doc->NextId = 1; Doc->ActiveCamera = -1;
	const int32 Folder = Doc->AddObject(EOKSceneObjectKind::Folder, TEXT("Group"));
	Doc->Find(Folder)->Transform = FTransform(FRotator(0.f, 30.f, 0.f), FVector(0.f, 0.f, 0.f));
	const int32 A = Doc->AddObject(EOKSceneObjectKind::Character, TEXT("A"), Folder);
	Doc->Find(A)->Character.Card = TEXT("Content/CharacterCards/CrowdAPNG/C000.png");
	Doc->Find(A)->Transform.SetLocation(FVector(60.f, 0.f, 0.f));
	const int32 B = Doc->AddObject(EOKSceneObjectKind::Character, TEXT("B"));
	Doc->Find(B)->Character.Card = TEXT("Content/CharacterCards/CrowdAPNG/C001.png");
	Doc->Find(B)->Character.ClothesStates[0] = 3;
	Doc->Find(B)->Transform.SetLocation(FVector(-60.f, 0.f, 0.f));
	Step(TEXT("animation assets"));
	TArray<FAssetData> Anims;
	FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get().GetAssetsByClass(UOKAnimSequence::StaticClass()->GetClassPathName(), Anims, true);
	if (Anims.Num()) Doc->Find(B)->Character.Animation = Anims[0].GetSoftObjectPath();
	const int32 Item = Doc->AddObject(EOKSceneObjectKind::Item, TEXT("Hair model"));
	Doc->Find(Item)->Item.Model = TEXT("447f3c180000000045cc17684f4b4331");
	Doc->Find(Item)->Transform.SetLocation(FVector(0.f, 80.f, 100.f));
	const int32 Light = Doc->AddObject(EOKSceneObjectKind::Light, TEXT("Key light"));
	Doc->Find(Light)->Transform.SetLocation(FVector(0.f, -200.f, 220.f));
	const int32 Cam = Doc->AddObject(EOKSceneObjectKind::Camera, TEXT("Camera"));
	Doc->Find(Cam)->Transform.SetLocation(FVector(0.f, 400.f, 140.f));
	Doc->ActiveCamera = Cam;
	Step(TEXT("build scene"));
	Refresh();
	Step(TEXT("scene built"));

	const AKKPreviewActor* PA = Actor->PreviewOf(A);
	const AKKPreviewActor* PB = Actor->PreviewOf(B);
	const AKKPreviewActor* PItem = Actor->PreviewOf(Item);
	Check(PA && PA->Session.PartCount() > 0, FString::Printf(TEXT("character A (OK card C000): %d parts"), PA ? PA->Session.PartCount() : 0));
	Check(PB && PB->Session.PartCount() > 0, FString::Printf(TEXT("character B (OK card C001, top off): %d parts"), PB ? PB->Session.PartCount() : 0));
	Check(PItem && PItem->Session.PartCount() > 0, FString::Printf(TEXT("item (library model): %d parts"), PItem ? PItem->Session.PartCount() : 0));
	Check(Cast<ULightComponent>(Actor->ComponentOf(Light)) != nullptr, TEXT("light component"));
	Check(Actor->ActiveCamera() != nullptr, TEXT("active camera component"));
	// Viewport transform widget on the selected object: shown, and a move / rotate drag writes the object's transform
	// (in its parent's frame: A sits in the rotated folder).
	if (Viewport && Viewport->GetClient())
	{
		FEditorViewportClient& Client = *Viewport->GetClient();
		Select(A);
		FEditorModeTools* Tools = Client.GetModeTools();
		Check(Client.GetShowWidget(), FString::Printf(TEXT("selecting an object shows the transform widget (default mode active %d, selected actors %d, scene component %d, widget mode %d)"),
			Tools->IsDefaultModeActive() ? 1 : 0, Tools->GetSelectedActors() ? Tools->GetSelectedActors()->Num() : -1, Tools->SelectionHasSceneComponent() ? 1 : 0, int32(Tools->GetWidgetMode())));
		const FVector Before = (Doc->WorldTransform(A) * Actor->GetActorTransform()).GetLocation();
		Check(Client.GetWidgetLocation().Equals(Before, 0.01f), FString::Printf(TEXT("widget at the object (%s)"), *Client.GetWidgetLocation().ToString()));
		FVector Drag(25.f, 0.f, 0.f), Scale(0.f);
		FRotator Rot(0.f);
		const bool bMoved = Client.InputWidgetDelta(Client.Viewport, EAxisList::X, Drag, Rot, Scale);
		const FVector After = (Doc->WorldTransform(A) * Actor->GetActorTransform()).GetLocation();
		Check(bMoved && After.Equals(Before + FVector(25.f, 0.f, 0.f), 0.01f) && Selection->Object.Transform.Equals(Doc->Find(A)->Transform),
			FString::Printf(TEXT("dragging the X handle moves the object 25 cm in the world (%s -> %s)"), *Before.ToString(), *After.ToString()));
		const float YawBefore = (Doc->WorldTransform(A) * Actor->GetActorTransform()).Rotator().Yaw;
		Drag = FVector::ZeroVector; Rot = FRotator(0.f, 40.f, 0.f);
		Client.InputWidgetDelta(Client.Viewport, EAxisList::Z, Drag, Rot, Scale);
		const float YawAfter = (Doc->WorldTransform(A) * Actor->GetActorTransform()).Rotator().Yaw;
		Check(FMath::IsNearlyEqual(FRotator::NormalizeAxis(YawAfter - YawBefore), 40.f, 0.05f), FString::Printf(TEXT("dragging the rotate ring turns it 40 degrees (%.2f -> %.2f)"), YawBefore, YawAfter));
		Doc->Find(A)->Transform = FTransform(FVector(60.f, 0.f, 0.f));
		Doc->NotifyChanged();
		Select(-1);
		Check(!Client.GetShowWidget(), TEXT("no selection hides the widget"));
	}
	const FVector Expected = Doc->WorldTransform(A).GetLocation();
	Check(PA && PA->GetActorLocation().Equals(Expected, 0.01f) && !Expected.Equals(FVector(60.f, 0.f, 0.f), 1.f),
		TEXT("child of a rotated folder placed in scene space: ") + (PA ? PA->GetActorLocation().ToString() : FString()));
	if (Anims.Num())
	{
		Check(PB && PB->PoseMode == 3, TEXT("animation ") + Anims[0].AssetName.ToString());
		Actor->AdvanceAnimations(0.25f);
	}

	Step(TEXT("pose tab"));
	{
		// The Pose tab lists every FK bone and the 13 IK targets of the selected character, and edits the document.
		Select(A);
		const int32 Rows = PoseBox ? PoseBox->NumSlots() : 0;
		const int32 Bones = FOKStudioCatalog::Get().FkBones.Num();
		EditCharacter([](FOKSceneCharacter& Ch) { Ch.Fk.Add({ TEXT("cf_j_arm00_L"), FVector(20.f, 0.f, 0.f) }); });
		const bool bEdited = Doc->Find(A)->Character.Fk.ContainsByPredicate([](const FOKSceneFkBone& B) { return B.Bone == TEXT("cf_j_arm00_L"); }) && Selection->Object.Character.Fk == Doc->Find(A)->Character.Fk;
		Check(Rows >= Bones + 13 && bEdited, FString::Printf(TEXT("pose tab: %d rows for %d FK bones and 13 IK targets; editing reaches the document %d"), Rows, Bones, bEdited ? 1 : 0));
		EditCharacter([](FOKSceneCharacter& Ch) { Ch.Fk.Reset(); });
		Refresh();
	}

	Step(TEXT("save and load"));
	const FString Path = FPaths::ProjectSavedDir() / TEXT("OKStudio/selftest.okscene");
	FString Error;
	const bool bSaved = SaveTo(Path, Error);
	UOKSceneDocument* Reloaded = NewObject<UOKSceneDocument>();
	const bool bLoaded = bSaved && Reloaded->LoadFromFile(Path, Error);
	Check(bSaved && bLoaded && Reloaded->ToJson() == Doc->ToJson(), TEXT("save and load round trip: ") + (bLoaded ? Path : Error));
	{
		// The same scene as an OK scene card: four views inside, the scene back with the same objects.
		const FString CardPath = FPaths::ProjectSavedDir() / TEXT("OKStudio/selftest_editor_card.png");
		FString CardError;
		const bool bCard = SaveTo(CardPath, CardError);
		File.Reset();
		UOKSceneDocument* FromCard = NewObject<UOKSceneDocument>();
		const bool bOpened = bCard && FOKSceneCard::Kind(CardPath) == FOKSceneCard::EKind::OkScene && FOKSceneCard::Open(CardPath, FromCard, CardError);
		Check(bOpened && FromCard->Objects.Num() == Doc->Objects.Num(), FString::Printf(TEXT("editor scene card round trip: %d of %d objects %s"), bOpened ? FromCard->Objects.Num() : 0, Doc->Objects.Num(), *CardError));
	}
	{
		// Timeline -> Level Sequence: a keyed camera becomes a spawnable with a transform track and the camera cut.
		const int32 SeqCam = Add(EOKSceneObjectKind::Camera);
		Doc->Find(SeqCam)->Transform.SetLocation(FVector(0.f, 300.f, 150.f));
		Doc->SetKey(SeqCam, 0.f);
		Doc->Find(SeqCam)->Transform.SetLocation(FVector(200.f, 300.f, 150.f));
		Doc->SetKey(SeqCam, 2.f);
		Doc->ActiveCamera = SeqCam;
		FString SeqReport, SeqError;
		ULevelSequence* Seq = FOKStudioSequence::Export(Doc, TEXT("_selftest_sequence"), SeqReport, SeqError);
		UMovieScene* Movie = Seq ? Seq->GetMovieScene() : nullptr;
		const bool bCut = Movie && Movie->GetCameraCutTrack() && !Movie->GetCameraCutTrack()->GetAllSections().IsEmpty();
		Check(Movie && Movie->GetSpawnableCount() == 2 && Movie->GetPossessableCount() == 1 && bCut, FString::Printf(TEXT("Level Sequence export (scene, camera, its focal length track, camera cut): %s %s"), *SeqReport, *SeqError));
		// The scene spawnable shows the real scene on its own (what Sequencer and Movie Render Queue spawn).
		const AOKSceneActor* SceneTemplate = nullptr;
		for (int32 I = 0; Movie && I < Movie->GetSpawnableCount(); ++I) if (const AOKSceneActor* T = Cast<AOKSceneActor>(Movie->GetSpawnable(I).GetObjectTemplate())) SceneTemplate = T;
		AOKSceneActor* Standalone = nullptr;
		if (SceneTemplate && Scene->GetWorld())
		{
			FActorSpawnParameters Params;
			Params.ObjectFlags = RF_Transient;
			Params.Template = const_cast<AOKSceneActor*>(SceneTemplate);
			Standalone = Scene->GetWorld()->SpawnActor<AOKSceneActor>(Params);
		}
		if (Standalone) { Standalone->SequenceTime = 1.f; Standalone->ShowSequenceTime(); }
		const AKKPreviewActor* SeqA = Standalone ? Standalone->PreviewOf(A) : nullptr;
		Check(SceneTemplate && !SceneTemplate->Document && Standalone && Standalone->Document && SeqA && SeqA->Session.PartCount() > 0,
			FString::Printf(TEXT("the sequence's scene actor builds the scene from its JSON: %d objects, character A %d parts"), Standalone && Standalone->Document ? Standalone->Document->Objects.Num() : 0, SeqA ? SeqA->Session.PartCount() : 0));
		if (Standalone) Standalone->Destroy();
		Doc->Tracks.RemoveAll([SeqCam](const FOKSceneTrack& T) { return T.ObjectId == SeqCam; });
		Doc->ActiveCamera = -1;
		Doc->RemoveObject(SeqCam);
		Refresh();
		IFileManager::Get().Delete(*FPackageName::LongPackageNameToFilename(TEXT("/Game/OKStudio/Sequences/_selftest_sequence"), FPackageName::GetAssetPackageExtension()));
	}

	const FVector Before = Doc->WorldTransform(A).GetLocation();
	Doc->Reparent(A, -1);
	Refresh();
	Check(Doc->Find(A)->Parent == -1 && Doc->WorldTransform(A).GetLocation().Equals(Before, 0.01f), TEXT("reparent to root keeps the world position"));
	Check(!Doc->Reparent(Folder, Folder), TEXT("an object cannot become its own parent"));
	const int32 Copy = Doc->Duplicate(B);
	Refresh();
	Check(Copy > 0 && Actor->PreviewOf(Copy) && Actor->PreviewOf(Copy)->Session.PartCount() > 0, TEXT("duplicate shows a second character"));
	Doc->RemoveObject(Copy);
	Refresh();
	Check(!Actor->PreviewOf(Copy), TEXT("delete removes it from the preview"));
	Doc->Find(B)->bVisible = false;
	Doc->NotifyChanged();
	Refresh();
	Check(PB && PB->IsHidden(), TEXT("hidden object is not shown"));

	Step(TEXT("studio catalogue item and map"));
	// Studio catalogue item and scene map.
	const FOKStudioCatalog& Catalog = FOKStudioCatalog::Get();
	const FOKStudioCatalogItem* Fox = Catalog.Items.FindByPredicate([](const FOKStudioCatalogItem& I) { return I.Prefab == TEXT("p_acs_foxmask_stu"); });
	const int32 Studio = Doc->AddObject(EOKSceneObjectKind::Item, TEXT("Studio item"));
	if (Fox) { Doc->Find(Studio)->Item.Group = Fox->Group; Doc->Find(Studio)->Item.Category = Fox->Category; Doc->Find(Studio)->Item.No = Fox->No; }
	Doc->Map = 0;
	Doc->NotifyChanged();
	Refresh();
	const AKKPreviewActor* PStudio = Actor->PreviewOf(Studio);
	Check(Fox && PStudio && PStudio->Session.PartCount() > 0, FString::Printf(TEXT("studio catalogue item %s: %d parts %s"), Fox ? *Fox->Name : TEXT("?"), PStudio ? PStudio->Session.PartCount() : 0, *Catalog.Error));
	Check(Actor->MapPartCount() > 0, FString::Printf(TEXT("scene map 0: %d parts %s"), Actor->MapPartCount(), *Actor->MapError));
	Doc->Map = -1;
	Doc->RemoveObject(Studio);
	Refresh();

	Lines.Add(Failures ? FString::Printf(TEXT("RESULT FAIL (%d)"), Failures) : FString(TEXT("RESULT PASS")));
	for (const FString& L : Lines) UE_LOG(LogTemp, Display, TEXT("OK studio selftest: %s"), *L);
	FFileHelper::SaveStringArrayToFile(Lines, *(FPaths::ProjectSavedDir() / TEXT("OKStudio/editor_selftest.txt")));
	if (bExit) FPlatformMisc::RequestExit(false);
}

namespace
{
	FAutoConsoleCommand OpenCommand(TEXT("OKStudio.Open"),
		TEXT("OKStudio.Open [scene.okscene]: opens OK Studio in the editor (no file: a new scene)."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			// The toolkit manager owns the editor until its window closes.
			FOKStudioEditor::Open(Args.Num() ? Args[0] : FString());
		}));

	/** Makes every effect of effects.json whose Niagara system is missing, from its template emitter, and saves it. */
	/** The particles' colour of an effect system follows its User.Color (white by default): every emitter's
	 *  InitializeParticle.Color (and a Color module's Color, as beams have) is linked to it. */
	bool WireEffectColor(UNiagaraSystem& System, TArray<FString>& OutLines)
	{
		const FNiagaraVariable UserColor(FNiagaraTypeDefinition::GetColorDef(), TEXT("User.Color"));
		FNiagaraUserRedirectionParameterStore& Exposed = System.GetExposedParameters();
		if (Exposed.IndexOf(UserColor) == INDEX_NONE) Exposed.AddParameter(UserColor, true, true);
		Exposed.SetParameterValue(FLinearColor::White, UserColor);
		TSet<FNiagaraVariableBase> Known;
		Known.Add(UserColor);
		int32 Linked = 0;
		for (const FNiagaraEmitterHandle& Handle : System.GetEmitterHandles())
		{
			FVersionedNiagaraEmitterData* Data = Handle.GetEmitterData();
			UNiagaraScriptSource* Source = Data ? Cast<UNiagaraScriptSource>(Data->GraphSource) : nullptr;
			if (!Source || !Source->NodeGraph) continue;
			TArray<UNiagaraNodeFunctionCall*> Calls;
			for (UEdGraphNode* Node : Source->NodeGraph->Nodes) if (UNiagaraNodeFunctionCall* Call = Cast<UNiagaraNodeFunctionCall>(Node)) Calls.Add(Call);
			for (UNiagaraNodeFunctionCall* Call : Calls)
			{
				const FString Name = Call->GetFunctionName();
				if (Name != TEXT("InitializeParticle") && Name != TEXT("Color")) continue;
				TArray<FString> Pins;
				for (const UEdGraphPin* Pin : Call->Pins) if (Pin && Pin->Direction == EGPD_Input) Pins.Add(Pin->PinName.ToString() + TEXT("=") + Pin->DefaultValue);
				OutLines.Add(FString::Printf(TEXT("PINS %s / %s / %s: %s"), *System.GetName(), *Handle.GetName().ToString(), *Name, *FString::Join(Pins, TEXT(", "))));
				// InitializeParticle only writes the colour in "Direct Set" mode (the template's "Unset" leaves it white).
				for (UEdGraphPin* Pin : Call->Pins)
					if (Pin && Pin->Direction == EGPD_Input && Pin->PinName == TEXT("Color Mode") && Pin->DefaultValue == TEXT("NewEnumerator0"))
					{
						Call->GetSchema()->TrySetDefaultValue(*Pin, TEXT("NewEnumerator1"));
						OutLines.Add(FString::Printf(TEXT("MODE %s / %s: Color Mode set to Direct Set"), *System.GetName(), *Name));
					}
				const FNiagaraParameterHandle Input = FNiagaraParameterHandle::CreateAliasedModuleParameterHandle(FNiagaraParameterHandle::CreateModuleParameterHandle(TEXT("Color")), Call);
				UEdGraphPin& Override = FNiagaraStackGraphUtilities::GetOrCreateStackFunctionInputOverridePin(*Call, Input, FNiagaraTypeDefinition::GetColorDef(), FGuid(), FGuid());
				// Already linked (an earlier rewire): kept, so rewire can run again.
				if (Override.LinkedTo.Num() == 0) FNiagaraStackGraphUtilities::SetLinkedParameterValueForFunctionInput(Override, UserColor, Known, ENiagaraDefaultMode::FailIfPreviouslyNotSet);
				++Linked;
			}
			Source->NodeGraph->NotifyGraphChanged();
		}
		OutLines.Add(FString::Printf(TEXT("WIRE %s: %d colour inputs linked to User.Color"), *System.GetName(), Linked));
		return Linked > 0;
	}

	/** The sprite material of an effect (effects.json "material"), made when missing: translucent, unlit, the particle
	 *  colour with a soft round alpha (times the particle alpha), faded where it meets the scene (soft particles, 1 m),
	 *  so a dark colour darkens what is behind it (the templates' default sprite material is additive). */
	UMaterialInterface* EffectSpriteMaterial(const FString& ObjectPath, TArray<FString>& OutLines)
	{
		if (UMaterialInterface* Existing = LoadObject<UMaterialInterface>(nullptr, *ObjectPath)) return Existing;
		const FString PackageName = FPackageName::ObjectPathToPackageName(ObjectPath);
		UPackage* Package = CreatePackage(*PackageName);
		UMaterial* Mat = NewObject<UMaterial>(Package, *FPackageName::ObjectPathToObjectName(ObjectPath), RF_Public | RF_Standalone | RF_Transactional);
		Mat->MaterialDomain = MD_Surface;
		Mat->BlendMode = BLEND_Translucent;
		Mat->SetShadingModel(MSM_Unlit);
		Mat->bUsedWithNiagaraSprites = true;
		auto Add = [Mat](UMaterialExpression* E) { E->Material = Mat; Mat->GetExpressionCollection().AddExpression(E); return E; };
		auto* Color = Cast<UMaterialExpressionParticleColor>(Add(NewObject<UMaterialExpressionParticleColor>(Mat)));
		auto* Shape = Cast<UMaterialExpressionCustom>(Add(NewObject<UMaterialExpressionCustom>(Mat)));
		Shape->OutputType = CMOT_Float1;
		Shape->Code = TEXT("float r = saturate(1.0 - length(UV * 2.0 - 1.0));\nreturn C.a * r * r * saturate((SceneD - PixelD) / 100.0);");
		Shape->Inputs.Reset();
		auto Input = [Shape](const TCHAR* Name, UMaterialExpression* E, int32 Output = 0) { FCustomInput& I = Shape->Inputs.AddDefaulted_GetRef(); I.InputName = Name; I.Input.Expression = E; I.Input.OutputIndex = Output; };
		Input(TEXT("C"), Color, 5 /* RGBA */);
		Input(TEXT("UV"), Add(NewObject<UMaterialExpressionTextureCoordinate>(Mat)));
		Input(TEXT("SceneD"), Add(NewObject<UMaterialExpressionSceneDepth>(Mat)));
		Input(TEXT("PixelD"), Add(NewObject<UMaterialExpressionPixelDepth>(Mat)));
		if (UMaterialEditorOnlyData* Ed = Mat->GetEditorOnlyData())
		{
			Ed->EmissiveColor.Expression = Color;
			Ed->EmissiveColor.OutputIndex = 0;
			Ed->Opacity.Expression = Shape;
		}
		Mat->PreEditChange(nullptr);
		Mat->PostEditChange();
		FAssetRegistryModule::AssetCreated(Mat);
		Package->MarkPackageDirty();
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		const FString File = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());
		if (!UPackage::SavePackage(Package, Mat, *File, SaveArgs)) { OutLines.Add(TEXT("FAIL save ") + File); return nullptr; }
		OutLines.Add(TEXT("MADE ") + ObjectPath);
		return Mat;
	}

	/** The effect's sprite renderers draw with its material (effects.json "material"), when it names one. */
	bool ApplyEffectMaterial(UNiagaraSystem& System, const FOKStudioEffect& E, TArray<FString>& OutLines)
	{
		if (E.Material.IsEmpty()) return true;
		UMaterialInterface* Material = EffectSpriteMaterial(E.Material, OutLines);
		if (!Material) return false;
		int32 Set = 0;
		for (const FNiagaraEmitterHandle& Handle : System.GetEmitterHandles())
			if (FVersionedNiagaraEmitterData* Data = Handle.GetEmitterData())
				for (UNiagaraRendererProperties* R : Data->GetRenderers())
					if (UNiagaraSpriteRendererProperties* Sprite = Cast<UNiagaraSpriteRendererProperties>(R)) { Sprite->Material = Material; ++Set; }
		OutLines.Add(FString::Printf(TEXT("%s %s: material %s on %d sprite renderers"), Set ? TEXT("SET") : TEXT("FAIL"), *E.Id, *E.Material, Set));
		return Set > 0;
	}

	/** The effect's constants (effects.json "constants") on every script's rapid iteration parameter of that module
	 *  input ("Constants.<emitter>.<Module>.<Input>"). False when one names no input of the system. */
	bool ApplyEffectConstants(UNiagaraSystem& System, const FOKStudioEffect& E, TArray<FString>& OutLines)
	{
		bool bAll = true;
		for (const TPair<FString, TArray<float>>& C : E.Constants)
		{
			int32 Set = 0;
			for (const FNiagaraEmitterHandle& Handle : System.GetEmitterHandles())
			{
				FVersionedNiagaraEmitterData* Data = Handle.GetEmitterData();
				if (!Data) continue;
				const FString Name = FString::Printf(TEXT("Constants.%s.%s"), *Handle.GetName().ToString(), *C.Key);
				TArray<UNiagaraScript*> Scripts;
				Data->GetScripts(Scripts, false);
				for (UNiagaraScript* Script : Scripts)
				{
					if (!Script) continue;
					for (const FNiagaraVariableWithOffset& P : Script->RapidIterationParameters.ReadParameterVariables())
					{
						if (P.GetName().ToString() != Name) continue;
						const bool bInt = P.GetType() == FNiagaraTypeDefinition::GetIntDef();
						if (P.GetType().GetSize() != C.Value.Num() * 4) { OutLines.Add(FString::Printf(TEXT("FAIL %s: %s takes %d bytes"), *E.Id, *Name, P.GetType().GetSize())); bAll = false; continue; }
						TArray<uint8> Bytes;
						Bytes.SetNumUninitialized(C.Value.Num() * 4);
						for (int32 I = 0; I < C.Value.Num(); ++I)
						{
							if (bInt) { const int32 V = FMath::RoundToInt(C.Value[I]); FMemory::Memcpy(&Bytes[I * 4], &V, 4); }
							else FMemory::Memcpy(&Bytes[I * 4], &C.Value[I], 4);
						}
						Script->RapidIterationParameters.SetParameterData(Bytes.GetData(), P, true);
						++Set;
					}
				}
			}
			OutLines.Add(FString::Printf(TEXT("%s %s: %s in %d scripts"), Set ? TEXT("SET") : TEXT("FAIL"), *E.Id, *C.Key, Set));
			bAll &= Set > 0;
		}
		return bAll;
	}

	int32 MakeEffects(TArray<FString>& OutLines, bool bRewire = false)
	{
		int32 Failures = 0;
		for (const FOKStudioEffect& E : FOKStudioCatalog::Get().Effects)
		{
			const FString PackageName = FPackageName::ObjectPathToPackageName(E.Asset);
			const FString AssetName = FPackageName::ObjectPathToObjectName(E.Asset);
			if (FPackageName::DoesPackageExist(PackageName))
			{
				if (!bRewire) { OutLines.Add(TEXT("KEEP ") + E.Asset); continue; }
				UNiagaraSystem* Existing = LoadObject<UNiagaraSystem>(nullptr, *E.Asset);
				if (!Existing || !WireEffectColor(*Existing, OutLines) || !ApplyEffectConstants(*Existing, E, OutLines) || !ApplyEffectMaterial(*Existing, E, OutLines)) { OutLines.Add(TEXT("FAIL rewire ") + E.Asset); ++Failures; continue; }
				Existing->RequestCompile(false);
				Existing->WaitForCompilationComplete();
				Existing->MarkPackageDirty();
				FSavePackageArgs RewireArgs;
				RewireArgs.TopLevelFlags = RF_Public | RF_Standalone;
				const FString RewireFile = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());
				if (UPackage::SavePackage(Existing->GetOutermost(), Existing, *RewireFile, RewireArgs)) OutLines.Add(TEXT("REWIRED ") + E.Asset);
				else { OutLines.Add(TEXT("FAIL save ") + RewireFile); ++Failures; }
				continue;
			}
			UNiagaraEmitter* Template = LoadObject<UNiagaraEmitter>(nullptr, *E.Template);
			if (!Template) { OutLines.Add(TEXT("FAIL no template ") + E.Template); ++Failures; continue; }
			UPackage* Package = CreatePackage(*PackageName);
			UNiagaraSystem* System = NewObject<UNiagaraSystem>(Package, *AssetName, RF_Public | RF_Standalone | RF_Transactional);
			UNiagaraSystemFactoryNew::InitializeSystem(System, true);
			FNiagaraEditorUtilities::AddEmitterToSystem(*System, *Template, Template->GetExposedVersion().VersionGuid);
			if (!WireEffectColor(*System, OutLines) || !ApplyEffectConstants(*System, E, OutLines) || !ApplyEffectMaterial(*System, E, OutLines)) { OutLines.Add(TEXT("FAIL set up ") + E.Asset); ++Failures; continue; }
			System->RequestCompile(false);
			System->WaitForCompilationComplete();
			FAssetRegistryModule::AssetCreated(System);
			Package->MarkPackageDirty();
			FSavePackageArgs SaveArgs;
			SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
			const FString File = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());
			if (UPackage::SavePackage(Package, System, *File, SaveArgs)) OutLines.Add(FString::Printf(TEXT("MADE %s from %s"), *E.Asset, *E.Template));
			else { OutLines.Add(TEXT("FAIL save ") + File); ++Failures; }
		}
		return Failures;
	}

	FAutoConsoleCommand DumpEffectInputsCommand(TEXT("OKStudio.DumpEffectInputs"),
		TEXT("OKStudio.DumpEffectInputs <effect id> [exit]: every module input pin and rapid iteration constant of that effect's Niagara system."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			const FOKStudioEffect* E = Args.Num() ? FOKStudioCatalog::Get().FindEffect(Args[0]) : nullptr;
			const UNiagaraSystem* System = E ? LoadObject<UNiagaraSystem>(nullptr, *E->Asset) : nullptr;
			if (System)
				for (const FNiagaraEmitterHandle& Handle : System->GetEmitterHandles())
				{
					FVersionedNiagaraEmitterData* Data = Handle.GetEmitterData();
					const UNiagaraScriptSource* Source = Data ? Cast<UNiagaraScriptSource>(Data->GraphSource) : nullptr;
					if (Source && Source->NodeGraph)
						for (const UEdGraphNode* Node : Source->NodeGraph->Nodes)
							if (const UNiagaraNodeFunctionCall* Call = Cast<UNiagaraNodeFunctionCall>(Node))
								for (const UEdGraphPin* Pin : Call->Pins)
									if (Pin && Pin->Direction == EGPD_Input)
										UE_LOG(LogTemp, Display, TEXT("OK studio effect input: %s / %s / %s = %s (links %d)"), *Handle.GetName().ToString(), *Call->GetFunctionName(), *Pin->PinName.ToString(), *Pin->DefaultValue, Pin->LinkedTo.Num());
					if (Data)
					{
						for (const UNiagaraRendererProperties* R : Data->GetRenderers())
							if (const UNiagaraSpriteRendererProperties* Sprite = Cast<UNiagaraSpriteRendererProperties>(R))
								UE_LOG(LogTemp, Display, TEXT("OK studio effect renderer: %s sprite material %s blend %d"), *Handle.GetName().ToString(),
									Sprite->Material ? *Sprite->Material->GetPathName() : TEXT("none"), Sprite->Material ? int32(Sprite->Material->GetBlendMode()) : -1);
						TArray<UNiagaraScript*> Scripts;
						Data->GetScripts(Scripts, false);
						for (UNiagaraScript* Script : Scripts)
							if (Script)
								for (const FNiagaraVariableWithOffset& P : Script->RapidIterationParameters.ReadParameterVariables())
								{
									const uint8* Raw = Script->RapidIterationParameters.GetParameterData(P.Offset);
									FString Value;
									const int32 Size = P.GetType().GetSize();
									if (Raw && P.GetType() == FNiagaraTypeDefinition::GetFloatDef()) Value = FString::SanitizeFloat(*(const float*)Raw);
									else if (Raw && Size % 4 == 0 && Size <= 16) for (int32 I = 0; I < Size / 4; ++I) Value += FString::Printf(TEXT("%s%g"), I ? TEXT(",") : TEXT(""), ((const float*)Raw)[I]);
									UE_LOG(LogTemp, Display, TEXT("OK studio effect constant: %s / %s / %s %s = %s"), *Handle.GetName().ToString(), *Script->GetName(), *P.GetName().ToString(), *P.GetType().GetName(), *Value);
								}
					}
				}
			else UE_LOG(LogTemp, Display, TEXT("OK studio effect input: no system for %s"), Args.Num() ? *Args[0] : TEXT("(none)"));
			if (Args.Contains(TEXT("exit"))) FPlatformMisc::RequestExit(false);
		}));

	FAutoConsoleCommand DumpEffectsCommand(TEXT("OKStudio.DumpEffects"),
		TEXT("OKStudio.DumpEffects [exit]: lists the emitters, modules and renderers of the OK effect library's Niagara systems."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			for (const FOKStudioEffect& E : FOKStudioCatalog::Get().Effects)
			{
				const UNiagaraSystem* System = LoadObject<UNiagaraSystem>(nullptr, *E.Asset);
				if (!System) { UE_LOG(LogTemp, Display, TEXT("OK studio effect dump: %s missing"), *E.Id); continue; }
				for (const FNiagaraEmitterHandle& Handle : System->GetEmitterHandles())
				{
					const FVersionedNiagaraEmitterData* Data = Handle.GetEmitterData();
					const UNiagaraScriptSource* Source = Data ? Cast<UNiagaraScriptSource>(Data->GraphSource) : nullptr;
					TArray<FString> Modules, Renderers;
					if (Source && Source->NodeGraph)
						for (const UEdGraphNode* Node : Source->NodeGraph->Nodes)
							if (const UNiagaraNodeFunctionCall* Call = Cast<UNiagaraNodeFunctionCall>(Node)) Modules.Add(Call->GetFunctionName());
					if (Data) for (const UNiagaraRendererProperties* R : Data->GetRenderers()) if (R) Renderers.Add(R->GetClass()->GetName());
					UE_LOG(LogTemp, Display, TEXT("OK studio effect dump: %s / %s: modules [%s] renderers [%s]"), *E.Id, *Handle.GetName().ToString(), *FString::Join(Modules, TEXT(", ")), *FString::Join(Renderers, TEXT(", ")));
				}
				for (const FNiagaraVariableWithOffset& P : System->GetExposedParameters().ReadParameterVariables())
					UE_LOG(LogTemp, Display, TEXT("OK studio effect dump: %s user %s %s"), *E.Id, *P.GetName().ToString(), *P.GetType().GetName());
			}
			if (Args.Contains(TEXT("exit"))) FPlatformMisc::RequestExit(false);
		}));

	FAutoConsoleCommand MakeEffectsCommand(TEXT("OKStudio.MakeEffects"),
		TEXT("OKStudio.MakeEffects [rewire] [exit]: makes the missing Niagara systems of the OK effect library (effects.json) from their template emitters; rewire links the existing ones' particle colour to User.Color."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			TArray<FString> Lines;
			const int32 Failures = MakeEffects(Lines, Args.Contains(TEXT("rewire")));
			for (const FString& L : Lines) UE_LOG(LogTemp, Display, TEXT("OK studio effects: %s"), *L);
			UE_LOG(LogTemp, Display, TEXT("OK studio effects: RESULT %s (%d effects, %d failed)"), Failures ? TEXT("FAIL") : TEXT("PASS"), FOKStudioCatalog::Get().Effects.Num(), Failures);
			if (Args.Contains(TEXT("exit"))) FPlatformMisc::RequestExit(false);
		}));

	FAutoConsoleCommand RenderMovieTestCommand(TEXT("OKStudio.RenderMovieTest"),
		TEXT("OKStudio.RenderMovieTest [exit]: a 1 s scene (a character, a moving camera) rendered with Movie Render Queue into an mp4."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			const bool bExit = Args.Contains(TEXT("exit"));
			auto Finish = [bExit](bool bOk, const FString& What)
			{
				UE_LOG(LogTemp, Display, TEXT("OK studio movie test: %s %s"), bOk ? TEXT("RESULT PASS") : TEXT("RESULT FAIL"), *What);
				if (bExit) FPlatformMisc::RequestExit(false);
			};
			// Movie Render Queue is shelved until the project turns on the GPU skin cache (Docs/OK_STUDIO_PLAN.md R3).
			if (!FOKStudioMovie::HasGpuSkinCache())
			{
				UE_LOG(LogTemp, Display, TEXT("OK studio movie test: RESULT SKIP the GPU skin cache is off (r.SkinCache.CompileShaders / r.SkinCache.Mode)"));
				if (bExit) FPlatformMisc::RequestExit(false);
				return;
			}
			UOKSceneDocument* Doc = NewObject<UOKSceneDocument>(GetTransientPackage());
			Doc->Duration = 1.f;
			const int32 Light = Doc->AddObject(EOKSceneObjectKind::Light, TEXT("Key light"));
			Doc->Find(Light)->Transform = FTransform(FRotator(-40.f, 30.f, 0.f), FVector(0.f, -200.f, 250.f));
			Doc->Find(Light)->Light.Type = EOKSceneLightType::Directional;
			const int32 A = Doc->AddObject(EOKSceneObjectKind::Character, TEXT("A"));
			Doc->Find(A)->Character.Card = TEXT("Content/CharacterCards/CrowdAPNG/C000.png");
			const int32 Cam = Doc->AddObject(EOKSceneObjectKind::Camera, TEXT("Camera"));
			Doc->Find(Cam)->Transform = FTransform(FRotator(-5.f, -90.f, 0.f), FVector(0.f, 260.f, 120.f));
			Doc->SetKey(Cam, 0.f);
			Doc->Find(Cam)->Transform = FTransform(FRotator(-5.f, -100.f, 0.f), FVector(60.f, 240.f, 120.f));
			Doc->SetKey(Cam, 1.f);
			Doc->ActiveCamera = Cam;
			FString Report, Error;
			ULevelSequence* Sequence = FOKStudioSequence::Export(Doc, TEXT("_movie_test"), Report, Error);
			if (!Sequence) { Finish(false, Error); return; }
			for (int32 I = 0; I < Sequence->GetMovieScene()->GetSpawnableCount(); ++I)
			{
				const FMovieSceneSpawnable& Spawnable = Sequence->GetMovieScene()->GetSpawnable(I);
				const UObject* Template = Spawnable.GetObjectTemplate();
				const AOKSceneActor* SceneTemplate = Cast<AOKSceneActor>(Template);
				UE_LOG(LogTemp, Display, TEXT("OK studio movie test: spawnable %s template %s scene %d chars, ownership %d"), *Spawnable.GetName(), Template ? *Template->GetClass()->GetName() : TEXT("none"),
					SceneTemplate ? SceneTemplate->SceneJson.Len() : -1, int32(Spawnable.GetSpawnOwnership()));
			}
			if (!FOKStudioMovie::Render(Sequence, 640, 360, 4, [Finish](const FString& Movie, const FString& MovieError)
				{
					const int64 Size = Movie.IsEmpty() ? 0 : IFileManager::Get().FileSize(*Movie);
					// Not black: the middle frame has lit pixels (the character under the key light).
					int32 Lit = 0, Blown = 0;
					const FString Probe = FPaths::ProjectSavedDir() / TEXT("OKStudio/movie_test_frame.png");
					if (Size > 0)
					{
						const FString Args = FString::Printf(TEXT("-y -loglevel error -i \"%s\" -vf select=eq(n\\,15) -vframes 1 \"%s\""), *Movie, *Probe);
						int32 Code = -1;
						FPlatformProcess::ExecProcess(*UOKStudioGame::FfmpegPath(), *Args, &Code, nullptr, nullptr);
						TArray64<uint8> Png;
						FImage Image;
						if (FFileHelper::LoadFileToArray(Png, *Probe) && FImageUtils::DecompressImage(Png.GetData(), Png.Num(), Image))
						{
							Image.ChangeFormat(ERawImageFormat::BGRA8, EGammaSpace::sRGB);
							const TArrayView64<FColor> Pixels = Image.AsBGRA8();
							for (const FColor& C : Pixels) { Lit += (C.R + C.G + C.B) > 60 ? 1 : 0; Blown += (C.R > 250 && C.G > 250 && C.B > 250) ? 1 : 0; }
						}
					}
					IFileManager::Get().Delete(*FPackageName::LongPackageNameToFilename(TEXT("/Game/OKStudio/Sequences/_movie_test"), FPackageName::GetAssetPackageExtension()));
					// Lit (the character is there) but not washed out (fixed exposure).
					Finish(Size > 1000 && Lit > 500 && Blown * 4 < Lit, FString::Printf(TEXT("%s (%lld bytes, frame 15: %d lit, %d blown-out pixels) %s"), *Movie, Size, Lit, Blown, *MovieError));
				}, Error)) Finish(false, Error);
		}));

	FAutoConsoleCommand SelfTestCommand(TEXT("OKStudio.EditorSelfTest"),
		TEXT("OKStudio.EditorSelfTest [exit]: builds and checks a scene in the last opened OK Studio."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			const bool bExit = Args.Contains(TEXT("exit"));
			TSharedRef<int32> Frames = MakeShared<int32>(0);
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([bExit, Frames](float)
			{
				if (++*Frames < 30) return true;
				if (FOKStudioEditor* Editor = FOKStudioEditor::LastOpened) Editor->RunSelfTest(bExit);
				else { UE_LOG(LogTemp, Error, TEXT("OK studio selftest: no OK Studio open")); if (bExit) FPlatformMisc::RequestExit(false); }
				return false;
			}), 0.1f);
		}));
}

#undef LOCTEXT_NAMESPACE
