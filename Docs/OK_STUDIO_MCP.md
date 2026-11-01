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
| `studio_state` | `state` | 無 | `scene_file, dirty, selection, primary, objects[{id,parent,name,kind,visible,attach_bone,location,rotation,scale,error?}], ui{commands...}, busy, timeline{time,duration,playing}, sound{bgm,bgm_play,bgm_repeat,bgm_file,bgm_playing,outside,outside_play,outside_repeat,outside_file,outside_playing}` |
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
| `studio_patch` | `patch` | `id`、`fields`（深層合併進物件 JSON，即 `studio_inspect` 的 `object`；物件逐鍵合併、陣列與純值取代；`id`／`kind`／`parent` 不能改，改父物件用 `studio_reparent`） | `{object}` |
| `studio_scene` | `scene` | `post?`（深層合併：bloom、景深、霧、天空、濾鏡、背景…，鍵同回傳的 `post`）、`map?`（-1 = 無）、`map_transform?{location,rotation,scale}`、`duration?`（秒）、`active_camera?`（-1 = 自由視角）、`sound?`（音效選單：`Bgm`、`bBgmPlay`、`bBgmRepeat`、`Outside`、`bOutsidePlay`、`bOutsideRepeat`，深層合併）；全不給只讀取 | `{post,map,map_transform,duration,active_camera,bookmarks}` |
| `studio_bones` | `bones` | `id`（角色） | `{fk[{bone,name,part,euler,set,world}], ik[{effector,name,enabled,position,world}], look_point, accessory_points[{key,name,group,world}]}` |
| `studio_bounds` | `bounds` | `ids?`（預設全部） | `{objects[{id,min,max,center,size}]}`（世界座標 cm） |
| `studio_catalog` | `catalog` | `kind`（`item map effect anim filter sky accessory_point`）、`group?`、`category?`、`anim_kind?` | `{groups?, categories?, entries[...]}`；文字搜尋仍用 `studio_catalog_search` |
| `studio_keys` | `keys` | `id`（物件；-2 = 場景本身的軌道）、`action`（`list set remove move ease clear smooth`）、`time?`、`to?`、`ease?`（`linear ease_in_out ease_in ease_out step`）、`smooth?` | `{keys[{time,ease,...}], duration}` |
| `studio_duplicate` | `duplicate` | `ids` | `{ids}`（新複本） |
| `studio_export` | `export` | `kind`（`png mp4 okanim`）、`fps?`、`width?`、`height?`、`from?`、`to?`、`folder?`、`id?`（okanim 的角色） | `{folder}` 或 `{file}`；進度看 `studio_state` 的 `export{running,frame,frames,last_movie,error}` |
| `studio_scenes` | `scenes` | `folder?` | `{folder, files[{path,name,kind,modified}]}`，給 `studio_load` 開 |
| `studio_report` | `report` | 無 | `{load[...], missing[...]}`：上次開檔沒能顯示的內容、場景缺的資源 |
| `studio_ui_shot` | `ui_shot` | `name?` | `{file}`，含 UI 面板的整個視窗截圖（晚一兩幀寫出，最多等 15 秒），並附上圖片預覽 |
| （只有控制通道） | `ui_fit` | `open?`、`close?`、`page?`＋`bones?` | 開關浮窗或詳情頁；不帶參數時回 `{view, shown, problems}`：超出視窗的元件、被截斷的文字、跑出畫面的視窗（`Tools/ok_studio_ui_fit.lua` 逐窗跑） |

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

## AI 擺初版、人手細調

用法是 AI 先用 `studio_*` 工具擺出場景的第一版，存成新檔，再由人在 SceneEditor 裡細調。

擺放時要知道的座標規則（2026-10-30 實測）：
- 單位 cm，Z 朝上，地面 Z = 0。
- 角色 `rotation [0,0,0]` 面向 +Y，也就是前視相機（`studio_camera` `preset: "front"`）看到的是臉；yaw −90 面向 +X、yaw 90 面向 −X、yaw 180 背對前視。兩個角色面對面站在 X 軸上：左邊的 yaw −90、右邊的 yaw 90。
- 道具與角色剛加入時還在建：`studio_bounds` 回 `built: false`，`min`／`max` 只是預留的小框；等到 `built: true`（或 `studio_state` 的 `busy` 不再是 `loading`）再用它的大小擺放。
- `studio_bones` 的 FK `euler` 是 Unity 區域角度（與 KK Studio 的 FK 相同）；IK `position` 是角色空間的 cm（+Y 前方、+Z 上方）；`world` 是世界座標 cm。
- 縮放和 KK Studio 一樣：物件的大小只看自己的 `scale`，不繼承父物件。父物件的縮放只會放大子物件的位置（`location` 乘上父物件的縮放）。所以把東西掛到放大的道具底下時，子物件的 `location` 要除以父物件的縮放；`studio_reparent` 會保持物件的世界大小。
- 匯入 KStudio 場景卡時，mod 道具要先在基礎庫裡（`ok-extract <遊戲> --oklib studio-mods <基礎庫> <場景資料夾>`）。之後 `studio_catalog` 也會列出 mod 的群組與道具，名稱是 mod 自己取的。
- 相機道具與螢幕道具（例：HarvexARC 的 CH1-Camera／CH1-Screen）靠同一張 RenderTexture 相連，螢幕即時顯示相機畫面。要先記錄 mod 道具的節點與相機：`ok-extract <遊戲> --oklib studio-mod-nodes <基礎庫> <場景資料夾>`。相機道具隱藏時照樣拍（Studio 只關 renderer）。

1. **啟動**：`studio_launch`（`studio_state` 回「OK Studio is not running」時）。
2. **找素材**：`studio_catalog` 依 group／category 瀏覽道具、地圖、特效、動畫、濾鏡、天空與飾品掛點；知道關鍵字時用 `studio_catalog_search`。
3. **放物件**：`studio_spawn` 加角色、道具、燈光、相機；多份相同的東西用 `studio_duplicate`。擺放時用 `studio_bounds` 對齊：要站在地上就讓物件的 `min` z 等於地面高度，放在桌上就對齊桌子的 `max` z，並排時比較 `min`／`max` 的 x、y，再用 `studio_transform` 移動。其他設定（顏色、燈光強度、可見度…）先 `studio_inspect` 看鍵，再用 `studio_patch` 改。
4. **擺姿勢**：`studio_bones` 讀角色的 FK 骨、IK 端點、注視點與飾品掛點，再用 `studio_pose` 設 FK／IK、動畫、表情、手型與視線；道具可掛到 `accessory_points` 的骨頭上。
5. **畫面氛圍**：`studio_scene` 設地圖、bloom、景深、霧、天空、濾鏡與背景；要做動畫時設 `duration`，用 `studio_keys` 打關鍵幀。
6. **檢查**：`studio_camera` 擺鏡頭（或放相機物件並用 `studio_scene` 的 `active_camera` 指定），`studio_capture` 拍圖確認；要看人在編輯器裡看到的畫面（含面板）用 `studio_ui_shot`。開舊場景時用 `studio_report` 看缺哪些資源。
7. **存檔**：`studio_save` 用新的名稱（不覆寫既有檔），回傳 `{file}`。

接著由人開啟這個場景細調：

```powershell
.\SceneEditor.cmd <studio_save 回傳的 file>
```

- Studio 開著控制通道時，AI 的編輯與人的編輯在同一份 undo 歷史裡，兩邊都可以 `Ctrl+Z`／`studio_undo` 復原對方的步驟。
- 人正在拖曳、場景載入中、時間軸播放或匯出時，AI 的編輯會被拒絕並回 `busy`（`{"ok":false,"busy":"<原因>"}`）；等人放手或載入、播放結束後再試，不要連續重送。
