#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "OpenKoikatsuGameMode.h"
#include "OKPaintDemo.generated.h"
class UPoseableMeshComponent;
UCLASS()
class OPENKOIKATSU_API AOKPaintDemoCharacter:public AActor
{
 GENERATED_BODY()
public:
 AOKPaintDemoCharacter();
 UPROPERTY(VisibleAnywhere) TObjectPtr<UPoseableMeshComponent> Body;
 UPROPERTY(EditAnywhere,Category="Paint Demo") bool Animate=true;
 virtual void BeginPlay() override;
 virtual void Tick(float Dt) override;
 float Time=0;
};
UCLASS()
class OPENKOIKATSU_API AOKPaintDemoGameMode:public AOpenKoikatsuGameMode
{
 GENERATED_BODY()
public:
 AOKPaintDemoGameMode();
};
