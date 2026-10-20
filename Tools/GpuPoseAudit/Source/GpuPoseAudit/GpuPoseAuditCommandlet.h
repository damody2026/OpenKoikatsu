#pragma once
#include "Commandlets/Commandlet.h"
#include "GpuPoseAuditCommandlet.generated.h"
UCLASS()
class UGpuPoseAuditCommandlet:public UCommandlet {
 GENERATED_BODY()
public:
 UGpuPoseAuditCommandlet();
 int32 Main(const FString& Params) override;
};
