#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "KKCardReader.h"
#include "KKCardWriter.h"
#include "KKTestFixtures.h"
#include "KKMsgpack.h"
#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKKCardSpeechProfile,"OpenKoikatsu.KKCard.SpeechProfile",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FKKCardSpeechProfile::RunTest(const FString&){
    FString Error;FKKCharacterFile Base;
    if(!EnsureMinimalKksCard(Error)||!UKKCardReader::LoadFromFile(KKTestCardPath(),Base,Error)){AddError(Error);return false;}
    TestEqual(TEXT("absent rate defaults .5"),Base.VoiceRate,.5f);
    int32 Case=0;
    for(float Rate:{-2.f,0.f,.25f,1.f,2.f}){
        FKKCharacterFile File=Base;auto& Raw=File.RawBlocks.FindChecked(TEXT("Parameter"));FKKMsgValue Parameter;
        if(!UnpackMsgpackExact(Raw.GetData(),Raw.Num(),Parameter,Error)){AddError(Error);return false;}
        auto Value=MakeShared<FKKMsgValue>();Value->Kind=EKKMsgKind::Float;Value->f=Rate;
        Parameter.Map.Add(TEXT("voiceRate"),Value);Parameter.MapKeys.AddUnique(TEXT("voiceRate"));
        auto Personality=MakeShared<FKKMsgValue>();Personality->Kind=EKKMsgKind::Int;Personality->i=32;Parameter.Map.Add(TEXT("personality"),Personality);
        if(!PackMsgpack(Parameter,Raw,Error)){AddError(Error);return false;}
        const FString Path=FPaths::ProjectSavedDir()/FString::Printf(TEXT("Tests/Speech356/Profile%d.png"),Case++);
        FKKCharacterFile Loaded;
        if(!UKKCardWriter::SaveWithEdits(File,Path,Error)||!UKKCardReader::LoadFromFile(Path,Loaded,Error)){AddError(Error);return false;}
        const float Expected=FMath::Clamp(Rate,0.f,1.f);
        TestEqual(TEXT("parsed voiceRate"),Loaded.VoiceRate,Expected);TestEqual(TEXT("personality preserved"),Loaded.Personality,32);
        TestTrue(TEXT("native pitch range"),FMath::IsNearlyEqual(FMath::Lerp(.94f,1.06f,Loaded.VoiceRate),.94f+.12f*Expected));
    }
    return !HasAnyErrors();
}
#endif
