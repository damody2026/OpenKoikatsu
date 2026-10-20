#pragma once

#include "CoreMinimal.h"
#include "ThumbnailRendering/ThumbnailRenderer.h"
#include "OKAnimThumbnailRenderer.generated.h"

/** Content browser thumbnail of an OK animation: its middle frame as a stick figure on the family preview skeleton. */
UCLASS()
class UOKAnimThumbnailRenderer : public UThumbnailRenderer
{
	GENERATED_BODY()

public:
	virtual bool CanVisualizeAsset(UObject* Object) override;
	virtual void Draw(UObject* Object, int32 X, int32 Y, uint32 Width, uint32 Height, FRenderTarget* Target, FCanvas* Canvas, bool bAdditionalViewFamily) override;
	virtual EThumbnailRenderFrequency GetThumbnailRenderFrequency(UObject* Object) const override { return EThumbnailRenderFrequency::OnAssetSave; }

	/** The 2D lines (in [0,1] x [0,1], y down) of an animation's pose at Time: what the thumbnail draws. */
	static bool PoseLines(const class UOKAnimSequence* Seq, float Time, TArray<TPair<FVector2D, FVector2D>>& OutLines);
};
