#include "OKStudioScene.h"
#include "OKComboBox.h"

#include "Blueprint/WidgetLayoutLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CheckBox.h"
#include "Components/ComboBoxString.h"
#include "Components/EditableTextBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/Slider.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "KKPreviewActor.h"
#include "KKRustExtract.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "OKCharacterController.h"
#include "OKMotionComboBox.h"
#include "OKSceneActor.h"
#include "OKStudioGame.h"
#include "OKVideoMotionPanel.h"
#include "OpenKoikatsuPlayerController.h"
#include "Rendering/DrawElements.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateBrush.h"

namespace
{
	const FLinearColor PanelColor(.02f, .035f, .06f, .92f);
	const TCHAR* const InterpNames[] = {TEXT("線性"), TEXT("平滑"), TEXT("常數")};

	// Original (Unity) coordinates for the fields: metres, Y up, left-handed.
	FVector ToUnityPos(const FVector& P) { return FVector(-P.X / 100., P.Z / 100., P.Y / 100.); }
	FVector FromUnityPos(const FVector& U) { return FVector(-U.X * 100., U.Z * 100., U.Y * 100.); }
	FVector ToUnityScale(const FVector& S) { return FVector(S.X, S.Z, S.Y); }
	FVector FromUnityScale(const FVector& U) { return FVector(U.X, U.Z, U.Y); }
	// Unity <-> Unreal rotation basis (same as OKUnrealLocalToUnity / OkLocalToUe in KKSkeletalBuilder).
	const FQuat UnityBasis(0., 0.7071067811865475, 0.7071067811865475, 0.);
	// Unity eulerAngles (degrees): rotation = Y * X * Z.
	FVector UnityEuler(const FQuat& Rotation)
	{
		const FQuat Q = (UnityBasis * Rotation * UnityBasis).GetNormalized();
		const double X = Q.X, Y = Q.Y, Z = Q.Z, W = Q.W;
		const double Ex = FMath::Asin(FMath::Clamp(2. * (W * X - Y * Z), -1., 1.));
		const double Ey = FMath::Atan2(2. * (X * Z + W * Y), 1. - 2. * (X * X + Y * Y));
		const double Ez = FMath::Atan2(2. * (X * Y + W * Z), 1. - 2. * (X * X + Z * Z));
		auto Deg = [](double V) { double D = FMath::RadiansToDegrees(V); return D < -1e-4 ? D + 360. : D; };
		return FVector(Deg(Ex), Deg(Ey), Deg(Ez));
	}
	FQuat FromUnityEuler(const FVector& E)
	{
		auto Axis = [](int32 I, double Deg) { const double H = FMath::DegreesToRadians(Deg) * .5; FQuat Q(0., 0., 0., FMath::Cos(H)); (I == 0 ? Q.X : I == 1 ? Q.Y : Q.Z) = FMath::Sin(H); return Q; };
		const FQuat Q = (Axis(1, E.Y) * Axis(0, E.X) * Axis(2, E.Z)).GetNormalized();
		return (UnityBasis * Q * UnityBasis).GetNormalized();
	}

	// Same rule as the player controller: only layout / viewport widgets leave the camera free.
	bool CursorOverUI()
	{
		if (!FSlateApplication::IsInitialized()) return false;
		FSlateApplication& App = FSlateApplication::Get();
		const FWidgetPath Path = App.LocateWindowUnderMouse(App.GetCursorPos(), App.GetInteractiveTopLevelWindows());
		if (!Path.IsValid() || Path.Widgets.Num() == 0) return false;
		const FString Type = Path.Widgets.Last().Widget->GetTypeAsString();
		return Type != TEXT("SViewport") && Type != TEXT("SGameLayerManager") && Type != TEXT("SWindow") && Type != TEXT("SOverlay")
			&& Type != TEXT("SConstraintCanvas") && Type != TEXT("SObjectWidget");
	}

	// The guide is drawn at waist height: at the feet it sits under the timeline in the default framing.
	// Moves and scales do not depend on the pivot; rotations still turn about the character's root.
	FVector GuidePivot(const AActor* A) { return A->GetActorLocation() + FVector(0., 0., 85. * A->GetActorScale3D().Z); }

	double SegmentDistance(const FVector2D& P, const FVector2D& A, const FVector2D& B)
	{
		const FVector2D AB = B - A;
		const double T = AB.SizeSquared() > 1e-6 ? FMath::Clamp(FVector2D::DotProduct(P - A, AB) / AB.SizeSquared(), 0., 1.) : 0.;
		return FVector2D::Distance(P, A + AB * T);
	}

	TArray<TSharedPtr<FJsonValue>> Numbers(std::initializer_list<double> Values)
	{
		TArray<TSharedPtr<FJsonValue>> Out;
		for (double V : Values) Out.Add(MakeShared<FJsonValueNumber>(V));
		return Out;
	}
	bool ReadNumbers(const TSharedPtr<FJsonObject>& O, const TCHAR* Field, int32 Count, double* Out)
	{
		const TArray<TSharedPtr<FJsonValue>>* A;
		if (!O || !O->TryGetArrayField(Field, A) || A->Num() != Count) return false;
		for (int32 I = 0; I < Count; ++I) if (!(*A)[I]->TryGetNumber(Out[I]) || !FMath::IsFinite(Out[I])) return false;
		return true;
	}
	FString ErrorText(const TCHAR* What, const FString& Path)
	{
		const uint32 Code = FPlatformMisc::GetLastError();
		TCHAR Buffer[512] = {};
		FPlatformMisc::GetSystemErrorMessage(Buffer, 511, Code);
		return FString::Printf(TEXT("%s：%s（系統錯誤 %u：%s）"), What, *Path, Code, Buffer);
	}
}

// ---------------------------------------------------------------- track

bool FOKStudioTrack::Evaluate(float Time, FTransform& Out) const
{
	if (Keys.IsEmpty()) return false;
	if (Time <= Keys[0].Time) { Out = FTransform(Keys[0].Rot, Keys[0].Pos, Keys[0].Scale); return true; }
	for (int32 I = 0; I + 1 < Keys.Num(); ++I)
	{
		const FOKStudioKey& A = Keys[I];
		const FOKStudioKey& B = Keys[I + 1];
		if (Time > B.Time) continue;
		double T = B.Time > A.Time ? (Time - A.Time) / (B.Time - A.Time) : 1.;
		if (A.Interp == 1) T = T * T * (3. - 2. * T);
		else if (A.Interp == 2) T = 0.;
		Out = FTransform(FQuat::Slerp(A.Rot, B.Rot, T).GetNormalized(), FMath::Lerp(A.Pos, B.Pos, T), FMath::Lerp(A.Scale, B.Scale, T));
		return true;
	}
	const FOKStudioKey& L = Keys.Last();
	Out = FTransform(L.Rot, L.Pos, L.Scale);
	return true;
}

int32 FOKStudioTrack::KeyAt(float Time, float Tolerance) const
{
	int32 Best = INDEX_NONE;
	float Distance = Tolerance;
	for (int32 I = 0; I < Keys.Num(); ++I)
		if (FMath::Abs(Keys[I].Time - Time) <= Distance) { Distance = FMath::Abs(Keys[I].Time - Time); Best = I; }
	return Best;
}

// ---------------------------------------------------------------- guide overlay

FVector UOKStudioWidget::GuideAxis(int32 Axis)
{
	// Original X / Y(up) / Z expressed in Unreal space (see OKUnrealLocalToUnity).
	return Axis == 0 ? FVector(-1, 0, 0) : Axis == 1 ? FVector(0, 0, 1) : FVector(0, 1, 0);
}

FLinearColor UOKStudioWidget::AxisColor(int32 Axis)
{
	return Axis == 0 ? FLinearColor(1.f, .22f, .2f) : Axis == 1 ? FLinearColor(.3f, 1.f, .3f) : Axis == 2 ? FLinearColor(.3f, .55f, 1.f) : FLinearColor(.9f, .9f, .9f);
}

bool UOKStudioWidget::BuildGuide(TArray<FHandle>& Out, FVector2D& Center) const
{
	Out.Reset();
	APlayerController* PC = GetOwningPlayer();
	const AKKPreviewActor* Actor = ObjectActor(Selected);
	if (!PC || !Actor || !PC->PlayerCameraManager) return false;
	auto Project = [PC](const FVector& World, FVector2D& Screen) { return UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition(PC, World, Screen, true); };
	const FVector O = GuidePivot(Actor);
	FVector2D Side;
	if (!Project(O, Center) || !Project(O + PC->PlayerCameraManager->GetCameraRotation().RotateVector(FVector(0, 1, 0)) * 10., Side)) return false;
	const double PixelsPer10 = FVector2D::Distance(Center, Side);
	if (PixelsPer10 < 1e-3) return false;
	const double CmPerPixel = 10. / PixelsPer10;
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		FHandle H;
		H.Axis = Axis;
		if (GuideMode == 1)
		{
			const FVector A = GuideAxis(Axis);
			const FVector U = (FMath::Abs(A.Z) < .9 ? (A ^ FVector::UpVector) : (A ^ FVector::ForwardVector)).GetSafeNormal();
			const FVector V = A ^ U;  // increasing angle = positive rotation about A
			const double Radius = 85. * CmPerPixel;
			for (int32 I = 0; I < 48; ++I)
			{
				const double T = 2. * PI * I / 48.;
				FVector2D P;
				if (!Project(O + (U * FMath::Cos(T) + V * FMath::Sin(T)) * Radius, P)) { H.Points.Reset(); break; }
				H.Points.Add(P);
			}
			H.bClosed = true;
		}
		else
		{
			const FVector Direction = GuideMode == 2 ? Actor->GetActorQuat().RotateVector(GuideAxis(Axis)) : GuideAxis(Axis);
			FVector2D End;
			if (!Project(O + Direction * 10., End)) continue;
			const FVector2D D = End - Center;
			if (D.Size() < .5) continue;
			const FVector2D N = D / D.Size();
			H.Points = {Center + N * 14., Center + N * (GuideMode == 2 ? 95. : 110.)};
		}
		if (H.Points.Num() >= 2) Out.Add(MoveTemp(H));
	}
	if (GuideMode == 2)
	{
		FHandle H;
		H.Axis = 3;
		H.Points = {Center + FVector2D(-9, -9), Center + FVector2D(9, -9), Center + FVector2D(9, 9), Center + FVector2D(-9, 9)};
		H.bClosed = true;
		Out.Add(MoveTemp(H));
	}
	return true;
}

int32 UOKStudioGuide::NativePaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& Culling, FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const
{
	const UOKStudioWidget* S = Studio.Get();
	APlayerController* PC = S ? S->GetOwningPlayer() : nullptr;
	if (!S || !PC) return Layer;
	const FPaintGeometry Paint = Geometry.ToPaintGeometry();
	auto Line = [&](const TArray<FVector2D>& Points, const FLinearColor& Color, float Width, bool bClosed)
	{
		TArray<FVector2f> P;
		for (const FVector2D& V : Points) P.Add(FVector2f(V));
		if (bClosed && P.Num() > 2) { const FVector2f First = P[0]; P.Add(First); }
		FSlateDrawElement::MakeLines(Out, Layer, Paint, P, ESlateDrawEffect::None, Color, true, Width);
	};
	// Floor grid: 50 cm cells, 10 m square around the studio origin (like the original's map grid).
	for (int32 Pass = 0; Pass < 2; ++Pass)
		for (int32 Line10 = -10; Line10 <= 10; ++Line10)
		{
			TArray<FVector2D> Run;
			for (int32 Step = -10; Step <= 10; ++Step)
			{
				const FVector W = S->Origin + (Pass == 0 ? FVector(Line10 * 50., Step * 50., 0.) : FVector(Step * 50., Line10 * 50., 0.));
				FVector2D P;
				if (UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition(PC, FVector(W.X, W.Y, S->FloorZ), P, true)) Run.Add(P);
				else { if (Run.Num() > 1) Line(Run, FLinearColor(1, 1, 1, .25f), 1.f, false); Run.Reset(); }
			}
			if (Run.Num() > 1) Line(Run, Line10 == 0 ? FLinearColor(1, 1, 1, .55f) : FLinearColor(1, 1, 1, .25f), 1.f, false);
		}
	TArray<UOKStudioWidget::FHandle> Handles;
	FVector2D Center;
	const bool bGuide = S->BuildGuide(Handles, Center);
	if (!bGuide) return Layer + 1;
	static const FSlateColorBrush White(FLinearColor::White);
	for (const UOKStudioWidget::FHandle& H : Handles)
	{
		const bool bHot = H.Axis == (S->DragAxis >= 0 ? S->DragAxis : S->HoverAxis);
		const FLinearColor Color = bHot ? FLinearColor(1.f, .9f, .2f) : UOKStudioWidget::AxisColor(H.Axis);
		Line(H.Points, Color, bHot ? 4.f : 2.5f, H.bClosed);
		if (!H.bClosed && H.Points.Num() == 2)
		{
			const FVector2D Tip = H.Points[1], N = (H.Points[1] - H.Points[0]).GetSafeNormal(), Perp(-N.Y, N.X);
			if (S->GuideMode == 0) Line({Tip + N * 12., Tip + Perp * 6., Tip - Perp * 6.}, Color, 2.5f, true);
			else FSlateDrawElement::MakeBox(Out, Layer, Geometry.ToPaintGeometry(FVector2f(10.f, 10.f), FSlateLayoutTransform(FVector2f(Tip - FVector2D(5, 5)))), &White, ESlateDrawEffect::None, Color);
		}
	}
	return Layer + 1;
}

// ---------------------------------------------------------------- timeline

float UOKStudioTimeline::TimeAt(const FGeometry& Geometry, const FVector2D& Screen) const
{
	const UOKStudioWidget* S = Studio.Get();
	const FVector2D Local = Geometry.AbsoluteToLocal(Screen);
	const double Width = FMath::Max(10., Geometry.GetLocalSize().X - OKStudio::Gutter - 10.);
	return S ? FMath::Clamp(float((Local.X - OKStudio::Gutter) / Width * S->Length), 0.f, S->Length) : 0.f;
}

int32 UOKStudioTimeline::NativePaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& Culling, FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const
{
	const UOKStudioWidget* S = Studio.Get();
	if (!S) return Layer;
	static const FSlateColorBrush White(FLinearColor::White);
	const FVector2D Size = Geometry.GetLocalSize();
	const double Width = FMath::Max(10., Size.X - OKStudio::Gutter - 10.);
	auto X = [&](float T) { return OKStudio::Gutter + T / FMath::Max(S->Length, .001f) * Width; };
	auto Box = [&](const FVector2D& At, const FVector2D& Extent, const FLinearColor& Color)
	{ FSlateDrawElement::MakeBox(Out, Layer, Geometry.ToPaintGeometry(FVector2f(Extent), FSlateLayoutTransform(FVector2f(At))), &White, ESlateDrawEffect::None, Color); };
	auto Text = [&](const FString& Value, const FVector2D& At, const FLinearColor& Color)
	{ FSlateDrawElement::MakeText(Out, Layer + 1, Geometry.ToPaintGeometry(FVector2f(200.f, 16.f), FSlateLayoutTransform(FVector2f(At))), Value, FCoreStyle::GetDefaultFontStyle("Regular", 9), ESlateDrawEffect::None, Color); };
	auto Line = [&](const FVector2D& A, const FVector2D& B, const FLinearColor& Color, float W)
	{ TArray<FVector2f> P{FVector2f(A), FVector2f(B)}; FSlateDrawElement::MakeLines(Out, Layer + 1, Geometry.ToPaintGeometry(), P, ESlateDrawEffect::None, Color, true, W); };

	Box(FVector2D::ZeroVector, Size, FLinearColor(.01f, .015f, .03f, .95f));
	// OKStudio::Ruler: a tick per second (per 5 s on long timelines), frame ticks when zoomed in enough.
	const float Major = S->Length > 60.f ? 10.f : S->Length > 20.f ? 5.f : 1.f;
	for (float T = 0.f; T <= S->Length + 1e-3f; T += Major)
	{
		Line(FVector2D(X(T), 2), FVector2D(X(T), OKStudio::Ruler), FLinearColor(1, 1, 1, .5f), 1.f);
		Text(FString::Printf(TEXT("%gs"), T), FVector2D(X(T) + 3, 2), FLinearColor(.8f, .85f, .9f));
	}
	for (int32 Track = 0; Track < 2; ++Track)
	{
		const double Y = OKStudio::Ruler + Track * OKStudio::Row;
		Box(FVector2D(0, Y + 1), FVector2D(Size.X, OKStudio::Row - 2), Track == S->Selected ? FLinearColor(.1f, .2f, .35f, .9f) : FLinearColor(.05f, .07f, .1f, .9f));
		Text(Track == 0 ? TEXT("女角") : TEXT("男角"), FVector2D(8, Y + 4), S->ObjectActor(Track) ? FLinearColor::White : FLinearColor(.5f, .5f, .5f));
		const TArray<FOKStudioKey>& Keys = S->Tracks[Track].Keys;
		for (int32 I = 0; I < Keys.Num(); ++I)
		{
			const FVector2D C(X(Keys[I].Time), Y + OKStudio::Row * .5);
			const bool bCurrent = FMath::Abs(Keys[I].Time - S->Time) < .5f / OKStudio::Fps;
			const FLinearColor Color = bCurrent ? FLinearColor(1.f, .85f, .2f) : Keys[I].Interp == 2 ? FLinearColor(.9f, .4f, .3f) : Keys[I].Interp == 1 ? FLinearColor(.4f, .9f, .6f) : FLinearColor(.85f, .9f, 1.f);
			TArray<FVector2f> D{FVector2f(C + FVector2D(0, -7)), FVector2f(C + FVector2D(7, 0)), FVector2f(C + FVector2D(0, 7)), FVector2f(C + FVector2D(-7, 0)), FVector2f(C + FVector2D(0, -7))};
			FSlateDrawElement::MakeLines(Out, Layer + 1, Geometry.ToPaintGeometry(), D, ESlateDrawEffect::None, Color, true, 3.f);
		}
	}
	const double Head = X(S->Time);
	Line(FVector2D(Head, 0), FVector2D(Head, Size.Y), FLinearColor(1.f, .3f, .3f), 2.f);
	return Layer + 2;
}

FReply UOKStudioTimeline::NativeOnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event)
{
	UOKStudioWidget* S = Studio.Get();
	if (!S || Event.GetEffectingButton() != EKeys::LeftMouseButton) return FReply::Unhandled();
	const FVector2D Local = Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition());
	const int32 Track = FMath::FloorToInt((Local.Y - OKStudio::Ruler) / OKStudio::Row);
	S->bTimelineOwnsMouse = true;
	if (Track == 0 || Track == 1)
	{
		if (Track != S->Selected) Track == 0 ? S->OnSelectFemale() : S->OnSelectMale();
		const double Width = FMath::Max(10., Geometry.GetLocalSize().X - OKStudio::Gutter - 10.);
		const TArray<FOKStudioKey>& Keys = S->Tracks[Track].Keys;
		for (int32 I = 0; I < Keys.Num(); ++I)
			if (FMath::Abs(OKStudio::Gutter + Keys[I].Time / FMath::Max(S->Length, .001f) * Width - Local.X) < 7.)
			{
				DragTrack = Track;
				DragKey = I;
				S->SetTime(Keys[I].Time);
				return FReply::Handled().CaptureMouse(TakeWidget());
			}
	}
	bScrubbing = true;
	S->SetPlaying(false);
	S->SetTime(TimeAt(Geometry, Event.GetScreenSpacePosition()));
	return FReply::Handled().CaptureMouse(TakeWidget());
}

FReply UOKStudioTimeline::NativeOnMouseMove(const FGeometry& Geometry, const FPointerEvent& Event)
{
	UOKStudioWidget* S = Studio.Get();
	if (!S || !HasMouseCapture()) return FReply::Unhandled();
	const float T = FMath::RoundToFloat(TimeAt(Geometry, Event.GetScreenSpacePosition()) * OKStudio::Fps) / OKStudio::Fps;
	if (DragKey != INDEX_NONE) { S->MoveKey(DragTrack, DragKey, T); DragKey = S->Tracks[DragTrack].KeyAt(T, .5f / OKStudio::Fps); }
	else if (bScrubbing) S->SetTime(TimeAt(Geometry, Event.GetScreenSpacePosition()));
	return FReply::Handled();
}

FReply UOKStudioTimeline::NativeOnMouseButtonUp(const FGeometry& Geometry, const FPointerEvent& Event)
{
	if (!HasMouseCapture()) return FReply::Unhandled();
	DragKey = DragTrack = INDEX_NONE;
	bScrubbing = false;
	if (UOKStudioWidget* S = Studio.Get()) S->bTimelineOwnsMouse = false;
	return FReply::Handled().ReleaseMouseCapture();
}

void UOKStudioTimeline::NativeOnMouseCaptureLost(const FCaptureLostEvent& Event)
{
	DragKey = DragTrack = INDEX_NONE;
	bScrubbing = false;
	if (UOKStudioWidget* S = Studio.Get()) S->bTimelineOwnsMouse = false;
}

// ---------------------------------------------------------------- studio

UOKStudioWidget* UOKStudioWidget::Open(UOKCharacterController* G)
{
	if (!G || !IsValid(G->PreviewActor)) return nullptr;
	UOKStudioWidget* W = CreateWidget<UOKStudioWidget>(G->GetOwningPlayer());
	if (!W) return nullptr;
	W->Gallery = G;
	G->bStudioMode = true;
	G->OnStopPair();  // studio mode: ends native pair playback, keeps both characters
	G->bVideoPairActive = false;
	W->Origin = G->PreviewActor->GetActorLocation();
	W->FloorZ = G->PreviewActor->GetComponentsBoundingBox(true).Min.Z;
	if (G->ShapePanel) G->ShapePanel->SetVisibility(ESlateVisibility::Collapsed);
	if (G->MotionPanel) G->MotionPanel->SetVisibility(ESlateVisibility::Collapsed);
	if (auto* PC = Cast<AOpenKoikatsuPlayerController>(G->GetOwningPlayer())) { PC->SetPaintPreviewActive(false); PC->bExternalMouseOwner = false; }
	W->AddToViewport(20);
	W->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	W->FitLength();
	W->SetStatus(TEXT("工作室：選擇左側物件，拖曳操作軸或輸入座標；在時間軸上打關鍵幀。"));
	UE_LOG(LogTemp, Display, TEXT("OK studio open card=%s"), G->Entries.IsValidIndex(G->SelectedIndex) ? *G->Entries[G->SelectedIndex].Path : TEXT(""));
	return W;
}

void UOKStudioWidget::Close()
{
	if (bClosing) return;
	bClosing = true;
	SetPlaying(false);
	UOKCharacterController* G = Gallery;
	for (int32 I = 0; I < 2; ++I)
		if (AKKPreviewActor* A = ObjectActor(I))
		{
			A->AnimationPlaybackSpeed = 1.f;
			if (bHadOriginal[I]) A->SetActorTransform(Original[I]);
		}
	if (auto* PC = Cast<AOpenKoikatsuPlayerController>(GetOwningPlayer())) PC->bExternalMouseOwner = false;
	if (VideoPanel) VideoPanel->PlayOverride = nullptr;
	RestoreBorrowed();
	RemoveFromParent();
	if (G)
	{
		G->bStudioMode = false;
		G->StudioWidget = nullptr;
		G->SetModelLayout(G->bModelLayout);
		if (G->VideoMotionPanel) G->VideoMotionPanel->Refresh();
		G->ApplyClickInputMode();
	}
	UE_LOG(LogTemp, Display, TEXT("OK studio closed"));
}

AKKPreviewActor* UOKStudioWidget::ObjectActor(int32 Index) const
{
	if (!Gallery) return nullptr;
	AKKPreviewActor* A = Index == 0 ? Gallery->PreviewActor.Get() : Gallery->PartnerActor.Get();
	return IsValid(A) && A->Session.IsOpen() ? A : nullptr;
}

FString UOKStudioWidget::ObjectName(int32 Index) const
{
	if (!Gallery) return FString();
	if (Index == 0)
	{
		const FString Name = (Gallery->Selected.LastName + TEXT(" ") + Gallery->Selected.FirstName).TrimStartAndEnd();
		return Name.IsEmpty() ? TEXT("女角") : Name;
	}
	if (!ObjectActor(1)) return TEXT("（未載入）");
	return FPaths::GetBaseFilename(Gallery->PartnerCardPath);
}

void UOKStudioWidget::SetStatus(const FString& Text, bool bError)
{
	Status = Text;
	if (StatusLabel)
	{
		StatusLabel->SetText(FText::FromString(Text));
		StatusLabel->SetColorAndOpacity(FSlateColor(bError ? FLinearColor(1.f, .4f, .3f) : FLinearColor(1.f, .92f, .45f)));
	}
	if (bError) UE_LOG(LogTemp, Warning, TEXT("OK studio %s"), *Text);
}

void UOKStudioWidget::SetTime(float NewTime)
{
	Time = FMath::Clamp(FMath::IsFinite(NewTime) ? NewTime : 0.f, 0.f, Length);
}

void UOKStudioWidget::SetPlaying(bool bPlay)
{
	bPlaying = bPlay;
	if (bPlay && Time >= Length - 1e-4f) Time = 0.f;
	if (PlayLabel) PlayLabel->SetText(FText::FromString(bPlay ? TEXT("❚❚ 暫停") : TEXT("▶ 播放")));
}

void UOKStudioWidget::FitLength()
{
	if (!FollowLength || FollowLength->IsChecked())
	{
		float L = 0.f;
		for (int32 I = 0; I < 2; ++I)
			if (const AKKPreviewActor* A = ObjectActor(I); A && A->PoseMode == 2)
				L = FMath::Max(L, A->AnimDuration / FMath::Max(Tracks[I].Speed, .05f));
		for (const FOKStudioTrack& T : Tracks) if (!T.Keys.IsEmpty()) L = FMath::Max(L, T.Keys.Last().Time);
		if (L > .05f) Length = FMath::Min(L, 3600.f);
	}
	Time = FMath::Min(Time, Length);
	if (LengthBox) LengthBox->SetText(FText::FromString(FString::Printf(TEXT("%.2f"), Length)));
}

void UOKStudioWidget::AddKey(int32 Track)
{
	AKKPreviewActor* A = ObjectActor(Track);
	if (!A) { SetStatus(TEXT("這個物件尚未載入，無法打關鍵幀。"), true); return; }
	const float T = FMath::RoundToFloat(Time * OKStudio::Fps) / OKStudio::Fps;
	FOKStudioKey Key;
	Key.Time = T;
	Key.Pos = A->GetActorLocation();
	Key.Rot = A->GetActorQuat();
	Key.Scale = A->GetActorScale3D();
	TArray<FOKStudioKey>& Keys = Tracks[Track].Keys;
	const int32 Existing = Tracks[Track].KeyAt(T, .5f / OKStudio::Fps);
	if (Existing != INDEX_NONE) { Key.Interp = Keys[Existing].Interp; Keys[Existing] = Key; }
	else
	{
		int32 At = 0;
		while (At < Keys.Num() && Keys[At].Time < T) ++At;
		Keys.Insert(Key, At);
	}
	if (Track == Selected) { SelectedTrackKey = Tracks[Track].KeyAt(T, .5f / OKStudio::Fps); bSelectedEdited = false; }
	if (T > Length) { Length = T; if (LengthBox) LengthBox->SetText(FText::FromString(FString::Printf(TEXT("%.2f"), Length))); }
	EvaluatedAt = Time;
	SetStatus(FString::Printf(TEXT("%s：%s %.2fs（第 %d 幀）的關鍵幀，共 %d 個"), *ObjectName(Track), Existing != INDEX_NONE ? TEXT("已更新") : TEXT("已新增"), T, FMath::RoundToInt(T * OKStudio::Fps), Keys.Num()));
}

void UOKStudioWidget::DeleteKey(int32 Track)
{
	const int32 Index = Tracks[Track].KeyAt(Time, .5f / OKStudio::Fps);
	if (Index == INDEX_NONE) { SetStatus(TEXT("目前時間沒有關鍵幀；先用「上一鍵／下一鍵」移到關鍵幀上。"), true); return; }
	Tracks[Track].Keys.RemoveAt(Index);
	EvaluatedAt = -1.f;
	SetStatus(FString::Printf(TEXT("已刪除 %s 在 %.2fs 的關鍵幀，剩 %d 個"), *ObjectName(Track), Time, Tracks[Track].Keys.Num()));
}

void UOKStudioWidget::MoveKey(int32 Track, int32 Key, float NewTime)
{
	TArray<FOKStudioKey>& Keys = Tracks[Track].Keys;
	if (!Keys.IsValidIndex(Key)) return;
	NewTime = FMath::Clamp(NewTime, 0.f, Length);
	const int32 Other = Tracks[Track].KeyAt(NewTime, .5f / OKStudio::Fps);
	if (Other != INDEX_NONE && Other != Key) return;  // never stack two keys on one frame
	FOKStudioKey Moved = Keys[Key];
	Moved.Time = NewTime;
	Keys.RemoveAt(Key);
	int32 At = 0;
	while (At < Keys.Num() && Keys[At].Time < NewTime) ++At;
	Keys.Insert(Moved, At);
	Time = NewTime;
	EvaluatedAt = -1.f;
}

void UOKStudioWidget::EditSelected(const FTransform& Transform, bool bCommit)
{
	AKKPreviewActor* A = ObjectActor(Selected);
	if (!A) return;
	FTransform T = Transform;
	FVector S = T.GetScale3D();
	S = FVector(FMath::Clamp(S.X, .01, 100.), FMath::Clamp(S.Y, .01, 100.), FMath::Clamp(S.Z, .01, 100.));
	T.SetScale3D(S);
	A->SetActorTransform(T);
	bSelectedEdited = true;
	EvaluatedAt = Time;
	if (bCommit)
	{
		if (AutoKey && AutoKey->IsChecked()) AddKey(Selected);
		else if (!Tracks[Selected].Keys.IsEmpty()) SetStatus(TEXT("尚未打關鍵幀：移動時間後會回到時間軸上的位置。按「打關鍵幀」(K) 保留。"));
	}
}

bool UOKStudioWidget::HitGuide(const FVector2D& Mouse, double Radius, FHitResult2D& Out) const
{
	TArray<FHandle> Handles;
	if (!ObjectActor(Selected) || !BuildGuide(Handles, Out.Center)) return false;
	double Best = Radius;
	bool bHit = false;
	for (const FHandle& H : Handles)
	{
		if (H.Axis == 3)
		{
			if (FMath::Abs(Mouse.X - Out.Center.X) < 11. && FMath::Abs(Mouse.Y - Out.Center.Y) < 11.) { Out.Handle = H; Out.Segment = 0; return true; }
			continue;
		}
		const int32 Count = H.bClosed ? H.Points.Num() : H.Points.Num() - 1;
		for (int32 I = 0; I < Count; ++I)
		{
			const double D = SegmentDistance(Mouse, H.Points[I], H.Points[(I + 1) % H.Points.Num()]);
			if (D < Best) { Best = D; Out.Handle = H; Out.Segment = I; bHit = true; }
		}
	}
	return bHit;
}

void UOKStudioWidget::BeginDrag(const FHitResult2D& Hit, const FVector2D& Mouse)
{
	AKKPreviewActor* A = ObjectActor(Selected);
	APlayerController* PC = GetOwningPlayer();
	if (!A || !PC) return;
	DragAxis = Hit.Handle.Axis;
	DragStart = Mouse;
	DragFrom = A->GetActorTransform();
	DragUnits = 1.;
	FVector2D Side;
	if (PC->PlayerCameraManager && UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition(PC, GuidePivot(A) + PC->PlayerCameraManager->GetCameraRotation().RotateVector(FVector(0, 1, 0)) * 10., Side, true))
		DragUnits = 10. / FMath::Max(FVector2D::Distance(Side, Hit.Center), 1e-3);
	const TArray<FVector2D>& P = Hit.Handle.Points;
	if (DragAxis == 3) DragDirection = FVector2D(1, 0);
	else if (Hit.Handle.bClosed) DragDirection = (P[(Hit.Segment + 1) % P.Num()] - P[Hit.Segment]).GetSafeNormal();
	else
	{
		// Move along the axis' screen projection; units per pixel follow that projection.
		DragDirection = (P[1] - P[0]).GetSafeNormal();
		FVector2D Tip;
		if (GuideMode == 0 && UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition(PC, GuidePivot(A) + GuideAxis(DragAxis) * 10., Tip, true))
			DragUnits = 10. / FMath::Max(FVector2D::Distance(Tip, Hit.Center), 1e-3);
	}
}

void UOKStudioWidget::DragTo(const FVector2D& Mouse)
{
	if (DragAxis == INDEX_NONE) return;
	const FVector2D M = Mouse - DragStart;
	FTransform T = DragFrom;
	if (GuideMode == 0)
		T.SetLocation(DragFrom.GetLocation() + GuideAxis(DragAxis) * (FVector2D::DotProduct(M, DragDirection) * DragUnits));
	else if (GuideMode == 1)
		T.SetRotation((FQuat(GuideAxis(DragAxis), FVector2D::DotProduct(M, DragDirection) / 85.) * DragFrom.GetRotation()).GetNormalized());
	else
	{
		const double Factor = FMath::Exp((DragAxis == 3 ? M.X - M.Y : FVector2D::DotProduct(M, DragDirection)) / 100.);
		FVector S = DragFrom.GetScale3D();
		if (DragAxis == 3) S *= Factor;
		else if (DragAxis == 0) S.X *= Factor;
		else if (DragAxis == 1) S.Z *= Factor;
		else S.Y *= Factor;
		T.SetScale3D(S);
	}
	EditSelected(T, false);
}

void UOKStudioWidget::UpdateGuide()
{
	if (!GetOwningPlayer() || !FSlateApplication::IsInitialized()) return;
	const bool bLeft = FSlateApplication::Get().GetPressedMouseButtons().Contains(EKeys::LeftMouseButton);
	const bool bPress = bLeft && !bPreviousLeft;
	bPreviousLeft = bLeft;
	const FVector2D Mouse = UWidgetLayoutLibrary::GetMousePositionOnViewport(this);
	AKKPreviewActor* A = ObjectActor(Selected);
	if (DragAxis != INDEX_NONE)
	{
		if (!A || !bLeft)
		{
			if (A) EditSelected(A->GetActorTransform(), true);
			DragAxis = INDEX_NONE;
			return;
		}
		DragTo(Mouse);
		return;
	}
	HoverAxis = INDEX_NONE;
	FHitResult2D Hit;
	if (!A || bTimelineOwnsMouse || CursorOverUI() || !HitGuide(Mouse, 8., Hit)) return;
	HoverAxis = Hit.Handle.Axis;
	if (bPress) BeginDrag(Hit, Mouse);
}

void UOKStudioWidget::NativeTick(const FGeometry& Geometry, float Delta)
{
	Super::NativeTick(Geometry, Delta);
	if (bClosing || !Gallery) return;
	if (!IsValid(Gallery->PreviewActor))
	{
		if (!Gallery->bLoadInFlight) Close();  // a reload swaps the character; otherwise it is gone
		return;
	}
	for (int32 I = 0; I < 2; ++I)
	{
		AKKPreviewActor* A = ObjectActor(I);
		if (A == Seen[I].Get()) continue;
		Seen[I] = A;
		EvaluatedAt = -1.f;
		if (!A) continue;
		if (!bHadOriginal[I]) { Original[I] = A->GetActorTransform(); bHadOriginal[I] = true; }
		if (I == 1)
		{
			if (PendingMaleScene && (PendingMaleCard.IsEmpty() || FPaths::IsSamePath(PendingMaleCard, Gallery->PartnerCardPath)))
			{
				const TSharedPtr<FJsonObject> Male = PendingMaleScene;
				PendingMaleScene.Reset();
				FString Clip;
				Male->TryGetStringField(TEXT("clip"), Clip);
				if (!Clip.IsEmpty() && !PlayOn(1, Clip)) SetStatus(TEXT("男角動作無法播放：") + Clip, true);
			}
			else if (!PendingPairMale.IsEmpty()) { Tracks[1].MaleClip = PendingPairMale; PendingPairMale.Empty(); }
			if (Tracks[1].Keys.IsEmpty() && ObjectActor(0)) A->SetActorTransform(ObjectActor(0)->GetActorTransform());
			RefreshMaleMotions();
			FitLength();
		}
	}
	if (APlayerController* PC = GetOwningPlayer())
	{
		if (PC->WasInputKeyJustPressed(EKeys::W)) OnModeMove();
		if (PC->WasInputKeyJustPressed(EKeys::E)) OnModeRotate();
		if (PC->WasInputKeyJustPressed(EKeys::R)) OnModeScale();
		if (PC->WasInputKeyJustPressed(EKeys::K)) OnAddKey();
		if (PC->WasInputKeyJustPressed(EKeys::Delete)) OnDeleteKey();
		if (PC->WasInputKeyJustPressed(EKeys::SpaceBar)) OnPlay();
		if (PC->WasInputKeyJustPressed(EKeys::Left)) OnPrevFrame();
		if (PC->WasInputKeyJustPressed(EKeys::Right)) OnNextFrame();
	}
	if (bPlaying)
	{
		Time += Delta;
		if (Time >= Length)
		{
			if (!Loop || Loop->IsChecked()) Time = Length > 0.f ? FMath::Fmod(Time, Length) : 0.f;
			else { Time = Length; SetPlaying(false); }
		}
	}
	const bool bMoved = Time != EvaluatedAt;
	for (int32 I = 0; I < 2; ++I)
	{
		AKKPreviewActor* A = ObjectActor(I);
		if (!A) continue;
		if (A->PoseMode == 2)
		{
			// The timeline owns the clock: the actor advances by the same delta this frame and
			// lands on Time next frame; paused, it re-poses at Time.
			A->AnimationPlaybackSpeed = bPlaying ? Tracks[I].Speed : 0.f;
			A->AnimPhase = Time * Tracks[I].Speed;
			if (!A->IsActorTickEnabled()) A->SetActorTickEnabled(true);
		}
		FTransform T;
		if (bMoved && !(I == Selected && DragAxis != INDEX_NONE) && Tracks[I].Evaluate(Time, T)) A->SetActorTransform(T);
	}
	if (bMoved) { EvaluatedAt = Time; bSelectedEdited = false; }
	UpdateGuide();
	if (auto* PC = Cast<AOpenKoikatsuPlayerController>(GetOwningPlayer())) PC->bExternalMouseOwner = DragAxis != INDEX_NONE || bTimelineOwnsMouse;
	if (TimeLabel)
	{
		const int32 Key = Tracks[Selected].KeyAt(Time, .5f / OKStudio::Fps);
		TimeLabel->SetText(FText::FromString(FString::Printf(TEXT("t=%.2fs / %.2fs  第 %d 幀%s"), Time, Length, FMath::FloorToInt(Time * OKStudio::Fps + 1e-3f), Key != INDEX_NONE ? TEXT("  ◆關鍵幀") : TEXT(""))));
	}
	const double Now = FPlatformTime::Seconds();
	if (Now - FieldsAt > .1)
	{
		FieldsAt = Now;
		RefreshFields();
		RefreshPanels();
	}
	if (VideoPanel) VideoPanel->Tick();
}

void UOKStudioWidget::RefreshFields()
{
	const AKKPreviewActor* A = ObjectActor(Selected);
	if (!A || Fields.Num() != 9) return;
	for (UEditableTextBox* F : Fields) if (F && F->HasKeyboardFocus()) return;
	const FVector P = ToUnityPos(A->GetActorLocation() - Origin), R = UnityEuler(A->GetActorQuat()), S = ToUnityScale(A->GetActorScale3D());
	const double Values[9] = {P.X, P.Y, P.Z, R.X, R.Y, R.Z, S.X, S.Y, S.Z};
	for (int32 I = 0; I < 9; ++I)
		Fields[I]->SetText(FText::FromString(I >= 3 && I < 6 ? FString::Printf(TEXT("%.1f"), Values[I]) : FString::Printf(TEXT("%.3f"), Values[I])));
}

FTransform UOKStudioWidget::FieldTransform() const
{
	double V[9];
	for (int32 I = 0; I < 9; ++I) V[I] = Fields.IsValidIndex(I) && Fields[I] ? FCString::Atod(*Fields[I]->GetText().ToString()) : (I >= 6 ? 1. : 0.);
	return FTransform(FromUnityEuler(FVector(V[3], V[4], V[5])), Origin + FromUnityPos(FVector(V[0], V[1], V[2])), FromUnityScale(FVector(V[6], V[7], V[8])));
}

void UOKStudioWidget::RefreshPanels()
{
	const bool bMale = ObjectActor(1) != nullptr;
	if (Selected == 1 && !bMale) Selected = 0;
	if (FemaleLabel) FemaleLabel->SetText(FText::FromString(FString::Printf(TEXT("%s 女角：%s"), Selected == 0 ? TEXT("▶") : TEXT("　"), *ObjectName(0))));
	if (MaleLabel) MaleLabel->SetText(FText::FromString(FString::Printf(TEXT("%s 男角：%s"), Selected == 1 ? TEXT("▶") : TEXT("　"), *ObjectName(1))));
	if (MaleButton) MaleButton->SetIsEnabled(bMale);
	if (FemaleMotion) FemaleMotion->SetVisibility(Selected == 0 ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	if (MaleMotion) MaleMotion->SetVisibility(Selected == 1 ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	if (MotionTitle) MotionTitle->SetText(FText::FromString(FString::Printf(TEXT("動作：%s"), *ObjectName(Selected))));
	if (SpeedLabel) SpeedLabel->SetText(FText::FromString(FString::Printf(TEXT("播放速度 %.2f 倍"), Tracks[Selected].Speed)));
	if (SpeedSlider && !SpeedSlider->HasMouseCapture() && !FMath::IsNearlyEqual(SpeedSlider->GetValue(), Tracks[Selected].Speed)) SpeedSlider->SetValue(Tracks[Selected].Speed);
	for (int32 I = 0; I < ModeButtons.Num(); ++I)
		if (ModeButtons[I]) ModeButtons[I]->SetBackgroundColor(I == GuideMode ? FLinearColor(1.f, .75f, .2f) : FLinearColor::White);
	if (MaleCardCombo && Gallery && MaleCardCombo->GetOptionCount() != Gallery->PartnerMaleLabels.Num())
	{
		MaleCardCombo->ClearOptions();
		for (const FString& Label : Gallery->PartnerMaleLabels) MaleCardCombo->AddOption(Label);
		if (MaleCardCombo->GetOptionCount() > 0) MaleCardCombo->SetSelectedIndex(Gallery->PartnerMalePaths.IsValidIndex(Gallery->SelectedPartnerMale) ? Gallery->SelectedPartnerMale : 0);
	}
}

void UOKStudioWidget::RefreshMaleMotions()
{
	if (!MaleMotionCombo) return;
	TGuardValue<bool> Guard(bRefreshingMale, true);
	MaleMotionCombo->ClearOptions();
	MaleMotionCombo->AddOption(TEXT("選擇男角的影片動作（可輸入文字搜尋）"));
	for (const FString& Key : FOkSession::VideoClipNames()) MaleMotionCombo->AddOption(Key.Mid(7));
	const int32 Index = Tracks[1].MaleClip.IsEmpty() ? INDEX_NONE : MaleMotionCombo->FindOptionIndex(Tracks[1].MaleClip.Mid(7));
	MaleMotionCombo->SetSelectedIndex(Index > 0 ? Index : 0);
}

bool UOKStudioWidget::PlayOn(int32 Index, const FString& Key)
{
	AKKPreviewActor* A = ObjectActor(Index);
	if (!A || Key.IsEmpty() || !A->SetGameAnimation(Key)) return false;
	if (Index == 0 && Gallery)
	{
		// Same bookkeeping as the editor's selection, so reloads (clothes) restore it.
		Gallery->SelectedStudioRow = NAME_None;
		Gallery->SelectedGameClip = Key;
		Gallery->SelectedGameSource = TEXT("video:");
		Gallery->bVideoPairActive = false;
	}
	if (Index == 1)
	{
		Tracks[1].MaleClip = Key;
		RefreshMaleMotions();
	}
	EvaluatedAt = -1.f;
	FitLength();
	return true;
}

void UOKStudioWidget::PlayVideo(const FString& FemaleKey, const FString& MaleKey, const FString& Label)
{
	if (MaleKey.IsEmpty())
	{
		const bool bOk = PlayOn(Selected, FemaleKey);
		SetStatus(bOk ? FString::Printf(TEXT("%s 播放影片動作：%s"), *ObjectName(Selected), *Label) : TEXT("影片動作套用失敗：") + Label, !bOk);
		return;
	}
	if (ObjectActor(1))
	{
		const bool F = PlayOn(0, FemaleKey), M = PlayOn(1, MaleKey);
		if (Tracks[1].Keys.IsEmpty()) ObjectActor(1)->SetActorTransform(ObjectActor(0)->GetActorTransform());
		SetTime(0.f);
		SetStatus(F && M ? FString::Printf(TEXT("雙人動作：%s（男角與女角同一原點；可各自打關鍵幀）"), *Label)
			: FString::Printf(TEXT("雙人動作套用失敗：女方%s、男方%s"), F ? TEXT("成功") : TEXT("失敗"), M ? TEXT("成功") : TEXT("失敗")), !(F && M));
		return;
	}
	// No partner yet: the editor plays the female half and auto-loads a male card.
	PendingPairMale = MaleKey;
	PlayOn(0, FemaleKey);
	SetStatus(Gallery->PlayVideoPair(FemaleKey, MaleKey, Label));
	Gallery->bVideoPairActive = false;
}

void UOKStudioWidget::Borrow(UWidget* Widget, UPanelWidget* Into)
{
	if (!Widget || !Into) return;
	if (UPanelWidget* Parent = Widget->GetParent())
		if (!Borrowed.ContainsByPredicate([Parent](const FOKStudioBorrowed& B) { return B.Parent == Parent; }))
		{
			FOKStudioBorrowed& B = Borrowed.AddDefaulted_GetRef();
			B.Parent = Parent;
			for (int32 I = 0; I < Parent->GetChildrenCount(); ++I)
			{
				UWidget* Child = Parent->GetChildAt(I);
				B.Children.Add(Child);
				B.Slots.Add(Child && Child->Slot ? DuplicateObject<UPanelSlot>(Child->Slot, this) : nullptr);
			}
		}
	Widget->RemoveFromParent();
	Into->AddChild(Widget);
}

void UOKStudioWidget::RestoreBorrowed()
{
	for (FOKStudioBorrowed& B : Borrowed)
		if (B.Parent)
		{
			B.Parent->ClearChildren();
			for (int32 I = 0; I < B.Children.Num(); ++I)
				if (UWidget* Child = B.Children[I]) { Child->RemoveFromParent(); B.Parent->AddChild(Child, B.Slots.IsValidIndex(I) ? B.Slots[I].Get() : nullptr); }
		}
	Borrowed.Reset();
}

TSharedRef<SWidget> UOKStudioWidget::RebuildWidget()
{
	if (!WidgetTree) WidgetTree = NewObject<UWidgetTree>(this);
	UWidgetTree* Tree = WidgetTree;
	auto Text = [Tree](const FString& Value, int32 Size = 13, FLinearColor Color = FLinearColor::White)
	{
		UTextBlock* T = Tree->ConstructWidget<UTextBlock>();
		T->SetText(FText::FromString(Value));
		T->SetColorAndOpacity(FSlateColor(Color));
		T->SetAutoWrapText(true);
		FSlateFontInfo Font = T->GetFont();
		Font.Size = Size;
		T->SetFont(Font);
		return T;
	};
	auto Button = [Tree, Text](const FString& Value, const FString& Tip = FString(), UTextBlock** Out = nullptr)
	{
		UButton* B = Tree->ConstructWidget<UButton>();
		UTextBlock* T = Text(Value, 13);
		T->SetAutoWrapText(false);
		B->SetContent(T);
		FButtonStyle Style = B->GetStyle();
		Style.Normal = FSlateColorBrush(FLinearColor::FromSRGBColor(FColor(24, 82, 132)));
		Style.Hovered = FSlateColorBrush(FLinearColor::FromSRGBColor(FColor(30, 100, 158)));
		Style.Pressed = FSlateColorBrush(FLinearColor::FromSRGBColor(FColor(18, 60, 100)));
		B->SetStyle(Style);
		if (!Tip.IsEmpty()) B->SetToolTipText(FText::FromString(Tip));
		if (Out) *Out = T;
		return B;
	};
	auto Row = [Tree]() { UHorizontalBox* H = Tree->ConstructWidget<UHorizontalBox>(); H->SetVisibility(ESlateVisibility::SelfHitTestInvisible); return H; };
	auto Add = [](UHorizontalBox* H, UWidget* W, bool bFill = false)
	{
		UHorizontalBoxSlot* S = H->AddChildToHorizontalBox(W);
		S->SetPadding(FMargin(2.f));
		S->SetVerticalAlignment(VAlign_Center);
		if (bFill) S->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		return S;
	};
	auto Panel = [Tree](float Width, UWidget*& OutOuter)
	{
		USizeBox* Size = Tree->ConstructWidget<USizeBox>();
		if (Width > 0.f) Size->SetWidthOverride(Width);
		UBorder* Back = Tree->ConstructWidget<UBorder>();
		Back->SetBrushColor(PanelColor);
		Back->SetPadding(FMargin(8.f));
		Size->AddChild(Back);
		UScrollBox* Scroll = Tree->ConstructWidget<UScrollBox>();
		Back->AddChild(Scroll);
		UVerticalBox* Column = Tree->ConstructWidget<UVerticalBox>();
		Scroll->AddChild(Column);
		OutOuter = Size;
		return Column;
	};
	auto Field = [Tree](float Width = 70.f)
	{
		UEditableTextBox* F = Tree->ConstructWidget<UEditableTextBox>();
		F->SetMinDesiredWidth(Width);
		FEditableTextBoxStyle Style = F->GetWidgetStyle();
		Style.SetBackgroundColor(FSlateColor(FLinearColor(.06f, .08f, .12f)));
		Style.SetForegroundColor(FSlateColor(FLinearColor::White));
		Style.SetFocusedForegroundColor(FSlateColor(FLinearColor::White));
		F->SetWidgetStyle(Style);
		return F;
	};

	UOverlay* Root = Tree->ConstructWidget<UOverlay>();
	Root->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	Tree->RootWidget = Root;
	Guide = Tree->ConstructWidget<UOKStudioGuide>();
	Guide->Studio = this;
	Guide->SetVisibility(ESlateVisibility::HitTestInvisible);
	if (UOverlaySlot* S = Root->AddChildToOverlay(Guide)) { S->SetHorizontalAlignment(HAlign_Fill); S->SetVerticalAlignment(VAlign_Fill); }
	UVerticalBox* Layout = Tree->ConstructWidget<UVerticalBox>();
	Layout->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	if (UOverlaySlot* S = Root->AddChildToOverlay(Layout)) { S->SetHorizontalAlignment(HAlign_Fill); S->SetVerticalAlignment(VAlign_Fill); }

	// ---- top bar: return, scenes
	{
		UBorder* Bar = Tree->ConstructWidget<UBorder>();
		Bar->SetBrushColor(PanelColor);
		Bar->SetPadding(FMargin(6.f, 4.f));
		UHorizontalBox* H = Row();
		Bar->AddChild(H);
		UTextBlock* Title = Text(TEXT("工作室"), 16, FLinearColor(1.f, .85f, .4f));
		Title->SetAutoWrapText(false);
		Add(H, Title);
		UButton* Return = Button(TEXT("返回角色編輯器"));
		Return->OnClicked.AddDynamic(this, &UOKStudioWidget::OnReturn);
		Add(H, Return);
		Add(H, Text(TEXT("　場景名稱"), 13));
		SceneName = Field(160.f);
		SceneName->SetHintText(FText::FromString(TEXT("例如：浴室01")));
		Add(H, SceneName);
		UButton* Save = Button(TEXT("儲存場景"), TEXT("儲存兩個角色的動作、關鍵幀、速度與鏡頭到 Saved/Studio。"));
		Save->OnClicked.AddDynamic(this, &UOKStudioWidget::OnSaveScene);
		Add(H, Save);
		SceneCombo = Tree->ConstructWidget<UOKComboBoxString>();
		Add(H, SceneCombo)->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
		UButton* Load = Button(TEXT("讀取場景"), TEXT("讀取選取的場景；場景中的男角卡片會自動載入。"));
		Load->OnClicked.AddDynamic(this, &UOKStudioWidget::OnLoadScene);
		Add(H, Load);
		StatusLabel = Text(FString(), 13, FLinearColor(1.f, .92f, .45f));
		Add(H, StatusLabel, true);
		Layout->AddChildToVerticalBox(Bar);
		RefreshScenes();
	}

	// ---- middle: workspace + guide (left), free viewport, motion / import (right)
	UHorizontalBox* Middle = Row();
	Layout->AddChildToVerticalBox(Middle)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	{
		UWidget* Outer = nullptr;
		UVerticalBox* Left = Panel(300.f, Outer);
		Add(Middle, Outer)->SetVerticalAlignment(VAlign_Fill);
		Left->AddChild(Text(TEXT("工作區"), 15, FLinearColor(1.f, .85f, .4f)));
		UTextBlock* Label0 = nullptr;
		FemaleButton = Button(TEXT("女角"), TEXT("選取女角（主角色）"), &Label0);
		FemaleLabel = Label0;
		FemaleButton->OnClicked.AddDynamic(this, &UOKStudioWidget::OnSelectFemale);
		Left->AddChild(FemaleButton);
		UTextBlock* Label1 = nullptr;
		MaleButton = Button(TEXT("男角"), TEXT("選取男角"), &Label1);
		MaleLabel = Label1;
		MaleButton->OnClicked.AddDynamic(this, &UOKStudioWidget::OnSelectMale);
		Left->AddChild(MaleButton);
		Left->AddChild(Text(TEXT("追加／更換男角"), 12, FLinearColor(.85f, .9f, 1.f)));
		MaleCardCombo = Tree->ConstructWidget<UOKComboBoxString>();
		Left->AddChild(MaleCardCombo);
		UButton* LoadMale = Button(TEXT("載入男角"), TEXT("從遊戲目錄或目前資料夾的男性卡片載入"));
		LoadMale->OnClicked.AddDynamic(this, &UOKStudioWidget::OnLoadMale);
		Left->AddChild(LoadMale);

		Left->AddChild(Text(TEXT("操作軸（W 移動／E 旋轉／R 縮放）"), 15, FLinearColor(1.f, .85f, .4f)));
		UHorizontalBox* Modes = Row();
		const TCHAR* ModeNames[] = {TEXT("移動"), TEXT("旋轉"), TEXT("縮放")};
		for (int32 I = 0; I < 3; ++I)
		{
			UButton* B = Button(ModeNames[I]);
			if (I == 0) B->OnClicked.AddDynamic(this, &UOKStudioWidget::OnModeMove);
			else if (I == 1) B->OnClicked.AddDynamic(this, &UOKStudioWidget::OnModeRotate);
			else B->OnClicked.AddDynamic(this, &UOKStudioWidget::OnModeScale);
			ModeButtons.Add(B);
			Add(Modes, B, true);
		}
		Left->AddChild(Modes);
		Left->AddChild(Text(TEXT("拖曳畫面上的紅（X）綠（Y 上）藍（Z）軸；縮放模式中央白框為等比縮放。座標用原作單位（公尺、度），以進入工作室時女角腳下為原點。"), 11, FLinearColor(.7f, .75f, .8f)));
		const TCHAR* Groups[] = {TEXT("位置"), TEXT("旋轉"), TEXT("縮放")};
		for (int32 G = 0; G < 3; ++G)
		{
			UHorizontalBox* H = Row();
			Add(H, Text(Groups[G], 12));
			for (int32 I = 0; I < 3; ++I)
			{
				UEditableTextBox* F = Field(62.f);
				F->SetToolTipText(FText::FromString(FString::Printf(TEXT("%s %s（按 Enter 套用）"), Groups[G], I == 0 ? TEXT("X") : I == 1 ? TEXT("Y") : TEXT("Z"))));
				F->OnTextCommitted.AddDynamic(this, &UOKStudioWidget::OnFieldCommitted);
				Fields.Add(F);
				Add(H, F, true);
			}
			Left->AddChild(H);
		}
		UButton* Reset = Button(TEXT("回到初始位置"), TEXT("回到進入工作室時的位置、方向與大小"));
		Reset->OnClicked.AddDynamic(this, &UOKStudioWidget::OnResetTransform);
		Left->AddChild(Reset);
	}
	{
		USizeBox* Space = Tree->ConstructWidget<USizeBox>();
		Space->SetVisibility(ESlateVisibility::HitTestInvisible);
		Add(Middle, Space, true);
	}
	{
		UWidget* Outer = nullptr;
		UVerticalBox* Right = Panel(380.f, Outer);
		Add(Middle, Outer)->SetVerticalAlignment(VAlign_Fill);
		MotionTitle = Text(TEXT("動作"), 15, FLinearColor(1.f, .85f, .4f));
		Right->AddChild(MotionTitle);
		SpeedLabel = Text(TEXT("播放速度 1.00 倍"), 12);
		Right->AddChild(SpeedLabel);
		SpeedSlider = Tree->ConstructWidget<USlider>();
		SpeedSlider->SetMinValue(.1f);
		SpeedSlider->SetMaxValue(3.f);
		SpeedSlider->SetValue(1.f);
		SpeedSlider->OnValueChanged.AddDynamic(this, &UOKStudioWidget::OnSpeed);
		Right->AddChild(SpeedSlider);
		FemaleMotion = Tree->ConstructWidget<UVerticalBox>();
		Right->AddChild(FemaleMotion);
		MaleMotion = Tree->ConstructWidget<UVerticalBox>();
		Right->AddChild(MaleMotion);
		if (Gallery)
		{
			// The original's 動作 tab: the editor's own motion selectors act on the female.
			FemaleMotion->AddChild(Text(TEXT("內建動作（來源 → 動作）"), 12, FLinearColor(.85f, .9f, 1.f)));
			Borrow(Gallery->GameSourceCombo, FemaleMotion);
			Borrow(Gallery->GameClipCombo, FemaleMotion);
			FemaleMotion->AddChild(Text(TEXT("Studio 動作"), 12, FLinearColor(.85f, .9f, 1.f)));
			Borrow(Gallery->StudioCategoryCombo, FemaleMotion);
			Borrow(Gallery->StudioMotionCombo, FemaleMotion);
			Borrow(Gallery->AnimationStatus, FemaleMotion);
			// The original's 状態 tab: clothes.
			FemaleMotion->AddChild(Text(TEXT("衣服"), 12, FLinearColor(.85f, .9f, 1.f)));
			const TArray<TObjectPtr<UButton>> Clothes = Gallery->ClothesStateButtons;
			for (UButton* B : Clothes) Borrow(B, FemaleMotion);
		}
		MaleMotionCombo = Tree->ConstructWidget<UOKMotionComboBox>();
		MaleMotionCombo->OnSelectionChanged.AddDynamic(this, &UOKStudioWidget::OnMaleMotion);
		MaleMotion->AddChild(Text(TEXT("男角動作（影片動作）"), 12, FLinearColor(.85f, .9f, 1.f)));
		MaleMotion->AddChild(MaleMotionCombo);
		UButton* Stop = Button(TEXT("停止動作（站姿）"));
		Stop->OnClicked.AddDynamic(this, &UOKStudioWidget::OnStopMotion);
		MaleMotion->AddChild(Stop);
		RefreshMaleMotions();

		Right->AddChild(Text(TEXT("影片動作（選取的角色播放；【雙人】同時套用兩人）"), 15, FLinearColor(1.f, .85f, .4f)));
		VideoPanel = NewObject<UOKVideoMotionPanel>(this);
		VideoPanel->PlayOverride = [this](const FString& F, const FString& M, const FString& L) { PlayVideo(F, M, L); };
		VideoPanel->Build(Tree, Right, Gallery);
	}

	// ---- bottom: transport + timeline
	{
		UBorder* Bottom = Tree->ConstructWidget<UBorder>();
		Bottom->SetBrushColor(PanelColor);
		Bottom->SetPadding(FMargin(6.f));
		UVerticalBox* Column = Tree->ConstructWidget<UVerticalBox>();
		Bottom->AddChild(Column);
		UHorizontalBox* H = Row();
		// AddDynamic stringizes the function name, so each button binds explicitly.
		auto Transport = [&](const FString& Label, const FString& Tip, UTextBlock** Out = nullptr)
		{
			UButton* B = Button(Label, Tip, Out);
			Add(H, B);
			return B;
		};
		Transport(TEXT("⏮"), TEXT("回到開頭"))->OnClicked.AddDynamic(this, &UOKStudioWidget::OnRewind);
		Transport(TEXT("◀ 前一幀"), TEXT("←"))->OnClicked.AddDynamic(this, &UOKStudioWidget::OnPrevFrame);
		UTextBlock* Play = nullptr;
		Transport(TEXT("▶ 播放"), TEXT("空白鍵"), &Play)->OnClicked.AddDynamic(this, &UOKStudioWidget::OnPlay);
		PlayLabel = Play;
		Transport(TEXT("後一幀 ▶"), TEXT("→"))->OnClicked.AddDynamic(this, &UOKStudioWidget::OnNextFrame);
		Transport(TEXT("◆◀ 上一鍵"), TEXT("跳到上一個關鍵幀"))->OnClicked.AddDynamic(this, &UOKStudioWidget::OnPrevKey);
		Transport(TEXT("下一鍵 ▶◆"), TEXT("跳到下一個關鍵幀"))->OnClicked.AddDynamic(this, &UOKStudioWidget::OnNextKey);
		Transport(TEXT("打關鍵幀 (K)"), TEXT("在目前時間記錄選取角色的位置、旋轉與縮放"))->OnClicked.AddDynamic(this, &UOKStudioWidget::OnAddKey);
		Transport(TEXT("刪除關鍵幀 (Del)"), TEXT("刪除選取角色在目前時間的關鍵幀"))->OnClicked.AddDynamic(this, &UOKStudioWidget::OnDeleteKey);
		Transport(TEXT("清除全部"), TEXT("清除選取角色的全部關鍵幀（再按一次確認）"))->OnClicked.AddDynamic(this, &UOKStudioWidget::OnClearKeys);
		Add(H, Text(TEXT("補間"), 12));
		InterpCombo = Tree->ConstructWidget<UOKComboBoxString>();
		for (const TCHAR* Name : InterpNames) InterpCombo->AddOption(Name);
		InterpCombo->SetSelectedIndex(0);
		InterpCombo->SetToolTipText(FText::FromString(TEXT("目前時間這個關鍵幀到下一個關鍵幀的補間方式")));
		InterpCombo->OnSelectionChanged.AddDynamic(this, &UOKStudioWidget::OnInterp);
		Add(H, InterpCombo);
		auto Check = [&](const FString& Label, bool bOn, const FString& Tip)
		{
			UCheckBox* C = Tree->ConstructWidget<UCheckBox>();
			C->SetIsChecked(bOn);
			C->SetContent(Text(Label, 12));
			C->SetToolTipText(FText::FromString(Tip));
			Add(H, C);
			return C;
		};
		AutoKey = Check(TEXT("自動關鍵幀"), true, TEXT("拖曳操作軸或輸入座標後，自動在目前時間打關鍵幀"));
		Loop = Check(TEXT("循環"), true, TEXT("播放到結尾時從頭開始"));
		FollowLength = Check(TEXT("長度跟隨動作"), true, TEXT("時間軸長度＝最長的動作；取消後可自行輸入"));
		Add(H, Text(TEXT("長度(秒)"), 12));
		LengthBox = Field(56.f);
		LengthBox->OnTextCommitted.AddDynamic(this, &UOKStudioWidget::OnLengthCommitted);
		Add(H, LengthBox);
		TimeLabel = Text(FString(), 13, FLinearColor(1.f, .92f, .45f));
		TimeLabel->SetAutoWrapText(false);
		Add(H, TimeLabel, true);
		Column->AddChild(H);
		USizeBox* Size = Tree->ConstructWidget<USizeBox>();
		Size->SetHeightOverride(OKStudio::Ruler + 2 * OKStudio::Row + 4.f);
		Timeline = Tree->ConstructWidget<UOKStudioTimeline>();
		Timeline->Studio = this;
		Timeline->SetVisibility(ESlateVisibility::Visible);  // user widgets default to self-hit-test-invisible
		Size->AddChild(Timeline);
		Column->AddChild(Size);
		Layout->AddChildToVerticalBox(Bottom);
	}
	SetPlaying(false);
	RefreshPanels();
	return Super::RebuildWidget();
}

// ---------------------------------------------------------------- handlers

void UOKStudioWidget::OnReturn()
{
	if (VideoPanel && VideoPanel->IsImporting()) { SetStatus(TEXT("影片匯入中：請等它完成或先按「取消」，再返回角色編輯器。"), true); return; }
	Close();
}
void UOKStudioWidget::OnSelectFemale() { Selected = 0; SelectedTrackKey = INDEX_NONE; DragAxis = INDEX_NONE; RefreshPanels(); RefreshFields(); }
void UOKStudioWidget::OnSelectMale()
{
	if (!ObjectActor(1)) { SetStatus(TEXT("尚未載入男角：在「追加／更換男角」選一位後按「載入男角」。"), true); return; }
	Selected = 1; SelectedTrackKey = INDEX_NONE; DragAxis = INDEX_NONE; RefreshPanels(); RefreshFields();
}
void UOKStudioWidget::OnLoadMale()
{
	if (!Gallery || !MaleCardCombo) return;
	const int32 Index = MaleCardCombo->GetSelectedIndex();
	if (!Gallery->PartnerMalePaths.IsValidIndex(Index)) { SetStatus(TEXT("找不到男性卡片：遊戲目錄 UserData/chara/male 或目前資料夾的男性獨立卡。"), true); return; }
	Gallery->SelectPartnerMale(Index);
	Gallery->LoadPartnerFromPath(Gallery->PartnerMalePaths[Index]);
	SetStatus(TEXT("正在載入男角：") + Gallery->PartnerMaleLabels[Index]);
}
void UOKStudioWidget::OnModeMove() { GuideMode = 0; DragAxis = INDEX_NONE; RefreshPanels(); }
void UOKStudioWidget::OnModeRotate() { GuideMode = 1; DragAxis = INDEX_NONE; RefreshPanels(); }
void UOKStudioWidget::OnModeScale() { GuideMode = 2; DragAxis = INDEX_NONE; RefreshPanels(); }
void UOKStudioWidget::OnFieldCommitted(const FText& Value, ETextCommit::Type Type)
{
	if (Type == ETextCommit::OnCleared) return;
	EditSelected(FieldTransform(), true);
}
void UOKStudioWidget::OnResetTransform()
{
	if (!bHadOriginal[Selected]) return;
	EditSelected(Original[Selected], true);
}
void UOKStudioWidget::OnMaleMotion(FString Item, ESelectInfo::Type Type)
{
	if (bRefreshingMale || !MaleMotionCombo || MaleMotionCombo->FindOptionIndex(Item) <= 0) return;
	const bool bOk = PlayOn(1, TEXT("video:|") + Item);
	SetStatus(bOk ? TEXT("男角播放：") + Item : TEXT("男角動作套用失敗：") + Item, !bOk);
}
void UOKStudioWidget::OnStopMotion()
{
	if (AKKPreviewActor* A = ObjectActor(Selected)) { A->SetPoseMode(0); A->AnimationPlaybackSpeed = 1.f; }
	if (Selected == 1) { Tracks[1].MaleClip.Empty(); RefreshMaleMotions(); }
	FitLength();
}
void UOKStudioWidget::OnSpeed(float Value)
{
	Tracks[Selected].Speed = FMath::Clamp(FMath::RoundToFloat(Value * 20.f) / 20.f, .1f, 3.f);
	FitLength();
}
void UOKStudioWidget::OnPlay() { SetPlaying(!bPlaying); }
void UOKStudioWidget::OnRewind() { SetTime(0.f); }
void UOKStudioWidget::OnPrevFrame() { SetPlaying(false); SetTime((FMath::RoundToFloat(Time * OKStudio::Fps) - 1.f) / OKStudio::Fps); }
void UOKStudioWidget::OnNextFrame() { SetPlaying(false); SetTime((FMath::RoundToFloat(Time * OKStudio::Fps) + 1.f) / OKStudio::Fps); }
void UOKStudioWidget::OnPrevKey()
{
	SetPlaying(false);
	const TArray<FOKStudioKey>& Keys = Tracks[Selected].Keys;
	for (int32 I = Keys.Num() - 1; I >= 0; --I) if (Keys[I].Time < Time - .5f / OKStudio::Fps) { SetTime(Keys[I].Time); return; }
	SetStatus(TEXT("前面沒有關鍵幀"));
}
void UOKStudioWidget::OnNextKey()
{
	SetPlaying(false);
	for (const FOKStudioKey& K : Tracks[Selected].Keys) if (K.Time > Time + .5f / OKStudio::Fps) { SetTime(K.Time); return; }
	SetStatus(TEXT("後面沒有關鍵幀"));
}
void UOKStudioWidget::OnAddKey() { AddKey(Selected); }
void UOKStudioWidget::OnDeleteKey() { DeleteKey(Selected); }
void UOKStudioWidget::OnClearKeys()
{
	static double ArmedUntil = 0.;
	const double Now = FPlatformTime::Seconds();
	if (Now > ArmedUntil) { ArmedUntil = Now + 3.; SetStatus(FString::Printf(TEXT("再按一次「清除全部」刪除 %s 的 %d 個關鍵幀"), *ObjectName(Selected), Tracks[Selected].Keys.Num()), true); return; }
	ArmedUntil = 0.;
	Tracks[Selected].Keys.Reset();
	EvaluatedAt = -1.f;
	SetStatus(TEXT("已清除 ") + ObjectName(Selected) + TEXT(" 的全部關鍵幀"));
}
void UOKStudioWidget::OnInterp(FString Item, ESelectInfo::Type Type)
{
	if (Type == ESelectInfo::Direct || !InterpCombo) return;
	const int32 Key = Tracks[Selected].KeyAt(Time, .5f / OKStudio::Fps);
	if (Key == INDEX_NONE) { SetStatus(TEXT("先移到關鍵幀上（上一鍵／下一鍵），再選補間方式。"), true); return; }
	Tracks[Selected].Keys[Key].Interp = uint8(FMath::Clamp(InterpCombo->FindOptionIndex(Item), 0, 2));
	EvaluatedAt = -1.f;
	SetStatus(FString::Printf(TEXT("%.2fs 的關鍵幀到下一鍵改為「%s」補間"), Tracks[Selected].Keys[Key].Time, *Item));
}
void UOKStudioWidget::OnLengthCommitted(const FText& Value, ETextCommit::Type Type)
{
	const float L = FCString::Atof(*Value.ToString());
	if (!FMath::IsFinite(L) || L < .1f) { SetStatus(TEXT("長度需大於 0.1 秒"), true); FitLength(); return; }
	if (FollowLength) FollowLength->SetIsChecked(false);
	Length = FMath::Min(L, 3600.f);
	Time = FMath::Min(Time, Length);
}

// ---------------------------------------------------------------- scenes

FString UOKStudioWidget::SceneDirectory() { return FPaths::ProjectSavedDir() / TEXT("Studio"); }

void UOKStudioWidget::RefreshScenes()
{
	if (!SceneCombo) return;
	const FString Keep = SceneCombo->GetSelectedOption();
	SceneCombo->ClearOptions();
	TArray<FString> Files;
	IFileManager::Get().FindFiles(Files, *(SceneDirectory() / TEXT("*.json")), true, false);
	Files.Sort();
	for (const FString& File : Files)
		if (File.EndsWith(TEXT(".okstudio.json"))) SceneCombo->AddOption(File.LeftChop(14));
	if (SceneCombo->GetOptionCount() > 0) SceneCombo->SetSelectedIndex(FMath::Max(0, SceneCombo->FindOptionIndex(Keep)));
}

bool UOKStudioWidget::SaveScene(const FString& Name, FString& Error)
{
	const FString Clean = Name.TrimStartAndEnd();
	if (Clean.IsEmpty() || Clean.Len() > 80) { Error = TEXT("請輸入場景名稱（1～80 字）"); return false; }
	for (TCHAR C : Clean) if (FCString::Strchr(TEXT("\\/:*?\"<>|"), C) || C < 32) { Error = TEXT("場景名稱不能包含 \\ / : * ? \" < > |"); return false; }
	if (!Gallery || !ObjectActor(0)) { Error = TEXT("沒有載入角色"); return false; }
	auto Keys = [](const FOKStudioTrack& T)
	{
		TArray<TSharedPtr<FJsonValue>> Out;
		for (const FOKStudioKey& K : T.Keys)
		{
			auto O = MakeShared<FJsonObject>();
			O->SetNumberField(TEXT("t"), K.Time);
			O->SetArrayField(TEXT("pos"), Numbers({K.Pos.X, K.Pos.Y, K.Pos.Z}));
			O->SetArrayField(TEXT("rot"), Numbers({K.Rot.X, K.Rot.Y, K.Rot.Z, K.Rot.W}));
			O->SetArrayField(TEXT("scale"), Numbers({K.Scale.X, K.Scale.Y, K.Scale.Z}));
			O->SetNumberField(TEXT("interp"), K.Interp);
			Out.Add(MakeShared<FJsonValueObject>(O));
		}
		return Out;
	};
	auto Root = MakeShared<FJsonObject>();
	Root->SetNumberField(TEXT("version"), 1);
	Root->SetNumberField(TEXT("length"), Length);
	Root->SetBoolField(TEXT("follow_length"), !FollowLength || FollowLength->IsChecked());
	Root->SetArrayField(TEXT("origin"), Numbers({Origin.X, Origin.Y, Origin.Z}));
	const AKKPreviewActor* Main = ObjectActor(0);
	auto Camera = MakeShared<FJsonObject>();
	Camera->SetArrayField(TEXT("focus"), Numbers({Main->OrbitFocus.X, Main->OrbitFocus.Y, Main->OrbitFocus.Z}));
	Camera->SetNumberField(TEXT("yaw"), Main->OrbitYaw);
	Camera->SetNumberField(TEXT("pitch"), Main->OrbitPitch);
	Camera->SetNumberField(TEXT("distance"), Main->OrbitDistance);
	Root->SetObjectField(TEXT("camera"), Camera);
	auto Female = MakeShared<FJsonObject>();
	Female->SetStringField(TEXT("card"), Gallery->Entries.IsValidIndex(Gallery->SelectedIndex) ? Gallery->Entries[Gallery->SelectedIndex].Path : FString());
	Female->SetStringField(TEXT("clip"), Main->PoseMode == 2 ? Gallery->SelectedGameClip : FString());
	Female->SetStringField(TEXT("row"), Main->PoseMode == 2 && !Gallery->SelectedStudioRow.IsNone() ? Gallery->SelectedStudioRow.ToString() : FString());
	Female->SetArrayField(TEXT("studio_options"), Numbers({Gallery->SavedStudioOptions.X, Gallery->SavedStudioOptions.Y, Gallery->SavedStudioOptions.Z}));
	Female->SetNumberField(TEXT("speed"), Tracks[0].Speed);
	Female->SetArrayField(TEXT("keys"), Keys(Tracks[0]));
	Root->SetObjectField(TEXT("female"), Female);
	if (const AKKPreviewActor* Partner = ObjectActor(1))
	{
		auto Male = MakeShared<FJsonObject>();
		Male->SetStringField(TEXT("card"), Gallery->PartnerCardPath);
		Male->SetStringField(TEXT("clip"), Partner->PoseMode == 2 ? Tracks[1].MaleClip : FString());
		Male->SetNumberField(TEXT("speed"), Tracks[1].Speed);
		Male->SetArrayField(TEXT("keys"), Keys(Tracks[1]));
		Root->SetObjectField(TEXT("male"), Male);
	}
	FString Json;
	FJsonSerializer::Serialize(Root, TJsonWriterFactory<>::Create(&Json));
	IFileManager::Get().MakeDirectory(*SceneDirectory(), true);
	const FString Path = SceneDirectory() / (Clean + TEXT(".okstudio.json"));
	if (!FFileHelper::SaveStringToFile(Json, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)) { Error = ErrorText(TEXT("無法寫入場景檔"), Path); return false; }
	RefreshScenes();
	if (SceneCombo) SceneCombo->SetSelectedOption(Clean);
	UE_LOG(LogTemp, Display, TEXT("OK studio saved %s"), *Path);
	return true;
}

bool UOKStudioWidget::LoadScene(const FString& Name, FString& Error)
{
	const FString Path = SceneDirectory() / (Name + TEXT(".okstudio.json"));
	FString Json;
	TSharedPtr<FJsonObject> Root;
	if (!FFileHelper::LoadFileToString(Json, *Path)) { Error = ErrorText(TEXT("無法讀取場景檔"), Path); return false; }
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root) || !Root.IsValid()) { Error = TEXT("場景檔不是有效的 JSON：") + Path; return false; }
	auto ReadTrack = [](const TSharedPtr<FJsonObject>& O, FOKStudioTrack& T)
	{
		T.Keys.Reset();
		double Speed = 1.;
		if (O->TryGetNumberField(TEXT("speed"), Speed)) T.Speed = FMath::Clamp(float(Speed), .1f, 3.f);
		const TArray<TSharedPtr<FJsonValue>>* Keys;
		if (O->TryGetArrayField(TEXT("keys"), Keys))
			for (const TSharedPtr<FJsonValue>& V : *Keys)
			{
				const TSharedPtr<FJsonObject> K = V->AsObject();
				double P[3], R[4], S[3], Time = 0., Interp = 0.;
				if (!K || !K->TryGetNumberField(TEXT("t"), Time) || !ReadNumbers(K, TEXT("pos"), 3, P) || !ReadNumbers(K, TEXT("rot"), 4, R) || !ReadNumbers(K, TEXT("scale"), 3, S)) continue;
				K->TryGetNumberField(TEXT("interp"), Interp);
				FOKStudioKey Key;
				Key.Time = FMath::Max(0.f, float(Time));
				Key.Pos = FVector(P[0], P[1], P[2]);
				Key.Rot = FQuat(R[0], R[1], R[2], R[3]).GetNormalized();
				Key.Scale = FVector(S[0], S[1], S[2]);
				Key.Interp = uint8(FMath::Clamp(int32(Interp), 0, 2));
				T.Keys.Add(Key);
			}
		T.Keys.Sort([](const FOKStudioKey& A, const FOKStudioKey& B) { return A.Time < B.Time; });
	};
	FString Warning;
	double Number = 0.;
	if (double O[3]; ReadNumbers(Root, TEXT("origin"), 3, O)) Origin = FVector(O[0], O[1], O[2]);
	bool bFollow = true;
	Root->TryGetBoolField(TEXT("follow_length"), bFollow);
	if (FollowLength) FollowLength->SetIsChecked(bFollow);
	if (Root->TryGetNumberField(TEXT("length"), Number) && Number > .05) Length = float(Number);
	const TSharedPtr<FJsonObject>* Female;
	if (Root->TryGetObjectField(TEXT("female"), Female))
	{
		ReadTrack(*Female, Tracks[0]);
		FString Card, Clip, StudioRow;
		(*Female)->TryGetStringField(TEXT("card"), Card);
		(*Female)->TryGetStringField(TEXT("clip"), Clip);
		(*Female)->TryGetStringField(TEXT("row"), StudioRow);
		if (Gallery->Entries.IsValidIndex(Gallery->SelectedIndex) && !Card.IsEmpty() && !FPaths::IsSamePath(Card, Gallery->Entries[Gallery->SelectedIndex].Path))
			Warning += TEXT("場景的女角卡片與目前角色不同（") + FPaths::GetCleanFilename(Card) + TEXT("），動作與關鍵幀套用在目前角色。");
		AKKPreviewActor* Main = ObjectActor(0);
		if (!StudioRow.IsEmpty() && Main && Gallery->AnimationCatalog)
		{
			double O[3] = {0., 0., 0.};
			ReadNumbers(*Female, TEXT("studio_options"), 3, O);
			FString StudioError;
			if (Main->SetStudioAnimation(Gallery->AnimationCatalog, FName(*StudioRow), StudioError, O[0], O[1], O[2]))
			{ Gallery->SelectedStudioRow = FName(*StudioRow); Gallery->SelectedGameClip = Clip; Gallery->SavedStudioOptions = FVector(O[0], O[1], O[2]); }
			else Warning += TEXT("女角 Studio 動作無法播放：") + StudioRow + TEXT(" ") + StudioError + TEXT("。");
		}
		else if (!Clip.IsEmpty())
		{
			if (Main && Main->SetGameAnimation(Clip)) { Gallery->SelectedStudioRow = NAME_None; Gallery->SelectedGameClip = Clip; if (FOkSession::IsVideoClipKey(Clip)) Gallery->SelectedGameSource = TEXT("video:"); }
			else Warning += TEXT("女角動作無法播放：") + Clip + TEXT("（內建動作請先在右側選一次同一來源）。");
		}
	}
	const TSharedPtr<FJsonObject>* Male;
	if (Root->TryGetObjectField(TEXT("male"), Male))
	{
		ReadTrack(*Male, Tracks[1]);
		FString Card, Clip;
		(*Male)->TryGetStringField(TEXT("card"), Card);
		(*Male)->TryGetStringField(TEXT("clip"), Clip);
		if (ObjectActor(1) && (Card.IsEmpty() || FPaths::IsSamePath(Card, Gallery->PartnerCardPath)))
		{
			if (!Clip.IsEmpty() && !PlayOn(1, Clip)) Warning += TEXT("男角動作無法播放：") + Clip + TEXT("。");
		}
		else if (!Card.IsEmpty() && IFileManager::Get().FileExists(*Card))
		{
			PendingMaleScene = *Male;
			PendingMaleCard = Card;
			Gallery->LoadPartnerFromPath(Card);
			Warning += TEXT("正在載入場景的男角：") + FPaths::GetCleanFilename(Card) + TEXT("。");
		}
		else Warning += TEXT("找不到場景的男角卡片：") + Card + TEXT("。");
	}
	else Tracks[1].Keys.Reset();
	const TSharedPtr<FJsonObject>* Camera;
	if (AKKPreviewActor* Main = ObjectActor(0); Main && Root->TryGetObjectField(TEXT("camera"), Camera))
	{
		double F[3], V = 0.;
		if (ReadNumbers(*Camera, TEXT("focus"), 3, F)) Main->OrbitFocus = FVector(F[0], F[1], F[2]);
		if ((*Camera)->TryGetNumberField(TEXT("yaw"), V)) Main->OrbitYaw = V;
		if ((*Camera)->TryGetNumberField(TEXT("pitch"), V)) Main->OrbitPitch = V;
		if ((*Camera)->TryGetNumberField(TEXT("distance"), V)) Main->OrbitDistance = V;
		Main->ApplyOrbitCamera();
	}
	FitLength();
	SetTime(0.f);
	EvaluatedAt = -1.f;
	if (SceneName) SceneName->SetText(FText::FromString(Name));
	SetStatus(TEXT("已讀取場景：") + Name + (Warning.IsEmpty() ? FString() : TEXT("　") + Warning), Warning.Contains(TEXT("無法")) || Warning.Contains(TEXT("找不到")));
	UE_LOG(LogTemp, Display, TEXT("OK studio loaded %s %s"), *Path, *Warning);
	return true;
}

void UOKStudioWidget::OnSaveScene()
{
	FString Error;
	const FString Name = SceneName ? SceneName->GetText().ToString() : FString();
	if (SaveScene(Name, Error)) SetStatus(TEXT("已儲存場景：") + SceneDirectory() / (Name.TrimStartAndEnd() + TEXT(".okstudio.json")));
	else SetStatus(Error, true);
}

void UOKStudioWidget::OnLoadScene()
{
	const FString Name = SceneCombo ? SceneCombo->GetSelectedOption() : FString();
	if (Name.IsEmpty()) { SetStatus(TEXT("Saved/Studio 裡還沒有場景。"), true); return; }
	FString Error;
	if (!LoadScene(Name, Error)) SetStatus(Error, true);
}

// ---------------------------------------------------------------- automation

TSharedRef<FJsonObject> UOKStudioWidget::StateJson() const
{
	auto R = MakeShared<FJsonObject>();
	R->SetNumberField(TEXT("time"), Time);
	R->SetNumberField(TEXT("length"), Length);
	R->SetBoolField(TEXT("playing"), bPlaying);
	R->SetNumberField(TEXT("selected"), Selected);
	R->SetNumberField(TEXT("guide_mode"), GuideMode);
	R->SetStringField(TEXT("status"), Status);
	TArray<TSharedPtr<FJsonValue>> Objects;
	for (int32 I = 0; I < 2; ++I)
	{
		auto O = MakeShared<FJsonObject>();
		const AKKPreviewActor* A = ObjectActor(I);
		O->SetStringField(TEXT("name"), ObjectName(I));
		O->SetBoolField(TEXT("loaded"), A != nullptr);
		O->SetNumberField(TEXT("keys"), Tracks[I].Keys.Num());
		O->SetNumberField(TEXT("speed"), Tracks[I].Speed);
		if (A)
		{
			const FVector P = ToUnityPos(A->GetActorLocation() - Origin), E = UnityEuler(A->GetActorQuat()), S = ToUnityScale(A->GetActorScale3D());
			O->SetArrayField(TEXT("pos"), Numbers({P.X, P.Y, P.Z}));
			O->SetArrayField(TEXT("rot"), Numbers({E.X, E.Y, E.Z}));
			O->SetArrayField(TEXT("scale"), Numbers({S.X, S.Y, S.Z}));
			O->SetNumberField(TEXT("pose_mode"), A->PoseMode);
			O->SetNumberField(TEXT("anim_phase"), A->AnimPhase);
			O->SetNumberField(TEXT("anim_duration"), A->AnimDuration);
			O->SetBoolField(TEXT("ticking"), A->IsActorTickEnabled());
		}
		O->SetStringField(TEXT("clip"), I == 0 ? (Gallery ? Gallery->SelectedGameClip : FString()) : Tracks[1].MaleClip);
		Objects.Add(MakeShared<FJsonValueObject>(O));
	}
	R->SetArrayField(TEXT("objects"), Objects);
	return R;
}

TSharedPtr<FJsonObject> UOKStudioWidget::Command(const FJsonObject& Request, FString& Error)
{
	FString Op;
	Request.TryGetStringField(TEXT("op"), Op);
	double Number = 0.;
	if (Op == TEXT("state")) {}
	else if (Op == TEXT("select")) { if (!Request.TryGetNumberField(TEXT("index"), Number)) { Error = TEXT("index required"); return nullptr; } Number == 1 ? OnSelectMale() : OnSelectFemale(); }
	else if (Op == TEXT("mode")) { if (!Request.TryGetNumberField(TEXT("mode"), Number) || Number < 0 || Number > 2) { Error = TEXT("mode 0..2"); return nullptr; } GuideMode = int32(Number); }
	else if (Op == TEXT("time")) { if (!Request.TryGetNumberField(TEXT("time"), Number)) { Error = TEXT("time required"); return nullptr; } SetPlaying(false); SetTime(float(Number)); }
	else if (Op == TEXT("play")) { bool bPlay = true; Request.TryGetBoolField(TEXT("play"), bPlay); SetPlaying(bPlay); }
	else if (Op == TEXT("transform"))
	{
		// Original units like the fields: pos metres from the studio origin, rot Unity euler degrees, scale.
		AKKPreviewActor* A = ObjectActor(Selected);
		if (!A) { Error = TEXT("selected object not loaded"); return nullptr; }
		const TSharedPtr<FJsonObject> Self = MakeShared<FJsonObject>(Request);
		double P[3], E[3], S[3];
		FTransform T = A->GetActorTransform();
		if (ReadNumbers(Self, TEXT("pos"), 3, P)) T.SetLocation(Origin + FromUnityPos(FVector(P[0], P[1], P[2])));
		if (ReadNumbers(Self, TEXT("rot"), 3, E)) T.SetRotation(FromUnityEuler(FVector(E[0], E[1], E[2])));
		if (ReadNumbers(Self, TEXT("scale"), 3, S)) T.SetScale3D(FromUnityScale(FVector(S[0], S[1], S[2])));
		EditSelected(T, true);
	}
	else if (Op == TEXT("guide_drag"))
	{
		// Same math as a mouse drag: grab the handle of `axis` in `mode` and move the cursor by (dx, dy) pixels.
		double Axis = 0., Dx = 0., Dy = 0., Mode = GuideMode;
		Request.TryGetNumberField(TEXT("axis"), Axis);
		Request.TryGetNumberField(TEXT("dx"), Dx);
		Request.TryGetNumberField(TEXT("dy"), Dy);
		Request.TryGetNumberField(TEXT("mode"), Mode);
		GuideMode = FMath::Clamp(int32(Mode), 0, 2);
		TArray<FHandle> Handles;
		FHitResult2D Hit;
		if (!BuildGuide(Handles, Hit.Center)) { Error = TEXT("guide not visible"); return nullptr; }
		const FHandle* Found = Handles.FindByPredicate([Axis](const FHandle& H) { return H.Axis == int32(Axis); });
		if (!Found) { Error = TEXT("axis handle not visible from this camera"); return nullptr; }
		Hit.Handle = *Found;
		Hit.Segment = 0;
		const FVector2D Grab = Found->Axis == 3 ? Hit.Center : Found->bClosed ? (Found->Points[0] + Found->Points[1]) * .5 : (Found->Points[0] + Found->Points[1]) * .5;
		BeginDrag(Hit, Grab);
		DragTo(Grab + FVector2D(Dx, Dy));
		if (AKKPreviewActor* A = ObjectActor(Selected)) EditSelected(A->GetActorTransform(), true);
		DragAxis = INDEX_NONE;
		TSharedRef<FJsonObject> R = StateJson();
		R->SetArrayField(TEXT("screen_direction"), Numbers({DragDirection.X, DragDirection.Y}));
		return R;
	}
	else if (Op == TEXT("key")) AddKey(Selected);
	else if (Op == TEXT("delete_key")) DeleteKey(Selected);
	else if (Op == TEXT("auto_key")) { bool bOn = true; Request.TryGetBoolField(TEXT("on"), bOn); if (AutoKey) AutoKey->SetIsChecked(bOn); }
	else if (Op == TEXT("interp")) { if (!Request.TryGetNumberField(TEXT("interp"), Number)) { Error = TEXT("interp 0..2"); return nullptr; } const int32 K = Tracks[Selected].KeyAt(Time, .5f / OKStudio::Fps); if (K == INDEX_NONE) { Error = TEXT("no key at time"); return nullptr; } Tracks[Selected].Keys[K].Interp = uint8(FMath::Clamp(int32(Number), 0, 2)); EvaluatedAt = -1.f; }
	else if (Op == TEXT("speed")) { if (!Request.TryGetNumberField(TEXT("speed"), Number)) { Error = TEXT("speed required"); return nullptr; } OnSpeed(float(Number)); }
	else if (Op == TEXT("video")) { FString Item; Request.TryGetStringField(TEXT("item"), Item); if (!VideoPanel || !VideoPanel->SelectItem(Item)) { Error = TEXT("Unknown video motion item"); return nullptr; } }
	else if (Op == TEXT("male_motion")) { FString Item; Request.TryGetStringField(TEXT("item"), Item); if (!PlayOn(1, TEXT("video:|") + Item)) { Error = TEXT("male motion failed"); return nullptr; } }
	else if (Op == TEXT("save") || Op == TEXT("load"))
	{
		FString Name;
		Request.TryGetStringField(TEXT("name"), Name);
		if (!(Op == TEXT("save") ? SaveScene(Name, Error) : LoadScene(Name, Error))) return nullptr;
	}
	else if (Op == TEXT("clear_keys")) { Tracks[Selected].Keys.Reset(); EvaluatedAt = -1.f; }
	else if (Op == TEXT("exit")) { Close(); }
	else { Error = TEXT("Unknown studio op: ") + Op; return nullptr; }
	return StateJson();
}

// ---------------------------------------------------------------- editor entry

void UOKCharacterController::OnEnterStudio()
{
	if (StudioWidget) return;
	if (InteractionWidget || InteractionScene) return;
	if (!IsValid(PreviewActor) || !PreviewActor->Session.IsOpen() || bLoadInFlight)
	{
		ShowAnimationStatus(TEXT("請先載入角色，再進入工作室。"), true);
		return;
	}
	ApplyPendingEditorMods();
	StudioWidget = UOKStudioWidget::Open(this);
}

void UOKCharacterController::OnExitStudio()
{
	if (StudioWidget) StudioWidget->Close();
}

void UOKCharacterController::OnEnterOkStudio()
{
	if (bOkStudioOpen || StudioWidget || InteractionWidget || InteractionScene) return;
	if (!IsValid(PreviewActor) || !PreviewActor->Session.IsOpen() || bLoadInFlight)
	{
		ShowAnimationStatus(TEXT("請先載入角色，再進入工作室。"), true);
		return;
	}
	ApplyPendingEditorMods();
	const FString Card = !PreviewCardPath.IsEmpty() ? PreviewCardPath : (Entries.IsValidIndex(SelectedIndex) ? Entries[SelectedIndex].Path : FString());
	FString Error;
	UOKStudioGame* Game = Card.IsEmpty() ? nullptr : UOKStudioGame::Start(GetWorld(), FString(), Error);
	if (!Game) { ShowAnimationStatus(TEXT("無法開啟工作室：") + (Card.IsEmpty() ? FString(TEXT("沒有角色卡")) : Error), true); return; }
	bOkStudioOpen = true;
	OnStopPair();
	// The studio scene stands where the gallery character stood; the gallery hides until the studio closes.
	const FVector Origin = PreviewActor->GetActorLocation();
	Game->SceneActor->SetActorLocation(Origin);
	Game->Focus = Origin + FVector(0.f, 0.f, 90.f);
	Game->AddCharacter(Card, PreviewOutfitIndex != INDEX_NONE ? PreviewOutfitIndex : OutfitIndex);
	for (AKKPreviewActor* A : { PreviewActor.Get(), PartnerActor.Get() }) if (IsValid(A)) A->SetActorHiddenInGame(true);
	if (auto* PC = Cast<AOpenKoikatsuPlayerController>(GetOwningPlayer())) { PC->SetPaintPreviewActive(false); PC->bExternalMouseOwner = false; }
	VisibilityBeforeOkStudio = GetVisibility();
	SetVisibility(ESlateVisibility::Collapsed);
	TWeakObjectPtr<UOKCharacterController> Self(this);
	Game->OnStopped.AddLambda([Self] { if (Self.IsValid()) Self->OnOkStudioStopped(); });
	UE_LOG(LogTemp, Display, TEXT("OK studio (scene tree) open card=%s"), *Card);
}

void UOKCharacterController::OnOkStudioStopped()
{
	if (!bOkStudioOpen) return;
	bOkStudioOpen = false;
	for (AKKPreviewActor* A : { PreviewActor.Get(), PartnerActor.Get() }) if (IsValid(A)) A->SetActorHiddenInGame(false);
	SetVisibility(VisibilityBeforeOkStudio);
	SetModelLayout(bModelLayout);
	ApplyClickInputMode();
	UE_LOG(LogTemp, Display, TEXT("OK studio (scene tree) closed"));
}
