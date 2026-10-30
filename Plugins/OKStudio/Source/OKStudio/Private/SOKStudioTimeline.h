#pragma once

// OK Studio timeline: a time ruler with the play head and the loop range, one row per keyed object (and the scene's own
// track) that opens into its transform / FK / state / value rows, keys as diamonds. Keys are picked, box-selected, dragged
// in time, deleted, copied to the play head and given an ease; the play head scrubs the scene (it really samples).
#include "CoreMinimal.h"
#include "OKStudioGame.h"
#include "Widgets/SLeafWidget.h"

class SOKStudioTimeline : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SOKStudioTimeline) {}
		SLATE_ARGUMENT(TWeakObjectPtr<UOKStudioGame>, Game)
		/** Asked for the ease menu of the selected keys (right click on a key). */
		SLATE_ARGUMENT(TFunction<void(const FVector2D&)>, OnEaseMenu)
		SLATE_ARGUMENT(TFunction<void(const FString&)>, OnStatus)
	SLATE_END_ARGS()

	/** A key by its track's object and its time (indices change as keys move). */
	struct FKeyRef
	{
		int32 Track = 0;
		float Time = 0.f;
		bool operator==(const FKeyRef& O) const { return Track == O.Track && FMath::Abs(Time - O.Time) < 1e-4f; }
	};

	void Construct(const FArguments& Args);
	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(500.f, 160.f); }
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& Culling, FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const override;
	virtual FReply OnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event) override;
	virtual FReply OnMouseMove(const FGeometry& Geometry, const FPointerEvent& Event) override;
	virtual FReply OnMouseButtonUp(const FGeometry& Geometry, const FPointerEvent& Event) override;
	virtual FReply OnMouseWheel(const FGeometry& Geometry, const FPointerEvent& Event) override;
	virtual FReply OnKeyDown(const FGeometry& Geometry, const FKeyEvent& Event) override;
	virtual bool SupportsKeyboardFocus() const override { return true; }

	TArray<FKeyRef> Selected;
	/** Times snap to whole frames of this rate (0: no snapping). */
	float SnapFps = 30.f;
	/** Tracks opened into their sub rows. */
	TSet<int32> Open;

	void DeleteSelected();
	void CopySelected();
	void PasteAtPlayhead();
	void SetEase(EOKSceneEase Ease);
	/** Moves the play head (samples the scene, characters' animations too). */
	void Scrub(float Time);
	float Snap(float Time) const { return SnapFps > 0.f ? FMath::RoundToFloat(Time * SnapFps) / SnapFps : Time; }
	/** Keys of the selected objects now (K). */
	void KeySelection();

private:
	TWeakObjectPtr<UOKStudioGame> Game;
	TFunction<void(const FVector2D&)> OnEaseMenu;
	TFunction<void(const FString&)> OnStatus;
	/** The rows as painted: the track, its sub row (-1 the track itself; 0 transform, 1 FK, 2 state, 3 value). */
	struct FRowInfo { int32 Track = 0; int32 Sub = -1; FString Label; };
	TArray<FRowInfo> RowInfos() const;
	/** Clipboard: keys relative to the earliest, per track. */
	TArray<TPair<int32, FOKSceneKey>> Clip;
	enum class EDrag : uint8 { None, Scrub, Keys, Box } Drag = EDrag::None;
	FVector2D DragFrom = FVector2D::ZeroVector, DragNow = FVector2D::ZeroVector;
	float DragTimeFrom = 0.f;
	float Zoom = 1.f, Scroll = 0.f;
	int32 RowScroll = 0;
	static constexpr float LabelWidth = 110.f, RulerHeight = 16.f, RowHeight = 16.f;
	float TimeToX(const FGeometry& G, float Time) const;
	float XToTime(const FGeometry& G, float X) const;
	/** The key under a local position. */
	bool HitKey(const FGeometry& G, const FVector2D& Local, FKeyRef& Out) const;
	bool SubHasKey(int32 Sub, const FOKSceneKey& K, const FOKSceneObject* O) const;
};
