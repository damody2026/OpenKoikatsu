#pragma once

#include "CoreMinimal.h"
#include "KKCardTypes.generated.h"

UENUM(BlueprintType)
enum class EKKCardKind : uint8
{
	Unknown UMETA(DisplayName = "Unknown"),
	KK UMETA(DisplayName = "KK"),
	KKSP UMETA(DisplayName = "KKSP"),
	KKS UMETA(DisplayName = "KKS"),
	Coordinate UMETA(DisplayName = "Coordinate"),
	Scene UMETA(DisplayName = "Scene")
};

USTRUCT(BlueprintType)
struct KKCARD_API FKKCardHeader
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "KKCard")
	int32 ProductNo = 0;

	UPROPERTY(BlueprintReadOnly, Category = "KKCard")
	FString Marker;

	UPROPERTY(BlueprintReadOnly, Category = "KKCard")
	FString Version;

	UPROPERTY(BlueprintReadOnly, Category = "KKCard")
	EKKCardKind Kind = EKKCardKind::Unknown;

	UPROPERTY(BlueprintReadOnly, Category = "KKCard")
	int32 PngEndOffset = 0;

	TArray<uint8> PreviewPng;
	TArray<uint8> FacePng;
	TArray<uint8> RawBlockTable;
};

USTRUCT(BlueprintType)
struct KKCARD_API FKKBlockInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "KKCard")
	FString Name;

	UPROPERTY(BlueprintReadOnly, Category = "KKCard")
	int32 Pos = 0;

	UPROPERTY(BlueprintReadOnly, Category = "KKCard")
	int32 Size = 0;
};

USTRUCT(BlueprintType)
struct KKCARD_API FKKFace
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "KKCard")
	TArray<float> ShapeValueFace;

	UPROPERTY(BlueprintReadOnly, Category = "KKCard")
	int32 HeadId = 0;

	UPROPERTY(BlueprintReadOnly, Category = "KKCard")
	int32 SkinId = 0;

	UPROPERTY(BlueprintReadOnly, Category = "KKCard")
	int32 DetailId = 0;

	UPROPERTY(BlueprintReadOnly, Category = "KKCard")
	int32 EyebrowId = 0;

	UPROPERTY(BlueprintReadOnly, Category = "KKCard")
	int32 NoseId = 0;

	UPROPERTY(BlueprintReadOnly, Category = "KKCard")
	int32 HlUpId = 0;

	UPROPERTY(BlueprintReadOnly, Category = "KKCard")
	int32 HlDownId = 0;

	UPROPERTY(BlueprintReadOnly, Category = "KKCard")
	int32 EyelineUpId = 0;

	UPROPERTY(BlueprintReadOnly, Category = "KKCard")
	int32 EyelineDownId = 0;

	UPROPERTY(BlueprintReadOnly, Category = "KKCard")
	int32 WhiteId = 0;

	UPROPERTY(BlueprintReadOnly, Category = "KKCard")
	int32 MoleId = 0;

	UPROPERTY(BlueprintReadOnly, Category = "KKCard")
	int32 LipLineId = 0;

	// ChaFileFace.foregroundEyes / foregroundEyebrow: 0 follows the game setting (Config.EtcData), 1 off, 2 on. On draws
	// the eyes / eyebrows through the front hair (ChaControl.SetForegroundEyesAndEyebrow).
	UPROPERTY(BlueprintReadOnly, Category = "KKCard")
	int32 ForegroundEyes = 0;

	UPROPERTY(BlueprintReadOnly, Category = "KKCard")
	int32 ForegroundEyebrow = 0;
};

USTRUCT(BlueprintType)
struct KKCARD_API FKKBody
{
	GENERATED_BODY()

 UPROPERTY(BlueprintReadOnly, Category="KKCard") float BustSoftness=0.5f;
 UPROPERTY(BlueprintReadOnly, Category="KKCard") float BustWeight=0.5f;

	UPROPERTY(BlueprintReadOnly, Category = "KKCard")
	TArray<float> ShapeValueBody;

	UPROPERTY(BlueprintReadOnly, Category = "KKCard")
	int32 SkinId = 0;

	UPROPERTY(BlueprintReadOnly, Category = "KKCard")
	int32 DetailId = 0;
};

USTRUCT(BlueprintType)
struct KKCARD_API FKKHairPart
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "KKCard")
	int32 Id = 0;
};

USTRUCT(BlueprintType)
struct KKCARD_API FKKHair
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "KKCard")
	FKKHairPart Front;

	UPROPERTY(BlueprintReadOnly, Category = "KKCard")
	FKKHairPart Back;

	UPROPERTY(BlueprintReadOnly, Category = "KKCard")
	FKKHairPart Side;

	UPROPERTY(BlueprintReadOnly, Category = "KKCard")
	FKKHairPart Extension;
};

USTRUCT(BlueprintType)
struct KKCARD_API FKKClothesPart
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "KKCard")
	int32 Id = 0;
};

USTRUCT(BlueprintType)
struct KKCARD_API FKKAccessoryPart
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "KKCard")
	int32 Type = 0;

	UPROPERTY(BlueprintReadOnly, Category = "KKCard")
	int32 Id = 0;

	UPROPERTY(BlueprintReadOnly, Category = "KKCard")
	bool bUsed = false;
};

USTRUCT(BlueprintType)
struct KKCARD_API FKKCoordinateSlot
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "KKCard")
	TArray<FKKClothesPart> Clothes;

	UPROPERTY(BlueprintReadOnly, Category = "KKCard")
	TArray<FKKAccessoryPart> Accessories;
};

USTRUCT(BlueprintType)
struct KKCARD_API FKKResolveInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "KKCard")
	FString ModID;

	UPROPERTY(BlueprintReadOnly, Category = "KKCard")
	int32 Slot = 0;

	UPROPERTY(BlueprintReadOnly, Category = "KKCard")
	int32 LocalSlot = 0;

	UPROPERTY(BlueprintReadOnly, Category = "KKCard")
	FString Property;

	UPROPERTY(BlueprintReadOnly, Category = "KKCard")
	int32 CategoryNo = 0;

	UPROPERTY(BlueprintReadOnly, Category = "KKCard")
	FString Author;

	UPROPERTY(BlueprintReadOnly, Category = "KKCard")
	FString Name;
};

// A pinned immutable preview revision. Parameter edits stay in the card;
// resource materialization is deferred until a consumer needs a portable PNG.
// Implementations must support copies of a card and concurrent save workers.
class KKCARD_API IKKDeferredCardPreview
{
public:
    virtual ~IKKDeferredCardPreview() = default;
    virtual bool BuildPreviewPng(TArray<uint8>& OutPreview, FString& OutError) const = 0;
};

USTRUCT(BlueprintType)
struct KKCARD_API FKKCharacterFile
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "KKCard")
	FKKCardHeader Header;

	UPROPERTY(BlueprintReadOnly, Category = "KKCard")
	TArray<FKKBlockInfo> Blocks;

	UPROPERTY(BlueprintReadOnly, Category = "KKCard")
	FKKFace Face;

	UPROPERTY(BlueprintReadOnly, Category = "KKCard")
	FKKBody Body;

	UPROPERTY(BlueprintReadOnly, Category = "KKCard")
	FKKHair Hair;

	UPROPERTY(BlueprintReadOnly, Category = "KKCard")
	TArray<FKKCoordinateSlot> Outfits;

	UPROPERTY(BlueprintReadOnly, Category = "KKCard")
	FString LastName;

	UPROPERTY(BlueprintReadOnly, Category = "KKCard")
	FString FirstName;

	UPROPERTY(BlueprintReadOnly, Category = "KKCard")
	FString Nickname;

	UPROPERTY(BlueprintReadOnly, Category = "KKCard")
	int32 Sex = 1;

	UPROPERTY(BlueprintReadOnly, Category = "KKCard")
	int32 Personality = 0;
	UPROPERTY(BlueprintReadOnly, Category = "KKCard")
	float VoiceRate = 0.5f;

	UPROPERTY(BlueprintReadOnly, Category = "KKCard")
	TArray<FKKResolveInfo> Resolves;

	// KK_SkinEffects card settings (KKEx "Marco.SkinEffects"): her hymen grows back after each time, it is already
	// stretched (no blood the first time), she bleeds easily (陰部脆弱).
	UPROPERTY(BlueprintReadOnly, Category = "KKCard")
	bool bHymenRegen = false;
	UPROPERTY(BlueprintReadOnly, Category = "KKCard")
	bool bStretchedHymen = false;
	UPROPERTY(BlueprintReadOnly, Category = "KKCard")
	bool bFragileVag = false;

	TMap<FString, TArray<uint8>> RawBlocks;
	TArray<uint8> OriginalBytes;
    TSharedPtr<const IKKDeferredCardPreview, ESPMode::ThreadSafe> DeferredPreview;
};
