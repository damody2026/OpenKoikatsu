#include "../OKBodyInteraction.h"
#include "KKPreviewActor.h"
#include "KKExactSkinning.h"
#include "KKPairedPlayback.h"
#include "Serialization/JsonSerializer.h"
#include "Engine/World.h"
#include "Misc/Paths.h"
#include "Misc/AutomationTest.h"
#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOKBodyTouchSoloTest,"OpenKoikatsu.Interaction.BodyTouchSolo",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FOKBodyTouchSoloTest::RunTest(const FString&){
 auto* World=UWorld::CreateWorld(EWorldType::Game,false);auto* Actor=World->SpawnActor<AKKPreviewActor>();FString Error;
 auto Finish=[&](){World->DestroyWorld(false);return !HasAnyErrors();};
 if(!TestTrue(TEXT("Solo APNG opens without game root"),Actor->Session.Open(FString(),FPaths::ProjectContentDir()/TEXT("CharacterCards/CrowdAPNG/C001.png"),0,Error))){AddError(Error);return Finish();}
 TArray<TSharedPtr<FJsonValue>> Rows;FString Key,Identity;const FString Root=FOkSession::CommonPairRoot();
 if(!TestTrue(TEXT("Shared interaction catalogue parses"),FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(FOkSession::CommonInteractionPoses()),Rows)))return Finish();
 for(const auto& V:Rows){auto Row=V->AsObject();if(Row->GetIntegerField(TEXT("participants"))!=1)continue;
  const FString Source=Row->GetStringField(TEXT("female_source"));
  for(const auto& Label:FOkSession::AnimationSourceNames(Root,Source))if(Label.EndsWith(TEXT(" / [Controller layer 0] Base Layer.Idle"))){
   Key=FOkSession::PairClipForRoot(Root,Source+TEXT("|")+Label,Row->GetIntegerField(TEXT("female_layer")));
   if(!Key.IsEmpty()&&FOkSession::PrepareAnimationClip(Root,Key)){Identity=FString::Printf(TEXT("%s:%d"),*Row->GetStringField(TEXT("category")),Row->GetIntegerField(TEXT("id")));break;}Key.Reset();
  }if(!Key.IsEmpty())break;
 }
 if(!TestTrue(TEXT("Original solo Idle resolves"),!Key.IsEmpty()))return Finish();
 FOKBodyInteraction Touch;if(!TestTrue(TEXT("Solo response prepares"),Touch.Prepare(Actor,Error))){AddError(Error);return Finish();}
 Touch.SetContext(Identity,TEXT("Idle"));TestTrue(TEXT("Solo Idle has original reactions"),Touch.IsAvailable());
 if(!TestTrue(TEXT("Solo controller starts"),Actor->SetGameSceneMode(3,false)&&Actor->SetGameAnimation(Key)))return Finish();
 const auto Shape=Actor->Session.ShapeBody();const float Clock=Actor->AnimPhase;float Before[16];Actor->Session.ApplyAnimFrame(0.f,0.f);Actor->Session.BoneWorldMatrix(TEXT("cf_j_spine03"),Before);
 TestTrue(TEXT("Solo hit begins"),Actor->Session.BodyReactionHit(0,{FVector3f(0,1,0),FVector3f(0,1,0),FVector3f(0,1,0)},.3f));
 double Maximum=0.;for(int32 I=0;I<160;++I){Actor->ApplyPoseFrame(1.f/120.f,false);float Current[16];if(Actor->Session.BoneWorldMatrix(TEXT("cf_j_spine03"),Current))for(int32 J=0;J<16;++J)Maximum=FMath::Max(Maximum,double(FMath::Abs(Current[J]-Before[J])));}
 TestEqual(TEXT("Paused solo clock stays frozen"),Actor->AnimPhase,Clock);TestTrue(TEXT("Solo animation reacts at frozen base time"),Maximum>1.e-4);TestFalse(TEXT("Solo reaction finishes"),Actor->Session.IsBodyReactionPlaying());TestTrue(TEXT("Solo reaction preserves body parameters"),Shape==Actor->Session.ShapeBody());
 return Finish();
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOKBodyTouchApngTest,"OpenKoikatsu.Interaction.BodyTouchAPNG",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FOKBodyTouchApngTest::RunTest(const FString&){
 auto* World=UWorld::CreateWorld(EWorldType::Game,false);auto* Actor=World->SpawnActor<AKKPreviewActor>();
 FString Error;FOKBodyInteraction Touch;
 const FString Card=FPaths::ProjectContentDir()/TEXT("CharacterCards/CrowdAPNG/C001.png");
 if(!TestTrue(TEXT("Open APNG without game root"),Actor->Session.Open(FString(),Card,0,Error))){AddError(Error);World->DestroyWorld(false);return false;}
 const auto Shape=Actor->Session.ShapeBody();
 TestTrue(TEXT("Runtime collider and authored solver preparation"),Touch.Prepare(Actor,Error));
 if(!Error.IsEmpty())AddError(Error);
 Touch.SetContext(TEXT("AnimationInfo_01:0"),TEXT("Idle"));
 TestTrue(TEXT("Authored pose exposes allowed reactions"),Touch.IsAvailable());
 int32 Hit=INDEX_NONE;double Distance=0.;FVector Origin,Direction;
 for(const TCHAR* Bone:{TEXT("cf_hit_wrist_L"),TEXT("cf_hit_arm_L"),TEXT("cf_hit_berry"),TEXT("cf_hit_spine03")}){
  float Raw[16];if(!Actor->Session.BoneWorldMatrix(Bone,Raw))continue;
  const FVector Center=OKSkinPaletteToUnreal(Raw).GetOrigin();
  for(const FVector Axis:{FVector(1,0,0),FVector(-1,0,0),FVector(0,1,0),FVector(0,-1,0),FVector(0,0,1)}){
   Origin=Center+Axis*200.;Direction=-Axis;Hit=Touch.Query(Origin,Direction,Distance);if(Hit!=INDEX_NONE)break;
  }
  if(Hit!=INDEX_NONE)break;
 }
 TestTrue(TEXT("Ray hits a real allowed collider"),Hit!=INDEX_NONE&&Distance>0&&Distance<400.);
 if(Hit!=INDEX_NONE){
  const double OriginalDistance=Distance;const FTransform Placement(FRotator(0,37,0),FVector(70,-90,20),FVector(1.3));
  Actor->SetActorTransform(Placement);
  TestEqual(TEXT("Actor placement preserves collider identity"),Touch.Query(Placement.TransformPosition(Origin),Placement.TransformVectorNoScale(Direction),Distance),Hit);
  TestTrue(TEXT("Actor scale changes ray distance consistently"),FMath::Abs(Distance-OriginalDistance*1.3)<.001);
  Actor->SetActorTransform(FTransform::Identity);
 }
 if(Hit!=INDEX_NONE){
  TestTrue(TEXT("Click starts authored reaction"),Touch.Press(Hit));
  TestTrue(TEXT("Native character reaction is active"),Actor->Session.IsBodyReactionPlaying());
  TestFalse(TEXT("Ordinary click does not restart an active reaction"),Touch.Press(Hit));
  const uint64 Revision=Touch.Revision;
  Touch.SetContext(TEXT("AnimationInfo_01:0"),TEXT("missing-state"));
  TestTrue(TEXT("Pose replacement invalidates captured anchor"),Touch.Revision!=Revision);
  TestFalse(TEXT("Pose replacement clears active native response"),Actor->Session.IsBodyReactionPlaying());
  TestFalse(TEXT("Missing mapping is unavailable"),Touch.IsAvailable());
  TestEqual(TEXT("Cannot click through unavailable surface"),Touch.Query(Origin,Direction,Distance),INDEX_NONE);
 }
 TestTrue(TEXT("Touch preserves body parameters"),Actor->Session.ShapeBody()==Shape);
 Touch.Cancel();Touch.Cancel();TestFalse(TEXT("Exit cancellation is idempotent"),Actor->Session.IsBodyReactionPlaying());
 World->DestroyWorld(false);return !HasAnyErrors();
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOKBodyTouchPairTest,"OpenKoikatsu.Interaction.BodyTouchPausedPair",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FOKBodyTouchPairTest::RunTest(const FString&){
 TArray<FString> Cards;FPlatformMisc::GetEnvironmentVariable(TEXT("OK_LIVE_PALETTE_AUDIT_CARDS")).ParseIntoArray(Cards,TEXT("|"),true);
 if(Cards.Num()<2){AddInfo(TEXT("Paired APNG fixture omitted; provide female/male -PaletteCards"));return true;}
 auto* World=UWorld::CreateWorld(EWorldType::Game,false);auto* Female=World->SpawnActor<AKKPreviewActor>();auto* Male=World->SpawnActor<AKKPreviewActor>();auto* Pair=World->SpawnActor<AKKPairedPlayback>();
 FString Error;
 auto Finish=[&](){Pair->Stop();World->DestroyWorld(false);return !HasAnyErrors();};
 if(!TestTrue(TEXT("Female APNG opens"),Female->Session.Open(FString(),Cards[0],0,Error))||!TestTrue(TEXT("Male APNG opens"),Male->Session.Open(FString(),Cards[1],0,Error))){AddError(Error);return Finish();}
 TArray<TSharedPtr<FJsonValue>> Rows;
 if(!TestTrue(TEXT("Common pair catalogue"),FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Female->Session.NativeAnimationPairs()),Rows)))return Finish();
 TSharedPtr<FJsonObject> Selected;for(const auto& V:Rows){auto O=V->AsObject();if(O->GetStringField(TEXT("category"))==TEXT("AnimationInfo_01")&&O->GetIntegerField(TEXT("id"))==0){Selected=O;break;}}
 if(!TestTrue(TEXT("Authored pair exists"),Selected.IsValid()))return Finish();
 const FString Root=FOkSession::CommonPairRoot();
 auto Key=[&](const TCHAR* Source,const TCHAR* Layer){const FString S=Selected->GetStringField(Source);for(const auto& Label:FOkSession::AnimationSourceNames(Root,S))if(Label.EndsWith(TEXT(" / [Controller layer 0] Base Layer.Idle")))return FOkSession::PairClipForRoot(Root,S+TEXT("|")+Label,Selected->GetIntegerField(Layer));return FString();};
 const FString FK=Key(TEXT("female_source"),TEXT("female_layer")),MK=Key(TEXT("male_source"),TEXT("male_layer"));
 if(!TestTrue(TEXT("Both authored Idle states resolve"),!FK.IsEmpty()&&!MK.IsEmpty()))return Finish();
 const FString FM=Selected->GetStringField(TEXT("female_motion")),MM=Selected->GetStringField(TEXT("male_motion"));
 if(!TestTrue(TEXT("Native pair preparation"),FOkSession::PrepareAnimationClip(Root,FK)&&FOkSession::PrepareAnimationClip(Root,MK)&&FOkSession::PrepareAnimationPair(Root,TEXT("Idle"),FM,MM)))return Finish();
 FOKBodyInteraction Touch;if(!TestTrue(TEXT("Body runtime prepared"),Touch.Prepare(Female,Error))){AddError(Error);return Finish();}
 if(!TestTrue(TEXT("Pair starts"),Pair->Start(Female,Male,FK,MK,TEXT("Idle"),FM,MM,Error))){AddError(Error);return Finish();}
 Pair->SetPaused(true);const float Clock=Pair->GetElapsed();float Before[16];Female->Session.BoneWorldMatrix(TEXT("cf_j_spine03"),Before);
 const TArray<FVector3f> Forces={FVector3f(0,1,0),FVector3f(0,1,0),FVector3f(0,1,0)};
 TestTrue(TEXT("Reaction begins while paused"),Female->Session.BodyReactionHit(0,Forces,.3f));Female->bBodyReactionPoseDirty=true;
 double Maximum=0.;
 for(int32 I=0;I<160;++I){if(!Pair->Advance(1.f/120.f,Error)){AddError(Error);break;}float Current[16];if(Female->Session.BoneWorldMatrix(TEXT("cf_j_spine03"),Current))for(int32 J=0;J<16;++J)Maximum=FMath::Max(Maximum,double(FMath::Abs(Current[J]-Before[J])));}
 TestTrue(TEXT("Paused animation still visibly responds"),Maximum>1.e-4);
 TestFalse(TEXT("Paused reaction reaches completion"),Female->Session.IsBodyReactionPlaying());
 TestEqual(TEXT("Reaction cannot advance paused pair clock"),Pair->GetElapsed(),Clock);
 TestEqual(TEXT("Role clocks remain synchronized"),Female->AnimPhase,Male->AnimPhase);
 return Finish();
}
#endif
