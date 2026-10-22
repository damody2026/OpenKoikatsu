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
		float Time = 0.f;
		/** FK bones the preview currently has. */
		TArray<FOKSceneFkBone> AppliedFk;
		TArray<FOKSceneIkTarget> AppliedIk;
        TArray<FQuat> AppliedFixedNeck;
		/** Studio animation loaded from the OK animation library (kept alive while shown). */
		TStrongObjectPtr<UObject> StudioAnim;
		int32 AppliedHands[2] = { -1, -1 };
		bool bEyesTracking = false;
		bool bFixedEyesApplied = false;
		TArray<float> AppliedFixedEyes;
		int32 AppliedFixedEyesPattern = -1;
		bool bNeckLooking = false;
	};
	TMap<int32, FShown> Shown;
	int32 ShownMap = -1;
	TArray<TWeakObjectPtr<AKKPreviewActor>> ShownMapPreviews;
	void SyncMap();
	// The map's time of day (SunLightInfo.Set): which time its parts show and its own sun light then.
	UPROPERTY(Transient)
	TObjectPtr<class UDirectionalLightComponent> MapSun;
	int32 ShownMapTime = -2;
	int32 ShownMapComponents = 0;
	FTransform ShownMapSunPlace;
	void ApplyMapTime(const FTransform& MapPlace);

	bool Build(const FOKSceneObject& Object, FShown& Out, FString& OutError);
	void Release(FShown& Item);
	void ApplyCharacterState(const FOKSceneObject& Object, FShown& Item);
};
