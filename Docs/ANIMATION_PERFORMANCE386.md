# Stage386：物理子步共用碰撞器幾何

2026-10-03，正式 BuildR1 actual 0 / Succeeded，主專案 DLL 已更新。

## 調查與通解

Stage385 的配對 CPU 更新仍約45–46ms，不能把一次body collider準備當成所有物理成本都已消失。本輪新增預設關閉的 `OK_ANIM_PROFILE=1` 附件分段；ProfileR1為未改collision算法的真卡測量：同期sync約0.008–0.042ms，較大的附件physics區段約1.5ms（包含attachment placement／配置／solver），而非全由骨架名稱搜尋造成。ProfileR1只測Rust API，不能和Unreal的rest直接當同一workload。

在 `UpdateParticles2` 中發現每個粒子都重算同一external collider的球／膠囊端點、方向向量與平方長度。新增局部 `PreparedCollision`：每次子步、每個external collider準備一次，依原ordered collider list作用於每個粒子。scaled radius先保留乘法結果，再加每粒子radius，原interior／endpoint分支、squared-distance predicate、sqrt及位置修正運算順序不變。

依 `C:/portable/KoikatuSunshine/kks_decomp/Assembly-CSharp/DynamicBoneCollider.cs:49` 的 Collide／Sphere／Capsule 與 `DynamicBone.cs:276,284` 的 UpdateParticles2 → ApplyParticlesToTransforms 順序：粒子更新期間不寫Transform locals，ApplyParticles才寫回。準備資料只存活在一次UpdateParticles2呼叫內；每個chain開始前既有body-space／implanted collider刷新仍保留，各子步重新準備。沒有跨幀、跨鏈或跨角色共用particle state，也沒有因renderer名稱相同就合併solver。

authored collider路徑仍走原逐次計算；Ver01／ordinary／Ver02流程、update-rate累積與skip、gravity／freeze axis／碰撞順序／暖機／時鐘／IK／陰影頻率不改。沒有持久化結構或ABI更動。

## 回歸

- ContactsR1 actual0，2 tests passed：215,622組grid contacts＋額外端點／接觸邊界，sphere／capsule、inside／outside、XYZ、不同particle radius、負／零縮放，prepared與保留的原完整per-particle算法 exact。Stage385名稱綁定邊界亦通過。
- DynamicsR1 actual0：31 passed、13 installed tests按原規則ignored；不是宣稱ignored皆執行。所有測試中的external contact都逐次對照原算法exact。
- InstalledR1 actual0、2 passed：真卡21 private solvers ×4 flags，84cases collider pose／順序／freeze axis／offset及step後particle pos／prev exact；有效／無效clip、floor flags＋delta0、explicit reset生命周期回歸。
- ProfileR1 actual0：改solver前的分段測量與真卡舊binding oracle；完整日志保留。
- Perf386PairR1：正式DLL男女667／681bones、原walk、幾何與world oracle、兩個native配對選项各30frames／共同時鐘、背景準備期間舊配對持續更新。
- Perf386ControlsR1 actual0／AutomationSuccess：KKS七mask、wheel／owner／gesture／clock／generation cancel。

## 性能與驗收範圍

| API測量 | Stage385 PairR1 | Stage386 PairR1 |
| --- | ---: | ---: |
| 第一個commit | 305.412ms | 351.242ms |
| 第二個commit | 83.074ms | 83.841ms |
| 第一個配對30frames CPU平均 | 45.981ms | 44.040ms |
| 第二個配對30frames CPU平均 | 45.301ms | 44.180ms |

最終PairR1 actual0／AutomationSuccess。单角色quiet20ticks 24.951ms，双角色原walk60ticks 36.692ms。prepare21.713／12.858秒，第二次期間舊pair前進256frames，max102.081ms。提交時間未呈現改善，第一項甚至比上輪高；不能用播放更新略低掩蓋commit仍有波動。约44ms仍是实际CPU成本，未聲稱完全不卡或整體FPS問題都解決。

API CPU更新時間不是整體GPU／scene FPS、硬體GUIclick延遲或全卡／全姿勢無bug認證。診斷開啟會增加日志成本，quiet數值另列；只有單輪跨階段比較，不能當固定比例保証。每幀成本仍需按分段證據處理，不能只看總rest就刪掉物理或按卡名調整。

完整記錄位於 `Saved/Tests/AnimationPerf386/`、E264與HANDOFF3 §77。fresh `R1.audit.json`／snapshot保存源碼、正式Rust lib／DLL、logs與receipts，核對Stage385全部301項旧封存不變。
