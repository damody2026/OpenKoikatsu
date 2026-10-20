#pragma once
#include "Commandlets/Commandlet.h"
#include "OKEditablePoseAuditCommandlet.generated.h"
UCLASS()
class UOKEditablePoseAuditCommandlet : public UCommandlet {
 GENERATED_BODY()
public:
 UOKEditablePoseAuditCommandlet();
 virtual int32 Main(const FString& Params) override;
};
