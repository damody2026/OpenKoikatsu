#include "../OKGalleryWidget.h"
#include "../OKAppearanceButton.h"
#include "KKCardReader.h"
#include "KKPreviewActor.h"
#include "ok_extract.h"
#include "Misc/Crc.h"
#include "Components/ComboBoxString.h"
#include "Components/EditableTextBox.h"
#include "Containers/Ticker.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/UObjectIterator.h"
#include "UnrealClient.h"
#include "ShaderCompiler.h"
#if WITH_DEV_AUTOMATION_TESTS
namespace OKAppearanceAudit {
struct FRun:TSharedFromThis<FRun>{
 TWeakObjectPtr<UWorld> World;TWeakObjectPtr<UOKGalleryWidget> Gallery;FString Card,Output;double VisualReady=0,Start=FPlatformTime::Seconds();int32 Stage=0,Expected=0,BeforeBodyVerts=0;uint64 Worn=0;uint32 BeforeTexture=0;FString BeforePath,ModGuid,BeforeMods;TArray<TSharedPtr<FJsonValue>> Cases;
 uint64 Clothes(UOKGalleryWidget* G){uint64 N=0;if(auto* A=G->PreviewActor.Get()){if(A->bClothingHidden)return 0;for(const auto& P:A->Parts)if(P.Role.StartsWith(TEXT("clothes"))){OkMeshView V{};if(A->Session.PartAt(P.SessionIndex,V)&&V.render_visible)N+=P.Triangles.Num()/3;}}return N;}
 int32 BodyVerts(UOKGalleryWidget* G){int32 Count=0;if(auto* A=G->PreviewActor.Get())for(int32 I=0;I<A->Session.PartCount();++I){OkMeshView V{};if(A->Session.PartAt(I,V)&&V.role&&FCStringAnsi::Stricmp(V.role,"body")==0)Count+=V.n_verts;}return Count;}
 uint32 Textures(UOKGalleryWidget* G){auto* A=G->PreviewActor.Get();uint32 Hash=0;if(A)for(int32 I=0;I<A->Session.PartCount();++I){OkMeshView V{};if(A->Session.PartAt(I,V)&&V.png&&V.png_len>0)Hash=HashCombine(Hash,FCrc::MemCrc32(V.png,V.png_len));}return Hash;}
 void Search(UOKGalleryWidget* G,const FString& V){G->AppearanceSearch->SetText(FText::FromString(V));G->AppearanceSearch->OnTextChanged.Broadcast(FText::FromString(V));}
 void Add(const TCHAR* Name){auto V=MakeShared<FJsonObject>();V->SetStringField(TEXT("case"),Name);V->SetBoolField(TEXT("valid"),true);Cases.Add(MakeShared<FJsonValueObject>(V));}
 bool Stop(const FString& Error){auto R=MakeShared<FJsonObject>();R->SetBoolField(TEXT("valid"),Error.IsEmpty());R->SetStringField(TEXT("error"),Error);R->SetArrayField(TEXT("cases"),Cases);if(auto* G=Gallery.Get()){R->SetNumberField(TEXT("items"),G->AppearanceItems.Num());R->SetNumberField(TEXT("targets"),G->AppearanceTargets.Num());}FString Text;FJsonSerializer::Serialize(R,TJsonWriterFactory<>::Create(&Text));FFileHelper::SaveStringToFile(Text,*Output);FPlatformMisc::RequestExitWithStatus(false,Error.IsEmpty()?0:1);return false;}
 bool Tick(float){auto* W=World.Get();if(!W||FPlatformTime::Seconds()-Start>1100)return Stop(TEXT("Appearance audit timeout"));auto* G=Gallery.Get();if(!G){for(TObjectIterator<UOKGalleryWidget> It;It;++It)if(It->GetWorld()==W&&It->PreviewImage){Gallery=*It;G=*It;break;}if(!G)return true;}
  if(Stage==0){G->bIndependentCards=true;G->Entries.Reset();FKKCardListEntry E;E.Path=Card;E.FileName=FPaths::GetCleanFilename(Card);G->Entries.Add(E);G->SelectIndex(0);G->OnLoad3D();Stage=1;return true;}
  if(G->bLoadInFlight||G->bAppearanceBusy||G->bAppearanceThumbBusy){VisualReady=FPlatformTime::Seconds();return true;}
  if(GShaderCompilingManager&&GShaderCompilingManager->IsCompiling()){VisualReady=FPlatformTime::Seconds();return true;}
  if(FPlatformTime::Seconds()-VisualReady<2)return true;
  if(Stage==1){G->OnOpenAppearance();Stage=2;return true;}
  if(Stage==2){if(G->AppearanceItems.Num()<100||G->AppearanceTargets.Num()<100)return Stop(TEXT("Catalog incomplete"));if(G->AppearanceTextures.Num()<16)return Stop(TEXT("Original eyebrow thumbnails missing"));Add(TEXT("original_and_mod_catalog_and_real_thumbnails"));FScreenshotRequest::RequestScreenshot(FPaths::ChangeExtension(Output,TEXT(""))+TEXT("_eyebrows.png"),true,false);
   Stage=20;return true;}
  if(Stage==20){G->OnAppearanceNext();if(G->AppearancePage!=1)return Stop(TEXT("Page navigation failed"));G->OnAppearancePrev();Search(G,TEXT("nonexistent_asset_qa"));if(!G->AppearanceFiltered.IsEmpty())return Stop(TEXT("Search filter failed"));Search(G,FString());Add(TEXT("search_and_pagination"));Stage=3;return true;}
  if(Stage==3){for(UOKAppearanceButton* B:G->AppearanceButtons){for(auto I:G->AppearanceItems)if(I->GetStringField(TEXT("key"))==B->ItemKey&&I->GetStringField(TEXT("source")).IsEmpty()){Expected=I->GetObjectField(TEXT("entry"))->GetIntegerField(TEXT("id"));if(Expected!=G->Selected.Face.EyebrowId){BeforeTexture=Textures(G);BeforeBodyVerts=BodyVerts(G);BeforeMods=G->EditorModsJson();B->OnClicked.Broadcast();Stage=4;return true;}}}return Stop(TEXT("No alternative eyebrow"));}
  if(Stage==4){if(G->Selected.Face.EyebrowId!=Expected)return Stop(TEXT("Image callback did not apply eyebrow"));auto* A=G->PreviewActor.Get();if(!A||!A->Session.IsPortable()||A->Parts.IsEmpty())return Stop(TEXT("Updated preview missing"));FKKCharacterFile Read;FString Error;if(!UKKCardReader::LoadFromFile(G->Entries[0].Path,Read,Error)||Read.Face.EyebrowId!=Expected)return Stop(TEXT("Appearance disk roundtrip failed"));if(BodyVerts(G)!=BeforeBodyVerts)return Stop(TEXT("Unchanged body mesh was replaced"));if(G->EditorModsJson()!=BeforeMods)return Stop(TEXT("Appearance source restoration changed current bone/pregnancy modifiers"));if(Textures(G)==BeforeTexture)return Stop(TEXT("Appearance texture did not change"));Add(TEXT("thumbnail_click_updates_portable_preview_and_disk"));G->AppearanceTarget->SetSelectedOption(TEXT("上眼線種類"));Stage=5;return true;}
  if(Stage==5){if(G->AppearanceTextures.Num()<16)return Stop(TEXT("Eyeline thumbnails missing"));FScreenshotRequest::RequestScreenshot(FPaths::ChangeExtension(Output,TEXT(""))+TEXT("_eyelines.png"),true,false);BeforePath=G->Entries[0].Path;BeforeTexture=Textures(G);for(auto I:G->AppearanceItems)if(I->GetObjectField(TEXT("entry"))->GetIntegerField(TEXT("category_no"))==412&&!I->GetStringField(TEXT("guid")).IsEmpty()){ModGuid=I->GetStringField(TEXT("guid"));Search(G,FPaths::GetCleanFilename(I->GetStringField(TEXT("source"))));break;}if(ModGuid.IsEmpty())return Stop(TEXT("Mod eyeline coverage missing"));G->OnAppearanceRandom();Stage=6;return true;}
  if(Stage==6){if(G->Entries[0].Path==BeforePath)return Stop(TEXT("Random mod apply failed"));bool Resolved=false;for(const auto& R:G->Selected.Resolves)Resolved|=R.ModID==ModGuid&&R.CategoryNo==412;if(!Resolved)return Stop(TEXT("Mod GUID was lost"));if(Textures(G)==BeforeTexture)return Stop(TEXT("Mod eyeline texture did not change"));Add(TEXT("eyeline_random_mod_guid_and_preview"));FScreenshotRequest::RequestScreenshot(FPaths::ChangeExtension(Output,TEXT(""))+TEXT("_mod_applied.png"),true,false);Stage=60;return true;}
  if(Stage==60){G->OnAppearanceReset();Stage=7;return true;}
  if(Stage==7){if(G->Selected.Face.EyelineUpId!=0)return Stop(TEXT("Reset type did not restore original default"));Add(TEXT("type_reset_and_reload"));Worn=Clothes(G);if(!Worn)return Stop(TEXT("No worn clothing for acceptance"));G->OnClothesState(101);Stage=8;return true;}
  if(Stage==8){if(G->ClothesStates[0]!=1)return Stop(TEXT("Half removal state failed after image change"));Add(TEXT("image_change_preserves_half_removal"));G->OnClothesState(102);Stage=9;return true;}
  if(Stage==9){if(Clothes(G)!=0)return Stop(TEXT("Full removal left visible clothing after image change"));Add(TEXT("image_change_preserves_full_removal"));G->OnClothesState(100);Stage=10;return true;}
  if(Stage==10){if(Clothes(G)!=Worn)return Stop(TEXT("Worn geometry failed to restore after image change"));Add(TEXT("image_change_preserves_worn_geometry"));return Stop(FString());}return true;
 }
};
}
static FAutoConsoleCommandWithWorld AppearanceAudit(TEXT("ok.AuditEditorAppearance"),TEXT("Verify original image selectors through live editor callbacks"),FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* W){const FString Card=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_GUI_RELOAD_CARD")),Output=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_GUI_RELOAD_OUTPUT"));if(!W||Card.IsEmpty()||Output.IsEmpty())return;auto R=MakeShared<OKAppearanceAudit::FRun>();R->World=W;R->Card=Card;R->Output=Output;FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([R](float D){return R->Tick(D);}));}));
#endif
