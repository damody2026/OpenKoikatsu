#include "../OKGalleryWidget.h"
#include "KKCardWorkspace.h"
#include "KKGameRoot.h"
#include "KKPreviewActor.h"
#include "Components/ComboBoxString.h"
#include "Components/EditableTextBox.h"
#include "UObject/UObjectIterator.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#if WITH_DEV_AUTOMATION_TESTS
namespace OKCardSourceAudit {
struct FRun:TSharedFromThis<FRun>{
 TWeakObjectPtr<UWorld> World;FTimerHandle Timer;double Started=FPlatformTime::Seconds(),LoadAt=0,FirstMs=0,SecondMs=0;TWeakObjectPtr<AKKPreviewActor> FirstActor;int32 Stage=0,Count=0;
 FString Directory,Output,PreviousRoot;
 void Stop(const FString& Error){const auto Keep=AsShared();if(auto* W=World.Get())W->GetTimerManager().ClearTimer(Timer);UKKGameRoot::Get()->Root=PreviousRoot;
 const FString Report=FString::Printf(TEXT("valid=%d error=%s portable_cards=%d apng_first_load_ms=%.3f apng_second_load_ms=%.3f game_root_empty=1\n"),Error.IsEmpty(),*Error,Count,FirstMs,SecondMs);FFileHelper::SaveStringToFile(Report,*(Output/TEXT("report.txt")));UE_LOG(LogTemp,Display,TEXT("Card source audit %s"),*Report);FPlatformMisc::RequestExitWithStatus(false,Error.IsEmpty()?0:1);}
 void Tick(){const auto Keep=AsShared();if(FPlatformTime::Seconds()-Started>180){Stop(TEXT("Timeout"));return;}auto* W=World.Get();if(!W){Stop(TEXT("World missing"));return;}
 UOKGalleryWidget* G=nullptr;for(TObjectIterator<UOKGalleryWidget> It;It;++It)if(It->GetWorld()==W&&It->CardSourceCombo){G=*It;break;}if(!G)return;
 if(Stage==0){if(!G->bIndependentCards||!FPaths::IsSamePath(G->CurrentCardDirectory,Directory)){Stop(TEXT("Startup APNG directory ignored"));return;}
 G->CardSourceCombo->SetSelectedOption(TEXT("KKS 遊戲目錄卡片"));if(G->bIndependentCards||!G->CurrentCardDirectory.IsEmpty()){Stop(TEXT("Environment overrides KKS source choice"));return;}
 G->CardSourceCombo->SetSelectedOption(TEXT("APNG 內嵌資源卡片"));if(!G->bIndependentCards||!FPaths::IsSamePath(G->CurrentCardDirectory,Directory)||G->Entries.IsEmpty()){Stop(TEXT("APNG directory not remembered"));return;}
 for(const auto& E:G->Entries)if(UKKCardWorkspace::PortableSourcePath(E.Path).IsEmpty()){Stop(TEXT("Non-portable card listed in APNG mode"));return;}Count=G->Entries.Num();
 UKKGameRoot::Get()->Root.Empty();G->SelectIndex(0);LoadAt=FPlatformTime::Seconds();G->OnLoad3D();if(!G->bLoadInFlight||G->CardSourceCombo->GetIsEnabled()){Stop(TEXT("Load did not disable source selection"));return;}Stage=1;return;}
 if(G->bLoadInFlight)return;
 if(!G->PreviewActor||!G->PreviewActor->Session.IsOpen()||!G->PreviewActor->Session.IsPortable()||G->PreviewActor->Parts.IsEmpty()){Stop(TEXT("APNG failed to load without game root"));return;}
 if(Stage==1){FirstMs=(FPlatformTime::Seconds()-LoadAt)*1000;FirstActor=G->PreviewActor;
 G->GameSourceCombo->SetSelectedOption(TEXT("studio/anime/00.unity3d"));
 if(G->SelectedGameSource!=TEXT("studio/anime/00.unity3d")||G->GameClipCombo->FindOptionIndex(TEXT("f_aruki_00_00_01"))<=0){Stop(TEXT("Offline shared source menu unavailable"));return;}
 G->GameClipCombo->SetSelectedOption(TEXT("f_aruki_00_00_01"));
 if(G->SelectedGameClip!=TEXT("studio/anime/00.unity3d|f_aruki_00_00_01")||G->PreviewActor->PoseMode!=2){Stop(TEXT("Offline shared clip selection did not start playback"));return;}
 const float Phase=G->PreviewActor->AnimPhase;
 if(G->PreviewActor->SetGameAnimation(TEXT("missing/source|unimported"))||G->PreviewActor->AnimPhase!=Phase){Stop(TEXT("Invalid shared clip changed playback"));return;}
 G->PreviewActor->SetPoseMode(0);LoadAt=FPlatformTime::Seconds();G->OnLoad3D();Stage=2;return;}
 if(Stage==2){SecondMs=(FPlatformTime::Seconds()-LoadAt)*1000;if(G->PreviewActor!=FirstActor){Stop(TEXT("Unchanged APNG rebuilt its actor"));return;}
 IFileManager::Get().SetTimeStamp(*G->Entries[G->SelectedIndex].Path,FDateTime::UtcNow()+FTimespan::FromSeconds(2));G->OnLoad3D();if(!G->bLoadInFlight){Stop(TEXT("Changed APNG failed to invalidate actor cache"));return;}Stage=3;return;}
 if(G->PreviewActor==FirstActor){Stop(TEXT("Changed APNG reused stale actor"));return;}
 if(!G->CardSourceCombo->GetIsEnabled()){Stop(TEXT("Source combo remained disabled after loading"));return;}Stop(FString());
 }
};
}
static FAutoConsoleCommandWithWorld CardSourceAudit(TEXT("ok.CardSourceAudit"),TEXT("APNG/KKS source switch and portable gallery loading audit"),FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* W){auto R=MakeShared<OKCardSourceAudit::FRun>();R->World=W;R->Directory=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_CARD_DIRECTORY"));R->Output=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_PAINT_OUTPUT"));R->PreviousRoot=UKKGameRoot::Get()->Root;if(!W||R->Directory.IsEmpty()||R->Output.IsEmpty()){R->Stop(TEXT("Missing audit environment"));return;}W->GetTimerManager().SetTimer(R->Timer,FTimerDelegate::CreateLambda([R](){R->Tick();}),.05f,true);}));
#endif
