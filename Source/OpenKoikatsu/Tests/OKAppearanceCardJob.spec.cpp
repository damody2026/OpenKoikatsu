#include "../OKAppearanceCardJob.h"
#include "Async/Async.h"
#include "HAL/FileManager.h"
#include "KKCardReader.h"
#include "KKCardWriter.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOKAppearanceCardTransactionTest,
    "OpenKoikatsu.Gallery.AppearanceCardTransaction",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOKAppearanceCardTransactionTest::RunTest(const FString&)
{
    const FString Source=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_TEST_WARM_CARD"));
    FKKCharacterFile Live;FString Error;
    if(!UKKCardReader::LoadFromFile(Source,Live,Error)){AddError(Error);return false;}
    const FString Folder=FPaths::ProjectSavedDir()/TEXT("Tests/AppearanceCardTransaction")/FGuid::NewGuid().ToString(EGuidFormats::Digits);
    IFileManager::Get().MakeDirectory(*Folder,true);
    FOKAppearanceCardJob Job;Job.Card=Live;
    const FString SnapshotName=Live.Nickname;
    Live.Nickname=TEXT("稍後才修改的角色");
    const FString Input=Folder/TEXT("input.png"),Output=Folder/TEXT("output.png");
    struct FResult {bool Success=false,Worker=false,InputExact=false;FString Error;};
    const FResult Result=Async(EAsyncExecution::ThreadPool,[&]{
        FResult R;R.Worker=!IsInGameThread();
        R.Success=Job.Run(Input,Output,[&](FString& E){
            FKKCharacterFile Read;
            if(!UKKCardReader::LoadFromFile(Input,Read,E))return false;
            R.InputExact=Read.Nickname==SnapshotName&&Read.RawBlocks.OrderIndependentCompareEqual(Job.Card.RawBlocks)&&Read.Body.ShapeValueBody==Job.Card.Body.ShapeValueBody;
            Read.Nickname=TEXT("背景外觀結果");
            return UKKCardWriter::SaveWithEdits(Read,Output,E);
        },R.Error);return R;
    }).Get();
    TestTrue(TEXT("All card work ran off the game thread"),Result.Worker);
    TestTrue(TEXT("Background round trip succeeded: ")+Result.Error,Result.Success);
    TestTrue(TEXT("Worker used the captured snapshot including original mod blocks"),Result.InputExact);
    TestEqual(TEXT("Completed output parsed before submission"),Job.Card.Nickname,FString(TEXT("背景外觀結果")));
    TestEqual(TEXT("Live selection remains untouched before commit"),Live.Nickname,FString(TEXT("稍後才修改的角色")));
    TestTrue(TEXT("All stage timings are finite"),FMath::IsFinite(Job.SaveMs)&&FMath::IsFinite(Job.RequestMs)&&FMath::IsFinite(Job.ReadMs));
    struct FFailures {bool SaveRejected=false,SaveSkippedRequest=false,RequestRejected=false,ReadRejected=false,Catalog=false;};
    const FFailures Failures=Async(EAsyncExecution::ThreadPool,[&]{
        FFailures R;FKKCharacterFile Snapshot=Job.Card;FString E;bool Called=false;
        FOKAppearanceCardJob BadSave;BadSave.Card=Snapshot;
        R.SaveRejected=!BadSave.Run(Folder,Output,[&](FString&){Called=true;return true;},E)&&!E.IsEmpty();R.SaveSkippedRequest=!Called;
        FOKAppearanceCardJob BadRequest;BadRequest.Card=Snapshot;
        R.RequestRejected=!BadRequest.Run(Folder/TEXT("request-fail.png"),Output,[](FString& Failure){Failure=TEXT("預期的原生失敗");return false;},E)&&E==TEXT("預期的原生失敗");
        FOKAppearanceCardJob BadRead;BadRead.Card=Snapshot;
        R.ReadRejected=!BadRead.Run(Folder/TEXT("read-fail.png"),Folder/TEXT("missing.png"),[](FString&){return true;},E)&&!E.IsEmpty();
        FOKAppearanceCardJob Catalog;Catalog.Card=Snapshot;
        R.Catalog=Catalog.Run(Folder/TEXT("catalog.png"),FString(),[](FString&){return true;},E)&&Catalog.ReadMs==0&&Catalog.Card.Nickname==Snapshot.Nickname;
        return R;
    }).Get();
    TestTrue(TEXT("Failed save rejected without invoking native request"),Failures.SaveRejected&&Failures.SaveSkippedRequest);
    TestTrue(TEXT("Native failure rejected with its error preserved"),Failures.RequestRejected);
    TestTrue(TEXT("Unreadable output rejected before commit"),Failures.ReadRejected);
    TestTrue(TEXT("Read-only property catalog requires no output card"),Failures.Catalog);
    return true;
}
#endif
