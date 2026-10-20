#include "../OKGalleryWidget.h"
#include "KKPreviewActor.h"
#include "Components/MeshComponent.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Containers/Ticker.h"
#include "Engine/World.h"
#include "Engine/Texture2D.h"
#include "HAL/IConsoleManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/UObjectIterator.h"
#include "UnrealClient.h"
#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
namespace OKPreviewShaderClothingAudit {
struct FRun {
 TWeakObjectPtr<UWorld> World;TWeakObjectPtr<UOKGalleryWidget> Gallery;TWeakObjectPtr<AKKPreviewActor> OriginalActor;
 TWeakObjectPtr<UMeshComponent> TestComponent;TStrongObjectPtr<UMaterialInterface> OriginalMaterial;
 FString Card,Output;int32 Stage=0,Generation=0,InitialPending=0,MaskCount=0;double Start=FPlatformTime::Seconds(),VisibleMs=0,MaxSwitchMs=0;
 TArray<float> Body,Face;
 TMap<UMaterialInstanceDynamic*,UTexture*> SavedMasks;
 uint64 VisibleClothes(AKKPreviewActor* Actor){uint64 Count=0;for(const auto& Part:Actor->Parts)if(Part.Role.StartsWith(TEXT("clothes"))){const FName Tag(*FString::Printf(TEXT("KKSessionPart_%d"),Part.SessionIndex));TInlineComponentArray<UMeshComponent*> Components;Actor->GetComponents(Components);for(auto* Component:Components)if(Component->IsVisible()&&!Component->bHiddenInGame&&(Component==Part.Comp||Component->ComponentHasTag(Tag)))++Count;}return Count;}
 bool Stop(const FString& Error){auto Result=MakeShared<FJsonObject>();Result->SetBoolField(TEXT("valid"),Error.IsEmpty());Result->SetStringField(TEXT("error"),Error);Result->SetNumberField(TEXT("visible_ms"),VisibleMs);Result->SetNumberField(TEXT("max_full_off_restore_ms"),MaxSwitchMs);Result->SetNumberField(TEXT("body_mask_materials"),MaskCount);Result->SetNumberField(TEXT("progressive_pending"),InitialPending);Result->SetNumberField(TEXT("visibility_switches"),32);FString Json;FJsonSerializer::Serialize(Result,TJsonWriterFactory<>::Create(&Json));FFileHelper::SaveStringToFile(Json,*Output);UE_LOG(LogTemp,Display,TEXT("Preview shader clothing audit valid=%d error=%s max_ms=%.3f pending=%d"),Error.IsEmpty(),*Error,MaxSwitchMs,InitialPending);FPlatformMisc::RequestExitWithStatus(false,Error.IsEmpty()?0:1);return false;}
 bool Tick(float){
  if(!World.IsValid()||FPlatformTime::Seconds()-Start>360)return Stop(TEXT("Audit timed out"));
  auto* G=Gallery.Get();if(!G){for(TObjectIterator<UOKGalleryWidget> It;It;++It)if(It->GetWorld()==World.Get()&&It->PreviewImage){G=*It;Gallery=G;break;}if(!G)return true;}
  if(Stage==0){G->bIndependentCards=false;G->Entries.Reset();FKKCardListEntry Entry;Entry.Path=Card;G->Entries.Add(Entry);G->SelectIndex(0);G->OnLoad3D();Stage=1;return true;}
  if(G->bLoadInFlight)return true;auto* Actor=G->PreviewActor.Get();if(!Actor)return Stop(TEXT("Actor missing"));
  if(Stage==1){
   VisibleMs=(FPlatformTime::Seconds()-Start)*1000.;OriginalActor=Actor;Generation=G->LoadGeneration;Body=Actor->Session.ShapeBody();Face=Actor->Session.ShapeFace();
   if(!VisibleClothes(Actor))return Stop(TEXT("Fixture has no visible clothing"));
   const uint64 Worn=VisibleClothes(Actor);const float Clock=Actor->AnimPhase;
   for(int32 Index=0;Index<32;++Index){const bool Off=Index%2==0;G->OnClothesState(Off?102:100);MaxSwitchMs=FMath::Max(MaxSwitchMs,G->LastClothingSwitchMs);
    if(G->bLoadInFlight||G->LoadGeneration!=Generation||G->PreviewActor!=Actor)return Stop(TEXT("Full off/restore rebuilt actor"));
    if(Actor->bClothingHidden!=Off||VisibleClothes(Actor)!=(Off?0:Worn))return Stop(TEXT("Clothing visibility or restoration mismatch"));
    if(Actor->AnimPhase!=Clock||Actor->Session.ShapeBody()!=Body||Actor->Session.ShapeFace()!=Face)return Stop(TEXT("Visibility switch reset pose/shape"));
    if(Off){MaskCount=Actor->ClothingMaskMaterials.Num();if(!MaskCount)return Stop(TEXT("Body mask was not overridden"));SavedMasks.Reset();for(int32 MaskIndex=0;MaskIndex<MaskCount;++MaskIndex){auto* Material=Actor->ClothingMaskMaterials[MaskIndex].Get();SavedMasks.Add(Material,Actor->ClothingOriginalMasks[MaskIndex].Get());UTexture* Texture=nullptr;if(!Material->GetTextureParameterValue(FMaterialParameterInfo(TEXT("_AlphaMask")),Texture)||!Texture)return Stop(TEXT("Body mask override missing"));auto* White=Cast<UTexture2D>(Texture);if(!White||White->GetSizeX()!=1||White->GetSizeY()!=1)return Stop(TEXT("Neutral mask dimensions wrong"));auto& Data=White->GetPlatformData()->Mips[0].BulkData;const auto* Pixel=static_cast<const FColor*>(Data.LockReadOnly());const bool Neutral=Pixel&&*Pixel==FColor::White;Data.Unlock();if(!Neutral)return Stop(TEXT("Full off retained a body clipping mask"));}}
    else for(const auto& Mask:SavedMasks){UTexture* Restored=nullptr;if(!Mask.Key->GetTextureParameterValue(FMaterialParameterInfo(TEXT("_AlphaMask")),Restored)||Restored!=Mask.Value)return Stop(TEXT("Original clothing mask not restored"));}
   }
   if(MaxSwitchMs>100)return Stop(TEXT("Visibility callback exceeded 100ms"));
   Stage=2;return true;
  }
  if(Stage==2){
   if(!Actor->TickProgressiveMaterials())return true;
   // A new shader-map identity exercises the real asynchronous compiler even
   // when the normal card's DDC is warm. The expression graph stays identical.
   for(const auto& Part:Actor->Parts)if(Part.Role==TEXT("body")&&Part.Comp&&Part.Comp->IsVisible()){TestComponent=Part.Comp;break;}
   if(!TestComponent.IsValid()){TInlineComponentArray<UMeshComponent*> Components;Actor->GetComponents(Components);for(auto* Component:Components)if(Component->IsVisible()&&!Component->bHiddenInGame&&Component->GetNumMaterials()){TestComponent=Component;break;}}
   if(!TestComponent.IsValid())return Stop(TEXT("No display to exercise progressive materials"));
   auto* Original=TestComponent->GetMaterial(0);OriginalMaterial.Reset(Original);
   auto* Test=DuplicateObject<UMaterial>(Original->GetMaterial(),Actor);Test->StateId=FGuid::NewGuid();Test->ForceRecompileForRendering();TestComponent->SetMaterial(0,Test);
   if(!Actor->BeginProgressiveMaterials())return true;
   InitialPending=Actor->PendingPreviewMaterials.Num();if(!InitialPending)return Stop(TEXT("Compiler fixture completed before preview path was exercised"));
   if(TestComponent->GetMaterial(0)==Test)return Stop(TEXT("Pending shader was not replaced by preview"));
   Stage=3;return true;
  }
  if(Stage==3){if(!Actor->TickProgressiveMaterials())return true;if(!TestComponent.IsValid()||TestComponent->GetMaterial(0)==OriginalMaterial.Get())return Stop(TEXT("Final material replacement missing"));TestComponent->SetMaterial(0,OriginalMaterial.Get());Stage=4;return true;}
  if(Stage==4){if(Actor!=OriginalActor.Get()||Actor->Session.ShapeBody()!=Body||Actor->Session.ShapeFace()!=Face)return Stop(TEXT("Progressive replacement changed actor/shape"));FScreenshotRequest::RequestScreenshot(FPaths::ChangeExtension(Output,TEXT("png")),true,false);Stage=5;return true;}
  return Stop(FString());
 }
};
}
static FAutoConsoleCommandWithWorld PreviewShaderClothingAudit(TEXT("ok.AuditPreviewShaderClothing"),TEXT("Verify progressive shader display and immediate full clothing removal"),FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World){auto Run=MakeShared<OKPreviewShaderClothingAudit::FRun>();Run->World=World;Run->Card=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_GUI_RELOAD_CARD"));Run->Output=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_GUI_RELOAD_OUTPUT"));if(!World||Run->Card.IsEmpty()||Run->Output.IsEmpty())return;FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Run](float Delta){return Run->Tick(Delta);}));}));
#endif
