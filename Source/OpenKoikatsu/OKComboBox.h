#pragma once

#include "CoreMinimal.h"
#include "Components/ComboBoxString.h"
#include "OKComboBox.generated.h"

// Every editor combo uses this class so the closed control and its popup rows
// share one readable dark style. The engine's runtime row style draws
// non-selected rows in near-black text, which vanishes on the dark menu.
UCLASS()
class OPENKOIKATSU_API UOKComboBoxString : public UComboBoxString
{
	GENERATED_BODY()

public:
	UOKComboBoxString(const FObjectInitializer& ObjectInitializer);
};
