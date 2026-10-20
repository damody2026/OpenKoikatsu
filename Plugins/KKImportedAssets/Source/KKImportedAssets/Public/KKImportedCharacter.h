#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameFramework/Actor.h"
#include "KKSharedAnimation.h"
#include "KKImportedCharacter.generated.h"
class UStaticMesh;class USkeletalMesh;class USkeleton;class UAnimSequence;class UCurveFloat;
USTRUCT(BlueprintType)
struct KKIMPORTEDASSETS_API FKKImportedMouthPair {
 GENERATED_BODY()
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Mouth") FName Close;
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Mouth") FName Open;
};
USTRUCT(BlueprintType)
struct KKIMPORTEDASSETS_API FKKImportedPart {
 GENERATED_BODY()
 UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Character") FString Name;
 UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Character") FString Role;
 UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Character") TObjectPtr<UStaticMesh> Model;
 UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Character") TObjectPtr<USkeletalMesh> SkinnedModel;
 UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Character") FTransform Transform;
 UPROPERTY(VisibleAnywhere,Category="Source") int32 NativePartIndex=INDEX_NONE;
 UPROPERTY(VisibleAnywhere,Category="Source") FString NativeRenderer;
 UPROPERTY(VisibleAnywhere,Category="Source") FString SkinSourceHash;
 UPROPERTY(VisibleAnywhere,Category="Source") FString SkinSourceHashV2;
 UPROPERTY(VisibleAnywhere,Category="Mouth") TArray<FKKImportedMouthPair> MouthPatterns;
 UPROPERTY(VisibleAnywhere,Category="Mouth") TMap<FName,float> MouthBaseline;
 UPROPERTY(VisibleAnywhere,Category="Mouth") bool MouthHideWhenClosed=false;
 UPROPERTY(VisibleAnywhere,Category="Mouth") bool MouthDefaultVisible=true;
 UPROPERTY(VisibleAnywhere,Category="Source") TArray<int32> PaletteSlots;
 UPROPERTY(VisibleAnywhere,Category="Source") FString RigidFollow;
 UPROPERTY(VisibleAnywhere,Category="Source") TArray<float> RigidReference;
};
USTRUCT(BlueprintType)
struct KKIMPORTEDASSETS_API FKKImportedAnimation {
 GENERATED_BODY()
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Animation") FString Source;
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Animation") FString Clip;
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Animation") int32 Outfit=0;
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Animation") bool Loop=false;
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Animation") float SourceDuration=0.f;
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Animation") TObjectPtr<UAnimSequence> Sequence;
};
USTRUCT(BlueprintType)
struct KKIMPORTEDASSETS_API FKKImportedOutfit {
 GENERATED_BODY()
 UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Character") int32 Index=0;
 UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Character") TObjectPtr<USkeleton> Skeleton;
 UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Character") TArray<FKKImportedPart> Parts;
 UPROPERTY() TArray<uint8> AnimationSnapshot;
};
UCLASS(BlueprintType)
class KKIMPORTEDASSETS_API UKKImportedCharacter:public UDataAsset {
 GENERATED_BODY()
public:
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Character") int32 Schema=1;
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Character") FString SourceHash;
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Character") TArray<FKKImportedOutfit> Outfits;
};
UCLASS(BlueprintType)
class KKIMPORTEDASSETS_API UKKImportedAnimationLibrary:public UDataAsset {
 GENERATED_BODY()
public:
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Animation") TObjectPtr<UKKImportedCharacter> Character;
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Animation") TArray<FKKImportedAnimation> Animations;
};
// A saved character can be placed without a card, Rust session or KKS assets.
UCLASS()
class KKIMPORTEDASSETS_API AKKImportedCharacterActor:public AActor {
 GENERATED_BODY()
public:
 AKKImportedCharacterActor();
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Character",meta=(ExposeOnSpawn=true)) TObjectPtr<UKKImportedCharacter> Character;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Character",meta=(ExposeOnSpawn=true)) int32 Outfit=0;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Animation") TObjectPtr<UKKImportedAnimationLibrary> AnimationLibrary;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Animation") int32 AnimationIndex=0;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Animation",meta=(ToolTip="Play saved skeletal parts. Static-only parts do not yet deform or follow this animation.")) bool PlaySkeletalAnimation=false;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Animation") bool ShareBakedPose=false;
 // Opt-in only for saved animation and an identical complete reference skeleton.
 bool ConfigureBakedPoseSharing();
 UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Shared Animation") TObjectPtr<UKKSharedAnimation> SharedAnimation;
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Shared Animation") float SharedAnimationTime=0.f;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Shared Animation") bool SharedAnimationPlaying=true;
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Shared Animation") FString AnimationError;
 UFUNCTION(BlueprintCallable,Category="Shared Animation") bool PlaySharedAnimation(UKKSharedAnimation* Animation);
 UFUNCTION(BlueprintCallable,Category="Character") void RebuildCharacter();
 UFUNCTION(BlueprintCallable,Category="Animation") bool PlayImportedAnimation(int32 Index);
 UFUNCTION(BlueprintCallable,Category="Animation") bool SetAnimationPosition(float Seconds);
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Mouth") bool MouthControlEnabled=false;
 UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Mouth") int32 MouthPattern=0;
 UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Mouth",meta=(ClampMin="0",ClampMax="1")) float MouthOpen=0.f;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Mouth") TObjectPtr<UCurveFloat> MouthOpenCurve;
 UFUNCTION(BlueprintCallable,Category="Mouth") int32 GetMouthPatternCount() const;
 UFUNCTION(BlueprintCallable,Category="Mouth") bool SetMouthControl(int32 Pattern,float Open);
 UFUNCTION(BlueprintCallable,Category="Mouth") void ResetMouthControl();
 virtual void Tick(float DeltaSeconds) override;
 virtual void OnConstruction(const FTransform& Transform) override;
private:
 void CreateCharacterComponents(const FKKImportedOutfit& Coordinate);
 void ApplyMouthControl();
 TUniquePtr<IKKSharedAnimationDriver> SharedDriver;
};
