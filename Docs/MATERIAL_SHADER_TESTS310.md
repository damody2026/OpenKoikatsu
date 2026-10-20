# Stage310：安裝版 MainItemPlus 共用 shader 載入（SESSION167→170）

## 目標與來源

按實際 shader 名稱及材質宣告選擇載入流程，不按卡名、截圖顏色或頂點高度修補。使用者延期的下巴輪廓不在本輪範圍。

來源為 KKS mods 的 `[xukmi][KKS] Vanilla Plus v1.7.2.zipmod`，bundle `abdata/chara/xukmi/shaders/vanillaplus.unity3d`，shader `xukmi/MainItemPlus`。本機快取 GitHub shader 比安裝版舊，缺少 ShadowHSV 等參數，不作本輪公式依據。

`Tools/dump_installed_shader.py` 保存 parsed shader、原始 DXBC、fxc disassembly 及 SHA256 到 `Saved/ShaderDxbcVanillaPlus310/MainItemPlus`。`Tools/shader_program_codegen.py` 將 Forward PS blob6、Outline PS blob1 的指令和原始立即數 bit pattern 轉為 HLSL，遇到不支援指令即失敗。生成碼存入插件 Private/Shaders；正式載入不讀 Saved 或截圖。

## 共用實作

- Rust shader kind9 按確切 shader 名稱選取；MainItemPlus 即使帶 hair hint、透明 keyword 或高 render queue，仍依來源 alpha clip 與 pass 行為處理。
- 保留原始 MainTex、ColorMask、AlphaMask、DetailMask、LineMask、normal/detail、ramp、emission、matcap/reflection mask，避免 CPU 先著色或烘焙 alpha 後再由 GPU 重做。
- 卡片顏色／HAC／MaterialEditor 的作用域解析結果寫回材質；ME 大小寫正規化 key 必須覆寫原本 canonical property，不可留下兩份值。
- 新增借用材質值／貼圖 FFI，保留舊 view ABI。source/cache/Session 均保存 native 材質；CACHE102、SESSION170，decoded bundle 不需重抽。
- Unreal masked unlit 材質執行生成的 Forward/Outline 程式，宣告預設值、Gamma 屬性、bool/int bit layout、camera/world basis、UV、normal/tangent 和 global ramp 均在共用 adapter 連接。
- Outline 根據來源 DetailMask.b、camera distance、global linewidth 與 LineWidthS 計算 WPO，CPU offset 為零。

## 分層驗證與限制

獨立 D3D11 WARP oracle 執行安裝版原始 DXBC 與新 HLSL；使用相同 sampler 座標及合成貼圖／cubemap，44 組參數、每組16像素，同時包含 front face、alpha discard、HSV、specular、reflection、emission 及black alpha正負控制。Forward 保留288／discard416，Outline 保留400／discard304；兩者 max error0，既定門檻2e-5未放寬。

這證明指定像素程式的運算移植，不代表所有 shader variants、UE 貼圖 transport 或遊戲 scene 完全一致。UVFlip1／鏡射 rows 的早期 Forward 比較仍有差異，必須與相同座標的 oracle 分開。本輪未移植 SHADOWS_SCREEN 或 point/vertex light variants；Unity scene reflection cubemap 尚未輸入，缺少時使用黑 cube 並記 warning。Normal／靜態 authored MIP／color space 的剩餘來源限制沿用 Stage308，不把截圖成功當 pixel parity。

Rust 三項 scoped 控制已實際執行：outline cache、native item material/masks cache、FFI invalid inputs 不改 output。UE 首次編譯被 MSVC 單個 raw string 長度拒絕；codegen 改相鄰短字串，第二次 build Succeeded7.46sec。RHI 與14卡回歸的實際結果列於下節，不以初次編譯結果代替。

## 本輪失敗紀錄

第一個 RHI smoke 帽子全黑，雖然 compile、load與3張captured gate成功，不能視覺列PASS。原因為非 Properties uniform Epsilon 預設誤填0，白色／無彩 RGBtoHCV 會除0。安裝 DXBC 使用此 uniform，維護者 [KKPItemDiffuse.cginc](https://github.com/starstormhun/KKShadersPlus/blob/main/Shaders/Item/KKPItemDiffuse.cginc) 第102行初始化1e-10；共用 adapter 保留該 initializer，顯式保存材質值仍优先。原36組合成彩色貼圖不足以揭露缺省載入問題，新增白／灰 MainTex與color／spec／reflection控制至40組。鏡射UV／rows獨立問題仍不因這項修正而列PASS。

詳見 ERROR_LOG E-167。每次編譯／oracle／capture 用不同 log/tag 保留；失敗結果不可覆寫成通過。

### RHI 追查後的修正

Epsilon 遺漏修好仍黑，NoOutline控制也黑；前述Epsilon不能概括黑帽根因。Albedo診斷白色輸出證實MainTex與材質連接，真正黑化原因是 null EmissionMask fallback的alpha錯誤。KKS所用Unity版本的 ShaderLab [black預設RGBA全0](https://docs.unity3d.com/cn/2020.2/Manual/SL-Properties.html)，不同於較新版本manual的alpha1。共用fallback改回alpha0後Smoke167R3帽子恢復藍色；不是改底色。gray／red也按該版本定義。

最終oracle擴充44組／704像素，各向正反面與缺省black alpha0正控制／alpha1黑化負控制：Forward288kept／416discard、Outline400／304，max error0。新增控制直接執行原始PS，證實同一程式的黑化由emission mask alpha造成；原40組資料／logs保留BeforeDefaults。

NormalMap原生RT為R8G8B8A8_SRGB；前輪只核對圖片方向，沒有核對UE上傳後的RGB傳輸。`MaterialEditorPlugin.ConvertNormalMap`的default RT、auto mip與wrap已決定native sampler provenance；後續完整比對發現UE原先把線性PNG數值直接標sRGB，詳見下節的SESSION172修正。legacy normal表示法尚待單獨完成。

VS2的o5是normalize(T×N)，adapter曾寫N×T，按原始指令修正；native kind9不使用舊hair name normal反轉。PS oracle使用相同varyings不能證明VS adapter，這項依原始VS指令與RHI確認，證據scope分開。分段顏色替換僅作診斷，完成後已從production與runner移除；保留唯讀Custom HLSL audit命令。

### 場景光源及無 DynamicBone 的 skinned 附件

用GamePoseProbe讀出原生Studio場景的有效layer10 `Directional Chara`：forward `(0,-0.6427876,-0.7660446)`、intensity1、white，角色 shader 的to-light為 `(0,0.6427876,0.7660446)`。舊native adapter把相機視線當 `_WorldSpaceLightPos0`；同一T-pose帽子採樣點原圖近白 `(241,239,240)`，改場景光後 `(148,169,221)`，遊戲照片藍區約 `(132,152,221)`。照片和UE鏡頭不同，這些採樣值只能作色彩趨勢，不能算逐像素誤差。`FOKNativeItemDraw.MainLightToSurfaceUnity`保存Studio預設並可由場景替換，不依卡名。原生證據在 `Saved/Tests/MainItem310NativeLights169/Card0.json.lights.tsv`；兩張UE控制在`Saved/DebugShots/MainItem310Smoke169`和`MainItem310StudioLight169`。

先前把帽子問題歸因CPU附件缺切線也不完整。Rust載入器對無DynamicBone但有SkinnedMeshRenderer權重的附件原本不用`AccessoryRuntime`，走剛體Bake而失去骨架姿態切線；hat的`skel_kind`其實是0，不是GPU 1/2或既有CPU4。現在`from_source`接受這類renderer，私有骨架無物理元件仍會每frame更新位置／法線／切線；FFI kind4把當前切線送UE procedural mesh。`skin_tangents`使用與位置相同的骨架及bind palette，保留authored handedness.w。這是附件類型規則，不是帽子特例。

原生材料控制R2及全表：先測14個，再擴至全部57個numeric effective屬性，max均為 `7.6841e-8`；1853個raw tangent對遊戲Mesh dump最大`2.9757e-8`，全部1853個姿態切線已改變；`manualSkinnedWorldBounds`六個界限max`1.214e-7m`。`worldVertices`標示BakeMesh+renderer Transform與`bakeMeshSpaceGuess=unknown`，會對此SMR重複縮放，不能作畫面幾何golden。Rust定向切線palette測試1PASS、UE Build170 Succeeded；Smoke170 T-pose／Walk各3張正常退出。

### 14 卡 RHI 回歸（170）

四張新增卡與前十張分別在 T-pose 和 Studio Walk 0.4 秒拍全身、正臉、側臉：`Saved/DebugShots/MainItem310Final170` 24 張、`MainItem310Regression170` 60 張，合計14卡84張、四個 UE 程序均 exit0。每張均900×1080，PV audit 每 pose 4／10，附件偏移最大0cm，骨架 palette 最大2.262e-6cm。新卡每 pose 103 個附件，舊卡各 pose 250 個；原 `Saved/Screenshots` 15 張 SHA 未改。ContactSheet 與 SideContactSheet 在 `Saved/Tests/MainItem310Final170` 和 `MainItem310Regression170`。兩份 audit JSON 與 `Saved/Logs/MainItem310FinalAudit170.json` 記錄來源、圖檔及程式 hash。資料載入和顯示檢查通過；`visual_parity_passed=false`。

目視 T-pose／Walk，March 眉毛、Alice 側髮、KK_384283 頭飾與 KK_091207 頸飾未見本輪新增偏位。Korone 衣服亮度、384914 紅衣飽和度、七海髮光細節及 Minase 帽子角狀深藍色塊與遊戲照片仍不同；相機位置不同，不用截圖逐像素作等價宣稱。帽子剩餘差異沿實際 runtime NormalMap 色彩空間追查，不按卡名調整色彩。

### 來源貼圖輸入的全像素對照（171–172）

`Tools/GamePoseProbe/run_renderer_native.ps1 -TextureMips` 從原生Studio同一renderer讀取實際`_ColorMask`、`_DetailMask`、`_NormalMap`；原Probe屬性清單漏DetailMask，已補通用native材質貼圖欄位。來源材質有`_USELIGHTCOLORSPECULAR_ON`和`_USERAMPFORLIGHTS_ON`，Forward變體中的blob6無額外runtime keyword，blob7／8／9分別加`SHADOWS_SCREEN`、`VERTEXLIGHT_ON`及兩者。此處核對的是貼圖輸入；尚未證明來源場景最後選了哪個runtime變體。

| 帽子貼圖 | 原生runtime類型 | 舊UE與原生 max RGBA | 證實的規則／修正後候選 max RGBA |
|---|---|---:|---:|
| `_ColorMask` | Gamma Texture2D，11 mip，Bilinear／Repeat | `[0,6,1,0]` | 已同方向，不額外翻轉 |
| `_NormalMap` | sRGB RenderTexture，11 mip，Bilinear／Repeat | `[0,73,74,6]` | ME的Blit將線性RGB存為sRGB；編碼RGB後`[0,4,1,6]`，alpha原值 |
| `_DetailMask` | Gamma Texture2D，11 mip，Bilinear／Repeat | `[24,235,24,0]` | `ParsedBundle.orient_raw_mask`已定向，native accessory不得再次翻面；翻面候選`[1,5,1,0]` |

完整真卡session在SESSION172重新載入後，三張1024²貼圖全部像素通過單通道差值<=6：ColorMask max`[0,6,1,0]`、DetailMask max`[1,5,1,0]`、NormalMap max`[0,4,1,6]`，三者超門檻像素均0。不是從帽子截圖推色調。`Saved/Logs/MainItem310NativeMaterial172.log`為1PASS，57項材質數值仍max`7.6841e-8`，原始切線與manual bounds的既有gate保留。原生三張完整PNG與sampling在`Saved/Tests/MainItem310HatTexturesFull171`，Session輸入在`Saved/Tests/MainItemPlus310/MaterialTextures167`。Production以精確shader family判斷native program，不按卡片名決定轉換；legacy accessory bake保留原路徑。

### SESSION172 Unreal 驗收

`Saved/Logs/MainItem310Build172.log`為Succeeded。`Saved/DebugShots/MainItem310Final172`四新卡24圖與`MainItem310Regression172`舊十卡60圖，T-pose／Walk共84張、四個owned UE均exit0。每pose新卡4PV／103附件、舊卡10PV／250附件，附件最大偏移0cm，palette最大2.262e-6cm；15張原`Saved/Screenshots` hash保持。兩組ContactSheet／SideContactSheet與兩份Audit JSON在對應`Saved/Tests`及`Saved/Logs` tag目錄；前版170圖和report完整保留。

目視172正臉，Minase帽子先前大塊角狀深藍色條紋已消失，帽面變連續藍色；原生照片與UE鏡頭不同，色調／反光仍不可當逐像素相符。側面帽頂仍有亮色區域，照片缺同角度證據；Korone粉紅衣亮度、384914紅衣與七海髮光等剩餘差異亦未由本輪宣稱解決。Alice側髮與飾品、KK_384283頭飾、KK_091207頸飾的Walk正臉／全身未見退步。`Saved/Logs/MainItem310Compare170to172.json`把84張與上一版UE同名圖比較：舊十卡60圖平均每圖絕對RGB差的平均為0.05452/255，最大單圖平均0.17030/255；差值只量UE回歸，不量遊戲照片parity。

目前`visual_parity_passed=false`。MainItemPlus的`SHADOWS_SCREEN`／`VERTEXLIGHT_ON`、場景reflection cubemap、UV transport oracle、authored MIP／LOD及其它材質家族仍需個別來源對照；此14卡兩姿態不能外推所有mods或衣服狀態。下巴輪廓依使用者指示保留延期。

### 剩餘衣服色差的來源分層

Korone粉紅主衣與KK_384914紅色主衣在game dump均為`Shader Forge/main_opaque`，不是本輪的`xukmi/MainItemPlus`。`session_renderer_tests::diagnose_native_clothes_maintex_inputs`以原生運行時RenderTexture PNG與SESSION172實際主貼圖做全1024²像素比較：Korone RGBA最大差`[1,2,1,0]`、平均`[0.0130,0.0160,0.0135,0]`；384914最大與平均皆0。垂直翻轉的平均差分別約`[20.56,16.43,16.45,20.08]`和`[14.44,4.24,5.02,0]`。`Saved/Tests/MainItem310ClothesInputs172.json`及`Saved/Logs/MainItem310ClothesInputs172R2.log`保存真卡控制。這排除兩件主衣的MainTex漏載、雙重染色或上下翻面作為色差主因；剩餘光照／陰影差須分別比對`Shader Forge/main_opaque`來源ForwardBase、場景光與UE kind2輸入，不按角色把粉紅／紅色硬調。此控制只涵蓋兩件可見主衣的MainTex，不代表其它衣服紋理或最終shader像素都通過。
