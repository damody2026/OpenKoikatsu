#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "KKCardReader.h"
#include "KKCardWriter.h"
#include "KKCardWorkspace.h"
#include "KKTestFixtures.h"
#include "KKMsgpack.h"
#include "UObject/Class.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKKIndependentShapeTest, "OpenKoikatsu.IndependentCard.ShapeRoundTrip",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FKKIndependentShapeTest::RunTest(const FString& Parameters)
{
    FString Err;
    if (!TestTrue(TEXT("fixture"), EnsureMinimalKksCard(Err))) return false;
    const FString Folder = FPaths::ProjectSavedDir() / TEXT("TestCards") / FGuid::NewGuid().ToString();
    IFileManager::Get().MakeDirectory(*Folder, true);
    for (int32 Layout = 0; Layout < 3; ++Layout)
    {
        FKKCharacterFile File;
        if (!TestTrue(TEXT("load"), UKKCardReader::LoadFromFile(KKTestCardPath(), File, Err))) return false;
        FKKMsgValue Custom;
        const TArray<uint8> Raw = File.RawBlocks.FindRef(TEXT("Custom"));
        if (!TestTrue(TEXT("parse custom"), UnpackMsgpack(Raw.GetData(), Raw.Num(), Custom, Err))) return false;
        TArray<float> Shapes; Shapes.Init(0.5f, 44);
        MsgSetFloatArray(*Custom.Map[TEXT("body")], TEXT("shapeValueBody"), Shapes);
        TArray<uint8> Bytes;
        if (Layout == 1)
        {
            TArray<FKKMsgValue> Parts = { *Custom.Map[TEXT("face")], *Custom.Map[TEXT("body")], *Custom.Map[TEXT("hair")] };
            TestTrue(TEXT("pack sequential"), PackLenPrefixedMaps(Parts, Bytes, Err));
        }
        else
        {
            TArray<uint8> Payload; TestTrue(TEXT("pack nested"), PackMsgpack(Custom, Payload, Err));
            if (Layout == 2) { int32 N=Payload.Num(); for (int I=0;I<4;++I) Bytes.Add(uint8(uint32(N)>>(I*8))); }
            Bytes.Append(Payload);
        }
        File.RawBlocks[TEXT("Custom")] = Bytes;
        File.OriginalBytes.Reset();
        File.Body.ShapeValueBody = Shapes;
        const FString Path = Folder / FString::Printf(TEXT("shape_%d.png"), Layout);
        if (!TestTrue(TEXT("baseline save"), UKKCardWriter::SaveWithEdits(File, Path, Err))) return false;
        TestTrue(TEXT("baseline reload"), UKKCardReader::LoadFromFile(Path, File, Err));
        FKKMsgValue Table;
        TestTrue(TEXT("parse block table"),UnpackMsgpack(File.Header.RawBlockTable.GetData(),File.Header.RawBlockTable.Num(),Table,Err));
        MsgSetString(*Table.Map[TEXT("lstInfo")]->Array[0],TEXT("version"),TEXT("custom_version_unchanged"));
        MsgSetString(Table,TEXT("futureMetadata"),TEXT("keep this"));
        TestTrue(TEXT("pack block metadata"),PackMsgpack(Table,File.Header.RawBlockTable,Err));
        const TArray<uint8> Extension = File.RawBlocks.FindRef(TEXT("KKEx"));
        File.Body.ShapeValueBody[0]=0.f; File.Body.ShapeValueBody[4]=1.f; File.Body.ShapeValueBody[22]=0.25f;
        File.Face.ShapeValueFace[0]=0.125f;
        File.Nickname=TEXT("independent");
        TestTrue(TEXT("edited save"), UKKCardWriter::SaveWithEdits(File, Path, Err));
        FKKCharacterFile Reloaded;
        TestTrue(TEXT("edited reload"), UKKCardReader::LoadFromFile(Path, Reloaded, Err));
        TestTrue(TEXT("all body channels preserved"), Reloaded.Body.ShapeValueBody == File.Body.ShapeValueBody);
        TestTrue(TEXT("face channels preserved"), Reloaded.Face.ShapeValueFace == File.Face.ShapeValueFace);
        TestTrue(TEXT("unknown extension unchanged"), Reloaded.RawBlocks.FindRef(TEXT("KKEx")) == Extension);
        TestEqual(TEXT("nickname"), Reloaded.Nickname, FString(TEXT("independent")));
        FKKMsgValue AfterTable;
        TestTrue(TEXT("parse saved table"),UnpackMsgpack(Reloaded.Header.RawBlockTable.GetData(),Reloaded.Header.RawBlockTable.Num(),AfterTable,Err));
        TestEqual(TEXT("unknown table field preserved"),AfterTable.Find(TEXT("futureMetadata"))->s,FString(TEXT("keep this")));
        TestEqual(TEXT("block version preserved"),AfterTable.Map[TEXT("lstInfo")]->Array[0]->Find(TEXT("version"))->s,FString(TEXT("custom_version_unchanged")));
        const TArray<uint8> Before = File.OriginalBytes;
        const TArray<uint8> BeforeCustom = File.RawBlocks.FindRef(TEXT("Custom"));
        File.Body.ShapeValueBody[0]=0.9f;
        TestFalse(TEXT("write failure"), UKKCardWriter::SaveWithEdits(File, Folder, Err));
        TestTrue(TEXT("failed write keeps original bytes"), File.OriginalBytes == Before);
        TestTrue(TEXT("failed write keeps raw custom"), File.RawBlocks.FindRef(TEXT("Custom")) == BeforeCustom);
        File.RawBlocks[TEXT("Custom")].Add(0xff);
        const TArray<uint8> Unsupported = File.RawBlocks[TEXT("Custom")];
        TestFalse(TEXT("unsupported trailing data rejected"),UKKCardWriter::SaveWithEdits(File,Folder / TEXT("unsupported.png"),Err));
        TestTrue(TEXT("rejection preserves custom data"),File.RawBlocks[TEXT("Custom")]==Unsupported);
        const FString NewPath = UKKCardWorkspace::NewCardPath(Folder, Path);
        TestFalse(TEXT("save as does not reuse original"), FPaths::IsSamePath(NewPath, Path));
    }
    return true;
}

namespace {
class FDeferredPreviewFixture final : public IKKDeferredCardPreview
{
public:
    TArray<uint8> Png;
    bool Fail = false;
    mutable int32 Calls = 0;
    virtual bool BuildPreviewPng(TArray<uint8>& Out, FString& Error) const override
    {
        ++Calls;
        if (Fail) { Error = TEXT("fixture materialization failure"); return false; }
        Out = Png; return true;
    }
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKKDeferredPreviewSaveTest,"OpenKoikatsu.IndependentCard.DeferredPreviewSave",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FKKDeferredPreviewSaveTest::RunTest(const FString& Parameters)
{
    FString Error;
    if (!TestTrue(TEXT("fixture"),EnsureMinimalKksCard(Error))) return false;
    FKKCharacterFile Card;
    if (!TestTrue(TEXT("load"),UKKCardReader::LoadFromFile(KKTestCardPath(),Card,Error))) return false;
    const FString Folder=FPaths::ProjectSavedDir()/TEXT("TestCards")/FGuid::NewGuid().ToString();
    IFileManager::Get().MakeDirectory(*Folder,true);
    const FString Output=Folder/TEXT("deferred.png");
    // The minimal fixture intentionally has no body shape array. Establish a
    // serialized shape baseline before testing a pending single-channel edit.
    Card.Body.ShapeValueBody.Init(0.5f,44);
    if(!TestTrue(TEXT("shape baseline"),UKKCardWriter::SaveWithEdits(Card,Folder/TEXT("baseline.png"),Error)))return false;
    auto Deferred=MakeShared<FDeferredPreviewFixture,ESPMode::ThreadSafe>();
    Deferred->Png=Card.Header.PreviewPng;
    Card.DeferredPreview=Deferred;
    const TArray<uint8> Original=Card.OriginalBytes;
    const TArray<uint8> Extension=Card.RawBlocks.FindRef(TEXT("KKEx"));
    Card.Nickname=TEXT("pending typed edit");
    Card.Body.ShapeValueBody[0]=0.123f;
    Deferred->Fail=true;
    TestFalse(TEXT("failed materialization rejects save"),UKKCardWriter::SaveWithEdits(Card,Output,Error));
    TestFalse(TEXT("failure creates no output"),IFileManager::Get().FileExists(*Output));
    TestTrue(TEXT("failure retains pinned revision"),Card.DeferredPreview.Get()==&Deferred.Get());
    TestTrue(TEXT("failure retains original bytes"),Card.OriginalBytes==Original);
    Deferred->Fail=false;
    const TArray<uint8> ValidPng=Deferred->Png;
    Deferred->Png.Reset();
    TestFalse(TEXT("invalid materialized PNG rejects save"),UKKCardWriter::SaveWithEdits(Card,Output,Error));
    TestTrue(TEXT("invalid PNG retains pinned revision"),Card.DeferredPreview.Get()==&Deferred.Get());
    Deferred->Png=ValidPng;
    FKKCharacterFile ScriptCopy;
    FKKCharacterFile::StaticStruct()->CopyScriptStruct(&ScriptCopy,&Card);
    TestTrue(TEXT("script struct copy pins the preview"),ScriptCopy.DeferredPreview.Get()==&Deferred.Get());
    FKKCharacterFile Copied=Card;
    TestTrue(TEXT("copied card pins the same immutable preview"),Copied.DeferredPreview.Get()==&Deferred.Get());
    const FString UnchangedPath=Folder/TEXT("unchanged.png");
    TestTrue(TEXT("save unchanged resolves resources"),UKKCardWriter::SaveUnchanged(Copied,UnchangedPath,Error));
    FKKCharacterFile Unchanged,OriginalCard;
    TestTrue(TEXT("read unchanged"),UKKCardReader::LoadFromFile(UnchangedPath,Unchanged,Error));
    TestTrue(TEXT("read original parameters"),UKKCardReader::LoadFromBytes(Original,OriginalCard,Error));
    TestEqual(TEXT("save unchanged retains original nickname"),Unchanged.Nickname,OriginalCard.Nickname);
    TestTrue(TEXT("save unchanged retains original shapes"),Unchanged.Body.ShapeValueBody==OriginalCard.Body.ShapeValueBody);
    TestTrue(TEXT("save unchanged retains source pin"),Copied.DeferredPreview.Get()==&Deferred.Get());
    TestFalse(TEXT("failed write retains source"),UKKCardWriter::SaveWithEdits(Card,Folder,Error));
    TestTrue(TEXT("failed write retains pinned revision"),Card.DeferredPreview.Get()==&Deferred.Get());
    TestTrue(TEXT("failed write retains original bytes"),Card.OriginalBytes==Original);
    TestTrue(TEXT("save pinned preview and pending parameters"),UKKCardWriter::SaveWithEdits(Card,Output,Error));
    TestFalse(TEXT("successful save resolves deferred revision"),Card.DeferredPreview.IsValid());
    FKKCharacterFile Reloaded;
    TestTrue(TEXT("reload"),UKKCardReader::LoadFromFile(Output,Reloaded,Error));
    TestTrue(TEXT("preview retained"),Reloaded.Header.PreviewPng==Deferred->Png);
    TestTrue(TEXT("unrelated extended data retained"),Reloaded.RawBlocks.FindRef(TEXT("KKEx"))==Extension);
    TestEqual(TEXT("pending nickname retained"),Reloaded.Nickname,FString(TEXT("pending typed edit")));
    TestEqual(TEXT("pending shape retained"),Reloaded.Body.ShapeValueBody[0],0.123f);
    const int32 Calls=Deferred->Calls;
    TestTrue(TEXT("resave"),UKKCardWriter::SaveWithEdits(Card,Output,Error));
    TestEqual(TEXT("resolved card does not rematerialize"),Deferred->Calls,Calls);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKKIndependentEmbeddedSaveTest,"OpenKoikatsu.IndependentCard.EmbeddedPngSave",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FKKIndependentEmbeddedSaveTest::RunTest(const FString& Parameters)
{
    const FString Path=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_TEST_PREVIEW_PACK"));
    if(!Path.EndsWith(TEXT(".png"))){AddWarning(TEXT("Set OK_TEST_PREVIEW_PACK to an embedded PNG to test asset preservation"));return true;}
    FKKCharacterFile File;FString Error;
    if(!TestTrue(TEXT("read embedded card"),UKKCardReader::LoadFromFile(Path,File,Error))) {AddError(Error);return false;}
    const TArray<uint8> PreviewBefore=File.Header.PreviewPng;
    const TArray<uint8> ExtensionBefore=File.RawBlocks.FindRef(TEXT("KKEx"));
    TestEqual(TEXT("embedded source detected"),UKKCardWorkspace::PortableSourcePath(Path),Path);
    TestTrue(TEXT("preview excludes large asset chunk"),UKKCardWorkspace::PngPreviewBytes(PreviewBefore).Num()<PreviewBefore.Num());
    File.Body.ShapeValueBody[0]=0.123f;File.Body.ShapeValueBody[4]=0.789f;File.Nickname=TEXT("portable edited");
    const FString Output=UKKCardWorkspace::NewCardPath(FPaths::GetPath(Path),Path);
    if(!TestTrue(TEXT("save embedded edits"),UKKCardWriter::SaveWithEdits(File,Output,Error))) {AddError(Error);return false;}
    FKKCharacterFile Reloaded;
    TestTrue(TEXT("reload saved embedded PNG"),UKKCardReader::LoadFromFile(Output,Reloaded,Error));
    TestEqual(TEXT("edited height"),Reloaded.Body.ShapeValueBody[0],0.123f);
    TestEqual(TEXT("edited bust"),Reloaded.Body.ShapeValueBody[4],0.789f);
    TestTrue(TEXT("embedded assets byte identical"),Reloaded.Header.PreviewPng==PreviewBefore);
    TestTrue(TEXT("extensions byte identical"),Reloaded.RawBlocks.FindRef(TEXT("KKEx"))==ExtensionBefore);
    TestEqual(TEXT("edited card remains standalone"),UKKCardWorkspace::PortableSourcePath(Output),Output);
    return true;
}
#endif
