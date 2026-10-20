#pragma once

#include "CoreMinimal.h"
#include "EdMode.h"

class FOKAnimationEditor;

/** Clickable bone of the preview (header index of the OK skeleton binding). */
struct HOKBoneProxy : public HHitProxy
{
	DECLARE_HIT_PROXY();
	int32 Bone;
	explicit HOKBoneProxy(int32 InBone) : HHitProxy(HPP_World), Bone(InBone) {}
	virtual EMouseCursor::Type GetMouseCursor() override { return EMouseCursor::Crosshairs; }
};

/**
 * Animate mode of the OK animation editor viewport: draws the skeleton, picks bones, drives the transform gizmo
 * (rotate any bone, move the hips, move a hand / foot with two-bone IK) and the Maya hotkeys:
 * S key, Shift+E key rotation, Shift+W key translation, , . previous / next key, Alt+, Alt+. previous / next frame,
 * Alt+V play, Home / End first / last frame, Up / Down pick-walk to parent / child, Delete delete keys at the
 * current frame, Ctrl+C / Ctrl+V copy / paste the pose of the selected bones.
 */
class FOKAnimEdMode : public FEdMode
{
public:
	static const FEditorModeID ModeId;
	FOKAnimationEditor* Editor = nullptr;

	virtual void Render(const FSceneView* View, FViewport* Viewport, FPrimitiveDrawInterface* PDI) override;
	virtual bool HandleClick(FEditorViewportClient* Client, HHitProxy* HitProxy, const FViewportClick& Click) override;
	virtual bool InputDelta(FEditorViewportClient* Client, FViewport* Viewport, FVector& Drag, FRotator& Rot, FVector& Scale) override;
	virtual bool InputKey(FEditorViewportClient* Client, FViewport* Viewport, FKey Key, EInputEvent Event) override;
	virtual bool StartTracking(FEditorViewportClient* Client, FViewport* Viewport) override;
	virtual bool EndTracking(FEditorViewportClient* Client, FViewport* Viewport) override;
	virtual FVector GetWidgetLocation() const override;
	virtual bool ShouldDrawWidget() const override;
	virtual bool UsesTransformWidget() const override { return true; }
	virtual bool UsesTransformWidget(UE::Widget::EWidgetMode Mode) const override;
	virtual bool AllowWidgetMove() override { return false; }
	virtual bool GetCustomDrawingCoordinateSystem(FMatrix& Matrix, void* Data) override;
	virtual bool GetCustomInputCoordinateSystem(FMatrix& Matrix, void* Data) override;
	virtual bool IsCompatibleWith(FEditorModeID Other) const override { return true; }

private:
	bool bTracking = false;
};
