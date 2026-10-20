-- Windows OCR (Windows.Media.Ocr, zh-Hant-TW + zh-Hans-CN) over localized street logos, through `ok-luakit ocr`.
-- Request: JSON list [{id, path, ...}]; output: [{id, path, texts: {"zh-Hant-TW": ..., "zh-Hans-CN": ...}}].
--   Tools\lua\lua.exe Tools\ocr_street_city_logos.lua --request <ocr-request.json> --output <ocr-output.json>
local ok = require "ok"
local args = ok.args.parser("OCR localized street logos")
  :opt("--request", { required = true }):opt("--output", { required = true }):opt("--lang", { default = "zh-Hant-TW,zh-Hans-CN" })
  :parse(arg)
local r = ok.kit.call({ "ocr", ok.path.abs(args.request), ok.path.abs(args.output), "--lang", args.lang })
print(("OCR %d images"):format(r.rows))
