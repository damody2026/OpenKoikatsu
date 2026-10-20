#include "../OKGalleryWidget.h"
#include "KKPreviewActor.h"
#include "Containers/Ticker.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/UObjectIterator.h"
#include "ShaderCompiler.h"
#include "UnrealClient.h"
#if WITH_DEV_AUTOMATION_TESTS
namespace OKEditorPerformance {
struct FRun {
 TWeakObjectPtr<UWorld> World; TWeakObjectPtr<UOKGalleryWidget> Gallery;
 FString Card,Output; int32 Stage=0; double Start=FPlatformTime::Seconds(),Ready=0,Last=0;
 TArray<double> Frames; TArray<double> BaseFrames;
 bool Stop(const FString& Error){
  auto R=MakeShared<FJsonObject>();R->SetBoolField(TEXT("valid"),Error.IsEmpty());R->SetStringField(TEXT("error"),Error);
  double Sum=0,Max=0;TArray<TSharedPtr<FJsonValue>> Values;for(double D:Frames){Sum+=D;Max=FMath::Max(Max,D);Values.Add(MakeShared<FJsonValueNumber>(D));}
  double BaseSum=0,BaseMax=0;for(double D:BaseFrames){BaseSum+=D;BaseMax=FMath::Max(BaseMax,D);}R->SetNumberField(TEXT("base_average_fps"),BaseSum>0?1000.*BaseFrames.Num()/BaseSum:0);R->SetNumberField(TEXT("base_minimum_fps"),BaseMax>0?1000./BaseMax:0);R->SetNumberField(TEXT("frames"),Frames.Num());R->SetNumberField(TEXT("average_fps"),Sum>0?1000.*Frames.Num()/Sum:0);R->SetNumberField(TEXT("minimum_fps"),Max>0?1000./Max:0);R->SetArrayField(TEXT("frame_ms"),Values);
  const bool Passed=Error.IsEmpty()&&Frames.Num()>=300&&Max<=1000./60.;R->SetBoolField(TEXT("editor_60fps_pass"),Passed);R->SetBoolField(TEXT("valid"),Passed);if(Error.IsEmpty()&&!Passed)R->SetStringField(TEXT("error"),TEXT("Editor frame time exceeded 60 FPS budget"));
  FString Text;FJsonSerializer::Serialize(R,TJsonWriterFactory<>::Create(&Text));FFileHelper::SaveStringToFile(Text,*Output);
  FPlatformMisc::RequestExitWithStatus(false,Passed?0:1);return false;
 }
 bool Tick(float){
  const double Now=FPlatformTime::Seconds();auto* W=World.Get();if(!W||Now-Start>300)return Stop(TEXT("Performance audit timeout"));
  auto* G=Gallery.Get();if(!G){for(TObjectIterator<UOKGalleryWidget> It;It;++It)if(It->GetWorld()==W&&It->PreviewImage){Gallery=*It;G=*It;break;}if(!G)return true;}
  if(Stage==0){G->bIndependentCards=true;G->Entries.Reset();FKKCardListEntry E;E.Path=Card;G->Entries.Add(E);G->SelectIndex(0);G->OnLoad3D();Stage=1;return true;}
  if(G->bLoadInFlight||!IsValid(G->PreviewActor.Get())){Ready=Now;return true;}
  if(Stage==1){if(G->bClothingWarmInFlight||G->ClothingWarmReady||!G->ClothingWarmQueue.IsEmpty()||(GShaderCompilingManager&&GShaderCompilingManager->IsCompiling())){Ready=Now;return true;}if(Now-Ready<5)return true;Last=Now;Stage=2;return true;}
  if(Stage==2){BaseFrames.Add((Now-Last)*1000.);Last=Now;if(BaseFrames.Num()<300)return true;G->OnOpenProperties();Ready=Now;Stage=3;return true;}
  if(Stage==3){if(G->bPropertyBusy){Ready=Now;return true;}if(Now-Ready<3)return true;Last=Now;Stage=4;return true;}
  if(Stage==4){Frames.Add((Now-Last)*1000.);Last=Now;if(Frames.Num()<300)return true;FScreenshotRequest::RequestScreenshot(FPaths::ChangeExtension(Output,TEXT("png")),true,false);Stage=5;return true;}
  return Stop(FString());
 }
};
}
static FAutoConsoleCommandWithWorld EditorPerformanceAuditCommand(TEXT("ok.AuditEditorPerformance"),TEXT("Measure normal editor frame times without freezing animation"),FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* W){auto R=MakeShared<OKEditorPerformance::FRun>();R->World=W;R->Card=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_GUI_RELOAD_CARD"));R->Output=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_GUI_RELOAD_OUTPUT"));if(!W||R->Card.IsEmpty()||R->Output.IsEmpty())return;FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([R](float D){return R->Tick(D);}));}));
#endif
