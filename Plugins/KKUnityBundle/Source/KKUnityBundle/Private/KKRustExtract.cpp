#include "KKRustExtract.h"
#include "Misc/ConfigCacheIni.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#endif
#include "KKSharedAnimation.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Serialization/JsonSerializer.h"
#include "KKSkeletalBuilder.h"
#include "Animation/Skeleton.h"
#include "ok_extract.h"
#include "KKRuntimeMaterial.h"
#include "KKRuntimePoseBankTypes.h"
#include "OKExtractBuildStamp.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "HAL/IConsoleManager.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Tasks/Task.h"
#include "UObject/StrongObjectPtr.h"
#include <atomic>

DEFINE_LOG_CATEGORY_STATIC(LogOkExtract, Log, All);
namespace { UKKSharedAnimation* FindSharedCurveAsset(const FString& Key); }

namespace {
#if CPUPROFILERTRACE_ENABLED
uint32 NativeTraceSpecs[47];
void NativeCpuTrace(uint32_t Event,int32_t Begin){
    if(Event>=UE_ARRAY_COUNT(NativeTraceSpecs))return;
    if(Begin)FCpuProfilerTrace::OutputBeginEvent(NativeTraceSpecs[Event]);
    else FCpuProfilerTrace::OutputEndEvent();
}
#endif
void RegisterNativeCpuTrace(){
    static const bool Registered=[](){
#if CPUPROFILERTRACE_ENABLED
        if(FParse::Param(FCommandLine::Get(),TEXT("OKNativeTrace"))){
            const ANSICHAR* Names[]={"KK_NativeValidate","KK_NativeSampleWait","KK_NativeContacts","KK_NativeDynamicsWait",
                "KK_NativeBodyPhysics","KK_NativeClothesPhysics","KK_NativeExport","KK_NativeAccessoryDispatch",
                "KK_NativeAccessorySync","KK_NativeAccessorySetup","KK_NativeAccessoryStep","KK_NativeSkinPrepare",
                "KK_NativeSkinLBS","KK_NativeSkinCorrection","KK_NativeRoleSample",
                "KK_NativeAnimatorReset","KK_NativeAnimatorCurves","KK_NativeAnimatorModifiers",
                "KK_NativeDynamicTopology","KK_NativeDynamicColliders","KK_NativeDynamicForces",
                "KK_NativeDynamicParticles","KK_NativeDynamicApply","KK_NativeDynamicWorlds","KK_NativeFrameWait",
                "KK_NativeShapeRestore","KK_NativeShapeBody","KK_NativeShapeHead","KK_NativeShapeFullHead","KK_NativeShapeReferences",
                "KK_NativeContactCorrectBefore","KK_NativeContactFreeze","KK_NativeContactSolve","KK_NativeContactCorrectAfter",
                "KK_NativePalettePrepare","KK_NativePaletteRead","KK_NativePalettePublish",
                "KK_NativeMotionPlan","KK_NativeMotionLeaves","KK_NativeMotionBlend","KK_NativeLeafSnapshot","KK_NativeLeafChannels","KK_NativeChannelLookup","KK_NativeChannelSample",
                "KK_NativeAccFrames","KK_NativeAccRadii","KK_NativeAccLeaders"};
            static_assert(UE_ARRAY_COUNT(Names)==UE_ARRAY_COUNT(NativeTraceSpecs),"Native CPU trace event storage must match the registered events");
            for(int32 I=0;I<UE_ARRAY_COUNT(Names);++I)NativeTraceSpecs[I]=FCpuProfilerTrace::OutputEventType(Names[I],__FILE__,__LINE__);
            const int32 Result=ok_set_cpu_trace_callback(NativeCpuTrace);
            checkf(Result==0,TEXT("Native CPU trace callback registration failed"));
        }
#endif
        return true;
    }();
    (void)Registered;
}
}

// Referenced so a new ok_extract.lib stamp recompiles this file and relinks the DLL.
static const char* GOkExtractLibStamp = OK_EXTRACT_LIB_STAMP;
static TArray<FString> ReadAnimationNames(TFunctionRef<int32(uint8*,int32)> Read){
 const int32 Size=Read(nullptr,0);TArray<FString> Result;if(Size<=0)return Result;TArray<uint8> Bytes;Bytes.SetNumZeroed(Size+1);
 if(Read(Bytes.GetData(),Size)!=Size)return Result;
 TArray<TSharedPtr<FJsonValue>> Values;
 if(FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(UTF8_TO_TCHAR(reinterpret_cast<const char*>(Bytes.GetData()))),Values))for(const auto& V:Values)Result.Add(V->AsString());
 return Result;
}
TArray<FString> FOkSession::AnimationSourceNames(const FString& Root,const FString& Source){
 const FTCHARToUTF8 RootUtf(*Root),SourceUtf(*Source);
 return ReadAnimationNames([&](uint8* Out,int32 Size){return ok_animation_source_labels(RootUtf.Get(),SourceUtf.Get(),Out,Size);});
}
bool FOkSession::CopyPortableClothingStates(const FString& Source,const FString& Output,FString& Error){
 ANSICHAR Message[4096]={};const bool Good=ok_copy_portable_clothing_states(TCHAR_TO_UTF8(*Source),TCHAR_TO_UTF8(*Output),Message,sizeof(Message))==0;
 Error=Good?FString():FString(UTF8_TO_TCHAR(Message));return Good;
}
FOkLowPoly::~FOkLowPoly(){if(Handle)ok_lowpoly_free(Handle);}
FString FOkLowPoly::GlbPath(const FString& CardPath){return FPaths::ChangeExtension(CardPath,TEXT("lowpoly.glb"));}
TSharedPtr<FOkLowPoly,ESPMode::ThreadSafe> FOkLowPoly::Capture(FOkSession& Session,FString& Error){
 if(!Session.Handle){Error=TEXT("No open session");return nullptr;}
 ANSICHAR Message[4096]={};void* Handle=ok_lowpoly_capture(Session.Handle,Message,sizeof(Message));
 if(!Handle){Error=UTF8_TO_TCHAR(Message);return nullptr;}
 TSharedPtr<FOkLowPoly,ESPMode::ThreadSafe> Result(new FOkLowPoly());Result->Handle=Handle;return Result;
}
bool FOkLowPoly::Export(const FString& CardPath,int32 Budget,FString& Summary,FString& Error) const{
 ANSICHAR Text[1024]={},Message[4096]={};
 const bool Good=Handle&&ok_lowpoly_export(Handle,TCHAR_TO_UTF8(*CardPath),Budget,Text,sizeof(Text),Message,sizeof(Message))==0;
 Summary=UTF8_TO_TCHAR(Text);Error=Good?FString():(Handle?FString(UTF8_TO_TCHAR(Message)):FString(TEXT("Low-poly source was not captured")));return Good;
}
bool FOkSession::PrepareStudioAnimation(const FString& Root,const FString& Key,const FString& Source,const FString& Owner){
 return ok_prepare_studio_animation(TCHAR_TO_UTF8(*Root),TCHAR_TO_UTF8(*Key),TCHAR_TO_UTF8(*Source),TCHAR_TO_UTF8(*Owner))>0;
}
bool FOkSession::PrepareAnimationSource(const FString& Root,const FString& Source){
 return ok_prepare_animation_source(TCHAR_TO_UTF8(*Root),TCHAR_TO_UTF8(*Source))>0;
}
bool FOkSession::PrepareAnimationClip(const FString& Root,const FString& Key){
 if(IsVideoClipKey(Key)){TArray<uint8> Bytes;return LoadVideoClip(Key,Bytes);}
 return ok_prepare_animation_clip(TCHAR_TO_UTF8(*Root),TCHAR_TO_UTF8(*Key))>0;
}
void FOkSession::BeginAnimationPreparation(){ok_begin_animation_preparation();}
void FOkSession::EndAnimationPreparation(){ok_end_animation_preparation();}
bool FOkSession::PrepareAnimationPair(const FString& Root,const FString& State,const FString& FemaleMotion,const FString& MaleMotion){
 return ok_prepare_animation_pair(TCHAR_TO_UTF8(*Root),TCHAR_TO_UTF8(*State),TCHAR_TO_UTF8(*FemaleMotion),TCHAR_TO_UTF8(*MaleMotion))>0;
}
static void OKPrepareCardEnvironment();
bool FOkSession::Prewarm(const FString& GameRoot){
 OKPrepareCardEnvironment();
 if(!InitBindings())return false;
 const double Start=FPlatformTime::Seconds();
 const bool bOk=ok_prewarm(TCHAR_TO_UTF8(*GameRoot))==0;
 UE_LOG(LogTemp,Display,TEXT("OK native prewarm root=%s ok=%d ms=%.1f"),GameRoot.IsEmpty()?TEXT("<base library>"):*GameRoot,bOk?1:0,(FPlatformTime::Seconds()-Start)*1000.);
 return bOk;
}
void FOkSession::SetThreadBackground(bool bBackground){if(InitBindings())ok_set_thread_background(bBackground?1:0);}
void FOkSession::ReleaseSourceCaches(){if(InitBindings())ok_release_source_caches();}
int32 FOkSession::CacheReady(const FString& CardPath,int32 Outfit,const uint8* Clothes){
 OKPrepareCardEnvironment();
 if(!InitBindings())return -1;
 return ok_session_cache_ready(TCHAR_TO_UTF8(*CardPath),Outfit,Clothes);
}
FString FOkSession::CommonPairRoot(){
 // The OK base library's animation root (`<lib>/paired`, converted by ok-paired-library): decoded OK data. No
 // session reads Unity bundles for shared animations; without a converted library there are none.
 const FString Configured=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_BASE_LIBRARY"));
 const FString Library=(Configured.IsEmpty()?FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir()/TEXT("IndependentResources/base_v1")):Configured)/TEXT("paired");
 if(FPaths::FileExists(Library/TEXT("okpaired.json")))return Library;
 static bool bReported=false;
 if(!bReported){bReported=true;UE_LOG(LogTemp,Warning,TEXT("OK shared animations: %s is not converted (ok-paired-library convert)"),*Library);}
 return FString();
}
bool FOkSession::HasAnimationClip(const FString& Key)const{
 if(!Handle)return false;
 if(IsVideoClipKey(Key)){TArray<uint8> Bytes;return LoadVideoClip(Key,Bytes);}
 if(bPortable&&Key.StartsWith(TEXT("states:")))return !CommonPairRoot().IsEmpty()&&PrepareAnimationClip(CommonPairRoot(),Key);
 if(bPortable){const auto* Asset=FindSharedCurveAsset(Key);return Asset&&!Asset->AuthoredCurves.IsEmpty();}
 return ok_animation_has_clip(Handle,TCHAR_TO_UTF8(*Key))==1;
}

FString FOkSession::CommonAnimationPairs()
{
	const FString Root=CommonPairRoot();if(Root.IsEmpty())return FString();
	if(!InitBindings())return FString();
	const int32 Size=ok_common_animation_pairs(TCHAR_TO_UTF8(*Root),nullptr,0);
	if(Size<=3){UE_LOG(LogTemp,Warning,TEXT("OK shared pairs: none from %s (%d)"),*Root,Size);}
	if(Size<=0||Size>8*1024*1024)return FString();
	TArray<ANSICHAR> Bytes;Bytes.SetNumZeroed(Size);if(ok_common_animation_pairs(TCHAR_TO_UTF8(*Root),Bytes.GetData(),Size)!=0)return FString();
	return UTF8_TO_TCHAR(Bytes.GetData());
}

FString FOkSession::NativeAnimationPairs() const
{
	if (bPortable) return CommonAnimationPairs();

	if(!Handle) return FString();
	const int32 Size=ok_animation_pairs(Handle,nullptr,0);
	if(Size<=0||Size>8*1024*1024) return FString();
	TArray<ANSICHAR> Bytes;Bytes.SetNumZeroed(Size);
	if(ok_animation_pairs(Handle,Bytes.GetData(),Size)!=0) return FString();
	return UTF8_TO_TCHAR(Bytes.GetData());
}
FString FOkSession::CommonInteractionPoses(){
 const FString Root=CommonPairRoot();if(Root.IsEmpty())return FString();
 const FTCHARToUTF8 Path(*Root);const int32 Size=ok_common_interaction_poses(Path.Get(),nullptr,0);
 if(Size<=0||Size>8*1024*1024)return FString();TArray<ANSICHAR> Bytes;Bytes.SetNumZeroed(Size);
 if(ok_common_interaction_poses(Path.Get(),Bytes.GetData(),Size)!=0)return FString();return UTF8_TO_TCHAR(Bytes.GetData());
}

FString FOkSession::PairClipForRoot(const FString& Root,const FString& Key,int32 Layer){
 if(Root.IsEmpty())return FString();const int32 Size=ok_common_animation_pair_clip(TCHAR_TO_UTF8(*Root),TCHAR_TO_UTF8(*Key),Layer,nullptr,0);
 if(Size<=0||Size>1024*1024)return FString();TArray<ANSICHAR> Bytes;Bytes.SetNumZeroed(Size);
 if(ok_common_animation_pair_clip(TCHAR_TO_UTF8(*Root),TCHAR_TO_UTF8(*Key),Layer,Bytes.GetData(),Size)!=0)return FString();return UTF8_TO_TCHAR(Bytes.GetData());
}
FString FOkSession::NativePairClip(const FString& Key,int32 Layer)const{
    if(bPortable){const FString Root=CommonPairRoot();if(Root.IsEmpty())return FString();const int32 Size=ok_common_animation_pair_clip(TCHAR_TO_UTF8(*Root),TCHAR_TO_UTF8(*Key),Layer,nullptr,0);if(Size<=0||Size>1024*1024)return FString();TArray<ANSICHAR> Bytes;Bytes.SetNumZeroed(Size);if(ok_common_animation_pair_clip(TCHAR_TO_UTF8(*Root),TCHAR_TO_UTF8(*Key),Layer,Bytes.GetData(),Size)!=0)return FString();return UTF8_TO_TCHAR(Bytes.GetData());}
    if(!Handle)return FString();int32 Size=ok_animation_pair_clip(Handle,TCHAR_TO_UTF8(*Key),Layer,nullptr,0);if(Size<=0||Size>1024*1024)return FString();TArray<ANSICHAR> Bytes;Bytes.SetNumZeroed(Size);if(ok_animation_pair_clip(Handle,TCHAR_TO_UTF8(*Key),Layer,Bytes.GetData(),Size)!=0)return FString();return UTF8_TO_TCHAR(Bytes.GetData());
}

bool FOkSession::ApplyPairedFrame(FOkSession& Female,FOkSession& Male,float Time,float Delta,
	const FString& State,const FString& FemaleMotion,const FString& MaleMotion,FString& Error,FOkPrivateSkinCallback Callback,void* Context)
{
    RegisterNativeCpuTrace();
	if (Female.bPortable != Male.bPortable) { Error = TEXT("配對角色需要相同資源模式"); return false; }
	ANSICHAR Message[4096]={};
	const bool Good=(Callback?ok_animation_pair_frame_observed(Female.Handle,Male.Handle,Time,Delta,
        TCHAR_TO_UTF8(*State),TCHAR_TO_UTF8(*FemaleMotion),TCHAR_TO_UTF8(*MaleMotion),Callback,Context,Message,sizeof(Message)):
        ok_animation_pair_frame(Female.Handle,Male.Handle,Time,Delta,
		TCHAR_TO_UTF8(*State),TCHAR_TO_UTF8(*FemaleMotion),TCHAR_TO_UTF8(*MaleMotion),Message,sizeof(Message)))==0;
	Error=Good?FString():FString(UTF8_TO_TCHAR(Message));return Good;
}

bool FOkSession::ApplyPairedFrameGpu(FOkSession& Female,FOkSession& Male,float Time,float Delta,const FString& State,const FString& FemaleMotion,const FString& MaleMotion,
 TConstArrayView<int32> FemaleParts,TConstArrayView<uint64> FemaleRevisions,TConstArrayView<int32> MaleParts,TConstArrayView<uint64> MaleRevisions,FString& Error){
 RegisterNativeCpuTrace();
 if(Female.bPortable!=Male.bPortable||FemaleParts.Num()!=FemaleRevisions.Num()||MaleParts.Num()!=MaleRevisions.Num()){Error=TEXT("Invalid paired GPU ownership layout");return false;}
 ANSICHAR Message[4096]={};
 const int32 Code=ok_animation_pair_frame_gpu(Female.Handle,Male.Handle,Time,Delta,TCHAR_TO_UTF8(*State),TCHAR_TO_UTF8(*FemaleMotion),TCHAR_TO_UTF8(*MaleMotion),
  FemaleParts.GetData(),FemaleRevisions.GetData(),FemaleParts.Num(),MaleParts.GetData(),MaleRevisions.GetData(),MaleParts.Num(),Message,sizeof(Message));
 if(Code==2)return ApplyPairedFrame(Female,Male,Time,Delta,State,FemaleMotion,MaleMotion,Error);
 Error=Code==0?FString():FString(UTF8_TO_TCHAR(Message));return Code==0;
}

static OkRustBindings GRust = {};
static bool GRustReady = false;

static void OkLogThunk(int32_t Level, const char* MsgUtf8)
{
	const FString Msg = UTF8_TO_TCHAR(MsgUtf8 ? MsgUtf8 : "");
	if (Level >= 2)
	{
		UE_LOG(LogOkExtract, Error, TEXT("%s"), *Msg);
	}
	else if (Level == 1)
	{
		UE_LOG(LogOkExtract, Warning, TEXT("%s"), *Msg);
	}
	else
	{
		UE_LOG(LogOkExtract, Log, TEXT("%s"), *Msg);
	}
}

bool FOkSession::SetPhysXSecondary(bool bOn) { bPhysXSecondary=bOn; return !Handle||ok_session_set_physx_secondary(Handle,bOn?1:0)==0; }
void FOkSession::ApplyPhysXSecondary() const { if(Handle){ok_session_set_physx_secondary(Handle,bPhysXSecondary?1:0);ok_session_set_external_animation(Handle,ExternalAnimation);} }
bool FOkSession::SetExternalAnimation(bool bExternal) { ExternalAnimation=bExternal?1:0; return !Handle||ok_session_set_external_animation(Handle,ExternalAnimation)==0; }
bool FOkSession::HeadLookError(float& Residual,float& Excess) const { float Out[2]={0.f,0.f}; if(!Handle||ok_head_look_error(Handle,Out)!=0)return false; Residual=Out[0];Excess=Out[1];return true; }
bool FOkSession::SetHeadLook(const FVector& Target,bool Enabled) { return Handle&&ok_set_head_look(Handle,Target.X,Target.Y,Target.Z,Enabled?1:0)==0; }
bool FOkSession::SetFk(const TArray<FString>& Bones,const TArray<FVector>& UnityEuler)
{
	if(!Handle||Bones.Num()!=UnityEuler.Num())return false;
	TArray<ANSICHAR> Names;TArray<float> Euler;
	for(int32 I=0;I<Bones.Num();++I)
	{
		const FTCHARToUTF8 Name(*Bones[I]);
		Names.Append(Name.Get(),Name.Length());Names.Add(0);
		Euler.Append({float(UnityEuler[I].X),float(UnityEuler[I].Y),float(UnityEuler[I].Z)});
	}
	if(Names.IsEmpty())Names.Add(0);
	return ok_session_set_fk(Handle,Names.GetData(),Euler.GetData(),Bones.Num())==0;
}
bool FOkSession::PrepareStudioIk(FString& OutError)
{
	if(!Handle){OutError=TEXT("no session");return false;}
	const FString Root=CommonPairRoot();
	if(Root.IsEmpty()){OutError=TEXT("shared resources (IndependentResources/PairedAnimations) missing");return false;}
	char Message[2048]={};
	if(ok_session_prepare_studio_ik(Handle,TCHAR_TO_UTF8(*Root),Message,UE_ARRAY_COUNT(Message))!=0){OutError=UTF8_TO_TCHAR(Message);return false;}
	return true;
}
bool FOkSession::SetStudioIk(const TArray<int32>& Effectors,const TArray<FVector>& Positions)
{
	if(!Handle||Effectors.Num()!=Positions.Num())return false;
	TArray<float> P;
	for(const FVector& V:Positions)P.Append({float(V.X),float(V.Y),float(V.Z)});
	return ok_session_set_studio_ik(Handle,Effectors.GetData(),P.GetData(),Effectors.Num())==0;
}
bool FOkSession::SetStudioIkPose(const TArray<int32>& Effectors,const TArray<FVector>& Positions,const TArray<FQuat>& Rotations,const TArray<uint8>& UseRotations)
{
    if(!Handle || Effectors.Num()!=Positions.Num() || Effectors.Num()!=Rotations.Num() || Effectors.Num()!=UseRotations.Num()) return false;
    TArray<float> P;
    TArray<float> Q;
    for(const FVector& V:Positions) P.Append({float(V.X),float(V.Y),float(V.Z)});
    for(const FQuat& V:Rotations) Q.Append({float(V.X),float(V.Y),float(V.Z),float(V.W)});
    return ok_session_set_studio_ik_pose(Handle,Effectors.GetData(),P.GetData(),Q.GetData(),UseRotations.GetData(),Effectors.Num())==0;
}
bool FOkSession::SetStudioFixedNeck(const TArray<FQuat>& Rotations)
{
    TArray<float> Q;
    for(const FQuat& V:Rotations) Q.Append({float(V.X),float(V.Y),float(V.Z),float(V.W)});
    return Handle && ok_session_set_studio_fixed_neck(Handle,Q.GetData(),Rotations.Num())==0;
}
bool FOkSession::SetStudioFixedEyes(const TArray<float>& Values, int32 Pattern, FVector& Rates)
{
    float Output[3] = {};
    if (!Handle || ok_session_set_studio_fixed_eyes(Handle, Values.GetData(), Values.Num(), Pattern, Output) != 0) return false;
    Rates = FVector(Output[0], Output[1], Output[2]);
    return true;
}
bool FOkSession::IkEffectorPosition(int32 Effector,FVector& Out) const
{
	float P[3]={};
	if(!Handle||ok_session_ik_effector_position(Handle,Effector,P)!=0)return false;
	Out=FVector(P[0],P[1],P[2]);return true;
}
bool FOkSession::BoneEuler(const FString& Bone,FVector& Out) const
{
	float E[3]={};
	if(!Handle||ok_session_bone_euler(Handle,TCHAR_TO_UTF8(*Bone),E)!=0)return false;
	Out=FVector(E[0],E[1],E[2]);return true;
}
bool FOkSession::StaticInteractionFrame(float Delta) { return Handle&&ok_static_interaction_frame(Handle,Delta)==0; }
bool FOkSession::PrepareBodyReaction(const TArray<uint8>& Definition,FString& Error){
 if(!Handle){Error=TEXT("角色尚未載入");return false;}
 FTCHARToUTF8 Root(*CommonPairRoot());char Message[2048]={};
 if(ok_body_reaction_prepare(Handle,Root.Get(),Definition.GetData(),Definition.Num(),Message,UE_ARRAY_COUNT(Message))!=0){Error=UTF8_TO_TCHAR(Message);return false;}return true;
}
bool FOkSession::BodyReactionHit(int32 Id,TConstArrayView<FVector3f> Forces,float Weight,uint8 ReleaseMask){
 static_assert(sizeof(FVector3f)==sizeof(float)*3);
 // Forces are in the native character's scene frame. Actor world placement
 // applies to both geometry and force; animated cf_j_root is not that frame.
 return Handle&&ok_body_reaction_hit_with_release(Handle,Id,reinterpret_cast<const float*>(Forces.GetData()),Forces.Num(),Weight,ReleaseMask)==0;
}
void FOkSession::ClearBodyReaction(){if(Handle)ok_body_reaction_clear(Handle);}
bool FOkSession::IsBodyReactionPlaying()const{return Handle&&ok_body_reaction_is_playing(Handle)!=0;}

bool FOkSession::InitBindings()
{
	if (GRustReady)
	{
		return true;
	}
	OkUnrealBindings Unreal;
	FMemory::Memzero(Unreal);
	Unreal.log = &OkLogThunk;
	if (ok_register_bindings(Unreal, &GRust) != 0)
	{
		return false;
	}
	GRustReady = true;
	UE_LOG(LogOkExtract, Display, TEXT("ok_extract.lib stamp %s"), UTF8_TO_TCHAR(GOkExtractLibStamp));
	return true;
}

FOkSession::~FOkSession()
{
	Close();
}

int32 FOkSession::HandShapeCount() const
{
	return Handle && GRust.hand_shape_count ? GRust.hand_shape_count(Handle) : 0;
}

bool FOkSession::SetHandShape(int32 Side, bool Enabled, int32 A, int32 B, float Blend)
{
	return Handle && GRust.set_hand_shape && GRust.set_hand_shape(Handle, Side, Enabled ? 1 : 0, A, B, Blend) == 0;
}

FOkSession::FOkSession(FOkSession&& Other) noexcept
	: Handle(Other.Handle), SkinPaletteCapacityHints(MoveTemp(Other.SkinPaletteCapacityHints)), PrivateSkinStageCapacityHints(MoveTemp(Other.PrivateSkinStageCapacityHints)), bPortable(Other.bPortable), bSharedAnimation(Other.bSharedAnimation), bPhysXSecondary(Other.bPhysXSecondary), ExternalAnimation(Other.ExternalAnimation)
{
	RuntimeMaterials=MoveTemp(Other.RuntimeMaterials);
	Other.Handle = nullptr;
}

FOkSession& FOkSession::operator=(FOkSession&& Other) noexcept
{
	if (this != &Other)
	{
		Close();
		Handle = Other.Handle;
		RuntimeMaterials=MoveTemp(Other.RuntimeMaterials);
		SkinPaletteCapacityHints = MoveTemp(Other.SkinPaletteCapacityHints);
		PrivateSkinStageCapacityHints = MoveTemp(Other.PrivateSkinStageCapacityHints);
		bPortable = Other.bPortable;
        bSharedAnimation = Other.bSharedAnimation;
		Other.Handle = nullptr;
		// The PhysX switch belongs to the character holding this session (AKKPreviewActor::AddPairPhysics), not to the
		// session a loader opened elsewhere and moves in: keep ours and give it to the new native session.
		ApplyPhysXSecondary();
	}
	return *this;
}

// OK base library and KKS installation for card work (opening, converting to OK cards).
static void OKPrepareCardEnvironment()
{
	// OK base library: cards carrying okLb open from it with an empty game root
	// (Rust resolves the library through OK_BASE_LIBRARY).
	if (FPlatformMisc::GetEnvironmentVariable(TEXT("OK_BASE_LIBRARY")).IsEmpty())
	{
		const FString Library = FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir() / TEXT("IndependentResources/base_v1"));
		if (FPaths::FileExists(Library / TEXT("oklib.json")))
		{
			FPlatformMisc::SetEnvironmentVar(TEXT("OK_BASE_LIBRARY"), *Library);
		}
	}
	// The KKS installation (KKGameRoot setting): original cards opened from the base library take their own
	// zipmods from its mods/ even when the caller shows independent cards (no game root).
	if (FPlatformMisc::GetEnvironmentVariable(TEXT("OK_GAME_ROOT")).IsEmpty())
	{
		FString GameRoot;
		if (GConfig && GConfig->GetString(TEXT("/Script/KKSideloader.KKGameRoot"), TEXT("Root"), GameRoot, GGameIni)
			&& !GameRoot.IsEmpty() && FPaths::DirectoryExists(GameRoot / TEXT("mods")))
		{
			FPlatformMisc::SetEnvironmentVar(TEXT("OK_GAME_ROOT"), *GameRoot);
		}
	}
}

bool FOkSession::Open(const FString& GameRoot, const FString& CardPath, int32 Outfit, FString& OutError, const uint8* Clothes, const FString& StatusJson)
{
	Close();
	// Live viewer: T-pose bind, GPU skin export. Dump extract does not call Open.
	// Accessory sockets read rest_positions in parent-bone local space.
	OKPrepareLiveGpuEnv();
	if (GameRoot.IsEmpty() && FPlatformMisc::GetEnvironmentVariable(TEXT("OK_BUILTIN_RESOURCES")).IsEmpty())
	{
		const FString Resources = FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir() / TEXT("IndependentResources/v1"));
		FPlatformMisc::SetEnvironmentVar(TEXT("OK_BUILTIN_RESOURCES"), *Resources);
	}
	OKPrepareCardEnvironment();
	if (!InitBindings() || !GRust.session_open)
	{
		OutError = TEXT("ok_register_bindings failed");
		return false;
	}
	const FTCHARToUTF8 GameUtf(*GameRoot);
	const FTCHARToUTF8 CardUtf(*CardPath);
	char Err[2048];
	Err[0] = 0;
	OkSession* Out = nullptr;
	const FTCHARToUTF8 StatusUtf(*StatusJson);
	const int32 Rc = !StatusJson.IsEmpty() ? ok_session_open_status(GameUtf.Get(),CardPath.IsEmpty()?nullptr:CardUtf.Get(),Outfit,Clothes,StatusUtf.Get(),&Out,Err,2048)
		: Clothes ? ok_session_open_clothes(GameUtf.Get(),CardPath.IsEmpty()?nullptr:CardUtf.Get(),Outfit,Clothes,&Out,Err,2048) : GRust.session_open(
		GameUtf.Get(),
		CardPath.IsEmpty() ? nullptr : CardUtf.Get(),
		Outfit,
		&Out,
		Err,
		2048);
	if (Rc != 0 || !Out)
	{
		OutError = UTF8_TO_TCHAR(Err);
		if (OutError.IsEmpty())
		{
			OutError = TEXT("session_open failed");
		}
		return false;
	}
	Handle = Out;
	ApplyPhysXSecondary();
	// Self-contained sessions (portable packs, cards from the OK base library) take animations from the
	// shared resources, whatever game root the caller passed.
	bPortable = ok_session_has_game_root(Out) == 0;
	return true;
}

bool FOkSession::OpenOkModel(const FString& Library, const FString& ModelId, const FString& Prefab, bool bIncludeDisabled, FString& OutError)
{
	Close();
	OKPrepareLiveGpuEnv();
	if (!InitBindings())
	{
		OutError = TEXT("ok_register_bindings failed");
		return false;
	}
	const FTCHARToUTF8 LibUtf(*Library), IdUtf(*ModelId), PrefabUtf(*Prefab);
	char Err[2048];
	Err[0] = 0;
	OkSession* Out = nullptr;
	if (ok_okmodel_open_session(LibUtf.Get(), IdUtf.Get(), PrefabUtf.Get(), bIncludeDisabled ? 1 : 0, &Out, Err, 2048) != 0 || !Out)
	{
		OutError = Err[0] ? UTF8_TO_TCHAR(Err) : TEXT("ok_okmodel_open_session failed");
		return false;
	}
	Handle = Out;
	ApplyPhysXSecondary();
	// No game root: the preview behaves like a self-contained (portable) session.
	bPortable = true;
	return true;
}

bool FOkSession::OpenOkModelFk(const FString& Library, const FString& ModelId, const FString& Prefab, const FString& FkJson, FString& OutError)
{
	if (FkJson.IsEmpty() || FkJson == TEXT("[]")) return OpenOkModel(Library, ModelId, Prefab, false, OutError);
	Close();
	OKPrepareLiveGpuEnv();
	if (!InitBindings()) { OutError = TEXT("ok_register_bindings failed"); return false; }
	const FTCHARToUTF8 LibUtf(*Library), IdUtf(*ModelId), PrefabUtf(*Prefab), FkUtf(*FkJson);
	char Err[2048];
	Err[0] = 0;
	OkSession* Out = nullptr;
	if (ok_okmodel_open_session_fk(LibUtf.Get(), IdUtf.Get(), PrefabUtf.Get(), 0, FkUtf.Get(), &Out, Err, 2048) != 0 || !Out)
	{
		OutError = Err[0] ? UTF8_TO_TCHAR(Err) : TEXT("ok_okmodel_open_session_fk failed");
		return false;
	}
	Handle = Out;
	ApplyPhysXSecondary();
	bPortable = true;
	return true;
}

bool FOkSession::OkModelNodeWorld(const FString& Library, const FString& ModelId, const FString& Prefab, const FString& Node, FVector& OutPosition, FQuat& OutRotation, FVector& OutScale)
{
	if (!InitBindings()) return false;
	const FTCHARToUTF8 LibUtf(*Library), IdUtf(*ModelId), PrefabUtf(*Prefab), NodeUtf(*Node);
	float V[10] = {};
	if (ok_okmodel_node_world(LibUtf.Get(), IdUtf.Get(), PrefabUtf.Get(), NodeUtf.Get(), V) != 0) return false;
	OutPosition = FVector(V[0], V[1], V[2]);
	OutRotation = FQuat(V[3], V[4], V[5], V[6]);
	OutScale = FVector(V[7], V[8], V[9]);
	return true;
}

TArray<FString> FOkSession::OkModelFkBones(const FString& Library, const FString& ModelId, const FString& Prefab)
{
	TArray<FString> Out;
	if (!InitBindings()) return Out;
	const FTCHARToUTF8 LibUtf(*Library), IdUtf(*ModelId), PrefabUtf(*Prefab);
	TArray<uint8> Buffer;
	Buffer.SetNumUninitialized(1 << 16);
	int32 N = ok_okmodel_fk_bones(LibUtf.Get(), IdUtf.Get(), PrefabUtf.Get(), Buffer.GetData(), Buffer.Num());
	if (N > Buffer.Num()) { Buffer.SetNumUninitialized(N); N = ok_okmodel_fk_bones(LibUtf.Get(), IdUtf.Get(), PrefabUtf.Get(), Buffer.GetData(), Buffer.Num()); }
	if (N <= 0 || N > Buffer.Num()) return Out;
	const FUTF8ToTCHAR Text(reinterpret_cast<const ANSICHAR*>(Buffer.GetData()), N);
	const FString Json(Text.Length(), Text.Get());
	TArray<TSharedPtr<FJsonValue>> Names;
	if (FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Names))
		for (const TSharedPtr<FJsonValue>& V : Names) Out.Add(V->AsString());
	return Out;
}

namespace
{
	/** Length-prefixed getter protocol of the ok_okmodel_* text/PNG functions. */
	template <typename F> bool OkModelBytes(F&& Call, TArray<uint8>& Out)
	{
		const int32 Len = Call(nullptr, 0);
		if (Len < 0) return false;
		Out.SetNumUninitialized(Len);
		return Call(Out.GetData(), Len) == Len;
	}
	FString Utf8(const TArray<uint8>& Bytes)
	{
		const FUTF8ToTCHAR Text(reinterpret_cast<const ANSICHAR*>(Bytes.GetData()), Bytes.Num());
		return FString(Text.Length(), Text.Get());
	}
}

bool FOkModel::Locate(const FString& ModelPath, FString& OutLibrary, FString& OutId)
{
	OutId = FPaths::GetBaseFilename(ModelPath);
	OutLibrary = FPaths::GetPath(FPaths::GetPath(FPaths::GetPath(FPaths::ConvertRelativePathToFull(ModelPath))));
	return OutId.Len() == 32 && FPaths::FileExists(OutLibrary / TEXT("oklib.json"));
}

bool FOkModel::Info(const FString& Library, const FString& ModelId, FString& OutJson, FString& OutError)
{
	const FTCHARToUTF8 L(*Library), I(*ModelId);
	TArray<uint8> Bytes;
	const int32 Len = ok_okmodel_info(L.Get(), I.Get(), nullptr, 0);
	Bytes.SetNumZeroed(Len < 0 ? 4096 : Len + 1);
	const int32 Got = ok_okmodel_info(L.Get(), I.Get(), Bytes.GetData(), Bytes.Num());
	if (Got < 0)
	{
		Bytes.SetNum(FCStringAnsi::Strlen(reinterpret_cast<const ANSICHAR*>(Bytes.GetData())));
		OutError = Utf8(Bytes);
		return false;
	}
	Bytes.SetNum(Got);
	OutJson = Utf8(Bytes);
	return true;
}

bool FOkModel::GetEdits(const FString& Library, const FString& ModelId, FString& OutJson)
{
	const FTCHARToUTF8 L(*Library), I(*ModelId);
	TArray<uint8> Bytes;
	if (!OkModelBytes([&](uint8* Out, int32 Cap) { return ok_okmodel_edits_get(L.Get(), I.Get(), Out, Cap); }, Bytes)) return false;
	OutJson = Utf8(Bytes);
	return true;
}

bool FOkModel::SetEdits(const FString& Library, const FString& ModelId, const FString& Json, FString& OutError)
{
	const FTCHARToUTF8 L(*Library), I(*ModelId), J(*Json);
	char Err[2048];
	Err[0] = 0;
	if (ok_okmodel_edits_set(L.Get(), I.Get(), J.Get(), Err, 2048) == 0) return true;
	OutError = UTF8_TO_TCHAR(Err);
	return false;
}

bool FOkModel::FirstPrefab(const FString& Library, const FString& ModelId, FString& OutPrefab, FString& OutError)
{
	const FTCHARToUTF8 L(*Library), I(*ModelId);
	char Out[2048];
	Out[0] = 0;
	const bool bOk = ok_okmodel_first_prefab(L.Get(), I.Get(), Out, 2048) == 0;
	(bOk ? OutPrefab : OutError) = UTF8_TO_TCHAR(Out);
	return bOk;
}

bool FOkModel::SaveAs(const FString& Library, const FString& ModelId, FString& OutModelId, FString& OutError)
{
	const FTCHARToUTF8 L(*Library), I(*ModelId);
	char Out[2048];
	Out[0] = 0;
	const bool bOk = ok_okmodel_save_as(L.Get(), I.Get(), Out, 2048) == 0;
	(bOk ? OutModelId : OutError) = UTF8_TO_TCHAR(Out);
	return bOk;
}

bool FOkModel::TexturePng(const FString& Library, const FString& ModelId, const FString& Texture, TArray<uint8>& OutPng)
{
	const FTCHARToUTF8 L(*Library), I(*ModelId), T(*Texture);
	return OkModelBytes([&](uint8* Out, int32 Cap) { return ok_okmodel_texture_png(L.Get(), I.Get(), T.Get(), Out, Cap); }, OutPng);
}

namespace { std::atomic<int32> GBackgroundCloses{0}; thread_local const FOkSessionHandle* GSolvingHandle = nullptr; }
void FOkSessionHandle::Sync() const
{
	if (Async.IsValid() && GSolvingHandle != this && !Async.IsCompleted())
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(KK_SessionAsyncWait);
		Async.Wait();
	}
}
void FOkSession::LaunchAsyncAnimFrame(float Time, float DeltaSeconds)
{
	Handle.Sync();
	if (!Handle.Raw) return;
	Handle.Async = UE::Tasks::Launch(UE_SOURCE_LOCATION, [this, Time, DeltaSeconds]()
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(KK_NativeAnimationAsync);
		TGuardValue<const FOkSessionHandle*> Solving(GSolvingHandle, &Handle);
		return ApplyAnimFrame(Time, DeltaSeconds);
	});
}
bool FOkSession::FinishAsyncAnimFrame()
{
	if (!Handle.Async.IsValid()) return false;
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(KK_SessionAsyncWait);
		Handle.Async.Wait();
	}
	const bool Result = Handle.Async.GetResult();
	Handle.Async = {};
	return Result;
}
void FOkSession::WaitForBackgroundCloses(){while(GBackgroundCloses.load()>0)FPlatformProcess::Sleep(0.001f);}
void FOkSession::Close()
{
 Handle.Sync();Handle.Async={};
 // The prepared material data (decoded texture mips, often hundreds of MB) is freed with the session, off the frame.
 TSharedPtr<struct FKKRuntimeMaterialData> Materials=MoveTemp(RuntimeMaterials);RuntimeMaterials.Reset();
	if (Handle && GRust.session_free)
	{
		// Freeing a character's native session (its meshes, textures and dynamics) took 10-35 ms; on the game thread
		// that was a dropped frame for every replaced or evicted preview. Nothing references the handle after Close.
		if (IsInGameThread() && !IsEngineExitRequested())
		{
			OkSession* Closing = Handle; auto Free = GRust.session_free;
			++GBackgroundCloses;
			UE::Tasks::Launch(UE_SOURCE_LOCATION, [Closing, Free, Materials = MoveTemp(Materials)]() mutable { Free(Closing); Materials.Reset(); --GBackgroundCloses; }, UE::Tasks::ETaskPriority::BackgroundNormal);
		}
		else GRust.session_free(Handle);
	}
	Handle = nullptr;
	SkinPaletteCapacityHints.Reset();
	PrivateSkinStageCapacityHints.Reset();
	bPortable = false;
 bSharedAnimation = false;
}

int32 FOkSession::PartCount() const
{
	if (!Handle || !GRust.part_count)
	{
		return 0;
	}
	return GRust.part_count(Handle);
}

bool FOkSession::PartAt(int32 Index, OkMeshView& Out) const
{
	if (!Handle || !GRust.part_at)
	{
		return false;
	}
	return GRust.part_at(Handle, Index, &Out) == 0;
}

bool FOkSession::TextureSettings(int32 Index, const char* Property, OkTextureSettings& Out) const
{
	return Handle && ok_part_texture_settings(Handle, Index, Property, &Out) == 0;
}

namespace {
// Borrowed source pointers and pose matrices are never cached. Stage all output
// so even a failed/retried native read cannot corrupt the caller's last frame.
int32 ReadSkinPalette(int32& CapacityHint, OkSkinPaletteView& Out, TArray<float>& Matrices,
 TFunctionRef<int32(OkSkinPaletteView*, float*, int32)> Read) {
 constexpr int32 MaxMatrices=65535;
 TArray<float> Values;
 auto Copy=[&](int32 Capacity)->int32 {
  if(Capacity<=0||Capacity>MaxMatrices)return -1;
  Values.SetNumUninitialized(Capacity*16);
  OkSkinPaletteView View{};
  const int32 Count=Read(&View,Values.GetData(),Capacity);
  if(Count<=0||Count>Capacity)return -1;
  Values.SetNum(Count*16,EAllowShrinking::No);
  Out=View;Matrices=MoveTemp(Values);CapacityHint=Count;
  return Count;
 };
 if(CapacityHint>0&&CapacityHint<=MaxMatrices){
  const int32 Count=Copy(CapacityHint);
  if(Count>0)return Count;
 }
 const int32 Needed=Read(nullptr,nullptr,0);
 const int32 Count=Copy(Needed);
 if(Count<=0)CapacityHint=0;
 return Count;
}
}

int32 FOkSession::SkinPalette(int32 Index,OkSkinPaletteView& Out,TArray<float>& Matrices) const {
 if(!Handle||Index<0)return -1;
 int32 Hint=SkinPaletteCapacityHints.FindRef(Index);
 const int32 Count=ReadSkinPalette(Hint,Out,Matrices,[&](OkSkinPaletteView* View,float* Values,int32 Capacity){
  return ok_part_skin_palette(Handle,Index,View,Values,Capacity);
 });
 if(Count>0)SkinPaletteCapacityHints.Add(Index,Hint);
 else SkinPaletteCapacityHints.Remove(Index);
 return Count;
}

int32 FOkSession::PrivateSkinStages(int32 Index,OkSkinPaletteView& Out,TArray<float>& Matrices,TArray<float>& PostCorrection) const {
 if(!Handle||Index<0||&Matrices==&PostCorrection)return -1;
 int32 Hint=PrivateSkinStageCapacityHints.FindRef(Index);float Post[16]{};
 const int32 Count=ReadSkinPalette(Hint,Out,Matrices,[&](OkSkinPaletteView* View,float* Values,int32 Capacity){
  return ok_part_private_skin_stages(Handle,Index,View,Values,Capacity,Post);
 });
 if(Count>0){PrivateSkinStageCapacityHints.Add(Index,Hint);PostCorrection.SetNumUninitialized(16);FMemory::Memcpy(PostCorrection.GetData(),Post,sizeof(Post));}
 else PrivateSkinStageCapacityHints.Remove(Index);
 return Count;
}

uint64 FOkSession::PrivateSkinSourceRevision(int32 Index) const {
 return Handle&&Index>=0?ok_part_private_skin_source_revision(Handle,Index):0;
}
bool FOkSession::SkinDisplayExceptPrivate(TConstArrayView<int32> Parts,TConstArrayView<uint64> Revisions){
 return Handle&&Parts.Num()==Revisions.Num()&&ok_skin_display_except_private(Handle,Parts.GetData(),Revisions.GetData(),Parts.Num())==0;
}
bool FOkSession::SetSceneGpuParts(TConstArrayView<int32> Parts,TConstArrayView<uint64> Revisions){
 return Handle&&Parts.Num()==Revisions.Num()&&ok_session_set_scene_gpu_parts(Handle,Parts.GetData(),Revisions.GetData(),Parts.Num())==0;
}
bool FOkSession::QueryPrivateSkinGeometry(int32 Part){return Handle&&Part>=0&&ok_query_private_skin_geometry(Handle,Part)==0;}
bool FOkSession::QueryPrivateSkinGeometryBatch(TConstArrayView<int32> Parts){return Handle&&ok_query_private_skin_geometry_batch(Handle,Parts.GetData(),Parts.Num())==0;}

bool FOkSession::PrivateSkinStagesBatch(TConstArrayView<int32> Parts,TConstArrayView<uint64> Revisions,TConstArrayView<int32> Counts,TArray<float>& Matrices,TArray<float>& Posts) const {
 if(!Handle||Parts.IsEmpty()||Parts.Num()!=Revisions.Num()||Parts.Num()!=Counts.Num()||&Matrices==&Posts)return false;
 int64 Total=0;for(int32 Count:Counts){if(Count<2||Count>65535)return false;Total+=Count;}if(Total>8388608)return false;
 TArray<float> Next,NextPosts;Next.SetNumUninitialized(int32(Total)*16);NextPosts.SetNumUninitialized(Parts.Num()*16);
 if(ok_private_skin_stages_batch(Handle,Parts.GetData(),Revisions.GetData(),Counts.GetData(),Parts.Num(),Next.GetData(),int32(Total),NextPosts.GetData())!=0)return false;
 Matrices=MoveTemp(Next);Posts=MoveTemp(NextPosts);return true;
}
int32 FOkSession::PrivateSkinStagesRevision(int32 Index,uint64 ExpectedRevision,OkSkinPaletteView& Out,TArray<float>& Matrices,TArray<float>& PostCorrection) const {
 if(!Handle||Index<0||ExpectedRevision==0||&Matrices==&PostCorrection)return -1;
 int32 Hint=PrivateSkinStageCapacityHints.FindRef(Index);float Post[16]{};
 const int32 Count=ReadSkinPalette(Hint,Out,Matrices,[&](OkSkinPaletteView* View,float* Values,int32 Capacity){
  return ok_part_private_skin_stages_revision(Handle,Index,ExpectedRevision,View,Values,Capacity,Post);
 });
 if(Count>0){PrivateSkinStageCapacityHints.Add(Index,Hint);PostCorrection.SetNumUninitialized(16);FMemory::Memcpy(PostCorrection.GetData(),Post,sizeof(Post));}
 else PrivateSkinStageCapacityHints.Remove(Index);
 return Count;
}

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKKPaletteReadCapacityTest,"OpenKoikatsu.Animation.PaletteReadCapacity",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FKKPaletteReadCapacityTest::RunTest(const FString&) {
 int32 Hint=0,Needed=3,Calls=0;bool Fail=false;
 float Source[3]={1,2,3};OkSkinPaletteView Out{};TArray<float> Values;
 auto Native=[&](OkSkinPaletteView* View,float* Matrices,int32 Capacity)->int32 {
  ++Calls;
  if(!Matrices)return Needed;
  // Deliberately dirty staging on failure to verify atomic caller publication.
  Matrices[0]=-999;
  if(Fail||Capacity<Needed)return -1;
  for(int32 I=0;I<Needed*16;++I)Matrices[I]=float(I+Calls);
  View->positions=Source;View->n_verts=3;return Needed;
 };
 TestEqual(TEXT("Cold read succeeds"),ReadSkinPalette(Hint,Out,Values,Native),3);
 TestEqual(TEXT("Cold discovery plus copy"),Calls,2);
 Calls=0;TestEqual(TEXT("Warm read succeeds"),ReadSkinPalette(Hint,Out,Values,Native),3);
 TestEqual(TEXT("Warm read exports only once"),Calls,1);
 TestEqual(TEXT("New pose is read, not cached"),Values[0],1.f);
 Needed=5;Calls=0;TestEqual(TEXT("Growth retries new capacity"),ReadSkinPalette(Hint,Out,Values,Native),5);
 TestEqual(TEXT("Growth bounded to copy, discovery, copy"),Calls,3);
 TestEqual(TEXT("Growth publishes complete length"),Values.Num(),80);
 Needed=2;Calls=0;TestEqual(TEXT("Shrink accepts current actual length"),ReadSkinPalette(Hint,Out,Values,Native),2);
 TestEqual(TEXT("Shrink exports once"),Calls,1);TestEqual(TEXT("Shrink trims old matrices"),Values.Num(),32);
 const TArray<float> Stable=Values;const float* StableSource=Out.positions;
 Fail=true;Calls=0;TestEqual(TEXT("Unavailable source rejected"),ReadSkinPalette(Hint,Out,Values,Native),-1);
 TestEqual(TEXT("Failure bounded to three native calls"),Calls,3);
 TestTrue(TEXT("Failed copy preserves matrices"),Values==Stable);
 TestTrue(TEXT("Failed copy preserves source view"),Out.positions==StableSource&&Out.n_verts==3);
 TestEqual(TEXT("Failed hint invalidated"),Hint,0);
 Fail=false;Needed=65536;Calls=0;
 TestEqual(TEXT("Unbounded allocation rejected"),ReadSkinPalette(Hint,Out,Values,Native),-1);
 TestEqual(TEXT("Illegal count never copied"),Calls,1);
 Needed=0;TestEqual(TEXT("Empty palette rejected"),ReadSkinPalette(Hint,Out,Values,Native),-1);
 TestTrue(TEXT("Invalid discovery preserves previous matrices"),Values==Stable);
 Needed=3;TestEqual(TEXT("Recovers after source becomes available"),ReadSkinPalette(Hint,Out,Values,Native),3);
 return true;
}
#endif
bool FOkSession::BoneWorldMatrix(const FString& Name,float Out[16]) const {return Handle&&ok_bone_world_matrix(Handle,TCHAR_TO_UTF8(*Name),Out)==0;}
FString FOkSession::SkinSourceHash(int32 Part) const {uint8 Hash[32];return Handle&&ok_part_skin_source_hash(Handle,Part,Hash)==0?BytesToHex(Hash,32):FString();}
FString FOkSession::SkinSourceHashV2(int32 Part) const {uint8 Hash[32];return Handle&&ok_part_skin_source_hash_v2(Handle,Part,Hash)==0?BytesToHex(Hash,32):FString();}
bool FOkSession::RawMorph(int32 Part,int32 Morph,FString& Name,TArray<uint32>& Indices,TArray<float>& Deltas) const {
 if(!Handle)return false;const int32 Count=ok_part_raw_morph(Handle,Part,Morph,nullptr,nullptr,nullptr,0);if(Count<0||Count>10000000)return false;
 if(!Count){Name.Reset();Indices.Reset();Deltas.Reset();return true;}TArray<uint32> I;TArray<float> D;I.SetNum(Count);D.SetNum(Count*3);const char* Label=nullptr;
 if(ok_part_raw_morph(Handle,Part,Morph,&Label,I.GetData(),D.GetData(),Count)!=Count||!Label)return false;
 Name=UTF8_TO_TCHAR(Label);Indices=MoveTemp(I);Deltas=MoveTemp(D);return true;
}
bool FOkSession::OutlineEnabled(int32 Index) const
{
	return Handle && ok_part_outline_enabled(Handle, Index) == 1;
}

bool FOkSession::MaterialKeyword(int32 Index,const char* Keyword) const
{
    return Handle && ok_part_material_keyword(Handle,Index,Keyword)==1;
}
FString FOkSession::MaterialMetadataJson(int32 Index) const
{
    if (!Handle) return {};
    const int32 Size=ok_part_material_metadata(Handle,Index,nullptr,0);
    if (Size<=0) return {};
    TArray<uint8> Bytes; Bytes.SetNumZeroed(Size+1);
    if (ok_part_material_metadata(Handle,Index,Bytes.GetData(),Size)!=Size) return {};
    return UTF8_TO_TCHAR(reinterpret_cast<const char*>(Bytes.GetData()));
}

FString FOkSession::MaterialShaderName(int32 Index) const
{
    TSharedPtr<FJsonObject> Root;
    if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(MaterialMetadataJson(Index)),Root) || !Root) return {};
    const TSharedPtr<FJsonObject>* Material=nullptr;
    FString Shader;
    if (Root->TryGetObjectField(TEXT("material"),Material) && Material && *Material)
        (*Material)->TryGetStringField(TEXT("shader"),Shader);
    return Shader;
}

bool FOkSession::MaterialValue(int32 Index,const char* Property,FLinearColor& Out) const
{
	float Value[4];if (!Handle || ok_part_material_value(Handle,Index,Property,Value)!=0) return false;
	Out=FLinearColor(Value[0],Value[1],Value[2],Value[3]);return true;
}
bool FOkSession::ShaderTexture(int32 Index,const char* Property,const uint8*& Out,int32& Length) const
{
	return Handle && ok_part_shader_texture(Handle,Index,Property,&Out,&Length)==0;
}
bool FOkSession::EyeExpressionUVs(int32 Index,TArray<FVector2f>& Out) const
{
	Out.Reset();
	const int32 Count=Handle?ok_part_eye_expression_uvs(Handle,Index,nullptr,0):-1;
	if(Count<=0)return false;
	TArray<float> Raw;Raw.SetNumUninitialized(Count*2);
	if(ok_part_eye_expression_uvs(Handle,Index,Raw.GetData(),Count)!=Count)return false;
	for(int32 V=0;V<Count;++V)Out.Add(FVector2f(Raw[V*2],Raw[V*2+1]));
	return true;
}

bool FOkSession::ApplyShapeLive(const TArray<float>& Body,const TArray<float>& Face,bool DeferDynamics)
{
 return Handle&&(DeferDynamics?ok_prepare_shape_live:ok_apply_shape_live)(Handle,Body.GetData(),Body.Num(),Face.GetData(),Face.Num())==0;
}

bool FOkSession::ApplyShape(const TArray<float>& Body, const TArray<float>& Face)
{
	if (!Handle || !GRust.apply_shape)
	{
		return false;
	}
	return GRust.apply_shape(
			   Handle,
			   Body.GetData(),
			   Body.Num(),
			   Face.GetData(),
			   Face.Num())
		== 0;
}

bool FOkSession::Skin()
{
	if (!Handle || !GRust.skin)
	{
		return false;
	}
	return GRust.skin(Handle) == 0;
}

TArray<float> FOkSession::ShapeBody() const
{
	TArray<float> Out;
	if (!Handle || !GRust.shape_body)
	{
		return Out;
	}
	const int32 N = GRust.shape_body(Handle, nullptr, 0);
	Out.SetNumUninitialized(N);
	if (N > 0)
	{
		GRust.shape_body(Handle, Out.GetData(), N);
	}
	return Out;
}

TArray<float> FOkSession::ShapeFace() const
{
	TArray<float> Out;
	if (!Handle || !GRust.shape_face)
	{
		return Out;
	}
	const int32 N = GRust.shape_face(Handle, nullptr, 0);
	Out.SetNumUninitialized(N);
	if (N > 0)
	{
		GRust.shape_face(Handle, Out.GetData(), N);
	}
	return Out;
}

int32 FOkSession::CoordinateCount() const
{
	if (!Handle || !GRust.coord_count)
	{
		return 0;
	}
	return GRust.coord_count(Handle);
}

int32 FOkSession::SkelBoneCount()
{
	if (!Handle || !GRust.skel_bone_count)
	{
		return 0;
	}
	return GRust.skel_bone_count(Handle);
}

bool FOkSession::SkelBoneAt(int32 Index, OkBoneView& Out)
{
	if (!Handle || !GRust.skel_bone_at)
	{
		return false;
	}
	return GRust.skel_bone_at(Handle, Index, &Out) == 0;
}

int32 FOkSession::MorphCount(int32 PartIndex) const
{
	if (!Handle || !GRust.morph_count)
	{
		return 0;
	}
	return GRust.morph_count(Handle, PartIndex);
}

bool FOkSession::MorphAt(int32 PartIndex, int32 MorphIndex, OkMorphView& Out) const
{
	if (!Handle || !GRust.morph_at)
	{
		return false;
	}
	return GRust.morph_at(Handle, PartIndex, MorphIndex, &Out) == 0;
}

bool FOkSession::MouthPair(int32 PartIndex, int32 Pattern, int32& Close, int32& Open) const
{
	if (!Handle || !GRust.mouth_pair)
	{
		return false;
	}
	return GRust.mouth_pair(Handle, PartIndex, Pattern, &Close, &Open) == 0;
}

int32 FOkSession::MouthPatternCount(int32 PartIndex) const
{
	return Handle && GRust.mouth_pattern_count ? GRust.mouth_pattern_count(Handle, PartIndex) : 0;
}

int32 FOkSession::FacePatternCount(int32 PartIndex, int32 Control) const
{
	return Handle && GRust.face_pattern_count ? GRust.face_pattern_count(Handle, PartIndex, Control) : 0;
}
bool FOkSession::FacePair(int32 PartIndex, int32 Control, int32 Pattern, int32& Close, int32& Open) const
{
	return Handle && GRust.face_pair && GRust.face_pair(Handle, PartIndex, Control, Pattern, &Close, &Open) == 0;
}
float FOkSession::FaceBaselineWeight(int32 PartIndex, int32 Morph) const
{
	return Handle && GRust.face_baseline_weight ? GRust.face_baseline_weight(Handle, PartIndex, Morph) : 0.f;
}
float FOkSession::FaceDefaultRate(int32 Control) const
{
	return Handle && GRust.face_default_rate ? GRust.face_default_rate(Handle, Control) : 0.f;
}
float FOkSession::FaceRuntimeSetting(int32 Setting) const
{
	return Handle && GRust.face_runtime_setting ? GRust.face_runtime_setting(Handle,Setting) : 0.f;
}
float FOkSession::EyeMaterialSetting(int32 PartIndex, int32 Setting) const
{
	return Handle && GRust.eye_material_setting ? GRust.eye_material_setting(Handle,PartIndex,Setting) : 0.f;
}
bool FOkSession::SetEyeGaze(float LeftH, float RightH, float Vertical)
{
	return Handle && GRust.set_eye_gaze && GRust.set_eye_gaze(Handle,LeftH,RightH,Vertical)==0;
}
int32 FOkSession::EyeLookPatternCount() const
{
    return Handle && GRust.eye_look_pattern_count ? GRust.eye_look_pattern_count(Handle) : 0;
}
bool FOkSession::StepEyeLook(const FVector& Target,int32 Pattern,float Dt,bool Enabled,float* Rates)
{
    return Handle && GRust.step_eye_look && GRust.step_eye_look(Handle,Target.X,Target.Y,Target.Z,Pattern,Dt,Enabled?1:0,Rates)==0;
}

bool FOkSession::ApplyAnimTime(float Time)
{
	if (bPortable) return true;

	if (!Handle || !GRust.apply_anim_time)
	{
		return false;
	}
	return GRust.apply_anim_time(Handle, Time) == 0;
}

int32 FOkSession::AnimationFloatCount() const
{
	if (bPortable) return 0;

    return Handle&&GRust.anim_float_count?GRust.anim_float_count(Handle):0;
}
bool FOkSession::AnimationFloatAt(int32 Index,FString& Name,float& Value,float& Min,float& Max) const
{
	if (bPortable) return false;

    char Buffer[1024];float Ranges[3];
    if(!Handle||!GRust.anim_float_at||GRust.anim_float_at(Handle,Index,Buffer,1024,Ranges)!=0) return false;
    Name=UTF8_TO_TCHAR(Buffer);Value=Ranges[0];Min=Ranges[1];Max=Ranges[2];return true;
}
bool FOkSession::SetAnimationFloat(const FString& Name,float Value)
{
	if (bPortable) return false;

    FTCHARToUTF8 Utf8(*Name);
    return Handle&&GRust.set_anim_float&&GRust.set_anim_float(Handle,Utf8.Get(),Value)==0;
}

TArray<FString> FOkSession::AnimationTriggers() const
{
	if (bPortable) return TArray<FString>();

    TArray<FString> Names;if(!Handle) return Names;
    for(int32 I=0;I<ok_animation_trigger_count(Handle);++I) {
        ANSICHAR Buffer[1024]={};
        if(ok_animation_trigger_at(Handle,I,Buffer,1024)==0) Names.Add(UTF8_TO_TCHAR(Buffer));
    }
    return Names;
}

bool FOkSession::SetAnimationTrigger(const FString& Name)
{
	if (bPortable) return false;

    FTCHARToUTF8 Utf8(*Name);return Handle&&ok_set_animation_trigger(Handle,Utf8.Get())==0;
}
bool FOkSession::CrossFadeAnimation(const FString& Name,float Duration,float Offset)
{
	if (bPortable) return false;

    FTCHARToUTF8 Utf8(*Name);return Handle&&ok_animation_crossfade(Handle,Utf8.Get(),Duration,Offset)==0;
}
bool FOkSession::CrossFadeAnimationLayer(const FString& Name,int32 Layer,float Duration,float Offset)
{
	if (bPortable) return false;

	FTCHARToUTF8 Utf8(*Name);return Handle&&ok_animation_crossfade_layer(Handle,Layer,Utf8.Get(),Duration,Offset)==0;
}
float FOkSession::AnimationElapsed() const {
	if (bPortable) return 0.f;
return Handle?ok_animation_elapsed(Handle):0.f;}
float FOkSession::AnimationTransitionSourceLength() const {
	if (bPortable) return 0.f;
return Handle?ok_animation_transition_source_length(Handle):0.f;}
float FOkSession::AnimationLayerTransitionSourceLength(int32 Layer) const {
	if (bPortable) return 0.f;
return Handle?ok_animation_layer_transition_source_length(Handle,Layer):0.f;}
bool FOkSession::IsAnimationLayerTransitioning(int32 Layer) const {
	if (bPortable) return false;
return Handle&&ok_animation_layer_in_transition(Handle,Layer)!=0;}
bool FOkSession::IsAnimationTransitioning() const {
	if (bPortable) return false;
return Handle&&ok_animation_in_transition(Handle)!=0;}

bool FOkSession::SetStudioAnimation(const FString& Key,bool IsH,float Height,float Breast,bool IsMotion,float Motion,const FString& YureSource,const FString& YureOwner,int32 PVMask)
{
	if (bPortable) return false;

    FTCHARToUTF8 Utf8(*Key),Source(*YureSource),Owner(*YureOwner);
    if(!Handle||!GRust.set_studio_animation_pv_profile||GRust.set_studio_animation_pv_profile(Handle,Utf8.Get(),IsH?1:0,Height,Breast,IsMotion?1:0,Motion,Source.Get(),Owner.Get(),PVMask)!=0) return false;
    FPlatformMisc::SetEnvironmentVar(TEXT("OK_UE_ANIM"),nullptr);
    return true;
}

bool FOkSession::SetStudioPV(int32 Mask,bool FK) {return Handle&&GRust.set_studio_pv&&GRust.set_studio_pv(Handle,Mask,FK?1:0)==0;}
int32 FOkSession::StudioPVFlag(int32 Index) const {return Handle&&GRust.studio_pv_flag?GRust.studio_pv_flag(Handle,Index):-1;}
int32 FOkSession::StudioPVBone(int32 Index,bool Source) {return Handle&&GRust.studio_pv_bone?GRust.studio_pv_bone(Handle,Index,Source?1:0):-1;}

bool FOkSession::ApplyAnimFrame(float Time, float DeltaSeconds)
{
	RegisterNativeCpuTrace();
	if (bPortable&&!bSharedAnimation) return true;

	if (!Handle || !GRust.apply_anim_frame)
	{
		return false;
	}
	return GRust.apply_anim_frame(Handle, Time, DeltaSeconds) == 0;
}

int32 FOkSession::StudioYureFlag(int32 Index) const
{
    return Handle&&GRust.studio_yure_flag?GRust.studio_yure_flag(Handle,Index):-1;
}

bool FOkSession::ExternalBodyPoseAt(int32 Bone,OkBodyPose& Out)
{
	return Handle&&GRust.external_body_pose_at&&GRust.external_body_pose_at(Handle,Bone,&Out)==0;
}
bool FOkSession::ApplyExternalBodyFrame(float Time,float DeltaSeconds,const TArray<OkBodyPose>& Poses)
{
	if (bPortable) return false;

	return Handle&&GRust.apply_external_body_frame&&GRust.apply_external_body_frame(Handle,Time,DeltaSeconds,Poses.GetData(),Poses.Num())==0;
}
bool FOkSession::BuildUnrealBodyFrame(const FReferenceSkeleton& Ref,UAnimSequence* Sequence,float Time,TArray<OkBodyPose>& Out)
{
	Out.Reset();const int32 Count=SkelBoneCount();if(Count!=Ref.GetNum()) return false;
	TArray<FTransform> Locals=Ref.GetRefBonePose();TArray<OkBodyPose> Baseline;
	for(int32 Bone=0;Bone<Count;++Bone) {
		OkBodyPose P{};
		if(ExternalBodyPoseAt(Bone,P)) {Locals[Bone]=OKUnityLocalToUnreal(P.position,P.rotation,P.scale);Baseline.Add(P);}
	}
	const TArray<FTransform> Before=Locals;
	if(!OKRetargetUnrealPose(Locals,Ref,Sequence,Time)) return false;
	for(const OkBodyPose& Base:Baseline) {
		if(Locals[Base.bone].ContainsNaN()) return false;
		if(Locals[Base.bone].Equals(Before[Base.bone],1.e-9)) continue;
		OkBodyPose P{};P.bone=Base.bone;
		OKUnrealLocalToUnity(Locals[Base.bone],P.position,P.rotation,P.scale);Out.Add(P);
	}
	return true;
}
bool FOkSession::ApplyUnrealFrame(const FReferenceSkeleton& Ref,UAnimSequence* Sequence,float Time,float DeltaSeconds)
{
	TArray<OkBodyPose> Frame;
	return BuildUnrealBodyFrame(Ref,Sequence,Time,Frame)&&ApplyExternalBodyFrame(Time,DeltaSeconds,Frame);
}

float FOkSession::AnimDuration()
{
	if (bPortable&&!bSharedAnimation) return 1.f;

	if (!Handle || !GRust.anim_duration)
	{
		return 1.f;
	}
	const float D = GRust.anim_duration(Handle);
	return FMath::IsFinite(D) && D > 0.f ? D : 1.f;
}

bool FOkSession::IsAnimLooping()
{
	if (bPortable&&!bSharedAnimation) return false;

	return Handle && GRust.anim_looping && GRust.anim_looping(Handle) != 0;
}

TArray<FString> FOkSession::ClipNames() const
{
 if(bPortable)return ImportedSharedClipNames();
 if(!Handle)return {};
 return ReadAnimationNames([&](uint8* Out,int32 Size){return ok_session_animation_names(Handle,"",0,Out,Size);});
}

TArray<FString> FOkSession::ClipSources() const
{
 if(bPortable){TArray<FString> Sources;for(const auto& Key:ImportedSharedClipNames()){FString Source,Clip;if(Key.Split(TEXT("|"),&Source,&Clip))Sources.AddUnique(Source);}return Sources;}
 if(!Handle)return {};
 return ReadAnimationNames([&](uint8* Out,int32 Size){return ok_session_animation_names(Handle,"",1,Out,Size);});
}

TArray<FString> FOkSession::ClipNamesForSource(const FString& Source) const
{
 if(bPortable&&Source.StartsWith(TEXT("states:")))return AnimationSourceNames(CommonPairRoot(),Source);
 if(bPortable){TArray<FString> Names;for(const auto& Key:ImportedSharedClipNames(Source)){FString Bundle,Clip;if(Key.Split(TEXT("|"),&Bundle,&Clip))Names.Add(Clip);}return Names;}
 if(!Handle)return {};
 const FTCHARToUTF8 SourceUtf(*Source);
 return ReadAnimationNames([&](uint8* Out,int32 Size){return ok_session_animation_names(Handle,SourceUtf.Get(),0,Out,Size);});
}

bool FOkSession::SetAnimClip(const FString& Name)
{
	if (IsVideoClipKey(Name))
	{
		// Portable sessions already run on a shared clip root; game sessions
		// keep their game root so outfits and native clips still resolve.
		TArray<uint8> Bytes;
		if (!Handle || !LoadVideoClip(Name, Bytes)) return false;
		if (bPortable) return InstallSharedClip(Bytes);
		if (ok_session_install_video_clip(Handle, Bytes.GetData(), Bytes.Num()) != 0) return false;
		// Like a native clip selection: sample source curves, not the external UE pose.
		FPlatformMisc::SetEnvironmentVar(TEXT("OK_UE_ANIM"), nullptr);
		return true;
	}
	if (bPortable) {const FString Root=CommonPairRoot();if(!Handle||Root.IsEmpty()||!Name.StartsWith(TEXT("states:")))return false;if(ok_select_common_animation(Handle,TCHAR_TO_UTF8(*Root),TCHAR_TO_UTF8(*Name))!=0)return false;bSharedAnimation=true;return true;}

	if (!Handle || !GRust.set_anim_clip)
	{
		return false;
	}
	const FTCHARToUTF8 NameUtf(*Name);
	if (GRust.set_anim_clip(Handle, NameUtf.Get()) != 0) return false;
	// Open prepares T-pose/external sampling. Selecting a game clip is the
	// common source-playback entry, including non-Actor and catalogue callers.
	FPlatformMisc::SetEnvironmentVar(TEXT("OK_UE_ANIM"), nullptr);
	return true;
}

bool FOkSession::SetAnimSceneMode(int32 Mode)
{
	// Scene mode is session metadata, also valid before the first portable
	// controller is selected. Selection uses it to classify the controller.
	return Handle && GRust.set_anim_scene_mode && GRust.set_anim_scene_mode(Handle, Mode) == 0;
}

bool OKExtractCard(
	const FString& GameRoot,
	const FString& CardPath,
	const FString& OutDir,
	FString& OutError,
	int32 OutfitIndex)
{
	IFileManager::Get().MakeDirectory(*OutDir, true);
	const FTCHARToUTF8 GameUtf(*GameRoot);
	const FTCHARToUTF8 OutUtf(*OutDir);
	const FTCHARToUTF8 CardUtf(*CardPath);
	char Err[2048];
	Err[0] = 0;
	const int32 Rc = ok_extract_card_outfit(
		GameUtf.Get(),
		CardPath.IsEmpty() ? nullptr : CardUtf.Get(),
		OutUtf.Get(),
		OutfitIndex,
		Err,
		2048);
	if (Rc != 0)
	{
		OutError = UTF8_TO_TCHAR(Err);
		if (OutError.IsEmpty())
		{
			OutError = TEXT("ok_extract_card failed");
		}
		return false;
	}
	return true;
}

bool FOkSession::ExportAnimationSnapshot(TArray<uint8>& Out)const{
 Out.Reset();if(!Handle)return false;const int32 Count=ok_session_animation_snapshot(Handle,nullptr,0);
 if(Count<=0||Count>512*1024*1024)return false;Out.SetNumUninitialized(Count);
 if(ok_session_animation_snapshot(Handle,Out.GetData(),Count)!=Count){Out.Reset();return false;}return true;
}
bool FOkSession::CloneForInteraction(FOkSession& Destination)const{
 if(!Handle)return false;auto* Clone=ok_session_clone_interaction(Handle);if(!Clone)return false;
 // from_preview_bytes creates a self-contained session with no game root.
 // Its animation source is the common library, even when the input was KKS.
 Destination.Close();Destination.Handle=Clone;Destination.bPortable=true;Destination.bSharedAnimation=false;Destination.ApplyPhysXSecondary();return true;
}
bool FOkSession::PrepareRuntimeMaterials(FString& Error){
 if(RuntimeMaterials)return true;
 TArray<FKKRuntimePalettePart> Parts;for(int32 I=0;I<PartCount();++I){FKKRuntimePalettePart Part;Part.NativePart=I;Parts.Add(MoveTemp(Part));}
 auto Result=MakeShared<FKKRuntimeMaterialData>();if(!OKCaptureRuntimeMaterials(Handle,Parts,*Result,Error))return false;
 RuntimeMaterials=MoveTemp(Result);return true;
}
bool FOkSession::DescribeRuntimeMaterialPart(int32 Index,FKKRuntimeMaterialPart& Out,FString& Error)const{
 if(!Handle){Error=TEXT("session not open");return false;}return OKDescribeRuntimeMaterialPart(Handle,Index,Out,Error);
}
bool FOkSession::OpenAnimationSnapshot(const TArray<uint8>& Bytes){
 if(!InitBindings()||Bytes.IsEmpty())return false;auto* Next=ok_session_open_animation_snapshot(Bytes.GetData(),Bytes.Num());
 if(!Next)return false;Close();Handle=Next;bPortable=false;ApplyPhysXSecondary();return true;
}
bool FOkSession::ExportSharedClip(const FString& Key,TArray<uint8>& Out)const{
 Out.Reset();if(!Handle)return false;const int32 Count=ok_session_export_shared_clip(Handle,TCHAR_TO_UTF8(*Key),nullptr,0);
 if(Count<=0||Count>128*1024*1024)return false;Out.SetNumUninitialized(Count);
 if(ok_session_export_shared_clip(Handle,TCHAR_TO_UTF8(*Key),Out.GetData(),Count)!=Count){Out.Reset();return false;}return true;
}
bool FOkSession::InstallSharedClip(const TArray<uint8>& Bytes){
 if(!Handle||Bytes.IsEmpty()||ok_session_install_shared_clip(Handle,Bytes.GetData(),Bytes.Num())!=0)return false;
 // Direct runtime APNG callers install common curve bytes without going
 // through SelectImportedSharedClip. ApplyAnimFrame must not silently skip
 // that successfully installed clip because the session is portable.
 bSharedAnimation=true;return true;
}

namespace {
TArray<FAssetData> SharedCurveAssets(){
 // Asset tags and UObject loads are not safe on the general worker pool.
 if(!IsInGameThread())return {};
 auto& Registry=FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
 TArray<FAssetData> Assets;Registry.GetAssetsByClass(UKKSharedAnimation::StaticClass()->GetClassPathName(),Assets);
 return Assets;
}
// The shared clips are a few small assets, listed on every character load: collected after each listing, they were
// loaded again every time (~55 ms of FlushAsyncLoading in the reader's load). Loaded ones are kept.
UKKSharedAnimation* KeepSharedCurveAsset(UObject* Object){
 auto* Asset=Cast<UKKSharedAnimation>(Object);
 static TArray<TStrongObjectPtr<UKKSharedAnimation>> Kept;
 if(Asset&&!Kept.ContainsByPredicate([Asset](const TStrongObjectPtr<UKKSharedAnimation>& P){return P.Get()==Asset;}))Kept.Emplace(Asset);
 return Asset;
}
UKKSharedAnimation* FindSharedCurveAsset(const FString& Key){
 if(!IsInGameThread())return nullptr;
 struct FLookup {FString Key;FSoftObjectPath Path;};
 // Paths retain no UObject/curve buffers. Validate live metadata again after
 // GC, asset edits, deletion or replacement instead of retaining stale pointers.
 static TArray<FLookup> Recent;
 auto& Registry=FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
 const int32 Cached=Recent.IndexOfByPredicate([&](const FLookup& E){return E.Key==Key;});
 if(Cached!=INDEX_NONE){
  FLookup Entry=MoveTemp(Recent[Cached]);Recent.RemoveAt(Cached);
  const FAssetData Item=Registry.GetAssetByObjectPath(Entry.Path);
  if(Item.IsValid())if(auto* Asset=Cast<UKKSharedAnimation>(Item.GetAsset()))
   if(Asset->Source+TEXT("|")+Asset->Clip==Key){Recent.Add(MoveTemp(Entry));return Asset;}
 }
 for(const auto& Item:SharedCurveAssets())if(auto* Asset=Cast<UKKSharedAnimation>(Item.GetAsset())){
  if(Asset->Source+TEXT("|")+Asset->Clip!=Key)continue;
  if(Recent.Num()>=256)Recent.RemoveAt(0);
  Recent.Add({Key,Item.GetSoftObjectPath()});return Asset;
 }
 return nullptr;
}
}
TArray<FString> FOkSession::ImportedSharedClipNames(const FString& Source){
 TArray<FString> Result;
 for(const auto& Item:SharedCurveAssets())if(auto* Asset=KeepSharedCurveAsset(Item.GetAsset())){
  if(!Asset->AuthoredCurves.IsEmpty()&&(Source.IsEmpty()||Asset->Source==Source))Result.AddUnique(Asset->Source+TEXT("|")+Asset->Clip);
 }
 Result.Sort();return Result;
}
bool FOkSession::SelectImportedSharedClip(const FString& Key){
 if(!Handle||!bPortable)return false;
 if(auto* Asset=FindSharedCurveAsset(Key)){
  const float Duration=Asset->AuthoredCurves.IsEmpty()?0:ok_shared_clip_duration(Asset->AuthoredCurves.GetData(),Asset->AuthoredCurves.Num());
  if(!FMath::IsFinite(Duration)||Duration<=0||!FMath::IsNearlyEqual(Duration,Asset->Duration,1.e-6f))return false;
  if(!InstallSharedClip(Asset->AuthoredCurves))return false;
  bSharedAnimation=true;return true;
 }
 return false;
}
TArray<uint8> FOkSession::ImportedSharedClipBytes(const FString& Key){
 if(auto* Asset=FindSharedCurveAsset(Key))return Asset->AuthoredCurves;
 return {};
}
FString FOkSession::VideoMotionDirectory(){return FPaths::ProjectSavedDir()/TEXT("VideoMotions");}
TArray<FString> FOkSession::VideoClipNames(){
 TArray<FString> Files;IFileManager::Get().FindFiles(Files,*(VideoMotionDirectory()/TEXT("*.okanim")),true,false);
 Files.Sort();TArray<FString> Keys;for(const FString& File:Files)Keys.Add(TEXT("video:|")+FPaths::GetBaseFilename(File));
 return Keys;
}
bool FOkSession::LoadVideoClip(const FString& Key,TArray<uint8>& Out){
 Out.Reset();if(!IsVideoClipKey(Key))return false;
 const FString Name=Key.Mid(7);
 if(Name.IsEmpty()||Name.Contains(TEXT("/"))||Name.Contains(TEXT("\\"))||Name.Contains(TEXT("..")))return false;
 const FString Path=VideoMotionDirectory()/(Name+TEXT(".okanim"));
 if(IFileManager::Get().FileSize(*Path)>128*1024*1024||!FFileHelper::LoadFileToArray(Out,*Path))return false;
 if(!PrepareSharedClipBytes(Out)){Out.Reset();return false;}
 return true;
}
bool FOkSession::PrepareSharedClipBytes(const TArray<uint8>& Bytes){
 if(Bytes.IsEmpty())return false;
 const float Duration=ok_shared_clip_duration(Bytes.GetData(),Bytes.Num());
 return FMath::IsFinite(Duration)&&Duration>0.f;
}

FString FOkSession::DecodeEditorMods(const TArray<uint8>& Extended){
 const int32 N=ok_editor_decode_mods(Extended.GetData(),Extended.Num(),nullptr,0);if(N<0)return FString();
 TArray<uint8> Bytes;Bytes.SetNumZeroed(N+1);if(ok_editor_decode_mods(Extended.GetData(),Extended.Num(),Bytes.GetData(),N)!=N)return FString();
 return UTF8_TO_TCHAR(reinterpret_cast<const char*>(Bytes.GetData()));
}
bool FOkSession::ClothingTransition(const FString& Root,const FString& Card,int32 Outfit,const uint8 Previous[9],int32 Kind,uint8 Out[9],FString& Error){
 char Buffer[1024]={};const int32 Result=ok_clothing_transition(TCHAR_TO_UTF8(*Root),TCHAR_TO_UTF8(*Card),Outfit,Previous,Kind,Out,Buffer,UE_ARRAY_COUNT(Buffer));
 if(Result!=0)Error=UTF8_TO_TCHAR(Buffer);return Result==0;
}
bool FOkSession::EncodeEditorMods(const TArray<uint8>& Extended,const FString& Json,TArray<uint8>& Out){
 const FTCHARToUTF8 Utf(*Json);const int32 N=ok_editor_encode_mods(Extended.GetData(),Extended.Num(),Utf.Get(),nullptr,0);if(N<0)return false;
 Out.SetNumUninitialized(N);return ok_editor_encode_mods(Extended.GetData(),Extended.Num(),Utf.Get(),Out.GetData(),N)==N;
}
bool FOkSession::ApplyEditorModsAndShape(const FString& Json,int32 Coordinate,const TArray<float>& Body,const TArray<float>& Face,bool* GeometryChanged){
 if(!Handle)return false;
 const int32 Result=ok_editor_apply_mods_shape(Handle,TCHAR_TO_UTF8(*Json),Coordinate,Body.GetData(),Body.Num(),Face.GetData(),Face.Num());
 if(GeometryChanged)*GeometryChanged=Result==2;return Result>=0;
}
bool FOkSession::ApplyEditorMods(const FString& Json,int32 Coordinate,bool* GeometryChanged){if(!Handle)return false;const int32 Result=ok_editor_apply_mods(Handle,TCHAR_TO_UTF8(*Json),Coordinate);if(GeometryChanged)*GeometryChanged=Result==2;return Result>=0;}

bool FOkSession::ApplyBodyPhysics(float Softness,float Weight){return Handle&&ok_editor_body_physics(Handle,Softness,Weight)==0;}

extern "C" int32_t ok_appearance_request(const char*,const char*,const char*,char*,int32_t);
extern "C" int32_t ok_clothes_file(int32_t,const char*,const char*,int32_t,const char*,char*,int32_t);
static bool OKClothesFile(int32 Op,const FString& A,const FString& B,int32 Outfit,const FString& Out,FString& Summary){
 char Buffer[2048]{};const int32 Result=ok_clothes_file(Op,TCHAR_TO_UTF8(*A),B.IsEmpty()?nullptr:static_cast<const char*>(TCHAR_TO_UTF8(*B)),Outfit,TCHAR_TO_UTF8(*Out),Buffer,UE_ARRAY_COUNT(Buffer));
 Summary=UTF8_TO_TCHAR(Buffer);return Result==0;
}
bool FOkSession::ClothesImport(const FString& CoordinateCard,const FString& Out,FString& Summary){return OKClothesFile(0,CoordinateCard,FString(),0,Out,Summary);}
bool FOkSession::ClothesExport(const FString& Card,int32 Outfit,const FString& Out,FString& Summary){return OKClothesFile(1,Card,FString(),Outfit,Out,Summary);}
bool FOkSession::ClothesWear(const FString& Card,const FString& OkClothes,int32 Outfit,const FString& Out,FString& Summary){OKPrepareCardEnvironment();return OKClothesFile(2,Card,OkClothes,Outfit,Out,Summary);}
bool FOkSession::MakeOkCard(const FString& Card,const FString& Out,FString& Summary){
 OKPrepareCardEnvironment();char Buffer[2048]{};
 const int32 Result=ok_okcard_make(nullptr,TCHAR_TO_UTF8(*FPaths::ConvertRelativePathToFull(Card)),TCHAR_TO_UTF8(*FPaths::ConvertRelativePathToFull(Out)),Buffer,UE_ARRAY_COUNT(Buffer));
 Summary=UTF8_TO_TCHAR(Buffer);return Result==0;
}
bool FOkSession::MakeOkCardForOutfit(const FString& Card,const FString& Out,int32 Outfit,FString& Summary){
 OKPrepareCardEnvironment();char Buffer[2048]{};
 const int32 Result=ok_okcard_make_outfit(nullptr,TCHAR_TO_UTF8(*FPaths::ConvertRelativePathToFull(Card)),TCHAR_TO_UTF8(*FPaths::ConvertRelativePathToFull(Out)),Outfit,Buffer,UE_ARRAY_COUNT(Buffer));
 Summary=UTF8_TO_TCHAR(Buffer);return Result==0;
}
static FAutoConsoleCommand GOKMakeOkCardCommand(TEXT("OK.MakeOkCard"),
 TEXT("OK.MakeOkCard <card.png> [out.png]: converts an original KKS card (with its zipmods from the KKS installation) or an edited OK card into an OK card for the OK base library; default output <card>.ok.png."),
 FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args){
  if(Args.Num()<1){UE_LOG(LogTemp,Error,TEXT("OK.MakeOkCard: card path required"));return;}
  const FString Out=Args.Num()>1?Args[1]:FPaths::ChangeExtension(Args[0],TEXT("ok.png"));FString Summary;
  if(FOkSession::MakeOkCard(Args[0],Out,Summary)){UE_LOG(LogTemp,Display,TEXT("OK.MakeOkCard: %s -> %s (%s)"),*Args[0],*Out,*Summary);}
  else{UE_LOG(LogTemp,Error,TEXT("OK.MakeOkCard: %s failed: %s"),*Args[0],*Summary);}
 }));
extern "C" int32_t ok_head_texture_changes(OkSession*,const OkSession*,int32_t*,int32_t,int32_t,char*,int32_t);
bool FOkSession::HeadTextureChanges(const FOkSession& Source,TArray<int32>& Parts,bool Apply,FString& Error){
 Parts.SetNumUninitialized(PartCount());char Buffer[4096]{};
 const int32 Count=ok_head_texture_changes(Handle,Source.Handle,Parts.GetData(),Parts.Num(),Apply?1:0,Buffer,UE_ARRAY_COUNT(Buffer));
 if(Count<0){Parts.Reset();Error=UTF8_TO_TCHAR(Buffer);return false;}
 Parts.SetNum(Count);if(Apply)RuntimeMaterials.Reset();Error.Reset();return true;
}
bool FOkSession::AppearanceRequest(const FString& Root,const FString& Request,const FString& Report,FString& Error){
 char Buffer[8192]{};const int32 Result=ok_appearance_request(TCHAR_TO_UTF8(*Root),TCHAR_TO_UTF8(*Request),TCHAR_TO_UTF8(*Report),Buffer,UE_ARRAY_COUNT(Buffer));Error=UTF8_TO_TCHAR(Buffer);return Result==0;
}

extern "C" {
 int32_t ok_appearance_context_open(const char*,const char*,const char*,const char*,int32_t,void**,char*,int32_t);
 void ok_appearance_context_free(void*);
 void ok_appearance_candidate_free(void*);
 void ok_appearance_checkpoint_free(void*);
 int32_t ok_appearance_prepare(void*,const char*,const char*,void**,char*,int32_t);
 int32_t ok_appearance_candidate_info(const void*,uint8*,int32_t);
 int32_t ok_appearance_candidate_texture(const void*,int32_t,int32_t,uint8*,int32_t);
 int32_t ok_appearance_candidate_card(const void*,uint8*,int32_t);
 int32_t ok_appearance_candidate_commit(void*,void*,OkSession*,int32_t,char*,int32_t);
 int32_t ok_appearance_checkpoint_capture(void*,void**,char*,int32_t);
 int32_t ok_appearance_candidate_checkpoint(void*,const void*,void**,char*,int32_t);
 uint64_t ok_appearance_checkpoint_revision(const void*);
 int32_t ok_appearance_checkpoint_export(const void*,const char*,char*,int32_t);
 void ok_appearance_geometry_free(void*);
 int32_t ok_appearance_prepare_geometry(void*,const char*,const char*,void**,char*,int32_t);
 int32_t ok_appearance_geometry_info(const void*,uint8*,int32_t);
 int32_t ok_appearance_geometry_card(const void*,uint8*,int32_t);
 int32_t ok_appearance_geometry_stage(const void*,OkSession**,char*,int32_t);
 int32_t ok_appearance_geometry_checkpoint(void*,const void*,void**,char*,int32_t);
 int32_t ok_appearance_geometry_commit(void*,void*,OkSession*,int32_t,char*,int32_t);
}
FOkAppearanceContext::~FOkAppearanceContext(){ok_appearance_context_free(Handle);}
FOkAppearanceCandidate::~FOkAppearanceCandidate(){ok_appearance_candidate_free(Handle);}
FOkAppearanceCheckpoint::~FOkAppearanceCheckpoint(){ok_appearance_checkpoint_free(Handle);}
bool FOkAppearanceContext::Open(const FString& Root,const FString& Card,const FString& Resources,const FString& Cache,int32 Outfit,FString& Error){
 if(Handle){Error=TEXT("Appearance context is already open");return false;}
 char Buffer[8192]{};const int32 Result=ok_appearance_context_open(TCHAR_TO_UTF8(*Root),TCHAR_TO_UTF8(*Card),TCHAR_TO_UTF8(*Resources),TCHAR_TO_UTF8(*Cache),Outfit,&Handle,Buffer,UE_ARRAY_COUNT(Buffer));
 Error=UTF8_TO_TCHAR(Buffer);return Result==0;
}
TSharedPtr<FOkAppearanceCandidate,ESPMode::ThreadSafe> FOkAppearanceContext::Prepare(const FString& Target,const FString& Item,FString& Error){
 char Buffer[8192]{};void* Candidate=nullptr;
 const int32 Result=ok_appearance_prepare(Handle,TCHAR_TO_UTF8(*Target),TCHAR_TO_UTF8(*Item),&Candidate,Buffer,UE_ARRAY_COUNT(Buffer));Error=UTF8_TO_TCHAR(Buffer);
 if(Result!=0)return {};
 TSharedPtr<FOkAppearanceCandidate,ESPMode::ThreadSafe> Owned=MakeShareable(new FOkAppearanceCandidate());Owned->Handle=Candidate;return Owned;
}
bool FOkAppearanceCandidate::Info(FString& Json) const{
 Json.Reset();const int32 Count=ok_appearance_candidate_info(Handle,nullptr,0);if(Count<0)return false;
 TArray<uint8> Bytes;Bytes.SetNumUninitialized(Count+1);
 if(ok_appearance_candidate_info(Handle,Bytes.GetData(),Count)!=Count)return false;
 Bytes[Count]=0;Json=UTF8_TO_TCHAR(reinterpret_cast<const char*>(Bytes.GetData()));return true;
}
bool FOkAppearanceCandidate::CardMetadata(TArray<uint8>& Bytes) const {
 Bytes.Reset();const int32 Count=ok_appearance_candidate_card(Handle,nullptr,0);if(Count<0)return false;
 Bytes.SetNumUninitialized(Count);
 if(ok_appearance_candidate_card(Handle,Bytes.GetData(),Count)==Count)return true;
 Bytes.Reset();return false;
}
bool FOkAppearanceCandidate::Texture(int32 Part,int32 Slot,TArray<uint8>& Bytes) const{
 Bytes.Reset();const int32 Count=ok_appearance_candidate_texture(Handle,Part,Slot,nullptr,0);if(Count<0)return false;
 if(!Count)return true;Bytes.SetNumUninitialized(Count);
 if(ok_appearance_candidate_texture(Handle,Part,Slot,Bytes.GetData(),Count)==Count)return true;
 Bytes.Reset();return false;
}
bool FOkAppearanceContext::Commit(FOkAppearanceCandidate& Candidate,FOkSession& Session,bool Apply,FString& Error){
 check(IsInGameThread());char Buffer[8192]{};
 const int32 Result=ok_appearance_candidate_commit(Handle,Candidate.Handle,Session.Handle,Apply?1:0,Buffer,UE_ARRAY_COUNT(Buffer));Error=UTF8_TO_TCHAR(Buffer);
 if(Result==0&&Apply)Session.RuntimeMaterials.Reset();return Result==0;
}
TSharedPtr<FOkAppearanceCheckpoint,ESPMode::ThreadSafe> FOkAppearanceContext::Capture(FString& Error){
 char Buffer[8192]{};void* Checkpoint=nullptr;
 const int32 Result=ok_appearance_checkpoint_capture(Handle,&Checkpoint,Buffer,UE_ARRAY_COUNT(Buffer));Error=UTF8_TO_TCHAR(Buffer);
 if(Result!=0)return {};
 TSharedPtr<FOkAppearanceCheckpoint,ESPMode::ThreadSafe> Owned=MakeShareable(new FOkAppearanceCheckpoint());Owned->Handle=Checkpoint;return Owned;
}
uint64 FOkAppearanceCheckpoint::Revision() const{return ok_appearance_checkpoint_revision(Handle);}
TSharedPtr<FOkAppearanceCheckpoint,ESPMode::ThreadSafe> FOkAppearanceContext::Capture(FOkAppearanceCandidate& Candidate,FString& Error){
 char Buffer[8192]{};void* Checkpoint=nullptr;
 const int32 Result=ok_appearance_candidate_checkpoint(Handle,Candidate.Handle,&Checkpoint,Buffer,UE_ARRAY_COUNT(Buffer));Error=UTF8_TO_TCHAR(Buffer);
 if(Result!=0)return {};
 TSharedPtr<FOkAppearanceCheckpoint,ESPMode::ThreadSafe> Owned=MakeShareable(new FOkAppearanceCheckpoint());Owned->Handle=Checkpoint;return Owned;
}
bool FOkAppearanceCheckpoint::Export(const FString& Path,FString& Error) const{
 char Buffer[8192]{};const int32 Result=ok_appearance_checkpoint_export(Handle,TCHAR_TO_UTF8(*Path),Buffer,UE_ARRAY_COUNT(Buffer));Error=UTF8_TO_TCHAR(Buffer);return Result==0;
}
FOkAppearanceGeometryCandidate::~FOkAppearanceGeometryCandidate(){ok_appearance_geometry_free(Handle);}
TSharedPtr<FOkAppearanceGeometryCandidate,ESPMode::ThreadSafe> FOkAppearanceContext::PrepareGeometry(const FString& Target,const FString& Item,FString& Error){
 char Buffer[8192]{};void* Candidate=nullptr;
 const int32 Result=ok_appearance_prepare_geometry(Handle,TCHAR_TO_UTF8(*Target),TCHAR_TO_UTF8(*Item),&Candidate,Buffer,UE_ARRAY_COUNT(Buffer));Error=UTF8_TO_TCHAR(Buffer);
 if(Result!=0)return {};
 TSharedPtr<FOkAppearanceGeometryCandidate,ESPMode::ThreadSafe> Owned=MakeShareable(new FOkAppearanceGeometryCandidate());Owned->Handle=Candidate;return Owned;
}
bool FOkAppearanceGeometryCandidate::Info(FString& Json) const{
 Json.Reset();const int32 Count=ok_appearance_geometry_info(Handle,nullptr,0);if(Count<0)return false;
 TArray<uint8> Bytes;Bytes.SetNumUninitialized(Count+1);
 if(ok_appearance_geometry_info(Handle,Bytes.GetData(),Count)!=Count)return false;
 Bytes[Count]=0;Json=UTF8_TO_TCHAR(reinterpret_cast<const char*>(Bytes.GetData()));return true;
}
bool FOkAppearanceGeometryCandidate::CardMetadata(TArray<uint8>& Bytes) const{
 Bytes.Reset();const int32 Count=ok_appearance_geometry_card(Handle,nullptr,0);if(Count<0)return false;
 Bytes.SetNumUninitialized(Count);
 if(ok_appearance_geometry_card(Handle,Bytes.GetData(),Count)==Count)return true;
 Bytes.Reset();return false;
}
bool FOkAppearanceContext::StageGeometry(const FOkAppearanceGeometryCandidate& Candidate,FOkSession& Destination,FString& Error){
 char Buffer[8192]{};OkSession* Stage=nullptr;
 const int32 Result=ok_appearance_geometry_stage(Candidate.Handle,&Stage,Buffer,UE_ARRAY_COUNT(Buffer));Error=UTF8_TO_TCHAR(Buffer);
 if(Result!=0||!Stage)return false;
 // The clone is independently owned by Destination's destructor.
 Destination.Close();Destination.Handle=Stage;Destination.bPortable=true;Destination.bSharedAnimation=false;Destination.ApplyPhysXSecondary();return true;
}
TSharedPtr<FOkAppearanceCheckpoint,ESPMode::ThreadSafe> FOkAppearanceContext::Capture(FOkAppearanceGeometryCandidate& Candidate,FString& Error){
 char Buffer[8192]{};void* Checkpoint=nullptr;
 const int32 Result=ok_appearance_geometry_checkpoint(Handle,Candidate.Handle,&Checkpoint,Buffer,UE_ARRAY_COUNT(Buffer));Error=UTF8_TO_TCHAR(Buffer);
 if(Result!=0)return {};
 TSharedPtr<FOkAppearanceCheckpoint,ESPMode::ThreadSafe> Owned=MakeShareable(new FOkAppearanceCheckpoint());Owned->Handle=Checkpoint;return Owned;
}
bool FOkAppearanceContext::CommitGeometry(FOkAppearanceGeometryCandidate& Candidate,FOkSession& Session,bool Apply,FString& Error){
 check(IsInGameThread());char Buffer[8192]{};
 const int32 Result=ok_appearance_geometry_commit(Handle,Candidate.Handle,Session.Handle,Apply?1:0,Buffer,UE_ARRAY_COUNT(Buffer));Error=UTF8_TO_TCHAR(Buffer);
 if(Result==0&&Apply)Session.RuntimeMaterials.Reset();return Result==0;
}
