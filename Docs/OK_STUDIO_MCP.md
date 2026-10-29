# OK Studio MCP（studio_* 工具）

`Tools/ok-game-mcp`（`.mcp.json` 的 `ok-game`）除了街區的 `game_*` 工具，也提供操作 OK Studio 場景編輯器（`SceneEditor.cmd`）的 `studio_*` 工具。程式在 `Tools/ok-game-mcp/src/studio.rs`。

## 啟動與連接埠

- 控制伺服器：`http://127.0.0.1:<port>/studio/<op>`，一律 `POST` JSON（可為 `{}`），回覆 `{"ok": true, ...}` 或 `{"ok": false, "error": "..."}`（HTTP 都是 200）。
- 連接埠：環境變數 `OK_STUDIO_PORT`，預設 **47890**；MCP 伺服器與編輯器要用同一個值。
- `studio_launch` 會在 checkout 根目錄執行（detached）：

  ```text
  Tools\lua\lua.exe Tools\run_scene_editor.lua [scene] -- -Control -StudioControlPort=<port>
  ```

  再等 `state` 回應（預設 120 秒），回傳啟動器 pid。手動啟動同理：`SceneEditor.cmd [scene] -- -Control -StudioControlPort=47890`。
- 連不上（沒有開 Studio）時，工具回錯誤「OK Studio is not running ...」，請先 `studio_launch`。
- 使用者正在拖曳、場景載入中、時間軸播放／匯出時，Studio 拒絕編輯：`{"ok":false,"error":"busy: <原因>","busy":"<原因>"}`，工具以錯誤回報 busy，稍後重試即可。
- 每個工具的參數原樣當成 request body；MCP 端只做寬鬆檢查（必填參數、頂層 JSON 型別），其餘交給 Studio。
- 沒有載入 MCP 的 session：`Tools/ok-game-mcp/target/release/ok-game-mcp.exe call <工具> '<JSON>'`。

## 工具

座標單位 cm；`location` = `[x,y,z]`，`rotation` = `[pitch,yaw,roll]`（度），`scale` = `[x,y,z]`。物件 id 由 `studio_state` 取得（整數或字串都接受）。

| 工具 | op | 參數 | 回傳 |
|---|---|---|---|
| `studio_launch` | — | `scene?`、`width?`、`height?`、`no_build?`（`-NoBuild`）、`options?`（`--` 後的 Unreal 參數）、`timeout_s?`（預設 120） | `{pid, port, seconds}` |
| `studio_stop` | `cmd` | 無（送 `{"id":"app.quit"}`，不殺程序） | `{}` |
| `studio_state` | `state` | 無 | `scene_file, dirty, selection, primary, objects[{id,parent,name,kind,visible,attach_bone,location,rotation,scale,error?}], ui{commands...}, busy, timeline{time,duration,playing}` |
| `studio_inspect` | `inspect` | `id` | `{object}` |
| `studio_catalog_search` | `catalog_search` | `query`（空白分隔、全部符合；`-字` 排除）、`kinds?`（`item effect map anim pose card`）、`limit?` | `{results[{kind,key,name,path?}]}` |
| `studio_spawn` | `spawn` | `kind`（`character item light camera effect folder`）、`card?`、`item?`（`group/category/no`）、`effect?`、`light_type?`（`directional point spot`）、`parent?`、`bone?`、`name?`、`location?`、`rotation?`、`scale?` | `{id}` |
| `studio_transform` | `transform` | `id`、`location?`、`rotation?`、`scale?`、`relative?` | `{}` |
| `studio_reparent` | `reparent` | `id`、`parent`（-1 = 根）、`bone?` | `{}` |
| `studio_delete` | `delete` | `ids` | `{}` |
| `studio_select` | `select` | `ids` | `{}` |
| `studio_pose` | `pose` | `id`、`fk?[{bone,euler}]`、`ik?[{effector,position}]`、`clear_fk?`、`clear_ik?`、`pose_file?`、`anim?`（`kind/group/category/no`）、`expression?{eyebrow,eyes,mouth,mouth_open}`、`hands?{left,right}`、`look?{eyes,neck}`、`mirror?`（`all left_to_right right_to_left`） | `{}` |
| `studio_camera` | `camera` | `focus?`、`yaw?`、`pitch?`、`distance?`、`preset?`（`front side top reset`）、`focus_selection?`、`use_camera?`、`bookmark_save?`／`bookmark_load?`（0–9） | `{focus,yaw,pitch,distance}` |
| `studio_capture` | `capture` | `width?`、`height?`、`transparent?`、`path?`（不覆寫既有檔） | `{file}`，並附上圖片預覽 |
| `studio_save` | `save` | `name`（不覆寫既有檔） | `{file}` |
| `studio_load` | `load` | `path`、`merge?` | `{report}` |
| `studio_undo` / `studio_redo` | `undo` / `redo` | 無 | `{}` |
| `studio_cmd` | `cmd` | `id`（`studio_state` 的 `ui.commands`）、`arg?` | `{}` |
| `studio_timeline` | `timeline` | `time?`、`play?`、`key?{ids}` | `{time,duration,playing}` |

## 範例：加角色、擺姿勢、拍照、存檔

以 CLI 示範（MCP 呼叫的參數相同）。`<id>`、`<card>`、`<anim>` 換成前一步回傳的值。

```powershell
$mcp = '.\Tools\ok-game-mcp\target\release\ok-game-mcp.exe'
& $mcp call studio_launch '{}'
& $mcp call studio_catalog_search '{"query": "school -swim", "kinds": ["card"], "limit": 5}'
& $mcp call studio_spawn '{"kind": "character", "card": "<card>", "name": "A", "location": [0, 0, 0]}'
& $mcp call studio_catalog_search '{"query": "stand", "kinds": ["anim"], "limit": 5}'
& $mcp call studio_pose '{"id": <id>, "anim": "<anim>", "expression": {"eyebrow": 1, "eyes": 1, "mouth": 2, "mouth_open": 0.3}, "look": {"eyes": "camera", "neck": "camera"}}'
& $mcp call studio_pose '{"id": <id>, "fk": [{"bone": "cf_j_arm00_L", "euler": [0, 0, -60]}]}'
& $mcp call studio_camera '{"preset": "front", "focus_selection": true}'
& $mcp call studio_capture '{"width": 1920, "height": 1080}'
& $mcp call studio_save '{"name": "mcp_example"}'
```

- `look.eyes`、`look.neck`：`animation`（照動畫）、`camera`（看鏡頭）、`point`（看注視點）、`fixed`（匯入的固定姿勢），也可用 0～3。
- `anim`：`studio_catalog_search` 回的 key 去掉開頭的 `anim:`（例如 `anim:0/1/2/3` → `0/1/2/3`）；`card` 用結果的 `path`。
- FK 骨名是 Studio 的 FK 骨（`cf_j_arm00_L` 等，左右以 `_L`／`_R` 結尾）；`mirror` 可用 `all`、`left_to_right`、`right_to_left`。
- 不確定欄位時先 `studio_inspect` 看角色目前的值。
