#include "Misc/AutomationTest.h"
#include "KKCardReader.h"
#include "KKCardWriter.h"
#include "KKRustExtract.h"
#include "Misc/Paths.h"
#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOKMultiOutfitTest,"OpenKoikatsu.IndependentCard.AllOutfits",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FOKMultiOutfitTest::RunTest(const FString&)
{
    const FString Path=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_TEST_MULTI_OUTFIT_CARD"));
    if(Path.IsEmpty()){AddInfo(TEXT("Set OK_TEST_MULTI_OUTFIT_CARD for local all-outfit acceptance"));return true;}
    FKKCharacterFile Card;FString Error;
    if(!TestTrue(TEXT("Read all outfits"),UKKCardReader::LoadFromFile(Path,Card,Error))){AddError(Error);return false;}
    const int32 Count=Card.Outfits.Num();TestTrue(TEXT("Multiple outfit metadata"),Count>1);
    const auto Coordinate=Card.RawBlocks.FindRef(TEXT("Coordinate"));const auto Preview=Card.Header.PreviewPng;
    Card.Body.ShapeValueBody[0]=.123f;Card.Body.ShapeValueBody[4]=.789f;Card.Nickname=TEXT("All outfits roundtrip");
    const FString Output=FPaths::ProjectSavedDir()/TEXT("APNGCards/all-outfits-edited.png");
    if(!TestTrue(TEXT("Save all outfits"),UKKCardWriter::SaveWithEdits(Card,Output,Error))){AddError(Error);return false;}
    FKKCharacterFile Reload;
    if(!TestTrue(TEXT("Reload all outfits"),UKKCardReader::LoadFromFile(Output,Reload,Error)))return false;
    TestEqual(TEXT("Outfit metadata count"),Reload.Outfits.Num(),Count);
    TestTrue(TEXT("Coordinate unchanged"),Reload.RawBlocks.FindRef(TEXT("Coordinate"))==Coordinate);
    TestTrue(TEXT("Preview and all outfit assets unchanged"),Reload.Header.PreviewPng==Preview);
    for(int32 I=0;I<Count;++I)
    {
        FOkSession Session;
        if(!TestTrue(FString::Printf(TEXT("Open outfit %d without game"),I),Session.Open(FString(),Output,I,Error))){AddError(Error);return false;}
        TestEqual(TEXT("All outfits available"),Session.CoordinateCount(),Count);TestTrue(TEXT("Has parts"),Session.PartCount()>0);
        TestTrue(TEXT("Global saved height"),FMath::IsNearlyEqual(Session.ShapeBody()[0],.123f));
        TestTrue(TEXT("Global saved bust"),FMath::IsNearlyEqual(Session.ShapeBody()[4],.789f));
    }
    AddExpectedError(TEXT("portable outfit index out of range"),EAutomationExpectedErrorFlags::Contains,2);
    FOkSession Invalid;TestFalse(TEXT("Reject negative outfit"),Invalid.Open(FString(),Output,-1,Error));
    TestFalse(TEXT("Reject missing outfit"),Invalid.Open(FString(),Output,Count,Error));return true;
}
#endif
