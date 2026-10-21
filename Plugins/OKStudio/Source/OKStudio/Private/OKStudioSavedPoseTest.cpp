#include "OKSceneDocument.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOKStudioSavedPoseTest, "OpenKoikatsu.Studio.SavedPose",
								 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FOKStudioSavedPoseTest::RunTest(const FString &Parameters)
{
	UOKSceneDocument *Document = NewObject<UOKSceneDocument>();
	const int32 Id = Document->AddObject(EOKSceneObjectKind::Character, TEXT("saved pose"));
	FOKSceneCharacter &Character = Document->Find(Id)->Character;
	FOKSceneIkTarget Hand;
	TestFalse(TEXT("legacy IK targets retain position-only behavior"), Hand.bUseRotation);
	Hand.Effector = 5;
	Hand.Position = FVector(12, 47, 83);
	Hand.bUseRotation = true;
	Hand.Euler = FVector(34, 305, 72);
	Character.Ik.Add(Hand);
	Character.FixedNeck = {FQuat(FVector::UpVector, .3), FQuat(FVector::RightVector, -.2)};
	Character.NeckLook = EOKSceneLook::Fixed;
	Character.FixedEyes = {0, 0, 0, 1, 0, 0, 0, 1, -12, 8, 6, 6};
	Character.FixedEyesPattern = 3;
	Character.EyesLook = EOKSceneLook::Fixed;
	const TArray<FQuat> ExpectedNeck = Character.FixedNeck;
	FString Error;
	UOKSceneDocument *Loaded = NewObject<UOKSceneDocument>();
	TestTrue(TEXT("saved pose JSON opens"), Loaded->FromJson(Document->ToJson(), Error));
	const FOKSceneObject *Object = Loaded->Find(Id);
	if (!TestNotNull(TEXT("character survives roundtrip"), Object))
		return false;
	TestTrue(TEXT("IK position and source Euler orientation survive roundtrip"), Object->Character.Ik == Character.Ik);
	TestTrue(TEXT("FIX mode survives roundtrip"), Object->Character.NeckLook == EOKSceneLook::Fixed);
	TestTrue(TEXT("FIX eyes and source angles survive roundtrip"), Object->Character.EyesLook == EOKSceneLook::Fixed && Object->Character.FixedEyes == Character.FixedEyes && Object->Character.FixedEyesPattern == 3);
	TestEqual(TEXT("saved FIX neck keeps both source bones"), Object->Character.FixedNeck.Num(), 2);
	if (Object->Character.FixedNeck.Num() == 2)
		for (int32 Index = 0; Index < 2; ++Index)
			TestTrue(TEXT("FIX local quaternion survives roundtrip"),
					 Object->Character.FixedNeck[Index].Equals(ExpectedNeck[Index], 1.e-7));
	Loaded->Find(Id)->Character.Ik.Reset();
	Loaded->Find(Id)->Character.FixedNeck.Reset();
	Loaded->Find(Id)->Character.FixedEyes.Reset();
	TestTrue(TEXT("cleared pose JSON opens"), Document->FromJson(Loaded->ToJson(), Error));
	TestTrue(TEXT("cleared IK persists"), Document->Find(Id)->Character.Ik.IsEmpty());
	TestTrue(TEXT("cleared FIX persists"), Document->Find(Id)->Character.FixedNeck.IsEmpty());
	TestTrue(TEXT("cleared FIX eyes persist"), Document->Find(Id)->Character.FixedEyes.IsEmpty());
	return true;
}
#endif
