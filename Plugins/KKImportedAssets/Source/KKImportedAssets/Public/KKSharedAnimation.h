#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "KKSharedAnimation.generated.h"
class AKKImportedCharacterActor;

// Original curve data is stored once and has no reference to any character.
UCLASS(BlueprintType)
class KKIMPORTEDASSETS_API UKKSharedAnimation : public UDataAsset {
 GENERATED_BODY()
public:
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Animation") FString Source;
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Animation") FString Clip;
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Animation") float Duration=0.f;
 UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Animation") bool Loop=true;
 UFUNCTION(BlueprintPure,Category="Animation") float GetPhase(float Seconds) const;
 UPROPERTY() TArray<uint8> AuthoredCurves;
};

// The native evaluator is supplied by KKUnityBundle; baked UAnimSequences keep
// working with KKImportedAssets alone. Neither path requires a KKS installation.
class KKIMPORTEDASSETS_API IKKSharedAnimationDriver {
public:
 virtual ~IKKSharedAnimationDriver()=default;
 virtual bool Initialize(AKKImportedCharacterActor& Actor,FString& Error)=0;
 virtual bool Evaluate(AKKImportedCharacterActor& Actor,float Seconds,FString& Error)=0;
};
using FKKSharedDriverFactory=TFunction<TUniquePtr<IKKSharedAnimationDriver>()>;
KKIMPORTEDASSETS_API void OKRegisterSharedAnimationDriver(FKKSharedDriverFactory Factory);
KKIMPORTEDASSETS_API TUniquePtr<IKKSharedAnimationDriver> OKCreateSharedAnimationDriver();
