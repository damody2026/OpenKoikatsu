#include "KKRuntimeSharedMaterial.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "KKRuntimeDisplayProfile.h"
#include "Materials/MaterialInstanceDynamic.h"

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
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOKVertexAmbientParentCompatibility,
    "OpenKoikatsu.RuntimeMaterial.VertexAmbientParentCompatibility",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FOKVertexAmbientParentCompatibility::RunTest(const FString&)
{
    FKKRuntimeMaterialPart Part;Part.ShaderKind=1;Part.Blend=TEXT("masked");Part.Unlit=true;
    Part.SourceMetadataJson=TEXT("{\"schema\":1,\"material\":{\"shader\":\"Koikano/hair_main_sun\",\"keywords\":[\"_OLDHAIR_ON\"]}}");
    auto* Legacy=OKLoadRuntimeSharedMaterialParent(TEXT("/Game/IndependentResources/RuntimeMaterials/StreetDisplay26"),EKKRuntimeMaterialRecipe::KoikanoHairSun2);
    auto* Current=OKLoadRuntimeSharedMaterialParent(TEXT("/Game/IndependentResources/RuntimeMaterials/StudioDisplay28"),EKKRuntimeMaterialRecipe::KoikanoHairSun2);
    if(!TestNotNull(TEXT("legacy cooked native hair parent"),Legacy)||!TestNotNull(TEXT("current cooked native hair parent"),Current))return false;
    FKKRuntimeMaterialScene Scene;OKApplyCharacterMakerLighting(Scene);FString Error;
    TestNotNull(*FString::Printf(TEXT("maker binds existing parent without optional vertex ambient: %s"),*Error),OKCreateRuntimeSharedMaterial(Part,{},Legacy,Scene,Error));
    const FLinearColor Probe(.2f,.1f,.3f,.4f);Scene.Uniforms.Add(TEXT("unity_SHBr"),Probe);
    TestNull(TEXT("nonzero vertex ambient requires a capable parent"),OKCreateRuntimeSharedMaterial(Part,{},Legacy,Scene,Error));
    auto* MID=OKCreateRuntimeSharedMaterial(Part,{},Current,Scene,Error);
    if(!TestNotNull(*FString::Printf(TEXT("current parent binds native probe: %s"),*Error),MID))return false;
    FLinearColor Actual;float Alpha=0;
    TestTrue(TEXT("native probe vector and alpha survive binding"),MID->GetVectorParameterValue(FMaterialParameterInfo(TEXT("unity_SHBr")),Actual)
        && MID->GetScalarParameterValue(FMaterialParameterInfo(TEXT("unity_SHBr__A")),Alpha)
        && Actual.Equals(Probe,.000001f) && FMath::Abs(Alpha-Probe.A)<.000001f);
    return true;
}

#endif
