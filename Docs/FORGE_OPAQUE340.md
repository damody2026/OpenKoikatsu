# 原生 Forge MainOpaque 通用著色與服裝指派修正

2026-10-01，接續菲比六套 outfit 審核。目標是共用 shader 與原生服裝流程，不改卡片／圖片／個別材質顏色。

## 真正根因

Stage339 從原卡 ME 黑色與舊 dump 藍色的差異，暫時保留「狀態不同」解釋；這不足以解釋畫面。Stage340 用既有 GamePoseProbe 從確切原卡建立新的 CharaStudio 角色，等候插件載入後擷取：`hair_acc._ShadowColor=RGBA(0,0,0,1)`，但原生畫面中的十字髮飾仍是藍紫色。原卡、native、runtime 的黑色設定正確；UE 舊 kind2 簡化陰影／diffuse 路徑錯誤。

`Saved/Tests/PhoebeME340Native/Card0.json.materials.tsv` 與 `.face.png` 保留數值及影像。Native 47 renderer fixture完成後 Studio未在15秒內退出，runner核對 owned PID23724／exe後終止，exit=-1；這是有完整輸出的受控終止，不宣稱正常退出。暫時 probe 已撤出，未儲存卡片或場景。

## 實作

- 從已安裝 MaterialEditor `EC_Shaders.unity3d` 取 `Shader Forge/main_opaque` 原始 DXBC，驗證快取 bytes 與安裝 bundle 一致。raw-scan10／12對 native binding12／14；CB大小28/5/1，texture/sampler binding逐項核對，保留 gamma property與預設 texture。
- 新增 `KKForgeOpaque` 與生成的 Program／Bindings，接入 common native material renderer。保留 `_MainTex/_DetailMask/_LineMask/_NormalMap/_AlphaMask/_AnotherRamp/_Texture2/_Texture3/_liquidmask` 原始輸入與 ME 優先序，避免 legacy CPU染色、二次法線轉換／翻面、extra texture丟失。確切 shader優先於 inherited hair hint；未支援的 opaque變體仍使用既有路徑，不假稱原版程式適用。
- MainOpaque 的 shadow-map sample接既有 UE 深度視圖；完整 fragment數學直接按原版程式，不以髮飾顏色或卡名猜補。外部相機、ramp、ambient、燈光仍是引擎對接條件，固定輸入GPU parity不等於兩引擎同場景全部像素相同。
- `ChangeCustomClothes` 按 normal01/alpha01→normal02→normal03→accessory依序重播。缺RT1／RT2不退用RT0；重複群組保留最後成功寫入，後續RT缺少則保留較早成功寫入。只改renderer第一材質，其他材質及不相干群組不變。
- SESSION219／CLI330失效舊輸出，prefab schema／portable schema不更改；獨立 APNG/Gallery 改動保留。

## 驗證

`ForgeOpaque340GpuParity.json`：兩個程式各44 controls／704 pixels（正反面、UV/ST、顏色、normal／cutoff、ramp、shadow、specular及black ShadowColor），原始DXBC對重建HLSL最大誤差0，固定門檻2e-5；每個程式錯ShadowColor負例確實被拒絕。原生黑ShadowColor仍有非零diffuse，排除「黑色就應整個髮飾全黑」假設。

第一輪 UE build Succeeded44.61秒；fresh `PhoebeOpaque340` 四視角exit0／材質及逐視角GPU readiness成功，沒有shader compile failure。目視十字髮飾藍紫色，红領帶保留，角色完整。最終 cache／重部署／六套 audit結果在驗收完成後追加。

## 最終驗收

- 新runtime六套176parts，120 native有效值全部PASS；raw audit變為118different／62matched／117missing，all_dumps_match仍false。原始dump所有條目保留，不把部分修正改称全部資料完全一致。
- 原生RT寫入順序／缺失RT／重複群組控制1PASS；all-role cache往返、resolved MainOpaque優先hair hint、null MainTex按ShaderLab white預設控制1PASS。Null material不能因缺少MainTex而退回舊簡化著色。
- `ForgeOpaque340Final`：菲比與KK_384914兩卡共8張900×1080、exit0、per-view material/GPU/frozen-shadow gates PASS。KK_384914兩件MainOpaque服裝確實進入原版程式（log uniforms30/textures7），並非只在髮飾上套用。
- 加上空主貼圖控制後重新部署，`ForgeOpaque340FinalNullUEBuild.log` Succeeded38.81秒；`ForgeOpaque340Deployed` 最終DLL重新擷取菲比四圖，exit0、目視四視角與內容檢查PASS。十字髮飾診斷ROI藍色像素由舊0到406；該ROI僅用影像驗證，production完全沒有像素位置／卡名分支。紅領帶、側背髮飾細節保留。
- 先前兩卡截圖使用的DLL SHA另存 `CaptureBinary.json`，最終DLL/source/staticlib與fresh deployed截圖/log SHA存 `Saved/Tests/ForgeOpaque340/Acceptance.json`；避免將舊build圖像冒稱新build。Native probe已撤出，owned Studio/UE/cargo皆已結束，正式Screenshots未覆寫。
- 完成共用MainOpaque fragment與此次RT邊界修正；未驗所有shader變體、全部mods、scene light／SH／camera／worldpose全面等價。MainOpaque只處理確切已驗證shader，未支援變體不借用此程式。
