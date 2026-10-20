# E297：C010 動畫 CPU 分段 profile

2026-10-05，使用者要求先 profile，並要求持續播放整體平均不超過 13 ms。本輪先完成量測；**目前沒有達到 13 ms，不能宣稱效能驗收完成。**

## 方法與證據

正式專案，CPU 模式，`output/CrowdAPNG/C010.png` outfit0，1600×1000，遊戲原始 walk 動畫。乾燥連續播放20秒，`OK_ANIM_PROFILE=1`；測試過程沒有同時編譯或執行其他測試。不是以降低動畫更新頻率、停止物理、關閉衣服或改成 T-pose 取得數字。

- 正式有效量測：`Saved/Tests/Anim297ProfileR2/report.txt`，valid=1，影像檢查通過。
- 原始 UE 記錄：`Saved/Tests/Anim297ProfileR2/run.log`；Rust：`native.stderr.log`。
- 分析結果：`Saved/Tests/Anim297ProfileR2/profile_summary.json`。
- 工具：`python Tools/analyze_animation_profile.py Saved/Tests/Anim297ProfileR2`。
- C++ 分段只取 probe begin/end 之間；Rust 分段為整次程序（含預備取樣），不可把兩者相加當作精確 frame 分解。各個 nested 階段不能重複相加。

## 整體結果

1122次角色更新，20.012508秒：

|指標|ms|
|---|---:|
|整體 frame wall-clock 平均|17.836459|
|角色 Tick 平均|14.852927|
|角色 Tick 最大|141.964402|
|角色 p50 上界（1ms直方圖）|15|
|角色 p95 上界|17|
|角色 p99 上界|21|

34ms以上4次，50ms以上3次，100ms以上1次。來源到顯示抽樣28020頂點，誤差0cm，動畫時間持續前進。直方圖固定256格，不隨播放時間增長；末格包含所有 >=255ms，該格不能當作有限上界。

## 平均成本在哪裡

|階段|平均ms|說明|
|---|---:|---|
|Session.Skin|5.071287|整個 CPU 網格更新入口|
|CPU 網格複製／準備|2.874118|Rust skin_all內部，包含許多非運算輸入的clone|
|CPU 幾何蒙皮與父變換|0.471301|skin_all內部真正的變形計算，不含私有rig另一條路徑|
|Unreal骨架姿勢建立|0.069000|很小，不是目前主要瓶頸|
|Unreal骨架姿勢套用|0.000249|CPU模式的這條路徑幾乎沒有工作|
|附件程序網格更新|4.417471|來源檢查、繪製頂點準備和UpdateMeshSection，含各材質section|
|動畫pose reset|0.222888|Rust基準骨架還原|
|動畫取樣／binding解析|1.633365|包含TOS、rig路徑、名字表、binding定位，不只是曲線插值|
|動畫後形狀／ABMX修正|1.135754|依來源遊戲順序執行|
|身體／衣服／其它物理|0.110641 / 0.619132 / 0.843457|不可把物理直接停掉當作修復|
|陰影提交|0.629256|平均小；尖峰時也會受阻塞影響|

`Session.Skin` 5.07ms 大於 skin_all內部3.35ms，因入口亦包含共享資源的私有rig幾何刷新和其它工作；不能把差值直接當作單一已證實函式成本。

### 尖峰

probe開始附近幀233：pose114.976ms、shadow26.986ms，共約141.965ms；其中骨架44.473ms、附件16.321ms。幀234陰影39.578ms；幀235pose60.628ms、shadow20.762ms。主要慢幀集中在開始附近，並非每五秒固定卡住。

這證明一次慢幀牽涉多個階段，尚不足以認定是某個單一算法；需進一步用 Unreal Insights/配置器與render queue事件分辨 startup、分配器、排程與提交等待。沒有將這次最大值刪除或把 p99 當成「沒有卡頓」。

## 本輪已做、已驗證的共用改動

1. Duration直接查已載入metadata；控制器參數／同步layer的motion plan仍每次計算，不快取可能隨參數改變的duration。
2. Streamed curve載入時建立每曲線索引，二分找前後key；保留重複時間、階梯／非有限derivative處理。未排序資料退回原掃描。索引serde skip，OKCLIP01版本及序列化資料不改；共享資源安裝後重建索引。
3. 大型非morph／非eye／非sphere-normal renderer先對原生position、normal、tangent做bit-exact比較，未變不再逐頂點換座標；不使用epsilon或hash，不丟失累積小動作。快取有固定renderer大小；完整模型／shape重建時失效。
4. 新增固定記憶體慢幀直方圖、5–120秒可調probe、profile分段與分析腳本。

原始walk的關鍵幀插值掃描本身僅0.007935ms／索引0.001171ms（300次原始取樣測試），**這不是主要收益來源**。重複工作存在不代表它是大瓶頸。不要誤把整個1.63ms binding階段歸因於這段0.008ms查詢。

基線 `Anim297BaselineR1` 整體18.583941ms，actor15.763014ms；`Anim297ProfileR2` 整體17.836459ms、actor14.852927ms。兩輪皆開細項profile；改善有限，不聲稱已達13ms。

## 驗證與下一個瓶頸

`Anim297BuildR6.log`編譯成功。
Rust `Anim297IndexR2.log`：3項通過，包含原遊戲walk 300時間點逐值相等、重複時間／step測試與既有catalogue檢查。
`Anim297RustRegressionR1.log`：動畫相關37項通過、30項明確忽略、0失敗；duration、共享資源roundtrip與原有取樣測試均通過。
Unreal自動回歸结果以 `Saved/Logs/Anim297RegressionR1.log` 實際Result記錄為準。

優先順序由profile決定：減少skin_all不必要的整份資料clone，再處理附件多section資料填入／上傳，接著對binding路徑建立正確的rig生命週期快取。需要比對原位置／法線、形狀切換、嘴型與物理歷史，不能因平均需求改動source邏輯。13ms目標尚未驗收。

正式main SHA256 `CAEE6B84E2CDE80C6DFDC85697BBF3223649EFB0B9AE42ED002E2420AFB07921`。
正式plugin SHA256 `893FFE6A2B9D75B6341D2C05A6AD18D26ACE28D7A05B6209C820A47CDD8D0175`。
沒有重製APNG、擴張大型cache或刪除使用者資料。

Unreal `Anim297RegressionR1.log`已逐項確認8項Success：CompactProceduralDraw、SourceGeometrySnapshot、KksMouse、SharedDeformation、ConservativeTransport、Film、MaskParity、Scene。不是只以程序退出碼判定。
