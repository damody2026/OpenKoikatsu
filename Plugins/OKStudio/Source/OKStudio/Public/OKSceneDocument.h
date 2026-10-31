#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "OKSceneDocument.generated.h"

/** Where a character's eyes or neck look. */
UENUM(BlueprintType)
enum class EOKSceneLook : uint8
{
	/** The animation's eyes / neck. */
	Animation,
	/** Follow the view. */
	Camera,
	/** Look at the character's look point. */
	Point,
    /** The neck locals saved with the imported pose. */
    Fixed,
	/** Straight ahead of the character (HS2Studio6 正面). */
	Front,
	/** Away from the view: the view mirrored through the head (HS2Studio6 背離). */
	Away,
};

/** How a timeline key blends into the next one. */
UENUM(BlueprintType)
enum class EOKSceneEase : uint8
{
	Linear,
	EaseInOut,
	EaseIn,
	EaseOut,
	/** Holds this key until the next. */
	Step,
};

/** What a scene object is. */
UENUM(BlueprintType)
enum class EOKSceneObjectKind : uint8
{
	Folder,
	Character,
	/** A model of the OK base library (a prop, an accessory, a studio item). */
	Item,
	Light,
	Camera,
	/** A Niagara effect of the OK effect library (effects.json). */
	Effect,
};

UENUM(BlueprintType)
enum class EOKSceneLightType : uint8
{
	Directional,
	Point,
	Spot,
	/** A rectangle that emits (HS2Studio6 面光源); like Point and Spot it lights the maps and Standard parts, not the toon
	 *  characters and items, which take one directional light. */
	Area,
};

/** A character: an OK card (or an original KKS card) with its outfit, animation and state. */
/** One FK bone of a character: its local rotation in Unity Euler degrees (as KK Studio and its pose files store it). */
USTRUCT(BlueprintType)
struct OKSTUDIO_API FOKSceneFkBone
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pose")
	FString Bone;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pose")
	FVector Euler = FVector::ZeroVector;

	bool operator==(const FOKSceneFkBone& O) const = default;
};

/** A Studio IK target: position in character-local cm and optional native hand/foot orientation. */
USTRUCT(BlueprintType)
struct OKSTUDIO_API FOKSceneIkTarget
{
	GENERATED_BODY()

	/** 0 body, 1 left shoulder, 2 right shoulder, 3 left thigh, 4 right thigh, 5 left hand, 6 right hand, 7 left foot, 8 right foot;
	 *  bend goals 9 left elbow, 10 right elbow, 11 left knee, 12 right knee. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pose")
	int32 Effector = 5;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pose")
	FVector Position = FVector::ZeroVector;

    /** Native hand/foot IK target orientation, in Unity Euler degrees. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pose")
    bool bUseRotation = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pose")
    FVector Euler = FVector::ZeroVector;

	/** How strongly the effector pulls, 0..1 (HS2Studio6 IK 權重; 1 reaches the target). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pose", meta = (UIMin = "0", UIMax = "1"))
	float Weight = 1.f;

	bool operator==(const FOKSceneIkTarget& O) const = default;
};

/** One Studio control of the character's H motion by name: option1 (height), option2 (breast) or pattern (KStudio's
 *  animeOptionParam1/2 and animePattern, HS2Studio6's 輔助 1/2). */
USTRUCT(BlueprintType)
struct OKSTUDIO_API FOKSceneAnimFloat
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation")
	FString Name;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation")
	float Value = 0.f;

	bool operator==(const FOKSceneAnimFloat& O) const = default;
};

USTRUCT(BlueprintType)
struct OKSTUDIO_API FOKSceneCharacter
{
	GENERATED_BODY()

    /** Saved FIX locals from NeckLookControllerVer2: source aBones order (neck, head), Unity quaternions. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pose")
    TArray<FQuat> FixedNeck;


	/** Card PNG: absolute, or relative to the project directory. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Character")
	FString Card;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Character", meta = (ClampMin = "0", ClampMax = "6"))
	int32 Outfit = 0;

	/** OK animation asset (object path); empty: the neutral pose. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Character", meta = (AllowedClasses = "/Script/OKAnimation.OKAnimationAsset"))
	FSoftObjectPath Animation;

	/** An .okanim file (a video motion of Saved/VideoMotions, an exported timeline, ...): absolute or relative to the project.
	 *  Played when no Studio animation is set. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Character")
	FString AnimFile;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Character", meta = (ClampMin = "0", UIMax = "4"))
	float Speed = 1.f;

	/** Seconds into the animation when the scene opens. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Character", meta = (ClampMin = "0"))
	float Time = 0.f;

	/** Plays the motion over and over even when it does not loop by itself (KStudio's forceLoop). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Character")
	bool bForceLoop = false;

	/** The Studio controls set for the motion (the rest keep the controller's own values). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Character")
	TArray<FOKSceneAnimFloat> AnimFloats;

	/** Clothing state per part (top, bottom, bra, shorts, gloves, pantyhose, socks, inner shoes, outer shoes): 0 on, 1 half, 3 off. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Character", EditFixedSize)
	TArray<uint8> ClothesStates = { 0, 0, 0, 0, 0, 0, 0, 0, 0 };

	/** Face: eyebrow, eye and mouth patterns (-1 keeps the card's). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face")
	int32 EyebrowPattern = -1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face")
	int32 EyesPattern = -1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face")
	int32 MouthPattern = -1;

	/** Mouth opening 0..1 (-1 keeps the animation's). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face", meta = (UIMin = "-1", UIMax = "1"))
	float MouthOpen = -1.f;

	/** Eye opening 0..1 (-1 keeps the character's; HS2Studio6 眼睛開合). Blinking still closes them from there. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face", meta = (UIMin = "-1", UIMax = "1"))
	float EyesOpen = -1.f;

	/** How far the neck turns towards its look target, 0..1 (HS2Studio6 跟隨強度; 1 fully). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pose", meta = (UIMin = "0", UIMax = "1"))
	float NeckFollow = 1.f;

	/** Automatic blinking: -1 as the character does, 0 off, 1 on. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face")
	int32 Blink = -1;

	/** Where the eyes look. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face")
	EOKSceneLook EyesLook = EOKSceneLook::Camera;

	/** EyeLookCalc.SaveAngle: QL, QR, H0,H1,V0,V1, in source units. Empty in legacy OK scenes. */
	UPROPERTY()
	TArray<float> FixedEyes;

	UPROPERTY()
	int32 FixedEyesPattern = -1;

	/** Studio animation of the OK animation library (studio_anims.json): kind 0 Studio motion, 1 H motion; No < 0: none
	 *  (Animation, an OK animation asset, is used instead). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation")
	int32 StudioAnimeKind = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation")
	int32 StudioAnimeGroup = -1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation")
	int32 StudioAnimeCategory = -1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation")
	int32 StudioAnimeNo = -1;

	/** Where the neck turns the head. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pose")
	EOKSceneLook NeckLook = EOKSceneLook::Animation;

	/** The point eyes and neck look at in Point mode (cm, the character's space; default: ahead at eye height). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pose")
	FVector LookPoint = FVector(0.f, 100.f, 140.f);

	/** Hand shapes (the game's finger patterns; -1: the animation's fingers). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pose")
	int32 HandLeft = -1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pose")
	int32 HandRight = -1;

	/** Studio FK: these bones keep these local rotations over the animation (or the neutral pose). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pose")
	TArray<FOKSceneFkBone> Fk;

	/** Studio IK: these effectors reach these positions (solved after FK and the card's bone modifiers). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pose")
	TArray<FOKSceneIkTarget> Ik;

	/** Parts whose FK is switched off (bit per FkPartOf part: body, chest, hair, neck, left hand, right hand, skirt): their
	 *  bones keep their values but follow the animation (HS2Studio6 部位開關). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pose")
	int32 FkPartsOff = 0;

	/** Parts whose IK is switched off (bit per IkPartOf part: body, left arm, right arm, left leg, right leg). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pose")
	int32 IkPartsOff = 0;

	/** The FK part of a bone (0 body, 1 chest, 2 hair, 3 neck, 4 left hand, 5 right hand, 6 skirt) and the IK part of an effector. */
	static int32 FkPartOf(const FString& Bone);
	static int32 IkPartOf(int32 Effector);
	bool FkPartOn(int32 Part) const { return !(FkPartsOff & (1 << Part)); }
	bool IkPartOn(int32 Part) const { return !(IkPartsOff & (1 << Part)); }

	bool operator==(const FOKSceneCharacter& O) const = default;
	/** Same built character: only the card, outfit, clothes and animation need a rebuild; face, pose and look are
	 *  applied to the shown character. */
	/** Character state (the card's ChaFileStatus; -1 / empty: the card's own). Changing it rebuilds the character. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "State")
	int32 TearsLevel = -1;

	/** Blush (hohoAkaRate 0..1). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "State")
	float Blush = -1.f;

	/** Nipple stand (nipStandRate 0..1). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "State")
	float NipStand = -1.f;

	/** Skin gloss (skinTuyaRate 0..1). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "State")
	float SkinGloss = -1.f;

	/** Fluids per part (siruLv: face, chest front, chest back, lower front, lower back; 0..2; empty: the card's). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "State")
	TArray<uint8> Fluids;

	/** Accessory slots shown (20, then one value for every extra MoreAccessories slot; empty: the card's). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "State")
	TArray<uint8> AccessoriesShown;

	/** Invisible Body (KK_InvisibleBody): -1 the card's, 0 shown, 1 only clothes and accessories drawn. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "State")
	int32 InvisibleBody = -1;

	bool SameState(const FOKSceneCharacter& O) const
	{
		return TearsLevel == O.TearsLevel && Blush == O.Blush && NipStand == O.NipStand && SkinGloss == O.SkinGloss && Fluids == O.Fluids && AccessoriesShown == O.AccessoriesShown
			&& InvisibleBody == O.InvisibleBody;
	}

	bool SameContent(const FOKSceneCharacter& O) const
	{
		return Card == O.Card && Outfit == O.Outfit && ClothesStates == O.ClothesStates && Animation == O.Animation && AnimFile == O.AnimFile && SameState(O)
			&& StudioAnimeKind == O.StudioAnimeKind && StudioAnimeGroup == O.StudioAnimeGroup && StudioAnimeCategory == O.StudioAnimeCategory && StudioAnimeNo == O.StudioAnimeNo;
	}
};

/** One of a Studio item's three pattern slots (OIItemInfo.pattern): a mask from the character list's patterns. */
USTRUCT(BlueprintType)
struct OKSTUDIO_API FOKSceneItemPattern
{
	GENERATED_BODY()

	/** Pattern id (character list mt_pattern, as Studio's pattern list); 0: none. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pattern")
	int32 Pattern = 0;

	/** Pattern colour (sRGB; alpha blends the pattern in). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pattern")
	FLinearColor Color = FLinearColor(100.f / 255.f, 100.f / 255.f, 100.f / 255.f, 1.f);

	/** UV offset (X, Y) and scale (Z, W). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pattern")
	FVector4 Uv = FVector4(0., 0., 1., 1.);

	/** Rotation (× π radians, about the UV centre). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pattern")
	float Rotation = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pattern")
	bool bClamp = false;

	bool operator==(const FOKSceneItemPattern& O) const = default;
};

/** What a Material Editor edit changes. */
UENUM(BlueprintType)
enum class EOKSceneMaterialEditKind : uint8
{
	Float,		// Value.R
	Color,		// Value, sRGB as authored
	Texture,	// Text: the PNG, base64
	TextureST,	// the texture's scale (R, G) and offset (B, A)
	Shader,		// Text: the shader the material switches to; Value.R: its render queue (-1: unchanged)
	Renderer,	// Material names the renderer (GameObject); Property: Enabled / ShadowCastingMode; Value.R
};

/** A Material Editor style edit of an item's material or renderer (Material Editor's scene data). */
USTRUCT(BlueprintType)
struct OKSTUDIO_API FOKSceneMaterialEdit
{
	GENERATED_BODY()

	/** Source material name (without " (Instance)"); empty: every material of the item. The renderer's name for
	 *  Renderer edits. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Material")
	FString Material;

	/** Shader property, as Unity names it (`_Color`, `_Glossiness` ...). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Material")
	FString Property;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Material")
	EOKSceneMaterialEditKind Kind = EOKSceneMaterialEditKind::Float;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Material")
	FLinearColor Value = FLinearColor::White;

	/** The texture (base64 PNG) or the shader name. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Material")
	FString Text;

	bool operator==(const FOKSceneMaterialEdit& O) const = default;
};

/** A model of the OK base library, shown like a character part. */
USTRUCT(BlueprintType)
struct OKSTUDIO_API FOKSceneItem
{
	GENERATED_BODY()

	/** Model id (32 hex) in the OK base library. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item")
	FString Model;

	/** Prefab of the model; empty: its first prefab. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item")
	FString Prefab;

	/** Studio catalogue entry (group, category, no as in the game's item lists); No < 0: Model / Prefab directly. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item")
	int32 Group = -1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item")
	int32 Category = -1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item")
	int32 No = -1;

    /** Source Animator state, normalized start time and playback rate. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item")
    int32 AnimePattern = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item")
    float AnimeTime = 0.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item")
    float AnimeSpeed = 1.f;

	/** Item colours 1..4 (sRGB); empty or alpha < 0: the model colour. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item")
	TArray<FLinearColor> Colors;

	/** Item shadow colour (sRGB; Studio's item 影色, the item_draw material's _ShadowColor); alpha < 0: the model's. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item")
	FLinearColor ShadowColor = FLinearColor(1.f, 1.f, 1.f, -1.f);

	/** Item alpha (Studio 透明度; fades the parts the item offers fading for). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item", meta = (ClampMin = "0", ClampMax = "1"))
	float Alpha = 1.f;

	/** Outline width scale (Studio 線の太さ, times the item's own width; 0: no outline). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item", meta = (ClampMin = "0"))
	float LineWidth = 1.f;

	/** Studio's light cancel (OIItemInfo.lightCancel; ItemComponent writes it to the materials' _LightCancel: how much of
	 *  the unlit colour shows); < 0: the model's. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item")
	float LightCancel = -1.f;

	/** Studio's item emission colour / power and outline colour (OIItemInfo), kept for the scene; alpha / value < 0: the
	 *  model's. Not written to the materials yet (the game writes them only to the materials an item flags). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item")
	FLinearColor EmissionColor = FLinearColor(1.f, 1.f, 1.f, -1.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item")
	float EmissionPower = -1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item")
	FLinearColor LineColor = FLinearColor(1.f, 1.f, 1.f, -1.f);

	/** Studio item FK: the item's bones and their local rotation (Unity Euler degrees); changing them rebuilds the item. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item")
	TArray<FOKSceneFkBone> Fk;

	/** Pattern slots 1..3 (empty: none); changing them rebuilds the item (the material takes them when it is made). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item")
	TArray<FOKSceneItemPattern> Patterns;

	/** Material property edits (Material Editor); changing them rebuilds the item (the materials take them when made). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item")
	TArray<FOKSceneMaterialEdit> MaterialEdits;

	bool operator==(const FOKSceneItem& O) const = default;
};

USTRUCT(BlueprintType)
struct OKSTUDIO_API FOKSceneLight
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Light")
	EOKSceneLightType Type = EOKSceneLightType::Point;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Light")
	FLinearColor Color = FLinearColor::White;

	/** Directional: lux; point and spot: candela. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Light", meta = (ClampMin = "0"))
	float Intensity = 8.f;

	/** Point and spot: attenuation radius (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Light", meta = (ClampMin = "1"))
	float Range = 1000.f;

	/** Spot: outer cone angle (degrees). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Light", meta = (ClampMin = "1", ClampMax = "80"))
	float SpotAngle = 30.f;

	/** Area (HS2Studio6 面光源): the emitting rectangle's width and height (cm), facing the light's forward. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Light", meta = (ClampMin = "1"))
	float AreaWidth = 100.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Light", meta = (ClampMin = "1"))
	float AreaHeight = 100.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Light")
	bool bShadows = true;

	/** Lights the characters and items only, not the map (Studio's character light). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Light")
	bool bCharactersOnly = false;

	bool operator==(const FOKSceneLight& O) const = default;
};

USTRUCT(BlueprintType)
struct OKSTUDIO_API FOKSceneCamera
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera", meta = (ClampMin = "5", ClampMax = "170"))
	float Fov = 40.f;

	/** Near clipping plane (cm; <= 0: the engine's). Unreal draws perspective views without a far plane: Far is kept for
	 *  the scene file and the UI, not applied. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
	float Near = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
	float Far = 0.f;

	/** Orthographic view (HS2Studio6 正交): OrthoWidth is how wide the view is (cm); there Near and Far both apply. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
	bool bOrtho = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera", meta = (ClampMin = "1"))
	float OrthoWidth = 400.f;

	bool operator==(const FOKSceneCamera& O) const = default;
};

/** One node of the scene tree. Its transform is relative to its parent (the scene root for Parent = -1). */
USTRUCT(BlueprintType)
struct OKSTUDIO_API FOKSceneEffect
{
	GENERATED_BODY()

	/** Effect id in the OK effect library (effects.json). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Effect")
	FString Effect;

	/** Playback speed (time dilation). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Effect")
	float Speed = 1.f;

	/** User.Intensity of systems that expose it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Effect")
	float Intensity = 1.f;

	/** User.Color of systems that expose it (alpha < 0: the effect's own colour). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Effect")
	FLinearColor Color = FLinearColor(1.f, 1.f, 1.f, -1.f);

	bool operator==(const FOKSceneEffect& O) const = default;
};

USTRUCT(BlueprintType)
struct OKSTUDIO_API FOKSceneObject
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Object")
	int32 Id = 0;

	/** Id of the parent object, -1 for the scene root. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Object")
	int32 Parent = -1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Object")
	FString Name;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Object")
	EOKSceneObjectKind Kind = EOKSceneObjectKind::Folder;

	/** Location (cm), rotation and scale relative to the parent. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Object")
	FTransform Transform;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Object")
	bool bVisible = true;

	/** Locked: clicks in the view pass through it (it is still selectable in the tree). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Object")
	bool bLocked = false;

	/** Under a character: the bone (Studio accessory point, e.g. a_n_hand_R) the object hangs on and follows as the character
	 * moves; empty: the character root. Transform is then relative to that bone. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Object")
	FString AttachBone;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Character", meta = (EditCondition = "Kind == EOKSceneObjectKind::Character", EditConditionHides))
	FOKSceneCharacter Character;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item", meta = (EditCondition = "Kind == EOKSceneObjectKind::Item", EditConditionHides))
	FOKSceneItem Item;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Light", meta = (EditCondition = "Kind == EOKSceneObjectKind::Light", EditConditionHides))
	FOKSceneLight Light;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera", meta = (EditCondition = "Kind == EOKSceneObjectKind::Camera", EditConditionHides))
	FOKSceneCamera Camera;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Effect", meta = (EditCondition = "Kind == EOKSceneObjectKind::Effect", EditConditionHides))
	FOKSceneEffect Effect;
};

DECLARE_MULTICAST_DELEGATE(FOKSceneChanged);

/** Scene look: post-processing of the view (Studio's bloom, depth of field, vignette and an exposure bias). */
USTRUCT(BlueprintType)
struct OKSTUDIO_API FOKScenePost
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Post")
	bool bBloom = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Post")
	float BloomIntensity = 0.675f;

	/** Bloom threshold (display value 0..1, Studio's bloom threshold): only brighter pixels glow; < 0: every pixel. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Post")
	float BloomThreshold = -1.f;

	/** Bloom size scale (Studio's bloom blur / 4). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Post", meta = (ClampMin = "0.1", ClampMax = "8"))
	float BloomSize = 1.f;

	/** Studio's ambient shadow colour (sRGB, SceneInfo.ambientShadow; default 128 grey): the items' toon shade. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Post")
	FLinearColor AmbientShadow = FLinearColor(128.f / 255.f, 128.f / 255.f, 128.f / 255.f, 1.f);

	/** Studio's ambient shadow strength (SceneInfo.ambientShadowG, 0..1); < 0: the shader's own. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Post")
	float AmbientShadowG = -1.f;

	/** Studio's outline darkness (SceneInfo.lineColorG, 0..1); < 0: the shader's own. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Post")
	float LineColorG = -1.f;

	/** Studio's outline width (SceneInfo.lineWidthG); < 0: the shader's own. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Post")
	float LineWidthG = -1.f;

	/** Studio's toon ramp (SceneInfo.rampG, an mt_ramp list number): Studio sets it as the global _RampG of every
	 * character and item; < 0: each card's own (the character display ramp). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Post")
	int32 RampG = -1;

	/** SceneInfo.shaderType: 0 selects Koikatsu, nonzero Koikano; < 0 preserves the card setting. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Post")
	int32 ShaderType = -1;

	/** Studio's shader globals for these settings (SystemButtonCtrl.SetAmbientShadow / SetLineColor / SetLineWidth). */
	TMap<FName, FLinearColor> ShaderGlobals() const;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Post")
	bool bDepthOfField = false;

	/** Focus distance (cm) and aperture (f-stop). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Post")
	float FocalDistance = 300.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Post")
	float Aperture = 2.8f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Post")
	bool bVignette = false;

	/** Unreal's own vignette strength: only while the source grading pass is not ready (a fallback). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Post")
	float VignetteIntensity = 0.4f;

	/** Studio's vignette (VignetteAndChromaticAberration.intensity; Studio only toggles the component, whose value is
	 * 0.1), applied by the source grading pass with the component's formula. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Post", meta = (ClampMin = "0", ClampMax = "0.9"))
	float SourceVignetteIntensity = 0.1f;

	/** Exposure compensation (EV). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Post")
	float Exposure = 0.f;

	/** Studio screen filter (Filter list number, -1: none): an Amplify Color LUT as colour grading, at this strength
	 * (KStudio scenes: 1 - aceBlend). Like Studio, the rest of the strength is filter 0 (Lut_Amount), Studio's
	 * blend target: strength 0 is Lut_Amount alone, not an ungraded picture. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Post")
	int32 ColorFilter = -1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Post", meta = (ClampMin = "0", ClampMax = "1"))
	float ColorFilterIntensity = 1.f;

	/** Background image file (PNG) shown behind the scene, filling the view (Studio's background; empty: none). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Post")
	FString Background;

	/** Frame image file (PNG with alpha) laid over the whole view (Studio's frame; empty: none). Like Background, a path
	 * relative to the OK base library (the game's own pictures) or a file of the scene. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Post")
	FString Frame;

	/** Solid colour behind the scene when there is no Background picture (sRGB; alpha < 0: none). Studio clears to
	 * light blue when a scene has no map, background or sky. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Post")
	FLinearColor BackgroundColor = FLinearColor(0.f, 0.f, 0.f, -1.f);

	/** Studio's sky (SceneInfo.skyInfo): a sky dome drawn behind everything with the StudioSky pattern SkyPattern
	 * (0 clear sky, 1 day, 2 evening, 3 night in the game's list; the OK base library's studio/sky.json). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Post")
	bool bSky = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Post")
	int32 SkyPattern = 0;

	/** The map's time of day (SceneInfo.sunLightType: 0 morning, 1 day, 2 evening, 3 night). A map with its own sky
	 * (MapSkyInfo, the OK base library's studio/sky.json maps) shows that time's sky instead of SkyPattern. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Post")
	int32 SunType = 0;

	/** Studio's fog (SceneInfo.enableFog, fogColor sRGB): Unity's own exponential-squared fog of everything drawn
	 * (RenderSettings of the Studio scene: Exp2, density 0.01); the sky and the background stay clear. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Post")
	bool bFog = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Post")
	FLinearColor FogColor = FLinearColor(0.9f, 0.9f, 0.9f, 1.f);

	/** Studio's ambient occlusion (SceneInfo.enableAOE: Amplify Occlusion, raw shader Tint and Radius in metres). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Post")
	bool bAmbientOcclusion = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Post")
	FLinearColor AmbientOcclusionTint = FLinearColor(0.7058824f, 0.7058824f, 0.7058824f, 1.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Post")
	float AmbientOcclusionRadius = 0.1f;

	/** Studio's sun shafts (SceneInfo.enableSunShafts: Standard Assets SunShafts from the view centre, sunColor and
	 * sunThreshold as the game's colours). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Post")
	bool bSunShafts = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Post")
	FLinearColor SunShaftColor = FLinearColor(1.f, 1.f, 1.f, 1.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Post")
	FLinearColor SunShaftThreshold = FLinearColor(0.87f, 0.74f, 0.65f, 1.f);

	/** Writes these settings (only what is switched on) over a post-process block. */
	void ApplyTo(struct FPostProcessSettings& Settings) const;
};

/** One timeline key of an object: its transform (relative to its parent) and its value (light intensity, camera FOV). */
USTRUCT(BlueprintType)
struct OKSTUDIO_API FOKSceneKey
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Timeline")
	float Time = 0.f;

	/** The curve from this key to the next. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Timeline")
	EOKSceneEase Ease = EOKSceneEase::Linear;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Timeline")
	FTransform Transform;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Timeline")
	float Value = 0.f;

	/** A character's FK bones at this key (interpolated per bone between keys). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Timeline")
	TArray<FOKSceneFkBone> Fk;

	/** A character's face, hands and clothes at this key: patterns switch at the key, the mouth opening blends. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Timeline")
	bool bCharacterState = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Timeline")
	int32 EyebrowPattern = -1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Timeline")
	int32 EyesPattern = -1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Timeline")
	int32 MouthPattern = -1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Timeline")
	float MouthOpen = -1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Timeline")
	int32 HandLeft = -1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Timeline")
	int32 HandRight = -1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Timeline")
	TArray<uint8> ClothesStates;

	/** A light's colour at this key (alpha < 0: not keyed). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Timeline")
	FLinearColor Color = FLinearColor(1.f, 1.f, 1.f, -1.f);

	/** A character's animation time at this key (seconds; < 0: the animation runs on its own). Between two keyed times the
	 *  animation is scrubbed, which also holds a pose still. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Timeline")
	float AnimTime = -1.f;

	/** The scene track (ObjectId SceneTrackId): exposure (EV) and depth of field focus distance (cm) at this key. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Timeline")
	float Exposure = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Timeline")
	float FocalDistance = 300.f;
};

/** One of the scene's ten view bookmarks (Ctrl+digit stores the view, the digit returns to it). */
USTRUCT(BlueprintType)
struct OKSTUDIO_API FOKSceneBookmark
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "View")
	bool bSet = false;

	/** Orbit view: focus (cm), yaw, pitch (degrees), distance (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "View")
	FVector Focus = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "View")
	float Yaw = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "View")
	float Pitch = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "View")
	float Distance = 380.f;

	/** The scene camera in use (-1: the free view). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "View")
	int32 Camera = -1;
};

/** Timeline keys of one object, in time order. */
USTRUCT(BlueprintType)
struct OKSTUDIO_API FOKSceneTrack
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Timeline")
	int32 ObjectId = -1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Timeline")
	TArray<FOKSceneKey> Keys;

	/** The position follows a smooth curve through the keys (Catmull-Rom; a camera path) instead of straight segments. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Timeline")
	bool bSmoothPath = false;
};

/**
 * The scene object tree shared by the in-game studio and the Unreal editor studio, stored as a .okscene file (JSON).
 * Objects reference OK cards and OK base library models only.
 */
UCLASS(BlueprintType)
class OKSTUDIO_API UOKSceneDocument : public UObject
{
	GENERATED_BODY()

public:
	static constexpr int32 FormatVersion = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scene")
	TArray<FOKSceneObject> Objects;

	/** Camera object the scene is viewed through (-1: the free camera). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scene")
	int32 ActiveCamera = -1;

	/** Studio map of the scene (catalogue no; -1: none). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scene")
	int32 Map = -1;

	/** Where the map stands in the scene (Studio's map position / rotation, SceneInfo.caMap; MapCtrl.Reflect). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scene")
	FTransform MapTransform;

	/** Post-processing of the scene's view. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scene")
	FOKScenePost Post;

	/** Timeline: keyframe tracks of objects and the length (seconds). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Timeline")
	TArray<FOKSceneTrack> Tracks;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Timeline")
	float Duration = 10.f;

	/** Track id of the scene's own track (post-processing values). */
	static constexpr int32 SceneTrackId = -2;

	/** View bookmarks 0..9. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scene")
	TArray<FOKSceneBookmark> Bookmarks;

	/** Keys the object's current transform and value at Time (replacing a key within 1/60 s). Id SceneTrackId keys the
	 *  scene's exposure and focus distance. */
	void SetKey(int32 Id, float Time);
	/** Moves a key to another time (merging with a key already there); returns its new index. */
	int32 MoveKey(int32 Id, int32 Index, float NewTime);
	/** Removes the object's key within 1/60 s of Time. */
	bool RemoveKey(int32 Id, float Time);
	/** One FK bone of a character keyed at Time (HS2Studio6 bone key): a key already there gets only this bone; a new key
	 *  holds only this bone (no transform or state change: it takes the track's values at Time). */
	void SetBoneKey(int32 Id, float Time, const FString& Bone);
	/** Drops one bone from the key at Time (the key stays for its other values). */
	bool RemoveBoneKey(int32 Id, float Time, const FString& Bone);
	/** Removes the object's whole track (清空軌道). */
	bool ClearTrack(int32 Id);
	/** The key of the object at the time (within the key tolerance), or null. */
	FOKSceneKey* KeyAt(int32 Id, float Time);
	/** The eased blend (0..1) between two keys at fraction U of the way. */
	static float EaseAlpha(EOKSceneEase Ease, float U);
	/** Poses every keyed object at Time (between keys: linear position and scale, slerped rotation). */
	void EvaluateTimeline(float Time);
	const FOKSceneTrack* TrackOf(int32 Id) const;
	FOKSceneTrack* TrackOf(int32 Id) { return const_cast<FOKSceneTrack*>(static_cast<const UOKSceneDocument*>(this)->TrackOf(Id)); }

	/** Next id handed out by AddObject. */
	UPROPERTY()
	int32 NextId = 1;

	/** Broadcast after any change made through this API. */
	FOKSceneChanged OnChanged;

	/** A new object under Parent (-1: root); returns its id. */
	UFUNCTION(BlueprintCallable, Category = "OK Studio")
	int32 AddObject(EOKSceneObjectKind Kind, const FString& Name, int32 Parent = -1);

	/** Removes the object and everything under it. */
	UFUNCTION(BlueprintCallable, Category = "OK Studio")
	bool RemoveObject(int32 Id);

	/** Moves an object under another (-1: root), keeping its world transform. Refuses cycles. */
	UFUNCTION(BlueprintCallable, Category = "OK Studio")
	bool Reparent(int32 Id, int32 NewParent);

	/** A copy of the object and its subtree next to it; returns the new id. */
	UFUNCTION(BlueprintCallable, Category = "OK Studio")
	int32 Duplicate(int32 Id);

	FOKSceneObject* Find(int32 Id);
	const FOKSceneObject* Find(int32 Id) const;
	TArray<int32> Children(int32 Parent) const;

	/** While one is alive, Find looks ids up in an index instead of scanning Objects (a frame's read-only passes over a
	 *  scene of thousands of objects); a change to the array's size or storage drops back to scanning. */
	struct FFindScope
	{
		explicit FFindScope(const UOKSceneDocument* InDocument);
		~FFindScope();
		FFindScope(const FFindScope&) = delete;
		FFindScope& operator=(const FFindScope&) = delete;
	private:
		const UOKSceneDocument* Document;
	};

	/** Transform in scene space (parents applied). */
	UFUNCTION(BlueprintCallable, Category = "OK Studio")
	FTransform WorldTransform(int32 Id) const;
    /** Runtime item clock; source normalized start times remain unchanged when saving. */
    float ItemAnimationSeconds = 0.f;
	/** Posed frame of a character bone relative to the character (set by the actor showing the scene; without it attached
	 * objects sit at the character root). */
	TFunction<bool(int32 Character, const FString& Bone, FTransform& OutBone)> BoneFrame;
	/** The frame children of `Parent` are placed in: its world, and for a catalogue item its child point (子の接続先). */
	FTransform ChildFrame(int32 Parent) const;
	/** The frame an object's Transform is relative to: its parent's child frame, and the posed bone it hangs on. */
	FTransform ParentFrame(int32 Id) const;

	/** Visible unless it or a parent is hidden. */
	bool IsShown(int32 Id) const;

	/** How far a walk up the parents goes (a corrupt file's parent cycle must not hang it). KStudio scenes nest up to
	 *  1024 deep (an audience of glow sticks: each one turning under the last); 256 left deep objects unplaced and
	 *  shown under a hidden folder. */
	static constexpr int32 MaxDepth = 4096;

	FString ToJson() const;
	bool FromJson(const FString& Json, FString& OutError);
	/** Objects (with everything under them, and their tracks) as a clipboard fragment. */
	FString ExportFragment(const TArray<int32>& Ids) const;
	/** Adds a fragment's objects under Parent (-1: root) with new ids; returns the new ids of its top objects. */
	TArray<int32> PasteFragment(const FString& Json, int32 Parent, FString& OutError);
	/** Adds another scene's objects and tracks with new ids, keeping this scene's camera, map and look (Studio's 追加). */
	TArray<int32> Merge(const FString& SceneJson, FString& OutError);
	/** Writes the file whole or not at all: a temporary file beside it replaces it once written. */
	bool SaveToFile(const FString& Path, FString& OutError) const;
	static bool ReplaceFile(const FString& Temp, const FString& Path, FString& OutError);
	bool LoadFromFile(const FString& Path, FString& OutError);

	void NotifyChanged() { OnChanged.Broadcast(); }

private:
	/** FFindScope's index: id -> position in Objects, valid for the array's storage and size it was built for. */
	mutable TMap<int32, int32> FindIndex;
	mutable const FOKSceneObject* FindIndexData = nullptr;
	mutable int32 FindIndexNum = -1;
	mutable int32 FindScopes = 0;
	const FOKSceneObject* FindIndexed(int32 Id) const;
};
