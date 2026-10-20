#include "KKImportAnimationCatalogCommandlet.h"
#include "KKAnimationCatalog.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/PackageName.h"
#include "HAL/FileManager.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#if WITH_EDITOR
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/SavePackage.h"
#include "UObject/Package.h"
#endif

UKKImportAnimationCatalogCommandlet::UKKImportAnimationCatalogCommandlet()
{
	IsClient = false; IsServer = false; IsEditor = true; LogToConsole = true;
}

int32 UKKImportAnimationCatalogCommandlet::Main(const FString& Params)
{
#if WITH_EDITOR
	FString Input;
	if (!FParse::Value(*Params, TEXT("Input="), Input))
		Input = FPaths::ProjectSavedDir() / TEXT("anim_export/unreal_catalog.json");
	FString Text;
	TSharedPtr<FJsonObject> Document;
	if (!FFileHelper::LoadFileToString(Text, *Input)
		|| !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Document)
		|| !Document || Document->GetIntegerField(TEXT("schema")) != 1)
	{
		UE_LOG(LogTemp, Error, TEXT("KK catalog: invalid import document %s"), *Input); return 1;
	}
	// Validate the entire document before modifying any persistent package.
	TArray<FKKExportCatalogTable> Tables;
	TArray<TArray<FKKExportCatalogRow>> Rows;
	TSet<FName> Names;
	const TSet<FString> ExpectedNames = {TEXT("animation_sources"), TEXT("animation_clips"),
		TEXT("studio_catalog"), TEXT("fbs_patterns"), TEXT("fbs_channels"),
		TEXT("mod_anim_bundles"), TEXT("mod_studio_info"), TEXT("mod_head_prefabs")};
	FString Markdown, DocumentHash;
	if (!Document->TryGetStringField(TEXT("document"), Markdown)
		|| !Document->TryGetStringField(TEXT("document_sha256"), DocumentHash)) return 1;
	const TArray<TSharedPtr<FJsonValue>>* JsonTables = nullptr;
	if (!Document->TryGetArrayField(TEXT("tables"), JsonTables) || JsonTables->Num() != 8) return 1;
	for (const TSharedPtr<FJsonValue>& Value : *JsonTables)
	{
		const TSharedPtr<FJsonObject> Table = Value->AsObject();
		if (!Table) return 1;
		FKKExportCatalogTable Spec;
		FString Name;
		if (!Table->TryGetStringField(TEXT("name"), Name) || !ExpectedNames.Contains(Name)) return 1;
		Spec.Name = FName(*Name);
		if (Names.Contains(Spec.Name)) return 1;
		Names.Add(Spec.Name);
		if (!Table->TryGetStringArrayField(TEXT("columns"), Spec.Columns) || Spec.Columns.IsEmpty()
			|| !Table->TryGetStringField(TEXT("source_sha256"), Spec.SourceSha256)) return 1;
		const TArray<TSharedPtr<FJsonValue>>* Entries = nullptr;
		if (!Table->TryGetArrayField(TEXT("rows"), Entries)) return 1;
		TArray<FKKExportCatalogRow> Data;
		for (const TSharedPtr<FJsonValue>& EntryValue : *Entries)
		{
			const TSharedPtr<FJsonObject> Entry = EntryValue->AsObject();
			FKKExportCatalogRow Row;
			if (!Entry || !Entry->TryGetStringArrayField(TEXT("values"), Row.Values)
				|| Row.Values.Num() != Spec.Columns.Num()
				|| !Entry->TryGetStringField(TEXT("source_key"), Row.RuntimeSource)) return 1;
			Spec.RowOrder.Add(FName(*FString::Printf(TEXT("r%08d"), Data.Num())));
			Data.Add(MoveTemp(Row));
		}
		Rows.Add(MoveTemp(Data)); Tables.Add(MoveTemp(Spec));
	}
	if (FParse::Param(*Params, TEXT("VerifyOnly")))
	{
		const UKKAnimationCatalog* Saved = UKKAnimationCatalog::LoadDefaultCatalog();
		if (!Saved || Saved->SchemaVersion != 1 || Saved->Tables.Num() != Tables.Num()
			|| Saved->ReferenceMarkdown != Markdown || Saved->DocumentSha256 != DocumentHash) return 1;
		int32 VerifiedRows = 0;
		for (int32 Index = 0; Index < Tables.Num(); ++Index)
		{
			const FKKExportCatalogTable& Expected = Tables[Index];
			const FKKExportCatalogTable* Actual = Saved->FindTable(Expected.Name);
			if (!Actual || Actual->Columns != Expected.Columns || Actual->RowOrder != Expected.RowOrder
				|| Actual->SourceSha256 != Expected.SourceSha256 || !Actual->Data
				|| Actual->Data->GetRowMap().Num() != Rows[Index].Num()) return 1;
			for (int32 Row = 0; Row < Rows[Index].Num(); ++Row)
			{
				const FKKExportCatalogRow* Entry = Saved->FindRow(Expected.Name, Expected.RowOrder[Row]);
				if (!Entry || Entry->Values != Rows[Index][Row].Values
					|| Entry->RuntimeSource != Rows[Index][Row].RuntimeSource) return 1;
			}
			VerifiedRows += Rows[Index].Num();
			UE_LOG(LogTemp, Display, TEXT("KK catalog verified %s rows=%d every cell/source/order/hash"), *Expected.Name.ToString(), Rows[Index].Num());
		}
		UE_LOG(LogTemp, Display, TEXT("KK catalog verified %d total rows and Markdown after disk reload"), VerifiedRows);
		return 0;
	}
	auto Save = [](UObject* Asset) {
		UPackage* Package = Asset->GetOutermost();
		Package->MarkPackageDirty();
		FSavePackageArgs Args; Args.TopLevelFlags = RF_Public | RF_Standalone;
		const FString Filename = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename), true);
		return UPackage::SavePackage(Package, Asset, *Filename, Args);
	};
	for (int32 Index = 0; Index < Tables.Num(); ++Index)
	{
		FKKExportCatalogTable& Table = Tables[Index];
		const FString AssetName = TEXT("DT_") + Table.Name.ToString();
		const FString PackageName = TEXT("/Game/Koikatsu/Data/") + AssetName;
		UDataTable* Asset = LoadObject<UDataTable>(nullptr, *(PackageName + TEXT(".") + AssetName), nullptr, LOAD_NoWarn);
		if (!Asset)
		{
			Asset = NewObject<UDataTable>(CreatePackage(*PackageName), *AssetName, RF_Public | RF_Standalone);
			FAssetRegistryModule::AssetCreated(Asset);
		}
		if (Asset->RowStruct) Asset->EmptyTable();
		Asset->RowStruct = FKKExportCatalogRow::StaticStruct();
		for (int32 Row = 0; Row < Rows[Index].Num(); ++Row) Asset->AddRow(Table.RowOrder[Row], Rows[Index][Row]);
		Table.Data = Asset;
		if (!Save(Asset)) return 1;
		UE_LOG(LogTemp, Display, TEXT("KK catalog imported %s rows=%d sha256=%s"), *PackageName, Rows[Index].Num(), *Table.SourceSha256);
	}
	const FString CatalogPath = TEXT("/Game/Koikatsu/Data/DA_AnimationAndMouth");
	UKKAnimationCatalog* Catalog = UKKAnimationCatalog::LoadDefaultCatalog();
	if (!Catalog)
	{
		Catalog = NewObject<UKKAnimationCatalog>(CreatePackage(*CatalogPath), TEXT("DA_AnimationAndMouth"), RF_Public | RF_Standalone);
		FAssetRegistryModule::AssetCreated(Catalog);
	}
	Catalog->Tables = MoveTemp(Tables);
	Catalog->ReferenceMarkdown = MoveTemp(Markdown);
	Catalog->DocumentSha256 = MoveTemp(DocumentHash);
	Catalog->SchemaVersion = 1;
	if (!Save(Catalog)) return 1;
	UE_LOG(LogTemp, Display, TEXT("KK catalog saved %s with 8 DataTables and source document"), *CatalogPath);
	return 0;
#else
	return 1;
#endif
}
