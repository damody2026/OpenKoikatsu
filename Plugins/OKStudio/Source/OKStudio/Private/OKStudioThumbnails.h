#pragma once

// Thumbnails of PNG files (cards, scene cards, pose pictures) and of pictures captured in the studio: decoded on a worker
// thread, made into brushes a few per frame on the game thread, kept in an LRU cache.
#include "CoreMinimal.h"

struct FSlateBrush;
struct FSlateDynamicImageBrush;

class FOKStudioThumbnails
{
public:
	static FOKStudioThumbnails& Get();
	/** The thumbnail of a PNG at most Size pixels on its long side (null until it is decoded; asks for it). */
	const FSlateBrush* File(const FString& Path, int32 Size = 128);
	/** A captured picture (BGRA rows, Width x Height) kept under Key. */
	void Put(const FString& Key, const TArray<FColor>& Pixels, int32 Width, int32 Height, int32 Size = 256);
	const FSlateBrush* Find(const FString& Key) const;
	/** Drops a file's thumbnail (it changed). */
	void Forget(const FString& Key);
	/** Makes decoded thumbnails into brushes (at most a few per call). */
	void Tick();
	int32 Pending() const;

private:
	struct FDecoded { FString Key; TArray<uint8> Bgra; int32 Width = 0, Height = 0; };
	struct FEntry { TSharedPtr<FSlateDynamicImageBrush> Brush; uint64 Used = 0; };
	TMap<FString, FEntry> Cache;
	TSet<FString> Requested;
	FCriticalSection Lock;
	TArray<FDecoded> Ready;
	TAtomic<int32> InFlight{ 0 };
	uint64 Clock = 0;
	void Make(FDecoded&& D);
	static void Shrink(const TArray<FColor>& In, int32 W, int32 H, int32 Size, FDecoded& Out);
};
