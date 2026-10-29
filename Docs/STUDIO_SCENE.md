# 工作室（動作編輯場景）
> **2026-10-30 已移除**：這份文件描述的 UMG 工作室（OKStudioScene.h/.cpp）與 Tools/video_motion/test_studio.lua 已刪除。讀卡畫面「動作時間軸」改開 OK Studio（Source/OpenKoikatsu/OKGalleryOkStudio.cpp）；影片雙人動作在 OK Studio 的「動畫」頁播放（Saved/VideoMotions/*.okanim、*.pair.json）；人物編輯 MCP 的 studio 動作只剩 enter／cmd／exit，場景編輯用 ok-game-mcp 的 studio_* 工具（[OK Studio MCP](OK_STUDIO_MCP.md)）。以下保留作歷史紀錄。


參考原作 CharaStudio 做的場景。在角色編輯器按「進入工作室（動作編輯）」進入，直接沿用目前載入的女角和配對男角（不重新載入）。

程式：`Source/OpenKoikatsu/OKStudioScene.h/.cpp`（`UOKStudioWidget`、`UOKStudioGuide`、`UOKStudioTimeline`）。

## 介面（對照原作）

| 區域 | 原作 | 這裡 |
| --- | --- | --- |
| 上方列 | システム／シーン | 返回角色編輯器、場景名稱、儲存／讀取場景、狀態訊息 |
| 左側 | ワークスペース | 工作區：女角、男角（點選即為操作對象）；追加／更換男角 |
| 左側下 | ガイドオブジェクト | 操作軸模式（移動／旋轉／縮放）、位置／旋轉／縮放數值、回到初始位置 |
| 右側 | 動作／状態 | 選取角色的動作與速度。女角使用編輯器原本的動作選單（內建、Studio 動作）和衣服按鈕；男角選擇影片動作 |
| 右側下 | — | 影片動作匯入面板（與角色編輯器相同，見 `Docs/VIDEO_MOTION.md`） |
| 下方 | （原作需外掛 Timeline） | 播放控制、關鍵幀按鈕、補間、時間軸 |

鏡頭操作與角色編輯器相同（原作 BaseCameraControl：左鍵旋轉、右鍵平移、雙鍵前後、滾輪縮放）。

## 操作軸

- 模式：移動（W）、旋轉（E）、縮放（R）。
- 紅＝X，綠＝Y（上），藍＝Z，都是原作的座標軸。移動與旋轉用世界軸，縮放用角色自身的軸；縮放模式中央的白框是等比縮放。
- 操作軸畫在腰部高度（腳下會被時間軸蓋住）；移動、縮放與位置無關，旋轉仍以角色原點（腳下）為軸心。
- 拖曳時滑鼠歸操作軸所有，鏡頭不跟著動（`AOpenKoikatsuPlayerController::bExternalMouseOwner`）。
- 數值欄用原作單位：位置以公尺計，原點是進入工作室時女角的腳下；旋轉是 Unity 的 eulerAngles（度，套用順序 Z→X→Y）；縮放是倍數。按 Enter 套用。
- 地面格線：每格 50 cm，範圍 10 m。

## 時間軸與關鍵幀

- 每個角色一條軌道。關鍵幀記錄角色的位置、旋轉與縮放，時間對齊到 30 fps 的幀。
- 打關鍵幀：K 或按鈕。目前時間已有關鍵幀時會更新它。刪除：Del。「清除全部」要連按兩次。
- 「自動關鍵幀」（預設開啟）：拖曳操作軸或輸入數值後，自動在目前時間打關鍵幀。關閉時，未打關鍵幀的修改在時間移動後會回到時間軸上的值。
- 補間是「這個關鍵幀到下一個」的方式：線性、平滑（smoothstep）、常數（停在這一鍵）。旋轉一律用 slerp。
- 時間軸上的菱形：點一下會跳到該時間並選取對應角色，拖曳可以改關鍵幀的時間（不能和其他關鍵幀重疊）。點空白處或拖曳可以捲動時間。
- 播放：空白鍵；←／→ 逐幀。可以勾「循環」。勾「長度跟隨動作」時，長度等於最長動作的長度÷速度（也至少涵蓋最後一個關鍵幀）；取消勾選後可以自己輸入長度。
- 時間軸掌管兩個角色的動作時鐘：`AnimPhase = 時間 × 速度`。暫停或捲動時兩人停在同一幀，播放時兩人同步前進。

## 影片動作

- 單人影片動作會套用到目前選取的角色。
- 【雙人】項目：女角播女方、男角播男方，兩人同一原點。還沒載入男角時會自動載入（和角色編輯器相同）。之後兩人可以各自打關鍵幀，例如整段一起平移。
- 匯入影片時不能返回角色編輯器（要等匯入完成或先取消）。

## 場景檔

`Saved/Studio/<名稱>.okstudio.json`（version 1）：

- `length`、`follow_length`、`origin`；
- `camera`：`focus`、`yaw`、`pitch`、`distance`；
- `female`：`card`、`clip`（或 Studio 動作的 `row` 加 `studio_options`）、`speed`、`keys`；
- `male`：`card`、`clip`、`speed`、`keys`。

每個 key 是 `{t, pos[3], rot[4], scale[3], interp}`，使用 Unreal 單位（cm、四元數）。

讀取場景時：

- 男角卡片不同或還沒載入時，會自動載入男角，載入完成後再套用他的動作。
- 女角卡片不同時只顯示警告，動作和關鍵幀套用到目前角色。
- 內建動作要先在右側選一次同一個來源，該來源才有動作清單可播。

## 與角色編輯器的關係

- 工作室開啟時 `UOKCharacterController::bStudioMode` 為 true。這時 `OnStopPair` 只停止原作雙人播放，不刪除男角；左右兩側面板保持隱藏（換衣服等重新載入也一樣）；噴漆預覽關閉。
- 換衣服：重新載入女角時保留男角，時間軸繼續控制兩人。
- 離開時：兩人回到進入時的位置，播放速度恢復成 1；借用的動作選單和衣服按鈕放回原位；角色編輯器的影片動作清單會重新整理。

## 測試

- `Tools\lua\lua.exe Tools\video_motion\test_studio.lua [--game <KKS 目錄>]`：透過 CharacterMCP（`studio` 動作，`op` 可用 enter／select／time／play／transform／key／delete_key／interp／speed／video／male_motion／guide_drag／save／load／clear_keys／exit）依序檢查：
  - 進入工作室；
  - 【雙人】影片動作自動載入男角；
  - 線性和常數補間、旋轉 slerp、縮放；
  - 播放時兩人的時鐘等於時間軸；
  - 場景儲存與讀取；
  - 三種操作軸的拖曳；
  - 換衣服後男角仍在；
  - 離開工作室。
- `screenshot` 動作加 `"ui": true` 可以把介面一起截下來。
- `guide_drag` 用的是和滑鼠拖曳相同的程式路徑（`BeginDrag` / `DragTo`），只是游標位置由參數給定。實際的滑鼠按下判定（`HitGuide`、`CursorOverUI`）仍需要手動確認。

## 尚未支援

- 骨骼（FK／IK）姿勢關鍵幀、表情軌道；
- 道具、燈光、地圖；
- 把關鍵幀烘焙成新的 `.okanim`。
