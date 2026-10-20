# Stage309：四張新增卡片的共用載入校對

## 範圍與獨立來源

本輪加入 KK_226176、KK_384914、KK_398302，以及「弹丸论破 - 七海千秋3」。卡片位於 `C:/portable/KoikatuSunshine/UserData/chara/female/`；遊戲 mesh／material／MainTex dump 與 SnowShot 參考照在 `C:/portable/KoikatuSunshine/mesh_dump/`，不是 UE 專案根目錄。

| 卡片 | 遊戲角色目錄 | mesh objects | 參考照 |
|---|---|---:|---|
| KK_226176 | Inugami Korone_0 | 49 | SnowShot_2026-09-29_18-02-08.jpg |
| KK_384914 | 九十九 過子_0 | 61 | SnowShot_2026-09-29_18-03-53.jpg |
| KK_398302 | Minase アキコ_0 | 169 | SnowShot_2026-09-29_20-16-01.jpg |
| 弹丸论破 - 七海千秋3 | 七海 千秋2.0_0 | 93 | SnowShot_2026-09-29_20-18-48.jpg |

dump 包含 disabled renderer、衣服全穿／半脫狀態；不能把全部 JSON 當成可見的單一服裝。

## 161 基準與診斷（已完成）

- `NewCards309Baseline161`：Tpose PID40220、Walk PID50732，正常退出 0，四卡 24 PNG（900×1080）。每卡全身／正臉／側臉；`NewCards309Baseline161Audit.json` 通過載入、attachment 與原根15張 Stage286 screenshot SHA 保留檢查。這不是全 shader 外觀認證。
- `NewCards309Identities161`、`NewCards309Renderer161`：安裝遊戲的卡片來源、KKEx、ME、overlay、runtime part 資料。來源貼圖比較保留在 Renderer161 目錄。
- `NewCards309NoOutline161`：Minase／七海 Tpose，PID52536，正常退出 0，6 PNG。Minase 黑裙恢复白色圖案，但肩部缺口保持；七海即使關輪廓仍無花紋。不得把兩者當同一問題，也不得把 `-DisableOutline` 當正式解法。

## 已確認的根因與共用實作

1. **DXT1 alpha**：第三方 generic BC1 RGB decoder 將 endpoint0≤endpoint1／selector3 解成 alpha255。Korone 襯衫原生有79,687透明像素，旧 PNG 卻全255；RGB幾乎一致不能證明材質正確。改用 BC1 RGBA decoder，沿用共用 BGRA→RGBA 轉換一次，無卡名判斷。
2. **null MainTex**：384914白蝴蝶結 `cf_m_top_sailor06_03` 原生 `_MainTex=null`，使用衣服 shader 的 white default。UE 原本未建立材質而顯示黑色預設。已明確對 Forge/main_opaque、Koikano/main_clothes_opaque 使用白色宣告預設；保留存在的 PNG，未知 shader 不猜預設。
3. **eye create row provenance**：Unity bundle Texture2D raw rows與Sideloader PNG檔案方向不同。Korone虹膜原先反向，另三卡不能一律再翻。改從實際選中的catalog／GUID／bundle或PNG來源轉成top-down；不借其他asset同名貼圖，也不按卡名、眼色決定。
4. **create_eyew**：`Saved/ShaderDxbc/mm_base/Koikano_create_eyew/0001.asm.txt` PS是`lerp(Color2,Color,Main.r)`，在線性空間運算、RT輸出sRGB，alpha亦取色彩插值。旧gamma插值／強制255有誤，現在依source colorSpace處理。
5. **source outline keyword**：七海衣服沒有`_OUTLINE_ON`，旧UE仍無條件新增衣服輪廓pass。Session與cache保留此旗標、additive FFI查詢；不改既有ABI table，不動使用者延期的下巴輪廓。
6. **ME floats**：共用push_meshes以前重播shader／texture／color卻漏numeric overrides。補在shader更換後重播，沿用object type／coordinate／slot／material識別，涵蓋衣服與髮絲。

7. **Vertex format／stream layout**：七海服裝 UV 為 Float16；舊程式讀 Float32，花紋因而集中成近常數。位置、法線、UV 共用按 scalar format 讀值，支援 Unity 的 half／normalized／integer formats。Stream stride先4-byte對齊、起點16-byte對齊，尾部legacy implicit資料才按每頂點擴展；不能把起點padding當stride。獨立遊戲 Mesh.uv 與四個服裝mesh對照 max<3e-8。
8. **上衣 Kind 與遮罩**：ChaControl.ChangeClothesTop 只在Kind1／2載入Sailor／Jacket SubA、SubB。Minase Kind4洋裝保留的舊SubA ID被舊載入器拿去遮身體，造成上臂大片缺口。模型／狀態與Body／Bra／Inner遮罩現在共用active top selector；top list先載入、真正active subpart再替换。四卡所選BodyMask名稱已與遊戲material dump核對。無mask時顯式clear，ME仍後播。
9. **衣服 outline vertex pass**：Koikano_main_clothes_opaque VS讀camera distance、global linewidth、DetailMask.b與_LineWidthS；不能固定CPU擠出0.15cm。UE WPO使用`normal * ((distance_cm*.001+.6)*globalLineWidth*.3*(1-detail.b)*_LineWidthS)`。GPU outline mesh零CPUoffset仍必須反轉winding，以獨立bOutlinePass標記，不能以offset>0判斷。是否建立pass仍由source keyword決定。
10. **UncensorSelector的預設身體**：遊戲BodyData首先取有效卡片BodyGUID，否則取Config中Default Female/Male Body的display name。三卡未保存GUID，遊戲設定`[Female] BP Profundis`有9823頂點，舊UE却取原版6823；Korone明確SAC.innie2為9080。現在從安裝zipmod的body宣告解析GUID／sex／displayName／file／asset，選取指定prefab與其SMR骨骼順序，合併所需新增骨骼；不固定SAC名單。明確Default.Body.Female/Male保持原版。cfg stamp納入Session cache key。

11. **大型 vertex blob TypeTree**：BP身體的m_VertexData有1,218,064bytes，第三方parser卻將TypelessData套用1,000,000 typed-array count限制。Lenient處理使vertex欄位整個丟失，後面的欄位也讀錯；164雖選到正確來源，mesh仍未載入。將unity-asset-binary 0.2.0 vendored到Tools/vendor，以原始所屬object remaining bytes作界限，parse與PPtr scan皆修改；typed-array element限制保持，沒有擴大固定常數。包括負長度／超長／截斷、下一欄位與scan位置控制。Cargo global registry未修改；upstream commit／MIT LICENSE與變更README保留。

正式cache目標：CACHE101／SESSION166，CLI `v-sacbody-278-prefab-renderer-instance`。165的renderer Transform錯誤亦不得复用；98診斷曾存錯UV、164曾缺BPbody，不能复用。原始161／162／163截圖以及161–165 diagnostics保留，禁止覆蓋當成新版本證據。安裝來源測試已確認四卡對應9080／9823／9823／9823頂點及100／134／134／134個SMR骨骼，非只驗證display name。

12. **同Mesh被多prefab共用**：七海白兔飾品其實是DS27 Blue Chinese Opera Mask加ME貼圖；Mesh名稱`wed_mik_MZ`不能當prefab識別。UnityPy独立確認Mesh410由GO406／420／432／445四個blue／red／Studio renderer共用。舊SkelGraph.mesh_go只保存最後一個GO，mesh_prefab_locals又使用全bundle對應，N_move ancestry從錯誤GO開始，將白色兔子部分放到頭後。166保存全部引用，依selected asset-root ancestry選renderer，再查local與move nodes；SMR的旁支bone也限定同root。不按卡名修位置。單元控制兩個prefab、同Mesh、引用順序反轉、無關mesh、缺root均PASS。同一prefab內同Mesh多instance尚無完整per-renderer instance表示，現明確warning，不能列為已解決。

## Source與驗證層次

- 遊戲decompiled ChaControl.ChangeClothesTop／ChangeClothesTopSub；UncensorSelector.cs的BodyData、DefaultData、BodyData constructor與DisplayNameToBodyGuid。
- DXBC：`Saved/ShaderDxbc/mm_base/Koikano_create_eyew/0001.asm.txt`、`Saved/ShaderDxbcCoBot/co_bot_01/Koikano_main_clothes_opaque/0000.asm.txt`。mod可讀shader在Saved/External/KKShadersPlus，來源為GitHub xukmi/KKShadersPlus；尚未當成已完成的UE shader支援。
- Scoped Rust unit：DXT1 selector／truncate、Float16 UV多stream alignment、null MainTex white/default precedence、create_eyew linear／alpha、outline flag cache roundtrip、Kind0..5 subpart／mask matrix、UncensorSelector saved/default／invalid GUID／exact Config key。逐項單线程，避免診斷測試全庫並行。
- Installed independent fixture：四卡active BodyMask材質名稱；verify_dump_uvs.py比較獨立game Mesh.uv和runtime JSON，不拿dump頂點入production。163只有5mesh可比，另外3body頂點數不符，嚴格verifier FAIL揭露預設mod缺漏；不能跳過後計為全通過。
- Renderer165嚴格8mesh全PASS、UV max2.9783630339785816e-8；BodySources165R2四卡全部skin weights max0、native SMR bone order一致。來源9823／134不能拿舊6823／100cache作golden。
- 165重跑最初全庫的三個失敗：兩項是測試假定raw MainTex必須存在、無implant即不需private runtime；改驗證真正catalog create RT及disabled／enabled geometry／cache，R3 PASS。第三項硬編外部Converted snapshot全缺，保留嚴格n_ok>0及diff，改明確manual ignored，不計PASS。沒有宣稱全庫已執行完成。
- NewCards309Final162四卡24PNG為中間控制：和服花紋／白蝴蝶結／透明按鈕與眼睛方向改善，上臂缺口尚在。
- NewCards309Final163四卡24PNG兩程序normal exit0：Kind mask與衣服WPO加入後，Minase上臂完整，裙花紋保持；ContactSheet已目視。這仍是body-default修正前的證據。

## 已排除的假設

- 四卡 outfit0 沒有KCOX衣服overlay。卡片其它coordinate有資料並不等於此套衣服有overlay；沒有為猜測添加衣服overlay分支。
- 七海MainTex兩份RGBA与遊戲dump相同；花紋消失需查render geometry／UV／shader，不能重畫貼圖或染色。
- Minase兩個衣服部分的Tpose world vertices、三角繞向大致與遊戲一致；不能先按名稱整體翻轉indices。

## 最終驗證及外觀待辦

- SESSION165四卡24PNG，Tpose PID61800／Walk PID63788正常exit0，PV與attachment gate通過，ContactSheet與SideContactSheet已目視；發現七海白兔renderer錯位後，前十卡Regression165主runner51652／UE69968保留identity後中止，部分結果不列為回歸PASS。
- SESSION166 shared-mesh unit PASS；independent game rigid renderer 244世界頂點PASS，max3.5823822019942497e-7m，mean9.889214987865538e-8m。Build166 Succeeded 30.04sec。Final166 Tpose PID66032／Walk PID75700均exit0，各12PNG、4PV、83attachments、max0cm；palette max2.252e-6／2.262e-6cm。ContactSheet、SideContactSheet与七海完整正臉已目視，白兔部分恢復。原根15張SHA保持。Renderer166四卡診斷PASS244.36sec，嚴格UV8mesh PASS，max2.9783630339785816e-8。前十卡60PNG回歸已完成。
- UncensorSelector Config=Random尚未重現C# seeded RNG／registration order，會明確記錄unresolved；不借game dump強制挑一個body。目前安裝config為固定BP Profundis，這四卡不走Random。
- Minase帽子source為xukmi/MainItemPlus，目前ShaderKind0；baked RGB確實為淡藍但UE default lit近白，game reference偏藍。不能把它假稱Koikano／Forge家族或以全局染藍修正。完整mod ForwardBase仍未移植；Korone上衣亮度、髮光、皮膚細節亦有差異。
- 七海白兔renderer錯位經166逐頂點與RHI驗證恢復；髮光及上方微小黑點／透明髮絲輪廓仍有差異。不能為縮小camera bounds而依高度裁頂點，亦不能以attachment numerical gate代替shader畫面。
- 使用者延期的下巴輪廓保持延期。84張RHI與source controls完成仍不等於四卡全shader外觀一致。

## SESSION166 最終結果

| 批次 | Tpose／Walk PID | PNG | 每個姿勢 PV／attachments | attachment max | palette max（Tpose／Walk） |
|---|---|---:|---|---|---|
| 四張新增卡 NewCards309Final166 | 66032／75700 | 24 | 4／83 | 0cm | 2.252e-6／2.262e-6cm |
| 前十卡 NewCards309Regression166 | 64920／10516 | 60 | 10／204 | 0cm | 2.252e-6／2.262e-6cm |

四個UE程序均normal exit0，84PNG皆900×1080。報告核對原根Stage28615張SHA全保持；沒有覆蓋舊screenshots。新卡ContactSheet／SideContactSheet，旧卡兩張分頁ContactSheet／兩張SideContactSheet皆目視；另看七海完整正臉、Minase全身、Korone正臉、March／Alice／Junko／384915／384917正臉、384283 Walk正臉、091207 Walk全身。眉毛、白蝴蝶結、和服花紋、七海白兔、Minase上臂保持；未見本輪新增頭髮／髮飾綁定回退。不代表現有材質差異消失。

BodySources166首次0passed／1ignored不可算PASS；166R2明確--ignored實際四卡source權重／骨架order控制PASS6.56sec，全部skin weights max0，頂點9080／9823／9823／9823及骨骼100／134／134／134符合。SharedMeshUnit166R2也實際1PASS。Renderer166嚴格UV8mesh全PASS。沒有重跑整個Rust庫或宣稱所有手動fixture通過。

來源29檔hash在Saved/Logs/NewCards309SourceManifest166.json；source／code／vendor patch／binary／tests／logs／captures／MD hash索引為Saved/Logs/NewCards309FinalAudit166.json。截圖及個別Audit在Saved/DebugShots/NewCards309Final166、NewCards309Regression166與Saved/Logs對應Audit.json。本輪未安裝或啟動Studio probe；最終owned UE／Studio程序0、installed probe0。

下一輪仍需完整MainItemPlus ForwardBase／normal與NormalMapDetail conversion／authored MIP／LOD／aniso／depth-stencil；七海上方微小黑點先查透明髮絲與輪廓pass，不能依高度裁頂點作解法。Config Random及同prefab同Mesh多renderer instance要另補來源一致的表示法與控制。使用者延期下巴維持延期；既有KK091胸前髮束與Junko服裝線／袖口差異不能從本輪scope中抹掉。
