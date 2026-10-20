// ok_mind.dll: the StreetSocial heroine mind, loaded in-process by Unreal (LoadLibrary / GetProcAddress).
// All strings are UTF-8 JSON. Every returned string must be released with okm_free.
// No call ever unwinds into the caller: Rust panics are caught and reported through okm_last_error.
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct okm_handle okm_handle;

// ABI version; Unreal refuses a DLL whose version differs from the one it was built against.
#define OKM_ABI_VERSION 1
uint32_t okm_abi_version(void);

// config_json: {"save_dir": "...", "deepseek_settings": "...", "model": "...", "workers": 2, "per_minute": 30, "debug": false}
// Returns NULL on failure (see okm_last_error).
okm_handle* okm_open(const char* config_json);

// Queues an asynchronous job: {"kind": "ping"|"chat", "priority": "dialogue"|..., ...}. Returns the job id, 0 on failure.
uint64_t okm_submit(okm_handle* handle, const char* job_json);

// Synchronous, in-memory read: {"kind": "status"}. Returns NULL on failure.
const char* okm_query(okm_handle* handle, const char* query_json);

// Finished jobs as a JSON array, or NULL when there is nothing new. Call from the game thread every tick.
const char* okm_poll(okm_handle* handle);

void okm_free(const char* text);

// Last error on the calling thread, or NULL. Release with okm_free.
const char* okm_last_error(void);

// Writes the mind state to save_dir atomically. Returns 1 on success.
int32_t okm_save(okm_handle* handle);

// Stops the workers (in-flight requests are abandoned), saves, and frees the handle.
void okm_close(okm_handle* handle);

#ifdef __cplusplus
}
#endif
