// Campus rules (Docs/STREET_SOCIAL_CAMPUS_PLAN.md): terms, timetables, exam scores, study and expulsion.
#include "../OKStreetSocialCampus.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
// Terms, timetables, grades and study are game rules (Tools/ok-rules campus.rs / academics.rs, cargo test).

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOKStreetCampusStudyTest, "OpenKoikatsu.StreetSocial.Campus.Save", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FOKStreetCampusStudyTest::RunTest(const FString&)
{
 // Save round trip.
 FOKStreetAcademic A; A.Dept = TEXT("csie"); A.Status = TEXT("leave"); A.Term = TEXT("2010-1"); A.Intelligence = 63.5f; A.LeaveUntil = TEXT("2010-2");
 A.Courses.SetNum(6); A.Courses[0].Name = TEXT("資料結構（一）"); A.Courses[0].Held = 5; A.Courses[0].Attended = 3; A.Courses[0].Midterm = 71; A.Transcript.Add(TEXT("2009-2|演算法（二）|72|85%"));
 FOKStreetAcademic B; OKStreetReadAcademic(OKStreetAcademicJson(A), B);
 TestEqual(TEXT("dept"), B.Dept, A.Dept); TestEqual(TEXT("status"), B.Status, A.Status); TestEqual(TEXT("leave"), B.LeaveUntil, A.LeaveUntil);
 TestEqual(TEXT("intelligence"), B.Intelligence, A.Intelligence); TestEqual(TEXT("courses"), B.Courses.Num(), 6);
 TestEqual(TEXT("attendance"), B.Courses[0].Attended, 3); TestEqual(TEXT("midterm"), B.Courses[0].Midterm, 71.f); TestEqual(TEXT("transcript"), B.Transcript.Num(), 1);
 return true;
}
#endif
