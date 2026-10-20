# 人物編輯模式：主光跟隨相機、飾品頭髮不跟動畫（2026-10-19）

## 1. 預設光照從相機照向角色

- **原狀**：`AKKPreviewActor::SetupStudioView` 生成三盞固定方向的 `ADirectionalLight`。角色卡通光 `_WorldSpaceLightPos0` 在共用母材質（`KKRuntimeSharedMaterial`）裡是載入時寫死的 Unity (0, .643, .766)，之後沒有任何程式更新。相機轉到側面或背面時，角色面向相機的一側是暗的。
- **修正**：新增 `AKKPreviewActor::UpdateCameraKeyLight()`。
  - 主光方向 = 相機的反向前方往相機上方抬 40°。正面預設鏡頭的光照和舊的固定光一樣，相機轉動時這個相對角度保持不變，跟原作人物編輯器「主光是相機子物件」相同。
  - 同時更新所有共用母材質實例（含外框）的 `_WorldSpaceLightPos0`（`SetSharedVectorParameter` 值沒變時會跳過），並轉動主 `ADirectionalLight`（強度 2.5 那盞）。兩盞補光維持固定。
  - 呼叫時機：每次 `Tick`（`FrameCurrentPose`、拍照機位會直接移動 `StudioCam`）與 `ApplyOrbitCamera` 結尾（MCP 拍照時 actor tick 是關的）。
  - 只有 `SetupStudioView(false)`（獨立的編輯畫面）會設 `bCameraKeyLight`。互動模式（`PreserveWorld=true`）和 OK Studio 道具（`bStudioItemShading`）不受影響。
- **未做**：卡通自陰影 `ToonShadowCapture` 的方向仍是載入時的固定值。顯示陰影預設關閉（`shadows=0`），因此這次沒有改。

## 2. 高模飾品頭髮不跟動畫、浮在旁邊

- **症狀**：卡片 `kke re刻晴 abmx修复.png` 播放動作時，亮紫色雙馬尾（飾品 `kqin1` 的 hair2，材質 `toufa_Copy`，`main_item_studio_alpha`）停在載入時的姿勢，頭頂露出頭皮。若動作有根骨位移（舉手動作），整塊頭髮會留在原地，看起來浮在身體旁邊。暗色層 hair1（`toufa`）大致正常。GPU 蒙皮模式（讀卡器預設）還會另外為這些飾品建 `Mouth_%d` 骨架網格，同樣停在原地。
- **根因**：`OKSessionMouthPairNames` 在 session 沒有嘴型對照時，退回 `OKGameMouthPairNames` 以**網格名稱子字串**比對。`polysurface95` 含有 `face`（poly-sur-**face**），被當成 `cf_O_face`，於是這個沒有任何 blend shape 的飾品被建成嘴型 slot。
  - CPU 路徑：嘴型更新會用完整頂點 `FillSection` 重建 section，蓋掉載入時 `OKCompactProceduralSections` 壓縮過的 section。之後每幀的動畫串流檢查 `Section->ProcVertexBuffer.Num()!=Count` 一直成立（2271 對 1210／1061），整個部件被靜默跳過，頂點停在最後一次嘴型更新時的位置。
  - GPU 路徑：同一個判斷會額外建立 `Mouth_%d` 骨架網格元件。
  - Rust 端沒有問題：暫時測試把身體根骨移 0.5 m，四個 polySurface95 部件（含 `toufa_Copy`）都正確跟著移 0.467 m。
- **修正（通用）**：`OKSessionMouthPairNames` 先檢查 `Session.MorphCount(PartIndex) > 0`。沒有 blend shape 的網格不可能驅動嘴型，因此名稱中含 face／tang／tooth／canine／noseline 的飾品都不會再被誤判。臉、鼻線、牙、舌照常有 morph，行為不變。
- **對照**：遊戲的 mesh dump（`D:\KoikatuSunshine\mesh_dump\kke re刻晴 abmx修复.png`）中，hair1 與 hair2 都是 `polySurface95`，各有 2 個 submesh，`blendShapeCount=0`。

## 3. 驗證（E:\OpenKoikatsu duo2，引擎 E:\PhysX\UE5.8）

證據放在 `Saved/Tests/EditorLightHair20261019/`：

- **啟動方式**：讀卡器相同環境（`OK_GPU_SKIN=1`、`OK_SKELMESH=1`、`OK_CPU_SKIN=0`），用 `-CharacterMCP` 操作。
- **修正前**
  - `before_keqing_stand01*.png`：亮色頭髮層停在 T-pose 位置，頭頂露出頭皮。
  - log 持續出現 `OK mouth secondary section topology mismatch part=18/19`。
- **修正後：刻晴卡**
  - 站立 01、特殊 01（舉手），以及部分脫衣再全穿（換裝快取命中）之後的立ち01，頭髮都貼在頭上（`after_keqing_*.png`）。
  - 上述嘴型 mismatch 錯誤為 0 次。
- **修正後：一般卡**：C050 的特殊 01、站立 01，頭髮跟著頭（`after_C050_*.png`）。
- **光照**：T-pose，相機 yaw 0／90／180／270、pitch 10（`after_light_*.png`）。角色像素平均亮度依序為 104.1、110.4、110.7、110.1，背面不再變暗。
