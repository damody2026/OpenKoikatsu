// OK Studio panel: floating windows and their saved places, the dialog, the workspace tree's editing, scene files
// (browser, backups, the open report, the resource check, stories), screenshots, the timeline and its export, settings.
#include "SOKStudioPanel.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Misc/ConfigCacheIni.h"
#include "Widgets/SToolTip.h"
#include "Widgets/SWindow.h"

using namespace OKStudioStyle;

namespace
{
	FString LayoutFile() { return FOKStudioSettings::Directory() / TEXT("ui_layout.json"); }

	/** A list of text rows with a click action (reports, backups, stories). */
	TSharedRef<SWidget> TextList(const TArray<FString>& Lines, TFunction<void(int32)> OnClick, float Height)
	{
		TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);
		for (int32 I = 0; I < Lines.Num(); ++I)
			Box->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 1.f)
			[
				SNew(SButton).ButtonStyle(&FlatButton()).ContentPadding(FMargin(3.f, 1.f)).OnClicked_Lambda([OnClick, I] { if (OnClick) OnClick(I); return FReply::Handled(); })
				[SNew(STextBlock).Font(Font(10.f)).ColorAndOpacity(OKStudioStyle::Text()).AutoWrapText(true).Text(FText::FromString(Lines[I]))]
			];
		return SNew(SBox).HeightOverride(Height)[SNew(SBorder).BorderImage(Brush(ListArea())).Padding(2.f)[SNew(SScrollBox) + SScrollBox::Slot()[Box]]];
	}

	/** The id a report line starts with ("#12 ..."), or -1. */
	int32 LineId(const FString& Line)
	{
		if (!Line.StartsWith(TEXT("#"))) return -1;
		return FCString::Atoi(*Line.Mid(1));
	}
}

// --- Floating windows ---

void SOKStudioPanel::AddFloat(const FString& Id, const FString& Title, const FMargin& Rect, TFunction<TSharedRef<SWidget>()> Build)
{
	FFloat& F = Floats.Add(Id);
	F.Title = Title;
	F.Default = FVector2D(Rect.Left, Rect.Top);
	F.Pos = F.Default;
	F.Size = FVector2D(Rect.Right, Rect.Bottom);
	F.Build = MoveTemp(Build);
	SConstraintCanvas::FSlot* Slot = nullptr;
	const FString SlotTag = Id == TEXT("timeline") ? FString(TEXT("TimelinePanel")) : TEXT("Float_") + Id;
	Canvas->AddSlot().Expose(Slot).Alignment(FVector2D(0.f, 0.f)).Anchors(FAnchors(0.f, 0.f)).ZOrder(TopZ)
		.Offset(TAttribute<FMargin>::CreateLambda([this, Id] { const FFloat* X = Floats.Find(Id); return X ? FMargin(X->Pos.X, X->Pos.Y, X->Size.X, X->Size.Y) : FMargin(); }))
	[
		Tagged(*SlotTag, SNew(SBox).Visibility_Lambda([this, Id] { return IsFloatOpen(Id) ? EVisibility::Visible : EVisibility::Collapsed; })
		[
			SNew(SFrontArea).OnFront([this, Id] { if (FFloat* X = Floats.Find(Id)) BringToFront(X->Slot); OpenOrder.Remove(Id); OpenOrder.Add(Id); })
			[
				Window(TAttribute<FText>::CreateLambda([this, Id] { const FFloat* X = Floats.Find(Id); return FText::FromString(X ? X->Title : FString()); }), SAssignNew(F.Body, SBox), [this, Id] { CloseFloat(Id); }, Panel(),
					[this, Id](const FVector2D& D)
					{
						// Keep the title bar reachable inside the view.
						FFloat* X = Floats.Find(Id);
						if (!X || !Canvas) return;
						const FVector2D Area = Canvas->GetCachedGeometry().GetLocalSize();
						X->Pos = FVector2D(FMath::Clamp(X->Pos.X + D.X, 40.f - X->Size.X, FMath::Max(40.f, float(Area.X) - 40.f)), FMath::Clamp(X->Pos.Y + D.Y, 0.f, FMath::Max(0.f, float(Area.Y) - 23.f)));
						bLayoutDirty = true;
					})
			]
		])
	];
	F.Slot = Slot;
}

void SOKStudioPanel::RegisterFloats()
{
	// Default places: HS2Studio6's (layout spec) where it has the window, else beside the menus.
	AddFloat(TEXT("timeline"), TEXT("時間軸"), FMargin(160.f, 458.f, 520.f, 260.f), [this] { return BuildTimeline(); });
	AddFloat(TEXT("cards"), TEXT("角色卡"), FMargin(142.f, 36.f, 620.f, 420.f), [this] { return BuildCardBrowser(); });
	AddFloat(TEXT("scenes"), TEXT("場景檔"), FMargin(142.f, 36.f, 620.f, 420.f), [this] { return BuildSceneBrowser(); });
	AddFloat(TEXT("quick"), TEXT("快搜"), FMargin(140.f, 40.f, 360.f, 590.f), [this] { return BuildQuickSearch(); });
	AddFloat(TEXT("poselib"), TEXT("姿勢庫"), FMargin(140.f, 40.f, 360.f, 368.f), [this] { return BuildPoseLibrary(); });
	AddFloat(TEXT("effects"), TEXT("特效"), FMargin(142.f, 36.f, 280.f, 380.f), [this] { return BuildEffectList(); });
	AddFloat(TEXT("maps"), TEXT("地圖"), FMargin(142.f, 36.f, 320.f, 400.f), [this] { return BuildMapBrowser(); });
	AddFloat(TEXT("color"), TEXT("顏色"), FMargin(470.f, 60.f, 260.f, 330.f), [this] { return BuildColorPicker(); });
	AddFloat(TEXT("settings"), TEXT("設定"), FMargin(260.f, 60.f, 640.f, 460.f), [this] { return BuildSettings(); });
	AddFloat(TEXT("shot"), TEXT("截圖"), FMargin(470.f, 60.f, 300.f, 345.f), [this] { return BuildShot(); });
	AddFloat(TEXT("export"), TEXT("輸出動畫"), FMargin(470.f, 60.f, 340.f, 330.f), [this] { return BuildExport(); });
	AddFloat(TEXT("report"), TEXT("讀取報告"), FMargin(470.f, 60.f, 440.f, 320.f), [this] { return BuildReport(); });
	AddFloat(TEXT("resources"), TEXT("資源檢查"), FMargin(470.f, 60.f, 440.f, 360.f), [this] { return BuildResources(); });
	AddFloat(TEXT("backups"), TEXT("備份恢復"), FMargin(300.f, 60.f, 500.f, 400.f), [this] { return BuildBackups(); });
	AddFloat(TEXT("recent"), TEXT("最近的場景"), FMargin(300.f, 60.f, 520.f, 380.f), [this] { return BuildRecent(); });
	AddFloat(TEXT("stories"), TEXT("播放故事"), FMargin(300.f, 60.f, 420.f, 360.f), [this] { return BuildStories(); });
	AddFloat(TEXT("dialog"), TEXT("確認"), FMargin(640.f, 380.f, 340.f, 120.f), [this] { return BuildDialog(); });
}

void SOKStudioPanel::BringToFront(SConstraintCanvas::FSlot* Slot)
{
	if (!Slot) return;
	TopZ += 1.f;
	Slot->SetZOrder(TopZ);
}

void SOKStudioPanel::RebuildFloat(const FString& Id)
{
	FFloat* F = Floats.Find(Id);
	if (F && F->Body && F->Build) F->Body->SetContent(F->Build());
}

void SOKStudioPanel::DeferFloat(const FString& Id)
{
	RegisterActiveTimer(0.f, FWidgetActiveTimerDelegate::CreateLambda([this, Id](double, float) { if (IsFloatOpen(Id)) RebuildFloat(Id); return EActiveTimerReturnType::Stop; }));
}

void SOKStudioPanel::OpenFloat(const FString& Id, bool bToggle)
{
	FFloat* F = Floats.Find(Id);
	if (!F) return;
	if (F->bOpen && bToggle) { CloseFloat(Id); return; }
	F->bOpen = true;
	if (Id == TEXT("cards") && CardFiles.IsEmpty()) ScanCardFolder();
	if (Id == TEXT("scenes")) ScanScenes();
	if (Id == TEXT("quick")) RunQuickSearch();
	if (Id == TEXT("dialog") && Canvas)
	{
		const FVector2D Area = Canvas->GetCachedGeometry().GetLocalSize();
		if (Area.X > 100.f) F->Pos = (Area - F->Size) * 0.5f;
	}
	BringToFront(F->Slot);
	OpenOrder.Remove(Id);
	OpenOrder.Add(Id);
	bLayoutDirty = true;
	DeferFloat(Id);
	DeferSubMenu();
}

void SOKStudioPanel::CloseFloat(const FString& Id)
{
	FFloat* F = Floats.Find(Id);
	if (!F) return;
	if (Id == TEXT("poselib")) CancelPosePreview();
	F->bOpen = false;
	OpenOrder.Remove(Id);
	if (Id == TEXT("timeline")) TimelineView.Reset();
	bLayoutDirty = true;
	// The body goes after this event (a widget cannot be destroyed inside its own callback).
	RegisterActiveTimer(0.f, FWidgetActiveTimerDelegate::CreateLambda([this, Id](double, float) { if (FFloat* X = Floats.Find(Id); X && !X->bOpen && X->Body) X->Body->SetContent(SNullWidget::NullWidget); return EActiveTimerReturnType::Stop; }));
	DeferSubMenu();
}

void SOKStudioPanel::LoadLayout()
{
	FString Text;
	TSharedPtr<FJsonObject> J;
	if (!FFileHelper::LoadFileToString(Text, *LayoutFile()) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), J) || !J) return;
	J->TryGetBoolField(TEXT("root"), bShowRoot);
	J->TryGetBoolField(TEXT("workspace"), bShowWorkspace);
	J->TryGetBoolField(TEXT("groups"), bShowGroups);
	J->TryGetBoolField(TEXT("transform"), bShowTransform);
	const TSharedPtr<FJsonObject>* Windows = nullptr;
	if (J->TryGetObjectField(TEXT("windows"), Windows))
		for (const auto& P : (*Windows)->Values)
		{
			FFloat* F = Floats.Find(FString(*P.Key));
			const TSharedPtr<FJsonObject> W = P.Value->AsObject();
			if (!F || !W) continue;
			F->Pos = FVector2D(W->GetNumberField(TEXT("x")), W->GetNumberField(TEXT("y")));
			bool bOpen = false;
			// The dialog and the colour window belong to what asked for them: they never reopen by themselves.
			if (W->TryGetBoolField(TEXT("open"), bOpen) && bOpen && FString(*P.Key) != TEXT("dialog") && FString(*P.Key) != TEXT("color")) RegisterActiveTimer(0.f, FWidgetActiveTimerDelegate::CreateLambda([this, Id = FString(*P.Key)](double, float) { OpenFloat(Id); return EActiveTimerReturnType::Stop; }));
		}
	const TSharedPtr<FJsonObject>* Detail = nullptr;
	if (J->TryGetObjectField(TEXT("detail"), Detail))
		for (const auto& P : (*Detail)->Values)
			if (const TSharedPtr<FJsonObject> W = P.Value->AsObject()) DetailOffsets.Add(uint8(FCString::Atoi(*FString(*P.Key))), FVector2D(W->GetNumberField(TEXT("x")), W->GetNumberField(TEXT("y"))));
	bLayoutDirty = false;
}

void SOKStudioPanel::SaveLayout() const
{
	TSharedRef<FJsonObject> J = MakeShared<FJsonObject>();
	J->SetBoolField(TEXT("root"), bShowRoot);
	J->SetBoolField(TEXT("workspace"), bShowWorkspace);
	J->SetBoolField(TEXT("groups"), bShowGroups);
	J->SetBoolField(TEXT("transform"), bShowTransform);
	TSharedRef<FJsonObject> Windows = MakeShared<FJsonObject>();
	for (const TPair<FString, FFloat>& P : Floats)
	{
		TSharedRef<FJsonObject> W = MakeShared<FJsonObject>();
		W->SetNumberField(TEXT("x"), P.Value.Pos.X);
		W->SetNumberField(TEXT("y"), P.Value.Pos.Y);
		W->SetBoolField(TEXT("open"), P.Value.bOpen);
		Windows->SetObjectField(P.Key, W);
	}
	J->SetObjectField(TEXT("windows"), Windows);
	TSharedRef<FJsonObject> Detail = MakeShared<FJsonObject>();
	for (const TPair<uint8, FVector2D>& P : DetailOffsets)
	{
		TSharedRef<FJsonObject> W = MakeShared<FJsonObject>();
		W->SetNumberField(TEXT("x"), P.Value.X);
		W->SetNumberField(TEXT("y"), P.Value.Y);
		Detail->SetObjectField(FString::FromInt(P.Key), W);
	}
	J->SetObjectField(TEXT("detail"), Detail);
	FString Out;
	FJsonSerializer::Serialize(J, TJsonWriterFactory<>::Create(&Out));
	FFileHelper::SaveStringToFile(Out, *LayoutFile(), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

void SOKStudioPanel::TickUi(float DeltaTime)
{
	if (!Game.IsValid()) return;
	TickItemPreview();
	// The open report once the scene's objects had their chance to build.
	if (ReportPending > 0 && --ReportPending == 0 && !Game->LoadReport().IsEmpty()) ShowLoadReport();
	// The guide's drag readout on the status line.
	if (!Game->GuideReadout.IsEmpty()) SetStatus(Game->GuideReadout);
	// The colour window is bound to the object it was opened for.
	if (IsFloatOpen(TEXT("color")) && ColorKey.StartsWith(TEXT("obj")) && FCString::Atoi(*ColorKey.Mid(3)) != Game->SelectedId) CloseFloat(TEXT("color"));
	// The window title: the scene and a star while it has unsaved changes.
	if (GEngine && GEngine->GameViewport)
		if (TSharedPtr<SWindow> Window = GEngine->GameViewport->GetWindow())
		{
			const FString Name = Game->File.IsEmpty() ? FString(TEXT("新場景")) : FPaths::GetBaseFilename(Game->File);
			const FString Title = FString::Printf(TEXT("OK Studio — %s%s"), *Name, Game->bDirty ? TEXT(" *") : TEXT(""));
			if (Window->GetTitle().ToString() != Title) Window->SetTitle(FText::FromString(Title));
		}
	const double Now = FPlatformTime::Seconds();
	if (bLayoutDirty && Now - LayoutSavedAt > 2.) { SaveLayout(); bLayoutDirty = false; LayoutSavedAt = Now; }
	// Export progress in its window.
	static int32 LastFrame = -2;
	if (IsFloatOpen(TEXT("export")) && Game->ExportFrameIndex() != LastFrame) { LastFrame = Game->ExportFrameIndex(); if (LastFrame < 0) DeferFloat(TEXT("export")); }
}

// --- Dialog ---

void SOKStudioPanel::Confirm(const FString& Text, const TArray<TPair<FString, TFunction<void()>>>& Buttons)
{
	DialogText = Text;
	DialogButtons = Buttons;
	OpenFloat(TEXT("dialog"));
}

void SOKStudioPanel::GuardUnsaved(TFunction<void()> Continue)
{
	if (!Game.IsValid() || !Game->bDirty) { if (Continue) Continue(); return; }
	Confirm(TEXT("場景有未儲存的變更。"), {
		{ TEXT("儲存並繼續"), [this, Continue] { if (RunCommand(TEXT("scene.save")) && Continue) Continue(); } },
		{ TEXT("不儲存"), [Continue] { if (Continue) Continue(); } },
		{ TEXT("取消"), nullptr } });
}

TSharedRef<SWidget> SOKStudioPanel::BuildDialog()
{
	TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);
	for (const TPair<FString, TFunction<void()>>& B : DialogButtons)
	{
		TFunction<void()> Do = B.Value;
		Row->AddSlot().FillWidth(1.f).Padding(2.f, 0.f)[Button(FText::FromString(B.Key), [this, Do] { CloseFloat(TEXT("dialog")); if (Do) Do(); }, 0.f, 24.f, false, 11.f)];
	}
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().FillHeight(1.f).Padding(6.f).VAlign(VAlign_Center)[SNew(STextBlock).Font(Font(12.f)).ColorAndOpacity(OKStudioStyle::Text()).AutoWrapText(true).Text(FText::FromString(DialogText))]
		+ SVerticalBox::Slot().AutoHeight().Padding(2.f, 0.f, 2.f, 4.f)[Row];
}

// --- Tree editing ---

TSharedPtr<SWidget> SOKStudioPanel::TreeContextMenu()
{
	if (!Game.IsValid() || Game->Selection.IsEmpty()) return nullptr;
	struct FItem { const TCHAR* Label; const TCHAR* Command; };
	const FItem Items[] = {
		{ TEXT("改名"), TEXT("edit.rename") }, { TEXT("拷貝"), TEXT("edit.copy") }, { TEXT("貼上"), TEXT("edit.paste") }, { TEXT("複製物件"), TEXT("edit.duplicate") },
		{ TEXT("刪除"), TEXT("edit.delete") }, { TEXT("隱藏／顯示"), TEXT("edit.hide") }, { TEXT("移到根層"), TEXT("edit.root") }, { TEXT("設為其他選取的父物件"), TEXT("edit.parent") },
		{ TEXT("聚焦"), TEXT("camera.focus") }, { TEXT("選取存成場景"), TEXT("edit.saveas") }, { TEXT("複製變換"), TEXT("transform.copy") }, { TEXT("貼上變換"), TEXT("transform.paste") },
		{ TEXT("複製姿勢"), TEXT("pose.copy") }, { TEXT("貼上姿勢"), TEXT("pose.paste") }, { TEXT("打 key"), TEXT("timeline.key") },
	};
	TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);
	for (const FItem& I : Items)
	{
		const FString Command = I.Command;
		Box->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 1.f)
		[
			Button(FText::FromString(Tip(I.Label, Command)), [this, Command] { FSlateApplication::Get().DismissAllMenus(); RunCommand(Command); }, 190.f, 18.f, false, 10.f)
		];
	}
	return SNew(SBorder).BorderImage(Brush(Panel())).Padding(4.f)[Box];
}

void SOKStudioPanel::BeginRename()
{
	if (!Game.IsValid() || Game->SelectedId < 0) return;
	LocateSelection();
	const int32 Id = Game->SelectedId;
	// The row exists once the list has drawn it.
	RegisterActiveTimer(0.05f, FWidgetActiveTimerDelegate::CreateLambda([this, Id](double, float)
	{
		if (TSharedPtr<SInlineEditableTextBlock> Name = RowNames.FindRef(Id).Pin()) Name->EnterEditingMode();
		return EActiveTimerReturnType::Stop;
	}));
}

bool SOKStudioPanel::CanDropOn(const TArray<int32>& Ids, int32 Target) const
{
	if (!Game.IsValid() || !Game->Document->Find(Target) || Ids.IsEmpty()) return false;
	for (int32 Id : Ids)
	{
		if (Id == Target) return false;
		// Not under itself: the target must not be inside a dragged object.
		for (const FOKSceneObject* P = Game->Document->Find(Target); P; P = Game->Document->Find(P->Parent)) if (P->Id == Id) return false;
	}
	return true;
}

void SOKStudioPanel::DropOn(const TArray<int32>& Ids, int32 Target)
{
	for (int32 Id : Ids) Game->Document->Reparent(Id, Target);
	Collapsed.Remove(Target);
	Game->SelectMany(Ids, Ids.Last());
	SetStatus(FString::Printf(TEXT("%d 個物件移到 %s 底下"), Ids.Num(), *Game->Document->Find(Target)->Name));
}

bool SOKStudioPanel::DropOnPoint(const TArray<int32>& Ids, const FString& Key)
{
	const int32 Character = GroupCharacter();
	if (Character < 0) return false;
	int32 Done = 0;
	for (int32 Id : Ids)
	{
		if (Id == Character || !CanDropOn({ Id }, Character)) continue;
		Game->Document->Reparent(Id, Character);
		if (FOKSceneObject* O = Game->Document->Find(Id)) { O->AttachBone = Key; O->Transform = FTransform::Identity; ++Done; }
	}
	Changed();
	Game->SelectMany(Ids, Ids.Last());
	SetStatus(FString::Printf(TEXT("%d 個物件掛到 %s"), Done, *Key));
	return Done > 0;
}

void SOKStudioPanel::CopySelection()
{
	if (!Game.IsValid() || Game->Selection.IsEmpty()) return;
	Clipboard = Game->Document->ExportFragment(Game->SelectionRoots());
	FPlatformApplicationMisc::ClipboardCopy(*Clipboard);
	SetStatus(FString::Printf(TEXT("已拷貝 %d 個物件"), Game->SelectionRoots().Num()));
}

bool SOKStudioPanel::PasteClipboard()
{
	if (!Game.IsValid()) return false;
	FString Text;
	FPlatformApplicationMisc::ClipboardPaste(Text);
	if (!Text.Contains(TEXT("okscene-fragment"))) Text = Clipboard;
	if (Text.IsEmpty()) return false;
	const FOKSceneObject* Sel = Game->Document->Find(Game->SelectedId);
	const int32 Parent = !Sel ? -1 : (Sel->Kind == EOKSceneObjectKind::Folder ? Sel->Id : Sel->Parent);
	FString Error;
	const TArray<int32> New = Game->Document->PasteFragment(Text, Parent, Error);
	if (New.IsEmpty()) { SetStatus(TEXT("貼上失敗：") + Error); return false; }
	Game->SelectMany(New, New.Last());
	SetStatus(FString::Printf(TEXT("已貼上 %d 個物件"), New.Num()));
	return true;
}

void SOKStudioPanel::DuplicateSelection()
{
	TArray<int32> New;
	for (int32 Id : Game->SelectionRoots()) { const int32 Copy = Game->Document->Duplicate(Id); if (Copy >= 0) New.Add(Copy); }
	if (New.Num()) Game->SelectMany(New, New.Last());
}

void SOKStudioPanel::DuplicateSelectionOffset()
{
	// As 複製物件, the copies moved 20 cm along the view's right so they do not sit inside the originals.
	DuplicateSelection();
	UWorld* World = Game->SceneActor ? Game->SceneActor->GetWorld() : nullptr;
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	const FVector Right = PC && PC->PlayerCameraManager ? FRotationMatrix(PC->PlayerCameraManager->GetCameraRotation()).GetUnitAxis(EAxis::Y) : FVector(1.f, 0.f, 0.f);
	for (int32 Id : Game->SelectionRoots())
		if (FOKSceneObject* O = Game->Document->Find(Id))
		{
			// The offset is in the parent's space (the transform is local to it).
			const FTransform ParentWorld = Game->Document->ChildFrame(O->Parent);
			O->Transform.AddToTranslation(ParentWorld.InverseTransformVector(Right * 20.f));
		}
	Changed();
}

void SOKStudioPanel::GroupSelection()
{
	// HS2Studio6-style grouping: a new folder beside the first selected object holds the selection (world placement kept).
	const TArray<int32> Roots = Game->SelectionRoots();
	if (Roots.IsEmpty()) { SetStatus(TEXT("沒有選取")); return; }
	const FOKSceneObject* First = Game->Document->Find(Roots[0]);
	const int32 Folder = Game->Document->AddObject(EOKSceneObjectKind::Folder, TEXT("群組"), First ? First->Parent : -1);
	for (int32 Id : Roots) Game->Document->Reparent(Id, Folder);
	Game->Select(Folder);
	SetStatus(FString::Printf(TEXT("%d 個物件放進新群組"), Roots.Num()));
	Changed();
}

void SOKStudioPanel::UngroupSelection()
{
	// The selected folders' children move up to the folder's parent (world placement kept), then the folders go.
	TArray<int32> Freed, Folders;
	for (int32 Id : Game->Selection)
		if (const FOKSceneObject* F = Game->Document->Find(Id)) if (F->Kind == EOKSceneObjectKind::Folder) Folders.Add(Id);
	for (int32 Folder : Folders)
	{
		const FOKSceneObject* F = Game->Document->Find(Folder);
		if (!F) continue;
		const int32 Up = F->Parent;
		TArray<int32> Children;
		for (const FOKSceneObject& O : Game->Document->Objects) if (O.Parent == Folder) Children.Add(O.Id);
		for (int32 Child : Children) { Game->Document->Reparent(Child, Up); Freed.Add(Child); }
		Game->Document->RemoveObject(Folder);
	}
	if (Folders.IsEmpty()) { SetStatus(TEXT("選取裡沒有資料夾")); return; }
	Game->SelectMany(Freed, Freed.Num() ? Freed.Last() : -1);
	Changed();
}

void SOKStudioPanel::SoloSelection()
{
	// Only the selection (with its parents and children) shows; again: every object's own visibility comes back.
	if (SoloSaved.Num())
	{
		for (const TPair<int32, bool>& P : SoloSaved) if (FOKSceneObject* O = Game->Document->Find(P.Key)) O->bVisible = P.Value;
		SoloSaved.Reset();
		SetStatus(TEXT("全部恢復顯示"));
		Changed();
		return;
	}
	if (Game->Selection.IsEmpty()) { SetStatus(TEXT("沒有選取")); return; }
	TSet<int32> Keep;
	for (int32 Id : Game->Selection)
	{
		for (int32 P = Id; P >= 0; ) { Keep.Add(P); const FOKSceneObject* O = Game->Document->Find(P); P = O ? O->Parent : -1; }
	}
	for (const FOKSceneObject& O : Game->Document->Objects)
		for (int32 P = O.Parent; P >= 0; ) { if (Game->IsSelected(P)) { Keep.Add(O.Id); break; } const FOKSceneObject* Up = Game->Document->Find(P); P = Up ? Up->Parent : -1; }
	for (FOKSceneObject& O : Game->Document->Objects)
	{
		// Lights and the active camera keep lighting and framing the scene.
		if (Keep.Contains(O.Id) || O.Kind == EOKSceneObjectKind::Light || O.Id == Game->Document->ActiveCamera) continue;
		SoloSaved.Add(O.Id, O.bVisible);
		O.bVisible = false;
	}
	SetStatus(TEXT("只顯示所選（再按一次恢復）"));
	Changed();
}

void SOKStudioPanel::LockSelection()
{
	bool bAllLocked = true;
	for (int32 Id : Game->Selection) if (const FOKSceneObject* O = Game->Document->Find(Id)) bAllLocked &= O->bLocked;
	for (int32 Id : Game->Selection) if (FOKSceneObject* O = Game->Document->Find(Id)) O->bLocked = !bAllLocked;
	SetStatus(bAllLocked ? TEXT("解除鎖定") : TEXT("已鎖定：畫面上點不到，樹上仍可選"));
	Changed();
}

void SOKStudioPanel::DeleteSelection()
{
	for (int32 Id : Game->SelectionRoots()) Game->Document->RemoveObject(Id);
	Game->Select(-1);
}

void SOKStudioPanel::HideSelection()
{
	bool bAllShown = true;
	for (int32 Id : Game->Selection) if (const FOKSceneObject* O = Game->Document->Find(Id)) bAllShown &= O->bVisible;
	for (int32 Id : Game->Selection) if (FOKSceneObject* O = Game->Document->Find(Id)) O->bVisible = !bAllShown;
	Changed();
}

void SOKStudioPanel::ParentSelectionToPrimary()
{
	const int32 Primary = Game->SelectedId;
	int32 Moved = 0;
	for (int32 Id : Game->SelectionRoots()) if (Id != Primary && CanDropOn({ Id }, Primary)) { Game->Document->Reparent(Id, Primary); ++Moved; }
	SetStatus(FString::Printf(TEXT("%d 個物件移到主選物件底下"), Moved));
}

FString SOKStudioPanel::SaveSelectionAsScene()
{
	if (!Game.IsValid() || Game->Selection.IsEmpty()) return FString();
	UOKSceneDocument* Temp = NewObject<UOKSceneDocument>();
	FString Error;
	Temp->PasteFragment(Game->Document->ExportFragment(Game->SelectionRoots()), -1, Error);
	const FOKSceneObject* O = Game->Document->Find(Game->SelectedId);
	const FString Path = UOKStudioGame::SceneDirectory() / FPaths::MakeValidFileName((O ? O->Name : FString(TEXT("selection"))) + FDateTime::Now().ToString(TEXT("_%Y%m%d_%H%M%S"))) + TEXT(".okscene");
	return Temp->SaveToFile(Path, Error) ? Path : FString();
}

void SOKStudioPanel::LocateSelection()
{
	if (!Game.IsValid() || Game->SelectedId < 0) return;
	// Open the way to it, then bring its row into view.
	for (const FOKSceneObject* P = Game->Document->Find(Game->Document->Find(Game->SelectedId) ? Game->Document->Find(Game->SelectedId)->Parent : -1); P; P = Game->Document->Find(P->Parent)) Collapsed.Remove(P->Id);
	Search.Reset();
	KindFilter = -1;
	RefreshRows();
	for (const TSharedPtr<FRow>& R : Rows) if (R->Id == Game->SelectedId && List) List->RequestScrollIntoView(R);
}

// --- Scene files ---

FString SOKStudioPanel::KStudioFolder()
{
	FString Root;
	GConfig->GetString(TEXT("/Script/KKSideloader.KKGameRoot"), TEXT("Root"), Root, GGameIni);
	return Root.IsEmpty() ? FString() : FPaths::ConvertRelativePathToFull(Root / TEXT("UserData/Studio/scene"));
}

void SOKStudioPanel::ScanScenes()
{
	if (SceneFolder.IsEmpty()) SceneFolder = UOKStudioGame::SceneDirectory();
	SceneList.Reset();
	if (SceneFolder == UOKStudioGame::SceneDirectory()) SceneList = UOKStudioGame::SceneFiles();
	else
	{
		TArray<FString> Names;
		IFileManager::Get().FindFiles(Names, *(SceneFolder / TEXT("*.png")), true, false);
		TArray<FString> More;
		IFileManager::Get().FindFiles(More, *(SceneFolder / TEXT("*.okscene")), true, false);
		Names.Append(More);
		for (const FString& N : Names) if (N.EndsWith(TEXT(".okscene")) || FOKSceneCard::Kind(SceneFolder / N) != FOKSceneCard::EKind::None) SceneList.Add(SceneFolder / N);
	}
	if (!SceneSearch.IsEmpty()) SceneList.RemoveAll([this](const FString& F) { return !FPaths::GetBaseFilename(F).Contains(SceneSearch); });
	if (SceneSort == 1) SceneList.Sort([](const FString& A, const FString& B) { return FPaths::GetBaseFilename(A) < FPaths::GetBaseFilename(B); });
	else SceneList.Sort([](const FString& A, const FString& B) { return IFileManager::Get().GetTimeStamp(*A) > IFileManager::Get().GetTimeStamp(*B); });
}

bool SOKStudioPanel::OpenScene(const FString& Path, bool bMerge)
{
	if (!Game.IsValid() || Path.IsEmpty()) return false;
	const bool bKStudio = FOKSceneCard::Kind(Path) == FOKSceneCard::EKind::KStudio;
	if (!bMerge)
	{
		GuardUnsaved([this, Path, bKStudio] { if (bKStudio) ImportKStudio(Path); else LoadFile(Path); CloseFloat(TEXT("scenes")); });
		return true;
	}
	// 疊加: the other scene's objects join this one under new ids; the camera, map and look stay.
	FString Source = Path, Error;
	if (bKStudio)
	{
		FOKStudioImport::FReport Report;
		if (!FOKStudioImport::ImportToFile(Path, Source, Report, Error)) { SetStatus(TEXT("匯入失敗：") + Error); return false; }
	}
	UOKSceneDocument* Other = NewObject<UOKSceneDocument>();
	FString Note;
	if (!FOKSceneCard::Open(Source, Other, Error, &Note)) { SetStatus(TEXT("讀取失敗：") + Error); return false; }
	const TArray<int32> New = Game->Document->Merge(Other->ToJson(), Error);
	if (New.IsEmpty() && !Error.IsEmpty()) { SetStatus(TEXT("疊加失敗：") + Error); return false; }
	Game->SelectMany(New, New.Num() ? New.Last() : -1);
	SetStatus(FString::Printf(TEXT("已疊加 %s：%d 個物件"), *FPaths::GetBaseFilename(Path), New.Num()));
	ReportPending = 90;
	CloseFloat(TEXT("scenes"));
	return true;
}

bool SOKStudioPanel::DeleteScene(const FString& Path)
{
	// Into the trash, not gone.
	const FString Trash = FOKStudioSettings::Directory() / TEXT("Trash") / FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S_")) + FPaths::GetCleanFilename(Path);
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(Trash), true);
	const bool bOk = IFileManager::Get().Move(*Trash, *Path, false, true);
	SetStatus(bOk ? TEXT("已移到 ") + Trash : TEXT("無法刪除 ") + Path);
	if (bOk) { ScenePick.Reset(); ScanScenes(); DeferFloat(TEXT("scenes")); }
	return bOk;
}

bool SOKStudioPanel::RenameScene(const FString& Path, const FString& NewName)
{
	const FString Clean = FPaths::MakeValidFileName(NewName.TrimStartAndEnd());
	if (Clean.IsEmpty()) return false;
	const FString To = FPaths::GetPath(Path) / Clean + TEXT(".") + FPaths::GetExtension(Path);
	if (FPaths::FileExists(To)) { SetStatus(TEXT("已有同名檔案：") + To); return false; }
	const bool bOk = IFileManager::Get().Move(*To, *Path, false, true);
	if (bOk)
	{
		if (Game.IsValid() && Game->File == Path) Game->File = To;
		ScenePick = To;
		FOKStudioThumbnails::Get().Forget(Path);
		ScanScenes();
		DeferFloat(TEXT("scenes"));
	}
	SetStatus(bOk ? TEXT("已改名為 ") + Clean : TEXT("無法改名"));
	return bOk;
}

TSharedRef<SWidget> SOKStudioPanel::BuildSceneBrowser()
{
	if (SceneFolder.IsEmpty()) SceneFolder = UOKStudioGame::SceneDirectory();
	// Folders: the scene folder, the game's KStudio scenes, the backups; then the folders inside the current one.
	TSharedRef<SVerticalBox> Folders = SNew(SVerticalBox);
	auto FolderButton = [this, &Folders](const FString& Label, const FString& Folder)
	{
		if (Folder.IsEmpty()) return;
		Folders->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 2.f)
			[Button(FText::FromString(Label), [this, Folder] { SceneFolder = Folder; ScenePick.Reset(); ScanScenes(); DeferFloat(TEXT("scenes")); }, 0.f, 18.f, SceneFolder == Folder, 10.f)];
	};
	FolderButton(TEXT("場景（UserData）"), UOKStudioGame::SceneDirectory());
	FolderButton(TEXT("KStudio 場景"), KStudioFolder());
	FolderButton(TEXT("自動備份"), UOKStudioGame::BackupDirectory());
	TArray<FString> Subs;
	IFileManager::Get().FindFiles(Subs, *(SceneFolder / TEXT("*")), false, true);
	for (const FString& S : Subs) FolderButton(TEXT("▸ ") + S, SceneFolder / S);
	if (SceneFolder != UOKStudioGame::SceneDirectory() && SceneFolder != KStudioFolder() && SceneFolder != UOKStudioGame::BackupDirectory()) FolderButton(TEXT(".. 上一層"), FPaths::GetPath(SceneFolder));

	// Pictures of the scenes, four across.
	TSharedRef<SWrapBox> Grid = SNew(SWrapBox).UseAllottedSize(true);
	for (const FString& F : SceneList)
	{
		const bool bPng = F.EndsWith(TEXT(".png"));
		const FOKSceneCard::EKind K = bPng ? FOKSceneCard::Kind(F) : FOKSceneCard::EKind::Json;
		const FString KindTag = K == FOKSceneCard::EKind::KStudio ? TEXT("[KStudio] ") : (!bPng ? TEXT("[.okscene] ") : TEXT(""));
		Grid->AddSlot().Padding(2.f)
		[
			SNew(SBox).WidthOverride(92.f).HeightOverride(86.f)
			[
				Fill(TAttribute<bool>::CreateLambda([this, F] { return ScenePick == F; }), ListArea(),
					SNew(SButton).ButtonStyle(&FlatButton()).ContentPadding(FMargin(2.f)).ToolTipText(FText::FromString(F))
					.OnClicked_Lambda([this, F] { ScenePick = F; SceneNewName = FPaths::GetBaseFilename(F); DeferFloat(TEXT("scenes")); return FReply::Handled(); })
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().FillHeight(1.f)
						[
							SNew(SImage).Image_Lambda([F, bPng] { const FSlateBrush* B = bPng ? FOKStudioThumbnails::Get().File(F, 96) : nullptr; return B ? B : Brush(Input()); })
						]
						+ SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Font(Font(9.f)).ColorAndOpacity(OKStudioStyle::Text()).Text(FText::FromString(KindTag + FPaths::GetBaseFilename(F)))]
					])
			]
		];
	}
	// The picked scene: open it over the scene, add it to the scene, rename, delete.
	TSharedRef<SVerticalBox> Pick = SNew(SVerticalBox);
	if (!ScenePick.IsEmpty())
	{
		const FString F = ScenePick;
		Pick->AddSlot().AutoHeight()[SNew(SBox).HeightOverride(96.f)[SNew(SImage).Image_Lambda([F] { const FSlateBrush* B = F.EndsWith(TEXT(".png")) ? FOKStudioThumbnails::Get().File(F, 256) : nullptr; return B ? B : Brush(Input()); })]];
		Pick->AddSlot().AutoHeight().Padding(0.f, 2.f)[SNew(STextBlock).Font(Font(10.f)).ColorAndOpacity(OKStudioStyle::Text()).AutoWrapText(true).Text(FText::FromString(FPaths::GetCleanFilename(F)))];
		Pick->AddSlot().AutoHeight().Padding(0.f, 2.f)[Button(FText::FromString(TEXT("覆蓋（開啟）")), [this, F] { OpenScene(F, false); }, 0.f, 20.f)];
		Pick->AddSlot().AutoHeight().Padding(0.f, 2.f)[Button(FText::FromString(TEXT("疊加到目前場景")), [this, F] { OpenScene(F, true); }, 0.f, 20.f)];
		Pick->AddSlot().AutoHeight().Padding(0.f, 2.f)[Button(FText::FromString(TEXT("取消")), [this] { ScenePick.Reset(); DeferFloat(TEXT("scenes")); }, 0.f, 20.f)];
		Pick->AddSlot().AutoHeight().Padding(0.f, 6.f, 0.f, 2.f)[SNew(SBox).HeightOverride(18.f)[SNew(SEditableTextBox).Style(&TextBoxStyle()).Text(FText::FromString(SceneNewName)).OnTextChanged_Lambda([this](const FText& T) { SceneNewName = T.ToString(); })]];
		Pick->AddSlot().AutoHeight().Padding(0.f, 2.f)[Button(FText::FromString(TEXT("改名")), [this, F] { RenameScene(F, SceneNewName); }, 0.f, 20.f)];
		Pick->AddSlot().AutoHeight().Padding(0.f, 2.f)[Button(FText::FromString(TEXT("刪除（移到 Trash）")), [this, F]
		{
			Confirm(FString::Printf(TEXT("把 %s 移到 Saved/OKStudio/Trash？"), *FPaths::GetCleanFilename(F)), { { TEXT("移到 Trash"), [this, F] { DeleteScene(F); } }, { TEXT("取消"), nullptr } });
		}, 0.f, 20.f)];
	}
	else Pick->AddSlot().AutoHeight()[OKStudioStyle::Label(FText::FromString(TEXT("點一個場景來預覽，再選覆蓋或疊加。")), 10.f, TextHint())];
	if (SaveAsName.IsEmpty()) SaveAsName = Game.IsValid() && !Game->File.IsEmpty() ? FPaths::GetBaseFilename(Game->File) : FDateTime::Now().ToString(TEXT("scene_%Y%m%d_%H%M%S"));
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 3.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f)[SNew(SBox).HeightOverride(20.f)[SNew(SEditableTextBox).Style(&TextBoxStyle()).HintText(FText::FromString(TEXT("場景名稱"))).Text(FText::FromString(SaveAsName))
				.OnTextChanged_Lambda([this](const FText& T) { SaveAsName = T.ToString(); })
				.OnTextCommitted_Lambda([this](const FText& T, ETextCommit::Type C) { SaveAsName = T.ToString(); if (C == ETextCommit::OnEnter) SaveNamed(SaveAsName); })]]
			+ SHorizontalBox::Slot().AutoWidth().Padding(3.f, 0.f)[Button(FText::FromString(bSceneSaveMode ? TEXT("另存") : TEXT("儲存")), [this]
			{
				const FString Path = UOKStudioGame::SceneDirectory() / FPaths::MakeValidFileName(SaveAsName.TrimStartAndEnd()) + TEXT(".png");
				if (FPaths::FileExists(Path) && (!Game.IsValid() || Game->File != Path))
					Confirm(TEXT("已有同名場景，要覆蓋嗎？"), { { TEXT("覆蓋"), [this] { SaveNamed(SaveAsName); } }, { TEXT("取消"), nullptr } });
				else SaveNamed(SaveAsName);
			}, 60.f, 20.f)]
			+ SHorizontalBox::Slot().AutoWidth().Padding(3.f, 0.f, 0.f, 0.f)[SNew(SBox).WidthOverride(130.f).HeightOverride(20.f)[SNew(SEditableTextBox).Style(&TextBoxStyle()).HintText(FText::FromString(TEXT("搜尋"))).Text(FText::FromString(SceneSearch))
				.OnTextCommitted_Lambda([this](const FText& T, ETextCommit::Type) { SceneSearch = T.ToString(); ScanScenes(); DeferFloat(TEXT("scenes")); })]]
			+ SHorizontalBox::Slot().AutoWidth().Padding(3.f, 0.f, 0.f, 0.f)[Button(FText::FromString(SceneSort == 1 ? TEXT("依名稱") : TEXT("依時間")), [this] { SceneSort = 1 - SceneSort; ScanScenes(); DeferFloat(TEXT("scenes")); }, 56.f, 20.f)]
		]
		+ SVerticalBox::Slot().FillHeight(1.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()[SNew(SBox).WidthOverride(120.f)[SNew(SScrollBox) + SScrollBox::Slot()[Folders]]]
			+ SHorizontalBox::Slot().FillWidth(1.f).Padding(4.f, 0.f)[SNew(SBorder).BorderImage(Brush(ListArea())).Padding(2.f)[SNew(SScrollBox) + SScrollBox::Slot()[Grid]]]
			+ SHorizontalBox::Slot().AutoWidth()[SNew(SBox).WidthOverride(140.f)[SNew(SScrollBox) + SScrollBox::Slot()[Pick]]]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f, 0.f, 0.f)[OKStudioStyle::Label(FText::FromString(FString::Printf(TEXT("%s　%d 個場景"), *SceneFolder, SceneList.Num())), 9.f, TextHint())];
}

TSharedRef<SWidget> SOKStudioPanel::BuildBackups()
{
	const TArray<FString> Files = UOKStudioGame::BackupFiles();
	TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);
	for (const FString& F : Files)
	{
		const FString Picture = FPaths::ChangeExtension(F, TEXT("png"));
		const FDateTime When = IFileManager::Get().GetTimeStamp(*F);
		Box->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 3.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()[SNew(SBox).WidthOverride(128.f).HeightOverride(72.f)[SNew(SImage).Image_Lambda([Picture] { const FSlateBrush* B = FOKStudioThumbnails::Get().File(Picture, 128); return B ? B : Brush(Input()); })]]
			+ SHorizontalBox::Slot().FillWidth(1.f).Padding(6.f, 0.f).VAlign(VAlign_Center)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()[OKStudioStyle::Label(FText::FromString((When + (FDateTime::Now() - FDateTime::UtcNow())).ToString(TEXT("%Y-%m-%d %H:%M:%S"))), 11.f)]
				+ SVerticalBox::Slot().AutoHeight()[OKStudioStyle::Label(FText::FromString(FPaths::GetCleanFilename(F)), 9.f, TextHint())]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 3.f)[Button(FText::FromString(TEXT("恢復這份")), [this, F]
				{
					GuardUnsaved([this, F]
					{
						// A backup opens as an unsaved scene: saving asks for a name, the backup stays as it is.
						if (LoadFile(F) && Game.IsValid()) { Game->File.Reset(); Game->bDirty = true; }
						CloseFloat(TEXT("backups"));
					});
				}, 90.f, 20.f)]
			]
		];
	}
	if (Files.IsEmpty()) Box->AddSlot().AutoHeight()[OKStudioStyle::Label(FText::FromString(TEXT("還沒有備份。")), 10.f, TextHint())];
	const FOKStudioSettings& S = FOKStudioSettings::Get();
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 3.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)[OKStudioStyle::Label(FText::FromString(FString::Printf(TEXT("每 %d 分鐘自動備份，保留 %d 份（設定可改）"), S.BackupMinutes, S.BackupKeep)), 10.f, TextHint())]
			+ SHorizontalBox::Slot().AutoWidth()[Button(FText::FromString(TEXT("立即備份")), [this] { RunCommand(TEXT("scene.backup")); DeferFloat(TEXT("backups")); }, 80.f, 20.f)]
		]
		+ SVerticalBox::Slot().FillHeight(1.f)[SNew(SBorder).BorderImage(Brush(ListArea())).Padding(3.f)[SNew(SScrollBox) + SScrollBox::Slot()[Box]]];
}

TSharedRef<SWidget> SOKStudioPanel::BuildRecent()
{
	// HS2Studio6's scene info: how many objects of each kind, then the scenes opened or saved lately.
	int32 Counts[6] = {};
	for (const FOKSceneObject& O : Game.IsValid() ? Game->Document->Objects : TArray<FOKSceneObject>()) ++Counts[FMath::Clamp(int32(O.Kind), 0, 5)];
	int32 Keys = 0;
	if (Game.IsValid()) for (const FOKSceneTrack& T : Game->Document->Tracks) Keys += T.Keys.Num();
	const FString Stats = FString::Printf(TEXT("物件 %d：角色 %d、道具 %d、燈光 %d、相機 %d、特效 %d、資料夾 %d；時間軸 key %d"),
		Game.IsValid() ? Game->Document->Objects.Num() : 0, Counts[int32(EOKSceneObjectKind::Character)], Counts[int32(EOKSceneObjectKind::Item)], Counts[int32(EOKSceneObjectKind::Light)],
		Counts[int32(EOKSceneObjectKind::Camera)], Counts[int32(EOKSceneObjectKind::Effect)], Counts[int32(EOKSceneObjectKind::Folder)], Keys);
	TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);
	for (const FString& F : FOKStudioSettings::Get().RecentScenes)
	{
		const bool bThere = FPaths::FileExists(F);
		const FString Picture = FPaths::GetExtension(F).Equals(TEXT("png"), ESearchCase::IgnoreCase) ? F : FString();
		Box->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 2.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()[SNew(SBox).WidthOverride(64.f).HeightOverride(36.f)[SNew(SImage).Image_Lambda([Picture] { const FSlateBrush* B = Picture.IsEmpty() ? nullptr : FOKStudioThumbnails::Get().File(Picture, 64); return B ? B : Brush(Input()); })]]
			+ SHorizontalBox::Slot().FillWidth(1.f).Padding(6.f, 0.f).VAlign(VAlign_Center)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()[OKStudioStyle::Label(FText::FromString(FPaths::GetBaseFilename(F) + (bThere ? TEXT("") : TEXT("（檔案不在了）"))), 11.f)]
				+ SVerticalBox::Slot().AutoHeight()[OKStudioStyle::Label(FText::FromString(FPaths::GetPath(F)), 9.f, TextHint())]
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[Button(FText::FromString(TEXT("開啟")), [this, F] { if (OpenScene(F, false)) CloseFloat(TEXT("recent")); }, 50.f, 20.f, false, 10.f, ButtonFill(), bThere)]
		];
	}
	if (FOKStudioSettings::Get().RecentScenes.IsEmpty()) Box->AddSlot().AutoHeight()[OKStudioStyle::Label(FText::FromString(TEXT("還沒有開啟或儲存過場景。")), 10.f, TextHint())];
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)[OKStudioStyle::Label(FText::FromString(Stats), 10.f)]
		+ SVerticalBox::Slot().FillHeight(1.f)[SNew(SBorder).BorderImage(Brush(ListArea())).Padding(3.f)[SNew(SScrollBox) + SScrollBox::Slot()[Box]]];
}

void SOKStudioPanel::ShowLoadReport()
{
	ReportLines = Game.IsValid() ? Game->LoadReport() : TArray<FString>();
	OpenFloat(TEXT("report"));
}

TSharedRef<SWidget> SOKStudioPanel::BuildReport()
{
	if (ReportLines.IsEmpty() && Game.IsValid()) ReportLines = Game->LoadReport();
	const TArray<FString> Lines = ReportLines;
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()[OKStudioStyle::Label(FText::FromString(FString::Printf(TEXT("%d 個物件沒有顯示；它們仍在場景裡（工作區名稱後標「（未顯示）」），補齊資源後重新開啟就會出現。點一行選取該物件。"), Lines.Num())), 10.f, TextSoft())]
		+ SVerticalBox::Slot().FillHeight(1.f).Padding(0.f, 3.f)[TextList(Lines, [this, Lines](int32 I) { const int32 Id = LineId(Lines[I]); if (Id > 0 && Game.IsValid()) Game->Select(Id); }, 220.f)]
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f).Padding(0.f, 0.f, 3.f, 0.f)[Button(FText::FromString(TEXT("複製問題清單")), [Lines] { FPlatformApplicationMisc::ClipboardCopy(*FString::Join(Lines, TEXT("\n"))); }, 0.f, 20.f)]
			+ SHorizontalBox::Slot().FillWidth(1.f)[Button(FText::FromString(TEXT("重新整理")), [this] { ReportLines.Reset(); DeferFloat(TEXT("report")); }, 0.f, 20.f)]
		];
}

TSharedRef<SWidget> SOKStudioPanel::BuildResources()
{
	// Every character card and item model the scene needs, and what is missing (from the shown scene and the files).
	TArray<FString> Lines;
	if (Game.IsValid())
	{
		const FString Project = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
		for (const FOKSceneObject& O : Game->Document->Objects)
		{
			FString Missing;
			if (O.Kind == EOKSceneObjectKind::Character)
			{
				const FString Card = FPaths::IsRelative(O.Character.Card) ? Project / O.Character.Card : O.Character.Card;
				if (O.Character.Card.IsEmpty()) Missing = TEXT("沒有指定卡片");
				else if (!FPaths::FileExists(Card)) Missing = TEXT("缺卡片 ") + O.Character.Card;
			}
			else if (O.Kind == EOKSceneObjectKind::Item)
			{
				FString Model, Prefab, Error;
				if (!AOKSceneActor::ItemModel(O.Item, Model, Prefab, Error)) Missing = FString::Printf(TEXT("缺少道具 [%d/%d/%d] %s"), O.Item.Group, O.Item.Category, O.Item.No, *Error);
			}
			else if (O.Kind == EOKSceneObjectKind::Effect && !FOKStudioCatalog::Get().FindEffect(O.Effect.Effect)) Missing = TEXT("缺特效 ") + O.Effect.Effect;
			if (const FString* E = Game->SceneActor ? Game->SceneActor->Errors.Find(O.Id) : nullptr) Missing += (Missing.IsEmpty() ? TEXT("") : TEXT("；")) + *E;
			if (!Missing.IsEmpty()) Lines.Add(FString::Printf(TEXT("#%d %s：%s"), O.Id, *O.Name, *Missing));
		}
		if (Game->SceneActor && !Game->SceneActor->MapError.IsEmpty()) Lines.Add(TEXT("地圖：") + Game->SceneActor->MapError);
	}
	const int32 Count = Lines.Num();
	if (Lines.IsEmpty()) Lines.Add(TEXT("沒有缺少的資源。"));
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()[OKStudioStyle::Label(FText::FromString(FString::Printf(TEXT("%d 項缺件（點一行選取物件）"), Count)), 10.f, TextSoft())]
		+ SVerticalBox::Slot().FillHeight(1.f).Padding(0.f, 3.f)[TextList(Lines, [this, Lines](int32 I) { const int32 Id = LineId(Lines[I]); if (Id > 0 && Game.IsValid()) { Game->Select(Id); LocateSelection(); } }, 260.f)]
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f).Padding(0.f, 0.f, 3.f, 0.f)[Button(FText::FromString(TEXT("複製清單")), [Lines] { FPlatformApplicationMisc::ClipboardCopy(*FString::Join(Lines, TEXT("\n"))); }, 0.f, 20.f)]
			+ SHorizontalBox::Slot().FillWidth(1.f)[Button(FText::FromString(TEXT("重新檢查")), [this] { DeferFloat(TEXT("resources")); }, 0.f, 20.f)]
		];
}

TSharedRef<SWidget> SOKStudioPanel::BuildStories()
{
	// Galgame stories (Docs/GALGAME_DIALOGUE_PLAN.md) under UserData and Content.
	TArray<FString> Stories;
	for (const FString& Root : { FPaths::ProjectDir() / TEXT("UserData"), FPaths::ProjectContentDir() })
	{
		TArray<FString> Found;
		IFileManager::Get().FindFilesRecursive(Found, *FPaths::ConvertRelativePathToFull(Root), TEXT("*.okstory.json"), true, false);
		Stories.Append(Found);
	}
	Stories.Sort();
	TArray<FString> Names;
	for (const FString& S : Stories) Names.Add(FPaths::GetCleanFilename(S).LeftChop(13) + TEXT("　") + FPaths::GetPath(S));
	if (Stories.IsEmpty()) Names.Add(TEXT("找不到 *.okstory.json（UserData 與 Content 底下）"));
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()[OKStudioStyle::Label(FText::FromString(TEXT("點一個故事播放（目前場景會換成故事的場景）")), 10.f, TextSoft())]
		+ SVerticalBox::Slot().FillHeight(1.f).Padding(0.f, 3.f)[TextList(Names, [this, Stories](int32 I)
		{
			if (!Stories.IsValidIndex(I) || !GEngine || !Game.IsValid()) return;
			const FString Story = Stories[I];
			GuardUnsaved([this, Story]
			{
				CloseFloat(TEXT("stories"));
				GEngine->Exec(Game->SceneActor ? Game->SceneActor->GetWorld() : nullptr, *FString::Printf(TEXT("OKStory.Play \"%s\""), *Story));
			});
		}, 300.f)];
}

// --- Screenshot ---

FString SOKStudioPanel::TakeShot()
{
	if (!Game.IsValid()) return FString();
	const FString Folder = ShotDirectory();
	IFileManager::Get().MakeDirectory(*Folder, true);
	FString ShotBase = FDateTime::Now().ToString(*ShotPattern);
	if (bShotAlpha) ShotBase += TEXT("_alpha");
	FString Path = Folder / FPaths::MakeValidFileName(ShotBase) + TEXT(".png");
	// Never over an existing picture unless asked.
	for (int32 N = 1; bShotNoOverwrite && FPaths::FileExists(Path); ++N) Path = Folder / FPaths::MakeValidFileName(ShotBase) + FString::Printf(TEXT("_%d.png"), N);
	FString Error;
	if (!bShotFrame && Game->SceneActor) Game->SceneActor->SetFrameShown(false);
	const bool bCaptured = Game->CaptureToPng(Path, ShotW, ShotH, Error, bShotAlpha);
	if (!bShotFrame && Game->SceneActor) Game->SceneActor->SetFrameShown(true);
	if (!bCaptured) { SetStatus(TEXT("截圖失敗：") + Error); return FString(); }
	SetStatus(TEXT("截圖：") + Path);
	return Path;
}

TSharedRef<SWidget> SOKStudioPanel::BuildShot()
{
	TSharedRef<SWrapBox> Presets = SNew(SWrapBox).UseAllottedSize(true);
	const FIntPoint Sizes[] = { { 1280, 720 }, { 1920, 1080 }, { 2560, 1440 }, { 3840, 2160 }, { 1080, 1920 }, { 2048, 2048 } };
	for (const FIntPoint& S : Sizes)
		Presets->AddSlot().Padding(1.f)[Button(FText::FromString(FString::Printf(TEXT("%d×%d"), S.X, S.Y)), [this, S] { ShotW = S.X; ShotH = S.Y; DeferFloat(TEXT("shot")); }, 92.f, 18.f, ShotW == S.X && ShotH == S.Y, 10.f)];
	auto Line = [](const FString& Name, const TSharedRef<SWidget>& W) { return SNew(SHorizontalBox) + SHorizontalBox::Slot().FillWidth(0.35f).VAlign(VAlign_Center)[OKStudioStyle::Label(FText::FromString(Name), 10.f)] + SHorizontalBox::Slot().FillWidth(0.65f)[SNew(SBox).HeightOverride(18.f)[W]]; };
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()[Presets]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 3.f)[Line(TEXT("寬"), Spin([this] { return float(ShotW); }, [this](float V) { ShotW = FMath::Clamp(FMath::RoundToInt(V), 16, 8192); }, 16.f, 8192.f, 1.f))]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 1.f)[Line(TEXT("高"), Spin([this] { return float(ShotH); }, [this](float V) { ShotH = FMath::Clamp(FMath::RoundToInt(V), 16, 8192); }, 16.f, 8192.f, 1.f))]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f)[CheckBox(TEXT("透明背景（只有角色與道具）"), [this] { return bShotAlpha; }, [this](bool B) { bShotAlpha = B; })]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f)[CheckBox(TEXT("不覆蓋既有檔案"), [this] { return bShotNoOverwrite; }, [this](bool B) { bShotNoOverwrite = B; })]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f)[CheckBox(TEXT("含前景框"), [this] { return bShotFrame; }, [this](bool B) { bShotFrame = B; })]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f)[CheckBox(TEXT("本次目錄（這次啟動的截圖放進同一個子資料夾）"), [this] { return bShotSessionFolder; }, [this](bool B) { bShotSessionFolder = B; })]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 1.f)[Line(TEXT("資料夾"), TextBox([this] { return ShotFolder; }, [this](const FString& V) { ShotFolder = V.TrimStartAndEnd().TrimQuotes(); }, TEXT("UserData/Studio/screenshot")))]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 1.f)[Line(TEXT("檔名樣式"), TextBox([this] { return ShotPattern; }, [this](const FString& V) { if (!V.TrimStartAndEnd().IsEmpty()) ShotPattern = V.TrimStartAndEnd(); }))]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 6.f, 0.f, 2.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f).Padding(0.f, 0.f, 3.f, 0.f)[Button(FText::FromString(TEXT("拍攝")), [this] { TakeShot(); }, 0.f, 24.f, false, 12.f)]
			+ SHorizontalBox::Slot().FillWidth(1.f)[Button(FText::FromString(TEXT("開資料夾")), [this]
			{
				const FString Folder = ShotDirectory();
				IFileManager::Get().MakeDirectory(*Folder, true);
				FPlatformProcess::ExploreFolder(*Folder);
			}, 0.f, 24.f)]
		];
}

// --- Timeline window and export ---

void SOKStudioPanel::ShowEaseMenu(const FVector2D& At)
{
	static const TCHAR* Names[] = { TEXT("線性"), TEXT("緩入緩出"), TEXT("緩入"), TEXT("緩出"), TEXT("跳格") };
	TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);
	for (int32 I = 0; I < 5; ++I)
		Box->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 1.f)[Button(FText::FromString(Names[I]), [this, I] { FSlateApplication::Get().DismissAllMenus(); if (TimelineView) TimelineView->SetEase(EOKSceneEase(I)); }, 100.f, 18.f, false, 10.f)];
	FSlateApplication::Get().PushMenu(AsShared(), FWidgetPath(), SNew(SBorder).BorderImage(Brush(Panel())).Padding(4.f)[Box], At, FPopupTransitionEffect(FPopupTransitionEffect::ContextMenu));
}

TSharedRef<SWidget> SOKStudioPanel::BuildTimeline()
{
	auto Icon = [this](FName I, const FString& Label, const FString& Command) { return IconButton(I, Tip(Label, Command), Cmd(Command), 22.f, 20.f); };
	TSharedRef<SWidget> Row1 = SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth()[Icon(TEXT("first"), TEXT("開頭"), TEXT("timeline.first"))]
		+ SHorizontalBox::Slot().AutoWidth().Padding(1.f, 0.f)[Icon(TEXT("prev"), TEXT("上一幀"), TEXT("timeline.prev"))]
		+ SHorizontalBox::Slot().AutoWidth()[IconButton(TEXT("play"), Tip(TEXT("播放／停止"), TEXT("timeline.play")), Cmd(TEXT("timeline.play")), 26.f, 20.f, TAttribute<bool>::CreateLambda([this] { return Game.IsValid() && Game->bTimelinePlaying; }))]
		+ SHorizontalBox::Slot().AutoWidth().Padding(1.f, 0.f)[Icon(TEXT("next"), TEXT("下一幀"), TEXT("timeline.next"))]
		+ SHorizontalBox::Slot().AutoWidth()[Icon(TEXT("last"), TEXT("結尾"), TEXT("timeline.last"))]
		+ SHorizontalBox::Slot().AutoWidth().Padding(4.f, 0.f).VAlign(VAlign_Center)
		[OKStudioStyle::Label(TAttribute<FText>::CreateLambda([this] { return FText::FromString(Game.IsValid() ? FString::Printf(TEXT("%.2f 秒 / 幀 %d"), Game->TimelineTime, FMath::RoundToInt(Game->TimelineTime * 30.f)) : FString()); }), 10.f)]
		+ SHorizontalBox::Slot().FillWidth(1.f)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[OKStudioStyle::Label(FText::FromString(TEXT("時長")), 10.f)]
		+ SHorizontalBox::Slot().AutoWidth().Padding(2.f, 0.f)[SNew(SBox).WidthOverride(48.f).HeightOverride(18.f)[Spin([this] { return Game.IsValid() ? Game->Document->Duration : 10.f; }, [this](float V) { if (Game.IsValid()) { Game->Document->Duration = FMath::Max(V, 0.1f); Changed(); } }, 0.1f, 3600.f, 0.1f)]]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[OKStudioStyle::Label(FText::FromString(TEXT("速度")), 10.f)]
		+ SHorizontalBox::Slot().AutoWidth().Padding(2.f, 0.f)[SNew(SBox).WidthOverride(40.f).HeightOverride(18.f)[Spin([this] { return Game.IsValid() ? Game->TimelineSpeed : 1.f; }, [this](float V) { if (Game.IsValid()) Game->TimelineSpeed = FMath::Clamp(V, 0.05f, 8.f); }, 0.05f, 8.f, 0.05f)]];
	TSharedRef<SWidget> Row2 = SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth()[IconButton(TEXT("key"), Tip(TEXT("為選取物件打 key"), TEXT("timeline.key")), Cmd(TEXT("timeline.key")), 22.f, 20.f)]
		+ SHorizontalBox::Slot().AutoWidth().Padding(1.f, 0.f)[Button(FText::FromString(TEXT("刪 key")), Cmd(TEXT("timeline.delete")), 40.f, 20.f, false, 10.f)]
		+ SHorizontalBox::Slot().AutoWidth().Padding(1.f, 0.f)[Button(FText::FromString(TEXT("場景 key")), Cmd(TEXT("timeline.scenekey")), 52.f, 20.f, false, 10.f)]
		+ SHorizontalBox::Slot().AutoWidth().Padding(1.f, 0.f)[Button(FText::FromString(TEXT("清空軌道")), Cmd(TEXT("timeline.clear")), 52.f, 20.f, false, 10.f)]
		+ SHorizontalBox::Slot().AutoWidth().Padding(1.f, 0.f)[Button(TAttribute<FText>::CreateLambda([this]
		{
			const FOKSceneTrack* T = Game.IsValid() ? Game->Document->TrackOf(Game->SelectedId) : nullptr;
			return FText::FromString(T && T->bSmoothPath ? TEXT("平滑路徑") : TEXT("直線路徑"));
		}), [this] { if (FOKSceneTrack* T = Game.IsValid() ? Game->Document->TrackOf(Game->SelectedId) : nullptr) { T->bSmoothPath = !T->bSmoothPath; Changed(); } }, 56.f, 20.f, false, 10.f)]
		+ SHorizontalBox::Slot().AutoWidth().Padding(4.f, 0.f, 1.f, 0.f).VAlign(VAlign_Center)[CheckBox(TEXT("循環"), [this] { return Game.IsValid() && Game->LoopEnd > Game->LoopStart; },
			[this](bool B) { if (!Game.IsValid()) return; if (B) { Game->LoopStart = 0.f; Game->LoopEnd = Game->Document->Duration; } else Game->LoopEnd = Game->LoopStart = 0.f; })]
		+ SHorizontalBox::Slot().AutoWidth().Padding(1.f, 0.f)[SNew(SBox).WidthOverride(40.f).HeightOverride(18.f)[Spin([this] { return Game.IsValid() ? Game->LoopStart : 0.f; }, [this](float V) { if (Game.IsValid()) Game->LoopStart = FMath::Max(V, 0.f); }, 0.f, 3600.f, 0.1f)]]
		+ SHorizontalBox::Slot().AutoWidth().Padding(1.f, 0.f)[SNew(SBox).WidthOverride(40.f).HeightOverride(18.f)[Spin([this] { return Game.IsValid() ? Game->LoopEnd : 0.f; }, [this](float V) { if (Game.IsValid()) Game->LoopEnd = FMath::Max(V, 0.f); }, 0.f, 3600.f, 0.1f)]]
		+ SHorizontalBox::Slot().AutoWidth().Padding(4.f, 0.f, 1.f, 0.f).VAlign(VAlign_Center)[CheckBox(TEXT("30fps 吸附"), [this] { return !TimelineView || TimelineView->SnapFps > 0.f; }, [this](bool B) { if (TimelineView) TimelineView->SnapFps = B ? 30.f : 0.f; })]
		+ SHorizontalBox::Slot().FillWidth(1.f)
		+ SHorizontalBox::Slot().AutoWidth()[Button(FText::FromString(TEXT("輸出…")), Cmd(TEXT("timeline.export")), 50.f, 20.f, false, 10.f)];
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()[Row1]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f)[Row2]
		+ SVerticalBox::Slot().FillHeight(1.f)[SAssignNew(TimelineView, SOKStudioTimeline).Game(Game)
			.OnEaseMenu([this](const FVector2D& At) { ShowEaseMenu(At); }).OnStatus([this](const FString& S) { SetStatus(S); })];
}

TSharedRef<SWidget> SOKStudioPanel::BuildExport()
{
	const bool bRunning = Game.IsValid() && Game->IsExporting();
	auto Line = [](const FString& Name, const TSharedRef<SWidget>& W) { return SNew(SHorizontalBox) + SHorizontalBox::Slot().FillWidth(0.35f).VAlign(VAlign_Center)[OKStudioStyle::Label(FText::FromString(Name), 10.f)] + SHorizontalBox::Slot().FillWidth(0.65f)[SNew(SBox).HeightOverride(18.f)[W]]; };
	static const TCHAR* Kinds[] = { TEXT("PNG 序列"), TEXT("mp4"), TEXT(".okanim（所選角色）") };
	TSharedRef<SHorizontalBox> KindRow = SNew(SHorizontalBox);
	for (int32 K = 0; K < 3; ++K) KindRow->AddSlot().FillWidth(1.f).Padding(0.f, 0.f, 1.f, 0.f)[Button(FText::FromString(Kinds[K]), [this, K] { ExportKind = K; DeferFloat(TEXT("export")); }, 0.f, 18.f, ExportKind == K, 10.f)];
	const float Duration = Game.IsValid() ? Game->Document->Duration : 10.f;
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()[KindRow]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 3.f)[Line(TEXT("從（秒）"), Spin([this] { return ExportFromT; }, [this](float V) { ExportFromT = FMath::Max(V, 0.f); }, 0.f, Duration, 0.1f))]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 1.f)[Line(TEXT("到（秒，-1 結尾）"), Spin([this] { return ExportToT; }, [this](float V) { ExportToT = V; }, -1.f, Duration, 0.1f))]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 1.f)[Line(TEXT("fps"), Spin([this] { return ExportFpsV; }, [this](float V) { ExportFpsV = FMath::Clamp(V, 1.f, 120.f); }, 1.f, 120.f, 1.f))]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 1.f)[Line(TEXT("寬"), Spin([this] { return float(ExportW); }, [this](float V) { ExportW = FMath::Clamp(FMath::RoundToInt(V), 16, 8192); }, 16.f, 8192.f, 1.f))]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 1.f)[Line(TEXT("高"), Spin([this] { return float(ExportH); }, [this](float V) { ExportH = FMath::Clamp(FMath::RoundToInt(V), 16, 8192); }, 16.f, 8192.f, 1.f))]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 1.f)[Line(TEXT("輸出資料夾"), TextBox([this] { return ExportFolderText; }, [this](const FString& V) { ExportFolderText = V.TrimStartAndEnd().TrimQuotes(); }, TEXT("UserData/Studio/movie")))]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 4.f)
		[
			SNew(SBox).HeightOverride(14.f)
			[SNew(SProgressBar).Percent_Lambda([this] { return Game.IsValid() && Game->IsExporting() ? TOptional<float>(float(Game->ExportFrameIndex()) / FMath::Max(1, Game->ExportFrameCount())) : TOptional<float>(0.f); })]
		]
		+ SVerticalBox::Slot().AutoHeight()[OKStudioStyle::Label(TAttribute<FText>::CreateLambda([this]
		{
			if (!Game.IsValid()) return FText::GetEmpty();
			if (Game->IsExporting()) return FText::FromString(FString::Printf(TEXT("輸出中 %d / %d 幀"), Game->ExportFrameIndex(), Game->ExportFrameCount()));
			if (!Game->LastMovie.IsEmpty()) return FText::FromString(TEXT("完成：") + Game->LastMovie);
			if (!Game->LastMovieError.IsEmpty()) return FText::FromString(TEXT("沒有影片：") + Game->LastMovieError);
			return FText::FromString(Game->ExportFolder().IsEmpty() ? FString() : TEXT("上次輸出：") + Game->ExportFolder());
		}), 10.f, TextSoft())]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 4.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f).Padding(0.f, 0.f, 2.f, 0.f)[Button(FText::FromString(TEXT("開始")), [this]
			{
				if (!Game.IsValid() || Game->IsExporting()) return;
				if (ExportKind == 2)
				{
					FString Error;
					const FString File = Game->ExportOkAnim(Game->SelectedId, ExportFpsV, Error);
					SetStatus(File.IsEmpty() ? TEXT("轉出失敗（先選一個角色）：") + Error : TEXT("OK 動畫：") + File);
					return;
				}
				if (ExportKind == 1 && !FPaths::FileExists(UOKStudioGame::FfmpegPath())) { SetStatus(TEXT("找不到 Tools/ffmpeg/ffmpeg.exe（第三方工具不進版控）")); return; }
				const FString Dir = Game->ExportFrames(ExportFpsV, ExportW, ExportH, ExportKind == 1, ExportFromT, ExportToT, ExportFolderText);
				SetStatus(TEXT("輸出到 ") + Dir);
				DeferFloat(TEXT("export"));
			}, 0.f, 22.f, false, 11.f, ButtonFill(), !bRunning)]
			+ SHorizontalBox::Slot().FillWidth(1.f).Padding(0.f, 0.f, 2.f, 0.f)[Button(FText::FromString(TEXT("取消")), [this] { if (Game.IsValid()) Game->CancelExport(); DeferFloat(TEXT("export")); }, 0.f, 22.f, false, 11.f, ButtonFill(), bRunning)]
			+ SHorizontalBox::Slot().FillWidth(1.f)[Button(FText::FromString(TEXT("開資料夾")), [this]
			{
				if (!Game.IsValid()) return;
				const FString Folder = Game->ExportFolder().IsEmpty() ? FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("UserData/Studio/movie")) : FPaths::GetPath(Game->ExportFolder());
				IFileManager::Get().MakeDirectory(*Folder, true);
				FPlatformProcess::ExploreFolder(*Folder);
			}, 0.f, 22.f)]
		];
}

// --- Settings ---

TSharedRef<SWidget> SOKStudioPanel::BuildSettings()
{
	FOKStudioSettings& S = FOKStudioSettings::Get();
	static const TCHAR* Tabs[] = { TEXT("一般"), TEXT("相機與 Gizmo"), TEXT("備份"), TEXT("資源目錄"), TEXT("快捷鍵"), TEXT("快捷鍵一覽") };
	TSharedRef<SVerticalBox> Menu = SNew(SVerticalBox);
	for (int32 I = 0; I < UE_ARRAY_COUNT(Tabs); ++I)
		Menu->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 3.f)[Button(FText::FromString(Tabs[I]), [this, I] { SettingsTab = I; DeferFloat(TEXT("settings")); }, 110.f, 20.f, SettingsTab == I, 10.f)];
	TSharedRef<SVerticalBox> Body = SNew(SVerticalBox);
	const FString Find = SettingsSearch;
	auto Show = [&Find](const FString& Name) { return Find.IsEmpty() || Name.Contains(Find); };
	// One setting: name, its control, a "?" with what it does, and back to the default.
	auto Setting = [&Body, &Show](const FString& Name, const FString& Help, const TSharedRef<SWidget>& Control, TFunction<void()> Default)
	{
		if (!Show(Name) && !Show(Help)) return;
		Body->AddSlot().AutoHeight().Padding(0.f, 1.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(0.38f).VAlign(VAlign_Center)[OKStudioStyle::Label(FText::FromString(Name), 10.f)]
			+ SHorizontalBox::Slot().FillWidth(0.45f)[SNew(SBox).HeightOverride(18.f)[Control]]
			+ SHorizontalBox::Slot().AutoWidth().Padding(3.f, 0.f)[SNew(SBox).ToolTipText(FText::FromString(Help))[Button(FText::FromString(TEXT("?")), nullptr, 18.f, 18.f, false, 10.f, Input())]]
			+ SHorizontalBox::Slot().AutoWidth()[Button(FText::FromString(TEXT("預設")), [Default] { if (Default) Default(); FOKStudioSettings::Get().Save(); }, 36.f, 18.f, false, 9.f)]
		];
	};
	auto Number = [this](float* Value, float Min, float Max, float Delta) { return Spin([Value] { return *Value; }, [Value](float V) { *Value = V; FOKStudioSettings::Get().Save(); }, Min, Max, Delta); };
	switch (SettingsTab)
	{
	case 0:
		Setting(TEXT("介面縮放"), TEXT("整個工作室介面的大小（乘在 HS2Studio6 的縮放上）；立即生效"), Number(&S.UiScale, 0.5f, 2.f, 0.05f), [] { FOKStudioSettings::Get().UiScale = 1.f; });
		Setting(TEXT("場景檔資料夾"), TEXT("場景瀏覽器的預設資料夾（唯讀）"), OKStudioStyle::Label(FText::FromString(UOKStudioGame::SceneDirectory()), 9.f, TextHint()), nullptr);
		Setting(TEXT("全部恢復預設"), TEXT("所有設定與快捷鍵回到預設（收藏與最近使用保留）"), Button(FText::FromString(TEXT("恢復預設")), [this] { FOKStudioSettings::Get().Reset(); DeferFloat(TEXT("settings")); }, 0.f, 18.f), nullptr);
		break;
	case 1:
		Setting(TEXT("繞行速度"), TEXT("拖動視窗時每像素轉幾度"), Number(&S.OrbitSpeed, 0.05f, 2.f, 0.01f), [] { FOKStudioSettings::Get().OrbitSpeed = 0.3f; });
		Setting(TEXT("平移速度"), TEXT("中鍵平移的倍率"), Number(&S.PanSpeed, 0.1f, 5.f, 0.05f), [] { FOKStudioSettings::Get().PanSpeed = 1.f; });
		Setting(TEXT("縮放速度"), TEXT("滾輪縮放的倍率"), Number(&S.ZoomSpeed, 0.1f, 5.f, 0.05f), [] { FOKStudioSettings::Get().ZoomSpeed = 1.f; });
		Setting(TEXT("飛行速度 cm/秒"), TEXT("自由飛行時 W A S D Q E 的速度"), Number(&S.FlySpeed, 10.f, 3000.f, 10.f), [] { FOKStudioSettings::Get().FlySpeed = 300.f; });
		Setting(TEXT("自由飛行"), TEXT("開啟後按住右鍵是轉鏡頭，W A S D Q E 移動"), CheckBox(TEXT("開"), [] { return FOKStudioSettings::Get().bFlyMode; }, [](bool B) { FOKStudioSettings::Get().bFlyMode = B; FOKStudioSettings::Get().Save(); }), [] { FOKStudioSettings::Get().bFlyMode = false; });
		Setting(TEXT("Gizmo 大小"), TEXT("移動、旋轉、縮放把手的螢幕大小倍率"), Number(&S.GizmoScale, 0.3f, 4.f, 0.05f), [] { FOKStudioSettings::Get().GizmoScale = 1.f; });
		Setting(TEXT("吸附：移動 cm"), TEXT("吸附開啟或按住 Ctrl 時，移動的步進"), Number(&S.SnapMove, 0.1f, 1000.f, 0.5f), [] { FOKStudioSettings::Get().SnapMove = 10.f; });
		Setting(TEXT("吸附：旋轉 度"), TEXT("吸附時旋轉的步進"), Number(&S.SnapRotate, 0.5f, 90.f, 0.5f), [] { FOKStudioSettings::Get().SnapRotate = 15.f; });
		Setting(TEXT("吸附：縮放"), TEXT("吸附時縮放的步進"), Number(&S.SnapScale, 0.01f, 1.f, 0.01f), [] { FOKStudioSettings::Get().SnapScale = 0.1f; });
		break;
	case 2:
		Setting(TEXT("自動備份間隔（分）"), TEXT("0 關閉；拖動、載入、播放、輸出中會延後"), Spin([] { return float(FOKStudioSettings::Get().BackupMinutes); }, [](float V) { FOKStudioSettings::Get().BackupMinutes = FMath::Max(0, FMath::RoundToInt(V)); FOKStudioSettings::Get().Save(); }, 0.f, 120.f, 1.f), [] { FOKStudioSettings::Get().BackupMinutes = 5; });
		Setting(TEXT("保留份數"), TEXT("超過的舊備份會刪掉"), Spin([] { return float(FOKStudioSettings::Get().BackupKeep); }, [](float V) { FOKStudioSettings::Get().BackupKeep = FMath::Max(1, FMath::RoundToInt(V)); FOKStudioSettings::Get().Save(); }, 1.f, 100.f, 1.f), [] { FOKStudioSettings::Get().BackupKeep = 10; });
		Setting(TEXT("備份資料夾"), TEXT("Saved/OKStudio/Backup"), Button(FText::FromString(TEXT("開資料夾")), [] { IFileManager::Get().MakeDirectory(*UOKStudioGame::BackupDirectory(), true); FPlatformProcess::ExploreFolder(*UOKStudioGame::BackupDirectory()); }, 0.f, 18.f), nullptr);
		break;
	case 3:
	{
		Body->AddSlot().AutoHeight()[OKStudioStyle::Label(FText::FromString(TEXT("角色卡瀏覽器的資料夾（Content/CharacterCards 與遊戲的 UserData/chara 一定會列）")), 10.f, TextSoft())];
		for (int32 I = 0; I < S.CardRoots.Num(); ++I)
			Body->AddSlot().AutoHeight().Padding(0.f, 1.f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)[OKStudioStyle::Label(FText::FromString(S.CardRoots[I]), 10.f)]
				+ SHorizontalBox::Slot().AutoWidth()[Button(FText::FromString(TEXT("移除")), [this, I] { FOKStudioSettings& X = FOKStudioSettings::Get(); if (X.CardRoots.IsValidIndex(I)) X.CardRoots.RemoveAt(I); X.Save(); DeferFloat(TEXT("settings")); }, 40.f, 18.f)]
			];
		Body->AddSlot().AutoHeight().Padding(0.f, 4.f)
			[SNew(SBox).HeightOverride(18.f)[SNew(SEditableTextBox).Style(&TextBoxStyle()).HintText(FText::FromString(TEXT("輸入資料夾路徑後按 Enter 加入")))
				.OnTextCommitted_Lambda([this](const FText& T, ETextCommit::Type C)
				{
					const FString Dir = T.ToString().TrimStartAndEnd().TrimQuotes();
					if (C != ETextCommit::OnEnter || Dir.IsEmpty()) return;
					if (!FPaths::DirectoryExists(Dir)) { SetStatus(TEXT("沒有這個資料夾：") + Dir); return; }
					FOKStudioSettings::Get().CardRoots.AddUnique(Dir);
					FOKStudioSettings::Get().Save();
					DeferFloat(TEXT("settings"));
				})]];
		break;
	}
	case 4:
	case 5:
	{
		const bool bEdit = SettingsTab == 4;
		const TArray<FString> Conflicts = S.Conflicts();
		if (bEdit && Conflicts.Num())
			Body->AddSlot().AutoHeight()[SNew(STextBlock).Font(Font(10.f)).ColorAndOpacity(FLinearColor(1.f, .5f, .4f)).AutoWrapText(true).Text(FText::FromString(TEXT("衝突：") + FString::Join(Conflicts, TEXT("；"))))];
		if (bEdit)
			Body->AddSlot().AutoHeight().Padding(0.f, 2.f)[Button(FText::FromString(TEXT("全部快捷鍵恢復預設")), [this] { FOKStudioSettings::Get().Bindings.Reset(); FOKStudioSettings::Get().Save(); DeferFloat(TEXT("settings")); }, 0.f, 18.f)];
		for (const FString& Id : CommandOrder)
		{
			const FCommand* C = Commands.Find(Id);
			const FString Keys = S.BindingOf(Id);
			if (!C || (!bEdit && Keys.IsEmpty()) || (!Show(C->Label) && !Show(Id) && !Show(Keys))) continue;
			TSharedRef<SHorizontalBox> Line = SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(0.4f).VAlign(VAlign_Center)[OKStudioStyle::Label(FText::FromString(C->Label), 10.f)]
				+ SHorizontalBox::Slot().FillWidth(0.3f).VAlign(VAlign_Center)[OKStudioStyle::Label(FText::FromString(Id), 9.f, TextHint())];
			if (bEdit)
			{
				Line->AddSlot().FillWidth(0.3f)[Button(FText::FromString(RebindingId == Id ? FString(TEXT("按新按鍵…（Esc 取消）")) : (Keys.IsEmpty() ? FString(TEXT("（未綁定）")) : Keys)),
					[this, Id] { RebindingId = Id; DeferFloat(TEXT("settings")); }, 0.f, 18.f, RebindingId == Id, 10.f, Input())];
				Line->AddSlot().AutoWidth().Padding(2.f, 0.f, 0.f, 0.f)[Button(FText::FromString(TEXT("清除")), [this, Id] { FOKStudioSettings::Get().Bindings.Add(Id, FString()); FOKStudioSettings::Get().Save(); DeferFloat(TEXT("settings")); }, 34.f, 18.f, false, 9.f)];
				Line->AddSlot().AutoWidth().Padding(2.f, 0.f, 0.f, 0.f)[Button(FText::FromString(TEXT("預設")), [this, Id] { FOKStudioSettings::Get().Bindings.Remove(Id); FOKStudioSettings::Get().Save(); DeferFloat(TEXT("settings")); }, 34.f, 18.f, false, 9.f)];
			}
			else Line->AddSlot().FillWidth(0.3f).VAlign(VAlign_Center)[OKStudioStyle::Label(FText::FromString(Keys), 10.f, TextSoft())];
			Body->AddSlot().AutoHeight().Padding(0.f, 1.f)[Line];
		}
		break;
	}
	default: break;
	}
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)
		[
			SNew(SBox).HeightOverride(20.f)
			[SNew(SEditableTextBox).Style(&TextBoxStyle()).HintText(FText::FromString(TEXT("搜尋設定或快捷鍵"))).Text(FText::FromString(SettingsSearch))
				.OnTextCommitted_Lambda([this](const FText& T, ETextCommit::Type) { SettingsSearch = T.ToString(); DeferFloat(TEXT("settings")); })]
		]
		+ SVerticalBox::Slot().FillHeight(1.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 6.f, 0.f)[Menu]
			+ SHorizontalBox::Slot().FillWidth(1.f)[SNew(SBorder).BorderImage(Brush(ListArea())).Padding(4.f)[SNew(SScrollBox) + SScrollBox::Slot()[Body]]]
		];
}
