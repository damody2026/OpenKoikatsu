#include "OKAnimationEditorToolkit.h"
#include "OKAnimEdMode.h"
#include "OKAnimationAssets.h"
#include "OKAnimationEditing.h"
#include "OKAnimationLibrary.h"
#include "KKOkAnimation.h"
#include "AdvancedPreviewScene.h"
#include "Algo/Reverse.h"
#include "AssetEditorModeManager.h"
#include "Animation/AnimSequence.h"
#include "Components/PoseableMeshComponent.h"
#include "CurveEditor.h"
#include "Dom/JsonObject.h"
#include "Editor.h"
#include "EditorModeManager.h"
#include "EditorViewportClient.h"
#include "Engine/SkeletalMesh.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/Notifications/NotificationManager.h"
#include "IDetailsView.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "RichCurveEditorModel.h"
#include "SCurveEditorPanel.h"
#include "SEditorViewport.h"
#include "ScopedTransaction.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Styling/AppStyle.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SListView.h"
#include "KKGameRoot.h"
#include "KKCardWorkspace.h"
#include "KKPreviewActor.h"
#include "LevelSequence.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "AssetRegistry/ARFilter.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Widgets/Input/SEditableTextBox.h"

#define LOCTEXT_NAMESPACE "OKAnimationEditor"

namespace
{
	const FName AppId(TEXT("OKAnimationEditorApp"));
	const FName ViewportTab(TEXT("OKAnimation_Viewport"));
	const FName DetailsTab(TEXT("OKAnimation_Details"));
	const FName PreviewTab(TEXT("OKAnimation_Preview"));
	const FName OutlineTab(TEXT("OKAnimation_Outline"));
	const FName ValidationTab(TEXT("OKAnimation_Validation"));
	const FName ChannelTab(TEXT("OKAnimation_Channels"));
	const FName GraphTab(TEXT("OKAnimation_Graph"));
	const FName DopeTab(TEXT("OKAnimation_DopeSheet"));
	const FName BlendTab(TEXT("OKAnimation_BlendSpace"));
	const FName ContactsTab(TEXT("OKAnimation_Contacts"));
	const FName PosesTab(TEXT("OKAnimation_Poses"));
	const FName AssetsTab(TEXT("OKAnimation_Assets"));
	const FName MontageTab(TEXT("OKAnimation_Montage"));
	const FName FamilyTab(TEXT("OKAnimation_Family"));

	void Notify(const FText& Text, bool bSuccess)
	{
		FNotificationInfo Info(Text);
		Info.ExpireDuration = bSuccess ? 4.f : 8.f;
		if (TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info))
			Item->SetCompletionState(bSuccess ? SNotificationItem::CS_Success : SNotificationItem::CS_Fail);
	}
	const TArray<FTransform> EmptyPose;
}

// ------------------------------------------------------------------------------------------------ viewport

class SOKAnimationViewport : public SEditorViewport
{
public:
	SLATE_BEGIN_ARGS(SOKAnimationViewport) {}
	SLATE_END_ARGS()

	void Construct(const FArguments&, TSharedPtr<FAdvancedPreviewScene> InScene, FOKAnimationEditor* InEditor)
	{
		Scene = InScene;
		Editor = InEditor;
		SEditorViewport::Construct(SEditorViewport::FArguments());
	}
	void Focus(const FBox& Box) { if (Client && Box.IsValid) Client->FocusViewportOnBox(Box, true); }
	FEditorViewportClient* GetClient() const { return Client.Get(); }

protected:
	virtual TSharedRef<FEditorViewportClient> MakeEditorViewportClient() override
	{
		Client = MakeShareable(new FEditorViewportClient(nullptr, Scene.Get(), SharedThis(this)));
		Client->SetRealtime(true);
		Client->SetViewLocation(FVector(250.f, 250.f, 150.f));
		Client->SetViewRotation(FRotator(-10.f, -135.f, 0.f));
		Client->EngineShowFlags.SetGrid(true);
		if (FAssetEditorModeManager* Modes = static_cast<FAssetEditorModeManager*>(Client->GetModeTools())) Modes->SetPreviewScene(Scene.Get());
		Client->GetModeTools()->ActivateMode(FOKAnimEdMode::ModeId);
		if (FOKAnimEdMode* Mode = static_cast<FOKAnimEdMode*>(Client->GetModeTools()->GetActiveMode(FOKAnimEdMode::ModeId))) Mode->Editor = Editor;
		Client->GetModeTools()->SetWidgetMode(UE::Widget::EWidgetMode::WM_Rotate);
		return Client.ToSharedRef();
	}

private:
	TSharedPtr<FAdvancedPreviewScene> Scene;
	TSharedPtr<FEditorViewportClient> Client;
	FOKAnimationEditor* Editor = nullptr;
};

// ------------------------------------------------------------------------------------------------ timeline

/** Frame ruler with the current frame, key diamonds (selected bones, or every keyed bone), scrubbing and key dragging. */
class SOKKeyTimeline : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SOKKeyTimeline) {}
	SLATE_END_ARGS()

	void Construct(const FArguments&, FOKAnimationEditor* InEditor) { Editor = InEditor; }
	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(200.f, 38.f); }
	virtual bool SupportsKeyboardFocus() const override { return true; }

	virtual int32 OnPaint(const FPaintArgs&, const FGeometry& Geo, const FSlateRect& Clip, FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle&, bool) const override
	{
		const FSlateBrush* White = FAppStyle::GetBrush("WhiteBrush");
		const FVector2D Size = Geo.GetLocalSize();
		FSlateDrawElement::MakeBox(Out, Layer, Geo.ToPaintGeometry(), White, ESlateDrawEffect::None, FLinearColor(0.02f, 0.02f, 0.02f));
		const int32 N = FMath::Max(1, Editor->NumFrames());
		const float Step = FrameWidth(Geo);
		const int32 Label = Step > 18.f ? 1 : Step > 6.f ? 5 : Step > 2.f ? 10 : 30;
		const FSlateFontInfo Font = FAppStyle::GetFontStyle("SmallFont");
		for (int32 F = 0; F < N; ++F)
		{
			const float X = FrameX(Geo, F);
			const bool bMajor = F % Label == 0;
			if (!bMajor && Step < 4.f) continue;
			FSlateDrawElement::MakeLines(Out, Layer + 1, Geo.ToPaintGeometry(), { FVector2D(X, 0.f), FVector2D(X, bMajor ? 10.f : 5.f) }, ESlateDrawEffect::None, FLinearColor(0.4f, 0.4f, 0.4f));
			if (bMajor)
				FSlateDrawElement::MakeText(Out, Layer + 1, Geo.ToPaintGeometry(FVector2f(30.f, 12.f), FSlateLayoutTransform(FVector2f(X + 2.f, 0.f))), FString::FromInt(F), Font, ESlateDrawEffect::None, FLinearColor(0.6f, 0.6f, 0.6f));
		}
		TArray<int32> Keys;
		Editor->GetTimelineKeys(Keys);
		for (const int32 F : Keys)
		{
			const bool bSel = Selected.Contains(F);
			const int32 Shown = bSel && bDraggingKeys ? FMath::Clamp(F + DragDelta, 0, N - 1) : F;
			const float X = FrameX(Geo, Shown);
			FSlateDrawElement::MakeBox(Out, Layer + 2, Geo.ToPaintGeometry(FVector2f(8.f, 8.f), FSlateLayoutTransform(FVector2f(X - 4.f, Size.Y * 0.5f + 2.f))), White,
				ESlateDrawEffect::None, bSel ? FLinearColor(1.f, 0.55f, 0.1f) : FLinearColor(0.95f, 0.85f, 0.2f));
		}
		// Quality problems: red bars along the bottom.
		for (const TSharedPtr<UOKAnimSequence::FQualityIssue>& I : Editor->QualityIssues)
		{
			const float X0 = FrameX(Geo, I->Start) - 2.f, X1 = FrameX(Geo, I->End) + 2.f;
			FSlateDrawElement::MakeBox(Out, Layer + 2, Geo.ToPaintGeometry(FVector2f(FMath::Max(3.f, X1 - X0), 4.f), FSlateLayoutTransform(FVector2f(X0, Size.Y - 5.f))), White,
				ESlateDrawEffect::None, FLinearColor(0.9f, 0.1f, 0.1f, 0.85f));
		}
		const float Cx = FrameX(Geo, Editor->CurrentFrame());
		FSlateDrawElement::MakeLines(Out, Layer + 3, Geo.ToPaintGeometry(), { FVector2D(Cx, 0.f), FVector2D(Cx, Size.Y) }, ESlateDrawEffect::None, FLinearColor(1.f, 0.2f, 0.2f), true, 2.f);
		FSlateDrawElement::MakeText(Out, Layer + 3, Geo.ToPaintGeometry(FVector2f(40.f, 12.f), FSlateLayoutTransform(FVector2f(Cx + 3.f, Size.Y - 13.f))), FString::FromInt(Editor->CurrentFrame()), Font,
			ESlateDrawEffect::None, FLinearColor(1.f, 0.4f, 0.4f));
		return Layer + 3;
	}

	virtual FReply OnMouseButtonDown(const FGeometry& Geo, const FPointerEvent& Event) override
	{
		const float X = Geo.AbsoluteToLocal(Event.GetScreenSpacePosition()).X;
		const int32 Hit = KeyAt(Geo, X);
		if (Event.GetEffectingButton() == EKeys::RightMouseButton)
		{
			if (Hit != INDEX_NONE && !Selected.Contains(Hit)) Selected = { Hit };
			OpenMenu(Event);
			return FReply::Handled();
		}
		if (Event.GetEffectingButton() != EKeys::LeftMouseButton) return FReply::Unhandled();
		if (Hit != INDEX_NONE)
		{
			if (Event.IsShiftDown() || Event.IsControlDown()) { if (Selected.Contains(Hit)) Selected.Remove(Hit); else Selected.Add(Hit); }
			else if (!Selected.Contains(Hit)) Selected = { Hit };
			bDraggingKeys = true;
			DragStart = FrameAt(Geo, X);
			DragDelta = 0;
		}
		else
		{
			Selected.Reset();
			bScrubbing = true;
			Editor->SetFrame(FrameAt(Geo, X));
		}
		return FReply::Handled().CaptureMouse(SharedThis(this)).SetUserFocus(SharedThis(this));
	}

	virtual FReply OnMouseMove(const FGeometry& Geo, const FPointerEvent& Event) override
	{
		const float X = Geo.AbsoluteToLocal(Event.GetScreenSpacePosition()).X;
		if (bScrubbing) { Editor->SetFrame(FrameAt(Geo, X)); return FReply::Handled(); }
		if (bDraggingKeys) { DragDelta = FrameAt(Geo, X) - DragStart; return FReply::Handled(); }
		return FReply::Unhandled();
	}

	virtual FReply OnMouseButtonUp(const FGeometry& Geo, const FPointerEvent& Event) override
	{
		if (bDraggingKeys && DragDelta != 0)
		{
			Editor->MoveKeys(Selected, DragDelta);
			for (int32& F : Selected) F = FMath::Clamp(F + DragDelta, 0, Editor->NumFrames() - 1);
		}
		else if (bDraggingKeys && Selected.Num() == 1) Editor->SetFrame(Selected[0]);
		bDraggingKeys = bScrubbing = false;
		DragDelta = 0;
		return FReply::Handled().ReleaseMouseCapture();
	}

	virtual FReply OnKeyDown(const FGeometry&, const FKeyEvent& Event) override
	{
		if (Event.GetKey() == EKeys::Delete && Selected.Num()) { Editor->DeleteKeys(Selected); Selected.Reset(); return FReply::Handled(); }
		return FReply::Unhandled();
	}

private:
	static constexpr float Margin = 8.f;
	float FrameWidth(const FGeometry& Geo) const { return (Geo.GetLocalSize().X - 2.f * Margin) / FMath::Max(1, Editor->NumFrames() - 1); }
	float FrameX(const FGeometry& Geo, int32 F) const { return Margin + F * FrameWidth(Geo); }
	int32 FrameAt(const FGeometry& Geo, float X) const { return FMath::Clamp(FMath::RoundToInt((X - Margin) / FrameWidth(Geo)), 0, FMath::Max(0, Editor->NumFrames() - 1)); }
	int32 KeyAt(const FGeometry& Geo, float X) const
	{
		TArray<int32> Keys;
		Editor->GetTimelineKeys(Keys);
		for (const int32 F : Keys) if (FMath::Abs(FrameX(Geo, F) - X) <= FMath::Max(5.f, FrameWidth(Geo) * 0.4f)) return F;
		return INDEX_NONE;
	}
	void OpenMenu(const FPointerEvent& Event)
	{
		FMenuBuilder Menu(true, nullptr);
		Menu.AddMenuEntry(LOCTEXT("SetKeyHere", "Key Selected Bones Here"), FText(), FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([this] { Editor->KeySelection(true, true); })));
		Menu.AddMenuEntry(LOCTEXT("DeleteKeys", "Delete Keys"), FText(), FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([this] { Editor->DeleteKeys(Selected); Selected.Reset(); }),
			FCanExecuteAction::CreateLambda([this] { return Selected.Num() > 0; })));
		FSlateApplication::Get().PushMenu(SharedThis(this), FWidgetPath(), Menu.MakeWidget(), Event.GetScreenSpacePosition(), FPopupTransitionEffect::ContextMenu);
	}

	FOKAnimationEditor* Editor = nullptr;
	TArray<int32> Selected;
	bool bScrubbing = false, bDraggingKeys = false;
	int32 DragStart = 0, DragDelta = 0;
};

// ------------------------------------------------------------------------------------------------ dope sheet

/** One row per keyed bone: its keys on the frame grid. Click a name to select the bone, click / drag keys of a row. */
class SOKDopeSheet : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SOKDopeSheet) {}
	SLATE_END_ARGS()

	void Construct(const FArguments&, FOKAnimationEditor* InEditor) { Editor = InEditor; }
	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(300.f, FMath::Max(1, Editor->KeyedBones().Num()) * RowHeight + 4.f); }
	virtual bool SupportsKeyboardFocus() const override { return true; }

	virtual int32 OnPaint(const FPaintArgs&, const FGeometry& Geo, const FSlateRect&, FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle&, bool) const override
	{
		const FSlateBrush* White = FAppStyle::GetBrush("WhiteBrush");
		const FSlateFontInfo Font = FAppStyle::GetFontStyle("SmallFont");
		const FVector2D Size = Geo.GetLocalSize();
		FSlateDrawElement::MakeBox(Out, Layer, Geo.ToPaintGeometry(), White, ESlateDrawEffect::None, FLinearColor(0.015f, 0.015f, 0.015f));
		const TArray<FName> Rows = Editor->KeyedBones();
		const TArray<int32>& Sel = Editor->GetSelection();
		for (int32 R = 0; R < Rows.Num(); ++R)
		{
			const float Y = R * RowHeight;
			const bool bSelectedBone = Sel.ContainsByPredicate([&](int32 H) { return Editor->GetBinding().FamilyName.IsValidIndex(H) && Editor->GetBinding().FamilyName[H] == Rows[R]; });
			FSlateDrawElement::MakeBox(Out, Layer + 1, Geo.ToPaintGeometry(FVector2f(Size.X, RowHeight - 1.f), FSlateLayoutTransform(FVector2f(0.f, Y))), White, ESlateDrawEffect::None,
				bSelectedBone ? FLinearColor(0.12f, 0.1f, 0.03f) : (R % 2 ? FLinearColor(0.035f, 0.035f, 0.035f) : FLinearColor(0.05f, 0.05f, 0.05f)));
			FSlateDrawElement::MakeText(Out, Layer + 2, Geo.ToPaintGeometry(FVector2f(NameWidth, RowHeight), FSlateLayoutTransform(FVector2f(4.f, Y + 2.f))), Rows[R].ToString(), Font,
				ESlateDrawEffect::None, bSelectedBone ? FLinearColor(1.f, 0.85f, 0.2f) : FLinearColor(0.75f, 0.75f, 0.75f));
			TArray<int32> Frames;
			Editor->GetBoneKeyFrames(Rows[R], Frames);
			for (const int32 F : Frames)
			{
				const bool bSel = Selected.Contains(TPair<FName, int32>(Rows[R], F));
				const int32 Shown = bSel && bDragging ? FMath::Clamp(F + DragDelta, 0, Editor->NumFrames() - 1) : F;
				FSlateDrawElement::MakeBox(Out, Layer + 3, Geo.ToPaintGeometry(FVector2f(7.f, 7.f), FSlateLayoutTransform(FVector2f(FrameX(Geo, Shown) - 3.5f, Y + RowHeight * 0.5f - 3.5f))), White,
					ESlateDrawEffect::None, bSel ? FLinearColor(1.f, 0.55f, 0.1f) : FLinearColor(0.95f, 0.85f, 0.2f));
			}
		}
		const float Cx = FrameX(Geo, Editor->CurrentFrame());
		FSlateDrawElement::MakeLines(Out, Layer + 4, Geo.ToPaintGeometry(), { FVector2D(Cx, 0.f), FVector2D(Cx, Size.Y) }, ESlateDrawEffect::None, FLinearColor(1.f, 0.2f, 0.2f), true, 1.5f);
		return Layer + 4;
	}

	virtual FReply OnMouseButtonDown(const FGeometry& Geo, const FPointerEvent& Event) override
	{
		const FVector2D P = Geo.AbsoluteToLocal(Event.GetScreenSpacePosition());
		const TArray<FName> Rows = Editor->KeyedBones();
		const int32 R = FMath::FloorToInt(P.Y / RowHeight);
		if (!Rows.IsValidIndex(R)) { Selected.Reset(); return FReply::Handled(); }
		if (P.X < NameWidth) { Editor->SelectBoneByName(Rows[R], Event.IsControlDown() || Event.IsShiftDown()); return FReply::Handled(); }
		const int32 F = FrameAt(Geo, P.X);
		TArray<int32> Frames;
		Editor->GetBoneKeyFrames(Rows[R], Frames);
		const int32* Hit = Frames.FindByPredicate([&](int32 K) { return FMath::Abs(FrameX(Geo, K) - P.X) <= 5.f; });
		if (Event.GetEffectingButton() == EKeys::RightMouseButton)
		{
			if (Hit) { Editor->DeleteKeysOf({ Rows[R] }, { *Hit }); Selected.Remove(TPair<FName, int32>(Rows[R], *Hit)); }
			return FReply::Handled();
		}
		if (!Hit) { Selected.Reset(); Editor->SetFrame(F); return FReply::Handled(); }
		const TPair<FName, int32> Key(Rows[R], *Hit);
		if (Event.IsShiftDown() || Event.IsControlDown()) { if (Selected.Contains(Key)) Selected.Remove(Key); else Selected.Add(Key); }
		else if (!Selected.Contains(Key)) Selected = { Key };
		bDragging = true;
		DragStart = F;
		DragDelta = 0;
		return FReply::Handled().CaptureMouse(SharedThis(this)).SetUserFocus(SharedThis(this));
	}

	virtual FReply OnMouseMove(const FGeometry& Geo, const FPointerEvent& Event) override
	{
		if (!bDragging) return FReply::Unhandled();
		DragDelta = FrameAt(Geo, Geo.AbsoluteToLocal(Event.GetScreenSpacePosition()).X) - DragStart;
		return FReply::Handled();
	}

	virtual FReply OnMouseButtonUp(const FGeometry& Geo, const FPointerEvent& Event) override
	{
		if (bDragging && DragDelta != 0)
		{
			// Each bone moves its own selected keys.
			TMap<FName, TArray<int32>> ByBone;
			for (const auto& K : Selected) ByBone.FindOrAdd(K.Key).Add(K.Value);
			for (const auto& B : ByBone) Editor->MoveKeysOf({ B.Key }, B.Value, DragDelta);
			for (auto& K : Selected) K.Value = FMath::Clamp(K.Value + DragDelta, 0, Editor->NumFrames() - 1);
		}
		else if (bDragging && Selected.Num() == 1) Editor->SetFrame(Selected[0].Value);
		bDragging = false;
		DragDelta = 0;
		return FReply::Handled().ReleaseMouseCapture();
	}

	virtual FReply OnKeyDown(const FGeometry&, const FKeyEvent& Event) override
	{
		if (Event.GetKey() != EKeys::Delete || Selected.IsEmpty()) return FReply::Unhandled();
		TMap<FName, TArray<int32>> ByBone;
		for (const auto& K : Selected) ByBone.FindOrAdd(K.Key).Add(K.Value);
		for (const auto& B : ByBone) Editor->DeleteKeysOf({ B.Key }, B.Value);
		Selected.Reset();
		return FReply::Handled();
	}

private:
	static constexpr float RowHeight = 18.f;
	static constexpr float NameWidth = 150.f;
	float FrameWidth(const FGeometry& Geo) const { return (Geo.GetLocalSize().X - NameWidth - 12.f) / FMath::Max(1, Editor->NumFrames() - 1); }
	float FrameX(const FGeometry& Geo, int32 F) const { return NameWidth + 4.f + F * FrameWidth(Geo); }
	int32 FrameAt(const FGeometry& Geo, float X) const { return FMath::Clamp(FMath::RoundToInt((X - NameWidth - 4.f) / FrameWidth(Geo)), 0, FMath::Max(0, Editor->NumFrames() - 1)); }

	FOKAnimationEditor* Editor = nullptr;
	TArray<TPair<FName, int32>> Selected;
	bool bDragging = false;
	int32 DragStart = 0, DragDelta = 0;
};

// ------------------------------------------------------------------------------------------------ blend space panel

/** Axes, samples and the preview parameter of a blend space: drag to preview, Shift+drag a sample to move it. */

/** One bar per contact of the sequence: drag the ends to change start / end, the middle to move it; Del deletes the selected. */
class SOKContactTrack : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SOKContactTrack) {}
	SLATE_END_ARGS()

	void Construct(const FArguments&, FOKAnimationEditor* InEditor) { Editor = InEditor; }
	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(300.f, FMath::Max(1, Contacts() ? Contacts()->Num() : 0) * RowHeight + 22.f); }
	virtual bool SupportsKeyboardFocus() const override { return true; }

	virtual int32 OnPaint(const FPaintArgs&, const FGeometry& Geo, const FSlateRect&, FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle&, bool) const override
	{
		const FSlateBrush* White = FAppStyle::GetBrush("WhiteBrush");
		const FSlateFontInfo Font = FAppStyle::GetFontStyle("SmallFont");
		const FVector2D Size = Geo.GetLocalSize();
		FSlateDrawElement::MakeBox(Out, Layer, Geo.ToPaintGeometry(), White, ESlateDrawEffect::None, FLinearColor(0.015f, 0.015f, 0.015f));
		const TArray<FOKContact>* C = Contacts();
		if (!C || C->IsEmpty())
		{
			FSlateDrawElement::MakeText(Out, Layer + 1, Geo.ToPaintGeometry(FVector2f(500.f, 14.f), FSlateLayoutTransform(FVector2f(6.f, 4.f))),
				FString(TEXT("No contacts. Select a bone (ground) or two bones (first reaches the second) and press Add Contact.")), Font, ESlateDrawEffect::None, FLinearColor(0.6f, 0.6f, 0.6f));
			return Layer + 1;
		}
		for (int32 I = 0; I < C->Num(); ++I)
		{
			const FOKContact& K = (*C)[I];
			const float Y = 18.f + I * RowHeight;
			const FString Name = FString::Printf(TEXT("%s -> %s"), *K.Effector.ToString(), K.TargetRole == -1 ? TEXT("ground") : *K.TargetBone.ToString());
			FSlateDrawElement::MakeText(Out, Layer + 1, Geo.ToPaintGeometry(FVector2f(NameWidth - 4.f, RowHeight), FSlateLayoutTransform(FVector2f(4.f, Y + 2.f))), Name, Font,
				ESlateDrawEffect::None, I == Editor->SelectedContact ? FLinearColor(1.f, 0.8f, 0.2f) : FLinearColor(0.8f, 0.8f, 0.8f));
			const float X0 = TimeX(Geo, K.Start), X1 = TimeX(Geo, K.End);
			FSlateDrawElement::MakeBox(Out, Layer + 1, Geo.ToPaintGeometry(FVector2f(FMath::Max(2.f, X1 - X0), RowHeight - 4.f), FSlateLayoutTransform(FVector2f(X0, Y + 2.f))), White,
				ESlateDrawEffect::None, I == Editor->SelectedContact ? FLinearColor(0.95f, 0.6f, 0.15f) : K.TargetRole == -1 ? FLinearColor(0.3f, 0.65f, 0.3f) : FLinearColor(0.3f, 0.5f, 0.85f));
		}
		const float Cx = TimeX(Geo, Editor->CurrentFrame() / FMath::Max(Editor->FrameRate(), 1.f));
		FSlateDrawElement::MakeLines(Out, Layer + 2, Geo.ToPaintGeometry(), { FVector2D(Cx, 0.f), FVector2D(Cx, Size.Y) }, ESlateDrawEffect::None, FLinearColor(1.f, 0.2f, 0.2f), true, 2.f);
		return Layer + 2;
	}

	virtual FReply OnMouseButtonDown(const FGeometry& Geo, const FPointerEvent& E) override
	{
		const TArray<FOKContact>* C = Contacts();
		const FVector2D P = Geo.AbsoluteToLocal(E.GetScreenSpacePosition());
		const int32 Row = FMath::FloorToInt((P.Y - 18.f) / RowHeight);
		if (!C || !C->IsValidIndex(Row) || P.X < NameWidth) { Editor->SelectedContact = C && C->IsValidIndex(Row) ? Row : INDEX_NONE; return FReply::Handled().SetUserFocus(SharedThis(this)); }
		Editor->SelectedContact = Row;
		const FOKContact& K = (*C)[Row];
		const float X0 = TimeX(Geo, K.Start), X1 = TimeX(Geo, K.End);
		Drag = FMath::Abs(P.X - X0) < 6.f ? 1 : FMath::Abs(P.X - X1) < 6.f ? 2 : (P.X > X0 && P.X < X1) ? 3 : 0;
		DragStartX = P.X;
		Original = FVector2D(K.Start, K.End);
		if (Drag) Editor->BeginEdit(NSLOCTEXT("OKAnimationEditor", "ContactDrag", "Move contact"));
		return FReply::Handled().CaptureMouse(SharedThis(this)).SetUserFocus(SharedThis(this));
	}

	virtual FReply OnMouseMove(const FGeometry& Geo, const FPointerEvent& E) override
	{
		if (!Drag || !HasMouseCapture()) return FReply::Unhandled();
		const float Dt = (Geo.AbsoluteToLocal(E.GetScreenSpacePosition()).X - DragStartX) / FMath::Max(1.f, PixelsPerSecond(Geo));
		const float Fps = FMath::Max(Editor->FrameRate(), 1.f);
		auto Snap = [Fps](float T) { return FMath::RoundToFloat(T * Fps) / Fps; };
		float S = Original.X, En = Original.Y;
		if (Drag == 1) S = FMath::Min(Snap(Original.X + Dt), En - 1.f / Fps);
		else if (Drag == 2) En = FMath::Max(Snap(Original.Y + Dt), S + 1.f / Fps);
		else { S = Snap(Original.X + Dt); En = S + (Original.Y - Original.X); }
		Editor->SetContactRange(Editor->SelectedContact, S, En);
		return FReply::Handled();
	}

	virtual FReply OnMouseButtonUp(const FGeometry&, const FPointerEvent&) override
	{
		if (Drag) { Editor->CommitContacts(); Editor->EndEdit(); }
		Drag = 0;
		return FReply::Handled().ReleaseMouseCapture();
	}

	virtual FReply OnKeyDown(const FGeometry&, const FKeyEvent& E) override
	{
		if (E.GetKey() == EKeys::Delete && Editor->SelectedContact != INDEX_NONE) { Editor->DeleteContact(Editor->SelectedContact); return FReply::Handled(); }
		return FReply::Unhandled();
	}

private:
	const TArray<FOKContact>* Contacts() const { const UOKAnimSequence* S = Editor->Sequence(); return S ? &S->Contacts : nullptr; }
	float Length() const { const UOKAnimSequence* S = Editor->Sequence(); return S ? FMath::Max(S->Duration, 1.e-3f) : 1.f; }
	float PixelsPerSecond(const FGeometry& Geo) const { return (Geo.GetLocalSize().X - NameWidth - 12.f) / Length(); }
	float TimeX(const FGeometry& Geo, float T) const { return NameWidth + 4.f + T * PixelsPerSecond(Geo); }
	static constexpr float RowHeight = 18.f, NameWidth = 220.f;
	FOKAnimationEditor* Editor = nullptr;
	int32 Drag = 0;
	float DragStartX = 0.f;
	FVector2D Original;
};


/** The sections of an OK montage on its timeline: drag a bar to move it, its right end to change its length, the
 *  shaded fade at its left end to change the blend-in; the arrow text shows the Next branch. */
class SOKMontageTrack : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SOKMontageTrack) {}
	SLATE_END_ARGS()

	void Construct(const FArguments&, UOKAnimMontage* InMontage, TFunction<void()> InChanged) { Montage = InMontage; Changed = InChanged; }
	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(300.f, FMath::Max(1, Montage.IsValid() ? Montage->Sections.Num() : 0) * RowHeight + 8.f); }

	int32 Selected = INDEX_NONE;

	virtual int32 OnPaint(const FPaintArgs&, const FGeometry& Geo, const FSlateRect&, FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle&, bool) const override
	{
		const FSlateBrush* White = FAppStyle::GetBrush("WhiteBrush");
		const FSlateFontInfo Font = FAppStyle::GetFontStyle("SmallFont");
		FSlateDrawElement::MakeBox(Out, Layer, Geo.ToPaintGeometry(), White, ESlateDrawEffect::None, FLinearColor(0.015f, 0.015f, 0.015f));
		if (!Montage.IsValid()) return Layer;
		const TArray<TPair<float, float>> Ranges = Montage->GetSectionRanges();
		for (int32 I = 0; I < Montage->Sections.Num() && I < Ranges.Num(); ++I)
		{
			const FOKMontageSection& S = Montage->Sections[I];
			const float Y = 4.f + I * RowHeight;
			const FString Label = FString::Printf(TEXT("%s%s"), *S.Name.ToString(), S.Next.IsNone() ? TEXT("") : *FString::Printf(TEXT(" -> %s"), *S.Next.ToString()));
			FSlateDrawElement::MakeText(Out, Layer + 1, Geo.ToPaintGeometry(FVector2f(NameWidth - 4.f, RowHeight), FSlateLayoutTransform(FVector2f(4.f, Y + 3.f))), Label, Font,
				ESlateDrawEffect::None, I == Selected ? FLinearColor(1.f, 0.8f, 0.2f) : FLinearColor(0.85f, 0.85f, 0.85f));
			const float X0 = TimeX(Geo, Ranges[I].Key), X1 = TimeX(Geo, Ranges[I].Value);
			FSlateDrawElement::MakeBox(Out, Layer + 1, Geo.ToPaintGeometry(FVector2f(FMath::Max(2.f, X1 - X0), RowHeight - 6.f), FSlateLayoutTransform(FVector2f(X0, Y + 3.f))), White,
				ESlateDrawEffect::None, I == Selected ? FLinearColor(0.85f, 0.55f, 0.15f) : FLinearColor(0.35f, 0.45f, 0.75f));
			const float Bx = FMath::Min(X1, TimeX(Geo, Ranges[I].Key + S.BlendIn));
			FSlateDrawElement::MakeBox(Out, Layer + 2, Geo.ToPaintGeometry(FVector2f(FMath::Max(1.f, Bx - X0), RowHeight - 6.f), FSlateLayoutTransform(FVector2f(X0, Y + 3.f))), White,
				ESlateDrawEffect::None, FLinearColor(1.f, 1.f, 1.f, 0.25f));
		}
		return Layer + 2;
	}

	virtual FReply OnMouseButtonDown(const FGeometry& Geo, const FPointerEvent& E) override
	{
		if (!Montage.IsValid()) return FReply::Unhandled();
		const FVector2D P = Geo.AbsoluteToLocal(E.GetScreenSpacePosition());
		const int32 Row = FMath::FloorToInt((P.Y - 4.f) / RowHeight);
		const TArray<TPair<float, float>> Ranges = Montage->GetSectionRanges();
		Selected = Ranges.IsValidIndex(Row) ? Row : INDEX_NONE;
		if (Selected == INDEX_NONE || P.X < NameWidth) return FReply::Handled();
		const float X0 = TimeX(Geo, Ranges[Row].Key), X1 = TimeX(Geo, Ranges[Row].Value), Xb = TimeX(Geo, Ranges[Row].Key + Montage->Sections[Row].BlendIn);
		Drag = FMath::Abs(P.X - X1) < 6.f ? 2 : FMath::Abs(P.X - Xb) < 6.f ? 3 : (P.X > X0 && P.X < X1) ? 1 : 0;
		DragStartX = P.X;
		Original = FVector(Ranges[Row].Key, Ranges[Row].Value - Ranges[Row].Key, Montage->Sections[Row].BlendIn);
		if (Drag) GEditor->BeginTransaction(NSLOCTEXT("OKAnimationEditor", "MontageDrag", "Edit montage section"));
		return FReply::Handled().CaptureMouse(SharedThis(this));
	}

	virtual FReply OnMouseMove(const FGeometry& Geo, const FPointerEvent& E) override
	{
		if (!Drag || !HasMouseCapture() || !Montage.IsValid()) return FReply::Unhandled();
		const float Dt = (Geo.AbsoluteToLocal(E.GetScreenSpacePosition()).X - DragStartX) / FMath::Max(1.f, PixelsPerSecond(Geo));
		auto Snap = [](float T) { return FMath::RoundToFloat(T * 30.f) / 30.f; };
		if (Drag == 1) OKAnimationEditing::SetMontageSectionTiming(Montage.Get(), Selected, Snap(Original.X + Dt), Original.Y);
		else if (Drag == 2) OKAnimationEditing::SetMontageSectionTiming(Montage.Get(), Selected, Original.X, FMath::Max(1.f / 30.f, Snap(Original.Y + Dt)));
		else OKAnimationEditing::SetMontageSectionBlend(Montage.Get(), Selected, FMath::Clamp(Snap(Original.Z + Dt), 0.f, Original.Y));
		if (Changed) Changed();
		return FReply::Handled();
	}

	virtual FReply OnMouseButtonUp(const FGeometry&, const FPointerEvent&) override
	{
		if (Drag) GEditor->EndTransaction();
		Drag = 0;
		return FReply::Handled().ReleaseMouseCapture();
	}

private:
	float Length() const
	{
		float L = 1.f;
		if (Montage.IsValid()) for (const TPair<float, float>& R : Montage->GetSectionRanges()) L = FMath::Max(L, R.Value);
		return L * 1.15f;
	}
	float PixelsPerSecond(const FGeometry& Geo) const { return (Geo.GetLocalSize().X - NameWidth - 12.f) / Length(); }
	float TimeX(const FGeometry& Geo, float T) const { return NameWidth + 4.f + T * PixelsPerSecond(Geo); }
	static constexpr float RowHeight = 22.f, NameWidth = 180.f;
	TWeakObjectPtr<UOKAnimMontage> Montage;
	TFunction<void()> Changed;
	int32 Drag = 0;
	float DragStartX = 0.f;
	FVector Original;
};

class SOKBlendSpacePanel : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SOKBlendSpacePanel) {}
	SLATE_END_ARGS()

	void Construct(const FArguments&, FOKAnimationEditor* InEditor) { Editor = InEditor; }
	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(300.f, 260.f); }

	virtual int32 OnPaint(const FPaintArgs&, const FGeometry& Geo, const FSlateRect&, FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle&, bool) const override
	{
		const FSlateBrush* White = FAppStyle::GetBrush("WhiteBrush");
		const FSlateFontInfo Font = FAppStyle::GetFontStyle("SmallFont");
		const UOKBlendSpace* Blend = Editor->BlendSpace();
		FSlateDrawElement::MakeBox(Out, Layer, Geo.ToPaintGeometry(), White, ESlateDrawEffect::None, FLinearColor(0.02f, 0.02f, 0.02f));
		if (!Blend || Blend->Axes.IsEmpty()) return Layer;
		const FVector2D Size = Geo.GetLocalSize();
		const FVector2f Min(Margin, Margin), Max(Size.X - Margin, Size.Y - Margin);
		FSlateDrawElement::MakeLines(Out, Layer + 1, Geo.ToPaintGeometry(),
			{ FVector2D(Min.X, Min.Y), FVector2D(Max.X, Min.Y), FVector2D(Max.X, Max.Y), FVector2D(Min.X, Max.Y), FVector2D(Min.X, Min.Y) }, ESlateDrawEffect::None, FLinearColor(0.35f, 0.35f, 0.35f));
		for (int32 G = 1; G < 4; ++G)
		{
			const float X = FMath::Lerp(Min.X, Max.X, G / 4.f), Y = FMath::Lerp(Min.Y, Max.Y, G / 4.f);
			FSlateDrawElement::MakeLines(Out, Layer + 1, Geo.ToPaintGeometry(), { FVector2D(X, Min.Y), FVector2D(X, Max.Y) }, ESlateDrawEffect::None, FLinearColor(0.12f, 0.12f, 0.12f));
			if (Blend->Axes.Num() > 1) FSlateDrawElement::MakeLines(Out, Layer + 1, Geo.ToPaintGeometry(), { FVector2D(Min.X, Y), FVector2D(Max.X, Y) }, ESlateDrawEffect::None, FLinearColor(0.12f, 0.12f, 0.12f));
		}
		const FOKBlendAxis& AX = Blend->Axes[0];
		Label(Geo, Out, Layer, FVector2f(Min.X, Max.Y + 2.f), FString::Printf(TEXT("%s %.2f"), *AX.Name.ToString(), AX.Min), Font);
		Label(Geo, Out, Layer, FVector2f(Max.X - 60.f, Max.Y + 2.f), FString::Printf(TEXT("%.2f"), AX.Max), Font);
		if (Blend->Axes.Num() > 1)
		{
			Label(Geo, Out, Layer, FVector2f(2.f, Min.Y - 14.f), FString::Printf(TEXT("%s %.2f"), *Blend->Axes[1].Name.ToString(), Blend->Axes[1].Max), Font);
			Label(Geo, Out, Layer, FVector2f(2.f, Max.Y - 12.f), FString::Printf(TEXT("%.2f"), Blend->Axes[1].Min), Font);
		}
		for (int32 I = 0; I < Blend->Samples.Num(); ++I)
		{
			const FVector2f P = ToScreen(Geo, Blend->Samples[I].Position);
			FSlateDrawElement::MakeBox(Out, Layer + 2, Geo.ToPaintGeometry(FVector2f(10.f, 10.f), FSlateLayoutTransform(P - FVector2f(5.f, 5.f))), White, ESlateDrawEffect::None,
				I == Dragging ? FLinearColor(1.f, 0.55f, 0.1f) : FLinearColor(0.9f, 0.9f, 0.9f));
			Label(Geo, Out, Layer + 2, P + FVector2f(7.f, -6.f), Blend->Samples[I].Animation ? Blend->Samples[I].Animation->GetName() : FString(TEXT("(none)")), Font);
		}
		const FVector2f C = ToScreen(Geo, Editor->GetBlendParams());
		FSlateDrawElement::MakeLines(Out, Layer + 3, Geo.ToPaintGeometry(), { FVector2D(C.X - 8.f, C.Y), FVector2D(C.X + 8.f, C.Y) }, ESlateDrawEffect::None, FLinearColor(0.2f, 1.f, 0.4f), true, 2.f);
		FSlateDrawElement::MakeLines(Out, Layer + 3, Geo.ToPaintGeometry(), { FVector2D(C.X, C.Y - 8.f), FVector2D(C.X, C.Y + 8.f) }, ESlateDrawEffect::None, FLinearColor(0.2f, 1.f, 0.4f), true, 2.f);
		return Layer + 3;
	}

	virtual FReply OnMouseButtonDown(const FGeometry& Geo, const FPointerEvent& Event) override
	{
		const UOKBlendSpace* Blend = Editor->BlendSpace();
		if (!Blend || Event.GetEffectingButton() != EKeys::LeftMouseButton) return FReply::Unhandled();
		const FVector2f P = FVector2f(Geo.AbsoluteToLocal(Event.GetScreenSpacePosition()));
		Dragging = INDEX_NONE;
		if (Event.IsShiftDown())
			for (int32 I = 0; I < Blend->Samples.Num(); ++I)
				if ((ToScreen(Geo, Blend->Samples[I].Position) - P).Size() < 8.f) { Dragging = I; break; }
		bDraggingParam = Dragging == INDEX_NONE;
		Apply(Geo, P, false);
		return FReply::Handled().CaptureMouse(SharedThis(this));
	}
	virtual FReply OnMouseMove(const FGeometry& Geo, const FPointerEvent& Event) override
	{
		if (!bDraggingParam && Dragging == INDEX_NONE) return FReply::Unhandled();
		Apply(Geo, FVector2f(Geo.AbsoluteToLocal(Event.GetScreenSpacePosition())), false);
		return FReply::Handled();
	}
	virtual FReply OnMouseButtonUp(const FGeometry& Geo, const FPointerEvent& Event) override
	{
		if (Dragging != INDEX_NONE) Apply(Geo, FVector2f(Geo.AbsoluteToLocal(Event.GetScreenSpacePosition())), true);
		Dragging = INDEX_NONE;
		bDraggingParam = false;
		return FReply::Handled().ReleaseMouseCapture();
	}

private:
	static constexpr float Margin = 24.f;
	void Label(const FGeometry& Geo, FSlateWindowElementList& Out, int32 Layer, FVector2f At, const FString& Text, const FSlateFontInfo& Font) const
	{
		FSlateDrawElement::MakeText(Out, Layer + 1, Geo.ToPaintGeometry(FVector2f(200.f, 14.f), FSlateLayoutTransform(At)), Text, Font, ESlateDrawEffect::None, FLinearColor(0.7f, 0.7f, 0.7f));
	}
	FVector2f ToScreen(const FGeometry& Geo, const FVector2D& V) const
	{
		const UOKBlendSpace* Blend = Editor->BlendSpace();
		const FVector2D Size = Geo.GetLocalSize();
		auto Norm = [](double X, const FOKBlendAxis& A) { return A.Max > A.Min ? (X - A.Min) / (A.Max - A.Min) : 0.5; };
		const double U = Norm(V.X, Blend->Axes[0]);
		const double W = Blend->Axes.Num() > 1 ? Norm(V.Y, Blend->Axes[1]) : 0.5;
		return FVector2f(Margin + U * (Size.X - 2.f * Margin), Size.Y - Margin - W * (Size.Y - 2.f * Margin));
	}
	FVector2D FromScreen(const FGeometry& Geo, const FVector2f& P) const
	{
		const UOKBlendSpace* Blend = Editor->BlendSpace();
		const FVector2D Size = Geo.GetLocalSize();
		const double U = FMath::Clamp((P.X - Margin) / (Size.X - 2.f * Margin), 0.0, 1.0);
		const double W = FMath::Clamp((Size.Y - Margin - P.Y) / (Size.Y - 2.f * Margin), 0.0, 1.0);
		const FOKBlendAxis& A = Blend->Axes[0];
		const double X = FMath::Lerp(double(A.Min), double(A.Max), U);
		const double Y = Blend->Axes.Num() > 1 ? FMath::Lerp(double(Blend->Axes[1].Min), double(Blend->Axes[1].Max), W) : 0.0;
		return FVector2D(X, Y);
	}
	void Apply(const FGeometry& Geo, const FVector2f& P, bool bFinal)
	{
		const FVector2D V = FromScreen(Geo, P);
		if (Dragging != INDEX_NONE) Editor->MoveBlendSample(Dragging, V, bFinal);
		else Editor->SetBlendParams(V);
	}

	FOKAnimationEditor* Editor = nullptr;
	int32 Dragging = INDEX_NONE;
	bool bDraggingParam = false;
};

// ------------------------------------------------------------------------------------------------ setup

FOKAnimationEditor* FOKAnimationEditor::LastOpened = nullptr;

FText FOKAnimationEditor::GetBaseToolkitName() const { return LOCTEXT("ToolkitName", "OK Animation Editor"); }

UOKAnimSequence* FOKAnimationEditor::Sequence() const
{
	if (UOKAnimSequence* Seq = Cast<UOKAnimSequence>(Asset)) return Seq;
	// A group: the role slot being edited.
	if (const UOKGroupAnimSequence* Group = Cast<UOKGroupAnimSequence>(Asset))
		if (Settings && Group->Roles.IsValidIndex(Settings->Role)) return Cast<UOKAnimSequence>(Group->Roles[Settings->Role].Animation);
	return nullptr;
}

FTransform FOKAnimationEditor::RolePlacement(int32 Role) const
{
	const UOKGroupAnimSequence* Group = Cast<UOKGroupAnimSequence>(Asset);
	auto Placement = [&](int32 R)
	{
		FTransform T = FTransform::Identity;
		if (Group && Group->Roles.IsValidIndex(R))
		{
			const FOKGroupRole& G = Group->Roles[R];
			T.SetRotation(FOKUnrealSkeleton::FromOk(FQuat(FVector(0., 1., 0.), FMath::DegreesToRadians(G.Yaw))));
			T.SetLocation(FOKUnrealSkeleton::FromOkPosition(G.Offset * Binding.HipsLength));
		}
		const float Height = Settings && Settings->RoleHeight.IsValidIndex(R) ? Settings->RoleHeight[R] : 1.f;
		T.SetScale3D(FVector(FMath::Clamp(Height, 0.5f, 1.5f)));
		return T;
	};
	// Relative to the edited role: its mesh stays at the origin at scale 1 (the animate tools work in its component
	// space), so the others take the height ratio.
	const FTransform Edited = Placement(Settings ? Settings->Role : 0);
	FTransform T = Placement(Role) * FTransform(Edited.GetRotation(), Edited.GetLocation()).Inverse();
	T.SetScale3D(Placement(Role).GetScale3D() / Edited.GetScale3D());
	return T;
}

bool FOKAnimationEditor::RoleBonePositions(int32 Role, TArray<FVector>& Out) const
{
	Out.Reset();
	if (!HasPreviewPose()) return false;
	const UPoseableMeshComponent* Mesh = nullptr;
	if (Role == Settings->Role) Mesh = PreviewMesh;
	else for (int32 I = 0, R = 0; I < RoleMeshes.Num(); ++R) { if (R == Settings->Role) continue; if (R == Role) { Mesh = RoleMeshes[I]; break; } ++I; }
	if (!Mesh || Mesh->GetComponentSpaceTransforms().Num() != Binding.Ref.GetNum()) return false;
	const FTransform& T = Mesh->GetComponentTransform();
	for (const int32 B : Binding.BoneIndex) Out.Add(T.TransformPosition(Mesh->GetComponentSpaceTransforms()[B].GetLocation()));
	return true;
}

void FOKAnimationEditor::SetEditedRole(int32 Role)
{
	if (!Settings) return;
	Settings->Role = FMath::Max(0, Role);
	Selection.Reset();
	bDirty = true;
	Rebuild();
}

void FOKAnimationEditor::InitEditor(EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost>& Host, UOKAnimationAsset* InAsset)
{
	Asset = InAsset;
	Settings = NewObject<UOKAnimationPreviewSettings>(GetTransientPackage());
	const UOKSkeletonFamily* Family = UOKAnimationLibrary::GetDefaultFamily();
	if (const UOKAnimSequence* Seq = Cast<UOKAnimSequence>(Asset))
	{
		Settings->Profile = Seq->EditProfile;
		if (Seq->EditProfile && Seq->EditProfile->Family) Family = Seq->EditProfile->Family;
		if (UAnimSequence* Proxy = Seq->EditProxy.LoadSynchronous()) if (Proxy->GetSkeleton()) Settings->Mesh = Proxy->GetSkeleton()->GetPreviewMesh(true);
	}
	if (!Settings->Mesh && Family) Settings->Mesh = Family->PreviewMesh.LoadSynchronous();

	Scene = MakeShared<FAdvancedPreviewScene>(FPreviewScene::ConstructionValues());
	PreviewMesh = NewObject<UPoseableMeshComponent>(GetTransientPackage());
	Scene->AddComponent(PreviewMesh, FTransform::Identity);

	FPropertyEditorModule& PropertyEditor = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	FDetailsViewArgs Args;
	Args.bHideSelectionTip = true;
	Args.NameAreaSettings = FDetailsViewArgs::HideNameArea;
	AssetDetails = PropertyEditor.CreateDetailView(Args);
	AssetDetails->SetObject(Asset);
	SettingsDetails = PropertyEditor.CreateDetailView(Args);
	SettingsDetails->SetObject(Settings);

	CurveEditor = MakeShared<FCurveEditor>();
	CurveEditor->InitCurveEditor(FCurveEditorInitParams());
	CurveEditor->GridLineLabelFormatXAttribute = LOCTEXT("GridX", "{0}s");

	ObjectChanged = FCoreUObjectDelegates::OnObjectPropertyChanged.AddLambda([this](UObject* Object, FPropertyChangedEvent&)
	{
		if (Object == Asset || Object == Settings || Cast<UOKAnimationAsset>(Object) || Cast<UOKRetargetProfile>(Object) || Cast<UOKSkeletonFamily>(Object)) bDirty = true;
	});

	const TSharedRef<FTabManager::FLayout> Layout = FTabManager::NewLayout("OKAnimationEditor_Layout_v8")
		->AddArea(FTabManager::NewPrimaryArea()->SetOrientation(Orient_Vertical)
			->Split(FTabManager::NewSplitter()->SetOrientation(Orient_Horizontal)->SetSizeCoefficient(0.7f)
				->Split(FTabManager::NewStack()->SetSizeCoefficient(0.16f)->AddTab(OutlineTab, ETabState::OpenedTab)->AddTab(AssetsTab, ETabState::OpenedTab)->AddTab(FamilyTab, ETabState::OpenedTab))
				->Split(FTabManager::NewStack()->SetSizeCoefficient(0.56f)->SetHideTabWell(true)->AddTab(ViewportTab, ETabState::OpenedTab))
				->Split(FTabManager::NewSplitter()->SetOrientation(Orient_Vertical)->SetSizeCoefficient(0.28f)
					->Split(FTabManager::NewStack()->SetSizeCoefficient(0.35f)->AddTab(ChannelTab, ETabState::OpenedTab))
					->Split(FTabManager::NewStack()->SetSizeCoefficient(0.65f)->AddTab(DetailsTab, ETabState::OpenedTab)->AddTab(PreviewTab, ETabState::OpenedTab)->AddTab(ValidationTab, ETabState::OpenedTab))))
			->Split(FTabManager::NewStack()->SetSizeCoefficient(0.3f)->AddTab(GraphTab, ETabState::OpenedTab)->AddTab(DopeTab, ETabState::OpenedTab)->AddTab(ContactsTab, ETabState::OpenedTab)->AddTab(PosesTab, ETabState::OpenedTab)->AddTab(MontageTab, ETabState::OpenedTab)->AddTab(BlendTab, ETabState::OpenedTab)));
	InitAssetEditor(Mode, Host, AppId, Layout, true, true, Asset);

	TSharedPtr<FExtender> Extender = MakeShared<FExtender>();
	Extender->AddToolBarExtension("Asset", EExtensionHook::After, GetToolkitCommands(), FToolBarExtensionDelegate::CreateRaw(this, &FOKAnimationEditor::FillToolbar));
	AddToolbarExtender(Extender);
	RegenerateMenusAndToolbars();
	if (UOKAnimSequence* Seq = Sequence()) { Seq->SetFlags(RF_Transactional); LastSignature = LayerSignature(); }
	RunValidation();
	LastOpened = this;
}

FOKAnimationEditor::~FOKAnimationEditor()
{
	FCoreUObjectDelegates::OnObjectPropertyChanged.Remove(ObjectChanged);
	if (LastOpened == this) LastOpened = nullptr;
	if (bEditing && GEditor) GEditor->EndTransaction();
	if (Viewport && Viewport->GetClient())
		if (FOKAnimEdMode* Mode = static_cast<FOKAnimEdMode*>(Viewport->GetClient()->GetModeTools()->GetActiveMode(FOKAnimEdMode::ModeId))) Mode->Editor = nullptr;
}

void FOKAnimationEditor::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(Asset);
	Collector.AddReferencedObject(Settings);
	Collector.AddReferencedObject(PreviewMesh);
	Collector.AddReferencedObjects(RoleMeshes);
	Collector.AddReferencedObject(SourceMesh);
	Collector.AddReferencedObject(RetargetResult);
}

void FOKAnimationEditor::PostUndo(bool)
{
	// The asset rebakes itself (PostEditUndo); curves point into the restored key layer.
	Pending.Reset();
	RefreshCurves();
	RefreshPlayer();
	LastSignature = LayerSignature();
}

float FOKAnimationEditor::Duration() const { return Player ? Player->Duration() : 0.f; }

// ------------------------------------------------------------------------------------------------ tabs

void FOKAnimationEditor::RegisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
	WorkspaceMenuCategory = InTabManager->AddLocalWorkspaceMenuCategory(LOCTEXT("Workspace", "OK Animation Editor"));
	FAssetEditorToolkit::RegisterTabSpawners(InTabManager);

	InTabManager->RegisterTabSpawner(ViewportTab, FOnSpawnTab::CreateLambda([this](const FSpawnTabArgs&)
	{
		Viewport = SNew(SOKAnimationViewport, Scene, this);
		return SNew(SDockTab)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().FillHeight(1.f)[Viewport.ToSharedRef()]
			+ SVerticalBox::Slot().AutoHeight().Padding(4.f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 2, 0)
				[
					SNew(SButton).Text(LOCTEXT("PrevKey", "|<")).ToolTipText(LOCTEXT("PrevKeyTip", "Previous key ( , )")).OnClicked_Lambda([this] { JumpKey(-1); return FReply::Handled(); })
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 2, 0)
				[
					SNew(SButton).Text_Lambda([this] { return bPlaying ? LOCTEXT("Pause", "Pause") : LOCTEXT("Play", "Play"); })
					.ToolTipText(LOCTEXT("PlayTip", "Play / pause (Alt+V)")).OnClicked_Lambda([this] { TogglePlay(); return FReply::Handled(); })
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)
				[
					SNew(SButton).Text(LOCTEXT("NextKey", ">|")).ToolTipText(LOCTEXT("NextKeyTip", "Next key ( . )")).OnClicked_Lambda([this] { JumpKey(1); return FReply::Handled(); })
				]
				+ SHorizontalBox::Slot().FillWidth(1.f)[SNew(SOKKeyTimeline, this)]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6, 0, 2, 0)
				[
					SNew(STextBlock).Text(LOCTEXT("LengthLabel", "Frames"))
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(SBox).WidthOverride(60.f)
					[
						SNew(SSpinBox<int32>).MinValue(2).MaxValue(100000).Delta(1)
						.IsEnabled_Lambda([this] { return Sequence() != nullptr; })
						.Value_Lambda([this] { return NumFrames(); })
						.OnValueCommitted_Lambda([this](int32 V, ETextCommit::Type) { SetLength(V); })
					]
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6, 0, 0, 0)
				[
					SNew(SCheckBox).ToolTipText(LOCTEXT("GhostTip", "Ghosts: the skeleton a few frames before (blue) and after (green)."))
					.IsChecked_Lambda([this] { return bShowGhosts ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
					.OnCheckStateChanged_Lambda([this](ECheckBoxState S) { bShowGhosts = S == ECheckBoxState::Checked; })
					[SNew(STextBlock).Text(LOCTEXT("Ghosts", "Ghosts"))]
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4, 0, 0, 0)
				[
					SNew(SCheckBox).ToolTipText(LOCTEXT("TrailTip", "Motion trail of the selected bone over the whole animation."))
					.IsChecked_Lambda([this] { return bShowTrail ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
					.OnCheckStateChanged_Lambda([this](ECheckBoxState S) { bShowTrail = S == ECheckBoxState::Checked; })
					[SNew(STextBlock).Text(LOCTEXT("Trail", "Trail"))]
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6, 0, 0, 0)
				[
					SNew(STextBlock).Text_Lambda([this] { return FText::FromString(FString::Printf(TEXT("%d / %d  %.2fs  %s"), CurrentFrame(), FMath::Max(0, NumFrames() - 1), Time, *Status)); })
				]
			]
		];
	})).SetDisplayName(LOCTEXT("ViewportTab", "Viewport")).SetGroup(WorkspaceMenuCategory.ToSharedRef());

	InTabManager->RegisterTabSpawner(DetailsTab, FOnSpawnTab::CreateLambda([this](const FSpawnTabArgs&) { return SNew(SDockTab)[AssetDetails.ToSharedRef()]; }))
		.SetDisplayName(LOCTEXT("DetailsTab", "Details")).SetGroup(WorkspaceMenuCategory.ToSharedRef());
	InTabManager->RegisterTabSpawner(PreviewTab, FOnSpawnTab::CreateLambda([this](const FSpawnTabArgs&) { return SNew(SDockTab)[SettingsDetails.ToSharedRef()]; }))
		.SetDisplayName(LOCTEXT("PreviewTab", "Preview Settings")).SetGroup(WorkspaceMenuCategory.ToSharedRef());

	InTabManager->RegisterTabSpawner(OutlineTab, FOnSpawnTab::CreateLambda([this](const FSpawnTabArgs&)
	{
		RefreshOutline();
		return SNew(SDockTab)
		[
			SAssignNew(OutlineView, SListView<TSharedPtr<FString>>)
			.ListItemsSource(&Outline)
			.SelectionMode(ESelectionMode::Single)
			.OnGenerateRow_Lambda([](TSharedPtr<FString> Item, const TSharedRef<STableViewBase>& Owner)
			{
				return SNew(STableRow<TSharedPtr<FString>>, Owner)[SNew(STextBlock).Text(FText::FromString(*Item))];
			})
			.OnSelectionChanged_Lambda([this](TSharedPtr<FString> Item, ESelectInfo::Type How)
			{
				if (!Item || How == ESelectInfo::Direct) return;
				const int32 H = Binding.FamilyName.IndexOfByKey(FName(*Item->TrimStartAndEnd()));
				if (H != INDEX_NONE) SelectBone(H, false);
			})
		];
	})).SetDisplayName(LOCTEXT("OutlineTab", "Bones / Spec")).SetGroup(WorkspaceMenuCategory.ToSharedRef());

	InTabManager->RegisterTabSpawner(ValidationTab, FOnSpawnTab::CreateLambda([this](const FSpawnTabArgs&)
	{
		return SNew(SDockTab)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(4.f)[SNew(SButton).Text(LOCTEXT("Validate", "Validate")).OnClicked_Lambda([this] { RunValidation(); return FReply::Handled(); })]
			+ SVerticalBox::Slot().AutoHeight().MaxHeight(160.f)[SNew(SMultiLineEditableTextBox).IsReadOnly(true).Text_Lambda([this] { return FText::FromString(ValidationText); })]
			+ SVerticalBox::Slot().AutoHeight().Padding(4.f)[SNew(STextBlock).Text_Lambda([this] { return FText::Format(LOCTEXT("QualityCount", "Motion quality: {0} problem(s) (click to jump)"), QualityIssues.Num()); })]
			+ SVerticalBox::Slot().FillHeight(1.f)
			[
				SNew(SListView<TSharedPtr<UOKAnimSequence::FQualityIssue>>)
				.ListItemsSource(&QualityIssues)
				.OnGenerateRow_Lambda([](TSharedPtr<UOKAnimSequence::FQualityIssue> I, const TSharedRef<STableViewBase>& Owner)
				{
					const FString Text = FString::Printf(TEXT("frames %d-%d  %s  %s  (%.2f)"), I->Start, I->End, *I->Kind, *I->Bone.ToString(), I->Value);
					return SNew(STableRow<TSharedPtr<UOKAnimSequence::FQualityIssue>>, Owner)[SNew(STextBlock).Text(FText::FromString(Text)).ColorAndOpacity(FLinearColor(1.f, 0.45f, 0.4f))];
				})
				.OnMouseButtonClick_Lambda([this](TSharedPtr<UOKAnimSequence::FQualityIssue> I) { if (I) SetFrame(I->Start); })
			]
		];
	})).SetDisplayName(LOCTEXT("ValidationTab", "Validation")).SetGroup(WorkspaceMenuCategory.ToSharedRef());

	InTabManager->RegisterTabSpawner(ChannelTab, FOnSpawnTab::CreateLambda([this](const FSpawnTabArgs&)
	{
		TSharedRef<SVerticalBox> Box = SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(4.f)
			[
				SNew(STextBlock).Font(FAppStyle::GetFontStyle("BoldFont")).Text_Lambda([this] { return PrimaryBoneName(); })
			];
		for (int32 Ch = 0; Ch < FOKBoneKeys::NumChannels; ++Ch)
		{
			Box->AddSlot().AutoHeight().Padding(4.f, 1.f)
			[
				SNew(SHorizontalBox)
				.IsEnabled_Lambda([this, Ch] { return CanAnimate() && Selection.Num() > 0 && (Ch < 3 || PrimaryIsHips()); })
				+ SHorizontalBox::Slot().FillWidth(0.45f).VAlign(VAlign_Center)
				[
					SNew(STextBlock).Text(FOKBoneKeys::ChannelName(Ch))
					.ColorAndOpacity_Lambda([this, Ch] { return ChannelKeyedNow(Ch) ? FSlateColor(FLinearColor(1.f, 0.3f, 0.3f)) : FSlateColor::UseForeground(); })
				]
				+ SHorizontalBox::Slot().FillWidth(0.55f)
				[
					SNew(SSpinBox<float>)
					.MinValue(TOptional<float>()).MaxValue(TOptional<float>())
					.Delta(Ch < 3 ? 0.1f : 0.001f)
					.Value_Lambda([this, Ch] { return GetChannel(Ch); })
					.OnBeginSliderMovement_Lambda([this] { BeginEdit(LOCTEXT("ChannelEdit", "Edit channel")); })
					.OnEndSliderMovement_Lambda([this](float) { EndEdit(); })
					.OnValueChanged_Lambda([this, Ch](float V) { if (bEditing) SetChannel(Ch, V); })
					.OnValueCommitted_Lambda([this, Ch](float V, ETextCommit::Type) { if (!bEditing) { BeginEdit(LOCTEXT("ChannelEdit", "Edit channel")); SetChannel(Ch, V); EndEdit(); } })
				]
			];
		}
		Box->AddSlot().AutoHeight().Padding(4.f)
		[
			SNew(SCheckBox).IsChecked_Lambda([this] { return bAutoKey ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
			.OnCheckStateChanged_Lambda([this](ECheckBoxState S) { SetAutoKey(S == ECheckBoxState::Checked); })
			[SNew(STextBlock).Text(LOCTEXT("AutoKey", "Auto Key"))]
		];
		Box->AddSlot().AutoHeight().Padding(4.f)
		[
			SNew(STextBlock).AutoWrapText(true).ColorAndOpacity(FSlateColor::UseSubduedForeground())
			.Text(LOCTEXT("Hotkeys", "S key 繚 Shift+E key rotate 繚 Shift+W key translate 繚 , . prev/next key 繚 Alt+, Alt+. prev/next frame 繚 Alt+V play 繚 Up/Down pick-walk 繚 Del delete key 繚 Ctrl+C/V copy/paste pose 繚 W/E move/rotate gizmo 繚 Ctrl+` world/local"))
		];
		return SNew(SDockTab)[Box];
	})).SetDisplayName(LOCTEXT("ChannelTab", "Channel Box")).SetGroup(WorkspaceMenuCategory.ToSharedRef());

	InTabManager->RegisterTabSpawner(BlendTab, FOnSpawnTab::CreateLambda([this](const FSpawnTabArgs&)
	{
		return SNew(SDockTab)
		[
			BlendSpace() ? StaticCastSharedRef<SWidget>(SNew(SOKBlendSpacePanel, this))
				: StaticCastSharedRef<SWidget>(SNew(STextBlock).Text(LOCTEXT("NotBlend", "Open an OK blend space to edit its samples here.")))
		];
	})).SetDisplayName(LOCTEXT("BlendTab", "Blend Space")).SetGroup(WorkspaceMenuCategory.ToSharedRef());

	InTabManager->RegisterTabSpawner(DopeTab, FOnSpawnTab::CreateLambda([this](const FSpawnTabArgs&)
	{
		return SNew(SDockTab)[SNew(SScrollBox) + SScrollBox::Slot()[SNew(SOKDopeSheet, this)]];
	})).SetDisplayName(LOCTEXT("DopeTab", "Dope Sheet")).SetGroup(WorkspaceMenuCategory.ToSharedRef());

	InTabManager->RegisterTabSpawner(ContactsTab, FOnSpawnTab::CreateLambda([this](const FSpawnTabArgs&)
	{
		return SNew(SDockTab)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(2.f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth()[SNew(SButton).Text(LOCTEXT("AddContact", "Add Contact")).ToolTipText(LOCTEXT("AddContactTip", "One selected bone: plants on the ground here. Two: the first reaches the second (same body). From the current frame for 0.5 s."))
					.OnClicked_Lambda([this] { AddContactFromSelection(); return FReply::Handled(); })]
				+ SHorizontalBox::Slot().AutoWidth()[SNew(SButton).Text(LOCTEXT("DeleteContact", "Delete Contact")).IsEnabled_Lambda([this] { return SelectedContact != INDEX_NONE; })
					.OnClicked_Lambda([this] { DeleteContact(SelectedContact); return FReply::Handled(); })]
				+ SHorizontalBox::Slot().AutoWidth()[SNew(SButton).Text(LOCTEXT("DetectFeet", "Detect Foot Contacts")).OnClicked_Lambda([this] { ApplyModifier(TEXT("{\"op\":\"foot_contacts\"}")); return FReply::Handled(); })]
			]
			+ SVerticalBox::Slot().FillHeight(1.f)[SNew(SScrollBox) + SScrollBox::Slot()[SNew(SOKContactTrack, this)]]
		];
	})).SetDisplayName(LOCTEXT("ContactsTab", "Contacts")).SetGroup(WorkspaceMenuCategory.ToSharedRef());

	InTabManager->RegisterTabSpawner(FamilyTab, FOnSpawnTab::CreateLambda([this](const FSpawnTabArgs&)
	{
		UOKSkeletonFamily* Family = FamilyForTab();
		if (!Family) return SNew(SDockTab)[SNew(STextBlock).Text(LOCTEXT("NoFamily", "No skeleton family."))];
		// Bone tree (indented by depth) with each bone's alias (a body's own name for it) and mirror partner.
		TSharedRef<SVerticalBox> Tree = SNew(SVerticalBox);
		TSharedPtr<FJsonObject> Header;
		TMap<FString, FString> Parent;
		TArray<FString> Order;
		if (FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Family->RestHeaderJson), Header) && Header)
			for (const TSharedPtr<FJsonValue>& V : Header->GetArrayField(TEXT("bones"))) { const TSharedPtr<FJsonObject> B = V->AsObject(); Order.Add(B->GetStringField(TEXT("name"))); Parent.Add(Order.Last(), B->GetStringField(TEXT("parent"))); }
		TWeakObjectPtr<UOKSkeletonFamily> Weak(Family);
		for (const FString& Bone : Order)
		{
			int32 Depth = 0;
			for (FString P = Parent.FindRef(Bone); Parent.Contains(P) && Depth < 64; P = Parent.FindRef(P)) ++Depth;
			const FName B(*Bone);
			FName Alias;
			for (const TPair<FName, FName>& A : Family->Aliases) if (A.Value == B) Alias = A.Key;
			Tree->AddSlot().AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().Padding(Depth * 10.f, 0.f, 4.f, 0.f).VAlign(VAlign_Center)
				[
					SNew(STextBlock).Text(FText::FromString(Bone)).ColorAndOpacity_Lambda([this, B] { const int32 H = Binding.FamilyName.IndexOfByKey(B); return Selection.Contains(H) ? FLinearColor(1.f, 0.8f, 0.2f) : FLinearColor(0.85f, 0.85f, 0.85f); })
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(4.f, 0.f).VAlign(VAlign_Center)
				[
					SNew(STextBlock).Text_Lambda([Weak, B] { const FName M = Weak.IsValid() ? Weak->MirrorOf(B) : NAME_None; return FText::FromString(M.IsNone() ? FString() : TEXT("<> ") + M.ToString()); }).ColorAndOpacity(FLinearColor(0.5f, 0.7f, 1.f))
				]
				+ SHorizontalBox::Slot().FillWidth(1.f)
				[
					SNew(SEditableTextBox).Text(FText::FromName(Alias)).HintText(LOCTEXT("AliasHint", "alias (a body's own name)"))
					.OnTextCommitted_Lambda([Weak, B](const FText& T, ETextCommit::Type)
					{
						if (!Weak.IsValid()) return;
						const FScopedTransaction Tx(LOCTEXT("AliasT", "Edit family alias"));
						Weak->Modify();
						for (auto It = Weak->Aliases.CreateIterator(); It; ++It) if (It->Value == B) It.RemoveCurrent();
						if (!T.IsEmpty()) Weak->Aliases.Add(FName(*T.ToString()), B);
					})
				]
			];
		}
		TSharedRef<SEditableTextBox> ChainName = SNew(SEditableTextBox).HintText(LOCTEXT("ChainHint", "IK chain name"));
		return SNew(SDockTab)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(2.f)[SNew(STextBlock).Text_Lambda([Weak] { return FText::FromString(Weak.IsValid() ? FString::Printf(TEXT("%s  rest v%d  %d aliases  %d mirror pairs  %d IK chains"), *Weak->GetName(), Weak->RestVersion, Weak->Aliases.Num(), Weak->MirrorPairs.Num(), Weak->IKChains.Num()) : FString()); })]
			+ SVerticalBox::Slot().AutoHeight().Padding(2.f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth()[SNew(SButton).Text(LOCTEXT("AutoMirror", "Auto Mirror Pairs")).OnClicked_Lambda([Weak] { if (Weak.IsValid()) { const FScopedTransaction T(LOCTEXT("AutoMirrorT", "Auto mirror pairs")); Weak->Modify(); Weak->AutoMirrorPairs(); } return FReply::Handled(); })]
				+ SHorizontalBox::Slot().FillWidth(1.f)[ChainName]
				+ SHorizontalBox::Slot().AutoWidth()[SNew(SButton).Text(LOCTEXT("AddChain", "Add IK Chain")).ToolTipText(LOCTEXT("AddChainTip", "From three selected bones (root, mid, end) or a selected end bone and its two parents."))
					.OnClicked_Lambda([this, ChainName] { AddIKChainFromSelection(FName(*ChainName->GetText().ToString())); return FReply::Handled(); })]
				+ SHorizontalBox::Slot().AutoWidth()[SNew(SButton).Text(LOCTEXT("SaveFamily", "Save Family")).OnClicked_Lambda([Weak] { if (Weak.IsValid()) OKAnimationEditing::SaveAsset(Weak.Get()); return FReply::Handled(); })]
			]
			+ SVerticalBox::Slot().FillHeight(1.f)[SNew(SScrollBox) + SScrollBox::Slot()[Tree]]
		];
	})).SetDisplayName(LOCTEXT("FamilyTab", "Skeleton Family")).SetGroup(WorkspaceMenuCategory.ToSharedRef());

	InTabManager->RegisterTabSpawner(MontageTab, FOnSpawnTab::CreateLambda([this](const FSpawnTabArgs&)
	{
		UOKAnimMontage* Montage = Cast<UOKAnimMontage>(Asset);
		if (!Montage) return SNew(SDockTab)[SNew(STextBlock).Text(LOCTEXT("NotMontage", "Open an OK montage to edit its sections here."))];
		TSharedRef<SOKMontageTrack> Track = SNew(SOKMontageTrack, Montage, [this] { bDirty = true; });
		auto Sel = [Track]() { return Track->Selected; };
		return SNew(SDockTab)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(2.f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth()[SNew(SButton).Text(LOCTEXT("LoopSection", "Loop Section")).ToolTipText(LOCTEXT("LoopSectionTip", "The selected section plays again after itself."))
					.OnClicked_Lambda([this, Montage, Sel] { const FScopedTransaction T(LOCTEXT("LoopT", "Loop section")); if (Montage->Sections.IsValidIndex(Sel())) OKAnimationEditing::SetMontageSectionNext(Montage, Sel(), Montage->Sections[Sel()].Name); bDirty = true; return FReply::Handled(); })]
				+ SHorizontalBox::Slot().AutoWidth()[SNew(SButton).Text(LOCTEXT("NextSection", "Branch to Next")).ToolTipText(LOCTEXT("NextSectionTip", "The selected section continues with the section after it on the timeline (wraps to the first)."))
					.OnClicked_Lambda([this, Montage, Sel] { const FScopedTransaction T(LOCTEXT("NextT", "Branch section")); const int32 S = Sel(); if (Montage->Sections.IsValidIndex(S)) OKAnimationEditing::SetMontageSectionNext(Montage, S, Montage->Sections[(S + 1) % Montage->Sections.Num()].Name); bDirty = true; return FReply::Handled(); })]
				+ SHorizontalBox::Slot().AutoWidth()[SNew(SButton).Text(LOCTEXT("ClearNext", "Clear Branch"))
					.OnClicked_Lambda([this, Montage, Sel] { const FScopedTransaction T(LOCTEXT("ClearNextT", "Clear branch")); OKAnimationEditing::SetMontageSectionNext(Montage, Sel(), NAME_None); bDirty = true; return FReply::Handled(); })]
			]
			+ SVerticalBox::Slot().FillHeight(1.f)[SNew(SScrollBox) + SScrollBox::Slot()[Track]]
		];
	})).SetDisplayName(LOCTEXT("MontageTab", "Montage Sections")).SetGroup(WorkspaceMenuCategory.ToSharedRef());

	InTabManager->RegisterTabSpawner(AssetsTab, FOnSpawnTab::CreateLambda([this](const FSpawnTabArgs&)
	{
		// Every OK asset of the project (asset registry), filtered by name / type; double-click opens it.
		struct FRow { FAssetData Data; FString Label; };
		TSharedRef<TArray<TSharedPtr<FRow>>> All = MakeShared<TArray<TSharedPtr<FRow>>>();
		TSharedRef<TArray<TSharedPtr<FRow>>> Shown = MakeShared<TArray<TSharedPtr<FRow>>>();
		TSharedRef<FString> Filter = MakeShared<FString>();
		TSharedPtr<SListView<TSharedPtr<FRow>>> ListView;
		auto Reload = [All]()
		{
			All->Reset();
			TArray<FAssetData> Found;
			FARFilter F;
			F.ClassPaths.Add(UOKAnimationAsset::StaticClass()->GetClassPathName());
			F.ClassPaths.Add(UOKPoseLibrary::StaticClass()->GetClassPathName());
			F.ClassPaths.Add(UOKSkeletonFamily::StaticClass()->GetClassPathName());
			F.ClassPaths.Add(UOKRetargetProfile::StaticClass()->GetClassPathName());
			F.bRecursiveClasses = true;
			IAssetRegistry::GetChecked().GetAssets(F, Found);
			Found.Sort([](const FAssetData& A, const FAssetData& B) { return A.PackageName.LexicalLess(B.PackageName); });
			for (const FAssetData& D : Found) All->Add(MakeShared<FRow>(FRow{ D, FString::Printf(TEXT("%s  [%s]  %s"), *D.AssetName.ToString(), *D.AssetClassPath.GetAssetName().ToString().Replace(TEXT("OK"), TEXT("")), *D.PackagePath.ToString()) }));
		};
		auto Refilter = [All, Shown, Filter]()
		{
			Shown->Reset();
			TArray<FString> Words;
			Filter->ToLower().ParseIntoArrayWS(Words);
			for (const TSharedPtr<FRow>& R : *All)
			{
				const FString L = R->Label.ToLower();
				if (Words.ContainsByPredicate([&L](const FString& W) { return !L.Contains(W); })) continue;
				Shown->Add(R);
			}
		};
		Reload();
		Refilter();
		TSharedRef<SDockTab> Tab = SNew(SDockTab)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(2.f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.f)[SNew(SEditableTextBox).HintText(LOCTEXT("AssetFilter", "Filter (name, type, folder)"))
					.OnTextChanged_Lambda([Filter, Refilter, this](const FText& T) { *Filter = T.ToString(); Refilter(); if (AssetsList) AssetsList->RequestListRefresh(); })]
				+ SHorizontalBox::Slot().AutoWidth()[SNew(SButton).Text(LOCTEXT("AssetsRefresh", "Refresh")).OnClicked_Lambda([Reload, Refilter, this] { Reload(); Refilter(); if (AssetsList) AssetsList->RequestListRefresh(); return FReply::Handled(); })]
			]
			+ SVerticalBox::Slot().FillHeight(1.f)
			[
				SAssignNew(ListView, SListView<TSharedPtr<FRow>>)
				.ListItemsSource(&Shown.Get())
				.OnGenerateRow_Lambda([Shown](TSharedPtr<FRow> R, const TSharedRef<STableViewBase>& Owner)
				{
					return SNew(STableRow<TSharedPtr<FRow>>, Owner)[SNew(STextBlock).Text(FText::FromString(R->Label)).ToolTipText(FText::FromString(R->Data.GetObjectPathString()))];
				})
				.OnMouseButtonDoubleClick_Lambda([](TSharedPtr<FRow> R) { if (R && GEditor) if (UObject* O = R->Data.GetAsset()) GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(O); })
			]
		];
		AssetsList = ListView;
		return Tab;
	})).SetDisplayName(LOCTEXT("AssetsTab", "OK Assets")).SetGroup(WorkspaceMenuCategory.ToSharedRef());

	InTabManager->RegisterTabSpawner(PosesTab, FOnSpawnTab::CreateLambda([this](const FSpawnTabArgs&)
	{
		TSharedRef<SEditableTextBox> NameBox = SNew(SEditableTextBox).HintText(LOCTEXT("PoseNameHint", "Pose name"));
		TSharedRef<SVerticalBox> List = SNew(SVerticalBox);
		auto Rebuild = [this, List]()
		{
			List->ClearChildren();
			for (const FOKPose& P : PoseLibrary()->Poses)
			{
				const FName N = P.Name;
				List->AddSlot().AutoHeight().Padding(1.f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)[SNew(STextBlock).Text(FText::FromString(FString::Printf(TEXT("%s  (%d bones%s)"), *N.ToString(), P.Bones.Num(), P.bHasHips ? TEXT(", hips") : TEXT(""))))]
					+ SHorizontalBox::Slot().AutoWidth()[SNew(SButton).Text(LOCTEXT("ApplyPose", "Apply")).ToolTipText(LOCTEXT("ApplyPoseTip", "Keys the whole stored pose at the current frame (weight from Preview Settings > Poses)."))
						.OnClicked_Lambda([this, N] { ApplyPose(N, false, Settings->PoseWeight); return FReply::Handled(); })]
					+ SHorizontalBox::Slot().AutoWidth()[SNew(SButton).Text(LOCTEXT("ApplyPoseSel", "Apply to Selected")).ToolTipText(LOCTEXT("ApplyPoseSelTip", "Keys only the selected bones from the stored pose."))
						.OnClicked_Lambda([this, N] { ApplyPose(N, true, Settings->PoseWeight); return FReply::Handled(); })]
				];
			}
		};
		Rebuild();
		return SNew(SDockTab)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(2.f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.f)[NameBox]
				+ SHorizontalBox::Slot().AutoWidth()[SNew(SButton).Text(LOCTEXT("StorePose", "Store Pose")).ToolTipText(LOCTEXT("StorePoseTip", "Stores the pose of the selected bones (all when none) under the name."))
					.OnClicked_Lambda([this, NameBox, Rebuild] { StorePose(FName(*NameBox->GetText().ToString())); Rebuild(); return FReply::Handled(); })]
				+ SHorizontalBox::Slot().AutoWidth()[SNew(SButton).Text(LOCTEXT("RefreshPoses", "Refresh")).OnClicked_Lambda([Rebuild] { Rebuild(); return FReply::Handled(); })]
			]
			+ SVerticalBox::Slot().FillHeight(1.f)[SNew(SScrollBox) + SScrollBox::Slot()[List]]
		];
	})).SetDisplayName(LOCTEXT("PosesTab", "Poses")).SetGroup(WorkspaceMenuCategory.ToSharedRef());

	InTabManager->RegisterTabSpawner(GraphTab, FOnSpawnTab::CreateLambda([this](const FSpawnTabArgs&)
	{
		return SNew(SDockTab)[SNew(SCurveEditorPanel, CurveEditor.ToSharedRef())];
	})).SetDisplayName(LOCTEXT("GraphTab", "Graph Editor")).SetGroup(WorkspaceMenuCategory.ToSharedRef());
}

void FOKAnimationEditor::UnregisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
	FAssetEditorToolkit::UnregisterTabSpawners(InTabManager);
	for (const FName Tab : { ViewportTab, DetailsTab, PreviewTab, OutlineTab, ValidationTab, ChannelTab, GraphTab, DopeTab, BlendTab, ContactsTab, PosesTab, AssetsTab, MontageTab, FamilyTab }) InTabManager->UnregisterTabSpawner(Tab);
}

void FOKAnimationEditor::FillToolbar(FToolBarBuilder& Builder)
{
	auto Button = [&](const FText& Label, const FText& Tip, TFunction<void()> Do, TFunction<bool()> Can = nullptr)
	{
		Builder.AddToolBarButton(FUIAction(FExecuteAction::CreateLambda(Do), Can ? FCanExecuteAction::CreateLambda(Can) : FCanExecuteAction()), NAME_None, Label, Tip);
	};
	Builder.BeginSection("OKAnimation");
	Button(LOCTEXT("ValidateButton", "Validate"), LOCTEXT("ValidateTip", "Checks the spec, the roles, the contacts and the blend samples."), [this] { RunValidation(); });
	if (Cast<UOKAnimSequence>(Asset))
	{
		auto CanAnim = [this] { return CanAnimate(); };
		Builder.AddSeparator();
		Button(LOCTEXT("Key", "Set Key"), LOCTEXT("KeyTip", "Keys the selected bones at the current frame (S)."), [this] { KeySelection(true, true); }, CanAnim);
		Button(LOCTEXT("KeyAll", "Key All"), LOCTEXT("KeyAllTip", "Keys every animated bone at the current frame."), [this] { KeyAllBones(); }, CanAnim);
		Button(LOCTEXT("DeleteKey", "Delete Key"), LOCTEXT("DeleteKeyTip", "Deletes the keys of the selected bones (all bones when none) at the current frame (Del)."), [this] { DeleteKeysAtCurrent(); }, CanAnim);
		Button(LOCTEXT("CopyPose", "Copy Pose"), LOCTEXT("CopyPoseTip", "Copies the pose of the selected bones (all when none) at this frame (Ctrl+C)."), [this] { CopyPose(); }, CanAnim);
		Button(LOCTEXT("PastePose", "Paste Pose"), LOCTEXT("PastePoseTip", "Keys the copied pose at this frame (Ctrl+V)."), [this] { PastePose(); }, CanAnim);
		Button(LOCTEXT("Mirror", "Mirror Pose"), LOCTEXT("MirrorTip", "The selected bones' opposite side takes their mirrored pose; nothing selected mirrors the whole pose (sides swapped)."), [this] { MirrorPose(); }, CanAnim);
		Button(LOCTEXT("Spline", "Spline"), LOCTEXT("SplineTip", "Keys at this frame (selected bones, or all) interpolate smoothly (auto tangents)."), [this] { SetKeyInterpolation(RCIM_Cubic); }, CanAnim);
		Button(LOCTEXT("Linear", "Linear"), LOCTEXT("LinearTip", "Keys at this frame interpolate linearly."), [this] { SetKeyInterpolation(RCIM_Linear); }, CanAnim);
		Button(LOCTEXT("Stepped", "Stepped"), LOCTEXT("SteppedTip", "Keys at this frame hold until the next key (blocking)."), [this] { SetKeyInterpolation(RCIM_Constant); }, CanAnim);
		Button(LOCTEXT("BuildPreview", "Family Preview Mesh"), LOCTEXT("BuildPreviewTip", "Builds (and saves) a stick-figure preview mesh of the default skeleton family, to animate its own animations directly."), [this]
		{
			FString Error;
			UOKSkeletonFamily* Family = OKAnimationEditing::EnsureDefaultFamilyAsset(Error, true);
			if (!Family) { Notify(FText::FromString(Error), false); return; }
			Settings->Mesh = Family->PreviewMesh.LoadSynchronous();
			Settings->Profile = nullptr;
			bDirty = true;
			Notify(FText::Format(LOCTEXT("PreviewBuilt", "Preview mesh {0}"), FText::FromString(Settings->Mesh ? Settings->Mesh->GetPathName() : FString())), Settings->Mesh != nullptr);
		});
		Builder.AddToolBarButton(FUIAction(FExecuteAction::CreateLambda([this] { SetAutoKey(!bAutoKey); }), FCanExecuteAction(),
			FIsActionChecked::CreateLambda([this] { return bAutoKey; })), NAME_None, LOCTEXT("AutoKeyButton", "Auto Key"),
			LOCTEXT("AutoKeyTip", "On: posing keys at once. Off: posing is kept until the frame changes unless keyed (S)."), FSlateIcon(), EUserInterfaceActionType::ToggleButton);
		Builder.AddToolBarButton(FUIAction(FExecuteAction::CreateLambda([this] { SetRecording(!bRecording); }), FCanExecuteAction::CreateLambda(CanAnim),
			FIsActionChecked::CreateLambda([this] { return bRecording; })), NAME_None, LOCTEXT("RecordButton", "Record"),
			LOCTEXT("RecordTip", "On: while playing, the bones you drag hold their pose and are keyed on every frame (live recording)."), FSlateIcon(), EUserInterfaceActionType::ToggleButton);
		Button(LOCTEXT("Collapse", "Collapse Layer"), LOCTEXT("CollapseTip", "Makes base + key layer the new base motion (the keys are removed)."), [this]
		{
			const FScopedTransaction T(LOCTEXT("CollapseT", "Collapse key layer"));
			Sequence()->Modify();
			FString Error;
			if (!Sequence()->CollapseLayer(Error)) Notify(FText::FromString(Error), false);
			AfterLayerEdit();
		}, [this] { return Sequence() && Sequence()->KeyLayer.Num() > 0; });
		Button(LOCTEXT("ClearLayer", "Clear Layer"), LOCTEXT("ClearLayerTip", "Removes every key of the key layer (back to the base motion)."), [this]
		{
			const FScopedTransaction T(LOCTEXT("ClearT", "Clear key layer"));
			Sequence()->Modify();
			Sequence()->KeyLayer.Reset();
			AfterLayerEdit();
		}, [this] { return Sequence() && Sequence()->KeyLayer.Num() > 0; });
		Builder.AddComboButton(FUIAction(FExecuteAction(), FCanExecuteAction::CreateLambda(CanAnim)), FOnGetContent::CreateLambda([this]
		{
			FMenuBuilder Menu(true, nullptr);
			auto Item = [&](const FText& Label, const FText& Tip, const FString& Op)
			{
				Menu.AddMenuEntry(Label, Tip, FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([this, Op] { ApplyModifier(Op); })));
			};
			Item(LOCTEXT("ModTrim", "Trim Rest Start"), LOCTEXT("ModTrimTip", "Removes leading frames at the source's rest (a T-pose before mocap motion)."), TEXT("{\"op\":\"trim_rest_start\",\"threshold\":3}"));
			Item(LOCTEXT("ModLoop", "Auto Loop"), LOCTEXT("ModLoopTip", "Finds the best loop (pose and velocity match, at least 0.5 s), cuts the clip to it and closes the seam."), TEXT("{\"op\":\"auto_loop\",\"min_seconds\":0.5}"));
			Item(LOCTEXT("ModClose", "Close Loop"), LOCTEXT("ModCloseTip", "Spreads the difference between the last and first frame over the clip so it loops seamlessly."), TEXT("{\"op\":\"close_loop\"}"));
			Item(LOCTEXT("ModInPlace", "Bake In Place"), LOCTEXT("ModInPlaceTip", "Removes the hips' net horizontal travel (walks on the spot)."), TEXT("{\"op\":\"in_place\"}"));
			Item(LOCTEXT("ModSmooth", "Smooth (De-jitter)"), LOCTEXT("ModSmoothTip", "Averages every frame with its neighbours (2 frames each side)."), TEXT("{\"op\":\"smooth\",\"radius\":2}"));
			Item(LOCTEXT("ModFeet", "Detect Foot Contacts"), LOCTEXT("ModFeetTip", "Adds ground contacts where a foot is low and still (replaces earlier foot ground contacts)."), TEXT("{\"op\":\"foot_contacts\"}"));
			Item(LOCTEXT("ModNorm", "Renormalize"), LOCTEXT("ModNormTip", "Normalises the rotations and removes sign flips between frames."), TEXT("{\"op\":\"renormalize\"}"));
			// Time tools at the current frame.
			Menu.AddSeparator();
			const UOKAnimSequence* Seq = Sequence();
			const float Now = Seq ? CurrentFrame() / FMath::Max(Seq->Fps, 1.f) : 0.f;
			const float Length = Seq ? Seq->Duration : 0.f;
			Item(LOCTEXT("TrimBefore", "Trim Before Current Frame"), LOCTEXT("TrimBeforeTip", "Removes the frames before the current one."), FString::Printf(TEXT("{\"op\":\"trim\",\"start\":%f,\"end\":%f}"), Now, Length));
			Item(LOCTEXT("TrimAfter", "Trim After Current Frame"), LOCTEXT("TrimAfterTip", "Removes the frames after the current one."), FString::Printf(TEXT("{\"op\":\"trim\",\"start\":0,\"end\":%f}"), Now));
			Item(LOCTEXT("InsertFrame", "Insert Frame (hold)"), LOCTEXT("InsertFrameTip", "Holds the current frame one frame longer (everything after moves later)."), FString::Printf(TEXT("{\"op\":\"insert_frames\",\"at\":%d,\"count\":1}"), CurrentFrame()));
			Item(LOCTEXT("DeleteFrame", "Delete Frame"), LOCTEXT("DeleteFrameTip", "Removes the current frame (everything after moves earlier)."), FString::Printf(TEXT("{\"op\":\"insert_frames\",\"at\":%d,\"count\":-1}"), CurrentFrame()));
			for (const float Fps : { 24.f, 30.f, 60.f })
				Item(FText::Format(LOCTEXT("Resample", "Resample to {0} fps"), Fps), LOCTEXT("ResampleTip", "The same motion and duration at another frame rate."), FString::Printf(TEXT("{\"op\":\"resample\",\"fps\":%f}"), Fps));
			for (const float Factor : { 0.5f, 0.8f, 1.25f, 2.f })
				Item(FText::Format(LOCTEXT("Speed", "Speed x{0}"), Factor), LOCTEXT("SpeedTip", "Plays faster or slower (the frame rate stays, the length changes)."), FString::Printf(TEXT("{\"op\":\"speed\",\"factor\":%f}"), Factor));
			return Menu.MakeWidget();
		}), LOCTEXT("Modifiers", "Modifiers"), LOCTEXT("ModifiersTip", "Cleanup and modifiers on the whole motion (undoable)."));
		Builder.AddSeparator();
		Button(LOCTEXT("Cards", "Load Cards"), LOCTEXT("CardsTip", "Loads the character cards of Preview Settings > Cards beside the preview mesh and plays this animation on them (the game's character pipeline)."), [this]
		{
			FString Error;
			const int32 N = LoadCardPreviews(Error);
			Notify(FText::Format(LOCTEXT("CardsLoaded", "{0} card(s) loaded {1}"), N, FText::FromString(Error)), Error.IsEmpty());
		});
		Button(LOCTEXT("Sequencer", "Edit in Sequencer"), LOCTEXT("SequencerTip", "Expands onto the preview mesh and opens a Level Sequence with the mesh and this animation (Sequencer / Control Rig). Write the result back with Write Back."), [this]
		{
			UOKAnimSequence* Seq = Sequence();
			USkeletalMesh* Mesh = Settings->Mesh;
			if (Settings->Profile) Seq->EditProfile = Settings->Profile;
			FString Error;
			const ULevelSequence* Level = OKAnimationEditing::MakeEditSequence(Seq, Mesh, true, Error);
			Notify(Level ? FText::Format(LOCTEXT("SequencerOk", "Level sequence {0}"), FText::FromString(Level->GetPathName())) : FText::FromString(Error), Level != nullptr);
		});
		Button(LOCTEXT("Expand", "Expand to AnimSequence"), LOCTEXT("ExpandTip", "Writes an AnimSequence on the preview mesh's skeleton (contacts as OK Contact notifies, curves as curves) to edit with Unreal's tools."), [this]
		{
			UOKAnimSequence* Seq = Sequence();
			USkeleton* Skeleton = Settings->Mesh ? Settings->Mesh->GetSkeleton() : OKAnimationEditing::DefaultExpandSkeleton(Seq);
			if (Settings->Profile) Seq->EditProfile = Settings->Profile;
			FString Error;
			UAnimSequence* Proxy = OKAnimationEditing::ExpandToAnimSequence(Seq, Skeleton, Error);
			Notify(Proxy ? FText::Format(LOCTEXT("Expanded", "Expanded to {0}"), FText::FromString(Proxy->GetName())) : FText::FromString(Error), Proxy != nullptr);
		});
		Button(LOCTEXT("WriteBack", "Write Back"), LOCTEXT("WriteBackTip", "Reads the bones, contacts and curves back from the expanded AnimSequence."), [this]
		{
			FString Error;
			const bool bOk = OKAnimationEditing::WriteBack(Sequence(), Error);
			Notify(bOk ? LOCTEXT("WrittenBack", "Written back from the AnimSequence") : FText::FromString(Error), bOk);
			bDirty = true;
		});
		Button(LOCTEXT("Export", "Export .okanim"), LOCTEXT("ExportTip", "Writes the OK animation file to Saved/OKAnimExport."), [this]
		{
			const FString Path = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("OKAnimExport"), Asset->GetName() + TEXT(".okanim"));
			FString Error;
			const bool bOk = OKAnimationEditing::ExportFile(Sequence(), Path, Error);
			Notify(bOk ? FText::Format(LOCTEXT("Exported", "Exported {0}"), FText::FromString(Path)) : FText::FromString(Error), bOk);
		});
	}
	Builder.EndSection();
}

void FOKAnimationEditor::RefreshOutline()
{
	Outline.Reset();
	if (const UOKAnimSequence* Seq = Cast<UOKAnimSequence>(Asset))
	{
		Outline.Add(MakeShared<FString>(FString::Printf(TEXT("%s  %d bones  %d frames @ %.0f fps  %s"), *Seq->Family.ToString(), Seq->Bones.Num(), Seq->Frames, Seq->Fps,
			Seq->bLooping ? TEXT("loop") : TEXT("once"))));
		for (const FName Bone : Seq->Bones)
		{
			int32 Depth = 0;
			for (const FName* P = Seq->BoneParents.Find(Bone); P && Depth < 64; P = Seq->BoneParents.Find(*P)) ++Depth;
			Outline.Add(MakeShared<FString>(FString::ChrN(Depth * 2, TEXT(' ')) + Bone.ToString()));
		}
	}
	else if (Asset)
	{
		FString Spec, Error;
		TArray<TArray<uint8>> Blobs;
		if (!Asset->BuildSpec(Spec, Blobs, Error)) Outline.Add(MakeShared<FString>(Error));
		else
		{
			TSharedPtr<FJsonObject> Json;
			const auto Reader = TJsonReaderFactory<>::Create(Spec);
			FString Pretty;
			if (FJsonSerializer::Deserialize(Reader, Json) && Json)
			{
				const auto Writer = TJsonWriterFactory<>::Create(&Pretty);
				FJsonSerializer::Serialize(Json.ToSharedRef(), Writer);
			}
			TArray<FString> Lines;
			Pretty.ParseIntoArrayLines(Lines);
			Outline.Add(MakeShared<FString>(FString::Printf(TEXT("%d roles, %d animations"), Asset->GetRoleCount(), Blobs.Num())));
			for (const FString& L : Lines) Outline.Add(MakeShared<FString>(L.Replace(TEXT("\t"), TEXT("  "))));
		}
	}
	if (OutlineView) OutlineView->RequestListRefresh();
}

void FOKAnimationEditor::RunValidation()
{
	const TArray<FString> Issues = OKAnimationEditing::Validate(Asset);
	QualityIssues.Reset();
	if (const UOKAnimSequence* Seq = Sequence())
	{
		TArray<UOKAnimSequence::FQualityIssue> Found;
		FString Error;
		if (Seq->CheckQuality(Found, Error)) for (const UOKAnimSequence::FQualityIssue& I : Found) QualityIssues.Add(MakeShared<UOKAnimSequence::FQualityIssue>(I));
	}
	ValidationText = Issues.IsEmpty() ? FString(TEXT("No problems found.")) : FString::Join(Issues, TEXT("\n"));
	if (!Binding.IsValid() && Settings->Mesh) ValidationText += TEXT("\nPreview: the preview mesh's skeleton is not mapped (set a retarget profile).");
}

// ------------------------------------------------------------------------------------------------ preview

void FOKAnimationEditor::RebindMesh()
{
	USkeletalMesh* Mesh = Settings->Mesh;
	if (PreviewMesh->GetSkinnedAsset() != Mesh)
	{
		PreviewMesh->SetSkinnedAssetAndUpdate(Mesh);
		bNeedFocus = true;
		Selection.Reset();
	}
	Binding = FOKUnrealSkeleton();
	bPosed = false;
	if (!Mesh) { Status = TEXT("(no preview mesh: set one in Preview Settings)"); return; }
	const UOKSkeletonFamily* Family = Settings->Profile && Settings->Profile->Family ? Settings->Profile->Family.Get() : UOKAnimationLibrary::GetDefaultFamily();
	FString Error;
	if (!Binding.Build(Mesh->GetRefSkeleton(), Settings->Profile, Family, Error)) Status = FString::Printf(TEXT("(%s)"), *Error);
	Selection.RemoveAll([this](int32 H) { return !Binding.BoneIndex.IsValidIndex(H); });
}

void FOKAnimationEditor::RefreshPlayer()
{
	FString Error;
	Player = Asset ? Asset->CreatePlayer(Error) : nullptr;
	// Retarget preview: the source converted now (profile edits show at once) replaces what plays on the preview mesh.
	RetargetResult = nullptr;
	if (IsRetargetPreview())
	{
		const UOKSkeletonFamily* Family = Settings->RetargetProfile && Settings->RetargetProfile->Family ? Settings->RetargetProfile->Family.Get() : UOKAnimationLibrary::GetDefaultFamily();
		RetargetResult = UOKAnimationLibrary::ConvertUnrealAnimation(Settings->RetargetSource, Settings->RetargetProfile, const_cast<UOKSkeletonFamily*>(Family), GetDefault<UOKAnimationSettings>()->ImportFps, GetTransientPackage(), NAME_None, Error);
		Player = RetargetResult ? RetargetResult->CreatePlayer(Error) : nullptr;
		if (!Player) Status = FString::Printf(TEXT("(retarget: %s)"), *Error);
		USkeletalMesh* SourcePreview = Settings->RetargetSource->GetSkeleton() ? Settings->RetargetSource->GetSkeleton()->GetPreviewMesh(true) : nullptr;
		if (!SourceMesh)
		{
			SourceMesh = NewObject<UPoseableMeshComponent>(GetTransientPackage());
			Scene->AddComponent(SourceMesh, FTransform(FVector(-150.f, 0.f, 0.f)));
		}
		if (SourceMesh->GetSkinnedAsset() != SourcePreview) SourceMesh->SetSkinnedAssetAndUpdate(SourcePreview);
		SourceBinding = FOKUnrealSkeleton();
		if (SourcePreview) SourceBinding.Build(SourcePreview->GetRefSkeleton(), Settings->RetargetProfile, Family, Error);
	}
	else if (SourceMesh) { Scene->RemoveComponent(SourceMesh); SourceMesh = nullptr; }
	RefreshCardAnimation();
	if (!Player) Status = FString::Printf(TEXT("(%s)"), *Error);
	// One extra preview per other role of a group (same mesh, shared group space).
	const int32 Others = Player && Settings->Mesh ? FMath::Max(0, Player->Roles() - 1) : 0;
	while (RoleMeshes.Num() > Others) { if (UPoseableMeshComponent* M = RoleMeshes.Pop()) Scene->RemoveComponent(M); }
	while (RoleMeshes.Num() < Others)
	{
		UPoseableMeshComponent* M = NewObject<UPoseableMeshComponent>(GetTransientPackage());
		Scene->AddComponent(M, FTransform::Identity);
		RoleMeshes.Add(M);
	}
	for (UPoseableMeshComponent* M : RoleMeshes) if (M->GetSkinnedAsset() != Settings->Mesh) M->SetSkinnedAssetAndUpdate(Settings->Mesh);
	UpdatePose();
}

void FOKAnimationEditor::Rebuild()
{
	bDirty = false;
	Status.Reset();
	RebindMesh();
	RefreshPlayer();
	RefreshOutline();
	RefreshCurves();
	RunValidation();
}

void FOKAnimationEditor::UpdatePose()
{
	if (!Player || !Binding.IsValid() || PreviewMesh->GetSkinnedAsset() == nullptr) return;
	Player->SetParam(0, Settings->X);
	Player->SetParam(1, Settings->Y);
	TArray<float> Sample;
	if (!Player->SampleOnto(Settings->Role, Binding.HeaderJson, Time, Sample) || Sample.Num() != Binding.BoneIndex.Num() * 4 + 3)
	{
		Status = FString::Printf(TEXT("(preview sample failed: %s)"), *FOkAnimBridge::LastError());
		UE_LOG(LogTemp, Warning, TEXT("OK animation editor: %s"), *Status);
		return;
	}
	TArray<FTransform> Local = Binding.Ref.GetRefBonePose();
	Binding.LocalsToPose(Sample.GetData(), Local);
	if (PreviewMesh->BoneSpaceTransforms.Num() != Local.Num()) return;
	PreviewMesh->BoneSpaceTransforms = Local;
	PreviewMesh->RefreshBoneTransforms();
	bPosed = true;
	TickCardPreviews();
	// The retarget source at the same time on its own skeleton, beside the result.
	if (IsRetargetPreview() && SourceMesh && SourceMesh->GetSkinnedAsset() && SourceBinding.Ref.GetNum() == SourceMesh->BoneSpaceTransforms.Num())
	{
		TArray<FTransform> Src;
		SourceBinding.SampleSource(Settings->RetargetSource, FMath::Min<double>(Time, Settings->RetargetSource->GetPlayLength()), Src);
		SourceMesh->BoneSpaceTransforms = Src;
		SourceMesh->RefreshBoneTransforms();
	}
	for (int32 I = 0, Role = 0; I < RoleMeshes.Num(); ++Role)
	{
		if (Role == Settings->Role) continue;
		UPoseableMeshComponent* M = RoleMeshes[I++];
		TArray<float> Other;
		if (!Player->SampleOnto(Role, Binding.HeaderJson, Time, Other) || Other.Num() != Sample.Num() || M->BoneSpaceTransforms.Num() != Local.Num()) continue;
		TArray<FTransform> OtherLocal = Binding.Ref.GetRefBonePose();
		Binding.LocalsToPose(Other.GetData(), OtherLocal);
		M->BoneSpaceTransforms = OtherLocal;
		M->SetRelativeTransform(RolePlacement(Role));
		M->RefreshBoneTransforms();
	}
}

bool FOKAnimationEditor::HasPreviewPose() const { return bPosed && Binding.IsValid() && PreviewMesh->GetComponentSpaceTransforms().Num() == Binding.Ref.GetNum(); }
const TArray<FTransform>& FOKAnimationEditor::GetComponentPose() const { return HasPreviewPose() ? PreviewMesh->GetComponentSpaceTransforms() : EmptyPose; }
FTransform FOKAnimationEditor::BoneTransform(int32 H) const
{
	const TArray<FTransform>& Comp = GetComponentPose();
	return Binding.BoneIndex.IsValidIndex(H) && Comp.IsValidIndex(Binding.BoneIndex[H]) ? Comp[Binding.BoneIndex[H]] : FTransform::Identity;
}

void FOKAnimationEditor::Tick(float DeltaTime)
{
	if (!Asset || !Settings) return;
	if (bDirty) Rebuild();
	if (bNeedFocus && Viewport && Settings->Mesh && Viewport->GetCachedGeometry().GetLocalSize().X > 1.f)
	{
		Viewport->Focus(Settings->Mesh->GetBounds().GetBox());
		bNeedFocus = false;
	}
	if (UOKAnimSequence* Seq = Sequence())
	{
		// Edits made elsewhere (graph editor, details) show at once.
		if (CurveShape() != LastCurveShape) RefreshCurves();
		if (!bEditing && LayerSignature() != LastSignature)
		{
			FString Error;
			if (!Seq->Rebake(Error)) Status = Error;
			LastSignature = LayerSignature();
			RefreshPlayer();
		}
	}
	if (!bPlaying) return;
	const float D = Duration();
	// Recording: the dragged bones keep their pose as time runs on, keyed at every new frame.
	TArray<FQuat> Held;
	FVector HeldHips = FVector::ZeroVector;
	const bool bRecord = bRecording && bEditing && Selection.Num() && CanAnimate();
	const int32 FrameBefore = CurrentFrame();
	if (bRecord) { for (const int32 H : Selection) Held.Add(BoneTransform(H).GetRotation()); HeldHips = BoneTransform(Binding.Hips).GetLocation(); }
	Time += DeltaTime * Settings->PlayRate;
	if (D > 0.f && Time > D) Time = FMath::Fmod(Time, D);
	UpdatePose();
	if (bRecord && CurrentFrame() != FrameBefore)
	{
		const TArray<int32> Keep = Selection;
		SnapToFrame();
		for (int32 I = 0; I < Keep.Num(); ++I)
		{
			Selection = { Keep[I] };
			RotateSelection(Held[I] * BoneTransform(Keep[I]).GetRotation().Inverse());
			if (Keep[I] == Binding.Hips) TranslateSelection(HeldHips - BoneTransform(Keep[I]).GetLocation());
			KeySelection(true, Keep[I] == Binding.Hips);
		}
		Selection = Keep;
		bPlaying = true;   // keying snaps to the frame and pauses; recording plays on
	}
}

void FOKAnimationEditor::SetRecording(bool bOn)
{
	bRecording = bOn;
	if (bOn) SetAutoKey(true);
}

// ------------------------------------------------------------------------------------------------ timeline

int32 FOKAnimationEditor::NumFrames() const
{
	if (const UOKAnimSequence* Seq = Sequence()) return FMath::Max(1, Seq->Frames);
	return FMath::Max(1, FMath::RoundToInt(Duration() * 30.f) + 1);
}
float FOKAnimationEditor::FrameRate() const { const UOKAnimSequence* Seq = Sequence(); return Seq ? FMath::Max(1.f, Seq->Fps) : 30.f; }
int32 FOKAnimationEditor::CurrentFrame() const { return FMath::Clamp(FMath::RoundToInt(Time * FrameRate()), 0, NumFrames() - 1); }

void FOKAnimationEditor::SetFrame(int32 Frame)
{
	RevertPending();
	bPlaying = false;
	Time = FMath::Clamp(Frame, 0, NumFrames() - 1) / FrameRate();
	UpdatePose();
}

void FOKAnimationEditor::SetTimeContinuous(float Seconds) { RevertPending(); Time = Seconds; UpdatePose(); }

void FOKAnimationEditor::TogglePlay()
{
	RevertPending();
	bPlaying = !bPlaying;
	if (!bPlaying) SetFrame(CurrentFrame());
}

void FOKAnimationEditor::StepFrame(int32 Direction)
{
	const int32 N = NumFrames();
	SetFrame((CurrentFrame() + Direction + N) % N);
}

void FOKAnimationEditor::JumpEnd(bool bEnd) { SetFrame(bEnd ? NumFrames() - 1 : 0); }

TArray<FName> FOKAnimationEditor::KeyedBonesForTimeline() const
{
	TArray<FName> Out;
	if (Selection.Num()) { for (const int32 H : Selection) Out.Add(Binding.FamilyName[H]); }
	else if (const UOKAnimSequence* Seq = Sequence()) { for (const FOKBoneKeys& K : Seq->KeyLayer) Out.Add(K.Bone); }
	return Out;
}

void FOKAnimationEditor::GetTimelineKeys(TArray<int32>& OutFrames) const
{
	OutFrames.Reset();
	UOKAnimSequence* Seq = Sequence();
	if (!Seq) return;
	for (const FName Bone : KeyedBonesForTimeline())
	{
		const FOKBoneKeys* Keys = Seq->FindKeys(Bone);
		if (!Keys) continue;
		TArray<float> Times;
		Keys->GetKeyTimes(Times);
		for (const float T : Times) OutFrames.AddUnique(FMath::RoundToInt(T * FrameRate()));
	}
	OutFrames.Sort();
}

void FOKAnimationEditor::JumpKey(int32 Direction)
{
	TArray<int32> Keys;
	GetTimelineKeys(Keys);
	if (Keys.IsEmpty()) return;
	const int32 Cur = CurrentFrame();
	if (Direction > 0) { for (const int32 F : Keys) if (F > Cur) { SetFrame(F); return; } SetFrame(Keys[0]); }
	else { for (int32 I = Keys.Num() - 1; I >= 0; --I) if (Keys[I] < Cur) { SetFrame(Keys[I]); return; } SetFrame(Keys.Last()); }
}

void FOKAnimationEditor::MoveKeys(const TArray<int32>& Frames, int32 Delta) { MoveKeysOf(KeyedBonesForTimeline(), Frames, Delta); }
void FOKAnimationEditor::DeleteKeys(const TArray<int32>& Frames) { DeleteKeysOf(KeyedBonesForTimeline(), Frames); }

void FOKAnimationEditor::MoveKeysOf(const TArray<FName>& Bones, const TArray<int32>& Frames, int32 Delta)
{
	UOKAnimSequence* Seq = Sequence();
	if (!Seq || Delta == 0 || Frames.IsEmpty()) return;
	const FScopedTransaction T(LOCTEXT("MoveKeys", "Move keys"));
	Seq->Modify();
	TArray<int32> Order = Frames;
	Order.Sort();
	if (Delta > 0) Algo::Reverse(Order);
	const float Tol = 0.5f / FrameRate();
	for (const FName Bone : Bones)
		if (FOKBoneKeys* Keys = Seq->FindKeys(Bone))
			for (const int32 F : Order) Keys->MoveKeys(F / FrameRate(), FMath::Clamp(F + Delta, 0, NumFrames() - 1) / FrameRate(), Tol);
	AfterLayerEdit();
}

void FOKAnimationEditor::DeleteKeysOf(const TArray<FName>& Bones, const TArray<int32>& Frames)
{
	UOKAnimSequence* Seq = Sequence();
	if (!Seq || Frames.IsEmpty()) return;
	const FScopedTransaction T(LOCTEXT("DeleteKeysT", "Delete keys"));
	Seq->Modify();
	for (const FName Bone : Bones)
		if (FOKBoneKeys* Keys = Seq->FindKeys(Bone))
			for (const int32 F : Frames) Keys->RemoveKeys(F / FrameRate(), 0.5f / FrameRate());
	AfterLayerEdit();
}

TArray<FName> FOKAnimationEditor::KeyedBones() const
{
	TArray<FName> Out;
	if (const UOKAnimSequence* Seq = Sequence())
	{
		for (const FName Bone : Seq->Bones) if (const_cast<UOKAnimSequence*>(Seq)->FindKeys(Bone)) Out.Add(Bone);
		for (const FOKBoneKeys& K : Seq->KeyLayer) Out.AddUnique(K.Bone);
	}
	return Out;
}

void FOKAnimationEditor::GetBoneKeyFrames(FName Bone, TArray<int32>& OutFrames) const
{
	OutFrames.Reset();
	UOKAnimSequence* Seq = Sequence();
	const FOKBoneKeys* Keys = Seq ? Seq->FindKeys(Bone) : nullptr;
	if (!Keys) return;
	TArray<float> Times;
	Keys->GetKeyTimes(Times);
	for (const float T : Times) OutFrames.AddUnique(FMath::RoundToInt(T * FrameRate()));
}

void FOKAnimationEditor::SelectBoneByName(FName Bone, bool bToggle)
{
	const int32 H = Binding.FamilyName.IndexOfByKey(Bone);
	if (H != INDEX_NONE) SelectBone(H, bToggle);
}

void FOKAnimationEditor::DeleteKeysAtCurrent() { DeleteKeys({ CurrentFrame() }); }

// ------------------------------------------------------------------------------------------------ selection / editing

bool FOKAnimationEditor::CanAnimate() const { return Sequence() && Binding.IsValid() && Player.IsValid() && HasPreviewPose(); }

void FOKAnimationEditor::SelectBone(int32 H, bool bToggle)
{
	if (!Binding.BoneIndex.IsValidIndex(H)) return;
	if (bToggle) { if (Selection.Contains(H)) Selection.Remove(H); else Selection.Add(H); }
	else Selection = { H };
	RefreshCurves();
	if (OutlineView)
	{
		const FString Name = Binding.FamilyName[H].ToString();
		for (const TSharedPtr<FString>& Item : Outline)
			if (Item->TrimStartAndEnd() == Name) { OutlineView->SetSelection(Item, ESelectInfo::Direct); OutlineView->RequestScrollIntoView(Item); break; }
	}
}

void FOKAnimationEditor::ClearSelection() { Selection.Reset(); RefreshCurves(); }

void FOKAnimationEditor::PickWalk(bool bUp)
{
	if (Selection.IsEmpty()) return;
	const int32 H = Selection.Last();
	if (bUp) { if (Binding.MappedParent[H] != INDEX_NONE) SelectBone(Binding.MappedParent[H], false); return; }
	for (int32 C = 0; C < Binding.MappedParent.Num(); ++C) if (Binding.MappedParent[C] == H) { SelectBone(C, false); return; }
}

void FOKAnimationEditor::BeginEdit(const FText& Description)
{
	if (bEditing || !Sequence() || !GEditor) return;
	GEditor->BeginTransaction(Description);
	Sequence()->Modify();
	bEditing = true;
}

void FOKAnimationEditor::EndEdit()
{
	if (!bEditing) return;
	bEditing = false;
	if (GEditor) GEditor->EndTransaction();
	LastSignature = LayerSignature();
}

void FOKAnimationEditor::SetAutoKey(bool bOn)
{
	if (bOn == bAutoKey) return;
	RevertPending();
	bAutoKey = bOn;
}

void FOKAnimationEditor::Remember(FName Bone)
{
	if (bAutoKey || Pending.Contains(Bone)) return;
	const FOKBoneKeys* Keys = Sequence()->FindKeys(Bone);
	FOKBoneKeys Backup;
	Backup.Bone = Bone;
	Pending.Add(Bone, Keys ? *Keys : Backup);
}

void FOKAnimationEditor::RevertPending()
{
	UOKAnimSequence* Seq = Sequence();
	if (!Seq || Pending.IsEmpty()) return;
	for (const TPair<FName, FOKBoneKeys>& P : Pending) Seq->FindOrAddKeys(P.Key) = P.Value;
	Pending.Reset();
	AfterLayerEdit();
}

uint32 FOKAnimationEditor::LayerSignature() const
{
	const UOKAnimSequence* Seq = Sequence();
	if (!Seq) return 0;
	uint32 H = GetTypeHash(Seq->KeyLayer.Num());
	for (const FOKBoneKeys& K : Seq->KeyLayer)
	{
		H = HashCombine(H, GetTypeHash(K.Bone));
		for (int32 C = 0; C < FOKBoneKeys::NumChannels; ++C)
			for (auto It = K.Channel(C).GetKeyIterator(); It; ++It)
				H = HashCombine(H, HashCombine(GetTypeHash(It->Time), HashCombine(GetTypeHash(It->Value), HashCombine(GetTypeHash(It->ArriveTangent),
					HashCombine(GetTypeHash(It->LeaveTangent), GetTypeHash(int32(It->InterpMode)))))));
	}
	return H;
}

uint32 FOKAnimationEditor::CurveShape() const
{
	const UOKAnimSequence* Seq = Sequence();
	uint32 H = GetTypeHash(Seq ? Seq->KeyLayer.Num() : -1);
	if (Seq) H = HashCombine(H, GetTypeHash(PTRINT(Seq->KeyLayer.GetData())));
	for (const int32 S : Selection)
	{
		H = HashCombine(H, GetTypeHash(S));
		if (const FOKBoneKeys* K = Seq ? const_cast<UOKAnimSequence*>(Seq)->FindKeys(Binding.FamilyName[S]) : nullptr)
			for (int32 C = 0; C < FOKBoneKeys::NumChannels; ++C) H = HashCombine(H, GetTypeHash(K->Channel(C).GetNumKeys() > 0));
	}
	return H;
}

void FOKAnimationEditor::RefreshCurves()
{
	LastCurveShape = CurveShape();
	if (!CurveEditor) return;
	CurveEditor->RemoveAllCurves();
	UOKAnimSequence* Seq = Sequence();
	if (!Seq) return;
	static const FLinearColor Colors[] = { FLinearColor(1.f, 0.25f, 0.25f), FLinearColor(0.3f, 1.f, 0.3f), FLinearColor(0.35f, 0.55f, 1.f),
		FLinearColor(1.f, 0.6f, 0.6f), FLinearColor(0.6f, 1.f, 0.6f), FLinearColor(0.6f, 0.75f, 1.f) };
	TArray<FCurveModelID> Ids;
	for (const int32 S : Selection)
	{
		FOKBoneKeys* Keys = Seq->FindKeys(Binding.FamilyName[S]);
		if (!Keys) continue;
		for (int32 C = 0; C < FOKBoneKeys::NumChannels; ++C)
		{
			if (!Keys->Channel(C).GetNumKeys()) continue;
			TUniquePtr<FRichCurveEditorModelRaw> Model = MakeUnique<FRichCurveEditorModelRaw>(&Keys->Channel(C), Seq);
			Model->SetShortDisplayName(FOKBoneKeys::ChannelName(C));
			Model->SetLongDisplayName(FText::Format(LOCTEXT("CurveName", "{0}.{1}"), FText::FromName(Keys->Bone), FOKBoneKeys::ChannelName(C)));
			Model->SetColor(Colors[C]);
			const FCurveModelID Id = CurveEditor->AddCurve(MoveTemp(Model));
			CurveEditor->PinCurve(Id);
			Ids.Add(Id);
		}
	}
	if (Ids.Num()) CurveEditor->ZoomToFitCurves(Ids);
}

void FOKAnimationEditor::AfterLayerEdit()
{
	UOKAnimSequence* Seq = Sequence();
	if (!Seq) return;
	FString Error;
	if (!Seq->Rebake(Error)) Status = Error;
	Seq->MarkPackageDirty();
	LastSignature = LayerSignature();
	if (CurveShape() != LastCurveShape) RefreshCurves();
	RefreshPlayer();
}

TArray<int32> FOKAnimationEditor::RootsOfSelection() const
{
	// A selected bone under another selected bone already turns with it.
	TArray<int32> Out;
	for (const int32 H : Selection)
	{
		bool bUnder = false;
		for (int32 P = Binding.MappedParent[H]; P != INDEX_NONE && !bUnder; P = Binding.MappedParent[P]) bUnder = Selection.Contains(P);
		if (!bUnder) Out.Add(H);
	}
	return Out;
}

void FOKAnimationEditor::ApplyRotation(int32 H, const FQuat& WorldDelta)
{
	// The animation's own world rotation of the bone: S = (posed preview world) x (aligned preview rest)^-1 x (source rest).
	// A world-space turn G of the bone is the local change S^-1 G S after the current local rotation (see OK_Animation_Plan 0.5).
	UOKAnimSequence* Seq = Sequence();
	FString Error;
	const TSharedPtr<const FOKAnimTracks> Base = Seq->GetBaseTracks(Error);
	const int32 K = Base ? Base->Find(Binding.FamilyName[H]) : INDEX_NONE;
	if (K == INDEX_NONE) { Status = FString::Printf(TEXT("(%s is not animated by this animation)"), *Binding.FamilyName[H].ToString()); return; }
	const FQuat W = FOKUnrealSkeleton::ToOk(BoneTransform(H).GetRotation());
	const FQuat S = W * Binding.AlignedRestWorld[H].Inverse() * Base->RestWorld[K];
	const FQuat M = (S.Inverse() * FOKUnrealSkeleton::ToOk(WorldDelta) * S).GetNormalized();
	Remember(Binding.FamilyName[H]);
	FOKBoneKeys& Keys = Seq->FindOrAddKeys(Binding.FamilyName[H]);
	Keys.SetRotationKey(Time, (Keys.EvalRotation(Time) * M).GetNormalized());
}

void FOKAnimationEditor::ApplyHipsTranslation(const FVector& WorldDelta)
{
	// Hips offset (hips heights, source hips-parent rest frame): D = W'_parent x (R'_parent^-1 A_parent) x dv x length.
	UOKAnimSequence* Seq = Sequence();
	FString Error;
	const TSharedPtr<const FOKAnimTracks> Base = Seq->GetBaseTracks(Error);
	if (!Base) return;
	const int32 H = Binding.Hips;
	const int32 A = Binding.MappedParent[H];
	const FQuat ParentPosed = A == INDEX_NONE ? FQuat::Identity : FOKUnrealSkeleton::ToOk(BoneTransform(A).GetRotation());
	const FQuat ParentRest = A == INDEX_NONE ? FQuat::Identity : Binding.AlignedRestWorld[A];
	const FVector D = FOKUnrealSkeleton::ToOkPosition(WorldDelta);
	const FVector Dv = (Base->HipsParentRestWorld.Inverse() * ParentRest * ParentPosed.Inverse()).RotateVector(D) / Binding.HipsLength;
	Remember(Binding.FamilyName[H]);
	FOKBoneKeys& Keys = Seq->FindOrAddKeys(Binding.FamilyName[H]);
	Keys.SetPositionKey(Time, Keys.EvalPosition(Time) + Dv);
}

void FOKAnimationEditor::ApplyLimbIK(int32 H, const FVector& WorldDelta)
{
	// Two-bone IK on (grandparent, parent, bone): bend the middle joint to the reach, then swing the upper bone to the target.
	const int32 Mid = Binding.MappedParent[H];
	const int32 Up = Mid == INDEX_NONE ? INDEX_NONE : Binding.MappedParent[Mid];
	if (Up == INDEX_NONE) return;
	const FVector G = BoneTransform(Up).GetLocation(), P = BoneTransform(Mid).GetLocation(), E = BoneTransform(H).GetLocation();
	const FVector Target = E + WorldDelta;
	const float La = (P - G).Size(), Lb = (E - P).Size();
	if (La < 1.e-3f || Lb < 1.e-3f) return;
	const float C = FMath::Clamp((Target - G).Size(), FMath::Abs(La - Lb) + 1.e-3f, La + Lb - 1.e-3f);
	const FVector DirGP = (G - P).GetSafeNormal(), DirEP = (E - P).GetSafeNormal();
	FVector N = FVector::CrossProduct(DirGP, DirEP);
	if (N.SizeSquared() < 1.e-8) N = FVector::CrossProduct(DirGP, (Target - G).GetSafeNormal());
	if (N.SizeSquared() < 1.e-8) N = FVector::CrossProduct(DirGP, FVector::UpVector);
	N.Normalize();
	const float Angle = FMath::Acos(FMath::Clamp((La * La + Lb * Lb - C * C) / (2.f * La * Lb), -1.f, 1.f));
	const FVector NewDirEP = FQuat(N, Angle).RotateVector(DirGP);
	const FQuat MidDelta = FQuat::FindBetweenNormals(DirEP, NewDirEP);
	const FVector NewE = P + NewDirEP * Lb;
	const FQuat UpDelta = FQuat::FindBetweenVectors(NewE - G, Target - G);
	ApplyRotation(Mid, MidDelta);
	AfterLayerEdit();
	ApplyRotation(Up, UpDelta);
}

void FOKAnimationEditor::SnapToFrame()
{
	// Keys go on frames: an edit made between frames (after playing) lands on the nearest one.
	bPlaying = false;
	const float Snapped = CurrentFrame() / FrameRate();
	if (FMath::Abs(Snapped - Time) > 1.e-5f) { Time = Snapped; UpdatePose(); }
}

void FOKAnimationEditor::RotateSelection(const FQuat& WorldDelta)
{
	if (!CanAnimate()) return;
	SnapToFrame();
	for (const int32 H : RootsOfSelection()) ApplyRotation(H, WorldDelta);
	AfterLayerEdit();
}

void FOKAnimationEditor::TranslateSelection(const FVector& WorldDelta)
{
	if (!CanAnimate()) return;
	SnapToFrame();
	for (const int32 H : RootsOfSelection())
	{
		if (H == Binding.Hips) ApplyHipsTranslation(WorldDelta);
		else ApplyLimbIK(H, WorldDelta);
		AfterLayerEdit();
	}
}

void FOKAnimationEditor::KeySelection(bool bRotation, bool bPosition)
{
	UOKAnimSequence* Seq = Sequence();
	if (!CanAnimate() || Selection.IsEmpty()) return;
	SnapToFrame();
	const FScopedTransaction T(LOCTEXT("SetKey", "Set key"));
	Seq->Modify();
	for (const int32 H : Selection)
	{
		FOKBoneKeys& Keys = Seq->FindOrAddKeys(Binding.FamilyName[H]);
		const bool bHips = H == Binding.Hips;
		if (bRotation) Keys.KeyCurrent(Time, false);
		if (bPosition && bHips) Keys.SetPositionKey(Time, Keys.EvalPosition(Time));
		if (!bRotation && !(bPosition && bHips)) continue;
		Pending.Remove(Keys.Bone);
	}
	AfterLayerEdit();
}

void FOKAnimationEditor::KeyAllBones()
{
	UOKAnimSequence* Seq = Sequence();
	if (!CanAnimate()) return;
	SnapToFrame();
	const FScopedTransaction T(LOCTEXT("KeyAllT", "Key all bones"));
	Seq->Modify();
	for (int32 H = 0; H < Binding.FamilyName.Num(); ++H)
	{
		if (!Seq->Bones.Contains(Binding.FamilyName[H])) continue;
		FOKBoneKeys& Keys = Seq->FindOrAddKeys(Binding.FamilyName[H]);
		Keys.KeyCurrent(Time, H == Binding.Hips);
	}
	Pending.Reset();
	AfterLayerEdit();
}

bool FOKAnimationEditor::HasKeyAtCurrentFrame(int32 H) const
{
	UOKAnimSequence* Seq = Sequence();
	const FOKBoneKeys* Keys = Seq && Binding.FamilyName.IsValidIndex(H) ? Seq->FindKeys(Binding.FamilyName[H]) : nullptr;
	return Keys && Keys->HasKey(CurrentFrame() / FrameRate(), 0.5f / FrameRate());
}

void FOKAnimationEditor::CopyPose()
{
	// The final pose (base x layer) of each bone, so it pastes the same on any frame.
	UOKAnimSequence* Seq = Sequence();
	FString Error;
	const TSharedPtr<const FOKAnimTracks> Base = Seq ? Seq->GetBaseTracks(Error) : nullptr;
	if (!Base || !CanAnimate()) return;
	Clipboard.Reset();
	const int32 F = FMath::Clamp(CurrentFrame(), 0, Base->Frames - 1);
	const int32 N = Base->Bones.Num();
	for (int32 H = 0; H < Binding.FamilyName.Num(); ++H)
	{
		if (Selection.Num() && !Selection.Contains(H)) continue;
		const int32 K = Base->Find(Binding.FamilyName[H]);
		if (K == INDEX_NONE) continue;
		const FOKBoneKeys* Keys = Seq->FindKeys(Binding.FamilyName[H]);
		const FQuat Rot = Base->Rot[F * N + K] * (Keys ? Keys->EvalRotation(Time) : FQuat::Identity);
		const FVector Hips = Base->Hips[F] + (Keys ? Keys->EvalPosition(Time) : FVector::ZeroVector);
		Clipboard.Add(Binding.FamilyName[H], { Rot, Hips });
	}
	Status = FString::Printf(TEXT("(copied %d bones)"), Clipboard.Num());
}

void FOKAnimationEditor::PastePose()
{
	UOKAnimSequence* Seq = Sequence();
	FString Error;
	const TSharedPtr<const FOKAnimTracks> Base = Seq ? Seq->GetBaseTracks(Error) : nullptr;
	if (!Base || !CanAnimate() || Clipboard.IsEmpty()) return;
	SnapToFrame();
	const FScopedTransaction T(LOCTEXT("PastePoseT", "Paste pose"));
	Seq->Modify();
	const int32 F = FMath::Clamp(CurrentFrame(), 0, Base->Frames - 1);
	const int32 N = Base->Bones.Num();
	for (const TPair<FName, TPair<FQuat, FVector>>& P : Clipboard)
	{
		const int32 K = Base->Find(P.Key);
		const int32 H = Binding.FamilyName.IndexOfByKey(P.Key);
		if (K == INDEX_NONE || (Selection.Num() && !Selection.Contains(H))) continue;
		FOKBoneKeys& Keys = Seq->FindOrAddKeys(P.Key);
		Keys.SetRotationKey(Time, (Base->Rot[F * N + K].Inverse() * P.Value.Key).GetNormalized());
		if (P.Key == Base->HipsBone) Keys.SetPositionKey(Time, P.Value.Value - Base->Hips[F]);
		Pending.Remove(P.Key);
	}
	AfterLayerEdit();
}

// ------------------------------------------------------------------------------------------------ channel box

FText FOKAnimationEditor::PrimaryBoneName() const
{
	if (!Sequence()) return LOCTEXT("NotSequence", "Animate mode edits OK animation sequences");
	if (!CanAnimate()) return LOCTEXT("NoPreview", "Set a preview mesh to animate");
	return Selection.Num() ? FText::FromName(Binding.FamilyName[Selection.Last()]) : LOCTEXT("NoSelection", "Select a bone");
}

bool FOKAnimationEditor::PrimaryIsHips() const { return Selection.Num() && Selection.Last() == Binding.Hips; }

float FOKAnimationEditor::GetChannel(int32 Ch) const
{
	UOKAnimSequence* Seq = Sequence();
	const FOKBoneKeys* Keys = Seq && Selection.Num() ? Seq->FindKeys(Binding.FamilyName[Selection.Last()]) : nullptr;
	return Keys ? Keys->Channel(Ch).Eval(Time) : 0.f;
}

void FOKAnimationEditor::SetChannel(int32 Ch, float Value)
{
	UOKAnimSequence* Seq = Sequence();
	if (!Seq || Selection.IsEmpty() || !CanAnimate()) return;
	const FName Bone = Binding.FamilyName[Selection.Last()];
	SnapToFrame();
	Remember(Bone);
	Seq->FindOrAddKeys(Bone).SetChannelKey(Ch, Time, Value);
	AfterLayerEdit();
}

bool FOKAnimationEditor::ChannelKeyedNow(int32 Ch) const
{
	UOKAnimSequence* Seq = Sequence();
	const FOKBoneKeys* Keys = Seq && Selection.Num() ? Seq->FindKeys(Binding.FamilyName[Selection.Last()]) : nullptr;
	if (!Keys) return false;
	for (auto It = Keys->Channel(Ch).GetKeyIterator(); It; ++It) if (FMath::Abs(It->Time - Time) <= 0.5f / FrameRate()) return true;
	return false;
}

// ------------------------------------------------------------------------------------------------ blend space

UOKBlendSpace* FOKAnimationEditor::BlendSpace() const { return Cast<UOKBlendSpace>(Asset); }
FVector2D FOKAnimationEditor::GetBlendParams() const { return FVector2D(Settings->X, Settings->Y); }

void FOKAnimationEditor::SetBlendParams(const FVector2D& Params)
{
	Settings->X = float(Params.X);
	Settings->Y = float(Params.Y);
	UpdatePose();
}

void FOKAnimationEditor::MoveBlendSample(int32 Sample, const FVector2D& Position, bool bFinal)
{
	UOKBlendSpace* Blend = BlendSpace();
	if (!Blend || !Blend->Samples.IsValidIndex(Sample)) return;
	if (!SampleDragOrigin.IsSet()) SampleDragOrigin = Blend->Samples[Sample].Position;
	if (bFinal)
	{
		// Undo goes back to where the drag started.
		Blend->Samples[Sample].Position = SampleDragOrigin.GetValue();
		SampleDragOrigin.Reset();
		const FScopedTransaction T(LOCTEXT("MoveSample", "Move blend sample"));
		Blend->Modify();
		Blend->Samples[Sample].Position = Position;
		Blend->MarkPackageDirty();
		RefreshPlayer();
		RefreshOutline();
		RunValidation();
		return;
	}
	Blend->Samples[Sample].Position = Position;
	RefreshPlayer();
}

// ------------------------------------------------------------------------------------------------ Maya tools

namespace
{
	/** Opposite-side bone name (_L/_R, _l/_r, .L/.R, Left/Right); NAME_None for a centre bone. */
	FName MirrorName(FName Bone)
	{
		const FString S = Bone.ToString();
		static const TPair<const TCHAR*, const TCHAR*> Pairs[] = { { TEXT("_L"), TEXT("_R") }, { TEXT("_l"), TEXT("_r") }, { TEXT(".L"), TEXT(".R") },
			{ TEXT("_L_"), TEXT("_R_") }, { TEXT("Left"), TEXT("Right") }, { TEXT("left"), TEXT("right") } };
		for (const auto& P : Pairs)
		{
			for (int32 Dir = 0; Dir < 2; ++Dir)
			{
				const FString From = Dir ? P.Value : P.Key, To = Dir ? P.Key : P.Value;
				const int32 At = S.Find(From, ESearchCase::CaseSensitive, ESearchDir::FromEnd);
				if (At == INDEX_NONE) continue;
				// A suffix, or followed by another separator / digit (cf_j_siri_L_01).
				const int32 After = At + From.Len();
				if (After == S.Len() || !FChar::IsAlpha(S[After])) return FName(S.Left(At) + To + S.Mid(After));
			}
		}
		return NAME_None;
	}

	/** Reflection across the character's sagittal plane (x -> -x in OK space) of a world rotation. */
	FQuat MirrorRotation(const FQuat& Q) { return FQuat(Q.X, -Q.Y, -Q.Z, Q.W); }
}

bool FOKAnimationEditor::PosePositionsAt(float Seconds, TArray<FVector>& Out) const
{
	Out.Reset();
	TArray<float> Sample;
	if (!Player || !Binding.IsValid() || !Player->SampleOnto(Settings->Role, Binding.HeaderJson, Seconds, Sample) || Sample.Num() != Binding.BoneIndex.Num() * 4 + 3) return false;
	TArray<FTransform> Local = Binding.Ref.GetRefBonePose();
	Binding.LocalsToPose(Sample.GetData(), Local);
	TArray<FTransform> Comp;
	Comp.SetNum(Local.Num());
	for (int32 I = 0; I < Local.Num(); ++I)
	{
		const int32 P = Binding.Ref.GetParentIndex(I);
		Comp[I] = P == INDEX_NONE ? Local[I] : Local[I] * Comp[P];
	}
	for (const int32 B : Binding.BoneIndex) Out.Add(Comp[B].GetLocation());
	return true;
}

const TArray<FVector>& FOKAnimationEditor::GetTrail(int32 Bone)
{
	const uint32 Key = HashCombine(HashCombine(LastSignature, GetTypeHash(Bone)), GetTypeHash(Player.Get()));
	if (Key == TrailKey) return Trail;
	TrailKey = Key;
	Trail.Reset();
	TArray<FVector> Pose;
	for (int32 F = 0; F < NumFrames(); ++F)
		if (PosePositionsAt(F / FrameRate(), Pose) && Pose.IsValidIndex(Bone)) Trail.Add(Pose[Bone]);
	return Trail;
}

UOKPoseLibrary* FOKAnimationEditor::PoseLibrary()
{
	if (!Settings->PoseLibrary) Settings->PoseLibrary = NewObject<UOKPoseLibrary>(GetTransientPackage(), NAME_None, RF_Transactional);
	return Settings->PoseLibrary;
}

bool FOKAnimationEditor::StorePose(FName Name)
{
	UOKPoseLibrary* Lib = PoseLibrary();
	if (!Lib || Name.IsNone() || !CanAnimate()) return false;
	CopyPose();   // the final pose of the selection (or everything) into the clipboard
	if (Clipboard.IsEmpty()) return false;
	const FScopedTransaction T(LOCTEXT("StorePoseT", "Store pose"));
	Lib->Modify();
	FOKPose Pose;
	Pose.Name = Name;
	const FName HipsBone = Binding.FamilyName.IsValidIndex(Binding.Hips) ? Binding.FamilyName[Binding.Hips] : NAME_None;
	for (const TPair<FName, TPair<FQuat, FVector>>& P : Clipboard)
	{
		Pose.Bones.Add(P.Key);
		Pose.Rotations.Add(P.Value.Key);
		if (P.Key == HipsBone) { Pose.bHasHips = true; Pose.Hips = P.Value.Value; }
	}
	const int32 Existing = Lib->FindPose(Name);
	if (Existing != INDEX_NONE) Lib->Poses[Existing] = Pose; else Lib->Poses.Add(Pose);
	Status = FString::Printf(TEXT("(stored pose %s: %d bones)"), *Name.ToString(), Pose.Bones.Num());
	return true;
}

bool FOKAnimationEditor::ApplyPose(FName Name, bool bSelectedOnly, float Weight)
{
	UOKPoseLibrary* Lib = PoseLibrary();
	const int32 I = Lib ? Lib->FindPose(Name) : INDEX_NONE;
	if (I == INDEX_NONE || !CanAnimate()) return false;
	const FOKPose& Pose = Lib->Poses[I];
	// Blend from the current final pose by the weight, then paste (which keys the bones; the selection limits it).
	const TArray<int32> KeepSelection = Selection;
	if (!bSelectedOnly) Selection.Reset();
	CopyPose();
	const TMap<FName, TPair<FQuat, FVector>> Current = Clipboard;
	Clipboard.Reset();
	const float W = FMath::Clamp(Weight, 0.f, 1.f);
	for (int32 B = 0; B < Pose.Bones.Num(); ++B)
	{
		const TPair<FQuat, FVector>* Now = Current.Find(Pose.Bones[B]);
		if (!Now) continue;
		const FQuat Rot = FQuat::Slerp(Now->Key, Pose.Rotations[B], W).GetNormalized();
		const FVector Hips = Pose.bHasHips ? FMath::Lerp(Now->Value, Pose.Hips, W) : Now->Value;
		Clipboard.Add(Pose.Bones[B], { Rot, Hips });
	}
	PastePose();
	Selection = KeepSelection;
	Status = FString::Printf(TEXT("(applied pose %s: %d bones, weight %.2f)"), *Name.ToString(), Clipboard.Num(), W);
	return Clipboard.Num() > 0;
}

UOKSkeletonFamily* FOKAnimationEditor::FamilyForTab() const
{
	if (EditedFamily.IsValid()) return EditedFamily.Get();
	return UOKAnimationLibrary::GetDefaultFamily();
}

bool FOKAnimationEditor::AddIKChainFromSelection(FName Name)
{
	UOKSkeletonFamily* Family = FamilyForTab();
	if (!Family || Selection.IsEmpty() || !Binding.IsValid()) return false;
	FOKIKChain Chain;
	if (Selection.Num() >= 3)
	{
		Chain.Root = Binding.FamilyName[Selection[0]];
		Chain.Mid = Binding.FamilyName[Selection[1]];
		Chain.End = Binding.FamilyName[Selection[2]];
	}
	else
	{
		const int32 End = Selection.Last();
		const int32 Mid = Binding.MappedParent[End];
		const int32 Root = Mid != INDEX_NONE ? Binding.MappedParent[Mid] : INDEX_NONE;
		if (Root == INDEX_NONE) return false;
		Chain.Root = Binding.FamilyName[Root];
		Chain.Mid = Binding.FamilyName[Mid];
		Chain.End = Binding.FamilyName[End];
	}
	Chain.Name = Name.IsNone() ? Chain.End : Name;
	const FScopedTransaction T(LOCTEXT("AddChainT", "Add IK chain"));
	Family->Modify();
	Family->IKChains.RemoveAll([&](const FOKIKChain& C) { return C.Name == Chain.Name; });
	Family->IKChains.Add(Chain);
	return true;
}

int32 FOKAnimationEditor::LoadCardPreviews(FString& OutError)
{
	for (TWeakObjectPtr<AKKPreviewActor>& A : CardActors) if (A.IsValid()) A->Destroy();
	CardActors.Reset();
	UWorld* World = Scene ? Scene->GetWorld() : nullptr;
	if (!World) { OutError = TEXT("no preview world"); return 0; }
	for (int32 I = 0; I < Settings->Cards.Num(); ++I)
	{
		const FString Card = Settings->Cards[I].FilePath;
		// Portable cards carry their resources (no game install needed); others read the configured game root.
		const FString Portable = UKKCardWorkspace::PortableSourcePath(Card);
		const UKKGameRoot* Root = Portable.IsEmpty() ? UKKGameRoot::Get() : nullptr;
		FOkSession Session;
		FString Error;
		if (!Session.Open(Root ? Root->Root : FString(), Portable.IsEmpty() ? Card : Portable, 0, Error)) { OutError += FString::Printf(TEXT("%s: %s; "), *FPaths::GetCleanFilename(Card), *Error); continue; }
		FActorSpawnParameters Params;
		Params.ObjectFlags = RF_Transient;
		AKKPreviewActor* Actor = World->SpawnActor<AKKPreviewActor>(FVector(120.f * (I + 1), 0.f, 0.f), FRotator::ZeroRotator, Params);
		if (!Actor) continue;
		Actor->SetActorTickEnabled(false);
		Actor->Session = MoveTemp(Session);
		if (!Actor->LoadSession(Error)) { OutError += FString::Printf(TEXT("%s: %s; "), *FPaths::GetCleanFilename(Card), *Error); Actor->Destroy(); continue; }
		CardActors.Add(Actor);
	}
	RefreshCardAnimation();
	return CardActors.Num();
}

void FOKAnimationEditor::RefreshCardAnimation()
{
	for (TWeakObjectPtr<AKKPreviewActor>& A : CardActors)
	{
		if (!A.IsValid() || !Asset) continue;
		FString Error;
		const TSharedPtr<FOkAnimPlayer> P = (RetargetResult ? static_cast<UOKAnimationAsset*>(RetargetResult) : Asset.Get())->CreatePlayer(Error);
		if (P && A->SetOkAnimation(P, false)) A->PoseMode = 3;
	}
}

void FOKAnimationEditor::TickCardPreviews()
{
	for (TWeakObjectPtr<AKKPreviewActor>& A : CardActors)
	{
		if (!A.IsValid() || !A->OkPlayer) continue;
		A->AnimPhase = Time;
		A->ApplyPoseFrame(0.f, false);
	}
}

int32 FOKAnimationEditor::AddContactFromSelection()
{
	UOKAnimSequence* Seq = Sequence();
	if (!Seq || Selection.IsEmpty() || !Binding.IsValid()) { Notify(LOCTEXT("ContactNeedsBones", "Select one bone (ground) or two bones (reach) first."), false); return INDEX_NONE; }
	const FScopedTransaction T(LOCTEXT("AddContactT", "Add contact"));
	Seq->Modify();
	FOKContact C;
	C.Effector = Binding.FamilyName[Selection[0]];
	const float Fps = FMath::Max(Seq->Fps, 1.f);
	C.Start = CurrentFrame() / Fps;
	C.End = FMath::Min(Seq->Duration, C.Start + 0.5f);
	if (C.End <= C.Start) C.Start = FMath::Max(0.f, C.End - 0.5f);
	C.Blend = 0.1f;
	// Positions in OK space (the authoring offsets are in hips heights).
	const float HipsHeight = FMath::Max(1.f, float(BoneTransform(Binding.Hips).GetLocation().Z)) / 100.f;
	if (Selection.Num() >= 2)
	{
		C.TargetRole = 0;
		C.TargetBone = Binding.FamilyName[Selection.Last()];
	}
	else
	{
		C.TargetRole = -1;
		C.Offset = FVector(0., BoneTransform(Selection[0]).GetLocation().Z / 100. / HipsHeight, 0.);
	}
	SelectedContact = Seq->Contacts.Add(C);
	CommitContacts();
	return SelectedContact;
}

void FOKAnimationEditor::SetContactRange(int32 Index, float Start, float End)
{
	UOKAnimSequence* Seq = Sequence();
	if (!Seq || !Seq->Contacts.IsValidIndex(Index)) return;
	Seq->Modify();
	FOKContact& C = Seq->Contacts[Index];
	C.Start = FMath::Clamp(Start, 0.f, Seq->Duration);
	C.End = FMath::Clamp(End, C.Start, Seq->Duration);
}

void FOKAnimationEditor::DeleteContact(int32 Index)
{
	UOKAnimSequence* Seq = Sequence();
	if (!Seq || !Seq->Contacts.IsValidIndex(Index)) return;
	const FScopedTransaction T(LOCTEXT("DeleteContactT", "Delete contact"));
	Seq->Modify();
	Seq->Contacts.RemoveAt(Index);
	SelectedContact = INDEX_NONE;
	CommitContacts();
}

void FOKAnimationEditor::CommitContacts()
{
	UOKAnimSequence* Seq = Sequence();
	if (!Seq) return;
	FString Error;
	if (!Seq->ApplyEdits(Error)) Notify(FText::FromString(Error), false);
	bDirty = true;
}

void FOKAnimationEditor::ApplyModifier(const FString& OpJson)
{
	UOKAnimSequence* Seq = Sequence();
	if (!Seq) return;
	const FScopedTransaction T(LOCTEXT("ModifierT", "Apply modifier"));
	Seq->Modify();
	FString Error;
	const int32 Before = Seq->Frames;
	const int32 ContactsBefore = Seq->Contacts.Num();
	if (!Seq->ApplyModifier(OpJson, Error)) { Notify(FText::FromString(Error), false); return; }
	AfterLayerEdit();
	bDirty = true;
	Notify(FText::Format(LOCTEXT("ModifierDone", "Frames {0} -> {1}, contacts {2} -> {3}"), Before, Seq->Frames, ContactsBefore, Seq->Contacts.Num()), true);
}

void FOKAnimationEditor::MirrorPose()
{
	UOKAnimSequence* Seq = Sequence();
	if (!CanAnimate()) return;
	SnapToFrame();
	// Targets (and the bone each copies): the opposite side of the selection, or every bone (sides swapped).
	TMap<int32, int32> SourceOf;
	auto Add = [&](int32 Source)
	{
		// The family's mirror pairs when it has them, else the naming rule.
		const UOKSkeletonFamily* MirrorFamily = FamilyForTab();
		const FName Explicit = MirrorFamily && MirrorFamily->MirrorPairs.Num() ? MirrorFamily->MirrorOf(Binding.FamilyName[Source]) : NAME_None;
		const FName Opposite = !Explicit.IsNone() ? Explicit : MirrorName(Binding.FamilyName[Source]);
		const int32 Target = Opposite.IsNone() ? Source : Binding.FamilyName.IndexOfByKey(Opposite);
		if (Target != INDEX_NONE) SourceOf.Add(Target, Source);
	};
	if (Selection.IsEmpty()) { for (int32 H = 0; H < Binding.BoneIndex.Num(); ++H) Add(H); }
	else for (const int32 H : Selection) Add(H);
	// Desired world rotations from the current (unmodified) pose, then each target's world change top-down:
	// a bone already turned by its ancestors' changes only needs the rest of the way (Total[b] x Total[parent]^-1).
	const int32 N = Binding.BoneIndex.Num();
	TArray<FQuat> Original, Total;
	Original.SetNum(N);
	Total.Init(FQuat::Identity, N);
	for (int32 H = 0; H < N; ++H) Original[H] = BoneTransform(H).GetRotation();
	const FScopedTransaction T(LOCTEXT("MirrorT", "Mirror pose"));
	Seq->Modify();
	FString Error;
	const TSharedPtr<const FOKAnimTracks> Base = Seq->GetBaseTracks(Error);
	for (int32 H = 0; H < N; ++H)   // header order follows the Unreal skeleton: parents first
	{
		const int32 P = Binding.MappedParent[H];
		const FQuat ParentTotal = P == INDEX_NONE ? FQuat::Identity : Total[P];
		const int32* Source = SourceOf.Find(H);
		if (!Source || !Base || Base->Find(Binding.FamilyName[H]) == INDEX_NONE) { Total[H] = ParentTotal; continue; }
		// OK-space world delta of the source, mirrored, put on this bone's own aligned rest.
		const FQuat SourceDelta = FOKUnrealSkeleton::ToOk(Original[*Source]) * Binding.AlignedRestWorld[*Source].Inverse();
		const FQuat Desired = FOKUnrealSkeleton::FromOk(MirrorRotation(SourceDelta) * Binding.AlignedRestWorld[H]);
		Total[H] = (Desired * Original[H].Inverse()).GetNormalized();
		// ApplyRotation reads the pose before any of these changes: express the change in that frame.
		ApplyRotation(H, (ParentTotal.Inverse() * Total[H]).GetNormalized());
	}
	if (SourceOf.Contains(Binding.Hips))
	{
		const FVector Hips = BoneTransform(Binding.Hips).GetLocation();
		FVector Ok = FOKUnrealSkeleton::ToOkPosition(Hips);
		Ok.X = -Ok.X;
		ApplyHipsTranslation(FOKUnrealSkeleton::FromOkPosition(Ok) - Hips);
	}
	Pending.Reset();
	AfterLayerEdit();
}

void FOKAnimationEditor::SetKeyInterpolation(ERichCurveInterpMode Mode)
{
	UOKAnimSequence* Seq = Sequence();
	if (!Seq) return;
	const FScopedTransaction T(LOCTEXT("InterpT", "Key interpolation"));
	Seq->Modify();
	const float Tol = 0.5f / FrameRate();
	for (const FName Bone : KeyedBonesForTimeline())
	{
		FOKBoneKeys* Keys = Seq->FindKeys(Bone);
		if (!Keys) continue;
		for (int32 C = 0; C < FOKBoneKeys::NumChannels; ++C)
		{
			FRichCurve& Curve = Keys->Channel(C);
			for (auto It = Curve.GetKeyHandleIterator(); It; ++It)
			{
				if (FMath::Abs(Curve.GetKeyTime(*It) - Time) > Tol) continue;
				Curve.SetKeyInterpMode(*It, Mode);
				if (Mode == RCIM_Cubic) Curve.SetKeyTangentMode(*It, RCTM_Auto);
			}
			Curve.AutoSetTangents();
		}
	}
	AfterLayerEdit();
}

void FOKAnimationEditor::SetLength(int32 Frames)
{
	UOKAnimSequence* Seq = Sequence();
	if (!Seq || Frames == Seq->Frames) return;
	const FScopedTransaction T(LOCTEXT("LengthT", "Animation length"));
	Seq->Modify();
	FString Error;
	if (!Seq->SetFrameCount(Frames, Error)) { Notify(FText::FromString(Error), false); return; }
	Time = FMath::Min(Time, (Seq->Frames - 1) / FrameRate());
	AfterLayerEdit();
	RefreshOutline();
}

// ------------------------------------------------------------------------------------------------ self test

void FOKAnimationEditor::RunSelfTest(bool bExitWhenDone)
{
	TArray<FString> Lines;
	int32 Failures = 0;
	auto Check = [&](bool bOk, const FString& What) { Lines.Add(FString::Printf(TEXT("%s %s"), bOk ? TEXT("PASS") : TEXT("FAIL"), *What)); Failures += bOk ? 0 : 1; };
	auto Find = [&](const TCHAR* Name) { return Binding.FamilyName.IndexOfByKey(FName(Name)); };
	auto AngleDeg = [](const FQuat& A, const FQuat& B) { return float(FOKUnrealSkeleton::AngleDegrees(A, B)); };
	// Multi-role: on a group, the second role's slot is edited like a sequence; the other role stands where the group puts it.
	if (const UOKGroupAnimSequence* Group = Cast<UOKGroupAnimSequence>(Asset))
	{
		SetEditedRole(1);
		UOKAnimSequence* RoleSeq = Sequence();
		const int32 Fore = Find(TEXT("cf_j_forearm01_L"));
		bool bKeyed = false;
		if (RoleSeq && CanAnimate() && Fore != INDEX_NONE)
		{
			const int32 Keys = RoleSeq->KeyLayer.Num();
			SetFrame(3);
			SetAutoKey(true);
			SelectBone(Fore, false);
			BeginEdit(LOCTEXT("TestRoleKey", "Test role key"));
			RotateSelection(FQuat(FVector(0, 0, 1), FMath::DegreesToRadians(20.f)));
			EndEdit();
			bKeyed = RoleSeq->KeyLayer.Num() == Keys + 1 && RoleSeq == Cast<UOKAnimSequence>(Group->Roles[1].Animation);
		}
		TArray<FVector> R0, R1;
		const bool bBoth = RoleBonePositions(0, R0) && RoleBonePositions(1, R1) && R0.Num() == R1.Num();
		Check(bKeyed && bBoth && RoleMeshes.Num() == Group->Roles.Num() - 1, FString::Printf(TEXT("multi-role: edit role 1 of %d (keyed=%d), both roles posed (%d bones)"), Group->Roles.Num(), bKeyed, R1.Num()));
		// Height of the other role: its preview scales.
		Settings->RoleHeight = { 1.2f, 1.f };
		UpdatePose();
		Check(RoleMeshes.Num() && FMath::IsNearlyEqual(RoleMeshes[0]->GetRelativeScale3D().X, 1.2, 0.001), FString::Printf(TEXT("multi-role: role height scale %.2f"), RoleMeshes.Num() ? RoleMeshes[0]->GetRelativeScale3D().X : 0.));
		Settings->RoleHeight.Reset();
		ClearSelection();
		SetEditedRole(0);
	}
	UOKAnimSequence* Seq = Sequence();
	Check(Seq && CanAnimate(), TEXT("animate mode available (sequence, preview mesh, binding, pose)"));
	if (Seq && CanAnimate())
	{
		SetAutoKey(true);
		SetFrame(5);
		// 1. Gizmo rotation of a forearm: the bone turns by exactly the world delta, and a key appears.
		const int32 Fore = Find(TEXT("cf_j_forearm01_L"));
		Check(Fore != INDEX_NONE, TEXT("forearm mapped"));
		if (Fore != INDEX_NONE)
		{
			SelectBone(Fore, false);
			const FQuat Before = BoneTransform(Fore).GetRotation();
			const FQuat G(FVector(0, 0, 1), FMath::DegreesToRadians(30.f));
			BeginEdit(LOCTEXT("TestRotate", "Test rotate"));
			RotateSelection(G);
			EndEdit();
			const FQuat After = BoneTransform(Fore).GetRotation();
			Check(AngleDeg(After, G * Before) < 0.05f, FString::Printf(TEXT("rotate: world delta reproduced (error %.3f deg)"), AngleDeg(After, G * Before)));
			Check(HasKeyAtCurrentFrame(Fore) && Seq->KeyLayer.Num() == 1, TEXT("auto key at the current frame"));
			// 2. Channel box: typing a value keys that channel.
			BeginEdit(LOCTEXT("TestChannel", "Test channel"));
			SetChannel(0, 45.f);
			EndEdit();
			Check(FMath::IsNearlyEqual(GetChannel(0), 45.f, 0.01f), FString::Printf(TEXT("channel box key (RotX=%.2f)"), GetChannel(0)));
		}
		// 3. Hips translation follows the gizmo.
		const int32 Hips = Binding.Hips;
		SetFrame(10);
		SelectBone(Hips, false);
		{
			const FVector Before = BoneTransform(Hips).GetLocation();
			const FVector D(0.f, 0.f, 20.f);
			BeginEdit(LOCTEXT("TestMove", "Test move"));
			TranslateSelection(D);
			EndEdit();
			const FVector After = BoneTransform(Hips).GetLocation();
			Check((After - (Before + D)).Size() < 0.5f, FString::Printf(TEXT("hips translate (error %.3f cm)"), (After - (Before + D)).Size()));
		}
		// 4. Two-bone IK on a hand, then undo.
		const int32 Hand = Find(TEXT("cf_j_hand_L"));
		if (Hand != INDEX_NONE)
		{
			SetFrame(15);
			SelectBone(Hand, false);
			const FVector Before = BoneTransform(Hand).GetLocation();
			const FVector D(8.f, 0.f, 6.f);
			BeginEdit(LOCTEXT("TestIK", "Test IK"));
			TranslateSelection(D);
			EndEdit();
			const FVector After = BoneTransform(Hand).GetLocation();
			Check((After - (Before + D)).Size() < 1.f, FString::Printf(TEXT("hand IK reaches the target (error %.3f cm)"), (After - (Before + D)).Size()));
			GEditor->UndoTransaction();
			Tick(0.f);
			const FVector Undone = BoneTransform(Hand).GetLocation();
			Check((Undone - Before).Size() < 0.5f, FString::Printf(TEXT("undo restores the pose (error %.3f cm)"), (Undone - Before).Size()));
		}
		// 5. Timeline: move the hips key from frame 10 to 20.
		{
			SelectBone(Hips, false);
			MoveKeys({ 10 }, 10);
			TArray<int32> Keys;
			GetTimelineKeys(Keys);
			Check(Keys.Contains(20) && !Keys.Contains(10), FString::Printf(TEXT("move keys on the timeline (%s)"), *FString::JoinBy(Keys, TEXT(","), [](int32 F) { return FString::FromInt(F); })));
		}
		// 6. Copy the forearm pose at frame 5, paste at frame 25.
		if (Fore != INDEX_NONE)
		{
			SelectBone(Fore, false);
			SetFrame(5);
			const FQuat Local5 = BoneTransform(Binding.MappedParent[Fore]).GetRotation().Inverse() * BoneTransform(Fore).GetRotation();
			CopyPose();
			SetFrame(25);
			PastePose();
			const FQuat Local25 = BoneTransform(Binding.MappedParent[Fore]).GetRotation().Inverse() * BoneTransform(Fore).GetRotation();
			Check(AngleDeg(Local5, Local25) < 0.05f, FString::Printf(TEXT("copy / paste pose (error %.3f deg)"), AngleDeg(Local5, Local25)));
		}
		// 8. Length: longer then back.
		{
			const int32 N = NumFrames();
			SetLength(N + 10);
			const int32 Longer = NumFrames();
			SetLength(N);
			Check(Longer == N + 10 && NumFrames() == N, FString::Printf(TEXT("animation length %d -> %d -> %d"), N, Longer, NumFrames()));
		}
		// 9. Stepped keys hold their value until the next key.
		if (Fore != INDEX_NONE)
		{
			SelectBone(Fore, false);
			SetFrame(30);
			BeginEdit(LOCTEXT("TestStep", "Test step"));
			RotateSelection(FQuat(FVector(1, 0, 0), FMath::DegreesToRadians(20.f)));
			EndEdit();
			SetKeyInterpolation(RCIM_Constant);
			const FOKBoneKeys* Keys = Seq->FindKeys(Binding.FamilyName[Fore]);
			bool bStepped = false;
			if (Keys) for (auto It = Keys->RotX.GetKeyIterator(); It; ++It) if (FMath::Abs(It->Time - Time) < 0.01f) bStepped = It->InterpMode == RCIM_Constant;
			Check(bStepped, TEXT("stepped key interpolation"));
		}
		// 10. Mirror: the right forearm takes the mirrored world turn of the left one.
		{
			const int32 L = Find(TEXT("cf_j_forearm01_L")), R = Find(TEXT("cf_j_forearm01_R"));
			if (L != INDEX_NONE && R != INDEX_NONE)
			{
				SetFrame(30);
				SelectBone(L, false);
				MirrorPose();
				const FQuat DeltaL = FOKUnrealSkeleton::ToOk(BoneTransform(L).GetRotation()) * Binding.AlignedRestWorld[L].Inverse();
				const FQuat DeltaR = FOKUnrealSkeleton::ToOk(BoneTransform(R).GetRotation()) * Binding.AlignedRestWorld[R].Inverse();
				const float Err = AngleDeg(MirrorRotation(DeltaL), DeltaR);
				Check(Err < 0.05f, FString::Printf(TEXT("mirror pose left -> right (error %.4f deg)"), Err));
				// Whole-pose mirror twice gives the pose back.
				ClearSelection();
				const FQuat Before = BoneTransform(L).GetRotation();
				MirrorPose();
				MirrorPose();
				Check(AngleDeg(Before, BoneTransform(L).GetRotation()) < 0.05f, FString::Printf(TEXT("mirror whole pose twice = identity (%.4f deg)"), AngleDeg(Before, BoneTransform(L).GetRotation())));
				SelectBone(L, false);
			}
		}
		// 11. Contacts: add a ground contact on the left foot, move it, then a reach contact, delete it.
		{
			const int32 Foot = Find(TEXT("cf_j_leg03_L")), HandL = Find(TEXT("cf_j_hand_L")), Hips2 = Binding.Hips;
			if (Foot != INDEX_NONE && HandL != INDEX_NONE)
			{
				const int32 Before = Seq->Contacts.Num();
				SetFrame(6);
				SelectBone(Foot, false);
				const int32 Ground = AddContactFromSelection();
				const bool bAdded = Ground != INDEX_NONE && Seq->Contacts.Num() == Before + 1 && Seq->Contacts[Ground].TargetRole == -1 && Seq->Contacts[Ground].Offset.Y > 0.;
				SetContactRange(Ground, 0.4f, 0.8f);
				CommitContacts();
				// The data carries it: reload the bytes and look.
				FString Info;
				FOkAnimBridge::Info(Seq->Data, Info);
				const bool bStored = Info.Contains(TEXT("\"effector\":\"cf_j_leg03_L\"")) && FMath::IsNearlyEqual(Seq->Contacts[Ground].Start, 0.4f, 0.001f);
				Check(bAdded && bStored, FString::Printf(TEXT("contact: add ground contact and move it to 0.4-0.8 s (stored=%d)"), bStored));
				SelectBone(HandL, false);
				SelectBone(Hips2, true);
				const int32 Reach = AddContactFromSelection();
				const bool bReach = Reach != INDEX_NONE && Seq->Contacts[Reach].TargetBone == Binding.FamilyName[Hips2] && Seq->Contacts[Reach].TargetRole == 0;
				DeleteContact(Reach);
				DeleteContact(Ground);
				Check(bReach && Seq->Contacts.Num() == Before, FString::Printf(TEXT("contact: reach contact hand -> hips, then both deleted (%d left)"), Seq->Contacts.Num()));
				ClearSelection();
			}
		}
		// 12. Pose library: store the forearm, turn it, apply the stored pose (back), apply at half weight (half way).
		{
			const int32 Arm = Find(TEXT("cf_j_forearm01_R"));
			if (Arm != INDEX_NONE)
			{
				SetFrame(NumFrames() / 2);   // a frame inside the clip, away from a loop's seam
				SelectBone(Arm, false);
				const FQuat Stored = BoneTransform(Arm).GetRotation();
				const bool bStored = StorePose(TEXT("SelfTestPose"));
				BeginEdit(LOCTEXT("TestPoseTurn", "Test pose turn"));
				RotateSelection(FQuat(FVector(1, 0, 0), FMath::DegreesToRadians(40.f)));
				KeySelection(true, false);
				EndEdit();
				const FQuat Turned = BoneTransform(Arm).GetRotation();
				ApplyPose(TEXT("SelfTestPose"), true, 1.f);
				const float Back = AngleDeg(BoneTransform(Arm).GetRotation(), Stored);
				BeginEdit(LOCTEXT("TestPoseTurn2", "Test pose turn"));
				RotateSelection(FQuat(FVector(1, 0, 0), FMath::DegreesToRadians(40.f)));
				KeySelection(true, false);
				EndEdit();
				ApplyPose(TEXT("SelfTestPose"), true, 0.5f);
				const float Half = AngleDeg(BoneTransform(Arm).GetRotation(), Stored);
				Check(bStored && AngleDeg(Turned, Stored) > 30.f && Back < 0.05f && FMath::Abs(Half - 20.f) < 1.f,
					FString::Printf(TEXT("pose library: store, apply (%.3f deg back), apply half weight (%.2f deg of 40)"), Back, Half));
				ClearSelection();
			}
		}
		// 13. Recording: hold a turned forearm while 6 frames play; every frame passed gets a key with that pose.
		{
			const int32 Arm = Find(TEXT("cf_j_forearm01_L"));
			if (Arm != INDEX_NONE)
			{
				const bool bWasPlaying = bPlaying;
				const int32 First = FMath::Max(1, NumFrames() / 3);
				SetFrame(First - 1);
				SelectBone(Arm, false);
				SetRecording(true);
				BeginEdit(LOCTEXT("TestRecord", "Test record"));
				RotateSelection(FQuat(FVector(0, 1, 0), FMath::DegreesToRadians(35.f)));
				const FQuat Held = BoneTransform(Arm).GetRotation();
				bPlaying = true;
				const float Rate = Settings->PlayRate;
				Settings->PlayRate = 1.f;
				for (int32 K = 0; K < 6; ++K) Tick(1.f / FMath::Max(FrameRate(), 1.f));
				Settings->PlayRate = Rate;
				bPlaying = false;
				EndEdit();
				SetRecording(false);
				int32 Keyed = 0;
				float MaxErr = 0.f;
				for (int32 F = First; F < First + 6; ++F)
				{
					SetFrame(F);
					Keyed += HasKeyAtCurrentFrame(Arm) ? 1 : 0;
					MaxErr = FMath::Max(MaxErr, AngleDeg(BoneTransform(Arm).GetRotation(), Held));
				}
				bPlaying = bWasPlaying;
				Check(Keyed >= 5 && MaxErr < 0.1f, FString::Printf(TEXT("record: %d of 6 frames keyed while playing, pose held (max %.3f deg)"), Keyed, MaxErr));
				ClearSelection();
			}
		}
		// 14. Retarget panel: the walk converted live on the preview mesh, its source beside it; removing the left
		// forearm from the profile changes the result at once.
		if (UAnimSequence* Walk = LoadObject<UAnimSequence>(nullptr, TEXT("/Engine/Tutorial/SubEditors/TutorialAssets/Character/Tutorial_Walk_Fwd.Tutorial_Walk_Fwd")))
		{
			const int32 Arm = Find(TEXT("cf_j_forearm01_L"));
			UOKRetargetProfile* Profile = NewObject<UOKRetargetProfile>(GetTransientPackage());
			Profile->ApplyPreset(TEXT("UE4Mannequin"));
			Settings->RetargetSource = Walk;
			Settings->RetargetProfile = Profile;
			Rebuild();
			SetFrame(10);
			const bool bLive = RetargetResult && Player && FMath::IsNearlyEqual(Duration(), Walk->GetPlayLength(), 0.05f) && SourceMesh && SourceMesh->GetSkinnedAsset();
			const FQuat Before = Arm != INDEX_NONE ? BoneTransform(Arm).GetRotation() : FQuat::Identity;
			Profile->BoneMap.Remove(TEXT("lowerarm_l"));
			Rebuild();
			SetFrame(10);
			const float Changed = Arm != INDEX_NONE ? AngleDeg(Before, BoneTransform(Arm).GetRotation()) : 0.f;
			Check(bLive && Changed > 0.5f, FString::Printf(TEXT("retarget panel: walk live on the preview (%.2fs) with its source; profile edit changes the forearm by %.2f deg"), Duration(), Changed));
			Settings->RetargetSource = nullptr;
			Settings->RetargetProfile = nullptr;
			Rebuild();
		}
		// 15. Character card preview: a demo card in the preview world plays this animation (OK pose mode).
		{
			const FString Card = FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir() / TEXT("CharacterCards/Demo/C050.png"));
			if (FPaths::FileExists(Card))
			{
				FFilePath CardPath;
				CardPath.FilePath = Card;
				Settings->Cards = { CardPath };
				FString Error;
				const int32 Loaded = LoadCardPreviews(Error);
				SetFrame(12);
				const AKKPreviewActor* A = CardActors.Num() ? CardActors[0].Get() : nullptr;
				Check(Loaded == 1 && A && A->Session.IsOpen() && A->OkPlayer && A->PoseMode == 3 && FMath::IsNearlyEqual(A->AnimPhase, Time, 1.e-3f),
					FString::Printf(TEXT("card preview: %d card loaded, OK animation at t=%.2f %s"), Loaded, A ? A->AnimPhase : -1.f, *Error));
				for (TWeakObjectPtr<AKKPreviewActor>& X : CardActors) if (X.IsValid()) X->Destroy();
				CardActors.Reset();
				Settings->Cards.Reset();
			}
		}
		// 7. Collapse: same pose, no keys left.
		{
			SetFrame(5);
			const FQuat Before = Fore != INDEX_NONE ? BoneTransform(Fore).GetRotation() : FQuat::Identity;
			FString Error;
			Seq->Modify();
			const bool bOk = Seq->CollapseLayer(Error);
			AfterLayerEdit();
			const FQuat After = Fore != INDEX_NONE ? BoneTransform(Fore).GetRotation() : FQuat::Identity;
			Check(bOk && Seq->KeyLayer.IsEmpty() && AngleDeg(Before, After) < 0.001f, FString::Printf(TEXT("collapse layer keeps the pose (%.4f deg) %s"), AngleDeg(Before, After), *Error));
		}
	}
	// Interactive run: leave a small keyed swing on the forearm to look at (timeline diamonds, graph editor curves).
	if (!bExitWhenDone && Seq && CanAnimate())
	{
		const int32 Fore = Find(TEXT("cf_j_forearm01_L"));
		if (Fore != INDEX_NONE)
		{
			SelectBone(Fore, false);
			for (const TPair<int32, float> K : TArray<TPair<int32, float>>{ { 0, 0.f }, { 20, 40.f }, { 40, -25.f }, { 55, 0.f } })
			{
				SetFrame(K.Key);
				BeginEdit(LOCTEXT("DemoKey", "Demo key"));
				RotateSelection(FQuat(FVector(0, 0, 1), FMath::DegreesToRadians(K.Value)));
				KeySelection(true, false);
				EndEdit();
			}
			SetFrame(20);
		}
	}
	Lines.Add(FString::Printf(TEXT("done: %d failures"), Failures));
	for (const FString& L : Lines) UE_LOG(LogTemp, Display, TEXT("OK editor selftest %s"), *L);
	FFileHelper::SaveStringToFile(FString::Join(Lines, TEXT("\n")), *FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("OKAnim"), TEXT("editor_selftest.txt")));
	if (bExitWhenDone) FPlatformMisc::RequestExit(true); // the test leaves the asset edited in memory: no save prompt
}

#undef LOCTEXT_NAMESPACE


