#include "OKStudioGame.h"
#include "SOKStudioPanel.h"
#include "OKStudioGameInternal.h"
#include "OKStudioSettings.h"
#include "OKStudioThumbnails.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/TextureCube.h"
#include "ImageUtils.h"
#include "TextureResource.h"
#include "UnrealClient.h"
#include "Widgets/Input/SSlider.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "KKPreviewActor.h"
#include "KKExactSkinning.h"
#include "ShaderCompiler.h"
#include "KKRustExtract.h"
#include "Misc/FileHelper.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "OKSceneActor.h"
#include "OKStudioCatalog.h"
#include "OKStudioSky.h"
#include "OKStudioPost.h"
#include "OKStudioBloom.h"
#include "OKStudioPose.h"
#include "OKStudioImport.h"
#include "OKStudioImportSettings.h"
#include "OKSceneCard.h"
#include "KKAnimatedPng.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/MeshComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Materials/MaterialInterface.h"
#include "ok_extract.h"
#include "KKOkAnimation.h"
#include "OKAnimationAssets.h"
#include "OKAnimationLibrary.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SListView.h"
#include "UObject/GarbageCollection.h"
#include "RenderingThread.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "JsonObjectConverter.h"
#include "EngineUtils.h"
#include "Engine/PostProcessVolume.h"
#include "GameFramework/HUD.h"
#include "GameFramework/WorldSettings.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Engine/SkinnedAsset.h"
#include "Components/SkeletalMeshComponent.h"

namespace
{
	TWeakObjectPtr<UOKStudioGame> GRunning;
}

UOKStudioGame* UOKStudioGame::Get() { return GRunning.Get(); }

UWorld* UOKStudioGame::GetTickableGameObjectWorld() const { return SceneActor ? SceneActor->GetWorld() : nullptr; }

TArray<FString> UOKStudioGame::AvailableCards()
{
	TArray<FString> Files;
	// Hidden folders below CharacterCards are skipped; the project path itself may be relative ("../../..") and must
	// not count, so only the part below the cards folder is looked at.
	const FString Root = FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir() / TEXT("CharacterCards"));
	IFileManager::Get().FindFilesRecursive(Files, *Root, TEXT("*.png"), true, false);
	Files.RemoveAll([&Root](const FString& F) { const FString Below = FPaths::ConvertRelativePathToFull(F).Mid(Root.Len()); return Below.Contains(TEXT("/.")) || Below.Contains(TEXT("\\.")); });
	Files.Sort();
	const FString Project = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
	for (FString& F : Files) { F = FPaths::ConvertRelativePathToFull(F); FPaths::MakePathRelativeTo(F, *Project); }
	return Files;
}

FString UOKStudioGame::SceneDirectory()
{
	return FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("UserData/Studio/scene"));
}

TArray<FString> UOKStudioGame::SceneFiles()
{
	TArray<FString> Names;
	const FString Dir = SceneDirectory();
	IFileManager::Get().FindFiles(Names, *(Dir / TEXT("*.okscene")), true, false);
	// Scene cards: OK scene cards and KStudio scene cards (other PNGs are left out).
	TArray<FString> Pngs;
	IFileManager::Get().FindFiles(Pngs, *(Dir / TEXT("*.png")), true, false);
	for (const FString& Png : Pngs) if (FOKSceneCard::Kind(Dir / Png) != FOKSceneCard::EKind::None) Names.Add(Png);
	TArray<FString> Files;
	for (const FString& Name : Names) Files.Add(Dir / Name);
	Files.Sort([](const FString& A, const FString& B) { return IFileManager::Get().GetTimeStamp(*A) > IFileManager::Get().GetTimeStamp(*B); });
	return Files;
}

UOKStudioGame* UOKStudioGame::Start(UWorld* World, const FString& InFile, FString& OutError)
{
	if (UOKStudioGame* Running = Get()) Running->Stop();
	if (!World) { OutError = TEXT("no game world"); return nullptr; }
	UOKStudioGame* Game = NewObject<UOKStudioGame>();
	Game->AddToRoot();
	GRunning = Game;
	Game->Document = NewObject<UOKSceneDocument>(Game);
	Game->Document->OnChanged.AddLambda([Game]
	{
		Game->bSyncPending = true;
		if (!Game->bHistoryRestoring && !Game->bTimelinePlaying && !Game->bEvaluatingTimeline) { Game->bHistoryDirty = true; Game->LastChangeSeconds = FPlatformTime::Seconds(); Game->bDirty = true; }
	});
	// A scene is a few rooms and characters in one view, nearly everything visible: occlusion queries hide little, while
	// the render thread waited ~2–4 ms a frame on the RHI thread for their fence (3167: 11.8 -> 8.0 ms).
	if (IConsoleVariable* Occlusion = IConsoleManager::Get().FindConsoleVariable(TEXT("r.AllowOcclusionQueries")))
	{
		Game->SavedOcclusionQueries = Occlusion->GetInt();
		Occlusion->Set(0, ECVF_SetByCode);
	}
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (APostProcessVolume* Volume = Cast<APostProcessVolume>(Actor))
		{
			if (Volume->bEnabled) { Volume->bEnabled = false; Game->DisabledWorldVolumes.Add(Volume); }
		}
		// Sky light, sky atmosphere and height fog are AInfo actors too: only the world settings, controllers and HUD stay.
		else if (!Actor->IsHidden() && !Actor->IsA<AWorldSettings>() && !Actor->IsA<AController>() && !Actor->IsA<AHUD>())
		{
			UE_LOG(LogTemp, Display, TEXT("OK studio: world actor %s (%s) hidden while the studio runs"), *Actor->GetName(), *Actor->GetClass()->GetName());
			Actor->SetActorHiddenInGame(true);
			Game->HiddenWorldActors.Add(Actor);
			// Hidden, it need not tick either (the gallery's character kept solving its animation and physics, ~12 ms a
			// frame, behind the studio).
			if (Actor->IsActorTickEnabled()) { Actor->SetActorTickEnabled(false); Game->PausedWorldActors.Add(Actor); }
			// Lights, sky atmosphere, sky light and fog go by their component's visibility, not the actor's.
			TInlineComponentArray<USceneComponent*> Components(Actor);
			for (USceneComponent* Component : Components)
				if (Component->IsVisible()) { Component->SetVisibility(false); Game->HiddenWorldComponents.Add(Component); }
		}
	}
	FActorSpawnParameters Params;
	Params.ObjectFlags = RF_Transient;
	Game->SceneActor = World->SpawnActor<AOKSceneActor>(FVector::ZeroVector, FRotator::ZeroRotator, Params);
	Game->Viewer = World->SpawnActor<AOKStudioViewer>(FVector::ZeroVector, FRotator::ZeroRotator, Params);
	if (!Game->SceneActor || !Game->Viewer) { OutError = TEXT("studio actors not spawned"); Game->Stop(); return nullptr; }
	Game->SceneActor->Document = Game->Document;
	if (APlayerController* PC = World->GetFirstPlayerController())
	{
		Game->PreviousViewTarget = PC->GetViewTarget();
		PC->SetViewTarget(Game->Viewer);
		PC->bShowMouseCursor = true;
		PC->SetInputMode(FInputModeUIOnly());
	}
	if (!InFile.IsEmpty()) { if (!Game->Load(InFile, OutError)) UE_LOG(LogTemp, Warning, TEXT("OK studio: %s"), *OutError); }
	else Game->Add(EOKSceneObjectKind::Light);
	if (GEngine && GEngine->GameViewport)
	{
		Game->Panel = SNew(SOKStudioPanel).Game(Game);
		Game->PanelHost = Game->Panel;
		GEngine->GameViewport->AddViewportWidgetContent(Game->PanelHost.ToSharedRef(), 10);
	}
	Game->SyncNow();
	Game->ResetHistory();
	Game->bDirty = false;
	Game->LastBackupSeconds = FPlatformTime::Seconds();
	OKStudioControl::Start();
	return Game;
}

void UOKStudioViewCamera::GetCameraView(float DeltaTime, FMinimalViewInfo& DesiredView)
{
	Super::GetCameraView(DeltaTime, DesiredView);
	if (NearClip > 0.f) DesiredView.PerspectiveNearClipPlane = NearClip;
}

AOKStudioViewer::AOKStudioViewer(const FObjectInitializer& Initializer)
	: Super(Initializer.SetDefaultSubobjectClass<UOKStudioViewCamera>(TEXT("CameraComponent")))
{
}

void UOKStudioGame::ResetHistory()
{
	UndoStack.Reset();
	RedoStack.Reset();
	UndoSelections.Reset();
	RedoSelections.Reset();
	HistoryState = Document ? Document->ToJson() : FString();
	HistorySelection = Selection;
	bHistoryDirty = false;
}

void UOKStudioGame::CommitHistory()
{
	bHistoryDirty = false;
	if (!Document) return;
	FString State = Document->ToJson();
	if (State == HistoryState) return;
	UndoStack.Add(MoveTemp(HistoryState));
	UndoSelections.Add(HistorySelection);
	RedoStack.Reset();
	RedoSelections.Reset();
	HistoryState = MoveTemp(State);
	HistorySelection = Selection;
	// At most 100 steps and about 256 MB of states (a big imported scene is megabytes of JSON).
	int64 Bytes = 0;
	for (const FString& S : UndoStack) Bytes += S.Len() * sizeof(TCHAR);
	while (UndoStack.Num() > 1 && (UndoStack.Num() > 100 || Bytes > (int64(256) << 20))) { Bytes -= UndoStack[0].Len() * sizeof(TCHAR); UndoStack.RemoveAt(0); UndoSelections.RemoveAt(0); }
}

void UOKStudioGame::RestoreHistory(const FString& State)
{
	const TArray<int32> Keep = HistorySelection;
	// Objects the step does not change keep their exact values: read back from JSON a float or a field the file does
	// not hold can differ, and the scene would rebuild every character and item.
	auto Text = [](const FOKSceneObject& O) { FString S; FJsonObjectConverter::UStructToJsonObjectString(O, S, 0, 0, 0, nullptr, false); return S; };
	TMap<int32, TPair<FString, FOKSceneObject>> Before;
	for (const FOKSceneObject& O : Document->Objects) Before.Add(O.Id, { Text(O), O });
	FString Error;
	bHistoryRestoring = true;
	if (!Document->FromJson(State, Error)) UE_LOG(LogTemp, Warning, TEXT("OK studio: undo state not restored: %s"), *Error);
	int32 Kept = 0;
	for (FOKSceneObject& O : Document->Objects)
		if (const TPair<FString, FOKSceneObject>* Old = Before.Find(O.Id); Old && Old->Key == Text(O)) { O = Old->Value; ++Kept; }
	LastHistoryKept = Kept;
	UE_LOG(LogTemp, Display, TEXT("OK studio: history restored, %d of %d objects unchanged"), Kept, Document->Objects.Num());
	Document->NotifyChanged();
	bHistoryRestoring = false;
	bHistoryDirty = false;
	// The selection the step was left with comes back with it (objects no longer there drop out); the view stays.
	TArray<int32> Restored;
	for (int32 Id : Keep) if (Document->Find(Id)) Restored.Add(Id);
	SelectMany(Restored, Restored.Num() ? Restored.Last() : -1);
	SyncNow();
}

void UOKStudioGame::Undo()
{
	if (bHistoryDirty) CommitHistory();
	if (UndoStack.IsEmpty()) return;
	RedoStack.Add(MoveTemp(HistoryState));
	RedoSelections.Add(HistorySelection);
	HistoryState = UndoStack.Pop();
	HistorySelection = UndoSelections.Num() ? UndoSelections.Pop() : TArray<int32>();
	RestoreHistory(HistoryState);
	bDirty = true;
}

void UOKStudioGame::Redo()
{
	if (bHistoryDirty) CommitHistory();
	if (RedoStack.IsEmpty()) return;
	UndoStack.Add(MoveTemp(HistoryState));
	UndoSelections.Add(HistorySelection);
	HistoryState = RedoStack.Pop();
	HistorySelection = RedoSelections.Num() ? RedoSelections.Pop() : TArray<int32>();
	RestoreHistory(HistoryState);
	bDirty = true;
}

void UOKStudioGame::Stop()
{
	bStopRequested = false;
	OKStudioControl::Stop();
	if (SceneActor && SceneActor->GetWorld())
	{
		APlayerController* PC = SceneActor->GetWorld()->GetFirstPlayerController();
		if (PC && PreviousViewTarget.IsValid() && PC->GetViewTarget() == Viewer) PC->SetViewTarget(PreviousViewTarget.Get());
	}
	if (PanelHost && GEngine && GEngine->GameViewport) GEngine->GameViewport->RemoveViewportWidgetContent(PanelHost.ToSharedRef());
	Panel.Reset();
	PanelHost.Reset();
	if (SceneActor) SceneActor->Destroy();
	if (Viewer) Viewer->Destroy();
	SceneActor = nullptr;
	Viewer = nullptr;
	for (const TWeakObjectPtr<AActor>& Actor : HiddenWorldActors) if (Actor.IsValid()) Actor->SetActorHiddenInGame(false);
	for (const TWeakObjectPtr<AActor>& Actor : PausedWorldActors) if (Actor.IsValid()) Actor->SetActorTickEnabled(true);
	PausedWorldActors.Reset();
	for (const TWeakObjectPtr<USceneComponent>& Component : HiddenWorldComponents) if (Component.IsValid()) Component->SetVisibility(true);
	HiddenWorldComponents.Reset();
	for (const TWeakObjectPtr<APostProcessVolume>& Volume : DisabledWorldVolumes) if (Volume.IsValid()) Volume->bEnabled = true;
	HiddenWorldActors.Reset();
	DisabledWorldVolumes.Reset();
	if (SavedOcclusionQueries != INDEX_NONE)
		if (IConsoleVariable* Occlusion = IConsoleManager::Get().FindConsoleVariable(TEXT("r.AllowOcclusionQueries"))) Occlusion->Set(SavedOcclusionQueries, ECVF_SetByCode);
	SavedOcclusionQueries = INDEX_NONE;
	if (GRunning.Get() == this) GRunning = nullptr;
	FSimpleMulticastDelegate Stopped = MoveTemp(OnStopped);
	OnStopped.Clear();
	Stopped.Broadcast();
	RemoveFromRoot();
}

int32 UOKStudioGame::AddCharacter(const FString& Card, int32 Outfit)
{
	const int32 Id = Add(EOKSceneObjectKind::Character);
	if (FOKSceneObject* O = Document->Find(Id))
	{
		O->Character.Card = Card;
		O->Character.Outfit = Outfit;
		O->Name = FPaths::GetBaseFilename(Card);
		if (Panel) Panel->RefreshRows();
		Document->NotifyChanged();
	}
	return Id;
}

int32 UOKStudioGame::Add(EOKSceneObjectKind Kind)
{
	const FOKSceneObject* Sel = Document->Find(SelectedId);
	const int32 Parent = !Sel ? -1 : (Sel->Kind == EOKSceneObjectKind::Folder ? Sel->Id : Sel->Parent);
	const int32 Id = Document->AddObject(Kind, FString(), Parent);
	FOKSceneObject* O = Document->Find(Id);
	if (Kind == EOKSceneObjectKind::Light) { O->Name = TEXT("主光"); O->Light.Type = EOKSceneLightType::Directional; O->Light.Intensity = 6.f; O->Transform = FTransform(FRotator(-35.f, -120.f, 0.f), FVector(0.f, 0.f, 300.f)); }
	if (Kind == EOKSceneObjectKind::Camera) O->Transform = FTransform(FRotator(Pitch, Yaw, 0.f), Focus - FRotator(Pitch, Yaw, 0.f).Vector() * Distance);
	Select(Id);
	return Id;
}

void UOKStudioGame::Select(int32 Id)
{
	SelectMany(Id >= 0 ? TArray<int32>{ Id } : TArray<int32>(), Id);
}

void UOKStudioGame::SelectMany(const TArray<int32>& Ids, int32 Primary)
{
	Selection.Reset();
	for (int32 Id : Ids) if (Document && Document->Find(Id)) Selection.AddUnique(Id);
	SelectedId = Selection.Contains(Primary) ? Primary : (Selection.Num() ? Selection.Last() : -1);
	if (Panel) Panel->RefreshRows();
}

void UOKStudioGame::ToggleSelect(int32 Id)
{
	TArray<int32> Ids = Selection;
	if (Ids.Contains(Id)) { Ids.Remove(Id); SelectMany(Ids, SelectedId == Id ? -1 : SelectedId); }
	else { Ids.Add(Id); SelectMany(Ids, Id); }
}

TArray<int32> UOKStudioGame::SelectionRoots() const
{
	TArray<int32> Out;
	for (int32 Id : Selection)
	{
		bool bUnder = false;
		for (const FOKSceneObject* P = Document->Find(Document->Find(Id) ? Document->Find(Id)->Parent : -1); P && !bUnder; P = Document->Find(P->Parent)) bUnder = Selection.Contains(P->Id);
		if (!bUnder) Out.Add(Id);
	}
	return Out;
}

bool UOKStudioGame::Save(const FString& Path, FString& OutError)
{
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
	if (FPaths::GetExtension(Path).Equals(TEXT("png"), ESearchCase::IgnoreCase))
	{
		// OK scene card: the current view and the three views, with the scene and its character cards inside. Written beside
		// the file first: the old card stays whole until the new one is complete.
		if (!Viewer || !SceneActor) { OutError = TEXT("studio not running"); return false; }
		UpdateViewer();
		SyncNow();
		const UCameraComponent* Camera = Viewer->GetCameraComponent();
		TArray<TArray<uint8>> Views;
		if (!FOKSceneCard::CaptureViews(SceneActor, Camera->GetComponentTransform(), Camera->FieldOfView, &Camera->PostProcessSettings, Views, OutError)) return false;
		const FString Temp = FPaths::GetPath(Path) / FPaths::GetBaseFilename(Path) + TEXT(".saving.png");
		if (!FOKSceneCard::Write(Document, Views, Temp, OutError)) { IFileManager::Get().Delete(*Temp, false, true, true); return false; }
		if (!UOKSceneDocument::ReplaceFile(Temp, Path, OutError)) return false;
	}
	else if (!Document->SaveToFile(Path, OutError)) return false;
	File = Path;
	bDirty = false;
	return true;
}

bool UOKStudioGame::Load(const FString& Path, FString& OutError)
{
	FString Note;
	if (!FOKSceneCard::Open(Path, Document, OutError, &Note)) return false;
	UE_LOG(LogTemp, Display, TEXT("OK studio: opened %s: %s"), *Path, *Note);
	// A KStudio scene is imported, not edited in place: saving writes an OK scene card beside it.
	File = FOKSceneCard::Kind(Path) == FOKSceneCard::EKind::KStudio ? FString() : Path;
	ReleaseSceneResources();
	ResetHistory();
	bDirty = false;
	return true;
}

void UOKStudioGame::ReleaseSceneResources()
{
	Select(-1);
	// Release the old scene as one unit, including ids also present in the new
	// document. A budgeted Sync otherwise retains old actors until their turn,
	// and only schedules GC after the replacement map has already been built.
	if (SceneActor)
	{
		SceneActor->ReleaseScene();
		FlushRenderingCommands();
		CollectGarbage(RF_NoFlags, true);
		FlushRenderingCommands();
		AKKPreviewActor::ReleaseSceneCaches();
		UE_LOG(LogTemp, Display, TEXT("OK studio: previous scene released (memory %.1f GB)"), FPlatformMemory::GetStats().UsedPhysical / double(1 << 30));
	}
}

namespace
{
	const FVector GuideAxes[3] = { FVector(1, 0, 0), FVector(0, 1, 0), FVector(0, 0, 1) };

	double SegmentDistance(const FVector2D& P, const FVector2D& A, const FVector2D& B)
	{
		const FVector2D AB = B - A;
		const double T = AB.SizeSquared() > 1e-6 ? FMath::Clamp(FVector2D::DotProduct(P - A, AB) / AB.SizeSquared(), 0., 1.) : 0.;
		return FVector2D::Distance(P, A + AB * T);
	}
}

namespace OKStudioGameInternal
{
	/** UE world transform of a bone of a shown character (its pose mesh). */
	bool BoneWorld(const AKKPreviewActor* Actor, const FString& Bone, FTransform& Out)
	{
		if (!Actor) return false;
		UPoseableMeshComponent* Mesh = Actor->BodySkel ? Actor->BodySkel.Get() : (Actor->SkelFollowers.Num() ? Actor->SkelFollowers[0].Get() : nullptr);
		if (!Mesh || Mesh->GetBoneIndex(*Bone) == INDEX_NONE) return false;
		Out = Mesh->GetBoneTransformByName(*Bone, EBoneSpaces::WorldSpace);
		return true;
	}
	/** Unity local axes X, Y, Z as UE local axes (the converter maps rotations by R_ue = B R_unity B, B a proper rotation). */
	const FVector UnityAxesInUe[3] = { FVector(-1, 0, 0), FVector(0, 0, 1), FVector(0, 1, 0) };
}

namespace
{
	/** Where the guide is: its origin, world axes, how its axes look on screen and which handles it has. */
	struct FGuideFrame
	{
		APlayerController* PC = nullptr;
		FVector Origin = FVector::ZeroVector;
		FVector Axes[3];
		/** Screen pixels per cm along each axis (zero: the axis points at the camera). */
		FVector2D PerCm[3];
		FVector2D Center = FVector2D::ZeroVector;
		double CmPerPixel = 1.;
		int32 Mode = 0;
		bool bBone = false, bIk = false;
		/** The character's own small move axes beside a bone or IK guide. */
		FVector RootOrigin = FVector::ZeroVector;
		FVector2D RootCenter = FVector2D::ZeroVector;
		bool bRoot = false;
		FVector2D RootPerCm[3];
	};

	bool Project(APlayerController* PC, const FVector& W, FVector2D& S) { return UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition(PC, W, S, true); }

	bool MakeGuideFrame(const UOKStudioGame& G, FGuideFrame& F)
	{
		if (!G.bShowGuide || !G.SceneActor || !G.SceneActor->GetWorld() || !G.Document) return false;
		F.PC = G.SceneActor->GetWorld()->GetFirstPlayerController();
		const FOKSceneObject* Object = G.Document->Find(G.SelectedId);
		if (!F.PC || !F.PC->PlayerCameraManager || !Object) return false;
		const FTransform ObjectWorld = G.Document->WorldTransform(G.SelectedId) * G.SceneActor->GetActorTransform();
		FTransform World = ObjectWorld;
		F.bBone = !G.GuideBone.IsEmpty() && Object->Kind == EOKSceneObjectKind::Character;
		if (F.bBone)
		{
			FTransform Bone;
			if (!OKStudioGameInternal::BoneWorld(G.SceneActor->PreviewOf(G.SelectedId), G.GuideBone, Bone)) return false;
			World = Bone;
			for (int32 K = 0; K < 3; ++K) F.Axes[K] = Bone.GetRotation().RotateVector(OKStudioGameInternal::UnityAxesInUe[K]).GetSafeNormal();
		}
		FVector IkWorld;
		F.bIk = !F.bBone && G.GuideIk >= 0 && G.IkTargetWorld(G.GuideIk, IkWorld);
		if (F.bIk) World = FTransform(IkWorld);
		if (!F.bBone)
			for (int32 K = 0; K < 3; ++K) F.Axes[K] = G.bLocalAxes && !F.bIk ? ObjectWorld.GetRotation().RotateVector(GuideAxes[K]).GetSafeNormal() : GuideAxes[K];
		F.Mode = F.bBone ? 1 : (F.bIk ? 0 : G.GuideMode);
		F.Origin = World.GetLocation();
		// Panel pixels per 10 cm at the pivot, measured sideways to the camera.
		FVector2D Side;
		if (!Project(F.PC, F.Origin, F.Center) || !Project(F.PC, F.Origin + F.PC->PlayerCameraManager->GetCameraRotation().RotateVector(FVector(0, 1, 0)) * 10., Side)) return false;
		const double PixelsPer10 = FVector2D::Distance(F.Center, Side);
		if (PixelsPer10 < 1e-3) return false;
		F.CmPerPixel = 10. / PixelsPer10;
		for (int32 K = 0; K < 3; ++K)
		{
			FVector2D End;
			F.PerCm[K] = Project(F.PC, F.Origin + F.Axes[K] * 10., End) ? (End - F.Center) / 10. : FVector2D::ZeroVector;
		}
		// Beside a bone or IK guide the whole character can still be moved: its own axes, small, at its feet.
		if ((F.bBone || F.bIk) && Project(F.PC, ObjectWorld.GetLocation(), F.RootCenter))
		{
			F.bRoot = true;
			F.RootOrigin = ObjectWorld.GetLocation();
			for (int32 K = 0; K < 3; ++K)
			{
				FVector2D End;
				F.RootPerCm[K] = Project(F.PC, F.RootOrigin + GuideAxes[K] * 10., End) ? (End - F.RootCenter) / 10. : FVector2D::ZeroVector;
			}
		}
		return true;
	}

	/** Centimetres along two axes for a screen movement (the axes' screen vectors need not be at right angles). */
	FVector2D SolvePlane(const FVector2D& A, const FVector2D& B, const FVector2D& D)
	{
		const double Det = A.X * B.Y - A.Y * B.X;
		if (FMath::Abs(Det) < 1e-6) return FVector2D::ZeroVector;
		return FVector2D((D.X * B.Y - D.Y * B.X) / Det, (A.X * D.Y - A.Y * D.X) / Det);
	}

	double AlongAxis(const FVector2D& PerCm, const FVector2D& Delta)
	{
		const double L2 = PerCm.SizeSquared();
		return L2 > 1e-8 ? FVector2D::DotProduct(Delta, PerCm) / L2 : 0.;
	}

	double Snapped(double V, double Step, bool bSnap) { return bSnap && Step > 0. ? FMath::RoundToDouble(V / Step) * Step : V; }

	bool InsidePolygon(const FVector2D& P, const TArray<FVector2D>& Poly)
	{
		bool bIn = false;
		for (int32 I = 0, J = Poly.Num() - 1; I < Poly.Num(); J = I++)
			if (((Poly[I].Y > P.Y) != (Poly[J].Y > P.Y)) && (P.X < (Poly[J].X - Poly[I].X) * (P.Y - Poly[I].Y) / (Poly[J].Y - Poly[I].Y) + Poly[I].X)) bIn = !bIn;
		return bIn;
	}
}

bool UOKStudioGame::GuideHandles(TArray<FGuideHandle>& Out, FVector2D& Center, double& CmPerPixel) const
{
	Out.Reset();
	FGuideFrame F;
	if (!MakeGuideFrame(*this, F)) return false;
	Center = F.Center;
	CmPerPixel = F.CmPerPixel;
	const double Size = FMath::Clamp(double(FOKStudioSettings::Get().GizmoScale), 0.3, 4.);
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		FGuideHandle H;
		H.Axis = Axis;
		const FVector A = F.Axes[Axis];
		if (F.Mode == 1)
		{
			const FVector U = (FMath::Abs(A.Z) < .9 ? (A ^ FVector::UpVector) : (A ^ FVector::ForwardVector)).GetSafeNormal();
			const FVector V = A ^ U;
			for (int32 I = 0; I < 48; ++I)
			{
				const double T = 2. * UE_DOUBLE_PI * I / 48.;
				FVector2D P;
				if (!Project(F.PC, F.Origin + (U * FMath::Cos(T) + V * FMath::Sin(T)) * 85. * Size * F.CmPerPixel, P)) { H.Points.Reset(); break; }
				H.Points.Add(P);
			}
			H.bClosed = true;
		}
		else
		{
			if (F.PerCm[Axis].Size() * 10. < .5) continue;
			const FVector2D N = F.PerCm[Axis].GetSafeNormal();
			H.Points = { F.Center + N * 14. * Size, F.Center + N * (F.Mode == 2 ? 95. : 110.) * Size };
		}
		if (H.Points.Num() >= 2) Out.Add(MoveTemp(H));
	}
	if (F.Mode == 0)
	{
		// Plane squares between two axes (move in that plane).
		static const int32 Pairs[3][2] = { { 0, 1 }, { 1, 2 }, { 0, 2 } };
		for (int32 P = 0; P < 3; ++P)
		{
			const FVector2D A = F.PerCm[Pairs[P][0]].GetSafeNormal(), B = F.PerCm[Pairs[P][1]].GetSafeNormal();
			if (A.IsNearlyZero() || B.IsNearlyZero() || FMath::Abs(A ^ B) < 0.15) continue;
			FGuideHandle H;
			H.Axis = 4 + P;
			H.bClosed = H.bFilled = true;
			const double I = 24. * Size, O = 40. * Size;
			H.Points = { F.Center + A * I + B * I, F.Center + A * O + B * I, F.Center + A * O + B * O, F.Center + A * I + B * O };
			Out.Add(MoveTemp(H));
		}
	}
	if (F.Mode == 2)
	{
		FGuideHandle H;
		H.Axis = 3;
		const double S = 9. * Size;
		H.Points = { F.Center + FVector2D(-S, -S), F.Center + FVector2D(S, -S), F.Center + FVector2D(S, S), F.Center + FVector2D(-S, S) };
		H.bClosed = true;
		Out.Add(MoveTemp(H));
	}
	if (F.bRoot)
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			if (F.RootPerCm[Axis].Size() * 10. < .5) continue;
			FGuideHandle H;
			H.Axis = 10 + Axis;
			const FVector2D N = F.RootPerCm[Axis].GetSafeNormal();
			H.Points = { F.RootCenter + N * 6. * Size, F.RootCenter + N * 45. * Size };
			Out.Add(MoveTemp(H));
		}
	return true;
}

int32 UOKStudioGame::HitGuide(const FVector2D& Position) const
{
	TArray<FGuideHandle> Handles;
	FVector2D Center;
	double CmPerPixel = 0.;
	if (!GuideHandles(Handles, Center, CmPerPixel)) return -1;
	int32 Best = -1;
	double BestDistance = 9.;
	for (const FGuideHandle& H : Handles)
	{
		if (H.bFilled) continue;
		if (H.Axis == 3) { const double S = 10. * FMath::Clamp(double(FOKStudioSettings::Get().GizmoScale), 0.3, 4.); if (FMath::Abs(Position.X - Center.X) <= S && FMath::Abs(Position.Y - Center.Y) <= S) return 3; continue; }
		const int32 Segments = H.Points.Num() - (H.bClosed ? 0 : 1);
		for (int32 I = 0; I < Segments; ++I)
		{
			const double D = SegmentDistance(Position, H.Points[I], H.Points[(I + 1) % H.Points.Num()]);
			if (D < BestDistance) { BestDistance = D; Best = H.Axis; }
		}
	}
	if (Best >= 0) return Best;
	for (const FGuideHandle& H : Handles) if (H.bFilled && InsidePolygon(Position, H.Points)) return H.Axis;
	return -1;
}

void UOKStudioGame::BeginGuideDrag(int32 Axis)
{
	DragAxis = Axis;
	DragTotal = FVector2D::ZeroVector;
	DragBoneApplied = 0.f;
	DragStart.Reset();
	GuideReadout.Reset();
	if (!Document || !SceneActor) return;
	const TArray<int32> Ids = Axis >= 10 ? TArray<int32>{ SelectedId } : SelectionRoots();
	for (int32 Id : Ids)
		if (const FOKSceneObject* O = Document->Find(Id))
		{
			DragStart.Add(Id, Document->WorldTransform(Id) * SceneActor->GetActorTransform());
			// The relative scale at the start (scaling multiplies it).
			DragStart.Add(-1000000 - Id, O->Transform);
		}
	if (const FOKSceneObject* O = Document->Find(SelectedId))
		if (const FOKSceneIkTarget* T = O->Character.Ik.FindByPredicate([this](const FOKSceneIkTarget& X) { return X.Effector == GuideIk; }))
			DragStart.Add(-1, FTransform(T->Position));
}

void UOKStudioGame::EndGuideDrag()
{
	DragAxis = -1;
	DragStart.Reset();
	GuideReadout.Reset();
}

bool UOKStudioGame::DragGuide(const FVector2D& Delta, bool bCtrl)
{
	FOKSceneObject* Object = Document ? Document->Find(SelectedId) : nullptr;
	FGuideFrame F;
	if (!Object || DragAxis < 0 || !MakeGuideFrame(*this, F)) return false;
	DragTotal += Delta;
	const FOKStudioSettings& S = FOKStudioSettings::Get();
	const bool bSnapNow = bSnap || bCtrl;
	static const TCHAR* AxisNames[] = { TEXT("X"), TEXT("Y"), TEXT("Z") };
	const int32 Axis = DragAxis;
	auto ParentWorld = [this](int32 Id) { return Document->ParentFrame(Id) * SceneActor->GetActorTransform(); };

	if (F.bIk && Axis < 3 && Object->Kind == EOKSceneObjectKind::Character)
	{
		// IK target: along a world axis from where it was (kept in the character's space).
		FOKSceneIkTarget* T = Object->Character.Ik.FindByPredicate([this](const FOKSceneIkTarget& X) { return X.Effector == GuideIk; });
		const AKKPreviewActor* P = SceneActor->PreviewOf(SelectedId);
		const FTransform* Start = DragStart.Find(-1);
		if (!T || !P || !Start) return false;
		const double Cm = Snapped(AlongAxis(F.PerCm[Axis], DragTotal), S.SnapMove, bSnapNow);
		T->Position = Start->GetLocation() + P->GetActorTransform().InverseTransformVector(F.Axes[Axis] * Cm);
		GuideReadout = FString::Printf(TEXT("IK 移動 %s %+.1f cm"), AxisNames[Axis], Cm);
		Document->NotifyChanged();
		return true;
	}
	if (F.bBone && Axis < 3 && Object->Kind == EOKSceneObjectKind::Character)
	{
		// Bone: turn about its own local axis and keep that as its FK rotation (snapped turns go in whole steps).
		const float Total = float(Snapped(DragTotal.X * 0.5, S.SnapRotate, bSnapNow));
		const float Step = Total - DragBoneApplied;
		DragBoneApplied = Total;
		GuideReadout = FString::Printf(TEXT("骨骼旋轉 %s %+.1f°"), AxisNames[Axis], Total);
		if (FMath::IsNearlyZero(Step)) return true;
		FVector Euler = FVector::ZeroVector;
		if (const FOKSceneFkBone* B = Object->Character.Fk.FindByPredicate([this](const FOKSceneFkBone& X) { return X.Bone == GuideBone; })) Euler = B->Euler;
		else if (const AKKPreviewActor* P = SceneActor->PreviewOf(SelectedId)) P->Session.BoneEuler(GuideBone, Euler);
		const float In[3] = { float(Euler.X), float(Euler.Y), float(Euler.Z) };
		float Turned[3] = {};
		if (ok_unity_euler_rotate(In, Axis, Step, Turned) != 0) return false;
		const FVector NewEuler(Turned[0], Turned[1], Turned[2]);
		if (FOKSceneFkBone* B = Object->Character.Fk.FindByPredicate([this](const FOKSceneFkBone& X) { return X.Bone == GuideBone; })) B->Euler = NewEuler;
		else Object->Character.Fk.Add({ GuideBone, NewEuler });
		Document->NotifyChanged();
		return true;
	}

	// Objects: the whole movement so far applied to where each selected object started.
	FVector Offset = FVector::ZeroVector;
	FQuat Turn = FQuat::Identity;
	double Factor = 1.;
	int32 Kind = -1;
	if (Axis >= 10 && Axis <= 12 && F.bRoot)
	{
		const double Cm = Snapped(AlongAxis(F.RootPerCm[Axis - 10], DragTotal), S.SnapMove, bSnapNow);
		Offset = GuideAxes[Axis - 10] * Cm;
		Kind = 0;
		GuideReadout = FString::Printf(TEXT("角色移動 %s %+.1f cm"), AxisNames[Axis - 10], Cm);
	}
	else if (F.Mode == 0 && Axis < 3)
	{
		const double Cm = Snapped(AlongAxis(F.PerCm[Axis], DragTotal), S.SnapMove, bSnapNow);
		Offset = F.Axes[Axis] * Cm;
		Kind = 0;
		GuideReadout = FString::Printf(TEXT("移動 %s %+.1f cm"), AxisNames[Axis], Cm);
	}
	else if (F.Mode == 0 && Axis >= 4 && Axis <= 6)
	{
		static const int32 Pairs[3][2] = { { 0, 1 }, { 1, 2 }, { 0, 2 } };
		const int32 A = Pairs[Axis - 4][0], B = Pairs[Axis - 4][1];
		const FVector2D Cm = SolvePlane(F.PerCm[A], F.PerCm[B], DragTotal);
		const double X = Snapped(Cm.X, S.SnapMove, bSnapNow), Y = Snapped(Cm.Y, S.SnapMove, bSnapNow);
		Offset = F.Axes[A] * X + F.Axes[B] * Y;
		Kind = 0;
		GuideReadout = FString::Printf(TEXT("移動 %s %+.1f、%s %+.1f cm"), AxisNames[A], X, AxisNames[B], Y);
	}
	else if (F.Mode == 1 && Axis < 3)
	{
		const double Degrees = Snapped(DragTotal.X * 0.5, S.SnapRotate, bSnapNow);
		Turn = FQuat(F.Axes[Axis], FMath::DegreesToRadians(Degrees));
		Kind = 1;
		GuideReadout = FString::Printf(TEXT("旋轉 %s %+.1f°"), AxisNames[Axis], Degrees);
	}
	else if (F.Mode == 2 && Axis <= 3)
	{
		const double Amount = Axis == 3 ? DragTotal.X : FVector2D::DotProduct(DragTotal, F.PerCm[Axis].GetSafeNormal());
		Factor = FMath::Max(0.01, 1. + Amount * 0.01);
		Kind = 2;
		GuideReadout = Axis == 3 ? FString::Printf(TEXT("縮放 ×%.2f"), Factor) : FString::Printf(TEXT("縮放 %s ×%.2f"), AxisNames[Axis], Factor);
	}
	if (Kind < 0) return false;
	const FTransform* Pivot = DragStart.Find(SelectedId);
	for (const TPair<int32, FTransform>& Start : DragStart)
	{
		if (Start.Key < 0) continue;
		FOKSceneObject* O = Document->Find(Start.Key);
		const FTransform* Relative = DragStart.Find(-1000000 - Start.Key);
		if (!O || !Relative) continue;
		const FTransform Parent = ParentWorld(Start.Key);
		if (Kind == 0) O->Transform.SetLocation(Parent.InverseTransformPosition(Start.Value.GetLocation() + Offset));
		else if (Kind == 1)
		{
			// Several objects turn about the primary one; each also turns in place.
			const FVector Center = Pivot ? Pivot->GetLocation() : Start.Value.GetLocation();
			O->Transform.SetLocation(Parent.InverseTransformPosition(Center + Turn.RotateVector(Start.Value.GetLocation() - Center)));
			O->Transform.SetRotation(Parent.GetRotation().Inverse() * (Turn * Start.Value.GetRotation()));
		}
		else
		{
			FVector Scale = Relative->GetScale3D();
			for (int32 K = 0; K < 3; ++K)
				if (Axis == 3 || Axis == K) Scale[K] = FMath::Max(bSnapNow ? S.SnapScale : 0.01, Snapped(Scale[K] * Factor, S.SnapScale, bSnapNow));
			O->Transform.SetScale3D(Scale);
		}
	}
	Document->NotifyChanged();
	return true;
}

bool UOKStudioGame::ApplyGuideDrag(int32 Axis, const FVector2D& Delta)
{
	// One movement from where the selection is now (tests and scripts).
	BeginGuideDrag(Axis);
	const bool bOk = DragGuide(Delta, false);
	EndGuideDrag();
	return bOk;
}

bool UOKStudioGame::IkTargetWorld(int32 Effector, FVector& Out) const
{
	const FOKSceneObject* O = Document ? Document->Find(SelectedId) : nullptr;
	const AKKPreviewActor* P = O && SceneActor ? SceneActor->PreviewOf(SelectedId) : nullptr;
	const FOKSceneIkTarget* T = O ? O->Character.Ik.FindByPredicate([Effector](const FOKSceneIkTarget& X) { return X.Effector == Effector; }) : nullptr;
	if (!T || !P) return false;
	Out = P->GetActorTransform().TransformPosition(T->Position);
	return true;
}

void UOKStudioGame::Orbit(float DeltaYaw, float DeltaPitch)
{
	Yaw += DeltaYaw;
	Pitch = FMath::Clamp(Pitch + DeltaPitch, -89.f, 89.f);
	Document->ActiveCamera = -1;
}

void UOKStudioGame::Look(float DeltaYaw, float DeltaPitch)
{
	// The camera stays where it is and turns; the focus moves with its line of sight.
	const FVector Eye = Focus - FRotator(Pitch, Yaw, 0.f).Vector() * Distance;
	Yaw += DeltaYaw;
	Pitch = FMath::Clamp(Pitch + DeltaPitch, -89.f, 89.f);
	Focus = Eye + FRotator(Pitch, Yaw, 0.f).Vector() * Distance;
	Document->ActiveCamera = -1;
}

void UOKStudioGame::Pan(float Right, float Up)
{
	const FRotator R(Pitch, Yaw, 0.f);
	const float Scale = Distance * 0.002f * FOKStudioSettings::Get().PanSpeed;
	Focus += (FRotationMatrix(R).GetUnitAxis(EAxis::Y) * Right + FRotationMatrix(R).GetUnitAxis(EAxis::Z) * Up) * Scale;
	Document->ActiveCamera = -1;
}

void UOKStudioGame::Zoom(float Steps)
{
	Distance = FMath::Clamp(Distance * FMath::Pow(0.88f, Steps * FOKStudioSettings::Get().ZoomSpeed), 20.f, 5000.f);
	Document->ActiveCamera = -1;
}

void UOKStudioGame::SetStoryMode(bool bOn)
{
	if (bOn == bStoryMode) return;
	bStoryMode = bOn;
	if (!PanelHost) return;
	if (bOn) { PanelVisibility = PanelHost->GetVisibility(); PanelHost->SetVisibility(EVisibility::Collapsed); }
	else PanelHost->SetVisibility(PanelVisibility);
}

void UOKStudioGame::UpdateViewer()
{
	if (!Viewer) return;
	UCameraComponent* ViewerCamera = Viewer->GetCameraComponent();
	// The story places the viewer itself.
	if (bStoryMode)
	{
		if (SceneActor) SceneActor->PlaceBackground(Viewer->GetActorTransform(), ViewerCamera->FieldOfView);
		return;
	}
	UOKStudioViewCamera* Near = Cast<UOKStudioViewCamera>(ViewerCamera);
	const FOKSceneObject* ActiveObject = Document ? Document->Find(Document->ActiveCamera) : nullptr;
	if (Near) Near->NearClip = ActiveObject && ActiveObject->Kind == EOKSceneObjectKind::Camera ? ActiveObject->Camera.Near : 0.f;
	if (const UCameraComponent* Active = SceneActor ? SceneActor->ActiveCamera() : nullptr)
	{
		Viewer->SetActorTransform(Active->GetComponentTransform());
		ViewerCamera->SetFieldOfView(Active->FieldOfView);
		if (SceneActor) SceneActor->PlaceBackground(Viewer->GetActorTransform(), ViewerCamera->FieldOfView);
		return;
	}
	const FRotator R(Pitch, Yaw, 0.f);
	Viewer->SetActorLocationAndRotation(Focus - R.Vector() * Distance, R);
	ViewerCamera->SetFieldOfView(40.f);
	if (SceneActor) SceneActor->PlaceBackground(Viewer->GetActorTransform(), ViewerCamera->FieldOfView);
}

FString UOKStudioGame::FfmpegPath()
{
	return FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("Tools/ffmpeg/ffmpeg.exe"));
}

bool UOKStudioGame::EncodeMp4(const FString& FramesDir, float Fps, const FString& OutFile, FString& OutEncoder, FString& OutError)
{
	const FString Ffmpeg = FfmpegPath();
	if (!FPaths::FileExists(Ffmpeg)) { OutError = TEXT("ffmpeg 不在 ") + Ffmpeg + TEXT("（第三方工具不進版控：請放一份 LGPL 版 ffmpeg.exe 到 Tools/ffmpeg）"); return false; }
	// Hardware encoders first (fast), the software OpenH264 last (always there in the LGPL build).
	struct FEncoder { const TCHAR* Name; const TCHAR* Options; };
	static const FEncoder Encoders[] = {
		{ TEXT("h264_nvenc"), TEXT("-preset p5 -cq 19") },
		{ TEXT("h264_amf"), TEXT("-quality quality -rc cqp -qp_i 18 -qp_p 20") },
		{ TEXT("h264_mf"), TEXT("-b:v 16M") },
		{ TEXT("libopenh264"), TEXT("-b:v 16M") },
	};
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(OutFile), true);
	FString Tried;
	for (const FEncoder& E : Encoders)
	{
		const FString Args = FString::Printf(TEXT("-hide_banner -loglevel error -y -framerate %g -i \"%s\" -vf \"pad=ceil(iw/2)*2:ceil(ih/2)*2\" -c:v %s %s -pix_fmt yuv420p -movflags +faststart \"%s\""),
			Fps, *(FramesDir / TEXT("frame_%05d.png")), E.Name, E.Options, *OutFile);
		int32 Code = -1;
		FString Out, Err;
		FPlatformProcess::ExecProcess(*Ffmpeg, *Args, &Code, &Out, &Err);
		if (Code == 0 && IFileManager::Get().FileSize(*OutFile) > 0) { OutEncoder = E.Name; return true; }
		Tried += FString::Printf(TEXT("%s: %s; "), E.Name, *Err.Left(200).TrimStartAndEnd());
	}
	OutError = TEXT("no H.264 encoder worked: ") + Tried;
	return false;
}

void UOKStudioGame::CancelExport()
{
	if (ExportFrame < 0) return;
	UE_LOG(LogTemp, Display, TEXT("OK studio: export cancelled after %d of %d frames (%s)"), ExportFrame, ExportTotal, *ExportDir);
	ExportFrame = -1;
	LastMovieError = TEXT("cancelled");
}

FString UOKStudioGame::ExportFrames(float Fps, int32 Width, int32 Height, bool bMp4, float From, float To, const FString& Folder)
{
	bExportMp4 = bMp4;
	LastMovie.Reset();
	LastMovieError.Reset();
	ExportFps = FMath::Clamp(Fps, 1.f, 120.f);
	ExportWidth = Width; ExportHeight = Height;
	ExportFrom = FMath::Clamp(From, 0.f, Document->Duration);
	const float Until = To > ExportFrom ? FMath::Min(To, Document->Duration) : Document->Duration;
	ExportTotal = FMath::Max(1, FMath::CeilToInt((Until - ExportFrom) * ExportFps));
	const FString Parent = Folder.IsEmpty() ? FPaths::ProjectDir() / TEXT("UserData/Studio/movie") : Folder;
	ExportDir = FPaths::ConvertRelativePathToFull(Parent / FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S")));
	IFileManager::Get().MakeDirectory(*ExportDir, true);
	ExportFrame = 0;
	bExportShotPending = false;
	bTimelinePlaying = false;
	return ExportDir;
}

void UOKStudioGame::TickExport()
{
	// One frame per tick: pose the scene at the frame's time and render it to a PNG (synchronously).
	if (ExportFrame < 0) return;
	if (ExportFrame >= ExportTotal)
	{
		ExportFrame = -1;
		UE_LOG(LogTemp, Display, TEXT("OK studio: exported %d frames to %s"), ExportTotal, *ExportDir);
		if (bExportMp4)
		{
			// The frames become one mp4 beside their folder; the folder goes once the movie is written.
			const FString Movie = ExportDir + TEXT(".mp4");
			FString Encoder;
			if (EncodeMp4(ExportDir, ExportFps, Movie, Encoder, LastMovieError))
			{
				LastMovie = Movie;
				IFileManager::Get().DeleteDirectory(*ExportDir, false, true);
				UE_LOG(LogTemp, Display, TEXT("OK studio: movie %s (%s)"), *Movie, *Encoder);
			}
			else UE_LOG(LogTemp, Warning, TEXT("OK studio: movie not written: %s"), *LastMovieError);
		}
		return;
	}
	const float FrameTime = ExportFrom + ExportFrame / ExportFps;
	SetTimelineTime(FrameTime);
	SyncNow();
	for (const FOKSceneObject& O : Document->Objects) if (O.Kind == EOKSceneObjectKind::Character) SceneActor->SetAnimationTime(O.Id, FrameTime);
	FString Error;
	if (!CaptureToPng(ExportDir / FString::Printf(TEXT("frame_%05d.png"), ExportFrame), ExportWidth, ExportHeight, Error))
	{
		UE_LOG(LogTemp, Warning, TEXT("OK studio: frame %d not exported: %s"), ExportFrame, *Error);
		ExportFrame = -1;
		return;
	}
	++ExportFrame;
}

void UOKStudioGame::SyncNow()
{
	bSyncPending = false;
	// A few batches at once (small scenes are complete on return); a large scene goes on building over the next frames.
	for (int32 Round = 0; SceneActor && Round < 8 && (Round == 0 || SceneActor->HasPendingBuilds()); ++Round) SceneActor->Sync();
	if (Panel) Panel->RefreshRows();
}

void UOKStudioGame::SetTimelineTime(float Time)
{
	TimelineTime = FMath::Clamp(Time, 0.f, FMath::Max(Document->Duration, 0.01f));
    Document->ItemAnimationSeconds = TimelineTime;
	// Posing the scene for a time is not an edit (no undo step, no unsaved mark).
	TGuardValue<bool> Evaluating(bEvaluatingTimeline, true);
	Document->EvaluateTimeline(TimelineTime);
}

FString UOKStudioGame::ExportOkAnim(int32 Id, float Fps, FString& OutError)
{
	const FOKSceneObject* Object = Document->Find(Id);
	if (!Object || Object->Kind != EOKSceneObjectKind::Character) { OutError = TEXT("not a character"); return FString(); }
	SyncNow();
	AKKPreviewActor* First = SceneActor ? SceneActor->PreviewOf(Id) : nullptr;
	if (!First || !First->Session.IsOpen()) { OutError = TEXT("character not loaded"); return FString(); }
	void* Recorder = FOkAnimBridge::RecordStart(First->Session);
	if (!Recorder) { OutError = FOkAnimBridge::LastError(); return FString(); }
	const float SavedTime = TimelineTime;
	const float Length = FMath::Max(Document->Duration, 0.01f);
	const int32 Frames = FMath::Max(2, FMath::RoundToInt(Length * Fps) + 1);
	for (int32 F = 0; F < Frames && OutError.IsEmpty(); ++F)
	{
		const float T = FMath::Min(F / Fps, Length);
		SetTimelineTime(T);
		SyncNow();
		SceneActor->SetAnimationTime(Id, T);
		AKKPreviewActor* P = SceneActor->PreviewOf(Id);
		if (!P || !FOkAnimBridge::RecordFrame(Recorder, P->Session)) OutError = FOkAnimBridge::LastError();
	}
	TArray<uint8> Bytes;
	const FString Name = Object->Name.IsEmpty() ? FString::Printf(TEXT("character_%d"), Id) : Object->Name;
	if (OutError.IsEmpty() && !FOkAnimBridge::RecordFinish(Recorder, Fps, Name, Bytes)) OutError = FOkAnimBridge::LastError();
	FOkAnimBridge::RecordFree(Recorder);
	SetTimelineTime(SavedTime);
	SyncNow();
	SceneActor->SetAnimationTime(Id, SavedTime);
	if (!OutError.IsEmpty()) return FString();
	const FString Path = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("UserData/Studio/anim")) / (FPaths::MakeValidFileName(Name) + FDateTime::Now().ToString(TEXT("_%Y%m%d_%H%M%S.okanim")));
	if (!FFileHelper::SaveArrayToFile(Bytes, *Path)) { OutError = TEXT("cannot write ") + Path; return FString(); }
	return Path;
}

bool UOKStudioGame::CaptureToPng(const FString& Path, int32 Width, int32 Height, FString& OutError, bool bTransparent)
{
	if (!Viewer || !SceneActor) { OutError = TEXT("studio not running"); return false; }
	Width = FMath::Clamp(Width, 16, 8192);
	Height = FMath::Clamp(Height, 16, 8192);
	UpdateViewer();
	UCameraComponent* Camera = Viewer->GetCameraComponent();
	TArray<FColor> Pixels;
	if (!FOKSceneCard::Capture(SceneActor, Camera->GetComponentTransform(), Camera->FieldOfView, &Camera->PostProcessSettings, Width, Height, bTransparent, Pixels, OutError)) return false;
	TArray64<uint8> Png;
	FImageUtils::PNGCompressImageArray(Width, Height, TArrayView64<const FColor>(Pixels.GetData(), Pixels.Num()), Png);
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
	if (Png.IsEmpty() || !FFileHelper::SaveArrayToFile(Png, *Path)) { OutError = TEXT("cannot write ") + Path; return false; }
	return true;
}

FString UOKStudioGame::Screenshot(int32 Width, int32 Height, bool bTransparent)
{
	const FString Path = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("UserData/Studio/screenshot")) / FDateTime::Now().ToString(bTransparent ? TEXT("okstudio_%Y%m%d_%H%M%S_alpha.png") : TEXT("okstudio_%Y%m%d_%H%M%S.png"));
	FString Error;
	if (CaptureToPng(Path, Width, Height, Error, bTransparent)) return Path;
	UE_LOG(LogTemp, Warning, TEXT("OK studio screenshot: %s"), *Error);
	return FString();
}

namespace
{
	/** OKStudio.RunScript: console lines run one per frame once the studio runs; "wait N" waits N frames, "shot NAME" saves
	 *  the window with its UI to Saved/StudioUIRef/ok/NAME.png, "quit" exits. Lets UI checks run without the mouse. */
	TArray<FString> GUiScript;
	int32 GUiScriptWait = 0;
}

void UOKStudioGame::Tick(float DeltaTime)
{
	if (bStopRequested) { Stop(); return; }
	if (GUiScriptWait > 0) --GUiScriptWait;
	else while (GUiScript.Num() && GUiScriptWait == 0)
	{
		const FString Line = GUiScript[0].TrimStartAndEnd();
		GUiScript.RemoveAt(0);
		if (Line.IsEmpty() || Line.StartsWith(TEXT("#"))) continue;
		if (Line.StartsWith(TEXT("wait "))) GUiScriptWait = FMath::Max(1, FCString::Atoi(*Line.Mid(5)));
		else if (Line.StartsWith(TEXT("shot ")))
		{
			FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("StudioUIRef/ok") / Line.Mid(5) + TEXT(".png"), true, false);
			GUiScriptWait = 5;
		}
		else if (Line == TEXT("quit")) { FPlatformMisc::RequestExit(false); GUiScript.Reset(); }
		else if (GEngine) GEngine->Exec(SceneActor ? SceneActor->GetWorld() : nullptr, *Line);
	}
	{QUICK_SCOPE_CYCLE_COUNTER(STAT_OKStudio_ApplyPost);if (Viewer && Document) Document->Post.ApplyTo(Viewer->GetCameraComponent()->PostProcessSettings);}
	if (Viewer && SceneActor) { SceneActor->ViewLocation = Viewer->GetActorLocation(); SceneActor->bHasViewLocation = true; }
	FOKStudioThumbnails::Get().Tick();
	if (Panel) Panel->TickUi(DeltaTime);
	// Free fly: W A S D forward / sideways, Q E down / up, while the right button is held in the view.
	if (bFlying && FlyKeys.Num())
	{
		const FRotator R(Pitch, Yaw, 0.f);
		FVector Move = FVector::ZeroVector;
		if (FlyKeys.Contains(EKeys::W)) Move += R.Vector();
		if (FlyKeys.Contains(EKeys::S)) Move -= R.Vector();
		if (FlyKeys.Contains(EKeys::D)) Move += FRotationMatrix(R).GetUnitAxis(EAxis::Y);
		if (FlyKeys.Contains(EKeys::A)) Move -= FRotationMatrix(R).GetUnitAxis(EAxis::Y);
		if (FlyKeys.Contains(EKeys::E)) Move += FVector::UpVector;
		if (FlyKeys.Contains(EKeys::Q)) Move -= FVector::UpVector;
		Focus += Move.GetSafeNormal() * FOKStudioSettings::Get().FlySpeed * DeltaTime;
		Document->ActiveCamera = -1;
	}
	TickExport();
	if (bTimelinePlaying && Document)
	{
		const float Length = FMath::Max(Document->Duration, 0.01f);
		float Next = TimelineTime + DeltaTime * TimelineSpeed;
		// A loop range plays inside itself.
		const float Begin = LoopEnd > LoopStart ? LoopStart : 0.f, End = LoopEnd > LoopStart ? FMath::Min(LoopEnd, Length) : Length;
		if (Next > End) Next = Begin + FMath::Fmod(Next - Begin, FMath::Max(End - Begin, 0.01f));
		SetTimelineTime(Next);
	}
	// Auto backup every few minutes, not while something is moving or loading.
	const int32 Minutes = FOKStudioSettings::Get().BackupMinutes;
	if (Minutes > 0 && FPlatformTime::Seconds() - LastBackupSeconds > Minutes * 60. && BusyReason().IsEmpty() && !bStoryMode) BackupNow(false);
	{QUICK_SCOPE_CYCLE_COUNTER(STAT_OKStudio_Sync);if (bSyncPending) SyncNow();}
	{QUICK_SCOPE_CYCLE_COUNTER(STAT_OKStudio_History);if (bHistoryDirty && FPlatformTime::Seconds() - LastChangeSeconds > 0.4) CommitHistory();}
	QUICK_SCOPE_CYCLE_COUNTER(STAT_OKStudio_UpdateViewer);
	UpdateViewer();
}


bool UOKStudioGame::RunUiCommand(const FString& Id, const FString& Arg) { return Panel && Panel->RunCommand(Id, Arg); }
FString UOKStudioGame::UiStateJson() const { return Panel ? Panel->StateJson() : FString(); }
FString UOKStudioGame::UiLayoutJson() const { return Panel ? Panel->LayoutJson() : FString(); }

namespace
{
	UWorld* GameWorld()
	{
		return GEngine && GEngine->GameViewport ? GEngine->GameViewport->GetWorld() : nullptr;
	}

	FAutoConsoleCommand UiCommand(TEXT("OKStudio.Cmd"),
		TEXT("OKStudio.Cmd <id> [arg]: runs a studio UI command, as its button or shortcut does (OKStudio.UiState lists the ids)."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			UOKStudioGame* Game = UOKStudioGame::Get();
			if (!Game || Args.IsEmpty()) { UE_LOG(LogTemp, Warning, TEXT("OKStudio.Cmd: no studio running or no command")); return; }
			FString Arg;
			for (int32 I = 1; I < Args.Num(); ++I) Arg += (I > 1 ? TEXT(" ") : TEXT("")) + Args[I];
			const bool bOk = Game->RunUiCommand(Args[0], Arg);
			UE_LOG(LogTemp, Display, TEXT("OKStudio.Cmd %s %s: %s"), *Args[0], *Arg, bOk ? TEXT("ok") : TEXT("failed"));
		}));

	FAutoConsoleCommand UiStateCommand(TEXT("OKStudio.UiState"),
		TEXT("OKStudio.UiState: the studio UI's windows, menus, toggles and command ids (Saved/OKStudio/ui_state.json)."),
		FConsoleCommandDelegate::CreateLambda([]
		{
			UOKStudioGame* Game = UOKStudioGame::Get();
			if (!Game) { UE_LOG(LogTemp, Warning, TEXT("OKStudio.UiState: no studio running")); return; }
			const FString Json = Game->UiStateJson();
			FFileHelper::SaveStringToFile(Json, *(FPaths::ProjectSavedDir() / TEXT("OKStudio/ui_state.json")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
			UE_LOG(LogTemp, Display, TEXT("OKStudio.UiState %s"), *Json);
		}));

	FAutoConsoleCommand UiLayoutCommand(TEXT("OKStudio.UiLayout"),
		TEXT("OKStudio.UiLayout <name>: writes the studio UI's widget rects to Saved/StudioUIRef/ok_layout/<name>.json (Tools/studio_layout_spec.lua --compare)."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			UOKStudioGame* Game = UOKStudioGame::Get();
			if (!Game) { UE_LOG(LogTemp, Warning, TEXT("OKStudio.UiLayout: no studio running")); return; }
			const FString File = FPaths::ProjectSavedDir() / TEXT("StudioUIRef/ok_layout") / (Args.Num() ? Args[0] : FString(TEXT("layout"))) + TEXT(".json");
			FFileHelper::SaveStringToFile(Game->UiLayoutJson(), *File, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
			UE_LOG(LogTemp, Display, TEXT("OKStudio.UiLayout %s"), *FPaths::ConvertRelativePathToFull(File));
		}));

	FAutoConsoleCommand UiScriptCommand(TEXT("OKStudio.RunScript"),
		TEXT("OKStudio.RunScript <file>: runs its console lines one per frame once the studio runs (wait N, shot NAME, quit)."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			if (Args.IsEmpty() || !FFileHelper::LoadFileToStringArray(GUiScript, *Args[0])) { UE_LOG(LogTemp, Warning, TEXT("OKStudio.RunScript: cannot read %s"), Args.Num() ? *Args[0] : TEXT("")); return; }
			GUiScriptWait = 0;
			UE_LOG(LogTemp, Display, TEXT("OKStudio.RunScript: %d lines"), GUiScript.Num());
		}));

	FAutoConsoleCommand PlayCommand(TEXT("OKStudio.Play"),
		TEXT("OKStudio.Play [scene.okscene]: opens OK Studio in the running game (no file: a new scene)."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			FString Error;
			if (!UOKStudioGame::Start(GameWorld(), Args.Num() ? Args[0] : FString(), Error)) UE_LOG(LogTemp, Error, TEXT("OKStudio.Play: %s"), *Error);
		}));

	FAutoConsoleCommand StopCommand(TEXT("OKStudio.Stop"), TEXT("OKStudio.Stop: closes OK Studio in the game."),
		FConsoleCommandDelegate::CreateLambda([] { if (UOKStudioGame* Game = UOKStudioGame::Get()) Game->Stop(); }));

	FAutoConsoleCommand ImportCommand(TEXT("OKStudio.ImportKStudio"),
		TEXT("OKStudio.ImportKStudio <scene.png>: imports a KStudio scene into UserData/Studio/scene (OK cards for its characters)."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			if (Args.IsEmpty()) { UE_LOG(LogTemp, Error, TEXT("OKStudio.ImportKStudio: scene path required")); return; }
			FString Scene, Error;
			FOKStudioImport::FReport Report;
			if (FOKStudioImport::ImportToFile(FString::Join(Args, TEXT(" ")), Scene, Report, Error)) { UE_LOG(LogTemp, Display, TEXT("OKStudio.ImportKStudio: %s -> %s %s"), *Args[0], *Scene, *Report.ToJson()); }
			else { UE_LOG(LogTemp, Error, TEXT("OKStudio.ImportKStudio: %s"), *Error); }
		}));

	/** Acceptance sample: imports every Nth scene of a folder and writes Saved/OKStudio/import_sample.txt. */
	FAutoConsoleCommand ImportSampleCommand(TEXT("OKStudio.ImportKStudioSample"),
		TEXT("OKStudio.ImportKStudioSample <folder> <count> [exit]: imports an evenly spread sample of KStudio scenes and reports what carried over."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			if (Args.Num() < 2) { UE_LOG(LogTemp, Error, TEXT("OKStudio.ImportKStudioSample: folder and count required")); return; }
			TArray<FString> Names;
			IFileManager::Get().FindFiles(Names, *(Args[0] / TEXT("*.png")), true, false);
			Names.Sort();
			const int32 Count = FMath::Clamp(FCString::Atoi(*Args[1]), 1, FMath::Max(1, Names.Num()));
			TArray<FString> Lines;
			int32 Ok = 0, Chars = 0, Cards = 0, Items = 0, Catalog = 0;
			double Seconds = 0.;
			// What did not carry over, by kind (the note without its numbers), over the whole sample.
			TMap<FString, int32> NoteKinds;
			for (int32 I = 0; I < Count && Names.Num(); ++I)
			{
				const FString Png = Args[0] / Names[int64(I) * Names.Num() / Count];
				FString Scene, Error;
				FOKStudioImport::FReport Report;
				const double T0 = FPlatformTime::Seconds();
				const bool bOk = FOKStudioImport::ImportToFile(Png, Scene, Report, Error);
				Ok += bOk; Chars += Report.Characters; Cards += Report.CharacterCards; Items += Report.Items; Catalog += Report.CatalogItems;
				Seconds += FPlatformTime::Seconds() - T0;
				for (const FString& Note : Report.Notes)
				{
					FString Kind;
					for (const TCHAR C : Note) if (!FChar::IsDigit(C)) Kind.AppendChar(C);
					NoteKinds.FindOrAdd(Kind.Left(90)) += 1;
				}
				Lines.Add(FString::Printf(TEXT("%s %s chars=%d cards=%d items=%d catalog=%d lights=%d cameras=%d map=%d %.1fs %s"), bOk ? TEXT("OK  ") : TEXT("FAIL"), *FPaths::GetCleanFilename(Png),
					Report.Characters, Report.CharacterCards, Report.Items, Report.CatalogItems, Report.Lights, Report.Cameras, Report.Map, FPlatformTime::Seconds() - T0, *Error));
				UE_LOG(LogTemp, Display, TEXT("OK studio import sample: %s"), *Lines.Last());
			}
			NoteKinds.ValueSort([](int32 L, int32 R) { return L > R; });
			int32 Shown = 0;
			for (const TPair<FString, int32>& K : NoteKinds) if (Shown++ < 25) Lines.Insert(FString::Printf(TEXT("NOTE %5d  %s"), K.Value, *K.Key), Shown - 1);
			Lines.Insert(FString::Printf(TEXT("SAMPLE scenes=%d imported=%d characters=%d ok_cards=%d items=%d catalog_items=%d %.0fs"), Count, Ok, Chars, Cards, Items, Catalog, Seconds), 0);
			FFileHelper::SaveStringArrayToFile(Lines, *(FPaths::ProjectSavedDir() / TEXT("OKStudio/import_sample.txt")));
			if (Args.Contains(TEXT("exit"))) FPlatformMisc::RequestExit(false);
		}));

	FAutoConsoleCommand CompareKStudioCommand(TEXT("OKStudio.CompareKStudio"),
		TEXT("OKStudio.CompareKStudio <folder|samples> <count> [frames] [exit]: an evenly spread sample of KStudio scenes (samples: the official scenes kept in the OK base library), each pictured from its Studio camera beside the scene card's own picture (Saved/OKStudio/compare)."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			if (Args.Num() < 2) { UE_LOG(LogTemp, Error, TEXT("OKStudio.CompareKStudio: folder and count required")); return; }
			const bool bExit = Args.Contains(TEXT("exit"));
			const FString Folder = Args[0].Equals(TEXT("samples"), ESearchCase::IgnoreCase) ? AOKSceneActor::LibraryDirectory() / TEXT("studio/samples") : Args[0];
			TArray<FString> Names, Scenes2;
			IFileManager::Get().FindFiles(Names, *(Folder / TEXT("*.png")), true, false);
			// OK scenes too (no card picture: the left half stays black), for diagnosing a part of a scene.
			IFileManager::Get().FindFiles(Scenes2, *(Folder / TEXT("*.okscene")), true, false);
			Names.Append(Scenes2);
			Names.Sort();
			const int32 Count = FMath::Clamp(FCString::Atoi(*Args[1]), 1, FMath::Max(1, Names.Num()));
			TArray<FString> Scenes;
			for (int32 I = 0; I < Count && Names.Num(); ++I) Scenes.Add(Folder / Names[int64(I) * Names.Num() / Count]);
			TSharedRef<int32> Frames = MakeShared<int32>(0);
			const int32 WaitFrames = Args.Num() > 2 && Args[2].IsNumeric() ? FMath::Max(1, FCString::Atoi(*Args[2])) : 45;
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([bExit, Frames, Scenes, WaitFrames](float)
			{
				if (++*Frames < 30) return true;
				FString Error;
				UOKStudioGame* Game = UOKStudioGame::Start(GameWorld(), FString(), Error);
				if (Game) Game->RunKStudioCompare(Scenes, WaitFrames, bExit);
				else { UE_LOG(LogTemp, Error, TEXT("OK studio compare: %s"), *Error); if (bExit) FPlatformMisc::RequestExit(false); }
				return false;
			}), 0.1f);
		}));

	FAutoConsoleCommand StressItemsCommand(TEXT("OKStudio.StressItems"),
		TEXT("OKStudio.StressItems <count> [group/category/no] [exit]: adds copies of one item in batches of 50, logging memory and live object counts."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			const bool bExit = Args.Contains(TEXT("exit"));
			const int32 Count = Args.Num() > 0 && Args[0].IsNumeric() ? FCString::Atoi(*Args[0]) : 600;
			const FString Item = Args.Num() > 1 && Args[1].Contains(TEXT("/")) ? Args[1] : FString(TEXT("3/18/188"));
			TSharedRef<int32> Frames = MakeShared<int32>(0);
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([bExit, Frames, Count, Item](float)
			{
				if (++*Frames < 30) return true;
				FString Error;
				UOKStudioGame* Game = UOKStudioGame::Start(GameWorld(), FString(), Error);
				if (Game) Game->RunStressItems(Count, Item, bExit);
				else { UE_LOG(LogTemp, Error, TEXT("OK studio stress: %s"), *Error); if (bExit) FPlatformMisc::RequestExit(false); }
				return false;
			}), 0.1f);
		}));

	FAutoConsoleCommand PatternTestCommand(TEXT("OKStudio.PatternTest"),
		TEXT("OKStudio.PatternTest [exit]: starts OK Studio and checks over real frames that an item pattern reaches its material."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			const bool bExit = Args.Contains(TEXT("exit"));
			TSharedRef<int32> Frames = MakeShared<int32>(0);
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([bExit, Frames](float)
			{
				if (++*Frames < 30) return true;
				FString Error;
				UOKStudioGame* Game = UOKStudioGame::Start(GameWorld(), FString(), Error);
				if (Game) Game->RunPatternTest(bExit);
				else { UE_LOG(LogTemp, Error, TEXT("OK studio pattern test: %s"), *Error); if (bExit) FPlatformMisc::RequestExit(false); }
				return false;
			}), 0.1f);
		}));

	FAutoConsoleCommand PostTestCommand(TEXT("OKStudio.PostTest"),
		TEXT("OKStudio.PostTest [exit]: starts OK Studio and checks Studio's fog, ambient occlusion and sun shafts (Saved/OKStudio/post_*.png)."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			const bool bExit = Args.Contains(TEXT("exit"));
			TSharedRef<int32> Frames = MakeShared<int32>(0);
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([bExit, Frames](float)
			{
				if (++*Frames < 30) return true;
				FString Error;
				UOKStudioGame* Game = UOKStudioGame::Start(GameWorld(), FString(), Error);
				if (Game) Game->RunPostTest(bExit);
				else { UE_LOG(LogTemp, Error, TEXT("OK studio post test: %s"), *Error); if (bExit) FPlatformMisc::RequestExit(false); }
				return false;
			}), 0.1f);
		}));

	FAutoConsoleCommand SkyTestCommand(TEXT("OKStudio.SkyTest"),
		TEXT("OKStudio.SkyTest [exit]: starts OK Studio and pictures every Studio sky pattern (Saved/OKStudio/sky_*.png), then the sky off."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			const bool bExit = Args.Contains(TEXT("exit"));
			TSharedRef<int32> Frames = MakeShared<int32>(0);
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([bExit, Frames](float)
			{
				if (++*Frames < 30) return true;
				FString Error;
				UOKStudioGame* Game = UOKStudioGame::Start(GameWorld(), FString(), Error);
				if (Game) Game->RunSkyTest(bExit);
				else { UE_LOG(LogTemp, Error, TEXT("OK studio sky test: %s"), *Error); if (bExit) FPlatformMisc::RequestExit(false); }
				return false;
			}), 0.1f);
		}));

	FAutoConsoleCommand EyeSetTestCommand(TEXT("OKStudio.EyeSetTest"),
		TEXT("OKStudio.EyeSetTest [exit]: starts OK Studio and checks a character's eyes patterns through their eye sets (gag eyes, pupil expression, blend shape)."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			const bool bExit = Args.Contains(TEXT("exit"));
			TSharedRef<int32> Frames = MakeShared<int32>(0);
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([bExit, Frames](float)
			{
				if (++*Frames < 30) return true;
				FString Error;
				UOKStudioGame* Game = UOKStudioGame::Start(GameWorld(), FString(), Error);
				if (Game) Game->RunEyeSetTest(bExit);
				else { UE_LOG(LogTemp, Error, TEXT("OK studio eye set test: %s"), *Error); if (bExit) FPlatformMisc::RequestExit(false); }
				return false;
			}), 0.1f);
		}));

	FAutoConsoleCommand EffectColorTestCommand(TEXT("OKStudio.EffectColorTest"),
		TEXT("OKStudio.EffectColorTest [exit]: starts OK Studio and checks over real frames that an effect's colour reaches its particles."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			const bool bExit = Args.Contains(TEXT("exit"));
			TSharedRef<int32> Frames = MakeShared<int32>(0);
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([bExit, Frames](float)
			{
				if (++*Frames < 30) return true;
				FString Error;
				UOKStudioGame* Game = UOKStudioGame::Start(GameWorld(), FString(), Error);
				if (Game) Game->RunEffectColorTest(bExit);
				else { UE_LOG(LogTemp, Error, TEXT("OK studio effect colour test: %s"), *Error); if (bExit) FPlatformMisc::RequestExit(false); }
				return false;
			}), 0.1f);
		}));

	FAutoConsoleCommand SelfTestCommand(TEXT("OKStudio.GameSelfTest"),
		TEXT("OKStudio.GameSelfTest [exit]: starts OK Studio in the game and checks the scene tree, objects, viewer and files."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			const bool bExit = Args.Contains(TEXT("exit"));
			TSharedRef<int32> Frames = MakeShared<int32>(0);
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([bExit, Frames](float)
			{
				if (++*Frames < 30) return true;
				FString Error;
				UOKStudioGame* Game = UOKStudioGame::Start(GameWorld(), FString(), Error);
				if (Game) Game->RunSelfTest(bExit);
				else { UE_LOG(LogTemp, Error, TEXT("OK studio game selftest: %s"), *Error); if (bExit) FPlatformMisc::RequestExit(false); }
				return false;
			}), 0.1f);
		}));
}
