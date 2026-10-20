#if WITH_DEV_AUTOMATION_TESTS
#include "KKRustExtract.h"
#include "ok_extract.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"

// Plays a video-derived clip (Saved/VideoMotions, Tools/video_motion) on a game
// session and a portable APNG session. Mapped bones must follow the clip, and
// the game session must still switch back to a native clip afterwards.
//   OK_VIDEO_TEST_GAME  game root (default C:/portable/KoikatuSunshine)
//   OK_VIDEO_TEST_CARD  game card PNG (skips the game half if missing)
//   OK_VIDEO_TEST_APNG  portable card (default CrowdAPNG/C000.png)
//   OK_VIDEO_TEST_DUMP  optional JSON path for sampled local rotations
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKKVideoMotionPlaybackTest,"OpenKoikatsu.VideoMotion.Playback",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FKKVideoMotionPlaybackTest::RunTest(const FString&){
 const TArray<FString> Keys=FOkSession::VideoClipNames();
 if(Keys.IsEmpty()){AddInfo(TEXT("No Saved/VideoMotions/*.okanim; import a video first (Tools/ok-video: ok-video.exe import <video>)"));return true;}
 const FString Key=Keys[0];
 const TArray<FString> Bones={TEXT("cf_j_hips"),TEXT("cf_j_head"),TEXT("cf_j_hand_L"),TEXT("cf_j_hand_R"),TEXT("cf_j_leg03_L"),TEXT("cf_j_leg03_R")};
 auto Env=[](const TCHAR* Name,const FString& Default){const FString V=FPlatformMisc::GetEnvironmentVariable(Name);return V.IsEmpty()?Default:V;};
 TSharedRef<FJsonObject> Dump=MakeShared<FJsonObject>();Dump->SetStringField(TEXT("key"),Key);

 // World positions (Unity metres, row-major: translation at 3/7/11) of
 // end effectors: they must move with the clip and stay finite.
 auto Sample=[&](FOkSession& Session,const FString& Label)->bool{
  TSharedRef<FJsonObject> Frames=MakeShared<FJsonObject>();
  TMap<FString,FVector3f> First;float Travel=0;
  for(const float Time:{0.f,1.f,3.f,5.f,7.f}){
   if(!TestTrue(Label+TEXT(": apply video frame"),Session.ApplyAnimFrame(Time,1.f/30.f)))return false;
   TSharedRef<FJsonObject> Row=MakeShared<FJsonObject>();
   for(const FString& Bone:Bones){
    float M[16];if(!TestTrue(*(Label+TEXT(": world matrix ")+Bone),Session.BoneWorldMatrix(Bone,M)))return false;
    const FVector3f P(M[3],M[7],M[11]);
    if(!TestTrue(Label+TEXT(": finite position"),!P.ContainsNaN()))return false;
    if(const FVector3f* F=First.Find(Bone))Travel=FMath::Max(Travel,FVector3f::Dist(*F,P));else First.Add(Bone,P);
    TArray<TSharedPtr<FJsonValue>> Values;for(int32 C=0;C<16;++C)Values.Add(MakeShared<FJsonValueNumber>(M[C]));
    Row->SetArrayField(Bone,Values);
   }
   Frames->SetObjectField(FString::SanitizeFloat(Time),Row);
  }
  Dump->SetObjectField(Label,Frames);
  AddInfo(FString::Printf(TEXT("%s: max end-effector travel %.3f m"),*Label,Travel));
  return TestTrue(Label+TEXT(": bones follow the video clip"),Travel>0.1f);
 };

 const FString Game=Env(TEXT("OK_VIDEO_TEST_GAME"),TEXT("C:/portable/KoikatuSunshine"));
 const FString Card=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_VIDEO_TEST_CARD"));
 if(!Card.IsEmpty()&&FPaths::FileExists(Card)){
  FOkSession Session;FString Error;
  if(!TestTrue(TEXT("Open game session"),Session.Open(Game,Card,0,Error))){AddError(Error);return false;}
  if(!TestTrue(TEXT("Game session plays video clip"),Session.HasAnimationClip(Key)&&Session.SetAnimClip(Key)))return false;
  Sample(Session,TEXT("game"));
  // The video install must not replace the game root (native clips resolve again).
  const FString Native=TEXT("studio/anime/00.unity3d|f_tachi_00_02");
  TestTrue(TEXT("Native clip still selectable after video clip"),Session.SetAnimClip(Native)&&Session.ApplyAnimFrame(0.5f,1.f/30.f));
 } else AddInfo(TEXT("OK_VIDEO_TEST_CARD not set; game session half skipped"));

 const FString Apng=Env(TEXT("OK_VIDEO_TEST_APNG"),FPaths::ProjectContentDir()/TEXT("CharacterCards/CrowdAPNG/C000.png"));
 if(FPaths::FileExists(Apng)){
  FOkSession Session;FString Error;
  if(!TestTrue(TEXT("Open portable session"),Session.Open(FString(),Apng,0,Error))){AddError(Error);return false;}
  if(!TestTrue(TEXT("Portable session plays video clip"),Session.SetAnimClip(Key)))return false;
  Sample(Session,TEXT("portable"));
  // Control: a known-good authored clip through the same readback.
  TArray<uint8> Walk;
  if(FFileHelper::LoadFileToArray(Walk,*(FPaths::ProjectContentDir()/TEXT("IndependentResources/RuntimeAnimations/walk_01.okanim")))&&Session.InstallSharedClip(Walk))
   Sample(Session,TEXT("portable-walk-control"));
 }
 const FString DumpPath=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_VIDEO_TEST_DUMP"));
 if(!DumpPath.IsEmpty()){FString Text;auto W=TJsonWriterFactory<>::Create(&Text);FJsonSerializer::Serialize(Dump,W);FFileHelper::SaveStringToFile(Text,*DumpPath,FFileHelper::EEncodingOptions::ForceUTF8);}
 return true;
}
#endif
