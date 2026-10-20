#include "Misc/AutomationTest.h"
#include "../OKCrowdLayout.h"
#include "../OKCrowdScene.h"
#include "GameFramework/WorldSettings.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "Misc/PackageName.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/StaticMeshActor.h"
#include "Materials/Material.h"
#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOKCrowdRoutes,"OpenKoikatsu.Crowd.Routes",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FOKCrowdRoutes::RunTest(const FString&){
 for(int32 I=0;I<100;++I){
  TestTrue(TEXT("Every resident moves"),FVector::Distance(OKCrowdLayout::Position(I,0),OKCrowdLayout::Position(I,1))>80);
  for(int32 Step=0;Step<120;++Step){const auto P=OKCrowdLayout::Position(I,Step*.25);
   if(!TestTrue(TEXT("Remain inside room walls with body clearance"),FMath::Abs(P.X)<2400&&FMath::Abs(P.Y)<2400&&P.Z==0))return false;
   for(int32 J=I+1;J<100;++J)if(!TestTrue(TEXT("Paths keep residents apart"),FVector::DistSquared(P,OKCrowdLayout::Position(J,Step*.25))>10000))return false;
  }
 }
 return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOKCrowdMap,"OpenKoikatsu.Crowd.SavedMap",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FOKCrowdMap::RunTest(const FString&){
 auto* World=LoadObject<UWorld>(nullptr,TEXT("/Game/Scenes/CrowdRoom/CrowdRoom.CrowdRoom"));
 if(!TestNotNull(TEXT("Real saved map"),World))return false;
 TestEqual(TEXT("Game scene mode"),World->GetWorldSettings()->DefaultGameMode.Get(),AOKCrowdGameMode::StaticClass());
 AOKCrowdScene* Scene=nullptr;for(const auto& Actor:World->PersistentLevel->Actors)if(auto* Candidate=Cast<AOKCrowdScene>(Actor.Get())){TestNull(TEXT("Single loader"),Scene);Scene=Candidate;}
 if(!TestNotNull(TEXT("Crowd loader"),Scene))return false;
 TestEqual(TEXT("Selected residents"),Scene->Characters.Num(),100);TestEqual(TEXT("Walk animations"),Scene->Walks.Num(),100);TestEqual(TEXT("Selection receipt"),Scene->CardNames.Num(),100);
 TSet<FString> Unique;
 for(int32 I=0;I<Scene->Characters.Num();++I){const auto Path=Scene->Characters[I].ToSoftObjectPath();Unique.Add(Path.ToString());TestTrue(TEXT("Saved character exists"),FPackageName::DoesPackageExist(Path.GetLongPackageName()));
  if(Scene->Walks.IsValidIndex(I))TestTrue(TEXT("Saved walk exists"),FPackageName::DoesPackageExist(Scene->Walks[I].ToSoftObjectPath().GetLongPackageName()));}
 TestEqual(TEXT("100 distinct character assets"),Unique.Num(),100);return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOKCrowdRoomEnvironment,"OpenKoikatsu.Crowd.RoomEnvironment",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FOKCrowdRoomEnvironment::RunTest(const FString&){
 auto* World=LoadObject<UWorld>(nullptr,TEXT("/Game/Scenes/CrowdRoom/CrowdRoom.CrowdRoom"));
 if(!TestNotNull(TEXT("Saved room"),World))return false;
 int32 Groups=0,Instances=0,Lights=0;
 for(AActor* Actor:World->PersistentLevel->Actors)if(Actor){
  TestFalse(TEXT("Legacy test floor/walls removed"),Actor->IsA<AStaticMeshActor>());
  if(Actor->IsA<ADirectionalLight>())++Lights;
  if(auto* Group=Actor->FindComponentByClass<UHierarchicalInstancedStaticMeshComponent>()){
   ++Groups;Instances+=Group->GetInstanceCount();
   TestFalse(TEXT("Decoration does not tick"),Actor->PrimaryActorTick.bCanEverTick);
   TestNotNull(TEXT("Shared geometry saved"),Group->GetStaticMesh().Get());
   auto* Interface=Group->GetMaterial(0);auto* Material=Interface?Interface->GetMaterial():nullptr;
   if(TestNotNull(TEXT("Shared material saved"),Material))TestTrue(TEXT("Cook includes instanced permutation"),Material->GetUsageByFlag(MATUSAGE_InstancedStaticMeshes));
   for(int32 I=0;I<Group->GetInstanceCount();++I){FTransform Transform;TestTrue(TEXT("Serialized instance readable"),Group->GetInstanceTransform(I,Transform));TestFalse(TEXT("Finite room geometry"),Transform.ContainsNaN());}
  }
 }
 TestEqual(TEXT("One environment generation"),Groups,11);TestEqual(TEXT("No duplicated geometry"),Instances,369);TestEqual(TEXT("One shadowed light"),Lights,1);return true;
}
#endif
