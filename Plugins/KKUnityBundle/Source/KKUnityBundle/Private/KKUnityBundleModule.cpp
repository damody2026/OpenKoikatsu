#include "KKUnityBundleModule.h"
#include "KKRustExtract.h"
#include "KKSharedAnimationDriver.h"
#include "KKSharedAnimation.h"
#include "OKPhysXSecondary.h"
#include "ok_extract.h"

void FKKUnityBundleModule::StartupModule()
{
	// Decoded animation cache (ok_extract anim/disk_cache.rs): in this project's Saved folder, never in the resource
	// folders (they may be links to another installation). Read once by the Rust side, before any animation work.
	if (FPlatformMisc::GetEnvironmentVariable(TEXT("OK_ANIM_CACHE_DIR")).IsEmpty())
	{
		FPlatformMisc::SetEnvironmentVar(TEXT("OK_ANIM_CACHE_DIR"), *FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("OkAnimCache")));
	}
	FOkSession::InitBindings();
	// DynamicBone chains of this module's ok_extract copy run on PhysX.
	OKPhysXSecondary::Register(&ok_set_physx_dynamic_solver);
	// Its built-in Verlet follows the same weight/hardness settings as the host solvers.
	OKPhysXSecondary::RegisterTuning(&ok_set_secondary_tuning_source);
 OKInstallNativeSharedAnimationDriver();
}

void FKKUnityBundleModule::ShutdownModule(){OKRegisterSharedAnimationDriver(FKKSharedDriverFactory());FOkSession::WaitForBackgroundCloses();}

IMPLEMENT_MODULE(FKKUnityBundleModule, KKUnityBundle);
