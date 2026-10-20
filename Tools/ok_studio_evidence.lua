-- Run-local Studio image evidence. No fallback to the shared compare directory.
local ok = require "ok"
local path = ok.path
local M = {}

local function link(file)
  return (file:gsub("[^%w%-%._~/]", function(c) return ("%%%02X"):format(c:byte()) end))
end

function M.collect(root, log, expected)
  local images, errors, seen = {}, {}, {}
  local function reject(message) errors[#errors + 1] = message end
  local done, total = log:match("OK studio compare: RESULT (%d+) / (%d+) pictured into ")
  if tonumber(done) ~= expected or tonumber(total) ~= expected then
    reject("compare RESULT does not confirm all " .. expected .. " images")
  end
  for name, memory in log:gmatch("OK studio compare: ([^\r\n]+) pictured  %(memory ([%d.]+) GB%)") do
    if not name:lower():match("%.png$") or name:find('[%z\1-\31\127/\\:%?%*<>|"]') then
      reject("unsafe image name in compare log: " .. name)
    elseif seen[name:lower()] then
      reject("duplicate image in compare log: " .. name)
    else
      seen[name:lower()] = true
      -- Compare logs the input scene name, but always writes a lowercase .png extension.
      local rel = "images/" .. name:sub(1, -5) .. ".png"
      local file = path.join(root, rel)
      local valid, result = pcall(function()
        local data = path.read_bytes(file)
        assert(data:sub(1, 8) == "\137PNG\r\n\26\n", "not a PNG")
        -- Ordinary screenshots only: no private card or scene payload after IEND.
        local pos, ended = 9, false
        while pos + 11 <= #data do
          local length = string.unpack(">I4", data, pos)
          local kind = data:sub(pos + 4, pos + 7)
          pos = pos + length + 12
          assert(pos <= #data + 1, "truncated PNG chunk")
          assert(kind ~= "okPv" and kind ~= "okLc", "card data in PNG")
          if kind == "IEND" then ended = length == 0; break end
        end
        assert(ended and pos == #data + 1, "missing IEND or trailing payload")
        local decoded = ok.image.load(file)
        assert(decoded.width == 1280 and decoded.height == 360, "expected 1280x360 side-by-side image")
        return { file = rel, sha256 = ok.hash.sha256_file(file), bytes = #data,
          width = decoded.width, height = decoded.height, scene_memory_gb = tonumber(memory) }
      end)
      if valid then images[#images + 1] = result
      else reject(name .. ": " .. tostring(result)) end
    end
  end
  if #images ~= expected then reject(("validated %d / %d images"):format(#images, expected)) end
  return images, errors
end

function M.write(root, results)
  path.write_text(path.join(root, "results.json"), ok.json.encode(results, { indent = 2 }) .. "\n")
  local lines = {
    "# OK Studio 自動驗收紀錄",
    "",
    "執行：`" .. results.tag .. "`；程式版本：`" .. results.revision .. "`。",
    "",
    "狀態：**" .. (results.finished and (results.passed and "PASS" or "FAIL") or "執行中") .. "**。",
    "",
    "對照圖左側為原作場景的預覽縮圖，右側為本輪 Unreal 擷取結果。成功出圖只驗證載入／擷取流程，不代表畫面完全吻合；色調、景深、bloom 與角色顯示仍須目視判定。",
    "",
    "使用 --compare 時，圖片保存在本輪 images 目錄，圖與 log／JSON 可整個目錄一起搬移。SHA-256、尺寸與場景記憶體記錄在 [results.json](results.json)。",
    "",
    "| 測試 | 結果 | 執行紀錄 |",
    "|---|---|---|",
  }
  for _, test in ipairs(results.tests) do
    local name = path.name(test.log)
    lines[#lines + 1] = ("| %s | %s | [%s](%s) |"):format(test.name, test.passed and "PASS" or "FAIL", name, link(name))
  end
  for _, test in ipairs(results.tests) do
    if test.memory then
      lines[#lines + 1] = ("\n擷取峰值：%s GB；最後卸載：%s GB。這不代表已回到啟動基準。"):format(
        tostring(test.memory.peak_scene_gb), tostring(test.memory.final_released_gb or "缺少紀錄"))
    end
    for _, err in ipairs(test.evidence_errors or {}) do lines[#lines + 1] = "\n證據檢查失敗：`" .. err:gsub("`", "'") .. "`。" end
    for _, img in ipairs(test.images or {}) do
      lines[#lines + 1] = "\n## " .. path.stem(img.file)
      lines[#lines + 1] = "\n![左原作縮圖、右本輪 Unreal 結果](" .. link(img.file) .. ")"
    end
  end
  path.write_text(path.join(root, "ACCEPTANCE.md"), table.concat(lines, "\n") .. "\n")
end

return M
