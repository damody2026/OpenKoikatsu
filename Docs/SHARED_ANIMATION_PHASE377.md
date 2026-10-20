# Stage377 共用動畫結尾與站立動作回歸

目標：原作 raw 動畫作為跨卡共用資源，循環、非循環播放都與獨立嘴型控制共用相位；擴大到男女站立動作，不以卡片或圖片名稱判斷。

修正：`UKKSharedAnimation.GetPhase` 統一 loop 的餘數相位及 one-shot 的結尾 clamp。骨架與 MouthOpenCurve 都呼叫同一函式；Loop 可在共用資產 Details 編輯。非循環動作結束後，嘴型曲線停留在同一結尾相位，物理仍使用真實經過時間。資產 Duration 與 authored payload 不一致時拒絕播放；切換失敗保留原本 actor 的動畫、時鐘與嘴型。

反例：曲線在 Duration 時開合為 0.2、Duration+0.2 時為 1；先前 one-shot 的骨架停住而嘴型繼續張開。新測試要求實際 morph 權重停在 20% 並扣除匯入 baseline，不只檢查時間變數。

驗證計畫：完整 Editor 編譯、男女站立 raw clip 匯入數量與無角色引用、原生 palette/mouth geometry 對照、兩種卡共用同一站立 clip 的無遊戲隔離播放、one-shot 結尾實際 morph、錯誤 metadata 拒絕及原 player 保留、原有行走回歸。全部採用全新輸出，Stage376 seal 與其 snapshot 保留。

驗證完成：BuildR3 actual0；ImportStandR1 保存女性 f_tachi_00_00（17.466667秒）、男性 m_tachi_00_02（5秒）兩份共用資源，character_references=0。

- DetachedStandR4／DetachedWalkR2：隔離 host AssetImport377/DetachedR2，無 pack／Tools／原卡或 source root，男女不同卡共用同一曲線，80 SK、loop／seek／pause、實際嘴型結尾20%及 metadata 拒絕／原 player 保留均成功。
- FemaleStandR4／MaleStandR3：原生 palette 五個時間點（含回跳、跨循環）與嘴型 geometry 成功；最大 matrix 元素誤差 0.000099838／0.000545382，原0.01門檻不變。嘴型頂點位移0.854885340／0.911619782公分，骨骼姿勢不變。這是 palette parity，並非全部原生頂點距離或 GPU 像素審查。
- 女角長同步原生 CPU oracle 用 -LongOracle（noheartbeatthread），外層仍有10分鐘timeout；該配置不改求解、取樣或誤差門檻。先前 exit1 無完成紀錄的根因未確定，不將該配置寫成已證明的根因修正。
- PoseParityR1：既有 portable fixture 四個 delta、92組私有 palette 逐值相同；pose-only後按需重算29066 CPU頂點與法線完全一致，不增加物理時間。
- RustR2 損壞資源／重開取樣、LegacyR2 舊 APNG 相容性通過。測試 log／receipt 前綴為 Shared376Stage377，站立匯入／build／Rust log 為 Shared377。
- 封存 Saved/Tests/AssetImport377/R1.audit.json，由 Tools/seal_shared_phase377.py 產生並檢查 Stage376 的821個已封存檔案。事故及停止的 runs 保留 ERROR_LOG E255。

本輪驗收涵蓋兩張卡outfit0、男女行走及站立 raw clips、循環與非循環嘴型；不代表全部卡／controller graph／Shipping 已驗收。

共用播放每次物理步進原本仍 clone 原始 private mesh（包含貼圖）並重算 CPU 頂點。新增只更新 palette 所需的私有骨架與 DynamicBone 狀態，不改 placement、implant、collider、240-frame settle 或時間步進；CPU 顯示輸出略過，由 Unreal 做蒙皮。以 session 的共用資源身分判斷，不切換全域環境。明確呼叫 Skin 時，以目前求解狀態重新輸出 CPU 幾何，不增加物理時間。無 serializer／既有 APNG 格式修改。女性17.466667秒站立的完整隔離測試本體約58秒；此前未完成的CPU-render run不算可比較的完整基準。
