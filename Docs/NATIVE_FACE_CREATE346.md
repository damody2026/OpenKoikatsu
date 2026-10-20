# Stage346 原生 FaceCreate 通解

本輪將臉部主貼圖合成接到遊戲的 `Koikano/create_skinface` DXBC。沿用 CACHE108，沒有新增持久化欄位或卡名分支。最終來源版本 SESSION237 / CLI347；正式結果以 `Saved/Tests/FaceCreate346/InputAcceptance.json` 與最終驗收檔為準。

## 合成與來源

按 ChaControl 的 MainTexAB、ColorMaskAB 欄位選取來源，明確的 0 保持 null，不能借用 head MainAB。來源 Texture2D 保留壓縮格式、mip、filter/wrap、aniso/bias；Sideloader PNG 建立原生 mip chain。依來源類型處理影像列方向，而不是從嘴部或角色外觀推測。

八個 texture、十七個 constant vectors 對應原生 PS。Color RGB 轉 linear、alpha 直接使用；依序合成 cheek、lipline、paint1、paint2、mole，保留各自 UV/layout 與 paint mask。KSOX under/over 的處理順序沿用 hook 邏輯。MaterialEditor MainTex 最後覆寫；旧近似合成僅由明確診斷環境變數 OK_FACE_LEGACY=1 啟用。

mm_base 必須逐資產解析：partial override 只含服裝時，不能遮掉 vanilla 的 cf_m_face_create。材質與 shader 均分別確認存在再採用覆寫。這是共通 asset resolution 修正，與特定卡名無關。

## 原生正負控制

`FaceCreate346MipsR3/GpuAcceptance.json`：十項 GPU 控制（含 null-main/null-mask、兩 paint、mole、combined、縮小取樣）各 RGBA 最大差不超過 1/255。原生 128→1 PNG 八層 mip bytes 完全相同；只取 L0 的負控制最大差 61，確認測試確實覆蓋 mip 使用。七項 CPU reference 控制最大差 1/255。上述控制使用 clone，不修改原卡 renderer。

最終驗收要求六套服裝全部 176 parts 的幾何、四 UV、effective values、runtime sampler 及非 face MainTex 圖片 bytes 保持一致，並要求正式 export 沒有 face compose failed 或 GPU failed。不能把 cargo 程序成功或微小色差當成合成成功。

## 失敗保留與範圍

235 的 RuntimeFinalR2/CaptureFinal 保留為失敗證據：程序與拍攝 readiness 雖成功，partial override 選錯卻使六套臉部進入皮膚備援，嚴格 sampler 比較抓出問題。停用快取的成功結果不能代替正式路徑；237 修正後必須重新正式匯出與拍攝。詳細錯誤見 Docs/ERROR_LOG.md E-224。

既有多人隔離與 native face draw 的封存結果見 MULTI_CHARACTER344.md、NATIVE_FACE_DRAW345.md。本輪原始 dump 的 unmatched/different 分類仍保留；完整場景姿勢、光照、所有 shader/NPOT mip 的逐像素零差尚未認證，不能宣稱所有 dump 完全對上或整個專案沒有 bug。

## 最終驗收（2026-10-02）

`Saved/Tests/FaceCreate346/Acceptance.json` PASS，SESSION237 / CLI347 / CACHE108。正式 release build、UE build R4 成功；RuntimeFinalR3 六套匯出 1 PASS / 253.58 秒。InputAcceptance：176 parts、435 項非臉部圖片 bytes、所有 runtime sampler/effective values/四 UV/幾何保持一致；六套 face MainTex 原生 RGBA 最大差均為 1/255，fresh native 比較同為 1/255；沒有合成備援或 GPU 失敗。

最終 UEControlsR4：ActorResourceLifetime、MultiCharacterIsolation（十同場 actors，含六套菲比、三男角與重複角色）、NativeFaceDrawStreams 三項 Success，實際 exit0。R3 缺 fixture 的失敗 log 保留，重跑明確設定 OK_MULTI_GAME/OK_MULTI_CARD；新增可重現的 run_face_create346_tests.ps1，缺 fixture 立即失敗。

`Saved/Tests/FaceCreate346Final`：七張卡各 outfit0、四視角，28 PNG、exit0；材質/GPU/每視角 GPU/凍結姿態/陰影/相機 readiness 及影像存在檢查全部 PASS。7 項 PV 與 135 項 attachment 顯示誤差均 0 cm；七卡四視角合併 contact 已目視檢查。capture 前後 14 項 sources/LIB/DLL SHA 相同，並與目前檔案一致。Stage345 的全 outfit 49 tasks/196 PNG 封存不覆寫。

六套 raw dump 仍為 64 matched_inputs / 116 different / 117 missing、all_dumps_match=false，全部 runtime parts 能映射；原始「完全對上」判定仍未通過。這份驗收證明上述通用合成、退化與多人所有權範圍，不把 inactive renderer、不同編碼或完整場景像素差異改寫成全面通過。
