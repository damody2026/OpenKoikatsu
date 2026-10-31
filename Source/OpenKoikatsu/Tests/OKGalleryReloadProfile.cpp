#include "../OKGalleryWidget.h"
#include "KKPreviewActor.h"
#include "KKGameRoot.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/PoseableMeshComponent.h"
#include "ProceduralMeshComponent.h"
#include "HAL/IConsoleManager.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "RenderingThread.h"
#include "ShaderCompiler.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/UObjectIterator.h"
#include "UnrealClient.h"
#if WITH_DEV_AUTOMATION_TESTS
namespace OKGalleryReloadProfile {
struct FRun : TSharedFromThis<FRun> {
 TWeakObjectPtr<UWorld> World;TWeakObjectPtr<UOKGalleryWidget> Gallery;
 TWeakObjectPtr<AKKPreviewActor> Previous;
 FString Card,Output;FTimerHandle Timer;TArray<TSharedPtr<FJsonValue>> Rows;
 double Start=FPlatformTime::Seconds(),Begin=0,Finished=0,GalleryFound=0;uint64 FinishFrame=0,HoldUntilFrame=0;
 int32 Run=0;bool Loading=false,Ready=false;
 void Stop(const FString& Error){
  const auto Keep=AsShared();if(auto* W=World.Get())W->GetTimerManager().ClearTimer(Timer);
  auto Json=MakeShared<FJsonObject>();Json->SetBoolField(TEXT("valid"),Error.IsEmpty());Json->SetStringField(TEXT("error"),Error);
  Json->SetStringField(TEXT("scope"),TEXT("Real Gallery SelectIndex/OnLoad3D/async FinishLoad3D, native session, card/outfit identity, shape parity, fresh actors and shader/four-frame/render-thread readiness. Excludes engine startup and initial scan. Not GPU presentation latency."));
  Json->SetArrayField(TEXT("runs"),Rows);FString Text;FJsonSerializer::Serialize(Json,TJsonWriterFactory<>::Create(&Text));
  const bool Saved=FFileHelper::SaveStringToFile(Text,*Output);
  UE_LOG(LogTemp,Display,TEXT("Gallery async reload valid=%d runs=%d saved=%d error=%s"),Error.IsEmpty()&&Saved?1:0,Rows.Num(),Saved?1:0,*Error);
  FPlatformMisc::RequestExitWithStatus(false,Error.IsEmpty()&&Saved?0:1);
 }
 void Step(){
  const auto Keep=AsShared();auto* W=World.Get();if(!W||FPlatformTime::Seconds()-Start>540){Stop(TEXT("World closed or reload timeout"));return;}
  auto* G=Gallery.Get();if(!G){for(TObjectIterator<UOKGalleryWidget> It;It;++It)if(It->GetWorld()==W&&It->PreviewImage){Gallery=*It;G=*It;break;}if(!G)return;}
  if(GFrameCounter<HoldUntilFrame)return; // the requested shot is drawn and saved first
  // OK_GUI_RELOAD_DELAY=<s>: the first load starts that long after the gallery appeared (as a user picks a card after
  // the startup prewarm instead of at once).
  if(Run==0&&!Loading&&!Ready){static const double Delay=FCString::Atod(*FPlatformMisc::GetEnvironmentVariable(TEXT("OK_GUI_RELOAD_DELAY")));if(GalleryFound==0)GalleryFound=FPlatformTime::Seconds();if(FPlatformTime::Seconds()-GalleryFound<Delay)return;}
  if(Ready){
   if(GShaderCompilingManager&&GShaderCompilingManager->IsCompiling())return;
   if(GFrameCounter<FinishFrame+4)return;FlushRenderingCommands();
   auto Row=MakeShared<FJsonObject>();Row->SetNumberField(TEXT("run"),Run+1);Row->SetNumberField(TEXT("gui_complete_ms"),(Finished-Begin)*1000.);
   Row->SetNumberField(TEXT("ready_ms"),(FPlatformTime::Seconds()-Begin)*1000.);Row->SetNumberField(TEXT("parts"),G->PreviewActor->Parts.Num());
   Row->SetNumberField(TEXT("catalogue_rows"),G->NativePairs.Num());
   if(G->NativePairs.IsEmpty()||G->PendingLoadPairGeneration!=INDEX_NONE){Stop(TEXT("Prepared pair catalogue missing or not consumed by active generation"));return;}
   // OK_GUI_RELOAD_SHOT=<png>: the first load's next drawn frame, for comparing two builds (taken while the second
   // load's worker still runs, so the first actor is what it shows).
   if(Run==0){const FString Shot=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_GUI_RELOAD_SHOT"));
    if(!Shot.IsEmpty()){FScreenshotRequest::RequestScreenshot(Shot,true,false);HoldUntilFrame=GFrameCounter+4;UE_LOG(LogTemp,Display,TEXT("Gallery reload shot requested %s"),*Shot);}}
   if(Run==0){
    auto* A=G->PreviewActor.Get();const bool Tick=A->IsActorTickEnabled(),Blink=A->bAutomaticBlink;A->SetActorTickEnabled(false);A->bAutomaticBlink=false;
    auto Set=[A](){A->SetMouthPattern(2,false);A->SetMouthOpen(.6f);A->SetFaceControl(1,1,.4f);A->SetFaceControl(2,1,.7f);};
    auto Snapshot=[A](){TArray<float> Values;for(const auto& Slot:A->MouthSlots){
     if(Slot.Comp){for(const auto* Patterns:{&Slot.Patterns,&Slot.EyePatterns,&Slot.BrowPatterns})for(const auto& Pair:*Patterns){Values.Add(Slot.Comp->GetMorphTarget(Pair.Key));Values.Add(Slot.Comp->GetMorphTarget(Pair.Value));}Values.Add(Slot.Comp->IsVisible()?1.f:0.f);}
     if(Slot.CpuComp){for(int32 S=0;S<Slot.CpuComp->GetNumSections();++S)if(auto* Section=Slot.CpuComp->GetProcMeshSection(S))for(const auto& V:Section->ProcVertexBuffer){Values.Add(V.Position.X);Values.Add(V.Position.Y);Values.Add(V.Position.Z);}Values.Add(Slot.CpuComp->IsVisible()?1.f:0.f);}
    }return Values;};
    Set();const auto Immediate=Snapshot();A->SetMouthOpen(0.f);A->SetFaceControl(1,0,1.f);A->SetFaceControl(2,0,1.f);
    A->BeginFaceDisplayUpdate();A->BeginFaceDisplayUpdate();Set();A->EndFaceDisplayUpdate();A->EndFaceDisplayUpdate();
    const auto Batched=Snapshot();if(Immediate.IsEmpty()||Immediate!=Batched||A->FaceDisplayBatchDepth!=0||A->bFaceDisplayPending){Stop(TEXT("Batched face display differs from immediate morph/geometry/visibility"));return;}
    UE_LOG(LogTemp,Display,TEXT("Gallery face batch parity valid=1 values=%d nested=1"),Immediate.Num());
    auto HandPose=[A](){TArray<FTransform> Pose;if(A->BodySkel)Pose.Append(A->BodySkel->BoneSpaceTransforms);for(UPoseableMeshComponent* F:A->SkelFollowers)if(F)Pose.Append(F->BoneSpaceTransforms);return Pose;};
    if(A->GetHandShapeCount()>2){
     const float Clock=A->AnimPhase,Face=A->FaceClock;A->SetHandShape(0,true,1,2,.3f);A->SetHandShape(1,true,2,1,.6f);const auto ImmediatePose=HandPose();
     A->SetHandShape(0,false,0,0,0.f);A->SetHandShape(1,false,0,0,0.f);
     A->BeginHandDisplayUpdate();A->SetHandShape(0,true,1,2,.3f);A->SetHandShape(1,true,2,1,.6f);A->EndHandDisplayUpdate();const auto BatchedPose=HandPose();
     bool Same=ImmediatePose.Num()>0&&ImmediatePose.Num()==BatchedPose.Num();for(int32 I=0;Same&&I<ImmediatePose.Num();++I)Same=ImmediatePose[I].Equals(BatchedPose[I],0.f);
     if(!Same||Clock!=A->AnimPhase||Face!=A->FaceClock||A->HandDisplayBatchDepth!=0||A->bHandDisplayPending){Stop(TEXT("Batched hands changed final pose or animation clocks"));return;}
     UE_LOG(LogTemp,Display,TEXT("Gallery hand batch parity valid=1 bones=%d clocks=1"),ImmediatePose.Num());
    }else{Stop(TEXT("Hand parity fixture needs multiple patterns"));return;}
    A->bAutomaticBlink=Blink;A->SetActorTickEnabled(Tick);
   }
   Rows.Add(MakeShared<FJsonValueObject>(Row));UE_LOG(LogTemp,Display,TEXT("Gallery async reload run=%d identity=1 shape=1 fresh_actor=1 gui_ms=%.3f ready_ms=%.3f parts=%d"),Run+1,Row->GetNumberField(TEXT("gui_complete_ms")),Row->GetNumberField(TEXT("ready_ms")),G->PreviewActor->Parts.Num());
   Previous=G->PreviewActor;Ready=false;Loading=false;if(++Run==2){Stop(FString());return;}
   if(GFrameCounter<HoldUntilFrame)return;
  }
  if(Loading){
   if(G->bLoadInFlight)return;
   auto* A=G->PreviewActor.Get();
   if(!IsValid(A)||A==Previous.Get()||A->Parts.IsEmpty()||G->PreviewCardPath!=Card||G->PreviewOutfitIndex!=0||G->SelectedIndex!=0||G->OutfitIndex!=0||
      A->Session.ShapeFace()!=G->Selected.Face.ShapeValueFace||A->Session.ShapeBody()!=G->Selected.Body.ShapeValueBody){Stop(TEXT("Async completion identity/shape/actor mismatch"));return;}
   Finished=FPlatformTime::Seconds();FinishFrame=GFrameCounter;Ready=true;return;
  }
  G->bIndependentCards=false;G->Entries.Reset();FKKCardListEntry Entry;Entry.Path=Card;Entry.FileName=FPaths::GetCleanFilename(Card);G->Entries.Add(Entry);
  Entry.Path=Card+TEXT(".blocked");G->Entries.Add(Entry);G->OutfitIndex=0;
  Begin=FPlatformTime::Seconds();G->SelectIndex(0);G->OnLoad3D();
  if(!G->bLoadInFlight){Stop(TEXT("GUI did not begin async load"));return;}
  G->SelectIndex(1);G->OnOutfitNext();G->OnOutfitPrev();
  if(G->SelectedIndex!=0||G->OutfitIndex!=0){Stop(TEXT("GUI in-flight selection was not protected"));return;}
  Loading=true;
 }
};
}
static FAutoConsoleCommandWithWorld GalleryReloadProfile(TEXT("ok.ProfileGalleryReload"),TEXT("Measure real GUI async reload; OK_GUI_RELOAD_CARD/ROOT/OUTPUT required"),FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* W){
 const FString Card=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_GUI_RELOAD_CARD")),Root=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_GUI_RELOAD_ROOT")),Output=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_GUI_RELOAD_OUTPUT"));
 if(!W||Card.IsEmpty()||Root.IsEmpty()||Output.IsEmpty()||IFileManager::Get().FileExists(*Output)){UE_LOG(LogTemp,Error,TEXT("Gallery reload requires world/card/root and fresh output"));FPlatformMisc::RequestExitWithStatus(false,1);return;}
 UKKGameRoot::Get()->Root=Root;auto Run=MakeShared<OKGalleryReloadProfile::FRun>();Run->World=W;Run->Card=Card;Run->Output=Output;
 W->GetTimerManager().SetTimer(Run->Timer,FTimerDelegate::CreateLambda([Run](){Run->Step();}),.05f,true);
}));
#endif
