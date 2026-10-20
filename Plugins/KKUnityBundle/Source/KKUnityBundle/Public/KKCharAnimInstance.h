#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "KKCharAnimInstance.generated.h"

struct FKKCharAnimInstanceProxy;
class UAnimSequence;

UCLASS(Transient, NotBlueprintable)
class KKUNITYBUNDLE_API UKKCharAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	void SetLocalPose(TArray<FTransform> InPose, TArray<uint8> InLayer);
	void SetExternalSequence(UAnimSequence* Seq, float Time = 0.f);

protected:
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;

	TArray<FTransform> PendingPose;
	TArray<uint8> BoneLayer;
	TObjectPtr<UAnimSequence> ExternalSeq;
	float SeqTime = 0.f;
	bool bHasPose = false;

	friend struct FKKCharAnimInstanceProxy;
};
