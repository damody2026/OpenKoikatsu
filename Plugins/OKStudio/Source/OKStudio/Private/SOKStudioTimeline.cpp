#include "SOKStudioTimeline.h"
#include "OKSceneActor.h"
#include "OKStudioStyle.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElements.h"

void SOKStudioTimeline::Construct(const FArguments& Args)
{
	Game = Args._Game;
	OnEaseMenu = Args._OnEaseMenu;
	OnStatus = Args._OnStatus;
}

TArray<SOKStudioTimeline::FRowInfo> SOKStudioTimeline::RowInfos() const
{
	TArray<FRowInfo> Rows;
	if (!Game.IsValid()) return Rows;
	const UOKSceneDocument* Doc = Game->Document;
	static const TCHAR* Subs[] = { TEXT("變換"), TEXT("FK"), TEXT("狀態"), TEXT("數值") };
	for (const FOKSceneTrack& T : Doc->Tracks)
	{
		const FOKSceneObject* O = Doc->Find(T.ObjectId);
		FRowInfo R;
		R.Track = T.ObjectId;
		R.Label = T.ObjectId == UOKSceneDocument::SceneTrackId ? FString(TEXT("場景（畫面）")) : (O ? O->Name : FString::Printf(TEXT("#%d"), T.ObjectId));
		Rows.Add(R);
		if (!Open.Contains(T.ObjectId) || T.ObjectId == UOKSceneDocument::SceneTrackId) continue;
		for (int32 S = 0; S < 4; ++S)
		{
			if ((S == 1 || S == 2) && (!O || O->Kind != EOKSceneObjectKind::Character)) continue;
			if (S == 3 && (!O || (O->Kind != EOKSceneObjectKind::Light && O->Kind != EOKSceneObjectKind::Camera && O->Kind != EOKSceneObjectKind::Effect))) continue;
			FRowInfo Sub;
			Sub.Track = T.ObjectId;
			Sub.Sub = S;
			Sub.Label = FString(TEXT("    ")) + Subs[S];
			Rows.Add(Sub);
		}
	}
	return Rows;
}

bool SOKStudioTimeline::SubHasKey(int32 Sub, const FOKSceneKey& K, const FOKSceneObject* O) const
{
	switch (Sub)
	{
	case 1: return K.Fk.Num() > 0;
	case 2: return K.bCharacterState || K.AnimTime >= 0.f;
	case 3: return O && (O->Kind == EOKSceneObjectKind::Light || O->Kind == EOKSceneObjectKind::Camera || O->Kind == EOKSceneObjectKind::Effect);
	default: return true;
	}
}

float SOKStudioTimeline::TimeToX(const FGeometry& G, float Time) const
{
	const float Width = FMath::Max(10.f, float(G.GetLocalSize().X) - LabelWidth - 6.f);
	const float Duration = Game.IsValid() ? FMath::Max(Game->Document->Duration, 0.1f) : 10.f;
	return LabelWidth + 3.f + (Time - Scroll) / Duration * Width * Zoom;
}

float SOKStudioTimeline::XToTime(const FGeometry& G, float X) const
{
	const float Width = FMath::Max(10.f, float(G.GetLocalSize().X) - LabelWidth - 6.f);
	const float Duration = Game.IsValid() ? FMath::Max(Game->Document->Duration, 0.1f) : 10.f;
	return FMath::Clamp(Scroll + (X - LabelWidth - 3.f) / (Width * Zoom) * Duration, 0.f, Duration);
}

int32 SOKStudioTimeline::OnPaint(const FPaintArgs&, const FGeometry& G, const FSlateRect&, FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle&, bool) const
{
	using namespace OKStudioStyle;
	const FVector2D Size = G.GetLocalSize();
	auto Box = [&](float X, float Y, float W, float H, const FLinearColor& C, int32 L)
	{
		FSlateDrawElement::MakeBox(Out, L, G.ToPaintGeometry(FVector2f(W, H), FSlateLayoutTransform(FVector2f(X, Y))), Brush(C), ESlateDrawEffect::None, C);
	};
	auto Text = [&](float X, float Y, const FString& S, const FLinearColor& C, float Px, int32 L)
	{
		FSlateDrawElement::MakeText(Out, L, G.ToPaintGeometry(FVector2f(400.f, 20.f), FSlateLayoutTransform(FVector2f(X, Y))), S, Font(Px), ESlateDrawEffect::None, C);
	};
	Box(0.f, 0.f, Size.X, Size.Y, ListArea(), Layer);
	if (!Game.IsValid()) return Layer + 1;
	const UOKSceneDocument* Doc = Game->Document;
	const float Duration = FMath::Max(Doc->Duration, 0.1f);
	// Loop range.
	if (Game->LoopEnd > Game->LoopStart)
	{
		const float X0 = TimeToX(G, Game->LoopStart), X1 = TimeToX(G, Game->LoopEnd);
		Box(X0, 0.f, FMath::Max(1.f, X1 - X0), Size.Y, FLinearColor(0.35f, 0.6f, 1.f, 0.12f), Layer + 1);
	}
	// Ruler: a tick every second (every 5 when crowded), labels.
	Box(0.f, 0.f, Size.X, RulerHeight, Panel(), Layer + 1);
	const float PixelsPerSecond = TimeToX(G, 1.f) - TimeToX(G, 0.f);
	const int32 Every = PixelsPerSecond > 40.f ? 1 : (PixelsPerSecond > 12.f ? 5 : 10);
	for (int32 S = 0; S <= FMath::CeilToInt(Duration); S += Every)
	{
		const float X = TimeToX(G, float(S));
		if (X < LabelWidth || X > Size.X) continue;
		FSlateDrawElement::MakeLines(Out, Layer + 2, G.ToPaintGeometry(), { FVector2f(X, RulerHeight - 6.f), FVector2f(X, Size.Y) }, ESlateDrawEffect::None, FLinearColor(1.f, 1.f, 1.f, 0.12f));
		Text(X + 2.f, 1.f, FString::Printf(TEXT("%d"), S), TextHint(), 9.f, Layer + 2);
	}
	// Rows.
	const TArray<FRowInfo> Rows = RowInfos();
	for (int32 I = RowScroll; I < Rows.Num(); ++I)
	{
		const float Y = RulerHeight + (I - RowScroll) * RowHeight;
		if (Y > Size.Y) break;
		const FRowInfo& R = Rows[I];
		const bool bSelectedObject = Game->IsSelected(R.Track);
		if (bSelectedObject) Box(0.f, Y, LabelWidth, RowHeight, FLinearColor(RowFill().R, RowFill().G, RowFill().B, 0.6f), Layer + 1);
		const FString Arrow = R.Sub < 0 && R.Track != UOKSceneDocument::SceneTrackId ? (Open.Contains(R.Track) ? TEXT("▼ ") : TEXT("▶ ")) : TEXT("  ");
		Text(3.f, Y + 1.f, Arrow + R.Label, R.Sub < 0 ? OKStudioStyle::Text() : TextSoft(), 10.f, Layer + 2);
		FSlateDrawElement::MakeLines(Out, Layer + 1, G.ToPaintGeometry(), { FVector2f(0.f, Y + RowHeight), FVector2f(Size.X, Y + RowHeight) }, ESlateDrawEffect::None, FLinearColor(1.f, 1.f, 1.f, 0.06f));
		const FOKSceneTrack* T = Doc->TrackOf(R.Track);
		const FOKSceneObject* O = Doc->Find(R.Track);
		if (!T) continue;
		for (const FOKSceneKey& K : T->Keys)
		{
			if (R.Sub >= 0 && !SubHasKey(R.Sub, K, O)) continue;
			const float X = TimeToX(G, K.Time);
			if (X < LabelWidth || X > Size.X) continue;
			const bool bSel = Selected.Contains(FKeyRef{ R.Track, K.Time });
			const FLinearColor C = bSel ? FLinearColor::Yellow : (K.Ease == EOKSceneEase::Step ? FLinearColor(1.f, .55f, .2f) : FLinearColor(.75f, .85f, 1.f));
			const float H = R.Sub < 0 ? 5.f : 3.5f, CY = Y + RowHeight * 0.5f;
			FSlateDrawElement::MakeLines(Out, Layer + 3, G.ToPaintGeometry(), { FVector2f(X, CY - H), FVector2f(X + H, CY), FVector2f(X, CY + H), FVector2f(X - H, CY), FVector2f(X, CY - H) }, ESlateDrawEffect::None, C, true, 2.f);
		}
	}
	FSlateDrawElement::MakeLines(Out, Layer + 2, G.ToPaintGeometry(), { FVector2f(LabelWidth, 0.f), FVector2f(LabelWidth, Size.Y) }, ESlateDrawEffect::None, FLinearColor(1.f, 1.f, 1.f, 0.2f));
	// Play head.
	const float PX = TimeToX(G, Game->TimelineTime);
	FSlateDrawElement::MakeLines(Out, Layer + 4, G.ToPaintGeometry(), { FVector2f(PX, 0.f), FVector2f(PX, Size.Y) }, ESlateDrawEffect::None, FLinearColor(1.f, .3f, .3f), true, 1.5f);
	if (Drag == EDrag::Box)
	{
		const FVector2D A(FMath::Min(DragFrom.X, DragNow.X), FMath::Min(DragFrom.Y, DragNow.Y)), B(FMath::Max(DragFrom.X, DragNow.X), FMath::Max(DragFrom.Y, DragNow.Y));
		Box(A.X, A.Y, B.X - A.X, B.Y - A.Y, FLinearColor(1.f, 1.f, 1.f, 0.12f), Layer + 5);
	}
	if (Rows.IsEmpty()) Text(LabelWidth + 8.f, RulerHeight + 6.f, TEXT("沒有關鍵幀：選物件後按「打 key」（K）"), TextHint(), 10.f, Layer + 2);
	return Layer + 6;
}

bool SOKStudioTimeline::HitKey(const FGeometry& G, const FVector2D& Local, FKeyRef& Out) const
{
	if (!Game.IsValid() || Local.Y < RulerHeight || Local.X < LabelWidth) return false;
	const TArray<FRowInfo> Rows = RowInfos();
	const int32 Row = RowScroll + int32((Local.Y - RulerHeight) / RowHeight);
	if (!Rows.IsValidIndex(Row)) return false;
	const FOKSceneTrack* T = Game->Document->TrackOf(Rows[Row].Track);
	const FOKSceneObject* O = Game->Document->Find(Rows[Row].Track);
	if (!T) return false;
	float Best = 6.f;
	bool bHit = false;
	for (const FOKSceneKey& K : T->Keys)
	{
		if (Rows[Row].Sub >= 0 && !SubHasKey(Rows[Row].Sub, K, O)) continue;
		const float D = FMath::Abs(TimeToX(G, K.Time) - float(Local.X));
		if (D < Best) { Best = D; Out = { Rows[Row].Track, K.Time }; bHit = true; }
	}
	return bHit;
}

void SOKStudioTimeline::Scrub(float Time)
{
	if (!Game.IsValid()) return;
	Game->SetTimelineTime(Time);
	// The characters' animations follow the play head too (a still frame of the moment).
	if (Game->SceneActor)
		for (const FOKSceneObject& O : Game->Document->Objects)
			if (O.Kind == EOKSceneObjectKind::Character) Game->SceneActor->SetAnimationTime(O.Id, Game->TimelineTime);
}

FReply SOKStudioTimeline::OnMouseButtonDown(const FGeometry& G, const FPointerEvent& E)
{
	if (!Game.IsValid()) return FReply::Unhandled();
	const FVector2D Local = G.AbsoluteToLocal(E.GetScreenSpacePosition());
	DragFrom = DragNow = Local;
	FKeyRef Key;
	const bool bKey = HitKey(G, Local, Key);
	if (E.GetEffectingButton() == EKeys::RightMouseButton)
	{
		if (bKey && !Selected.Contains(Key)) Selected = { Key };
		if (bKey && OnEaseMenu) OnEaseMenu(E.GetScreenSpacePosition());
		return FReply::Handled().SetUserFocus(SharedThis(this), EFocusCause::Mouse);
	}
	if (E.GetEffectingButton() != EKeys::LeftMouseButton) return FReply::Unhandled();
	if (Local.Y < RulerHeight && Local.X >= LabelWidth)
	{
		Drag = EDrag::Scrub;
		Game->bTimelinePlaying = false;
		Scrub(Snap(XToTime(G, Local.X)));
	}
	else if (Local.X < LabelWidth && Local.Y >= RulerHeight)
	{
		// Label: open / close the track, select its object.
		const TArray<FRowInfo> Rows = RowInfos();
		const int32 Row = RowScroll + int32((Local.Y - RulerHeight) / RowHeight);
		if (Rows.IsValidIndex(Row) && Rows[Row].Sub < 0)
		{
			const int32 Track = Rows[Row].Track;
			if (Local.X < 14.f) { if (Open.Contains(Track)) Open.Remove(Track); else Open.Add(Track); }
			else if (Game->Document->Find(Track)) { if (E.IsShiftDown() || E.IsControlDown()) Game->ToggleSelect(Track); else Game->Select(Track); }
		}
	}
	else if (bKey)
	{
		if (E.IsShiftDown() || E.IsControlDown()) { if (Selected.Contains(Key)) Selected.Remove(Key); else Selected.Add(Key); }
		else if (!Selected.Contains(Key)) Selected = { Key };
		Drag = EDrag::Keys;
		DragTimeFrom = XToTime(G, Local.X);
	}
	else
	{
		if (!E.IsShiftDown() && !E.IsControlDown()) Selected.Reset();
		Drag = EDrag::Box;
	}
	return FReply::Handled().CaptureMouse(SharedThis(this)).SetUserFocus(SharedThis(this), EFocusCause::Mouse);
}

FReply SOKStudioTimeline::OnMouseMove(const FGeometry& G, const FPointerEvent& E)
{
	if (!HasMouseCapture() || !Game.IsValid()) return FReply::Unhandled();
	DragNow = G.AbsoluteToLocal(E.GetScreenSpacePosition());
	if (Drag == EDrag::Scrub) Scrub(Snap(XToTime(G, DragNow.X)));
	if (Drag == EDrag::Keys && OnStatus) OnStatus(FString::Printf(TEXT("移動 %d 個 key %+.3f 秒"), Selected.Num(), Snap(XToTime(G, DragNow.X) - DragTimeFrom)));
	return FReply::Handled();
}

FReply SOKStudioTimeline::OnMouseButtonUp(const FGeometry& G, const FPointerEvent& E)
{
	if (!HasMouseCapture()) return FReply::Unhandled();
	if (Game.IsValid())
	{
		if (Drag == EDrag::Keys)
		{
			const float Shift = Snap(XToTime(G, DragNow.X) - DragTimeFrom);
			if (FMath::Abs(Shift) > 1e-4f)
			{
				// Later keys first when moving right (a moved key never lands on one still to move), earlier first when left.
				TArray<FKeyRef> Order = Selected;
				Order.Sort([Shift](const FKeyRef& A, const FKeyRef& B) { return Shift > 0.f ? A.Time > B.Time : A.Time < B.Time; });
				TArray<FKeyRef> Moved;
				for (const FKeyRef& K : Order)
				{
					FOKSceneTrack* T = Game->Document->TrackOf(K.Track);
					const int32 Index = T ? T->Keys.IndexOfByPredicate([&K](const FOKSceneKey& X) { return FMath::Abs(X.Time - K.Time) < 1e-4f; }) : INDEX_NONE;
					if (Index == INDEX_NONE) continue;
					const float To = FMath::Max(0.f, Snap(K.Time + Shift));
					Game->Document->MoveKey(K.Track, Index, To);
					Moved.Add({ K.Track, To });
				}
				Selected = Moved;
			}
		}
		else if (Drag == EDrag::Box)
		{
			const FVector2D A(FMath::Min(DragFrom.X, DragNow.X), FMath::Min(DragFrom.Y, DragNow.Y)), B(FMath::Max(DragFrom.X, DragNow.X), FMath::Max(DragFrom.Y, DragNow.Y));
			const TArray<FRowInfo> Rows = RowInfos();
			for (int32 I = RowScroll; I < Rows.Num(); ++I)
			{
				const float Y = RulerHeight + (I - RowScroll) * RowHeight + RowHeight * 0.5f;
				if (Y < A.Y || Y > B.Y) continue;
				const FOKSceneTrack* T = Game->Document->TrackOf(Rows[I].Track);
				const FOKSceneObject* O = Game->Document->Find(Rows[I].Track);
				if (T) for (const FOKSceneKey& K : T->Keys)
				{
					const float X = TimeToX(G, K.Time);
					if (X >= A.X && X <= B.X && (Rows[I].Sub < 0 || SubHasKey(Rows[I].Sub, K, O))) Selected.AddUnique({ Rows[I].Track, K.Time });
				}
			}
		}
	}
	Drag = EDrag::None;
	return FReply::Handled().ReleaseMouseCapture();
}

FReply SOKStudioTimeline::OnMouseWheel(const FGeometry& G, const FPointerEvent& E)
{
	if (E.IsControlDown())
	{
		// Zoom about the mouse.
		const FVector2D Local = G.AbsoluteToLocal(E.GetScreenSpacePosition());
		const float At = XToTime(G, Local.X);
		Zoom = FMath::Clamp(Zoom * (E.GetWheelDelta() > 0.f ? 1.25f : 0.8f), 1.f, 40.f);
		Scroll = FMath::Max(0.f, Scroll + At - XToTime(G, Local.X));
	}
	else if (E.IsShiftDown()) Scroll = FMath::Max(0.f, Scroll - E.GetWheelDelta() * (Game.IsValid() ? Game->Document->Duration : 10.f) * 0.05f / Zoom);
	else RowScroll = FMath::Clamp(RowScroll - int32(E.GetWheelDelta()), 0, FMath::Max(0, RowInfos().Num() - 1));
	return FReply::Handled();
}

FReply SOKStudioTimeline::OnKeyDown(const FGeometry&, const FKeyEvent& E)
{
	if (E.GetKey() == EKeys::Delete) { DeleteSelected(); return FReply::Handled(); }
	if (E.IsControlDown() && E.GetKey() == EKeys::C) { CopySelected(); return FReply::Handled(); }
	if (E.IsControlDown() && E.GetKey() == EKeys::V) { PasteAtPlayhead(); return FReply::Handled(); }
	return FReply::Unhandled();
}

void SOKStudioTimeline::DeleteSelected()
{
	if (!Game.IsValid()) return;
	for (const FKeyRef& K : Selected) Game->Document->RemoveKey(K.Track, K.Time);
	if (OnStatus) OnStatus(FString::Printf(TEXT("刪除 %d 個 key"), Selected.Num()));
	Selected.Reset();
}

void SOKStudioTimeline::CopySelected()
{
	if (!Game.IsValid() || Selected.IsEmpty()) return;
	Clip.Reset();
	float First = TNumericLimits<float>::Max();
	for (const FKeyRef& K : Selected) First = FMath::Min(First, K.Time);
	for (const FKeyRef& K : Selected)
		if (const FOKSceneKey* Key = Game->Document->KeyAt(K.Track, K.Time)) { FOKSceneKey Copy = *Key; Copy.Time -= First; Clip.Add({ K.Track, Copy }); }
	if (OnStatus) OnStatus(FString::Printf(TEXT("複製 %d 個 key"), Clip.Num()));
}

void SOKStudioTimeline::PasteAtPlayhead()
{
	if (!Game.IsValid() || Clip.IsEmpty()) return;
	UOKSceneDocument* Doc = Game->Document;
	Selected.Reset();
	for (const TPair<int32, FOKSceneKey>& C : Clip)
	{
		FOKSceneTrack* T = Doc->TrackOf(C.Key);
		if (!T && (Doc->Find(C.Key) || C.Key == UOKSceneDocument::SceneTrackId)) { T = &Doc->Tracks.AddDefaulted_GetRef(); T->ObjectId = C.Key; }
		if (!T) continue;
		FOKSceneKey K = C.Value;
		K.Time = Snap(Game->TimelineTime + K.Time);
		T->Keys.RemoveAll([&K](const FOKSceneKey& X) { return FMath::Abs(X.Time - K.Time) < 1.f / 60.f; });
		T->Keys.Add(K);
		T->Keys.Sort([](const FOKSceneKey& A, const FOKSceneKey& B) { return A.Time < B.Time; });
		Doc->Duration = FMath::Max(Doc->Duration, K.Time);
		Selected.Add({ C.Key, K.Time });
	}
	Doc->NotifyChanged();
	if (OnStatus) OnStatus(FString::Printf(TEXT("貼上 %d 個 key 到 %.2f 秒"), Clip.Num(), Game->TimelineTime));
}

void SOKStudioTimeline::SetEase(EOKSceneEase Ease)
{
	if (!Game.IsValid()) return;
	for (const FKeyRef& K : Selected) if (FOKSceneKey* Key = Game->Document->KeyAt(K.Track, K.Time)) Key->Ease = Ease;
	Game->Document->NotifyChanged();
}

void SOKStudioTimeline::KeySelection()
{
	if (!Game.IsValid()) return;
	const float Time = Snap(Game->TimelineTime);
	for (int32 Id : Game->Selection) Game->Document->SetKey(Id, Time);
	if (OnStatus) OnStatus(FString::Printf(TEXT("在 %.2f 秒為 %d 個物件打 key"), Time, Game->Selection.Num()));
}
