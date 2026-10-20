# 1901 舞台白光修正（2026-10-21）

`koikatu_cs0001901` 右下方原本不應出現的大片白光已消除，恢復舞台底部；保留原作指定的曝光、相機、Bloom 與景深。

![上排修正前，下排修正後；每排左原作縮圖、右 Unreal 實際擷取](images/OKStudio/Lighting1901_20261021/comparison.png)

## 根因與通用修正

地圖共用同一個 mesh 的多個 GameObject，卻有不同的啟用狀態。舊 `ModelView` 將網格複製到所有 hierarchy 接點時，只保存 Renderer.enabled，遺漏每個實例的 GameObject.activeSelf／activeInHierarchy；另外也略過 Animator 的 m_IsActive 曲線。結果原作已停用的舞台光束仍被畫出，再經正常後製擴散成大片白光。

- `ParsedBundle::prefab_renderer_visibility` 保存逐接點的靜態啟用狀態及控制器 settled state 起始影格的開關曲線；Controller／Clip 引用依 FileID 和所屬 serialized file 解析。
- 沿父子階層計算 activeInHierarchy；子物件開啟不能越過關閉的父物件。同網格的其他實例保留自己的狀態。
- `SunLightInfo.visibleList` 的根物件維持可切換資格，交由既有 `_OKSunVisible` 按地圖時段控制。
- 逐實例寫入既有 `_OKRendererEnabled`，沿用正式渲染隱藏路徑。沒有場景編號／光束名稱／shader 名稱的正式程式特例。
- 90 個共用 Studio 模型已重新記錄資料；Session cache revision 提高至 18。既有 Studio 場景重新開啟便會讀取更新的模型資料。

只補 Animator 曲線的中間版本仍有白光，已透過正式出圖發現並修正；該輪不是最終驗收。診斷中的全 shader 隱藏也沒有作為正式方案。

## 驗收

- Rust：420 PASS／468 ignored；Editor 與一般遊戲 Development 建置成功。
- 六項 Studio 自測全部 PASS（GameSelfTest、PatternTest、EyeSetTest、BloomTest、TransparencyTest、EditorSelfTest）；透明度驗收包含101張圖片。
- 七項材質／陰影／Studio姿勢 Automation 全部 Success。
- 14／14 場景實際擷取成功，PNG完整性與雜湊全部通過；已逐張檢視對照，包含3167透明度、6331姿勢及6964地圖時段回歸。
- 純追蹤資源副本同步至 `e6299a4a4`，665個LFS payload（8,636,436,776 bytes）逐一符合HEAD OID，617個模型皆為追蹤檔；最後git status clean。

- 原作與完整共用庫的 1375 個 renderer 接點可見性向量完全一致；純追蹤資源庫重跑亦一致。實際 Session 的停用光束被關閉，其他共用材質實例仍保留。
- 原卡 `kks-verify`：完整庫／純追蹤資源庫各 MATCH。OK 卡 `okcard-check ... live`：兩庫各 1 MATCH／0 DIFFERENT，recorded、closed、live digest 相同，`gaps=0`、`game_reads=0`。
- 合併主線後Editor／Game再次建置成功，1901重新擷取1／1；修正前後camera trace完全一致；最後GameSelfTest再次PASS。
- 固定舞台底部區域（640×360 渲染座標 `[505,320,545,350]`）中，RGB 三通道皆 ≥245 的像素比例：修正前 99.92%，修正後 0%；原作縮圖該區域亦為 0%。這只是白光回歸指標，不代表逐像素吻合。

真實圖片、SHA-256、PNG 尺寸、IEND 後無額外 payload 的檢查與區域量測見 [manifest](images/OKStudio/Lighting1901_20261021/manifest.json)。

## 範圍

本次修正錯誤顯示的停用實例及其後製白光。角色髮色、膚色、陰影及景深與原作縮圖仍有差異，沒有宣稱全場景逐像素一致。完整14景序列中3800仍有既有的過度景深模糊（舊 `Verify-HandoffEAutoEvidence` 的同序列亦可重現）；14／14指成功擷取及檔案檢查，不能當成每張圖的所有顯示細節都正確。這裡的 Animator 支援沿用地圖載入時 settled state 的起始影格，沒有將它宣稱為任意動態 Animator／腳本的完整播放。

本機證據：`Saved/OKStudio/Verify-Light1901Release/`、`Verify-Light1901FinalCompare/`、`Verify-Light1901FinalSelfTests/`、`1901-accepted-source-oracle.log`、`1901-strict-clean-oracle.log`、`1901-accepted-rust-suite.log`、`1901-clean-tracked-verification.json`、`1901-final-validation.json`。診斷失敗與修正保留在 `Docs/ERROR_LOG.md` 及相應本機 log。
