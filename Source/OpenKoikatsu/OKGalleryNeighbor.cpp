// The next card in the list, prepared while the reader is idle (OK_PRELOAD_NEIGHBOR=1; off by default): its session
// opens on a background worker exactly as loading it would (the card's own editor mods, physics and shape, all
// clothes worn, the first outfit), then its character is built hidden a step per frame. Loading that card swaps the
// prepared character in and runs only FinishPreparedPreview. Browsing the list in order then shows each next card
// at once; one extra character (session, textures, GPU display) stays in memory.
#include "OKGalleryWidget.h"
#include "KKPreviewActor.h"
#include "KKSkeletalBuilder.h"
#include "KKCardReader.h"
#include "KKGameRoot.h"
#include "KKCardWorkspace.h"
#include "KKRustExtract.h"
#include "Async/Async.h"
#include "Engine/World.h"
#include "Components/SizeBox.h"

struct FOKNeighborJob {
 // Path: the list entry; OpenPath: what StartLoad3D would open for it (the portable source for independent cards).
 FOkSession Session;FString Path,OpenPath,Error,Mods,PairJson;FVector2D Physics;TArray<float> ShapeFace,ShapeBody;bool Ok=false;
 TWeakObjectPtr<AKKPreviewActor> Actor;
};
namespace {
bool NeighborEnabled(){static const bool On=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_PRELOAD_NEIGHBOR"))==TEXT("1");return On;}
}

void UOKCharacterController::ResetNeighborPreload(){
 ++NeighborGeneration;
 if(NeighborJob)if(auto* Actor=NeighborJob->Actor.Get())if(Actor!=PreviewActor)Actor->Destroy();
 NeighborJob.Reset();
 if(IsValid(NeighborActor)&&NeighborActor!=PreviewActor)NeighborActor->Destroy();
 NeighborActor=nullptr;NeighborPath.Reset();
}

void UOKCharacterController::TickNeighborPreload(){
 if(!NeighborEnabled())return;
 // The card after the one shown (selecting the next card and loading it are separate steps for the user).
 int32 Shown=INDEX_NONE;
 if(!PreviewCardPath.IsEmpty())for(int32 I=0;I<Entries.Num();++I)if(Entries[I].Path==PreviewCardPath){Shown=I;break;}
 const FString Target=Shown!=INDEX_NONE&&Entries.IsValidIndex(Shown+1)?Entries[Shown+1].Path:FString();
 // The shown card changed: what is prepared belongs to another card.
 if(!NeighborPath.IsEmpty()&&NeighborPath!=Target)ResetNeighborPreload();
 if(Target.IsEmpty())return;
 // The reader's own work first: the same idle gate as the clothing previews, and those go before the neighbor.
 if(bLoadInFlight||bAppearanceBusy||bAppearanceThumbBusy||bPropertyBusy||(AppearancePanel&&AppearancePanel->IsVisible())||bAnimationPreparing||
    PendingAnimationKind>=0||!bModelLayout||InteractionWidget||InteractionScene||PairedPlayback||!IsValid(PreviewActor)||IsValid(PreparingPreview))return;
 // The neighbor goes before the clothing previews (browsing the list), so only a build already under way waits.
 if(ClothingWarmReady)return;
 if(IsValid(NeighborActor)){if(!NeighborActor->bPlaybackMaterialsReady)NeighborActor->PreparePlaybackMaterials(false);return;}
 if(NeighborJob){
  // Build the prepared session's character, one batch per frame.
  auto Job=NeighborJob;FString Error;bool Ok=false;
  AKKPreviewActor* Actor=Job->Actor.Get();
  if(!Actor){
   Actor=GetWorld()->SpawnActor<AKKPreviewActor>(PreviewActor->GetActorLocation(),PreviewActor->GetActorRotation());
   if(Actor){Job->Actor=Actor;Actor->SetActorHiddenInGame(true);Actor->SetActorTickEnabled(false);Actor->Session=MoveTemp(Job->Session);Ok=Actor->BeginSessionLoad(Error);}
  }else Ok=Actor->ContinueSessionLoad(Error);
  if(Actor)Actor->SuspendDisplayTicks(true);
  if(!Ok){if(Actor)Actor->Destroy();Job->Actor=nullptr;NeighborJob.Reset();UE_LOG(LogTemp,Warning,TEXT("OK neighbor preload display %s: %s"),*Job->Path,*Error);}
  else if(Actor->IsSessionLoadComplete()){
   Actor->SetActorTickEnabled(false);NeighborActor=Actor;
   UE_LOG(LogTemp,Display,TEXT("OK neighbor preload ready %s"),*Job->Path);
  }
  return;
 }
 // NeighborPath set without a job: started (in flight) or failed for this card; nothing more to do for it.
 if(bNeighborInFlight||!NeighborPath.IsEmpty())return;
 UKKGameRoot* Root=bIndependentCards?nullptr:UKKGameRoot::Get();
 if(!bIndependentCards&&!Root)return;
 const FString OpenPath=bIndependentCards?UKKCardWorkspace::PortableSourcePath(Target):Target;
 FKKCharacterFile Card;FString Error;
 NeighborPath=Target;
 if(OpenPath.IsEmpty()){UE_LOG(LogTemp,Warning,TEXT("OK neighbor preload %s: no portable source"),*Target);return;}
 if(!UKKCardReader::LoadFromFile(Target,Card,Error)){UE_LOG(LogTemp,Warning,TEXT("OK neighbor preload card %s: %s"),*Target,*Error);return;}
 auto Job=MakeShared<FOKNeighborJob,ESPMode::ThreadSafe>();
 Job->Path=Target;Job->OpenPath=OpenPath;Job->Mods=FOkSession::DecodeEditorMods(Card.RawBlocks.FindRef(TEXT("KKEx")));
 Job->Physics=FVector2D(Card.Body.BustSoftness,Card.Body.BustWeight);Job->ShapeFace=Card.Face.ShapeValueFace;Job->ShapeBody=Card.Body.ShapeValueBody;
 bNeighborInFlight=true;
 const int32 Generation=NeighborGeneration;const FString Game=Root?Root->Root:FString();const bool PrepareSkeleton=OKLiveGpuSkinRequested();
 TWeakObjectPtr<UOKCharacterController> Weak(this);
 Async(EAsyncExecution::Thread,[Weak,Generation,Game,Job,PrepareSkeleton](){
  FOkSession::InitBindings();
  const double Begin=FPlatformTime::Seconds();
  // Speculative: gives way to a card the user opens meanwhile (the open fails and is retried when idle).
  FOkSession::SetThreadBackground(true);
  uint8 Worn[9]{};
  Job->Ok=Job->Session.Open(Game,Job->OpenPath,0,Job->Error,Worn);
  FOkSession::SetThreadBackground(false);
  if(Job->Ok){
   Job->Session.ApplyBodyPhysics(Job->Physics.X,Job->Physics.Y);if(!Job->Mods.IsEmpty())Job->Session.ApplyEditorMods(Job->Mods,0);
   if(!Job->ShapeBody.IsEmpty()&&(Job->ShapeFace!=Job->Session.ShapeFace()||Job->ShapeBody!=Job->Session.ShapeBody()))Job->Session.ApplyShape(Job->ShapeBody,Job->ShapeFace);
   AKKPreviewActor::PrecacheSessionTextures(Job->Session,PrepareSkeleton);
   if(!Job->Session.IsPortable()){Job->Session.ClipSources();Job->Session.ClipNames();}
   Job->PairJson=Job->Session.NativeAnimationPairs();
  }
  UE_LOG(LogTemp,Display,TEXT("OK neighbor preload worker %s ok=%d ms=%.1f %s"),*Job->Path,Job->Ok?1:0,(FPlatformTime::Seconds()-Begin)*1000.,*Job->Error);
  AsyncTask(ENamedThreads::GameThread,[Weak,Generation,Job](){
   UOKCharacterController* G=Weak.Get();if(!G)return;
   G->bNeighborInFlight=false;
   if(Generation!=G->NeighborGeneration)return;
   if(Job->Ok)G->NeighborJob=Job;
   // Preempted by the user's own load: try again when idle.
   else if(Job->Error.Contains(TEXT("preempted")))G->NeighborPath.Reset();
  });
 });
}

bool UOKCharacterController::NeighborPreloadPending() const{
 if(!NeighborEnabled()||IsValid(NeighborActor))return false;
 // A neighbor in flight or being built; NeighborPath alone (no job, nothing in flight) is a card that failed.
 if(bNeighborInFlight||NeighborJob.IsValid())return true;
 if(!NeighborPath.IsEmpty()||PreviewCardPath.IsEmpty())return false;
 // Not started yet: only when the shown card has a next one.
 for(int32 I=0;I+1<Entries.Num();++I)if(Entries[I].Path==PreviewCardPath)return true;
 return false;
}

bool UOKCharacterController::TryNeighborPreload(){
 if(!NeighborEnabled()||!IsValid(NeighborActor)||!NeighborJob||!Entries.IsValidIndex(SelectedIndex))return false;
 if(NeighborPath!=Entries[SelectedIndex].Path||OutfitIndex!=0)return false;
 for(uint8 State:ClothesStates)if(State!=0)return false;
 AKKPreviewActor* Actor=NeighborActor;const auto Job=NeighborJob;
 NeighborActor=nullptr;NeighborJob.Reset();NeighborPath.Reset();++NeighborGeneration;
 // As StartLoad3D and FinishLoad3D would: the current character stays until FinishPreparedPreview replaces it.
 if(PreviewActor){if(StalePreview&&StalePreview!=PreviewActor)StalePreview->Destroy();StalePreview=PreviewActor;PreviewActor=nullptr;}
 const int32 Generation=++LoadGeneration;AppearanceGeometryLoadGeneration=INDEX_NONE;
 SetLoadBusy(true);
 PendingLoadPairJson=Job->PairJson;PendingLoadPairGeneration=Generation;
 Actor->SuspendDisplayTicks(false);
 if(Job->Physics!=FVector2D(Selected.Body.BustSoftness,Selected.Body.BustWeight))Actor->Session.ApplyBodyPhysics(Selected.Body.BustSoftness,Selected.Body.BustWeight);
 // Edits made after selecting the card are applied to the shown character on the next tick (ApplyPendingEditorMods).
 bEditorModsDirty=EditorModsJson()!=Job->Mods;
 UE_LOG(LogTemp,Display,TEXT("OK neighbor preload hit %s"),*Job->Path);
 FinishPreparedPreview(Actor,true,FString());
 return true;
}
