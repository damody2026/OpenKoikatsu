# 場景元件共通製作流程

完整操作、重跑命令、整棟接合及驗收方法見根目錄 [model3d.md](../../../model3d.md)。S01三層完成模型位於 `output/street-social-city-production/S01-modular-components-v1/assembly-building/`。

依使用者要求，適用於自行建構、Tripo 生成及複用的元件。

1. 優先複用已驗收元件；可用長方體組成的結構以 Blender 程式精確建立，曲面或複雜造型使用 Tripo Model。
2. 原始模型不可覆蓋。記錄來源、尺寸、每件三角面上限、貼圖解析度、局部座標、可動部件與擺放數量。
3. 每個新資產版本必須經過 `E:/Paper/instant-meshes-rs/target/release/instant-meshes.exe` remesh。已完成並驗收的相同版本可複用其 remesh 產物，不重複處理每個實例；改動幾何後須重新處理。
4. 程式只接受 OBJ／PLY／FBX 幾何，會忽略 UV、材質、動畫。逐部件處理，避免把門片、車廂與其他可動部件焊死；回 Blender 還原 UV／材質並檢查外觀。蒙皮與動畫轉移尚未實作，不可宣稱保留。
5. 指定面數不是硬上限；驗收需實際三角化計數。檢查包圍盒、雙向表面誤差、薄板厚度、閉合模型非流形邊、部件數、貼圖、比例及碰撞／可行走淨空。
6. 未通過 remesh 驗收的版本禁止進入正式組裝。保留原檔與被拒候選、指令、執行檔雜湊及失敗紀錄；修正後重跑驗收，不可用原檔冒充已 remesh 成功。

## 批次接合程式

`Tools/remesh_street_city_asset.py` 由 Blender 執行，可輸入靜態 GLB，逐網格轉 OBJ、呼叫指定 Rust 程式，再轉回 GLB。UV 用最近來源三角形重心插值轉移，仍需人工外觀檢查。`remesh-report.json` 的通過狀態及 `adopted` 必須分開；目前不會自動採用候選。

## 書架首次實測

書架由9片木板組成，原始396三角面。2026-10-13 以三角模式、硬邊30度、邊界對齊、4000面上限測試，Remesh 後部分邊緣偏移約10mm，來源頂點至結果最大誤差約40.5mm，超過5mm驗收容差，拒絕採用。薄板及倒角不能僅憑工具返回成功判定合格。

完整紀錄：`output/street-social-city-production/S01-modular-components-v1/board-bookshelf/remesh/remesh-report.json`。已生成但尚未下載的 Tripo 元件尚未跑 remesh，不屬於合格組裝資產。

## 矩形薄板分支（2026-10-14）
`--box-panels` 僅供來源經抽樣確認為完整實心長方體的獨立板件。先條件化成均勻比例立方體，再以指定 Rust 執行檔 remesh，最後恢復硬平面及原尺寸；保留 remesh 拓樸。這個流程會去除微小倒角，需雙向表面誤差檢查，不可套用到曲面家具。新版書架432面通過，原始及失敗候選均保留。`Tools/review_street_city_local_components.py` 會重新匯入 GLB，焊接僅用於拓樸檢查，驗證尺寸、正體積、閉合、材質、UV、2K與實際面數。

## 曲面特徵修復與實際匯出驗收
1. `prepare_street_city_generated_component.py` 校正軸向、包圍尺寸、落地原點，下載原檔不覆蓋。位置焊接與每角UV分開處理，保留2K貼圖。
2. `remesh_street_city_asset.py --project-source --dense-pass` 執行指定Rust程式，將密集輸出降至執行面數，再投影及修補來源支持的小洞。包圍尺寸與雙向表面抽樣須在5mm內。來源閉合時結果也必須閉合；來源開口需另列，不可宣稱封閉。
3. 遇到曲面細部被Rust抽取遺失，可顯式選擇 `--recover-source-topology`：先保留Rust結果，再採用來源連接拓樸重新建構，僅採用距來源5mm內的remesh位置場，位移限制1mm，缺失特徵由來源恢復。這是來源拓樸修復，並非純Rust輸出拓樸；報告必須列出特徵恢復前誤差、來源及使用頂點數，保留所有中間檔。
4. `review_street_city_generated_components.py` 重新匯入最終GLB驗證尺寸、面數、UV及貼圖，列出開口邊並渲染實際外觀。不可把來源開口網格當成封閉碰撞體。
5. `assemble_street_city_modular_f02.py` 僅載入通過且來源雜湊一致的資產，實例重複計入10萬三角面上限。`accept_street_city_modular_f02.py` 重新匯入整層GLB，檢查走廊完整站立體積、抽樣樓板連續性、門洞、樓梯踏面及淨高。
6. `publish_street_city_f02_review.lua`（`Tools/lua/lua.exe` 執行）僅在上述完成後發布HTML、合併元件驗收並設定採用狀態。重跑變更後須重新驗收；Blender返回碼不足以判定成功，必須讀取 `run_street_city_blender_task.py` 的 succeeded 狀態。

本次S01 F02結果：16種基本元件，93,711三角面，1,442項實際匯出幾何檢查通過。衛浴經來源拓樸修復；椅子與馬桶保留來源開口。遊戲控制器、角色導航及完整一樓模型不在此驗收範圍。
