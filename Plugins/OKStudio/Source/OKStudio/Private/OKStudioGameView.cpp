// OK Studio in-game: picking in the view, framing the selection, view presets and bookmarks, camera objects from the
// view, auto backups and the open report.
#include "OKStudioGame.h"
#include "OKStudioGameInternal.h"
#include "OKStudioSettings.h"
#include "OKSceneActor.h"
#include "KKPreviewActor.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Camera/CameraComponent.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
	const FVector InitialFocus(0.f, 0.f, 90.f);
	constexpr float InitialYaw = -90.f, InitialPitch = -8.f, InitialDistance = 380.f;

	/** The bones of a shown character with their parents (world positions), for picking and framing. */
	bool SkeletonSegments(const AKKPreviewActor* Actor, TArray<TPair<FVector, FVector>>& Out)
	{
		UPoseableMeshComponent* Mesh = Actor ? (Actor->BodySkel ? Actor->BodySkel.Get() : (Actor->SkelFollowers.Num() ? Actor->SkelFollowers[0].Get() : nullptr)) : nullptr;
		if (!Mesh) return false;
		const int32 Bones = Mesh->GetNumBones();
		for (int32 B = 0; B < Bones; ++B)
		{
			const FName Name = Mesh->GetBoneName(B);
			const FName Parent = Mesh->GetParentBone(Name);
			if (Parent.IsNone()) continue;
			const FVector A = Mesh->GetBoneTransformByName(Name, EBoneSpaces::WorldSpace).GetLocation();
			const FVector P = Mesh->GetBoneTransformByName(Parent, EBoneSpaces::WorldSpace).GetLocation();
			// Long jumps (the root to the hips) and zero-length bones say nothing about where the body is.
			const double L = FVector::Distance(A, P);
			if (L > 0.5 && L < 60.) Out.Add({ P, A });
		}
		return Out.Num() > 0;
	}

	double PointSegment(const FVector2D& P, const FVector2D& A, const FVector2D& B)
	{
		const FVector2D AB = B - A;
		const double T = AB.SizeSquared() > 1e-6 ? FMath::Clamp(FVector2D::DotProduct(P - A, AB) / AB.SizeSquared(), 0., 1.) : 0.;
		return FVector2D::Distance(P, A + AB * T);
	}
}

bool UOKStudioGame::WorldBounds(int32 Id, FBox& Out) const
{
	Out = FBox(ForceInit);
	const FOKSceneObject* O = Document ? Document->Find(Id) : nullptr;
	if (!O || !SceneActor) return false;
	if (O->Kind == EOKSceneObjectKind::Character)
	{
		TArray<TPair<FVector, FVector>> Segments;
		if (SkeletonSegments(SceneActor->PreviewOf(Id), Segments)) for (const TPair<FVector, FVector>& S : Segments) { Out += S.Key; Out += S.Value; }
	}
	else if (const AKKPreviewActor* Preview = SceneActor->PreviewOf(Id))
	{
		// Items are shown by a preview actor of their own (like characters): its drawn meshes.
		TInlineComponentArray<UPrimitiveComponent*> Parts(Preview);
		for (const UPrimitiveComponent* P : Parts) if (P->IsVisible() && P->IsRegistered()) Out += P->Bounds.GetBox();
	}
	else if (USceneComponent* C = SceneActor->ComponentOf(Id))
	{
		TArray<USceneComponent*> Children;
		C->GetChildrenComponents(true, Children);
		Children.Add(C);
		for (USceneComponent* Child : Children)
			if (const UPrimitiveComponent* P = Cast<UPrimitiveComponent>(Child); P && P->IsVisible() && P->IsRegistered()) Out += P->Bounds.GetBox();
	}
	if (!Out.IsValid)
	{
		// A light, a camera, a folder or an object not built: a small box where it is.
		const FVector At = (Document->WorldTransform(Id) * SceneActor->GetActorTransform()).GetLocation();
		Out = FBox(At - FVector(15.), At + FVector(15.));
	}
	return true;
}

int32 UOKStudioGame::PickObject(const FVector2D& Position) const
{
	APlayerController* PC = SceneActor && SceneActor->GetWorld() ? SceneActor->GetWorld()->GetFirstPlayerController() : nullptr;
	if (!PC || !PC->PlayerCameraManager || !Document) return -1;
	const FVector Eye = PC->PlayerCameraManager->GetCameraLocation();
	auto Project = [PC](const FVector& W, FVector2D& S) { return UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition(PC, W, S, true); };
	int32 Best = -1;
	double BestDepth = TNumericLimits<double>::Max();
	for (const FOKSceneObject& O : Document->Objects)
	{
		if (!Document->IsShown(O.Id) || O.Kind == EOKSceneObjectKind::Folder) continue;
		double Depth = -1.;
		if (O.Kind == EOKSceneObjectKind::Character)
		{
			// The stick figure of the skeleton, 14 px thick.
			TArray<TPair<FVector, FVector>> Segments;
			if (!SkeletonSegments(SceneActor->PreviewOf(O.Id), Segments)) continue;
			for (const TPair<FVector, FVector>& S : Segments)
			{
				FVector2D A, B;
				if (!Project(S.Key, A) || !Project(S.Value, B)) continue;
				if (PointSegment(Position, A, B) < 14.) { const double D = FVector::Distance(Eye, (S.Key + S.Value) * 0.5); Depth = Depth < 0. ? D : FMath::Min(Depth, D); }
			}
		}
		else if (O.Kind == EOKSceneObjectKind::Light || O.Kind == EOKSceneObjectKind::Camera)
		{
			const FVector At = (Document->WorldTransform(O.Id) * SceneActor->GetActorTransform()).GetLocation();
			FVector2D S;
			if (Project(At, S) && FVector2D::Distance(S, Position) < 12.) Depth = FVector::Distance(Eye, At);
		}
		else
		{
			FBox Box;
			if (!WorldBounds(O.Id, Box)) continue;
			FBox2D Screen(ForceInit);
			int32 Seen = 0;
			for (int32 C = 0; C < 8; ++C)
			{
				const FVector Corner((C & 1) ? Box.Max.X : Box.Min.X, (C & 2) ? Box.Max.Y : Box.Min.Y, (C & 4) ? Box.Max.Z : Box.Min.Z);
				FVector2D S;
				if (Project(Corner, S)) { Screen += S; ++Seen; }
			}
			if (Seen && Screen.IsInside(Position)) Depth = FVector::Distance(Eye, Box.GetCenter());
		}
		if (Depth >= 0. && Depth < BestDepth) { BestDepth = Depth; Best = O.Id; }
	}
	return Best;
}

void UOKStudioGame::FocusSelection()
{
	FBox All(ForceInit);
	for (int32 Id : Selection) { FBox B; if (WorldBounds(Id, B)) All += B; }
	if (!All.IsValid || !SceneActor) return;
	// Back into the scene's own frame (the orbit focus is in world space, the scene actor sits at the origin).
	Focus = All.GetCenter();
	const float Radius = FMath::Max(float(All.GetExtent().Size()), 10.f);
	Distance = FMath::Clamp(Radius / FMath::Tan(FMath::DegreesToRadians(20.f)) * 1.15f, 20.f, 5000.f);
	Document->ActiveCamera = -1;
}

void UOKStudioGame::ViewPreset(int32 Preset)
{
	Document->ActiveCamera = -1;
	switch (Preset)
	{
	case 0: Yaw = -90.f; Pitch = 0.f; break;
	case 1: Yaw = 180.f; Pitch = 0.f; break;
	case 2: Pitch = -89.f; break;
	case 3: Yaw = 90.f; Pitch = 0.f; break;
	case 4: Yaw = 0.f; Pitch = 0.f; break;
	default: Focus = InitialFocus; Yaw = InitialYaw; Pitch = InitialPitch; Distance = InitialDistance; break;
	}
}

void UOKStudioGame::SaveBookmark(int32 Slot)
{
	if (Slot < 0 || Slot > 9) return;
	while (Document->Bookmarks.Num() <= Slot) Document->Bookmarks.AddDefaulted();
	FOKSceneBookmark& B = Document->Bookmarks[Slot];
	B.bSet = true; B.Focus = Focus; B.Yaw = Yaw; B.Pitch = Pitch; B.Distance = Distance; B.Camera = Document->ActiveCamera;
	Document->NotifyChanged();
}

bool UOKStudioGame::LoadBookmark(int32 Slot)
{
	if (!Document->Bookmarks.IsValidIndex(Slot) || !Document->Bookmarks[Slot].bSet) return false;
	const FOKSceneBookmark& B = Document->Bookmarks[Slot];
	Focus = B.Focus; Yaw = B.Yaw; Pitch = B.Pitch; Distance = B.Distance;
	Document->ActiveCamera = Document->Find(B.Camera) ? B.Camera : -1;
	return true;
}

int32 UOKStudioGame::CameraFromView()
{
	const int32 Id = Add(EOKSceneObjectKind::Camera);
	StoreViewInCamera(Id);
	return Id;
}

bool UOKStudioGame::StoreViewInCamera(int32 Id)
{
	FOKSceneObject* O = Document->Find(Id);
	if (!O || O->Kind != EOKSceneObjectKind::Camera || !Viewer) return false;
	UpdateViewer();
	const FTransform World = Viewer->GetActorTransform();
	const FTransform Parent = Document->ParentFrame(Id) * (SceneActor ? SceneActor->GetActorTransform() : FTransform::Identity);
	O->Transform = World.GetRelativeTransform(Parent);
	O->Transform.SetScale3D(FVector::OneVector);
	O->Camera.Fov = Viewer->GetCameraComponent()->FieldOfView;
	Document->NotifyChanged();
	return true;
}

void UOKStudioGame::NextCamera()
{
	TArray<int32> Cameras;
	for (const FOKSceneObject& O : Document->Objects) if (O.Kind == EOKSceneObjectKind::Camera) Cameras.Add(O.Id);
	const int32 At = Cameras.IndexOfByKey(Document->ActiveCamera);
	Document->ActiveCamera = Cameras.IsValidIndex(At + 1) ? Cameras[At + 1] : -1;
}

FString UOKStudioGame::BusyReason() const
{
	if (IsExporting()) return TEXT("exporting");
	if (bTimelinePlaying) return TEXT("playing");
	if (DragAxis >= 0 || bUserDragging) return TEXT("dragging");
	if (SceneActor && SceneActor->HasPendingBuilds()) return TEXT("loading");
	if (bStoryMode) return TEXT("story");
	return FString();
}

FString UOKStudioGame::BackupDirectory()
{
	return FOKStudioSettings::Directory() / TEXT("Backup");
}

TArray<FString> UOKStudioGame::BackupFiles()
{
	TArray<FString> Names;
	IFileManager::Get().FindFiles(Names, *(BackupDirectory() / TEXT("*.okscene")), true, false);
	TArray<FString> Files;
	for (const FString& N : Names) Files.Add(BackupDirectory() / N);
	Files.Sort([](const FString& A, const FString& B) { return A > B; });
	return Files;
}

FString UOKStudioGame::BackupNow(bool bForce)
{
	LastBackupSeconds = FPlatformTime::Seconds();
	if (!Document) return FString();
	const FString Json = Document->ToJson();
	const uint32 Hash = GetTypeHash(Json);
	if (!bForce && Hash == LastBackupHash) return FString();
	LastBackupHash = Hash;
	const FString Base = BackupDirectory() / FDateTime::Now().ToString(TEXT("backup_%Y%m%d_%H%M%S"));
	IFileManager::Get().MakeDirectory(*BackupDirectory(), true);
	FString Error;
	if (!Document->SaveToFile(Base + TEXT(".okscene"), Error)) { UE_LOG(LogTemp, Warning, TEXT("OK studio backup: %s"), *Error); return FString(); }
	// A small picture of the view for the restore window.
	CaptureToPng(Base + TEXT(".png"), 256, 144, Error);
	TArray<FString> Files = BackupFiles();
	const int32 Keep = FMath::Max(1, FOKStudioSettings::Get().BackupKeep);
	for (int32 I = Keep; I < Files.Num(); ++I)
	{
		IFileManager::Get().Delete(*Files[I], false, true, true);
		IFileManager::Get().Delete(*FPaths::ChangeExtension(Files[I], TEXT("png")), false, true, true);
	}
	UE_LOG(LogTemp, Display, TEXT("OK studio: backup %s.okscene"), *Base);
	return Base + TEXT(".okscene");
}

TArray<FString> UOKStudioGame::LoadReport() const
{
	TArray<FString> Lines;
	if (!SceneActor || !Document) return Lines;
	for (const TPair<int32, FString>& E : SceneActor->Errors)
	{
		const FOKSceneObject* O = Document->Find(E.Key);
		Lines.Add(FString::Printf(TEXT("#%d %s：%s"), E.Key, O ? *O->Name : TEXT("?"), *E.Value));
	}
	if (!SceneActor->MapError.IsEmpty()) Lines.Add(TEXT("地圖：") + SceneActor->MapError);
	return Lines;
}
