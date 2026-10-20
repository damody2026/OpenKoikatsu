# 三月七 / C001 皮膚偏白修正

## 計畫
1. 以使用者指定 mesh_dump 材質輸出、原始卡與 C001 APNG 核對來源，不修改卡片顏色。
2. 分開核對來源貼圖色彩空間、主光方向、陰影與高光；以來源 shader 指令為依據，不以曝光倍率掩蓋差異。
3. 同鏡頭保存修改前後 3200×2000 畫面，分別驗證原始卡與 APNG，回歸 KK_384914。
4. 編譯與實際成像檢查，記錄錯誤與未解差異。

## 定位進度
- 使用者指定 mesh_dump/[Card] March_7th_keiyuu.png 是匯出目錄，並非 PNG 檔案；原始卡位於 UserData/chara/female，C001 verified receipt 的 source hash 相同。
- 原作匯出 body/face 使用 Koikano/main_skin。皮膚與頭髮原先分別使用相機正面光／Studio 主光，這不是原作 shader 的共用 _WorldSpaceLightPos0 行為。
- 原作主光 snapshot：Unity _WorldSpaceLightPos0=(0,0.6427876,0.7660446,0)，換成 UE 軸後為 (0,0.7660446,0.6427876)。Koikano main_skin DXBC 0072:333–344 也用此方向計算高光偏移與 Lambert。
- 本輪先修正 skin 使用既有 Studio 主光；等待實際前後畫面驗證，尚未宣稱完整根因或顏色一致。

## 材質與輸入核對（續）
- 主貼圖縮放至 512 後，原作匯出與 runtime RGB MAE 為 0.01757/255；DetailMask 逐像素相同。因此不能把偏白歸因於低解析度或貼圖顏色。
- 原作 shader globals 確認主光方向為 (0,0.6427876,0.7660446)，ambientshadow RGB=128/255，alpha=1，KanoVerShift=1。
- 編輯預覽接入共用 KoikanoSkin 原始像素程式、uniform layout 與 binder，避免手寫近似程式漏掉 ramp / ambient 計算。R5 已能實際渲染，但視覺仍有差異，尚未通過顏色驗收。
- 新增 skin legacy resolved 值至原生 uniform 的映射，編輯預覽與 cooked runtime 共用；不修改角色卡。資料例：材質 prefab 色值與卡片載入後色值確實不同。
- 新增 OK_SKIN_INPUT_AUDIT 記錄預覽實際使用的法線、細節法線、線條遮罩與來源 metadata。
- 截圖工具新增 -Portable，以空原作目錄開啟 APNG，避免把原始卡路徑測試當作 APNG 獨立載入驗收。
- 原作 oracle 新增 -RawRender，在手動相機渲染期間停用相機 MonoBehaviour 後還原，分離後製對畫面的影響。

## 尚待驗收
- 新版編譯、來源法線核對、C001 獨立 APNG 的 3200×2000 截圖。
- 色彩與陰影視覺差異消除前不得標記完成；編譯成功或產生四張截圖僅代表管線可執行。
- KK_384914 回歸與使用者 CardReader 重新啟動。

## 反編譯追蹤（使用者指定來源）
- kks_decomp/Assembly-CSharp/ChaShader.cs:214–223：shaderType 對應 _KanoVerShift=0/1，遊戲角色 ambient RGB=(.019,.027,.039)、alpha=depth。
- Studio/SystemButtonCtrl.cs:1166–1175：Studio 改用場景 ambientShadow，alpha=1-ambientShadowG；兩個場景的規則不能混用。
- Studio/SystemButtonCtrl.cs:1465–1472：SetACE 依 dicFilterLoadInfo 的 bundlePath/fileName 載入真正的 LutTexture。
- AmplifyColorBase.cs:748–795：依 activeColorSpace、Camera.allowHDR、Tonemapper 選 shader pass，再套 Exposure / LUT / BlendAmount。因此像素 shader 一致不能代表最終顏色一致。
- BetterColorPicker/BetterColorPicker.cs:96、252：內嵌 lookuptexture 是螢幕取色補償查表，不能把反推表當成原作顯示 pass 完整實作的證明。
- 已新增原作 display.tsv / display-lut.png 擷取與 native-display 對照，追查共用顯示 LUT 在原作與 UE 中的色彩空間。

## 反編譯後的單變量驗證
- **確認場景全域輸入混用**：目前預覽採用 Studio 的 `_ambientshadowG=(128/255,128/255,128/255,1-shadowDepth)`；人物編輯器的 `Manager/Character.cs:409–412 → ChaShader.cs:219` 則設定 `(0.019,0.027,0.039,shadowDepth)`。相同 shader 並不代表相同場景輸入。
- 本機 system.xml 的 shadowDepth=0、shaderType=1。因此兩條路徑分別產生 alpha=1 與 alpha=0；不能只替換 RGB，或把 alpha 的意義混用。
- Native Oracle R7 對同一卡、同鏡頭、同主光、同 LUT 僅呼叫原作 `ChaShader.ChangeAmbientShaodwColor`：`native-display.png` 與 `character-display.png` 顯示後者恢復明顯陰影與較深的粉色層次。證據位於 Saved/Tests/MaterialColorMarchNativeOracle2xR7，量測 scene-global-ab-report.json。這證明全域輸入影響，不代表 UE 最終顏色已通過驗收。
- 已排除「UE LUT 未生效」：C001 3200×2000 的 LUT on/off 對照，RGBA MAE=18.43148/255，最大差140；背景由 (128,128,131) 變為 (151,159,155)。Saved/Tests/MaterialColorMarchNoLUT2xR1/lut-ab-report.json。
- 已排除「UE5.8 預設切到 ACES 所以忽略 legacy LUT」：Engine/Private/Scene.cpp:440 預設仍是 Filmic；不可因 ACES 分支存在就直接修改 tonemapping method。
- 真正 native 32³ LUT 經 RGB sRGB 讀回解碼、Y 翻轉，以同座標三線性取樣後，與現有16³表相當接近。例如輸入(.95,.85,.8)：native=(250.05,208.03,196.11)，目前=(249.75,209.19,196.50)。LUT 有近似誤差，但目前證據不支持它是大幅偏白的主因；不更換全域 LUT。
- 修正方向：把「人物編輯／遊戲」與「Studio」的陰影設定作為明確場景輸入，共用原始 shader；保留 APNG 的來源材質資料。不能依單一卡片調曝光、改皮膚貼圖，或把所有場景硬切到另一組 shaderType。
- 尚需確認使用者原作畫面的 shaderType 與實際場景主光；R8 額外單獨測試 character globals + shaderType0，避免把柔和陰影與色彩差異混成同一原因。
- R8 已完成：character-globals.tsv 確認原作實際讀入 `(0.019,0.027,0.039,0)`、shaderType=1；另捕捉 mode0。mode0 主要改變輪廓／邊緣光，仍保留明顯硬陰影，因此不能宣稱切換 mode0 即可重現使用者柔和陰影。原作 oracle 使用 Studio 主光，完整人物編輯器的光照與 shadowDepth 仍需另外對齊。
- R7/R8 snapshot_complete=true；原作 Application.Quit 未於5秒內結束，工具最後僅終止自身啟動的 Studio PID。探針 DLL 已移除；未修改原作 system.xml、未修改 D:/UE5.8。

## 本輪結論
確定存在人物編輯器／Studio 全域陰影輸入混用；貼圖資料、LUT 未套用與 ACES 預設假設已排除。新增的對照探針已編譯並在原作成功執行，且所有臨時全域值均 finally 復原。此次完成根因調查與可重現證據，**不是最終 UE 色彩一致驗收通過**；後續修正需把人物編輯器實際場景光照與陰影設定一起接到共用材質，而不能只改一個 RGB 或降低曝光。

## Minimalist 補充與穿衣陰影檢查
- **後續更正**：原作 mt_ramp 清單已確認 Minimalist 是 Ramp，不是後製 LUT；此節早先關於「需查找 Minimalist LUT」的假設已撤回。實作與證據見 CHARACTER_MINIMALIST_NO_SHADOW.md。
- 使用者已確認參考畫面使用 Minimalist。此前擷取的 native LUT 尚未以 filter 名稱／資源路徑確認是 Minimalist；「目前 LUT 接近擷取表」不代表接近使用者指定的 Minimalist。最終濾鏡顏色結論需保留此限制。
- 穿衣原作 R3 同鏡頭 source／ambient-alpha000 對照：只改全域陰影 alpha，白色袖子下緣出現冷色陰影、裙褶變明顯，頸部與腿部的明暗交界也改變。這證明問題是跨材質的場景輸入，不能以單一角色材質補色修正。
- 本次僅核對陰影，未改正式場景參數。穿衣 A/B 位於 Saved/Tests/MaterialColorMarchNativeOracle2xR3/Card0.json.renderer54.source.png 與同目錄 ambient-alpha000.png；此組停用後製，用來隔離陰影，不能當作 Minimalist 最終成像驗收。
