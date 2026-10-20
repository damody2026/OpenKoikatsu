#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "OKCameraDrag.h"
#include "OKBodyGesture.h"
#include "OKBodyInteraction.h"
#include "OpenKoikatsuPlayerController.generated.h"

struct FInputKeyEventArgs;

UCLASS()
class OPENKOIKATSU_API AOpenKoikatsuPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	class AKKPreviewActor* GetPreviewCameraOwner() const;
	uint8 GetCameraMouseButtons() const;
	void UpdateCameraMouse(bool HasMouse, bool Focused, bool OverUI, const FVector2D& Position);
	bool UpdatePaintGesture(bool Left,bool HasMouse,bool Focused,bool OverUI);
	void UpdatePaintAim(bool Fire);
	uint64 PaintAimQueries=0;
	void SetPaintPreviewActive(bool Enabled);
	bool IsPaintPreviewActive() const { return PaintPreviewActive; }
	virtual void BeginPlay() override;
	virtual void PlayerTick(float DeltaTime) override;
	virtual bool InputKey(const FInputKeyEventArgs& Params) override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	bool PaintAimHeld=false;
	// Set by the studio while its guide object or timeline owns the left button: the camera ignores the drag.
	bool bExternalMouseOwner=false;
 FOKBodyInteraction BodyInteraction;
 FOKBodyGesture BodyGesture;
 bool bBodyTouchEnabled=false;
 void SetBodyTouchEnabled(bool Enabled);
#if !UE_BUILD_SHIPPING
    // Audit input feeds the normal aiming path while PlayerTick/camera/UI stay live.
    TOptional<FVector> ScriptedPaintAim;
    bool bScriptedPaintFire=false;
#endif
	UPROPERTY() TObjectPtr<class AOKPaintSprayer> PaintSprayer;

private:
	FOKCameraDrag CameraDrag;
	TWeakObjectPtr<class AKKPreviewActor> DragActor;
	mutable TWeakObjectPtr<class UOKGalleryWidget> CameraGallery;
	uint64 WheelFrame = MAX_uint64;
	bool PaintPreviewActive=false;
	bool PaintOwnsLeft=false;
	bool PaintPreviousLeft=false;
	TSharedPtr<class IInputProcessor> PaintInput;
};
