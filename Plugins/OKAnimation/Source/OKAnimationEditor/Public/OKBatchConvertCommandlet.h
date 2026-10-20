#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "OKBatchConvertCommandlet.generated.h"

/**
 * Bulk import into OK animations:
 *   -run=OKBatchConvert -Source=/Game/Mocap -Dest=/Game/OKAnimation [-Profile=/Game/X.X] [-Fps=30]
 *       every AnimSequence under Source (recursive) -> OK animation assets in Dest (folders kept);
 *   -run=OKBatchConvert -Files=D:/okanim -Dest=/Game/OKAnimation
 *       every .okanim / .okgroup file (e.g. written by OKAnim.BatchKK from the game's KK clips) -> assets.
 */
UCLASS()
class OKANIMATIONEDITOR_API UOKBatchConvertCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UOKBatchConvertCommandlet();
	virtual int32 Main(const FString& Params) override;
};
