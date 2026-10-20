#pragma once

#include "CoreMinimal.h"
#include "KKCardTypes.h"
#include "KKCardWriter.generated.h"

USTRUCT(BlueprintType)
struct KKCARD_API FKKCardListEntry
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "KKCard")
	FString Path;

	UPROPERTY(BlueprintReadOnly, Category = "KKCard")
	FString FileName;

	UPROPERTY(BlueprintReadOnly, Category = "KKCard")
	EKKCardKind Kind = EKKCardKind::Unknown;

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
};

UCLASS()
class KKCARD_API UKKCardWriter : public UObject
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "KKCard")
	static bool SaveUnchanged(const FKKCharacterFile& File, const FString& Path, FString& OutError);

	UFUNCTION(BlueprintCallable, Category = "KKCard")
	static bool SaveWithEdits(FKKCharacterFile& File, const FString& Path, FString& OutError);
};

UCLASS()
class KKCARD_API UKKCardLibrary : public UObject
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "KKCard")
	static TArray<FKKCardListEntry> ScanFolder(const FString& Folder);
};
