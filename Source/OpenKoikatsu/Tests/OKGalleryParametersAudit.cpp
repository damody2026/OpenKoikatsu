#include "../OKGalleryWidget.h"
#include "../OKInteractionScene.h"
#include "../OKShapeSlider.h"
#include "KKPreviewActor.h"
#include "KKCardReader.h"
#include "ok_extract.h"
#include "Components/ComboBoxString.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "TimerManager.h"
#include "UObject/UObjectIterator.h"
#if WITH_DEV_AUTOMATION_TESTS
namespace OKGalleryParametersAudit {
struct FRun:TSharedFromThis<FRun>{
 TWeakObjectPtr<UWorld> World;TWeakObjectPtr<UOKGalleryWidget> Gallery;FTimerHandle Timer;
 FString Cards[2],Output;FKKCharacterFile Originals[2];FVector Scales[2];TWeakObjectPtr<AKKPreviewActor> Current;
 TArray<float> EditedBody,EditedFace;FString EditedMods;int32 Stage=0;double Started=FPlatformTime::Seconds(),At=0.;TArray<TSharedPtr<FJsonValue>> Cases;
 FVector Scale(AKKPreviewActor* A){for(int32 I=0;I<A->Session.SkelBoneCount();++I){OkBoneView B{};if(A->Session.SkelBoneAt(I,B)&&B.name&&FString(UTF8_TO_TCHAR(B.name))==TEXT("cf_s_shoulder02_L"))return FVector(B.pose_scl[0],B.pose_scl[1],B.pose_scl[2]);}return FVector::ZeroVector;}
 void Add(const TCHAR* Name){auto J=MakeShared<FJsonObject>();J->SetStringField(TEXT("case"),Name);J->SetBoolField(TEXT("valid"),true);Cases.Add(MakeShared<FJsonValueObject>(J));}
 void Stop(const FString& Error){auto Keep=AsShared();if(auto* W=World.Get())W->GetTimerManager().ClearTimer(Timer);auto J=MakeShared<FJsonObject>();J->SetBoolField(TEXT("valid"),Error.IsEmpty());J->SetStringField(TEXT("error"),Error);J->SetNumberField(TEXT("stage"),Stage);J->SetArrayField(TEXT("cases"),Cases);FString Text;FJsonSerializer::Serialize(J,TJsonWriterFactory<>::Create(&Text));const bool Saved=FFileHelper::SaveStringToFile(Text,*Output);UE_LOG(LogTemp,Display,TEXT("Gallery parameters audit valid=%d stage=%d error=%s"),Error.IsEmpty()&&Saved,Stage,*Error);FPlatformMisc::RequestExitWithStatus(false,Error.IsEmpty()&&Saved?0:1);}
 bool Values(UOKGalleryWidget* G,const TArray<float>& Body,const TArray<float>& Face,const FString& Mods){auto* A=G->PreviewActor.Get();return A&&A->Session.IsOpen()&&G->Selected.Body.ShapeValueBody==Body&&G->Selected.Face.ShapeValueFace==Face&&A->Session.ShapeBody()==Body&&A->Session.ShapeFace()==Face&&G->EditorModsJson()==Mods&&!G->bEditorModsDirty&&!A->bShapeDirty;}
 bool Original(UOKGalleryWidget* G,int32 Index,bool Bones=true){const auto& O=Originals[Index];return FPaths::IsSamePath(G->PreviewCardPath,Cards[Index])&&Values(G,O.Body.ShapeValueBody,O.Face.ShapeValueFace,FOkSession::DecodeEditorMods(O.RawBlocks.FindRef(TEXT("KKEx"))))&&G->Selected.Body.BustSoftness==O.Body.BustSoftness&&G->Selected.Body.BustWeight==O.Body.BustWeight&&(!Bones||Scale(G->PreviewActor.Get()).Equals(Scales[Index],.0001));}
 bool Edit(UOKGalleryWidget* G){auto* A=G->PreviewActor.Get();G->OnShapeChanged(true,0,G->Selected.Body.ShapeValueBody[0]<.6f?.79f:.23f);G->OnShapeChanged(false,0,G->Selected.Face.ShapeValueFace[0]<.6f?.73f:.27f);G->OnBodyPhysicsChanged(TEXT("bustSoftness"),.83f);G->OnBodyPhysicsChanged(TEXT("bustWeight"),.21f);
  UOKShapeSlider* Bone=nullptr;for(UOKShapeSlider* S:G->BoneSliders)if(S->ModBone==TEXT("cf_s_shoulder02_L")&&S->ModAxis==-1){Bone=S;break;}if(!Bone)return false;
  Bone->ModSide->SetSelectedIndex(1);G->OnBoneChanged(Bone,1.25f);G->ApplyPendingEditorMods();A->ApplyShapes(G->Selected.Face.ShapeValueFace,G->Selected.Body.ShapeValueBody);EditedBody=G->Selected.Body.ShapeValueBody;EditedFace=G->Selected.Face.ShapeValueFace;EditedMods=G->EditorModsJson();return true;}
 void Next(int32 S){Stage=S;At=FPlatformTime::Seconds();}
 void Tick(){auto Keep=AsShared();auto* W=World.Get();if(!W||FPlatformTime::Seconds()-Started>420){Stop(TEXT("Navigation/parameter audit timeout"));return;}
  auto* G=Gallery.Get();if(!G){for(TObjectIterator<UOKGalleryWidget> It;It;++It)if(It->GetWorld()==W&&It->PreviewImage){Gallery=*It;G=*It;break;}if(!G)return;}
  if(Stage==0){FString Error;for(int32 I=0;I<2;++I)if(!UKKCardReader::LoadFromFile(Cards[I],Originals[I],Error)){Stop(Error);return;}G->bIndependentCards=true;G->Entries.Reset();for(int32 I=0;I<2;++I){FKKCardListEntry E;E.Path=Cards[I];E.FileName=FPaths::GetCleanFilename(Cards[I]);G->Entries.Add(E);}FKKCardListEntry Invalid;Invalid.Path=Cards[0]+TEXT(".missing");G->Entries.Add(Invalid);G->SelectIndex(0);G->OnLoad3D();Next(1);return;}
  if(G->bLoadInFlight||G->bAppearanceBusy||G->bAnimationPreparing||FPlatformTime::Seconds()-At<.3)return;
  if(!G->PreviewActor||!G->PreviewActor->Session.IsPortable()){Stop(TEXT("Portable actor missing"));return;}
  switch(Stage){
   case 1:
    if(!Original(G,0,false)){Stop(TEXT("Initial card parameters mismatch"));return;}Scales[0]=Scale(G->PreviewActor.Get());Current=G->PreviewActor;Add(TEXT("initial_saved_parameters"));if(!Edit(G)){Stop(TEXT("Mod bone control absent"));return;}Next(2);return;
   case 2:
    if(!Values(G,EditedBody,EditedFace,EditedMods)||Scale(G->PreviewActor.Get()).Equals(Scales[0],.0001)){Stop(TEXT("Edits not applied to native model"));return;}Add(TEXT("body_face_physics_mod_edits"));G->OnEnterInteraction();Next(3);return;
   case 3:
    if(!G->InteractionWidget||!G->InteractionScene||G->GetVisibility()!=ESlateVisibility::Collapsed||!G->PreviewActor->bUsePrivateGpuPlayback||!Values(G,EditedBody,EditedFace,EditedMods)){Stop(TEXT("Edited interaction entry failed"));return;}Add(TEXT("editor_to_interaction_preserves_edits"));G->InteractionWidget->OnReturn();Next(4);return;
   case 4:
    if(G->InteractionWidget||G->InteractionScene||!G->bModelLayout||G->PreviewActor->bUsePrivateGpuPlayback||G->PreviewActor!=Current.Get()||!Values(G,EditedBody,EditedFace,EditedMods)){Stop(TEXT("Interaction return lost parameters or ownership"));return;}Add(TEXT("interaction_to_editor_preserves_edits"));G->OnLoad3D();Next(5);return;
   case 5:
    if(!Values(G,EditedBody,EditedFace,EditedMods)){Stop(TEXT("Editor reload lost edits"));return;}G->OnBackToGallery();G->OnLoad3D();Next(6);return;
   case 6:
    if(!G->bModelLayout||!Values(G,EditedBody,EditedFace,EditedMods)){Stop(TEXT("Gallery roundtrip lost edits"));return;}Add(TEXT("reload_and_gallery_roundtrip_preserve_edits"));
    // Queue a final slider update, then reselect before its render tick.
    G->OnShapeChanged(true,0,.91f);G->OnBackToGallery();G->SelectIndex(0);G->OnLoad3D();Next(7);return;
   case 7:
    if(!Original(G,0)||G->PreviewActor!=Current.Get()){Stop(TEXT("Same-card reused actor did not restore saved parameters"));return;}Add(TEXT("same_card_reselection_restores_saved_model_and_mods"));if(!Edit(G)){Stop(TEXT("Second edit failed"));return;}G->OnBackToGallery();G->SelectIndex(0);G->OnEnterInteraction();Next(8);return;
   case 8:
    if(!G->InteractionWidget||!Original(G,0)){Stop(TEXT("Card-page interaction retained stale edits"));return;}Add(TEXT("card_page_interaction_restores_saved_parameters"));G->InteractionWidget->OnReturn();G->OnBackToGallery();G->SelectIndex(1);G->OnLoad3D();Next(9);return;
   case 9:
    if(!Original(G,1,false)||G->PreviewActor==Current.Get()){Stop(TEXT("New card inherited previous card parameters"));return;}Scales[1]=Scale(G->PreviewActor.Get());Add(TEXT("different_card_loads_own_saved_parameters"));if(!Edit(G)){Stop(TEXT("New card edit failed"));return;}Next(10);return;
   case 10:
    if(!Values(G,EditedBody,EditedFace,EditedMods)){Stop(TEXT("New card edits failed"));return;}
    if(G->ClothingPreviews.IsEmpty())return;for(const auto& P:G->ClothingPreviews)if(!P.Value->bPlaybackMaterialsReady)return;
    G->OnClothesState(101);Next(11);return;
   case 11:
    if(!Values(G,EditedBody,EditedFace,EditedMods)){Stop(TEXT("Clothing transition lost edits"));return;}
    {auto* Worn=G->ClothingPreviews.Find(G->ClothingPreviewContext+TEXT("|clothes=0,0,0,0,0,0,0,0,0"));if(!Worn||!IsValid(*Worn)||!(*Worn)->bPlaybackMaterialsReady)return;}
    Add(TEXT("clothing_transition_preserves_current_edits"));G->OnBackToGallery();G->SelectIndex(1);G->OnEnterInteraction();Next(12);return;
   case 12:
    if(!G->InteractionWidget||!G->bLastClothingSwitchCached||!Original(G,1)){Stop(TEXT("Cached clothing entry did not enter interaction with saved parameters"));return;}Add(TEXT("cached_actor_restores_card_and_enters_interaction"));G->InteractionWidget->OnReturn();G->OnBackToGallery();G->SelectIndex(0);G->OnEnterInteraction();Next(13);return;
   case 13:
    if(!G->InteractionWidget||!Original(G,0)){Stop(TEXT("Switch back to first card interaction failed"));return;}Add(TEXT("new_card_direct_interaction_and_return_to_first_card"));G->InteractionWidget->OnReturn();G->OnBackToGallery();Current=G->PreviewActor;G->SelectIndex(2);Next(14);return;
   case 14:
    if(G->SelectedIndex!=0||G->PreviewActor!=Current.Get()||!Original(G,0)){Stop(TEXT("Failed card load damaged current selection"));return;}Add(TEXT("failed_card_read_preserves_selection_and_model"));
    // Appearance/property jobs use a working copy under the original list row.
    G->SetWorkingCardPath(Cards[1]);G->SelectIndex(0);G->OnLoad3D();Next(15);return;
   case 15:
    if(!FPaths::IsSamePath(G->Entries[0].Path,Cards[0])||!G->CardSources.IsEmpty()||!Original(G,0)){Stop(TEXT("Working appearance copy replaced original card identity"));return;}Add(TEXT("appearance_working_copy_reselection_restores_original_source"));Stop(FString());return;
  }
 }
};
}
static FAutoConsoleCommandWithWorld AuditGalleryParameters(TEXT("ok.AuditGalleryParameters"),TEXT("Saved card parameters and editor/interaction navigation roundtrips"),FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* W){
 const FString Card=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_GUI_RELOAD_CARD")),Other=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_GUI_OTHER_CARD")),Output=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_GUI_RELOAD_OUTPUT"));if(!W||Card.IsEmpty()||Other.IsEmpty()||Output.IsEmpty()||IFileManager::Get().FileExists(*Output)){FPlatformMisc::RequestExitWithStatus(false,1);return;}auto Run=MakeShared<OKGalleryParametersAudit::FRun>();Run->World=W;Run->Cards[0]=Card;Run->Cards[1]=Other;Run->Output=Output;W->GetTimerManager().SetTimer(Run->Timer,FTimerDelegate::CreateLambda([Run](){Run->Tick();}),.1f,true);
}));
#endif
