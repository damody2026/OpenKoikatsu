#include "KKAnimationReadyClock.h"
#include "Misc/AutomationTest.h"
#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKKAnimationReadyClockTest,"OpenKoikatsu.AnimationCatalog.LoadedWorldClock",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FKKAnimationReadyClockTest::RunTest(const FString&)
{
    FKKAnimationReadyClock Clock;Clock.Ready(40);
    TestEqual(TEXT("Explicit sampling preserves its requested delta"),Clock.Delta(.05f,41,false),.05f);
    TestEqual(TEXT("Same-frame explicit warm sampling remains exact"),Clock.Delta(.05f,40,true),.05f);
    TestEqual(TEXT("First world tick excludes time before the clip was ready"),Clock.Delta(.4f,41,true),0.f);
    TestEqual(TEXT("Normal playback keeps its original delta"),Clock.Delta(.016f,42,true),.016f);
    TestEqual(TEXT("Genuine later slow frames are not clamped"),Clock.Delta(.25f,43,true),.25f);
    TestEqual(TEXT("Loading time is rebased exactly once"),Clock.Rebases,uint64(1));
    Clock.Ready(44);Clock.Stop();
    TestEqual(TEXT("Stopping the clip cancels the pending rebase"),Clock.Delta(.03f,45,true),.03f);
    Clock.Ready(46);
    TestEqual(TEXT("A later selection starts its own clock origin"),Clock.Delta(.8f,47,true),0.f);
    TestEqual(TEXT("Both loading deltas are accounted for"),Clock.Rebases,uint64(2));
    return true;
}
#endif
