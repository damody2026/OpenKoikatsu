#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "OKSceneDocument.h"
#include "UObject/StrongObjectPtr.h"
#include "OKSceneActor.generated.h"

class AKKPreviewActor;
class UCameraComponent;
class ULightComponent;
class UStaticMeshComponent;
class UTexture2D;

/**
 * Shows an OK scene document in a world (game or editor preview): characters from OK cards and items from OK base
 * library models as preview actors, lights and cameras as components. Sync() follows the document; objects whose
 * content changed are rebuilt, others only move.
 */
UCLASS()
class OKSTUDIO_API AOKSceneActor : public AActor
{
	GENERATED_BODY()

public:
	AOKSceneActor();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OK Studio")
	TObjectPtr<UOKSceneDocument> Document;

	/** Level Sequence / Movie Render Queue: the scene as .okscene JSON. With it the actor shows the scene on its own
	 *  (no OK Studio around it): its document is built from the JSON and posed at SequenceTime. */
	UPROPERTY(EditAnywhere, Category = "OK Studio")
	FString SceneJson;

	/** The scene timeline time shown (an exported sequence keys it from 0 to the scene length). */
	UPROPERTY(EditAnywhere, Interp, Category = "OK Studio")
	float SequenceTime = 0.f;

	/** Shows SequenceTime of the SceneJson scene (builds it the first time): timeline keys, character animations. */
	void ShowSequenceTime();

	/** Brings the world in line with the document. Errors of objects that could not be shown go to Errors. */
	UFUNCTION(BlueprintCallable, Category = "OK Studio")
	void Sync();

	/** Releases the complete previous scene before a replacement is built. */
	void ReleaseScene();

	/** Objects built per Sync (one frame); the rest are built on the following Ticks. */
	int32 BuildsPerFrame = 24;

	/** Whether some document objects are not built yet (a large scene being built over several frames). */
	bool HasPendingBuilds() const { return bBuildsPending; }

	/** Advances character animations (seconds). */
	UFUNCTION(BlueprintCallable, Category = "OK Studio")
	void AdvanceAnimations(float DeltaSeconds);
    /** Evaluates character poses and secondary motion once; holding the clip phase does not freeze cloth. */
    void AdvanceCharacterFrames(float DeltaSeconds, bool bAdvancePhase);
    /** Evidence capture holds the saved animation phase while rendering settles. */
    bool bPauseAnimations = false;
	/** Poses a character's animation at `Time` seconds of the scene timeline (its own start time and speed applied). */
	void SetAnimationTime(int32 Id, float Time);
	/** The next Sync builds the object again (after something changed its preview outside the document). */
	void Rebuild(int32 Id);
	/** Plays another Studio animation (OK animation library) on a shown character in place, without building it again;
	 *  the document object follows. */
	bool SetCharacterAnimation(int32 Id, int32 Kind, int32 Group, int32 Category, int32 No, FString& OutError);

	/** The preview actor showing a character or item object. */
	AKKPreviewActor* PreviewOf(int32 Id) const;
	/** The Studio controls (option1, option2, pattern) a shown character's H motion blends along and the values its
	 *  player has (empty: a plain clip). */
	TArray<TPair<FString, float>> BlendControlsOf(int32 Id) const;
	/** The shown objects' preview actors (characters and items; not the map). */
	TArray<AActor*> ObjectPreviews() const;
	USceneComponent* ComponentOf(int32 Id) const;
	/** Camera component of the active camera object, if any. */
	UCameraComponent* ActiveCamera() const;

	/** Per object id: why it could not be shown. */
	TMap<int32, FString> Errors;

	/** The scene map (Document->Map), if shown; MapError when it could not be. */
	AKKPreviewActor* MapPreview() const { return ShownMapPreviews.Num() ? ShownMapPreviews[0].Get() : nullptr; }
	int32 MapPartCount() const;
	FString MapError;

	virtual void Destroyed() override;
	virtual void BeginPlay() override;
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual bool ShouldTickIfViewportsOnly() const override { return true; }

	/** Where the scene is viewed from (game viewer / editor viewport camera); characters looking at the camera use it. */
	FVector ViewLocation = FVector::ZeroVector;
	bool bHasViewLocation = false;
	/** Eyes and neck of every shown character towards the view or their look point (each tick). */
	void UpdateLooks(float DeltaSeconds, bool bApplyStaticPose = true);
	/** Moves objects hung on character bones (and what hangs under them) to the bones' current pose. */
	void PlaceAttached();
	/** Studio's scene-root character light as the items' light: their shared parent instances'
	 *  _WorldSpaceLightPos0 and _LightColor0, written when the light changes. */
	void UpdateItemLighting();
	FLinearColor ItemLightDirection = FLinearColor(0.f, 0.f, 0.f, -1.f), ItemLightColor = FLinearColor(0.f, 0.f, 0.f, -1.f);
	/** The scene has had a light: from then on the characters' forward light follows the scene's lights (off when none is on). */
	bool bCharacterLightSeen = false;
	TSet<TWeakObjectPtr<AKKPreviewActor>> ItemsLit;
	/** The map/sky's full probe on characters and unlightmapped Standard surfaces: the sky pattern it was written for (-1: the
	 *  character display's own) and the characters that have it. */
	int32 AmbientSky = -1;
	int32 AmbientMap = -1;
	TSet<TWeakObjectPtr<AKKPreviewActor>> AmbientLit;
	/** Stands the scene's background image (Post.Background) behind everything in the view `View` with horizontal field of
	 * view `Fov` (degrees), filling it like Studio's BackgroundCtrl; hidden without a background. */
	void PlaceBackground(const FTransform& View, float Fov, float Aspect = 16.f / 9.f);
	/** The sky pattern the dome shows (-1 none; 1000 and up: a map's own sky). */
	int32 ShownSkyPattern() const { return SkyShown; }

	/** Directory of the OK base library (items). */
	static FString LibraryDirectory();
	/** Shows or hides the scene's frame picture (a screenshot without it); it stays hidden only until shown again. */
	void SetFrameShown(bool bShown) { if (Frame.Plane.IsValid()) Frame.Plane->SetVisibility(bShown && Frame.Texture.IsValid()); }
	/** The library model and prefab an item shows (its catalogue entry's, or its own). */
	static bool ItemModel(const FOKSceneItem& Item, FString& OutModel, FString& OutPrefab, FString& OutError);

private:
	/** A picture shown in the view (background or frame): its plane, texture and the file shown. */
	struct FScenePicture
	{
		TStrongObjectPtr<UStaticMeshComponent> Plane;
		TStrongObjectPtr<UTexture2D> Texture;
		FString Shown;
	};
	FScenePicture Background, Frame;
	/** Studio's sky: a dome around the view with the pattern's material (OKStudioSky), and the pattern shown (-1 none). */
	TStrongObjectPtr<UStaticMeshComponent> SkyDome;
	int32 SkyShown = -1;
	void PlaceSky(const FTransform& View);
	/** The shown items' sky dome parts (OKSkyDome), found again after the shown objects change. */
	TArray<TWeakObjectPtr<UPrimitiveComponent>> SkyDomeParts;
	/** The shown items' blended parts (AKKPreviewActor::BlendedParts), ranked every frame in Unity's draw order for their
	 *  materials' depth test (RankBlendedParts); Rank is what each was last given. */
	struct FRankedPart
	{
		TWeakObjectPtr<UPrimitiveComponent> Part;
		int32 Queue = 3000;
		bool bZWrite = false;
		int32 Renderer = -1;
		int32 Submesh = 0;
		int32 Rank = -1;
		/** Its own sort priority when first seen (OKSceneActor adds its queue's place to it). */
		int32 BasePriority = 0;
	};
	TArray<FRankedPart> BlendedParts;
	void RankBlendedParts(const FVector& Eye);
	/** The shown objects' and map's parts Unity's camera depth texture holds (AKKPreviewActor::DepthTexturePresentParts):
	 *  while something reads that texture (SunShafts, StylizedWater) they draw into custom depth (stencil 0, below every
	 *  blended part's rank), which stands in for it (OKStudioPost, the water's depth). */
	TArray<TWeakObjectPtr<UPrimitiveComponent>> DepthTexturePresentParts;
	bool bDepthTextureMarked = false;
	/** A shown item reads Unity's depth texture itself (AKKPreviewActor::bReadsUnityDepthTexture: StylizedWater). */
	bool bDepthTextureReaders = false;
	void MarkDepthTexture();
	void EnableCustomDepthStencil();
	bool bCustomDepthStencil = false;
	FVector RankEye = FVector(NAN);
	bool bSkyDomesDirty = true;
	bool bBuildsPending = false;
	/** Previews destroyed since the last forced garbage collection. */
	int32 ReleasedSinceCollect = 0;
	float ShownSequenceTime = -1.f;
	/** GFrameCounter of the last Sync that built an object. */
	uint64 LastBuildFrame = 0;

	struct FShown
	{
		EOKSceneObjectKind Kind = EOKSceneObjectKind::Folder;
		FOKSceneCharacter Character;
		FOKSceneItem Item;
		FOKSceneLight Light;
		/** The shown effect's id (another id rebuilds it; speed, intensity and colour apply in place). */
		FString Effect;
		TWeakObjectPtr<AKKPreviewActor> Preview;
		/** Studio's guide replaces the prefab root TRS; undo the root baked into model vertices. */
		FTransform RenderCorrection = FTransform::Identity;
		TWeakObjectPtr<USceneComponent> Component;
		/** Where the component sits in the object (a camera item's capture at its camera node); identity for the rest. */
		FTransform ComponentOffset = FTransform::Identity;
		/** A camera item's RenderTexture key (its capture is `Component`); 0 for the rest. */
		int32 RenderCameraKey = 0;
		float Time = 0.f;
		/** FK bones the preview currently has. */
		TArray<FOKSceneFkBone> AppliedFk;
		TArray<FOKSceneIkTarget> AppliedIk;
		int32 AppliedFkPartsOff = 0, AppliedIkPartsOff = 0;
		float AppliedIkWeight = 1.f;
		/** The joint correction categories the preview session runs (-1: not set yet, the session runs all). */
		int32 AppliedJointCorrection = -1;
        TArray<FQuat> AppliedFixedNeck;
		/** Studio animation loaded from the OK animation library (kept alive while shown). */
		TStrongObjectPtr<UObject> StudioAnim;
		/** The Studio controls the shown animation blends along (player parameter I is control I), their defaults and
		 *  the values the player has. */
		TArray<FString> BlendControls;
		TArray<float> BlendDefaults, AppliedBlend;
		int32 AppliedHands[2] = { -1, -1 };
		bool bEyesTracking = false;
		bool bFixedEyesApplied = false;
		TArray<float> AppliedFixedEyes;
		int32 AppliedFixedEyesPattern = -1;
		bool bNeckLooking = false;
		/** The character's eye opening before the scene set one (-1: the scene never did). */
		float EyesOpenBefore = -1.f;
		bool bEyesManualBefore = false;
		/** PlaceAttached's placement generation this object was last placed at (static placements skip until it changes). */
		uint32 PlacedGeneration = 0;
	};
	/** Bumped when what static placements depend on may have changed (a sync, the scene actor moving). */
	uint32 PlaceGeneration = 1;
	FTransform PlacedActorTransform;
	TMap<int32, FShown> Shown;
	int32 ShownMap = -1;
	TArray<TWeakObjectPtr<AKKPreviewActor>> ShownMapPreviews;
	void SyncMap();
	// The map's time of day (SunLightInfo.Set): which time its parts show and its own sun light then.
	UPROPERTY(Transient)
	TObjectPtr<class UDirectionalLightComponent> MapSun;
	int32 ShownMapTime = -2;
	/** The map sun as the items' forward light (Unity: the brightest directional light whose culling mask has the
	 *  item's layer; the map's own sun lights layer 11, where landscape items are authored): towards-light direction,
	 *  linear colour and culling mask (0: no sun). */
	FLinearColor MapSunItemDirection = FLinearColor::Black, MapSunItemColor = FLinearColor::Black;
	uint32 MapSunMask = 0;
	/** The map's own realtime lamps (ok_okmodel_map_lights) with the times of day they show at (0: always). */
	TArray<TPair<TWeakObjectPtr<class ULightComponent>, uint32>> MapLights;
	void AddMapLights(class AKKPreviewActor* Map, const FString& Model, const FString& Root);
	int32 ShownMapComponents = 0;
	FTransform ShownMapSunPlace;
	void ApplyMapTime(const FTransform& MapPlace);

	bool Build(const FOKSceneObject& Object, FShown& Out, FString& OutError);
	/** A Studio camera item (a camera drawing into a RenderTexture, e.g. HarvexARC's CH1-Camera): its capture as `Out.Component`. */
	void AddRenderCamera(const FString& Model, const FString& Prefab, FShown& Out);
	/** Gives each screen item showing a camera item's RenderTexture that camera's target (the first camera item of a
	 *  key, hidden or not: Studio hides only an item's renderers), and captures every frame only the cameras some shown
	 *  screen shows. */
	void WireRenderTextures();
	/** OK_RT_DUMP=1: the shown camera items' pictures and poses (debugging). */
	void DumpRenderTextures();
	/** The camera items' targets by RenderTexture key (shared by every camera and screen of that key). */
	UPROPERTY(Transient)
	TMap<int32, TObjectPtr<class UTextureRenderTarget2D>> SharedRenderTargets;
	/** Draws each shown mirror part's reflection (AKKPreviewActor::MirrorParts) as Unity's MirrorReflection script does:
	 *  the main view mirrored in the part's plane, clipped at the plane by an oblique near plane, without image effects. */
	void UpdateMirrors();
	UPROPERTY(Transient)
	TArray<TObjectPtr<class USceneCaptureComponent2D>> MirrorCaptures;
	TArray<TWeakObjectPtr<class UMaterialInstanceDynamic>> MirrorCaptureMaterials;
	void Release(FShown& Item);
	void ApplyCharacterState(const FOKSceneObject& Object, FShown& Item);
};
