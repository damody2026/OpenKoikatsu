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
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOKStudioGlassesBlend,
    "OpenKoikatsu.RuntimeMaterial.GlassesBlend",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FOKStudioGlassesBlend::RunTest(const FString&)
{
    for (int32 I = int32(EKKRuntimeMaterialRecipe::KoikanoGlasses0);
         I <= int32(EKKRuntimeMaterialRecipe::KoikanoGlasses11); ++I)
    {
        const auto Recipe = EKKRuntimeMaterialRecipe(I);
        TestTrue(TEXT("all native glasses variants preserve shader alpha"), OKRuntimeSharedMaterialTranslucent(Recipe));
        TestEqual(TEXT("source SrcAlpha / OneMinusSrcAlpha"), int32(OKRuntimeSharedMaterialBlendMode(Recipe)), int32(BLEND_Translucent));
    }
    return true;
}
#endif
