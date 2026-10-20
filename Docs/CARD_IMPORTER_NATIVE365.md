# 原始 KKS 卡匯入 Unreal（Stage365）

專案已包含 KKS 共用資源，匯入器不需要使用者指定 KKS 安裝目錄。原始 KKS PNG 使用專案內基礎資源；所需 zipmod 必須放在卡片同一層目錄。已轉換的獨立 PNG/APNG 仍沿用原有共用資源包。

## 操作

1. 重新開啟專案，選 **Tools → Import KKS / Independent Character Card...**；Content Browser 的 Add 選單也有相同入口。
2. 選擇原始 KKS 卡片 PNG。不使用 zipmod 的原版卡只需 PNG；模組卡將所需 `.zipmod` 放在 PNG 旁邊，檔名不限，依 manifest GUID 解析。
3. 指定全新的 `/Game/IndependentImports/MyCharacter` 資料夾。Outfit `-1` 匯入全部，`0` 匯入第一套。
4. 按 **Import Character Assets**。完成後定位 `Character` Data Asset；`Resources` 下保存模型、骨架、材質與貼圖。

專用選單與一般 **Import to Current Folder** 不同；目前拖曳 PNG 不會自動匯入角色。匯入同步執行，沒有取消功能。已有目的地不可覆寫。

## 共用資源與來源

2026-10-11 更新：原始人物卡的來源準備排除 `abdata/map/` 與
`abdata/studio/map/`，不再讀取、驗證或掛載這兩類場景 payload。
既有 `native_v1/catalog.json` 可保留這些項目，場景檔案不在也不會阻擋
人物卡匯入；索引中繼資料仍會驗證。其餘基礎資源仍須存在並通過大小及
SHA-256 校驗。此規則是排除已知場景類別，不是每張卡的最小依賴閉包，
不可據此刪除其他未經確認的 bundle。日誌增加 `excluded_scene_bundles`。

排除範圍合計 175 個 bundle、7,262,186,411 bytes（約 6.763 GiB）。
測試將場景 payload 設為遺失或 checksum 不符，來源準備仍成功；
必要人物資源遺失或損毀、索引含 traversal，以及只剩場景的空來源仍拒絕。
原版 `ill_male_01.png` 已在 scene-free 來源中載入 25 個部件並完成蒙皮。
本機結果保存在 `Saved/Reports/resource-audit-2026-10-11/`。

本次驗收確認來源準備與角色載入，不宣稱完整 `.uasset` 保存已通過。
完整匯入另遇到既有 `Texture has no readable mip`；驗證途中等待貼圖編譯後，
又遇到材質引用暫存 `VolumeTexture` 的 `Unsaved dependency`。
相關試驗性貼圖修改已撤回，本次程式變更只處理場景來源依賴。

原始模組卡 `KK_398530.png` 目前在配件解析回報
`single-slot source is not an accessory`；scene-free 與補回全部 175 個場景檔的
完整來源對照均出現同一錯誤，這張卡尚未通過當前版本的完整匯入。
此結果不覆蓋下方 Stage365 的歷史驗收紀錄。

- `Content/IndependentResources/v1`：既有獨立卡共用資源，1691 chunks，保持不變。
- `Content/IndependentResources/native_v1`：原始卡用的資源路徑索引與獨立副本，共 1146 個基礎檔案、30,030,018,520 bytes。由工具建立時讀取原版資源，完成後匯入不再需要原遊戲。
- `Saved/NativeImportSources/<唯一ID>`：每次匯入建立的私有來源視圖。排除上述場景後，保留的基礎檔案先驗完整 SHA-256，再連到專案副本；同磁碟用 hard link，跨磁碟用複製。zipmod 的當前發現與選擇規則見 `KKLocalMods::BuildForCard`，此場景排除修改不調整模組來源。

共用 commandlet/native extractor 負責所有卡片的轉換，不套卡名、圖片或單一卡偏移。日誌中的 `game_root=provided` 是此私有視圖，不是原遊戲安裝目錄。輸出的 `.uasset` 新程序重開不需要卡片或來源視圖。

## 缺件與錯誤

只掃描卡片同一層的 zipmod，不遞迴掃描子目錄。相同 GUID、相同內容可去重；同 GUID 不同內容拒絕。manifest 的 ZIP CRC 與解壓長度均驗證，合法 UTF-8 BOM 可讀取。無法解析的相鄰 zipmod 明確拒絕。

卡片要求但未找到的模組 GUID 回報 `missing adjacent zipmod GUIDs`，不改成預設部件。身體選擇器的 descriptor GUID 使用其原生 descriptor resolver，與套件 manifest GUID 分開。原生 Default.Body.Female/Male 保留原版行為。原始 KK_346095 的 `kks.co_bot_00` 在現有模組索引也缺少，因此不是已驗收成功的原始卡。

保存中途出錯可能留下部分 `.uasset`；保留 log，改用新的目的地。卡別資源、任意模組、GPU 像素、完整動畫控制器及完整應用 Shipping 不在本輪通過範圍。`KKImportedCharacterActor` 仍是 Static snapshot 組裝；保存的 Skeletal Mesh 可另用 Unreal 工具處理。

## 驗證證據

所有資料在 `Saved/Tests/AssetImport365`，log 在 `Saved/Logs/AssetImport365*`，不覆寫 Stage361–364 舊資產或 Audit。

- 原版 `ill_male_01.png`：同目錄 zipmod 0；17 Static、11 Skeletal、39 材質、33 貼圖，共 102 資產。
- 原始模組卡 `KK_398530.png`：同目錄 33 個 zipmod；57 Static、11 Skeletal、158 材質、85 貼圖，共 313 資產。
- InterfaceR1：實際選單、Slate 截圖、輸入／越界／缺件／覆寫拒絕及獨立卡全部 9 outfits，1667 資產，Automation Success / exit0。
- VerifyModsR1 / VerifyVanillaR1 / VerifyUIR1：新程序實際 RHI，三組保存資產均 exit0，UV、頂點色、來源 mip、三角形與 morph 檢查通過。模組卡 300 morph / 91970 有效位置 delta，9 outfit 卡 2808 / 864108；tiny0。
- LocalModsR1：同目錄範圍、任意檔名、GUID 去重／衝突、CRC 拒絕與 UTF-8 BOM 解析，Automation Success / exit0。

最終完整 BuildR7 Succeeded / exit0。RawVanillaR2、RawModsR3 均 Automation Success / exit0，VerifyVanillaR2 / VerifyModsR2 再以新程序重開最終輸出通過。Rust release 的 portable 與 mods 兩組各 3 passed、0 failed、1 ignored。不能以程序 exit0 取代 Automation Success；RawModsR1 的 BOM 拒絕失敗保留，修正後 RawModsR2/R3 通過。

額外 Rust debug 全庫測試共列出 522 項，觀察到 10 項失敗，且使用約 87 GB 工作記憶體，之後只終止已核對程序樹與本輪 log 的自有測試程序，退出 -1；此測試不算通過。兩項失敗另以 release 精確重測確認：舊 main_alpha 測試期待 translucent、現有還原程式回 masked；混合來源 ColorMask 測試期待已染色 PNG、目前回原 PNG。二者均 exit101，沒有為了消除失敗而改期待值或材質程式，其餘失敗原因未全數核對。詳 RustR1.cancelled.json、KnownShaderFailureR1 / KnownFixtureFailureR1 logs。這些失敗仍待另行處理，沒有整個專案零 bug 的驗收。
