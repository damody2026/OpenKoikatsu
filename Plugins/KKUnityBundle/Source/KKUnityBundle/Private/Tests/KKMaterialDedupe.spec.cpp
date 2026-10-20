#include "KKMaterialDedupe.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Materials/Material.h"
#include "Misc/AutomationTest.h"
#include "UObject/GarbageCollection.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOKMaterialDedupeLifetime,
    "OpenKoikatsu.MaterialDedupe.SceneLifetime",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOKMaterialDedupeLifetime::RunTest(const FString&)
{
    TStrongObjectPtr<UMaterial> Owner(NewObject<UMaterial>(GetTransientPackage()));
    const TWeakObjectPtr<UMaterial> Released = Owner.Get();
    FString Key;
    OKFindEqualMaterial(Owner.Get(), TEXT("SceneLifetime"), Key);
    OKRememberMaterial(Key, Owner.Get());
    FString HitKey;
    TestTrue(TEXT("live scene can reuse its material"),
        OKFindEqualMaterial(Owner.Get(), TEXT("SceneLifetime"), HitKey) == Owner.Get());
    Owner.Reset();
    CollectGarbage(RF_NoFlags, true);
    TestFalse(TEXT("dedupe must not retain an unloaded scene material"), Released.IsValid());
    return true;
}
#endif
