# 1268 天使角度：動畫道具接點修正

驗收實作版本：`ff1cd4ffa`；比較工具播放狀態收尾：`2d3284930`（2026-10-21）。

`koikatu_cs0001268` 的兩個天使掛在「回転（obj無し）」道具 10/3/397 的 `N_setuzoku` 下。原作的 `N_studio_kaiten` Animator clip 旋轉其上層 `N_move`；角色 guide 的旋轉只是其中一層。舊版只取靜態 prefab 接點，且解析器丟棄道具的 `animePattern`／`animeNormalizedTime`，使天使的位置與朝向錯誤。

## 實際圖片

上排修正前、下排修正後；每排左側是原作場景縮圖，右側是 Unreal 實際擷取。修正後兩個小角色回到原作頭頂兩側的方向；沒有改相機，也沒有替 1268 加旋轉特例。

![1268 動畫接點修正前後](images/OKStudio/AnimatedAttachments20261021/comparison.png)

[完整修正後圖片](images/OKStudio/AnimatedAttachments20261021/after.png)；[修正前圖片](images/OKStudio/AnimatedAttachments20261021/before.png)；[圖片尺寸與 SHA-256](images/OKStudio/AnimatedAttachments20261021/manifest.json)。圖片均完整解碼，確認 IEND 後無 payload，沒有 okPv／okLc 卡片資料。

## 通用實作

- Rust 保留場景道具的動畫 pattern、normalized time 與 speed。1268 兩個旋轉道具保存的相位為 `311.40283203125` 與 `268.4969177246094`，不再歸零。
- `ok-extract <game> --oklib studio-child-anims <base_v1>` 從來源 Animator clip 與 prefab 階層烘焙動畫接點，寫入 `studio_child_animations.json`。本機原作共 32 種有動畫的子接點，32 種成功、0 種缺失。取樣率至少 120 Hz；TRS 組合前正規化四元數，移除 guide 已取代的 prefab root TRS。
- 執行時只讀共用庫。接點依保存相位、來源速度、經過秒數及 clip 的 loop／hold 政策取樣；支援旋轉、平移、scale、反向與零速。子角色、孫物件、gizmo parent frame 和重新掛接使用相同接點。
- 播放時每幀更新受動畫道具影響的子物件；timeline／sequence 使用指定時間，輸出影格不再額外推進一次時鐘。存檔保留來源起始相位，runtime elapsed clock 不寫入場景，重新載入歸零。
- 角色動畫移除 Studio 外層無條件 Fmod，讓 `.okanim` player 自行決定 loop／hold，並在建置角色後套用保存的姿勢時間。
- 比較工具固定保存時刻，等待 shader／renderer 完成時不再推進場景動畫。`OK_STUDIO_POSE_DUMP=1` 可記錄實際角色世界變換與 IK 目標／骨架位置。
- 新共用資料已宣告為 NonUFS runtime dependency。場景沒有可轉出的接點動畫或選用尚未轉出的非零 pattern 時，匯入報告列出 Note，保留既有靜態接點 fallback。

## 驗收

- 原作來源交叉核對：`OCIItem.OnSave` 保存 normalizedTime；`AddObjectItem` 在載入時以該相位 `Animator.Play`；來源動畫曲線綁定 `N_chousei/N_move`，不是角色根旋轉或攝影機。
- 原作曲線 oracle：9 個含非烘焙格點的相位，最大角度誤差 `0.0006905 rad`（約 `0.04°`）；1268 兩個保存相位通過。場景解析器另有保存相位／速度的回歸測試。
- Rust：417 PASS、463 ignored；兩個具名來源測試另行執行 PASS。
- 第一輪六項既有 Studio 自測全部 PASS（含 48 組透明材質、101 張 PNG、13 張代表圖）。
- 已提交原始碼的 Editor／Game Development 建置成功；`OpenKoikatsu.Studio.AnimatedChildPoint` Automation PASS，涵蓋來源旋轉方向、相位／速度／反向／零速、非循環 hold、平移插值、巢狀 scale／rotation、gizmo frame 與場景存讀。
- `Verify-Angel1268TrackedSelfTests`：已提交 `ff1cd4ffa` 的六項自測再次全部 PASS；透明材質 48 組、101 張 PNG、13 張代表圖完整性／SHA-256 驗證通過。`2d3284930` 在擷取完成後恢復原播放開關，Editor／Game 再建置並重拍單場景。
- `Verify-Angel1268CleanAutomation`：指定獨立 checkout 的 `OK_BASE_LIBRARY`，32 種接點及完整 Automation 斷言 PASS；該執行使用 NullRHI，不讀原作動畫 bundle。
- 新建稀疏 clone 以追蹤 LFS OID 驗證 665 個共用 payload（約 8.62 GB），沒有複製未追蹤角色或 mod 模型。既有 `E:/OpenKoikatsuClean` 的本機修改未覆寫。
- 乾淨庫與完整本機庫：各五張原卡 `kks-verify` 全部 MATCH。OK 卡的 recorded／closed／live 外觀雜湊兩邊五張均相同，`game_reads=0`；完整庫嚴格判定 5 MATCH／0 DIFFERENT、全部 gaps=0。乾淨庫為 4 MATCH／1 DIFFERENT：March_7th 測試卡的三個雜湊仍一致，但 `gaps=1`，沒有當作通過。兩版基礎庫的 Git 差異只有本次新增接點 JSON，此項既有卡片依賴缺口未在本次角度修正中改動。
- `okcard-check` 即使有 DIFFERENT 仍回傳 exit 0，本輪以各卡明確 verdict 與 gaps 判定，沒有只靠程序結束碼。

本機完整證據：`Saved/OKStudio/Verify-Angel1268Release/`、`Verify-Angel1268TrackedAutomation/`、`Verify-Angel1268TrackedSelfTests/` 及 `1268-*.log`。公開圖片不含角色卡。

## 支援邊界

本次修正的是來源動畫道具的子接點與其後代物件。沒有宣稱任意 mod 的多狀態／混合 Animator、動畫幾何、道具 FK 與動畫的所有組合均已支援。舊 `.okscene` 沒有保存過來源相位，需重新匯入原作 PNG 才能取得該資料。

當輪圖中的白色服裝與 Unreal 校服差異保留，當輪只驗收動畫接點角度。後續確認白色服裝實為來源 top28「バスタオル」，差異來自轉卡時自動交換服裝，修正與新圖見 [1268 服裝驗收](OK_STUDIO_CLOTHES_1268_20261021.md)；色調仍未宣稱逐像素一致。
