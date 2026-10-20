#pragma once

#include "CoreMinimal.h"
#include "Factories/Factory.h"
#include "AssetDefinitionDefault.h"
#include "OKModelFactories.generated.h"

/** Imports an .okmodel of an OK base library as a UOKModelAsset (the asset references the model in place). */
UCLASS()
class OKMODELEDITOR_API UOKModelImportFactory : public UFactory
{
	GENERATED_BODY()

public:
	UOKModelImportFactory();
	virtual bool FactoryCanImport(const FString& Filename) override;
	virtual UObject* FactoryCreateFile(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, const FString& Filename,
		const TCHAR* Parms, FFeedbackContext* Warn, bool& bOutOperationCanceled) override;
};

UCLASS()
class OKMODELEDITOR_API UAssetDefinition_OKModel : public UAssetDefinitionDefault
{
	GENERATED_BODY()

public:
	virtual FText GetAssetDisplayName() const override;
	virtual TSoftClassPtr<UObject> GetAssetClass() const override;
	virtual FLinearColor GetAssetColor() const override { return FLinearColor(0.35f, 0.85f, 0.55f); }
	virtual TConstArrayView<FAssetCategoryPath> GetAssetCategories() const override;
	virtual EAssetCommandResult OpenAssets(const FAssetOpenArgs& OpenArgs) const override;
};
