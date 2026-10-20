# 菲比 v1.1 全 outfit dump 核對（Stage338）

2026-10-01。已完成現有資料的全量核對；結論是**網格來源皆可找到對照，但不能宣稱所有 dump／渲染層完全等價**。沒有套用逐卡修補，也沒有修改正式截圖或 production 匯入／shader 邏輯。

來源：`C:/portable/KoikatuSunshine/mesh_dump/菲比v1.1.png`；角色卡：`C:/portable/KoikatuSunshine/UserData/chara/female/菲比v1.1.png`。現有 outfit 0、1、2、3、5、6，資料夾沒有 outfit4，不能聲稱測過 outfit4。每套只有一組平面 MeshDumper 輸出，沒有多個時間戳子資料夾；其中的多份 mesh/material dump 全部列入。

| outfit | 網格 dump | runtime 網格 | 比對範圍內逐值相同 | 原始輸入有差異 | 未找到對應的材質／渲染層 |
|---|---:|---:|---:|---:|---:|
| 0 | 50 | 30 | 8 | 22 | 22 |
| 1 | 41 | 26 | 8 | 18 | 17 |
| 2 | 49 | 30 | 8 | 23 | 20 |
| 3 | 49 | 32 | 11 | 22 | 21 |
| 5 | 45 | 27 | 8 | 20 | 19 |
| 6 | 48 | 31 | 8 | 24 | 18 |
| 合計 | 282 | 176 | 51 | 129 | 117 |

右三欄按 renderer/material 計數，共297筆；多材質 renderer 所以不等於282。51筆是本次實際核對範圍內相同，並非完整 shader 或世界座標等價認證。`DumpAudit.json` 保存每筆來源 field/slot、階層、enabled、匹配方法、誤差及來源 SHA256。

## 已確認的結果

- 六套176個 runtime 網格全部找到 dump 對照，`unmatched_runtime_parts=[]`。
- 152筆可直接核對的單材質三角索引完全相同；已匹配材質的 shader 名稱沒有差異。
- 非虹膜 UV0–UV3 最大誤差 `5.957031246062172e-8`，小於 `1e-6` 門檻。左右虹膜合計12筆另行保留差異，不移除失敗樣本。
- 六套前髮／背髮主材質共12張 MainTex：解碼後 RGBA 最大誤差全部0。
- 六套 `hairF.MECopy1` 原始 PNG alpha 不同，但套用 production 已有 `main_item_studio_alpha` 的 `alpha * _alpha` 烘焙後，RGBA 最大誤差全部0。這是表示方式差異，不是缺失的透明度修正。
- 共核對235筆貼圖、2868筆共同材質／ST參數。282組網格 JSON/OBJ的頂點數、非空頂點串流長度、有限值及三角索引皆有效；宣告的 PNG 都存在。額外的 parts、skeleton、shape JSON 也讀取並核對有限值，保存 SHA；尚未證明其骨骼世界姿勢與 runtime 相同。

## 未能宣稱完全對上的部分

Stage339 補充：12筆 `Highlight_*_rend` 已由 SliderHighlight plugin 的 CreateHighlightRenderer 證實為 Maker 滑桿編輯輔助。新增實際 draw-input 欄位後，虹膜 source UV、EyeDraw 設定及 skin 設定另行驗證；詳見 PHOEBE_OUTFIT_DUMP339.md。下列保留 Stage338 當時的原始結果，不改寫其歷史驗收。

1. dump 比 runtime 多出的117筆包含半脫／脫衣狀態、gag eyes、原生其他解剖網格、陰影／輪廓、Height bar indicator 等。12筆是 `Highlight_cf_O_face_rend`／`Highlight_o_body_a_rend` 附加渲染層。這些項目完整列出，沒有為了通過而丟棄。`Renderer.enabled=true` 不代表祖先節點 active；dump 缺少完整 activeInHierarchy 證據，因此117不能直接解讀為117個匯入 bug。
2. 虹膜原始 UV/ST/rotation 有差異。`eye_gaze.rs` 的 `EyeDraw::apply` 將 shader affine UV 變換折入頂點串流，`IrisRuntime` 還受注視方向影響。現有 dump 與本次時間0 runtime 未建立同一注視狀態，故不能拿原始 UV 相減直接判斷錯誤，更不能再用逐卡翻轉 UV。
3. 主髮的 native `_Color` 等與來源 material metadata 有差異，雖然 MainTex 像素完全相同。UE `bItemDraw` 分支對缺少 ColorMask 使用黑色，三色 lerp 在該分支不影響 MainTex；必須核對實際 draw inputs，而不能把來源色彩 metadata 一律改白。
4. 其他 MainTex、NormalMap、overlay 及材質值仍有原始差異。有些是讀回／量化、法線編碼或烘焙方式，有些尚未證明等價；報告不把它們當成全部已修好，也不把每項原始差異自動當成畫面 bug。
5. `uncovered_textures` 包含眼球 EyeDraw 的 highlight、normal_detail 等沒有從本次 diagnostic export 暴露的專用欄位。它代表**本次工具沒有覆蓋該輸入**，不代表 production 沒有該貼圖。

## 重現與證據

- `Tools/ok-extract/src/dump_outfit_audit.rs`：忽略式診斷 fixture，必須明確指定 `OK_AUDIT_GAME/CARD/OUTPUT/OUTFITS`；使用真實 `session::open`，不以 dump 資料餵入匯入器。`cargo test --lib dump_outfit_audit::export_runtime_outfits -- --ignored --nocapture` 完成六套匯出，exit0，845.13秒。這個測試的成功只表示匯出成功。
- `Tools/GamePoseProbe/audit_outfit_dumps.py`：先按來源領域、mesh/renderer 名、材質及頂點數匹配；head多材質拆分要求唯一材質身分。沒有依差異值選最接近的候選。歧義保持未驗證。
- `python Tools/GamePoseProbe/audit_outfit_dumps.py "C:/portable/KoikatuSunshine/mesh_dump/菲比v1.1.png" Saved/Tests/PhoebeOutfitDump338`。
- 完整結果：`Saved/Tests/PhoebeOutfitDump338/DumpAudit.json`；原始中間結果：`DumpAuditInitial.json`；六套 `outfit*/runtime.json` 及獨立匯出貼圖。
- 執行紀錄：`Saved/Logs/PhoebeOutfitDump338Export.log`、`PhoebeOutfitDump338AuditFinal.log`。`integrity_passed=true`，`all_dumps_match=false`。未進行新 UE 畫面或世界姿勢等價驗證，不能使用舊截圖補充成新圖像驗收。
