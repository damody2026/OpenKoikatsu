#pragma once

// OK Studio in-game Slate UI: the scene view and the studio panel. Shared by OKStudioGame.cpp and its self tests.
#include "OKStudioGame.h"
#include "OKStudioStyle.h"
#include "OKStudioSettings.h"
#include "OKStudioThumbnails.h"
#include "SOKStudioTimeline.h"
#include "Framework/Application/IInputProcessor.h"
#include "Framework/Application/MenuStack.h"
#include "Input/DragAndDrop.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SGridPanel.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"
#include "Widgets/Views/STileView.h"
#include "Types/ISlateMetaData.h"
#include "Widgets/Layout/SConstraintCanvas.h"
#include "Widgets/Layout/SDPIScaler.h"
#include "Widgets/Layout/SSpacer.h"
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

namespace OKStudioPanelText
{
	inline const TCHAR* KindTag(EOKSceneObjectKind Kind)
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

	inline const TCHAR* ClothesPart(int32 I)
	{
		static const TCHAR* Names[] = { TEXT("上衣"), TEXT("下著"), TEXT("胸罩"), TEXT("內褲"), TEXT("手套"), TEXT("褲襪"), TEXT("襪子"), TEXT("室內鞋"), TEXT("室外鞋") };
		return Names[FMath::Clamp(I, 0, 8)];
	}

	inline const TCHAR* ClothesStateName(uint8 S) { return S == 0 ? TEXT("穿") : (S == 1 ? TEXT("半脫") : TEXT("脫")); }
}

class SOKStudioPanel;

/** The scene area: left click picks (Ctrl / Shift add), left drag orbits (right drag too; in fly mode it turns the camera
 *  and W A S D Q E move it), middle drag pans, wheel zooms; the guide, pose points, the look point and the selected
 *  light's reach are drawn over it. */
class SOKStudioView : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SOKStudioView) {}
		SLATE_ARGUMENT(TWeakObjectPtr<UOKStudioGame>, Game)
		SLATE_ARGUMENT(SOKStudioPanel*, Owner)
	SLATE_END_ARGS()

	void Construct(const FArguments& Args)
	{
		Game = Args._Game;
		Owner = Args._Owner;
		ChildSlot[SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("NoBorder"))];
	}

	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event) override;
	virtual FReply OnMouseButtonUp(const FGeometry& Geometry, const FPointerEvent& Event) override;
	virtual FReply OnMouseMove(const FGeometry& Geometry, const FPointerEvent& Event) override;
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& Culling, FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const override;
	virtual FReply OnMouseWheel(const FGeometry&, const FPointerEvent& Event) override
	{
		if (Game.IsValid()) Game->Zoom(Event.GetWheelDelta());
		return FReply::Handled();
	}
	virtual void OnMouseCaptureLost(const FCaptureLostEvent&) override
	{
		if (Game.IsValid()) { if (DragAxis >= 0) Game->EndGuideDrag(); Game->bFlying = false; Game->FlyKeys.Reset(); Game->bUserDragging = false; }
		DragAxis = -1; bLookDrag = false; Button = FKey();
	}

private:
	TWeakObjectPtr<UOKStudioGame> Game;
	SOKStudioPanel* Owner = nullptr;
	FKey Button;
	int32 DragAxis = -1;
	bool bLookDrag = false;
	FVector2D PressAt = FVector2D::ZeroVector;
	double Moved = 0.;
};

/** Studio shortcuts before Slate routes the key (they work whatever widget has focus; text fields are skipped). */
class FOKStudioKeyProcessor : public IInputProcessor
{
public:
	TFunction<bool(const FKeyEvent&)> OnKey;
	TFunction<bool(const FKeyEvent&)> OnKeyUp;
	virtual void Tick(const float, FSlateApplication&, TSharedRef<ICursor>) override {}
	virtual bool HandleKeyDownEvent(FSlateApplication&, const FKeyEvent& Event) override { return OnKey && OnKey(Event); }
	virtual bool HandleKeyUpEvent(FSlateApplication&, const FKeyEvent& Event) override { return OnKeyUp && OnKeyUp(Event); }
	virtual const TCHAR* GetDebugName() const override { return TEXT("OKStudioKeys"); }
};

/** Objects dragged from the workspace tree (onto another object: it becomes their parent; onto a body part: they hang on it). */
class FOKStudioRowDrag : public FDragDropOperation
{
public:
	DRAG_DROP_OPERATOR_TYPE(FOKStudioRowDrag, FDragDropOperation)
	TArray<int32> Ids;
	FString Label;
	/** Whether the place under the mouse takes them (the decorator turns green or red). */
	bool bCanDrop = false;
	bool bOverTarget = false;
	static TSharedRef<FOKStudioRowDrag> New(const TArray<int32>& Ids, const FString& Label)
	{
		TSharedRef<FOKStudioRowDrag> Op = MakeShared<FOKStudioRowDrag>();
		Op->Ids = Ids;
		Op->Label = Label;
		Op->Construct();
		return Op;
	}
	virtual TSharedPtr<SWidget> GetDefaultDecorator() const override
	{
		return SNew(SBorder).Padding(FMargin(6.f, 2.f))
			.BorderImage_Lambda([this] { return OKStudioStyle::Brush(!bOverTarget ? OKStudioStyle::Panel() : bCanDrop ? FLinearColor(0.1f, 0.55f, 0.15f, 0.95f) : FLinearColor(0.6f, 0.1f, 0.1f, 0.95f)); })
			[OKStudioStyle::Label(FText::FromString(Label), 11.f)];
	}
};

/** A drop place outside the tree (a body part of the group panel). */
class SOKStudioDropArea : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SOKStudioDropArea) {}
		SLATE_DEFAULT_SLOT(FArguments, Content)
		SLATE_ARGUMENT(TFunction<bool(const TArray<int32>&)>, CanDrop)
		SLATE_ARGUMENT(TFunction<void(const TArray<int32>&)>, OnDrop)
	SLATE_END_ARGS()
	void Construct(const FArguments& Args) { CanDropFn = Args._CanDrop; OnDropFn = Args._OnDrop; ChildSlot[Args._Content.Widget]; }
	virtual void OnDragEnter(const FGeometry&, const FDragDropEvent& E) override { if (TSharedPtr<FOKStudioRowDrag> Op = E.GetOperationAs<FOKStudioRowDrag>()) { Op->bOverTarget = true; Op->bCanDrop = CanDropFn && CanDropFn(Op->Ids); } }
	virtual void OnDragLeave(const FDragDropEvent& E) override { if (TSharedPtr<FOKStudioRowDrag> Op = E.GetOperationAs<FOKStudioRowDrag>()) Op->bOverTarget = false; }
	virtual FReply OnDrop(const FGeometry&, const FDragDropEvent& E) override
	{
		TSharedPtr<FOKStudioRowDrag> Op = E.GetOperationAs<FOKStudioRowDrag>();
		if (!Op || !CanDropFn || !CanDropFn(Op->Ids)) return FReply::Unhandled();
		if (OnDropFn) OnDropFn(Op->Ids);
		return FReply::Handled();
	}
private:
	TFunction<bool(const TArray<int32>&)> CanDropFn;
	TFunction<void(const TArray<int32>&)> OnDropFn;
};

/** Outliner, object properties and scene buttons. */
class SOKStudioPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SOKStudioPanel) {}
		SLATE_ARGUMENT(TWeakObjectPtr<UOKStudioGame>, Game)
	SLATE_END_ARGS()

	struct FRow { int32 Id = -1; int32 Depth = 0; };

	/** Detail windows (one at a time, HS2Studio6's menu windows) and their spec rects (canvas units, top-left anchored). */
	enum class EPage : uint8 { None, Object, State, Bones, Anim, Scene, Files, CharLoader, ItemAdd, LightAdd, Camera };

	struct FMenuEntry { FString Label; FString Command; TFunction<bool()> On; bool bEnabled = true; };

	virtual ~SOKStudioPanel() override
	{
		if (KeyProcessor && FSlateApplication::IsInitialized()) FSlateApplication::Get().UnregisterInputPreProcessor(KeyProcessor);
		SaveLayout();
		if (PreviewActor.IsValid()) PreviewActor->Destroy();
	}

	void Construct(const FArguments& Args)
	{
		Game = Args._Game;
		for (const FString& Card : UOKStudioGame::AvailableCards()) Cards.Add(MakeShared<FString>(Card));
		RegisterCommands();
		using namespace OKStudioStyle;
		// Canvas anchoring of the spec: left groups from the top-left, right groups from the top-right (the spec's right
		// edge is 1768.9 units at its 2194x1010 capture), the transform bar from the bottom-left.
		constexpr float SpecRight = 1768.9f;
		auto Right = [](float X) { return X - SpecRight; };
		ChildSlot
		[
			SNew(SOverlay)
			+ SOverlay::Slot()[SAssignNew(View, SOKStudioView).Game(Game).Owner(this)]
			+ SOverlay::Slot()
			[
				SNew(SDPIScaler).Visibility_Lambda([this] { return bHideUi ? EVisibility::Collapsed : EVisibility::SelfHitTestInvisible; })
				.DPIScale_Lambda([this]
				{
					// Unity CanvasScaler (match 0.5) on the window's pixels, divided by the scale Slate already applies.
					const FGeometry& G = GetTickSpaceGeometry();
					const float Pixel = G.Scale > 0.f ? G.Scale : 1.f;
					return CanvasScale(FVector2D(G.GetLocalSize()) * Pixel) / Pixel * FMath::Clamp(FOKStudioSettings::Get().UiScale, 0.5f, 2.f);
				})
				[
					SAssignNew(Canvas, SConstraintCanvas).Visibility(EVisibility::SelfHitTestInvisible)
					+ SConstraintCanvas::Slot().Alignment(FVector2D(0.f, 0.f)).Anchors(FAnchors(0.f, 0.f)).Offset(FMargin(7.f, 7.f, 35.f, 177.f))[Tagged(TEXT("RootToolbar"), MakeRootToolbar())]
					+ SConstraintCanvas::Slot().Alignment(FVector2D(0.f, 0.f)).Anchors(FAnchors(0.f, 0.f)).Offset(TAttribute<FMargin>::CreateLambda([this] { return FMargin(47.f, 7.f, 80.f, SubMenuHeight()); }))
						[Tagged(TEXT("SubMenu"), SNew(SBox).Visibility_Lambda([this] { return RootMenu >= 0 && bShowRoot ? EVisibility::Visible : EVisibility::Collapsed; })[SAssignNew(SubMenuBody, SBox)])]
					+ SConstraintCanvas::Slot().Alignment(FVector2D(0.f, 0.f)).Anchors(FAnchors(0.f, 0.f)).Offset(FMargin(4.f, 420.f, 33.f, 33.f))
						[Tagged(TEXT("PoseLibraryButton"), IconButton(TEXT("pose"), Tip(TEXT("姿勢庫"), TEXT("app.poselibrary")), Cmd(TEXT("app.poselibrary")), 33.f, 33.f, TAttribute<bool>::CreateLambda([this] { return IsFloatOpen(TEXT("poselib")); })))]
					+ SConstraintCanvas::Slot().Alignment(FVector2D(0.f, 0.f)).Anchors(FAnchors(0.f, 0.f)).Offset(FMargin(4.f, 458.f, 33.f, 33.f))
						[Tagged(TEXT("QuickSearchButton"), IconButton(TEXT("search"), Tip(TEXT("快搜"), TEXT("app.quicksearch")), Cmd(TEXT("app.quicksearch")), 33.f, 33.f, TAttribute<bool>::CreateLambda([this] { return IsFloatOpen(TEXT("quick")); })))]
					+ SConstraintCanvas::Slot().Alignment(FVector2D(0.f, 0.f)).Anchors(FAnchors(1.f, 0.f)).Offset(FMargin(Right(1448.9f), 7.f, 313.f, 33.f))[Tagged(TEXT("TopTools"), MakeTopTools())]
					+ SConstraintCanvas::Slot().Alignment(FVector2D(0.f, 0.f)).Anchors(FAnchors(1.f, 0.f)).Offset(FMargin(Right(1284.9f), 47.f, 240.f, 429.f))
						[Tagged(TEXT("CharacterGroupPanel"), SNew(SBox).Visibility_Lambda([this] { return bShowGroups ? EVisibility::Visible : EVisibility::Collapsed; })[MakeGroupPanel()])]
					+ SConstraintCanvas::Slot().Alignment(FVector2D(0.f, 0.f)).Anchors(FAnchors(1.f, 0.f)).Offset(FMargin(Right(1528.9f), 47.f, 240.f, 429.f))
						[Tagged(TEXT("TreePanel"), SNew(SBox).Visibility_Lambda([this] { return bShowWorkspace ? EVisibility::Visible : EVisibility::Collapsed; })[MakeTreePanel()])]
					+ SConstraintCanvas::Slot().Alignment(FVector2D(0.f, 0.f)).Anchors(FAnchors(0.f, 1.f)).Offset(FMargin(0.f, -60.f, 478.f, 60.f))
						[Tagged(TEXT("TransformBar"), SNew(SBox).Visibility_Lambda([this] { return bShowTransform ? EVisibility::Visible : EVisibility::Collapsed; })[MakeTransformBar()])]
					+ SConstraintCanvas::Slot().Expose(DetailSlot).Alignment(FVector2D(0.f, 0.f)).Anchors(FAnchors(0.f, 0.f)).Offset(TAttribute<FMargin>::CreateLambda([this] { return PageRect(); }))
						[Tagged(TEXT("DetailWindow"), SNew(SBox).Visibility_Lambda([this] { return Page == EPage::None ? EVisibility::Collapsed : EVisibility::Visible; })
							[SNew(SFrontArea).OnFront([this] { BringToFront(DetailSlot); })
								[Window(TAttribute<FText>::CreateLambda([this] { return FText::FromString(PageTitle()); }), SAssignNew(DetailBody, SBox), [this] { SetPage(EPage::None); }, Panel(),
									[this](const FVector2D& D) { DetailOffsets.FindOrAdd(uint8(Page)) += D; bLayoutDirty = true; })]])]
					+ SConstraintCanvas::Slot().Alignment(FVector2D(0.f, 0.f)).Anchors(FAnchors(0.f, 1.f)).Offset(FMargin(490.f, -20.f, 900.f, 18.f)).ZOrder(1000.f)
						[SAssignNew(Status, STextBlock).Font(Font(10.f)).ColorAndOpacity(TextHint()).ShadowOffset(FVector2D(1.f, 1.f))]
				]
			]
		];
		RegisterFloats();
		LoadLayout();
		RebuildSubMenu();
		RebuildDetailBody();
		RefreshRows();
		if (Game.IsValid() && Game->FindMmdCameraId(true) >= 0) OpenFloat(TEXT("mmd"), false);
		KeyProcessor = MakeShared<FOKStudioKeyProcessor>();
		KeyProcessor->OnKey = [this](const FKeyEvent& E) { return HandleKey(E); };
		KeyProcessor->OnKeyUp = [this](const FKeyEvent& E) { return HandleKeyUp(E); };
		FSlateApplication::Get().RegisterInputPreProcessor(KeyProcessor);
	}

	/** Names a shell part for OKStudio.UiLayout (the spec's object names). */
	static TSharedRef<SWidget> Tagged(const TCHAR* Name, const TSharedRef<SWidget>& Widget)
	{
		Widget->AddMetadata<FTagMetaData>(MakeShared<FTagMetaData>(FName(Name)));
		return Widget;
	}

	// --- Commands: every button, shortcut, OKStudio.Cmd and (later) the control channel run the same table. ---

	struct FCommand { FString Label; TFunction<bool(const FString&)> Run; };
	TMap<FString, FCommand> Commands;
	TArray<FString> CommandOrder;

	void AddCommand(const FString& Id, const FString& Label, TFunction<bool(const FString&)> Run) { Commands.Add(Id, { Label, MoveTemp(Run) }); CommandOrder.Add(Id); }
	TFunction<void()> Cmd(const FString& Id, const FString& Arg = FString()) { return [this, Id, Arg] { RunCommand(Id, Arg); }; }

	bool RunCommand(const FString& Id, const FString& Arg = FString())
	{
		FCommand* C = Commands.Find(Id);
		return C && Game.IsValid() && C->Run(Arg);
	}

	void RegisterCommands()
	{
		auto Simple = [this](const FString& Id, const FString& Label, TFunction<void()> Do) { AddCommand(Id, Label, [Do](const FString&) { Do(); return true; }); };
		auto Page_ = [this](const FString& Id, const FString& Label, EPage P) { AddCommand(Id, Label, [this, P](const FString&) { SetPage(Page == P ? EPage::None : P); return true; }); };
		auto Float_ = [this](const FString& Id, const FString& Label, const FString& Window) { AddCommand(Id, Label, [this, Window](const FString&) { OpenFloat(Window, true); return true; }); };
		// Scene files.
		Simple(TEXT("scene.new"), TEXT("新場景"), [this]
		{
			GuardUnsaved([this]
			{
				Game->ReleaseSceneResources();
				Game->Document->Objects.Reset(); Game->Document->Tracks.Reset(); Game->Document->Bookmarks.Reset(); Game->Document->NextId = 1; Game->Document->ActiveCamera = -1;
				Game->Document->Map = -1; Game->Document->Post = FOKScenePost(); Game->File.Reset();
				Game->Add(EOKSceneObjectKind::Light); Game->Document->NotifyChanged();
				Game->ResetHistory(); Game->bDirty = false;
			});
		});
		AddCommand(TEXT("scene.save"), TEXT("儲存"), [this](const FString& Arg)
		{
			if (Arg.IsEmpty() && !Game->File.IsEmpty() && FOKSceneCard::Kind(Game->File) != FOKSceneCard::EKind::KStudio) { FString E; const bool bOk = Game->Save(Game->File, E); SetStatus(bOk ? TEXT("已存 ") + Game->File : E); return bOk; }
			return SaveNamed(Arg.IsEmpty() ? FDateTime::Now().ToString(TEXT("scene_%Y%m%d_%H%M%S")) : Arg);
		});
		Simple(TEXT("scene.saveas"), TEXT("另存新檔"), [this] { bSceneSaveMode = true; OpenFloat(TEXT("scenes")); });
		Simple(TEXT("scene.browser"), TEXT("開啟場景"), [this] { bSceneSaveMode = false; OpenFloat(TEXT("scenes"), true); });
		AddCommand(TEXT("scene.load"), TEXT("讀取"), [this](const FString& Arg) { return !Arg.IsEmpty() && LoadFile(Arg); });
		AddCommand(TEXT("scene.merge"), TEXT("疊加"), [this](const FString& Arg) { return !Arg.IsEmpty() && OpenScene(Arg, true); });
		AddCommand(TEXT("scene.import"), TEXT("匯入 KStudio"), [this](const FString& Arg) { if (Arg.IsEmpty()) { bSceneSaveMode = false; SceneFolder = KStudioFolder(); OpenFloat(TEXT("scenes")); return true; } return ImportKStudio(Arg); });
		AddCommand(TEXT("scene.backup"), TEXT("立即備份"), [this](const FString&) { const FString F = Game->BackupNow(true); SetStatus(F.IsEmpty() ? FString(TEXT("備份失敗")) : TEXT("已備份 ") + F); return !F.IsEmpty(); });
		Float_(TEXT("scene.backups"), TEXT("備份恢復"), TEXT("backups"));
		Float_(TEXT("scene.report"), TEXT("讀取報告"), TEXT("report"));
		Float_(TEXT("scene.resources"), TEXT("資源檢查"), TEXT("resources"));
		// Adding objects.
		Simple(TEXT("add.folder"), TEXT("資料夾"), [this] { Game->Add(EOKSceneObjectKind::Folder); });
		AddCommand(TEXT("add.character"), TEXT("角色"), [this](const FString& Arg)
		{
			if (Arg.IsEmpty()) { OpenFloat(TEXT("cards")); return true; }
			return AddOrReplaceCard(Arg, false);
		});
		AddCommand(TEXT("add.item"), TEXT("道具"), [this](const FString& Arg)
		{
			TArray<FString> P; Arg.Replace(TEXT("/"), TEXT(" ")).ParseIntoArray(P, TEXT(" "));
			if (P.Num() < 3) { SetPage(EPage::ItemAdd); return true; }
			return AddItemTo(FCString::Atoi(*P[0]), FCString::Atoi(*P[1]), FCString::Atoi(*P[2]), P.Num() > 3 && P[3] == TEXT("child"));
		});
		AddCommand(TEXT("add.light"), TEXT("燈光"), [this](const FString& Arg)
		{
			const int32 Id = Game->Add(EOKSceneObjectKind::Light);
			if (FOKSceneObject* O = Game->Document->Find(Id))
			{
				O->Light.Type = Arg == TEXT("spot") ? EOKSceneLightType::Spot : Arg == TEXT("point") ? EOKSceneLightType::Point : EOKSceneLightType::Directional;
				O->Name = Arg == TEXT("spot") ? TEXT("聚光") : Arg == TEXT("point") ? TEXT("點光") : TEXT("方向光");
				Game->Document->NotifyChanged();
			}
			return true;
		});
		Simple(TEXT("add.camera"), TEXT("相機"), [this] { Game->CameraFromView(); });
		AddCommand(TEXT("add.effect"), TEXT("特效"), [this](const FString& Arg)
		{
			if (Arg.IsEmpty()) { OpenFloat(TEXT("effects")); return true; }
			const FOKStudioEffect* E = FOKStudioCatalog::Get().FindEffect(Arg);
			if (!E) return false;
			const int32 Id = Game->Add(EOKSceneObjectKind::Effect);
			if (FOKSceneObject* O = Game->Document->Find(Id)) { O->Effect.Effect = E->Id; O->Name = E->Name; Game->Document->NotifyChanged(); }
			FOKStudioSettings::Get().UseRecent(TEXT("effect:") + E->Id);
			return true;
		});
		Float_(TEXT("add.map"), TEXT("地圖"), TEXT("maps"));
		// Editing the selection.
		Simple(TEXT("edit.duplicate"), TEXT("複製物件"), [this] { DuplicateSelection(); });
		Simple(TEXT("edit.copy"), TEXT("拷貝"), [this] { CopySelection(); });
		AddCommand(TEXT("edit.paste"), TEXT("貼上"), [this](const FString&) { return PasteClipboard(); });
		Simple(TEXT("edit.root"), TEXT("移到根層"), [this] { for (int32 Id : Game->SelectionRoots()) { if (FOKSceneObject* O = Game->Document->Find(Id)) O->AttachBone.Reset(); Game->Document->Reparent(Id, -1); } });
		Simple(TEXT("edit.parent"), TEXT("設為父物件"), [this] { ParentSelectionToPrimary(); });
		Simple(TEXT("edit.delete"), TEXT("刪除"), [this] { DeleteSelection(); });
		Simple(TEXT("edit.hide"), TEXT("隱藏／顯示"), [this] { HideSelection(); });
		Simple(TEXT("edit.rename"), TEXT("改名"), [this] { BeginRename(); });
		Simple(TEXT("edit.selectall"), TEXT("全選"), [this] { TArray<int32> All; for (const FOKSceneObject& O : Game->Document->Objects) All.Add(O.Id); Game->SelectMany(All, Game->SelectedId); });
		Simple(TEXT("edit.saveas"), TEXT("選取存成場景"), [this] { const FString F = SaveSelectionAsScene(); SetStatus(F.IsEmpty() ? FString(TEXT("沒有選取")) : TEXT("已存 ") + F); });
		Simple(TEXT("edit.undo"), TEXT("復原"), [this] { Game->Undo(); });
		Simple(TEXT("edit.redo"), TEXT("重做"), [this] { Game->Redo(); });
		AddCommand(TEXT("select"), TEXT("選取"), [this](const FString& Arg)
		{
			TArray<FString> P; Arg.ParseIntoArray(P, TEXT(" "));
			TArray<int32> Ids; for (const FString& S : P) Ids.Add(FCString::Atoi(*S));
			Game->SelectMany(Ids, Ids.Num() ? Ids.Last() : -1);
			return true;
		});
		// Guide.
		Simple(TEXT("gizmo.move"), TEXT("移動"), [this] { Game->GuideMode = 0; });
		Simple(TEXT("gizmo.rotate"), TEXT("旋轉"), [this] { Game->GuideMode = 1; });
		Simple(TEXT("gizmo.scale"), TEXT("縮放"), [this] { Game->GuideMode = 2; });
		Simple(TEXT("gizmo.axis"), TEXT("主軸"), [this] { Game->bShowGuide = !Game->bShowGuide; });
		Simple(TEXT("gizmo.local"), TEXT("局部座標"), [this] { Game->bLocalAxes = !Game->bLocalAxes; SetStatus(Game->bLocalAxes ? TEXT("軸：局部") : TEXT("軸：世界")); });
		Simple(TEXT("gizmo.snap"), TEXT("吸附"), [this] { Game->bSnap = !Game->bSnap; SetStatus(Game->bSnap ? TEXT("吸附：開") : TEXT("吸附：關（按住 Ctrl 也會吸附）")); });
		// View and cameras.
		Simple(TEXT("camera.use"), TEXT("用此相機"), [this] { const FOKSceneObject* O = Obj(); Game->Document->ActiveCamera = O && O->Kind == EOKSceneObjectKind::Camera ? O->Id : -1; });
		Simple(TEXT("camera.free"), TEXT("自由視角"), [this] { Game->bLockActiveCamera = false; Game->Document->ActiveCamera = -1; SetStatus(TEXT("視角：自由視角（已解除鏡頭鎖定）")); });
		Simple(TEXT("camera.mmd"), TEXT("MMD 系統鏡頭"), [this]
		{
			const int32 CamId = Game->FindMmdCameraId(false);
			if (CamId >= 0)
			{
				Game->Document->ActiveCamera = CamId;
				Game->bLockActiveCamera = true;
				const FOKSceneObject* C = Game->Document->Find(CamId);
				SetStatus(FString::Printf(TEXT("已啟用並鎖定 MMD 系統鏡頭：%s (#%d)"), C ? *C->Name : TEXT("Camera"), CamId));
			}
			else SetStatus(TEXT("場景中沒有相機物件"));
		});
		Simple(TEXT("camera.lock"), TEXT("鎖定系統鏡頭"), [this]
		{
			Game->bLockActiveCamera = !Game->bLockActiveCamera;
			SetStatus(Game->bLockActiveCamera ? TEXT("鏡頭鎖定：開（滑鼠拖曳不脫離系統鏡頭）") : TEXT("鏡頭鎖定：關"));
		});
		Simple(TEXT("camera.focus"), TEXT("聚焦所選"), [this] { Game->FocusSelection(); });
		Simple(TEXT("camera.reset"), TEXT("初始視角"), [this] { Game->ViewPreset(5); });
		Simple(TEXT("camera.front"), TEXT("前視"), [this] { Game->ViewPreset(0); });
		Simple(TEXT("camera.side"), TEXT("側視"), [this] { Game->ViewPreset(1); });
		Simple(TEXT("camera.top"), TEXT("俯視"), [this] { Game->ViewPreset(2); });
		Simple(TEXT("camera.back"), TEXT("後視"), [this] { Game->ViewPreset(3); });
		Simple(TEXT("camera.left"), TEXT("左視"), [this] { Game->ViewPreset(4); });
		Simple(TEXT("camera.fromview"), TEXT("從視角建相機"), [this] { Game->CameraFromView(); });
		Simple(TEXT("camera.store"), TEXT("視角存到所選相機"), [this] { if (!Game->StoreViewInCamera(Game->SelectedId)) SetStatus(TEXT("先選一個相機")); });
		Simple(TEXT("camera.next"), TEXT("下一相機"), [this] { Game->NextCamera(); });
		Simple(TEXT("camera.fly"), TEXT("自由飛行"), [this] { FOKStudioSettings& S = FOKStudioSettings::Get(); S.bFlyMode = !S.bFlyMode; S.Save(); SetStatus(S.bFlyMode ? TEXT("自由飛行：按住右鍵用 W A S D Q E") : TEXT("自由飛行：關")); });
		for (int32 N = 0; N < 10; ++N)
		{
			Simple(FString::Printf(TEXT("view.save.%d"), N), FString::Printf(TEXT("存視角 %d"), N), [this, N] { Game->SaveBookmark(N); SetStatus(FString::Printf(TEXT("視角 %d 已存"), N)); });
			Simple(FString::Printf(TEXT("view.load.%d"), N), FString::Printf(TEXT("讀視角 %d"), N), [this, N] { if (!Game->LoadBookmark(N)) SetStatus(FString::Printf(TEXT("視角 %d 還沒存"), N)); });
		}
		// Application.
		Simple(TEXT("app.quit"), TEXT("離開"), [this] { GuardUnsaved([this] { Game->RequestStop(); }); });
		Float_(TEXT("app.settings"), TEXT("設定"), TEXT("settings"));
		Float_(TEXT("app.screenshot"), TEXT("截圖"), TEXT("shot"));
		Float_(TEXT("app.quicksearch"), TEXT("快搜"), TEXT("quick"));
		Float_(TEXT("app.poselibrary"), TEXT("姿勢庫"), TEXT("poselib"));
		Float_(TEXT("app.story"), TEXT("播放故事"), TEXT("stories"));
		AddCommand(TEXT("window.open"), TEXT("開視窗"), [this](const FString& Arg) { if (!Floats.Contains(Arg)) return false; OpenFloat(Arg); return true; });
		AddCommand(TEXT("window.close"), TEXT("關視窗"), [this](const FString& Arg) { if (!Floats.Contains(Arg)) return false; CloseFloat(Arg); return true; });
		for (int32 M = 0; M < 4; ++M)
		{
			static const TCHAR* Ids[] = { TEXT("menu.add"), TEXT("menu.character"), TEXT("menu.sound"), TEXT("menu.system") };
			static const TCHAR* Labels[] = { TEXT("加入"), TEXT("角色"), TEXT("音效"), TEXT("系統") };
			Simple(Ids[M], Labels[M], [this, M] { RootMenu = RootMenu == M ? -1 : M; DeferSubMenu(); });
		}
		Page_(TEXT("page.object"), TEXT("屬性"), EPage::Object);
		Page_(TEXT("page.state"), TEXT("狀態"), EPage::State);
		Page_(TEXT("page.bones"), TEXT("骨骼"), EPage::Bones);
		Page_(TEXT("page.anim"), TEXT("動畫"), EPage::Anim);
		Page_(TEXT("page.scene"), TEXT("畫面設定"), EPage::Scene);
		Simple(TEXT("page.files"), TEXT("讀取／儲存"), [this] { bSceneSaveMode = false; OpenFloat(TEXT("scenes"), true); });
		Simple(TEXT("page.charloader"), TEXT("角色載入"), [this] { OpenFloat(TEXT("cards"), true); });
		Page_(TEXT("page.items"), TEXT("道具目錄"), EPage::ItemAdd);
		Page_(TEXT("page.lights"), TEXT("添加光源"), EPage::LightAdd);
		Page_(TEXT("page.camera"), TEXT("相機"), EPage::Camera);
		Simple(TEXT("page.close"), TEXT("關閉視窗"), [this] { SetPage(EPage::None); });
		Simple(TEXT("page.pose"), TEXT("姿勢檔"), [this] { BonePage = 6; SetPage(EPage::Bones); });
		AddCommand(TEXT("bones.page"), TEXT("骨骼分頁"), [this](const FString& Arg) { BonePage = FMath::Clamp(FCString::Atoi(*Arg), 0, 6); if (Page != EPage::Bones) SetPage(EPage::Bones); else DeferRebuild(); return true; });
		AddCommand(TEXT("scene.tab"), TEXT("畫面設定分頁"), [this](const FString& Arg) { SceneTab = FMath::Clamp(FCString::Atoi(*Arg), 0, 7); if (Page != EPage::Scene) SetPage(EPage::Scene); else DeferRebuild(); return true; });
		Simple(TEXT("toggle.root"), TEXT("選單"), [this] { bShowRoot = !bShowRoot; bLayoutDirty = true; });
		Simple(TEXT("toggle.workspace"), TEXT("工作區"), [this] { bShowWorkspace = !bShowWorkspace; bLayoutDirty = true; });
		Simple(TEXT("toggle.groups"), TEXT("分組"), [this] { bShowGroups = !bShowGroups; bLayoutDirty = true; });
		Simple(TEXT("toggle.transform"), TEXT("變換列"), [this] { bShowTransform = !bShowTransform; bLayoutDirty = true; });
		Simple(TEXT("toggle.timeline"), TEXT("時間軸"), [this] { OpenFloat(TEXT("timeline"), true); });
		Simple(TEXT("toggle.mmd"), TEXT("MMD 導演"), [this] { OpenFloat(TEXT("mmd"), true); });
		Simple(TEXT("toggle.ui"), TEXT("隱藏介面"), [this] { bHideUi = !bHideUi; });
		Simple(TEXT("tree.expand"), TEXT("展開全部"), [this] { Collapsed.Reset(); RefreshRows(); });
		Simple(TEXT("tree.collapse"), TEXT("收起全部"), [this] { for (const FOKSceneObject& O : Game->Document->Objects) if (Game->Document->Children(O.Id).Num()) Collapsed.Add(O.Id); RefreshRows(); });
		Simple(TEXT("tree.locate"), TEXT("定位當前"), [this] { LocateSelection(); });
		AddCommand(TEXT("tree.filter"), TEXT("依種類篩選"), [this](const FString& Arg) { KindFilter = Arg.IsEmpty() ? (KindFilter >= 5 ? -1 : KindFilter + 1) : FCString::Atoi(*Arg); RefreshRows(); return true; });
		AddCommand(TEXT("group.attach"), TEXT("掛到所選部位"), [this](const FString& Arg) { return AttachToPoint(Arg.IsEmpty() ? GroupPoint : Arg); });
		Simple(TEXT("transform.copy"), TEXT("複製變換"), [this] { if (const FOKSceneObject* O = Obj()) { CopiedTransform = O->Transform; bHasCopiedTransform = true; } });
		Simple(TEXT("transform.paste"), TEXT("貼上變換"), [this] { if (!bHasCopiedTransform) return; for (int32 Id : Game->Selection) if (FOKSceneObject* O = Game->Document->Find(Id)) O->Transform = CopiedTransform; Changed(); });
		AddCommand(TEXT("transform.move"), TEXT("移動（x y z cm）"), [this](const FString& Arg)
		{
			TArray<FString> P; Arg.ParseIntoArrayWS(P);
			if (P.Num() != 3 || Game->Selection.IsEmpty()) return false;
			const FVector D(FCString::Atof(*P[0]), FCString::Atof(*P[1]), FCString::Atof(*P[2]));
			for (int32 Id : Game->SelectionRoots()) if (FOKSceneObject* O = Game->Document->Find(Id)) O->Transform.AddToTranslation(D);
			Changed();
			return true;
		});
		Simple(TEXT("transform.reset"), TEXT("重設變換"), [this] { for (int32 Id : Game->Selection) if (FOKSceneObject* O = Game->Document->Find(Id)) O->Transform = FTransform::Identity; Changed(); });
		// Timeline.
		Simple(TEXT("timeline.key"), TEXT("打 key"), [this] { if (TimelineView) TimelineView->KeySelection(); else for (int32 Id : Game->Selection) Game->Document->SetKey(Id, Game->TimelineTime); });
		Simple(TEXT("timeline.scenekey"), TEXT("場景 key"), [this] { Game->Document->SetKey(UOKSceneDocument::SceneTrackId, Game->TimelineTime); });
		Simple(TEXT("timeline.delete"), TEXT("刪 key"), [this] { if (TimelineView && TimelineView->Selected.Num()) TimelineView->DeleteSelected(); else for (int32 Id : Game->Selection) Game->Document->RemoveKey(Id, Game->TimelineTime); });
		Simple(TEXT("timeline.copy"), TEXT("複製 key"), [this] { if (TimelineView) TimelineView->CopySelected(); });
		Simple(TEXT("timeline.paste"), TEXT("貼上 key"), [this] { if (TimelineView) TimelineView->PasteAtPlayhead(); });
		Simple(TEXT("timeline.play"), TEXT("播放／停止"), [this] { Game->bTimelinePlaying = !Game->bTimelinePlaying; });
		Simple(TEXT("timeline.prev"), TEXT("上一幀"), [this] { ScrubTo(Game->TimelineTime - 1.f / 30.f); });
		Simple(TEXT("timeline.next"), TEXT("下一幀"), [this] { ScrubTo(Game->TimelineTime + 1.f / 30.f); });
		Simple(TEXT("timeline.first"), TEXT("開頭"), [this] { ScrubTo(0.f); });
		Simple(TEXT("timeline.last"), TEXT("結尾"), [this] { ScrubTo(Game->Document->Duration); });
		AddCommand(TEXT("timeline.time"), TEXT("播放頭"), [this](const FString& Arg) { ScrubTo(FCString::Atof(*Arg)); return true; });
		AddCommand(TEXT("timeline.duration"), TEXT("時長"), [this](const FString& Arg) { const float D = FCString::Atof(*Arg); if (D <= 0.f) return false; Game->Document->Duration = D; Changed(); return true; });
		AddCommand(TEXT("timeline.ease"), TEXT("曲線"), [this](const FString& Arg) { if (!TimelineView) return false; TimelineView->SetEase(EOKSceneEase(FMath::Clamp(FCString::Atoi(*Arg), 0, 4))); return true; });
		Float_(TEXT("timeline.export"), TEXT("輸出"), TEXT("export"));
		// Animation files and video motion pairs.
		AddCommand(TEXT("anim.file"), TEXT("播放 OK 動畫檔"), [this](const FString& Arg) { FOKSceneObject* X = Obj(); return X && !Arg.IsEmpty() && PickAnimFile(*X, Arg); });
		AddCommand(TEXT("anim.pair"), TEXT("雙人影片動作"), [this](const FString& Arg) { return PickVideoPair(Arg); });
		// Pose.
		AddCommand(TEXT("pose.mirror"), TEXT("鏡像姿勢"), [this](const FString& Arg) { FOKSceneObject* X = Obj(); return X && MirrorPose(*X, Arg == TEXT("l2r") ? 1 : Arg == TEXT("r2l") ? 2 : 0); });
		Simple(TEXT("pose.ikfromfk"), TEXT("IK←FK"), [this] { if (FOKSceneObject* X = Obj()) IkFromFk(*X); });
		Simple(TEXT("pose.fkfrompose"), TEXT("FK←目前姿勢"), [this] { if (FOKSceneObject* X = Obj()) FkFromPose(*X); });
		Simple(TEXT("pose.copy"), TEXT("複製姿勢"), [this] { if (const FOKSceneObject* X = Obj(); X && X->Kind == EOKSceneObjectKind::Character) { PoseClipFk = X->Character.Fk; PoseClipIk = X->Character.Ik; bHasPoseClip = true; SetStatus(TEXT("已複製姿勢")); } });
		Simple(TEXT("pose.paste"), TEXT("貼上姿勢"), [this] { if (!bHasPoseClip) return; for (int32 Id : Game->Selection) if (FOKSceneObject* X = Game->Document->Find(Id); X && X->Kind == EOKSceneObjectKind::Character) { X->Character.Fk = PoseClipFk; X->Character.Ik = PoseClipIk; } Changed(); DeferRebuild(); });
	}

	/** Studio shortcuts, whatever has focus (except text fields and while a story plays); the settings' bindings. */
	bool HandleKey(const FKeyEvent& E)
	{
		if (!Game.IsValid() || Game->IsStoryMode() || !FSlateApplication::IsInitialized()) return false;
		const FKey K = E.GetKey();
		// A shortcut being rebound in the settings window takes this key (modifiers alone wait for the key).
		if (!RebindingId.IsEmpty())
		{
			if (K == EKeys::LeftControl || K == EKeys::RightControl || K == EKeys::LeftShift || K == EKeys::RightShift || K == EKeys::LeftAlt || K == EKeys::RightAlt) return true;
			if (K != EKeys::Escape) { FOKStudioSettings& S = FOKStudioSettings::Get(); S.Bindings.Add(RebindingId, FOKStudioSettings::ChordText(E)); S.Save(); }
			RebindingId.Reset();
			DeferFloat(TEXT("settings"));
			return true;
		}
		const TSharedPtr<SWidget> Focus = FSlateApplication::Get().GetKeyboardFocusedWidget();
		if (Focus && (Focus->GetType() == TEXT("SEditableText") || Focus->GetType() == TEXT("SMultiLineEditableText"))) return false;
		// Flying: the movement keys are held, not commands.
		if (Game->bFlying && (K == EKeys::W || K == EKeys::A || K == EKeys::S || K == EKeys::D || K == EKeys::Q || K == EKeys::E)) { Game->FlyKeys.Add(K); return true; }
		// The timeline's own Delete / copy / paste while it has focus.
		if (Focus && TimelineView && Focus == TimelineView && (K == EKeys::Delete || (E.IsControlDown() && (K == EKeys::C || K == EKeys::V)))) return false;
		if (K == EKeys::Escape)
		{
			if (IsFloatOpen(TEXT("dialog"))) CloseFloat(TEXT("dialog"));
			else if (Page != EPage::None) SetPage(EPage::None);
			else if (RootMenu >= 0) { RootMenu = -1; DeferSubMenu(); }
			else if (!OpenOrder.IsEmpty()) CloseFloat(OpenOrder.Last());
			else return false;
			return true;
		}
		const FString Id = FOKStudioSettings::Get().CommandFor(E);
		if (Id.IsEmpty() || !Commands.Contains(Id)) return false;
		// Held keys repeat only for stepping through frames.
		if (E.IsRepeat() && Id != TEXT("timeline.prev") && Id != TEXT("timeline.next")) return true;
		RunCommand(Id);
		return true;
	}
	bool HandleKeyUp(const FKeyEvent& E)
	{
		return Game.IsValid() && Game->FlyKeys.Remove(E.GetKey()) > 0;
	}

	/** Button tooltip: the label and its shortcut. */
	FString Tip(const FString& Label, const FString& Command) const
	{
		const FString Keys = FOKStudioSettings::Get().BindingOf(Command);
		return Keys.IsEmpty() ? Label : FString::Printf(TEXT("%s（%s）"), *Label, *Keys);
	}

	/** Moves the play head and samples the scene there. */
	void ScrubTo(float Time)
	{
		Game->bTimelinePlaying = false;
		const float T = FMath::Clamp(FMath::RoundToFloat(Time * 30.f) / 30.f, 0.f, Game->Document->Duration);
		if (TimelineView) TimelineView->Scrub(T);
		else Game->SetTimelineTime(T);
	}

	/** UI state for OKStudio.UiState (JSON). */
	FString StateJson() const
	{
		static const TCHAR* PageNames[] = { TEXT("none"), TEXT("object"), TEXT("state"), TEXT("bones"), TEXT("anim"), TEXT("scene"), TEXT("files"), TEXT("charloader"), TEXT("items"), TEXT("lights"), TEXT("camera") };
		TSharedRef<FJsonObject> J = MakeShared<FJsonObject>();
		J->SetStringField(TEXT("page"), PageNames[uint8(Page)]);
		J->SetNumberField(TEXT("bone_page"), BonePage);
		J->SetNumberField(TEXT("root_menu"), RootMenu);
		J->SetNumberField(TEXT("selected"), Game.IsValid() ? Game->SelectedId : -1);
		J->SetBoolField(TEXT("timeline"), IsFloatOpen(TEXT("timeline")));
		J->SetBoolField(TEXT("mmd"), IsFloatOpen(TEXT("mmd")));
		J->SetNumberField(TEXT("active_camera"), Game.IsValid() && Game->Document ? Game->Document->ActiveCamera : -1);
		J->SetBoolField(TEXT("lock_camera"), Game.IsValid() && Game->bLockActiveCamera);
		J->SetNumberField(TEXT("timeline_time"), Game.IsValid() ? Game->TimelineTime : 0.f);
		J->SetBoolField(TEXT("timeline_playing"), Game.IsValid() && Game->bTimelinePlaying);
		J->SetBoolField(TEXT("workspace"), bShowWorkspace);
		J->SetBoolField(TEXT("groups"), bShowGroups);
		J->SetBoolField(TEXT("transform"), bShowTransform);
		J->SetBoolField(TEXT("ui_hidden"), bHideUi);
		J->SetNumberField(TEXT("rows"), Rows.Num());
		TArray<TSharedPtr<FJsonValue>> Sel, Open;
		if (Game.IsValid()) for (int32 Id : Game->Selection) Sel.Add(MakeShared<FJsonValueNumber>(Id));
		J->SetArrayField(TEXT("selection"), Sel);
		for (const FString& Id : OpenOrder) Open.Add(MakeShared<FJsonValueString>(Id));
		J->SetArrayField(TEXT("windows"), Open);
		J->SetBoolField(TEXT("dirty"), Game.IsValid() && Game->bDirty);
		J->SetStringField(TEXT("busy"), Game.IsValid() ? Game->BusyReason() : FString());
		J->SetStringField(TEXT("dialog"), IsFloatOpen(TEXT("dialog")) ? DialogText : FString());
		J->SetNumberField(TEXT("kind_filter"), KindFilter);
		J->SetBoolField(TEXT("local_axes"), Game.IsValid() && Game->bLocalAxes);
		J->SetBoolField(TEXT("snap"), Game.IsValid() && Game->bSnap);
		TArray<TSharedPtr<FJsonValue>> Ids;
		for (const FString& Id : CommandOrder) Ids.Add(MakeShared<FJsonValueString>(Id));
		J->SetArrayField(TEXT("commands"), Ids);
		FString Out;
		FJsonSerializer::Serialize(J, TJsonWriterFactory<>::Create(&Out));
		return Out;
	}

	/** The shell's widget tree in canvas units (same fields as the HS2Studio6 dumps; Tools/studio_layout_spec.lua compares them). */
	FString LayoutJson() const
	{
		if (!Canvas) return FString();
		const FGeometry& Root = Canvas->GetPaintSpaceGeometry();
		const FVector2D Origin = Root.GetAbsolutePosition();
		const double K = Root.GetLocalSize().X > 0. ? Root.GetAbsoluteSize().X / Root.GetLocalSize().X : 1.;
		TArray<TSharedPtr<FJsonValue>> Nodes;
		TFunction<void(const TSharedRef<SWidget>&, const FString&, int32)> Walk = [&](const TSharedRef<SWidget>& W, const FString& Path, int32 Depth)
		{
			if (!W->GetVisibility().IsVisible()) return;
			const TSharedPtr<FTagMetaData> Tag = W->GetMetaData<FTagMetaData>();
			const FName Type = W->GetType();
			const bool bLeaf = Type == TEXT("SButton") || Type == TEXT("STextBlock") || Type == TEXT("SSpinBox<float>") || Type == TEXT("SEditableTextBox") || Type == TEXT("SCheckBox") || Type.ToString().StartsWith(TEXT("SListView"));
			FString Here = Path;
			if (Tag) Here = Path.IsEmpty() ? Tag->Tag.ToString() : Path + TEXT("/") + Tag->Tag.ToString();
			if (Tag || bLeaf)
			{
				const FGeometry& G = W->GetPaintSpaceGeometry();
				const FVector2D P = (G.GetAbsolutePosition() - Origin) / K, S = G.GetAbsoluteSize() / K;
				if (S.X >= 0.5 && S.Y >= 0.5)
				{
					TSharedRef<FJsonObject> N = MakeShared<FJsonObject>();
					N->SetStringField(TEXT("path"), Tag ? Here : Here + TEXT("/") + Type.ToString());
					N->SetNumberField(TEXT("d"), Depth);
					const FString Kind = Type == TEXT("SButton") ? TEXT("Button") : Type == TEXT("STextBlock") ? TEXT("Text") : Type == TEXT("SCheckBox") ? TEXT("Toggle")
						: Type == TEXT("SEditableTextBox") || Type == TEXT("SSpinBox<float>") ? TEXT("InputField") : Type.ToString().StartsWith(TEXT("SListView")) ? TEXT("ScrollRect") : TEXT("Rect");
					N->SetStringField(TEXT("kind"), Kind);
					TArray<TSharedPtr<FJsonValue>> R;
					for (double V : { P.X, P.Y, S.X, S.Y }) R.Add(MakeShared<FJsonValueNumber>(FMath::RoundToDouble(V * 10.) / 10.));
					N->SetArrayField(TEXT("rect"), R);
					if (Type == TEXT("STextBlock")) N->SetStringField(TEXT("text"), StaticCastSharedRef<STextBlock>(W)->GetText().ToString());
					Nodes.Add(MakeShared<FJsonValueObject>(N));
				}
			}
			FChildren* Children = W->GetChildren();
			for (int32 I = 0; Children && I < Children->Num(); ++I) Walk(Children->GetChildAt(I), Here, Depth + 1);
		};
		Walk(Canvas.ToSharedRef(), FString(), 0);
		TSharedRef<FJsonObject> C = MakeShared<FJsonObject>();
		C->SetStringField(TEXT("name"), TEXT("OKStudio"));
		C->SetNumberField(TEXT("order"), 0);
		C->SetNumberField(TEXT("scale"), K);
		C->SetArrayField(TEXT("reference"), { MakeShared<FJsonValueNumber>(1600), MakeShared<FJsonValueNumber>(900) });
		C->SetArrayField(TEXT("nodes"), Nodes);
		TSharedRef<FJsonObject> J = MakeShared<FJsonObject>();
		const FVector2D Screen = Root.GetAbsoluteSize();
		J->SetArrayField(TEXT("screen"), { MakeShared<FJsonValueNumber>(FMath::RoundToInt(Screen.X)), MakeShared<FJsonValueNumber>(FMath::RoundToInt(Screen.Y)) });
		J->SetArrayField(TEXT("canvases"), { MakeShared<FJsonValueObject>(C) });
		FString Out;
		FJsonSerializer::Serialize(J, TJsonWriterFactory<>::Create(&Out));
		return Out;
	}

	// --- Shell parts (HS2Studio6 layout spec, "shell"). ---

	TSharedRef<SWidget> MakeRootToolbar()
	{
		using namespace OKStudioStyle;
		static const TCHAR* Labels[] = { TEXT("加入"), TEXT("角色"), TEXT("音效"), TEXT("系統") };
		static const TCHAR* Ids[] = { TEXT("menu.add"), TEXT("menu.character"), TEXT("menu.sound"), TEXT("menu.system") };
		static const TCHAR* Icons[] = { TEXT("add"), TEXT("chara"), TEXT("sound"), TEXT("system") };
		TSharedRef<SVerticalBox> Box = SNew(SVerticalBox).Visibility_Lambda([this] { return bShowRoot ? EVisibility::Visible : EVisibility::Collapsed; });
		for (int32 M = 0; M < 4; ++M)
			Box->AddSlot().AutoHeight().Padding(0.f, M ? 4.f : 0.f, 0.f, 0.f)
				[Tagged(*FString::Printf(TEXT("Root_%s"), Ids[M] + 5), IconButton(Icons[M], Labels[M], Cmd(Ids[M]), 35.f, 41.f, TAttribute<bool>::CreateLambda([this, M] { return RootMenu == M; }), RootIcon()))];
		return Box;
	}

	TArray<FMenuEntry> SubMenuEntries() const
	{
		const FOKSceneObject* O = Game.IsValid() ? Game->Document->Find(Game->SelectedId) : nullptr;
		const bool bChar = O && O->Kind == EOKSceneObjectKind::Character;
		auto On = [this](EPage P) { return [this, P] { return Page == P; }; };
		auto Win = [this](const TCHAR* Id) { const FString W = Id; return [this, W] { return IsFloatOpen(W); }; };
		switch (RootMenu)
		{
		case 0: return { { TEXT("角色"), TEXT("page.charloader"), Win(TEXT("cards")) }, { TEXT("道具"), TEXT("page.items"), On(EPage::ItemAdd) }, { TEXT("燈光"), TEXT("page.lights"), On(EPage::LightAdd) },
			{ TEXT("相機"), TEXT("add.camera"), nullptr }, { TEXT("特效"), TEXT("add.effect"), Win(TEXT("effects")) }, { TEXT("資料夾"), TEXT("add.folder"), nullptr }, { TEXT("地圖"), TEXT("add.map"), Win(TEXT("maps")) },
			{ TEXT("快搜"), TEXT("app.quicksearch"), Win(TEXT("quick")) } };
		case 1: return { { TEXT("狀態"), TEXT("page.state"), On(EPage::State), bChar }, { TEXT("骨骼"), TEXT("page.bones"), On(EPage::Bones), bChar }, { TEXT("動畫"), TEXT("page.anim"), On(EPage::Anim), bChar },
			{ TEXT("屬性"), TEXT("page.object"), On(EPage::Object), O != nullptr }, { TEXT("姿勢庫"), TEXT("app.poselibrary"), Win(TEXT("poselib")), bChar }, { TEXT("MMD 導演"), TEXT("toggle.mmd"), Win(TEXT("mmd")) } };
		case 2: return { { TEXT("背景音樂（未支援）"), FString(), nullptr, false } };
		case 3: return { { TEXT("新場景"), TEXT("scene.new"), nullptr }, { TEXT("開啟／儲存"), TEXT("scene.browser"), Win(TEXT("scenes")) }, { TEXT("另存新檔"), TEXT("scene.saveas"), nullptr },
			{ TEXT("畫面設定"), TEXT("page.scene"), On(EPage::Scene) }, { TEXT("時間軸"), TEXT("toggle.timeline"), Win(TEXT("timeline")) }, { TEXT("MMD 導演"), TEXT("toggle.mmd"), Win(TEXT("mmd")) },
			{ TEXT("截圖"), TEXT("app.screenshot"), Win(TEXT("shot")) }, { TEXT("備份恢復"), TEXT("scene.backups"), Win(TEXT("backups")) }, { TEXT("資源檢查"), TEXT("scene.resources"), Win(TEXT("resources")) },
			{ TEXT("匯入 KStudio"), TEXT("scene.import"), nullptr }, { TEXT("播放故事"), TEXT("app.story"), Win(TEXT("stories")) }, { TEXT("設定"), TEXT("app.settings"), Win(TEXT("settings")) }, { TEXT("離開"), TEXT("app.quit"), nullptr } };
		default: return {};
		}
	}
	/** HS2Studio6 sub menus are fixed: 187 high (add, character, sound), the system menu 140. */
	float SubMenuHeight() const { return RootMenu == 3 ? 140.f : 187.f; }

	void DeferSubMenu() { RegisterActiveTimer(0.f, FWidgetActiveTimerDelegate::CreateLambda([this](double, float) { RebuildSubMenu(); return EActiveTimerReturnType::Stop; })); }
	void RebuildSubMenu()
	{
		using namespace OKStudioStyle;
		if (!SubMenuBody) return;
		TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);
		for (const FMenuEntry& E : SubMenuEntries())
		{
			TFunction<bool()> On = E.On;
			const FString Id = E.Command;
			const bool bEnabled = E.bEnabled;
			Box->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 1.f)
				[SNew(SBox).ToolTipText(FText::FromString(Tip(E.Label, Id)))[Button(FText::FromString(E.Label), [this, Id] { if (!Id.IsEmpty()) RunCommand(Id); DeferSubMenu(); }, 70.f, 17.f, TAttribute<bool>::CreateLambda([On] { return On && On(); }), 11.f, ButtonFill(),
					TAttribute<bool>::CreateLambda([this, bEnabled, Id] { return bEnabled && !Id.IsEmpty(); }))]];
		}
		// The menus keep HS2Studio6's fixed heights; a longer one scrolls.
		SubMenuBody->SetContent(SNew(SBorder).BorderImage(Brush(Panel())).Padding(FMargin(5.f, 5.f, 5.f, 4.f))[SNew(SScrollBox).ScrollBarThickness(FVector2D(3.f, 3.f)) + SScrollBox::Slot()[Box]]);
	}

	TSharedRef<SWidget> MakeTopTools()
	{
		using namespace OKStudioStyle;
		struct FTool { const TCHAR* Label; const TCHAR* Command; TFunction<bool()> On; const TCHAR* Icon; };
		const FTool Tools[] = {
			{ TEXT("角色分組"), TEXT("toggle.groups"), [this] { return bShowGroups; }, TEXT("groups") },
			{ TEXT("MMD 導演／設定"), TEXT("toggle.mmd"), [this] { return IsFloatOpen(TEXT("mmd")); }, TEXT("play") },
			{ TEXT("主選單"), TEXT("toggle.root"), [this] { return bShowRoot; }, TEXT("menu") },
			{ TEXT("工作區"), TEXT("toggle.workspace"), [this] { return bShowWorkspace; }, TEXT("tree") },
			{ TEXT("變換列"), TEXT("toggle.transform"), [this] { return bShowTransform; }, TEXT("move") },
			{ TEXT("畫面設定"), TEXT("page.scene"), [this] { return Page == EPage::Scene; }, TEXT("color") },
			{ TEXT("屬性"), TEXT("page.object"), [this] { return Page == EPage::Object; }, TEXT("options") },
			{ TEXT("相機"), TEXT("page.camera"), [this] { return Page == EPage::Camera; }, TEXT("camera") },
		};
		TSharedRef<SHorizontalBox> Box = SNew(SHorizontalBox);
		for (int32 I = 0; I < UE_ARRAY_COUNT(Tools); ++I)
		{
			const FTool& T = Tools[I];
			TFunction<bool()> On = T.On;
			if (!T.Command) { Box->AddSlot().AutoWidth().Padding(0.f, 0.f, 7.f, 0.f)[SNew(SBox).WidthOverride(33.f)]; continue; }
			Box->AddSlot().AutoWidth().Padding(0.f, 0.f, 7.f, 0.f)
				[Tagged(*FString::Printf(TEXT("TopTool_%d"), I), IconButton(T.Icon, Tip(T.Label, T.Command), Cmd(T.Command), 33.f, 33.f, TAttribute<bool>::CreateLambda([On] { return On(); }), ButtonFill(), ToolOn()))];
		}
		return Box;
	}

	/** Workspace: title, expand / collapse all, the object tree, timeline button, kind filter, locate and search, tool buttons. */
	TSharedRef<SWidget> MakeTreePanel()
	{
		using namespace OKStudioStyle;
		struct FTool { const TCHAR* Icon; const TCHAR* Label; const TCHAR* Command; };
		const FTool Row1[] = { { TEXT("root"), TEXT("移到根層"), TEXT("edit.root") }, { TEXT("copy"), TEXT("複製物件"), TEXT("edit.duplicate") }, { TEXT("delete"), TEXT("刪除"), TEXT("edit.delete") },
			{ TEXT("folder"), TEXT("加資料夾"), TEXT("add.folder") }, { TEXT("camera"), TEXT("從視角建相機"), TEXT("camera.fromview") }, { TEXT("frame"), TEXT("用此相機"), TEXT("camera.use") }, { TEXT("options"), TEXT("屬性"), TEXT("page.object") } };
		TSharedRef<SHorizontalBox> Tools = SNew(SHorizontalBox);
		for (const FTool& T : Row1) Tools->AddSlot().AutoWidth().Padding(0.f, 0.f, 4.f, 0.f)[IconButton(T.Icon, Tip(T.Label, T.Command), Cmd(T.Command), 28.f, 28.f)];
		static const TCHAR* FilterIcons[] = { TEXT("tree"), TEXT("folder"), TEXT("chara"), TEXT("frame"), TEXT("light"), TEXT("camera"), TEXT("effect") };
		return SNew(SBorder).BorderImage(Brush(Panel())).Padding(0.f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()[SNew(SBox).HeightOverride(23.f).Padding(FMargin(7.f, 2.f, 0.f, 0.f))[OKStudioStyle::Label(TAttribute<FText>::CreateLambda([this]
				{
					// The scene's name, starred while it has unsaved changes.
					const FString Name = Game.IsValid() && !Game->File.IsEmpty() ? FPaths::GetBaseFilename(Game->File) : FString(TEXT("新場景"));
					return FText::FromString(FString::Printf(TEXT("工作區　%s%s"), *Name, Game.IsValid() && Game->bDirty ? TEXT(" *") : TEXT("")));
				}), 12.f)]]
			+ SVerticalBox::Slot().AutoHeight().Padding(3.f, 0.f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth()[Button(FText::FromString(TEXT("展開全部")), Cmd(TEXT("tree.expand")), 117.f, 18.f, false, 10.f)]
				+ SHorizontalBox::Slot().AutoWidth()[Button(FText::FromString(TEXT("收起全部")), Cmd(TEXT("tree.collapse")), 117.f, 18.f, false, 10.f)]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(3.f, 3.f, 3.f, 0.f)
			[
				SNew(SBox).HeightOverride(285.f)
				[
					SNew(SBorder).BorderImage(Brush(ListArea())).Padding(0.f)
					[
						SAssignNew(List, SListView<TSharedPtr<FRow>>)
						.ListItemsSource(&Rows)
						.SelectionMode(ESelectionMode::Multi)
						.OnGenerateRow(this, &SOKStudioPanel::MakeRow)
						.OnSelectionChanged_Lambda([this](TSharedPtr<FRow> Row, ESelectInfo::Type Info)
						{
							if (Info == ESelectInfo::Direct || !Game.IsValid() || bSyncingList) return;
							TArray<int32> Ids;
							for (const TSharedPtr<FRow>& R : List->GetSelectedItems()) Ids.Add(R->Id);
							Game->SelectMany(Ids, Row ? Row->Id : -1);
						})
						.OnMouseButtonDoubleClick_Lambda([this](TSharedPtr<FRow> Row) { if (Row && Game.IsValid()) { Game->Select(Row->Id); BeginRename(); } })
						.OnContextMenuOpening_Lambda([this] { return TreeContextMenu(); })
					]
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(2.f, 8.f, 3.f, 0.f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth()[IconButton(TEXT("timeline"), Tip(TEXT("時間軸"), TEXT("toggle.timeline")), Cmd(TEXT("toggle.timeline")), 64.f, 22.f, TAttribute<bool>::CreateLambda([this] { return IsFloatOpen(TEXT("timeline")); }))]
				+ SHorizontalBox::Slot().AutoWidth().Padding(2.f, 0.f, 0.f, 0.f)[Button(FText::FromString(TEXT("MMD")), Cmd(TEXT("toggle.mmd")), 38.f, 22.f, TAttribute<bool>::CreateLambda([this] { return IsFloatOpen(TEXT("mmd")); }), 10.f)]
				+ SHorizontalBox::Slot().AutoWidth().Padding(3.f, 0.f, 0.f, 0.f)
				[
					SNew(SBox).WidthOverride(22.f).HeightOverride(22.f).ToolTipText_Lambda([this]
					{
						static const TCHAR* Names[] = { TEXT("全部"), TEXT("資料夾"), TEXT("角色"), TEXT("道具"), TEXT("燈光"), TEXT("相機"), TEXT("特效") };
						return FText::FromString(FString(TEXT("依種類篩選：")) + Names[FMath::Clamp(KindFilter + 1, 0, 6)]);
					})
					[
						Fill(TAttribute<bool>::CreateLambda([this] { return KindFilter >= 0; }), ButtonFill(),
							SNew(SButton).ButtonStyle(&FlatButton()).ContentPadding(0.f).HAlign(HAlign_Center).VAlign(VAlign_Center)
							.OnClicked_Lambda([this] { RunCommand(TEXT("tree.filter")); DeferTreeTools(); return FReply::Handled(); })
							[SAssignNew(FilterIcon, SBox)[SNew(SIcon).Icon(FilterIcons[0]).Size(14.f)]])
					]
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(2.f, 0.f, 0.f, 0.f)[IconButton(TEXT("eye"), TEXT("定位當前"), Cmd(TEXT("tree.locate")), 22.f, 22.f)]
				+ SHorizontalBox::Slot().AutoWidth().Padding(2.f, 0.f, 0.f, 0.f)
				[
					SNew(SBox).WidthOverride(72.f).HeightOverride(22.f)
					[
						SNew(SEditableTextBox).Style(&TextBoxStyle()).HintText(FText::FromString(TEXT("搜尋")))
						.OnTextChanged_Lambda([this](const FText& T) { Search = T.ToString(); RefreshRows(); })
					]
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(5.f, 6.f, 0.f, 0.f)[Tools]
		];
	}
	void DeferTreeTools()
	{
		static const TCHAR* FilterIcons[] = { TEXT("tree"), TEXT("folder"), TEXT("chara"), TEXT("frame"), TEXT("light"), TEXT("camera"), TEXT("effect") };
		if (FilterIcon) FilterIcon->SetContent(SNew(OKStudioStyle::SIcon).Icon(FilterIcons[FMath::Clamp(KindFilter + 1, 0, 6)]).Size(14.f));
	}
	TSharedPtr<SBox> FilterIcon;
	bool bSyncingList = false;

	/** Character groups (HS2Studio6 角色分組): the selected character's accessory points by group; attach the selected item. */
	TSharedRef<SWidget> MakeGroupPanel()
	{
		using namespace OKStudioStyle;
		return SNew(SBorder).BorderImage(Brush(Panel())).Padding(0.f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()[SNew(SBox).HeightOverride(23.f).Padding(FMargin(7.f, 2.f, 0.f, 0.f))[OKStudioStyle::Label(FText::FromString(TEXT("角色分組")), 12.f)]]
			+ SVerticalBox::Slot().AutoHeight().Padding(3.f, 0.f)
			[
				SNew(SBox).HeightOverride(20.f).Padding(FMargin(4.f, 2.f, 0.f, 0.f))
				[OKStudioStyle::Label(TAttribute<FText>::CreateLambda([this] { const FOKSceneObject* C = Game.IsValid() ? Game->Document->Find(GroupCharacter()) : nullptr; return FText::FromString(C ? C->Name : FString(TEXT("先選擇一個角色"))); }), 11.f, TextSoft())]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(3.f, 2.f, 3.f, 0.f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth()[Button(FText::FromString(TEXT("展開全部")), [this] { OpenGroups.Reset(); for (int32 G = 0; G < 10; ++G) OpenGroups.Add(G); DeferGroups(); }, 117.f, 18.f, false, 10.f)]
				+ SHorizontalBox::Slot().AutoWidth()[Button(FText::FromString(TEXT("收起全部")), [this] { OpenGroups.Reset(); DeferGroups(); }, 117.f, 18.f, false, 10.f)]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(3.f, 3.f, 3.f, 0.f)
			[
				SNew(SBox).HeightOverride(286.f)[SNew(SBorder).BorderImage(Brush(ListArea())).Padding(2.f)[SNew(SScrollBox) + SScrollBox::Slot()[SAssignNew(GroupList, SVerticalBox)]]]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(3.f, 4.f, 3.f, 0.f)
			[Button(FText::FromString(TEXT("掛到所選部位")), Cmd(TEXT("group.attach")), 234.f, 22.f, false, 11.f, ButtonFill(),
				TAttribute<bool>::CreateLambda([this] { const FOKSceneObject* O = Obj(); return !GroupPoint.IsEmpty() && GroupCharacter() >= 0 && O && O->Kind != EOKSceneObjectKind::Character; }))]
			+ SVerticalBox::Slot().AutoHeight().Padding(3.f, 3.f, 3.f, 0.f)
			[
				SNew(SBox).WidthOverride(234.f).HeightOverride(22.f)
				[SNew(SEditableTextBox).Font(Font(10.f)).HintText(FText::FromString(TEXT("搜尋分組／部位"))).OnTextChanged_Lambda([this](const FText& T) { GroupSearch = T.ToString(); DeferGroups(); })]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(7.f, 3.f, 0.f, 0.f)
			[OKStudioStyle::Label(FText::FromString(FString::Printf(TEXT("10 個分組 · %d 個部位；先選道具再選部位"), FOKStudioCatalog::Get().AccessoryPoints.Num())), 10.f, TextHint())]
		];
	}

	static const TCHAR* GroupName(int32 G)
	{
		static const TCHAR* Names[] = { TEXT("頭髮"), TEXT("頭"), TEXT("臉"), TEXT("頸"), TEXT("胸背"), TEXT("腰"), TEXT("腿"), TEXT("臂"), TEXT("手"), TEXT("下腹") };
		return G >= 0 && G < 10 ? Names[G] : TEXT("其他");
	}

	/** The character the group panel shows: the selected character, or the character the selection hangs under. */
	int32 GroupCharacter() const
	{
		if (!Game.IsValid()) return -1;
		for (const FOKSceneObject* O = Game->Document->Find(Game->SelectedId); O; O = Game->Document->Find(O->Parent))
			if (O->Kind == EOKSceneObjectKind::Character) return O->Id;
		return LastCharacter >= 0 && Game->Document->Find(LastCharacter) ? LastCharacter : -1;
	}

	void DeferGroups() { RegisterActiveTimer(0.f, FWidgetActiveTimerDelegate::CreateLambda([this](double, float) { RebuildGroups(); return EActiveTimerReturnType::Stop; })); }
	void RebuildGroups()
	{
		using namespace OKStudioStyle;
		if (!GroupList) return;
		GroupList->ClearChildren();
		if (GroupCharacter() < 0) return;
		const TArray<FOKStudioAccessoryPoint>& Points = FOKStudioCatalog::Get().AccessoryPoints;
		for (int32 G = 0; G < 10; ++G)
		{
			TArray<const FOKStudioAccessoryPoint*> In;
			for (const FOKStudioAccessoryPoint& P : Points)
				if (P.Group == G && (GroupSearch.IsEmpty() || P.Name.Contains(GroupSearch) || P.Key.Contains(GroupSearch) || FString(GroupName(G)).Contains(GroupSearch))) In.Add(&P);
			if (In.IsEmpty()) continue;
			const bool bOpen = OpenGroups.Contains(G) || !GroupSearch.IsEmpty();
			GroupList->AddSlot().AutoHeight()
			[
				SNew(SButton).ButtonStyle(&OKStudioStyle::FlatButton()).ContentPadding(FMargin(4.f, 1.f))
				.OnClicked_Lambda([this, G] { if (OpenGroups.Contains(G)) OpenGroups.Remove(G); else OpenGroups.Add(G); DeferGroups(); return FReply::Handled(); })
				[OKStudioStyle::Label(FText::FromString(FString::Printf(TEXT("%s 分組：%s"), bOpen ? TEXT("▼") : TEXT("▶"), GroupName(G))), 11.f)]
			];
			if (!bOpen) continue;
			for (const FOKStudioAccessoryPoint* P : In)
			{
				const FString Key = P->Key;
				// A part takes objects dropped from the tree (they hang on it).
				GroupList->AddSlot().AutoHeight().Padding(16.f, 0.f, 0.f, 0.f)
				[
					SNew(SOKStudioDropArea)
					.CanDrop([this](const TArray<int32>& Ids) { const int32 C = GroupCharacter(); return C >= 0 && !Ids.Contains(C) && Ids.Num() > 0; })
					.OnDrop([this, Key](const TArray<int32>& Ids) { DropOnPoint(Ids, Key); })
					[
						OKStudioStyle::Fill(TAttribute<bool>::CreateLambda([this, Key] { return GroupPoint == Key; }), FLinearColor::Transparent, SNew(SButton).ButtonStyle(&OKStudioStyle::FlatButton()).ContentPadding(FMargin(4.f, 0.f))
						.OnClicked_Lambda([this, Key] { GroupPoint = GroupPoint == Key ? FString() : Key; return FReply::Handled(); })
						[OKStudioStyle::Label(FText::FromString(FString::Printf(TEXT("%s（%s）"), *P->Name, *P->Key)), 10.f, TextSoft())])
					]
				];
			}
		}
	}

	/** Hangs the selected object on an accessory point of the group panel's character (it sits at the point). */
	bool AttachToPoint(const FString& Key)
	{
		FOKSceneObject* O = Obj();
		const int32 Character = GroupCharacter();
		if (!O || Key.IsEmpty() || Character < 0 || O->Kind == EOKSceneObjectKind::Character || O->Id == Character) return false;
		Game->Document->Reparent(O->Id, Character);
		if (FOKSceneObject* X = Obj()) { X->AttachBone = Key; X->Transform = FTransform::Identity; Changed(); }
		SetStatus(FString::Printf(TEXT("已掛到 %s"), *Key));
		return true;
	}

	/** Transform bar (bottom left): position, rotation, scale of the selection, copy / paste / reset, gizmo modes. */
	TSharedRef<SWidget> MakeTransformBar()
	{
		using namespace OKStudioStyle;
		TSharedRef<SVerticalBox> Rows3 = SNew(SVerticalBox);
		static const TCHAR* TransformRows[] = { TEXT("位置"), TEXT("旋轉"), TEXT("縮放") };
		for (int32 Row = 0; Row < 3; ++Row)
		{
			TSharedRef<SHorizontalBox> Line = SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(SBox).WidthOverride(29.f)[OKStudioStyle::Label(FText::FromString(TransformRows[Row]), 12.f)]];
			static const TCHAR* Axes[] = { TEXT("X"), TEXT("Y"), TEXT("Z") };
			static const FLinearColor AxisColors[] = { FLinearColor(1.f, .25f, .25f), FLinearColor(.35f, 1.f, .35f), FLinearColor(.4f, .6f, 1.f) };
			for (int32 Axis = 0; Axis < 3; ++Axis)
			{
				Line->AddSlot().AutoWidth().VAlign(VAlign_Center)[SNew(SBox).WidthOverride(16.f).HAlign(HAlign_Center)[OKStudioStyle::Label(FText::FromString(Axes[Axis]), 12.f, AxisColors[Axis])]];
				Line->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 6.f, 0.f)
				[
					SNew(SBox).WidthOverride(51.f).HeightOverride(15.f)
					[
						SNew(SSpinBox<float>).Style(&SpinStyle()).Font(Font(11.f)).MinValue(Row == 2 ? 0.01f : -100000.f).MaxValue(100000.f).Delta(Row == 1 ? 1.f : (Row == 2 ? 0.01f : 1.f))
						.Value_Lambda([this, Row, Axis] { const FOKSceneObject* O = Obj(); return O ? TransformValue(O->Transform, Row, Axis) : 0.f; })
						.OnValueChanged_Lambda([this, Row, Axis](float V) { if (FOKSceneObject* O = Obj()) { SetTransformValue(O->Transform, Row, Axis, V); Changed(); } })
						.OnBeginSliderMovement_Lambda([this] { if (Game.IsValid()) Game->bUserDragging = true; })
						.OnEndSliderMovement_Lambda([this](float) { if (Game.IsValid()) Game->bUserDragging = false; })
					]
				];
			}
			Line->AddSlot().AutoWidth().VAlign(VAlign_Center)[Button(FText::FromString(TEXT("重設")), [this, Row] { if (FOKSceneObject* O = Obj()) { ResetTransformRow(O->Transform, Row); Changed(); } }, 30.f, 15.f, false, 10.f)];
			Rows3->AddSlot().AutoHeight().Padding(0.f, Row ? 3.f : 0.f, 0.f, 0.f)[Line];
		}
		return SNew(SBorder).BorderImage(Brush(Panel())).Padding(FMargin(5.f, 4.f, 4.f, 4.f))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()[Rows3]
			+ SHorizontalBox::Slot().AutoWidth().Padding(6.f, 0.f, 0.f, 0.f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()[Button(FText::FromString(TEXT("複製變換")), Cmd(TEXT("transform.copy")), 70.f, 16.f, false, 10.f)]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f)[Button(FText::FromString(TEXT("貼上變換")), Cmd(TEXT("transform.paste")), 70.f, 16.f, false, 10.f)]
				+ SVerticalBox::Slot().AutoHeight()[Button(FText::FromString(TEXT("重設變換")), Cmd(TEXT("transform.reset")), 70.f, 16.f, false, 10.f)]
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(4.f, 0.f, 0.f, 0.f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth()[SNew(SBox).ToolTipText(FText::FromString(Tip(TEXT("主軸"), TEXT("gizmo.axis"))))[Button(FText::FromString(TEXT("主軸")), Cmd(TEXT("gizmo.axis")), 50.f, 17.f, TAttribute<bool>::CreateLambda([this] { return Game.IsValid() && Game->bShowGuide; }), 10.f)]]
					+ SHorizontalBox::Slot().AutoWidth().Padding(2.f, 0.f)[SNew(SBox).ToolTipText(FText::FromString(Tip(TEXT("局部座標"), TEXT("gizmo.local"))))[Button(FText::FromString(TEXT("局部")), Cmd(TEXT("gizmo.local")), 25.f, 17.f, TAttribute<bool>::CreateLambda([this] { return Game.IsValid() && Game->bLocalAxes; }), 9.f)]]
					+ SHorizontalBox::Slot().AutoWidth()[SNew(SBox).ToolTipText(FText::FromString(Tip(TEXT("吸附（按住 Ctrl 也會）"), TEXT("gizmo.snap"))))[Button(FText::FromString(TEXT("吸附")), Cmd(TEXT("gizmo.snap")), 25.f, 17.f, TAttribute<bool>::CreateLambda([this] { return Game.IsValid() && Game->bSnap; }), 9.f)]]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 3.f, 0.f, 0.f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth()[IconButton(TEXT("move"), Tip(TEXT("移動"), TEXT("gizmo.move")), Cmd(TEXT("gizmo.move")), 32.f, 30.f, TAttribute<bool>::CreateLambda([this] { return Game.IsValid() && Game->GuideMode == 0; }))]
					+ SHorizontalBox::Slot().AutoWidth().Padding(4.f, 0.f)[IconButton(TEXT("rotate"), Tip(TEXT("旋轉"), TEXT("gizmo.rotate")), Cmd(TEXT("gizmo.rotate")), 32.f, 30.f, TAttribute<bool>::CreateLambda([this] { return Game.IsValid() && Game->GuideMode == 1; }))]
					+ SHorizontalBox::Slot().AutoWidth()[IconButton(TEXT("scale"), Tip(TEXT("縮放"), TEXT("gizmo.scale")), Cmd(TEXT("gizmo.scale")), 32.f, 30.f, TAttribute<bool>::CreateLambda([this] { return Game.IsValid() && Game->GuideMode == 2; }))]
				]
			]
		];
	}

	static float TransformValue(const FTransform& T, int32 Row, int32 Axis)
	{
		if (Row == 0) return float(T.GetLocation()[Axis]);
		if (Row == 2) return float(T.GetScale3D()[Axis]);
		const FRotator R = T.Rotator();
		return float(Axis == 0 ? R.Pitch : (Axis == 1 ? R.Yaw : R.Roll));
	}
	static void SetTransformValue(FTransform& T, int32 Row, int32 Axis, float V)
	{
		if (Row == 0) { FVector L = T.GetLocation(); L[Axis] = V; T.SetLocation(L); return; }
		if (Row == 2) { FVector S = T.GetScale3D(); S[Axis] = FMath::Max(V, 0.01f); T.SetScale3D(S); return; }
		FRotator R = T.Rotator();
		(Axis == 0 ? R.Pitch : (Axis == 1 ? R.Yaw : R.Roll)) = V;
		T.SetRotation(R.Quaternion());
	}
	static void ResetTransformRow(FTransform& T, int32 Row)
	{
		if (Row == 0) T.SetLocation(FVector::ZeroVector);
		else if (Row == 1) T.SetRotation(FQuat::Identity);
		else T.SetScale3D(FVector::OneVector);
	}

	/** Detail window rect per page: the spec's place, moved where the user dragged it. */
	FMargin PageRect() const
	{
		FMargin R = BasePageRect();
		if (const FVector2D* D = DetailOffsets.Find(uint8(Page))) { R.Left += D->X; R.Top += D->Y; }
		return R;
	}
	/** Detail window rect per page (spec windows, canvas units). */
	FMargin BasePageRect() const
	{
		switch (Page)
		{
		case EPage::State: return FMargin(134.f, 7.f, 252.f, 326.f);
		case EPage::Bones: return FMargin(134.f, 7.f, 320.f, 390.f);
		case EPage::Anim: return FMargin(135.f, 10.f, 446.f, 370.f);
		case EPage::ItemAdd: return FMargin(134.f, 7.f, 274.f, 234.f);
		case EPage::LightAdd: return FMargin(142.f, 36.f, 210.f, 166.f);
		case EPage::CharLoader: case EPage::Files: return FMargin(142.f, 36.f, 620.f, 420.f);
		case EPage::Scene: return FMargin(142.f, 36.f, 480.f, 326.f);
		default: return FMargin(135.f, 47.f, 300.f, 429.f);
		}
	}
	FString PageTitle() const
	{
		const FOKSceneObject* O = Obj();
		const FString Who = O ? O->Name : FString();
		switch (Page)
		{
		case EPage::Object: return O ? FString::Printf(TEXT("%s #%d"), OKStudioPanelText::KindTag(O->Kind), O->Id) : TEXT("屬性");
		case EPage::State: return TEXT("狀態　") + Who;
		case EPage::Bones: return TEXT("骨骼　") + Who;
		case EPage::Anim: return TEXT("動畫　") + Who;
		case EPage::Scene: return TEXT("畫面設定");
		case EPage::Files: return TEXT("場景檔");
		case EPage::CharLoader: return TEXT("角色載入器");
		case EPage::ItemAdd: return TEXT("道具目錄");
		case EPage::LightAdd: return TEXT("添加光源");
		case EPage::Camera: return TEXT("相機");
		default: return FString();
		}
	}

	void SetPage(EPage P)
	{
		// The scene files and the character loader are their own windows now.
		if (P == EPage::Files) { OpenFloat(TEXT("scenes")); return; }
		if (P == EPage::CharLoader) { OpenFloat(TEXT("cards")); return; }
		Page = P;
		if (P != EPage::None && DetailSlot) BringToFront(DetailSlot);
		RegisterActiveTimer(0.f, FWidgetActiveTimerDelegate::CreateLambda([this](double, float) { RebuildDetailBody(); RefreshRows(); RebuildSubMenu(); return EActiveTimerReturnType::Stop; }));
	}

	/** The detail window's body for the page: the property rows (and the bones sub menu), or the scene file box. */
	void RebuildDetailBody()
	{
		using namespace OKStudioStyle;
		if (!DetailBody) return;
		TSharedRef<SWidget> Area = SNew(SScrollBox).ScrollBarThickness(FVector2D(4.f, 4.f)) + SScrollBox::Slot().Padding(3.f, 0.f)[SAssignNew(Props, SVerticalBox)];
		if (Page == EPage::Scene)
		{
			// 畫面設定: tabs down the left (camera, environment / sky, fog, bloom, depth of field, tone / LUT, vignette, background / frame).
			static const TCHAR* Tabs[] = { TEXT("相機"), TEXT("環境／天空"), TEXT("霧"), TEXT("泛光"), TEXT("景深"), TEXT("色調／LUT"), TEXT("暗角"), TEXT("背景／前景"), TEXT("預設") };
			TSharedRef<SVerticalBox> Menu = SNew(SVerticalBox);
			for (int32 I = 0; I < UE_ARRAY_COUNT(Tabs); ++I)
				Menu->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 3.f)[Button(FText::FromString(Tabs[I]), Cmd(TEXT("scene.tab"), FString::FromInt(I)), 86.f, 17.f, TAttribute<bool>::CreateLambda([this, I] { return SceneTab == I; }), 10.f)];
			DetailBody->SetContent(SNew(SHorizontalBox) + SHorizontalBox::Slot().AutoWidth().Padding(2.f, 2.f, 6.f, 0.f)[Menu] + SHorizontalBox::Slot().FillWidth(1.f)[Area]);
			return;
		}
		if (Page != EPage::Bones) { DetailBody->SetContent(Area); return; }
		static const TCHAR* Names[] = { TEXT("FK"), TEXT("IK"), TEXT("視線"), TEXT("頸部"), TEXT("表情"), TEXT("手勢"), TEXT("姿勢檔") };
		TSharedRef<SVerticalBox> Menu = SNew(SVerticalBox);
		for (int32 I = 0; I < 7; ++I)
			Menu->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 3.f)[Button(FText::FromString(Names[I]), Cmd(TEXT("bones.page"), FString::FromInt(I)), 110.f, 17.f, TAttribute<bool>::CreateLambda([this, I] { return BonePage == I; }))];
		DetailBody->SetContent(SNew(SHorizontalBox) + SHorizontalBox::Slot().AutoWidth().Padding(2.f, 2.f, 6.f, 0.f)[Menu] + SHorizontalBox::Slot().FillWidth(1.f)[Area]);
	}

public:
	// --- Floating windows (U1-3): dragged by the title, brought to the front by a click; places kept in ui_layout.json. ---
	struct FFloat
	{
		FString Title;
		FVector2D Pos = FVector2D::ZeroVector, Size = FVector2D(300.f, 300.f), Default = FVector2D::ZeroVector;
		bool bOpen = false;
		TSharedPtr<SBox> Body;
		TFunction<TSharedRef<SWidget>()> Build;
		SConstraintCanvas::FSlot* Slot = nullptr;
	};
	TMap<FString, FFloat> Floats;
	/** Open windows, the frontmost last. */
	TArray<FString> OpenOrder;
	SConstraintCanvas::FSlot* DetailSlot = nullptr;
	float TopZ = 10.f;
	TMap<uint8, FVector2D> DetailOffsets;
	bool bLayoutDirty = false;
	double LayoutSavedAt = 0.;
	void AddFloat(const FString& Id, const FString& Title, const FMargin& Rect, TFunction<TSharedRef<SWidget>()> Build);
	void RegisterFloats();
	void OpenFloat(const FString& Id, bool bToggle = false);
	void CloseFloat(const FString& Id);
	bool IsFloatOpen(const FString& Id) const { const FFloat* F = Floats.Find(Id); return F && F->bOpen; }
	void RebuildFloat(const FString& Id);
	void DeferFloat(const FString& Id);
	void BringToFront(SConstraintCanvas::FSlot* Slot);
	void LoadLayout();
	void SaveLayout() const;
	/** Per frame: thumbnails, the item preview, the status line, the window title, saving the layout now and then. */
	void TickUi(float DeltaTime);

	// --- Dialog and the unsaved-changes guard (U7-4). ---
	FString DialogText;
	TArray<TPair<FString, TFunction<void()>>> DialogButtons;
	void Confirm(const FString& Text, const TArray<TPair<FString, TFunction<void()>>>& Buttons);
	void GuardUnsaved(TFunction<void()> Continue);
	TSharedRef<SWidget> BuildDialog();

	// --- Tree (U2). ---
	/** Kind filter of the tree (-1: all; else an EOKSceneObjectKind). */
	int32 KindFilter = -1;
	TMap<int32, TWeakPtr<SInlineEditableTextBlock>> RowNames;
	int32 DragHoverId = -1;
	double DragHoverStart = 0.;
	TSharedPtr<SWidget> TreeContextMenu();
	void BeginRename();
	bool CanDropOn(const TArray<int32>& Ids, int32 Target) const;
	void DropOn(const TArray<int32>& Ids, int32 Target);
	bool DropOnPoint(const TArray<int32>& Ids, const FString& Key);
	void CopySelection();
	bool PasteClipboard();
	void DuplicateSelection();
	void DeleteSelection();
	void HideSelection();
	void ParentSelectionToPrimary();
	FString SaveSelectionAsScene();
	void LocateSelection();
	FString Clipboard;

	// --- Card browser (U3-1). ---
	FString CardFolder, CardSearch, CardPreview;
	int32 CardPage = 0;
	TArray<FString> CardFiles;
	TArray<FString> CardRoots() const;
	void ScanCardFolder();
	TSharedRef<SWidget> BuildCardBrowser();
	bool AddOrReplaceCard(const FString& Card, bool bReplace);

	// --- Items: search, hover preview, middle click adds as a child (U3-2). ---
	FString ItemSearch;
	FIntVector HoverItem = FIntVector(-1);
	double HoverSince = 0.;
	TStrongObjectPtr<UOKSceneDocument> PreviewDoc;
	TWeakObjectPtr<AOKSceneActor> PreviewActor;
	FIntVector PreviewBuilding = FIntVector(-1);
	int32 PreviewWait = 0;
	TArray<FString> PreviewCache;
	void TickItemPreview();
	bool AddItemTo(int32 Group, int32 Category, int32 No, bool bAsChild);
	static FString ItemKey(int32 Group, int32 Category, int32 No) { return FString::Printf(TEXT("item:%d/%d/%d"), Group, Category, No); }

	// --- Quick search (U3-3), the effect list and the map browser (U3-4). ---
	struct FHit { FString Kind, Key, Name, Path; };
	TArray<FHit> SearchCatalog(const FString& Query, const TArray<FString>& Kinds, int32 Limit, bool bHidden = false) const;
	FString QuickQuery;
	int32 QuickMode = 0;
	TArray<TSharedPtr<FHit>> QuickHits;
	TSharedPtr<SListView<TSharedPtr<FHit>>> QuickList;
	void RunQuickSearch();
	bool UseHit(const FHit& Hit);
	TSharedRef<SWidget> BuildQuickSearch();
	TSharedRef<SWidget> BuildEffectList();
	TSharedRef<SWidget> BuildMapBrowser();
	int32 MapPick = -1;
	FString EffectSearch, MapSearch;

	// --- Pose (U5). ---
	/** FK parts shown and posed: body, chest, hair, neck, left hand, right hand, skirt (HS2Studio6's defaults: body). */
	TArray<bool> FkParts = { true, false, false, false, false, false, false };
	/** IK parts: body, left arm, right arm, left leg, right leg. */
	TArray<bool> IkParts = { true, true, true, true, true };
	bool bShowFkPoints = true, bShowIkPoints = true;
	static int32 FkPartOf(const FString& Bone);
	static int32 IkPartOf(int32 Effector);
	FString BoneSearch;
	TSet<FString> OpenBones;
	struct FPosePoint { FVector2D At = FVector2D::ZeroVector; int32 Ik = -1; FString Bone; bool bParent = false; FVector2D Parent = FVector2D::ZeroVector; bool bOn = false; };
	bool IsPoseMode() const;
	bool PosePoints(TArray<FPosePoint>& Out) const;
	bool PickPosePoint(const FVector2D& At);
	bool LookPointScreen(FVector2D& Out) const;
	void DragLookPoint(const FVector2D& Delta);
	bool MirrorPose(FOKSceneObject& X, int32 Mode);
	void IkFromFk(FOKSceneObject& X);
	void FkFromPose(FOKSceneObject& X);
	TArray<FOKSceneFkBone> PoseClipFk;
	TArray<FOKSceneIkTarget> PoseClipIk;
	bool bHasPoseClip = false;
	FString PoseSearch, PoseName, PosePreviewFile;
	TArray<FOKSceneFkBone> PoseBeforeFk;
	TArray<FOKSceneIkTarget> PoseBeforeIk;
	int32 PosePreviewId = -1;
	TSharedRef<SWidget> BuildPoseLibrary();
	void PreviewPose(const FString& File);
	void CommitPosePreview();
	void CancelPosePreview();
	bool SavePoseWithPicture(const FString& Name);
	/** A searchable bone list: each bone opens into its X Y Z. */
	void BoneTree(const TArray<FString>& Bones, TFunction<FString(const FString&)> Show, TFunction<FVector(FOKSceneObject&, const FString&)> Get,
		TFunction<void(FOKSceneObject&, const FString&, const FVector&)> Set, TFunction<void(const FString&)> OnPick, TFunction<bool(const FString&)> IsSet);
	void ExpressionPage(FOKSceneObject& O);
	void HandPage(FOKSceneObject& O);
	void LookPage(FOKSceneObject& O, bool bNeck);
	void FkPage(FOKSceneObject& O);
	void IkPage(FOKSceneObject& O);
	void PoseFilesPage();

	// --- Timeline and the export window (U6). ---
	TSharedPtr<SOKStudioTimeline> TimelineView;
	TSharedRef<SWidget> BuildTimeline();
	void ShowEaseMenu(const FVector2D& At);
	float ExportFromT = 0.f, ExportToT = -1.f, ExportFpsV = 30.f;
	int32 ExportW = 1920, ExportH = 1080, ExportKind = 0;
	FString ExportFolderText;
	TSharedRef<SWidget> BuildExport();
	TSharedRef<SWidget> BuildMmdDirector();

	// --- Scene files (U7). ---
	FString SceneFolder, SceneSearch, ScenePick, SceneNewName, SaveAsName;
	int32 SceneSort = 0;
	bool bSceneSaveMode = false;
	TArray<FString> SceneList;
	static FString KStudioFolder();
	void ScanScenes();
	TSharedRef<SWidget> BuildSceneBrowser();
	bool OpenScene(const FString& Path, bool bMerge);
	bool DeleteScene(const FString& Path);
	bool RenameScene(const FString& Path, const FString& NewName);
	TSharedRef<SWidget> BuildBackups();
	TSharedRef<SWidget> BuildReport();
	TSharedRef<SWidget> BuildResources();
	TSharedRef<SWidget> BuildStories();
	void ShowLoadReport();
	TArray<FString> ReportLines;

	// --- Look (U8). ---
	TFunction<void(const FLinearColor&)> ColorTarget;
	FLinearColor ColorValue = FLinearColor::White;
	FString ColorTitle, ColorKey;
	void PickColor(const FString& Key, const FString& Title, const FLinearColor& Start, TFunction<void(const FLinearColor&)> Set);
	TSharedRef<SWidget> BuildColorPicker();
	/** A colour field: a swatch that opens the colour window (again: closes it). */
	TSharedRef<SWidget> Swatch(const FString& Key, const FString& Title, TFunction<FLinearColor()> Get, TFunction<void(const FLinearColor&)> Set);
	void ColorRow(const FString& Name, TFunction<FLinearColor(FOKSceneObject&)> Get, TFunction<void(FOKSceneObject&, const FLinearColor&)> Set);
	int32 SceneTab = 0;
	void SceneSettingsPage();
	int32 ShotW = 1920, ShotH = 1080;
	bool bShotAlpha = false, bShotNoOverwrite = true;
	FString ShotFolder, ShotPattern = TEXT("okstudio_%Y%m%d_%H%M%S");
	FString TakeShot();
	TSharedRef<SWidget> BuildShot();
	/** Shader properties an item's materials have (for the material edit drop-down). */
	TArray<FString> MaterialProperties(int32 Id) const;
	TMap<int32, TSharedRef<TArray<TSharedPtr<FString>>>> MaterialPropertyOptions;

	// --- Settings (U9). ---
	int32 SettingsTab = 0;
	FString SettingsSearch, RebindingId;
	TSharedRef<SWidget> BuildSettings();

	TSharedPtr<SOKStudioView> View;
	TSharedPtr<FOKStudioKeyProcessor> KeyProcessor;
	TSharedPtr<SConstraintCanvas> Canvas;
	TSharedPtr<SBox> SubMenuBody, DetailBody;
	TSharedPtr<SVerticalBox> GroupList;
	EPage Page = EPage::None;
	int32 BonePage = 0;
	int32 RootMenu = -1;
	int32 LoaderCard = 0;
	int32 LastCharacter = -1;
	bool bShowRoot = true, bShowWorkspace = true, bShowGroups = true, bShowTransform = true, bHideUi = false;
	TSet<int32> Collapsed, OpenGroups;
	FString Search, GroupSearch, GroupPoint;
	FTransform CopiedTransform;
	bool bHasCopiedTransform = false;

public:

	void SetStatus(const FString& Text) { if (Status) Status->SetText(FText::FromString(Text)); }

	bool SaveNamed(const FString& Name)
	{
		if (!Game.IsValid()) return false;
		const FString Clean = FPaths::MakeValidFileName(Name.TrimStartAndEnd());
		if (Clean.IsEmpty()) { SetStatus(TEXT("請輸入場景名稱")); return false; }
		const FString Path = UOKStudioGame::SceneDirectory() / Clean + TEXT(".png");
		FString E;
		const bool bOk = Game->Save(Path, E);
		SetStatus(bOk ? TEXT("已存 ") + Clean : E);
		if (bOk) { FOKStudioThumbnails::Get().Forget(Path); ScanScenes(); DeferFloat(TEXT("scenes")); }
		return bOk;
	}
	bool LoadFile(const FString& Path)
	{
		if (!Game.IsValid()) return false;
		FString E;
		const bool bOk = Game->Load(Path, E);
		SetStatus(bOk ? TEXT("已讀取 ") + FPaths::GetBaseFilename(Path) : E);
		if (bOk) ReportPending = 90;
		return bOk;
	}
	/** Scene files the browser lists now (UserData/Studio/scene when it was never opened). */
	int32 ListedFiles() { if (SceneList.IsEmpty()) ScanScenes(); return SceneList.Num(); }
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
		ScanScenes();
		return bOk;
	}
	/** Frames left before the open report shows (objects build over a few frames after an open). */
	int32 ReportPending = 0;

	void RefreshRows()
	{
		Rows.Reset();
		if (!Game.IsValid()) return;
		if (const FOKSceneObject* S = Game->Document->Find(Game->SelectedId); S && S->Kind == EOKSceneObjectKind::Character) LastCharacter = S->Id;
		auto Passes = [this](const FOKSceneObject& O) { return KindFilter < 0 || int32(O.Kind) == KindFilter; };
		if (!Search.IsEmpty() || KindFilter >= 0)
		{
			// Searching or filtering lists every match flat.
			for (const FOKSceneObject& O : Game->Document->Objects)
				if ((Search.IsEmpty() || O.Name.Contains(Search)) && Passes(O)) { TSharedPtr<FRow> R = MakeShared<FRow>(); R->Id = O.Id; R->Depth = 0; Rows.Add(R); }
		}
		else
		{
			TFunction<void(int32, int32)> Walk = [&](int32 Parent, int32 Depth)
			{
				for (int32 Id : Game->Document->Children(Parent))
				{
					TSharedPtr<FRow> R = MakeShared<FRow>(); R->Id = Id; R->Depth = Depth; Rows.Add(R);
					if (!Collapsed.Contains(Id)) Walk(Id, Depth + 1);
				}
			};
			Walk(-1, 0);
		}
		if (List)
		{
			List->RequestListRefresh();
			TGuardValue<bool> Syncing(bSyncingList, true);
			List->ClearSelection();
			for (const TSharedPtr<FRow>& R : Rows) if (Game->IsSelected(R->Id)) List->SetItemSelection(R, true, ESelectInfo::Direct);
		}
		RebuildProperties();
		RebuildGroups();
	}

	/** Tree row (18 high, 12 per level): expander, visibility, kind, the name (double click or F2 renames); dragged onto
	 *  another object it moves under it. */
	TSharedRef<ITableRow> MakeRow(TSharedPtr<FRow> Row, const TSharedRef<STableViewBase>& Owner)
	{
		using namespace OKStudioStyle;
		const int32 Id = Row->Id;
		const bool bParent = Game.IsValid() && Game->Document->Children(Id).Num() > 0;
		const FOKSceneObject* Shown = Game.IsValid() ? Game->Document->Find(Id) : nullptr;
		static const TCHAR* KindIcons[] = { TEXT("folder"), TEXT("chara"), TEXT("frame"), TEXT("light"), TEXT("camera"), TEXT("effect") };
		const FName Icon = KindIcons[Shown ? FMath::Clamp(int32(Shown->Kind), 0, 5) : 0];
		static FInlineEditableTextBlockStyle NameStyle = []
		{
			FInlineEditableTextBlockStyle S = FCoreStyle::Get().GetWidgetStyle<FInlineEditableTextBlockStyle>("InlineEditableTextBlockStyle");
			S.SetEditableTextBoxStyle(TextBoxStyle());
			S.SetTextStyle(FTextBlockStyle(S.TextStyle).SetFont(Font(11.f)).SetColorAndOpacity(FSlateColor(OKStudioStyle::Text())));
			return S;
		}();
		TSharedPtr<SInlineEditableTextBlock> Name;
		TSharedRef<STableRow<TSharedPtr<FRow>>> Out = SNew(STableRow<TSharedPtr<FRow>>, Owner).Style(&RowStyle()).Padding(FMargin(2.f + 12.f * Row->Depth, 0.f, 2.f, 0.f))
			.OnDragDetected_Lambda([this, Id](const FGeometry&, const FPointerEvent&)
			{
				// Drag the selection (or just this row when it is not selected).
				TArray<int32> Ids = Game.IsValid() && Game->IsSelected(Id) ? Game->SelectionRoots() : TArray<int32>{ Id };
				const FOKSceneObject* O = Game.IsValid() ? Game->Document->Find(Id) : nullptr;
				return FReply::Handled().BeginDragDrop(FOKStudioRowDrag::New(Ids, Ids.Num() > 1 ? FString::Printf(TEXT("%d 個物件"), Ids.Num()) : (O ? O->Name : FString())));
			})
			.OnCanAcceptDrop_Lambda([this, Id](const FDragDropEvent& E, EItemDropZone Zone, TSharedPtr<FRow>) -> TOptional<EItemDropZone>
			{
				TSharedPtr<FOKStudioRowDrag> Op = E.GetOperationAs<FOKStudioRowDrag>();
				if (!Op) return TOptional<EItemDropZone>();
				Op->bOverTarget = true;
				Op->bCanDrop = CanDropOn(Op->Ids, Id);
				// Resting on a closed object for a moment opens it.
				const double Now = FPlatformTime::Seconds();
				if (DragHoverId != Id) { DragHoverId = Id; DragHoverStart = Now; }
				else if (Now - DragHoverStart > 0.45 && Collapsed.Contains(Id)) { Collapsed.Remove(Id); DeferRebuild(); }
				return Op->bCanDrop ? TOptional<EItemDropZone>(EItemDropZone::OntoItem) : TOptional<EItemDropZone>();
			})
			.OnAcceptDrop_Lambda([this, Id](const FDragDropEvent& E, EItemDropZone, TSharedPtr<FRow>)
			{
				TSharedPtr<FOKStudioRowDrag> Op = E.GetOperationAs<FOKStudioRowDrag>();
				if (!Op || !CanDropOn(Op->Ids, Id)) return FReply::Unhandled();
				DropOn(Op->Ids, Id);
				return FReply::Handled();
			})
			[
				SNew(SBox).HeightOverride(18.f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						SNew(SBox).WidthOverride(13.f).HeightOverride(13.f)
						[
							bParent
							? StaticCastSharedRef<SWidget>(SNew(SButton).ButtonStyle(&OKStudioStyle::FlatButton()).ContentPadding(0.f).HAlign(HAlign_Center).VAlign(VAlign_Center)
								.OnClicked_Lambda([this, Id] { if (Collapsed.Contains(Id)) Collapsed.Remove(Id); else Collapsed.Add(Id); DeferRebuild(); return FReply::Handled(); })
								[OKStudioStyle::Label(FText::FromString(Collapsed.Contains(Id) ? TEXT("▶") : TEXT("▼")), 9.f, TextSoft())])
							: StaticCastSharedRef<SWidget>(SNew(SSpacer))
						]
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(1.f, 0.f, 2.f, 0.f)
					[
						SNew(SCheckBox).Style(&CheckStyle()).ToolTipText(FText::FromString(TEXT("顯示")))
						.IsChecked_Lambda([this, Id] { const FOKSceneObject* O = Game.IsValid() ? Game->Document->Find(Id) : nullptr; return O && O->bVisible ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
						.OnCheckStateChanged_Lambda([this, Id](ECheckBoxState S) { if (FOKSceneObject* O = Game.IsValid() ? Game->Document->Find(Id) : nullptr) { O->bVisible = S == ECheckBoxState::Checked; Changed(); } })
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 3.f, 0.f)[SNew(SIcon).Icon(Icon).Size(12.f).Color(TextSoft())]
					+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
					[
						SAssignNew(Name, SInlineEditableTextBlock).Style(&NameStyle)
						.Text_Lambda([this, Id]
						{
							const FOKSceneObject* O = Game.IsValid() ? Game->Document->Find(Id) : nullptr;
							if (!O) return FText::GetEmpty();
							const bool bError = Game->SceneActor && Game->SceneActor->Errors.Contains(Id);
							return FText::FromString(FString::Printf(TEXT("%s%s%s"), *O->Name, O->AttachBone.IsEmpty() ? TEXT("") : *(TEXT("　@") + O->AttachBone), bError ? TEXT("（未顯示）") : TEXT("")));
						})
						.ToolTipText_Lambda([this, Id] { const FString* E = Game.IsValid() && Game->SceneActor ? Game->SceneActor->Errors.Find(Id) : nullptr; return FText::FromString(E ? *E : FString()); })
						.OnBeginTextEdit_Lambda([this, Id](const FText&) { if (const FOKSceneObject* O = Game.IsValid() ? Game->Document->Find(Id) : nullptr) if (TSharedPtr<SInlineEditableTextBlock> N = RowNames.FindRef(Id).Pin()) N->SetText(FText::FromString(O->Name)); })
						.OnTextCommitted_Lambda([this, Id](const FText& T, ETextCommit::Type Commit)
						{
							// Enter or leaving the field renames; Esc, an empty or unchanged name do not (and add no history step).
							FOKSceneObject* O = Game.IsValid() ? Game->Document->Find(Id) : nullptr;
							const FString New = T.ToString().TrimStartAndEnd();
							if (O && Commit != ETextCommit::OnCleared && !New.IsEmpty() && New != O->Name) { O->Name = New; Changed(); }
							DeferRebuild();
						})
					]
				]
			];
		RowNames.Add(Id, Name);
		return Out;
	}

	FOKSceneObject* Obj() const { return Game.IsValid() ? Game->Document->Find(Game->SelectedId) : nullptr; }
	void Changed() { if (Game.IsValid()) Game->Document->NotifyChanged(); }

	/** A section heading of the detail window. */
	void Label(const FString& Text)
	{
		Props->AddSlot().AutoHeight().Padding(0.f, 6.f, 0.f, 1.f)[OKStudioStyle::Label(FText::FromString(Text), 11.f, OKStudioStyle::TextSoft())];
	}
	/** A row: name on the left (40 %), the control on the right. */
	void Row(const FString& Name, const TSharedRef<SWidget>& Control, float NameShare = 0.4f)
	{
		Props->AddSlot().AutoHeight().Padding(0.f, 1.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(NameShare).VAlign(VAlign_Center)[OKStudioStyle::Label(FText::FromString(Name), 10.f)]
			+ SHorizontalBox::Slot().FillWidth(1.f - NameShare)[SNew(SBox).HeightOverride(18.f)[Control]]
		];
	}
	/** Number box in the theme: drag to change, click to type. Dragging counts as one undo step. */
	TSharedRef<SWidget> Spin(TFunction<float()> Get, TFunction<void(float)> Set, float Min, float Max, float Delta)
	{
		return SNew(SSpinBox<float>).Style(&OKStudioStyle::SpinStyle()).Font(OKStudioStyle::Font(10.f)).MinValue(Min).MaxValue(Max).Delta(Delta)
			.Value_Lambda([Get] { return Get(); })
			.OnValueChanged_Lambda([Set](float V) { Set(V); })
			.OnBeginSliderMovement_Lambda([this] { if (Game.IsValid()) Game->bUserDragging = true; })
			.OnEndSliderMovement_Lambda([this](float) { if (Game.IsValid()) Game->bUserDragging = false; });
	}
	void Float(const FString& Name, TFunction<float(FOKSceneObject&)> Get, TFunction<void(FOKSceneObject&, float)> Set, float Min, float Max, float Delta)
	{
		Row(Name, Spin([this, Get] { FOKSceneObject* O = Obj(); return O ? Get(*O) : 0.f; }, [this, Set](float V) { if (FOKSceneObject* O = Obj()) { Set(*O, V); Changed(); } }, Min, Max, Delta));
	}
	TSharedRef<SWidget> CheckBox(const FString& Name, TFunction<bool()> Get, TFunction<void(bool)> Set)
	{
		return SNew(SCheckBox).Style(&OKStudioStyle::CheckStyle())
			.IsChecked_Lambda([Get] { return Get() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
			.OnCheckStateChanged_Lambda([Set](ECheckBoxState S) { Set(S == ECheckBoxState::Checked); })
			[SNew(SBox).Padding(FMargin(4.f, 0.f, 0.f, 0.f))[OKStudioStyle::Label(FText::FromString(Name), 10.f)]];
	}
	void Check(const FString& Name, TFunction<bool(FOKSceneObject&)> Get, TFunction<void(FOKSceneObject&, bool)> Set)
	{
		Props->AddSlot().AutoHeight().Padding(0.f, 1.f)
			[CheckBox(Name, [this, Get] { FOKSceneObject* O = Obj(); return O && Get(*O); }, [this, Set](bool B) { if (FOKSceneObject* O = Obj()) { Set(*O, B); Changed(); } })];
	}
	TSharedRef<SWidget> TextBox(TFunction<FString()> Get, TFunction<void(const FString&)> Set, const FString& Hint = FString())
	{
		return SNew(SEditableTextBox).Style(&OKStudioStyle::TextBoxStyle()).HintText(FText::FromString(Hint))
			.Text_Lambda([Get] { return FText::FromString(Get()); })
			.OnTextCommitted_Lambda([Set](const FText& T, ETextCommit::Type) { Set(T.ToString()); });
	}
	void Text(const FString& Name, TFunction<FString(FOKSceneObject&)> Get, TFunction<void(FOKSceneObject&, const FString&)> Set)
	{
		Row(Name, TextBox([this, Get] { FOKSceneObject* O = Obj(); return O ? Get(*O) : FString(); }, [this, Set](const FString& V) { if (FOKSceneObject* O = Obj()) { Set(*O, V); Changed(); RefreshRows(); } }), 0.3f);
	}
	void Cycle(const FString& Name, TFunction<FString(FOKSceneObject&)> Show, TFunction<void(FOKSceneObject&)> Next)
	{
		Row(Name, OKStudioStyle::Button(TAttribute<FText>::CreateLambda([this, Show] { FOKSceneObject* O = Obj(); return FText::FromString(O ? Show(*O) : FString()); }),
			[this, Next] { if (FOKSceneObject* O = Obj()) { Next(*O); Changed(); } }, 0.f, 18.f, false, 10.f, OKStudioStyle::Input()));
	}
	/** A button that changes how many rows the object shows (adds or removes entries): the panel rebuilds after it. */
	void Action(const FString& Name, TFunction<FString(FOKSceneObject&)> Show, TFunction<void(FOKSceneObject&)> Do)
	{
		Row(Name, OKStudioStyle::Button(TAttribute<FText>::CreateLambda([this, Show] { FOKSceneObject* O = Obj(); return FText::FromString(O ? Show(*O) : FString()); }),
			[this, Do] { if (FOKSceneObject* O = Obj()) { Do(*O); Changed(); DeferRebuild(); } }, 0.f, 18.f, false, 10.f));
	}
	/** One choice of several as a row of buttons (the chosen one green). */
	void Choice(const FString& Name, const TArray<FString>& Options, TFunction<int32(FOKSceneObject&)> Get, TFunction<void(FOKSceneObject&, int32)> Set)
	{
		TSharedRef<SHorizontalBox> Box = SNew(SHorizontalBox);
		for (int32 I = 0; I < Options.Num(); ++I)
			Box->AddSlot().FillWidth(1.f).Padding(0.f, 0.f, 1.f, 0.f)
			[
				OKStudioStyle::Button(FText::FromString(Options[I]), [this, Set, I] { if (FOKSceneObject* O = Obj()) { Set(*O, I); Changed(); } }, 0.f, 18.f,
					TAttribute<bool>::CreateLambda([this, Get, I] { FOKSceneObject* O = Obj(); return O && Get(*O) == I; }), 10.f)
			];
		Row(Name, Box, Options.Num() > 3 ? 0.25f : 0.4f);
	}
	/** Choice buttons of a value that is not on the selected object (the scene's). */
	void ChoiceF(const FString& Name, const TArray<FString>& Options, TFunction<int32()> Get, TFunction<void(int32)> Set)
	{
		TSharedRef<SHorizontalBox> Box = SNew(SHorizontalBox);
		for (int32 I = 0; I < Options.Num(); ++I)
			Box->AddSlot().FillWidth(1.f).Padding(0.f, 0.f, 1.f, 0.f)
				[OKStudioStyle::Button(FText::FromString(Options[I]), [Set, I] { Set(I); }, 0.f, 18.f, TAttribute<bool>::CreateLambda([Get, I] { return Get() == I; }), 10.f)];
		Row(Name, Box, Options.Num() > 3 ? 0.25f : 0.4f);
	}
	TArray<TSharedPtr<int32>> FilterOptions;
	FString PresetName;
	/** A drop-down of named values. */
	TSharedRef<SWidget> Combo(TArray<TSharedPtr<int32>>* Options, TFunction<FString(int32)> Show, TFunction<int32()> Current, TFunction<void(int32)> Pick)
	{
		return SNew(SComboBox<TSharedPtr<int32>>).ComboBoxStyle(&OKStudioStyle::ComboStyle()).ItemStyle(&OKStudioStyle::RowStyle()).OptionsSource(Options)
			.OnGenerateWidget_Lambda([Show](TSharedPtr<int32> V) { return OKStudioStyle::Label(FText::FromString(Show(*V)), 10.f); })
			.OnSelectionChanged_Lambda([Pick](TSharedPtr<int32> V, ESelectInfo::Type Info) { if (V && Info != ESelectInfo::Direct) Pick(*V); })
			[OKStudioStyle::Label(TAttribute<FText>::CreateLambda([Show, Current] { return FText::FromString(Show(Current())); }), 10.f)];
	}
	/** Named pattern lists (TArray of options kept per list; -1 first: "the card's / the animation's"). */
	TMap<FString, TSharedRef<TArray<TSharedPtr<int32>>>> NamedOptions;
	void Named(const FString& Name, const FString& ListName, int32 Count, TFunction<FString(int32)> Show, TFunction<int32(FOKSceneObject&)> Get, TFunction<void(FOKSceneObject&, int32)> Set)
	{
		if (!NamedOptions.Contains(ListName)) NamedOptions.Add(ListName, MakeShared<TArray<TSharedPtr<int32>>>());
		TArray<TSharedPtr<int32>>& Options = NamedOptions[ListName].Get();
		if (Options.Num() != Count + 1) { Options.Reset(); for (int32 I = -1; I < Count; ++I) Options.Add(MakeShared<int32>(I)); }
		Row(Name, Combo(&Options, Show, [this, Get] { FOKSceneObject* O = Obj(); return O ? Get(*O) : -1; }, [this, Set](int32 V) { if (FOKSceneObject* O = Obj()) { Set(*O, V); Changed(); } }));
	}
	/** The detail window's rows for the current page (HS2Studio6: one window per menu entry). */
	void RebuildProperties()
	{
		if (!Props) return;
		Props->ClearChildren();
		switch (Page)
		{
		case EPage::None: case EPage::Files: case EPage::CharLoader: return;
		case EPage::Scene: SceneSettingsPage(); return;
		case EPage::ItemAdd: ItemCatalog(); return;
		case EPage::LightAdd: LightCatalog(); return;
		case EPage::Camera: CameraPage(); return;
		default: break;
		}
		FOKSceneObject* O = Obj();
		if (!O) { Label(TEXT("先選擇一個物件")); return; }
		const bool bCharacter = O->Kind == EOKSceneObjectKind::Character;
		if (Page == EPage::State || Page == EPage::Bones || Page == EPage::Anim)
		{
			if (!bCharacter) { Label(TEXT("先選擇一個角色")); return; }
			if (Page == EPage::State) CharacterState(O);
			else if (Page == EPage::Anim) CharacterAnim(O);
			else CharacterBones(O);
			return;
		}
		Text(TEXT("名稱"), [](FOKSceneObject& X) { return X.Name; }, [](FOKSceneObject& X, const FString& V) { X.Name = V; });
		Check(TEXT("顯示"), [](FOKSceneObject& X) { return X.bVisible; }, [](FOKSceneObject& X, bool V) { X.bVisible = V; });
		if (!O->AttachBone.IsEmpty())
			Action(TEXT("掛點"), [](FOKSceneObject& X) { return X.AttachBone + TEXT("（點一下取消）"); }, [](FOKSceneObject& X) { X.AttachBone.Reset(); });
		switch (O->Kind)
		{
		case EOKSceneObjectKind::Character:
		{
			Label(TEXT("角色"));
			Row(TEXT("卡片"), OKStudioStyle::Button(TAttribute<FText>::CreateLambda([this] { FOKSceneObject* X = Obj(); return FText::FromString(X ? FPaths::GetCleanFilename(X->Character.Card) : FString()); }),
				[this] { OpenFloat(TEXT("cards")); }, 0.f, 18.f, false, 10.f, OKStudioStyle::Input()), 0.3f);
			Choice(TEXT("服裝組"), { TEXT("1"), TEXT("2"), TEXT("3"), TEXT("4"), TEXT("5"), TEXT("6"), TEXT("7") }, [](FOKSceneObject& X) { return X.Character.Outfit; }, [](FOKSceneObject& X, int32 V) { X.Character.Outfit = V; });
			Label(TEXT("狀態、骨骼、動畫在「角色」選單"));
			break;
		}
		case EOKSceneObjectKind::Item:
			ItemBrowser(*O);
			{
				const FOKStudioCatalogItem* Entry = O->Item.No >= 0 ? FOKStudioCatalog::Get().FindItem(O->Item.Group, O->Item.Category, O->Item.No) : nullptr;
				for (int32 Index = 0; Index < 3; ++Index)
				{
					if (Entry && !Entry->Colors[Index]) continue;
					// Colours are kept as sRGB values (0..1) in the scene; alpha < 0: the model's own colour.
					ColorRow(FString::Printf(TEXT("顏色%d"), Index + 1),
						[Index](FOKSceneObject& X) { return X.Item.Colors.IsValidIndex(Index) ? X.Item.Colors[Index] : FLinearColor(1.f, 1.f, 1.f, -1.f); },
						[Index](FOKSceneObject& X, const FLinearColor& C) { while (X.Item.Colors.Num() <= Index) X.Item.Colors.Add(FLinearColor(1.f, 1.f, 1.f, -1.f)); X.Item.Colors[Index] = C; });
				}
				// Pattern slots the item offers (ItemList 柄): pattern (character list mt_pattern id, 0 none), colour, scale.
				auto Slot = [](FOKSceneObject& X, int32 Index) -> FOKSceneItemPattern& { while (X.Item.Patterns.Num() <= Index) X.Item.Patterns.AddDefaulted(); return X.Item.Patterns[Index]; };
				for (int32 Index = 0; Index < 3; ++Index)
				{
					if (!Entry || !Entry->Patterns[Index]) continue;
					Float(FString::Printf(TEXT("花紋%d（0 無）"), Index + 1), [Index](FOKSceneObject& X) { return X.Item.Patterns.IsValidIndex(Index) ? float(X.Item.Patterns[Index].Pattern) : 0.f; },
						[Index, Slot](FOKSceneObject& X, float V) { Slot(X, Index).Pattern = FMath::Clamp(FMath::RoundToInt(V), 0, 9999); }, 0.f, 400.f, 1.f);
					ColorRow(FString::Printf(TEXT("花紋%d 顏色"), Index + 1),
						[Index](FOKSceneObject& X) { return X.Item.Patterns.IsValidIndex(Index) ? X.Item.Patterns[Index].Color : FLinearColor(100.f / 255.f, 100.f / 255.f, 100.f / 255.f, 1.f); },
						[Index, Slot](FOKSceneObject& X, const FLinearColor& C) { Slot(X, Index).Color = FLinearColor(C.R, C.G, C.B, C.A < 0.f ? 1.f : C.A); });
					Float(FString::Printf(TEXT("花紋%d 縮放"), Index + 1), [Index](FOKSceneObject& X) { return X.Item.Patterns.IsValidIndex(Index) ? float(X.Item.Patterns[Index].Uv.Z) : 1.f; },
						[Index, Slot](FOKSceneObject& X, float V) { FOKSceneItemPattern& P = Slot(X, Index); P.Uv.Z = P.Uv.W = FMath::Max(V, 0.01f); }, 0.01f, 20.f, 0.05f);
				}
				// Studio's item shadow colour, fading (items that offer it) and outline width.
				ColorRow(TEXT("影色"), [](FOKSceneObject& X) { return X.Item.ShadowColor; }, [](FOKSceneObject& X, const FLinearColor& C) { X.Item.ShadowColor = C; });
				if (!Entry || Entry->bAlpha)
					Float(TEXT("透明度"), [](FOKSceneObject& X) { return X.Item.Alpha; }, [](FOKSceneObject& X, float V) { X.Item.Alpha = FMath::Clamp(V, 0.f, 1.f); }, 0.f, 1.f, 0.05f);
				Float(TEXT("描邊寬度"), [](FOKSceneObject& X) { return X.Item.LineWidth; }, [](FOKSceneObject& X, float V) { X.Item.LineWidth = FMath::Max(V, 0.f); }, 0.f, 2.f, 0.05f);
				// Material Editor style edits: material (empty: every material), shader property, and a colour (#RRGGBB or
				// #RRGGBBAA, sRGB) or a number; imported textures, texture scale/offset, shaders and renderer switches are
				// shown and can be deleted. Changing them rebuilds the item.
				Label(TEXT("材質編輯（材質名空白＝全部；值 #RRGGBB 是顏色，數字是數值）"));
				for (int32 Index = 0; Index < O->Item.MaterialEdits.Num(); ++Index)
				{
					auto Edit = [Index](FOKSceneObject& X) -> FOKSceneMaterialEdit* { return X.Item.MaterialEdits.IsValidIndex(Index) ? &X.Item.MaterialEdits[Index] : nullptr; };
					const FOKSceneMaterialEdit& Shown = O->Item.MaterialEdits[Index];
					if (Shown.Kind != EOKSceneMaterialEditKind::Float && Shown.Kind != EOKSceneMaterialEditKind::Color)
					{
						const FString What = Shown.Kind == EOKSceneMaterialEditKind::Texture ? FString::Printf(TEXT("貼圖 %s（%d 字元）"), *Shown.Property, Shown.Text.Len())
							: Shown.Kind == EOKSceneMaterialEditKind::TextureST ? FString::Printf(TEXT("%s 縮放 %g,%g 位移 %g,%g"), *Shown.Property, Shown.Value.R, Shown.Value.G, Shown.Value.B, Shown.Value.A)
							: Shown.Kind == EOKSceneMaterialEditKind::Shader ? TEXT("shader ") + Shown.Text
							: FString::Printf(TEXT("renderer %s = %g"), *Shown.Property, Shown.Value.R);
						Label(FString::Printf(TEXT("%d %s：%s"), Index + 1, *Shown.Material, *What));
						Action(FString::Printf(TEXT("%d 刪除"), Index + 1), [](FOKSceneObject&) { return FString(TEXT("刪除這筆")); },
							[Index](FOKSceneObject& X) { if (X.Item.MaterialEdits.IsValidIndex(Index)) X.Item.MaterialEdits.RemoveAt(Index); });
						continue;
					}
					Text(FString::Printf(TEXT("%d 材質"), Index + 1), [Edit](FOKSceneObject& X) { const FOKSceneMaterialEdit* E = Edit(X); return E ? E->Material : FString(); },
						[Edit](FOKSceneObject& X, const FString& V) { if (FOKSceneMaterialEdit* E = Edit(X)) E->Material = V.TrimStartAndEnd(); });
					{
						// The property: picked from the item's materials, or typed (advanced).
						if (!MaterialPropertyOptions.Contains(O->Id)) MaterialPropertyOptions.Add(O->Id, MakeShared<TArray<TSharedPtr<FString>>>());
						TArray<TSharedPtr<FString>>& Options = MaterialPropertyOptions[O->Id].Get();
						if (Options.IsEmpty()) for (const FString& P : MaterialProperties(O->Id)) Options.Add(MakeShared<FString>(P));
						Row(FString::Printf(TEXT("%d 屬性"), Index + 1), SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().FillWidth(0.55f)
							[
								SNew(SComboBox<TSharedPtr<FString>>).ComboBoxStyle(&OKStudioStyle::ComboStyle()).ItemStyle(&OKStudioStyle::RowStyle()).OptionsSource(&Options)
								.OnGenerateWidget_Lambda([](TSharedPtr<FString> P) { return OKStudioStyle::Label(FText::FromString(*P), 10.f); })
								.OnSelectionChanged_Lambda([this, Edit](TSharedPtr<FString> P, ESelectInfo::Type Info)
								{
									if (!P || Info == ESelectInfo::Direct) return;
									FOKSceneObject* X = Obj();
									FOKSceneMaterialEdit* E = X ? Edit(*X) : nullptr;
									if (!E) return;
									// "_Color (color)" -> the property and the value type.
									FString Name = *P, Kind;
									P->Split(TEXT(" "), &Name, &Kind);
									E->Property = Name;
									if (Kind.Contains(TEXT("color"))) E->Kind = EOKSceneMaterialEditKind::Color; else if (Kind.Contains(TEXT("float"))) E->Kind = EOKSceneMaterialEditKind::Float;
									Changed(); DeferRebuild();
								})
								[OKStudioStyle::Label(FText::FromString(TEXT("選擇…")), 10.f)]
							]
							+ SHorizontalBox::Slot().FillWidth(0.45f).Padding(2.f, 0.f, 0.f, 0.f)
							[TextBox([this, Edit] { FOKSceneObject* X = Obj(); const FOKSceneMaterialEdit* E = X ? Edit(*X) : nullptr; return E ? E->Property : FString(); },
								[this, Edit](const FString& V) { if (FOKSceneObject* X = Obj()) if (FOKSceneMaterialEdit* E = Edit(*X)) { E->Property = V.TrimStartAndEnd(); Changed(); } })], 0.3f);
					}
					if (Shown.Kind == EOKSceneMaterialEditKind::Color)
						ColorRow(FString::Printf(TEXT("%d 顏色"), Index + 1), [Edit](FOKSceneObject& X) { const FOKSceneMaterialEdit* E = Edit(X); return E ? E->Value : FLinearColor::White; },
							[Edit](FOKSceneObject& X, const FLinearColor& C) { if (FOKSceneMaterialEdit* E = Edit(X)) E->Value = C.A < 0.f ? FLinearColor(C.R, C.G, C.B, 1.f) : C; });
					else
						Float(FString::Printf(TEXT("%d 數值"), Index + 1), [Edit](FOKSceneObject& X) { const FOKSceneMaterialEdit* E = Edit(X); return E ? E->Value.R : 0.f; },
							[Edit](FOKSceneObject& X, float V) { if (FOKSceneMaterialEdit* E = Edit(X)) E->Value.R = V; }, -100.f, 100.f, 0.01f);
					Text(FString::Printf(TEXT("%d 值"), Index + 1),
						[Edit](FOKSceneObject& X)
						{
							const FOKSceneMaterialEdit* E = Edit(X);
							if (!E) return FString();
							return E->Kind == EOKSceneMaterialEditKind::Color ? TEXT("#") + E->Value.ToFColor(false).ToHex() : FString::SanitizeFloat(E->Value.R);
						},
						[Edit](FOKSceneObject& X, const FString& V)
						{
							FOKSceneMaterialEdit* E = Edit(X);
							if (!E) return;
							FString S = V.TrimStartAndEnd();
							if (S.RemoveFromStart(TEXT("#")) && (S.Len() == 6 || S.Len() == 8))
							{
								const FColor C = FColor::FromHex(S.Len() == 6 ? S + TEXT("FF") : S);
								E->Kind = EOKSceneMaterialEditKind::Color;
								E->Value = FLinearColor(C.R / 255.f, C.G / 255.f, C.B / 255.f, C.A / 255.f);
							}
							else if (S.IsNumeric()) { E->Kind = EOKSceneMaterialEditKind::Float; E->Value = FLinearColor(FCString::Atof(*S), 0.f, 0.f, 0.f); }
						});
					Action(FString::Printf(TEXT("%d 刪除"), Index + 1), [](FOKSceneObject&) { return FString(TEXT("刪除這筆")); },
						[Index](FOKSceneObject& X) { if (X.Item.MaterialEdits.IsValidIndex(Index)) X.Item.MaterialEdits.RemoveAt(Index); });
				}
				Action(TEXT("材質編輯"), [](FOKSceneObject& X) { return FString::Printf(TEXT("新增（目前 %d 筆）"), X.Item.MaterialEdits.Num()); },
					[](FOKSceneObject& X) { FOKSceneMaterialEdit E; E.Property = TEXT("_Color"); E.Kind = EOKSceneMaterialEditKind::Color; X.Item.MaterialEdits.Add(E); });
			}
			ItemFkProperties(*O);
			Label(TEXT("或直接指定基礎庫模型"));
			Text(TEXT("模型 id"), [](FOKSceneObject& X) { return X.Item.Model; }, [](FOKSceneObject& X, const FString& V) { X.Item.Model = V.TrimStartAndEnd(); X.Item.No = -1; });
			Text(TEXT("prefab"), [](FOKSceneObject& X) { return X.Item.Prefab; }, [](FOKSceneObject& X, const FString& V) { X.Item.Prefab = V.TrimStartAndEnd(); X.Item.No = -1; });
			break;
		case EOKSceneObjectKind::Effect:
		{
			Label(TEXT("特效（Niagara）"));
			auto EffectName = [](const FString& Id) { const FOKStudioEffect* E = FOKStudioCatalog::Get().FindEffect(Id); return E ? E->Category + TEXT("／") + E->Name : Id; };
			Row(TEXT("特效"), OKStudioStyle::Button(TAttribute<FText>::CreateLambda([this, EffectName] { FOKSceneObject* X = Obj(); return FText::FromString(X ? EffectName(X->Effect.Effect) : FString()); }),
				[this] { OpenFloat(TEXT("effects")); }, 0.f, 18.f, false, 10.f, OKStudioStyle::Input()), 0.3f);
			Float(TEXT("速度"), [](FOKSceneObject& X) { return X.Effect.Speed; }, [](FOKSceneObject& X, float V) { X.Effect.Speed = V; }, 0.f, 10.f, 0.05f);
			Float(TEXT("強度"), [](FOKSceneObject& X) { return X.Effect.Intensity; }, [](FOKSceneObject& X, float V) { X.Effect.Intensity = V; }, 0.f, 20.f, 0.05f);
			ColorRow(TEXT("顏色"), [](FOKSceneObject& X) { return X.Effect.Color; }, [](FOKSceneObject& X, const FLinearColor& C) { X.Effect.Color = C; });
			break;
		}
		case EOKSceneObjectKind::Light:
			Label(TEXT("燈光"));
			// On/off is the object's visibility (the tree's eye): a switched-off light lights nothing, characters included.
			Check(TEXT("開啟"), [](FOKSceneObject& X) { return X.bVisible; }, [](FOKSceneObject& X, bool V) { X.bVisible = V; });
			Choice(TEXT("種類"), { TEXT("方向光"), TEXT("點光"), TEXT("聚光") }, [](FOKSceneObject& X) { return int32(X.Light.Type); }, [](FOKSceneObject& X, int32 V) { X.Light.Type = EOKSceneLightType(V); });
			Float(TEXT("強度"), [](FOKSceneObject& X) { return X.Light.Intensity; }, [](FOKSceneObject& X, float V) { X.Light.Intensity = V; }, 0.f, 200.f, 0.5f);
			if (O->Light.Type != EOKSceneLightType::Directional)
				Float(TEXT("範圍 cm"), [](FOKSceneObject& X) { return X.Light.Range; }, [](FOKSceneObject& X, float V) { X.Light.Range = V; }, 1.f, 10000.f, 10.f);
			if (O->Light.Type == EOKSceneLightType::Spot)
				Float(TEXT("聚光角"), [](FOKSceneObject& X) { return X.Light.SpotAngle; }, [](FOKSceneObject& X, float V) { X.Light.SpotAngle = V; }, 1.f, 80.f, 1.f);
			ColorRow(TEXT("顏色"), [](FOKSceneObject& X) { return X.Light.Color; }, [](FOKSceneObject& X, const FLinearColor& C) { X.Light.Color = FLinearColor(C.R, C.G, C.B, 1.f); });
			Check(TEXT("陰影"), [](FOKSceneObject& X) { return X.Light.bShadows; }, [](FOKSceneObject& X, bool V) { X.Light.bShadows = V; });
			Choice(TEXT("作用對象"), { TEXT("全部"), TEXT("只照角色與道具") }, [](FOKSceneObject& X) { return X.Light.bCharactersOnly ? 1 : 0; }, [](FOKSceneObject& X, int32 V) { X.Light.bCharactersOnly = V == 1; });
			break;
		case EOKSceneObjectKind::Camera:
			Label(TEXT("相機"));
			Float(TEXT("視角"), [](FOKSceneObject& X) { return X.Camera.Fov; }, [](FOKSceneObject& X, float V) { X.Camera.Fov = V; }, 5.f, 170.f, 1.f);
			Float(TEXT("近面 cm（0 預設）"), [](FOKSceneObject& X) { return X.Camera.Near; }, [](FOKSceneObject& X, float V) { X.Camera.Near = FMath::Max(V, 0.f); }, 0.f, 500.f, 0.5f);
			Float(TEXT("遠面 cm（只存檔）"), [](FOKSceneObject& X) { return X.Camera.Far; }, [](FOKSceneObject& X, float V) { X.Camera.Far = FMath::Max(V, 0.f); }, 0.f, 1000000.f, 100.f);
			Props->AddSlot().AutoHeight().Padding(0.f, 2.f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.f).Padding(0.f, 0.f, 2.f, 0.f)[OKStudioStyle::Button(FText::FromString(TEXT("用此相機")), Cmd(TEXT("camera.use")), 0.f, 20.f, false, 10.f)]
				+ SHorizontalBox::Slot().FillWidth(1.f).Padding(0.f, 0.f, 2.f, 0.f)[OKStudioStyle::Button(FText::FromString(TEXT("目前視角存入")), Cmd(TEXT("camera.store")), 0.f, 20.f, false, 10.f)]
			];
			break;
		default:
			break;
		}
	}

	/** 狀態: card, outfit, clothes, fluids, accessories. */
	void CharacterState(FOKSceneObject* O)
	{
			Label(TEXT("角色"));
			Row(TEXT("卡片"), OKStudioStyle::Button(TAttribute<FText>::CreateLambda([this] { FOKSceneObject* X = Obj(); return FText::FromString(X ? FPaths::GetCleanFilename(X->Character.Card) : FString()); }),
				[this] { OpenFloat(TEXT("cards")); }, 0.f, 18.f, false, 10.f, OKStudioStyle::Input()), 0.3f);
			Choice(TEXT("服裝組"), { TEXT("1"), TEXT("2"), TEXT("3"), TEXT("4"), TEXT("5"), TEXT("6"), TEXT("7") }, [](FOKSceneObject& X) { return X.Character.Outfit; }, [](FOKSceneObject& X, int32 V) { X.Character.Outfit = V; });
		Label(TEXT("服裝狀態"));
			for (int32 Part = 0; Part < 9; ++Part)
			{
				// 穿 / 半脫 / 脫 are the game's states 0, 1, 3.
				Choice(OKStudioPanelText::ClothesPart(Part), { TEXT("穿"), TEXT("半脫"), TEXT("脫") },
					[Part](FOKSceneObject& X) { const uint8 S = X.Character.ClothesStates.IsValidIndex(Part) ? X.Character.ClothesStates[Part] : 0; return S == 0 ? 0 : (S == 1 ? 1 : 2); },
					[Part](FOKSceneObject& X, int32 V) { X.Character.ClothesStates.SetNumZeroed(9); X.Character.ClothesStates[Part] = V == 0 ? 0 : (V == 1 ? 1 : 3); });
			}
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
			Cycle(TEXT("隱形身體"), [](FOKSceneObject& X) { return FString(X.Character.InvisibleBody < 0 ? TEXT("照卡片") : X.Character.InvisibleBody ? TEXT("只顯示衣服與飾品") : TEXT("顯示")); },
				[](FOKSceneObject& X) { X.Character.InvisibleBody = X.Character.InvisibleBody < 0 ? 1 : X.Character.InvisibleBody == 1 ? 0 : -1; });
			if (O->Character.AccessoriesShown.Num() == 21)
				for (int32 Slot = 0; Slot < 21; ++Slot)
					Check(Slot < 20 ? FString::Printf(TEXT("飾品 %d"), Slot + 1) : FString(TEXT("擴充飾品（MoreAccessories）")), [Slot](FOKSceneObject& X) { return X.Character.AccessoriesShown.IsValidIndex(Slot) && X.Character.AccessoriesShown[Slot] != 0; },
						[Slot](FOKSceneObject& X, bool bOn) { if (X.Character.AccessoriesShown.IsValidIndex(Slot)) X.Character.AccessoriesShown[Slot] = bOn ? 1 : 0; });
	}

	/** 動畫: speed and the Studio animation browser. */
	void CharacterAnim(FOKSceneObject* O)
	{
			Float(TEXT("動畫速度（0 定格）"), [](FOKSceneObject& X) { return X.Character.Speed; }, [](FOKSceneObject& X, float V) { X.Character.Speed = FMath::Max(V, 0.f); }, 0.f, 4.f, 0.05f);
			// The animation's time: with speed 0 the character holds this frame (the timeline keys it as 動畫時間).
			const FOKStudioAnim* Playing = O->Character.StudioAnimeNo >= 0 ? FOKStudioCatalog::Get().FindAnim(O->Character.StudioAnimeKind, O->Character.StudioAnimeGroup, O->Character.StudioAnimeCategory, O->Character.StudioAnimeNo) : nullptr;
			Float(TEXT("動畫時間 秒"), [](FOKSceneObject& X) { return X.Character.Time; }, [](FOKSceneObject& X, float V) { X.Character.Time = FMath::Max(V, 0.f); X.Character.Speed = 0.f; },
				0.f, Playing && Playing->Duration > 0.f ? Playing->Duration : 60.f, 0.01f);
			AnimBrowser(*O);
			VideoMotionList(*O);
	}

public:
	/** A video motion pair (Saved/VideoMotions/*.pair.json, written by the gallery's video import): its two .okanim files. */
	struct FVideoPair { FString Name, Female, Male; };
	static TArray<FVideoPair> VideoPairs()
	{
		TArray<FVideoPair> Out;
		const FString Dir = FPaths::ProjectSavedDir() / TEXT("VideoMotions");
		TArray<FString> Files;
		IFileManager::Get().FindFiles(Files, *(Dir / TEXT("*.pair.json")), true, false);
		Files.Sort();
		auto FileOf = [](const FString& Key) { FString Name = Key; Name.RemoveFromStart(TEXT("video:|")); return FString(TEXT("Saved/VideoMotions/")) + Name + TEXT(".okanim"); };
		for (const FString& F : Files)
		{
			FString Text;
			TSharedPtr<FJsonObject> J;
			if (!FFileHelper::LoadFileToString(Text, *(Dir / F)) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), J) || !J) continue;
			FVideoPair P;
			J->TryGetStringField(TEXT("name"), P.Name);
			FString Female, Male;
			J->TryGetStringField(TEXT("female"), Female);
			J->TryGetStringField(TEXT("male"), Male);
			if (P.Name.IsEmpty() || Female.IsEmpty() || Male.IsEmpty()) continue;
			P.Female = FileOf(Female); P.Male = FileOf(Male);
			Out.Add(P);
		}
		return Out;
	}
	/** .okanim files a character can play: the video motions and the studio's exported timelines. */
	static TArray<FString> AnimFiles()
	{
		TArray<FString> Out;
		for (const FString& Folder : { FString(TEXT("Saved/VideoMotions")), FString(TEXT("UserData/Studio/anim")) })
		{
			TArray<FString> Names;
			IFileManager::Get().FindFiles(Names, *(FPaths::ProjectDir() / Folder / TEXT("*.okanim")), true, false);
			Names.Sort();
			for (const FString& N : Names) Out.Add(Folder / N);
		}
		TArray<FString> SceneAnims;
		IFileManager::Get().FindFilesRecursive(SceneAnims, *(FPaths::ProjectDir() / TEXT("UserData/Studio/scene")), TEXT("*.okanim"), true, false);
		SceneAnims.Sort();
		const FString Proj = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
		for (FString Path : SceneAnims)
		{
			FPaths::MakePathRelativeTo(Path, *(Proj + TEXT("/")));
			Out.Add(Path);
		}
		return Out;
	}
	/** Plays an .okanim file on a character (the Studio animation stops). */
	bool PickAnimFile(FOKSceneObject& X, const FString& File)
	{
		if (X.Kind != EOKSceneObjectKind::Character) return false;
		X.Character.StudioAnimeNo = -1;
		X.Character.Animation.Reset();
		X.Character.AnimFile = File;
		X.Character.Time = 0.f;
		Changed();
		DeferRebuild();
		return true;
	}
	/** A video motion pair: the woman's motion on the primary character, the man's on the other selected character (or
	 *  the next character of the scene); both start together. */
	bool PickVideoPair(const FString& Name)
	{
		const TArray<FVideoPair> Pairs = VideoPairs();
		const FVideoPair* P = Pairs.FindByPredicate([&Name](const FVideoPair& V) { return V.Name == Name; });
		FOKSceneObject* First = Obj();
		if (!P || !First || First->Kind != EOKSceneObjectKind::Character) { SetStatus(TEXT("先選一個角色（雙人動作再加選第二個角色）")); return false; }
		int32 Other = -1;
		for (int32 Id : Game->Selection) if (Id != First->Id && Game->Document->Find(Id) && Game->Document->Find(Id)->Kind == EOKSceneObjectKind::Character) { Other = Id; break; }
		if (Other < 0) for (const FOKSceneObject& O : Game->Document->Objects) if (O.Id != First->Id && O.Kind == EOKSceneObjectKind::Character) { Other = O.Id; break; }
		const int32 FirstId = First->Id;
		PickAnimFile(*First, P->Female);
		if (FOKSceneObject* Partner = Game->Document->Find(Other)) PickAnimFile(*Partner, P->Male);
		if (FOKSceneObject* Again = Game->Document->Find(FirstId)) Again->Character.Time = 0.f;
		SetStatus(Other >= 0 ? FString::Printf(TEXT("雙人動作 %s：#%d 女、#%d 男"), *Name, FirstId, Other) : FString::Printf(TEXT("雙人動作 %s：只有一個角色，只套了女方"), *Name));
		return true;
	}
	void VideoMotionList(const FOKSceneObject& O)
	{
		using namespace OKStudioStyle;
		const TArray<FVideoPair> Pairs = VideoPairs();
		const TArray<FString> Files = AnimFiles();
		Label(TEXT("影片動作與 OK 動畫檔") + (O.Character.AnimFile.IsEmpty() ? FString() : TEXT("（播放中：") + FPaths::GetBaseFilename(O.Character.AnimFile) + TEXT("）")));
		if (Pairs.IsEmpty() && Files.IsEmpty()) { Label(TEXT("沒有影片動作（讀卡畫面的「影片動作」匯入後會出現在這裡）")); return; }
		for (const FVideoPair& P : Pairs)
		{
			const FString Name = P.Name;
			Props->AddSlot().AutoHeight().Padding(0.f, 1.f)[Button(FText::FromString(TEXT("雙人：") + Name), [this, Name] { PickVideoPair(Name); }, 0.f, 18.f, false, 10.f, RowFill())];
		}
		for (const FString& F : Files)
			Props->AddSlot().AutoHeight().Padding(0.f, 1.f)
				[Button(FText::FromString(FPaths::GetBaseFilename(F)), [this, F] { if (FOKSceneObject* X = Obj()) PickAnimFile(*X, F); }, 0.f, 18.f,
					TAttribute<bool>::CreateLambda([this, F] { const FOKSceneObject* X = Obj(); return X && X->Character.AnimFile == F; }), 10.f, RowFill())];
	}

	/** 骨骼 pages: FK, IK, eyes, neck, expression, hands, pose files. */
	void CharacterBones(FOKSceneObject* O)
	{
			auto LookName = [](EOKSceneLook L) { return FString(L == EOKSceneLook::Fixed ? TEXT("固定姿勢") : L == EOKSceneLook::Camera ? TEXT("看鏡頭") : L == EOKSceneLook::Point ? TEXT("看注視點") : TEXT("照動畫")); };
		switch (BonePage)
		{
		case 0: FkPage(*O); return;
		case 1: IkPage(*O); return;
		case 2: LookPage(*O, false); return;
		case 3: LookPage(*O, true); return;
		case 4: ExpressionPage(*O); return;
		case 5: HandPage(*O); return;
		case 6: PoseFilesPage(); return;
		default: break;
		}
		switch (BonePage)
		{
		case 2:
			Label(TEXT("視線"));
			Cycle(TEXT("眼睛"), [LookName](FOKSceneObject& X) { return LookName(X.Character.EyesLook); }, [](FOKSceneObject& X) { X.Character.EyesLook = EOKSceneLook((uint8(X.Character.EyesLook) + 1) % (X.Character.FixedEyes.Num() == 12 ? 4 : 3)); });
			Label(TEXT("注視點（cm，角色空間）"));
			for (int32 Axis = 0; Axis < 3; ++Axis)
				Float(FString(TEXT("XYZ")).Mid(Axis, 1), [Axis](FOKSceneObject& X) { return float(X.Character.LookPoint[Axis]); }, [Axis](FOKSceneObject& X, float V) { X.Character.LookPoint[Axis] = V; }, -1000.f, 1000.f, 1.f);
			break;
		case 3:
			Label(TEXT("頸部"));
			Cycle(TEXT("脖子"), [LookName](FOKSceneObject& X) { return LookName(X.Character.NeckLook); }, [](FOKSceneObject& X) { X.Character.NeckLook = EOKSceneLook((uint8(X.Character.NeckLook) + 1) % (X.Character.FixedNeck.Num() == 2 ? 4 : 3)); });
			break;
		case 4:
			Label(TEXT("表情（-1 照卡片）"));
			Float(TEXT("眉"), [](FOKSceneObject& X) { return float(X.Character.EyebrowPattern); }, [](FOKSceneObject& X, float V) { X.Character.EyebrowPattern = FMath::RoundToInt(V); }, -1.f, 40.f, 1.f);
			Float(TEXT("眼"), [](FOKSceneObject& X) { return float(X.Character.EyesPattern); }, [](FOKSceneObject& X, float V) { X.Character.EyesPattern = FMath::RoundToInt(V); }, -1.f, 40.f, 1.f);
			Float(TEXT("嘴"), [](FOKSceneObject& X) { return float(X.Character.MouthPattern); }, [](FOKSceneObject& X, float V) { X.Character.MouthPattern = FMath::RoundToInt(V); }, -1.f, 40.f, 1.f);
			Float(TEXT("張嘴"), [](FOKSceneObject& X) { return X.Character.MouthOpen; }, [](FOKSceneObject& X, float V) { X.Character.MouthOpen = V; }, -1.f, 1.f, 0.05f);
			break;
		case 5:
			Label(TEXT("手型（-1 照動畫）"));
			Float(TEXT("左手"), [](FOKSceneObject& X) { return float(X.Character.HandLeft); }, [](FOKSceneObject& X, float V) { X.Character.HandLeft = FMath::RoundToInt(V); }, -1.f, 40.f, 1.f);
			Float(TEXT("右手"), [](FOKSceneObject& X) { return float(X.Character.HandRight); }, [](FOKSceneObject& X, float V) { X.Character.HandRight = FMath::RoundToInt(V); }, -1.f, 40.f, 1.f);
			break;
		default: PoseFiles(); break;
		}
	}

	/** Character loader: the cards of Content/CharacterCards; add one or replace the selected character's card. */
	void CharacterLoader()
	{
		using namespace OKStudioStyle;
		Label(FString::Printf(TEXT("角色卡（%d 張）"), Cards.Num()));
		Props->AddSlot().AutoHeight().MaxHeight(300.f)
		[
			SNew(SListView<TSharedPtr<FString>>).ListItemsSource(&Cards).SelectionMode(ESelectionMode::Single)
			.OnGenerateRow_Lambda([this](TSharedPtr<FString> C, const TSharedRef<STableViewBase>& Owner)
			{
				const int32 Index = Cards.IndexOfByKey(C);
				return SNew(STableRow<TSharedPtr<FString>>, Owner)[SNew(STextBlock).Font(Font(11.f)).ColorAndOpacity(Index == LoaderCard ? OKStudioStyle::On() : OKStudioStyle::Text()).Text(FText::FromString(FPaths::GetBaseFilename(*C)))];
			})
			.OnMouseButtonClick_Lambda([this](TSharedPtr<FString> C) { LoaderCard = Cards.IndexOfByKey(C); DeferRebuild(); })
		];
		Props->AddSlot().AutoHeight().Padding(0.f, 6.f, 0.f, 0.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()[Button(FText::FromString(TEXT("讀取")), Cmd(TEXT("add.character")), 160.f, 22.f)]
			+ SHorizontalBox::Slot().AutoWidth().Padding(6.f, 0.f)[Button(FText::FromString(TEXT("替換選中角色")), [this]
			{
				FOKSceneObject* X = Obj();
				if (!X || X->Kind != EOKSceneObjectKind::Character || !Cards.IsValidIndex(LoaderCard)) return;
				X->Character.Card = *Cards[LoaderCard]; X->Name = FPaths::GetBaseFilename(X->Character.Card); Changed(); DeferRebuild();
			}, 160.f, 22.f)]
		];
	}

	/** Item catalogue: a search box, then group, category and item columns (or the matches of the search). Left click adds
	 *  the item (or changes the selected item), middle click adds it under the selection; resting on an item shows its
	 *  full name and a picture of it. */
	void ItemCatalog()
	{
		using namespace OKStudioStyle;
		const FOKStudioCatalog& C = FOKStudioCatalog::Get();
		if (!C.bLoaded) { Label(C.Error); return; }
		Props->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 2.f)
		[
			SNew(SBox).HeightOverride(18.f)
			[
				SNew(SEditableTextBox).Style(&TextBoxStyle()).HintText(FText::FromString(TEXT("搜尋道具（空格＝且，-詞＝排除）"))).Text(FText::FromString(ItemSearch))
				.OnTextCommitted_Lambda([this](const FText& T, ETextCommit::Type) { ItemSearch = T.ToString(); DeferRebuild(); })
			]
		];
		if (BrowseGroup < 0 && C.Groups.Num()) BrowseGroup = C.Groups[0].Id;
		const TArray<const FOKStudioCatalogGroup*> Cats = C.CategoriesOf(BrowseGroup);
		if (!Cats.ContainsByPredicate([this](const FOKStudioCatalogGroup* G) { return G->Id == BrowseCategory; })) BrowseCategory = Cats.Num() ? Cats[0]->Id : -1;
		GroupOptions.Reset(); for (const FOKStudioCatalogGroup& G : C.Groups) GroupOptions.Add(MakeShared<int32>(G.Id));
		CategoryOptions.Reset(); for (const FOKStudioCatalogGroup* G : Cats) CategoryOptions.Add(MakeShared<int32>(G->Id));
		// Items: of the category, or every match of the search (as indices into the catalogue).
		ItemOptions.Reset();
		if (!ItemSearch.IsEmpty())
		{
			for (const FHit& H : SearchCatalog(ItemSearch, { TEXT("item") }, 400))
			{
				TArray<FString> P; H.Key.Mid(5).ParseIntoArray(P, TEXT("/"));
				if (P.Num() != 3) continue;
				const FOKStudioCatalogItem* I = C.FindItem(FCString::Atoi(*P[0]), FCString::Atoi(*P[1]), FCString::Atoi(*P[2]));
				if (I) ItemOptions.Add(MakeShared<int32>(int32(I - C.Items.GetData())));
			}
		}
		else for (const FOKStudioCatalogItem* I : C.ItemsIn(BrowseGroup, BrowseCategory)) ItemOptions.Add(MakeShared<int32>(int32(I - C.Items.GetData())));
		auto Column = [this](float Width, TArray<TSharedPtr<int32>>* Options, TFunction<FString(int32)> Name, TFunction<bool(int32)> IsOn, TFunction<void(int32)> Pick)
		{
			return SNew(SBox).WidthOverride(Width).HeightOverride(176.f)
			[
				SNew(SBorder).BorderImage(Brush(ListArea())).Padding(1.f)
				[
					SNew(SListView<TSharedPtr<int32>>).ListItemsSource(Options).SelectionMode(ESelectionMode::None)
					.OnGenerateRow_Lambda([Name, IsOn, Pick](TSharedPtr<int32> V, const TSharedRef<STableViewBase>& Owner)
					{
						const int32 Value = *V;
						return SNew(STableRow<TSharedPtr<int32>>, Owner).Style(&RowStyle()).Padding(FMargin(0.f, 0.f, 0.f, 1.f))
						[
							Fill(IsOn(Value), RowFill(), SNew(SButton).ButtonStyle(&FlatButton()).ContentPadding(FMargin(2.f, 0.f))
							.OnClicked_Lambda([Pick, Value] { Pick(Value); return FReply::Handled(); })
							[SNew(STextBlock).Font(Font(10.f)).ColorAndOpacity(OKStudioStyle::Text()).Text(FText::FromString(Name(Value)))])
						];
					})
				]
			];
		};
		auto GroupName = [](int32 Id) { const FOKStudioCatalogGroup* G = FOKStudioCatalog::Get().Groups.FindByPredicate([Id](const FOKStudioCatalogGroup& X) { return X.Id == Id; }); return G ? G->Name : FString(); };
		auto CategoryName = [this](int32 Id) { const int32 Group = BrowseGroup; const FOKStudioCatalogGroup* G = FOKStudioCatalog::Get().Categories.FindByPredicate([Id, Group](const FOKStudioCatalogGroup& X) { return X.Id == Id && X.Group == Group; }); return G ? G->Name : FString(); };
		// The item column: hover for the name and a picture, left click adds, middle click adds under the selection.
		TSharedRef<SWidget> Items = SNew(SBox).WidthOverride(ItemSearch.IsEmpty() ? 102.f : 264.f).HeightOverride(176.f)
		[
			SNew(SBorder).BorderImage(Brush(ListArea())).Padding(1.f)
			[
				SNew(SListView<TSharedPtr<int32>>).ListItemsSource(&ItemOptions).SelectionMode(ESelectionMode::None)
				.OnGenerateRow_Lambda([this](TSharedPtr<int32> V, const TSharedRef<STableViewBase>& Owner)
				{
					const FOKStudioCatalogItem& I = FOKStudioCatalog::Get().Items[*V];
					const int32 G = I.Group, Ca = I.Category, N = I.No;
					const FString Key = ItemKey(G, Ca, N);
					const bool bFavorite = FOKStudioSettings::Get().Favorites.Contains(Key);
					return SNew(STableRow<TSharedPtr<int32>>, Owner).Style(&RowStyle()).Padding(FMargin(0.f, 0.f, 0.f, 1.f))
					[
						SNew(SBorder).BorderImage(Brush(RowFill())).Padding(0.f)
						.OnMouseButtonDown_Lambda([this, G, Ca, N](const FGeometry&, const FPointerEvent& E)
						{
							if (E.GetEffectingButton() != EKeys::MiddleMouseButton) return FReply::Unhandled();
							AddItemTo(G, Ca, N, true);
							return FReply::Handled();
						})
						.ToolTip(SNew(SToolTip)
						[
							SNew(SVerticalBox)
							+ SVerticalBox::Slot().AutoHeight()[OKStudioStyle::Label(FText::FromString(FString::Printf(TEXT("%s　[%d/%d/%d]"), *I.Name, G, Ca, N)), 11.f)]
							+ SVerticalBox::Slot().AutoHeight()[OKStudioStyle::Label(FText::FromString(TEXT("左鍵加入（選著道具時替換）；中鍵加到所選物件底下")), 9.f, TextHint())]
							+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 3.f)
							[
								SNew(SBox).WidthOverride(256.f).HeightOverride(144.f)
								[
									SNew(SOverlay)
									+ SOverlay::Slot()[SNew(SImage).Image_Lambda([Key] { const FSlateBrush* B = FOKStudioThumbnails::Get().Find(Key); return B ? B : Brush(ListArea()); })]
									+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)[OKStudioStyle::Label(TAttribute<FText>::CreateLambda([Key] { return FText::FromString(FOKStudioThumbnails::Get().Find(Key) ? FString() : FString(TEXT("預覽產生中…"))); }), 10.f, TextHint())]
								]
							]
						])
						[
							SNew(SButton).ButtonStyle(&FlatButton()).ContentPadding(FMargin(2.f, 0.f))
							.OnHovered_Lambda([this, G, Ca, N] { HoverItem = FIntVector(G, Ca, N); HoverSince = FPlatformTime::Seconds(); })
							.OnUnhovered_Lambda([this] { HoverItem = FIntVector(-1); })
							.OnClicked_Lambda([this, G, Ca, N]
							{
								const FOKSceneObject* X = Obj();
								if (!X || X->Kind != EOKSceneObjectKind::Item) AddItemTo(G, Ca, N, false);
								else { PickItem(G, Ca, N); FOKStudioSettings::Get().UseRecent(ItemKey(G, Ca, N)); }
								return FReply::Handled();
							})
							[SNew(STextBlock).Font(Font(10.f)).ColorAndOpacity(bFavorite ? FLinearColor(1.f, 0.85f, 0.3f) : OKStudioStyle::Text()).Text(FText::FromString(I.Name))]
						]
					];
				})
			]
		];
		if (ItemSearch.IsEmpty())
			Props->AddSlot().AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth()[Column(80.f, &GroupOptions, GroupName, [this](int32 V) { return V == BrowseGroup; }, [this](int32 V) { BrowseGroup = V; BrowseCategory = -1; DeferRebuild(); })]
				+ SHorizontalBox::Slot().AutoWidth().Padding(2.f, 0.f)[Column(80.f, &CategoryOptions, CategoryName, [this](int32 V) { return V == BrowseCategory; }, [this](int32 V) { BrowseCategory = V; DeferRebuild(); })]
				+ SHorizontalBox::Slot().AutoWidth()[Items]
			];
		else Props->AddSlot().AutoHeight()[Items];
		Label(FString::Printf(TEXT("%d 個道具；左鍵加入、中鍵加為子物件"), ItemOptions.Num()));
	}

	/** Light catalogue: add a directional, point or spot light. */
	void LightCatalog()
	{
		using namespace OKStudioStyle;
		static const TCHAR* Names[] = { TEXT("方向光"), TEXT("點光源"), TEXT("聚光燈") };
		static const TCHAR* Args[] = { TEXT("directional"), TEXT("point"), TEXT("spot") };
		for (int32 I = 0; I < 3; ++I)
			Props->AddSlot().AutoHeight().Padding(0.f, 2.f)[Button(FText::FromString(Names[I]), Cmd(TEXT("add.light"), Args[I]), 196.f, 22.f)];
		int32 Lights = 0;
		if (Game.IsValid()) for (const FOKSceneObject& O : Game->Document->Objects) Lights += O.Kind == EOKSceneObjectKind::Light ? 1 : 0;
		Label(FString::Printf(TEXT("目前燈光：%d"), Lights));
	}

	/** Camera: the view in use, scene cameras, view presets, the ten bookmarks, screenshots. */
	void CameraPage()
	{
		using namespace OKStudioStyle;
		const FOKSceneObject* Active = Game.IsValid() ? Game->Document->Find(Game->Document->ActiveCamera) : nullptr;
		Label(TEXT("目前視角：") + (Active ? Active->Name : FString(TEXT("自由視角"))));
		auto Buttons = [this](std::initializer_list<TPair<const TCHAR*, const TCHAR*>> List, float Height = 20.f)
		{
			TSharedRef<SHorizontalBox> Box = SNew(SHorizontalBox);
			for (const TPair<const TCHAR*, const TCHAR*>& B : List)
				Box->AddSlot().FillWidth(1.f).Padding(0.f, 0.f, 2.f, 0.f)[SNew(SBox).ToolTipText(FText::FromString(Tip(B.Key, B.Value)))[Button(FText::FromString(B.Key), Cmd(B.Value), 0.f, Height, false, 10.f)]];
			Props->AddSlot().AutoHeight().Padding(0.f, 2.f)[Box];
		};
		Props->AddSlot().AutoHeight().Padding(0.f, 2.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f).Padding(0.f, 0.f, 2.f, 0.f)[Button(FText::FromString(TEXT("啟用 MMD 系統鏡頭")), Cmd(TEXT("camera.mmd")), 0.f, 20.f, TAttribute<bool>::CreateLambda([this] { return Game.IsValid() && Game->Document->ActiveCamera >= 0 && Game->Document->ActiveCamera == Game->FindMmdCameraId(false); }))]
			+ SHorizontalBox::Slot().FillWidth(1.f)[Button(FText::FromString(TEXT("MMD 導演／設定")), Cmd(TEXT("toggle.mmd")), 0.f, 20.f, TAttribute<bool>::CreateLambda([this] { return IsFloatOpen(TEXT("mmd")); }))]
		];
		Props->AddSlot().AutoHeight().Padding(0.f, 2.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f).Padding(0.f, 0.f, 2.f, 0.f)[Button(FText::FromString(TEXT("使用選中相機")), Cmd(TEXT("camera.use")), 0.f, 20.f)]
			+ SHorizontalBox::Slot().FillWidth(1.f)[Button(FText::FromString(TEXT("自由視角")), Cmd(TEXT("camera.free")), 0.f, 20.f, TAttribute<bool>::CreateLambda([this] { return Game.IsValid() && Game->Document->ActiveCamera < 0; }))]
		];
		Props->AddSlot().AutoHeight().Padding(0.f, 2.f)
			[CheckBox(TEXT("鎖定目前相機（防止滑鼠拖曳視角時脫離）"), [this] { return Game.IsValid() && Game->bLockActiveCamera; }, [this](bool B) { if (Game.IsValid()) Game->bLockActiveCamera = B; })];
		Buttons({ { TEXT("從視角建相機"), TEXT("camera.fromview") }, { TEXT("視角存入所選"), TEXT("camera.store") }, { TEXT("下一相機"), TEXT("camera.next") } });
		if (const FOKSceneObject* O = Obj(); O && O->Kind == EOKSceneObjectKind::Camera)
		{
			Float(TEXT("視野角度"), [](FOKSceneObject& X) { return X.Camera.Fov; }, [](FOKSceneObject& X, float V) { X.Camera.Fov = V; }, 5.f, 170.f, 1.f);
			Float(TEXT("近面 cm"), [](FOKSceneObject& X) { return X.Camera.Near; }, [](FOKSceneObject& X, float V) { X.Camera.Near = FMath::Max(V, 0.f); }, 0.f, 500.f, 0.5f);
		}
		Label(TEXT("視角"));
		Buttons({ { TEXT("聚焦所選"), TEXT("camera.focus") }, { TEXT("初始"), TEXT("camera.reset") }, { TEXT("前"), TEXT("camera.front") }, { TEXT("側"), TEXT("camera.side") }, { TEXT("上"), TEXT("camera.top") } });
		Props->AddSlot().AutoHeight().Padding(0.f, 2.f)
			[CheckBox(TEXT("自由飛行（按住右鍵，W A S D Q E 移動）"), [] { return FOKStudioSettings::Get().bFlyMode; }, [this](bool) { RunCommand(TEXT("camera.fly")); })];
		Label(TEXT("視角書籤（Ctrl+數字存、數字讀；存在場景裡）"));
		TSharedRef<SHorizontalBox> Marks = SNew(SHorizontalBox);
		for (int32 N = 0; N < 10; ++N)
			Marks->AddSlot().FillWidth(1.f).Padding(0.f, 0.f, 1.f, 0.f)
			[
				SNew(SBox).ToolTipText(FText::FromString(FString::Printf(TEXT("點：讀視角 %d；右鍵：存目前視角"), N)))
				[
					SNew(SBorder).BorderImage(Brush(FLinearColor::Transparent)).Padding(0.f)
					.OnMouseButtonDown_Lambda([this, N](const FGeometry&, const FPointerEvent& E)
					{
						if (E.GetEffectingButton() != EKeys::RightMouseButton) return FReply::Unhandled();
						RunCommand(FString::Printf(TEXT("view.save.%d"), N));
						return FReply::Handled();
					})
					[Button(FText::FromString(FString::FromInt(N)), Cmd(FString::Printf(TEXT("view.load.%d"), N)), 0.f, 20.f,
						TAttribute<bool>::CreateLambda([this, N] { return Game.IsValid() && Game->Document->Bookmarks.IsValidIndex(N) && Game->Document->Bookmarks[N].bSet; }), 10.f)]
				]
			];
		Props->AddSlot().AutoHeight().Padding(0.f, 2.f)[Marks];
		Label(TEXT("截圖"));
		Props->AddSlot().AutoHeight().Padding(0.f, 2.f)[Button(FText::FromString(Tip(TEXT("截圖…"), TEXT("app.screenshot"))), Cmd(TEXT("app.screenshot")), 0.f, 22.f)];
	}

	/** KK Studio pose files (UserData/Studio/pose): load one, or save the selected character's FK. */
	void PoseFiles()
	{
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
		X->Character.StudioAnimeKind = A.Kind; X->Character.StudioAnimeGroup = A.Group; X->Character.StudioAnimeCategory = A.Category; X->Character.StudioAnimeNo = A.No; X->Character.AnimFile.Reset();
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
		PostFloat(TEXT("光圈 f"), &FOKScenePost::Aperture, 0.3f, 32.f, 0.1f);
		PostCheck(TEXT("暗角"), &FOKScenePost::bVignette);
		PostFloat(TEXT("暗角強度"), &FOKScenePost::SourceVignetteIntensity, 0.f, 0.9f, 0.01f);
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
};
