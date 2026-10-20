#pragma once

#include "CoreMinimal.h"

class UOKSceneDocument;

/**
 * KStudio scene import (S5): the original game's Studio scene PNG becomes an OK Studio scene. Characters are rebuilt
 * as KKS cards from the scene and converted to OK cards (installed game + zipmods); vanilla items map to the Studio
 * catalogue of the OK base library; lights, cameras, folders and the scene map carry over.
 */
struct OKSTUDIO_API FOKStudioImport
{
	struct FReport
	{
		FString Source;
		FString Version;
		int32 Characters = 0;
		int32 CharacterCards = 0;
		int32 Items = 0;
		int32 CatalogItems = 0;
		int32 Lights = 0;
		int32 Cameras = 0;
		int32 Folders = 0;
		int32 Routes = 0;
		int32 Texts = 0;
		int32 Map = -1;
		/** What did not carry over (mod items, route paths, texts, animations waiting for S6...). */
		TArray<FString> Notes;
		FString ToJson() const;
	};

	/** Imports a scene PNG into a document; character OK cards go to CardDirectory. */
	static bool Import(const FString& ScenePng, UOKSceneDocument* Document, const FString& CardDirectory, FReport& Report, FString& OutError);
	/** Imports into UserData/Studio/scene/<name>.okscene (cards in <name>_cards/, report <name>.import.json). */
	static bool ImportToFile(const FString& ScenePng, FString& OutScene, FReport& Report, FString& OutError);

	/** Unity local transform (Studio ChangeAmount: position, Euler degrees, scale) as a UE transform (cm). */
	static FTransform UnityToUe(const FVector& Position, const FVector& EulerDegrees, const FVector& Scale);
};
