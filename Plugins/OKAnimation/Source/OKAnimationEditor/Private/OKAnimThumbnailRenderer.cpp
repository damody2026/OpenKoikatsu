#include "OKAnimThumbnailRenderer.h"
#include "OKAnimationAssets.h"
#include "OKAnimationLibrary.h"
#include "OKUnrealSkeleton.h"
#include "CanvasItem.h"
#include "CanvasTypes.h"
#include "Engine/SkeletalMesh.h"

bool UOKAnimThumbnailRenderer::CanVisualizeAsset(UObject* Object)
{
	const UOKAnimSequence* Seq = Cast<UOKAnimSequence>(Object);
	return Seq && !Seq->Data.IsEmpty();
}

bool UOKAnimThumbnailRenderer::PoseLines(const UOKAnimSequence* Seq, float Time, TArray<TPair<FVector2D, FVector2D>>& OutLines)
{
	OutLines.Reset();
	UOKSkeletonFamily* Family = UOKAnimationLibrary::GetDefaultFamily();
	USkeletalMesh* Mesh = Family ? Family->PreviewMesh.LoadSynchronous() : nullptr;
	if (!Seq || !Mesh) return false;
	// One binding for the preview skeleton (the thumbnails of a folder render one after another).
	static FOKUnrealSkeleton Binding;
	static TWeakObjectPtr<USkeletalMesh> BoundMesh;
	FString Error;
	if (BoundMesh.Get() != Mesh || !Binding.IsValid())
	{
		Binding = FOKUnrealSkeleton();
		if (!Binding.Build(Mesh->GetRefSkeleton(), nullptr, Family, Error)) return false;
		BoundMesh = Mesh;
	}
	TArray<FTransform> Local;
	if (!Binding.SamplePose(Seq, Time, Local)) return false;
	const FReferenceSkeleton& Ref = Mesh->GetRefSkeleton();
	TArray<FVector> Pos;
	TArray<FTransform> Comp;
	Comp.SetNum(Local.Num());
	for (int32 I = 0; I < Local.Num(); ++I)
	{
		const int32 P = Ref.GetParentIndex(I);
		Comp[I] = P == INDEX_NONE ? Local[I] : Local[I] * Comp[P];
	}
	// A three-quarter view: screen x from the side and front axes, screen y from up.
	FBox2D Bounds(ForceInit);
	TArray<FVector2D> Screen;
	for (const FTransform& T : Comp)
	{
		const FVector P = T.GetLocation();
		const FVector2D S(P.X * 0.8 + P.Y * 0.45, -P.Z + P.Y * 0.15);
		Screen.Add(S);
		Bounds += S;
	}
	const FVector2D Size = Bounds.GetSize();
	const double Scale = 0.9 / FMath::Max(1.e-3, FMath::Max(Size.X, Size.Y));
	const FVector2D Center = Bounds.GetCenter();
	auto Fit = [&](const FVector2D& S) { return FVector2D(0.5, 0.5) + (S - Center) * Scale; };
	for (int32 H = 0; H < Binding.BoneIndex.Num(); ++H)
	{
		const int32 P = Binding.MappedParent[H];
		if (P == INDEX_NONE) continue;
		OutLines.Emplace(Fit(Screen[Binding.BoneIndex[P]]), Fit(Screen[Binding.BoneIndex[H]]));
	}
	return OutLines.Num() > 0;
}

void UOKAnimThumbnailRenderer::Draw(UObject* Object, int32 X, int32 Y, uint32 Width, uint32 Height, FRenderTarget*, FCanvas* Canvas, bool)
{
	const UOKAnimSequence* Seq = Cast<UOKAnimSequence>(Object);
	Canvas->DrawTile(X, Y, Width, Height, 0, 0, 1, 1, FLinearColor(0.06f, 0.07f, 0.09f));
	TArray<TPair<FVector2D, FVector2D>> Lines;
	if (!PoseLines(Seq, Seq ? Seq->Duration * 0.5f : 0.f, Lines)) return;
	for (const TPair<FVector2D, FVector2D>& L : Lines)
	{
		FCanvasLineItem Line(FVector2D(X + L.Key.X * Width, Y + L.Key.Y * Height), FVector2D(X + L.Value.X * Width, Y + L.Value.Y * Height));
		Line.SetColor(FLinearColor(0.95f, 0.55f, 0.25f));
		Line.LineThickness = FMath::Max(1.f, Width / 96.f);
		Canvas->DrawItem(Line);
	}
}
