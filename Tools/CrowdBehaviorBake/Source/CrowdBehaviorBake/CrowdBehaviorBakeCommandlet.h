#pragma once
#include "Commandlets/Commandlet.h"
#include "CrowdBehaviorBakeCommandlet.generated.h"
UCLASS() class UCrowdBehaviorBakeCommandlet:public UCommandlet {GENERATED_BODY() public:UCrowdBehaviorBakeCommandlet();virtual int32 Main(const FString&) override;};
