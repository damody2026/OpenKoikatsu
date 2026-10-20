# 街區與 CardReader 色彩輸出修正

## 計畫

1. 以 CardReader 目前正常顯示的共用材質／相機流程為基準，核對街區差異。
2. 修正共用色彩輸出，不改角色卡顏色，也不對 C019 使用特別補色。
3. 同卡、同鏡頭、同材質做 LUT 開／關 A/B，檢查服裝與頭髮色彩及細節。
4. 三张不同來源卡驗證，再跑當前功能回歸與發布完整性核對。

## 已定位差異

CardReader 的 `AKKPreviewActor::SetupStudio` 載入 `Content/KKPreview/KKSDefaultSaturationLUT16.png`，
以 ColorGradingIntensity=1 套用原作共用色彩濾鏡。
街區及 runtime 診斷相機只有 ToneCurveAmount=0 / ExpandGamut=0，未套用該 LUT。
LUT 由 `Tools/generate_kks_saturation_lut.py` 根據 BetterColorPicker 原作濾鏡反表生成，
不是取用某張角色或參考圖片的補色。
例如濾鏡輸入 RGB(179,99,87) 轉為約 RGB(176,29,15)，解釋街區紅色制服偏灰棕。
本輪新增共用 display profile 載入與相機設定，沿用同一份 LUT 檔案。

## 錯誤與防止重犯

- 使用者已確認 `StreetColorProfile21CleanLibrary/visual.back.gpu.png` 的光影與髮絲高光符合目標。
  後續只調整頭髮描邊，不再變更這版底色、高光、陰影或濾鏡。
- 原始 COM3D2 `hair_r144` 確實只有 UV0，並非 APNG 遺失 UV1：
  `Saved/Tests/StreetAppearance01/source_mesh_channels.json` 保存原始 bundle 頻道資料。
  CardReader 單 UV skeletal vertex factory 會重複最後可用座標；runtime 的固定四組 UV
  以前把缺少的髮絲 UV1 填零，兩者行為不同。現已對 hair shader 的「未宣告 UV1」
  沿用 UV0；存在的 UV1（包括全零 UV1）仍完整保留，眼睛等其他 shader 不套用此規則。
- 第19～21輪部分補丁同時保留兩份 `RuntimeMaterialBake` shader archive。
  不同材質目錄不代表 shader 程式庫互相獨立；舊 archive 可遮蔽新 shader，產生黑色材質。
  新增 `prepare_street_social_render_patch.ps1` 組裝只有一份 common material pak 的新補丁。
  來源圖片比較與正式發布均拒絕缺少 shader、uncooked shader map 及 default material fallback。
- 只移除 StreetSocial map pak 也不正確：它包含此 executable 所需的新增 global shader。
  來源比較改用實際 StreetSocial map 的診斷入口，保留完整 production patch，避免
  載入 shader archive 不匹配的舊 CrowdRoom map，並讓角色比較與正式版本使用相同補丁。
- 第21輪完整測試完成15次高模往返後，後段測試超過540秒限制而被測試工具停止。
  此輪不算完整通過；最終完整測試需給予足夠的高模載入及所有互動驗證時間。
- 頭髮描邊使用原生顏色、遮罩與 alpha，增加依目前 view projection／解析度計算的
  最小像素寬度。這是共用顯示策略，不修改卡片、不為單張圖片補線；作者設定線寬0
  仍不產生描邊，遮罩與髮尾的漸細效果仍會乘上最後的擴張量。

- 之前 CPU／GPU 一致及來源材質完整性通過，沒有證明畫面顏色與 CardReader 一致。
  不得再用兩條同樣缺少色彩濾鏡的路徑互相比對，宣稱洗白問題已解決。
- 比對必須涵蓋最終色彩輸出；原作／CardReader 的濾鏡也是呈現流程的一部分。
- LUT 以線性取樣資料載入：SRGB=false、ColorLookupTable、Clamp、Bilinear、單 mip，
  不能將 LUT 本身再次 sRGB 解碼。
- Gameplay camera 必須套用同一 profile 並明確 PostProcessBlendWeight=1，
  診斷 capture 從同一 camera 複製設定；只改診斷相機不能算修正真實街區。

- StreetColorProfile08 編譯發現新增外部 API 未使用具名 namespace 的弱指標場景表。
  修正為與既有 API 相同的 `OKRuntimeScene::ActiveScenes.Find`／`Pin`，不可直接把弱指標當共享指標。

- 額外定位：`_VerticalShift` 已從原作 session 解析，但 `_KanoVerShift` 只對眼睛綁定，
  衣物 shader 沒收到這個全域參數，細節綠通道無法依原作規則移動陰影 ramp。
  修正所有宣告 `_KanoVerShift` 的來源 shader 綁定，不寫入特定卡片值。
- CardReader 的皮膚／衣物／item 主光方向為 `-ResolvedView.ViewForward`，
  此處必須區分 legacy preview 與 exact Forge 材質。只對 Koikano 皮膚／衣物使用視角主光；
  exact Forge opaque/item 與髮絲保留來源主光，不可從 legacy 分支推論 exact 分支；
  只在方向或材質數量改變時更新，診斷三視角各使用自己的鏡頭方向。

## 驗收

已完成並發布 StreetColorProfile24；正式啟動器完整性檢查通過。完整街區驗收與最終獨立高模主機驗收保留各自真實證據，不互相改標。

## 2026-10-09 光影調查及失敗證據

- 材質提交完成後 `E.Material` 會釋放；從該暫存陣列判斷動態光方向會永遠跳過更新。
  已將每個基底／outline 的光方向策略存成獨立旗標，隨正式材質存活。
- StreetColorProfile16 使用世界角色的 shadowDepth 規則，會產生較硬的髮束陰影。
  尚不符合使用者提供的明亮 Studio 背面及髮絲高光參考，不得宣布驗收完成。
- 已讀取遊戲與 Studio 的 globalgamemanagers：兩者 `m_ActiveColorSpace=1`（Linear）。
  不得以猜測 Gamma 專案為由，全面關閉原生貼圖的 sRGB 取樣。
- StreetColorProfile18Full 再次在高模主機發生 `FReadOnlyMeshDrawSingleShaderBindings::SetShaderBindings`
  存取違例，證據保留於 `Saved/StreetSocial/Runtime-StreetColorProfile18Full/HighInteraction/DE9C879341AEDCACB4D3E7AEBF14A771/host.log`。
  此輪完整驗收失敗；先前相同崩潰尚未定位，不能用成功的單次測試掩蓋。
- `ok.ShotPreview` 直接載入的多輪圖出現頭髮位置或鏡頭尺度異常，包括 CPU_SKIN=1 的第19輪。
  這些圖不能作為正常 CardReader 外觀參考；使用者提供的原作圖仍是外觀目標。
- 第12輪 diagnostic 局部變數 `Textures` 與外層同名，被 UE 編譯規則拒絕；改用明確名稱。
  第14輪 `FImageView` 不接受原 uint8 指標建構方式；改為正確的 FColor 影像視圖。
- PowerShell 下不得將萬用字元放在 rg 的路徑參數。應使用存在的資料夾與 `-g` 過濾。
  查找檔案前先以 `rg --files` 確認，不要反覆猜不存在的檔名。
- 測試脚本參數須先讀宣告：street runtime 使用 `-Patch`，APNG source visual 使用 `-ShaderPatchDirectory`。
  地圖必須使用 `/Engine/Maps/Templates/Template_Default`，不可使用不存在的 `/Game/Template_DefaultGameMode`。
- 第19輪新增僅在 visual capture 開啟的材質取樣診斷；一般材質模式為0。
  診斷圖用於定位 UV／高光，不可充當正式外觀或發布證據。

## 2026-10-09 頭髮描邊

- 使用者確認 StreetColorProfile21CleanLibrary 背面圖的光影；後續鎖定此色彩／Studio 陰影／髮絲高光。
- 第23輪加入投影像素最小寬度後，與第22輪全部像素相同，不能據此宣稱描邊成功。
- 根因：`ok_part_outline_enabled` 讀取 `_OUTLINE_ON`，但 C019 的 Forge `hair_r144` 材質 keywords 為空；其 shader 的 OUTLINE pass 無條件存在。CardReader 在 shader_kind 1/10 上同樣不使用此 keyword 作為開關。
- 共用 runtime material capture 對頭髮採用原作／CardReader 的無條件 pass 規則，仍保留其他 keyword-controlled 材質政策、來源線寬／遮罩及零寬。新增 per-part outline_enabled 診斷，必須同時檢查實際畫面。
- 此次搜尋曾猜測 Native、KKUE5 及 session_renderer.rs 路徑，造成無效讀取；後續先使用 rg --files 確認實際路徑。
- 第24輪已確認 Forge hair 真正建立 outline，三張卡九個視角 CPU/GPU 像素誤差皆為0，沒有 shader fallback。C019 背面新增10857個深色髮束線條像素，其餘250173個原髮色像素完全不變（範圍依第23輪紫髮遮罩統計）。
- UE SaveStringToFile 遇到日文名稱會寫 UTF-16 BOM；新的 per-part 發布檢查首次以 UTF-8 讀取失敗。已加入 BOM 辨識，並讓測試 fixture 使用 UTF-16，避免在只有 ASCII 的假資料通過後仍不能驗收真實角色。
- StreetSocial Python 測試69項、shader codegen測試11項通過；完整高模往返及對話／操作驗收仍進行中，尚未替換正式 receipt。
- 第24輪正常縮減模型三視角通過，原始幾何九視角外也覆蓋實際街區路徑。Presentation 驗收五人站姿、六人跑步、六滑桿、十五組互斥關係及同步均通過，實際相機 LUT 已套用。
- 與使用者確認的第21輪背面圖比對，252246個紫髮遮罩像素逐值完全相同；新增輪廓線的統計與各髮部件 outline_enabled 證據見 `Saved/Tests/StreetAppearance01/outline24.validation.json`。像素統計不等於原作逐像素還原。
- 高模主機日誌另有既存 CommonUI GameViewportClient 提示；此主機實際互動使用原有 Slate，尚無此次描邊修改造成的輸入失敗證據。不得把既存提示誤當成本次共用材質錯誤，也不得將舊的偶發 Renderer 崩潰標成已修正。

## 反鋸齒補充（使用者追問）

- 專案 `r.AntiAliasingMethod=1` 為 FXAA，正式街區不是完全沒開 AA。1024×1024 的來源比對 SceneCapture 額外關閉 TemporalAA，不能把這些驗證圖當成正式時間式 AA 畫質證據。
- 增加可重現的 runtime 測試參數 AntiAliasing（Project/FXAA/TSR）與 RenderScale（100–200）；各比較保留實際啟動命令及 CVar 生效日誌。
- 第24輪 TSR 100% 的細髮線局部圖仍可見碎點；不能只憑設定成功或 presentation 通過就宣稱視覺改善。目前比較 FXAA +150% 內部解析度，不修改已確認的材質與光影。
- FXAA +150% 內部解析度的真實 1920×1080 街區 presentation 驗收已通過：五站姿、六跑步、十五關係切換、相機 LUT 均正常；日誌確認 r.AntiAliasingMethod=1、r.ScreenPercentage=150，沒有 shader fallback。
- 正式啟動器讀取驗收 receipt 的共用畫質設定，不改 CardReader 或某張角色卡。預設將使用此150%模式（1920×1080輸出時，內部約2880×1620，像素工作量為原生2.25倍）；`StreetSocial.cmd -RenderScale 100` 可使用原生內部解析度，100..200可調。
- 畫質發布證據與 LUT／執行檔一樣保留 SHA256，啟動器驗證證據未被替換。來源三視角原始比對仍保留非時間式版本，以免歷史濾波掩蓋 CPU/GPU 差異。
- 最後整合測試在道具活動段遇到 UE 預設材質自身缺少 Nanite usage 的警告（DefaultMaterial fallback 到 DefaultMaterial），不是角色 cooked shader 缺失。發布閘門先前把所有「Default Material will be used」一概判為角色 shader 失敗，會誤拒這則既存引擎警告。
- 現僅排除這個完整、明確的 Engine DefaultMaterial Nanite 警告行；其他材質 fallback／Missing shader／uncooked shader／Invalid shader map 繼續拒絕。新增測試驗證該行不會掩蓋下一行真實 hair shader 失敗。其餘來源驗收沒有此警告。
- 完整整合驗收的監督預算1200秒仍低估了十五次高模主機啟動，加上十組原作打西瓜動畫各13–15秒等實際耗時；不能縮短原作動畫或跳過既有功能來湊時間。
- 測試工具預設預算改為2400秒，新增 Resume：只接管 game.pid 對應的同一執行檔、manifest、patch及測試模式，拒絕重建 selection 或關係重定向測試。原有驗收條件完整保留。
- 在原監督器期限前核對唯一父程序88780的命令、遊戲70420的路徑與父子關係，只停止這個測試監督器；遊戲及所有已完成案例持續執行。由 Resume 監督器接續等待同一次完整驗收結果，並保留原報告路徑及證據，沒有重標另一個測試為完整成功。

## 高模主機建置隔離

- 第24輪完整 StreetSocial 整合驗收已成功（包括十五次高模往返、所有共同活動、十組打西瓜）；首次發布檢查隨後攔到 root CardReader 模組指紋從265f69c4…變為5126a096…，檔案最後寫入時間14:33:58，晚於該輪十五個主機測試的完成時間。不得只更新指紋來發布未驗證模組。
- 原正式 receipt 未被成功發布流程替換；失敗檢查與備份保留。StreetSocial host 工具改為建立唯一、獨立的專案快照，複製主要模組及外掛 DLL／module manifests／shader資源，分享唯讀資源 Content。hash驗證複製過程沒有中途變動。
- 快照 `Saved/StreetSocial/Build/HighHosts/StreetDisplay24` 已通過實際 C019 高模載入／男女互動／返回。重新跑相同街區執行檔及150%畫質下的十五組關係往返，另外保存為 StableHigh 證據；保留之前完整街區整合的真實來源，不把其舊高模子測試改標為新版本。
- selection／發布／啟動器均驗證所有凍結 DLL 的 SHA256，外部重新建置 root editor 不再覆蓋街區所使用的高模主機。最終發布必須核對新高模 evidence 的 project 與快照一致。

## 最終驗收與發布

- 已發布執行檔：`Saved/StreetSocial/Build/StreetColorProfile24.stage/OpenKoikatsu/Binaries/Win64/OpenKoikatsu.exe`，SHA256 `3e62eaad2d73157c15db918c144441fbe0b03ade681c589df45e89b1412b2157`。
- 共用預編譯材質：StreetDisplay23；patch 只有一個 RuntimeMaterialBake shader archive，避免舊版遮蔽新版。
- 三張卡 C019／C000／C044 的九個來源幾何視角 CPU/GPU 全部像素誤差0；C019 一般縮減模型三視角也通過，頭髮 outline_enabled 與實際黑色髮束線條均確認。
- 完整整合：`Saved/StreetSocial/Runtime-StreetColorProfile24Full/social.validation.json` 通過，所有原有活動與十組原作打西瓜完整播放並驗證。
- Presentation：五站姿、六跑步、六數值滑桿與十五關係切換及同步通過；DialogueContext：二十組原作前後文、五告白成功／五告白失敗對話及二十二來源語音通過；TouchReaction：二十組身體點擊反應通過。
- 最終固定高模主機：`Saved/StreetSocial/Runtime-StreetColorProfile24StableHigh/social.validation.json` 十五組陌生／朋友／戀人往返通過，街區同一執行檔、相同共用材質及150%畫質。主模組 SHA256 `5126a0962cf46fbf4f22c6e04678a68cf5de7fe6a80061230ae516f36cf9f9ae`；75個DLL／module manifests 固定於專屬快照。
- 最終抗鋸齒：FXAA +150%內部渲染的實際街區操作驗收通過；來源比對圖保持1024×1024非時間式版本，不能將其誤作正式畫面 AA 證據。
- 七十項 StreetSocial Python 測試及十一項 shader codegen 測試通過；發布時重新執行來源對話、版面、電話、聯絡人、高模等獨立驗證，全部通過。
- 正式 `receipt.json` 已更新且保留前版備份；`run_street_social_development.ps1 -ValidateOnly` 與 `-RenderScale 100` 完整性檢查通過。重新開啟 `StreetSocial.cmd` 使用新版。
- 舊輪偶發 Renderer 崩潰仍作為歷史未定位問題保留；本輪完整整合與最終凍結高模15次均成功，不宣稱已修復未定位的歷史崩潰。
