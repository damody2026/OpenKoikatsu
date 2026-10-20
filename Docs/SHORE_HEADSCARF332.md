# Stage332 守岸人頭巾色差檢查（2026-10-01）

目前：完整 MainItem 程式移植後，SESSION212/213實際四視角的頭巾內側已恢復藍紫星點。下文保存 SESSION209 舊失敗與診斷；最終完整37卡回歸仍需通過，不能把 fragment math parity 當作全部 Unity render passes 一致。

## 可重現證據

- 遊戲圖：`C:/portable/KoikatuSunshine/mesh_dump/kks_守岸人_abmx修复.png/SnowShot_2026-09-30_23-00-37.jpg`
- UE 圖：`Saved/DebugShots/Clothes331All37R2/Tpose/kks_守岸人_abmx修复_outfit0_face.png`
- 原生頭巾材質：同一 dump 的 `outfit0/mesh_2_ChildOf-N_move.materials.json`。
- 匯入材質：`Saved/Tests/Shore332Source/Materials.json`；source diagnostics R2 PASS。
- `python Tools/GamePoseProbe/audit_shore_headscarf.py` 可重跑。輸出 `Saved/Logs/Shore332HeadscarfAudit.json` 含來源 SHA256，區分 source 檢查 PASS 與 visual mismatch 未修復。

## 已確認與排除

1. **R2 更正：真正頭巾為 `erjh`，GameObject `ChildOf-N_move`，使用 `Shader Forge/main_item`。** 首輪把 `o_acs_head_headband_00` 認成頭巾，實際是頭髮；其零誤差只證明頭髮貼圖一致，不能當頭巾證據。修正 audit 對象後，真正頭巾的 MainTex 2048×2048 全 RGBA 最大誤差仍為 0；不能用角色調色或換 PNG 補丁處理。
2. 原生第二材質 `Welt.MECopy1` 使用 `main_item_studio_alpha`。匯入 part19 名稱為小寫 `welt.mecopy1`，shader kind4、translucent，`_alpha=0.7811049818992615` 與 dump 0.781105 相符。首輪用大小寫敏感 `Welt` 過濾會漏列，R2 改用 renderer 身分完整列出。
3. C++ `Part.ShaderKind != 4 && ...` 已排除 studio_alpha 的 TwoSided。原生 KKS shader 的 Cull Back 與這條件一致。先前看到 hair kind1 的 TwoSided 不能推論 ME copy 也被雙面化；不得修改成角色特例。

## 尚待處理

追查共用 Forge MainItem 的最終著色；頭髮 ME copy 是另外的 renderer，不再當頭巾透明層。主貼圖來源完全相同只排除 MainTex 重建錯誤，不能當最終 renderer RGB 正確。

## 完整 MainItem 程式移植（進行中）

- `Shore332SceneRampR3`（SESSION210）與 `Shore332NullNormal`（SESSION211）都產生四視角、exit0，內側仍黑，不能算修復通過。
- 舊使用者 dump 的 `_NormalMap=NULL` 不是目前原生綁定。新 `Shore332HoodNative/Card0.json.materials.tsv` 兩個頭巾材質有 1024²、11 mips NormalMap RenderTexture；卡片 saved `NormalMap` 為788093 bytes。不得以舊 dump 強制刪除正常的 ME NormalMap。清除來源無 slot 的殘留圖後，ME 再指派正常圖，因此 SESSION211 對此卡沒有色差修復效果。
- `Tools/generate_forge_item332.py` 保留原版 DXBC 精確立即數並產生生產程式；raw-scan 0007/0005 對應 native metadata 12/14，並核對每個 texture/sampler 配對，兩種 ID 不能混用。
- `Tools/forge_item_oracle332.py` 和 `Tools/forge_item_oracle332.cpp` 驗證不含螢幕陰影／含螢幕陰影兩版，各44控制、704 pixels（544保留、160discard），WARP 原版與 HLSL 最大誤差0。此結果只證明同輸入下 fragment math，不證明 UE 最終畫面或陰影相機一致。
- SESSION212 / CLI323 改用共用完整 Forge MainItem Forward 程式，所有此 shader 的原始 MainTex / ColorMask / 材質參數分開保留，停止預先烘焙顏色；原版自行進行色相、ambient、diffuse、specular、line 和正反面計算。
- Unity screen shadow 資源由 UE 既有共用陰影相機的四點深度 visibility 供應；這是外部引擎資源轉接，不是 Unity shadow-map 像素 parity 認證。保留此限制，重畫與其他角色回歸通過前不發佈正式截圖。

## R2 原生隔離與共用修正

- 原生 Probe `Shore332HoodNative` 隱藏 renderer2 (`erjh`) 後整個頭巾消失，確認實際對象；`Shore332Native` 隱藏 renderer0 是頭髮。兩次 fixture 都有66 renderers，Studio delayed quit owned stop exit=-1，不能寫正常退出。
- UE `Shore332NoOutline` 四視角 exit0，內側仍黑，排除描邊遮蓋。這是控制試驗，不是關閉描邊的生產解法。
- 安裝 EC shader 與既有 `main_item/0005.dxbc` bytes 相同，SHA 與 metadata 保存在 `Shore332CorrectRenderer.json`。原生 Forward cb2[0] 為場景 `_WorldSpaceLightPos0`，cb0[20] 為 `_RampG_ST`；signed NdotL 同時作用在 U/V（PS 164–176）。UE kind8 原先用相機方向光、V固定.5。
- 修正 kind8 共用場景 light / signed雙軸Ramp / 材質ST，SESSION210、CLI321。未用卡名或像素修色。最終陰影、色相、specular 等未因這兩處修正而自動通過；必須重畫驗證。
- 首次一般 build LNK1104：另一個未由本輪擁有的 UE 背景 capture 鎖住正式 DLL，未終止它。單模組 suffix build 可連結但啟動時主模組仍 import 舊 DLL，造成 duplicate KKAnimationCatalog class，capture exit3。不能把這次當回歸通過。R2 同時重建依賴它的 OpenKoikatsu suffix 模組，保留原進程。
- shader inspector 第一版假定 LZ4 block metadata 都是陣列，EC source 有 scalar，TypeError；R2 正規化 scalar / list，原始 byte比對通過。
