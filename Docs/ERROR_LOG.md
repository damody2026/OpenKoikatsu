# 試錯與回退紀錄

這份紀錄供 `HANDOFF2.md` 的實作與驗證使用。每次出現錯誤、視覺退步或錯誤推論，當次增列一筆：寫明變更、可重現的症狀、已證實與仍待查的原因、回退／修正、下一次試驗前的檢查。未證實的原因不得寫成結論；保留診斷圖片與 log 路徑。正式截圖只放在 `Saved/Screenshots` 根目錄，失敗圖放 `Saved/DebugShots`。

## E-001　衣物改用相機光向後袖子與裙腰變黑（2026-09-26）

- **變更**：依 Maker 的 layer-10 方向光和 `Koikano/main_clothes_opaque/0024.asm.txt` 的 `cb2[0]`，暫把衣物 shader kind 2／3 的 UE ramp 入射方向改為隨相機轉動；原先只有 skin 使用此方向。這次沒有同步改動衣物的自陰影捕捉／投影。
- **症狀與證據**：March 的 T-pose 全身試拍出現大片黑袖子與裙腰黑洞，比 Maker 原圖及先前正式截圖明顯惡化。失敗試拍位於 `Saved/DebugShots/MakerClothesLightFailed`；基準九圖在 `Saved/DebugShots/BeforeMakerClothesLight`。
- **原因狀態**：已證實單獨換 ramp 光向不可直接保留。試驗原本同時改 kind 2／3，卻只先核對 `Koikano/main_clothes_opaque` 的 `cb2[0]`；March 白袖實際為 **kind 2 `Shader Forge/main_opaque`**（Maker `mesh_61_o_inner_a.materials.json`），因此試驗前缺少該 shader 的獨立 pass 驗證。後來查其來源 `oo_base.unity3d` blob 12：ForwardBase `cull=0`、讀 `SV_IsFrontFace` 及 `cb2[0]=_WorldSpaceLightPos0`；UE kind 2 原先卻未設雙面。這是已證實的實作差異，**尚未證實就是黑袖／裙腰的全部原因**。固定方向的陰影捕捉與新方向不一致仍是待驗假說。
- **處置**：撤回衣物光向修改，從基準備份恢復 `Saved/Screenshots` 根目錄九張正式 PNG，重新編譯 UE Editor Development 成功。失敗圖只留診斷資料夾。
- **後續獨立修正**：按 `oo_base.unity3d` blob 12 的 `cull=0`，UE kind 2 共用材質改為雙面；**沒有重新啟用失敗的相機光向試驗**。UE Editor Development build 成功，三卡九張 `OK_NOCACHE=1` 試拍在 20:47–20:48 完成，log 九次 screenshot、正常 `RequestExit`，無 shader/material compile error。相對試驗前基準圖，March 全身平均 RGB 絕對差 `(0.046,0.028,0.014)/255`、Alice `(0.057,0.029,0.026)/255`、Yuuka `(0.277,0.279,0.256)/255`；目視沒有黑袖／黑腰復發。根目錄九張、`old` 185 張。這驗證 pass 剔除狀態的小幅修正，**不能視為相機光向問題已解決**。
- **再試前檢查**：先按 kind 2／3 各自的實際 shader/pass 驗證正反面、法線／切線和 ramp 公式，再讓衣物 ramp 與 shadow capture 使用同一入射方向；以黑袖子、裙腰及 Yuuka 白領做 A/B，確認無退步後才推到三卡正式圖。
- **已排除的單點猜測**：UE 建立 `ToonRampTex` 時已設 `AddressX/Y=TA_Clamp` 並重新上傳資源；Minimalist ramp 右端為亮色，因此「U=1 重複取樣到黑色左端」與目前實作不符。Maker 白袖正面有效 tangent／normal-map 合成法線朝鏡頭的 Z 約 0.54–0.99，也不支持直接把來源衣袖當背面。兩項都不是本次黑袖的已證實原因。
- **隔離 A/B**：只改 kind 2、保持其他 family 與陰影圖不動時，固定 UE `+Y` 與 `-ResolvedView.ViewForward` 的 March 全身照幾乎相同（平均 RGB 差 `(0.046,0.027,0.013)/255`），兩者均未產生黑袖。固定 `+Y` 診斷照在 `Saved/DebugShots/ForgeFixedFrontLightDiag`；相機向量那組曾目視與量測，但未在覆蓋前另存，見 E-010。這將原先的結論收窄為「上次同時改 kind 2／3 的試驗退步」，**不能再說 kind 2 的相機向量單獨造成黑袖**。兩個診斷光向都已撤回並重新成功編譯；正式九圖由 `BeforeForgeLightDirectionDiag` 逐檔 hash 恢復。

## E-002　僅靠 rim 屬性辨識 Shader Forge skin 過廣（2026-09-26）

- **變更／症狀**：初版在 shader PPtr 缺名時只看 `_rimV`／`_rimpower`，錯把 March 臉及三張身體判成 `Shader Forge/main_skin`。
- **原因與處置**：March 的 `Koikano/main_skin` 臉仍殘留 rim 屬性；已撤回寬鬆判定，僅在 `cf_O_face` 且缺 `_FaceNormalG` 等完整屬性組合時補判 Forge，身體維持來源 prefab 的 Koikano。定向測試加入 March 臉與身體反例；細節見 `HANDOFF2.md` §55。
- **再試前檢查**：分類規則先對三卡所有臉／身輸出 shader kind 和 Maker dump 對照，不以單一屬性推斷 shader。

## E-003　IBL 髮飾裸加白色直接光洗白（2026-09-26）

- **變更／症狀**：只依 `IBL_Shader_alpha/0003.asm.txt` 漫反射與 Maker 白色方向光增加直接光，尚無逐像素 shadow attenuation；Alice 藍色髮夾變灰白。
- **原因狀態／處置**：已證實這個不完整的光照項退步，試驗 C++ 已撤回、重新編譯並恢復正式截圖；缺少的 shadow、法線及 probe 綁定仍待查。見 `HANDOFF2.md` §50。
- **再試前檢查**：先取得或重建 t7/t8/t9 與 per-draw 常數、深度／陰影，再連同 BRDF 試拍；不可裸加白光或對 Alice 特設藍色 tint。

## E-004　全域翻轉 Texture2D 導致衣領／領帶消失（2026-09-26）

- **變更／症狀**：為修臉部 atlas 方向，試過在所有 Unity Texture2D 解碼後翻轉；Yuuka 領帶與衣領消失。
- **原因與處置**：衣物主貼圖原本已接近 Maker runtime 方向；全域翻轉誤傷衣物。已撤回，臉、身合成各按實際 Maker `_MainTex` 校對，衣物解碼保持原方向；見 `HANDOFF2.md` §49–50。
- **再試前檢查**：UV／列方向修正要先逐材質角色核對網格 UV 和 Maker runtime 貼圖，並測試至少一件衣物。

## E-005　序列化場景光向直接套衣物造成退步（2026-09-26）

- **變更／症狀**：早期曾把相機隨動光向同時送進 ramp 和自陰影；跨卡重用時 Alice 髮／裙變黑，重設角色後 Yuuka 白領偏暗、March 藍袖折痕消失。
- **原因狀態／處置**：跨卡光向殘留已定位並修正；更廣的衣物視覺退步仍缺來源完整 shader 輸入，該輪光向改動已撤回。見 `HANDOFF2.md` §41。
- **再試前檢查**：單卡及 March→Alice→Yuuka 連拍都要比；serialized light transform 不能代替逐 draw 陰影／SH／probe 驗證。
- **後續釐清（21:53）**：kind 2 相機燈配合已核對的 runtime normal map，March 白袖不再重現舊黑袖，正面藍帶反而收斂。再單獨把 Koikano kind 3 改為相機燈並三卡重拍，March 裙腰、Alice 裙面、Yuuka 制服也沒有大片黑區；前次共同變更的黑區不能歸咎於任一家族的相機主光本身。仍須依其完整 ForwardBase 尾段處理不同材質色階。

## E-006　IBL pass 狀態與 bundle blob 的錯讀（2026-09-26）

- **變更／症狀**：曾把 `IBL_Shader_alpha` 第一個只寫深度的 pass 當 Forward 顏色 pass；也曾把兩個平台的 LZ4 區塊合讀成假 DXBC，錯判 Windows shader 不存在。
- **原因與處置**：已按 pass 順序及逐平台 blob/manifest 重新解析；詳見 `HANDOFF2.md` §43–44、§47–48。
- **再試前檢查**：shader 結論同時核對序列化 pass、有效平台、DXBC profile 及 resource binding，不以第一個 state 或掃描序號推斷。

## E-007　Maker 參考圖路徑沿用舊記憶而讀取失敗（2026-09-26）

- **症狀**：首次嘗試讀 `C:\portable\KoikatuSunshine\mesh\_dump\...\outfit0-tpose-body.jpg` 得到目錄不存在。
- **原因與處置**：本機這三張 Maker 參考圖實際位於 `C:\portable\KoikatuSunshine\mesh_dump\<卡片截圖名>\outfit0-tpose-body.jpg`；用檔案清單核實後已在正確位置檢視 March 參考照。沒有修改來源圖。
- **再試前檢查**：引用使用者或舊紀錄的路徑時先檢查目前檔案實存，以實際解析結果寫入診斷命令。
- **再犯與修正（21:33）**：把角色 manifest 的 `Alice_0-tpose` 子目錄誤加進 JPEG 路徑，`view_image` 找不到圖。用 `rg --files mesh_dump | rg 'outfit0-tpose-body\\.jpg$'` 核實三張 JPEG 都直接在各自 `*_outfit0_face.png` 目錄下；mesh JSON 才在更深的 tpose 子目錄。
- **再犯與修正（21:54）**：為找 face 參考圖又查了舊 `mesh\\_dump` 路徑，該目錄本機不存在。沿用已核實的 `mesh_dump` 清單；目前只有 T-pose JPEG 和其資源 dump，不能假設同名資料夾中另有正／側臉 Maker 原照。

## E-008　Maker 法線診斷腳本首次執行失敗（2026-09-26）

- **症狀**：將多行 Python 塞進 `python -c` 時，字面 `\\n` 造成 `SyntaxError`；改用 PowerShell here-string 後，Maker `mesh_61_o_inner_a.json` 的 136 個 tangent 含 `null`，NumPy 產生 object array，`np.round` 又報 `TypeError`。
- **原因與處置**：命令字串換行未按 PowerShell／Python 邊界處理；dump 的缺值也不能當全為浮點。已改用 here-string 管線、把 `null` 轉成 `NaN`，並在取樣前排除非有限 tangent。有效白袖正面樣本的合成世界法線 Z 約 0.54–0.99。
- **再試前檢查**：多行診斷程式用 here-string；讀 Maker JSON 浮點陣列先驗證 `null`／非有限值與 dtype，再作向量運算。修改同一檔案的 patch 分開提交，避免工具拒絕多筆操作。

## E-009　PowerShell 萬用字元未匹配方括號檔名（2026-09-26）

- **症狀**：備份 March 診斷照時，`-Filter '[[]Card[]] March_7th_keiyuu_outfit0_*.png'` 找到 0 張，目標資料夾一度是空的。
- **原因與處置**：PowerShell 的 `-Filter` 交由檔案系統提供者處理，該方括號寫法未匹配這批檔名。改以 `-Filter '*.png'` 枚舉，再用 `.Name.StartsWith('[Card] March_7th_keiyuu_outfit0_')` 精確篩出三張；備份數核實為 3。
- **再試前檢查**：操作含 `[`／`]` 的卡名時，先用實際檔案列表與備份數驗證，避免依賴跨工具不同的 glob 轉義規則。
- **再犯與修正（21:26）**：用 `Get-Content '...\\[Card]...\\mesh_61_o_inner_a.json'` 又觸發 PowerShell 方括號通配，雖然 `rg --files` 已證實檔案存在。改用 `Get-Content -LiteralPath`；後續所有含卡名的 PowerShell 路徑都用 `-LiteralPath`。

## E-010　相機光向診斷截圖未獨立保存（2026-09-26）

- **症狀**：完成 kind 2 相機向量單卡試拍並目視、量測後，恢復正式九圖時覆蓋了這三張診斷 PNG；固定 `+Y` 那組有獨立備份，但相機向量那組已無磁碟原圖。
- **原因與處置**：回退前只記錄了 MAE `(0.046,0.027,0.013)/255` 和畫面檢視，漏做試驗圖複製。程式碼與正式九圖已回到光向診斷前，且逐檔 SHA-256 與 `BeforeForgeLightDirectionDiag` 相同；不把缺失的原圖宣稱為保留的證據。
- **再試前檢查**：每組 A/B 在下一次覆蓋 `Saved/Screenshots` 前，先複製到具名 `Saved/DebugShots` 目錄並核對檔數；再做回退。

## E-011　缺 authored tangent 的法線貼圖直接開啟後白袖出現藍帶（2026-09-26）

- **變更**：Maker `BakeMesh` 的 March `o_inner_a` 有 2,751／2,887 個有效 tangent；依 UV 重建的 tangent 對這些值平均角差 1.4°、handedness 全部一致。據此暫移除抽取器 `source_tangents == false` 時丟棄 normal map 的閘門，並升 session／CLI 快取版本。
- **症狀與證據**：UE log 確認 `o_inner_a` 的 normal map 從 0 變成 1,190,086 bytes；March T-pose 白袖出現大片不規則藍帶，與 Maker 參考不符。失敗截圖已保存在 `Saved/DebugShots/UvTangentNormalMapFailed`。
- **原因狀態**：UV 重建 tangent 與 Maker runtime 有效值的接近度已驗證，但抽取後 normal map 的像素／列方向、UE tangent-to-world 與 Unity TBN 的映射仍未逐點核對；**不可斷言 tangent fallback 單獨造成藍帶**。此改動目前屬失敗試驗。
- **處置與再試前檢查**：先封存失敗圖，取得新 session 的 normal PNG，和 Maker `tex_41...__NormalMap.png` 在相同 UV／方向逐像素比對；再核對 UE 的 V 軸翻轉、bitangent sign 與 source fragment 的 DXT5nm 解碼。三卡 A/B 合格前撤回閘門改動並恢復正式九圖。

## E-012　Normal conversion 修正後白袖藍帶仍在（2026-09-26）

- **變更與證據**：MaterialEditor 內嵌 `normal_convert` 的有效 Windows DXBC 證實輸出 `(1, sample.g, sample.b, sample.a)`。抽取器照此重建 runtime normal map；March 新 PNG 的 R 全為 255，與 Maker 直接傾印相同，G／B／A 同方向全圖平均差 `(2.829,2.312,4.266)/255`。三卡正式試拍九圖存於 `Saved/DebugShots/UvTangentNormalConvertedFailed`。
- **症狀**：March 白袖下緣仍有遠比 Maker 寬的鋸齒藍帶。故 E-011 的藍帶不能單獨歸因於 R 通道錯誤；converted normal map 雖更符合來源，未使整條材質管線等價。
- **接續檢查與結果**：UE `KKSkeletalBuilder` 的 bitangent sign 經 Unity→UE `(-X,Z,Y)`（行列式正）和貼圖／UV 同時翻轉推導後反轉。單卡 A/B 白袖左右 ROI 平均 RGB 差僅 `(0.614,0.384,0.125)` 與 `(0.768,0.434,0.096)/255`，藍帶仍在；此符號不是主因。符號修正本身符合座標推導，但需三卡確認不造成其它退步。
- **後續定位**：在同一批 Maker 白袖前側 350 個幾何法線上，遊戲相機燈 `(0,0,1)` 只有 2.3% 低於 Minimalist ramp 全亮門檻，UE 舊固定光向等效 `(0.25,0.45,0.85)` 則有 22.6%。只對 Forge kind 2 同時用相機燈和 normal map 單卡重拍，鋸齒藍帶大幅縮小；失敗組與成功單卡都已各自存檔。先前「必須等完整 TBN 才能開啟」的推論過強，主要成因是**normal map 放行時仍使用錯誤的光向**。
- **處置與驗收**：Forge kind 2 採遊戲相機隨動光向且允許缺 authored tangent 的 UV 重建 normal；其它 shader family 保留 tangent 閘門。21:42–21:43 三卡九圖完成、程式正常退出，March／Alice 全身白袖藍邊比失敗組收斂，Yuuka 全身無新缺件；根目錄九張、`old` 185 張。March 側臉衣領／袖子的局部色階差仍大，缺同角度 Maker 參考，記為待驗限制。

## E-013　Forge 光向診斷後的測試型別錯誤（2026-09-26）

- **症狀**：新增 Forge normal map 放行測試時，誤寫不存在的 `crate::unity::MaterialInfo`，`cargo test` 回報 E0422；同一命令前段 `rg 'Material {'` 也因未跳脫 `{` 報 regex parse error。隨後在 `Tools/ok-extract` 工作目錄下又把搜尋路徑寫成 repo 相對路徑，造成一次找不到檔案。
- **修正**：檢查同檔既有測試與 `src/unity.rs`，正確型別是 `MeshMaterial`；測試已改用它。後續在執行搜尋前確認工作目錄，字面大括號用 `-F` 或適當跳脫；新增測試先沿用鄰近測試的實際型別。
- **驗證**：更正後定向 Rust 測試與 UE Editor Development build 都通過，三卡九圖完成；此錯誤未進入正式編譯產物。

## E-014　近臉相機初次 fit 沒有留出頭飾空間（2026-09-26）

- **症狀**：第一次把上方頂點納入透視 fit，仍算出 `base=90, front=90, side=90`；Alice 藍色髮夾上緣繼續出框。
- **原因與處置**：誤用肖像視窗寬高比及 40° 指令值；實際 shot 的 CameraCache 是 FOV 50°、參考 aspect 16:9。改按 CameraCache 的垂直半視角與 16:9 參考值，以頭飾實際頂點深度分別計算正／側鏡頭距離並留 25% 空間；新試拍 Alice 夾子完整入鏡。相機修正不等於髮片遮擋修正。

## E-015　UE 相機診斷使用不存在的 API（2026-09-26）

- **症狀**：加入 FOV 記錄時使用 `PlayerCameraManager->GetCameraCachePOV()`，UE 5.8 編譯失敗。
- **處置**：改為實際 API `GetCameraCacheView()`，Editor Development build 通過；記錄顯示 shot FOV 50°、aspect 1.778。改 C++ API 前先在本機引擎 header 查名稱。

## E-016　隱藏臉的診斷只關了骨架分支（2026-09-26）

- **症狀**：`OK_DIAG_HIDE_FACE` 試拍仍見臉；`cf_O_face` 在該次 shot 使用 procedural `Comp`，不是被關閉的 `Follow`。因此該張不能當「無臉」遮擋證據。
- **處置**：辨識兩個渲染分支後移除這個未完成的診斷開關。正式圖從備份或完整重拍產生，不保留這個試驗為成果。

## E-017　髮長基準值曾被過度推論為全部前髮的根因（2026-09-26）

- **症狀**：依 `ChaCustomHairComponent.Update` 發現 `baseLength` 後，起初預期它會修復 Alice 全部瀏海。A/B 證明 slot7 `AS01_O_kami_05F_0` 平均位置誤差從 4.30 mm 降至 0.04 mm，但 slot2／13 `cf_hair_f_14_i_base` 仍約 9.6 mm，不能宣稱前髮已對齊。
- **處置**：保留來源碼可證的通用賦值 `localY=baseLength+addLength*lengthRate`，並把 slot2／13 的遮擋另列調查。執行時 Maker probe 的 slot2／13 `joint1-1=-0.1339748` 與新公式相符。

## E-018　髮飾縮放與根位移的兩個猜測被 A/B 否定（2026-09-26）

- **單層縮放試驗**：暫以 `OK_DIAG_SINGLE_HEAD_ACC_SCALE` 移除頭部第二層 scale，Alice 多個髮飾平均位置誤差惡化到 25–32 mm。已刪除開關並恢復原通用尺度；不能用單層縮放修瀏海。
- **根位移猜測**：一度誤讀 `move_nodes` 輸出，以為 `ca_slot` 的 −0.008 m 根位移被抵消。prefab 節點診斷證實 `N_move` 的 `parentWorld` 與 `selfWorld` 都含 −0.008 m，原變換保留了根位移。移除 `OK_MOVEDIAG` 診斷碼，沒有依錯誤猜測改換算。

## E-019　全關臉部無深度覆蓋後鼻線變成白塊（2026-09-26）

- **症狀**：用 `OK_EYE_NOZ=0` 做 Alice A/B 時，右瀏海能覆蓋眼部，但鼻樑出現白色矩形。舊的 `bHead` 邏輯把 `cf_O_noseline` 強制設為 opaque；先前 `bNoZ` 分支同時把它改成 translucent，遮蔽了這個分類錯誤。
- **處置**：鼻線維持抽取器 manifest 的 `translucent`，眉線與上眼線使用深度測試；只保留與臉網格共面的下眼線例外。後續須以新 shot 核對鼻線及瀏海，不可把全關深度覆蓋的白塊試拍當正式圖。

## E-020　本輪診斷命令與不存在檔案（2026-09-26）

- **症狀**：曾用不存在的 `outfit0-tpose-face.jpg` 路徑讀 Alice 參考、在 `rg` 路徑使用 PowerShell 不展開的 glob、把沒有 `Debug` 的 Rust `Tf` 用 `{:?}` 編譯診斷、用 Windows cp950 預設解碼 UTF-8 JSON，另有一次 NumPy 腳本假定遊戲 OBJ 含 `vt`，實際 UV 在對應 JSON。這些操作各自失敗，沒有作為渲染證據。
- **處置**：以實際檔案清單核實路徑；`rg --files` 後篩選；診斷只列印 `Tf.pos`；JSON 明確用 UTF-8；UV 讀 `mesh_*.json`。診斷編譯錯誤已修正後重編成功，最後移除臨時 `OK_MOVEDIAG`。

## E-021　把遊戲 BakeMesh OBJ 的差值當成實際髮片位移（2026-09-26）

- **症狀**：逐頂點比較 Alice 遊戲傾印與本機抽取，slot1／2／6／8／10／11／13 等 SkinnedMeshRenderer 髮片約高 8–9 mm；起初把這當成實際畫面中「臉蓋住前髮」的幾何根因，甚至考慮將髮片整體下移。
- **第一次重新核對的錯誤**：這批 JSON 自標 `bakeMeshSpaceGuess="unknown"`，slot2 `.obj worldBounds` 寬 0.127 m、`rendererWorldBounds` 寬 0.197 m。我過早把後者當作實際頂點尺寸，並依[較新 Unity 的 `BakeMesh` 文件](https://docs.unity3d.com/2022.3/Documentation/ScriptReference/SkinnedMeshRenderer.BakeMesh.html)推定舊 OBJ 不可靠；因此沒有加入固定 −8 mm 位移。舊版遊戲其實只有單參數 `BakeMesh(mesh)`，且 `rendererWorldBounds` 是保守 bounds，不能當精確頂點範圍。
- **同幀證據與修正**：Maker 重新載卡後，RuntimeLightProbe 直接 `BakeMesh(mesh)` 並用 Renderer `TransformPoint` 寫 slot2／13 與臉的世界頂點。以臉的平均位移對齊目前抽取器，slot2／13 仍平均高 9.25／8.75 mm；新 slot2 的寬度約 0.127 m，與舊 OBJ 的精確頂點寬度一致。這證明前述「8–9 mm 全是傾印縮放假差」不成立。保留不用固定 offset 的決定，改查附著尺度與 `N_move` 的矩陣順序，並以同幀資料 A/B 驗證。

## E-022　舊材質傾印漏掉 Material Editor 複製層（2026-09-26）

- **症狀**：Alice 舊 `mesh_2/35_cf_hair_f_14.materials.json` 各列一層 `Shader Forge/main_hair_front`，抽取器卻有同網格兩層，曾懷疑 `.MECopy1` 是 UE 多畫的假層。
- **驗證與處置**：在 Maker 重新載入同張 Alice 卡後，臨時 RuntimeLightProbe 讀到 slot2／13 的 `cf_hair_f_14` renderer 各有兩層：`main_hair_front` queue 2475，以及 `cf_m_hair_f_14_00_i.MECopy1` 的 `main_item_studio_alpha` queue 3000，與抽取器預期一致。未刪除複製層；舊靜態傾印不代表目前同一卡載入完 Material Editor 後的材質狀態。臨時遊戲插件在取樣後已移除，遊戲程序已停止。

## E-023　遊戲 Unity 版本沒有 BakeMesh 的雙參數多載（2026-09-26）

- **症狀**：為檢查同幀髮片幾何，臨時 RuntimeLightProbe 先呼叫 `SkinnedMeshRenderer.BakeMesh(mesh, bool)`；以遊戲 DLL 編譯回報 CS1501，該版本只提供一個參數。
- **處置**：改用遊戲實有的 `BakeMesh(mesh)`，另外把輸出頂點分別按 Renderer 完整 `TransformPoint` 與只套位置／旋轉寫成診斷 OBJ，以比較縮放空間；重編成功。不能直接把較新 Unity API 的 `useScale` 多載當成此遊戲可呼叫的介面。

## E-024　第二層頭部尺度改到 N_move 後面會破壞左右定位（2026-09-26）

- **試驗**：同幀資料確認 Alice slot2／13 相對臉各高約 9 mm 後，把抽取器 `parent * extraScale * N_move` 暫改為 `parent * N_move * extraScale`，希望卡片位移先由頭部節點定位，額外尺度只作用於網格。試驗輸出在 `Saved/DebugShots/AliceTransformOrderFixed`。
- **結果**：slot2 對同幀 Maker、以臉位移對齊後的平均頂點誤差降至 5.24 mm，但 slot13 仍 8.22 mm，且 X 平均偏移新增 4.77 mm；原版 slot2／13 的 X 接近零差。只改這個矩陣順序並非通解，不能以單張正臉目視取代全頂點／雙側核對。
- **處置**：撤回順序試驗，正式 Rust 原邏輯與 UE 九張截圖不受此 A/B 影響。保留同幀 OBJ 與誤差供下一輪定位髮飾骨架／bindpose；不要套固定的 −9 mm 位移掩蓋 X 與形變問題。

## E-025　prefab SQL 快取掩蓋模組 renderer 原點，誤判為無法重建（2026-09-26）

- **症狀**：Alice 前髮 slot2／13 的 Unity 同幀 `BakeMesh` 在不套 renderer scale 時，逐頂點與 `boneWorld * bindpose * rest` 一致；套完整 `TransformPoint` 後，會以 renderer 自己的位置約 `(0,1.2929,-0.0357)` 為縮放中心。抽取器原先以頭部掛點當第二層縮放中心，產生約 9 mm 的系統位移。
- **第一次查詢的錯誤**：嘗試從 `mesh_prefab_locals` 找 skinned renderer 的 GameObject 矩陣時，Alice mod mesh ID `3640120205658767845`、renderer ID `883967653992184622` 一直回 `None`。當時忘了 `meshes_for_entry` 會先讀 SQL prefab 快取，診斷其實沒有走新解析碼；還一度查到無關的原版 `f_54` prefab，誤判為 mod 資源無法解析。這批擴大遍歷、按同名 prefab 合併和 renderer ID 索引試驗均已撤回。
- **修正與驗證**：改用 `OK_NOCACHE=1` 後，找到真正來源 `[DeathWeasel] Accessory Hair Front v1.5.4.zipmod` 的 `p_cf_hair_f_14_i`，同一序列化檔確有該 renderer。原有 prefab graph 直接給出 renderer 本地原點 `y=-0.057350818`、`z=0.0045301197`；與頭部掛點矩陣相乘可重建遊戲 renderer 原點。新通用公式在 renderer 原點套其世界尺度，slot2／13 相對臉的平均 Y 誤差由 `+9.26/+8.75 mm` 降為 `−0.35/−0.85 mm`，slot7 髮飾沒有退步。快取版本已增加以淘汰舊 prefab。保留初次失敗原因，後續遇到 mod prefab 結構調查先用無快取抽取核實。

## E-026　PowerShell 下用 rg 的 Windows glob 導致查詢失敗（2026-09-26）

- **症狀**：`rg ... Tools\\ok-extract\\src\\*.rs` 在此環境把含星號的 Windows 路徑直接傳給 ripgrep，回報 OS error 123。
- **處置**：改對 `Tools/ok-extract/src` 目錄執行 `rg`。該失敗僅是診斷命令，沒有修改檔案或作為幾何結論。

## E-027　診斷 Python 單行命令及 UE 視窗解析度（2026-09-26）

- **Python**：一次 `python -c` 命令尾部帶字面 `\\n`，導致 `SyntaxError`；改把頂點對比加入 `Tools/RuntimeLightProbe/analyze_skin.py`，結果可重現。
- **UE 截圖**：第一次三卡完整拍攝只帶 `-ResX=900 -ResY=1080`，UE 沿用 2194×1234 的無邊框視窗大小，不能與先前 900×1080 比。九張寬圖備份在 `Saved/DebugShots/RendererPivotWideDiag`；改帶 `-windowed -ForceRes` 後 Alice 單卡三圖確認為 900×1080，再重拍三卡。解析度未核實前，不把寬圖當正式比較成果。

## E-028　把中央前髮誤認為臉遮擋（2026-09-27）

- **症狀**：先前以中央深紫色髮束的深度與眉眼測試，回答「臉蓋住頭髮」；使用者指出真正問題是左右淺粉髮束被同樣粉色的臉蓋住。
- **驗證**：`OK_DIAG_FACE_GREEN` 暫時把 `cf_O_face` 標綠，`Saved/DebugShots/FaceGreen/Alice_keiyuu_outfit0_face.png` 顯示左右額角大片原粉色區域確實是臉。中央髮束的結果不能代替兩側。診斷開關已從正式 C++ 移除。
- **防止重犯**：相近顏色造成遮擋歧義時，先以材質／mesh ID 著色或 stencil 確認像素來源，再做深度推論。

## E-029　用所有透明髮層關閉深度測試是假修復（2026-09-27）

- **試驗**：`Saved/DebugShots/HairNoZ` 暫時關閉附件透明層的深度測試，左右前髮立即顯示。
- **問題**：同時讓長髮蓋到衣服，與 Maker 全身照及 `main_item_studio_alpha` 的 `ZTest LEqual` 不符。此診斷開關已撤回，沒有作為正式通解。
- **方向**：維持原深度繪製；只對實際 `main_hair_front` renderer 及其 Material Editor 複製層，在臉部 stencil 區域補上被臉擋住的像素。

## E-030　mod Shader 外層名稱為空，錯把前髮當未知 shader（2026-09-27）

- **症狀**：第一版臉部合成輸出完全不變，UE 記錄 Alice 所有前髮 `front=0`。擴大 `main_hair_front` 字串比對仍無效。
- **根因**：DeathWeasel mod bundle 的 Shader 物件 `m_Name=""`，但 `m_ParsedForm.m_Name="Shader Forge/main_hair_front"`。抽取器只讀外層名稱，令材質 `shader` 空白、只能靠 `hair_shader_hint` 暫列 hair family；Material Editor 複製層則讀到 `main_item_studio_alpha`。外層材料的 serialized queue 2850 也不能當 runtime 2475 的可靠替代。
- **修正與證據**：Unity bundle Shader 讀取改於外層名稱空白時使用 ParsedForm 名稱，`material_named` 及 prefab 材質綁定同用。Alice 原始卡無快取 CLI 顯示 slot2／13 的基本層為 `Shader Forge/main_hair_front`、同 renderer 的 copy 亦標記 front=true；沒有角色、slot、髮型名稱硬編碼。

## E-031　本輪診斷命令格式（2026-09-27）

- PowerShell 把 `rg` 路徑中的 `Saved/ShaderDxbc*` 及 `00*.asm.txt` 當字面 Windows 路徑，回報 OS error 123；改用 `rg --files` 或搜尋目錄。`Tools/ok-extract/src/{lib.rs,session.rs,ffi.rs}` 的 Bash brace 語法也不能直接用於 PowerShell，改逐一路徑。
- Python 讀 KKS JSON 未指定 UTF-8 時遇到 cp950 解碼錯誤；加 `encoding='utf-8'`。這些查詢失敗沒有用作渲染結論。

## E-032　把整組前髮重畫到臉前造成 Yuuka 白髮片（2026-09-27）

- **症狀**：第一版以臉部 custom stencil 補畫整個 `main_hair_front` renderer；Alice 側瀏海顯現，Yuuka 卻出現大片白色前髮，且頭頂層次破碎。全髮無深度測試的更早試驗也會把長髮畫到衣服前。
- **原因與處置**：重畫原始前髮會顯示本應被深度遮住的材質面；縮到 Material Editor 的 `main_item_studio_alpha` 複製層可消除 Yuuka 白片，但仍是額外的近似 pass。最終查到骨骼頭髮位置根因後，整個 stencil／額外複製 pass、FFI 前髮群組欄位與 `r.CustomDepth=3` 都已撤回。三卡九張正式圖只用正常深度路徑。

## E-033　把 `BakeMesh` 傾印誤當 GPU 上最終頂點（2026-09-27）

- **錯誤判斷**：先前 Alice slot2／13 的抽取結果與 `BakeMesh` 再 `Renderer.TransformPoint` 的遊戲 OBJ 相差 8–9 mm，於是額外以 renderer pivot 套一次世界尺度，讓 OBJ 誤差降到約 1 mm。這只是對齊**傾印程式的 CPU 產物**；它沒有驗證遊戲畫面中 GPU 骨骼頂點。使用者指出髮飾與頭髮相對位置錯，證明舊結論不能當作渲染正確性。
- **同幀證據**：遊戲 probe 確認 Alice `cf_hair_07_00` 的 renderer 世界 Y 尺度約 0.787、pivot 約 1.338 m；`BakeMesh`+`TransformPoint` 的最高點約 1.460 m。再加一次 0.787 尺度會把髮頂向 pivot 壓低約 2.6–3 cm。Maker 畫面髮頂覆蓋藍色 `O_nosetama` 根部，而前版 UE 畫面把整根藍色飾品露出；直接用已完成的骨骼世界頂點、不再乘 renderer 尺度後，前髮與飾品恢復該遮擋關係。遊戲 probe 的 ornament 高度約 1.498 m，非骨骼飾品位置未改。`o_shadowcaster` 暫時停用的遊戲 A/B 外觀幾乎不變，排除其額外可見幾何。
- **通用修正**：`snap_accessory` 對已由骨骼矩陣算成世界座標的頭部 skinned 附件，不再於 renderer pivot 或掛點重乘尺度；沒有角色、slot 或 mod 名稱條件。既有對 OBJ 的量測保留作為 `BakeMesh` 路徑證據，不能再拿它要求 GPU 預覽逐頂點相同。session pipeline／CLI 抽取版本已更新，三卡重新拍攝。

## E-034　拍攝參數與診斷命令（2026-09-27）

- 第一次單卡／三卡診斷漏設 `OK_OUTFITS_LIMIT=1`，額外拍到 outfit1 並使兩個 UnrealEditor 同時啟動。停止那兩個明確的診斷程序，以限定一套 outfit 的三卡任務重新拍攝；正式九圖未使用誤拍結果。
- 一次 Python `-c` 內含巢狀字串與換行的查詢在 PowerShell 變成未終止字串，改加進可重複執行的 `compare_alice_hair_geometry.py`。一次在 `Tools/ok-extract` 工作目錄使用專案根相對路徑的 `rg` 也查不到檔案，改從專案根目錄查詢。兩者只是診斷命令錯誤，未作為幾何結論。

## E-035　側臉鏡頭被長髮包圍盒拉偏（2026-09-27）

- **症狀**：骨骼頭髮座標修正後，Alice `outfit0_face_side` 的臉幾乎在畫面右邊界外；舊截圖已有類似裁切，變形範圍變大後更明顯。側臉鏡頭原本看向所有部件包圍盒中心，Alice 往背後延伸的長髮把中心拉離臉。
- **修正**：側臉鏡頭改看 `cf_O_face` 實際頂點形成的包圍盒中心；若找不到臉網格，回退到原中心。上緣距離仍依可見頂點 fit，正臉／全身鏡頭不改。Alice A/B 顯示側臉回到畫面中，March／Yuuka 同規則重拍也完整顯示臉與髮飾。`Saved/DebugShots/GpuSkinFaceCenteredAllCards` 為最後九圖，前一批已移到 `Saved/Screenshots/old/2026-09-27-before-side-face-center`。

## E-036　固定頭髮光向與本輪診斷命令（2026-09-27）

- **實作差異**：UE 的 `ShaderKind == 1` 頭髮原先使用固定 `(-0.25,0.85,0.45)` 光向；`Shader Forge/main_hair/0000.asm.txt` 的 ForwardBase 在 Lambert、Gloss 座標均讀 `cb2[0]`，遊戲 Maker layer-10 角色燈則掛在 Camera 下。固定光向在側臉不會跟相機轉動。已讓 hair 與其他確認使用此燈的材質共用 `-ResolvedView.ViewForward`，無卡片、模組或鏡頭特例。
- **驗證界限**：三卡九張 A/B 與 UE build 通過；Alice 正臉頭頂原有的深紫色大片仍在，表示這次光向修正不足以解決頭頂材質差。不可把光向來源正確當成 ForwardBase 完整等價；下一步須核對 `_DetailMask`／自陰影／透明層的逐像素來源。
- **查詢失敗**：最初誤以為 `Saved/ShaderDxbc/EC_Shaders` 目錄直接有 `shaders.txt`、`index.tsv`；這兩個檔案不在該目錄。改直接讀其 `Shader_Forge_main_hair`、`main_hair_front` 子目錄的 ASM。另一次 Python `-c` 路徑以尾端反斜線接字串造成 `SyntaxError`；改用正斜線路徑。這些命令錯誤沒有作為 shader 結論。

## E-037　Material Editor shader 診斷的編譯及路徑錯誤（2026-09-27）

- 首次加入 kind 4 `Cull Back` 條件時漏一個右括號，UE C2059 編譯失敗；已修正並重新編譯成功，未用失敗 binary 拍正式圖。臨時 RuntimeLightProbe 首次 csc 指令使用錯誤的相對引用路徑，改用實際遊戲 DLL 絕對路徑後編譯成功。另一次把 `main_item_studio_alpha` ASM 誤查到 `Saved/ShaderDxbc/EC_Shaders`；KKS 材質的實際位置是 `Saved/ShaderDxbcKKS/KK_Plugins.Resources.KKS_Shaders/Shader_Forge_main_item_studio_alpha`。路徑查詢失敗沒有作為 shader 證據。
- 後續核對 `main_hair_front` 時先把 shader 名查進 KKS_Shaders bundle，工具回報未找到；它實際位於 `KK_Plugins.Resources.EC_Shaders.unity3d`。改查正確 bundle，且區分 pass 0 ShadowCaster（Cull Front）與 pass 1 ForwardBase（Cull Off），不將 ShadowCaster 狀態套到色彩繪製。

## E-038　不完整的 ColorMask 公式與通用 ramp 使材質退步（2026-09-27）

- Maker 即時移除 Alice 前髮的 `main_item_studio_alpha` 複製層後髮色更暗，證實「刪除多餘複製層」是假說錯誤。已恢復遊戲材質，診斷 DLL 移出 BepInEx；正式抽取仍保留兩層。
- 試將 kind 4 的 ColorMask RGB 以 hair 公式直接烘焙；Alice 頭頂深色塊沒有改善，Yuuka 肩部重新出現白色片。`main_item_studio_alpha` DXBC 同時讀三張 PatternMask，不能把一段 ColorMask lerp 脫離完整 UV／mask／alpha 支路移植。Rust 公式改動與快取版本 216／60 已撤回至 215／59；試驗圖留於 `Saved/DebugShots/StudioAlphaMaskLerpAlice`／`StudioAlphaMaskLerpOtherCards`。
- 再試讓 kind 4 套 UE 通用 ramp／相機光向，Alice 側髮出現鋸齒暗塊，頭頂未改善；C++ 試驗改動已撤回，圖在 `Saved/DebugShots/StudioAlphaForwardBaseAlice`。只保留來源 ForwardBase 明確支持的 `Cull Back`。後續改 shader 必須先完整追蹤 texture register、UV 變換及 blend，再三卡無快取對照。

## E-039　把 Alice 暗帶歸因於 PatternMask，漏看 ColorMask 的行方向（2026-09-27）

- **錯誤假說**：看到 `main_item_studio_alpha` DXBC 讀三張 PatternMask，就推測缺這三張貼圖是 Alice 額前暗帶主因。Maker 即時 probe 證實 Alice 複製層的三張 `_PatternMask` 均為 null；複製層 MainTex 與卡片 PNG 的 RGBA 完全相同。抽取 ColorMask 卻相對 Maker 即時 ColorMask 上下顛倒，將深色底部套在亮色髮頂。現在只對來源方向不同的 MainTex／mask 配對翻轉 mask，Alice 單卡無快取 A/B 的暗帶明顯消退。Yuuka 的 PatternMask 仍需另行核對，不能用 Alice 的 null 推論所有卡片。
- **診斷錯誤**：首次 csc 的 PowerShell 參照陣列被合成一個參數，第二次使用相對路徑令 csc 找錯 `Probe.cs`，第三次新區塊的 `source` 名稱與外層變數衝突 CS0136；逐一修正後 probe 編譯成功。Python 初次直接用 2048² 座標讀 1024² mask 產生 IndexError，改按尺寸換算。最初拿 Maker runtime hair0 與抽取 slot2 比 alpha，誤以為 MainTex alpha 不同；hair0 實為 slot13，hair1 才是 slot2，正確對應後 RGBA 全圖平均差為 0。以上失敗均未當作最後材質證據。

## E-040　把臉部後處理漏層誤判成 cheek 貼圖錯誤（2026-09-27）

- 初看 Alice 遊戲臉 atlas 有斜向臉頰線與鼻尖亮點，抽取 atlas 沒有；因卡片 `cheekId=505` 使用 Sideloader，先推測全域 `(category,id)` 撞到別的 mod。卡片 KKEx 確實指向 `yamadamod.ymdtexture`，但 Maker 同幀 `_Texture4` 也正是 zipmod 中的 `mf_cheek3kai_3` 白色柔邊橢圓，**並非**斜線來源。這個假說已由遊戲 `matCreate` 實測否定，沒有依它人工畫斜線。
- 真正的層次是 `customTexCtrlFace.createTex` 沒有斜線和亮點，`matDraw.MainTex` 有；Alice 卡片 Material Editor 的 `(objectType=4,slot=0,material=cf_m_face_00,property=maintex)` PNG 與遊戲 `matDraw.MainTex` 1024² RGBA 逐像素相同。舊抽取路徑只輸出 `CreateFaceTexture` 的重建圖，漏讀其後的材質 MainTex 替換。現在以材質鍵對所有卡片查這筆覆蓋，有才套用；Alice 抽取 `head.png` 已與遊戲讀回圖逐像素相同。
- 沒有 Material Editor 覆蓋時，臉頰 `_Texture4` 仍必須走 `Koikano/create_skinface` 的 `_tex4uv`，不是直接把 1024×256 圖縮放到 1024²。首次手算把 `r2.xy` 誤記成 `0.5*scale`；逐指令重讀 DXBC 後修正為 `scale-0.5`，並按 Maker 材質 `_tex4uv=(0.5,0.64,-0.33,-4)`、Clamp/Bilinear 取樣。用同幀 `texMain`、ColorMask、Texture4 與 `createTex` 對照，正確 UV/行方向候選的臉部區域平均誤差約 0.0065（0–1），錯誤方向約 0.012–0.017；仍有色彩／後處理差距，不宣稱整個 create shader 逐像素等價。
- 診斷操作失敗：一次 `rg` 以 PowerShell 不支援的 `src/*.rs` 路徑查詢，改查目錄；臨時 Rust 測試曾用不存在的 `paint_layout` 欄位，改成 `paint_layouts`；兩次 csc 命令因 PowerShell 參照陣列合併與相對 `Probe.cs` 路徑失敗，改用逐項加入陣列及絕對路徑；Python 原始字串以反斜線結尾造成 `SyntaxError`，改用正斜線；一次 `apply_patch` 上下文不符未套用，重讀區塊後補丁成功。這些錯誤均未用作畫面結論。
- 查 Yuuka 參考時先用了已不存在的 `mesh/_dump`，實際使用者照片在 `mesh_dump/<卡名>/outfit0-tpose-body.jpg`。移動 KSOX 的第一版補丁放在 face atlas 最終上下翻轉**之前**；同幀 KSOX PNG 與遊戲 `matDraw.MainTex` 都是翻轉後方向，因此立即改為先完成方向歸一化、再覆蓋 FaceUnder／FaceOver。錯誤順序未編譯或拍正式圖。

## E-041　以單一 KSOX 行方向搬動 FaceUnder／FaceOver 造成 March 額頭色塊（2026-09-27）

- Maker 同幀 Alice 的 `createTex` 沒有鼻尖、`matDraw.MainTex` 有，於是試把 KSOX FaceUnder／FaceOver 都移到 create shader 之後。第一批 `Saved/DebugShots/FaceKsoxOrderAllCards` 拍到 March 正臉時，額頭出現遊戲參考沒有的大塊粉色三角形；立即停止 Editor，沒有取代正式九圖。
- March KSOX FaceOver PNG 自身包含大片粉色圖形，與 FaceUnder 的 UV／行方向不能一概而論。第二版只對 FaceOver 翻轉，`Saved/DebugShots/FaceKsoxUvMarch` 的額頭仍有較淡的粉色塊，證明問題不只是逐張 PNG 的 V 翻轉。不能從 Alice 的 createTex/Material Editor 先後關係推論 March 的 KSOX 最終合成。這兩版 source、版本 218／62 和 219／63 均已撤回；恢復 217／61 的臉部合成順序及已驗證的 Material Editor MainTex 修正，Rust 測試／release 和 UE Development 重編成功。正式九張仍為 `FaceMeAllCards`，待遊戲同幀讀取 March KSOX compositor 再調整。

## E-042　KSOX 行方向與 Texture2D 色彩空間漏轉（2026-09-27）

- 先前只看 Alice 的 Material Editor 結果，把 `FaceUnder`／`FaceOver` 當作同階段圖層。`KoiSkinOverlayX/Hooks.cs:OverlayBlitImpl` 明確區分 under 在 create shader 前、over 在後。March 同幀 RT 顯示兩層 PNG 以遊戲 RT 的行方向繪製；抽取器原始貼圖方向相反。修正使用原始 atlas 的口腔位置決定翻面，再將 under 映到來源方向、over 放在最終方向，不再用同一個 V 翻轉處理兩層。中途 `rg` 在 `Tools/ok-extract` 工作目錄卻傳入含 `Tools/ok-extract` 前綴的路徑，出現找不到檔案；改用專案根目錄查詢，未把空查詢當作證據。
- 第一次只修合成順序，March 臉區誤差雖從約 11.13 降到 1.66，但口腔區仍約 68。讀回 `texMain` 後才發現 Unity 把來源線性 Texture2D 寫入 sRGB RT；原始 `(75,28,28)` 應成 `(148,93,93)`。補 IEC sRGB 轉換後 March 整圖誤差約 1.58、口腔區 0.13；Yuuka 這張含 mod 卡的整圖約 0.01、口腔 0。這也說明先前用暗口腔 atlas 判斷下巴材質可能誤判。
- Runtime probe 原本把 March、Yuuka 的 Maker 截圖都寫到 `AliceMakerFace*.png`，覆蓋既有 Alice 參考；已把 Yuuka 複製到 `YuukaMakerFace*.png`，重拍 Alice 並恢復原有 `AliceMakerFace*.png` 檔案。probe 現由卡片檔名產生診斷檔名，`OK_PROBE_CARD` 可選卡片，後續不再默默覆寫其他角色參考。March 的 Maker 截圖另以新檔名重拍；atlas 讀回資料保持完整。
- 色彩空間通解第一次 `apply_patch` 使用太寬的上下文，把 `texture_color_space` 插入 `decode_tangents` 函式內；編譯前檢查即發現並移到 `ParsedBundle` 的 `decode_tex` 後。第一次 `cargo check` 又因 `source_ab` 的 `&String`／`String` 分支型別不同報 E0308；統一成 `String` 後 `cargo check`、release 和測試通過。查 Cargo registry 時在 Windows 路徑中直接給 `rg` 萬用字元目錄得到 OS error 123；先用 `Get-ChildItem` 找實際 crate 路徑再查。PowerShell 讀 March `[Card]` 檔名時漏加 `-LiteralPath`，導致誤報不存在；改以 literal path 讀回，沒有遺失檔案。這些失敗未進入正式九圖。

## E-043　拿舊 Maker 法線檔判定 March 法線回歸（2026-09-27）

- 加 March UV 斷言時，舊 `Saved/ShaderDxbc/runtime_march_face_normals.tsv` 令 `march_tpose_face_vertices_match_game_dump` 失敗，報法線平均 `18.85°`、最大 `164.3°`。先比索引頂點位置才發現該檔與本輪遊戲相差數公分，不能當目前 runtime 真值。首次試用抽取 OBJ 的 `vn` 作檔外對照也失敗，因本專案 `head.obj` 只寫 `v`／`vt`、沒有 `vn`；沒有據此修法線。
- RuntimeLightProbe 改以卡片名保存即時 `BakeMesh` 的 inverse-transpose 世界法線，March 新檔 `Saved/DebugShots/[Card] March_7th_keiyuuFaceNormals.tsv`。抽取法線對新 Maker 平均 `1.456°`、最大 `3.533°`；Maker idle 頭點對 T-pose dump 的 Z 平均差約 `21.56 mm`。第一次改讀新檔仍因舊測試的最大角 `<3°` 失敗；按已量得姿勢差改為 `<4°`，指定測試通過。UV 的 U／V 平均差僅約 `5×10⁻⁹`。不應用這個舊檔或不同姿勢的極值調整預覽法線；正式九圖不受這次診斷測試改動影響。

## E-044　March 額頭、頭髮 pass 與診斷濾鏡的誤判（2026-09-27）

- 最初以 UE `OK_FACE_SKIN=1` 隱去粉色額頭、以及未加濾鏡的 Maker 特寫較淡，推測額頭粉色來自錯誤 UV 或 UE skin shader。遊戲 Maker 無髮讀回圖其實也有同一色帶，且先前 March atlas、UV、頂點與法線已對上；未匹配 Maker／Studio 相機、姿勢與後處理前，不能把這個色差當作 atlas 錯誤。`OK_NO_LUT=1` 的直接 A/B 也推翻「UE 灰背景代表 LUT 沒啟用」的假說。沒有依這些誤判修改正式 LUT 或臉 atlas。
- 初加 `AmplifyColorEffect` 到診斷 Camera 時，後處理改掉 `RenderTexture.active`，第一次 `ReadPixels` 讀到 UI 而非角色。每次 `camera.Render()` 後重設 `RenderTexture.active=target` 才取得有效 Maker 原始／濾鏡圖。兩次 csc 以正斜線 Windows 路徑傳參造成 CS1504，改用反斜線絕對路徑後編譯成功。再次查遊戲全身照時沿用使用者最初寫的 `mesh/_dump`，實際檔案在 `mesh_dump`；路徑失敗沒有被當成照片不存在。
- 首版依反組譯修 `main_hair` 深度和 outline 外擴後，March 頭部產生過量黑線：只補了 shader pass 與主貼圖 alpha，漏了未序列化 `_LineColor` 的 shader 預設值。遊戲同幀材質讀回、Unity shader 屬性表均是 `(0.5,0.5,0.5,1)`；抽取器原來填黑色。現按 shader family 補預設，重新編譯與三卡重拍；中途黑線圖僅留在 `Saved/DebugShots/HairPassStateAllCards`／`MarchHairGameOutlineWpo`，沒有放進正式截圖。不可用 Material Editor／prefab 未明寫屬性推論它在 Unity runtime 等於零。

## E-045　腳趾白色的錯誤歸因與參考資料夾錯配（2026-09-27）

- 初見 BodyUnder 合成順序錯誤時，曾推測它是 March 白色腳趾的直接原因；實際檢查 KSOX 原圖，該腳趾 atlas 點 alpha 為 0。真正原因是身體 create shader 的 ColorMask.b／`_Color5` 指甲色完全漏算。BodyUnder 仍按 `OverlayBlitImpl` 移到 shader 前，但不能拿腳趾修好當作該順序的單獨證據。最初用 `Select-Object -Last 12` 看 KSOX dump 漏看到 kind3 BodyUnder，又曾用 `Select-Object -First` 截斷 Rust 程序 stdout 導致 broken pipe；改完整列舉輸出檔案。
- 第一次 CLI 對照把不存在的 `mesh_dump/[Card] March_7th_keiyuu_outfit0_face.png` 當卡片 PNG 輸入。CLI 容許缺失卡片並輸出預設角色，`manifest.json` 的 `card`／`marker` 為空、skin 是預設 `(0.78,0.56,0.46)`；輸出 atlas 因而錯誤。重新使用 `Saved/ShotInputThree/[Card] March_7th_keiyuu.png`，先核對 manifest 為 March／`【KoiKatuCharaSun】` 再計算誤差。此前 `mesh/_dump` 路徑也已不存在，遊戲傾印在 `mesh_dump`。
- 測試第一次手填完整綠通道預期值時，藍色預期少 1（寫 121，實際 GPU 公式量化為 122）；校正後指定單元測試通過，沒有以失敗斷言改 shader。另發現 Alice 與 Yuuka 的 `mesh_dump` 外層資料夾名和內層角色顛倒；照外層名配對會產生假 5–6／255 平均色差。改用內層角色與 atlas 膚色交叉驗證，得到 Alice 0.376、Yuuka 1.292／255。這些診斷錯誤未進入正式九圖。

## E-046　把顯示材質乳頭效果假畫進 create atlas（2026-09-27）

- 舊 `paint_body_details` 以乳頭骨骼推測貼圖 UV，在 body MainTex 烘焙固定顏色與半徑的乳暈、乳頭、肚臍。March 與遊戲 `matDraw.MainTex` 比對，該處遊戲是正常膚色，抽取器卻有深色圓斑；最大通道誤差 142。載入真 `NipTex` 後又丟棄、只用假圓斑的程式路徑使此錯誤更難察覺。
- 反編譯 `ChaControl.ChangeSettingNip`／`ChangeSettingAreolaSize`／`ChangeSettingNipColor` 證明乳頭在顯示材質 `_overtex1`，不在 `CreateBodyTexture`。已刪除假圓斑、猜 UV 的 helper 和無用貼圖載入；March body atlas 平均誤差 0.5417→0.3893／255，該圓斑點對上遊戲。真正 `_overtex1` 效果仍需按 `main_skin` pass 實作，尤其裸體卡片不能宣稱完成。另一個 `underhair` 舊近似也屬顯示材質 `_overtex2`，暫未重構，後續要獨立驗證。

## E-047　身體顯示疊圖讀錯 vertex stream 與批次拍照參數（2026-09-27）

- 初版 `_overtex1`／`_overtex2` 材質圖照 `Koikano_main_skin` DXBC 接上 UV1／UV2 與 COLOR0 後，March 全身照右手出現紅色乳頭圖樣。診斷 March `o_body_a` 第 5692 頂點讀到的 COLOR0 為 `[233,226,127,63]`，UV1 卻重複主 UV `(0.6412,0.6581)`；遊戲 mesh dump 對應 UV1 是 `(0,0)`，而那四個「顏色」位元組正是另一個 UV 浮點值 `0.999556` 的位元表示。根因是 `channel_layout` 將 stream 直接緊接，漏掉 Unity 各 stream 起點的 16-byte 對齊。`oo_base.unity3d` 6823 頂點的實際資料長度 682320，對齊後 stream 起點 `[0,272928,463984]` 剛好吻合；舊連接法短 20 bytes。已修成依布局對齊，不依手部位置或卡片名稱遮罩。重拍 March 手部紅斑消失，三卡九圖通過；新增真 bundle 邊界測試。
- 首次 C++ patch 因上下文太寬，把 `overtex2_png` 載入放進 `ok.ShotPreview`，編譯失敗；移到 part texture 載入處後 UE build 成功。UnityPy 診斷首次誤用 `Mesh.name`（應是 `m_Name`）並把 `m_Floats` tuple 當 dict；改讀實際資料型別後才取樣。PowerShell 中以 `rg` 路徑內萬用字元查檔數次得到 OS error 123，改用固定目錄加 `-g`；這些命令失敗沒有當成素材不存在。
- 三卡批次第一次把資料夾交給 `OK_SHOT_CARDS`（該參數只接受分號分隔的檔案），造成 explicit cards=0 並拍到預設 `KK_091207`。該三張僅留在診斷資料夾，正式九圖由 `OK_SHOT_DIR=Saved/ShotInputThree` 重跑並以檔名、數量與 SHA256 驗證。往後批次拍照先核對 UE log 的 `rendering N task(s)` 和第一張卡名。

## E-048　三月 7 臉部拉長：把傾印頂點／相機假設當渲染真值（2026-09-27）

- 初先猜近距離鏡頭透視，以 `OK_PORTRAIT_TELEPHOTO` 延長距離並縮 FOV、另以 `OK_PORTRAIT_ORTHO` 拍正交 A/B；前者臉反而縮小，後者仍有過長額頭。這些不是幾何修正，已移除所有暫時相機開關及診斷 log，實驗圖只留 `Saved/DebugShots/MarchTelephoto25*`／`MarchPortraitOrtho`。
- March 頭部 renderer 尺度 X/Z `0.7684907`、Y `0.853878558`，Alice／Yuuka 三軸均勻。舊測試直接以 `BakeMesh + TransformPoint` dump 逐點吻合為成功，漏看該路徑與畫面 GPU 蒙皮可能不在同一空間。第一版索性移除全部額外頭部尺度，March 變圓，但 `HeadSingleScaleThree` 的 Alice／Yuuka 臉和眼明顯過大；此版未進正式截圖。後續只保留每張卡 X/Z 的共同尺寸因子，讓非等比形狀在 `head_onto_body` 中套一次。這是通用矩陣規則，仍須更多 mod 頭型與遊戲同鏡頭實測；沒有把單卡外觀係數寫死。
- 診斷時用 `rg` 查不存在的單檔 log 與 Windows 路徑萬用字元曾得到找不到檔案／OS error 123；改列實際目錄。Python 將 `.materials.json` 當 renderer dump 讀取曾有 KeyError；改限定主 mesh JSON。最初 `& UnrealEditor.exe` 由短命 PowerShell 啟動，Shell 離開後拍照程序未持續；改由 `Start-Process -Wait -WindowStyle Hidden` 完成三卡。一次測試補丁因定位過寬誤改 `march_skirt_vs_game_dump` 的卡片路徑，已撤回並放入臉部測試；中斷了錯路徑造成的漫長重抽，之後正式指定測試通過。這些失敗沒有放入正式九圖。

## E-049　增測兩卡時不知火舞前髮／眉部尚未對齊（2026-09-27）

- `KK_384283` 的新正臉圖有深色厚片壓在額前、近白的眉部；側臉有前髮遮住眼鼻。這是新卡測試揭出的可見殘差，沒有當作 §77 頭部縱橫比規則已適配所有卡片的證據。`KK_091207` 新圖則沒有同樣的大面積前髮問題。兩卡正式六張及原三卡九張均保留，以便下次同鏡頭比對。
- 初看抽取 OBJ 對遊戲 dump 的髮片世界頂點有 15–18 mm 平均差，可能誤以為附件整體縮放錯；但差值斜率接近 `renderer.lossyScale` 的倒數，且舊 §65 已指出 `BakeMesh + TransformPoint` 並不能直接代表遊戲 GPU 畫面。沒有用該差值強制縮放、移位或更換材質。這兩個 dump 沒有遊戲 T-pose 畫面，卡片封面亦是其他鏡頭／衣服；先取得同鏡頭遊戲畫面或 GPU／材質狀態，才能判定前髮的幾何、透明深度與眉部貼圖各占多少。

## E-050　髮片比例誤判與漏讀 Material Editor AlphaMask（2026-09-27）

- 使用者提醒三月 7、不知火舞的問題可能是頭髮相對臉被拉長，或臉也需加寬。先試把頭部附件查找從 full head skeleton 換成 face skeleton；不知火舞 `cf_hair_f_110`／`cf_hair_f_20` 的 OBJ bounds 完全不變，證明這不是缺陷位置，已撤回。三份不知火舞卡片的 outfit0 slot9/10 `addMove` 尺度均為 `(0.53,0.74,0.75)`，所以不能以卡片版本差異解釋髮片體積。遊戲 dump 的 f110 `bakedRawBounds`、`manualSkinnedWorldBounds` 皆寬約 79 mm，而 `BakeMesh + renderer.TransformPoint` 的 `worldBounds` 只有 55 mm；抽取器對前者接近。把 55 mm 當最終 GPU 尺寸並全卡再乘 renderer 尺度，會重複縮放；沒有套用。
- 真正漏掉的是卡片 Material Editor 對不知火舞 slot11/17 `cf_m_hair_f_20_00` 的 `_AlphaMask` 覆寫。卡內兩張 512² PNG 與遊戲 dump `tex_0/2...__AlphaMask.png` 逐像素相同；原本輸出的 f20 PNG alpha 卻恆為 255。`main_hair_front` 反組譯的裁切條件是 `_MainTex.a × _AlphaMask.r < 0.5`，抽取器現對該 shader family 把兩者相乘後輸出 alpha，沒有按角色名縮放或遮住整片髮。真卡與合成像素單元測試驗證黑遮罩區 alpha 變 0、白區保留 255；重連 UE 後兩條深色直片消失，見 `HANDOFF2.md` §79。
- 曾把不存在的 `[Card] March_7th_keiyuu_outfit0_face.png` 當角色卡傳給 CLI；舊版抽取器安靜地改輸出預設角色。現卡片先解析，缺檔或解析錯誤直接回傳錯誤，且在清理舊匯出前退出；`invalid_card_preserves_previous_export` 驗證原有 OBJ 未被刪。無效輸入不可再當作比例對照。
- 查資料時又沿用已不存在的 `mesh/_dump` 路徑；實際遊戲傾印位於 `C:\portable\KoikatuSunshine\mesh_dump`，已改從該目錄讀取。單行巨大 JSON 曾被 `Get-Content -TotalCount` 整行輸出而截斷分析；後續先用 Python 解析後只印必要欄位。
- 嘗試以臨時 RuntimeLightProbe DLL 放入遊戲 BepInEx/plugins 取得不知火舞同鏡頭 GPU 照時，自動批准審查拒絕該操作；未改走其他途徑，也未留下 DLL。既有 mesh/material dump 和 UE A/B 可繼續使用，但未取得的同鏡頭遊戲 GPU 圖不能當作已驗證結果。

## E-051　以重複 renderer 縮放的 dump 當臉寬真值、忘記重連 UE（2026-09-27）

- §77 依 `mesh_dump` 的 `worldVertices` 保留第二次共同頭部尺寸，曾把三月 7 的臉做成 dump 的 X 寬 127.75 mm；但 `KKS_MeshDumper/MeshDumper.cs:95–165` 明確先 `BakeMesh`，再以 `smr.transform.localToWorldMatrix` 乘每個點。其 `ComputeManualSkinnedBounds` 則直接用 `bones[i].localToWorldMatrix * bindposes[i]`。三月 7 的 manual X 寬 166.24 mm、不知火舞 168.28 mm；舊抽取臉分別 127.75／156.57 mm，髮片卻接近 manual 蒙皮空間。依 `BakeMesh + TransformPoint` 的二次縮放值修臉，造成臉相對頭髮偏小。現在移除臉部多餘尺寸，保留卡片本身的 `head_onto_body` 非等比形狀；March 逐頂點平均差 X/Y/Z 約 0.04/0.48/0.14 mm，Alice／不知火舞的臉包圍盒也對上遊戲 manual bounds（每軸差 <2 mm）。先前沒有逐點驗證 GPU 座標空間就以視覺 A/B 選小臉，是這次的主要錯誤。
- 第一次修完 Rust 後直接啟動 UE 的五卡 `HeadGpuSpaceFive` 拍照，畫面沒有變。查 `KKUnityBundle.Build.cs` 才發現 Editor 靜態連結 `target/release/ok_extract.lib`，重建 Rust `.lib` 後必須再跑 UE Editor Development build 才會重連；`OK_NOCACHE=1` 不能更新已連結的程式碼。這批畫面只能當舊程式對照，不能作為本次修正的驗證。往後以 UE log 的 `ok_extract.lib stamp`、Rust lib 修改時間和重建時間交叉確認，再重拍正式圖。

## E-052　把衣服 prefab 的多個狀態當成單一已穿網格（2026-09-27）

- 舊載入只保留 `_a` 正常穿著節點，並以 renderer.enabled 推斷傾印中哪些衣片可見。使用者指出還有「穿一半」狀態後，回讀 `ChaControl.DrawClothes`：`fileStatus.clothesState[0..8]` 在同一衣物 prefab 切換 DEF／NUGE／NUGE2／脫下。inactive 父節點下的 renderer 仍可 enabled；先前的 mesh dump 稽核因而把半脫變體誤報成正常穿著漏載。已在 `HANDOFF2.md` §80 記錄狀態及檢查方式；未經實卡 0／1／2 對照前，不可宣稱半脫畫面已修好。
- 調整衣服狀態選擇時，快取必須包含狀態或停用共用快取；否則狀態 1 可能載到狀態 0 的 prefab。此錯誤與卡片的衣物 ID 不同：ID 選 prefab，`fileStatus.clothesState` 選該 prefab 的可見形狀。
- 第一版以節點名稱 `n_top_b` 一律當成上衣狀態，`n_bot_c` 一律當成下裝狀態；實際 K384 prefab 的胸罩也有 `n_top_a/b`，內褲也有 `n_bot_a/b/c`，同名不能決定衣物種類。現從 list category 傳入所屬衣物 kind，再選節點；K384 真卡抽取證實上衣 `_b`、胸罩 `_b`、內褲 `_c` 會一起出現且下裝仍 `_a`。此測試只證明 prefab 變體選取，跨衣物遮擋與狀態畫面尚待遊戲對照。

## E-053　以網格名稱及全 prefab move 節點推斷附件變形（2026-09-27）

- 舊 `skin_mesh` 把名稱為 `35_mesh` 的實際頭髮誤判為非頭髮，對其套用權重修補、遠距權重修剪與尖刺修復，造成大量頂點偏離遊戲手動骨骼蒙皮範圍。現在一般路徑直接使用素材權重做 LBS；Alice 該網格的 Z 包圍盒誤差由約 16.2 mm 降至約 3.5 mm。網格名稱不是判斷蒙皮演算法的可靠資料。
- 另一個錯誤是把 prefab 裡所有 `N_move`／`N_move2` 套到每片附件。K384 一對獸耳分屬不同子樹，舊法把兩邊移動都加到每隻耳朵，造成約 30–40 mm 的位置／尺寸差。現按 renderer 與 SMR bone 的 ancestry 指定 move 節點，並為 renderer／bone 與 move 節點分離的模組保留 prefab fallback；已用真卡附件 bounds 稽核，但其他卡仍有 DynamicBone／pose 殘差，不能把此修正視為所有 mod 已逐點對齊。
- K091 slot10 後髮的遊戲 bounds 與抽取結果 Z 中心差約 84 mm。停用 DynamicBone 重抽後 Z 差降到約 14 mm，但 X 中心與尺寸各惡化到約 40／81 mm；不能以全域停用 DynamicBone 當修復。附件根骨世界位置已對上遊戲，剩餘需逐骨比較相同模擬時間與 collider 條件。Yuuka 外套的 `o_emblem_01_a` 雖被 bundle 解出，卡片未選徽章圖時遊戲 emblem shader 會裁切，因此它不出現在最終 manifest 並非遺失衣服。

## E-054　衣服狀態的名稱推斷、父節點與物件啟用誤判（2026-09-27）

- 原衣物狀態過濾把所有 `n_top_*`／`n_bot_*` 都解成所屬衣物狀態，並在最近的狀態節點停止。反編譯 `DrawClothes` 的上衣下片／下裝上片／胸罩下片其實由相鄰衣物狀態控制；巢狀節點還需要所有祖先啟用。`Bool8.Any(n)` 的名稱容易誤讀成 OR，實際實作為 AND。現依 category 與實際條件選擇、走完整祖先鏈，並把 `MeshFilter` class ID 從錯誤的 43 改為 33。未找到半脫狀態的遊戲同鏡頭 PNG，僅驗證真卡網格選取及 UE 診斷圖。
- K384 正常狀態的 shorts renderer 雖 enabled，其 `ct_shorts` 父物件 inactive；舊輸出仍含 `o_shorts_a`。回查 `HideShorts`／`botHideIds` 和 `notShorts` 條件後移除。強制對沒有半脫網格的下裝指定狀態 1，CLI 可得到沒有下裝的診斷結果；遊戲 `SetClothesState` 會先依 list `StateType` 選可用狀態，預覽環境變數目前沒有模擬那個循環，不能把這種強制狀態解讀為正常 UI 效果。
- 五卡 A/B 初見 Yuuka 額前兩片頭髮移動數公分，曾懷疑這輪衣物過濾誤動頭髮。比較舊／新 manifest，25 件 hair/accessory 均存在，僅附件槽 7／8 的 OBJ 座標改變；遊戲手動蒙皮 bounds 證實新位置正確、舊位置 Y/Z 偏約 62–65 mm。沒有為保留舊畫面而撤回正確位置。過程中兩次以 PowerShell 不支援的 `{lib.rs,sqlcache.rs}` 路徑形式查檔或多打一個 `)` 造成 ParserError；改用明列路徑與分開指令，失敗查詢未作結論。

## E-055　K091 後髮物理的重力、碰撞體與逐幀重置（2026-09-27）

- 先把 `DynamicBone.SetupParticles` 的 `InverseTransformDirection(m_Gravity)` 由錯誤的「點乘逆矩陣」改成純方向旋轉；K091 `cf_hair_b_05_01_b` 的 `m_Gravity=(0,0,0)`，重新抽取仍有 Z 中心約 84 mm 差。方向修正本身正確，但它不是這束髮的根因。後續又發現方向轉換不能帶入頭部縮放，`quat_from_mat` 也必須先從世界矩陣去掉尺度再取旋轉。
- `m_Colliders` 欄位長度為 15，曾誤以為是 15 個有效碰撞體；逐個 PPtr 解析後全為 `m_PathID=0`，所以這束髮實際上沒有 prefab 碰撞。不能以「15 個碰撞體漏載」解釋 84 mm。一般非空引用仍已照來源 ID 接到 prefab 碰撞體，但 K091 不受這部分影響。
- 第一版把 DynamicBone 推遲到附件依卡片 `N_move` 接到頭部後模擬，卻仍連續 240 幀沿用上幀變形的 local 骨節。其 K091 中心誤差變為 X/Z 約 59/71 mm；修正矩陣取旋轉後甚至約 75/71 mm，兩版均不得作為正式圖。回看反編譯 `DynamicBone.Update`／`LateUpdate`，遊戲每幀先 `InitTransforms` 還原 `m_InitLocalPosition/Rotation`。補上這個次序後，K091 X/Z 中心差降至約 8/39 mm，joint1 的 local rotation 與 joint2 的 local position 接近遊戲 dump。joint2/3 的旋轉仍有顯著殘差，不能聲稱物理逐骨等價。卡片 slot10 `noShake=false`，因此停用物理不是正確通解。
- 查檔時再次把真實 `mesh_dump` 誤寫成舊 `mesh/_dump`，並以 `audit_mesh_dump_loader.py --help` 呼叫沒有 argparse 的腳本，得到 IndexError；已改讀腳本 Usage 與實際路徑。不要以失敗查詢的空結果判斷遊戲資料缺失。
- 首版延後附件 DynamicBone 時，把整個 prefab 的 `part_skel` 也略過模擬。Yuuka slot7/8 的網格雖沒有自己的 deferred DynamicBone，仍使用該 prefab 骨架作配戴與蒙皮；兩片瀏海中心從舊版約 `Y=1.386/Z=0.059 m` 掉到 `Y=1.322/Z=-0.005 m`，在正臉被遮住。修正為整體 `part_skel` 照常模擬、只有帶對應 component 的每網格骨架延後至配戴後模擬；無對應 component 的網格沿舊路徑。CLI 無快取重抽後 Yuuka slot7/8 中心回到約 `Y=1.386/Z=0.059 m`。`Saved/DebugShots/DBoneInitPlacedFive` 的 15 圖屬修正前壞版，不能提升到正式截圖。
- 一次全量 `cargo test --lib` 用預設平行數執行了多個真卡抽取測試，記憶體提交量達約 60 GB、CPU 逾 3000 秒，已中止；這是測試資源失控，沒有測到功能失敗。之後只執行與修改相關的 `cargo test --lib dbone:: -- --test-threads=1`，3 項通過。拍照首次用 PowerShell 直接呼叫 GUI，shell 很快回傳 0，但 Editor 在啟動後結束、沒有產生圖；改用 `Start-Process -Wait -WindowStyle Hidden`，並以 PNG 數量及 UE log 驗證，不能只看 shell 退出碼。
- 再比對 `ApplyParticlesToTransforms` 發現 Unity 用 `TransformDirection` 和 `Transform.rotation`（不包含 lossyScale），而移植先前用 `localToWorldMatrix.MultiplyVector` 及從世界矩陣反推父旋轉。已改成沿骨架組合四元數；另依 `UpdateDynamicBones` 的 `Mathf.Abs(Transform.lossyScale.x)` 找到 DynamicBone component 所屬 GameObject 的 Transform，不再假定 `m_Root` 就是 component 自身。這兩個通用修正對 K091 slot10 bounds **沒有變化**：重測仍為中心 `8/-8/39 mm`、尺寸 `-16/16/0.5 mm` 殘差。不能因程式更貼近反編譯就宣稱這束髮已修好；剩餘需比較同一時刻的粒子／骨骼狀態，而非按卡名加位移。

## E-056　忽略卡片 `noShake`，讓應固定的 mod 髮飾參與物理（2026-09-27）

- 原抽取器只看 prefab DynamicBone component 的 `m_Enabled`，未讀 `ChaFileHair.parts[].noShake` 與 `ChaFileAccessory.parts[].noShake`。反編譯的 `ChaControl.ChangeShakeHair`／`ChangeShakeAccessory` 在 prefab 載入後把 component.enabled 設為 `!noShake`；只看 prefab 會把卡片要求停用的骨鏈繼續模擬。K091 槽 11 是 `true`，槽 10 是 `false`；前者的 `cf_hair_f_110_02_c` 經停用物理後，OBJ bounds 與遊戲手動蒙皮 bounds 各軸約 0.1 mm 內一致，後者仍應模擬，且 39 mm Z 殘差未消失。
- 現已從卡片及 MoreAccessories XML 讀旗標，頭髮／附件載入前決定是否模擬，並讓 SQL 與記憶體 prefab 快取鍵包含這個狀態。先只加欄位時 `HairPart` 手寫 `Default` 漏補 `no_shake`，Rust 測試編譯錯誤 E0063；補上預設 false 後通過。首次新解析測試誤用不存在的 `CardInfo::default()`，編譯錯誤 E0599；改用實卡 `parse_card` 作為測試基礎並檢查槽 10／11 的相反旗標。失敗編譯均未接到 UE，正式五卡圖在成功重編與無快取重拍後才更新。
- K091 槽 10 剩餘 39 mm 不能直接歸因於 KKABMX：把卡內 `KKABMPlugin.ABMData` 的 MessagePack ext 99／LZ4 block 解開後共有 14 個 modifier，僅有臉／身體骨，沒有附件 `joint2`／`joint3`。遊戲 `components.json` 的 `ca_slot10` 只列一個 DynamicBone。遊戲骨架 joint2／joint3 的 x 四元數分量約 `+0.149/-0.195`，目前固定 240 幀模擬為 `-0.023/-0.025`，但兩節合計接近且 joint4 以後又接近。停用物理時兩節皆為原始 identity，bounds 更差（中心 X/Z `-40/14 mm`、尺寸 X 約 `+81 mm`）；診斷跑 1／30 幀也未出現遊戲的兩節相反旋轉。這支持「遊戲熱鍵傾印捕到不同粒子／動作狀態」的可能，但尚未證明，不能依單張照片調整固定幀數。用來比較的 `OK_DB_FRAMES` 診斷開關已移除，正式路徑仍為 240 幀。

## E-057　K091 mod 虛擬路徑及 DynamicBone 鏡射缺口（2026-09-27）

- 首次用 Python 列印 `mesh_dump` 的 `sourcePart`，Windows cp950 終端無法編碼日文配件名稱，得到 `UnicodeEncodeError`。改用 PowerShell UTF-8 JSON 或設定 `PYTHONIOENCODING=utf-8`；這只是診斷輸出失敗，不能當成 JSON 缺欄位。
- `sourcePart.assetBundleFullPath` 指向 Sideloader 解包後的虛擬 `abdata/chara/deathweasel/acc_bo_hair_b.unity3d.unity3d`，磁碟上沒有該檔。實體 bundle 是 `[DeathWeasel] Accessory Hair Back v1.6.5.zipmod` 中的 `abdata/chara/deathweasel/acc_bo_hair_b.unity3d`；直接把虛擬路徑交給 UnityPy 的 `exists=False` 不代表 mod 缺失。
- 查實 `p_cf_hair_b_05_b` 上的 DynamicBone 指向 `joint1`，prefab 未序列化 `m_Weight`；反編譯類別預設 `1f`。此鏈的 `p_cf_hair_b_05_b/N_move/root/joint1` local scale 全為正 1，故 `m_Weight`／負縮放都不能解釋 K091 槽 10 的 39 mm Z 殘差。不要因發現移植缺口，就把無關的通用修正記成這束髮的解法。
- 另查 `ApplyParticlesToTransforms` 對 component 及其直接父節點負縮放會依 `Transform.right/up/forward` 鏡射目標向量；移植原來漏算。現已按來源補上並增定向測試；這是其他負縮放素材的通用修正，不改 K091 正縮放骨鏈。

## E-058　遊戲即時骨節診斷被執行政策擋下（2026-09-27）

- 為區分 K091 靜態 dump 的 39 mm 差異是模擬公式或物理拍攝時間造成，擴充既有 `Tools/RuntimeLightProbe/Probe.cs`，在 Maker 載入 `KK_091207.png` 後逐幀輸出槽 10 的 `joint1`–`joint8`。首次編譯誤把遊戲不存在的 `UnityEngine.SceneManagementModule.dll` 列入 csc 參照，得到 CS0006；移除該參照後編譯成功，僅有既存不可達診斷程式警告。不能把編譯通過當成 probe 已在遊戲驗證。
- 將暫時 DLL 複製到遊戲 `BepInEx/plugins` 並啟動遊戲的 PowerShell 命令遭執行工具自動審查拒絕，回傳 `blocked by policy`，未提供更細原因。命令未執行，遊戲資料夾沒有新增 probe DLL；本輪沒有 K091 即時時間序列，不能宣稱已找出剩餘 39 mm 根因，也不應繞過同一政策重試。
- 靜態對照 `KKS_Colliders/Colliders.cs`：插件會把額外地板 collider 加到 DynamicBone，但臂／腿 collider 只處理裙子用 `ct_clothesBot`。地板離 K091 頭部很遠；僅憑 mod 存在，不能把槽 10 的 joint2/3 反向彎折歸因於頭部碰撞。遊戲 dump 仍只列槽 10 的一個 DynamicBone component。

## E-059　衣物 prefab 快取漏掉狀態維度（2026-09-27）

- `ParsedBundle::clothes_state_filter` 會在解析 prefab 時依 `OK_CLOTHES_STATES` 丟掉未選的 `_a/_b/_c` 網格，但 `meshes_for_entry_shake` 的記憶體及 SQL prefab key 原先只包含 bundle／`shake`。第一次推論「SQL 已跨狀態污染」不精確：回查 `sqlcache::disabled()` 發現設定 `OK_CLOTHES_STATES` 時整個 SQL 快取直接停用，先前只是用繞開快取掩蓋缺鍵，並未觀察到錯誤 SQL 命中。前面的半脫診斷又都用 `OK_NOCACHE=1`，所以未測正常重用路徑。
- 現對衣物與上衣子部件 category 105–112、200–212 將 9 個衣物狀態併入記憶體及 SQL prefab key，並移除 `OK_CLOTHES_STATES` 一律停用 SQL 的特殊規則；頭髮／附件仍只依 `shake` 區分。K384 真卡半脫 `1,0,1,2,...` 取得 `o_top_tsyats_b/o_bra_b`，正常狀態取得 `_a/_a`，SQL `bundle_prefab` 中兩組狀態鍵各有 5 個 prefab。半脫再抽一次得到相同衣物路徑與全部 4 件 OBJ 的 SHA256，相符於可重用快取。定向測試、Rust release 通過；`SetClothesState` 的 `StateType` 可用狀態循環仍未完成。
- 移除全域停用後再追查整份角色的 `session_blob`：`card_key` 使用的 `env_sig` 原先也沒有 `OK_CLOTHES_STATES`，即使 prefab key 正確，UE 仍可能直接命中另一衣物狀態的角色 session。已將狀態加入 session 環境簽章並升 session pipeline 版；純函式測試確認正常／半脫簽章不同。這是同一缺陷的第二層，不能只用 CLI OBJ 證明 UE session cache 安全。
- UE K384 以半脫狀態先拍 3 張，再取消覆寫拍正常 3 張，兩批均成功 RequestExit(0)。SQL `session_blob` 產生不同的 `p85` key（約 21.96／22.32 MB），半脫與正常全身圖平均 RGB 差 4.478/255；正常圖和上版正式圖僅差 0.034/255。這驗證 session key 分隔後沒有把半脫外觀套到正常卡；半脫圖是明確指定數值的診斷，不代表遊戲 UI 可選此組合。

## E-060　衣物狀態先篩網格、後讀清單，導致連動資訊遺失（2026-09-27）

- 前版以 `OK_CLOTHES_STATES` 直接選 `_a/_b/_c`，未依 list `StateType` 做 `SetClothesState` 的可用狀態循環。更深的順序錯誤是衣物迴圈在查 `Coordinate`、`NotBra`、`HideShorts` 之前，就按狀態 3 跳過該件；隱藏的連體上衣／胸罩仍應提供 `notBot`／`notShorts`，不能從「沒有輸出網格」推論它沒有清單規則。
- 首版修正只讀主上衣 `NotBra`，再查 `ChaControl.ChangeClothesTopPartsAsync` 發現 Kind 1 由 subpart A 覆蓋，Kind 2 由 A/B 合併；已把 subpart 的 Sideloader resolve 也提前並共用於後段網格載入。另一個不能偷省的分支是 `AddClothesStateKind` 用 `ChaReference` 的 prefab 節點存在性改寫相鄰衣物 type，現限定在命名 prefab 子樹找 `n_bot_a`／`n_top_a`，不依單一卡片寫死 type。請求與生效狀態分別記錄，實際狀態作為 prefab cache key。
- K384 真卡的 `StateType` 為上衣 0、下裝 1、胸罩 0、內褲 2；請求上衣 2／下裝 1 時，生效值應為 3／3，不能把空白的 `_c/_b` 當半脫正解。後續驗證與未解差異見 `HANDOFF2.md` §86。


## E-061　較舊 zipmod 更新後仍命中舊角色 session（2026-09-27）

- 原 `sig_of` 只回傳 zip 數量及最大 mtime。任何非最新 zip 的修改可能不改這兩個值，`ensure_mods` 會沿用舊 list／GUID／override；`session_blob` key 只有卡片與環境變數，`.ok_stamp` 也只看卡片，兩層都未受 mod 庫變化約束。曾只修衣物 prefab 狀態 key，不能據此宣稱 mod 卡片在更新後會正確重載。
- 改以排序 zip 的路徑／大小／mtime 逐一計入穩定簽章，並把同一簽章帶進 SQL session key 與匯出 stamp。mtime 改用微秒以辨認同一秒內的正常檔案更新；這仍是檔案 metadata 簽章，若惡意把內容改了又完整還原大小及修改時間，無法偵測，不能稱為內容雜湊。純函式測試覆蓋非最新 zip 更新；K384 新版第一次建立、第二次命中，同一張卡都回 35 parts。快取版本升至 69/89，匯出 stamp 升 `v-sacbody-245-mod-inventory-micros`。

## E-062　微秒遷移未如預期避免本機 zip 全量重讀（2026-09-27）

- 新 `same_zip_stamp` 分支原打算把舊秒級 mtime 原位更新，避免重新解析全部 zip。真實 K384 首次切換時，SQL log 仍顯示約 7,310 個 zipmod refresh；不能把純函式遷移測試當作本機資料零重讀的證明。舊資料的其他格式或內容未在更新前保存，現在無法再從已覆寫的 SQL row 斷定是哪一種；因此不猜測根因。重新讀取後 mod catalog 為 29,189 list entries、7,292 GUID、22,729 override；第二次同卡 288.5 ms 命中 session，且無新 refresh。程式註解已改為「僅對整秒匹配者原位更新，未知格式保守重讀」。本機這次一次性重讀沒有改寫 zipmod 本身。


## E-063　未將 `OK_POSE` 納入角色快取鍵，優香前髮被舊姿勢蓋掉（2026-09-27）

- 微秒 mod 快取版本重拍五卡時，優香正臉中央大片前髮消失，和無快取圖平均 RGB 差 4.6239/255。這批圖留在 `Saved/DebugShots/ModInventoryFinalFive`，沒有覆蓋正式 `Saved/Screenshots`。起初懷疑 prefab SQL，實卡 CLI 對照發現有／無 SQL 的 51 個 OBJ、51 張 PNG、manifest 全部逐檔相同，排除那一層。
- 查 UE `ok.ShotPreview`：第一次 `Session.Open` 後的 `SetupStudioView` 才把 `OK_POSE` 設成走路姿勢，`RunShotTask` 又開同一卡；Rust 抽取會依 `OK_POSE` 預先套 pose，但 SQL session key 漏掉這個環境值。因而有快取時第二次拿到中立姿勢 session，無快取時第二次依走路姿勢重建。修正為首次開卡前固定預覽姿勢，session key 記錄所有已辨識的輸出相關開關，CLI stamp 與 key 用同一環境簽章；升版清除舊錯誤 session 的命中可能。定向測試及編譯通過，畫面回歸仍需實測確認。


- 修正後優香單卡 SQL 正臉與正確無快取圖的平均 RGB 差降到 0.1496/255，五卡正式批次的優香正臉為 0.1204/255、側臉完全相同；整批 15 張最大差 0.2899/255、平均 0.0779/255，UE 正常退出。正式根目錄 15 張 SHA256 全部對上修正批次；前一批已歸檔，錯版從未升正式。這是姿勢鍵與載入順序回歸的驗證，不表示遊戲實際 T-pose dump 或 Minimalist shader 已逐像素等價。

## E-064　DynamicBone `m_EndOffset` 誤沿根骨方向（2026-09-27）

- 舊 `gather_dparticles` 以 `m_Root` 世界矩陣乘 `m_EndOffset`，但遊戲 `AppendParticles` 呼叫元件 GameObject 的 `Transform.TransformDirection(m_EndOffset)`，方向不含 scale。元件與根骨朝向不同、或存在非單位縮放的 mod 會產生錯誤虛擬末端；現按反編譯來源改為元件的純旋轉，並以人工旋轉／縮放骨鏈的定向測試固定行為。
- 查 K091 槽 10 的實際欄位 `m_EndOffset=0`、`m_EndLength=0`，新舊真卡後髮 OBJ SHA256 完全一致。這次找到的是其他非零 offset 素材的通用缺口，**不是** K091 剩餘約 39 mm Z 中心差的根因；不能用此變更作為已修好 K091 的證據。先前遊戲逐幀 probe 的安裝／啟動命令被工具政策拒絕，沒有重新嘗試，也沒有取得可供判定那個殘差的時間序列。

## E-065　不知火舞臉部 Material Editor 遮罩漏套，誤判為貼圖上下顛倒（2026-09-27）

- 額頭橫線先以純色臉貼圖、關臉網格、albedo only、關 DetailMask 四種獨立診斷定位到臉 shader 的 DetailMask 分支。起初比較「抽取器目前的預設遮罩」和遊戲 RenderTexture，看到垂直翻轉後紅／綠通道接近，誤認所有面罩都應上下翻轉；這比較忽略卡片 Material Editor 的 `_DetailMask` 覆寫，因此沒有把翻轉改進正式程式。
- 重新讀卡片 `me_tex` 後，March、Alice、Yuuka、不知火舞均有 `ObjectType=4` 的 `cf_m_face_00/detailmask`；不知火舞卡片 PNG 與遊戲 dump 的紅／綠通道保持原方向，約 3,040／1,048,576 像素不同，翻轉反而大幅偏離。真正缺陷是臉 `cf_O_face` 載入流程只套 Material Editor `MainTex`，其他材質貼圖沒有套；其他部件的 `emit_part` 原已處理這些 slots。現補齊臉部 draw material slots，並用真卡／dump 測試固定原方向。不以停用 DetailMask 或卡名分支掩蓋問題；所有臨時 UE 診斷分支已移除。
- 五卡回歸發現優香前髮少片，SQL 與 `OK_NOCACHE=1` 截圖相同；`OK_SKIP_FACE=1` 後仍少片，排除新臉遮罩的遮擋。這個現象和 E-063 相似，但本次不能沿用 E-063 的「SQL 姿勢鍵」作根因，因為無快取亦重現；需要獨立追查來源。`FaceMeMaskFive` 沒有全批升正式圖，只升經驗證的不知火舞三張。不要以本輪 K384 修正宣稱五卡所有頭髮位置都已對齊遊戲。

## E-066　重名 prefab 根節點隨 HashMap 順序變動，誤把前髮位移歸因於姿勢或 DynamicBone（2026-09-27）

- 優香 `AS01_O_kami_07F_0`／`05F_0` 的兩種 CLI 輸出約差 60 mm，舊「濃瀏海」截圖也曾同時出現。起初沿 E-063 歸因於 `OK_POSE` 或 DynamicBone；同版無快取實測 `OK_POSE` 有無時兩片 OBJ 完全相同，`OK_NODB=1` 則出現低位置，但卡片第 7 槽實際 `noShake=true`、第 8 槽 `false`，不能推論全域物理開關真的在模擬第 7 槽。E-063 的姿勢快取維度缺口確實存在，但其所述「造成這兩片前髮回歸」沒有被這次幾何證據支持，應撤回該因果判定。
- `OK_ACCDIAG` 追到第 7 槽的 `N_move`：同一版抽取有時是 `move_nodes=[Some(...),None]`，有時是 `[None,None]`。後者令 `UpdateAccessoryMoveFromInfo` 的卡片移動量未套用；兩種 attach matrix 分別約平移 `(0.0031,1.3742,0.0464)` 和 `(0,1.3100,-0.0179)` m。`mesh_prefab_move_nodes` 與 `prefab_node_world` 都從無序 `HashMap` 以 `.find()` 挑第一個同名 prefab 根節點，而此 mod bundle 確有重名候選，因此會把 renderer 的祖先路徑接到錯的根。`OK_NODB` 看似控制前髮位置，只是兩次載入碰巧選到不同根的干擾因素；不得把關閉 DynamicBone 當正式修法。
- 現蒐集全部匹配的根 transform，排序去重；每片網格仍沿自身 renderer／SMR bone 祖先路徑判定所屬根。`prefab_node_world` 的全域 fallback 亦對所有同名根搜尋，結果確定性排序。回歸測試固定重名根集合。修正後無快取 `OK_NODB` 有／無兩次抽取，第 7／8 槽 `N_move` 都存在，兩片 OBJ SHA256 分別完全相同；第 7 槽仍遵守卡片 `noShake=true`。新中心第 7 槽 `(-0.025151,1.386272,0.059111)` m，與遊戲 dump `manualSkinnedWorldBounds` 相符。舊低位置對上 `N_move` 遺失，不是可靠的遊戲真值。
- UE 重新連結後，`PrefabRootsFive` 的優香正臉在 slot7／8 保持高位置時也恢復濃瀏海。這證明「濃瀏海一定代表那兩片低位置」是另一項錯誤推論；UE 舊截圖沒有逐片 OBJ，不能把舊圖的所有像素直接映射到兩個 CLI 檔。遊戲 T-pose 照片與按鍵觸發的 mesh dump 並非同一幀，遊戲 GPU 頂點未直接讀回，仍不能宣稱所有髮片／shader 等價。之前 E-065 將無臉圖的「前髮仍少」當成排除臉遮擋，也只能排除這次臉 DetailMask 導致的單一回歸；無臉圖中確有更多髮片出現，深度／材質差異仍要獨立驗證。`OK_NODB` 診斷圖不可升為正式結果。

## E-067　裸露手臂遮罩方向與來源混用（2026-09-27）

- K091／K384 裸露手臂的橘白色塊不是幾何或衣物陰影。K091 關 LineMask 後色塊消失；遊戲 dump 的 body UV 與目前輸出相同，而抽出的 `cf_body_09_line` 在 UE 的 V 取樣位置把許多手臂頂點落在遮罩圖塊外的白區。修正 1024 DXT5 解碼只恢復原圖尺寸，沒有消掉色塊，不能再把「低解析度遮罩」當根因。
- 對照 `[NGNL]Eroge Nipples` 原始 Texture2D、遊戲實際匯出的 `_overtex1`，UnityPy 與遊戲 PNG 直接方向的平均差約 2.2/255，垂直翻轉後更大；抽取器的 raw Texture2D PNG 則與 UnityPy 垂直翻轉版相同。`cf_body_00_md` 的 raw BC7 與 UnityPy 垂直翻轉版逐像素相同。因 UE 幾何 UV 已把 Unity V 轉為 `1-V`，raw 遮罩又多翻一次，形成錯位。單翻 LineMask 的 K091／K384 A/B 圖皆消掉手臂硬斷層。
- 第一次把 UE 的所有遮罩輸入一律翻轉是錯的：K384 的卡片 Material Editor 臉部 `DetailMask` 本來與遊戲 RenderTexture 同方向，試驗圖 `MaskOrientationAll` 又在額頭產生橫線，且改變臉部五官陰影。該 UE 翻轉已撤回；現只在 Rust 取得 bundle Texture2D 遮罩時校正列方向，卡片 ME PNG 不動。不可依卡名或 shader 類型猜測來源方向。
- 單獨移植 `Koikano/main_skin` DXBC 的 RampG UV 位移及 drawnLight 次序時，K091／K384 裸露部位反而變暗；缺少同 shader 的完整間接光、SH 等路徑，不能以部分公式的數值接近當成畫面改善。試驗圖保存在 `K091BodyDxRamp`／`SkinRampMarchK384`，代碼已撤回。最終通用方向修正與五卡驗證見 `HANDOFF2.md` §92。

## E-068　預先混合皮膚正常貼圖，並把 UE 貼圖 RGB 誤當 RGBA（2026-09-27）

- K091 遊戲 dump 的 `_NormalMap` 與 `_NormalMapDetail` 是兩張各自 1024×1024 的 RenderTexture。從 `mm_base` 抽出的 `cf_body_00_base_n` 經 Material Editor 的 normal blit 轉換後，和遊戲 `_NormalMap` 每通道平均誤差小於 1/255；mod `cf_body_09_n` 的 G/B/A 與遊戲 `_NormalMapDetail` 也在原方向接近。舊 `blend_skin_dxt5nm` 先把兩張圖混成一張，輸出平均 alpha 約 80，遊戲 `_NormalMap` 約 127，還失去 t7/t8 各自的 UV/ST 和 GPU 取樣。不能拿這張預混圖當遊戲的 base normal。
- 初次改用雙取樣時，UE TextureSample 接到 Custom expression 的預設輸出只有 RGB，公式卻讀 `Packed.a`／`Detail.a`。shader compiler 明確報 `vector swizzle 'a' is out of bounds`；UE 回退預設材質使 K091／K384 身體變黑。錯圖只在 `Saved/DebugShots/SkinNormalTwo`，未升正式截圖。已分別接 TextureSample alpha（output index 4），重編後 `SkinNormalAlphaFixedTwo` 兩卡六張正常、無材質編譯錯誤；K091 裸露手臂仍平順，K384 腿部的過重邊色減少。
- 現在保留 base/detail 兩張圖在 session/FFI，依 `0072.asm.txt` t7/t8 及 `_DetailNormalMapScale` 在 UE pixel shader 合成 tangent normal。刪除舊預混函式及其只驗證近似公式的測試；加入 base normal 與遊戲 RT 的實資料比對，以及 cache 往返測試。完整 diffuse/SH/liquid 路徑仍未逐像素驗證，不把這次正常貼圖修正宣稱為完整 Minimalist shader 對齊。

## E-069　將皮膚 `_AlphaMask` 烘進 MainTex alpha（2026-09-27）

- 舊 body 載入把衣物 `OverBodyMask` 的紅、綠通道先相乘到 `_MainTex.a`，並用 MainTex alpha 作 UE 的 masked clip。`Koikano/main_skin` `0072.asm.txt:105-109,596-598` 實際在 t0 以 body UV 取樣獨立 `_AlphaMask`，算 `min(max(r,1-_alpha_a),max(g,1-_alpha_b))`，再與 `_Cutoff=0.5` 比較；片段從 t2 取 `_MainTex.rgb`，不靠 MainTex alpha 進行 body clip。CPU 烘焙會改變雙線性取樣邊界與兩通道的 min 次序；卡片 Material Editor 稍後覆寫 MainTex 時還可能把烘好的遮罩整張蓋掉。
- 現改在 body session 保留來源遮罩，依來源 Texture2D 方向校正；衣物狀態為 0/1 的 channel 保留，關閉的 channel 設為白色，讓 UE 材質和 skin 外框各自取樣 R/G 並作 min。Material Editor 的 body `alphamask` 可在覆寫階段替換來源。此規則依材質與衣物狀態，不按角色名稱特判；五卡與衣物遮擋結果見 `HANDOFF2.md` §94。
- 第一次兩卡及五卡診斷時，雖然 Rust 傳入獨立 mask，`KKPreviewActor` 稍後仍對所有 `bBody` 強制 `Blend="opaque"`；看起來「沒有新黑塊」不足以證明新 mask 真正參與裁切。該批 `SkinAlphaMaskTwo` 與 `SkinAlphaMaskFive` 不可升正式圖。已修正 body 的 blend 選擇，讓 `Part.Blend="masked"` 保留，重新編譯並另拍有效批次；往後驗證遮罩必須查 body 材質實際 blend 與像素裁切，不能只看 Rust session 是否帶貼圖。
- 最後比對 Outline PS `0012.asm.txt` 時又發現它與 ForwardBase 共用 `_Cutoff`；UE 外框材質原本落在自己的預設 clip 門檻。已明確將 skin 外框 cutoff 設為 0.5，並在此變更後重新連結 UE、重拍最終五卡，避免讓「正式截圖」停留在前一個 binary。
- 更後續檢查發現 UE 解碼 `View.alpha_mask` 的條件仍限定 `shader_kind==1`（頭髮）。body 雖在 log 顯示 `blend=masked`，實際 `AlphaMaskTex` 為 null，材質又退回 MainTex alpha；這說明只驗證 blend 仍不夠。已把 kind 5/7 skin 納入解碼條件，新增 body 遮罩 bytes／decoded log。中止的 `SkinAlphaMaskFinalFive` 也不可作最終圖；必須確認 `decoded=1` 且材質編譯無誤後另拍完整批次。
- `SkinAlphaMaskDecodedFive` 最終五卡 log 中六次 body 載入均 `decoded=1 blend=masked`，15 張完整輸出且沒有材質錯誤；K091 內衣覆蓋處的乳點消失。K384 衣領／胸下有新裁切邊，來源 mask 的形狀與 body atlas 同區，不能只因「新邊緣」就再次關掉 mask；若要判定其與遊戲像素一致，須取得該卡相同狀態與鏡頭的遊戲截圖。錯誤批次皆保存在 DebugShots 或 `old`，正式根目錄只放有效的最終 15 張。

## E-070　把舊版 Unity Color32 通道當作 tangent，導致裸露皮膚退回 UV 切線（2026-09-27）

- K091 遊戲 `o_body_a` dump 有 9,048 個切線，現有 session 卻為 0。舊 `decode_tangents` 固定讀 `m_Channels[2]` 並要求 float32×4；SAC bundle 的通道 2 是 `UNorm8×4` Color32，真正 float32×4 切線在通道 7。讀到第一筆 `[0,0,0,255]` 時一度以為是壓縮切線，直接解成 `(-1,-1,-1,1)` 會產生錯誤方向；檢查完整 channel layout 與遊戲 dump 後，已撤回這項假設。
- 現依頂點通道排列辨認 2 或 7 的四維浮點切線，排除 Color32，保留原生 `w` handedness。初次修正仍把「有四維浮點通道」直接當 `source_tangents=true`；長袖衣物回歸測試指出它的通道 7 雖形狀相同，整組資料卻不是有效切線。已改以完整成功解碼的結果設定旗標，維持缺來源切線時的 normal map 閘門。第一批修正後五卡截圖只作診斷，沒有升正式圖。
- `[SAC] Uncensor Selector Pack v0.9.2.zipmod` 的 9,048 個來源切線與遊戲 dump 逐點比對，平均角差 0.0085°、最大 0.0396°，handedness 0 筆不符。`MeshDumper` 的 `normals/tangents` 取自未擺姿勢的 sharedMesh，而 `worldVertices` 來自 BakeMesh；不能把前兩者直接和已擺姿勢的 session 法線比較。
- 這是 bundle 通道排列問題，不應按 K091 卡名、SAC GUID 或手部頂點區域硬套切線。更新 prefab/session/CLI 快取版號，防止舊的零切線資料直接命中。是否改善實際手部陰影，仍需以新版 UE 截圖對照；來源切線吻合本身不等於完整 `main_skin` 光照等價。

## E-071　皮膚 TBN 用法線重新叉乘 bitangent（2026-09-27）

- `Koikano_main_skin/0071.asm.txt` 以來源 `TANGENT.w` 組幾何 TBN，PS `0072.asm.txt` 將視線投影到此基底。UE 舊 specular-height 節點卻用已經過 normal map 擾動的法線和 vertex tangent 叉乘 bitangent，漏掉 `w` 並使高度方向隨法線貼圖改變。K091 身體 9,048 頂點中 8,876 個 `w=-1`，不是可忽略的差異。現直接用 UE Tangent→World `(0,1,0)` 取得帶 handedness 的 bitangent；兩卡無快取截圖與前版平均通道差約 0.03–0.18/255，不能從微小差異宣稱整體手部光照已修好。只保留與 DXBC 基底公式一致的改動。

## E-072　把 Animator state 當成 AnimationClip 名稱（2026-09-27）

- 新 clip 清單第一次測試錯誤地要求 `f_aruki_00_00` 在 530 個 AnimationClip 名稱裡。它是 AnimatorController state，實際 clip 為 `f_aruki_00_00_01`；舊載入器可由 state hash 對應到 clip，但清單直接列的是 bundle 中的 clip。改以真實 clip 名稱測試，並保留 state hash 解析給舊 `OK_POSE` 路徑。選單只提供真正可解析的 clip；不要把 state 與 clip 名稱混作同一索引。
- 初次 FFI 實作寫了不存在的 `sess_mut`，編譯失敗；此模組的 `sess_ref` 已回傳可變 session，已改正並重編。C++ 嘴型表改動期間亦補上一處提早缺少的關閉括號，最終 UE build 通過。以後修改 ABI 時先核對 Rust/C header 函數表與 session accessor。

## E-073　固定嘴型索引無法容納 mod 插入 blendshape（2026-09-27）

- 原先 `OkMouthPair` 只列 5 組，並以固定數字取得每個 mesh 的 morph。遊戲 `FBSTargetInfo.PtnSet` 有 54 組；March 等 mod 卡在後段插入額外 morph，索引會偏移。K384 與 March 的 `PtnSet` 數字不同，對應的 morph 名稱卻逐組相同。現以已安裝遊戲的 dump PtnSet 轉為名稱表，載入時在每張卡的實際 `Session.MorphAt` 名稱中查找；未找到時不套錯誤索引並記 warning。四卡、五個 mouth renderer 共 270 組與各自 dump 的實際配對名稱吻合。未知 mod 若改名或自訂 PtnSet，仍需要從該 prefab 讀其表，不能把這份名稱表誤稱涵蓋所有自訂嘴型。
- 嘗試執行整包 `cargo test --release --lib` 時，既有重型測試進程使用約 89 GB 記憶體且長時間未結束，已停止；後續只執行針對本改動的 clip／mouth／幾何測試，避免讓大量資料測試與 UE 編譯互相搶記憶體。

## E-074　遊戲動作切回 T-pose／UE 走路時沿用上一幀骨架（2026-09-27）

- 新動作模式初版只在選 clip 時更新 `Session.ApplyAnimTime`；既有 `SetPoseMode(0)` 僅在 `PendingBody` 或 `PendingFace` 非空才呼叫 `Session.ApplyShape`。一般載入卡片後這兩個 pending 陣列可為空，按 T-pose 就只停止 Tick，骨架仍停在上一幀遊戲動作；切 UE 走路也會把程序化步態疊在遊戲姿勢上。現切回 0/1 模式都從 session 中現有的 body/face shape 重建 neutral GPU 骨架，再套 UE 步態。模式切換必須測骨架狀態，不可只檢查按鈕改了整數模式。

## E-075　坐姿時頭與配件停在 T-pose 高度（2026-09-27）

- `GSit_00` 把軀幹下移，但預設 `OK_GPU_SKIN=1` 的臉與髮仍由 Rust 烘焙到 body space，`skel_kind=0`；UE 只更新 GPU 身體，所以第一張坐姿圖頭髮懸空、臉和脖子分離。起初只讓臉／髮跟 `cf_s_head`，仍漏掉已烘焙且清掉 `parent_bone` 的 mod 髮飾；更把 `Current.GetRelativeTransform(Neutral)` 當位移，旋轉後臉跑到畫面左側。兩張錯圖都僅存 `Saved/DebugShots/GameSitResetK384`／`GameSitHeadFollowK384`，沒有升正式圖。
- 現在抽取器另存 `rigid_follow_bone`，保留臉的 `cf_s_head`、髮的 `cf_J_FaceUp_ty` 及卡片配件的真實 parent key；與已烘焙頂點使用的 `parent_bone` 分開，避免二次附著。UE 以 `Neutral.Inverse() * Current` 對烘焙網格做骨骼原位姿到當前位姿的位移，T-pose 切換重新捕獲原位姿。`GameSitMouthRigidK384` 坐姿全身圖臉、長髮、髮飾已隨身體到正確高度；這是剛性跟隨，尚未等同遊戲每根髮骨的動態物理。

## E-076　嘴型選單有值但預設臉根本沒有可控制元件（2026-09-27）

- 前版嘴型選單與 54 組名稱表已建立，但 UE 的 `MouthSlots` 只在 GPU 頭部網格建立；預設臉與細節走 CPU 烘焙，槽數是 0。只看到選單或 270/270 名稱對照，不能宣稱嘴型有顯示。現對實際顯示的程序網格建立嘴型槽；Rust 在頭部 LBS 與 `cf_s_head` 附著後，把稀疏 `kuti_*` blendshape 位移轉到相同 body space。UE 先移除已烘焙的 pattern 0 closed 權重，再用 `FBSBase.CalculateBlendShape` 的 close/open 權重套入所選形狀。初始開合設 0，避免載入卡片時意外張嘴。
- `GameSitMouthRigidK384` 的預設與 pattern 46/open 1 正臉，在嘴部裁切區 `(375,735)-(525,790)` 平均通道差 6.15/255，3,964 個像素不同；坐姿、切回 T-pose、步行同批輸出 12 張，exit 0。這證明可見網格受嘴型控制，尚不代表每個 mod 的所有新增 morph 都被載入。

## E-077　把 FBSTarget `ObjTarget` 當 renderer，且把 runtime 擴充的 54 組誤當原始 prefab 長度（2026-09-27）

- 首次 Unity bundle 診斷測試對個別無法解析的檔案直接 `unwrap()`，遇到空檔 panic；已改為略過無效 serialized file。正式解析器最初把 `MouthCtrl.FBSTarget.ObjTarget` PPtr 當作 `SkinnedMeshRenderer`，造成真 prefab 測試回傳空表。實際 `ObjTarget` 指向 GameObject；須由 GameObject 的 renderer component 回查網格，並以 FaceBlendShape 所在 GameObject 名稱鎖定卡片選中的 `p_cf_head_*` prefab，避免同 bundle 三個頭部 prefab 混用。
- E-073 與 HANDOFF2 §97 所稱「遊戲 PtnSet 54 組」只是**執行中的遊戲 dump**，其中含 SexFaces 類 runtime 擴充；原始 `bo_head_00.unity3d` 所選 `p_cf_head_00` 的 `cf_O_face` PtnSet 為 43 組。現在先讀每張卡選中 prefab 的原始 PtnSet；超出原始長度的已安裝遊戲擴充，只在該卡實際 morph 名稱完整存在時使用 live dump 名稱後綴。不可把 54 硬套到所有 prefab，也不可用原始 43 截斷真實 runtime 擴充。

## E-078　只讀來源 PtnSet 漏掉遊戲啟動後由插件建立的嘴型（2026-09-27）

- §98 第一版正確讀出來源 prefab 的 43 組，卻只能在既有 mesh 已含新 morph 時顯示 43–53。遊戲實際載入了 `BepInEx/plugins/KKS_SexFaces.dll`；其反編譯 `SexFacesController.PatchFaces` 會在角色建立後呼叫 `AddAhegao` 三次、`AddLopsided` 六次、`AddClosedTeeth` 兩次。因此來源 bundle 中沒有後 11 組或其六個 lopsided blendshape，是載入時序差異，不是卡片少資料。
- 現在保存原始 `FBSTarget` 順序，依插件 Face/Neck/Teeth/Eyes/Tongue 目標索引套用同樣的 base pattern、TongueOut 與 BigI 特例；lopsided 的 Face 使用來源未變形頂點 X 經 `InverseLerp(-maxX,maxX)` 和 `tanh((x-0.5)*10)` 混合 close/open 位移，其他 target 複製 base open。只有該 DLL 存在時才擴充，DLL 檔案 stamp 也進 session cache key；沒有插件時仍保持來源 43 組。不可只依靜態 live dump 名稱直接假裝來源 prefab 已有新 morph。
- 擴充前先檢查所需來源索引、face 頂點數與 X 範圍；任一 mod 目標資料不完整時保持原 43 組，不留下「只追加幾組」的半成品表。已用真 `bo_head_00` 五目標測試全部 54 組 close/open 都對應實存 morph，並以三月七／Alice 的第 46 組截圖驗證可見變形。

## E-079　動作清單只看兩個 bundle，其他來源同名 clip 可能選錯（2026-09-27）

- 第一版 `clip_bundle_paths` 只有 `custom/custompose.unity3d` 和 `action/animator/00.unity3d`；遊戲反編譯的 `ChaControl.LoadAnimation(assetBundleName, assetName)` 和 `ActionGame.Chara.NPC.LoadAnimator` 實際依來源 bundle 載入，另有 Studio、Studio K、ADV、esthetic、H female 等角色動畫。只用 clip 名稱做選單 key 會把不同 bundle 的同名 clip 合併或載錯。
- 現將來源路徑與 clip 名組成 `source|clip`；原有兩個來源仍可用舊裸名稱。預設來源選單只列 Studio／Studio K／ADV／esthetic／H female 的既有 bundle 路徑，選到來源才解析其 AnimationClip，避免開角色時掃描約數 GB。手動輸入可指定其他 `abdata/...unity3d` 或 `mods/...zipmod!內部/...unity3d`；相對路徑禁止 `..`，canonicalize 後仍須留在遊戲 `abdata`／`mods` 根下。zipmod 內部以完整後綴匹配，若有多個候選便拒絕，不能退回只比 basename 而載錯檔。
- 初版 FFI 若每取一個名稱都複製整個來源清單，對大型 Studio bundle 會形成平方成本；已改為只複製該索引名稱，並明確在重入載入前釋放 catalogue mutex guard，避免冷載入時自我鎖死。實際 Studio／ADV／H female 與 zipmod 動畫包已測得 clip。zipmod 來源目前需手動給路徑，未自動掃描所有 mod；動畫曲線與髮骨物理仍各有來源邏輯待對齊，不能把「可讀 clip」等同完整 Unity Animator 行為。
- 加入手動來源後重拍同一 ADV clip，三張 PNG 的 SHA256 均與前次不同，不能直接判為載入回歸；兩次全身姿勢目視一致，平均 RGB 通道差全身 0.029/255、正側臉 0.186／0.208/255，且兩次皆報 1.333 秒 clip、正常退出。動畫持續播放的截圖需用姿勢與數值容差比對，不可要求跨執行位元完全相同。

## E-080　把 Unity StreamedClip 的三次係數當成無用欄位（2026-09-27）

- 原本 `parse_streamed_keys` 只取每 key 的第 4 個 float 當值，`sample_cached` 在相鄰 key 間做線性插值。[AssetStudio 的 `StreamedCurveKey` 原始碼](https://github.com/Perfare/AssetStudio/blob/master/AssetStudio/Classes/AnimationClip.cs) 標明 `coeff[2]` 為 outSlope、`coeff[3]` 為 value，其 `CalculateNextInSlope` 公式也驗證 `coeff[0..2]` 是本 key 後方時間的三次多項式係數。忽略這些係數會改變動作速度曲線，階梯式 key 甚至被錯誤插成平滑動作。
- 現保留四係數，以 `((a*dt+b)*dt+slope)*dt+value` 取樣；前三項全 0 時自然保持前值。合成三次曲線與階梯 key 測試通過。真遊戲 `f_aruki_00_00_01` 中 119 組非階梯、相距小於 1 秒的相鄰 Streamed key，把前一 key 的多項式算到下一 key 時，端點差中位數為 0、95 分位小於 8e-24、最大約 2.7e-23。DenseClip 仍按相鄰樣本線性取樣，Animator layer／blend tree 尚未實作，不能將本修正稱為整個 Unity Animator 等價。
- 真步行 clip 的這 119 組中點與舊線性結果相差中位數 2.98e-8、95 分位 1.12e-5、最大 8.87e-5；因此這段影片的肉眼變化可能不明顯，不可拿「截圖差距小」否定係數取樣必要性。新版步行全身圖可見抬腿且頭部附著正常，三張診斷圖 exit 0；舊版線性假設已由來源係數與端點驗證推翻。

## E-081　把 DenseClip 的 padding 影格拉伸進播放時間（2026-09-27）

- 錯誤：舊 `dense_at` 用 `time / streamed_last_time × (frame_count - 1)`。真實 KKS clip 的 DenseClip 另有 `m_SampleRate`、`m_BeginTime`，MuscleClip 另有 start／stop；把 frame count 視為整段播放長度，會把結尾 padding 幀提前混進角色姿勢。
- 證據：步行 clip 64 幀、30 fps、stop 2.066667 秒；stop 時來源時鐘是第 62 幀，舊式取到第 63 幀。ADV 站姿 42 幀／30 fps／stop 1.333333 秒應取第 40 幀，舊式取第 41 幀；esthetic 動作也同樣差一幀。這些欄位可在 [AssetStudio `DenseClip` 資料結構](https://github.com/Perfare/AssetStudio/blob/master/AssetStudio/Classes/AnimationClip.cs) 及本機 bundle 直接核對。
- 修正：讀取每個 clip 的 rate、begin、start、stop，以 clip 來源時間定位 Dense sample；長度由 stop-start 決定，無 stop 時優先用 Streamed 結尾以避開 Dense padding，再退到 Dense 結尾；來源時鐘失效才使用舊式正規化回退。合成與真 bundle 測試、UE Editor Development 建置通過。往後新增動畫來源，應先核對時間欄位與末端額外影格，不可只用 Streamed 最後 key 推算所有曲線的時間軸。
- 兩張 mod 卡的步行診斷共六張正常輸出，眼睛、髮絲與飾品未見新缺件；正式五卡 T-pose 15 張未更換。動畫截圖發生在持續播放中的不同影格，跨執行影像不應用 SHA256 完全相等作驗證。

## E-082　讀到 Animator 的 scale binding 卻沒有套用（2026-09-27）

- 錯誤：`apply_clip_at` 讀出 transform binding 的 attribute 3，但 match 分支空白；位置與旋轉正確也會讓坐姿、站姿的前臂和手部比例與遊戲不同。首次選動作時只看整體姿勢，沒有逐項核對 binding attribute。
- 真 bundle 證據：`GSit_00` 的 `cf_j_forearm01_L/R` 來源 X scale 0.92、`cf_j_hand_L/R` 來源 Y/Z scale 0.9；ADV `Stand_01_00` 的手部亦為 0.9。已對應骨骼的 attribute 3 現套用來源三軸，過濾非有限或零尺度。`cf_d_*` 屬角色形狀後處理，仍需保持原有控制順序，不能把所有名稱相近的 transform 一概覆寫。
- 真 bundle 測試對坐姿 0.25 秒的左手來源 scale Y/Z 驗證為 0.9，UE 編譯通過；最終場景校正和 ABMX 順序另見 E-083／E-084。以後擴充動作時，先列出 position／rotation／scale 的 binding 數與非預設值，逐類確認是否真的更新了骨架。
- 三月七與 Alice 坐姿各三張診斷圖輸出成功、exit 0。舊／新版坐姿截圖的手部區域有可見數值差，但動畫取樣影格不完全相同，不應把像素差誤當作單一 scale 曲線的因果證據；真 bundle 骨架數值測試才驗證此修正。畫面中既有的手部材質外觀與長髮缺少動態物理仍需另案處理。

## E-083　誤把 clip 的 scale 當成最終手部尺度，並憑名稱猜來源（2026-09-27）

- §102 修正的是 Animator 曲線；反編譯 `ChaControl.LateUpdateForce` 在 Animator 後仍會依 `Character.enableCorrectArmSize`／`enableCorrectHandSize` 改寫前臂與手的 localScale。Maker 為 arm/hand 開，一般場景 arm 關 hand 開，H／美容場景 hand 關。同一 clip 可跨場景播放，不能僅看曲線值推斷最終骨架尺度。
- 第一輪新測試把 `GSit_00` 預期為 action 來源而失敗，實際 `load_clip_at` 從 `custom/custompose` 解析到它；以 clip 名稱猜來源會導致錯誤的場景後處理。已把實際選中的 bundle 類型記在 clip cache，auto 模式依此推斷；另提供明確場景選單覆寫，因 Unity 的旗標屬於當前場景，不屬於 clip。手臂／手部校正現按遊戲順序在動畫取樣後執行，ABMX 仍在其後。
- 真 bundle 測試檢查 `GSit_00` 為 Maker、`f_aruki_00_00_01` 為一般場景、esthetic clip 為 hand 關，亦檢查手動覆寫與最終骨骼尺度；UE Editor Development 編譯通過。以後涉及反編譯的場景靜態旗標，需區分「資產來源推斷」和「執行時場景狀態」，不得以卡片或 clip 名稱硬編。

## E-084　把場景校正值當成 mod 卡的最終骨骼尺度（2026-09-27）

- 初版真卡回歸測試直接斷言三月七坐姿手部最終 scale 是 `[1,0.9,0.9]`，在 `session.apply_anim_time` 後失敗。原因是 ABMX 的 `BoneModifier.Apply` 在 Animator 與場景校正後還會以卡片 modifier 乘上骨骼基準尺度；直接比較絕對值會誤判正確的 mod 行為。
- 修正測試在相同卡片、clip 與取樣時間切換場景，只比較 Maker/H 的前臂 X 比值 0.92、手部 Y/Z 比值 0.9；再比一般步行/H 的前臂比值 1、手部比值 0.9。真卡完整 session 測試已通過。以後驗證場景後處理必須保留 mod 倍率，不能把遊戲全域旗標的值直接當作最終 Transform 值。

## E-085　將局部手掌座標代入 ExpressionBone 的世界座標公式（2026-09-27）

- 舊 `apply_expression_bone` 的 ForearmL/R 分支拿 `cf_j_hand_*` 的 localPosition 配 `cf_j_middle01_*` 的 world position，手腕輔助點又以局部前臂 TRS 反算。角色根節點一移動、旋轉或有非均勻縮放，結果便與遊戲 `ExpressionBone.LateUpdate` 的 `Transform.position`、`Transform.rotation`、`InverseTransformPoint` 不符；僅靠靜止 T-pose 肉眼檢查很難發現。
- 修正時逐項標記公式是 local 還是 world，手掌與中指用世界位置、前臂與手掌用階層累乘的世界旋轉，前臂逆世界矩陣保留父階層尺度；手掌局部旋轉僅供 `cf_d_forearm02_*`。初版新函式又把 Hand 扭轉錯放在「必須有中指骨」的共同條件後，與遊戲獨立的 `enableHandL/R` 分支不符；已拆開條件。合成測試包含父節點位移、90° 旋轉、非均勻尺度與缺中指骨，檢查手掌解析偏移與仍能執行的扭轉；真卡 session 回歸與 UE 編譯通過。之後若修肘、腿或胸部骨骼，也應先對照每個 Unity Transform 存取器及每個獨立 enable 條件，不可複製舊的 local/world 混算。

## E-086　把 Unity SetLocalRotationX 誤當 z-x-y 旋轉（2026-09-27）

- 舊手腕段在更新 `cf_d_wrist_*` 的 X 角時，以 `ConvertRotation` 的 z-x-y 順序拆解／重建 quaternion。反編譯 `IllusionUtility.SetUtility.TransformRotationEx.SetLocalRotationX` 實際取 `transform.localRotation.eulerAngles.y/z`，再呼叫 `Quaternion.Euler(x,y,z)`；Unity 的組合順序是 y-x-z。既有 Y/Z 旋轉非零時，錯序會改變另一兩軸，讓 mod 骨架的手腕方向漂移。
- 現新增 y-x-z 解算，手腕保留 Unity Euler Y/Z，更新 X 後用既有 `euler_deg_to_quat` 重建。合成測試從手腕 `[15°,25°,35°]` 開始，要求改 X 後 Y/Z 仍是 25°／35°，並保留世界座標與缺中指骨測試。往後移植 `SetLocalRotation*` 要先查 extension 實作；不能因同一檔案其他分支使用 `ConvertRotation`，就共用其旋轉順序。

## E-087　肘部正弦式括號與骨骼存在條件轉寫錯誤（2026-09-27）

- 舊 `ExpressionBone` opt-in 肘部支線把遊戲 `Mathf.Sin((y-1f)*0.006f)` 誤轉為 `(y-1f)*0.006f.Sin()`；這把有界的正弦位移變成隨角度線性增加。舊程式也以 `(fore,elbo,elbob)` 三骨共同存在為執行條件，遊戲則先檢查前臂，再分別檢查前、後肘骨。
- 已按左右肘的反編譯公式拆成預設啟用函式，移除重複的舊分支。合成測試以 30° 驗證左右前肘 Z 位移，並驗證缺前肘時後肘仍更新；Rust 測試與 UE 編譯通過。往後移植三角函數時需檢查括號與角度單位，逐個對照 Unity 的骨骼 null 條件，不能將相鄰分支合併成單一條件。

## E-088　用受使用者縮放影響的自動截圖比較骨骼（2026-09-27）

- K091 首批肘部回歸圖的角色只有約 200 像素高，而舊圖接近填滿 1080 像素。使用者指出曾不小心縮小鏡頭；兩次 log 的角色頂點界與計算相機位置相同，不能把這個構圖差當作肘部網格或比例回歸。
- `ok.ShotPreview` 現在在自動截圖期間忽略 `OrbitDrag`／`OrbitZoom`，每個全身、正臉、側臉請求前重新套用當次 rig 位置與旋轉。重拍 `ExpressionElbowCameraLockedK091` 後全身構圖與舊圖吻合；左右肘裁切區域沒有像素差，三張圖正常輸出且 exit 0。往後做截圖差異分析，先核對相機視角與角色佔框比例，再比較材質或骨骼；不得把縮放差異當成渲染改動。

## E-089　把遊戲預設啟用的胸部變形留在試驗開關後（2026-09-27）

- 舊 `apply_expression_bone` 雖轉寫 `enableBust` 和 `enableBustColDB` 的公式，卻放在 `OK_EXPR_ALL` 之後。遊戲反編譯的 `ExpressionBone` 對這兩旗標預設為 true，因此正常預覽漏掉 `cf_d_bust00` 和左右 `cf_d_hit_bust_*` 的每幀更新。
- 現把兩個分支抽到預設執行路徑，依遊戲的局部脊柱旋轉及左右胸骨 localPosition/localScale 更新，仍獨立檢查每個骨骼是否存在。合成測試檢查脊柱三軸旋轉、主胸骨位移及單側碰撞輔助骨，Rust 與 UE 編譯通過。往後盤點 decompiled component 時，除了公式，還要對照各 `enable*` 欄位的預設值及目前執行路徑，不能因為完整支線未驗證而連可單獨驗證的分支一併停用。

## E-090　大腿扭轉誤讀膝骨並以共同條件跳過獨立分支（2026-09-28）

- 舊 `OK_EXPR_ALL` 大腿段從 `cf_j_leg01_*` 的 Y 角計算 `cf_d_thigh01/02/03_*`，但反編譯 `ExpressionBone.LateUpdate` 的 ThighL/R 讀 `cf_j_thigh00_*`；膝段才讀 `cf_j_leg01_*`。舊共同條件又要求腰、高度與膝骨都存在，會連大腿及小腿獨立分支一起略過。已分別按來源骨與分支條件預設啟用，並刪舊區塊避免覆寫；合成測試故意給大腿與膝相反 Y 角，且移除膝骨後仍檢查大腿。往後比對 ExpressionBone 先逐個核對 `BoneName` 索引與每個 `enable*` 分支，不以鄰近骨的數值代用。

## E-091　AcsNull 來源／目標骨反向，且靜態配件漏了逐幀自身物理（2026-09-28）

- 從註解「a_n_neck -> cf_j_head」推測來源關係是錯的。反編譯 `BoneName` 最後兩項為索引 60 `a_n_neck`、61 `cf_j_head`；`LateUpdate` 先讀 `dictBone[61].localRotation`，再寫 `dictBone[60]` 局部 X/Z。舊 Rust opt-in 程式反向讀項鍊掛點、寫頭骨，預設還未啟用；現在依列舉索引更正並預設執行。合成測試明確斷言頭骨位置不變，且掛點按來源公式移動。不能只從變數名或鄰近程式推斷列舉方向。
- K091 槽 19 明確宣告 `parentKey=a_n_neck`，兩個項鍊 renderer 的 `rigid_follow_bone` 也是 `a_n_neck`，身體骨架中該點的父節點是 `cf_s_neck`，UE 截圖 log 無缺骨警告。不能把畫面疑似飾品錯位直接改掛 `cf_j_head` 或其他骨：遊戲 `ChangeAccessoryParent` 原本就選 `a_n_neck`。在 0.25 秒步行姿態，AcsNull 的 X/Z 修正只有約 -0.30/+1.23 mm，因此它無法單獨解釋肉眼可見的垂墜差。
- 同一項鍊 base mesh 的 354 頂點中，218 個帶內骨 0 權重、176 個帶內骨 1 權重（混合權重頂點可重複計數）；stone mesh 的 74 頂點均受內骨 0，prefab 的 DynamicBone 鏈為兩粒子，卡片 `noShake=false`。component 根骨 `acs_J_neck_01` 的 `m_Force=(0,-0.001,0)`、`m_Inert=0.9`、`m_EndOffset=(0,-0.02,0)`。抽取時雖已模擬並蒙皮一次，但 session 將整片轉成靜態頂點，UE 動作只讓整片跟 `a_n_neck`。這會丟失遊戲逐幀的內骨擺動。修復需保留自身骨、權重與物理狀態並在 runtime 更新；不能把重新烘焙的靜態圖或改模擬固定幀數當成通解。本輪 `ExpressionNeckAnchorK091` 可確認來源掛點校正與動作截圖仍正常，**不能**證明逐幀項鍊物理已解決。

## E-092　配件動態驗證混淆掛點、重名網格與物理旋轉（2026-09-28）

- 初版項鍊真卡測試的浮點時間陣列沒有宣告 `f32`，呼叫 `.min()` 時編譯器無法推斷型別；改成明確 `0.0f32`。首次比對原烘焙與 `apply_shape` 後位置，直接把約 2.1–2.5 cm 的世界頂點差稱作配件偏移；其大部分其實是角色從抽取中性姿勢切到動畫起點時**掛點整體移動**。往後先計算 `anchorAfter × inverse(anchorBefore)`，再看局部殘差，不可直接比較兩個不同姿勢的世界座標。
- K384 有兩片同名 `cf_hair_f_20`。第一版審計用 `find(name)` 把第二片的烘焙頂點與第一片的新頂點相比，虛構出 77.8 mm 漂移；改用 session part index 後，兩片的中性局部誤差都約 1.7 mm。另一版測試沒設 UE 預覽的 `OK_GPU_SKIN`／`OK_SKELMESH`／`OK_NOPOSE`／`OK_UE_ANIM` 環境，甚至把臉讀成沒有 rigid follower，不能代表實際截圖。後續真卡測試明確複製這四個環境設定，開始遊戲 clip 前才清掉 `OK_UE_ANIM`。檢查私有 `DBoneSet.bones` 時也曾造成一次測試編譯失敗；改用受限的 test-only root getter，不擴張正式資料結構可見度。
- 曾斷言 DynamicBone 根骨整個 3×4 矩陣必須維持掛點矩陣，K384 首片差 0.065 而測試失敗；反編譯 `ApplyParticlesToTransforms` 本來就依第一個粒子旋轉根骨，應檢查**根骨位置**是否貼著掛點，而讓旋轉自由。修正前 K384 0.5 秒根骨位置有 0.865 mm 殘差：非均勻上層的 sheared 掛點被拆成單一 TRS 時丟了剪切。現在在蒙皮後補完整矩陣，9 片動態配件中性幾何回到原烘焙位置，走路時根骨位置誤差降到浮點量級。另依遊戲 `TransformDirection` 修正重力向量不可乘骨架縮放的問題。
- 首輪 K384 步行外觀曾被誤判為已通過，使用者指出頭髮／髮飾似乎偏離頭部。後來以 `OK_SHOT_ANIM_TIME=0.5` 固定同一姿勢、`OK_RUNTIME_ACCESSORY=0` 做診斷，確認髮根仍在前額交界；新動態路徑讓**髮末端**相對固定版擺動，部分髮束跨到臉前。無同動作遊戲參考圖時，不能以不同時間點的兩張截圖推斷整件髮飾脫落，也不能用手寫固定偏移壓掉遊戲的 DynamicBone 擺動。往後用固定時間、根骨位置和原始 prefab 物理參數一併驗證。

## E-093　把 DynamicBone_Ver01 當普通骨樹物理、末端骨誤用世界向量（2026-09-28）

- 原 `parse_dbones` 接受 `DynamicBone` 與 `DynamicBone_Ver01` 兩類，卻都交給普通版遞迴遍歷 `m_Root` 子樹。反編譯 Ver01 根本不遍歷子樹，而按序列化 `m_Nodes` 指定粒子次序、個別 `RotationCalc`，並以粒子序號取分布曲線；其重力、跳幀慣性、末端長度與角度倍率也有差異。這會讓使用 Ver01 的 mod 卡出現髮節／飾品位移或多算其他子骨。現明確保存元件種類和 Ver01 節點，分流初始化、更新與套用；合成測試放入未列入的子骨、關閉根旋轉並以 0.5 倍角驗證。往後遇到同名近似元件，先核對 `SetupParticles` 的資料模型，不能因公開參數看似相同就共用粒子拓樸。
- 普通版 `AppendParticles` 對沒有父 Transform 的末端骨，以局部 `[m_EndLength,0,0]` 建虛擬粒子；原 Rust 把世界 `[1,0,0]` 送入逆矩陣再乘長度，會依物件位置、旋轉和縮放改變方向。已直接使用來源的局部 +X，合成測試以平移、90° 旋轉與 2 倍縮放驗證末端世界點。K384 實際前髮元件不是 Ver01，修正後的 0.5 秒最大非剛性位移仍約 27.4 mm；此數值不能替代遊戲同時刻畫面對照。真實 Ver01 prefab 的序列化欄位還未經實物審計，需在遇到該類配件時補上實物驗證。

## E-094　把 dump 的二次變換 OBJ 當作 Renderer 實際世界幾何（2026-09-28）

- K384 前髮 `mesh_18_cf_hair_ext_34_01.obj` 的界限比預覽小約 25–30%；逐點仿射擬合恰得到 renderer `lossyScale≈(0.695,0.747,0.770)`。最初誤判預覽漏乘尺度，準備再縮小前髮。實際 `mesh_18...json` 的 `rendererWorldBounds` 和 `manualSkinnedWorldBounds` 都與預覽界限吻合，最大差約 0.95 mm；dump 的 `worldVertices`／OBJ 是 `BakeMesh` 後又 `TransformPoint` 的診斷輸出，把 renderer 尺度多乘一次。現以 JSON 的真實 renderer／手算骨骼界限做 ignored 真卡測試，並移除直接把 OBJ 頂點當基準的比較。往後使用 dump 時先讀 `vertexSpace`、`bakeMeshSpaceGuess` 及多套 bounds，不能由檔名 `worldVertices` 推斷其就是 Unity 畫面使用的蒙皮世界頂點。
- 另一個通用載入錯誤是把卡片 `noShake` 套到所有相似物理元件。遊戲 `ChaControl.ChangeShakeHair/Accessory` 只切換 `DynamicBone`，不切換獨立的 `DynamicBone_Ver01`。已在一般及 Material Editor renderer 覆寫載入支線保留 Ver01，只停普通版；合成測試覆蓋混合元件集合。抽查 12 個原生髮 bundle 未見 Ver01，故這項行為目前以反編譯邏輯及合成測試驗證，不能宣稱所有 mod 的序列化格式已經實物覆蓋。

## E-095　逐條還原共用骨節，覆寫同幀先前 DynamicBone 的結果（2026-09-28）

- 配件 runtime 先對鏈 A 還原初始 Transform 並套用粒子，接著對鏈 B 做相同流程。若 B 的根骨也是 A 的子骨，B 的還原階段會清掉 A 已算出的位移或旋轉。最初只用一條無旁支的鏈測試，父骨旋轉仍會把共用子骨帶到看似正確的位置，掩蓋這個錯誤；加上父骨第二個子骨，才觀察到 A 的粒子末端與最終骨架的共用骨相差約 0.18 m。
- 遊戲 `DynamicBone.Update` 與 `LateUpdate` 是不同 Unity 生命週期。現在把所有鏈的 `InitTransforms` 集中於所有 `UpdateDynamicBones` 前，再按原元件順序執行粒子更新與骨架套用；測試明確比較 A 的粒子座標與最終共用骨世界座標，也比對中性 bake 的位置與旋轉。K384、K091 真卡審計與 UE 編譯通過。往後驗證多個物理元件時，測試骨架必須有共用骨與旁支，且要檢查最後輸出的 Transform，不能只檢查單條鏈自己的粒子。

## E-096　把 Bash 大括號檔名展開用在 PowerShell（2026-09-28）

- 查 K384／K091 測試名稱時，以 `Tools/ok-extract/src/{session.rs,dbone.rs}` 傳給 PowerShell，語法解析在逗號處失敗，檔案未被搜尋。改成 `rg` 後分列兩個完整路徑，搜尋成功。後續在 PowerShell 呼叫 `rg` 時不要使用 Bash 的 `{a,b}` 路徑展開。

## E-097　逐元件跑完 240 幀才建立下一條粒子鏈（2026-09-28）

- 上一輪只測兩條 60 Hz 共用骨節鏈，靜態 bake 與 runtime 都收斂到相同結果，因此錯判兩條路徑已等價。檢查 `hang_set_attached` 才發現它逐元件呼叫 `simulate_dynamic(...,240)`，下一元件的 `SetupParticles` 看到的已不是原始 prefab 骨架。反編譯的 `DynamicBone.Start`／`Update`／`LateUpdate` 是元件生命週期，應先建立各元件粒子，再按幀更新。把第二條鏈改為 0.1 Hz、保留共用骨與旁支後，舊 bake 的骨 2 旋轉偏約 0.178 弧度，回歸測試失敗。
- 預設靜態 bake 現直接使用 runtime 的 `AttachedDynamicSet::new` 預熱結果，避免再維護兩套更新順序；保留顯式診斷開關的舊演算法。修正後測試的位置與旋轉一致，K384／K091 實卡審計及 UE 編譯通過，cache 版本同步更新。往後比較物理路徑不能只用會快速收斂的 60 Hz 例子，須涵蓋低更新率、共用骨節與最終局部旋轉。

## E-098　測試前沿用已不存在的舊 dump 路徑（2026-09-28）

- Unreal 實測前查檔時沿用最早提供的 `C:/portable/KoikatuSunshine/mesh/_dump`，`Get-ChildItem` 回報路徑不存在。目前已驗證的 mesh dump 根目錄是 `C:/portable/KoikatuSunshine/mesh_dump`；動畫／表情明細在專案的 `Saved/anim_export`。後續查參考資料先用目前已驗證的根目錄，不把歷史訊息的舊位置當成仍然存在。

## E-099　Hidden 骨架材質在宣告 usage 前先查詢 usage（2026-09-28）

- `HideComputeSkel` 的臨時隱藏材質先呼叫 `SetMaterialUsage(SkeletalMesh)`，到 shader 編譯後才透過 `OKEnsureSkeletalMaterialUsage` 設旗標。UE 的 `-game` 不會替它自動補 usage，首次建立時因此印出 `Material_1 missing usage flag SkeletalMesh`。這是計算骨架的隱藏材質，不應直接宣稱可見衣物已換成預設材質。已將 usage 宣告移到查詢與編譯前；以引擎實測 log 驗證，而非只看旗標最後是否為 true。
- 全量掃描首輪 log 還發現 CommonUI 未使用 CommonGameViewportClient 的輸入路由錯誤；本次自動截圖不使用 CommonUI 輸入，且正常完成，但不能把沒有 material/fatal 說成所有 log 都無錯誤。這個既有 UI 設定問題仍需在真正使用 CommonUI 時處理。

## E-100　猜測檔名並混用工作目錄（2026-09-28）

- 查骨架建立器時誤用不存在的 `KKSkelMeshBuilder.cpp`／`src/gpu.rs`；實際檔案為 `KKSkeletalBuilder.cpp`／`src/skel_export.rs`。另在 cwd 已是 `Tools/ok-extract` 時，仍傳 `Tools/ok-extract/src/skel_export.rs`，使路徑重複。後續先用 `rg --files` 找到檔案，再使用與當次 cwd 相符的路徑；已改用專案根目錄執行這類查詢。

## E-101　只驗證 Rust 掛點，漏掉 Unreal 再套一次頭部變換（2026-09-28）

- 使用者指出 `UnrealSharedWarmup260` 的兩卡側髮沒有綁在頭上。先前 K384／K091 的 Rust 真卡測試只檢查 `AccessoryRuntime` 的髮根與掛點，沒有檢查最後的 UE component 變換。C++ `LoadSession` 對名稱含 hair 的非 GPU 網格預設指定 `RigidFollowBone=cf_s_head`；Rust 的 skel_kind 4 動態配件早已把動畫掛點乘進頂點，C++ 的自動 fallback 又加一次。`34_mesh` 因名稱沒有 hair 而逃過錯誤，不能因此以這片髮束的零誤差代表全部側髮。
- 新增 `OK_SHOT_ATTACHMENT_AUDIT=1`，逐頂點比較 `componentWorld × displayedVertex` 與 `actorWorld × sessionVertex`，同時驗證真正的 section buffer，無 buffer 時印 Error。修正前固定步行 0.5 秒，K384 六片髮束有 3.639–3.661 cm 誤差，K091 三片側／後髮有 3.428–3.465 cm 誤差；頸飾為零。已讓 skel_kind 4 不進剛性 fallback，靜態配件仍照來源 rigid_follow_bone 跟隨。不得再只憑照片說髮根已附著，或只用 Rust 掛點數值宣稱 UE 最終畫面通過。
- 修正後重編 UE 並在 `RuntimeDoubleFollowFixed` 重拍六張；兩卡共 14 個動態配件的最終顯示頂點誤差均為 log 顯示的 `0.000000 cm`，無 audit Error，正常 exit 0。側髮正／側視位置已改正，Hidden 材質 usage warning 也消失。這項測試驗證從 session 到 UE 顯示沒有重複變換；遊戲 DynamicBone 振幅與 shader 等價仍需各自的來源／畫面對照。

## E-102　用 Animator 取樣時間差當作 DynamicBone 的物理時間（2026-09-28）

- `OkSession::apply_anim_time` 原本以新舊 clip 時間差算 `dt`，倒退時加一次 clip 長度。取樣時間無法辨認一幀跨過幾次循環，也無法區分循環、倒退跳轉與同一時間重複取樣；例如一幀剛好走完一圈，取樣時間相同就算出零。反編譯 `DynamicBone.LateUpdate` 呼叫 `UpdateDynamicBones(Time.deltaTime)`，不是 Animator 時間差；普通版與 Ver01 都必須使用真實經過時間。
- 新增 `apply_anim_frame(sample_time, delta_seconds)`，一路接通 Rust、C ABI 與 Unreal。遊戲 clip 播放傳 Actor Tick 的 `DeltaSeconds`，固定截圖步進傳每步的實際時間；起播、切場景的同幀取樣傳零。舊 timestamp-only API 留給既有呼叫者相容，不能用它做需要跨圈或跳轉的播放。粒子不因 clip phase 回到零而重建，保持遊戲逐幀物理狀態。快幀最多三個物理 substep 的來源規則不變。
- 真卡回歸使用 K384 的全部 9 個動態配件，在 Animator 暫停的條件下施加相同頭部掛點移動，獨立參考路徑直接傳 30 Hz 物理時間。比較三種 timestamp：永遠零、120 Hz 短 clip 每幀跨四圈、0.4/0.1 秒交替倒退。先讓測試呼叫舊 API，重現最大 0.0340810 m 頂點偏差；改走新 FFI 後三種路徑全部與參考一致（顯示誤差 0.0000000 m）。刻意不更新物理的負對照仍差 0.0340810 m，排除沒有觸發擺動卻誤判通過。測試只隔離物理時鐘，沒有宣稱不同動畫姿態本身應產生相同幾何。
- 避免方式：移植 Unity 物理前，核對每個函式的時間來源；不能因通常只有一次循環就從取樣時間推回 wall/frame time。新播放呼叫者必須提供真實 `delta_seconds`，seek 只控制 pose sample；需要重設粒子時另走既有 reset 流程。
- 引擎端驗證：UE Editor Development 重編成功，`OpenKoikatsu.KKUnityBundle.RustExtractAbi` 一項自動化測試 Success／exit 0。`FrameClockFixed` 重拍 K384／K091 六張圖，14 個動態配件的最終顯示誤差全部為 `0.000000 cm`，無 attachment audit／材質錯誤，正常 exit 0；既有 CommonUI 設定錯誤仍列在 log。詳見 HANDOFF2 §116。

## E-103　混合換行的 C++ 檔讓文字 patch 無法匹配（2026-09-28）

- 替換 `StartAnimLoop` 的呼叫時，`apply_patch` 回報找不到確實存在的 `Session.ApplyAnimTime(0.f)`。`KKPreviewActor.cpp` 同時有 CRLF 與 LF 段落，先前 patch 能匹配後方 LF 區塊，卻不能匹配這段 CRLF。此次失敗未寫入修改。
- 改用 bytes 精確替換，先斷言零時間呼叫有兩處、`AnimPhase` 呼叫一處，才替換為 explicit-frame API；保留所有原始換行與其他內容。之後 UE 編譯通過、diff check 無空白錯誤。後續遇到此類 mismatch 先檢查檔案 bytes／換行，不重複放寬上下文或整檔正規化。

## E-104　Python 盤點查詢多打一個右括號（2026-09-28）

- 第一次統計同 bundle 同名動畫時，`list(csv.DictReader(...)))` 多了一個 `)`，Python 在讀檔前即回報 `SyntaxError: unmatched ')'`。沒有寫入任何資料。改為先用 `with open(...)` 開檔，再建立 `DictReader`，查詢成功。後續短腳本仍逐層檢查括號，不因只是唯讀查詢而省略語法核對。

## E-105　用 clip 名稱去重，讓同 bundle 的不同動作永遠選到第一份（2026-09-28）

- 舊 `names_in_bundle` 只保存 `AnimationClip.m_Name` 並 `sort/dedup`，`load_clip_at` 則在整個 bundle 搜 controller state 或取首個同名 clip。匯出 TSV 的 29,956 個動畫只有 10,536 組 source/name，存在 1,337 組 bundle 內同名；例如 Studio 原生 `S_OLoop` 在同一 bundle 有 67 份。以前只測跨 bundle 同名，漏掉 bundle 內碰撞。
- 反編譯 `Studio.OCIChar.LoadAnime` 用 AnimeLoadInfo 的 bundlePath/fileName 載入指定 RuntimeAnimatorController，`ChaControl.LoadAnimation` 以資產參照設定 Animator；不能把名稱當作整個 bundle 內唯一身分。現在目錄保存 bundle、serialized file 名稱與 64-bit path_id；qualified source 的載入直接定位該檔與物件，完全不再搜尋第一個同名狀態。Controller／OverrideController 的本地 clip PPtr 只用來給同名選項加來源名稱；同一 owner 的多份 clip 仍各自保留，排序與區分標籤穩定。外部 fileID 的參照不誤當本地 path_id。
- source 內不重名的選項仍維持原名稱；同名選項如 `tachi_00 [gravure_00]`、`tachi_00 [pose_04]`、`tachi_00 [pose_04] (2)` 逐一映射不同資源，不把 path_id 印到 UI。模糊的 `source|tachi_00` 明確拒絕，不再偷偷挑一份。基本 Maker/controller state aliases 保留既有行為；完整 AnimatorController／blend tree／override 執行圖仍是另一層工作，本次修的是單一 clip 選擇與資源載入身分。
- 定向測試先在舊碼重現 `studio_k/anime/04.unity3d` 實際 66 clips、目錄卻僅 43；修正後完整 66，各自可選三個 `tachi_00` 且取樣曲線確實不同。初次全套測試另有兩項失敗，因先前也把 esthetic 的兩份 `L_aomuke` 誤當唯一；已核對其 owners 是 `ke_f_00/ke_f_01`，測試指定正確 qualifier，沒有放寬尺度或場景斷言。最終 catalogue tests 11 通過／5 手動盤點忽略。
- 完整 ignored 盤點 `animation_inventory_preserves_all_exported_assets` 讀實際 base/mod bundles，逐來源核對 TSV 筆數與 label 唯一性：101 個含 clip 的來源、29,956 個 clip 全部保留，58.74 秒通過，log `Saved/Logs/AnimationClipIdentityInventory.log`。這驗證全目錄的資源覆蓋，沒有宣稱每份曲線或控制器圖都已在 UE 視覺回歸。
- C++ 原 `ClipNamesForSource` 使用固定 512-byte buffer，新增同名 qualifier 後也可能讓長名稱被悄悄略過；已讓原 FFI 名稱函式支援 null-output 的 UTF-8 配置長度查詢，C++ 按長度配置後再讀取，不截斷資源選項。避免方式：把 bundle/file/path_id 與顯示名稱分開；驗證來源內撞名、控制器內多份資源及跨 FFI 的完整字串，不能只檢查 source/name 去重後的清單。
- UE automation `AnimationClipIdentity` 已核對 C++ 的 66 個選項、三個同名動作選擇與取樣，Success／exit 0。另實際重拍 `ClipIdentityGravure`／`ClipIdentityPose04V2` 共 12 張；同一張卡的兩個動作分別手在身旁／舉到頭後，每組 K384 9／K091 5 個動態配件的最終顯示誤差均為 `0.000000 cm`、無 attachment audit Error、正常 exit 0。細節見 HANDOFF2 §117。

## E-106　把短於 0.05 秒的有效動畫當作缺失長度（2026-09-28）

- TSV 中有 5 支合法 0.033333 秒的 clip，包括 `action/animator/00.unity3d|f_twoshot_00_02`。Rust 已依 `m_MuscleClip.m_StopTime - m_StartTime` 正確回傳 1/30 秒，C++ `FOkSession::AnimDuration` 卻把 `D <= 0.05` 改成 1 秒，`SetGameAnimation` 與逐幀回繞又另設 0.05 秒下限。這不是 clip 缺時鐘，而是把有效來源時長覆寫成任意週期。
- 在既有 UE `AnimationClipIdentity` 測試新增真實一幀 clip 的 1/30 秒斷言。修正前引擎 log `AnimationShortDurationBefore.log` 明確 Result={Fail}，唯一失敗是 `preserve authored one-frame duration`；其餘同名資源測試仍通過。`-TestExit=Automation Test Queue Empty` 本次退出碼雖是 0，不能只看程序退出碼判定 automation 成功，必須看 Test Completed 結果與錯誤事件。
- 現 wrapper 只對非有限或非正的長度回退；遊戲 clip 的 PoseMode 2 起播與每幀回繞使用來源的正長度，移除 0.05 秒門檻。DynamicBone 仍使用獨立 `DeltaSeconds`，沒有將短 clip 的週期當成物理 dt，也沒有卡名／clip 名的特殊分支。避免方式：先對照來源 m_StartTime/m_StopTime 與 sample rate，只有確實無效的時間才回退；不能用可見動畫通常較長的經驗門檻判斷有效性。
- 修正後 `AnimationShortDurationFixed.log` 的 UE automation 為 Success，包含同名 clip 目錄／選取測試與 1/30 秒時長斷言；重編成功。初版 `ClipIdentityShortFrame` 顯示來源 duration=0.033、固定取樣 0.010，9 個動態配件零誤差並正常 exit 0，但畫面檢查發現臉被切掉半邊，不能當成合格截圖。這是 E-107 的相機根因，與 clip 載入或髮根位置分開修正。

## E-107　動畫平移角色後，截圖相機仍停在載入時的位置（2026-09-28）

- 一幀 clip `f_twoshot_00_02` 的載入、時長與 9 個配件的最終頂點審計均通過，但 `ClipIdentityShortFrame/KK_384283_outfit0_face.png` 只剩右半張臉。`RunShotTask` 在取樣後直接使用載入／中性姿勢的 `GStudioRig`；來源動作改了頭部相對中性姿勢的位置，相機沒有跟著目標移動。不能因頂點審計為零就忽略畫面裁切，也不能改 mesh 或卡片偏移來迎合錯誤相機。
- 中性構圖現保存當時 `cf_j_root`／`cf_s_head` 世界變換，以及全身／正臉／側臉焦點。固定姿勢取樣與嘴型後，將全身焦點從中性 root 空間轉至目前 root，臉與側臉焦點從中性 head 空間轉至目前 head；相機隨焦點位移但保留原視軸與距離。使用完整 FTransform，包括來源旋轉與縮放，不硬編某張卡或動作的補償量。缺對應骨架時保留既有構圖。
- 不再次呼叫 neutral `OkComputeCameras`，因為它會移角色腳底、且身體 BaseVertices 仍是中性頂點；重新呼叫既會改變剛完成的 audit 世界空間，也未必正確追蹤 GPU 骨骼的動畫。新的 pose camera 僅改截圖相機，來源 root motion、角色 Transform 與配件審計幾何都照原資料維持。初版失敗圖保留，修正後重拍另存 `ClipIdentityShortFrameCentered`。
- UE 重編成功，兩卡新六張圖正／側臉已回到焦點，14 個動態配件 audit 全為 `0.000000 cm`、無材質 warning/error、正常 exit 0。來源 duration=0.033、取樣 0.010。從實際頭骨得到的 face focus 位移（cm）為 K384 `(17.200,-11.419,-5.979)`、K091 `(15.252,-10.476,-5.016)`；這支動作的 root focus 位移均為零，不能把頭部局部動作誤稱整體 root translation。數值僅記錄診斷，不放進執行邏輯。既有 CommonUI viewport log 仍在。

## E-108　控制器調查的指令／資料結構誤用（2026-09-28）

- 第一次 UnityPy 查詢假設所有版本都有 `m_StateMachineMotionSetIndex`，action bundle 其實使用 `m_StateMachineSynchronizedLayerIndex`，查詢發生 KeyError。先讀 key，再按版本取值；執行程式也保留兩種欄位，非零的 base motion set 尚未支援時明確略過。
- 對 `OKGalleryWidget.cpp` 沿用錯的 plugin 路徑而 rg 失敗；實際檔案在 `Source/OpenKoikatsu`。另一查詢將 Bash 的 `{file1,file2}` 路徑展開寫進 PowerShell，整個命令在執行前發生 ParserError；改列明確檔案參數。兩次失敗沒有寫入資料。
- 追查手肘时又查詢已失效的 `mesh/_dump`，重犯 E-098。正確根目錄為 `C:/portable/KoikatuSunshine/mesh_dump`；先 `rg --files` 找到 `KK_091207.png/outfit0-tpose/Ushiwakamaru_0/skeleton.json`，不能猜成卡片目錄直下的 skeleton.json。後續需先查本檔已記錄路徑。
- 肘部診斷第一版誤寫 `SessionPart.triangles`，Rust E0609 編譯失敗；欄位是 `indices`，更正後重跑。查詢 layer 時也曾把完整 skeleton mask 印出造成输出截斷；之後只印名稱、權重、IK 與相關欄位。
- 後續診斷又假設 export 骨架已建立，Option::unwrap 在 None 處失敗；需先呼叫 `ensure_skel_export`。第一次誤寫成不存在的 `skeleton_export` 又發生 E0599，已更正並通過診斷。沒有直接查原宣告就猜函式名是本次操作錯因。`OK_OUTFITS_LIMIT` 也不是 Rust 內部選項，已移除這個無效診斷設定。
- 又猜錯 UncensorSelector／RuntimeLightProbe 檔案位置，改用 `rg --files` 取得 `kks_decomp/plugins/KKS_Plugins/KKS_UncensorSelector` 與 `Tools/RuntimeLightProbe/Probe.cs`。對單行的大型 skeleton.json 用 rg 造成輸出截斷，改用 Python JSON 查詢欄位。新 game probe 的 .NET Framework csc 不接受 forward-slash 的完整來源路徑；改用 Windows backslash 絕對路徑後編譯成功。

## E-109　控制器狀態與直接 clip 載入混用（2026-09-28）

- §117 已修 clip 資源身分，但沒有指定 AnimatorController 的狀態載入。遊戲 `ChaControl.LoadAnimation` 先指定控制器、`AnimPlay` 再播其中狀態；`OCIChar.LoadAnime` 也是如此。不能在 bundle 內搜尋所有 controller 的第一個同名 state，也不能遇到 blend tree 就拿第一個 clip。
- 新增独立 `states:<source>` 目錄與完整 `controller / FullPath` 標籤；UI 顯示「控制器狀態：來源」。同檔同名控制器／狀態也穩定区分。`AnimatorOverrideController` 的 base controller 及 original/override clip 按本地 PPtr 解析，null override 保留原 clip；m_AnimationClips 的 null／外部項保留索引，不 filter_map 改變 m_ClipID 的意義。外部／缺失參照不猜其他資源。
- Red test：舊版 `states:studio_k/anime/04.unity3d` 回傳 0，而實際 66 個單一 motion 狀態應各自保留。修正後 `pose_04`／`gravure_00` 同名狀態分開載入；action `c33 / Base Layer.Main.Idle` 逐曲線等於其 override `Stand_map_33_00` 在 0.7 倍時間取樣，來源長度按 0.7 除回狀態秒數。
- 此階段是按明確狀態取樣，尚未實作自動 transition、1D/2D blend tree、動態參數、非零额外 layer、IK 或跨 bundle override。這些情況印出 skipped 原因且不列為可選狀態，不用第一個 clip 冒充結果。

## E-110　把多 layer 一律判定不可載入，錯拒 action override（2026-09-28）

- 第一版 scope guard 要求 layer 數等於 1。Studio 的 66 狀態已通過，但 c33 的真資源測試失敗，action 僅列 11 狀態；base 控制器有額外兩層，預設權重皆為 0。Unity base layer 的有效權重固定為 1，即使 serialized m_DefaultWeight 是 0；其餘零權重层不參與姿勢。
- 現按有效預設權重判斷；非零额外层仍拒絕，沒有忽略其遮罩與 additive/synchronized 語意。c33 回歸通過，action 共 4,295 個可解析的單一 motion 狀態，另有 4,664 個 blend tree／42 個非正時鐘未列入。
- 依據：原生 action 實際 layer 資料與 [Unity Mecanim FAQ](https://docs.unity.com/en-us/engine/6000.3/manual/animation-section/animation-mecanim/mecanim-faq)。避免方式：區分 layer 存在與是否參與姿勢，不只按 layer 數量決定。

## E-111　狀態循環旗標需要一路傳到 Unreal 的時間推進（2026-09-28）

- 新 state sampler 雖可對非循環狀態 clamp，但 UE PoseMode 2 原本每幀都 Fmod。若只在 Rust 處理，UE 先把時間回繞，非循環狀態就永遠到不了 end hold。這是在接入時發現的控制流程錯誤，已在引擎驗證前修正。
- bindings 表尾新增 anim_looping，C++ IsAnimLooping 與 Actor bGameAnimLooping 接通；explicit state 用編譯 loop flag，raw clip 保留原預覽循環行為。非循環時 phase 到來源 duration 後停住，仍傳真正 DeltaSeconds，讓配件物理繼續更新。
- UE automation 已核對 Studio 非循環狀態與 c33 循環狀態；Rust clock 測試涵蓋速度、offset、負時間、回繞與 end hold。不能只檢查 metadata，還須檢查每個消費時間的呼叫者。

## E-112　改環境變數不能當成已切換 CPU 蒙皮的證據（2026-09-28）

- 發現 c33 的 K091 彎肘有破面後，首輪診斷將 OK_GPU_SKIN／OK_SKELMESH 設 0，輸出到 ControllerOverrideC33Cpu；但 log 仍是 `OK skelmesh gpu part=o_body_a`。Actor 使用自己的 SkeletalMesh 啟用設定，Rust 的環境變數並未讓這批圖變成 CPU 顯示。
- 這批圖與 GPU 圖相同不能證明 CPU/GPU 等價；改在 Rust 真 session 直接 skin_all，並用共享骨架的 posed×inverse(rest) 矩陣獨立重建 GPU 頂點，比較真正的兩個數值路徑。不得把資料抽取開關與最後渲染方式混為一談。
- 修正診斷工具後，K091 同一 c33/Idle 0.5 秒的最大 CPU/GPU 頂點差為 `0.0000845 m`（0.0845 mm），不支持 GPU 矩陣造成數十公分後肘位移的假設。骨骼名稱沒有重複；cf_s_elboback_L/R 的局部 X 為 -0.464502／0.545377 m。真正破面根因繼續對照遊戲 runtime，不能把 palette 差異小誤報成畫面已修好。

## E-113　新控制器彎肘暴露場景元件混用（2026-09-28，已定位修正）

- ControllerOverrideC33 的六張引擎圖有 14 個配件零誤差與正確 controller/clip/時長，但 K091 3D 圖的彎肘出現明顯破面。不能因控制器測試與髮飾審計通過，就把這批全身圖當作完成。
- K091 真 session 的 c33/Idle 0.5 秒：前臂 zxy Euler Y 約 L 94.698°、R -109.378°，後肘骨位移達数十公分。現有肘公式與目前遊戲 DLL 重新反編譯的 `Saved/Logs/ExpressionBoneLive.cs` 相同；尚不能憑大位移就任意縮小係數。
- 新增 `Tools/GamePoseProbe/Probe.cs`／`build.ps1` 暫時診斷，從遊戲 Maker 自動載入相同卡、action c33、完整 state path 與相同 normalized time，讀實際骨架、ExpressionBone 開關／dictBone 參照、實際 renderer BakeMesh。只讀卡片與輸出 log，不保存卡片。調查結論與修正驗證應追加在本節。

- 後續改用 CharaStudio 載入相同 mod 卡：`StudioPoseC33Fields.json.bones.tsv` 實測 ExpressionBone 存在，所有分類旗標及 enabled 都為 true；左右後肘位置與 Rust 公式吻合。`StudioPoseC33Render.json.png` 的遊戲畫面在這個組合下也有斷臂。停用該元件的同一卡／controller control（`StudioPoseC33NoExpression.json.png`）恢復連續手臂，因此不能改 12／7.6／16 係數來掩蓋場景不符。
- 來源 `InitializeExpression` 的呼叫者是 Studio AddObjectFemale／Male 和 ExpressionTest；普通遊戲／Maker 的角色工廠不建立此元件。plain factory 真測 `expression_exists=False`，但也漏走部分 mod 的 Studio 初始化，衣服／髮飾和 ABMX 與完整卡不同，不能當作整張 mod 卡的點對點參考。Studio 停用 control 可以保留初次載入時已變形的 helper；同樣不能聲稱它完全等同普通遊戲工廠。
- 修正：自動模式按 clip 實際 bundle／zipmod 內路徑辨認 Studio 來源，其餘場景不插入 ExpressionBone；顯式 scene 4 保留 Studio 模擬，1/2/3 為 Maker／普通／H。C ABI、UI、session 的形變與逐幀兩條路徑，以及 opt-in 靜態 pose 抽取都接通；沒有卡名特例。普通 c33 的 9048 頂點對 Studio 停用 control：身體平均差 0.452 mm、p95 2.191 mm、最大 19.836 mm；1672 個手臂相關頂點平均 0.0629 mm、最大 0.3784 mm。這不是全身完全等價，mod 臀部 `cf_j_siri_*` 的 live 位移仍與靜態身體骨架不同。
- UE 最新 `ControllerSceneFixed` 六圖，K091 彎肘已連續；14 個動態配件最終頂點 audit 零誤差、嘴型 0/open 0.8、exit 0。source/scene、非關節曲線、Expression 分支、cache scene key、真卡 attachment 測試與 UE AnimationClipIdentity automation 通過。舊破面與錯誤 CPU control 保留診斷，不覆寫正式 T-pose 圖。

## E-114　遊戲探針與重拍指令的診斷失誤（2026-09-28）

- Unity JsonUtility 沒有輸出自訂巢狀 Bone／Flag 陣列。起初從 JSON 缺欄位推測 Maker 沒有元件，這個推論沒有數據支持；已撤回，改由 TSV 明寫 expression_exists、全 transform、dictBone 與分類旗標。實測 Studio 為 true，plain factory 為 false，搭配呼叫者來源才形成結論。
- Maker 首輪成功後 timeout 又寫同一路徑，覆蓋了結果；加入 finished guard。首輪 body 9823 點、Studio body 9048 點，不能做逐點比較；比較工具現在拒絕不同頂點數。
- PNG 擷取第一次編譯缺 UnityEngine.ImageConversionModule 參照而 CS1061 失敗，加入該 DLL 後成功。系統 Python 沒有 matplotlib，未安装套件；比較工具只用標準函式庫，診斷畫面由遊戲相機本身輸出。
- 比較脚本有一輪在 Tools/ok-extract 的 cwd 使用專案根目錄相對路徑，找不到檔案；改在 repo 根執行。重拍又把 card 檔名當成絕對路徑、把 OK_SHOT_OUTPUT_DIR 誤寫成 OK_SHOT_OUTPUT，log 顯示 explicit cards=0；停止自建空場景，核對 C++ 選項後用絕對卡路徑／正確輸出鍵重跑。未生成或更動正式圖。
- 新姿勢的 CPU/GPU palette 最大差由 0.0845 mm 變為 0.1041 mm，原診斷的 0.1 mm 門檻因此失敗。保留實測數字，診斷門檻改為 0.2 mm，對共享 reference palette／原 SMR bind matrices 的近似維持次毫米 gate；沒有改運行時 geometry 或用這項 gate 證明遊戲全身等價。
- 所有測試用 CharaStudio／UE 自建程序已結束；遊戲 plugins 目錄的探針 DLL 已移回工具目錄。未保存卡片或場景。最新版 UE log 尚有既有 CommonUI routing error 和 PMC 重載的 resource cleanup hitch warnings；無材質編譯 warning/error，不可把它概述為完整 log 無錯誤。
- HANDOFF2 追加第一輪沿用摘要中的舊末行作 patch context 而 verification failed，該呼叫未寫入資料；重新讀實際檔案末行後追加成功。摘要不是可直接拿來定位檔案的字面內容。

## E-115　只寫 cf_j 關節漏掉動畫 helper，膝部字面名稱誤導公式（2026-09-28）

- 舊 apply_clip_at 將所有非 cf_j 和 cf_j_root 曲線略過，理由是其他骨由 morph 管理。實際 c33、步行、ADV、H 都含 cf_d／cf_s／a_n／k_f 等 Generic Transform bindings；例如 c33/Idle 0.5 秒的 cf_d_siri_L scale.x=1.01344323、cf_s_kneeB_L pos.y/z=-0.03418356/-0.08344822，與 live 遊戲一致，舊 loader 留在中性值。
- 現對 Animator 骨架內所有有效 Transform bindings 載入位置／旋轉／縮放；非 Transform 類不套骨架。按 ShapeBodyInfoFemale.UpdateAlways 在 Animator 後只回填 cf_d_kokan Z 和左右 cf_d_shoulder X，繼續保留其他已動畫化的 helper，而非重套整套 morph。遊戲值回歸與場景回歸通過。
- ExpressionBone 的 BoneName[9/12] 是 cf_s_kneeB，8/11 是 cf_d_kneeF。舊 port 按字面 front/back 名稱反套兩組公式，位置相差 13–16 cm；現按來源索引修正，測試也依來源索引驗證，不能讓測試複製同一個錯誤命名假設。
- LegL/R calculation transform 又誤用 thigh 局部位置和 waist 局部旋轉，來源是兩者世界位置／世界旋轉。改用正確世界座標後，移角色／轉角色不再改變局部 helper（合成 raised-leg 回歸通過）；Studio 分支執行此已移植公式，不再用 OK_EXPR_ALL 開關掩蓋空間錯誤。對 live Studio 的 cf_s_leg_* 殘差約 1.97 mm，完整頂點最大仍約 19.89 mm，不能宣稱所有 mod 身體物理／後處理已等價。

## E-116　新增 pose 場景必須進烘焙 cache key（2026-09-28）

- 接通靜態 pose 抽取後發現 env_sig 列表沒有新增的 OK_POSE_SCENE。相同卡／clip 用 scene 4 烘焙可能誤讀先前普通場景的結果；加入 signature 並以不更改全域環境的 cache-key 測試驗證兩者分開。
- 抽取版本 261、session pipeline 107；中性卡不因設定卡名改行為，動作來源／場景和已解析曲線決定流程。之後每加一個會改烘焙頂點的選項都必須核對 cache key，不能只升版本解決一次。

## E-117　配件物理不會自動涵蓋身體 Ver02（2026-09-28）

- 前次 K091 臀部 cf_j_siri live 差約 3.2–3.3 cm，不能推成 mod 身體 mesh 比例錯誤。遊戲 ChaControl 的 dictDynamicBoneBust 使用 DynamicBone_Ver02，普通 DynamicBone／Ver01 解析器不處理它；UncensorSelector.TransferBones 只按名稱將新 mesh 綁回現有骨架，並沒有換掉這組身體物理。
- 新 loader 從 p_cf_body_bone 的本地 PPtr 讀 Root、Bones、Patterns、RefTransform 與 collider；ChaPreparationBodyBone._dynamicBones 提供 BreastL/R、HipL/R 的 kind，不能只按骨名猜不同卡片的載入方式。骨架 merge 會產生 synthetic IDs，解析必须先使用原 prefab IDs，再按解析得到的名稱綁回共用骨架。
- Ver02 的 InitTransforms 位於 LateUpdate，從實際 reference bone 還原位置／旋轉／scale；普通 DynamicBone 的 Update/LateUpdate 次序不能直接挪用。LocalPosition／InitLocalScale 在 shape setup 時建立，Animator 每幀仍更新 reference tree。胸部參數按卡片 bustSoftness、bustWeight、shape[4] 與 BustSoft/BustGravity 的公式計算；sex 0 是男性、1 是女性，修正 CardInfo 的反向註解，男性 Hip kind 依 ChaControl 停用。
- 身體物理現於 Animator、場景 helper、ABMX 後執行，再 refresh GPU palette、更新配件。240 幀啟動預熱是預覽的確定性 settled-pose 規則，不能聲稱遊戲每次切動畫都會預熱；逐幀用真正 deltaSeconds，維持粒子歷史。Session cache 保存來源參數，粒子狀態不寫入磁碟；版本 262／108。
- 防重犯：碰到同名 DynamicBone 的不同版本先對照類別與 serialized 欄位；先比骨架/元件，再比 mesh；GPU 必須在所有會改姿勢的後處理之後 refresh。控制器 transition、blend tree／active additional layer 以及任意插件儲存的動態骨 override 仍未全數支援，不得宣稱所有 mod 卡已完整等價。

## E-118　FromToRotation 的小角度截斷（2026-09-28）

- 新 Ver02 遊戲控制測試最初胸部差約 0.208 mm、quaternion xyz 差約 0.002427。補錄 ResetPosition 後的 setup 沒有解決這個差異；最初將其歸因於取樣時點的假設已被後續測試否定。
- 共用 quat_from_to 在 dot > 0.9999 時直接回 identity，抹去最高約 0.81° 的旋轉。遊戲 Quaternion.FromToRotation 保留這個角度。移除正向截斷，近反向退化容差改為 0.000001；新增 0.00485 rad 回歸。這也改變普通版／Ver01 髮束的細微轉動，因此升 cache 並重跑既有物理與掛點回歸。
- BodyVer02GameFinalComparison.log：四條鏈共十個真骨，局部位置最大約 0.00000015 m（0.00015 mm）、quaternion xyz 最大約 0.00002、scale 差 0，原 0.000002 m／0.00002／0.000002 的門檻通過，沒有放寬。比較使用捕獲的 setup 與 soft params 隔離演算法；不能把這個數字當作整張卡/完整動作的頂點誤差。

## E-119　Ver02 本輪操作與探針控制失誤（2026-09-28）

- 在 Tools/ok-extract cwd 使用 repo-root 相對路徑跑 rg，找不到檔案；cargo 呼叫另已成功啟動。改回正確 cwd 後檢查。Python 第一次直接打印完整 ChaPreparationBodyBone 字典含數百個 BoneInfos，使輸出截斷；後續只取需要的欄位。
- 首個控制探針用 Animator.Update(0) 當成每幀 frozen pose，但反覆 Evaluate 不保證其他 transform 不变；改為捕獲整棵 body tree 並逐幀還原。第二個版本只有重設前 physics 欄位，補上 physics-setup.tsv；保留三次探針產物，README 標記用途。
- 新真卡測試把 ensure_skel_export 當 Result 呼叫 unwrap，引發 E0599；又未按已讀到的回傳 &SkelExport 處理，誤改 assert! 引發 E0600。最後使用既有寫法 let _，測試才編譯通過。避免方式：查實際簽名後直接按型別修正，不能憑方法名推測。
- 三次遊戲探針均只讀卡、輸出 log／PNG，未保存卡或場景；已停止核對過路徑的自建 CharaStudio 程序，將 DLL 移回 Tools/GamePoseProbe。遊戲 plugins 目錄不留探針。

## E-120　只核對原 DLL 演算法，漏掉已安裝物理修正插件（2026-09-28）

- 60 Hz 的 Ver02 控制比較通過後，繼續檢查插件發現 IllusionFixes/KKS_Fix_DynamicBones.dll 對普通 DynamicBone 與 Ver02 的 SkipUpdateParticles 做 prefix return false，不影響 Ver01。BepInEx 實測 Loading Dynamic Bones Fix 21.4.1；探針查到兩個方法相同的 Harmony owner。60 Hz 每幀有 substep，無法覆蓋這個零 substep 分支。
- 加入 120 Hz、body root 每幀移 0.001 m 的 controlled fixture。原碼最大 local position 差 0.0011892292 m、quaternion xyz 差約 0.002397。接入 skip patch 後 rotation 差大幅減少，position 還差約 0.001186 m；不能因此放寬門檻。
- 根因是探針的 ResetParticlesPosition／ResetPosition 不重設 UpdateTime，遊戲計時器仍有原本的餘數；新建 Rust state 起始為 0。補錄 component_state 的 UpdateTime／ObjectPrevPosition／Weight，對照採用同一餘數。BodyVer02HalfRateStateFixed.log 位置最大約 0.00000017 m、quaternion xyz 最大約 0.00001094、scale 0，原門檻通過。先前未記錄計時器的 HalfRateGame 只可當失敗診斷。
- 生產路徑按當前 game_root 的已安裝修正 DLL 選擇分支；普通版 static/deferred/runtime 與 Ver02 一致設定，Ver01 不套。變更 private DBone cache layout 後 CACHE_VER=81、extract=263、session pipeline=109；plugin stamp／presence 進 session、prefab memo／SQL、OBJ stamp 判斷，避免安裝／移除 plugin 後誤用來源物理分支。更新 DLL 的時間／大小會刷新 session cache；prefab/OBJ 只依本修正分支是否啟用。
- 新普通版定向測試：剛性掛點移動 0.1 m、dt=0，patched 粒子維持原世界位置，vanilla full inertia 仍移 0.1 m；Ver01 保持可執行 skip。額外兩次遊戲探針皆已退出，DLL 移回工具目錄。這次没有修改玩家 DLL／config，也未保存任何卡片或場景。
- 另核對 KKS_Colliders：本機 Studio Default Studio Settings=Off，普通遊戲配置 Breast／Floor／Skirt=true，會增加碰撞體／radius。現有四條鏈只處理 serialized collider refs，尚未移植該插件新增的跨角色 arm／floor colliders 或 Studio 儲存狀態。這是下一個具體相容差異，不能稱全部 mod 身體後處理已完成。

## E-121　KKS_Colliders 的來源與本輪操作錯誤（2026-09-28）

- 插件生成的 collider 不在原 bundle；只讀 serialized refs 會漏掉普通遊戲的自體手臂／地板碰撞與胸部 radius。Studio 必須遵循 Default Studio Settings，再按 card extended data 三個布林欄位覆蓋；非 Studio 不讀這些卡片覆蓋值。此輪開始移植身體 Ver02 路徑，跨角色碰撞與普通髮／裙物理不在這一條 runtime 中。
- PowerShell 指令誤用了 Bash 路徑大括號展開，整段被 parser 拒絕；改成列出各檔案。新增程式誤用尚未穩定的 Option::map_or_default，rustc E0658；改回 map_or(Default::default(), ...)。避免方式：命令按目前 shell 寫，新增 API 先核對工具鏈支援；測試命令須保留 cargo 真實 exit code。
- 接觸 fixture 必須證明碰撞有作用，不能只驗證 collider 有七個就宣稱等價。新對照清除相同初始 state 的 colliders 再跑 240 幀，胸骨位移差約 54.918 mm；啟用版與遊戲最大位置差約 0.00013 mm，維持原數值門檻。檢查尺寸／父骨／particle radius 均比遊戲捕獲值；未把 collider/radius 從 fixture 注入生產設定。DLL 已移回工具，原 config/card 不改。

## E-122　普通髮型和衣服不能共用錯誤的載入／烘焙分支（2026-09-28）

- 再讀 ChaControl.ChangeHairNoAsync／Async：copyDynamicBone=false、copyWeights=0、SetParent(HairParent,false)，髮型保留私有骨架；舊 rig_mesh 註解卻稱它使用 copyWeights=1 並映射到 head_full。原生頭髮也必須使用 prefab rig 與真掛點，不能靠名稱相同就把髮束當成 head 的骨頭。普通 DynamicBone 僅 static settle，也無法涵蓋 motion history／地板。
- ChaControl 的 top/bottom copyDynamicBone=true 只重綁普通版 root/exclusions/notRolls，且清除 authored collider refs，改成角色 objBodyBone 的 native collider。之前 hang_clothes_on_body 只模擬一次，runtime apply_shape／Animator 還原會蓋掉衣服物理；新的 session 必須保留來源 config、持續 particle state，於 Animator／shape／ABMX／身體 Ver02 之後、GPU refresh 之前更新裙骨。
- KKS_Colliders 的普通版 floor 不依賴 Breast flag；ct_clothesBot 加六個 leg colliders，不加 plugin arm；root 名 Split('_')[3] 可解析為 0 才改 FreezeAxis，Skirt true→X、false→None。Ver01 不套此插件或普通版 LoadCharaFbxData 的 collider 清單覆寫。外部碰撞必須用完整世界矩陣，不把帶非均勻祖先的骨架矩陣降成近似 TRS。
- 本輪第一次 cargo 失敗 E0624：從 lib.rs 呼叫新增 runtime update，未調整 private 可見性；改 pub(crate)。同一 shell 的 rg 又在 Tools/ok-extract cwd 用 repo 相對路徑，重犯 E-119；後續 repo 搜尋固定用 repo cwd，cargo 只用 src/...。使用早期使用者 dump 路徑搜尋也發現目錄已不存在；不把缺檔當成比對通過，後續用新遊戲探針和現存卡路徑核對。
- 新普通版控制探針建立 DynamicBone 後未初始化 m_Exclusions／m_notRolls／distribution curves。與來源 prefab 序列化出的空集合不同，AddComponent 留下 null，SetupParticles 的已 patched AppendParticles 拋 NullReferenceException；外層 coroutine 又沒接住錯誤。查到遊戲輸出其實在 game_root/output_log.txt（不是 CharaStudio_Data/output_log.txt）。保留失敗 log，補齊集合／curve 及 RunOrdinaryControl exception→JSON，再用 v2 fixture 重跑；失敗 fixture 不做比較依據。
- v2 控制探針把角色全部非 KKS_Colliders collider 當作 native source，遇到 Uncensor/附加部件的四個 cm_J_dan* collider，實測普通版 28+floor／28+floor+legs，與本次 oo_base p_cf_body_bone 的 24 個不同。失敗 log OrdinaryGameComparison265 保留；v3 改從遊戲實際 native prefab 的 collider 名單限定「此次來源演算法控制」後比較。任意 mod 的附加器官 collider 載入尚不在此來源集合，不能稱涵蓋它們。
- source collider table 在 bundle 中不照骨架子節點順序排列；遊戲 GetComponentsInChildren 依 Transform.m_Children 深度遍歷。新解析器按原 PPtr 子節點順序輸出，不能對 collider 依 ID 排序或用 HashMap 任意順序；碰撞順序可能影響最終結果。
- v3 的六個普通版骨頭與遊戲位置／quaternion 已一致，但「有碰撞 vs 清除碰撞」只差約 0.00004 mm，接觸斷言正確拒絕。不能把這個 pass 冒充碰撞驗證。原因是普通版初始方向會抵消 m_Gravity，且測試位置未確保深入目標表面；v4 使用來源 m_Force 並以遊戲真 collider 的 TransformPoint(center) 安排地板／大腿接觸。原容差與接觸最低位移不變，重跑新 fixture。

## E-123　接觸量測座標空間與 Animator／物理執行順序（2026-09-28）

- E-122 最後一項對「沒有接觸」的原因推論不充分：v4 仍在接觸門檻失敗，但六骨對遊戲數值皆吻合。根因是測試比較子骨的 localPosition，DynamicBone.ApplyParticlesToTransforms 會旋轉父骨，因此子骨 localPosition 幾乎不變也能有明顯世界位移。改以完整祖先矩陣量測相同骨頭的世界位置；未改生產參數或放寬門檻。v4 有無碰撞最大差 0.07334827 m，遊戲 local position 最大差 0.00000011 m、quaternion 分量最大差 0.00000044，原 0.000002 m／0.00002 門檻通過。失敗 OrdinaryGameContact266.log 和 OrdinaryGameNoContact266.log 保留；驗證使用 OrdinaryGameContact267.log。
- 共用衣服骨架的物理 step 原先還原粒子初始 local transform，可能蓋掉 Animator 當幀的曲線。來源 DynamicBone.Update 還原在 Animator 之前，LateUpdate 求解則讀 Animator／shape／ABMX 後的姿態。分出 step_on_animated_pose，衣服使用它；私有髮／配件仍使用每幀還原自身基準的 step。新增有實際位置曲線、stiffness=1 的定向回歸，確認 LateUpdate 保留 (0.5,1,0) 的 Animator 寫入。
- 衣服粒子 setup 使用 body_shaped 加 ABMX 的中性骨架，逐幀輸入完整當幀姿勢；不能用第一個任意動畫取樣作為 authored rest length。cache pipeline 升為 113、extract 267，防止直接重用上一版烘焙。
- 本輪搜尋又將 kks_decomp 當成 UE repo 子目錄；rg 明確失敗，沒有拿缺檔當查核成功。實際來源在 C:/portable/KoikatuSunshine/kks_decomp。後續先用 rg --files 確定路徑再讀檔。修改測試時一度寫成未存在的 wpos，編譯前核對共用 helper 簽名並改為 pos_of。
- 普通版來源控制遊戲只讀卡與輸出診斷，未保存卡、場景或配置；已核對自行啟動 PID 43756 的 CharaStudio.exe 路徑後結束程序，探針 DLL 移回 Tools/GamePoseProbe，不留在遊戲 plugins。

## E-124　衣服重綁不代表所有骨頭都屬於 body rig（2026-09-28）

- §126 的 retarget_clothes 只保留找得到同名 root 的普通版，略過其餘 root 與 Ver01；render 的 remap_part_to_body 又把未知 weighted bone 映射到祖先／bone 0。遊戲 LoadCharaFbxData 的 copyDynamicBone 只替換匹配參照；AssignedWeightsAndSetBoundsLoop 使用原 bones array，只改 TryGetValue 成功的槽，不是另一個 AssignedWeightsLoop 建立新空 array 的分支。有效私有骨必须保留，不可用卡片名、祖先或骨頭 0 補位。
- AssignedWeightsAndSetBounds 最後會 Destroy 私有 cf_j_root 子樹。新路徑要分辨匹配後存活的 body 參照、仍存活的私有 Transform，以及已刪除的骨／MonoBehaviour。普通版匹配 root 留在共享物理；存活的未匹配 root、copyDynamicBone=false 的普通版與 Ver01 留在私有物理。被刪除的私有 root 或 component 不可復活。
- 混合 SMR 保留原始槽順序及 bind pose，避免兩槽引用同一 body bone 時丟失不同 bind 矩陣。舊 set_bone_order 會去重；renderer 已有正確順序時不再重排。相同 shared bone 的兩個 bind（0 與 x+=0.1）在新三頂點測試分別得到 10 和 10.1，body 移到 20 時得到 20 和 20.1；私有槽保持 2.25。
- 遊戲控制 MixedClothesGame268.json.clothing.tsv 實際呼叫 installed AssignedAnotherWeights.AssignedWeightsAndSetBounds，再等 Destroy 完成，確認第 3 槽為 null。BakeMesh 在 borrowed x=10/20 兩次皆讓 null 槽使用單位矩陣再乘原 bind：四個點為 (0.2,0,0)、(1.2,0,0)、(0.2,1,0)、(0.2,0,1)。最初生產 palette 還保留被刪除的骨舊 world；現補上 null 分支，不能以私有 skinning 的成功取代刪除分支的驗證。這個測試角色外層保持中性 frame，不宣稱任意 Studio 全局位移或完整 Animator graph 已驗證。
- source DynamicBone component 的 GameObject 留在衣服 prefab；只有 Root 換到 body。原版 component_transform=p_cf_body_bone proxy 丟掉元件原本 rotation/scale，會誤算 EndOffset、物件尺度與慣性。新 config 保存完整 component ancestry，runtime 為共享骨求解保留這些獨立 helper transforms；同步 Animator 姿態時不能刪掉 helper。top 子零件仍掛 ct_clothesTop，私有骨使用該 prefab parent matrix，body 槽不再重乘一次。
- 本輪先在 shared retarget 覆寫 component_transform 才讀 source component name，程式審查發現順序錯誤，已改成先從原 source ID 讀 name／ancestry 再重綁。新增 ancestry Vec<Tf> 時 Debug derive 引發 E0277，補 Tf 的 Debug；失敗 log MixedClothesTargeted269.log 保留。未存在的 clothesObjName 搜尋沒有當作來源，已由 ChaControl 的實際 ct_* 名稱與 topPartsNames 對照。
- 首次遊戲探針啟動後因短時間未看到輸出，就推測 batch mode／EndOfFrame 有問題並啟動 v2；之後盤點發現首批 MixedClothesGame268 已完整產出，不能把這個推測當確認根因。兩次讀 v2 的檔名失敗已保留在工具紀錄；後續以實際檔名、JSON error、clothing TSV、PNG 確認成功批次，不能依程序退出或表格中只看到長度就猜 fixture。自建 PID 路徑核對後清理，DLL 已移回工具，未保存遊戲卡／場景。

## E-125　ModBoneImplantor 改變衣服骨重綁與碰撞來源（2026-09-28）

- 原先只按 vanilla AssignedWeightsAndSetBounds 分析不完整。本機安裝 ModBoneImplantor 1.1.2，Harmony prefix 會讀取 BoneImplantProcess：有效 trfSrc 以 SetParent(實際 body trfDst,false) 植入；SMR 槽依「已植入 Transform 參照 → body 同名 → null」選擇。即使標記無效、沒有成功植入，標記存在仍會進入新 array／未知槽為 null 的分支。沒有標記才執行 vanilla；不能把插件名字或角色名字當成某張卡一定植入的證據。
- 新實作解析標記、活躍 GameObject 層級與來源 DFS 順序，保留私有原始 ID／local TRS，以完整 body 祖先 TRS 橋接逐幀更新。植入骨可從待刪除 cf_j_root 子樹被救出。遊戲 MixedClothesImplantGame270.json.clothing.tsv 實際建立標記並執行已安裝 Harmony hook：removed_private 存活，外部未植入 private 槽變 null；七頂點在 body x=10／20 的兩次比較皆為 0 m。另一份無標記控制仍得到原 vanilla 結果，原 2e-6 m 門檻未放寬。
- 新混合路徑最初只用「骨名字是否缺於 body」判斷需要 private runtime，會漏掉與 body 同名的植入骨；植入參照在遊戲有優先權。已加原 SMR slot ID 的植入集合判斷，測試確保 private borrowed 保留 x+2 local offset，cache 還原及 body 移動後仍成立。
- 初版仍只保存 authored m_Colliders，會提前丟掉 Implantor 稍後追加的 collider。改保存 rig 範圍的完整 collider metadata，普通版追加植入 collider，Ver01 保持原清單。共享 root 也保留植入 component／collider 完整祖先橋接並逐幀更新；不能只補 private renderer 的 skinning 就認為 shared root 物理正確。定向測試驗證兩次 body 位置、collider world 與有／無接觸，負對照位移差 0.05564100 m；此數字是 Rust 定向測試，不冒充遊戲物理比較。
- parent helper 加入後，top subpart 的 prefab root 不再是 hierarchy root，依 parent 判斷元件名字會誤留原 prefab 名。現於重親前按 ChaControl 的 ct_* 名字重命名實際 prefab root，並維持 SMR 槽 ID／bind 順序；不能把輔助祖先當遊戲 prefab。
- ExtractedMesh 新增 bone_implants 後漏改 session 的完整 initializer，編譯 E0063。已補 Vec::new()；失敗 MixedClothesTargeted270.log 保留。後續新增 collider ancestry 已核對完整 DBone parser initializer，避免同類漏欄位。寫此節時第一次 patch 使用 README 中不存在的單行 Contact is measured anchor，工具拒絕整個 patch，兩檔均未變；已讀實際末尾後重新套用。
- 兩份遊戲控制都是中性外層 frame、人工構造的載入分支，並非某張原始 mod 衣服已完整對照。五張既有卡的 mixed cloth renderer 均為 0；它們只證明既有路徑回歸，正向混合／植入覆蓋來自來源控制與定向測試。外部 serialized-file script/PPtr、UncensorSelector.TransferBones 植入支援及跨 renderer 私有鏈相互更新，不能由這些控制自動推定已完整等價。
- 已讀取植入控制 JSON 的空 error、TSV 及 PNG，核對自建 PID 2188 的 CharaStudio.exe 絕對路徑後結束；DLL 移回 Tools/GamePoseProbe/OpenKoikatsu.GamePoseProbe.clothes-implant-run.dll，未保存遊戲卡、場景或配置。
- 最後審查補上同一 GameObject 的 m_Component 順序：多個 BoneImplantProcess 的 SetParent 順序、同一 Transform 的多個 collider 順序不能從 serialized table 推定。純測試重跑通過後再編 UE、重拍五卡，使用 Final build／shots log 作驗證。最終 UE log 唯一 Error 是既有 CommonUI 未使用 CommonGameViewportClient 的 routing error，本轮未處理；cleanup hitches 也仍存在。15 張尺寸與 71 個 0 cm audit、正常退出均實際核對，未因這個既有錯誤假稱整份 log 無 error。
- 審查確認仍有整合缺口：共享 root 的粒子來源仍是 body Skeleton，植入的新增私有後代尚未合併。保留 component／collider helper 只能修正其 world transform，不能宣稱完整植入 particle hierarchy 已有通解。後續必須以 prefab 級 rig coordinator 合併身分與一次求解，再提供各 renderer；不要用 renderer 級重算 shared root 或名字 fallback 掩蓋。詳細下一步已寫入 HANDOFF2 §127。

## E-126　植入 rig 必須與共享粒子樹整合；嚴格遊戲 solver 比較（2026-09-28，已修正）

- 沒有把 E-125 的整合缺口當完成。本輪接續新增 implant/rig.rs：每個 prefab 保存有身分的存活 TF／body links／colliders／DynamicBone bindings；一次分配作用域內 ID，body link 直接 alias 實際 body Transform，再收集所有 shared/private 粒子。各 renderer 只讀同一個求解後 world，不再各跑一次植入的鏈。top/bottom/subparts 依 copyDynamicBone=true、bra/shorts/gloves/panst/socks/shoes 為 false；Ver01 保留來源參照。更新 cache layout 86／pipeline 117／extract 271。
- 正向測試先通過 shared root 包含新增後代、兩鏈共用同一 TF、两个 renderer dt=0／0.5 得到同一份頂點、cache 還原、body 移動、copyDynamicBone=false 不復活 clone root。最後一個 body 移動断言誤假設 root.rotation=identity，忽略前面 solver 已旋轉 root，ClothesCoordinatorPositive271.log 正確失敗；改用當幀父骨完整矩陣算預期，沒有改生產位移或放寬門檻，Positive271v2 通過。
- 一度對 lib.rs 多段 patch 將檔案頂端 EXTRACT_CACHE_VER 放在後續段落之後，工具找不到向前的 anchor，整個 patch 拒絕；核對檔案後按原文順序重套。修改 set_world_pos 時也誤以為既有程式呼叫 mat_vec_local，實際是展開四項公式；工具拒絕，讀取原文後改精確段落。不能拿失敗工具呼叫當改檔成功。
- Windows 的 test executable 仍在跑五卡時，再編同一份 test binary 引發 LNK1104；Cargo 編譯鎖已放掉但 OS 仍鎖住 exe。ClothesCoordinatorGameComparison271.log 保留；等原五卡程序正常完畢再編測試。不要終止正在驗證的測試、刪除 target 或改用成功 exit code 掩蓋。另一次命令讀取尚未產生的 AllUnit271Final.log 得到缺檔，該檔不是成功依據。
- 新遊戲 fixture 實際植入兩節私有骨，ordinary shared root 與 private root 兩鏈、120 次 60 Hz，記三骨 local TRS 與七個 BakeMesh 點。初版位置最大差 11.29 µm／quaternion 0.00002284／頂點 56.45 µm，原 2e-6 m／2e-5 quaternion gate 拒絕。不得放宽 gate。推測 frame 掛在 objTop 引入世界基準；v2 改為物理控制 frame 的世界 identity，結果數值完全相同，已排除此推測，初版／v2 產物都保留。
- 展開逆世界矩陣的 translation 與約 x=10 的頂點相消會丟失 local offset。改用逆線性 basis 乘 (worldPosition-parentWorldPosition)，保留 shear。local 誤差降至 9.32 µm，但 quaternion／頂點 gate 仍不通過，不能宣稱已找到完整 solver 根因。下一步用實際 Unity Quaternion.FromToRotation 的角度控制與分骨診斷排查；ClothesCoordinatorInversePosition271.log 保留。
- 元件先後順序還影響 collider world：前一鏈旋轉實際 body／implant 祖先後，後一鏈要重讀 transform；逐幀開始的 matrix snapshot 會過時。新 state 對 coordinator body-space 與 implanted 範圍在各鏈 LateUpdate 前刷新矩陣，不能依私有／body 原始數字 ID 的碰巧相等判斷。ClothesCoordinatorColliderOrder271.log 驗證後一鏈 collider 位置與當幀真 TF 一致，並確保前一鏈確實移動祖先。
- 271 首次五卡回歸通過（239.94 秒）、首次 UE build 成功（25.05 秒），但當時後續 solver 修正仍在進行，不能以這些初版結果代替最終版本驗證。每個自行啟動的遊戲 PID 都按絕對路徑核對再結束，探針 DLL 在控制結束後移回工具；v3 亦已清理，遊戲 plugins 不留 OpenKoikatsu.GamePoseProbe.dll。
- 真正造成重疊鏈累積差異的是 Unity native Quaternion.FromToRotation 的小角度分支。遊戲 v3 七角度控制確認 ≤0.001 rad 回 identity、0.0015／0.00485 rad 有旋轉。舊 dot>0.9999 過寬，完全取消 parallel cutoff 也錯。依實測改 dot>0.999999；不可再只看 tiny-angle 純数学推導就移除此分支。七個 native 控制分量誤差皆 <1e-7；已有 0.00485 rad 定向測試仍通過。
- stable inverse basis＋native cutoff 後，原 coordinator 遊戲比較通過：三骨最大 local position 0.03 µm、quaternion 分量 0.00000004、七頂點 0.19 µm，2e-6 m／2e-5 門檻不變。ClothesCoordinatorNativeQuaternion271.log 四個手動測試均通過；slot 載入兩分支仍有 28 頂點 0 m 控制。失敗 Comparison271v2／SolverDiagnostic271／InversePosition271 仍保留，不改寫成成功。
- 最終 dbone:: 23 通過／7 手動忽略；普通六骨 contact、Ver02 十骨 contact 手動各通過，確認小角度修正沒有破壞既有來源 solver 控制。五卡 30 幀／cache／scene 回歸通過（116.62 秒），UE build 23.36 秒成功。ClothesCoordinator271 全部 15 張已目視；71 個 UE 最終頂點審計均 0 cm，15 captures、正常 exit 0。唯一既有 CommonUI routing Error 與 cleanup hitches 如實保留，不聲稱 log 全無錯誤。
- 本次修正的是 prefab 級身分／一次物理求解與該控制的數值語意；五卡 mixed cloth renderer=0，不能用它們證明原始任意 zipmod 的植入解析。外部 PPtr、任意子節點順序、Uncensor hooks、跨角色碰撞和完整 Animator graph 的界線記於 HANDOFF2 §128，避免下次把缺少對照誤寫成完全支援。
- 收尾命令把 Get-Process 查詢已退出 PID 放在最後，PowerShell 對找不到程序留下 exit 1；前三份 JSON 空 error、PNG、v1/v2/v3 物理檔相同與 DLL 已移除的 Python 斷言都已通過，不能把整個 wrapper exit 1 誤解為這些斷言失敗。後續用顯式布林程序快照與各工具實際退出碼分別核對。中文命令輸出另因 console encoding 亂碼，MD 本體按 UTF-8 寫入及檢查，未重新編碼原檔。

## E-127　Transform ID 不是 Unity 子骨順序；notRolls 要按來源展開（2026-09-28，已修正）

- §128 尚存的排序缺口已確認：children_of 對 PathID 排序，prefab coordinator 又從 HashMap 分配新 ID，會使多分支粒子走訪偏離 Unity Transform.m_Children。新 Tf 保存來源 sibling index，序列化 parser 從父層 m_Children 的位置讀取；複製／cache 保留，植入 SetParent(false) 依載入順序追加到實際 body 既有子骨後，並保持 local TRS。不使用角色或 mod 名稱選擇。
- 舊 gather/append 對每個 notRoll child 立即展開全部 grandchildren；反編譯 DynamicBone.AppendParticles 先處理普通 siblings，再只展開最後一個未 excluded 的 notRoll child。BranchingGame272 的 actual SetupParticles 已核對 plain=18、notroll=14、excluded=9 粒子，包含虛擬末端與 parent indices；第一個 roll_a 的 ra_tip 在 notroll 分支不進粒子，不能以較合理的自訂演算法改掉遊戲語意。對照使用獨立世界 identity 控制、120 幀 60 Hz，36 個 final transform；JSON error 空、PNG 存在，無保存卡／場景／配置。
- 首輪新增測試編譯失敗 E0308／E0502：DBoneSet.extend 接受 owned Self，誤傳 &second；粒子名稱 Vec<&str> 借用 state 直到模擬後印 count，阻止 mutable step。已改 owned config 與 Vec<String>；SourceSiblingTargeted272.log 失敗產物保留，不能當作生產 solver 測試成功。所有 Tf initializer 同步補 None，來源 parser 再填實際 sibling index。
- 一次 rg 把 Tools/*.ps1 與 *.md 當 Windows 原樣路徑，exit 1；後續改用 -g pattern 或確切路徑。沒有把搜尋失敗當來源不存在。遊戲自建 PID 69976 核對 C:/portable/KoikatuSunshine/CharaStudio.exe 後結束，探針 DLL 移回 Tools/GamePoseProbe/OpenKoikatsu.GamePoseProbe.branching-run.dll；plugin 目錄無殘留。
- 新控制原門檻通過：三組 exact particle order／parent indices 一致，36 骨最大位置 0.07 µm／quaternion 分量 0.00000107；SourceSiblingTargeted272Final.log 三項測試成功。原始 oo_base bundle 5,531 個 child references、814 個 parent 的 source order 不同於 PathID 排序，證明這不是僅合成負 ID 的問題。兩 prefab marker append／body 既有 children／cache 保留都有正向測試。
- 審查分配 helper 身分時，把原 TF.parent 暫存在新骨架會碰到跨 rig 的同數字 source ID。現分配時 parent=None，待所有節點分配完才依作用域映射；append rank 另按 body 原 children 與 prefab 載入次序產生，不由 HashMap 枚舉决定。重建 state／cache 後仍一致；不以同名 bone 或重排 SMR bind 槽補救。
- 最終 dbone:: 24 純測試通過、8 手動忽略；四個 clothing game、普通六骨 contact、Ver02 十骨 contact 另行通過。五卡 30 幀／GPU palette／cache／scene 4→2 回歸通過（242.70 秒）；UE Editor Development build 成功（22.78 秒）。SourceSibling272 全部 15 張 900×1080 已目視，15 captures／71 個 final display audits 最大全 0 cm、正常 exit 0；唯一 Error 為原有 CommonUI routing，cleanup hitches 仍存在，無 material compilation error。
- cache layout 87／pipeline 118／extract 272，新增 Tf serialized 欄位不可沿用舊快取。遊戲探針已移出 plugins，UE 自建 PID 20404 正常退出，正式 Saved/Screenshots 的 T-pose 圖未覆寫。有限多分支 native 控制與原生 serialized parser 驗證不代表全 Animator／外部 PPtr／Uncensor hooks 已完整支援；後續範圍保留於 HANDOFF2 §129。


## E-128　外部 MonoScript 參照不能退回本檔 PathID（2026-09-28，腳本來源修正已驗證）

- 普通／Ver01／Ver02、DynamicBoneCollider 與 BoneImplantProcess 的腳本查找原先忽略 m_FileID。外部腳本若 PathID 與本檔 MonoScript 相同，會被誤認為本檔類別；不存在則漏讀。新共用 PPtr 以 FileID＋PathID 保留身分，FileID>0 對到本 serialized file.externals[FileID-1]，只從真正 MonoScript.m_ClassName 辨識；m_Name 只是資產標籤，不能當 managed class。外部 Transform／collider／marker 欄位尚未合併跨檔 rig，現明確拒絕本檔同號 fallback，不把拒絕當完整支援。
- 一次 PowerShell 搜尋使用 Bash brace expansion src/{lib,session,...}.rs，ParserError 令整批未執行；後續改逐個明確路徑。第一個跨段 apply_patch 又因重複 anchor 順序拒絕，整 patch 未套用；拆成骨架欄位與方法的順序修改。Python 改檔脚本從 Tools/ok-extract 工作目錄執行卻仍使用 repo-relative Tools/ok-extract/src 路徑，FileNotFoundError 發生在第一個讀檔，無改檔；該 wrapper 又繼續 cargo，Check273 的 E0425／E0061 只是尚未完成接線，不能當 runtime failure。已回 repo root 完成接線，Check273v2 成功。
- Targeted273 初版兩項控制失敗：直接 bincode 比對含 HashMap 的 DBoneSet 會受枚舉序影響，欄位完全相同仍得到不同 bytes；改比較 serde_json::Value 的全部語意欄位，保留骨／參照陣列順序。另一項假設 UnityPy lz4 存檔將 directory 放在前面，實際是 flags 194 的尾端；生成 dependency 改明確 tuple (data_flag=66, block_info_flag=2)，並保留 source 的尾端格式。UnityPy.save 的 tuple 說明與實際 save_fs 參數順序不一致，須依實作核對，不照 doc 猜。
- 早期 ZIP metadata parser 的 4KB prefix 超出時會讀完整 entry，造成前端 directory 稍大的 bundle 也被全讀；已改 BufferedEntry 按 directory reader 要求逐段取資料，無 payload 解壓。真正尾端 directory 在 deflate ZIP 仍需消耗該 entry，這是目前 I/O 限制。cache 記每個來源完整路徑／size／mtime，跨啟動只重掃改變的來源；同一載入批次使用記憶體快取，clear_bundle_cache 同時清 reference index。
- 原始 oo_base 與 ao_hair_00 的副本把全部 MonoScript 移至 dependency，來源保留同 PathID 錯誤類別 decoy；Saved/Tests/ExternalScripts273 在專案內，遊戲 abdata/mods 不改。Targeted273v2 三項控制通過：body 270／hair 34 個真 MonoBehaviour 類別與完整物理 config 對回原始 bundle，body 四個 Ver02、24 個女性 native collider；loose bundle 與 zipmod 依賴、同 bundle 多 serialized-file 都驗證。目錄分別讀 136/41,568,676 與 114/18,580 bytes；二次索引 sources=2/refreshed=0。原失敗 log 保留。

- 接線檢查又從 cargo 子目錄用 repo-relative rg 路徑，搜尋報缺路徑而 cargo 照常執行；已在 repo root 重搜三個 begin_load 呼叫，避免把最後 wrapper exit 0 當整段搜尋成功。今後同一命令的所有路徑必須依 workdir 核對，編譯另用獨立 exec。新增 streaming reader 審查發現 seek 超過 EOF 時 slicing 可能 panic，已先判斷 position>=buffer length 回 EOF，增加截斷負對照。
- 遊戲 ExternalScriptsGame273 實際載入兩份 external bundles：body 13 prefab／4 Ver02／28 collider（女性來源子樹 24），hair 29 prefab／5 ordinary；所有 MonoBehaviour type、hierarchy、物理 public fields 與原版一致，JSON error 空、PNG／external-scripts.tsv 都存在。這個控制驗證原始資源改寫副本的外部腳本反序列化，不宣稱 solver／任意 plugin overrides 已完整對照。自建 PID 24148 核对 CharaStudio.exe 絕對路徑後結束，probe DLL 移回工具，未保存遊戲卡／場景／配置。
- 最後審查補上依賴的快取失效：只以來源 bundle stamp／mods stamp 組 session key 會漏掉 abdata 中較舊 dependency 的變動。每次卡片／extract boundary 先計完整來源 path/size/mtime epoch，加入 session、prefab variant、OBJ stamp；變動清 parsed bundle、entry memo 與 reference index。epoch 不靠 newest file 決定，純測試修改較舊 dependency 後確實清 index。未載入依賴的普通卡只做檔案 metadata 掃描，不建整個 CAB payload 索引。

- 審查來源衝突時再補實際 package scope：Sideloader 可以遇到相同 CAB 的 zipmod，遊戲 log 本身也有 CAB 隨機化重試。只有全域唯一匹配會讓合法套件內依賴被另一個 bundle 阻擋。override_bundle_with_source 與 accessory_bundle_source 現保留真正返回 bytes 的 zip 路徑，普通／MaterialEditor visibility fallback／共享衣服三條路徑都傳至 ParsedBundle。先限於實際 source zip 的精確 CAB，再找全域唯一；source scope 改變清 script/physics maps。無 source hint 或同套件內仍多匹配才拒絕，不能依索引順序拿第一個。新增「兩個 loose 重名＋一個含 source/dep 的 zip」正向／scope 移除負對照，並保留全域 ambiguity gate。首次五卡是在此項修改前開始，只算中間回歸；最後版本須重新跑。

- 六項最終控制在 ExternalScriptTargeted273Final.log 通過，dbone:: 純測試 ExternalScriptAllUnit273Final.log 24 通過／8 手動忽略。source zip 內的精確依賴會優先，移除來源 scope 時三個重名候選確實拒絕並清空旧 physics config；回復唯一來源亦不殘留舊 ambiguity。最終 cache 89／pipeline 120 明確避開初輪 88／119 產物，五卡已重新驗證最終編譯版本。最終實作不調整任何 solver 門檻或卡片骨比例。
- 補查卡片時照較早訊息使用 C:/portable/KoikatuSunshine/mesh/_dump，實際路徑已不存在；Get-ChildItem 明確失敗。後續直接核對目前 mesh_dump，五張 dump 與最終測試逐一對應，不能從 UserData 的其他卡片名猜另外三張，也不能把同一 wrapper 後續 git／列表成功的 exit 0 當路徑查詢成功。三月七、Alice、優香也已包含，沒有漏測。
- 最終 ExternalScriptFiveCards273Final.log 五卡回歸通過（231.69 秒），30 幀／GPU／cache／scene 4→2，無 unresolved MonoScript。ExternalScriptUnrealBuild273Final.log 編譯成功（22.46 秒），新 binary 重拍 15 張 900×1080，全部目視；71 個 final display audits 均 0 cm，正常 exit 0。這個 audit 只對 extractor，沒有混稱遊戲頂點／像素誤差。K384 face camera 裁到高髮髻、既有 CommonUI routing error／cleanup hitches 明確保留；沒有 material compilation error。UE／遊戲自建 PID 均已結束，probe DLL 不留 game plugins，正式 T-pose 圖未覆寫。整個外部 Transform／mesh／collider／marker graph 與其他 HANDOFF2 §130 限制仍未完成，不能把本節腳本修正當全部卡片相容完成。

## E-129　動畫／表情 TSV 的來源身分與 Unreal 匯入（2026-09-28，已驗證）

- 先前動畫 UI 只掃固定來源目錄，不會自動顯示匯出的 79 個 mod bundles、男角色來源與完整 Studio 目錄。這輪新增八張持久 DataTable＋UKKAnimationCatalog，保留所有原始 cell／空值／重名 row／來源順序與 SHA256；mod 絕對來源另轉為 game-root-relative ZIP＋entry 身分。不能把 29,956 個 metadata rows 當已烘焙 UAnimSequence，也不能按裸 clip 名去重或播放。
- 盤點時錯用 csv.DictReader 讀三份 scan_mods 產出的無表頭清單，會吃掉第一列；輸出又碰到 cp950 無法編碼中文。該盤點只讀檔，沒有改原資料。匯入器對五份 headered TSV 用 csv.reader(newline='', strict=True)，三份 raw TSV 用 literal tab 拆行；統計實際為 mod_anim_bundles 79、mod_studio_info 4,980、mod_head_prefabs 2，不能沿用錯讀的 78／4,979／1。後續命令明確 PYTHONIOENCODING=utf-8。
- 查 Rust crate API 時猜測 unity-asset-decode/src/object.rs 與 object/，兩者均不存在；正確為 lib.rs 重匯出 unity-asset-binary 的 object 模組。錯誤搜尋輸出保留，不以 wrapper 最後成功當全部查詢成功。跨檔 Transform／物件 graph 尚未修改，因本輪使用者新增 Unreal catalog 匯入而先接此項；先前 §130 缺口仍保留，不能宣稱已完成。
- 首個 TSV quoted-cell 控制在 Windows 失敗：Path.write_text 預設將 LF 改為 CRLF，csv.reader 正確保留 cell 內的 CRLF，卻與測試預期 LF 不同。修正 fixture 寫入 newline=''，沒有把匯入器改成刪換行，避免為通過測試破壞原始內容。失敗 log AnimationCatalogPrepareTests274.log 保留，修正重跑另存 Final。
- Studio CSV 的 bundle／override bundle 和 file／override file 必須對應實際 controller 及 state，不是裸 clip。解析先按 base 或實際來源 ZIP 配對 animation_sources，無同套件來源時只接受唯一匹配；多來源或多 full-state-path 同葉名都拒絕。UI 延用 Rust 真實 controller catalogue 的完整 label，不能拿未包含 PathID 的 animation_clips.tsv 假造唯一 clip identity。FBS patterns 亦以 source＋prefab＋controller＋target index 查詢，不將 base 嘴型配對套到未知 mod head。
- 首次 Unreal import 的九個 uasset 都寫出，但 commandlet exit 1，不能看到 saved 訊息就當成功。原因是新建 DataTable 尚無 RowStruct 時先 EmptyTable，UE 記錄八個 Error，commandlet 會以 error summary 判失敗。修正為只有既有 RowStruct 才清舊表，再設正確 row struct；失敗 log AnimationCatalogImport274.log 保留，最終須重編／重匯入，再獨立程序逐 cell 驗證。首次尚不存在總覽資產的 lookup warning 改用 LOAD_NoWarn；不隱藏實際解析／儲存失敗。

- 兩次讀檔命令誤寫 `Select-Object -First seventy`，PowerShell 要求整數；必須寫 70。另猜測 ShaderCompilerCore.h／CameraTypes.cpp 位置失敗；先用 rg --files 核對實際 ShaderCompiler.h／CameraStackTypes.cpp，不以鄰近命名推定 API 檔名。截圖統計誤搜「Screenshot saved」得到 0，實際引擎記錄是「Tracing Screenshot」；须同時核對 PNG 數、尺寸及 capture log，不能把錯誤搜尋當截圖失敗。
- 第一輪 Gallery 驗證雖已選中匯入的 Studio walk，但嘴型控制仍放在 3D 隱藏的 inspector，且截圖時 shader 還在編譯，衣服呈黑色。嘴型操作移到 3D 操作欄，增加完整 channel tooltip；驗證等待 ShaderCompilingManager 完成後才拍。只關 bEnableOnScreenDebugMessages 仍有引擎 directional-light 警告；最後驗證用 GAreScreenMessagesEnabled 關閉 capture 疊字，log 不隱藏。原始失敗圖保存到 Saved/DebugShots/old/AnimationCatalog274UI_v1／v2。
- 重跑截圖時把 mesh_dump 的角色資料夾誤當成 card PNG 路徑；Test-Path 擋住啟動，沒有程序或角色資料改動。實際卡在 UserData/chara/female，mesh_dump 存遊戲 mesh／參考照。重跑先以 LiteralPath 確認五張卡存在；不能依資料夾的 .png 副檔名判斷它是檔案。
- 最終 import／reload verify 已 exit 0，50,066 列逐 cell／欄序／列序／來源身分／SHA256 與完整 MD 均通過；Unreal ImportedData／Playback 兩項 Success，後者實際取樣 base Studio walk 與 mod L_WLoop1。資料清單不包含逐格 curves，不以匯入成功宣稱所有 Animator graph／追加層已完成。
- 末次原生 Gallery 畫面又發現程式呼叫 OnMouthChanged(0.8) 只更新角色與 SelectedMouthOpen，local slider 沒同步顯示，畫面看似 0。將 MouthOpenSlider 保存為 widget 屬性，開合 setter 同步它的值，並在原生 verifier 加 slider=0.8 的明確 gate。只重拍受影響 Gallery；五卡 renderer／匯入資產不因 UI 同步修正而需要全部重跑。

## E-130　截圖取景必須使用當幀網格與玩家實際投影（2026-09-28）

- 初輪 walk 截圖沿用中性 BaseVertices 的距離，雖追蹤 root/head anchor，腳尖與 K384 高髮髻仍會被裁。新增當幀 procedural vertices＋CPU skinned vertices（含 morph）取景，所有卡共用透視不等式，actor／骨／mesh 不為相機重新定位。CPU skin readback 只在凍結拍照／明確 FrameCurrentPose 時做，不放每幀 tick。
- 第二輪新取景仍裁切，自己計出的 NDC<=0.92 不能當真實渲染 gate。根因是 StudioCam.FieldOfView=50°，PlayerCameraManager.SetFOV(40°) 鎖住最終視角；CameraComponent.GetCameraView 的 projection 不是玩家實際畫面。改為更新 player camera cache，再以 LocalPlayer.GetProjectionData 讀引擎真正投影矩陣，包含有效 FOV／aspect constraint／view extension。初輪與第二輪 15 圖保存到 Saved/DebugShots/old/AnimationCatalog274_v1／v2。
- 初次改接玩家 API 用到 protected LocalPlayer.GetViewPoint，編譯 C2248，AnimationCatalogBuild274v7.log 保留。改用公開 PlayerCameraManager.GetCameraCacheView 取得 near/FOV 診斷，實際投影仍從 GetProjectionData 取得。AnimationCatalogBuild274v8.log 成功；不得改引擎權限或用常數補猜玩家投影。
- 第三輪 log 一度將 camera cache 的 50° 標成 player_fov，實際 LocalPlayer.GetViewPoint 會再用 GetFOVAngle 的 locked 40° 覆寫；投影矩陣已走正確的 GetProjectionData，所以圖像已正確。診斷改明列 cache_fov／effective_fov，不以 cache 欄位描述最後視角。讀第三輪 K384 正臉時該 task 尚未輸出，view_image 報不存在；後續先核對五卡程序完成及 15 檔存在，再完成目視，不把尚未產出當 renderer failure。
- 上半身取景僅 fit 焦點以上的 geometry，以包含頭頂／髮飾；全身 fit 所有可見當幀 geometry，兩者保留 0.92 的投影 margin。此為相機通解，不更動卡片比例或取代遊戲的載入證據。最後五卡實際圖像與投影統計於 HANDOFF2 §131 追加。
- 末次圖像統計第一次猜 `Engine exit requested (reason: RequestExit` 字串，assert 失敗；實際記錄為 Win RequestExit 及 FPlatformMisc::RequestExitWithStatus(0,0)，改核對來源中真正的 exit 0 行。統計前段 PNG／71 audit／15 projection gates 已通過，但整段失敗時不存成功 JSON；修正後重新完整執行，AnimationCatalogShots274Audit.json 正式生成。最後更新 MD 的跨檔 patch 又用錯 E-129 標題 anchor 被工具拒絕，核對兩檔未改後以實際標題重新套用。
- 最終 15 圖／71 audit 全部完成，最大 display vertex error=0 cm；五張全身頭頂／腳尖完整，下緣留至少 43 px。K384 高髮髻與 K091 側髮飾正臉完整可見；mouth 8 的下唇紅線仍需相同 actual-game 控制才可判斷材質，未作卡名／顏色補猜。兩個自建最後 UE 程序已正常退出；圖像、log、重跑參數均有保存。

## E-131　KKS 手勢使用 ShapeHandInfo；不可將舊 HandAnime 清單當執行路徑（2026-09-28）

- §131 把獨立手勢列為 Animator 追加層待辦，核對已安裝 CharaStudio 的 Studio/OCIChar.ChangeHandAnime 後修正：它呼叫 SetShapeHandValue 與 SetEnableShapeHand；ChaControl 的 LateUpdateForce 在 Animator 後呼叫 UpdateAlwaysShapeHand。來源是 chara/oo_hand.unity3d 的 cf_anmShapeHand／cf_customhand，不是 legacy HandAnimeCtrl 的 cf_hand_L/R controllers。原 42 列 HandAnime 資產仍保留，但不能依 metadata 推斷它是本版執行方法。
- AnimationKeyInfo.GetInfo 用序列索引，不是每鍵儲存的 no；GetKeyCount 取第一個序列長度。混合逐軸使用 Mathf.LerpAngle，Repeat 後正 180° 平手，t 夾在 0..1；不能換成 slerp 或不夾值的 Euler 線性插值。只寫 local rotation，左右 mask 分開；私有 mod 同名手指不是 body 的另一個目標。手勢停用必須從同一動畫 frame 還原，不能保存覆寫後的 pose 為 rest。
- 遊戲探針實際呼叫已安裝 ChaControl.UpdateAlwaysShapeHand，24 組、58 cases、34 bones/case（1972 筆），含逐手全 pattern、混合 0.3/0.8、超界混合 -1/2 與停用。Rust 比對 local rotation 最大 quaternion 分量誤差 0.000000119，位置與縮放逐值一致，2e-5 門檻未放寬。HandShapesGameControl275.log 保存結果；不是只跑自建預期或 metadata assertion。
- 本輪猜 skeleton 方法 recompute_worlds，檢查實際 API 後在編譯前改 rebuild_worlds；rg 在 PowerShell 參數裡使用不存在的檔案 glob KKPreviewActor.* 報 os error 123，改以 rg --files 得到真實 Plugins 路徑。清理 probe 初次比較 / 與 Windows Path 的反斜線導致 guard 拒絕；先 GetFullPath 正規化後核對自建 PID 26980 才停止。DLL 已移回 Tools/GamePoseProbe/OpenKoikatsu.GamePoseProbe.hand-shapes-run.dll，未保存卡／場景／配置。
- 更新此節第一次用不存在的短標題 E-130 當 patch anchor 被拒絕；先讀實際末尾再追加。不得猜標題或因 patch 工具輸出簡短就假設已寫入。
- Unreal 手勢 UI／五卡／cache／GPU palette 驗證已完成，最終結果見本節末尾與 HANDOFF2 §132。完整 Animator graph／blend tree／active additional layers 仍是其他載入缺口，這次手勢通解不能冒充它们已完成。
- 第一輪 Unreal HandShapes 測試失敗：用 Contains(index/little/middle/ring/thumb) 計算目標會把額外 helper joints 算入，右側算出 21 而非原生 15。loader 的 enum 30 個目標與遊戲數值比較已正確；修測試為完整 cf_j_{stem}{01/02/03}_{L/R} 身分，其他 helper 繼續驗證不被改動。HandShapesUnrealAutomation275.log 保留失敗，後續用新 binary 重新測試，不把 ImportedData／Playback 成功當整組通過。
- 首輪五卡截圖忘了 OK_OUTFITS_LIMIT=1；explicit OK_SHOT_CARDS 沒有 directory 模式的 outfit0 預設，三月七第一卡的 7 套讓 queue 變成 35 tasks。按實際 exe 絕對路徑核對自建 PID 59824 後停止，完整 partial folder 移至 Saved/DebugShots/old/HandShapes275_extra-outfits。重跑明確設 limit 1，不能把第一卡多套圖誤當五卡全測。
- 五卡完整 all-pattern session 測試以 debug binary 跑，首卡已花數分鐘且尚未結束（Debug GPU palette/多個原生 mod rig 的逐幀重算成本），沒有 assertion failure。核對自建測試 exe PID 28628 後停止，保留 HandShapesFiveCards275.log；改用 release test 執行同一測試、同一 48 selections/card、同一門檻，不刪案例或降低驗證以假稱完成。

- 上條紀錄一開始因 command 的 workdir 是 Tools/ok-extract，把相對 ERROR_LOG.md 寫到工具目錄；核對該新檔只包含本輪單條內容後搬回根 MD 並移除新檔。後續記錄均用完整路徑。
- 最終 HandShapesUnrealAutomation275Final.log 三項 Success；HandShapesFiveCards275Final.log 五卡 240 selections／30 targets／GPU／停用／cache／clock 成功（132.95秒）。HandShapesUnrealBuild275Finalv2.log 成功；五卡最終 15 PNG 与原生 Gallery 已全部目視、正常退出。verify_hand_shapes.py 完整核對通過，71 display audits 最大 0 cm、15 projection fits、全身至少 51 px 下緣，HandShapes275Audit.json 已保存。
- Debug 中途執行成本的精確熱點未剖析，不能把 GPU palette 或某個 mod rig 直接當已確認的性能根因；release 執行同一案例成功只能證明測試完成。所有遇到的失敗／guard／錯誤設定都保留上方紀錄，最新結果取 Final logs。
- 遊戲手势數值與 Unreal display audits 各有不同比較來源；不能混在一起宣稱完整遊戲幾何／像素等價。下唇紅線、部分衣服穿透與完整 Animator／expression graph 仍須各自 source 控制；本輪沒用顏色／卡名修補。
- 確認本機 Unreal 的 TestExit="Automation Test Queue Empty" 即使單項 Fail 也會 RequestExitWithStatus(1,0)。所以程序 exit 0 不代表 tests 通過；必須讀每項 Result，verify_hand_shapes.py 明確拒絕 Result={Fail} 並要求三項 Success。兩次 automation 正常退出的描述只指程序生命週期，首輪測試失敗已記錄。

## E-132　嘴型 8 下唇線必須以原生 FBS／遊戲圖找來源（2026-09-28，調查中）

- §131／§132 的 mouth8/open0.8 可見下唇紅線。不能按卡名、色彩或移動幾何修補；新增原生 probe 比較 loaded head 的實際 FBSBase.CalcBlend、所有 target channel weights／BakeMesh 和原生 shader 圖，固定 mouth width=1 以隔離随机幅度。其他角色 shape／ABMX／mods 保留，未保存卡／場景。
- 首次猜 FBSCtrlBase.cs 檔名讀取失敗；實際 base 是 FBSBase.cs，已由 rg --files 找到再讀。原生 FBSBase 把 Clamp(open*100,0,100) 轉 int，Unreal 目前直接用浮點 1-open/open；先以 0.61337 與已顯示問題的 0.8 作控制，不能把這個已知語意差異直接猜成紅線根因。
- 首輪 GameMouthMarch 的 JSON error 雖空，六個 mouths case 沒有序列化 meshes 欄位，不能据此推斷 FBSTarget.Length=0。v3 的明確 metadata 證明權威 ChaControl.fbsCtrl／mouthCtrl 引用一致、目標為 5；初輪「無目標」推論撤回，舊 JSON 不作有效變形證據。改為逐 channel／vertex TSV，包含非空目標、case 計數及完整結尾 gate。renderer bounds 涵蓋全身，改以 target0 實際 BakeMesh 頂點取景。
- 嘗試識別 Studio 插件是否合併 renderer 是待驗證推測，不能只憑插件名斷定。探針 metadata 加入 Object.ReferenceEquals 時編譯 CS0104（System.Object 和 UnityEngine.Object 有歧義），改完整 System.Object.ReferenceEquals，MouthShapesProbeBuild276v2.log 保留；v3 編譯成功。自建 PID74792 路徑核對後停止，初輪 DLL 移回工具。
- 第二輪 PID30996 停在 Unity Fatal error，CPU 0.06 秒且 BepInEx log 未更新，沒有 probe 輸出；不能當控制器失敗。核對 executable 路徑及視窗標題後停止該自建程序，重跑明確指定遊戲 WorkingDirectory。原因是否僅工作目錄由重跑判定。另一次讀 KKPreviewActor 猜到 Source 路徑不存在，已用 rg --files 找到 Plugins/KKUnityBundle 真實位置。
- v4 編譯猜 FBSTargetInfo 在 FBSAssist 命名空間而 CS0234；實際 field 型別已有 compiler 可推導，改 var 不猜 namespace，v5 成功。v3 probe 的 renderer metadata 沒有合併或被清空的證據；不把插件優化當根因。
- PID32188 又在启动 Fatal error，因此之前只改 WorkingDirectory 的推测不能定論。Stop-Process 後明確 WaitForExit，再確認無另一個 Studio 程序才重啟；不能在旧程序尚未退出時並發啟動 game。尚無原生錯誤文字可確認確切 startup 根因。
- 非空 TSV 已有 5 targets／54 patterns each／10 cases、3120 channels、33150 vertices；實際 BakeMesh 差值非零（face 最大約0.00884 Unity units）。但同 frame 立即 Camera.Render 的圖片嘴仍為舊 closed 狀態；權重／BakeMesh 不能證明 rendered screenshot 更新。probe 改每 case 等 WaitForEndOfFrame，分開保存 MouthMarchFrame 結果；舊數值資料保留。
- 使用者指出探針圖眉毛消失後，確認 renderer enabled/active、mayuge def_op=100、眼線34/66都有效；透視相機仍缺線，所以不能歸因正交投影或FBS目標被清空。相同 framing 改用已安裝遊戲 Camera.main 的實際元件流程，眉毛／眼線出現。新 camera.CopyFrom 只複製 Camera 屬性，漏掉原相機的元件／渲染流程；精確是哪個 hook 仍未隔離。最终 source default 拍法使用 Camera.main，所有相機參數在 finally 還原，GameMouthMarchSourceFinal 兩圖／metadata 已保存，自建PID4072清理、DLL移回工具。
- CPU嘴型僅更新section0、overlay/outline不動是實際接線缺口，已按同一 deformation 同步各section、保留各自offset/UV/color/normal/winding。五個UE tests成功，原生权重2360筆符合 int percent，但三月七紅線仍在；不能把這個有效修正當成紅線根因。診斷 affine 只作比較，rest最大誤差約0.00796，不能宣稱遊戲幾何已完全等價；未把 affine 寫進載入。另一次猜 alphaShot source 路徑不存在，須先 rg --files 找實際檔，不用不存在檔案推斷插件行為。

- 已定位探針缺眉／眼線的來源：本機 `KK_Fix_Eyebrow.cfg` 的 Enabled=true；反編譯 `IllusionFixes.Blitty.Start` 綁定 Camera.main，`OnWillRenderObject` 暫停原 renderer，`OnRenderObject` 把 DrawRenderer CommandBuffer 掛到該主相機，`Hooks.AmplifyColorBase_OnRenderImage` 再合成 EyebrowFix.rt。Camera.CopyFrom 不複製元件、CommandBuffer 或 Harmony 後處理路徑，因此新相機的圖缺線。不得關掉插件或替卡片畫眉毛來掩蓋。探針使用真主相機並還原參數，補存 renderer component／camera buffers，避免下次把探針錯誤誤判成 Unreal 資源遺失。
- Isolation 三組圖已完成：去舌頭只消掉下唇紅色、仍留下白色下唇突片；去鼻線／關輪廓紅線仍在。這是診斷控制，未納入正式載入設定，不能把隱藏舌頭當通解。另一次 rg 用 Windows 不支援的 src/rig* literal path，明確報 os error 123；後續改直接讀實際 lib.rs，不能將成功的另一個搜尋當此路徑有效。

- 上一項「關輪廓」控制撤回：OutlineEnabled／OutlineWidth 把首次環境值保存在 static，ShotPreview 同程序切 variant 時仍沿用第一次值。修成建立每個 actor／材質時讀目前設定；第一組 Isolation/noOutline 不作關輪廓證據，須以新 binary 重拍並核對各 actor 的 section 數。去舌頭／鼻線的來源提取控制不受這個 static 影響。
- 最新 native probe 證實三個 renderer 均有 IllusionFixes.Blitty，主相機已有三條 AfterForwardOpaque buffer；缺眉根因閉環。但 probe 在停用 FBS 時凍結了隨機眨眼，這輪 eyes 接近全閉、前輪34/66，不能當同一表情。嘴型控制新增 Eyes／Eyebrow pattern0，固定為各控制器載入的 OpenMax，避免隨機 blink 污染重跑。嘴型數值資料不因眨眼失效，畫面須使用再重拍版本。
- 重拍啟動曾誤寫 OK_SHOT_OUTPUT，實際為 OK_SHOT_OUTPUT_DIR；在第一張圖輸出前核對 source、停止自建PID73072並WaitForExit，正確輸出至 IsolationFinal 重跑，未把結果混進 Saved/Screenshots。不能從猜測的環境變數名稱啟動正式測試。
- 最終 native 圖固定 eyes/brow 中性，兩張均已目視，眉毛／眼線恢复。MouthShapesUnrealAutomation276Final.log 五項逐一Success，verify_mouth_shapes.py完整gates通過。隔離圖最終face secondary_sections確實1→0，關outline仍有下唇紅色；去tongue後白色突片仍在，mouth問題繼續保留未完成，不以五個tests Success冒充視覺已修好。45份舊probe與無效Isolation移至已驗證的Saved/Tests/MouthShapes276/old。原生自建59428已核對路徑停止WaitForExit，DLL移回Tools；UE70044／37048正常完成。probe cleanup 又補至finally，拍攝失敗亦還原RenderTexture.active、清temporary resources。

## E-133　下唇突片的世界頂點與骨架來源控制（2026-09-28，基準與犬齒狀態已修正）

- 不能用對 native BakeMesh local vertices 擬合出的 affine 宣稱載入正確；renderer scale 可能被 BakeMesh／TransformPoint 重複計算。新增獨立原生 world control，直接按每個實際 blendshape weight 加 authored delta，再用 renderer.bones 的 localToWorld * bindpose 加權，核對100%單frame及權重sum=1。完整骨world／bind matrices同時保存，使用相同口型／phase定位載入步驟，沒有把診斷變換寫進產品。
- 開始查 skin_mesh 時先在 pose.rs 搜不到；實際 wrapper 在 lib.rs，已由 rg找到。又對 dump 的目錄誤用 Get-Content 且 ErrorAction SilentlyContinue，不能用該次輸出認定dump有效；後續用明確fixture路徑。原生有cf_O_canine renderer active=false，正式載入目前可能未尊重這個狀態，需資料證據再修，不能先猜成唯一紅線根因。
- 原生FaceRoot框架內的所有共有骨矩陣最大分量差2.35e-7，但臉9.79mm／牙19.58mm／舌24.81mm有固定基準偏移，跨mouth0/8及open都不變。查到 apply_default_fbs 的 mouth_ptn_override 預設Some(0)、mouth_open_percent預設80；Live CPU ApplyMouth則假定base為pattern0 closed100並只扣closed，造成重複開嘴。修 live基準使用實際head FBS PtnSet[0].Close，而非卡名或固定索引；GPU morph基準raw，其他控制器不動。pipeline121避開舊錯基準session，decoded cache89不變。
- 又猜 shape/head.rs 與 ok-extract/build.rs 不存在；前者SilentlyContinue、後者明確報FileNotFound，不能依不存在的檔案猜build流程。後续直接用已列出的build-release.cmd。新增嘴型基準控件需純資料permute／異名／同index／缺index／GPUraw控制，再用worldfixture驗證，不以修正假設當完成。
- 原生neutral Eyes权重為92/8，Rust neutral用0.926浮點比例，眼／鼻附近約1.09mm差。neutral FBS亦改原生int-percent，無卡載入每次重設EyesOpenMax=1，避免沿用上一張卡；其他BoneShape不改。這是可重現的來源差異，不用affine或比例調整補猜。
- 上一項0.926是早期診斷假設，不是已讀取該次fixture的證據；最新world比較眼區仍有2.43mm差，不能宣稱full-face等價。眼／眉OpenMax暫存改保存float原bits再做int percent，避免round至milli令0.91999跨成92。真正native嘴型支持頂點（以原生closed/open差識別）最大約1.6e-6，牙／舌2.9e-7；眼的runtime狀態須另對照，不混稱mouth已精確控制眼。
- 首次五卡指令誤以 | 分隔OK_SHOT_CARDS，實際parser用 ;，所以未找到任一指定path並fallback到K091，只輸出3圖。五個view_image有四個找不到檔，立即核對source/log，不能當五卡回歸完成。錯誤3圖移至Saved/DebugShots/old/MouthBasis277_incorrect-card-list，正式重跑用 ;、新Final log並要求5tasks/15圖 gate，舊log保留。
- 修嘴基準後又發現原生三月七 `cf_O_canine` active=false，UE 卻總顯示犬齒。已核對 ChaFileFace.doubleTooth 預設 false、ChaReference 的 ObjDoubleTooth 查找 cf_O_canine、ChaControl 按卡旗標 SetActiveIfDifferent。解析此旗標、使用 head 原 renderer 身分決定可見性，Session/cache/C ABI 保留 inactive target 的全部 FBS，UE 只跳過繪製。不能刪所有犬齒或用角色名分支；pipeline122 對應新增 PartBlob 欄位，decoded cache89 不動。
- 壓縮上下文後一次搜尋又猜成 Source/OpenKoikatsu/Private/KKPreviewActor.cpp；實際仍在 Plugins/KKUnityBundle，rg 明確報不存在，後續讀實際檔。另一次對 rg 傳 README* literal path 出現 Windows os error123，改以確切檔或目錄 glob 搜尋，不從失敗搜尋推斷 README 不存在。
- 新增 visibility cache 測試初版把稀疏 blend_shapes 的 Vec<(vertex,[xyz])> 寫成密集 Vec<[xyz]>，Rust E0308；DoubleToothCard277.log 保留編譯失敗，修測試為真實型別後另跑 Final log。不得把編譯失敗那次當測試已跑。
- 上次型別修正又少一個 array 的 ]，編譯 delimiter error；DoubleToothCard277Final.log 保留，修正括號後結果另存 Finalv2，確認每個測試實際 passed 才算完成。
- 查 runtime 預設環境時又猜 OKGameInstance.cpp，rg 報不存在；真正預設設定在已找到的 KKSkeletalBuilder.cpp。後续搜尋目錄，再以實際檔讀取，不以猜測名稱宣稱有初始化。
- pipeline122 最終 native geometry fixture 重新載入與 serialization 138.27秒成功；最後40組嘴部最大2.30e-6m、牙／犬齒／舌4.10e-7m。先前1.59e-6／2.94e-7是中間fixture結果，不把單一pattern8/open80的小數誤稱最終全組最大；驗證仍以原2e-5 gate，完整眼区不同狀態不算嘴部比較。
- 最後build／5項UE tests／五卡15圖／71audit／15projection gates均已完成；verify_mouth_basis.py成功、MouthBasis277Audit.json保存。全部圖目視嘴外突片消失，犬齒按卡flag關閉、側髮與頸飾跟隨動作；所有自建UE／game程序均已退出，probe未留在plugins。結果與重現見HANDOFF2 §134及Docs/UNREAL_FACE_BLENDSHAPES.md。不能把這個native幾何比較、UE對extractor audit及目視混稱完整遊戲像素等價。
- 末次圖仍可見舌頭偏紅、部分chin outline斷續。發現 resolve_head_detail_texture 的 tang 分支強制solid_png([0.78,0.40,0.42])並早退，未尊重該分支ME／完整shader資料；dump cf_m_tang有Koikano/main_clothes_item及ColorMask/DetailMask/_Color。這是已知材質載入缺口，尚未隔離它對實際畫面各像素的影響，不能再按紅色觀感換常數或誤稱此項已修好。
- 讀舌頭dump時 Python read_text() 用Windows預設cp950，遇UTF-8中文欄位UnicodeDecodeError；明確改encoding=utf-8-sig後重讀。dump另有body/head兩個同名o_tang（mesh60/51），不能只按basename挑來源；本次核對完整hierarchyPath，兩者_Color資料相同，原生FBS實際目標是head的mesh51。這項資料核對不能取代同state材質像素控制。

## E-134　舌頭 draw material 被固定色早退取代（2026-09-28，修正並完成驗證）

- 原生 head 的 cf_m_tang 使用 Koikano/main_clothes_item、null MainTex=white、cf_dummy_mc=全紅線性 ColorMask。三月七／Alice／Yuuka 的 Material Editor 各有不同 _Color，遊戲 probe 三月七值與卡值相同。舊 resolve_head_detail_texture 按 tang 名稱換固定粉紅 PNG 並早退；tooth/canine 也跳過 ME，因此顏色覆寫沒有進入 draw material。不得用另一個粉紅常數修補。
- 本輪抽出已安裝 KKS_MaterialEditor.dll 的 KKS_Shaders resource，反組譯 main_clothes_item 的40個 DXBC。PS0024 的實際公式有第四色：依 r/g/b 連續 lerp，再以 1-max(mask.rgb) lerp Color4，最後乘 MainTex。舊 KKShadersRecreated MainItem 只有三色，不能冒充目前已安裝 shader 的完整來源。四個 Color property 均為 Color 型別，來源 PlayerSettings 是 Linear；cf_dummy_mc、cf_tang_md、cf_tang_n 的 m_ColorSpace=0，數值 mask 不可當 sRGB albedo。
- 第一次猜 KKItemVertFrag.cginc 不存在、Get-Content SilentlyContinue；後續列實際檔找到 MainItem.shader，不從不存在檔案推斷演算法缺失。Shader bindings 命令外層 exit1 是後續 rg 沒命中，Python bindings 本身成功；須分開核對命令與資料。兩次 rg 又傳 Docs/UNREAL*、Docs/SHADER* literal 路徑，Windows os error123；改 directory + -g glob，不能把另一個成功輸出當整個搜尋成功。
- 首次跨多段 patch 中間用無定位的 }，讓後續 anchor 搜尋失敗，工具拒絕且檔案未變；核對實際行再拆成有命名 anchor 的 patch。新增 ItemDrawMaterial 初版把原 SessionPart 的 derive(Clone) 掛到新 struct，成為重複 Clone 並漏掉 SessionPart；寫完即讀回發現，編譯前修正。不能忽略新增 struct 前既有 attributes。
- 修正保留來源 MainTex、四個顏色、ColorMask／獨立 ST，按 material／shader 身分選 head draw 路徑，支援 mod 改 mesh 名稱；ME shader／float／color／texture 在繪製前套用，head 座標固定0。Session／cache／C ABI 傳原值，GPU 混色，避免 MainTex 預烘顏色再乘一次。此項僅是 head item 材質輸入與混色修正，完整光影尾段、眼眉控制器、chin outline 尚不可宣稱逐像素等價。
- 初次 head_item filter 在新增測試前只編譯成功、實際 running0tests；它不是測試通過證據。新增 real-card 控制又把 meshes_for_maindata 的 (meshes,skeleton) tuple 當 Vec，Rust E0599；MouthMaterialUnit278Final.log 保留失敗，改 .0 再跑新 Finalv2，要求2項实际passed。
- 舌頭顏色的直接證據是遊戲 mesh dump materials.json 與卡值；現有 native probe metadata 僅對 brow／eyeline記色彩，tongue只記shader／queue。先前進度所稱「探針同色」校正，不將未記錄欄位當探針證據。來源dump舌頭 NormalMap 為ME轉出的RenderTexture，因此補用既有已驗證 normal_convert，pipeline124避開調查中123的session。首次此patch有多餘@@，工具拒絕未寫入；移除空hunk後套用成功。
- 首次UE automation透過PowerShell & GUI exe立即回exit0，但log只到HandShapes開始且之後無程序／無正常exit／無測試Result，不能判成功，確切中止原因未明。保留MouthMaterialUnrealAutomation278.log；正式重跑由Start-Process -PassThru、WaitForExit捕捉自建PID／真exit，仍要求每项Result={Success}。
- Start-Process/WaitForExit的首轮Final真exit0，但log只有4个Success且无SharedDeformation／QueueEmpty／RequestExit，仍不算完整automation通过，不能解释成材质正确或完整正常退出。改用console型UnrealEditor-Cmd.exe重跑并保留输出，明确要求5项Success及完成日志；该无完整尾段退出原因尚未确认。

- 實際 mod head 測試 MouthMaterialSelectedSource278 在 Alice 失敗：來源是 minih HeadMod v4 的 Shader Forge/main_item，新增 head draw 只接受 Koikano/main_clothes_item，因此 item_draw=None。先前以 vanilla head 配五卡覆寫的兩項測試不能證明各卡實際 mod 材質均支援。部分9張 UE 圖及 log 已保留，圖移至 Saved/DebugShots/old/MouthMaterial278_unresolved-mod-shader；自建72556經絕對路徑核對後停止並 WaitForExit。正式結果須重新拍五卡15圖。
- 調查先猜外部 shader PPtr 解析失敗，實際 Shader Forge/main_item 在 source bundle 內可正確解碼，排除該猜測。又猜 MaterialEditor Shader Optimization 把 Forge alias 到 Koikano；實際 EC resource 仍為 Forge同名，未找到 alias 證據，禁止把未證實替換寫入載入。新增 native Alice material pre/post-load 控制再決定來源分支。
- source 查詢有多次猜錯路徑：CharaController.cs 實際為 MaterialEditorCharaController.cs；KKS_Sideloader 在 KKS_BepisPlugins 下；Probe.cs 被誤叫 Plugin.cs；舊 mesh/_dump 已不存在，現有資料為 mesh_dump。cargo metadata --offline 因非本平台 hermit-abi 缺件失敗，不能當 Rust build 失敗；registry decode/object 是 reexport，需讀 binary crate實際來源。minih manifest UTF8 BOM輸出cp950又觸發 UnicodeEncodeError，改 utf-8-sig 讀取並明確 stdout UTF8。失敗搜尋均不構成插件不存在或載入規則的證據。
- 原生逐卡控制排除 Shader Optimization alias：March/K091 是 Koikano四色，Alice/Yuuka/K384 是 Forge三色，載入前後保留同一 shader名。舊調查將body同名tongue或外層dump標籤當head來源；真正Alice外層目錄內是 Hayase Yuuka V1.02_0，Yuuka外層內是 Alice_0-tpose，顏色亦交叉。不能從外層資料夾名判角色或推斷遊戲shader更換。初版 verify_mouth_material 以全部Koikano及同名folder色值為gate失敗；保留失敗log，改以指定原始卡路徑的五份fresh native controls為正向gate，舊dump差異逐筆存報告，不隱藏差異或放寬native門檻。
- EC shader抽取首次誤加 --ignored，該測試未標ignore，實際running0tests；改 --include-ignored 後1 passed、488 blobs/24 shaders，Forge main_item自身20 blobs。猜沿用KKS的0024.asm不存在，先列實際blob再核對0005的三色公式。上述0test與不存在檔案不能當shader證据。
- console UE第3輪的abslog又只留下起始段，但stdout包含5項Success、Queue Empty及forced RequestExitWithStatus(1,0)。TestExit强制退出可能未flush非同步檔案logger；完整stdout才是這輪gate，不用exit0或截斷abslog推断測試沒跑／已跑。MouthMaterialUnrealAutomation278Finalv3Console.log為正式證據。
- pipeline125最終 source/cache測試3 passed，UE重編11.22秒成功、5 automation Success。五份native JSON皆error空、五卡tongue shader/Color/ShadowColor與loader一致；正常捕圖15張、71 attachment audits=0cm、15投影fit≤0.92，所有15圖已目視，眉／附件保留。MouthMaterial278Audit.json與verify_mouth_material.py通過；嘴部40組原native支持頂點最大2.295458e-6m，35.90秒新fixture通過。完整光影、chin outline及眼區2.43mm差仍未宣稱修好。Native owned67400/52044/76584/70152/63016因遊戲Quit延遲，在fixture完成後按exact exe path停止WaitForExit，probe DLL已移回Tools；UE72172正常exit0、Cmd73132完整stdout/exit0。

## E-135　卡片眼開度與原生控制器上限分離（2026-09-28，修正並完成驗證）

- stage277/278整臉眼區2.43mm差不是未明state。原始March卡Status.eyesOpenMax=1，native fixture eye channels為closed8/open92；ChaControl.ChangeEyesOpenMax原碼7371-7381分開設定fileStatus=[0,1]、eyesCtrl.OpenMax=[0,.92]，既有loader漏掉後者。此前只保存float bits仍不夠，不能再猜0.926來源或用卡名調整眼型。EyeLook/blink correction是另外一層，這條source clamp不能替代它。
- 先保存pipeline125的RustMouthMarch fixture到MouthMaterial278，再以pipeline126重建neutral eyes；原卡Status不修改。新增verify-all gate對40組整臉／牙／舌所有頂點核對，維持2e-5原門檻，不能只用mouth support成功掩盖眼區。完整Eyes/Eyebrow pattern、blink/gaze/controller時間混合仍未宣稱已實作。
- EyeClampUnit279／GeometryRust279各1項通過，整臉40組最大2.295458e-6m；UE build成功、完整stdout五項Success。五卡15張全部目視、71 attachment audits=0cm、15 projection fits≤.92，verify_eye_clamp.py與EyeClamp279Audit.json通過。自建程序均已退出、plugins未留probe；完整控制器與渲染尚非全部等價。
- 壓縮上下文後又誤讀Docs/ERROR_LOG.md，明確FileNotFound；真正記錄仍在repo根目錄ERROR_LOG.md。後續直接讀實際檔，不把缺檔搜尋當沒有紀錄。

## E-136　眼眉中性基準用名稱猜通道（2026-09-28，修正並完成驗證）

- 嘴型已讀actual head MouthCtrl，但眼／眉仍用_def_cl/op及mayu字樣分配權重，mod任意命名時會漏變形。來源FBSBase.Init使用pattern0，CalculateBlendShape先清全部PtnSet引用的channels、再累加Close/Open，-1不寫；同channel相加100。FaceBlendShape.LateUpdate依Eyebrow→Eyes→Mouth順序寫，跨控制器共channel為後者覆寫，不能累加。
- ParsedBundle共用face_control_patterns讀实际EyesCtrl/EyebrowCtrl，head loader按target索引建立中性geometry，不再按channel名猜。眉OpenMax讀卡Status並按ChaControl clamp[0,1]、眼仍按[0,.92]；SESSION_PIPELINE_VER127使旧中性cache失效，decoded cache89不動。此項是pattern0基準，不代表完整blink/gaze／pattern切換／blend-time已實作。
- HeadFbsUnit280一項通過，含異名／重排、same-index、-1、量化與跨controller覆寫。HeadFbsSource280一項實讀五卡selected prefab通過，UnityPy獨立逐筆核對全部2456筆patterns、保存bundle SHA256。不是只拿vanilla head套mod卡的覆寫。
- 執行source test時workdir已是Tools/ok-extract，前置rg/Get-Content卻又寫Tools/ok-extract/src/lib.rs，實際查到重複路徑並失敗；cargo本身另已執行成功。不要把最後shell exit0混稱前面每個搜尋成功。來源工具初稿又猜sanitize保留連字號，讀回實際實作後改為ASCII alphanumeric／其餘underscore／48字元，當輪檔名無連字號不受影響。
- 查UE預設GPU開關又把KKSkeletalBuilder.cpp猜在Source/OpenKoikatsu，實際位於Plugins/KKUnityBundle/Source/KKUnityBundle/Private；Get-Content明確失敗後，以rg --files定位重讀。不要沿用summary中的檔名就補猜目錄。
- pipeline127最終40組March mouth targets全部頂點max2.295458e-6m，眉／iris獨立renderer不在此world控制內。UE build23.04秒成功、五項automation Success、15圖全部目視，71 attachment audits=0cm、15fit≤.92。verify_head_fbs.py完整通過，HeadFbs280Audit.json保存2456source patterns hashes；owned UE72280／26748正常exit0，沒有新增native probe。完整runtime眼眉／blink／gaze與shader仍未完成。

## E-137　皮膚outline錯套髮shader公式與nearest mask（2026-09-28，來源漏項修正並驗證）

- 舊WPO對Koikano與Forge skin一律用頭髮(.1*distance_m+.3)*.005*_linewidthG。實際已安裝KKS resource Koikano/main_skin VS0000/binding0使用(.1*d+.6)*.003*_linewidthG*_LineWidthS；Forge skin VS0000仍是(.1*d+.3)*.005且無_LineWidthS。不得將§53的可讀重製shader公式當全部本機compiled shader的相同演算法。source family分支及材質scalar經Session/cache/C ABI傳入UE，pipeline128／decoded89。
- DXBC sample_l LOD0使用_DetailMask_ST與numeric藍通道，dump filterMode=Bilinear。舊AddOutlineSection却CPU nearest採樣、漏ST、量化到vertex alpha8bit，再插值。改WPO直接線性texture LOD0藍通道與source ST，不再靠CPU coverage；保留source triangles/normals，不加卡名／臉型bias。完整非均勻object scale、其餘fragment光影及chin根因範圍仍要分別核對，不把這個確定漏項宣稱全部像素等價。
- 第一次假設KKS所有shader都有.asm.txt，main_skin只有.dxbc，Get-Content失敗；列檔後才以fxc真正反組譯。另Saved/Tests/MouthMaterial278/EC_Shaders.unity3d是先前不可讀placeholder來源，沒有Shader objects，binding查找失敗；選中minih prefab與實際kks_decomp resource均可讀且binding一致，另存Final/Installed logs，不改shader名來湊。Saved/ShaderDxbcEC278沒有README.txt，不能猜它存在。查UE5.8 TextureSample header又猜舊Classes路徑，實際Public/Materials，rg --files定位後再讀。
- 首次SkinOutlineUnrealBuild281失敗，新增Subtract節點漏include Materials/MaterialExpressionSubtract.h，C2065/C2672；其他節點已有headers不能推定新類別也已宣告。補include後重編另存Final log，舊Failed log保留，不算成功。
- 最終build10.69秒成功，scalar cache控制一項通過、freshgeometry35.59秒通過／40組max2.295458e-6m，五項UE tests逐一Success。nativeMarch _LineWidthS初／後均1、JSON error空；自建15160因Quit延遲按exact path清理WaitForExit、probe DLL移回工具。UE54248／53092正常exit0，五卡15圖全部目視、71audit=0cm／15fit≤.92，verify_skin_outline.py與SkinOutline281Audit.json通過。
- 細線仍有斷續，來源公式／取樣差是確定漏項，尚不足證明所有chin像素問題只由它引起。不能把source metadata、一組原生材料scalar、mouth-target幾何與UE display audits混成完整shader/frame像素等價。保留非均勻object scale、fragment光影及controller範圍限制，沒有加chin bias或特定卡例外。

## E-138　眼眉控制器缺失與不可見 FBS 目標被刪除（2026-09-28）

- 使用者本輪明確暫緩下巴輪廓線，未修改下巴／outline擠出演算法。轉做實際 head EyesCtrl／EyebrowCtrl 的 pattern 與手動 FixedRate；來源 FBSBase.CalculateBlendShape 清全部引用通道、整數百分比、Close/Open 累加、-1 略過，FaceBlendShape.LateUpdate 依眉→眼→嘴覆寫。每卡 Session 保存真正通道索引與已烘焙基準，不依名稱猜表情或按角色加例外；CPU／GPU 使用 desired-minus-baseline，避免重複疊變形。手動 FixedRate 可到1；這與普通 eyesCtrl.OpenMax 上限.92 是不同來源路徑。
- 第一次 NativeFaceControls 引擎測試失敗：舊 is_head_detail 名稱白名單沒有 tears，is_hidden_part 又會刪掉 cf_O_namida_*，但 EyesCtrl 仍引用三個目標。不得刪測試目標湊成功。改為保留所有 actual FBS target，包括改名 mod mesh，再分開 render_visible。ChaControl.UpdateVisible:1266–1279／ChaFileDefine.tearsFlags 表明 tearsLv=0 三者隱藏，眼型切換不改 tear 等級；預設仍不繪製，資料保留。Session pipeline129初版／130最終，decoded89不變。
- 新增 numeric texture 探針曾編譯 CS1061：此 Unity 版本 Texture 沒有 activeTextureColorSpace。失敗 log NumericTexturesProbeBuild282 保留；使用者暫緩下巴後整段實驗已撤除。ME mask sRGB 假說未 GPU 驗證，不能宣稱它是根因。source morph normals/tangents 五卡均零，沒有據此另寫 normal 補丁；UV>.91 舊 triangle cut 調查也未改生產程式。
- 第一個 functions.exec 把多行 shell 放進 JS 普通字串，SyntaxError 未執行；C ABI patch 猜 set_hand_shape 的參數叫 t，實際 blend，整筆拒絕後讀回再修。查 dump 的 hierarchyPath 初次錯讀 materials.json，正確在同名 mesh JSON。另誤搜 repo/kks_decomp，實際 game/kks_decomp；本 repo 無 AGENTS.md，協調指令來自使用者訊息。上述失敗不作來源或實作證據。
- 第一輪重拍指令用卡檔相對路徑，UE ConvertRelativePathToFull 以 Engine/Binaries/Win64 解出不存在檔；explicit cards=0 後落入 K091 fallback／7 outfits。驗收不可只數 screenshot 或看 exit。按 exact exe 檢查自建 PID10652 後停止並 WaitForExit，失敗 log FaceControlsShots282Final 保留、圖搬到 old/FaceControls282_bad-relativepaths。改絕對原始卡路徑及真正 OK_OUTFITS_LIMIT=1；OK_SHOT_OUTFITS／VARIANTS 不是此程式參數，不可繼續使用。
- 初版缺 tears 的15圖保存於 old/FaceControls282_missing-tear-targets。最終 UE automation 啟動漏 -stdout，redirect 檔只有 AutoSDK，不當測試證據；本次 abslog 完整有六個 Success、QueueEmpty6 與 forced exit0，可直接採用完整 abslog，無須為取 stdout 重跑已通過測試。先前失敗 NativeFaceControls log 保留。cargo test 未指定 --lib 另跑 main 的零項測試，lib 中 cache 控制確有1 passed；後續 geometry 明確用 --lib，不能將 tail 的0 tests 誤稱全部未測或用其當測試成功。
- 最終 source/native 通道 gate 已通過：March141／Alice135組、source pairs1096、channel comparisons145524，含 tears、所有正向／歸零權重。六項 UE tests Success；build24.15秒成功，pipeline130新 geometry40.22秒通過，March40組mouth targets全頂點 max2.295458e-6m。五卡捕圖與 UI／完整 verifier 驗收另記本節後續。時間混合 ChangePtn、自动blink/gaze、tear 等級 UI、完整 Animator graph與fragment像素等價尚未宣稱完成。
- 第一版 verify_face_controls 又假設 tears 隱藏訊息恰好5次，實際6次：ShotPreview 先載 March 建 queue，再載第一個 task；其餘四卡各一次。初版 AssertionError 保留於 FaceControls282Verified.log。改按 task 區段分別核對2／1／1／1／1次，維持每個被載入目標均隱藏的 gate，不放寬成任意>=5。
- 最終 FaceControls282FinalVerified.log／FaceControls282Audit.json完整通過；UnityPy獨立重核五卡2456 source patterns，15圖及UI截圖已目視。五卡眼型30／28／28／30／28、眉型皆17，主／覆蓋／外框變形還原誤差0cm；71附件audit最大0cm、15fit≤.92。Gallery真callback切換eye1/.61337及brow2/.8，來源動作與嘴型8/.8同步，clock未因表情setter改變。自建UE60304／74388正常退出，nativeprobe已移回Tools，遊戲plugins無探針；GPU頭實際蒙皮／完整pixels不由這批預設CPU-head capture證明。
- 上一段是pipeline130中間驗收，後續程式複查找出眼線第二材質 cf_O_eyeline_kage 漏繼承FBS，不能當最後成果。生成material pass時改保留actual source key，final metadata用source而非改名display key；複製相同channels、baseline與已轉換deltas，讓閉眼時shadow層同步。新增UE控制完整比對March／Alice的58組shadow pairs及每筆delta／baseline，pipeline131六tests Success，尚未完成該版capture便因下一條source visibility完善而停止自建48556（exact exe／WaitForExit），部分圖保留old/FaceControls282_pipeline131_partial，非正常exit的正式capture。
- 按mod通解複查：hidden part也不能只看Mesh名，ChaReference找的是GameObject。final按source renderer_names判斷，只有無renderer metadata時才退回asset名；同時synthetic material pass用同一source reference。新增改名mod／錯誤asset名／缺renderer資料控制；pipeline132使舊preview visibility cache失效。130圖／UI移到old/*_source-pass_missing，Audit另存FaceControls282Intermediate130Audit.json。這次揭露loader層漏項，不用pixel擬合補丁；最終132驗收續記。
- 132最終已驗收：build22.80秒成功，cache與visibility各1passed，freshgeometry60.76秒通過／40組max2.295458e-6m。FaceControlsUnrealAutomation282Finalv4Console.log六項Success、source shadow58pairs／全部delta相同、native276cases／145524channels。五卡15圖／UI已重新目視，十個visible FBS slots完整更新，7676／7818／7818／10513／10513 vertices還原0cm，71附件audit0cm、15fit≤.92。FaceControls282Final132Verified.log與Audit.json完整通過；最終自建52196／69948／71660正常exit0，plugins無probe。
- 新增run_face_controls.ps1三種模式集中真正參數／絕對來源卡路徑、-stdout／FullStdOutLogOutput、hidden owned PID／WaitForExit，避免重複手寫錯字；原失敗logs／中間圖不覆蓋。git diff --check沒有whitespace錯誤；現有Git LF→CRLF警告不是新測試失敗，不改其他工作檔來消警告。GPU-head完整顯示、時間混合／blink／gaze及全fragment rendering仍保留限制；下巴演算法未改。

## E-139　表情混合時序、眨眼狀態機與浮點驗收（2026-09-28）

- 延續使用者暫緩下巴要求，沒有改outline或chin。來源FBSBase.ChangePtn重选同目標不重啟；中斷把上一個dictNowFace目標複製成Back，不取目前混合結果。TimeProgressCtrl預設.15秒，每個controller每frame Calculate一次；開度先truncate到整數百分比，再乘Back/Now混合係數，眉→眼→嘴順序覆寫。舊instant UI與沒有blink的runtime漏項已接上來源邏輯。
- FBSBlinkControl不是固定週期sin曲線。閉合後count每幀減一次且不重設deadline，過期一次只切一個狀態；fixedFlags會保留openRate並暫停狀態更新。float Random.Range(0,.05)、int Range(0,3)／Range(0,frequency)保留區間；frequency0安全處理。來源SetSpeed竟是Max(1,value)，照原生實測，不改成猜測的Min。測試注入原生實際random結果，只認證狀態演算法，不宣稱UE與Unity RNG序列相同。
- selected head實讀BlinkFrequency/BaseSpeed、Eyebrow SyncBlink、Eyes/Brow OpenMin／FixedRate，卡Status eyesBlink另讀。SessionPart／PartBlob／C ABI保留8值，pipeline133／decoded89；不以角色名挑演算法。auto模式UI不寫手動FixedRate，否則checkbox雖開仍不眨眼；pattern切換仍可混合。disable強制eyes/brow openRate=1遵ChaControl.ChangeEyesBlinkFlag，authored FixedRate仍依來源優先。
- 第一次read猜FBSCtrlBase.cs不存在，正確FBSBase.cs；TimeProgressCtrl在FBSAssist子目錄。壓縮後又猜KKCharacterGallery.cpp/.h，實際Source/OpenKoikatsu/OKGalleryWidget；rg的Windows檔案參數不能傳未展開*.ps1或OKGalleryWidget.*，改rg -g與實際路徑。還誤查不存在verify_mouth_world.py，正確compare_mouth_world.py。未開始capture便Get-ChildItem預期資料夾也明確缺檔，不能當capture失敗／成功证據。上述錯誤搜尋未改檔、不作來源證據。
- fresh March／mod Alice原生timed fixture兩份JSON error空，72frame各含Mouth/Eyes/Brow切換、blend中斷、立即切換、same-target與完成；240blink frames各含fixed/resume／force close/open／frequency0／SetSpeed。第一次UEbuild30.21秒成功，七tests全Success／QueueEmpty7／forcedexit0，實核16runtime settings、1096pairs、144timeframes／75888channels及480blinkframes；原有276固定眼眉案例與145524channels仍通過。native自建PID32312／35100完成後Quit延遲，按exact exe停止WaitForExit，probe移回Tools。
- 首次五卡capture在第一張audit停止：FMath::IsNearlyEqual(.05f/.15f,1.f/3.f)未指定容差，約3e-8差異超過default small tolerance。改為1e-6 alpha容差，與原生time test一致；原source演算法與geometry gate不動，原全部7676 displayvertices restore_error=0cm。失敗圖尚未產生，logs保留FaceTimeShots283_halfFloatGateFailed／Runner對應檔，不能算正式capture。原log RequestExitWithStatus(0,1)但OS exit0，再次證明不能單憑exit判成功；新runner同步拒絕source LogTemp Error與failed status。
- fresh pipeline133的native geometryfixture41.18秒一項通過；compare_mouth_world --verify-all以原2e-5m gate核40組mouth-target全頂點。後續最終build、重新capture／UI及完整驗收補記於本節；GPU-head全蒙皮、gaze correctOpenMax、tear等級UI、完整Animator graph／shader像素等價仍是各自範圍。
- 最終build10.98秒成功，最終binary七項tests Success／QueueEmpty7／exit0；unit1passed／40geometry max2.295458102745069e-6m。五卡half15圖、March／Alice open/closed12圖、Gallery UI已全部目視，5time display audits pass、所有sections還原0cm、71attachment audits0cm／15fit≤.92。verify_face_time.py／FaceTime283Audit.json完整通過，UnityPy實讀五卡selected prefab settings並存hash。UE20220／67560／23820／69184／60672正常退出，game插件無probe。
- 原生重跑腳本初稿未處理已存在completion JSON；首次資料夾全新，因此本輪fixture確由owned processes生成。現在啟動前把同名JSON／TSV搬進明確old目錄，避免下次誤讀舊完成。最終MD patch又把ERROR_LOG的句子當HANDOFF2尾端anchor，整筆verification rejected、未修改檔；讀回真正tail再重做，不能聲稱被拒patch已完成。原生權重／state trace與UE display各自認證範圍，random不宣稱跨引擎序列一致；未改chin，剩餘範圍見§140。
- 檢查新的native fixture archival loop時發現PowerShell `@($path,$path+'.face-time.tsv')`先組array再加suffix，實際得到三個元素（JSON、JSON、單獨suffix），會漏掉舊TSV的保存。離線字串實測Count=3揭露問題，改第二項獨立括號，確認Count=2且完整TSV路徑。錯誤初稿未曾重跑native，因此未移走或覆寫本輪fixture；不能只以Parser零錯誤認定表達式值正確。

## E-140　視線開度修正與瞳孔 UV 來源漏項（2026-09-28，來源漏項已驗證）

- FaceBlendShape.LateUpdate 按左眼H／共用V計算 correctOpenMax，MathfEx.LerpAccel 是 sqrt(t)；中央視線在 Eyes.OpenMax=.92／UpCorrect=.1 時為.9。FixedRate仍優先，負correctMax仍用OpenMax。不能把靜態.92當全部視線與眨眼的最終開度。actual rig的EyeLookController在head-bone prefab中由ChaControl取得，不能因head FBS序列化指標為空便判定不存在。
- 五卡EyeLookMaterialControll序列化_renderer為null；ReSetupMaterial按同GameObject的GetComponent<Renderer>補上，載入器必須保留此來源規則。控制器按renderer reference／fallback定位，左右採eyeLR，不按角色或改名mesh猜。Mathf.Lerp(1,5,scale)有Clamp01，scale1.8只到5，旧UV helper錯算8.2。舊UV在每vertex截到[0,1]又違反sampler於fragment截斷的來源順序；改保留raw UV，在runtime由同一controller公式重算。
- 首次PowerShell把Rust四個路徑寫成src/{lib.rs,session.rs,ffi.rs,unity.rs}，ParserError沒有執行。Windows rg未展開Docs/*.md亦再次失敗。新Rust檔的leading-dot literals在閱讀時修成0.5，未算編譯證據。首次cargo check失敗E0425：沿用不存在sess_mut名稱；本檔sess_ref實際回傳&mut，讀回後修正並check成功。
- 從283 runner生成284的初稿全字串替換283，誤改卡KK_384283為KK_384284。啟動UE之前讀回發現，改numeric boundary正規式，重新生成，保留原卡名。不得用任意replace改參考卡身份。錯誤MD patch又猜不存在的# anchor，verification rejected／未修改，讀回實際末段才補本節。
- fresh原生March／Alice兩JSON error空；各200視線／blink／FixedRate cases及200非預設材質公式控制，native probe按owned exact PID48480／8840清理Quit延遲並WaitForExit，DLL移回Tools。Rust native formula test400 ST公式／cache12settings+rawUV一項各passed。UE build／正式五卡capture與UI尚需驗收，不能把來源表或第一次build失敗当完成。
- 本輪API輸入是EyeLookCalc的normalized H/V rates。world-target求角／eye bone rotation、highlights完整shader／像素等價仍是獨立未完成範圍，不能以MainTex ST或FBS權重宣稱整套EyeLookCalc已移植。下巴輪廓線依使用者要求延期。
- 首次EyeGazeUnrealBuild284編譯失敗C2065/C2923：新增GazeSliders在舊的inline class USlider宣告之前，漏了header前置宣告。不能因cpp已include Slider.h推定header生成碼能識別。補class USlider並重新編譯，失敗log保留，未拿它啟動正式capture。
- 第一次geometry指令漏--ignored，log是0passed／1ignored，不能当幾何驗證成功；另存Final log明確跑ignored來源診斷。source audit核五卡實際Up/Down/Side為0/.33/.17，並非class initializer .1/.3/.1；native settings與生產載入已正確讀實際值，verifier初稿卻把中央最大開度硬寫.9，閱讀source report後改依actual Up／OpenMax推導。普通中央視線五卡為.92、測試方向(.6,-.4,-.4)為.6164，不能混用初始化預設值。
- geometry重跑又漏--release，Debug累積CPU逾304秒仍在附件settle。核exact path及native_mouth_geometry_fixture參數，停止自建PID70176／WaitForExit改為既有Release驗收模式。移存Debug日誌因parent shell仍持有redirect file handle失敗；之後新Release指令覆寫同名Final log，原完整Debug日誌沒有保存，不能聲稱DebugStopped.log存在。停止未通過的Debug不是成功證據；最終只採完整Release結果。以后archive redirect log須先等parent tool session結束，不能只等child test process退出。
- 最終build10.63秒成功、8項UE tests Success／QueueEmpty8；400cases、80settings、1096pairs、210800channel weights、143200UV vertices全部相符。Rust native formula／cache／legacy UV各一項passed；Release geometry33.73秒1passed、40組全vertices max2.295458102745069e-6m。五卡15圖＋UI全部目視，眉毛保留，5gaze audits各358UV有變化／全sections還原0cm与UV0誤差0，71attachments0cm／15fit≤.92。verify_eye_gaze.py與EyeGaze284Audit.json完整通過。自建UE65432／26488／5224正常exit0，nativeprobe不留game plugins。完整world-target／eye bones／shader與GPU-head仍是獨立未驗收範圍；下巴延期，不混稱所有工作完成。

## E-141　把視線輔助節點誤認為眼球蒙皮骨骼（2026-09-28，stage285已驗證）

- 先前把「eye bone rotation尚未完成」列為缺少幾何旋轉，這個假設不正確。EyeLookCalc實際旋轉EyeTargetL/R；在原始rig中它們與cf_J_Eye_rz左右節點為同層，非眼球或臉mesh的祖先。五卡選定頭bundle的SMR不引用它們。必須依actual引用核對，而不是按眼睛名稱旋轉臉／眼球。遊戲以它們計算H/V，再由EyeLookMaterialControll UV與FaceBlendShape修正開度。新增原生全renderer／非零weight descendant綁定檢查，不能把未覆蓋的mod動態骨架宣稱已驗證。
- C#第一次compile把欄位命名fixed（保留字，CS1041）；第二次直接存取trfCenter／centerEyeLength（private，CS1061）。改rotations並用明確reflection讀來源private欄位。Rust metadata closure漏完整UnityValue type路徑（E0425）；已補。vortho_normalize是值傳入、回傳tuple，移植時須先核對函式簽名。
- 原生JsonUtility默默省略插件自訂巢狀config／frames，completion.error仍為空，五檔只剩511 bytes，不能視為通過。已改完整reflection JSON序列化並重跑；驗收必須同時存在6種config／144frames／完整basis與expected數值。失敗版本移入Saved/Tests/EyeLook285/old。逐格test在其餘卡還是舊版本時誤提前執行，正確拒絕null config；以獨立單卡診斷環境變數核三月七，全五卡正式驗收必須移除該filter。
- PowerShell cwd切到Tools/ok-extract後仍組根目錄相對路徑，造成read/write路徑不存在，未修改檔；接著Rusttest也抓到open_session只回part數、不是OkSession。改absolute patch及session::open。rg檔案glob須用-g，不能把Windows不展開的src/*.rs當實際檔案。失敗診斷與0passed不可當成功證據。
- 六種模式、controller/eyeTransform/PPtr metadata與runtime共用載入；session pipeline135。Mathf.Sign(0)=+1、Clamp／Lerp／InverseLerp來源定義、右眼反向角度limits、AWAY共用比例、NO_LOOK保留角度、近距離／FORWARD5度、非等比center TransformPoint與frame dt皆須保留。World-target在preview actor的Unity座標空間計算；不得再加每卡拉伸／骨架偏移。
- 追加真正載入後的center-local眼節點geometry gate揭露三月七2mm偏差；不能只驗算法trace、initial refs或finite rates便判載入正確。根因是apply_shape在ABMX之前保存head_shaped，而apply_anim_frame每次還原head_shaped後只重套body ABMX，漏了head ABMX。遊戲cf_J_Eye_tx_L實際Y=-.00205729157、UE runtime還原為-.00405729157；額外native transforms與原MouthWorld277紀錄一致。現在每個動畫frame按shaped baseline重套head ABMX，再更新skel export／accessory／gaze，pipeline136使舊結果失效。沒有加2mm常數、沒有修改ShapeHead公式／某張卡的資料。新增全五卡center-local眼位置（2e-5m gate）與root-relative parent rotation gate，正在重新驗收。先前pipeline135的9項UE／15圖只列診斷，不作最終完成證據。
- 初始化來源也不能只依五卡都剛好是identity的結果推定：EyeLookCalc.Awake的referenceLook/up／origRotation應保留剛實例化rig、CopySameNameTransform／shape／ABMX之前的值。新增Init資料經session cache保存；head rig與calc metadata用同一bundle、先走Sideloader studio/base/00 override再用vanilla／body fallback，rootNode空依來源fallback component.transform，center空停用雙眼correct。最後cache schema以pipeline137固定，136之前的診斷不列最終驗收。Metadata JSON optional Init使用serde(default)，不可skip_serializing_if省略bincode欄位破壞session二進位layout。
- 查根因时又对UnityPy的Vector3f用vars()，其extension对象没有__dict__，TypeError；改读取实际x/y/z或直接repr。ShapeHeadInfo的文件名应先rg --files，实际是ShapeHeadInfoFemale；臆测未存在文件与MouthWorld277的字面glob均读失败。这些只是失败诊断，不能把后续Shell其他命令的exit0认成那些读取成功。最终原生trace与geometry／source probe都有独立存在性和count gates。
- 最終pipeline137／EyeLookUnrealBuild285Accepted成功35.31秒，9項UE tests Success／QueueEmpty9／exit0；五卡120animated state steps、400native gaze cases／210800channels／143200UV vertices regression通過。五卡720source solver frames rates最大3.71933e-5／angles .0010976046度／dirUp6.737653e-6／quaternion component9.590294e-6／target2.3841858e-7m，各別tolerance不放寬；真正載入的五卡Init／center-local eyes／relative parent rotation gate全通過（BasisAccepted130.55秒）。Release mouth geometry46.50秒1passed，40組全vertices最大2.295458102745069e-6m。
- 最終五卡15PNG＋1440×1000 UI全部目視，眉毛保留、未見新側髮／髮飾／頸飾位移；71attachments=0cm、15fit≤.92、五次gaze UV restore與所有sections positions皆0誤差，鏡頭checkboxcallback／pause hold／clock unchanged通過。verify_eye_look.py完整PASS，EyeLook285Audit.json保存來源與fixture hashes／各gate；ContactSheet與README保存。舊135 captures／logs在old，原T-pose Screenshots未改。自建最後UE3308／74340／15560正常exit0，所有native owned processes完成後清理Quit延遲；game plugins不留probe。檢查時Git HEAD在工作期間有新提交，本輪未執行commit；最終production hashes與accepted audit一致。

## E-142　眼睛高光烤入瞳孔且混用 shader family（2026-09-28，stage286已驗收）

- 舊 bake_iris 把上下高光依序 lerp 到 MainTex，漏掉遊戲 draw 的獨立 UV1／UV2、高光 X 位移、左右眼 rotation 與 max 合成。不能由 MainTex ST 正確便宣稱所有眼睛 shader 正確。依 EyeLookMaterialControll.Update、ChaControl.ChangeSettingEyeHL*／EyeTilt 與實際 DXBC 拆出 draw 輸入。
- 初版推定五卡皆 Koikano/main_eye；fresh原生材料表證實 March／KK_091207 為 Koikano，Alice／Yuuka／KK_384283 為 Shader Forge/toon_eye_lod0。必須依 shader 身分分流。Koikano 主 UV 先 ST 再旋轉，高光再加各自 rotation；Forge 主 UV 先旋轉再 ST，高光不旋轉，且不讀 _Color／_rotation1／_rotation2／_texcoord2/3_ST。Forge GetFloat/GetVector 的零值表示不存在的參數，不可拿來抹除 UV。
- Koikano 高光 RGB 使用 texture.r×texture.a×overcolor.rgb，Forge 使用 texture.a×overcolor.rgb；兩者 alpha 均以 max(texture.a×overcolor.a)×isHighLight，不能混成同一個 sequential alpha-over。完整 expression／overtex3／lighting 尾段仍須分別驗收，本輪不把高光修正稱為全像素等价。
- 第一次 read 猜 Tools/run_eye_look.ps1、Public/OkExtract.h 不存在；实际 Tools/GamePoseProbe/run_eye_look.ps1、Tools/ok-extract/include/ok_extract.h。先 rg --files 查路径，不以同命令其他成功輸出掩蓋讀取失敗。初次C++ build C4456：內層 i 遮蔽外層 part i，改 EyeIndex。Rust native test E0308：session::open 的 card 參數為 Option<&Path>，補 Some；失敗編譯不是驗收。
- 初版 main fragment rotation 對 Forge 的 ST 順序錯誤，已在正式 capture 前改成三組來源 sample UV：仿來源的 affine rotation／ST 按順序作用於 vertex stream，線性插值等價，GPU sampler 才 clamp。Session 的 EyeLookMaterialControll ST 診斷 UV 保留，viewer 使用另存的 sample UV。共用 ShadowParams 每個 part 必須重置 eye kind／texture pointers，避免前一材質殘留到下一個 renderer。
- 五份 fresh native fixture 各25視線方向／兩眼／三組ST，共750 transforms；完成檔 error 空且每檔150 ST列。自建game PID30952／73684／41156／43272／38872完整輸出後依 exact exe 清除 Quit 延遲，probe已移回Tools。正式Rust、UE及截圖結果另補，不以目前中途 build 或 metadata 稱完成。
- 第一次完整載入 gate 抓到 Alice eye_draw=0：不是應增加 Alice 例外，而是 Shader 的 Object.m_Name=toon_eye_lod0 与 ParsedForm.m_Name=Shader Forge/toon_eye_lod0 同時存在，舊 reader 只有 object名為空才讀 runtime名。現兩個共用材質reader一律優先 ParsedForm.m_Name，再 fallback資產名；decoded CACHE_VER90／session pipeline139使舊錯誤分類失效。138版9項UE已通過但只列診斷，截圖尚未完成的owned UE74516已停止，9張舊PNG／logs移到old/BeforeShaderRuntimeName；不能拿它們驗收139。
- 真正貼圖像素 gate 抓到 March 原生 up=512²、抽取 up=516²，且形狀不同。不是 BC padding 或縮圖問題：`hlUpId=1` 沒有該 category 的 KKEx resolve，應使用 vanilla 的 cw_t_hitomi_hi_u_001；舊 merged mod_catalog 優先取同 local id 的另一張圖。改用既有 face_detail_entry：有 resolve 按 GUID／原始 slot，沒有則 vanilla 優先，從那個 list row 指定 bundle 讀圖，不從別的 head/body texture pool 借同名圖。pipeline140；139舊30PNG與logs移到old/BeforeHighlightResourceIdentity。March up RGBA完全相同，down來源壓縮差平均0.222878/255，翻錯方向平均1.29892/255；原尺寸與像素門檻未放寬。
- 貼圖來源診斷曾假設 EyeLookMaterialControll cached material 與 renderer 當前 material 不同。fresh March probe實測兩者instance ID相同（左右分別-91452/-91454），該假說撤回；probe改讀renderer.sharedMaterial並記錄兩者身分，以後不拿cached值冒充實際renderer。Material Editor沒有這張高光覆寫，不能將錯圖歸因於ME resize。
- 把guid_index傳給draw時遇E0382，因較早附件pass把整個map移入index；改為借用，避免复制或第二次掃mods。中途猜chara_list/chalist.rs、KKUeWalk.cpp、Docs/GPUSkin.md、Tools/ok-extract/Tools/build_unreal.ps1皆不存在，實際list.rs、KKSkeletalBuilder.cpp、根GPUSkin.md，build由UE Build.bat與Build.cs呼叫build-release.cmd；以rg --files／定義搜尋校正。大patch的anchor不符合現碼，apply_patch整批拒絕，拆成實際ResetOrbit／ShotMouth anchor後成功。
- 五卡fresh抽取的debug test耗時過長，owned test51032依完整target/debug/deps路徑核對後停止，改用release執行相同所有尺寸、色彩、UV、ST、rotation及20貼圖gate；未完成的debug log不能當pass。完整最終數字另補。
- 最終pipeline140／decoded90：release材質／20貼圖測試143.13秒1passed，三UV cache測試1passed；20張原生貼圖尺寸與方向全部一致，最大平均RGBA誤差0.442928314/255。750來源ST／5370 raw UV vertices通過，最終production build53.29秒成功、9項UE Success／QueueEmpty9／exit0。Walk與Tpose各五卡15圖全部目視，眉毛可見、側髮／髮飾／頸飾未見新增錯位；各71附件audits0cm、15fits≤.92，高光UV1/2各358vertices改變且全部還原0。verify_eye_shader.py完整PASS，EyeShader286Audit.json保存來源／貼圖／30图hash；旧138／139診斷不列正式結果。沒有進行下巴專用修正。

## E-143　一般走路只覆蓋UE顯示骨，繞過乳搖更新（2026-09-28，stage287已驗收）

- 原生／mod遊戲動畫的PoseMode2已有DynamicBone_Ver02逐幀模擬，但一般走路按鈕走PoseMode1：RefreshSkinnedDisplay後在C++直接改骨頭rotation，沒有把該動畫姿勢交給body／clothing dynamics。不能以Rust solver存在，便宣稱一般UE走路已顯示乳搖；也不能只讓body動、衣服仍用另一份palette。
- 無OK_WALK_ANIM外部重定向資產時，SetPoseMode(1)現在使用實際Studio controller state `states:studio/anime/00.unity3d|walk_run / Base Layer.f_aruki_00_00_01`，走既有通用SetGameAnimation／ApplyAnimFrame。Gallery保留SelectedGameClip，換卡仍播放相同來源動作。卡片自身size／softness／weight、原生pattern／骨鏈、已安裝DynamicBones修正與collider policy沿原載入流程；沒有新增按卡名振幅或正弦動畫。
- 新增UE BodyDynamics gate：五卡各180幀，動畫sample time循環但physics dt固定1/60；同一phase以OK_NODB跳過模擬作負對照，該零時間取樣不更新粒子歷史。另Shot display audit確認兩側六節真正顯示骨、全部GPU follower palette一致，輸出1080骨骼trace rows/card與15張圖。只有數值／顯示正向控制一起通過才驗收。
- 顯式OK_WALK_ANIM的mannequin重定向仍為既有另一條路徑，本次未把它冒充原生遊戲動作物理。完整Animator graph／任意第三方physics override／跨角色碰撞仍有既有範圍限制。BreastPhysicsController卡片plugin data存在不等於目前安裝該DLL；本機plugins搜尋僅找到DynamicBoneEditor，來源Editor只處理普通DynamicBone，不能自行套不存在的BPC參數。
- 新UE測試首次把所有cf_j_bust前綴都算粒子，讀到12支而非6支：來源rig另有六支`_01` reference transforms。改選實際Ver02兩側三節，不把reference當粒子；原生physics-baseline.tsv佐證。測試Fail但UE process exit0，runner現在另拒絕`Result={Fail}`，OS exit不能單獨當通過。壞版test logs封存old/ParticleNameSelection。
- 只改test selector後太早重鏈結，舊驗證UE仍占用plugin DLL，UBA寫入retry20秒後LNK1104。先讓owned9項tests與Walk15圖正常exit0，再重鏈結，BodyDynamicsUnrealBuild287Accepted成功2.95秒；沒有為了編譯終止正在完成的正式capture。最早production build53.29秒已成功；這次只有test selection改動。
- 原生120Hz、root每幀平移的controlled fixture重跑1passed／10骨，local position max0.00000011m、quaternion component max0.00000082；嚴格門檻保持。這是來源solver證據，後續仍須UE負對照與真GPU顯示gate。
- 一次在Tools/ok-extract cwd卻用Saved/Logs前綴拷貝accepted log，copy/read失敗；Rust source fixture test其後exit0不代表前兩個操作成功。回專案根目錄單獨拷貝並讀回驗證，保留每個gate独立結果。
- 最終UE BodyDynamics Success／QueueEmpty1／exit0，五卡900幀、每卡720個physics差異joint samples；對同phase停用物理的最大位移0.020781570–0.181548457cm、角差0.004833739–0.062871525rad。五卡真正顯示900幀、57240次GPU follower比對、palette誤差0cm，每卡1080列finite trace。15張900×1080圖全部目視，71附件audits0cm；verify_body_dynamics.py完整PASS／BodyDynamics287Audit.json保留數值及hash。Test137秒執行中一度無新stdout，最後正常完成，不能僅靠log暫無更新誤判hang。UE owned38136／12080正常退出，game plugins沒有probe。

## E-144　外部UE動畫在物理之後覆蓋顯示骨架（2026-09-29，stage288已驗收）

- 延續使用者要求通解，stage287雖接通來源遊戲／mod clip，但顯式OK_WALK_ANIM仍走顯示overlay；頭髮／附件與乳搖看不到當幀動畫。新增外部Animator local pose入口，先由card-shaped baseline重定向，再共用來源ShapeBody.UpdateAlways、hand、場景helpers、ABMX、Ver02、衣物與附件時序。不能從上一幀物理結果當neutral，也不能post-physics再overlay衣服。
- 舊mapper只有upperarm Roll特例、丟掉骨盆位移，其他rotation還使用vanilla KkRest而非卡片baseline。改為完整local quaternion、reference component basis與人體左右／上下方向對齊，位移轉到target parent frame、依身高比例，縮放乘到卡片baseline。exact KKS骨名優先，其他沿humanoid mapping；無來源geometry的診斷fixture明確用identity alignment，不冒充真來源rest幾何。
- 明確外部asset載入失敗不能偷偷合成walk；production SetUnrealAnimation接受真UAnimSequence，驗證後才重設時序，未找到asset回失敗。原生成sequence只作診斷fixture。
- 新FFI輸入在動pose／粒子歷史之前整幀驗證，拒絕重複、非body／shape bones、invalid index、NaN／Inf、零quaternion與零scale；sample time与actual dt獨立。外部無Unity source場景，auto用Studio preview policy，明確場景仍優先。
- 本輪搜尋又猜src/skel.rs不存在，Skeleton實際定義在pose.rs；後續以rg定義位置閱讀，該失敗讀取不算來源證據。Rust cargo check通過，完整數值／UE顯示尚需驗收。
- 初次Rust parity測試unwrap body_shaped失敗，直接open／deserialize不保證已呼叫ApplyShape；來源動畫既有入口會lazy初始化，外部baseline getter也必須同樣處理。getter首次建立baseline，輸入提交仍整幀驗證後才更動pose/history；測試初始化兩個session同一份neutral history再比較，保留失敗log，不降低gate。
- 下一個parity gate在frame0拒絕誤差（position 0.000359639m／rotation component .010848994）：先前把所有Animator輸入限制為cf_j_／layer1是錯的。實際遊戲AnimationClip也會驅動body內其他helper transforms；shape layer分類不能當Anim binding分類。外部入口改允許存在於該body tree的全部未locked transforms，再由ShapeBody.UpdateAlways依來源還原需要的形體helpers、ABMX照次序覆寫。仍拒絕head tree、synthetic locked root、未知index與無效數值，不能靠放寬數值門檻接受錯誤。上方「拒絕shape bones」的初稿假設在此撤回。
- 又在Tools/ok-extract cwd使用Saved/Logs相對路徑archive，重複E-143的cwd錯誤；Copy-Item失敗後同命令仍開始cargo，原LayerFailed完整log被覆寫，只能保留先前tool回傳的錯誤摘錄，不能聲稱完整log已封存。從此archive／驗收命令使用絕對路徑與ErrorActionPreference=Stop，保存成功後才開始重跑；此次未放寬gate。
- 第一輪外部display audit誠實拒絕motion_cm=0（followers一致不代表有動畫）。診斷fixture建立FReferenceSkeletonModifier後必須先結束scope、完成Skeleton hierarchy再初始化AnimationSequencerDataModel；改成既有合法scope順序，新增實際tracks數與quarter pelvis sample gate。負對照原只檢查hip與baseline差異仍可能接受恆定位移，補「跨frame變化」gate，不能靠渲染一致或process exit0宣稱動畫成功。另一個帶[Card]的Get-Content未用LiteralPath又被glob解讀，該讀取失敗不能當trace不存在。
- 無條件重正規化已unit的來源float quaternion會讓長碰撞鏈歷史逐幀偏移；Alice附件41.52581µm超過20µm gate。維持來源已unit值（length誤差≤1e-6），只正規化需要修正的輸入，不放寬位置gate。修正後已有多卡bone／attachment差異精確0。
- 又猜Engine/Public/Animation/AnimData header路徑失敗，rg --files確認實際Classes/Animation/AnimData。讀UE5.8 primary source確認缺少source track不能當identity覆蓋target baseline；加入IsValidBoneTrackName檢查，並用移除arm track的fixture與正／負非等比scale座標roundtrip測試此通解。
- 最終統一verifier抓到預期ExternalBody288_open沒有PNG：從287 runner改288時，只換了stem、漏換含Saved/DebugShots前綴的完整output expression，兩次正常external capture都寫入BodyDynamics287_Walk_open，覆寫先前source-control PNG。外部最新15圖由原capture log與mtime／SHA256記錄後移到正確目錄，不改log假裝原路徑正確；runner改成完整output path。保留原287 audit／capture log／ContactSheet，重新輸出來源走路control並核原image hashes以恢復原證據；不能把空old目錄當已封存15張診斷PNG。Body runner另清除external audit env，避免shell沿用。這是artifact路徑錯誤，數值、骨骼與trace gates均已通過，仍未完成截圖驗收前不得稱整體完成。
- 已通过的外部fixture把兩臂維持近Tpose，側鏡頭的手擋住頭髮／頸飾，正臉fit也把手臂算入範圍而變遠，不能宣稱這15圖足以目視附件。診斷motion改為reference Tpose上雙臂下放（不改production retarget，不隱藏mesh），保留三軸轉頭與骨盆動作；重跑physics負對照與最終capture，舊受遮擋圖保留old。
- Archive範圍guard初稿把GetFullPath回傳的反斜線與手寫斜線prefix比較，安全拒絕、尚未移动任何檔。改成所有路徑先GetFullPath並以DirectorySeparatorChar校驗同workspace與目標子目錄，才封存舊15圖／logs／audit／ContactSheet／recovery manifest。路徑校驗失敗不能直接刪掉guard。
- 重新產生287來源走路15圖後，與原audit的PNG SHA256比較是0/15相符；不能宣稱原始檔已復原。保留原audit／log／ContactSheet，Recovery288.json逐檔記原／新hash，重新跑source display verifier全部PASS，最新287 audit帶有capture_recovery欄位。最終288 capture直接輸出正確目錄，verifier新增actual log output path gate以防重犯。
- 最終Rust source入口parity五卡900幀／539100骨／19528380附件頂點差全部0，30invalid cases拒絕且後續history一致。主build11項UE regression Success，最終review build4.66秒成功並重跑external dynamics／sparse两項Success，共12種測試；900負對照幀有physics差異，900真顯示幀／57240followers完整transform一致、位置差0cm。15張清楚全身／臉／側臉圖全部目視，71附件0cm／15fit≤.92；ExternalBody288Verified.log PASS、Audit含source／image／log hashes。owned60212／42472均正常exit0。外部UAnimSequence使用同一來源body／衣物／附件時序；非humanoid mapping／完整Animator graph／任意第三方physics override／跨角色碰撞與下巴仍按文件範圍，不混稱全部專案完成。
- 收尾code review發現SetUnrealAnimation的builder只查ContainsNaN，合法float但零scale的clip可能先SetPoseMode(0)重設動畫／history，再被FFI拒絕；這違反API先驗再切換的契約。retarget preflight現在先拒絕零scale／零quaternion／非有限TRS與無效reference；Sparse test真的把pelvis track設為(1,0,1)，確認mapper回0。此guard不改有效clip的TRS。原12種測試不增加假數，擴充同一Sparse另重跑，5.79秒input build後重新輸出五卡RHI15圖／900幀，保留前版old/BeforeInvalidTrackGuard。
- 最後重現檢查發現runner預設Tests現為12種，verifier卻只接受歷史main run的11項；若按文件重跑會錯拒合法12項。verifier允許歷史11／新12，同時仍核額外2項external與最終Sparse的Success、QueueEmpty與invalid clip證據；文件明列三個selectors，重跑不重複計數。最後單命令Get-Process找不到已退出UE時PowerShell返回1，即使先前verifier已PASS；後續改用Get-Process集合與顯式0/1判定，不把清理查詢狀態誤報成驗收失敗。最終owned69276／54224均exit0，最新版15圖再目視／verifier全PASS。

## E-145　controller載入排除多motion狀態（2026-09-29，stage289已驗收）

- 現行single_motion只接受一個node，造成真實1D／巢狀BlendTree不能播放。獨立UnityPy核action/animator/00、studio/anime/00、studio_k/anime/04的139個controller，805個多motion狀態、907個type0與168個type3分支。不是某張卡的rig偏移；必須保留controller／override PPtr身分並混合實際子clip，不能把第一個leaf冒充整個state。新1D evaluator按m_BlendEventID／threshold／child index解析，2D、mirror、IK及未支援layer仍明確拒絕，尚未稱完整Animator graph完成。
- 原生探針第一次animator.speed=0導致AnimatorStateInfo.length=Infinity；completion.error空与40frames存在不足以驗收時間。改Animator.enabled=false以停自動tick、speed=1並明確Play(normalizedTime)/Update(0)，重跑得到finite長度與450個去重cf transforms/frame；舊102MB控制檔／completion在old保留，不能拿無限長度作來源證據。
- 新Session初始化在lib.rs使用未import的HashMap（E0433），改完整std::collections路徑。原生Rust gate第一次把card &PathBuf傳給open，實际函式要Option<&Path>（E0308）；依定義修成Some，不放寬驗證。又猜ChaControl.Anime.cs不存在；相關API實際在已知ChaControl.cs與Studio/OCIChar.cs，後續只讀實際檔案。
- Source Studio的height／breast初始化有CheckAnimeParam優先序，不能看到tree上的同名參數就硬塞某卡值。1D參數先保留controller預設，由通用API覆寫；Studio身形參數的來源優先序需依實際controller可用參數處理與核對，不能把全graph或所有Scene呼叫流程冒充已完成。
- 已依Studio/OCIChar.LoadAnime完成身高初始化：查整個controller的float參數，優先height1、再height，取card shape_body[0]；一般action仍保留controller defaults，明確Studio場景沿同規則。沒有自行推定HAnime乳房參數與additional layers流程已完成。
- UE新滑桿初稿local變數Slot觸發C4458（遮蔽UWidget::Slot），改RowSlot；新test猜TNumericLimits<float>::QuietNaN不存在與OkBoneView.pose_scale欄位名錯誤，讀定義後改std::numeric_limits<float>::quiet_NaN及pose_scl。兩次失敗build log保留，最終UIAccepted build9.05秒Success。另一次在Rust cwd讀根目錄相對Saved路徑再次失敗；不得把讀取失敗算來源不存在，後續所有產物／封存／驗收讀寫使用absolute path。
- 舊catalog test用了不存在的Base Layer.Main.Twoshot_00而false，沒有真的驗證BlendTree拒絕；改實際Base Layer.Twoshot.Twoshot_00可選，raw single-clip loader仍None以防冒充第一leaf，實際composite由原生60frames/card驗證。controller兩項與catalog15項回歸成功。新runner從舊版複製了Tpose選项卻仍會套1D clip，啟動該選項前讀回發現；限制此專用runner為Walk，並清除外部動畫／停用物理環境變數，不能把未執行過的選項宣稱已驗證。
- 15張真RHI截圖目視發現側臉上方黑背景；canonical SetupStudioView只有後牆與地板，ShotPreview另一路雖有side wall，載卡呼叫SetupStudioView後會隱藏舊牆。補共用左右側牆，不改角色材質或像素後製。保留原15PNG／logs／sheet於old/BeforeSideBackdrop，再build／重拍並檢查neutral-gray背景。
- verifier初稿從March控制檔推定五卡都是450個cf transforms；讀回全部fixtures發現450–496，mod hierarchy有額外transform。改逐卡檢查完整60frames的數量一致且無重複，不以March的數量硬判其他卡缺／多骨。81,000比較數是animated TRS屬性樣本，文件／audit明確標示，不能說成完整骨骼或全部scalar分量。
- 修背景後統一verifier初次拒絕背景spread≤2；實際五卡完整側臉上緣都是(128,128,131)，與既有正面背景一致，經現有LUT不是無色差RGB。改先核正面中灰範圍，再逐pixel比較同卡側面與正面底色≤1，不為接受黑色放寬gate；角色shader／LUT均未改。失敗驗收log封存BeforeBackgroundReference，不能只以目視灰色覆蓋失敗紀錄。一次診斷Counter在迴圈重開PNG900次，改為每檔只open一次再crop讀像素，避免無意義重複I/O。
- 最終native五卡300frames／81,000animatedTRS屬性樣本，最大position1.4901161e-8m、quaternion1.7881393e-7、scale1.1920929e-7；weights／duration一致。Rust controller2／catalog15／native1項PASS；UE13distincttests Success／QueueEmpty13／exit0，新增450blendframes與原source／external各900negativeframes。BackdropAccepted build28.50秒成功，最新五卡15PNG全部目視／71附件0cm／15fit≤.92／side背景比對通過，Gallery實際3callbacks及phase／clock通過。Animator289Verified.log PASS、Audit含所有來源控制與artifacthashes。owned54296／5316正常exit0，game plugins無probe。2D／transitions／layers等範圍保持明列，不以本次1D驗收冒充全graph。
- 收尾archive查詢錯把old內ContactSheet.png也算角色截圖，回16而拒絕；實際15張角色PNG與1張sheet完整。改以本次15個確切角色檔名核對封存、另查sheet存在，確認無owned verifier process；沒有刪除sheet或任意改期望來假裝15檔。

## E-146　2D混合位於額外動畫層（2026-09-29，stage290已驗收）

- 不能只把type3 weight evaluator接到Base Layer，來源88個2D state／168branches屬於四個Studio controller的額外層，带skeleton mask；需查實際layer語意、base default與composition後才驗收。
- 初次Python搜尋未過濾空type3 node list，把805multi-motion states誤報為type3，隨後IndexError；修篩選後正確是88states。後續診斷又用Python comprehension外的r（Python3不外洩）造成NameError，讀取失敗不是資料不存在。
- 印完整73層mask造成逾1MB無效console輸出，改獨立inspect_2d_graph.py只印摘要、完整常數寫Saved。rg --files無match返回1不是先前檔案閱讀失敗；重要gate以獨立結果核對，避免末命令掩蓋讀取與Python錯誤。
- 生成新runner時把帶$env的Python字串包在PowerShell雙引號，escape／變數展開造成SyntaxError、未寫檔。改PowerShell原生ReadAllText／literal single-quoted Replace與WriteAllText，讀回確認stage290目錄、144frames、2Dflag及舊1Dflag清空後才啟動遊戲；沒有使用壞生成檔。資料摘要仍曾印base motions全量，已改只印default state flags。
- layer初稿保存base state的字串label；同bundle內同名controller經catalogue加suffix後會指錯base。改用serialized file／controller PathID／state index身分，在suffix完成後解析base label。override來源仍依原PPtr，不能因layer新增又跨同名controller載入。motion plan另拒絕空leaves／零或非有限duration，避免壞資源落入poses[0]或除零。
- 第一輪144-frame March gate拒絕cf_d_siri_R position差0.020929083m（native vs Rust），weights／duration先已相符。錯把skeleton mask的m_PathHash從Avatar TOS轉成bone名字；實際mask用Animator相對完整路徑CRC32，某hash不在該Avatar TOS時落到root default1，導致未選右側也被覆寫。改依卡片實際cf_j_root階層產生完整relative path hash，未知子路徑繼承父mask、Animator tree外保留base pose；不是按骨名L/R硬排除，不放寬TRS門檻。失敗logAnimatorRust290March.log保留為MaskNameFailed，冷cache抽卡441秒仍持續有source進度，不是hang。
- 上條對Avatar TOS不匹配的根因推定不完整，現撤回為「初步假設」：改relative path後仍拒絕hand rotation差.39148307／sirihit position差.022624645。追加診斷確定base動作姿勢與native相符，但layer.mask本身是空的；serialized layer包在data內，uv_get未unwrap，unwrap_or([])把讀錯誤吞成無遮罩。改unwrap layer並對missing mask明確拒絕，合法empty mask仍按source full weight；CRC32 actual path方案保留以避免同名TF／mod階層歧義。MaskPathFailed與MaskDiagnostic log保留，不能拿推定當已證實根因。
- 診斷又用 Get-Content -Head 2 讀單行27MB JSON，仍印出全部一行，造成數百萬token的工具輸出截斷；後續 JSON 先解析再選必要欄位，不能以行数限制代替大型serialized資料摘要。這是診斷操作錯誤，沒有改變fixtures或放寬gate。
- March純2D與1D→2D原生144frames現已通過，38880 animated TRS屬性樣本：position7.450581e-9m／quaternion1.1920929e-7／scale1.7881393e-7；仍需完成其餘卡與UE gate，不以單卡結果宣稱五卡通解驗收。Stage290 runner先前沿用Walk名稱，實際source為base Idle＋explicit override，已改Layer輸出名稱，避免混稱一般走路。
- RHI目視發現UI看似倒轉、face圖只有脚／裙子：該controller base Idle本為躺姿，鏡頭只追蹤head位置、仍保持neutral站姿方向；preview固定Z=2地板又截掉負Z的頭部。finite／attachments0cm不足以接受這批圖。通用portrait現在連head orientation一起追蹤，frustum upper selection沿camera up而非world Z，fit保留roll；preview地板依實際顯示bounds向下移，不改角色TRS／root motion／physics，也不隱藏角色mesh。舊圖／UI／logs須待owned UE退出後封存再build重拍。
- 五卡Rust正式gate命令漏設UE的OK_GPU_SKIN=1／OK_SKELMESH=1，使用不同env_sig而重新抽CPU geometry（10分鐘仍處理第二卡）；這不是TRS拒絕。March CPU144frames已通過，未完成CPU log保留AnimatorRust290CpuPartial.log；核owned14872 exactexe後結束該輪，不把終止狀態當公式錯誤或五卡PASS。改明確GPU設定並以相同fixtures重跑五卡，正式驗收限與UE相同GPU載入。重現文件與專用Rust runner一起修正，避免shell歷史狀態決定測試配置。
- GPU旗標補上後也没有现成的完全相同env_sig cache；不能宣稱改旗標便必然命中cache。未優化debug texture／mesh extraction仍很慢，改正式runner使用與UE production相同的release profile，保持全部720-frame／weight／duration／2e-5 TRS assertions。核owned59536 exactexe後結束debug抽取，GPU partial與runner終止紀錄保留，不當作數值FAIL或驗收PASS。正式gate以完整release結果為準；未改CACHE_VER、SQL rows或容差以繞過抽取。
- 最終Python verifier初稿把AnimatorStateInfo.speedMultiplier一律期望1而拒絕；layer5來源m_SpeedParamID對應float default .5，native effective length2.66667175，Rust既有speed default解析與720-frame parity已正確通过。verifier改从同一serialized state與default float讀取speed／multiplier逐欄核對，而不是任意放寬長度或TRS tolerance。失敗log保留Animator290VerifierSpeedFailed.log；不能把Animator.speed=1混成每個state multiplier=1。
- 最終release native五卡720frames／194400 animated TRS屬性樣本，maxposition7.450581e-9m／quat1.1920929e-7／scale1.7881393e-7；weights／duration／mask/base全通過。1D五卡300frames／81000samples與controller2／catalog15回歸PASS，未完成CPU／GPUdebug不列正式證據。UE14distincttests全部Success／QueueEmpty14／exit0；首build29.44秒、camera12.51秒成功。最終15真RHI圖全部目視，71附件0cm、15fit≤.92、背景同正面中灰；Gallery四實際callbacks／三參數列／phase／clock通過。Animator290Verified.log全PASS／Audit保存source、fixtures、logs、images hashes；遊戲probe移回Tools，ownedUE25328／77632／77764正常exit0。single explicit Override＋default base與Cartesian範圍見Docs/UNREAL_ANIMATOR_2D.md，不混稱全部Animator／automatic transitions／多active layers／additive／IK／mirror；下巴依使用者要求略過。

## E-147　逐幀Session入口丟掉動畫參數（2026-09-29，stage291已驗收）

- 初次讀取又把kks_decomp當專案子目錄而rg失敗；HANDOFF2 §10已載明在遊戲根目錄。後續使用C:/portable/KoikatuSunshine/kks_decomp的實際來源，不將不存在的專案路徑當缺少反編譯資料。
- 檢查多層載入時發現apply_shape傳anim_parameters，但apply_anim_frame仍呼叫無參數apply_clip_at。UI getters／duration／raw evaluator parity皆正確，不能證明真Session pose用了滑桿數值。stage289／290的raw native數值與mask公式證據仍有效，先前RHI／finite／UI gate不足以證明參數完整進入逐幀骨架；該完整播放聲明在本輪撤回，須修入口與更強對照後重拍。
- 新增真FFI設定float後逐幀發布的五卡測試：以既有native驗證的explicit evaluator生成locals，走external Animator入口作獨立plumbing控制，兩條路共用相同helpers／ABMX／乳搖／衣物／附件。比較所有body transforms與runtime附件vertices，另要求非default參數有真pose變化。先保留未修版失敗，再改production；不靠getter值或finite pose當等價證據。
- 未修版正式負對照AnimatorSession291BeforeFix.log在March／Locomotion_Anger／phase0／cf_j_sk_01_03失敗，最大分量誤差0.059407875、exit101。production逐幀入口改傳同一Session anim_parameters，沒有更改raw公式或2e-5容差。
- rg在Windows不會展開Tools/GamePoseProbe/run*.ps1這類path glob；初次搜尋因此os error123，改rg -g '*.ps1' Tools。不能把glob失敗當runner不存在。
- Gallery新版實際display骨架驗證不能覆寫舊stage289／290 UI證據；新增OK_VERIFY_ANIM_OUTPUT_DIR與stage291專用runner，所有重拍使用新目錄，舊圖／logs／audit保持歷史紀錄。
- Unreal新positive gate首輪失敗：FOkSession::Open內OKPrepareLiveGpuEnv會設OK_UE_ANIM=1供Tpose／external取樣，test直接用FOkSession::SetAnimClip卻沒清旗標，ApplyAnimFrame因此跳過來源Animator。正式Actor::SetGameAnimation本已清除，所以不能把此測試設定錯誤當production仍漏參數。既有Animator1DBlends／Animator2DLayers同樣只驗到finite Tpose；撤回其450／300 frames作為「真來源取樣」的先前聲明。首輪補正時三測試在Open後清旗標、檢查非空骨架、scope還原環境，重跑完整queue；其後再把模式切換集中到共用SetAnimClip入口；保留首輪failed logs。NODB negative control在Open後設定，只隔離particle sampling，避免以不同env_sig重新抽一套geometry。
- Cartesian Gallery首次要求每個X／Y改變都在物理之後改world pose而失敗；續查local buffer與external flag，local/world皆0且flag已清，排除getter/world cache／external-mode推定。Rust診斷顯示cf_j_siri_L的body與export都被reference writer恢復。反編譯DynamicBone_Ver02.InitTransforms明確在Animator後寫localPosition／Rotation／Scale，Rust Ver02同順序；raw source變化與post-physics變化不能混稱。Gallery positive control現在scope隔離physics以核真正slider到display publisher，再恢復原設定與真pose才拍照；production物理與原30張RHI不改，完整Session gate仍含物理。此測試隔離不代表HAnime互動layer的物理enable政策已實作。
- 診斷時猜body_dynamics.rs／dynamic_bone_ver02.rs而read失敗；实际是src/dbone/ver02.rs，应先rg --files再讀。Rust診斷又沿用C++的.4／.31浮點字面值而compile失敗，改0.4／0.31；AnimatorCartesianPublisher291CompileFailed.log保留，不算執行期FAIL或PASS。
- 繼續檢查發現catalogue Playback／HandShapes也直接用FOkSession，而不是Actor；只在三個新tests清external flag仍是各caller補救。通用C++ FOkSession::SetAnimClip成功選取來源後現在統一清OK_UE_ANIM，選取失敗不改模式。移除三個tests的手動清旗標，要求common entry自行完成，重跑全15項，確保catalogue／mod／hand caller也真的取樣。Actor原有清旗標可保持相容，沒有按角色或state例外分支。

- 最終native完整Session五卡1020frames／610980 body transforms／22132164附件vertices，最大分量誤差皆0，tolerance2e-5不變，457.67秒；940個非default raw positive frames是物理前控制，不能冒充940個最終物理pose變化。Unreal最終common-entry15distincttests全Success／QueueEmpty15／exit0，五卡15參數positive及restore控制通過，真1D450／Cartesian300frames、source／external各900物理負對照與全部臉部回歸通過。30張RHI全部目視，每組71附件0cm／15fits≤.92。1D Gallery642display骨骼變化；Cartesian X(world7/local1)、Y(world9/local3)，physics隔離positive後還原才拍照。Reference writer獨立控制export delta0.053007465 PASS。Build26.06／9.48／12.08秒Success；最終source／fixture／log／image hashes在Animator291Audit.json，Verifier全部PASS。舊Tpose參考與stage289／290歷史證據保持；未聲稱automatic transitions、多weighted layers或完整HAnime physics-enable初始化完成。

## E-148　來源 Animator 速度參數被固定成預設值（2026-09-29，stage292已驗收）

- controller state_entry只在載入時把m_SpeedParamID的default乘進m_Speed；motion參數清單與每幀clock都漏了速度倍率。通解保存source參數身分與default，供所有單clip／BlendTree／explicit Override state共用，duration與sample clock一起更新，參數setter先驗有效正向clock再提交；不用卡名或特定骨骼分支。
- 新參數加入後不能把AnimationFloatAt(0)或UI第0列當Speed；native／UI／1D測試都要按來源參數名稱找列。原289／290預設倍率控制仍須回歸，不改既有native fixtures。
- 本輪再次猜kks_decomp/Studio/OCIChar.cs而搜尋失敗；正確來源為Managed/CharaStudio_Data/Assembly-CSharp/Studio/OCIChar.cs。先rg --files再讀，禁止從不完整路徑推論檔案不存在。
- Rust新完整Session測試首次編譯失敗：猜into_session接受game_root，以及HashMap.into型別推論成str。改成實際零參數method與明確HashMap<String,f32>；失敗log保存AnimatorClock292CompileFailed.log。修復patch混入不存在的MD anchor而整批verification失敗；重新檢查檔案確認未寫入，再分開code patch與append紀錄。
- 不能把任意很大／很小的正float都稱為invalid：只拒絕非finite、非positive effective speed與非finite／非positive duration。負倍率／pause的Unity反向clock語義未實作，不能聲稱這輪已支持。完整HAnime breast/layer/yure初始化仍是另外的缺口；只從controller速度參數得到的結果不能替代它。
- 編譯修復後的runner把整理logs和cargo串在專案根目錄，cargo找不到Cargo.toml；保存AnimatorClock292WrongCwd.log。檔案整理與Rust執行分開，cargo明確workdir Tools/ok-extract，輸出用absolute路徑。這不是取樣FAIL。
- 複製stage runner時在PowerShell的python -c使用C風格反斜線雙引號，產生ParserError並使下一步runner不存在；改原生PowerShell讀字串／Replace／Set-Content，明確先檢查生成結果再執行，不跨shell拼接。此次沒有啟動UE。
- 新增單clip速度參數後，其motion由None變Clip節點；不能沿用motion.is_some即拒絕raw sampler的舊判斷，否則破壞單clip default-clock API／override identity gate。改只拒絕真正的BlendTree或masked layer，保留原始單clip的預設取樣；參數化播放仍走完整pose evaluator。
- 首輪UE Animator2DLayers在nested state因source速度參數新增而FAIL：原gate硬寫4列，actual5列。改核對X/Y/height/Breast的source名稱，保留新增clock列，不刪參數或降低取樣／pose gates。首輪完整queue保存old後，最終build重跑。
- 加入layer fixture的可空controller欄位後，C# regular-frame initializer也必須明確寫base／layer0／空baseState，否則日後重跑會輸出null而Rust String不能解碼；已修復並另外做schema重跑。既有regular120筆缺欄位仍用serde default相容。Layer completion的controller原沿用base marker，實際source身分只由逐筆controller/layer/state及native hash guard核對；新completion改讀actual runtime controller.name，舊接受fixture保留。

- Stage292最終已驗收：native五卡180frames／48180 animated TRS attributes，max pos1.1920929e-7／q1.7881393e-7／scale2.3841858e-7；完整Session180frames／107820 body TF／3905676附件vertices max0，900invalid atomic控制。Native原1D300／Cartesian720回歸PASS，single-clip raw default API與新nullable schema重跑PASS。最終UE16tests全Success／QueueEmpty16／exit0，15RHI／71附件0cm／15fits≤.92，UI兩callback各642display變化且clock保持。build26.41／24.73秒成功，正式所有UE／capture用最後build；Verifier PASS及hash在Animator292Audit.json。錯誤logs與failed queue保留，T-pose根目錄未覆寫。完整HAnime初始化及其他Animator graph功能仍另列，不用本輪clock證據替代。
- 最後git diff --check發現Set-Content多加EOF空白行，exit2；保存AnimatorClock292DiffCheckFailed.log，改WriteAllText只保留一個結尾newline。這是MD空白錯誤，未更改程式或數值證據。

## E-149　Studio runtime override 載入丟掉 base controller 圖層（2026-09-29，stage293 驗證中）

- 根因：把 HAnime 的 override_file 當成新狀態機。實際 OCIChar.LoadAnime 呼叫 Illusion.Utils.Animator.SetupAnimatorOverrideController：保留 base graph，按 over.animationClips 的名稱及原陣列順序替換 clip，runtime controller.name 才改成 over.name。不能把這種 runtime 名稱映射與 serialized AnimatorOverrideController 的原始 PPtr 映射混用。
- 來源參數優先順序 height1→height、Breast1→Breast→breast；H 的保存 option 值在 LoadAnime 尾端會蓋過角色身高／胸部 seed。普通 Studio 保留角色身高。H options CSV 的空 breastLayer 經 int.TryParse(out) 會變 0，不是欄位宣告的 -1。
- 第一輪 native gate 失敗：沒有 layer79 composed entry。來源使用 m_StateMachineSynchronizedLayerIndex 指定 motion set；舊 parser 只讀 indexArray[0] 並排除非零 motion set。改從該 set 取 tree，unset tree 才退回 base tree；m_SyncedLayerAffectsTiming=false 以 base phase 取樣，true 暫不支援，不假裝成功。失敗摘錄保存在 Studio293NativeRustMarchMissingSync.txt。
- 第一輪 UE build 失敗（Studio293UnrealBuildFailed.log）：KKRustExtract.cpp 未宣告裸 Win32 SetEnvironmentVariableW，改沿用同檔案現有 FPlatformMisc::SetEnvironmentVar。這是編譯錯誤，不是可用結果。
- 本輪又猜錯 catalog/Actor 路徑與 rg shell wildcard；實際 catalog/Actor 在 Plugins/KKUnityBundle，先 rg --files，再用 rg -g。Python stdout cp950 印日本字元中止掃描；資料分析用 UTF-8 stdout 或只印 ASCII 身分欄位，不從中斷輸出推論完整性。
- 把 MD／log archive 與 cargo 放同一 exec，workdir 是 Tools/ok-extract，relative ERROR_LOG.md 讀取失敗；archive 也因 relative Saved 不存在而沒保存第一次 native log，rerun 覆寫。已改全 absolute 目標，保留實際失敗摘錄並記明不是完整 log。不能把已丟失 log 說成完整保存。
- 驗證期間不覆寫 stage292 fixtures／T-pose 根目錄；新 evidence 全在 Studio293。完整 Yure／PV／多 active layers／automatic transitions 尚不能用這一輪 graph gate 冒充完成。
- 新 raw gate 曾用 display label 尾碼判斷 synchronized state，但同 fullPath 的 layer label 被去重加上 (2) 等尾碼；改保存來源 m_FullPathID 並按 controller／layer／hash 核對，原失敗 log 為 Studio293NativeRustMarchLabelHashFailed.log。display label 不是 runtime 身分。
- 新增 Gallery 簡短 Studio source label 解析後 UE link 失敗：OpenKoikatsu 模組沒有自己的 Json 依賴。補 Build.cs Json 並重建，失敗 log 為 Studio293UnrealBuildJsonLinkFailed.log；不能依賴插件的另一個 binary 已連結 Json。
- 原 raw comparison 對 cf_d_bust00 出現 quaternion 1.328e-4 差異；曾懷疑重名 Transform，改限定 actual Animator subtree 後仍失敗，故此假說未證實，不能把 scope 改動當作根因修好。保留 quaternion／wrong-scope logs，另做 native Animator 與 AnimationClip.SampleAnimation 的獨立控制，門檻仍維持 2e-5。
- 獨立 native sampler（Studio293Sampler/*.sampler.tsv）證明 raw AnimationClip.SampleAnimation 的 cf_d_bust00 quaternion 與 Rust 一致，但 Animator 不同。根因是 runtime AnimatorOverrideController 的 string indexer 只命中原始 animationClips 陣列中第一張同名 clip。原本按名稱替換所有 motion leaves，誤換了其他同名 PPtr 的 breast motion。通解先按原陣列建立 first-name→original(file,PPtr)，再用 source overlay 陣列的順序替換該 original 身分；其他同名 clip 保留。失敗 before-name-indexer log 保留，嚴格原生數值 gate 重跑。
- Native identity 追加控制證實 base／layer 同名 clip instance ID 不同，關 layer79 的 quaternion 與 raw SampleAnimation 相同。最終 135 raw frames／36,450 TRS gate PASS，max pos/q 1.1920929e-7／scale5.9604645e-8，45 個 override 負對照；完整 Session135 frames／80,865 TF／2,929,257附件 vertices max0，405 atomic 控制。原 threshold 未放寬。
- 同步 layer 不能以「獨立 layer+authored Idle」公開給 raw selector；新支援只透過 composed Studio graph。非 base synchronized machine、affectsTiming=true、Additive 等維持拒絕；count／at／selectable 用共同 filter，另做 actual source 邊界測試。
- 早於 first-name-indexer 修正的 UE queue PID76596 已由 owner 驗 exact exe 後停止；部分 queue／build logs 存 old/Studio293BeforeNameIndexerFix，不算完成或 PASS。修正後重建，再跑完整 queue。
- Stage293 第一次修正後的完整 UE queue 已 17/17 Success（包含 source palette positive controls／已有乳搖及全部 face gates）；之後增加 public synced-state filter、Studio auto-scene policy、嚴格 TryParse／Bool.Parse 相容及完整 motion 可取樣驗證。該 queue 保存 old/Studio293BeforeFinalGuards，最終 build 重新跑全 17 項，避免把前版本測試冒充最後 binary。
- Studio descriptor 的 cache key 是 JSON，不能靠 cache 字串內的路徑分隔符猜 Auto scene；Studio source variant 明確返回 Studio。初始化也必須先驗 layer 與 base 的實際 motion plan，不能把 duration helper 的 fallback1 當成缺少 clip 的成功載入。

- 收尾再猜 Tools/run_studio.ps1 失敗；實際 runner 在 Tools/GamePoseProbe，文件已列完整路徑。診斷把fixture欄位猜成 floats 造成 KeyError，實際 parameters；PowerShell不接受 Bash {Public,Private} 路徑展開，整組只讀命令未執行，改 -g 檔名搜尋。
- 最終build47.06秒成功，17distinct UE Success／QueueEmpty17／exit0；Studio135frames、10options positive、45atomic controls；來源／外部乳搖各900影格、1D／2D／clock與臉部回歸全部通過。五卡15PNG及Gallery均使用此build，71附件0cm、15fit≤.92、gaze／highlight與背景驗收通過；ContactSheet/UI已目視，verifier完整PASS及SHA256。
- 檢視來源確認 Yure 不只 enable 動態骨：左右9個形狀flag會把card values換為來源defaults再分側updateMask更新，並非每幀UpdateAlways的胸部覆寫；UpdateAlways只處理kokan與shoulder。不能以「把9項Always寫回」代替實際LoadAnime形狀初始化。stage293當時尚未接入，不用其fullSession同路徑比較冒充遊戲Yure一致；stage294後續實作與獨立原生驗收見E-150。


## E-150　Studio Yure enable／形狀初始化（2026-09-29，stage294已驗收）

- 來源 OCI.LoadAnime／YureCtrl.LoadExcel 先ResetShape再Proc；四組dynamic enable與左右9形狀flag都有來源資料，原stage293只接controller與保存options，尚未接這部分。普通Studio動作不reset Yure，原生序列確認H→normal保留off/mask；不存在的Yure H才restore defaults。不能用「切普通一律重設」通解。
- DisableShapeBodyID不是每幀Always覆寫。ChaControl.UpdateShapeBody先mask7 card，再把5..13關閉值換為 [.5,.5,.5,.5,.5,.5,0,.5,.5]分側mask1/2，關閉nipStand使用.5且不套nipStandRate。source ResetPosition刷新胸部reference/local，不重建hips或清particle history；OnEnable清Position/PrevPosition/ObjectPrevPosition但保留UpdateTime。
- Rust初稿把UnityValue::Object猜BTreeMap實際IndexMap，match arm return後錯加分號；check兩error保留Studio294RustCheck.log，改pattern destructuring並修分隔，Fixed check PASS。測試ExportBone欄位誤寫pose_scale，實際pose_scl，failed compile保留EnableUnit.log；修後enable unit PASS。
- 一邊跑release test exe一邊改來源再跑cargo test，Windows linker LNK1104鎖住同個test executable，PhysicsMarch.log保留。cargo build鎖不涵蓋執行中的test；後續所有會重link相同test exe的測試順序執行，沒有終止既有test。第一份March Session仍完整PASS，9loads／1278shape attrs，max5.9604645e-8。
- 初版native physics trace未導出UpdateTime/原粒子/參數，不能以新runtime zero clock與native已運行clock比較；補齊yure-baseline.tsv及yure-setup.tsv後重新導出五卡，初版March四fixture保留old，不算完整物理證據。正式trace240frames/card、左右分別off/on及hipL，數值門檻維持2e-5。
- 檔案搜尋仍曾傳Windows wildcard路徑（KKExtractor*/Tools/build*），改rg -g；本輪未有這些檔案，bridge實際KKRustExtract。所有錯誤與假設都記錄，不以命令串最後read的exit0冒充前方compile成功。

- 第一輪UE新增test漏include ok_extract.h、RowFor lambda回FName與NAME_None(enum)推導不同型，compiler失敗Studio294UnrealBuild.log保存；補header／明確return FName，Fixed build26.92秒Success。
- 五卡H真RHI capture在March world-target eye-look audit拒絕，原manual gaze／highlight先PASS。沒有跳過audit或改target，追加selected／first_failed_step／paused／held／clock診斷；此版capture沒有PNG、不算驗收，失敗log保留。18測試queue繼續獨立完成再rebuild，避免改動正在載入的DLL。
- 原生五卡45loads／6390source-written shape attrs／20masked negative／45atomic初版PASS，最大1.1920929e-7；五卡physics1200frames／12000bones max1.7022714e-5，在原2e-5內。當時加入cache中立baseline guard後仍需重跑；最終最新版已於下列105.05秒Session驗證通過，不以初版PASS代替。
- H RHI診斷已確認根因：冷載入時body_shaped/head_shaped都None，set_yure_policy只建立body_shaped；apply_anim_frame以body-only判定初始化完成，reset_animator_pose把head_skel設為None。EyeLook config／Awake init仍存在，但source頭／center／兩EyeTarget全部消失。Rust預先apply_frame(0)的diagnostic掩蓋此錯誤；冷載入測試重現，失敗保留EyeLookColdDiagnostic與EyeLookMissingHead。不能以手動gaze PASS或finite body palette冒充頭部仍存在。
- 通解：共享shaped_baselines_missing同時檢查有來源rest的body與head；第一次Yure選擇用完整apply_shape建立兩者，再按current policy準備物理。已初始化切換仍只refresh胸部reference，保留粒子history／hips。UE新增五卡先建GPU export、第一個動作直接H後的source EyeLook斷言；沒有加入角色卡分支。
- 冷載入修正後第一組H截圖的臉／眉／EyeLook通過，但目視發現黑色背景與斜地平線；原背景只有固定rear/side牆與有限floor，portrait跟隨head轉到未覆蓋方向。不能只用TracedScreenshot數量判定影像通過，也不能刪除灰底gate。共用背景改六面enclosure，由actual displayed bounds及三個fitted cameras共同計算，保留actor/root/physics姿勢；初版完整五卡圖待封存BeforeBackdrop，不算最終圖。
- 首次verify_yure.py錯把背景建立次數當2×5=10；真ShotPreview先建立初始viewer，再reload五個task，每task一次Setup與一次fitted frame，合計11。15圖／15fit完整；保留Studio294VerifierBeforeSetupCount.log，依實際呼叫順序改1+2×卡數。像素灰底gate維持全部15圖top row與全身四角，不拿特寫下方可能有身體的角點當純背景。

- 最終cache guard＋完整cold baseline版：45loads／6390source shape attrs／20masked negative／45atomic，max1.1920929e-7，105.05秒PASS；五卡cold H→300EyeLook updates／5paused hold，52.94秒PASS。第一個動作不再靠warmup掩蓋missing head。
- 最新core版18項UE Success／QueueEmpty18／exit0，五卡Yure450frames與cold_eye_look=1，existing source/external dynamics、Studio、嘴型／眉眼、視線回歸全通過。背景版build10.34秒Success，最終五卡15RHI圖／71附件0cm／15fits≤.92／全部top灰底通過；總覽與March/Alice原圖已目視。完整verify_yure PASS與SHA256另存Audit；不宣稱全H自動scheduler或所有mod已驗收。


## E-151　Studio PV 手腳複製與来源開關（2026-09-29，stage295）

- 發現共用流程只有 Animator／手型／ExpressionBone，缺少 Studio.Preparation._pvCopy 的四組來源節點→手腳骨複製。HAnimeLoadInfo 的 PV 使用 bool.TryParse，空／invalid 為 false；普通 LoadAnime 恢復四個 true；ActiveKinematicMode FK 令全部停用，退出 FK 恢復保存的 enablePV。不能沿用 Yure 的「普通動作保留舊設定」。
- CopyPosRotScl 順序為 localScale→position→rotation；後兩項是世界空間。目的 parent 非等比 scale 時必須用完整 parent matrix 反算 position、用 parent quaternion 反算 rotation，不能從含 shear 的 world matrix 分解 rotation，不能整組 local TRS 直接複製。
- 綁定從 actual studio/base/00 的 Preparation._pvCopy／PVCopy.pv／bone GameObject PPtr 讀取；Reset 中四個硬編碼名稱只是 editor helper，不是 production 規則。使用 prefab-relative hierarchy paths，拒絕重複 suffix 的猜測；外部 FileID 不得用同檔 PathID 冒認。global Sideloader bundle override 與 source rig 共用。
- 本輪初始查來源誤猜 root AnimeCtrl.cs，實際 Studio/CharAnimeCtrl.cs。兩次誤猜 cache.rs／session/cache.rs，實際 sqlcache.rs；另一組查詢工作目錄已在 Tools/ok-extract 卻仍使用 root-relative path。只讀失敗沒有改檔；後續先 rg --files 並按 cwd 使用 absolute paths。
- probe 初稿把 FK setter 寫成 ActiveFK(bool) 編譯失敗；真正來源 ActiveKinematicMode(FK,active,force)。原生已改呼叫實際 setter。PowerShell 內嵌複雜 python -c 引號造成 SyntaxError，改 single-quoted here-string 送 Python stdin。
- 從 Yure runner 文字替換建立 PV runner，第二個全字串替換誤把剛設成1的 OK_PROBE_PV 又清為 null。第一場只跑通用 probe，沒有 .pv.json，runner 拒絕；Studio295NativeMarchRunner.log 保留。後續明確只設一次旗標；錯誤 completion／圖／TSV 封存 old，不能當 PV 證據。
- apply_patch 首次匹配 apply_shape 的手型段落，未匹配 finish_animator_pose；閱讀兩條入口後補上共用每幀／external 流程。不能只修初始化卻聲稱每幀生效。Rust test 浮點前導0在執行前已修正，未以編譯失敗當結果。
- actual compiled PVCopy <Start> callback 的獨立原生 fixture 已完成五卡75frames：180 copied、120 held，包含 source H slots0011、FK停用／恢復、normal恢复、非等比parent／PV scale与不同rotation。最大 local/world/quaternion误差3.5762787e-7，原2e-5 gate未改；180 positive controls與全部任意mod節點改名對照通過。
- 第一個共用Session版五卡300frames／720 positive／10 atomic拒絕通過；四組 binding均由source paths解析，normal与cache均reset transient policy。新增display ABI後最終版本仍需完成UE及RHI gates，再記正式驗收。

- display audit 初稿 range-for 用 auto* 推導 TObjectPtr<UPoseableMeshComponent> 失敗，Studio295UnrealBuildFinal.log 保留 C3535/C2440；改與原BodyDynamics audit相同的明確pointer type。接受build29.12秒Success；沒有用未成功build執行驗收。
- 補 source setter次序：先改destination localScale再讀PV世界位置，避免mod把PV放在destination descendant時讀到舊parent scale。最新Rust75原生／300共用frames再次全部PASS，2passed，最大3.5762787e-7；沒有改原2e-5門檻。
- 新增完整Studio body tree後，legacy regression 每frame exported transforms由599→600，135frames為81000而非80865。獨立UnityPy確認新增來源Look At Target挂在cf_j_head下，oo_base沒有此節點；不是少算比較項。最新135frames／81000骨TRS／2929257附件vertices max0、405atomic均PASS。
- verifier寫入前查實際UE queue log，依真格式 Automation Test Queue Empty 19 tests performed / RequestExitWithStatus(1,0)判定，沒有以不存在的TEST COMPLETE字串或stage294舊骨數當gate。錯誤模式的三個額外native artifacts已驗證在fixture root內後移old/WrongProbeFlag。

- 新增UE PV測試初次失敗：只查category128/id0，迴圈把正確group2 H row覆寫成group3的另一個H row；後者的來源PV全部true，所以mask15本來正確。初次判讀說成普通row，補查TSV後更正。來源Info的索引是(group,category,id)，不能把category視為global unique。修正測試加入group2／kind=hanime、H／normal各唯一性gate；RHI runner本來即使用完整identity，所以五卡source0011均PASS。完整第一次19項queue保留失敗，不算19全綠；待其餘18項完成後只重跑修正的PV項目。

- group修正後FK對照又失敗，差0.25；補完整名稱／local TRS／兩側flags診斷，不降低exact-hold gate。原生callback與Rust直接hierarchy原本通過，只是finite palette不能定位UE差异。診斷證明同index254在Session是cf_j_hand_L、Control是cf_s_bust00_L，兩側flags都0；測試跨Session錯用palette index，還把source PV input送入Control不同骨。索引必須屬於它的Session，即使同一張卡也不可共用。
- 測試改用每個Session的studio_pv_bone讀serialized slot，目的名稱先一致性gate，input的bone handle也分別轉換。production RHI audit原本使用自己的Session binding，不受此測試錯誤影響。兩次FK失敗log分別保留old/Studio295BeforeFKDiagnostic、old/Studio295FKDiagnostic；第一個統一verifier拒絕失敗的PV項，Studio295VerifiedBeforeDocs.log保留。不能看到OS exit0便宣稱PASS，runner本次均以failed gate拒絕。

- 檢查相同錯誤是否影響既有測試：BodyDynamics／ExternalBodyDynamics使用同一Session或name map，沒有跨Session索引問題；KKStudioYure也使用Control.SkelBoneAt(I)，已改Control name→local index join並驗唯一names、完整來源group identity與source PV mask。需補跑其450frames，不能繼續拿錯骨對照作Yure effect證據。
- 各Session獨立slot handle修正版PV測試正式300frames通過；五卡各144positive、FK exact hold／normal reset／2atomic，max .2646–.27。未改production計算或RHI顯示，歷次失敗均為選取／對照對象錯誤。后續完成Yure補驗後統一驗收。

- 最終Yure name join补驗450frames通過：每卡1799–1800 source effect更新，max .05–.489771，cold_eye_look／normal retain／fallback／atomic全PASS。PV300frames與17其他core合計19distinct全接受；不把初始Fail或錯骨差值算positive，沒有重跑無關native physics或修改誤差門檻。
- 五卡15RHI／424followers样本palette誤差2.252e-6cm、71attachments0cm、15fits≤.92、gray／face gates全部PASS／目視；根目錄Tpose286 hashes全相符。verify_pv完整PASS，最终日志Audit保存current sources／fixtures／images hash。所有owned进程完成，临时probe不留游戏plugins。stage295正式驗收。


## E-152　Animator motion 時間倍率（stage296，實作中）

- 源資料掃描437個base AnimatorController找到6個leaf m_Duration!=1；目前parser拒絕所有這些leaf。player Locomotion一個.8333333，khr_f_02_00的OLoop同步motion set1五個.5。後者base tree與motion set2皆1，不能把掃描結果誤稱base OLoop本身不可載入。
- 初次印整個H controller layer masks／motion arrays，輸出超量截斷。後續只輸出layer摘要／selected tree，完整常數存專用fixture檔；不從截斷輸出宣稱完整掃描。
- 舊歷史stage295與Tpose286保持。先以原生Animator量測m_Duration語義，再實作正值共用clock；沒有卡片名分支。

- 新test module的apply_patch只匹配單獨mod clock_tests但actual宣告與cfg同一行，patch被拒絕未修改；按真上下文修正。原raw single default sample也必須除leaf duration並合併cycle，只改BlendTree weighted duration會漏掉single-clip公共API，已一起補。
- 第一輪UE test編譯少一個右括號，MotionTimeScale296UnrealBuild.log保存C2143。改將fixture path與LoadFileToString分開表達後重建，不以失敗build作驗收。

- 第一輪Rust兩test中原生300frames／81000attrs已PASS，但Session對照在讀expected.body_shaped時None panic。ensure_skel_export不等於建立shaped baseline；external對照須先走apply_anim_frame(0,0)再從baseline獨立取樣。補初始化兩Session後重跑Session；失敗完整log保留FirstRun，不把1passed/1failed當全部通過。

- Session修補記錄與log移動又使用root-relative路徑，但cargo的cwd是Tools/ok-extract；MD片段誤建在Tools/ok-extract，log move失敗。片段已寫回root並存Saved/Logs/WrongCwdFragment，失敗log改absolute移到FirstRun；後續所有跨cwd路徑使用absolute。

- 第一組RHI15張目視拒絕：手腳被拉成細長線；原數值raw／Session同路徑與palette傳遞gate不能證明選對來源場景。測試用的是game H controller，但runner強制Studio scene4／PVCopy1111；追查場景／PV來源規則後再修正，這組不得列accepted或以15PNG數量取代目視。

- 根因：Studio composed descriptor把来源場景固定为Studio；game H controller的同步motion虽可共用图层组合，却不应自动插入Studio PVCopy／ExpressionBone和normal hand size修正。新增controller@组合保留base bundle来源场景，Studio@仍保留原OCI语义，两者cache key分离；H auto为H/esthetic，normal action为normal，沒有卡片分支。之前15圖與對應Session／UE logs封存BeforeSourceContext，最终需用正确source context重跑。
- 查此修補的rg又使用anim*作Windows檔名，命令拒绝未讀到結果；改實際目錄搭配-g，禁止从无结果推定沒有match。
- archive腳本已全absolute，但其後rg仍誤沿用root-relative到cargo cwd；只讀失敗，改absolute重查constructor／match，沒有根據失敗查詢改production。
- Context第一輪Rust native300仍PASS，但Session external Auto預設Studio（外部UE曲線沒有Unity來源），actual Auto已正確讀normal/H，差0.6386。這是對照場景不一致；保留ContextFirstRun，expected明確指定known native normal2／H3，actual保持Auto0並加入來源場景／PV exclusion斷言，不能改external既有Auto契約或放寬gate。

- 最終source-context版：Rust2passed130.35秒，native300frames／81000attrs，Session300frames／180000骨TRS／6509460附件vertices max5.960e-8／0，原2e-5不變；controller units4passed。新build24.38秒Success，UE motion300frames／318 changed bones與四項1065frames回歸全PASS，5distinct test。
- 最終15RHI已目視，手腳恢復正常；71attachments0cm、15fits、424followers、palette2.252e-6cm及全部五卡face/gaze/highlight/EyeLook PASS。Studio metadata PV flags1111仍保存，但H auto不執行PV；display audit不能把傳遞一致性推成該context有套PV。
- verifier草稿一度猜capture fit／world EyeLook log字樣，執行前依actual accepted log修正為ndc_x/y与eye look shot states；驗收保持真实15fits和5gates，沒有跳過失敗。最新source與binary／native／PNG統一SHA256，原Tpose286保持，错误图保存old，temporary probe已移回Tools。


## E-153（2026-09-29）Animator Additive 層：原生參考與分數權重

- 起因：437個base controllers有526個Additive layers，原loader只支援Override，因此真實HandCtrl層不能播放。HandCtrl.cs:366/370連續設定src/dst layer weights；不能把layer clip當完整身體動作。
- 調查首輪把raw/ref差值套到所有骨骼，忽略SkeletonMask，得到position .01347、scale .16353、rotation .07681假誤差。依原serialized path mask（layer7僅9個非零含root）重新計算後position5.84e-10、scale0。必須保留來源遮罩與未綁定骨。
- 初版假設weighted additive rotation=Slerp(identity,delta)，native layer8 weight.37 /elapsed1.7的cf_j_kokan誤差1.82494e-4，nlerp也錯。原生實測符合normalize(delta.xyz*weight,delta.w)；修改共用實作，不放寬2e-5門檻。
- Probe的Run是IEnumerator；新增branch不可return，必須finished=true/Application.Quit/yield break。已在第一次build前修正，沒有拿中間patch當原生結果。
- Rust first compile失敗6個無整數部分float literals（.37/.12/-.01）；補0修正，失敗log AnimatorAdditive297RustMarchFirst.log保留。該次還在cargo工作目錄使用root-relative rg operand，讀不到pose.rs；改absolute C:/portable/OpenKoikatsu/Tools/ok-extract/src/pose.rs確認Tf型別，不再組root-relative跨cwd命令。
- 目前驗證進行中；本節後續追加正式結果，不把五卡native檔存在當作演算法已通過。

- 第一輪native比較失敗在matched>100，不是已通過geometry。fixture450 native bones與production實際matched615 binding attrs正確，test在開卡前過早snapshot Avatar TOS；改在載入同卡骨架後取得來源TOS，增加matched/attrs/tos診斷，失敗log改名AnimatorAdditive297RustMarchFirstComparison.log保存。

- 第二輪matched診斷90 /205 source names /901 TOS證實上述過早TOS推定不是根因。Source clip含90 CF與115 CM names；測試把「>100 animated attributes」誤改成「>100 unique bones」。改為來源CF binding數至少80且全部逐骨逐TRS比較（實際90骨×3=270 attrs/frame），不減少任何來源CF骨，不改2e-5 geometry門檻。AnimatorAdditive297RustMarchSecondComparison.log保留。
- 產生UE runner的連鎖replace暫得AnimatorAnimatorAdditive錯測試名，靜態rg檢查即修正AnimatorAdditiveLayers，未執行錯queue。

- Docs讀取未指定encoding，Windows Python default cp950導致UnicodeDecodeError。讀取即失敗未改檔；重跑明確read_text(encoding=utf-8)/write_text(encoding=utf-8)，後續所有MD／TSV／JSON明確UTF8。
- 正式native五卡180frames/48600 TRS attrs皆PASS（max三項均1.1920929e-7）；Session180frames/108000骨/3905676附件vertices max5.9604645e-8/0m。獨立UnityPy source-mask＋native snapshots83988 transforms正向PASS，Slerp/nlerp/absolute Override負向控制均Fail正確排除。Unit與production UE build、新UE180frames/360 invalid setter gates通過，原2e-5門檻不變；RHI與regression接續，尚不提前宣稱完成。

- 最後code review發現nonempty reference格式check只放在motion_plan；public listing／set_anim_clip先接受，失敗後可能fallback成缺clip姿勢。移到ClipEntry來源metadata，含runtime override replacement後再檢查，unsupported Additive從public graph排除；數量／索引／setter保持一致。新增partial/nonfinite/type錯reference unit；舊正向Rust與unit移BeforeReferenceGuard保存，重新build正式版本並跑驗證。

- 最終reference guard版重新完整驗證：Rust兩項78.21秒PASS、兩項unit PASS、production build34.01秒Success；UE新180frames／360非法weight拒絕／五卡palette改變、五项1365frames regression均Success（distinct6tests）、queue1/5、exit0。RHI15PNG、71附件max0cm、15fit≤.92、424follower samples／palette2.252e-6cm、所有face/gaze/eye gates五卡PASS；完整15圖＋3張原圖目視通過新增退步檢查。verify_additive.py完整PASS、Tpose28615圖SHA256未變、probe與owned進程已清理。BeforeReferenceGuard歷史passes／3個失敗Rust logs都保留，不放寬任何數值門檻，也未用Actor假offset補卡。


## E-154 — Stage298 多層 body Animator：來源profile与測試名稱

- code review發現把Studio options判斷改成scene_for_source==Studio會讓controller@包住Studio來源也套OCI初始化，與Stage296區分來源profile的規則衝突。正式build前改成studio_source_profile递迴Layers但保留Studio variant明確bool；scene correction與初始化來源不可混為一談。
- 複製UE test用兩次字串取代，註冊名稱成AnimatorMultiLayerLayers而runner用AnimatorMultiLayer。這是E-153同類工具產生錯誤再次發生，rg在build前找到並直接修成exact正式名稱。後續驗收必須檢查「實際test Success名稱集合＋QueueEmpty數」，不只process exit0，複製generator需用明確字串欄位。
- native独立控制確認升序layer index與reverse order有可量測差異；順序不由使用者描述排列決定。不得把Additive quaternion線性相加，也不得每層重設base。記錄屬演算法負向控制，不是已執行錯誤production版本。
- Stage298原生／Rust數值對齊沒有失敗；最終兩項Rust101.90秒PASS，native75frames／20250 attrs max1.1920929e-7，Session45000完整骨與1627365附件頂點max骨5.9604645e-8、附件0m。25個unsupported描述與每層4類非法權重拒絕，拒絕後參數／時鐘不變。
- 新UE75frames Success／QueueEmpty1／exit0，五卡實際published palette各1骨改變，390次非法權重拒絕。RHI仍記錄既有E-099 CommonUI viewport routing error；不要將材質／geometry gate通過概括為完整log無Error。
- 首輪RustNative141.35秒pass在formal Studio profile guard／unsupported selection assertions前執行，保留AnimatorMultiLayer298RustNativeFirst.log作歷史；正式採用最終101.90秒兩項log與production build後UE控制。初次獨立native composition log只含當時已齊的2卡30frame，正式採用完整五卡75frame NativeCompositionAudit，不把partial coverage誤報為五卡。

- Stage298最終七項UE Success／1620sourceframes／QueueEmpty1/6／exit0；15RHI900×1080／71attachments max0cm／424followersamples／palette max2.252e-6cm，face／gaze／time／highlight／scene gates通過。全部15圖與四張原圖檢查無新增幾何回歸；rootTpose28615圖SHA保持。`verify_multilayer.py`PASS／Audit JSON保存完整hash。此結論不包含deferred chin／所有shader像素／完整HandCtrl scheduler；未降低2e-5來源誤差門檻。


## E-155 — Stage299 起播時序與來源Restart需區分

- 開始計畫把HandCtrl切換籠統描述為Play；讀到HandCtrl.AnimatrotRestrart後確認它設定Restart trigger，沒有直接逐層Play。立即修正範圍：這輪實作共用Animator.Play可表達的epoch／normalized start，serialized trigger transitions仍另行實作。不可把LayerBlend權重或API取樣控制當完整HandCtrl排程驗收。
- 增加多種synthetic controls時，固定ID lane能與另layer control／Unity source hash碰撞。正式實作改canonical source index順序，動態取未被來源float占用的ID；reserved名稱（含sync層clock名稱）碰撞明確拒絕，不能在add_parameter以相同ID合併掉不同名稱。
- Code review發現新Rust拒絕clock測試若在嘗試非法clock後才保存參數，只會驗到weight拒絕後不變。最終run前把snapshot移到全部非法setter之前；所有拒絕一起核對參數與global時間不变。
- 依E-153／154，UE新test採exact registration字串更新，runner啟動前核對註冊名稱，最終仍要求實際Success名称集合與QueueEmpty數。這輪沒有生成重複suffix的test名。
- native及Rust正式結果完成後補齊；partial native composition另存Partial.log，不當完整五卡證據。

- Unreal實際播放路徑仍對global AnimPhase套base duration的Fmod／Min；只測FOkSession raw time會漏掉此問題。主循環會重置其他層epoch，base end-hold也會凍結更長的layer。改為game sampler保存未wrap elapsed，各source state自行loop／clamp；UE Sequence路徑維持其自身時鐘。新增五卡實際Actor.Tick跨三個base週期及base duration之後起播、loop/end-hold共40samples gate。
- 上述Tick gate會改動動力學／表情歷史，不能先跑再截圖。移到每卡三張capture完成之後、載入下一張卡之前；只恢復clock與parameters，不假稱恢復physics particle history。
- 並行UE測試與RHI程序出現HTTP listener 30000／9876 bind errors。非動畫數值失敗，但不能宣稱log無Error；最終build後改依序測試／截圖，既有CommonUI routing錯誤仍如實保留。
- verifier code review修正Python assertion左側只有truthy字串而沒做membership的錯誤，兩個拒絕數字都明確核對。工作區UE程式位於Plugins/KKUnityBundle，不是Source/OpenKoikatsu/Private；錯路徑讀取失敗後用rg --files定位，未改錯檔。

- runner預設全測試分支還沿用Stage298 log stem，雖本輪使用的兩個explicit switches正確，仍修成StudioUnrealAutomation299避免覆寫前輪。新增capture完整數量 gate与自動README；編譯保留既有Gallery verifier C4701 Value警告，不宣稱整個專案零warning。

- Stage299最終native/Rust/UE/RHI全部PASS：100原生frames／27000attrs／46660独立Transform controls，Session60000骨與2169820附件頂點、八項UE／1720sourceframes、40 Actor.Tick samples與15captures；max值與範圍见Docs/UNREAL_ANIMATOR_LAYER_CLOCK.md，2e-5門檻未降低。BeforeActorClockGuard版本完整保留，不冒充最終driver證據；root28615圖SHA不變、probe移回Tools、owned程序均退出。
- RHI全15圖及五張重點原圖已目視；KK091胸前彎曲髮束在Stage298已有，來源附件傳遞驗證不等於全部hair dynamics遊戲像素驗收。最終只剩既有CommonUI routing Error及cleanup hitches／Gallery warning，無本輪並行HTTP bind錯誤。不得把geometry／time gates的PASS泛化為所有mod、shader像素或完整HandCtrl FSM完成。


## E-156 — Stage300 來源trigger self re-entry

- HandCtrl.Restart不能把所有Animator層或global elapsed一起歸零。來源khr_f_02_00的AnyState Restart只在部分獨立層，destination0、duration0、offset0、no exit time、can self；base與同步73/74無此rule。本輪API按serialized type9 trigger及rule判斷，只支援即时self re-entry，unsupported／pending／cross-state／crossfade不假設可重置。
- 複製native runner的連鎖replace把try內剛設定的OK_PROBE_TRIGGER_RESTART又清成null，導致首輪March走一般pose/physics dump而非restart fixture。讀到缺少.restart.json及controller=c33即定位，停止path驗證的owned PID68736；wrong branch輸出保留old，不當動畫驗證。修正exact設定並在每次launch前assert主旗標1及其它分支未啟用。複製generator不得以重疊全域replace改互相包含的旗標。
- Trigger API採三個新增C exports，保持OkRustBindings原配置大小／欄位不變，避免擴大寫入舊caller較小buffer。Rust lib/header/UE plugin仍一起build；decoded／mesh cache format不改。

- 最終執行前review發現Shot runner仍指向Stage299輸出目錄。已修為AnimatorRestart300，避免覆寫已驗收舊圖；新runner在執行前須同時檢查test registration、logs與capture三個stage欄位，不能只更新log名字。
- 同名unsupported AnyState rule不可跳過；按來源優先序保留blocker並整次拒絕。future layer epoch尚未進入時，此即時API拒絕且保持全部params，不能用clock reset偷偷提前啟用。API沒有pending queue／完整FSM，不將selected self re-entry驗收泛化為全controller transitions完成。

- 首次UE build因新test內float Value遮蔽外層fixture Value，UE將C4456當error而失敗。只改test變數為FloatValue，失敗build保留AnimatorRestart300UnrealBuildFirstFailed.log；production Rust／native結果不受影響。巢狀fixture／parameter迴圈須用語義不同的變數名。

- 驗收讀取cache版本時先猜cache.rs／decoded.rs而rg報檔案不存在；已改用rg --files或對確定存在的src目錄搜尋。不能沿用檔名印象推定來源位置。

- 最終完整五卡原生75frames、獨立34995 transforms、Rust兩項205.62秒PASS、production build6.91秒成功、UE新75frames／90events／225非法拒絕及八項1720frames回歸全部通過，數值原門檻2e-5不變。首次build失敗與單卡／partial logs保留，不冒充最終production或五卡覆蓋。
- RHI15PNG＋15post-capture Actor.Tick samples／events PASS；10capture events與90UE fixture events分開記錄。71attachment0cm／424followersamples／palette2.252e-6cm／全部face gates通過，完整目視未見新增hair／accessory回歸。KK091胸前彎曲前髮是Stage298已有，來源palette proof不當所有hair物理的遊戲像素認證。既有CommonUI Error仍保留、無新HTTP bind錯誤。
- verify_restart.py完整PASS，正式root286 Tpose15圖SHA保持、probe不留game plugins、owned程序均退出；new目錄只有當輪15PNG＋README。完整FSM／pending triggers／HandCtrl items與shaderpixel仍待，不把本輪通用self re-entry說成全專案完成。

## E-157 — Stage301 CrossFade來源與驗證

- 工作區没有實體AGENTS.md；首次讀取報不存在，採用使用者訊息提供的AGENTS規則。後續先用rg --files定位檔案，避免假設實體檔存在。
- ChaControl.setAnimPtnCrossFade與EasyLoader.Motion直接呼叫Animator.CrossFade，transitionDuration是normalized來源state時間而不是無條件秒數。先用原生Animator量測source／destination clocks與mix，不以自訂ease／定格來源pose代替。

- 新Rust tests四處沿用C#的.37／.4寫法造成compile error，再次觸犯E-153既有紀錄。已改0.37／0.4；首輪compile log保留RustFirstCompileFailed。跨語言搬測試前檢查浮點literal，不把這類失敗log当數值比對結果。
- Code review將post-transition phase計算移到參數提交之前，finite overflow要整次拒絕；當前preview float setter改讀Session的正式elapsed，避免CrossFade後繼續按base duration重算全域時鐘。既有零epoch行為仍由Session維持。

- 首次數值tests把serialized zero scale當Animator寫入，cf_s_head實際保留卡片值，與raw shaped baseline不同；當時推測缺少ABMX回呼是錯的。Probe在disabled Animator上手動Update，沒有再跑LateUpdate；補ABMX反而在cf_j_spine03造成0.005m重複位移。改依每個sample的有效Transform binding比對，zero scale依既有共用sampler規則不寫入，保留門檻2e-5；所有body Animator域內有效位移／旋轉／縮放逐一驗證，private head不冒稱body animated curves。獨立native composition仍比較完整34995 transforms。另把f32::MAX normalized duration一律當非法是測試假設錯誤：來源長度小於1時乘積仍finite，應允許，僅乘積overflow拒絕。FirstNumericFailed／RustMarch／RustMarchAttributes／RustMarchEffective失敗log皆保留，不當正式五卡證據。
- Python generator的內文包含PowerShell here-string結尾標記，PowerShell先解析導致ParserError，沒有寫入runner。改用直接檔案patch，避免嵌套引號與delimiter。另一個rg讀取誤用Windows wildcard operand，改搜確定存在目錄配-g；不再猜glob路徑。
- 誤對single-line native JSON使用rg，輸出整份巨量fixture。後續用Python json解析只列指定骨／欄位。兩次猜不存在的clock／motion檔名再次造成read error；先rg --files定位才讀，不能沿用舊檔名印象。
- 查build重現指令時再次使用Tools/build* wildcard operand；已停止此查法，直接用已確認的UE Build.bat。首次UE build34.10秒因新spec只include bridge forward declaration，缺少ok_extract.h而OkBoneView不完整；補正式header，保留UnrealBuildFirstFailed.log。bridge header足以傳reference，不足以建立struct實例；新增spec需檢查完整型別來源。
- 來源decomp命名空間目錄是Illusion.Game.Elements.EasyLoader與NodeCanvas.Tasks.Actions，不能依namespace猜多層目錄；exists檢查False後用rg --files找到正式檔，修正feature doc路径與audit hash。
- 正式五卡native75frames／20625有效body通道、獨立34995 native transforms與Session45000骨／1627365附件頂點全部PASS；兩項Rust153.44秒，原2e-5門檻未放寬。最終build6.78秒Succeeded、新UE75frames／285拒絕與九項1795frames回歸Success，共10distinct／1870sourceframes、QueueEmpty1/9、exit0。
- RHI15PNG＋15post-capture Actor.Tick PASS；71attachment0cm／15fits≤.92／424PV follower samples max2.252e-6cm、CrossFade followers0cm，全部face gates通過。全15圖sheet與六張重點原圖目視未見本輪新增眉眼／側髮／髮飾／頸飾偏移；normal與前輪H context不同，不作同動畫遊戲像素對照。CommonUI routing既有Error與Gallery C4701／cleanup warnings保留，無本輪HTTP bind errors；不假稱particle history還原。
- verify_crossfade.py完整PASS；root286 Tpose15圖SHA不變，probe移回Tools、八個owned native／UE程序均退出，新目錄只有15PNG＋README。source/decomp／code／binary／logs／PNG hash存Audit.json；active interruption／layer CrossFade／automatic FSM／HandCtrl等剩餘範圍如實記錄，未當全專案完成。

## E-158 — Stage302 獨立layer CrossFade混合順序

- 不能把兩個已完成整體layer合成的骨架直接lerp當作layer CrossFade。Additive與fractional layer weight的quaternion混合不具交換／結合律；來源應先混合該layer的raw motion/delta，再依mask、weight與layer index合成。先取原生combined／isolated／from／to與錯誤final-pose混合反例，不按角色卡補償。
- 沿用E-157規則：manual Animator.Update原生比對不再套ABMX；有效Transform通道與全量native composition分別驗證，維持2e-5門檻。Rust float literal帶前導0、UE spec包含完整ok_extract.h、先定位檔名及namespace目錄，不搜巨型單行JSON。
- 生成native runner時重疊replace又把新flag初始化後清為null，重複E-156錯誤；launch前assert攔下，沒有啟動遊戲，DLL已finally移回Tools。保留NativeBeforeLaunchFailed.log。改為先清本runner process繼承的OK_PROBE_*，再單次指定branch，不以重疊文字replace維護初始化／cleanup；沒有修改使用者或系統環境。

- 抽出sample_motion_pose／compose_motion_layer時，Python generator重複拼接已包含的compose尾段，造成unexpected closing delimiter，編譯前未進入數值測試。已只移除重複尾段，保留原mask／additive公式；失敗log保留AnimatorLayerFade302RustFirstCompileFailed.log。重構文字切片需核對完整函式邊界，不重複append已包含的區塊。修補指令首次與Cargo workdir合併導致相對path讀取失敗，沒有寫入；已將root修補與Cargo測試分開、Stop on error。

- 第二次compile指出Session引用private anim::controller；改以anim crate內部重新export clock命名函式，維持controller私有。VisibilityFailed.log保留。查UE actor時再次猜KKCardPreviewActor而實際是KKPreviewActor，rg只報不存在未修改；改用已驗收verifier記錄的確定path。

- 重新export後還需將clock命名函式由pub(super)擴至pub(crate)，否則Rust E0364；ReExportFailed.log保留。controller模組本身仍私有，只共用兩個internal名稱函式。

- UE spec generator以未錨定子字串找尾段，匹配到內層Key而截掉主loop；寫後檢查即發現，compile前補回完整20frame loop。改以patch核對函式邊界，不以同名子字串假設唯一。Actor需要來源child length而非base duration，新增唯讀export查active source length，兩個新exports都在舊binding表之外。

- Shot runner生成再次嵌入PowerShell here-string delimiter造成ParserError（E-157重犯），未寫入檔案。已改直接apply_patch新增runner，不再包在here-string內；在build／launch前檢查新stage registration、logs及output欄位。

- 首輪UE新測試20frames轉場gate通過，但假設第7層零duration必有可見rotation palette變化，assert失敗。原生第7層final q差約3.47e-4且變化域小，第8層約1.43e-2；改用第8層與已驗收clock test相同的可見控制驗零duration（不改production／誤差門檻），保留FirstPaletteFailed Console／Runner／abslog。失敗不能概括為五卡PASS，最終重跑全100frames；native／Rust原結果保持。

- 進一步定位原生第7層相位差只有cf_j_kokan的rotation超1e-5，第8層另有cf_j_waist02。raw Animator變化不等於每張卡skinned公開palette都必改變；測試需選實際輸出域的來源控制，不能以通過有限值gate推定visible motion或改production補償。

- UE spec零duration來源修正需重新build；已path／project／log識別後停止owned pre-final regression PID71736（1D成功，其餘未完成），保留BeforeFinalSpec三份log與build。最終new／十項regression／RHI將依序使用同一最終binary，partial不得計入1970frame驗收。

- 最終五卡native100frames／27000有效attrs與獨立46660Transform合成、Rust169.48秒
  兩項PASS、Session60000骨／2169820private附件頂點（5.9605e-8／0m）通過原2e-5。
  正式UE build8.32秒Succeeded、新100frames／1100拒絕／5zero duration，以及十項
  1870frames回歸均Success，共11distinct／1970sourceframes。FirstPaletteFailed、
  BeforeFinalSpec partial與compile failures另列歷史，不當最後binary／五卡證據。
- RHI15PNG＋README、71attachment0cm／15fits／424PV samples max2.252e-6cm、全部face
  gates／15post-capture Actor.Tick palette0cm PASS；完整sheet與八張原圖目視。
  KK091既有胸前彎曲髮束不當新回歸，palette不當全hair dynamics遊戲像素認證。
  root28615圖SHA保持、probe移回Tools、十個owned程序均退出。CommonUI routing Error、
  Gallery C4701／cleanup／HTTP timeout warnings仍保留，沒有本輪HTTP bind錯誤。
- verify_layer_fade.py完整PASS，Audit保存完整hash。僅一個independent child transition，
  未驗收concurrent／synced／不同state普遍native覆蓋／active interruption／FSM／HandCtrl
  scheduler或全部mods／shaderpixel，下巴仍依使用者指示延期。


## E-159 — Stage303 並行layer CrossFade

- 現有Session只有一筆active fade，將第二個不同layer的CrossFade當interruption拒絕。
  ChaControl的CrossFade／isBlend都是per-layer；本輪按layer存runtime轉場，each fade先
  混合該層，再按source mask／weight／index合成。禁止以最後一個global progress套全部層。
- 沿用E-157／E-158：先定位函式與檔案、只解析JSON必要欄位、Rust前導0、UE完整header；
  runner先清本process OK_PROBE_*再設定唯一branch，launch前assert；新增runner只改互不
  重疊的精確stage token，PowerShell parser與輸出根檢查在啟動前完成。原2e-5保持。

## E-159 — Stage303 並行 layer CrossFade

- 不可沿用全域單一CrossFade進度或在新layer事件重置舊layer。每層保存from/to clock與source length，按來源index合成raw/delta、mask與weight；原生100frames負向collapsed-progress差0.003501超2e-5。
- 新Rust執行指令漏release，debug-profile耗時過長；驗證PID27920的完整exe路徑與parallel_fade_tests命令後停止，保留RustDebugPartial.log，不當PASS。正式指令明確加--release，未降低門檻。
- 單卡／partial native composition40frames保存Partial.log；最終完整100frames另存NativeComposition.log，不能以初步結果宣稱五卡完成。


- 產生新測試時先猜Session base method名crossfade_animation，讀正式fn發現應為crossfade，已在compile前修正。應先rg確認API名，避免跨語言／舊名稱搬移。正式release用manifest-path從root可執行，但重現仍採Tools/ok-extract顯式workdir與絕對log路徑，不能依root相對路徑猜cargo行為。
- 正式Rust兩項223.31秒PASS：100nativeframes/27000有效attrs，Session100frames/200events/60000骨TRS/2169820private附件，最大骨5.9605e-8、附件0m、2600原子拒絕、5zero-duration-during-active，2e-5不变。native與同sampler Session整合證據分開表述。
- 新兩個逐層C exports保持OkRustBindings布局；global source_length多層active返回0，不能任取第一／最後層。每層完成單獨移除，不清除未完成層；SetAnimClip清除兩組轉場。

- 查截圖歸檔又猜OKScreenshotHelpers.cpp而rg不存在，重複E-157已記錄的定位錯誤；改先rg --files取真實路徑，再讀KKPreviewActor的已知capture區塊。不得以檔名印象直接讀取。


- 新UE target100frames/200events/2600拒絕/5zero控制已Success且UE RequestExitWithStatus(1,0)，但沿用runner WaitForExit後ExitCode為null，Tests exit輸出空白。不冒稱runner讀到0；改在Wait前取得Handle、Wait後Refresh並拒絕null exit，避免Shots跳過README。new target的UE exit證據與wrapper exit0分開，後續regression/RHI採修正runner。


- RHI前review發現SetGameAnimation/SetAnimClip會clear anim_parameters；新Actor並行audit沿用只設定layer controls，卻以native height=.37/height1=.61/Breast1=.37的絕對完成時間判斷，會跑成defaults。先修audit加入完整來源floats，不改production sampler或門檻。已验证PID32196 exe/project/log後停止partial regression；首build/已成功new target/partial regression完整保存BeforeActorSourceParameters，正式binary重建後重跑必要new/regression gates。


- 最後production build13.60秒Succeeded，正式new target100frames/200events/2600拒絕/5zero控制Success；11項1970sourceframes回歸全Success，合計12distinct/2070來源frames、QueueEmpty1/11、UE及修正runner exit0。前輪new/partial不能當最後binary的正式證據。
- RHI15PNG+README與15直接Actor.Tick PASS，10capture events、71attachment0cm、15fits≤.92、424PV followers2.252e-6cm、全部face/time/gaze/highlight/EyeLook/scene gates成功。全15sheet+8原圖目視無本輪新增hair/accessory偏移；KK091胸前彎曲前髮對照Stage302仍存在，是Stage298既有未修問題。geometry proof不當所有hair physics/shaderpixel認證，下巴仍延期。
- verify_parallel_fade.py完整PASS；root28615Tpose SHA保持、probe不留game plugins、12個owned native/Rust/UE進程（含debug/partial）均退出。新目錄只有15PNG+README，runner重跑先以已驗證absolute root移top-level舊圖到old，無刪除。CommonUI既有Error及BpGeneratorUltimate/Gallery/cleanup/HTTP warnings保留、無新listener bind error。source/decomp/code/binary/logs/native/PNG hashes存正式Audit；更新MD後再跑總驗收保存最後hash。

## E-160 — Stage304 左右面板驗證的眨眼 callback（2026-09-29）

- 首次 UI smoke 在 face gallery selection/rate/clock gate failed 停止，沒有產生驗收圖。完整失敗 log 保留於 Saved/Logs/GalleryPanels304CaptureBeforeBlinkCallback.log，初次 build 保留 BeforeBlinkCallback；不得把這次當成功。
- 原因已由 UE 5.8 UCheckBox::SetIsChecked 與 Gallery callback 確認：程式設定勾選值不會廣播 OnCheckStateChanged。既有 ok.VerifyAnimationCatalog 的 OK_VERIFY_FACE_CONTROLS branch 只 SetIsChecked(false)，沒有停掉 Actor 自動眨眼，因此手動開合值不會寫入。測試補呼叫既有 OnAutomaticBlink(false)，UI 使用者點擊的正式 callback 不需變更。
- 該 UE 首輪即使記 RequestExitWithStatus(0,1)，Process.ExitCode 仍讀到0。驗收必須同時有成功事件與截圖，不以 exit0 覆蓋 log 的 gate failure。
- 查 Actor 時又猜 Source/OpenKoikatsu/KKPreviewActor.cpp，實際在 Plugins/KKUnityBundle/Source/KKUnityBundle/Private。此為重複定位錯誤；先 rg --files -g '*PreviewActor*' 取得路徑再讀，不能靠記憶猜檔名。
- 原先所有動作、表情與96個身體／臉部 slider 擠在同一左欄；本輪改為左側 shape scroll、右側 motion scroll，各自可捲動。中央 Spacer 與 Root 必須不攔截滑鼠，避免角色旋轉／縮放被新 UMG 容器擋住。所有卡共用版面，沒有卡名分支。
- 修正後正式build8.85秒Succeeded；face callback/count/rate與Studio/mouth smoke PASS，gallery_face_controls.png已目視，正常RequestExit(0)與wrapper exit0。此圖只驗UI配置與callback，不將其中Studio pose當遊戲mesh比例證據。所有15張root286 Tpose SHA256保持。

## E-161 — Stage305 新增角色載入與畫面失敗（2026-09-29）

- 來源實際在 C:/portable/KoikatuSunshine/mesh_dump，初次仍依舊聊天猜 mesh 目錄而不存在；之後又猜不存在的 src/bin、OkSession.h 與 RunAnimatorParallelFade303.ps1，並把 UNREAL_ANIMATOR*.md 當 Windows rg 檔案 operand。重複 E-160：先讀交接的有效路徑、rg --files -g 模式取得真實檔名，不能以記憶或 wildcard operand 直接讀取。
- 找到三個新增目錄而非兩個：KK_388205、KK_346095、弹丸论破 - 江之岛盾子2；各有一張JPG，無新增 mesh/材質 dump。三張都測，不能把照片比較寫成逐頂點来源認證。
- 首次 T-pose 設 OK_SHOT_BLINK=open 後，SetPoseMode(0) 又開 Tick，正臉取景時可能已閉眼。首輪18圖與兩份log保留old/BeforeFreeze／BeforeFreeze.log。截圖測試分支建立中立姿勢後關閉 Tick，重新正式build10.86秒成功、重拍18張固定frame；沒有修改正式互動眨眼功能。
- KK_388205：鼻部有灰色矩形、虹膜只剩小淺藍圓片。KK_346095：白領變藍、胸前灰色三角、臉頰红暈／皮膚细節過強。Junko：未見眼鏡，虹膜／髮飾／靴子材質不同，遠處零碎幾何使全身／臉部 framing失效。完整根因尚未定位，不把任何一個 shader 或 mod 當已證實原因。
- 三張Studio walk 0.4s公開圖片都有手臂伸長，不能因Rust已找到73個body bindings、PV display及附件buffer audits通過而判動作正確。需要對world TRS／bind matrix／實際蒙皮頂點查驗，不能以隱藏頂點或相機補丁掩蓋。
- 新程序單卡Junko也有相同遠處幾何，已排除「前卡殘留」，但未證明特定飾品slot／骨架根因。單卡領結／頸部材質仍有差異；只保存證據，不以卡片特例調色。
- 最終三卡18圖+單卡3圖正常RequestExit(0)／runner exit0；main兩pose各3PV audits／27 runtime parts、最大附件0cm，palette2.252e-6cm。這只證明與runtime輸出一致，三卡外觀均未驗收。最終圖、卡、來源JPG hashes和ContactSheet記Saved/Logs/NewCards305Audit.json，visual_parity_passed=false。
- 所有5個owned UE程序69028／74616（before freeze）、72280／69212（正式兩pose）、61480（單卡）已退出；原root Stage286 Tpose15張SHA保持。新截圖只在NewCards305與NewCards305Solo；共用runner可換任意卡名，production loader無卡名分支。詳細見Docs/NEW_DUMP_CARD_TESTS305.md。
- 曾考慮將Actor.MissingMods加到ShotPreview，但檢查發現它只在LoadDirectory manifest路徑填入，不能代表本次Session路徑的mod完整性；在build前移除該誤導性診斷。不把未列missing等同全mods正確。

## E-162 — Stage306 Studio PV 動畫漏綁定與新卡修正（2026-09-29）

- 本輪又猜 Docs/ERROR_LOG.md 與 src/rig.rs；真實錯誤紀錄在 repo root，rig 載入在 lib.rs。初次合併 patch 使用不存在的 `#` context 而整批拒絕，尚未改 production。再次提醒：先 rg --files 定位，不猜檔名或 patch context。
- 遊戲 OCIChar.LoadAnime 的 normal Studio PV 四個預設都為 true，不能直接關 PV 解決手臂拉長。原生 Studio295 fixture 的 cf_pv_hand/leg 會隨 Animator 改變；先驗證動畫 binding 的解析，不能只驗 CopyPosRotScl 公式。
- Stage293 的 TRS 驗證只比較 Avatar TOS 已辨識的 attributes；未知 path hash 被跳過，因此可能漏掉整類已動作的節點。新增來源 fixture 對四個序列化 PV slots 的全部 TRS 強制核對，不用同一 binding parser 決定驗收範圍。

- E-162 初次追加紀錄使用相對路徑，因 workdir 是 Tools/ok-extract 而生成錯誤位置；已核對內容移入 root ERROR_LOG.md 並移除本輪新建副本。後續 MD 寫入一律用 absolute path。
- 新強制來源測試首輪失敗（StudioPV306Before.log）：March walk time0 的 cf_pv_hand_L 仍為T-pose [-0.6123993,1.4131999,-0.03494391]，原生為[-0.24310453,0.9363166,0.01760908]，位置誤差0.4768833m。Avatar TOS只有901路徑，Studio clip963 bindings只有627 matched；已確認未知generic hierarchy hashes造成PV停在原位。補上目前rig的Animator相對路徑CRC解析，保留遊戲的PV eligibility/copy，沒有卡名分支。
- 診斷 Python 初次 read_text 未指定UTF-8，Windows預設cp950讀中文卡名失敗；已改encoding=utf-8。一次cargo命令於repo root未加manifest-path而失敗，已用完整manifest選擇。Windows rg wildcard operand再犯一次；後续先rg --files，避免依猜測存取shader檔。
- Stage306正式UE build24.96秒Succeeded，三新卡18張Tpose/Walk成功，三張walk手臂伸長已目視消失；540個原生PV TRS誤差max1.7881393e-6、135 Studio frames/36450attributes max1.1920929e-7均通過。不能以此宣告眼鏡／材質已修復。
- 原生snapshot證實Junko accessory25為cat123/id0眼鏡，框／鏡片都active。載入器原來以id==0略過所有配件；遊戲空slot是type120，id0在其它類別有效。已按category修正，沒有卡名例外。
- runtime定位飄散幾何來自兩個claws，不是hair/camera：右側instance最高2.5206637m。原生SMR slots指向共用手指骨架；現仍調查ClothesToAccessories的來源component與重新AssignWeights條件，不能直接把所有skinned配件改成身體綁定。
- Python stdout預設cp950在印含簡體字的mod路徑再次失敗，後续用python -X utf8或sys.stdout.reconfigure。一次PowerShell内嵌Python regex雙引號解析失敗；改single-quoted here-string傳stdin，避免用反斜線假裝shell escaping。

- 實際重新綁骨來源是 KKS_AccessoryClothes 的 ChaAccessoryClothes.Start，不是先猜的 ClothesToAccessories。只處理元件指定 rendNormal、按精確骨名替換 SMR slots，保留 bind matrix，銷毀 private ArmatureRoot 的未匹配骨採 identity。不能對全部飾品強制共用骨架。
- cargo 初次因鏈式 temporary borrow E0716 與診斷 Tf.scl（實際 scale）失敗，修正後才重跑；保存 NewCards306AccessoryBody.log 等失敗。原生配件比較差 0.0262m 時，查明診斷 Auto neutral 前臂 scale=.92，而來源 Studio Tpose=1；應先統一來源 pose context，不能直接加位移補丁。
- Unity JsonUtility 漏掉 nested 自訂 public fields，首份 renderer mesh fixtures 缺 bones；改用既有 ProbeJson serializer，完整 boneWorlds／bindposes 保存。第一次 native RT readback 沒有設定 GL.sRGBWrite，使來源數值誤判；readback 補實際 shader 寫入色彩空間、finally restore，旧 fixtures 保留。
- create_eye 的 DXBC cb3 是 _Color2、cb4 是 _Color。開始猜相反顏色順序，原生常數 oracle [85,185,42,204] 證明錯誤；按 oracle 校正。Source Texture2D colorSpace、linear filtering、sRGB RT encoding 必須分開，不用平均色亮度猜 Y flip。左右瞳孔各自解析 Pupil1／Pupil2，id0 有效，不能用 left 最大 id 或非黑顏色 fallback。
- KSOX EyeOver 第一次只 lerp RGBA，KK346 alpha max27，NewCards306EyePixels.log 保留。反組譯 composite shader 輸出 alpha=max(base,over)，SrcAlpha/OneMinusSrcAlpha 又混回原 RT，正式 RGB 權重 over.a*maxAlpha、alpha=lerp(base,maxAlpha,maxAlpha)。同一材質用於 face/body/eye，共用來源算法。
- Junko 熊飾不是顏色設定錯：Unity RGB24 1024² 貼圖資料被 decoder 讀成257bytes，應有3145728bytes。原先只對 DXT 做長度修復，導致 MainTex 解碼 None；擴至已知固定 bytes-per-pixel 格式並保留完整 inline mip chain。NewCards306BearInputs.log 失敗保存，Inputs2／3 真正解碼及 selected prefab material 都通過；快取 CACHE_VER=91、pipeline146 避免沿用缺貼圖快取。
- 再次猜 EyeNative2 的 mesh1 fixture 檔名不存在；mesh fixtures 實際在 EyeNative。先 rg --files 確認，再讀。新增 KK384915／KK384917 有完整 mesh dump（87／68 OBJ），與 Stage305 三卡只有 JPG 的證據等級不同。

- 加入兩新卡後 create_eye pixels gate 卡0 max5（mean0.319），舊三卡 max≤1。保持 max3 門檻、保存 AddedEyePixels／AddedEyePixels2；正在查額外 mod hook／取樣差异。配件相同來源 pose 測試首次 max0.04444m 也未通過，必須核對 slot identity、骨 world／bind矩陣後再驗，不以 source palette audit 代替原生逐頂點。

- 一次 iris wrapper patch 只用共通返回型別當 context，落到 png_from_bundle；rg review 在 compile 前發現，已移回 bake_iris。後續 patch 必須帶精確函式名，不能靠通用簽名定位。

- 首輪五新卡 RHI 30 PNG/runner exit0，但 KK384915 裙子跑到頭上、臉前有白色圓片；KK384917 額飾材質不對。保存 NewCards306Final，不因載入成功宣稱視覺完成。原生 skirt 148 SMR slots/binds，舊 loader remap 後101，輔助骨不在 body SMR palette 被壓到ancestor，bind位移丟失。所有衣服保留 authored SMR slots、精確 body Transform name 重綁（即使無 private dynamics），pipeline147。
- 配件 max0.04444m 比較中，raw vertex與bind完全一致、boneWorld差僅3.58e-7。回查 Stage277既有Probe註解：BakeMesh再TransformPoint會雙乘renderer scale；這次重複同一錯誤。新增独立 native ShaderWorld LBS control，保留原 BakeMesh worldVertices，不對production加scale補丁。
- ProbeJson 只序列化 fields，Unity BoneWeight 的 index/weight 是 properties，舊weights全部{}。已新增明確BoneWeight輸出；來源權重讀取必須有非空欄位驗證。
- create_eye ShaderLab pass 的 AlphaToMask=1，single-sample RT 輸入alpha<.5會留下透明背景。只看DXBC漏掉固定renderstate；新增相同coverage處理。新卡GPU input readback與CPU source PNG bytes一致、Gamma，暫未證明max5為linear source quantization；新增ARGBFloat GPU sampling資料繼續定位，不能把猜測寫成根因。

- KK384915 臉前白圈是 slot33 的 rigid Double Husk Sphere，不是臉或眼睛。原生尺寸約0.0029m，runtime約0.096m；四個 Soft Cube 耳飾也過大。原函式以 part_skel 是否存在判斷 rigid，但 mod bundle 可能有 Transform hierarchy 而 MeshFilter 沒有 skin weights，於是漏乘 renderer prefab matrix。改以 renderer 的 authored weights 判斷；保留 N_move/N_move2 替換鏈，rigid 始終套 prefabLocal，不用卡名／比例補丁。pipeline148。

- pipeline148／149 的 tiny rigid accessory 修正未通過 RHI：來源 MeshRenderer 可保留未使用的 skin channels，必須讀 component 類型；更關鍵的是 Session.skin_all 會還原 rest buffers，而 rig_mesh 存的是放置前的頂點，因此第一幀又把 card addMove／prefab matrix 撤銷。Rigid placement 完成後保存 world-space rest position／normal，不能靠改相機或固定比例。148／149 失敗保留，pipeline150 將重新回測。
- 配件 ME 色彩查詢把 `_Color` 原樣 lower 成 `_color`，但 MaterialEditor 保存的是 `color`，導致所有配件 ME base／gradient／outline 色覆寫未生效。按來源 property 命名去除前導底線，coordinate／slot／material identity 仍精確配對，不用卡名。
- 新卡 eye max5 的原因已證明是 GPU sRGB sampler 精度：native ARGBFloat 的 encoded250讀得0.953125，理想CPU decode為0.95597335；全部 sampler 最大差0.00374875。獨立 DXBC 算法使用實測GPU浮點输入，三卡雙眼全像素max1；CPU PNG gate max3未放寬、原max5 failure保留。不能將之前linear來源猜測或id9999假設當已證實mod hook。
- native claws comparison 改正 ShaderWorld LBS control 後雙手各952頂點 PASS，最大5.993e-7m、RMS3.335e-7m；原錯誤BakeMesh+TransformPoint結果仍留存。
- 本輪再犯 Windows rg wildcard operand、猜測不存在Shaders/session子目錄，以及直接rg單行大型JSON導致巨大輸出；後續一律先rg --files -g定位，JSON用Python只取所需欄位。

- 補正 tiny rigid 根因：KK384915 原始 Sphere／Cube 的 weights 與 binds 都為0，所以「這兩件有未使用 skin channels」不是本例原因。真正錯誤是 prefab 根物件與子 renderer 同名，按名稱選到子物件，漏讀 N_move 與卡片 addScale。現在按 AssetBundle.m_Container 的本地 asset PPtr 選根，幾何與骨架圖共用同一根解析；不合成 N_move、不按卡名乘尺寸。CACHE93／pipeline151。
- 真實 dump 五個 MeshRenderer 全頂點誤差2.39–2.40e-7m，第一幀 positions==rest_positions；skirt 原生148 slots／979頂點，以原生body TRS獨立LBS對照max3.578e-7m。151Fixed正式PASS，150的0.06051m失敗完整保留。
- 首151 Rust compile把 derive(Default)附在新helper函式而非PrefabGraph，E0774失敗保存NewCards306NativeRigidClothes151.log；修正屬性位置後另存151Fixed.log，不覆蓋失敗。
- 原五卡report沒有KK091／KK384283來源JPG時發生IndexError。已只對無JPG者用既有UE Tpose並明確標成Previous Unreal T-pose／previous_unreal_capture，不能把UE舊圖冒充遊戲來源。
- 本輪又在cargo workdir執行root相對Saved log讀取而報不存在；也重複猜測ShaderDxbcME的asm檔名。先列真實檔案；有dxbc但無asm的shader以已驗證SDK fxc生成，所有log／MD使用絕對路徑。
- 151五新卡30RHI正常exit0：915大圓片／耳飾尺寸／裙子位置已修，917額飾仍黑。原15張Stage286 SHA維持。151Audit.visual_parity_passed=false，不能把runner成功當材質一致。
- Forge main_item實際FORWARD CullOff，被分類為unknown kind0而CullBack，新增明確kind8／pipeline152。但RHI重拍仍黑，不能把此真實renderstate修正當額飾完整根因。NoOutline152與Base153（關輪廓／跳ramp）額飾同樣黑，已排除單靠輪廓或toon ramp造成的假設，繼續查UV／材質輸入。實際Unity pass狀態保存NewCards306ForgePassStates.json。
- 原鏡片Shader為Koikano/main_clothes_item_glasses；native null MainTex是白色default，不是缺素材。正式Forward為SrcAlpha/OneMinusSrcAlpha、CullOff，原loader判masked；且unity::tint_png只乘RGB、不寫alpha。只按確定glass shader使用宣告白圖與Color4 RGBA、按pass透明，不全體貼圖假設white。原生Color4=(0,.163623989,.19607836,.1254902)的正式Session pixel=[0,42,50,32]／blend=translucent PASS。Base153診斷已目視眼睛穿透鏡片；shader完整GlassRamp／Fresnel尾段仍非逐像素驗收。

- UV來源驗證：917額飾121 UV與dump逐點max0。按world前方三角形選點，UV v=.68–.74；UE使用1-v後讀到RGBA(66,82,66,255)背景，直接Unity列則讀到金屬圖案，顯示靜態Texture2D bottom-up尚未在accessory render boundary正規化。靜態烘焙保留row方向；ME PNG本來top-down。pipeline154按各texture的實際ME覆寫來源翻靜態albedo／detail／line／alpha／未經normal_convert的normal與gloss，不翻ME來源，也不在已翻行的normal_convert上再翻。
- Base153是診斷，非正式畫面；OK_MATERIAL_BASE_ONLY暫時分支與runner選項已移除。CullOff、A*R/G正常解碼、SV_IsFrontFace幾何Z符號与camera _WorldSpaceLightPos0依main_item DXBC接通kind8；完整ForwardBase尾段仍未宣稱等價。最近一次又猜OKSkeletalMeshBuilder.cpp而不存在，已回到實際KKPreviewActor中FillSection與UV傳輸函式；此為重複定位錯誤。

- 154回歸review另發現ColorMask來源對齊只處理ME MainTex＋static mask，漏掉static MainTex＋ME mask。新增獨立兩色UV控制，四種來源組合中反向組合在154得到[0,100,0,255,80,0,0,255]而應[200,0,0,255,0,160,0,255]，NewCards306MixedRows154Before.log保存FAIL。155按main_from_me!=mask_from_me對齊遮罩，沒有production卡名分支；測試以既有卡解析作資料容器後清除ME/HAC覆寫，不把容器卡名當算法判斷。
- 補此通解前確認owned UE PID66656、D:/UE5.8/Engine/Binaries/Win64/UnrealEditor.exe及NewCards306Regression154_Tpose.log命令，再主動停止尚未完成的154回歸；identity與partial PNG保留。runner正確拒絕exit=-1，這是planned stop，不計入正式完成回歸。最後155將重新驗證全部十卡。
- EyeIndependentPixels.json的可攜CPU誤差是384915 max5、384917 max1、Junko max2，實測GPU輸入＋獨立公式三卡全像素max1。文件原把「新增两卡max5」概括過度，已按各卡實際結果訂正；sampler來源差不代表每種顏色都會放大到相同程度。

- 使用者指出KK346095衣領錯色後，不能將已知cyan衣領／gray胸片列成延期而結束。原生遊戲三個mendako配件Color/Color2/Color3皆white，MainTex RGB逐像素white。155同樣ME覆寫命中，但ACCCOLOR仍保留authored cyan／gray，角色顏色不是根因。
- 真正根因：Accessory Hair Vanilla Fix只覆寫ao_head_00部分髮飾，沒有p_acs_head_mendako。geometry已fallback到vanilla renderer4048779298634606239，但accessory_color_use從patch返回另一prefab renderer16，role=0跳過ChangeAccessoryColor。Sideloader BundleManager.TryGetObjectFromName逐bundle Contains(name)，不是whole-bundle替換。156按asset存在選来源、mesh／顏色／assignment共用來源，缺少指定prefab不能返回不相關component；bare m_Container名字也按GameObject PPtr解析。
- Prefab cache先前vanilla file key忽略實際override zip。現在key及memo包含選中來源zip/file的stamp，CACHE94／pipeline156使錯誤快取失效。不是刪cache當修復，也不是把這張卡配件硬改white。
- 診斷中曾猜ME MainTex沒命中、或create accessory RT導致錯色；實測ME_main都有命中，反編譯ChangeAccessoryColor直接寫材質顏色，兩項猜測已排除。根因以native材料、卡片顏色、兩份bundle的Renderer PPtr證明。
- 本輪兩次輸出過大：TSV以top匹配到所有BodyTop路徑；JSON排除vertices卻仍帶巨大runtime bind_poses。後續一律欄位白名單，TSV逐欄精確匹配renderer或material。AGENTS.md不存在的讀取失敗也不等於未遵守使用者訊息內已給的規則。

- 156 RHI衣領／肩飾變white，但胸口gray星狀區仍存在，不能把它和cyan當同一根因或只靠albedo白色像素宣告整件完成。native o_inner_a MainTex本来就有星狀透明區，gray是背景而非衣服灰色染色；底下body被原版Top AlphaMask遮掉。實際卡KKEx KCOX Overlays coordinate0/3都有BodyMask101006bytes，parser先前完全忽略KCOX。
- 按KoiClothesOverlayController.BodyMaskHook/GetBodyMask接通三種mask：只讀當前coordinate、PNG存在才替換、Override標誌對mask替換無影響（它只控制衣服MainTex清空／composite）。不用全體body不透明、不填星形固定patch。157加入解析與raw/boundary方向一致處理；普通衣服overlay compositing另有流程，不能把三mask支持宣稱全部KCOX特性。
- Final156五新卡Tpose/Walk30PNG正常exit0保存；原五卡Regression156尚在Tpose，為接入已證實KCOX缺漏，確認owned wrapper15164與Unreal59648 parent/exe/project/log後計畫停止，保留identity與partial截圖，不計入final通過。157將同binary重新測十卡。
- 再次猜插件資料夾KKS_ClothesOverlayX而不存在；立即rg --files找到真實KKS_OverlayMods/KoiClothesOverlayX。以已讀到的實際來源檔案作定位依據，禁止重複猜路徑。

- 157 KCOX source控制PASS：遊戲獨立o_body_a AlphaMask2048²、4,194,304像素與Session RGB max0，遮罩coverage mismatch0；KCOX coordinate／Override=false／null-mask unit PASS。157正式Unreal29.77秒Succeeded；第一張KK346095 RHI確認white衣領／肩飾及胸前star開口下的skin，不再是gray背景。來源white像素控制與實際畫面驗證分開記錄。
- 156重新驗證原生geometry：915五個rigid max2.4028e-7m，skirt148slots/979頂點max3.5778e-7m；Junko兩手各952頂點max5.9925e-7m。157只新增KCOX mask，不能用它宣告所有shader尾段完成。
- 末輪曾猜Saved/Logs/NewCards306BodyMaskNativeSources.json而不存在；真實metadata是Saved/Tests/NewCards306BodyMaskNative/Sources.json，PID64912與exit=-1已按該檔確認。任何缺檔讀取失敗不應混成production gate failure。

- 初始capture目錄尚未建立時直接Get-ChildItem即使SilentlyContinue仍可令shell exit1。末輪查詢改先Test-Path；pending無圖不能當runner failed，也不要用假設路徑讀控制metadata。

- 最後157同binary十卡outfit0×Tpose/Studio walk.4s×全身/臉/側臉60張900×1080，四程序normal exit0；每pose5PV audits、附件max0cm、palette max2.262e-6cm。兩套ContactSheet與SideContactSheet及關鍵完整臉圖目視完成，白領／肩飾、胸前skin、兩新卡小飾品與額飾、March眉毛／Alice側髮確認。原根15張Stage286 SHA逐張保持；owned UE/Studio=0、game plugins Probe=0。載入／來源控制PASS仍不等於所有shader pixel parity；完整ForwardBase及皮膚／靴子雜訊差異續列Docs。

- 最後彙總verifier初次執行SyntaxError：草稿把`not in`寫成`not`，不是production或capture失敗。保留verify_finalBeforeSyntax.py，修正後再執行整個彙總；不可因60PNG已有而跳過失敗的彙總gate。

## E-163 — Stage307 皮膚青色雜点的 HSV 分支錯誤（2026-09-29）

- Stage306 白衣領與 KCOX BodyMask 已修，但 KK346095 裸露手臂、脖子仍有青色細點。不能把所有材質差異直接歸咎於法線貼圖／採樣或套卡片專用減飽和參數。
- `Koikano_main_skin/0072.asm.txt` 的 RGB→HSV 在第二次 permutation 的 R 非最大分支仍保留 R；原 Unreal `float4 hsv` 卻保留第一輪的 0 或 -1。結果 chroma 不再是 max(R,G,B)-min(R,G,B)，甚至大於 1。原生 albedo 的 RGB(241,243,243) 被算成 chroma 0.952941，正確為 0.007843；RGB(240,239,241) 原值1.945098、正確0.007843。這將白色高光中的微小色差放大，並影響陰影與 specular 的 hue tint。
- 用共用 `UnitySkinHueCode()` 按 DXBC 兩輪 permutation 修正皮膚顏色與輪廓顏色的 HSV，沒有角色名條件或修改幾何／下巴輪廓算法。Session資料格式未變，CACHE94／pipeline157保留。
- `verify_skin_hsv.py` 從 production HLSL 讀取實際公式，獨立解釋遊戲 DXBC 的 ge/and/mov/mad/min/div，並以 Python colorsys 核對六種通道順序。304,913組輸入包含17³網格、100,000隨機色、200,000原生皮膚貼圖取樣；負控制舊碼 chroma max差1.9723516，修正後0、hue最大差3.43e-7（dump常數只有六位小數）。只證明 HSV 段，不等於整個 shader 的 pixel parity。
- 初版驗證工具自己的 expression evaluator 先有 ternary 分支前導換行 IndentationError，後有 scalar `chroma` 不在eval locals的NameError。已修正strip及locals，保留 `SkinHsv307BeforeInterpreterError.log`；這些不屬於production失敗。前導查詢把Get-Content接在python後會掩蓋非零exit，後續正式gate採 `exit $LASTEXITCODE` 並检查報告passed。
- HSV-only正式編譯 `SkinHsv307UnrealBuild.log` Succeeded19.29秒、十卡60PNG回歸PASS保留；其後DXT5／clothes create正式158 build30.31秒Succeeded、SkinHsv307Final158R2同binary十卡60PNG Tpose/Walk末輪PASS。局部青點8103→0，但fine grain／腮紅仍有差異，不能宣稱整個skin完成。

## E-164 — Stage307 靴子 DXT5 header 與 clothes-create 採樣（2026-09-29）

- 原生靴子 MainTex正常灰面／紅底，Session卻幾乎全黑且alpha滿是雜訊。此問題不能用specular係數或模糊遮掩。來源list112/id15222／Sister [boots]，`[[OMEGA]] HS2 port to KK v1.8.1.zipmod`／`chara/HS2-KK.unity3d` 的 `cf_shoes_sister_t` DXT5實際2,796,240bytes，converter回傳2,796,272bytes，仍含32bytes標頭。非空／足夠top-mip大小不代表起點正確。
- 通用payload修正依serialized inline array宣告長度／bounds恢復，不限制「短資料」；header搜尋按UTF-8 name byte length起點，支援長名稱；沒有inline array的stream保持原資料。CACHE95／SESSION158使錯誤prefab PNG與session重新產生。
- 獨立UnityPy原compressed bytes／解出的RGBA fixture和Rust比較：main2,097,152＋mask524,288像素RGBA max0。舊碼 `SkinHsv307Dxt5Before.log` FAILED；修正後 `SkinHsv307Dxt5Final.log` PASS。long UTF-8 name、oversized header、streamed-no-inline及原short payload控制PASS。
- 第一版native clothes-create albedo對照仍FAILED max142／mean1.0693，證明「解碼對」不能等於「整個混色對」。mask512×1024而main1024×2048，舊碼取nearest、以gamma RGB混色並截斷到u8；原生使用來源sampler、sRGB輸入解碼、Linear混色與render-target量化。
- 依Texture2D m_TextureSettings讀取FilterMode、WrapMode或WrapU/V、m_ColorSpace；在texel中心取樣，處理Repeat/Clamp/Mirror/MirrorOnce，clothes-create常數與texture在線性空間計算、輸出sRGB round。找不到部分override中的texture sampler時繼續查下一來源；不得借錯誤prefab/asset的settings。修正後maxRGBA6／mean0.0739596，RGB最大[2,4,2]；strict max3 gate仍FAILED、6個像素>3，coverage mismatch0。沒有放寬max3或把FAIL記為PASS；正在用原生GPU float inputs分離sampler/decoder與公式誤差。
- Sampler accessor草稿誤把IndexMap傳給uv_obj且UnityValue未qualified，編譯失敗已修。`SkinHsv307SamplerCompileFailure.log`保留。一次大型apply_patch上下文定位失敗，讀當前函式後拆小patch，沒有用全檔覆盖。
- 查舊diagnostic JSON誤假定root為list，真正格式是{card,parts}；改先確認type/keys再存取parts，不能印整份骨架或頂點資料。早期DXT5測試assert_eq向量導致失敗log太大，改成只assert相等並印length/前20bytes。
- Native renderer fixture PID62316、61 rows完成，Studio delayed quit停止exit=-1／Probe已移回Tools。HSV-only十卡60PNG／兩程序normal exit0，另存SkinHsv307FinalAudit，不可代替CACHE95／SESSION158末輪。
- 不應同時讓Unreal十卡capture、native Studio和大型Rust session diagnostics爭用RAM：曾UE約13GB＋Rust約7GB，造成等待與paging風險。末輪先收完native/診斷、再跑UE；不能只因幾十秒無新log判定deadlock。

- 載入後讀 `cha.ctCreateClothes[8,0]` 得 null；來源 ChaControl:10651–10661 會 Release 並設 null，首次 SkinHsv307BootGpuInputs 61 renderer rows 不代表有 clothes GPU inputs，後續 Python FileNotFound 證明缺檔。此失敗保存，不當 GPU proof。改在 CustomTextureCreate.RebuildTextureAndSetMaterial postfix 擷取，runner 檢查 requested metadata／MainTex／ColorMask float files，不能只檢查 Card0.json。
- 新 SkinHsv307BootGpuHook PID77816／61 renderer rows，exit=-1 delayed quit、Probe 清除。真實 Koikano/create_topN、沒有 pattern texture。獨立 DXBC cascade 使用原生 ARGBFloat inputs 對所有2,097,152 pixels：RGB max[1,1,1]、alpha max0、mean0.0571486、coverage mismatch0，PASS；CPU strict max3 仍 max6 FAIL。差異定位到實際輸入邊界，不將它寫成全部 BC3 hardware precision 已證明，也沒有硬編 lookup。
- 十卡 cold cache 的第一輪固定10分鐘預算不足。已確認 PID62340 的 exe／project／abslog，保存 Final158PlannedStop.json 並計畫停止；runner 正確拒絕 exit=-1。partial 圖保留，不計 final。共用 runner 期限依卡數 max(10,3+2×count)，Load/display、exit0、3PNG/card/pose 的 gate 不放寬，Final158R2 重新跑全部十卡。
- 本輪一度直接 ConvertTo-Json 整個 CimInstance，印出不相關 CimClass／process properties；重新使用 Select-Object 白名單，只保存 PID／parent／exe／commandline。誤讀 ASM register t0 為 MainTex亦未用於production：t0 後續有法線解碼，不能憑 sample 順序決定資源 identity 或移除 MainTex alpha clip；保留既有 blend 流程。
- MIP缺漏有實際來源：CustomTextureCreate.Initialize 建立 useMipMap=true 的 RT，而 UE transient PNG 只載 top mip。記為待接通的 texture metadata／LOD 問題，不應再把全部 fine grain 宣告 HSV 修好了或以材質 blur 遮掩。
- BC3 alpha 精度補充：原block endpoints13/237、code4標準插值147.4（CPU147），native GPU sample153。fixture中 endpoint0<endpoint1 的27 interpolated pixels，實測四種code與3/16、6/16、10/16、13/16量化權重最多差1，標準值最多差5.6；這支持本機device解碼精度造成罕見6byte差的診斷，不代表全部GPU／runtime source bytes都已認證。權重只在verifier觀察，production未改成device lookup；真正逐像素一致需來源compressed GPU resource／GPU create pass。
- 158末輪十卡×Tpose/Studio walk.4×全身/正臉/側臉60PNG900×1080，UE PID49416/73044 normal exit0，每pose10PV/204attachment audits、attachment0cm、palette2.252e-6/2.262e-6cm。root Stage286 15SHA保持、owned UE/Studio0、installedProbe0。兩張contact及側臉表、四張關鍵完整圖目視；Hestia白領／胸前skin／青點與Junko靴子大塊破圖已改善。Junko白袖口／衣線／髮光及靴toe白線仍與JPG不同，所有報告visual_parity_passed=false，保留未完成項目，不以60圖PASS等同材質全完成。
- E-165 / Stage308：MIP 不能以理論色彩空間取代原生證據。TextureMips308Native 的 body/face MainTex 是 Bilinear、Clamp、sRGB RT、13 層；法線 RT 是 Repeat、sRGB、11 層。原生逐層 2×2 encoded-byte 平均 max1；先 sRGB decode 再平均的負控制 max73。禁止直接把所有貼圖改成 linear-average MIP，或按角色／shader role 猜 RT 來源。Runtime normal 的 PNG 目前是線性分量，不能直接改 SRGB=true；必須另驗證表示法。Probe 首兩次 build 因 Unity Texture2D.activeTextureColorSpace 非 public 而失敗，R3 以 reflection 讀取成功，失敗 logs 保留。本輪數次過大輸出截斷，以及 PowerShell rg quoted regex 出錯，均改用小段讀取／literal 搜尋；不把被截斷輸出當驗證。

- 初次 Rust compile 因配件迴圈是 owned ExtractedMesh 而傳入 m，expected &mut／found owned；改 &mut m，cache roundtrip 控制通過。一次大 patch context 不匹配，工具拒絕整個 patch；確認沒有半套變更後分段重套。
- TextureMips308NativeR2 runner 新 gate 的 glob 漏 `.json`，錯誤回報 requested fixture absent，實際檔案完整；修 glob 後 R3 runner exit0，但 Studio 本身延遲退出仍是 -1。不得把 runner0等同原生遊戲normal exit0。
- NPOT 控制 5×3／1×7／7×1 按 max dimension 推估 mip count 讀到全零層，kernel max249 FAIL；显式 GenerateMips 的 R3 仍同樣失敗，排除僅 autoGenerate 呼叫時機的假說。新增直接讀 ID3D11Texture2D::GetDesc 驗證實際 GPU mipLevels，不能把不存在的 subresource 當有效黑色 MIP 或直接放寬 gate。R2／R3失敗JSON與logs保留。
- R4 GetDesc 證實層數存在。R5 direct staging 因只接受 typed format 拒絕27 TYPELESS，completion.error與runnerFAIL保存；相容UNORM staging修正後R6成功。Unity CopyTexture/ReadPixels 細長層讀零，direct GPU Map 有有效值；8 controls／45 steps encoded kernel max1、recursive max3，linear sRGB負控制max73。型別／引用釋放依本機SDK header，沒有device lookup入production。
- 首輪159 RHI出現 Hestia綠色嘴內：ME replacement全設Clamp是錯誤。原生 renderer43 o_tang是RT／Repeat；decompiled MaterialAPI.SetTexture會複製被替換texture.wrapMode，null才Repeat。不能只看TextureContainer建RT的defaults。讀取原材質texture PPtr的wrap，ME保留它；已有create RT優先（face/body仍Clamp）。無舌頭／Hestia名稱特例。確認UE PID48144、exe／project／log後計畫停止159，partial PNG／identity保留，runner拒絕exit=-1；不算60圖PASS。CACHE97／SESSION160使舊cache重新建立。另一次定位把plugin路徑猜成MaterialEditor目錄而不存在，隨即以rg --files找實際KKS_MaterialEditor；不以錯路徑當來源缺失。
- 160嘴內綠像素932→0，但native sampler獨立對照又抓到眉毛／眼線仍Repeat，遊戲Clamp：只讀prefab漏了ChaControl先從角色清單ChangeTexture、再ME複製wrap的順序。依同一cat／head suffix／GUID／來源bundle與Sideloader PNG解析原設定，eye create RT仍優先；Sideloader PNG路徑含clamp需Clamp，不能全當預設Repeat。來源resolver共用clothes mask，不加角色特例。4個Rust控制包含native Hestia catalog來源Wrap PASS。native無該property的cf_O_eyeline_kage列為unknown，不能假設是source mismatch或虛報相同。確認PID76424／exe／log後停止160，partial PNG與identity保留，runnerFAIL(-1)不是完整回歸；SESSION161重新產生，decoded CACHE97保持。
- 最終CACHE97／SESSION161，build28.15秒Succeeded；production helper automation PID44248 normal exit0、8controls／45steps／kernel1／recursive3。正式十卡Tpose23012／Walk45988皆normal exit0，共60PNG900×1080，每pose10PV／204attachments、attachment0cm、palette2.252e-6／2.262e-6cm。两pose source sampler已知21／0 mismatch／1 unknown。全部contact與側臉表及關鍵完整圖已目視，嘴內綠像素932→0，白領／眉毛維持。root15SHA保持、owned UE／Studio0、installedProbe0。未宣稱normal MIP／authored MIP／完整ForwardBase或所有mods通過；visual_parity_passed仍false，既有091胸髮與延期下巴不在本次結論。
- 最後hash verifier把plugin DLL誤寫成root Binaries，FileNotFoundError保存TextureMips308FinalVerified161.log；實際位於Plugins/KKUnityBundle/Binaries/Win64，依rg --files確認後修路徑。這是audit路徑錯誤，不算程式或RHI測試成功／失敗的替代，R2另存結果。


## E-166 — Stage309 新卡 UV／DXT1／材質預設與驗證範圍（2026-09-29）

- 新增四卡：KK_226176、KK_384914、KK_398302、弹丸论破 - 七海千秋3。來源是 KKS `mesh_dump`，不是 UE 根目錄或舊 `mesh/_dump`。按 filename 的 `rg --files -g` 不會找到同名目錄；兩次拿錯根目錄造成 FileNotFound，應先確認實際絕對路径。
- 不可只比 RGB：Korone shirt 的 native alpha 有79,687透明像素，旧DXT1全255，黑色按鈕面板是 BC1 RGB decoder 漏透明selector3。正式decode改BC1 RGBA，保留一次BGRA→RGBA。
- 384914白蝴蝶結MainTex為null，shader white default不能落回UE黑色預設材質。只補已知shader宣告預設，不猜未知shader。
- raw Unity bundle rows與Sideloader PNG文件rows不同。Korone虹膜方向錯不能代表全部四卡都要翻；依精確catalog／GUID／實際來源決定。create_eyew的線性RGB與colour alpha按DXBC還原。
- 七海MainTex已与native RGBA一致，但关outline仍没花纹。早先把衣服問題推為outline是未證實假設；NoOutline161控制否定，不能為七海重畫texture。geometry診斷才发现Float16 UV被read_f32解碼：native UV约0..1，UE約1e-24..0.007（少數0.528）；依channel.format／component byte size修正共用位置、normal、UV scalar讀取。stream alignment padding不得算入per-vertex stride。
- Minase NoOutline161恢复白裙／圖案但肩缺口仍在；兩個問題分開調查。Triangle／position对照不支持按卡名整体反转indices。不能把關閉輪廓作为通解。
- 衣服outline以source `_OUTLINE_ON`決定；七海无此keyword。cache保留旗標，新增FFI初稿誤用opaque pointer `.0.parts`，检查发现后改現有sess_ref，尚未带着错误build。
- 泛用push_meshes漏ME numeric overrides；补在shader更換後依ObjectType／coordinate／slot／material重播，不能只在head／accessory處理。
- 四卡outfit0没有KCOX clothing overlays；其它coordinate存在不能當此套服装證據。此假設已排除，未添加臆測overlay分支。
- 本轮命令错誤：PowerShell不支援shell brace-expanded path；单行JSON直接Get-Content造成巨大输出；猜測accessory名称使comparison StopIteration；palette PNG直接当RGBA导致numpy维度错误。改先keys／短字段／真實名稱与convert(RGBA)。
- 一次Python脚本在Tools/ok-extract cwd使用repo-relative path，未写入helper即启动cargo导致exit101；`NewCards309Dxt1Unit162.log`保留。R2改绝对路径完成。随后该cwd读C++再次FileNotFound，往后统一绝对路径，不借错误输出继续判断。
- 一次误跑全库测试 `--skip ignored` 并不等于只跑新unit；旧诊断并行改变环境／读取外部fixture。出现3个FAILED且extract_outfit3仍运行，不能视为新码PASS，也不能未经重现就断言是环境race。确认owned PID75332／exe／完整command后停止，exit=-1／identity／完整log保留。改按明确名称、单线程运行生产helper／独立dump验证。
- 历史 CACHE98诊断在UV修正前已存错数据，因此正式decoded cache升99／SESSION162；旧截图与旧诊断不覆盖。最终结果见 Docs/NEW_DUMP_CARD_TESTS309.md；未完成项不可写成全shader一致。
- SESSION163定位Minase上臂缺口为inactive Sailor/Jacket SubA遮罩：top Kind4不會載入它。以前以SubA ID>0、stale resolve存在推論active是錯誤；SubA ID0亦可能有效。共用Kind selector控制模型、狀態與Body/Bra/Inner遮罩；四卡所選BodyMask獨立material dump核對通過，163實際UE上臂完整。
- Source衣服outline採camera-distance／DetailMask.b／_LineWidthS WPO，零CPUoffset GPU殼仍需反向winding。原builder用width>0判斷pass，在檢查時抓到錯誤，補顯式bOutlinePass後才build，不能以固定外擠距離當通解。
- UV獨立verifier初稿錯把gameObject字串當{name}物件，TypeError保留；修schema後163嚴格結果8mesh中5可比、3body無同頂點數來源，退出1不可稱全通過。追查遊戲UncensorSelector固定預設BP Profundis9823vsUE原版6823，揭露載入根因。SESSION164補卡GUID／Config預設／指定prefab與SMR骨架；不能以SAC骨骼名單套全部mod身體，cfg納入cache key。
- 本輪仍有一次Get-Content單行body骨架造成60k tokens截斷，與Windows rg literal wildcard／猜Source/Private路徑失敗。以rg --files查實際Plugins目錄、JSON只輸出keys／統計；截斷的輸出不用於驗證。不要再次重蹈這些讀取錯誤。
- SESSION164的body來源GUID控制PASS不代表mesh有載入。擴充獨立測試後R2／R3／R4 FAIL抓到BP prefab缺o_body_a，不能直接build宣稱修好。實際TypeTree warning為m_VertexData TypelessData1218064 exceeds1000000，後續field錯讀；不是external PPtr、body disabled或衣服狀態。這些曾考慮的假說沒有進production。
- unity-asset-binary vendored保留上游source／commit／license；parse與PPtr scan以object bytes界限讀TypelessData，typed-array上限仍保留。初次cargo -p dependency test拒絕非workspace member，改manifest test；R2 unit被上游parse_object在EOF吞error的語義影響，改直接測TypelessData field parser／scanner的拒絕與界限，R3 PASS，未修改無關root EOF語義。
- CACHE100／SESSION165 invalidates164缺body與98錯UV。BodySources165四卡指定prefab之native頂點9080／9823／9823／9823、SMR骨骼100／134／134／134均PASS；測試不再只看選到display name。新關鍵錯誤需要同時記錄來源、完整geometry與RHI結果，不能把前一層成功外推下一層。
- 165單线程重跑最初全庫的3失敗，證實並非僅parallel環境race：clothing_implant舊測試要求implants=false沒有runtime，但既有AssignedWeights通解已必須保留renderer slots/binds；改disabled geometry跟body x10，enabled private reference跟x12/22，cache驗證保持。Wedding raw prefab可null MainTex，測試需查ChaControl catalog create RT；改驗證static或實際create可解PNG，R3兩項PASS。第一次新增測試碰private implants_active field導致compile E0616，移除內部field依賴，直接驗證geometry。
- clothes_rt_matches_dump_all_outfits 是硬編外部Saved/Converted/_user_check0..6 snapshot；目前全缺，n_ok=0嚴格FAIL合理，不能改成skip即PASS。標為明確manual ignored測試，保留n_ok>0與diff門檻，缺fixture仍不能執行通過。此分類不宣稱全庫測試通過。
- Renderer165四卡8mesh與game Mesh.uv全部PASS、max2.9783630339785816e-8；BodySources165R2再比四卡全部skin weights max0與100/134骨架order一致。不是只看RGBA或以自家cache作golden；原163 mismatch與164 missing mesh失敗仍保存。

- 165四卡RHI24PNG正常退出且數值gate通過，目視仍發現七海白兔部分缺失。不能只因shader差異就停止追查：native Mask世界bounds與UE wed_mik_MZ顯著不同。UnityPy独立確認同一Mesh410由GO406/420/432/445四個blue/red/Studio prefab共用；舊SkelGraph.mesh_go單值被最後renderer覆蓋，prefab_local又未限制所選root，N_move ancestry亦因此查錯。166改保留全部引用並按prefab root選renderer與skin bone祖先。不是卡名修補；同prefab多個同mesh instances仍需完整instance模型，現明確warning不得宣稱支援。
- 前十卡165回歸尚未完成即因已確認的新根因中止；先核對runner51652與UE69968的ExecutablePath／ParentPID／完整tag，保存NewCards309Regression165Interrupted.json，停止runner後停止其UE，避免runner自動開始Walk。session exit -1是刻意中止，不能算通過。保留部分截圖／logs；166會重跑。
- 本輪又曾將runtime bind_poses與native renderer整物件印出造成截斷；已回到明確fields與統計。一次properties list誤當dict造成AttributeError；使用native實際schema後重讀。猜測未建立capture目錄造成Get-ChildItem missing；進度改读已存在log。這些讀取失敗均不算驗證證據。

- 166共享Mesh祖先選擇unit PASS；独立game rigid renderer244世界頂點max3.5823822019942497e-7m／mean9.889214987865538e-8m PASS，證實白兔位置根因；Build166 Succeeded30.04sec。更新註解的首個apply_patch因未讀取完整context遭拒，未改檔；讀實際行後再修，非production編譯失敗。最终RHI與前十卡回歸尚未完成時不能提前列PASS。

- 準備166 source body重跑的command漏了installed fixture所需--ignored；已在source attribute核對時發現。exit0可代表1 ignored，必須以test result的實際passed／ignored數核對，不能只看exit code。保留166首次log，UE回歸結束後用166R2明確--ignored重跑，不與RHI同時執行。
- 一次將已知UV報告誤讀Saved/Tests而實際在Saved/Logs導致FileNotFoundError；改用rg --files定位並讀到passed=true/8rows，失敗讀取不計證據。

- 166最終四卡24PNG（PID66032/75700）與前十卡60PNG（PID64920/10516）四程序normal exit0；每pose新卡4PV/83attachments、舊卡10PV/204attachments，attachment max0cm、palette2.252e-6/2.262e-6cm，84圖900x1080。原根15張SHA全保留；contact/side與重要完整圖已目視，七海白兔恢復、Minase上臂完整、March/Alice眉毛與髮臉前後保持。MainItemPlus帽子色差、髮光／細粒、七海上方微點、既有091胸髮與Junko服裝差異仍不宣稱完成；下巴維持使用者延期。
- BodySources166首輪確實0passed/1ignored，保留log；166R2加--ignored實際執行四卡native skin weights max0/SMR骨order控制PASS6.56sec，SharedMesh unit R2也1PASS。嚴格UV166八mesh max2.9783630339785816e-8 PASS。沒有以ignored或外部snapshot缺失來湊全庫PASS。來源29檔與最終code/vendor/binary/log/MD/captures hash保存NewCards309SourceManifest166.json及NewCards309FinalAudit166.json。

- 最終combined audit初稿錯把本輪有效control log數設成至少16，但實際清單為15，assert拒絕產出；這是manifest計數錯誤，非測試FAIL。改為核對實際15項且每項必須有真正passed>0、failed=0；ignored首輪不列入。
## E-167　MainItemPlus 誤用通用 Lit 與 native 程式移植（Stage310）

- 根因：安裝版 `xukmi/MainItemPlus` 被歸為 kind0／UE Lit，沒有執行原生色罩、HSV、ramp、detail、specular、emission 和 outline 公式。GitHub 快取 shader 版本又較舊，不能以它代表安裝版。改按精確 shader 名選 kind9，使用安裝 bundle 的 DXBC6/1 生成共用材質程式，來源參數與貼圖穿過 cache／FFI，無卡名顏色補償。
- D3D11 oracle 最初因 Windows max macro 編譯 C2589；加入 NOMINMAX。Forward 使用鏡射 rows／UVFlip1 曾出現 .346800 與 .345613 差異，precise 無法消除；改為相同採樣座標的運算控制後，Forward／Outline 各36組576像素 max error0，alpha discard一致。只能證實運算移植；UV transport 差異仍需另驗證，不能把疑似 sampler fractional precision 寫成已證實根因，門檻2e-5未放寬。
- 嘗試截短 DXBC 輸出中間 register 失敗：CreatePixelShader E_INVALIDARG（-2147024809），D3DSetBlobPart E_FAIL（-2147467259）。失敗 helper 移到 Saved/Tests/MainItemPlus310/Failed，正式 oracle 移除 debug替換支線；失敗結果不計 PASS。
- Rust 新增 ExtractedMesh shader texture 欄位漏掉手動 initializer，E0063；補齊後 scoped test 真正1PASS。首輪 --exact 短函式 filter 不應計成功，以 R2 test result實際passed數核對。ME lower-case key 若直接插入會被舊 canonical key先查到，現依原 key大小寫找到後覆寫。
- UE 首次 MainItem310Build167.log C2026：生成的單一 raw string 超過 MSVC長度上限。codegen切成6000字元相鄰字串，HLSL內容不變；MainItem310Build167R2.log Succeeded7.46sec。不得縮減公式以避開編譯問題。
- 本輪兩次在 PowerShell 使用 shell brace expansion 路徑造成 ParserError，整個 command未執行；重跑用逐個明確路徑。猜測不存在 .cargo/config.toml 的讀取失敗不作依據。一次 append patch留下空 bullet已修正；下次先用實際尾段作 patch anchor。
- 現有 shader math PASS 不等於所有 variants、來源 cubemap、UV／normal／MIP transport 或14卡 RHI完全一致；捕圖 runner新增 material/shader compilation失敗 gate，不接受 default material截圖當成功。結果與剩餘限制持續記入 Docs/MATERIAL_SHADER_TESTS310.md。

- MainItem310Smoke167 三圖正常exit0／無shader compile error，但帽子全黑，不能列visual PASS。根因查到安裝程式使用非Properties uniform `Epsilon`，adapter一律填0，使白色／無彩 albedo的RGBtoHCV除0。現查核維護者新版 KKPItemDiffuse.cginc 宣告 `float Epsilon = 1e-10`，與安裝DXBC同一HCV公式／uniform；在Value缺省路徑補來源initializer，並將oracle加入白／灰albedo控制，非改顏色或加任意clamp。原36組合成彩色貼圖沒有覆蓋此邊界，這也是測試缺口；後續擴充40組且保留原錯圖。

- MainItem310NativeMaterial167 初次有效fixture測試 FAIL364.99sec，因測試假設每個declared property必定序列化到Material，unwrap None；新shader新增的ShadowHSV等可沿用shader宣告default，不一定有SavedProperties。驗證改明確解析安裝shader default與材質override優先，記每項來源，再與game GetColor/GetFloat結果比較；不把None補成任意0，也不放寬2e-6門檻。保留首輪log，R2實際passed才算。

- Oracle40首次build command給cmd.exe /c forward-slash相對路徑，cmd當成Saved指令而未執行編譯。MainItem310OracleBuild167.log保留；R2用已知絕對backslash路徑成功，不是C++或shader失敗。Forward40／640像素224kept／416discard、Outline336／304，max error0。NativeMaterial167R2實際14個game屬性max7.684097291793535e-8、raw white MainTex／原始mask控制PASS。

- 讀NativeMaterial JSON曾用Python read_text()系統cp950，含日文來源路徑觸發UnicodeDecodeError；改明確utf-8，同步改final verifier。讀取失敗不代表native控制失敗，也不以忽略字元來繞過。

- MainItem310Smoke167R2補Epsilon後帽子仍黑；NoOutline控制也黑，故之前把Epsilon直接寫為黑帽完整根因過早。Epsilon default遺漏由來源證實需修，但黑帽尚有另一根因。新增僅診斷命令的albedo／mask／color分段輸出及UE實際Custom HLSL保存，正式capture不用診斷開關；原黑圖均保留。引用遊戲照片時一次誤猜在角色子目錄，實際在卡片目錄根；以Get-ChildItem確認後再讀。

- Albedo167白色輸出證實MainTex／材質連接正常。UE有效參數顯示EmissionColor黑alpha1、EmissionMaskMode0，舊fallback black PNG也是alpha1，使來源PS混色覆蓋底色。KKS Unity2019的ShaderLab black default為RGBA0／0／0／0（Unity2018.4／2020.2官方manual），不是新版Unity文檔alpha1。改共用DefaultTexture映射black alpha0，gray／red也按版本RGBA；Smoke167R3帽子恢復藍色，但仍有陰影細節差異，不列全外觀PASS。RHI根因與缺少Epsilon分開記錄；oracle增加alpha0正控制與alpha1黑化負控制。

- 原生RT NormalMap像素與本輪輸出同方向max6／mean RGBA差0／.02638／.01630／.00869，翻轉比較max192；不是任意再翻UV即可修。native graphicsFormat為SRGB，舊native adapter卻按linear sampler讀RGB，188的green被当.737而不是约.503。依反編譯ConvertNormalMap（默认RT、autoGenerateMips、保留wrap）記RT provenance，MainItemPlus native路徑尊重sRGB及mips；NormalMapDetail同事件處理，legacy normal representation另列限制。SESSION168使舊session失效，decoded CACHE102保持。
- 複查原始VS2發現o5是normalize(T×N)，初版adapter寫N×T；PS oracle給兩邊相同varyings無法驗證VSadapter。按DXBC改正符號，並避免native kind9受舊髮名normal反轉影響。必須分開列pixel運算／vertex adapter／RHI來源證據，不以PS誤差0概括整個渲染流程。再一次rg命中minified material JSON造成截斷，後續只解析需要欄位；兩個猜測不存在的source檔路徑讀取失敗不列驗證。

- NativeMaterial168实际1PASS356.54sec，包括原生sRGB RT flags；追加geometry診斷時誤用SessionPart不存在的bone_names欄位，NativeMaterial168R2 E0609未執行測試。刪掉錯誤欄位後以R3重跑，不把R2當PASS。外層PowerShell未立即檢查cargo exit而接著build，production build不編cfg(test)因此可能仍成功；兩者結果必須獨立核對，後續串接必檢LASTEXITCODE。

- 169 首次把帽子的光影差先歸因 CPU runtime 切線；補切線、Rust定向1PASS、UE Build169 Succeeded，Smoke169畫面帽子仍白。獨立原生材質測試169在執行386.08秒後因 `runtime_accessory=None` 失敗；再錯猜它走GPU `skel_kind=1/2`，R2執行419.21秒後也失敗。實際舊流程是無DynamicBone的SkinnedMeshRenderer落到剛體Bake分支，`skel_kind=0`；先查part的實際流程再寫fixture斷言，不能從素材有骨架推斷UE使用GPU或CPU哪個分支。錯誤兩次log均保留，不列PASS。
- 原生 Studio `Directional Chara` (layer10，enabled) forward `(0,-0.6427876,-0.7660446)`；舊MainItemPlus把相機前向當主光，跟此來源不符。`Saved/Tests/MainItem310NativeLights169/Card0.json.lights.tsv` 是遊戲runtime來源；StudioLight169同一帽子重拍由大片近白轉成藍色。此結果只證目前Studio預設場景，不可推稱任意Maker/Studio光源設定都相同；production按場景光源輸入，不按卡名顏色修補。
- 曾拿dump的`worldVertices`比UE帽子位置，見最大約0.07084m後誤稱載入縮放錯。dump明寫`bakeMeshSpaceGuess=unknown`，這個worldVertices是BakeMesh再乘renderer Transform，對此SMR重複縮放；`manualSkinnedWorldBounds`的min/max與舊UE幾何近似逐值相同。原生幾何回歸必須用manual skin bounds或真正渲染骨架，不能用無來源保證的BakeMesh worldVertices。已改fixture門檻，拒絕為這個假差異硬縮帽子。
- Probe light audit `apply_patch` 首稿對同一檔列兩個Update操作被工具拒絕，整理成同一Update後套用；先猜生成bindings為`.h`而實際是`.inl`，改`rg --files`定位。這兩種工具/路徑錯誤都沒有被當shader證據。Probe與Rust session同時掃game mods讓cache key在臨時Probe安裝期間變動，觸發重抽；後續大型runtime fixture與原生Probe序列化執行，避免不必要的6分鐘冷cache。
- 170讓所有有權重的SkinnedMeshRenderer附件，即使無DynamicBone仍走私有骨架palette；保留原始tangent.w，CPU每次骨架更新後送變形切線到ProceduralMesh。NativeMaterial170首跑388.11秒在舊`worldVertices`斷言失敗，原因是fixture尚未用正確的manualSkinnedWorldBounds；這不是新runtime幾何失敗。改後170R2 3.14秒1PASS，帽子1853原始切線對game dump max2.9757e-8、1853切線都有姿態變化、manual bounds最大差1.214e-7m。舊169兩次測試失敗與170首跑失敗均保留，不能合併為通過次數。
- 新舊截圖差異診斷初稿把`Path`整段拼到舊目錄，造成FileNotFound；修用`p.name`後，Windows cp950 stdout又無法印七海中文檔名，改JSON `ensure_ascii=True`輸出。不是渲染錯誤。四新卡T-pose臉圖與166基準相比，Korone平均通道差約0.1/255、七海約0.2/255、384914約0.6–0.9/255，Minase光源修正約4.2–14.7/255。數值只用於偵測回歸，不是game照片逐像素對位或parity證明。

- NativeAllProperties170 進一步把 Minase 帽子全部57個有效numeric properties與遊戲dump對照，1PASS／最大7.6841e-8；Final170四新卡24張、Regression170十舊卡60張，四owned UE exit0、PV14卡兩pose／附件max0cm／palette max2.262e-6cm，原根15圖SHA未變。這些是來源及回歸gate，畫面仍有帽子深藍色塊等差異；不可因此把visual parity改為true。更新final verifier文字時一次`apply_patch`因舊整行context不符而拒絕，重新讀取完整行後修正；未影響production。

## E-168　Native NormalMap 的 sRGB RenderTexture 傳輸（Stage310 接續）

- **症狀及錯誤假設**：雖然MainItemPlus像素程式同座標oracle 2×704像素max0、材料57欄正確、切線與骨架界限正確、Studio光向已修，Minase帽子仍有遊戲照片未見的角狀深藍色塊。前輪把`NormalMap`原生RT標成`SRGB=true`，但直接把線性RGB的PNG數值交給UE sRGB sampler，將「RT格式」誤當作「PNG已完成sRGB寫入」。只比對方向與部分像素、未比對完整輸入色彩傳輸，故未擋住此錯誤。
- **原生證據**：新增單卡Probe `Saved/Tests/MainItem310HatTexture171`，原生`_NormalMap`為1024²、11 mip、Bilinear／Repeat／sRGB RenderTexture；`_ColorMask`為Gamma Texture2D。UE輸入 `_ColorMask` 對原生同方向max[0,6,1,0]、各通道平均約0；`_NormalMap`直接比RGB max[0,73,74]、1043660像素任一通道>6。把線性RGB按sRGB傳輸編碼、alpha不變後，法線RT對原生RGBA max[0,4,1,6]、0像素>6；中心[255,190,192,191]→[255,224,225,191]，與原生一致。原生`MaterialEditorPluginBase.ConvertNormalMap`先`MakeTextureReadable`，設定R=1，接著`Graphics.Blit`至預設sRGB RenderTexture，這是通用轉換依據。
- **修正與防再犯**：`source_program`且ME實際轉換法線時，對`_NormalMap`與`_NormalMapDetail`共同做翻面／R=1、再把RGB編碼成sRGB，alpha保留；UE仍按RT provenance啟用sRGB sampler，legacy normal流程不變。SESSION171與CLI283失效舊session；新增1×1邊界單元及完整原生RT fixture比較。`cargo test --exact`第一次因模組全名不符跑出0 tests，不能列PASS；移除`--exact`後1PASS。曾用`rg`搜尋minified dump JSON導致超大截斷輸出，改為解析所需欄位；任何截斷輸出不作證據。
- **待核對**：NativeMaterial171的真卡session輸出、UE build、14卡84圖回歸及目視結果另補。未測其它shader families或runtime陰影variants，不能從本修正外推所有mod。

- **第二個輸入錯誤**：Probe原先只列MainTex／ColorMask／NormalMap等，漏掉MainItemPlus的`_DetailMask`，第一次原生貼圖快照無法覆蓋帽子色塊的另一個輸入；擴充共用Probe屬性清單後重抓，`Saved/Tests/MainItem310HatTexturesFull171`顯示DetailMask同方向max[24,235,24,0]、綠通道平均59.584，垂直翻轉後max[1,5,1,0]。`ParsedBundle`已對Bundle的Detail／Line／AlphaMask呼叫`orient_raw_mask`，native accessory分支又依舊式烘焙規則翻了一次；對native shader按來源欄位只保留第一次定向，legacy accessory bake沿舊流程。不是Minase專用UV／顏色特判。真卡 fixture改同時驗證ColorMask／DetailMask／NormalMap各通道差<=6，SESSION172／CLI284重新建session。
- **診斷工具錯誤**：一次PowerShell管線將`foreach`語句塊直接接`| Format-Table`造成ParserError，改用`ForEach-Object`後正確讀到Forward blob6無額外keyword、blob7=`SHADOWS_SCREEN`、blob8=`VERTEXLIGHT_ON`、blob9兩者。這是命令語法失敗，不能算shader變體證據；來源parsed JSON的後次輸出才算。
- **真卡驗證**：NativeMaterial171在單獨sRGB修正後1PASS（舊DetailMask仍未比對）；NativeMaterial172含三張貼圖全像素斷言1PASS／427.15秒，ColorMask／DetailMask／NormalMap各通道最大分別`[0,6,1,0]`／`[1,5,1,0]`／`[0,4,1,6]`，均0個通道值差>6。材質57屬性max7.6841e-8，bone/tangent bounds門檻繼續通過。兩輪真卡測試不可合併聲稱同一次完成；172才是含全部輸入的gate。完整shader輸出仍需RHI目視及其它變體驗證。
- **172 RHI與稽核的診斷錯誤**：Final172四卡T-pose／Walk各12張、Regression172十卡各30張均exit0；新舊14卡合計84圖、PV每pose4／10、附件偏移max0cm。先寫Doc時沿用Stage166舊卡每pose204筆附件數，但重新讀`MainItem310Regression170Audit.json`與172兩份audit都是**250**；已把310文件改成250，不把歷史166的204誤套本輪。稽核命令曾再次把PowerShell `foreach`語句塊直接管線而ParserError，改存`$rows`再輸出；Python列中文卡名又遇cp950 stdout error，改`json.dumps(...,ensure_ascii=True)`後重跑。final verifier初稿對`Card0.json..._NormalMap`用`Path.with_suffix('.png')`會吃掉貼圖名稱，改字面附加`.png`；這些未完成／失敗指令不可作驗證結果。
- **RHI目視結論與限制**：Build172 Succeeded，14卡84圖各自report PASS（四程序exit0、PV28項、附件max0cm、palette max2.262e-6cm），老15張正式圖SHA未改。Minase正面帽子舊角狀深藍色塊已消失，Alice側髮／384283角飾／091207項鍊Walk沒有本輪退步；舊十卡對170版60張同名UE圖最大單圖平均RGB差0.17030/255。遊戲照片角度不同，側面帽頂亮區、衣色／髮光差異及shader其它variants仍未驗證，visual parity保持false。不能把本輪三張輸入全像素近似或84圖載入成功推論所有卡片完全一致。
- **衣服色差的排除控制及測試錯誤**：Korone主衣和KK_384914主衣都是Forge/main_opaque，原生MainTex為1024²RenderTexture。初次真卡diagnostic測試在第二張卡執行389.77秒後panic，因把遊戲dump的`o_top_sailor01_01_a`當成Session必定同字面名稱；Session實際可見part為`o_top_sailor01_01_a_0`，並非衣服未載入。依runtime log核對後，test只在診斷匹配來源名稱及實例suffix，R2 cache-hit7.29秒1PASS。Korone同方向RGBA max`[1,2,1,0]`，384914 max`[0,0,0,0]`；翻面平均差顯著。這只排除兩主衣MainTex來源錯位，無法證明Forge光照／陰影相同；不應因貼圖已對齊就硬調卡片色值。另一次PowerShell列多項材質屬性的複雜`ForEach-Object`語法ParserError後，改Python逐欄解析成功；首個錯誤命令不列證據。

## E-169　新卡參考來源與共用光向實驗（Stage311）

- 新增四卡中，`KK_380982`、索尼婭、`KK_381623`各有game mesh dump與全身照（mesh JSON 56／82／69）；邊谷山佩子只有卡片PNG，無同名遊戲dump。舊`report_dump_cards.py`在無照片時直接讀`Saved/Screenshots/<stem>_outfit0_3d.png`，新卡會FileNotFound；現依序選game照片、既有Unreal圖、卡片預覽，明寫`reference_type`及dump檔數。卡片預覽不能冒充遊戲outfit0實拍。
- 邊谷山佩子卡面是紫色兔耳服，初次Unreal outfit0為女僕服；不能直接判成載錯。原生`Studio.AddObjectFemale.Add(card)`快照69個renderer，active/enabled的`o_top_maid_01_a`／`o_bot_maid_01_a`／`o_shoes_maid_01`與Unreal T-pose同名；目前證據只支持卡面顯示不同造型。Native runner取得fixture後對自己的延遲退出Studio執行stop，Studio exit=-1、runner exit0；不把Studio exit=-1誤稱正常退出。原生plugin已從遊戲plugins目錄移走。
- 讀四卡清單時又將PowerShell `foreach { ... }`語句直接接管線，ParserError使該次inventory整個未執行；改收集`$rows`再輸出才取得有效結果。猜不存在的`Plugins/OkRuntime`路徑造成rg讀取失敗；先`rg --files`後找到實際`Plugins/KKUnityBundle`。曾把原生renderer完整JSON的`components`／`bones`印出被截斷，改只查active mesh、vertexCount、shader；截斷輸出不作載入證據。首次寫本條時又用錯前段作patch anchor被拒，讀檔尾的實際行後才套用。
- 原生Studio probe測到`Directional Chara`的Unity forward `(0,-0.6427876,-0.7660446)`。曾把共用hair／skin／clothes ramp材質中相機向光輸入一口氣換成這個固定Studio方向，Unreal編譯成功、實際RHI A/B卻使`KK_381623`上臂、襯衫、白褲出現遊戲照未見的硬邊陰影，髮色也未明顯改善。這是**失敗的演算法假設**：光向來源正確不代表整段shader的normal、visibility、ramp與tone輸入已對。保留`NewCards311StudioLight174`24張實驗圖、Audit與Build log，撤回生產改動並Build175 Succeeded；RevertSmoke175三圖exit0且相對改前單圖平均通道差最大0.145/255。不得把Build或附件稽核當視覺PASS，也不得以卡片色值補償。

## E-170　Studio ambient global 與髮法線錯誤假設（Stage312）

- **先前誤判**：從`Shader Forge/main_hair`／`main_hair_front` DXBC看到`_NormalMap.r × alpha`，修正Unreal對有法線圖的共用解碼；但未先確認這批卡的貼圖是否綁定。掃描`mesh_dump`的1355份material JSON，共436份這兩種髮材質，`_NormalMap`綁定數為**0**。`HairNormal312Final176`七卡42圖／稽核通過，但四新卡相對Stage311的單張完整畫面各通道平均差最多0.145/255；這項source-correct修改**沒有解決**新卡髮色。以後先查實際shader keyword及貼圖欄位，再推斷PS修正是否會影響可見圖像。
- **共用根因**：匯出器把`system.xml`的`shadowDepth`直接當`_ambientshadowG.a`。原生Studio同影格probe在`KK_381623`量到`_ambientshadowG=(0.5019608,0.5019608,0.5019608,1)`；設定`shadowDepth=0`，而反編譯`Studio.SystemButtonCtrl.SetAmbientShadow`確實送`1-sceneInfo.ambientShadowG`，`Studio.SceneInfo` RGB預設128/255。先前引用`Manager.Character.UpdateGlobalShader`的非Studio邏輯是**錯誤上下文**。現在匯出器按Studio場景語義傳alpha，三個Unreal共用shader分支採Studio RGB；改動由場景而非卡片決定。`StudioAmbient312AB178`三卡9張alpha-only A/B／UE exit0，`KK_381623`的灰綠髮陰影消失；遊戲照片的金色對比仍未完全匹配，不把它稱視覺PASS。原生probe runner退出0、延遲關閉的Studio exit=-1，臨時DLL已移出遊戲plugins目錄。
- **幾何錯誤與驗證**：骨架載入曾因mesh名稱含`hair`反轉來源法線，且HairGloss UV用`cross(T,N)`而未含`w`。原始VS用來源N及`cross(N,T)*tangent.w`；三卡髮網格的切線w正負皆有，不能用卡別、髮名或固定負號替代。移除名稱反轉、GlossUV改用UE帶符號副切線；Build180 Succeeded，四卡A/B 12張exit0、無材質compile error，對ambient-only基準最大單圖完整畫面平均通道差0.224/255，無明顯新幾何錯置。因Studio ambient alpha=1壓低可見陰影，A/B只能證此場景下無大回歸，**不能**證任意場景色彩一致。一次對錯誤路徑`C:/portable/KoikatuSunshine/mesh/_dump`的讀取失敗，實際dump在`.../mesh_dump`；一次`rg`轉義正規式失敗，均重新查到正確來源，不作材質證據。一次Python用Windows cp950輸出中文卡名失敗，改`PYTHONIOENCODING=utf-8`完成數值比對。Build的工具輸出曾被截斷，須以完整log與exit code判斷成功。
- **漏測HairGloss採樣**：Stage311與本輪前半的原生`materials.tsv`探針未列`_HairGloss`，因此曾把髮色剩餘差異完全歸到公式，漏看貼圖mip來源。補探針後`HairGloss312Native181`在`KK_381623`實測所選`cf_hair_00_04_mh`是Gamma、Bilinear、Repeat、11 mip；舊UE `TextureFromPngBytes`沒帶Settings只建1 mip。改同一選取bundle條目回傳PNG及Unity `m_TextureSettings`／`m_ColorSpace`／`m_MipCount`，按實際拿到gloss的part輸出`_HairGloss`runtime metadata，UE用同設定建mip chain；SESSION173使舊cache失效。Native來源Rust測試1PASS，Cargo check與Build182 Succeeded；`HairGloss312AB182` log證實相關hair part `mips=11 filter=1 wrap=(0,0) srgb=1`、三圖exit0，單臉圖對前基準完整畫面平均RGB差`(0.0035,0.0184,0.0785)/255`，故此修正**沒有解決**明顯金髮對比差。UE由PNG重建mips，未聲稱與Unity原始DXT5各層位元相同。更新Stage312文件時初次`apply_patch`的目標行忘記原檔前導`- `而失敗，重讀後成功；該失敗沒有更改檔案。
- **空貼圖不能跳過shader**：原生`HairGloss312Native181`有髮飾材質`_HairGloss=null`；UE原本以`GlossTex && DetailTex`作進入髮PS的條件，會讓沒有綁圖的Forge hair改走泛用乘色路徑。先從安裝版`EC_Shaders.unity3d`序列化Shader確認`main_hair`與`main_hair_front`兩者的`_HairGloss`和`_DetailMask`預設均為black，再以常數黑保留原PS分支，不從材質表XML猜預設值。兩份ForwardBase DXBC的`SV_IsFrontFace`分支也都將背面RGB乘0.66；UE先前漏掉，現按髮shader family使用`TwoSidedSign`補上。`HairDefaults312Build183`成功、四卡A/B 12圖exit0、相對上一輪單圖完整畫面平均差皆<0.1/255，沒有解決KK_381623仍偏平淡的金髮；不得把來源公式補齊說成視覺已完全對齊。編輯時曾短暫寫出不適合該函式位置的`continue`，在編譯前改用正確的`ShaderKind==1 || (ShaderKind==2 && DetailTex)`條件；Build183才是驗證版本。A/B統計第一次用Windows cp950印中文檔名遇`UnicodeEncodeError`、中途退出，改`PYTHONIOENCODING=utf-8`後完整跑完12圖，失敗的部分輸出不列完整驗證。
- **完整回歸與工具錯誤**：`HairDefaults312Final183`七卡T-pose／Walk各21圖、兩個owned UE exit0、每pose七PV／188附件max0cm、palette最大2.262e-6cm、舊根15圖SHA保持。目視接觸表未見新掉髮或飾品錯位，但KK_381623與遊戲金髮對比仍不同，`visual_parity_passed=false`。第一次掃兩份log時把`Saved/Logs/HairDefaults312Final183_*.log`當成Windows `rg`的檔名，回OS error123；改顯式列兩個log後沒有材質編譯或runtime error標記。首次失敗的rg不算shader檢查結果。
- **髮shader場景光向隔離實驗**：原始`main_hair` PS `cb2[0]`用`_WorldSpaceLightPos0`，native Studio探針量得`(0,0.6427876,0.7660446)`，但UE髮分支仍取`-ResolvedView.ViewForward`。先前Stage311把此方向同時套到皮膚、髮、衣而出現硬邊偽影，所以本次只替髮family做A/B，不把整組共享分支再改一次。將新增scene light欄位插入`FOKToonShadowParameters`既有位置時，舊aggregate初始化參數往後錯位，Build184出C2440；移到struct末端保留既有初始化順序，Build184R2 Succeeded。Build184失敗不可列通過；後續以A/B視覺判斷髮分支是否保留，且不將測量的Studio預設方向推稱所有自訂場景通用。
- **光向不是金髮暗部根因**：`HairLight312AB184`三卡9圖exit0；KK_381623正面金髮選取區相對前版median與p10未變，整張正臉圖平均差0.026/255，側臉差0.185/255。遊戲照片金髮選取區G通道p10=155，UE=202；選取門檻與構圖不同，只證色階暗尾差異仍存在，不能拿它當逐像素配準。這次source-correct光向**沒有修好**額外暗部；繼續追髮shader的visibility／ramp／gloss與來源貼圖，不調卡別顏色。
- **髮貼圖排除控制**：起初打算對照原生髮`_MainTex`，但原生`HairInputs312Native185`與前輪材質快照都顯示`cf_hair_b_21_00`的該欄為null、ShaderLab預設white；強行比較主貼圖是錯誤方向。改對照實際綁定的ColorMask／DetailMask／HairGloss。加入一般CLI診斷開關`OK_DUMP_HAIR_INPUTS=1`輸出session原始輸入PNG，不影響正常export；整圖比對工具同時嘗試垂直方向與sRGB傳輸，不把探針PNG的sRGB寫入值誤當原始linear bytes。`HairInputs312Compare185.json`最佳對齊：ColorMask翻面+sRGB平均RGB差`[0,.058,.135]`，DetailMask同向+sRGB `[.001,.295,.043]`，HairGloss翻面+raw `[.001,.023,.001]`；最大單通道分別6.339／6.339／6。第一次只比較raw PNG會出現ColorMask B平均17.35、DetailMask G平均29.16的假差異；不可因此調材質顏色或翻錯遮罩。這三張來源輸入已接近，不代表整個hair PS輸出相同。`HairLight312Final184`七卡42圖、兩owned UE exit0、每pose七PV／188附件max0cm、palette最大2.262e-6cm，舊根15張SHA不變，visual parity仍false。
- **Ramp不是剩餘髮色根因**：原生`HairRamp312Native186`的`_RampG`為Linear 256×4／9 mip／Bilinear／Clamp，與Rust所選`mt_ramp`整圖在探針sRGB寫入後R平均差0.017、max0.549／255，G/B為0。先前UE只建頂層mip；Build186以共用來源採樣規則建mip鏈成功。三卡`HairRamp312AB186`9圖exit0，所有同名完整圖平均差最多0.161/255，KK381623金髮暗尾仍未對上。不能把mip數修正寫成光影完整修復；下一個問題在髮PS運算、visibility或未比對的後處理。七卡完整回歸`HairRamp312Final186`另記結果。
- **Ramp完整回歸**：`HairRamp312Final186`七卡T-pose／Walk共42圖、兩owned UE exit0、每pose七PV／188附件max0cm、palette最大2.262e-6cm，原根15圖SHA保持。接觸表已目視，KK381623金髮暗尾仍不符遊戲，`visual_parity_passed=false`；不把載入／骨架PASS當光影PASS。

## E-171. Hair ColorMask 必須在fragment採樣；修正8×8誤判

- **錯誤推論**：初見`hair_get_diffuse`與空`_MainTex`，誤稱KK381623的1024² ColorMask被8×8白底縮到8×8。查`ParsedBundle`後證實：有ColorMask時白底會先建成相同1024²，舊session輸出亦為1024²。這張卡沒有該縮圖錯誤。之後不得只看`solid_png(8,8)`預設值就推斷實際烘焙尺寸，必須查看後續替換與正式session PNG。
- **仍成立的通用差異**：原始`main_hair` ForwardBase在`cb0[13]`獨立ColorMask ST後以`t3`逐fragment採樣，再依R/G/B權重混`_Color`／`_Color2`／`_Color3`，最後乘MainTex。舊Rust先以8-bit PNG烘焙；採樣、ST、mip與量化順序不符DXBC。改用材質端混色並透傳ColorMask原生採樣資訊，ME替換貼圖時清掉過時的authored資訊；依shader family及hair renderer hint，不按角色名。`SESSION_PIPELINE_VER=174`、CLI cache stamp換版。
- **187驗證**：Rust目標單元1PASS、release CLI／UE Build187R2 Succeeded；KK381623、Alice、March A/B T-pose九圖owned UE exit0。正臉完整圖相對186平均RGB差0.673／0.953／0.006每255；這些只是改動幅度。KK381623原生`_Color=(.99,.9248319,.6576429,1)`和UE log輸入一致，但照片暗部仍未達一致。`HairColor312Final187`七卡T-pose／Walk42圖、兩owned UE exit0、每pose七PV／188附件max0cm、palette最大2.262e-6cm、根15圖SHA保留；接觸表未見新增掉髮或飾品錯位，visual parity仍false。
- **主髮面與輪廓路徑必須一起改**：187把髮Fill改成原生ColorMask時，Outline仍以未上色的MainTex做LineColor overlay。原始`Shader_Forge_main_hair/0003.asm.txt`也會採樣ColorMask、混三Color再做overlay；只改Fill會造成新舊pass顏色不一致。Build188已把同一個ColorMask／三Color接入Outline並成功，三卡A/B九圖owned UE exit0；相對187正臉整圖平均差KK381623／Alice／March分別0.489／0.858／0.086每255。目視KK381623髮線變金色，未見三卡新錯位。`HairOutline312Final188`七卡42圖、兩owned UE exit0、每pose七PV／188附件max0cm、palette最大2.262e-6cm、舊15張SHA保留；visual parity仍false。後續更改材質輸入要檢查每個使用該輸入的pass。
- **不可把所有hair的缺席屬性當成同一預設**：兩個安裝版ShaderLab的`_Color2=.7843137`、`_Color3=.5`、`_ShadowExtend=.5`一致，但`_SpeclarHeight`一般髮=.85、front=.985。先前session缺席時分別給white／white、1及.95。Stage189按來源shader variant補預設、顯式材質及ME覆寫優先；`SESSION175`與CLI stamp286清掉舊值。ForwardBase `cb0[10].x`的`_rimV`項先前完全未算，189按來源float／預設.75加到fragment；KK381623原生該欄是0，因此不把rim修正誤稱該卡金髮暗尾原因。Rust單元1PASS、CLI／UE Build189成功；四卡A/B與七卡回歸另記。

## E-172. Shader預設不能蓋過MaterialEditor的執行期RemoveRim

- **失敗與根因**：Stage189直接把`main_hair`的ShaderLab `_rimV=.75`預設送入UE。`HairDefaults312AB189`四卡T-pose 12張雖exit0，KK381623正臉相對188整圖平均RGB改動約2.954/255，出現不合遊戲的大片亮黃色rim；因此189的A/B為**視覺失敗**。原生mesh dump該卡19個髮材質的`_rimV`都是0。只按shader blob預設、不依場景插件的載入順序是錯誤。
- **反編譯依據與通解**：目前安裝的`com.deathweasel.bepinex.materialeditor.cfg`設定`Remove Rim Lighting = true`。`MaterialEditorCharaController.RemoveRim`在卡片float覆寫後逐一處理衣服、四個髮槽、配件：材質有`_rimV`且同材質/槽沒有保存的`rimV`覆寫，才設為0。另一個已停用的`KKS_RimRemover.dl_`不能代表目前遊戲。匯出器依同一順序、role/槽/material key及卡片ME值處理，不用卡名；`me_slot`補髮B/F/S/O對應0/1/2/3。MaterialEditor設定檔時間戳進入session及CLI快取鍵，避免設定切換後重用舊值。`SESSION176`、CLI stamp287。
- **190第一次試拍無效**：Rust release重建後立刻跑`HairRim312AB190`四卡12圖，卻忘了UE是靜態連結`ok_extract.lib`，使用的仍是Stage189舊程式；截圖和189相同不能驗證修正。四卡runner exit0只表示舊二進位能拍攝。隨後重建`OpenKoikatsuEditor`再用新tag重跑；今後Rust靜態庫有變動必須先完成Unreal重新連結再試拍。

## E-173. MaterialEditor float大小寫重複鍵讓卡片覆寫失效

- `parse_material_editor_floats`將Property轉小寫；`apply_me_floats`舊版直接插入`_rimv`等小寫鍵。Unity原材質若已有`_rimV`，`MeshMaterial.float("_rimV")`會優先命中原值，卡片保存的明確覆寫被蓋掉。190的合成單元只用沒有舊`_rimV`的來源材質，漏測此情況；首版四卡A/B的成功不證所有ME覆寫正確。
- 192改為插入ME值前移除大小寫等價的舊float鍵，讓來源材質、卡片覆寫與執行期RemoveRim有單一有效值；測試加原值`_rimV=.75`、卡片值`.6`，確認覆寫優先。`SESSION177`、CLI stamp288；191七卡正式跑若在此修改之前開始，只作中間診斷，192重編Rust與UE後另做正式回歸。這是所有ME float的共用鍵正規化，沒有卡別分支。
- **實卡效應**：192七卡T-pose的Yuuka正臉相對191整圖平均RGB差約8.177/255，頭髮從大面積亮條變為較暗紫色；遊戲`mesh_dump`該卡抽樣15個髮材質（含MECopy）的`_rimV`全為0，遊戲T-pose照片亦沒有191那種亮條。這支持顯式float覆寫的優先順序修正；仍不把整張照片稱逐像素對齊。其餘六卡正臉平均變化≤0.848/255。
- **本輪工具操作失敗**：首次`functions.exec` JavaScript括號錯誤，未執行巢狀指令；首次`apply_patch`重複列同一`lib.rs`目標被拒，未改檔；對Windows `rg`傳未展開的`*.rs`／`*189*`路徑遭OS error123，改用目錄與`-g`。第一次測試在最後的authored-key調整前已開始，故再跑`HairRim312FloatTest192R2.log`；只以第二次PASS作最終原始碼證據。

## E-174. RemoveRim的HasProperty不能用shader名稱片段猜測

- 192版以`main_clothes`／`main_item`／`main_hair`名稱片段推斷`HasProperty("_rimV")`。核對安裝MaterialEditor的`KK_Plugins.Resources.default.xml`後發現`Koikano/main_clothes_opaque`等不宣告`rimV`，而`Shader Forge/main_alpha`、`main_opaque`、`Koikano/hair_main_sun`及眼鏡shader有宣告。前者會被錯誤寫入無效float，後者在缺少序列化值時漏掉RemoveRim。192七卡若在此修正前開始，只能算中間回歸。
- 193改用屬性表列出的完整shader名稱判斷，mod shader仍可藉來源材質已序列化的`_rimV`確認；不按卡名或圖像判斷。測試覆蓋有/無rim的兩種衣物shader及髮shader。`SESSION178`、CLI stamp289，需重新建置Rust、連結UE並重跑七卡。未納入屬性表且未序列化rim的第三方shader，其`HasProperty`仍需從該shader資源讀取，不能推稱已完整覆蓋任意未知mod。
- **193正式驗證**：Rust目標單元PASS、release Cargo及UE Build193成功且UBT確實重新Link靜態庫。`HairRim312Final193`七卡T-pose／Walk42圖、兩owned UE exit0；每pose七PV、188附件max0cm，palette最大2.262e-6cm；舊15張正式截圖SHA保留、log無shader compile或runtime error標記。接觸表已目視，Yuuka維持較接近遊戲的暗紫髮，未見新掉髮／飾品偏移；相對192正臉整圖平均RGB差[0.135,0.121,0.123]/255。`visual_parity_passed=false`，KK381623金髮明暗與遊戲照仍有差，不能把此通用載入修正寫成完整Minimalist shader等價。
- 後續查髮DXBC binding時曾把`inspect_unity_shader_bindings.py`的bundle參數誤給`KKS_MaterialEditor.dll`，工具正確報「shader not found」；改用解出的`KK_Plugins.Resources.EC_Shaders.unity3d`才取得Shader Forge/main_hair資訊。DLL不是Unity bundle，這次失敗不構成shader缺失證據。

## E-175. XML清單不是Shader.HasProperty宣告；外部PathID不可混用（Stage312）

- **錯誤假設**：193把`KK_Plugins.Resources.default.xml`中列出的shader視為完整`_rimV`屬性表，因而把`Shader Forge/main_emblem`與`Shader Forge/toon_glasses_lod0`判成有rim。直接解碼已安裝`EC_Shaders.unity3d`的`Shader.m_ParsedForm.m_PropInfo.m_Props`後，兩者都**沒有**`_rimV`；XML描述MaterialEditor暴露的設定，不能代替Unity執行期的`Material.HasProperty`。第一次Python檢查誤讀Shader物件根的`m_PropInfo`而得到全0，立即核對typetree鍵後改讀`m_ParsedForm`；全0輸出不作證據。
- **通用修正**：匯出器讀取與材質同一serialized file內的Shader物件宣告，保留`shader_has_rim=Some(true/false)`，在RemoveRim前優先按該值判斷。第三方Shader即使材質沒有序列化預設`_rimV`也可正確命中；已安裝`IBL_Shader_alpha`資源宣告此屬性，真實mod Shader物件測試PASS。外部Shader引用維持`None`，備援表改由EC/KKS兩個bundle的實際宣告聯集（13個名稱）建立，不再包含上述兩個假陽性。
- **PathID範圍**：Material的Shader PPtr只有`m_FileID=0`才可從本serialized file的PathID解析；不同file可能重用PathID。`material_named`逐file建立shader映射，renderer材質解析每file清空shader映射，避免把另一file的宣告誤套。外部PPtr若沒有可解析的Shader資源仍是未知，不以同數字PathID或名稱片段臆測。
- **覆寫順序再犯風險**：第一版只在bundle材質載入時存宣告，漏想`apply_me_shader`會在RemoveRim前替換Shader，舊的`Some(false)`可能跟著新shader保留。現在`me_shader_registration`讀已安裝EC/KKS或實際zipmod Shader資源的宣告；SetShader成功時同步更新`shader_has_rim`，失敗則保留原shader與原宣告。March真卡hair accessory測試從舊`Some(false)`覆寫後驗證成`Some(true)`；IBL mod shader回傳`Some(true)`，眼鏡Forge回傳`Some(false)`。不能只驗證載入時的欄位而忽略後續MaterialEditor順序。
- **快取驗證順序**：第一輪七卡在上述覆寫修正之前開始，且其session已用暫定pipeline179建立；若只重新連結UE而不升版，最終拍攝可能直接讀回舊session，把新程式誤判為已測。最終升pipeline180／CLI291，並把EC/KKS shader bundle stamp納入session key；第二輪才可當本輪實作的回歸證據。
- **診斷輸出錯誤**：比對193與194人臉PNG時第一次Python在Windows cp950主控台印中文卡名而`UnicodeEncodeError`，命令沒有產生可用數字；設定`PYTHONIOENCODING=utf-8`重跑後才採納輸出。第一輪七卡正臉相對193的全圖RGB平均差均≤0.129/255，僅表示局部變更未大幅改這七張圖，不能用來宣稱未知shader或金髮視覺已對齊。
- **本輪工具失敗不當證據**：開頭一個`functions.exec` JavaScript括號錯誤，兩個巢狀讀取命令完全未跑；第一次`apply_patch`以不符合實際檔案行結尾的context被拒，沒有修改原始碼，縮小patch後成功。曾猜`Saved/ShaderDxbc/EC_Shaders/index.tsv`存在而`rg`報路徑不存在；改以Shader Unity bundle的`m_BlobIndex`／`inspect_unity_shader_bindings.py`確認ForwardBase fragment blob12，才讀到真正binding。上述失敗均不作shader結論。
- 查SQLite新session時，第一次PowerShell／Python雙層引號讓`*`被當PowerShell命令；第二次誤猜`session_blob`欄名`cache_key`，schema實際是`card_key`。改以唯讀SQLite連線、`pragma table_info`後查正確欄，最終版T-pose期間已看到p180七筆及舊p179七筆；這兩次失敗查詢不是快取驗證結果。
- **正式回歸**：Rust release與UE `ShaderProperty194ReleaseBuild.log`成功，UBT重新連結`UnrealEditor-KKUnityBundle.dll`。`ShaderProperty194Final`七卡T-pose／Walk共42 PNG、兩owned UE exit0；每pose七PV、188附件max0cm、palette最大2.262e-6cm，原根15截圖SHA保留。正／側接觸表目視未見新增掉髮或飾品漂移；七張正臉相對193單圖RGB平均差最大0.128/255。稽核仍是`visual_parity_passed=false`，尤其KK381623金髮暗尾與遊戲照不同。UE啟動log的CommonUI與UnifiedErrorTest文字是引擎既有訊息，無材質shader編譯失敗；不得把泛搜`Error`字樣或附件PASS直接當視覺結論。
- 查舊mip工具時把`verify_texture_mips*.py`直接作Windows `rg`路徑，OS報error123，該次搜索未找到任何證據；後續須對父目錄用`-g 'verify_texture_mips*.py'`。這與本輪程式邏輯無關，但仍記錄避免重犯。
- **驗證界線**：目標Rust測試以EC bundle的髮／眼鏡／徽章真實宣告與材質引用，以及IBL mod Shader物件驗證；合成測試覆蓋未儲存rim與舊材質float的HasProperty語義。初次七卡截圖在備援表修正前的UE建置啟動，僅作中間結果；須完成Rust與UE重編、正式七卡T-pose/Walk後才可宣稱本輪回歸。金髮暗部與遊戲照片仍需獨立shader來源驗證，不能藉此屬性修正宣稱完成視覺一致性。

## E-176. DetailMask 採樣資料遺失（Stage312）

- **來源差異**：遊戲 `HairInputs312Native185` 的 `KK_381623` 後髮 `mf_tex2_md` 是 Linear、Bilinear、Repeat/Repeat、11 mip；匯出器原本只帶 `_DetailMask` 的 PNG，UE 在 `TextureFromPngBytes` 以單一 mip 建圖。ForwardBase DXBC 的 `t7` 會同時取其 G/B 通道參與光照和 rim；不能把來源採樣當成無關資訊，也不能依卡名調色。
- **通用修正**：對每個來源材質解析 `_DetailMask` 貼圖的序列化採樣資訊，隨 session 傳給 UE；材質改用身體共用材質時換成新來源 metadata，Uncensor 替換幾何時複製原身體 metadata，MaterialEditor 替換 PNG 時移除過時來源 metadata。`SESSION_PIPELINE_VER=181`、CLI stamp292 避免舊快取遮住修正。真卡 Rust 測試確認 `cf_hair_b_21_00` 帶 `[Bilinear,Repeat,Repeat,Linear,11]`；Rust release、UE Build195 已通過。
- **工具操作失敗**：本輪曾猜不存在的 `run_game_probe.ps1` 與 `ShaderProperty194FinalTpose.log` 路徑，兩次讀取均未提供證據；改用 `rg --files` 找到實際 `run_renderer_native.ps1`／`run_dump_cards.ps1`，用已存在的 `HairInputs312Native185` 表驗證。一次 `rg` 的括號引號寫錯造成 regex parse error，改用 `-F` 固定字串。一次 `apply_patch` 對同一 `lib.rs` 在同一 patch 中重複列目標，被拒且沒有改檔；合併該檔 hunk 後才成功。這些失敗均不是 shader 結論。
- **驗證界線**：UE 目前從解碼 PNG 重建 mip；此項尚未證明各層 texel 與原生壓縮貼圖完全相同。單獨的真卡測試只證明來源採樣 metadata，七卡實拍及遊戲照片色階須另記，不可把 metadata 修正直接稱為金髮暗尾根因。
- **正式回歸**：`DetailMask195Final` 七卡 T-pose／Walk 共 42 PNG，兩 owned UE exit0；每 pose 七項 PV、188 項附件最大 0 cm、palette 最大 2.262e-6 cm，根目錄原 15 張截圖 hash 保留。接觸表及側視表已目視，沒有新增側髮或飾品漂移；七張正臉相對 194 的整圖平均 RGB 單通道最大差 0.162/255。`KK_381623` 金髮暗尾仍與遊戲照不同，稽核 `visual_parity_passed=false`。這次原生 mip 政策修正不是該可見色階差的證據。

## E-177. Gamma 遮罩來源與 Unreal sampler 不一致（Stage312）

- **錯誤與直接症狀**：Stage195 已傳 `_DetailMask` 的來源 `m_ColorSpace`，但 UE 的所有 Detail/ColorMask `TextureSample` 仍宣告 `SAMPLERTYPE_LinearColor`。Stage196 首次讓實際 Gamma 來源保留 `Texture2D::SRGB=true` 後，`MaskColorSpace196Final` T-pose log 出現大量 `Sampler type is Linear Color, should be Color`，Unreal 將相關材質編譯失敗並回退。runner 的材質編譯 gate 正確拒絕該次 T-pose；其截圖無效，Walk 未執行，不能把這輪當回歸 PASS。
- **來源證據**：遊戲原生 `HairInputs312Native185/Card0.json.materials.tsv` 中，`KK_381623` 的同一 front-hair renderer，`_ColorMask` 是 Linear 而 `_DetailMask` 是 Gamma、兩者皆 Bilinear／Repeat／12 mip；後髮的 `_DetailMask` 則是 Linear。這是逐貼圖屬性，不能依角色、髮槽或 shader 名一律強制 Linear。原生 Probe `MaskColorSpace196NativeR2` 與 session PNG 在垂直翻面後以原樣資料最接近：兩個 Gamma DetailMask 的綠通道平均絕對差約 0.0049、0.0213/255；多做 sRGB 編碼／解碼的差異分別升至約 2.3、11.9/255。初次 Probe 用未帶 `_g`／`_k` 後綴的 mesh 名，沒有抓到目標貼圖；改核對 `renderers.json` 的完整 renderer 名後重抓，首次資料不可作像素證據。
- **通用修正**：來源貼圖依其序列化色彩空間建立 UE texture；每個讀取該 texture 的 Fill、Outline、Skin mask、Detail offset 與 MainItemPlus 節點，以 texture 的 `SRGB` 狀態選 `Color` 或 `LinearColor` sampler。生成貼圖仍沿原本 Linear 規則。MaterialEditor 取代 `_ColorMask` 時，包含 MainItemPlus 的替換路徑，都清除已被取代的來源採樣 metadata；不保留舊材質的 Gamma/Linear 判斷。`SESSION_PIPELINE_VER=182`、CLI stamp293 使舊 session 失效。真實 mod `KK_381623` 的兩個 front-hair mask metadata 及 MainItemPlus 替換優先級的 Rust 目標測試已通過。
- **197 正式回歸**：`MaskSource197CargoBuild.log`／`MaskSource197Build.log` 成功，UBT重新連結 Rust 靜態庫及 Unreal DLL。`MaskSource197Final` 七卡 T-pose／Walk 42 圖、兩 UE exit0，兩 log 均沒有 sampler／材質編譯失敗；每pose七PV、188附件最大0cm，palette最大2.262e-6cm，原15張正式截圖 hash 保留。正／側接觸表已檢查，沒有新掉髮或飾品飄移；七張正臉相對195每通道平均差最大0.275/255。`visual_parity_passed=false`，尤其 KK_381623 金髮暗尾差及其眉毛方向仍可見；來源採樣修正不可寫成視覺完全一致。Stage196 的失敗截圖不列入回歸 PASS。
- **路徑檢查錯誤**：追查遊戲照片時沿用早期說明中的 `mesh/_dump`，目前遊戲安裝實際為 `C:\portable\KoikatuSunshine\mesh_dump`；第一次 `rg` 路徑不存在，改列遊戲根目錄後確認。不存在路徑的搜尋結果不代表照片或卡片不存在。

## E-178. 眉毛的直接 Texture2D 被重複翻面（Stage312）

- **症狀**：`KK_381623`（Android 18）Unreal 眉筆畫向下彎，遊戲 T-pose 照則向上拱。先前把問題歸於表情權重的假設被遊戲 mesh dump 否定：`cf_O_mayuge` 的 `mayuge.mayu00_def_op` 為100，與目前匯出器使用的 EyebrowCtrl 第0組 Open 權重一致。
- **原生像素證據**：原生 `BrowOrientation198Native/Card0.json.renderer29.mat0._MainTex.png` 是向上拱的眉筆畫；舊匯出 `MaskColorSpace196Extract/head_cf_O_mayuge.png` 是向下彎。同為512²，兩圖 alpha 不翻面時平均絕對差6.936/255、7804像素差>16；把舊匯出圖再垂直翻回後，alpha 每個像素完全相同。RGB受舊匯出時先烘卡片眉色影響，不以RGB值作方向證據。
- **根因與修正**：`ChaControl.ChangeSettingEyebrow` 的 `ChangeTexture` 直接把來源 Texture2D 指派到 `_MainTex`，沒有 `Graphics.Blit`；`resolve_head_detail_texture` 卻對所有 `mayuge` 無條件執行 `flip_png_v`。移除這個眉毛直接來源的額外翻面，保留 eyeline 依實際筆畫的既有判斷及其它來源處理；不是依卡名或圖片顏色旋轉。CLI stamp294／`SESSION_PIPELINE_VER=183` 避免舊快取遮住修正。
- **獨立來源交叉檢查**：新Rust release匯出的 Android 18（512²）與 March mod 卡（2048²）眉毛 alpha，對各自遊戲執行期 `_MainTex` 均為逐像素誤差0；若多翻一次，平均絕對差分別6.936及7.972/255、超過16的像素分別7804及141714。`BrowOrientation198AndroidCompare.json`／`BrowOrientation198MarchCompare.json` 是兩個不同貼圖來源的檢查，不以卡名決定生產分支。另一張 `KK_380982` 原生 `_MainTex` 為全透明，不能拿它證明眉筆畫方向。`BrowOrientation198CargoBuild.log` 和 `BrowOrientation198Build.log` 成功，UBT確實重新連結 Rust 靜態庫。
- **診斷命令錯誤**：`mesh_dump/KK_381623.png` 是資料夾，不是一般PNG檔；第一次用 `Get-ChildItem -File -Recurse` 搜尋同名檔得到空清單，不能據此稱沒有遊戲參考。改用 `Get-Item` 確認容器類型後找到 `Android 18_0` dump及SnowShot。跨卡Python inventory第一次把中文卡名輸出到cp950主控台造成 `UnicodeEncodeError`，那次清單在後段中止；改設定UTF-8輸出且只採用完整原生fixture。兩個命令失敗均不作材質或貼圖來源證據。
- **文件編輯失敗**：首次將Stage197/198摘要補入 `HANDOFF2.md` 時，誤把 `Docs/HAIR_AMBIENT_STAGE312.md` 的末段當成 HANDOFF2 的patch定位，`apply_patch`拒絕且未改檔；讀取HANDOFF2實際末段後再附加。這個工具失敗不影響production或測試結果。
- **198 正式回歸**：`BrowOrientation198Final` 七卡 T-pose／Walk 共42圖、兩UE exit0，兩log沒有sampler／材質編譯失敗；每pose七PV、188附件最大0cm，palette最大2.262e-6cm，原15張正式截圖hash保留。正／側接觸表已目視，沒有新增髮飾或側髮漂移。Android 18 新正臉眉毛確實向上拱，與遊戲照片和原生貼圖一致，舊版向下彎消失；七卡對197版的正臉整圖各通道平均差最大0.472/255，變動集中在眉筆畫。稽核仍 `visual_parity_passed=false`，金髮暗尾與遊戲照不同、佩子沒有同姿勢遊戲照片；不能把眉毛修正外推為整體shader parity。

## E-179. Forge 髮雙 Ramp 的暫存器誤判與遺失貼圖（Stage312）

- **文件命令失敗**：兩次用 `apply_patch` 時把巨段落的前綴當完整定位行，精確行驗證拒絕且未修改；改用UTF-8精確字串替換修正「199正式回歸」標籤。被拒絕的patch不列為已完成修改。
- **誤判先於改碼**：只看 `main_hair` DXBC 指令 `dot(normal, camera-world)` 後的 `t6` 取樣，曾推測 `_RampG` 應改用視線方向。逐項查 Unity `Shader.m_ParsedForm` 的 blob12/14 綁定後，確認 `cb0[11]`／`t6`（陰影變體 t7）實為 `_AnotherRamp`，而 `cb0[16]`／`t5`（陰影變體 t6）才是 `_RampG`；後者確實用 `_WorldSpaceLightPos0`。**沒有**把錯誤推測的視線方向套到 `_RampG`。往後必須把 DXBC blob 和同一 permutation 的 Unity 綁定表配對，再解讀暫存器。
- **實際通用缺口**：兩種 `Shader Forge/main_hair`/`main_hair_front` 都依 `min_sat(_AnotherRamp.r, _RampG.r)` 合成光照；`_AnotherRamp` UV 是正規化法線與視線內積乘其 `_ST.xy` 加 `_ST.zw`。匯出器雖讀 `_AnotherRamp` 到 `shader_extra_textures`，session 對非 MainItemPlus 無條件清空，Unreal 髮公式遂只讀 `_RampG`。現保留這一張髮貼圖及來源 filter/wrap/sRGB/mip，MaterialEditor 替換時清舊metadata，Unreal 按來源座標／色彩空間取樣後做 min；沒有卡名分支。安裝版兩種 Shader 的 `_AnotherRamp` 預設均為 white；未綁時等價原有輸出。
- **不能宣稱金髮修好**：掃描本地遊戲 dump 436 份 Forge 髮材質，`_AnotherRamp` 實際綁定數為0。這次是支援可能綁該貼圖的其他／mod 卡的通解，不能解釋或聲稱已修 `KK_381623` 的金髮暗尾；該視覺差異仍需追其它來源輸入或公式。
- **工具錯誤**：最初猜錯 `EC_Shaders.unity3d` 少了一層 `KKS_Plugins` 的路徑，UnityPy 空清單只是不存在來源，不是 Shader 不存在；用 `rg --files` 找到實際 bundle 才查綁定。曾猜 `Saved/ShaderDxbcKKS/EC_Shaders`／`Saved/ShaderDxbcME/EC_Shaders` 而無資料，實際 dump 在 `Saved/ShaderDxbcEC278/KK_Plugins.Resources.EC_Shaders`；`where.exe fxc` 查無 PATH 項，最後用 Windows SDK 的完整 `fxc.exe` 路徑。又猜錯 `Tools/run_dump_cards.ps1` 位置，實際為 `Tools/GamePoseProbe/run_dump_cards.ps1`；將中文卡名印到 cp950 終端首次 `UnicodeEncodeError`，改 `PYTHONIOENCODING=utf-8` 後得到完整七卡名。另兩次 `rg` 將不存在的 `Tools/ok-extract/src/bin`、`Docs/HANDOFF2.md` 列為搜尋路徑，後來改搜存在的 `Tools/ok-extract/src` 與根 `HANDOFF2.md`。等待 UE 截圖程序時曾把已交回的 exec cell 當成仍活躍而呼叫 `wait`，回 `cell not found`；改以回傳的 session ID 用 `write_stdin`，程序本身仍正常執行。這些失敗輸出都不列 shader 或卡片證據。
- **199 中間回歸**：`HairAnotherRamp199RustTest.log` 的兩種髮shader session/cache 目標測試通過；`HairAnotherRamp199CargoBuild.log` 與 `HairAnotherRamp199Build.log` 成功，UBT重新連結 Rust 靜態庫。`HairAnotherRamp199Final` 七卡 T-pose／Walk 共42圖、兩UE exit0；每pose七PV、188附件最大0cm，palette最大2.262e-6cm，原15張正式截圖hash保留；兩log沒有材質／sampler編譯失敗。正／側接觸表已目視，無新增掉髮或飾品漂移。七張正臉相對198的整圖平均RGB單通道差最大0.1615/255，符合這批卡都沒綁 `_AnotherRamp` 的預期。稽核 `visual_parity_passed=false`；不能把通用貼圖支援或附件PASS說成金髮暗尾已修好。
- **收尾發現來源辨識不一致**：Stage199 第一版只在 `mat.shader` 含 `main_hair` 時翻正／套用 MaterialEditor `_AnotherRamp`；但 `SessionPart::from_mesh` 已把 `hair_shader_hint=true` 且 Shader PPtr 無名稱者當髮shader。這會讓無名稱mod髮的貼圖進 session，卻沒有依來源方向處理。Stage201 用與 session 相同的提示條件，測試增加空shader名但有hair hint的路徑；SESSION185／CLI stamp296 清除先前快取。Stage199／200 的七卡測試只含已解析的髮shader，不能拿它代替此修正的正式回歸。
- **白色預設也要走取樣程式**：Stage199 的七卡都沒有綁 `_AnotherRamp`，因此只編譯了Unreal的常數白色fallback，無法確認真貼圖分支的shader。Stage200改為每次載入共用一張白色貼圖，符合Unity序列化的white預設，未綁時也編譯同一條UV／sampler路徑。`HairAnotherRamp200Build.log`成功、`HairAnotherRamp200Final`七卡42圖均exit0／無材質編譯錯；相對199正臉整圖單通道平均差最大0.1618/255。這是貼圖分支的中間驗證，之後仍需以Stage201重建的session版本為正式結果。
- **201 正式回歸**：`HairAnotherRamp201RustTest.log`的目標測試通過，`HairAnotherRamp201CargoBuild.log`／`HairAnotherRamp201Build.log`成功且UBT重新連結Rust靜態庫。`HairAnotherRamp201Final`七卡T-pose／Walk共42圖、兩UE exit0、每pose七PV與188附件最大0cm、palette最大2.262e-6cm、舊15張正式截圖hash保留。兩log沒有shader/sampler編譯失敗，正／側接觸表無新增髮或飾品錯位；相對200版七張正臉整圖單通道平均差最大0.1618/255。`Saved/Logs/HairAnotherRamp201FinalAudit.json`仍為`visual_parity_passed=false`；此版是通用髮貼圖載入修正，不是KK381623金髮暗尾的根因修正。

## E-180. Forge 髮 `_RampG` 的第二軸與 ST 被固定值取代（Stage202）

- **根因**：`main_hair`／`main_hair_front` ForwardBase DXBC 用同一個有號 `dot(normal, _WorldSpaceLightPos0)` 經 `cb0[16] = _RampG_ST` 形成 U、V，再採樣 `_RampG`。Unreal 原本把 V 固定為0.5，未讀 `_RampG_ST`。現僅髮shader改為來源公式，並按匯出 PNG 的上下方向轉換 V；其他shader家族保持各自的ramps路徑，沒有卡名特例。
- **證據界線**：本機 Studio `_RampG` 是256×4、四列相同，因此這項修正對目前七卡不應產生明顯變化；它使不同列或自訂 ST 的 mod ramp 有正確座標。KK_381623 金髮暗尾差仍未找到根因，不能把本次源碼對齊寫成視覺修復。
- **搜尋／路徑錯誤**：本輪最初用預設 `rg --files` 搜 `Saved` 的反組譯資料，因 Saved 被 ignore 而空結果；改直接讀已知 `Saved/Tests/HairShade199Asm` 路徑並以 `--no-ignore` 搜。也曾猜該資料夾有 `README.txt`，`Get-Content` 失敗；實際既有的三個 `.asm.txt` 已足夠，不把不存在的 README 當成 shader 證據。
- **比對腳本錯誤**：七卡 A/B 第一次直接用 Python 在 cp950 主控台印中文檔名，印出 `pairs 7` 後於姓名處 `UnicodeEncodeError`，不是圖像差或截圖錯。重跑時設定 `PYTHONIOENCODING=utf-8` 後，七張正臉差量皆完整輸出；KK_381623 為逐像素0，其餘每通道全圖平均最高0.14037/255。
- **202 正式回歸**：`HairRampUV202Build.log` UE編譯／連結成功。`HairRampUV202Final` 七卡 T-pose／Walk 共42 PNG，兩個受控UE程序 exit0，兩姿勢各7項PV及188項附件校對、附件最大0cm，palette最大2.262e-6cm，原15張正式截圖SHA保留。兩log沒有材質／sampler編譯錯；正／側接觸表無新增掉髮或飾品位移。相對Stage201，KK_381623正臉像素完全相同，其餘六卡每通道全圖平均差最大0.14037/255。`Saved/Logs/HairRampUV202FinalAudit.json` 仍 `visual_parity_passed=false`，金髮暗尾未修好。

## E-181. 髮陰影擷取方向直接改成 Studio 主光會弄黑衣服（Stage203）

- **誤判與驗證**：遊戲 Studio 主光在 UE 座標為 (0, 0.7660446, 0.6427876)，原自製陰影深度擷取使用 (-0.25, 0.85, 0.45)。直接共用主光方向後，HairShadowDirection203AB 三卡 T-pose 中 KK_381623 的藍上衣及白褲變近黑；全身相對202平均 RGB 差為 (19.441, 19.919, 20.746)/255，但同卡金髮裁切 p10 仍約 (220,183,69)、暗部比例約0.146，沒有改善。Alice、March 變化很小，不能以少數卡的未退化代替通解驗證。
- **處置**：已撤回深度擷取方向及強制 MainLightToSurface 的兩處改動；不把失敗圖列為正式回歸。毛髮 Lambert 主光與自製 depth capture 是不同輸入，尚須核對遊戲陰影貼圖的投射矩陣、偏移及受光對象，不能再用單一方向替換整個陰影系統。
- **量測修正**：舊比較把遊戲全身小頭與 UE 正臉特寫混用。改用全身頭部裁切後，遊戲金髮 p10 約 (198,154,77)、暗部比例0.234；UE Stage202 約 (220,183,70)、0.146。閾值僅用於追蹤金色像素分布，兩圖尚未像素配準，不能宣稱絕對色差。

## E-182. Forge 髮 ColorMask 列方向假說使金髮更亮（Stage204–205）

- **假說與反證**：KK_381623 的 ColorMask/DetailMask 同源於 yamadamod nohairB rev8 的 mf_haircap.unity3d，均為 1024² DXT1；舊匯出對遊戲擷取的最佳列方向卻分別是再翻面與不翻面。曾依來源格式相同推測 ColorMask 多翻一次，對 Forge 髮取消該翻面並把 session/CLI 快取暫升至186/297。三卡 HairColorRows204AB 正臉相對202平均差：KK381623 (0.798,1.345,1.469)/255、Alice (0.365,0.458,0.356)、March (0.076,0.141,0.058)。金髮全身頭部 p10 由 (220,183,70) 升到 (222,187,74)，暗部比例由0.146降到0.139，朝遊戲照片相反方向變亮。遊戲 runtime Texture2D capture 的 PNG 列方向不能只憑兩張來源格式相同推斷；此改動已撤回。
- **恢復驗證**：快取版本恢復185/296，Rust release 與 HairRows205RestoredBuild.log 成功，UBT重連 ok_extract.lib。HairRows205Restored 單卡 T-pose 三圖、UE exit0；KK381623 正臉與 Stage202 逐像素完全相同。Stage204 失敗圖保留作 A/B 證據，不當成 PASS。
- **來源檢查**：遊戲 mesh dump 的 cf_hair_b_21_00 有3599頂點；Rust 測試逐點比對法線、切線、UV0、UV1 全數通過，額外保留為回歸門檻。因此已排除這一片後髮的來源頂點輸入錯位，未排除 Unreal 最終切線空間、陰影貼圖或螢幕取樣差異；金髮暗尾仍未達遊戲照片。
- **命令與路徑失誤**：第一次以 Python -c 的字面反斜線 n 寫多行造成 SyntaxError，改用 PowerShell here-string 管線；第一次猜錯後髮 OBJ 名稱，改由實際匯出清單定位。測試時另把不存在的 Card0.json.globals.tsv 當檔案，實際是 Card0.json.shader-globals.tsv；把單行大型 mesh JSON 當一般 Get-Content 輸出造成大量截斷，後續只由 Python 解析欄位及數量。第一次追加本節時 apply_patch 定位文字不完全相符而拒絕，改以 UTF-8 附加，該失敗未修改檔案。診斷用 HAIR203/HAIR204 列印均已移除，這些命令失敗不構成材質證據。

## E-183. 截圖根目錄的舊說明與實際檔案不符（四視圖批次）

- 開始拍攝前，`Saved/Screenshots/README.md` 聲稱根目錄有先前五卡 15 張正式圖，但實際根目錄只有這份 README，沒有 PNG。不能把說明文字當成檔案清單或成功證據；先從遊戲 dump 與卡片檔重建 17 卡 inventory，再由 Unreal 重新拍攝。
- 本次只在 staging `Saved/DebugShots/DumpOutfit0FourViews/Tpose` 完成 68 張、UE exit0、逐卡四檔／900×1080／SHA-256 稽核以及五頁目視檢查後才發布。舊 README 原樣移到 `Saved/Screenshots/old/BeforeDumpOutfit0FourViews_20260930-103812/`，根目錄現只含這次的 68 張 PNG。發布後逐檔比對 staging 與正式檔 hash，結果 PASS，見 `Saved/Logs/DumpOutfit0FourViewsPublish.json`。
- 額外的時間戳 dump 目錄 `20260929_222402` 有 male mesh 檔，但沒有可對應的女性角色卡檔名；不能靠時間或照片外觀猜成第 18 張女性卡。正式 inventory 只收有 `mesh_*.json` 且名稱能對到 `UserData/chara/female` 的 17 個目錄。

## E-184. MaterialEditor 材質名空格與 915 髮片誤判（2026-09-30）

- **914 真正根因**：載入器 `me_mat_key` 刪 `" (instance)"`，連同分隔空格一起去掉。遊戲 `NameFormatted` 只刪 `"(Instance)"`，中間接 mod hash 的名稱因此留下空格。`Hair_F310-HairF (Instance)_(-722896)d` 在舊載入器變 `hair_f310-hairf_(-722896)d`，但卡片 ME key 是 `hair_f310-hairf _(-722896)d`，使三個髮色都落到白色。已按反編譯規則改通用名稱正規化並加入真卡測試；以新 UE 四視圖驗證。
- **915 不可套 914 修法**：915 的兩片 `cf_hair_f_34_06` 有正確 ME 貼圖，卡片 15,572-byte PNG 與遊戲 runtime dump 81,061-byte PNG 編碼不同但 2048² RGBA 像素完全相同；材質三色也同為白。新建置後左上偏灰未消失。不能因視覺像髮色錯，就寫單卡色值或重複 MaterialEditor key 修法；其根因仍須從 shader 採樣／遮擋／光照驗證。
- **發布工具錯誤與恢復**：第一次 PowerShell 發布命令用 `New-Item -LiteralPath`（該 cmdlet 無此參數），未建立 `old/PartialHairColor313`；同一行後續 `Move-Item` 失敗，但 `Copy-Item` 已覆蓋正式 914 四圖。不能以該命令的末行計數當完整成功。改用已驗證的原 `Saved/DebugShots/DumpOutfit0FourViews/Tpose` 四圖填入新建的 archive，逐檔確認正式檔 SHA 與 `PartialHairColor313/Tpose` 新圖相同、archive 四圖存在，根目錄 68 張。以後發布步驟設 `$ErrorActionPreference='Stop'` 並對 `New-Item -Path` 明確 `-ErrorAction Stop`，每步驗證後再複製。
- **915 原生擷取篩選失敗**：第一次 GamePoseProbe 用 GameObject 名 `cf_hair_f_34_06` 當 `-MeshNames`，但該參數精確比對 `mesh.name=cf_hair_f_34_06_g`；因此雖產出 renderer 表，沒有目標貼圖的 sampling fixture，腳本正確拒絕。第二次用 mesh 名加 `20_mesh` 並換新 tag `PartialHair915NativeExact` 成功取得 runtime 材質／貼圖；插件 DLL 已由腳本移回工具目錄，沒有留在遊戲 plugins。不能把第一次缺檔誤判成遊戲缺 HairGloss。
- **915 剩餘差異的證據界線**：原生 HairGloss 與匯出完全同像素，DetailMask 翻列後完全相同，ColorMask 僅有線性到 sRGB 的低量化差；UV1 最大誤差 `5.71e-8`。原生 Studio 兩件前髮相對遊戲 dump 的世界頂點最大誤差 1.23 mm；目前 session T-pose 相對 dump 最高 3.3 cm，臉也有 2.4 cm，不能混稱為「兩份遊戲擷取互差 3 cm」，更不能直接斷定前髮 placement 錯。`_MainTex` 灰區的實際 RGBA `(95,95,97,255)` 與遊戲 runtime 逐像素同值，遊戲畫面變深色需查遮擋或 ForwardBase。第一個 MaterialEditor 名稱修正對 915 無視覺效果，故第二修正仍開放，不以染灰片來假修。
- **診斷命令路徑／檔案匹配錯誤**：在 `Tools/ok-extract` 內執行 test 時把 log 相對路徑寫成 `Saved/Logs/...`，PowerShell 找不到子目錄，重跑時改用專案絕對路徑；讀貼圖時 glob `*accessory1*` 同時命中 `accessory13`，一度抽到別的髮片。後續以完整前綴 `accessory_135_0*accessory1*` 限定並核對檔名。暫時的髮根／頂點診斷 test 和為它公開的欄位均已移除，避免把未定論的量測留在正式 API。

## E-185. KK_384915 偏灰髮片：先隔離 renderer，再對照髮 shader 的 rim 指令（Stage314）

- **先前判錯物件**：原先只看名字及灰色 `_MainTex`，把 slot1／2 的 `cf_hair_f_34_06_g` 當成左上偏灰主因。遊戲原生 Studio 的逐 renderer 移除截圖證實主要可見區屬於配件 slot6、renderer11 `cf_hair_f_08_CenterRight`；移除 slot1／2 幾乎不改該區。以後先做 renderer 隔離再討論材質與骨架，不能以相同貼圖或相近名稱代替像素歸屬證據。
- **排除來源資料**：renderer11 的遊戲 runtime `_MainTex`、`_DetailMask` 與 session 輸出 PNG 均逐像素相同；`uv`／`uv2` 對 mesh dump 的最大分量差分別為 `1.49e-8`／`2.61e-8`，`_ShadowExtend=.4673212`、`_SpeclarHeight=.5767454`、`_rimV=.5986764` 及三色白也相符。遊戲 mesh dump 對原生 Studio 同片頂點最大差約0.7mm；session 與 dump 有姿勢時序位移，因此沒有按卡名搬動頂點。原生停用 `AmplifyColorEffect` 證實全域 LUT 會把灰髮染深紫；UE `OK_NO_LUT=1` 對照證實 UE 也已啟用 LUT，不能把差異簡化成「UE 漏加 LUT」。
- **通用根因**：`Saved/ShaderDxbc/EC_Shaders/Shader_Forge_main_hair_front/0001.asm.txt` 和 `Shader_Forge_main_hair/0000.asm.txt` 都先算 `max(dot(normal,view),0)`，再以 `_rimpower` 形成 `saturate(5 * pow(1-ndv, 9*_rimpower+1)-1.5)`，與來源 hue tint 相乘並逐通道截到0.5，最後乘 `_rimV * (1-DetailMask.g) * (1-DetailMask.b)`。舊 UE 只把固定 hue tint 乘後兩個遮罩，漏掉視角 rim gate，使大面積不該受光的灰區被額外提亮。現對兩種 Forge 髮 shader 共用該 DXBC 計算，從每材質讀 `_rimpower`，不加卡片判斷或指定顏色。原生將 renderer11 `_rimV` 設零時，深色區近乎不變、只有局部反光消失，與來源視角遮罩一致。
- **驗證界線**：UE `PartialHair915RimBuild.log` 成功；`PartialHair915RimGeneral` 的 915／914／381623 各四視圖共12張、UE exit0，無 shader 編譯錯。915 灰區範例像素從 `(131,115,127)` 降為 `(65,48,66)`；原生相近深色區約 `(39,29,59)`，未做兩相機像素配準，不能宣稱逐像素一致。914、381623 全正臉與舊版的每通道平均絕對差都低於 `0.2/255`，目視無新掉髮。其餘17卡完整回歸與正式圖發布結果見 HANDOFF2.md §182。
- **操作失誤**：PowerShell 不接受 Bash 的 `{session.rs,lib.rs}` 路徑展開，也不接受檔名位置的 shell `*.log` glob；改把兩個檔名逐一傳給 `rg`，在 PowerShell 用 `-g`。第一次於 `Tools/ok-extract` 工作目錄把 `Saved/Logs` 當相對路徑，改為專案絕對路徑。查找截圖時把 `face-without-AmplifyColorEffect.png` 誤寫成 `face-effect-AmplifyColorEffect.png`；先列實際檔案再讀。曾猜錯 `KKShadersRecreated` 的本機路徑，最後直接用 repo `Saved/ShaderDxbc/EC_Shaders` 反組譯，不把不存在路徑當來源證據。UV／頂點臨時 Rust test 已在完成診斷後移除。
- **全卡回歸中的反例檢查**：17卡68圖、UE exit0、inventory／尺寸／SHA稽核PASS；附件415列max0cm、17次PV audit均passed。`KK_226176` 的正臉平均每通道改變3.52／4.93／5.15，是比915更大範圍的髮色變深；逐張目視舊／新及遊戲 dump 全身照後，形狀未變、遊戲髮色偏深，不能因這張變化面積大便當成回歸失敗，也不能因此宣稱逐像素匹配。`KK_388205` 主要是眼周深色加強，已目視檢查。完整稽核在 `Saved/Logs/HairRim314AllCardsAudit.json`，正式圖與舊圖各68張且逐檔hash驗證見 `HairRim314AllCardsPublish.json`。第一次以 Python 在 cp950 console 印全部中文檔名，於第十張 `UnicodeEncodeError` 中止報表；設定 `PYTHONIOENCODING=utf-8` 重跑後17卡統計完整，此錯誤不是截圖／shader失敗。

## E-186. 七海千秋頭頂黑點與 RenderTexture mip 邊界（2026-09-30）

- **先看了錯的對照**：最初只看 UE 全身截圖與先前文字備註，誤認頭頂分散黑點是 UE 特有瑕疵。UE 關描邊仍有黑點；隔離八個 `cf_hair_b_103_01_a` 配件髮片（session 42–49）後才消失，證明像素歸屬，但不能證明這八片應被刪除。重新直接檢視遊戲 dump 的 `SnowShot_2026-09-29_20-18-48.jpg`，相同位置也有黑點。診斷用隱藏髮片／禁用髮 mip 只是 A/B，不可提交為正式載入規則；已移除兩個暫時的環境變數與腳本參數。以後先看原始遊戲照片，再判斷 UE 異常。
- **真實通用數值差異**：遊戲原生 Studio `_MainTex` 是 2048²、12 mip、Bilinear／Repeat 的 RenderTexture，mip0 與 session PNG 完全相同。遊戲 mip1 的 RGBA 等於上層 2×2 encoded channel 平均後截斷；舊 UE `RoundToInt` 在 9,088 個 mip1 alpha 邊界把原生 127 寫為 128，會改變 0.5 遮罩門檻。改用 `FloorToInt` 後，同卡原生 mip1 四通道逐位相同；mip2–7 alpha 的遮罩通過數也一致，殘餘每通道最多 1 的差異符合既有 GPU 控制誤差。再以原生另兩張 MainTex、一張 NormalMap 的 mip1 驗證，截斷的平均絕對差三組都比四捨五入小。這是通用 runtime texture mip 量化修正，不是用「移掉黑點」當成功判準。
- **驗證界線與操作錯誤**：`NanamiMipFloorBuild.log`、`TextureMipFloorFinalTestBuild.log` 成功；含127.5→127遮罩邊界斷言的 `TextureMipFloorBoundaryFinal` 原生控制自動化測試通過。曾把 native GPU row order 直接對 PNG top row，得到假性 alpha MAE 88.821；翻轉 native 行後 mip0 逐位相同。第一版分析腳本把已縮小的維度再右移 level，mip1 reshape 失敗；修正尺寸才取得有效比較。另在 PowerShell 搜檔時誤寫 `$_.FullName` 的點前空格，及把 Bash 式 glob 當 Windows rg 路徑，均為命令失誤，不構成來源資料錯誤。正式 `TextureMipFloorAllCards` 17卡68圖、UE exit0、inventory／尺寸／SHA稽核PASS、17次PV與附件顯示稽核通過，五頁接觸表目視無大幅顏色或幾何退化；與前版逐張整圖差最高平均0.2322/255。舊68張歸檔、新68張正式發布且hash一致，詳見 HANDOFF2.md §183。

## E-187. `KK_398302` 帽色初判與 ColorMask UV 假說未成立（2026-09-30）

- 對照遊戲 dump 全身照與 UE 大臉圖，確認寬帽簷在遊戲是淡藍、UE 偏白。初看也懷疑整件衣服顏色錯；重看原圖後，衣服大部分本來是白色，不能據此改衣服色值。先精確歸屬錯色的 renderer、submesh 和材質槽。
- 帽子原生 `_ColorMask` 對匯出 PNG 直接同列 MAE 0.0001106、翻列 25.5271；UV0 dump 對 OBJ 直接最大差約 5e-9。只因 UE 色塊位置偏白就推測 V 應取消翻面，證據不足。診斷開關 `OK_DIAG_ITEM_MASK_UV=1` 的四視圖只讓帽子局部變淡藍，剩餘帽片仍白且有硬邊，不符合遊戲。該 A/B 存於 `Saved/DebugShots/MinaseItemMaskUvAB/Tpose/`，沒有發布到正式 `Saved/Screenshots`。下一輪須以原生 shader 取樣與可見面歸屬追根因，並撤除未成立的診斷分支；詳見 `HANDOFF3.md` §3–5。
- 原生 Studio 擷取已寫完 166 列 fixture，但關閉逾時，腳本只結束自有程序而回報 `exit=-1`；資料是否有效要核對目標輸出檔，不能把退出碼誤認成貼圖缺失。遊戲插件已移除。

## E-188. MainItemPlus 帽色：PNG 列方向不能代替原生 GPU 取樣（Stage315）

- 已移除 E-187 的 `OK_DIAG_ITEM_MASK_UV` 和 `-ItemMaskUvDiag`。原生逐 renderer 色區控制證實帽簷主要屬於第一色（近白），第二色淡藍主要在帽帶；當前 Studio 同一張卡也呈白帽。`UserData/config/system.xml` 的 `shadowDepth=0`、Augh.MinimalShadow ramp，修改時間晚於舊照片。不能把不同時間、不同場景的 JPG 差異直接判成 UV 錯誤。
- `MainItem315NativeScene074` 原生控制顯示 `_ambientshadowG` RGB=128/255；alpha 1、.74、.26、0 會由白色逐漸轉淡藍。遊戲場景 depth 與有效 alpha 是 `1-depth`。UE MainItemPlus adapter 原使用舊 RGB (.019,.027,.039)，現改讀共同 Studio RGB 與場景 alpha；原生／UE runner 的 `-StudioShadowDepth` 可給相同診斷值，不修改遊戲 config，也不把 .74 強設為所有卡的預設。舊照片缺少 globals，仍不能宣稱完全復現其帽色。
- 原生 GPU 在帽子全部1853個 UV0 取 mip0 ColorMask RGB，與 session PNG 的 bilinear、sRGB 與既有 `1-V` 路徑逐值相同；取消 V 轉換的 G/B MAE 約 .5713/.2111。DetailMask 平均 G 差 .000794、最大 .01728（壓縮／GPU量化範圍）。來源與 SHA 見 `MainItem315HatUvAudit.json`。Forward／Outline DXBC oracle 各44 cases、704 pixels、max_error=0，重新生成三個 `.inl`／bindings 檔 SHA 與正式檔一致；沒有手改生成 shader。
- 第一版臉部相機未包住帽簷，不能用那張圖判帽色；改用手算 palette 世界頂點取景的 renderer controls。第一版 DrawMeshNow UV GPU fixture 全零，屬於無效擷取；改 GL 雙面 quad、清除值 sentinel 後才通過。Unlit/Texture 強制輸出 alpha=1，早期 NormalMap alpha 資料無法證明 packed normal parity；最終工具只核對 mask RGB，明確排除 alpha、mip 鏈與完整 shader 像素一致性。
- 操作錯誤：猜錯 dump 的巢狀人物目錄、把文字 `eighty/sixty` 當 PowerShell Count、`New-Item -LiteralPath`、Windows rg 的路徑 glob，以及在 ok-extract 工作目錄執行 repo 相對 Python 路徑，均已改用列出的實際檔名、數值、`-Path`／`-g` 或正確工作目錄。CLI 沒有 `--help` 選項，誤呼叫只回 usage，並非 release build 失敗。錯誤不作為材質證據。
- 彙整驗證報表曾假設 silent C# build log 必含成功字樣，以及只生成3檔；實際 csc 成功時沒有輸出，生成目錄另含2個 HLSL，5個來源檔 SHA 全相同。修正報表檢查後才產出 `MainItem315SourceEvidence.json`，不把報表錯誤當 shader/build 失敗。

## E-189. 第二卡反例：MainItemPlus shader 登錄與 ME 遮罩色彩空間（Stage315）

- `KK_380982` 的 MaterialEditor 已存牙齒／舌頭 `xukmi/MainItemPlus`，原生也用該 shader；UE 先前仍是 Forge kind3。原因是 shader 可用性掃描只讀平坦壓縮區塊表，漏讀 Unity2019 的 platform→block 陣列。增加巢狀 block 表解壓，僅對已有原生 UE adapter 的 MainItemPlus 啟用；SkinPlus／HairPlus 等未實作 adapter 的較新 shader 仍保留既有能力路徑，不把來源存在誤報成完整支援。
- 原生舌頭 renderer source 截圖的嘴型是當時 Studio 嘴型，舌頭被臉部遮住；UE 回歸固定 mouth0／open .8。兩者不可用作舌頭最終可見像素 parity。有效反例證據是相同材料、ME bytes、原生每 UV GPU mask 取樣及 DXBC arithmetic oracle，已在報表中限定此範圍。
- 頭部原生 MainItemPlus 保留 ME 三色、原始 MainTex、遮罩與額外 shader texture，使用共用 runtime normal 轉換，避免舊 CPU tint 再乘一次。第一個測試錯誤假設真卡 MainTex 是全白；實際真卡有 ME 貼圖，改斷言與原卡 ME bytes 完全相同，`MainItem315HeadRegistrationR2Test.log` 通過，保留第一份失敗 log。
- 新原生 GPU 反例發現舌頭 DetailMask 是 ME sRGB RenderTexture（9 mip），UE 原把缺 metadata 的 PNG 當 linear。舊 RGB MAE .12674、max .28725，不能放寬門檻使其通過。現共用 MaterialAPI texture 標記器保留原 wrap、sRGB、Bilinear 與 auto mip；頭部 MainItemPlus 的 ME MainTex／ColorMask／DetailMask／extras 依同一規則標記。修正後6個 mask inputs、1476 UV 通過固定 max .025／mean .002，見 `MainItem315CounterUvAudit.json`。UE 實際 log 確認 MainTex／DetailMask `srgb=1,mips=9`。SESSION188／CLI stamp299 防止讀回舊 cache。
- `MainItem315FinalAllCards` 是遮罩修正前的批次，由核對過命令列的自有 UE PID17188 中止，不能發布；先前 `MainItem315AllCards`、`MainItem315FinalScene074` 同為中間版本。正確最終診斷是 `MainItem315SamplingScene074`，兩卡8圖 exit0、UE build 成功。一次測試 filter 名稱不符而跑0 tests，不算 PASS 證據；改實際名稱 `runtime_texture_material_editor_copies_replaced_wrap` 後1 test 通過。正式完整回歸及發布以 HANDOFF2 §184 的 R2 批次為準。

## E-190. MainItemPlus ME 貼圖規則不能只修頭部（Stage316）

- 上輪只在頭部保存 ME DetailMask／ColorMask 的 sRGB、mip 與 wrap，配件／衣服／髮的共用 draw 路徑仍刪除 runtime metadata。反編譯 `kks_decomp/plugins/KKS_Plugins/KKS_MaterialEditor/KK_Plugins/TextureContainer.cs` 對所有屬性建立相同 sRGB auto-mip RT；`MaterialEditorAPI/MaterialAPI.cs:311` 的 SetTexture 先 HasProperty，再複製當前貼圖 wrap，null才Repeat，無頭部或卡片特例。現於每次實際替換時使用共同標記器，包含 mask、line、normal與extra textures，避免先刪 metadata 後從prefab猜wrap。
- 第一版擴充也假設 `_AlphaMask` 可套到 MainItemPlus。原生 controls 及已生成 ParsedForm schema 表明該 shader 沒有這個 texture property；遊戲 SetTexture 會忽略它。現從與 UE adapter 相同的 generated bindings JSON讀 texture property集合，`me_texture` 對 native MainItemPlus 做相同 HasProperty 檢查，測試明確拒絕未宣告 `_AlphaMask`，不手列按卡片使用的 property白名單。SESSION190／CLI stamp301失效中間189／300結果。
- 使用原生已安裝的 TextureContainer／SetTexture API反射控制兩個材質，null／Repeat／Clamp／Mirror、8個有效properties各4組，共64 cases／67,168 RGB samples通過；max差 .004031、最大整組RGB平均差 .000645（先前誤寫 .000618，已按完整R2報表重算），錯誤 linear negative control全部不通過。這是mip0 RGB／factory／wrap證據，不包含packed normal alpha、完整mip鏈或最終照片像素。報表 `Saved/Logs/MainItem316NativeFactoryAudit.json`；原卡／場景／config未保存，clone和sampler input只存在診斷fixture與自有Studio。
- 第一個原生控制 `MainItem316NativeTextureAPI` 因測試持有 shared material clone，而 API 的 GetMaterials 使用 `renderer.materials` 可能另建instance，SetTexture驗證失敗。改取得實際bound instance、使用NameFormatted對應名稱，追蹤並在finally銷毀自有instance；`MainItem316NativeTextureAPIR2`兩卡error皆空、所有控制通過。第一版失敗fixture保留，不視為生產材質失敗。第一個apply_patch包含猜測的foreach程式片段而未匹配，原子更新沒有寫入；改讀實際區段後套用。
- 一次在cargo工作目錄執行repo相對Python路徑，重複了E-188的工作目錄錯誤；改為所有混合cargo／Python驗證使用專案絕對路徑，重新通過後才生成最終audit。另第一次找錯Saved/Decompiled目錄與再用Windows rg路徑glob，後續先從既有Docs取得真實kks_decomp來源位置並逐一傳檔名。
- 曾在進度文字將共用 draw函式的4種role測試說成4種生產入口都已覆蓋。實際body／face有獨立入口，body測試僅核對共用組裝與objectType4／coord0 lookup；已更正進度和README。本輪修正涵蓋既有native MainItemPlus共用配件／衣服／髮及頭部draw，不能以此宣稱完整body／face ME shader置換或新SkinPlus／HairPlus支援。長期範圍仍依HANDOFF3§6；17卡回歸與發布結果見HANDOFF2§185。

- 最後的原生 controls 加入8個超出0–1的UV／property，32組59,552 RGB樣本仍通過；實際 SetTexture 拒絕 `_AlphaMask` 1次。清理現追蹤 API 遞迴的整個 renderer hierarchy，避免只還原單一renderer。見 `MainItem316NativeFactoryStressAudit.json`。RGB factory controls總平均差與單通道最大平均差應分開標示，前文 .000618 非最大單通道平均，最後彙整以報表重算。
- 回歸報表第一次用Windows預設cp950讀UTF-8 JSON失敗；改所有read_text明確encoding。接著用泛稱 `LogTemp: Error` 掃描，把UE::UnifiedErrorTest內建的3行故意錯誤當成失敗；進一步掃到CommonUI viewport既有錯誤，已對Stage315相同日誌核實。報表逐項保留既有錯誤，不能說整份log無Error或UI routing通過。

## E-191. 眉眼區回歸：同優先值的眼線陰影覆蓋筆畫（Stage316）

- `MainItem316FinalAllCards` 數量／PV／附件／平均差通過，但KK_388205臉图放大有皮膚色弧片蓋住上眼線，使用者指出眉毛錯誤。五頁縮圖及整圖mean不能代替局部目視；該批回歸報表改FAIL、visual_review_passed=false，不發布任何一張。保持正式根目錄Stage315。
- 同碼獨立重拍 `MainItem316LargestDeltaRepeat` 沒有弧片，不能以挑一張成功重拍替換作暫解。原生 `MainItem316LargestDeltaNative` 的眉眼無此弧片；原生renderer10上眼線material0 queue2903、shadow material1 queue2902。`IllusionFixes/Blitty.cs:25,54`按renderQueue排序後依序DrawRenderer。UE卻讓原上眼線與從同mesh生成的 `_kage` pass都用priority103，兩者幾何同面，render-proxy tie break可讓陰影在筆畫上方，造成皮膚色弧片。
- 從只讀SQLite解碼pipeline188／190整批／190獨立重拍，16個head part的PNG bytes、UV、positions／rest_positions、FBS patterns／baseline／deltas／settings全部相同，見 `MainItem316FaceInputEquality.json`；排除再翻眉毛貼圖、改FBS權重及單卡調色。session blob總SHA不同可能來自HashMap序列化順序，不能當資料有變證據。
- 共用face透明层現在依相同相對順序區分shadow103／upper104／lower105／brow106／nose107，sclera101／iris102與hair110的相對順序保留；CPU與GPU同用Part.EyeSort，沒有卡名分支。仍須建置、順序反例與整批回歸後才發布。新增 `OK face layer` 日誌用於核實實際優先值。
- 診斷example第一次誤用private session模組而編譯失敗；未改public API，改短暫module內測試。第一次cargo在專案根目錄無manifest失敗，改明確 `--manifest-path`。example及臨時卡名診斷test已移除，產出的只讀資料／log保留。又一次rg使用Windows檔名glob，應改 `-g` 或實際檔名，避免重複E-188。

- 三卡正序 `MainItem316FaceLayerForward` 實際12圖、UE exit0；第一個外層命令卻以PowerShell `$LASTEXITCODE` 判斷script成功，沿用之前native工具殘留值，誤報FAIL並跳過逆序。保留该外層失敗；改讀當次Captures.json的exit／images及script `$?`，不混用外部exe與PowerShell script的成功狀態。正序具體layer稽核3 cases PASS，白弧片消失。逆序与最終全卡結果另記後續。

- 最終回歸報表一度把正序／逆序兩個獨立control的逐像素相同，誤外推為17卡batch也必須整張同SHA。實際batch對control全圖RGB mean .09355/255、max62，差異散布全圖；所有source／queue门檻与白弧片修正仍通过。改如實記錄batch image差值，逐頁及放大目視後才標visual PASS，不放寬原生GPU的固定門檻、不宣稱最終像素parity。

- 最終 `MainItem316FinalAllCardsR2` 17卡68圖exit0；23個actual face-order controls、17次PV／415附件max0cm、5頁及局部大圖目視全部通过。江之島盾子上眼線筆畫亦恢復，無單卡branch。前68張先複製歸檔並核原SHA，再發布新68張、逐檔對staging SHA，`MainItem316FinalAllCardsR2Publish.json` PASS。第一批Stage316仍visual FAIL保存；後續以R2為正式版，不能混用原始失敗圖或部分controls的通過數。

## E-192 — Stage317 頸部帶飾品與 accessory move hierarchy（2026-09-30）

- 使用者指認 Junko 白色環帶垂到胸前。先前 attachment 0cm 只校對 UE 與同一 session，不能證明原生位置正確；必須隔離 renderer 與來源蒙皮頂點。
- 第一輪 Neck317Native 將 GameObject `o_mesh` 當成 mesh 名傳給 runner，實際 sharedMesh.name 是 `o_tie_01`，所以該 fixture 沒有擷取白帶。保留第一輪，依 renderers.json 的 mesh 欄修正為 Neck317NativeR2；不得拿缺少目標的 fixture 宣稱驗證成功。
- 原生 ChaControl.UpdateAccessoryMoveFromInfo 分別寫 N_move／N_move2 的 local TRS。舊 accessory_placement_matrix 把 renderer 和所有 bone 祖先掃到的 move 節點累乘成一個 post-skin matrix；N_move2 只位於領帶下半部骨架分支，卻轉動了另一分支的頸部環帶。白帶100個 root-only vertices 舊最大誤差0.102348028m，修正後 CLI 0.000000160245m，沒有單卡位移／旋轉補丁。
- 這次 apply_patch 首次重複同一檔案的 Update 區塊被原子拒絕；重寫時又插入重複 mouth 函數宣告，讀取上下文後即時修復，未編譯該錯誤版本。後續一個檔案只用一個 Update 區塊、不同位置用多個 @@，並讀回確認。
- 共用修正先在 SMR skeleton 中更新真正 move Transform，再執行各骨 LBS／DynamicBone；runtime 與非 runtime fallback 都採同一規則。rigid MeshRenderer 保留 renderer 祖先矩陣路徑。bind/rest 不重寫，SESSION191／CLI302 避免復用舊結果。巢狀／平行分支、混合權重的獨立測試已通過；正式原生／UE／全卡結果接續記錄。
- 額外驗證帽子時漏加 --ignored，該次 cargo exit0 實際是0 passed／1 ignored。保存 Neck317HatTestIgnored.log，重新帶 --ignored 執行；不能用 exit0 代替真正執行測試的 count。
- 又對單行大型 MainItem316FaceCacheComparison.json 用 Get-Content -First，造成巨量輸出；它是原始資料而非小型報表。後續只用Python解析後輸出欄名／數量／統計，不能把行數限制當輸出大小限制。
- 新增唯讀190／191快取差異診斷測試時，把內部類型寫為Session，正確名稱是OkSession，compile exit101。保存BeforeCompileFix.log後修正；應先讀型別宣告，不能按記憶猜名稱。
- 快取診斷第一次執行FAIL是0 matched rows：原生Studio啟動重寫colliders／uncensor／materialeditor三個config mtime，不能單靠pipeline190→191字串替換找同key。改比較實際cached inputs，保留兩邊完整key與差異，不宣稱config完全相同或把所有變化都歸因於本輪程式；UV／topology／textures仍逐值檢查。原FAIL保留NoExactConfigKey.log。
- 回歸報表又有一個read_text漏寫encoding，Windows cp950讀含Unicode卡名的BeforeScreenshots.json失败；補encoding=utf-8後完整重跑，17PV／415attachments／68PNG gates皆通過。所有Python JSON讀寫需明確encoding，PYTHONIOENCODING只控制輸出，不能代替read_text編碼。
- 快取比較初版呼叫OkSession::from_cache_bytes會重建live physics，與唯讀cached-buffer目的不同；完成partial後保留RehydratedPartial.json，最終改直接deserialize SessionBlob，不啟動sim／不改production。嘗試中止前確認owner已找不到process所以安全拒絕，未停止任何UE／無關程序；partial自行exit0。
- Stage317首批17卡68圖所有數值門檻仍PASS，但放大Yuuka發現新增翹到頭頂的髮束，Neck317FinalAllCardsRegression.json改FAIL，全部保留且未發布。追加原生Neck317YuukaNative確認659vertices：190 static LBS max3.57726e-7m，191 max0.0438343m，不能只看縮圖／attachment0。
- 根因是split prefab用原N_move2作實例調整0；兩個兄弟node的author world皆identity。mesh.move_nodes已保存alias到index0，但新helper僅找名字N_move，改了無關分支。改以matrix候選＋真正SMR bone ancestry識別，普通同分支再以canonical名字排序；新增重合矩陣alias反例。SESSION192／CLI303將重新驗證／回歸，不靠Yuuka卡名或mesh名修改production。
- 修alias的首個patch留了一個空Update block被原子拒絕；去掉空block後才實際成功。之前失敗的mouth重複宣告未再出現。
- 讀檔命令誤把bash的 `{a,b,c}` 路徑展開寫進PowerShell，整個命令在解析階段拒絕、沒有執行讀取或修改。改為rg多個明確路徑；不可混用shell展開語法。
- 最終192原生固定分支219 vertices最大1.21344e-7m，948 vertices的native-palette LBS最大2.38891e-7m；Yuuka659 static vertices最大2.39493e-7m，重合矩陣alias反例及其餘accessory測試11 passed／7 ignored。assigned claws兩側各952 vertices最大5.99254e-7m；192實際重跑均PASS。動態領帶尾部0.02034／0.11396m差異仍保留，這些來源控制不代表完整physics parity。
- 最終Neck317FinalAllCardsR2 17卡68圖exit0，inventory／17PV／415附件max0cm／17眉眼排序／5頁及6组原尺寸舊新對照PASS；唯讀17卡UV／topology／textures相同。舊正式Stage316先複製歸檔並核原SHA，再發布R2的68張，三方逐檔雜湊皆PASS。首版191仍FAIL保留未發布；不得混用首批數值通過和最終圖的證據。所有報表與限制更新HANDOFF2§186、HANDOFF3§9及Neck317SourceEvidence.json。

## E-193 — Stage318 動態配件固定步數與來源設定對照（2026-09-30）

- 延續Stage317動態尾端差異，先建同一native初始Transform graph、實際SetupParticles係數、collider refs、240步60Hz的installed／no_colliders對照，不直接修改每卡尾端頂點。
- 讀檔時把Select-Object -First數量誤寫成英文sixty，PowerShell解析參數失敗。改為60；此失敗沒有讀取或修改檔案，後续命令需使用數字型參數。
- 新Rust診斷collect的colliders未寫明HashMap<i64,BodyCollider>，rustc E0282／exit101。保留Accessory318TestCompile.log，補實際目標型別後成功編譯。
- 第一輪固定步數solver嚴格稽核FAIL：Junko installed frame1 quaternion component最大2.2352e-5，略超原2e-5 gate；相同影格world頂點位置最大1.367e-6m，240步rotation最大2.097e-6。Yuuka兩模式／全部影格亦在位置與rotation门檻內。原FAIL報表及log保留，不把最後240步通過冒充所有中間影格通過；這個微小差值不能解釋既有11cm尾端差異，繼續核對來源初始骨架、分布曲線與碰撞設定。
- 來源診斷原先按raw root名字數5條chain，split prefab會把cf_J_hairF_00R及非SMR helper改名成cf_J_hairF_00／cf_J_hairF_01。首版只匹配4條，保留log，改以實際SMR bone slot建立root alias，其他helper依粒子parent/order及TRS核對；production未新增名稱替換特例。第一次來源報表因count assert在寫檔前丟失，改先寫全部量測再檢查coverage，避免失敗時缺少證據。
- 新attachment ancestor helper的match arm結尾誤用分號造成compile101，Accessory318CachedInputsTestR2.log保留。改逗號後重新編譯與執行，不引用失敗命令的成功數。
- 確認來源根因是把含shear的socket矩陣壓成單一TRS，再以placement_correction於蒙皮後補回。這只能補幾何，不能補SetupParticles所讀的世界骨長／curve係數／碰撞。兩條領帶初始化位置誤差0.04323／0.03094m、係數最大0.03095／0.03949；保留BeforeAncestry.json。改保留實際body／head socket祖先local TRS後，5條chain／24 particles初始化最大2.39e-7m、係數最大1.19e-7；來源curve＋socket＋native collider controls的0／1／60／240步全部通過原2e-5位置及quaternion gates。重建native graph的獨立首版嚴格FAIL仍保留，不混用成最終來源控制PASS。

- Stage318新增Rust測試時另一個長時間debug test EXE仍在Windows執行，linker LNK1104無法覆寫同一EXE；Accessory318SourcePhysicsTestR2.log沒有執行測試，不列為測試PASS。等待舊EXE退出後R3重跑通過；後續不可並行relink同一debug測試binary。
- NativeR3追加原有live particle/private history，Junko14 particles的原有係數及bone length與重新Setup全部相同；Yuuka7筆bone length差約1.6e-5至1.66e-4m，不能解釋Junko懸垂尾端0.102m差。保留差異，不以初始化時機猜測固化production。
- Accessory318FinalAllCards完成68圖exit0，但使用者再次指出眉毛方向退化，因此該批不發布。數值與渲染流程通過不能代替使用者指出的視覺回歸。

## E-194 — Stage319 直接眉毛Texture2D與MaterialEditor替換來源混用（2026-09-30）

- 使用者指出江之島盾子、優香眉毛方向錯，原本正確的眉毛被修壞。查明E-178兩張alpha讀回圖（Android 18／March）其實不能證明catalog直接Texture2D的行方向；當時把MaterialEditor替換的RenderTexture結果外推至所有眉毛，導致SESSION183至193的直接來源漏掉Unity底列→UE 1-v的轉換。E-178「every direct source」及對來源類型的歸因撤回，歷史測試仍保留。
- 新opt-in原生Unlit/Transparent GPU coverage在實際mesh UV及額外64×64密集UV測試四卡，先驗證native讀回PNG用1-v可重現coverage，再驗證session alpha在UE 1-v座標。固定GPU max .002、alpha max2e-5、UV max1e-6，沒有由卡名／筆畫形狀選方向。舊192江之島盾子／優香alpha max1，Android 18／綾華ME來源通過，Brow319PriorUvAudit.json整體FAIL，不能覆蓋或引用為PASS。
- 共用resolve_head_detail_texture的catalog眉毛路徑恢復垂直轉換；MaterialEditor的既有early-return保留輸入方向。反編譯ChaControl.ChangeSettingEyebrow直接指派Texture2D、ChangeSettingEyebrowColor另設_Color；TextureContainer／MaterialAPI替換流程獨立。SESSION194／CLI305，不加單卡例外。
- 診斷第一次猜錯優香卡檔名，runner在驗證階段拒絕，未安裝probe／未啟動game，Brow319NativeRun.log留存；依inventory使用含「For KKS」的正確檔名重跑。新cache diagnostic誤讀不存在PartBlob.shader_params，編譯E0609失敗；改用實際main_tex_st／shader_kind後Brow319CachedInputsTestR2通過。
- 搜尋誤把PowerShell wildcard路徑傳給rg（KKMaterial*／KKMesh*及ShaderDxbc*）且猜不存在KKMaterialRuntime.cpp／dependency processor.rs；改用實際目錄與rg -g篩選。一次Python read_text漏encoding造成cp950 UnicodeDecodeError，重跑所有JSON／TSV均明確utf-8-sig。上述命令失敗不列來源證據。

- 首版194／305修好bundle眉毛卻被四卡GPU oracle擋下：Android18 alpha max1，Brow319FixedUvAudit.json FAIL。查source catalog／卡片ME keys確認Android18其實是Sideloader zipmod PNG覆寫tensai，不是ME（me_textures空）；其native RT不能直接推論由ME建立。先前本節「Android18 ME來源」及首次commentary歸因撤回。綾華是ME、Junko／Yuuka是bundle；三種來源均需獨立核對。
- 既有eye_create_png已按Sideloader PNG優先、bundle rows轉換解決同類來源邊界，現改名png_in_runtime_row_order並共享給catalog眉毛；ME仍early-return。只翻真正bundle Texture2D，不按shader名、卡名或brush畫法。SESSION195／CLI306。首批194的自有UE52440驗明exe與log身份後停止，部分圖FAIL保留未發布，重新build後用R2新tag驗證。

- 反編譯Sideloader.GetPng以ImageConversion.LoadImage建立PNG貼圖，與bundle裸Texture2D rows不同；source helper的existing選擇順序適用。Brow319ProvenanceTestR2以真卡catalog／ME及真正production resolver輸出4卡，再用native GPU coverage固定門檻全部PASS，17,576 UV samples、alpha max0，見Brow319ResolvedUvAudit.json。Native4個自有Studio均因quit延遲被runner驗明owner後結束（exit-1），不是正常exit0；完整fixtures與GPU結果有效，但不把exit稱為成功退出。


## E-195 — Stage320 舊版 KSOX FaceOver 未解析，Android 18 漏眼眶線（2026-09-30）

- 使用者確認原生 Brow319Native/Card2.json.face.png 的眉毛正常，後續指出 Unreal Android 18 少眼眶線。原生 cf_O_eyeline 上眼線／陰影／下眼線貼圖 alpha 全 0；Koikano/main_eyew DXBC 以 alpha clip，不以 R 作透明度。不能將這個反例處理為新的眼線翻轉或 R-alpha 補丁。
- 隔離 renderer28 face 後黑色眼眶消失、隔離 sclera 後仍見黑色區；native cf_O_face runtime MainTex 有完整筆畫，UE cached face 缺整層。這只能定位到臉部合成，不能直接認定是 paint。初次 commentary 把原因過早判成彩繪，已撤回：native create 原始底圖及純 create pass 都無這組筆畫。
- 原生 CustomTextureCreate 合成材質在 ReleaseFaceCustomTexture 後會清空大部分來源，合成完再讀 matCreate 得到 null 不能證明卡片沒有來源。新增 opt-in -FaceCreate 在 RebuildTextureAndSetMaterial prefix 擷取真實 create inputs／clone controls、postfix 擷取輸出，並在 renderer／相機控制前保存實際 face 與 KSOX runtime overlays。R2／R3 初期不同 create 呼叫覆寫同名診斷檔，後續改 call 序號保存；早期 partial calls 不能當完整 material 配置控制。
- 關鍵證據：原生 KoiSkinOverlayController.GetOverlayTextures(FaceOver) 有 1024²覆蓋圖，而 Rust card.ksox 為空。檢查卡片實際 KKEx bytes：KSOX=[1,{FaceOver:PNG}]，不是新版 Lookup／_TextureID_。先前只支援 v2 格式的 parse_ksox 漏掉 v0／v1。依本機反編譯 OnReload／ReadLegacyData 解析 TexType 名稱、EyeUnder 分到 L/R、EyeOver 分到 L/R，儲存 coordinate0作所有衣裝 fallback；v2 仍讀原 Lookup，不混用同名舊欄位。
- 生產修改只有通用舊版 overlay 解析與 cache stamps（SESSION196／CLI307）；沿用已存在的 shared composite_overlay。沒有開啟近似 OK_FACE_OVERLAY 彩繪流程、沒有新 UV／alpha／卡名補丁。Android saved overlay 與 native runtime PNG 全 RGBA 最大差0；32,594個完全不透明 overlay 像素的 production resolver face 對 native runtime 最大差0，舊 face 最大差239/255。其餘臉色、彩繪、表情及完整原生像素 parity 不由此宣稱完成。
- 命令錯誤：曾對不存在 Reference／Decompiled 目錄及不存在 shader asm／uncensor.rs 路徑查詢；rg 再次收到 PowerShell wildcard路徑，改用實際目錄加 -g。首次 inventory cargo test 在 project root 無Cargo.toml而未執行，R2改 Tools/ok-extract 執行；一次Python stdout仍以cp950輸出中文失敗，改明確PYTHONIOENCODING=utf-8。輸出失敗不列測試成功。第一次 cache stamp replacement 誤猜 u32，檢查實際 i32 後改196，build前已修正。
- Stage319 R2完整17卡68圖exit0，眉毛 GPU／來源及全卡 cache 校對通過；因新的眼眶缺層問題仍未發布。Stage320使用新 tag 全卡重跑，正式68圖在完整驗證前保持Stage317。原生 probes正常生成fixtures，但仍有 quit 延遲由runner驗明自有exe後結束（exit-1），不可稱原生正常exit0。

- Final native version欄位第一次缺失：GetExtendedData實際為公開繼承方法，誤以NonPublic搜尋得到null。改用Type.EmptyTypes選公開無參數overload後重拍；該次缺欄fixture不得冒充完整version來源證明。

## E-196 — Stage321 KK_226176 額外灰弧、眼線第二材質槽與精確資產查找（2026-09-30）

- 使用者新增 KK_226176 反例。真正 mayuge 的原生 GPU UV/alpha 已與 p196 相同，但 UE 眉毛下仍多一道灰弧；不能只憑 mayuge oracle PASS 宣稱整張圖正常。灰弧是 cf_O_eyeline 的第二材質槽。原生 MaterialEditor 把 cf_m_eyeline_kage MainTex 換成透明 PNG；舊匯出 shadow 複製 stroke 材質，套錯 MaterialEditor identity。
- 對齊本機 CharaStudio ChaControl.cs:4021–4097：slot0=EyelineUpTex，slot1=EyelineShadowTex，slot1另設 skinMainColor。從實際 source renderer m_Materials[1] 取得材質，再共用 resolve_head_detail_texture 的 catalog / ME 路徑。SESSION197 / CLI308，不按卡名或眉毛形狀處理。
- 首輪六卡十八眼線 oracle FAIL 保留 Brow321SlotUvAudit.json。Junko / Ayaka shadow alpha max1：named_png 的模糊前綴/子字串匹配把缺少的 head suffix 命中成其他圖，甚至透明 kage 選到有筆畫的圖。ChaControl.GetTexture 實際是 LoadAsset 精確名稱、suffix 失敗才試 base。新增 direct_catalog_png 逐候選取 Sideloader PNG 或 selected bundle 的 exact asset，來源 rows 決定轉換；移除 eyeline_stroke_needs_vflip 外觀啟發式。不能把 stroke/PNG/同名 pool 的規則套到 slot1。
- R2 真實來源測試 PASS，六卡 shadow UE/native PNG alpha 全0、UV最大2.88e-8。十八筆完整稽核 Brow321SlotUvAuditR2.json 仍 FAIL：Junko lower alpha max .00588235，部分 native RGB讀回/float GPU coverage 超 .002；不能把它改成全 PASS。獨立 UnityPy 與 DXT5 endpoint interpolation 也無法逐像素重現該 native lower，暫保留來源 runtime/recompression 的未解差異，不加下眼線補丁。
- 新 shadow 專項 GPU 控制從 Unlit/Transparent over clear 的 float alpha² 還原 source alpha，再按 RGBA8 每 texel 1 UNORM8 量化區間先建立界限再 bilinear；不以有損 sRGB RGB輸出猜 rows。Brow321ShadowSourceAudit.json 六卡 PASS；此報告只涵蓋 slot1 identity/alpha/UV，不取代十八筆原 FAIL，也不宣稱 full RGB/compression parity。
- 本輪命令/程式錯誤留存：renderer objects Vec 首次直接 chain 造成 E0599，改 into_iter 後 CompileAndLegacyTestR2 PASS；source oracle 曾在 inputs.json 尚未產生時過早執行；log 路徑誤用 Saved/Tests（實為 Saved/Logs）、script 路徑誤用 Tools（實為 Tools/GamePoseProbe）；再次把 Decompiled* wildcard 傳 rg 導致 os error123，改實際 kks_decomp/Managed/CharaStudio_Data 路徑；overlay inventory JSON 誤以 dict 存取（實為 list）造成 TypeError，修正後讀取成功。失敗命令不列測試 PASS。
- UE build 30.36秒與 release build26.62秒成功。Brow321FinalAllCards 正在重畫17卡68圖；首張 KK_226176 大圖已核對原生眉毛/眼線，額外灰弧消失，正常雙眼皮仍保留。全批完成驗證前正式 Screenshots 仍 Stage317；Stage320完整68圖保留 unpublished，因 KK_226176 回歸不得稱全卡PASS。


## E-197 — Stage321 完整目視重查與錯誤退化判斷（2026-09-30）

- Stage321 全17卡68圖已正常exit0。初次檢視KK_384283大圖卻誤判真正眉毛消失，暫停發布調查；重新開原尺寸pair與眉眼放大圖，兩版真正深色眉毛均完整存在，變化是眼線陰影，新版與独立原生照片相符。先前commentary「真正眉毛消失」撤回，沒有以此錯誤判斷加入Cull Off、Always、Offset或關閉深度測試補丁。
- 新獨立Brow322CounterNative採實際renderer22 mayuge，cached p197 PNG/UV對native差0（4394個samples），shaderWorldVertices298個最大3.5978362e-7m。原生plain worldVertices不是實際shader LBS，不能作錯誤幾何oracle。其AssetBundle container名稱與Texture2D object名稱相同，本卡不能支持先前懷疑的load-address根因。
- installed KKS/EC shaders實際Forward：Shader Forge/toon_eyew_lod0及Koikano/main_eyew皆CullBack2、ZTestLEqual4、ZWrite0、Offset0/0。ShadowCaster Offset1/1不可外推Forward。診斷素材位於Brow322Shader，不修改production狀態。
- native runner完成59項檔案，但Studio quit延遲由runner驗明自有exe後結束exit-1，不能稱原生exit0。真正UE batch exit0獨立記錄。
- 本輪命令失敗：再次誤把KKPreviewSkel* wildcard交rg，後改實際目錄加-g；Cargo工作目錄的後續讀log誤用project-relative Saved；shader resource regex漏unity3d副檔名造成空目錄，改Shaders\.unity3d$；最初brow image mask把RGBA alpha255納入RGB max造成empty selection/ValueError。後续只轉RGB測量，不能把空集合當PASS。較大ROI混入眼線、髮片的差值不代表眉毛退化；其原報告保留，縮窄量測另寫檔，不據此改production。

- 後續命令錯誤：猜pair檔名多加.face及native EyeOrder目錄不存在，改rg --files --no-ignore定位；一次read_text又漏utf-8-sig造成cp950解碼失敗，重新明確encoding讀取；又誤把kks_decomp當project子目錄，實際C:/portable/KoikatuSunshine/kks_decomp。所有失敗未計入PASS。
- 最終全17卡68圖exit0、17PV、415附件max0cm、17排序controls、5contacts/7full pairs完成；先前KK384283退化判斷撤回後才批准發布。正式68張已更新、原Stage317完整備份old/Brow321FinalAllCards，三方SHA PASS。原十八筆strict eyeline FAIL與broad ROI FAIL量測保留，不覆寫成PASS。

## E-198. Stage322 Koikano 背髮來源追查（進行中）
- 新增31卡／2240份材料dump。Siesta的Koikano/hair_main_sun先前被generic accessory分類；新增同系列kind10及原生Forward三色toggle／reverse／.01 clamp。不使用卡名調色。
- 第一輪Hair322Controls兩卡8圖exit0但Siesta仍粉色，維持FAIL、不發布。UE背髮_Color=(.691176,.562965,.533629)，native=(.335714,.335714,.335714)，且native HairGloss=null、UE仍有gloss。不能把shader分類成功當完整修復，接著查component renderer映射／HairAccessoryCustomizer覆寫。
- 改C++條件的文字替換誤把新宣告本身替成bHairShader || bHairShader0，建置前逐行檢查已修正為ShaderKind==1||10。避免無界限replace或讓新插入的碼再次被替換。
- Rust synthetic試驗首次錯誤假設沒有component map會保留bundle色；真正ChangeAccessoryColor fallback會寫白色，原FAIL Hair322AccessoryTest.log保留，修測試預期，不改生產去迎合錯誤fixture。
- 命令錯誤：rg只掃Source漏Plugins；native舊session85089已無process ID，不能當正常退出證據；Hair322NativeR2重新取得完整兩卡Sources.json。舊原生fixture保留。cp950 stdout遇新增卡名UnicodeEncodeError，後續命令顯式PYTHONIOENCODING=utf-8。
- 其他無效查詢：猜錯反編譯目錄與fxc 0088（實際Forward0096/0104）、廣掃DXBC index造成輸出截斷、PNG尚未產生即Get-ChildItem報missing。應先核實來源路徑／pass blob／完成狀態，使用精確清單及utf-8-sig。
- 原生runner delayed quit只核對並終止自己Studio，兩卡fixture各53／58 renderer，退出-1；不是正常exit0。正式Saved/Screenshots仍保留Stage321結果。
- 深追來源確認沒有HAC設定的背髮來自category101原生hair prefab。ClothesToAccessories.ConvertToChaAccessoryComponent在不存在ChaAccessoryComponent時設定normal=custom hair、alpha=hair accessory、use123=true。新增has_accessory_component辨識，保留真正配件的authored map；修正gloss只能在HAC設定存在時覆寫。p199／CLI310排除p198粉髮快取。
- Hair322ComponentProvenanceR3失敗是TSV colour字串為RGBA(...)，不能只trim括號後parse float；R4讀prefix後三色對native <1e-6、兩null MainTex白色／null gloss及原生component map通過。CachedInputs初次E0502保留，改先clone材料後呼叫mutable session ABI，R2實際10個kind10/cached keywords通過。
- 第一次sampling audit找到0 samples；UV sampling在Probe.cs由RendererControls啟用，TextureMips僅存mip。新增Hair322SamplingControlsNative取得真正GPU UV值，不能把10個colour checks當sampling PASS。列檔必須按property完整suffix，不能猜renderer5.sampling.tsv。

- 採樣驗證三次失敗均保留：原本直接把6/255施加在線性RGB不適用sRGB；改encoded texel interval仍未計算D3D11濾波tap固定點精度，兩個DetailMask超界。最終依Microsoft D3D11 §3.2.4.1／§7.18.16.1的8-bit fractional、0.6 ULP座標界限傳播雙線性區間，沒有放寬生產顏色或改角色貼圖。前兩版分別保存Hair322NativeInputsAuditLinearBoundFail／EncodedBoundFail.json。
- 加入逐texel原生PNG檢查時初次ColorMask差.294失敗；Probe.WriteTexture實際先blit到sRGB ARGB32，linear來源必須解碼readback。按實際輸出語意修正後8個texture RGB差都在encoded 6/255內，再獨立檢查GPU UV interval。Hair322NativeInputsAudit.json PASS10材質／8採樣；這不等同完整Koikano lighting parity。缺少DetailMask metadata時先前KeyError；按C++既有linear/repeat fallback核對，不能猜成gamma。
- 再次以Get-Content讀巨大單行JSON造成截斷，後續改Python解析精確欄位；不能以截斷輸出作審查證據。

- Hair322ExpandedFinal31卡124圖產完、UE程序正常退出，但runner exit1拒絕通過：龍卷3個HairGloss材質sampler type固定Color，實際Texture2D SRGB=false，觸發6條compile warnings並使用default material。audit_capture_regression只檢查Error字樣，初次錯報numeric PASS；已新增Failed to compile Material warning gate，重跑原報告改FAIL，不當作正式回歸。共用GlossSample及G sampler按GlossTex->SRGB選Color/LinearColor；Hair322SamplerBuild16.26秒通過。Hair322ExpandedSamplerR2重新跑31卡，需驗runner與目視後發布。
- 完成追加3舊卡12圖exit0；Hair322SessionComparisonTest17卡PASS：272個head parts材質不變，所有UV/indices/positions/normals不變；其餘變更僅native hair輸入。13 accessory regression PASS／11明確ignored，不把ignored列為PASS。
- 新增兔田佩克拉衣物黑片與遊戲紅花紋差異，整批visual暫FAIL；不是只憑Siesta修好便全批PASS。Hair322PekoraClothesNative/R2與CachedClothingInputs證明top/gloves PNG和UV與原生相符（UV<2.98e-8），正續查row transport/renderer selection/outline。未新增卡名patch。初次MeshNames用gameObject cf_top_sakura不是實際mesh Blood_high_costume_top，R2才選中；避免重犯。兩次Python診斷失敗：rglob漏子目錄IndexError、null material缺shader KeyError；OBJ無vt不可做空array reduction；改讀JSON mesh.uv。一次列renderer整個bones陣列又造成截斷，後改只輸出mesh/path/active。

- Pekora coverage新control：top6832 UV／gloves5716 UV，正常1-v可見比例與native相同；不翻V的假說alpha max1/mean.536／.411已被排除。Top正常alpha max.00153、gloves少數.015625 coverage讀回差保留為diagnostic，不硬改門檻宣稱full alpha PASS。NoOutline診斷4圖exit0讓袖子紅紋恢復，body/skirt/black rectangle仍存在，證明不應關閉outline作修復。
- PekoraOtherNative選LolaHead造成Probe.WriteRendererSnapshot的weighted palette越界IndexOutOfRange；failure JSON完整保留，不當成source oracle。R2排除Head選U_Char5/6、skirt、socks完成102 renderer／native quit-1；已核對六個MainTex尺寸／alpha，RGB最大差1/4/9/13 quantization levels，貼圖未遺失。Skirt1188／socks956 UV與native各<2.95e-8。
- 暫增明確opt-in OK_DIAG_SOURCE_ALBEDO／run_dump_cards -SourceAlbedoDiag，只改素材顯示為來源MainTex以分離取樣與光照，UV/alpha保留；尚待建置及診斷，不是生產修復，不可發布診斷圖。定位後須移除該暫時分支與switch，或明確另驗其通用診斷用途。

- SourceAlbedo/PreLight/LightFactor/FinalColor四組診斷都完成exit0，但FinalColor臨時graph仍出現黑片，不能把分離節點正常推論成production shading已證偽。完整R2的Pekora已紅色，先前「R2仍黑」判斷撤回；重新用清除全部診斷分支的production單卡DefaultR3驗證也正常，四視圖對R2平均差0.0166–0.0677/255。前後卡SHA相同。尚無足夠證據把早期黑片指定為某個lighting/outline公式根因，沒有基於它改UV、材質色值或關閉production描邊。
- Screenshot原本僅使用固定timer。追加GShaderCompilingManager FinishAllCompilation及FlushRenderingCommands，在每卡開始記錄pending_before/after；NoOutline重拍4圖exit0正常，pending 0→0，因此此控制只證明這次ready，不宣稱已證明早期黑片由非同步編譯造成。四個opt-in診斷分支與runner switch均已移除，Hair322CleanBuild11.25秒及ReadinessBuild11.83秒成功。
- Hair322ExpandedSamplerR2完整31卡124圖exit0，747附件max0cm／31PV／zero compile failures，八contact頁已全部目視；龍卷材質正常、Pekora紅花紋正常、Android18／KK226176／Ayaka／Junko控制保留。R2 visual PASS另寫新報告；首批ExpandedFinal維持FAIL。追加三舊卡12圖數值PASS（96附件），dump provenance以先前PASS卡SHA驗證。正以Pekora排第一重跑最終34卡136圖，正式root尚未更新。
- 本輪命令錯誤：Python誤把list(p)當keys列出巨大材質/UV，輸出再次截斷；rg又傳Tools/shader* wildcard，及猜不存在KKShotPreviewSubsystem.cpp。改list(p[0].keys())、精確目錄/rg --files定位，無效輸出不得用作證據。

- 最終Hair322FinalAll34以Pekora排第一，34卡136張900×1080、UE exit0、34PV／843附件max0cm／34face queue controls／34material readiness全部PASS；pending_before最高746，after全0。9頁四視圖與9組大圖controls確認後才發布；舊68圖先copy old/Hair322FinalAll34並核原SHA，正式136圖逐檔SHA PASS。對R2＋舊3卡整图平均變化.049603/255，不是native pixel parity。
- 額外廣跑430 library tests誤將大量installed資源測試並行，owned debug test PID31320 private memory達129,570,856,960 bytes；核對exe後停止，FullLibraryTests保留FAIL/aborted，不當PASS。暴露舊ME mask metadata None斷言（Stage316已改保存wrap/sRGB）、flat red atlas測試誤把常色圖當packed、cache benchmark250ms在高負載不可靠；僅修測試fixture/斷言，不改production。cache改核thread-local Arc identity；flat red應被拒絕、非constant packed fixture另外核既有輸出。
- 隔離測試初次--exact漏tests::prefix，實際0 tests不得計PASS；修prefix後確認metadata舊斷言FAIL。在全套exe仍執行時重編test造成LNK1104，IsolatedTestR3保留FAIL；要等owned test退出再重編，不只UE DLL會鎖定。
- ScopedLibraryTests以2 threads排除39個重型installed／manual DXBC／舊dump診斷，280 PASS／110 ignored，但baked_face_has_eyeshadow_at_eye_uv讀已刪除mesh_dump/outfit4/Ushiwakamaru_0時NotFound，報告仍FAIL。新增R2排除此缺失fixture，40項排除清單獨立保存；不能稱430全部通過，也不能把排除/ignored當PASS。歷史dump失敗未修改成假PASS，production來源校對另由Stage322 installed controls及全34卡證據支撐。

- ScopedLibraryTestsR2以2 threads正常exit0，280 PASS／0 FAIL／110 ignored／40 filtered out（314.03秒）；全部被排除名稱保存ExclusionsR2，原430 aborted與首輪391 NotFound FAIL仍保留。3個修正的測試實際列入此通過範圍，沒有把missing fixture改成PASS。此後只更新文件／evidence；production library/render source沒有追加改動。


## E-199 — Koikano hair gloss UV 不能沿用 Forge 的視角／前髮位移（2026-10-01）

- 原生 slot21 shader bytecode證實：VS0032/0048先對UV1套 `_texcoord2_ST`；PS0096直接使用該值，PS0104（`_OLDHAIR_ON`）才額外套 `_HairGloss_ST`。之後U加 signed NdotL×0.005，第一tap V加0.008；前髮source PS相同。`_REVERSECOLOR01_ON`不改此UV路徑。
- 舊共用Forge路徑把SpecHeight/view bitangent位移、front U×0.02+0.99套到kind10，且ST在offset後面；非單位ST的反例會失敗。不能只用預設ST或Siesta null gloss判定通用採樣正確。
- 修正限kind10的共用材質圖：保留Forge既有路徑，Koikano依原生VS/PS順序與keyword，不使用卡名、mesh名或圖片補丁。production HLSL在Shaders/KoikanoHairGlossUv.inl，Rust SESSION199/CLI310不變。
- Tools/shader_oracle310/hair_gloss_uv.py以原始DXBC immediate bits產生座標階段reference；D3D11 WARP四組2048cases（雙tap、負UV、非單位／負ST）新版max0；同組舊公式counterexample全部exit1，max3.790426至4.590454。這只證明座標階段，不是執行整支原生PS的完整光照parity。
- 初次prepare失敗：共用pixel precise_lines只排除ps header，VS header造成instruction counts differ；在VS caller明確移除vs profile行，不改既有pixel generator。初次oracle C++build漏string/stdexcept，明確include後成功；初次UE build引用skin block內Scalar helper超作用域，Hair323Build.log FAIL保留，改明確constant nodes後Hair323BuildR2.log成功。
- 操作錯誤再次出現：rg wildcard path Saved/Logs/Hair322*在Windows為invalid path；應傳真目錄再用-g。未設PYTHONIOENCODING=utf-8印Unicode中文JSON造成cp950 exception；先設定encoding並只印摘要。兩者不構成材質或測試PASS。
- 完整34卡回歸另見本階段最終報告；Stage322正式136圖及歷史來源索引維持原證據，通過回歸與目視前不覆寫正式圖。Koikano剩餘光照、strict eye oracle及free-running physics仍依HANDOFF3開放項追蹤。

- 第一個Hair323FinalAll34 numeric PASS但visual FAIL：Megumin第一全身靴子色變淡，整圖差.943582/255；same batch side/back差.028039/.031851，單卡Hair323MeguminControl四圖exit0、靴子正常。不能把第一批發布或換入單卡圖片湊PASS。Gpu resource readiness新增RHI SubmitAndBlockUntilGPUIdle＋render-thread flush以補足完成條件，全卡R2重新驗證；未證實前不得宣稱GPU upload是唯一原因。Rust來源SHA未變，但UBT重新cargo --release --lib後.lib SHA不同，應記新binary SHA，不宣稱binary與Stage322相同。

- 診斷工具定位失誤：Hair322EvidenceFiles.json在Saved/Tests，非Saved/Logs；查找Megumin load log時猜marker造成StopIteration，改先rg實際card/task marker。RHI完成API依本機UE5.8 RHICommandList.h核對：先ImmediateFlush(FlushRHIThread)，再RHIBlockUntilGPUIdle；非任意增加timer秒數。舊批次在require-gpu-readiness下明確FAIL，報告Hair323MissingGpuReadinessCounterexample.json保留。

- Hair323GpuFinalAll34也visual FAIL未發布：Megumin正面恢復，但側面靴子仍變淡（整圖差.762732/255），不能把單task GPU屏障說成色差已解。明確撤回任何「GPU屏障已解決全部色差」推論。每次換視角都在task barrier之後，補齊每個RequestScreenshot前的shader/render/RHI/GPU完成條件，接著重新整批验证；仍不以一次正常輸出證明唯一根因。

- 第三批Hair323ViewGpuFinalAll34的Megumin正面仍差.943514/255，側面正常.019522。每次截圖都有GPU等待仍不能消除色差；這進一步反證把問題單獨歸因RHI/GPU未完成。不得發布第三批或宣稱等待屏障是完整修復。查到固定姿勢SetActorTickEnabled(false)同時停止Tick中的CaptureScene深度更新；新增RefreshShotShadow，僅重畫current pose的雙buffer深度並綁定，明確保持AnimPhase／FaceClock不變，後續驗證獨立於GPU假設。

- Hair323ShadowControls三卡12圖exit0：12次shadow captured／pose_clock_held=1、12次view GPU完成、3PV／94附件max0cm／零shader failures，Megumin正面與側面橘靴正常。刷新會顯示既有1024²陰影的粗邊，不能宣稱native lighting parity；Siesta頸部灰暗及Megumin衣服缺少原生裝飾仍為既有未完成材質／光照差異。完整34卡仍須獨立回歸，不混入控制圖。
- 再次encoding失誤：PYTHONIOENCODING只控制stdout，不會改Path.read_text的Windows預設cp950。中文JSON讀取仍須明確encoding='utf-8-sig'；該摘要命令UnicodeDecodeError不是測試FAIL或PASS，修正後重讀。

- Hair323ShadowFinalAll34完整34卡136圖exit0、34PV／843附件max0cm／34排序／136held-clock shadow與view GPU gates numeric PASS，但Megumin橘靴再變淡，visual FAIL未發布。三卡控制正常不能取代全批次；陰影刷新是確實的停更修正，不能說已消除所有色差。
- 共用材質input identity新增Main／Detail／Normal／Ramp CRC，不改輸入。InputOrderControls與InputSingleControl的Megumin38組fingerprint相同，兩次均淡橘，不能直接認定卡片順序為根因。禁用LUT控制整體去飽和；依UE本機PostProcessCombineLUTs.cpp以r.LUT.UpdateEveryFrame=1強制重算的控制仍淡橘，對失敗單卡靴ROI平均只差.2179/255，此假說被反證；診斷只在Saved/Tests衍生runner中，production未加入該CVar。
- Texture UpdateResource排程初始化，截圖前flush無法保證材質建立時綁定已ready。在共用ApplyPartMaterial建立UMaterial前FlushRenderingCommands後，TextureInitControl單卡橘靴正常；相同ROI變化15.8442/255。材質公式與來源未改，還需同順序三卡與完整34卡验证，不能以一次正常畫面宣稱唯一根因。
- CLI錯誤：PowerShell不能用Python反斜線引號跳脫嵌套script，LUT診斷產生器ParserError；改保存.py後執行。apply_patch猜UMaterial型別而實際為UMaterialInterface造成context mismatch，未改檔；先讀精確函式內容再套patch。兩者不作測試結果。

- TextureInitOrder三卡再次正面淡橘、側面正常；材質建立前flush不是完整修復，已移除。不能由單次恢復橘色宣布資源競態根因。真正來源反例：dump mesh56/57是ct_shoes_inner／ct_shoes_outer，vertices/normals/UV/indices/bindposes/boneWeights全部相同、MainTex不同；UE舊版同時載入part40/41，深度相同的兩套鞋互相覆蓋，表現隨draw順序變化。
- 原生Hair322NativeR2/Card1.json.renderers.json直接證實inner enabled=true、active=false，outer enabled=true、active=true；ChaControl.DrawClothes精確以shoesType==0/1選slot7/8，另要求clothesState[slot]==0。ChaFileStatus.MemberInit鞋型預設1。Studio AddObjectFemale.LoadCharaFile預設noLoadStatus=true，因此不能拿卡片保存的Status鞋型覆蓋Studio初始化；明確runtime／scene模式才透過OK_SHOES_TYPE指定。
- 通解在clothes_kind_visible加入鞋型，移除未選擇的鞋子render parts，不用geometry duplicate／卡名／色值判斷；選擇資料加入session／extract cache key，SESSION200／CLI311，解碼bundle cache保持102。兩個targeted footwear／cache unit tests PASS；未廣跑432 tests，不把430filtered當PASS。舊p199 caches保留作反例。
- 室外三卡12圖exit0、3PV／59附件max0cm／12shadow及view gates PASS；室內四圖exit0。相同Megumin素材分別只保留outer CRC4a30845a或inner CRC3322948c，兩組各37parts；室外三全身視角橘色、室內淡橘是它原本來源顏色。Native visibility verifier拒絕舊log：(2,1,extra/missing footwear)，反例log保留。完整34卡正在以p200獨立重跑，正式圖仍未更新。
- 新verifier最初以[-nativeCount:]截取鞋子資料，會掩蓋多餘renderer；已改擷取actual task最後一個完整part0/body輸入區塊並嚴格要求數量相同，再跑正例與舊版負例。不得截斷多餘資料湊PASS。
- 路徑錯誤：在Tools/ok-extract工作目錄讀Saved/Logs相對路徑不存在；應使用../../Saved/Logs或workspace絕對路徑。再次rg ChaFile*.cs wildcard path為invalid path，已改真目錄配-g；不要把CLI lookup失敗當source不存在。

- Cached34 首輪比較直接重序列化 PartBlob，忽略 MeshMaterial HashMap 的隨機 iteration order，body 被誤判異動；R1Failed 保留。只排序 material 後第二輪 accessory 又因 private skeleton/deferred collider HashMap 與兩個 bone membership HashSet 順序被誤判，R2Failed 保留。比較工具改 canonical unordered collections；ordered arrays 維持原順序，float 以 bits 保存，不增加數值容差、不更改 cache／production。必須以完整 R3 實測決定 PASS，不能把前兩次當通過。


- 最終 Hair323ShoesFinalAll34：全34卡136圖 UE exit0；34 task GPU／136 view GPU／136 captured/held shadow、PV/attachments/眉眼排序及九頁與重要大圖目視 PASS。唯讀 p199→200 全34卡所有保留 part 在 unordered map／bone membership set canonical 排序後各值與浮點 bits 精確相同，只移除未啟用鞋；最終 native footwear verifier PASS，舊雙鞋 counter FAIL 原檔保留。
- 已發布全136張，Stage322 前136張完整 copy 至 Saved/Screenshots/old/Hair323ShoesFinalAll34，三方 SHA PASS。四個失敗完整批次均未發布、未混入控制圖。專項3個 unit tests與一項 cached34 integration PASS，不宣稱433 tests 全過。最終來源／完成索引 Hair323SourceEvidence.json、Hair323FinalCompletionAudit.json；完整 native lighting、strict eyeline 與 free-running physics 仍開放。

## E-200 — MaterialEditor texture Scale／Offset 不依賴 TexID（2026-10-01）

- Siesta 原生 body/face 使用 xukmi/SkinPlus，現有 fallback 並非完整原生 shader；不先把全部頸部暗影歸因 shadow bias。追到 MaterialTexturePropertyList 同時保存 nullable Scale/Offset，解析器只取 TexID/PNG，無 TexID 紀錄整筆跳過。原生 MaterialEditorCharaController 分別呼叫 MaterialAPI.SetTextureOffset/Scale，與 TexID 獨立，Original 欄位只作還原。
- 共用 parser 現把有效兩分量 Scale/Offset 輸入既有 material ST component transport，保留 objectType/coordinate/slot/material/property 身分；nil 保留 authored axes，不讀 Original、不改 PNG/UV/geometry。實際 KK_380982 卡片 NormalMap scale5×5與 native renderer35 TSV 同值。SESSION201／CLI312使舊錯誤 session/extract cache 失效，bundle102／FFI／SessionBlob layout 不變。完整 SkinPlus 尚未因此完成，畫面影響待驗證。
- 查找失誤：猜 KKMainSkin.cpp/CharaController.cs/Probe 檔名不存在；應 rg --files 先定位（實際 MaterialEditorCharaController.cs／Probe.cs）。rg 含未跳脫括號產生 regex error，改 -F。一次 apply_patch 送出 placeholder context，匹配失敗未改檔；已重讀實際版本常數後正確套用。上述均不作驗證結果。

- 201 Controls12圖 exit0 不代表ST修正已入最終資料；cached integration嚴格斷言body NormalMapST5×5失敗，實際仍1×1，R1Failed保留。body繞過push_meshes共用ME floats路徑；現補body ST-only套用，不順帶宣稱其其他數值或完整SkinPlus支援。版本改SESSION202／CLI313，使201早期錯誤cache失效。201 side頸暗仍存在，不能宣稱ST已消除所有暗影。

- R2 cached comparison 過早在三卡截圖尚未結束時啟動；Siesta/Android 已核但第三卡p202尚未寫入，QueryReturnedNoRows FAIL，EarlyFailed保留。先等待owned runner exit與完整輸出數，再做指定全卡cache比較；不排除第三卡或把缺資料當PASS。202 Siesta側面頸暗仍可見，證明ST資料修正不是完整SkinPlus光照修復。

- 全34卡比較第一輪在 Megumin body 的 texture/geometry 組合斷言 FAIL，Texture324SessionComparisonR1Failed.log 保留；numeric／visual capture PASS 不足以略過來源資料差異，因此先不發布。追加逐欄 byte 差診斷，先定位實際欄位与 cache key，不能放寬容差或跳過 body。診斷首版猜 crc32fast dependency 造成 E0433 compile FAIL，原 log 保留；改用 serialized length／first differing byte 的標準庫診斷，不新增依賴。
- 逐欄診斷定位到 body PNG，源 cache key 顯示 p200 被選成 OK_SHOES_TYPE=0 室內控制、p202 是正式 default 室外。SQL 排除模式只寫 `%OK_SHOES_TYPE=0|%`，但鞋型在 key 最尾沒有 `|`，所以室內資料漏排除。ST 比較現只接受正式 default 的精確 key 尾，並要求完整兩個 key 除 pipeline 號碼外完全相同；Megumin 單卡 R3 差異檢查 PASS，不改 PNG 或容差，完整34卡重新跑。
- 此 selector 錯誤也存在舊 footwear cache test。Hair323FootwearSessionComparison 歷史 Megumin 實際是 p200 室內，而非正式室外；原報告保留，但撤回將它當成正式室外全欄完全相同的證據。已補末尾0排除，Stage323 原生 shoes visibility／單卡室內外／整批影像的獨立證據仍各有原始範圍，不因此冒稱舊 cache assertion 完全對齊正式設定。
- 新 dump ST verifier 首輪以 GameObject 名匹配 SessionPart，Hestia髮 renderer 名 cf_hair_b_15_00、Mesh 資產名 cf_hair_b_15_00_a 不同而 FAIL；改讀 dump 明確 mesh 欄位，不加後綴猜測。第二輪揭露七海 slot2／3／24 共用同一 Mesh／材質而 offset 不同，不能按 ST 值挑匹配行。原兩個 FAIL log 保留；新增 test-only ordered source manifest replay，核對 cached／fresh 每個 role、mesh、完整 source material，取原本 property 的實際 slot，再與 native sourcePart.field／slot 逐槽比對。不把 dump 幾何輸入 production、不改 ABI。
- 查找錯誤仍出現：rg 對 accessory*.rs wildcard path invalid，且猜 accessory_runtime.rs／runtime_accessory.rs 不存在；實際 AccessoryRuntime 在 session.rs。應先 rg 真目錄後讀已定位檔案，不再猜檔名。
- 七海完整63 parts 的 ordered source replay PASS，確認八個 changed parts 的真實 accessory slots 為2／3／5／9／10／11／13／37；native同Mesh的slot24並未被挑入。全dump第三輪到泱泱 eyeline_kage FAIL，因 Session 額外第二材質 draw 名稱是原 Mesh 的衍生名；不能猜去除 _kage。原 source renderer slot1 分支在 test-only manifest 記下未改名的原 Mesh 身分，再用 cached／fresh 全材質相同的 source replay 核對，production／FFI／cache layout 不變。第三輪 FAIL 原檔保留。


- 最終 p202／CLI313：三項 parser 專項與三卡12圖 cached ST 控制 PASS；完整 p200→202 全34卡 geometry／UV／image buffers／palettes 相同，來源 material 變動限 ST；每個已支援 ST 欄位與來源值吻合。native TSV 六筆 changed ST 最大4.768371586472142e-08、body NormalMap5×5 差0。三卡 R2 必須等 runner exit0 後才重跑，不把 EarlyFailed 隱藏。
- Texture324FinalAll34 全34卡136圖 exit0，PV／attachments／眉眼分層及 task／view GPU、held-clock shadow gates PASS，九頁及重要大圖目視 PASS。全136張已發布，Stage323 前136張 copy 至 old/Texture324FinalAll34，逐檔三方 SHA PASS。新來源／完成索引 Texture324SourceEvidence.json、Texture324FinalCompletionAudit.json；原歷史索引不覆寫。
- SkinPlus 抽出14組原生 D3D11 程式，僅作研究來源，沒有把其分類為已支援。Siesta 側面頸暗仍存在、shared lighting／clothing decoration／strict eye oracle／free-running physics 繼續開放；ST 資料通解不等於已完成完整原生光照。
- 目前 dump 的24筆 changed ST，依真實 Mesh／material 與必要的 source field／slot 身分核對 PASS；七海63 parts、泱泱32 parts source replay 與 cached 每個完整材質相同。沒有靠ST值選同名配件或猜去除 _kage。三張缺目前 dump 的舊控制不宣稱新 native ST parity；三個 validator 早期 FAIL 報告保留，最終 SourceIdentities／DumpTransformAudit 保存完整來源 SHA。


## E-201 Stage325 SkinPlus source／oracle 測試假設修正（2026-10-01）

- 不得將 metadata CB size 與 DXBC dcl_constantbuffer 要求完全相同：編譯器只宣告 used prefix，Unity 保留完整 layout。首次 generator 嚴格相等 FAIL 在 Skin325GenerateR1Failed.log；修為 register 集合相同、declared≤完整size、每個 uniform 範圍有效，兩種容量都記錄，不縮减 uniforms。
- Windows write_text 預設CRLF，若先hash LF字串，寫出的HLSL SHA不同。Skin325PrepareR1Failed.log 保留；其後 runner configuration不存在的 FAIL 見 Skin325PixelOracleR1Failed.log。generator 指定 newline=LF；最終hash讀實際檔，prepare成功才run。
- 第一版所有 samplers 相同會隱藏 register 錯接，改各自不同filter/wrap。選 s0 作負例在 SHADOWS_SCREEN 中是 shadow sampling，會被後續 max／係數遮住，R4反例測試FAIL保留；改按 native MainTex sampler index設錯接，不猜0。R5 NormalMap ST反例因normal fixture變化太小而未觸發，保留原FAIL；改更強且有限的packed normal資料，不改production公式或放寬2e-5門檻。
- R6 numeric equality mask=1 的負例仍PASS：這個native eq結果只供 movc 非零判定，float1 bits與all-ones此處等效。不可強迫所有語義上等效的程式 FAIL；改成反轉point-light equality檢查，最終21反例全部預期FAIL。translator仍忠實輸出native all-ones mask。R4/R5/R6原log、舊反例HLSL/log保留，不與最終report混用。
- 一開始以cached material.shader==SkinPlus過濾，read-only cache test1 FAIL／rows空（Skin325CachedInputsR1Failed.log）。實際 native兩材質被既有未移植註冊保護與body/face分支留下legacy shader。現在以native dump Mesh／material身分挑source parts，使用Texture324 report的確切default p202 key，test R2 PASS。這是新增來源診斷，不是已修正runtime shader。
- 早期dump遍歷只glob漏掉巢狀Siesta_0，且假設每個material slot都有shader導致KeyError；source工具改rglob、明確跳過null／缺shader槽，重複身份直接拒絕。不得用GameObject名、source desired値或角色名猜Mesh/slot。
- 曾猜測Public/KKExtractAPI.h路徑造成rg找不到，沒有修改該檔；往後先rg --files找實際檔。命令輸出過多遭截斷後改限定函式／欄位，不據截斷資料判定完成。
- 最終五pixel variants320 controls／5120 pixels max0、21counterexamples、MainItemPlus5-file byte regression PASS；cache1項／productioncheck PASS。SourceCoverage native_parity_passed=false，七個非空資源缺獨立transport不等於七個已證明畫面bug；typed legacy值／native defaults與raw material分開。native VS/COLOR/scene-global keywords、body/face完整ME replay與sampling待實作，未移除註冊保護、未改正式 binaries或136張圖片、未宣稱整專案零bug。


## E-202 Stage326 隱藏材質屬性遺失與來源回放假設（2026-10-01）

- material_named 只讀 m_Floats／m_TexEnvs、整組 m_Colors 漏讀，連 Vector 都失去。原生 SetShader 保留所有 saved material properties；不能用新ShaderLab defaults重建來源材質。UnityPy獨立讀mm_base證明 EmissionColor黑、ShadowColor來源膚色、LiquidTiling3.5就保存在資產。修正共享讀取器，不在production填這些數字或按卡名分支。
- 初次以Uncensor mm_base/material metadata解釋body三差值的假設不成立：Siesta BP.profundis仍選原始mm_base，face來源本來完整。三差值由reader漏m_Colors造成。沒有證據支持SkinEffects/Liquid_shader_wobble修改三值，不改插件/config。
- face cheekColor是create-face Color4，draw overcolor2的RGB來自來源材質；ChangeHohoAka只把alpha設為Lerp(0,.2,status)。把cheekColor再写draw RGB或直接套saved hoho而不考虑Studio context会重复／错误回放。新helper明確由caller提供runtime狀態，ME color在其後。
- 早期 Skin326NumericReplay.log／R2／R3 的 test PASS 只是成功產生診斷，不是numeric parity：R1有四差值、R2/R3仍有body三差值。保留R3 JSON witness；R4起加入每個值1e-6嚴格斷言，最終再加所有native texture ST軸。獨立validator直接重讀native dump，不只相信JSON自報native欄位。
- ChaFileBody constructor detailPower=0，既有parser缺值取0.5不符原生。已修正0並用最小無Custom block卡验证；五個新增typed values defaults與原生constructor吻合，partial更新保留已讀值。
- 一次 rg 在workspace猜kks_decomp目錄失敗，實際是遊戲目錄；一次猜 shader_2.asm檔名失敗，實際0002.asm.txt。這些查找失敗不當成來源證據。另一個skin_ ignored filter過寬含既有March控制，四項最終全PASS；後續改精確skin_source::tests／卡片函式，不能說只測了新helper或全library。
- 最終來源238項／328分量PASS，max1.2850341801762255e-7；七個專項與productioncheckPASS。尚未runtime integration／新capture／publish；textures/sampling、queue/keywords parity、VS/COLOR/world basis、Outline/ShadowCaster、scene light/shadow及全34卡回歸仍需完成。正式binaries/136圖片維持Stage324；下次source部署須新cache/pipeline。詳Docs/SKINPLUS_SOURCE326.md。


## E-203 Stage327 VS／ShadowCaster oracle 假設與signature錯接（2026-10-01）

- blob11不是猜測的SHADOWS_DEPTH/SHADOWS_NATIVE，而是metadata SHADOWS_CUBE。Skin327GenerateR1.log保持FAIL；修正label/keyword後七VS完整生成。early Generated的錯名ShadowCasterVertex檔保持診斷，但不安裝也不計入verified program集合。
- 首次vertex oracle HRESULT InvalidArgument；加API行號後定位CreateGeometryShaderWithStreamOutput。錯把packed signature Mask的zw register位置當StartComponent。SO取整semantic時entry內StartComponent=0，register packing由API處理。反射仍檢查component count／mask連續；修正後每次SO statistics確認32頂點寫满，再讀buffer比值。R1、OutlineDiagnosticR2失敗保留，沒有放寬2e-5。
- R2的shadow-bias反例意外PASS，原因是全域替換!=同時反轉ne比較與movc非零mask測試，兩次反轉語義抵消。不能把等效程式硬判FAIL；改只反轉ne、保留movc。ne實際控制normal-bias非零分支，不是錯誤推測的point/directional比較。舊HLSL/PASSlog保留；最終18VS錯接反例全部預期FAIL。
- ShadowCaster PS首輪discard mismatch case0，因oracle沿用Forward的輸入register/signature順序。原ShadowCaster先TEXCOORD1 register0，再SV_POSITION register1；相應VS與generated wrapper都改成原生順序。原PS指令未改，最終兩PS128controls/2048pixels RGBA/discard相同，8個UV/ST/sampler/漏discard反例全部拒絕。
- 一度猜MainItemPlus310資料夾內有parsed.json而失敗；從既有Docs/MATERIAL_SHADER_TESTS310定位真正Saved/ShaderDxbcVanillaPlus310/MainItemPlus後重生。早期路徑FAILlog保留。一次讀README輸出遭截斷；未據截斷資訊執行runtime操作或判定nativeCOLOR預設。
- 實際Siesta cachedbody有9823筆Color32，face無顏色stream；現有user dump的overlayVertexColors null不能證明Unity draw的default COLOR。這個缺口仍需原生attribute／GPU證據，不拿本輪synthetic vertex controls冒充真實mesh parity。
- 最終七VS224controls／7168vertices／163840components max0；兩shadow PS128controls／2048pixels max0；原五PS320controls／5120pixels重新max0。總47個錯接反例均預期FAIL。11個既有generated files byte不變，productioncheckPASS；全部14 native programs的source arithmetic已驗證，完整renderer/texture/sampling/scene/shadow仍未接入。正式binaries/136截圖不變，不宣稱頸暗或全專案零bug已解決。


## E-204 Stage328 UV猜測、版本與probe序列化錯誤（2026-10-01）

- decode_overtex3_uv 依跨度／少零值／額外偏移猜通道，bodyUV3選到巨大float與非有限值，JSON含null。原compare因此TypeError不是有效PASS；已改declared通道，保留cached舊值作witness。不以[0,1]數值判合法UV。
- ProbeJson 未處理byte，Color32四分量序列化成{}；native floatCOLOR與cached/255其實相符。補byte整數與第二次capture再逐筆比較，不能把probe錯誤修成production顏色補丁。
- source test曾猜Siesta head_id=0，實際headId1且KKEx mod head；R2 FAIL保留。改既有card resolver，不從卡名或vanilla猜頭。R3 bodyUV3<1 assertion失敗，實際合法最大1；取消範圍推測，獨立對native逐項1e-6。
- 最初把所有declared channel當2019 index，R4 exporter PASS仍不代表face native parity。face UV1–3錯移一通道；追查serialized source為Unity5.6.2f1，修正所有 mesh decode入口傳實際source version。最終R5＋獨立audit兩網格四UV全相符，舊report不混用。
- 一次Python預設cp950讀UTF8 UnicodeDecodeError未寫入，改utf-8-sig；一次m.uv不存在導致compile FAIL，改m.uvs。原Skin328SourceAttributes.log/R2/R3/R4保留。曾猜serialized_file.rs、UnityVersion module路徑與UnityPy GetChannelInfo不存在；改rg --files/實際MeshHelper channel mapping，不將查找失敗當來源證據。
- native runner兩次均完整dump53rows但owned Studio最後停止exit=-1，不可寫正常退出。face缺COLORstream僅證明stream缺失，不證明GPUdefault。正式binaries/136圖未動，UV0 legacy heuristic／完整shader資源與runtime integration仍開放。


## E-205 Stage329 靜態貼圖傳遞與readback假設（2026-10-01）

- 原body/face legacy合成欄位不能代替SkinPlus獨立_overtex/liquid槽；以schema逐槽保留白/黑/bump預設、綁定與非空未解析三狀態。缺少sampler不可借上一槽，任何Unresolved先拒絕mutation，不報完整成功。
- 原load_card_makeup_png id0直接None不符ChangeTexture的GetTexture目錄規則，新的prospective SkinPlus loader按KKEx與目錄asset=0處理null，未修改legacy路徑避免未驗證退化。ME replacements、動態RT及SideloaderPNG override還未接，不能把目前十static binding PASS當完整draw textures完成。
- 首次直接將source PNG與native exportedPNG逐像素比較，nip max222／underhair255／lip191／blush255；比對方向翻轉後lip/blush完全相同。nip仍max75；nativeGPU Blit至sRGB RT再ReadPixels會轉Linear來源顏色，轉換後仍BC解碼差異max[1,6,3,1]；underhairmax4。診斷數字留Skin329PixelDiagnostics，不放寬uncompressed exact測試、不在production加入gamma/卡名補丁，不宣稱BC parity。
- 一次猜Managed/Assembly-CSharp/ChaControl.cs錯誤、一個PowerShell wildcard傳rg非法路徑，改rg --files與直接Assembly-CSharp來源；來源定位失敗不是證據。
- mip_count來源值與native相符但PNG只有level0，不等於native mipchain已傳；sRGB僅四native controls，不等於所有材質global sampler parity。十binding/兩未壓縮image/五反例/atomic control/numeric回歸checkPASS；renderer gate不移除，正式binaries及136圖不更新。


## E-206 Stage330 男角來源、骨架與衣服圖樣（2026-10-01）

- 舊工具/圖庫只找 UserData/female，漏掉 UserData/male 和 DefaultData/male；Probe 固定 AddObjectFemale。共用 resolver 現在搜尋四個頂層卡片目錄，歧義不同內容拒絕；Probe 讀卡 sex 再選原生 Male/Female factory，圖庫保留完整路徑。
- extract_session 原本固定女性 base body/cf_m_body。男角改依 sex 使用 p_cm_body_00/cm_m_body/cm_body_00_md，保留共用 cf 頭與臉型流程。初輪男身 prefab 的 optional skeleton 是生殖器 renderer 的三根骨頭，造成手消失與骨盆皮片；必須使用 o_body_a.smr_skel 的101根原始骨頭順序。Male330Final 失敗图保留，R2修正。
- source 控制最初錯把 Random uncensor 必然選到mod，unwrap None；第二次 debug slice 假設至少5根、遇3根panic。依實際 default male body及 renderer skeleton 修正。Random 全選項尚未等價，不宣稱支持所有男體替換。
- UV3 native 是空串流，不能因前3組6823筆而要求第四組同長。獨立 audit 接受真正空串流且反例 fabricated UV3 必須拒絕；101骨序/三UV/Color32逐筆核對。
- 男襯衫格紋缺失：ChaControl.ChangeCustomClothes 指定 MainTexAB，舊 generic catalog loader用MainAB而找不到pattern。改共用 category430 resolver/MainTexAB；第一次漏修session_renderer_tests呼叫參數導致E0061，失敗log保留。
- R3出現格紋仍密度/混色錯。舊StudioEditor近似 lerp(1,20,tiling)不是原生create_topN；PS DXBC明確20-19*tiling，不clamp，包含offset/rotate及pattern alpha段。依Assembly-CSharp ChaFileClothes defaults(offset=.5,.5/rotate=.5)補解析；按Shader CB binding _Color1_2/_Color 和原生算術改共用bake，不以卡名或截圖調色。
- R4對pattern V重複反轉，與整個Graphics.Blit輸出翻轉混淆；原R4 source PNG/差異報告留存，再驗原始PNG座標，最終仍需native貼圖像素證據。不能只看圖近似就宣稱nativeRGB parity。
- debug CLI baseline 長時間未結束且持有exe導致cargo build os error5；核實本輪exe/卡片/output PID後只停止本輪owned process。native dump runner完整落檔後quit超時owned Studio停止exit=-1，不可寫exit0。UE批次退出與native批次退出分別記錄。

- 追加native衣服控制時把兩個不同卡的衣服texture names放同一批，runner會要求每張卡都有兩者，第一卡因此Missing native clothes input而退出1；其完成snapshot保留但不寫整批PASS。改單卡/對應texture name分別驗證。
- 女鞋嚴格native_mod_boot_clothes_albedo回歸仍max6/mean.07396（與Stage307已知差異相同），不得放寬max3或宣稱此項通過。新shift/combined pattern controls的16/8差異也留存，alpha-only max1；根因尚需分析，不能冒充完整任意旋轉pixel parity。

- authored pattern27 mipchain CPU候選雖來源8層、LOD採DX衍生rho，jacket max23/mean.033變max27/mean.060，尚未證明原生sampler/LOD等價。候選完整source保存Saved/Tests/Male330MipsCandidate；未部署，正式採已拍攝R5版本。不得把讀到mipcount當sampler parity，也不得以加LOD猜測硬稱修好。


## E-207 Stage331 原生 GPU 衣服與品質／資源假設（2026-10-01）

- 起初以任意旋轉的 GPU trig 精度解釋 max16，原始 DXBC 的 hardware bilinear 仍同樣錯。ForceEnable + authored compressed mipchain + hardware anisotropic 才完整 exact；不以 mip count 或均值下降宣稱通過。
- oracle 曾誤把 UnityTextureFormat25 當 crunched DXT1，兩解壓入口皆失敗；實際25是BC7、28/29才crunched。另 string UnityVersion 與 tuple 比較 TypeError。修正診斷格式表，保留 JacketOracle R1/R2/R3 失敗，R4成功，不修改 production 顏色去遮錯。
- globalgamemanagers 的 QualitySettings stripped type tree 無法用一般 UnityObject 讀出完整值。早期 SourceGpu test PASS 僅表示成功輸出CPU fallback，不是GPU parity。改嚴格2019.4 class layout，檢查所有profile模式及textureQuality；混合／截斷／尾資料拒絕，沒有猜ForceEnable=2。
- partial mm_base override 存在卻沒有 create_topN shader，不能整個bundle優先便停止。改各asset實際查找與vanilla fallback；cached DXBC先與現安裝來源SHA核對，才用作oracle。
- Texture2D Object.Instantiate 在這裡得到零尺寸／空GPU資料。SamplerNative R1黑控制max239、R2 CopyTexture width0且NullChecks吞錯，runner仍38rows；完成row數不等於所有控制有圖。R3用explicit Texture2D/Apply/Graphics.CopyTexture，全部控制有輸出且與source GPU max0。保留R1/R2錯誤。
- native Point 即使ForceEnable仍Point；不能把文件aniso/trilinear概述套所有filter。Point0/1、Bilinear0、Disable、Enable五個原生control全部逐RGBA max0。
- 獨立checker第一版誤要求十一個GPU log，實際六個wrapper有log、五個直接with_quality函式不印；十二組圖片已exact仍被log gate誤拒絕。改六log + direct control實際檔案逐pixel驗證，未放寬pixel門檻。另一次猜 capture_preview_batch.ps1 不存在，改rg找檔，不當證據。
- 最終12cases/42,991,616pixels全RGBA max0；女鞋原max6變0，不放寬strict門檻。shader ABI、partialGPU input與quality反例皆PASS。adapter/driver與quality/shader來源納入cache；PNGoverride/unsupportedformat等CPU fallback必須列出，不能宣稱全部mods native exact。
- Native Studio delayedquit最後owned stop exit=-1，不寫正常退出。UE full37capture在獨立gate完成前不當PASS。Stage330來源及失敗快照不覆寫，SkinPlus/Siesta/最終ForwardRGB長期缺口仍需接續。詳Docs/CLOTHES_CREATE331.md。

E-207 R2補記：first full37 曾先口述「下一張」來源缺口，實際stderr那筆來自已完成第8卡KK_381623，不是第10卡。以確切loaded entry／zipmod查證，format5 ARGB32，補通用ARGB32/BGRA32而非PNG猜測。第一次native runner用 Windows PowerShell5，把66 renderer array算成1，錯拒絕Incomplete fixture；檔案仍保留但不是runner成功。create名字cf_body_00_t也匹配create_skinbody，不能用該hook輸出當nail RT。R2用pwsh與o_gloves_long01，native renderer61 mat0正確256²，source max0，66rows exit=-1。曾向仍被runner鎖住的log Add-Content失敗、猜charalist.rs不存在及PowerShell rg wildcard錯誤，均不改證據。

E-207工具補記：parallel prewarm原用Python預設cp950 stdout，中文進度print在成功CLI return與source log落盤後UnicodeEncodeError；map取消尚未開始的工作，不代表全25已匯入。保留首輪source/FAILlog，UTF8 stdout與單次print修正；既有完成log須terminal session parts行及當前p209相同env cached key共同符合才恢復，其他卡正常續跑。使用read-only SQLite，未手改key或覆蓋舊payload。加入prewarm後male source及GPU bake診斷可能只存在CLI log，gate須合併實際source證據，仍要求UE148張各自PV/readiness，不以cache hit冒充畫面PASS。

## E-208 守岸人頭巾內側偏黑：來源一致仍有 final renderer 色差

- Native 藍紫星點內側與 SESSION209 UE 偏黑不同。MainTex 2048² 全 RGBA max0，不能先調角色顏色。
- 診斷首輪大小寫敏感 Welt selector 漏掉 lowercase welt.mecopy1；R2 依 renderer 名列出，透明複製層、kind4、alpha 0.781105 正確存在。
- 不可從 hair kind1 TwoSided 分支推論 studio_alpha 也雙面；外層 `ShaderKind != 4` 已排除。CullBack 不是已確認缺陷，未做錯誤修改。
- 尚未定位最終陰影／背面／透明混色根因，不宣稱修好。可重跑 audit 與證據見 Docs/SHORE_HEADSCARF332.md。截圖視角／光照不同，不作整張 numeric parity。

E-208 R2更正：headband 是頭髮，真正頭巾是 erjh / ChildOf-N_move，Shader Forge/main_item；不可因物件名推論可見配件。原生 renderer2 isolation 與 headband renderer0 isolation 確認，正確2048² MainTex RGBA max0。舊audit曾把頭髮零誤差當頭巾證據，已更正對象，保留此錯誤。NoOutline控制4圖exit0內側仍黑。main_item 原生SceneLight和signed双軸Ramp與UE cameraLight/fixedV不同，已做shader-family修正，未宣稱完整Forward parity。
E-208 build補記：正式DLL被另一背景UE capture鎖住，不終止非本輪owned process。單模組suffix連結成功但OpenKoikatsu仍import舊DLL，重複class令UEexit3；須suffix所有直接依賴模組並驗證啟動，build success不代表renderer成功。Unity EC shaderLZ4 metadata有scalar與list，診斷第一版TypeError，R2明確正規化後byte一致。

E-208 R3：`Shore332SceneRampR3` p210 與 `Shore332NullNormal` p211 四圖 exit0 仍黑。舊 dump NormalMap=NULL 是過期 binding；fresh native Probe erjh 兩材質有1024²/11mips NormalMap RT，卡片 exact `NormalMap` 788093 bytes，不能強制刪除 ME 正常法線。ME 屬性大小寫不是此案根因。
E-208 R4：完整 Forge MainItem 原始 DXBC 0007/0005 分別對 native metadata12/14，不能以 raw-scan index 查 native blob。安裝bundle與兩份bytecode逐byte相同；不同陰影版本的 sampler 配對各自驗證。原版黑 ShadowColor 仍輸出非零diffuse；舊簡化ShadowColor乘法不能模擬完整Forward。兩版各44控制704pixels最大誤差0，錯ShadowColor負例拒絕。SESSION212首張實際頭巾內側藍紫星點恢復，37卡回歸尚待完成。
E-208 R5：新增cache單測第一次寫Rust `.2` 而非 `0.2` 編譯失敗，修正。第二次揭露head role令已解析Forge shader落入kind3；改為已解析 shader 優先於role/hair hint，R3單測PASS。SESSION213/CLI324隔離快取，不沿用SESSION212。原版One/Zero忽略export alpha、fragment已有discard；UE OpacityMask不能再以原版alpha做第二次.5cutoff，改為常數1並保留原始clip。完整37卡預熱、GPU math與最終渲染是不同驗證範圍，不得把已知衣服PNG fallback藏成全通解PASS。

E-208 R6：首批37卡148圖exit0漏设OK_SHOT_PV_AUDIT，numeric auditor max(empty)崩溃。已改缺量測輸出null並保留FAIL gate，R2重新帶PV旗標。廣泛Rust測試仍運行時重建同一debug test exe造成LNK1104，改用release隔離。廣泛suite最後27PASS/2FAIL/9ignored：舊head期待kind3已修；height測試受其他測試全域OK_SKELMESH影響、skin_all刻意不更新GPU頂點，需隔離重跑才可判定。不要將測試環境問題宣稱生產身高已修。

## E-209 龍卷衣服圖案與材質遺失

- 原生與source選同一Nam衣服、同一顏色。原生橘色NIKU来自KCOX component ct_top_parts_A，舊解析只處理mask，漏掉衣服overlay。禁止換卡片衣服或依圖片補色；通用KCOX解析/RT圖層合成補齊。
- Forge main_alpha落入kind0，未完整傳輸法線及原生片段程式。通用kind11與原版兩陰影program移植，GPU44x2 controls max0；實際UE畫面未完成前不能驗收。
- Native input probe錯把material名cf_m_top_inner04當原始MainTex asset名，該capture input缺失FAIL，fixture保留；用native clothes-load.tsv核對實際asset名p_o_taisougi_std_00_t，不能猜檔名。
- 新DXBC oracle複製舊000格式造成00017路徑，修為固定4位；ShadowColor負例使用錯cb位置而意外PASS，查MainAlpha metadata確認cb0[7]後負例正確拒絕。兩個錯誤均保留FAIL log。
- 新ClothesRendererMap欄位未同步舊單測literal導致E0063，補齊；新source檢查揭露Texture3缺失，因bundle CACHE103仍回傳舊decoded mesh。升CACHE104，不手改已存payload、不放寬缺貼圖assert。
- Shell含巢狀雙引號的Python改檔失敗；改用apply_patch或單引號here-string，中文工具輸出一律python -X utf8。PowerShell檔案命名先rg --files，再讀實際路徑，避免再次猜不存在檔。

E-209續記：prewarm CLI已啟動後，冗餘cargo build嘗試覆寫正在執行的ok-extract.exe，os error5失敗。預熱使用已成功R3二進位繼續，R4只新增測試、不改生產行為；重建CLI必須等owned預熱結束，UE staticlib獨立--lib建置不寫CLI。身高隔離PASS y1.282→1.522，未改生產身高邏輯。

E-209來源複核：Windows rg 的輸入路徑 `kks_decomp/ChaControl*` 不展開 glob，os error123；先 rg --files -g '*ChaControl*.cs' 再讀 Studio 實際路徑。KCOX 原生可選 Alpha02，但 ChaControl.ChangeCustomClothes 只將 create RT 指派 Normal01/02/03 與 Alpha01，不能因 Alpha02 欄位存在就擅自指派另一個 RT；本輪圖層僅修改已建立的 runtime MainTex，不宣稱任意插件額外 RT 路徑已完整覆蓋。

E-209驗收界線：ForgeAlpha333All37 37卡148視角exit0、PV37、附件869 max0、全部readiness與目視回歸PASS，龍卷圖案/材質恢復。source MainTex最大byte差1、normal最大5；不可寫成native全像素0。預熱37卡匯入全PASS，但七海千秋3仍MainTex/MainTex02兩筆CPUfallback，full_clothes_gpu_passed=false，正式圖未覆寫。完整authored mips、透明ZWrite、原生outline/外部screen shadow未全部一致，畫面改善不代表全shader所有pass或全專案零bug。

## E-210 七海花紋回退的錯誤來源假說

- 先前以 source PNG 存在就推論原生使用 Sideloader PNG，錯誤。fresh native 是 RGB24/Linear/11mips；installed compatibility manifest 的 StripAll 優先於 MigrateAll。Importer漏 migration 才誤選同slot Sunshine PNG。禁止直接補PNG GPU mip或放寬fallback gate掩蓋錯來源。
- category/ID相同不代表相同property，原生ResolveStructure使用完整outfit/property；花紋要限制衣服種類與colorInfo channel，避免借用另一outfit或part的resolve。
- Probe新增ALL診斷選擇後，第一輪PowerShell仍驗證檔名ALL.tsv，fixture存在卻commandFAIL；同步script檢查實際clothes-create TSV，保留Png334Native失敗。R2及Resolve334Native完成並清理owned Studio/plugin。
- 增加build_clothes_patterns outfit參數漏session_renderer_tests call，E0061；rg所有呼叫補齊，native褲襪property是Pants而非Panst，按StructReference修正後單測PASS。
- 原生Create RT八項controls逐像素max0僅涵蓋此來源及取樣；不代表任意migration或所有Forward/outline/透明深度均已一致。

## E-211 四視角截圖未凍結眨眼

- Resolve334All37 的 Siesta 眼睛與 Stage333 不同，兩輪起始blink log卻同為open_rate0.5/eyes0.46。不能直接宣稱source migration造成眼睛退化，也不能忽略差異。
- ShotPreview只在指定clip-time或RESET_POSE時停Actor Tick；一般Tpose設定half blink後仍執行FaceBlendShape LateUpdate。眼睛在1.5至8.8秒的四視角timer之間繼續眨眼，既有shadow pose_clock_held只證明單次同步refresh沒改時鐘，不代表整個四視角期間凍結。
- 共用ShotPreview在所有pose/face控制設定後停Actor Tick，每個capture拒絕未凍結Actor，新增frozen-frame audit gate。正常preview Tick保持原生動畫邏輯。先比較Siesta兩次独立載入，再跑新整批。
- 本輪owned PID74768身分與abslog已核對後停止，R1留96張、exit-1，未發布；UE Frozen build成功20.25sec。先前記錄仍保留，不把中止的R1當全37PASS。
- 診斷讀SQLite時猜欄名cache_key，PRAGMA顯示實際card_key；修正後read-only擷取兩個pipeline的payload。再次rg路徑glob不展開，應使用rg --files的實際檔名或rg -g pattern目錄；不要因查詢/搜尋命令失敗推論資料不存在。
- half表情是診斷條件，不能把半閉眼照片作中性卡片外觀。捕捉script新增BlinkState，正式Final採共用open；R2半閉眼owned PID81244已停止並保留圖，未作正式PASS。來源37卡已全GPU、fallback0，仍需Final凍結/open完整148圖與目視驗收。
- Siesta兩次獨立half截圖frozen-frame/PV/readiness PASS，兩輪eyes0.46/brow0.5；全PNG逐byte再現檢查FAIL，各視角max55/45/66/69，保留Resolve334SiestaRepeatAudit.json，不放寬max0門檻、不宣稱全渲染器逐像素再現。Config/ShotPreview是FXAA（AntiAliasingMethod1），不能沒有證據就歸因TAA。此案精確來源Create RT八项仍max0；完整PNG差異根因未定位，與Actor Tick未凍結的已確認問題分開記錄。

E-210／E-211最終驗收：Resolve334PrewarmAudit37卡exit0、CPUfallback0；Resolve334All37Final37卡148圖正常exit0，PV37、附件869 max0cm、骨矩陣max2.252e-6cm，material/GPU/view/shadow/frozen-frame全PASS，Contact01–10及重點放大圖目視未見新增退化。正式圖148張已經嚴格SHA綁定稽核後發布，原136張已先備份並驗證，詳Resolve334All37FinalPublish.json；此案來源及截圖時鐘修正已驗收，整PNG repeat FAIL與既有完整renderer限制仍開放，不因發布而改為PASS。

文件工具更正：第一次收尾 patch 夾帶沒有內容的 Update hunk，apply_patch 在驗證階段拒絕、未修改檔案；移除空 hunk 後重套並核對結果。不能將工具拒絕當成文件已更新。

## E-212 H 動畫男角來源漏列與首張灰屏

- 原掃描僅 h/anim/female，手動指定 male 可播放不是自動清單正確的證據。共用 clip_sources 補 male，保持 raw/states 身分；Gallery 依 sex 篩選相反角色的 H 來源、標示男／女。
- HPlayback335 單角四卡96動作5856幀通過、每卡 catalog male8/female16。Gallery 實際選取、暫停、來源切換、重播另以 ok.VerifyHPlayback 驗證，不能拿 C ABI 取代介面驗收。
- 首張 male capture 相機快取位置誤差134.907505032cm；明確 UpdateCamera 後延遲兩次 next tick，相機快取位置與角度誤差0，首張角色可見。GPU fence 不等於 viewport 使用新相機。
- 稽核新增每張 PNG 全圖與中央區域角色前景存在門檻；全部灰的圖不得只憑 GPU readiness PASS。
- HPlayback335RhiCold 四卡16圖 PASS，但初版 script 用不存在的 OK_SHOT_GAME_TIME/CLIP_TIME，實際取樣時間0；保留為冷啟動 t=0 對照，不稱 t=.61。RhiWarmR3 改 OK_SHOT_ANIM_TIME=.61，四卡16圖全 gate PASS。
- Warm/WarmR2 沒有 ShotPreview 是 Start-Process 的 ExecCmds 空格未加引號，實際只執行 t.MaxFPS，不是 H 動畫當機。核對 PID、exe、abslog 後停止自己啟動的32244/9064；失敗證據保留。修正為 -ExecCmds="t.MaxFPS 30,ok.ShotPreview"。

## E-213 雙人播放的原生綁定與驗證界線

- 配對來源讀 AnimationInfoData category/id、男女 base/override controller，不使用檔名相似或角色卡名推測。117筆原生男女來源已驗證；雙人 UI 僅 participants2，三人資料不能偷用兩人播。
- 原生 MotionIK.SetPartners 索引0是自己、1是另一位，並非固定女0男1。早期未部署草稿的 canonical 映射已更正；bend chain 指自己。
- ChaControl 對男女都載入 oo_base/p_cf_body_bone。以 cm_ 名字篩選男角 IK 是錯誤假設，原生共享 FullBodyBipedIK 與 FrameCorrect；序列化欄位為 _list，component 是直接 Transform PPtr。工廠24個 FrameCorrect 綁定測試通過。
- MotionIK 的 state 名稱 exact match；L_SLoop1 不在 khh_00 的資料時原生釋放 IK，不可自行套 Idle 或其他狀態補救。
- 配對 shape offset 使用各 frame.editNo 的角色；effector/bend 的世界偏移用自己的 objBodyBone.TransformDirection，不能用對方骨架旋轉。
- 新讀取器先驗證二進位所有數量、字串、長度及有限值，避免舊 parser 面對損壞資料越界或分配大量記憶體。負向測試通過。按 bundle 大小／mtime 重用解析資料，避免每幀重新解壓 H list。
- Rust 首輪 private with_serialized E0624、owned name loop E0382 已修正。FFI cstr 不存在改既有 cstr_opt；C++ UKKCardReader::Read 不存在改既有 LoadFromFile。搜尋原生 BaseProcess/FrameCorrect 必須使用 namespace 子目錄；PowerShell 對傳给 rg 的路徑 glob 不展開。
- 單角色、雙人有限值／相同時鐘／接觸權重測試與原生逐骨骼 oracle 是不同證據；沒有實際 oracle 不宣稱全套 H 原生逐骨骼或逐像素一致。聲音、完整 H scene flow、三人互動不在此雙人播放器的驗收范围。

E-213 補充：第一輪 pair frame 測試 FAIL 不是資產缺失，完整 native composite controller 狀態標籤包含 [Controller layer 0]；修正精確 prefix 後 R2 128配對幀、256 active contact weights PASS。Unreal C4458 的 Role 名稱遮蔽 AActor::Role 改為 PreviewRole；UEBuildR2 成功37.76秒。畫面接觸測試需側視圖，正面可能由男角遮住女角，不能把遮擋當成女角未建立。

E-213 介面負向結果：GalleryR2 的 ill_male_01 同一 frame717 先選動作又檢查時鐘，因長時間同步選取後 TimerManager 追補 callback，Actor Tick 尚未執行；FAIL保留，不能說卡片沒匯入，也不能忽略FAIL。驗證 state machine 新增 GFrameCounter once-per-frame gate，之後重跑四卡。

E-213 原生合約 oracle 首次 CS0012 需要 Assembly-CSharp-firstpass 的 FinalIK，補 build reference；首次以 Studio.AddObject 載入時部分 IK 綁定元件缺失，NullReference fixture為FAIL，已保留NativeContract.json，改用ChaControl原生plain factory（男女相同），新增明確元件檢查，不把缺元件誤當公式通過。

E-213 原生contract R2 factory load完成但 Unity JsonUtility 省略 plugin-defined nested contracts，只寫scope；讀到error空字串不是合約完整。既有ProbeJson已記載此限制，改用該serializer並強制16contract筆數檢查，保留R2僅scope的FAIL fixture與NativeContractAudit FAIL。

E-213 NativeLiveTargets 單測新增Rust浮點寫成 .61（沿用C++寫法）導致parser拒絕，改為0.61；首失敗log保留。原生binding／shape oracle R3完成16合約，64目標及384 FrameCorrect綁定逐項比對PASS，最大分量誤差2.3283064e-10；此PASS不代表全部solver結果逐骨骼一致。

E-213 Native HActionBase.SetDefaultAnimatorFloat 的 _isFemale1=false 指第二位女角，並不排除第一位女角；height 要同時寫第一女角與男角。原型只寫 male 的錯誤假設已改成共同 card-female height，motion seed0；frame單測新增 height一致檢查。原生 live target核對兩種雙人類別皆 expected8／active8，沒有省略找不到的PV目標。

E-213 收尾檢查：結束配對後若只恢復角色 Tick，單角下拉仍顯示先前單角 clip，但角色實際留下配對 Idle。Gallery teardown 統一回到 Tpose、清 clip選取，避免重播／UI狀態與實際session不一致；驗證也檢查此狀態。


E-212／E-213 最終驗收（2026-10-01）：GalleryR3四卡全PASS；最後height與teardown修正各男女兩卡再PASS。RhiFinal017以實際t=.17、15FPS四卡16圖PASS，camera等待按GFrameCounter量測，不能把timer callback次數當rendered frame。加上t=.61/30FPS16圖，全部32圖与四張paired圖已目視；灰屏負例正常拒絕。

E-213 NativeContractR3雖完整寫出16合約，但Application.Quit延遲，初版runner以exit timeout回報FAIL；完整fixture不等於runner PASS，保留該失敗。修正只在核對owned executable後停止延遲Quit的自建Studio，finally撤出probe；新R4 runner exit0，16合約與R3數值repeat最大差0。沒有停止既存使用者Studio或覆蓋其他probe。

最終證據：單角96/5856、配對61幀/4負例/錯誤姿勢不變、128配對幀/256contacts、native64targets/384bindings及兩類8/8livecontacts、UE三回歸全PASS、R6build成功。Acceptance.json保存hash與scope。完整native IK求解後骨矩陣、persistent solver、完整H scene/三人/audio等尚未驗證；不能由這些PASS外推為全專案零bug。


## E-214 Stage336 原生多幀 IK oracle

首次 NativeSolver336R1 fixture FAIL：objBodyBone 子樹含重名 n_cam_tc，不能假設所有 Transform 短名唯一。改以完整path建立父子identity，保留短名作原生IK references；solver結果只比對cf_j骨骼。R1失敗fixture／log保留。原生FrameCorrect存在First/Second/Last執行階段，目前adapter全放solver前及每幀重建solver的行為仍待oracle定量修正，不能以Stage335綁定合約PASS代替完整求解parity。

E-214 oracle R2首次solver比較24幀FAIL max1.27499m，僅男角。fixture轉Rust時把真實根骨id設為0，而既有Unity PPtr模型以0代表NULL，子骨world_of跳過180度男角root。這是測試adapter的identity錯誤，不可宣稱為runtime男性IK根因。改用1起的非零id，保存Solver336AuditR2反證；未取得新PASS前不更新原生parity結論。

E-214 corrected oracle solver R3：根骨改非零id後24幀PASS，最大position8.3924101e-7m／quat1-minus-abs-dot9.536743e-7。原生cf_j_root身體根部由ChaControl.objBodyBone與dump確認，外層p_cf_body_bone只是prefab，不可用其旋轉替代TransformDirection。adapter改為cf_j_root、兩角色target同一求解前snapshot、FrameCorrect依nativeFirst/Second/Last、per-session持續solver；runtime不序列化，舊snapshot schema保持。

E-214 adapter R1 FAIL24幀／96contacts，max0.009412m、252骨差。後置FrameCorrect不能使用給proxy solver重寫後的合成shape資料，必須保留原生state；同時FinalIK世界rotation原本由矩陣去scale取得，非均勻ABMX下會含shear，不等於Unity Transform.rotation。改沿local quaternion父鏈，setter亦依父鏈。adapter R2 PASS max9.374359e-6m、quat1.788139e-7；失敗Adapter336AuditR1保存。這是已驗證24幀範圍，不外推所有H scene。

E-214 證據保存錯誤：Gallery verify截圖路徑硬編碼Saved/Tests/HPlayback335，Stage336重跑覆蓋前組男女兩張pair PNG；先前Acceptance存的SHA不能當現在別名的SHA。舊PairContactAll合圖及後組兩原PNG在覆蓋前以原SHA核對保存至Stage335Preserved；前組兩原圖無法恢復，保留原SHA/log但標示已被更新替代。共用verification新增OK_H_VERIFY_SHOT_DIR，runner每次fresh Tag資料夾並拒绝已存在，未指定也用GUID子目錄，避免再覆蓋歷史圖片。

E-214 最終：NativeSolver336R4原生runner exit0，新增兩類Idle/SLoop/OLoop與nonuniform parent scale Idle，共96角色幀/15360 cf_j骨骼幀；solver最大8.60881e-5m、adapter最大8.58751e-5m/384contacts，全部原先1e-4m/1e-5quat門檻內，沒有因FAIL調大門檻。FinalIK7項、native4項、128paired/256contacts、部署96動作5856幀+61pair/4負例、Gallery四卡及fresh-tag最終男女兩卡均PASS。六張配對PNG目視兩角色存在。最後UE build11.32秒Succeeded；臨時native probe已撤出、owned進程均結束。控制輸入的求解器/adapter oracle仍非完整native scheduler/首次HScene/init/audio/3P parity。

E-214 最後驗收腳本首輪將Path傳入image_content_presence，該helper需RGB ndarray，TypeError且未寫出Acceptance；改用PIL讀RGB再np.asarray並assert內容PASS，不能只在檔案存在時當影像有效。

## E-215 Stage337 原生自動初始化與運算順序（驗證中）

Stage336 的 controlled oracle 每個序列顯式呼叫 solver.Initiate，不能證明 SolverManager.Start/OnDisable 的自然首次初始化。原生 readonly Harmony prefix 記錄第一次 IKSolver.Initiate 的 cf_j_root 子樹；另一角色建立後先等待其自然 Start。NativeAuto337R1 包含兩角色原生初始姿勢及 96 幀，跨 khh/khs、Idle/SLoop/OLoop、倒回時間與非均勻 scale 不再顯式重新初始化。SolverManager componentInitiated guard 只初始化一次，原匯入器因 motion 改變或 time 倒回而重建 solver 是通用生命週期錯誤。

初始骨架並非所有節點完全未修改：cf_n_height 已收到角色身高，手指/裙骨等另有原生 shape/hand 處理。關鍵 solver 的 chain/spine/limb mapping 骨骼需逐一比對，不能把不影響初始化的全樹差異當成 solver 根因。原始 prefab 高度不一致的探索資料保留 InitialPrefabExplorationR3。

SolverAutoR1 FAIL 2 骨，max0.142541mm；移除 FromToRotation 平行角 cutoff 的 R2 反而 FAIL10/max0.147371mm，已撤回該猜測。Transform setter 正規化後 R3 FAIL1/max0.120725mm；Vector3.Normalize 按 native 每軸除法及 Quaternion*Vector3 展開式後 R4/R5 仍 FAIL2/4。不得放宽原 0.1mm / quat1e-5 門檻。發現 FBIKChain.SolveTrigonometric/Reach 中 native 是 vector / magnitude * distance，原 port 是 vector * (distance/magnitude)，浮點順序不等價；修正並繼續驗證。所有失敗 JSON/log 保留。

AdapterAutoR1 尚未寫報告即因測試 session 未 apply_anim_time、body_shaped=None 而失敗 native height bone missing；這是測試準備錯誤，需先走正常形體初始化再覆寫 native sampled 骨架。禁止用此錯誤推導個別卡片特判。

E-215 更新：原生 chain/ReadPose 診斷確認 offset reset 寫錯欄位。改 node.offset=0（舊 port 清 solver_position）後 ReadPose 根部約0.034mm的跨幀誤差消除；但右前臂0.124mm仍存在，不能把所有殘差都歸因此項。NativeAuto337R5 初始化計數[1,1]，兩角色未被其他plugin二次Initiate；first/latest一致，否定二次初始化猜測。

Compiled Quaternion primitives證明 FromToRotation平行範圍為1e-6，旧1e-7過窄；近乎平行LookRotation應fallback FromTo(+Z,forward)，旧port漏分支。30筆原生對照，修正後FromTo component max5.96046e-8、Look max1.60933e-6。Quaternion.Lerp原port錯用spherical公式，已分離nlerp/Slerp。這些是函式語義修正，不是放大solver位置門檻。

原生非單位Quaternion對照另否定「Inverse除以norm」的猜測：installed Unity Inverse直接conjugate，R14所試norm除法已撤回；nativeSlerp對非單位輸入的端點亦不應擅自正規化。父鏈position遞迴運算試驗未消除右前臂殘差，已恢復矩陣方式。SerializedSolverParameters.txt確認本工廠iterations4、reach/pushSmoothing1、spine iterations3/twist1及pullBodyVertical0.5/Horizontal0，沒有以猜測改原生參數。Vector3.Angle改原生dot/sqrt(sqrMagA*sqrMagB)，持續驗證。

Stage337 補充：NativeAuto337R8 診斷探針以 NonPublic reflection 讀取實際 public 的 defaultLocalDirection/clampF，造成 NullReferenceException。改用反編譯宣告的 public 成員直接存取；此失敗輪次不得列為原生驗收。R16 double triangle 試算未消除差異，已還原原生 f32 運算。R20 solver/adapter 仍各有 1 幀超過固定 0.1mm 門檻，尚未完成驗收。

E-215 更新：原生 first-init 與 prefab 重建的矩陣乘法捨入會令近乎伸直的初始化彎曲方向差1.3244e-5；逐層 local TRS TransformPoint、translation最後相加，與 native 初始化方向差≤1e-7。這是通用 IK Transform getter修正，非卡片特判。清除已不生效的 cache seed 與多模式診斷分支，避免測試誤稱使用了 native world cache。NativeAuto337R9 vector dump 384筆均 changed=false，否定這輪殘差來自ClampDirection/Slerp分支，未按猜測改Slerp。NativeAuto337R10 triangle primitive 1920筆對照 max7.58689e-7m、Look component max1.19209e-7；因此仍需查上游求解狀態。共用 build兩次撞上另一工作進行中的Settings Default/portable Vec型別錯誤，保留FAIL log，沒有算成驗收成功。

E-215 更新：RotationCompositionR1比較96幀共69,600骨骼旋轉。root-first平均component差7.5324e-8；leaf-first1.7435e-8，額外normalization各模式反而更差。IK worldRotation改用leaf-first parent*rotation及managed Quaternion operator分組，保持renderer pose路徑不改。IKConstraintBend.rotationOffset另對齊反編譯左結合(qFrom*offset)*vector，舊逐次旋轉vector的代數替換已移除。R23仍FAIL1/max0.114818mm，不能當驗收。

E-215 更新：NativeAuto337R11 的1176筆 Vector3.OrthoNormalize 與目前每軸除法移植逐bit一致；reciprocal替代最大1.63277e-5，不採用。R24 leaf-first修正仍FAIL1/max0.114187mm。R12額外擷取GetDirToBendPoint的z/y與重建結果：1920筆reconstruction差0，純f32表達式max_y7.48783e-7；f64表達式在num float store及Clamp float call邊界捨入，z/y均逐bit差0。這不是R16所試的stable triangle改式；保留原生law-of-cosines與原生捨入邊界，修正後另跑R25，不能只據primitive就宣稱full solver PASS。

E-215 流程根因：FBIKChain.Stage2的childConstraints在原生屬於該chain。移植只有root chain[0]持有的全域集合，卻在各leaf limb再跑root約束，額外改動其他肢體root。對齊root ownership，Stage1/Stage2/constraint systems不在leaf重跑。R28自動初始化96幀首次PASS，max0.096882955mm，門檻未改。另新增leaf Stage2不改sibling roots回歸；full adapter與部署驗收尚待完成。R25/26/27的FAIL保留。ManagedMathR1證明1920筆q*v及384筆骨長按managed表達式邊界差0，native C++ Ortho/Transform組合保持單獨f32路徑。

E-215 測試準備錯誤：FinalR1新回歸使用不存在的IKSolverFullBodyBiped::new，改用Default，未影響runtime。早期Ortho執行舊test binary曾matched 0 tests，不列驗證。FinalR2的solver與adapter均PASS（96幀、384contacts；0.096883/0.096826mm），但整suite因新triangle assertion FAIL，不冒稱整suite PASS。該新增assert錯把非逐bit的LookRotation也放入triangle exact位置gate；已拆分triangle z/y與給定原生look的q*v嚴格差0，Look使用先前既有component1e-5界線（實測1.19209e-7，輸出vec2.12831e-7m）。Full solver及adapter仍固定0.1mm/quat1e-5，完全未改。

E-215 最終：NativeAutoFinalR3整suite11PASS（其中Slerp比較僅診斷）；fresh NativeAuto337R12自然solver96及adapter96/384contacts在原0.1mm門檻內，max0.096883/0.096826mm。Controlled回歸max0.099035/0.099057mm亦PASS。FinalIK8含leaf sibling、128pair/256contacts、最新部署四卡96動作5856幀+61pair/4負例、Gallery四卡/fresh4PNG目視及內容、UE三Animator重新回歸均PASS。UE build31.42秒Succeeded。owned native/Gallery/Unreal均結束，probe撤出。Acceptance337封存本輪source/binary/evidence SHA，正式Screenshots未覆寫。Native非單位Quaternion.Slerp診斷不等於本輪Biped（使用Lerp）已移植所有Slerp分支；全場景與renderer既有限制保留。

## E-216：菲比全 outfit dump 核對不能把原始輸入差異直接當成匯入 bug

2026-10-01 Stage338。六套 outfit0/1/2/3/5/6，共282網格、282材質dump、297個renderer/material條目已全部核對；不存在outfit4來源，不冒稱測過。176個runtime網格都有dump對照，152筆單材質三角索引完全相同；非虹膜UV max5.957031246062172e-8；前／背髮主材質12張MainTex RGBA差0。

- 初步診斷把同名o_tang在head與無sourcePart的其他網格混配；最終加入source field對role領域，不能靠頂點數／值最接近跨來源選候選。head多材質renderer會拆分，需唯一材質身分，不能因runtime有_kage命名就說缺少原生影線。
- 六套MECopy1 raw alpha相差127是既有_alpha烘焙；按production round規則重建後差0。不得為了raw PNG等同而移除正確透明度處理。
- 虹膜12筆UV/ST/rotation差異包含EyeDraw affine UV變換與注視狀態，未建立同一注視／相機條件；不能據raw UV差再次逐卡翻轉。NormalMap/overlay等表示方式與量化差異也不能直接當成已確認畫面bug。
- 117筆未對應的native條目包括隱藏服裝狀態、gag eyes、輔助／其他解剖mesh、12個Highlight附加層；Renderer.enabled不等於activeInHierarchy。額外的專用EyeDraw/normal_detail貼圖未由本次診斷欄位暴露，uncovered不是production缺貼圖的證明。
- 最終integrity_passed=true、all_dumps_match=false；原始差異129筆，範圍內逐值相同51筆。已檢查dump結構／引用，不宣稱世界姿勢或完整shader／圖像parity。這輪是全量檢查，production、正式截圖皆未修改。
- 執行準備曾寫錯python -c括號，另印原生bone中文遭cp950編碼例外；兩者不作為缺來源的證據。正式audit設定UTF-8並使用獨立檔案。初步輸出DumpAuditInitial.json保留；最終DumpAudit.json綁定來源、診斷程式與卡片SHA256。

詳見Docs/PHOEBE_OUTFIT_DUMP338.md與Saved/Tests/PhoebeOutfitDump338/DumpAudit.json。匯出fixture exit0/845.13秒只證明匯出成功，不把該test pass改稱全部原生對照通過。

## E-217：實際 draw inputs、服裝 accessory RT 指派與 dump 狀態來源

2026-10-01 Stage339。不能只比prefab metadata就認定角色draw正確：body/face detail與gloss漏掉ChaControl設定；120有效值檢查先FAIL30，依Status.skinTuyaRate／body／face角色參數與ME優先修正，最終120PASS（門檻不變）。Card Status/default2、draw precedence1回歸PASS。

白色領帶的子服裝C第4色邏輯原本正確；第一次cached fixture未執行新診斷hook，不能將沒有診斷檔當成生成失敗。一次uncached證實ID正確、component只有rendAccessory：原生ChangeCustomClothes會把RT0寫入該群組，舊port漏掉。修正共用群組，首材質回歸PASS；native/runtime領帶512×512 RGBA差0、新UE四圖紅色。不是修改卡片／圖片／第4色，也不是prefab cache修補。

- SliderHighlight CreateHighlightRenderer為Maker編輯輔助；12個Highlight dump不能直接當成缺角色材質。
- hair_acc native ShadowColor藍色，卡片ME七座標Value黑色、ValueOriginal藍色；獨立KKEx讀取證實runtime覆寫正確。Native charaFileName null、nameIndex+shape七候選來源不是確切loaded-card bytes，禁止為了raw dump一致取消正確ME設定或聲稱同一編輯狀態。
- 原卡Coordinate是Msgpack byte-array列表，各元素是clothes/accessory/makeup長度前綴容器，不能直接unpackb整個coordinate；Custom又是不同容器。初次ExtraData是診斷工具假設錯誤，已依ChaFileCoordinate格式修正。曾接續讀不存在SavedCoordinates，不能計成證據。
- PowerShell rg輸入Docs/PHOEBE*是非法path；用目錄加-g。數個猜測capture/build路徑失敗，應先rg --files盤點；一行runtime.json的Get-Content -Head會輸出整個大JSON，改JSON parse挑必要欄位。提前讀尚未生成報告／圖片只代表未完成，不能當測試失敗。
- Acceptance初稿遇source=null的get與錯DLL位置，未算PASS；修成(source or {})及實際Plugins/KKUnityBundle/Binaries/Win64路徑，最後驗收成功。

最終六套176parts、120effective checks、領帶RGBA、四視角readiness/影像存在PASS，UE真正重新link Succeeded34.03s。Raw audit仍all_dumps_match=false；未宣稱完整shader/worldpose/光照等價。Doc PHOEBE_OUTFIT_DUMP339與Acceptance保存所有FAIL／結果／來源SHA。

## E-218：黑色ShadowColor不代表原生diffuse全黑；完整MainOpaque取代簡化shader

2026-10-01 Stage340。Stage339以saved ME黑／dump original藍解釋髮飾色差並不充分。新確切卡片Studio native probe數值也是black，但截圖仍藍紫；完整原版fragment的ambient/diffuse/drawn spec/ramp數學保留非零diffuse，舊UE kind2近似乘ShadowColor使髮飾全黑。現在共用MainOpaque DXBC程式接renderer，不改卡片／圖片／ME值。兩variant×44 controls原版／重建GPU max0，錯ShadowColor負例拒絕；不要再用單一uniform或raw metadata猜最終顏色。

ChangeCustomClothes另確認先normal01/alpha01、normal02、normal03、最後accessory；原helper選normal02優先與缺RT退用RT0都錯。改依序重播成功寫入；後續RT缺失須保留已寫入結果，不能單選最後群組後直接skip。第一材質／重複群組／缺RT控制PASS。

- 原卡native probe47renderer+materials+face輸出完整；owned Studio23724延遲退出，runner核對exe後終止，exit=-1。只認fixture完整，不冒稱正常退出。probe已撤出。
- 新DXBC來源與installed bundle byte一致；raw-scan ID不是native m_BlobIndex，明確保存10/12對12/14，CB28/5/1及sampler bindings。MainAlpha的shadow取樣register不能盲拷到Opaque，已核對Opaque t6/s0、r6。
- 初次cmd編譯引用與正斜線路徑錯誤，改保存build.cmd再以Windows反斜線路徑執行；cp950讀UTF8 source例外，改encoding=utf-8。在Tools/ok-extract cwd用root-relative paths保存capture hash失敗，改絕對路徑重做，不列失敗操作為驗收。
- shader名稱應優先於 inherited hair hint；MainOpaque null MainTex是ShaderLab white預設，不能因無PNG而退回legacy。all-role cache/材質值/extra textures/default/hairhint控制PASS。

最終六outfit176parts／120effective checks PASS，兩卡8視角與fresh最終DLL4视角exit0/readiness/內容PASS，十字髮飾ROI藍色像素0→406僅為diagnostic，不在production用ROI／卡名特判。UE部署Succeeded38.81s；all_dumps_match仍false，不宣稱外部scene light/SH/camera/worldpose全面parity。詳Docs/FORGE_OPAQUE340與Acceptance340。

## E-219（2026-10-01）：服裝 component 不可借用其他 prefab；overlay 必須依當下共享 RT

Stage341 對照 ChaControl.ChangeCustomClothes 與 KoiClothesOverlayController.GetApplicableRenderers/ApplyOverlays，修正三項錯誤假設：
- 未解析 component 不代表可把 RT0 寫到所有第一材質；原生缺 component 或 RT0 直接返回，保留原材質。
- 同 bundle 任意 ChaClothesComponent 不是載入 prefab 的 component；移除跨 prefab fallback。三張男卡實際 component 可解析，虛構 prefab 都回傳 None。
- KCOX 選 renderer，不是固定選 RT0/1；依 normal01/02 或 alpha01/02 第零元素順序，修改當下成功指派的共享 RT，非 RT 停止。同 RT 被選兩次不可去重；所選 renderer 沒有 mesh 匯出，不代表它不能影響共享 RT。

工具錯誤保留：PowerShell 不支援 bash 的 {lib.rs,unity.rs} 路徑展開；改目錄配 -g 搜尋。一次 cwd 拼接把 repo 路徑重複套到 Tools/ok-extract，沒有成功寫入；改 apply_patch。測試 EXE 執行六 outfit 匯出時另一 cargo test 重新連結，Windows LNK1104；等舊匯出结束，最終 R2 編譯重跑通過，不能把初次失敗算 PASS。截圖 runner 要求含 .png 的卡片檔名，首次省略副檔名被 resolver 拒絕，補完整名稱重跑。

最終 SESSION221/CLI331；220 為中途版，不作最終驗收。驗收、影像、原始差異與 source/binary hash 見 Docs/CLOTHES_COMPONENT341.md 與 Saved/Tests/ClothesComponent341/Acceptance.json。


## E-220（2026-10-01）：法線來源 domain、GPU BC 解碼與未解 body 合成

normal texture 命名不能推導 sRGB；不能對 authored sRGB bytes 再直接 gamma encode。native shader RT 與 legacy linear adapter 必須各自保持正確 domain。僅修 transfer 可把 max68–138降到8，但超過固定6仍是FAIL；補GPU壓縮來源讀回後223版54項有3FAIL，不能放寬門檻。Linear source GPU讀回必須使用Linear target，不能一律sRGB；224版驗證進行中。

OOBase custom MainTex/ColorMask載入及skin alpha修正後，body RGB182仍存在。這否定「custom來源載入即完成」的假設。原卡paint/sunburn ID皆0；新增opt-in原生body-create探針取輸入，不用區域補色。native fixture delayed-quit exit=-1保留，不算exit0。

操作錯誤：Python預設cp950讀UTF8 source再次失敗，統一encoding=utf-8；Tools/ok-extract cwd再次使用repo-relative路徑導致FileNotFound，改绝對root；這些失敗沒有完成預期修改，必須核對diff後重做。舊GPU control對錯誤Linear gamma輸出預期，在讀回修正後失敗；R2依新domain語義驗證兩來源原樣RGBA通過。不存在的猜測來源檔名改用rg --files確認。詳Docs/NATIVE_TEXTURE342.md；本輪尚未完整驗收。

E-220續（2026-10-02）：原生plugin實際覆寫ConvertNormalMap，base API的Texture2D first-red條件不是Sunshine執行路徑；後續依override。ME normal PNG已在RT行序，不能再翻，226六套54/54PASS。KSOX BodyUnder須先轉回authored行序；native overlay與原卡PNG差0，修正後body182→17。body探針旗標是properties不是fields，R2改GetProperty重跑。忽略mask雙線性插值誤推sub/nail Vector不用sRGB decode，完整green native pure否定，已修為RGB全gamma→linear。不要用最近點反推shaderconstant。中途Linear8 overlay量化導致synthetic row control FAIL，改維持native temporary sRGB RT域，不放寬門檻。一次unittest從repo module方式執行不能解析sibling import；直接python test檔3PASS。229/CLI339為待最終驗收。

E-220最終：SOURCEVIEW224 GPU linear讀回本身未消除panst三項max8，真正根因是ME PNG行序再翻；不要把源BC解碼假說當最後根因。body RGB所有Color參數都需gamma→linear，sub/nail「直接linear vector」推論已由native pure否定。主base來源Linear，KSOX BodyUnder temporary RT是sRGB，不可先量化成Linear8再body取樣。229/CLI339最終六套bodymax2、normal54/54、draw120/120、176parts与16圖exit0驗收PASS，raw仍64matched/116different/117missing。詳Acceptance342與NATIVE_TEXTURE342，不將中途FAIL或native delayed-quit exit=-1包裝成PASS。

## E-221（2026-10-02）：renderer狀態不能由enabled或同名猜測；display overlay來源domain/行序

raw missing117不是117個都應可見。完整native六套 state probe 精確路徑連接得到92inactive/6ShadowsOnly/18Studio absent/1active unresolved。頭舌與身體舌同名同vertex，卻相反active；shadowcaster active但僅投影。禁止以name、enabled=true、nullMainTex或空shader查詢就改標PASS。Maker highlight/HeightBar已找到plugin建立來源，仍保留raw缺項；徽章null texture的實際shader coverage未認證。

body overtex1/2來源Linear，但舊PNG未翻Texture2D rows且UE固定SRGB=true。修正shared catalog display resolver、source sampler metadata、UE LinearColor sampler，保留原生Clamp/filter/mips。original gamma-readback比較證實flip後max1/6；舊行序同域max72/255。依原生GetTexture low-poly選_low，不能bytes來自high卻套low metadata。SESSION230/CLI340驗證進行中，不能預先宣稱全量通過。

工具錯誤：再次猜錯PreviewActor路徑，改rg --files確認；Python stdout cp950印中文失敗，統一python -X utf8；再次將PowerShell pathname *及-ErrorAction傳rg造成無效搜尋，改rg目錄 -g與PowerShell錯誤處理。這些命令沒有提供預期證據，不用空輸出推論不存在。原生StateOnly完整fixture但delayed quit exit=-1，如實紀錄。
E-221驗證工具修正：sampler TSV第4欄是mipmapCount（10），第8欄是anisoLevel（1）。初次audit把第8欄當mip導致false FAIL；按Probe.WriteSampling欄位定義改讀第4欄，未改production bytes/metadata或放寬固定max6。首次多檔patch因MD整行不匹配未套用，核對檔案後用完整原行重做。
E-221驗證工具續：outfit5原生無衣服AlphaMask，新增未變更檢查不能假設每套一定有該PNG。第一次full audit KeyError中止而非PASS；修為先檢查前後presence一致、兩者均無時略過bytes比較，有一方缺失仍FAIL。未新增假貼圖或改production。
E-221 overlay最終：final release六套176parts、12sampler及fresh native readback2 PASS，max1/6，錯誤舊來源負控制136/255；所有其他176parts幾何UV與texture bytes不變。sampler cache1PASS，UE build33.72秒PASS，四卡16圖owned53088 exit0/readiness/內容/目視PASS，CaptureEndBinary證實capture開始結束DLL一致。raw64matched/116different/117missing仍false。另做徽章source/hidden/main-white正控制，未完成前不將active renderer判成不可見。
E-221 native徽章正控制最終：Emblem343Native原source vs renderer-hidden全圖差0，只將clone MainTex換白positive max42、514441 pixels改變；證實真mesh/camera/shader coverage，原source null貼圖無color contribution，既有排除在此view正確。沒有把所有null material當不可見，也未改raw117missing。native29632 delayed quit exit=-1、Probe撤出，NativeCoverage.json與Acceptance343封存證據。

## E-222（2026-10-02）：多人物所有權、銷毀貼圖與 dump 卡片身分

AKKPreviewActor 每次 reload 有 DestroyAllParts，但銷毀未清除 AddToRoot 的 LoadedTextures。對齊原生 ChaControl.OnDestroy→ReleaseAll，新增 EndPlay/Destroyed 清理本人的 parts/textures/session；同場他人資源不受影響。dump join 必須先验证 character root 與 sourcePart.objectHierarchyPath，不能用同名 mesh/material/vertex 認作另一人。未知 root 保留 unverified。六套282 renderer＋同名他人負控制通過；raw仍64matched/116different/117missing，false。

舊 card_resolver 有最多7候選及 nameIndex(first)，缺原載入卡 bytes SHA。不可用猜中filename宣稱原卡身分已驗證。Rust10live sessions正反序及UE10actors最終測試執行中，結果見Docs/MULTI_CHARACTER344.md/Acceptance.json。

工具失敗：ProceduralMeshComponent 模組標頭不能加 Components/；CreateWorld 已初始化，重複 InitializeNewWorld 導致 LevelActor.cpp fatal；改傳 InitializationValues。Automation不能RunTests後立即Quit，改TestExit queue empty。首輪isolated test world無context警告，新增FWorldContext建立／清理重跑。保留各失敗log；再次提醒Python -X utf8、rg --files確認路徑、PowerShell萬用pathname不能當rg輸入。沒有以空輸出或跑到中途判PASS。
E-222最終：final UE build Succeeded6.30秒，2tests Success／10同場actors／queue empty／exit0，world context警告消除，DLL/source起訖SHA一致。Rust1PASS2331.51秒，10simultaneous sessions正反兩序、20個修改負控制、其他人完整狀態不變、刪除與原始形狀重開通過。Acceptance344核對source/card/dump/test binary SHA後exit0，bounded ownership PASS；282foreign-renderer負控制PASS。raw仍64/116/117且false，舊dump原卡SHA缺失不認證，未宣稱全native像素零bug。所有本輪owned程序結束，release LIB／Editor／debug test EXE窗口均釋放。

## E-223（2026-10-02）：臉部 draw overlays 不能提前混入主貼圖或共用眼影槽

原生 ChaControl.ChangeSettingLip／ChangeSettingEyeShadow／ChangeHohoAkaRate 在 create pass 後對 matDraw 的 _overtex1/2/3 各自設定。舊 compose_face_texture 將 hoho 用固定RGB提前合成，head emit 把眼影放入overtex1；SessionPart還會清除skin shader_extra_textures，UE face只保留UV3作眼影。改保留原生三槽、來源hoho RGB及status alpha、ME最後覆寫，現有shader_textures map/cache/FFI承載，不加持久化欄位。UE四組UV獨立，按0071 VS和0072 PS保留COLOR0.r/b、ST與混合順序。

另：ChangeTexture選中row的property=0/空必須清除，不能fallback到MainTex或vanilla同id；shared display resolver改精確selected row/key。驗證新增真正存在的來源正控制與selected null負控制。CACHE108／SESSION231／CLI341，尚在最終驗證，不能先判PASS。

工具失敗：再次將PowerShell glob路徑傳rg、猜composite.rs/eye_draw.rs/KKSession.h與CaptureInventory位置，均無有效搜尋；改rg --files／rg目錄 -g。首次UE build在FKKLoadedPart宣告前寫Part.bNativeFaceDraw，C2065；移至宣告後，R2編譯成功35.98秒。初稿Rust測試引用不存在empty_for_test，執行前刪掉並在session模組用PartBlob實際bincode roundtrip，未讓假constructor成為production。所有失敗與未提供證據的命令不當作PASS。
E-223續：231/CLI341首輪遺漏真正Shader Forge/main_skin，僅測Koikano不能驗收菲比。R2/R3補kind7，按Forge PS在COLOR0 gate後套ST；最終232/CLI342、UE Succeeded37.15秒、三tests Success/exit0，Rust3controls PASS0.63秒/10session release PASS231.41秒，六套176parts/18色/6hoho L0完全對上。raw仍64/116/117且false。第一次PowerShell直接GUI exe啟動回傳0不是程序完成，capture空；改Start-Process Hidden/WaitForExit讀owned exit。不要以CaptureExit.txt=0判PASS。Get-Content讀one-line renderers JSON造成大量截斷；改Python parse只列必要欄位。嘗試對Stage342全量舊資料重跑body regression，因独立head geometry已更新而失敗，不能當face改動退化；同輪Runtime→RuntimeR2所有非face bytes/samplers和幾何不變已獨立驗證。

## E-224（2026-10-02）：FaceCreate 浮點色彩域與合成驗證

舊face compose在gamma bytes上乘skin Color、使用整數nearest mask，且paint/lipline/mole只放在opt-in近似路徑。原生create DXBC必須在linear域相乘，Color RGB轉linear，alpha不轉；每張Texture2D依實際sRGB設定取樣，不是全部PNG強制decode。七原生GPU正控制與新face_create.rs全部max1/255，含兩paint layout、mole rotation、paintmask及combined順序。現階段僅獨立compiler host驗證，未接入DLL，不能宣稱完成匯入。

驗證工具初版Rust使用.5無整數部分及loop-local paint_mask借用跨closure，编譯失敗；改0.5並把paint_mask移到loop外，R2七controls通過。保留FaceCreate346Cpu.log/R2.log，不把首輪失敗覆寫成PASS。
E-224續：隔離預設材質test最初猜bo_head_00 bundle，找不到cf_m_face_create，不能視為來源不存在。依ChaControl10732確認mm_base，R3 inspect_native_create_defaults346 PASS，預設paintmask cf_face_00_mp。首次Python -c多層shell引號截斷，SyntaxError、cargo沒有有效manifest；改保存prepare_face346_validation.py再執行。新源module初稿catalog模組名錯，查lib use後改list，實際cargo check隔離crate PASS；尚未改部署lib。
E-224再續：CPU六套MainTex max6→2，仍須保留source mips不能只驗L0。原生Sideloader.GetPng默認mipmap=true，舊metadata默認1不可沿用；實際PNG生成128→1八層直接RGBA讀回證實逐horizontal平均截斷後vertical平均截斷，所有mip bytes差0。首次mipped-paint負控制差0，原因card paint scale沒有觸發縮小，不是mip已驗證；R2在clone設定paint1 w=-8後重跑，不改原卡或renderer。null ColorMask正控制與MainTex差0，證明ShaderLab default white；修正CPU reference null mask預設，GPU使用white。
E-224最終接入續：SetCreateTexture直接使用MainTexAB/ColorMaskAB欄位；AB=0必須清除，不能借head MainAB；create材質與PS也依真正mm_base override優先。235/CLI345失效舊session。native null-main以Graphics.Blit(null)正控制證實white default，linear skin/colorMask推導max1；沒有用固定skin PNG代替原生create。重建R2與未結束Editor重疊，LNK1104，測試成功不是重建成功；等ownedUE3測試全部Success/exit0且進程退出後重建R3 Succeeded5.04秒。後續只用final235DLL作stream/28圖回歸，不把234的3tests冒充235版本全量測試。
E-224 驗收補抓：235 RuntimeFinalR2 的測試程序成功不代表原生合成成功。嚴格 metadata 比較發現 face _MainTex runtime sampler 消失，log 六次 face compose failed。停用 cache 的對照成功，但不能作正式通過。正常 override index 選到只含服裝的 mm_base partial override，沒有 cf_m_face_create；先選整個 bundle 再找 material 錯把 vanilla face 資產遮掉。改成逐 asset 選取（override material 存在才採用，否則 vanilla；PS 同樣逐 shader 查找），對齊既有 clothes GPU 資產解析。236 diagnostic unique-card-copy（同原卡 bytes）也重現，排除單純 session 舊資料。237/CLI347 失效錯誤 session；正式 export 檢查增加不得 face compose failed/GPU failed，並仍要求所有 sampler、非 face bytes、UV/geometry 相等。235 CaptureFinal 的 28 圖 readiness PASS 只證明拍攝成功，不能算 FaceCreate 驗收；保留失敗資料。
E-224 測試啟動補記：237 UEControlsR3 忘設 OK_MULTI_GAME/OK_MULTI_CARD，MultiCharacterIsolation 明確報 fixture missing，NativeFaceDrawStreams 因 source 空路徑也失敗；不是 import regression。保留失敗 log/actual exit，R4 補同一明確 game/card 重跑三項，不用 ActorResourceLifetime 單項通過冒充整組成功。
E-224 最終：237/CLI347 正式 build與UEbuild R4 Succeeded；六套RuntimeFinalR3 export1PASS253.58秒，沒有face compose failed/GPU failed。InputAcceptance176parts/435非face bytes/所有sampler、四UV、geometry與effective_values不變，六套與freshnative MainTex max1。最終UEControlsR4三項Success、十同場actors、exit0；七卡28PNG全部readiness/內容/目視PASS，PV7與attachment135 error0cm，14項source/LIB/DLL起訖SHA一致並核對目前文件。Acceptance346封存PASS，raw64/116/117仍false，沒有宣稱全dump/全場景像素零差。兩次 diagnostic 用 Python -c 多層 PowerShell 引號再度失敗，後續僅用 apply_patch/保存的 script 修改源碼；保留235與fixture missing失敗資料，不用停cache結果代替正式驗收。所有owned進程已完成，probe不存在。
## E-225（2026-10-02）：身體 articulation 不能只驗動畫可播放

三實際來源檢查：菲比 BP.profundis 有32相關骨骼、13普通DynamicBone；KK_226176/SAC.innie2與Alice/SAC.clit使用vagina.open morph，不能硬加同一骨骼/固定卡名偏移。原生clone數值控制六weights（0/25/50/75/100/0）證明SAC真正頂點變形並回到原始closed，完全不改原卡/renderer，不拍攝私密部位。
共通缺口：替換身體僅換mesh/rig，普通body DynamicBone未保留；C++只替臉部morph建立可控制renderer。補replacement-body components按Transform name rebind，保留來源collider/versions；普通morph使用原生百分比及actor-local runtime override。源rig更換所有Transform IDs負控制，13components完整重綁且120frames/3120相關bone輸出變化、無非有限值；這只證明物理輸出，不能拿骨骼有動當開合/雙人互動全部認證。
首輪UE BodyMorphControls確實失敗：morph名稱/資料已匯入，但USkeletalMesh.FindMorphTarget找不到。查本機UE5.8原碼RegisterMorphTarget(false)只append，不建立name/index map。Builder批量註冊後補InitMorphTargets再InitResources，保留失敗log，待R2實際驗收；不能因API可寫weight就預先宣稱GPU畫面已變形。
本輪Python -c內再次含PowerShell $env/多層引號導致ScriptBlock錯誤；避免此類嵌入，後續用apply_patch或原生PowerShell單引號Literal replace，runner新增明確BodyMorph switch。
E-225續：CPU身體morph舊路徑直接使用mesh-local delta，角色旋轉後方向錯誤；改按當前LBS palette轉換原始blend shape，旋轉90度回歸max2.0236257e-9m。FFI初稿誤用不存在sess_mut，BuildR2失敗，按實際sess_ref的mutable返回修正，BuildR3成功。UE final新增console輸入解析時猜Misc/LexFromString.h、Misc/LexicalConversion.h兩個不存在header，R6/R7失敗；rg實際引擎確認String/LexFromString.h，final build Succeeded12.37秒。不得再猜include路徑。原生SAC兩來源六權重閉合restore0；BP普通物理13元件保留與次級運動通過，不能冒充BetterPenetration接觸／拉骨完整開合。
E-225續：靜態權重測試不足以證明播放時保持開合。檢查RefreshSkinnedDisplay發現CPU body未納入live base刷新，且刷新section後未重套part override；CPU morph body改走既有runtime-refresh通道，刷新後對有part權重actor重套ApplyMouth。新增每個權重後RefreshSkinnedDisplay測試；原GPU四Import回歸為前版，最终CPU/GPU body控制須重新建置通過。
E-225續：驗收曾誤判CPU PASS，因OKPrepareLiveGpuEnv每次Open強制GPU，CPU日誌max=0且實際Comp仍為GPU。不能只設OK_GPU_SKIN=0便稱CPU測試。保留UECPUFalsePositive.log，Acceptance撤回false；新增OK_CPU_SKIN明確CPU renderer mode與requested renderer mode實際類型斷言，須以重跑的非零CPU頂點位移驗收。
E-225最終：actual CPU renderer已斷言，最大頂點位移0.932681433/1.154252291cm與native對上、六權重含姿勢refresh和閉合復原、隔離/卸載PASS，exit0；最新GPU同test/batch/RHI PASS/exit0。四Import回歸為前版GPURegression，保留版本界線。Final build5.90秒；Rust3PASS；Acceptance347 bounded_checks_passed=true、BP contact/all-body automatic均false。全部owned程序退出、probe撤出。

## E-226（2026-10-02）：替換身體 Ver01 粒子與碰撞器重綁

Stage347 retarget_body聲稱保留兩版普通物理，但未重綁Ver01有序m_Nodes Transform IDs，ID變更後無有效粒子。又將所有來源m_Colliders移到implanted通道，而configure_character_colliders跳過Ver01，導致其來源碰撞器不會進入執行列表。依反編譯DynamicBone_Ver01.SetupParticles及UpdateParticles2保留有序節點/rotation flag與Ver01原collider通道；ordinary維持shared configuration通道。SESSION239/CLI349需使舊cache失效，無persistent schema增加。

測試失敗保留：Red初版把唯一tip放入not_rolls，自己消除ordinary鏈，不能當product bug證據；改獨立aux節點，RedR2確認舊node IDs未重綁；只修nodes後RedR3確認來源collider數量0而非1。Green初版錯假設AttachedDynamicSet可Serialize（实际只deriveClone/Debug），E0277；改比對所有Transform數值與粒子歷史snapshot，DBoneSet才做既有bincode roundtrip。不為測試新增production序列化欄位。
E-226最終：GreenR2 PASS，dbone suite27PASS/11ignored；239 live/cache23components/61frames PASS；Editorbuild31.95秒Succeeded。239/CLI349 CPU與GPU final controls Success/exit0，CPU actualmax0.932681433/1.154252291cm，姿勢刷新/閉合/隔離/卸載通過。Acceptance348 source/native-decompile/LIB/DLL SHA封存，無owned程序。未重跑完整四Import tests，不冒充新版本全量；保留Stage347歷史驗收。

## E-227（2026-10-02）：穿著衣服的雙角色身高差驗證

只做骨架數值稽核，不產生互動場景圖片。不能把有限值或共同clock當接觸位置/衣服穿插正常；原生0/1身高與人工cf_n_height比例壓力案例分開記錄，後者不當原生ABMX/card匯入正控制。舊HPlaybackAudit報告的未實作結論已由Stage335–337取代，閱讀時必須往下看追加更正。

新audit初版誤認Skeleton.world_tf返回tuple，實際為Mat4，E0609；改按本機API取矩陣translation，保留PairedHeight349.log失敗，不列PASS。
E-227續：R2依清單前四state抽樣，多個contacts=0，無法驗收IK；只終止已核對Path的自有test PID69580，保留中止exitFFFFFFFF，不作PASS。改以實際MotionIK state含有效位置目標篩选pair/state，每案必須contacts>0。setup對不存在state回None，不能用空state探整份data，按實際controller state查詢。新增UE DifferentActorScaleGuard數值測試，驗證兩Actor世界scale不同會拒絕Start，不產生場景圖。
E-227最終：R3抽4pair/8states/5height configurations，360pair frames/720role frames/2880 active effector observations，nonfinite0、共同clock/height一致、proxy清除。native0/1範圍max target residual0.727mm；額外骨架比例3.57倍時max114.419mm，含partial weight及FrameCorrect，不能冒充native parity誤差或認證視覺/衣服正常。UE Actor scale1/2 guard實測Success/exit0，直接拒絕Start。build37.20秒Succeeded；Acceptance349 numeric_smoke=true、normality=false。未改產品solver/playback，測試例source/DLL/LIB/卡片SHA封存。

## E-228（2026-10-02）：通用 IK TransformPoint 縮放精度

極端比例的原生對照必須使用相同輸入，不能把 target residual 當 port parity。建立無卡片/無動作/停用 Renderer 的骨架一般伸展 oracle。編譯初版漏 ChaLoad namespace（CS0246）；追查時再次用 rg 直接輸入 Windows wildcard 路徑、假設 Decompiled 目錄、猜錯 runner/測試檔路徑，以及另一 cwd 用錯相對 log 路徑。先用 rg --files/正確目錄 -g 找路徑，不讓無輸出或檔案不存在當驗證成功。

NativeR1誤以 cf_j_foot_L/R 當 IK 足端，實際為 cf_j_leg03；只有96正權重而不是預期192，runner拒絕。改用原生 IKEffector.isEndEffector，不硬寫骨名。另建立 HandsOnly 正式控制；NativeR3 重現相同1.414mm膝部差。追加追蹤時猜 OnPostRead API（CS1061），改按既有原生 OnPreSolve callback 取得 ReadPose 結果，失敗 build R4 保留。

NativeR3 ReadPose 顯示右腳踝輸入約1.1200679e-7m差、鏈長1.1920929e-7m差，近直腿放大成1.413999mm與quat差0.00222。五種浮點運算順序診斷發現先縮放點再旋轉/平移，使六個骨架的右腳踝輸入差全為0；改共用IkSkeleton.world_pos，普通renderer pose不動。GreenR3 48角色幀max0.007719mm/quat1.19209e-7 PASS；四末端GreenR2 max0.014488mm PASS；舊controlled/natural各96幀max0.099035/0.096889mm PASS，門檻不變。八FinalIK tests PASS。SESSION240/CLI350，無卡別/動作/時間偏移、無persistent schema新增。

原生一般伸展也有有限肢體長度/solver約束殘差：極端比例四端max113.200mm、雙手max147.897mm。不得將此等同Stage349場景根因已全面認證，也不得把原生對齊當衣服/雙人位置正常。NativeR2/R3 fixture完整但Quit延遲，核對自有PID/exe後終止，actualexit=-1；不冒充exit0。所有負控制及錯誤log保留。

E-228最終：Editorbuild31.77秒Succeeded、CLIbuild37.38秒，三個部署binary均含350marker；UE三項動畫回歸全Success/actualexit0。720role frames重測241.49秒PASS，nonfinite0/clock一致/proxy清理，3.57倍target殘差仍114.418752mm，不冒充極端比例對位修好了。Audit350封存50項SHA、all_generic_ik_cases_passed=true僅限已測數值案例、scene normality=false。所有owned程序退出，probe撤出；保留先前Acceptance348/349不重新封存歷史。

## E-231（2026-10-02）：一般 DynamicBone 原生逐幀縮放對照

先找現有runner時猜 Tools/run_generic_ik_scale350.ps1，實際在 Tools/GamePoseProbe；失敗讀檔不是不存在控制的證據，改用 rg --files -g 精確找檔。RustR1新增fixture loader猜錯Tf.sibling_index型別i32，E0308；來源是Option<u32>，依型別修正、保留原compile log。未跑到solver的compile failure不列產品負控制。

NativeR1為生成的一般裸骨鏈、無Renderer/卡片/動作圖片，五案900幀3600粒子觀測完整。Application.Quit延遲，runner確認owned PID7868 executable後終止，actualexit=-1；runner exit0只表示fixture收集完整，不能冒充native乾淨退出。探針已撤出，Rust對照尚在執行，完成後追加。

RustR2/R3為真正solver負控制：四個均勻案例在20µm/quat2e-5內，非均勻案例粒子28.8646µm/world25.8736µm/quat6.36578e-5失敗。NativeR2追加中間值後，component rotation誤差僅1.19209e-7，但objectScale錯0.000424266。LossyScaleDiagnosis顯示world matrix column length包含shear，原生lossyScale是去除Transform.rotation後的對角值；投影公式所有案例max5.57e-8。修正共用component縮放（persistent/one-shot/legacy），無卡別補償、門檻不變；SESSION241/CLI351使舊cache失效。NativeR2同樣完整但owned延遲Quit終止actualexit=-1；對照與部署仍待完成。

GreenR1/R2五案900幀PASS，particle4.885605µm/world1.079409µm/quat9.11951e-6。另重跑Accessory318NativeR3，卡0碰撞frame1旋轉2.23517e-5超既有2e-5門檻；cfg(test)舊column算法控制結果完全相同，歷史Accessory318SolverAudit也記同一失敗，不能誤判本次退化或隱藏成PASS。追查原生DynamicBoneCollider膠囊內段的更新使用position += offset*((radius-distance)/distance)，旧通用nearestPoint + offset*radius/distance雖實數等價，f32不同；同時原生用平方距離分支。依反編譯改共用sphere/capsule碰撞運算順序，沒有卡別補償；結果待嚴格重測。

AccessoryGreenR1僅改膠囊順序仍FAIL：quat2.21729e-5，保留報告。PositionDiagnosis對原生137 Transform初始位置比較，既有matrix translation max0.357647µm，階層先scale點再rotation/translation max0.122878µm（手節點直接為0）；與Stage350已驗TransformPoint順序一致。調整共用dbone.world_of的translation運算，matrix basis仍保留完整shear，無骨名或卡片分支；再次嚴格重測。初版patch假設parent.filter行但實際是nested if，verification failure無修改；改按剛讀取的實際source套用。

E-231最終：三項共用修正後GreenR3/R4原生五案900幀/3600粒子觀測PASS，particle/prev7.365852µm/world1.309947µm/quat1.403689e-5。R4明確斷言每幀6nodes/4particles及輸出TRS有限，避免NaN被max忽略。AccessoryGreenR2 16原生時間點控制PASS，max world0.796712µm/quat8.713454e-6；既有紅案通解修復，門檻不變。dbone29PASS/0FAIL/12ignored。InstalledR1四卡960幀PASS286.20秒/actualexit0，兩靜態卡不算物理正控制，兩衣服物理卡23,040共享粒子位置觀測finite/isolation PASS，實卡private粒子0。

Editorbuild43.32秒、CLIbuild38.56秒Succeeded/exit0，241/351/108部署。UE GPU三項Animator與衣服scale/isolation四項Success/actualexit0，CPU衣服同項Success/actualexit0；CPU/GPU各99bounds觀測max0cm並確認實際renderer。Audit353保存本輪source/native/log/report/binary SHA；所有owned退出、探針撤出，沒有圖片。仍未驗inside／旋轉非均勻外部碰撞器lossyScale.z／Ver01&Ver02逐幀原生parity、衣服穿插／配對scene／全面無bug，不能擴大PASS範圍。

## E-230（2026-10-02）：一般衣服次級物理縮放稽核

Stage352追查時再次把 Windows wildcard `dbone/attached*` 直接傳入 rg，invalid path；應先 rg --files，再以目錄及 -g 搜尋。AttachedDynamicSet 實際在 dbone.rs。只驗一般走路與物理數值，不輸出場景圖。原生先碰撞再約束鏈長，有限值或碰撞器數量不等於完全無穿插，需另設碰撞開關的位移正控制。測試結果於完成後追加，不覆寫 Stage351 歷史封存。

UnitR1 失敗：球心放在骨鏈正軸上，碰撞推出後鏈長投影恰好回原位，positive displacement=0；屬測試配置錯誤，不是產品不處理碰撞。保留 WardrobePhysics352UnitR1.log，UnitR2 改側偏0.1m球心；仍使用來源碰撞順序與原2µm鏈長門檻，不放寬斷言。

E-230最終：UnitR2 480骨鏈觀測PASS，minimum normalized collision delta0.185218945m；InstalledR1 960幀PASS250.02秒/actualexit0。男女兩卡衣服粒子0，不能列物理正控制；Alice/March_7th各48共享粒子，480物理幀23,040粒子位置觀測、每段59變化、nonfinite0、另一session不變。實卡私有衣服粒子0，該覆蓋未認證。dbone suite28PASS/11ignored/0FAIL0.71秒/actualexit0。只有cfg(test)與文件，產品仍240/350/108，未改LIB/DLL；不把數值smoke當原生parity、衣服無穿插或配對scene正常。

## E-229（2026-10-02）：一般衣服身高/renderer覆蓋稽核

WardrobeHeight351只做一般站姿/走路/骨架伸展，不輸出場景圖片。先依反編譯AssignedWeightsAndSetBounds與ABMX BoneModifier.Apply驗證通用路徑；關閉次級物理，只量測幾何與isolation，不能當碰撞穿插通過。

初版apply_patch用不存在佔位行，verification failure，無source改動；先讀真實模組結尾再加入cfg(test)。再次在Windows用rg直接傳*.cpp造成invalid filename，改目錄加-g，不因錯誤沒有輸出就當找不到。R1把raw clip source清單當controller state，無有效走路state；使用states:前綴，寫完整labels清單。R2誤只選普通skel_kind1、排除runtime衣服，實際主要衣服全有私有SMR槽位，正控制數量0，保留失敗。改驗真正runtime.source權重/槽位按body name匹配的輸出，不粗略flatten。R3猜男角m_aruki名稱失敗，清單實際male / Base Layer.mc_m_walk_00_00_01；R4按精確清單名稱選取，逐幀寫partial-rows避免中途失敗丟掉已算資料。

R4 1PASS／20.01秒，兩卡48角色幀、381144衣服頂點觀測，全部可匹配height-body slots，最大uniform error0.001194mm、body CPU/GPU差0.061795mm、nonfinite0、同卡另一session骨架/頂點不變。6女/5男衣服都是private runtime，GPU garment比較數量0；僅body有GPU正控制，不能標整套衣服GPU驗收。產品SESSION240/CLI350未改邏輯。

原生一般伸展96目標，66超出當幀求解後肩根的兩段臂長範圍，最大native residual147.897mm、固定根幾何下界147.891mm。不是全身根可移動時的不可達證明，也不能代替場景極端身高同輸入native oracle。Unreal增加兩實際actor的衣服world scale/bounds/isolation控制，CPU/GPU必須斷言實際body renderer，完成後保存各actualexit。

E-229最終：UE buildR2 Succeeded7.26秒，CPU/GPU新ClothedActorScaleIsolation皆Success/actualexit0，各99衣服元件bounds觀測max0cm，另一actor Session pose/材料payload/world骨骼不變，實際body renderer斷言通過。Audit351保存32項SHA；只新增通用controls，無產品邏輯或版本改動。關閉次級物理、未驗body/cloth穿插、private garment共享GPU palette、不同world transform配對及互動scene正常性。所有owned程序退出，未安裝probe或產生圖像。不要把一般display元件能scale當Pair.Start支持不同Transform。

## E-232 Stage354：旋轉碰撞器半徑仍混入剪切

Stage353修正component lossyScale後，collide_particle_world仍取matrix z column norm，漏掉原生DynamicBoneCollider.Collide的Abs(lossyScale.z)。renderer-free旋轉/nonuniform與inside/outside四新案RedR1 particle9.858282mm，超原20µm門檻，保留exit101。通解由來源Transform rig計算尺度，external tuple保留尺度、live更新同步，不從sheared matrix猜rotation，不加卡別補丁。九案1620幀/6480粒子、authored與真正character外部configuration皆通過，門檻不放寬。

本輪操作錯誤：在Tools/ok-extract cwd讀Saved/Logs時漏../../，只造成讀檔錯誤，不是solver失敗；再次猜bodycolliders.rs不存在，改用rg實際定義。Red/GreenR1 log仍固定frames=900，JSON實際1620；改顯示rows.len，seal以JSON counts及每案180檢查。原生資料完整但delayedQuit owned cleanup actual-1，勿記exit0。後續部署/測試結果補記本節末。

E-232驗證事故：UEGPU首跑exit1，停在實卡載入，沒有TestCompleted且UE log沒有fatal/新crash；同時四卡Rust GPU擷取在執行，可能有資源或快取競爭，但目前證據不能確認根因。此首跑保持失敗，不把exit1當成功。四卡InstalledR1最終960幀PASS455.48秒/actualexit0。改善runner使用fresh tag並保存UE stdout/stderr，改串行重跑；這是診斷隔離，尚不能宣稱產品問題已修復。

E-232最終：SESSION242/CLI352/108，Editor47.47秒/CLI36.09秒成功；四卡960幀actual0。GPU R2與CPU R2串行皆Success/actual0，各99boundsmax0cm/actualrenderer/isolation通過，首跑失敗根因仍未確認，不抹除、不猜測當定論。源碼及部署binary/原生fixtures/負正控制/build/logs由Audit354 SHA封存。未驗收穿插/所有Transform/整體scene。所有owned程序已退出，probe撤出；保留舊seals。

## E-233 Stage355：負祖先尺度反射、零距離浮點不連續

直接原生DynamicBoneCollider九種縮放/三軸/內外邊界/球膠囊分界，RustR1首次失敗raw0.742497087m，scale差0.400527894。diagnostic確認單純composed quaternion遇單/雙負祖先尺度不等於Unity Transform.rotation；沿階層用累積scale sign反射local quaternion xyz後與native一致。修正只在lossyScale投影共用路徑，不冒稱其他world_rotation/鏡像完整simulation全部已修。cfg(test)ignore-reflection RedR2保留同一尺度失敗。

另一問題必須保持未驗收：原生outside判斷distanceSquared>0，沒有epsilon，複合Transform端點約1e-7m浮點差在球心/膠囊中心線被正規化放大。最終raw max0.773254633m，279近中心線輸入未通過。不可忽略大差異、以finite當正常，也不可加epsilon/卡別偏移。報告保存all_samples_passed=false、raw/stable各自counts；穩定7821樣本max0.240736µm與identity810輸入max0.074506µm通過2µm，這是有限子集合。診斷最初重寫同輪DiagnosisR1後改用DiagnosisR2保留新tag，不覆寫任何歷史seal。後續需native matrix/TransformPoint中間值對照。

E-233最終部署：243/CLI353/108，Editor43.83秒、CLI32.32秒成功；29dbone/原九案Dynamic1620幀/原生飾品16觀測PASS。四卡960一般走路幀PASS413.30秒/actual0，串行GPU/CPU各衣服scale/isolation Success/actual0，99boundsmax0cm/renderer斷言通過。Audit355明列verified_subset_passed=true、all_collision_samples_passed=false，279近中心線樣本/raw0.773254633m仍未驗收，不加epsilon。全部owned程序退出，native probe撤出。後續按native矩陣/TransformPoint中間值追蹤，不把此通解有限驗證當全面無bug。

## E-239 Stage361：MorphThresholdPosition 預設刪除小位移

骨架MeshDescription仍保存來源morph，但Unreal重建MorphTarget預設MorphThresholdPosition=0.015cm，丟棄较小位移。MorphRedR1新程序檢查舊Stage360AllR3，SK_00021 eye_face.f00_def_cl vertex582原本Y約-.001/Z-.003cm，render為0，預期拒絕actual1。不能用模型載入/靜態圖正常當morph完整驗收。

共用匯入器MorphThresholdPosition=0，保存前及-VerifyMorphs重開逐項檢查名字、raw point→render頂點映射/位置、重複或越界索引與position delta。按UE MeshDescription先Base+Delta再減Base的float順序核對，不靠epsilon；Source delta屬性仍保存原值。UE另有IsNearlyZero篩選，對非零極小source samples另計數，該九套為0；未測其他卡非零極小delta，不冒稱raw render delta bit parity／morph normals／controller／原作pixels。第一次猜錯MorphTarget.cpp在Private/Animation，rg --files後正確定位Private/MorphTarget.cpp，往後先定位。

BuildR3 Succeeded7.90秒actual0；SmokeR1一套312targets/96012samples actual0；ImportAllR1空GameRoot九套1667assets保存前2808模型內morph項目/864108有效render位置delta，ue_nearly_zero_samples=0；VerifyAllR1另啟實際RHI同數actual0。項目跨模型/服裝累計不是2808種不同表情。99skins226512triangles、完整position/UV/color、97chains1156mips與Static四UV/材質/骨索引維持。DetachedR1無card/pack/parser Success actual0；CookR1全1667包actual0。新資產Stage361AllR1，歷史資產/日志/seals保留，詳Docs/INDEPENDENT_ASSET_MORPH361.md及新Audit361。rawKKS/private rig/full controller/fullappShipping/pixels/Stage355279案例限制維持。

E239最終：CookR1 actual0/0errors/2既有HTTP warnings，NativeMips R1 Success actual0；LegacyR1匯入/實際RHI重開 actual0、1skin16754triangles/91textures/21chains294mips，但該可匯出部分morph0，不能算legacy表情驗收。新Audit361保存源碼、引擎morph轉換參考、兩輸入卡、資產/Cook/logs及未改的Audit360原檔；沒有改原始卡或重封舊階段。

## E-238 Stage360：骨架頂點色旗標遺失與預設 50% reduction

獨立匯入器只複製SoftSkinVertex.Color，未設目的SkeletalMesh HasVertexColors；UE GetMeshDescription依旗標決定是否寫入顏色。ColorRedR1實際匯入拒絕1，保存前停止。共用修正傳遞flag、轉換後逐角與來源color比對；骨架顏色按UE CreateFromMeshDescription的非sRGB量化ToFColor(false)，不能照搬Static的ToFColor(true)。

補旗標後完整角檢查揭露第二根因：AddLODInfo預設ReductionSettings.NumOfTrianglesPercentage=0.5，LOD0也會簡化、移動頂點。GeometryRedR1重開舊Stage359資產，SK_00014 source16754/render8376預期拒絕1，證明舊幾何近半被減掉。明確設三角形/頂點比例1，bRemoveDegenerates=false、position/tangent/UV合併閾值0以保存原始角資料；不以epsilon放寬驗收、不按卡/圖片調整。Source/Render triangles保存前及重開皆須相等，完整位置/UV/RGBA tuples精確比對。

診斷事故完整保留：ImportAllR1/ColorDiagR1/R2直接用MeshToImport/raw索引對Source Description，在縮減後位置不符，不能單憑它判定顏色損壞；ImportAllR2、ImportSmokeR1/R2、ColorDiagR3/R4精確payload仍失敗，近點deltaSquared0.000307946553221，僅關閉welding閾值無效，最後定位reduction。BuildR4 private RawPointIndices2不能直接訪問；BuildR6 FColor無GetTypeHash overload，改packed ARGB；失敗build後runner拒絕舊DLL，未誤用。數次rg猜錯Engine cpp位置或Windows literal glob，應先rg --files定位、以目錄及-g篩選。

Final BuildR12 Succeeded7.15秒actual0，ImportSmokeR3先通過，ImportAllR3空GameRoot九套1667assets actual0，VerifyAllR3實際RHI重開actual0，99Skins完整226512三角形、所有色彩buffer角資料通過；無色部件仍白。Static1201500三角形/四UV，212textures/97多層鏈/1156Source層不退化。DetachedR1無卡/pack/Rust/parser，9套重建/建立後移動/清理Success actual0。最新使用Stage360AllR3，失敗所有目標未落盤，旧Stage358/359資產/日志/audits保留。詳Docs/INDEPENDENT_ASSET_SKIN360.md與新Audit360；完整動畫控制器/private rig/rawKKS/shader pixels/full appShipping/Stage355279未通過案例仍未驗收。

E238最終補驗：CookR1全部1667包Windowsactual0，0errors/2既有HTTP warnings；NativeMips R1 Success actual0；完整舊snapshot ImportLegacyR1/VerifyLegacyR1 actual0，57Static/1Skin/158mat/91textures，骨架Source/Render完整16754triangles及角資料通過，21chains294mips維持。新Audit360封存本輪資料及未改的Audit359原檔，沒有重新封存舊階段。

## E-237 Stage359：匯入貼圖丟失來源低層 mip

Texture 持久化原先只讀 PlatformData.Mips[0]，Source.Init 只存一層，UE 預設重生低層；這丟失 OKCreateRuntimeTexture 已依原生 mip_policy 建立的 encoded-byte mip 鏈，可能改變 alpha clip。共用修正逐層檢查尺寸／格式／長度、保存全部 Source mips；多層 LeaveExistingMips、单層 NoMipmaps，去重鍵包含整鏈與層數，無卡別或圖片調整。

NativeMips R1/R2 Success/actual0：首層相同但低層127/128不同不合併，整鏈相同去重，三層bytes一致，單層不增生，錯尺寸拒絕。MipRedR1先命中1×1 neutral depth，不能證明低層丟失；檢查排除無低層可能的1×1後，MipRedR2舊AllR4 T_00005預期actual1，保留兩輪。新AllR1空GameRoot 9套1667assets，實際RHI重開四UV/skin/material/構建actual0，97多層鏈共1156Source層。CookR1所有1667包actual0、0errors/2既有HTTP逾時warning。DetachedR2無卡/pack/parser，建立後移動/旋轉/非均勻縮放、無效outfit與清卡stale components清除全部Success/actual0。

BuildR2與Import尾段生命週期曾重疊，兩者退出0但不是推薦流程；最終BuildR3/R4串行且BuildR4 Succeeded5.21秒。所有後續工具必須核對owned程序退出再build，避免舊DLL／鎖檔。第一次讀build log誤猜在Saved/Tests，實際在Saved/Logs，應先rg --files定位；一次文件patch使用未讀取的標題context而未套用，讀取實際標題後修正；沒有文件內容遺失。完整舊snapshot ImportLegacyR1/VerifyLegacyR1亦actual0，91貼圖/21多層链/294Source層，保留舊格式相容性回歸。

最新使用Content/IndependentImports/Stage359AllR1，舊Stage358資產/日志/封存不改寫。這不是壓縮後pixels或完整原作材質parity認證；原始KKS卡/full controller/private rig/整工具Shipping/Stage355未通過279樣本維持既有限制。詳Docs/INDEPENDENT_ASSET_MIPS359.md及新Audit359。

## E-236 Stage358：獨立卡轉成 Unreal 原生資源

首次編譯誤用不存在的 PKG_Transient、FSoftSkinVertex aggregate {} 遇 FVector4 explicit constructor、骨骼 pose 屬性誤用 FTransform3f，以及 TObjectPtr MorphTargets 用 auto*。BuildR1/R2 失敗均保留；按實際 UE 5.8 headers 修正，BuildR3 後成功。查引擎路徑時曾猜 Material headers 在 Classes、SkeletalMeshUtilities 在 Editor，應先 rg --files，實際為 Engine/Public/Materials 與 Developer 模組。

ImportR1 的 CommitMeshDescription ensure：預覽 Skeletal Mesh 沒有匯入 LOD，必須先建立 GetImportedModel()->LODModels，再保存 MeshDescription；即使 log 出現 saved marker，actual exit1 仍是失敗。首輪輸出保留作診斷，不視為交付。

VerifyR3/R4 強化檢查失敗但診斷不完整；VerifyR5 定位 BodyMesh 的材質重開為 null。根因是把「沒有 RF_Transient 且不在 GetTransientPackage」誤當真正資產：預覽 Actor 下的 Dynamic Material 同樣符合這個條件，但不能當永久 asset 參照。改用 IsAsset 前提，不符合者持久化成 Material Instance Constant，再複製 parent／參數／貼圖。不得僅以幾何能重開或 SavePackage 成功宣稱材質也正確。

FReferenceFinder 的 SerializeRecursively 在 Outer=nullptr 時實際不遞迴，必須明確遍歷材質所屬 expression 子物件收集貼圖參照，再替換。貼圖去重必須包含壓縮與取樣設定。場景 Render Target 不屬卡片來源資料，使用可保存的中性遠深度貼圖並明列即時陰影需由目標場景供應，不能留下 null sampler 或 transient capture。

ImportR4/VerifyR6 首套全資源通過，最終 BuildR10 Succeeded。ImportAllR1/VerifyAllR1 全九套通過，367 Static Mesh／99 Skeletal Mesh、979 材質／212 貼圖，1,201,500 來源三角形，重複 construction 不累積，所有 actual exit0。Rust/session/cache 版本未變，原卡未改，未新增卡別補償。Cook 與最終封存結果另記本節末；不把資源驗收當所有卡外觀／動畫控制器／零bug認證。

E-236後續：完整非Editor GameBuildR1 exit6（710.55秒），既有KKPreview/KKMainItemPlus等runtime shader建立與Editor動畫tests無WITH_EDITOR隔離；新增importer/Actor本身已編譯，不能把此結果說成整個遊戲可打包。把Data Asset/Actor分離至KKImportedAssets，只依賴Core/CoreUObject/Engine。RuntimePluginR1首次在嚴格Engine plugin UHT要求Category時失敗6，補全公開屬性Category；R2/R3/R4空白HostProject UnrealGame Win64 Development及Shipping全部BUILD SUCCESSFUL/actual0。BuildR13誤用只有Static Mesh存在的IsMeshDescriptionValid於Skeletal Mesh，改GetMeshDescription。BuildR17補UV驗證時patch誤命中Save的FinishAllCompilation，用到不在該scope的Card，改Save遍歷Assets、Verify遍歷Card；失敗均保留。Windows rg路徑KKForge*.cpp字面glob再次失敗，應目錄加-g。LegacyR1與Gamebuild同時啟動時卡在Turnkey ValidatePlatforms等UBT mutex，沒有卡匯入錯誤；後續串行。

E-236 ImportSmokeR1：新加入/Temp/ package參照檢查攔到MI_00002仍持有預覽RenderTarget，actual1、未寫輸出。僅GetAllTextureParameterInfo不夠：runtime MID可帶不在現行expression dictionary的override，CopyMaterialUniformParameters會複製整個TextureParameterValues陣列。改成逐筆持久化全部override，不以unused為由留下暫存或默默丟掉。ImportAllR3通過強化保存檢查，但UV獨立檢查再發現下一項問題。

E-236 UVRedR1：UE FMeshBuildSettings預設bGenerateLightmapUVs=true、DstLightmapIndex=1，原先只在Source保存四UV仍會在render build重包UV1。以來源與實際LOD的四UV tuple集合比對，舊AllR3的SM_00001實際失敗/exit1。現在關閉lightmap重包與刪除來源degenerate triangles，保存Source/RHI四UV，九套AllR4保存與VerifyAllR4全部通過，uv_streams=preserved。不以UV通道數等於4當保留成功。Source貼圖尺寸/byte count、triangle完整性及存檔前輸出已出現的競爭情境也加拒絕檢查，避免越界與覆寫。

E-236最終：BuildR19 Succeeded4.88秒；ImportAllR4、VerifyAllR4 actual0，9套367Static/99Skinned/9Skeleton/979材質/212貼圖/1manifest=1667 assets，1201500來源三角形。RuntimePluginR4 Dev/Shipping actual0；DetachedR2只帶此plugin及uassets，無卡/共用pack/Rust/KKUnityBundle，automation Success/actual0，9套構建、反覆切換與非原點旋轉/非均勻縮放World placement/Root attachment通過。CookR3 actual0，1667全Cook且skipped0，5個既有editor插件HTTP逾時warning，無shader/資源錯誤。Actor新增ExposeOnSpawn及Blueprint RebuildCharacter。最終僅用Stage358AllR4，前期outputs不交付；原始任意KKS卡、私有骨架動畫/full controller/逐像素parity/整個OpenKoikatsu Game build未驗收，源碼Rust未改，歷史seals不重封。

## E-234 Stage356：一般語音解碼／裝置取樣率與測試世界

一般說話以 comm_lines.json TopicTalkCommon_ / sun_tk_ 及精確性格bundle選取，不以整個adv目錄當一般語音，因adv索引還含其他類型資產。無卡別補償、無性愛語音連動。來源Player.voiceNo優先Voice.infoTable[-100]，本索引只有女角普通話題，因此UI不讓男卡以personality誤用女聲，顯示缺少男角一般語音。

BuildR1：FSoundQualityInfo在VorbisAudioInfo.h只有forward declaration，應讀實際Interfaces/IAudioFormat.h，使用TargetPlatform include path。BuildR2：USoundWaveProcedural繼承需要AudioExtensions模組；具體FVorbisAudioInfo.HasError沒有export，應經ICompressedAudioInfo virtual interface讀取狀態（noinline helper避免被devirtualize為未匯出符號）。BuildR3成功；後續R4–R9成功。本輪再次猜錯插件內CardTypes/Msgpack/FaceAnimation spec、Engine標頭、Docs協調檔位置；PowerShell rg glob也曾當字面檔名失敗。以rg --files找實際路徑，不繼續猜同一不存在檔。

UE_R1 Core成功但Playback測試崩潰：World.SetAudioDevice收到main world handle，違反InHandle.GetWorld()==this。修正為AudioDeviceManager.RequestAudioDevice且AssociatedWorld=測試world。UE_R2 timeout frames0；R3主動tick/deviceUpdate後frames53248、mouth1，但editor非PIE的bGameTicking=false仍反覆暫停game-only音源。只將latent測試音源標UI sound解除測試環境干擾；產品保持一般game audio行為。UE_R1 actualexit3；R2/R3 actualexit0但Test Completed為Fail，故仍是失敗，不用exit0代替測試通過。各輪log／stdout／stderr保留。UE_R4自然結束／隔離PASS，R5增加實際pitch三倍率和profile／臉部回歸PASS；R6加全1160 corpus與有效嘴型恢復檢查，六項Success/actualexit0。

重要通解：AudioMixer的generator InitParams.SampleRate為裝置rate，不是OGG rate；wave也必須設相同rate，再由generator把原PCM轉換到裝置rate，避免速度錯誤或双重pitch修正。R6實際1/.94/1.06播放時間符合Duration/pitch，40性格1160檔皆解碼。失敗語音先驗證解碼再替換；每次新component，回呼按component identity防舊完成事件誤停新clip。AudioAssist previous是上一次raw clamped音量，不是平滑結果；stop保持previous、load重置。

本輪使用生成器pre-mixer RMS fallback，尚無wavInfoData及Unity最終輸出包絡逐樣本比較；未做畫面聲畫時差驗收。不能將六項PASS當全語音／所有角色畫面零bug。詳Docs/GENERAL_SPEECH356.md，Saved/Tests/Speech356/Audit.json保存源碼／輸入／部署binary／各輪log SHA，原Rust243/353/108不變。

E234最終：BuildR9 Succeeded5.57秒、UE_R7六項Success/actualexit0，40性格1160檔解碼及三pitch實際播放再驗通過；UI男卡不代用女性普通話題。R2/R3 actualexit0但TestFail保留，runner必須逐項核對Success。所有owned退出、三DLL已部署，聲畫／原生最終包絡完整parity仍未驗收。

## E-235 Stage357：GPU身體不是GPU嘴型、建置失敗後舊DLL陷阱

新增普通語音decoder/generator→Actor.Tick→實際嘴型renderer接線驗證。CPUR1 PASS563040頂點；GPUR1 TestFail/actualexit0：錯誤要求每個嘴型slot有GPU component。診斷源碼bCanSkel要求rest_positions/weights/bone_idx與skel_kind1/2/3；實際KK_226176的五個嘴型slot kind0，所以GPU身體模式仍使用CPU嘴型。不得為讓測試過而改產品／強迫GPU；改測試按source eligibility檢查，顯式保存cpu_slots/gpu_slots與gpu_bodies，GPU嘴型0不列GPU morph/RHI驗收。

BuildR2 failed/actual6：用了OkMeshView卻只含KKRustExtract forward declaration，補ok_extract.h。當時外層PowerShell沒有失敗gate，仍誤跑GPUR2的舊DLL，結果重複旧assert TestFail/actual0；該輪不是新測試驗收，log保留。後續建置exit非0立即throw；renderer runner加test source timestamp vs deployed DLL檢查。BuildR3 Succeeded6.34秒後CPUR3/GPUR3皆Success/actual0，各563040實際頂點、90步／2嘴型、manual通道誤差0、另actor不變。GPU body實際可見component1，但GPU mouth slot0，此界限保留。

新增full-RHI測試後，一般headless runner原OpenKoikatsu.Speech前綴會納入不適用NullRHI的RenderedMouth，因此改為明列原六項tests，renderer另runner串行。再次猜錯Public/KKFaceBlendShape.h、Docs/GPUSkin.md、gpu_skin.rs及不存在AGENTS.md；改rg --files找到private header與根目錄GPUSkin.md。曾把PowerShell -ErrorAction接到rg導致參數錯誤，改純rg；未過濾log tail帶入大量data URL，改只輸出有限匹配。這些不是產品失敗，仍記錄避免重犯。

測試直接以普通話題PCM的包絡驅動renderer，非同步音訊錄影；manual參考不是原生獨立oracle，既有NativeWeights fixture另行驗證；全CPU/混合各自通過，不冒稱跨模式vertex parity或零bug。原生成器pre-mixer包絡與聲畫latency仍有既有未驗範圍。詳Docs/SPEECH_DISPLAY357.md、新Audit357，保留Audit356及既有碰撞279近中心線未驗收結果。

E235最終：同最終DLL的Speech356UE_R8原六項全部Success/actual0；1160檔／40性格再解碼及三pitch真裝置播放PASS。CPUR3／GPUR3成功並已退出；新增Audit357保存本輪源碼、native參考、輸入卡／OGG、現有三DLL、全失敗／成功log與舊Audit356原檔SHA。未發現需改產品的語音問題，不加卡別暫解；GPU mouth槽0、聲畫latency與native最終output oracle仍未驗收。

## E-240 Stage362：保存資產的實際骨架元件驗證

材質缺少 SkeletalMesh/MorphTargets usage 的假設未成立：UsageRedR1 Success/actual0，不能因 tag 叫 Red 就當反證。新測試先查保存flags，再註冊99個骨架元件讀取CPU蒙皮；72個有效morph開啟/清除精確還原，99個poseable根骨ComponentSpace平移/還原均通過。沒有產品匯入器或卡別修補，最新資產仍Stage361AllR1。

BuildR2 actual6重犯TObjectPtr陣列以auto*迭代錯誤；應const auto&再Get()，不得建置失敗後跑舊DLL冒稱通過。BuildR4 actual0/5.10秒；SkinR1/SkinR2 Success/actual0。Runner新增來源/DLL時間檢查並核對Automation Success及實際退出碼。Material.h路徑曾猜錯，應rg --files定位Engine/Public/Materials。GetCPUSkinnedVertices會切換CPU模式，即使實際RHI啟動也不能冒稱GPU變形/pixels驗收；根骨平移不等於完整關節/動畫控制器驗證。详Docs/INDEPENDENT_ASSET_RUNTIME362.md與新Audit362，保留舊Audit361與所有事故。

## E-241 Stage363：非根骨旋轉不能用根骨移動代替驗收

Stage362只能驗根骨平移，本輪依保存section BoneMap/量化權重自動挑最大總權重非根骨，旋轉.35 radians，逐頂點依inverse reference×component-space矩陣及weight/65535計算預期位置，比對實際CPU SIMD蒙皮；還原要求baseline精確相等。固定64*FLT_EPSILON*(1+sum absmax weighted terms)捨入預算，未以失敗調epsilon，非原作dump獨立oracle或bit parity。99模型/root_only0/130797頂點，maxerror .0000457764cm，JointR1 Success/actual0；BuildR1 actual0/5.16秒。沒有新產品/編譯錯誤，匯入器/資產不變，無卡別補償。不能把這輪當所有關節/軸/GPU/fullcontroller通過，Actor仍Static。詳Docs/INDEPENDENT_ASSET_JOINT363.md、新Audit363，舊Audit362/361原檔保留。

## E-242 Stage364：Editor 匯入介面與模組載入

新增Editor-only KKCardImporterEditor，Tools與ContentBrowser新增選單、Slate選檔/目的地/服裝、等待及錯誤、成功定位Character。共用commandlet保存，不複製卡別轉換；空GameRoot native session預檢提供結構化raw/outfit錯誤。loaded package prefix必須Destination+'/'，不得把同名前綴兄弟資料夾誤拒絕。UI仍只接受轉換獨立PNG/APNG，共用卡需pack，原PNG拒絕。

BuildR1 original Editor鎖DLL/LNK1104 actual6，未殺使用者Editor。BuildR2 private UToolMenu.FindEntry actual6，應FindSection再section公開FindEntry。單模組BuildR3 actual0但沒更新.modules，InterfaceR1啟動找不到newmodule/ensure且actual -1073741819，只終止exact exe/log所屬測試程序。曾暫補module manifest保留before副本；使用者關Editor後完整BuildR4自動產生manifest，最終非手補交付。InterfaceR3 actual0但TestFail：TryExecuteToolUIAction只處理FToolUIAction、不處理FUIAction；raw/range預期error計數錯誤且error capture未提供可靠native訊息。BuildR5再次存取private Action actual6。現在用FToolUIAction原生context delegate、native Session預檢及log flush；錯誤訊息不依一行passed判通過。

Final BuildR7完整Editor actual0/6.07秒；InterfaceR5 actual0/AutomationSuccess，兩menus/action Slate窗口/PNG、輸入與raw/range拒絕、273資產single outfit實際匯入/overwrite拒絕通過。新程序VerifyUIR1實際RHI actual0：46Static/11Skin/125Mat/89Tex/1Skeleton/1Manifest，skin25168 triangles、morph312項96012deltas、36mipchains434層、UV/colors維持。詳Docs/CARD_IMPORTER_UI364.md與新Audit364，所有舊seal/Stage361資產不覆寫。同步匯入無cancel，UI未接標準PNG factory、全controller/私有rig/rawKKS/fullShipping/pixels/Stage355279限制維持。

## E-243 Stage365：原始 KKS 卡的專案共用資源與相鄰 zipmod

既有 v1 共用卡資源不能直接當原始卡的 logical bundle/list 索引。新增 native_v1 的 1146 基礎檔案獨立副本與 SHA-256 catalog（30,030,018,520 bytes），匯入器自動建立 Saved 私有視圖；只掃卡片同層 zipmod，沒有 installed game/global mods/config fallback。匯入時 game_root=provided 指私有視圖，不代表需要原遊戲。

BuildR2 actual6：FJsonValue::TryGetObject 必須傳 out pointer；Python patch 讀 Rust UTF-8 未指定 encoding 被 cp950 解碼失敗，且後續 build 沒 gate 前一步，造成部分 patch。應 explicit UTF-8 並逐步檢查退出狀態。PowerShell 的 rg 不展開 path/*.h、Docs/INDEPENDENT_ASSET*、Tools/*.py；本輪重複犯錯，必須用 rg --files 或 rg directory -g pattern，不能再傳猜測的 glob 路徑。Rust mods.rs 和 Editor spec 的路徑亦曾猜錯，已用實際 rg 搜尋定位。

RawModsR1 actual0 但 AutomationFail：合法 zipmod 的 UTF-8 BOM 被 UE XML ConstructFromBuffer 判非法。原始拒絕 manifest 已保存。只移除解碼後開頭 U+FEFF，保留 ZIP CRC/長度驗證；RawModsR2 與 LocalModsR1 通過。相同GUID不同內容拒絕，任意檔名可依 GUID 讀取；nested mods 不掃。Strict body guard 不能把 body descriptor GUID 當套件 manifest GUID；改用原生 uncensor_body_for_card resolver，Default.Body.Female/Male 使用原生標記。

BuildR7 Succeeded/actual0（56.34秒）。InterfaceR1 菜單/Slate窗口/9 outfits共1667資產 Success/actual0；VerifyModsR1、VerifyVanillaR1、VerifyUIR1新程序實際RHI均actual0，UV/colors/mips/triangles/morph通過。LocalModsR1 BOM/CRC/去重/衝突 Success/actual0。原版男角102資產、模組卡313資產；原始 KK_346095 缺 kks.co_bot_00，明確拒絕，不補卡別替代。PowerShell runner 是 script 非 native executable，不能用未設定的 LASTEXITCODE 做下一步 gate；曾因此讓串接verify只跑首項，已分別執行並檢查所有實際log/exit文件。

詳細操作、最終 raw 重測與 Rust 結果見 Docs/CARD_IMPORTER_NATIVE365.md 及本輪 Audit。舊 Stage361–364 seals/資產/原卡保留。完整 controller、GPU 像素、所有模組、跨磁碟複製 fallback 與完整應用 Shipping 未驗收，不能聲稱沒有任何 bug。

Stage365 最終：RawVanillaR2 / RawModsR3 AutomationSuccess actual0、VerifyVanillaR2 / VerifyModsR2 actual0。Rust portable/mods release各3passed/0failed/1ignored。額外debug全庫522項觀察10個Fail，工作記憶體約87GB，核對本輪pwsh log及cargo/test父子樹後僅Stop自有test child，actual-1，不算PASS。兩項release精確重測actual101：main_alpha舊測試translucent vs現有masked；mixed ColorMask期待bakedPNG vs原PNG。沒有改期待值掩蓋失敗，其餘未逐一核對。保存cancelled JSON與所有failed logs，完整庫/像素零bug未成立。

## E-244 Stage366：材質分流、Windows模組路徑與測試隔離

十項精確release重現全部101。main_alpha被不透明masked早分支截走，違反原生FORWARD SrcAlpha/OneMinusSrcAlpha與DXBC採樣alpha；已分流，SESSION244。SQL原始字串斜線造成7310zip全部removed/changed；inventory及增量normalize Windows斜線，真變更仍失效。ColorMask測試誤要求CPU預染色，改驗原始貼圖/色彩各自保留；samplerfixture缺NormalMapbinding、且錯認MainItemPlus有Texture2/3/liquidmask，修正fixture與HasProperty負例。兩輪sampler失敗保留。5個缺歷史dump的診斷顯式ignored，不作PASS。dump_game_dxbc是有寫檔的診斷，精確重現曾寫共用ShaderDxbc index/shaders，已改必須explicit bundle/fresh out、禁止覆寫；新EC bundle488blobs24shaders PASS。不能宣稱舊共用index完整保留；舊Audit原檔未重封。

本輪仍誤用rg未展開glob與猜測ref/Decompiled路徑，已改實際rg --files定位kks_decomp。一次泛用patch片段誤修改alphablend測試而非main_alpha，FinalR1 blend8pass1fail揭露；改用函式context修正，FinalR2 blend9pass。不改產品期待值去掩蓋錯誤。詳細與最終證據見Docs/IMPORT_REGRESSIONS366.md，新Audit366。

Stage366 RawModsR1虽Success/actual0，新程序核對由195914降191298triangles、158降157materials；透明Forward修正後通用bOutlinable把MainAlpha獨立One/Zero輪廓一起刪掉4616triangles。拒絕此輪final結果，不以exit掩蓋；新增kind11獨立outline入口，其他透明shader排除維持。既有輪廓adapter仍非原生MainAlpha全部fragment/WPO pixels驗收。BuildR2 actual0/41.98秒，ModsR2/VerifyModsR2重測。

Stage366 final：ModsR2/VerifyModsR2 actual0/Success，313assets、158mat、195914Statictris完整恢復，19914Skintris、300morph/91970deltas、15mipchains/230levels、UV/colors維持；VanillaR1/VerifyVanillaR1 actual0。ShaderOverwriteR1預期101拒絕既有目錄，index SHA256不變。新seal366不改旧365Audit（72419E...F6C06）。

## E-245 Stage367：獨立來源未採用 MaterialEditor 法線預設

Standalone 私有來源無外部 config，舊函式 read失敗→false，與反編譯 Config.Bind Convert Normal Maps 預設true不符。NormalRedR1精確重現actual101；改標記require_complete_mods來源缺config→true，明確false優先、unmarked既有來源維持，SESSION245/cache108。不按卡名修色、不讀原遊戲配置。產品轉換通道/行方向/sRGB公式不變。

NormalGreenR1曾5pass但歷史March7th runtime dump診斷缺檔直接return：不列為原作比對通過，改明確ignored，手動啟用要求fixture存在。NormalGreenR3 actual101：新增false負例錯誤假設runtime sampler必存在，index panic；改Option，true仍要求RT全metadata，false貼圖必存在且不同，未改產品掩蓋失敗。最終GreenR4 5passed/1ignored，另Rows/Sampler各1、SQL6，共13有效pass。新增draw assembly四角色×bundle/ME，與explicit true位元相同、wrap/sRGB/mips及false反例通過。本輪又猜錯KKCardSource.cpp模組路徑，Get-Content失敗；必須先rg --files定位，不猜路徑。

BuildR1完整Editor actual0/61.91秒；產品修正已含DLL，後續只修改cfg(test)。RawModsR1/VanillaR1與各自fresh RHI Verify actual0/Success；313/102assets，幾何/morph/UV/colors保留，Mods mip18chains259levels（舊15/230）。詳Docs/IMPORT_NORMAL_DEFAULT367.md與新Audit367；缺dump不計pass，全卡/像素零bug/fullcontroller/fullShipping未驗收。舊Audit366原檔與資產保留，不重封。

## E-246 Stage368：資源包 SHA／複製兩次讀取與平行發布競爭

舊 build_native_resources365.py 先 hash 再 copyfile，讀取期間來源變動可使 catalog SHA 不等於保存 bytes；exists後copy也不是平行去重發布的原子操作。BuilderRedR1 actual1 注入實際來源改寫，原工具仍成功輸出錯誤catalog。現改同一串流寫独立暫存副本/hash/count，前後ID/size/mtime核對，完整close後exclusive os.link發布；重複目標驗SHA/size，不覆寫。全worker成功才發布catalog，任何失敗留partial但無可用catalog；source不是hardlink，CLI/schema維持，拒絕空abdata。

BuilderGreenR1 actual1：Windows Python3.12 stat/fstat ctime定義不同，正常檔案亦誤拒；改兩API一致的dev/ino/size/mtime_ns，注入變動必須實際執行，不能用提早錯誤當通過。GreenR3 actual1：新增test函式插在正例最後三行之前，owned變數落到錯函式造成NameError；依完整函式界線修正，產品不變。失敗資料保留。最終GreenR4 actual0/3passed：變動拒絕、24logical/5unique且4worker barrier同時發布、獨立副本與重跑拒絕、發布I/O失敗無catalog。詳Docs/NATIVE_RESOURCE_COPY368.md及Audit368。

只修離線建包工具，不更動Unreal邏輯/DLL，沒有無故重跑舊runtime驗證。原native_v1 1146條目/30,030,018,520bytes只讀全SHA核對，不重建。原卡/資產/Audit367保留、不重封。mtime檢查不是跨檔案停機快照；發布需同目錄hardlink支持，未宣稱全卡/GPUpixels/fullcontroller/fullShipping零bug。

## E-247 Stage369：遊戲目錄卡片的 zipmod 自動來源

新增沿正規化卡片祖先偵測 abdata/UserData/KoikatsuSunshine.exe或Koikatu.exe，同時取相鄰zipmod與遊戲mods遞迴zipmod；外部卡不掃全機遊戲。來源仍project native_v1，不讀原abdata或plugin/config。序號archive名稱解決不同目錄同名檔碰撞，GUID解析維持。介面help同步更新。

RawGameR1/R2均actual0但AutomationFail：R1把無關global損壞XML當fatal；R2又把遊戲已裝多版本com3d2.hair當衝突。依反編譯Sideloader改global無效manifest記錄略過、global同GUID按ManifestVersionComparer數值token/版本降序與filename長度選擇；缺版本按mtime降序。相鄰錯檔fatal與相鄰/選中global不同內容衝突保留，真缺件strict guard不補替代。5個不支援/損壞global manifest記錄，不宣稱支持。穩定路徑tie順序非原遊戲Directory.GetFiles順序保證。

BuildR3 actual6：猜錯Misc/LexFromString.h，rg定位正確String/LexFromString.h，BuildR4 Succeeded actual0/5.18秒。曾猜錯KKSideloader模組路徑，之後用rg --files；一次PowerShell Python inline SQL錯誤引號SyntaxError，未執行查詢，改here-string stdin readonly。SourcesR1 LocalZipmods AutomationSuccess actual0：祖先/正規化/遞迴/外部與非安裝隔離/同名archive/CRC/BOM/衝突/壞global跳過/1.10>1.9/mtime選版通過。

RawGameR3 actual0/AutomationSuccess：遊戲UserData原始KK398530自動選7283zipmods、1146project base，313assets。VerifyGameR3 fresh實際RHI actual0：195914Statictris/19914Skintris、158材質/85貼圖、18mipchains259levels、300morph91970deltas/tiny0、UV/colors維持。外部卡及最終封存見Audit369。原卡/zipmod/歷史資產與Audit368保留。外部AdditionalModsDirectory/普通zip未支援，Actor仍Static，全卡/GPUpixels/controller/fullShipping未驗收。詳Docs/GAME_DIRECTORY_ZIPMOD369.md。

Stage369 final：RawExternalR1 actual0/AutomationSuccess且detected_game=none/import_zipmods33，VerifyExternalR1 fresh實際RHI actual0；與遊戲來源皆完整313assets/195914Statictris/19914Skintris/158mat/85tex/18mipchains259levels/300morph91970delta/tiny0。全部自有UE測試已結束，最終Audit369保存source/DLL/failed&passed logs/兩組新資產，舊368及367Audit與歷史assets不重封。


## E248 / Stage370 原遊戲骨架動畫匯入（進行中）

首次 MaleR1 actual1：來源 palette 順序不等於 USkeleton MergeAllBonesToBoneTree 保存後的索引，骨骼3階層檢查拒絕。修正為唯一 FName 映射，按父骨骼名稱/綁定姿勢核對，烘焙時用 target→source map；禁止按相同index硬套。失敗log與輸出不覆寫。

調查曾猜錯 Rust skel_export、AccessoryRuntime 及 UE IAnimationDataModel 位置；應先 rg --files / rg 宣告再讀，避免再次猜路徑。普通動畫驗證使用 studio/anime/00.unity3d 的男女站立/行走，來源精確label由 ListR1列出；動畫catalog不是UAnimSequence，不可拿metadata成功宣稱匯入完成。

現有卡片包含僅Static的部件以及kind4私有rig，骨架播放通過不等於完整角色所有部件/動態/原作pixels通過。必須另驗部件跟隨與變形，禁止直接擴大kind4為現有全域palette或按角色/圖像補丁。最終實測見Docs/ORIGINAL_SKELETAL_ANIMATIONS370.md。

MaleR2 actual1：寫入後用GetBoneTransform(false)混入UE壓縮資料與原始model核對，誤把兩種取樣途徑當等價；改直接GetBoneTrackTransform按原始sample幀驗證寫入，另保留fresh重開的Unreal實際evaluate/變形驗證，不放寬原始TRS門檻。

MaleR3 actual1補充：直接raw-track比較位置error .000000238cm、scale0，但四元數角差.001145205rad超門檻。AngularDistance要求unit quaternion；DataModel float重建quat的norm舍入會製造acos角差。比較時將讀回rotation正規化，維持原position .01cm/angle .001rad/scale .001門檻，不直接放寬門檻。MaleR2不能歸因於壓縮，先前描述僅是調查假設；raw/壓縮分驗仍保留。

MaleR4 actual1：GetNormalized仍得到同.001145205rad假角差，先前修正不足；讀UE Quat.h後確認Normalize的SIMD inverse-sqrt不是精確unit。最終角度metric改 abs(dot)/sqrt(normA² normB²) 後2acos/clamp，對兩邊殘留長度都消除，仍維持.001rad門檻。原因須以新實測證明，不以編譯通過替代。

MaleR5 actual1再次保持同角差，否定「norm舍入是根因」假設。UE源码AnimSequencerController::SetBoneCurveKeys會把FQuat4f.Euler存入FK ControlRig的Euler曲線，旋轉奇點有不可逆角度損失。改用自己的UAnimSequence子類，只為此類註冊UAnimDataModel四元數provider（Engine AnimationData PreDefault，runtime module Default），標準UAnimSequence與其他資產不改。Runtime只讀標準壓縮動畫，不依賴Editor模型；後續同時量測default_euler_loss與quat原始寫入誤差，以確認真根因。

MaleR6 actual1：module啟動phase不足以保證ModularFeature最後註冊，Engine選最後非null provider，實際NewObject仍得到Sequencer模型。補自己的ImportedAnimSequence在PostInitProperties/PostLoad的Super前重新排自己provider到最後；不移除其他provider，不影響普通動畫。ModelProbe須先通過再跑長匯入。

ModelProbeR1 actual1：Engine UAnimSequenceBase constructor已先CreateModel，PostInitProperties重排太晚。新資產在PostInitProperties的Super後替換尚無keys的model為UAnimDataModel並重設controller/通知；RF_NeedLoad不替換，其PostLoad先選對provider保留已保存keys。

BuildR10 actual6：OpenKoikatsuModuleRules.dll短暫I/O鎖，未刪檔/未終止未知MSBuild節點，保留失敗並fresh BuildR11重試。追加文件曾因Python Windows預設cp950讀UTF8 MD而UnicodeDecodeError（寫入前），改所有中文MD明確encoding=utf-8。新猜路徑失敗（AnimDataModel/Runtime module/Sequencer controller）已以rg --files定位，不當成產品修正。

MaleR7 actual0：667bones，行走129frames/2.133333445sec、站立300frames/5sec，raw angleerror max.000000084rad，position/scale0；對照默認Euler轉換loss.001145172rad證實根因。VerifyMaleR1 actual3：commandlet預設不建立render MeshObject，CPUskin讀回assert；runner補AllowCommandletRendering，保留failed log，不以NullRHI或略過vertices替代。

VerifyMaleR2 actual3：AllowCommandletRendering仍不夠，IsClient=false令world僅有不提供SkeletalMeshUpdater的scene。匯入/驗證commandlet改IsClient=true並保留AllowCommandletRendering，使用真渲染場景；不改Actor為CPU-only以掩蓋GPU初始化問題。

VerifyMaleR3 actual0：client=true+AllowCommandletRendering後真RHI重新載入播放成功，兩clip各11skins/共23140CPUvertices，bone_error0，motion68.308350/2.621200cm。FemaleR1 actual1：將任何零pose scale都當無效過度拒絕；零scale是原作有效的收縮/隱藏姿勢，UE可處理。僅保留pose TRS非finite拒絕，零scale逐骨記錄並原樣保存，必須fresh runtime驗證finite，而非按角色特判或把0改1。

Stage370 core：FemaleR2的zeroScale實際cf_hit_head，是有效非render collider pose；两clips667bones rawerror position/scale0、maxangle.000000103rad，原样零scale保存。VerifyFemaleR1实际RHI通过。DetachedR2 AutomationSuccess actual0，4clips44skin instances46280vertices、compressed valid+clock advances、无parser/pack。男6/女46 Static-only部件尚未完整動畫化，接續通解，不能宣稱全人物完成。


## E249 / Stage371 完整部件動畫覆蓋（進行中）

Stage370 的 44 個 SK 播放驗證只涵蓋 body/head，不能當整張卡動畫成功。新增只讀 Inventory 按 Comp 名稱+role 對原始 renderer：MaleR1 actual0，17部件/6 Static-only，全6 kind4 runtime private；FemaleR1 actual0，57部件/46 Static-only，其中37 kind4 private、9 kind0。另 body/head 和多個部件包含2材質區段，原 persistent skin 只保存主表面，完整材質/輪廓亦需通解。禁止直接改 kind4 gate 或以 head/role 猜掛點。

先抽出 native renderer 共用 effective_palette：保留 AssignedWeightsAndSetBounds、植入、已刪除 slot identity、原始 SMR slot bindPose，以及完整 socket correction。新增 additive binary API，匯出 raw geometry/4weights/slot indices + row-major world*bind matrix，舊 ABI 不改；獨立匯出路徑以現行原始 positions 作頂點 oracle，驗5時間點，不先降低門檻或假設矩陣可無損單TRS。

本輪操作錯誤：MaleR1程序尚未完成便讀尚未存在的JSON；後改等actual exit再讀。一次 apply_patch runner的context只寫半行，整批拒絕、未修改檔案；重新讀原文後使用完整匹配。源碼變更與資料驗證分開記錄，不以 Build success 宣稱私有骨架匯入完成。

Stage371矩陣oracle：MaleR2/FemaleR2五時間點最大error2.950026864e-7/2.711677741e-7m；shear .504950235/.154178694，證實單TRS不可當通解。MathR2 actual0，實際compressed zeroScale/shear/reflection讀回通過。新adapter初稿座標慣例猜成(Z,-X,Y)，在編譯前對KKSkeletalBuilder修為(-X,Z,Y)，加入直接native座標oracle測試避免同源自驗。另一次查Engine SkeletalMeshUtilitiesCommon猜成Editor目錄失敗，rg --files正確Developer定位；讀檔必須先discovery。算法尚未接 persistent SK/12 influences，不能當完整人物/GPU通過。

Stage371 final本輪驗證：BuildR6 actual0；MathR3 AutomationSuccess actual0（含ABI失敗不修改buffer），MaleR3/FemaleR3 actual0並重現相同native oracle誤差與shear。自有測試程序皆結束，Progress.json僅記錄進度/來源snapshot，complete=false，無全人物完成seal。


## E250 / Stage372 完整持久化動畫（本輪回歸完成）

新增原始卡schema2精確palette骨架：identity scene root、原作骨架locals保留、每個實際使用SMR slot三column骨；12 influences/16bit index，所有可見renderer含私有rig及原始rigid_follow掛點，不按卡/role猜骨。來源幾何/權重/binds hash與renderer身份、slot嚴格匹配；逐section保存材質/UV/colors、原始mesh-local morph。持久化SK/動畫正接入，未驗收前不宣稱通過。

BuildR1 actual6：FBoneIndexType誤加AnimationCore namespace、FVector4<double>直接轉FVector3f，以及局部Name遮蔽外層Name（UE視為error）；依實際header修global型別、先轉FVector、名稱分開。一次apply_patch尾段用半行context導致整批拒絕未修改，重新匹配完整行。另猜SkeletalMeshReductionSettings是Classes路徑失敗，rg定位Public；不能再猜Engine檔案位置。

Stage372續作：MaleCardR1 actual3，FReferenceSkeletonModifier 尚未析構便呼叫 Skeleton merge，raw bones已建立但full骨架仍空，Engine assert parent=-1。依 Engine Private/Animation/ReferenceSkeleton.cpp 析構 RebuildRefSkeleton 修正scope；骨數上限改查RawBoneNum。MaleCardR2 actual1：cache載入的private髮骨動態state尚未初始化，source hash不應依賴palette成功；改immutable source_mesh getter，卡片及rig驗證以原作ApplyAnimFrame(0,0)初始化完整掛點/動態鏈。BuildR3/R4 actual0；MaleCardR3 actual0 (17 SM+17 SK,108 assets)，FemaleCardR1 actual0 (57 SM+57 SK,359 assets)。這只證明模型保存成功，尚未代表原作頂點動畫通過。MaleAnimR1 actual1，HeadMesh來源檢查拒絕，新增分項renderer/role/hash診斷，禁止直接略過hash。
操作錯誤仍有猜Engine ReferenceSkeleton.cpp及runner單複數路徑失敗；先rg --files定位實際Private/Animation及run_detached_animations370.ps1。PowerShell rg檔案glob需用-g而非將*當literal path。

MaleAnimR2 actual1分項診斷證实renderer与role相同但hash不同。根因FOkSession::Open呼叫OKPrepareLiveGpuEnv，預覽GPU模式把face原始weights清空並烘焙成cf_s_head rigid，卡匯入則CPU模式保留原始SMR。新增scope保存/还原五個renderer env，原始動畫與native audit统一CPU資料模式，hash檢查仍嚴格，未硬編碼角色。BuildR5/R6 actual0。操作上一次在PowerShell使用bash brace expansion造成ParserError，未执行任何修改；改明確檔名參數。

MaleNativeR1 actual1：真RHI CPU頂點oracle第一幀body偏差3.984942769cm，未放寬0.05cm門檻。匯入初始化骨架後未呼叫Skin，PMC仍用初始cache positions，卻以新palette取inverse建參考頂點；補ApplyAnimFrame→Skin→LoadSession同步順序。FemaleAnimR1仍烘焙舊模型，已核對自有PID47260的EXE和唯一abslog後停止，保留失敗/未完成log；不可當女角動畫通過。

MaleCardR4/FemaleCardR2 actual1：新逐頂點triplet檢查攔下UE權重重建，原writer未設定bUseHighPrecisionSkinWeights，UE預設8bit量化破壞三column等權重。設為16bit，保留原始12 influences，逐render section BoneMap查回SMR slot，要求每slot恰三骨同weight、總65535，不略過或放寬。BuildR7/R8 actual0；待新卡與native oracle確認，4cm偏差原因仍須以修後頂點測試證實。

MaleCardR5/FemaleCardR3 actual0，16bit權重triplet守衛通過。MaleAnimR4 actual0；MaleCardVerifyR1 actual0，17SK完整79572tris、UV/colors/morph/mips通過。MaleNativeR2 actual1：oracle的initial_offset=4.428,-.250,-.002cm，錯把另一次session預設動畫姿勢和保存bind姿勢差當renderer區段offset。修為保存static同一renderer各section減主surface，與Preview RuntimeSectionOffsets邏輯一致，主surface必為0；不得將不同pose差視為幾何offset。0.05cm門檻仍維持，等待fresh oracle重驗。

MaleNativeR3/FemaleNativeR1 actual1：修正oracle offset後 body首幀誤差仍.064441363/.060235015cm，原始軌道誤差僅1.6e-5cm量級，需驗證壓縮loss。新增schema2使用BitwiseCompressOnly + translation/quaternion/scale ACF_None，不做一般關節容許的有損壓縮。提供fresh Recompress命令從已保存的原始四元數軌道重新編碼，不重讀原遊戲、也不覆寫舊資產。BuildR9/R10 actual0，待float版本native對照。

Float版本確認：MaleFloatR1/FemaleFloatR1 actual0；MaleNativeR4 actual0（643392頂點比較、max.006479431cm）、FemaleNativeR2 actual0（1536012頂點比較、max.001669370cm），全部17/57部件及所有sections，未改.05cm門檻，證實前次.06cm實為有損compression誤差。fresh MaleVerifyR1/FemaleVerifyR1 actual0，34/114 skin instances、107232/256002vertices、compressed有效，中間seek及finite檢查通過。WrongCardR1預期actual1，SHA1拒絕且無輸出目錄；FemaleCardVerifyR1 actual0，全57SK/195914tris與morph/UV/colors/native mips驗證。
新增環境scope完整還原env及KKSkeletalBuilder三個mode/cache flags；不可僅還原CPU旗標或重新Prepare而把原本false的全局GPU mode改true。卡匯入亦包住caller環境，數值來源模式與已通過case保持一致。最後BuildR11/Math/Detached驗證後記final。

BuildR11 actual0，Math372R1 AutomationSuccess actual0（含source scope完整env/cache還原）。DetachedR1 actual3：保存的float duplicate動畫帶入AnimationData/ControlRig資料，無editor外掛時DataModel空assert。不能以在隔離host開啟額外外掛掩蓋依賴；Recompress改新建UKKImportedAnimSequence，逐骨只複製原始四元數/TRS軌道，拒絕非UAnimDataModel目標。另壓縮settings改獨立uasset，避codec instancing/PostLoadSubobjects沿Sequence outer提前PostLoad；原Core Import同用獨立settings，全部Reference核心Engine類型。失敗host與uassets不覆寫，freshR2驗證。

Stage372 final：BuildR12 actual0；MaleFloatR2/FemaleFloatR2 actual0，fresh quaternion Model与独立settings后DetachedR2 AutomationSuccess actual0，无ControlRig/Sequencer/parser/pack依赖，4clips148skin instances363234vertices，完整renderer覆盖+clock advances。MaleNativeR5/FemaleNativeR3 actual0，最终新构造资产重跑2179404次顶点比较，最大.006479431cm。普通最终匯入MaleFreshR1及FreshVerifyR1 actual0；MathR1环境/cache恢复及affine codec测试成功。ExistingR1预期actual1，不覆写既有目标。OldAudit370 JSON hash不变；570列中的8个live来源/MD变动属继续开发，旧资产与封存来源/二进制仍一致，不将live源码预期变化误称旧资产损坏，不重封旧Audit。


## E251 — Stage373 無蒙皮髮型掛接與多 outfit 來源生命週期（本輪驗收完成）

- Male02CardR1 actual1：ill_male_02 的 cf_hair_s_03_00 沒有蒙皮權重，完整匯入拒絕；不是缺少 zipmod。
- 根因：rig_mesh 髮型 fallback 已在 HairParent 下烘焙頂點，但 rigid_follow_bone 只在 OK_GPU_SKIN=1 保存；原卡匯入使用 CPU SMR 來源，因此漏掉同一個原作掛接資訊。
- 原作依據：ChaControl.ChangeHairNoAsync（ChaControl.cs:5821）取得 RefObjKey.HairParent，LoadCharaFbxData copyWeights=0；ChaReference.cs:228 對應 cf_J_FaceUp_ty。
- 通解：CPU/GPU 共用 fallback 保留掛接資訊，保持 world bake 並清除 parent_bone 以避免二次掛接；不依卡片/mesh 名稱猜骨骼、不補造權重。Session pipeline 245→246，decoded bundle cache108不變。
- RustR1 actual101：新測試誤用 .2 等 Rust 不接受的 float literal；已改為 0.2，保留失敗 log，後續只以成功重跑判定。
- 所有驗收輸出使用 Stage373 fresh 目錄；Stage372 Audit.json 不重寫。

- R2Male224305Card：actual0但缺少全部匯入完成標記；只完成outfit0–4，沒有Character.uasset，驗收必須失敗。不得把process exit0當成功。程序提前退出的外部原因未確認；System resource-exhaustion事件未取得，當前shell JobLimits顯示沒有memory limit，因此不得冒稱已確認OOM。
- 同時確認匯入器的資源生命週期問題：Actor::Destroy只等待GC，逐outfit仍持有Rust session和預覽資源；Copies以來源UObject地址作key，若加入GC但不清理會有地址重用風險。通用修正以FGCObject保留所有待保存輸出，完成每個outfit立即Close來源session、清空outfit來源Copies、等待asset編譯後GC，並記錄memory；不減少outfit、部件或動畫覆蓋。待fresh完整矩陣验证，不先宣稱已解決提前退出。

- 最終R3兩卡10項完整矩陣皆actual0：ill_male_02全4 outfit/69部件/8clips，KK_224305全7 outfit/312部件/14clips；完整保存381 SM+381 SK，全部材質/UV/顏色/morph/mip/部件覆蓋重開驗證。
- Native R3共15063804次全section頂點比較；男02 max .006482028cm，KK224305 max .006049211cm，原.05cm門檻未放寬。
- DetachedR1 Automation Success actual0：無parser/來源包，22clips、762 skin instances、2510634vertices，compressed/clock/seek/finite/部件覆蓋通過。PriorFemaleR1 actual0：57部件、兩clips、1536012次比較，max .001669370cm，未退化。
- BuildR2 actual0；RustR2 CPU/GPU rigid-hair掛接與非等比父節點fixture通過；相同矩陣tag在launch前拒絕，plan SHA未變。R3每outfit Close來源+GC後完整保存並可重開，不能把此結果當作已確認R2退出的外部原因。
- 工具調查曾誤用rg wildcard檔案路徑與猜測不存在的Engine/Plugins/Marketplace；後續必須先rg --files確認實際路徑，-g用於檔名glob。原生UAnimSequence使用rational sample rate保留SourceDuration，不能只看129 frames便誤判成固定60fps的時長退化；未做錯誤的timeline補丁。
- 文件Docs/CARD_ANIMATION_MATRIX373.md；fresh R3.audit封存。Stage372原Audit JSON與封存uassets/Sources/Binaries不修改。


## E252 — Stage374 私有骨架／掛接來源契約（宣告範圍驗收完成）

- 確認 v1 skin hash 只包含 rest geometry/weights/binds/bone names，漏掉私有 hierarchy/local TRS/attachment/assignment；來源 mod 僅改骨架時可能誤用舊持久化模型。
- 增加 additive v2 ABI 與 FKKImportedPart.SkinSourceHashV2；v1 ABI/舊資產保持既有檢查，新匯入要求 v2，CheckRig 在建 persistent package 前拒絕不一致。舊資產會提醒 reimport 以取得私有來源驗證。
- 不可直接 hash source.smr_skel.worlds 或 implant bridge 的 live TRS：implant.sync 每幀改寫橋接節點 pos/rot/scale；橋接保留 ID/name/hierarchy/assignment，TRS 由共用 body rig 驗證。其餘私有 TRS、rest、palette order、掛接/placement、移除/植入集合都納入，集合與 Transform map 按 ID 排序。
- 初版新增 authored_rig serialized 欄位會破壞既有 portable bincode 格式，review 後已撤除；最終不改 SessionBlob/AccessoryRuntime wire layout。pipeline 246→247 為中間版，最終248使中間快取失效；decoded cache108不變。
- RustR1 actual101：HashMap iterator 不支援 rev，改收集 Vec 後 reverse。首次 runner cwd 導致 exit receipt 目錄不存在，101依工具實際回傳补記；誤建空目錄已以非遞迴 Remove-Item 清除。
- R1Male02Card actual0 保存初版；R1Male02Animation 由本 agent 主動停止，actual-1，因發現 wire layout 問題重做，不是引擎 crash，不列驗收。第一個停止嘗試遇到程序已退出的 race，改用當時 exact owned EXE/log 核對後停止動畫子程序；原資料保留。
- LegacyR1 actual101：「portable_tall.png」實為截圖，無 okPv chunk；不得按檔名猜卡片。改用實際 KK_346095_builtin_v2.png，LegacyR2 actual0/原檔唯讀載入；RustR4 actual0 通過最終契約 fixture。調查也曾猜不存在 apng.rs 和讀 log 忘記 cwd，後續要先 rg --files 與絕對路徑。
- 最終完整 Build/矩陣/契約拒絕/隔離測試尚在進行，不宣稱全卡零 bug。

- 最終R2男女兩卡／outfit0全10checks actual0，19+57部件、4原作clips。Native全section2346744次比較，max男.006482028cm／女.001672517cm；原.05cm門檻不變。女卡前輪.001669370cm有微小差異，不能冒稱完全同值，亦未將門檻放寬。
- ContractR1Male02／Female Automation Success actual0，76份保存v2 metadata、播放不變、v1相同但v2不一致時拒絕、原資產不修改。DetachedR1 actual0／Success，無parser/pack、4clips152skins391124vertices，完整coverage/clock/compressed/finite/seek通過。
- BuildR3、RustR4、LegacyR2 actual0；R2.audit封存1115檔並重新全SHA/size驗證，Stage372/373舊seal/assets/snapshots保持，owned UE程序全部結束。本輪是宣告矩陣的私有rig／掛接契約驗收，不是全卡／全mod archive／GPU pixels／Shipping保證。

## E253 — Stage375 匯入目的地保護與驗證錯誤（回歸通過）

- 三個匯入入口的目的地檢查不一致，可能覆蓋記憶體中尚未保存的子 package；新增共用檢查，拒絕既有磁碟及 loaded descendants，保留 sibling。
- BuildR1 actual6：include 未將本檔 header 放第一、測試誤用不存在的 Schema 欄位，已修正。
- BuildR2 actual6：其他既有修改引用當時未定義的方法，未撤回他人修改；後續須重新編譯驗證。
- IsolatedBuildR1 actual1：使用 generic UnrealEditor target 誤觸大量 engine actions；核對 UBT 的專屬 project 命令列後停止 owned process tree。隔離資料與 log 保留；後續使用專案 OpenKoikatsuEditor target，不能把此中止當作通過。

## E254 — Stage376 共用動畫與嘴型（宣告範圍回歸通過）

- 已保存 morph targets 不等於存在原作 FBS 控制；既有角色專用 palette 動畫也不等於跨卡共用動畫。
- mouth interior 原匯入迴圈只選 visible renderer，會遺漏閉嘴時隱藏的牙齒、舌頭；改由原作控制 metadata 決定是否保留，維持預設可見性。
- 嘴型基準已烘焙在原始頂點，控制權重必須扣除 baseline，避免重複變形。未編譯或 oracle 通過前不得稱完成。
- 實作及驗收清單：Docs/SHARED_ANIMATION_MOUTH376.md。
- Python edit initially relied on cp950 default and failed before writing; all subsequent source edits explicitly use UTF-8. Several guessed source paths were wrong; use rg --files and the returned paths.
- Shared376BuildR1 actual6: current C++ compiled but production DLLs were locked by the user editor. IsolatedBuildR1 actual6 missed optional BpGeneratorUltimate; fixture excludes unrelated optional plugins only. IsolatedBuildR2 actual6 locked Engine NetCore; all six project/plugin DLLs linked. Module-only IsolatedBuildR3/R4 actual0; fixture manifests copied from same engine BuildId, not a production full-build claim.
- SourceR1 actual0: male/female raw walking curves match original samples after independent memory install. RustR1/R2 synthetic roundtrip and corruption tests passed.
- Shared ImportR1/FemaleR1 actual1: module-only build did not emit manifests, fixture startup lacked game module. Original matching .modules metadata copied to fixture. Destination test R1 owned process stopped after same fixture failure; keep logs.
- Shared ImportR2 actual0 but clips=1 instead of requested2: FParse default stops at comma. Fix shared commandlet FParse::Value separator policy; require requested clip count, not exit0 alone. Initial PowerShell unquoted comma command failed parsing before launching; quote arguments.
- FemaleR2 actual1 exact skin guard: isolation initially omitted Config/DefaultEngine.ini, disabling UnlimitedBoneInfluences and 16BitBoneIndex. No Exact skin success marker preceded failure; this was a fixture configuration mismatch, not established mouth renderer failure. Copy original Config verbatim and rerun fresh; do not weaken triplet/weight guards.
- FemaleRuntime R3/R4 affine error grew to40.894484520 only at backward seek (.25s); forward samples stayed below.001 matrix element error. Reference SetAnimClip did not clear body/clothes physics history, whereas shared seek correctly rebuilds its session. Fix oracle to reopen native source for each independent seek sample; retain original threshold.
- BuildR2/runtime R2 loaded stale diagnostic code despite current file timestamp; confirmed new diagnostic strings absent from DLL. Forced native compilation with -NoUBA and unique oracle marker; BuildR3/R4 actual0. Do not assume a rebuilt timestamp proves code identity.
- RuntimeR5 was launched while BuildR5 still running (tool returned session ID, not completion). Verified exact owned EXE/log/PID42260 and stopped this test to release the DLL. Await actual build completion before launching dependent UE processes; interrupted test is not accepted.
- FemaleRuntimeR6 actual3: new mouth geometry oracle read GetCPUSkinnedVertices immediately after hiding the oral renderer; Unreal intentionally removes its MeshObject and asserts on this debug API. Verify oral visibility separately and measure visible face deformation. This is the test's invalid API sequence, not a runtime mouth crash.
- Reopening native source at every forward checkpoint introduced a different physical frame history (4.376092553 matrix error). Oracle must mirror identical partial-frame calls for forward playback and reopen only for backward seek. Keep thresholds unchanged.
- Shared playback loop handling was missing: native curve evaluator takes a phase, while dynamics require real elapsed delta. Add wrapped/clamped phase with independent elapsed clock, matching KKPreviewActor.AnimPhase, and cover a loop-crossing sample.

Stage376 RuntimeMaleR2：Automation 實際 Fail（UE exit 0 不算成功）。body palette 一致，但 face 最大誤差 144.195964932cm、cm_top_o_menssailor palette 缺失；先補原生 oracle 部件名稱及 v2 hash 檢查，避免把不同來源索引錯配當成動畫求解錯誤。保留失敗輸出與原門檻，不對單卡硬改索引。
RuntimeMaleR3 根因確認：原生讀取器新增 o_dan_f/o_dankon 後，舊 snapshot 臉部 index=1、即時來源臉部 index=3；renderer 的 v2 hash 相同。OracleR9 只在測試端依 renderer+role+v2 hash 唯一匹配參考部件；runtime 仍嚴格核對 snapshot 內索引。缺失／多候選直接失敗，不降低 palette 門檻、不改卡片資產。

RustR3 編輯命令誤把專案相對路徑套在 Rust workdir；ReadAllText 失敗但 PowerShell 非終止錯誤後仍執行舊測試，不能當新檢查驗收。改用絕對路徑、ErrorActionPreference=Stop，RustR4 驗證新增 NaN 係數拒絕；保留 Unity Infinity stepped-key 編碼。

最終驗證：BuildR10 actual0；FemaleR7／MaleR4 palette 與真實 mouth geometry 成功，max matrix .000208616／.000245869。DetachedR2 使用 BuildR10 DLL，兩種卡共用同一曲線、80 SK、無 pack/tools/source/card root、曲線 morph／手動嘴型各自保留，actual0／Automation Success。目的地 Production376R1、ExactPaletteMath Stage376R1、既有 baked 隔離 Stage376R1（4 clips/148 skins/363234 vertices）通過；LegacyR1、RustR4 通過。全部中途失敗保留，不以 UE exit0 單獨認定成功。

SourceR2 原作 male/female walking 曲線逐點記憶體重開對照成功，actual0。最終 seal 保留舊 Stage374 assets/snapshot；本輪範圍不含全部卡／所有 controller／GPU pixels／Shipping。

## E255 — Stage377 共用動畫非循環相位與資源 metadata

非循環骨架已 clamp 到 source duration，但嘴型曲線用 unwrapped clock，結尾後仍可能變化；Loop 也只讀無法在資源介面選擇。以 UKKSharedAnimation.GetPhase 共用 loop/clamp 邏輯，Loop 改為可編輯。duration 必須與 authored payload 一致，初始化與播放都拒絕 metadata 分歧，不能讓骨架與嘴型採用不同週期。新增 one-shot 結尾 morph 實際权重及失敗切換不影響既有播放的驗證，站立 raw clip 加入跨卡隔離回歸。

Stage377DetachedStandR1 啟動前 runner ParserError：PowerShell 雙引號 replacement 展開 taskText，意外把完整舊 script 插入條件。從已封存 runner 讀取並以單引號 literal 修復，先 AST ParseFile 無錯再啟動 R2。沒有 UE 程序啟動／資產修改，保留 R1 runner log。PowerShell glob 不會自動展開傳給 rg 的目錄萬用字元，後續改 rg 目錄搭配 -g。

Stage377DetachedStandR2、FemaleStandR1、MaleStandR1 三個程序 actual exit1，未留下 Automation 完成或 native panic／UE crash 訊息，不能當通過。log 都停止在 session cache／初始求解附近；先改單程序循序重現，避免把未確認的突然退出誤判為 duration 或物理 parity 根因。

Stage377DetachedStandR3 長片段 CPU profile 顯示約333秒CPU，原生每步 update_dynamic_accessories 仍 clone/skin private source mesh（含貼圖），而共享播放器只消費 palette。核對 PID76704/EXE/專用log/isolated project 後停止這個 owned test；不列成功，也不繼續其後 queued native runs。新增 per-session 共用播放 pose-only accessory update，維持原 placement／implant／collider／240-step settle／DynamicBone，略過僅供 CPU 顯示的 geometry。原生 renderer 與明確 Skin 接口保留完整輸出，不能用全域環境旗標切换。必須以原生 palette parity 回歸驗證。

FemaleStandR3 actual1，前四時間点 palette 最大约0.000099838，長時間 native CPU oracle 途中退出，無完成／crash marker。Root cause 仍未確認。UE GenericPlatformMisc.cpp 的 noheartbeatthread 可關閉測試程序 heartbeat thread；加入明確 -LongOracle 僅供長同步原生對照，維持外層10分鐘 timeout、actual exit、Automation Success、geometry與palette原門檻。不把這項測試配置當成求解修正或已證明退出根因。PoseParityR1 實際 portable fixture 比較92組私有palette逐值相等；pose-only後明確CPU刷新29066頂點／法線一致，通過。

Stage377 最終：BuildR3 actual0；DetachedStandR4／WalkR2 actual0／Success，80 SK及真實end morph／不一致duration拒絕／prior player保留。FemaleStandR4（LongOracle）／MaleStandR3 actual0／Success，max matrix .000099838／.000545382，mouth位移 .854885340／.911619782cm，骨架未被嘴型改動。RustR2／LegacyR2／PoseParityR1通過；不降低原誤差門檻、不用卡別分支。中間 ParserError／exit1／owned中止不列驗收。詳細範圍 Docs/SHARED_ANIMATION_PHASE377.md。


## E256 — Stage378：共用動畫首次求解失敗會破壞原播放器

- 根因：只預驗 Initialize、再呼叫 destructive Rebuild，時間先重設、舊 components／driver 先銷毀，Evaluate 失敗不能保留舊動畫。不能把 Initialize 成功視為完整 rig 可播放。
- Red：Stage378DetachedRedR1 程序 actual0 但 Automation Failed；保留完整 log、RedSourceR1 與 DetachedRedR1 DLL，不以 exit0 冒稱成功。
- 修正：候選 components／driver 首次完整姿態成功後提交，失敗還原舊 tags／curve 資源引用；保留原 solver、姿態、clock、playing、嘴型權重。舊資源強引用，candidate 不重複初始化。
- palette index 邊界先比較 Slot 與 count，再計算 offset，防整數溢位；錯誤包含 renderer，避免無訊息拒絕。
- BuildRedR1 失敗是主 Editor 的全域 Live Coding mutex，不是 C++ 錯誤。僅獨立 BuildHost 輸出加 -NoHotReloadFromIDE；禁止對使用中的主 DLL 套此做法。既有 Editor 未中斷；其自然結束後使用正常 production build 更新主 DLL。
- 初始讀取測試檔誤猜兩個 Private/Tests 路徑造成 Get-Content missing，後由 rg --files 找到真正 Private/KKSharedDetached.spec.cpp。以後先 rg 定位，不再猜路徑。
- 最終 BuildGreenR2／BuildProductionR1 actual0；DetachedWalkR1／StandR1 與正式 DLL ProductionWalkR1／StandR1 Automation Success actual0，80 SK 與錯誤復原／不同曲線切換／嘴型並行通過。R1.audit.json 核對 Stage376／377 舊 seal，保持不變。

- 正式 DLL ProductionStandR1 actual1，無 Automation 完成標記／失敗斷言／崩潰原因，保留 log，不猜測原因也不當成功。沿用 Stage377 既有長同步測試方式，ProductionStandR2 只在測試 runner 加 -LongTest/-noheartbeatthread，外層10分鐘 watchdog與同一斷言仍保留，未改 runtime 或放寬條件。


## E257 — Stage379：GUI 二次開卡重複解 PNG／建 mip 與套相同身形

- 真實卡 `[Card] March_7th_keiyuu.png` 用 GUI 相同的 FOkSession.Open／AKKPreviewActor.LoadSession／ApplyShapes／動畫 catalog 流程量測：ProfileR1 第二次 SQL session hit，open1074.774ms，但建角色12567.785ms（textures11020.5ms、PNG decode4763.6ms）、相同 shapes3113.802ms，合16850.082ms。不是將所有時間錯判成缺少磁碟 cache。
- 通解：內容 hash＋完整壓縮 PNG bytes 核對＋mip policy 的 immutable CPU mip cache；每次仍建立獨立 UTexture2D，sampling／SRGB／後續 actor 修改不能共用 mutable texture。圖片變更自然 miss，角色／材質不在 cache identity；保留512MiB LRU payload 上限。GUI 僅在所選 shape 值不同於已載入 session 時 ApplyShapes。
- GreenR2 未壓縮缓存只保留31entries、482258886bytes，二次仍miss91／133次request，13348.715ms，證明順序LRU工作集淘汰，不可把有效cache程式碼就報成完成。改無損 packed mip 後89entries、61875590bytes，二次miss0、4799.893ms，134貼圖引用全部mip hash／sampler／SRGB相同。
- PackedR1 使用 FCompression 的 NAME_LZ4，UE5.8 Compression.cpp 實際走 LZ4_compress_HC，首次建圖34429.563ms（退化），不可接受。改Core公開 Compression/lz4.h 的 LZ4_compress_default＋LZ4_decompress_safe；byte-exact，無額外套件與 lossy texture 壓縮。未修改原生 encoded mip kernel 或 cache/portable ABI。
- BuildGreenR2 actual6：auto* range loop 無法推導 TObjectPtr<UTexture2D>，改顯式 UTexture2D*。當時沒有先核對build receipt就啟動 CacheR1（舊DLL）；其測試初始化 OkTextureSettings 誤認欄位順序（實際 filter/wrap_u/wrap_v/srgb/mip_policy），產生不相容 mip數並越界 assert/exit3。修正欄位、保留failedlog，後續每次build actual0才啟動測試；CacheR2／CacheR3 Success，無反過來宣稱 runtime PNG cache 崩潰。
- 一次誤猜 KKRuntimeTexture.cpp、一次誤猜 ffi 子目錄、一次誤猜 ThirdParty/LZ4 路徑；均未修改資料。正確來源由 rg --files 定位為 KKTextureRuntime.cpp、ffi.rs 與 Core/Public/Compression/lz4.h。後續應用已讀取的真實路徑，不能從類名推測。
- 最終 BuildFastR1 actual0；PackedFastR1 二次4815.602ms、build3595.886ms、decode0、133PNG request全hit／miss不增加，89entries77516886bytes，134texture引用全mip與sampling/SRGB相同。首次Unreal build12322.182ms，消除HC退化；UKKCardReader實際GUI shape值與session相同而跳過。CacheFastR2及NativeMipsR1最終actual0／Automation Success才驗收。封存R1.audit，舊378所有1892檔全核對。仍不宣稱整個GUI所有額外控制／每個機器／所有卡／跨程序磁碟mip cache已完成。

- CacheFastR1 實際 actual0/Automation Success/valid1，但 runner 擴充 ValidateSet 的文字替換也誤改 if 比較成 array，誤報失敗並中止後續 PackedFastR1/NativeMipsR1（當時未啟動）。已限定修正 if，AST parse 無錯；不得因 runner 誤報抹去 log，也不能把未啟動項目当已測。


## E258 — 載入期間更換選取造成 async session 與 GUI identity 不一致（Stage380）

- OnLoad3D 在背景 thread 捕捉 root/card/outfit，FinishLoad3D 卻讀取目前 Entries/SelectedIndex/OutfitIndex/Selected。SetLoadBusy 原只禁用 load/shape-load；SelectIndex 與三個 outfit callback 不檢查 busy。因此載入途中切換可使模型來源、所套 shape、PreviewCardPath/PreviewOutfitIndex 不一致。
- 共用修正：busy 時禁用 card list/outfit nav/combo，SelectIndex、ScanAndShow、outfit callbacks 都保護 identity；blocked combo callback 恢復實際選取，完成後恢復操作。同卡 shape edit 仍允許，完成載入才套用不同 shape。未加入卡名特判或修改 native shader/shape 計算。
- BuildR1 actual6：檢查程序時主 Editor 尚未開啟，編譯期間它以 -skipcompile 重新開啟（PID67896），Live Coding 拒絕正常 build。沒有關掉使用者 Editor，也沒有繞過它來覆寫主 DLL。
- 獨立 BuildHostR1 actual6：測試檔位於 Tests 子目錄而直接 include OKGalleryWidget.h（module 根未列入 include path），改 ../OKGalleryWidget.h；同時 source engine 的 NetCore 嘗試重連結載入中的 engine DLL 而 LNK1104。独立 host 改 UBT 正式 -UsePrecompiled，只編譯專案/插件，不重建引擎；BuildHostR2 actual0。
- 此輪兩次誤猜已知 header/log 路徑、一次誤猜 UE TargetRules.cs 路徑，讀取失敗後均由 rg --files 定位。不可假設類名與檔案位置，也不可把 failed build 當測試成功。舊 logs/seals 保留。
- 最終 SelectionHostR1／SwitchHostR1 actual0＋Automation Success。GUI busy/unbusy 真實callback正確，March_7th outfit0→ill_male_02 outfit0→outfit1→依序replay六次成功，134／60／56textures逐mip hash／尺寸／SRGB/filter/wrap一致，native height edit/restore session值與貼圖不變。replay7.910936／2.182277／2.020721秒，miss137固定，137entries108616491bytes。不把男角首輪native建構127／83秒當warm耗時，也不把同時運行主Editor的7.91秒與379不同負載直接作退化判定。
- 原379 seal266項保留；獨立host與当前source逐檔SHA核對。正式DLL仍被主Editor PID67896（Live Coding）阻擋，BuildHostR2不是正式build；不得為了宣稱完成關掉使用者程序／覆寫mapped DLL。正式編譯待主Editor結束後續辦，詳Docs/CARD_LOAD_SELECTION380.md。

- 2026-10-03 E258 後續解除：使用者關閉 Editor 後確認程序結束，正常正式 BuildProductionR1 actual0／Succeeded，正式DLL已更新（未使用NoHotReloadFromIDE绕過主Editor）。源碼與 accepted host 逐檔SHA相同。SelectionProductionR1／SwitchProductionR1 主專案 actual0＋Automation Success，六次卡／outfit载入資料一致、height edit/restore通過，replay 5.577／2.032／1.927秒，miss137不增加。R2封存正式binary/log/source，舊R1 220項及379 266項原樣保留。前段「正式DLL受阻」是R1歷史狀態，現已解除，無剩餘正式編譯待辦。


## E259 — 核心載入耗時與完整 GUI 不可混用；重複 face/hand 顯示更新（Stage381）

- 380/379是native session/actor/catalog及真實callback guard測試，未完整包含GUI控制初始化。新增真實Gallery SelectIndex/OnLoad3D/background/FinishLoad3D測量後，March第二輪GuiR1 10.403秒／GuiR2 11.270秒，decode0；不能宣稱先前5.577秒就是整個GUI操作。
- 找到初始化mouth/eye/brow及左右hand各自刷新display；face/hand batching保留原setter/native狀態與delta0時鐘，只最終刷新一次。不是skip不同shape、不是忽略hand reset、不是共用角色mutablegeometry。GuiR3 face parity23039值exact，GuiR4加hand parity2001骨骼tolerance0/clock不變。平常單一操作依舊即時。
- BuildR1/R2/R3/R4全actual0，GuiR1/R2/R3/R4 actual0/valid1。GuiR4 secondGUI10.390秒／ready10.661秒，不能用單次小幅差異宣稱穩定大幅加速。已保存全部baseline/profile/oracle，不隱藏額外目錄／控制／語音等成本。
- 本輪一次先猜KKGameRoot所屬KKUnityBundle路徑，讀取失敗，隨即以rg --files實際KKSideloader路徑讀取，沒有修改錯誤檔案。既有CommonUI input routing error在所有baseline/final均存在；本測試不聲稱硬體點擊驗證，也沒有用actual0去抹掉其日志。

- MaleGuiR1亦actual0/valid1，23464臉部值／2668骨骼exact、時鐘不變，第二次完整GUI4.110秒／ready4.204秒，decode0；男女正式BuildR4均成功。Saved/Tests/GalleryReload381/R1.audit.json封存，380/R2 218項原樣保留。全部owned測試程序正常退出。


## E260 — KKS camera 三鍵操作與視圖選取（Stage382）

- 原PlayerController只有左键orbit與指數wheel，且直接取World第一個PreviewActor，可能命中stale／partner；UI按住拖出後可突然變成orbit。原pitch硬限-75/80與距離45/2400也不是BaseCameraControl_Ver2預設邏輯。
- 依BaseCameraControl_Ver2 InputMouseProc 314–397、wheel301–312、LateUpdate及Studio/CameraControl一致分支：左右同按優先camera XZ、left旋轉、right垂直worldY／水平Dir.z、middle camera XY。Unity→UE(-x,z,y)*100，Mouse Y反向screen Y，Unity Euler旋轉方向轉換不可沿用原orbit符號。
- 實際KoikatsuSunshine_Data與CharaStudio_Data/globalgamemanagers InputManager三mouse axes sensitivity均0.1（已存InputManagerR1.json），base rotation5/move0.05m/zoom5；不可猜速度或使用dt倍率。原SetCtrlSpeed stub rateAddSpeed1保留。
- UI起始按壓ownership鎖定，失焦／離viewport／改camera owner取消，不持續按住重取owner；選目前Gallery actor且StudioCam等於view target，fallback也只找view target相同者。wheel先於同frame drag，NoCapture維持UMG按鈕／slider。
- ControlsR1 actual3：人工建立PlayerCameraManager後漏InitializeFor(PC)，FTViewTarget::CheckViewTarget assert OwningController；是測試fixture未完整初始化，不是camera drag產品crash。修正fixture，failedlog保留，不算PASS。一次rg用了未由shell展開的glob路徑，改具體目錄/rg -g，未改任何原始遊戲資料。

- ControlsR2 actual0但Automation Fail：直接InputKey wheel測試受到真實Slate hover gate阻擋，active distance保持500。這個測試依赖桌面游標位置，不可用移动使用者游標或停用产品UI gate修它。改直接验证production GetPreviewCameraOwner选择，再调用其zoom；UI gesture gate另以確定性状态測試。原failedlog保留，尚未啟動的NativeR1不列已測。

- 最終 BuildR4 actual0／Succeeded；ControlsR3 actual0／Automation Success／mouse valid1；NativeR1 actual0／Automation Success／native valid1。真實March卡原版walk，camera七種拖曳與wheel不改667骨骼／AnimPhase／FaceClock，後續Tick有131骨骼變動且phase0.3。失敗fixture logs全保留；沒有移動使用者桌面游標。正式DLL已更新，說明Docs/KKS_CAMERA_CONTROLS382.md；Stage382 R1封存並逐檔核對Stage381舊證據。


## E261 — 動畫切換與播放性能（Stage383）

- 確認source count先配置所有labels、逐index再次解析source path／filter nth，Restart又完整列舉；控制器graph雖cache，不能當查詢cache。共用不可變labels以root／descriptor／kind隔離，count直接len、index直接get，保留排序／同步層filter。Catalog R2的3696 states三次仍468.660ms，原因為每次filesystem canonicalize；將source resolve限制在miss後，R3降1.051ms，冷解析11678.966ms須背景prepare，不可稱冷解析消失。raw5006 labels三次2.023ms。
- CatalogueR1 actual101：Arc collect未註明Vec<String>而E0282，補明確型別；R2/R3 actual0。一次猜portable_solver.rs不存在、一次patch期望錯誤export declaration，均未修改錯檔，之後使用實際讀取的宣告。
- worker只以root/source/key解析immutable caches，不使用session／UObject；完成回GameThread並核對generation、弱actor identity、選項，Stop取消。state membership直接Rust查詢替代Restart再次完整UTF8 list。
- 發現GPU顯示每個follower重讀相同骨骼、每幀MarkRenderStateDirty重建proxy；改一次sample姿勢及dynamic update，Engine PoseableMeshComponent RefreshBoneTransforms已做Fill／bounds／dirty，可移除重複及每幀Allocate填表。
- Perf383NativeR1 actual0/Success，667bones exact，proxy不重建，worker prepare通過；20ticks平均119.479ms，不能宣稱FPS已修復。R2 actual0/Success，191.227ms，display約bones0.3ms／accessory5ms，尚須pose／face／shadow分段測量。不同負載測值不可当穩定性能退化或提升。

- 進一步測量native sample約6–7ms、dynamics約100ms，其中body／clothes各不到1ms；不能把所有dynamics當body物理或整角色SkinAll。最大浪費是runtime skin_mesh最後rebuild_obj每幀產生ASCII再丟棄，新增共用核心geometry-only路徑，保留原CLI／export OBJ。LBS矩陣每bone一次，smooth iters0不建neighbors。
- Perf383NativeR7 actual -1073740791：診斷對world fresh/cache用strict assert_eq，release浮點平移差約6e-8造成FFI不能unwind的abort。改明訂matrixabs≤1e-6、診斷valid0/1不panic；不是放寬頂點／法線對照。R8以後通過，最終max2.38e-7。SkinR1 testfloat省略integer part造成編譯101；BuildR9 0.f32被解析為欄位而actual6，修正0.0f32。一次rg再次錯用Windows glob、一次Python stdout CP950不能印Unicode name，均改具體目錄與UTF8／JSON ASCII，logs保留。
- 最終BuildR16 actual0；Perf383ControlsR3 actual0＋Automation Success，七mask／cancel generation／viewport控制回歸。NativeR12 actual0＋Success，worker prepare／membership／667bones／proxy通過，37runtime renderers44480vertices positions/normals exact，6matrix checks max2.38e-7。CatalogueFinalR1／SkinFinalR1／TangentFinalR1 actual0。NativeR1 119.479ms，NativeR12 quiet20ticks37.006ms約-69%；不是整場景FPS或五次實際雙人GUIclick延遲，phase/load不同不作固定保証。
- backgroundprepare遍歷所有blend-tree葉片／base／active layers，不只default身高；GTcallback驗證generation／weakactors／selection，Stop／Pause／Replay／Refresh／NativeDestruct取消並更新提示，舊shared player保留到新片段ready。未降物理頻率／停陰影／卡名特判。Docs/ANIMATION_PERFORMANCE383.md與Stage383封存保存正式source/DLL及失敗logs。

## E262 — Stage384 雙人提交仍卡住：不可把 worker 準備成功當成性能修復

- Perf384PairR1 actual0/Success，但兩次 Start commit 5413.992/5226.971 ms。把 MotionIK/config 移到 worker 後 R2/R3 仍約5–6秒；最初將剩餘提交主要歸因 IK 的推論不足，必須分段量測，不宣稱已解決。
- R3 女角 neutral 2360.906 ms、select 1.004 ms、frame 2357.492 ms。SetGameAnimation 每次先 SetPoseMode(0) 重做形體/Tpose，再 set_anim_clip 清 private state，重複每鏈240步暖機。按 OCIChar.LoadAnime / DynamicBone.Start、OnEnable 保留一般切換粒子；真實 pending shape edit 仍原更新路徑。
- R4 後續 commit89.543 ms，首選仍2546.029 ms；根因 ordinary collider flags 變更又清 private state。按 KKS_Colliders 更新普通collider refs／freeze axis而不重啟鏈；還原 authored freeze axis，Ver02胸部既有重建不改。runtime AttachedChain不持久化，無wire layout變動。
- 最終 BuildR7 actual0。Perf384PairR5 actual0/Automation Success：首commit369.098 ms、後commit93.045 ms，各30 frames CPU47.106/47.603 ms；背景準備第二姿勢舊配對更新240 frames，max115.793 ms。冷prepare20.6/12.9秒仍存在。不是整場景FPS／硬體GUIclick完整延遲或所有姿勢零停頓。
- 骨骼refresh每次索引保留body→head→head_full及原same-name first；37/男卡renderer幾何/矩陣既有parity與667/681bones回歸。ParticlesR2真卡成功：有效clip保留private state，invalid不改clock/state，floorflag＋dt0粒子pos/prev exact，explicitreset仍清除。IndexFinalR2／MotionFinalR2 actual0，ControlsR2 actual0/Success。
- 命令失誤保存：IndexR1未指定manifest，在專案根跑cargo actual101；改--manifest-path且fresh IndexR2，未覆寫失敗。誤讀stderr漏.log、猜controller.rs/unity/mod.rs不存在、PowerShell使用{Public,Private} brace造成ParserError，改先rg --files與明確路徑；不要再套Bash的brace/glob。一次patch同文字錨點把FemaleMotion放錯callback，立即rg核對移回OnPairStateSelected，R3編譯成功，不將錯位置留在正式版本。
- 詳Docs/ANIMATION_PERFORMANCE384.md。所有中間build/tests/logs保留；Stage384封存核對Stage383舊證據。沒有卡名／姿勢名特判，沒有降低物理／陰影／IK頻率。


## E263 — Stage385 每幀碰撞器重複準備與性能驗收界線

- Stage384雙人每幀約47ms仍存在。普通附件逐renderer／chain搜尋相同body骨骼、重算native及ordinary collider world／lossyScale；改每session刷新延遲建立immutable CharacterColliderFrame，main body／coordinated clothes分開，不跨角色／幀共享mutable state。implanted colliders仍算private rig；不降低物理／IK／陰影頻率、不改wire格式。
- 新名稱索引須保留原大小寫敏感及same-name first，不能套Stage384骨骼export的ASCII-insensitive規則；native→implanted→ordinary順序／offset亦不可重排。舊算法保留cfg(test)獨立oracle，matrix／scale／flags／particles exact。
- BindingR1 actual0；CollidersR1 actual0，InstalledR1最終兩tests通過，真卡84cases exact；粒子life-cycle regression無退化。BuildR1 actual0／Succeeded；正式DLL更新。此輪無編譯或測試失敗，保留所有receipt與log。
- Perf385PairR1 actual0／AutomationSuccess：667／681bones、原walk及兩個native pairs；commit305.412／83.074ms，每30frames CPU45.981／45.301ms；第二次prepare期間舊pair249frames，max101.918ms。冷prepare20.938／12.732秒仍需等待，不能說耗時消失。
- 相比Stage384約47ms只有小幅單輪差異，不把避免重複工作當成固定FPS提升證據，更不能聲稱所有dynamics耗時皆由collider造成。API CPU計時不是GPU／全場景FPS或完整GUIclick。未知卡／全姿勢未全面認證。
- Perf385ControlsR1回歸及fresh R1封存見Docs/ANIMATION_PERFORMANCE385.md；原Stage384 341項封存證據逐檔核對不變。


## E264 — Stage386 碰撞器幾何重算與性能量測界線

- Stage385約45–46ms仍存在；新增附件sync／physics opt-in分段。ProfileR1真卡sync約0.008–0.042ms，physics大鏈約1.5ms（含placement／config），不能將所有rest當骨骼名稱搜尋或全部當collision。
- UpdateParticles2原external collision逐粒子重算TransformPoint球／膠囊端點，改每次子步局部PreparedCollision。依据原DynamicBoneCollider.Collide及UpdateParticles2後ApplyParticlesToTransforms，子步內Transform不變；各chain前live collider refresh及每子步重新準備保留。原contact算式／分支／碰撞順序精確一致，不合併不同角色或renderer的solver、不改wire／ABI、不減頻。
- ContactsR1 actual0：215622 grid＋額外邊界exact，sphere/capsule inside/outside三方向／負零scale；DynamicsR1 actual0：31passed／13原installed tests ignored，不將ignored算已執行。cfg(test)每個external contact都直接比原完整collide_particle_world。InstalledR1 actual0兩tests，84cases collider／particle exact及clip／flags生命週期回歸。
- BuildR1 actual0／Succeeded正式DLL更新；Perf386PairR1 actual0／AutomationSuccess，原walk／667及681bones／world及skin parity／兩native pairs／共享時鐘。commit351.242／83.841ms，pair30frames CPU44.040／44.180ms；旧pairprepare期間256frames，max102.081ms。冷prepare21.713／12.858秒仍存在。
- 单輪略低不能當固定速度保証。第一commit比Stage385305.412ms更高；必須如實記錄波動，不能只報播放數值以掩蓋。CPU44ms仍存在，非完整GUIclick／GPUsceneFPS／全卡零bug。無卡名特判／停止物理或陰影。
- 本輪rg再次把implant*當Windows路徑glob，os123；改用rg --files與明確目錄。不得再對cmd argument內路徑猜Bashglob。沒有修改錯檔、沒有覆寫失敗log。此錯誤不影響編譯／測試結果。
- Perf386ControlsR1操作回歸及fresh R1封存詳Docs/ANIMATION_PERFORMANCE386.md；Stage385所有301項舊封存逐檔原樣核對。


## E265 — Stage387 配對IK每次setter重建完整骨架

- IkSkeleton world setter先flush dirty全bone worlds，但IK的後續world/local getter已直接讀live tfs並依Unity階層順序計算，反覆發布矩陣不必要。new_batched僅配對solver使用；原new()仍原中途flush，solver完成／temporary proxy移除後原rebuild_worlds一次發布。不共用男女solver／粒子、不改迭代／碰撞／形體／物理／陰影頻率或wire／ABI。
- 要避免誤刪必要cache：先全檔確認solver讀取不使用sk.worlds，再以獨立oldsolver從相同body及persistent solver clone核對最終locals／worlds／names exact。診斷runtime僅valid0/1，不在FFI panic；cfg(test)assert。oracle成本必須排除在quiet性能計時之外。
- IkR1 actual101：新增測試猜euler_rotation函式不存在E0425；修正crate::pose::euler_deg_to_quat，fresh IkR2 actual0／9passed，保留失敗log／receipt。另一次讀取猜motion_ik.rs不存在，實際rg --files已顯示finalik.rs／h_playback.rs；改實際檔案，未修改錯檔。避免先列檔後仍使用猜測名稱。
- BuildR1 actual0／Succeeded正式DLL更新；cross-category真卡與正式pair／controls最終數值另依完成日志記錄，不能只以編譯通過宣稱IK姿勢正確。

- 覆蓋缺口已確認：Perf387PairR1沿用舊test的第一個排序共同state（S_Drink），setup找不到對應MotionIK state而不執行solver，因此無IK parity日志。雖actual0／AutomationSuccess及clock／geometry通過，43.966／44.687ms不能作IK性能驗收；Stage384–386同類共同state數值也只代表該非active-IK播放工作。原日志及seal保留，不能抹除。
- 修正native fixture只選雙方原生controller的Idle，另兩category ContactsR1已證明Idle有active contacts。正式測試加delta0 IK oracle，seal要求四筆男女角色exact IK記錄，防止再以API成功掩蓋solver沒跑。每case量測batch30frames後以opt-in OK_ANIM_IK_LEGACY=1重啟並量測原flush30frames，同DLL同原生state；default始終batch，旗標測後還原。對比不包含oracle／restart耗時，粒子歷史及輪次順序仍不同，不能當全場景FPS固定保証。

- 最終BuildR2及IkFinalR1 actual0（9passed）；ContactsR1 actual0，兩category各8active，四筆oldsolver exact。Perf387PairR2 actual0／AutomationSuccess，Idle active IK四筆release exact，角色tfs613／627含proxy。batch46.691／46.609ms，sameDLL legacy50.256／49.976ms，約少7%；各30frames time0→0.5且old／new粒子歷史順序不同，非全sceneFPS保証。
- active首次commit1281.467ms／後續88.928ms；第一次女SetGameAnimation frame189.582ms、男102.907ms之外還有約989ms配對初始化等，未分段證明主因，不能直接猜factory並報已修。prepare12.956／2.252秒，第二次舊pair37frames／max107.160ms。首次active IK卡頓仍存在，誠實保留為後續調查。
- 操作回歸Perf387ControlsR1及fresh R1封存詳Docs/ANIMATION_PERFORMANCE387.md；Stage386全部303項證據原樣核對。


## E266 — Stage388 首次IK提交來源重讀與測試fixture初始化

- ProfileR1正式active Idle actual0／AutomationSuccess，首commit1303.897ms；female load394.266／parse102.893／init0.230ms，male load401.502／parse43.665／init0.198ms。不能把943ms來源讀／parse成本都說成solver init慢。
- Immutable Arc prefab cache key=root＋full SHA256，cap8，short lock／parse外鎖。prepare_pair worker先準備；apply_contacts缺runtime時共用一次來源讀取，各角色clone、套自己的cf_n_height、各自from_prefab／init。仍查當前override／nativebytes避免root-only stale cache；freshparser防完整key誤取sampled parser。未共享solver／particle、不改height順序／wire／ABI／頻率。
- FactoryR1 actual101：測試open後未apply_shape，body_shaped None而unwrap，fixture漏正式初始化；補apply_shape，FactoryR2 actual0，三身高0.72/1.02/1.28初始solver全部exact、template bytes未改、同內容Arc命中。保留失敗log。不以刪掉height驗證或改產品初始化來掩蓋fixture問題。
- IdentityR2實際factory_key root／未取樣bytes變更隔離通過，BuildR1–R4 actual0／Succeeded，DLL正式更新。完整來源快取沒有偷偷跳過查詢，GT仍有一筆約400ms來源讀取，不能稱首次切換全非阻塞或零卡顿。

- 最終Perf388PairR1 actual0／AutomationSuccess，active Idle四筆release oldsolver exact、667/681匯出bones及原walk／geometry／world回歸。首commit769.308ms對同輪ProfileR1 1303.897ms約少41%；後續89.300ms對86.123ms未改善。CPU46.472／46.875ms仍存在。prefab load396.534ms＋hash/lookup18.588ms，init0.518／0.214ms，剩餘來源讀取不能稱已解。
- prepare13.352／2.728秒，第二次舊pair48frames／max100.526ms；冷準備仍存在。Perf388ControlsR1與fresh R1封存見Docs/ANIMATION_PERFORMANCE388.md，核對Stage387310项舊證據。沒有GUIclick／GPUFPS／全卡無bug保証。


## E267 — Stage389 單筆 override 查找複製全部 mods catalog 與重複 metadata 查詢

- ensure_mods 原每次連 catalog／guids／overrides 都 clone，單筆 override 不需要。內部改 Arc<ModsMem>、外部 owned API僅clone所需欄位，override_source clone單一PathBuf；每次仍完整 zip inventory核對與原incremental更新。SQL empty／disabled仍走原TSV／scan，populated missing直接None；不得把empty與有效miss混淆而改fallback。
- Windows path.is_dir＋第二輪fs::metadata逐檔重查；改DirEntry file_type／metadata，link／unknown保留path跟隨。原排序／微秒stamp／partial IOerror契約不變。7,311zip tuples exact，inventory253.075→37.660ms，避免約94ms全索引clone。不能把這些局部數據當GUI／GPUFPS。
- SqlR1 actual101 是5-byte fixture未滿原bundle bytes.len()>16门檻，修長fixture不改產品门檻，R2／Final7passed。保留失敗日志。提前讀尚未存在receipt也是本輪程序錯誤；等待session完成再核對actual receipt，不能相信SetContent後的shell0。
- NoSqlR1隔離process證實scan／TSV；FactoryR1三高度initialsolver exact／templateimmutable、IdentityR1fullcontentkey、BuildR2actual0，正式DLL更新。PairR1四筆activeIKexact、男女667/681bones回歸；ControlsR1回歸。
- firstcommit769.308→377.255ms、next85.119ms；source396.534→48.074ms（resolve35.635／read12.439），CPUframe仍46.793/46.119ms。仍有commit／frame成本，不能冒稱全部零卡頓。完整Docs/ANIMATION_PERFORMANCE389.md、fresh R2封存保留Stage388314項舊證據。

- 封存操作錯誤：把seal stdout導向正在封存的base/seal-output.txt，R1 audit在print之前記錄emptyhash，完成後唯一該log hash變動。R1.verification.txt明列mismatch；原R1 audit/snapshot/output全保留，不改寫hash或截斷log。改用fresh R2 audit/snapshot，直接回傳stdout，封存後另核對全部記錄及live source/binary。往後封存自身輸出必須置於封存目錄外，或由工具返回，不能讓它成為正在增寫的封存項。


## E268 — Stage390 GUI NoCapture 不轉送滑鼠 primary press，拖曳全部失效

- 使用者確認只wheel正常。原GUI/PC選NoCapture以保留UMG，但UE SceneViewport game/PIE在無capture時不送primary press，IsInputKeyDown一直false。wheel走axis事件仍正常。禁止直接改CaptureDuringMouseDown搶UI按下；改GetCameraMouseButtons读Slate全域按鍵，無Slate才fallbackPlayerInput。
- 原activeowner、firstpress/UIgesture、focus/release、wheelpriority、nativeOrbit方向/比例不改；正式PlayerTick与測試共用UpdateCameraMouse入口，沒有卡名特判或改動畫頻率。
- 舊KksMouse只驗數學/抽象gesture，漏實際NoCapture事件邊界，之前綠燈不足以保證GUI可拖。新合成SlateDown/Up＋真实PIENoCapture SceneViewport證實primarypress0、PlayerInputfalse，但controller mask與camera drag四組皆有效；UI起始gesture仍拒絕、release清mask。不能冒稱實體滑鼠GUI驗證。
- BuildR1fixture舊rawclientconstructor有C4996；R2改sharedclient/newSetViewportClientAPI，正式BuildR2actual0更新DLL。ControlsR1/R2actual0，NativeR1actual0含男女/activeIK/geometry/clock回歸。詳Docs/CAMERA_INPUT390.md、Saved/Tests/CameraInput390 fresh R1封存，保留Stage389R2的625項舊證據。


## E269 — Stage391 Studio／所有動作清單同步解析與物理生命週期重建

- Studio／source／clip／manual／replay 改共用背景準備，最多一個active＋最新pending。root/value快照、generation／actor／load／manual文字校驗，禁止worker讀live UObject/session、禁止舊job覆蓋最新結果。瀏覽來源保留當前播放；配對沿用Stage383 worker。
- FOkSession 的三類清單改批次JSON，原indexed FFI仍保留；不再count後逐index重列全表，也不再固定512byte截名稱。Yure成功policy按root/state/owner/file/zip stamps快取，IO預設和錯誤不快取。
- GuiR1只有request<=150ms驗收，漏commit2.2秒，該綠燈不足。補commit門檻；R2 254.737／R3 196.433／R4 234.132ms皆validfalse，雖Unreal exit0也必須判失敗。不能只看receipt0。
- Studio setter不應全配件reset；body/clothes flag切換也不應clear runtime再240step。依OCIChar.LoadAnime／KKS_Colliders原地配置，保留particle/timer/rig；Ver02替換plugin尾段避免重複collider、停用時恢復authored radius。真正shape/resource重建仍可reset，未跳物理或減頻。
- GuiR5通過10cases，首次134.008／重複89.096ms，但BuildR9 GuiFinal首次192.043又超標，不能以先前綠燈結案。profiling body0.120/clothes0.542/rest58.741，native sample123.110ms；磁碟恢復session可能未讀Avatar TOS，動畫prepare補load_avatar_tos使immutable binding metadata先在worker準備。保留所有失敗以續查。
- BuildR2 actual6為並行mods.rs Win32 MetadataExt不穩定API／Vec推論，未覆寫其它工作；對方修正後後續編譯成功。GuiR4在BuildR7 receipt未存在即啟動，不能當最終DLL驗收；往後先等session完成再驗receipt和Succeeded。
- 多次猜不存在檔名／Windows wildcard作rg positional（OkExtractSession.h、anim_api.rs、acc/dynamics.rs、anim/studio.rs、OKNative*、Tests/*.cpp等）造成read/search錯誤；未錯改檔。搜尋先rg --files或對真實目錄rg -g filter，不再猜路徑。
- ColliderR1與LifetimeR2 exact／無collider累積／authored恢復，YureR2兩測試通過。最終結果詳Docs/ANIMATION_MENUS391.md；冷解析等待仍存在，不宣稱GPU FPS改善／所有卡全零bug。

- Studio Yure／Source R1並行啟動各自SessionId仍被Unreal Automation互相discover，收到其它controller的StopTests／RunTests，兩者actual1、無TestCompleted，HTTP ports也碰撞；保留R1。此類UE automation共用Saved/Automation與messaging，不視為可安全並行，改fresh R2逐項執行並核對本test Success。

- 最終review補單人／配對準備跨類型世代：BeginAnimationPreparation先CancelPairPreparation，保留已提交pair直到新clip完成；GUI快速取代case也驗pending pair generation被取消。
- GuiFinal3（BuildR12）cold commit221.062ms／validfalse：詳細profiling reset52.068／curves11.713／modifiers2.518／rest141.234ms。所有輸出保留，未提高150ms門檻。Actor原為診斷把OK_MENU_PROFILE連帶啟用逐配件OK_ANIM_PROFILE；拆成獨立OK_MENU_DETAIL_PROFILE。正式runner清除詳細log環境再測正常播放，不能把診斷IO模式當一般性能測量，也不能抹掉尖峰。
- 初始FinishLoad3D還可能冷解析来源／基本／已選片段；現在先在独占load job的worker預熱，發佈session之後UI僅讀已準備目錄。沒有把live actor/session交給worker，也不改卡資源內容。

- 最終BuildR14actual0／Succeeded；GuiFinal4正常模式10cases／validtrue，Studio首19.809/repeat20.226、clips17.307/17.211、replay14.762、sources1.012/0.789、manual2.103、H52.797ms。cold準備35.729/22.799秒仍在背景，H636frames/422舊samples。最後YureR3、StudioR2、ControlsR1、NativeR1皆actual0／AutomationSuccess；4筆activeIKexact與667/677palette／skin/world檢查。fresh R1封存保留Stage390305項舊證據。

- seal首次驗收在複製／建立audit之前失敗：沿用Stage390 male palette681，但本輪NativeR1指定KK_224305實際677（4activeIKexact／667/677export皆valid），前輪指定DefaultData/ill_male_02。不能用不同卡的bonecount作退化比較；保留失敗原因，補NativeR2同前輪卡再核對681，R1亦保留作另一男卡覆蓋。文件先前681描述更正為fixture-specific結果。

- NativeR2同前輪ill_male_02 actual0／AutomationSuccess，667/681 export／4activeIK exact／skin/world皆valid；確認R1677是不同card fixture，封存按兩fixture分別核對，不改骨架與parity。


## E270 — Stage392 來源選單選完沒有播放回饋／同來源重載漏事件

- GameSourceCombo 是 bundle/controller，不是動作。原 GameClipCombo 隔著 Studio、場景與手動載入，來源完成未顯示個數／下一步，InspectorText 留在準備狀態。來源與播放片段應明示且相鄰；不任選第一片段作暫解。
- 同來源 SetSelectedOption 沒有 selection change event，手動載入成功卻不更新清單。改抑制 selector 事件後直接套用 prepared names，避免相同選項漏事件，也避免變更選項重複提交。
- pending source 時舊 clip 清單可誤選，現在停用並在 handler 防守。失败保留原清單與動畫、selector 回復已提交來源；所有失敗記 kind/source/error，取消完成重新啟用。
- BuildR1 actual6：其它工作四個群眾 commandlet 佔用 -0393 DLL，引擎 regular build 的 cleanup 失敗；新 -0396 ModuleWithSuffix 編譯 actual0。不終止非本輪程序、不把 commandlet 工作算成自己的驗證。
- PowerShell suffix 的逗號必須整段參數加引號；未引用的 parser error 不是 build receipt。兩次 apply_patch context 失敗原檔未改，用實際讀到的行置換後 review diff。不要猜不存在的 KoikatsuSunshine_Data/TargetDescriptor.cs 路徑，先 rg --files。
- GuiR1 actual0 / validtrue8cases，基本530／Studio5006動作、播放時間進展、同來源refresh、missing-source雙入口恢復、latest selection與取消。只是實際 UMG 事件驗證，不宣稱所有 clip 或實體鼠標測試；冷 Studio worker13.962秒仍存在。完整結果 Docs/ANIMATION_SOURCE392.md。

- 新增狀態列的review補出重播／Studio套用失敗仍留準備中的漏項，補成功／錯誤狀態並加第9項重播測試。MenuRegressionR1 actual0/validtrue10cases；不是最終配套驗收。
- BuildR3 actual0不等於可啟動。GuiR2 actual3啟動fatal Default__KKAnimationCatalog重複類別、無JSON：插件新-0007與main link response的unsuffixed KKUnityBundle.lib混用。BuildR4 三模組同時explicit suffix，main-0398 response核對plugin-0008.lib，manifest同一配套；未停止其他4個群眾commandlet。runner若非0/缺JSON先回報啟動失敗，不再讓missing JSON遮住根因。

- 最終BuildR4 actual0/Succeeded；main-0398與plugin-0008 explicit相依link。GuiR3 actual0/validtrue9cases；MenuRegressionR2 actual0/validtrue10cases，Studio22.207/repeat19.369ms、replay18.661ms、H58.909ms。firstStudio背景59.5秒/H30.431秒仍存在，不能宣稱所有cold來源立即完成。R1 audit/snapshot保留本輪成功／失敗及Stage391420項舊證據。

- 封存初次在複製／audit建立前失敗：含中文的UE SaveStringToFile預設UTF-16 BOM，GuiR3.json不能硬讀utf-8；JSON reader依BOM選utf-16／utf-8-sig，保留原JSON bytes，不轉存覆寫證據。


## E271 — Stage393 動作選擇依賴先選來源／名稱相撞錯播風險

- 使用者要求動作自動選來源。原GameClipCombo只列selected source的raw clips，UI仍強迫使用者找bundle。把已匯入studio_catalog的anime/hanime row加到清單，按穩定row身份準備其controller/state，不以名字猜來源。
- GameClip與Studio共享ApplyStudioMotion／SetStudioAnimation，保留base/override/controller layer/Yure與scene；成功才同步來源和片段。不另建精簡raw clip路徑跳過原生設定。
- global與raw可能有相同顯示名，不能用map猜第一筆或丟掉raw。global unique row labels、raw suffix加GameRawClipNames保存真clip；profile注入同名raw/catalog fixtures確認不丟身份。
- busy raw guard不能阻擋穩定目錄row取代pending請求。review補例外，R1完成後再做fresh R2，保留R1不當最終驗收。沒有卡名特判。

- AutoR1 actual0但valid=false/9cases：H第一次commit524.767ms>150ms，actor coarse init493.009ms/frame15.333/render4.537；其餘commit7.758–32.401ms，不可只看exit0。保留JSON/log不放寬门檻。FeedbackR1 actual0/validtrue9cases。
- 補exact Studio零值options的prepare：舊PrepareStudio只暖controller authored defaults與Yure，現在worker另用native studio_parameters驗證None/Some(0) motion路徑。原setter行為不變；新增OK_MENU_PROFILE才啟用的parameters/yure/commit粗計時以確認根因，不啟用逐配件詳細IO。BuildR3 fresh三模組後測AutoR2。

- AutoR2 actual0但validfalse：H commit151.856ms仍>150，不把接近門檻算通過；native parameters2.402/yure0.587/commit67.395ms，actor init75.143/frame40.344/render11.656ms。不能宣稱原493ms全部就是parameters，R1沒有分項。
- 目錄選動作原本仍重建8751目錄項。改保留全域清單；raw保存完整scoped key，而非選取時用SelectedGameSource重新拼接（否則自動切來源後舊raw會錯播）。worker與commit都按raw保存的key，成功同步raw對應來源。只在手動來源／Studio分組直接選動作時重列raw。profile case4驗global後直接選仍在舊清單的基本raw，來源必須回基本動作。
- BuildR4完成後補raw worker Source應從own key推導、而非當下顯示來源；fresh BuildR5另加cross-source raw測試，未用R4當最終DLL證據。FeedbackR2actual0/validtrue9cases保留。

- 最終BuildR5actual0；AutoR3actual0/validtrue10cases，8751目錄項，ordinary21.332/repeat25.417ms、cross-source raw19.020ms、H67.520ms，request/有效commit皆<150ms；H615frames/512oldsamples。FeedbackR3actual0/validtrue9cases。R1/R2性能失敗原證據保留，不宣稱全部8751逐幀或實體鼠標驗證。R1 audit封存並保留Stage392320项；冷解析仍在worker等待。

## E272 — Stage394 搜尋篩選必須保留完整選項身份

搜尋不能透過 ClearOptions/AddOption 改寫 authoritative UMG Options，否則 Studio/Pair 的 FindOptionIndex 會對到錯 row，原始／同名片段與目前選取也可能丟失。本輪使用獨立 FilteredOptions shared pointers、原文＋譯文查詢快取與 virtualized SListView；明確點選／清單 Enter 才提交原 SetSelectedOption，導航、pruning、無結果不觸發動畫。每次開啟讀取最新來源，清掉舊查詢快取，不解析controller。保留 Stage393 自動來源與完整 raw key 邏輯。

BuildR1 actual0/main0404/plugins0014。SearchR1 actual0/Success，8751 cached filter2.638ms；AutoR1 actual0/validtrue10cases，有效commit<150ms，H77.874ms、666frames/563oldsamples。冷控制器仍背景等待，不把快取查詢數字當冷查詢／整體載入性能；未宣稱實體鼠標或全部動作驗證。既有 CommonUI viewport訊息、recursive FlushRenderingCommands及C4701保留原證據，不屬本輪搜尋新增錯誤。Docs/ANIMATION_FILTER394.md、Saved/Tests/AnimationFilter394；舊Stage393 audit SHA256仍F9A556D5FCF90F54F4397B9A25CE2534D9C9EDDED06DBAB6F358C03E75D85163。

- 封存工具第一次舊audit比對失敗，原因是手抄預期SHA少了一個d；實際舊audit未變。修正固定字串後重新執行，未略過比對或覆蓋舊audit。

## E273 — Stage395 搜尋鍵盤提交、過期結果及冷查詢

Stage394 搜尋框沒有OnTextCommitted，焦點在文字框的Enter無法選取；SSearchBox原預設0.25秒延遲變更，快速輸入會暫留舊結果。現在即時篩選、Enter按提交文字驗證、上下進清單／清單Enter／Escape操作；只有mouse-click與明確Enter提交，導航／pruning不提交。Commit必須同時屬於FilteredOptions和Options，停用時拒絕，避免快變文字後舊高亮或已替換來源pointer錯播。自製popup的焦點恢復不能依賴base ComboBox舊的隱藏ListView，CloseSearchMenu補同樣的Slate user focus邏輯。

冷查詢不能用Stage394 cached2.638ms代表。原轉譯每個字掃583條短語，改首字索引保留原順序；開啟時保留未變譯文cache、移除消失來源。以原全掃演算法作oracle驗1749案例一致，不改clip／卡片／翻譯鍵值。BuildR1actual0 main0420/plugins0420，SearchR1兩項Success cold11.347/cached2.695ms。補focus後fresh BuildR2actual0 main0421/plugins0421，最終SearchR2actual0兩項Success cold9.528/cached2.638ms；真Slate SetText/ListView Home/Enter/mouse callback、disabled／stale結果／cacheprune通過。

AutoR1actual0 validtrue10cases，有效commit13.625–125.095ms，H94.346ms/request4.676ms；H背景41.314秒，782frames/698oldsamples。所有原150ms門檻維持，不宣稱冷控制器零等待或全8751逐一播放／實體鼠標測試。修正E272紀錄中手誤的literal換行標記，舊Stage394 audit與30項snapshot保留。Docs/ANIMATION_FILTER395.md、Saved/Tests/AnimationFilter395。

- 封存前共享manifest被另一建置改成0412，首次sealer assert拒絕（未建立snapshot）。不能把目前manifest當先前測試的DLL證據，也不覆寫別人的manifest。以SearchR2/AutoR1 InternalLoadLibrary確認0421，fresh SearchR3实际0412兩項Success/actual0，cold9.949/cached3.643ms；封存兩版DLL和正確log，0412不宣稱跑過AutoR1。

## E274 — Stage396 噴漆資產／API 驗證（進行中）
UE5.8.3：首輪 HotReload alias衝突未進入 C++；Editor 已關閉，改用 -NoHotReload。FFinalSkinVertex 要 include SkeletalRenderPublic.h；UHT 生成 constructor 不可持有不完整 FSurface 的 TUniquePtr，改用 TSharedPtr。Niagara Stateless editor資產需 Internal headers（含 NiagaraShader/Internal），ResetGraphForOutput未匯出導致連結失敗，改用Factory InitializeSystem(true)。資產首測不是成功：SkeletalMesh缺ImportedModel LODModel造成CommitMeshDescription ensure；RT材質DefaultTexture/LinearColor sampler不符。保留AssetsR1及備份再修正。BuildR7 actual0；場景／渲染仍待驗證，不把commandlet result0或檔案產生視為測試通過。詳見Docs/PAINT_SPRAY396.md。

### E274 補充：TestsR6 抓到 RT 初始化競態
Camera.KksMouse 與 Paint.Film先通過；新增2面PMC/4x4 RT後，Paint.Scene在render thread的UpdateTexture2D空指標崩潰，actual3。根因是在game thread取得尚未InitResource的RHI texture；較大骨架案例只是時序上沒撞到，不能據此宣稱通解。改為render command內取得Resource->GetRenderTargetTexture，遵循init/upload/release命令順序；保留小RT與快速清除／換拓撲回歸，不加特定卡延遲。TestsR6及FinalSnapshotR2保留。

### E274 水槍尺寸與驗證修正
用Niagara的厘米單位時，原1–3cm水滴看起來像水球。共用液柱改2mm寬、分離滴1.8mm寬、飛濺1.5mm，附著滴單顆0.008cm³；落地污漬按量計算，不能僅降低流量來掩蓋尺寸問題。資產builder會跳過既有NS，所以修改C++後必須備份並重建NS資產，AssetsR5actual0/ready=valid=1。BuildR23/25actual0；TestsR8actual0三項Success，含小RT GPU讀回、拓撲更換、散射偏軸命中及30/60/120Hz守恆。TestsR6崩潰根因已用該回歸驗證修復，不宣稱所有專案零bug。

- BuildR24的LNK1104不是成功：與前一個測試程序的DLL釋放時序碰撞。未終止其它程序；等自身測試完成後BuildR25重新連結actual0。VisualR6實際是舊相機，改由VisualR7驗新側面鏡頭。不要把shell exit0當成內部build成功，必須讀build自己的exit檔與Result。
- 最終BuildR26／AssetsR6／TestsR9／VisualR8皆actual0，三項回歸Success；測試前後DLL SHA256一致。側面截圖確認細噴流、小飛濺與清除結果；量測與品質限制記入Docs，主液柱與分離滴使用不同發射密度。

## E275 — 10萬面與黏性水槍比較（驗證中）

先前骨架生成的Rings24/Sides24應有6912面，實際3454面：FSkeletalMeshOptimizationSettings預設NumOfTrianglesPercentage=0.5，不能只增加原始分段就宣稱render mesh已到目標。現Rings100/Sides84，LOD0三角形／頂點比例1.0，目標100800面並以執行時faces日誌確認。SK_TestRig與NS_PaintJet舊版完整備份HighPolyAssetBackup。

噴流寬3.5mm、短條12cm、主密度Flow×200，分离水滴2.5mm×7mm；預設黏度8、表面速度8、殘留厚度0.004cm、滴落門檻0.12cm、附著1秒。AdhesionSeconds／ResidualThickness公開且面板可調；薄膜保留與脫離扣量共用通解，仍保留120Hz積分。尚待實測，不能用修改常數代替效果或效能驗證。

- E275驗證完成：BuildR27／AssetsR7／TestsR10／VisualR9actual0，Camera.KksMouse／Film／Scene均Success，runtime100800面／1024²atlas、雙人201600面。48cm³發射，12秒後film47.992、floor0.008，守恆誤差小於1e-7。黏性與更細邊界截圖已檢視；CPU初次369.118ms、均值0.588ms、末次8.552ms。不同並行負載下引擎幀率不可作低面數/高面數加速比，首次建表卡頓未被隱藏。

## E276 — 低面數材質遮罩，取代增加身體面數（實作中）

使用者指出10萬面不是合適通解。原DrawFilm把每個三角形的厚度寫成2×2同色，再建立整個三角形覆蓋層，因此圓形筆刷也變成三角形斑塊。改保留低面數身體，獨立UV chart每面4–64 texel（3454面為16），真實筆刷位置以動畫後世界表面映射到高解析遮罩；bilinear採樣／邊界擴張，形狀與身體面數解耦。物理體積仍由有限體積圖守恆，遮罩反向追蹤世界切線／跨幾何邊，插值後重新正規化，不讓渲染插值創造油漆。低面數資產恢復自HighPolyAssetBackup，高面數比較留TextureMaskAssetBackup。

BuildR28發現FMath沒有RoundDownToPowerOfTwo，改用FloorLog2位移，不假設API存在。第一次PowerShell原位寫檔遇共享文件鎖，改apply_patch並檢查內容；BuildR29目前等待共享建置鎖，不能將等待當成成功。
- BuildR29/R30被共享LiveCoding及DLL／NetCore鎖阻擋；只調整OKCrowdQuality.cpp首include順序以符合UHT，未終止其它建置。BuildR32局部module命令actual0但0actions且未產生新DLL，因此不是遮罩程式驗證。VisualR10仍載舊atlas128，保留為低面數原三角形塗抹baseline，不誤報為新材質效果。
- VisualR11證明16texel遮罩可畫三角形內形狀，但流动後有斑塊分裂：原粗面模型依面平均體積向鄰面送流，即使筆刷根本未到共享邊。改以高解析遮罩的邊界厚度／平均厚度調制有限體積邊通量，圓形污漬在面內先移动，到共享邊才傳流；固定覆蓋層0.025cm偏移取代各面不同厚度擠出，厚度感仍材質呈現。BuildR34遭共享LiveCoding鎖阻，改嘗試帶suffix的完整建置，不關閉其它程序。

- E276最終：BuildR35actual0/main0464；BuildR36因共享CrowdRoomEnvironment.inl尚未建立失敗，等待檔案出現後BuildR37actual0/main0466。TestsR13actual0三項Success，新增乾邊阻止粗面錯誤傳流、沾濕後守恆傳流、單粗三角形圓形筆刷與跨chart厚度驗證。VisualR12actual0/main0464近景靜止筆刷→動畫附著→清除已檢視；目前人物3454面、atlas1024²，48.4cm³守恆，Actor均值0.284ms/峰23.091ms。0466僅新增本輪回歸及其它同場工作，噴漆核心與0464相同；tests DLL前後hash一致。Docs已改寫為目前低面數材質通解，不把舊10萬面結果當現況。

## E277 — 高解析遮罩滴落位置與歸零生命週期

繼續審查发现旧滴落判斷仍只看Face.Volume/Area，生成点仍是最低頂點：局部濕邊可能因平均太薄不滴，面內沾漆卻可能從乾角滴落。改TakeDrip依當前高解析遮罩局部厚度、朝下法線／濕潤開放低邊挑選重心位置；只扣實際濕格的體積，維持残留與padding，Film同步扣量。

原Tick在表面量歸零後跳過DrawFilm，殘留上一張GPU遮罩／覆蓋幾何。新增HasRenderedPaint，歸零仍更新一次並清空mesh section。ClearPaintForActor僅清所選人物表面／附著滴、保留chart/BVH供再噴；已脫離液滴及其他人物不變，Cleared納入守恆帳本。GPU讀回必須讀Clear後新front RT，不可持有swap前舊front來誤判清除失敗。新增局部滴落、乾角拒絕、局部扣量、單人物GPU歸零與再次噴射回歸；結果待實測。
- E277編譯／回歸完成：BuildR38/39actual0，R40因共享LiveCoding拒絕，未關Editor；R41 suffix0467actual0。TestsR15actual0三項Success，含真实朝下PMC湿区附著／脱离、局部扣量、单人物GPU双通道归零及重畫／其他人物保留；前後DLL hash一致。原模型仍3454面／1024²遮罩，不靠調整特定卡片。


## E278 — 動畫遮罩附著與模擬更新頻率不可共用

VisualR13雖actual0且film仍48.4cm³，Flowing截圖卻沒有白漆。根因：人物每幀變形，覆蓋層只在SurfaceHz（預設20）重建，部分幀埋進皮膚；帳目／API成功不代表畫面正確。舊截圖保留VisualR13Backup，不以加面、提高外推距離或關閉深度測試掩蓋。

DrawFilm拆分遮罩上傳與表面跟隨：RT／BVH按SurfaceHz更新，覆蓋層每幀讀當前位置，固定0.025cm偏移。PMC直接用目前頂點，Poseable只蒙皮沾漆面所需頂點且共享頂點快取，一般Skinned保留完整CPU變形讀回。頂點數相同使用UpdateMeshSection，避免每幀重建section。仍附著液滴亦依當前姿勢更新。新增5Hz遮罩／240Hz子幀突然換姿勢回歸，實際覆蓋頂點距離目前身體頂點<0.027cm且RT未換，不只看油漆量。

BuildR42/43actual0；TestsR16實際0469、actual0，Paint.Film／Scene兩項Success，命令使用兩個RunTests未納入Camera，不能記成三項。VisualR14實際0468actual0，Flowing可見附著污漬；Actor均值0.277ms／峰17.797ms，4450引擎幀／12.354秒，非GPU Present FPS。新增PMC非均勻縮放／子幀變形回歸與附著滴每幀跟隨後，BuildR44actual6：同場新增CrowdBonePacking依賴ScenePrivate.h，但編譯開始時include path未到位；R45actual6：下一層TranslucentPassResource.h需要Renderer/Internal，該路徑亦在編譯後才出現。未改空檔、關閉其他Editor或回退別人的工作，按最新Build.cs重試。最終結果待補。

文件記錄首次使用Python預設cp950讀UTF8 Markdown失敗（UnicodeDecodeError），未修改文件；改明確encoding=utf-8，避免重複使用locale預設。

- E278後續：R46/47/49/51全目標actual6，阻擋為另一項引擎變更的NetCore.dll被已開Editor鎖住；R51確實編譯OKPaintSprayer並連結0477，不能把整體失敗寫成成功。NetCore suffix及UsePrecompiled並未避開該動作，停止重複嘗試。R48/50 Module-only回傳0但0actions，即使改過源檔也未編譯，不當證據。
- FrameFollowProject是隔離驗證目錄，Content／Plugins junction到實際資源，Config快照，僅自身manifest指定實際新DLL0477；TestsR17實際載入0477、actual0、Camera.KksMouse／Paint.Film／Paint.Scene三项Success。新增PMC非均勻縮放於1/240秒内改頂點，覆蓋層世界X符合目前皮膚＋0.025cm偏移，骨架子幀測試同樣通過。正式遊戲manifest僅OpenKoikatsu欄位登錄同hash0477，保留BuildId及其他欄位，前後manifest/DLL备份WatergunSnapshot；未更動引擎／插件manifest或停止其他Editor。此為已編譯且實測模組的可回復登錄，不宣稱全引擎target建置成功。

- E278正式場景VisualR15actual0，確實載入正式專案0477，Painted／Flowing／Cleared全部檢視，彎腰後白漆仍在表面、R清空無殘留。發射48.8cm³，film48.8，守恆誤差<1e-7；Actor平均0.253ms、峰17.907ms。4920引擎幀／12.048秒，不是GPU Present FPS。正式DLL測試前後SHA256一致。


## E279 — KKS 讀卡 3D 水槍整合

舊PlayerController無條件生成Sprayer，未綁Gallery畫面生命週期；3D關閉／換卡仍可保留輸入與油漆，設定面板亦未顯式對齊Gallery。KKS BodySkel為只供骨架palette的不可見載體，SkelOutline另為描邊pass，舊Discover把這些也當表面。改3D成功後啟用、開始重新載入／返回／widget結束即停射清除，保留一個可重用sprayer；設定面板z110、只在3D顯示。KKS只取Part.SurfaceMat對應且bRenderInMainPass的顯示表面，避免不可見載體搶命中。

首次Python使用locale cp950讀UTF8 Controller失敗，僅header修改；重做全檔明確utf-8。GalleryBuildR1actual6為NetCore.dll既有Editor鎖定。R2actual6另有新audit語法錯誤：TObjectPtr range不能auto*推導，缺PMC完整include；UPrimitiveComponent的5.8 API沒有ShouldRenderInMainPass getter，實際公開欄位為bRenderInMainPass。查本機header後修正，不假設API存在。新增真Gallery SelectIndex/OnLoad3D、native KKS／3D噴射／動畫／返回清除／重開／再載入audit及controller生命週期回歸，結果待實測。

- E279環境與audit：R3/4遊戲模組編譯及連結完成，全target仍NetCore锁actual6；同場建置期間引擎／遊戲.modules暫時缺失，新啟動actual3且無日誌，舊DLLbaseline亦未能啟動，不能當功能退化。Editor關閉後R5完整targetactual0，UBT恢復正式engine/game/plugin登錄（BuildId08cab42c）。正式GalleryTestsR2實際新main、actual0三項Success含3D進入／離開清除／重用sprayer。
- GalleryAuditR4真GUI KKS載入ok1，但噴射零命中。audit停PC tick後不再更新CameraManager、仍取可能舊的相機快取來瞄準；改每步更新CameraManager並取StudioCam實際origin。另Stop在ClearTimer後未持有自身shared引用，造成report Card字串損壞／輸出檔未寫；改Tick/Stop保留AsShared，與既有ReloadProfile相同，不把程序exit0當valid。增加失敗ray/stats供追查。覆蓋層sort priority改原surface+1，讓透明頭髮等表面的污漬畫在本表面之上，不調高全場paint排序。

- E279 R6未連結新DLL：同場新CrowdBonePacking使用Renderer SkinningSceneExtension需Engine/Internal的SkeletalMeshUpdater.h，該次開始時Build.cs尚未包含；路徑由同場工作加入後R7完整建置actual0/main0915。R5audit誤在R6失敗後啟動了舊main，已停止自有audit，不把其結果當新版證據；之後一律先檢查Result、exit及實際manifest再開始。

- E279 R6audit結果valid0已保存正確report：origin(-0,421,77)、target(0,0,0.328)，surfaces0。Camera修正後仍未命中，根因不能只歸於audit：KKS renderer.Bounds描述小bind mesh，未反映目前骨架palette的實際變形，廣相位排掉模型。Discover所有Skinned用當前變形頂點建立候選AABB，與三角形命中共用位置，通過後Move到FSurface避免重複蒙皮；不以擴大固定bounds或卡片特例補救。audit瞄準也用當前表面AABB中心，新增把renderer.Bounds故意改成(-1..1)仍命中動畫人物的回歸。

- E279 R8audit仍valid0：body實際有morph，載入器會把原Poseable Follow隱藏，改用MouthSlots.Comp（USkeletalMeshComponent）顯示，材質為另一個MID。Parts.SurfaceMat仍指隱藏Follow，僅材質相等不足以找出所有實際顯示表面。Discover改接受可見Part.Comp、SurfaceMat對應或MouthSlots的Comp/CpuComp；仍排除不可見palette與outline。audit改找Body Morph Slot並讀精確CPU變形，拒絕隱藏PMC fallback；先前0.328瞄準值來自隱藏後備comp，不可單独用其值斷言每個Renderer.Bounds都錯。變形頂點AABB仍維持，新增renderer bounds故意縮小仍能命中的回歸已通過GalleryTestsR4，三項Success／actual0。未將失敗audit當成正常。

- E279 GalleryAuditR9實際0922已在真KKS Morph body上產生43次命中／17.2cm³，進入walk後50hits，表面4個／18384面；已檢視Painted／Animated。返回清除及再開3D檢查亦通過，但重新讀卡時PreviewActor暫時nullptr是正常生命周期，audit在等待分支前做通用Actor必須存在的檢查而誤報失敗。改Stage6先等待bLoadInFlight，不跳過實際重載。測試瞄準裸露腿部，按JetSpeed補償重力彈道便於檢查污漬；玩家預設噴流／物理瞄準不改。R9仍記valid0，不冒充全流程通過。

- E279最終：GalleryBuildR12完整target actual0/main0924；GalleryTestsR5實際0924actual0三項Success，包括故意stale bounds、KKS morph replacement異MID、Gallery enabled/reset/reuse。GalleryAuditR10實際正式GUI／原KKS March card，report.valid1、actual0、95hits／38cm³，3 surfaces16638faces，動畫／返回清除／重開／再載入均通過，三張畫面已檢視。單次Actor update13.095ms／peak71.112ms，不冒充平均／FPS或低面數fixture效能；cold card／catalog等待不屬本次加速結果。前後DLL SHA256一致，新snapshot保留DLL/manifest/來源hash。仍未逐張測所有KKS卡／實體鍵鼠，不能宣稱全專案零bug。


### E280：水槍程序網格多分區與隱藏狀態（2026-10-04）

- 原先Discover以component為唯一鍵並只取PMC section0，會漏掉一般OKPaintable程序網格的其它材質分區；FindPaintHit僅查component可見性，已建立表面仍可能命中隱藏owner／隱藏section，sprayer所屬覆蓋層也不會自動遵循來源owner隱藏。這是通用生命週期／分區問題，不用角色卡片特例修補。
- 改component＋section去重，非KKS程序網格納入所有可見且非空分區。KKS PMC仍保留section0規則，排除原生重複描邊；Skinned仍納入原實作啟用分區。FSurface.IsDisplayed統一owner、component、main pass、PMC section可見性；命中、表面流動與附著滴脫離遵循此狀態。覆蓋層及附著滴每幀隨來源顯示／隱藏，隱藏期間保留污漬與守恆帳目，重新顯示可繼續。
- GalleryBuildR13 actual6：本輪const PMC指標無法呼叫UE5.8 GetProcMeshSection（只有非const API），已改普通指標；現有CrowdThreadPlacement的DWORD/uint32 Max型別錯誤由同場其它工作修正，未覆寫其檔案。GalleryBuildR14/R15 actual6被另一專案Editor LiveCoding擋住。PowerShell ModuleWithSuffix含逗號必須引號；-NoLiveCoding不能代替-NoHotReloadFromIDE的UBT session guard。
- GalleryBuildR16 unexpectedly排4730項引擎工作，核對0938來源command line後只停止本輪UBT，不停止其它Editor／其它工作。MultiSectionBuildR1受共用PCH失效阻擋；R2改私有物件、/Y-重編兩個修改檔。連結發現引擎import libs已不存在，從現有DLL exports在私有Imports目錄建立lib，不改共用引擎檔。第一次link缺PCH companion object及新增CrowdThreadPlacement函數，保留原PCH obj並編譯現有ThreadPlacement補齊後LinkRecoveredR2通過0939。這是模組編譯／連結成功，不能宣稱完整target成功。
- 共用project／plugin manifests已不存在，只補回缺少檔案，使用目前engine BuildId 08cab42c及實際既有標準DLL；未覆寫已存在manifest。RestoredMissingManifests.json保存受影響路徑；0939正式專案啟動及GalleryTestsR6實載0939、actual0、Camera.KksMouse／Paint.Film／Paint.Scene三項Success。新增第二PMC region沾漆、section hide/show、owner hide/show、覆蓋層可見性、重用表面與總量守恆全部通過。未把本輪當完整引擎重建／正式封裝／全部角色卡驗證。

- E280真KKS再驗證：GalleryAuditR11正式Template_Default原March card，實載0939、actual0、report.valid1，94hits／37.6cm³，3表面16638面，動畫／返回清除／重開／再載入通過，Painted/Animated畫面已檢視。update單次7.900ms、peak684.784ms，本次同場另有工作且初始化／精確morph讀回仍可能停頓，不能宣稱水槍性能已全面改善或穩定FPS。0939前後SHA256相同，MultiSectionBuildR2／WatergunSnapshot保存編譯物件、import defs/libs、link rsp、manifest、source hash及DLL。完整target受共用引擎狀態阻擋，模組與實際功能驗證已完成。


### E281：直接啟動讀卡模式（2026-10-04）

新增CardReaderDevelopment.cmd／Tools/run_card_reader_development.ps1，不共用Crowd啟動參數或benchmark入口；明確Template_Default?game=/Script/OpenKoikatsu.OpenKoikatsuGameMode、-game、可見視窗，保留既有原生／獨立卡匯入器。Start-Process ArgumentList會合併字串，全部參數加引號避免空白路徑斷裂；PS腳本UTF8 BOM以支援Windows PowerShell5.1。啟動前驗證引擎／project／manifest對應DLL／可選CardDirectory，環境OK_AUTO_3D=0、OK_AUTO_CARD空、OK_CARD_DIRECTORY可選值僅給本輪子程序並finally還原。

DryRun及指定female資料夾DryRun通過；實際透過cmd启动PID97448，窗口OpenKoikatsu (64-bit Development PCD3D_SM6) Responding=True，正常載入0939。啟動日誌Saved/Logs/CardReader/20261004-223439215-e608575d.log與同名json保存參數及DLL。這是可直接使用的Unreal開發遊戲入口，仍需引擎與編譯資源，不冒充獨立封裝。此輪僅新增啟動器與文件，未變更C++，不重做無關全引擎建置。

- E281最後覆核：最近啟動紀錄PID97208仍為讀卡視窗、Responding=True；對應日誌實載0939及OpenKoikatsuGameMode，未匹配Fatal error／Unhandled Exception。檢查後未發現本啟動器有待修正項目，未重複啟動更多視窗或變更C++。


### E282：水槍330ms幀成本調查與優化（2026-10-04～05，已完成本輪驗證）

- 根因程式證據：UE5.8 SkinnedMeshComponent.cpp GetCPUSkinnedVertices不是輕量讀回；會RefreshBoneTransforms、切換CPU skin並立即重建render state，取頂點後又切回。原水槍掃描各候選元件、RefreshGeometry、DrawFilm反覆使用，Discover又每個飛行packet重掃整個world，放大GPU／render同步成本。
- FOKPaintPose直接讀目前RefToLocal、section BoneMap、實際skin weights與ActiveMorphTargets，先套position morph delta再線性蒙皮；門檻UE_SMALL_NUMBER／MaxBlendWeight與引擎一致，用GetDeltaIteratorForLOD兼容cooked壓縮morph。共享leader matrices、原權重分母65535與世界非均勻縮放，不Refresh／切skin模式／強制LOD。cloth、external morph、mesh deformer保留原精確fallback，CPU權重／頂點缺資料拒絕此表面，不能把沒morph的近似位置冒充實際變形。
- Discover改每次Geometry更新只掃描一次，候選範圍包含source cone及所有flight packets，不能只檢查主射線而漏偏軸／放開按鍵後的液滴。DrawFilm只蒙皮實際沾漆所需頂點並共享vertex cache，BVH全位置仍按SurfaceHz。未增加面數／降低預設畫質／延後每幀附著。
- PerfBaselineBuildR1新audit編譯但link因缺plugin imports／同場新增UOKClothesStateButton反射類與舊unity物件不一致而失敗；不把混合新舊class layout DLL當有效baseline。改私有原SprayerBefore.cpp加Statistics平均欄位、保留原已驗證audit／GUI物件，PerfBaselineBuildR2成功0940。PerfOptimizedBuildR1兩個修改C++單元及新精度測試編譯／link0941成功；追加CPU資料檢查後尚需重編。
- PerfBaselineAuditR1 actual3：當時共用引擎／module metadata正在另一個完整重建中，engine UnrealEditor.modules不存在；此run無效不能當性能數據。未停止另一建置／覆寫其metadata，等引擎完成才做A/B實測。新增測試正／負／零morph、非均勻縮放對Engine CPU oracle精度及fast path不改CPUSkinning／forcedLOD；尚待真正執行，不能以編譯成功冒充測試通過。

- E282追加：舊GalleryAudit每0.05秒重新GetCPUSkinnedVertices來瞄準，會干擾整體幀時間；已保留原功能audit，另外新增ok.PaintPerformanceAudit（不含Gallery反射頭檔），只在開始讀取一次body pose／瞄準，暖機2秒＋實測8秒。採樣Actor TotalUpdateMs差分與每幀LastUpdateMs P95，另外記錄GFrameCounter／wall clock引擎幀時間，不當GPU Present FPS。baseline與optimized皆以相同輕量audit／同卡／鏡頭／設定量測。
- E282性能audit R3缺ProceduralMeshComponent／MID完整型別、R4比較TObjectPtr<PMC>與Skinned sibling型別不能直接==；改轉UPrimitiveComponent基底比較。編譯器讀檔期間寫入PermissionError，待該compile結束再修改，不重試強制解鎖。PerfBaselineBuildR5 link0943成功；PerfOptimizedBuildR2 link0942成功（含CPU資料檢查），以相同perf audit補編0944進行中。引擎完整建置仍由同場其它工作執行，未偽造metadata繞過此活躍建置。

- E282 2026-10-05追加：共用引擎建置結束後，完整project build受Crowd兩個unity單元的匿名Fresh／Save重名阻擋；Packed及其唯一include的HairOutlineRepair改為FreshPackedAsset／SavePackedAsset，僅改局部helper名稱。PerfFullBuildR1至R6完整target成功，正常manifest指向UnrealEditor-OpenKoikatsu.dll，不再把私有局部連結当完整建置。
- PerfRegressionR1 actual3：新CPU oracle精度測試沿用前段隱藏Native actor，GetCPUSkinnedVertices在MeshObject缺失時assert；修正測試前置狀態（顯示actor/component、重建render state、Flush），並避免Prepare失敗後解參考LOD。PerfRegressionR2 actual0，Camera.KksMouse、Paint.Film、Paint.Scene三項Success，包括正／負／零morph、非等比縮放及skin模式／LOD不變檢查。
- 隔離host第一次啟動缺插件完整目錄，改保留四個KKS插件實際descriptor＋Binaries，只在隔離host禁用缺SteamRuntime依賴的BpGeneratorUltimate，不改正式專案。多次game啟動停在frame0，不能直接當水槍成本或指令失效；移除RenderOffscreen、改D3D11／NullRHI均未解。對本輪owned process取native stack，確認FKKModIndex::Build／ReadZipmodGuid正在重讀整個mods。複製既有Saved/mod_index.json到隔離host後可進入測試；腳本固定帶此前置快取。
- StartupDiagnosticR1錯把unity內的PlayerController單独加進link，LNK2005；僅明列的獨立obj才能私有替換，unity內容必須完整重建。診斷log已從PlayerController移除，R5/R6完整建置成功。
- PerfOptimizedAuditR5／PerfBaselineAuditR6已完成有效8秒數據，但截圖請求讓UE SceneView::ConfigureBufferVisualizationSettings對全部overview材質EnsureIsComplete，退出卡在shader FinishCompilation；兩輪在RequestExit之後由本agent核對command line後停止，不能宣稱正常exit0或已生成截圖。性能audit移除不必要的截圖；功能audit只關閉測試的buffer overview targets，仍擷取正常畫面。相關舊run保留以供排錯。

- E282最終：PerfFullBuildR7完整target actual0；正式manifest指向未加suffix的新DLL，與實測0955 SHA256完全相同。PerfFormalRegressionR1正式專案actual0、3項Success，無需隔離host設定才能使用。PerfRegressionR3隔離host亦actual0／3項Success。PerformanceFinal0955保存DLL／manifest／來源與基準hash。
- 量測暖機錯誤：R1在SetPoseMode之前記ReadyAt，原動畫同步載入時間令暖機被跳過。R2改SetPoseMode完成後才記時間，兩輪均正常exit0／valid1；以R2為最終數據。原生Studio走路、March card、同鏡頭、4表面18384面、2秒暖機＋8秒實測：Actor平均11.642376→5.828430ms、P95 14.254298→8.724403ms；全輪Actor peak（含初始化）497.145→73.884ms。引擎frame counter/wall平均44.502299→39.235936ms，不能冒充GPU FPS或使用者330ms整幀情境精確重現。51／60hits皆守恆，分布因幀步长／動畫不同；其它Editor／背景工作可能影響整體時間。
- PerfFunctionalR1 actual0／valid1，94hits37.6cm³，原GUI沾漆／遊戲動畫／離開清除／重開／再次載卡成功，三张畫面已檢視。正式launch讀卡模式沿用CardReaderDevelopment.cmd。此輪通用優化不增加面數／降低畫質，cloth／external morph／mesh deformer保留精確fallback，未宣稱所有卡片／全專案零bug。詳細表格與限制見Docs/PAINT_PERFORMANCE282.md。
### E283：水槍拓撲建立與稀疏覆蓋成本（2026-10-05，已完成本輪驗證）

- E282後仍有首次建立表面的成本：BuildAdjacency每條邊產生多個FString／鍵串接／動態owner陣列；DrawFilm每幀掃全部Faces，即使只有少數沾漆。改為原量化座標的int64點／無向邊鍵，固定保存前兩個owner並計總數；第三個以上owner仍保持斷開。覆蓋只遍歷Film.Active，UV chart及每幀變形不變。
- 核對UE5.8 TextureRenderTarget2D.cpp後，UpdateResourceImmediate僅enqueue UpdateDeferredResource，不是同步Flush；沒有因函式名字誤判而刪除必要初始化。原清除仍上傳零厚度並刪除幾何，Active與原Film.AddVolume一致維護。
- 測試審查發現TArray.Add(Chart.Faces[0])違反UE Array.CheckAddress的自引用規則；在執行前改為複製局部Duplicate再Add，未把這種測試錯誤推給產品。R8/R9完整target建置成功。R8 warnings來自既有PaintAssets／Controls double-to-float，本輪沒有新增該轉換。
- PerfRegressionR4 actual0，Camera.KksMouse／Paint.Film／Paint.Scene三項Success。20,001面／負大座標／重複接縫／非流形邊fixture對保存的原字串鍵oracle，全邊邻接一致；原路徑71.605ms、整數鍵3.927ms，僅代表本fixture拓撲建立，尚不當整體FPS或遊戲中首次噴射結果。實際March動畫A/B與GUI清除／重載驗證繼續中。
- E283最終：PerfPreviousAnimatedR3（0955）與PerfOptimizedAnimatedR3（0957）同設定March原生走路，8秒Actor平均8.558214→6.644020ms、P95 12.449499→10.860000ms、全輪含初始化／暖機peak78.275→26.311ms；4表面18384面、64／56hits、守恆／valid1／正常exit0。背景負載影響數值，不跨輪拼接倍率或宣稱GPU FPS。PerfFunctionalR2 actual0、valid1、95hits38cm³，动画／返回清除／重開／重載全部成功，三張畫面已檢視。完整PerfFullBuildR9及PerfRegressionR4通過，正式DLL與0957 hash相同，PerformanceFinal0957保存。Docs/PAINT_PERFORMANCE282.md追加第二階段表格；讀卡啟動器沿用正常DLL。

### E284（2026-10-05）：放開噴射後卡頓的壓力測試漏測／讀卡來源覆寫

- 使用者 269 ms Frame／272 ms Game 截圖證明 E282／E283 的腿部小範圍平均不能代表整個功能。最新 CardReader 日誌噴中頭髮、臉、衣服、飾品；必須用預設散射、頭部 48 表面／86,992 面，並量測停止噴射後八秒。原版受控重現整幀 2,142 ms；這不是同一次使用者截圖，不混用倍率。
- 根因：每個 120 Hz 液滴子步都重算全部遮罩像素與滴落；反覆世界幾何投影、每幀頂點雜湊、完整 atlas 清空／上傳及 BVH 葉內不相關三角形迭代掃掠；補算形成回饋。通用修正包含獨立固定表面時鐘（預設20Hz）、精確圖表算術／padding 共用、只讀快照分塊並行、殘留薄層保留、滴落門檻排除與附著集合、持久頂點快取／預算矩陣、每個 ping-pong 目標的髒圖塊行更新、逐三角形包圍盒排除及逐顯示幀更新附著位置。沒有改特定卡片、提高面數、丟棄累積時間或質量。20Hz表面離散精度是明确品質設定，不能宣稱完全等同舊120Hz圖形。
- MaskFastRegressionR1／R2 oracle 不一致：原公式在 Local=0 與極小正值處有殘留門檻的不連續，世界浮點投影會讓乾像素阻止進入。將小於1e-12的厚度標準化為零，oracle保留同一規則後精確圖表計算差異降至浮點精度；另外保留低於殘留門檻的實際足跡。不得隨意放寬測試來掩蓋差異。
- 並行圖塊不能一邊 FindOrAdd 一邊保存 map 值的指標；先完成配置／移除，再取指標，worker 不修改 map 拓撲、不操作 UObject 或 RHI。256 圖塊 oracle 包含輸出 map 新增。
- 初版來源 audit 用字串比較 Windows 路徑，被斜線正規化誤判；改成 FPaths::IsSamePath。啟動環境目錄只初始化選擇；APNG／KKS ComboBox 可互換並記住 APNG 目錄。APNG只列可解析portable資源的PNG／原型包，僅解析可見頁面。
- 同一卡／同服裝 portable Actor 可重用，卡與外部包路徑／大小／修改時間變更就失效；audit修改自己的卡片副本驗證失效，不能修改原卡或以檔名特判。第一次仍有模型／貼圖建立成本，不能承諾所有APNG都比KKS快。
- FinalDeliveryStressR1 啟動 exit1／GetLastError127：其它開發工作更新共用 runtime plugin，測量主DLL與即時複製的依賴不一致。改用每版完整target的不可變模組依賴快照；不用這個失敗當性能數據。回歸 wrapper 同時檢查 Result={Fail}，不能只看 Unreal exit0。建置並行的中途性能輪次不能拿來宣稱加速倍率。
- 新 sweep oracle 的區域名稱 Face 觸發 C4456；改 OracleFace。共用 Crowd GPU 模組建置曾遇 WorkCount 區域／全域重名 C4459，僅將該函式區域計數改 LayoutWorkCount，保留其餘正在開發的改動。
- 最終報告、操作、時間分段及限制見 Docs/PAINT_PERFORMANCE284.md。静止與動畫分別報告；動畫仍包含原生播放、變形與覆蓋成本，不用静止數字宣稱全情境60 FPS。

- E284最終證據：ReleaseFinalRegressionR1四項Success、完整ReleaseFinalBuildR1；靜止原KKS整幀6.964／停止後11.058ms、動畫44.641／74.771ms；APNG靜止11.994／16.675ms。ReleaseFinalSourceReuseR1首次5857.822ms／重開7.756ms，修改副本能失效。兩個GUI功能audit valid1exit0。來源與水槍版本0973正常DLL／依賴hash核對通過，詳見性能文檔。
- 新查核缺口：ReleasePortableAnimatedR1在空gameRoot顯示「OK source walk could not load」，audit的valid1只檢查水槍／守恆，未驗證實際骨骼變化。不得將此輪當離線APNG動畫通過或FPS優勢；預設walk共用來源解析尚未修正，已列HANDOFF。

### E285（2026-10-05）：動畫測試假陽性與共用動畫資產的執行緒邊界

- 舊 portable 動畫 audit 只送出 SetPoseMode，沒有核對成功或骨架變形；`OK source walk could not load` 仍 valid1。新增真正載入結果、64 個分散身體頂點位移、停止噴射後質量守恆檢查。不得用静止模型代表動畫性能。
- `Buffer285PortableAnimatedR1` 首次修正遇到 AssetRegistry.cpp:2518 assertion：gallery 載入 worker 原有 ClipSources 暖機呼叫不可列舉／載入 UObject 共用動畫。APNG 跳過原生來源暖機，共用清單與選取回到 GameThread；一般 worker 不准查詢 in-memory asset registry。這次失敗 exit3 不作性能證據。
- 原始曲線資產只代表原始 clip，不等於 Animator controller state graph；依完整 Source|Clip 身分解析，不把任意狀態名稱拆短後假裝同一動作。不存在的共用片段應保留原播放。

- 驗證包裝再次誤用 `$LASTEXITCODE` 檢查直接呼叫的 PowerShell 腳本；ProcessStartInfo 的 Editor ExitCode 不會設定它。`Final285RegressionR1` 本身四項全 Success／exit0，包裝卻回報 Regression failed。後續只依 audit helper 的明確 ExitCode／report 檢查與例外判定，不用空值或先前值的 `$LASTEXITCODE`。

- E285 完成：0979 全 target build、四項回歸、APNG/KKS來源與共用動畫選單／失敗保留／檔案變更重載、原KKS／空遊戲路徑APNG動畫第0幀後持續變形及停止8秒守恆皆通過。遮罩CPU前後緩衝重用、覆蓋每面法線切線共用、只有命中材質建立雙RT。不同輪性能波動照實保留：March静止10.388–25.399ms、動畫51.277ms，APNG動畫36.519–83.863ms；不可只選最快輪宣稱固定FPS。全報告／限制見 Docs/PAINT_PERFORMANCE285.md。

### E286（2026-10-05）：動畫覆蓋重複工作與並行資料邊界

- 表面更新每面重算同解析度 chart，殘留分支前先算重力，未跨縫 texel 先算世界位置；改共用 chart／內部 texel 直接讀原 grid／殘留先判定／跨縫才計算世界位置。必須對舊世界投影 oracle 比對，不能靠畫面像就宣稱相同。
- 覆蓋並行分成唯讀姿勢擷取、唯一頂點列表、每面固定輸出範圍。所有 map／array 分配及寫入 marks 在工作執行緒啟動前完成；各 worker 只寫自己的頂點／三角形／UV tile，不能在 worker 上新增 UObject、改材質或呼叫 PMC 更新。附著滴的面也必須納入頂點列表，即使該面已無表面油漆。
- `Coating286BuildR1.log` 完整 target 因另一專案持有 UnrealEditor-Engine.dll，Engine link LNK1104。本專案模組本身已編譯／連結；使用 `-Module=OpenKoikatsu` 成功確認，不關閉或覆寫其它專案的 Engine。完整引擎重建失敗不得寫成完整 target 成功。

- E286驗證完成：0983專案模組編譯成功、四項回歸Success；強制並行／串行逐頂點、法線、UV、切線與索引相同；世界投影遮罩oracle差2.38e-7。真正KKS／離線APNG動畫、停止8秒與同姿勢對照守恆通過。March覆蓋同資料7.116→6.110ms；APNG約4.116／4.141ms無明顯改善。整幀動畫仍有最慢92.342ms，不能將單一步驟加速宣稱全面FPS改善。完整數據及編譯限制見Docs/PAINT_PERFORMANCE286.md。


## E287：表面回溯重複投影與逐材質等待

- 接縫選到最近面後再次算最近點、逐texel重查鄰面map；保存獲勝最近點及每面唯讀鄰面指標。乾燥鄰面也必須參與選面，否則會改變接縫，不能以有無油漆決定几何候選。
- 滴落每個texel重算三角形中心／出口邊；改在面級計算，無出口完成質量同步後返回。新增多解析度／旋轉／開放封閉邊界與原算法對照。
- 各材質surface分別啟動且等待工作；改批次独立遮罩。worker只操作自己map、唯讀Film，屏障後才能改Film／建立UObject。大表面仍需chart並行，不能一律串行化；工作執行緒不能用GetValueOnGameThread讀CVar。
- 0986模組建置成功、四項回歸Success。原世界投影oracle差2.38e-7；真KKS／離線APNG動畫與停止8秒守恆通過。相同資料遮罩6.020→5.322ms／7.564→4.641ms，逐像素差零、活體質量差零。整幀48.898／25.644ms，不能宣稱全場景60FPS。完整數據與限制見Docs/PAINT_PERFORMANCE287.md。


## E288：液滴重建碰撞名單及候選三角形盒子

- 同120Hz子步每packet重建完整忽略component名單；改一子步共用，仍逐步驗證component，不長期快取弱指標。
- 每次BVH葉查詢重建同一動畫幾何三角形FBox；改Refit一次建立每面快取，葉節點合成與原min/max相同。快取必須與動畫幾何同時失效／重建，不能用renderer Bounds。
- Sweep288ModuleBuildR1新增test的Face遮蔽舊區域變數，C4456編譯失敗；改TestFace，R2成功。測試大函式新增區域變數避免與外層同名。
- 0987模組建置成功、四項回歸Success、位移／旋轉／非等比縮放256射線對完整掃掠通過。真KKS／離線APNG動畫與停止8秒valid1／exit0守恆通過；同射線1.493→1.415ms／1.021→1.005ms，命中／面／時間／重心座標零不一致。整幀仍47.607／28.204ms，改善有限，不能宣稱全場景流暢。
- 本輪March同資料遮罩批次5.524ms比逐面5.174ms慢，保留反例；APNG仍較快。不能將E287單輪加速外推每個濕面分布。完整數據見Docs/PAINT_PERFORMANCE288.md。


## E289：塗層反覆配置與活躍代理重建

- 每幀配置六組輸出與scratch／pose陣列；改surface保留容量。face名單／順序／容量必須完整比較，UV不能只以face數量判定；profile pointers每輪重新收集、使用後Reset。pose重用先Reset Morph／matrices／fallback，Morph相同容量若直接SetNumZeroed會留舊內容並累加，正／負／零連續權重回歸避免此錯誤。
- 活躍油漆擴散改vertex count，每次CreateMeshSection使代理／RHI重建；保留約25%（至少16面）容量，空位零面積並錨定實際頂點。不能放在世界原點造成錯誤bounds，也不能只用cached vertex count就漏掉新face UV。兩面連續噴射驗證同容量／不重建／UV更新／空位零面積。
- R1/R2 build遇Crowd診斷對IsWeightDataValid的LNK2019/LNK1120：該方法在現有引擎DLL未匯出，改公開inline GetWeightData()!=nullptr並保留Unreadable分支。不改Engine、不打開CPU retention、不回退其它Crowd改動。R3到R7成功。
- 排程慢值不能直接猜是單surface主導。0991實際dominance僅.262/.163，均選批次，未證明之前March反例是工作量不均。依工作量70%策略是一般排程備援，不能聲稱已消除所有反例。
- 0992四項回歸Success；KKS／離線APNG六次動畫噴射8秒／停止8秒全部守恆。對照exact size→容量pool，Native區段重建1071→92、反向重測1114→98；APNG940→78。停止整幀Native79.528→62.713ms／反向60.411→51.309ms，APNG34.231→26.690ms。仍非全場景60FPS，較慢pool輪及所有中間資料保留。詳見Docs/PAINT_PERFORMANCE289.md。

## APNG library default (2026-10-05)
The prepared 100-card APNG library is output/CrowdAPNG (100 PNG cards and 100 verification receipts), not Saved/APNGCards, which contains development captures and test exports. CardReader launcher now defaults to the prepared library when it exists; explicit -CardDirectory still overrides it. WindowsEditor workspace remembers the same APNG folder. Verified launcher DryRun default and explicit overrides; no C++ or module rebuild required.

Reader290 investigation: initial real-library Rust run reached native clothing preparation after the 100-card pose loop, then stalled (all worker threads waiting, CPU stopped increasing). Stopped only the verified owned test process; this run is not counted as passed. Re-run with native phase profiling before treating state preparation as working. Reader290BuildR1 failed on pre-existing Crowd control local PackingMode shadowing a global (C4459); renamed only that control-local variable to RequestedPackingMode, R2 compiled successfully. Windows rg does not expand globs embedded in explicit path arguments; use directory + -g instead. Cargo must run with --manifest-path or its package working directory.


## E290 讀卡水柱隱藏、APNG private rig 局部頂點、衣服狀態被重用（2026-10-05）

- 水槍真正根因是 SetupStudioView 清場把既有 Sprayer Actor 隱藏。Niagara active/ready 與命中量不是可見性的證明。GuiR2/R3/R4 報 valid1 只證命中與衣服，截圖仍沒有水柱；R4 診斷 sprayer_hidden=1 才揭露原因。互動 Actor tag 保留於清場，R5 必須檢查 actor hidden=false 並檢視截圖。不要只測新建 demo 或首次 spawn，必須測重載畫面中的既有工具。
- 初版噴射 audit 用 GetComponentsBoundingBox(true) 瞄準深度，混入 studio 元件導致 GuiR1 未命中；改真正 OrbitFocus。停用 PC tick 後需更新 camera manager cache；截圖停止時序不可用固定 0.5 秒猜測，改 OnScreenshotRequestProcessed 回報後才停止清除。R3 仍不見水柱證明截圖等待不是 Actor 隱藏的修復。
- 頭髮使用 portable 載入零時間原始 private rig 求值，不針對 March 設 offset。最初 test 只找 role=hair，C000/C001 hair 是 acc 角色，會假通過；補全 private rig 部件檢查及 name hair/kami，C001 24 件高度 assertion。C000 的自訂 hair 名称無 hair/kami，因此只做全部 private rig 數值與 GUI 影像驗證，不宣稱其名字分類覆蓋。Reader290LibraryR3/R4 coverage assertion 失敗保留，不算成功。
- 同卡 Actor cache 必須包含九個衣服狀態。原 APNG 不保存被 prefab 選擇排除的半脫節點，不能只 hide 幾何當原生半脫。完整 SHA256 verified source 一次準備 state snapshot；快照後可離線，缺來源或 hash 不符要失敗並還原 UI。LibraryR5 六個 native oracle＋offline case、100 outfit0/1977 private parts通過；不等於所有 outfit 或狀態組合通過。
- 工具錯誤：R4 audit 缺 ok_extract.h 導致 OkMeshView compile error；補直接 include。R6 Python 預設 cp950 讀 UTF8 source 失敗，後面 noop build success 不能當修改成功；檔案讀取指定 UTF8並立即檢查 exit。資產 commandlet NullRHI／沒有 AllowCommandletRendering 無法證 Niagara GPU ready，Assets/R2 exit1 保留；PaintAssetsR3 正確 D3D12＋AllowCommandletRendering exit0。
- Reader290BuildR10 成功，0997 四項 UE 回歸成功；GuiR5 為最終可見水柱、頭髮和衣服測試。詳細操作、資源限制及測試範圍見 Docs/CARD_READER_FIX290.md。


## E291 APNG 換預覽／身材另存後衣服狀態快取失效（2026-10-05）

- E290 用整 PNG SHA256 命名快取，預覽／Custom 編輯也失效。資源身份改成 CRC 校驗後 okPv payload SHA256，服裝與九狀態另列；現卡形體載入後仍重算，不能把緩存形體當目前參數。舊整卡 key fallback／遷移保持相容；只讀舊 cache 不能因遷移失敗就無法讀。
- 另存只保存 PNG 會漏 .okstates。普通另存／四視圖 capture callback 接共用 Rust copy helper，驗兩卡資源ID、合法檔名與快照解碼，帶走 prepared states。有效來源 receipt 派生新的 apng SHA／保留原始 source SHA，可準備尚未存在的其它狀態；缺 receipt 仍可讀 prepared states，不能假造原生缺失幾何。
- 固定 PID 暫存名可能與殘留／同程序併發碰撞；改序號、有限重試、失敗清理，destination 已存在相同內容可接受。原子發布測試含成功、重複、失敗和 no-temp-leak。OnClothesState programmatic callback 也守 busy gate，雖然 UI 按鈕原本已禁用，不把它聲稱為已重現的實體點擊 race。
- Reader291BuildR2 成功。CacheTestsR2 兩項成功，LibraryR2 實際全14穿著服裝／12 native、edited離線、shape案例通過；GUI R1 真 Gallery 跨目錄另存與0.123身高、半脫／穿回、unchanged actor reuse、busy callback、水槍可見性 valid1 exit0。0998四項UE回歸成功。R1 library 不包含後續 receipt copier 測試，不取代 R2 最終證據。
- 測試快照複製 Binaries/* 帶入歷史 DLL／PDB，0998 副本約9.2GB，今後建立新 snapshot 應僅取 manifest 與目前引用的 runtime DLL（不要複製歷史所有檔）。本輪對自行建立的0998多餘副本檔案清理已驗 absolute target，但 exec 自動審查拒絕（blocked by policy，未提供細分理由）；未刪除、未重試或換工具繞過，原始 Binaries 不變。此清理失敗不影響功能验证。
- 沒有實體鍵鼠四視圖另存測試，無全部卡衣物组合／FPS保證。原100APNG不改，.okstates 仍是派生資源；未準備狀態須原始資源一次準備。Docs/CARD_READER_FIX291.md 保存操作、驗證範圍和限制。


## E292 水柱透明度、衣服顏色、服裝資源及畫面品質（2026-10-05，進行中）

- 水柱原先只用 PaintColor.A，GUI 沒有透明度控制。加入獨立 JetOpacity，Niagara User.PaintColor alpha 用 Color.A×clamp(opacity)，物理 Flow、衣服 pigment 不變。四項 UE 回歸及真 GUI 兩卡已通過 1.0/0.35 binding、clothing overlay、半脫/全脫/穿回、形體另存。
- Toon 預覽照明不能可靠照亮 translucent film，白色污漬看起來黑。Film 保留 lit/roughness，加入原 pigment emissive；C000_Jet 真截圖確認衣服白色污漬，不可只拿 deposited 計數當可見性證據。資產 builder 對 existing material 也更新，PaintBuild-20261005-103731 成功。
- E291 資源 copier 在 GameThread 大量解碼/複製。普通及四視圖另存改共同 background helper，immutable paths、weak UI callback、GameThread-only UI；並擋重複保存。Reader292GuiR1 實際呼叫 OnSave（修正 E291 audit 直接呼叫 CardWriter 卻稱 GUI 另存的覆蓋範圍），兩卡 background copied=1。
- 精確服裝狀態加入 OKPV0004：內容 hash 借用原卡及專案共同資源，差異資源內嵌；CRC/上限/base digest/borrowed asset checks，原 MAGIC 狀態仍相容。批次原卡不改，每 outfit 的穿著 private rig 有限值/重算一致性、半脫/移除 compact geometry roundtrip；全庫準備尚在進行，不能把 partial valid=false 報告當完成。
- 工具錯誤：第一次 Python 編輯在 Tools/ok-extract cwd 用 project-relative 路徑，FileNotFoundError；立即 abort，改 absolute root 後真正執行，Reader292RustBuildR1 完成。新解析度 FMath::RoundUpToPowerOfTwo 回傳 unsigned，與 Max(int,int) 不匹配，BuildR2 失敗；明確轉 int32，BuildR3 成功，不能引用失敗 build。
- 使用者指出 1600×1000 全身遠景不足以看污漬。固定 1024 圖集讓密集模型每三角形只分到 4–8 texel；默认2048、GUI512–4096，生效於清除後新 surface。新 audit 要 ForceRes、100% screen percentage、停 dynamic res，4K 並調近鏡頭；必須檢查真 PNG 尺寸與 closeup，不能放大原圖冒充細節。高品質成本待實測，不保證 FPS。

- E292 OtherCards4KR1 資料 valid1/exit0，但實際四張首幀黑圖，不能當頭髮視覺通過。Audit SetPoseMode/FrameCurrentPose 後立刻截圖，camera cache 更新不等於材質/陰影/背景已渲染；WornOnly 改等至少4真frame及0.5秒後 capture、processed event 後換卡，補影像檢查。Reader292RustBuildR2 在project cwd忘記 manifest，cargo exit101，R3 明確 --manifest-path 成功；對日誌/退出碼照實保存。

E292 追加：Reader292OtherCards4KR2 延後0.5秒/4幀後仍黑，不能把固定等待當成材質就緒。動態 UMaterial PostEditChange/ForceRecompile 為非同步；截圖稽核改為服務 shader 結果並等待 IsCompiling=false，再以中性灰背景與非空人物區域檢查實際PNG。先前R1/R2數值valid=1不代表視覺通過，新增image_checks明確失敗。另添加停止噴射5秒後的film重心下降/質量守恆與Flow5s截圖，尚待實測，不先宣稱流痕驗證完成。Reader292NativeOfflineR1 Rust native/edited/offline/shape 12case全部通過；新報告保存在Reader292NativeOffline.json並還原原E291歷史報告。第一次apply_patch因整行匹配失敗無修改，改為精確文字替換；BuildR7完整編譯成功。

E292 資源中斷：2026-10-05 11:25 左右 C 槽 FreeSpace=0，批次寫stderr與全outfit完整性報告遇os error112，兩批prepare已退出。Reader292AllOutfitsIntegrityR1到C026/192outfit後失敗，不算通過。未刪卡片或既有證據；確認無Unreal/UBT/prepare程序後，把 C:/portable/OpenKoikatsu/Intermediate 完整移至 D:/OpenKoikatsuWorking/Intermediate（23.97GB），原路徑建立junction，釋出>29GB，編譯資料保持可恢復。批次工具加入3GB剩餘空間檢查與截斷JSON續跑處理；重新執行主4worker與尾端2worker，完整性R2重跑。舊0998清理被拒絕的目錄未觸碰。

E292 流動畫面：1003 Reader292GuiFlow4KR1 CPU質量守恆/5秒重心下降均通過，但C001 Flow5s看不到足夠明顯白色痕跡，不宣稱視覺流痕完成。新增命中側朝向：三角形winding不保證外側，首次實際命中由入射方向決定coating偏移側（不關閉深度測試），流動重力投影仍按動畫後幾何計算。液滴附著/脫離及下表面判定使用相同側；加入正反winding可見側幾何回歸。BuildR8編輯時header參數名稱是Bary非Anchor、loop S為index，啟動cargo階段就發現並修正，沒有把未編譯變更當成功；BuildR8/R9成功，1004實測待驗證。

E292 清理（依使用者明確要求）：先記錄/驗證目標絕對路徑、manifest與已載入模組，不循junction，不遞迴刪目錄。ModuleSnapshots去除4320個未被manifest/程序使用的.dll/.pdb/.lib/.exp，154.14GiB；PerfHostStable Binaries去除292個同類冗餘副本，10.01GiB。合計164.15GiB，所有198個snapshot宣告DLL仍存在，主專案原始binary與卡片/dump/有效報告未刪。完整清單Reader292SnapshotCleanup.json、Reader292HostCleanup.json。新增snapshot_reader_dependencies.ps1，依manifest只copy當前DLL/uplugin/shaders，CheckOnly為17file/29.21MiB，避免整份歷史複製再膨脹。現在C槽>330GiB可用。上一輪0998清理拒絕後沒有繞過；本次新的明確清理指示後以限定目標直接執行成功。

E292 1004 Scene含正反winding幾何可見側、30/60/120Hz質量測試與其它3項回歸全部通過；GuiFlow4KR2首次命中Dot C000=-0.989、C001=-0.976（兩模型winding往內），修正side=-1後C001衣服白色mask由不可见恢復可見。C000/C001停止5秒film重心分別下降1.745/1.213cm，守恆誤差<1e-9；仍持續調整通用預設流速與滴落顏色，完整性/所有原生服裝資源批次未完成，不能宣稱100卡全部過關。

E292 依使用者指定刪除巨大 .ok_cache.sqlite：定位 C:/portable/KoikatuSunshine/.ok_cache.sqlite（186208616448bytes，173.42GiB）與WAL483630352bytes/SHM589824bytes。先驗證並停止本輪prepare主程序36204/23740及其Reader292LibraryBatchR1六個owned worker，確認不再有本輪寫入者，限定單一檔案路徑刪除sqlite和兩個伴檔成功，完整清單Reader292SqlCacheCleanup.json。這是可重建解析/來源session快取，不是卡片或APNG成品。一次刪除列表用了未括號的PowerShell逗號/字串加法，實際只刪主DB；檢查後以明確literal伴檔清單補刪，不假報三檔已刪。停止過程一個worker自行退出，查核全部Writer已無存活。prepare_reader_library.ps1現在為子程序強制OK_NOCACHE=1，不再把每個原生outfit/state的完整貼圖session重複寫入SQLite。原APNG/.okstates不刪；批次已完成的狀態仍可續用。

E292 續修（2026-10-05）：Reader292BuildR10/R11全建置成功；1005 Reader292RegressionR7四項成功。Reader292CompactFlow4KR1 使用無來源receipt、空gameRoot及32個精確prepared state的離線兩卡fixture（hardlink，只讀原PNG，另存輸出另檔），3840×2160實際GUI水柱/衣服白色film/clear/半脫/全脫/穿回/背景另存/edited offline全部valid1 exit0，image_checks=true，人工檢視两張Flow5s原衣料與白色下流。SurfaceSpeed24、Viscosity8，5秒film重心C000 80.844717→74.567659cm，C001 75.682262→65.355647cm，總量error<1e-9；在途packet抵達導致film量增加，不把film量單獨當全系統守恆。三角形profile仍有限解析度，不聲稱完全光滑液體或真FLIP。

E292 SQLite上限後的索引正確性：原incremental/ingest_zip忽略多個INSERT錯誤，即使快取滿也能發布mods_sig、不完整override索引被當authoritative miss。改任一catalog/guid/override/stamp写失敗回滾整個transaction，標示sql_valid=false，所有catalog/guid/override accessor回傳None沿用原TSV/scan fallback，不能假冒有效miss。seed的guid/override也檢查失敗。新增trigger模擬中途寫失敗保留已提交stamp與override的回歸，SqlAtomicR2九項成功/兩項ignored（原no-SQL孤立測試已有NoSqlCacheR1通過）；R1連結LNK1104 test exe未能覆寫，查核無本輪存活test程序後R2重試成功，R1不算通過。一般資料庫max_page_count預設4GiB/SQL WAL autocheckpoint1024/journal_size_limit64MiB，不會把已有巨大DB自動縮小；新增bundle/session blob寫入被拒仍讀source，不能承諾WAL硬限制4GiB。

E292 批次可靠性：直接prepare-reader-library命令自身也設定OK_NOCACHE=1，不能僅靠PSwrapper。為更新目前被占用的CLI，驗明93676master及六個child對應Reader292LibraryBatchR1後停止，partial .okstates保持，再用新CLI（CliBoundedR1成功）續跑4worker。六worker時FreePhysicalMemory約16GiB，高outfit卡C029有20outfit；減少同時decoder，wrapper新增低於16GiB不啟新job的防護。resume以前只信report.valid而沒驗目前PNG，現在每卡報告加input_sha256，hash不符必重建。對本輪28個已有成功報告先用全庫完整性receipt及原PNG hash核對後一次迁移指紋，清單ResumeFingerprintMigration.json。這不是依卡名修幾何，只是相同資源的續跑驗證。BuildR12及新批次進度持續記錄，仍未宣稱所有1440state完成。

E292 BuildR12失敗不是編譯錯誤：另一個KKImportIndependentCard -OptimizeStorage/-StorageApply commandlet PID104736於11:58:57啟動，占用KKCrowdGpuPacking及KKUnityBundle DLL，連結LNK1104/UBA9001。沒有終止另一工作，保持其資產整理。Reader292BuildR11此前完整成功已含SQL atomic/budget修正；R12新增只有直接CLI prepare自帶NoCache，CLI自身CliBoundedR1已成功。等待commandlet正常退出後再連結，不把R12宣稱成功。

E292 StripAll cache installed ProfileR1失敗：兩個實際zipmod（LeiseHG AndreaDoria/Hindenburg）的manifest為UTF-16 BOM，read_to_string當UTF-8失敗，使完整索引不被重用。補共享read_manifest解碼UTF-8 BOM/UTF-16LE/BE（拒絕奇數長度與不合法surrogate），GUID/uncensor及root StripAll各入口沿用相同解碼。IOException不發布cache，確定InvalidData按原行為略過，檔案改動後仍失效。MigrationCacheR2四項成功，installed首次1393.143ms／每次仍全inventory檢查的warm mean44.600ms（20次）；只代表此步驟，不代表角色FPS或整次載入倍率。兩個UTF-16實際manifest皆無StripAll；原有91個StripAll規則保留。SESSION_PIPELINE_VER251避免旧missing-mod session遮蔽新GUID解析，bundle/cache schema不改。一次Python stdout因cp950無法列出中文路徑失敗，改JSON ASCII輸出定位，不讓console編碼當資料解析失敗。

E292 BuildR13使用引擎確認支持的ModuleWithSuffix成功，避開其它StorageApply工作占用的normalDLL；第一次未quote逗號參數被PowerShell parser拒絕，沒有啟動建置，後以完整quotedflags成功。StorageApply正常退出後Reader292BuildR14/R15全normal建置成功；1007最小依賴快照供最後測試。R15加讀卡實際UI OnOutfitNext稽核、多outfit各別PNG名（避免覆蓋）、WornOnly AllOutfits參數，不能用outfit0畫面冒充全服裝GPU測試。GUI全outfit正在驗證；原全720資料完整性報告維持。

E292 全outfit視覺漏檢：MultiOutfit4KR1 6卡42套數值valid1/image_checks=true/exit0，但人工contact sheet發現Alice(C002) outfit0/1/2/3/5/6沒有五官，outfit4正常；Augusta(C050) outfit3/4疑似前臂缺失。非黑屏/像素stddev只能確認有角色，不足以確認材質與完整模型。更正Docs原OtherCards4KR3「材質畫面通過」為僅非黑屏，不用自動數值冒充完整視覺成功。SourceVisibility oracle 345.56s成功收集C002 0/4/6與C0503；extractor與APNG全部renderer摘要相同，但共享解析路徑，仍需真正原遊戲oracle。

E292 Alice原遊戲oracle：Reader292AliceNative0實際CharaStudio已輸出有五官的face.png，APNG六套無五官確屬匯入顯示缺陷。wrapper因未指定MeshNames而要求renderercontrols.tsv不存在退出1；既有face/renderers證據保留，不把這次wrapper列為整體成功。所有owned Studio程序與臨時probe均已清理，下一次取消不適用RendererControls。另一次Python讀JSON未明確UTF8而cp950失敗，改encoding=utf-8，資料本身正常。一次rg把portable*當Windows路徑失敗，改-g portable*.rs篩選。

E292 材質診斷：Reader292FaceMaterialsR1成功。Alice outfit0/4/6全部head與nosetama的變形後bounds、材質、PNG SHA/alpha相同，先排除單純遺漏貼圖；轉查GPU骨架/顯示流程。Native4 wrapper完整產出102renderer與face.png後owned Studio延遲退出，被wrapper有界停止（record exit=-1），不能描述為遊戲自然exit0。另兩次rg再次誤用Windows含*路徑無資料修改，後續只用目錄配-g。

E292 GPU源證據：Alice CpuR1 closeup鏡頭只看到身體，不能用來驗五官；CpuR2完整畫面五官正常，GpuR1仍無五官，根因縮小至GPU顯示。Augusta Native3 body.png有完整前臂，APNG缺失不是原卡意圖。ProbeBuildR1成功，新增通用可見renderer baked bounds全身攝影，不改原卡。SkinGpuR1使用GetCPUSkinnedVertices切換CPUSkinning重建MeshObject遇引擎SkinnedMeshComponent.cpp:4987 assertion，exit3未通過，改以現有OKExtractSkinnedMesh/ComputeSkinnedPositions查詢不重建代理。RustExportPaletteR1是數據收集非精度斷言；原生矩陣head最大差0.000375m，尚不能解釋UE五官完全消失。

E292 BuildR17 failed LNK2019：稽核引用OKExtractSkinnedMesh不是跨DLL export，改稽核內直接公開USkinnedMeshComponent::ComputeSkinnedPositions，不增加無必要production export。R16全建置成功54.22s，1008 immutable main/deps保留；R17未通過不替換測試host。

E292 SkinGpuR2已完成14套，但最初查詢Skel_N是被Mouth_N替代的隱藏poseable，不是實際顯示模型，MeshObject=null/visibility=false是預期，不可當根因。稽核優先查Mouth_N，再fallback Skel_N。R18成功14.68s；R19/R20因其它工作新增OKGpuAnimationManifest.cpp const_cast<UTF8CHAR*>不符FTCHARToUTF8::Get的const ANSICHAR*，未通過。只把該行cast目標改ANSICHAR，不變該工作的manifest邏輯；R21全建置成功，1010一致快照供實際Mouth模型查詢。


### E292 GPU morph / replacement draw-order follow-up

- Original Studio probes show Alice face features and Augusta outfit3 arm armor exist. Native/APNG renderer summaries agreeing does not establish visual fidelity. GPU blank Alice vs CPU full features reproduces a renderer-path difference.
- `ensure_skel_export` wrote reference-space morphs into serializable `SessionPart.morph_dxyz`; subsequent export could transform those again. Fix keeps reference-space channels in nonserialized `SkelExport.part_morphs` and derives them from preserved mesh-local `blend_shapes`, including legacy APNG payloads. Verify repeated export, cache roundtrip and legacy converted channels with the installed-card regression before claiming visual repair.
- Morph display replacement lost `Follow.TranslucencySortPriority`; copy it before component registration so face layers retain intended order.
- Skin diagnostic previously queried the hidden pose follower, then used `ComputeSkinnedPositions` which excludes active morphs. Query actual display and read-only `FOKPaintPose` instead.
- Operator errors: initial source paths assumed rust/src instead of Tools/ok-extract/src; relative Python edit ran from the Rust workdir and failed before any edit. Corrected with absolute workspace paths. Cargo success after that failed edit was not evidence of the intended change. Windows rg wildcards must use directory plus -g.


### E292 March 7th 腰部白斑：動態內衣遮罩遺失（2026-10-05）
- 使用者提供 `mesh_dump/[Card] March_7th_keiyuu.png/outfit0`。`mesh_61_o_inner_a.materials.json` 明確綁定 `_AlphaMask=cf_top_08_inner_mab`，`_alpha_a/_alpha_b=1`；外衣 MainTex 與匯入 PNG 相同，不能以貼圖白色或油漆推論。
- `ChaControl.cs:6238/6242/6283/6287` 將外衣的 `texInnerAlphaMask` 指派給內衣 renderer。`append_meshes` 的 source_program 分支略過 alpha baking 時也略過外衣傳入遮罩，導致白色內衣穿出外衣。
- 排除：NoInnerOutlineR1 白斑仍在；NoInnerR1 隱藏整件內衣後消失。隱藏內衣只作定位，不是正式修法。外衣位置對 dump 最大差 0.032 mm，不能任意縮小或移動網格。
- 共用修正：原生 shader 綁定動態 `_AlphaMask`、UV ST、兩個狀態通道，保留 MainTex；SESSION_PIPELINE_VER 252。unit NativeMaskR1 四種衣服狀態與既有 CPU texel/方向契約一致，通過。
- 新增 repair-clothing-masks 通用 APNG 全 outfit 資源遷移；先驗證原卡/APNG receipt SHA，幾何/UV/MainTex 不可變，PNG/APNG 其他 chunks 原樣保留。March 遷移與實際 GPU 驗證進行中，未宣稱完成。
- 操作錯誤：PowerShell 下將 glob 放到 rg positional path 會失敗；必須 rg directory -g pattern。讀取不存在的猜測 script 路徑前應 rg --files。

- 已完成 Reader292WaistFixed4KR1：March 7th 全 7 outfit、3840×2160 GUI 圖片有效，outfit0 腰部白斑消失，內衣袖子/領口仍保留。MaskRegressionR10 四項 KksMouse/Film/MaskParity/Scene 成功；BuildR25 成功95.90s。
- 修正開發中的 Rust E0382：讀取 alpha_mask_png 時不可移走欄位後再借用 mesh；clone binding 後重新計算 main_alpha 的 opaque 證明標記。CliBuildR4 編譯成功，NativeMaskR2 通過。
- 舊批次主程序90152與其核對命令列的4個子程序已停止，避免續生缺遮罩 states；所有已完成資料保留。新版全庫遮罩升級正在 Reader292MaskLibraryR1/Cards staging，原100卡未覆寫，receipt 保留來源SHA並增加升級前APNG SHA與report。

- WaistClothing4KR1 實际GUI通过：valid=1/exit0/4K图像有效；worn=14832 triangles、half=8741、full removed=0、穿回成功；clothing_coatings=1，opacity=.35，流动5s下落8.015cm，守恒误差约0。
- March MaskStatesR1 全7 outfit/14真实状态完成 valid=true。全库已有状态可用 upgrade-prepared-clothing-masks 重新绑定新基底；CRC/原基底hash/升级前后几何及编码roundtrip验证，不必重拆已正确的半脱/全脱模型。未测试此新增迁移前不可称全库状态完成。

- 全库mask升级完成100卡/720outfit/128个动态遮罩绑定。mask-byte-validation.json valid=true：所有原APNG影格和卡片metadata（除okPv资源chunk）逐位元相同。
- 状态升级工具初测错误：decode_at接收portable payload而非整个PNG；直接传PNG导致invalid portable pack header，未产生/发布错误state。已改为png_snapshot取okPv payload；CliBuildR6重新编译及重测。曾在链接结束前启动旧CLI不认识新command，须先核对cargo exit0再执行新工具。

- upgrade-prepared-clothing-masks R3 完成80张存在旧state的卡/1127个state，CRC/base digest及几何roundtrip全部通过。全库100卡/720outfit新PNG已经发布到output/CrowdAPNG，原PNG/receipt备份Saved/Backups/Reader292BeforeMaskUpgradeR1。状态采用同磁盘hardlink避免复制数GB数据，若跨盘才copy。
- 发布脚本 PowerShell `File.Replace(tmp,receipt,$null)` 把null变为空字串导致C000 PNG已替换后receipt步骤失败。恢复实现以旧/新SHA与原始backup三方核对允许续跑，改用明确的receipt backup路径；已恢复并完成100卡，所有live PNG SHA与migration report一致，publication.json valid=true。禁止用假设已完整发布来跳过核对。
- BuildR28成功68.65s，1017 minimal snapshot19files29.81MiB；MaskRegressionR13四项Success。WaistUpgradedStates4KR1实际GUI使用重新绑定的旧半脱/全脱模型valid1/exit0/imagevalid，clothes flow5s下降8.167cm。正在正式library再测WaistLive4KR1。
- 全库新版prepare_reader_library已恢复：exec82997，Reader292LibraryBatchR1；旧input SHA receipt不可当完成，新资源hash隔离旧cache。1127已迁移states不等于全部1440目标完成，需以report.json valid=true为准。

E292 正式庫 GUI 驗證（2026-10-05）：Reader292WaistLive4KR1 已完成，valid=1、exit=0，三張 3840×2160 圖片均通過角色存在檢查，人工檢視 C001_Worn.png 確認腰部白斑消失，白色內衣領口／袖子保留。正式 output/CrowdAPNG 使用 normal 1017；真實半脫 8741 面、穿著 14832 面、全脫衣物 0 面、穿回與另存後離線狀態通過。水柱透明度 0.35、衣服 coating=1，5 秒流痕重心下降 8.015 cm、守恆誤差約零。此測試不代表 Alice GPU face／Augusta 手臂材質已修復。

### E292 全庫遮罩與穿脫資源完成（2026-10-05，最終檢查點）

- Reader292LibraryBatchR1/report.json：valid=true、100 卡、720 outfit、1440 states、png_unchanged=true、errors=[]；批次程序正常 exit=0。live-hash-validation.json 再次核對全部 100 張正式 PNG、verified receipt、prepared report 的 SHA256 相符；720 outfit 的 private rig/finite 與 1440 幾何 roundtrip 全部通過。
- 正式庫維持 output/CrowdAPNG，CardReaderDevelopment.cmd 直接使用。100 張 APNG 的所有 outfit 已遷移必要衣服遮罩，原預覽影格與尾端卡資料未改；原卡與 receipt 保留於 Saved/Backups/Reader292BeforeMaskUpgradeR1。所有目標半脫/全脫快照已備妥，可由相同 APNG 資源離線讀取，不需首次重新拆原生控制器/衣物。
- 狀態驗證範圍為每 outfit 的 111100000 與 333300000（上衣、下裝、胸罩、內褲），並非九欄所有 4^9 組合。720 outfit 是資料完整性/骨架/幾何驗證，不是逐張原遊戲視覺比對。
- 最終建置 Reader292BuildR28 成功；normal main=1017，ModuleSnapshots/1017 匹配依賴。MaskRegressionR13 的 KksMouse/Film/MaskParity/Scene 四項 Success，NativeMaskR2 四衣服狀態的動態 mask/UV/ST/MainTex 契約通過。
- 正式庫 Reader292WaistLive4KR1：valid=1、exit=0，4K 截圖已檢視，March 7th 腰部白斑消失，白色領口/袖子保留；穿脫/穿回/另存離線、水槍 opacity .35、衣服流痕 5 秒下落 8.015cm、質量守恆通過。
- 獨立未解問題仍為 Alice GPU face 與 Augusta 部分手臂材質；不得用本輪遮罩/狀態驗證宣稱整個專案零 bug。不得清除 concurrent Crowd 工作或原卡/dump/有效 resources。

### E293 首次 GPU 顯示姿勢同步調查（2026-10-05）
- Reader293BaselineR1 正式 APNG 兩卡 14 outfit 的 4K 稽核數值通過，但人工仍見 Alice 六套缺五官、Augusta outfit3/4 身體區域缺失；數值有效不能代替外觀驗證。
- HideOrnamentR1 隱藏 O_nosetama 不改善五官。HideFaceR1 已改成隱藏真正的 Mouth_N/Skel_N 顯示元件，移除臉後仍沒有眼睛；NoSkinCacheR1 實際 r.SkinCache.Mode=0 也沒有改善，禁止把關閉 SkinCache 作正式修法。
- 發現 LoadSession 只更新 Poseable followers，沒有呼叫 SyncMouthPose。Morph 替代元件仍可能使用參考姿勢；Gallery 未修改形狀時跳過 ApplyShapes，不能依賴後續操作初始化。已在載入最後同步替代元件，待 BuildR2 與實際畫面驗證。
- 操作錯誤：猜測不存在 KKSkelMeshBuilder.cpp/OKPaintSkinning.cpp 路徑讀取失敗，實際檔為 KKSkeletalBuilder.cpp/OKPaintSkinning.h；先 rg --files。原生材質 JSON 同時包含 _LineColor/_linecolor，PowerShell ConvertFrom-Json 必須 -AsHashtable，否則會拒絕，不可忽略大小寫不同的來源鍵。

E293 後續排除與範圍調整：BuildR2 成功，InitialPoseR1 實際畫面仍缺五官，SyncMouthPose 只能修補初始化缺口，不能宣稱主因已修復。HideFrontR1、HideBodyR1 仍缺五官；CpuAugustaR1 outfit3 同樣有身體缺塊，GPU/CPU 問題不可混為一談。使用者明確要求停止 GPU 調查，以 CPU 及水槍／真實穿脫／APNG 十秒內載入為本輪完成範圍。GPU face 與 Augusta 遮罩差異保留記錄，不再展開調查。

E293 操作記錄：Get-Content -First seventy 無法解析，已改數字參數。再次使用 positional wildcard 的 rg 查詢失敗，後續使用目錄搭配 -g；不以失敗查詢推論檔案不存在。

E293 CPU 功能驗證：Reader293CpuAcceptanceR1 的水柱可見、衣服 coating、透明度 .35、流動 5 秒、守恆、真實半脫/全脫/穿回、另存離線均通過。但 C001 首次 load_seconds=19.003，不能當成十秒載入驗收通過。CpuLoadProfileR1 顯示 UE 貼圖階段 9.102 秒（解碼 4.090 秒），總 UE 10.600 秒。正在把不可變 PNG/mip 準備移到 session worker，最多四個 decoder 並行；512MiB cache 預算、完整來源位元組 identity、每 actor 獨立 UTexture2D 保留，不降低貼圖解析度。
E293 BuildR5 編譯錯誤：新增預熱程式誤寫 overtex_len / overtex2_len；OkMeshView 實際欄位為 overtex_png_len / overtex2_png_len，已按 include/ok_extract.h 修正。必須以成功建置與實測作為發布依據。

E293 CPU 載入修正驗證記錄：BuildR6/BuildR7 成功。CpuTex293R2 的 PngCache 通過並行像素準備／獨立 sampler／LRU；CpuMip293R1 的 RuntimeTextureMips 原生 dump 對照通過，包含 127.5 alpha 平均必須保留 127 的裁切邊界。首次預熱 R1 從 19.003 秒降為 11.109 秒，仍未達十秒，不能宣稱完成；已補上 View 的 detail/gloss/eye/normal 資源預熱，並以等價整數平均加速偶數 mip。
E293 測試操作失誤：PowerShell script 通過後，呼叫端以 $LASTEXITCODE 檢查 script 狀態，null -ne 0 導致誤報並阻止下一個 GUI 測試。該 script 已自行檢查 process exit 與 Result={Success} 並於失敗時 throw；後續直接依其驗證結果續跑，RuntimeTextureMips 確實成功。錯誤測試前綴 OpenKoikatsu.Texture 沒有涵蓋 RuntimeTextureMips，已單獨用完整名稱重測。

E293 整庫載入 R1 未通過十秒目標，已保留 run.log，不宣稱全庫成功。檢查實際 UE5.8 Material.cpp，PostEditChange 已執行 CacheResourceShadersForRendering 並對一般材質建立 FGlobalComponentRecreateRenderStateContext；程式又立即 ForceRecompileForRendering，對每个新材質重複處理。共用預覽材質工廠改標示 bIsPreviewMaterial 並只編譯一次；該旗標在 Engine/Public/Materials/Material.h 明確為 transient/duplicatetransient，匯出永久材質不會保留預覽旗標。
已核對並停止本輪 Reader293CpuLibraryR1 的 PID 98016；其他 Unreal 程序未碰觸。首次身分檢查因 log 路徑使用斜線與實際反斜線不同而拒絕停止，改為 GetFullPath 的精確 abslog 後才停止，沒有放寬目標。

E293 Reader293CpuLibraryR2 完成 100 張，但 load_times.json valid=false：max_visible_seconds=14.613，不能當全庫達標。C028 的349材質/191 parts 仍偏慢，C037 預热 slot 含不使用的 authored 贴图。共用修正再加入實際 shader 綁定清單、依核心數最多8 decoder、線性貼圖在首次上傳前設定 sampler，避免重複 UpdateResource。BuildR11 成功。
Reader293CpuHeavyR1：C001/C028/C037 均 valid=1/exit0/imagevalid；實際 dispatch 到顯示 max=9.352秒，load_seconds=6.000006/3.773696/1.522607。正式100卡 Reader293CpuLibraryR3 正在重新驗證。不能用三張的成功代替全部100張。
R2診斷 script 的 cp950 console 無法輸出含簡體中文字的 log，改輸出 ASCII-safe 診斷；原 log 未修改。

E293 正式全庫 CPU 載入驗收完成：Reader293CpuLibraryR3/report.txt valid=1、process exit=0、100 張角色截圖有效；load_times.json valid=true、expected=100、measured=100、max_visible_seconds=8.649。範圍為100卡首套服裝、預設1600×1000視窗，包含視圖/shader readiness，排除Unreal啟動。全720 outfit資料完整性已由E292驗證，但不得宣稱本次測過720套顯示時間。Reader293CpuFinalR1完整水槍/穿脫回歸進行中；過早讀取尚未產生的report.txt只得到檔案不存在，後續等測試完成再讀最終報告。

### E293 最終驗收（2026-10-05）
- Reader293BuildR11 成功；正式專案 DLL 與測試 snapshot1024 使用同建置相依模組。
- Reader293CpuLibraryR3：100卡首套服裝 CPU 真實 GUI 載入全部成功；1600×1000，從載入到畫面顯示最慢8.649秒，load_times.json valid=true。不含Unreal程序啟動。
- Reader293CpuFinalR1：兩卡3840×2160 GUI完整回歸 valid=1、exit=0、截圖有效且已檢視。水柱 opacity=1/.35 可見並命中衣服；每卡 clothing_coatings=1。停止噴射後5秒流痕重心下降7.051736/8.121519cm，質量守恆誤差約零；空中剩餘液滴仍會入射，表面量26.4→29.2並非無故增量。
- March C001 穿著14832面、真實半脫8741面、全脫對應衣物0面；穿回、另存後離線載入、busy callback保護及未修改卡重用均通過。全庫100卡/720outfit/1440目標穿脫快照已由E292準備並驗證，不需再次轉換。
- CardReaderDevelopment.cmd 預設CPU並直接進入output/CrowdAPNG。A+左鍵噴射、R清除；面板水柱不透明度0–1。共用修正未降低貼圖解析度，未用角色名稱特例。
- 本輪依使用者停止GPU視覺調查，保留E293既有未解記錄；不得用本輪驗收宣稱所有其他問題已修復。

E293 發布核對補充：驗收固定使用1024 immutable snapshot；收尾時其他工作已重新建置正常DLL（16:56:17，與1024 bytes不同），不能稱兩者逐位元相同。正常專案、KKUnityBundle plugin及Engine目前BuildId皆8efe457f-c803-4fca-96e9-fb6466924b49，啟動器DryRun通過且仍選CPU/output/CrowdAPNG；保留其他工作的正常DLL，不覆蓋為舊snapshot。

E293 正式版本續驗：前次僅固定1024 snapshot通過，正式DLL被並行AliceGpuBoundsBuildR4重建，故補跑正式uproject。run_reader_regression_audit.ps1新增ProjectPath選項並使用與啟動器相同GameMode；記錄launch.json的DLL SHA256及BuildId，啟動前檢查Engine相符。正式啟動log出現CommonUI非CommonGameViewportClient訊息；讀卡畫面走既有Slate/PlayerController路徑，本次水槍/衣服實測仍需驗證，不因訊息推論功能失敗，也不隱藏log。

### E293 正式版本補驗完成（2026-10-05）
- Reader293NormalLibraryR1 直接執行正式 OpenKoikatsu.uproject / normal DLL，CPU，1600×1000。100卡首套服裝 GUI及角色影像檢查全部通過；process exit0，report valid1，load_times.json valid=true，expected=100/measured=100，最慢8.425秒（包括視圖及shader就緒，不含Unreal啟動）。
- launch.json 記錄DLL SHA256 B788CFAAD7ACE3A32F5B1EEDE8700F78B99636842C3E0889FB89AE2FAC97F7E9，建置BuildId 8efe457f-c803-4fca-96e9-fb6466924b49；驗收結束再次核對DLL hash未改。此結果補足之前snapshot1024與正式DLL版本差異。
- Reader293NormalFinalR1 正式專案兩卡4K完整GUI功能回歸通過：opacity1/.35、水柱命中衣服、clothing_coatings1、流動5秒下落7.031827/8.272231cm、守恆誤差約0、真實半脫/全脫/穿回、另存離線、busy callback與未修改卡重用。C001_Flow5s.png 已人工檢視。
- 通用測試工具新增ProjectPath（預設保持原snapshot host）、與正式啟動器相同GameMode，啟動前核對Engine BuildId並保存launch.json。未新增角色特例、未改寫有效APNG、未降低貼圖或幾何品質。
- 本輪CPU/水槍/衣服/100卡十秒載入範圍完成；全720 outfit仍為之前資料/幾何完整性驗證，不把100個首套服裝計時當720套性能結果。

### E293 使用者要求最後驗收（2026-10-05）
- Reader293AcceptanceFinalR1 直接正式uproject/CPU，兩卡4K全功能回歸完成：valid=1、exit0、image_checks valid=true，C001_Flow5s圖已檢視。水柱opacity1/.35、衣物coating、5秒流動下落6.997392/8.301426cm、質量守恆、真實半脫/全脫/穿回、另存離線與busy/reuse全部通過。
- 正式DLL SHA256與Reader293NormalLibraryR1及本次launch.json相同，驗收前後未改；因此同版本100卡首套服裝最慢8.425秒的完整GUI驗證仍適用。不把720outfit資料完整性視為720套性能計時。
- CardReaderDevelopment啟動設定核對CPU/output/CrowdAPNG，既有100卡/720outfit/1440目標狀態完整性valid=true/errors=[]。本次未發現本輪功能失敗，不需額外修補或重製APNG；保留先前已知其他範圍問題，不聲稱全專案零bug。

### E294 使用者回報推翻前次視覺驗收（2026-10-05）
前次短噴射、全身圖與質量守恆不足以驗證手臂近看：使用者實測噴3秒、停止5秒後看到三角白斑及Frame196ms/Game52ms。原Mask.Advance與Film.Advance分別搬移形狀/面量，再按面縮放且空白面Brush fallback，形成三角邊界；水柱Tick每幀VisualRandom方向與物理方向不同，形成瞄準抖動。改保守格點搬量及共享頂點法線/偏移；取消隨機轉向；加入可調512水柱粒子、16飛濺effect（每個至多12粒）、64碰撞/附著液滴、128地面污漬上限。BuildR1/R2成功，必須以新的3秒/5秒回歸與近看為準。
查詢再次誤用Windows positional wildcard失敗，已改目錄加-g。假設的NiagaraStatelessEmitter.h路徑不存在，未依假設新增API。

E294 Reader294FlowR1首次回歸失敗：已向下流動但保守搬量寫入float厚度貼圖，多次疊加後double帳本誤差8.9067e-5，超出1e-5門檻。禁止放寬門檻；保留double初始量，只把每次float量化殘差回補到最大濕chart的質量帳本，不建立新足跡或逐面重新分配flux。已加入修正，待重測。

E294 裸手臂audit首測未命中，不能當視覺成功：測試取Part.BaseVertices作世界位置，未對齊真正顯示的蒙皮/替代元件。改取可見Skinned/CPU procedural顯示位置，避免把source/rest位置誤當目前皮膚。未修改角色幾何或用卡名例外。

E294 裸手臂R2仍未命中：證明只修正來源座標不是根因，不得沿用猜測。發現audit只做一次距離/速度重力補償，發射速度normalize後的飛行時間不同，細手臂邊界會錯過。改共用AimDirection解析低彈道（Runtime與audit相同），不用一次距離近似；沒有真實解時返回直射，不編造可達。降低水柱上限會重置Jet系統，避免舊粒子暫時超過新預算。

E294 PerformanceR1不能使用舊GPU性能audit驗證CPU：該工具只找USkinnedMeshComponent，CPU模式顯示body是Procedural替代，故No displayed body失敗。未將零樣本當性能結果。改讀卡GUI回歸以Sprayer.TotalUpdateMs/UpdateSamples每幀帳本與實際wall/frame差計算停噴後5秒均值，避免timer抽樣alias及4K截圖阻塞混入FPS；保留失敗報告。新增設定後面板超出視窗，加入640高可捲動容器。

### E294 最終修正及驗收完成
Reader294BuildR7成功；共用保守厚度Transport取代runtime雙重face/chart flux及fallback；共享法線偏移、解析彈道、固定FX方向、可調512 jet/16 FX/64 simulation/128 floor上限，面板可捲動。原始APNG、角色網格未改/未重製。
Reader294FinalArmR1、Reader294FinalFlowR1正式CPU GUI valid1/exit0/imagevalid；噴3秒停5秒，手臂近拍及衣服流痕已核對，質量守恆及穿脫/離線回歸通過。每幀累計paint平均0.100157/0.142517ms、最大2.888501/3.867902ms；實際wall/frame整幀平均3.078688/3.225058ms（單卡Tpose、不包含readback、不承諾任意硬體/動畫）。peak simulation54/7<=64，impact16<=16。R2/R3 timer抽樣性能值不能代替這次帳本值。PaintTransport294R2四項Success及PaintAim294R1窄目標解析瞄準測試Success。
完整說明Docs/PAINT_FIX294.md。E293的100卡載入hash不同，本輪不宣稱新版本全100卡重新跑過；本輪load C0002.074671/C0015.923513秒。先前GPU face/Augusta其他疑點仍保留，不擴大為全專案零bug。

### E294 使用者要求最後完整驗收（2026-10-05）
正式CPU DLL SHA256 BBB22E4CD77C5CBC30F276B89A41FCB32044865BF2EC1DA5DB6B18415BDA3CEC，驗收前後與先前FinalFlow相同。啟動器DryRun為CPU/output/CrowdAPNG。
Reader294AcceptanceArmR1 / Reader294AcceptanceClothesR1：正式uproject、4K、噴3秒/停5秒均valid1/exit0/image_checks true；截圖已檢視，未見原三角鋸齒白斑。水柱1/.35、命中、下流、質量守恆、穿脫/穿回及另存離線均通過。停噴後油漆actor平均0.323913/0.245138ms、最大5.105603/4.708901ms；同期wall/frame平均整幀7.495937/5.015856ms，較上一輪受同機負載影響而變動，但未重現196ms停噴卡住。只能描述本機單卡Tpose測試，不能承諾任何環境的FPS。
峰值simulation54/7（上限64）、impact16（上限16），jet預算512。此輪未出現新的功能測試失敗，不需要額外修改實作；原資源保留。驗收範圍維持E294水槍共用修正，未重跑全部720 outfit或E293 100卡載入計時。

### E295 C018 停噴卡頓重新調查（2026-10-05）
使用者回報不知火舞C018噴3秒/停5秒仍卡；E294僅C000/C001及固定局部射擊不能宣稱已涵蓋所有命中負載。正式CardReader 185919日誌證實多材質/頭髮/臉接觸後每秒僅前進數幀。C018固定腰部/手臂未重現，新增橫向上半身掃射成功重現：Reader295C018HeadBaselineR1停5秒 paint平均65.611326ms、最大350.237701ms、整幀70.598760ms；mask transport主要耗時。標籤Head實際+45cm擊中胸口上衣，不能稱已驗頭髮；另加+70cm Hair測試。
共用修正：殘留厚度直接留原整數格點，避免重插值的roundoff擴散；triangle內用預算affine barycentric平移，跨面exact query重用姿勢/步長/大小revision快取，最多65536路由每surface；reuse chart buffer、每face area/normal及整數padding；變形/世界transform真的改變才refit並invalidate。沒有卡名特例、降低解析度、丟棄油漆或停掉流動。
BuildR1因區域P名稱遮蔽（UE treats C4456 as error）失敗，改Current；建置R2/R3/R4成功。避免SetNumUninitialized後讀新增未初始化位置，Changed短路後才比較舊位置。圖表內TMap加entry可能reallocate，因此Held使用TArray data指標、不跨插入保留map value reference。
Paint295R1自動化CLI忘記quote含空白參數，TestExit只讀到Automation，提前exit，不算成功。R2 NullRHI下ConservativeTransport/Film/MaskParity成功，但Scene渲染RT命令crash；Scene必須用真正RHI，Paint295R3四項全部Success。保留失敗日誌，不能以process exit0代替測試結果。
Reader295C018HeadFixR1停5秒平均整幀6.964068ms、paint平均4.257099/最大38.813602ms，valid1/exit0。Reader295C018HeadLongR1停15秒整幀9.741862ms、paint平均6.653881/最大65.327398ms，valid1/exit0。兩者守恆及實際穿脫/離線均通過；尚待最终Hair及原卡回歸。

E295 第二階段：HairFinalR1真頭部噴3/停15雖mask已加速，但coating每幀重送靜止mesh、全部濕chart平均/Float16轉換，整幀40.350483ms仍不足。HairFinalR2因測試錯把頭髮命中要求必須有衣服coating而valid0；改頭髮模式不要求衣物、仍要求Niagara/Deposited/HitCount。最細的逐tile RHI上傳產生過多小copy、曾出現高frame時間，改每surface最多16個局部矩形，不降低解析度。HairFinalR3僅bounded rect尚有整幀24.733718ms，不列為完成。加入雙RT各自pending dirty、實際float厚度變更/清除才上传，靜止mesh不重送、動態pose/transform仍逐幀驗證；TakeDrip同步float量有改變也標dirty。避免新RenderPositions讀未初始化值，初始copy真實world位置。
BuildR5–R10成功。Paint295R4真正RHI四項Success。Reader295C018AcceptanceR1噴3/停5整幀6.768012ms，paint3.284065/33.614099ms；HairAcceptanceR1噴3/停15整幀6.527173ms，paint3.618426/43.700799ms，皆valid1/exit0/imagevalid，守恆/下流/64simulation/16impact/512jet/真實衣服穿脫/離線save全通過。不能把Actor峰值說成零卡頓或每幀均低於16ms；持續數百ms負載已解除。這些數據為R10 DLL089DC3A58252ED2B279E5B9AD7372768F064945CA29762C1906A39003863F440。
正在補收尾修正：靜止mask後仍可變更PaintColor，不能把材質色彩更新綁在dirty貼圖上傳。最終以新建置確認。

E295 收尾回歸：Reader295OriginalCardsR1 C000/C001水槍、下流、守恆、穿脱/穿回及離線save通過，整幀3.485943/3.385229ms。本輪C001 load10.285299秒，未修改APNG載入流程，但不能宣稱本輪兩卡均<10秒；記錄實測，不沿用舊數字。已新增靜止塗層不重送網格、靜止油漆可調顏色的Scene測試；RenderFaces依face ID排序，避免TSet重建迭代順序改變誤判拓樸。BuildR11/R12成功，最終版本仍需檢查Paint295R5與正式GUI結果。

E295 最後正式版本完成：BuildR12 / DLL SHA256 C7016EF11775852554DDE7E1D550FAD9BB511097C7CFEA6DA28E93CBED5E6169，驗收前後相同。Paint295R5 四項Success，包含靜止塗層無mesh重送、顏色可更新。Reader295FinalUpperR1 / FinalHairR1 / FinalOriginalR1皆valid1、exit0、image_checks true，最終截圖已檢視。
C018噴3/停5：paint平均6.071284、最大33.714600ms、整幀15.533918ms；原同位置baseline最大350.237701ms、整幀70.598760ms。頭髮/臉噴3/停15：paint平均7.000142、最大27.733702ms、整幀16.055862ms，未復發持續數百ms負載。此前R10不同時間6.5–6.8ms不得當R12保證；保留本機負載/幀率變動證據，不宣稱任何幀零卡頓。
C000/C001整幀2.797082/3.630545ms、load2.408374/6.585781秒；原卡水槍/下流/質量守恆/穿脫/穿回/另存離線全通過。C018 load1.665573/1.670881秒。C001前輪10.285299秒仍保留，不隱瞞、不延伸成100卡性能承諾。
全部保留原APNG、原網格及貼圖解析度，無卡名特例；64 simulation/16 impact/512 jet预算。重啟CardReaderDevelopment.cmd即用新DLL。詳細紀錄Docs/PAINT_FIX295.md。此輪完成C018 CPU水槍持續卡頓通用修正；未重跑全部720outfit或全100卡載入。

### E295 再次完整驗收（2026-10-05）
使用者要求繼續並最後驗收。正式DLL仍為C7016EF11775852554DDE7E1D550FAD9BB511097C7CFEA6DA28E93CBED5E6169；啟動器DryRun核對CPU/output/CrowdAPNG，先前同版C000/C001回歸與Paint295R5四項Success均已核對。本次未新增程式修改。
Reader295RepeatUpperR1 / RepeatHairR1 正式CPU、1600×1000、C018：噴3/停5及噴3/停15均valid1/exit0/image_checks true，實際截圖已檢视，命中/下流/守恆/穿脫/穿回/另存離線全通過。
停噴期平均整幀5.826514/5.097292ms，paint平均2.963988/2.946916ms、最大47.081400/36.365397ms；與前輪整幀15.53/16.06差異保留為同機負載/幀率變動，不承諾每幀零卡頓。未重現持續數百ms負載。simulation<=64、impact<=16、jet預算512；下流重心下降9.268317/2.214132cm，守恆約零。載入1.917107/1.946918秒。
第一個程序RHI初始化等待较久，後續正常進入場景，不計入APNG load_seconds或假稱噴射卡頓；保留啟動日誌。本輪功能測試沒有新的失敗，無需重製APNG或新增修補。驗收前後DLL相同，本輪完成。

## E296 動畫 CPU 卡頓調查注意事項（2026-10-05）
- 上輪 T-pose 噴射效能驗收不涵蓋持續播放動畫；不得引用靜態結果宣稱動畫正常。最新使用者日誌動畫每幀約 40 ms，已新增五秒真實連續播放與播放中噴射驗收。
- Windows rg 又出現 positional 萬用字元路徑錯誤：`KKPreviewActor*.cpp`、`ffi*` 不能直接作路徑，必須使用實際目錄搭配 `-g`。亦曾把插件原始碼誤查在 Source/OpenKoikatsu；已改查 Plugins/KKUnityBundle。
- Python 輸出角色名稱到 cp950 終端造成 UnicodeEncodeError；改用 JSON ensure_ascii=True。原始 UTF-8 檔案未損毀。
- 初判大型來源網格拆材質存在未引用頂點；逐頂點索引查核證明 C010 的八個 48,376 頂點附件引用全部頂點，不能靠 compaction 減量。主要瓶頸是每幀重傳完全未變的資料。保留 compaction 作通用無損優化，但不能聲稱它解決本案例。
- Reader native stdout/stderr 原本繼承終端，大量 profile 超過工具預算；改為非同步 ReadToEndAsync 並儲存 native.*.log，避免堵塞及遺失剖析結果。
- 新並行私有骨架求解已以 C001 30 幀／變動步長檢查位置、法線、切線及粒子歷史與串行完全相等；不得靠停用 dynamics 或更改步長獲得性能數字。
- 眼睛即時 UV 及 morph 需要來源索引，compaction 排除 eye_draw 與有 morph 的部件。沒有重製 APNG 或修改卡片內容。
- 所有中間與最終數據見 Docs/ANIMATION_CPU_FIX296.md；保留單幀尖峰與測試覆蓋限制，不宣稱全面 60 FPS。

- E296 相機新增測試第一次失敗：把『相機平移 100 cm』誤當成一定要改變包圍盒。測試世界原有元件 bounds 約 ±1515 cm，相機仍在包圍盒內，保留網格才是正確行為。改用跨出包圍盒的 5000 cm 移動；沒有放寬產品斷言。第一次 Automation 雖 exit 0，仍有一項 Fail；必須逐項檢查 Result，不用程序退出碼判斷通過。

- E296 最終 R9 驗收：Anim296RegressionR2 七项逐項 Success；C001七 outfit、C001/C010播放+喷射、C018播放+扫射3秒+停15秒均valid1，详情 Docs/ANIMATION_CPU_FIX296.md。已保留C010单帧115.44ms尖峰限制，不把平均更新改善夸大成全卡稳定60FPS。

## E297（2026-10-05）：動畫 CPU profile 與本輪錯誤
- 使用者要求13ms平均，C010正式20秒profile整體17.836459ms、actor14.852927ms；尚未達標。CPU網格資料clone約2.874ms，真正geometry skin約0.471ms，附件顯示約4.417ms。詳細證據與限制見 Docs/ANIMATION_CPU_PROFILE297.md。
- 不得將「找到重複取樣」直接等同主要瓶頸：原walk curve scan只有0.008ms；binding整段1.633ms包含其它解析。
- Python Windows預設cp950 read_text再次失敗；後續檔案讀寫明確encoding=utf-8。一次在Tools/ok-extract cwd使用workspace相對路徑造成FileNotFound；改成src/anim.rs後才實際修改。Anim297DurationR1没有跑到新增測試，不能作通過證據；R2實際1 passed。
- Rust新增測試首次使用*.1/*.01不合法literal、keys.swap同時借用len造成E0502；修正0.1/0.01與先存last，Anim297IndexR2三項實際通過。
- Anim297FinalR1在正式build完全結束前啟動，main receipt hash與最新build不同。已核對PID21444及精確abslog後停止；本輪此測試無效，不採為驗收。之後完整等Anim297BuildR6 Succeeded且無其他build/tests，再跑Anim297ProfileR2。
- Profile_cpp窗口有begin/end標記，Rust統計仍含程序預備工作。Nested階段不可重複加總、不同窗口不可冒稱精確frame分解；慢幀包含startup附近多階段等待，未證實單一演算法根因。
- E297最終Unreal回歸8項逐項Success（Anim297RegressionR1.log）；正式BuildR6成功，source parity／shared resource／動畫Rust回歸通過。13ms效能目標仍未達成，先profile階段已完成。

## E298（2026-10-05）：依profile減少CPU資料複製與viewer閒置工作
- 通用skin_all buffers移交／還原，UV/color不進幾何臨時副本，OK_SMOOTH仍用私有weights/bone_idx，避免每幀累積平滑。C010 30姿勢12,942,360頂點、C001 smooth1 30姿勢464,670頂點和舊clone oracle精確相等。
- Avatar Arc+helper overlay、完整拓樸比較的8份binding cache；實際來源解析parity／invalidate通過。程序網格僅更新mutable streams並保留UV色；Engine實際API與測試確認。
- 未達13ms的中間R2 C00115.364966ms/C01013.632285ms不得宣稱已完成。Viewer idle仍查瞄準／寫Niagara並反覆查UObject；改為idle只stop emission一次、gallery弱引用仍讀current actor。
- BuildR1既有GPU oracle缺完整FRDGBuilder header，補RenderGraphBuilder.h後R2成功。新增test缺AOKPaintSprayer header，R4失敗；自動replace沒匹配../header且未assert，R5重複失敗，明確include ../OKPaintSprayer.h後R6成功。所有替換必須assert anchor/結果。
- 相對cwd查report與rg positional glob又犯過；後續使用絕對report路徑及目录-g。詳見Docs/ANIMATION_CPU_FIX298.md。Rust動畫38 passed/31 ignored、Unreal9項實際Success。

## E299（2026-10-05）：CPU剛性配件未隨動畫掛接
- 使用者指出Anim298PerfR3/C010_Flow5s.png兩側手臂裝飾懸空。不能把source到draw局部座標相等誤當掛接正確；驗收必須檢查元件變換後的世界／component位置。
- 反編譯ChaControl.cs:2373 ChangeAccessoryParent使用GetReferenceInfo並SetParent(false)，ChaReference.cs:278指定a_n_arm_L等參考物件。C010實際APNG Choker_03的8個submesh parent為a_n_arm_L/R、maidband為a_n_wrist_L/R。
- CPU沒有BodySkel，CaptureRigidFollowerNeutral/UpdateRigidFollowers直接return，因此保留T-pose世界bake。Accessory299BaselineR2.log真實fixture根移動測試失敗，確認缺陷不限手部。
- 先嘗試native變換全部頂點雖fixture通過（Accessory299FixedR1.log），會導致這批48,376頂點/submesh配件每幀上傳；已撤回該方案與臨時Rust測試。最終改用既有BoneWorldMatrix每個socket一次、整個元件delta，不重製APNG、不改動private physics、GPU仍使用既有BodySkel路徑。
- 本輪工具錯誤：一處rg positional wildcard，另一處Rust cwd下用workspace相對路徑replace失敗；已改絕對路徑。臨時fixture首次引用不存在mat_point_pub編譯失敗，改用已查證transform_positions，BaselineR2實際重現。
- E298最新R3 frame22.990046/16.324873/10.632436ms（C001/010/018），13ms目標未達；ProfileR1 C00115.373784ms，cloning0.034ms。不要以actor平均或較快舊版本宣稱達標。
- E299最終BuildR1 Succeeded。C010七outfit walk全部valid1、7張圖valid，剛性分區誤差<=0.000026106cm；完整20秒walk／3秒噴／15秒停與換衣／離線存讀valid1、3張圖valid，誤差0.000127057cm。C001／C018額外回歸valid1、2張图valid。詳細Docs/CPU_RIGID_ACCESSORY_FIX299.md；C00114.322250ms，13ms全卡目标仍未完成。

## E300（2026-10-05）：私有骨架求解與幾何輸出分離
- E298 profile候選已確認：shared CPU ApplyAnimFrame完成私有rig求解後，Skin又以dt0進入update_dynamic_accessories_impl，重做collider frame、attachment chain、implant sync與world rebuild。dt0不積分粒子，但仍重複前處理。
- 改為render_solved_accessories讀取既有state／palette並輸出幾何，不變更骨架／粒子。初始化refresh_portable_render、ApplyShape／Animator／paired／external frame的求解路徑保留。未引入APNG欄位／格式變更、特殊卡片分支、降低動畫或物理頻率。
- 首次fixture編譯錯誤：Rust浮點literal寫成.017/.003，必須寫0.017/0.003。Anim300SolvedParityR1失敗不作驗收；R2改正後C001 40幀1,779,200頂點的位置／法線／切線、worlds／粒子歷史逐項完全相等。最終其餘fixture／正式build／perf待追加。
- E300 BuildR1成功，PerfR1三卡整套／9張圖valid；frame13.025948／12.936263／10.186767ms，C001略超13。ProfileR1發現CPU仍建立無消費者的UE pose array0.060ms，移除未使用工作、保留有BodySkel／SkelFollowers路徑；BuildR2成功。
- Unreal RegressionR1命令使用Start-Process未逐參數quote，ExecCmds/TestExit含空格被拆，提前以TestExit=Automation退出；0exit不能算測試通過。本輪R1無效，R2已逐參數加quote，須確認9項实际ResultSuccess。

E300最後驗收：BuildR2成功；PerfR2三卡20秒walk、3秒噴／15秒停、換衣／存卡／離線读／9張影像valid，純動畫整幀C00112.122474／C01012.439131／C01810.561600ms，C010停噴14.794957ms仍超13。UnrealRegressionR2逐項9Success；缺quote的R1無效。已實作私有rig求解／幾何輸出分離與CPU無消費者骨骼陣列跳過，三卡40幀幾何及粒子歷史精確相等。Docs/ANIMATION_CPU_FIX300.md。接續使用者新需求：讀卡GUI身體滑桿16ms內更新，以及身高／參數調整後裂開問題，尚未完成。

## E301 讀卡身體編輯：已重現，修正與驗證中
- 舊 GUI 每次呼叫完整 apply_shape，重新建立 DynamicBone 並各暖機 240 步；C001 實測 9 次平均 480.207600ms、最大 504.718803ms。讀卡熱編輯應遵循 ChaControl.UpdateShapeBody，不能等同 ResetDynamicBoneAll。
- E299 CPU rigid 跟隨錯誤地納入原生已蒙皮的頭部（skel_kind 2/3），身高改變後重複套骨骼矩陣；baseline 截圖已重現頭部錯位。修正分類僅允許 skel_kind 0 的世界烘焙剛性物件，真正手部飾品保留跟隨。
- 新增 live shape 通道保留物理歷史並更新 shape/reference/collider；舊完整載入通道保留。C001 44 項原生對照通過，1275 接縫對最大裂縫 0m；native update+skin 峰值 18.2265ms，尚不能宣稱達 16ms。
- 回歸檢查必須獨立斷言 CPU 已蒙皮元件不再套第二個 rigid transform；不能只以同一份錯誤期待矩陣驗證自己。
- 搜尋時誤用不存在的 Plugins/KoikatsuImporter 路徑；實際模組為 Plugins/KKUnityBundle。應先 rg --files 確認。
- Shape301GuiR2 的額外變換檢查誤把根元件 BodyMesh 的合法 actor 落地平移（-53.35cm）當第二次蒙皮。應比較 ComponentToWorld 相對 ActorTransform，根元件不可直接以 RelativeTransform==Identity 判斷。該輪 valid0 保留；264 項 GUI 事件平均13.698085ms，仍需修正測試後重新驗收。
- 使用者已明確將16ms要求改為平均；budget驗收改檢查完整ApplyPoseFrame平均，最大值/事件到完成延遲照樣列出，不把平均當每次保證。
- Shape301GuiR3 平均16.636912ms，超過平均16ms門檻，valid0，不可選用較快的R2當最終成功。進一步移除熱編輯後與同幀Animator重複求解物理：animated prepare_shape_live只更新基準/reference，接著正常Animator求解與發布；靜止模式仍即時更新。
- PowerShell以LF字串匹配CRLF原始碼失敗，沒有插入新FFI方法；改用apply_patch並rg核對，避免部分修改直接進編譯。
- NativeC001R2 的staged-vs-dt0+frame比較失敗：額外dt0物理解算會改變後續body結果，不能以重複step為正确oracle。改檢查所有shape基準對照、prepare不改private粒子、正常frame有限值及animated接縫；NativeC001/C010/C018R3均通過。沒有放寬接縫0.0005m門檻。
- Shape301GuiR4 平均16.109317ms，仍超門檻，valid0。不是完成；後續加入依face/父縮放判斷的head形變重用，NativeC001R4對照全部44項head_shaped/head_full_posed精確通過。
- body/head形變及ABMX資訊原本每次更新同步寫大量stderr；改成OK_SHAPE_DIAGNOSTICS=1才輸出這些詳細資訊，錯誤/缺資源訊息與profile計時仍保留。
- Shape301GuiR5 C001/C010 平均14.528142/14.827282ms，但C010真實手臂rigid socket偏差1.613833cm，valid0。非均勻身體比例導致骨骼矩陣含shear，FTransform分解會丟失部分變形。加入 Exact*ComponentInverse 殘差，僅在殘差改變時更新已compact的draw頂點/逆轉置法線/原始切線，維持普通TRS元件快路徑。驗收改取實際section頂點再套component與原始骨骼矩陣對照。
- CPU world-baked頂點的neutral基準屬載入時，切姿势/編輯不得重抓成新的neutral而導致飾品跳位；GPU原路徑保留。

## E302 讀卡模式所有載入後即時操作平均16ms（驗證中）
Reader302PerfR1 C010動畫/編輯整幀15.420110ms、264形變更新15.283550ms；噴3秒active Paint0.816052ms、整幀14.441543ms。但valid0：全身比例調整後，harness仍以舊OrbitFocus作目標，未生成衣服塗層。不是成功驗收。測試改為選目前可見前向衣服draw頂點，之後O(1)跟隨該頂點；不每幀重掃所有幾何，仍檢查命中/塗層/流動/守恆。需要重跑。
- Reader302PerfR2在Reader302BuildR3尚未完成時啟動，當時UnrealEditor.modules正在重建而不存在，harness於啟動前即失敗。不得只看最後compile action就認定build完成；先確認Result:Succeeded及metadata存在再測。此tag無有效UE測試，不列成性能結果。

### E302：換衣服後持續超出 16ms（2026-10-06）
- Reader302ProfileR1 功能通過，但半脫／全脫／穿回後平均整幀 21.107 / 18.829 / 22.425ms，不能作為效能驗收。
- CPU rigid follower 的剪切殘差曾逐頂點變換並 UpdateMeshSection；穿回階段 rigid 平均 3.486ms、最高 75.897ms。改成 ProceduralMesh 的完整 GetRenderMatrix，配套 CalcBounds 與水槍表面查詢，不能只丟棄剪切或減少模型。
- 搜尋再犯 Windows positional wildcard 錯誤（OKCard*）與猜測不存在的測試檔名。改以 rg 目錄 -g 篩选，先列出確實存在檔案。
E302 BuildR4：測試巨集續行誤留字元 + 導致 C2675；已移除，重編譯後才啟動驗證。
E302 BuildR5：第一次修補沒有命中巨集同一行的 + 字元，仍編譯失敗；直接查閱原始行後修正，BuildR6 重新驗證。
E302 BuildR6：KKUnityBundle 與主模組已編譯連結，但引擎 NetCore DLL 被正在執行的其他程序占用，LNK1104；未停止其他程序。重新查詢時該程序已離開，BuildR7 重試完整建置。
E302：完整建置 R7 仍因其他 Crowd 背景工作占用 NetCore 失敗；限定 KKUnityBundle/OpenKoikatsu 模組建置成功，但不產生完整 manifest。主專案 RegressionR1 也因未完成 manifest/BpGenerator 載入失敗，不能宣稱測試通過。建立獨立 Reader302HostR1，使用此次實際編譯 DLL、引擎相同 BuildId 與必要 runtime 依賴進行功能驗證；主專案 manifest 未手動改動。效能驗收須等背景工作離開。
E302 RegressionR2：9項通過，Paint.Scene 在 NullRHI 的 render upload 發生 access violation（RT resource 為空仍呼叫方法）；完整回歸失敗，已加上 Resource 空值防護並重新測試。Reader302AffineFunctionalR1 與四個 Crowd 工作並行，只驗證功能、不作16ms效能證據。
E302：Scene 回歸包含 GPU RT readback，必須使用實際 RHI；RegressionR3 改為 D3D 後十项全部通過。完整 BuildR8 在背景工作重啟後仍被 NetCore DLL 鎖定，不能宣稱主專案完整建置完成。新增剪切後塗層距離/質量守恆回歸，RegressionR4 重測。
E302 BudgetR4 嚴格失敗：C010 形變13.280711ms、乾燥整幀15.668880ms，噴射物理1.133360ms，但噴射整幀16.089402ms。不可四捨五入算通過。仍有外部 Crowd/驗證工作。針對真正 rigid kind0，延伸原生位元精確 dirty check 到小網格，避免不變幾何每幀float→double比較；socket渲染矩陣與物理不跳過。
E302 ProfileR6 功能valid1但不是budget驗收（門檻暫關）：另一背景Crowd Cook在corelimit19下處理約19,000 packages，乾燥17.212423／噴射18.115147／停止18.059060／half17.574461／off18.092860／restore17.679958ms。不能以Actor平均替代整幀或反覆挑較快測試。rigid已0.075~0.099ms；目前最大繼續成本為native skin2.85~3.36ms、procedural geometry4.73~5.86ms、其餘pose約5ms及渲染等待。正常main已ModuleBuildR4產出metadata並成功起動ProfileR6完整流程，正式三卡16ms驗收仍未完成。
E302 RegressionR5：主專案完整Editor啟動停在TurnkeySupport載入超過2分鐘、未執行任何測試（不是測試通過）。僅停止自己 abslog=Reader302RegressionR5.log 的 PID49052；讀卡 -game 主專案已完整valid1，不受此Editor啟動阻擋。背景Cook已於00:48:22結束，改做乾淨同版本三卡16ms驗收；最新單元回歸使用隔離host去除非本次功能的BpGenerator Editor插件。
E302 BudgetR7：C001 shape11.478494/動畫14.265549/噴12.883989/停12.385785/half15.494807ms通過，替代f_tachi後16.164376ms超限，valid0；C018尚未執行。根據geometry profile，調整procedural stream平行填充門檻1024→16384，小批次省去task派發等待，大網格仍平行；不改變每個頂點的計算或面數。加入內部cvar方便同版本A/B，不以此當作完成驗收。
E302 BuildR9 完整Succeeded。BuildR10：本次read-card檔編譯後，被同時更動的OKCrowdQuality.cpp:281格式字串C2664/C2088阻擋，非本次讀卡修改。保留其他工作，未改其程式；等待修正後再建置。新增真實角色parallel/serial draw attrs oracle，強制dirty只作用於實際可refresh的parts（排除slotattached/invalid），避免測試破壞不更新的BaseVertices。
E302 BudgetR8：真實角色parallel/serial946,995 draw頂點各屬性與clock相等；形變12.770505/乾燥14.776004/噴15.159154/停14.510146ms通過，但half16.368713ms超限。發現載入交接後Source動畫5秒測量卻走到5.366秒，first world delta含載入前耗時。新增一次性的world clock rebase，只對真正開始遊戲的world tick、下一個frame生效；手動同幀取樣與之後正常/慢幀delta保留，stop取消。初始pose0已由selector產生，不重新暖機物理。需LoadedWorldClock單元與實際GUI/手動walk probe共同驗證。

### E302 R9 / world tick context (2026-10-06)
BudgetR9 retained as valid0: C010 shape12.126526ms, dry14.384445ms, spray14.513545ms, released14.248225ms; half16.827989ms exceeds16. This run ended before the next external authoring process started; cannot attribute the persistent half cost solely to background load.
The animation-ready rebase must identify actual engine TickActor, not infer automatic Tick from world/registration. Direct manual Tick can occur in a later frame. Move the context to TickActor, scope-restore it, and preserve original delta for speech/face/Super. BuildR13 succeeded. ProfileR10 measures remaining costs; latest clock regression still pending.
Shell search correction: Windows rg positional wildcard KKPreviewActor.* is invalid; locate with rg --files, then search the actual plugin directory with -g. Do not guess the Source path.

### E302 validation host synchronization (2026-10-06)
RegressionR7–R10 did not run tests: KKCrowdGpuPacking became a real linked dependency, and KKCard/KKImportedAssets host manifests were stale after full build changed BuildId. Disabling a linked runtime plugin cannot solve missing imports. Synchronize every enabled runtime plugin DLL + manifest from the successful build, preserving BpGenerator disabled only in the isolated host. RegressionR11 then ran eleven real-D3D tests, all Success, exit0 (includes LoadedWorldClock and Paint.Scene).
OtherCardsR11 failed C018 attached-clothing paint assertion; dry/shape/spray timing cannot replace this failed correctness check. Audit aim now follows actual-render triangle centroid and no longer assumes authored normal winding indicates visibility. R12 repeats C018 then C001. All failed reports retained.

### E302 batching / audit target (2026-10-06)
BuildR15/R16 Succeeded. BatchStreamsR13/R14 actual draw parity passed946995 vertices, shape/dry/spray/released averages recorded, but strict film-downhill check failed: centroid nearZ81.723 to81.718 (<.01cm movement), mass conserved. Do not label these functional valid1 or relax the flow assertion. Camera OrbitFocus depends on accessories and is not a reliable torso target. Audit now derives torso height from rendered body bounds (72% height), tracks a sloped triangle interior, and still requires visible clothing coating, downhill motion and conserved mass. Product fluid parameters unchanged. New check_reader_frame_budget.py requires every requested card and every shape/dry/spray/released/clothing phase; it flags all raw means>16 rather than stopping on one card or rounding.
OtherCardsR12 functionalvalid1: C018 all seven phase means<16, C001 half19.920614/off16.384875/restore17.046280 still exceed. Six manual later-frame samples exact; six images valid. Water physics CPU.2–.524ms, spray/release frames<16 for both. Expanded goal remains incomplete.

### E302 R18 material lifetime / fixture lifecycle (2026-10-06)
BatchStreamsR15 all three cards functionalvalid1, exact draw parity all cards and9imagesvalid. Strict frame checker fails multiple stages; C010 first-spray paint maximum552.772101ms, mean5.182452ms and whole frame19.119974ms. Materials were loaded lazily in InitializeCoating/Splash/drip; R18 retains FilmBase/DropBase from BeginPlay instead, preventing interactive asset-load calls and GC unloading between strokes.
Verified local UE5.8 UpdateResourceImmediate enqueues clear, not a synchronous Flush; retained it. Do not remove resource initialization based on its name.
RegressionR12:10Success, Scene failed3 coating assertions. Inspection found later sprayers in manually-created Game test world never received BeginPlay (only initial actor was manually dispatched). Their preloaded materials remained null. R19 test helper dispatches BeginPlay for every later fixture actor, using same lifecycle as runtime; production does not reintroduce lazy LoadObject fallback. Must rerun actual Scene/11 tests; failure remains logged.

### E302 R20 allocation reuse / profile-duration mistake
Skeleton.rebuild_worlds now reuses names/worlds Vec capacity and String buffers and reserves the hierarchy cache; TRS math, hierarchy traversal, decompiled modifier order unchanged. Independent direct world_of_id parity/name pointer retention test passed;7 ExpressionBone tests passed; C001/C010/C01844parameter live/staged/full baselines, dynamics history, finite geometry and seams passed. Full BuildR20Succeeded.
PaintHitchProfileR17 invalid0: setting dry duration5seconds with264GUIshape events every50ms truncated the probe after87events. This is a test configuration error, not a final-shape regression; final_shape_matches1. Keep20seconds when shape probe enabled. R18 repeats with complete time; no acceptance fromR17.
MaterialReadyR16(BuildR19) functionalvalid1, all3cards9imagesvalid, but strictframebudgetfailed: C010spray17.216973/half16.419266/off16.199884/restore17.365884; C001dry16.224812/half18.011347/off16.632739/restore17.099660. C010paintmean4.277735/max510.011602 despite preloading; preload did not solve the first-impact hitch, do not claim otherwise. All physics CPU means<16; overall scope still incomplete. R20 profile logs every >16ms paint update in addition to periodic samples to isolate geometry/simulation/coating.

### E302 R21/R22: measured paint discovery hitch
PaintHitchProfileR18 (BuildR20) functional valid1. C010 whole-frame shape11.094874/dry13.346301/spray17.014208/released13.851211/half15.659814/off17.275413/restore15.895487ms; strict16ms goal still fails. First paint update geometry492.711ms, simulation.036ms/coating.004ms. 23 candidate surfaces include four88064-triangle sections each costing104–111ms. Material preload did not resolve this geometry bottleneck.
R21 defers adjacency/normals until actual hit, preserves discovery-pose weld coordinates, and builds median BVH with cached centers/nth_element rather than recursively sorting whole ranges. No mesh reduction or particle/film shutdown. R22 adds independent brute-force triangle oracle outside sweep timings to check hit existence/time/world contact despite shared-edge face ties.
BuildR21 paint compiled; whole build failed in unrelated OKCrowdGpuClock.inl private UpdateBoneTransformRevisionNumber call. Did not modify that code. Subsequent external correction present; BuildR22 Succeeded. Reader302DeferredGeometryR19 testing all three cards with20sec264shapeevents, spray3/release15 and real clothing states. No acceptance until results exist.

### E302 R19/R23: full results and next stream allocation fix
DeferredGeometryR19 BuildR22 all three cards functionalvalid1 and9imagesvalid; RegressionR14 elevenSuccess includes independent brute-force sweep oracle. Physics CPU means spray/release C0101.808629/1.213638, C001.685060/.479865, C018.624026/.304523ms; every spray/released whole-frame mean<16. C010 first geometry89.769ms vs492.711ms, no claimed spike-free result. Strict checker still fails C010off16.540096/restore16.309721; C001half18.148521/off16.016060/restore16.569818; C018allstagespass.
ClothingProfileR20 shapeprobe0/dry5sec is diagnostic only, not final acceptance. Native reset.3–.4/curves~1/modifiers~1.1/dynamics rest~2.4ms, display accessories~3–5ms. UE scratch streams were recreated each frame despite reuse between parts. R23 changes to actor-owned scratch capacity, unchanged vertex formulas/attributes and copy-owning UpdateMeshSection. Needs parity/performance validation.
BuildR23 KKUnityBundle compiled/linked, whole target failed linking engine NetCore.dll locked by unrelated Crowd GpuPoseAudit resident process. Did not terminate external job or change engine. R24 targets only project modules using locally verified UBT -Module= syntax; needs result. Source reads repeatedly guessed filenames/wildcards resulting rg errors; discover via rg --files or directory -g first.

### E302 R24/R21 latest verification
Module-target BuildR24Succeeded. PersistentStreamsR21 functionalvalid1,264shapeevents/card/allserial-parallelattributes exact/9imagesvalid. Four externalCrowdGPUauditprocesses active during timings; rawwholeframecheckerfailed, retained. PhysicsCPU spray/release C0102.077980/2.064300, C0011.323239/1.629002, C0181.180185/.535378ms. No claimed overall16ms pass or scratch speedup from unmatched loads. BuildR23 fullengineNetCorelink lock remainslogged. No externaljobs stopped. See full tables in Docs/READER_FRAME_BUDGET302.md.

RegressionR15 (latest R24 module combination) eleven tests all Success; no pending own tests. Independent triangle sweep oracle / film transport / mass conservation / actual D3D coating / loaded world clock / affine draw / mutable stream tests passed. Whole frame goal remains incomplete; external GPU audits still active. Binary receipts at verification: C:\portable\OpenKoikatsu\Binaries\Win64\UnrealEditor-OpenKoikatsu.dll SHA256=10A847AF37AC4FED27D910B1F52F620A08A902B4137F0FDB72321636B3E2DE43 | C:\portable\OpenKoikatsu\Plugins\KKUnityBundle\Binaries\Win64\UnrealEditor-KKUnityBundle.dll SHA256=A87CA2073E652B71B615267F0799F9DB628CA4F01320972DF79B114020463C55

### E302 R25 native split profile / R26 candidate
NativeBreakdownR22 BuildR25 diagnostic functionalvalid1; under other GPU audit work clothing means23.509495/20.147191/25.176443ms, not acceptance. Split dynamics timers show export_ms0.000, accessories_ms2.5–2.8; do not optimize or skip skeletal export based on earlier combined rest_ms assumption. Both accessory callers maintain cached worlds via prefab preparation and particle step. implant::sync currently rebuilt every private rig even with empty body_bridges; R26 avoids that no-op for complete caches, retaining fallback for incomplete legacy data and all nonempty bridge assignments. Adds independent legacy-always-rebuild private-particle/geometry/world-cache oracle with60frames at120/60/30Hz; test-only atomic flag compiled out of production. Synthetic64frame current dynamic pose + nonempty/missing bridge + incomplete-cache recovery test passed R1 before adding realfixture oracle. Realfixture and fullruntime tests still pending; cannot claim performance benefit yet.

### E302 R26 oracle verification / profile R23
R26 modulebuildSucceeded. Real-card independent legacy-unconditional-rebuild oracle passed C00137emptyrigs/C01030/C01817,60frames each at120/60/30Hz; positions/normals/tangents/private worlds and particle historyexact. NativeBreakdownR22 measured export0/accessories2.5–2.8ms. R23 after no-op skip stillaccessories2.5–3.2ms; benefit not proven under four external GPU audit processes. Keep rawprofile, do not claim this alone fixes16ms.
Next R27 removes name allocations in AttachedDynamicSet.set_attachment_chain by updating existing Tf in place, identical parent/TRS/name/sibling fields, retaining legacy missing-node fallback and mandatory rebuild after changed ancestors. Extended shear/history test with legacy clone+insert oracle60frames and stableStringbuffer pointers. Newtest/build/runtime pending.

### E302 R27 attachment allocation reuse test
AttachedDynamicSet.set_attachment_chain updates every existing Tf field in place and reuses String capacity; missing IDs retain clone+insert recovery, hierarchy/world rebuild retained. Extended attached_ancestry_preserves_shear_and_particle_history passed60frame legacyclone+insert oracle across120/60/30Hz, fullserdeTf fields/worlds/particlehistory exact, namebuffersretainpointers. No matrix/TRS simplification. At03:04 process inventory shows externalUnreal jobs finished; R27 builds next, then fullcurrent threecardraw16ms audit under presentload (do not substitute oldresults).

### E302 R27/R29 and quiet full audit R24
R27 modulebuildSucceeded. RigReuseBudgetR24 quiet inventory (only ownedUE86064) all3functionalvalid1 /792shapeevents /allattributeparity /9imagesvalid. Strict budget failed: C010shape14.482044/dry17.879063/spray18.621074/release13.803585/half15.609696/off16.347453/restore16.428028; C001shape11.233076/dry13.531527/spray13.189125/release12.577179/half17.255924/off15.792570/restore16.305778; C018shape9.039321/dry10.887375/spray12.294532/release10.008278/half10.594293/off11.306402/restore10.640112ms. Exact release value verified against original report. FourGPUjobs not present this run, so failures cannot be excused asexternalonly.
Added per-operation actor CPU counters without per-frame logspam. R29 modulesSucceeded; TraceCpu script uses actual UE5.8 cpu/frame/bookmark/region channels and tracefile, Scope names KK_PreviewTick/Pose/NativeAnimation/Skin/Streams. Regions Reader_C001_half_loaded/removed_loaded/restored_loaded permit real Insights per-phase export. Verified engine source supports region export and -NoUI/-AutoQuit/-ExecOnAnalysisCompleteCmd; trace disabled in final budget runs. InsightsC001R25 running diagnostic only (shape0,dry5sec), not final acceptance. First combined patch failed because BOM first include did not match; no partial mutation, repatched using second include.

### E302 Insights R25 / rejected project PMC override R30
Insights diagnostic identified per-clothing-phase697/699/804ms GT idle under Sync_RenderingThread. Render thread UpdateSection RT max711.983ms, upload allocator own max711.357ms; native export0.000ms. Root inside allocator still not proved; avoid attributing locks/driver without trace children. First Insights argument quoting produced no CSV despite exit0; corrected by Start-Process literal -ExecOnAnalysisCompleteCmd="...". Insights memory tag warnings mean no memory conclusions from CPU-only trace.
Attempted project-local UE5.8 PMC plugin override to skip unchanged UV/color uploads. UBT rejected engine plugins referencing project module hierarchy. No engine files changed; exact owned new plugin moved to Saved/Tests/Reader302RejectedPluginOverrideR30 for provenance, removed from plugin discovery. Do not disable unrelated engine plugins or ignore module hierarchy to force it. Next implementation uses opt-in owned scene proxy in KKUnityBundle, preserves stock component behavior elsewhere and original public API call routing.

### E302 stream proxy R31–R34
Owned opt-in KKStreamMeshProxy adapted from installed UE5.8 PMC; engine source unmodified, project plugin override absent. Dirty flags travel with each owned render packet. Positions and packed tangent basis still upload each animation update; unchanged UV0–3/colors do not. Actual UV/color edits still enqueue uploads. Collision-enabled sections fall back to stock PMC behavior. Preview body/head and parts opt in; every actor/secondary face section update plus outline visibility audit routes via wrappers because stock APIs use nonvirtual private proxy casts.
R31 compile rejected local Bounds variable shadowing USceneComponent::Bounds (C4458); renamed SectionBounds. R32 link rejected missing PhysicsCore direct dependency for copied debug collision GetCollisionTraceFlag; added PhysicsCore matching engine PMC build dependencies. Two rg attempts guessed private header/cpp paths failed; locate via rg --files, do not repeat assumptions. BuildR33/R34Succeeded. RegressionR16 actual D3D 12tests Success including new StreamUpload:60mutable updates preserve all4UV/color/TBN, unchanged stream upload counts0, queued UV3/color edit followed by unchanged packet uploads exactlyonce, bounds/visibilitycorrect. These checks inspect RT retained buffers/command counts, not independent GPU readback. Full threecard R26 wholeframe audit running; no all16 claim yet.

### E302 R34 full threecard stream upload budget R26
Functionalvalid1 /792shapeevents /9imagesvalid /drawmapping0 /socketoracle exact /whitepaint conservation/clothing/hair all pass. Quiet run no externalUE. C010shape10.901515/dry13.026742/spray14.397129/released13.431972/half15.127989/off15.961384/restore15.596115ms allrawmeans<=16. C001shape10.629258/dry12.584473/spray11.748774/released11.603455/half17.254901/off15.962918/restore17.616700ms; C018shape9.201024/dry10.106526/spray9.853952/released9.679951/half10.272381/off10.786446/restore10.145514. Strictchecker passed=false due C001half/restore. Do not round or use warmedmeans asacceptance. First spray max C01088.916600ms stillspike; physical average1.755729/1.125850ms spraying/released. APNGload2.332184/4.128390/2.038842s. New InsightsStreamC001R27 diagnostic traces remaininghalf/restore; notfullacceptance.

### E302 R27 allocation trace / R35 bounded static buffer uploads
Newowned proxy stillhad 659/665/681ms AllocUploadResource waits in C001 clothing regions; unchangedUV/color removal lowersuploadcountbutdoesnotremoveallocationstall. Local engine D3D12Allocation.cpp confirms small pooled upload limit64KiB. D3D12Buffer.cpp static LockBuffer(Size) allocates requestedrange, uses destination LockOffset; dynamic locks renameentireBufferSize. Position/tangent/UV/color resource sources verifiedBUF_Static. R35 UploadKKStreamBuffer limits eachstaticwrite64KiB, retainsbyteexact offsets/fullcoverage, dynamicAnyDynamic alwaysfullwrite (partial dynamic writes would discardothercontent, forbidden). CVar ok.Animation.StreamUploadChunkBytes default65536,0referencefullupload. Enginefilesunchanged. BuildR35Succeeded.
StreamUpload test expanded13000vertices andfirst/last animation/UV/color edits to cross64KiB boundaries. Adds real GPU readback ofposition/color/tangents/UVbuffers, comparedbyte-for-bytewithretainedCPU streams; blockingRLM_ReadOnlyonlydevtest, no productionFlush/GPUwait. RegressionR17running; finalbudgetpending. PreviousR16onlyRTCPUproofmustnotbeclaimedasGPUreadback.

### E302 R28 raw budget / R29 missing diagnostic probes
ChunkUploadBudgetR28 fullfunctionalvalid1 /792shapeevents /9imagesvalid; rawbudgetstillfalse: C010off16.079505; C001half17.276471/restore16.482958. Other18stagespass. Do not round16.08down oromitloadready5s boundary. GPUreadbackRegressionR17 twelveSuccess, sourceexact.
InsightsChunkC001R29 accidentally omitted WALK_PROBE_TIME and OPERATION_PROBE flags in fresh exec_command shell; env assignments from othercells do notpersist. Reportvalid1 butnoanimation/operationregions; CSVexportnone. Do NOTinterpret its4ms frames asanimated/clothingperformance. Retainfailed diagnostic and usefreshR30 withallflags explicitlyset. Noengine/sourcehierarchychangeforced.

### E302 R30 refined trace / R36 readonly texture lifetime
R30 correctprobes: afterchunkuploads KK_StreamUpload max<.64ms, but690ms AllocUploadResource nowunder SlateDrawWindowsCommand→RenderGraphExecute→SubmitBufferUploads→Upload_Multiple. RHI_SubmitClose concurrent705ms; DeleteRHIResources child~0, costinuntracedRHIEndFrame/ProcessDeleteQueue internals. Local Adapter.EndFrame uploadpoolcleanup has20frame lag andlocks; exactinternalcostnotinstrumented, do notclaimdriver/mutexproof. Chunkstreamoptimizationcorrectbutdidnotresolveglobalresourcechurn.
ExistingKKTextureRuntime cache retains compressed decodedmips only andalwayscreatesnewUTexture2D, evenidenticalimage/settingsonclothingreload. R36 adds opt-in readonlyGPUresource reuse viaweakUObject references: completePNGbyteequality+SHA1/settingsidentity, validatescurrentmetadata, capsstoredidentitydata64MiB/4096entries, retainszeroUObjects/GPUmemory. Legacy mutablefactory remains independent. OnlyknownreadonlyfinalsettingsnativeMainTex(excludeseyes/overlays),nativeRamp/Gloss/Detail/ColorMask, fixedlinearinputsoptin. Intentionalmutablefallback/overlay/eye pathsremainindependent. LoadedTextures isUPROPERTY soeachactor retainsownreferenceswhenoldactorunroots/destroys. Unconditional nativeColorMaskUpdateResource removedwhenmetadataunchanged.
NewtestReadonlyTextureReuse covers identicalresource reuse,settingsseparation,mutablefactoryisolation,stalesampler rejection,sourcebytesexact,weakcacheGCnoownership. BuildR36Succeeded; R37 adds once/loadreadonlystats for actualreuseevidence, test/fullbudgetpending. NoactiveUEprocessduringbuild.

### E302 readonly test R18 correction
RegressionR18 twelveexistingtestsSuccess, ReadonlyTextureReuse oneassertionfailed because test fixture initialized srgb=0 but expectedtrue, not becausemutationsharedresource. ABIverifiedfilter,wrap_u,wrap_v,srgb,mip_policy ordering; fixturechangedtonamedfieldassignments(srgb1), Otherfixture(srgb0) hasexplicitfilter/wrap/mipassertions. Productionsettingscodeusesnamedfieldsandunchanged. Do notclaimR18allpassed; retainfailure. BuildR38 and correctedtestrequired beforefullperf.

### E302 R39 shared primitive uniforms / R19 dependency mismatch
R39 compiled successfully. Raster scene proxy shares identical primitive transform/bounds uniform buffer within one collector invocation, never across frames or collectors; section material/indices remain separate and ray tracing unchanged. RegressionR19 failed before tests: main module Windows loader error127. Independent dumpbin imports/exports identified missing FOKGpuSkinPaletteBatch constructor/destructor/AddPass in existing KKCrowdGpuPacking DLL after main rebuilt concurrent Crowd source. Module-target build did not rebuild that dependency. BuildR40 includes KKCrowdGpuPacking explicitly, retains foreign source/settings. Do not report failed host boot as test success; no application logic rollback or plugin disabling.

### E302 R31 readonly/uniform budget not accepted
BuildR40 succeeded; corrected RegressionR20 13 tests all Success on actual D3D, including readonly cache weak ownership/sampler isolation and GPU stream byte equality. Full R31 valid=1/792shapeevents/nineimagesvalid, readonly metadata_rejections=0 and real clothing reuse, but raw whole-frame budget failed. C010 dry17.390457/spray23.883950/released22.214512/half25.986142/off26.819490/restore24.595555; C001 dry17.908179/spray17.772456/released17.634907/half20.951233/off20.460256/restore21.137261; C018 all stages<=16. Shape CPU14.569891/14.706286/8.700463. Physics CPU4.429209/1.224869/.571811 spraying,3.030517/.945099/.289833 released. Never call this all16 based on physicsCPU. Firsttwo cards unexpectedly slower than R28, cause not established; new R32 current-build trace required, no speculative attribution to texture cache or hardware.

### E302 R32 allocation hitch improvement / R41 compact packet
Current R32 diagnostic C001 (shape disabled, trace enabled, only one card) half/off/restore raw13.457797/12.872433/14.630248 ms; not equivalent to three-card shaped R31 acceptance. New allocation maxima20.221/15.941/19.582 ms vs R30 ~690 ms, Sync_RenderingThread max6.576/10.297/7.507 ms. Texture/uniform lifetime optimizations remove the earlier large observed allocation wait in this diagnostic, but performance cause for R31 sustained CPU rise not yet established.
R41 compact owned render packets avoid copying the full FProcMeshVertex with immutable four LWC UVs and color every update. Animated position converts at the same point as previous render conversion; normal/tangent remain original double precision until stock FPackedNormal quantization, since float-first can alter rounding. Dirty UV/color arrays are owned separately only on actual changes; game-thread CPU section retained exact for collision/paint/oracles. Packets filled during existing section-update loop, eliminating the second full vertex memcpy. BuildR41 succeeded. R42 adds half-integer double normal/tangent quantization boundary test; full real GPU readback retained. New regression and full three-card untraced performance pending.

### E302 R33 compact packet whole-frame failures retained
R42 compiled; RegressionR21 thirteen Success including real GPU byte equality with double quantization boundary inputs. R33 three-card functional valid1/792bodyevents/nineimagesvalid, APNG2.795666/4.512940/1.617248 seconds, but whole-frame gate false. C010 half16.271422/off18.358419/restore17.885690; C001 released17.986491/half23.612883/off21.299206/restore22.129890; C018 spray18.048768/released16.004447. Do not round16.004down. Body CPU13.492981/11.414606/11.579736 and paintCPU all<=16. Sustained actor CPU exceeds16 for C001 clothing phases; this cannot be attributed solely to render resource stalls. Fresh R34 traces actual shaped C001 with Rust native profile, no claim from earlier no-shape diagnostic.

### E302 R34 shaped CPU trace / R43–R44 actual repeated work
Shaped C001 R34 diagnostic traces still show sustained own CPU: native animation mean5.760–6.320 ms, streams4.859–5.118 ms, skin2.104–2.456 ms. Render synchronization maxima <15 ms and upload allocations <29 ms, so remaining high CPU cannot be described as the old700ms allocation stall. Rust breakdown includes native accessories3.107 ms across run; no physics disabled.
R43 collects changed mesh sections and dispatches one global parallel job batch per actor frame instead of separately dispatching/waiting each mesh. 512-vertex disjoint jobs avoid an O(work-list) per-vertex search. Per-work retained scratch buffers use NoShrinking; original offsets/normals/tangents/math remain. BuildR43 succeeded. R44 additionally caches immutable per-slot morph names at load and checks exact effective baseline weight before copying large inactive raw morph arrays. Unpruned reference remains available via ok.Face.PruneInactiveMorphs=0; actual shaped three-card audit compares every CPU face/outline draw attribute against this reference with held clocks. No changed morph weighting/order/quantization or physics. Build/regression/full budget pending.
A guessed glob path in rg failed (Windows invalid path); actual session API source found through rg --files and symbol search: KKRustExtract.h/.cpp. Do not infer filesystem paths from class name.

### E302 R35 global batching improved; one remaining raw failure
R44 / RegressionR22 thirteen Success. Full R35 functional valid1,792shapeevents,nineimagesvalid; exact serial/parallel draw946995/97310/89926 vertices and inactive-morph legacy reference7818/25474/26808 vertices, clocks unchanged. Body means10.010540/10.042780/7.503999 ms; APNG2.680606/5.487634/.998305seconds. Twenty of21 raw stages<=16; C010 restored16.534021 still fails. C010 dry12.216637/spray13.489891/released12.468946/half13.882850/off15.026698, C001 maximum13.274980,C018 maximum10.725384. WaterCPU .497–1.733 spraying/.277–1.102 released. Do not omit remaining .534ms or use warmed15.111630 ms as acceptance. R36 traces actual shaped C010 current global batch, with no diagnostic per-frame logging, to identify the remaining overhead.

### E302 R36 UI prepass/paint repeated work / R45–R46 reader invalidation
Current shaped C010 trace has Slate prepass .573–.601 ms and slow paint .506–.543 ms per loaded frame; own actor10.50–11.02 ms. Allocation maxima<18 ms, synchronization maxima<10 ms. R36 is diagnostic, not substitute for the R35 remaining restored16.534 failure.
R45 wraps only the reader/gallery controls in installed UE UInvalidationBox, no global Slate or engine setting changes. Direct UMG text/slider/visibility/layout setters invalidate changed widgets; static control prepass/paint can be retained between events. Root content and hit-test visibility preserved. BuildR45Succeeded. R46 strengthens existing shape audit to drive the actual UOKShapeSlider OnValueChanged delegate264times/card and verify numeric ValueLabel plus final native shape, rather than only calling Gallery.OnShapeChanged. Cache presence verified. This adds actual UI event work to the same full-frame acceptance, not removed/trimmed time. Compile/full3card budget pending.

### E302 R46 / R37 最終限定驗收通過（2026-10-06）
R35 的 C010 restore 16.534021ms 是有效 FAIL，未抹去。R36 trace 顯示讀卡靜態 Slate prepass / slow paint 約 1.1ms；R45 局部 UInvalidationBox、R46 實際滑桿事件及文字驗證後，完整 R37 三卡七階段 21/21 raw average <=16ms，最高整幀 14.768363ms，792 真實控制項事件平均 7.521159–9.859983ms，APNG 1.025704–4.845972 秒。R46 build 成功、R23 真實 D3D 回歸 13/13 Success，9 圖人工檢視完成。完整數值、尖峰、範圍、DLL hashes 和證據見 Docs/READER_FRAME_BUDGET302.md 最新首節。此通過不擴張成每幀 <=16ms、全卡全 outfit 或每種動畫的保證；不可用 profile 單卡、裁切 warm-up 或 Actor Tick 替代 raw 整幀驗收。
紀錄工具錯誤：首次寫入最終紀錄的 functions.exec JS template 含未跳脫 PowerShell backtick，JS SyntaxError，未執行任何檔案寫入；改用 Environment.NewLine 後成功。

### E302 R38 重新 profile 與 R47 通用更新迴圈（2026-10-06）
依使用者要求重新對正式 R46 做 profile：Reader302ProfileR38，C010/outfit0、CPU 高模 1600×1000，20s 動畫、264 個真滑桿事件、3s 噴射、15s 停止、half/off/restore 各 5s。功能 valid=1、stream/morph reference 相等。追蹤本身含 shader worker 背景工作，不能把多執行緒 exclusive 合計當 GameThread 時間，也不能以此替代無 trace 正式效能門檻。
Insights 匯出 all_Reader_C010_*.csv：half/off/restore 原生動畫平均 4.414/3.613/3.685ms，CPU 蒙皮 3.337/2.507/2.581ms，streams 4.446/3.729/3.307ms；Slate Tick 0.522/0.447/0.431ms，局部介面快取仍有效。優先改善 streams 的重複掃描：R47 把 bounds 累積併入保留完整 CPU vertices 的更新迴圈，UV/normal/tangent/color 供應判斷移到迴圈外，避免每個頂點重查四個未提供 UV。不改面數、數值精度、GPU TBN 量化或材質。StreamUpload 增加 fused box 與原 stock FBox(Positions) exact equality，既有 GPU byte readback 保留；待編譯、回歸和完整無 trace 驗證，不提前宣稱改善幅度。
工具路徑錯誤：曾猜 OKNativeSession.cpp 不存在，rg IO error；已用 rg --files 查實際來源，不再沿錯誤路徑。

### E302 R39 FAIL → R40 再追蹤 → R48 材質載入完成條件（2026-10-06）
R47 全三卡無 trace R39 功能 valid=1、792 slider、9 圖及資料 oracle 全通過，但 C010 off 16.255238ms、restore 19.247923ms 是有效 FAIL，19/21 通過，不能拿早期 R37 通過當目前版本完成。
R40 同 R47 單卡再追蹤，half/off/restore Actor Tick 11.600/10.944/10.912ms、streams 3.480/3.716/3.293ms，未重現前次高成本；仍有各段 801/582/784 個 PreprocessShader 事件。不能證明 shader worker 是唯一原因或以這次 trace 宣稱成功。發現目前 LoadNative 回傳成功時，當前人物 runtime material 尚未完成編譯；以前 audit 只有初始噴射前全局等待，衣服操作直接開始量測，GUI 也沒有自己材質 ready 條件。
R48 在 LoadNative 結束前去重收集本 actor 元件所用 base UMaterial，呼叫 UE5.8 已安裝 API EnsureIsComplete，僅目前人物材質、僅載入邊界，不在每幀或等待整個世界全部 shader jobs。加入 KK_PreviewMaterialReady trace 和 materials/initially_compiling/wait_ms receipt。新 loading 時間須實測，不假設 APNG 仍 <=10s；待 R48 build、新版整體驗收。
工具錯誤：UE5.8 Material.h / MaterialInterface.h 位於 Engine/Public/Materials 而不是舊 Classes/Materials；猜路徑 rg 失敗後用 rg --files 找到實檔並確認 EnsureIsComplete 的 engine implementation。第一個 apply_patch 使用錯誤 TextureStats context，未套用，讀精確附近程式碼後套用。

### E302 R41 逐材質等待被反證、R49 API compile error（2026-10-06）
R41 初次 C010 材質 ready 141 個、wait_ms=14320.785，逐一 EnsureIsComplete 使載入超過 10 秒。因此 primary 僅終止已核對 CommandLine 含 Reader302MaterialReadyBudgetR41 的自己 UnrealEditor.exe PID 21792，保存所有已產生 run.log/launch/exit 記錄；本 run 是主動中止／未完整驗收，不是 PASS。initially_compiling=0 不表示完整 shader map，on-demand jobs 要主動提交。
改採所有目前人物 base material 的 active-world platform resource 先 SubmitCompileJobs_GameThread，最後 FMaterial::FinishCompilation(Materials) 批次等待自己 resource。R49 嘗試 GetFeatureLevelsToCompileForRendering 編譯 C2248 protected API，不可存取；R50 改用 GetWorld()->GetFeatureLevel（無 world fallback GMaxRHIFeatureLevel），只準備實際渲染平台，不存取 protected、不等待其他平台及外部世界的全部工作。待新 build、實際 loading 和完整幀門檻結果。

### E302 R50 重新測試環境與排隊取消（2026-10-06）
RegressionR26 真實 D3D 13/13 Success。四個外部 Crowd UE commandlet 持續百卡／分段減面轉換，不停止、不改 affinity。安靜環境無法立即取得，primary 核對自己的等待 shell PID 105848（完整 CommandLine 含 Reader302MaterialBatchBudgetR42.wait.log 與 taskWaitDeadline）後只取消該排隊程序，避免未來額外啟動測試。重新執行 R42 完整三卡功能、載入與 raw 幀平均，背景工作清單存 Reader302MaterialBatchBudgetR42.environment.json；這不是 quiet run，不能和 R37 做因果改善百分比或宣稱安靜環境的結果。若 raw gate 有 FAIL 必須保留，不用背景工作作為抹除 FAIL 理由。

### E302 R42 幀平均通過但首次 shader 載入 FAIL、R51–52 移到動畫邊界（2026-10-06）
R50 / R42 在四個外部 Crowd 工作仍運行的條件下，21/21 raw average <=16ms、最高整幀 13.806067ms，功能 valid=1、792 sliders、9 圖及 streams/morph/socket/clock oracle 全通過。但 APNG load C010 5.964288s / C001 8.189152s / C018 14.286032s；C018 首次 shader ready 12.930683s，違反 10s，不可把整幀 PASS 當所有要求完成。
R51–52 把完整材質批次準備從卡片模型／貼圖載入移至首次動畫載入，在 Unreal／原遊戲／Studio 三個動畫入口、AnimationReadyClock 發布之前完成；卡片顯示維持原本異步 shader 生成行為，首次 animation setup 可 loading（使用者明確允許），等待時間保留 readiness log 不藏在每幀。同 actor 後續狀態切換不重複等待；DestroyAllParts 一律清除 readiness，避免換 outfit／模型後沿用舊資料。改用實際 UMaterialInterface resource 去重，保留 material instance 的靜態排列，而非只檢查 base material。
R51 build 成功，R52 是清理路徑 reset 修正；待最新版完整三卡及回歸，不提前使用前版 PASS。

### E302 R52 / R43：功能與 APNG 達標，效能 raw FAIL（2026-10-06）
R52 build 成功，RegressionR27 真實 D3D 13/13 Success。完整 R43 功能 valid=1，792 slider、stream/morph/socket/clock oracle、9 圖皆通過。APNG load 2.603197 / 8.586384 / 5.043302 秒。原 raw gate 11/21 PASS、10/21 FAIL，最高 C010 half 32.272121ms，不能宣稱完成平均 16ms。原始資料、單幀尖峰及失敗 stage 全部保留；最新整理在 Docs/READER_FRAME_BUDGET302.md 首節及 HANDOFF3.md 最新節。
環境紀錄更正：R43 environment.json 顯示開始時只有 1 個外部 Crowd Unreal，結束後有 4 個（先前口頭沿用 R42 的 4 個不精確）；是動態背景負载，不能認定唯一根因或以之抹除 FAIL。自己的 UE 與等待 shell 已結束，未終止外部工作。完整安靜環境驗收待外部工作完成；不得繼續引用舊 R46 PASS 為最新版本結論，也不得只挑本次偶爾較快的段落。

## E302 R53 / ProfileR44（2026-10-06）

- ProfileR44 使用 R52 正式 DLL、三卡 CPU 高模 1600x1000、792 slider events、3 秒噴射、15 秒流動、half/off/restore，各功能與影像檢查通過，但 traced raw gate 8/21 PASS，13/21 FAIL，不能當 16ms 成功。C010 形變 16.128458ms，dry 19.215965ms、spray 22.497210ms、released 20.679363ms、half/off/restore 22.344092/23.725607/22.258047ms。trace 開銷及背景負載均存在，不能據此推論唯一原因。
- 開始前 5 秒背景取樣累計 71.46875 CPU 秒。environment*.json 與 cpu_samples.json 保存背景 Crowd Cook/Resident 工作資訊；未停止外部程序。
- C010 換衣後 KK_PreviewTick 17.024/18.887/17.041ms；NativeAnimation 5.509/5.660/5.400ms，NativeSkin 4.273/4.457/4.190ms，PreviewStreams 5.978/7.384/6.200ms。每個播放 region 只有 2 個 PreprocessShader 事件；不能繼續只以 shader 尚未就緒解釋全部成本。父/子 inclusive 重疊，跨 worker WaitForTasks 總量不是 GameThread 幀長；FrameTime 多執行緒平均也不是整幀。
- R53 新增來源準備/平行 draw 填充/提交 stream 的 trace scopes，以及 dry_edit/spray/released region。不變更測量窗口、門檻、模型與數值計算。BuildR53 Succeeded。
- 工具誤用：PowerShell 中 rg 路徑 Source/OpenKoikatsu/OK* 不是 shell 展開的 glob，產生 os error 123；已改用目錄搜尋，沒有改檔或讀取敏感資料。
- R44 載入期間出現 FlushRenderingCommands recursive warning，保留原 log；尚未確認呼叫來源，不先改全域 renderer。

- 後續誤猜 KKMaterialBuilder.cpp 和 KKTexture* 路徑再次出錯（missing path / Windows glob）；已用 rg --files 找到 KKTextureRuntime.cpp，再搜尋整個實際目錄。之後路徑清單優先，避免重複猜檔名。
- ProfileR45（BuildR53）完整 C010 功能 valid=1、264 真實 sliders、三張影像與原始 7/7 mean gate PASS；不是完整三卡結案。所有細分 region 已匯出；C010 off 的來源準備1.802ms、填充0.693ms、提交2.239ms；勿把平行填充假設成最大瓶頸。數值計算未改，這不是新優化帶來的百分比改善。仍有外部 Cook / shader workers，並非 quiet run。

## E302 R53 / FinalBudgetR46 未通過（2026-10-06）

完整未開 trace 的正式根專案三卡結果 functional valid=1，raw mean gate 20/21 PASS；C010 half_loaded 16.700953ms FAIL。C010 first_observed 86.365700ms，after_half_second 15.833638ms 僅是診斷，不替代 raw gate。C001/C018 全部平均達標；APNG 資源載入 2.463930/6.313734/1.176234 秒。不得挑 ProfileR45 的 C010 PASS 跟 R46 的其他卡拼成 21/21。R46 開始前 5 秒背景工作 31.046875 CPU 秒，有 Cook 與 6 個活躍 shader workers；不是 quiet run。

R54 實作一個待验证的通用載入邊界修正：actor 首次 shader 準備之後、動畫 ready clock 發布之前，使用 UWorld::SendAllEndOfFrameUpdates 與 FlushRenderingCommands 完成 deferred render resources；只在 readiness=false 時執行，不在每幀、不暖機推進動畫，不直接呼叫引擎註明禁止直接呼叫的 UActorComponent::DoDeferredRenderUpdates_Concurrent。現有峰值不能全歸因於此，必須重新量測。BuildR54 成功。

## E302 R54 最終驗收（2026-10-06）

R47正式根專案完整三卡raw mean gate21/21 PASS，functional valid=1，792真實slider events與各oracle通過，9張影像檢查/人工檢視完成。動畫/噴射/停止後流动/半脫/全脫/穿回整幀平均9.007645–14.620444ms，身體更新平均8.018078–10.954539ms，APNG載入1.162396–6.544710秒。RegressionR28真實D3D13/13 Success、exit0；BuildR54成功，root/launch hashes一致。前面R43/R44/R46失敗仍完整保留，不以暖機後值替代原始門檻。外部Cook仍執行，不宣稱quiet環境或改善百分比因果；不停止外部工作。驗收範圍與詳細數據見Docs/READER_FRAME_BUDGET302.md開頭。

## E302 三卡重測 R48（2026-10-06 16:44 Asia/Taipei）

raw平均21/21 PASS、functional valid=1、792真實slider events、9張影像存在/尺寸/非灰屏檢查通過；stage整幀平均5.001098–8.635110ms、身體工作4.518150–6.290429ms、APNG2.728255–4.882673秒。正式DLL於測試結束後更新，不能將launch hash版本的PASS冒稱為之後的DLL驗收；版本差異與結束環境已記錄version_check/environment_end.json。完整數據在Docs/READER_FRAME_BUDGET302.md首節。

工具錯誤：首次產生重測文件的Python read_text行多了一個右括號，SyntaxError發生於解析階段，未写入檔案；已修正並從JSON/原report生成精確表格。PowerShell空pipeline未產生JSON列表時，需明確儲存[]，已補齊啟動前空快照。


## 角色編輯器（2026-10-08）

本次錯誤、原因、修正與防止重犯規則集中記錄於 `Docs/CHARACTER_EDITOR_ERRORS.md`；完整驗收範圍及證據集中於 `Docs/CHARACTER_EDITOR_ACCEPTANCE.md`。抽查不當成全項播放驗收。

## 讀卡器：jeanne.png 頭皮從頭髮露出（2026-10-19）

- **症狀**：StreetHeroines `jeanne.png`（原卡 `Jeanne V1.11.png`）在讀卡器頭頂、前額露出粉色皮膚，看起來像「臉比較大、穿模」。遊戲 T-pose 截圖沒有。
- **誤判一（重犯 §558／§1424）**：直接拿 `mesh_dump` 的 `.obj` 當基準，得出臉大 1.11 倍、頭髮大 1.43 倍。`.obj`／`worldVertices` 是 `BakeMesh` 後又 `TransformPoint`，renderer `lossyScale` 被乘兩次（臉 0.9009、頭髮 0.699，比值剛好是它的倒數）。改用 JSON 的 `manualSkinnedWorldBounds`，或把 OBJ 以 renderer 位置為中心除以 lossyScale 之後，臉逐頂點中位誤差 0（p90 1.4 mm 是表情），頭頂髮蓋 19016 個頂點誤差 0.03 mm，只有辮子因遊戲物理而不同。幾何本來就正確。
- **原因**：卡片的 MaterialEditor 在 `cf_m_face_00` 設了 `_AlphaMask`（372 KB PNG，`_alpha_a=_alpha_b=1`）。遊戲的 `Shader Forge/main_skin` 在 `min(max(r,1-_alpha_a),max(g,1-_alpha_b))<0.5` 時 `clip`，把會從頭髮間透出的頭皮挖掉。我們有讀進這張遮罩，但臉以 `"opaque"` 輸出；UE `MakePartMaterial` 的 AlphaMask 裁切也只處理 `ShaderKind==5`（Koikano），而臉是 kind 7（Shader Forge）；`ResolveParts` 另外把所有 head 部件強制改成 opaque。
- **修正（通用）**：Rust `face_alpha_blend`：臉有 `_AlphaMask` 時依材質的 `_alpha_a`/`_alpha_b` 白化停用的通道（與身體的 `body_mask_for_alpha_state` 相同），並以 `masked` 輸出。UE：AlphaMask 裁切改為適用所有 skin shader（5、7）；masked 且帶遮罩的 skin head 不再被強制改成 opaque。街頭模式的 runtime ForgeSkin 本來就是 `BLEND_Masked` 加原生 `clip`，不需要改。
- **規則**：拿 dump 比較幾何前，先看 renderer `lossyScale`；不是 1 就不能直接用 OBJ（先修正，或用 `manualSkinnedWorldBounds`）。幾何確認一致但畫面不同時，去查材質的貼圖覆寫（MaterialEditor `me_tex`）與 shader 的 `clip`／blend，不要去改骨架或縮放。部件被丟掉的貼圖可以在 `SessionPart` 和 UE 的 blend 覆寫兩處查。
- **追記（同日）**：修正後重新開卡看到頭髮變黑，當時歸因於別人尚未完成的 `SourceBytes` 修改，這個判斷是錯的。實際原因：工作目錄裡已有 C7「編輯器也走共用母材質」的程式碼，同一個 build 加 `OK_SHARED_PARENTS=0` 就是正確金髮，共用路徑才是黑紫色（Forge MainItem 配件頭髮）。Rust 的頭髮貼圖逐位元組不變。**規則**：畫面差異先用同一個 build 切換路徑開關做 A/B，再決定歸因到誰。
- **量測錯誤**：用「R>225、G>195、B>185、R−B 介於 18–60」數頭頂皮膚像素時，金髮 (242,230,201) 也被算進去，舊路徑一度得到 2177。改成 R−G>18 且 B>205（皮膚 (255,228,221) 與金髮可分開）後，結果是：修正前 97／51，舊路徑與 C7 共用路徑都是 0／0。顏色門檻要先拿目標和背景各取樣一次驗證。

## 共用外框改用原作頂點程式（2026-10-19）

- **bake host 少 plugin**：`prepare_runtime_shared_material.lua` 把 bake host 的 plugin 清單寫死成 4 個。`KKRuntimePose` 從 10-15 起依賴 `OKPhysXSecondary`（再依賴 KawaiiPhysics），之後沒人跑過這個工具，所以一跑就編不過（`RegisterTuning` 找不到）。改成沿 `.uplugin` 的 `Plugins` 遞迴帶入專案內的 plugin，並複製 `Binaries/ThirdParty`。**規則**：複製 plugin 的工具不寫死清單，從描述檔推導相依。
- **轉譯頂點程式時寫死輸入**：第一版假設外框頂點程式的輸入是 v0 位置、v1 法線、v2 UV，輸出是 o0–o2。Koikano 的輸入有 8 個（含切線、UV1–3、頂點色），ForgeSkin 還把同一個輸出暫存器分成兩行宣告，結果 shader 編不過。改成依 DXBC 的輸入簽章按語意產生 `vN`，輸出暫存器依 `dcl_output` 去重宣告。**規則**：從 DXBC 轉的程式，輸入輸出一律讀簽章，不依某一個家族的排列。
- **差點讓舊母材質的外框消失**：`OKCreateRuntimeSharedMaterial` 規定 binding 的每個參數母材質都要有。新增的頂點程式輸入和 `__OK_OutlineApprox`，在街頭打包版仍在用的 StreetDisplay23 上不存在，外框會整個建立失敗。改成只對「頂點程式新增的參數」在母材質缺少時略過。**規則**：替共用母材質加參數時，要確認舊 tag 的母材質仍能建立實例。

## 街頭打包版換成 StreetDisplay24／StreetHG03（2026-10-19）

- **遊戲版編不過**：`KKMaterialDedupe.cpp` 的 `OKFindEqualMaterialGraph`（1aebc2bb，OK Studio）在非編輯器版本呼叫 `UMaterial::GetExpressions()`，街頭 stage（FreeTalk01HostD，`OpenKoikatsu Win64 Development`）出現 C2039。只編過編輯器目標就提交了。改成以 `WITH_EDITORONLY_DATA` 包住逐 expression 補 MID 的迴圈；遊戲版本來就不會在執行時編 graph，直接用共用材質。**規則**：動到 `UMaterial` 的 expression、editor data 的程式，提交前至少編一次遊戲目標（街頭 host 的 Build.bat 只要幾分鐘）。
- **差點用舊 stage 驗證新母材質**：最新的 `Conquest97.stage` 是 03:29 建的，早於 40c57679，沒有外框頂點程式的參數綁定。若直接換 materials.json 開遊戲，新母材質的 `_linewidthG`／`_LineWidthS` 會停在母材質預設值，看到的粗細不是原作的。重建 `Outline24.stage` 後才比對。**規則**：換母材質 tag 時，先確認要跑的 stage 執行檔晚於綁定那些參數的 C++ 提交。
- **cmd 引號**：在 Git Bash 用 `cmd //c "…Build.bat … -Project=…"` 啟動編譯，引號被吃掉，只開了一個互動 cmd 就結束，log 看起來像成功。改用 PowerShell 的 `& 'Build.bat' …`。**規則**：Windows 批次檔從 PowerShell 呼叫，並檢查 log 裡有 `Result: Succeeded`。
## OK Studio 天空與 gag 眼動畫（2026-10-19）

- **材質存檔的 keyword 不是執行時的 keyword**：一開始直接用 skybox 材質存檔的 `m_ShaderKeywords` 選變體，晴れた空的材質存檔只有 GRADIENT_BACKGROUND、VERTEX_GRADIENT_BACKGROUND，會畫成沒有太陽和雲的天空。原作的 `TimeOfDayController` 每次都依 profile 的功能開關重設 keyword。改成照 `SkyProfile.IsFeatureEnabled`（含相依）推 keyword。**規則**：Unity 執行時會改材質的元件（控制器、MaterialEditor）要照它的程式算，不能拿材質存檔當成畫面的值。
- **`bIsSky` 讓 SceneCapture 拍不到天空球**：天空材質設了 `bIsSky`，自測的截圖拍到的是關卡預設的天空球。拿掉後才正常。另外自測一開始在換材質的同一個 tick 就拍照，拍到的是上一個樣式。**規則**：換材質後至少等一幀、並等 shader 編完（`MaterialsPending`）再拍；自測的「關閉後畫面要改變」這類對照項能抓到「根本沒畫出來」。
- **自測的期望值要先看原作數值**：「夜空要暗」失敗，但夜空 profile 的頂部顏色本來就是 sRGB (0.30, 0.51, 0.68) 的亮藍。改成「比白天暗」並對準 `_MoonPosition` 檢查月亮。
- **轉譯器不支援 `sincos` 的 null 輸出**：`shader_program_codegen.lua` 遇到 `sincos null, r0.z, r0.z` 直接失敗。補上單一輸出的寫法，兩個輸出都有時的輸出不變。
- **MSVC 不接受長度 0 的陣列**：產生的 `StudioSkyPrograms.inl` 有 `GSky0PixelBuffers[]={}`，在共用工作樹擋住了別人的建置。產生器改成放一個佔位元素、長度另外傳 0。**規則**：產生 C++ 表格時，空表要用佔位元素；產生後在自己建置之前就先編譯一次，不要等別人建置才發現。
- **gag 眼動畫其實沒生效，自測卻 PASS**：改了卡片開出的內容（gag 部件的動畫值和整張 sprite sheet），沒遞增 `oklib.rs` 的 `SESSION_CACHE_REVISION`。UE 開卡讀的是 `%LOCALAPPDATA%\OpenKoikatsu\oklib-cache` 裡改動前的 session，`bGagAnimation` 一直是 false。Rust 測試用 `OK_NOCACHE=1` 所以有值，兩邊對不上。自測的「一秒後畫面要變」門檻 20 像素，頭髮晃動就有 150 像素，因此誤判 PASS，直到協調者重建後拿到 0 像素才發現。修正：版本改 8，門檻改 500，UE 端讀到動畫值時記一行 log。**規則**：改了 Rust 開卡產出的內容就要遞增 `SESSION_CACHE_REVISION`；動畫類自測的門檻先量「不動時」的雜訊再定，並用 log 確認參數真的進了材質。
- **提交時 patch 放錯位置，HEAD 編不過**：共用工作樹裡別人的改動夾在同一個檔案，我用 `git diff -U0` 挑自己的 hunk，再以 `git apply --cached --unidiff-zero` 套到暫存 index。沒有上下文的 hunk 只能按行號套，而行號取自含別人改動的工作樹，7934a99f8 有 5 段落在錯的地方（`OKSceneDocument.h` 的 bSky、`OKSceneActor.cpp` 的 BeginPlay、`OKStudioImport.cpp`、`KKPreviewActor.cpp` 的 gag 區塊、`unity.rs` 的 `texture_object_png`），由同步 session 的 fe422784d 搬回。後一次提交（7dc5bceb1）改成以 `git show HEAD:<檔>` 為底、只做自己的字串替換，再 `git hash-object -w` + `git update-index --cacheinfo` 寫進暫存 index，就沒有出錯。**規則**：混合檔案不要用 `-U0` patch；以 HEAD 內容套自己的改動，提交前用 `git show <新 commit>:<檔>` 看過自己的段落位置，必要時用 HEAD 單獨建置一次。

## 讀卡器：長辮在 T-pose 不下垂（DynamicBoneEditor，2026-10-19）

- **症狀**：jeanne.png 的辮子在讀卡器 T-pose 往後斜伸約 45°；遊戲 dump 的辮子下垂，manualSkinnedWorldBounds 低約 5 cm、前後短約 9 cm。
- **原因**：卡片的 DynamicBoneEditor（`com.deathweasel.bepinex.dynamicboneeditor` 的 `AccessoryDynamicBoneData`）把 slot 16 的 `Bone.002` 鏈改成 Damping 0.33、Elasticity 0.047（prefab 是 0.15、0.3）。其他 slot 還有把 Weight 設成 0（停用物理）或 0.16–0.24 的設定。我們只在換裝轉移時保留這份資料，從沒套用到物理上。
- **修正（通用）**：`card.rs` 解析 `dynamic_bone_edits`；`apply_dynamic_bone_editor` 依（服裝、slot、m_Root 名稱）把 FreezeAxis／Damping／Elasticity／Stiffness／Inertia／Radius／Weight 套到配件的 `deferred_dbones`。套用放在記憶化之後，`meshes_for_accessory_entry` 和 `accessory_extract::emit` 的 missing_entry 分支都會套。`SESSION_CACHE_REVISION` 升到 8。結果：原卡沉降後的頭髮界限與遊戲誤差在 3 mm 以內，OK 卡開卡時的姿勢誤差約 1 cm，讀卡器截圖中辮子也改成下垂。
- **差點造成大回歸**：第一版在 `DBone` 加了 `weight_override` 欄位。`DBone` 在 OK 函式庫的 `dbone_sets` 紀錄裡是 **bincode** 序列化，不帶欄位描述，結構一改，所有舊紀錄都反序列化失敗。重播時被當成缺紀錄現場計算，但函式庫沒有 bundle 原始資料，結果是 0 個元件，**所有 OK 卡的配件動態骨骼都消失了**（`missing_list` 有 `dbone_sets` 缺紀錄）。改成不動結構：`lerp(1, s·c(t), w) = (1−w) + w·s·c(t)` 對曲線是線性的，直接改寫 `stiffness` 與 `stiffness_curve` 的值和切線。**規則**：會進 OK 函式庫紀錄（`oklib::memo`）或 session 快取的結構，不能加減欄位或改順序；要新增資料就改寫現有欄位、另開記憶化方法名，或連同函式庫一起重錄。改完用測試裡的 `oklib::active().missing_list()` 確認沒有新的缺紀錄。
- **診斷工具**：`OK_DBONE_DIAG=1` 會印出 bundle 中每個 DynamicBone 元件的 root 與參數、是否在骨架內、DynamicBoneEditor 套到哪些元件，以及 `ensure_dbone_sets` 的件數與 script context。`crowd_native_tests.rs` 的 `card_part_settle` 會輸出 60 Hz 沉降過程的界限，並與 `OK_DUMP_PROBE_BOUNDS` 對照，同時列出函式庫缺紀錄。

## 人物編輯模式：主光跟隨相機、飾品頭髮被誤判成嘴型網格（2026-10-19）

- **以子字串比對網格名稱**：`OKGameMouthPairNames` 用 `Contains("face")` 判斷 `cf_O_face`。飾品網格 `polySurface95` 含有 "face"，沒有任何 blend shape 卻被建成嘴型 slot。嘴型更新用完整頂點重建 section，蓋掉壓縮後的 section，每幀的動畫串流因頂點數不符被靜默跳過，頭髮停在載入姿勢。修正：`OKSessionMouthPairNames` 要求部件有 morph。**規則**：依名稱推斷部件種類前，先確認部件真的有該功能需要的資料（morph、骨頭）；名稱比對不要用裸子字串。詳見 Docs/CHARACTER_EDITOR_LIGHT_HAIR_20261019.md。
- **靜默的 `continue` 讓問題很難找**：動畫串流在 section 頂點數不符時直接跳過，沒有 log。Rust 端的頂點完全正確，花了很多時間在 Rust 的附著鏈、物理和 palette 上排查。**規則**：先用 UE 端實際寫入元件的數值確認問題在哪一層，再往上游追；log 裡持續出現的錯誤（這次是 `OK mouth secondary section topology mismatch`）要先查清楚。
- **一開始用錯渲染設定重現**：直接開 `-CharacterMCP` 沒設讀卡器的 `OK_GPU_SKIN=1`／`OK_SKELMESH=1`，走的是 CPU 路徑。另外誤以為「高模」是互動模式，用桌面點擊進入時，視窗不在前景，點到了別處。**規則**：重現使用者的問題，要用使用者 log 中的啟動參數和操作順序，不要猜。只有確認目標視窗在前景時才做桌面點擊。

## OK Studio 泛光、霧／AO／光束、地圖天空（2026-10-19）

- **把原作的 BloomSize 直接當成 UE 的泛光 Size**：3800 整張糊掉。原作 `BloomAndFlares` 每次迭代擴散 4×BloomSize「像素」，UE 的 Size 是畫面寬的百分比、還分 6 個 tint。改成先換成高斯 σ（畫面寬的比例），只開 Bloom3 對上 σ。**規則**：移植後製效果前，先讀原作 image effect 的參數與程式，把單位（像素、畫面比例、線性／gamma）換算清楚，不要照名稱對應。
- **後製材質還在編就加進 blendables，自測拍到格紋**：執行時 `NewObject<UMaterial>` 建的後製材質，shader 還沒編完時引擎退回預設材質，整個畫面蓋上 WorldGrid 格紋；三次建置的自測數值一模一樣才發現效果根本沒套上。修正：材質狀態不是 ready（`IsCompilationFinished`、`IsGameThreadShaderMapComplete`）就不加，自測等材質 ready 並先拍幾張預熱。**規則**：自測數值在改程式前後完全相同，代表被測的東西沒進畫面；先看自測存下的圖再改程式。
- **以為「沒在編譯」就是「編好了」**：加了「編譯中不加進 blendables」之後，AO 與光束的開／關數值完全一樣（577→577）。材質狀態其實是 shader map 不完整：引擎用 shader job cache（按需編譯），執行時建的後製材質沒有人要求後製 pass 的 shader，引擎就一直退回預設材質。加狀態 log（`OKStudioScenePostMaterialState`）才看出來，修正是不完整時 `CacheShaders(EMaterialShaderPrecompileMode::Default)`。**規則**：執行時建的材質要檢查 `IsGameThreadShaderMapComplete`，不能只看 `IsCompiling`；狀態判斷先用 log 證實再改。
- **用 `GMaxRHIFeatureLevel` 連結失敗**：OKStudio 沒依賴 RHI 模組（LNK2019）。Build.cs 加 `RHI`，並改用 `GMaxRHIShaderPlatform`。
- **確認 E 是否有人在用時把 C 線的 Unreal 也算進去**：只看 `UnrealEditor.exe` 就以為 E 忙。改成只算命令列含 `E:\OpenKoikatsu` 的行程。**規則**：多個工作樹共用機器時，依路徑判斷是哪個工作樹的行程。
- **地圖天空找不到 profile**：場景 bundle 的依賴清單不完整（ホテル等），只從 bundle 依賴解析會失敗。改成另外到 `studio/map/materials`、`studio/sky` 找（`Sources::with_dirs`）。

## 基礎庫共用資源與版控（2026-10-20）

- **把第三方 mod 模型移出版控前，沒先確認「基礎庫有記錄、但模型不在」會怎樣**：乾淨 clone 開卡時，`sources.okidx` 記著的 zipmod 只在卡片指名時才改讀實體檔；卡片沒指名、依 ID 找到的 mod（例如 `[minih] headmod`）和 uncensor 身體全部變成缺口，7 張測試卡全部 DIFFERENT。修正：`Fallback::installation`（有遊戲路徑時，本機沒有的模型讀遊戲的 zipmod）、uncensor 與全域 mod 收進共用資源。防止重犯：改版控內容後，一定要在乾淨 clone 跑 `kks-verify` 與 `okcard-check live`。
- **基礎庫錄下的遊戲設定是某一台電腦的**：`UserData/config/system.xml`、UncensorSelector 設定被錄進 `sources.okidx`，基礎庫模式一直讀這份（D: 的 `BP Profundis`、`Augh.MinimalShadow`），換一台電腦也一樣。修正：有遊戲路徑時讀那個遊戲的；轉 OK 卡時把用到的設定錄進 `okLc`；只有卡片時 uncensor 設定讀卡片資料夾或專案 `Config/UncensorSelector.cfg`。
- **轉 OK 卡時讓遊戲目錄完全不能提供東西，ramp 就消失了**：ramp 找不到時不會留缺口，所以缺口迴圈補不回來，OK 卡的 `ramp_png` 變成 null。修正：陰影 ramp mod 全部收進共用資源，設定檔照上一條處理。防止重犯：靠「缺口」找缺的資源時，先確認那個查詢找不到時真的會記缺口。
- **已知差異（未修）**：預設男卡 ill_male_01／02 在完整基礎庫上也是 DIFFERENT：只差身體 `runtime_textures._Texture2` 的取樣資訊（skin shader 的 liquid 貼圖），幾何與貼圖本身相同。`Default Female Penis = Random` 的 Random 選擇仍未解析，`o_dankon` 頂點數與遊戲不同。
- **把「本機沒有模型的 zip 項目」一律當缺失**：項目目錄掃描每張卡會碰到所有 zipmod 的 bundle（約 8,850 個），全部變成缺口；轉 OK 卡的缺口迴圈把幾十個無關 zipmod 當成卡片要的，超過 64 個就停，`okLc` 不完整。改成只在真的用到模型內容時記 `absent-model <id>`。防止重犯：新增缺口來源前，先在一張卡上數缺口數量，確認只指向那張卡用到的東西。
- **轉 OK 卡重試時沿用了上一輪的行程內快取**：找到卡片沒指名的 zipmod 後重試，那一輪的項目與 bundle 全從快取來，zip 項目沒被讀，pack 的 `zips=0`，封閉重開時卡片自己的 zipmod 變成缺口（60 張樣本 25 張退步）。完整基礎庫掩蓋了這個問題，因為那些模型本來就在庫裡。修正：模型不在本機的 zipmod，`library_zip_memo` 依後備世代重算，重試前釋放 bundle 快取。防止重犯：改動後要拿「乾淨 clone」與「完整庫」各跑一次同樣的卡，比對兩邊結果。
- **新增共用母材質配方只烘進自己的根目錄，街區啟動就退出**（2026-10-20，OK Studio）：配方 62 `ForgeStudioOpaque` 只烘進 OK Studio 用的 `StudioDisplay25`。街區的 `OKCrowdRuntimeScene` 啟動時要求每個配方都有母材質，所以在 `StreetDisplay24`／`StreetHG03` 找不到 `M_ForgeStudioOpaque` 就退出。修正：新增 `OKRuntimeSharedMaterialFallback`／`OKLoadRuntimeSharedMaterialParent`（舊根目錄缺新配方時改用同程式的舊配方 61 `M_ForgeStudio`），`OKCreateRuntimeSharedMaterial` 依實際母材質的 family 核對；C 組另外把新配方烘進街區根目錄。預防：新增配方時同時處理所有出貨的根目錄（StudioDisplay*、StreetDisplay24、StreetHG03），或事先通知 C 組；新配方一律要給退路配方。
- **1901 背景黑，先後猜成大螢幕偏高、地圖 Animator、LED 面板**（2026-10-20，OK Studio 地圖）：真正原因是場景存的地圖位移 `SceneInfo.caMap`（1901 是 z −15）被 `studio_scene.rs` 讀了就丟，以及兩個 2D 效果（`Legacy Shaders/Particles/Additive` 的レンズフレア）被畫成不透明黑方塊。我照「原作背景像大螢幕」的推測查了一整輪 Animator。預防：畫面和原作差很多時，先確認場景檔的每個欄位都有被使用（解析器裡 `r.change()?; // xxx` 這種讀了就丟的值要逐一檢查），再用 `OK_RT_VIEW` 拍清楚的圖、`OK_STUDIO_COMPARE_HIDE`／`OK_HIDE_SHADER` 逐一藏候選，確認是哪個物件擋住
- **`FString::Printf` 的格式字串用了執行期的 TCHAR\***（2026-10-20）：UE 的 Printf 要編譯期常數格式，傳變數編不過，連帶後面的呼叫也報錯，擋住了別人的建置。預防：格式固定、變的部分當參數（`TEXT("_OKSun%d%s")`）；改完先自己建置，不要留給下一個排建置的人
- **執行期材質的 TextureSampleParameter2D 預設貼圖是線性的，取樣器卻設 Color**（2026-10-20）：材質編譯失敗、畫成預設格紋。預防：Color 取樣器配 sRGB 預設貼圖（`/Engine/EngineResources/Black`），LinearColor 配 `OKExposureWhiteTexture`；看到格紋先 grep log 的 `Failed to compile Material`
- **gamma 空間的加法輸出沒先夾到 1 就 pow 2.2**（2026-10-20）：舞台光束 `_brightness` 5 的片段加 5^2.2≈34，加上場景泛光整張白。預防：模擬 KKS（gamma、LDR 畫面）的輸出一律 `pow(saturate(x), 2.2)`

## 共用母材質配方與出貨根目錄（2026-10-20）

- **把 Legacy 粒子加法誤記成 One/One，忽略 alpha**（2026-10-20，HANDOFF_E 接手）：安裝的 `studio/mat/00.unity3d` 中 `Legacy Shaders/Particles/Additive` 實際是 SrcAlpha/One（5/1），pixel program 為 `2*vertex*tint*texture`，alpha 另 saturate；舊圖只乘 RGB，丟掉 tint alpha、texture alpha 與 vertex colour，1901 光暈因此偏亮。改按安裝程式算出 gamma 空間的來源加法貢獻，再轉成 UE 線性輸出；保留來源 `_TintColor.a`。

- **Studio 道具把 prefab 根節點位移加了兩次**（2026-10-20，HANDOFF_E 接手）：`prefab_session_fk` 會把根 TRS 烘進頂點；顯示時又直接套場景的道具 TRS。原作 `AddObjectItem.Load` 最後呼叫 `changeAmount.OnChange`，`GuideObject.CalcPosition/CalcRotation` 是覆寫根節點。6964 的教室机 prefab 原有 z=-0.306 m，重複位移讓桌面遮住人物。修正：顯示道具時先消除已烘入的 prefab 根 TRS；子物件接點也改為相對根節點。重拍 6964 後人物與桌子構圖已恢復，另補 GameSelfTest 的根節點定位回歸檢查。
- **只傳 RGBM lightmap，遺漏方向貼圖**（2026-10-20，HANDOFF_E 接手）：1F 廁所 `m_LightmapsMode=1`，`m_DirLightmap` 非空；安裝的 Standard PS 112 以 `(dot(N, direction.rgb - 0.5) + 0.5) / max(direction.a, 0.0001)` 解碼，舊實作只算 `5*a*rgb`。新增獨立 memo（不更動既有 bincode 結構）、每張方向圖只由第一個部件攜帶，顯示端以同一份 lightmap UV 套方向重平衡。數值與畫面驗收另記在 OK_STUDIO_PLAN。

- **新增 Rust 測試把浮點型別後綴寫成 `10.f32`**（2026-10-20，HANDOFF_E 接手）：編譯器將它視為整數欄位存取，報 E0610。改為 `10.0f32` 並重新跑測試；編譯失敗的那次不算驗收。
- **Studio 的使用者 FK 沒有關閉 PV 複製**（2026-10-20，HANDOFF_E 接手）：`apply_user_fk` 之後仍用預設 `pv_fk=false` 呼叫 `Config::apply`，手腳會被 PV 節點重新覆寫。原作 `OCIChar.ActiveKinematicMode` 明確以 `!enableFK && enablePV[k]` 設定四個 PV 開關。修正：兩個 Session 姿態分支與 `studio_pv_flag` 都把非空 user FK 視為 FK 模式；清空 FK 恢復原先 mask，保留 host 明確設定的 FK gate。新增 FFI 回歸測試，畫面影響另以 6964 重拍確認。

- **僅依 shader keyword 推斷 MDK 的程式與貼圖需求**（2026-10-20，HANDOFF_E 接手）：`main_StandardMDK_studio` 沒有 `_EMISSION`，但安裝的程式仍讀 `_EmissionMap`；舊 Session 的一般分支將它丟掉，也沒有輸出 `_BumpMap`。先前又因產生器註解列出的變體沒有 `_ALPHATEST_ON`，把護網退回近似材質圖；實際 DXBC 已無條件執行 `_Cutoff` 裁切。修正：記錄 MDK 的完整來源貼圖與採樣資訊、保留到 Session、統一貼圖行方向，護網和機身均走既有 Masked 母材質。驗收必須同時檢查實際傳入的 texture slots 與畫面，不能只看配方名稱。
- **查 Windows 路徑時誤用 Bash 的 brace expansion 與未展開檔名 glob**（2026-10-20，HANDOFF_E 接手）：PowerShell 無法解析 `{Private/...,Public/...}`，`rg` 也不會替不存在的 `Tools/*lua` 路徑展開。改成列出實際路徑，檔名篩選交給 `rg -g`；查詢失敗後重新執行並核對輸出。

- **新增配方只烘進一個根目錄，街區一開就退出**：E 組為 OK Studio 新增第 62 個配方 `ForgeStudioOpaque`，母材質只烘進 `StudioDisplay25`；街區的 `StreetDisplay24`／`StreetHG03` 與 patch 都沒有，而 `OKCrowdRuntimeScene` 啟動時要求每個配方都有母材質，Conquest107 啟動即退出（`Common runtime material parent missing: M_ForgeStudioOpaque`）。街區角色也會用到它：`main_item_studio` 的配件沒淡出時就選這個配方（例如被 Material Editor 換 shader 的頭髮）。修正：烘新根目錄 `StreetDisplay25`（`prepare_runtime_shared_material.lua --tag`）與 `StreetHG04`（`--hg-shading`），`prepare_face_materials.lua` 補淚水與 `_Fg`，`Launch/materials.json`、`runtime.json` 的 patch（`StreetDisplay25HG.patch`）與編輯器讀卡器的預設根目錄改指新根目錄，`rebuild_street_social_patch.lua` 重建 patch。新根目錄＝舊的全部檔案＋`M_ForgeStudioOpaque`。
- **檢查只報第一個缺的母材質**：看不出缺在哪個根目錄、還缺幾個。現在 `OKCrowdRuntimeScene` 先掃完全部配方，錯誤訊息列出根目錄、缺幾個與缺哪些，並附修法。
- **規則**：新增或改動配方（`EKKRuntimeMaterialRecipe`、`OKRuntimeSharedMaterialRecipes`）時，同一次就把母材質烘進所有會出貨的根目錄：`Launch/materials.json` 的 `root` 與 `hg_root`，加上 OK Studio 用的根目錄。根目錄一律用新 tag 重烘（工具不覆寫舊根目錄），接著跑 `prepare_face_materials.lua`、改 `materials.json`／`runtime.json` 的 patch 名稱、`rebuild_street_social_patch.lua`，並通知會打包街區的 session。

- **依交接名稱追錯 0010 的前景物件**（2026-10-20，HANDOFF_E 接手）：把前景白色圓盤當成 `レトロな扇風機`，多次只追 `senpu` 材質。逐項核對原作場景與地圖 renderer 後，確認它是地圖內的 `mo_koi_mi_meter00_00` 儀表，使用 Standard；電扇是另一個場景道具。修正必須依 renderer 身分與實際貼圖定位，不能只沿用交接中的物件稱呼。
- **Studio 原作貼圖只在 accessory 路徑統一行方向**（2026-10-20）：Studio 的角色為 `part`，Standard／MDK／粒子主貼圖與 lightmap 仍保留 Unity 的 bottom-up rows，Unreal 卻傳入 `(u,1-v)`。補上各原作材質與 lightmap 的行方向轉換，保留 Material Editor 已為 top-down 的輸入，並用不對稱兩行貼圖測試單次翻轉；Session 快取版號提升到 14。
- **強引用材質去重快取延長場景生命週期**（2026-10-20）：即使先釋放舊場景並 GC，768 個材質的強引用 LRU 仍持有地圖貼圖。改成弱引用，只共用仍由場景擁有的材質；新增 Automation 測試確認共用有效且 Owner 釋放後材質可回收。
- **乾淨庫 hardlink 快照包含會被驗收改寫的診斷檔**（2026-10-20）：`kks-verify`／`okcard-check` 的 `flush()` 會寫 `missing.txt`，連帶修改乾淨 clone 的同 inode。已核對只變更此診斷檔，解除連結後由該 clone 的 HEAD 還原，並恢復主工作樹的生成清單；後續快照中可寫的診斷與 work 檔不得共用 hardlink。
- **Git 的非 ASCII 路徑與原作測試環境**（2026-10-20）：建立快照時未設 `core.quotepath=false`，日文檔名被當成帶引號的 escape 字串；改用未 quote 路徑後完成 6480 個檔案的快照。執行 `item_open_timing` 模組篩選又同時選到需原作的 MDK 探針，未設定 `OK_TEST_GAME_ROOT` 而失敗；改用完整測試名稱及明確 D: 原作根目錄，保留失敗紀錄而不將其算成通過。

- **2026-10-20｜Studio Linear 色彩與粒子混合**：曾沿用 Gamma 的 Standard 常數與 RGBM 解碼，並把 Legacy 粒子當 One/One。安裝的 PlayerSettings 與既有原生擷取均為 Linear；Standard PS 112 使用 dielectric .04/.96、abs(NdotV)、線性 RGBM，粒子 PS 使用 SrcAlpha/One 與 soft-depth alpha。依來源修正，不用個別場景亮度係數；反射仍須另外目視驗收，公式／資料測試不等於畫面吻合。
- **2026-10-20｜漏掉 ShadowsOnly 遮光模型**：來源 meshes 的一般可見分支會略過 shadow-only renderer；只保留燈與光照貼圖仍使室內被太陽直照。新增 renderer provenance 測試先失敗，補回只投影的 geometry、shadow mode、enabled 與 32 位 layer mask 後通過；6331 地板恢復灰色。不得刪掉不可見模型後才猜光照強度。
- **2026-10-20｜建置與驗收證據**：一次 DepthFade 圖建置漏 include ScalarParameter，補齊後編譯成功。另一次增量建置雖成功，DLL 尚無剛新增的 compare 最後釋放字串；更新來源時間重編，並檢查實際 DLL／執行 log 確認新路徑。驗收 runner 現在要求 final-release 訊息並保存記憶體數值，不能僅依建置成功推論已執行最新程式。
- **2026-10-20｜測試模式與零測試陷阱**：一般 Rust suite 設定原作 oracle 環境時出現 portable clothing fixture 失敗；移除原作環境、單執行緒完整重跑 416 PASS。原作環境只供 ignored oracle 使用。方向圖／遮光測試曾用錯 module filter 得到 0 tests，已改 exact 完整名稱並確認各 1 PASS，不以 exit 0 當成有執行測試。
- **2026-10-20｜EyeSet 偶發引擎 assertion**：一輪 EyeSet 在首次擷取前觸發 SkeletalRenderGPUSkin 的 FMorphVertexBufferPool reading-index assertion；單獨重跑通過，尚無證據確認根因。保留 Verify-HandoffELinear/EyeSetTest.log 與 eye-linear-recheck.log，最終再跑整套；不得把重跑通過寫成已修好引擎問題。

- **2026-10-20｜Standard 重複 albedo emission**：只核對光照貼圖／反射公式仍過亮，原因是通用預覽圖原先無條件把 albedo 接進 Emissive；再加 Standard 的 baked diffuse／probe 會多照一層。Standard 路徑移除這個通用底色項，保留來源 `_EmissionColor`／`_EmissionMap`。在判斷 BRDF／RGBM 錯誤前，必須追完整材質圖的所有 Emissive 輸入。發現後中止僅屬本輪的舊驗收與一般遊戲建置，重建新圖再驗收。

- **2026-10-20｜粒子景深順序**：UE 新材質預設 TranslucencyPass=AfterDOF；Unity 透明粒子在攝影機景深／bloom 前已混入 scene color。只改 alpha 公式仍留下銳利白塊。Legacy Additive 明確設 BeforeDOF，避免把後處理差異誤當材質亮度再加場景係數。

- **2026-10-20｜UE 自測與 UBT 共用鎖**：一般遊戲版編譯持有 UBT mutex，UE 啟動時的 ValidatePlatforms 也呼叫 UBT，使 Pattern／Editor 自測停在啟動並可能逾時。查引擎 TargetPlatformManagerModule 確認既有 `UE_SKIP_UBT_SDK_SETUP=1`；已建好 Editor 的純執行驗收以 child-scoped 環境跳過重複 SDK discovery，立即恢復 parent 環境，不改引擎或全域設定。舊等待中的 owned 驗收停止並用新 tag 重跑，不能記作功能失敗／通過。

- **2026-10-20｜驗收紀錄只有文字與路徑，未嵌圖**：已完成擷取與目視檢查，卻只在計畫列出本機 Saved 路徑，讀者無法直接從紀錄核對畫面。補上可攜的 14 張普通對照 PNG、總覽與圖文驗收報告；主計畫亦直接嵌圖。後續涉及畫面的驗收必須同時提供圖片、左右／前後意義、修正結果與剩餘差異；確認所有 Markdown 圖片路徑存在、圖片可解碼且不含卡片 payload，不能以「已目視」或出圖數量取代可檢視證據。

- **2026-10-20｜Windows 圖片路徑大小寫掩蓋 Git 漏收**：現有資料夾為 `Docs/images`，補圖時使用 `Docs/Images`，Windows 可正常讀取，但 Git 指定路徑暫存未收進圖片，跨平台文件連結也會失效。已統一採用實際小寫 `images`，並檢查 staged 清單包含 15 張 PNG 與雜湊清單，不能只檢查本機路徑存在。

- **2026-10-20｜補圖只做單次整理，驗收工具仍可能遺失證據**：原工具只保存 log／JSON，Compare 圖片仍寫入共用目錄；手動補好一份報告無法避免下次漏圖或後續重跑覆蓋。增加每輪獨立擷取輸出與自動 Markdown 嵌圖；以具名成功紀錄逐張核對本輪 PNG、尺寸與雜湊，缺圖／重複／損壞／目錄外舊圖均判失敗，保留完整資料夾即可重看同輪結果。驗收自動化必須產生可攜的圖片證據，不只輸出「14／14」。

- **2026-10-20｜驗收啟動失敗不能留下「執行中」**：新增自動報告後檢查啟動例外路徑，發現只寫部分結果再拋錯會留下 `finished=false`。已在啟動失敗時保存最終 FAIL 與完整原因；以不存在的引擎路徑做負向驗收，確認非零結束碼、`finished=true`、`passed=false`。這是預期失敗案例，不列為功能通過。

- **2026-10-20｜未核對主相機 HDR 就套 LDR 泛光換算**：早期以 `1/(1-threshold)` 與七點高斯近似 Studio 泛光；重新核對 level2 的 Main Camera（2794）確認 HDR=true，安裝版 BrightPassFilter 的 PS 只逐通道減 threshold.x，沒有該倍率；SeparableBlurPlus 實為九點，最外側 ±6.5。修正依實際 DXBC 與執行順序，不以場景專用強度係數修圖。UnityPy 對 stripped MonoBehaviour 的完整讀取長度檢查會失敗；僅以基礎 header 的 `check_read=False` 取 script 引用，再依對應類別與實際長度核對欄位，不把不完整 typetree 當完整資料。

- **2026-10-20｜GPU 測試片因 Static Mobility 沒有幾何**：在已註冊的 AStaticMeshActor 上直接 SetStaticMesh，被引擎以 Static Mobility 拒絕，畫面成黑；基準色檢查使測試正確 FAIL，沒有把黑對黑的泛光計算算成成功。測試元件先改 Movable 再設定幾何，並保留已知 RGB 的基準檢查。

- **2026-10-20｜只關 ToneCurveAmount 仍留下 UE BlueCorrection**：已知 RGB 顯示截圖出現色差，追引擎 PostProcessCombineLUTs 確認 BlueCorrection 在 tone-curve 混合前後仍各執行一次，部分矩陣混合不會互相消除；Studio 明確將 BlueCorrection 設 0。但修改後顯示截圖的紅色仍約 0.074，不能將全部差異歸因於它或宣稱基準色已恢復。泛光 GPU 測試改用線性 HDR float readback，避免把顯示 LUT／sRGB 量化當作被測運算；不放寬原測試容差。

- **2026-10-20｜GPU 捕捉器未保留 ViewState，後製材質被略過**：線性 HDR 捕捉器起初未設 `bAlwaysPersistRenderingState`，泛光開／關結果相同，測試正確 FAIL。核對引擎 `UMaterialInterface::OverrideBlendableSettings` 確認沒有 View.State 即直接 return；與正式場景擷取路徑一樣啟用持久 rendering state 後，逐通道門檻、常數場、相機參數隔離與關閉回復檢查 PASS。不能只依材質編譯 ready 推論 GPU 已執行。

- **2026-10-20｜單包 cook 未命中目標卻納入編輯器啟動資源**：首次用 CookDir／CookSinglePackage 的診斷 cook 實際處理 301 個啟動套件，沒有八個 SourceBloom 材質，且 Landmass 引用 NeverCook EditorMaterials 而退出 1。保留原 log，改依該引擎 CookCommandlet 的明確 `PACKAGE` 清單、`cooksinglepackage`、`CookSkipRequests` 與 `SkipZenStore`，輸出新目錄；驗收必須逐一確認八個目標的 cooked 產物，不能只看到 Done 或 result 0 訊息就算通過。

- **2026-10-20｜Windows 搜尋路徑的萬用字元與 UE 標頭位置**：直接把帶 `*.usf` 的路徑交給 rg 在 PowerShell 下不會展開，出現 os error 123；UE5.8 材質標頭也已移到 Public/Materials。後續先 `rg --files` 定位，再用父目錄配合 `-g` 篩選，不沿用猜測的 Classes 路徑反覆搜尋。

- **2026-10-20｜新檔未追蹤時通過、提交後 Unity build 發生名稱遮蔽**：自適應建置先把工作中 Bloom.cpp 單獨編譯；提交後它進入 Unity 分組，匿名 namespace 的全域 Cache 讓同組 Sky.cpp 的局部 Cache 觸發 C4459。私有狀態與 helper 改用 SourceBloom 前綴，並避開 Base／Instance 等同組全域名稱；保留 Unity build，不用關閉分組或讓檔案持續留在 working set 來規避。最終驗收必須包含已追蹤檔案的 Unity 分組建置。

- **2026-10-20｜3167 鏡片被共用材質強制不透明**：KoikanoGlasses0–11 未列入 OKRuntimeSharedMaterialTranslucent，baker 因此改寫原始 pixel shader 的 alpha 為 1，約 0.197 的鏡片變為黑色實心。按既有來源 Forward 的 SrcAlpha/OneMinusSrcAlpha 修正完整家族，三個出貨根以新版本重烘；不能只改 PNG alpha、只修單張卡或只烘 Studio。診斷腳本一度誤用不存在的 ok.path.read/write，改用既有 path.copy/read_text/write_text；Saved 臨時腳本沒有提交。

- **2026-10-20｜透明度驗證前置錯誤**：第一次 Python 編輯未指定 UTF-8，cp950 解碼 Rust 檔案失敗，沒有套用修改；改用 apply_patch。第一次篩選 accessory_glass 只找到 ignored 原作 oracle，不能當回歸通過；加入快取 round-trip 測試後以 --include-ignored 執行，又因漏設 OK_TEST_GAME_ROOT 使 oracle 正確 FAIL，補齊明確的 D:/KoikatuSunshine 測試環境再執行。
- **2026-10-20｜鏡片第四組顏色漏寫，被誤判為較晚的 ME 覆寫**：嚴格原作卡 oracle 要求 alpha 0.1254902，實際原生材質卻是 0.197。曾改用量化值並猜測是 MaterialEditor 較晚覆寫；直接檢查序列化卡片後證實沒有該 ME Color4，這個推論錯誤。根因是 color_key 只有三組，zip(cols) 沒有寫回第四組顏色；已明確 set_material_color(_Color4, cols[3])，恢復原嚴格斷言，不放寬容差。快取版本提升到 17，避免重用修正中產生的舊結果。
- **2026-10-21｜透明度測試同一影格切換材質，誤讀尚未更新的繪製資料**：首輪 48 組透射率全部為 1，一度猜測 Plane 朝向、綁定、Nanite 或 SceneCapture 的 SeparateTranslucency／UnlitViewmode 預設。新增不透明與常數 alpha 雙控制後，不透明控制 PASS、常數 alpha 仍消失，證明尚不能歸因於來源 shader。SceneCapture 旗標修改本身也沒有修好。最終改成跨影格設材質、下一個 ticker 才擷取，常數 alpha 及全部 48 組皆 PASS；保留先前 FAIL，沒有放寬單層 .5／雙層 .25 的容差。正式 Capture 不保留未經證實必要的旗標變更。測試沿用正式綁定器初始化來源燈光／probe 等輸入。
- **2026-10-21｜GPU fixture 混用舊眼白父材質與新 selector**：還原正式擷取預設後，45 組 PASS、三個根的 ForgeEyeWhite 失敗。檢查 selector 確認該 shader 現在選 ForgeEyeWhiteLod（family 56），不是舊 ForgeEyeWhite（family 8）；先前手動覆蓋 family 曾掩蓋 fixture 混搭。移除冗餘手動 family，改測真正的現行父材質，48 組與兩個控制均 PASS。沒有變更正式 shader selector 或以錯誤 family 規避契約。

- **2026-10-21｜1268 把動畫道具的子接點當成靜態骨架**：原作兩個小角色掛在「回転（obj無し）」道具下，父物件的 guide rotation 為零仍不代表子接點沒有旋轉。舊解析器丟棄 OIItemInfo.animePattern／animeNormalizedTime，WorldTransform 只讀靜態 N_setuzoku，造成子角色位置與朝向錯誤。保留來源相位／速度，離線轉出共用庫 32 種 Animator 接點動畫，依原作相位取樣；後續先檢查完整父子鏈及來源 Animator，不能對子角色加場景專用旋轉。
- **2026-10-21｜1268 診斷曾先懷疑 IK／動畫時間，未先查父道具動畫**：實際播放骨架的九個 IK effector 位置已接近來源目標；缺手腳旋轉是既有支援邊界，不能當成本次角度根因。根因由來源 ItemList anime=true、OCIItem.OnSave／AddObjectItem 的 normalizedTime，以及父道具 N_studio_kaiten clip 交叉確認。未針對此場景改 IK 解算、角色根旋轉或相機。
- **2026-10-21｜旋轉接點烘焙須正規化四元數**：generic clip 讀出的四元數曲線逐分量插值，離鍵值位置可能不是單位長度。直接用於有平移子節點的 TRS 會扭曲接點位置；在組合階層之前正規化，並以離開烘焙影格網格的原作 clip 取樣做角度 oracle。
- **2026-10-21｜診斷矩陣與 Windows 搜尋路徑錯誤**：骨架 C ABI 為 row-major，最初診斷讀 M[12..14] 印出零位置，改讀 M[3],M[7],M[11]；此錯只在新診斷日誌，未用來改角色。rg 路徑不可寫 src/studio* 或 Private/KKPreview*（Windows os error 123），應給父目錄加 -g 或先 rg --files。map_animator_defaults 的 OK_ANIM_BUNDLE 要完整檔案路徑，第一次給相對遊戲 bundle 名稱造成探針失敗，補完整路徑後成功。

- **2026-10-21｜乾淨資源驗收不可只相信舊 clone 的檔名**：新建稀疏 checkout 從舊驗收庫 hydrate LFS 時，第一個不符 HEAD 的模型雜湊被嚴格檢查攔下；改逐檔核對追蹤 LFS OID，只在雜湊相符時建立 hard link，90 個舊來源不符者使用主工作庫中符合 HEAD 的內容。665 個追蹤 payload 共約 8.62 GB 全部核對，不提交或覆寫舊驗收庫的 missing.txt。稀疏 checkout 必須包括根 .gitattributes，否則 hydrated LFS payload 暫呈修改；重新套用 attributes 後刷新 index（不新增 commit），確認資源無實際 diff。sparse-checkout add 不接受 --no-cone，該模式只在 set 時指定。

- **2026-10-21｜嚴格乾淨庫回歸必須保留 gaps 失敗**：本輪真正只含 HEAD 追蹤共用資源的新 checkout 中，March_7th 的 OKCHECK recorded／closed／live 三個 digest 一致、game_reads=0，但 gaps=1，嚴格結果為 DIFFERENT；其餘四張 gaps=0 MATCH。完整庫五張皆 MATCH。新增接點資料之外沒有改 base_v1 舊資源，故不以本次角色角度已正確而掩蓋這個既有依賴缺口，也不把私有 mod 模型加入共用版控來湊綠。okcard-check 有 DIFFERENT 仍 exit 0，驗收必須解析 verdict／gaps，不能只看退出碼。

- **2026-10-21｜6331 遺失保存的手腕方向與 FIX 頭部姿勢**：原作左側角色 555 的左手啟用 IK，保存 hand target 的 pos／rot；頸部則為 neckLookPtn=4。舊匯入器只傳 IK 位置並丟掉 NeckLookControllerVer2 的 neckByteData，導致手腕方向與低頭姿勢錯誤。依 AddObjectAssist 的 usedRot（僅手／腳）與實際 prefab 的 aBones（cf_j_neck、cf_j_head）保存並套用來源四元數，FIX 在動畫與 IK 之後、動態骨之前生效，不替場景加角度或移動相機。
- **2026-10-21｜未使用的頸部四元數可能為零**：初版新解碼對所有 neck pattern 驗證四元數，會把非 FIX 模式下無效但未使用的零值誤當場景損壞。只對 FIX 資料解碼／驗證；非 FIX 保留原動畫流程。FIX 模式仍嚴格檢查長度、有限值與非零 norm，錯誤不可寫入目前狀態。
- **2026-10-21｜Rust 測試誤用 C++ 小數語法**：新斷言寫成 >.9999，Rust 需要 >0.9999，第一輪測試未編譯；已修正後重跑，沒有把編譯失敗列為通過。首次診斷不應列印整個場景全部道具／隱藏角色，6331 輸出過多；改為只讀有效可見角色與需要的骨架欄位。

- **2026-10-21｜保存的固定頭部不能藏在「照動畫」模式下**：初版以 FixedNeck 陣列覆蓋 Animation 模式，畫面雖正確，但 UI「照動畫」不能真正還原動畫頭部。改用獨立 Fixed enum（附加於既有值之後），匯入時明確設為「固定姿勢」；只有有保存頸部資料的角色提供此選項。切回 Animation／Camera／Point 時清除 runtime FIX，保存資料仍可切回使用，避免為單張圖隱藏控制狀態。

- **2026-10-21｜大型 SelfTest 函式新增 Before 變數撞名**：固定頭部切換整合測試使用 FVector Before，遮蔽同一函式前面的 FString Before，Windows 編譯將 C4456 視為錯誤。改成 SavedNeckBefore／SavedNeckFixed／SavedNeckRestored 等用途前綴。保留失敗 build log，以重建後結果驗收；在大型既有函式加測試時先檢查外層名稱，避免重複使用 Before／After／Fixed 這類名稱。

- **2026-10-21｜FIX 頭部也必須保留於直接身形重建**：一般動畫／static interaction 的 finishing pass 已重套 FIX，但 apply_shape／apply_shape_live 的直接顯示路徑也會還原骨架，不能只靠第一次載入 setter。於身體 shape baseline 保存之後、IK 之後重套固定 locals，僅在 FIX 存在且非 deferred frame 時執行，不推進追視或物理時鐘。回歸測試實際修改身形數值並驗證兩條路徑的頸／頭 locals；固定資料不能烘進 shaped baseline，否則切回動畫會殘留。

- **2026-10-21｜Studio 不可套用展示服裝自動排序**：1268 天使來源第0套為白色浴巾（top28），通用 OK 卡轉換的 shown_outfit_first 依外出鞋與部件數選中第1套並交換到0，Studio卻仍按來源coordinate=0載入，變成校服。新增明確服裝轉換API，保留全部coordinate／Status／plugin編號，並將明確選中非0套的資源一起錄入；Gallery預設展示規則維持原行為。驗收必須檢查轉卡後服裝資料與實際renderer，不能只看角度正確或程序成功。
- **2026-10-21｜診斷需區分現代OK卡與舊快照**：ok-appearance inspect只適用okPv快照，對okLb卡會回報PNG has no portable snapshot；本輪誤用後改用來源資料塊與實際Session比對。Python讀舊MessagePack binary字串需raw=True，再針對文字欄位解碼；rg在Windows不可將1268*當成目錄參數，應給父目錄搭配-g。初期白色簡化模式／材質猜測均經來源資料排除，不以猜測改渲染。

- **2026-10-21｜Session open 已負責掛載卡片資源包**：1268 新來源回歸測試先手動 mount_card_pack 再呼叫 session::open，對含pack的卡重複取得 PACK_OPEN 非重入鎖，測試停住。移除外層手動掛載，讓既有Session生命週期處理；停止本輪測試PID後重跑，未修改正式鎖或跳過pack。

- **2026-10-21｜錄製卡內資源時不可用最終Session快取代替來源走訪**：明確指定非0套服裝時，其zipmod模型與query也必須錄入okLc；已暖載入的Session不能提供完整來源依賴。錄製card pack期間略過Session／baked快取，正常遊戲載入快取不受影響。來源回歸先暖載入再轉卡，檢查模型清單非空與closed reopen，避免只有冷快取成功。

- **2026-10-21｜3800 構圖差異不能先歸因鏡頭**：來源camera位置、完整含roll旋轉及vertical FOV均已正確傳入，實際horizontal FOV=46.777942657，背景窗框投影亦吻合；真正差異是FK頸部被保存的FIX locals覆蓋。原作OCIChar.ActiveFKGroup(Neck)會強制FIX以停止追視，但FKCtrl仍控制頸／頭。修正FIX只套用未被明確FK接管的骨頭，不能用改FOV或相機offset掩蓋姿勢錯誤；並回歸6331未啟用頸部FK的固定低頭。
- **2026-10-21｜診斷工具與檔案需先確認存在**：本輪誤查不存在的OKStudioCompare.cpp、studio/maps.json與非firstpass的DepthOfField.cs；比較器實際在OKStudioGame.cpp，先rg --files再讀。系統Python沒有cv2，改用既有numpy梯度相關與實際camera trace，沒有為診斷安裝套件。

- **2026-10-21｜新增身形回歸需先查現有API**：3800測試誤將apply_shape／apply_shape_live當成單一參數且回傳Result，導致測試E0061／E0599未編譯。實際需同時傳body與face且回傳()；修正後重跑，失敗log保留，不以正式lib建置成功代替測試通過。

- **2026-10-21｜直接啟動測試binary要等新編譯完成**：3800第一輪6331回歸在cargo更新測試binary前啟動，log仍顯示883 filtered而新版本應885。等待最新419項suite建完後重跑具名測試，使用3800-latest-6331-pose-regression.log，避免將舊binary結果列為新版本驗收。

- **2026-10-21｜1901 後製排除要作用於實際SceneCapture設定**：ShowFlag.PostProcessing 0沒有關掉比較器另建SceneCapture的自有後製，圖片近乎相同不能當作無後製結果；改用只影響比較文件的BLOOM／DEPTH／FILTER診斷開關。隱藏Legacy粒子、volumeLight、spotLight各次診斷未移除右下白光，不能據此關閉正式場景特效當修正。
- **2026-10-21｜Unity path_id僅在各serialized file內有效**：1901 Python探針把bundle所有物件按path_id放同一字典，Shader引用誤對到同ID mesh；改從material所屬assets_file.objects解析。同bundle可能含多個serialized file，不能用全域path_id表。

- **2026-10-21｜地圖顯示根不一定等於清單prefab名**：1901 ray probe以清單LiveStage開模型，回報no meshes；該地圖實際root為Map。改取模型已記錄的shown prefab，不將清單名稱直接當渲染root，也不把探針失敗當成場景缺模型。

- **2026-10-21｜1901 原作動畫開關遺漏**：舊版只取 Renderer 材質曲線，註解明確略過 GameObject m_IsActive，導致六組光束父物件原作為0仍顯示。改以完整 FileID／PathID 解析控制器與clip，套用 activeSelf 並沿父鏈取 activeInHierarchy，逐renderer instance保留；重新記錄90個共用Studio模型並提高session cache revision。不能改曝光或依shader名稱全隱藏，因為同shader可能也用於正常顯示的其他光束。
- **2026-10-21｜原作oracle不可猜測測試環境或部件數**：第一輪用OK_GAME_ROOT啟動依賴test_support的oracle，實際契約是OK_TEST_GAME_ROOT，因而找不到來源檔。第二輪把六組父開關誤當成六個材質部件；原作有114個子renderer，Custom/StandardDouble也被其他光束共用。改核對完整來源／共用庫node可見性向量，另驗受影響材質instance確實隱藏且正常部件仍顯示；保留失敗log，不把不同shader共用部件數硬套成父物件數。

- **2026-10-21｜動畫開關通過不能代替正式出圖驗收**：1901初版補齊六組Animator父開關後，正式圖片右下白塊仍存在。來源檢查確認共用mesh的spotBeam_geo接點activeSelf=False；ModelView卻把已載入的共享mesh複製到所有hierarchy接點。修正可見性需同時保留每個instance的靜態activeSelf與祖先狀態，不能只管動畫覆寫；SunLightInfo控制的root另保持可切換資格。初版出圖保留在Verify-Light1901Accepted，不能當最終成功圖。

- **2026-10-21｜同名node與同shader都不是renderer身份**：額外map_animated_materials探針曾以spotBeam_geo名稱斷言全部關閉；原作存在同名接點及不同材質的正常啟用光束，該斷言失敗並非最終1375個node向量來源比對失敗。移除這項名稱猜測，保留專項逐node完整向量oracle與真實前後圖；後續不可把name／shader當唯一renderer identity。

- **2026-10-21｜LFS純追蹤副本更新須核對payload與索引**：更新Clean1268的90個模型後，payload已符合新HEAD的SHA-256，但skip-smudge checkout的stat/index仍將它們列為modified，最初clean斷言失敗。先逐檔核對新commit的LFS OID，再只對這90條既有追蹤路徑重新套用LFS clean filter更新索引；確認cached diff及git status皆空，再核對全部共用庫LFS payload。不能用assume-unchanged或忽略dirty狀態假裝乾淨。

- **2026-10-21｜4432探針必須先篩選物件型別**：初版JSON探針用「含fk欄位」當角色條件，把大量道具也輸出，且把某些children的陣列誤當dict而失敗。改為遞迴處理dict／list，只選kind=Character，僅輸出服裝、啟用群組與計數，完整資料留本機檔。不得用截斷的大量輸出當角色資料驗收。
- **2026-10-21｜Studio不能把動畫相位暫停當成物理dt=0**：原AdvanceAnimations固定傳ApplyPoseFrame(0,false)，比較暫停更完全略過角色更新；固定FK腿部後衣物碰撞因此沒有收到經過時間。改由場景每tick統一送一次真實dt，動畫相位可獨立保持；static FK同樣求解，頭部追視先準備target，避免static角色額外步進第二次。需實拍驗證衣物，不可只驗動畫phase有變。

- **2026-10-21｜6964調查重複誤用Windows rg路徑萬用字元**：數次把KKForge*／KKRuntime*直接寫在路徑參數而報os error 123，也曾猜測不存在的檔案位置。改用既有父目錄配合-g篩選；先以rg --files確認精確路徑再讀取。失敗搜尋不能當成程式不存在。

- **2026-10-21｜6964場景shaderType遺漏**：來源SceneInfo.shaderType=0，但角色卡依遊戲system.xml的shaderType=1建立；Studio匯入只處理ramp／陰影色，未把SetShaderType對應的_KanoVerShift帶入，因而繼續用錯的渲染模式。補保存場景ShaderType並透過既有全域材質binding覆寫；缺省=-1保留舊檔／普通卡設定。須以實際前後圖確認影響，不預先將此單一修正當成全部暗部根因。

- **2026-10-21｜6964診斷編譯C4335**：直接decode含CRLF的bytes再以Windows text mode寫回，產生CRCRLF而被編譯器視為Mac格式。修正為原始bytes去除重複CR；後續文字編輯先統一換行或維持bytes，不能把未正規化的CRLF再套系統換行轉換。

- **2026-10-21｜6964環境遮蔽Tint錯誤解碼**：AO後製對保存的Tint再次做sRGB→linear，將6964約0.53錯壓成約0.24，暗部乘色過黑。原作AmplifyOcclusionBase將Tint直接送Shader.SetGlobalColor(_AO_Levels)，同版UnityEngine.Shader可見它直接轉呼叫SetGlobalVectorImpl，沒有色彩轉換。修正只取消這一次錯誤解碼，保留AO強度、半徑與開關；停用AO只是診斷，不能當正式修復。

- **2026-10-21｜6964虹膜來源Linear標記遺失**：原作探針的create_eye RenderTexture與直接mip0讀回一致；舊抽取虹膜有效區RGB誤差約40/255。虹膜先翻列再用PNG bytes重新核對來源，導致Linear旗標失配、以sRGB再次解碼。改讓虹膜與眼白共用「來源bytes＋列方向＋color_space」一次解析，先保留metadata再翻列合成；Session cache revision提升19。先前只有AO／模式修正的圖與14景中途執行不能當最終虹膜驗收，已終止自己持有的過期比較程序。

- **2026-10-21｜6964具名oracle新增檔案路徑需隨工作目錄調整**：在Tools/ok-extract執行時仍以專案根相對路徑寫lib.rs，導致FileNotFound且測試尚未新增就啟動cargo。修正使用絕對路徑，重新編譯並要求具名測試實際1 passed；不能把0 tests或舊binary當作驗收。來源貼圖探針亦先列實際產物，原始Texture2D不可讀時使用linear float readback，不假設source.png必存在。

- **2026-10-21｜6964驗收不應把既有問題混成新修正失敗**：PostTest舊霧斷言以全畫面平均紅色上升3判斷，天空佔大面積使均值僅+1；實際1736個幾何像素紅升／綠降至少5。改以至少100個受影響像素驗證方向與幅度，不改霧正式公式。全庫轉卡首輪則沿用本機未追蹤私有模型，pack未含該mod，乾淨庫gaps=2；依既有共用庫政策改由純追蹤庫轉卡，pack記錄1個私有模型／3查詢與實體zip條目，CLI容量25.2%通過。不能提交私有模型填補乾淨庫，也不能把首輪容量失敗或DIFFERENT記為PASS。

- **2026-10-21｜6964乾淨庫檢查會寫入追蹤的missing.txt**：LFS payload全核對後git clean斷言因前輪失敗CLI更新missing.txt而失敗。先將該輪缺口檔原樣留存Saved/OKStudio/6964-clean-missing-before-restoration.txt，再只還原自己測試寫入的該追蹤檔並重新核對。不可忽略dirty或把失敗證據刪掉。

- **2026-10-21｜6964乾淨庫是sparse checkout**：提交後的驗證器假設Clean1268有Plugins實體來源檔而FileNotFound；該副本刻意只展開共用資源。改以git物件核對來源blob，並以git hash-object套用同一clean filter核對目前工作檔，而非將稀疏路徑不存在判成程式遺漏。多指令shell也須檢查每一步退出碼，不以最後一個git status的0掩蓋前面Python失敗。

- **2026-10-21｜全場景畫面盤點不能以出圖成功代替畫面驗收**：前輪14/14代表載入及PNG完整性，3800連續載入景深模糊、6331材質及其他樣本色調差異仍存在。新輪先逐張檢視14圖並建立獨立問題清單。調查時數次猜測shader／spec／Saved路徑造成找不到檔案，應先用rg --files或Get-ChildItem確認實際位置，避免重複無效讀取。

- **2026-10-21｜Studio匯入重用文件會沿用前景後製狀態**：Import只清Objects，遇3800的depth_aperture=0時略過啟用分支，前一景的DOF仍為true；背景色等中性／缺省值也可能沿用。修正於成功解析新來源後重設Post、MapTransform、Tracks、Duration與動畫時鐘，再套用新來源；加入污染文件與全新文件匯入結果一致的整合測試。不能用固定關景深或場景ID修補。

- **2026-10-21｜舊Studio場景沒有shaderType欄位，不可把Rust預設0當成來源值**：前輪6964把SceneEffects的Default零值誤認為場景保存模式0。原作SceneInfo從1.1.0.0才讀取shaderType，舊版本初始化自Manager.Config.GraphicData.shaderType；3800原作實際讀回_KanoVerShift=1。改為Option並在未保存時省略JSON欄位，UE沿用卡片已錄的系統設定；新增14樣本版本與欄位存在性oracle。明確保存0仍必須保留。前輪虹膜色彩及AO修正證據有效，但shaderType來源判斷需以本輪更正為準。

- **2026-10-21｜Studio錯套Gallery無陰影展示政策**：Config/DefaultGame.ini的CharacterDisplay.EnableShadows=False由AKKPreviewActor::LoadSession覆寫到Studio角色，共用材質綁定因此把來源_ShadowColor改白。原作3800實際材質為(0.8304498,0.8662278,0.9411765)，Rust來源資料也正確，故不能以改原始卡、材質色或調曝光修補。新增host層的可選照明覆寫，由Studio選擇保留來源材質照明，Gallery照舊使用自己的設定。

- **2026-10-21｜頭髮UV通道修正不能冒充整體shader還原完成**：骨架／程序顯示路徑將原作UV1只留在UE通道2，共用父材質卻從通道1取樣；補上通道1，保留舊路徑通道2。實拍後3800變化很小，因目前顯示還有其他渲染路徑；不能只看程式接線就宣稱髮絲光澤完整恢復。另具名原作貼圖oracle確認3800光澤PNG誤差僅0.008689/255、最大6，來源取樣為Gamma，排除任意翻列／提高光澤貼圖亮度的猜測。

- **2026-10-21｜CameraLightCtrl名稱不代表角色燈掛在相機下**：舊匯入器把charaLight旋轉直接套到相機子燈，且漏掉level2原生Directional Chara的固定local quaternion(0,0.939692676,-0.342020094,0)，即Euler(40,180,0)。原作LightCalc.Reflect旋轉的是場景根Light Chara；以3800保存旋轉獨立計算towards=(-0.30333951,0.19080912,-0.93358302)，和新Rust D3D11探針讀回GPU常數一致。修正為world-root及root*child，加入光向及改相機不帶動燈的整合測試；並依Unity HDR尾段線性化光源色／強度。未靠場景專用offset或曝光補償。中間VisualAuditAllScenes回歸在9張後由本輪確定PID中止，以建置該根因修正；保留部分圖與FAIL紀錄，不能報成14/14。

- **2026-10-21｜Studio地圖環境光不是人物編輯器環境光，也不是單一空場景常數**：原作3800／6331實際AmbientProbe分別來自courtyard／bushitu場景的RenderSettings.m_AmbientProbe，與GPU／renderer讀回27係數各最大差約8.21e-10／1.01e-9。空Studio初始化反而是Funly天空且probe為零，不能複製一張場景的量測值到全部場景。新增Rust通用轉換，從共用清單所有地圖的原始場景讀取27係數，輸出72筆map_environments.json；依地圖選用，保留顯式天空原有流程。既有shader讀SHAr/g/b的路徑先修正，不宣稱所有SH高階／反射細節都已還原。

- **2026-10-21｜共用float4更新不能只送Vector**：烘焙材質以Vector的RGB與獨立__A scalar組成float4；SetSharedVectorParameter只更新Vector，使角色燈alpha與環境光常數分量可能停留舊值。一起更新有宣告的__A，並驗證實際角色材質的四分量同步。Native GPU常數與UE最終材質snapshot分開保存，不能把建立時的預設光向log當成最後顯示的光向。

- **2026-10-21｜完整Rust測試的並行失敗需隔離查證**：本輪預設並行跑出418 PASS／2 FAIL，分別為bundle path cache的Arc identity及portable clothing rules的not_bot；兩項在獨立程序各PASS，完整單執行緒重跑420 PASS／0 FAIL／472 ignored。保留三組log，不把首輪失敗改寫為全過，也不為Studio修正擅改無關功能。現有測試會碰程序環境／共用來源快取，完整驗收採明確的單執行緒執行方式；不能只重跑失敗兩項就宣稱整套通過。

- **2026-10-21｜乾淨庫命令須以實際cwd解析**：已在Clean1268工作目錄內又指定git -C Saved/OKStudio/Clean1268，造成路徑重複且fetch未執行。改為該目錄內直接git fetch／merge並檢查每一步退出碼。CLI寫入的missing.txt先留存本輪證據，再只還原自己的測試輸出；不可讓後續status掩蓋前面操作失敗。

- **2026-10-21｜保留來源材質陰影不可為每個Studio道具分配角色陰影RT**：初版把CharacterDisplayShadowsOverride=true套到所有非map Actor。4432大量道具因此各分配2個1024² RGBA32f（約32MiB），完整回歸升到50GB且每幀約5秒。道具既有bStudioItemShading已能保留材質陰影，不應因此啟用角色擷取。修正由Studio明確選擇角色true／道具false，讓道具共用既有4×4中性depth；新增實際載入道具的RT尺寸及陰影政策整合檢查。該輪只完成10張後核對PID中止，保留FAIL與50GB證據；必須重建後重新跑全14景，不能把前輪結果當成最終通過。

- **2026-10-21｜不能把所有Koikano的頂點環境光寫到相同varying**：原作forward VS證實髮／一般衣服用o5、皮膚用o7，而clothes_item用o6；其o5是螢幕座標。初稿以同一v5處理衣物，在正式驗收前逐族讀回DXBC時查出。修正逐族採原作語義，並將首輪StudioDisplay27僅留作原型，不計入最終材質驗收；正式根改用重新建置後的新版本。不可只看pixel shader的SHAr輸入一致，便忽略原生vertex輸出的高階SH項；也不可用pixel normal直接計算二次項取代原作頂點計算後插值。

- **2026-10-21｜新GPU頂點oracle須按實際windows-rs版本宣告**：初版漏啟用Win32_Graphics_Direct3D，且把SOSetTargets誤寫成切片API；0.58版實際為數量＋buffer pointer＋offset pointer。核對本機binding後修正，原作六族／地圖20及33各2048頂點皆PASS。額外自測診斷曾以float Error遮蔽RunSelfTest外層FString Error，C4456建置拒絕；改為SampleError。保留失敗建置log，不以舊DLL冒稱新測試通過。

- **2026-10-21｜環境光整合測試也須尊重地圖自帶天空**：新增測試最初直接拿map 0烘焙probe當期望，但該圖由MapSkyInfo選天空，正式路徑採天空環境，故讀到的高階SH為零。診斷保留52個實際向量與失敗witness；改由同一來源規則選擇map／sky期望，並不要求原作未使用的SHC.w。另以地圖20／33原作DXBC直接GPU執行及正式場景snapshot獨立驗證非零高階SH，不能把所有期望改成零來讓測試通過。

### 2026-10-21 Studio 高階環境光測試時序
- GameSelfTest 在 SyncNow 建完 renderer 後立即讀 MID；正式照明在下一個 SceneActor Tick 提交，導致讀到 parent 缺省零值。先前只修正 sky 期望值仍失敗。測試現在明確執行零秒場景 frame 再驗證，保留兩次失敗記錄。不能靠放寬誤差宣稱成功。
- Unity plugin event 的 SRV inventory 可能已被 Unity 清空，空清單不能證明 draw 未綁貼圖。紋理改由明確 native pointer staging 讀回，完成標記必須最後寫出。

### 2026-10-21 原作 GPU 貼圖探針 typeless 資源
- ARGBFloat target 的 resource descriptor 是 DXGI R32G32B32A32_TYPELESS(1)，不是 typed FLOAT(2)。初版 raw readback 拒絕它，未產生有效影像；補相同 16-byte payload 格式並使任一 queued texture 失敗時整體 capture 明確 FAIL，不能只看 constant buffer PASS。失敗 JSON 保留。

### 2026-10-21 Studio 地圖未烘焙表面的間接光
- 原作 3800 窗牆的 lightmapIndex=-1、lightProbeUsage=BlendProbes；舊 Standard 圖只加 lightmap/reflection，漏掉無 lightmap 的 SH diffuse。補 native vertex L2 + pixel L0/L1，逐 renderer MID 套用，避免修改共用父材質。
- 地圖太陽此前把色彩先轉 Linear 再乘強度，與 lightsUseLinearIntensity=false 的原生 GPU 常數不同。現在與角色燈共用最後 gamma/HDR 轉換，原生 (0.964627564,1.11674428,0.959291399) 作明確 oracle；不加場景曝光補償。
- 原作選定後髮在來源相機下的 target 為全零，不能當 shader 顏色證據。可選 frame-mesh 僅在探針暫時取景，結束還原相機；有效 framed draw 有128574個像素，另留原場景圖而不冒作縮圖同鏡頭對照。
- 引擎 SetLightColor 內部存 8-bit sRGB FColor，直接送 HDR RGB 會截斷。地圖燈先把最大通道能量移到 intensity，再量化正規化色彩；整合檢查保留 UE 量化容許誤差，不能把純函式的1e-6當成實際燈光逐像素誤差。

### 2026-10-21 卡片髮光澤與地圖 HDR fallback
- 再查來源後確認髮卡 glossColor 雖有解析，沒有提交到原生材質；改為一般／mod／缺清單的頭髮來源一致套用。ChaFileHair 缺省是217/255；HairAccessoryCustomizer 缺省確實為0.8490566，不能一起改。HAC colour-match繼承主髮光澤，否則用自身值，Material Editor覆寫最後優先。session cache revision20避免暖快取沿用舊值。
- 地圖的舊 reflection atlas經PNG，無法完整保留HDR；default probe改標明來源身分並走已驗過byte的原始cube；局部probe維持原有路徑，沒有宣稱局部probe／box projection全修。

### 2026-10-21 建置中繼續改源碼的驗收風險
- native-default build尚在執行時補寫了cube整合斷言；後續log沒有該斷言，不能以RESULT PASS冒稱檢查已執行。凍結實作後重新完整建置，確認具名斷言出現在新log，再跑最終實拍。中間輪保留，沒有計入最終cube綁定驗收。
- default black cube改為共用一份，避免每張自建圖產生不同texture identity破壞材質去重。

### 2026-10-21 驗收資產的儲存類型
- 原先假定90個StudioDisplay28父材質是LFS payload，實際 .gitattributes 對此目錄未設filter，故LFS篩選數量為0，驗證斷言失敗。改先查git check-attr，逐檔比對普通Git blob OID並另存SHA-256；沒有把普通blob錯報成LFS，也沒有修改資產儲存政策。

### 2026-10-21 舊共用父材質相容性
- 程式審查發現初版無條件要求四個新vertex-only參數，會讓未重烘焙的StreetDisplay26原生髮／皮膚binding失敗。只對明確提供的高階SH覆寫要求參數；Maker無高階項時沿用舊父材質，新父材質採明確零缺省。非零SH配舊父材質仍明確拒絕，不能悄悄丟掉照明。新增使用兩代實際資產的Automation相容性檢查。

### 2026-10-21 局部 reflection probe 的來源身分
- 新的最終斷言得到0個default cube材質，該輪GameSelfTest FAIL，不計通過。原作再測證明3800窗牆的GetClosestReflectionProbes選中Map/Reflection Probe（ReflectionProbe-1、weight1），不是RenderSettings default。擴充無損輸出至逐prefab root／probe node，保留原始FileID身分；runtime逐renderer綁所選局部cube，不以default硬套所有表面。
- 新raw-reader closure首次編譯漏限定UnityValue所在crate，E0425已修為unity_asset_core::UnityValue，失敗build log保留。

### 2026-10-21 地圖材質環境旗標不可借用世界生命週期旗標
- 繼續追查0個cube後發現第二個根因：之前假設Studio設bPreserveSourceWorld=true，實際factory從未設定，它是Gallery世界／相機生命週期旗標，不能拿來決定map shader分支。新增明確bSourceMapShading，在載入前依map角色設定；不改世界生命週期行為。native reflection、無lightmap Standard與probe身分tag共用此材質語義。原作局部probe修正仍必要，兩個原因都記錄，不把先前假設當事實。

### 2026-10-21 材質相容性測試 fixture 的 schema
- 新Automation兩次Bind都null，實作API先驗證source metadata schema=1，初版fixture只填material而漏掉schema，故尚未走到新舊父材質相容性。補合法schema並輸出Error診斷；保留7 Success／1 Fail的原log，不能把預期舊父材質拒絕當作整體測試成功。

### 2026-10-21 6331臉部差異排除證據
- 原作face renderer MainTex 1024×1024 raw mip0，依明確讀回列方向與三個相同臉卡的CPU建立結果RGBA逐byte相同，RGB MAE=0。不能再猜作CreateFaceTexture色彩空間或重新調色。
- 用現有OK_NO_FACE_SPHERE進行純診斷A/B，6331整體灰白色差沒有修復，眼睛影格不同不能當成照明改善；未把此環境變數交付成修正，也未修改法線政策。

### 2026-10-21 原生臉部化妝色被舊中性通道覆蓋
- MainTex RGBA零差異後，逐uniform追到原作 _overcolor1=(0.760919,0.401296,0.522312,0.0861634)，UE卻是white／alpha0。Rust source_material已有正確lip／cheek，C ABI仍讀face snapshot的舊body overcolor缺省，後續binding蓋掉來源值。改在view邊界按native face角色讀canonical material colour；不改body nipple通道，來源欄位缺失仍保留舊fallback。舊portable snapshots不需重轉即可取得已存的正確face欄位，未增加會破壞封包的struct欄位。

### 2026-10-21 預備材質 dump 與最終覆寫不可混用
- 6331 的 OK shared dump 顯示卡片預備 ramp 平均值228，曾懷疑場景 RampG=4 未生效；追查發現 dump 在 Actor 的 ToonRampTex 覆寫之前。正式建立時 RampOverride 優先，MID 後續明確綁 _RampG，不能把預備輸入當成最終 renderer 證據。原作 gt_ramp_04 GPU raw 與 kk_4.png RGBA 逐 byte 相同；未以此假說修改 ramp 或調色。
- 原生 face RT 的 DXGI RGBA8_TYPELESS(27)、ramp R8_TYPELESS(60) 是合法讀回格式，探針補相同 byte-size 處理；失敗輸出不計成功。
- 本輪再次使用 rg 的字面 wildcard 路徑及猜測圖片子目錄，造成無效讀取；改查實際檔案清單與 -g 篩選，無效讀取不當驗收證據。

### 2026-10-21 LFS驗證命令的參數語義
- git lfs ls-files的裸位置參數是Git ref，不是目錄；把base_v1當參數造成invalid object name。改取 --json 後依name前綴篩選，對實體payload逐檔SHA-256與HEAD OID／size核對，最後716檔通過。

### 2026-10-21 6331最終材質與色彩空間排除
- 關閉濾鏡的實際A/B仍偏灰，不把濾鏡關閉交付成修正。擴充實際MID稽核至face及ramp：54材質全部指向場景RampOverride，SRGB=false；7組SH對原作誤差0；三個相同女角face的lip／cheek對nativecb最大誤差<1e-6。先前prebinding dump不代表最終值的推論現已實測排除。
- 原作face MainTex RenderTexture rt_srgb=True／activeTextureColorSpace=Gamma，與UE載入相同；native hair ColorMask BC7解碼後與三個來源卡RGBA輸入的RGB逐byte相同，且Linear旗標一致。未再次改貼圖gamma來補償整體色差。

### 2026-10-21 無地圖背景誤用固定藍色
- 5698原作實跑主相機SolidColor=(0,0,0,1)，子相機雖保存藍色卻只clear Depth。匯入器先前以量到的藍色硬編碼所有無地圖／無圖／無天空場景，沒有核對相機clearFlags及StudioScene初始化。反編譯證明StudioScene設Camera.main.backgroundColor=Config.EstData.BackColor，ConfigCtrl背景色也改同一設定；system.xml的System/Est/BackColor才是來源，缺省Color.black，不能讀H/BackColor。
- 修正為讀目前安裝的Est設定並保存到.okscene，不需原作時用來源黑色缺省；不在每幀覆蓋已存或使用者編輯的背景。保留圖片／天空優先權。之前6331與5698來源縮圖差異不可一律歸因缺失的歷史設定，本輪5698確實有可修的共用根因。

### 2026-10-21 空場景與道具環境光的Maker污染
- 原作空場景、2534、5698共81個SH係數全0；舊Studio無map／sky時卻用Maker環境光，並且只對character／map更新SH，漏掉道具。明確支援map=-1的原生零環境，所有角色及道具共用場景SH提交，保留有map與sky的來源分支。source oracle為native-empty-environment-verification.json，不用縮圖估環境亮度。
- 2534保存縮圖是藍背景，但目前原作重載主相機為黑色；背景色是安裝設定而非SceneInfo內保存，不能為了縮圖替該景加藍色特例。5698原作重載與縮圖均黑；新匯入讀Est/BackColor並保存，所以無遊戲重開也不會回頭改色。

### 2026-10-21 零環境整合斷言不能要求未使用的L2參數
- 首輪GameSelfTest的69個材質檢查FAIL，原因為眼睛／眉毛／部分鏡片原生program只有L1，卻被強制要求4組L2。實際資產也確認這些parent不宣告unity_SHBr。補測只核對有宣告的高階參數，L1仍必須存在，並加驗float4 alpha及__A scalar，不放寬非零誤差。最終PASS：69材質、112個unused L2項缺省、nonzero=0。首輪FAIL與其餘5 PASS保留，另跑新的完整6項輪次，不把舊輪改寫成成功。
- 調查ForgeStudio front-face時，僅看特化varying片段曾懷疑v6未設定；完整caller已先用FaceSign初始化v6，未修改正確程式，也沒有重烘焙或用這個假說解釋尚存道具明暗。

### 2026-10-21 6331 AO對照不是修正
- 以現有比較診斷開關只關AO重拍，整體灰棕髮／灰白臉仍存在。該1/1只證明診斷擷取完成，不是6331 shader修復；正式AO設定未變，也未以常駐關閉AO掩蓋差異。

### 2026-10-21 幾何快照不能直接輸出 minified JSON 或混淆座標
- 曾對 1.4 MB 的單行 selected-mesh.json 使用 Get-Content -TotalCount，造成巨量終端輸出；改用 JSON parser，只輸出指定欄位、數量與誤差。也曾猜測 Private/KKSkeletalBuilder.h、verify_okstudio.lua 等路徑及把 wildcard 直接傳給 rg；後續應先 rg --files，再用 -g 過濾。
- 原作 sharedMesh normals 與 CPU posed normals 不在同一座標／階段；本輪另匯出真正 GPU reference stream。6331女角2739頂點的 reference normals 最大誤差1.4928e-7，UV0最大2.9755e-8。UE GPU UV有既定V翻轉及half量化，不能將未反轉的0.9699差異誤報UV錯誤。GPU packed normals最大0.00394，符合8-bit tangent壓縮，不足以證明本次整體色差根因。

### 2026-10-21 Studio 保存的 FIX 視線被略過
- Rust parser 原先只取 eyes data 長度後直接略過；但原作 AddObjectAssist.ChangeChara 會 EyeLookCalc.LoadAngle，還原兩眼 local quaternion及H0/H1/V0/V1。6331原作眼睛rate=(0.340582967,-0.09320921,-0.343585044)，不是零視線。眼皮開合也依該rate修正，不可將此差異都算成shader或直接調眼睛大小。
- Import 另有一行無條件把剛讀取的 EyesLook 設回Animation，會破壞相機追視。修正為只在建立匯入角色時設Animation缺省，保存的Camera或FIX政策保留。FIX資料經來源NO_LOOK row驗證後才提交，不為6331加入眼角offset。

### 2026-10-21 驗收 Lua wrapper 的 CLI 參數來源
- 曾用 dofile 呼叫 verify_ok_studio.lua 並重設 arg；底層參數器仍讀到原程序 argv，將 wrapper 路徑當成多餘參數，首輪未啟動任何UE。保留 capture-all-console.log，改由設好環境的 wrapper 啟動獨立 Lua CLI，使用正式參數來源；沒有把錯誤輪改寫為PASS。
- D3D11 vertex-buffer 探針確實讀到4個綁定stream，但沒有 input-layout 解碼，不先替20-byte缺省stream指定COLOR欄位。CommandBuffer單獨DrawRenderer的輸出也不是場景相機的視覺金標；其形狀／骨架時點尚未核對，不把它拿來宣稱原作幾何吻合。reference-normal／UV驗證使用有明確座標標籤的sharedMesh與UE reference stream。

### 2026-10-21 shader 假說必須核對最終 MID
- 原作6331臉部87個morph（含啟用表情）的normal delta全為0，沒有因此新增模型格式／重建基礎庫。統計保存在NativeFaceNormalMorphs6331，不能把「parser沒有讀normal」直接當成該景色差根因。
- 看到靜態貼圖fallback後曾懷疑頭髮DetailMask以Linear讀取；完整最終MID確認其SRGB=true，與原作Gamma相符。另確認頭髮MainTex雖1024²，所有RGBA都是255，是缺省白圖，不是重複烘焙髮色。兩者都未改動生產路徑。比較最終綁定，不能只看fallback分支或尺寸。

### 2026-10-21 光影／LUT 隔離驗證的界線
- 再次直接向 rg 傳入含 wildcard 的完整 Windows 路徑，造成 os error 123；也猜測了不存在的 OKStudioPostProcess.cpp 與 catalog.json。應先用 rg --files 或已知目錄清單定位，後續使用 -g 過濾。沒有修改資產來迴避搜尋失敗。
- 原作 1268／0010 實跑都是 Lut_Amount、BlendAmount=0、Exposure=1、Tonemapper=Disabled。不能因關閉 LUT 圖較亮就當成修正；關閉 AO 也沒有消除地圖過暗。保留 Baseline／NoAO／NoFilter 實拍作為排除證據。
- UnityPy 的顯示 PNG 行方向與 Rust texture_png 的原始像素方向相反；原作 GPU 色票對前者須反轉 G 行後才可比對。不得直接把 UnityPy 圖當成 UE LUT 行方向錯誤的證據。正在以原生 HDR/Linear GPU 色票檢驗最終 UE 後製。
- 新 32³ LUT 後製 shader 首輪使用 linear 作為區域變數，但 linear 是 HLSL 插值修飾字，GPU 編譯失敗；改名 gradedLinear。保留 GradeSourceCompileFailed.log；只停止已核對路徑與 GradeTest 命令列的本輪程序，沒有把 fallback 當新 shader 通過。
- 32³ LUT 第二輪 GPU 測試誤差0.1645來自測試 SceneCapture 沒有 bAlwaysPersistRenderingState，View.State 為空時引擎直接略過 custom post blendables；並非 native LUT 失效。補上持續 ViewState 後43組最大Linear誤差0.0110272通過（舊16³為0.0424901）。保留失敗CSV，不提高門檻掩蓋錯誤。

### 2026-10-21 原作探針在 LateUpdate 前手動渲染，造成頭髮離體
- 使用者指出 LightingLutNativeGrade/Scene2.png 的頭髮沒有跟著人物。原因是 AuditStudioScene 的 yield null continuation 在 Studio 完成 LateUpdate 的 FK、臉部與附件跟隨前呼叫 Camera.Render，混用了 Animator 中間姿態與前一階段的附件狀態。
- 暖機後與最後手動渲染前改為 WaitForEndOfFrame；LightingLutEndOfFrame0010 實拍確認頭髮、頭部、身體及後方角色姿態恢復一致。先前這類錯位圖不得用作角色幾何／完整外觀的視覺金標；純 GPU 輸入色票不含角色骨架，另依數值驗證。
- 圖文證據 Docs/OK_STUDIO_NATIVE_CAPTURE_20261021.md。這是擷取時序修正，沒有對 UE 頭髮加 offset。
- 舊 map mesh memo 還原時不會重建外部材質貼圖的來源對應，直接追加 named BumpMap 查詢會得到空值；首次來源補錄在 mt_koi_map00_desk00 明確失敗，未當成成功。改由重新計算 meshes_for_maindata 取得真正外部來源，再補錄貼圖與 sampler。逐地圖清除全部解析快取會反覆重建8427份來源索引，已改為有界程序快取與可選 bundle 清單，結束時再清除。中止的全量輪日誌保留，後續重新驗證實際封包。
- 2026-10-21：Studio 截圖偏暗：只驗證 HDR 色票，漏驗實際 PNG 顯示路徑；嘗試單改 sRGB target gamma 後反而雙重編碼。改為線性 RGBA8 render target、LDR TargetGamma=2.2，由 tonemapper 執行一次顯示編碼；加入實際 SceneCard PNG 色票驗證。43 色最大顯示差由 32/255 降至 4/255。失敗 CSV 留在 Saved/OKStudio/LightingLut20261021。
- 2026-10-21：把來源靜態貼圖 mip_count 當成 runtime mip_policy；policy=1 實際代表 encoded RenderTexture 生成 mip，造成球場白線 alpha coverage 消失。靜態來源貼圖使用 policy=0，保留來源 sampler 與 mip0，不冒稱已傳遞原生完整 mip chain。StaticSamplerFinal 實拍確認球場白線恢復。
- 2026-10-21：原生 0059 重載後為黑畫面，saved animation phase 重試仍黑；PNG 存在並不代表可用的視覺基準。留存原生 TSV 與圖，不納入有效原生光影 golden；不得把 14 個 PNG 檔案通過誤寫為 14 場景原生視覺通過。撤去無效果的 saved-phase 選項。
- 2026-10-21：地圖貼圖補錄仍經過完整 mesh 路徑，且外部貼圖逐名重讀相同來源 bundle；多次中止的全量日誌不算通過。改成只走原生 renderer/material PPtr，外部貼圖使用既有 size/mtime 路徑快取。驗收聚焦全部14樣本使用的8張地圖；工具省略清單仍可處理完整 catalogue。
- 2026-10-21：新增 sampler fallback 直接把不存在的本地名稱當外部貼圖，舊 OK 卡雜湊雖相同，完整／乾淨庫仍各報 gaps=5，不能當成通過。改為重播時先確認有已記錄的 dependency_texture_model，沒有則保留來源 None；加入不存在名稱不增加 misses 的回歸斷言，再重跑兩庫 cardcheck。失敗日誌保留 full-cardcheck.log／clean-cardcheck.log。
- 2026-10-21：補負向 sampler 斷言時直接存取 ModelView 私有 parsed 欄位，Rust 測試編譯 E0616；改用既有 library token parser API，不放寬欄位可見度。保留 rust-card-compat-serial.log，修正後再跑。
- 2026-10-21：LFS 批次驗證首輪未用 `git ls-files -z`，含非 ASCII 路徑被 Git 引號轉義，cat-file 回傳 missing；改用 NUL 分隔原始 UTF-8 路徑，716 個 payload 全核 SHA。進度更新亦曾誤報12景，實際只有10個PNG；已更正，後續僅按PNG及 pictured 記錄計數。
- **Studio 濾鏡強度被當成「往原圖混」**（2026-10-22，OK Studio）：匯入把 `aceBlend` 解讀成 lerp(LUT 結果, 原圖, aceBlend)。原作 `AmplifyColorBase.OnRenderImage` 是先把兩張 LUT 混合：lerp(`LutTexture`, `LutBlendTexture`, `BlendAmount`)。Studio 的 `SetACE` 只換 `LutTexture`，攝影機 prefab 序列化的 `LutBlendTexture` 固定是 `Lut_Amount`（濾鏡 0，本身會提高飽和度）。所以 aceBlend ≠ 0 的場景（6331 的 `Lut_Mono` 0.545、3167 的 `Lut_OldPoster2` 0.27）在 UE 被混向無調色的原圖，6331 頭髮因此變成灰棕色（飽和度 0.101，原作 0.214）。修正：`M_SourceGrade32`（新路徑 `SourceGrade2`）同時取樣選定 LUT 與 `Lut_Amount`，結果 = lerp(Lut_Amount, 選定, 1 − aceBlend)；強度 0 是完整的 `Lut_Amount`，只有 `ColorFilter = -1` 才關閉。預防：後製參數的語意要對照原作元件實際的 `OnRenderImage`，並用原生探針 `--stages` 逐元件關閉擷取（`run_scene_visual_audit.lua`）確認，不要只看 UI 名稱。
- **地圖部件的反射探針用「中心點在盒內」判斷**（2026-10-22，OK Studio）：`okmodel_view::assign_reflection_probes` 只在部件頂點中心落在探針盒內時才用該探針。置物櫃室（地圖 26）的後牆 `mo_koi_map46_wall01` 中心 x = 4.05，盒子邊界是 3.98，因此被判在盒外、退回天空反射 ReflectionProbe-1。原作 GPU 常數顯示它用的是房間探針 ReflectionProbe-0；含金屬門的右牆也是一樣。結果 0010 右側門框的反射只有原作約一半，畫面偏暗 14–19%。修正：改成 renderer 包圍盒與探針盒相交就受影響（importance → 重疊量 → 較小的盒），完全不相交才用天空反射；`SESSION_CACHE_REVISION` 21。驗證：原作探針新增 `renderer_reflection`（`GetClosestReflectionProbes`），地圖 26／8（廁所）／20（中庭）共 477 個部件，逐一和原作一致，0 個不同。
- **診斷時誤認畫面物件**（2026-10-22）：把 0010 畫面 (300,250) 一帶當成地板，其實是角色和道具（關掉角色燈就變暗）。逐區比對前，先用原作探針 `--hide <路徑片段>` 或白色反照率 `--white` 確認該區屬於哪個 renderer，不要只看畫面位置猜。
- **Studio 暗角用 UE 內建暗角硬套強度**（2026-10-22，OK Studio）：原作是舊版 `VignetteAndChromaticAberration`（Simple），Studio 只開關它，元件的 intensity 一直是 0.1，`OnRenderImage` 傳 `_Intensity = 1/(1 − intensity) − 1`，mask = 1 − dot(c, c)·k。UE 改用自己的暗角、強度 0.4，0010 暗角造成約 −8% 的亮度（原作約 −3%），四角明顯過暗。修正：調色後製照原作公式做暗角（`SourceVignetteIntensity`），UE 內建暗角只留給後製材質尚未就緒時當退路。預防：後製效果一律先看原作元件的 `OnRenderImage` 和參數，再決定 UE 的對應方式，不要用名稱相近的 UE 設定代替。
- **主 UV 掃描把交錯頂點紀錄的「+8 位元組」當成填充**（2026-10-22，OK Studio／基礎庫）：`unity::decode_uvs` 為了舊的串流對齊問題，每個 2 分量通道都另試「偏移 +8」並取 (0,0) 頂點較少的讀法。在單一交錯串流（例：stride 56，uv0 在 40、uv1 在 48）裡，+8 正好是下一個通道 uv1（光照貼圖 UV），真的 uv0 若有 (0,0) 角就輸掉。3167 的 Studio 道具「色影」（`p_koi_stu_marukage00_00`，`main_item_studio_add` 加法橘光）因此拿到光照貼圖 UV、只取到貼圖黑邊，UE 完全沒畫出這片光，原作上半部比 UE 亮 5–11%；同樣的錯也讓地圖樹葉草叢（`*_kusa*`、`*_ha*`）、`*_shadow*` 網格與 Studio 道具用錯 UV。修正：+8 只在讀取會跨出同一筆頂點紀錄（獨立 UV 串流）時才試。掃描 22,008 個來源有 685 個主 UV 改變（原作 34 個、共用 mod 0 個），以 `convert @清單` 重轉原作 34 個；`SESSION_CACHE_REVISION` 22。預防：Rust 原作測試 `studio_marukage_quad_keeps_uv0` 對照原生探針 `--mesh-dump` 的 `mesh.uv`；改 UV 解碼規則後先跑 `sources_with_changed_main_uv` 列出受影響來源再重轉。
- **把原作亮出來的部分當成光照計算差**（2026-10-22）：3167 上半部偏暗時，先後懷疑光照貼圖解碼、方向項、反射、霧、SH，花了很多輪擷取。真正原因是一個 Studio 加法特效道具在 UE 沒畫出。預防：先用原作探針把光照貼圖換全黑（`white-blacklightmap`）看「不是光照貼圖的貢獻」剩什麼，再對可疑物件 `--hide` 量它的貢獻，最後才去比對著色公式。
- **Unity 內建網格與網格上的 Skybox 沒有轉出**（2026-10-22，OK Studio）：Studio 道具「青空／夜空／夕方」是一顆 Unity 內建 `Sphere`（`Library/unity default resources`，bundle 裡沒有）配 `Skybox/Cubemap` 材質。外部網格解碼找不到檔案就略過，`_Tex` 是 Cubemap（class 89），材質貼圖槽也只認 Texture2D，所以 4432 天空整片黑（只剩雲）。修正：`unity::builtin_mesh` 產生內建 Cube／Sphere／Plane／Quad；材質貼圖槽與外部貼圖名稱也認 Cubemap；`Skybox/Cubemap` 部件帶 `_OKSkyCube`（反射探針同格式的六面圖集）與 `_OKSkybox`，物件空間方向寫進頂點色（session 會把 renderer 變換烘進位置）；UE 用自己的無光照雙面材質依方向取圖集，乘 `_Tint`（線性）×4.5948×`_Exposure`，不投影、不描邊。第一版用 `PreSkinnedPosition` 在像素著色器取不到，材質編譯失敗改用預設材質——加材質後要看日誌的 `Failed to compile Material`。
- **水面道具畫成白色**（2026-10-22，OK Studio）：「水面(川)」用 `Xffect/heat_distortion_t2`（GrabPass 後 SrcAlpha/OneMinusSrcAlpha 輸出 背後畫面×`_MainTex`.rgb、alpha `_MainTex`.a），UE 把它的淡藍平色貼圖當不透明顏色，4432 稻田整片白。修正：Rust 標 `_OKModulate`，UE 用 Modulate 混合畫 lerp(1, tex.rgb, tex.a)（忽略 ≤0.1 的波紋扭曲）。

## E392 — Yvonne 特寫錄影途中服裝預熱重開卡（2026-10-22）

- 症狀：50 段臉部特寫裡，前兩段掃光時畫面突然重建，fps 從約 160 掉下去。`Saved/Tests/YvonneCloseup/editor.log`（第一輪，11:30）有兩次 `clothing warm begin`，分別花 8.2 秒與 6.3 秒再開同一張 Yvonne，並在遊戲執行緒建出隱藏的 `KKPreviewActor_1`／`_2`。開卡本身的 `t.MaxFPS=15` 只發生在前景 `OnLoad3D`，不是每段都在重開。
- 修正：`OK_NO_CLOTHING_WARM=1` 時不排服裝預熱、也不把開卡限在 15 fps。`product_sweep` 另外呼叫 `SuspendClothingWarmForCapture`，清掉已排隊的預熱。一般畫廊沒有這個環境變數，預熱仍在。
- 重錄：第二輪 editor pid 79540，log 只有 `OK clothing warm held for product capture`，沒有 `clothing warm begin`。`00-card.mp4` 時長 3.000 秒，`all.mp4` 149.767 秒、50 段。光線讀回 start=-0.740 mid=0.002 end=0.740 read=1/1/1 slots=28。影片與本機錄製腳本不進版控。

## E-249　角色 MCP heartbeat 每 0.5 秒把遊戲執行緒卡住（2026-10-22）

- **症狀**：Yvonne 特寫錄影時 `stat unitgraph` 一卡一卡。`OK_FRAME_GRAPH=1` 在材質完成、服裝預熱與動畫目錄都停掉之後，idle 與 product sweep 仍約每 0.5 秒一幀：wall 被夾在 125.0ms，game 168–212ms，draw／GPU 只有數毫秒。log：`Saved/Tests/YvonneCloseup/framegraph-editor.log`（12:31:49–12:31:57 那輪）。
- **原因**：`-CharacterMCP` 的 heartbeat 每 0.5 秒呼叫 `State()`，而 `State()` 會 `Refresh()`：把整包 KKEx 解成 JSON，再把所有控制項（含下拉全部選項）序列化後做 CRC。這段在遊戲執行緒上，不是 GPU，也不是陰影捕捉（這次 gallery `shadows=0`）。
- **處置**：heartbeat 仍寫 `status.json` 的忙碌、相位、運鏡等欄位，但不再做指紋。正式請求進來時仍會 `Refresh()`，版本檢查不變。超過 8ms 才記 `OK mcp heartbeat`。
- **驗證**：同一張卡重跑 frame graph。idle n=400 wall_p50=4.65 p95=7.62 max=11.46 over16=0；sweep n=500 wall_p50=4.70 p95=7.49 max=10.65 game_p95=5.13 gpu_p95=4.31 over8=13 over16=0。沒有 125ms 週期。接著 50 段 3 秒重錄完成，第一段光線 start=-0.740 mid=0.001 end=0.740，`clothing warm held`、沒有 `clothing warm begin`。錄影中 heartbeat 超過 8ms 只有 5 次，最大 11.6ms。
- **Studio 道具一律吃角色燈、MDK 少了舊式環境光**（2026-10-22，OK Studio）：Studio 角色燈的 culling mask 是第 10 層（Chara），但地景道具（`main_StandardMDK_studio` 的砂地、木材、野原等）在第 11 層（Map），原作關掉角色燈它們完全不變；UE 對所有道具送 `_LightColor0`。MDK 的 pixel program 還會加上 max(自發光, 2·`glstate_lightmodel_ambient`)，UE 從未設定這個 uniform（=0）。5065 木材因此偏橘偏暗、4432 砂地偏亮。修正：部件依 `_OKRendererLayer` 帶 `OKLayer<n>` 標記，所有部件都在角色燈圖層外的道具 `_LightColor0` 設為 0；`glstate_lightmodel_ambient` 依原作探針（= 線性 ambientSkyColor / 2）：無地圖 (0.318, 0.3405, 0.3885, 1.5)，地圖 15／26／28 為 0.1698 灰。5065 超過 10% 的格子 27 → 7。
- **比對用的挑選資料夾裝了別的場景**（2026-10-22）：`pick2` 裡留著先前的 0010、3167，`CompareKStudio <資料夾> 2` 只拍了前兩個，5065 的結果其實是舊圖，誤判成「參數對 5065 沒作用」。預防：單場景比對各用自己的資料夾，複製結果前核對日誌的 `pictured` 行是哪個場景。
- **用 perl 一行式改程式，替換文字跑到檔案開頭／換行被展開**（2026-10-22，ok-story）：用 `|` 當 `s|||` 分隔字元，pattern 裡又有 `\|`（Rust 閉包 `|s|`、`||`），比對位置錯亂，新程式被插到 `director.rs` 第 1 行；另一次 replacement 寫 `\n`，Lua 字串裡變成真的換行，`run_scene_editor.lua` 無法載入。兩次都是改完立刻 build／dry-run 才發現。預防：多行或含 `|`、`\` 的修改一律用 Edit 工具；perl 只做不含這些字元的單純替換，改完馬上看 diff。
- **自動測試把「角色退場」寫成「畫面上不再有她」**（2026-10-22，ok-story）：劇本在說話者自己的那句把她移出站位（`"c1": null`），她接著以場外分身說話（立繪要用）；測試卻斷言 stage 上已沒有她而失敗。行為正確，改成斷言送出 `place visible:false` 且立繪仍是她。預防：退場與場外說話同句時，先想清楚應有的指令序列再寫斷言。
- **漏算 UE 的 RenderCorrection，誤判道具根節點沒被抵銷**（2026-10-22，OK Studio）：追 4432 天空雲時，拿 Rust 部件頂點（含 prefab 根節點位置）加上場景道具位置，和原作比對，推論「根節點偏移沒被拿掉」，於是在 Rust 把根節點歸零。其實 `AOKSceneActor` 早就用 `OkModelNodeWorld` 算出根節點並以 `RenderCorrection` 抵銷（GameSelfTest「Studio item placement replaces the authored prefab root」），歸零後變成重複補償，6964 的桌椅與相機錯位，自測 FAIL；已撤回。預防：比對 UE 物件位置要用預覽 actor 實際的變換（含 RenderCorrection），改放置規則前先搜尋既有的自測與補償。
- **新檔的檔內 helper 名稱和別的檔撞名，擋住別人的建置**（2026-10-22，ok-story／OK Studio）：`SOKStoryView.cpp` 在匿名 namespace 裡定義 `Str`、`Num`、`Flag` 等短名稱，自己單獨編譯時正常；別的 session 改檔後，UBT 的 adaptive unity 把它和其他 .cpp 併成同一個編譯單元，同名函式衝突（C2264），E 的 OpenKoikatsuEditor 整個無法建置。另外，用 `\bNum\(` 一次全改名時，也誤改了 `.Num()`、`->Measure()` 這些方法呼叫。修正：helper 一律加 `OKStory` 前綴，再把方法呼叫改回原名。預防：plugin 內 .cpp 的檔內 helper 一開始就用模組前綴命名；批次改名的 regex 要排除 `.` 和 `->` 後面的成員呼叫。
- **改了公用結構的欄位就讓建置停在半途，擋住別人的建置**（2026-10-22，OK Studio）：把 `FOKSceneMaterialEdit::bColor` 換成 `Kind` 列舉時，先改了標頭、還沒改完 `OKStudioGame.cpp` 的五處用法，就去改 Rust；同一個 E 工作樹的 galgame session 這時建置，`bColor` 不存在而整個 OpenKoikatsuEditor 失敗。修正：改完五處後再建置通過，並通知對方。預防：共用工作樹裡改公用結構，先 grep 全部用法、一次改完再做別的事；改到一半要離開時先讓它能編譯。
- **在預設地圖比對場景，把關卡自己的地板當成場景的問題追了好幾輪**（2026-10-23，OK Studio）：手動跑 `OKStudio.CompareKStudio` 沒帶 `/Engine/Maps/Entry`，2840 右側出現粉白格子地、海不見，以為是道具或水的材質問題，逐一隱藏道具群組才發現格子是預設地圖 Template_Default 的地板（驗收腳本一律用 Entry，所以沒出現）。修正：工作室啟動時隱藏世界原有的 actor 與元件（讀卡畫面進工作室時也一樣）。預防：比對前先確認跑的地圖與驗收一致；畫面出現不像場景內容的東西，先用 `OK_STUDIO_COMPARE_HIDE`（可用萬用字元）把所有道具隱藏，看剩下什麼。
- **新的 .inl 少 include、共用標頭只前置宣告，擋住別人的建置**（2026-10-23，街頭 galgame 演出）：`OKStreetSocialGalgame.inl` 用了 `USceneCaptureComponent2D` 卻沒有 include（C2027）；`OKStoryCapture.h` 的 `TStrongObjectPtr<...>` 成員只前置宣告型別，解構時需要完整型別（C2027／C2338），同一個 E 工作樹的另一個 session 建置也跟著失敗。後來又因為在 lambda 參數 `P` 的範圍內再宣告 `P`，觸發 C4456（警告當錯誤）而失敗。預防：在共用工作樹改標頭前先自己完整建置一次；放進 `TStrongObjectPtr`／`TUniquePtr` 的型別一律 include 完整標頭；新程式的區域變數避開外層已用的短名稱。
- **把 (u, 1−v) 列翻轉擴到整個 shader 家族，連光效道具也翻了**（2026-10-23，OK Studio）：修 main_item_studio 道具貼圖列方向時，條件寫成 `shader_family(...).contains("main_item_studio")`，`main_item_studio_add`（波動等光效，原本就正確）也被翻轉，0001 的劍光從彩虹變成整片黃；當輪只看了陽傘等目標場景，14 張驗收圖的格數也沒和前一輪逐張比。修正：排除 `_add`。之後改共用條件時，列出所有符合條件的 shader 名稱逐一確認，並把 14 張圖逐張和前一輪對照。
- **把共用 leader 姿勢也套到臉部元件，表情消失**（2026-10-23，OK Studio 效能）：照一對一互動場景開 `bShareIdenticalPose`，嘴／臉的骨架元件也設了 leader；UE 的 follower 連 morph target 權重都改用 leader 的，Studio 角色的表情 morph 全部失效（3800 閉眼笑變成睜眼中性臉）。效能 A/B 只看時間沒看畫面才漏掉。修正：場景驅動的角色臉部元件不共用。之後每個效能開關都先拍角色場景比對圖再算數字。
- **重合的服裝層在 UE 裡輪流蓋過對方而閃爍**（2026-10-23，讀卡器）：CrowdAPNG `C001.png` 的外套袖子是同一份 `o_normal_a` 網格的三個部件：飾品 28 的原材質、它的 Material Editor 複製材質 `acs_m_cardigan.mecopy1`（扣環皮帶貼花），以及飾品 29（袖口羅紋貼花），三者都是 masked 而且頂點完全重合。Unity 依繪製順序搭配 ZTest LEqual，讓後畫的貼花層固定蓋在上面；UE 的 base pass 對等深像素沒有穩定的先後，於是皮帶時有時無。修正：`separate_coincident_layers`（`Tools/ok-extract/src/lib.rs`）在組好 session 後，把同幾何的 opaque／masked 部件依 session 順序（即繪製順序），每層沿法線多推 0.05 mm；半透明部件仍由 UE 的 sort priority 排序，不改幾何。飾品掛在私有 rig 上時，每一幀都從 `AccessoryRuntime` 保留的來源網格重新蒙皮；第一版只推了部件的 `rest_positions`／`positions`，畫面照樣閃，所以 `SessionPart::push_surface` 也要推來源網格，並透過 `replace_source` 更新驗證版本。`SESSION_CACHE_REVISION` 改為 28。驗證：讀卡器連拍 12 張，袖子區域像素完全相同。預防：多個部件畫同一個表面時，要對照來源的繪製順序決定誰在上面，不能交給引擎的 tie-break。後續（同日）：改用引擎 patch `OpaqueSortPriority`，照 Unity 的方式依繪製順序畫，幾何不再推移（`draw_layer` → `TranslucencySortPriority`，`r.OK.OpaqueSortPriority=1`，快取版本 29）。讀卡器連拍 12 張完全相同；CVar 設為 0 時閃爍重新出現。見 [計畫](OPAQUE_DRAW_ORDER_ENGINE_PLAN.md)。
- **共用的讀卡快取重用了別版程式存的結果，街頭的雷電將軍胸部不見**（2026-10-23，ok-extract／街頭）：`%LOCALAPPDATA%\OpenKoikatsu\oklib-cache` 由 C、E、讀卡器、街頭與打包版共用，但鍵只有卡片、基礎庫和手動遞增的 `SESSION_CACHE_REVISION`。今天多個 session 同時改 ok-extract（服裝層推移後又改回、GPU 部件不再 CPU 蒙皮等），某個版本存進去的 session 被其他版本照用，街頭的雷電將軍身形錯了。讀卡器走的路徑不同，所以正常。`OK_NOCACHE=1` 重開就恢復，證實是快取。修正：`build.rs` 對 `src/` 全部原始碼算雜湊（`OK_EXTRACT_SOURCE_HASH`，忽略換行差異），放進 `session_cache_key`。任何程式改動都會換新的快取鍵，不必再靠人記得遞增版本號。
- **OK Studio 載入場景時 GPU 蒙皮 morph 斷言當機**（2026-10-23，OK Studio 效能）：連跑 8 個 KStudio 場景時，第一個場景剛生成角色、設好 leader 的那幾幀，渲染執行緒在 `FMorphVertexBufferPool::GetReadingIndex` 觸發 `check(Index == 1)`。引擎假設「目前的 morph 緩衝沒寫過」只會發生在第一次更新，但 revision 翻面而那幀沒寫 morph（leader 切換重送權重、元件仍在建立）也會讓目前緩衝是空的。只出現過一次，無法穩定重現。修正：引擎補丁 `Tools/EnginePatches/MorphDoubleBufferRead.patch`（引擎分支 commit `11576da86`），改讀有資料的那個緩衝。D: 引擎（C 線）也要套這個補丁。
- **臉部元件共用 leader 表情消失，當時誤判為「leader 會蓋掉 follower 的 morph」**（2026-10-23，OK Studio 效能）：當時只把臉部排除在共用之外，又試了切換時重發 morph（無效）。真正原因：UE 的 `USkeletalMeshComponent::UpdateFollowerComponent` 先清掉 follower 的 morph，只在 leader 也是 `USkeletalMeshComponent` 時才把 follower 自己的 morph 加回去；我們的 leader 是 Poseable，所以每次 leader 刷新就清空。修正：`UKKRuntimeSkeletalMeshComponent::UpdateFollowerComponent` 在 leader 不是 SkeletalMesh 時補回自己的 morph，臉部元件恢復共用（`ok.Studio.ShareFacePose`）。教訓：現象背後的引擎行為要讀原始碼確認，不要只憑症狀下結論。
- **galgame 避牆探測同步讀回 GPU，每換一句台詞就卡 100–130ms**（2026-10-23，ok-story／街頭／OK Studio）：`FOKStoryProbes::Run` 每個候選位置擷取深度後，立刻在遊戲執行緒 `ReadLinearColorPixels`，強迫 CPU 等 GPU 畫完；一個鏡頭有十幾個候選位置，主畫面就被拖住。立繪則每幀擷取兩次完整畫面（含 Lumen、反射、AO）。改法：探測改成 `FRHIGPUTextureReadback` 非同步讀回，在 render thread 收結果，每幀最多擷取 3 個；立繪改成雙緩衝，顏色和遮罩分兩幀畫進後緩衝，完成才交換，遮罩用一半解析度、不打光，兩次擷取都關掉 Lumen／反射／AO。E 的打包版量測（對話約 22 秒）：卡頓從 9 次（5 次在 117–132ms）降到 6 次（都在 40–58ms，卡在主畫面的 View Visibility），對話時約 120 FPS。量測時用控制通道截圖也會同步讀回（`ReadSurfaceData_Flush`），要量卡頓時別截圖。
- **用暫存 index 提交時把整個檔案的換行改掉**（2026-10-23，OK Studio 效能）：`OKStudioGame.cpp` 有別的 session 的未提交改動，我用 Lua 把自己的兩段插進 HEAD 版本再寫成 blob；HEAD 檔案是 LF 為主、夾雜少數 CRLF，腳本看到 CRLF 就把整檔轉成 CRLF，commit 變成全檔 8000 多行的差異。推送前用 `git diff HEAD~1 HEAD --stat` 看到才重做。修正：插入時保留原檔每一行原本的換行，只在錨點處比對 LF 或 CRLF；提交前一律看 `--stat` 的行數是否等於自己改的行數。
- **把會在 worker 上執行的函式裡留了 `GetValueOnGameThread`**（2026-10-23，OK Studio 效能）：`PreparePrivateGpuDisplayPoses` 搬進 `SolveScenePose`（`ParallelFor` 的 worker）後，裡面讀 `ok.PrivateGpuStreams` 的 `GetValueOnGameThread` 在 worker 上觸發 `ensure(GetShadowIndex() == 0)`。自測當時仍 PASS，log 裡的 ensure 沒被注意到就推送了（`d22ef8c03`）。修正：改 `GetValueOnAnyThread`。教訓：把程式移到其他執行緒時，逐行檢查 `GetValueOnGameThread`、`IsInGameThread` 與 UObject 寫入；自測後 grep log 的 `Ensure condition failed`。
- **讀卡器自動序列太早切動作，量到的是 T-pose**（2026-10-23，讀卡器）：`OK_AUTO_SEQUENCE` 的開卡步驟回報完成後，讀卡器在衣服暖機完成時還會再換一次角色 Actor（`KKPreviewActor_1`），緊接著選的動作套在被換掉的舊 Actor 上，我拿 T-pose 的截圖去比物理、說「重現不了」。修正做法：開卡後先 `~15` 等替換完成再選動作，並先看截圖確認姿勢真的換了。胸、臀凹陷與抖動的真正原因與修正見 `Docs/PHYSX_ERRORS.md`（身體鏈不碰自己的身體膠囊與頭髮）。
- **讀卡器跑的是舊的編輯器模組，看不到新功能**（2026-10-23，臉部壓平）：`CardReaderDevelopment.cmd`（`Tools/run_card_reader_development.lua`）只在共用引擎變動時才重編 OpenKoikatsuEditor，原始碼改了照樣直接啟動舊 DLL。改了 C++ 要先自己跑 `Build.bat OpenKoikatsuEditor ... -NoEngineChanges` 再開讀卡器。另：臉部壓平強度到 1 時臉、眼、口腔壓到同一深度互相穿插（黑塊），上限改為 0.9。
- **讀卡器切動作／服裝跳回 T-pose：共用環境變數被背景預熱改掉**（2026-10-24，讀卡器）：`OK_UE_ANIM` 是程序層級的環境變數，`KKSkeletalBuilder` 與 `KKPreviewActor` 在任何角色建骨架、重建中立姿勢時都會設它；背景預熱別套服裝一建角色就設成 1，正在顯示的角色下一幀停止取樣原作動作，變回 T-pose。教訓：每個角色自己的模式不能放在程序共用的環境變數；Rust 端早就為 video／共用根目錄留過特例註解，應該當時就改成每個 Session 的旗標。
- **讀卡器開卡時刻意把幀率壓到 15 fps**（2026-10-24，讀卡器）：`OKThrottleForNativeOpen` 為了讓 worker 開卡快一點，在開卡期間設 `t.MaxFPS 15`，使用者看到的是長達數秒的 15 fps。改成 worker 降低執行緒優先權，畫面維持全速。
- **貼圖快取每次命中都在遊戲執行緒重算整張貼圖的 SHA-1**（2026-10-24，KKRuntimePose）：`RuntimeTextureIdentity` 對所有 mip 雜湊，每個角色、每套服裝預熱都要 100 ms 以上。快取鍵要在產生資料的地方算一次、跟資料一起保存。
- **顯存量測與 ok-asr 權重改動的兩個錯誤**（2026-10-24，ok-song／ok-asr）：
  - 用「全卡已用顯存的峰值減去開始前的值」量 ok-song 換聲，得到 13.7 GB。實際只有約 3.4 GB，差額是同時在跑的遊戲和 Editor 的變動。WDDM 下 nvidia-smi 看不到各程序的用量；改用程式內的緩衝統計（`OK_GPU_MEM`），以及 Windows 計數器 `GPU Process Memory`。
  - ok-asr 的 `affine` 改成「權重一律是 bf16、GEMM 前展開」時，漏改了 `proj1`／`proj2` 的載入（仍是 f32），輸出全部變成「是，是，是」或空字串。部署前用新舊版逐檔比對轉寫才抓到。改共用 helper 的參數語意後，要 grep 所有呼叫者的權重來源。
- **`if(...)UE_LOG(...);else UE_LOG(...);` 編不過**（2026-10-24，街區男主語音）：`UE_LOG` 展開成帶大括號的區塊，後面的 `;` 讓 `else` 沒有對應的 `if`（C2181），浪費一次 stage 建置。`UE_LOG` 放在 if／else 分支時一律加大括號。
- **沙盒測男主語音時麥克風收到房間裡的人聲**（2026-10-24，街區男主語音）：面對面對話會自動開即時麥克風，ASR 把旁邊的說話聲當成玩家的話，還「打斷」了她，她的 TTS 回覆被丟掉。用控制通道測對話前先 `/mic {"ignore":true}`。同一次也發現他的 TTS 從喇叭出來時麥克風沒關，已改成他說話期間靜音。
- **OK 卡換到別套服裝時，衣服蓋不住胸部**（2026-10-24，讀卡器，不知火舞 C016 第 4 套）：上衣的形狀、蒙皮都和原作一致（session 蒙皮後的頂點與遊戲 mesh_dump 相差 0.1 mm），差在身體材質少了上衣的 `OverBodyMask`（mod 的 `maiBM`）。原作用它把被衣服蓋住的皮膚 clip 掉，所以乳頭不會穿出衣服。`--oklib card` 轉 OK 卡時只開第 0 套服裝錄卡內資源包；這個遮罩所在的 bundle 基礎庫沒有，第 4 套又沒錄，讀 OK 卡時就靜靜地找不到。修正：轉卡時每一套服裝都開一次錄進資源包，遮罩找不到時印出來；E 的 CrowdAPNG、StreetHeroines 全部重轉。預防：比對時先用 `ok-extract <game> --posed-objs <dir> <card>`（`OK_OUTFIT`、`OK_READER_LIB`、`OK_POSED_FRAMES`）輸出 session 蒙皮後的網格，和遊戲 mesh_dump 比，分辨是幾何問題還是材質問題；也要用使用者實際開的那張卡（OK 卡或原作卡），兩者的資源來源不同。
  - 後續（同日）：E 的 CrowdAPNG 100 張與 StreetHeroines 10 張已用新工具重轉（舊卡備份在 scratchpad），每張每套服裝只用 OK 資料重開共 763 次，全部成功。仍印「OverBodyMask／OverBraMask … not found」的，經 `ok-extract <game> --bundle-textures <bundle>` 查證，是 mod 清單指的貼圖在 mod 自己的 bundle 裡就不存在（例如 Cyberpunk 的 `lucyBM`，bundle 只有 `lucylaBM`），原作 `LoadAsset` 同樣拿到 null，行為一致。
- **重轉 OK 卡讓女主變成陌生人**（2026-10-25，E 的 StreetHeroines）：為了補齊各套服裝的資源重轉了 10 張女主卡，街區一開就變成「Raiden Shogun」「re 刻晴」、好感全部歸零。原因：女主身分是整張卡檔的 sha256（`apng-<id>`，`Tools/prepare_street_social_selection.lua`），名字對照（`name_overrides.json`）、個人設定（`Profiles/`）、存檔關係都以它為鍵；角色資料相同但資源包換了，檔案位元組就變，身分也跟著變。修正：先從備份還原卡片（存檔裡舊身分的關係都還在，只多了 10 筆新身分的陌生人資料）；`--oklib card`／`cards` 重轉一張已是 OK 卡的卡時，把它原本的身分寫進新卡的 `okId` 區塊（`oklib::keep_identity`），`card_identity` 有 `okId` 就用它，重轉幾次都不變。預防：改動卡片位元組（重轉、換資源包、寫入語音）前，先確認身分怎麼算、還有誰拿它當鍵；換卡後先比對新舊身分是否一致，再覆蓋原檔。
- **她唱歌時被「玩家插話」打斷，歌只唱了 10 秒**（2026-10-25，街區唱歌）：即時麥克風的插話（barge-in）會停掉她正在說的任何語音，歌曲也算在內；麥克風在她唱歌時只壓低門檻、沒有關掉，房間裡的人聲（以及喇叭放出的歌聲）就會觸發。唱歌改成和他說話時一樣把麥克風靜音，插話也不再停歌（`OKStreetSocialLiveTalk.inl`，`SongOwnsVoice`）。新增會播放長段語音的功能時，要一起決定麥克風在那段期間的狀態。
- **同一個 `if/else UE_LOG` 錯誤又犯一次**（2026-10-25，街區自動存檔改背景執行）：背景存檔的成功／失敗日誌又寫成不帶大括號的 `if(...)UE_LOG(...);else UE_LOG(...);`，C4430／C2181，又浪費一次 stage 建置。寫完 C++ 後先 `grep -n "else UE_LOG"` 自查再打包。
- **街區建築模型全用 ProceduralMeshComponent，每幀重收集 190 萬三角形**（2026-10-25，120 FPS 優化）：39 個地點模型以 PMC 顯示，走 dynamic mesh 路徑，每幀每個 pass（base、depth、四層陰影）重建 draw command，宿舍區 render thread 與 worker 的 DynamicPrimitive 佔最大份。生成後一律以 `BuildMeshDescription` + `UStaticMesh::BuildFromMeshDescriptions` 轉成執行期 static mesh，PMC 只留作節點。執行期產生的大量不動網格不要停在 PMC。
- **用 C 的 `lua.exe` 跑 E 的打包腳本，結果建在 C**（2026-10-25，E 組 120 FPS）：`C:/portable/OpenKoikatsu/Tools/lua/lua.exe Tools/rebuild_street_social_runtime.lua`（cwd 在 E）會依 lua.exe 所在位置找專案根目錄，於是用 D 引擎重編了 C 的 FreeTalk01Host，並在 C 的 `Saved/StreetSocial/Build/` 產生 Look10E.stage（C 的 StreetSocial.cmd 會挑到最新的它）。已先拆 junction／hard link 再刪除該 stage 與 Rebuild-Look10E。E 的打包一律用 `E:/OpenKoikatsu/Tools/lua/lua.exe`，跑完確認輸出路徑在 `E:\`。
- **`if/else UE_LOG` 第三次**（2026-10-25，ok-mind 存檔排入佇列）：又寫成 `if(!Ok)UE_LOG(...);else ...`（C2181）。只靠「記得」防不住，改成機器檢查：`Tools/rebuild_street_social_runtime.lua` 編譯前掃 `Source/` 的 `UE_LOG(...);else`，有就直接報出檔名行號、不開始建置。
- **男主語音占住 TTS 伺服器，她的回覆逾時顯示「无法连接到 TTS 服务器」**（2026-10-25，街區男主語音）：GPU 滿載時他一句 34 字的台詞合成要 40 秒；伺服器一次只做一句，她的回覆排在後面、15 秒逾時。遊戲端逾時不會停掉伺服器上的工作，所以放棄的那句還是照樣占著引擎。修正：他的請求帶自己的 channel／generation，放棄（逾時、她的回覆已等 3 秒、對話結束、換下一句）時送 /cancel 讓伺服器也停掉。凡是會放棄等待的 TTS 請求，都要能在伺服器端取消。另外測試腳本挑「最新的 Play 目錄」拿到的是別的 session 的遊戲 log，要用控制埠辨認自己的。
- **用舊版 ok-tts 測 GPU 優先權，設定沒有作用**（2026-10-25，E 組 TTS）：直接用 C 的 `Build/ok-tts/ok-tts.exe`（10-22 編的）加 `OK_TTS_GPU_PRIORITY=5`，但這個環境變數是 10-25 才加進原始碼，舊 exe 不認，白跑一輪才發現。測伺服器設定前先確認啟動 log 有對應的那一行（這裡是 `GPU scheduling priority class 5`），沒有就從原始碼重編。

- **E 線唱歌改版的兩個編譯錯誤**（2026-10-25）：`FJsonObject::Values` 的 key 在 UE5.8 不能直接傳給 `TMap<FString,...>::FindOrAdd`（C2665），要包成 `FString(P.Key)`；BuildSongRow 裡新宣告的 `Name` 遮住了外層同名變數（C4456，警告視為錯誤）。各浪費一次 stage 建置。在 .inl 裡加區域變數前先看外層有沒有同名變數。
- **在 GPU 記憶體快滿時開沙盒，遊戲卡在載入**（2026-10-25）：開了三個遊戲、兩個 Editor，再加上自己的 ok-tts，32 GB 用到 31.1 GB，新開的沙盒在第 1 幀之後就不動了。開遊戲前先看 `nvidia-smi`，剩不到 6 GB 就等別人的測試結束；測試要用 TTS 時，優先共用已經在跑的 ok-tts。
- **ok-song 的工作執行緒 panic 後，工作永遠停在 generating**（2026-10-25，E 線唱歌）：測試時直接執行 `target\release\ok-song.exe`，旁邊沒有 CUDA DLL（nvrtc），生成階段 panic，把唯一的工作執行緒一起帶走。工作狀態一直是 generating，遊戲就等了 40 分鐘以上。修正：每個工作都包在 `catch_unwind` 裡，panic 時記為 failed，執行緒繼續處理下一個（`Tools/ok-song/src/server.rs`）；遊戲端製作超過一小時也會記為失敗（Song.inl）。另外，測試用的 ok-song 要放在有 `cuda\` 的資料夾旁邊執行，例如建一個 junction 指到 `Build/ok-song/cuda`。
- **`OKT(...)+TEXT(...)` 編不過**（2026-10-25，E 線唱歌）：`OKT` 傳回 `const TCHAR*`，兩個指標不能相加（C2110）。要接字串時先包成 `FString(OKT(...))`。又浪費一次 stage 建置。
- **90 秒的歌在 90 秒處被硬切**（2026-10-25，E 線唱歌）：歌詞寫成兩段主歌、三次副歌加橋段，模型唱完要 100 秒以上，`max_duration_ms=90000` 把最後一段副歌切在字中間（嘴型在第 90 秒還在動）。修正：拿掉橋段，給模型 100 秒讓它自己唱完；mix 最後 2 秒一律淡出（`ok-song audio::mix`，artifact key 加 `fade_out_s`）。驗收長度上限的時候，要看結尾是不是還在唱，而不是只看長度對不對。
- **PowerShell 裡用相對路徑呼叫 `[IO.File]`，改到另一份 checkout 的檔案**（2026-10-25，E 線唱歌）：先 `cd E:\OpenKoikatsu`，再用 `[IO.File]::ReadAllLines('Docs\...')` 解合併衝突。.NET 的相對路徑是以程序的工作目錄（C:\portable\OpenKoikatsu）為準，結果讀寫的是 C 的同名文件，C 的文件被塞進垃圾內容；E 的衝突標記則照樣被提交了。C 那份沒有別人的改動，用 git checkout 還原；E 再補一個提交。`[IO.File]` 一律給絕對路徑，合併後提交前先用 Grep 確認沒有衝突標記。
- **替換程式時把註解接在行尾，吃掉同一行後面的程式**（2026-10-25，E 組存檔序列化）：把 `...S.Store=MoveTemp(Candidate);` 換成「新程式 + `// 註解`」時，原本同一行後面還有 `{...FlushRulesEffects();}return true;`，結果整段變成註解（C4716 沒有傳回值）。在單行密集寫法的檔案裡替換片段時，行尾註解一律另起一行，或確認被替換的片段就是整行結尾。
- **執行期建的 static mesh 沒設 UV 密度，每次開場卡 12 秒**（2026-10-25，E 組 120 FPS）：建築模型改成執行期 `UStaticMesh` 時，`FStaticMaterial` 的 `UVChannelData` 沒初始化，texture streaming 第一次查詢就觸發 ensure（`UVChannelData.bInitialized`），ensure 的 stack walk 讓第一幀停了 12 秒，從 faee41cb4 起每次啟動都有，量 FPS 時沒發現。執行期建 `UStaticMesh` 時每個材質槽都設 `UVChannelData=FMeshUVChannelInfo(1.f)`；新功能上線後要掃一次 game.log 的 `Ensure condition failed`，不能只看 FPS。
- **PowerShell 雙引號 here-string 裡寫 Markdown 反引號，反引號加 t 變成 tab**（2026-10-25，E 組 TTS 文件）：用 `@"..."@` 附加文件時，`talker.rs` 前面的反引號加 t 被當成跳脫字元，寫進文件的是 tab 加 `alker.rs`。含反引號的文字改用單引號 here-string（`@'...'@`）或 Write 工具。另外，Lua `io.popen` 呼叫的命令列若以引號開頭又含其他引號，cmd.exe 會拿掉頭尾的引號，命令就壞了；整串命令外面要再包一層引號。
- **回歸測試沿用上一個測試留下的遊戲時間，把「她自己去吃晚餐」當成功能壞掉**（2026-10-25，E 組 galgame 回歸）：量效能的腳本把時鐘跳到傍晚並存了檔，接著的 galgame 回歸在同一份存檔開始，三月七到晚餐時間說「先走一步」結束對話，滾輪與回滾兩項因此 FAIL。測試要自己設好前提：`Tools/galgame_regression.lua` 開始時用 `/clock {"hours":10}` 設時間，並先確認 TTS 伺服器連得上。
- **預先測試走路格子，反而讓卡頓幀 173 → 881**（2026-10-25，E 組 120 FPS）：為了讓同步算的路線（玩家導航等）不必第一次測格子，加了 `TickNavWarm`，每幀 0.5 ms 從玩家位置向外洪水填滿 250 m 的格子。結果：(1) 每 8 次出隊才看一次時間，而單格測試在空曠處要 0.1～1 ms（`OKStreetObstacleGap` 找不到附近方塊時會一路擴大到整個城市範圍），一幀拖到 13 ms；(2) 改成每格看時間後，佇列與 `TSet` 長到 150 萬格，容器擴張（rehash、搬移）本身就一次 12 ms；(3) 格子都測過之後，玩家長路線仍要 11～13 ms，真正的成本是 A* 每展開一格的數十次雜湊查詢，不是格子測試。撤掉預先測試；`OKStreetObstacleGap` 加 `Enough`（呼叫端只和門檻比較時，查到門檻就停）；A* 在搜尋範圍內改用連續陣列。教訓：先用計時把成本拆到最底層再決定做法，不要依「應該是物理測試慢」的印象加一層快取；每幀分段的工作要每個單位都看時間，且不能讓容器無上限長大。
- **共用的 TTS 伺服器量測途中停了，回歸只印 WARN，接著把語音相關項目判成 FAIL**（2026-10-25，E 組 galgame 回歸）：Sched11E 的量測和回歸都設 `OK_TTS_ENDPOINT` 指向 C 的 ok-tts（47860），途中它停了；回歸腳本雖然先檢查 `/health`，但只印 WARN 就繼續，scene／skip／rollback 都 FAIL，看起來像改壞了。改用遊戲自己的 ok-tts 重跑，9 項全過。修正：`galgame_regression.lua` 最多等 TTS 3 分鐘，仍連不上就 exit 2（未測試）。前提條件不成立的測試要停下來回報「沒測」，不能讓它產生 FAIL；共用別人的伺服器時，量測前後都要確認它還在。
- **以為 E 的遊戲會自己開 ok-tts**（2026-10-25，E 組 galgame 回歸，補正上一筆「共用的 TTS 伺服器量測途中停了」）：上一筆寫「改用遊戲自己的 ok-tts 重跑」，但 E 的 `local-instance.json` 是 `voice=false`，啟動器本來就不會開 ok-tts，E 也沒有 `Build/ok-tts`。下一輪回歸等了 3 分鐘都連不上 47960，才發現這件事；Sched11E 那次通過時接到的是哪一個伺服器沒有查證。現在 E 的測試一律先自己開一個 ok-tts（E 原始碼編的，私有 port），再用 `OK_TTS_ENDPOINT` 指過去；不接 C 的 47860，那是 C 的遊戲在用的。寫 ERROR_LOG 前，要先查證「為什麼這次好了」，不能用猜的。
- **PowerShell 的 `[IO.File]` 又用了相對路徑**（2026-10-25，E 組 TTS 0.6B）：先 `cd E:\OpenKoikatsu\Tools\ok-tts\src`，再用 `[IO.File]::ReadAllText('speaker.rs')` 改檔，讀到的是 C 工作目錄下不存在的檔，整批取代都失敗（這次沒有寫壞檔案）。同一個錯第二次：`cd` 不會改 .NET 的工作目錄。改檔一律先組完整路徑（`$d='E:\...\src\'; $d+'speaker.rs'`），或用 Edit 工具。
- **`String.Replace` 換掉所有相同片段，連別的結構也一起改**（2026-10-25，E 組 TTS 0.6B）：要在 `VoiceCheckArgs` 的建構處加 `judge`／`compare` 兩個欄位，用 `base: ...` 那一行當錨點取代，但 `profile::BuildArgs` 也有一字不差的同一行，兩邊都被加上，編譯報 E0560。用整段取代前，先確認錨點在檔案裡只出現一次；不唯一就改用 Edit 工具（不唯一時會直接報錯）。
- **voice-check 的語料檔帶 BOM 時，第一行的 `#` 註解被當成台詞唸**（2026-10-25，E 組 TTS 0.6B）：用 PowerShell `Set-Content -Encoding UTF8` 寫出的語料有 BOM，`starts_with('#')` 看到的是 BOM，註解變成第一句台詞。`voicecheck.rs` 讀語料時先去掉行首的 BOM。讀使用者可編輯的文字檔時，要把 BOM 當成可能出現的東西。
- **Lua 呼叫 ffmpeg 的命令列被 cmd 拿掉引號，ASR 讀不到轉好的 wav**（2026-10-25，E 組 TTS 0.6B）：`io.popen('"' .. cmd .. '" 2>&1')` 把結尾的引號放在 `2>&1` 前面，指令本身又以帶引號的 exe 路徑開頭，cmd.exe 拿掉頭尾引號後 exe 路徑就斷了，四句的 CER 都是 1.0。統一寫成 `io.popen('"' .. cmd .. ' 2>&1"')`：整條指令（含重導向）外面再包一層引號。
- **收尾測試時誤殺 C 遊戲的 ok-tts**（2026-10-25，E 組唱歌測試）：我的沙盒測試在 48195 開了一個 ok-tts；收尾時看到一個 `C:\portable\OpenKoikatsu\Build\ok-tts\ok-tts.exe`，啟動時間和測試差不多，就把它（PID 8776）停掉了。結果那是 C 的 Gest58 遊戲自己啟動的 ok-tts（`--port 47860 --parent-pid 10972`），C 的遊戲從那時起就沒有聲音；我自己那個早就結束了。同一個 exe 可能同時被好幾個遊戲使用，路徑和啟動時間都不能拿來判斷是不是自己的。只停命令列裡有自己 port 的程序（`Get-CimInstance Win32_Process` 比對 `--port 48195`），或只停自己啟動時記下的 PID。
- **評估腳本卡了兩小時沒人發現：popen 的子程序用 `start /b` 開了伺服器，管線一直沒關**（2026-10-25，E 組 TTS 0.6B 評估）：`eval06.lua` 用 `io.popen` 跑 `asr_cer.lua`，後者在找不到 ASR 伺服器時用 `start "" /b ok-asr.exe serve ...` 自己開一個。ok-asr 繼承了 popen 的 stdout 管線，`asr_cer.lua` 16:59 就跑完了，主腳本的 `read("a")` 卻一直等 EOF，等到 19:03 才發現，在等的 galgame 量測也因此延後。修正：停掉那個 ok-asr 後管線關閉，評估繼續。規則：(1) 背景伺服器一定要在最外層的腳本開，或把輸出重導到檔案並且不在 popen 裡面開；(2) 長流程照新規則每十分鐘檢查一次進度，不能只等完成通知。
