#include "OKModelFactories.h"
#include "OKModelAsset.h"
#include "OKModelEditorToolkit.h"
#include "KKRustExtract.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Containers/Ticker.h"
#include "Editor.h"
#include "HAL/IConsoleManager.h"
#include "Misc/PackageName.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

#define LOCTEXT_NAMESPACE "OKModelEditor"

IMPLEMENT_MODULE(FDefaultModuleImpl, OKModelEditor)

UOKModelImportFactory::UOKModelImportFactory()
{
	SupportedClass = UOKModelAsset::StaticClass();
	bEditorImport = true;
	bCreateNew = false;
	bText = false;
	Formats.Add(TEXT("okmodel;OK Base Library Model"));
}

bool UOKModelImportFactory::FactoryCanImport(const FString& Filename)
{
	return FPaths::GetExtension(Filename).Equals(TEXT("okmodel"), ESearchCase::IgnoreCase);
}

UObject* UOKModelImportFactory::FactoryCreateFile(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, const FString& Filename,
	const TCHAR* Parms, FFeedbackContext* Warn, bool& bOutOperationCanceled)
{
	FString Library, Id, Error;
	if (!FOkModel::Locate(Filename, Library, Id))
	{
		Warn->Logf(ELogVerbosity::Error, TEXT("%s is not inside an OK base library (<library>/models/xx/<id>.okmodel)"), *Filename);
		return nullptr;
	}
	UOKModelAsset* Asset = NewObject<UOKModelAsset>(InParent, InName, Flags | RF_Transactional);
	Asset->Library = Library;
	Asset->ModelId = Id;
	if (!Asset->Refresh(Error))
	{
		Warn->Logf(ELogVerbosity::Error, TEXT("OK model %s: %s"), *Filename, *Error);
		return nullptr;
	}
	return Asset;
}

FText UAssetDefinition_OKModel::GetAssetDisplayName() const { return LOCTEXT("OKModel", "OK Model"); }
TSoftClassPtr<UObject> UAssetDefinition_OKModel::GetAssetClass() const { return UOKModelAsset::StaticClass(); }

TConstArrayView<FAssetCategoryPath> UAssetDefinition_OKModel::GetAssetCategories() const
{
	static const FAssetCategoryPath Categories[] = { FAssetCategoryPath(EAssetCategoryPaths::Basic, LOCTEXT("OKCategory", "OK Base Library")) };
	return Categories;
}

EAssetCommandResult UAssetDefinition_OKModel::OpenAssets(const FAssetOpenArgs& OpenArgs) const
{
	for (UOKModelAsset* Asset : OpenArgs.LoadObjects<UOKModelAsset>())
	{
		const TSharedRef<FOKModelEditor> Editor = MakeShared<FOKModelEditor>();
		Editor->InitEditor(OpenArgs.GetToolkitMode(), OpenArgs.ToolkitHost, Asset);
	}
	return EAssetCommandResult::Handled;
}

namespace
{
	/** Asset for an .okmodel file (imported into /Game/OKModels/<id> once) or an asset path. */
	UOKModelAsset* ModelAsset(const FString& Arg)
	{
		FString Library, Id;
		if (!FOkModel::Locate(Arg, Library, Id)) return LoadObject<UOKModelAsset>(nullptr, *Arg);
		const FString PackageName = TEXT("/Game/OKModels/OK_") + Id;
		if (UOKModelAsset* Existing = LoadObject<UOKModelAsset>(nullptr, *(PackageName + TEXT(".OK_") + Id))) return Existing;
		UPackage* Package = CreatePackage(*PackageName);
		UOKModelImportFactory* Factory = NewObject<UOKModelImportFactory>();
		bool bCanceled = false;
		UOKModelAsset* Asset = Cast<UOKModelAsset>(Factory->FactoryCreateFile(UOKModelAsset::StaticClass(), Package, FName(TEXT("OK_") + Id),
			RF_Public | RF_Standalone, Arg, nullptr, GWarn, bCanceled));
		if (!Asset) return nullptr;
		FAssetRegistryModule::AssetCreated(Asset);
		FSavePackageArgs Save;
		Save.TopLevelFlags = RF_Public | RF_Standalone;
		UPackage::SavePackage(Package, Asset, *FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension()), Save);
		return Asset;
	}

	FAutoConsoleCommand OpenCommand(TEXT("OKModel.Open"),
		TEXT("OKModel.Open <file.okmodel | asset path>: opens an OK base library model in the OK model editor (a file is imported into /Game/OKModels first)."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			UOKModelAsset* Asset = Args.Num() ? ModelAsset(Args[0]) : nullptr;
			if (!Asset) { UE_LOG(LogTemp, Error, TEXT("OKModel.Open: no OK model for %s"), Args.Num() ? *Args[0] : TEXT("(nothing)")); return; }
			if (GEditor) GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(Asset);
		}));

	FAutoConsoleCommand SelfTestCommand(TEXT("OKModel.EditorSelfTest"),
		TEXT("OKModel.EditorSelfTest [exit]: runs the self test of the last opened OK model editor once its preview is loaded."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			const bool bExit = Args.Contains(TEXT("exit"));
			TSharedRef<int32> Tries = MakeShared<int32>(0), Ready = MakeShared<int32>(0);
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([bExit, Tries, Ready](float)
			{
				FOKModelEditor* Editor = FOKModelEditor::LastOpened;
				// A few more frames after the preview loaded so the viewport has drawn it.
				if (!Editor || !Editor->HasPreview()) { if (++*Tries < 600) return true; }
				else if (++*Ready < 20) return true;
				if (Editor) Editor->RunSelfTest(bExit);
				else { UE_LOG(LogTemp, Error, TEXT("OK model selftest: no OK model editor open")); if (bExit) FPlatformMisc::RequestExit(false); }
				return false;
			}), 0.1f);
		}));
}

#undef LOCTEXT_NAMESPACE
