# Stage307：皮膚 HSV、mod DXT5 與衣服混色

## 修正內容

1. **RGB→HSV 分支**：`KKPreviewActor.cpp` 共用 `UnitySkinHueCode()`，對齊 `Koikano_main_skin/0072.asm.txt` 兩輪 channel permutation。R 不是最大通道時仍保留 R，chroma 保持 max(R,G,B)-min(R,G,B)。不依賴角色名、膚色、髮色或固定比例；輪廓只共用顏色轉換，未修改下巴輪廓幾何。
2. **宣告的 inline texture byte array**：解析器回傳非空且夠大的資料仍可能包含標頭。依 raw serialized byte array 的宣告長度、起點與尾端 bounds 恢復完整 mip chain，同時處理過短及過長 payload。標頭搜尋從 UTF-8 name 的實際 byte length 後開始，支援長名稱；streamed texture 無 inline array 時保留其已載入的 stream。
3. **衣服 create 的取樣與色彩空間**：讀取指定來源 Texture2D 的 FilterMode／WrapU/V／m_ColorSpace；texel-center 取樣支援 Point／Bilinear 與 Repeat／Clamp／Mirror／MirrorOnce。sRGB texture 先解碼再插值，衣服顏色在線性空間混合，輸出 sRGB round。部分 override 沒有指定 texture 時，繼續查下一來源。配件直接 shader 的既有色彩 convention 分開保留。
4. **快取**：CACHE95／SESSION158，使先前已快取的錯誤貼圖與 Session 重新產生。沒有角色專用材質參數或直接用原生截圖貼到模型。

## 獨立來源證據

### 皮膚

- 原生參考：`Saved/Tests/NewCards306BodyMaskNative/Card0.json.renderer50.mat0._MainTex.png`，4096²，是遊戲實際 draw MainTex。
- `Tools/GamePoseProbe/verify_skin_hsv.py` 讀取 production HLSL，解釋 DXBC 中實際 ge/and/mov/mad/min/div 指令；Python colorsys 另核對所有通道順序。
- 17³網格、100,000隨機色、200,000原生貼圖取樣，共304,913組。舊码負控制 chroma max差1.9723515968／hue max差0.3291838；修正後 chroma差0／hue差3.43e-7（反組譯的常數印出六位小數）。
- 原生貼圖 RGB(241,243,243) 的 chroma 正確0.007843，舊碼0.952941；RGB(240,239,241) 正確0.007843，舊碼1.945098。這放大了微小白色高光色差。
- HSV-only 中間版 `SkinHsv307Final` 十卡×兩pose×三視角60 PNG、两程序normal exit0，保留於 DebugShots，不能代替完成 DXT5 修正後的末輪回歸。
- 同構圖手臂固定crop cyan判定 G-R>8 且 B-R>8：左2975→0、右5128→0。`SkinCyanRhi.json` 只量測局部色異常，不表示完整 shader 等價。`SkinComparison.png` 已目視。

### mod 靴子

- 原生 fixture：`Saved/Tests/SkinHsv307BootNative`，Studio PID62316、61 renderers，delayed quit由runner停止，exit=-1。不能記成normal exit0；Probe已移回Tools。
- mesh `o_shoes_bootsbelt`／draw `cf_m_shoes_sister`／Shader Forge/main_opaque；MainTex1024×2048、ST identity。
- 來源 zip `[[OMEGA]] HS2 port to KK v1.8.1.zipmod` 的 `chara/HS2-KK.unity3d`；list112／15222／Sister [boots]，MainTex `cf_shoes_sister_t`、ColorMask `cf_shoes_sister_mc`。
- DXT5 main實際mip chain2,796,240bytes，converter回傳2,796,272bytes，開頭仍是32bytes標頭；舊的「至少有top mip大小」檢查錯誤地接受它。原Session幾乎RGB0、平均alpha75.93；native正常gray靴面／red鞋底、平均alpha254.28。不能將這個問題歸咎shader specular或用材質blur掩蓋。
- UnityPy直接由同一bundle解出 `.dxT5` 與 `.UnityPy.png`，供Rust independently decoded comparison。修正後main2,097,152像素＋mask524,288像素，原compressed bytes exact、decoded RGBA max0。
- 負控制 `SkinHsv307Dxt5Before.log` FAILED 保留；修正後 `SkinHsv307Dxt5After.log` PASS。合成長UTF-8 name／oversized header／沒有inline array控制 `SkinHsv307PayloadRules.log` PASS。

### 衣服 create 的獨立像素控制

| 對照 | 最大 RGBA 差 | 平均差 | 結果 |
|---|---:|---:|---|
| 正確 DXT5＋舊 nearest／gamma 混色 | 142 | 1.0693 | FAIL，不能以解碼 PASS 代替混色 |
| Bilinear、仍用 gamma 混色 | 88 | 0.4933 | FAIL |
| 來源 sampler＋linear 混色、可攜 CPU PNG | 6 | 0.0739596 | strict max3 FAIL，6 pixels >3 |
| 原生 GPU float inputs＋獨立 DXBC 公式 | 1 | 0.0571486 | PASS，2,097,152 pixels |

- 原生真正 shader 為 `Koikano/create_topN`，公式對照 `Saved/ShaderDxbc/mm_base/Koikano_create_topN/0001.asm.txt` 最後的 RGB mask cascade 與保留 MainTex alpha。這份 fixture 沒有 pattern texture；有花紋的完整 shader 分支尚未用此 verifier 認證。
- `Tools/GamePoseProbe/verify_clothes_create.py` 讀取遊戲混色當下的 `_Color/2/3`、`_MainTex`／`_ColorMask` ARGBFloat GPU 取樣，不執行 Rust production bake，獨立重建公式。RGB 每通道 max1、alpha max0、coverage mismatch0、pixels >3 為0。輸入／ASM／native output hashes 保存於 `SkinHsv307BootGpuHook/ClothesIndependentPixels.json`。
- 首次嘗試在載入後查 `ctCreateClothes[8,0]` 沒有取得資料：`ChaControl.cs` 會 Release 並置 null。`SkinHsv307BootGpuInputs` 只有 renderer snapshot，**不是有效 GPU clothes 證據**。改以 Harmony 在 `CustomTextureCreate.RebuildTextureAndSetMaterial` 的 postfix 擷取，保留同名來源最後一次混色，runner 必須確認 requested float files 存在。
- Hook fixture Studio PID77816、61 renderers 完成，delayed quit 由 runner 停止 exit=-1；Probe 移回 Tools。擷取只讀輸入，不改角色混色常數或 production 材質。
- CPU strict max3 gate 未放寬。可攜 CPU alpha 6／RGB [2,4,2] 的差異在實際 GPU 取樣輸入已存在；使用該輸入的公式只差1。這定位到輸入取樣／解碼邊界，沒有以硬體 lookup 或每張卡 alpha 補丁抹掉差異；實際 runtime source bytes／全部 GPU decoder 細節尚未完整認證。
- 原compressed BC3區塊進一步控制：native top-down (x642,y33) 的原區塊 endpoints=(13,237)、alpha code4，標準5分段插值147.4、CPU decoded147，GPU sampled153。對fixture內 endpoint0<endpoint1 的四種interpolated codes（共27像素），目前GPU觀察值與3/16、6/16、10/16、13/16的量化權重最多差1，而標準分段值最多差5.6。這是**本機device的診斷觀察**，不是全顯卡認證，也沒有將權重表放到production；若要像素一致，需保留來源compressed GPU resource／GPU create pass，而非修改標準CPU decoder。

## 末輪驗證

- 正式 Unreal build：`SkinHsv307Final158UnrealBuild.log`，Succeeded30.31秒。
- 首次十卡 `SkinHsv307Final158` 觀察冷快取每卡時間後，預估 batch 超出原本固定十分鐘預算；確認 PID62340／project／log 身分後計畫停止。partial 圖／log／identity 保留，不計入正式 pass。runner 改為 max(10, 3+2×card_count) 分鐘，載入錯誤／正常退出／截圖數量 gate 不變。
- 同一正式158 binary完成 `SkinHsv307Final158R2`：十卡outfit0 × Tpose／Studio walk row r00000022、scene4、time0.4 × 全身／正臉／側臉，共60PNG900×1080。Tpose PID49416／Walk PID73044 normal exit0；每pose10PV audits／204attachment audits，attachment max0cm，palette max2.252e-6／2.262e-6cm。
- `report_dump_cards.py` 完整PASS；ContactSheet／SideContactSheet與Hestia、Junko、March、Alice完整圖已目視。Hestia衣領／肩飾仍white、胸前star開口有skin、手臂青點0；Junko靴子大面積黑白破圖消失；March眉毛／Alice側髮維持。KK091／KK384283沒有來源JPG，圖板明確標 Previous Unreal T-pose，不當遊戲來源認證。
- 原Saved/Screenshots根15張Stage286 SHA逐張保持。owned UE／Studio全部退出，GamePoseProbe不留在game plugins。Source／binary／decomp／logs／PNG／文件hash與各gate範圍見 `Saved/Logs/SkinHsv307FinalAudit158.json`，Stage306報告維持歷史hash。
- 最新局部cyan量測 `SkinCyanRhiFinal158.json`：左臂2975→0、右臂5128→0，脖子crop原本0→0。兩張末輪比較圖為 `SkinComparisonFinal158.png`／`BootComparisonFinal158.png`；不將細顆粒或全部肌膚差異混稱已修。

## 範圍與後續

- HSV 段、貼圖解碼、clothes create albedo、RHI畫面與骨架audits分別記錄；任何單一PASS都不表示完整 ForwardBase fragment pixel parity。
- 皮膚細顆粒／腮紅、hair gloss、完整衣服／玻璃shader、深度與stencil仍需各自原生input與fragment證據。
- Junko與Game JPG仍有白袖口／上衣白線及髮高光差異，來源衣服／pattern／overlay與完整fragment須另驗；靴子toe附近白線亦未宣稱逐像素一致。60圖與來源貼圖PASS不等於全部畫面完成。
- `CustomTextureCreate.Initialize` 的來源 RenderTexture 設 useMipMap=true；目前 UE PNG transient 只有 top mip。source MIP／LOD／sampler 傳遞尚待，不能用全體貼圖 blur 或 blanket MIP 替代不同 texture settings。pattern 分支的完整取樣亦尚未對齊。
- KK091胸前髮束屈折是既有Stage298問題；下巴輪廓依使用者要求暫不處理。
- Stage306歷史末輪報告保留，不重新改寫其當時binary哈希。

## 重現

```powershell
python -X utf8 C:/portable/OpenKoikatsu/Tools/GamePoseProbe/verify_skin_hsv.py
cargo test --release --manifest-path C:/portable/OpenKoikatsu/Tools/ok-extract/Cargo.toml --lib native_mod_dxt5_texture_payload -- --ignored --nocapture
cargo test --release --manifest-path C:/portable/OpenKoikatsu/Tools/ok-extract/Cargo.toml --lib native_mod_boot_clothes_albedo -- --ignored --nocapture
cargo test --release --manifest-path C:/portable/OpenKoikatsu/Tools/ok-extract/Cargo.toml --lib oversized_texture_payload_uses_declared_array_after_long_name -- --nocapture
python -X utf8 C:/portable/OpenKoikatsu/Tools/GamePoseProbe/verify_clothes_create.py C:/portable/OpenKoikatsu/Saved/Tests/SkinHsv307BootGpuHook --name cf_shoes_sister_t --renderer 58 --source-dxt5 C:/portable/OpenKoikatsu/Saved/Tests/SkinHsv307/cf_shoes_sister_t.dxT5
```

fixtures皆在Saved，不要求重新安裝遊戲插件；若重抓native，使用 `run_renderer_native.ps1`，保留其Sources.json與退出理由。錯誤紀錄：root ERROR_LOG E-163／E-164。
