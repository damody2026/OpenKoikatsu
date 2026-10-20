#include "../OKGalleryWidget.h"
#include "../OKShapeSlider.h"
#include "KKPreviewActor.h"
#include "ok_extract.h"
#include "Misc/Paths.h"
#include "Components/ComboBoxString.h"
#include "HAL/IConsoleManager.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "TimerManager.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/UObjectIterator.h"
#include "UnrealClient.h"
#include "ShaderCompiler.h"
#if WITH_DEV_AUTOMATION_TESTS
namespace OKEditorModsAudit {
struct FRun:TSharedFromThis<FRun>{
 TWeakObjectPtr<UWorld> World;TWeakObjectPtr<UOKGalleryWidget> Gallery;FTimerHandle Timer;
 FString Card,Output,Key;int32 Stage=0;double Start=FPlatformTime::Seconds(),Changed=0.;
 FVector LeftBefore,RightBefore;uint64 Samples=0;TArray<TSharedPtr<FJsonValue>> Cases;
 FVector Scale(AKKPreviewActor* Actor,const TCHAR* Name){for(int32 I=0;I<Actor->Session.SkelBoneCount();++I){OkBoneView Bone{};if(Actor->Session.SkelBoneAt(I,Bone)&&Bone.name&&FString(UTF8_TO_TCHAR(Bone.name))==Name)return FVector(Bone.pose_scl[0],Bone.pose_scl[1],Bone.pose_scl[2]);}return FVector::ZeroVector;}
 void Add(const TCHAR* Name){auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("case"),Name);Row->SetBoolField(TEXT("valid"),true);Cases.Add(MakeShared<FJsonValueObject>(Row));}
 void Stop(const FString& Error){auto Keep=AsShared();if(auto* W=World.Get())W->GetTimerManager().ClearTimer(Timer);auto Root=MakeShared<FJsonObject>();Root->SetBoolField(TEXT("valid"),Error.IsEmpty());Root->SetStringField(TEXT("error"),Error);Root->SetArrayField(TEXT("cases"),Cases);FString Text;FJsonSerializer::Serialize(Root,TJsonWriterFactory<>::Create(&Text));FFileHelper::SaveStringToFile(Text,*Output);FPlatformMisc::RequestExitWithStatus(false,Error.IsEmpty()?0:1);}
 void Tick(){auto Keep=AsShared();auto* W=World.Get();if(!W||FPlatformTime::Seconds()-Start>240){Stop(TEXT("Editor mods audit timeout"));return;}auto* G=Gallery.Get();if(!G){for(TObjectIterator<UOKGalleryWidget> It;It;++It)if(It->GetWorld()==W&&It->PreviewImage){Gallery=*It;G=*It;break;}if(!G)return;}
  if(Stage==0){G->bIndependentCards=true;G->Entries.Reset();FKKCardListEntry Entry;Entry.Path=Card;Entry.FileName=FPaths::GetCleanFilename(Card);G->Entries.Add(Entry);G->OutfitIndex=0;for(uint8& S:G->ClothesStates)S=0;G->SelectIndex(0);G->OnLoad3D();Stage=1;return;}
  if(G->bLoadInFlight||G->bAnimationPreparing||G->PendingAnimationKind>=0)return;
  auto* A=G->PreviewActor.Get();if(!A||!A->Session.IsPortable()){Stop(TEXT("Portable preview missing"));return;}
  if(Stage==1){G->OnStopPair();A->SetPoseMode(0);G->OnResetAllShapes();G->ApplyPendingEditorMods();A->ApplyShapes(G->Selected.Face.ShapeValueFace,G->Selected.Body.ShapeValueBody);LeftBefore=Scale(A,TEXT("cf_s_shoulder02_L"));RightBefore=Scale(A,TEXT("cf_s_shoulder02_R"));
   UOKShapeSlider* Found=nullptr;for(UOKShapeSlider* Slider:G->BoneSliders)if(Slider->ModBone==TEXT("cf_s_shoulder02_L")&&Slider->ModAxis==-1){Found=Slider;break;}
   if(!Found||G->BoneSliders.Num()<1000||LeftBefore.IsNearlyZero()){Stop(TEXT("Maker control registry missing"));return;}
   Found->ModSide->SetSelectedIndex(1);G->OnBoneChanged(Found,1.35f);G->ApplyPendingEditorMods();Changed=FPlatformTime::Seconds();Stage=2;return;
  }
  if(FPlatformTime::Seconds()-Changed<.5)return;
  if(Stage==2){if(!Scale(A,TEXT("cf_s_shoulder02_L")).Equals(LeftBefore*1.35,.0001)||!Scale(A,TEXT("cf_s_shoulder02_R")).Equals(RightBefore,.0001)){Stop(TEXT("Left/right rendered bone modifiers differ"));return;}Add(TEXT("left_scale_without_changing_right"));
   G->BoneCategoryCombo->SetSelectedOption(TEXT("骨骼調整：上半身"));FScreenshotRequest::RequestScreenshot(FPaths::ChangeExtension(Output,TEXT(""))+TEXT("_abmx.png"),true,false);Changed=FPlatformTime::Seconds();Stage=9;return;
  }
  if(Stage==9){
   G->BoneCategoryCombo->SetSelectedOption(TEXT("腹部形變"));G->PregnancyPreset->SetSelectedOption(TEXT("標準"));G->ApplyPendingEditorMods();Changed=FPlatformTime::Seconds();Stage=3;return;
  }
  if(Stage==3){if(GShaderCompilingManager&&GShaderCompilingManager->IsCompiling())return;if(A->Parts.IsEmpty()||A->SkelFollowers.IsEmpty()){Stop(TEXT("Belly rebuild lost GPU display"));return;}Add(TEXT("pregnancy_rebuild_keeps_gpu_display"));
   FScreenshotRequest::RequestScreenshot(FPaths::ChangeExtension(Output,TEXT("png")),true,false);
   Changed=FPlatformTime::Seconds();Stage=8;return;
  }
  if(Stage==8){
   const FString Option=G->DemoAnimationOptions.IsValidIndex(1)?G->DemoAnimationOptions[1]:FString();if(Option.IsEmpty()){Stop(TEXT("Jump demo missing"));return;}
   Key=G->GameRawClipKeys.FindRef(Option);Samples=A->AnimationTickSamples;G->GameClipCombo->SetSelectedOption(Option);Changed=FPlatformTime::Seconds();Stage=4;return;
  }
  if(Stage==4){if(A->AnimationTickSamples<=Samples)return;if(G->SelectedGameClip!=Key||A->PoseMode!=2){Stop(TEXT("Animation lost during mod editing"));return;}Add(TEXT("animation_plays_with_mods"));G->OnClothesState(101);Changed=FPlatformTime::Seconds();Stage=5;return;}
  if(Stage==5){if(G->SelectedGameClip!=Key||A->PoseMode!=2){Stop(TEXT("Half removal lost animation"));return;}Add(TEXT("half_removal_preserves_mods_and_animation"));G->OnClothesState(100);Changed=FPlatformTime::Seconds();Stage=6;return;}
  if(Stage==6){G->OnResetAllShapes();G->ApplyPendingEditorMods();Changed=FPlatformTime::Seconds();Stage=7;return;}
  if(Stage==7){TSharedPtr<FJsonObject> Mods;FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(G->EditorModsJson()),Mods);if(!Mods||Mods->GetObjectField(TEXT("pregnancy"))->GetNumberField(TEXT("inflationSize"))!=0||Mods->GetArrayField(TEXT("abm")).ContainsByPredicate([](const TSharedPtr<FJsonValue>& Bone){return Bone->AsObject()->GetIntegerField(TEXT("location"))<10;})||G->SelectedGameClip!=Key){Stop(TEXT("Reset did not restore mod defaults or preserve animation"));return;}Add(TEXT("reset_all_restores_original_defaults"));Stop(FString());}
 }
};
}
static FAutoConsoleCommandWithWorld EditorModsAudit(TEXT("ok.AuditEditorMods"),TEXT("Audit maker mod controls and GPU preview through actual editor callbacks"),FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* W){
 const FString Card=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_GUI_RELOAD_CARD")),Output=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_GUI_RELOAD_OUTPUT"));if(!W||Card.IsEmpty()||Output.IsEmpty())return;
 auto Run=MakeShared<OKEditorModsAudit::FRun>();Run->World=W;Run->Card=Card;Run->Output=Output;W->GetTimerManager().SetTimer(Run->Timer,FTimerDelegate::CreateLambda([Run](){Run->Tick();}),.1f,true);
}));
#endif
