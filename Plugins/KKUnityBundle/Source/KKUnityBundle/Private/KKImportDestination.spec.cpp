#include "KKImportDestination.h"
#include "KKAnimationImporter.h"
#include "KKImportedCharacter.h"
#include "KKImportIndependentCardCommandlet.h"
#include "KKImportSkeletalAnimationsCommandlet.h"
#include "Misc/AutomationTest.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "Misc/Paths.h"
#include "Misc/PackageName.h"
#include "HAL/FileManager.h"
#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKKImportDestinationTest,"OpenKoikatsu.Import.DestinationReservations",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FKKImportDestinationTest::RunTest(const FString& Parameters){
 const FString Base=TEXT("/Game/ImportDestinationTest_")+FGuid::NewGuid().ToString(EGuidFormats::Digits);
 FString Error;TestTrue(TEXT("Fresh destination accepted"),OKValidateImportDestination(Base,Error));
 TStrongObjectPtr<UPackage> Reserved(CreatePackage(*(Base/TEXT("Animations"))));
 auto* Existing=NewObject<UKKImportedAnimationLibrary>(Reserved.Get(),TEXT("Animations"));
 TStrongObjectPtr<UKKImportedAnimationLibrary> KeepExisting(Existing);
 FKKImportedAnimation Sentinel;Sentinel.Clip=TEXT("Unsaved sentinel");Existing->Animations.Add(Sentinel);Reserved->SetDirtyFlag(true);
 TestFalse(TEXT("Loaded child rejected"),OKValidateImportDestination(Base,Error));
 TestTrue(TEXT("Loaded diagnostic"),Error.Contains(TEXT("loaded packages")));
 TestFalse(TEXT("Case variant rejected"),OKValidateImportDestination(TEXT("/Game/")+Base.Mid(6).ToLower(),Error));
 TestTrue(TEXT("Sibling prefix accepted"),OKValidateImportDestination(Base+TEXT("Sibling"),Error));
 TestFalse(TEXT("Exact loaded package rejected"),OKValidateImportDestination(Base/TEXT("Animations"),Error));
 TestFalse(TEXT("Non Game mount rejected"),OKValidateImportDestination(TEXT("/Engine/Fresh"),Error));
 TestFalse(TEXT("Traversal rejected"),OKValidateImportDestination(TEXT("/Game/../Fresh"),Error));
 const FString Missing=FPaths::ProjectSavedDir()/TEXT("missing_destination_fixture375.png");
 // Destination must win over missing source/character; neither parser nor packages are touched.
 TestNull(TEXT("Animation service refuses loaded destination first"),KKAnimationImporter::Import(Missing,TEXT(""),TEXT("studio/anime/00.unity3d"),{TEXT("clip")},Base,-1,60,Error));
 TestTrue(TEXT("Animation service diagnostic"),Error.Contains(TEXT("loaded packages")));
 TStrongObjectPtr<UKKImportIndependentCardCommandlet> Card(NewObject<UKKImportIndependentCardCommandlet>());
 AddExpectedError(TEXT("Destination contains loaded packages"),EAutomationExpectedErrorFlags::Contains,1);
 TestEqual(TEXT("Card commandlet refuses loaded child first"),Card->Main(FString::Printf(TEXT("-Card=\"%s\" -Destination=\"%s\""),*Missing,*Base)),1);
 TStrongObjectPtr<UKKImportSkeletalAnimationsCommandlet> Animation(NewObject<UKKImportSkeletalAnimationsCommandlet>());
 AddExpectedError(TEXT("Destination contains loaded packages"),EAutomationExpectedErrorFlags::Contains,1);
 TestEqual(TEXT("Recompress refuses loaded child first"),Animation->Main(FString::Printf(TEXT("-Recompress=\"/Game/IndependentImports/Stage374R2Male02Animation/Animations.Animations\" -Destination=\"%s\""),*Base)),1);
 TestEqual(TEXT("Existing asset count unchanged"),Existing->Animations.Num(),1);
 TestEqual(TEXT("Existing asset data unchanged"),Existing->Animations[0].Clip,FString(TEXT("Unsaved sentinel")));
 TestTrue(TEXT("Unsaved changes preserved"),Reserved->IsDirty());
 TestFalse(TEXT("Rejected import created no directory"),IFileManager::Get().DirectoryExists(*FPackageName::LongPackageNameToFilename(Base)));
 UE_LOG(LogTemp,Display,TEXT("Import destination reservations fresh=passed loaded_child=rejected case=rejected sibling=passed animation=blocked card=blocked recompress=blocked unsaved=preserved no_output=1"));
 return !HasAnyErrors();
}
#endif
