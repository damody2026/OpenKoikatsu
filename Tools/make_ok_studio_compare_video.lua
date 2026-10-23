-- A vertical (1080x1920) comparison short from OKStudio.CompareKStudio pictures: each scene's original (top, the scene
-- card's own picture) over OpenKoikatsu (bottom), with titles, joined by cross-fades.
-- Tools/lua/lua.exe Tools/make_ok_studio_compare_video.lua --dir <compare pictures> --scenes cs0000001,cs0001901,...
--   --out <video.mp4> [--hold 3.0] [--fade 0.5] [--title-seconds 3.0] [--font C:/Windows/Fonts/NotoSansCJKsc-Bold.otf]
-- The pictures are side by side (left original, right OK Studio), as CompareKStudio writes them.
local ok = require "ok"
local path, proc = ok.path, ok.proc
local p = ok.args.parser("OK Studio comparison short")
p:opt("--dir", { required = true })
p:opt("--scenes", { required = true })
p:opt("--out", { required = true })
p:opt("--hold", { default = "3.0" })
p:opt("--fade", { default = "0.5" })
p:opt("--title-seconds", { default = "3.0" })
p:opt("--font", { default = "C:/Windows/Fonts/NotoSansCJKsc-Bold.otf" })
p:opt("--ffmpeg", { default = path.join(ok.root, "Tools/ffmpeg/ffmpeg.exe") })
local a = p:parse(arg)
local hold, fade, title_seconds = tonumber(a.hold), tonumber(a.fade), tonumber(a["title_seconds"] or a["title-seconds"])
local work = path.join(ok.root, "Saved/OKStudio/video_work")
path.mkdir(work, true)

local W, H, PW, PH = 1080, 1920, 1080, 608
local TOP_Y, BOTTOM_Y = 330, 330 + 608 + 90
-- drawtext reads its text from files (no escaping of CJK or punctuation in the filter graph)
local function text_file(name, text) local f = path.join(work, name .. ".txt"); path.write_text(f, text); return f end
local function esc(s) return (s:gsub("\\", "/"):gsub(":", "\\:")) end
local font = esc(a.font)
local function drawtext(file, size, x, y, color, box)
  local s = ("drawtext=fontfile='%s':textfile='%s':fontsize=%d:fontcolor=%s:x=%s:y=%s"):format(font, esc(file), size, color or "white", x, y)
  if box then s = s .. ":box=1:boxcolor=black@0.55:boxborderw=14" end
  return s
end
local function run(args) local r = proc.run(args); assert(r.ok, "ffmpeg failed: " .. table.concat(args, " ")) end

local header1 = text_file("header1", "Koikatsu 工作室场景 → Unreal Engine 5")
local header2 = text_file("header2", "OpenKoikatsu 移植效果对比")
local label_top = text_file("label_top", "原版 Koikatsu Sunshine 工作室")
local label_bottom = text_file("label_bottom", "OpenKoikatsu（Unreal Engine 5）")
local note = text_file("note", "上：场景卡内置缩略图（320×180 放大）　下：同一场景在 UE5 中重现")

-- One still frame per scene.
local frames = {}
local index = 0
for scene in a.scenes:gmatch("[^,]+") do
  index = index + 1
  local src = path.join(a.dir, "koikatu_" .. scene .. ".png")
  assert(path.exists(src), "missing " .. src)
  local number = text_file("scene" .. index, ("官方样本场景 %s"):format(scene:gsub("^cs0*", "#")))
  local out = path.join(work, ("frame_%02d.png"):format(index))
  local graph = table.concat({
    ("color=c=0x101418:s=%dx%d[bg]"):format(W, H),
    "[0]split[a][b]",
    ("[a]crop=iw/2:ih:0:0,scale=%d:%d:flags=lanczos[top]"):format(PW, PH),
    ("[b]crop=iw/2:ih:iw/2:0,scale=%d:%d:flags=lanczos[bottom]"):format(PW, PH),
    ("[bg][top]overlay=0:%d[t1]"):format(TOP_Y),
    ("[t1][bottom]overlay=0:%d,"):format(BOTTOM_Y)
      .. table.concat({
        drawtext(header1, 52, "(w-text_w)/2", "110"),
        drawtext(header2, 40, "(w-text_w)/2", "190", "0xffd27f"),
        drawtext(label_top, 34, "24", tostring(TOP_Y + 20), "white", true),
        drawtext(label_bottom, 34, "24", tostring(BOTTOM_Y + 20), "0x9fe7ff", true),
        drawtext(number, 38, "(w-text_w)/2", tostring(BOTTOM_Y + PH + 70), "white"),
        drawtext(note, 26, "(w-text_w)/2", tostring(BOTTOM_Y + PH + 150), "0xa0a8b0"),
      }, ","),
  }, ";")
  run({ a.ffmpeg, "-y", "-loglevel", "error", "-i", src, "-filter_complex", graph, "-frames:v", "1", out })
  frames[#frames + 1] = out
end

-- Title card.
local title_png = path.join(work, "frame_00.png")
local t1 = text_file("title1", "恋活 工作室场景")
local t2 = text_file("title2", "移植到 Unreal Engine 5")
local t3 = text_file("title3", "OpenKoikatsu · 原版 vs UE5 对比")
run({ a.ffmpeg, "-y", "-loglevel", "error", "-f", "lavfi", "-i", ("color=c=0x101418:s=%dx%d"):format(W, H), "-vf",
  table.concat({ drawtext(t1, 92, "(w-text_w)/2", "(h/2)-220"), drawtext(t2, 72, "(w-text_w)/2", "(h/2)-80", "0xffd27f"),
    drawtext(t3, 44, "(w-text_w)/2", "(h/2)+60", "0x9fe7ff") }, ","), "-frames:v", "1", title_png })
table.insert(frames, 1, title_png)

-- Stills to clips joined by cross-fades.
local args = { a.ffmpeg, "-y", "-loglevel", "error" }
local durations = {}
for i, frame in ipairs(frames) do
  local d = (i == 1 and title_seconds or hold) + fade
  durations[i] = d
  for _, v in ipairs({ "-loop", "1", "-framerate", "30", "-t", ("%.3f"):format(d), "-i", frame }) do args[#args + 1] = v end
end
local chain, offset, last = {}, 0, "[0:v]"
for i = 2, #frames do
  offset = offset + durations[i - 1] - fade
  local out = i == #frames and "[v]" or ("[x%d]"):format(i)
  chain[#chain + 1] = ("%s[%d:v]xfade=transition=fade:duration=%.3f:offset=%.3f%s"):format(last, i - 1, fade, offset, out)
  last = out
end
local total = offset + durations[#frames]
chain[#chain + 1] = ("[v]fade=t=out:st=%.3f:d=0.6,format=yuv420p[final]"):format(total - 0.6)
for _, v in ipairs({ "-filter_complex", table.concat(chain, ";"), "-map", "[final]", "-c:v", "h264_nvenc", "-preset", "p7",
  "-rc", "vbr", "-cq", "19", "-b:v", "12M", "-maxrate", "20M", "-r", "30", "-movflags", "+faststart", a.out }) do args[#args + 1] = v end
run(args)
print(("video %s: %d scenes, %.1f s"):format(a.out, #frames - 1, total))
