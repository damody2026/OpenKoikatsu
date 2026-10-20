#include "../OKStreetSocialRules.h"
#include "Misc/AutomationTest.h"
#include "../OKStreetSocialExpression.h"
#include "../OKStreetSocialPresentation.h"
#include "../OKStreetSocialTopics.h"
#include "../OKStreetSocialDialogueContext.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOKStreetExpressionCatalogueTest,
 "OpenKoikatsu.StreetSocial.SourceExpressions",
 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FOKStreetExpressionCatalogueTest::RunTest(const FString& Parameters){
 const auto Partial=OKStreetParseExpressions(TEXT("{\"schema\":1,\"profiles\":{\"0\":{\"test\":{\"values\":{\"eyes\":{\"pattern\":3},\"eyes_open\":0.4,\"blink\":false}}}}}"));
 TestTrue(TEXT("Partial source expression parses"),Partial.Error.IsEmpty());
 const auto* Expression=Partial.Find(0,TEXT("test"));TestNotNull(TEXT("Exact personality lookup"),Expression);
 if(Expression){TestEqual(TEXT("Selected eye pattern"),Expression->Face.EyePattern,3);TestEqual(TEXT("Absent brow preserves card"),Expression->Face.BrowPattern,-1);TestEqual(TEXT("Absent mouth preserves card"),Expression->Face.MouthPattern,-1);TestFalse(TEXT("Source blink disabled"),Expression->Blink);TestTrue(TEXT("Absent blend defaults true"),Expression->Face.EyeBlend);TestFalse(TEXT("Skipped brow does not blend"),Expression->Face.BrowBlend);TestFalse(TEXT("Skipped mouth does not blend"),Expression->Face.MouthBlend);}
 const auto Held=OKStreetParseExpressions(TEXT("{\"schema\":1,\"profiles\":{\"2\":{\"held\":{\"values\":{\"mouth\":{\"pattern\":4,\"blend\":false},\"brow\":{\"pattern\":1}}}}}}"));
 TestTrue(TEXT("Explicit blend parses"),Held.Error.IsEmpty());const auto* HeldExpression=Held.Find(2,TEXT("held"));
 if(HeldExpression){TestFalse(TEXT("Explicit mouth blend is immediate"),HeldExpression->Face.MouthBlend);TestTrue(TEXT("Present brow without blend flag blends"),HeldExpression->Face.BrowBlend);TestEqual(TEXT("Held mouth pattern"),HeldExpression->Face.MouthPattern,4);TestEqual(TEXT("Held eyes stay absent"),HeldExpression->Face.EyePattern,-1);TestFalse(TEXT("Absent eyes do not blend"),HeldExpression->Face.EyeBlend);}
 const auto BadBlend=OKStreetParseExpressions(TEXT("{\"schema\":1,\"profiles\":{\"0\":{\"bad\":{\"values\":{\"eyes\":{\"pattern\":1,\"blend\":1}}}}}}"));
 TestFalse(TEXT("Non-bool blend rejected"),BadBlend.Error.IsEmpty());
 TestNull(TEXT("Other personality cannot borrow expression"),Partial.Find(1,TEXT("test")));
 const auto Invalid=OKStreetParseExpressions(TEXT("{\"schema\":1,\"profiles\":{\"0\":{\"bad\":{\"values\":{\"eyes\":{\"pattern\":-1}}}}}}"));
 TestFalse(TEXT("Invalid source pattern rejected"),Invalid.Error.IsEmpty());
 FString Body;TestTrue(TEXT("Common expression catalogue exists"),FFileHelper::LoadFileToString(Body,*(FPaths::ProjectContentDir()/TEXT("IndependentResources/StreetSocialData/RuntimeV2/expressions.json"))));
 const auto Actual=OKStreetParseExpressions(Body);TestTrue(TEXT("Common expression schema"),Actual.Error.IsEmpty());TestEqual(TEXT("All original named expressions"),Actual.Items.Num(),3440);
 FString Dialogue;const FString Path=FPaths::ProjectContentDir()/TEXT("IndependentResources/StreetSocialData/TestFixtures/IntegrationCompletion02/dialogue.runtime.json");TestTrue(TEXT("Real dialogue exists"),FFileHelper::LoadFileToString(Dialogue,*Path));const auto Catalogue=OKStreetParseCatalogue(Dialogue,Path);
 int32 Covered=0;for(const auto& Node:Catalogue.Nodes){if(Node.FacialExpression.IsEmpty())continue;++Covered;TestNotNull(TEXT("Every dialogue expression resolves its own personality"),Actual.Find(Node.PersonalityId,Node.FacialExpression));}
 TestTrue(TEXT("Actual source nodes exercised"),Covered>0);return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOKStreetSocialRulesTest,
 "OpenKoikatsu.StreetSocial.RulesAndRealCatalogue",
 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FOKStreetSocialRulesTest::RunTest(const FString& Parameters){
 TArray<FString> Failures;
 OKStreetRunScenarios(Failures);
 // The rules on the real catalogues (lines, branches, farewells, events) are cargo tests: Tools/ok-rules tests/talk_data.rs.
 for(const FString& Failure:Failures)AddError(Failure);
 return Failures.IsEmpty();
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOKStreetIdentityRetargetTest,
 "OpenKoikatsu.StreetSocial.IdentityRetarget",
 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FOKStreetIdentityRetargetTest::RunTest(const FString& Parameters){
 TArray<FString> Failures;
 OKStreetCheckIdentityRetarget(Failures);
 for(const FString& Failure:Failures)AddError(Failure);
 return Failures.IsEmpty();
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOKStreetTalkCandidateEligibilityTest,
 "OpenKoikatsu.StreetSocial.TalkCandidateEligibility",
 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FOKStreetTalkCandidateEligibilityTest::RunTest(const FString& Parameters){
 TArray<FString> Failures;
 OKStreetCheckTalkCandidateEligibility(Failures);
 for(const FString& Failure:Failures)AddError(Failure);
 return Failures.IsEmpty();
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOKStreetTalkCandidateCameraTest,
 "OpenKoikatsu.StreetSocial.TalkCandidateCamera",
 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FOKStreetTalkCandidateCameraTest::RunTest(const FString& Parameters){
 TArray<FString> Failures;
 OKStreetCheckTalkCandidateCamera(Failures);
 for(const FString& Failure:Failures)AddError(Failure);
 return Failures.IsEmpty();
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOKStreetActivityStopsTest,
 "OpenKoikatsu.StreetSocial.ActivityStops",
 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FOKStreetActivityStopsTest::RunTest(const FString& Parameters){
 TArray<FString> Failures;OKStreetCheckActivityStops(Failures);
 for(const FString& Failure:Failures)AddError(Failure);
 return Failures.IsEmpty();
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOKStreetRelationshipVoiceConfirmationTest,
 "OpenKoikatsu.StreetSocial.RelationshipVoiceConfirmation",
 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FOKStreetRelationshipVoiceConfirmationTest::RunTest(const FString& Parameters){
 TestFalse(TEXT("Queued voice cannot confirm"),OKStreetRelationshipMayCommit(true,true,true,true,false));
 TestFalse(TEXT("Aborted incomplete voice cannot confirm even without a node"),OKStreetRelationshipMayCommit(true,true,false,true,false));
 TestFalse(TEXT("Active line cannot confirm a previously completed voice"),OKStreetRelationshipMayCommit(true,true,true,true,true));
 TestFalse(TEXT("Cancelled event cannot confirm"),OKStreetRelationshipMayCommit(false,true,false,true,true));
 TestFalse(TEXT("Released actor cannot confirm"),OKStreetRelationshipMayCommit(true,false,false,true,true));
 TestTrue(TEXT("Complete voiced preview can commit automatically while idle"),OKStreetRelationshipMayCommit(true,true,false,true,true));
 TestTrue(TEXT("Original text ADV preview still can commit automatically"),OKStreetRelationshipMayCommit(true,true,false,false,false));
 return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOKStreetPresentationTest,
 "OpenKoikatsu.StreetSocial.MovementFacingAndPlayerPrompts",
 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FOKStreetPresentationTest::RunTest(const FString& Parameters){
 for(const FVector Direction:{FVector(1,0,0),FVector(-1,0,0),FVector(0,1,0),FVector(0,-1,0),FVector(1,1,0),FVector(-1,-1,0)}){
  const auto Facing=OKStreetFacingMovement(Direction,FRotator::ZeroRotator);
  TestTrue(TEXT("APNG local +Y faces actual horizontal travel"),FVector::DotProduct(Facing.RotateVector(FVector::YAxisVector),Direction.GetSafeNormal())>.999f);
  TestTrue(TEXT("Travel does not pitch or roll the actor"),Facing.Pitch==0&&Facing.Roll==0);
 }
 for(float Dt:{0.f,.001f,1.f/120.f,1.f/60.f,.5f,4.f})for(float Start:{-179.f,-90.f,0.f,179.f})for(float Target:{-179.f,-90.f,0.f,179.f}){
  const auto Next=OKStreetLimitFemaleFacing(FRotator(0,Start,0),FRotator(0,Target,0),Dt);
  TestTrue(TEXT("Female angular speed is capped independently of frame rate"),FMath::Abs(FMath::FindDeltaAngleDegrees(Start,Next.Yaw))<=45.f*Dt+.001f);
  TestTrue(TEXT("Female takes shortest route towards target"),FMath::Abs(FMath::FindDeltaAngleDegrees(Next.Yaw,Target))<=FMath::Abs(FMath::FindDeltaAngleDegrees(Start,Target))+.001f);
 }
 TestEqual(TEXT("Horizontal mouse gain doubled"),OKStreetMouseYawSensitivity,.5f);
 TestEqual(TEXT("Vertical mouse gain unchanged"),OKStreetMousePitchSensitivity,.2f);
 const FRotator Previous(0,47,0);TestTrue(TEXT("Idle preserves facing"),OKStreetFacingMovement(FVector::ZeroVector,Previous).Equals(Previous));
 TestTrue(TEXT("Vertical-only delta preserves facing"),OKStreetFacingMovement(FVector(0,0,4),Previous).Equals(Previous));
 FOKStreetCatalogue Empty;TestTrue(TEXT("No source line does not invent a player prompt"),OKStreetSourcePlayerPrompt(Empty,0).IsEmpty());
 TestFalse(TEXT("Idle conversation may switch language or play farewell"),OKStreetConversationBusy(false,false,false,false));
 TestTrue(TEXT("Waiting answer or silent collected sentence locks language and shows cancel"),OKStreetConversationBusy(false,true,false,false));
 TestTrue(TEXT("Unconfirmed relationship remains busy without audio"),OKStreetConversationBusy(false,false,true,false));
 TestTrue(TEXT("Playing voice remains busy"),OKStreetConversationBusy(true,false,false,false));
 TestTrue(TEXT("Touch preparing remains busy without a line"),OKStreetConversationBusy(false,false,false,true));
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOKStreetCollectedTopicsTest,
 "OpenKoikatsu.StreetSocial.CollectedTopicInventoryAndEffects",
 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FOKStreetCollectedTopicsTest::RunTest(const FString& Parameters){
 FString Json,Error;FOKStreetTopics Data;
 TestTrue(TEXT("Shared source topic catalogue exists"),FFileHelper::LoadFileToString(Json,*(FPaths::ProjectContentDir()/TEXT("IndependentResources/StreetSocialData/RuntimeV2/topics.json"))));
 if(!TestTrue(TEXT("48 source topics and five independent personality profiles parse"),OKStreetReadTopics(Json,Data,Error)&&Data.Topics.Num()==48&&Data.Profiles.Num()==5))return false;
 // What answers are worth and the daily pickup are rules (Tools/ok-rules talk.rs, cargo test invitations_and_topics).
 FOKStreetStore Store;Store.TopicStock.Add(0,1);Store.TopicClaims.Add(0,Store.TopicDay);Store.TopicStock.Add(3,99);
 auto& Relation=Store.Relations.AddDefaulted_GetRef();Relation.Id=TEXT("topic-test");Relation.ListenRecent={0,3};Relation.SexDesire=10;Relation.TopicPoints=7;
 FOKStreetStore Reloaded;TestTrue(TEXT("Inventory, daily claims and heroine topic data roundtrip together"),OKStreetParseSave(OKStreetWriteSave(Store),Reloaded,Error)==EOKStreetLoad::Ok&&OKStreetWriteSave(Reloaded)==OKStreetWriteSave(Store));
 FString Invalid;TSharedPtr<FJsonObject> InvalidRoot;FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(OKStreetWriteSave(Store)),InvalidRoot);InvalidRoot->GetObjectField(TEXT("topic_inventory"))->GetObjectField(TEXT("stock"))->SetNumberField(TEXT("3"),100);FJsonSerializer::Serialize(InvalidRoot.ToSharedRef(),TJsonWriterFactory<>::Create(&Invalid));TestTrue(TEXT("Invalid inventory count rejects the save"),OKStreetParseSave(Invalid,Reloaded,Error)==EOKStreetLoad::Invalid);
 TestTrue(TEXT("Old empty save loads without inventory"),OKStreetParseSave(TEXT("{\"schema\":1,\"relations\":[]}"),Reloaded,Error)==EOKStreetLoad::Ok&&Reloaded.TopicStock.IsEmpty());
 return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOKStreetTouchAndDropTest,
 "OpenKoikatsu.StreetSocial.TouchMappingAndDialogueDrops",
 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FOKStreetTouchAndDropTest::RunTest(const FString& Parameters){
 FString Json,Error;FOKStreetTopics Data;
 if(!TestTrue(TEXT("Shared topics and source touch parse"),FFileHelper::LoadFileToString(Json,*(FPaths::ProjectContentDir()/TEXT("IndependentResources/StreetSocialData/RuntimeV2/topics.json")))&&OKStreetReadTopics(Json,Data,Error)))return false;
 TestEqual(TEXT("All original dialogue drop rows"),Data.Drops.Num(),48);
 TestTrue(TEXT("World touch allowed only in focused conversation cursor mode"),OKStreetWorldTouchAllowed(true,true,true,false,false));
 TestFalse(TEXT("UI click must never trigger world touch"),OKStreetWorldTouchAllowed(true,true,true,false,true));
 TestFalse(TEXT("Background window cannot touch"),OKStreetWorldTouchAllowed(false,true,true,false,false));
 TestFalse(TEXT("Camera mode cannot touch"),OKStreetWorldTouchAllowed(true,false,true,false,false));
 TestFalse(TEXT("No conversation target cannot touch"),OKStreetWorldTouchAllowed(true,true,false,false,false));
 TestFalse(TEXT("Active touch cannot overlap another"),OKStreetWorldTouchAllowed(true,true,true,true,false));
 TestEqual(TEXT("Left/right hand share touch reaction"),OKStreetTouchPart(2,false),OKStreetTouchPart(3,false));
 TestEqual(TEXT("Left/right chest share touch reaction"),OKStreetTouchPart(4,false),OKStreetTouchPart(5,false));
 TestEqual(TEXT("See mode never targets hands"),OKStreetTouchPart(2,true),INDEX_NONE);
 TestEqual(TEXT("Invalid part cannot target another body part"),OKStreetTouchPart(6,false),INDEX_NONE);
 TestEqual(TEXT("First reaction"),OKStreetTouchRepeat(1),0);TestEqual(TEXT("Second reaction"),OKStreetTouchRepeat(2),2);TestEqual(TEXT("Repeated reaction"),OKStreetTouchRepeat(3),1);
 TestEqual(TEXT("Long sessions saturate at repeated reaction without overflow"),OKStreetNextTouchCount(MAX_int32),3);TestEqual(TEXT("New session starts at first reaction"),OKStreetNextTouchCount(0),1);
 for(const auto& Profile:Data.Profiles){TestEqual(TEXT("All 54 source reactions retained including explicit unavailable row"),Profile.Touch.Num(),54);for(int32 Stage=0;Stage<3;++Stage)for(int32 Part=0;Part<4;++Part)for(int32 Repeat=0;Repeat<3;++Repeat){const auto* Reaction=OKStreetFindTouch(Profile,Stage,Part,false,Repeat);if(!TestNotNull(TEXT("Source touch metadata retained"),Reaction))continue;if(Stage==2&&Part==3&&Repeat==2){TestFalse(TEXT("Missing source second cheek must not use another voice"),Reaction->Available);TestTrue(TEXT("Unavailable source never carries replacement audio"),Reaction->Parts.IsEmpty());const auto* Next=OKStreetFindTouch(Profile,Stage,Part,false,OKStreetTouchRepeat(OKStreetNextTouchCount(2)));TestTrue(TEXT("Next repeat has exact available source after gap"),Next&&Next->Available);}else TestTrue(TEXT("Available touch relation/part/repeat has exact voiced reaction"),Reaction->Available);}}
 // Dialogue drops and the topic a line carries are rules (Tools/ok-rules talk.rs, cargo test invitations_and_topics).
 return true;
}
#endif
