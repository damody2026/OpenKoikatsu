# Stage343：原生 renderer 狀態與 body display overlay

## 原生狀態覆蓋

將 `run_renderer_native.ps1` 加入 outfit 0–6 選擇及 `StateOnly`。Probe 按原生 `ChaControl.ChangeCoordinateTypeAndReload` 切換 coordinate，等待 loadEnd 與 reload 後更新，再保存每個 renderer 的完整路徑、activeSelf、activeInHierarchy、enabled、inactive ancestors、shadowCastingMode。StateOnly 不重建 body/eye textures，不先改變被觀測的材料。

菲比六套原卡 outfit0/1/2/3/5/6 均完成，clothesState 九項皆0，來源卡 SHA 與 Stage342 原始 dump audit 一致。每個 owned Studio 產生完整 fixture 後因 delayed quit 由 runner 停止，exit=-1，不能記成正常 exit0。Probe 已撤出。

`Tools/audit_renderer_states343.py` 只去除 Studio 的 `CommonSpace/` 父節點，以完整路徑＋mesh＋vertex count＋material 精確連接，沒有名字模糊兜底或忽略缺項。`Saved/Tests/RendererStates343/StateCoverage.json` 封存證據與 SHA：

| raw missing 分類 | 數量 |
|---|---:|
| 原生 inactive 或 disabled |92|
| 原生 active，但 ShadowsOnly |6|
| Studio 原生場景不存在 |18|
| 原生 active，runtime 缺失 |1|

head o_tang 與 body o_tang 同名、同 vertex count，卻是不同完整路徑與相反啟用狀態，不能合併。o_shadowcaster 原生 active、ShadowsOnly，不能叫 inactive，也不能宣稱 UE 投影等價已認證。

18 absent 是六套各一個 `Highlight_o_body_a_rend`、`Highlight_cf_O_face_rend`、`Height bar indicator`。原生 plugin source `KKS_SliderHighlight/SliderHighlight/SliderHighlightPlugin.cs:342` 和 `KKS_HeightBar/HeightBar/HeightBar.cs:153` 證實是 Maker 產生的輔助物件；本輪僅證實 Studio 不存在，不將 Maker/其他狀態的渲染差異改標 PASS。

唯一 active missing 是 outfit0 `o_emblem_sailor01_01_a`。原生 material `_MainTex=null`，但 null 本身不能證明 invisible。`ChangeCustomEmblem`（原生 ChaControl.cs:3182）會寫入 null；目前 loader 的 apply_clothes_emblems 因無選擇貼圖而移除 mesh。讀取 mm_base 與衣服 bundle 沒取得該 shader，空查詢不是 invisible 的證據，因此再做原生 coverage 正控制。

`Emblem343Native` 按真正 palette-space mesh 界限 framing，保留原生 shader/所有 uniforms：source 與關閉 renderer 的 hidden PNG全圖max差0；只在clone material將MainTex換白，positive對hidden max42、514441個像素改變。此正控制證實 geometry/frustum/native shader能畫，而source null MainTex在此原生實際視角沒有任何color contribution。`NativeCoverage.json`封存PNG/state/material/probe來源SHA。不是按名字或null猜測，不修改原始missing記錄，也不擴張為其他shader或shadow全量認證。owned Studio29632完整fixture後delayed quit exit=-1，Probe撤出。

完整 raw 結果仍為64 matched／116 different／117 missing、all_dumps_match=false，分類不修改原始 audit。

## 共通 overlay 根因與修正

原生 `ChangeSettingNip`／`ChangeSettingUnderhair`（ChaControl.cs:4715/4755）透過 `GetTexture` 按 list 取得 Texture2D 後交給 draw material；GetTexture 依 hiPoly 選擇 `_low`。Native 六套 materials.tsv 證實 body `_overtex1`、`_overtex2` 為 Linear、Bilinear、Clamp、10 層 mip。

舊 runtime overlay 缺少來源 sampler，載入固定 SRGB=true；PNG 仍保留 authored Texture2D 行序。舊 outfit0 bytes 按 Linear native gamma-readback 域解讀且翻行後最大差1/6；不翻行時為72/255。這證明不是逐卡顏色問題。

新增 shared `load_catalog_display_texture`：從同一 selected catalog/bundle/name 取得 PNG 與 sampler，保留 Sideloader PNG 行序、翻轉 authored Texture2D 到 runtime PNG 行序；low-poly 使用 `_low`。body overlay 保存 filter/wrap/srgb/mip metadata 到既有 SessionPart runtime_textures，沿既有序列化/FFI傳遞。UE 依 metadata 載入 overlay，材質 texture sample 依 SRGB 選 Color/LinearColor，保留來源 wrap；只有無 metadata 舊資料使用原本 Clamp fallback。

SESSION230／CLI340／CACHE108，避免舊快取遮蔽修正。沒有卡名、圖片或區域判斷；未改原卡與 portable/Gallery 邏輯。

## Overlay 最終驗證

cargo check PASS；UE final build Succeeded33.72秒。debug 六套匯出1PASS（694.72秒），final release fixture `Overlay343FinalRuntime` 六套1PASS（10.32秒，含最終low-poly guard來源）；sampler session-cache control1PASS。

`audit_body_overlay343.py`：12個 sampler與六套native metadata一致、fresh outfit0 gamma readback max1/6（固定門檻6，未放寬），前版錯誤sRGB與行序負控制max136/255，176parts全部positions／indices／UV與其他所有texture bytes不變。無AlphaMask的套裝要求前後presence一致，不製造假PNG。

`Overlay343Final` 菲比＋三男卡16圖、owned UE53088 exit0；骨骼palette、60 attachment rows error0、material/GPU/view/shadow/frozen-frame/camera readiness、影像內容皆PASS。已目視全部16圖contact sheet，未見本輪可見退化，對前版全圖meanRGB差0–0.09618/255。source/staticlib/DLL SHA在CaptureBinarySources.json封存；退出後CaptureEndBinary.json確認DLL仍同hash。其後独立portable thread可rebuild，不以較新DLL冒認本次capture來源。

`Tools/audit_native_overlay343.py` bounded acceptance PASS，結果與來源SHA見 `Saved/Tests/RendererStates343/Acceptance.json`。final raw重新跑仍64matched／116different／117missing、all_dumps_match=false。本輪認證overlay sampler/domain/rows、strict renderer狀態歸因、原生null徽章color coverage與載入回歸；不宣稱scene/shader所有像素或所有原生狀態零bug。shadow-only投影等價、Maker helper scene、face hoho其他draw inputs仍非本輪全面認證範圍。所有本輪owned程序已結束，窗口釋放；保留獨立portable shape/headGPU修改。
