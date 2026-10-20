#include "OKSceneDocument.h"
#include "OKStudioCatalog.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOKStudioChildAnimationTest, "OpenKoikatsu.Studio.AnimatedChildPoint",
								 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FOKStudioChildAnimationTest::RunTest(const FString &Parameters)
{
	const FOKStudioCatalog &Catalog = FOKStudioCatalog::Get();
	TestEqual(TEXT("all animated base-library attachment points converted"), Catalog.ChildAnimations.Num(), 32);
	const FOKStudioChildAnimation *Clip = Catalog.ChildAnimations.Find(FIntVector(10, 3, 397));
	if (!TestNotNull(TEXT("source rotating attachment clip"), Clip))
		return false;
	auto Point = [&](float Phase, float Seconds = 0.f, float Speed = 1.f) {
		return Catalog.AnimatedItemChildPoint(10, 3, 397, 0, Phase, Seconds, Speed);
	};
	const FQuat Expected(FVector::UpVector, -UE_TWO_PI * .4f);
	TestTrue(TEXT("source N_studio_kaiten rotates about Unity Y / UE Z"), FMath::Abs(Point(.4f).GetRotation() | Expected) > .9999);
	TestTrue(TEXT("saved normalized time retains its fractional phase"), Point(311.40283203125f).Equals(Point(.40283203125f), .0001f));
	TestTrue(TEXT("elapsed seconds apply source duration and speed"), Point(.1f, Clip->Duration, .3f).Equals(Point(.4f), .001f));
	TestTrue(TEXT("negative rate wraps backwards"), Point(.1f, Clip->Duration, -.7f).Equals(Point(.4f), .001f));
	TestTrue(TEXT("zero rate holds"), Point(.4f, 10.f, 0.f).Equals(Point(.4f), .001f));
	TestTrue(TEXT("unsupported pattern retains the static fallback"),
			 Catalog.AnimatedItemChildPoint(10, 3, 397, 1, .4f, 0.f, 1.f).Equals(Catalog.ItemChildPoint(10, 3, 397), .001f));
	FOKStudioCatalog Synthetic;
	FOKStudioChildAnimation Hold;
	Hold.Duration = 2.f;
	Hold.Frames = {FTransform(FVector(10, 0, 0)), FTransform(FVector(30, 0, 0))};
	Synthetic.ChildAnimations.Add(FIntVector(1, 2, 3), Hold);
	TestTrue(TEXT("nonlooping attachment holds its end"),
			 Synthetic.AnimatedItemChildPoint(1, 2, 3, 0, 7.f, 0.f, 1.f).GetLocation().Equals(FVector(30, 0, 0)));
	TestTrue(TEXT("nonlooping attachment clamps before its start"),
			 Synthetic.AnimatedItemChildPoint(1, 2, 3, 0, -1.f, 0.f, 1.f).GetLocation().Equals(FVector(10, 0, 0)));
	TestTrue(TEXT("attachment translation interpolates"),
			 Synthetic.AnimatedItemChildPoint(1, 2, 3, 0, .25f, 0.f, 1.f).GetLocation().Equals(FVector(15, 0, 0)));
	UOKSceneDocument *Doc = NewObject<UOKSceneDocument>();
	const int32 Parent = Doc->AddObject(EOKSceneObjectKind::Item, TEXT("animated"));
	FOKSceneObject *O = Doc->Find(Parent);
	O->Item.Group = 10;
	O->Item.Category = 3;
	O->Item.No = 397;
	O->Item.AnimeTime = .4f;
	O->Item.AnimeSpeed = .3f;
	O->Transform = FTransform(FRotator(13, 51, -9), FVector(30, -20, 170), FVector(.16));
	const FTransform ParentWorld = O->Transform;
	const int32 Child = Doc->AddObject(EOKSceneObjectKind::Folder, TEXT("child"), Parent);
	Doc->Find(Child)->Transform = FTransform(FRotator(7, 24, 12), FVector(50, 10, 20));
	const FTransform ChildLocal = Doc->Find(Child)->Transform;
	const FTransform ExpectedWorld = ChildLocal * Point(.4f) * ParentWorld;
	TestTrue(TEXT("nested item scale and rotation apply the animated point once"), Doc->WorldTransform(Child).Equals(ExpectedWorld, .001f));
	TestTrue(TEXT("gizmo parent frame matches placement"), Doc->ParentFrame(Child).Equals(Point(.4f) * ParentWorld, .001f));
	FString Error;
	const FString Json = Doc->ToJson();
	Doc->ItemAnimationSeconds = 17.f;
	TestTrue(TEXT("scene roundtrip"), Doc->FromJson(Json, Error));
	TestEqual(TEXT("roundtrip resets elapsed clock"), Doc->ItemAnimationSeconds, 0.f);
	TestTrue(TEXT("roundtrip retains saved phase and child placement"), Doc->WorldTransform(Child).Equals(ExpectedWorld, .001f));
	Doc->ItemAnimationSeconds = Clip->Duration;
	TestTrue(TEXT("timeline evaluates attachment at elapsed time"),
			 Doc->WorldTransform(Child).Equals(ChildLocal * Point(.7f) * ParentWorld, .001f));
	return true;
}
#endif
