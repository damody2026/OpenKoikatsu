#pragma once

#include "CoreMinimal.h"
#include "OKSceneDocument.h"

/**
 * KK Studio pose files (`UserData/Studio/pose/*.dat`, the game's PauseCtrl format; read and written by ok-extract).
 * FK bones are keyed by the Studio FK bone list of the base library catalogue.
 */
struct OKSTUDIO_API FOKStudioPose
{
	/** UserData/Studio/pose of this project, and its pose files newest first. */
	static FString Directory();
	static TArray<FString> Files();

	/** Applies a pose file to a character: its FK bones of the enabled FK parts (FK off: none). */
	static bool Load(const FString& Path, FOKSceneCharacter& Character, FString& OutError, FString* OutName = nullptr);
	/** FK part (activeFK index: hair, neck, breast, body, right hand, left hand, skirt) of a Studio Bone sheet group
	 *  (Studio.FKCtrl); pose files and scenes switch FK on per part. */
	static int32 FkPart(int32 Group);

	/** Saves the character's FK bones as a KK Studio pose file. */
	static bool Save(const FString& Path, const FOKSceneCharacter& Character, const FString& Name, FString& OutError);
};
