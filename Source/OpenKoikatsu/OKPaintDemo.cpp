#include "OKPaintDemo.h"
#include "Components/PoseableMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/HUD.h"
AOKPaintDemoCharacter::AOKPaintDemoCharacter(){PrimaryActorTick.bCanEverTick=true;Body=CreateDefaultSubobject<UPoseableMeshComponent>(TEXT("PaintTestRig"));SetRootComponent(Body);Tags.Add(TEXT("OKPaintable"));}
void AOKPaintDemoCharacter::BeginPlay(){Super::BeginPlay();Body->SetSkinnedAssetAndUpdate(LoadObject<USkeletalMesh>(nullptr,TEXT("/Game/PaintDemo/SK_TestRig.SK_TestRig")));}
void AOKPaintDemoCharacter::Tick(float Dt)
{
 Super::Tick(Dt);if(!Animate||!Body->GetSkinnedAsset())return;Time+=Dt;
 // An original, generated six-bone fixture: breathing, bend, raise arms, walk.
 const bool Action=FMath::Fmod(Time,16.f)>=8.f;
 const FTransform Upper(FRotator(Action?25.f*FMath::Sin(Time):2.f*FMath::Sin(Time*2),0,0),FVector(0,0,80));
 Body->SetBoneTransformByName(TEXT("Root"),FTransform::Identity,EBoneSpaces::ComponentSpace);
 Body->SetBoneTransformByName(TEXT("Upper"),Upper,EBoneSpaces::ComponentSpace);
 for(int32 Side=0;Side<2;++Side){const float Sign=Side?1:-1;
  const FTransform Arm(FRotator(0,0,Action?Sign*(40+30*FMath::Sin(Time)):0),FVector(0,Sign*25,50));
  Body->SetBoneTransformByName(Side?TEXT("ArmR"):TEXT("ArmL"),Arm*Upper,EBoneSpaces::ComponentSpace);
  Body->SetBoneTransformByName(Side?TEXT("LegR"):TEXT("LegL"),FTransform(FRotator(Action?Sign*25*FMath::Sin(Time*3):0,0,0),FVector(0,Sign*13,70)),EBoneSpaces::ComponentSpace);
 }
 Body->RefreshBoneTransforms();
}
AOKPaintDemoGameMode::AOKPaintDemoGameMode(){HUDClass=AHUD::StaticClass();}
