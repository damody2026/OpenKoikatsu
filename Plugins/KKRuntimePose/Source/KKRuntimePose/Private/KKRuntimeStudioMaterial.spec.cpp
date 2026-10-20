#include "KKRuntimeSharedMaterial.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOKStudioMdkCoverage,
    "OpenKoikatsu.RuntimeMaterial.StudioMdkCoverage",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOKStudioMdkCoverage::RunTest(const FString&)
{
    for (const TCHAR* Keywords : { TEXT("[\"_NORMALMAP\"]"), TEXT("[\"_NORMALMAP\",\"_ALPHATEST_ON\"]") })
    {
        FKKRuntimeMaterialPart Part;
        Part.SourceMetadataJson = FString::Printf(TEXT("{\"material\":{\"shader\":\"Shader Forge/main_StandardMDK_studio\",\"keywords\":%s}}"), Keywords);
        const auto Recipe = OKRuntimeSharedMaterialRecipe(Part);
        TestEqual(TEXT("body and guard use native MDK"), int32(Recipe), int32(EKKRuntimeMaterialRecipe::ForgeMDK));
        TestEqual(TEXT("MDK writes depth and clips alpha"), int32(OKRuntimeSharedMaterialBlendMode(Recipe)), int32(BLEND_Masked));
    }
    return true;
}
#endif
