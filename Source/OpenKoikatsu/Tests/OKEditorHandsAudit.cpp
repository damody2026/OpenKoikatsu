#include "../OKGalleryWidget.h"
#include "KKPreviewActor.h"
#include "ok_extract.h"
#include "Components/ComboBoxString.h"
#include "Components/Slider.h"
#include "Components/SizeBox.h"
#include "Containers/Ticker.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "ShaderCompiler.h"
#include "UObject/UObjectIterator.h"
#include "UnrealClient.h"
#if WITH_DEV_AUTOMATION_TESTS
namespace OKHandsAudit {
struct FRun:TSharedFromThis<FRun>{
 TWeakObjectPtr<UWorld> World;TWeakObjectPtr<UOKGalleryWidget> Gallery;FString Card,Output;int32 Stage=0;uint64 Samples=0;double Start=FPlatformTime::Seconds(),Ready=0;
 TArray<TSharedPtr<FJsonValue>> Cases;TMap<FString,FQuat> BaselineLeft,BaselineRight,Left,Right;
 TMap<FString,FQuat> Capture(UOKGalleryWidget* G,int32 Side){TMap<FString,FQuat> Result;auto* A=G->PreviewActor.Get();if(!A)return Result;for(int32 I=0;I<A->Session.SkelBoneCount();++I){OkBoneView V{};if(!A->Session.SkelBoneAt(I,V)||!V.name||V.layer!=1)continue;const FString N=UTF8_TO_TCHAR(V.name);bool Finger=false;for(const TCHAR* Stem:{TEXT("index"),TEXT("little"),TEXT("middle"),TEXT("ring"),TEXT("thumb")})for(int32 J=1;J<=3;++J)Finger|=N==FString::Printf(TEXT("cf_j_%s%02d_%s"),Stem,J,Side==0?TEXT("L"):TEXT("R"));if(Finger)Result.Add(N,FQuat(V.pose_rot[0],V.pose_rot[1],V.pose_rot[2],V.pose_rot[3]));}return Result;}
 bool Same(const TMap<FString,FQuat>& A,const TMap<FString,FQuat>& B){if(A.Num()!=15||B.Num()!=15)return false;for(const auto& V:A){const FQuat* Q=B.Find(V.Key);if(!Q||!Q->Equals(V.Value,0.00002))return false;}return true;}
 void Add(const TCHAR* Name){auto R=MakeShared<FJsonObject>();R->SetStringField(TEXT("case"),Name);R->SetBoolField(TEXT("valid"),true);Cases.Add(MakeShared<FJsonValueObject>(R));}
 bool Stop(const FString& Error){auto R=MakeShared<FJsonObject>();R->SetBoolField(TEXT("valid"),Error.IsEmpty());R->SetStringField(TEXT("error"),Error);R->SetArrayField(TEXT("cases"),Cases);if(auto* G=Gallery.Get())if(auto* A=G->PreviewActor.Get())R->SetNumberField(TEXT("hand_patterns"),A->GetHandShapeCount());FString Text;FJsonSerializer::Serialize(R,TJsonWriterFactory<>::Create(&Text));FFileHelper::SaveStringToFile(Text,*Output);FPlatformMisc::RequestExitWithStatus(false,Error.IsEmpty()?0:1);return false;}
 bool Tick(float){auto* W=World.Get();if(!W||FPlatformTime::Seconds()-Start>500)return Stop(TEXT("Hands audit timeout"));auto* G=Gallery.Get();if(!G){for(TObjectIterator<UOKGalleryWidget> It;It;++It)if(It->GetWorld()==W&&It->PreviewImage){Gallery=*It;G=*It;break;}if(!G)return true;}
  if(Stage==0){G->bIndependentCards=true;G->Entries.Reset();FKKCardListEntry E;E.Path=Card;E.FileName=FPaths::GetCleanFilename(Card);G->Entries.Add(E);G->SelectIndex(0);G->OnLoad3D();Stage=1;return true;}
  if(G->bLoadInFlight||G->bAnimationPreparing||G->PendingAnimationKind>=0){Ready=FPlatformTime::Seconds();return true;}auto* A=G->PreviewActor.Get();if(!A)return Stop(TEXT("Preview missing"));
  if(Stage==1){G->OnStopPair();A->SetPoseMode(0);G->OnResetLeftHand();G->OnResetRightHand();G->OnOpenHandControls();const int32 Count=A->GetHandShapeCount();if(Count<2||G->HandCombos.Num()!=4||G->HandCombos[0]->GetOptionCount()!=Count+1||G->MotionPanel->GetVisibility()!=ESlateVisibility::Visible)return Stop(TEXT("Independent hand controls missing"));if(G->HandCombos[1]->GetIsEnabled()||G->HandBlendSliders[1]->GetIsEnabled())return Stop(TEXT("Follow-animation blend must be disabled"));BaselineLeft=Capture(G,0);BaselineRight=Capture(G,1);bool Changed=false;
   for(int32 I=0;I<Count;++I){G->HandCombos[0]->SetSelectedIndex(I+1);if(!Same(Capture(G,1),BaselineRight))return Stop(TEXT("Left hand changed right fingers"));Changed|=!Same(Capture(G,0),BaselineLeft);}if(!Changed)return Stop(TEXT("Left finger poses did not change"));G->HandCombos[0]->SetSelectedIndex(2);Left=Capture(G,0);Add(TEXT("all_left_patterns_preserve_right"));Changed=false;
   for(int32 I=0;I<Count;++I){G->HandCombos[2]->SetSelectedIndex(I+1);if(!Same(Capture(G,0),Left))return Stop(TEXT("Right hand changed left fingers"));Changed|=!Same(Capture(G,1),BaselineRight);}if(!Changed)return Stop(TEXT("Right finger poses did not change"));G->HandCombos[2]->SetSelectedIndex(Count);Right=Capture(G,1);Add(TEXT("all_right_patterns_preserve_left"));
   G->HandCombos[1]->SetSelectedIndex(Count-1);G->HandBlendSliders[0]->OnValueChanged.Broadcast(.3f);if(!Same(Capture(G,1),Right))return Stop(TEXT("Left blend changed right fingers"));Left=Capture(G,0);G->HandCombos[3]->SetSelectedIndex(1);G->HandBlendSliders[1]->OnValueChanged.Broadcast(.8f);if(!Same(Capture(G,0),Left))return Stop(TEXT("Right blend changed left fingers"));Right=Capture(G,1);Add(TEXT("independent_hand_blends"));
   G->OnResetLeftHand();if(!Same(Capture(G,0),BaselineLeft)||!Same(Capture(G,1),Right)||G->SelectedHandA[1]!=Count-1)return Stop(TEXT("Single-side reset changed other hand"));Add(TEXT("left_reset_restores_animation_without_changing_right"));G->HandCombos[0]->SetSelectedIndex(2);G->HandCombos[1]->SetSelectedIndex(Count-1);G->HandBlendSliders[0]->OnValueChanged.Broadcast(.3f);Left=Capture(G,0);Ready=FPlatformTime::Seconds();Stage=2;return true;}
  if(Stage==2){if(GShaderCompilingManager&&GShaderCompilingManager->IsCompiling()){Ready=FPlatformTime::Seconds();return true;}if(FPlatformTime::Seconds()-Ready<2)return true;FScreenshotRequest::RequestScreenshot(FPaths::ChangeExtension(Output,TEXT("png")),true,false);Stage=3;return true;}
  if(Stage==3){if(G->DemoAnimationOptions.Num()<3)return Stop(TEXT("Demo catalog missing"));Samples=A->AnimationTickSamples;G->GameClipCombo->SetSelectedOption(G->DemoAnimationOptions[1]);Stage=4;return true;}
  if(Stage==4||Stage==5){if(A->AnimationTickSamples<=Samples)return true;if(!Same(Capture(G,0),Left)||!Same(Capture(G,1),Right))return Stop(TEXT("Full-body animation overwrote independent hands"));if(Stage==4){Add(TEXT("both_hand_overrides_survive_demo_playback"));Samples=A->AnimationTickSamples;G->GameClipCombo->SetSelectedOption(G->DemoAnimationOptions[2]);Stage=5;return true;}Add(TEXT("both_hand_overrides_survive_animation_switch"));G->OnClothesState(101);Stage=6;return true;}
  if(Stage==6){if(!Same(Capture(G,0),Left)||!Same(Capture(G,1),Right)||G->SelectedHandA[0]!=1||G->SelectedHandA[1]!=A->GetHandShapeCount()-1)return Stop(TEXT("Clothing reload lost hand selections"));Add(TEXT("hands_survive_clothing_reload"));G->OnResetRightHand();if(!Same(Capture(G,0),Left)||G->SelectedHandA[1]!=-1||G->SelectedHandBlend[1]!=0)return Stop(TEXT("Right reset changed left hand"));Add(TEXT("right_reset_preserves_left_override"));return Stop(FString());}return true;
 }
};
}
static FAutoConsoleCommandWithWorld HandsAudit(TEXT("ok.AuditEditorHands"),TEXT("Verify independent maker hand poses through actual UI callbacks"),FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* W){const FString Card=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_GUI_RELOAD_CARD")),Output=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_GUI_RELOAD_OUTPUT"));if(!W||Card.IsEmpty()||Output.IsEmpty())return;auto R=MakeShared<OKHandsAudit::FRun>();R->World=W;R->Card=Card;R->Output=Output;FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([R](float D){return R->Tick(D);}));}));
#endif
