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
	/** A sideloader mod's item (studio_mod_items.json): the mod's GUID; group, category and No are the mod's own. */
	FString Guid;
	/** A mod item's bundle (`mods/…zipmod|abdata/…`): a particle pack's name tells its effects apart from models. */
	FString Bundle;
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
	/** Who plays it: 0 either, 1 female, 2 male. */
	int32 Sex = 0;
	/** H motions: the Studio controls (option1 height, option2 breast, pattern) the variants blend along, each one's
	 *  default and sample values, and the variant files at every grid point (first control slowest). */
	TArray<FString> Controls;
	TArray<float> ControlDefaults;
	TArray<TArray<float>> ControlValues;
	TArray<FString> Variants;
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
	/** Words (lower case) a mod particle item's name or prefab carries for this effect (EffectForParticleMod), and the
	 *  scale each gives the effect (a cigarette's smoke is a wisp of the smoke effect). */
	TArray<FString> Words;
	TArray<float> WordScales;
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
	/** Eyes pattern names (cha_eyeset rows) and Studio hand pattern names per side (0 left, 1 right; the HandAnime clip). */
	TMap<int32, FString> EyeNames;
	TMap<int32, FString> HandNames[2];
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
	/** Original 32-cube Amplify table, with Unity's G row order and encoded texels preserved. */
	class UTexture* SourceFilterLut(int32 No) const;

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
	/** A mod item by its mod's GUID and slot (a KStudio scene's StudioResolveInfo), null when the library lacks it. */
	const FOKStudioCatalogItem* FindModItem(const FString& Guid, int32 Slot) const;
	/** A particle pack's item by its GUID and slot (also ones without a model: ParticleSystems only). */
	const FOKStudioCatalogItem* FindModParticle(const FString& Guid, int32 Slot) const;
	/** Mod items (ok-extract --oklib studio-mods) whose bundles are in the library; added to Items. */
	void LoadModItems(const FString& File);
	TMap<TPair<FString, int32>, int32> ModItems;
	/** Particle packs' items by (GUID, slot), with or without a converted model (FindModParticle). */
	TMap<TPair<FString, int32>, FOKStudioCatalogItem> ModParticles;
	mutable TMap<FIntVector, int32> ItemIndex;
	/** Where an item's children attach (its 子の接続先 node, item space, UE units); identity without one. Cached. */
	FTransform ItemChildPoint(int32 Group, int32 Category, int32 No) const;
    FTransform AnimatedItemChildPoint(int32 Group, int32 Category, int32 No, int32 Pattern, float NormalizedTime, float ElapsedSeconds, float Speed) const;
    /** Whether AnimatedItemChildPoint moves with time for this item and pattern. */
    bool HasChildAnimation(int32 Group, int32 Category, int32 No, int32 Pattern) const { return Pattern == 0 && ChildAnimations.Contains(FIntVector(Group, Category, No)); }
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
	/** The effect a sideloader particle item stands for: a particle or effect pack's item whose name or prefab carries an
	 *  effect's word (the longest wins); null for other items. */
	const FOKStudioEffect* EffectForParticleMod(const FOKStudioCatalogItem& Item, float* OutScale = nullptr) const;
	/** Where effects.json is. */
	static FString EffectsFile();
};
