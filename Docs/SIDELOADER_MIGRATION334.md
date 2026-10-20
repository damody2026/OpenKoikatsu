# Stage334 原生 StripAll migration 與衣服花紋解析

計畫：先用 fresh native 對照查明兩筆回退的真正載入來源，依 installed manifest 與 UniversalAutoResolver 實作共用規則，做完整 Create RT controls，再部署及37卡回歸。

七海千秋3的兩筆衣服 CPU fallback 並非遊戲使用PNG而 importer缺GPU解碼。卡片保存 kks.mt_pattern_30、slot313，但安裝的 `[KKS]KK_Backport_Compatibilty.zipmod` 對它宣告 StripAll。原生 `UniversalAutoResolver.MigrateData` 優先於 MigrateAll 清空 GUID，CompatibilityResolve 保留可用的原生內建313；fresh native `_PatternMask1` 是 RGB24、Linear、11mips、Bilinear/Repeat/aniso1。舊 importer 忽略 migration，誤用 `chara/sunshine/mt_pattern_30` 的 sRGB PNG。

共用實作：讀 root manifest schema1、排除非 Sunshine game，收集 StripAll；尊重 Sideloader `Migration enabled=false`。在選各來源之前清除被 strip 的 resolve，保留保存的數值欄位，走現有內建相容解析。不限角色或花紋編號。花紋 resolve 同時要求 outfit、九種衣服 property、色彩通道與 ID，對齊 StructReference；褲襪的原生 property 拼字是 ClothesPantsPattern，不能猜成 Panst。

部署 identity：SESSION215 / CLI326-native-stripall / CACHE105。Sideloader config stamp 同時納入 SQL session 與檔案 export cache，mod inventory stamp 本來已存在。沒有更改既有 PNG 回退門檻或 shader 算式。

驗證：

- StripAll manifest/game/schema/comment/quote controls PASS；花紋錯 outfit/part/channel/ID 負例 PASS。
- `Resolve334Native` fresh native 與 `Resolve334Source`：MainTex/MainTex02 × original/shift/alpha/combined，8項、8,388,608 pixels，RGBA byte最大差0。最終來源檔案及 SHA 記錄在 `Saved/Logs/Resolve334PixelAudit.json`；R1 證據保留。
- `Resolve334Native` Studio owned PID53092，fixture完成後 delayed quit停止，exit=-1只表示程序關閉方式；診斷插件已移出遊戲。不可將退出碼當像素失敗。
- Rust build及UE build PASS；新37卡預熱全部 PASS、CPU fallback0。首次回歸因發現一般ShotPreview未凍結眨眼，停止owned PID74768，保留96張反證，不發布。修正共用capture freeze與每張frozen-frame gate後UE Frozen build PASS，重新驗證Siesta及全37卡。

截圖差異不能直接推論材質/mesh退化：`OK_SHOT_BLINK=half`只設定當下狀態，Actor Tick若繼續執行，後續四視角拍到不同眨眼時刻。新增的凍結只用於ShotPreview截圖，不改正常動畫preview。詳ERROR E-211。

37卡預熱已全部exit0、CPU fallback0，`Resolve334PrewarmAudit.json.full_clothes_gpu_passed=true`。固定half的Siesta單卡四圖與frozen-frame/PV/readiness/附件稽核PASS。R2半閉眼批次保留作診斷、主代理核對owned PID81244後停止；正式展示採統一open，tag `Resolve334All37Final`，不以半閉眼診斷表情當卡片外觀。額外Siesta FrozenR2 half獨立重載驗證再現性。

Siesta Frozen與FrozenR2各自四張frozen-frame/readiness/PV/附件PASS，eyes0.46與brow0.5一致；完整PNG逐byte比較FAIL max55/45/66/69。不得把表情凍結與有效控制一致當整個renderer像素再現max0；這項完整PNG差異尚未定位，原始FAIL保留在Resolve334SiestaRepeatAudit.json。只有Create RT來源八項嚴格max0。

範圍：本輪新增明確缺失的 StripAll 優先規則，未聲稱 Migrate/MigrateAll/CompatibilityResolve所有未知mod情況已完整移植；真正使用PNG的其他資源仍保留明確回退。完整 authored mips transport、fractional-alpha ZWrite、native outline/外部screen shadow 等既有界線仍存在。

最終驗收及發布（2026-10-01）：`Resolve334All37Final` 使用 SESSION215／CLI326／CACHE105、統一 open blink，UE 正常 exit0，37卡148張900×1080。PV37、骨矩陣最大誤差2.252e-6cm、附件869筆最大誤差0cm，material/GPU/view/shadow/frozen-frame 五項門檻全部通過，沒有新增未預期 Error。Contact01–10逐頁目視，並放大 Android18、KK226176、Siesta側面及江之島盾子2檢查；龍卷圖案／材質、守岸人星點頭巾、Siesta背髮、眉毛／眼眶線、頸飾與三男角均保留，未發現本次修正新增的可見退化。

`Resolve334All37FinalAudit.json`／`Resolve334All37FinalRegression.json`／`Resolve334All37FinalVisualReview.json` 與 zero-fallback 的 `Resolve334PrewarmAudit.json` 以 SHA 綁定同一來源／截圖；嚴格 publisher 通過後正式 `Saved/Screenshots` 已更新148張，原136張先複製到 `Saved/Screenshots/old/Resolve334All37Final` 並逐檔核對 SHA，再發布新圖。`Resolve334All37FinalPublish.json` 保存前後與備份 hashes。owned UE PID62744 已正常結束。此發布表示指定37卡的來源及顯示回歸通過，不代表上述未完成的 renderer 完整像素一致性已解決。
