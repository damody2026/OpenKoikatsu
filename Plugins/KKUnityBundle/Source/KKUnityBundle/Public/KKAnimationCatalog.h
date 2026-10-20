#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Engine/DataTable.h"
#include "KKAnimationCatalog.generated.h"

// Source rows retain every cell, including duplicate clip names and empty cells.
USTRUCT(BlueprintType)
struct KKUNITYBUNDLE_API FKKExportCatalogRow : public FTableRowBase
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Koikatsu")
	TArray<FString> Values;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Koikatsu")
	FString RuntimeSource;
};

USTRUCT(BlueprintType)
struct KKUNITYBUNDLE_API FKKExportCatalogTable
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Koikatsu")
	FName Name;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Koikatsu")
	TArray<FString> Columns;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Koikatsu")
	TObjectPtr<UDataTable> Data;
	// UDataTable's map order is not source order.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Koikatsu")
	TArray<FName> RowOrder;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Koikatsu")
	FString SourceSha256;
};

UCLASS(BlueprintType)
class KKUNITYBUNDLE_API UKKAnimationCatalog : public UDataAsset
{
	GENERATED_BODY()
public:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Koikatsu")
	int32 SchemaVersion = 1;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Koikatsu")
	TArray<FKKExportCatalogTable> Tables;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Koikatsu")
	FString ReferenceMarkdown;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Koikatsu")
	FString DocumentSha256;

	UFUNCTION(BlueprintCallable, Category="Koikatsu|Catalog")
	static UKKAnimationCatalog* LoadDefaultCatalog();
	UFUNCTION(BlueprintPure, Category="Koikatsu|Catalog")
	int32 GetRowCount(FName Table) const;
	UFUNCTION(BlueprintPure, Category="Koikatsu|Catalog")
	FString GetCell(FName Table, FName Row, const FString& Column) const;
	UFUNCTION(BlueprintPure, Category="Koikatsu|Catalog")
	TArray<FName> FindRows(FName Table, const FString& Column, const FString& Value) const;
	UFUNCTION(BlueprintPure, Category="Koikatsu|Catalog")
	TArray<FString> GetAnimationSources() const;
	UFUNCTION(BlueprintPure, Category="Koikatsu|Catalog")
	TArray<FName> GetExpressionPatterns(const FString& Source, const FString& Prefab,
		const FString& Controller, int32 TargetIndex) const;

	const FKKExportCatalogTable* FindTable(FName Name) const;
	const FKKExportCatalogRow* FindRow(FName Table, FName Row) const;
	// Match authored Studio controller/state identity against the actual game's
	// resolved state labels. A duplicate or missing state is not a bare clip guess.
	bool ResolveStudioAnimation(FName Row, const TArray<FString>& StateLabels,
		FString& OutKey, FString& OutError) const;
	FString StudioSource(FName Row) const;
	bool StudioSettings(FName Row, bool& IsH, int32& Layer, bool& IsMotion) const;
	FString StudioBundleSource(FName Row, bool Override) const;
	void StudioYureSource(FName Row, FString& Source, FString& Owner) const;
	int32 StudioPVMask(FName Row) const;
};
