# ok-luakit

Rust helper for the Lua scripts (`Tools/lua/ok/kit.lua`, `ok/image.lua`).
Each command prints one JSON value on stdout; failures print to stderr and exit 1.
`-` as an input path reads stdin.

```
cargo build --release --manifest-path Tools/ok-luakit/Cargo.toml
```

| Command | Result |
|---|---|
| `hash sha256\|md5 <file>` | `{digest}` |
| `hash-many sha256\|md5 <list>` | digests of the files listed one per line (`-` = stdin), same order; `null` if unreadable |
| `zstd-d <in> <out>` / `zstd-c <in> <out> [level]` | `{bytes}` |
| `lz4-block-d <in> <out> <size>` / `lz4-block-c <in> <out>` | `{bytes}` |
| `zlib-d`/`zlib-c [level]`, `gzip-d`/`gzip-c [level]` `<in> <out>` | `{bytes}` |
| `msgpack-json <in>` | decoded value; binary -> `{"$bytes": hex}`; several top-level values -> array |
| `json-msgpack <in.json> <out>` | `{bytes}` |
| `image-info <file>` | `{width,height,color,format}` |
| `image-decode <file> <raw_out> [RGBA\|RGB\|LA\|L]` | raw 8-bit pixels (PIL `convert` semantics), `{width,height,mode,source_mode}` |
| `image-encode <raw> <w> <h> <mode> <out>` | png/jpg/webp/bmp by extension |
| `image-convert <raw> <w> <h> <from> <to> <raw_out>` | PIL-identical L luma |
| `image-stats <file> [--box l,t,r,b] [--alpha-min N]` | `{count,mean,std,min,max,opaque,transparent}` RGBA |
| `image-diff <a> <b> [--threshold N] [--output png] [--box ...]` | `{max,channel_max,mean,rms,differing,total}` |
| `image-resize <src> <dst> <w> <h> [nearest\|triangle\|catmullrom\|gaussian\|lanczos3]` | |
| `image-compose <spec.json> <dst>` | contact sheets: `{width,height,background,items:[{file,x,y,w?,h?}]}` |
| `text-render <spec.json>` | TrueType text to PNG, PIL-style anchors |
| `unity-dump <bundle> [--type T] [--name N] [--path-id ID] [--bytes-dir D] [--no-tree]` | UnityPy `objects` + `read_typetree()` (byte arrays as `$bytes`/`$file`) |
| `hardlink <existing> <new>` | `{linked}` or `{linked:false, os_error, message}`; never replaces (183 = exists, 17 = other volume) |
| `window-noactivate <pid>` | `{windows}`: restores that process's visible top-level windows without activating them and moves them to the bottom of the z-order (`pair_ctl.lua start --background`) |
| `ocr <request.json> <output.json> [--lang zh-Hant-TW,zh-Hans-CN]` | `{rows}`: Windows.Media.Ocr per image of request `[{id, path}]`; writes `[{id, path, texts: {<lang>: text}}]` (`ocr_street_city_logos.lua`) |
| `file-id <path>` | `{volume, index, links, size, mtime_ns}` (st_dev/st_ino/st_nlink/st_size/st_mtime_ns) |
| `disk-free <path>` | `{total, free, available}` like `shutil.disk_usage` |
| `zip-read <zip> [--suffix S]` | `{names, texts:[{name,text}]}`; zipfile names, members ending in S inflated as UTF-8 (replace) |
| `zip-read-many <list.txt\|-> [--suffix S]` | array of zip-read results (`zip` key) or `{zip, error}` per archive |
| `proc-times` | `[{pid, name, cpu_seconds}]` for every process that can be opened (`Get-Process` `CPU`; name without `.exe`) |
| `proc-wait <pid> <ms>` | `{exists, exited, path}`: waits up to ms for a process the caller did not start |
| `proc-priority <pid> [class]` | `{priority}`; sets `Idle\|BelowNormal\|Normal\|AboveNormal\|High\|RealTime` when given |
| `proc-affinity <pid> [mask]` | `{mask}`; sets the process affinity mask when given |
| `cpu-cache` | `[{CacheBytes, Mask, Group, LogicalProcessors}]`: L3 caches with a single group mask (crowd `LargestL3` affinity) |
| `font-metrics <font> [index]` | `{units_per_em, ascent, descent, line_gap, height}`; PIL size → text-render size: `size * height / units_per_em` |
| `u8-stats <raw\|->` | `{count, sum, mean, std, min, max}` of a uint8 buffer with numpy `mean()`/`std()` rounding |

`unity-dump` writes non-finite floats as `{"$float": "Infinity"|"-Infinity"|"NaN"}`;
`ok.kit.call` turns them back into Lua `inf`/`nan`, so JSON re-encodes like Python's
`Infinity`/`NaN`.

### Shader / animation export helpers (`src/shader.rs`)

| Command | Result |
|---|---|
| `zip-member <zip> <member> <out>` | `{bytes}`; `ZipFile(zip).read(member)` (UTF-8 flag / cp437 names, stored/deflate), reads only the directory and the member |
| `zip-names <list\|->` | `[{path, names}\|{path, error}]`: `namelist()` of one zip per input line (tail + central directory only) |
| `unity-fields <file> --type T --fields a,b.c[,x#len] [--name N]` | `[{path_id,class_id,type,name,tree}]` like `unity-dump` but the tree holds only the listed dotted paths; `x#len` stores `len(x)` instead of the value (keeps AnimationClip curve data out of the JSON) |
| `unity-raw <file> --path-id ID <out>` | `{bytes, class_id, unity_version}`: the object's serialized bytes (e.g. TextAsset scripts that are not UTF-8) |
| `unity-stream <bundle> <stream path> <offset> <size> <out>` | `{bytes}`: `m_StreamData` payload from the bundle's `.resS` member |
| `unity-version <file>` | `{unity_version}` of the first serialized file (UnityPy `object_reader.version`) |

### `probe` (GamePoseProbe verifiers, `src/probe.rs`)

Whole-image numeric passes that reproduce the original PIL/numpy results
bit-for-bit (verified against Pillow 12 / numpy 2.5 with OpenBLAS 0.3.33).

| Command | Result |
|---|---|
| `probe contact-sheet <spec\|->` | PIL contact sheet: `{width,height,background,output,items:[{file,x,y,w,h}],texts:[{x,y,text,fill}]}`; thumbnails = `convert("RGB").resize(LANCZOS)` (Pillow Resample.c port), labels = `ImageDraw.text` with Pillow's default font (glyph masks in `assets/pil-default-font-10.json`, ASCII exact; other chars approximate via `assets/aileron-regular-pil.ttf`). Pixels match PIL; PNG bytes do not (Pillow uses zlib-ng) |
| `probe fg-bounds <png> <t>` | `{count,bounds:[xmin,ymin,xmax,ymax]\|null}` of `max\|rgb - rgb[0,0]\| > t` |
| `probe diff-count <a> <b> <t>` | `{count,max}` of pixels with `max\|a - b\| > t` (RGB) |
| `probe content-presence <png>` | `{foreground_fraction, center_foreground_fraction}` (corner-median background, threshold 8) |
| `probe mean-abs-diff <a> <b>` | `{mean}` = `abs(a.astype(float) - b).mean()` over RGB |
| `probe linalg <ops\|->` | `{"ops":[{op:"inv",a}\|{op:"matmul",a,b}\|{op:"pow",a:[..],b}]}` -> results; numpy `linalg.inv` / `@` with OpenBLAS operation order and FMA, `x ** b` through the CRT `pow` |
| `probe tex-sample <spec\|->` | float64 RGBA/255 texture (ops: `{add,channels}` clip, `{srgb:true}` decode with pow 2.4), bilinear `accumulate` (clip/repeat) or `lerp` (wrap 0/1/2) kernel at `uv` or `uv_tsv` (np.loadtxt skiprows=1 cols 1:3); `{samples, uv?}`. Without `png`: `{uv}` only |
| `probe tex-diff <spec\|->` | `{max}` of `abs(a[:,:,:3] - b[:,:,:3])` for two tex-sample image specs |
| `probe alpha-flip <target> <source>` | brow alpha `\|t - s\|` and `\|t - s[::-1]\|`: mean, max, count > 16 |
| `probe rgba-max-diff <a> <b> [--mask m]` | `{max,count}` of int RGBA `\|a - b\|` (only where `m` alpha == 255) |

## Market data (`src/market.rs`)

Helpers for the StreetSocial market-data scripts (`Tools/build_ashare_data.lua`, `build_us_*.lua`,
`build_market_sqlite.lua`, `build_stock_names.lua`, `verify_*_data.lua`).

| Command | Result |
|---|---|
| `sqlite <db> <spec\|->` | runs `{"readonly","timeout","ops":[...]}` on one connection (autocommit unless ops BEGIN/COMMIT): `{sql,params}` -> `{changes}`, `{sql,many}` (executemany), `{sql,params,query:true[,out]}` -> `{rows}` (or JSON lines to `out`), `{script}` (executescript), `{okbar,sid\|market+code}` / `{okfx,pair}` -> DELETE + INSERT bars / fx (split/div `round(.,6)` like build_market_sqlite.py). One result per op. Bundled SQLite |
| `okbar-dump <file>` | `{rows:[[d,o,h,l,c,pc,vol,amount,split,div,lu,ld,flags]]}` |
| `zstd-c-sized <in> <out> [level]` | single frame with content size = python-zstandard `ZstdCompressor(level).compress()` (same libzstd 1.5.7 -> same bytes) |
| `zstd-c-batch <spec\|->` | `{"level","files":[{in,out}]}` -> `[{bytes,sha256}]`, same frames as zstd-c-sized, multi-threaded |
| `zstd-batch <spec\|->` | `{"files":[{in,out}]}` -> `[{bytes,sha256,content_size,size,eof,trailing,error?}]`; first frame decoded to `out` |
| `zhconv <conversion> <in\|-> [--lines]` | OpenCC conversion (`s2tw`, `s2t`, `t2s`, ...) as done by opencc-python-reimplemented 0.1.7 (its dictionaries are in `data/opencc/`, Apache-2.0): `{text}` or `{lines}` |
| `pdf-text <pdf> [--pages N]` | `{pages:[text]}` via pdf-extract (pypdf's text differs at most in line breaks) |
| `text-decode <encoding> <in\|->` | `{text}` (WHATWG labels: gbk, gb18030, big5, ...; bad bytes -> U+FFFD) |
| `baostock <jobs\|-> [--workers N] [--label L]` | BaoStock TCP client (baostock 0.9.x protocol): `{"jobs":[{fn,kw,out}]}` with fn = `query_trade_dates`, `query_stock_basic`, `query_history_k_data_plus`, `query_dividend_data`; one session per worker thread, `BS.query` retry/relogin rules of build_ashare_data.py; each job writes `{fields,rows}` or `{error}` to `out`; progress on stderr; `{done,errors,aborted}` |
