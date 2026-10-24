#pragma once
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

// ok_mind.dll (Tools/ok-mind), the heroine mind, loaded into this process at runtime. When the DLL is missing or
// fails to open, IsOpen() stays false and StreetSocial keeps its old behaviour (plan §3).
class FOKStreetMind {
public:
 ~FOKStreetMind();
 // SaveDir: Saved/StreetSocial/mind. Settings: the FreeTalk deepseek.json. Debug enables the debug_panic job.
 bool Open(const FString& SaveDir, const FString& Settings, bool bDebug, FString& Error, const FString& DataDir = FString());
 void Close();
 bool IsOpen() const { return Handle != nullptr; }
 const FString& DllPath() const { return LoadedFrom; }
 // Returns the job id, 0 on failure.
 uint64 Submit(const FString& JobJson, FString& Error);
 bool Query(const FString& QueryJson, FString& Out, FString& Error);
 // Finished jobs since the last call; call from the game thread.
 void Poll(TArray<TSharedPtr<FJsonObject>>& Out);
 bool Save(FString& Error);
 // Save without waiting for the disk (an older DLL without it saves and waits). Flusher: the DLL's wait for those
 // writes, callable from any thread and without the mind (nullptr from an older DLL, whose SaveQueued already waited).
 bool SaveQueued(FString& Error);
 using FFlush = int32 (*)();
 FFlush Flusher() const;
 // Hot reload, only while no job is queued or running: a newer build of the DLL (Tools/ok-mind/target/release, or
 // OK_MIND_SOURCE_DLL) is loaded from a copy under a fresh name; either way the mind is closed (saved) and opened
 // again with the same configuration, so the settings file (endpoint, model, key) is read anew. The old one comes
 // back when the new one does not open.
 bool Reload(FString& Out, FString& Error);

 // Where the DLL is looked for: OK_MIND_DLL, next to the executable, then the project's Tools/ok-mind build.
 static TArray<FString> Candidates();

 struct FApi;

private:
 FString TakeError();
 bool OpenWith(FApi* NewApi, const FString& Path, FString& Error);
 FApi* Api = nullptr;
 void* Handle = nullptr;
 FString LoadedFrom;
 // What Open was given (Reload opens with the same), and the build Reload watches.
 FString ConfigJson, SourcePath, ReloadDir;
 FDateTime SourceStamp;
};

// A FreeTalk turn waiting for its talk job.
struct FOKStreetMindTalk { int32 Character = INDEX_NONE; FString Id, Text; };

// The running street scene's mind, for the ok.StreetMind console commands; null when none is open.
void OKStreetSetActiveMind(FOKStreetMind* Mind);
