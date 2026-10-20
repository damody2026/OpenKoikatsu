#include "KKCardImporterService.h"
#include "KKImportedCharacter.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"
#include "ToolMenus.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SWindow.h"
#include "HAL/FileManager.h"
#include "Misc/PackageName.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"
#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKKImporterInterfaceTest,"OpenKoikatsu.CardImporter.Interface",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FKKImporterInterfaceTest::RunTest(const FString& Parameters){
 for(const FName MenuName:{FName(TEXT("LevelEditor.MainMenu.Tools")),FName(TEXT("ContentBrowser.AddNewContextMenu"))}){auto* Menu=UToolMenus::Get()->FindMenu(MenuName);if(TestNotNull(TEXT("Importer menu registered"),Menu)){auto* Section=Menu->FindSection(TEXT("OpenKoikatsu"));if(TestNotNull(TEXT("Importer section registered"),Section))TestNotNull(TEXT("Importer action registered"),Section->FindEntry(TEXT("KKImportIndependentCard")));}}
 auto* Tools=UToolMenus::Get()->FindMenu(TEXT("LevelEditor.MainMenu.Tools"));
 if(Tools){auto* Section=Tools->FindSection(TEXT("OpenKoikatsu"));if(Section){auto* Entry=Section->FindEntry(TEXT("KKImportIndependentCard"));if(Entry){TestTrue(TEXT("Menu executes importer window action"),Entry->TryExecuteToolUIAction(FToolMenuContext()));bool Found=false;for(const auto& Window:FSlateApplication::Get().GetTopLevelWindows())if(Window->GetTitle().ToString()==TEXT("Import KKS / Independent Character Card")){Found=true;TestTrue(TEXT("Importer has widget content"),Window->GetContent()->GetChildren()->Num()>0);Window->SlatePrepass();FSlateApplication::Get().Tick();TArray<FColor> Pixels;FIntVector Size;
 if(TestTrue(TEXT("Importer UI screenshot"),FSlateApplication::Get().TakeScreenshot(Window->GetContent(),Pixels,Size))){TArray64<uint8> PNG;FImageUtils::PNGCompressImageArray(Size.X,Size.Y,Pixels,PNG);TestTrue(TEXT("UI screenshot saved"),FFileHelper::SaveArrayToFile(PNG,*(FPaths::ProjectSavedDir()/TEXT("Tests/AssetImport365/ImporterUI.png"))));}Window->RequestDestroyWindow();}TestTrue(TEXT("Importer Slate window created"),Found);}}}
 const FString Card=FPaths::ProjectDir()/TEXT("Saved/IndependentCards/Release/KK_346095_all_outfits_skinned_apng.png");const FString Destination=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_UI_IMPORT_DESTINATION"));FString Error;
 TestFalse(TEXT("Missing file rejected"),KKCardImporter::Validate(TEXT("missing.png"),TEXT("/Game/FreshCard"),0,Error));TestTrue(TEXT("Missing diagnostic"),!Error.IsEmpty());
 TestFalse(TEXT("Quote injection rejected"),KKCardImporter::Validate(Card+TEXT("\" -Verify=foo"),TEXT("/Game/FreshCard"),0,Error));
 TestFalse(TEXT("Non Game path rejected"),KKCardImporter::Validate(Card,TEXT("/Engine/FreshCard"),0,Error));
 TestFalse(TEXT("Existing folder rejected"),KKCardImporter::Validate(Card,TEXT("/Game/IndependentImports/Stage361AllR1"),0,Error));
 TestFalse(TEXT("Invalid outfit rejected"),KKCardImporter::Validate(Card,TEXT("/Game/FreshCard"),-2,Error));
 auto* Loaded=CreatePackage(TEXT("/Game/ImportedInterfaceReserved/Character"));TestNotNull(TEXT("Loaded reservation"),Loaded);
 TestTrue(TEXT("Sibling prefix allowed"),KKCardImporter::Validate(Card,TEXT("/Game/ImportedInterfaceReservedSibling"),0,Error));
 TestFalse(TEXT("Loaded child package rejected"),KKCardImporter::Validate(Card,TEXT("/Game/ImportedInterfaceReserved"),0,Error));
 if(Destination.IsEmpty()){AddError(TEXT("Set OK_UI_IMPORT_DESTINATION to a fresh /Game/ folder for actual interface service import"));return false;}
 TestFalse(TEXT("Newline injection rejected"),KKCardImporter::Validate(Card+TEXT("\n"),Destination,0,Error));
 TestNull(TEXT("Out of range outfit rejected by native importer"),KKCardImporter::Import(Card,Destination+TEXT("Range"),10000,Error));TestTrue(TEXT("Range error visible"),!Error.IsEmpty());
 const FString Raw=TEXT("C:/portable/KoikatuSunshine/UserData/chara/female/KK_346095.png");
 if(IFileManager::Get().FileExists(*Raw)){AddExpectedError(TEXT("missing adjacent zipmod GUIDs"),EAutomationExpectedErrorFlags::Contains,1);TestNull(TEXT("Raw KKS cannot bypass independent requirement"),KKCardImporter::Import(Raw,Destination+TEXT("Raw"),0,Error));TestTrue(TEXT("Native raw failure visible"),Error.Contains(TEXT("missing adjacent zipmod GUIDs")));TestFalse(TEXT("Rejected raw writes no folder"),IFileManager::Get().DirectoryExists(*FPackageName::LongPackageNameToFilename(Destination+TEXT("Raw"))));}
 TestTrue(TEXT("Fresh input accepted"),KKCardImporter::Validate(Card,Destination,0,Error));
 auto* Result=KKCardImporter::Import(Card,Destination,-1,Error);if(!TestNotNull(TEXT("Interface service returns saved manifest"),Result)){AddError(Error);return false;}
 TestEqual(TEXT("Requested all outfits"),Result->Outfits.Num(),9);TestEqual(TEXT("Selected outfit index"),Result->Outfits[0].Index,0);TestTrue(TEXT("Saved parts exist"),!Result->Outfits[0].Parts.IsEmpty());TestFalse(TEXT("Reimport cannot overwrite"),KKCardImporter::Validate(Card,Destination,0,Error));
 UE_LOG(LogTemp,Display,TEXT("Card importer interface menus_window=passed validation=passed raw_range=rejected import=passed outfits=%d parts=%d overwrite=rejected"),Result->Outfits.Num(),Result->Outfits[0].Parts.Num());return !HasAnyErrors();
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKKStandaloneImportTest,"OpenKoikatsu.CardImporter.Standalone",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FKKStandaloneImportTest::RunTest(const FString& Parameters){
 const FString Card=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_RAW_CARD"));const FString Destination=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_RAW_DESTINATION"));const int32 Outfit=FCString::Atoi(*FPlatformMisc::GetEnvironmentVariable(TEXT("OK_RAW_OUTFIT")));FString Error;
 if(Card.IsEmpty()||Destination.IsEmpty()){AddError(TEXT("Set raw card and fresh destination"));return false;}
 TestFalse(TEXT("Bad external resource override rejected"),KKCardImporter::Validate(Card,Destination,Outfit,Error,TEXT("Z:/missing")));
 auto* Result=KKCardImporter::Import(Card,Destination,Outfit,Error);if(!TestNotNull(TEXT("Raw KKS import without game root"),Result)){AddError(Error);return false;}
 TestTrue(TEXT("At least one outfit"),!Result->Outfits.IsEmpty());int32 Parts=0;for(const auto& O:Result->Outfits){TestTrue(TEXT("Outfit contains saved parts"),!O.Parts.IsEmpty());Parts+=O.Parts.Num();for(const auto& P:O.Parts)TestNotNull(TEXT("Raw part persisted mesh"),P.Model.Get());}
 TestFalse(TEXT("Raw output cannot overwrite"),KKCardImporter::Validate(Card,Destination,Outfit,Error));
 UE_LOG(LogTemp,Display,TEXT("Standalone raw KKS import outfits=%d parts=%d base_source=project_pack zipmods=card_and_detected_game"),Result->Outfits.Num(),Parts);return !HasAnyErrors();
}

#endif
