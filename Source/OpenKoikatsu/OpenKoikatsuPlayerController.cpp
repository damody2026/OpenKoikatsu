#include "OpenKoikatsuPlayerController.h"
#include "KKPreviewActor.h"
#include "OKGalleryWidget.h"
#include "Camera/CameraActor.h"
#include "Camera/PlayerCameraManager.h"
#include "UObject/UObjectIterator.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "Framework/Application/SlateApplication.h"
#include "InputKeyEventArgs.h"
#include "OKPaintSprayer.h"
#include "OKPaintDemo.h"
#include "Framework/Application/IInputProcessor.h"

AKKPreviewActor* AOpenKoikatsuPlayerController::GetPreviewCameraOwner() const
{
 const APlayerController* PC=this;
 if(!PC||!PC->GetWorld())return nullptr;
 // The cached gallery is only a lookup shortcut: read its active actor on
 // every call, including switches which reuse the same camera.
 if(auto* Gallery=CameraGallery.Get())if(Gallery->GetWorld()==PC->GetWorld()&&IsValid(Gallery->PreviewActor)&&Gallery->PreviewActor->StudioCam==PC->GetViewTarget())return Gallery->PreviewActor;
 // The gallery's active actor wins over stale/partner actors in the same world.
 for(TObjectIterator<UOKGalleryWidget> It;It;++It)
  if(It->GetWorld()==PC->GetWorld()&&IsValid(It->PreviewActor)&&It->PreviewActor->StudioCam==PC->GetViewTarget()){CameraGallery=*It;return It->PreviewActor;}
 for(TActorIterator<AKKPreviewActor> It(PC->GetWorld());It;++It)
  if(IsValid(It->StudioCam)&&It->StudioCam==PC->GetViewTarget())return *It;
 return nullptr;
}

// Sliders and buttons should keep the click. The 3D view is the viewport itself.
static bool CursorOverInteractiveUI()
{
	if (!FSlateApplication::IsInitialized())
	{
		return false;
	}
	FSlateApplication& App = FSlateApplication::Get();
	const FWidgetPath Path = App.LocateWindowUnderMouse(
		App.GetCursorPos(),
		App.GetInteractiveTopLevelWindows());
	if (!Path.IsValid() || Path.Widgets.Num() == 0)
	{
		return false;
	}
	const FString Type = Path.Widgets.Last().Widget->GetTypeAsString();
	return Type != TEXT("SViewport")
		&& Type != TEXT("SGameLayerManager")
		&& Type != TEXT("SWindow")
		&& Type != TEXT("SOverlay")
		&& Type != TEXT("SConstraintCanvas")
		&& Type != TEXT("SObjectWidget");
}

class FOKPaintInput final : public IInputProcessor
{
 TWeakObjectPtr<AOpenKoikatsuPlayerController> PC;
public:
 explicit FOKPaintInput(AOpenKoikatsuPlayerController* P):PC(P){}
 virtual void Tick(const float,FSlateApplication&,TSharedRef<ICursor>) override {}
 virtual bool HandleKeyDownEvent(FSlateApplication& App,const FKeyEvent& E) override {
  const auto Focus=App.GetKeyboardFocusedWidget();if(Focus&&(Focus->GetTypeAsString().Contains(TEXT("Editable"))||Focus->GetTypeAsString().Contains(TEXT("SearchBox"))))return false;
  if(auto* P=PC.Get())if(P->IsPaintPreviewActive()&&!E.IsRepeat()){
   if(E.GetKey()==EKeys::A)P->PaintAimHeld=true;
   if(E.GetKey()==EKeys::R&&P->PaintSprayer)P->PaintSprayer->ClearPaint();
  }return false;
 }
 virtual bool HandleKeyUpEvent(FSlateApplication&,const FKeyEvent& E) override {if(auto* P=PC.Get())if(E.GetKey()==EKeys::A)P->PaintAimHeld=false;return false;}
};

void AOpenKoikatsuPlayerController::BeginPlay()
{
	Super::BeginPlay();
	if(GetWorld()->GetAuthGameMode<AOKPaintDemoGameMode>())SetPaintPreviewActive(true);
	if(FSlateApplication::IsInitialized()){PaintInput=MakeShared<FOKPaintInput>(this);FSlateApplication::Get().RegisterInputPreProcessor(PaintInput);}
	bShowMouseCursor = true;
	bEnableClickEvents = true;
	bEnableMouseOverEvents = true;
	FInputModeGameAndUI Mode;
	Mode.SetHideCursorDuringCapture(false);
	Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	SetInputMode(Mode);
	// CaptureDuringMouseDown steals the press from UMG, so 加载3D sometimes never fires.
	if (UGameViewportClient* VP = GetWorld() ? GetWorld()->GetGameViewport() : nullptr)
	{
		VP->SetMouseCaptureMode(EMouseCaptureMode::NoCapture);
		VP->SetHideCursorDuringCapture(false);
		if (IsLocalController() && GEngine)
		{
			// Enable explicitly so scene re-entry cannot toggle an existing display off.
			GEngine->SetEngineStat(GetWorld(), VP, TEXT("FPS"), true);
			UE_LOG(LogTemp, Log, TEXT("OK startup stat fps enabled=%d"), VP->IsStatEnabled(TEXT("FPS")) ? 1 : 0);
		}
	}
}

void AOpenKoikatsuPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);
    // Street locomotion owns its input until the common preview mode is active.
    if(!PaintPreviewActive&&!GetPreviewCameraOwner())return;
	if (UGameViewportClient* VP = GetWorld() ? GetWorld()->GetGameViewport() : nullptr)
	{
		if (VP->GetMouseCaptureMode() != EMouseCaptureMode::NoCapture)
		{
			VP->SetMouseCaptureMode(EMouseCaptureMode::NoCapture);
		}
	}
 float X=0.f,Y=0.f;const bool HasMouse=GetMousePosition(X,Y);
 const bool Focused=!FSlateApplication::IsInitialized()||FSlateApplication::Get().IsActive();
 if(!Focused)PaintAimHeld=false;
 const bool OverUI=CursorOverInteractiveUI();const bool Left=(GetCameraMouseButtons()&1)!=0;
 bool Fire=PaintPreviewActive&&UpdatePaintGesture(Left,HasMouse,Focused,OverUI);
#if !UE_BUILD_SHIPPING
 if(ScriptedPaintAim.IsSet())Fire=PaintPreviewActive&&bScriptedPaintFire;
#endif
 UpdatePaintAim(Fire);
 const bool BodyEnabled=bBodyTouchEnabled&&BodyInteraction.IsReady()&&BodyInteraction.Owner.Get()==GetPreviewCameraOwner();
 int32 BodyHit=INDEX_NONE;double BodyDistance=0.;
 if(BodyEnabled&&HasMouse&&Focused&&!OverUI&&!PaintAimHeld&&!PaintOwnsLeft){
  FVector Origin,Direction;if(DeprojectMousePositionToWorld(Origin,Direction))BodyHit=BodyInteraction.Query(Origin,Direction,BodyDistance);
 }
 const bool AnchorValid=BodyEnabled&&BodyGesture.Revision==BodyInteraction.Revision;
 const auto BodyEvent=BodyGesture.Update(BodyEnabled,Left,HasMouse,Focused,OverUI,PaintAimHeld||PaintOwnsLeft,
  BodyHit==INDEX_NONE?0:uint64(BodyHit+1),BodyInteraction.Revision,AnchorValid,FVector2D(X,Y));
 if(BodyEvent.Event==EOKBodyGestureEvent::Press)BodyInteraction.Press(int32(BodyEvent.Target-1));
 if(BodyEvent.Event==EOKBodyGestureEvent::Cancel)BodyInteraction.Cancel();
 UpdateCameraMouse(HasMouse,Focused,OverUI,FVector2D(X,Y));
}
void AOpenKoikatsuPlayerController::SetBodyTouchEnabled(bool Enabled){
 if(!Enabled){BodyGesture.Cancel();BodyInteraction.Cancel();}
 bBodyTouchEnabled=Enabled;
}
void AOpenKoikatsuPlayerController::UpdatePaintAim(bool Fire)
{
 if(!IsValid(PaintSprayer))return;
 // No visible aim beam exists while idle. Existing packets and surface film
 // keep ticking in the sprayer; stopping emission does not clear them.
 if(!Fire){PaintSprayer->StopSpray();return;}
  FVector Origin=FVector::ZeroVector,Dir=FVector::ForwardVector;bool Aim=false;
#if !UE_BUILD_SHIPPING
  if(ScriptedPaintAim.IsSet()&&PlayerCameraManager){Origin=PlayerCameraManager->GetCameraLocation();Dir=(ScriptedPaintAim.GetValue()-Origin).GetSafeNormal();Aim=!Dir.IsNearlyZero();}
  else
#endif
  Aim=DeprojectMousePositionToWorld(Origin,Dir);
  // An axial jet originating on the sight ray is viewed end-on and can look
  // invisible. Offset the gun, then converge back onto the cursor at the
  // character's depth. Physics packets and Niagara use this identical ray.
  float AimDepth=500.f;
  if(auto* Actor=GetPreviewCameraOwner())AimDepth=FMath::Max(100.f,float(FVector::DotProduct(Actor->OrbitFocus-Origin,Dir)));
  int32 PaintTarget,PaintFace;FVector PaintBary;double PaintTime;
  if(Aim&&(++PaintAimQueries,PaintSprayer->FindPaintHit(Origin,Origin+Dir*5000.,.05,PaintTarget,PaintFace,PaintBary,PaintTime)))AimDepth=FMath::Max(30.f,float(PaintTime*5000.));
  const FRotationMatrix ViewRotation(PlayerCameraManager?PlayerCameraManager->GetCameraRotation():GetControlRotation());
  const FVector Nozzle=Origin+Dir*30.+ViewRotation.GetUnitAxis(EAxis::Y)*12.-ViewRotation.GetUnitAxis(EAxis::Z)*8.;
  const FVector Target=Origin+Dir*AimDepth;
  PaintSprayer->SetSpray(Aim,Nozzle,AOKPaintSprayer::AimDirection(Nozzle,Target,PaintSprayer->Settings.JetSpeed));
}
void AOpenKoikatsuPlayerController::SetPaintPreviewActive(bool Enabled)
{
 if(!Enabled){PaintAimHeld=false;PaintOwnsLeft=false;PaintPreviousLeft=false;CameraDrag.Cancel();}
 PaintPreviewActive=Enabled;
 if(Enabled&&!IsValid(PaintSprayer)&&GetWorld()){
  PaintSprayer=GetWorld()->SpawnActor<AOKPaintSprayer>();
  UE_LOG(LogTemp,Display,TEXT("Paint preview enabled: A + left spray, R clear"));
 }
 if(IsValid(PaintSprayer)){
  if(Enabled)PaintSprayer->SetActorHiddenInGame(false);
  if(!Enabled){PaintSprayer->SetSpray(false,FVector::ZeroVector,FVector::ForwardVector);PaintSprayer->ClearPaint();}
  PaintSprayer->SetControlsVisible(Enabled);PaintSprayer->SetActorTickEnabled(Enabled);
 }
}
bool AOpenKoikatsuPlayerController::UpdatePaintGesture(bool Left,bool HasMouse,bool Focused,bool OverUI)
{
 if(!Focused)PaintAimHeld=false;
 if(!Left||!Focused)PaintOwnsLeft=false;else if(!PaintPreviousLeft&&PaintAimHeld&&HasMouse&&Focused&&!OverUI)PaintOwnsLeft=true;
 PaintPreviousLeft=Left;return PaintOwnsLeft&&PaintAimHeld&&Left&&HasMouse&&Focused&&!OverUI;
}

uint8 AOpenKoikatsuPlayerController::GetCameraMouseButtons() const
{
 // NoCapture game viewports do not forward primary button-down to PlayerInput.
 // Slate tracks physical button state even when a UMG control consumes input.
 // Gesture ownership below still rejects presses which began on that control.
 if(FSlateApplication::IsInitialized()){
  const TSet<FKey>& Keys=FSlateApplication::Get().GetPressedMouseButtons();
  return (Keys.Contains(EKeys::LeftMouseButton)?1:0)|(Keys.Contains(EKeys::RightMouseButton)?2:0)|(Keys.Contains(EKeys::MiddleMouseButton)?4:0);
 }
 return (IsInputKeyDown(EKeys::LeftMouseButton)?1:0)|(IsInputKeyDown(EKeys::RightMouseButton)?2:0)|(IsInputKeyDown(EKeys::MiddleMouseButton)?4:0);
}

void AOpenKoikatsuPlayerController::UpdateCameraMouse(bool HasMouse,bool Focused,bool OverUI,const FVector2D& Position)
{
 const uint8 Buttons=GetCameraMouseButtons();
 if(PaintOwnsLeft||BodyGesture.Owned||bExternalMouseOwner){CameraDrag.Cancel();CameraDrag.Update(Buttons,HasMouse,Focused,true,Position);return;}
 AKKPreviewActor* Actor=GetPreviewCameraOwner();
 if(Actor!=DragActor.Get()){CameraDrag.Cancel();DragActor=Actor;}
 const FVector2D Delta=CameraDrag.Update(Buttons,HasMouse,Focused,OverUI,Position);
 if(!Actor){CameraDrag.Cancel();return;}
 // BaseCameraControl_Ver2.LateUpdate gives wheel input priority over drag.
 if(CameraDrag.Owned&&WheelFrame!=GFrameCounter)Actor->OrbitMouse(Buttons,Delta.X,Delta.Y);
}

bool AOpenKoikatsuPlayerController::InputKey(const FInputKeyEventArgs& Params)
{
	if(PaintPreviewActive&&(Params.Key==EKeys::A||Params.Key==EKeys::R)){
		if(Params.Key==EKeys::A){
			if(Params.Event==IE_Released)PaintAimHeld=false;
			else if(Params.Event==IE_Pressed){
				const auto Focus=FSlateApplication::IsInitialized()?FSlateApplication::Get().GetKeyboardFocusedWidget():TSharedPtr<SWidget>();
				if(!Focus||(!Focus->GetTypeAsString().Contains(TEXT("Editable"))&&!Focus->GetTypeAsString().Contains(TEXT("SearchBox"))))PaintAimHeld=true;
			}
		}else if(Params.Event==IE_Pressed&&PaintSprayer)PaintSprayer->ClearPaint();
		return true;
	}
	if (Params.Key == EKeys::MouseWheelAxis && !CursorOverInteractiveUI())
	{
		if (AKKPreviewActor* Actor = GetPreviewCameraOwner())
		{
			Actor->OrbitZoom(Params.AmountDepressed);
			if(!FMath::IsNearlyZero(Params.AmountDepressed))WheelFrame=GFrameCounter;
		}
	}
	return Super::InputKey(Params);
}

void AOpenKoikatsuPlayerController::EndPlay(const EEndPlayReason::Type Reason)
{
 SetBodyTouchEnabled(false);
 if(PaintInput&&FSlateApplication::IsInitialized())FSlateApplication::Get().UnregisterInputPreProcessor(PaintInput);
 PaintInput.Reset();if(PaintSprayer)PaintSprayer->Destroy();Super::EndPlay(Reason);
}
