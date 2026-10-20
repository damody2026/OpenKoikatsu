#pragma once

#include "CoreMinimal.h"

// One list of every solo motion, with its Traditional Chinese label, read from
// Content/IndependentResources/MotionCatalogue/MotionCatalogue.json (made by `ok-extract motion-catalogue`).
struct FOKMotionCatalogueEntry
{
	FString Key;   // "<source>|<name>": what the animation loader takes
	FString Name;  // original animator state name, unique across the list
	FString Zh;    // Chinese display label
};

class OPENKOIKATSU_API FOKMotionCatalogue
{
public:
	static const TArray<FOKMotionCatalogueEntry>& Entries();
	// Chinese label for an original motion name, or null when the name is not in the catalogue.
	static const FString* Label(const FString& Name);
};
