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

	// Local library: models of third-party mods (character and Studio mods) converted on this machine, kept apart from
	// the shared base library (OK_LOCAL_LIBRARY; empty: Content/IndependentResources/local_v1 when present).
	UPROPERTY(config, EditAnywhere, Category = "OpenKoikatsu")
	FString LocalLibrary;

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
