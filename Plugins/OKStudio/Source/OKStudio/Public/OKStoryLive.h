#pragma once

#include "CoreMinimal.h"

class FJsonObject;

/**
 * The live presenter of ok_story.dll (Tools/ok-story/src/live.rs): galgame presentation of conversations whose words
 * come from the game. The game sends what happens (Input: conversation, line, reveal, end) and the facts each frame;
 * Tick answers the frame to draw (dialog, portrait, camera, probes) — what an SOKStoryView shows.
 */
class OKSTUDIO_API FOKStoryLive
{
public:
	~FOKStoryLive();
	/** Loads ok_story.dll (OK_STORY_DLL, next to the executable, or Tools/ok-story/target/release). */
	bool Open(FString& OutError);
	bool IsOpen() const { return Handle != nullptr; }
	void Close();
	void Input(const FString& Json);
	/** One frame: the facts as JSON in, the frame out (null on failure, see OutError). */
	TSharedPtr<FJsonObject> Tick(const FString& FactsJson, FString& OutError);
	FString State() const;
	FString DllPath() const { return Path; }

	struct FApi;

private:
	FApi* Api = nullptr;
	void* Handle = nullptr;
	FString Path;
};
