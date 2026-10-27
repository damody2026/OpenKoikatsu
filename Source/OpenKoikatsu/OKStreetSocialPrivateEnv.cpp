#include "OKStreetSocialPrivateEnv.h"

#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <windows.h>
#include "Windows/HideWindowsPlatformTypes.h"
#endif

// The one-on-one scene's high models open their sessions with the live renderer's extraction switches, written into the
// process environment (OKPrepareLiveGpuEnv: OK_GPU_SKIN, OK_SKELMESH, OK_NOPOSE, OK_UE_ANIM, OK_HEAD_GPUSKIN; the card
// root OK_GAME_ROOT) and never put back. The street's crowd reads the same environment whenever it opens its characters
// again (a load or a new game restarts the map): every heroine, not only the one in the scene, came back built the other
// way (hair flung out as black spikes, sleeves stretched into strips, clothes missing). What was there before the scene
// is put back when it ends; the crowd is suspended and the high models are gone by then, so nothing reads it meanwhile.
namespace
{
	const TCHAR* const GKeys[] = {TEXT("OK_GPU_SKIN"), TEXT("OK_SKELMESH"), TEXT("OK_NOPOSE"), TEXT("OK_UE_ANIM"), TEXT("OK_HEAD_GPUSKIN"), TEXT("OK_CPU_SKIN"), TEXT("OK_GAME_ROOT")};
	TArray<TOptional<FString>> GSaved;
	bool GHeld = false;
}

void OKStreetPrivateEnvSave()
{
#if PLATFORM_WINDOWS
	if (GHeld) return; // the first one is what the street had
	GSaved.Reset();
	for (const TCHAR* Key : GKeys)
	{
		const DWORD Size = ::GetEnvironmentVariableW(Key, nullptr, 0);
		if (Size == 0) { GSaved.Add(TOptional<FString>()); continue; }
		TArray<WCHAR> Buffer;
		Buffer.SetNumZeroed(Size);
		::GetEnvironmentVariableW(Key, Buffer.GetData(), Size);
		GSaved.Add(FString(Buffer.GetData()));
	}
	GHeld = true;
#endif
}

void OKStreetPrivateEnvRestore()
{
#if PLATFORM_WINDOWS
	if (!GHeld) return;
	int32 Changed = 0;
	for (int32 I = 0; I < UE_ARRAY_COUNT(GKeys) && I < GSaved.Num(); ++I)
	{
		const DWORD Size = ::GetEnvironmentVariableW(GKeys[I], nullptr, 0);
		TArray<WCHAR> Buffer;
		Buffer.SetNumZeroed(FMath::Max<DWORD>(Size, 1));
		if (Size) ::GetEnvironmentVariableW(GKeys[I], Buffer.GetData(), Size);
		const TOptional<FString> Now = Size ? TOptional<FString>(FString(Buffer.GetData())) : TOptional<FString>();
		if (Now == GSaved[I]) continue;
		::SetEnvironmentVariableW(GKeys[I], GSaved[I].IsSet() ? *GSaved[I].GetValue() : nullptr);
		++Changed;
	}
	GHeld = false;
	UE_LOG(LogTemp, Display, TEXT("Street private: extraction environment put back (%d switches)"), Changed);
#endif
}
