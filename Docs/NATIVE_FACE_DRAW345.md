# Stage345 原生 face draw 通解

## 問題與修正

ChaControl 的 ChangeSettingLip、ChangeSettingEyeShadow、ChangeHohoAkaRate 在 CreateFaceTexture 後設定 draw material 的三個獨立槽。舊路徑將 hoho 用固定 RGB 提前混入 MainTex，把眼影放入 lip 槽，且 session 清除了 head 的 shader extra textures。這是共用流程缺漏，不以卡名或圖片區域修色。

現有 shader_textures map 保留 lip／hoho／eye shadow 三槽，來源 sampling 與 MaterialEditor 最後覆寫；hoho 保留 prefab RGB、只按 status/config 改 alpha。selected catalog property 為 0／空就不載入，不借 MainTex 或同 ID vanilla row。CACHE108、SESSION232、CLI342；未新增持久化欄位。

UE procedural 與 skeletal mesh 路徑保留四組 UV，COLOR0.r/b 參與原生 gate。Koikano/main_skin 的 ST 在 VS、gate 前；Shader Forge/main_skin 的 overlay ST 在 PS、gate 後，不能共用前者順序。菲比實際是後者。原生證據：ChaControl.cs 4172/4185/4222/7820、Koikano 0071/0072 DXBC、Forge 0001/0002 DXBC。

## 已完成的檢查

- 六套 outfit0/1/2/3/5/6、176 parts。18 overlay colors 誤差小於 1e-6；六張 hoho L0 RGBA 與 dump 完全相同；null lip/eye-shadow 沒有假貼圖。FaceInputAudit.json PASS。
- 與此次前版 Runtime 比較，全部 positions/normals/indices/UV 不變，所有非 face 貼圖 bytes 與 sampling 不變。這避免使用歷史版本中其他 head 修正造成假回歸。
- 新鮮原卡 native probe SHA 與實際卡一致、47 renderers；最終 face hoho L0 與 runtime 完全相同。Studio delayed quit，runner 終止自己的 PID80260，exit=-1；不是正常 exit0。
- Rust 三個 slot/cache/selected-null controls PASS（0.63 秒）；十個同時 session、正反載入、20 mutations、destroy/reopen PASS（231.41 秒 release）。包含六套菲比、三男卡、重複卡。
- UE final build Succeeded37.15 秒；ActorResourceLifetime、MultiCharacterIsolation、NativeFaceDrawStreams 三項 Success、queue empty、exit0。實際十個同場 actors 及 procedural 四組 UV。

## 截圖驗證狀態

最終 DLL 的 CaptureR2 owned PID28872 已完成七卡、七 outfit、四視角，共49 tasks／196 PNG，實際 exit0。材質／GPU／相機／影格／陰影 readiness、49 PV 與1073 attachment rows、影像內容全數 PASS。七張完整 contact sheets 已目視檢查，未見空圖或明顯匯入回歸。StartBinaryR2/EndBinaryFinal 的 DLL、LIB 與六項封存 source SHA 相同；Acceptance.json 是本輪 bounded PASS，沒有全面 native pixel 宣稱。

第一次用 PowerShell 直接呼叫 GUI exe，shell 在啟動後即返回0、capture中止；CaptureExit.txt 不是完整執行成功。R2 改 Start-Process Hidden、持有 owned process、WaitForExit，讀實際 exit code。

## 保留差異與下一步

全 raw dump 仍64 matched_inputs／116 different／117 missing，all_dumps_match=false。Face MainTex 尚有 max6/255；不能把 overlay 輸入通過說成所有 shader／光照／pose／scene 像素一致。ME PNG L0 與 native RT mip-chain 不同，完整 mip parity 未認證。gag eye 動態 alpha 與 headGPU 實際 vertex-buffer parity 尚未列入此輪驗收。

FaceCreate 下一步依真正 GPU create pass 驗證線性色彩域、cheek/lipline/paint/mole 的完整 UV 與混合。FaceCreate346Controls 的七項原生正控制已與獨立編譯的 face_create.rs 共用 sampler/transfer helpers 比對：color-only、cheek、lipline、paint1、paint2、mole、combined 全部 max1/255。這是合成器驗證，尚未接入部署，不冒充卡片完整匯入通過。
後續Stage346原生FaceCreate已完成有界驗收，詳NATIVE_FACE_CREATE346.md與Acceptance346；六套MainTex max6降至1，raw差異分類仍保留。
