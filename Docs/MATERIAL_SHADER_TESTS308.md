# Stage308：執行期主貼圖 MIP 與來源設定

## 已實作的通用流程

- `ExtractedMesh.runtime_textures` 以 shader property 保留貼圖建立資訊；只有成功執行 create／MaterialEditor 替換時寫入，未按卡名、臉型、角色、shader role 或貼圖顏色判斷。
- `CustomTextureCreate` 的衣服第一材質、身體、臉、眼白、虹膜 create，以及 MaterialEditor 主貼圖替換，標記 `_MainTex` 為 runtime RT。衣服其他 submesh、失敗的 create、普通靜態髮絲 PNG 不因角色名稱而變成 RT。
- Session／decoded cache 保留設定；正式 CACHE97／SESSION161。新增 `ok_part_texture_settings`，既有 `OkMeshView` 與 function-pointer table ABI 沒變。
- Unreal PNG loader 使用設定建立完整 `FTexturePlatformData.Mips`，保持 sRGB storage、Bilinear 與實際來源 wrap；靜態／未知來源走原本載入流程。建立公式在 `KKTextureRuntime.cpp`，無角色專用參數。
- 縮圖按 normalized UV texel centers 作 encoded-channel bilinear minification。方形 POT 是 2×2 平均；奇數尺寸、矩形、1×N／N×1 亦處理至 1×1，alpha 與 RGB 各自取樣，沒有 premultiplied alpha 或色彩模糊補丁。

## 遊戲來源

在 `C:/portable/KoikatuSunshine/kks_decomp/Managed/CharaStudio_Data/Assembly-CSharp`：

- `CustomTextureCreate.Initialize` 建立 RenderTexture，`useMipMap=true`。
- `RebuildTextureAndSetMaterial` 的 blit 使用 `GL.sRGBWrite=true`。
- `CustomTextureControl.SetNewCreateTexture` 將 createTex 指派給 draw material `_MainTex`。

在反編譯 plugins：

- `KK_Plugins/TextureContainer.cs` 使用含 MIP 的 Texture2D，再 blit 到 `useMipMap=true` RenderTexture。
- `MaterialEditorPlugin.ConvertNormalMap` 另建立 autoGenerateMips RT，並保留原 wrapMode；本輪**沒有**把目前線性 normal PNG 改成 sRGB。它與主貼圖的表示法不同，另列待辦。
- **`MaterialEditorAPI.MaterialAPI.SetTexture`**：替換值複製原 texture.wrapMode，原值 null 才用 Repeat。由材質 texture PPtr 解析 wrap；已被 create RT 取代的目標優先用 runtime 設定。不能把 TextureContainer 的 RT 預設 Clamp 套到最後的 ME draw texture。

### 159 首輪視覺退步與修正

`TextureMips308Final159` Hestia 嘴內變綠，原生 renderer43 `o_tang` 為 RT／Repeat；159錯誤使用Clamp。這個問題在 NullRHI MIP kernel測試看不出來，需來源 sampler＋RHI 目視。依上述 SetTexture 通解保留來源 wrap，沒有舌頭／角色名特例。Face／Eye create RT 的設定亦在 ME 替換前保留。

確認PID48144／exe／project／log後停止首輪，partial圖與 `TextureMips308Final159PlannedStop.json` 留存；runner拒絕exit=-1，這輪不算完成回歸。CACHE97／SESSION160清掉錯誤來源cache。

修正後首卡已目視確認嘴內恢復粉色。`verify_texture_mips_visual.py` 固定同構圖嘴部 crop：158綠像素0、159錯誤Clamp為932、160正確source wrap為0。這個卡片名只存在於診斷 fixture，production 不用它決定載入。比較圖 `Saved/Tests/TextureMips308/MouthSamplerComparison160.png`。

### 清單來源的替換順序

160再以原生 sampler 比較捕捉到眉毛／眼線兩項 Repeat≠Clamp。它們不是使用 prefab texture：ChaControl先從角色清單 ChangeTexture，再ME替換。source spec／GUID／head suffix／實際bundle或Sideloader PNG lookup共用原本載入路徑，取被替換的**清單來源**wrap。眼睛已走create RT，仍用Clamp。Sideloader.LoadTextureFromPNG的path含`clamp`要Clamp，含`repeat`或default要Repeat。

160 PID76424的partial圖、`TextureMips308Final160PlannedStop.json`與失敗 sampler JSON保留，runner拒絕exit=-1，不列正式回歸。SESSION161重新產生，parsed CACHE97可保留。`runtime_texture_catalog_wrap_matches_native`用原生眉毛／眼線／eye設定檢查實際catalog來源，四個Rust控制PASS。

## 原生設定與獨立 MIP 證據

`Saved/Tests/TextureMips308NativeR6`：KK_346095，原生 Studio PID59484，68 renderer rows。完成資料後 Studio 延遲退出，由 runner 驗證 owned exe 後停止：Studio exit=-1，runner exit0。Probe finally 移回 Tools。此處不宣稱 Studio normal exit0。

| 原生貼圖 | 尺寸 | MIP 層 | Filter | Wrap U/V | RT sRGB |
|---|---|---:|---|---|---|
| renderer50 body MainTex | 4096² | 13 | Bilinear | Clamp／Clamp | True |
| renderer27 face MainTex | 4096² | 13 | Bilinear | Clamp／Clamp | True |
| renderer50 NormalMap | 1024² | 11 | Bilinear | Repeat／Repeat | True |

第三項是 MIP kernel 的獨立控制，**不是** normal production path 已修好的證據。Body 的 overtex／LineMask／AlphaMask 又有不同 colorSpace 與 mip count，不能把這張表套到所有 texture slots。

### 測試工具自己的讀回錯誤

初版把 MIP count 用 max dimension 推算，NPOT 控制有空層；顯式 `GenerateMips()` 仍失敗。直接 `ID3D11Texture2D::GetDesc` 確認 GPU 確實有那些 subresources。最後使用 staging Texture2D＋CopySubresourceRegion＋Map，獨立繞過 Unity CopyTexture／ReadPixels 控制：

- 5×3 的 mip2、1×7／7×1 的 mip1/2：Unity 控制讀到全零，direct GPU 讀回有實際資料。
- typeless resource format27 不是錯誤；staging 使用相容的 UNORM format28。第一個只接受 typed formats 的 R5 失敗，log／error completion 保留。
- 所有 COM slots 對照本機 Windows SDK `d3d11.h` vtable；GetDevice／GetImmediateContext 取得的引用與 staging 都在 finally Release。
- `.direct.mipN.rgba8` 為 D3D GPU row order；普通 `.mipN.rgba8` 為 Unity ReadPixels row order。控制驗證選 direct 檔，不混用兩種方向。垂直翻轉不影響對稱縮圖 kernel，但不應拿兩檔直接逐列比對。
- R2／R3／R4 的 NPOT max249 FAIL 保留；它是讀回工具問題，未加進 production 演算法或放寬 kernel gate。

### 數值

`verify_texture_mips.py`／`MipKernelAllLevels.json`：3張實際 RT＋5個尺寸控制（5×3、1×7、7×1、4×4、8×4），共8組、45次縮圖：

| 比較 | 最大差 | 說明 |
|---|---:|---|
| 原生每一層作輸入，encoded-channel 重建下一層 | 1 | 所有45步 |
| 從原生 top mip 連續重建整條 chain | 3 | UNORM rounding 累積；不宣稱 byte identity |
| 先 sRGB decode，再 linear average，再 encode | 73 | 明確負控制；不是這個遊戲／device 的 GenerateMips 行為 |

此結果是 native MIP kernel 證據；原始 albedo CPU create／GPU sampler 精度的既有差異仍存在。沒有把本機 BC3 或 sRGB precision 做成 production lookup table。

## 正式程式驗證

- Rust `runtime_texture_tracks_create_assignment_not_role`：實際 clothes create 成功的第一 submesh 有設定；其他 submesh 保留原圖／靜態來源；create 缺圖不寫 RT 設定。
- Rust `runtime_texture_provenance_survives_both_caches`：decoded mesh 與 Session cache roundtrip 均保留設定；普通 body role 不會自行生成設定。
- `TextureMips308RustFinal.log`：2 PASS。
- `runtime_texture_material_editor_copies_replaced_wrap`：null→Repeat、原texture→Mirror、create RT覆寫原asset後→Clamp；`TextureMips308RustFinal160.log`：3 PASS。
- Unreal build `TextureMips308UnrealBuild159.log`：Succeeded，37.44秒。
- 正式160 build `TextureMips308UnrealBuild160.log`：Succeeded，28.24秒。編譯前確認上一輪 UE 已退出；沒有 hot rebuild。
- 正式161 build `TextureMips308UnrealBuild161.log`：Succeeded，28.15秒；`TextureMips308RustCatalog161.log`：4 PASS，包含原生catalog來源控制。
- `OpenKoikatsu.Material.RuntimeTextureMips` 使用**production C++ helper**比對原生每層與整條 chain，並檢查 transient platform mips／完整 byte data／Clamp／Bilinear／sRGB。
- `TextureMips308Automation159.log`：8 controls、45 steps、kernel max1、recursive max3；UE PID27220，normal exit0。NullRHI 驗證 CPU／platform upload 資料，不當作 GPU fragment parity。
- 正式160再次使用相同 native controls 驗證 production helper：`TextureMips308Automation160.log`，PID48448、normal exit0，8／45／max1／recursive3保持。
- 正式161 automation `TextureMips308Automation161.log`：PID44248、normal exit0，同樣8／45／max1／recursive3。
- `verify_runtime_sampler.py`：第一張原生卡21個有資料的MainTex，MIP count／filter／wrap／sRGB與UE一致。`cf_O_eyeline_kage`沒有native MainTex資料，保留unknown，不列為一致。160兩個實際mismatch已修正。
- runner 要求指定 sampling TSV、所有宣告的 MIP 檔與 direct GPU controls；修掉漏 `.json` 的 glob，不跳過失敗 gate。

## 十卡 RHI 回歸

正式標籤 `TextureMips308Final161`，T-pose／Studio Walk 0.4s，每卡全身／正臉／側臉，共60張900×1080。159／160另留作失敗對照。

卡片：KK_346095、江之島盾子2、KK_384915、KK_384917、KK_388205、三月七、Alice、Yuuka、KK_091207、KK_384283。

正式回歸已完成：

| Pose | UE PID | Exit | 圖片 | PV audits | 配件偏差最大 | Palette偏差最大 |
|---|---:|---:|---:|---:|---:|---:|
| T-pose | 23012 | 0 | 30 | 10 | 0 cm | 2.252e-6 cm |
| Walk 0.4s | 45988 | 0 | 30 | 10 | 0 cm | 2.262e-6 cm |

每pose 204個配件audit，全部PNG900×1080。两pose的native sampler對照皆21個已知／0 mismatch／1 unknown。原根15張 Stage286 SHA保持。最後owned UE／Studio0，installed Probe0；`TextureMips308Cleanup.json`。

ContactSheet／SideContactSheet、Hestia完整圖、Junko／384917／March／Alice正臉、384283 Walk臉與091207 Walk全身均已目視。眉毛可見，白領／肩飾維持，MIP／sampler沒有新增可見的臉蓋髮／髮飾脫離；091207胸前髮束仍是既有問題。完整shader／膚色细節、Junko白袖口／衣線／髮高光與JPG仍有差異，`visual_parity_passed=false`，不以骨架audit代替像素驗收。

`MouthSamplerComparison161.png` 與 `MouthSamplerRhi161.json`保留正式嘴內對照；同crop綠像素0／932／0。`TextureMips308FinalAudit161.json`保存來源／code／binary／log／raw mip／文件hash；Stage306／307歷史報告沒有重寫。新圖只寫獨立 DebugShots tag。

## 仍待接通

1. NormalMap／NormalMapDetail 的 source colorSpace、MaterialEditor converter、SRGB RT storage 與目前 UE linear normal representation。
   原生擷取 property 清單目前有 `_DetailNormalMap`，但 main_skin 實際使用 `_NormalMapDetail`；下輪須補正確 property，不能把未擷取當成沒有資料。
2. 靜態 Texture2D 的 authored MIP、Filter／wrap／LOD／aniso。不能用 runtime encoded MIP generator 取代所有 authored chain。
3. 完整 pattern 分支，hair／clothes／glass ForwardBase 與 stencil/depth；皮膚細節／腮紅。
4. 既有 KK091 胸前髮束問題。下巴輪廓幾何依使用者要求延期。

十卡載入／骨架 audits 通過不代表全部 mod、動畫、衣服狀態或 shader 已與遊戲逐像素一致。
