#pragma once

#include "CoreMinimal.h"
#include "KKGameRoot.generated.h"

UCLASS(config = Game, defaultconfig)
class KKSIDELOADER_API UKKGameRoot : public UObject
{
	GENERATED_BODY()

public:
	UKKGameRoot();

	UPROPERTY(config, EditAnywhere, Category = "OpenKoikatsu")
	FString Root;

	UFUNCTION(BlueprintCallable, Category = "OpenKoikatsu")
	static UKKGameRoot* Get();

	UFUNCTION(BlueprintCallable, Category = "OpenKoikatsu")
	FString Abdata() const;

	UFUNCTION(BlueprintCallable, Category = "OpenKoikatsu")
	FString Mods() const;

	UFUNCTION(BlueprintCallable, Category = "OpenKoikatsu")
	FString CharaFemale() const;

	UFUNCTION(BlueprintCallable, Category = "OpenKoikatsu")
	FString Coordinate() const;
};
