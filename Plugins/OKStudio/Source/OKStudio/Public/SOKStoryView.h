#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateBrush.h"
#include "Widgets/SLeafWidget.h"

class FJsonObject;
class UMaterialInstanceDynamic;
class UTexture2D;

/** Where a story view gets what it shows and sends what the player does (a story player, or the street's live
 *  conversations). */
struct FOKStoryViewSource
{
	TFunction<TSharedPtr<FJsonObject>()> Frame;
	TFunction<UMaterialInstanceDynamic*()> Portrait;
	TFunction<UTexture2D*(const FString&)> Thumbnail;
	TFunction<void(const FString&)> Input;
};

/**
 * The story's screen: draws the director's frame (dialog box, name plate, typed text, live portrait, choices, act
 * title, quick buttons, menu / save / load / backlog / settings panels) and turns every click, key and wheel into the
 * director's input events. It decides nothing itself.
 */
class OKSTUDIO_API SOKStoryView : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SOKStoryView) {}
		SLATE_ARGUMENT(FOKStoryViewSource, Source)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(100.0, 100.0); }
	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonDoubleClick(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override { return OnMouseButtonDown(MyGeometry, MouseEvent); }
	virtual FReply OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply OnKeyUp(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;

private:
	FOKStoryViewSource Source;

	/** What a click on a drawn element sends (the last painted frame's elements, topmost last). */
	struct FHit
	{
		FVector2D Min, Max;
		FString Event;
	};
	mutable TArray<FHit> Hits;
	/** Panel bounds while a panel is open (a click outside closes it). */
	mutable FVector2D PanelMin, PanelMax;
	mutable bool bPanelOpen = false;
	mutable float BacklogScroll = 0.f;
	mutable float OutlineScroll = 0.f;
	mutable TMap<TWeakObjectPtr<UObject>, FSlateBrush> Brushes;
	FVector2D LastPointer = FVector2D(-1.0, -1.0);

	const FSlateBrush* BrushFor(UObject* Resource, const FVector2D& Size) const;
	void Send(const FString& Json) const;
	static FString KeyEvent(const FKeyEvent& Event, bool bDown);
};
