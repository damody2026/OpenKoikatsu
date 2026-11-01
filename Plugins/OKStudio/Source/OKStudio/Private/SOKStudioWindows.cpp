// OK Studio panel: floating windows and their saved places, the dialog, the workspace tree's editing, scene files
// (browser, backups, the open report, the resource check, stories), screenshots, the timeline and its export, settings.
#include "SOKStudioPanel.h"
#include "KKCardReader.h"
#include "KKCardWorkspace.h"
#include "KKRustExtract.h"
#include "ImageUtils.h"
#include "Misc/Base64.h"
#include "Algo/Find.h"
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
	AddFloat(TEXT("mmd"), TEXT("MMD 導演／設定（MMDD / KKVMD）"), FMargin(140.f, 47.f, 430.f, 520.f), [this] { return BuildMmdDirector(); });
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
	AddFloat(TEXT("material"), TEXT("材質編輯器"), FMargin(300.f, 60.f, 640.f, 420.f), [this] { return BuildMaterialEditor(); });
	AddFloat(TEXT("animctl"), TEXT("動畫控制器"), FMargin(470.f, 400.f, 300.f, 150.f), [this] { return BuildAnimController(); });
	AddFloat(TEXT("bgm"), TEXT("背景音樂"), FMargin(142.f, 36.f, 280.f, 420.f), [this] { return BuildBgm(); });
	AddFloat(TEXT("outside"), TEXT("外部音源"), FMargin(142.f, 36.f, 320.f, 360.f), [this] { return BuildOutsideSound(); });
	AddFloat(TEXT("voice"), TEXT("語音"), FMargin(142.f, 36.f, 600.f, 420.f), [this] { return BuildVoice(); });
	AddFloat(TEXT("help"), TEXT("說明"), FMargin(640.f, 120.f, 300.f, 140.f), [this]
	{
		return SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)[OKStudioStyle::Label(FText::FromString(PinnedHelpTitle), 11.f)]
			+ SVerticalBox::Slot().FillHeight(1.f)[SNew(STextBlock).Text(FText::FromString(PinnedHelp)).Font(OKStudioStyle::Font(10.f)).ColorAndOpacity(OKStudioStyle::Text()).AutoWrapText(true)];
	});
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
	// A toggle closes the window only when it is in front: one under another window (the 動畫 page over the card list)
	// comes up instead, as clicking its menu button again would mean.
	if (F->bOpen && bToggle && (!F->Slot || F->Slot->GetZOrder() >= TopZ)) { CloseFloat(Id); return; }
	if (Algo::FindByPredicate(AddLists, [&Id](const TCHAR* L) { return Id == L; }))
	{
		CloseAddLists(Id);
		if (Page == EPage::ItemAdd || Page == EPage::LightAdd) SetPage(EPage::None);
	}
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

void SOKStudioPanel::CloseAddLists(const FString& Keep)
{
	for (const TCHAR* Other : AddLists) if (Keep != Other && IsFloatOpen(Other)) CloseFloat(Other);
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
	// The material editor follows the selection, and fills in once the item has built.
	if (IsFloatOpen(TEXT("material")))
	{
		const AKKPreviewActor* Shown = Game->SceneActor ? Game->SceneActor->PreviewOf(Game->SelectedId) : nullptr;
		const TPair<int32, int32> Now(Game->SelectedId, Shown ? Shown->Session.PartCount() : 0);
		if (Now != MaterialShown) { if (Now.Key != MaterialShown.Key) MaterialPart = 0; MaterialShown = Now; DeferFloat(TEXT("material")); }
	}
	// The animation controller lists the Studio controls of the selected character's motion, which change with it.
	if (IsFloatOpen(TEXT("animctl")))
	{
		FString Now = FString::FromInt(Game->SelectedId);
		if (Game->SceneActor) for (const TPair<FString, float>& P : Game->SceneActor->BlendControlsOf(Game->SelectedId)) Now += TEXT("|") + P.Key;
		if (Now != AnimCtlShown) { AnimCtlShown = Now; DeferFloat(TEXT("animctl")); }
	}
	// The voice window shows the selected character's list and what it browses.
	if (IsFloatOpen(TEXT("voice")))
	{
		FString Now = FString::Printf(TEXT("%d|%d|%d|%d"), Game->SelectedId, VoiceGroupPick, VoiceCategoryPick, VoiceListPick);
		if (const FOKSceneObject* O = Game->Document->Find(Game->SelectedId); O && O->Kind == EOKSceneObjectKind::Character)
			for (const FOKSceneVoice& V : O->Character.Voices) Now += FString::Printf(TEXT("|%d.%d.%d"), V.Group, V.Category, V.No);
		if (Now != VoiceShown) { VoiceShown = Now; DeferFloat(TEXT("voice")); }
	}
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
	if (bSceneRecursive)
	{
		// 包含子資料夾 (HS2Studio6): every scene under the folder, except the cards an import took out of a scene
		// ("<scene>_cards", character PNGs only).
		TArray<FString> Found;
		IFileManager::Get().FindFilesRecursive(Found, *SceneFolder, TEXT("*.png"), true, false);
		TArray<FString> More;
		IFileManager::Get().FindFilesRecursive(More, *SceneFolder, TEXT("*.okscene"), true, false);
		Found.Append(More);
		for (const FString& F : Found)
		{
			if (FPaths::GetPath(F).Contains(TEXT("_cards"))) continue;
			if (F.EndsWith(TEXT(".okscene")) || FOKSceneCard::Kind(F) != FOKSceneCard::EKind::None) SceneList.Add(F);
		}
	}
	else if (SceneFolder == UOKStudioGame::SceneDirectory()) SceneList = UOKStudioGame::SceneFiles();
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
	// "<scene>_cards" beside its scene holds the cards an import took out of it: no scenes to browse there.
	auto SceneCards = [this](const FString& S)
	{
		const FString Scene = SceneFolder / S.LeftChop(6);
		return S.EndsWith(TEXT("_cards")) && (FPaths::FileExists(Scene + TEXT(".okscene")) || FPaths::FileExists(Scene + TEXT(".png")));
	};
	for (const FString& S : Subs) if (!SceneCards(S)) FolderButton(TEXT("▸ ") + S, SceneFolder / S);
	if (SceneFolder != UOKStudioGame::SceneDirectory() && SceneFolder != KStudioFolder() && SceneFolder != UOKStudioGame::BackupDirectory()) FolderButton(TEXT(".. 上一層"), FPaths::GetPath(SceneFolder));
	// 包含子資料夾 and 選擇資料夾 (HS2Studio6 场景卡选择): any folder, typed or pasted, Enter goes there.
	Folders->AddSlot().AutoHeight().Padding(0.f, 6.f, 0.f, 2.f)
		[CheckBox(TEXT("包含子資料夾"), [this] { return bSceneRecursive; }, [this](bool B) { bSceneRecursive = B; ScenePick.Reset(); ScanScenes(); DeferFloat(TEXT("scenes")); })];
	Folders->AddSlot().AutoHeight().Padding(0.f, 2.f)
		[SNew(SBox).HeightOverride(18.f)[SNew(SEditableTextBox).Style(&TextBoxStyle()).HintText(FText::FromString(TEXT("選擇資料夾（貼上路徑按 Enter）")))
			.OnTextCommitted_Lambda([this](const FText& T, ETextCommit::Type C)
			{
				if (C != ETextCommit::OnEnter) return;
				const FString Dir = FPaths::ConvertRelativePathToFull(T.ToString().TrimStartAndEnd().TrimQuotes());
				if (Dir.IsEmpty() || !FPaths::DirectoryExists(Dir)) { SetStatus(TEXT("沒有這個資料夾：") + Dir); return; }
				SceneFolder = Dir;
				ScenePick.Reset();
				ScanScenes();
				DeferFloat(TEXT("scenes"));
			})]];

	// Pictures of the scenes, four across.
	TSharedRef<SWrapBox> Grid = SNew(SWrapBox).UseAllottedSize(true);
	// A whole tree (包含子資料夾) can be thousands of scenes: the first 500 are shown, the search narrows the rest.
	constexpr int32 MaxShown = 500;
	for (int32 Index = 0; Index < SceneList.Num(); ++Index)
	{
		if (Index == MaxShown)
		{
			Grid->AddSlot().Padding(4.f)[OKStudioStyle::Label(FText::FromString(FString::Printf(TEXT("還有 %d 個場景沒列出，請用搜尋縮小範圍。"), SceneList.Num() - MaxShown)), 10.f, TextHint())];
			break;
		}
		const FString& F = SceneList[Index];
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
						+ SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Font(Font(9.f)).ColorAndOpacity(OKStudioStyle::Text()).OverflowPolicy(ETextOverflowPolicy::Ellipsis).AddMetaData<FTagMetaData>(TEXT("Ellipsis")).Text(FText::FromString(KindTag + FPaths::GetBaseFilename(F)))]
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
	else
	{
		TSharedRef<STextBlock> Hint = OKStudioStyle::Label(FText::FromString(TEXT("點一個場景來預覽，再選覆蓋或疊加。")), 10.f, TextHint());
		Hint->SetAutoWrapText(true);
		Pick->AddSlot().AutoHeight()[Hint];
	}
	if (SaveAsName.IsEmpty()) SaveAsName = Game.IsValid() && !Game->File.IsEmpty() ? FPaths::GetBaseFilename(Game->File) : FDateTime::Now().ToString(TEXT("scene_%Y%m%d_%H%M%S"));
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 3.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f)[SNew(SBox).HeightOverride(20.f)[SNew(SEditableTextBox).Style(&TextBoxStyle()).HintText(FText::FromString(TEXT("場景名稱"))).Text_Lambda([this] { return FText::FromString(SaveAsName); })
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

TSharedRef<SWidget> SOKStudioPanel::BuildBgm()
{
	// Studio's BGM window: pick a piece of the BGM list (the base library's converted clips), then play / stop; it
	// plays apart from the timeline and is saved with the scene (FOKSceneSound).
	using namespace OKStudioStyle;
	const FOKStudioCatalog& C = FOKStudioCatalog::Get();
	auto Sound = [this]() -> FOKSceneSound* { return Game.IsValid() && Game->Document ? &Game->Document->Sound : nullptr; };
	TSharedRef<SVerticalBox> SoundRows = SNew(SVerticalBox);
	int32 Missing = 0;
	for (const FOKStudioBgm& B : C.Bgm)
	{
		const int32 No = B.No;
		const bool bThere = !C.BgmSound(No).IsEmpty();
		Missing += bThere ? 0 : 1;
		SoundRows->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 1.f)
			[Button(FText::FromString(FString::Printf(TEXT("%02d %s"), No, *B.Name)), [this, Sound, No] { if (FOKSceneSound* S = Sound()) { S->Bgm = No; Changed(); } }, 0.f, 18.f,
				TAttribute<bool>::CreateLambda([Sound, No] { const FOKSceneSound* S = Sound(); return S && S->Bgm == No; }), 10.f, ButtonFill(), bThere)];
	}
	const FString Note = C.Bgm.IsEmpty() ? TEXT("基礎庫沒有 BGM 清單：執行 ok-extract <遊戲> --oklib studio-sounds <基礎庫>。")
		: Missing ? FString::Printf(TEXT("%d 首尚未轉換（灰色）。"), Missing) : FString();
	TSharedRef<STextBlock> State = OKStudioStyle::Label(TAttribute<FText>::CreateLambda([this, Sound]
	{
		const FOKSceneSound* S = Sound();
		if (!S || !Game.IsValid()) return FText();
		const FOKStudioBgm* B = FOKStudioCatalog::Get().FindBgm(S->Bgm);
		const FString Name = B ? FString::Printf(TEXT("%02d %s"), B->No, *B->Name) : FString::Printf(TEXT("%d（不在清單）"), S->Bgm);
		return FText::FromString(Name + (Game->IsBgmPlaying() ? TEXT("：播放中") : S->bBgmPlay ? TEXT("：準備中／已播完") : TEXT("：停止")));
	}), 10.f);
	State->SetAutoWrapText(true);
	TSharedRef<SVerticalBox> Box = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 3.f)[State]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 3.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f).Padding(0.f, 0.f, 2.f, 0.f)[Button(FText::FromString(TEXT("播放")), [this, Sound] { if (FOKSceneSound* S = Sound()) { S->bBgmPlay = true; Changed(); } }, 0.f, 20.f,
				TAttribute<bool>::CreateLambda([Sound] { const FOKSceneSound* S = Sound(); return S && S->bBgmPlay; }), 10.f)]
			+ SHorizontalBox::Slot().FillWidth(1.f).Padding(0.f, 0.f, 2.f, 0.f)[Button(FText::FromString(TEXT("停止")), [this, Sound] { if (FOKSceneSound* S = Sound()) { S->bBgmPlay = false; Changed(); } }, 0.f, 20.f, false, 10.f)]
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)[CheckBox(TEXT("循環"), [Sound] { const FOKSceneSound* S = Sound(); return S && S->bBgmRepeat; }, [this, Sound](bool B) { if (FOKSceneSound* S = Sound()) { S->bBgmRepeat = B; Changed(); } })]
		];
	if (!Note.IsEmpty())
	{
		TSharedRef<STextBlock> Hint = OKStudioStyle::Label(FText::FromString(Note), 9.f, TextHint());
		Hint->SetAutoWrapText(true);
		Box->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 3.f)[Hint];
	}
	Box->AddSlot().FillHeight(1.f)[SNew(SBorder).BorderImage(Brush(ListArea())).Padding(3.f)[SNew(SScrollBox) + SScrollBox::Slot()[SoundRows]]];
	return Box;
}

TSharedRef<SWidget> SOKStudioPanel::BuildOutsideSound()
{
	// Studio's outside sound: an Ogg Vorbis or WAV file of UserData/audio (or beside the scene), play / stop, repeat.
	using namespace OKStudioStyle;
	auto Sound = [this]() -> FOKSceneSound* { return Game.IsValid() && Game->Document ? &Game->Document->Sound : nullptr; };
	const FString Folder = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("UserData/audio"));
	TArray<FString> Files;
	for (const TCHAR* Ext : { TEXT("*.ogg"), TEXT("*.wav") })
	{
		TArray<FString> Found;
		IFileManager::Get().FindFiles(Found, *(Folder / Ext), true, false);
		for (const FString& F : Found) Files.Add(Folder / F);
	}
	// A scene's own file (an imported scene's copy beside its cards) is listed too.
	if (const FOKSceneSound* S = Sound(); S && !S->Outside.IsEmpty() && Game.IsValid() && !Game->OutsideSoundFile().IsEmpty()) Files.AddUnique(Game->OutsideSoundFile());
	Files.Sort();
	TSharedRef<SVerticalBox> SoundRows = SNew(SVerticalBox);
	for (const FString& F : Files)
		SoundRows->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 1.f)
			[Button(FText::FromString(FPaths::GetCleanFilename(F)), [this, Sound, F] { if (FOKSceneSound* S = Sound()) { S->Outside = F; Changed(); } }, 0.f, 18.f,
				TAttribute<bool>::CreateLambda([this, F] { return Game.IsValid() && Game->OutsideSoundFile() == F; }), 10.f)];
	if (Files.IsEmpty())
	{
		TSharedRef<STextBlock> Hint = OKStudioStyle::Label(FText::FromString(TEXT("把 .ogg 或 .wav 放進 ") + Folder + TEXT("，再開一次這個視窗。")), 10.f, TextHint());
		Hint->SetAutoWrapText(true);
		SoundRows->AddSlot().AutoHeight()[Hint];
	}
	TSharedRef<STextBlock> State = OKStudioStyle::Label(TAttribute<FText>::CreateLambda([this, Sound]
	{
		const FOKSceneSound* S = Sound();
		if (!S || !Game.IsValid()) return FText();
		const FString Name = S->Outside.IsEmpty() ? FString(TEXT("（未選檔）")) : FPaths::GetCleanFilename(S->Outside) + (Game->OutsideSoundFile().IsEmpty() ? TEXT("（找不到檔案）") : TEXT(""));
		return FText::FromString(Name + (Game->IsOutsideSoundPlaying() ? TEXT("：播放中") : S->bOutsidePlay ? TEXT("：準備中／已播完") : TEXT("：停止")));
	}), 10.f);
	State->SetAutoWrapText(true);
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 3.f)[State]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 3.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f).Padding(0.f, 0.f, 2.f, 0.f)[Button(FText::FromString(TEXT("播放")), [this, Sound] { if (FOKSceneSound* S = Sound()) { S->bOutsidePlay = true; Changed(); } }, 0.f, 20.f,
				TAttribute<bool>::CreateLambda([Sound] { const FOKSceneSound* S = Sound(); return S && S->bOutsidePlay; }), 10.f)]
			+ SHorizontalBox::Slot().FillWidth(1.f).Padding(0.f, 0.f, 2.f, 0.f)[Button(FText::FromString(TEXT("停止")), [this, Sound] { if (FOKSceneSound* S = Sound()) { S->bOutsidePlay = false; Changed(); } }, 0.f, 20.f, false, 10.f)]
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)[CheckBox(TEXT("循環"), [Sound] { const FOKSceneSound* S = Sound(); return S && S->bOutsideRepeat; }, [this, Sound](bool B) { if (FOKSceneSound* S = Sound()) { S->bOutsideRepeat = B; Changed(); } })]
		]
		+ SVerticalBox::Slot().FillHeight(1.f)[SNew(SBorder).BorderImage(Brush(ListArea())).Padding(3.f)[SNew(SScrollBox) + SScrollBox::Slot()[SoundRows]]];
}

TSharedRef<SWidget> SOKStudioPanel::BuildVoice()
{
	// Studio's voice window: personality → category → voice (a click adds it to the selected character's list), and
	// the list with play / stop, repeat, remove, move up / down and clear. The character says it with its mouth moving.
	using namespace OKStudioStyle;
	const FOKStudioCatalog& C = FOKStudioCatalog::Get();
	const int32 Id = Game.IsValid() ? Game->SelectedId : -1;
	auto Char = [this, Id]() -> FOKSceneCharacter*
	{
		FOKSceneObject* O = Game.IsValid() ? Game->Document->Find(Id) : nullptr;
		return O && O->Kind == EOKSceneObjectKind::Character ? &O->Character : nullptr;
	};
	const FOKSceneObject* Selected = Game.IsValid() ? Game->Document->Find(Id) : nullptr;
	if (!Char())
		return OKStudioStyle::Label(FText::FromString(TEXT("先選擇一個角色。")), 10.f, TextHint());
	if (C.Voices.IsEmpty())
	{
		TSharedRef<STextBlock> Hint = OKStudioStyle::Label(FText::FromString(TEXT("基礎庫沒有語音清單：執行 ok-extract <遊戲> --oklib studio-voices <基礎庫>。")), 10.f, TextHint());
		Hint->SetAutoWrapText(true);
		return Hint;
	}
	auto Column = [](const TSharedRef<SVerticalBox>& Rows) -> TSharedRef<SWidget> { return SNew(SBorder).BorderImage(Brush(ListArea())).Padding(3.f)[SNew(SScrollBox) + SScrollBox::Slot()[Rows]]; };
	// Long lines (voice texts) end in an ellipsis, the whole line in the tooltip.
	auto Row = [](TAttribute<FText> Text, TFunction<void()> Click, TAttribute<bool> bOn, bool bEnabled = true) -> TSharedRef<SWidget>
	{
		return SNew(SBox).HeightOverride(18.f).ToolTipText(Text)
		[
			Fill(bOn, ButtonFill(),
				SNew(SButton).ButtonStyle(&FlatButton()).IsEnabled(bEnabled).HAlign(HAlign_Left).VAlign(VAlign_Center).ContentPadding(FMargin(3.f, 0.f))
				.OnClicked_Lambda([Click] { if (Click) Click(); return FReply::Handled(); })
				[SNew(STextBlock).Font(Font(10.f)).ColorAndOpacity(OKStudioStyle::Text()).OverflowPolicy(ETextOverflowPolicy::Ellipsis).AddMetaData<FTagMetaData>(TEXT("Ellipsis")).Text(Text)])
		];
	};
	TSharedRef<SVerticalBox> GroupRows = SNew(SVerticalBox), CategoryRows = SNew(SVerticalBox), VoiceRows = SNew(SVerticalBox), ListRows = SNew(SVerticalBox);
	for (const FOKStudioCatalogGroup& G : C.VoiceGroups)
	{
		const int32 No = G.Id;
		GroupRows->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 1.f)[Row(FText::FromString(G.Name), [this, No] { VoiceGroupPick = No; VoiceCategoryPick = 0; }, VoiceGroupPick == No)];
	}
	for (const FOKStudioCatalogGroup& G : C.VoiceCategories)
	{
		if (G.Group != VoiceGroupPick) continue;
		const int32 No = G.Id;
		CategoryRows->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 1.f)[Row(FText::FromString(G.Name), [this, No] { VoiceCategoryPick = No; }, VoiceCategoryPick == No)];
	}
	int32 Missing = 0;
	for (const FOKStudioVoice& V : C.Voices)
	{
		if (V.Group != VoiceGroupPick || V.Category != VoiceCategoryPick) continue;
		const bool bThere = !C.VoiceSound(V.Group, V.Category, V.No).IsEmpty();
		Missing += bThere ? 0 : 1;
		const FOKSceneVoice Entry{ V.Group, V.Category, V.No };
		VoiceRows->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 1.f)
			[Row(FText::FromString(FString::Printf(TEXT("%d %s"), V.No, *V.Name)), [this, Char, Entry] { if (FOKSceneCharacter* Ch = Char()) { Ch->Voices.Add(Entry); VoiceListPick = Ch->Voices.Num() - 1; Changed(); } }, false, bThere)];
	}
	const TArray<FOKSceneVoice>& VoiceList = Char()->Voices;
	for (int32 I = 0; I < VoiceList.Num(); ++I)
	{
		const FOKStudioVoice* V = C.FindVoice(VoiceList[I].Group, VoiceList[I].Category, VoiceList[I].No);
		const FString Name = V ? V->Name : FString::Printf(TEXT("（不在清單 %d/%d/%d）"), VoiceList[I].Group, VoiceList[I].Category, VoiceList[I].No);
		ListRows->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 1.f)
			[Row(TAttribute<FText>::CreateLambda([this, Id, I, Name] { return FText::FromString(FString::Printf(TEXT("%s%d. %s"), Game.IsValid() && Game->VoicePlayingIndex(Id) == I ? TEXT("▶ ") : TEXT(""), I + 1, *Name)); }),
				[this, I] { VoiceListPick = I; }, VoiceListPick == I, V && !C.VoiceSound(V->Group, V->Category, V->No).IsEmpty())];
	}
	if (VoiceList.IsEmpty())
	{
		TSharedRef<STextBlock> Hint = OKStudioStyle::Label(FText::FromString(TEXT("點左邊的語音加入清單。")), 10.f, TextHint());
		Hint->SetAutoWrapText(true);
		ListRows->AddSlot().AutoHeight()[Hint];
	}
	// The list's edits keep the picked entry on the same voice.
	auto Edit = [this, Char](TFunction<void(TArray<FOKSceneVoice>&, int32&)> Do) { if (FOKSceneCharacter* Ch = Char()) { Do(Ch->Voices, VoiceListPick); Ch->VoiceStart = FMath::Clamp(Ch->VoiceStart, 0, FMath::Max(Ch->Voices.Num() - 1, 0)); Changed(); } };
	static const TCHAR* RepeatNames[] = { TEXT("播完停止"), TEXT("全部循環"), TEXT("單句循環") };
	TSharedRef<STextBlock> State = OKStudioStyle::Label(TAttribute<FText>::CreateLambda([this, Id, Char]
	{
		const FOKSceneCharacter* Ch = Char();
		if (!Ch || !Game.IsValid()) return FText();
		const int32 Now = Game->VoicePlayingIndex(Id);
		return FText::FromString(FString::Printf(TEXT("%d 句：%s"), Ch->Voices.Num(), Now >= 0 ? *FString::Printf(TEXT("說第 %d 句"), Now + 1) : Ch->bVoicePlay ? TEXT("準備中／已播完") : TEXT("停止")));
	}), 10.f);
	State->SetAutoWrapText(true);
	TSharedRef<SVerticalBox> Box = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 3.f)[OKStudioStyle::Label(FText::FromString(TEXT("目前角色：") + (Selected ? Selected->Name : FString())), 10.f)]
		+ SVerticalBox::Slot().FillHeight(1.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(0.8f).Padding(0.f, 0.f, 2.f, 0.f)[Column(GroupRows)]
			+ SHorizontalBox::Slot().FillWidth(1.f).Padding(0.f, 0.f, 2.f, 0.f)[Column(CategoryRows)]
			+ SHorizontalBox::Slot().FillWidth(1.4f).Padding(0.f, 0.f, 4.f, 0.f)[Column(VoiceRows)]
			+ SHorizontalBox::Slot().FillWidth(1.4f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 2.f)[State]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 2.f)
				[
					SNew(SHorizontalBox)
					// 播放 starts from the picked entry (or the first).
					+ SHorizontalBox::Slot().FillWidth(1.f).Padding(0.f, 0.f, 2.f, 0.f)[Button(FText::FromString(TEXT("播放")), [this, Char] { if (FOKSceneCharacter* Ch = Char(); Ch && Ch->Voices.Num()) { Ch->VoiceStart = Ch->Voices.IsValidIndex(VoiceListPick) ? VoiceListPick : 0; Ch->bVoicePlay = true; Changed(); } }, 0.f, 20.f,
						TAttribute<bool>::CreateLambda([Char] { const FOKSceneCharacter* Ch = Char(); return Ch && Ch->bVoicePlay; }), 10.f)]
					+ SHorizontalBox::Slot().FillWidth(1.f).Padding(0.f, 0.f, 2.f, 0.f)[Button(FText::FromString(TEXT("停止")), [this, Char] { if (FOKSceneCharacter* Ch = Char()) { Ch->bVoicePlay = false; Changed(); } }, 0.f, 20.f, false, 10.f)]
					+ SHorizontalBox::Slot().FillWidth(1.2f)[Button(TAttribute<FText>::CreateLambda([Char] { const FOKSceneCharacter* Ch = Char(); return FText::FromString(RepeatNames[Ch ? FMath::Clamp(Ch->VoiceRepeat, 0, 2) : 1]); }),
						[this, Char] { if (FOKSceneCharacter* Ch = Char()) { Ch->VoiceRepeat = (FMath::Clamp(Ch->VoiceRepeat, 0, 2) + 1) % 3; Changed(); } }, 0.f, 20.f, false, 10.f)]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 2.f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1.f).Padding(0.f, 0.f, 2.f, 0.f)[Button(FText::FromString(TEXT("移除")), [Edit] { Edit([](TArray<FOKSceneVoice>& L, int32& P) { if (L.IsValidIndex(P)) { L.RemoveAt(P); P = FMath::Min(P, L.Num() - 1); } }); }, 0.f, 20.f, false, 10.f)]
					+ SHorizontalBox::Slot().FillWidth(1.f).Padding(0.f, 0.f, 2.f, 0.f)[Button(FText::FromString(TEXT("上移")), [Edit] { Edit([](TArray<FOKSceneVoice>& L, int32& P) { if (P > 0 && L.IsValidIndex(P)) { L.Swap(P, P - 1); --P; } }); }, 0.f, 20.f, false, 10.f)]
					+ SHorizontalBox::Slot().FillWidth(1.f).Padding(0.f, 0.f, 2.f, 0.f)[Button(FText::FromString(TEXT("下移")), [Edit] { Edit([](TArray<FOKSceneVoice>& L, int32& P) { if (P >= 0 && P + 1 < L.Num()) { L.Swap(P, P + 1); ++P; } }); }, 0.f, 20.f, false, 10.f)]
					+ SHorizontalBox::Slot().FillWidth(1.f)[Button(FText::FromString(TEXT("清空")), [Edit] { Edit([](TArray<FOKSceneVoice>& L, int32& P) { L.Reset(); P = -1; }); }, 0.f, 20.f, false, 10.f)]
				]
				+ SVerticalBox::Slot().FillHeight(1.f)[Column(ListRows)]
			]
		];
	if (Missing)
	{
		TSharedRef<STextBlock> Hint = OKStudioStyle::Label(FText::FromString(FString::Printf(TEXT("這個分類有 %d 句尚未轉換（灰色）。"), Missing)), 9.f, TextHint());
		Hint->SetAutoWrapText(true);
		Box->AddSlot().AutoHeight().Padding(0.f, 3.f, 0.f, 0.f)[Hint];
	}
	return Box;
}

TSharedRef<SWidget> SOKStudioPanel::BuildMaterialEditor()
{
	// HS2Studio6's Material Editor: the selected item's renderers, the material of the one picked, and that material's
	// shader properties (filtered by name). A change is a material edit of the item, saved with the scene; 預設 drops it.
	FOKSceneObject* O = Game.IsValid() ? Game->Document->Find(Game->SelectedId) : nullptr;
	AKKPreviewActor* Shown = O && O->Kind == EOKSceneObjectKind::Item && Game->SceneActor ? Game->SceneActor->PreviewOf(O->Id) : nullptr;
	const int32 Parts = Shown ? Shown->Session.PartCount() : 0;
	MaterialPropertiesShown = 0;
	MaterialNameShown.Reset();
	if (Parts <= 0) return OKStudioStyle::Label(FText::FromString(TEXT("選取一個已載入的道具，這裡會列出它的模型、材質與屬性。")), 10.f, TextHint());
	MaterialPart = FMath::Clamp(MaterialPart, 0, Parts - 1);
	const int32 ItemId = O->Id;
	auto Column = [this](const FString& Title, const TSharedRef<SWidget>& Body)
	{
		return SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 2.f)[OKStudioStyle::Label(FText::FromString(Title), 10.f, TextHint())]
			+ SVerticalBox::Slot().FillHeight(1.f)[SNew(SBorder).BorderImage(Brush(ListArea())).Padding(2.f)[SNew(SScrollBox) + SScrollBox::Slot()[Body]]];
	};
	TSharedRef<SVerticalBox> Models = SNew(SVerticalBox);
	for (int32 I = 0; I < Parts; ++I)
	{
		OkMeshView Mesh{};
		const FString Name = Shown->Session.PartAt(I, Mesh) && Mesh.name && *Mesh.name ? FString(UTF8_TO_TCHAR(Mesh.name)) : FString::Printf(TEXT("部件 %d"), I);
		Models->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 1.f)[Button(FText::FromString(Name), [this, I] { MaterialPart = I; RebuildFloat(TEXT("material")); }, 0.f, 18.f, MaterialPart == I, 10.f)];
	}
	// The picked renderer's source material (name, shader, values), as the card's bundle has it.
	FString Material, Shader;
	TArray<TPair<FString, FLinearColor>> Colors;
	TArray<TPair<FString, float>> Numbers;
	TArray<TPair<FString, FString>> Textures;
	TSharedPtr<FJsonObject> Root;
	const TSharedPtr<FJsonObject>* M = nullptr;
	if (FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Shown->Session.MaterialMetadataJson(MaterialPart)), Root) && Root && Root->TryGetObjectField(TEXT("material"), M) && M && *M)
	{
		(*M)->TryGetStringField(TEXT("name"), Material);
		(*M)->TryGetStringField(TEXT("shader"), Shader);
		const TSharedPtr<FJsonObject>* Map = nullptr;
		if ((*M)->TryGetObjectField(TEXT("colors"), Map) && Map && *Map)
			for (const auto& P : (*Map)->Values)
			{
				const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
				if (P.Value->TryGetArray(A) && A->Num() >= 4) Colors.Add({ FString(*P.Key), FLinearColor((*A)[0]->AsNumber(), (*A)[1]->AsNumber(), (*A)[2]->AsNumber(), (*A)[3]->AsNumber()) });
			}
		if ((*M)->TryGetObjectField(TEXT("floats"), Map) && Map && *Map)
			for (const auto& P : (*Map)->Values) if (!FString(*P.Key).StartsWith(TEXT("_OK"))) Numbers.Add({ FString(*P.Key), float(P.Value->AsNumber()) });
		const TArray<TSharedPtr<FJsonValue>>* Slots = nullptr;
		if ((*M)->TryGetArrayField(TEXT("tex_slots"), Slots))
			for (const TSharedPtr<FJsonValue>& S : *Slots)
			{
				const TArray<TSharedPtr<FJsonValue>>* Pair = nullptr;
				if (S->TryGetArray(Pair) && Pair->Num() >= 2) Textures.Add({ (*Pair)[0]->AsString(), (*Pair)[1]->AsString() });
			}
	}
	MaterialNameShown = Material;
	MaterialPropertiesShown = Colors.Num() + Numbers.Num() + Textures.Num();
	Colors.Sort([](const auto& A, const auto& B) { return A.Key < B.Key; });
	Numbers.Sort([](const auto& A, const auto& B) { return A.Key < B.Key; });
	TSharedRef<SVerticalBox> Materials = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()[Button(FText::FromString(Material.IsEmpty() ? TEXT("（無名材質）") : Material), [] {}, 0.f, 18.f, true, 10.f)]
		+ SVerticalBox::Slot().AutoHeight().Padding(2.f)[OKStudioStyle::Label(FText::FromString(Shader), 9.f, TextHint())];
	// One edit of the item (kind, material or renderer, property), or null; Put adds it when missing.
	auto FindEdit = [this, ItemId](EOKSceneMaterialEditKind Kind, const FString& Target, const FString& Property) -> FOKSceneMaterialEdit*
	{
		FOKSceneObject* X = Game.IsValid() ? Game->Document->Find(ItemId) : nullptr;
		if (!X) return nullptr;
		return X->Item.MaterialEdits.FindByPredicate([&](const FOKSceneMaterialEdit& E) { return E.Kind == Kind && E.Material == Target && (Property.IsEmpty() || E.Property == Property || TEXT("_") + E.Property == Property); });
	};
	auto Put = [this, ItemId, FindEdit](EOKSceneMaterialEditKind Kind, const FString& Target, const FString& Property) -> FOKSceneMaterialEdit*
	{
		FOKSceneObject* X = Game.IsValid() ? Game->Document->Find(ItemId) : nullptr;
		if (!X) return nullptr;
		FOKSceneMaterialEdit* E = FindEdit(Kind, Target, Property);
		if (!E) { E = &X->Item.MaterialEdits.AddDefaulted_GetRef(); E->Kind = Kind; E->Material = Target; E->Property = Property; }
		return E;
	};
	auto Drop = [this, ItemId](EOKSceneMaterialEditKind Kind, const FString& Target, const FString& Property)
	{
		if (FOKSceneObject* X = Game.IsValid() ? Game->Document->Find(ItemId) : nullptr)
			X->Item.MaterialEdits.RemoveAll([&](const FOKSceneMaterialEdit& E) { return E.Kind == Kind && E.Material == Target && (E.Property == Property || TEXT("_") + E.Property == Property); });
	};
	// The picked renderer (HS2Studio6 Renderer 頁): shown or not, and how it casts shadows (Unity ShadowCastingMode).
	OkMeshView PickedMesh{};
	const FString Renderer = Shown->Session.PartAt(MaterialPart, PickedMesh) && PickedMesh.name ? FString(UTF8_TO_TCHAR(PickedMesh.name)) : FString();
	if (!Renderer.IsEmpty())
	{
		static const TCHAR* ShadowNames[] = { TEXT("不投影"), TEXT("投影"), TEXT("雙面投影"), TEXT("只有影子") };
		auto RendererValue = [FindEdit, Renderer](const TCHAR* Property, float Default) { const FOKSceneMaterialEdit* E = FindEdit(EOKSceneMaterialEditKind::Renderer, Renderer, Property); return E ? E->Value.R : Default; };
		auto SetRenderer = [this, Put, Drop, Renderer](const TCHAR* Property, float Value, float Default)
		{
			if (FMath::IsNearlyEqual(Value, Default)) Drop(EOKSceneMaterialEditKind::Renderer, Renderer, Property);
			else if (FOKSceneMaterialEdit* E = Put(EOKSceneMaterialEditKind::Renderer, Renderer, Property)) E->Value = FLinearColor(Value, 0.f, 0.f, 0.f);
			Changed();
		};
		Materials->AddSlot().AutoHeight().Padding(0.f, 4.f, 0.f, 1.f)[OKStudioStyle::Label(FText::FromString(TEXT("Renderer")), 10.f, TextHint())];
		Materials->AddSlot().AutoHeight()[CheckBox(TEXT("顯示"), [RendererValue] { return RendererValue(TEXT("Enabled"), 1.f) > 0.5f; }, [SetRenderer](bool B) { SetRenderer(TEXT("Enabled"), B ? 1.f : 0.f, 1.f); })];
		Materials->AddSlot().AutoHeight().Padding(0.f, 1.f)[Button(TAttribute<FText>::CreateLambda([RendererValue] { return FText::FromString(FString(TEXT("陰影：")) + ShadowNames[FMath::Clamp(FMath::RoundToInt(RendererValue(TEXT("ShadowCastingMode"), 1.f)), 0, 3)]); }),
			[RendererValue, SetRenderer] { SetRenderer(TEXT("ShadowCastingMode"), float((FMath::Clamp(FMath::RoundToInt(RendererValue(TEXT("ShadowCastingMode"), 1.f)), 0, 3) + 1) % 4), 1.f); }, 0.f, 18.f, false, 10.f)];
	}
	// The material's shader and render queue (Material Editor 换 shader): the item is built again with them.
	{
		auto ShaderEdit = [FindEdit, Material] { return FindEdit(EOKSceneMaterialEditKind::Shader, Material, FString()); };
		Materials->AddSlot().AutoHeight().Padding(0.f, 4.f, 0.f, 1.f)[OKStudioStyle::Label(FText::FromString(TEXT("Shader（Enter 套用；空白回原本）")), 10.f, TextHint())];
		Materials->AddSlot().AutoHeight()[SNew(SBox).HeightOverride(18.f)[TextBox([ShaderEdit, Shader] { const FOKSceneMaterialEdit* E = ShaderEdit(); return E ? E->Text : Shader; },
			[this, Put, Drop, Material, Shader, ShaderEdit](const FString& V)
			{
				const FString Name = V.TrimStartAndEnd();
				const FOKSceneMaterialEdit* Old = ShaderEdit();
				const float Queue = Old ? Old->Value.R : -1.f;
				if (Name.IsEmpty() || (Name == Shader && Queue < 0.f)) Drop(EOKSceneMaterialEditKind::Shader, Material, FString());
				else if (FOKSceneMaterialEdit* E = Put(EOKSceneMaterialEditKind::Shader, Material, FString())) { E->Text = Name; E->Value = FLinearColor(Queue, 0.f, 0.f, 0.f); }
				Changed();
			}, Shader)]];
		Materials->AddSlot().AutoHeight().Padding(0.f, 1.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(0.5f).VAlign(VAlign_Center)[OKStudioStyle::Label(FText::FromString(TEXT("Render Queue")), 9.f)]
			+ SHorizontalBox::Slot().FillWidth(0.5f)[SNew(SBox).HeightOverride(18.f)[Spin([ShaderEdit] { const FOKSceneMaterialEdit* E = ShaderEdit(); return E ? E->Value.R : -1.f; },
				[this, Put, Material, Shader, ShaderEdit](float V)
				{
					// -1 keeps the material's own queue; a queue alone keeps the shader.
					const FOKSceneMaterialEdit* Old = ShaderEdit();
					const FString Name = Old && !Old->Text.IsEmpty() ? Old->Text : Shader;
					if (FOKSceneMaterialEdit* E = Put(EOKSceneMaterialEditKind::Shader, Material, FString())) { E->Text = Name; E->Value = FLinearColor(FMath::RoundToFloat(FMath::Clamp(V, -1.f, 5000.f)), 0.f, 0.f, 0.f); }
					Changed();
				}, -1.f, 5000.f, 1.f)]]
		];
	}
	// Copy / paste a material's edits (onto the material picked then, of this or another item) and the item's reset.
	Materials->AddSlot().AutoHeight().Padding(0.f, 4.f, 0.f, 1.f)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.f).Padding(0.f, 0.f, 1.f, 0.f)[Button(FText::FromString(TEXT("複製改動")), [this, ItemId, Material]
		{
			const FOKSceneObject* X = Game.IsValid() ? Game->Document->Find(ItemId) : nullptr;
			if (!X) return;
			MaterialClipboard = X->Item.MaterialEdits.FilterByPredicate([&](const FOKSceneMaterialEdit& E) { return E.Material == Material && E.Kind != EOKSceneMaterialEditKind::Renderer; });
			SetStatus(FString::Printf(TEXT("複製了 %d 筆材質改動"), MaterialClipboard.Num()));
		}, 0.f, 18.f, false, 10.f)]
		+ SHorizontalBox::Slot().FillWidth(1.f)[Button(FText::FromString(TEXT("貼上改動")), [this, ItemId, Material]
		{
			FOKSceneObject* X = Game.IsValid() ? Game->Document->Find(ItemId) : nullptr;
			if (!X || MaterialClipboard.IsEmpty()) return;
			X->Item.MaterialEdits.RemoveAll([&](const FOKSceneMaterialEdit& E) { return E.Material == Material && E.Kind != EOKSceneMaterialEditKind::Renderer; });
			for (FOKSceneMaterialEdit E : MaterialClipboard) { E.Material = Material; X->Item.MaterialEdits.Add(E); }
			Changed();
			RebuildFloat(TEXT("material"));
			SetStatus(FString::Printf(TEXT("貼上 %d 筆材質改動"), MaterialClipboard.Num()));
		}, 0.f, 18.f, false, 10.f)]
	];
	Materials->AddSlot().AutoHeight().Padding(0.f, 1.f)[Button(FText::FromString(TEXT("道具全部重置")), [this, ItemId]
	{
		if (FOKSceneObject* X = Game.IsValid() ? Game->Document->Find(ItemId) : nullptr) { X->Item.MaterialEdits.Reset(); Changed(); RebuildFloat(TEXT("material")); }
	}, 0.f, 18.f, false, 10.f)];
	// Exports (HS2Studio6 导出): the picked renderer's UV layout, the item as OBJ, every texture it shows.
	Materials->AddSlot().AutoHeight().Padding(0.f, 4.f, 0.f, 1.f)[OKStudioStyle::Label(FText::FromString(TEXT("匯出到 Saved/OKStudio/MaterialExport")), 9.f, TextHint())];
	Materials->AddSlot().AutoHeight()
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.f).Padding(0.f, 0.f, 1.f, 0.f)[Button(FText::FromString(TEXT("UV")), Cmd(TEXT("material.export"), TEXT("uv")), 0.f, 18.f, false, 10.f)]
		+ SHorizontalBox::Slot().FillWidth(1.f).Padding(0.f, 0.f, 1.f, 0.f)[Button(FText::FromString(TEXT("OBJ")), Cmd(TEXT("material.export"), TEXT("obj")), 0.f, 18.f, false, 10.f)]
		+ SHorizontalBox::Slot().FillWidth(1.f)[Button(FText::FromString(TEXT("貼圖")), Cmd(TEXT("material.export"), TEXT("textures")), 0.f, 18.f, false, 10.f)]
	];
	// The item's edit of this material's property (empty material: every material), or null.
	auto EditOf = [this, ItemId, Material](const FString& Property) -> FOKSceneMaterialEdit*
	{
		FOKSceneObject* X = Game.IsValid() ? Game->Document->Find(ItemId) : nullptr;
		if (!X) return nullptr;
		for (FOKSceneMaterialEdit& E : X->Item.MaterialEdits)
			if ((E.Kind == EOKSceneMaterialEditKind::Float || E.Kind == EOKSceneMaterialEditKind::Color) && E.Material == Material
				&& (E.Property == Property || TEXT("_") + E.Property == Property)) return &E;
		return nullptr;
	};
	auto Write = [this, ItemId, Material, EditOf](const FString& Property, EOKSceneMaterialEditKind Kind, const FLinearColor& Value)
	{
		FOKSceneObject* X = Game.IsValid() ? Game->Document->Find(ItemId) : nullptr;
		if (!X) return;
		FOKSceneMaterialEdit* E = EditOf(Property);
		if (!E) { E = &X->Item.MaterialEdits.AddDefaulted_GetRef(); E->Material = Material; E->Property = Property; }
		E->Kind = Kind;
		E->Value = Value;
		Changed();
	};
	auto Reset = [this, ItemId, Material](const FString& Property)
	{
		FOKSceneObject* X = Game.IsValid() ? Game->Document->Find(ItemId) : nullptr;
		if (!X) return;
		X->Item.MaterialEdits.RemoveAll([&](const FOKSceneMaterialEdit& E) { return E.Material == Material && (Property.IsEmpty() || E.Property == Property || TEXT("_") + E.Property == Property); });
		Changed();
		RebuildFloat(TEXT("material"));
	};
	auto Shows = [this](const FString& Property) { return MaterialFilter.IsEmpty() || Property.Contains(MaterialFilter); };
	TSharedRef<SVerticalBox> Properties = SNew(SVerticalBox);
	auto Line = [&Properties, this, EditOf, Reset](const FString& Property, const TSharedRef<SWidget>& Value)
	{
		Properties->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 1.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(0.42f).VAlign(VAlign_Center)
				[OKStudioStyle::Label(TAttribute<FText>::CreateLambda([EditOf, Property] { return FText::FromString((EditOf(Property) ? TEXT("● ") : TEXT("")) + Property); }), 10.f)]
			+ SHorizontalBox::Slot().FillWidth(0.58f)[SNew(SBox).HeightOverride(18.f)[Value]]
			+ SHorizontalBox::Slot().AutoWidth().Padding(2.f, 0.f, 0.f, 0.f)[Button(FText::FromString(TEXT("預設")), [Reset, Property] { Reset(Property); }, 34.f, 18.f, false, 9.f)]
		];
	};
	for (const TPair<FString, FLinearColor>& C : Colors)
	{
		if (!Shows(C.Key)) continue;
		const FString Property = C.Key;
		const FLinearColor Source = C.Value;
		Line(Property, Swatch(FString::Printf(TEXT("mat%d:%s:%s"), ItemId, *Material, *Property), Property,
			[EditOf, Property, Source] { const FOKSceneMaterialEdit* E = EditOf(Property); return E ? E->Value : Source; },
			[Write, Property](const FLinearColor& V) { Write(Property, EOKSceneMaterialEditKind::Color, V.A < 0.f ? FLinearColor(V.R, V.G, V.B, 1.f) : V); }));
	}
	for (const TPair<FString, float>& N : Numbers)
	{
		if (!Shows(N.Key)) continue;
		const FString Property = N.Key;
		const float Source = N.Value;
		Line(Property, Spin([EditOf, Property, Source] { const FOKSceneMaterialEdit* E = EditOf(Property); return E ? E->Value.R : Source; },
			[Write, Property](float V) { Write(Property, EOKSceneMaterialEditKind::Float, FLinearColor(V, 0.f, 0.f, 0.f)); }, -100.f, 100.f, 0.01f));
	}
	// Texture slots (HS2Studio6 纹理): load a PNG (a path, or a name under UserData/Studio/texture), clear it, back to the
	// source, export what it shows; scale and offset (_ST).
	if (!Textures.IsEmpty())
		Properties->AddSlot().AutoHeight().Padding(0.f, 2.f)[SNew(SBox).HeightOverride(18.f)[TextBox([this] { return MaterialTexturePath; }, [this](const FString& V) { MaterialTexturePath = V.TrimStartAndEnd().TrimQuotes(); }, TEXT("載入用的 PNG 路徑（或 UserData/Studio/texture 下的檔名）"))]];
	for (const TPair<FString, FString>& T : Textures)
	{
		if (!Shows(T.Key)) continue;
		const FString Property = T.Key;
		const FString SourceName = T.Value;
		auto TextureEdit = [FindEdit, Material, Property] { return FindEdit(EOKSceneMaterialEditKind::Texture, Material, Property); };
		Properties->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 1.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(0.42f)[OKStudioStyle::Label(TAttribute<FText>::CreateLambda([TextureEdit, Property] { return FText::FromString((TextureEdit() ? TEXT("● ") : TEXT("")) + Property); }), 10.f)]
			+ SHorizontalBox::Slot().FillWidth(0.58f)[OKStudioStyle::Label(TAttribute<FText>::CreateLambda([TextureEdit, SourceName]
				{
					const FOKSceneMaterialEdit* E = TextureEdit();
					return FText::FromString(E ? (E->Value.A < 0.f ? TEXT("（已清空）") : TEXT("（載入的圖）")) : SourceName.IsEmpty() ? TEXT("（無貼圖）") : SourceName);
				}), 9.f, TextHint())]
		];
		auto Load = [this, Put, Material, Property]
		{
			FString Path = MaterialTexturePath;
			if (!Path.IsEmpty() && FPaths::IsRelative(Path))
			{
				const FString Under = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("UserData/Studio/texture") / Path);
				Path = FPaths::FileExists(Under) ? Under : FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / Path);
			}
			TArray<uint8> Png;
			static const uint8 Signature[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
			if (Path.IsEmpty() || !FFileHelper::LoadFileToArray(Png, *Path) || Png.Num() < 8 || FMemory::Memcmp(Png.GetData(), Signature, 8)) { SetStatus(TEXT("載入貼圖：讀不到 PNG ") + Path); return; }
			if (FOKSceneMaterialEdit* E = Put(EOKSceneMaterialEditKind::Texture, Material, Property)) { E->Text = FBase64::Encode(Png); E->Value = FLinearColor::White; }
			Changed();
			SetStatus(FString::Printf(TEXT("%s 換成 %s"), *Property, *FPaths::GetCleanFilename(Path)));
		};
		auto Clear = [this, Put, Material, Property]
		{
			// A 1x1 white texture stands for "none" (what Unity binds for an empty white-default slot); alpha -1 marks it.
			TArray64<uint8> Png;
			FImageUtils::PNGCompressImageArray(1, 1, TArray<FColor>{ FColor::White }, Png);
			if (FOKSceneMaterialEdit* E = Put(EOKSceneMaterialEditKind::Texture, Material, Property)) { E->Text = FBase64::Encode(Png.GetData(), uint32(Png.Num())); E->Value = FLinearColor(1.f, 1.f, 1.f, -1.f); }
			Changed();
		};
		auto Back = [this, Drop, Material, Property] { Drop(EOKSceneMaterialEditKind::Texture, Material, Property); Drop(EOKSceneMaterialEditKind::TextureST, Material, Property); Changed(); };
		auto Export = [this, Material, Property, TextureEdit, SourceName, ItemId]
		{
			TArray<uint8> Png;
			if (const FOKSceneMaterialEdit* E = TextureEdit()) FBase64::Decode(E->Text, Png);
			else if (const FOKSceneObject* X = Game.IsValid() ? Game->Document->Find(ItemId) : nullptr)
			{
				FString Model, Prefab, Error;
				if (!SourceName.IsEmpty() && AOKSceneActor::ItemModel(X->Item, Model, Prefab, Error)) FOkModel::TexturePng(UKKCardWorkspace::LibraryRoot(), Model, SourceName, Png);
			}
			const FString Out = MaterialExportDir(ItemId) / FPaths::MakeValidFileName(Material + TEXT("_") + Property) + TEXT(".png");
			if (Png.IsEmpty() || !FFileHelper::SaveArrayToFile(Png, *Out)) { SetStatus(TEXT("匯出貼圖失敗：") + Property); return; }
			SetStatus(TEXT("匯出 ") + Out);
		};
		Properties->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 1.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f).Padding(0.f, 0.f, 1.f, 0.f)[Button(FText::FromString(TEXT("載入")), Load, 0.f, 16.f, false, 9.f)]
			+ SHorizontalBox::Slot().FillWidth(1.f).Padding(0.f, 0.f, 1.f, 0.f)[Button(FText::FromString(TEXT("清空")), Clear, 0.f, 16.f, false, 9.f)]
			+ SHorizontalBox::Slot().FillWidth(1.f).Padding(0.f, 0.f, 1.f, 0.f)[Button(FText::FromString(TEXT("預設")), Back, 0.f, 16.f, false, 9.f)]
			+ SHorizontalBox::Slot().FillWidth(1.f)[Button(FText::FromString(TEXT("匯出")), Export, 0.f, 16.f, false, 9.f)]
		];
		// Scale X / Y and offset X / Y, from the material's own _ST when it has one.
		FLinearColor SourceST(1.f, 1.f, 0.f, 0.f);
		for (const TPair<FString, FLinearColor>& C : Colors) if (C.Key == Property + TEXT("_ST")) SourceST = C.Value;
		TSharedRef<SHorizontalBox> ST = SNew(SHorizontalBox);
		static const TCHAR* STNames[] = { TEXT("縮X"), TEXT("縮Y"), TEXT("移X"), TEXT("移Y") };
		for (int32 C = 0; C < 4; ++C)
			ST->AddSlot().FillWidth(1.f).Padding(0.f, 0.f, 1.f, 0.f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 2.f, 0.f)[OKStudioStyle::Label(FText::FromString(STNames[C]), 9.f, TextHint())]
				+ SHorizontalBox::Slot().FillWidth(1.f)[SNew(SBox).HeightOverride(16.f)[Spin([FindEdit, Material, Property, SourceST, C] { const FOKSceneMaterialEdit* E = FindEdit(EOKSceneMaterialEditKind::TextureST, Material, Property); return (E ? E->Value : SourceST).Component(C); },
					[this, Put, FindEdit, Material, Property, SourceST, C](float V)
					{
						const FOKSceneMaterialEdit* Old = FindEdit(EOKSceneMaterialEditKind::TextureST, Material, Property);
						FLinearColor Now = Old ? Old->Value : SourceST;
						Now.Component(C) = V;
						if (FOKSceneMaterialEdit* E = Put(EOKSceneMaterialEditKind::TextureST, Material, Property)) E->Value = Now;
						Changed();
					}, -100.f, 100.f, 0.01f)]]
			];
		Properties->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 3.f)[ST];
	}
	const TSharedRef<SVerticalBox> Right = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 2.f)
			[TextBox([this] { return MaterialFilter; }, [this](const FString& V) { MaterialFilter = V.TrimStartAndEnd(); RebuildFloat(TEXT("material")); }, TEXT("篩選屬性"))]
		+ SVerticalBox::Slot().FillHeight(1.f)[Column(FString::Printf(TEXT("屬性（%d 色、%d 數值、%d 貼圖）"), Colors.Num(), Numbers.Num(), Textures.Num()), Properties)];
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().FillHeight(1.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(0.28f).Padding(0.f, 0.f, 3.f, 0.f)[Column(FString::Printf(TEXT("模型（%d）"), Parts), Models)]
			+ SHorizontalBox::Slot().FillWidth(0.22f).Padding(0.f, 0.f, 3.f, 0.f)[Column(TEXT("材質"), Materials)]
			+ SHorizontalBox::Slot().FillWidth(0.5f)[Right]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 3.f, 0.f, 0.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)[OKStudioStyle::Label(FText::FromString(TEXT("● 是改過的屬性；改動隨場景儲存。")), 9.f, TextHint())]
			+ SHorizontalBox::Slot().AutoWidth()[Button(FText::FromString(TEXT("此材質全部預設")), [Reset] { Reset(FString()); }, 0.f, 20.f, false, 10.f)]
		];
}

FString SOKStudioPanel::MaterialExportDir(int32 ItemId) const
{
	const FOKSceneObject* X = Game.IsValid() ? Game->Document->Find(ItemId) : nullptr;
	const FString Dir = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("OKStudio/MaterialExport")
		/ FPaths::MakeValidFileName(FString::Printf(TEXT("%d_%s"), ItemId, X ? *X->Name : TEXT(""))));
	IFileManager::Get().MakeDirectory(*Dir, true);
	return Dir;
}

bool SOKStudioPanel::ExportMaterial(const FString& What)
{
	FOKSceneObject* O = Game.IsValid() ? Game->Document->Find(Game->SelectedId) : nullptr;
	AKKPreviewActor* Shown = O && O->Kind == EOKSceneObjectKind::Item && Game->SceneActor ? Game->SceneActor->PreviewOf(O->Id) : nullptr;
	const int32 Parts = Shown ? Shown->Session.PartCount() : 0;
	if (Parts <= 0) { SetStatus(TEXT("匯出：先選取一個已載入的道具")); return false; }
	const FString Dir = MaterialExportDir(O->Id);
	auto PartName = [](const OkMeshView& M, int32 I) { return M.name && *M.name ? FPaths::MakeValidFileName(UTF8_TO_TCHAR(M.name)) : FString::Printf(TEXT("part%d"), I); };
	int32 Files = 0;
	if (What == TEXT("uv"))
	{
		// The picked renderer's triangles in UV space, black lines on white (1024 square, v up).
		OkMeshView M{};
		const int32 I = FMath::Clamp(MaterialPart, 0, Parts - 1);
		if (!Shown->Session.PartAt(I, M) || !M.uvs || !M.indices) { SetStatus(TEXT("匯出 UV：這個 renderer 沒有 UV")); return false; }
		const int32 Size = 1024;
		TArray<FColor> Pixels;
		Pixels.Init(FColor::White, Size * Size);
		auto At = [&](uint32 V) { return FIntPoint(FMath::Clamp(int32(M.uvs[V * 2] * Size), 0, Size - 1), FMath::Clamp(int32((1.f - M.uvs[V * 2 + 1]) * Size), 0, Size - 1)); };
		auto Line = [&](FIntPoint A, const FIntPoint B)
		{
			const int32 DX = FMath::Abs(B.X - A.X), DY = -FMath::Abs(B.Y - A.Y), SX = A.X < B.X ? 1 : -1, SY = A.Y < B.Y ? 1 : -1;
			for (int32 Err = DX + DY;;)
			{
				Pixels[A.Y * Size + A.X] = FColor::Black;
				if (A == B) break;
				const int32 E2 = 2 * Err;
				if (E2 >= DY) { Err += DY; A.X += SX; }
				if (E2 <= DX) { Err += DX; A.Y += SY; }
			}
		};
		for (int32 T = 0; T + 2 < M.n_idx; T += 3)
		{
			const uint32 A = M.indices[T], B = M.indices[T + 1], C = M.indices[T + 2];
			if (int32(FMath::Max3(A, B, C)) >= M.n_verts) continue;
			Line(At(A), At(B)); Line(At(B), At(C)); Line(At(C), At(A));
		}
		TArray64<uint8> Png;
		FImageUtils::PNGCompressImageArray(Size, Size, Pixels, Png);
		const FString Out = Dir / PartName(M, I) + TEXT("_uv.png");
		if (FFileHelper::SaveArrayToFile(Png, *Out)) ++Files;
	}
	else if (What == TEXT("obj"))
	{
		// Every renderer as one object (Unity's left-handed space mirrored on X, so the faces turn back).
		FString Obj = FString::Printf(TEXT("# OK Studio item %d %s\n"), O->Id, *O->Name);
		int32 Base = 1;
		for (int32 I = 0; I < Parts; ++I)
		{
			OkMeshView M{};
			if (!Shown->Session.PartAt(I, M) || !M.positions || !M.indices || M.n_verts <= 0) continue;
			Obj += FString::Printf(TEXT("o %s\n"), *PartName(M, I));
			for (int32 V = 0; V < M.n_verts; ++V) Obj += FString::Printf(TEXT("v %.6f %.6f %.6f\n"), -M.positions[V * 3], M.positions[V * 3 + 1], M.positions[V * 3 + 2]);
			if (M.uvs) for (int32 V = 0; V < M.n_verts; ++V) Obj += FString::Printf(TEXT("vt %.6f %.6f\n"), M.uvs[V * 2], M.uvs[V * 2 + 1]);
			for (int32 T = 0; T + 2 < M.n_idx; T += 3)
			{
				const int32 A = int32(M.indices[T]) + Base, B = int32(M.indices[T + 2]) + Base, C = int32(M.indices[T + 1]) + Base;
				Obj += M.uvs ? FString::Printf(TEXT("f %d/%d %d/%d %d/%d\n"), A, A, B, B, C, C) : FString::Printf(TEXT("f %d %d %d\n"), A, B, C);
			}
			Base += M.n_verts;
		}
		const FString Out = Dir / FPaths::MakeValidFileName(O->Name.IsEmpty() ? TEXT("item") : O->Name) + TEXT(".obj");
		if (FFileHelper::SaveStringToFile(Obj, *Out, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)) ++Files;
	}
	else if (What == TEXT("textures"))
	{
		// What each renderer shows (its main texture as built) and the material's named textures from the library.
		FString Model, Prefab, Error;
		const bool bModel = AOKSceneActor::ItemModel(O->Item, Model, Prefab, Error);
		TSet<FString> Named;
		for (int32 I = 0; I < Parts; ++I)
		{
			OkMeshView M{};
			if (!Shown->Session.PartAt(I, M)) continue;
			if (M.png && M.png_len > 0 && FFileHelper::SaveArrayToFile(TArrayView<const uint8>(M.png, M.png_len), *(Dir / PartName(M, I) + TEXT("_main.png")))) ++Files;
			TSharedPtr<FJsonObject> Root;
			const TSharedPtr<FJsonObject>* Mat = nullptr;
			const TArray<TSharedPtr<FJsonValue>>* Slots = nullptr;
			if (!bModel || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Shown->Session.MaterialMetadataJson(I)), Root) || !Root || !Root->TryGetObjectField(TEXT("material"), Mat) || !Mat || !(*Mat)->TryGetArrayField(TEXT("tex_slots"), Slots)) continue;
			for (const TSharedPtr<FJsonValue>& S : *Slots)
			{
				const TArray<TSharedPtr<FJsonValue>>* Pair = nullptr;
				if (!S->TryGetArray(Pair) || Pair->Num() < 2) continue;
				const FString Texture = (*Pair)[1]->AsString();
				TArray<uint8> Png;
				if (Texture.IsEmpty() || Named.Contains(Texture) || !FOkModel::TexturePng(UKKCardWorkspace::LibraryRoot(), Model, Texture, Png)) continue;
				Named.Add(Texture);
				if (FFileHelper::SaveArrayToFile(Png, *(Dir / FPaths::MakeValidFileName(Texture) + TEXT(".png")))) ++Files;
			}
		}
	}
	else { SetStatus(TEXT("匯出：參數是 uv、obj 或 textures")); return false; }
	SetStatus(FString::Printf(TEXT("匯出 %d 個檔案到 %s"), Files, *Dir));
	return Files > 0;
}

TSharedRef<SWidget> SOKStudioPanel::BuildAnimController()
{
	// HS2Studio6's animation controller: a window of its own for the selected character's motion, kept open when the
	// motion list closes. Everything reads the selection live, so it follows whichever character is picked.
	auto Character = [this]() -> FOKSceneObject* { FOKSceneObject* X = Obj(); return X && X->Kind == EOKSceneObjectKind::Character ? X : nullptr; };
	auto Playing = [Character]() -> const FOKStudioAnim*
	{
		const FOKSceneObject* X = Character();
		return X && X->Character.StudioAnimeNo >= 0 ? FOKStudioCatalog::Get().FindAnim(X->Character.StudioAnimeKind, X->Character.StudioAnimeGroup, X->Character.StudioAnimeCategory, X->Character.StudioAnimeNo) : nullptr;
	};
	// The next or previous converted motion of the same category (Studio's 動作變化).
	auto Step = [this, Playing](int32 Dir)
	{
		const FOKStudioCatalog& C = FOKStudioCatalog::Get();
		const FOKStudioAnim* Now = Playing();
		if (!Now) return;
		TArray<int32> Same;
		for (int32 I = 0; I < C.Anims.Num(); ++I)
			if (C.Anims[I].Kind == Now->Kind && C.Anims[I].Group == Now->Group && C.Anims[I].Category == Now->Category && !C.Anims[I].File.IsEmpty()) Same.Add(I);
		const int32 At = Same.IndexOfByPredicate([&C, Now](int32 I) { return &C.Anims[I] == Now; });
		if (At != INDEX_NONE && Same.Num() > 1) PickAnim(Same[(At + Dir + Same.Num()) % Same.Num()]);
	};
	auto Do = [this, Character](TFunction<void(FOKSceneCharacter&)> F) { if (FOKSceneObject* X = Character()) { F(X->Character); Changed(); } };
	auto Line = [](const FString& Name, const TSharedRef<SWidget>& W)
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(0.3f).VAlign(VAlign_Center)[OKStudioStyle::Label(FText::FromString(Name), 10.f)]
			+ SHorizontalBox::Slot().FillWidth(0.7f)[SNew(SBox).HeightOverride(18.f)[W]];
	};
	TSharedRef<SVerticalBox> Box = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)
			[OKStudioStyle::Label(TAttribute<FText>::CreateLambda([Character, Playing]
			{
				const FOKSceneObject* X = Character();
				const FOKStudioAnim* A = Playing();
				return FText::FromString(!X ? FString(TEXT("請選取一個角色")) : FString::Printf(TEXT("%s：%s"), *X->Name, A ? *A->Name : TEXT("（沒有動作）")));
			}), 11.f)]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 1.f)
			[Line(TEXT("速度"), Spin([Character] { const FOKSceneObject* X = Character(); return X ? X->Character.Speed : 0.f; }, [Do](float V) { Do([V](FOKSceneCharacter& C) { C.Speed = FMath::Max(V, 0.f); }); }, 0.f, 4.f, 0.05f))]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 1.f)
			[Line(TEXT("時間 秒"), Spin([Character] { const FOKSceneObject* X = Character(); return X ? X->Character.Time : 0.f; }, [Do](float V) { Do([V](FOKSceneCharacter& C) { C.Time = FMath::Max(V, 0.f); C.Speed = 0.f; }); }, 0.f, 600.f, 0.01f))]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 3.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f).Padding(0.f, 0.f, 1.f, 0.f)[Button(FText::FromString(TEXT("◀ 變化")), [Step] { Step(-1); }, 0.f, 20.f, false, 10.f)]
			+ SHorizontalBox::Slot().FillWidth(1.f).Padding(0.f, 0.f, 1.f, 0.f)[Button(FText::FromString(TEXT("變化 ▶")), [Step] { Step(1); }, 0.f, 20.f, false, 10.f)]
			// Studio's 強制ループ: a motion that ends starts over.
			+ SHorizontalBox::Slot().FillWidth(1.f)[Button(FText::FromString(TEXT("強制循環")), [Do] { Do([](FOKSceneCharacter& C) { C.bForceLoop = !C.bForceLoop; }); },
				0.f, 20.f, TAttribute<bool>::CreateLambda([Character] { const FOKSceneObject* X = Character(); return X && X->Character.bForceLoop; }), 10.f)]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 1.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f).Padding(0.f, 0.f, 1.f, 0.f)[Button(FText::FromString(TEXT("播放")), [this, Do]
				{ Do([this](FOKSceneCharacter& C) { if (C.Speed <= 0.f) C.Speed = AnimPausedSpeed > 0.f ? AnimPausedSpeed : 1.f; }); },
				0.f, 20.f, TAttribute<bool>::CreateLambda([Character] { const FOKSceneObject* X = Character(); return X && X->Character.Speed > 0.f; }), 10.f)]
			+ SHorizontalBox::Slot().FillWidth(1.f).Padding(0.f, 0.f, 1.f, 0.f)[Button(FText::FromString(TEXT("暫停")), [this, Do]
				{ Do([this](FOKSceneCharacter& C) { if (C.Speed > 0.f) { AnimPausedSpeed = C.Speed; C.Speed = 0.f; } }); }, 0.f, 20.f, false, 10.f)]
			+ SHorizontalBox::Slot().FillWidth(1.f).Padding(0.f, 0.f, 1.f, 0.f)[Button(FText::FromString(TEXT("從頭")), [Do] { Do([](FOKSceneCharacter& C) { C.Time = 0.f; }); }, 0.f, 20.f, false, 10.f)]
			+ SHorizontalBox::Slot().FillWidth(1.f)[Button(FText::FromString(TEXT("停止")), [this, Do] { Do([](FOKSceneCharacter& C) { C.StudioAnimeNo = -1; }); DeferRebuild(); }, 0.f, 20.f, false, 10.f)]
		];
	// Studio's 輔助 1／2 and 動作模式 sliders: the controls the selected character's H motion blends along.
	const FOKSceneObject* Selected = Character();
	const TArray<TPair<FString, float>> Controls = Selected && Game.IsValid() && Game->SceneActor ? Game->SceneActor->BlendControlsOf(Selected->Id) : TArray<TPair<FString, float>>();
	if (!Controls.IsEmpty())
	{
		Box->AddSlot().AutoHeight().Padding(0.f, 5.f, 0.f, 1.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)[OKStudioStyle::Label(FText::FromString(TEXT("動作參數")), 10.f, TextSoft())]
			+ SHorizontalBox::Slot().AutoWidth()[Button(FText::FromString(TEXT("預設")), [Do] { Do([](FOKSceneCharacter& C) { C.AnimFloats.Reset(); }); }, 44.f, 18.f, false, 10.f)]
		];
	}
	for (const TPair<FString, float>& Control : Controls)
	{
		const FString Name = Control.Key;
		const TCHAR* Label = Name == TEXT("option1") ? TEXT("輔助 1（身長）") : Name == TEXT("option2") ? TEXT("輔助 2（胸）") : Name == TEXT("pattern") ? TEXT("動作模式") : *Name;
		Box->AddSlot().AutoHeight().Padding(0.f, 1.f)
		[
			Line(Label, Spin([this, Character, Name]
				{
					const FOKSceneObject* X = Character();
					const FOKSceneAnimFloat* F = X ? X->Character.AnimFloats.FindByPredicate([&Name](const FOKSceneAnimFloat& A) { return A.Name == Name; }) : nullptr;
					if (F) return F->Value;
					// Not set by the scene: what the player has (the controller's own value).
					for (const TPair<FString, float>& P : X && Game.IsValid() && Game->SceneActor ? Game->SceneActor->BlendControlsOf(X->Id) : TArray<TPair<FString, float>>())
						if (P.Key == Name) return P.Value;
					return 0.f;
				},
				[Do, Name](float V)
				{
					Do([&Name, V](FOKSceneCharacter& C)
					{
						FOKSceneAnimFloat* F = C.AnimFloats.FindByPredicate([&Name](const FOKSceneAnimFloat& A) { return A.Name == Name; });
						if (!F) { F = &C.AnimFloats.AddDefaulted_GetRef(); F->Name = Name; }
						F->Value = FMath::Clamp(V, 0.f, 1.f);
					});
				}, 0.f, 1.f, 0.01f))
		];
	}
	return Box;
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
	TSharedRef<STextBlock> Note = OKStudioStyle::Label(FText::FromString(FString::Printf(TEXT("%d 個物件沒有顯示；它們仍在場景裡（工作區名稱後標「（未顯示）」），補齊資源後重新開啟就會出現。點一行選取該物件。"), Lines.Num())), 10.f, TextSoft());
	Note->SetAutoWrapText(true);
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()[Note]
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
	// Each character (HS2Studio6 资源检查): its card's kind, whether it shows, and the mods an original card names (what
	// to install before 重新讀取角色). An OK card carries its own parts.
	if (Game.IsValid())
	{
		const FString Project = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
		bool bHeading = false;
		for (const FOKSceneObject& O : Game->Document->Objects)
		{
			if (O.Kind != EOKSceneObjectKind::Character) continue;
			if (!bHeading) { Lines.Add(TEXT("── 角色 ──")); bHeading = true; }
			const FString Card = FPaths::IsRelative(O.Character.Card) ? Project / O.Character.Card : O.Character.Card;
			const bool bShown = Game->SceneActor && Game->SceneActor->PreviewOf(O.Id);
			FString Kind;
			TArray<FString> Mods;
			if (!FPaths::FileExists(Card)) Kind = TEXT("缺卡片");
			else if (!UKKCardWorkspace::PortableSourcePath(Card).IsEmpty()) Kind = TEXT("OK 卡（自帶資源）");
			else
			{
				Kind = TEXT("原作卡");
				FKKCharacterFile File;
				FString Error;
				if (UKKCardReader::LoadFromFile(Card, File, Error)) { for (const FKKResolveInfo& R : File.Resolves) if (!R.ModID.IsEmpty()) Mods.AddUnique(R.ModID); }
				else Kind += TEXT("，讀不出：") + Error;
			}
			Lines.Add(FString::Printf(TEXT("#%d %s：%s，%s%s"), O.Id, *O.Name, *Kind, bShown ? TEXT("已顯示") : TEXT("未顯示"),
				Mods.IsEmpty() ? TEXT("") : *FString::Printf(TEXT("，卡片指名 %d 個 mod"), Mods.Num())));
			Mods.Sort();
			for (const FString& M : Mods) Lines.Add(FString::Printf(TEXT("#%d 　　%s"), O.Id, *M));
		}
	}
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()[OKStudioStyle::Label(FText::FromString(FString::Printf(TEXT("%d 項缺件（點一行選取物件）"), Count)), 10.f, TextSoft())]
		+ SVerticalBox::Slot().FillHeight(1.f).Padding(0.f, 3.f)[TextList(Lines, [this, Lines](int32 I) { const int32 Id = LineId(Lines[I]); if (Id > 0 && Game.IsValid()) { Game->Select(Id); LocateSelection(); } }, 260.f)]
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f).Padding(0.f, 0.f, 3.f, 0.f)[Button(FText::FromString(TEXT("複製清單")), [Lines] { FPlatformApplicationMisc::ClipboardCopy(*FString::Join(Lines, TEXT("\n"))); }, 0.f, 20.f)]
			+ SHorizontalBox::Slot().FillWidth(1.f).Padding(0.f, 0.f, 3.f, 0.f)[Button(FText::FromString(TEXT("重新檢查")), [this] { DeferFloat(TEXT("resources")); }, 0.f, 20.f)]
			+ SHorizontalBox::Slot().FillWidth(1.f)[Button(FText::FromString(TEXT("重新讀取角色")), Cmd(TEXT("scene.reloadcharacters")), 0.f, 20.f)]
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
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f)[SNew(SHorizontalBox) + SHorizontalBox::Slot().FillWidth(1.f)[CheckBox(TEXT("含前景框"), [this] { return bShotFrame; }, [this](bool B) { bShotFrame = B; })]
			+ SHorizontalBox::Slot().AutoWidth()[HelpIcon(TEXT("含前景框"), TEXT("關掉時，拍攝的那一刻把前景框藏起來，截圖只有場景；畫面上的前景框不受影響。"))]]
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
	auto Setting = [this, &Body, &Show](const FString& Name, const FString& Help, const TSharedRef<SWidget>& Control, TFunction<void()> Default)
	{
		if (!Show(Name) && !Show(Help)) return;
		Body->AddSlot().AutoHeight().Padding(0.f, 1.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(0.38f).VAlign(VAlign_Center)[OKStudioStyle::Label(FText::FromString(Name), 10.f)]
			+ SHorizontalBox::Slot().FillWidth(0.45f)[SNew(SBox).HeightOverride(18.f)[Control]]
			+ SHorizontalBox::Slot().AutoWidth().Padding(3.f, 0.f)[HelpIcon(Name, Help)]
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
		Setting(TEXT("控制點大小"), TEXT("畫面上 FK 骨點、IK 點的大小倍率（也是點選範圍）"), Number(&S.PointScale, 0.3f, 4.f, 0.05f), [] { FOKStudioSettings::Get().PointScale = 1.f; });
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

// --- MMD Director & Settings (MMDD / KKVMDPlay / VNGE) ---

TSharedRef<SWidget> SOKStudioPanel::BuildMmdDirector()
{
	TSharedRef<SVerticalBox> Body = SNew(SVerticalBox);
	auto SectionHeader = [](const FString& Title)
	{
		return SNew(SBorder).BorderImage(Brush(FLinearColor(0.12f, 0.22f, 0.38f, 0.95f))).Padding(FMargin(6.f, 3.f))
			[OKStudioStyle::Label(FText::FromString(Title), 11.f, FLinearColor(0.95f, 0.98f, 1.f))];
	};

	// 1. MMD Transport & Timeline Sync
	Body->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 3.f)[SectionHeader(TEXT("一、MMD 播放與時間軸總控（動作＋鏡頭同步）"))];
	Body->AddSlot().AutoHeight().Padding(0.f, 2.f)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.4f).Padding(0.f, 0.f, 2.f, 0.f)
		[
			Button(TAttribute<FText>::CreateLambda([this]
			{
				return FText::FromString(Game.IsValid() && Game->bTimelinePlaying ? TEXT("■ 暫停 MMD") : TEXT("▶ 播放 MMD"));
			}), Cmd(TEXT("timeline.play")), 0.f, 24.f, TAttribute<bool>::CreateLambda([this] { return Game.IsValid() && Game->bTimelinePlaying; }), 11.f)
		]
		+ SHorizontalBox::Slot().FillWidth(0.8f).Padding(0.f, 0.f, 2.f, 0.f)[Button(FText::FromString(TEXT("|< 開頭")), Cmd(TEXT("timeline.first")), 0.f, 24.f, false, 10.f)]
		+ SHorizontalBox::Slot().FillWidth(0.7f).Padding(0.f, 0.f, 2.f, 0.f)[Button(FText::FromString(TEXT("-1 幀")), Cmd(TEXT("timeline.prev")), 0.f, 24.f, false, 10.f)]
		+ SHorizontalBox::Slot().FillWidth(0.7f).Padding(0.f, 0.f, 2.f, 0.f)[Button(FText::FromString(TEXT("+1 幀")), Cmd(TEXT("timeline.next")), 0.f, 24.f, false, 10.f)]
		+ SHorizontalBox::Slot().FillWidth(0.8f)[Button(FText::FromString(TEXT("結尾 >|")), Cmd(TEXT("timeline.last")), 0.f, 24.f, false, 10.f)]
	];

	Body->AddSlot().AutoHeight().Padding(2.f, 2.f)
	[
		OKStudioStyle::Label(TAttribute<FText>::CreateLambda([this]
		{
			if (!Game.IsValid() || !Game->Document) return FText::GetEmpty();
			const float T = Game->TimelineTime, D = FMath::Max(Game->Document->Duration, 0.01f);
			return FText::FromString(FString::Printf(TEXT("進度：%.2f / %.2f 秒　｜　幀：%d / %d (30 fps)"),
				T, D, FMath::RoundToInt(T * 30.f), FMath::RoundToInt(D * 30.f)));
		}), 10.f, FLinearColor(0.85f, 0.95f, 1.f))
	];

	Body->AddSlot().AutoHeight().Padding(2.f, 2.f)
	[
		SNew(SBox).HeightOverride(20.f)
		[
			SNew(SSlider)
			.Value_Lambda([this]
			{
				if (!Game.IsValid() || !Game->Document) return 0.f;
				return FMath::Clamp(Game->TimelineTime / FMath::Max(Game->Document->Duration, 0.01f), 0.f, 1.f);
			})
			.OnValueChanged_Lambda([this](float V)
			{
				if (!Game.IsValid() || !Game->Document) return;
				ScrubTo(V * FMath::Max(Game->Document->Duration, 0.01f));
			})
		]
	];

	Body->AddSlot().AutoHeight().Padding(0.f, 2.f)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[OKStudioStyle::Label(FText::FromString(TEXT("倍速")), 10.f)]
		+ SHorizontalBox::Slot().AutoWidth().Padding(4.f, 0.f)[SNew(SBox).WidthOverride(52.f).HeightOverride(18.f)[Spin([this] { return Game.IsValid() ? Game->TimelineSpeed : 1.f; }, [this](float V) { if (Game.IsValid()) Game->TimelineSpeed = FMath::Clamp(V, 0.05f, 4.f); }, 0.05f, 4.f, 0.05f)]]
		+ SHorizontalBox::Slot().AutoWidth().Padding(2.f, 0.f)[Button(FText::FromString(TEXT("0.5x")), [this] { if (Game.IsValid()) Game->TimelineSpeed = 0.5f; }, 34.f, 18.f, false, 9.f)]
		+ SHorizontalBox::Slot().AutoWidth().Padding(2.f, 0.f)[Button(FText::FromString(TEXT("1.0x")), [this] { if (Game.IsValid()) Game->TimelineSpeed = 1.0f; }, 34.f, 18.f, false, 9.f)]
		+ SHorizontalBox::Slot().AutoWidth().Padding(2.f, 0.f)[Button(FText::FromString(TEXT("2.0x")), [this] { if (Game.IsValid()) Game->TimelineSpeed = 2.0f; }, 34.f, 18.f, false, 9.f)]
		+ SHorizontalBox::Slot().FillWidth(1.f)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[OKStudioStyle::Label(FText::FromString(TEXT("總長(秒)")), 10.f)]
		+ SHorizontalBox::Slot().AutoWidth().Padding(4.f, 0.f, 0.f, 0.f)[SNew(SBox).WidthOverride(56.f).HeightOverride(18.f)[Spin([this] { return Game.IsValid() && Game->Document ? Game->Document->Duration : 10.f; }, [this](float V) { if (Game.IsValid() && Game->Document) { Game->Document->Duration = FMath::Max(V, 0.1f); Changed(); } }, 0.1f, 3600.f, 0.5f)]]
	];

	Body->AddSlot().AutoHeight().Padding(0.f, 2.f)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
		[
			OKStudioStyle::Label(TAttribute<FText>::CreateLambda([this]
			{
				if (!Game.IsValid() || !Game->Document || Game->Document->SoundFile.IsEmpty()) return FText::FromString(TEXT("音樂：（無音軌）"));
				return FText::FromString(FString::Printf(TEXT("音樂：%s"), *FPaths::GetCleanFilename(Game->Document->SoundFile)));
			}), 9.5f, FLinearColor(0.65f, 0.95f, 0.75f))
		]
		+ SHorizontalBox::Slot().AutoWidth().Padding(4.f, 0.f).VAlign(VAlign_Center)[CheckBox(TEXT("靜音"), [this] { return Game.IsValid() && Game->bMuteSound; }, [this](bool B) { if (Game.IsValid()) { Game->bMuteSound = B; Game->UpdateSceneAudio(); } })]
		+ SHorizontalBox::Slot().AutoWidth().Padding(4.f, 0.f, 2.f, 0.f).VAlign(VAlign_Center)[OKStudioStyle::Label(FText::FromString(TEXT("音量")), 9.5f)]
		+ SHorizontalBox::Slot().AutoWidth()[SNew(SBox).WidthOverride(44.f).HeightOverride(18.f)[Spin([this] { return Game.IsValid() ? Game->SoundVolume : 1.f; }, [this](float V) { if (Game.IsValid()) { Game->SoundVolume = FMath::Clamp(V, 0.f, 2.f); Game->StopSceneAudio(); Game->UpdateSceneAudio(); } }, 0.f, 2.f, 0.05f)]]
		+ SHorizontalBox::Slot().AutoWidth().Padding(4.f, 0.f, 2.f, 0.f).VAlign(VAlign_Center)[OKStudioStyle::Label(FText::FromString(TEXT("延遲(s)")), 9.5f)]
		+ SHorizontalBox::Slot().AutoWidth()[SNew(SBox).WidthOverride(48.f).HeightOverride(18.f)[Spin([this] { return Game.IsValid() && Game->Document ? Game->Document->SoundDelay : 0.f; }, [this](float V) { if (Game.IsValid() && Game->Document) { Game->Document->SoundDelay = V; Game->StopSceneAudio(); Game->UpdateSceneAudio(); Changed(); } }, -60.f, 60.f, 0.05f)]]
	];

	// 2. MMD System Camera
	Body->AddSlot().AutoHeight().Padding(0.f, 6.f, 0.f, 3.f)[SectionHeader(TEXT("二、MMD 系統鏡頭控制與補償（MMDD Camera）"))];
	Body->AddSlot().AutoHeight().Padding(2.f, 1.f)
	[
		OKStudioStyle::Label(TAttribute<FText>::CreateLambda([this]
		{
			if (!Game.IsValid() || !Game->Document) return FText::GetEmpty();
			const int32 ActiveId = Game->Document->ActiveCamera;
			const FOKSceneObject* ActiveCam = Game->Document->Find(ActiveId);
			const int32 MmdId = Game->FindMmdCameraId(false);
			const FOKSceneObject* MmdCam = Game->Document->Find(MmdId);
			const FOKSceneTrack* Track = MmdCam ? Game->Document->TrackOf(MmdId) : nullptr;
			const int32 Keys = Track ? Track->Keys.Num() : 0;
			if (ActiveCam && ActiveCam->Kind == EOKSceneObjectKind::Camera)
			{
				const FVector Pos = ActiveCam->Transform.GetLocation() + Game->MmdCameraOffset;
				const float Fov = FMath::Clamp(ActiveCam->Camera.Fov + Game->MmdCameraFovOffset, 5.f, 170.f);
				return FText::FromString(FString::Printf(TEXT("目前鏡頭：%s (#%d)%s｜關鍵幀：%d｜FOV：%.1f°｜XYZ：(%.0f, %.0f, %.0f)"),
					*ActiveCam->Name, ActiveCam->Id, Game->bLockActiveCamera ? TEXT(" [已鎖定]") : TEXT(""),
					Keys, Fov, Pos.X, Pos.Y, Pos.Z));
			}
			return FText::FromString(FString::Printf(TEXT("目前鏡頭：自由視角（場景鏡頭：%s，共 %d 個關鍵幀）"),
				MmdCam ? *MmdCam->Name : TEXT("無"), Keys));
		}), 10.f, FLinearColor(0.95f, 0.9f, 0.65f))
	];

	Body->AddSlot().AutoHeight().Padding(0.f, 2.f)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.2f).Padding(0.f, 0.f, 2.f, 0.f)
		[
			Button(FText::FromString(TEXT("啟用 MMD 系統鏡頭")), Cmd(TEXT("camera.mmd")), 0.f, 22.f,
				TAttribute<bool>::CreateLambda([this] { return Game.IsValid() && Game->Document && Game->Document->ActiveCamera >= 0 && Game->Document->ActiveCamera == Game->FindMmdCameraId(false); }), 10.f)
		]
		+ SHorizontalBox::Slot().FillWidth(1.f).Padding(0.f, 0.f, 2.f, 0.f)
		[
			Button(FText::FromString(TEXT("切換自由視角")), Cmd(TEXT("camera.free")), 0.f, 22.f,
				TAttribute<bool>::CreateLambda([this] { return Game.IsValid() && Game->Document && Game->Document->ActiveCamera < 0; }), 10.f)
		]
		+ SHorizontalBox::Slot().FillWidth(0.9f)
		[
			Button(FText::FromString(TEXT("選取鏡頭物件")), [this]
			{
				if (!Game.IsValid()) return;
				const int32 CamId = Game->FindMmdCameraId(false);
				if (CamId >= 0) { Game->Select(CamId); LocateSelection(); }
			}, 0.f, 22.f, false, 10.f)
		]
	];

	Body->AddSlot().AutoHeight().Padding(0.f, 2.f)
	[
		CheckBox(TEXT("鎖定 MMD 系統鏡頭（防止滑鼠拖曳／滾輪誤觸脫離鏡頭）"),
			[this] { return Game.IsValid() && Game->bLockActiveCamera; },
			[this](bool B) { if (Game.IsValid()) Game->bLockActiveCamera = B; })
	];

	Body->AddSlot().AutoHeight().Padding(0.f, 2.f)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[OKStudioStyle::Label(FText::FromString(TEXT("鏡頭位移 cm")), 10.f)]
		+ SHorizontalBox::Slot().AutoWidth().Padding(4.f, 0.f, 1.f, 0.f).VAlign(VAlign_Center)[OKStudioStyle::Label(FText::FromString(TEXT("X")), 10.f, FLinearColor(1.f, .4f, .4f))]
		+ SHorizontalBox::Slot().AutoWidth()[SNew(SBox).WidthOverride(52.f).HeightOverride(18.f)[Spin([this] { return Game.IsValid() ? float(Game->MmdCameraOffset.X) : 0.f; }, [this](float V) { if (Game.IsValid()) Game->MmdCameraOffset.X = V; }, -2000.f, 2000.f, 1.f)]]
		+ SHorizontalBox::Slot().AutoWidth().Padding(4.f, 0.f, 1.f, 0.f).VAlign(VAlign_Center)[OKStudioStyle::Label(FText::FromString(TEXT("Y")), 10.f, FLinearColor(.4f, 1.f, .4f))]
		+ SHorizontalBox::Slot().AutoWidth()[SNew(SBox).WidthOverride(52.f).HeightOverride(18.f)[Spin([this] { return Game.IsValid() ? float(Game->MmdCameraOffset.Y) : 0.f; }, [this](float V) { if (Game.IsValid()) Game->MmdCameraOffset.Y = V; }, -2000.f, 2000.f, 1.f)]]
		+ SHorizontalBox::Slot().AutoWidth().Padding(4.f, 0.f, 1.f, 0.f).VAlign(VAlign_Center)[OKStudioStyle::Label(FText::FromString(TEXT("Z(高)")), 10.f, FLinearColor(.5f, .7f, 1.f))]
		+ SHorizontalBox::Slot().AutoWidth()[SNew(SBox).WidthOverride(52.f).HeightOverride(18.f)[Spin([this] { return Game.IsValid() ? float(Game->MmdCameraOffset.Z) : 0.f; }, [this](float V) { if (Game.IsValid()) Game->MmdCameraOffset.Z = V; }, -2000.f, 2000.f, 1.f)]]
		+ SHorizontalBox::Slot().FillWidth(1.f)
		+ SHorizontalBox::Slot().AutoWidth()[Button(FText::FromString(TEXT("重設補償")), [this] { if (Game.IsValid()) { Game->MmdCameraOffset = FVector::ZeroVector; Game->MmdCameraYawOffset = 0.f; Game->MmdCameraFovOffset = 0.f; } }, 56.f, 18.f, false, 9.f)]
	];

	Body->AddSlot().AutoHeight().Padding(0.f, 2.f)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[OKStudioStyle::Label(FText::FromString(TEXT("水平旋轉 °")), 10.f)]
		+ SHorizontalBox::Slot().AutoWidth().Padding(4.f, 0.f)[SNew(SBox).WidthOverride(52.f).HeightOverride(18.f)[Spin([this] { return Game.IsValid() ? Game->MmdCameraYawOffset : 0.f; }, [this](float V) { if (Game.IsValid()) Game->MmdCameraYawOffset = V; }, -180.f, 180.f, 1.f)]]
		+ SHorizontalBox::Slot().AutoWidth().Padding(6.f, 0.f, 0.f, 0.f).VAlign(VAlign_Center)[OKStudioStyle::Label(FText::FromString(TEXT("FOV 補償 °")), 10.f)]
		+ SHorizontalBox::Slot().AutoWidth().Padding(4.f, 0.f)[SNew(SBox).WidthOverride(52.f).HeightOverride(18.f)[Spin([this] { return Game.IsValid() ? Game->MmdCameraFovOffset : 0.f; }, [this](float V) { if (Game.IsValid()) Game->MmdCameraFovOffset = V; }, -60.f, 60.f, 0.5f)]]
		+ SHorizontalBox::Slot().FillWidth(1.f)
		+ SHorizontalBox::Slot().AutoWidth().Padding(1.f, 0.f)[Button(FText::FromString(TEXT("高-5")), [this] { if (Game.IsValid()) Game->MmdCameraOffset.Z -= 5.f; }, 34.f, 18.f, false, 9.f)]
		+ SHorizontalBox::Slot().AutoWidth().Padding(1.f, 0.f)[Button(FText::FromString(TEXT("高+5")), [this] { if (Game.IsValid()) Game->MmdCameraOffset.Z += 5.f; }, 34.f, 18.f, false, 9.f)]
	];

	// 3. Characters & VMD Motions
	Body->AddSlot().AutoHeight().Padding(0.f, 6.f, 0.f, 3.f)[SectionHeader(TEXT("三、角色 VMD 動作、視線與表情設定"))];
	if (Game.IsValid() && Game->Document)
	{
		int32 CharCount = 0;
		auto LookText = [](EOKSceneLook L) { return L == EOKSceneLook::Fixed ? TEXT("固定") : L == EOKSceneLook::Camera ? TEXT("看鏡頭") : L == EOKSceneLook::Point ? TEXT("注視點") : TEXT("照動畫"); };
		for (const FOKSceneObject& O : Game->Document->Objects)
		{
			if (O.Kind != EOKSceneObjectKind::Character) continue;
			++CharCount;
			const int32 Id = O.Id;
			const FString AnimName = !O.Character.AnimFile.IsEmpty() ? FPaths::GetCleanFilename(O.Character.AnimFile)
				: (O.Character.StudioAnimeNo >= 0 ? FString::Printf(TEXT("Studio %d/%d/%d"), O.Character.StudioAnimeGroup, O.Character.StudioAnimeCategory, O.Character.StudioAnimeNo) : FString(TEXT("（無動作）")));
			Body->AddSlot().AutoHeight().Padding(0.f, 2.f)
			[
				SNew(SBorder).BorderImage(Brush(FLinearColor(0.08f, 0.09f, 0.12f, 0.9f))).Padding(4.f)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
						[
							OKStudioStyle::Label(FText::FromString(FString::Printf(TEXT("#%d %s　[%s]"), Id, *O.Name, *AnimName)), 10.f,
								Game->SelectedId == Id ? FLinearColor(1.f, 0.9f, 0.35f) : OKStudioStyle::Text())
						]
						+ SHorizontalBox::Slot().AutoWidth().Padding(2.f, 0.f)[Button(FText::FromString(TEXT("選取")), [this, Id] { if (Game.IsValid()) { Game->Select(Id); DeferFloat(TEXT("mmd")); } }, 36.f, 18.f, Game->SelectedId == Id, 9.f)]
						+ SHorizontalBox::Slot().AutoWidth().Padding(2.f, 0.f)[Button(FText::FromString(TEXT("換動作")), [this, Id] { if (Game.IsValid()) { Game->Select(Id); SetPage(EPage::Anim); DeferFloat(TEXT("mmd")); } }, 44.f, 18.f, false, 9.f)]
						+ SHorizontalBox::Slot().AutoWidth().Padding(2.f, 0.f)[Button(FText::FromString(TEXT("骨骼/表情")), [this, Id] { if (Game.IsValid()) { Game->Select(Id); SetPage(EPage::Bones); DeferFloat(TEXT("mmd")); } }, 54.f, 18.f, false, 9.f)]
						+ SHorizontalBox::Slot().AutoWidth()[Button(FText::FromString(TEXT("服裝")), [this, Id] { if (Game.IsValid()) { Game->Select(Id); SetPage(EPage::State); DeferFloat(TEXT("mmd")); } }, 36.f, 18.f, false, 9.f)]
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f, 0.f, 0.f)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[OKStudioStyle::Label(FText::FromString(TEXT("速度")), 9.f, TextSoft())]
						+ SHorizontalBox::Slot().AutoWidth().Padding(3.f, 0.f)[SNew(SBox).WidthOverride(44.f).HeightOverride(16.f)[Spin([this, Id] { const FOKSceneObject* X = Game.IsValid() ? Game->Document->Find(Id) : nullptr; return X ? X->Character.Speed : 1.f; }, [this, Id](float V) { if (FOKSceneObject* X = Game.IsValid() ? Game->Document->Find(Id) : nullptr) { X->Character.Speed = FMath::Clamp(V, 0.f, 4.f); Changed(); } }, 0.f, 4.f, 0.05f)]]
						+ SHorizontalBox::Slot().AutoWidth().Padding(4.f, 0.f, 0.f, 0.f).VAlign(VAlign_Center)[OKStudioStyle::Label(FText::FromString(TEXT("偏移(s)")), 9.f, TextSoft())]
						+ SHorizontalBox::Slot().AutoWidth().Padding(3.f, 0.f)[SNew(SBox).WidthOverride(62.f).HeightOverride(16.f)[Spin([this, Id] { const FOKSceneObject* X = Game.IsValid() ? Game->Document->Find(Id) : nullptr; return X ? X->Character.Time : 0.f; }, [this, Id](float V) { if (FOKSceneObject* X = Game.IsValid() ? Game->Document->Find(Id) : nullptr) { X->Character.Time = V; Changed(); } }, -120.f, 120.f, 0.05f)]]
						+ SHorizontalBox::Slot().FillWidth(1.f)
						+ SHorizontalBox::Slot().AutoWidth().Padding(2.f, 0.f)
						[
							Button(TAttribute<FText>::CreateLambda([this, Id, LookText]
							{
								const FOKSceneObject* X = Game.IsValid() ? Game->Document->Find(Id) : nullptr;
								return FText::FromString(FString(TEXT("眼：")) + (X ? LookText(X->Character.EyesLook) : TEXT("")));
							}), [this, Id]
							{
								if (FOKSceneObject* X = Game.IsValid() ? Game->Document->Find(Id) : nullptr)
								{
									X->Character.EyesLook = EOKSceneLook((uint8(X->Character.EyesLook) + 1) % 3);
									Changed();
								}
							}, 72.f, 16.f, false, 9.f)
						]
						+ SHorizontalBox::Slot().AutoWidth()
						[
							Button(TAttribute<FText>::CreateLambda([this, Id, LookText]
							{
								const FOKSceneObject* X = Game.IsValid() ? Game->Document->Find(Id) : nullptr;
								return FText::FromString(FString(TEXT("頸：")) + (X ? LookText(X->Character.NeckLook) : TEXT("")));
							}), [this, Id]
							{
								if (FOKSceneObject* X = Game.IsValid() ? Game->Document->Find(Id) : nullptr)
								{
									X->Character.NeckLook = EOKSceneLook((uint8(X->Character.NeckLook) + 1) % 3);
									Changed();
								}
							}, 72.f, 16.f, false, 9.f)
						]
					]
				]
			];
		}
		if (CharCount == 0)
			Body->AddSlot().AutoHeight()[OKStudioStyle::Label(FText::FromString(TEXT("場景中尚無角色")), 10.f, TextSoft())];
	}

	// 4. Keyframe Tracks & Quick Tools
	Body->AddSlot().AutoHeight().Padding(0.f, 6.f, 0.f, 3.f)[SectionHeader(TEXT("四、場景關鍵幀軌道與工具"))];
	if (Game.IsValid() && Game->Document)
	{
		for (const FOKSceneTrack& T : Game->Document->Tracks)
		{
			const FOKSceneObject* ObjPtr = Game->Document->Find(T.ObjectId);
			const FString ObjName = T.ObjectId == UOKSceneDocument::SceneTrackId ? FString(TEXT("場景後製軌道")) : (ObjPtr ? ObjPtr->Name : FString::Printf(TEXT("物件 #%d"), T.ObjectId));
			const float LastTime = T.Keys.Num() ? T.Keys.Last().Time : 0.f;
			Body->AddSlot().AutoHeight().Padding(2.f, 1.f)
			[
				OKStudioStyle::Label(FText::FromString(FString::Printf(TEXT("• %s (#%d)：%d 個關鍵幀（0.0s ~ %.2fs）"), *ObjName, T.ObjectId, T.Keys.Num(), LastTime)), 9.5f, TextSoft())
			];
		}
	}
	Body->AddSlot().AutoHeight().Padding(0.f, 4.f, 0.f, 2.f)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.f).Padding(0.f, 0.f, 2.f, 0.f)[Button(FText::FromString(TEXT("開啟完整時間軸視窗")), Cmd(TEXT("toggle.timeline")), 0.f, 22.f, TAttribute<bool>::CreateLambda([this] { return IsFloatOpen(TEXT("timeline")); }), 10.f)]
		+ SHorizontalBox::Slot().FillWidth(1.f).Padding(0.f, 0.f, 2.f, 0.f)[Button(FText::FromString(TEXT("輸出影片 / MP4…")), Cmd(TEXT("timeline.export")), 0.f, 22.f, TAttribute<bool>::CreateLambda([this] { return IsFloatOpen(TEXT("export")); }), 10.f)]
		+ SHorizontalBox::Slot().FillWidth(0.8f)[Button(FText::FromString(TEXT("儲存場景")), Cmd(TEXT("scene.save")), 0.f, 22.f, false, 10.f)]
	];

	return SNew(SScrollBox).ScrollBarThickness(FVector2D(4.f, 4.f))
		+ SScrollBox::Slot().Padding(4.f)[Body];
}
