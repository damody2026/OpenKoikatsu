#pragma once

#include "CoreMinimal.h"
#include "KKCardTypes.h"
#include "KKCardReader.generated.h"

UCLASS()
class KKCARD_API UKKCardReader : public UObject
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "KKCard")
	static bool LoadHeaderFromFile(
		const FString& Path,
		FKKCardHeader& OutHeader,
		TArray<FKKBlockInfo>& OutBlocks,
		FString& OutError);

	UFUNCTION(BlueprintCallable, Category = "KKCard")
	static bool LoadFromFile(const FString& Path, FKKCharacterFile& OutFile, FString& OutError);

	static bool LoadFromBytes(const TArray<uint8>& Bytes, FKKCharacterFile& OutFile, FString& OutError);
};
