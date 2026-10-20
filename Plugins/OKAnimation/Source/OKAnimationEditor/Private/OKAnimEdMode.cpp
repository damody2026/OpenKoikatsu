#include "OKAnimEdMode.h"
#include "OKAnimationEditorToolkit.h"
#include "EditorModeManager.h"
#include "EditorViewportClient.h"
#include "SceneManagement.h"
#include "UnrealWidget.h"

IMPLEMENT_HIT_PROXY(HOKBoneProxy, HHitProxy);

const FEditorModeID FOKAnimEdMode::ModeId(TEXT("OKAnimationAnimateMode"));

void FOKAnimEdMode::Render(const FSceneView* View, FViewport* Viewport, FPrimitiveDrawInterface* PDI)
{
	FEdMode::Render(View, Viewport, PDI);
	if (!Editor || !Editor->HasPreviewPose()) return;
	const FOKUnrealSkeleton& B = Editor->GetBinding();
	const TArray<FTransform>& Comp = Editor->GetComponentPose();
	const FReferenceSkeleton& Ref = B.Ref;
	// Every bone as a thin line; the animated (mapped) ones as clickable joints joined to their animated parent.
	for (int32 I = 0; I < Ref.GetNum() && I < Comp.Num(); ++I)
	{
		const int32 P = Ref.GetParentIndex(I);
		if (P != INDEX_NONE) PDI->DrawLine(Comp[P].GetLocation(), Comp[I].GetLocation(), FLinearColor(0.25f, 0.25f, 0.25f), SDPG_Foreground, 0.f);
	}
	// Ghosts: the skeleton GhostStep frames before (blue) and after (green).
	if (Editor->bShowGhosts)
	{
		const float Fps = Editor->FrameRate();
		for (const int32 Dir : { -1, 1 })
		{
			const int32 F = Editor->CurrentFrame() + Dir * Editor->GhostStep;
			TArray<FVector> Pose;
			if (F < 0 || F >= Editor->NumFrames() || !Editor->PosePositionsAt(F / Fps, Pose)) continue;
			const FLinearColor Color = Dir < 0 ? FLinearColor(0.2f, 0.45f, 1.f, 0.6f) : FLinearColor(0.2f, 1.f, 0.4f, 0.6f);
			for (int32 H = 0; H < Pose.Num(); ++H)
				if (B.MappedParent[H] != INDEX_NONE) PDI->DrawLine(Pose[B.MappedParent[H]], Pose[H], Color, SDPG_Foreground, 0.5f);
		}
	}
	// Motion trail of the selected bone over the whole animation, frame ticks on it, the current frame larger.
	if (Editor->bShowTrail && Editor->GetSelection().Num())
	{
		const TArray<FVector>& Trail = Editor->GetTrail(Editor->GetSelection().Last());
		for (int32 I = 1; I < Trail.Num(); ++I) PDI->DrawLine(Trail[I - 1], Trail[I], FLinearColor(1.f, 0.5f, 0.1f), SDPG_Foreground, 0.6f);
		for (int32 I = 0; I < Trail.Num(); ++I) PDI->DrawPoint(Trail[I], FLinearColor(1.f, 0.6f, 0.2f), I == Editor->CurrentFrame() ? 8.f : 3.f, SDPG_Foreground);
	}
	// Contacts active now: the effector joined to its target (a bone, or the ground under it).
	if (const UOKAnimSequence* Seq = Editor->Sequence())
	{
		const float T = Editor->CurrentFrame() / FMath::Max(Editor->FrameRate(), 1.f);
		for (int32 I = 0; I < Seq->Contacts.Num(); ++I)
		{
			const FOKContact& C = Seq->Contacts[I];
			const int32 E = B.FamilyName.IndexOfByKey(C.Effector);
			if (E == INDEX_NONE || T < C.Start || T > C.End) continue;
			const FVector From = Comp[B.BoneIndex[E]].GetLocation();
			const int32 Tg = C.TargetRole == -1 ? INDEX_NONE : B.FamilyName.IndexOfByKey(C.TargetBone);
			const FVector To = C.TargetRole == -1 ? FVector(From.X, From.Y, 0.) : Tg != INDEX_NONE ? Comp[B.BoneIndex[Tg]].GetLocation() : From;
			const FLinearColor Color = I == Editor->SelectedContact ? FLinearColor(1.f, 0.6f, 0.1f) : C.TargetRole == -1 ? FLinearColor(0.3f, 1.f, 0.3f) : FLinearColor(0.3f, 0.6f, 1.f);
			PDI->DrawLine(From, To, Color, SDPG_Foreground, 1.2f);
			PDI->DrawPoint(To, Color, 10.f, SDPG_Foreground);
		}
	}
	// A group's contacts between roles (and within them), active now.
	if (const UOKGroupAnimSequence* Group = Cast<UOKGroupAnimSequence>(Editor->GetEditedAsset()))
	{
		const float T = Editor->CurrentFrame() / FMath::Max(Editor->FrameRate(), 1.f);
		TMap<int32, TArray<FVector>> Roles;
		auto RolePos = [&](int32 R) -> const TArray<FVector>* { if (!Roles.Contains(R)) Editor->RoleBonePositions(R, Roles.Add(R)); const TArray<FVector>& P = Roles[R]; return P.Num() ? &P : nullptr; };
		for (const FOKContact& C : Group->Contacts)
		{
			if (T < C.Start || T > C.End) continue;
			const int32 E = B.FamilyName.IndexOfByKey(C.Effector);
			const TArray<FVector>* From = E != INDEX_NONE ? RolePos(C.Role) : nullptr;
			if (!From) continue;
			const FVector A = (*From)[E];
			FVector To = FVector(A.X, A.Y, 0.);
			if (C.TargetRole >= 0)
			{
				const int32 Tg = B.FamilyName.IndexOfByKey(C.TargetBone);
				const TArray<FVector>* Target = Tg != INDEX_NONE ? RolePos(C.TargetRole) : nullptr;
				if (!Target) continue;
				To = (*Target)[Tg];
			}
			const FLinearColor Color = C.TargetRole == -1 ? FLinearColor(0.3f, 1.f, 0.3f) : C.TargetRole == C.Role ? FLinearColor(0.3f, 0.6f, 1.f) : FLinearColor(1.f, 0.35f, 0.8f);
			PDI->DrawLine(A, To, Color, SDPG_Foreground, 1.f);
			PDI->DrawPoint(To, Color, 8.f, SDPG_Foreground);
		}
	}
	for (int32 H = 0; H < B.BoneIndex.Num(); ++H)
	{
		const bool bSelected = Editor->GetSelection().Contains(H);
		const bool bKeyed = Editor->HasKeyAtCurrentFrame(H);
		const FLinearColor Color = bSelected ? FLinearColor(1.f, 0.85f, 0.1f) : bKeyed ? FLinearColor(1.f, 0.25f, 0.25f) : FLinearColor(0.35f, 0.75f, 1.f);
		const FVector Pos = Comp[B.BoneIndex[H]].GetLocation();
		if (B.MappedParent[H] != INDEX_NONE)
			PDI->DrawLine(Comp[B.BoneIndex[B.MappedParent[H]]].GetLocation(), Pos, bSelected ? Color : FLinearColor(0.2f, 0.5f, 0.8f), SDPG_Foreground, bSelected ? 1.5f : 0.7f);
		PDI->SetHitProxy(new HOKBoneProxy(H));
		PDI->DrawPoint(Pos, Color, bSelected ? 12.f : 9.f, SDPG_Foreground);
		PDI->SetHitProxy(nullptr);
	}
}

bool FOKAnimEdMode::HandleClick(FEditorViewportClient* Client, HHitProxy* HitProxy, const FViewportClick& Click)
{
	if (!Editor || Click.GetKey() != EKeys::LeftMouseButton) return false;
	if (HitProxy && HitProxy->IsA(HOKBoneProxy::StaticGetType()))
	{
		Editor->SelectBone(static_cast<HOKBoneProxy*>(HitProxy)->Bone, Click.IsControlDown() || Click.IsShiftDown());
		return true;
	}
	if (!HitProxy || !HitProxy->IsA(HWidgetAxis::StaticGetType())) Editor->ClearSelection();
	return false;
}

bool FOKAnimEdMode::StartTracking(FEditorViewportClient* Client, FViewport* Viewport)
{
	if (!Editor || Editor->GetSelection().IsEmpty() || Client->GetCurrentWidgetAxis() == EAxisList::None) return false;
	bTracking = true;
	Editor->BeginEdit(NSLOCTEXT("OKAnimation", "GizmoEdit", "Pose bones"));
	return true;
}

bool FOKAnimEdMode::EndTracking(FEditorViewportClient* Client, FViewport* Viewport)
{
	if (!bTracking) return false;
	bTracking = false;
	if (Editor) Editor->EndEdit();
	return true;
}

bool FOKAnimEdMode::InputDelta(FEditorViewportClient* Client, FViewport* Viewport, FVector& Drag, FRotator& Rot, FVector& Scale)
{
	if (!Editor || !bTracking || Editor->GetSelection().IsEmpty()) return false;
	if (!Rot.IsZero()) Editor->RotateSelection(Rot.Quaternion());
	if (!Drag.IsZero()) Editor->TranslateSelection(Drag);
	return true;
}

bool FOKAnimEdMode::InputKey(FEditorViewportClient* Client, FViewport* Viewport, FKey Key, EInputEvent Event)
{
	if (!Editor || (Event != IE_Pressed && Event != IE_Repeat)) return false;
	const bool bAlt = Viewport->KeyState(EKeys::LeftAlt) || Viewport->KeyState(EKeys::RightAlt);
	const bool bShift = Viewport->KeyState(EKeys::LeftShift) || Viewport->KeyState(EKeys::RightShift);
	const bool bCtrl = Viewport->KeyState(EKeys::LeftControl) || Viewport->KeyState(EKeys::RightControl);
	if (Key == EKeys::S && !bCtrl && !bAlt) { Editor->KeySelection(true, true); return true; }
	if (Key == EKeys::E && bShift) { Editor->KeySelection(true, false); return true; }
	if (Key == EKeys::W && bShift) { Editor->KeySelection(false, true); return true; }
	if (Key == EKeys::Comma) { bAlt ? Editor->StepFrame(-1) : Editor->JumpKey(-1); return true; }
	if (Key == EKeys::Period) { bAlt ? Editor->StepFrame(1) : Editor->JumpKey(1); return true; }
	if (Key == EKeys::V && bAlt) { Editor->TogglePlay(); return true; }
	if (Key == EKeys::Home) { Editor->JumpEnd(false); return true; }
	if (Key == EKeys::End) { Editor->JumpEnd(true); return true; }
	if (Key == EKeys::Up && !bCtrl) { Editor->PickWalk(true); return true; }
	if (Key == EKeys::Down && !bCtrl) { Editor->PickWalk(false); return true; }
	if (Key == EKeys::Delete) { Editor->DeleteKeysAtCurrent(); return true; }
	if (Key == EKeys::C && bCtrl) { Editor->CopyPose(); return true; }
	if (Key == EKeys::V && bCtrl) { Editor->PastePose(); return true; }
	return false;
}

FVector FOKAnimEdMode::GetWidgetLocation() const
{
	return Editor && !Editor->GetSelection().IsEmpty() ? Editor->BoneTransform(Editor->GetSelection().Last()).GetLocation() : FVector::ZeroVector;
}

bool FOKAnimEdMode::ShouldDrawWidget() const { return Editor && !Editor->GetSelection().IsEmpty() && Editor->CanAnimate(); }

bool FOKAnimEdMode::UsesTransformWidget(UE::Widget::EWidgetMode Mode) const
{
	return Mode == UE::Widget::EWidgetMode::WM_Rotate || Mode == UE::Widget::EWidgetMode::WM_Translate;
}

bool FOKAnimEdMode::GetCustomDrawingCoordinateSystem(FMatrix& Matrix, void* Data)
{
	if (!Editor || Editor->GetSelection().IsEmpty() || GetModeManager()->GetCoordSystem() != COORD_Local) return false;
	Matrix = FRotationMatrix::Make(Editor->BoneTransform(Editor->GetSelection().Last()).GetRotation());
	return true;
}

bool FOKAnimEdMode::GetCustomInputCoordinateSystem(FMatrix& Matrix, void* Data) { return GetCustomDrawingCoordinateSystem(Matrix, Data); }
