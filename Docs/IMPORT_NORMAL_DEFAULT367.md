# Stage367：獨立匯入的 MaterialEditor 法線預設值

原始 KKS 卡的私有來源視圖刻意不帶原遊戲的 BepInEx 設定。此前 `material_editor_converts_normals` 在設定檔不存在時回 false，使獨立匯入跳過原作預設啟用的法線 RenderTexture 轉換。這是來源模式的共用缺陷，與卡名、角色、服裝無關。

## 原作依據與修正

反編譯來源 `C:/portable/KoikatuSunshine/kks_decomp/plugins/KKS_Plugins/KKS_MaterialEditor/KK_Plugins.MaterialEditor/MaterialEditorPlugin.cs`：104 行 `Config.Bind<bool>("Config", "Convert Normal Maps", true, ...)`；`AssetLoadedHook`、`ConvertNormalMaps` 檢查設定與材質 property，`ConvertNormalMap` 建立使用自動 mip 的 RenderTexture，Blit 後保留來源 wrapMode。

只對 `require_complete_mods` 標記的專案獨立來源，在設定檔不存在時採用 true。明確設定 false 仍優先；未標記且無設定的既有安裝來源維持原行為。不讀外部遊戲設定，不複製遊戲或 plugin DLL，也不新增逐卡替代。既有轉換的通道、PNG 行方向、sRGB 編碼與 shader property 分流保持原來公式。

`SESSION_PIPELINE_VER` 244 → 245，bundle cache 108 不變，避免繼續使用修正前的材質 session。舊 `.uasset` 不自動改寫；重新匯入請選新的目的地。

## 驗證

- `NormalRedR1`：新增預設值回歸先重現，actual exit101。
- `NormalGreenR4`：5 passed、0 failed、1 ignored，actual0。涵蓋缺設定的兩種來源模式、明確 true/false、缺設定鍵，以及實際 `push_meshes` 的四類角色 × bundle/ME 兩種貼圖來源。獨立模式與明確 true 的貼圖及 sampler 完全一致；false 的負例保留不同貼圖。核對轉換紅通道、wrap2、sRGB、自動 mip。另兩項既有測試分別驗 ME 貼圖行方向與所有 draw roles 的 property/sampler，均 actual0。SQL 六項回歸亦通過。
- 原有 March 7th dump 診斷在缺檔時直接 return，被 Rust 算成通過。改為明確 ignored，手動啟用時要求原始卡與歷史 dump 必須存在；本輪不宣稱通過原作影像比對。`NormalGreenR1` 曾列5 passed，但其中這項沒有執行比對，排除為最終證據。
- `NormalGreenR3` actual101：新增組裝測試在 false 負例錯誤索引不存在的 runtime sampler，測試假設錯誤。改用 Option 保留「未被 RT 替換」狀態，true 必須有完整 RT metadata，false 仍必須有正常貼圖 binding 且 bytes 不同；沒有修改產品邏輯去迎合測試。
- 完整 Editor `BuildR1` Succeeded、actual0、61.91 秒。測試函式後續修正僅在 `cfg(test)`，產品修正均已包含於此 DLL。
- `RawModsR1`：33 相鄰 zipmod、1146 專案基礎資源，AutomationSuccess / actual0，313 assets，原遊戲 unused。新程序實際 RHI `VerifyModsR1` actual0：57 Static、11 Skin、158 材質、85 貼圖，Static195914 / Skin19914 三角形、300 morph / 91970 deltas、tiny0、UV/色彩保留。轉換後保存 18 mip chains / 259 source levels，舊 Stage366 為15 /230；數量變動符合 RT 自動 mip 的修正，既有幾何與輪廓不減少。
- `RawVanillaR1`：0 zipmod、AutomationSuccess / actual0，102 assets；新程序實際 RHI `VerifyVanillaR1` actual0：17 Static、11 Skin、39 材質、33 貼圖，Static79572 / Skin19914 三角形、300 morph /91970 deltas、tiny0、7 mip chains /103 levels、UV/色彩保留。全部13項有效 Rust 回歸通過，歷史 dump 的1項 ignored 不計入。

最終原版卡結果與所有檔案 SHA-256 收於 `Saved/Tests/AssetImport367/Audit.json`。所有本輪 logs/exit files 使用新名稱，原卡、基礎包、Stage366 資產及舊 Audit 不覆寫。

## 範圍

此輪驗證設定預設、材質組裝及資產重開，沒有全卡、全部 shader、GPU 最終像素或整個應用 Shipping 的零 bug 證明。來源模式採用受支援 plugin 的預設語意，不能還原一個未提供的自訂遊戲設定。歷史 dump 缺檔診斷仍略過，Static 組裝 Actor 與既有限制維持。
