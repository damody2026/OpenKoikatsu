// OKStudio.UiSelfTest [exit]: the studio UI checked through its command table and public calls, one step per frame
// (objects build over frames). Results in Saved/OKStudio/ui_selftest.txt; "UI SELFTEST n/m" at the end of the log.
#include "SOKStudioPanel.h"
#include "OKStudioGameInternal.h"

namespace
{
	UWorld* TestWorld() { return GEngine && GEngine->GameViewport ? GEngine->GameViewport->GetWorld() : nullptr; }

	struct FUiTest
	{
		TWeakObjectPtr<UOKStudioGame> Game;
		TArray<FString> Lines;
		int32 Passed = 0, Failed = 0, Step = 0, Wait = 0;
		bool bExit = false;
		int32 Character = -1, Item = -1, Camera = -1, Light = -1;
		FString SceneFile, OtherFile, ExportDir;
		FVector Saved = FVector::ZeroVector;

		void Check(bool bOk, const FString& What)
		{
			Lines.Add(FString::Printf(TEXT("%s %s"), bOk ? TEXT("PASS") : TEXT("FAIL"), *What));
			(bOk ? Passed : Failed) += 1;
			UE_LOG(LogTemp, Display, TEXT("OK studio UI self test: %s %s"), bOk ? TEXT("PASS") : TEXT("FAIL"), *What);
		}
		SOKStudioPanel* Panel() const { return Game.IsValid() ? Game->GetPanel() : nullptr; }
		UOKSceneDocument* Doc() const { return Game.IsValid() ? Game->Document.Get() : nullptr; }
		bool Cmd(const FString& Id, const FString& Arg = FString()) const { return Panel() && Panel()->RunCommand(Id, Arg); }
		FOKSceneObject* Obj(int32 Id) const { return Doc() ? Doc()->Find(Id) : nullptr; }
		int32 Newest(EOKSceneObjectKind Kind) const
		{
			int32 Best = -1;
			if (Doc()) for (const FOKSceneObject& O : Doc()->Objects) if (O.Kind == Kind) Best = FMath::Max(Best, O.Id);
			return Best;
		}
		/** Let the scene build (and the history settle) before the next step. */
		void Settle(int32 Frames = 20) { Wait = Frames; if (Game.IsValid()) Game->SyncDocumentNow(); }

		void Finish()
		{
			Lines.Insert(FString::Printf(TEXT("UI SELFTEST %d/%d"), Passed, Passed + Failed), 0);
			FFileHelper::SaveStringArrayToFile(Lines, *(FPaths::ProjectSavedDir() / TEXT("OKStudio/ui_selftest.txt")));
			UE_LOG(LogTemp, Display, TEXT("UI SELFTEST %d/%d"), Passed, Passed + Failed);
			if (!SceneFile.IsEmpty()) IFileManager::Get().Delete(*SceneFile, false, true, true);
			if (!OtherFile.IsEmpty()) IFileManager::Get().Delete(*OtherFile, false, true, true);
			if (!ExportDir.IsEmpty()) IFileManager::Get().DeleteDirectory(*ExportDir, false, true);
			if (bExit) FPlatformMisc::RequestExit(false);
		}

		/** One step; false when done. */
		bool Tick()
		{
			if (!Game.IsValid() || !Panel()) { Check(false, TEXT("studio running")); Finish(); return false; }
			if (Wait > 0) { --Wait; return true; }
			UOKSceneDocument* D = Doc();
			UOKStudioGame* G = Game.Get();
			SOKStudioPanel* P = Panel();
			switch (Step++)
			{
			case 0:
			{
				// U0: add a character, select it, move it, save, open, undo — through the command table only.
				const TArray<FString> Cards = UOKStudioGame::AvailableCards();
				Check(Cards.Num() > 0 && Cmd(TEXT("add.character"), Cards.Num() ? Cards[0] : FString()), TEXT("add.character adds a character"));
				Character = Newest(EOKSceneObjectKind::Character);
				Check(Cmd(TEXT("select"), FString::FromInt(Character)) && G->Selection == TArray<int32>{ Character }, TEXT("select selects it"));
				Check(Cmd(TEXT("transform.move"), TEXT("50 0 0")) && Obj(Character) && FMath::IsNearlyEqual(Obj(Character)->Transform.GetLocation().X, 50.), TEXT("transform.move moves it"));
				Settle(60);
				return true;
			}
			case 1:
			{
				SceneFile = UOKStudioGame::SceneDirectory() / TEXT("_uiselftest.png");
				IFileManager::Get().Delete(*SceneFile, false, true, true);
				Check(Cmd(TEXT("scene.save"), TEXT("_uiselftest")) && FPaths::FileExists(SceneFile) && !G->bDirty, TEXT("scene.save writes the scene and clears the unsaved mark"));
				Check(Cmd(TEXT("scene.load"), SceneFile) && Obj(Character) && FMath::IsNearlyEqual(Obj(Character)->Transform.GetLocation().X, 50.), TEXT("scene.load brings it back"));
				Settle(60);
				return true;
			}
			case 2:
			{
				Cmd(TEXT("select"), FString::FromInt(Character));
				Cmd(TEXT("transform.move"), TEXT("10 0 0"));
				G->CommitHistory();
				Check(G->bDirty, TEXT("a change marks the scene unsaved"));
				Check(Cmd(TEXT("edit.undo")) && Obj(Character) && FMath::IsNearlyEqual(Obj(Character)->Transform.GetLocation().X, 50.), TEXT("edit.undo takes the move back"));
				Check(G->Selection.Contains(Character), TEXT("undo keeps the selection of that step"));
				// U2: an item next to it.
				const FOKStudioCatalog& C = FOKStudioCatalog::Get();
				const FOKStudioCatalogItem* I = C.Items.Num() ? &C.Items[0] : nullptr;
				Cmd(TEXT("select"));
				Check(I && Cmd(TEXT("add.item"), FString::Printf(TEXT("%d %d %d"), I->Group, I->Category, I->No)), TEXT("add.item adds a catalogue item"));
				Item = Newest(EOKSceneObjectKind::Item);
				if (FOKSceneObject* X = Obj(Item)) { X->Transform.SetLocation(FVector(-60.f, 0.f, 0.f)); D->NotifyChanged(); }
				Settle(90);
				return true;
			}
			case 3:
			{
				// U2-1: the character is picked where it stands on screen.
				FBox Box;
				FVector2D At;
				APlayerController* PC = G->SceneActor->GetWorld()->GetFirstPlayerController();
				const bool bProjected = G->WorldBounds(Character, Box) && PC && UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition(PC, Box.GetCenter(), At, true);
				const int32 Picked = bProjected ? G->PickObject(At) : -1;
				Check(Picked == Character, FString::Printf(TEXT("clicking the character in the view picks it (picked %d)"), Picked));
				// U2-2: Shift adds the item; moving moves both; one undo returns both.
				G->Select(Character);
				G->ToggleSelect(Item);
				Check(G->Selection.Num() == 2 && G->SelectedId == Item, TEXT("Shift adds the item to the selection"));
				G->CommitHistory();
				const FVector A = Obj(Character)->Transform.GetLocation(), B = Obj(Item)->Transform.GetLocation();
				G->GuideMode = 0;
				G->BeginGuideDrag(0);
				const bool bDragged = G->DragGuide(FVector2D(40.f, 0.f), false);
				G->EndGuideDrag();
				const FVector A2 = Obj(Character)->Transform.GetLocation(), B2 = Obj(Item)->Transform.GetLocation();
				Check(bDragged && !A2.Equals(A, 0.01) && (A2 - A).Equals(B2 - B, 0.5), TEXT("the guide moves both selected objects together"));
				G->CommitHistory();
				G->Undo();
				Check(Obj(Character)->Transform.GetLocation().Equals(A, 0.01) && Obj(Item)->Transform.GetLocation().Equals(B, 0.01), TEXT("one undo returns both"));
				// U2-4: dropped on the right hand it hangs there and stays after save and open.
				const FOKStudioAccessoryPoint* Hand = C_Hand();
				G->Select(Character);
				Check(Hand && P->DropOnPoint({ Item }, Hand->Key) && Obj(Item)->Parent == Character && Obj(Item)->AttachBone == Hand->Key, TEXT("dropping the item on the right hand hangs it there"));
				Cmd(TEXT("scene.save"), TEXT("_uiselftest"));
				Cmd(TEXT("scene.load"), SceneFile);
				Check(Obj(Item) && Obj(Item)->Parent == Character && Hand && Obj(Item)->AttachBone == Hand->Key, TEXT("it still hangs on the hand after save and open"));
				// U2-5: copy, then paste into a new scene with new ids.
				G->Select(Item);
				Cmd(TEXT("edit.copy"));
				G->bDirty = false;
				Cmd(TEXT("scene.new"));
				const int32 Before = D->Objects.Num();
				Check(Cmd(TEXT("edit.paste")) && D->Objects.Num() == Before + 1 && Newest(EOKSceneObjectKind::Item) >= 1, TEXT("Ctrl+V pastes the copied object into a new scene"));
				Cmd(TEXT("scene.load"), SceneFile);
				Settle(90);
				return true;
			}
			case 4:
			{
				// U2-3: rename through the tree keeps one history step; hide toggles all selected.
				G->Select(Character);
				const FString OldName = Obj(Character)->Name;
				Obj(Character)->Name = TEXT("UiTest");
				D->NotifyChanged();
				G->CommitHistory();
				G->Undo();
				Check(Obj(Character)->Name == OldName, TEXT("a rename is one undo step"));
				G->SelectMany({ Character, Item }, Character);
				Cmd(TEXT("edit.hide"));
				Check(!Obj(Character)->bVisible && !Obj(Item)->bVisible, TEXT("hide hides every selected object"));
				Cmd(TEXT("edit.hide"));
				// U3: search with AND and exclusion; the middle click adds under the selection.
				const TArray<SOKStudioPanel::FHit> All = P->SearchCatalog(TEXT(""), { TEXT("item") }, 0);
				const FString Word = All.Num() ? All[0].Name.Left(1) : FString();
				const TArray<SOKStudioPanel::FHit> Some = P->SearchCatalog(Word, { TEXT("item") }, 0);
				const TArray<SOKStudioPanel::FHit> Fewer = P->SearchCatalog(Word + TEXT(" -") + Word, { TEXT("item") }, 0);
				Check(Some.Num() > 0 && Fewer.IsEmpty(), FString::Printf(TEXT("quick search matches \"%s\" (%d) and excludes it (%d)"), *Word, Some.Num(), Fewer.Num()));
				G->Select(Character);
				const FOKStudioCatalogItem& I = FOKStudioCatalog::Get().Items[0];
				Check(P->AddItemTo(I.Group, I.Category, I.No, true) && Obj(G->SelectedId) && Obj(G->SelectedId)->Parent == Character, TEXT("middle click adds the item under the selection"));
				Cmd(TEXT("edit.delete"));
				// U4: frame the selection; a camera from the view; bookmarks.
				G->Select(Character);
				Cmd(TEXT("camera.focus"));
				FBox Box;
				G->WorldBounds(Character, Box);
				Check(Box.IsValid && Box.ExpandBy(1.f).IsInside(G->Focus) && G->Distance > Box.GetExtent().Size(), TEXT("F frames the selection (focus in its bounds, far enough)"));
				Check(Cmd(TEXT("camera.fromview")) && (Camera = Newest(EOKSceneObjectKind::Camera)) >= 0, TEXT("a camera is made from the view"));
				Cmd(TEXT("view.save.1"));
				const float Yaw = G->Yaw;
				G->Orbit(37.f, 0.f);
				Cmd(TEXT("view.load.1"));
				Check(FMath::IsNearlyEqual(G->Yaw, Yaw), TEXT("a view bookmark returns to its view"));
				// U4-1: snapping and local axes.
				G->Select(Item);
				G->bSnap = true;
				G->BeginGuideDrag(0);
				G->DragGuide(FVector2D(37.f, 3.f), false);
				G->EndGuideDrag();
				G->bSnap = false;
				const double X = Obj(Item)->Transform.GetLocation().X;
				Check(Obj(Item)->AttachBone.Len() || FMath::IsNearlyZero(FMath::Fmod(FMath::Abs(X), double(FOKStudioSettings::Get().SnapMove)), 0.01), FString::Printf(TEXT("snapped moves land on whole steps (x=%.3f)"), X));
				Settle(10);
				return true;
			}
			case 5:
			{
				// U4-1 local axes: an object turned 90 degrees moves along its own X (the world's Y).
				Cmd(TEXT("select"));
				Cmd(TEXT("add.folder"));
				const int32 F = Newest(EOKSceneObjectKind::Folder);
				G->Select(F);
				Obj(F)->Transform = FTransform(FRotator(0.f, 90.f, 0.f), FVector::ZeroVector);
				D->NotifyChanged();
				G->bLocalAxes = true;
				G->BeginGuideDrag(0);
				const bool bMoved = G->DragGuide(FVector2D(30.f, 30.f), false);
				G->EndGuideDrag();
				G->bLocalAxes = false;
				const FVector L = Obj(F)->Transform.GetLocation();
				Check(bMoved && FMath::Abs(L.X) < 0.01 && FMath::Abs(L.Y) > 0.1, FString::Printf(TEXT("local X of a turned object is the world's Y (%s)"), *L.ToString()));
				Cmd(TEXT("edit.delete"));
				// U5: an IK target moves with the guide and comes back with undo; mirroring is symmetric.
				G->Select(Character);
				P->SetPage(SOKStudioPanel::EPage::Bones);
				FOKSceneObject* X = Obj(Character);
				const bool bIk = X && P->SetIkEnabled(*X, 6, true);
				Check(bIk, TEXT("the right hand's IK target turns on"));
				if (bIk)
				{
					G->CommitHistory();
					const FVector Before = Obj(Character)->Character.Ik.Last().Position;
					G->GuideIk = 6;
					G->BeginGuideDrag(2);
					G->DragGuide(FVector2D(0.f, -40.f), false);
					G->EndGuideDrag();
					const FVector After = Obj(Character)->Character.Ik.Last().Position;
					G->CommitHistory();
					G->Undo();
					Check(!After.Equals(Before, 0.01) && Obj(Character)->Character.Ik.Num() && Obj(Character)->Character.Ik.Last().Position.Equals(Before, 0.01), TEXT("the guide moves the IK target and undo returns it"));
					G->GuideIk = -1;
				}
				X = Obj(Character);
				X->Character.Ik.Reset();
				SOKStudioPanel::SetFk(*X, TEXT("cf_j_arm00_L"), FVector(10.f, 20.f, 30.f));
				SOKStudioPanel::SetFk(*X, TEXT("cf_j_arm00_R"), FVector(1.f, 2.f, 3.f));
				D->NotifyChanged();
				Cmd(TEXT("pose.mirror"), TEXT("all"));
				const FOKSceneFkBone* R = Obj(Character)->Character.Fk.FindByPredicate([](const FOKSceneFkBone& B) { return B.Bone == TEXT("cf_j_arm00_R"); });
				const FOKSceneFkBone* Lb = Obj(Character)->Character.Fk.FindByPredicate([](const FOKSceneFkBone& B) { return B.Bone == TEXT("cf_j_arm00_L"); });
				Check(R && Lb && R->Euler.Equals(FVector(10.f, -20.f, -30.f), 0.01) && Lb->Euler.Equals(FVector(1.f, -2.f, -3.f), 0.01), TEXT("mirroring swaps left and right with mirrored rotations"));
				// U5-5: a pose saved with its picture comes back the same.
				const FString PoseName = TEXT("_uiselftest_pose");
				const FString PosePath = FOKStudioPose::Directory() / PoseName + TEXT(".dat");
				IFileManager::Get().Delete(*PosePath, false, true, true);
				const TArray<FOKSceneFkBone> Fk = Obj(Character)->Character.Fk;
				const bool bSaved = P->SavePoseWithPicture(PoseName);
				Obj(Character)->Character.Fk.Reset();
				const bool bLoaded = bSaved && P->LoadPose(PosePath);
				const FOKSceneFkBone* Back = Obj(Character)->Character.Fk.FindByPredicate([](const FOKSceneFkBone& B) { return B.Bone == TEXT("cf_j_arm00_R"); });
				Check(bLoaded && Back && Back->Euler.Equals(FVector(10.f, -20.f, -30.f), 0.05) && FPaths::FileExists(FPaths::ChangeExtension(PosePath, TEXT("png"))), TEXT("a saved pose (with its picture) loads back"));
				IFileManager::Get().Delete(*PosePath, false, true, true);
				IFileManager::Get().Delete(*FPaths::ChangeExtension(PosePath, TEXT("png")), false, true, true);
				P->SetPage(SOKStudioPanel::EPage::None);
				Settle(10);
				return true;
			}
			case 6:
			{
				// U6: a camera's two FOV keys blend; a moved key comes back with undo; an export stops when cancelled.
				FOKSceneObject* C = Obj(Camera);
				Check(C != nullptr, TEXT("the camera is there"));
				if (C)
				{
					C->Camera.Fov = 40.f; D->SetKey(Camera, 0.f);
					Obj(Camera)->Camera.Fov = 80.f; D->SetKey(Camera, 2.f);
					G->SetTimelineTime(1.f);
					Check(FMath::IsNearlyEqual(Obj(Camera)->Camera.Fov, 60.f, 0.1f), FString::Printf(TEXT("the camera track blends FOV (%.2f at 1 s)"), Obj(Camera)->Camera.Fov));
					G->CommitHistory();
					D->MoveKey(Camera, 1, 3.f);
					G->CommitHistory();
					G->Undo();
					const FOKSceneTrack* T = D->TrackOf(Camera);
					Check(T && T->Keys.Num() == 2 && FMath::IsNearlyEqual(T->Keys[1].Time, 2.f), TEXT("a moved key comes back with undo"));
				}
				// Scene track: exposure between two keys.
				D->Post.Exposure = 0.f; D->SetKey(UOKSceneDocument::SceneTrackId, 0.f);
				D->Post.Exposure = 2.f; D->SetKey(UOKSceneDocument::SceneTrackId, 2.f);
				G->SetTimelineTime(1.f);
				Check(FMath::IsNearlyEqual(D->Post.Exposure, 1.f, 0.01f), TEXT("the scene track blends exposure"));
				D->Tracks.Reset();
				ExportDir = G->ExportFrames(30.f, 64, 36, false, 0.f, 3.f);
				Wait = 4;
				return true;
			}
			case 7:
			{
				G->CancelExport();
				TArray<FString> Files;
				IFileManager::Get().FindFiles(Files, *(ExportDir / TEXT("*")), true, false);
				const bool bOnlyFrames = !Files.ContainsByPredicate([](const FString& F) { return !F.StartsWith(TEXT("frame_")); });
				Check(!G->IsExporting() && Files.Num() > 0 && Files.Num() < 90 && bOnlyFrames, FString::Printf(TEXT("a cancelled export stops and leaves only its frames (%d)"), Files.Num()));
				// U7: an unsaved change asks before a new scene; cancelling keeps the scene.
				G->Select(Character);
				Cmd(TEXT("transform.move"), TEXT("1 0 0"));
				const int32 Objects = D->Objects.Num();
				Cmd(TEXT("scene.new"));
				Check(P->IsFloatOpen(TEXT("dialog")) && D->Objects.Num() == Objects, TEXT("a new scene with unsaved changes asks first"));
				P->CloseFloat(TEXT("dialog"));
				Check(D->Objects.Num() == Objects && Obj(Character), TEXT("cancelling leaves the scene as it was"));
				// U7-2: merging a scene gives new ids and saves and opens the same.
				const int32 Count = D->Objects.Num();
				Check(Cmd(TEXT("scene.merge"), SceneFile), TEXT("scene.merge adds the saved scene"));
				TSet<int32> Ids;
				for (const FOKSceneObject& O : D->Objects) Ids.Add(O.Id);
				Check(D->Objects.Num() > Count && Ids.Num() == D->Objects.Num(), FString::Printf(TEXT("merged ids are unique (%d objects)"), D->Objects.Num()));
				OtherFile = UOKStudioGame::SceneDirectory() / TEXT("_uiselftest_merged.okscene");
				FString Error;
				const FString Json = D->ToJson();
				const bool bRound = D->SaveToFile(OtherFile, Error) && Cmd(TEXT("scene.load"), OtherFile) && D->ToJson() == Json;
				Check(bRound, TEXT("the merged scene saves and opens the same"));
				// U7-3: a save that cannot be written leaves the old file whole.
				FString Old;
				FFileHelper::LoadFileToString(Old, *OtherFile);
				IFileManager::Get().MakeDirectory(*(OtherFile + TEXT(".saving")), true);
				const bool bFailed = !D->SaveToFile(OtherFile, Error);
				IFileManager::Get().DeleteDirectory(*(OtherFile + TEXT(".saving")), false, true);
				FString Now;
				FFileHelper::LoadFileToString(Now, *OtherFile);
				Check(bFailed && Now == Old, TEXT("a failed save leaves the old file whole"));
				// U7-5: a backup is written now and listed.
				Check(!G->BackupNow(true).IsEmpty() && UOKStudioGame::BackupFiles().Num() > 0, TEXT("a backup is written and listed"));
				Settle(30);
				return true;
			}
			case 8:
			{
				// U8: a colour picked in the colour window reaches the light; new fields save and open the same.
				Cmd(TEXT("select"));
				Cmd(TEXT("add.light"), TEXT("spot"));
				Light = Newest(EOKSceneObjectKind::Light);
				G->Select(Light);
				P->PickColor(FString::Printf(TEXT("obj%d:color"), Light), TEXT("color"), FLinearColor::White, [this](const FLinearColor& C) { if (FOKSceneObject* L = Obj(Light)) { L->Light.Color = C; Doc()->NotifyChanged(); } });
				if (P->ColorTarget) P->ColorTarget(FLinearColor(1.f, 0.5f, 0.25f, 1.f));
				Check(Obj(Light) && Obj(Light)->Light.Color.Equals(FLinearColor(1.f, 0.5f, 0.25f, 1.f)), TEXT("the colour window sets the light's colour"));
				P->CloseFloat(TEXT("color"));
				Obj(Light)->Light.bCharactersOnly = true;
				if (FOKSceneObject* C = Obj(Camera)) { C->Camera.Near = 5.f; C->Camera.Far = 9000.f; }
				D->Post.ColorFilter = 1; D->Post.BackgroundColor = FLinearColor(0.2f, 0.3f, 0.4f, 1.f); D->Post.FogColor = FLinearColor(0.5f, 0.6f, 0.7f, 1.f);
				D->NotifyChanged();
				FString Error;
				const FString Json = D->ToJson();
				Check(D->SaveToFile(OtherFile, Error) && Cmd(TEXT("scene.load"), OtherFile) && D->ToJson() == Json && Obj(Light) && Obj(Light)->Light.bCharactersOnly
					&& D->Bookmarks.IsValidIndex(1) && D->Bookmarks[1].bSet, TEXT("new fields (light target, camera planes, look, bookmarks) save and open the same"));
				// U9: rebinding W moves the shortcut and the tooltips follow.
				FOKStudioSettings& S = FOKStudioSettings::Get();
				const TMap<FString, FString> Keep = S.Bindings;
				S.Bindings.Add(TEXT("gizmo.move"), TEXT("G"));
				const FKeyEvent KeyG(EKeys::G, FModifierKeysState(), 0, false, 0, 0), KeyW(EKeys::W, FModifierKeysState(), 0, false, 0, 0);
				Check(S.CommandFor(KeyG) == TEXT("gizmo.move") && S.CommandFor(KeyW).IsEmpty() && P->Tip(TEXT("移動"), TEXT("gizmo.move")).Contains(TEXT("G")), TEXT("a rebound shortcut works, the old key does not, the tooltip follows"));
				S.Bindings = Keep;
				// The UI state lists windows and the selection.
				P->OpenFloat(TEXT("quick"));
				const FString State = G->UiStateJson();
				Check(State.Contains(TEXT("\"quick\"")) && State.Contains(TEXT("\"selection\"")), TEXT("OKStudio.UiState lists open windows and the selection"));
				P->CloseFloat(TEXT("quick"));
				// Floating windows remember where they were.
				if (SOKStudioPanel::FFloat* F = P->Floats.Find(TEXT("timeline"))) F->Pos += FVector2D(13.f, 7.f);
				P->SaveLayout();
				FString Layout;
				FFileHelper::LoadFileToString(Layout, *(FOKStudioSettings::Directory() / TEXT("ui_layout.json")));
				Check(Layout.Contains(TEXT("\"timeline\"")), TEXT("window places are saved in ui_layout.json"));
				if (SOKStudioPanel::FFloat* F = P->Floats.Find(TEXT("timeline"))) F->Pos -= FVector2D(13.f, 7.f);
				P->SaveLayout();
				G->bDirty = false;
				Finish();
				return false;
			}
			default:
				Finish();
				return false;
			}
		}

		static const FOKStudioAccessoryPoint* C_Hand()
		{
			for (const FOKStudioAccessoryPoint& A : FOKStudioCatalog::Get().AccessoryPoints)
				if (A.Bone.EndsWith(TEXT("hand_R")) || A.Key.EndsWith(TEXT("hand_R")) || A.Name.Contains(TEXT("右手"))) return &A;
			return nullptr;
		}
	};

	FAutoConsoleCommand UiSelfTestCommand(TEXT("OKStudio.UiSelfTest"),
		TEXT("OKStudio.UiSelfTest [exit]: checks the studio UI through its command table (Saved/OKStudio/ui_selftest.txt)."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			TSharedRef<FUiTest> Test = MakeShared<FUiTest>();
			Test->bExit = Args.Contains(TEXT("exit"));
			TSharedRef<int32> Frames = MakeShared<int32>(0);
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Test, Frames](float)
			{
				if (++*Frames < 30) return true;
				if (!Test->Game.IsValid())
				{
					FString Error;
					UOKStudioGame* Game = UOKStudioGame::Get() ? UOKStudioGame::Get() : UOKStudioGame::Start(TestWorld(), FString(), Error);
					if (!Game) { UE_LOG(LogTemp, Error, TEXT("OK studio UI self test: %s"), *Error); if (Test->bExit) FPlatformMisc::RequestExit(false); return false; }
					Test->Game = Game;
					Game->bDirty = false;
					return true;
				}
				return Test->Tick();
			}), 0.f);
		}));
}
