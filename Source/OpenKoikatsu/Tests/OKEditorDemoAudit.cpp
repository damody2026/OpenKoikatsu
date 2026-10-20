#include "../OKGalleryWidget.h"
#include "KKPreviewActor.h"
#include "ok_extract.h"
#include "Misc/Paths.h"
#include "Components/ComboBoxString.h"
#include "HAL/IConsoleManager.h"
#include "HAL/FileManager.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/UObjectIterator.h"
#if WITH_DEV_AUTOMATION_TESTS
namespace OKEditorDemoAudit {
struct FRun : TSharedFromThis<FRun> {
 TWeakObjectPtr<UWorld> World;TWeakObjectPtr<UOKGalleryWidget> Gallery;FTimerHandle Timer;
 FString Card,Output,Key;TArray<FString> Options;int32 Stage=0,Index=0,ClothesIndex=0;uint64 Samples=0,Worn=0;
 double Start=FPlatformTime::Seconds();bool Waiting=false;TArray<TSharedPtr<FJsonValue>> Rows;
 uint64 Triangles(AKKPreviewActor* A){if(A->bClothingHidden)return 0;uint64 N=0;for(const auto& P:A->Parts)if(P.Role.StartsWith(TEXT("clothes"))){OkMeshView V{};if(A->Session.PartAt(P.SessionIndex,V)&&V.render_visible)N+=P.Triangles.Num()/3;}return N;}
 void Stop(const FString& Error){auto Keep=AsShared();if(auto* W=World.Get())W->GetTimerManager().ClearTimer(Timer);auto J=MakeShared<FJsonObject>();J->SetBoolField(TEXT("valid"),Error.IsEmpty());J->SetStringField(TEXT("error"),Error);J->SetStringField(TEXT("card"),Card);J->SetNumberField(TEXT("expected_demos"),Options.Num());J->SetNumberField(TEXT("played_demos"),Index);J->SetArrayField(TEXT("cases"),Rows);FString Text;FJsonSerializer::Serialize(J,TJsonWriterFactory<>::Create(&Text));const bool Saved=FFileHelper::SaveStringToFile(Text,*Output);UE_LOG(LogTemp,Display,TEXT("Editor demo audit valid=%d cases=%d error=%s"),Error.IsEmpty()&&Saved,Rows.Num(),*Error);FPlatformMisc::RequestExitWithStatus(false,Error.IsEmpty()&&Saved?0:1);}
 void Tick(){auto Keep=AsShared();auto* W=World.Get();if(!W||FPlatformTime::Seconds()-Start>1200){Stop(TEXT("Demo audit timed out"));return;}auto* G=Gallery.Get();if(!G){for(TObjectIterator<UOKGalleryWidget> It;It;++It)if(It->GetWorld()==W&&It->PreviewImage){Gallery=*It;G=*It;break;}if(!G)return;}
  if(Stage==0){G->bIndependentCards=true;G->Entries.Reset();FKKCardListEntry E;E.Path=Card;E.FileName=FPaths::GetCleanFilename(Card);G->Entries.Add(E);G->OutfitIndex=0;for(uint8& S:G->ClothesStates)S=0;G->SelectIndex(0);G->OnLoad3D();Stage=1;return;}
  if(G->bLoadInFlight||G->bAnimationPreparing||G->PendingAnimationKind>=0)return;
  auto* A=G->PreviewActor.Get();if(!A||!A->Session.IsPortable()){Stop(TEXT("Portable preview missing"));return;}
  if(Stage==1){if(G->DemoAnimationOptions.Num()<67){Stop(TEXT("Demo catalog incomplete"));return;}
   // Full acceptance selects every catalog entry through the same UI path.
   Options=G->DemoAnimationOptions;
   if(FPlatformMisc::GetEnvironmentVariable(TEXT("OK_DEMO_CLOTHES_ONLY"))==TEXT("1"))Options.SetNum(9);
   Worn=Triangles(A);Stage=2;}
  if(Stage==2){
   if(Waiting){if(A->AnimationTickSamples<=Samples)return;if(G->SelectedGameClip!=Key||A->PoseMode!=2||!FMath::IsFinite(A->AnimDuration)||A->AnimDuration<=0||A->GetActorTransform().ContainsNaN()){Stop(TEXT("Demo playback failed: ")+Options[Index]);return;}auto R=MakeShared<FJsonObject>();R->SetStringField(TEXT("demo"),Options[Index]);R->SetStringField(TEXT("key"),Key);R->SetNumberField(TEXT("duration"),A->AnimDuration);R->SetNumberField(TEXT("live_samples"),double(A->AnimationTickSamples-Samples));Rows.Add(MakeShared<FJsonValueObject>(R));++Index;Waiting=false;}
   if(Index<Options.Num()){Key=G->GameRawClipKeys.FindRef(Options[Index]);Samples=A->AnimationTickSamples;G->GameClipCombo->SetSelectedIndex(0);G->GameClipCombo->SetSelectedOption(Options[Index]);Waiting=true;return;}
   Key=G->SelectedGameClip;Stage=3;
  }
  if(Stage==3){const int32 Actions[]={101,102,100,0,0,0,100};if(ClothesIndex==UE_ARRAY_COUNT(Actions)){Stop(FString());return;}
   uint8 Expected[9];FString Error;if(!FOkSession::ClothingTransition(FString(),Card,G->OutfitIndex,G->ClothesStates,Actions[ClothesIndex],Expected,Error)){Stop(TEXT("Clothing rules failed: ")+Error);return;}
   G->OnClothesState(Actions[ClothesIndex]);for(int32 I=0;I<9;++I)if(G->ClothesStates[I]!=Expected[I]){Stop(TEXT("Clothing callback lost native state transition"));return;}
   // Already-worn requests reuse the current actor without a load in flight.
   Stage=4;return;}
  if(Stage==4){if(G->SelectedGameClip!=Key||A->PoseMode!=2){Stop(TEXT("Clothing reload lost demo playback"));return;}const uint64 Count=Triangles(A);if(ClothesIndex==1&&Count!=0){Stop(TEXT("Full removal left clothing visible"));return;}if((ClothesIndex==2||ClothesIndex==6)&&Count!=Worn){Stop(TEXT("Worn clothing did not restore"));return;}
   uint64 Top=0;for(const auto& P:A->Parts)if(P.Role==TEXT("clothes_top")){OkMeshView V{};if(A->Session.PartAt(P.SessionIndex,V)&&V.render_visible)Top+=P.Triangles.Num()/3;}
   if(G->ClothesStates[0]==3&&Top!=0){Stop(TEXT("Individual top removal left top geometry visible"));return;}
   auto R=MakeShared<FJsonObject>();R->SetNumberField(TEXT("clothing_case"),ClothesIndex);R->SetNumberField(TEXT("visible_clothing_triangles"),double(Count));R->SetNumberField(TEXT("visible_top_triangles"),double(Top));R->SetNumberField(TEXT("top_state"),G->ClothesStates[0]);R->SetNumberField(TEXT("bottom_state"),G->ClothesStates[1]);Rows.Add(MakeShared<FJsonValueObject>(R));++ClothesIndex;Stage=3;}
 }
};
}
static FAutoConsoleCommandWithWorld EditorDemoAudit(TEXT("ok.AuditEditorDemo"),TEXT("Verify original demo playback and actual clothing changes through editor callbacks"),FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* W){
 const FString Card=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_GUI_RELOAD_CARD")),Output=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_GUI_RELOAD_OUTPUT"));if(!W||Card.IsEmpty()||Output.IsEmpty()||IFileManager::Get().FileExists(*Output)){FPlatformMisc::RequestExitWithStatus(false,1);return;}auto Run=MakeShared<OKEditorDemoAudit::FRun>();Run->World=W;Run->Card=Card;Run->Output=Output;W->GetTimerManager().SetTimer(Run->Timer,FTimerDelegate::CreateLambda([Run](){Run->Tick();}),.1f,true);
}));
#endif
