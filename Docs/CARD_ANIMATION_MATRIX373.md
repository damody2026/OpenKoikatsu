# Stage373 男卡、多 outfit 與無蒙皮髮型的完整動畫匯入

本輪接續 Stage372，擴大至 ill_male_02.png 與 KK_224305.png 的所有 outfit。每張卡依序做原卡匯入、兩個原作動畫匯入、獨立資產重開驗證、壓縮動畫播放驗證、原生全 section 頂點對照，最後用無 parser／來源包的隔離專案驗證。

## 通解與原作依據

原作 ChaControl.ChangeHairNoAsync 在 ChaControl.cs:5821 取得 RefObjKey.HairParent，LoadCharaFbxData copyWeights=0；ChaReference.cs:228 對應 cf_J_FaceUp_ty。無蒙皮 MeshRenderer 的髮型仍應跟隨這個 Transform，不能因沒有權重便視為不能播放。

現有 rig_mesh fallback 已將這類髮型烘焙到 world space，但只在 GPU 預覽模式保存 rigid_follow_bone。原卡資產匯入採 CPU SMR 來源，遺失這個 metadata，造成 Male02CardR1 actual1。修正使 CPU/GPU 保留相同原作掛接資訊，parent_bone 清空，避免重複掛接；不依卡名、mesh 名或性別推測骨骼，也不補造皮膚權重。既有 exporter 以保存的完整 reference matrix 與動畫當幀完整 matrix 求 delta，再展開三個 affine alias bones，保留非等比縮放/shear。

Session pipeline 更新為246，decoded cache108維持不變。舊 session 不再遮蔽新掛接 metadata；既有 uassets、封存 Sources/Binaries 與 Audit 不覆寫。

## 可重用驗收

- Tools/card_animation_cases373.json 宣告卡路徑、来源、clips、outfit；-1代表全部 outfit。
- Tools/run_card_animation_matrix.ps1 接收任意 cases JSON，以 fresh RunTag 產生計畫、原卡 SHA256 與逐項結果，全部五步完成才回報成功。
- Tools/run_asset_audit.ps1 支援指定 Stage、Source，分別要求每種 Mode 的完成標記，保存實際 process exit，timeout只清理自身 PID/EXE/log 一致的 process。
- Tools/run_detached_animation_matrix.ps1 只複製計畫中的已保存資產資料夾及 KKImportedAssets runtime plugin，不帶來源包或 parser；環境變數在 finally 還原。
- Runtime automation 不再硬編碼兩個性別／四個 clips，逐一覆蓋宣告的 library 中所有 clip/outfit；檢查 null sequence、duplicate library、compressed data、Unreal clock、finite vertices、完整部件與 RequiredBones。
- Tools/audit_card_animation_matrix.py 要求五種模式的真 exit0、原卡 SHA 不變、outfit/clip 完整覆蓋、所有 native comparisons低於原 .05cm，且隔離播放成功，才保存新的 audit與来源/DLL快照。

## 最終驗證

本輪宣告矩陣完成。RustR2實測 CPU/GPU 的任意無蒙皮髮型 world placement、非等比縮放、掛接來源及無二次掛接通過；BuildR1 actual0。Male02 五項完整驗收 actual0：全4 outfit、69部件（19、18、17、15）、364 assets、8個原作clips；CardVerify 全69 SM+69 SK、361851 triangles；Verify 138 skin instances、471940 vertices；NativeVerify 2831640次頂點比較，max .006482028cm（.06482028mm），保留原 .05cm門檻。R2Male224305Card提前結束，actual0卻沒有全部完成標記，正確被判定失敗；不是完整匯入。完成outfit0–4的記錄保留，沒有Character.uasset。程序退出的外部原因未確認，不能冒稱OOM。

確認來源生命週期問題後，FCardAssetImport以FGCObject保護所有待保存输出；每個outfit結束立即Close來源session、清理Copies中的來源地址key、等待編譯與GC，釋放已銷毀actor的預覽資源，並記錄memory。持續保留TextureCache與輸出資產。BuildR2 actual0；fresh R3完整矩陣10項檢查全部actual0。Male02重跑全4 outfit後依舊69部件、364 assets，全部UV/morph/顏色/mip/材質與動畫覆蓋通過，native誤差不變。KK_224305全7 outfit、312部件（45、42、45、45、46、44、45）、1584 assets；CardVerify312 SM+312 SK、1324302 triangles、19 native mip chains/264 source mips；14 clips、624 skin instances、2038694 vertices；12232164次原生頂點比較，max .006049211cm。兩卡共381部件、22clip/outfit項、15063804次頂點對照，最大 .006482028cm。

每個outfit source released log皆存在；KK_224305回收後memory依序4588.1、4922.9、5530.2、6198.2、7146.5、14418.0、15611.4MB。此為回收點觀測，不能當peak或推論原先退出原因。DetachedR1 Automation Success、actual0：僅KKImportedAssets runtime plugin與輸出資產，無parser、無IndependentResources；22 clips、762 skin instances、2510634 vertices，所有部件覆蓋、compressed valid、clock advances、fractional seek/finite vertices通過。隔離host位於Saved/Tests/AssetImport373/DetachedR1/DetachedAssets.uproject。

PriorFemaleR1 actual0：使用最新parser重開Stage372 KK_398530已保存資產，57部件、2 clips、1536012次原生頂點對照；max .001669370cm與前輪完全一致。兩張新增男卡加女卡回歸共16599816次原生頂點對照。

ExistingRunR1預期拒絕：相同RunTag在launch前拒絕，原R2.plan.json SHA前後相同，不覆寫證據。三個runner經PowerShell parser檢查，Python auditor可編譯，git diff --check通過。

Docs/ERROR_LOG.md E251 保留 Male02CardR1 與 RustR1 的失敗。Stage372 Audit SHA256 維持 BA97A11AE2F3C73C323060DD73B2EBDFA463EEADBA9D7486F7772B7CD7E87002。這是宣告測試矩陣的驗收，不宣稱所有卡、GPU 最終像素、完整 controller 或 Shipping 全部無 bug。

## 在 Unreal 使用已驗收資產

- Male02角色：/Game/IndependentImports/Stage373R3Male02Card/Character.Character；動畫：/Game/IndependentImports/Stage373R3Male02Animation/Animations.Animations。
- KK_224305角色：/Game/IndependentImports/Stage373R3Male224305Card/Character.Character；動畫：/Game/IndependentImports/Stage373R3Male224305Animation/Animations.Animations。
- KKImportedCharacterActor指定Character、AnimationLibrary、Outfit與對應AnimationIndex，勾選PlaySkeletalAnimation。所有outfit均可播放各自的walk／stand。
- Saved/Tests/AssetImport373/R3.plan.json保存原卡SHA256與來源；R3.results.json保存10項真exit0；R3.audit.json封存資產、測試記錄、來源/DLL快照及前輪封存檔核對。

重現：Tools/run_card_animation_matrix.ps1 -Cases Tools/card_animation_cases373.json -RunTag <fresh>；再以對應plan執行Tools/run_detached_animation_matrix.ps1。保留所有失敗和舊資產，禁止以同tag覆写。
