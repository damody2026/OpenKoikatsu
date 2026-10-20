# Project scripts in Lua

All project scripts run with the vendored interpreter:

```
Tools\lua\lua.exe Tools\some_script.lua [args]
```

Python stays only where a host forces it: scripts executed inside Blender
(`bpy`) or the Unreal editor (`unreal`), the `street_city_*` library those
scripts import (single source, no Lua twin), the scripts that depend on that
library, GVHMR (`Tools/video_motion`) and ML inference references
(`Tools/ok-tts/reference`). Everything else is Lua. Game rules stay in Rust
(`Tools/ok-rules`, hot reload).

PowerShell is retired the same way: launchers, build and stage scripts are Lua,
`.cmd`/`.bat` files call `Tools\lua\lua.exe`, and the engine root comes from
`ok.engine_root()` (in `.cmd`: the last line of `Tools\ok_engine.lua`, read with
``for /f "usebackq delims=" %%E in (`call "%~dp0Tools\lua\lua.exe" "%~dp0Tools\ok_engine.lua"`)``;
the leading `call` keeps cmd from stripping the quotes). The only `.ps1` left is
`three/scripts/demo/server.ps1`, shipped in the portable demo ZIP for machines
without this toolchain. Retired scripts keep their path under `old_py/` and `old_ps1/`.

## Layout

| Path | What |
|---|---|
| `lua.exe`, `lua54.dll` | LuaBinaries 5.4.8 (msvcrt) |
| `lfs.dll` | LuaFileSystem, UTF-8 paths |
| `okfs.dll` (`src/okfs.c`) | UTF-8 open/popen/rename/remove/env, CreateProcessW runner, clock, sleep |
| `ok/` | the `ok` library (below) |
| `../ok-luakit/` | Rust CLI for heavy work (images, Unity bundles, codecs) |

Rebuild `okfs.dll`: `Tools\lua\lua.exe Tools\lua\src\build_okfs.lua --lua-src <lua-5.4.8\src>` (MinGW GCC).
After changing `ok/` or `okfs.c`, run `Tools\lua\lua.exe Tools\lua\tests\test_ok.lua`
(compares against CPython reference output in `tests/fixtures/python_golden.json`).
Static check for any script change: `Tools\lua\lua.exe Tools\lua\lint.lua [files]` —
vendored luacheck (syntax, undefined variables, accidental globals) plus a check
that every `ok.<module>.<name>` used actually exists.
Build luakit: `cargo build --release --manifest-path Tools/ok-luakit/Cargo.toml`.

## The `ok` library

```lua
local ok = require "ok"      -- always first; patches io/os for UTF-8 paths
```

Loading `ok` makes `io.open`, `io.lines`, `io.popen`, `os.remove`, `os.rename`,
`os.execute`, `os.getenv` and `arg` UTF-8 safe, switches the console to UTF-8,
and puts the script's own folder and `Tools/` on `package.path`, so
`require "street_social_common"` finds `Tools/street_social_common.lua`.

| Module | Replaces |
|---|---|
| `ok.root`, `ok.script_dir`, `ok.lua_exe` | `Path(__file__).resolve().parents[n]` |
| `ok.is_main()` | `if __name__ == "__main__":` |
| `ok.game_root([soft, sep])`, `ok.game_path(p)` | Koikatsu Sunshine install (`OK_GAME_ROOT`, else probed: the old `C:/portable/KoikatuSunshine` only keeps BepInEx, the game now lives on `D:`). Never hard-code the install path. |
| `ok.main(fn)` | run with traceback + exit code |
| `ok.json` | `json` — ordered objects, Python float repr, `indent`, `sort_keys`, `ensure_ascii` (default true like Python), `separators` |
| `ok.path` | `pathlib`, `os.path`, `shutil`, `glob` |
| `ok.proc` | `subprocess` (no cmd.exe for argument lists) |
| `ok.args` | `argparse`, `sys.argv` checks |
| `ok.csv` | `csv` (excel dialect, `\r\n` rows) |
| `ok.hash` | `hashlib` sha256/md5/sha1, `zlib.crc32`; `files(list)` digests many files in one luakit call |
| `ok.time` | `time`, `datetime` basics |
| `ok.util` | `round` (banker's), `sorted` (stable), `repr/str` of numbers, string helpers, base64/hex |
| `ok.test` | `unittest` |
| `ok.difflib` | `difflib` (SequenceMatcher, unified_diff, ndiff) |
| `ok.image` | `PIL.Image` (load/get/set/crop/convert/save, stats/diff/resize in Rust) |
| `ok.kit` | `UnityPy`, `zstandard` (`zstd_compress_sized_bytes` = `ZstdCompressor().compress`), `lz4`, `zlib`, `gzip`, `msgpack`, `requests` (curl.exe: `http_get`, `http_request` never raises on HTTP status, `http_batch` parallel GETs), `sqlite3` (`sqlite`, `sql_rows`), `opencc` (`zhconv`), `pypdf` text (`pdf_text_pages`) |
| `ok.re` | `re` (compile/search/match/fullmatch/findall/finditer/sub/split; patterns copied verbatim; `\w`/`\d`/`\s` ASCII, bytes >= 0x80 count as `\w`) |

### JSON rules
- Decoded objects keep key order and re-encode identically.
- Build new objects with `ok.json.object({{"k1", v1}, {"k2", v2}})` (or
  `local o = ok.json.object(); o.k1 = v1`) whenever key order matters —
  plain Lua tables encode with **sorted** keys.
- Empty plain table encodes as `[]`; use `ok.json.object()` for `{}`.
- `ok.json.null` is JSON null; integers vs floats follow Lua subtypes
  (`1` -> `1`, `1.0` -> `1.0`), matching Python output.
- `json.dumps(v, indent=2, ensure_ascii=False)` =
  `ok.json.encode(v, {indent=2, ensure_ascii=false})`.

### Text files
`ok.path.read_text` uses universal newlines and drops a UTF-8 BOM;
`ok.path.write_text` writes `\n` as `\r\n` by default (Python text mode on
Windows) — pass `{newline="\n"}` where the Python code used `newline=""` or
`"\n"`. `write_bytes` writes exactly. `{atomic=true}` writes `.tmp` then
replaces.

### Python semantics that bite
- Lua tables are 1-based; PIL/numpy pixel coordinates in `ok.image` are 0-based.
- `0`, `""` and `{}` are **truthy** in Lua; use `ok.util.truthy(v)` when porting `if v:`.
- `//` and `%` floor like Python; `/` always gives a float.
- `table.sort` is not stable and needs a strict `<`; use `ok.util.sorted(t, key, reverse)`.
- `string.format("%s", 1.0)` prints `1.0` but `tostring(0.1+0.2)` differs from
  Python; use `ok.util.num(x)` for Python `str(float)`.
- `round(x)` in Python is banker's rounding: `ok.util.round`.
- String methods are byte based; for code points use `utf8.*` / `ok.util.ulen`, `ok.util.usub`.
- Exceptions: `error(msg)`/`pcall`; Python `assert c, msg` -> `assert(c, msg)`
  (or `ok.util.check(c, fmt, ...)`). Uncaught errors exit 1 like Python.
- Python 3.12 `sum()` of floats is Neumaier-compensated; use `ok.util.pysum(list)`
  when float sums must match bit for bit (`ok.util.sum` adds naively).
- `ok.is_main()` is false for `require` and for `dofile`/`loadfile` of another script.

### Unity bundles
`ok.kit.unity_objects(path, {type="Shader", name="...", path_id=..., bytes_dir=dir, tree=false})`
returns `{file, path_id, class_id, type, name, container, byte_size, tree}` per object; `tree` equals
UnityPy `read_typetree()` except byte arrays, which arrive as `{"$bytes": hex}`
(or `{"$file": path}` with `bytes_dir`); `ok.kit.bytes(v)` returns the raw string.
Non-finite floats come back as `inf`/`nan`. Class names the Rust table lacks
(e.g. `SkinnedMeshRenderer`, `AnimatorOverrideController`) are mapped from UnityPy's
`ClassIDType`. `{type=T, fields={"m_Name", "a.b", "c#len"}}` returns pruned trees
(only those paths; `#len` = `len()`), which keeps AnimationClip curves out of the JSON.

### Heavy work goes to Rust
Per-pixel loops over whole images, codecs and binary formats belong in
`Tools/ok-luakit` subcommands (one JSON value on stdout). Add a module file
`src/<topic>.rs`, register the command in `src/main.rs`, document it in
`Tools/ok-luakit/README.md`.

## Porting checklist
1. `foo.py` -> `foo.lua` in the same folder, same CLI (arguments, defaults, outputs).
2. Run the Python original and the Lua port on the same inputs; outputs must
   match byte for byte (JSON, CSV, generated code). When inputs no longer exist,
   port carefully and say so.
3. Update every caller: `.cmd`/`.bat`, other Lua scripts (`ok.proc.lua(...)`), Rust/C++ comments such as
   "Generated by Tools/x.py", README/plan docs that tell people how to run it.
4. Leave the original in place until the batch is accepted, then `git mv` it to
   `old_py/<path>` or `old_ps1/<path>` in one reviewed step.

PowerShell specifics: `-Switch` / `-Name value` become `--switch` / `--name value`
(`ok.args`, dashes map to `_`); `$env:X=...; Start-Process` becomes
`ok.proc.spawn(cmd, {env = {X = ...}})` (`false` clears a variable for the child);
`Invoke-WebRequest` becomes `ok.kit.http_get`, `[uri]::EscapeDataString` `ok.kit.urlencode`;
window tricks such as show-without-focus are `ok-luakit` commands (`window-noactivate <pid>`).
