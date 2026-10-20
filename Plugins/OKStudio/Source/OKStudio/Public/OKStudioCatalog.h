#pragma once

#include "CoreMinimal.h"

/** A source Animator clip baked in item-root space for the child attachment point. */
struct FOKStudioChildAnimation
{
    float Duration = 0.f;
    bool bLooping = false;
    TArray<FTransform> Frames;
};

/** One vanilla Studio item of the OK base library (`studio_items.json`, written by `ok-extract --oklib convert`). */
struct FOKStudioCatalogItem
{
	int32 Group = 0;
	int32 Category = 0;
	int32 No = 0;
	FString Name;
	/** Library model (32 hex) and prefab. */
	FString Model;
	FString Prefab;
	/** Child attach point inside the item. */
	FString Child;
	bool bAnime = false;
	bool Colors[3] = {};
	bool Patterns[3] = {};
	bool bScale = true;
	/** ItemComponent.defLineWidth (0: the item has no outline); < 0: not in the catalogue. */
	float LineWidth = -1.f;
	/** The item fades with its alpha (ItemComponent.rendAlpha). */
	bool bAlpha = false;
};

struct FOKStudioCatalogMap
{
	int32 No = 0;
	int32 Category = 0;
	FString Name;
	FString Model;
	FString Prefab;
	/** What the map shows: its scene roots (or the prefab). */
	TArray<FString> Roots;
};

struct FOKStudioCatalogGroup
{
	int32 Group = -1;
	int32 Id = 0;
	FString Name;
};

/** Studio FK bone: group 0 body .. 13, Op 0 main skeleton / 1 optional parts (hair, skirt, fingers...). */
struct FOKStudioFkBone
{
	int32 No = 0;
	FString Bone;
	FString Name;
	int32 Group = 0;
	int32 Op = 0;
};

/** Studio screen filter (Filter sheet): an Amplify Color LUT (1024x32) in a game bundle; scenes pick one by number. */
struct FOKStudioFilter
{
	int32 No = 0;
	FString Name;
	FString Bundle;
	FString File;
};

/** Studio character attach point (AccessoryPoint sheet): the transform scene children on that slot hang on. */
struct FOKStudioAccessoryPoint
{
	int32 No = 0;
	int32 Group = 0;
	FString Key;
	FString Name;
	FString Bone;
};

/** One Studio animation of the OK animation library (converted from the game's Studio lists). */
struct FOKStudioAnim
{
	int32 Kind = 0;
	int32 Group = 0;
	int32 Category = 0;
	int32 No = 0;
	FString Name;
	/** .okanim file relative to the library (empty: not converted). */
	FString File;
	float Duration = 0.f;
	bool bLooping = false;
};

/** One Niagara effect of the OK effect library (`IndependentResources/OKStudio/effects.json`). */
struct FOKStudioEffect
{
	FString Id;
	FString Name;
	FString Category;
	/** The Niagara system (object path). */
	FString Asset;
	/** Niagara emitter a missing system is made from (OKStudio.MakeEffects). */
	FString Template;
	/** Original Studio particle items (group, category, no) a KStudio import turns into this effect. */
	TArray<FIntVector> Kks;
	/** Particle colour when the scene object sets none (linear; times the intensity, into User.Color). */
	FLinearColor Color = FLinearColor::White;
};

/** Studio item, map and light lists of the OK base library. */
class OKSTUDIO_API FOKStudioCatalog
{
public:
	/** Catalogue of the active library (loaded once; Reload after a conversion). */
	static const FOKStudioCatalog& Get();
	static void Reload();

	bool bLoaded = false;
	FString Error;
	mutable TMap<FIntVector, FTransform> ChildPoints;
	TArray<FOKStudioCatalogGroup> Groups;
	/** Categories: Group is the item group. */
	TArray<FOKStudioCatalogGroup> Categories;
	TArray<FOKStudioCatalogItem> Items;
	TArray<FOKStudioCatalogGroup> MapCategories;
	TArray<FOKStudioCatalogMap> Maps;
	TArray<FOKStudioFkBone> FkBones;
	TArray<FOKStudioAccessoryPoint> AccessoryPoints;
	TArray<FOKStudioFilter> Filters;
	/** Studio animations (studio_anims.json) with their groups and categories. */
	TArray<FOKStudioAnim> Anims;
	TArray<FOKStudioCatalogGroup> AnimGroups;
	TArray<FOKStudioCatalogGroup> AnimCategories;
	const FOKStudioAnim* FindAnim(int32 Kind, int32 Group, int32 Category, int32 No) const;
	/** Any kind (KStudio scenes key motions by group, category and no only). */
	const FOKStudioAnim* FindAnimAnyKind(int32 Group, int32 Category, int32 No) const;
	void LoadAnims(const FString& File);
	const FOKStudioFkBone* FindFkBone(const FString& Bone) const;
	const FOKStudioAccessoryPoint* FindAccessoryPoint(int32 No) const;
	const FOKStudioFilter* FindFilter(int32 No) const;
	/** Filter `No` as an Unreal colour-grading LUT (256x16, converted from the Amplify Color 32x32x32 LUT); cached, null without it. */
	class UTexture* FilterLut(int32 No) const;

	/** What a character's eyes pattern (ChaFileStatus.eyesPtn, a cha_eyeset row) does (ChaControl.ChangeEyesPtn). */
	struct FEyeSet
	{
		/** The FaceBlendShape eye pattern; < 0: unknown (no eye sets in the library). */
		int32 BlendPattern = -1;
		/** The set also changes parts (gag eyes, hidden eye base or pupils): the character is built with it. */
		bool bChangesParts = false;
	};
	/** The eye set of eyes pattern `Ptn` from the OK base library's character lists; cached. */
	FEyeSet EyeSet(int32 Ptn) const;

	const FOKStudioCatalogItem* FindItem(int32 Group, int32 Category, int32 No) const;
	/** Where an item's children attach (its 子の接続先 node, item space, UE units); identity without one. Cached. */
	FTransform ItemChildPoint(int32 Group, int32 Category, int32 No) const;
    FTransform AnimatedItemChildPoint(int32 Group, int32 Category, int32 No, int32 Pattern, float NormalizedTime, float ElapsedSeconds, float Speed) const;
    void LoadChildAnimations(const FString& File);
    TMap<FIntVector, FOKStudioChildAnimation> ChildAnimations;
	const FOKStudioCatalogMap* FindMap(int32 No) const;
	TArray<const FOKStudioCatalogItem*> ItemsIn(int32 Group, int32 Category) const;
	TArray<const FOKStudioCatalogGroup*> CategoriesOf(int32 Group) const;

	bool Load(const FString& File);

	/** The OK effect library. */
	TArray<FOKStudioEffect> Effects;
	TArray<FString> EffectCategories;
	void LoadEffects(const FString& File);
	const FOKStudioEffect* FindEffect(const FString& Id) const;
	/** The effect an original Studio particle item becomes, if any. */
	const FOKStudioEffect* EffectForKks(int32 Group, int32 Category, int32 No) const;
	/** Where effects.json is. */
	static FString EffectsFile();
};
