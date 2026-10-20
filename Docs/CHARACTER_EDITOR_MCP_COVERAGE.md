# 人物編輯器 MCP 覆蓋與驗收

目前使用 Rust 2.0.0；最新正常模組實機驗收為 RustLiveR5，20 項全部通過。先前 Python 及 RustLiveR3 結果保留為歷史對照，最終證據見本文末。

實作日期：2026-10-09 至 2026-10-10。範圍為目前 OpenKoikatsu 人物編輯器已提供的參數及操作。完整計畫見 [CHARACTER_EDITOR_MCP_PLAN.md](CHARACTER_EDITOR_MCP_PLAN.md)，啟動與設定見 [Tools/character_mcp/README.md](../Tools/character_mcp/README.md)。

## 參數覆蓋

| 類別 | 介面 | 覆蓋方式 |
|---|---|---|
| 身體 | `shape.body.0` 至 `.43` | 44 項，原生形變、原遊戲預設值、批次寫入與重設 |
| 臉部 | `shape.face.0` 至 `.51` | 52 項，原生形變、原遊戲預設值、批次寫入與重設 |
| ABMX 骨骼 | `bone.<骨名>.<軸向>` | 從全部 1,192 個定義生成；展開左右後目前為 1,928 個唯一 key；明確支援狀態 |
| 物理 | `physics.*` | 柔軟度、重量，沿用即時套用流程 |
| 腹部形變 | `pregnancy.*` | 16 項數值；開關及全部預設由 editor controls 提供 |
| 詳細卡片／模組屬性 | 原生屬性 key | 依卡片動態列舉；數值、顏色、布林、配件綁定；先暫存，再套用 |
| 表情／眨眼／視線 | `control.*` | 原生選單、滑桿、開關與回呼 |
| 左右手勢 | `control.HandCombos.*`／`control.HandBlendSliders.*` | 全部當前可用選項與混合值；左右重設命令 |
| 動畫 | `control.Game*`／`control.Studio*`／`animation.*` | 來源、動作、場景與動態參數；暫停、重播、姿勢命令 |
| 服裝 | `control.OutfitCombo`／`editor_action` | 服裝切換；九個部位及整體穿著狀態，沿用原生合法轉換 |
| 頭髮／衣物／配件素材 | appearance tools | 動態 targets/items、素材選擇與重設、新增配件槽 |
| 語音 | speech controls／editor actions | 本機來源、聲線、語音選擇、載入／播放／停止 |
| 其餘編輯介面 | `editor_controls`／`editor_control_set` | WidgetTree 與 controller UPROPERTY 盤點；支援 slider／combo／checkbox／text／spinbox |
| 攝影與觀察 | capture tools | 正交／透視、固定尺度、目標部位隱藏、區域 PNG、同版本三視圖 |
| 身體幾何與量測 | `body_geometry`／`character_measure`／`body_sections` | 當前形變後解剖網格、骨骼標記、精確頂點 bounds；指定高度的獨立閉合截面及圍度 |
| 三視圖比較 | `body_masks`／`reference_prepare`／`body_compare` | 排除衣髮配件的幾何遮罩、一次對齊後固定投影、IoU 與輪廓邊界誤差 |
| 通用身形搜尋 | `body_sensitivity`／`body_fit`／`body_fit_start/status/cancel` | 目錄驅動的有界數值搜尋、雙向敏感度、背景進度、取消、最佳值保存與續跑 |
| 長作業診斷 | `editor_progress` | 不排在長作業後的 heartbeat、作業階段及等待條件 |
| 保存 | snapshot／save／load tools | 卡片資料與暫存屬性快照、新檔保存、重新載入 |

骨骼 key 包含 `scale.uniform`、`scale.x/y/z`、`length`、`position.x/y/z`、`rotation.x/y/z`。左右同步使用同一批 edits 同時寫入左右骨名。服裝限定骨骼修飾保留原有座標資料。

測試卡 `Content/CharacterCards/CrowdAPNG/C000.png` 的即時盤點為 4,300 項人物資料參數：96 項原生身形／臉形、1,928 項骨骼、18 項物理／腹部數值、2,258 項詳細屬性；另有 49 個編輯控制。其他卡片的屬性、素材、骨骼可用性及動畫控制數量可不同。

這是「完整註冊與可調介面」的覆蓋，不把每一個數值的視覺正確性都宣稱已獨立驗證。未知第三方二進位插件保持原樣；不以此聲稱原遊戲所有未移植插件功能已完成。

## 驗收證據

- Python 測試：`python -m unittest discover -s Tools/character_mcp -p 'test_*.py' -v`，16 項通過；紀錄 `Saved/Logs/CharacterMCP-python-final.log`。包含協定、取消、版本干擾保護、背景作業持久化、未知幾何自動還原、分割、固定投影、截面、敏感度、Windows 原子檔案競爭、獨立程序首次 NumPy 初始化，以及小數高度／奇數寬度對齊端點回歸。
- Unreal automation：`OpenKoikatsu.CharacterMCP.Registry` 通過；最新紀錄 `Saved/Logs/CharacterMCP-registry-r3.log`。驗證 44／52 項、骨骼唯一性、缺失骨骼拒絕、批次無部分修改、重複 key／範圍拒絕及讀寫。
- Unreal 正式建置：`Saved/Logs/CharacterMCP-build-r21.log`，成功；使用完整專案 `-NoHotReload` 產生一致的正式 DLL／lib／manifest。批次包裝鎖等待時改由正式 UnrealBuildTool 入口執行，保留 `-WaitMutex`。
- 原有真實 stdio／渲染驗收 `Saved/Tests/CharacterMCP/LiveR3/report.json` 的 13 項通過證據保留。最終 [LiveR12 報告](../Saved/Tests/CharacterMCP/LiveR12/report.json) 的 20 項全部通過，`valid=true`，工具共 31 個。
- `verify_live.py` 使用獨立信箱與自行啟動的 Unreal 執行個體，檢查全部 96 項原生參數批次讀寫／還原、幾何與截面、遮罩、參考對齊、敏感度、背景身形搜尋及最佳角色卡保存／重開。
- 失敗證據和共同原因保留：LiveR1 空白截圖改由引擎 screenshot callback 解決；LiveR7 的 GPU 隱藏 observer 過濾修正；LiveR8 首次 NumPy worker DLL 初始化阻塞已定位堆疊並修正。完整修正記於 [錯誤紀錄](CHARACTER_EDITOR_MCP_ERRORS.md)。

### 最終實機驗收（LiveR12）

| 項次 | 已通過項目 |
|---|---|
| 1 | 真實人物編輯器啟動、stdio MCP 連線 |
| 2 | 全部身體／臉部／骨骼／詳細屬性目錄，4,300 項、無重複 key |
| 3 | 其他編輯控制盤點，測試卡 49 項 |
| 4 | 基準三視圖真實渲染 |
| 5 | 非法批次拒絕且沒有部分寫入 |
| 6 | 身形讀寫及舊 revision 拒絕 |
| 7 | 真實骨骼參數讀寫 |
| 8 | 三視圖同 revision、各視圖像素不同、形變後圖像改變 |
| 9 | 320 × 480 目標區域精確截圖 |
| 10 | 詳細屬性暫存、原生外觀套用與重載讀回 |
| 11 | 快照回復身形 |
| 12 | 另存新角色卡 |
| 13 | 重開角色卡後身形／骨骼／詳細屬性一致 |
| 14 | 當前解剖幾何：19,871 頂點、35,503 三角形、677 骨骼標記 |
| 15 | 全部 44 身體＋52 臉部參數批次寫入、讀回、還原 |
| 16 | 三個指定高度的真實三角網格截面，閉合圈正周長、無效部分明列 |
| 17 | 獨立三視圖遮罩、參考對齊；相同身體對齊 loss=0 |
| 18 | 真實參數雙向敏感度探測、有輪廓改變且完成後還原 |
| 19 | 背景通用身形搜尋達到目標門檻、最佳值寫回 |
| 20 | 最佳身形另存並重開後，全部指定參數一致 |

### 合成三視圖搜尋實測

從真實角色產生目標遮罩，擾動 `shape.body.0`、`.22`、`.23`，僅向搜尋器提供三視圖及參數 key，不提供原始答案。採固定正交投影、T-pose、有界自適應座標搜尋；9 次評估後以 `target_loss` 正常完成。

| 指標 | 實測 |
|---|---|
| 初始綜合輪廓 loss | 0.13281102388409247 |
| 最佳綜合輪廓 loss | 0.0019241573809563107 |
| 相對誤差下降 | 98.5512% |
| 正／側／背 IoU | 0.997172／0.997559／0.997172 |
| 平均邊界誤差／影像高度（正／側／背） | 0.0001402／0.0000701／0.0001402 |

Loss 為 `0.7 × (1 − IoU) + 0.3 × 邊界誤差／影像高度`，對三視圖取平均；不是公分誤差，也不是任意照片還原率。目標、最佳遮罩與每次試驗保存在 [驗收目錄](../Saved/Tests/CharacterMCP/LiveR12)；完整搜尋紀錄見 [run JSON](../Saved/Tests/CharacterMCP/LiveR12/mailbox/fitting/run-9b11cd4bf64b45c18b2724030f96c174.json)。不同參數可能得到相近輪廓，本例驗證形狀吻合與持久化，不宣稱解唯一。

## 使用條件與精度邊界

- 參考圖必須為可用的人物輪廓，並與角色的固定姿勢相符。接受二值遮罩、透明背景，或明確指定 RGB 背景的分割；複雜照片的語義分割與姿勢／透視反推由外部 AI 處理。
- 無真實身高時只能比較比例。輪廓無法唯一決定所有參數，超出現有 morph／骨骼表達範圍的形狀不能保證還原。
- 原生 GPU 隱藏 CPU observer 不會把解剖網格排除；procedural 只取正式表面 section 0，避免膨脹外框影響量測。截面分圈回報，開放或非流形部分明列無效，不冒充胸／腰／臀。
- 搜尋由實際參數範圍與當前幾何驅動，不硬編碼角色解答。支援立即套用的身體／臉部／骨骼數值；耗時的暫存外觀屬性仍可由完整編輯工具調整。
- 驗收確認目前實作功能，不將所有角色／未知第三方模組逐參數窮舉的視覺測試當作完成條件。
- 快照目前保存卡片與暫存屬性；表情、攝影、動畫等預覽狀態分別操作。

外部 AI 可完成「查參數 → 準備三視圖目標 → 敏感度／批次調整／有界搜尋 → 量測比較與截圖 → 回退／保存」迴圈。實測精度以驗收報告的實際數值呈現，不把單一合成案例當作任意照片的精度保證。


## Rust 版本驗收（2026-10-10）

目前 MCP 執行入口為官方 rmcp 3.5.1 的 Rust 2.0.0，包含純 Rust 影像、幾何比較與搜尋；使用端不需 Python／NumPy／Pillow。上述 Python LiveR12 數字保留為歷史基準，新版本以 RustLiveR3 為準。

- Rust 核心／模擬編輯器／CLI／真實 stdio 協定：16／16 通過；全目標 Clippy 零警告。
- 相同 20 項實機案例：20／20 通過；31 工具及全部 inputSchema/action 保持一致。
- 對齊 loss=0；未知合成目標 9 次評估，loss=0.13342587168414 → 0.001855926981424242，下降 98.6090%，最佳值另存重開一致。
- 單一 exe 可在獨立目錄且 PATH 只有 Windows System32 的環境初始化／列工具；只依賴 Windows 系統 DLL。
- 原生 MCP 穩定等待修正為驅動非阻塞 PreparePlaybackMaterials(false)，避免漸進材質替換後 readiness 旗標一直為 false。

證據：`Saved/Logs/CharacterMCP-Rust-tests.log`、`Saved/Logs/CharacterMCP-Rust-clippy.log`、`Saved/Tests/CharacterMCP/RustLiveR3/report.json`、`Saved/Tests/CharacterMCP/RustValidation.json`。完整遷移計畫與結果見 [Rust 計畫](CHARACTER_EDITOR_MCP_RUST_PLAN.md)，新啟動方式見 [使用說明](CHARACTER_EDITOR_MCP_USAGE.md)。


### 最終正常模組驗收：RustLiveR5

專案四模組正式建置 r8 與官方專案 WriteMetadata 完成；manifest 全部指向正常 DLL。RegistryR3 通過，startup 日誌確認載入正常 KKUnityBundle.dll。使用同一批正常模組再驗收 RustLiveR5，**20／20 通過**。相同身體對齊 loss=0.0；未知合成目標 9 次評估，loss=0.13342587168414 → 0.001855926981424242（下降 98.6090%），最佳值保存重開一致。

最新證據：`Saved/Tests/CharacterMCP/RustLiveR5/report.json`、`Saved/Logs/CharacterMCP-Rust-build-r8.log`、`Saved/Logs/CharacterMCP-Rust-metadata.log`、`Saved/Logs/CharacterMCP-Rust-registry-r3.log`；總索引與執行檔雜湊：`Saved/Tests/CharacterMCP/RustValidation.json`。R1/R2/R4 失敗或中止結果保留，不當作通過證據。
