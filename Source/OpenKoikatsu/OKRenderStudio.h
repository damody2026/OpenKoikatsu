#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Subsystems/WorldSubsystem.h"
#include "OKRenderStudio.generated.h"

// Render-style studio (Docs/HG_RENDER_STYLE.md): its own empty map (/Engine/Maps/Entry), a plain gray backdrop, the
// selection's heroines in a row, every render style photographed under the same key lights, then exit.
//   OpenKoikatsu.exe /Engine/Maps/Entry -RenderStudio=<studio.json>
struct FOKRenderStudio;

UCLASS(NotPlaceable)
class AOKRenderStudio:public AActor{
 GENERATED_BODY()
public:
 AOKRenderStudio();
 FString ConfigPath;
protected:
 virtual void BeginPlay() override;
 virtual void Tick(float DeltaSeconds) override;
private:
 TSharedPtr<FOKRenderStudio> Studio;
};

// Spawns the studio in a game world when the command line asks for it; nothing otherwise.
UCLASS()
class UOKRenderStudioSubsystem:public UWorldSubsystem{
 GENERATED_BODY()
public:
 virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
 virtual void OnWorldBeginPlay(UWorld& World) override;
};
