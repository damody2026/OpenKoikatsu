# 街區女角的動作（原作動畫全接入）

2026-10-24。街區原本只有約 50 個手選動作（上限 64）。這次接入原作所有非 H 的 Studio 動畫，並讓這些動作會出現在三個地方：
- 對話時的手勢由 DeepSeek 選。
- 日程與閒逛時的待機動作依地點選。
- 合照可以換姿勢。

## 動畫庫

- `Tools/export_street_animation_library.lua` 從 `base_v1/studio_anims.json`（原作 Studio 全部動畫）取出這些群組：キャラ、スタジオ、アウトドア、サマー、汎用、アフタースクール、EC、サンシャイン、バトル（戰鬥，供惠惠詠唱這類活動用）。不取的有：H 群組、ライブ、走路與跑步（街區有自己的移動動畫）、T-pose。
- 每個 Studio 狀態以 `ok-extract --export-runtime-clips` 匯出成街區用的 `OKCLIP01`，放在 `Content/IndependentResources/RuntimeAnimations/library/lib_<群組>_<分類>_<編號>.okanim`。共 873 個（其中バトル 278 個）、約 332 MB，以 Git LFS 存放。
- 一個狀態若依身高有 `_S/_M/_L` 三個片段，取 `_M`；混合樹（blend tree）不匯出。目前只有 1 個沒匯出：`lib_17_14_5`，它播兩個片段。
- `library.manifest.json` 記下每個片段的姿勢（stand / chair / floor / lie）、性別、是否循環、長度、日文名與分類。
- `ok-extract` 的 `export_shared_clip` 現在接受 `states:` 控制器狀態鍵：狀態只播一個片段時，就匯出那個片段。
- 原作九種雙人合照姿勢（`f_twoshot_00_00..08`）都已匯出：`photo_female_00..08`，原本只有其中三種。

## 執行端

- `OKCrowdRuntimeScene`：admitted 動作上限由 64 提高到 2048。啟動時只讀基本曲線，動作曲線在第一次準備時才讀檔（`FScene::ClipBytes`），所以幾百個動作不會拖慢啟動，也不佔記憶體。
- `prepare_street_social_selection.lua` 把 `library.manifest.json` 與 `photo_poses.manifest.json` 加進啟動清單。

## 誰決定播哪個：`rules/motions.toml`（ok-rules `motions.rs`）

- `[[gesture]]`：她說話時的手勢，34 個鍵，例如 wave、shy、apologize、proud、cheer、sulk、dance。每個鍵對應站姿片段，部分另有坐姿片段。DeepSeek 在回覆 JSON 的 `gesture` 欄位寫鍵名，三語的 free-talk prompt 都有【肢體動作】段落說明各鍵的意思。C++ 的 `PlayGesture` 問 `motion_gesture` 拿片段，再用 `StartLoverReaction` 播幾秒。只在面對面、不在活動中、距離 4 m 內時才播。
- `[[life]]`：日程把她留在原地時的循環動作，依 kind（meal、class、study、work、rest、leisure、sleep）與地點選片段。同一個日程時段固定同一個（`LifeClip` 以「角色｜kind｜地點｜開始時間｜日」為種子快取）。`seated` 會一起擺出椅子，`table` 會擺桌子（`ReadMotionTable`）。
- `kind = "ambient"`：她自己在觀景點或長椅停下時做的動作，依所在地點選，例如公園伸展、看天空，街上靠牆、整理頭髮、張望等人。這是 `TickAmbientActivity` 的第 5 種，與手機、飲料、自拍、整理頭髮輪流出現。
- `[[photo]]`：九種雙人合照姿勢。開始合照時隨機選一種。合照中按「換個姿勢」，或她回覆 `gesture: "another_pose"`，就換到下一種：她先放下姿勢，準備好新姿勢後，兩人再從同一錨點開始（`SwitchPhotoPose`）。

## 效能

- 動作準備（pose bank）在背景執行緒進行，最多兩個同時準備，與原本相同。遊戲執行緒只多了第一次讀約 0.2–0.9 MB 的檔案。
- 手勢與待機都經過原本的串流動作欄位，同一時間播放的動作數沒有增加，所以不影響維持 120 FPS 的目標。

- 實測（公園同一視角，機器上沒有其他遊戲在跑）：舊版 Int1024C 176 FPS。新版剛接入時 106～125 FPS，原因是動作查詢用線性比對 651 個 id；改成雜湊查表（`FScene::FindClip`）後為 160 FPS。剛開遊戲角色還在載入時會掉到 14～28 FPS（舊版載入期間未量）。
