// ok_story.dll: the galgame story director of OK Studio (Docs/GALGAME_DIALOGUE_PLAN.md), loaded in-process by Unreal.
// The director owns the story's progress and every clock, interpolation and random choice; Unreal reports facts
// (frame time, bone positions, voice progress), forwards input and executes the commands it is handed, nothing else.
// All strings are UTF-8 JSON. Every returned string must be released with oks_free.
// No call ever unwinds into the caller: Rust panics are caught and reported through oks_last_error.
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct oks_handle oks_handle;

// ABI version; Unreal refuses a DLL whose version differs from the one it was built against.
#define OKS_ABI_VERSION 1
uint32_t oks_abi_version(void);

// config_json: {"story": path, "data_dir": Content/IndependentResources/Story, "save_dir": UserData/Story,
// "expressions": path of the game's expression table, "lang": "zh"|"ja"|..., "seed": n}.
// Returns NULL on failure (see oks_last_error).
oks_handle* oks_open(const char* config_json);

// One player input event (queued, handled by the next tick): {"type": "key", "key": "Enter", "down": true},
// {"type": "click"}, {"type": "choose", "index": 1}, {"type": "button", "id": "auto"}, {"type": "mouse", "x": .., "y": ..} ...
// Returns 1 when accepted.
int32_t oks_input(oks_handle* handle, const char* input_json);

// Once per frame: facts in, the frame to present (with the commands to execute) out. NULL on failure.
const char* oks_tick(oks_handle* handle, const char* facts_json);

// Result of a command: {"id": n, "ok": true, ...}. Returns 1 when the id was pending.
int32_t oks_ack(oks_handle* handle, const char* result_json);

// Read-only questions: {"q": "state" | "saves" | "backlog" | "validate"}. NULL on failure.
const char* oks_query(oks_handle* handle, const char* query_json);

// The director's state as JSON (hot reload, debugging) and back. oks_import returns 1 on success.
const char* oks_export(oks_handle* handle);
int32_t oks_import(oks_handle* handle, const char* state_json);

// Re-reads the story, tuning and expression files, keeping the position. Returns 1 on success.
int32_t oks_reload(oks_handle* handle);

void oks_free(const char* text);

// Last error on the calling thread, or NULL. Release with oks_free.
const char* oks_last_error(void);

void oks_close(oks_handle* handle);

#ifdef __cplusplus
}
#endif
