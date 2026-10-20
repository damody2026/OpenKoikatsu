#include "../OKGalleryWidget.h"
#include "../OKShapeSlider.h"
#include "../OKShapeDefaults.h"
#include "Components/TextBlock.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOKShapeResetTest, "OpenKoikatsu.Gallery.ShapeReset", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FOKShapeResetTest::RunTest(const FString&)
{
    auto* Widget = NewObject<UOKGalleryWidget>();
    auto* Slider = NewObject<UOKShapeSlider>(Widget);
    Slider->Owner = Widget;
    Slider->bBody = true;
    Slider->Index = 0;
    Slider->SetMinValue(-1.f);
    Slider->SetMaxValue(2.f);
    Slider->ValueLabel = NewObject<UTextBlock>(Slider);
    Widget->ShapeSliders.Add(Slider);
    for (int32 Sex : {0, 1})
    {
        Widget->Selected.Sex = Sex;
        Widget->Selected.Nickname = TEXT("Keep name");
        Widget->Selected.Body.ShapeValueBody.Init(1.7f, 44);
        Widget->Selected.Face.ShapeValueFace.Init(-.8f, 52);
        Slider->OnResetClicked();
        TestEqual(TEXT("Individual reset uses matching sex preset"), Widget->Selected.Body.ShapeValueBody[0], OKShapeDefaults::Value(Sex, true, 0));
        TestEqual(TEXT("Individual reset preserves neighboring value"), Widget->Selected.Body.ShapeValueBody[1], 1.7f);
        TestEqual(TEXT("Individual reset preserves face"), Widget->Selected.Face.ShapeValueFace[0], -.8f);
        TestEqual(TEXT("Slider synchronized"), Slider->GetValue(), Widget->Selected.Body.ShapeValueBody[0]);
        Widget->ResetShape(false, 0);
        TestEqual(TEXT("Individual face reset"), Widget->Selected.Face.ShapeValueFace[0], OKShapeDefaults::Value(Sex, false, 0));
        Widget->OnResetAllShapes();
        for (int32 Index = 0; Index < 44; ++Index)
            TestEqual(TEXT("All body defaults"), Widget->Selected.Body.ShapeValueBody[Index], OKShapeDefaults::Value(Sex, true, Index));
        for (int32 Index = 0; Index < 52; ++Index)
            TestEqual(TEXT("All face defaults"), Widget->Selected.Face.ShapeValueFace[Index], OKShapeDefaults::Value(Sex, false, Index));
        TestEqual(TEXT("Reset preserves identity"), Widget->Selected.Nickname, FString(TEXT("Keep name")));
        Widget->Selected.Body.ShapeValueBody[0] = 1.7f;
        Widget->bEnterInteractionAfterLoad = true;
        Slider->OnResetClicked();
        Widget->OnResetAllShapes();
        TestEqual(TEXT("Interaction transition blocks resets"), Widget->Selected.Body.ShapeValueBody[0], 1.7f);
        Widget->bEnterInteractionAfterLoad = false;
    }
    Widget->MarkAsGarbage();
    return !HasAnyErrors();
}
#endif
