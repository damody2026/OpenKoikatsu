# Stage374 私有骨架來源相容性

動畫匯入先核對原卡 SHA1、原生共用骨架、renderer/role、skin source、使用中的 palette slots。此次補上 v1 未涵蓋的私有骨架階層與掛接設定，避免 mod 只改私有 rig 時把舊持久化模型視為相容；不依卡名或角色套用例外。

## 實作

- 原 `ok_part_skin_source_hash` 保持不變，新增 `ok_part_skin_source_hash_v2`。新 FKKImportedPart 保存 `SkinSourceHashV2`，動畫 CheckRig 在建立持久化 package 前拒絕不一致。
- v2 使用版本 domain、既有 v1，加上私有骨架 names/rest/SMR count/bone order、按 ID 排序的 Transform name/parent/sibling/TRS，以及 parent key/local placement、copy-weight/implant/coordinate 設定、植入與移除集合、body bridges、clothes component。
- 原作 implant 的橋接 Transform TRS 每幀從共用 body 覆寫，因此不把 live bridge TRS 當作 immutable 來源；它的 hierarchy 與橋接 assignment 仍核對，body 的來源則由既有共用 CheckRig 驗證。原作對齊依據為已移植的 `dbone/implant.rs::sync` 與衣服 AssignedWeightsAndSetBounds 路徑。
- 不 hash evaluated worlds、粒子狀態或 collider flags。集合／map 排序，不因 Rust HashMap seed 改變。
- 不改 AccessoryRuntime/SessionBlob 的 bincode layout，保留既有自製 APNG；decoded bundle cache108 不變，session pipeline248 排除中間版快取。
- 舊 Character 沒有 v2 時維持原 v1 檢查並提示重新匯入；既有 uassets/Audit JSON 不覆寫。完整私有來源保護適用新匯入資產，沒有假裝舊資產已具備 v2。

## 最終驗證

- BuildR3 完整 Editor actual0；RustR4 最終契約 fixture actual0；LegacyR2 既有 KK_346095_builtin_v2.png 唯讀載入 actual0。
- R2 全10項矩陣 actual0，男女各一張／outfit0，19+57部件完整 SM/SK、UV/colors/morph/native mips 與參照重開驗證。
- 4段原作 walk/stand 以獨立無損 float codec 保存並驗證 compressed 播放、中間 seek 與有限值。
- 男卡810732、女卡1536012，共2346744次原始 keyframe／所有section頂點比較；max .006482028cm／.001672517cm，原.05cm門檻未改。女卡前輪.001669370cm與此次不是完全同值，仍在原門檻內，沒有冒稱完全相同。
- ContractR1Male02／ContractR1Female Automation Success actual0：保存的76個v2 metadata皆有效、動畫前後hash不變；暫存outfit副本只改v2、v1仍相同，CheckRig正確拒絕且diagnostic明確，原資產未修改。
- DetachedR1 Automation Success actual0：parser/pack absent，4clips、152skin instances、391124vertices，完整部件／clock／compressed／seek／finite通過。
- R2.audit.json封存1115檔；重新逐檔核對SHA與大小通過。舊Stage372與373已封存資產／來源snapshot合計核對4997個條目，原Audit JSON SHA保持原值。
- Audit SHA256：`04F8407E200BEA03D7A89BF4A60C481ABEEB7ABC3C89AD54AFEF110B4F8D7963`。本輪錯誤／中間版紀錄見ERROR_LOG E252，沒有把R1中途結果列入驗收。

## 重跑

使用 fresh tag：

```powershell
Tools/run_card_animation_matrix.ps1 -Stage 374 -Cases Tools/card_animation_cases374.json -RunTag R3
Tools/run_source_contract374.ps1 -Plan Saved/Tests/AssetImport374/R3.plan.json -Tag R3
Tools/run_detached_animation_matrix.ps1 -Stage 374 -Plan Saved/Tests/AssetImport374/R3.plan.json -Tag R3
python Tools/audit_card_animation_matrix.py --stage 374 --run-tag R3 --detached-tag R3 --contract-tag R3
```

此範圍驗證骨架／skin assignment 契約、原始 keyframe 頂點與隔離播放；不是整個 mod archive 的材質/貼圖版本追蹤，不是所有卡、任意中間幀原生頂點、完整 controller、GPU 最終畫面或 Shipping 驗收。
