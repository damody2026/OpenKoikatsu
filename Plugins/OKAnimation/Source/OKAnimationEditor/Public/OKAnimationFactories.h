#pragma once

#include "CoreMinimal.h"
#include "Factories/Factory.h"
#include "AssetDefinitionDefault.h"
#include "OKAnimationFactories.generated.h"

/** Imports .okanim (one OK animation) and .okgroup (OKGROUP1 package: a group and its role animations) files. */
UCLASS()
class OKANIMATIONEDITOR_API UOKAnimationImportFactory : public UFactory
{
	GENERATED_BODY()

public:
	UOKAnimationImportFactory();
	virtual UObject* FactoryCreateBinary(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, UObject* Context, const TCHAR* Type,
		const uint8*& Buffer, const uint8* BufferEnd, FFeedbackContext* Warn) override;
	virtual bool FactoryCanImport(const FString& Filename) override;
};

/** .fbx -> OK animations, when UOKAnimationSettings::bImportFbxAsOK is on (otherwise Unreal's FBX import handles .fbx). */
UCLASS()
class OKANIMATIONEDITOR_API UOKFbxImportFactory : public UFactory
{
	GENERATED_BODY()

public:
	UOKFbxImportFactory();
	virtual bool FactoryCanImport(const FString& Filename) override;
	virtual UObject* FactoryCreateFile(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, const FString& Filename, const TCHAR* Parms, FFeedbackContext* Warn, bool& bOutOperationCanceled) override;
};

/** "New" factories of the OK asset types (content browser > OK Animation). */
UCLASS(Abstract)
class OKANIMATIONEDITOR_API UOKAssetFactoryNew : public UFactory
{
	GENERATED_BODY()

public:
	UOKAssetFactoryNew();
	virtual UObject* FactoryCreateNew(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;
	virtual bool ShouldShowInNewMenu() const override { return true; }
	virtual uint32 GetMenuCategories() const override;
};

/** A new OK animation holding the family rest pose (61 frames at 30 fps): a blank start to key in the animate mode. */
UCLASS() class OKANIMATIONEDITOR_API UOKAnimSequenceFactoryNew : public UOKAssetFactoryNew { GENERATED_BODY() public: UOKAnimSequenceFactoryNew(); };
UCLASS() class OKANIMATIONEDITOR_API UOKGroupAnimSequenceFactoryNew : public UOKAssetFactoryNew { GENERATED_BODY() public: UOKGroupAnimSequenceFactoryNew(); };
UCLASS() class OKANIMATIONEDITOR_API UOKBlendSpaceFactoryNew : public UOKAssetFactoryNew { GENERATED_BODY() public: UOKBlendSpaceFactoryNew(); };
UCLASS() class OKANIMATIONEDITOR_API UOKAnimMontageFactoryNew : public UOKAssetFactoryNew { GENERATED_BODY() public: UOKAnimMontageFactoryNew(); };
UCLASS() class OKANIMATIONEDITOR_API UOKSkeletonFamilyFactoryNew : public UOKAssetFactoryNew { GENERATED_BODY() public: UOKSkeletonFamilyFactoryNew(); };
UCLASS() class OKANIMATIONEDITOR_API UOKRetargetProfileFactoryNew : public UOKAssetFactoryNew { GENERATED_BODY() public: UOKRetargetProfileFactoryNew(); };
UCLASS() class OKANIMATIONEDITOR_API UOKPoseLibraryFactoryNew : public UOKAssetFactoryNew { GENERATED_BODY() public: UOKPoseLibraryFactoryNew(); };

/** Asset definitions: names, colours, category, and the OK animation editor for the playable types. */
UCLASS(Abstract)
class OKANIMATIONEDITOR_API UAssetDefinition_OKBase : public UAssetDefinitionDefault
{
	GENERATED_BODY()

public:
	virtual FLinearColor GetAssetColor() const override { return FLinearColor(0.85f, 0.35f, 0.55f); }
	virtual TConstArrayView<FAssetCategoryPath> GetAssetCategories() const override;
};

UCLASS(Abstract)
class OKANIMATIONEDITOR_API UAssetDefinition_OKPlayable : public UAssetDefinition_OKBase
{
	GENERATED_BODY()

public:
	virtual EAssetCommandResult OpenAssets(const FAssetOpenArgs& OpenArgs) const override;
};

UCLASS() class UAssetDefinition_OKAnimSequence : public UAssetDefinition_OKPlayable
{
	GENERATED_BODY()
public:
	virtual FText GetAssetDisplayName() const override;
	virtual TSoftClassPtr<UObject> GetAssetClass() const override;
};
UCLASS() class UAssetDefinition_OKGroupAnimSequence : public UAssetDefinition_OKPlayable
{
	GENERATED_BODY()
public:
	virtual FText GetAssetDisplayName() const override;
	virtual TSoftClassPtr<UObject> GetAssetClass() const override;
	virtual FLinearColor GetAssetColor() const override { return FLinearColor(0.95f, 0.55f, 0.25f); }
};
UCLASS() class UAssetDefinition_OKBlendSpace : public UAssetDefinition_OKPlayable
{
	GENERATED_BODY()
public:
	virtual FText GetAssetDisplayName() const override;
	virtual TSoftClassPtr<UObject> GetAssetClass() const override;
	virtual FLinearColor GetAssetColor() const override { return FLinearColor(0.55f, 0.45f, 0.95f); }
};
UCLASS() class UAssetDefinition_OKAnimMontage : public UAssetDefinition_OKPlayable
{
	GENERATED_BODY()
public:
	virtual FText GetAssetDisplayName() const override;
	virtual TSoftClassPtr<UObject> GetAssetClass() const override;
	virtual FLinearColor GetAssetColor() const override { return FLinearColor(0.35f, 0.75f, 0.85f); }
};
UCLASS() class UAssetDefinition_OKSkeletonFamily : public UAssetDefinition_OKBase
{
	GENERATED_BODY()
public:
	virtual EAssetCommandResult OpenAssets(const FAssetOpenArgs& OpenArgs) const override;
	virtual FText GetAssetDisplayName() const override;
	virtual TSoftClassPtr<UObject> GetAssetClass() const override;
};
UCLASS() class UAssetDefinition_OKPoseLibrary : public UAssetDefinition_OKBase
{
	GENERATED_BODY()
public:
	virtual FText GetAssetDisplayName() const override;
	virtual TSoftClassPtr<UObject> GetAssetClass() const override;
};
UCLASS() class UAssetDefinition_OKRetargetProfile : public UAssetDefinition_OKBase
{
	GENERATED_BODY()
public:
	virtual FText GetAssetDisplayName() const override;
	virtual TSoftClassPtr<UObject> GetAssetClass() const override;
};
