#include "../OKGalleryWidget.h"
#include "../OKShapeSlider.h"
#include "../OKBoneControls.h"
#include "../OKPregnancyPresets.h"
#include "KKPreviewActor.h"
#include "ok_extract.h"
#include "Containers/Ticker.h"
#include "Components/ComboBoxString.h"
#include "Components/ScaleBox.h"
#include "Components/SizeBox.h"
#include "Components/ScrollBox.h"
#include "Components/SpinBox.h"
#include "Components/PoseableMeshComponent.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Misc/FileHelper.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/UObjectIterator.h"
#include "UnrealClient.h"
#if WITH_DEV_AUTOMATION_TESTS
namespace OKEditorLiveAudit {
struct FRun:TSharedFromThis<FRun>{
 TWeakObjectPtr<UWorld> World;TWeakObjectPtr<UOKGalleryWidget> Gallery;TWeakObjectPtr<UOKShapeSlider> Slider;
 FString Card,Output;double Start=FPlatformTime::Seconds(),StageStart=0;
 int32 Stage=0,Updates=0,Observed=0;FVector LastScale=FVector::ZeroVector;
 TWeakObjectPtr<USkinnedAsset> LastMesh;float SavedWidth=360,SavedFont=16;bool bSaved=false;
 TArray<TSharedPtr<FJsonValue>> Cases;
 FVector Scale(AKKPreviewActor* A){for(int32 I=0;I<A->Session.SkelBoneCount();++I){OkBoneView B{};if(A->Session.SkelBoneAt(I,B)&&B.name&&FString(UTF8_TO_TCHAR(B.name))==TEXT("cf_s_shoulder02_L"))return FVector(B.pose_scl[0],B.pose_scl[1],B.pose_scl[2]);}return FVector::ZeroVector;}
 void Add(const TCHAR* Name){auto R=MakeShared<FJsonObject>();R->SetStringField(TEXT("case"),Name);R->SetBoolField(TEXT("valid"),true);Cases.Add(MakeShared<FJsonValueObject>(R));}
 bool Stop(const FString& Error){if(auto* G=Gallery.Get();G&&bSaved){G->OnEditorFontSizeChanged(SavedFont);G->OnEditorPanelWidthChanged(SavedWidth);G->SaveEditorDisplaySettings();}
  auto R=MakeShared<FJsonObject>();R->SetBoolField(TEXT("valid"),Error.IsEmpty());R->SetStringField(TEXT("error"),Error);R->SetArrayField(TEXT("cases"),Cases);FString Text;FJsonSerializer::Serialize(R,TJsonWriterFactory<>::Create(&Text));FFileHelper::SaveStringToFile(Text,*Output);FPlatformMisc::RequestExitWithStatus(false,Error.IsEmpty()?0:1);return false;}
 bool Tick(float){auto* W=World.Get();if(!W||FPlatformTime::Seconds()-Start>240)return Stop(TEXT("Live editor audit timeout"));auto* G=Gallery.Get();if(!G){for(TObjectIterator<UOKGalleryWidget> It;It;++It)if(It->GetWorld()==W&&It->PreviewImage){Gallery=*It;G=*It;break;}if(!G)return true;}
  if(Stage==0){SavedWidth=G->EditorPanelWidth;SavedFont=G->EditorFontSize;bSaved=true;G->bIndependentCards=true;G->Entries.Reset();FKKCardListEntry E;E.Path=Card;E.FileName=FPaths::GetCleanFilename(Card);G->Entries.Add(E);G->SelectIndex(0);G->OnLoad3D();Stage=1;return true;}
  if(G->bLoadInFlight||G->bAnimationPreparing||G->PendingAnimationKind>=0)return true;
  auto* A=G->PreviewActor.Get();if(!A||!A->Session.IsPortable())return Stop(TEXT("Portable actor missing"));
  if(Stage==1){
   for(const auto& Def:OKBoneControls){const FString Label=Def.Label;bool Chinese=false;for(TCHAR C:Label){Chinese|=C>=0x3400&&C<=0x9fff;if((C>='a'&&C<='z')||(C>='A'&&C<='W'))return Stop(TEXT("Untranslated bone control"));}if(!Chinese)return Stop(TEXT("Bone label lacks Chinese text"));}
   for(const auto& Preset:OKPregnancyPresets)if(G->PregnancyPreset->FindOptionIndex(Preset.Label)==INDEX_NONE||G->PregnancyPreset->FindOptionIndex(Preset.Name)!=INDEX_NONE)return Stop(TEXT("Preset display is not localized"));
   Add(TEXT("all_bone_settings_and_presets_chinese"));
   G->OnResetAllShapes();G->BoneCategoryCombo->SetSelectedOption(TEXT("骨骼調整：上半身"));
   G->EditorWidthInput->OnValueChanged.Broadcast(600);G->EditorFontInput->OnValueChanged.Broadcast(24);StageStart=FPlatformTime::Seconds();Stage=2;return true;}
  if(Stage==2){if(FPlatformTime::Seconds()-StageStart<1)return true;
   if(!FMath::IsNearlyEqual(G->ShapePanel->GetWidthOverride(),600)||!FMath::IsNearlyEqual(G->EditorContentScale->GetUserSpecifiedScale(),1.5f)||G->ShapeScroll->GetCachedGeometry().GetLocalSize().Y<100)return Stop(TEXT("Expanded scaled panel layout invalid"));
   G->SaveEditorDisplaySettings();GConfig->Flush(true,GGameUserSettingsIni);float Width=0,Font=0;GConfig->GetFloat(TEXT("OpenKoikatsu.EditorDisplay"),TEXT("PanelWidth"),Width,GGameUserSettingsIni);GConfig->GetFloat(TEXT("OpenKoikatsu.EditorDisplay"),TEXT("FontSize"),Font,GGameUserSettingsIni);
   if(Width!=600||Font!=24)return Stop(TEXT("Display preference disk roundtrip failed"));Add(TEXT("width_font_and_persistence"));
   FScreenshotRequest::RequestScreenshot(FPaths::ChangeExtension(Output,TEXT(""))+TEXT("_large.png"),true,false);
   Stage=3;StageStart=FPlatformTime::Seconds();return true;}
  if(Stage==3){if(FPlatformTime::Seconds()-StageStart<.5)return true;G->EditorFontInput->OnValueChanged.Broadcast(12);G->EditorWidthInput->OnValueChanged.Broadcast(320);Stage=4;StageStart=FPlatformTime::Seconds();return true;}
  if(Stage==4){if(FPlatformTime::Seconds()-StageStart<.5)return true;
   if(G->ShapePanel->GetWidthOverride()!=320||G->EditorContentScale->GetUserSpecifiedScale()!=.75f)return Stop(TEXT("Compact panel layout invalid"));Add(TEXT("compact_layout"));
   FScreenshotRequest::RequestScreenshot(FPaths::ChangeExtension(Output,TEXT(""))+TEXT("_small.png"),true,false);
   for(UOKShapeSlider* S:G->BoneSliders)if(S->ModBone==TEXT("cf_s_shoulder02_L")&&S->ModAxis==-1){Slider=S;S->ModSide->SetSelectedIndex(1);break;}
   if(!Slider.IsValid())return Stop(TEXT("Shoulder control missing"));LastScale=Scale(A);Stage=5;StageStart=FPlatformTime::Seconds();return true;}
  if(Stage==5){const FVector Current=Scale(A);if(!Current.Equals(LastScale,.0001)){++Observed;LastScale=Current;}
   // Keep emitting the actual slider event every frame. Never call ApplyPending,
   // never emit a mouse-release event and never pause input to allow a debounce.
   Slider->OnValueChanged.Broadcast(1.f+.35f*FMath::Sin(++Updates*.17f));
   if(FPlatformTime::Seconds()-StageStart>2&&Updates>=12){if(Observed<4)return Stop(TEXT("Bone preview waits for dragging to stop"));Add(TEXT("continuous_bone_input_updates_before_release"));G->PregnancyPreset->SetSelectedOption(TEXT("標準"));G->BoneCategoryCombo->SetSelectedOption(TEXT("腹部形變"));Stage=6;Updates=Observed=0;}return true;}
  if(Stage==6){if(A->SkelFollowers.IsEmpty())return Stop(TEXT("GPU body missing"));auto* Mesh=A->SkelFollowers[0]->GetSkinnedAsset();if(LastMesh.IsValid()&&LastMesh.Get()!=Mesh)++Observed;LastMesh=Mesh;
   G->OnPregnancyChanged(TEXT("inflationSize"),5.f+float(++Updates%6));
   if(Updates>=8){if(Observed<4)return Stop(TEXT("Pregnancy geometry waits for release"));Add(TEXT("continuous_pregnancy_input_updates_gpu_before_release"));G->OnResetAllShapes();Stage=7;StageStart=FPlatformTime::Seconds();}return true;}
  if(Stage==7){if(FPlatformTime::Seconds()-StageStart<.5)return true;if(G->bEditorModsDirty||A->Parts.IsEmpty()||A->SkelFollowers.IsEmpty())return Stop(TEXT("Final reset not applied by frame update"));Add(TEXT("final_value_and_reset_applied"));return Stop(FString());}
  return true;
 }
};
}
static FAutoConsoleCommandWithWorld LiveAudit(TEXT("ok.AuditEditorLive"),TEXT("Verify editor sizing and continuous slider updates without releasing input"),FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* W){
 const FString Card=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_GUI_RELOAD_CARD")),Output=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_GUI_RELOAD_OUTPUT"));if(!W||Card.IsEmpty()||Output.IsEmpty())return;
 auto R=MakeShared<OKEditorLiveAudit::FRun>();R->World=W;R->Card=Card;R->Output=Output;FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([R](float D){return R->Tick(D);}));
}));
#endif
