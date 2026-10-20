#pragma once
#include "CoreMinimal.h"
#include "Animation/AnimSequence.h"
#include "KKImportedAnimSequence.generated.h"
// Standard compressed UAnimSequence at runtime. Editor source keys use the
// quaternion model to avoid the Sequencer FK/Euler singularity conversion.
UCLASS(BlueprintType)
class KKIMPORTEDASSETS_API UKKImportedAnimSequence:public UAnimSequence {
 GENERATED_BODY()
public:
 virtual void PostInitProperties() override;
 virtual void PostLoad() override;
};
