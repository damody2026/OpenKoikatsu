#pragma once

#include "CoreMinimal.h"
#include "Serialization/MemoryReader.h"
#include "KKCardTypes.h"

KKCARD_API bool FindPngIendEnd(const TArray<uint8>& Bytes, int32& OutEnd);
KKCARD_API EKKCardKind ClassifyCard(const TArray<uint8>& Bytes);
KKCARD_API bool Read7BitInt(FArchive& Reader, int32& OutValue);
KKCARD_API bool Read7BitString(FArchive& Reader, FString& OutString);
KKCARD_API void Write7BitInt(TArray<uint8>& Out, int32 Value);
KKCARD_API void Write7BitString(TArray<uint8>& Out, const FString& Value);
KKCARD_API void WriteInt32LE(TArray<uint8>& Out, int32 Value);
KKCARD_API void WriteInt64LE(TArray<uint8>& Out, int64 Value);
KKCARD_API bool ParseCardHeader(
	const TArray<uint8>& Bytes,
	FKKCardHeader& OutHeader,
	TArray<FKKBlockInfo>& OutBlocks,
	FString& OutError);

KKCARD_API bool ParseCardHeader(
	const TArray<uint8>& Bytes,
	FKKCardHeader& OutHeader,
	TArray<FKKBlockInfo>& OutBlocks,
	TMap<FString, TArray<uint8>>& OutRawBlocks,
	FString& OutError);
