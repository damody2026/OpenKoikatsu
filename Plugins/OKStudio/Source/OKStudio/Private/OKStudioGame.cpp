#include "OKStudioGame.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
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
#include "OKSceneCard.h"
#include "KKAnimatedPng.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "Components/PoseableMeshComponent.h"
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

namespace
{
	TWeakObjectPtr<UOKStudioGame> GRunning;

	const TCHAR* KindTag(EOKSceneObjectKind Kind)
	{
		switch (Kind)
		{
		case EOKSceneObjectKind::Character: return TEXT("角色");
		case EOKSceneObjectKind::Item: return TEXT("道具");
		case EOKSceneObjectKind::Light: return TEXT("燈光");
		case EOKSceneObjectKind::Camera: return TEXT("相機");
		case EOKSceneObjectKind::Effect: return TEXT("特效");
		default: return TEXT("資料夾");
		}
	}

	const TCHAR* ClothesPart(int32 I)
	{
		static const TCHAR* Names[] = { TEXT("上衣"), TEXT("下著"), TEXT("胸罩"), TEXT("內褲"), TEXT("手套"), TEXT("褲襪"), TEXT("襪子"), TEXT("室內鞋"), TEXT("室外鞋") };
		return Names[FMath::Clamp(I, 0, 8)];
	}

	const TCHAR* ClothesStateName(uint8 S) { return S == 0 ? TEXT("穿") : (S == 1 ? TEXT("半脫") : TEXT("脫")); }
}

/** The scene area: drag to orbit (left / right button), middle button to pan, wheel to zoom. */
class SOKStudioView : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SOKStudioView) {}
		SLATE_ARGUMENT(TWeakObjectPtr<UOKStudioGame>, Game)
	SLATE_END_ARGS()

	void Construct(const FArguments& Args)
	{
		Game = Args._Game;
		ChildSlot[SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("NoBorder"))];
	}

	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event) override
	{
		Button = Event.GetEffectingButton();
		// Left button on a guide handle drags the selection; anywhere else orbits the camera.
		DragAxis = Button == EKeys::LeftMouseButton && Game.IsValid() ? Game->HitGuide(Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition())) : -1;
		return FReply::Handled().CaptureMouse(SharedThis(this)).SetUserFocus(SharedThis(this), EFocusCause::Mouse);
	}
	virtual FReply OnMouseButtonUp(const FGeometry&, const FPointerEvent&) override
	{
		Button = FKey();
		DragAxis = -1;
		return FReply::Handled().ReleaseMouseCapture();
	}
	virtual FReply OnMouseMove(const FGeometry&, const FPointerEvent& Event) override
	{
		if (!HasMouseCapture() || !Game.IsValid()) return FReply::Unhandled();
		const FVector2D D = Event.GetCursorDelta();
		if (DragAxis >= 0) Game->ApplyGuideDrag(DragAxis, D);
		else if (Button == EKeys::MiddleMouseButton) Game->Pan(-D.X, D.Y);
		else Game->Orbit(D.X * 0.3f, -D.Y * 0.3f);
		return FReply::Handled();
	}
	virtual FReply OnKeyDown(const FGeometry&, const FKeyEvent& Event) override
	{
		if (!Game.IsValid()) return FReply::Unhandled();
		const FKey Key = Event.GetKey();
		if (Key == EKeys::W) { Game->GuideMode = 0; return FReply::Handled(); }
		if (Key == EKeys::E) { Game->GuideMode = 1; return FReply::Handled(); }
		if (Key == EKeys::R) { Game->GuideMode = 2; return FReply::Handled(); }
		return FReply::Unhandled();
	}
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& Culling, FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const override
	{
		Layer = SCompoundWidget::OnPaint(Args, Geometry, Culling, Out, Layer, Style, bParentEnabled);
		TArray<UOKStudioGame::FGuideHandle> Handles;
		FVector2D Center;
		double CmPerPixel = 0.;
		if (!Game.IsValid() || !Game->GuideHandles(Handles, Center, CmPerPixel)) return Layer;
		static const FLinearColor Colors[] = { FLinearColor(1.f, .22f, .2f), FLinearColor(.3f, 1.f, .3f), FLinearColor(.3f, .55f, 1.f), FLinearColor(.9f, .9f, .9f) };
		for (const UOKStudioGame::FGuideHandle& H : Handles)
		{
			TArray<FVector2f> P;
			for (const FVector2D& V : H.Points) P.Add(FVector2f(V));
			if (H.bClosed && P.Num() > 2) { const FVector2f First = P[0]; P.Add(First); }
			const bool bActive = H.Axis == DragAxis;
			FSlateDrawElement::MakeLines(Out, Layer + 1, Geometry.ToPaintGeometry(), P, ESlateDrawEffect::None, bActive ? FLinearColor::Yellow : Colors[FMath::Clamp(H.Axis, 0, 3)], true, bActive ? 4.f : 2.5f);
		}
		return Layer + 1;
	}
	virtual FReply OnMouseWheel(const FGeometry&, const FPointerEvent& Event) override
	{
		if (Game.IsValid()) Game->Zoom(Event.GetWheelDelta());
		return FReply::Handled();
	}

private:
	TWeakObjectPtr<UOKStudioGame> Game;
	FKey Button;
	int32 DragAxis = -1;
};

/** Outliner, object properties and scene buttons. */
class SOKStudioPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SOKStudioPanel) {}
		SLATE_ARGUMENT(TWeakObjectPtr<UOKStudioGame>, Game)
	SLATE_END_ARGS()

	struct FRow { int32 Id = -1; int32 Depth = 0; };

	void Construct(const FArguments& Args)
	{
		Game = Args._Game;
		for (const FString& Card : UOKStudioGame::AvailableCards()) Cards.Add(MakeShared<FString>(Card));
		auto Button = [this](const FString& Label, TFunction<void()> Do)
		{
			return SNew(SButton).Text(FText::FromString(Label)).OnClicked_Lambda([Do] { Do(); return FReply::Handled(); });
		};
		TSharedRef<SWrapBox> Buttons = SNew(SWrapBox).UseAllottedSize(true);
		auto AddButton = [&](const FString& Label, TFunction<void()> Do) { Buttons->AddSlot().Padding(1.f)[Button(Label, Do)]; };
		AddButton(TEXT("新場景"), [this] { if (Game.IsValid()) { Game->Document->Objects.Reset(); Game->Document->NextId = 1; Game->Document->ActiveCamera = -1; Game->File.Reset(); Game->Select(-1); Game->Add(EOKSceneObjectKind::Light); Game->Document->NotifyChanged(); } });
		AddButton(TEXT("場景檔"), [this] { ShowFiles(!bFilesShown); });
		AddButton(TEXT("+資料夾"), [this] { if (Game.IsValid()) Game->Add(EOKSceneObjectKind::Folder); });
		AddButton(TEXT("+角色"), [this] { if (Game.IsValid()) { const int32 Id = Game->Add(EOKSceneObjectKind::Character); if (FOKSceneObject* O = Game->Document->Find(Id)) { O->Character.Card = Cards.Num() ? *Cards[0] : FString(); O->Name = FPaths::GetBaseFilename(O->Character.Card); Game->Document->NotifyChanged(); } } });
		AddButton(TEXT("+道具"), [this] { if (Game.IsValid()) Game->Add(EOKSceneObjectKind::Item); });
		AddButton(TEXT("+燈光"), [this] { if (Game.IsValid()) Game->Add(EOKSceneObjectKind::Light); });
		AddButton(TEXT("+相機"), [this] { if (Game.IsValid()) Game->Add(EOKSceneObjectKind::Camera); });
		AddButton(TEXT("+特效"), [this]
		{
			if (!Game.IsValid() || FOKStudioCatalog::Get().Effects.IsEmpty()) return;
			const int32 Id = Game->Add(EOKSceneObjectKind::Effect);
			if (FOKSceneObject* O = Game->Document->Find(Id)) { const FOKStudioEffect& E = FOKStudioCatalog::Get().Effects[0]; O->Effect.Effect = E.Id; O->Name = E.Name; Game->Document->NotifyChanged(); }
		});
		AddButton(TEXT("複製"), [this] { if (Game.IsValid()) { const int32 Id = Game->Document->Duplicate(Game->SelectedId); if (Id >= 0) Game->Select(Id); } });
		AddButton(TEXT("移到根層"), [this] { if (Game.IsValid()) Game->Document->Reparent(Game->SelectedId, -1); });
		AddButton(TEXT("刪除"), [this] { if (Game.IsValid()) { Game->Document->RemoveObject(Game->SelectedId); Game->Select(-1); } });
		AddButton(TEXT("移動(W)"), [this] { if (Game.IsValid()) Game->GuideMode = 0; });
		AddButton(TEXT("旋轉(E)"), [this] { if (Game.IsValid()) Game->GuideMode = 1; });
		AddButton(TEXT("縮放(R)"), [this] { if (Game.IsValid()) Game->GuideMode = 2; });
		AddButton(TEXT("離開"), [this] { if (Game.IsValid()) Game->RequestStop(); });
		AddButton(TEXT("用此相機"), [this] { if (!Game.IsValid()) return; const FOKSceneObject* O = Game->Document->Find(Game->SelectedId); Game->Document->ActiveCamera = O && O->Kind == EOKSceneObjectKind::Camera ? O->Id : -1; });

		ChildSlot
		[
			SNew(SOverlay)
			+ SOverlay::Slot()[SNew(SOKStudioView).Game(Game)]
			+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Fill).Padding(8.f)
			[
				SNew(SBox).WidthOverride(300.f)
				[
					SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(FLinearColor(0.02f, 0.03f, 0.05f, 0.82f)).Padding(6.f)
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Text(FText::FromString(TEXT("OK Studio"))).ColorAndOpacity(FLinearColor::White)]
						+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 4.f)[Buttons]
						+ SVerticalBox::Slot().AutoHeight()[MakeFilesBox()]
						+ SVerticalBox::Slot().FillHeight(1.f)
						[
							SAssignNew(List, SListView<TSharedPtr<FRow>>)
							.ListItemsSource(&Rows)
							.SelectionMode(ESelectionMode::Single)
							.OnGenerateRow(this, &SOKStudioPanel::MakeRow)
							.OnSelectionChanged_Lambda([this](TSharedPtr<FRow> Row, ESelectInfo::Type Info) { if (Row && Info != ESelectInfo::Direct && Game.IsValid()) Game->Select(Row->Id); })
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 4.f)[MakeTimeline()]
						+ SVerticalBox::Slot().AutoHeight()[SAssignNew(Status, STextBlock).ColorAndOpacity(FLinearColor(0.8f, 0.9f, 1.f)).AutoWrapText(true)]
					]
				]
			]
			+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Fill).Padding(8.f)
			[
				SNew(SBox).WidthOverride(330.f)
				[
					SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(FLinearColor(0.02f, 0.03f, 0.05f, 0.82f)).Padding(6.f)
					[
						SNew(SScrollBox) + SScrollBox::Slot()[SAssignNew(Props, SVerticalBox)]
					]
				]
			]
		];
		RefreshRows();
	}

	void SetStatus(const FString& Text) { if (Status) Status->SetText(FText::FromString(Text)); }

	/** Timeline: time slider, keys of the selected object, playback, screenshot. */
	TSharedRef<SWidget> MakeTimeline()
	{
		auto Btn = [this](const FString& Label, TFunction<void()> Do) { return SNew(SButton).Text(FText::FromString(Label)).OnClicked_Lambda([Do] { Do(); return FReply::Handled(); }); };
		return SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(STextBlock).ColorAndOpacity(FLinearColor(0.6f, 0.85f, 1.f)).Text_Lambda([this]
				{
					if (!Game.IsValid()) return FText::GetEmpty();
					const FOKSceneTrack* T = Game->Document->TrackOf(Game->SelectedId);
					return FText::FromString(FString::Printf(TEXT("時間軸 %.2f / %.1f 秒（選取物件 %d 個關鍵幀，共 %d 條軌）"), Game->TimelineTime, Game->Document->Duration, T ? T->Keys.Num() : 0, Game->Document->Tracks.Num()));
				})
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SSlider).MinValue(0.f).MaxValue(1.f)
				.Value_Lambda([this] { return Game.IsValid() ? Game->TimelineTime / FMath::Max(Game->Document->Duration, 0.01f) : 0.f; })
				.OnValueChanged_Lambda([this](float V) { if (Game.IsValid()) Game->SetTimelineTime(V * Game->Document->Duration); })
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SWrapBox).UseAllottedSize(true)
				+ SWrapBox::Slot().Padding(1.f)[Btn(TEXT("設關鍵幀"), [this] { if (Game.IsValid() && Game->Document->Find(Game->SelectedId)) Game->Document->SetKey(Game->SelectedId, Game->TimelineTime); })]
				+ SWrapBox::Slot().Padding(1.f)[Btn(TEXT("刪關鍵幀"), [this] { if (Game.IsValid()) Game->Document->RemoveKey(Game->SelectedId, Game->TimelineTime); })]
				+ SWrapBox::Slot().Padding(1.f)
				[
					SNew(SButton)
					.Text_Lambda([this]
					{
						const FOKSceneTrack* T = Game.IsValid() ? Game->Document->TrackOf(Game->SelectedId) : nullptr;
						return FText::FromString(T ? (T->bSmoothPath ? TEXT("路徑：平滑") : TEXT("路徑：直線")) : TEXT("路徑（沒有關鍵幀）"));
					})
					.OnClicked_Lambda([this]
					{
						if (FOKSceneTrack* T = Game.IsValid() ? Game->Document->TrackOf(Game->SelectedId) : nullptr) { T->bSmoothPath = !T->bSmoothPath; Game->Document->NotifyChanged(); }
						return FReply::Handled();
					})
				]
				+ SWrapBox::Slot().Padding(1.f)
				[
					SNew(SButton)
					.Text_Lambda([this]
					{
						const FOKSceneKey* K = Game.IsValid() ? Game->Document->KeyAt(Game->SelectedId, Game->TimelineTime) : nullptr;
						static const TCHAR* Names[] = { TEXT("線性"), TEXT("緩入緩出"), TEXT("緩入"), TEXT("緩出"), TEXT("跳格") };
						return FText::FromString(K ? FString(TEXT("曲線：")) + Names[uint8(K->Ease) % 5] : FString(TEXT("曲線（此時間沒有關鍵幀）")));
					})
					.OnClicked_Lambda([this]
					{
						if (FOKSceneKey* K = Game.IsValid() ? Game->Document->KeyAt(Game->SelectedId, Game->TimelineTime) : nullptr) { K->Ease = EOKSceneEase((uint8(K->Ease) + 1) % 5); Game->Document->NotifyChanged(); }
						return FReply::Handled();
					})
				]
				+ SWrapBox::Slot().Padding(1.f)[Btn(TEXT("播放／停止"), [this] { if (Game.IsValid()) Game->bTimelinePlaying = !Game->bTimelinePlaying; })]
				+ SWrapBox::Slot().Padding(1.f)[Btn(TEXT("長度 +5 秒"), [this] { if (Game.IsValid()) { Game->Document->Duration += 5.f; Game->Document->NotifyChanged(); } })]
				+ SWrapBox::Slot().Padding(1.f)[Btn(TEXT("截圖 1920×1080"), [this] { if (Game.IsValid()) SetStatus(TEXT("截圖：") + Game->Screenshot(1920, 1080)); })]
				+ SWrapBox::Slot().Padding(1.f)[Btn(TEXT("截圖 3840×2160"), [this] { if (Game.IsValid()) SetStatus(TEXT("截圖：") + Game->Screenshot(3840, 2160)); })]
				+ SWrapBox::Slot().Padding(1.f)[Btn(TEXT("透明背景截圖 1920×1080"), [this] { if (Game.IsValid()) SetStatus(TEXT("截圖：") + Game->Screenshot(1920, 1080, true)); })]
				+ SWrapBox::Slot().Padding(1.f)[Btn(TEXT("輸出影片幀 30fps"), [this] { if (Game.IsValid() && !Game->IsExporting()) SetStatus(FString::Printf(TEXT("輸出 %d 幀到 %s"), FMath::Max(1, FMath::CeilToInt(Game->Document->Duration * 30.f)), *Game->ExportFrames(30.f, 1920, 1080))); })]
				+ SWrapBox::Slot().Padding(1.f)[Btn(TEXT("角色時間軸轉 .okanim"), [this]
				{
					if (!Game.IsValid() || Game->IsExporting()) return;
					FString Error;
					const FString File = Game->ExportOkAnim(Game->SelectedId, 30.f, Error);
					SetStatus(File.IsEmpty() ? TEXT("轉出失敗（先選一個角色）：") + Error : TEXT("OK 動畫：") + File);
				})]
				+ SWrapBox::Slot().Padding(1.f)[Btn(TEXT("輸出 mp4 1920×1080 30fps"), [this]
				{
					if (!Game.IsValid() || Game->IsExporting()) return;
					if (!FPaths::FileExists(UOKStudioGame::FfmpegPath())) { SetStatus(TEXT("找不到 Tools/ffmpeg/ffmpeg.exe（第三方工具不進版控，請放一份 LGPL 版）")); return; }
					SetStatus(TEXT("輸出 mp4：") + Game->ExportFrames(30.f, 1920, 1080, true) + TEXT(".mp4"));
				})]
			];
	}

	/** Scene file box: a name to save under and the scenes in UserData/Studio/scene (click to load). */
	TSharedRef<SWidget> MakeFilesBox()
	{
		return SAssignNew(FilesBox, SVerticalBox).Visibility(EVisibility::Collapsed)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.f)[SAssignNew(FileName, SEditableTextBox).HintText(FText::FromString(TEXT("場景名稱")))]
				+ SHorizontalBox::Slot().AutoWidth().Padding(2.f, 0.f)[SNew(SButton).Text(FText::FromString(TEXT("存檔"))).OnClicked_Lambda([this] { SaveNamed(FileName->GetText().ToString()); return FReply::Handled(); })]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.f)[SAssignNew(ImportPath, SEditableTextBox).HintText(FText::FromString(TEXT("KStudio 場景 PNG 路徑")))]
				+ SHorizontalBox::Slot().AutoWidth().Padding(2.f, 0.f)[SNew(SButton).Text(FText::FromString(TEXT("匯入"))).OnClicked_Lambda([this] { ImportKStudio(ImportPath->GetText().ToString().TrimQuotes().TrimStartAndEnd()); return FReply::Handled(); })]
			]
			+ SVerticalBox::Slot().AutoHeight().MaxHeight(180.f)
			[
				SAssignNew(FileList, SListView<TSharedPtr<FString>>)
				.ListItemsSource(&Files)
				.SelectionMode(ESelectionMode::Single)
				.OnGenerateRow_Lambda([this](TSharedPtr<FString> F, const TSharedRef<STableViewBase>& Owner)
				{
					const FString* Tag = FileKinds.Find(*F);
					return SNew(STableRow<TSharedPtr<FString>>, Owner)[SNew(STextBlock).ColorAndOpacity(FLinearColor::White).Text(FText::FromString((Tag ? *Tag : FString()) + FPaths::GetBaseFilename(*F)))];
				})
				.OnMouseButtonClick_Lambda([this](TSharedPtr<FString> F) { if (F) LoadFile(*F); })
			];
	}

	void ShowFiles(bool bShow)
	{
		bFilesShown = bShow;
		if (!FilesBox) return;
		FilesBox->SetVisibility(bShow ? EVisibility::Visible : EVisibility::Collapsed);
		if (!bShow) return;
		Files.Reset();
		FileKinds.Reset();
		for (const FString& F : UOKStudioGame::SceneFiles())
		{
			Files.Add(MakeShared<FString>(F));
			const FOKSceneCard::EKind K = FOKSceneCard::Kind(F);
			FileKinds.Add(F, K == FOKSceneCard::EKind::KStudio ? TEXT("[KStudio] ") : K == FOKSceneCard::EKind::Json ? TEXT("[舊檔] ") : TEXT(""));
		}
		if (FileList) FileList->RequestListRefresh();
		if (FileName && Game.IsValid()) FileName->SetText(FText::FromString(Game->File.IsEmpty() ? FDateTime::Now().ToString(TEXT("scene_%Y%m%d_%H%M%S")) : FPaths::GetBaseFilename(Game->File)));
	}

public:
	bool SaveNamed(const FString& Name)
	{
		if (!Game.IsValid()) return false;
		const FString Clean = FPaths::MakeValidFileName(Name.TrimStartAndEnd());
		if (Clean.IsEmpty()) { SetStatus(TEXT("請輸入場景名稱")); return false; }
		const FString Path = UOKStudioGame::SceneDirectory() / Clean + TEXT(".png");
		FString E;
		const bool bOk = Game->Save(Path, E);
		SetStatus(bOk ? TEXT("已存 ") + Clean : E);
		if (bOk) ShowFiles(true);
		return bOk;
	}
	bool LoadFile(const FString& Path)
	{
		if (!Game.IsValid()) return false;
		FString E;
		const bool bOk = Game->Load(Path, E);
		SetStatus(bOk ? TEXT("已讀取 ") + FPaths::GetBaseFilename(Path) : E);
		if (bOk && FileName) FileName->SetText(FText::FromString(FPaths::GetBaseFilename(Path)));
		return bOk;
	}
	int32 ListedFiles() const { return Files.Num(); }
	/** Imports a KStudio scene PNG as a new OK scene file and opens it. */
	bool ImportKStudio(const FString& Png)
	{
		if (!Game.IsValid() || Png.IsEmpty()) return false;
		FString Scene, E;
		FOKStudioImport::FReport Report;
		SetStatus(TEXT("匯入中…"));
		if (!FOKStudioImport::ImportToFile(Png, Scene, Report, E)) { SetStatus(TEXT("匯入失敗：") + E); return false; }
		const bool bOk = LoadFile(Scene);
		SetStatus(FString::Printf(TEXT("已匯入 %s：角色 %d（OK 卡 %d）、道具 %d（基礎庫 %d）、燈光 %d、相機 %d；%d 項說明見 .import.json"),
			*FPaths::GetBaseFilename(Png), Report.Characters, Report.CharacterCards, Report.Items, Report.CatalogItems, Report.Lights, Report.Cameras, Report.Notes.Num()));
		ShowFiles(true);
		return bOk;
	}

	void RefreshRows()
	{
		Rows.Reset();
		if (!Game.IsValid()) return;
		TFunction<void(int32, int32)> Walk = [&](int32 Parent, int32 Depth)
		{
			for (int32 Id : Game->Document->Children(Parent)) { TSharedPtr<FRow> R = MakeShared<FRow>(); R->Id = Id; R->Depth = Depth; Rows.Add(R); Walk(Id, Depth + 1); }
		};
		Walk(-1, 0);
		if (List)
		{
			List->RequestListRefresh();
			for (const TSharedPtr<FRow>& R : Rows) if (R->Id == Game->SelectedId) List->SetSelection(R, ESelectInfo::Direct);
		}
		RebuildProperties();
	}

	TSharedRef<ITableRow> MakeRow(TSharedPtr<FRow> Row, const TSharedRef<STableViewBase>& Owner)
	{
		const int32 Id = Row->Id;
		return SNew(STableRow<TSharedPtr<FRow>>, Owner).Padding(FMargin(4.f + 14.f * Row->Depth, 1.f, 2.f, 1.f))
		[
			SNew(STextBlock).ColorAndOpacity(FLinearColor::White).Text_Lambda([this, Id]
			{
				const FOKSceneObject* O = Game.IsValid() ? Game->Document->Find(Id) : nullptr;
				if (!O) return FText::GetEmpty();
				const bool bError = Game->SceneActor && Game->SceneActor->Errors.Contains(Id);
				return FText::FromString(FString::Printf(TEXT("[%s] %s%s%s"), KindTag(O->Kind), *O->Name, O->bVisible ? TEXT("") : TEXT("（隱藏）"), bError ? TEXT(" ⚠") : TEXT("")));
			})
		];
	}

	FOKSceneObject* Obj() const { return Game.IsValid() ? Game->Document->Find(Game->SelectedId) : nullptr; }
	void Changed() { if (Game.IsValid()) Game->Document->NotifyChanged(); }

	void Label(const FString& Text)
	{
		Props->AddSlot().AutoHeight().Padding(0.f, 6.f, 0.f, 1.f)[SNew(STextBlock).Text(FText::FromString(Text)).ColorAndOpacity(FLinearColor(0.6f, 0.85f, 1.f))];
	}
	void Float(const FString& Name, TFunction<float(FOKSceneObject&)> Get, TFunction<void(FOKSceneObject&, float)> Set, float Min, float Max, float Delta)
	{
		Props->AddSlot().AutoHeight().Padding(0.f, 1.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(0.4f).VAlign(VAlign_Center)[SNew(STextBlock).Text(FText::FromString(Name)).ColorAndOpacity(FLinearColor::White)]
			+ SHorizontalBox::Slot().FillWidth(0.6f)
			[
				SNew(SSpinBox<float>).MinValue(Min).MaxValue(Max).Delta(Delta)
				.Value_Lambda([this, Get] { FOKSceneObject* O = Obj(); return O ? Get(*O) : 0.f; })
				.OnValueChanged_Lambda([this, Set](float V) { if (FOKSceneObject* O = Obj()) { Set(*O, V); Changed(); } })
			]
		];
	}
	void Check(const FString& Name, TFunction<bool(FOKSceneObject&)> Get, TFunction<void(FOKSceneObject&, bool)> Set)
	{
		Props->AddSlot().AutoHeight().Padding(0.f, 1.f)
		[
			SNew(SCheckBox)
			.IsChecked_Lambda([this, Get] { FOKSceneObject* O = Obj(); return O && Get(*O) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
			.OnCheckStateChanged_Lambda([this, Set](ECheckBoxState S) { if (FOKSceneObject* O = Obj()) { Set(*O, S == ECheckBoxState::Checked); Changed(); } })
			[SNew(STextBlock).Text(FText::FromString(Name)).ColorAndOpacity(FLinearColor::White)]
		];
	}
	void Text(const FString& Name, TFunction<FString(FOKSceneObject&)> Get, TFunction<void(FOKSceneObject&, const FString&)> Set)
	{
		Props->AddSlot().AutoHeight().Padding(0.f, 1.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(0.3f).VAlign(VAlign_Center)[SNew(STextBlock).Text(FText::FromString(Name)).ColorAndOpacity(FLinearColor::White)]
			+ SHorizontalBox::Slot().FillWidth(0.7f)
			[
				SNew(SEditableTextBox)
				.Text_Lambda([this, Get] { FOKSceneObject* O = Obj(); return FText::FromString(O ? Get(*O) : FString()); })
				.OnTextCommitted_Lambda([this, Set](const FText& T, ETextCommit::Type) { if (FOKSceneObject* O = Obj()) { Set(*O, T.ToString()); Changed(); RefreshRows(); } })
			]
		];
	}
	void Cycle(const FString& Name, TFunction<FString(FOKSceneObject&)> Show, TFunction<void(FOKSceneObject&)> Next)
	{
		Props->AddSlot().AutoHeight().Padding(0.f, 1.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(0.4f).VAlign(VAlign_Center)[SNew(STextBlock).Text(FText::FromString(Name)).ColorAndOpacity(FLinearColor::White)]
			+ SHorizontalBox::Slot().FillWidth(0.6f)
			[
				SNew(SButton).Text_Lambda([this, Show] { FOKSceneObject* O = Obj(); return FText::FromString(O ? Show(*O) : FString()); })
				.OnClicked_Lambda([this, Next] { if (FOKSceneObject* O = Obj()) { Next(*O); Changed(); } return FReply::Handled(); })
			]
		];
	}

	void RebuildProperties()
	{
		if (!Props) return;
		Props->ClearChildren();
		FOKSceneObject* O = Obj();
		if (!O) { SceneProperties(); return; }
		Label(FString::Printf(TEXT("%s #%d"), KindTag(O->Kind), O->Id));
		Text(TEXT("名稱"), [](FOKSceneObject& X) { return X.Name; }, [](FOKSceneObject& X, const FString& V) { X.Name = V; });
		Check(TEXT("顯示"), [](FOKSceneObject& X) { return X.bVisible; }, [](FOKSceneObject& X, bool V) { X.bVisible = V; });
		Label(TEXT("位置（cm，相對父物件）"));
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			Float(FString(TEXT("XYZ")).Mid(Axis, 1), [Axis](FOKSceneObject& X) { return float(X.Transform.GetLocation()[Axis]); },
				[Axis](FOKSceneObject& X, float V) { FVector L = X.Transform.GetLocation(); L[Axis] = V; X.Transform.SetLocation(L); }, -5000.f, 5000.f, 1.f);
		}
		Label(TEXT("旋轉（度）"));
		const TCHAR* Rot[] = { TEXT("Pitch"), TEXT("Yaw"), TEXT("Roll") };
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			Float(Rot[Axis], [Axis](FOKSceneObject& X) { const FRotator R = X.Transform.Rotator(); return float(Axis == 0 ? R.Pitch : (Axis == 1 ? R.Yaw : R.Roll)); },
				[Axis](FOKSceneObject& X, float V) { FRotator R = X.Transform.Rotator(); (Axis == 0 ? R.Pitch : (Axis == 1 ? R.Yaw : R.Roll)) = V; X.Transform.SetRotation(R.Quaternion()); }, -360.f, 360.f, 1.f);
		}
		Float(TEXT("縮放"), [](FOKSceneObject& X) { return float(X.Transform.GetScale3D().X); }, [](FOKSceneObject& X, float V) { X.Transform.SetScale3D(FVector(FMath::Max(V, 0.01f))); }, 0.01f, 20.f, 0.01f);

		switch (O->Kind)
		{
		case EOKSceneObjectKind::Character:
		{
			Label(TEXT("角色"));
			Props->AddSlot().AutoHeight().Padding(0.f, 1.f)
			[
				SNew(SComboBox<TSharedPtr<FString>>).OptionsSource(&Cards)
				.OnGenerateWidget_Lambda([](TSharedPtr<FString> C) { return SNew(STextBlock).Text(FText::FromString(FPaths::GetBaseFilename(*C))); })
				.OnSelectionChanged_Lambda([this](TSharedPtr<FString> C, ESelectInfo::Type) { if (FOKSceneObject* X = Obj(); X && C) { X->Character.Card = *C; X->Name = FPaths::GetBaseFilename(*C); Changed(); RefreshRows(); } })
				[SNew(STextBlock).Text_Lambda([this] { FOKSceneObject* X = Obj(); return FText::FromString(X ? FPaths::GetCleanFilename(X->Character.Card) : FString()); })]
			];
			Float(TEXT("服裝組"), [](FOKSceneObject& X) { return float(X.Character.Outfit); }, [](FOKSceneObject& X, float V) { X.Character.Outfit = FMath::Clamp(FMath::RoundToInt(V), 0, 6); }, 0.f, 6.f, 1.f);
			Float(TEXT("動畫速度"), [](FOKSceneObject& X) { return X.Character.Speed; }, [](FOKSceneObject& X, float V) { X.Character.Speed = V; }, 0.f, 4.f, 0.05f);
			AnimBrowser(*O);
			for (int32 Part = 0; Part < 9; ++Part)
			{
				Cycle(ClothesPart(Part), [Part](FOKSceneObject& X) { return FString(ClothesStateName(X.Character.ClothesStates.IsValidIndex(Part) ? X.Character.ClothesStates[Part] : 0)); },
					[Part](FOKSceneObject& X) { X.Character.ClothesStates.SetNumZeroed(9); uint8& S = X.Character.ClothesStates[Part]; S = S == 0 ? 1 : (S == 1 ? 3 : 0); });
			}
			Label(TEXT("表情（-1 照卡片）"));
			Float(TEXT("眉"), [](FOKSceneObject& X) { return float(X.Character.EyebrowPattern); }, [](FOKSceneObject& X, float V) { X.Character.EyebrowPattern = FMath::RoundToInt(V); }, -1.f, 40.f, 1.f);
			Float(TEXT("眼"), [](FOKSceneObject& X) { return float(X.Character.EyesPattern); }, [](FOKSceneObject& X, float V) { X.Character.EyesPattern = FMath::RoundToInt(V); }, -1.f, 40.f, 1.f);
			Float(TEXT("嘴"), [](FOKSceneObject& X) { return float(X.Character.MouthPattern); }, [](FOKSceneObject& X, float V) { X.Character.MouthPattern = FMath::RoundToInt(V); }, -1.f, 40.f, 1.f);
			Float(TEXT("張嘴"), [](FOKSceneObject& X) { return X.Character.MouthOpen; }, [](FOKSceneObject& X, float V) { X.Character.MouthOpen = V; }, -1.f, 1.f, 0.05f);
			auto LookName = [](EOKSceneLook L) { return FString(L == EOKSceneLook::Fixed ? TEXT("固定姿勢") : L == EOKSceneLook::Camera ? TEXT("看鏡頭") : L == EOKSceneLook::Point ? TEXT("看注視點") : TEXT("照動畫")); };
			Cycle(TEXT("眼睛"), [LookName](FOKSceneObject& X) { return LookName(X.Character.EyesLook); }, [](FOKSceneObject& X) { X.Character.EyesLook = EOKSceneLook((uint8(X.Character.EyesLook) + 1) % 3); });
			Cycle(TEXT("脖子"), [LookName](FOKSceneObject& X) { return LookName(X.Character.NeckLook); }, [](FOKSceneObject& X) { X.Character.NeckLook = EOKSceneLook((uint8(X.Character.NeckLook) + 1) % (X.Character.FixedNeck.Num() == 2 ? 4 : 3)); });
			Label(TEXT("注視點（cm，角色空間）"));
			for (int32 Axis = 0; Axis < 3; ++Axis)
				Float(FString(TEXT("XYZ")).Mid(Axis, 1), [Axis](FOKSceneObject& X) { return float(X.Character.LookPoint[Axis]); }, [Axis](FOKSceneObject& X, float V) { X.Character.LookPoint[Axis] = V; }, -1000.f, 1000.f, 1.f);
			Label(TEXT("狀態（-1 照卡片；改了會重建角色）"));
			Float(TEXT("淚"), [](FOKSceneObject& X) { return float(X.Character.TearsLevel); }, [](FOKSceneObject& X, float V) { X.Character.TearsLevel = FMath::Clamp(FMath::RoundToInt(V), -1, 3); }, -1.f, 3.f, 1.f);
			Float(TEXT("臉紅"), [](FOKSceneObject& X) { return X.Character.Blush; }, [](FOKSceneObject& X, float V) { X.Character.Blush = V < 0.f ? -1.f : FMath::Min(V, 1.f); }, -1.f, 1.f, 0.05f);
			Float(TEXT("肌膚光澤"), [](FOKSceneObject& X) { return X.Character.SkinGloss; }, [](FOKSceneObject& X, float V) { X.Character.SkinGloss = V < 0.f ? -1.f : FMath::Min(V, 1.f); }, -1.f, 1.f, 0.05f);
			Float(TEXT("乳首"), [](FOKSceneObject& X) { return X.Character.NipStand; }, [](FOKSceneObject& X, float V) { X.Character.NipStand = V < 0.f ? -1.f : FMath::Min(V, 1.f); }, -1.f, 1.f, 0.05f);
			{
				static const TCHAR* FluidParts[5] = { TEXT("汁：臉"), TEXT("汁：胸前"), TEXT("汁：胸後"), TEXT("汁：下身前"), TEXT("汁：下身後") };
				for (int32 Part = 0; Part < 5; ++Part)
					Cycle(FluidParts[Part], [Part](FOKSceneObject& X) { return X.Character.Fluids.IsValidIndex(Part) ? FString::FromInt(X.Character.Fluids[Part]) : FString(TEXT("照卡片")); },
						[Part](FOKSceneObject& X) { if (X.Character.Fluids.Num() != 5) { X.Character.Fluids.Init(0, 5); return; } X.Character.Fluids[Part] = (X.Character.Fluids[Part] + 1) % 3; });
			}
			Cycle(TEXT("飾品"), [](FOKSceneObject& X)
				{
					if (X.Character.AccessoriesShown.Num() != 21) return FString(TEXT("照卡片"));
					int32 Shown = 0; for (int32 I = 0; I < 20; ++I) Shown += X.Character.AccessoriesShown[I] ? 1 : 0;
					return FString::Printf(TEXT("顯示 %d／20"), Shown);
				},
				[](FOKSceneObject& X) { if (X.Character.AccessoriesShown.Num() != 21) X.Character.AccessoriesShown.Init(0, 21); else X.Character.AccessoriesShown.Reset(); });
			if (O->Character.AccessoriesShown.Num() == 21)
				for (int32 Slot = 0; Slot < 21; ++Slot)
					Check(Slot < 20 ? FString::Printf(TEXT("飾品 %d"), Slot + 1) : FString(TEXT("擴充飾品（MoreAccessories）")), [Slot](FOKSceneObject& X) { return X.Character.AccessoriesShown.IsValidIndex(Slot) && X.Character.AccessoriesShown[Slot] != 0; },
						[Slot](FOKSceneObject& X, bool bOn) { if (X.Character.AccessoriesShown.IsValidIndex(Slot)) X.Character.AccessoriesShown[Slot] = bOn ? 1 : 0; });
			Label(TEXT("手型（-1 照動畫）"));
			Float(TEXT("左手"), [](FOKSceneObject& X) { return float(X.Character.HandLeft); }, [](FOKSceneObject& X, float V) { X.Character.HandLeft = FMath::RoundToInt(V); }, -1.f, 40.f, 1.f);
			Float(TEXT("右手"), [](FOKSceneObject& X) { return float(X.Character.HandRight); }, [](FOKSceneObject& X, float V) { X.Character.HandRight = FMath::RoundToInt(V); }, -1.f, 40.f, 1.f);
			FkProperties(*O);
			IkProperties(*O);
			break;
		}
		case EOKSceneObjectKind::Item:
			ItemBrowser(*O);
			{
				const FOKStudioCatalogItem* Entry = O->Item.No >= 0 ? FOKStudioCatalog::Get().FindItem(O->Item.Group, O->Item.Category, O->Item.No) : nullptr;
				for (int32 Index = 0; Index < 3; ++Index)
				{
					if (Entry && !Entry->Colors[Index]) continue;
					Text(FString::Printf(TEXT("顏色%d"), Index + 1),
						[Index](FOKSceneObject& X) { return X.Item.Colors.IsValidIndex(Index) && X.Item.Colors[Index].A >= 0.f ? TEXT("#") + X.Item.Colors[Index].ToFColor(false).ToHex().Left(6) : FString(TEXT("（模型色）")); },
						[Index](FOKSceneObject& X, const FString& V)
						{
							while (X.Item.Colors.Num() <= Index) X.Item.Colors.Add(FLinearColor(1.f, 1.f, 1.f, -1.f));
							FString Hex = V.TrimStartAndEnd();
							Hex.RemoveFromStart(TEXT("#"));
							if (Hex.Len() == 6) { const FColor C = FColor::FromHex(Hex); X.Item.Colors[Index] = FLinearColor(C.R / 255.f, C.G / 255.f, C.B / 255.f, 1.f); }
							else X.Item.Colors[Index] = FLinearColor(1.f, 1.f, 1.f, -1.f);
						});
				}
				// Pattern slots the item offers (ItemList 柄): pattern (character list mt_pattern id, 0 none), colour, scale.
				auto Slot = [](FOKSceneObject& X, int32 Index) -> FOKSceneItemPattern& { while (X.Item.Patterns.Num() <= Index) X.Item.Patterns.AddDefaulted(); return X.Item.Patterns[Index]; };
				for (int32 Index = 0; Index < 3; ++Index)
				{
					if (!Entry || !Entry->Patterns[Index]) continue;
					Float(FString::Printf(TEXT("花紋%d（0 無）"), Index + 1), [Index](FOKSceneObject& X) { return X.Item.Patterns.IsValidIndex(Index) ? float(X.Item.Patterns[Index].Pattern) : 0.f; },
						[Index, Slot](FOKSceneObject& X, float V) { Slot(X, Index).Pattern = FMath::Clamp(FMath::RoundToInt(V), 0, 9999); }, 0.f, 400.f, 1.f);
					Text(FString::Printf(TEXT("花紋%d 顏色"), Index + 1),
						[Index](FOKSceneObject& X) { return X.Item.Patterns.IsValidIndex(Index) ? TEXT("#") + X.Item.Patterns[Index].Color.ToFColor(false).ToHex().Left(6) : FString(TEXT("#646464")); },
						[Index, Slot](FOKSceneObject& X, const FString& V)
						{
							FString Hex = V.TrimStartAndEnd();
							Hex.RemoveFromStart(TEXT("#"));
							if (Hex.Len() == 6) { const FColor C = FColor::FromHex(Hex); Slot(X, Index).Color = FLinearColor(C.R / 255.f, C.G / 255.f, C.B / 255.f, Slot(X, Index).Color.A); }
						});
					Float(FString::Printf(TEXT("花紋%d 縮放"), Index + 1), [Index](FOKSceneObject& X) { return X.Item.Patterns.IsValidIndex(Index) ? float(X.Item.Patterns[Index].Uv.Z) : 1.f; },
						[Index, Slot](FOKSceneObject& X, float V) { FOKSceneItemPattern& P = Slot(X, Index); P.Uv.Z = P.Uv.W = FMath::Max(V, 0.01f); }, 0.01f, 20.f, 0.05f);
				}
				// Studio's item shadow colour, fading (items that offer it) and outline width.
				Text(TEXT("影色"),
					[](FOKSceneObject& X) { return X.Item.ShadowColor.A >= 0.f ? TEXT("#") + X.Item.ShadowColor.ToFColor(false).ToHex().Left(6) : FString(TEXT("（模型色）")); },
					[](FOKSceneObject& X, const FString& V)
					{
						FString Hex = V.TrimStartAndEnd();
						Hex.RemoveFromStart(TEXT("#"));
						if (Hex.Len() == 6) { const FColor C = FColor::FromHex(Hex); X.Item.ShadowColor = FLinearColor(C.R / 255.f, C.G / 255.f, C.B / 255.f, 1.f); }
						else X.Item.ShadowColor = FLinearColor(1.f, 1.f, 1.f, -1.f);
					});
				if (!Entry || Entry->bAlpha)
					Float(TEXT("透明度"), [](FOKSceneObject& X) { return X.Item.Alpha; }, [](FOKSceneObject& X, float V) { X.Item.Alpha = FMath::Clamp(V, 0.f, 1.f); }, 0.f, 1.f, 0.05f);
				Float(TEXT("描邊寬度"), [](FOKSceneObject& X) { return X.Item.LineWidth; }, [](FOKSceneObject& X, float V) { X.Item.LineWidth = FMath::Max(V, 0.f); }, 0.f, 2.f, 0.05f);
			}
			ItemFkProperties(*O);
			Label(TEXT("或直接指定基礎庫模型"));
			Text(TEXT("模型 id"), [](FOKSceneObject& X) { return X.Item.Model; }, [](FOKSceneObject& X, const FString& V) { X.Item.Model = V.TrimStartAndEnd(); X.Item.No = -1; });
			Text(TEXT("prefab"), [](FOKSceneObject& X) { return X.Item.Prefab; }, [](FOKSceneObject& X, const FString& V) { X.Item.Prefab = V.TrimStartAndEnd(); X.Item.No = -1; });
			break;
		case EOKSceneObjectKind::Effect:
		{
			Label(TEXT("特效（Niagara）"));
			const TArray<FOKStudioEffect>& Effects = FOKStudioCatalog::Get().Effects;
			auto EffectName = [](const FString& Id) { const FOKStudioEffect* E = FOKStudioCatalog::Get().FindEffect(Id); return E ? E->Category + TEXT("／") + E->Name : Id; };
			Cycle(TEXT("特效"), [EffectName](FOKSceneObject& X) { return EffectName(X.Effect.Effect); }, [&Effects](FOKSceneObject& X)
			{
				if (Effects.IsEmpty()) return;
				const int32 At = Effects.IndexOfByPredicate([&](const FOKStudioEffect& E) { return E.Id == X.Effect.Effect; });
				const FOKStudioEffect& Next = Effects[(At + 1) % Effects.Num()];
				X.Effect.Effect = Next.Id; X.Name = Next.Name;
			});
			Float(TEXT("速度"), [](FOKSceneObject& X) { return X.Effect.Speed; }, [](FOKSceneObject& X, float V) { X.Effect.Speed = V; }, 0.f, 10.f, 0.05f);
			Float(TEXT("強度"), [](FOKSceneObject& X) { return X.Effect.Intensity; }, [](FOKSceneObject& X, float V) { X.Effect.Intensity = V; }, 0.f, 20.f, 0.05f);
			Text(TEXT("顏色 #RRGGBB（空白：特效本身）"), [](FOKSceneObject& X) { return X.Effect.Color.A < 0.f ? FString() : X.Effect.Color.ToFColor(true).ToHex().Left(6); },
				[](FOKSceneObject& X, const FString& V)
				{
					FString Hex = V.TrimStartAndEnd();
					Hex.RemoveFromStart(TEXT("#"));
					if (Hex.Len() == 6) { const FColor C = FColor::FromHex(Hex); X.Effect.Color = FLinearColor(C.R / 255.f, C.G / 255.f, C.B / 255.f, 1.f); }
					else X.Effect.Color = FLinearColor(1.f, 1.f, 1.f, -1.f);
				});
			break;
		}
		case EOKSceneObjectKind::Light:
			Label(TEXT("燈光"));
			Cycle(TEXT("種類"), [](FOKSceneObject& X) { return FString(X.Light.Type == EOKSceneLightType::Directional ? TEXT("方向光") : (X.Light.Type == EOKSceneLightType::Spot ? TEXT("聚光") : TEXT("點光"))); },
				[](FOKSceneObject& X) { X.Light.Type = EOKSceneLightType((uint8(X.Light.Type) + 1) % 3); });
			Float(TEXT("強度"), [](FOKSceneObject& X) { return X.Light.Intensity; }, [](FOKSceneObject& X, float V) { X.Light.Intensity = V; }, 0.f, 200.f, 0.5f);
			Float(TEXT("範圍"), [](FOKSceneObject& X) { return X.Light.Range; }, [](FOKSceneObject& X, float V) { X.Light.Range = V; }, 1.f, 10000.f, 10.f);
			Float(TEXT("聚光角"), [](FOKSceneObject& X) { return X.Light.SpotAngle; }, [](FOKSceneObject& X, float V) { X.Light.SpotAngle = V; }, 1.f, 80.f, 1.f);
			Float(TEXT("紅"), [](FOKSceneObject& X) { return X.Light.Color.R; }, [](FOKSceneObject& X, float V) { X.Light.Color.R = V; }, 0.f, 1.f, 0.01f);
			Float(TEXT("綠"), [](FOKSceneObject& X) { return X.Light.Color.G; }, [](FOKSceneObject& X, float V) { X.Light.Color.G = V; }, 0.f, 1.f, 0.01f);
			Float(TEXT("藍"), [](FOKSceneObject& X) { return X.Light.Color.B; }, [](FOKSceneObject& X, float V) { X.Light.Color.B = V; }, 0.f, 1.f, 0.01f);
			Check(TEXT("陰影"), [](FOKSceneObject& X) { return X.Light.bShadows; }, [](FOKSceneObject& X, bool V) { X.Light.bShadows = V; });
			break;
		case EOKSceneObjectKind::Camera:
			Label(TEXT("相機"));
			Float(TEXT("視角"), [](FOKSceneObject& X) { return X.Camera.Fov; }, [](FOKSceneObject& X, float V) { X.Camera.Fov = V; }, 5.f, 170.f, 1.f);
			break;
		default:
			break;
		}
	}

	/** Studio FK of a character: pick a bone (the Studio FK list), then its local rotation (Unity Euler degrees). */
	void FkProperties(const FOKSceneObject& O)
	{
		const FOKStudioCatalog& C = FOKStudioCatalog::Get();
		Label(FString::Printf(TEXT("FK（%d 根骨骼有設定）"), O.Character.Fk.Num()));
		if (C.FkBones.IsEmpty()) { Label(TEXT("基礎庫沒有 FK 骨骼清單")); return; }
		if (!C.FindFkBone(FkBone)) FkBone = C.FkBones[0].Bone;
		FkOptions.Reset();
		// Main skeleton first, then the optional parts.
		for (int32 Op = 0; Op < 2; ++Op) for (int32 I = 0; I < C.FkBones.Num(); ++I) if ((C.FkBones[I].Op == 0) == (Op == 0)) FkOptions.Add(MakeShared<int32>(I));
		auto BoneLabel = [](int32 I)
		{
			const FOKStudioCatalog& Cat = FOKStudioCatalog::Get();
			return Cat.FkBones.IsValidIndex(I) ? FString::Printf(TEXT("%s %s"), *Cat.FkBones[I].Name, *Cat.FkBones[I].Bone) : FString();
		};
		Props->AddSlot().AutoHeight().Padding(0.f, 1.f)
		[
			SNew(SComboBox<TSharedPtr<int32>>).OptionsSource(&FkOptions)
			.OnGenerateWidget_Lambda([BoneLabel](TSharedPtr<int32> I) { return SNew(STextBlock).Text(FText::FromString(BoneLabel(*I))); })
			.OnSelectionChanged_Lambda([this](TSharedPtr<int32> I, ESelectInfo::Type Info) { if (I && Info != ESelectInfo::Direct) { FkBone = FOKStudioCatalog::Get().FkBones[*I].Bone; DeferRebuild(); } })
			[SNew(STextBlock).Text_Lambda([this, BoneLabel] { const FOKStudioCatalog& Cat = FOKStudioCatalog::Get(); return FText::FromString(BoneLabel(Cat.FkBones.IndexOfByPredicate([this](const FOKStudioFkBone& B) { return B.Bone == FkBone; }))); })]
		];
		if (Game.IsValid() && !Game->GuideBone.IsEmpty()) Game->GuideBone = FkBone;
		Props->AddSlot().AutoHeight().Padding(0.f, 1.f)
		[
			SNew(SCheckBox)
			.IsChecked_Lambda([this] { return Game.IsValid() && !Game->GuideBone.IsEmpty() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
			.OnCheckStateChanged_Lambda([this](ECheckBoxState S) { if (Game.IsValid()) Game->GuideBone = S == ECheckBoxState::Checked ? FkBone : FString(); })
			[SNew(STextBlock).Text(FText::FromString(TEXT("用 3D 把手轉這根骨骼"))).ColorAndOpacity(FLinearColor::White)]
		];
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			Float(FString(TEXT("XYZ")).Mid(Axis, 1),
				[this, Axis](FOKSceneObject& X) { return float(CurrentFk(X)[Axis]); },
				[this, Axis](FOKSceneObject& X, float V) { FVector E = CurrentFk(X); E[Axis] = V; SetFk(X, FkBone, E); }, -180.f, 180.f, 0.5f);
		}
		Props->AddSlot().AutoHeight().Padding(0.f, 2.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(1.f)[SNew(SButton).Text(FText::FromString(TEXT("取目前姿勢"))).OnClicked_Lambda([this] { if (FOKSceneObject* X = Obj()) { SetFk(*X, FkBone, PosedEuler(*X, FkBone)); Changed(); } return FReply::Handled(); })]
			+ SHorizontalBox::Slot().AutoWidth().Padding(1.f)[SNew(SButton).Text(FText::FromString(TEXT("移除此骨"))).OnClicked_Lambda([this] { if (FOKSceneObject* X = Obj()) { X->Character.Fk.RemoveAll([this](const FOKSceneFkBone& B) { return B.Bone == FkBone; }); Changed(); DeferRebuild(); } return FReply::Handled(); })]
			+ SHorizontalBox::Slot().AutoWidth().Padding(1.f)[SNew(SButton).Text(FText::FromString(TEXT("清除 FK"))).OnClicked_Lambda([this] { if (FOKSceneObject* X = Obj()) { X->Character.Fk.Reset(); Changed(); DeferRebuild(); } return FReply::Handled(); })]
		];
		// KK Studio pose files (UserData/Studio/pose).
		PoseOptions.Reset();
		for (const FString& F : FOKStudioPose::Files()) PoseOptions.Add(MakeShared<FString>(F));
		Props->AddSlot().AutoHeight().Padding(0.f, 2.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f)
			[
				SNew(SComboBox<TSharedPtr<FString>>).OptionsSource(&PoseOptions)
				.OnGenerateWidget_Lambda([](TSharedPtr<FString> F) { return SNew(STextBlock).Text(FText::FromString(FPaths::GetBaseFilename(*F))); })
				.OnSelectionChanged_Lambda([this](TSharedPtr<FString> F, ESelectInfo::Type Info) { if (F && Info != ESelectInfo::Direct) LoadPose(*F); })
				[SNew(STextBlock).Text(FText::FromString(FString::Printf(TEXT("讀 KK 姿勢（%d）"), PoseOptions.Num())))]
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(1.f)[SNew(SButton).Text(FText::FromString(TEXT("存成 KK 姿勢"))).OnClicked_Lambda([this]
			{
				if (FOKSceneObject* X = Obj())
				{
					const FString Name = FDateTime::Now().ToString(TEXT("%Y_%m%d_%H%M_%S"));
					SavePose(FOKStudioPose::Directory() / Name + TEXT(".dat"), Name);
				}
				return FReply::Handled();
			})]
		];
	}

public:
	/** Saves the selected character's FK as a KK Studio pose file. */
	bool SavePose(const FString& Path, const FString& Name)
	{
		FOKSceneObject* X = Obj();
		FString E;
		const bool bOk = X && X->Kind == EOKSceneObjectKind::Character && FOKStudioPose::Save(Path, X->Character, Name, E);
		SetStatus(bOk ? TEXT("已存姿勢 ") + Name : (E.IsEmpty() ? FString(TEXT("請選一個角色")) : E));
		if (bOk) DeferRebuild();
		return bOk;
	}
	/** Applies a KK Studio pose file to the selected character. */
	bool LoadPose(const FString& Path)
	{
		FOKSceneObject* X = Obj();
		FString E, Name;
		const bool bOk = X && X->Kind == EOKSceneObjectKind::Character && FOKStudioPose::Load(Path, X->Character, E, &Name);
		SetStatus(bOk ? FString::Printf(TEXT("已套用姿勢 %s（%d 根 FK 骨骼）"), *Name, X->Character.Fk.Num()) : (E.IsEmpty() ? FString(TEXT("請選一個角色")) : E));
		if (bOk) { Changed(); DeferRebuild(); }
		return bOk;
	}

private:
	TArray<TSharedPtr<FString>> PoseOptions;

	/** Studio animations of the OK animation library: Studio or H motion, group, category, then the motion. */
	void AnimBrowser(const FOKSceneObject& O)
	{
		const FOKStudioCatalog& C = FOKStudioCatalog::Get();
		const FOKStudioAnim* Current = O.Character.StudioAnimeNo >= 0 ? C.FindAnim(O.Character.StudioAnimeKind, O.Character.StudioAnimeGroup, O.Character.StudioAnimeCategory, O.Character.StudioAnimeNo) : nullptr;
		Label(TEXT("Studio 動畫：") + (Current ? Current->Name : FString(TEXT("（無）"))));
		if (C.Anims.IsEmpty()) { Label(TEXT("基礎庫沒有 Studio 動畫（studio_anims.json）")); return; }
		if (Current) { AnimKind = Current->Kind; AnimGroup = Current->Group; AnimCategory = Current->Category; }
		// Groups and categories that have converted motions of this kind.
		TSet<int32> Groups;
		for (const FOKStudioAnim& A : C.Anims) if (A.Kind == AnimKind && !A.File.IsEmpty()) Groups.Add(A.Group);
		if (!Groups.Contains(AnimGroup) && Groups.Num()) AnimGroup = *Groups.CreateConstIterator();
		TSet<int32> Cats;
		for (const FOKStudioAnim& A : C.Anims) if (A.Kind == AnimKind && A.Group == AnimGroup && !A.File.IsEmpty()) Cats.Add(A.Category);
		if (!Cats.Contains(AnimCategory) && Cats.Num()) AnimCategory = *Cats.CreateConstIterator();
		AnimGroupOptions.Reset(); for (int32 G : Groups) AnimGroupOptions.Add(MakeShared<int32>(G));
		AnimGroupOptions.Sort([](const TSharedPtr<int32>& A, const TSharedPtr<int32>& B) { return *A < *B; });
		AnimCategoryOptions.Reset(); for (int32 K : Cats) AnimCategoryOptions.Add(MakeShared<int32>(K));
		AnimCategoryOptions.Sort([](const TSharedPtr<int32>& A, const TSharedPtr<int32>& B) { return *A < *B; });
		AnimOptions.Reset();
		for (int32 I = 0; I < C.Anims.Num(); ++I) { const FOKStudioAnim& A = C.Anims[I]; if (A.Kind == AnimKind && A.Group == AnimGroup && A.Category == AnimCategory && !A.File.IsEmpty()) AnimOptions.Add(MakeShared<int32>(I)); }
		auto GroupName = [](int32 G) { const FOKStudioCatalogGroup* X = FOKStudioCatalog::Get().AnimGroups.FindByPredicate([G](const FOKStudioCatalogGroup& Y) { return Y.Id == G; }); return X ? FString::Printf(TEXT("%d %s"), G, *X->Name) : FString::FromInt(G); };
		auto CategoryName = [this](int32 K) { const int32 G = AnimGroup; const FOKStudioCatalogGroup* X = FOKStudioCatalog::Get().AnimCategories.FindByPredicate([G, K](const FOKStudioCatalogGroup& Y) { return Y.Group == G && Y.Id == K; }); return X ? FString::Printf(TEXT("%d %s"), K, *X->Name) : FString::FromInt(K); };
		Props->AddSlot().AutoHeight().Padding(0.f, 1.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(1.f)[SNew(SButton).Text(FText::FromString(TEXT("Studio 動作"))).OnClicked_Lambda([this] { AnimKind = 0; DeferRebuild(); return FReply::Handled(); })]
			+ SHorizontalBox::Slot().AutoWidth().Padding(1.f)[SNew(SButton).Text(FText::FromString(TEXT("H 動作"))).OnClicked_Lambda([this] { AnimKind = 1; DeferRebuild(); return FReply::Handled(); })]
			+ SHorizontalBox::Slot().AutoWidth().Padding(1.f)[SNew(SButton).Text(FText::FromString(TEXT("停止動畫"))).OnClicked_Lambda([this] { if (FOKSceneObject* X = Obj()) { X->Character.StudioAnimeNo = -1; Changed(); DeferRebuild(); } return FReply::Handled(); })]
		];
		auto Combo = [this](TArray<TSharedPtr<int32>>* Options, TFunction<FString(int32)> Show, TFunction<int32()> Current, TFunction<void(int32)> Pick)
		{
			Props->AddSlot().AutoHeight().Padding(0.f, 1.f)
			[
				SNew(SComboBox<TSharedPtr<int32>>).OptionsSource(Options)
				.OnGenerateWidget_Lambda([Show](TSharedPtr<int32> V) { return SNew(STextBlock).Text(FText::FromString(Show(*V))); })
				.OnSelectionChanged_Lambda([Pick](TSharedPtr<int32> V, ESelectInfo::Type Info) { if (V && Info != ESelectInfo::Direct) Pick(*V); })
				[SNew(STextBlock).Text_Lambda([Show, Current] { return FText::FromString(Show(Current())); })]
			];
		};
		Combo(&AnimGroupOptions, GroupName, [this] { return AnimGroup; }, [this](int32 V) { AnimGroup = V; AnimCategory = -1; DeferRebuild(); });
		Combo(&AnimCategoryOptions, CategoryName, [this] { return AnimCategory; }, [this](int32 V) { AnimCategory = V; DeferRebuild(); });
		Props->AddSlot().AutoHeight().MaxHeight(220.f).Padding(0.f, 2.f)
		[
			SNew(SListView<TSharedPtr<int32>>).ListItemsSource(&AnimOptions).SelectionMode(ESelectionMode::Single)
			.OnGenerateRow_Lambda([this](TSharedPtr<int32> I, const TSharedRef<STableViewBase>& Owner)
			{
				const FOKStudioAnim& A = FOKStudioCatalog::Get().Anims[*I];
				const FOKSceneObject* X = Obj();
				const bool bCurrent = X && X->Character.StudioAnimeNo == A.No && X->Character.StudioAnimeKind == A.Kind && X->Character.StudioAnimeGroup == A.Group && X->Character.StudioAnimeCategory == A.Category;
				return SNew(STableRow<TSharedPtr<int32>>, Owner)[SNew(STextBlock).ColorAndOpacity(bCurrent ? FLinearColor::Yellow : FLinearColor::White).Text(FText::FromString(A.Name))];
			})
			.OnMouseButtonClick_Lambda([this](TSharedPtr<int32> I) { if (I) PickAnim(*I); })
		];
	}

public:
	/** Plays catalogue animation `Index` on the selected character. */
	bool PickAnim(int32 Index)
	{
		FOKSceneObject* X = Obj();
		const FOKStudioCatalog& C = FOKStudioCatalog::Get();
		if (!X || X->Kind != EOKSceneObjectKind::Character || !C.Anims.IsValidIndex(Index) || C.Anims[Index].File.IsEmpty()) return false;
		const FOKStudioAnim& A = C.Anims[Index];
		X->Character.StudioAnimeKind = A.Kind; X->Character.StudioAnimeGroup = A.Group; X->Character.StudioAnimeCategory = A.Category; X->Character.StudioAnimeNo = A.No;
		X->Character.Time = 0.f;
		Changed();
		DeferRebuild();
		return true;
	}

private:
	int32 AnimKind = 0, AnimGroup = -1, AnimCategory = -1;
	TArray<TSharedPtr<int32>> AnimGroupOptions, AnimCategoryOptions, AnimOptions;

	/** Studio IK of a character: pick an effector, enable its target (it starts where the limb is), move it. */
	void IkProperties(const FOKSceneObject& O)
	{
		static const TCHAR* Names[13] = { TEXT("身體"), TEXT("左肩"), TEXT("右肩"), TEXT("左大腿"), TEXT("右大腿"), TEXT("左手"), TEXT("右手"), TEXT("左腳"), TEXT("右腳"),
			TEXT("左手肘（彎曲方向）"), TEXT("右手肘（彎曲方向）"), TEXT("左膝（彎曲方向）"), TEXT("右膝（彎曲方向）") };
		Label(FString::Printf(TEXT("IK（%d 個目標）"), O.Character.Ik.Num()));
		IkOptions.Reset();
		for (int32 I = 0; I < 13; ++I) IkOptions.Add(MakeShared<int32>(I));
		Props->AddSlot().AutoHeight().Padding(0.f, 1.f)
		[
			SNew(SComboBox<TSharedPtr<int32>>).OptionsSource(&IkOptions)
			.OnGenerateWidget_Lambda([](TSharedPtr<int32> I) { return SNew(STextBlock).Text(FText::FromString(Names[*I])); })
			.OnSelectionChanged_Lambda([this](TSharedPtr<int32> I, ESelectInfo::Type Info) { if (I && Info != ESelectInfo::Direct) { IkEffector = *I; if (Game.IsValid() && Game->GuideIk >= 0) Game->GuideIk = IkEffector; DeferRebuild(); } })
			[SNew(STextBlock).Text_Lambda([this] { return FText::FromString(Names[FMath::Clamp(IkEffector, 0, 12)]); })]
		];
		Check(TEXT("啟用這個 IK 目標"), [this](FOKSceneObject& X) { return X.Character.Ik.ContainsByPredicate([this](const FOKSceneIkTarget& T) { return T.Effector == IkEffector; }); },
			[this](FOKSceneObject& X, bool bOn) { SetIkEnabled(X, IkEffector, bOn); DeferRebuild(); });
		if (!O.Character.Ik.ContainsByPredicate([this](const FOKSceneIkTarget& T) { return T.Effector == IkEffector; })) return;
		Props->AddSlot().AutoHeight().Padding(0.f, 1.f)
		[
			SNew(SCheckBox)
			.IsChecked_Lambda([this] { return Game.IsValid() && Game->GuideIk == IkEffector ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
			.OnCheckStateChanged_Lambda([this](ECheckBoxState S) { if (Game.IsValid()) { Game->GuideIk = S == ECheckBoxState::Checked ? IkEffector : -1; if (Game->GuideIk >= 0) Game->GuideBone.Reset(); } })
			[SNew(STextBlock).Text(FText::FromString(TEXT("用 3D 把手移動 IK 目標"))).ColorAndOpacity(FLinearColor::White)]
		];
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			Float(FString(TEXT("XYZ")).Mid(Axis, 1),
				[this, Axis](FOKSceneObject& X) { const FOKSceneIkTarget* T = X.Character.Ik.FindByPredicate([this](const FOKSceneIkTarget& Y) { return Y.Effector == IkEffector; }); return T ? float(T->Position[Axis]) : 0.f; },
				[this, Axis](FOKSceneObject& X, float V) { if (FOKSceneIkTarget* T = X.Character.Ik.FindByPredicate([this](const FOKSceneIkTarget& Y) { return Y.Effector == IkEffector; })) T->Position[Axis] = V; }, -300.f, 300.f, 0.5f);
		}
	}

public:
	/** Turns a character's IK target on (where the limb is now) or off. */
	bool SetIkEnabled(FOKSceneObject& X, int32 Effector, bool bOn)
	{
		X.Character.Ik.RemoveAll([Effector](const FOKSceneIkTarget& T) { return T.Effector == Effector; });
		if (bOn)
		{
			// A new target starts where the limb is now (the solver knows its effector bones once prepared).
			AKKPreviewActor* P = Game.IsValid() && Game->SceneActor ? Game->SceneActor->PreviewOf(X.Id) : nullptr;
			FVector Unity;
			FString Error;
			if (!P || !P->Session.PrepareStudioIk(Error) || !P->Session.IkEffectorPosition(Effector, Unity)) { SetStatus(TEXT("IK 無法啟用：") + Error); return false; }
			P->bStudioIkReady = true;
			X.Character.Ik.Add({ Effector, AKKPreviewActor::UnityToActorLocal(Unity) });
		}
		if (Game.IsValid() && !bOn && Game->GuideIk == Effector) Game->GuideIk = -1;
		Changed();
		return true;
	}

private:
	int32 IkEffector = 5;
	TArray<TSharedPtr<int32>> IkOptions;

	/** The bone's FK rotation, or its posed rotation when it has none yet. */
	FVector CurrentFk(const FOKSceneObject& X) const
	{
		const FOKSceneFkBone* B = X.Character.Fk.FindByPredicate([this](const FOKSceneFkBone& F) { return F.Bone == FkBone; });
		return B ? B->Euler : PosedEuler(X, FkBone);
	}
	FVector PosedEuler(const FOKSceneObject& X, const FString& Bone) const
	{
		FVector E = FVector::ZeroVector;
		if (Game.IsValid() && Game->SceneActor) if (const AKKPreviewActor* P = Game->SceneActor->PreviewOf(X.Id)) P->Session.BoneEuler(Bone, E);
		return E;
	}

public:
	/** Sets one FK bone of a character object. */
	static void SetFk(FOKSceneObject& X, const FString& Bone, const FVector& Euler)
	{
		if (FOKSceneFkBone* B = X.Character.Fk.FindByPredicate([&](const FOKSceneFkBone& F) { return F.Bone == Bone; })) B->Euler = Euler;
		else X.Character.Fk.Add({ Bone, Euler });
	}

private:
	FString FkBone;
	TArray<TSharedPtr<int32>> FkOptions;

	/** Scene settings (nothing selected): the studio map. */
	void SceneProperties()
	{
		Label(TEXT("場景（選一個物件來編輯它）"));
		const FOKStudioCatalog& C = FOKStudioCatalog::Get();
		MapOptions.Reset();
		MapOptions.Add(MakeShared<int32>(-1));
		for (const FOKStudioCatalogMap& M : C.Maps) MapOptions.Add(MakeShared<int32>(M.No));
		auto MapName = [](int32 No) { const FOKStudioCatalogMap* M = FOKStudioCatalog::Get().FindMap(No); return M ? FString::Printf(TEXT("%d %s"), No, *M->Name) : FString(TEXT("（無地圖）")); };
		Props->AddSlot().AutoHeight().Padding(0.f, 1.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(0.3f).VAlign(VAlign_Center)[SNew(STextBlock).Text(FText::FromString(TEXT("地圖"))).ColorAndOpacity(FLinearColor::White)]
			+ SHorizontalBox::Slot().FillWidth(0.7f)
			[
				SNew(SComboBox<TSharedPtr<int32>>).OptionsSource(&MapOptions)
				.OnGenerateWidget_Lambda([MapName](TSharedPtr<int32> No) { return SNew(STextBlock).Text(FText::FromString(MapName(*No))); })
				.OnSelectionChanged_Lambda([this](TSharedPtr<int32> No, ESelectInfo::Type) { if (No && Game.IsValid()) { Game->Document->Map = *No; Changed(); } })
				[SNew(STextBlock).Text_Lambda([this, MapName] { return FText::FromString(Game.IsValid() ? MapName(Game->Document->Map) : FString()); })]
			]
		];
		if (!C.bLoaded) Label(C.Error);
		else if (Game.IsValid() && Game->SceneActor && !Game->SceneActor->MapError.IsEmpty()) Label(Game->SceneActor->MapError);
		Label(TEXT("畫面後製"));
		auto Doc = [this]() -> FOKScenePost* { return Game.IsValid() ? &Game->Document->Post : nullptr; };
		auto PostCheck = [this, Doc](const FString& Name, bool FOKScenePost::* Field)
		{
			Props->AddSlot().AutoHeight().Padding(0.f, 1.f)
			[
				SNew(SCheckBox)
				.IsChecked_Lambda([Doc, Field] { FOKScenePost* P = Doc(); return P && P->*Field ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([this, Doc, Field](ECheckBoxState S) { if (FOKScenePost* P = Doc()) { P->*Field = S == ECheckBoxState::Checked; Changed(); } })
				[SNew(STextBlock).Text(FText::FromString(Name)).ColorAndOpacity(FLinearColor::White)]
			];
		};
		auto PostFloat = [this, Doc](const FString& Name, float FOKScenePost::* Field, float Min, float Max, float Delta)
		{
			Props->AddSlot().AutoHeight().Padding(0.f, 1.f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(0.4f).VAlign(VAlign_Center)[SNew(STextBlock).Text(FText::FromString(Name)).ColorAndOpacity(FLinearColor::White)]
				+ SHorizontalBox::Slot().FillWidth(0.6f)
				[
					SNew(SSpinBox<float>).MinValue(Min).MaxValue(Max).Delta(Delta)
					.Value_Lambda([Doc, Field] { FOKScenePost* P = Doc(); return P ? P->*Field : 0.f; })
					.OnValueChanged_Lambda([this, Doc, Field](float V) { if (FOKScenePost* P = Doc()) { P->*Field = V; Changed(); } })
				]
			];
		};
		PostCheck(TEXT("泛光"), &FOKScenePost::bBloom);
		PostFloat(TEXT("泛光強度"), &FOKScenePost::BloomIntensity, 0.f, 8.f, 0.05f);
		PostFloat(TEXT("泛光門檻（-1 全部）"), &FOKScenePost::BloomThreshold, -1.f, 1.f, 0.02f);
		PostFloat(TEXT("泛光大小"), &FOKScenePost::BloomSize, 0.1f, 8.f, 0.05f);
		PostCheck(TEXT("景深"), &FOKScenePost::bDepthOfField);
		PostFloat(TEXT("對焦距離 cm"), &FOKScenePost::FocalDistance, 10.f, 5000.f, 5.f);
		PostFloat(TEXT("光圈 f"), &FOKScenePost::Aperture, 0.7f, 32.f, 0.1f);
		PostCheck(TEXT("暗角"), &FOKScenePost::bVignette);
		PostFloat(TEXT("暗角強度"), &FOKScenePost::VignetteIntensity, 0.f, 1.f, 0.02f);
		PostFloat(TEXT("曝光補償 EV"), &FOKScenePost::Exposure, -5.f, 5.f, 0.1f);
		// Studio's fog, ambient occlusion and sun shafts (OKStudioPost).
		PostCheck(TEXT("霧"), &FOKScenePost::bFog);
		PostCheck(TEXT("環境遮蔽"), &FOKScenePost::bAmbientOcclusion);
		PostFloat(TEXT("環境遮蔽半徑 m"), &FOKScenePost::AmbientOcclusionRadius, 0.01f, 2.f, 0.01f);
		PostCheck(TEXT("太陽光束"), &FOKScenePost::bSunShafts);
		// Studio's sky (SceneInfo.skyInfo): on/off and the StudioSky pattern.
		PostCheck(TEXT("天空"), &FOKScenePost::bSky);
		SkyOptions.Reset();
		for (const FOKStudioSkyPattern& P : OKStudioSkyPatterns()) SkyOptions.Add(MakeShared<int32>(P.No));
		auto SkyName = [](int32 No) { for (const FOKStudioSkyPattern& P : OKStudioSkyPatterns()) if (P.No == No) return FString::Printf(TEXT("%d %s"), No, *P.Name); return FString::Printf(TEXT("%d"), No); };
		Props->AddSlot().AutoHeight().Padding(0.f, 1.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(0.4f).VAlign(VAlign_Center)[SNew(STextBlock).Text(FText::FromString(TEXT("天空樣式"))).ColorAndOpacity(FLinearColor::White)]
			+ SHorizontalBox::Slot().FillWidth(0.6f)
			[
				SNew(SComboBox<TSharedPtr<int32>>).OptionsSource(&SkyOptions)
				.OnGenerateWidget_Lambda([SkyName](TSharedPtr<int32> No) { return SNew(STextBlock).Text(FText::FromString(SkyName(*No))); })
				.OnSelectionChanged_Lambda([this, Doc](TSharedPtr<int32> No, ESelectInfo::Type) { if (FOKScenePost* P = Doc(); P && No) { P->SkyPattern = *No; Changed(); } })
				[SNew(STextBlock).Text_Lambda([Doc, SkyName] { FOKScenePost* P = Doc(); return FText::FromString(P ? SkyName(P->SkyPattern) : FString()); })]
			]
		];
		// The map's time of day (SceneInfo.sunLightType): a map with its own sky shows that time's sky.
		SunOptions.Reset();
		for (int32 T = 0; T < 4; ++T) SunOptions.Add(MakeShared<int32>(T));
		auto SunName = [](int32 T) { static const TCHAR* Names[] = { TEXT("朝"), TEXT("昼"), TEXT("夕"), TEXT("夜") }; return FString(T >= 0 && T < 4 ? Names[T] : TEXT("?")); };
		Props->AddSlot().AutoHeight().Padding(0.f, 1.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(0.4f).VAlign(VAlign_Center)[SNew(STextBlock).Text(FText::FromString(TEXT("時段"))).ColorAndOpacity(FLinearColor::White)]
			+ SHorizontalBox::Slot().FillWidth(0.6f)
			[
				SNew(SComboBox<TSharedPtr<int32>>).OptionsSource(&SunOptions)
				.OnGenerateWidget_Lambda([SunName](TSharedPtr<int32> T) { return SNew(STextBlock).Text(FText::FromString(SunName(*T))); })
				.OnSelectionChanged_Lambda([this, Doc](TSharedPtr<int32> T, ESelectInfo::Type) { if (FOKScenePost* P = Doc(); P && T) { P->SunType = *T; Changed(); } })
				[SNew(STextBlock).Text_Lambda([Doc, SunName] { FOKScenePost* P = Doc(); return FText::FromString(P ? SunName(P->SunType) : FString()); })]
			]
		];
	}

	/** Studio catalogue browser: group, category, then the items of that category. */
	void ItemBrowser(const FOKSceneObject& O)
	{
		const FOKStudioCatalog& C = FOKStudioCatalog::Get();
		Label(TEXT("Studio 道具"));
		if (!C.bLoaded) { Label(C.Error); return; }
		if (O.Item.No >= 0) { BrowseGroup = O.Item.Group; BrowseCategory = O.Item.Category; }
		if (BrowseGroup < 0 && C.Groups.Num()) BrowseGroup = C.Groups[0].Id;
		const TArray<const FOKStudioCatalogGroup*> Cats = C.CategoriesOf(BrowseGroup);
		if (!Cats.ContainsByPredicate([this](const FOKStudioCatalogGroup* G) { return G->Id == BrowseCategory; })) BrowseCategory = Cats.Num() ? Cats[0]->Id : -1;
		GroupOptions.Reset();
		for (const FOKStudioCatalogGroup& G : C.Groups) GroupOptions.Add(MakeShared<int32>(G.Id));
		CategoryOptions.Reset();
		for (const FOKStudioCatalogGroup* G : Cats) CategoryOptions.Add(MakeShared<int32>(G->Id));
		ItemOptions.Reset();
		for (const FOKStudioCatalogItem* I : C.ItemsIn(BrowseGroup, BrowseCategory)) ItemOptions.Add(MakeShared<int32>(I->No));
		auto GroupName = [](int32 Id) { const FOKStudioCatalogGroup* G = FOKStudioCatalog::Get().Groups.FindByPredicate([Id](const FOKStudioCatalogGroup& X) { return X.Id == Id; }); return G ? G->Name : FString(); };
		auto CategoryName = [this](int32 Id) { const int32 Group = BrowseGroup; const FOKStudioCatalogGroup* G = FOKStudioCatalog::Get().Categories.FindByPredicate([Id, Group](const FOKStudioCatalogGroup& X) { return X.Id == Id && X.Group == Group; }); return G ? G->Name : FString(); };
		auto Combo = [this](const FString& Name, TArray<TSharedPtr<int32>>* Options, TFunction<FString(int32)> Show, TFunction<int32()> Current, TFunction<void(int32)> Pick)
		{
			Props->AddSlot().AutoHeight().Padding(0.f, 1.f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(0.3f).VAlign(VAlign_Center)[SNew(STextBlock).Text(FText::FromString(Name)).ColorAndOpacity(FLinearColor::White)]
				+ SHorizontalBox::Slot().FillWidth(0.7f)
				[
					SNew(SComboBox<TSharedPtr<int32>>).OptionsSource(Options)
					.OnGenerateWidget_Lambda([Show](TSharedPtr<int32> V) { return SNew(STextBlock).Text(FText::FromString(Show(*V))); })
					.OnSelectionChanged_Lambda([Pick](TSharedPtr<int32> V, ESelectInfo::Type Info) { if (V && Info != ESelectInfo::Direct) Pick(*V); })
					[SNew(STextBlock).Text_Lambda([Show, Current] { return FText::FromString(Show(Current())); })]
				]
			];
		};
		Combo(TEXT("大分類"), &GroupOptions, GroupName, [this] { return BrowseGroup; }, [this](int32 V) { BrowseGroup = V; BrowseCategory = -1; DeferRebuild(); });
		Combo(TEXT("中分類"), &CategoryOptions, CategoryName, [this] { return BrowseCategory; }, [this](int32 V) { BrowseCategory = V; DeferRebuild(); });
		Props->AddSlot().AutoHeight().MaxHeight(260.f).Padding(0.f, 2.f)
		[
			SNew(SListView<TSharedPtr<int32>>).ListItemsSource(&ItemOptions).SelectionMode(ESelectionMode::Single)
			.OnGenerateRow_Lambda([this](TSharedPtr<int32> No, const TSharedRef<STableViewBase>& Owner)
			{
				const FOKStudioCatalogItem* I = FOKStudioCatalog::Get().FindItem(BrowseGroup, BrowseCategory, *No);
				const FOKSceneObject* X = Obj();
				const bool bCurrent = X && I && X->Item.No == I->No && X->Item.Group == I->Group && X->Item.Category == I->Category;
				return SNew(STableRow<TSharedPtr<int32>>, Owner)[SNew(STextBlock).ColorAndOpacity(bCurrent ? FLinearColor::Yellow : FLinearColor::White).Text(FText::FromString(I ? I->Name : FString()))];
			})
			.OnMouseButtonClick_Lambda([this](TSharedPtr<int32> No) { if (No) PickItem(BrowseGroup, BrowseCategory, *No); })
		];
	}

public:
	/** Makes the selected item object show a catalogue item. */
	bool PickItem(int32 Group, int32 Category, int32 No)
	{
		FOKSceneObject* X = Obj();
		const FOKStudioCatalogItem* I = FOKStudioCatalog::Get().FindItem(Group, Category, No);
		if (!X || X->Kind != EOKSceneObjectKind::Item || !I) return false;
		X->Item.Group = Group; X->Item.Category = Category; X->Item.No = No;
		X->Item.Model.Reset(); X->Item.Prefab.Reset();
		X->Name = I->Name;
		Changed();
		DeferRebuild();
		return true;
	}

private:
	/** Rebuilds the rows and properties after the current Slate event (a widget cannot be destroyed inside its own callback). */
	void DeferRebuild()
	{
		RegisterActiveTimer(0.f, FWidgetActiveTimerDelegate::CreateLambda([this](double, float) { RefreshRows(); return EActiveTimerReturnType::Stop; }));
	}

	int32 BrowseGroup = -1;
	int32 BrowseCategory = -1;
	TArray<TSharedPtr<int32>> GroupOptions, CategoryOptions, ItemOptions, MapOptions, SkyOptions, SunOptions;

	TWeakObjectPtr<UOKStudioGame> Game;
	TArray<TSharedPtr<FRow>> Rows;
	TArray<TSharedPtr<FString>> Cards;
	/** Scene list: the tag of each file's kind (KStudio, old .okscene). */
	TMap<FString, FString> FileKinds;
	/** Item FK bones per "model|prefab" (asked of the library once). */
	TMap<FString, TArray<FString>> ItemFkBones;

	/** Studio item FK: each bone the item's skinned meshes follow, its local rotation (unset bones keep the model's). */
	void ItemFkProperties(FOKSceneObject& O)
	{
		FString Model, Prefab, Error;
		if (!AOKSceneActor::ItemModel(O.Item, Model, Prefab, Error)) return;
		const FString Key = Model + TEXT("|") + Prefab;
		if (!ItemFkBones.Contains(Key)) ItemFkBones.Add(Key, FOkSession::OkModelFkBones(AOKSceneActor::LibraryDirectory(), Model, Prefab));
		const TArray<FString> Bones = ItemFkBones[Key];
		if (Bones.IsEmpty()) return;
		Label(FString::Printf(TEXT("道具 FK（%d 根骨頭，度；設了才會轉）"), Bones.Num()));
		Props->AddSlot().AutoHeight().Padding(0.f, 1.f)
		[
			SNew(SButton).Text(FText::FromString(TEXT("清除道具 FK")))
			.OnClicked_Lambda([this] { if (FOKSceneObject* X = Obj()) { X->Item.Fk.Reset(); Changed(); RebuildProperties(); } return FReply::Handled(); })
		];
		for (const FString& Bone : Bones)
			for (int32 Axis = 0; Axis < 3; ++Axis)
				Float(Bone + TEXT(" ") + FString(TEXT("XYZ")).Mid(Axis, 1),
					[Bone, Axis](FOKSceneObject& X) { const FOKSceneFkBone* B = X.Item.Fk.FindByPredicate([&](const FOKSceneFkBone& F) { return F.Bone == Bone; }); return B ? float(B->Euler[Axis]) : 0.f; },
					[Bone, Axis](FOKSceneObject& X, float V)
					{
						FOKSceneFkBone* B = X.Item.Fk.FindByPredicate([&](const FOKSceneFkBone& F) { return F.Bone == Bone; });
						if (!B) B = &X.Item.Fk.Add_GetRef({ Bone, FVector::ZeroVector });
						B->Euler[Axis] = V;
					}, -360.f, 360.f, 1.f);
	}
	TSharedPtr<SListView<TSharedPtr<FRow>>> List;
	TSharedPtr<SVerticalBox> Props;
	TSharedPtr<STextBlock> Status;
	TSharedPtr<SVerticalBox> FilesBox;
	TSharedPtr<SEditableTextBox> FileName;
	TSharedPtr<SListView<TSharedPtr<FString>>> FileList;
	TArray<TSharedPtr<FString>> Files;
	bool bFilesShown = false;
	TSharedPtr<SEditableTextBox> ImportPath;
};

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
	Game->Document->OnChanged.AddLambda([Game] { Game->bSyncPending = true; });
	FActorSpawnParameters Params;
	Params.ObjectFlags = RF_Transient;
	Game->SceneActor = World->SpawnActor<AOKSceneActor>(FVector::ZeroVector, FRotator::ZeroRotator, Params);
	Game->Viewer = World->SpawnActor<ACameraActor>(FVector::ZeroVector, FRotator::ZeroRotator, Params);
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
	return Game;
}

void UOKStudioGame::Stop()
{
	bStopRequested = false;
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
	SelectedId = Id;
	if (Panel) Panel->RefreshRows();
}

bool UOKStudioGame::Save(const FString& Path, FString& OutError)
{
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
	if (FPaths::GetExtension(Path).Equals(TEXT("png"), ESearchCase::IgnoreCase))
	{
		// OK scene card: the current view and the three views, with the scene and its character cards inside.
		if (!Viewer || !SceneActor) { OutError = TEXT("studio not running"); return false; }
		UpdateViewer();
		SyncNow();
		const UCameraComponent* Camera = Viewer->GetCameraComponent();
		TArray<TArray<uint8>> Views;
		if (!FOKSceneCard::CaptureViews(SceneActor, Camera->GetComponentTransform(), Camera->FieldOfView, &Camera->PostProcessSettings, Views, OutError)) return false;
		if (!FOKSceneCard::Write(Document, Views, Path, OutError)) return false;
	}
	else if (!Document->SaveToFile(Path, OutError)) return false;
	File = Path;
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

namespace
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

bool UOKStudioGame::GuideHandles(TArray<FGuideHandle>& Out, FVector2D& Center, double& CmPerPixel) const
{
	Out.Reset();
	APlayerController* PC = SceneActor && SceneActor->GetWorld() ? SceneActor->GetWorld()->GetFirstPlayerController() : nullptr;
	if (!PC || !PC->PlayerCameraManager || !Document || !Document->Find(SelectedId)) return false;
	FTransform World = Document->WorldTransform(SelectedId) * SceneActor->GetActorTransform();
	// Bone guide: rotate rings around the bone's local axes.
	FVector BoneAxes[3];
	const bool bBone = !GuideBone.IsEmpty() && Document->Find(SelectedId)->Kind == EOKSceneObjectKind::Character;
	if (bBone)
	{
		FTransform Bone;
		if (!BoneWorld(SceneActor->PreviewOf(SelectedId), GuideBone, Bone)) return false;
		World = Bone;
		for (int32 K = 0; K < 3; ++K) BoneAxes[K] = Bone.GetRotation().RotateVector(UnityAxesInUe[K]).GetSafeNormal();
	}
	FVector IkWorld;
	const bool bIk = !bBone && GuideIk >= 0 && IkTargetWorld(GuideIk, IkWorld);
	if (bIk) World = FTransform(IkWorld);
	const int32 Mode = bBone ? 1 : (bIk ? 0 : GuideMode);
	const FVector O = World.GetLocation();
	auto Project = [PC](const FVector& W, FVector2D& S) { return UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition(PC, W, S, true); };
	// Panel pixels per 10 cm at the pivot, measured sideways to the camera.
	FVector2D Side;
	if (!Project(O, Center) || !Project(O + PC->PlayerCameraManager->GetCameraRotation().RotateVector(FVector(0, 1, 0)) * 10., Side)) return false;
	const double PixelsPer10 = FVector2D::Distance(Center, Side);
	if (PixelsPer10 < 1e-3) return false;
	CmPerPixel = 10. / PixelsPer10;
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		FGuideHandle H;
		H.Axis = Axis;
		const FVector A = bBone ? BoneAxes[Axis] : GuideAxes[Axis];
		if (Mode == 1)
		{
			const FVector U = (FMath::Abs(A.Z) < .9 ? (A ^ FVector::UpVector) : (A ^ FVector::ForwardVector)).GetSafeNormal();
			const FVector V = A ^ U;
			for (int32 I = 0; I < 48; ++I)
			{
				const double T = 2. * UE_DOUBLE_PI * I / 48.;
				FVector2D P;
				if (!Project(O + (U * FMath::Cos(T) + V * FMath::Sin(T)) * 85. * CmPerPixel, P)) { H.Points.Reset(); break; }
				H.Points.Add(P);
			}
			H.bClosed = true;
		}
		else
		{
			FVector2D End;
			if (!Project(O + A * 10., End) || FVector2D::Distance(End, Center) < .5) continue;
			const FVector2D N = (End - Center).GetSafeNormal();
			H.Points = { Center + N * 14., Center + N * (Mode == 2 ? 95. : 110.) };
		}
		if (H.Points.Num() >= 2) Out.Add(MoveTemp(H));
	}
	if (Mode == 2)
	{
		FGuideHandle H;
		H.Axis = 3;
		H.Points = { Center + FVector2D(-9, -9), Center + FVector2D(9, -9), Center + FVector2D(9, 9), Center + FVector2D(-9, 9) };
		H.bClosed = true;
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
		if (H.Axis == 3 && FMath::Abs(Position.X - Center.X) <= 10. && FMath::Abs(Position.Y - Center.Y) <= 10.) return 3;
		const int32 Segments = H.Points.Num() - (H.bClosed ? 0 : 1);
		for (int32 I = 0; I < Segments; ++I)
		{
			const double D = SegmentDistance(Position, H.Points[I], H.Points[(I + 1) % H.Points.Num()]);
			if (D < BestDistance) { BestDistance = D; Best = H.Axis; }
		}
	}
	return Best;
}

bool UOKStudioGame::ApplyGuideDrag(int32 Axis, const FVector2D& Delta)
{
	FOKSceneObject* Object = Document ? Document->Find(SelectedId) : nullptr;
	TArray<FGuideHandle> Handles;
	FVector2D Center;
	double CmPerPixel = 0.;
	if (!Object || !GuideHandles(Handles, Center, CmPerPixel)) return false;
	if (GuideIk >= 0 && GuideBone.IsEmpty() && Object->Kind == EOKSceneObjectKind::Character && Axis < 3)
	{
		// IK target guide: move the target along a world axis (stored in the character's space).
		FOKSceneIkTarget* T = Object->Character.Ik.FindByPredicate([this](const FOKSceneIkTarget& X) { return X.Effector == GuideIk; });
		const AKKPreviewActor* P = SceneActor->PreviewOf(SelectedId);
		if (!T || !P) return false;
		FVector2D N(1., 0.);
		for (const FGuideHandle& H : Handles) if (H.Axis == Axis && !H.bClosed && H.Points.Num() >= 2) N = (H.Points.Last() - H.Points[0]).GetSafeNormal();
		const FVector Move = GuideAxes[Axis] * (FVector2D::DotProduct(Delta, N) * CmPerPixel);
		T->Position += P->GetActorTransform().InverseTransformVector(Move);
		Document->NotifyChanged();
		return true;
	}
	if (!GuideBone.IsEmpty() && Object->Kind == EOKSceneObjectKind::Character)
	{
		// Bone guide: turn the bone about its own local axis and keep that as its FK rotation.
		if (Axis > 2) return false;
		FVector Euler = FVector::ZeroVector;
		if (const FOKSceneFkBone* B = Object->Character.Fk.FindByPredicate([this](const FOKSceneFkBone& F) { return F.Bone == GuideBone; })) Euler = B->Euler;
		else if (const AKKPreviewActor* P = SceneActor->PreviewOf(SelectedId)) P->Session.BoneEuler(GuideBone, Euler);
		const float In[3] = { float(Euler.X), float(Euler.Y), float(Euler.Z) };
		float Turned[3] = {};
		if (ok_unity_euler_rotate(In, Axis, float(Delta.X * 0.5), Turned) != 0) return false;
		const FVector NewEuler(Turned[0], Turned[1], Turned[2]);
		if (FOKSceneFkBone* B = Object->Character.Fk.FindByPredicate([this](const FOKSceneFkBone& F) { return F.Bone == GuideBone; })) B->Euler = NewEuler;
		else Object->Character.Fk.Add({ GuideBone, NewEuler });
		Document->NotifyChanged();
		return true;
	}
	const FTransform ParentWorld = Document->ParentFrame(SelectedId) * SceneActor->GetActorTransform();
	const FTransform World = Document->WorldTransform(SelectedId) * SceneActor->GetActorTransform();
	// Screen direction of the axis handle.
	FVector2D N(1., 0.);
	for (const FGuideHandle& H : Handles) if (H.Axis == Axis && !H.bClosed && H.Points.Num() >= 2) N = (H.Points.Last() - H.Points[0]).GetSafeNormal();
	if (GuideMode == 0 && Axis < 3)
	{
		const FVector Move = GuideAxes[Axis] * (FVector2D::DotProduct(Delta, N) * CmPerPixel);
		Object->Transform.SetLocation(ParentWorld.InverseTransformPosition(World.GetLocation() + Move));
	}
	else if (GuideMode == 1 && Axis < 3)
	{
		const FQuat Turn(GuideAxes[Axis], FMath::DegreesToRadians(Delta.X * 0.5));
		Object->Transform.SetRotation(ParentWorld.GetRotation().Inverse() * (Turn * World.GetRotation()));
	}
	else if (GuideMode == 2)
	{
		FVector Scale = Object->Transform.GetScale3D();
		if (Axis == 3) Scale *= FMath::Max(0.05, 1. + Delta.X * 0.01);
		else Scale[Axis] = FMath::Max(0.01, Scale[Axis] * (1. + FVector2D::DotProduct(Delta, N) * 0.01));
		Object->Transform.SetScale3D(Scale);
	}
	else return false;
	Document->NotifyChanged();
	return true;
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

void UOKStudioGame::Pan(float Right, float Up)
{
	const FRotator R(Pitch, Yaw, 0.f);
	const float Scale = Distance * 0.002f;
	Focus += (FRotationMatrix(R).GetUnitAxis(EAxis::Y) * Right + FRotationMatrix(R).GetUnitAxis(EAxis::Z) * Up) * Scale;
	Document->ActiveCamera = -1;
}

void UOKStudioGame::Zoom(float Steps)
{
	Distance = FMath::Clamp(Distance * FMath::Pow(0.88f, Steps), 20.f, 5000.f);
	Document->ActiveCamera = -1;
}

void UOKStudioGame::UpdateViewer()
{
	if (!Viewer) return;
	UCameraComponent* ViewerCamera = Viewer->GetCameraComponent();
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

FString UOKStudioGame::ExportFrames(float Fps, int32 Width, int32 Height, bool bMp4)
{
	bExportMp4 = bMp4;
	LastMovie.Reset();
	LastMovieError.Reset();
	ExportFps = FMath::Clamp(Fps, 1.f, 120.f);
	ExportWidth = Width; ExportHeight = Height;
	ExportTotal = FMath::Max(1, FMath::CeilToInt(Document->Duration * ExportFps));
	ExportDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("UserData/Studio/movie") / FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S")));
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
	SetTimelineTime(ExportFrame / ExportFps);
	SyncNow();
	for (const FOKSceneObject& O : Document->Objects) if (O.Kind == EOKSceneObjectKind::Character) SceneActor->SetAnimationTime(O.Id, ExportFrame / ExportFps);
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

void UOKStudioGame::Tick(float DeltaTime)
{
	if (bStopRequested) { Stop(); return; }
	if (Viewer && Document) Document->Post.ApplyTo(Viewer->GetCameraComponent()->PostProcessSettings);
	if (Viewer && SceneActor) { SceneActor->ViewLocation = Viewer->GetActorLocation(); SceneActor->bHasViewLocation = true; }
	TickExport();
	if (bTimelinePlaying && Document)
	{
		const float Length = FMath::Max(Document->Duration, 0.01f);
		SetTimelineTime(FMath::Fmod(TimelineTime + DeltaTime, Length));
	}
	if (bSyncPending) SyncNow();
	UpdateViewer();
}

void UOKStudioGame::RunKStudioCompare(const TArray<FString>& Scenes, int32 WaitFrames, bool bExit)
{
	// Each KStudio scene, imported and pictured from its Studio camera after some real frames, beside the picture the
	// original Studio saved in the scene card: Saved/OKStudio/compare/<scene>.png (left original, right OK Studio).
	struct FState { int32 Index = -1; int32 Frame = 0; int32 Done = 0; bool bLoaded = false; bool bPreviousPause = false; };
	TSharedRef<FState> State = MakeShared<FState>();
    State->bPreviousPause = SceneActor && SceneActor->bPauseAnimations;
	TWeakObjectPtr<UOKStudioGame> Self(this);
	// Acceptance runs own their images: another run must not overwrite their evidence.
	FString Out;
	if (!FParse::Value(FCommandLine::Get(), TEXT("OKStudioCompareOutput="), Out) || Out.IsEmpty())
		Out = FPaths::ProjectSavedDir() / TEXT("OKStudio/compare");
	Out = FPaths::ConvertRelativePathToFull(Out);
	IFileManager::Get().MakeDirectory(*Out, true);
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Self, State, Scenes, Out, WaitFrames, bExit](float)
	{
		if (!Self.IsValid()) { if (bExit) FPlatformMisc::RequestExit(false); return false; }
		if (State->bLoaded && ++State->Frame < WaitFrames) return true;
		// Runtime materials still compiling draw nothing yet: wait for them (at most ten times the frames asked for).
		if (State->bLoaded && Self->MaterialsPending() && State->Frame < WaitFrames * 10) return true;
		if (State->bLoaded)
		{
			const FString& Path = Scenes[State->Index];
			const int32 W = 640, H = 360;
			TArray<FColor> Pixels;
			FString Error;
			Self->UpdateViewer();
			const UCameraComponent* Cam = Self->Viewer->GetCameraComponent();
			FImage Original;
			if (FOKSceneCard::Capture(Self->SceneActor, Self->Viewer->GetActorTransform(), Cam->FieldOfView, &Cam->PostProcessSettings, W, H, false, Pixels, Error) && FImageUtils::LoadImage(*Path, Original))
			{
				FImage Left;
				Original.ResizeTo(Left, W, H, ERawImageFormat::BGRA8, EGammaSpace::sRGB);
				TArray<FColor> Side;
				Side.SetNumUninitialized(W * 2 * H);
				const TArrayView64<FColor> L = Left.AsBGRA8();
				for (int32 Y = 0; Y < H; ++Y)
					for (int32 X = 0; X < W; ++X) { Side[Y * W * 2 + X] = L[Y * W + X]; Side[Y * W * 2 + W + X] = Pixels[Y * W + X]; }
				for (FColor& C : Side) C.A = 255;
				TArray64<uint8> Png;
				FImageUtils::PNGCompressImageArray(W * 2, H, TArrayView64<const FColor>(Side.GetData(), Side.Num()), Png);
				const FString Output = Out / (FPaths::GetBaseFilename(Path) + TEXT(".png"));
				if (FFileHelper::SaveArrayToFile(Png, *Output)) ++State->Done;
				else Error = TEXT("cannot write ") + Output;
				// OK_STUDIO_COMPARE_DUMP=<text>: the materials (blend mode, parent) of the mesh components whose name contains it.
				const FString Dump = FPlatformMisc::GetEnvironmentVariable(TEXT("OK_STUDIO_COMPARE_DUMP"));
				if (!Dump.IsEmpty())
					for (const FOKSceneObject& O : Self->Document->Objects)
						if (AKKPreviewActor* Preview = Self->SceneActor->PreviewOf(O.Id))
						{
							TInlineComponentArray<UMeshComponent*> Meshes(Preview);
							for (UMeshComponent* Mesh : Meshes)
								for (int32 Slot = 0; Slot < Mesh->GetNumMaterials(); ++Slot)
									if (UMaterialInterface* M = Mesh->GetMaterial(Slot); M && (O.Name.Contains(Dump) || Mesh->GetName().Contains(Dump)))
										UE_LOG(LogTemp, Display, TEXT("OK studio compare dump: %s %s slot %d %s blend=%d base=%s visible=%d"), *O.Name, *Mesh->GetName(), Slot, *M->GetName(),
											int32(M->GetBlendMode()), *M->GetMaterial()->GetName(), Mesh->IsVisible() ? 1 : 0);
						}
                if (!FPlatformMisc::GetEnvironmentVariable(TEXT("OK_STUDIO_POSE_DUMP")).IsEmpty())
                    for (const FOKSceneObject& O : Self->Document->Objects)
                        if (AKKPreviewActor* P = Self->SceneActor->PreviewOf(O.Id))
                        {
                            UE_LOG(LogTemp, Display, TEXT("OK studio pose: %s world=%s anime=%d/%d/%d time=%.6f"), *O.Name, *P->GetActorTransform().ToString(), O.Character.StudioAnimeGroup, O.Character.StudioAnimeCategory, O.Character.StudioAnimeNo, O.Character.Time);
                            for (const FOKSceneIkTarget& T : O.Character.Ik)
                            {
                                FVector Actual;
                                if (P->Session.IkEffectorPosition(T.Effector, Actual))
                                    UE_LOG(LogTemp, Display, TEXT("OK studio pose: %s effector=%d target=%s actual=%s"), *O.Name, T.Effector, *AKKPreviewActor::ActorLocalToUnity(T.Position).ToString(), *Actual.ToString());
                            }
                            for (const TCHAR* B : { TEXT("cf_j_hips"), TEXT("cf_j_spine03"), TEXT("cf_j_head") })
                            {
                                float M[16];
                                if (P->Session.BoneWorldMatrix(B, M)) UE_LOG(LogTemp, Display, TEXT("OK studio pose: %s bone=%s xyz=(%.6f,%.6f,%.6f)"), *O.Name, B, M[3], M[7], M[11]);
                            }
                        }
				// OK_STUDIO_COMPARE_WIDE=<factor>: the same view pulled back along its direction (what lies outside the frame).
				const float Wide = FCString::Atof(*FPlatformMisc::GetEnvironmentVariable(TEXT("OK_STUDIO_COMPARE_WIDE")));
				if (Wide > 1.f)
				{
					FTransform Back = Self->Viewer->GetActorTransform();
					const FVector Focus = Back.GetLocation() + Back.GetRotation().GetForwardVector() * 200.f;
					Back.SetLocation(Focus - Back.GetRotation().GetForwardVector() * 200.f * Wide);
					TArray<FColor> WidePixels;
					if (FOKSceneCard::Capture(Self->SceneActor, Back, Cam->FieldOfView, &Cam->PostProcessSettings, W, H, false, WidePixels, Error))
					{
						for (FColor& C : WidePixels) C.A = 255;
						TArray64<uint8> WidePng;
						FImageUtils::PNGCompressImageArray(W, H, TArrayView64<const FColor>(WidePixels.GetData(), WidePixels.Num()), WidePng);
						FFileHelper::SaveArrayToFile(WidePng, *(Out / (FPaths::GetBaseFilename(Path) + TEXT("_wide.png"))));
					}
				}
			}
			UE_LOG(LogTemp, Display, TEXT("OK studio compare: %s %s %s (memory %.1f GB)"), *FPaths::GetCleanFilename(Path), Error.IsEmpty() ? TEXT("pictured") : TEXT("failed"), *Error, FPlatformMemory::GetStats().UsedPhysical / double(1 << 30));
		}
		if (++State->Index >= Scenes.Num())
		{
			UE_LOG(LogTemp, Display, TEXT("OK studio compare: RESULT %d / %d pictured into %s"), State->Done, Scenes.Num(), *Out);
            if (Self->SceneActor) Self->SceneActor->bPauseAnimations = State->bPreviousPause;
			if (bExit)
			{
				Self->ReleaseSceneResources();
				UE_LOG(LogTemp, Display, TEXT("OK studio compare: final scene released (memory %.1f GB)"), FPlatformMemory::GetStats().UsedPhysical / double(1 << 30));
				FPlatformMisc::RequestExit(false);
			}
			return false;
		}
		FString LoadError;
		// Remove only this run's named output: a failed import must never leave
		// an older picture looking like a successful comparison.
		IFileManager::Get().Delete(*(Out / (FPaths::GetBaseFilename(Scenes[State->Index]) + TEXT(".png"))), false, true);
		State->bLoaded = Self->Load(Scenes[State->Index], LoadError);
		if (!State->bLoaded)
		{
			UE_LOG(LogTemp, Warning, TEXT("OK studio compare: %s not opened: %s"), *Scenes[State->Index], *LoadError);
			return true;
		}
		// OK_STUDIO_COMPARE_HIDE=<group>/<category>/<no>[,...]: those catalogue items hidden (what they cover).
		TArray<FString> Hide;
		FPlatformMisc::GetEnvironmentVariable(TEXT("OK_STUDIO_COMPARE_HIDE")).ParseIntoArray(Hide, TEXT(","));
		for (FOKSceneObject& O : Self->Document->Objects)
			if (O.Kind == EOKSceneObjectKind::Item && Hide.Contains(FString::Printf(TEXT("%d/%d/%d"), O.Item.Group, O.Item.Category, O.Item.No))) O.bVisible = false;
		Self->SceneActor->bPauseAnimations = true;
        Self->Document->ItemAnimationSeconds = 0.f;
        Self->SyncNow();
		Self->UpdateViewer();
		State->Frame = 0;
		return true;
	}));
}

void UOKStudioGame::RunStressItems(int32 Count, const FString& Item, bool bExit)
{
	TArray<FString> Id;
	Item.ParseIntoArray(Id, TEXT("/"));
	if (Id.Num() != 3) { UE_LOG(LogTemp, Error, TEXT("OK studio stress: item must be group/category/no")); if (bExit) FPlatformMisc::RequestExit(false); return; }
	struct FState { int32 Added = 0; int32 Wait = 0; };
	TSharedRef<FState> State = MakeShared<FState>();
	TWeakObjectPtr<UOKStudioGame> Self(this);
	const int32 G = FCString::Atoi(*Id[0]), C = FCString::Atoi(*Id[1]), N = FCString::Atoi(*Id[2]);
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Self, State, Count, G, C, N, bExit](float)
	{
		if (!Self.IsValid()) { if (bExit) FPlatformMisc::RequestExit(false); return false; }
		if (++State->Wait < 10) return true;
		State->Wait = 0;
		// Live objects by class (what each new copy keeps), and the process' memory.
		auto Live = [](UClass* Class) { int32 K = 0; for (TObjectIterator<UObject> It; It; ++It) if (It->IsA(Class)) ++K; return K; };
		const FPlatformMemoryStats Mem = FPlatformMemory::GetStats();
		UE_LOG(LogTemp, Display, TEXT("OK studio stress: items=%d used=%.2f GB textures=%d materials=%d mids=%d meshes=%d actors=%d"),
			State->Added, Mem.UsedPhysical / double(1 << 30), Live(UTexture2D::StaticClass()), Live(UMaterial::StaticClass()),
			Live(UMaterialInstanceDynamic::StaticClass()), Live(UMeshComponent::StaticClass()), Live(AActor::StaticClass()));
		if (State->Added >= Count) { UE_LOG(LogTemp, Display, TEXT("OK studio stress: RESULT done")); if (bExit) FPlatformMisc::RequestExit(false); return false; }
		for (int32 K = 0; K < 50 && State->Added < Count; ++K, ++State->Added)
		{
			const int32 Obj = Self->Document->AddObject(EOKSceneObjectKind::Item, TEXT("stress"), -1);
			FOKSceneObject* O = Self->Document->Find(Obj);
			O->Item.Group = G; O->Item.Category = C; O->Item.No = N;
			O->Transform = FTransform(FVector((State->Added % 25) * 30.0, (State->Added / 25) * 30.0, 0.0));
		}
		Self->Document->NotifyChanged();
		Self->SyncNow();
		return true;
	}));
}

bool UOKStudioGame::MaterialsPending() const
{
	if (SceneActor && SceneActor->HasPendingBuilds()) return true;
	if (GShaderCompilingManager && GShaderCompilingManager->GetNumRemainingJobs() > 0) return true;
	if (!SceneActor || !Document) return false;
	for (const FOKSceneObject& O : Document->Objects)
		if (const AKKPreviewActor* P = SceneActor->PreviewOf(O.Id); P && !P->PendingPreviewMaterials.IsEmpty()) return true;
	return false;
}

void UOKStudioGame::RunEyeSetTest(bool bExit)
{
	// One character pictured at the face with eyes patterns 0 (default), 23 (swirl gag eyes), 25 (hearts in the
	// pupils) and 34 (right eye squeezed shut, blend shape pattern 23): the gag eye shows only for 23, the pupils hide for it, both
	// special patterns change the face picture, and the face takes each set's blend shape pattern.
	Select(-1);
	const int32 Id = Add(EOKSceneObjectKind::Character);
	Document->Find(Id)->Character.Card = TEXT("Content/CharacterCards/CrowdAPNG/C002.png");
	Document->Find(Id)->Character.EyesPattern = 0;
	struct FState { int32 Frame = 0; int32 Step = 0; TArray<FColor> Plain; TArray<FColor> Swirl; bool bSwirlTaken = false; TArray<FString> Lines; int32 Failures = 0; TOptional<FTransform> View; };
	TSharedRef<FState> State = MakeShared<FState>();
	TWeakObjectPtr<UOKStudioGame> Self(this);
	Document->NotifyChanged();
	SyncNow();
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Self, State, Id, bExit](float)
	{
		if (!Self.IsValid()) { if (bExit) FPlatformMisc::RequestExit(false); return false; }
		const int32 Frame = ++State->Frame;
		if (Frame < 60 || (Self->MaterialsPending() && Frame < 900)) return true;
		static const int32 Patterns[] = { 0, 23, 25, 34 };
		auto Check = [&State](bool bOk, const FString& What) { State->Lines.Add(FString::Printf(TEXT("%s  %s"), bOk ? TEXT("PASS") : TEXT("FAIL"), *What)); if (!bOk) ++State->Failures; };
		AKKPreviewActor* Preview = Self->SceneActor->PreviewOf(Id);
		// The face from the front: the eye in front of the face's centre gives the facing.
		FBox Face(ForceInit), Eye(ForceInit);
		int32 GagShown = 0, PupilShown = 0;
		if (Preview)
		{
			// Parts by their session name (components are named Part_n / Skel_n): shown ones are built. A part with
			// morphs (face, eyes, gag eyes) draws through its mouth slot's skeletal component; its own one is hidden.
			TMap<int32, UPrimitiveComponent*> Drawn;
			for (const AKKPreviewActor::FOkMouthSlot& Slot : Preview->MouthSlots) if (Slot.Comp) Drawn.Add(Slot.SessionPartIndex, Slot.Comp);
			for (const FKKLoadedPart& Part : Preview->Parts)
			{
				OkMeshView View;
				FMemory::Memzero(View);
				if (Part.SessionIndex == INDEX_NONE || !Preview->Session.PartAt(Part.SessionIndex, View) || !View.name || !View.render_visible) continue;
				const FString N = FString(UTF8_TO_TCHAR(View.name)).ToLower();
				UPrimitiveComponent* Comp = Drawn.FindRef(Part.SessionIndex);
				if (!Comp) Comp = Part.Comp;
				const FBox Bounds = Comp ? Comp->Bounds.GetBox() : FBox(ForceInit);
				if (N == TEXT("cf_o_face")) Face += Bounds;
				// Both eye whites: their middle is in front of the face's centre.
				if (N.StartsWith(TEXT("cf_ohitomi_")) && !N.Contains(TEXT("02"))) Eye += Bounds;
				if (N.Contains(TEXT("gag_eye")))
				{
					++GagShown;
					UMaterialInterface* M = Comp ? Comp->GetMaterial(0) : nullptr;
					UE_LOG(LogTemp, Display, TEXT("OK studio eye set test: gag part %s material %s (base %s)"), *N, M ? *M->GetName() : TEXT("none"),
						M && M->GetBaseMaterial() ? *M->GetBaseMaterial()->GetName() : TEXT("none"));
				}
				if (N.StartsWith(TEXT("cf_ohitomi_")) && N.Contains(TEXT("02"))) ++PupilShown;
			}
			UE_LOG(LogTemp, Display, TEXT("OK studio eye set test: eyes %d parts=%d eye patterns=%d face=%d"), Patterns[State->Step], Preview->Parts.Num(), Preview->GetFacePatternCount(1), Preview->FacePattern[0]);
		}
		TArray<FColor> Pixels;
		// The view is found once, with the default eyes (an eye set may hide the eye whites).
		if (!State->View && Face.IsValid && Eye.IsValid)
		{
			const FVector Front = (Eye.GetCenter() - Face.GetCenter()).GetSafeNormal2D();
			const FVector Target = Eye.GetCenter();
			State->View = FTransform(FRotationMatrix::MakeFromX(-Front).ToQuat(), Target + Front * 45.f);
		}
		if (State->View)
		{
			FString Error;
			FOKSceneCard::Capture(Self->SceneActor, *State->View, 40.f, &Self->Viewer->GetCameraComponent()->PostProcessSettings, 320, 180, false, Pixels, Error);
			TArray64<uint8> Png;
			if (Pixels.Num() == 320 * 180)
			{
				FImageUtils::PNGCompressImageArray(320, 180, TArrayView64<const FColor>(Pixels.GetData(), Pixels.Num()), Png);
				FFileHelper::SaveArrayToFile(Png, *(FPaths::ProjectSavedDir() / FString::Printf(TEXT("OKStudio/eyeset_%d.png"), Patterns[State->Step])));
			}
		}
		auto Differs = [&State, &Pixels]() { int32 N = 0; for (int32 K = 0; K < Pixels.Num() && K < State->Plain.Num(); ++K) { const FColor& A = State->Plain[K]; const FColor& B = Pixels[K]; N += FMath::Abs(A.R - B.R) + FMath::Abs(A.G - B.G) + FMath::Abs(A.B - B.B) > 60 ? 1 : 0; } return N; };
		const int32 Ptn = Patterns[State->Step];
		const int32 Blend = FOKStudioCatalog::Get().EyeSet(Ptn).BlendPattern;
		if (Ptn == 0) { State->Plain = Pixels; Check(Pixels.Num() > 0 && GagShown == 0 && PupilShown > 0, FString::Printf(TEXT("eyes 0: face pictured, gag eyes %d, pupils %d"), GagShown, PupilShown)); }
		else if (Ptn == 23 && !State->bSwirlTaken)
		{
			// The swirl turns (its gag material's _AngleSpeed -0.5): pictured again a second later. Taken once even when
			// the picture failed (no view), so the test ends with a failure instead of retrying forever.
			State->bSwirlTaken = true;
			State->Swirl = Pixels;
			const int32 D = Differs();
			Check(GagShown == 1 && PupilShown == 0 && D > 50, FString::Printf(TEXT("eyes 23 (swirl): gag eyes %d, pupils %d, %d pixels changed"), GagShown, PupilShown, D));
			State->Frame = 50;
			return true;
		}
		else if (Ptn == 23)
		{
			int32 N = 0;
			for (int32 K = 0; K < Pixels.Num() && K < State->Swirl.Num(); ++K)
				N += FMath::Abs(State->Swirl[K].R - Pixels[K].R) + FMath::Abs(State->Swirl[K].G - Pixels[K].G) + FMath::Abs(State->Swirl[K].B - Pixels[K].B) > 30 ? 1 : 0;
			// Half a turn changes the swirls; hair sway alone changes about 150 pixels of this picture.
			Check(N > 500, FString::Printf(TEXT("eyes 23 (swirl): turns, %d pixels changed in a second"), N));
			// eyeset_23.png is the second picture; the first one beside it.
			TArray64<uint8> First;
			if (State->Swirl.Num() == 320 * 180)
			{
				FImageUtils::PNGCompressImageArray(320, 180, TArrayView64<const FColor>(State->Swirl.GetData(), State->Swirl.Num()), First);
				FFileHelper::SaveArrayToFile(First, *(FPaths::ProjectSavedDir() / TEXT("OKStudio/eyeset_23_first.png")));
			}
		}
		else if (Ptn == 25) { const int32 D = Differs(); Check(GagShown == 0 && PupilShown > 0 && D > 10, FString::Printf(TEXT("eyes 25 (hearts in the pupils): gag eyes %d, %d pixels changed"), GagShown, D)); }
		else Check(Blend == 23 && Preview && Preview->FacePattern[0] == 23, FString::Printf(TEXT("eyes 34 (right eye squeezed shut): blend shape pattern %d, face %d"), Blend, Preview ? Preview->FacePattern[0] : -1));
		if (State->Step + 1 < UE_ARRAY_COUNT(Patterns))
		{
			++State->Step;
			State->Frame = 0;
			Self->Document->Find(Id)->Character.EyesPattern = Patterns[State->Step];
			Self->Document->NotifyChanged();
			Self->SyncNow();
			return true;
		}
		for (const FString& L : State->Lines) UE_LOG(LogTemp, Display, TEXT("OK studio eye set test: %s"), *L);
		UE_LOG(LogTemp, Display, TEXT("OK studio eye set test: RESULT %s"), State->Failures ? TEXT("FAIL") : TEXT("PASS"));
		if (bExit) FPlatformMisc::RequestExit(false);
		return false;
	}), 0.1f);
}

void UOKStudioGame::RunSkyTest(bool bExit)
{
	// No map, no background: each sky pattern pictured looking up and towards the horizon, then with the sky off.
	// Every pattern draws (the patterns differ), the day sky is blue above, the night sky dark, and with the sky off
	// the view changes (the dome is gone). Last a map with its own sky (MapSkyInfo, map 50) with the scene's sky off:
	// the dome shows the map's sky.
	Select(-1);
	Document->Map = -1;
	Document->Post.Background.Reset();
	Document->Post.BackgroundColor = FLinearColor(0.f, 0.f, 0.f, -1.f);
	struct FState { int32 Frame = 0; int32 Step = 0; TArray<TArray<FColor>> Ups; TArray<FString> Lines; int32 Failures = 0; };
	TSharedRef<FState> State = MakeShared<FState>();
	TWeakObjectPtr<UOKStudioGame> Self(this);
	auto Show = [Self](int32 Step)
	{
		const TArray<FOKStudioSkyPattern>& Patterns = OKStudioSkyPatterns();
		FOKScenePost& P = Self->Document->Post;
		P.bSky = Patterns.IsValidIndex(Step);
		if (P.bSky) P.SkyPattern = Patterns[Step].No;
		Self->Document->NotifyChanged();
		Self->SyncNow();
		// The dome takes the pattern now; its material compiles and its render state updates before the pictures.
		Self->SceneActor->PlaceBackground(FTransform(FVector(0.f, 0.f, 160.f)), 70.f);
	};
	Show(0);
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Self, State, Show, bExit](float)
	{
		if (!Self.IsValid()) { if (bExit) FPlatformMisc::RequestExit(false); return false; }
		const TArray<FOKStudioSkyPattern>& Patterns = OKStudioSkyPatterns();
		auto Check = [&State](bool bOk, const FString& What) { State->Lines.Add(FString::Printf(TEXT("%s  %s"), bOk ? TEXT("PASS") : TEXT("FAIL"), *What)); if (!bOk) ++State->Failures; };
		if (State->Step == 0 && State->Frame == 0) Check(Patterns.Num() >= 4, FString::Printf(TEXT("%d sky patterns in the library"), Patterns.Num()));
		if (++State->Frame < 30 || (Self->MaterialsPending() && State->Frame < 1200)) return true;
		if (State->Step == Patterns.Num() + 1)
		{
			int32 Want = -1;
			const bool bHas = OKStudioMapSky(50, Self->Document->Post.SunType, Want);
			Self->SceneActor->PlaceBackground(FTransform(FVector(0.f, 0.f, 160.f)), 70.f);
			const int32 Shown = Self->SceneActor->ShownSkyPattern();
			Check(bHas && Want >= 1000 && Shown == Want, FString::Printf(TEXT("map 50 (scene sky off): the dome shows the map's own sky (pattern %d, shown %d, map %s)"),
				Want, Shown, Self->SceneActor->MapError.IsEmpty() ? TEXT("shown") : *Self->SceneActor->MapError));
			Self->Document->Map = -1;
			Self->Document->NotifyChanged();
			Self->SyncNow();
			for (const FString& L : State->Lines) UE_LOG(LogTemp, Display, TEXT("OK studio sky test: %s"), *L);
			UE_LOG(LogTemp, Display, TEXT("OK studio sky test: RESULT %s"), State->Failures ? TEXT("FAIL") : TEXT("PASS"));
			if (bExit) FPlatformMisc::RequestExit(false);
			return false;
		}
		const bool bOff = !Patterns.IsValidIndex(State->Step);
		const FString Label = bOff ? FString(TEXT("off")) : FString::Printf(TEXT("%d %s"), Patterns[State->Step].No, *Patterns[State->Step].Name);
		const FVector Eye(0.f, 0.f, 160.f);
		TArray<FColor> Up, Horizon;
		FString Error;
		for (TPair<TArray<FColor>*, float> V : { TPair<TArray<FColor>*, float>(&Up, 60.f), TPair<TArray<FColor>*, float>(&Horizon, 8.f) })
		{
			// The dome follows the view it is placed for.
			const FTransform View(FRotator(V.Value, 30.f, 0.f), Eye);
			Self->SceneActor->PlaceBackground(View, 70.f);
			FOKSceneCard::Capture(Self->SceneActor, View, 70.f, &Self->Viewer->GetCameraComponent()->PostProcessSettings, 320, 180, false, *V.Key, Error);
			TArray64<uint8> Png;
			if (V.Key->Num() == 320 * 180)
			{
				FImageUtils::PNGCompressImageArray(320, 180, TArrayView64<const FColor>(V.Key->GetData(), V.Key->Num()), Png);
				FFileHelper::SaveArrayToFile(Png, *(FPaths::ProjectSavedDir() / FString::Printf(TEXT("OKStudio/sky_%s_%s.png"),
					bOff ? TEXT("off") : *FString::FromInt(Patterns[State->Step].No), V.Value > 30.f ? TEXT("up") : TEXT("horizon"))));
			}
		}
		auto Mean = [](const TArray<FColor>& Px) { FLinearColor M(0, 0, 0, 0); for (const FColor& C : Px) M += FLinearColor(C.R, C.G, C.B, 0); return Px.Num() ? M / float(Px.Num()) : M; };
		const FLinearColor U = Mean(Up), H = Mean(Horizon);
		const FString Values = FString::Printf(TEXT("up mean (%.0f,%.0f,%.0f), horizon mean (%.0f,%.0f,%.0f)"), U.R, U.G, U.B, H.R, H.G, H.B);
		if (bOff)
		{
			Check(Up.Num() == 320 * 180 && State->Ups.Num() > 0 && Up != State->Ups.Last(), TEXT("sky off: the view changes; ") + Values);
			Self->Document->Map = 50;
			Self->Document->NotifyChanged();
			Self->SyncNow();
			++State->Step;
			State->Frame = 0;
		}
		else
		{
			Check(Up.Num() == 320 * 180 && Horizon.Num() == 320 * 180, FString::Printf(TEXT("sky %s: pictured; %s"), *Label, *Values));
			if (Patterns[State->Step].No == 1) Check(U.B > U.R + 30.f, FString::Printf(TEXT("sky %s: blue above"), *Label));
			if (Patterns[State->Step].No == 3 && State->Ups.Num() > 1)
			{
				// Night: darker above than the day, and its moon (MOON_CUSTOM_TEXTURE) bright where _MoonPosition points.
				const FLinearColor Day = Mean(State->Ups[1]);
				Check(U.R + U.G + U.B < Day.R + Day.G + Day.B - 20.f, FString::Printf(TEXT("sky %s: darker above than the day"), *Label));
				FVector Moon;
				TArray<FColor> At;
				if (OKStudioSkyBodyDirection(3, TEXT("_MoonPosition"), Moon))
				{
					const FTransform View(Moon.Rotation(), Eye);
					Self->SceneActor->PlaceBackground(View, 20.f);
					FOKSceneCard::Capture(Self->SceneActor, View, 20.f, &Self->Viewer->GetCameraComponent()->PostProcessSettings, 320, 180, false, At, Error);
				}
				const FLinearColor M = Mean(At);
				const FColor C = At.Num() == 320 * 180 ? At[90 * 320 + 160] : FColor::Black;
				if (At.Num() == 320 * 180)
				{
					TArray64<uint8> Png;
					FImageUtils::PNGCompressImageArray(320, 180, TArrayView64<const FColor>(At.GetData(), At.Num()), Png);
					FFileHelper::SaveArrayToFile(Png, *(FPaths::ProjectSavedDir() / TEXT("OKStudio/sky_3_moon.png")));
				}
				Check(C.R + C.G + C.B > M.R + M.G + M.B + 60.f, FString::Printf(TEXT("sky %s: the moon at _MoonPosition (centre %d,%d,%d, mean %.0f,%.0f,%.0f)"), *Label, C.R, C.G, C.B, M.R, M.G, M.B));
			}
			for (int32 K = 0; K < State->Ups.Num(); ++K)
			{
				const FLinearColor O = Mean(State->Ups[K]);
				Check(FMath::Abs(O.R - U.R) + FMath::Abs(O.G - U.G) + FMath::Abs(O.B - U.B) > 6.f, FString::Printf(TEXT("sky %s differs from pattern %d"), *Label, Patterns[K].No));
			}
			State->Ups.Add(Up);
			++State->Step;
			State->Frame = 0;
			Show(State->Step);
		}
		return true;
	}), 0.1f);
}

void UOKStudioGame::RunPostTest(bool bExit)
{
	// One character, no map, the day sky. Each effect pictured off and on from its own view: fog (red) on the character
	// 40 m away (Unity Exp2 0.01: about 15 % fog there), ambient occlusion (black tint, 0.3 m) on her up close, and sun
	// shafts (white) looking at the sky past her head.
	Select(-1);
	Document->Map = -1;
	Document->Post.Background.Reset();
	Document->Post.bSky = true;
	Document->Post.SkyPattern = 1;
	const int32 Id = Add(EOKSceneObjectKind::Character);
	Document->Find(Id)->Character.Card = TEXT("Content/CharacterCards/CrowdAPNG/C002.png");
	Document->NotifyChanged();
	SyncNow();
	struct FState { int32 Frame = 0; bool bWarm = false; int32 WarmShots = 0; TArray<FString> Lines; int32 Failures = 0; };
	TSharedRef<FState> State = MakeShared<FState>();
	TWeakObjectPtr<UOKStudioGame> Self(this);
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Self, State, bExit](float)
	{
		if (!Self.IsValid()) { if (bExit) FPlatformMisc::RequestExit(false); return false; }
		if (++State->Frame < 60 || (Self->MaterialsPending() && State->Frame < 1200)) return true;
		// The post material is made the first time an effect is on; its shaders compile before the pictures.
		if (!State->bWarm)
		{
			State->bWarm = true;
			State->Frame = 30;
			FPostProcessSettings Warm;
			Self->Document->Post.bFog = true;
			Self->Document->Post.ApplyTo(Warm);
			Self->Document->Post.bFog = false;
			return true;
		}
		const FString PostState = OKStudioScenePostMaterialState();
		if (PostState != TEXT("ready") && State->Frame < 1800)
		{
			FPostProcessSettings Warm;
			Self->Document->Post.bFog = true;
			Self->Document->Post.ApplyTo(Warm);
			Self->Document->Post.bFog = false;
			if (State->Frame % 100 == 0) UE_LOG(LogTemp, Display, TEXT("OK studio post test: post material %s"), *PostState);
			return true;
		}
		// A few throwaway pictures with every effect on: the material instance gets its render state before the checks.
		if (State->WarmShots < 10)
		{
			if (State->WarmShots++ == 0) UE_LOG(LogTemp, Display, TEXT("OK studio post test: post material %s after %d frames"), *PostState, State->Frame);
			FOKScenePost& P = Self->Document->Post;
			P.bFog = P.bAmbientOcclusion = P.bSunShafts = true;
			FPostProcessSettings Settings = Self->Viewer->GetCameraComponent()->PostProcessSettings;
			P.ApplyTo(Settings);
			P.bFog = P.bAmbientOcclusion = P.bSunShafts = false;
			TArray<FColor> Pixels;
			FString Error;
			FOKSceneCard::Capture(Self->SceneActor, FTransform(FVector(-300.f, 0.f, 100.f)), 60.f, &Settings, 64, 36, false, Pixels, Error);
			return true;
		}
		auto Check = [&State](bool bOk, const FString& What) { State->Lines.Add(FString::Printf(TEXT("%s  %s"), bOk ? TEXT("PASS") : TEXT("FAIL"), *What)); if (!bOk) ++State->Failures; };
		FOKScenePost& Post = Self->Document->Post;
		auto Picture = [&](const FTransform& View, float Fov, const TCHAR* Name)
		{
			Self->SceneActor->PlaceBackground(View, Fov);
			FPostProcessSettings Settings = Self->Viewer->GetCameraComponent()->PostProcessSettings;
			Post.ApplyTo(Settings);
			TArray<FColor> Pixels;
			FString Error;
			FOKSceneCard::Capture(Self->SceneActor, View, Fov, &Settings, 320, 180, false, Pixels, Error);
			TArray64<uint8> Png;
			if (Pixels.Num() == 320 * 180)
			{
				FImageUtils::PNGCompressImageArray(320, 180, TArrayView64<const FColor>(Pixels.GetData(), Pixels.Num()), Png);
				FFileHelper::SaveArrayToFile(Png, *(FPaths::ProjectSavedDir() / FString::Printf(TEXT("OKStudio/post_%s.png"), Name)));
			}
			FLinearColor M(0, 0, 0, 0);
			for (const FColor& C : Pixels) M += FLinearColor(C.R, C.G, C.B, 0);
			return Pixels.Num() ? M / float(Pixels.Num()) : M;
		};
		// The pictures need a frame for a sky or setting change to reach the renderer: the off and on pictures are taken
		// with the same view right after one another; the effect is in the post-process material (no render state).
		const FTransform Far(FRotator(0.f, 0.f, 0.f), FVector(-4000.f, 0.f, 100.f));
		Post.bFog = false;
		const FLinearColor FogOff = Picture(Far, 4.f, TEXT("fog_off"));
		Post.bFog = true;
		Post.FogColor = FLinearColor(1.f, 0.f, 0.f, 1.f);
		const FLinearColor FogOn = Picture(Far, 4.f, TEXT("fog_on"));
		Post.bFog = false;
		Check(FogOn.R > FogOff.R + 3.f && FogOn.G < FogOff.G, FString::Printf(TEXT("fog: red rises (%.0f -> %.0f), green falls (%.0f -> %.0f)"), FogOff.R, FogOn.R, FogOff.G, FogOn.G));
		const FTransform Near(FRotator(-10.f, 0.f, 0.f), FVector(-120.f, 0.f, 140.f));
		Post.bAmbientOcclusion = false;
		const FLinearColor AoOff = Picture(Near, 40.f, TEXT("ao_off"));
		Post.bAmbientOcclusion = true;
		Post.AmbientOcclusionTint = FLinearColor::Black;
		Post.AmbientOcclusionRadius = 0.3f;
		const FLinearColor AoOn = Picture(Near, 40.f, TEXT("ao_on"));
		Post.bAmbientOcclusion = false;
		Check(AoOn.R + AoOn.G + AoOn.B < AoOff.R + AoOff.G + AoOff.B - 3.f, FString::Printf(TEXT("ambient occlusion: darker (%.0f -> %.0f)"), AoOff.R + AoOff.G + AoOff.B, AoOn.R + AoOn.G + AoOn.B));
		const FTransform Up(FRotator(35.f, 0.f, 0.f), FVector(-150.f, 0.f, 120.f));
		Post.bSunShafts = false;
		const FLinearColor ShaftOff = Picture(Up, 60.f, TEXT("shafts_off"));
		Post.bSunShafts = true;
		Post.SunShaftColor = FLinearColor::White;
		Post.SunShaftThreshold = FLinearColor(0.2f, 0.2f, 0.2f, 1.f);
		const FLinearColor ShaftOn = Picture(Up, 60.f, TEXT("shafts_on"));
		Post.bSunShafts = false;
		Check(ShaftOn.R + ShaftOn.G + ShaftOn.B > ShaftOff.R + ShaftOff.G + ShaftOff.B + 3.f, FString::Printf(TEXT("sun shafts: brighter (%.0f -> %.0f)"), ShaftOff.R + ShaftOff.G + ShaftOff.B, ShaftOn.R + ShaftOn.G + ShaftOn.B));
		for (const FString& L : State->Lines) UE_LOG(LogTemp, Display, TEXT("OK studio post test: %s"), *L);
		UE_LOG(LogTemp, Display, TEXT("OK studio post test: RESULT %s"), State->Failures ? TEXT("FAIL") : TEXT("PASS"));
		if (bExit) FPlatformMisc::RequestExit(false);
		return false;
	}), 0.1f);
}

void UOKStudioGame::RunPatternTest(bool bExit)
{
	// An item with a pattern slot, pictured plain and then with pattern 1 (stripes) in red: the patterned picture must
	// differ where the item is, and every pattern parameter must have reached its material (no unapplied overrides).
	const FOKStudioCatalogItem* Patterned = FOKStudioCatalog::Get().Items.FindByPredicate([](const FOKStudioCatalogItem& I) { return I.Patterns[0] && I.Colors[0] && I.Group == 0; });
	if (!Patterned) Patterned = FOKStudioCatalog::Get().Items.FindByPredicate([](const FOKStudioCatalogItem& I) { return I.Patterns[0]; });
	if (!Patterned) { UE_LOG(LogTemp, Display, TEXT("OK studio pattern test: RESULT FAIL no patterned catalogue item")); if (bExit) FPlatformMisc::RequestExit(false); return; }
	Select(-1);
	const int32 Id = Add(EOKSceneObjectKind::Item);
	FOKSceneItem& Item = Document->Find(Id)->Item;
	Item.Group = Patterned->Group; Item.Category = Patterned->Category; Item.No = Patterned->No;
	Item.Colors = { FLinearColor(1.f, 1.f, 1.f, 1.f) };
	Document->Find(Id)->Transform = FTransform(FRotator(0.f, 0.f, 0.f), FVector(0.f, 400.f, 100.f), FVector(3.f));
	Focus = FVector(0.f, 400.f, 110.f); Yaw = 90.f; Pitch = -15.f; Distance = 260.f;
	struct FState { int32 Frame = 0; int32 Step = 0; int32 ColoredChanged = 0; int32 ColoredRed = 0; TArray<FColor> Plain; };
	TSharedRef<FState> State = MakeShared<FState>();
	TWeakObjectPtr<UOKStudioGame> Self(this);
	const FString Name = Patterned->Name;
	Document->NotifyChanged();
	SyncNow();
	UpdateViewer();
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Self, State, Id, Name, bExit](float)
	{
		if (!Self.IsValid()) { if (bExit) FPlatformMisc::RequestExit(false); return false; }
		// Three pictures, each once the rebuilt item's runtime materials have compiled: plain (white), main colour red,
		// then white with pattern 1 in red.
		const int32 Frame = ++State->Frame;
		const bool bCompiling = Self->MaterialsPending();
		if (Frame < 60 || (bCompiling && Frame < 900)) return true;
		TArray<FColor> Pixels;
		FString Error;
		Self->UpdateViewer();
		const UCameraComponent* Cam = Self->Viewer->GetCameraComponent();
		FOKSceneCard::Capture(Self->SceneActor, Self->Viewer->GetActorTransform(), Cam->FieldOfView, &Cam->PostProcessSettings, 320, 180, false, Pixels, Error);
		static const TCHAR* Shots[] = { TEXT("plain"), TEXT("colored"), TEXT("striped") };
		TArray64<uint8> Png;
		if (Pixels.Num() == 320 * 180)
		{
			FImageUtils::PNGCompressImageArray(320, 180, TArrayView64<const FColor>(Pixels.GetData(), Pixels.Num()), Png);
			FFileHelper::SaveArrayToFile(Png, *(FPaths::ProjectSavedDir() / FString::Printf(TEXT("OKStudio/pattern_%s.png"), Shots[State->Step])));
		}
		// Pixels that differ from the plain picture, and how many of them are red.
		auto Red = [&State](const TArray<FColor>& Shot, int32& Changed)
		{
			int32 Reddish = 0;
			Changed = 0;
			for (int32 K = 0; K < Shot.Num() && K < State->Plain.Num(); ++K)
			{
				const FColor& A = State->Plain[K];
				const FColor& B = Shot[K];
				if (FMath::Abs(A.R - B.R) + FMath::Abs(A.G - B.G) + FMath::Abs(A.B - B.B) > 60) { ++Changed; Reddish += B.R > B.G + 40 && B.R > B.B + 40 ? 1 : 0; }
			}
			return Reddish;
		};
		FOKSceneItem& Shown = Self->Document->Find(Id)->Item;
		if (State->Step == 0)
		{
			State->Plain = Pixels;
			Shown.Colors = { FLinearColor(1.f, 0.f, 0.f, 1.f) };
		}
		else if (State->Step == 1)
		{
			State->ColoredRed = Red(Pixels, State->ColoredChanged);
			FOKSceneItemPattern P;
			P.Pattern = 1;
			P.Color = FLinearColor(1.f, 0.f, 0.f, 1.f);
			P.Uv = FVector4(0., 0., 4., 4.);
			Shown.Colors = { FLinearColor(1.f, 1.f, 1.f, 1.f) };
			Shown.Patterns = { P };
		}
		if (State->Step < 2)
		{
			++State->Step;
			State->Frame = 0;
			Self->Document->NotifyChanged();
			Self->SyncNow();
			return true;
		}
		int32 Changed = 0;
		const int32 Reddish = Red(Pixels, Changed);
		TArray<FString> Unapplied;
		if (AKKPreviewActor* Preview = Self->SceneActor->PreviewOf(Id))
		{
			Preview->ApplyMaterialOverrides(&Unapplied);
			// What the item's materials do take, for a failure report.
			if (!Unapplied.IsEmpty())
			{
				TInlineComponentArray<UMeshComponent*> Meshes(Preview);
				for (UMeshComponent* Mesh : Meshes)
					for (int32 Slot = 0; Slot < Mesh->GetNumMaterials(); ++Slot)
						if (UMaterialInterface* M = Mesh->GetMaterial(Slot))
						{
							TArray<FMaterialParameterInfo> Infos, More;
							TArray<FGuid> Ids;
							M->GetAllTextureParameterInfo(Infos, Ids);
							M->GetAllVectorParameterInfo(More, Ids); Infos.Append(More);
							M->GetAllScalarParameterInfo(More, Ids); Infos.Append(More);
							TArray<FString> Names;
							for (const FMaterialParameterInfo& I : Infos) Names.Add(I.Name.ToString());
							UE_LOG(LogTemp, Display, TEXT("OK studio pattern test: %s slot %d material %s (%s) parameters [%s]"), *Mesh->GetName(), Slot, *M->GetName(), *M->GetMaterial()->GetName(), *FString::Join(Names, TEXT(" ")));
						}
			}
		}
		const bool bOk = State->ColoredRed > 200 && Changed > 200 && Reddish > 100 && Unapplied.IsEmpty();
		UE_LOG(LogTemp, Display, TEXT("OK studio pattern test: RESULT %s item %s: main colour red %d of %d changed; pattern %d pixels changed, %d red; unapplied [%s]"), bOk ? TEXT("PASS") : TEXT("FAIL"), *Name, State->ColoredRed, State->ColoredChanged, Changed, Reddish, *FString::Join(Unapplied, TEXT(", ")));
		Self->Document->RemoveObject(Id);
		Self->Document->NotifyChanged();
		if (bExit) FPlatformMisc::RequestExit(false);
		return false;
	}));
}

void UOKStudioGame::RunEffectColorTest(bool bExit)
{
	// The effect colour reaches the particles (User.Color, linked by OKStudio.MakeEffects rewire). The particles are
	// simulated and drawn over real frames (they reach the renderer at the end of a frame), red then blue, and the
	// bright pixels of a picture away from everything else must follow.
	// A looping template keeps its earlier particles a while, so each colour gets a fresh effect object.
	Focus = FVector(0.f, 400.f, 150.f); Yaw = 90.f; Pitch = 0.f; Distance = 250.f;
	struct FState { int32 Frame = 0; int32 Fx = -1; double R[2] = { 0, 0 }; double B[2] = { 0, 0 }; int32 Lit[2] = { 0, 0 }; };
	TSharedRef<FState> State = MakeShared<FState>();
	TWeakObjectPtr<UOKStudioGame> Self(this);
	auto Paint = [Self, State](const FLinearColor& Color)
	{
		if (!Self.IsValid()) return;
		if (State->Fx >= 0) Self->Document->RemoveObject(State->Fx);
		Self->Select(-1);
		State->Fx = Self->Add(EOKSceneObjectKind::Effect);
		FOKSceneObject* O = Self->Document->Find(State->Fx);
		O->Effect.Effect = TEXT("fireball");
		O->Effect.Color = Color;
		O->Transform.SetLocation(FVector(0.f, 400.f, 150.f));
		Self->Document->NotifyChanged();
		Self->SyncNow();
		Self->UpdateViewer();
	};
	Paint(FLinearColor(1.f, 0.f, 0.f, 1.f));
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Self, State, bExit, Paint](float)
	{
		if (!Self.IsValid()) { if (bExit) FPlatformMisc::RequestExit(false); return false; }
		const int32 Frame = ++State->Frame;
		if (Frame != 90 && Frame != 180) return true;
		const int32 Shot = Frame == 90 ? 0 : 1;
		TArray<FColor> Pixels;
		FString Error;
		Self->UpdateViewer();
		if (FOKSceneCard::Capture(Self->SceneActor, Self->Viewer->GetActorTransform(), 40.f, &Self->Viewer->GetCameraComponent()->PostProcessSettings, 320, 180, false, Pixels, Error))
		{
			for (const FColor& P : Pixels) if (FMath::Max3(P.R, P.G, P.B) > 60) { State->R[Shot] += P.R; State->B[Shot] += P.B; ++State->Lit[Shot]; }
			TArray64<uint8> Png;
			FImageUtils::PNGCompressImageArray(320, 180, TArrayView64<const FColor>(Pixels.GetData(), Pixels.Num()), Png);
			FFileHelper::SaveArrayToFile(Png, *(FPaths::ProjectSavedDir() / FString::Printf(TEXT("OKStudio/effect_color_%s.png"), Shot ? TEXT("blue") : TEXT("red"))));
		}
		if (Shot == 0) { Paint(FLinearColor(0.f, 0.f, 1.f, 1.f)); return true; }
		const bool bOk = State->Lit[0] > 20 && State->Lit[1] > 20 && State->R[0] > State->B[0] * 1.5 && State->B[1] > State->R[1] * 1.5;
		UE_LOG(LogTemp, Display, TEXT("OK studio effect colour test: RESULT %s red %d lit (R %.0f / B %.0f), blue %d lit (R %.0f / B %.0f)"), bOk ? TEXT("PASS") : TEXT("FAIL"),
			State->Lit[0], State->R[0], State->B[0], State->Lit[1], State->R[1], State->B[1]);
		Self->Document->RemoveObject(State->Fx);
		Self->Document->NotifyChanged();
		if (bExit) FPlatformMisc::RequestExit(false);
		return false;
	}));
}

void UOKStudioGame::RunSelfTest(bool bExit)
{
	TArray<FString> Lines;
	int32 Failures = 0;
	auto Step = [](const TCHAR* What) { UE_LOG(LogTemp, Display, TEXT("OK studio game selftest step: %s"), What); GLog->Flush(); };
	auto Check = [&](bool bOk, const FString& What)
	{
		Lines.Add(FString::Printf(TEXT("%s  %s"), bOk ? TEXT("PASS") : TEXT("FAIL"), *What));
		if (!bOk) ++Failures;
	};
	Check(Panel.IsValid(), TEXT("studio panel in the game viewport"));
	Check(Document->Objects.Num() == 1 && Document->Objects[0].Kind == EOKSceneObjectKind::Light, TEXT("a new scene starts with a key light"));
	Check(AvailableCards().Num() >= 100, FString::Printf(TEXT("card picker lists %d cards"), AvailableCards().Num()));
	Step(TEXT("add folder and character"));
	const int32 Folder = Add(EOKSceneObjectKind::Folder);
	const int32 A = Add(EOKSceneObjectKind::Character);
	Document->Find(A)->Character.Card = TEXT("Content/CharacterCards/CrowdAPNG/C002.png");
	Document->Find(A)->Character.ClothesStates[1] = 1;
	Document->Find(A)->Character.EyesPattern = 2;
	Select(Folder);
	const int32 Item = Add(EOKSceneObjectKind::Item);
	Document->Find(Item)->Item.Model = TEXT("447f3c180000000045cc17684f4b4331");
	Document->Find(Item)->Transform.SetLocation(FVector(0.f, 60.f, 120.f));
	const int32 Cam = Add(EOKSceneObjectKind::Camera);
	Step(TEXT("sync"));
	SyncNow();
	Step(TEXT("synced"));
	const AKKPreviewActor* PA = SceneActor->PreviewOf(A);
	const AKKPreviewActor* PItem = SceneActor->PreviewOf(Item);
	Check(Document->Find(A)->Parent == Folder && Document->Find(Item)->Parent == Folder, TEXT("objects added into the selected folder"));
	Check(PA && PA->Session.PartCount() > 0, FString::Printf(TEXT("character (OK card C002): %d parts"), PA ? PA->Session.PartCount() : 0));
	Check(PItem && PItem->Session.PartCount() > 0, FString::Printf(TEXT("item (library model): %d parts"), PItem ? PItem->Session.PartCount() : 0));
	Check(SceneActor->Errors.Num() == 0, FString::Printf(TEXT("no object errors (%d)"), SceneActor->Errors.Num()));
	// Character state (R8): tears level 2 shows the medium tears, every accessory slot switched off hides them all.
	Step(TEXT("character state"));
	{
		auto Count = [&](int32& Tears, int32& Accessories)
		{
			Tears = Accessories = 0;
			const AKKPreviewActor* P = SceneActor->PreviewOf(A);
			for (int32 Part = 0; P && Part < P->Session.PartCount(); ++Part)
			{
				OkMeshView V;
				if (!P->Session.PartAt(Part, V) || !V.render_visible) continue;
				const FString Name = UTF8_TO_TCHAR(V.name ? V.name : ""), Role = UTF8_TO_TCHAR(V.role ? V.role : "");
				Tears += Name.Contains(TEXT("cf_O_namida_M")) ? 1 : 0;
				Accessories += Role.Contains(TEXT("acc")) ? 1 : 0;
			}
		};
		int32 Tears0 = 0, Acc0 = 0, Tears1 = 0, Acc1 = 0;
		Count(Tears0, Acc0);
		Document->Find(A)->Character.TearsLevel = 2;
		Document->Find(A)->Character.AccessoriesShown.Init(0, 21);
		Document->NotifyChanged();
		SyncNow();
		Count(Tears1, Acc1);
		Check(Tears0 == 0 && Tears1 == 1, FString::Printf(TEXT("tears level 2 shows the medium tears (%d → %d)"), Tears0, Tears1));
		Check(Acc0 > 0 && Acc1 == 0, FString::Printf(TEXT("accessories switched off (%d → %d shown)"), Acc0, Acc1));
		Document->Find(A)->Character.TearsLevel = -1;
		Document->Find(A)->Character.AccessoriesShown.Reset();
		Document->NotifyChanged();
		SyncNow();
	}
	// Item child point (R4): a child of a catalogue item with 子の接続先 sits on that node, not on the item origin.
	Step(TEXT("child point"));
	{
		const FOKStudioCatalogItem* Joint = FOKStudioCatalog::Get().Items.FindByPredicate([](const FOKStudioCatalogItem& I) { return I.Prefab == TEXT("p_koi_stu_60kansetu00_02"); });
		const FTransform Point = Joint ? FOKStudioCatalog::Get().ItemChildPoint(Joint->Group, Joint->Category, Joint->No) : FTransform::Identity;
		Select(-1);
		const int32 Base = Add(EOKSceneObjectKind::Item);
		FOKSceneItem& BI = Document->Find(Base)->Item;
		if (Joint) { BI.Model = Joint->Model; BI.Prefab = Joint->Prefab; BI.Group = Joint->Group; BI.Category = Joint->Category; BI.No = Joint->No; }
		Document->Find(Base)->Transform = FTransform(FRotator(0.f, 90.f, 0.f), FVector(100.f, 0.f, 0.f));
		const int32 Kid = Document->AddObject(EOKSceneObjectKind::Folder, TEXT("child"), Base);
		Document->Find(Kid)->Transform = FTransform::Identity;
		const FVector Expected = (Point * Document->Find(Base)->Transform).GetLocation();
		Check(Joint && !Point.GetLocation().IsNearlyZero() && Document->Find(Kid)->Parent == Base && Document->WorldTransform(Kid).GetLocation().Equals(Expected, 0.01f),
			FString::Printf(TEXT("child sits on the item's child point (%s, %s)"), *Point.GetLocation().ToString(), *Document->WorldTransform(Kid).GetLocation().ToString()));
		const FVector Before = Document->WorldTransform(Kid).GetLocation();
		Document->Reparent(Kid, -1);
		Check(Document->WorldTransform(Kid).GetLocation().Equals(Before, 0.01f), TEXT("unparenting from the item keeps the child where it was"));
		Document->RemoveObject(Kid);
		Document->RemoveObject(Base);
		Select(-1);
	}
	Step(TEXT("viewer"));
	// Free camera, then the camera object.
	Orbit(30.f, -5.f);
	UpdateViewer();
	const FVector Free = Viewer->GetActorLocation();
	Document->Find(Cam)->Transform.SetLocation(FVector(250.f, 250.f, 160.f));
	Document->ActiveCamera = Cam;
	SyncNow();
	UpdateViewer();
	Check(Viewer->GetActorLocation().Equals(SceneActor->ActiveCamera()->GetComponentLocation(), 0.1f) && !Viewer->GetActorLocation().Equals(Free, 1.f), TEXT("viewer follows the active camera object"));
	APlayerController* PC = SceneActor->GetWorld()->GetFirstPlayerController();
	Check(PC && PC->GetViewTarget() == Viewer, TEXT("player views through the studio viewer"));
	// Guide object on the character, seen from the free camera.
	Step(TEXT("guide"));
	Document->ActiveCamera = -1;
	Focus = FVector(0.f, 0.f, 90.f); Yaw = -90.f; Pitch = -8.f; Distance = 380.f;
	UpdateViewer();
	if (PC && PC->PlayerCameraManager) PC->PlayerCameraManager->UpdateCamera(0.f);
	Select(A);
	GuideMode = 0;
	{
		TArray<FGuideHandle> Handles;
		FVector2D Center;
		double CmPerPixel = 0.;
		const bool bHandles = GuideHandles(Handles, Center, CmPerPixel);
		Check(bHandles && Handles.Num() == 3, FString::Printf(TEXT("move guide: %d handles, %.3f cm per pixel"), Handles.Num(), CmPerPixel));
		if (bHandles && Handles.Num() == 3)
		{
			const FVector2D End = Handles[0].Points.Last();
			Check(HitGuide(End) == 0, TEXT("clicking the X handle picks X"));
			const FVector Before = Document->WorldTransform(A).GetLocation();
			ApplyGuideDrag(0, (End - Handles[0].Points[0]).GetSafeNormal() * 50.);
			const FVector After = Document->WorldTransform(A).GetLocation();
			Check(After.X - Before.X > 1. && FMath::IsNearlyEqual(After.Y, Before.Y, 0.01) && FMath::IsNearlyEqual(After.Z, Before.Z, 0.01), FString::Printf(TEXT("dragging X moves %.1f cm along X only"), After.X - Before.X));
			GuideMode = 1;
			const double Yaw0 = Document->WorldTransform(A).Rotator().Yaw;
			ApplyGuideDrag(2, FVector2D(20., 0.));
			const double Turned = FRotator::NormalizeAxis(Document->WorldTransform(A).Rotator().Yaw - Yaw0);
			Check(FMath::IsNearlyEqual(Turned, 10., 0.1), FString::Printf(TEXT("rotating about Z turns %.2f degrees"), Turned));
			GuideMode = 2;
			ApplyGuideDrag(3, FVector2D(50., 0.));
			Check(FMath::IsNearlyEqual(Document->Find(A)->Transform.GetScale3D().X, 1.5, 0.01), TEXT("uniform scale handle scales 1.5x"));
			Document->Find(A)->Transform.SetScale3D(FVector::OneVector);
			GuideMode = 0;
			SyncNow();
			Check(SceneActor->PreviewOf(A) && SceneActor->PreviewOf(A)->GetActorLocation().Equals(Document->WorldTransform(A).GetLocation() + SceneActor->GetActorLocation(), 0.1f), TEXT("the character follows the guide"));
		}
	}
	Step(TEXT("fk"));
	FVector FkSet(10.f, 25.f, -50.f);
	{
		const FOKStudioCatalog& Catalog = FOKStudioCatalog::Get();
		Check(Catalog.FkBones.Num() >= 300 && Catalog.FindFkBone(TEXT("cf_j_arm00_L")), FString::Printf(TEXT("studio FK bone list: %d bones"), Catalog.FkBones.Num()));
		const AKKPreviewActor* P = SceneActor->PreviewOf(A);
		const AKKPreviewActor* const P0 = P;
		FVector Arm0(0), Head0(0), Arm1(0), Head1(0), Arm2(0);
		const bool bRead = P && P->Session.BoneEuler(TEXT("cf_j_arm00_L"), Arm0) && P->Session.BoneEuler(TEXT("cf_j_head"), Head0);
		SOKStudioPanel::SetFk(*Document->Find(A), TEXT("cf_j_arm00_L"), FkSet);
		Document->NotifyChanged();
		SyncNow();
		P = SceneActor->PreviewOf(A);
		const bool bPosed = P && P->Session.BoneEuler(TEXT("cf_j_arm00_L"), Arm1) && P->Session.BoneEuler(TEXT("cf_j_head"), Head1);
		Check(bRead && bPosed && P == P0 && Arm1.Equals(FkSet, 0.05f) && Head1.Equals(Head0, 0.05f),
			FString::Printf(TEXT("FK arm00_L %s -> %s (head unchanged %s), no rebuild"), *Arm0.ToString(), *Arm1.ToString(), *Head1.ToString()));
		Document->Find(A)->Character.Fk.Reset();
		Document->NotifyChanged();
		SyncNow();
		const bool bCleared = P && P->Session.BoneEuler(TEXT("cf_j_arm00_L"), Arm2) && Arm2.Equals(Arm0, 0.05f);
		Check(bCleared, FString::Printf(TEXT("clearing FK restores the pose %s"), *Arm2.ToString()));
		// Accessory point: an object hung on a_n_hand_L sits at the posed hand and follows it when the arm turns.
		{
			Check(Catalog.AccessoryPoints.Num() >= 50 && Catalog.FindAccessoryPoint(43) && Catalog.FindAccessoryPoint(43)->Bone == TEXT("a_n_hand_L"),
				FString::Printf(TEXT("studio accessory points: %d"), Catalog.AccessoryPoints.Num()));
			const int32 Held = Document->AddObject(EOKSceneObjectKind::Light, TEXT("held light"), A);
			Document->Find(Held)->AttachBone = TEXT("a_n_hand_L");
			Document->NotifyChanged();
			SyncNow();
			auto HandWorld = [&](FVector& Out)
			{
				float M[16];
				if (!P || !P->Session.BoneWorldMatrix(TEXT("a_n_hand_L"), M)) return false;
				Out = (FTransform(OKSkinPaletteToUnreal(M)) * P->GetActorTransform()).GetLocation();
				return true;
			};
			const USceneComponent* HeldComponent = SceneActor->ComponentOf(Held);
			FVector HeldHand0(0), HeldHand1(0);
			const bool bAt = HeldComponent && HandWorld(HeldHand0) && HeldComponent->GetComponentLocation().Equals(HeldHand0, 0.1);
			SOKStudioPanel::SetFk(*Document->Find(A), TEXT("cf_j_arm00_L"), FkSet);
			Document->NotifyChanged();
			SyncNow();
			SceneActor->PlaceAttached();
			const bool bFollows = HeldComponent && HandWorld(HeldHand1) && FVector::Dist(HeldHand0, HeldHand1) > 1. && HeldComponent->GetComponentLocation().Equals(HeldHand1, 0.1);
			Check(bAt && bFollows, FString::Printf(TEXT("an object on accessory point a_n_hand_L sits at the hand (%s) and follows it %.1f cm"),
				HeldComponent ? *HeldComponent->GetComponentLocation().ToString() : TEXT("none"), FVector::Dist(HeldHand0, HeldHand1)));
			Document->Find(A)->Character.Fk.Reset();
			Document->RemoveObject(Held);
			Document->NotifyChanged();
			SyncNow();
		}
		// Hand shape: the game's finger pattern replaces the animated fingers, without a rebuild.
		FVector Finger0(0), Finger1(0), Finger2(0);
		const bool bFinger0 = P && P->Session.BoneEuler(TEXT("cf_j_index02_L"), Finger0);
		Document->Find(A)->Character.HandLeft = 2;
		Document->NotifyChanged();
		SyncNow();
		const bool bFinger1 = P == SceneActor->PreviewOf(A) && P->Session.BoneEuler(TEXT("cf_j_index02_L"), Finger1) && !Finger1.Equals(Finger0, 0.5f);
		Check(bFinger0 && bFinger1, FString::Printf(TEXT("left hand shape 2 bends the index finger %s -> %s (%d shapes)"), *Finger0.ToString(), *Finger1.ToString(), P ? P->GetHandShapeCount() : 0));
		Document->Find(A)->Character.HandLeft = -1;
		Document->NotifyChanged();
		SyncNow();
		const bool bFinger2 = P->Session.BoneEuler(TEXT("cf_j_index02_L"), Finger2) && Finger2.Equals(Finger0, 0.05f);
		Check(bFinger2, FString::Printf(TEXT("hand shape -1 gives the fingers back %s"), *Finger2.ToString()));
		// KK Studio pose file: save the FK, clear it, load it back.
		{
			SOKStudioPanel::SetFk(*Document->Find(A), TEXT("cf_j_arm00_L"), FkSet);
			SOKStudioPanel::SetFk(*Document->Find(A), TEXT("cf_j_neck"), FVector(5.f, -15.f, 0.f));
			Document->NotifyChanged();
			SyncNow();
			Select(A);
			const TArray<FOKSceneFkBone> Saved = Document->Find(A)->Character.Fk;
			const FString PosePath = FOKStudioPose::Directory() / TEXT("_selftest_pose.dat");
			const bool bPoseSaved = Panel && Panel->SavePose(PosePath, TEXT("selftest"));
			TArray<uint8> Head;
			FFileHelper::LoadFileToArray(Head, *PosePath);
			const FUTF8ToTCHAR Code(Head.Num() > 1 ? (const ANSICHAR*)Head.GetData() + 1 : "", Head.Num() > 1 ? FMath::Min<int32>(Head[0], Head.Num() - 1) : 0);
			const bool bSigned = FString(Code.Length(), Code.Get()) == TEXT("【pose】");
			Document->Find(A)->Character.Fk.Reset();
			Document->NotifyChanged();
			SyncNow();
			const bool bPoseLoaded = bPoseSaved && Panel->LoadPose(PosePath);
			SyncNow();
			FVector Neck(0);
			const bool bNeck = P->Session.BoneEuler(TEXT("cf_j_neck"), Neck) && Neck.Equals(FVector(5.f, -15.f, 0.f), 0.05f);
			Check(bPoseSaved && bSigned && bPoseLoaded && Document->Find(A)->Character.Fk == Saved && bNeck,
				FString::Printf(TEXT("KK pose file round trip (%d FK bones, neck %s)"), Saved.Num(), *Neck.ToString()));
			IFileManager::Get().Delete(*PosePath);
			Document->Find(A)->Character.Fk.Reset();
		}
		// Bone guide: rotate rings on the bone, a drag turns it about its local Y and becomes its FK rotation.
		{
			Select(A);
			GuideBone = TEXT("cf_j_arm00_L");
			Document->ActiveCamera = -1;
			UpdateViewer();
			if (PC && PC->PlayerCameraManager) PC->PlayerCameraManager->UpdateCamera(0.f);
			TArray<FGuideHandle> BoneHandles;
			FVector2D BoneCenter;
			double BoneCm = 0.;
			const bool bRings = GuideHandles(BoneHandles, BoneCenter, BoneCm) && BoneHandles.Num() == 3 && BoneHandles[0].bClosed;
			FVector Start(0), Posed(0);
			P->Session.BoneEuler(TEXT("cf_j_arm00_L"), Start);
			const float In[3] = { float(Start.X), float(Start.Y), float(Start.Z) };
			float Want[3] = {};
			ok_unity_euler_rotate(In, 1, 10.f, Want);
			const bool bDragged = ApplyGuideDrag(1, FVector2D(20., 0.));
			SyncNow();
			const FOKSceneFkBone* Turned = Document->Find(A)->Character.Fk.FindByPredicate([](const FOKSceneFkBone& F) { return F.Bone == TEXT("cf_j_arm00_L"); });
			const bool bPosedOk = P->Session.BoneEuler(TEXT("cf_j_arm00_L"), Posed) && Posed.Equals(FVector(Want[0], Want[1], Want[2]), 0.05f);
			Check(bRings && bDragged && Turned && Turned->Euler.Equals(FVector(Want[0], Want[1], Want[2]), 0.01f) && bPosedOk,
				FString::Printf(TEXT("bone guide turns arm00_L about its local Y: %s -> %s"), *Start.ToString(), *Posed.ToString()));
			GuideBone.Reset();
			Document->Find(A)->Character.Fk.Reset();
			Document->NotifyChanged();
			SyncNow();
		}
		// Studio IK: a left-hand target starts on the hand; moving it carries the hand there; off gives the pose back.
		{
			Select(A);
			FTransform Hand0, Hand1, Hand2;
			const bool bHand0 = BoneWorld(P, TEXT("cf_j_hand_L"), Hand0);
			const bool bOn = Panel && Panel->SetIkEnabled(*Document->Find(A), 5, true);
			SyncNow();
			FVector Target0(0), Target1(0);
			const bool bAt = bOn && IkTargetWorld(5, Target0) && FVector::Dist(Target0, Hand0.GetLocation()) < 0.5;
			Check(bHand0 && bAt, FString::Printf(TEXT("an IK target starts on the hand (%.2f cm off)"), FVector::Dist(Target0, Hand0.GetLocation())));
			if (FOKSceneIkTarget* T = Document->Find(A)->Character.Ik.FindByPredicate([](const FOKSceneIkTarget& X) { return X.Effector == 5; })) T->Position += FVector(-12.f, -8.f, 10.f); // toward the body: the T-posed arm cannot reach further out
			Document->NotifyChanged();
			SyncNow();
			const bool bMoved = IkTargetWorld(5, Target1) && BoneWorld(P, TEXT("cf_j_hand_L"), Hand1) && FVector::Dist(Hand1.GetLocation(), Target1) < 1.0 && FVector::Dist(Hand1.GetLocation(), Hand0.GetLocation()) > 10.0;
			Check(bMoved, FString::Printf(TEXT("the hand follows its IK target (%.2f cm off, moved %.1f cm)"), FVector::Dist(Hand1.GetLocation(), Target1), FVector::Dist(Hand1.GetLocation(), Hand0.GetLocation())));
			GuideIk = 5;
			TArray<FGuideHandle> IkHandles;
			FVector2D IkCenter;
			double IkCm = 0.;
			const bool bIkGuide = GuideHandles(IkHandles, IkCenter, IkCm) && IkHandles.Num() == 3 && !IkHandles[0].bClosed;
			const FVector Before2 = Document->Find(A)->Character.Ik[0].Position;
			const bool bDragged = bIkGuide && ApplyGuideDrag(0, (IkHandles[0].Points.Last() - IkHandles[0].Points[0]).GetSafeNormal() * 40.);
			Check(bIkGuide && bDragged && !Document->Find(A)->Character.Ik[0].Position.Equals(Before2, 0.1), TEXT("the IK guide moves the target"));
			GuideIk = -1;
			Panel->SetIkEnabled(*Document->Find(A), 5, false);
			SyncNow();
			const bool bBack = BoneWorld(P, TEXT("cf_j_hand_L"), Hand2) && FVector::Dist(Hand2.GetLocation(), Hand0.GetLocation()) < 0.5 && Document->Find(A)->Character.Ik.IsEmpty();
			Check(bBack, FString::Printf(TEXT("turning IK off gives the pose back (%.2f cm)"), FVector::Dist(Hand2.GetLocation(), Hand0.GetLocation())));
		}
		// Kept for the save / load round trip below.
		SOKStudioPanel::SetFk(*Document->Find(A), TEXT("cf_j_arm00_L"), FkSet);
		Document->NotifyChanged();
		SyncNow();
	}
	const FString Path = FPaths::ProjectSavedDir() / TEXT("OKStudio/game_selftest.okscene");
	FString Error;
	const FString Before = Document->ToJson();
	Step(TEXT("save"));
	const bool bSaved = Save(Path, Error);
	Document->Objects.Reset();
	SyncNow();
	Check(!SceneActor->PreviewOf(A), TEXT("clearing the scene removes its characters"));
	Step(TEXT("load"));
	const bool bLoaded = bSaved && Load(Path, Error);
	SyncNow();
	Check(bLoaded && Document->ToJson() == Before && SceneActor->PreviewOf(A) && SceneActor->PreviewOf(A)->Session.PartCount() > 0, TEXT("save, clear and load restores the scene: ") + (bLoaded ? Path : Error));
	Step(TEXT("scene card"));
	{
		// OK scene card: an APNG of four 1280x720 views, 3 s each; the scene and its character cards inside.
		const FString CardPath = FPaths::ProjectSavedDir() / TEXT("OKStudio/selftest_card.png");
		FString CardError;
		const bool bWritten = Save(CardPath, CardError);
		TArray<uint8> CardBytes;
		TArray<FKKPngFrame> Frames;
		uint32 Plays = 1;
		FString ApngError;
		const bool bApng = bWritten && FFileHelper::LoadFileToArray(CardBytes, *CardPath) && FKKAnimatedPng::Read(CardBytes, Frames, Plays, ApngError);
		FImage First;
		const bool bSize = bApng && FImageUtils::DecompressImage(CardBytes.GetData(), CardBytes.Num(), First) && First.SizeX == FOKSceneCard::Width && First.SizeY == FOKSceneCard::Height;
		const bool bTiming = Frames.Num() == 4 && !Frames.ContainsByPredicate([](const FKKPngFrame& F) { return !FMath::IsNearlyEqual(F.Seconds, 3.0, 0.01); });
		Check(bWritten && bApng && bSize && bTiming && Plays == 0 && FOKSceneCard::Kind(CardPath) == FOKSceneCard::EKind::OkScene,
			FString::Printf(TEXT("scene card: %d frames, first %dx%d, loop %u, %d bytes %s%s"), Frames.Num(), First.SizeX, First.SizeY, Plays, CardBytes.Num(), *CardError, *ApngError));
		// The four views differ (preview, front, side, top).
		bool bDistinct = Frames.Num() == 4;
		for (int32 I = 1; bDistinct && I < 4; ++I) bDistinct = Frames[I].Png != Frames[I - 1].Png;
		Check(bDistinct, TEXT("the scene card's four views differ"));
		// Opened again: the same scene (character cards from inside the card), characters rebuilt.
		auto WithoutCards = [](UOKSceneDocument* D) { UOKSceneDocument* C = DuplicateObject(D, GetTransientPackage()); for (FOKSceneObject& O : C->Objects) O.Character.Card.Reset(); return C->ToJson(); };
		const FString Expected = WithoutCards(Document);
		const TWeakObjectPtr<AKKPreviewActor> PreviousPreview = SceneActor->PreviewOf(A);
		const bool bHadPreviousPreview = PreviousPreview.IsValid();
		const bool bOpened = Load(CardPath, CardError);
		Check(bHadPreviousPreview && bOpened && !PreviousPreview.IsValid(), TEXT("opening a replacement releases the old preview before rebuilding"));
		SyncNow();
		const FOKSceneObject* Reopened = Document->Find(A);
		Check(bOpened && WithoutCards(Document) == Expected && Reopened && FPaths::FileExists(Reopened->Character.Card) && SceneActor->PreviewOf(A) && SceneActor->PreviewOf(A)->Session.PartCount() > 0,
			TEXT("opening the scene card restores the scene and its characters: ") + (bOpened ? (Reopened ? Reopened->Character.Card : FString()) : CardError));
		// Official KStudio scenes the tests use are in the OK base library (studio/samples): no game needed.
		const FString KStudio = AOKSceneActor::LibraryDirectory() / TEXT("studio/samples/koikatu_cs0000059.png");
		if (FPaths::FileExists(KStudio)) Check(FOKSceneCard::Kind(KStudio) == FOKSceneCard::EKind::KStudio, TEXT("an original KStudio scene card is recognised"));
		// Back to the scene's own card files before the extracted copies go.
		FString RestoreError;
		Document->FromJson(Before, RestoreError);
		Document->NotifyChanged();
		SyncNow();
		IFileManager::Get().Delete(*CardPath);
		IFileManager::Get().DeleteDirectory(*(FOKSceneCard::CardDirectoryFor(CardPath)), false, true);
		File.Reset();
	}
	{
		FVector ArmLoaded(0);
		const AKKPreviewActor* PL = SceneActor->PreviewOf(A);
		const bool bLoadedFk = PL && PL->Session.BoneEuler(TEXT("cf_j_arm00_L"), ArmLoaded) && ArmLoaded.Equals(FkSet, 0.05f);
		Check(bLoadedFk, FString::Printf(TEXT("a loaded scene poses the character with its FK %s"), *ArmLoaded.ToString()));
	}
	Step(TEXT("studio catalogue"));
	{
		const FOKStudioCatalog& Catalog = FOKStudioCatalog::Get();
		Check(Catalog.bLoaded && Catalog.Items.Num() >= 2000 && Catalog.Maps.Num() >= 70, FString::Printf(TEXT("studio catalogue: %d items, %d maps %s"), Catalog.Items.Num(), Catalog.Maps.Num(), *Catalog.Error));
		{
			// Installed classroom desk root: Unity z=-0.306 m. AddObjectItem's
			// guide overwrites that root with the scene TRS, rather than adding it.
			AOKSceneActor* RootTest = SceneActor->GetWorld()->SpawnActor<AOKSceneActor>();
			RootTest->Document = NewObject<UOKSceneDocument>(RootTest);
			const int32 Id = RootTest->Document->AddObject(EOKSceneObjectKind::Item, TEXT("root placement test"), -1);
			FOKSceneObject* Desk = RootTest->Document->Find(Id);
			Desk->Item.Group = 2; Desk->Item.Category = 12; Desk->Item.No = 681;
			Desk->Transform = FTransform(FRotator(0, 35, 0), FVector(110, 220, 330));
			RootTest->Sync();
			const AKKPreviewActor* Preview = RootTest->PreviewOf(Id);
			Check(Preview && Preview->GetActorTransform().TransformPosition(FVector(0, -30.6, 0)).Equals(Desk->Transform.GetLocation(), 0.05),
				TEXT("Studio item placement replaces the authored prefab root"));
			RootTest->Destroy();
		}
		const FOKStudioCatalogItem* Balloon = Catalog.Items.FindByPredicate([](const FOKStudioCatalogItem& I) { return I.Prefab == TEXT("p_acs_waterbaloon_stu"); });
		Select(-1);
		const int32 Picked = Add(EOKSceneObjectKind::Item);
		const bool bPicked = Balloon && Panel && Panel->PickItem(Balloon->Group, Balloon->Category, Balloon->No);
		SyncNow();
		const AKKPreviewActor* PB = SceneActor->PreviewOf(Picked);
		Check(bPicked && PB && PB->Session.PartCount() > 0, FString::Printf(TEXT("catalogue item %s from the browser: %d parts %s"), Balloon ? *Balloon->Name : TEXT("?"), PB ? PB->Session.PartCount() : 0, SceneActor->Errors.Contains(Picked) ? *SceneActor->Errors[Picked] : TEXT("")));
		if (FOKSceneObject* PO = Document->Find(Picked)) PO->Item.Colors = { FLinearColor(1.f, 0.f, 0.f, 1.f) };
		Document->NotifyChanged();
		SyncNow();
		const AKKPreviewActor* PRed = SceneActor->PreviewOf(Picked);
		Check(PRed && PRed != PB && PRed->ItemColorOverrides.Num() == 1 && PRed->ItemColorOverrides[0] == FLinearColor(1.f, 0.f, 0.f, 1.f) && PRed->Session.PartCount() > 0, TEXT("changing an item colour rebuilds it with the colour"));
		Document->RemoveObject(Picked);
		Document->Map = 0;
		SyncNow();
		Check(SceneActor->MapPartCount() > 0, FString::Printf(TEXT("scene map 0: %d parts %s"), SceneActor->MapPartCount(), *SceneActor->MapError));
		// OK_STUDIO_MAP_SHOT=<folder>: pictures of maps 0 and 1 for a look at where their rigid meshes landed.
		const FString MapShots = FPlatformMisc::GetEnvironmentVariable(TEXT("OK_STUDIO_MAP_SHOT"));
		if (!MapShots.IsEmpty())
		{
			const FVector ShotFocus = Focus; const float ShotYaw = Yaw, ShotPitch = Pitch, ShotDistance = Distance;
			for (int32 MapNo : { 0, 1 })
			{
				Document->Map = MapNo;
				SyncNow();
				Focus = FVector(0.f, 0.f, 120.f); Yaw = 35.f; Pitch = -25.f; Distance = MapNo == 0 ? 650.f : 1800.f;
				UpdateViewer();
				FString ShotError;
				const FString Shot = MapShots / FString::Printf(TEXT("map_%d.png"), MapNo);
				Check(CaptureToPng(Shot, 1280, 720, ShotError), FString::Printf(TEXT("map %d picture %s (%d parts) %s"), MapNo, *Shot, SceneActor->MapPartCount(), *ShotError));
			}
			Document->Map = 0;
			Focus = ShotFocus; Yaw = ShotYaw; Pitch = ShotPitch; Distance = ShotDistance;
			UpdateViewer();
			SyncNow();
		}
		const FString WithMap = Document->ToJson();
		FString MapError;
		Check(Document->FromJson(WithMap, MapError) && Document->Map == 0, TEXT("the scene map is saved with the scene"));
		Document->Map = -1;
		SyncNow();
		Check(!SceneActor->MapPreview(), TEXT("clearing the map removes it"));
	}
	Step(TEXT("timeline"));
	{
		const int32 Moving = Add(EOKSceneObjectKind::Light);
		Document->Find(Moving)->Transform.SetLocation(FVector(0.f, 0.f, 100.f));
		Document->Find(Moving)->Light.Intensity = 2.f;
		Document->SetKey(Moving, 0.f);
		Document->Find(Moving)->Transform.SetLocation(FVector(200.f, 0.f, 100.f));
		Document->Find(Moving)->Light.Intensity = 10.f;
		Document->SetKey(Moving, 2.f);
		SetTimelineTime(1.f);
		const FVector Mid = Document->Find(Moving)->Transform.GetLocation();
		const float MidIntensity = Document->Find(Moving)->Light.Intensity;
		Check(Mid.Equals(FVector(100.f, 0.f, 100.f), 0.01f) && FMath::IsNearlyEqual(MidIntensity, 6.f, 0.01f), FString::Printf(TEXT("timeline interpolates between keys: %s, intensity %.2f"), *Mid.ToString(), MidIntensity));
		const FString Json = Document->ToJson();
		UOKSceneDocument* Copy = NewObject<UOKSceneDocument>();
		FString TimelineError;
		const bool bRound = Copy->FromJson(Json, TimelineError) && Copy->TrackOf(Moving) && Copy->TrackOf(Moving)->Keys.Num() == 2;
		Check(bRound, TEXT("timeline tracks are saved with the scene"));
		bTimelinePlaying = true;
		SetTimelineTime(0.f);
		Tick(0.5f);
		bTimelinePlaying = false;
		Check(FMath::IsNearlyEqual(TimelineTime, 0.5f, 0.01f) && Document->Find(Moving)->Transform.GetLocation().Equals(FVector(50.f, 0.f, 100.f), 0.01f), TEXT("playback advances the timeline"));
		// Curves: ease in-out from the first key is at 2 * 0.25^2 of the way a quarter in (x 25 of 200 at 0.5 s); a step holds the key.
		Document->KeyAt(Moving, 0.f)->Ease = EOKSceneEase::EaseInOut;
		SetTimelineTime(0.f);
		Document->SetKey(Moving, 0.f);
		const bool bKept = Document->KeyAt(Moving, 0.f)->Ease == EOKSceneEase::EaseInOut;
		SetTimelineTime(0.5f);
		const float Eased = float(Document->Find(Moving)->Transform.GetLocation().X);
		Document->KeyAt(Moving, 0.f)->Ease = EOKSceneEase::Step;
		SetTimelineTime(1.9f);
		const float Held = float(Document->Find(Moving)->Transform.GetLocation().X);
		Check(bKept && FMath::IsNearlyEqual(Eased, 25.f, 0.05f) && FMath::IsNearlyEqual(Held, Document->TrackOf(Moving)->Keys[0].Transform.GetLocation().X, 0.01f),
			FString::Printf(TEXT("timeline curves: ease in-out x %.2f at 0.5 s, step holds x %.2f; re-keying keeps the curve %d"), Eased, Held, bKept ? 1 : 0));
		Document->KeyAt(Moving, 0.f)->Ease = EOKSceneEase::Linear;
		// A smooth path (camera path) passes through a middle key without a corner: off the straight line between keys.
		{
			FOKSceneObject* M = Document->Find(Moving);
			const FTransform SavedAt2 = Document->TrackOf(Moving)->Keys.Last().Transform;
			M->Transform.SetLocation(FVector(100.f, 100.f, 100.f));
			Document->SetKey(Moving, 1.f);
			Document->TrackOf(Moving)->bSmoothPath = true;
			SetTimelineTime(1.f);
			const FVector OnKey = Document->Find(Moving)->Transform.GetLocation();
			SetTimelineTime(0.5f);
			const FVector Curve = Document->Find(Moving)->Transform.GetLocation();
			Document->TrackOf(Moving)->bSmoothPath = false;
			SetTimelineTime(0.5f);
			const FVector Straight = Document->Find(Moving)->Transform.GetLocation();
			Check(OnKey.Equals(FVector(100.f, 100.f, 100.f), 0.01f) && Straight.Equals(FVector(50.f, 50.f, 100.f), 0.01f) && Curve.Y > Straight.Y + 1.f,
				FString::Printf(TEXT("smooth path through the key %s: half way %s (straight %s)"), *OnKey.ToString(), *Curve.ToString(), *Straight.ToString()));
			Document->RemoveKey(Moving, 1.f);
			Document->TrackOf(Moving)->Keys.Last().Transform = SavedAt2;
		}
		Check(Document->RemoveKey(Moving, 2.f) && Document->TrackOf(Moving)->Keys.Num() == 1, TEXT("removing a key"));
		// A character's FK is part of its keys: the bone turns between the keyed rotations.
		{
			FOKSceneObject* Ch = Document->Find(A);
			const FOKSceneCharacter SavedCharacter = Ch->Character;
			const FTransform SavedTransform = Ch->Transform;
			Ch->Character.Fk = { { TEXT("cf_j_arm00_L"), FVector(10.f, 0.f, 0.f) } };
			Document->SetKey(A, 0.f);
			Document->Find(A)->Character.MouthOpen = 0.f;
			Document->Find(A)->Character.EyesPattern = 1;
			Document->SetKey(A, 0.f);
			Document->Find(A)->Character.Fk = { { TEXT("cf_j_arm00_L"), FVector(50.f, 0.f, 0.f) } };
			Document->Find(A)->Character.MouthOpen = 1.f;
			Document->Find(A)->Character.EyesPattern = 4;
			Document->SetKey(A, 2.f);
			SetTimelineTime(1.f);
			SyncNow();
			const FOKSceneFkBone* MidBone = Document->Find(A)->Character.Fk.FindByPredicate([](const FOKSceneFkBone& B) { return B.Bone == TEXT("cf_j_arm00_L"); });
			FVector Posed(0);
			const bool bPosed = SceneActor->PreviewOf(A) && SceneActor->PreviewOf(A)->Session.BoneEuler(TEXT("cf_j_arm00_L"), Posed);
			Check(MidBone && MidBone->Euler.Equals(FVector(30.f, 0.f, 0.f), 0.05f) && bPosed && Posed.Equals(FVector(30.f, 0.f, 0.f), 0.05f),
				FString::Printf(TEXT("timeline FK keys turn the bone halfway: %s, posed %s"), MidBone ? *MidBone->Euler.ToString() : TEXT("?"), *Posed.ToString()));
			const FOKSceneCharacter& MidC = Document->Find(A)->Character;
			Check(FMath::IsNearlyEqual(MidC.MouthOpen, 0.5f, 0.01f) && MidC.EyesPattern == 1, FString::Printf(TEXT("timeline face keys: mouth %.2f blends, eyes pattern %d holds until the next key"), MidC.MouthOpen, MidC.EyesPattern));
			SetTimelineTime(2.f);
			Check(Document->Find(A)->Character.EyesPattern == 4, TEXT("the eyes pattern switches at its key"));
			// The timeline as an OK animation: played back without the FK keys, the arm is halfway at 1 s.
			{
				const float SavedDuration = Document->Duration;
				Document->Duration = 2.f;
				FString AnimError;
				const FString AnimFile = ExportOkAnim(A, 30.f, AnimError);
				Document->Duration = SavedDuration;
				UOKAnimSequence* Recorded = AnimFile.IsEmpty() ? nullptr : UOKAnimationLibrary::LoadOkAnimFile(AnimFile, nullptr, NAME_None, AnimError);
				const TSharedPtr<FOkAnimPlayer> Player = Recorded ? Recorded->CreatePlayer(AnimError) : nullptr;
				AKKPreviewActor* Actor = SceneActor->PreviewOf(A);
				FVector Played(0);
				bool bPlayed = false;
				if (Player && Actor)
				{
					Actor->SetUserFk({}, {});
					bPlayed = Actor->SetOkAnimation(Player, false);
					Actor->PoseMode = 3;
					Actor->AnimPhase = 1.f;
					Actor->ApplyPoseFrame(0.f, false);
					bPlayed = bPlayed && Actor->Session.BoneEuler(TEXT("cf_j_arm00_L"), Played);
				}
				Check(Recorded && bPlayed && Played.Equals(FVector(30.f, 0.f, 0.f), 0.5f) && Recorded->GetPlayLength() > 1.9f,
					FString::Printf(TEXT("timeline to .okanim: %s, %.2f s, arm at 1 s %s %s"), *FPaths::GetCleanFilename(AnimFile), Recorded ? Recorded->GetPlayLength() : 0.f, *Played.ToString(), *AnimError));
				if (!AnimFile.IsEmpty()) IFileManager::Get().Delete(*AnimFile);
				// The test played the file on the character's preview: back to the document's character.
				SceneActor->Rebuild(A);
			}
			Document->Tracks.RemoveAll([&](const FOKSceneTrack& T2) { return T2.ObjectId == A; });
			Document->Find(A)->Character = SavedCharacter;
			Document->Find(A)->Transform = SavedTransform;
			Document->NotifyChanged();
			SyncNow();
		}
		Document->RemoveObject(Moving);
		Document->Tracks.Reset();
		Document->Duration = 10.f;
		Select(-1);
		SyncNow();
	}
	Step(TEXT("effect"));
	{
		const FOKStudioEffect* Fireflies = FOKStudioCatalog::Get().FindEffect(TEXT("fireflies"));
		const int32 Fx = Add(EOKSceneObjectKind::Effect);
		Document->Find(Fx)->Effect.Effect = TEXT("fireflies");
		Document->Find(Fx)->Effect.Speed = 0.5f;
		Document->Find(Fx)->Transform.SetLocation(FVector(0.f, 50.f, 100.f));
		Document->NotifyChanged();
		SyncNow();
		const UNiagaraComponent* Nc = Cast<UNiagaraComponent>(SceneActor->ComponentOf(Fx));
		Check(Fireflies && Nc && Nc->GetAsset() && Nc->GetAsset()->GetPathName() == Fireflies->Asset && Nc->IsActive() && FMath::IsNearlyEqual(Nc->GetCustomTimeDilation(), 0.5f),
			FString::Printf(TEXT("Niagara effect fireflies: component %d, asset %s, active %d %s"), Nc ? 1 : 0, Nc && Nc->GetAsset() ? *Nc->GetAsset()->GetPathName() : TEXT("-"), Nc && Nc->IsActive() ? 1 : 0,
				SceneActor->Errors.Contains(Fx) ? *SceneActor->Errors[Fx] : TEXT("")));
		const FString Json = Document->ToJson();
		UOKSceneDocument* FxCopy = NewObject<UOKSceneDocument>();
		FString FxError;
		Check(FxCopy->FromJson(Json, FxError) && FxCopy->Find(Fx) && FxCopy->Find(Fx)->Effect == Document->Find(Fx)->Effect, TEXT("effects are saved with the scene"));
		const FOKStudioEffect* Imported = FOKStudioCatalog::Get().EffectForKks(6, 148, 2529);
		Check(Imported && Imported->Id == TEXT("fireflies") && FOKStudioCatalog::Get().Effects.Num() >= 8, FString::Printf(TEXT("the KKS firefly item becomes the OK fireflies effect; %d effects"), FOKStudioCatalog::Get().Effects.Num()));
		Document->RemoveObject(Fx);
		Document->NotifyChanged();
		SyncNow();
	}
	Step(TEXT("item fk"));
	{
		const int32 FkItem = Add(EOKSceneObjectKind::Item);
		FOKSceneItem& FI = Document->Find(FkItem)->Item;
		FI.Group = 15; FI.Category = 74; FI.No = 978;
		Document->NotifyChanged();
		SyncNow();
		FString Model, Prefab, ModelError;
		const TArray<FString> Bones = AOKSceneActor::ItemModel(Document->Find(FkItem)->Item, Model, Prefab, ModelError) ? FOkSession::OkModelFkBones(AOKSceneActor::LibraryDirectory(), Model, Prefab) : TArray<FString>();
		// The item's vertices (session parts, model space): their centre moves when a bone turns part of the mesh.
		auto Centre = [&](int32& OutVerts)
		{
			FVector Sum(0);
			OutVerts = 0;
			const AKKPreviewActor* P = SceneActor->PreviewOf(FkItem);
			for (int32 Part = 0; P && Part < P->Session.PartCount(); ++Part)
			{
				OkMeshView V;
				if (!P->Session.PartAt(Part, V) || !V.positions) continue;
				for (int32 I = 0; I < V.n_verts; ++I) Sum += FVector(V.positions[I * 3], V.positions[I * 3 + 1], V.positions[I * 3 + 2]);
				OutVerts += V.n_verts;
			}
			return OutVerts ? Sum / OutVerts * 100.0 : FVector::ZeroVector;
		};
		int32 N0 = 0, N1 = 0, N2 = 0;
		const FVector C0 = Centre(N0);
		Check(Bones.Contains(TEXT("neo_J_fk_kansetu_00")) && N0 > 0, FString::Printf(TEXT("FK item 15/74/978: %d FK bones, %d vertices shown"), Bones.Num(), N0));
		Document->Find(FkItem)->Item.Fk = { { TEXT("neo_J_fk_kansetu_00"), FVector(0.f, 0.f, 60.f) } };
		Document->NotifyChanged();
		SyncNow();
		const FVector C1 = Centre(N1);
		Document->Find(FkItem)->Item.Fk.Reset();
		Document->NotifyChanged();
		SyncNow();
		const FVector C2 = Centre(N2);
		Check(N1 == N0 && N2 == N0 && FVector::Dist(C0, C1) > 0.1 && FVector::Dist(C0, C2) < 0.001, FString::Printf(TEXT("item FK turns the item (vertex centre moves %.2f cm) and clearing restores it (%.4f cm)"), FVector::Dist(C0, C1), FVector::Dist(C0, C2)));
		Check(!SceneActor->Errors.Contains(FkItem), TEXT("the FK item has no errors"));
		Document->RemoveObject(FkItem);
		Document->NotifyChanged();
		SyncNow();
	}
	Step(TEXT("look modes"));
	{
		AKKPreviewActor* PL = SceneActor->PreviewOf(A);
		FVector Head0(0), Head1(0), Head2(0);
		const bool bH0 = PL && PL->Session.BoneEuler(TEXT("cf_j_neck"), Head0);
		FOKSceneCharacter& LC = Document->Find(A)->Character;
		LC.NeckLook = EOKSceneLook::Point;
		LC.LookPoint = FVector(150.f, 60.f, 140.f);
		for (int32 I = 0; I < 30; ++I) SceneActor->UpdateLooks(1.f / 30.f);
		const bool bH1 = PL && PL->Session.BoneEuler(TEXT("cf_j_neck"), Head1) && !Head1.Equals(Head0, 2.f);
		Check(bH0 && bH1, FString::Printf(TEXT("the neck turns to the look point: %s -> %s"), *Head0.ToString(), *Head1.ToString()));
		Document->Find(A)->Character.NeckLook = EOKSceneLook::Animation;
		for (int32 I = 0; I < 30; ++I) SceneActor->UpdateLooks(1.f / 30.f);
		const bool bH2 = PL && PL->Session.BoneEuler(TEXT("cf_j_neck"), Head2) && Head2.Equals(Head0, 0.5f);
		Check(bH2, FString::Printf(TEXT("back to the animation's neck: %s"), *Head2.ToString()));
	}
    Step(TEXT("saved fixed neck"));
    {
        AKKPreviewActor* Preview = SceneActor->PreviewOf(A);
        FVector SavedNeckBefore;
        const bool bBefore = Preview && Preview->Session.BoneEuler(TEXT("cf_j_head"), SavedNeckBefore);
        auto NativeQuat = [](const FVector& Euler)
        {
            const FQuat U = FOKStudioImport::UnityToUe(FVector::ZeroVector, Euler, FVector::OneVector).GetRotation();
            return FQuat(-U.X, U.Z, U.Y, U.W);
        };
        FOKSceneCharacter& Character = Document->Find(A)->Character;
        Character.FixedNeck = { NativeQuat(FVector(9,-12,3)), NativeQuat(FVector(18,-27,7)) };
        Character.NeckLook = EOKSceneLook::Fixed;
        Document->NotifyChanged();
        SyncNow();
        FVector SavedNeckFixed;
        const bool bFixed = Preview && Preview->Session.BoneEuler(TEXT("cf_j_head"), SavedNeckFixed);
        Check(bFixed && FMath::Abs(NativeQuat(SavedNeckFixed) | Character.FixedNeck[1]) > .99999, TEXT("saved FIX mode applies the source head local"));
        Character.NeckLook = EOKSceneLook::Animation;
        Document->NotifyChanged();
        SyncNow();
        FVector SavedNeckRestored;
        const bool bRestored = Preview && Preview->Session.BoneEuler(TEXT("cf_j_head"), SavedNeckRestored);
        Check(bBefore && bRestored && FMath::Abs(NativeQuat(SavedNeckRestored) | NativeQuat(SavedNeckBefore)) > .99999, TEXT("animation mode clears runtime FIX and restores the animated head"));
        Character.NeckLook = EOKSceneLook::Fixed;
        Document->NotifyChanged();
        SyncNow();
        const bool bAgain = Preview && Preview->Session.BoneEuler(TEXT("cf_j_head"), SavedNeckFixed);
        Check(bAgain && FMath::Abs(NativeQuat(SavedNeckFixed) | Character.FixedNeck[1]) > .99999, TEXT("saved FIX can be selected again without reimport"));
        Character.FixedNeck.Reset();
        Character.NeckLook = EOKSceneLook::Animation;
        Document->NotifyChanged();
        SyncNow();
    }
	Step(TEXT("post and frames"));
	{
		Document->Post.bBloom = true; Document->Post.BloomIntensity = 2.5f;
		Document->Post.bVignette = true; Document->Post.VignetteIntensity = 0.7f;
		Document->Post.bDepthOfField = true; Document->Post.FocalDistance = 250.f;
		Tick(0.f);
		const FPostProcessSettings& S = Viewer->GetCameraComponent()->PostProcessSettings;
		const bool bSourceBloom = OKStudioSourceBloomState() == TEXT("ready") && S.BloomIntensity == 0.f && S.WeightedBlendables.Array.Num() >= 8;
		Check(S.bOverride_BloomIntensity && (bSourceBloom || S.BloomIntensity >= 2.5f) && FMath::IsNearlyEqual(S.VignetteIntensity, 0.7f) && S.bOverride_DepthOfFieldFocalDistance && FMath::IsNearlyEqual(S.DepthOfFieldFocalDistance, 250.f),
			TEXT("scene post-processing reaches the view (bloom, vignette, depth of field)"));
		UOKSceneDocument* PostCopy = NewObject<UOKSceneDocument>();
		FString PostError;
		Check(PostCopy->FromJson(Document->ToJson(), PostError) && PostCopy->Post.bBloom && FMath::IsNearlyEqual(PostCopy->Post.VignetteIntensity, 0.7f), TEXT("post-processing is saved with the scene"));
		Document->Post = FOKScenePost();
		Tick(0.f);
		const float SavedDuration = Document->Duration;
		Document->Duration = 0.1f;
		const FString Dir = ExportFrames(30.f, 320, 180);
		Check(IsExporting() && ExportFrameCount() == 3, FString::Printf(TEXT("frame export plans %d frames into %s"), ExportFrameCount(), *Dir));
		for (int32 Guard = 0; IsExporting() && Guard < 20; ++Guard) TickExport();
		TArray<FString> Frames;
		IFileManager::Get().FindFiles(Frames, *(Dir / TEXT("frame_*.png")), true, false);
		Check(!IsExporting() && Frames.Num() == 3, FString::Printf(TEXT("frame export writes %d of 3 PNG frames"), Frames.Num()));
		// The same frames as an mp4 (ffmpeg is a local tool; without it the check is skipped).
		if (FPaths::FileExists(FfmpegPath()))
		{
			const FString Movie = Dir + TEXT(".mp4");
			FString Encoder, MovieError;
			const bool bMovie = EncodeMp4(Dir, 30.f, Movie, Encoder, MovieError);
			const int64 Bytes = IFileManager::Get().FileSize(*Movie);
			Check(bMovie && Bytes > 1000, FString::Printf(TEXT("mp4 export: %lld bytes with %s %s"), Bytes, *Encoder, *MovieError));
			IFileManager::Get().Delete(*Movie);
		}
		else Lines.Add(TEXT("SKIP  mp4 export (no Tools/ffmpeg/ffmpeg.exe)"));
		IFileManager::Get().DeleteDirectory(*Dir, false, true);
		Document->Duration = SavedDuration;
	}
	Step(TEXT("screenshot"));
	{
		const FString Shot = Screenshot(640, 360);
		TArray<uint8> ShotBytes;
		const bool bShot = !Shot.IsEmpty() && FFileHelper::LoadFileToArray(ShotBytes, *Shot) && ShotBytes.Num() > 1000;
		FImage Decoded;
		const bool bSize = bShot && FImageUtils::DecompressImage(ShotBytes.GetData(), ShotBytes.Num(), Decoded) && Decoded.SizeX == 640 && Decoded.SizeY == 360;
		Check(bShot && bSize, FString::Printf(TEXT("screenshot 640x360 written (%d bytes) %s"), ShotBytes.Num(), *Shot));
		if (bShot) IFileManager::Get().Delete(*Shot);
		// Transparent background: the characters opaque, the rest clear.
		const FString Clear = Screenshot(640, 360, true);
		TArray<uint8> ClearBytes;
		FImage ClearImage;
		int64 Opaque = 0, Transparent = 0;
		if (!Clear.IsEmpty() && FFileHelper::LoadFileToArray(ClearBytes, *Clear) && FImageUtils::DecompressImage(ClearBytes.GetData(), ClearBytes.Num(), ClearImage))
		{
			ClearImage.ChangeFormat(ERawImageFormat::BGRA8, EGammaSpace::sRGB);
			for (const FColor& C : ClearImage.AsBGRA8()) { if (C.A == 255) ++Opaque; else if (C.A == 0) ++Transparent; }
		}
		const int64 All = int64(640) * 360;
		Check(Opaque > All / 200 && Transparent > All / 2 && Opaque + Transparent == All, FString::Printf(TEXT("transparent screenshot: %lld opaque, %lld clear of %lld pixels"), Opaque, Transparent, All));
		if (!Clear.IsEmpty()) IFileManager::Get().Delete(*Clear);
	}
	Step(TEXT("studio animation"));
	{
		const FOKStudioCatalog& Catalog = FOKStudioCatalog::Get();
		const int32 Walk = Catalog.Anims.IndexOfByPredicate([](const FOKStudioAnim& X) { return X.Kind == 0 && !X.File.IsEmpty() && X.Duration > 1.f && X.bLooping; });
		Select(A);
		// FK (kept from the save / load check above) would hold its bones over the animation.
		Document->Find(A)->Character.Fk.Reset();
		const bool bPicked = Walk != INDEX_NONE && Panel && Panel->PickAnim(Walk);
		SyncNow();
		AKKPreviewActor* PA2 = SceneActor->PreviewOf(A);
		FVector E0(0), E1(0);
		const bool bE0 = PA2 && PA2->Session.BoneEuler(TEXT("cf_j_arm00_L"), E0);
		SceneActor->AdvanceAnimations(Walk != INDEX_NONE ? Catalog.Anims[Walk].Duration * 0.37f : 0.5f);
		const bool bMoved = PA2 && PA2->PoseMode == 3 && PA2->Session.BoneEuler(TEXT("cf_j_arm00_L"), E1) && !E1.Equals(E0, 0.5f);
		Check(bPicked && bE0 && bMoved && !SceneActor->Errors.Contains(A), FString::Printf(TEXT("library Studio animation %s plays (%d animations, arm %s -> %s) %s"),
			Walk != INDEX_NONE ? *Catalog.Anims[Walk].Name : TEXT("?"), Catalog.Anims.FilterByPredicate([](const FOKStudioAnim& X) { return !X.File.IsEmpty(); }).Num(), *E0.ToString(), *E1.ToString(), SceneActor->Errors.Contains(A) ? *SceneActor->Errors[A] : TEXT("")));
		Document->Find(A)->Character.StudioAnimeNo = -1;
		Document->NotifyChanged();
		SyncNow();
	}
	Step(TEXT("kstudio import"));
	{
		const FString Samples = AOKSceneActor::LibraryDirectory() / TEXT("studio/samples");
		const FString Png = Samples / TEXT("koikatu_cs0000059.png");
		if (FPaths::FileExists(Png))
		{
			UOKSceneDocument* Saved = DuplicateObject(Document.Get(), this);
			FOKStudioImport::FReport Report;
			FString ImportError;
			const FString CardDir = FPaths::ProjectSavedDir() / TEXT("OKStudio/import_selftest_cards");
			const bool bImported = FOKStudioImport::Import(Png, Document, CardDir, Report, ImportError);
			SyncNow();
			int32 Built = 0, Chars = 0, Items = 0, ItemsBuilt = 0;
			for (const FOKSceneObject& O : Document->Objects)
			{
				const AKKPreviewActor* PO = SceneActor->PreviewOf(O.Id);
				if (O.Kind == EOKSceneObjectKind::Character) { ++Chars; if (PO && PO->Session.PartCount() > 0) ++Built; }
				if (O.Kind == EOKSceneObjectKind::Item) { ++Items; if (PO && PO->Session.PartCount() > 0) ++ItemsBuilt; }
			}
			Check(bImported && Chars == Report.Characters && Report.CharacterCards == Report.Characters && Built == Chars && Chars > 0,
				FString::Printf(TEXT("KStudio import: %d characters, %d OK cards, %d shown %s"), Chars, Report.CharacterCards, Built, *ImportError));
			Check(bImported && Items == Report.CatalogItems && ItemsBuilt == Items, FString::Printf(TEXT("KStudio import: %d of %d items from the catalogue, %d shown; map %d"), Report.CatalogItems, Report.Items, ItemsBuilt, Report.Map));
			for (const FString& N : Report.Notes) UE_LOG(LogTemp, Display, TEXT("OK studio import note: %s"), *N);
			// A scene posed with Studio IK: its characters get the 9 effector targets and their hands reach them.
			const FString IkPng = Samples / TEXT("koikatu_cs0000010.png");
			FOKStudioImport::FReport IkReport;
			if (FPaths::FileExists(IkPng) && FOKStudioImport::Import(IkPng, Document, CardDir, IkReport, ImportError))
			{
				SyncNow();
				int32 Posed = 0, Reached = 0;
				float Worst = 0.f;
				for (const FOKSceneObject& O : Document->Objects)
				{
					if (O.Kind != EOKSceneObjectKind::Character || O.Character.Ik.Num() != 13) continue;
					++Posed;
					const AKKPreviewActor* PO = SceneActor->PreviewOf(O.Id);
					const FOKSceneIkTarget* Hand = O.Character.Ik.FindByPredicate([](const FOKSceneIkTarget& T) { return T.Effector == 5; });
					FVector At;
					if (PO && Hand && PO->Session.IkEffectorPosition(5, At))
					{
						const float Miss = float(FVector::Dist(At, AKKPreviewActor::ActorLocalToUnity(Hand->Position))) * 100.f;
						Worst = FMath::Max(Worst, Miss);
						if (Miss < 3.f) ++Reached;
					}
					else
					{
						const FString* ShowError = SceneActor->Errors.Find(O.Id);
						UE_LOG(LogTemp, Display, TEXT("OK studio IK import: %s not measured (preview %d, %s)"), *O.Name, PO ? 1 : 0, ShowError ? **ShowError : TEXT("no error"));
					}
				}
				Check(Posed > 0 && Reached == Posed, FString::Printf(TEXT("KStudio IK import: %d characters with 13 IK targets (9 effectors, 4 bend goals), %d left hands on target (worst %.2f cm)"), Posed, Reached, Worst));
			}
			Document->Objects = Saved->Objects; Document->NextId = Saved->NextId; Document->ActiveCamera = Saved->ActiveCamera; Document->Map = Saved->Map;
			Document->NotifyChanged();
			SyncNow();
		}
		else Lines.Add(TEXT("SKIP  KStudio import (no installed game scene)"));
	}
	Step(TEXT("scene list"));
	{
		// The save button writes an OK scene card; reopened, its characters use the card's embedded copies.
		const FString Named = SceneDirectory() / TEXT("_selftest_scene.png");
		const FString Current = Document->ToJson();
		auto WithoutCards = [](UOKSceneDocument* D) { UOKSceneDocument* C = DuplicateObject(D, GetTransientPackage()); for (FOKSceneObject& O : C->Objects) O.Character.Card.Reset(); return C->ToJson(); };
		const FString Expected = WithoutCards(Document);
		const bool bNamed = Panel && Panel->SaveNamed(TEXT("_selftest_scene"));
		Check(bNamed && SceneFiles().Contains(Named) && Panel->ListedFiles() == SceneFiles().Num(), FString::Printf(TEXT("scene list shows the saved scene (%d files)"), SceneFiles().Num()));
		Document->Objects.Reset();
		SyncNow();
		Check(bNamed && Panel->LoadFile(Named) && WithoutCards(Document) == Expected, TEXT("picking it from the list loads it"));
		FString RestoreError;
		Document->FromJson(Current, RestoreError);
		Document->NotifyChanged();
		SyncNow();
		IFileManager::Get().Delete(*Named);
		IFileManager::Get().DeleteDirectory(*(SceneDirectory() / TEXT("_selftest_scene_cards")), false, true);
	}
	Step(TEXT("stop"));
	{
		bool bStopped = false;
		OnStopped.AddLambda([&bStopped] { bStopped = true; });
		APlayerController* StopPC = SceneActor->GetWorld()->GetFirstPlayerController();
		const AActor* OldViewer = Viewer;
		RequestStop();
		Tick(0.f);
		Check(bStopped && !SceneActor && Get() == nullptr && StopPC && StopPC->GetViewTarget() != OldViewer, TEXT("leaving the studio tears it down, restores the view and tells the caller"));
	}
	Lines.Add(Failures ? FString::Printf(TEXT("RESULT FAIL (%d)"), Failures) : FString(TEXT("RESULT PASS")));
	for (const FString& L : Lines) UE_LOG(LogTemp, Display, TEXT("OK studio game selftest: %s"), *L);
	FFileHelper::SaveStringArrayToFile(Lines, *(FPaths::ProjectSavedDir() / TEXT("OKStudio/game_selftest.txt")));
	if (bExit) FPlatformMisc::RequestExit(false);
}

namespace
{
	UWorld* GameWorld()
	{
		return GEngine && GEngine->GameViewport ? GEngine->GameViewport->GetWorld() : nullptr;
	}

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
			TArray<FString> Names;
			IFileManager::Get().FindFiles(Names, *(Folder / TEXT("*.png")), true, false);
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
