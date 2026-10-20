#include "OKMotionCatalogue.h"
#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	struct FCatalogue
	{
		TArray<FOKMotionCatalogueEntry> Entries;
		TMap<FString, int32> ByName;
	};

	const FCatalogue& Loaded()
	{
		static const FCatalogue Catalogue = []()
		{
			FCatalogue Result;
			const FString Path = FPaths::ProjectContentDir() / TEXT("IndependentResources/MotionCatalogue/MotionCatalogue.json");
			FString Text;
			TSharedPtr<FJsonObject> Root;
			if (!FFileHelper::LoadFileToString(Text, *Path)
				|| !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid())
			{
				UE_LOG(LogTemp, Warning, TEXT("Motion catalogue missing or unreadable: %s"), *Path);
				return Result;
			}
			const TArray<TSharedPtr<FJsonValue>>* Motions = nullptr;
			if (!Root->TryGetArrayField(TEXT("motions"), Motions))
			{
				return Result;
			}
			Result.Entries.Reserve(Motions->Num());
			for (const TSharedPtr<FJsonValue>& Value : *Motions)
			{
				const TSharedPtr<FJsonObject>* Item = nullptr;
				if (!Value->TryGetObject(Item))
				{
					continue;
				}
				FOKMotionCatalogueEntry Entry;
				Entry.Key = (*Item)->GetStringField(TEXT("k"));
				Entry.Zh = (*Item)->GetStringField(TEXT("z"));
				int32 Bar = INDEX_NONE;
				if (!Entry.Key.FindChar(TEXT('|'), Bar))
				{
					continue;
				}
				Entry.Name = Entry.Key.Mid(Bar + 1);
				const FString Name = Entry.Name;
				Result.ByName.Add(Name, Result.Entries.Add(MoveTemp(Entry)));
			}
			UE_LOG(LogTemp, Display, TEXT("OK motion catalogue: %d motions"), Result.Entries.Num());
			return Result;
		}();
		return Catalogue;
	}
}

const TArray<FOKMotionCatalogueEntry>& FOKMotionCatalogue::Entries()
{
	return Loaded().Entries;
}

const FString* FOKMotionCatalogue::Label(const FString& Name)
{
	const FCatalogue& C = Loaded();
	const int32* Index = C.ByName.Find(Name);
	return Index ? &C.Entries[*Index].Zh : nullptr;
}
