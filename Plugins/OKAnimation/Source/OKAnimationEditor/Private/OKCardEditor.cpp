#include "OKCardEditor.h"
#include "AdvancedPreviewScene.h"
#include "AssetEditorModeManager.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Async/Async.h"
#include "Camera/CameraComponent.h"
#include "EditorViewportClient.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "IDetailsView.h"
#include "KKCardReader.h"
#include "KKCardWorkspace.h"
#include "KKCardWriter.h"
#include "KKPreviewActor.h"
#include "KKRustExtract.h"
#include "KKShapeRows.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "OKAnimationAssets.h"
#include "PropertyEditorModule.h"
#include "SEditorViewport.h"
#include "Containers/Ticker.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "OKCardEditor"

FOKCardEditor* FOKCardEditor::LastOpened = nullptr;
const FName FOKCardEditor::ViewportTab(TEXT("OKCardEditor_Viewport"));
const FName FOKCardEditor::DetailsTab(TEXT("OKCardEditor_Details"));

class SOKCardViewport : public SEditorViewport
{
public:
	SLATE_BEGIN_ARGS(SOKCardViewport) {}
	SLATE_END_ARGS()

	void Construct(const FArguments&, TSharedPtr<FAdvancedPreviewScene> InScene)
	{
		Scene = InScene;
		SEditorViewport::Construct(SEditorViewport::FArguments());
	}

protected:
	virtual TSharedRef<FEditorViewportClient> MakeEditorViewportClient() override
	{
		Client = MakeShareable(new FEditorViewportClient(nullptr, Scene.Get(), SharedThis(this)));
		Client->SetRealtime(true);
		// A standing character about 1.6 m tall at the origin, seen from the front.
		Client->SetViewLocation(FVector(0.f, 260.f, 95.f));
		Client->SetViewRotation(FRotator(-5.f, -90.f, 0.f));
		Client->EngineShowFlags.SetGrid(true);
		if (FAssetEditorModeManager* Modes = static_cast<FAssetEditorModeManager*>(Client->GetModeTools())) Modes->SetPreviewScene(Scene.Get());
		return Client.ToSharedRef();
	}

private:
	TSharedPtr<FAdvancedPreviewScene> Scene;
	TSharedPtr<FEditorViewportClient> Client;
};

namespace
{
	TArray<FOKCardShapeValue> ShapeValues(const TArray<float>& Values, bool bBody)
	{
		TMap<int32, FString> Labels;
		for (const FKKShapeRow& Row : KKShapeRows()) if (Row.bBody == bBody) Labels.Add(Row.Index, Row.Label);
		TArray<FOKCardShapeValue> Out;
		for (int32 I = 0; I < Values.Num(); ++I)
		{
			FOKCardShapeValue& V = Out.AddDefaulted_GetRef();
			V.Index = I;
			V.Value = Values[I];
			const FString* Label = Labels.Find(I);
			V.Label = Label ? *Label : FString::Printf(TEXT("%s %d"), bBody ? TEXT("Body") : TEXT("Face"), I);
		}
		return Out;
	}

	TArray<float> Values(const TArray<FOKCardShapeValue>& Shapes)
	{
		TArray<float> Out;
		Out.SetNumZeroed(Shapes.Num());
		for (const FOKCardShapeValue& S : Shapes) if (Out.IsValidIndex(S.Index)) Out[S.Index] = S.Value;
		return Out;
	}

	bool PngHasChunk(const FString& Path, const ANSICHAR* Type)
	{
		TArray<uint8> Bytes;
		if (!FFileHelper::LoadFileToArray(Bytes, *Path) || Bytes.Num() < 8) return false;
		for (int64 Pos = 8; Pos + 12 <= Bytes.Num();)
		{
			const uint32 Len = (uint32(Bytes[Pos]) << 24) | (uint32(Bytes[Pos + 1]) << 16) | (uint32(Bytes[Pos + 2]) << 8) | uint32(Bytes[Pos + 3]);
			if (FMemory::Memcmp(&Bytes[Pos + 4], Type, 4) == 0) return true;
			if (FMemory::Memcmp(&Bytes[Pos + 4], "IEND", 4) == 0) return false;
			Pos += 12 + int64(Len);
		}
		return false;
	}
}

FText FOKCardEditor::GetBaseToolkitName() const { return LOCTEXT("ToolkitName", "OK Card Editor"); }

TSharedPtr<FOKCardEditor> FOKCardEditor::Open(const FString& Card)
{
	TSharedRef<FOKCardEditor> Editor = MakeShared<FOKCardEditor>();
	Editor->Init();
	FString Error;
	if (!Editor->Load(FPaths::ConvertRelativePathToFull(Card), false, Error))
	{
		UE_LOG(LogTemp, Error, TEXT("OK card editor: %s: %s"), *Card, *Error);
		Editor->SetStatus(Error);
	}
	return Editor;
}

void FOKCardEditor::Init()
{
	State = NewObject<UOKCardEditorState>(GetTransientPackage(), NAME_None, RF_Transient | RF_Transactional);
	Scene = MakeShared<FAdvancedPreviewScene>(FPreviewScene::ConstructionValues());

	FPropertyEditorModule& PropertyEditor = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	FDetailsViewArgs Args;
	Args.bHideSelectionTip = true;
	Args.NameAreaSettings = FDetailsViewArgs::HideNameArea;
	Details = PropertyEditor.CreateDetailView(Args);
	Details->SetObject(State);
	ObjectChanged = FCoreUObjectDelegates::OnObjectPropertyChanged.AddLambda([this](UObject* Object, FPropertyChangedEvent&)
	{
		if (Object == State) bShapesDirty = true;
	});

	const TSharedRef<FTabManager::FLayout> Layout = FTabManager::NewLayout("OKCardEditor_Layout_v1")
		->AddArea(FTabManager::NewPrimaryArea()->SetOrientation(Orient_Horizontal)
			->Split(FTabManager::NewStack()->SetSizeCoefficient(0.65f)->SetHideTabWell(true)->AddTab(ViewportTab, ETabState::OpenedTab))
			->Split(FTabManager::NewStack()->SetSizeCoefficient(0.35f)->AddTab(DetailsTab, ETabState::OpenedTab)));
	InitAssetEditor(EToolkitMode::Standalone, TSharedPtr<IToolkitHost>(), TEXT("OKCardEditorApp"), Layout, true, true, State);

	TSharedPtr<FExtender> Extender = MakeShared<FExtender>();
	Extender->AddToolBarExtension("Asset", EExtensionHook::After, GetToolkitCommands(), FToolBarExtensionDelegate::CreateRaw(this, &FOKCardEditor::FillToolbar));
	AddToolbarExtender(Extender);
	RegenerateMenusAndToolbars();
	LastOpened = this;
}

FOKCardEditor::~FOKCardEditor()
{
	if (LastOpened == this) LastOpened = nullptr;
	FCoreUObjectDelegates::OnObjectPropertyChanged.Remove(ObjectChanged);
	if (Preview.IsValid()) Preview->Destroy();
}

void FOKCardEditor::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(State);
}

void FOKCardEditor::RegisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
	WorkspaceMenuCategory = InTabManager->AddLocalWorkspaceMenuCategory(LOCTEXT("Workspace", "OK Card Editor"));
	FAssetEditorToolkit::RegisterTabSpawners(InTabManager);
	InTabManager->RegisterTabSpawner(ViewportTab, FOnSpawnTab::CreateLambda([this](const FSpawnTabArgs&)
	{
		SAssignNew(Viewport, SOKCardViewport, Scene);
		return SNew(SDockTab).Label(LOCTEXT("Viewport", "Preview"))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().FillHeight(1.f)[Viewport.ToSharedRef()]
			+ SVerticalBox::Slot().AutoHeight().Padding(4.f)[SAssignNew(Status, STextBlock)]
		];
	})).SetDisplayName(LOCTEXT("Viewport", "Preview")).SetGroup(WorkspaceMenuCategory.ToSharedRef());
	InTabManager->RegisterTabSpawner(DetailsTab, FOnSpawnTab::CreateLambda([this](const FSpawnTabArgs&)
	{
		return SNew(SDockTab).Label(LOCTEXT("Details", "Card"))[Details.ToSharedRef()];
	})).SetDisplayName(LOCTEXT("Details", "Card")).SetGroup(WorkspaceMenuCategory.ToSharedRef());
}

void FOKCardEditor::UnregisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
	FAssetEditorToolkit::UnregisterTabSpawners(InTabManager);
	InTabManager->UnregisterTabSpawner(ViewportTab);
	InTabManager->UnregisterTabSpawner(DetailsTab);
}

void FOKCardEditor::FillToolbar(FToolBarBuilder& ToolbarBuilder)
{
	auto Report = [this](bool bOk, const FString& Done, const FString& Error) { SetStatus(bOk ? Done : Error); };
	ToolbarBuilder.BeginSection("OKCard");
	ToolbarBuilder.AddToolBarButton(FUIAction(FExecuteAction::CreateLambda([this, Report]
	{
		FString Error;
		Report(SaveTo(State->Card, Error), TEXT("Saved as an OK card: ") + State->Card, Error);
	})), NAME_None, LOCTEXT("Save", "Save OK Card"), LOCTEXT("SaveTip", "Write the edited card over the opened card as an OK card (OK base library + the card's own pack)"));
	ToolbarBuilder.AddToolBarButton(FUIAction(FExecuteAction::CreateLambda([this, Report]
	{
		FString Error;
		const FString Out = UKKCardWorkspace::NewCardPath(FPaths::GetPath(State->Card), State->Card);
		Report(SaveTo(Out, Error), TEXT("Saved as a new OK card: ") + Out, Error);
	})), NAME_None, LOCTEXT("SaveAs", "Save As New OK Card"), LOCTEXT("SaveAsTip", "Write the edited card as a new OK card next to the opened one"));
	ToolbarBuilder.AddToolBarButton(FUIAction(FExecuteAction::CreateLambda([this, Report]
	{
		FString Error;
		Report(WearClothes(State->WearClothes.FilePath, Error), TEXT("Wearing ") + State->WearClothes.FilePath, Error);
	})), NAME_None, LOCTEXT("Wear", "Wear Clothes"), LOCTEXT("WearTip", "Wear the chosen .okclothes as the current outfit (saved with the card)"));
	ToolbarBuilder.AddToolBarButton(FUIAction(FExecuteAction::CreateLambda([this, Report]
	{
		FString Error;
		const FString Out = ExportClothes(Error);
		Report(!Out.IsEmpty(), TEXT("Exported ") + Out, Error);
	})), NAME_None, LOCTEXT("Export", "Export Clothes"), LOCTEXT("ExportTip", "Export the current outfit as .okclothes"));
	ToolbarBuilder.AddToolBarButton(FUIAction(FExecuteAction::CreateLambda([this, Report]
	{
		if (bPlaying) { StopAnimation(); SetStatus(TEXT("Animation stopped")); return; }
		FString Error;
		Report(PlayAnimation(Error), TEXT("Playing animation"), Error);
	})), NAME_None, LOCTEXT("Play", "Play / Stop Animation"), LOCTEXT("PlayTip", "Play the chosen OK animation on the card"));
	ToolbarBuilder.AddToolBarButton(FUIAction(FExecuteAction::CreateLambda([this, Report]
	{
		FString Error;
		Report(Load(State->Card, false, Error), TEXT("Reloaded ") + State->Card, Error);
	})), NAME_None, LOCTEXT("Reload", "Reload"), LOCTEXT("ReloadTip", "Discard unsaved edits and reload the card"));
	ToolbarBuilder.EndSection();
}

void FOKCardEditor::SetStatus(const FString& Text)
{
	if (Status) Status->SetText(FText::FromString(Text));
	UE_LOG(LogTemp, Display, TEXT("OK card editor: %s"), *Text);
}

FString FOKCardEditor::WorkDirectory() const
{
	const FString Dir = FPaths::ProjectSavedDir() / TEXT("OKCard");
	IFileManager::Get().MakeDirectory(*Dir, true);
	return Dir;
}

bool FOKCardEditor::Load(const FString& Card, bool bKeepShapes, FString& OutError)
{
	FKKCharacterFile File;
	if (!UKKCardReader::LoadFromFile(Card, File, OutError)) return false;
	if (State->Card.IsEmpty() || !bKeepShapes) State->Card = Card;
	Source = Card;
	State->Name = FString::Printf(TEXT("%s %s"), *File.LastName, *File.FirstName);
	if (!bKeepShapes || State->Body.Num() != File.Body.ShapeValueBody.Num() || State->Face.Num() != File.Face.ShapeValueFace.Num())
	{
		State->Body = ShapeValues(File.Body.ShapeValueBody, true);
		State->Face = ShapeValues(File.Face.ShapeValueFace, false);
	}
	State->Outfit = FMath::Clamp(State->Outfit, 0, FMath::Max(File.Outfits.Num() - 1, 0));
	if (Details) Details->ForceRefresh();
	return OpenPreview(OutError);
}

bool FOKCardEditor::OpenPreview(FString& OutError)
{
	const bool bWasPlaying = bPlaying;
	StopAnimation();
	if (Preview.IsValid()) Preview->Destroy();
	UWorld* World = Scene ? Scene->GetWorld() : nullptr;
	if (!World) { OutError = TEXT("no preview world"); return false; }
	// An OK card opens from the OK base library and its pack; an original card from the library plus its zipmods.
	FOkSession Session;
	if (!Session.Open(FString(), Source, State->Outfit, OutError)) return false;
	FActorSpawnParameters Params;
	Params.ObjectFlags = RF_Transient;
	AKKPreviewActor* Actor = World->SpawnActor<AKKPreviewActor>(FVector::ZeroVector, FRotator::ZeroRotator, Params);
	if (!Actor) { OutError = TEXT("preview actor not spawned"); return false; }
	Actor->SetActorTickEnabled(false);
	Actor->Session = MoveTemp(Session);
	if (!Actor->LoadSession(OutError)) { Actor->Destroy(); return false; }
	TArray<UCameraComponent*> Cameras;
	Actor->GetComponents(Cameras);
	for (UCameraComponent* Cam : Cameras) Cam->DestroyComponent();
	Preview = Actor;
	LoadedOutfit = State->Outfit;
	ApplyShapes();
	if (bWasPlaying) { FString Ignored; PlayAnimation(Ignored); }
	SetStatus(FString::Printf(TEXT("%s  outfit %d  (%d parts)%s"), *FPaths::GetCleanFilename(Source), State->Outfit, Actor->Session.PartCount(),
		PngHasChunk(Source, "okLb") ? TEXT("  OK card") : TEXT("  original card")));
	return true;
}

void FOKCardEditor::ApplyShapes()
{
	bShapesDirty = false;
	if (Preview.IsValid()) Preview->ApplyShapes(Values(State->Face), Values(State->Body));
}

void FOKCardEditor::Tick(float DeltaTime)
{
	if (!State) return;
	if (bShapesDirty)
	{
		if (State->Outfit != LoadedOutfit) { FString Error; if (!OpenPreview(Error)) SetStatus(Error); }
		else ApplyShapes();
	}
	if (bPlaying && Preview.IsValid() && Preview->OkPlayer)
	{
		const float Length = State->Animation ? FMath::Max(State->Animation->GetPlayLength(), 0.01f) : 1.f;
		AnimTime = FMath::Fmod(AnimTime + DeltaTime, Length);
		Preview->AnimPhase = AnimTime;
		Preview->ApplyPoseFrame(0.f, false);
	}
}

bool FOKCardEditor::SaveTo(const FString& Out, FString& OutError)
{
	FKKCharacterFile File;
	if (!UKKCardReader::LoadFromFile(Source, File, OutError)) return false;
	File.Body.ShapeValueBody = Values(State->Body);
	File.Face.ShapeValueFace = Values(State->Face);
	if (!UKKCardWriter::SaveWithEdits(File, Out, OutError)) return false;
	FString Summary;
	if (!FOkSession::MakeOkCard(Out, Out, Summary)) { OutError = TEXT("OK card conversion failed: ") + Summary; return false; }
	State->Card = Out;
	return Load(Out, false, OutError);
}

bool FOKCardEditor::WearClothes(const FString& OkClothes, FString& OutError)
{
	if (OkClothes.IsEmpty() || !FPaths::FileExists(OkClothes)) { OutError = TEXT("choose an .okclothes file first"); return false; }
	const FString Worn = WorkDirectory() / FPaths::GetBaseFilename(State->Card) + TEXT(".worn.png");
	FString Summary;
	if (!FOkSession::ClothesWear(Source, OkClothes, State->Outfit, Worn, Summary)) { OutError = Summary; return false; }
	// The worn copy is what Save writes; the edited parameters stay.
	const FString Card = State->Card;
	const bool bLoaded = Load(Worn, true, OutError);
	State->Card = Card;
	return bLoaded;
}

FString FOKCardEditor::ExportClothes(FString& OutError)
{
	const FString Out = WorkDirectory() / FString::Printf(TEXT("%s.outfit%d.okclothes"), *FPaths::GetBaseFilename(State->Card), State->Outfit);
	FString Summary;
	if (!FOkSession::ClothesExport(Source, State->Outfit, Out, Summary)) { OutError = Summary; return FString(); }
	return Out;
}

bool FOKCardEditor::PlayAnimation(FString& OutError)
{
	if (!State->Animation) { OutError = TEXT("choose an OK animation first"); return false; }
	if (!Preview.IsValid()) { OutError = TEXT("no preview"); return false; }
	const TSharedPtr<FOkAnimPlayer> Player = State->Animation->CreatePlayer(OutError);
	if (!Player || !Preview->SetOkAnimation(Player, false)) { if (OutError.IsEmpty()) OutError = TEXT("animation not playable on this card"); return false; }
	Preview->PoseMode = 3;
	AnimTime = 0.f;
	bPlaying = true;
	return true;
}

void FOKCardEditor::StopAnimation()
{
	bPlaying = false;
	if (Preview.IsValid() && Preview->OkPlayer) Preview->SetPoseMode(0);
}

bool FOKCardEditor::HasPreview() const { return Preview.IsValid() && Viewport.IsValid(); }
int32 FOKCardEditor::PreviewParts() const { return Preview.IsValid() ? Preview->Session.PartCount() : 0; }
TArray<float> FOKCardEditor::PreviewShape(bool bBody) const
{
	return Preview.IsValid() ? (bBody ? Preview->Session.ShapeBody() : Preview->Session.ShapeFace()) : TArray<float>();
}

void FOKCardEditor::RunSelfTest(bool bExit)
{
	TArray<FString> Lines;
	int32 Failures = 0;
	auto Check = [&](bool bOk, const FString& What)
	{
		Lines.Add(FString::Printf(TEXT("%s  %s"), bOk ? TEXT("PASS") : TEXT("FAIL"), *What));
		if (!bOk) ++Failures;
	};
	const FString Original = State->Card;
	const int32 Parts = PreviewParts();
	Check(Parts > 0, FString::Printf(TEXT("preview %s: %d parts"), *FPaths::GetCleanFilename(Original), Parts));
	Check(PngHasChunk(Original, "okLb"), TEXT("opened card is an OK card"));

	// Live parameter edit: the preview session takes the new body value.
	const float Edited = State->Body.Num() ? FMath::Clamp(State->Body[0].Value + 0.3f, -1.f, 2.f) : 0.f;
	if (State->Body.Num()) { State->Body[0].Value = Edited; ApplyShapes(); }
	const TArray<float> Live = PreviewShape(true);
	Check(Live.Num() > 0 && FMath::IsNearlyEqual(Live[0], Edited, 1e-4f), FString::Printf(TEXT("%s live = %.3f"), State->Body.Num() ? *State->Body[0].Label : TEXT("(none)"), Live.Num() ? Live[0] : -9.f));

	// Outfit export and wear (the same outfit back): the card stays an OK card.
	FString Error;
	const FString Clothes = ExportClothes(Error);
	Check(!Clothes.IsEmpty() && FPaths::FileExists(Clothes), TEXT("export outfit: ") + (Clothes.IsEmpty() ? Error : Clothes));
	const bool bWorn = !Clothes.IsEmpty() && WearClothes(Clothes, Error);
	Check(bWorn && PreviewParts() > 0 && PngHasChunk(Source, "okLb") && !PngHasChunk(Source, "okPv"), TEXT("wear outfit: ") + (bWorn ? FString::Printf(TEXT("%d parts"), PreviewParts()) : Error));
	Check(State->Body.Num() && FMath::IsNearlyEqual(State->Body[0].Value, Edited, 1e-4f), TEXT("edited parameter kept across wear"));

	// Animation: any OK animation asset of the project.
	TArray<FAssetData> Anims;
	FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get().GetAssetsByClass(UOKAnimSequence::StaticClass()->GetClassPathName(), Anims, true);
	State->Animation = Anims.Num() ? Cast<UOKAnimationAsset>(Anims[0].GetAsset()) : nullptr;
	const bool bPlayed = State->Animation && PlayAnimation(Error);
	if (bPlayed) Tick(0.25f);
	Check(bPlayed && Preview.IsValid() && Preview->PoseMode == 3, TEXT("play animation ") + (State->Animation ? State->Animation->GetName() : FString(TEXT("(no OK animation asset)"))) + (bPlayed ? FString() : TEXT(": ") + Error));
	StopAnimation();

	// Save As: a new OK card that reopens with the edit.
	const FString Out = WorkDirectory() / TEXT("selftest.ok.png");
	IFileManager::Get().Delete(*Out);
	const bool bSaved = SaveTo(Out, Error);
	Check(bSaved && PngHasChunk(Out, "okLb") && !PngHasChunk(Out, "okPv"), TEXT("save as OK card: ") + (bSaved ? Out : Error));
	Check(bSaved && State->Body.Num() && FMath::IsNearlyEqual(State->Body[0].Value, Edited, 1e-3f) && PreviewParts() > 0, TEXT("reopened OK card keeps the edit"));

	Lines.Add(Failures ? FString::Printf(TEXT("RESULT FAIL (%d)"), Failures) : FString(TEXT("RESULT PASS")));
	for (const FString& L : Lines) UE_LOG(LogTemp, Display, TEXT("OK card selftest: %s"), *L);
	FFileHelper::SaveStringArrayToFile(Lines, *(WorkDirectory() / TEXT("editor_selftest.txt")));
	if (bExit) FPlatformMisc::RequestExit(false);
}

namespace
{
	FAutoConsoleCommand OpenCommand(TEXT("OKCard.Open"),
		TEXT("OKCard.Open <card.png>: opens an OK card (or an original KKS card) in the OK card editor."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			if (Args.Num() < 1) { UE_LOG(LogTemp, Error, TEXT("OKCard.Open: card path required")); return; }
			// The toolkit manager owns the editor (InitAssetEditor registers it) until its window closes.
			FOKCardEditor::Open(Args[0]);
		}));

	FAutoConsoleCommand SelfTestCommand(TEXT("OKCard.EditorSelfTest"),
		TEXT("OKCard.EditorSelfTest [exit]: runs the self test of the last opened OK card editor once its preview is loaded."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			const bool bExit = Args.Contains(TEXT("exit"));
			TSharedRef<int32> Tries = MakeShared<int32>(0), Ready = MakeShared<int32>(0);
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([bExit, Tries, Ready](float)
			{
				FOKCardEditor* Editor = FOKCardEditor::LastOpened;
				if (!Editor || !Editor->HasPreview()) { if (++*Tries < 1200) return true; }
				else if (++*Ready < 20) return true;
				if (Editor) Editor->RunSelfTest(bExit);
				else { UE_LOG(LogTemp, Error, TEXT("OK card selftest: no OK card editor open")); if (bExit) FPlatformMisc::RequestExit(false); }
				return false;
			}), 0.1f);
		}));
}

#undef LOCTEXT_NAMESPACE
