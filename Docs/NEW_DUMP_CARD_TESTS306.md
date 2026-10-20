# Stage306 — 新增角色與通用載入修正

日期：2026-09-29。最後 production：decoded cache **94**、session pipeline **157**。
原則：依遊戲 prefab／renderer／shader／plugin 元件與卡片資料決定載入；沒有 production 卡名分支。
下巴輪廓線仍依使用者指示延期。

## 本輪卡片與來源

| 卡片 | 來源證據 |
| --- | --- |
| KK_384915.png | `mesh_dump/KK_384915.png/飴宮 怠美_0`：87 OBJ、mesh JSON、材質、貼圖及全身 JPG。 |
| KK_384917.png | `mesh_dump/KK_384917.png/凶鳥 狂死華_0`：68 OBJ、mesh JSON、材質、貼圖及全身 JPG。 |
| KK_388205.png、KK_346095.png、弹丸论破 - 江之岛盾子2.png | Stage305 的三張新增卡，各有遊戲 JPG；本輪另從實際含 mods 的遊戲取得骨架、renderer／材質及像素 fixtures。 |
| March、Alice、Yuuka、KK_091207、KK_384283 | 原五張回歸卡。前3張有遊戲全身 JPG；後2張無 JPG 時比較圖明確標示 Previous Unreal T-pose。 |

## 已實作的通用修正

1. **Animator generic path binding**：補上目前 Animator rig 的完整相對路徑 CRC32，解決 Avatar TOS 沒有列出的 PV helpers；保留遊戲 PV eligibility 與 CopyPosRotScl。完整路徑 collision／duplicate 會拒絕，不能任選同名葉節點。三張 Stage305 卡的走路前臂拉長因此修正。
2. **Accessory id=0**：遊戲空槽由 category/type120 判斷；其它 category 的 id0 有效。Junko cat123/id0 眼鏡現在可載入。
3. **AccessoryClothes.AssignWeights**：按已安裝 KKS_AccessoryClothes 與來源 ChaAccessoryClothes 元件指定的 rendNormal，精確骨名重新指定 shared body Transform，保留原 bind matrices／SMR slot 順序；被銷毀 ArmatureRoot 的未匹配骨為 identity。不能把所有配件一律綁身體。
4. **衣服 authored palettes**：即使無 private dynamics，仍保留原 SMR 全部槽位，按 body Transform 精確名稱重綁。body SMR palette 不包含全部 helper Transform；合併到 ancestor 會丟失 bind 位移。384915 裙子148 slots不再縮為101。
5. **Prefab 根身分**：依 AssetBundle.m_Container 本地 asset PPtr 取得 MainData 的真正根 GameObject，再供幾何與骨架圖使用。根與子 renderer 同名時，名稱搜尋會選錯子節點並漏讀 N_move。384915 tiny Sphere／Cube 的卡片縮放因此恢復；不合成 N_move。
6. **Renderer 型別與 rest buffers**：讀 MeshRenderer／SkinnedMeshRenderer 元件型別，不用「有 skeleton」推測蒙皮。rigid 放置完成後保存 world-space rest positions／normals，避免 Session.skin_all 第一幀撤銷 prefab／card placement。
7. **MaterialEditor property 名稱**：_Color／_Color2 等查詢先去前導底線，再按 coordinate／slot／material identity 精確匹配；來源儲存名為 color，原來 _color 查詢永遠失敗。
8. **RGB24 payload 恢復**：固定 bytes-per-pixel 格式檢查 inline payload 宣告長度，保留完整 mip chain。Junko 1024²熊貼圖原來只讀257 bytes，應3145728 bytes，解碼為None；現在正常解碼。沒有為熊飾另外畫貼圖。
9. **create_eye 與 KSOX composite**：依實際 DXBC 對齊 _Color／_Color2、vivid-light／gradation、linear輸入與sRGB RT、AlphaToMask；左右瞳孔分別解析。KSOX PS alpha=max(base,over)，再經 SrcAlpha 硬體混合，RGB權重為 over.a*maxAlpha。共同 face/body/eye 合成公式修正。
10. **空鼻部 detail list row**：合法的空 asset row 保留空資料／透明覆蓋，不 fallback 到灰色鼻圖；388205 的灰色鼻部矩形消失。
11. **實際 main_item pass**：明確 shader kind8，Forward Cull Off、normal A*R/G、SV_IsFrontFace幾何Z符號與 camera _WorldSpaceLightPos0。不是依飾品名稱開雙面。此項單獨沒有修正額飾黑色，失敗截圖保留；154正規化靜態貼圖列後，RHI金屬圖案已恢复。
12. **Null MainTex 的鏡片**：Koikano/main_clothes_item_glasses／Forge toon_glasses_lod0 的宣告 default white 與 _Color4 RGBA保留；依 Forward SrcAlpha/OneMinusSrcAlpha透明。原 tint_png只寫RGB，不能保留12.5% alpha。完整 GlassRamp／Fresnel尚未逐像素等價。
13. **Accessory 貼圖列方向**：靜態 Unity Texture2D 及其色彩烘焙保留 bottom-up，ME PNG已是top-down。emit boundary按來源正規化 albedo與相關numeric textures，配合 UE 的 (u,1-v)；ME不再翻行、normal_convert已翻行者也不再翻一次。額飾UV本身121點與dump完全一致，錯的是取樣列方向。
14. **ColorMask 混用來源**：烘焙時以MainTex方向為準，只要MainTex與ColorMask來源方向不同就先對齊遮罩，兩者同來源不翻。補齊「static MainTex＋ME ColorMask」的反向組合；四種來源組合使用獨立兩色UV控制驗證，154反例FAIL與155結果分開保存。

15. **Sideloader 逐 asset 來源**：`BundleManager.TryGetObjectFromName` 先 `Contains(name)`，patch bundle不包含MainData時使用vanilla。同一個prefab的geometry／ChaAccessoryComponent／assignment讀同一來源，禁止跨prefab fallback；支援m_Container bare asset名字與本地GameObject PPtr。prefab SQL與memo key也包含實際來源zip/file stamp，cache94／pipeline156失效舊錯誤。這修正了KK346095衣領及肩膀由white變cyan的根因，不設卡名／固定white特例。

16. **KCOX身體／內衣／內衫遮罩**：解析`KCOX.Overlays` coordinate→mask→ClothesTexData[PNG,Override]，BodyMask/BraMask/InnerMask按Material.SetTexture hook替換list mask；只讀目前coordinate，PNG=null保留原遮罩，Override不影響mask替換。卡PNG方向轉成此API使用的Unity raw rows，在boundary正規化一次。原生KK346095胸前星狀MainTex確實透明，但body必須由KCOX自訂BodyMask顯示，這是另一个根因，不能把gray背景當衣服染色。此項只接通三mask，沒有宣稱所有clothing albedo overlay完成。

### KK346095衣服錯色的原生控制

- 三個配件slot24/25/26，vanilla `p_acs_head_mendako`、renderer4048779298634606239，原生材質`acs_m_mendako`、Shader Forge/main_item；三組卡片顏色及遊戲_Color/_Color2/_Color3皆white。
- Accessory Hair Vanilla Fix只含同bundle的部分髮飾，不含mendako；舊hint拿到其它prefab renderer16而geometry是vanilla，role0跳过ChangeAccessoryColor。ME MainTex本來有命中；不是缺白圖，也不是卡片配色不同。
- `NewCards306CollarNative`原生material TSV與_MainTex PNG保存。三個遊戲MainTex RGB逐像素white；`native_partial_override_accessory_colors`同維度共1,572,864像素RGB全部white，renderer-group normal，對實際存在的p_acs_ahoge01則仍使用patch。
- KCOX自訂BodyMask原生`NewCards306BodyMaskNative/Card0.json.renderer50.mat0._AlphaMask.png`與正式Session mask2048²全部4,194,304像素RGB max0、coverage mismatch0。`NewCards306KcoxNative157.log` PASS；獨立coordinate／Override=false／null-mask規則控制`NewCards306KcoxRules157.log` PASS。相同算法依資料作用於其它卡，沒有星形貼補。
- 155失敗診斷：`NewCards306CollarSource155Hint.log`、`...155Ids.log`、`...155Resolved.log`；156 PASS：`NewCards306PartialOverride156.log`。原生runner完成fixtures後延遲退出，由owned PID45216停止、exit=-1，finally移除Probe；這不是UE final capture exit0。

## 來源驗證與證據範圍

| 檢查 | 結果 | 證據限制 |
| --- | --- | --- |
| 原五卡原生 PV slots 27frames×4 | 540 TRS，max1.7881393e-6 | 強制覆蓋來源helpers，不用同一binding parser挑驗收欄位。 |
| Studio LoadGraph | 135frames／36450 attrs，max1.1920929e-7 | 動畫曲線／來源骨TRS，非全畫面像素。 |
| 384915 五個 rigid dump | Sphere3204＋四Cube各143頂點，max2.4028122e-7m | 真實使用者mesh dump全頂點；首幀 positions==rest_positions。 |
| KCOX body mask | 原生2048²，4,194,304像素RGB max0／coverage mismatch0 | 材質輸入與可見區控制；不代表skin ForwardBase片段完全等價。 |
| 384915 skirt | 原生148 slots／979頂點，max3.5778305e-7m | 以原生body TRS的獨立ShaderWorld LBS控制；不是GPU vertex-buffer readback。 |
| Junko claws | 雙手各952頂點，max5.992538e-7m，RMS3.33496e-7m | raw／bind一致，body matrices max3.576e-7；不用錯誤BakeMesh×rendererScale控制。 |
| 384917額飾UV | 121點，max0 | 原生mesh JSON；位置／UV與像素證据分開。 |
| Null-MainTex glasses | native Color4=(0,.163623989,.19607836,.1254902)→Session PNG [0,42,50,32]，translucent | 原生材質alpha輸入保存；非完整glasses frag驗收。 |
| create_eye算法 | 實測GPU浮點輸入＋獨立公式，三卡雙眼全像素max1 | **可攜CPU輸入重建：384915 max5，原max3門檻仍FAIL；384917 max1、Junko max2**。不宣稱CPU完全等價。 |

384915眼睛max5的來源為GPU sRGB sampler精度：encoded250讀到.953125，理想CPU解碼.95597335；sampler最大差.00374875，被vivid-light放大。相同輸入配合其它眼睛顏色，誤差幅度不同；不能將384915的max5概括成兩張新卡都max5。
`verify_eye_create.py`使用原生ARGBFloat輸入分離算法與sampler誤差。不得硬編碼硬體取樣表或降低門檻。

原生fixtures：`Saved/Tests/NewCards306SourceWorld`；卡0=384915、卡1=384917、卡2=Junko。
來源遊戲延遲退出由runner確認exe／PID後停止，exit=-1；不可寫成原生normal exit0。
Probe由runner finally移回Tools，沒有留在遊戲plugins。

## Unreal 最終回歸（已完成）

- 同一最後正式binary：`NewCards306Final157` 五張新增卡；`NewCards306Regression157` 原五卡。
- 每卡outfit0、T-pose／Studio walk .4s各全身／正臉／側臉，已完成共60張900×1080 PNG。
- 四個capture程序正常exit0；每batch Tpose與Walk各15PNG、5個PV audits，attachment max0cm、palette max2.262e-6cm。兩份Audit都確認原15張SHA保持；native Probe已移回Tools，owned Unreal／Studio進程均已退出。
- 已目視兩batch共四張ContactSheet／SideContactSheet與關鍵完整臉圖：KK346095衣領／肩飾white及胸口skin恢復；384915 tiny飾品與裙子、384917金屬額飾、Junko眼鏡／熊飾、March眉毛／Alice側髮維持。Tpose／walk侧髮与飾品跟頭，不把大圖縮圖目視當逐像素shader證明。
- 衣服前後比較：`Saved/Tests/NewCards306ClothesColor157/Comparison.png`。最後四程序、60圖hash與source/code/binary/doc evidence：`Saved/Logs/NewCards306FinalAudit.json`。
- Walk：Studio row r00000022、walk_run／f_aruki_00_00_01、scene4；mouth0/open.8、blink open，取景固定同frame。
- 資料／截圖 gate與視覺檢查必須分開。`report_dump_cards.py`保留visual_parity_passed=false；因完整材質尚有殘差，不能以載入成功或buffer audit宣告全shader一致。
- 原Saved/Screenshots根15張Stage286的SHA逐張保持。新結果放各自Tag，不覆蓋原圖；重跑先歸檔old。

## 仍有差異與適用範圍

- KK346095的皮膚雜訊及腮紅仍偏強；Junko靴子仍有雜訊，頭髮高光也與遊戲照片不同。這些沒有以換圖、改角色比例或關閉材質來掩蓋。
- 384915的magenta襪子與紅色頸圈原本就存在於遊戲全身JPG，不能只憑縮圖把它們判成錯色。
- 完整skin／clothes／hair／glasses ForwardBase片段與depth／stencil尚未逐像素等價。已修的貼圖輸入、pass設定與palette，不等於全部shader已完成。
- 此次截圖涵蓋十卡outfit0與兩個固定pose；不能外推成所有mods、所有衣服狀態、所有動畫frame都已驗收。KK091胸前彎曲前髮是既有Stage298問題；下巴輪廓線維持延期。
- 原生逐頂點控制已在geometry與claws pipeline156重新PASS；之後153–155變更材質／列方向；156補齊逐asset來源解析並重新驗證geometry，157只加KCOX mask。最後157全部十卡RHI回歸與palette audits另列，不把來源控制冒充157的GPU vertex-buffer讀回。

## 已排除或保留的失敗

- Stage305手臂、鼻部、眼鏡／熊飾、遠處碎片失敗：原圖與log保留。
- NewCards306Final（pipeline146）：915裙子到頭上、巨大白色Sphere／Cube，917黑額飾。
- NewCards306Slots148、Regression148：前者已修裙子但tiny物件未修；後者只是148回歸，不能當最後157binary證據。
- NewCards306Final151：30張載入與audits成功，額飾仍黑／鏡片仍不透明。
- NewCards306Cull152、NoOutline152、Base153：Cull Off／關輪廓／跳ramp仍黑，沒有把假設當完整根因。Base153是診斷畫面；暫時BaseOnly程式與runner選項已移除。
- NewCards306Final154：30張正常exit0，額飾／頸圈圖案已恢復。Regression154在補齊反向ME ColorMask之前，確認owned PID66656與project／log後主動停止，exit=-1、partial截圖保留；不能算正常完成的最後回歸。
- NewCards306Final156：五新卡30張正常exit0，衣領／肩飾white，但胸前gray背景還在。來源證明漏KCOX BodyMask；Regression156計畫停止owned wrapper15164／UE59648，partial保留，不能算最後回歸。
- ShaderWorld之前的BakeMesh對照0.04444m是Probe重複乘renderer scale；weights={}是serializer漏properties；失敗fixtures都保留。

## 重現工具

```powershell
& C:/portable/OpenKoikatsu/Tools/GamePoseProbe/run_dump_cards.ps1 -CardNames @('KK_384915.png','KK_384917.png','KK_388205.png','KK_346095.png','弹丸论破 - 江之岛盾子2.png') -Tag NewCards306Final157
python -X utf8 C:/portable/OpenKoikatsu/Tools/GamePoseProbe/report_dump_cards.py --tag NewCards306Final157 KK_384915.png KK_384917.png KK_388205.png KK_346095.png '弹丸论破 - 江之岛盾子2.png'
cargo test --release --manifest-path C:/portable/OpenKoikatsu/Tools/ok-extract/Cargo.toml --lib native_partial_override_accessory_colors -- --ignored --nocapture
cargo test --release --manifest-path C:/portable/OpenKoikatsu/Tools/ok-extract/Cargo.toml --lib native_clothes_overlay_mask -- --ignored --nocapture
cargo test --release --manifest-path C:/portable/OpenKoikatsu/Tools/ok-extract/Cargo.toml --lib kcox_mask_coordinate_and_override_rules -- --nocapture
cargo test --release --manifest-path C:/portable/OpenKoikatsu/Tools/ok-extract/Cargo.toml --lib accessory_colormask_mixed_source_rows -- --nocapture
cargo test --release --manifest-path C:/portable/OpenKoikatsu/Tools/ok-extract/Cargo.toml --lib native_rigid_and_clothes_geometry -- --ignored --nocapture
cargo test --release --manifest-path C:/portable/OpenKoikatsu/Tools/ok-extract/Cargo.toml --lib native_assigned_accessory_vertices -- --ignored --nocapture
cargo test --release --manifest-path C:/portable/OpenKoikatsu/Tools/ok-extract/Cargo.toml --lib native_rigid_material_uv -- --ignored --nocapture
cargo test --release --manifest-path C:/portable/OpenKoikatsu/Tools/ok-extract/Cargo.toml --lib native_accessory_glass_material -- --ignored --nocapture
python -X utf8 C:/portable/OpenKoikatsu/Tools/GamePoseProbe/verify_eye_create.py C:/portable/OpenKoikatsu/Saved/Tests/NewCards306SourceWorld
```

錯誤紀錄：root `Docs/ERROR_LOG.md` E-162。最後正式編譯：`NewCards306Final157UnrealBuild.log` Succeeded29.77秒。
