#pragma once

#include "CoreMinimal.h"
#include "KKCardWorkspace.generated.h"

// An empty CardDirectory keeps the existing game-backed gallery available.
UCLASS(config = Game)
class KKCARD_API UKKCardWorkspace : public UObject
{

	GENERATED_BODY()

public:
	UPROPERTY(config, EditAnywhere, Category = "OpenKoikatsu|Cards")
	FString CardDirectory;

	UPROPERTY(config, EditAnywhere, Category = "OpenKoikatsu|Cards")
	FString ApngCardDirectory;

	UPROPERTY(config, EditAnywhere, Category = "OpenKoikatsu|Cards")
	FString OutputDirectory;

	UPROPERTY(config, EditAnywhere, Category = "OpenKoikatsu|Cards")
	bool bCopyLocalZipmods = true;

	static FString NewCardPath(const FString& Directory, const FString& SourcePath);
	/** Self-contained card source: an okPv card, its .okpreview pack, or an okLb card opened from the OK base library. */
	static FString PortableSourcePath(const FString& CardPath);
	/**
	 * What a preview opens for this card: PortableSourcePath, or, with the OK base library installed, an original card itself
	 * (opened from the library plus its zipmods). Applying appearance edits to an OK card yields such an original card.
	 */
	static FString LoadableSourcePath(const FString& CardPath);
	/** Content/IndependentResources/base_v1 when the OK base library is installed, else empty. */
	static FString LibraryRoot();
	static TArray<uint8> PngPreviewBytes(const TArray<uint8>& Png);
};
