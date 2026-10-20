-- Regression tests for the ok library and okfs.dll.
--   Tools\lua\lua.exe Tools\lua\tests\test_ok.lua [name-filter]
-- fixtures/python_golden.json holds CPython reference output (generator kept in
-- old_py/Tools/lua/tests/make_golden.py).
local ok = require "ok"
local json, util, path = ok.json, ok.util, ok.path
local T = ok.test.suite("ok library")
local golden = ok.read_json(ok.script_dir .. "/fixtures/python_golden.json")

local function bytes(hex) return util.unhex(hex) end

T:case("json dumps matches CPython for every option set", function(t)
  local opts = {
    default = {}, indent2 = { indent = 2 },
    sorted_noascii = { ensure_ascii = false, indent = 1, sort_keys = true },
    compact = { separators = { ",", ":" }, ensure_ascii = false },
  }
  for name, o in pairs(opts) do t:eq(json.encode(golden.doc, o), golden.dumps[name], name) end
end)

T:case("json decode keeps order, -0.0, int/float subtypes", function(t)
  local v = json.decode('{"b":1,"a":-0.0,"c":1.0,"d":[],"e":{}}')
  t:eq(json.keys(v), { "b", "a", "c", "d", "e" })
  t:eq(math.type(v.b), "integer"); t:eq(math.type(v.c), "float")
  t:eq(json.encode(v), '{"b": 1, "a": -0.0, "c": 1.0, "d": [], "e": {}}')
  v.a = nil; v.a = 2
  t:eq(json.encode(v), '{"b": 1, "c": 1.0, "d": [], "e": {}, "a": 2}')
end)

T:case("float repr matches CPython repr", function(t)
  for _, r in ipairs(golden["repr"]) do t:eq(json.float_repr(r.x), r.s) end
end)

T:case("csv.parse matches csv.reader (fuzz)", function(t)
  for _, c in ipairs(golden.csv) do
    t:eq(ok.csv.parse(c.text, { delimiter = c.delim }), c.rows, json.encode(c.text))
  end
end)

T:case("csv writer matches csv.writer", function(t)
  local w = ok.csv.writer()
  w:row({ "a", "b,c", 'd"e', 1.5, nil, "x\ny", true, n = 7 })
  t:eq(w:text(), golden.csv_row)
end)

T:case("csv dict readers skip empty rows", function(t)
  local d = ok.csv.parse_dicts("\r\na,b\r\n\r\n1,2\r\n")
  t:eq(d.fields, { "a", "b" }); t:eq(#d, 1); t:eq(d[1].b, "2")
end)

T:case("hashes and base64 match hashlib/zlib/base64", function(t)
  for _, h in ipairs(golden.hashes) do
    local b = bytes(h.hex)
    t:eq(ok.hash.sha256(b), h.sha256); t:eq(ok.hash.md5(b), h.md5)
    t:eq(ok.hash.sha1(b), h.sha1); t:eq(ok.hash.crc32(b), h.crc32)
    t:eq(util.b64encode(b), h.b64); t:eq(util.b64decode(h.b64), b)
  end
end)

T:case("round is banker's rounding like CPython", function(t)
  for _, r in ipairs(golden.round) do
    if r.n == json.null then r.n = nil end
    local got = r.n == nil and util.round(r.x) or util.round(r.x, r.n)
    t:eq(got, r.r, ("round(%s, %s)"):format(util.num(r.x), tostring(r.n)))
    t:eq(math.type(got), math.type(r.r))
  end
end)

T:case("stable sort and python compare", function(t)
  local s = util.sorted({ { 2, "a" }, { 1, "b" }, { 2, "c" }, { 1, "d" } }, function(v) return v[1] end)
  t:eq(s, { { 1, "b" }, { 1, "d" }, { 2, "a" }, { 2, "c" } })
  t:eq(util.sorted({ 3, 1, 2 }, nil, true), { 3, 2, 1 })
end)

T:case("utf-8 paths: write, read, rename, glob, remove", function(t)
  local dir = path.temp("_中文dir")
  path.mkdir(dir .. "/子目錄")
  local f = dir .. "/子目錄/檔案 😀.txt"
  path.write_text(f, "一\n二\n")
  t:eq(path.read_bytes(f), "一\r\n二\r\n")
  t:eq(path.read_text(f), "一\n二\n")
  t:eq(path.read_lines(f), { "一", "二" })
  path.write_text(f, "x\n", { newline = "\n", atomic = true })
  t:eq(path.read_bytes(f), "x\n")
  t:eq(#path.rglob(dir, "*.txt"), 1)
  t:eq(path.name(path.glob(dir, "*/*.txt")[1]), "檔案 😀.txt")
  path.rmtree(dir)
  t:falsy(path.exists(dir))
end)

T:case("path helpers mirror pathlib", function(t)
  t:eq(path.suffix("a/b.tar.gz"), ".gz"); t:eq(path.stem("a/b.tar.gz"), "b.tar")
  t:eq(path.with_suffix("a/b.txt", ".json"), "a/b.json")
  t:eq(path.parent("C:/x/y"), "C:/x"); t:eq(path.parent("C:/x"), "C:/")
  t:eq(path.join("a", "b", "/c"), "/c"); t:eq(path.rel("C:/x/y/z", "C:/x"), "y/z")
  t:truthy(path.fnmatch("Foo.PNG", "*.png"))
end)

T:case("proc.run passes arguments literally without cmd.exe", function(t)
  local b = string.char(92)
  local tricky = 'a&b "q" %PATH% ' .. b .. b .. "srv" .. b .. "x" .. b
  local child = path.temp(".lua")
  path.write_text(child, 'require "ok"\nio.write(arg[1], "|", arg[2]); io.stderr:write("E"); os.exit(3)\n')
  local r = ok.proc.lua(child, { tricky, "中文 空格😀" }, { capture = true })
  path.remove(child)
  t:eq(r.code, 3); t:eq(r.stdout, tricky .. "|中文 空格😀"); t:eq(r.stderr, "E")
  t:eq(ok.proc.run("exit /b 4", { capture = true }).code, 4)
  local missing = ok.proc.run({ "definitely-not-a-command-xyz" }, { capture = true })
  t:eq(missing.code, -1)
end)

T:case("proc.popen interactive pipes with timeout", function(t)
  local child = path.temp(".lua")
  path.write_text(child, 'require "ok"\nwhile true do local l = io.read("l"); if not l or l == "quit" then break end; io.write(l:upper(), "\\n"); io.stdout:flush() end\nos.exit(5)\n')
  local p = ok.proc.popen({ ok.lua_exe, child }, { stderr = "stdout" })
  p:write("hi 中文\n"); t:eq(p:read_line(10), "HI 中文")
  local line, err = p:read_line(0.2); t:is_nil(line); t:eq(err, "timeout")
  p:write("quit\n"); t:eq(select(2, p:read_line(10)), "eof"); t:eq(p:wait(10), 5)
  path.remove(child)
end)

T:case("args parser handles options, flags, append, positionals", function(t)
  local a = ok.args.parser("t"):arg("input"):arg("rest", { nargs = "*" })
    :opt("--count", { type = "int", default = 3 }):flag("--verbose"):opt("--tag", { multiple = true })
    :parse({ "x.json", "--count", "5", "--tag", "a", "--tag=b", "--verbose", "y", "z" })
  t:eq(a.input, "x.json"); t:eq(a.count, 5); t:eq(a.verbose, true); t:eq(a.tag, { "a", "b" }); t:eq(a.rest, { "y", "z" })
end)

T:case("is_main is false for required modules", function(t)
  local dir = path.temp("_mod")
  path.mkdir(dir)
  path.write_text(dir .. "/m_x.lua", 'local ok = require "ok"\nreturn {main = ok.is_main()}\n')
  package.path = dir .. "/?.lua;" .. package.path
  t:eq(require("m_x").main, false)
  path.rmtree(dir)
end)

T:case("luakit roundtrips: zstd, msgpack, image", function(t)
  local data = ("abc中文"):rep(1000)
  t:eq(ok.kit.zstd_decompress_bytes(ok.kit.zstd_compress_bytes(data)), data)
  local mp = path.temp(".mp")
  ok.kit.msgpack_save(mp, json.object({ { "a", 1 }, { "b", { 1.5, "x" } } }))
  local v = ok.kit.msgpack_load(mp); path.remove(mp)
  t:eq(v.a, 1); t:eq(v.b[2], "x")
  local img = ok.image.new(3, 2, "RGBA", { 10, 20, 30, 255 })
  img:set(2, 1, 1, 2, 3, 4)
  local png = path.temp(".png"); img:save(png)
  local back = ok.image.load(png); path.remove(png)
  t:eq({ back:get(2, 1) }, { 1, 2, 3, 4 }); t:eq({ back:get(0, 0) }, { 10, 20, 30, 255 })
  t:eq({ back:convert("L"):get(0, 0) }, { 18 })
end)

T:run()
