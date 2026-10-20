#include "KKRuntimeNativeSession.h"
#include "KKRuntimePoseNative.h"
#include "OKPhysXSecondary.h"
#include "Modules/ModuleManager.h"
#include "Async/Async.h"
#include "Misc/SecureHash.h"
#include "Misc/ScopeExit.h"
#include "HAL/FileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
static FString RuntimeApngContentIdentity(const FString& Path){
 TUniquePtr<FArchive> Reader(IFileManager::Get().CreateFileReader(*Path,FILEREAD_Silent));if(!Reader||Reader->TotalSize()<=0||Reader->TotalSize()>512ll*1024*1024)return {};
 FSHA1 Hash;TArray<uint8> Block;Block.SetNumUninitialized(1024*1024);
 while(!Reader->AtEnd()){const int32 Bytes=int32(FMath::Min<int64>(Block.Num(),Reader->TotalSize()-Reader->Tell()));Reader->Serialize(Block.GetData(),Bytes);if(Reader->IsError())return {};Hash.Update(Block.GetData(),Bytes);}
 Hash.Final();FSHAHash Digest;Hash.GetHash(Digest.Hash);return Digest.ToString();
}
bool OKDescribeRuntimeRig(FKKRuntimeSnapshotResult&,const TArray<uint8>&);
bool OKDescribeRuntimeRigFromSession(FKKRuntimeSnapshotResult&,const TArray<uint8>&,OkSession*);

const OkRustBindings& OKRuntimeNativeBindings(){
 static const OkRustBindings Bindings=[](){OkRustBindings B{};OkUnrealBindings U{};U.log=[](int32 Level,const char* Message){if(Level>=2)UE_LOG(LogTemp,Error,TEXT("Runtime native: %s"),UTF8_TO_TCHAR(Message));};const int32 Status=ok_register_bindings(U,&B);if(Status!=0)UE_LOG(LogTemp,Fatal,TEXT("Runtime native bindings rejected"));return B;}();return Bindings;
}
bool OKOpenRuntimePoseSnapshot(const FString& Apng,int32 Outfit,TArray<uint8>& Snapshot,FString& Error){
 if(Apng.IsEmpty()||Outfit<0){Error=TEXT("Invalid runtime portable card request");return false;}
 const auto& B=OKRuntimeNativeBindings();OkSession* Session=nullptr;char Message[2048]={};
 if(B.session_open("",TCHAR_TO_UTF8(*Apng),Outfit,&Session,Message,sizeof(Message))!=0||!Session){Error=UTF8_TO_TCHAR(Message);return false;}
 const int32 Count=ok_session_animation_snapshot(Session,nullptr,0);bool Passed=Count>0&&Count<=512*1024*1024;
 TArray<uint8> Bytes;if(Passed){Bytes.SetNumUninitialized(Count);Passed=ok_session_animation_snapshot(Session,Bytes.GetData(),Count)==Count;}
 B.session_free(Session);if(!Passed){Error=TEXT("Runtime portable rig snapshot rejected");return false;}Snapshot=MoveTemp(Bytes);Error.Reset();return true;
}
// OK cards (okLb) take their shared resources from the OK base library, which Rust finds through OK_BASE_LIBRARY.
// Set it before any card opens: the crowd opens cards here (session_open with no game root), not via FOkSession.
static void OKDefaultBaseLibrary(){
 if(!FPlatformMisc::GetEnvironmentVariable(TEXT("OK_BASE_LIBRARY")).IsEmpty())return;
 const FString Library=FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir()/TEXT("IndependentResources/base_v1"));
 if(FPaths::FileExists(Library/TEXT("oklib.json")))FPlatformMisc::SetEnvironmentVar(TEXT("OK_BASE_LIBRARY"),*Library);
}
class FKKRuntimePoseModule:public IModuleInterface {void StartupModule()override{FModuleManager::LoadModuleChecked<IModuleInterface>(TEXT("ImageWrapper"));OKDefaultBaseLibrary();OKRuntimeNativeBindings();OKPhysXSecondary::Register(&ok_set_physx_dynamic_solver);OKPhysXSecondary::RegisterTuning(&ok_set_secondary_tuning_source);}};
TFuture<FKKRuntimeSnapshotResult> OKOpenRuntimePoseSnapshotAsync(FString Apng,int32 Outfit,TArray<uint8> InitializationCurves){
 return Async(EAsyncExecution::ThreadPool,[Apng=MoveTemp(Apng),Outfit,InitializationCurves=MoveTemp(InitializationCurves)](){
  const double Start=FPlatformTime::Seconds();FKKRuntimeSnapshotResult R;R.WorkerThread=!IsInGameThread();
  if(OKOpenRuntimePoseSnapshot(Apng,Outfit,R.Snapshot,R.Error)&&OKDescribeRuntimeRig(R,InitializationCurves))R.RigIdentity=FSHA1::HashBuffer(R.Snapshot.GetData(),R.Snapshot.Num()).ToString();
  R.PrepareMs=(FPlatformTime::Seconds()-Start)*1000;return R;
 });
}
IMPLEMENT_MODULE(FKKRuntimePoseModule,KKRuntimePose);

float OKRuntimeSharedClipDuration(const TArray<uint8>& Curves){return Curves.IsEmpty()||Curves.Num()>128*1024*1024?0.f:ok_shared_clip_duration(Curves.GetData(),Curves.Num());}


bool OKCaptureRuntimeMaterials(OkSession*,TConstArrayView<FKKRuntimePalettePart>,FKKRuntimeMaterialData&,FString&);
bool OKCaptureRuntimeMeshes(OkSession*,TConstArrayView<FKKRuntimePoseBone>,TConstArrayView<FKKRuntimePalettePart>,TArray<FKKRuntimeMeshPart>&,uint64,FString&,bool);
bool OKApplyRuntimeLowPoly(OkSession*,TConstArrayView<FKKRuntimePoseBone>,TConstArrayView<FKKRuntimePalettePart>,TArray<FKKRuntimeMeshPart>&,const FString&,FString&);
TFuture<FKKRuntimeSnapshotResult> OKOpenRuntimeCharacterSnapshotAsync(FString Apng,int32 Outfit,TArray<uint8> Curves,uint64 Budget,bool CaptureFacePatterns){
 return Async(EAsyncExecution::ThreadPool,[Apng=MoveTemp(Apng),Outfit,Curves=MoveTemp(Curves),Budget,CaptureFacePatterns](){
  const double Start=FPlatformTime::Seconds();FKKRuntimeSnapshotResult R;R.WorkerThread=!IsInGameThread();
  auto Fail=[&](const FString& Why){R.Error=Why;R.Meshes.Reset();R.PrepareMs=(FPlatformTime::Seconds()-Start)*1000;return MoveTemp(R);};
  if(Apng.IsEmpty()||Outfit<0||Curves.IsEmpty()||!Budget)return Fail(TEXT("Invalid runtime character capture request"));
  const FString ContentIdentity=RuntimeApngContentIdentity(Apng);if(ContentIdentity.IsEmpty())return Fail(TEXT("Runtime APNG content identity unavailable"));
  const auto& B=OKRuntimeNativeBindings();OkSession* S=nullptr;char Message[2048]={};
  if(B.session_open("",TCHAR_TO_UTF8(*Apng),Outfit,&S,Message,sizeof(Message))!=0||!S)return Fail(UTF8_TO_TCHAR(Message));
  ON_SCOPE_EXIT{B.session_free(S);};R.NativeOpenMs=(FPlatformTime::Seconds()-Start)*1000;double StageStart=FPlatformTime::Seconds();
  const int32 Count=ok_session_animation_snapshot(S,nullptr,0);if(Count<=0||Count>512*1024*1024)return Fail(TEXT("Runtime character snapshot size invalid"));
  R.Snapshot.SetNumUninitialized(Count);if(ok_session_animation_snapshot(S,R.Snapshot.GetData(),Count)!=Count)return Fail(TEXT("Runtime character snapshot copy failed"));
  if(ContentIdentity!=RuntimeApngContentIdentity(Apng))return Fail(TEXT("APNG changed during runtime capture; retry selection"));R.PersistentSourceIdentity=ContentIdentity+FString::Printf(TEXT(":outfit-%d"),Outfit);
  if(!OKDescribeRuntimeRigFromSession(R,Curves,S))return Fail(R.Error);R.RigCaptureMs=(FPlatformTime::Seconds()-StageStart)*1000;StageStart=FPlatformTime::Seconds();
  R.RigIdentity=FSHA1::HashBuffer(R.Snapshot.GetData(),R.Snapshot.Num()).ToString();
  // Shared animation updates palettes without updating CPU vertex streams.
  // Capture both from the same solved frame, once during background preparation.
  if(B.skin(S)!=0)return Fail(TEXT("Runtime initial mesh skin refresh failed"));
  if(!OKCaptureRuntimeMeshes(S,R.Bones,R.Parts,R.Meshes,Budget,R.Error,CaptureFacePatterns))return Fail(R.Error);
  // Opt-in: replace renderers with the APNG's remeshed low model (<card>.lowpoly.okmap).
  if(FParse::Param(FCommandLine::Get(),TEXT("RuntimeLowPoly"))){const FString Map=FPaths::ChangeExtension(Apng,TEXT("lowpoly.okmap"));FString Summary;
   const bool Used=OKApplyRuntimeLowPoly(S,R.Bones,R.Parts,R.Meshes,Map,Summary);UE_LOG(LogTemp,Display,TEXT("Runtime low-poly %s: %s %s"),Used?TEXT("applied"):TEXT("skipped"),*Apng,*Summary);}
  R.MeshCaptureMs=(FPlatformTime::Seconds()-StageStart)*1000;StageStart=FPlatformTime::Seconds();
  if(!OKCaptureRuntimeMaterials(S,R.Parts,R.Materials,R.Error))return Fail(R.Error);R.MaterialCaptureMs=(FPlatformTime::Seconds()-StageStart)*1000;
  R.PrepareMs=(FPlatformTime::Seconds()-Start)*1000;return R;
 });
}
