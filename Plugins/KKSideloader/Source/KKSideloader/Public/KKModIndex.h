#pragma once

#include "CoreMinimal.h"
#include "KKCardTypes.h"

struct KKSIDELOADER_API FKKZipmod
{
	FString Guid;
	FString Path;
	FString Name;
	bool bInSideloaderModpack = false;
};

class KKSIDELOADER_API FKKModIndex
{
public:
	bool Build(const FString& ModsDir, FString& OutError);
	bool LoadCache(const FString& CachePath);
	bool SaveCache(const FString& CachePath) const;
	const FKKZipmod* Find(const FString& Guid) const;
	TArray<FKKResolveInfo> Missing(const TArray<FKKResolveInfo>& Resolves) const;
	int32 Num() const { return GuidToMod.Num(); }

	TMap<FString, FKKZipmod> GuidToMod;
	FString ModsDir;
	FString LastError;
};

bool KKSIDELOADER_API ReadZipmodGuid(const FString& ZipPath, FString& OutGuid, FString& OutName);
