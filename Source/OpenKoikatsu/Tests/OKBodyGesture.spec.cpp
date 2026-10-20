#include "../OKBodyGesture.h"
#include "Misc/AutomationTest.h"
#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOKBodyGestureTest,"OpenKoikatsu.Interaction.BodyGesture",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FOKBodyGestureTest::RunTest(const FString&){
 FOKBodyGesture G;
 auto Input=[&](bool Left,bool UI=false,bool Valid=true,bool Enabled=true,bool Other=false,FVector2D P=FVector2D::ZeroVector,uint64 Hit=11){return G.Update(Enabled,Left,true,true,UI,Other,Hit,7,Valid,P);};
 TestTrue(TEXT("Idle hover"),Input(false).Event==EOKBodyGestureEvent::Hover);
 TestTrue(TEXT("UI cannot own press"),Input(true,true).Event==EOKBodyGestureEvent::None);
 TestTrue(TEXT("Held UI press cannot acquire on entering viewport"),Input(true).Event==EOKBodyGestureEvent::None);
 Input(false);
 TestTrue(TEXT("Viewport press owns hit"),Input(true).Event==EOKBodyGestureEvent::Press);
 const auto Drag=Input(true,false,true,true,false,FVector2D(3,4),22);
 TestTrue(TEXT("Dragging retains original target"),Drag.Event==EOKBodyGestureEvent::Drag&&Drag.Target==11&&Drag.Revision==7);
 TestEqual(TEXT("Raw displacement"),Drag.Delta,FVector2D(3,4));
 TestTrue(TEXT("Release stays with captured target"),Input(false).Event==EOKBodyGestureEvent::Release);
 Input(true);TestTrue(TEXT("Replacement cancels anchor"),Input(true,false,false).Event==EOKBodyGestureEvent::Cancel);
 TestFalse(TEXT("No reacquisition while held"),Input(true).Event==EOKBodyGestureEvent::Press);
 Input(false);Input(true);TestTrue(TEXT("Competing gesture cancels"),Input(true,false,true,true,true).Event==EOKBodyGestureEvent::Cancel);
 Input(false);Input(true);TestTrue(TEXT("Focus loss cancels"),G.Update(true,true,true,false,false,false,11,7,true,FVector2D::ZeroVector).Event==EOKBodyGestureEvent::Cancel);
 Input(false);Input(true);TestTrue(TEXT("Explicit exit cancellation"),G.Cancel().Event==EOKBodyGestureEvent::Cancel);
 TestTrue(TEXT("Cancel idempotent"),G.Cancel().Event==EOKBodyGestureEvent::None);
 TestFalse(TEXT("Scene reentry while held cannot acquire"),Input(true).Event==EOKBodyGestureEvent::Press);
 Input(false);TestTrue(TEXT("Empty ray cannot own"),Input(true,false,true,true,false,FVector2D::ZeroVector,0).Event==EOKBodyGestureEvent::None);
 Input(false);TestTrue(TEXT("Disabled input cannot own"),Input(true,false,true,false).Event==EOKBodyGestureEvent::None);
 // Same path at two frame rates has the same total displacement.
 auto Path=[](int32 N){FOKBodyGesture S;S.Update(true,false,true,true,false,false,1,1,true,FVector2D::ZeroVector);S.Update(true,true,true,true,false,false,1,1,true,FVector2D::ZeroVector);FVector2D Sum=FVector2D::ZeroVector;for(int32 I=1;I<=N;++I)Sum+=S.Update(true,true,true,true,false,false,1,1,true,FVector2D(120.*I/N,60.*I/N)).Delta;return Sum;};
 TestEqual(TEXT("60 and 120 input displacement matches"),Path(60),Path(120));
 return !HasAnyErrors();
}
#endif
