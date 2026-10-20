#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "KKCardSource.h"
#include "KKRustExtract.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/Guid.h"
#include "HAL/FileManager.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKKCardSourceSceneExcludedSessionTest,"OpenKoikatsu.CardImporter.SceneExcludedSession",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FKKCardSourceSceneExcludedSessionTest::RunTest(const FString&){
 const FString Card=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_RAW_CARD"));
 if(Card.IsEmpty()){AddError(TEXT("Set OK_RAW_CARD to a real original KKS card"));return false;}
 FString Root,Error;if(!TestTrue(TEXT("Original card resolves to its zipmods"),OKResolveCardSource(Card,Root,Error))){AddError(Error);return false;}
 // Vanilla data comes from the OK base library: the view holds the card's zipmods only, no Unity base bundles.
 TestFalse(TEXT("No Unity base data in resolved view"),IFileManager::Get().DirectoryExists(*(Root/TEXT("abdata"))));
 FOkSession Session;if(!TestTrue(TEXT("Real character Session opens from the OK base library"),Session.Open(Root,Card,0,Error))){AddError(Error);return false;}
 TestFalse(TEXT("Original fixture is not portable"),Session.IsPortable());TestTrue(TEXT("Character has parts"),Session.PartCount()>0);
 TestTrue(TEXT("Character skinning works"),Session.Skin());
 UE_LOG(LogTemp,Display,TEXT("Scene-free original card session parts=%d source=%s"),Session.PartCount(),*Root);
 return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKKOriginalSourceControlTest,"OpenKoikatsu.CardImporter.OriginalSourceControl",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FKKOriginalSourceControlTest::RunTest(const FString&){
 const FString Card=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_RAW_CARD"));
 const FString Root=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_RAW_CONTROL_ROOT"));
 if(Card.IsEmpty()||Root.IsEmpty()){AddError(TEXT("Set OK_RAW_CARD and OK_RAW_CONTROL_ROOT for the full-source baseline"));return false;}
 FOkSession Session;FString Error;
 if(!TestTrue(TEXT("Full-source baseline character session opens"),Session.Open(Root,Card,0,Error))){AddError(Error);return false;}
 TestTrue(TEXT("Full-source baseline character has parts"),Session.PartCount()>0);
 return !HasAnyErrors();
}
#endif
