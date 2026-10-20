// ok_rules.dll: the StreetSocial game rules, loaded in-process by Unreal (LoadLibrary / GetProcAddress).
// Stateless: Unreal owns the save and passes the facts each call; the rules answer with what changes.
// All strings are UTF-8 JSON. Every returned string must be released with okr_free.
// No call ever unwinds into the caller: Rust panics are caught and reported through okr_last_error.
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct okr_handle okr_handle;

// ABI version; Unreal refuses a DLL whose version differs from the one it was built against.
#define OKR_ABI_VERSION 1
uint32_t okr_abi_version(void);

// config_json: {"tuning_dir": "..."}; tuning files missing there fall back to the copies built into the DLL.
// Returns NULL on failure (see okr_last_error).
okr_handle* okr_open(const char* config_json);

// request_json: {"rule": ..., ...} ("kind" is accepted when "rule" is absent; with "rule" set, "kind" is plain data): "status", "bonds_defaults" | "bonds_tick" | "bond_event" | "bond_mood" (src/bonds.rs),
// "needs_defaults" | "needs_tick" (src/needs.rs), "debug_panic" (tests). Returns the answer as JSON, NULL on failure.
const char* okr_eval(okr_handle* handle, const char* request_json);

// Re-reads the tuning files. Returns 1 on success; on failure the previous tuning stays.
int32_t okr_reload(okr_handle* handle);

void okr_free(const char* text);

// Last error on the calling thread, or NULL. Release with okr_free.
const char* okr_last_error(void);

void okr_close(okr_handle* handle);

#ifdef __cplusplus
}
#endif
