#include "OKStudioImportSettings.h"
#include "OKStudioSky.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOKStudioClearColorSettingsTest, "OpenKoikatsu.Studio.ClearColorSettings", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FOKStudioClearColorSettingsTest::RunTest(const FString&)
{
    TestTrue(TEXT("source default is black without an installation"), OKStudioClearColorFromSettings(TEXT("")).Equals(FLinearColor::Black));
    const FString Xml = TEXT("<System><H><BackColor>1,0,0,1</BackColor></H><Est><BackColor>0.2, 0.3, 0.4, 0.1</BackColor></Est></System>");
    TestTrue(TEXT("Studio uses Est settings, preserving display RGB and ignoring camera alpha"), OKStudioClearColorFromSettings(Xml).Equals(FLinearColor(.2f,.3f,.4f,1.f)));
    for (const FString Invalid : { TEXT("<System><Est/></System>"), TEXT("<System><Est><BackColor>1,2,3</BackColor></Est></System>"), TEXT("<System><Est><BackColor>1,bad,0,1</BackColor></Est></System>") })
        TestTrue(TEXT("missing or invalid colour retains source black default"), OKStudioClearColorFromSettings(Invalid).Equals(FLinearColor::Black));
    FLinearColor A=FLinearColor::Red,B=FLinearColor::Green,C=FLinearColor::Blue;
    TArray<FLinearColor> Higher;
    TestTrue(TEXT("empty native scene never inherits Maker probe"), OKStudioMapAmbient(-1,A,B,C,&Higher)
        && A.Equals(FLinearColor(0,0,0,0)) && B.Equals(A) && C.Equals(A) && Higher.Num()==4
        && Higher[0].Equals(A) && Higher[1].Equals(A) && Higher[2].Equals(A) && Higher[3].Equals(FLinearColor(0,0,0,1)));
    return true;
}
#endif
