# Stage342：來源法線色彩空間與身體合成稽核（本輪驗收完成）

本輪按 MaterialEditorPluginBase.ConvertNormalMap、ChaControl.CreateBodyTexture 及 TextureExporter 的讀回方式追查共同流程。沒有卡名、outfit 或像素區域特判；固定 normal readback 門檻仍為 6。

## 已確認與已實作

- 原生 normal 命名不保證來源是 Linear。保留 authored sampler metadata，對 sRGB source 解碼一次，對 native shader 的 sRGB RT 表示再编码；legacy adapter 保留線性值。
- 使用 authored compressed mip payload 做 D3D11 GPU 讀回，避免 CPU BC 解碼誤差。讀回目標依 DXGI source view 選 sRGB/Linear；服裝既有 bake 仍明確使用 sRGB 目標。CACHE108 / SESSION224 / CLI334；223 為中間驗證版。
- UncensorSelector OOBase.mainTex/colorMask 按選定 descriptor 載入，男角 mask 預設 cm_body_00_mc。body create 保留 skin alpha。
- 新增 effective normal readback 與精確 renderer native probe 比較工具；保留 raw audit，不把表示等價或隱藏候選當全面 PASS。
- 新增 opt-in BodyCreate 原生探針，重建並保存 create_skinbody 輸入/顏色/paint layout/純 shader 合成，以確定差異發生在哪一步。

## 中間證據與否定的假設

NativeNormalBody342FinalRuntime 六套 176 parts：raw 118 different / 62 matched / 117 missing，all_dumps_match=false。54 normal effective checks 中 51 PASS、3 panst FAIL max8。前版 sRGB CPU correction 有6 FAIL，max8；更前版 max68–138。門檻不放寬。

新鮮 PhoebeState342Native 的 47 renderer fixture 完整，但 Studio 延遲退出後 runner 終止 owned PID82772，exit=-1，不能聲稱正常退出。新鮮 probe panst 正規化仍 max7。原生與 runtime body MainTex RGB max182；custom OOBase source 成功載入，故不能把載入 custom body 當 RGB 差異已修好。alpha 背景236已匹配。直接從原卡 Custom/body 解碼保存 BodyCardFields.json：paintId=[0,0]、sunburnId=0，不能先判定「非零 paint 漏套」就是本卡根因；仍需確認 id0 原生 asset 與合成鏈。

NativeNormal342LinearGpuControlsR2：sRGB 與 Linear source 行序/RGBA 原樣讀回 1 PASS。初版測試仍期待 Linear 被 gamma 編碼，在目標讀回語義修正後 FAIL；R2 改成驗證兩個來源 domain 各自保留 bytes。新的六套 SourceViewsRuntime 正在重跑，尚未列通過。

## 中途未完成清單（以下最終驗收已完成本輪 normal/body 項目）

- SourceViewsRuntime 54 normal checks 與新鮮 native 比較。
- BodyCreate 原生輸入定位、必要的通用 body compositor 修正。
- final 靜態庫/UE build、回歸與來源/二進位 provenance；舊223截图不能當224驗收。
- 117 native missing 條目仍須分別核對父物件 active、服裝狀態及輔助 renderer；不能整批排除。世界pose與完整shader parity未認證。

## 2026-10-02 行序定位

實際 Sunshine MaterialEditorPlugin 覆寫 ConvertNormalMap，不能只看 API base method。TextureContainer 的 saved PNG 已是 RT ReadPixels 行序；舊 helper 又翻轉，造成 panst 3項max8。SESSION226 六套176parts的54 normal checks全PASS；新鮮 PhoebeBody342NativeR3 法線也PASS。

原卡 KSOX BodyUnder/Over 各一張2048²，與probe讀回PNG逐bytes相同。BodyUnder要先轉到 authored base row domain，最終body Blit再反轉；BodyOver已在final RT domain。此共同修正使body182降到17；未使用卡名或區域補丁。

第一輪body native探針誤將updateCMBodyTex等public properties當fields，故沒重載ColorMask；R2改GetProperty取bool[]重跑完整。R3另保存原生BodyUnder/Over，三輪fixture完整但runner皆延遲quit後停止owned程序exit=-1，不計正常退出。

後續確認mask雙線性插值不可用最近點推論顏色domain：忽略插值曾誤推sub/nail是linear向量。全green區域native pure直接否定該推論；所有RGB colors需gamma→linear，MainTex則依實際Linear authored source或KSOX temporary sRGB RT取樣，alpha不做transfer。新增Linear8中途量化也使directional control失敗，改保留overlay sRGB intermediate再於body shader取樣解碼，不放寬control門檻。最新SESSION229/CLI339六套與部署仍待最終驗證。

## 最終數值（2026-10-02，截圖驗收收集中）

NativeTexture342FinalRuntime SESSION229/CLI339/CACHE108 六套176parts匯出1PASS（277.75秒）；法線54/54PASS、有效draw inputs120/120PASS。六套原始dump body MainTex maxRGBA皆2，新鮮exact-card body maxRGB=[2,2,2]、alpha0。這是全部body像素數值比較，不是ROI或逐圖補色。body_create兩控制、BodyUnder directional控制、normal十控制全PASS；UE build Succeeded47.49秒。

全量raw audit現為64matched /116different /117missing，all_dumps_match=false。原始差異仍包括PNG表示、動態／其他狀態及未覆蓋項，不能整批改標PASS。最新capture NativeTexture342Final四卡16圖正在收集，DLL/source hashes在CaptureBinarySources.json封存；舊fixture226/227與先前223截圖不用作最終binary驗收。

## 本輪最終驗收完成

`python Tools/audit_native_texture342.py` PASS，Acceptance.json保存六套body max2、54 normal、120draw、176parts、4卡16圖、source/binary/log/image SHA。owned UE77872正常exit0，material/GPU/view readiness與影像內容gate通過；16圖均已目視，沒有灰屏或本輪可見匯入退化。新的腿部BodyUnder/Over細節按原卡恢復。DLL與capture開始封存hash一致；沒有把後續portable/APNG改動的source當capture來源。

範圍：法線source/ME override domain与行序、六套body compositor、load/RHI regression。原始raw仍116different/64matched/117missing、all_dumps_match=false；未聲稱隱藏／其他狀態全量覆蓋、所有shader/worldpose/fullscene零bug。Body paint/sunburn非零完整合成仍不是本輪認證範圍。native probe為完整fixture但exit=-1，不冒稱正常exit0。

最終source版本CACHE108/SESSION229/CLI339。所有本輪owned cargo/UE/Studio結束，probe撤出，共享build/capture窗口釋放。未覆寫原卡或正式Screenshots。
