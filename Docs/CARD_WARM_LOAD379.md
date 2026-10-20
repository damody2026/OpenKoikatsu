# Stage379 GUI 二次開啟卡片

## 根因與實作

GUI 每次建立一個新的 PreviewActor，session 的磁碟缓存只保存原生模型與 PNG；Unreal 重複解 PNG、生成 encoded mip、上傳貼圖與建立材質。ProfileR1 用原生 March_7th 卡連續兩次 Open／LoadSession／ApplyShapes／catalog；第二次已 SQL hit，總16850.082ms，主要 texture11020.5ms 與相同 shapes3113.802ms。因此不刪除有效磁碟快取、不以卡別名稱或截圖例外補丁處理。

KKTextureRuntime 提供共用 PNG CPU 快取，key 包含圖片內容 hash／長度／mip policy，命中還需完整 PNG byte equality，避免 hash collision 誤重用。source SRGB／filter／wrap 每次在獨立 UTexture2D 設定，因為目前 preview 會再依角色材質修改這些屬性；不將 mutable texture UObject 跨角色共用。immutable mip 以 Core 快速 LZ4 無損保存；不可壓縮者保留原bytes。解壓成功的長度必須完全相同，否则丟棄該 cache entry 並由 PNG 重新建立。LRU payload（source PNG＋packed mips）上限512MiB，提供 trim/clear。

GUI FinishLoad3D 僅在 Selected.ShapeValues 與 session 已載入的值不同時 ApplyShapes。正常身形編輯仍執行原生 ApplyShape；沒有全域跳過形變。Rust／SESSION_PIPELINE249／CACHE108／原生 mip kernel／資產格式不變。

## 中間結果不當作完成

未壓縮cache工作集過大，GreenR2二次仍miss91，總13.35秒，不能當成足夠改善。FCompression 的 LZ4實際為HC，PackedR1二次4.80秒、全部134貼圖引用的mip bytes/sampling/SRGB相同，但首次build退化至34.43秒。最後採用引擎 Core 公開快速 LZ4 API，避免只改善warm而惡化cold。

## 驗證與範圍

測試包含 image content 變更、mip policy、不同sampling與獨立mutable textures、LRU淘汰與清空、與未快取生成的mip逐位元組一致。實際卡從GUI同一UKKCardReader取得shape值，再連續兩次session與角色建構、catalog，核對每個LoadedTextures的全mip hash、尺寸、SRGB、filter、wrap。正式 BuildFastR1 actual0。CacheFastR2（修正runner後）、PackedFastR1與NativeMipsR1為最終驗收tag；均需 actual0／Automation Success。PackedFastR1 原始數字：

|階段|修改前第二次 ProfileR1|修改後第二次 PackedFastR1|
|---|---:|---:|
|session open|1074.774 ms|1117.030 ms|
|Unreal build|12567.785 ms|3595.886 ms|
|same shapes|3113.802 ms|0.006 ms|
|catalog|93.721 ms|102.680 ms|
|合計|16850.082 ms|4815.602 ms|

約減少71.4%。未熱的CPU mip cache首次Unreal建構12322.182ms（原ProfileR1首次12953.151ms），沒有HC版本的首次退化。首次session兩種來源cache狀態不同，不以其總時長作公平cold比較。最終89entries占77516886bytes，第二輪133次PNG請求全hit，miss數仍89；輸出134個texture引用全mip hash／sRGB／filter／wrap完全一致。PNG cache為不可變CPUbytes、每次新建texture，因此快取不跨角色分享可變材質資源。

NativeMipsR1重跑既有8個原生controls、既有kernel <=1／recursive <=3門檻；未放寬，不把native GPU readback近似門檻當成全部像素相同。

原卡PNG與既有封存未覆寫；R1.audit.json記錄完整receipt／source／正式DLL。重開Unreal即可使用，無需新增卡片設定。

CPU mip cache 在同一 Unreal 程序內生效，關閉程序後會清空；原本session磁碟cache保持。容量不足時仍正常重建，不保證任意巨大卡工作集全數常駐。耗時數字為Open／build／shape／catalog，不包括選卡前的UI解析、語音／其他控制的額外工作；並未使用 native UI 自動化點擊整個GUI，也不是最終GPU pixels的驗收。
