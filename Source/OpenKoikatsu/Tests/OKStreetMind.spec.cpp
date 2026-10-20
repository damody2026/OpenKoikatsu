#include "../OKStreetSocialRules.h"
#include "../OKStreetSocialCalendar.h"
#include "../OKStreetMind.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/Guid.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOKStreetCalendarTest,
 "OpenKoikatsu.StreetSocial.Mind.Calendar",
 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FOKStreetCalendarTest::RunTest(const FString& Parameters){
 TestEqual(TEXT("Day 0 at 08:00 is 2010-09-01 08:00"),OKStreetCalendarDateTime(0,8.0),FDateTime(2010,9,1,8,0,0));
 TestEqual(TEXT("02:00 of game day 0 is already the next calendar date"),OKStreetCalendarDateTime(0,2.0),FDateTime(2010,9,2,2,0,0));
 TestEqual(TEXT("Game day 122 is 2011-01-01"),OKStreetCalendarDateTime(122,10.0),FDateTime(2011,1,1,10,0,0));
 TestEqual(TEXT("Label shows the weekday"),OKStreetCalendarLabel(0,8.5),FString(TEXT("2010/9/1（三） 08:30")));
 TestEqual(TEXT("Crossing 04:00 starts a day"),OKStreetDayStartsPassed(3.99,4.01),1);
 TestEqual(TEXT("Midnight alone does not"),OKStreetDayStartsPassed(23.9,0.1),0);
 TestEqual(TEXT("Wrapping past midnight and 04:00 does"),OKStreetDayStartsPassed(23.0,5.0),1);
 TestEqual(TEXT("A tick inside the day does not"),OKStreetDayStartsPassed(10.0,10.5),0);
 TestEqual(TEXT("Landing exactly on 04:00 counts once"),OKStreetDayStartsPassed(3.5,4.0),1);
 TestEqual(TEXT("Leaving 04:00 does not count again"),OKStreetDayStartsPassed(4.0,4.5),0);
 TestEqual(TEXT("No movement is no day"),OKStreetDayStartsPassed(4.0,4.0),0);
 TestEqual(TEXT("A step back is not a day"),OKStreetDayStartsPassed(17.6,17.59),0);
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOKStreetSaveSchema2Test,
 "OpenKoikatsu.StreetSocial.Mind.SaveSchema2",
 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FOKStreetSaveSchema2Test::RunTest(const FString& Parameters){
 FOKStreetStore Store;FString Error;
 Store.CalendarDay=37;Store.CalendarMinute=19*60+5;
 const FString Json=OKStreetWriteSave(Store);
 {TSharedPtr<FJsonObject> Root;double Schema=0;
  TestTrue(TEXT("Writes schema 2"),FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Root)&&Root->TryGetNumberField(TEXT("schema"),Schema)&&Schema==2);}
 FOKStreetStore Loaded;
 TestTrue(TEXT("Schema 2 reloads"),OKStreetParseSave(Json,Loaded,Error)==EOKStreetLoad::Ok);
 TestEqual(TEXT("Calendar day survives"),Loaded.CalendarDay,37);
 TestEqual(TEXT("Calendar minute survives"),Loaded.CalendarMinute,19*60+5);
 TestEqual(TEXT("Round trip is byte-stable"),OKStreetWriteSave(Loaded),Json);
 FOKStreetStore Old;
 TestTrue(TEXT("A schema 1 save still loads"),OKStreetParseSave(TEXT("{\"schema\":1,\"relations\":[]}"),Old,Error)==EOKStreetLoad::Ok);
 TestTrue(TEXT("and starts a new calendar"),Old.CalendarDay==0&&Old.CalendarMinute==-1);
 FOKStreetStore Bad;
 TestTrue(TEXT("Calendar minute past midnight is rejected"),OKStreetParseSave(TEXT("{\"schema\":2,\"relations\":[],\"calendar\":{\"day\":0,\"minute\":1440}}"),Bad,Error)==EOKStreetLoad::Invalid&&Error==TEXT("save_calendar_invalid"));
 TestTrue(TEXT("Negative day is rejected"),OKStreetParseSave(TEXT("{\"schema\":2,\"relations\":[],\"calendar\":{\"day\":-1,\"minute\":0}}"),Bad,Error)==EOKStreetLoad::Invalid);
 TestTrue(TEXT("Schema 3 is rejected"),OKStreetParseSave(TEXT("{\"schema\":3,\"relations\":[]}"),Bad,Error)==EOKStreetLoad::Invalid);
 return true;
}

// Needs Tools/ok-mind built (cargo build --release); proves the in-process DLL round-trips jobs and that a
// Rust panic comes back as an error result instead of taking the editor down.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOKStreetMindDllTest,
 "OpenKoikatsu.StreetSocial.Mind.Dll",
 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FOKStreetMindDllTest::RunTest(const FString& Parameters){
 const FString Dir=FPaths::ProjectSavedDir()/TEXT("Tests/StreetMind")/FGuid::NewGuid().ToString(EGuidFormats::Digits);
 FOKStreetMind Mind;FString Error;
 if(!Mind.Open(Dir,FString(),true,Error)){AddError(TEXT("ok_mind.dll did not open: ")+Error);return false;}
 AddInfo(TEXT("Loaded ")+Mind.DllPath());
 FString Status;
 TestTrue(TEXT("Status query answers"),Mind.Query(TEXT("{\"kind\":\"status\"}"),Status,Error)&&Status.Contains(TEXT("\"abi\":1")));
 const uint64 Ping=Mind.Submit(TEXT("{\"kind\":\"ping\",\"echo\":\"ue\"}"),Error);
 const uint64 Panic=Mind.Submit(TEXT("{\"kind\":\"debug_panic\"}"),Error);
 TestTrue(TEXT("Jobs accepted"),Ping>0&&Panic>0);
 TestEqual(TEXT("Invalid job is refused with an error"),Mind.Submit(TEXT("not json"),Error),uint64(0));
 TestTrue(TEXT("and the error explains why"),Error.StartsWith(TEXT("job_invalid")));
 TMap<uint64,TSharedPtr<FJsonObject>> Seen;
 const double Deadline=FPlatformTime::Seconds()+10;
 while(Seen.Num()<2&&FPlatformTime::Seconds()<Deadline){
  TArray<TSharedPtr<FJsonObject>> Results;Mind.Poll(Results);
  for(const auto& R:Results)Seen.Add(uint64(R->GetNumberField(TEXT("id"))),R);
  FPlatformProcess::Sleep(0.01f);
 }
 TestTrue(TEXT("Ping came back"),Seen.Contains(Ping)&&Seen[Ping]->GetBoolField(TEXT("ok"))&&Seen[Ping]->GetStringField(TEXT("echo"))==TEXT("ue"));
 TestTrue(TEXT("Panic came back as an error, the editor is still alive"),Seen.Contains(Panic)&&!Seen[Panic]->GetBoolField(TEXT("ok"))&&Seen[Panic]->GetStringField(TEXT("error")).StartsWith(TEXT("panic:")));
 TestTrue(TEXT("Save writes state.json"),Mind.Save(Error)&&FPaths::FileExists(Dir/TEXT("state.json")));
 Mind.Close();
 IFileManager::Get().DeleteDirectory(*Dir,false,true);
 return true;
}
#endif
