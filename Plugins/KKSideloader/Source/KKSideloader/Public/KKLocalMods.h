#pragma once

#include "CoreMinimal.h"
#include "KKCardTypes.h"

enum class EKKLocalModStatus : uint8 { Found, Missing, Conflict };

struct KKSIDELOADER_API FKKLocalMod
{
	FString Guid;
	FString Name;
	FString Version;
	FString Path;
};

struct KKSIDELOADER_API FKKLocalModRequirement
{
	FString Guid;
	EKKLocalModStatus Status = EKKLocalModStatus::Missing;
	TArray<FKKLocalMod> Candidates;
};

// Discovers manifests only; Found never implies that 3D conversion is supported.
class KKSIDELOADER_API FKKLocalModIndex
{
public:
	void BuildForCard(const FString& CardPath);
	TArray<FKKLocalModRequirement> Resolve(const TArray<FKKResolveInfo>& References) const;
	bool CopyFound(const TArray<FKKResolveInfo>& References, const FString& Directory, FString& Error) const;
	TMap<FString, TArray<FKKLocalMod>> ByGuid;
	TArray<FString> Diagnostics;
	// Empty for cards outside a recognized game installation.
	FString GameRoot;
	TArray<FString> ZipmodPaths;
	TArray<FString> SkippedGameMods;
};
