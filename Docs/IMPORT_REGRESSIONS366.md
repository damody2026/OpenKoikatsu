# Stage366：原始卡匯入回歸與歷史測試核對

延續 Stage365，不改卡片、角色顏色或圖像。先以 release、精確測試名稱逐一重現上一輪 10 個失敗，紀錄在 `Saved/Tests/AssetImport366/RegressionsR1.json`；10 個均 exit101，沒有把取消的全庫測試當成完整測試結果。

## 程式修正

`Shader Forge/main_alpha` 曾被提前納入 MainItem／MainOpaque 的 masked 分支。原作 `Saved/Tests/ForgeAlpha333/Generated/parsed.json` 的 FORWARD pass 使用 srcBlend=5（SrcAlpha）、destBlend=10（OneMinusSrcAlpha）；原 DXBC 0017 的 `o0.w` 是採樣／mask alpha，而非常數1。改為透明、自行計算光照的原生 shader 分支，與 renderer role、renderQueue 無關。既有 `SessionPart` 在真正證明所有 albedo／有效 alpha mask 完全不透明時才使用 `_OKNativeAlphaOpaque` 的最佳化，未放寬這個條件。SESSION_PIPELINE_VER 243→244，避免舊 session 的錯誤 blend 判定繼續使用；bundle cache schema108保持。

Windows zipmod SQL 索引曾以 path 的原始字串當識別：`C:/...` 與 `C:\...` 是同一個檔案，卻被判成全數刪除與重新匯入。修正 inventory signature、排序、增量比對的斜線正規化；不折疊大小寫，也不改其他平台的路徑規則。未變檔案保留舊 SQL key 及內容，真正 size／mtime 改變才移除舊 alias 的相依資料並重新解析。在記憶體 SQLite 的回歸測試中，不存在的檔案更換路徑拼法仍保留 GUID sentinel；真正修改大小／時間則必須清除 sentinel。現有 7310 個 zipmod 環境的缺 GUID 查詢由約 28.1 秒降至 1.8 秒；這是此環境暖快取數值，不是任意冷快取效能保證。

## 修正測試假設

- ColorMask：原生 MainItem fragment 混色，必須保留 MainTex、ColorMask 與三組色彩 uniforms，不能要求 CPU 預染色。測試保留全部四種 ME／bundle 來源組合，逐一檢查兩張原圖的 row／bytes 與獨立色彩。
- sampler：原 fixture 宣告有舊 NormalMap runtime sampler，卻沒有材質 NormalMap 綁定，被正確當成殘留資料清除。補齊有效 binding；並依已保存的原生 MainItemPlus property schema 驗證不支援的 Texture2／Texture3／liquidmask 不可套 ME texture 或改 sampler。所有 role 與 Repeat／Clamp／Mirror 控制仍保留。SamplerR2 的第二次失敗亦保留，不刪掉失敗項目。
- 5 個 Ushiwakamaru 眼影／UV4 診斷依賴已不存在的 `mesh_dump/outfit4/Ushiwakamaru_0` 歷史資料。改列顯式 ignored、附 fixture 原因；沒有宣稱它們通過，也沒有用其他卡或其他 outfit 代替原對照。將來提供正確 fixture 後須用 `--ignored` 明確執行。
- DXBC exporter 原來在預設 mm_base（不是含 DXBC 的 shader bundle）要求 DXBC，且會寫入共用歷史 ShaderDxbc 目錄。改為顯式診斷工具：要求 `OK_DXBC_BUNDLE` 與不存在的新 `OK_DXBC_ROOT`，即使手動執行也不可覆寫旧輸出。實際用原生 EC shader bundle 匯出488 blobs／24 shaders，exit0。此前精確重現曾觸發共用 ShaderDxbc 的 index.tsv／shaders.txt 改写，不能宣稱此輸出索引保留；舊 Audit JSON 原檔保持不變，之後使用獨立新目錄。

## 驗證範圍

FinalR2 的 blend 9、ColorMask 1、sampler 1、SQL 6、portable 3、mods 3：共23個測試通過、0 failed，另外2個原有 installed fixture tests ignored。AlphaCacheR1 的透明／確實不透明輸入與 cache roundtrip 另1項通過。ShaderExportR1 的顯式原生 shader 匯出1項通過。GuidsR2 的現有安裝效能測試另1項通過。

BuildR1 已成功完整編譯 Editor，102.79秒。RawModsR1 雖然 Automation Success，資產重開卻顯示少1個輪廓材質與4616個輪廓三角形：`bOutlinable` 的透明材質排除規則把 MainAlpha 的獨立 One/Zero Outline pass 也排除。這不是可接受的 final；保留 R1 資產与 log，修正通用 shader-kind11 的獨立 outline 入口，既有 `_outline` uniform/外輪廓繪製路徑維持。它不代表 MainAlpha Outline fragment/WPO 的全部像素已驗證。BuildR2 完整建置 actual0，41.98秒。

RawVanillaR1、RawModsR2 均 Automation Success / actual0，新程序實際RHI VerifyVanillaR1、VerifyModsR2 均actual0。原版17Static/11Skin/39Mat/33Tex，共102assets；模組卡57Static/11Skin/158Mat/85Tex，共313assets。模組卡195914個Static三角形恢復原數值，19914個Skin三角形、300morph/91970有效delta、15mipchains/230sourcelevels維持；UV/colors/構造亦通過。ShaderOverwriteR1顯式重試既有輸出被拒絕actual101，index SHA256不變。所有匯入使用 Stage365 的只讀測試卡與新 Stage366 目的地，不覆寫 Stage365/361 的資產與 Audit。

不是全庫523項通過，也沒有驗收5個缺歷史資料的診斷、GPU最終像素、任意mod、完整controller或完整應用Shipping。`KKImportedCharacterActor`仍Static snapshot。操作仍見 [原始KKS卡匯入](CARD_IMPORTER_NATIVE365.md)。
