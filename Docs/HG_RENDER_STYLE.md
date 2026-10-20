# 渲染風格：原作 ramp 與 HG

畫面風格分三類，風格值存在 `player.json` 的 `render_style`，也可以用 `POST /render {"style":...}` 切換：

| 風格值 | 內容 | 切換方式 |
|---|---|---|
| `koikatsu` | 每張角色卡自帶的陰影 ramp（原本的樣子） | 即時 |
| `ramp:<key>` | 原作「圖形設定」裡的陰影 ramp，共 49 種：原版 13 種，加上 mod 36 種（Minimalist、Very Slight Anime、dak 逆光、koikoi 邊緣光、PH アニメ調等） | 即時，直接替換所有材質的 `_RampG` |
| `hg` | 下面說明的 HG 卡通 | 存 `auto` 後重開地圖 |

## 原作 ramp

- 來源：`Tools/ok-extract` 的 `ok-ramps GAME_ROOT Content/KKPreview/Ramps`。它沿用 `configured_ramp_png` 的解析方式，讀類別 432 的清單（原版 abdata 加上所有 Sideloader zipmod），輸出線性空間 PNG 和 `ramps.json`（key、原名、來源 bundle、雜湊）。
  - 這次從 `D:\KoikatuSunshine` 匯出 49 種，與原作探針清單 `Card0.json.ramps.tsv` 一致。
  - Minimalist 與既有的 `KKPreview/MinimalistRamp.png` 逐像素相同。
- key 命名：原版為 `kk_<id>`；mod 為 `<guid>_<本地 id>`。
- 名稱：49 種在 `ui.toml` 的 `[ui.settings] ramp_<key>` 都有三種語言的譯名（mod 加註作者，如「極簡陰影（Augh）」）；之後新匯出、還沒翻譯的 ramp 會顯示遊戲原名。
- 執行時：
  - `OKHGShading` 註冊 `ok.RenderRamp`，負責讀取清單（selection 的 `ramp_catalogue`）和 PNG。
  - crowd runtime（`OKCrowdRuntimeScene.cpp`）會記住每個材質原本的卡片 ramp，切換時換掉 `_RampG`；換回 `koikatsu` 就還原。
- HG 風格關掉了 KK 自陰影，所以 ramp 在 HG 下沒有作用。

## 攝影棚（風格對照照）

`OpenKoikatsu.exe /Engine/Maps/Entry -RenderStudio=<studio.json>`（`Source/OpenKoikatsu/OKRenderStudio.cpp`）：

- 用引擎的空地圖，不碰 StreetSocial 場景。由 `UOKRenderStudioSubsystem` 在帶有 `-RenderStudio=` 時生成 `AOKRenderStudio`。
- 載入 selection 的角色，把指定的女角排成一排（`spacing`），播站立待機、眼睛張開。
- 每種風格 × 每種主光（`lights`：指向光源的方向，UE 空間）各拍一次，存成 `<style>.<light>.png`。
- 背景是純灰（`background`）：先用 LDR 拍一次取得最終色調，再拍一次 HDR，用它的不透明度把空背景換成灰色，半透明邊緣按覆蓋率混合。
- 棚燈透過 `OKSetCrowdRuntimeStudioLight` 套到所有材質，HG 也會改用同一盞燈。
- HG 用的是另一套材質，要用 `"render_style": "hg"` 另外啟動一次。
- 拍完寫出 `studio.json` 後結束；150 張 4800×2000 約 3 分鐘。

2026-10-15 的成果在 `Saved/StreetSocial/Studio02/`，設定為間距 95 cm、4800×2000：

- `row_front.jpg`、`row_side.jpg`、`row_back.jpg`：五人全景，51 種風格各一格。
- `closeup_1..5.jpg`：每位女角一張，每種風格的正面、側面、背面三種光並排。

# HG 渲染風格

角色有兩種畫面風格：

- **Koikatsu**（預設）：原本移植自 Unity 的 KK shader。
- **HG**：卡通風格，由 `Plugins/OKHGShading` 提供。

參考對象是 `EndfieldToonShader` 專案的整體效果，但沒有搬它的程式碼或貼圖。那個專案的公式大多翻譯自遊戲本體的 shader，授權不明；它要的每角色貼圖（FaceSDF、打包 Mask、UV2 平滑法線）KK 角色也沒有。

## 做法：重新上光，不重寫材質

每個 runtime 材質家族（61 種）原本就有一段翻譯自 Unity 的程式，負責算貼圖、ColorMask、KK 自己的高光。HG 版的 parent 照樣跑同一段程式，只是：

1. binder 以 `EnableShadows=false` 綁定，`_ShadowColor` 為白色，所以程式輸出的是**沒有自陰影的表面色**。
2. 在程式最後 `return o0` 之前，嵌入 `Plugins/OKHGShading/Shaders/Private/OKHGRelight.ush`。這段在材質產生時就內嵌進 Custom 節點，所以 cook 和遊戲執行時都不需要這個外掛的 shader 路徑。它負責：
   - half-Lambert 套窄 smoothstep，切出兩階明暗。陰影是染色（布料偏冷、皮膚偏暖），不是單純變暗。
   - 受光側的 Fresnel 邊緣光。
   - 頭髮加一條 Kajiya-Kay 各向異性高光帶。髮絲方向取 Bitangent。
   - 臉：`__OK_HG_Face=1` 的零件（role `head`）把法線往角色正前方拉（`__OK_HG_FaceFlatten`），避免鼻子、臉頰切出零碎陰影。這是沒有 SDF 時的替代方案。
3. 主光預設跟著鏡頭（`__OK_HG_ViewKey`）。關掉時改讀該家族自己的 `_WorldSpaceLightPos0`；沒有這個 uniform 的家族用預設主光。

哪些家族要重新上光由 `OKHGSurfaceOf` 決定：

| 類別 | 家族 |
|---|---|
| 皮膚 | KoikanoSkin、ForgeSkin |
| 頭髮 | ForgeHair、ForgeHairFront、KoikanoHairSun0-7 |
| 不處理，保留 KK 原樣 | 眼睛、眼白、眉毛、鼻線、眼鏡、IBLDepth、Dither、Emission、OilPainting、Goo、BetterMatcap |
| 一般表面 | 其餘所有家族 |

描邊（`_Outline`）沿用 KK 的。

## 可調參數

都是 parent 上的材質參數，MID 可以在執行中改：

| 參數 | 預設 | 用途 |
|---|---|---|
| `__OK_HG_Enable` | 1 | 0 = 只去掉自陰影，不重新上光 |
| `__OK_HG_ViewKey` / `__OK_HG_KeyDir` | 1 / (-.5,.55,.65) | 1 = 主光跟著鏡頭（右、上、朝鏡頭），從哪個角度看都有明暗；0 = 用材質的 `_WorldSpaceLightPos0`。街景給多數家族的是固定主光，給皮膚的是鏡頭光，所以預設用鏡頭相對主光 |
| `__OK_HG_ShadowThreshold` / `__OK_HG_ShadowSoftness` | .52 / .035 | 明暗交界位置與寬度 |
| `__OK_HG_ShadowTint` / `__OK_HG_SkinShadowTint` | 冷灰藍 / 暖粉 | 陰影染色 |
| `__OK_HG_RimStrength` / `__OK_HG_RimPower` / `__OK_HG_RimColor` | .32 / 4.5 / 暖白 | 邊緣光 |
| `__OK_HG_HairSpec` / `__OK_HG_HairSpecPower` / `__OK_HG_HairShift` | .22 / 72 / .12 | 頭髮高光帶 |
| `__OK_HG_FaceFlatten` / `__OK_HG_Face` | .72 / 0 | 臉部法線拉平（由遊戲設定） |

## 產生與打包

```
Tools\lua\lua.exe Tools/prepare_runtime_shared_material.lua --tag StreetHG01 --hg-shading
```

這個指令輸出 `Content/IndependentResources/RuntimeMaterials/StreetHG01`。產生後還要兩步：

- `Launch/materials.json` 的 `hg_root` 指向這個根目錄。
- `Tools/rebuild_street_social_patch.lua` 會把 `root` 和 `hg_root` 一起 cook 進同一個 patch 和 shader library。

`prepare_street_social_selection.lua` 會把 `hg_root` 寫成 selection 的 `hg_material_root`。

## 切換

- 主控台變數 `ok.RenderStyle`（0 = Koikatsu，1 = HG），由 OKHGShading 註冊。角色載入、建立材質時讀取（`KKPreviewRuntimeMaterials.inl`）。
- StreetSocial：
  - 設定欄的「畫面風格」下拉選單，或控制通道 `POST /render {"style":"hg"}`。
  - 選擇存在 `player.json` 的 `render_style`。
  - 切換時先存 `auto`，再以它重開地圖，因為材質只在載入時建立。
- 找不到 `hg_material_root` 時，會記一行警告並沿用 Koikatsu 材質。

## 目前狀態（2026-10-15）

- 材質根目錄：`StreetHG03`（2026-10-19 起，和 Koikatsu 風格的 `StreetDisplay24` 一起換；外框粗細走原作外框頂點程式）。之前是 `StreetHG02`／`StreetDisplay23`。
- patch：`Saved/StreetSocial/Build/StreetDisplay24HG.patch`，已寫進 `Launch/runtime.json`。`StreetSocial_P.pak`（地圖與引擎內容，不含母材質）是從舊 patch 手動複製過來的，`rebuild_street_social_patch.lua` 不會產生這個檔。
- 執行檔：`HGStyle02.stage`。
- StreetSocial 的角色走 crowd runtime（`OKCrowdRuntimeScene.cpp`）。KKPreviewActor 那條路徑也已接上，供編輯器以外的 high model 使用。
- 已在遊戲內驗證：HG 開機載入、`/render` 切回 Koikatsu（存 `auto` 後重開地圖，位置保留）。

## 之後可做

- 每種 KK 頭型離線烘一張臉部 SDF 陰影圖，取代目前的法線拉平。
- 角色自陰影：可以搬 Endfield 的自陰影 atlas 作法。但它需要 `r.EarlyZPass=2`、`r.SkeletalMesh.UseCachedMDCs=0`，還要替換 skeletal proxy，會影響全街景效能，要先實測。
- 用平滑法線（UV2）外擴描邊。
