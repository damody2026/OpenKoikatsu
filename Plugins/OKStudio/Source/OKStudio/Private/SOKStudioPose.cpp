// OK Studio panel: the scene view's mouse (picking, guide drags, pose points, the look point, fly mode) and what it draws
// over the scene, and the pose work: FK / IK pages with part switches, mirroring, IK from FK, the pose library, the
// bone lists, expression, hands and look pages.
#include "SOKStudioPanel.h"
#include "OKStudioGameInternal.h"

using namespace OKStudioStyle;

namespace
{
	APlayerController* ViewController(const UOKStudioGame* Game)
	{
		return Game && Game->SceneActor && Game->SceneActor->GetWorld() ? Game->SceneActor->GetWorld()->GetFirstPlayerController() : nullptr;
	}
	bool ToScreen(const UOKStudioGame* Game, const FVector& World, FVector2D& Out)
	{
		APlayerController* PC = ViewController(Game);
		return PC && UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition(PC, World, Out, true);
	}

	/** The mirrored partner of a bone or effector name ("_L" <-> "_R"); itself when it has none. */
	FString MirrorName(const FString& Bone)
	{
		if (Bone.EndsWith(TEXT("_L"))) return Bone.LeftChop(2) + TEXT("_R");
		if (Bone.EndsWith(TEXT("_R"))) return Bone.LeftChop(2) + TEXT("_L");
		return Bone;
	}
	/** A Unity local rotation reflected through the character's left-right plane (Unity X). */
	FVector MirrorEuler(const FVector& E) { return FVector(E.X, -E.Y, -E.Z); }
	int32 MirrorEffector(int32 E) { return E == 0 ? 0 : (E % 2 == 1 ? E + 1 : E - 1); }
}

// --- The scene view ---

FReply SOKStudioView::OnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event)
{
	Button = Event.GetEffectingButton();
	const FVector2D Local = Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition());
	PressAt = Local;
	Moved = 0.;
	DragAxis = -1;
	bLookDrag = false;
	bool bPoint = false;
	if (Game.IsValid() && Button == EKeys::LeftMouseButton)
	{
		// Pose points sit over the guide and are picked first; then the look point; then the guide's handles.
		FVector2D Look;
		if (Owner && Owner->PickPosePoint(Local)) bPoint = true;
		else if (Owner && Owner->LookPointScreen(Look) && FVector2D::Distance(Look, Local) < 10.) bLookDrag = true;
		else
		{
			DragAxis = Game->HitGuide(Local);
			if (DragAxis >= 0) Game->BeginGuideDrag(DragAxis);
		}
		Game->bUserDragging = DragAxis >= 0 || bLookDrag;
	}
	if (Game.IsValid() && Button == EKeys::RightMouseButton && FOKStudioSettings::Get().bFlyMode) Game->bFlying = true;
	// A picked point is a click only: the mouse that follows does not orbit.
	if (bPoint) Moved = 1e6;
	return FReply::Handled().CaptureMouse(SharedThis(this)).SetUserFocus(SharedThis(this), EFocusCause::Mouse);
}

FReply SOKStudioView::OnMouseMove(const FGeometry&, const FPointerEvent& Event)
{
	if (!HasMouseCapture() || !Game.IsValid()) return FReply::Unhandled();
	const FVector2D D = Event.GetCursorDelta();
	const bool bWasClick = Moved < 4.;
	Moved += D.Size();
	const FOKStudioSettings& S = FOKStudioSettings::Get();
	if (DragAxis >= 0) Game->DragGuide(D, Event.IsControlDown());
	else if (bLookDrag) { if (Owner) Owner->DragLookPoint(D); }
	else if (Button == EKeys::MiddleMouseButton) Game->Pan(-D.X, D.Y);
	else if (Button == EKeys::RightMouseButton && Game->bFlying) Game->Look(D.X * S.OrbitSpeed, -D.Y * S.OrbitSpeed);
	else if (Moved < 1e5 && !(bWasClick && Moved < 4.)) Game->Orbit(D.X * S.OrbitSpeed, -D.Y * S.OrbitSpeed);
	return FReply::Handled();
}

FReply SOKStudioView::OnMouseButtonUp(const FGeometry&, const FPointerEvent& Event)
{
	if (Game.IsValid())
	{
		// A left click (no drag) picks the object under the mouse; Ctrl / Shift add it to the selection or take it out.
		if (Button == EKeys::LeftMouseButton && DragAxis < 0 && !bLookDrag && Moved < 4.)
		{
			const int32 Id = Game->PickObject(PressAt);
			if (Event.IsControlDown() || Event.IsShiftDown()) { if (Id >= 0) Game->ToggleSelect(Id); }
			else Game->Select(Id);
		}
		if (DragAxis >= 0) Game->EndGuideDrag();
		Game->bFlying = false;
		Game->FlyKeys.Reset();
		Game->bUserDragging = false;
	}
	Button = FKey();
	DragAxis = -1;
	bLookDrag = false;
	return FReply::Handled().ReleaseMouseCapture();
}

int32 SOKStudioView::OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& Culling, FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const
{
	Layer = SCompoundWidget::OnPaint(Args, Geometry, Culling, Out, Layer, Style, bParentEnabled);
	if (!Game.IsValid()) return Layer;
	const FPaintGeometry Paint = Geometry.ToPaintGeometry();
	auto Lines = [&](const TArray<FVector2D>& Points, const FLinearColor& Color, float Thickness, int32 L)
	{
		TArray<FVector2f> P;
		for (const FVector2D& V : Points) P.Add(FVector2f(V));
		FSlateDrawElement::MakeLines(Out, L, Paint, P, ESlateDrawEffect::None, Color, true, Thickness);
	};
	if (Game->bHideHelpers) return Layer;
	// The ground grid: 50 cm cells over 10 m on the scene's floor, each line in short pieces so lines that pass behind
	// the view are cut instead of folding across the screen.
	if (Game->bShowGrid && Game->SceneActor)
	{
		const FTransform Floor = Game->SceneActor->GetActorTransform();
		for (int32 I = -10; I <= 10; ++I)
			for (int32 Dir = 0; Dir < 2; ++Dir)
			{
				const FLinearColor C = I == 0 ? FLinearColor(1.f, 1.f, 1.f, .45f) : FLinearColor(.8f, .8f, .8f, .18f);
				TArray<FVector2D> Run;
				for (int32 S = 0; S <= 20; ++S)
				{
					const float A = I * 50.f, B = -500.f + S * 50.f;
					const FVector P = Floor.TransformPosition(Dir == 0 ? FVector(A, B, 0.f) : FVector(B, A, 0.f));
					FVector2D At;
					if (ToScreen(Game.Get(), P, At)) { Run.Add(At); continue; }
					if (Run.Num() > 1) Lines(Run, C, 1.f, Layer + 1);
					Run.Reset();
				}
				if (Run.Num() > 1) Lines(Run, C, 1.f, Layer + 1);
			}
	}
	// The selection's bounds (thin), so several selected objects show.
	if (Game->Selection.Num() > 1)
		for (int32 Id : Game->Selection)
		{
			FBox Box;
			if (!Game->WorldBounds(Id, Box)) continue;
			FBox2D S(ForceInit);
			for (int32 C = 0; C < 8; ++C) { FVector2D P; if (ToScreen(Game.Get(), FVector((C & 1) ? Box.Max.X : Box.Min.X, (C & 2) ? Box.Max.Y : Box.Min.Y, (C & 4) ? Box.Max.Z : Box.Min.Z), P)) S += P; }
			if (S.bIsValid) Lines({ S.Min, FVector2D(S.Max.X, S.Min.Y), S.Max, FVector2D(S.Min.X, S.Max.Y), S.Min }, Id == Game->SelectedId ? FLinearColor(1.f, .85f, .2f, .8f) : FLinearColor(.6f, .8f, 1.f, .6f), 1.f, Layer + 1);
		}
	// The selected light's reach: a point light's range, a spot's cone, a directional light's direction.
	if (const FOKSceneObject* L = Game->Document->Find(Game->SelectedId); L && L->Kind == EOKSceneObjectKind::Light && Game->SceneActor)
	{
		const FTransform W = Game->Document->WorldTransform(L->Id) * Game->SceneActor->GetActorTransform();
		const FVector O = W.GetLocation(), Fwd = W.GetRotation().GetForwardVector();
		const FLinearColor C(1.f, .9f, .4f, .8f);
		auto Circle = [&](const FVector& Center, const FVector& U, const FVector& V, float R)
		{
			TArray<FVector2D> P;
			for (int32 I = 0; I <= 32; ++I) { FVector2D S; const float T = 2.f * UE_PI * I / 32.f; if (ToScreen(Game.Get(), Center + (U * FMath::Cos(T) + V * FMath::Sin(T)) * R, S)) P.Add(S); else { P.Reset(); break; } }
			if (P.Num() > 2) Lines(P, C, 1.f, Layer + 1);
		};
		if (L->Light.Type == EOKSceneLightType::Point)
		{
			Circle(O, FVector::ForwardVector, FVector::RightVector, L->Light.Range);
			Circle(O, FVector::ForwardVector, FVector::UpVector, L->Light.Range);
			Circle(O, FVector::RightVector, FVector::UpVector, L->Light.Range);
		}
		else if (L->Light.Type == EOKSceneLightType::Spot)
		{
			const FVector U = W.GetRotation().GetRightVector(), V = W.GetRotation().GetUpVector();
			const float R = FMath::Tan(FMath::DegreesToRadians(FMath::Clamp(L->Light.SpotAngle, 1.f, 80.f) * 0.5f)) * L->Light.Range;
			const FVector End = O + Fwd * L->Light.Range;
			Circle(End, U, V, R);
			FVector2D A;
			if (ToScreen(Game.Get(), O, A))
				for (int32 I = 0; I < 8; ++I) { FVector2D B; const float T = 2.f * UE_PI * I / 8.f; if (ToScreen(Game.Get(), End + (U * FMath::Cos(T) + V * FMath::Sin(T)) * R, B)) Lines({ A, B }, C, 1.f, Layer + 1); }
		}
		else if (L->Light.Type == EOKSceneLightType::Area)
		{
			// The emitting rectangle and its facing.
			const FVector U = W.GetRotation().GetRightVector() * (L->Light.AreaWidth * 0.5f), V = W.GetRotation().GetUpVector() * (L->Light.AreaHeight * 0.5f);
			TArray<FVector2D> Rect;
			for (const FVector& Corner : { O - U - V, O + U - V, O + U + V, O - U + V, O - U - V }) { FVector2D S; if (ToScreen(Game.Get(), Corner, S)) Rect.Add(S); }
			if (Rect.Num() == 5) Lines(Rect, C, 1.5f, Layer + 1);
			FVector2D A, B;
			if (ToScreen(Game.Get(), O, A) && ToScreen(Game.Get(), O + Fwd * 60.f, B)) Lines({ A, B }, C, 2.f, Layer + 1);
		}
		else
		{
			FVector2D A, B;
			if (ToScreen(Game.Get(), O, A) && ToScreen(Game.Get(), O + Fwd * 120.f, B)) { Lines({ A, B }, C, 2.f, Layer + 1); Circle(O, W.GetRotation().GetRightVector(), W.GetRotation().GetUpVector(), 15.f); }
		}
	}
	// The guide.
	TArray<UOKStudioGame::FGuideHandle> Handles;
	FVector2D Center;
	double CmPerPixel = 0.;
	if (Game->GuideHandles(Handles, Center, CmPerPixel))
	{
		static const FLinearColor Colors[] = { FLinearColor(1.f, .22f, .2f), FLinearColor(.3f, 1.f, .3f), FLinearColor(.3f, .55f, 1.f), FLinearColor(.9f, .9f, .9f) };
		static const FLinearColor Planes[] = { FLinearColor(.65f, .6f, .25f, .9f), FLinearColor(.3f, .75f, .65f, .9f), FLinearColor(.65f, .4f, .6f, .9f) };
		for (const UOKStudioGame::FGuideHandle& H : Handles)
		{
			TArray<FVector2D> P = H.Points;
			if (H.bClosed && P.Num() > 2) { const FVector2D First = P[0]; P.Add(First); }
			const bool bActive = H.Axis == DragAxis;
			const FLinearColor C = bActive ? FLinearColor::Yellow : H.Axis >= 10 ? Colors[H.Axis - 10] * 0.8f : H.Axis >= 4 && H.Axis <= 6 ? Planes[H.Axis - 4] : Colors[FMath::Clamp(H.Axis, 0, 3)];
			Lines(P, C, bActive ? 4.f : (H.bFilled ? 3.f : (H.Axis >= 10 ? 2.f : 2.5f)), Layer + 2);
		}
	}
	// Pose points: IK targets (red ends, blue elbows and knees), FK bones with lines to their parents.
	TArray<SOKStudioPanel::FPosePoint> Points;
	if (Owner && Owner->PosePoints(Points))
	{
		for (const SOKStudioPanel::FPosePoint& P : Points) if (P.bParent) Lines({ P.Parent, P.At }, FLinearColor(1.f, 1.f, 1.f, .35f), 1.f, Layer + 3);
		for (const SOKStudioPanel::FPosePoint& P : Points)
		{
			const bool bActive = (P.Ik >= 0 && Game->GuideIk == P.Ik) || (!P.Bone.IsEmpty() && Game->GuideBone == P.Bone);
			const FLinearColor C = bActive ? FLinearColor::Yellow : P.Ik >= 9 ? FLinearColor(.3f, .5f, 1.f) : P.Ik >= 0 ? FLinearColor(1.f, .25f, .2f) : (P.bOn ? FLinearColor(.4f, 1.f, .4f) : FLinearColor(.75f, .95f, .75f));
			const float R = bActive ? 6.f : 4.f;
			FSlateDrawElement::MakeBox(Out, Layer + 4, Geometry.ToPaintGeometry(FVector2f(2.f * R, 2.f * R), FSlateLayoutTransform(FVector2f(P.At - FVector2D(R, R)))), Brush(C), ESlateDrawEffect::None, C);
		}
	}
	// The look point (draggable).
	FVector2D Look;
	if (Owner && Owner->LookPointScreen(Look))
	{
		TArray<FVector2D> Ring;
		for (int32 I = 0; I <= 16; ++I) Ring.Add(Look + FVector2D(FMath::Cos(I * UE_PI / 8.), FMath::Sin(I * UE_PI / 8.)) * 8.);
		Lines(Ring, FLinearColor(.4f, 1.f, 1.f), 2.f, Layer + 4);
		Lines({ Look - FVector2D(12., 0.), Look + FVector2D(12., 0.) }, FLinearColor(.4f, 1.f, 1.f), 1.f, Layer + 4);
		Lines({ Look - FVector2D(0., 12.), Look + FVector2D(0., 12.) }, FLinearColor(.4f, 1.f, 1.f), 1.f, Layer + 4);
	}
	return Layer + 5;
}

// --- Pose points ---

int32 FOKSceneCharacter::FkPartOf(const FString& Bone)
{
	const FString B = Bone.ToLower();
	if (B.Contains(TEXT("hair"))) return 2;
	if (B.Contains(TEXT("_sk_")) || B.StartsWith(TEXT("cf_j_sk"))) return 6;
	if (B.Contains(TEXT("neck")) || B.Contains(TEXT("head"))) return 3;
	if (B.Contains(TEXT("mune")) || B.Contains(TEXT("bust"))) return 1;
	const bool bHand = B.Contains(TEXT("hand")) || B.Contains(TEXT("thumb")) || B.Contains(TEXT("index")) || B.Contains(TEXT("middle")) || B.Contains(TEXT("ring")) || B.Contains(TEXT("little"));
	if (bHand) return B.EndsWith(TEXT("_r")) ? 5 : 4;
	return 0;
}

int32 FOKSceneCharacter::IkPartOf(int32 Effector)
{
	switch (Effector)
	{
	case 1: case 5: case 9: return 1;
	case 2: case 6: case 10: return 2;
	case 3: case 7: case 11: return 3;
	case 4: case 8: case 12: return 4;
	default: return 0;
	}
}

bool SOKStudioPanel::IsPoseMode() const
{
	const FOKSceneObject* O = Obj();
	return Game.IsValid() && O && O->Kind == EOKSceneObjectKind::Character && Page == EPage::Bones && BonePage <= 1;
}

bool SOKStudioPanel::PosePoints(TArray<FPosePoint>& Out) const
{
	Out.Reset();
	if (!IsPoseMode() || !Game->SceneActor || Game->bHideHelpers) return false;
	const FOKSceneObject* O = Obj();
	AKKPreviewActor* P = Game->SceneActor->PreviewOf(O->Id);
	if (!P) return false;
	if (BonePage == 1 && bShowIkPoints)
	{
		FString Error;
		const bool bReady = P->bStudioIkReady || P->Session.PrepareStudioIk(Error);
		if (bReady) P->bStudioIkReady = true;
		for (int32 E = 0; E < 13; ++E)
		{
			if (!IkPartOn(IkPartOf(E))) continue;
			FVector World;
			if (!Game->IkTargetWorld(E, World))
			{
				FVector Unity;
				if (!bReady || !P->Session.IkEffectorPosition(E, Unity)) continue;
				World = P->GetActorTransform().TransformPosition(AKKPreviewActor::UnityToActorLocal(Unity));
			}
			FPosePoint Point;
			Point.Ik = E;
			Point.bOn = O->Character.Ik.ContainsByPredicate([E](const FOKSceneIkTarget& T) { return T.Effector == E; });
			if (ToScreen(Game.Get(), World, Point.At)) Out.Add(Point);
		}
	}
	if (BonePage == 0 && bShowFkPoints)
	{
		UPoseableMeshComponent* Mesh = P->BodySkel ? P->BodySkel.Get() : (P->SkelFollowers.Num() ? P->SkelFollowers[0].Get() : nullptr);
		TMap<FName, FVector2D> Shown;
		for (const FOKStudioFkBone& B : FOKStudioCatalog::Get().FkBones)
		{
			if (!FkPartOn(FkPartOf(B.Bone))) continue;
			FTransform World;
			FPosePoint Point;
			if (!OKStudioGameInternal::BoneWorld(P, B.Bone, World) || !ToScreen(Game.Get(), World.GetLocation(), Point.At)) continue;
			Point.Bone = B.Bone;
			Point.bOn = O->Character.Fk.ContainsByPredicate([&B](const FOKSceneFkBone& F) { return F.Bone == B.Bone; });
			Shown.Add(FName(*B.Bone), Point.At);
			Out.Add(Point);
		}
		// A line to the nearest shown ancestor.
		if (Mesh)
			for (FPosePoint& Point : Out)
			{
				FName Parent = Mesh->GetParentBone(FName(*Point.Bone));
				for (int32 Up = 0; Up < 6 && !Parent.IsNone(); ++Up, Parent = Mesh->GetParentBone(Parent))
					if (const FVector2D* At = Shown.Find(Parent)) { Point.bParent = true; Point.Parent = *At; break; }
			}
	}
	return Out.Num() > 0;
}

bool SOKStudioPanel::PickPosePoint(const FVector2D& At)
{
	TArray<FPosePoint> Points;
	if (!PosePoints(Points)) return false;
	const FPosePoint* Best = nullptr;
	double BestDistance = 7.;
	for (const FPosePoint& P : Points) { const double D = FVector2D::Distance(P.At, At); if (D < BestDistance) { BestDistance = D; Best = &P; } }
	if (!Best) return false;
	FOKSceneObject* X = Obj();
	if (Best->Ik >= 0)
	{
		// An IK point: its target turns on where the limb is, and the guide moves it.
		if (X && !X->Character.Ik.ContainsByPredicate([E = Best->Ik](const FOKSceneIkTarget& T) { return T.Effector == E; })) SetIkEnabled(*X, Best->Ik, true);
		IkEffector = Best->Ik;
		Game->GuideIk = Best->Ik;
		Game->GuideBone.Reset();
	}
	else
	{
		FkBone = Best->Bone;
		Game->GuideBone = Best->Bone;
		Game->GuideIk = -1;
		OpenBones.Add(Best->Bone);
	}
	DeferRebuild();
	return true;
}

bool SOKStudioPanel::LookPointScreen(FVector2D& Out) const
{
	const FOKSceneObject* O = Obj();
	if (!Game.IsValid() || !O || O->Kind != EOKSceneObjectKind::Character || Page != EPage::Bones || !Game->SceneActor) return false;
	const bool bEyes = BonePage == 2 && O->Character.EyesLook == EOKSceneLook::Point;
	const bool bNeck = BonePage == 3 && O->Character.NeckLook == EOKSceneLook::Point;
	const AKKPreviewActor* P = Game->SceneActor->PreviewOf(O->Id);
	return (bEyes || bNeck) && P && ToScreen(Game.Get(), P->GetActorTransform().TransformPosition(O->Character.LookPoint), Out);
}

void SOKStudioPanel::DragLookPoint(const FVector2D& Delta)
{
	FOKSceneObject* O = Obj();
	APlayerController* PC = ViewController(Game.Get());
	const AKKPreviewActor* P = O && Game->SceneActor ? Game->SceneActor->PreviewOf(O->Id) : nullptr;
	if (!O || !PC || !PC->PlayerCameraManager || !P) return;
	// Along the view's plane at the point's depth.
	const FVector World = P->GetActorTransform().TransformPosition(O->Character.LookPoint);
	const FRotator ViewRotation = PC->PlayerCameraManager->GetCameraRotation();
	const FVector Right = FRotationMatrix(ViewRotation).GetUnitAxis(EAxis::Y), Up = FRotationMatrix(ViewRotation).GetUnitAxis(EAxis::Z);
	FVector2D A, B;
	if (!ToScreen(Game.Get(), World, A) || !ToScreen(Game.Get(), World + Right * 10., B)) return;
	const double CmPerPixel = 10. / FMath::Max(FVector2D::Distance(A, B), 1e-3);
	const FVector Move = (Right * Delta.X - Up * Delta.Y) * CmPerPixel;
	O->Character.LookPoint += P->GetActorTransform().InverseTransformVector(Move);
	Changed();
}

// --- Pose editing ---

bool SOKStudioPanel::MirrorPose(FOKSceneObject& X, int32 Mode)
{
	if (X.Kind != EOKSceneObjectKind::Character) return false;
	// Every FK bone's rotation now: its FK value, or the pose it shows.
	TMap<FString, FVector> Now;
	for (const FOKStudioFkBone& B : FOKStudioCatalog::Get().FkBones)
	{
		const FOKSceneFkBone* F = X.Character.Fk.FindByPredicate([&B](const FOKSceneFkBone& Y) { return Y.Bone == B.Bone; });
		Now.Add(B.Bone, F ? F->Euler : PosedEuler(X, B.Bone));
	}
	int32 Changed_ = 0;
	for (const TPair<FString, FVector>& B : Now)
	{
		const FString Partner = MirrorName(B.Key);
		const FVector* From = Now.Find(Partner);
		if (!From) continue;
		// 0 the whole pose, 1 the left side onto the right, 2 the right onto the left.
		if (Mode == 1 && !B.Key.EndsWith(TEXT("_R"))) continue;
		if (Mode == 2 && !B.Key.EndsWith(TEXT("_L"))) continue;
		SetFk(X, B.Key, MirrorEuler(*From));
		++Changed_;
	}
	// IK targets: positions reflected through the character's left-right plane (actor X), partners swapped.
	TArray<FOKSceneIkTarget> Ik = X.Character.Ik;
	TArray<FOKSceneIkTarget> Out;
	for (const FOKSceneIkTarget& T : Ik)
	{
		const int32 Partner = MirrorEffector(T.Effector);
		const bool bTargetSide = Mode == 0 || (Mode == 1 && T.Effector % 2 == 0 && T.Effector > 0) || (Mode == 2 && T.Effector % 2 == 1);
		const FOKSceneIkTarget* Source = Ik.FindByPredicate([Partner](const FOKSceneIkTarget& Y) { return Y.Effector == Partner; });
		if (!bTargetSide || !Source) { Out.Add(T); continue; }
		FOKSceneIkTarget M = *Source;
		M.Effector = T.Effector;
		M.Position.X = -M.Position.X;
		M.Euler = MirrorEuler(M.Euler);
		Out.Add(M);
	}
	X.Character.Ik = Out;
	Changed();
	DeferRebuild();
	SetStatus(FString::Printf(TEXT("鏡像 %d 根骨骼"), Changed_));
	return true;
}

void SOKStudioPanel::IkFromFk(FOKSceneObject& X, int32 Part)
{
	if (X.Kind != EOKSceneObjectKind::Character || !Game->SceneActor) return;
	const int32 Id = X.Id;
	// Where the limbs are under FK alone: the targets go off for a moment, the pose is read, they come back there.
	// One part: only its targets (all of its effectors when none is on), the others stay.
	auto Mine = [Part](int32 E) { return Part < 0 || IkPartOf(E) == Part; };
	TArray<int32> Effectors;
	for (const FOKSceneIkTarget& T : X.Character.Ik) if (Mine(T.Effector)) Effectors.Add(T.Effector);
	if (Effectors.IsEmpty()) for (int32 E = 0; E < 13; ++E) if (Mine(E)) Effectors.Add(E);
	X.Character.Ik.RemoveAll([&Mine](const FOKSceneIkTarget& T) { return Mine(T.Effector); });
	Changed();
	Game->SyncDocumentNow();
	Game->SceneActor->UpdateLooks(0.f);
	AKKPreviewActor* P = Game->SceneActor->PreviewOf(Id);
	FOKSceneObject* Y = Game->Document->Find(Id);
	FString Error;
	if (!P || !Y || !P->Session.PrepareStudioIk(Error)) { SetStatus(TEXT("IK←FK 失敗：") + Error); return; }
	P->bStudioIkReady = true;
	for (int32 E : Effectors)
	{
		FVector Unity;
		if (P->Session.IkEffectorPosition(E, Unity)) Y->Character.Ik.Add({ E, AKKPreviewActor::UnityToActorLocal(Unity) });
	}
	Changed();
	DeferRebuild();
	SetStatus(FString::Printf(TEXT("IK←FK：%d 個目標"), Effectors.Num()));
}

void SOKStudioPanel::FkFromPose(FOKSceneObject& Object, int32 Part)
{
	FOKSceneObject* Y = &Object;
	if (Part >= 0 && Game.IsValid() && Game->SceneActor)
	{
		// One part starts from the animation: its FK goes for a moment so the pose read is the animation's.
		const int32 Id = Y->Id;
		Y->Character.Fk.RemoveAll([Part](const FOKSceneFkBone& B) { return FkPartOf(B.Bone) == Part; });
		Changed();
		Game->SyncDocumentNow();
		Game->SceneActor->UpdateLooks(0.f);
		Y = Game->Document->Find(Id);
		if (!Y) return;
	}
	FOKSceneObject& X = *Y;
	int32 Count = 0;
	for (const FOKStudioFkBone& B : FOKStudioCatalog::Get().FkBones)
	{
		const int32 Of = FkPartOf(B.Bone);
		if (Part >= 0 ? Of == Part : X.Character.FkPartOn(Of)) { SetFk(X, B.Bone, PosedEuler(X, B.Bone)); ++Count; }
	}
	Changed();
	DeferRebuild();
	SetStatus(FString::Printf(TEXT("FK←目前姿勢：%d 根骨骼"), Count));
}

void SOKStudioPanel::FkFromIk(FOKSceneObject& X)
{
	if (X.Kind != EOKSceneObjectKind::Character) return;
	// The pose IK makes now (body and hands, where IK reaches) becomes FK, and the IK targets go (HS2Studio6 FK←IK).
	int32 Count = 0;
	for (const FOKStudioFkBone& B : FOKStudioCatalog::Get().FkBones)
	{
		const int32 Part = FkPartOf(B.Bone);
		if (Part == 0 || Part == 4 || Part == 5) { SetFk(X, B.Bone, PosedEuler(X, B.Bone)); ++Count; }
	}
	X.Character.Ik.Reset();
	if (Game.IsValid()) Game->GuideIk = -1;
	Changed();
	DeferRebuild();
	SetStatus(FString::Printf(TEXT("FK←IK：%d 根骨骼"), Count));
}

void SOKStudioPanel::BoneTree(const TArray<FString>& Bones, TFunction<FString(const FString&)> Show, TFunction<FVector(FOKSceneObject&, const FString&)> Get,
	TFunction<void(FOKSceneObject&, const FString&, const FVector&)> Set, TFunction<void(const FString&)> OnPick, TFunction<bool(const FString&)> IsSet)
{
	Props->AddSlot().AutoHeight().Padding(0.f, 2.f)
	[
		SNew(SBox).HeightOverride(18.f)
		[SNew(SEditableTextBox).Style(&TextBoxStyle()).HintText(FText::FromString(TEXT("搜尋骨骼"))).Text(FText::FromString(BoneSearch))
			.OnTextCommitted_Lambda([this](const FText& T, ETextCommit::Type) { BoneSearch = T.ToString(); DeferRebuild(); })]
	];
	int32 Listed = 0;
	for (const FString& Bone : Bones)
	{
		const FString Name = Show ? Show(Bone) : Bone;
		if (!BoneSearch.IsEmpty() && !Name.Contains(BoneSearch) && !Bone.Contains(BoneSearch)) continue;
		if (++Listed > 300) { Label(TEXT("（太多了，請用搜尋）")); break; }
		const bool bOpen = OpenBones.Contains(Bone);
		Props->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 1.f)
		[
			Button(FText::FromString(FString::Printf(TEXT("%s %s"), bOpen ? TEXT("▼") : TEXT("▶"), *Name)), [this, Bone, OnPick]
			{
				if (OpenBones.Contains(Bone)) OpenBones.Remove(Bone); else OpenBones.Add(Bone);
				if (OnPick) OnPick(Bone);
				DeferRebuild();
			}, 0.f, 17.f, TAttribute<bool>::CreateLambda([IsSet, Bone] { return IsSet && IsSet(Bone); }), 10.f, RowFill())
		];
		if (!bOpen) continue;
		for (int32 Axis = 0; Axis < 3; ++Axis)
			Float(FString(TEXT("　　")) + FString(TEXT("XYZ")).Mid(Axis, 1), [Get, Bone, Axis](FOKSceneObject& X) { return float(Get(X, Bone)[Axis]); },
				[Get, Set, Bone, Axis](FOKSceneObject& X, float V) { FVector E = Get(X, Bone); E[Axis] = V; Set(X, Bone, E); }, -360.f, 360.f, 0.5f);
	}
}

void SOKStudioPanel::FkPage(FOKSceneObject& O)
{
	const FOKStudioCatalog& C = FOKStudioCatalog::Get();
	Label(FString::Printf(TEXT("FK（%d 根骨骼有設定）"), O.Character.Fk.Num()));
	if (C.FkBones.IsEmpty()) { Label(TEXT("基礎庫沒有 FK 骨骼清單")); return; }
	static const TCHAR* Parts[] = { TEXT("身體"), TEXT("胸"), TEXT("頭髮"), TEXT("頸"), TEXT("左手"), TEXT("右手"), TEXT("裙子") };
	TSharedRef<SWrapBox> PartBox = SNew(SWrapBox).UseAllottedSize(true);
	for (int32 I = 0; I < 7; ++I)
		PartBox->AddSlot().Padding(1.f)[Button(FText::FromString(Parts[I]), [this, I] { if (FOKSceneObject* X = Obj()) { X->Character.FkPartsOff ^= 1 << I; Changed(); DeferRebuild(); } }, 42.f, 17.f,
			TAttribute<bool>::CreateLambda([this, I] { return FkPartOn(I); }), 10.f)];
	PartBox->AddSlot().Padding(1.f)[Button(FText::FromString(TEXT("全部")), [this] { if (FOKSceneObject* X = Obj()) { X->Character.FkPartsOff = X->Character.FkPartsOff ? 0 : 0x7f; Changed(); DeferRebuild(); } }, 42.f, 17.f,
		TAttribute<bool>::CreateLambda([this] { const FOKSceneObject* X = Obj(); return X && X->Character.FkPartsOff == 0; }), 10.f)];
	Props->AddSlot().AutoHeight()[PartBox];
	// 初始化: one part's FK values taken from the animation as it plays now (HS2Studio6 FK 初始化).
	TSharedRef<SWrapBox> InitBox = SNew(SWrapBox).UseAllottedSize(true);
	InitBox->AddSlot().Padding(1.f).VAlign(VAlign_Center)[OKStudioStyle::Label(FText::FromString(TEXT("初始化")), 10.f)];
	for (int32 I = 0; I < 7; ++I)
		InitBox->AddSlot().Padding(1.f)[Button(FText::FromString(Parts[I]), [this, I] { if (FOKSceneObject* X = Obj()) FkFromPose(*X, I); }, 36.f, 16.f, false, 9.f)];
	Props->AddSlot().AutoHeight()[InitBox];
	// 復位: one part's FK values go (the part follows the animation again until posed).
	TSharedRef<SWrapBox> ResetBox = SNew(SWrapBox).UseAllottedSize(true);
	ResetBox->AddSlot().Padding(1.f).VAlign(VAlign_Center)[OKStudioStyle::Label(FText::FromString(TEXT("復位")), 10.f)];
	for (int32 I = 0; I < 7; ++I)
		ResetBox->AddSlot().Padding(1.f)[Button(FText::FromString(Parts[I]), [this, I]
		{
			if (FOKSceneObject* X = Obj()) { X->Character.Fk.RemoveAll([I](const FOKSceneFkBone& B) { return FkPartOf(B.Bone) == I; }); Changed(); DeferRebuild(); }
		}, 36.f, 16.f, false, 9.f)];
	Props->AddSlot().AutoHeight()[ResetBox];
	// 關節補正 (Studio's joint correction): ExpressionBone straightens these joints' skin as they bend.
	static const TCHAR* Joints[] = { TEXT("左臂"), TEXT("右臂"), TEXT("左腿"), TEXT("右腿"), TEXT("左前臂"), TEXT("右前臂"), TEXT("左大腿"), TEXT("右大腿") };
	TSharedRef<SWrapBox> JointBox = SNew(SWrapBox).UseAllottedSize(true);
	JointBox->AddSlot().Padding(1.f).VAlign(VAlign_Center)[OKStudioStyle::Label(FText::FromString(TEXT("關節補正")), 10.f)];
	for (int32 I = 0; I < 8; ++I)
		JointBox->AddSlot().Padding(1.f)[Button(FText::FromString(Joints[I]), [this, I] { if (FOKSceneObject* X = Obj()) { X->Character.JointCorrection ^= 1 << I; Changed(); } }, 42.f, 17.f,
			TAttribute<bool>::CreateLambda([this, I] { const FOKSceneObject* X = Obj(); return X && (X->Character.JointCorrection & (1 << I)); }), 10.f)];
	JointBox->AddSlot().Padding(1.f)[Button(FText::FromString(TEXT("全部")), [this] { if (FOKSceneObject* X = Obj()) { X->Character.JointCorrection = (X->Character.JointCorrection & 0xff) == 0xff ? 0 : 0xff; Changed(); } }, 42.f, 17.f,
		TAttribute<bool>::CreateLambda([this] { const FOKSceneObject* X = Obj(); return X && (X->Character.JointCorrection & 0xff) == 0xff; }), 10.f)];
	Props->AddSlot().AutoHeight()[JointBox];
	Props->AddSlot().AutoHeight().Padding(0.f, 1.f)[CheckBox(TEXT("畫面上顯示骨點（點骨點用把手轉它）"), [this] { return bShowFkPoints; }, [this](bool B) { bShowFkPoints = B; })];
	Props->AddSlot().AutoHeight().Padding(0.f, 1.f)[CheckBox(TEXT("用 3D 把手轉選中骨骼"), [this] { return Game.IsValid() && !Game->GuideBone.IsEmpty(); }, [this](bool B) { if (Game.IsValid()) { Game->GuideBone = B ? FkBone : FString(); if (B) Game->GuideIk = -1; } })];
	auto Buttons = [this](std::initializer_list<TPair<const TCHAR*, TFunction<void()>>> List)
	{
		TSharedRef<SHorizontalBox> Box = SNew(SHorizontalBox);
		for (const TPair<const TCHAR*, TFunction<void()>>& B : List) Box->AddSlot().FillWidth(1.f).Padding(0.f, 0.f, 1.f, 0.f)[Button(FText::FromString(B.Key), B.Value, 0.f, 18.f, false, 10.f)];
		Props->AddSlot().AutoHeight().Padding(0.f, 1.f)[Box];
	};
	Buttons({ { TEXT("FK←姿勢"), Cmd(TEXT("pose.fkfrompose")) }, { TEXT("IK←FK"), Cmd(TEXT("pose.ikfromfk")) }, { TEXT("清除 FK"), [this] { if (FOKSceneObject* X = Obj()) { X->Character.Fk.Reset(); Changed(); DeferRebuild(); } } } });
	Buttons({ { TEXT("鏡像全部"), Cmd(TEXT("pose.mirror"), TEXT("all")) }, { TEXT("左→右"), Cmd(TEXT("pose.mirror"), TEXT("l2r")) }, { TEXT("右→左"), Cmd(TEXT("pose.mirror"), TEXT("r2l")) } });
	Buttons({ { TEXT("複製姿勢"), Cmd(TEXT("pose.copy")) }, { TEXT("貼上姿勢"), Cmd(TEXT("pose.paste")) }, { TEXT("姿勢庫…"), Cmd(TEXT("app.poselibrary")) } });
	// The chosen bone on the timeline (its own track: it moves between the keys that hold it).
	Buttons({ { TEXT("骨骼打 key"), Cmd(TEXT("timeline.bonekey")) }, { TEXT("骨骼刪 key"), Cmd(TEXT("timeline.boneunkey")) } });
	if (Game.IsValid() && !Game->GuideBone.IsEmpty()) Game->GuideBone = FkBone.IsEmpty() ? Game->GuideBone : FkBone;
	TArray<FString> Bones;
	for (int32 Op = 0; Op < 2; ++Op)
		for (const FOKStudioFkBone& B : C.FkBones)
			if ((B.Op == 0) == (Op == 0) && FkPartOn(FkPartOf(B.Bone))) Bones.Add(B.Bone);
	BoneTree(Bones, [](const FString& Bone) { const FOKStudioFkBone* B = FOKStudioCatalog::Get().FindFkBone(Bone); return B ? FString::Printf(TEXT("%s %s"), *B->Name, *Bone) : Bone; },
		[this](FOKSceneObject& X, const FString& Bone) { const FOKSceneFkBone* B = X.Character.Fk.FindByPredicate([&Bone](const FOKSceneFkBone& F) { return F.Bone == Bone; }); return B ? B->Euler : PosedEuler(X, Bone); },
		[](FOKSceneObject& X, const FString& Bone, const FVector& E) { SetFk(X, Bone, E); },
		[this](const FString& Bone) { FkBone = Bone; if (Game.IsValid()) { Game->GuideBone = Bone; Game->GuideIk = -1; } },
		[this](const FString& Bone) { const FOKSceneObject* X = Obj(); return X && X->Character.Fk.ContainsByPredicate([&Bone](const FOKSceneFkBone& F) { return F.Bone == Bone; }); });
}

void SOKStudioPanel::IkPage(FOKSceneObject& O)
{
	static const TCHAR* Names[13] = { TEXT("身體"), TEXT("左肩"), TEXT("右肩"), TEXT("左大腿"), TEXT("右大腿"), TEXT("左手"), TEXT("右手"), TEXT("左腳"), TEXT("右腳"),
		TEXT("左手肘"), TEXT("右手肘"), TEXT("左膝"), TEXT("右膝") };
	static const TCHAR* Parts[] = { TEXT("身體"), TEXT("左手"), TEXT("右手"), TEXT("左腳"), TEXT("右腳") };
	Label(FString::Printf(TEXT("IK（%d 個目標）"), O.Character.Ik.Num()));
	TSharedRef<SWrapBox> PartBox = SNew(SWrapBox).UseAllottedSize(true);
	for (int32 I = 0; I < 5; ++I)
		PartBox->AddSlot().Padding(1.f)[Button(FText::FromString(Parts[I]), [this, I] { if (FOKSceneObject* X = Obj()) { X->Character.IkPartsOff ^= 1 << I; Changed(); DeferRebuild(); } }, 42.f, 17.f,
			TAttribute<bool>::CreateLambda([this, I] { return IkPartOn(I); }), 10.f)];
	PartBox->AddSlot().Padding(1.f)[Button(FText::FromString(TEXT("全部")), [this] { if (FOKSceneObject* X = Obj()) { X->Character.IkPartsOff = X->Character.IkPartsOff ? 0 : 0x1f; Changed(); DeferRebuild(); } }, 42.f, 17.f,
		TAttribute<bool>::CreateLambda([this] { const FOKSceneObject* X = Obj(); return X && X->Character.IkPartsOff == 0; }), 10.f)];
	Props->AddSlot().AutoHeight()[PartBox];
	// 復位: one part's targets back onto where its limbs are under FK (HS2Studio6 IK 复位).
	TSharedRef<SWrapBox> ResetBox = SNew(SWrapBox).UseAllottedSize(true);
	ResetBox->AddSlot().Padding(1.f).VAlign(VAlign_Center)[OKStudioStyle::Label(FText::FromString(TEXT("復位")), 10.f)];
	for (int32 I = 0; I < 5; ++I)
		ResetBox->AddSlot().Padding(1.f)[Button(FText::FromString(Parts[I]), [this, I] { if (FOKSceneObject* X = Obj()) IkFromFk(*X, I); }, 36.f, 16.f, false, 9.f)];
	Props->AddSlot().AutoHeight()[ResetBox];
	Props->AddSlot().AutoHeight().Padding(0.f, 1.f)[CheckBox(TEXT("畫面上顯示 IK 點（點一下啟用並用把手移動）"), [this] { return bShowIkPoints; }, [this](bool B) { bShowIkPoints = B; })];
	// The effectors: green while on; the chosen one is outlined by its name in the heading below.
	TSharedRef<SWrapBox> Grid = SNew(SWrapBox).UseAllottedSize(true);
	for (int32 E = 0; E < 13; ++E)
	{
		if (!IkPartOn(IkPartOf(E))) continue;
		Grid->AddSlot().Padding(1.f)[Button(FText::FromString(Names[E]), [this, E] { IkEffector = E; if (Game.IsValid() && Game->GuideIk >= 0) Game->GuideIk = E; DeferRebuild(); }, 52.f, 18.f,
			TAttribute<bool>::CreateLambda([this, E] { const FOKSceneObject* X = Obj(); return X && X->Character.Ik.ContainsByPredicate([E](const FOKSceneIkTarget& T) { return T.Effector == E; }); }), 10.f,
			IkEffector == E ? FLinearColor(0.45f, 0.35f, 0.1f) : ButtonFill())];
	}
	Props->AddSlot().AutoHeight().Padding(0.f, 2.f)[Grid];
	Label(TEXT("目前：") + FString(Names[FMath::Clamp(IkEffector, 0, 12)]));
	Check(TEXT("啟用這個 IK 目標"), [this](FOKSceneObject& X) { return X.Character.Ik.ContainsByPredicate([this](const FOKSceneIkTarget& T) { return T.Effector == IkEffector; }); },
		[this](FOKSceneObject& X, bool bOn) { SetIkEnabled(X, IkEffector, bOn); DeferRebuild(); });
	if (O.Character.Ik.ContainsByPredicate([this](const FOKSceneIkTarget& T) { return T.Effector == IkEffector; }))
	{
		Props->AddSlot().AutoHeight().Padding(0.f, 1.f)[CheckBox(TEXT("用 3D 把手移動 IK 目標"), [this] { return Game.IsValid() && Game->GuideIk == IkEffector; },
			[this](bool B) { if (Game.IsValid()) { Game->GuideIk = B ? IkEffector : -1; if (B) Game->GuideBone.Reset(); } })];
		for (int32 Axis = 0; Axis < 3; ++Axis)
			Float(FString(TEXT("XYZ")).Mid(Axis, 1),
				[this, Axis](FOKSceneObject& X) { const FOKSceneIkTarget* T = X.Character.Ik.FindByPredicate([this](const FOKSceneIkTarget& Y) { return Y.Effector == IkEffector; }); return T ? float(T->Position[Axis]) : 0.f; },
				[this, Axis](FOKSceneObject& X, float V) { if (FOKSceneIkTarget* T = X.Character.Ik.FindByPredicate([this](const FOKSceneIkTarget& Y) { return Y.Effector == IkEffector; })) T->Position[Axis] = V; }, -300.f, 300.f, 0.5f);
		// Hands and feet can also keep a direction (HS2Studio6 IK rotation): in rotate mode (E) the handle turns them.
		if (OKStudioGameInternal::IkEffectorTurns(IkEffector))
		{
			auto Target = [this](FOKSceneObject& X) { return X.Character.Ik.FindByPredicate([this](const FOKSceneIkTarget& Y) { return Y.Effector == IkEffector; }); };
			Check(TEXT("用旋轉（把手按 E 轉方向）"), [Target](FOKSceneObject& X) { const FOKSceneIkTarget* T = Target(X); return T && T->bUseRotation; },
				[this, Target](FOKSceneObject& X, bool bOn)
				{
					FOKSceneIkTarget* T = Target(X);
					if (!T) return;
					// Switched on, it starts from where the hand / foot points now, so nothing jumps.
					if (bOn && !T->bUseRotation && Game.IsValid() && Game->SceneActor) OKStudioGameInternal::IkEffectorEuler(Game->SceneActor->PreviewOf(X.Id), IkEffector, T->Euler);
					T->bUseRotation = bOn;
					DeferRebuild();
				});
			if (const FOKSceneIkTarget* T = O.Character.Ik.FindByPredicate([this](const FOKSceneIkTarget& Y) { return Y.Effector == IkEffector; }); T && T->bUseRotation)
			{
				for (int32 Axis = 0; Axis < 3; ++Axis)
					Float(FString(TEXT("轉 X轉 Y轉 Z")).Mid(Axis * 3, 3).TrimStart(),
						[Target, Axis](FOKSceneObject& X) { const FOKSceneIkTarget* Y = Target(X); return Y ? float(Y->Euler[Axis]) : 0.f; },
						[Target, Axis](FOKSceneObject& X, float V) { if (FOKSceneIkTarget* Y = Target(X)) Y->Euler[Axis] = V; }, -180.f, 180.f, 1.f);
				Props->AddSlot().AutoHeight().Padding(0.f, 1.f)[Button(FText::FromString(TEXT("取目前方向")), [this, Target]
				{
					FOKSceneObject* X = Obj();
					FOKSceneIkTarget* T = X ? Target(*X) : nullptr;
					if (T && Game.IsValid() && Game->SceneActor && OKStudioGameInternal::IkEffectorEuler(Game->SceneActor->PreviewOf(X->Id), IkEffector, T->Euler)) { Changed(); DeferRebuild(); }
				}, 0.f, 18.f, false, 10.f)];
			}
		}
		Float(TEXT("位置權重"),
			[this](FOKSceneObject& X) { const FOKSceneIkTarget* T = X.Character.Ik.FindByPredicate([this](const FOKSceneIkTarget& Y) { return Y.Effector == IkEffector; }); return T ? T->Weight : 1.f; },
			[this](FOKSceneObject& X, float V) { if (FOKSceneIkTarget* T = X.Character.Ik.FindByPredicate([this](const FOKSceneIkTarget& Y) { return Y.Effector == IkEffector; })) T->Weight = FMath::Clamp(V, 0.f, 1.f); }, 0.f, 1.f, 0.01f);
		if (const FOKSceneIkTarget* T = O.Character.Ik.FindByPredicate([this](const FOKSceneIkTarget& Y) { return Y.Effector == IkEffector; }); T && T->bUseRotation)
			Float(TEXT("旋轉權重"),
				[this](FOKSceneObject& X) { const FOKSceneIkTarget* Y = X.Character.Ik.FindByPredicate([this](const FOKSceneIkTarget& Z) { return Z.Effector == IkEffector; }); return Y ? Y->RotationWeight : 1.f; },
				[this](FOKSceneObject& X, float V) { if (FOKSceneIkTarget* Y = X.Character.Ik.FindByPredicate([this](const FOKSceneIkTarget& Z) { return Z.Effector == IkEffector; })) Y->RotationWeight = FMath::Clamp(V, 0.f, 1.f); }, 0.f, 1.f, 0.01f);
	}
	// HS2Studio6 IK 權重: the whole IK's weight, stepping through the targets that are on, and the reset.
	Float(TEXT("整體權重"), [](FOKSceneObject& X) { return X.Character.IkWeight; }, [](FOKSceneObject& X, float V) { X.Character.IkWeight = FMath::Clamp(V, 0.f, 1.f); }, 0.f, 1.f, 0.01f);
	auto Step = [this](int32 Dir)
	{
		const FOKSceneObject* X = Obj();
		if (!X || X->Character.Ik.IsEmpty()) return;
		TArray<int32> On;
		for (const FOKSceneIkTarget& T : X->Character.Ik) On.AddUnique(T.Effector);
		On.Sort();
		const int32 At = On.IndexOfByKey(IkEffector);
		IkEffector = On[At == INDEX_NONE ? (Dir > 0 ? 0 : On.Num() - 1) : (At + Dir + On.Num()) % On.Num()];
		if (Game.IsValid() && Game->GuideIk >= 0) Game->GuideIk = IkEffector;
		DeferRebuild();
	};
	TSharedRef<SHorizontalBox> Weights = SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.f).Padding(0.f, 0.f, 1.f, 0.f)[Button(FText::FromString(TEXT("上一個目標")), [Step] { Step(-1); }, 0.f, 18.f, false, 10.f)]
		+ SHorizontalBox::Slot().FillWidth(1.f).Padding(0.f, 0.f, 1.f, 0.f)[Button(FText::FromString(TEXT("下一個目標")), [Step] { Step(1); }, 0.f, 18.f, false, 10.f)]
		+ SHorizontalBox::Slot().FillWidth(1.f)[Button(FText::FromString(TEXT("權重復位")), [this]
		{
			// The whole weight and the chosen target's weights back to 1 (HS2Studio6 复位整体与当前目标权重).
			FOKSceneObject* X = Obj();
			if (!X) return;
			X->Character.IkWeight = 1.f;
			if (FOKSceneIkTarget* T = X->Character.Ik.FindByPredicate([this](const FOKSceneIkTarget& Y) { return Y.Effector == IkEffector; })) T->Weight = T->RotationWeight = 1.f;
			Changed();
		}, 0.f, 18.f, false, 10.f)];
	Props->AddSlot().AutoHeight().Padding(0.f, 1.f)[Weights];
	TSharedRef<SHorizontalBox> Row1 = SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.f).Padding(0.f, 0.f, 1.f, 0.f)[Button(FText::FromString(TEXT("IK←FK")), Cmd(TEXT("pose.ikfromfk")), 0.f, 18.f, false, 10.f)]
		+ SHorizontalBox::Slot().FillWidth(1.f).Padding(0.f, 0.f, 1.f, 0.f)[Button(FText::FromString(TEXT("FK←IK")), Cmd(TEXT("pose.fkfromik")), 0.f, 18.f, false, 10.f)]
		+ SHorizontalBox::Slot().FillWidth(1.f).Padding(0.f, 0.f, 1.f, 0.f)[Button(FText::FromString(TEXT("鏡像")), Cmd(TEXT("pose.mirror"), TEXT("all")), 0.f, 18.f, false, 10.f)]
		+ SHorizontalBox::Slot().FillWidth(1.f)[Button(FText::FromString(TEXT("清除 IK")), [this] { if (FOKSceneObject* X = Obj()) { X->Character.Ik.Reset(); if (Game.IsValid()) Game->GuideIk = -1; Changed(); DeferRebuild(); } }, 0.f, 18.f, false, 10.f)];
	Props->AddSlot().AutoHeight().Padding(0.f, 3.f)[Row1];
}

void SOKStudioPanel::LookPage(FOKSceneObject& O, bool bNeck)
{
	Label(bNeck ? TEXT("頸部") : TEXT("視線"));
	const bool bFixed = bNeck ? O.Character.FixedNeck.Num() == 2 : O.Character.FixedEyes.Num() == 12;
	const TArray<EOKSceneLook> Modes = LookModes(bFixed);
	TArray<FString> Options;
	for (EOKSceneLook M : Modes) Options.Add(LookModeName(M));
	Choice(bNeck ? TEXT("脖子") : TEXT("眼睛"), Options, [bNeck, Modes](FOKSceneObject& X) { return Modes.IndexOfByKey(bNeck ? X.Character.NeckLook : X.Character.EyesLook); },
		[this, bNeck, Modes](FOKSceneObject& X, int32 V) { if (Modes.IsValidIndex(V)) (bNeck ? X.Character.NeckLook : X.Character.EyesLook) = Modes[V]; DeferRebuild(); });
	Label(TEXT("注視點（cm，角色空間；選「注視點」時可在畫面上拖動）"));
	for (int32 Axis = 0; Axis < 3; ++Axis)
		Float(FString(TEXT("XYZ")).Mid(Axis, 1), [Axis](FOKSceneObject& X) { return float(X.Character.LookPoint[Axis]); }, [Axis](FOKSceneObject& X, float V) { X.Character.LookPoint[Axis] = V; }, -1000.f, 1000.f, 1.f);
	if (bNeck) Float(TEXT("跟隨強度"), [](FOKSceneObject& X) { return X.Character.NeckFollow; }, [](FOKSceneObject& X, float V) { X.Character.NeckFollow = FMath::Clamp(V, 0.f, 1.f); }, 0.f, 1.f, 0.05f);
	// HS2Studio6: take the view's position as the look point (and look there), or go back to following the view.
	auto TakeView = [this, bNeck]
	{
		FOKSceneObject* X = Obj();
		APlayerController* PC = ViewController(Game.Get());
		const AKKPreviewActor* P = X && Game.IsValid() && Game->SceneActor ? Game->SceneActor->PreviewOf(X->Id) : nullptr;
		if (!X || !PC || !PC->PlayerCameraManager || !P) return;
		X->Character.LookPoint = P->GetActorTransform().InverseTransformPosition(PC->PlayerCameraManager->GetCameraLocation());
		(bNeck ? X->Character.NeckLook : X->Character.EyesLook) = EOKSceneLook::Point;
		Changed(); DeferRebuild();
	};
	auto BackToView = [this, bNeck] { if (FOKSceneObject* X = Obj()) { (bNeck ? X->Character.NeckLook : X->Character.EyesLook) = EOKSceneLook::Camera; Changed(); DeferRebuild(); } };
	Props->AddSlot().AutoHeight().Padding(0.f, 3.f)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.f).Padding(0.f, 0.f, 1.f, 0.f)[Button(FText::FromString(TEXT("取鏡頭位置")), TakeView, 0.f, 18.f, false, 10.f)]
		+ SHorizontalBox::Slot().FillWidth(1.f)[Button(FText::FromString(TEXT("恢復看鏡頭")), BackToView, 0.f, 18.f, false, 10.f)]
	];
}

void SOKStudioPanel::ExpressionPage(FOKSceneObject& O)
{
	Label(TEXT("表情"));
	auto Show = [](const TCHAR* Kind) { return [Kind](int32 N) { return N < 0 ? FString(TEXT("照卡片")) : FString::Printf(TEXT("%s %d"), Kind, N); }; };
	Named(TEXT("眉"), TEXT("eyebrow"), 40, Show(TEXT("眉型")), [](FOKSceneObject& X) { return X.Character.EyebrowPattern; }, [](FOKSceneObject& X, int32 V) { X.Character.EyebrowPattern = V; });
	// Eyes patterns carry the game's names (character list cha_eyeset); eyebrows and mouths have none in the game data.
	const TMap<int32, FString>& EyeNames = FOKStudioCatalog::Get().EyeNames;
	int32 EyeCount = 40;
	for (const TPair<int32, FString>& E : EyeNames) EyeCount = FMath::Max(EyeCount, E.Key + 1);
	Named(TEXT("眼"), TEXT("eyes"), EyeCount, [](int32 N)
	{
		if (N < 0) return FString(TEXT("照卡片"));
		const FString* Name = FOKStudioCatalog::Get().EyeNames.Find(N);
		return Name && !Name->IsEmpty() ? FString::Printf(TEXT("%d %s"), N, **Name) : FString::Printf(TEXT("眼型 %d"), N);
	}, [](FOKSceneObject& X) { return X.Character.EyesPattern; }, [](FOKSceneObject& X, int32 V) { X.Character.EyesPattern = V; });
	Named(TEXT("嘴"), TEXT("mouth"), 40, Show(TEXT("嘴型")), [](FOKSceneObject& X) { return X.Character.MouthPattern; }, [](FOKSceneObject& X, int32 V) { X.Character.MouthPattern = V; });
	Float(TEXT("張嘴（-1 照動畫）"), [](FOKSceneObject& X) { return X.Character.MouthOpen; }, [](FOKSceneObject& X, float V) { X.Character.MouthOpen = V; }, -1.f, 1.f, 0.05f);
	Float(TEXT("睜眼（-1 照角色）"), [](FOKSceneObject& X) { return X.Character.EyesOpen; }, [](FOKSceneObject& X, float V) { X.Character.EyesOpen = V < 0.f ? -1.f : FMath::Min(V, 1.f); }, -1.f, 1.f, 0.05f);
	Choice(TEXT("眨眼"), { TEXT("照卡片"), TEXT("不眨"), TEXT("眨") }, [](FOKSceneObject& X) { return X.Character.Blink + 1; }, [](FOKSceneObject& X, int32 V) { X.Character.Blink = V - 1; });
	Label(TEXT("數值輸入（進階）"));
	Float(TEXT("眉 #"), [](FOKSceneObject& X) { return float(X.Character.EyebrowPattern); }, [](FOKSceneObject& X, float V) { X.Character.EyebrowPattern = FMath::RoundToInt(V); }, -1.f, 200.f, 1.f);
	Float(TEXT("眼 #"), [](FOKSceneObject& X) { return float(X.Character.EyesPattern); }, [](FOKSceneObject& X, float V) { X.Character.EyesPattern = FMath::RoundToInt(V); }, -1.f, 200.f, 1.f);
	Float(TEXT("嘴 #"), [](FOKSceneObject& X) { return float(X.Character.MouthPattern); }, [](FOKSceneObject& X, float V) { X.Character.MouthPattern = FMath::RoundToInt(V); }, -1.f, 200.f, 1.f);
}

void SOKStudioPanel::HandPage(FOKSceneObject& O)
{
	Label(TEXT("手型"));
	// Studio's hand patterns (HandAnime): the clip names, with a Chinese name for the common ones.
	auto Show = [](int32 Side)
	{
		return [Side](int32 N)
		{
			if (N < 0) return FString(TEXT("照動畫"));
			static const TMap<FString, FString> Zh = { { TEXT("goo"), TEXT("握拳") }, { TEXT("scissors"), TEXT("剪刀") }, { TEXT("par"), TEXT("張開") }, { TEXT("grip_hard"), TEXT("用力握") },
				{ TEXT("grip_normal"), TEXT("握") }, { TEXT("grip_soft"), TEXT("輕握") }, { TEXT("par_soft"), TEXT("輕張") }, { TEXT("par_grip"), TEXT("半握") }, { TEXT("gun_grip"), TEXT("握槍") },
				{ TEXT("soft_grip"), TEXT("柔握") }, { TEXT("natural_pose"), TEXT("自然") }, { TEXT("hug"), TEXT("擁抱") }, { TEXT("index_finger1"), TEXT("伸食指 1") },
				{ TEXT("index_finger2"), TEXT("伸食指 2") }, { TEXT("thumb_finger"), TEXT("豎拇指") }, { TEXT("ok"), TEXT("OK") }, { TEXT("soft_hold"), TEXT("輕持") },
				{ TEXT("hard_hold"), TEXT("緊持") }, { TEXT("par_straight"), TEXT("伸直張開") } };
			const FString* Clip = FOKStudioCatalog::Get().HandNames[Side].Find(N);
			if (!Clip || Clip->IsEmpty()) return FString::Printf(TEXT("手型 %d"), N);
			const FString* Name = Zh.Find(*Clip);
			return Name ? FString::Printf(TEXT("%d %s（%s）"), N, **Name, **Clip) : FString::Printf(TEXT("%d %s"), N, **Clip);
		};
	};
	auto Count = [](int32 Side) { int32 C = 40; for (const TPair<int32, FString>& H : FOKStudioCatalog::Get().HandNames[Side]) C = FMath::Max(C, H.Key + 1); return C; };
	Named(TEXT("左手"), TEXT("hand_l"), Count(0), Show(0), [](FOKSceneObject& X) { return X.Character.HandLeft; }, [](FOKSceneObject& X, int32 V) { X.Character.HandLeft = V; });
	Named(TEXT("右手"), TEXT("hand_r"), Count(1), Show(1), [](FOKSceneObject& X) { return X.Character.HandRight; }, [](FOKSceneObject& X, int32 V) { X.Character.HandRight = V; });
	Props->AddSlot().AutoHeight().Padding(0.f, 3.f)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.f).Padding(0.f, 0.f, 1.f, 0.f)[Button(FText::FromString(TEXT("左→右")), [this] { if (FOKSceneObject* X = Obj()) { X->Character.HandRight = X->Character.HandLeft; Changed(); } }, 0.f, 18.f, false, 10.f)]
		+ SHorizontalBox::Slot().FillWidth(1.f).Padding(0.f, 0.f, 1.f, 0.f)[Button(FText::FromString(TEXT("右→左")), [this] { if (FOKSceneObject* X = Obj()) { X->Character.HandLeft = X->Character.HandRight; Changed(); } }, 0.f, 18.f, false, 10.f)]
		+ SHorizontalBox::Slot().FillWidth(1.f)[Button(FText::FromString(TEXT("復位")), [this] { if (FOKSceneObject* X = Obj()) { X->Character.HandLeft = X->Character.HandRight = -1; Changed(); } }, 0.f, 18.f, false, 10.f)]
	];
}

// --- Pose library ---

void SOKStudioPanel::PoseFilesPage()
{
	Label(TEXT("姿勢檔（KK Studio 的 UserData/Studio/pose）"));
	Props->AddSlot().AutoHeight().Padding(0.f, 2.f)[Button(FText::FromString(Tip(TEXT("開姿勢庫…"), TEXT("app.poselibrary"))), Cmd(TEXT("app.poselibrary")), 0.f, 22.f)];
	// HS2Studio6's pose page (n / total, previous / next, back to the T-pose). KKS has no built-in pose presets: these
	// step through the Studio pose files.
	auto Step = [this](int32 By)
	{
		const TArray<FString> Files = FOKStudioPose::Files();
		if (Files.IsEmpty()) { SetStatus(TEXT("沒有姿勢檔")); return; }
		PoseStep = ((PoseStep < 0 ? (By > 0 ? -1 : 0) : PoseStep) + By + Files.Num()) % Files.Num();
		LoadPose(Files[PoseStep]);
		DeferRebuild();
	};
	auto TPose = [this]
	{
		if (FOKSceneObject* X = Obj()) { X->Character.Fk.Reset(); X->Character.Ik.Reset(); X->Character.StudioAnimeNo = -1; PoseStep = -1; Changed(); DeferRebuild(); }
	};
	const int32 Total = FOKStudioPose::Files().Num();
	Props->AddSlot().AutoHeight().Padding(0.f, 3.f)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.f).Padding(0.f, 0.f, 1.f, 0.f)[Button(FText::FromString(TEXT("◀")), [Step] { Step(-1); }, 0.f, 22.f, false, 11.f)]
		+ SHorizontalBox::Slot().FillWidth(1.f).HAlign(HAlign_Center).VAlign(VAlign_Center)
			[OKStudioStyle::Label(FText::FromString(PoseStep < 0 ? FString::Printf(TEXT("— / %d"), Total) : FString::Printf(TEXT("%d / %d"), PoseStep + 1, Total)), 13.f)]
		+ SHorizontalBox::Slot().FillWidth(1.f).Padding(1.f, 0.f, 1.f, 0.f)[Button(FText::FromString(TEXT("▶")), [Step] { Step(1); }, 0.f, 22.f, false, 11.f)]
		+ SHorizontalBox::Slot().FillWidth(1.5f)[Button(FText::FromString(TEXT("復位 T-pose")), TPose, 0.f, 22.f, false, 10.f)]
	];
	PoseFiles();
}

void SOKStudioPanel::PreviewPose(const FString& File)
{
	FOKSceneObject* X = Obj();
	if (!X || X->Kind != EOKSceneObjectKind::Character) { SetStatus(TEXT("請選一個角色")); return; }
	// The first preview remembers the pose to go back to.
	if (PosePreviewId != X->Id) { PoseBeforeFk = X->Character.Fk; PoseBeforeIk = X->Character.Ik; PosePreviewId = X->Id; }
	PosePreviewFile = File;
	LoadPose(File);
	DeferFloat(TEXT("poselib"));
}

void SOKStudioPanel::CommitPosePreview()
{
	PosePreviewId = -1;
	PosePreviewFile.Reset();
	DeferFloat(TEXT("poselib"));
}

void SOKStudioPanel::CancelPosePreview()
{
	if (PosePreviewId < 0 || !Game.IsValid()) return;
	if (FOKSceneObject* X = Game->Document->Find(PosePreviewId)) { X->Character.Fk = PoseBeforeFk; X->Character.Ik = PoseBeforeIk; Changed(); }
	PosePreviewId = -1;
	PosePreviewFile.Reset();
	DeferRebuild();
	if (IsFloatOpen(TEXT("poselib"))) DeferFloat(TEXT("poselib"));
}

bool SOKStudioPanel::SavePoseWithPicture(const FString& Name)
{
	const FString Clean = FPaths::MakeValidFileName(Name.TrimStartAndEnd().IsEmpty() ? FDateTime::Now().ToString(TEXT("%Y_%m%d_%H%M_%S")) : Name.TrimStartAndEnd());
	FString Path = FOKStudioPose::Directory() / Clean + TEXT(".dat");
	for (int32 N = 1; FPaths::FileExists(Path); ++N) Path = FOKStudioPose::Directory() / FString::Printf(TEXT("%s_%d.dat"), *Clean, N);
	if (!SavePose(Path, Clean)) return false;
	// A picture of the current view beside it (256 px).
	FString Error;
	if (Game.IsValid()) Game->CaptureToPng(FPaths::ChangeExtension(Path, TEXT("png")), 256, 256, Error);
	FOKStudioThumbnails::Get().Forget(FPaths::ChangeExtension(Path, TEXT("png")));
	DeferFloat(TEXT("poselib"));
	return true;
}

TSharedRef<SWidget> SOKStudioPanel::BuildPoseLibrary()
{
	TArray<FString> Files = FOKStudioPose::Files();
	if (!PoseSearch.IsEmpty()) Files.RemoveAll([this](const FString& F) { return !FPaths::GetBaseFilename(F).Contains(PoseSearch); });
	TSharedRef<SWrapBox> Grid = SNew(SWrapBox).UseAllottedSize(true);
	for (const FString& F : Files)
	{
		const FString Picture = FPaths::ChangeExtension(F, TEXT("png"));
		Grid->AddSlot().Padding(2.f)
		[
			SNew(SBox).WidthOverride(76.f).HeightOverride(104.f)
			[
				Fill(TAttribute<bool>::CreateLambda([this, F] { return PosePreviewFile == F; }), ListArea(),
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().FillHeight(1.f)
					[
						SNew(SButton).ButtonStyle(&FlatButton()).ContentPadding(FMargin(1.f)).ToolTipText(FText::FromString(TEXT("點：試套（可取消）")))
						.OnClicked_Lambda([this, F] { PreviewPose(F); return FReply::Handled(); })
						[SNew(SImage).Image_Lambda([Picture] { const FSlateBrush* B = FPaths::FileExists(Picture) ? FOKStudioThumbnails::Get().File(Picture, 96) : nullptr; return B ? B : Brush(Input()); })]
					]
					+ SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Font(Font(8.f)).ColorAndOpacity(OKStudioStyle::Text()).OverflowPolicy(ETextOverflowPolicy::Ellipsis).AddMetaData<FTagMetaData>(TEXT("Ellipsis")).Text(FText::FromString(FPaths::GetBaseFilename(F)))]
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().FillWidth(1.f)[Button(FText::FromString(TEXT("套用")), [this, F] { CommitPosePreview(); LoadPose(F); FOKStudioSettings::Get().UseRecent(TEXT("pose:") + F); }, 0.f, 16.f, false, 9.f)]
						+ SHorizontalBox::Slot().AutoWidth()[IconButton(TEXT("delete"), TEXT("刪除（移到 Trash）"), [this, F, Picture]
						{
							Confirm(FString::Printf(TEXT("把姿勢 %s 移到 Saved/OKStudio/Trash？"), *FPaths::GetBaseFilename(F)), { { TEXT("移到 Trash"), [this, F, Picture]
							{
								const FString Trash = FOKStudioSettings::Directory() / TEXT("Trash") / FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S_"));
								IFileManager::Get().Move(*(Trash + FPaths::GetCleanFilename(F)), *F, false, true);
								if (FPaths::FileExists(Picture)) IFileManager::Get().Move(*(Trash + FPaths::GetCleanFilename(Picture)), *Picture, false, true);
								DeferFloat(TEXT("poselib"));
							} }, { TEXT("取消"), nullptr } });
						}, 18.f, 16.f)]
					])
			]
		];
	}
	TSharedRef<SWidget> Preview = PosePreviewId >= 0
		? StaticCastSharedRef<SWidget>(SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)[OKStudioStyle::Label(FText::FromString(TEXT("試套中：") + FPaths::GetBaseFilename(PosePreviewFile)), 10.f, TextSoft())]
			+ SHorizontalBox::Slot().AutoWidth()[Button(FText::FromString(TEXT("確定")), [this] { CommitPosePreview(); }, 50.f, 20.f)]
			+ SHorizontalBox::Slot().AutoWidth().Padding(2.f, 0.f, 0.f, 0.f)[Button(FText::FromString(TEXT("取消")), [this] { CancelPosePreview(); }, 50.f, 20.f)])
		: StaticCastSharedRef<SWidget>(SNullWidget::NullWidget);
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f)[SNew(SBox).HeightOverride(20.f)[SNew(SEditableTextBox).Style(&TextBoxStyle()).HintText(FText::FromString(TEXT("搜尋姿勢"))).Text(FText::FromString(PoseSearch))
				.OnTextCommitted_Lambda([this](const FText& T, ETextCommit::Type) { PoseSearch = T.ToString(); DeferFloat(TEXT("poselib")); })]]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f)[Preview]
		+ SVerticalBox::Slot().FillHeight(1.f)[SNew(SBorder).BorderImage(Brush(ListArea())).Padding(2.f)[SNew(SScrollBox) + SScrollBox::Slot()[Grid]]]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 3.f, 0.f, 0.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f)[SNew(SBox).HeightOverride(20.f)[SNew(SEditableTextBox).Style(&TextBoxStyle()).HintText(FText::FromString(TEXT("新姿勢名稱"))).Text(FText::FromString(PoseName))
				.OnTextChanged_Lambda([this](const FText& T) { PoseName = T.ToString(); })]]
			+ SHorizontalBox::Slot().AutoWidth().Padding(3.f, 0.f, 0.f, 0.f)[Button(FText::FromString(TEXT("存目前姿勢")), [this] { SavePoseWithPicture(PoseName); }, 80.f, 20.f)]
		]
		+ SVerticalBox::Slot().AutoHeight()[OKStudioStyle::Label(FText::FromString(FString::Printf(TEXT("%d 個姿勢（%s）"), Files.Num(), *FOKStudioPose::Directory())), 9.f, TextHint())];
}
