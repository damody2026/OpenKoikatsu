#include "Misc/AutomationTest.h"
#include "HAL/PlatformTime.h"
#include "HAL/PlatformMisc.h"
#include "KKMsgpack.h"
#include "KKCardReader.h"
#include "KKTestFixtures.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKKSelectiveMsgpackTest,"OpenKoikatsu.Card.SelectiveExtendedData",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FKKSelectiveMsgpackTest::RunTest(const FString& Parameters)
{
    const TArray<FString> Keys={TEXT("keep")};FString Error;FKKMsgValue Result;
    const TArray<TArray<uint8>> Values={
        {0xc0},{0xc2},{0xc3},{0x7f},{0xff},{0xcc,255},{0xcd,0,1},{0xce,0,0,0,1},
        {0xcf,0,0,0,0,0,0,0,1},{0xd0,255},{0xd1,0,1},{0xd2,0,0,0,1},{0xd3,0,0,0,0,0,0,0,1},
        {0xca,0,0,0,0},{0xcb,0,0,0,0,0,0,0,0},{0xa1,'a'},{0xd9,1,'a'},{0xda,0,1,'a'},{0xdb,0,0,0,1,'a'},
        {0xc4,1,42},{0xc5,0,1,42},{0xc6,0,0,0,1,42},{0x91,0},{0xdc,0,1,0},{0xdd,0,0,0,1,0},
        {0x81,0,0},{0xde,0,1,0,0},{0xdf,0,0,0,1,0,0},
        {0xd4,0,42},{0xd5,0,0,0},{0xd6,0,0,0,0,0},{0xd7,0,0,0,0,0,0,0,0,0},
        {0xd8,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
        {0xc7,1,0,42},{0xc8,0,1,0,42},{0xc9,0,0,0,1,0,42}
    };
    const TArray<uint8> Prefix={0x82,0xa4,'k','e','e','p',42,0xa5,'o','t','h','e','r'};
    for(const auto& Value:Values)
    {
        TArray<uint8> Bytes=Prefix;Bytes.Append(Value);
        TestTrue(TEXT("skip each MessagePack encoding"),UnpackMsgpackSelectedMap(Bytes.GetData(),Bytes.Num(),Keys,Result,Error));
        TestEqual(TEXT("only selected key allocated"),Result.Map.Num(),1);
        if(Result.Find(TEXT("keep")))TestEqual(TEXT("selected value retained"),Result.Find(TEXT("keep"))->AsInt(),42);
        Bytes.Pop();TestFalse(TEXT("truncated map rejected"),UnpackMsgpackSelectedMap(Bytes.GetData(),Bytes.Num(),Keys,Result,Error));
        TestEqual(TEXT("failure publishes no partial map"),Result.Map.Num(),0);
    }
    TArray<uint8> Deep=Prefix;Deep.Init(0x91,4096);Deep.Insert(Prefix,0);Deep.Add(0);
    TestTrue(TEXT("deep skipped arrays are iterative"),UnpackMsgpackSelectedMap(Deep.GetData(),Deep.Num(),Keys,Result,Error));
    TArray<uint8> Overflow=Prefix;Overflow.Append({0xdd,255,255,255,255});
    TestFalse(TEXT("oversized array count rejected"),UnpackMsgpackSelectedMap(Overflow.GetData(),Overflow.Num(),Keys,Result,Error));
    TArray<uint8> Trailing=Prefix;Trailing.Append({0,0});
    TestFalse(TEXT("trailing data rejected"),UnpackMsgpackSelectedMap(Trailing.GetData(),Trailing.Num(),Keys,Result,Error));
    const TArray<uint8> Duplicate={0x82,0xa4,'k','e','e','p',42,0xa4,'k','e','e','p',43};
    TestTrue(TEXT("duplicate keys retain existing last-value semantics"),UnpackMsgpackSelectedMap(Duplicate.GetData(),Duplicate.Num(),Keys,Result,Error));
    if(Result.Find(TEXT("keep")))TestEqual(TEXT("last selected value"),Result.Find(TEXT("keep"))->AsInt(),43);

    FString CardPath=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_TEST_WARM_CARD"));
    if(CardPath.IsEmpty()){
        if(!TestTrue(TEXT("fixture"),EnsureMinimalKksCard(Error)))return false;
        CardPath=KKTestCardPath();
    }
    FKKCharacterFile Card;const double ReadStart=FPlatformTime::Seconds();
    if(!TestTrue(TEXT("load actual card"),UKKCardReader::LoadFromFile(CardPath,Card,Error)))return false;
    AddInfo(FString::Printf(TEXT("Actual card read_ms=%.3f bytes=%d"),(FPlatformTime::Seconds()-ReadStart)*1000.,Card.OriginalBytes.Num()));
    if(const TArray<uint8>* Raw=Card.RawBlocks.Find(TEXT("KKEx")))
    {
        const TArray<FString> ResolveKeys={TEXT("com.bepis.sideloader.universalautoresolver"),TEXT("EC.Core.Sideloader.UniversalAutoResolver")};
        FKKMsgValue Full,Selected;double Start=FPlatformTime::Seconds();
        if(!TestTrue(TEXT("independent full extended-data parse"),UnpackMsgpackExact(Raw->GetData(),Raw->Num(),Full,Error)))return false;
        const double FullMs=(FPlatformTime::Seconds()-Start)*1000.;Start=FPlatformTime::Seconds();
        if(!TestTrue(TEXT("selected extended-data parse"),UnpackMsgpackSelectedMap(Raw->GetData(),Raw->Num(),ResolveKeys,Selected,Error)))return false;
        const double SelectedMs=(FPlatformTime::Seconds()-Start)*1000.;
        for(const FString& Key:ResolveKeys)
        {
            const auto* A=Full.Find(*Key);const auto* B=Selected.Find(*Key);
            TestEqual(TEXT("resolver presence matches"),A!=nullptr,B!=nullptr);
            if(A&&B){TArray<uint8> Old,New;PackMsgpack(*A,Old,Error);PackMsgpack(*B,New,Error);TestTrue(TEXT("complete resolver payload matches full parser"),Old==New);}
        }
        AddInfo(FString::Printf(TEXT("KKEx bytes=%d full_parse_ms=%.3f selected_parse_ms=%.3f resolves=%d"),Raw->Num(),FullMs,SelectedMs,Card.Resolves.Num()));
    }
    return true;
}
#endif
