#pragma once

// OK Studio's own settings (Saved/OKStudio/settings.json) and its favourites / hidden / recent lists (favorites.json).
#include "CoreMinimal.h"
#include "InputCoreTypes.h"

struct FKeyEvent;

struct FOKStudioSettings
{
	static FOKStudioSettings& Get();

	/** Camera: orbit degrees per pixel, pan and zoom speed, free-fly speed (cm/s) and whether right-drag flies. */
	float OrbitSpeed = 0.3f;
	float PanSpeed = 1.f;
	float ZoomSpeed = 1.f;
	float FlySpeed = 300.f;
	bool bFlyMode = false;
	/** Guide handle size (times the default) and snapping steps: move (cm), rotate (degrees), scale. */
	float GizmoScale = 1.f;
	float SnapMove = 10.f;
	float SnapRotate = 15.f;
	float SnapScale = 0.1f;
	/** Auto backup: minutes between backups (0: off) and how many to keep. */
	int32 BackupMinutes = 5;
	int32 BackupKeep = 10;
	/** Card folders the card browser lists besides Content/CharacterCards and the game's UserData/chara. */
	TArray<FString> CardRoots;
	/** UI scale (times the HS2Studio6 canvas scale). */
	float UiScale = 1.f;

	/** Shortcuts: command id -> chords ("Ctrl+Shift+Z", several separated by ", "); missing ids use the defaults. */
	TMap<FString, FString> Bindings;
	static const TArray<TPair<FString, FString>>& DefaultBindings();
	/** The chords of a command (the user's or the default). */
	FString BindingOf(const FString& Id) const;
	/** The command bound to a key event (empty: none). */
	FString CommandFor(const FKeyEvent& Event) const;
	/** Chord text of a key event ("Ctrl+K"). */
	static FString ChordText(const FKeyEvent& Event);
	/** Commands that share a chord with another command (conflicts), as "chord: id, id". */
	TArray<FString> Conflicts() const;

	/** Favourites, hidden entries and the most recent ones (keys like "item:3/18/188", "anim:0/1/2/3"). */
	TSet<FString> Favorites, Hidden;
	TArray<FString> Recent;
	void UseRecent(const FString& Key);
	/** Scenes opened or saved lately (newest first, at most 10; HS2Studio6 最近開啟). */
	TArray<FString> RecentScenes;
	void UseScene(const FString& Path);
	/** Colours picked lately (newest first). */
	TArray<FLinearColor> RecentColors;
	void UseColor(const FLinearColor& Color);

	static FString Directory();
	void Load();
	void Save() const;
	void SaveFavorites() const;
	/** Back to the defaults (the shortcuts too). */
	void Reset();

private:
	bool bLoaded = false;
};
