# Unreal 的 KKS controller 1D／巢狀 1D 動作混合

Stage291 更正：本文件的 raw 原生數值比對仍有效；原先 UI getter／finite pose 未證明逐幀入口使用指定參數。漏傳參數的根因與完整播放驗證見 [UNREAL_ANIMATOR_RUNTIME.md](UNREAL_ANIMATOR_RUNTIME.md)。舊截圖保留作歷史紀錄，指定參數的最新截圖使用 Animator291 路徑。

Stage289，2026-09-29。這份文件接續 `UNREAL_ANIMATION_CATALOG.md`、`UNREAL_BODY_DYNAMICS.md`。

## 根因與來源

舊 controller reader 的 `single_motion` 只接受一個 motion node，實際遊戲的多 clip 狀態因此被排除。這不是角色卡比例或附件骨架的問題。取第一個 child clip 會失去參數權重、時間與 override 身分。

獨立 UnityPy 讀取 action/animator/00、studio/anime/00、studio_k/anime/04：139 個 controller、805 個多 motion 狀態；分支 type0 共 907 個、type3 共 168 個。數字包含所有 state-machine arrays，**不代表目前 runtime 可選清單的筆數**，也不代表已支援 type3。

來源證據：

- `kks_decomp/Assembly-CSharp/ChaControl.cs`：AnimPlay、syncPlay、SetFloat 與直接指定 state 的入口。
- `kks_decomp/Assembly-CSharp/Studio/OCIChar.cs`：LoadAnime 的 controller 參數檢查、height1／height 初始化優先序。
- `Saved/Tests/Animator289/ControllerAudit.json`：完整 serialized 常數、TOS、layers、defaults 與 multi-motion states。
- 原生遊戲 Animator 的 `Play(normalizedTime)`／`Update(0)`、`GetCurrentAnimatorClipInfo`、`AnimatorStateInfo.length` 與實際 local TRS 控制檔。

官方結構說明：[Unity 1D blending](https://docs.unity.com/en-us/engine/6000.6/manual/animation-section/animation-mecanim/animation-animator-controller/animation-state-machines/animation-blend-trees/blend-tree-1dblending)。時間與姿勢是否一致以本機原生控制檔驗證，沒有僅依文件推定。

## 實作契約

1. 保留 bundle、controller、full state path 與 original／override PPtr 身分。Null override 保留原 clip；missing／external PPtr 不重新編號或跨 controller 搜尋。
2. 由 `m_BlendTreeConstantIndexArray` 找 motion set，從 node0 按 `m_ChildIndices` 遞迴。查 `m_BlendEventID`、controller float defaults、TOS 名稱與 `m_ChildThresholdArray`。
3. 1D 參數在相鄰 threshold 之間線性分配權重；範圍外取端點。巢狀 node 乘上父權重。同名參數在不同分支的範圍取聯集。
4. 當前有效長度為實際 leaf clip 長度的加權和；state speed／cycle、leaf cycle 與 loop／end-hold 都參與時間計算，各 leaf 以相同 normalized phase 取樣自身長度。
5. 每個 leaf 都從卡片未模擬 baseline 取樣。Position／scale 加權，quaternion 先選相同 hemisphere 再加權正規化；重新建立 world transforms。不是把某卡的臉／頭髮縮放套給其他卡。
6. 混合結果交回既有通用 `ApplyAnimFrame`：source shape／hand／scene helpers／ABMX／Ver02／衣物／GPU palette／私有髮與配件。沒有在物理之後另外覆蓋顯示骨架。
7. Studio scene 載入時依 **整個 controller 的 float 參數**，先找 `height1`、再找 `height`，取角色的 `shape_body[0]`。一般 action 保留 controller defaults；手動指定 Studio scene 同樣遵循該初始化。這不是只看 active tree 的名稱硬塞卡片值。

拒絕循環／過深樹、無效 child／clip、非有限或未嚴格排序的 threshold。現階段接受 leaf `m_Duration=1`；不支援的 time-scale／mirror／foot IK／2D／額外 layers 不冒充單一 child。Raw single-clip loader 對 composite 回 None，真正取樣由 motion evaluator 完成。

## Unreal API 與操作

- `AKKPreviewActor::SetGameAnimationFloat(Name, Value)`，Blueprint 可用。
- Rust C ABI 新增 `anim_float_count`、`anim_float_at`、`set_anim_float`。查詢值為 current／min／max。
- Gallery 從當前 controller tree 產生參數滑桿；名稱與範圍由來源資料提供，換 state 或卡片重新載入參數。
- 修改參數後重算 duration，保留 normalized phase，使用 dt=0 立即更新姿勢；不推进表情時鐘或粒子歷史。
- 未知名稱與 NaN／Inf 先拒絕，保持原姿勢與參數。API 只接受當前 tree 的 blend float，不宣稱已提供 bool／trigger／MotionSpeed／layer weight 等完整 Animator 控制。

操作：選「控制器狀態：action/animator/00」，載入該來源，選 `base / Base Layer.Main.Locomotion_Anger`，拖 `Speed`。巢狀 tree 會產生其實際參數列，無需按卡名編寫 UI。

## 驗收方式與證據

原生控制是五張 dump 對應卡片，各測三個真實狀態、五個參數值、四個 phase，共 60 frames/card、300 frames：

| 狀態 | 參數 | 額外條件 |
|---|---|---|
| Base Layer.Main.Locomotion_Anger | Speed | 真實步行 1D |
| Base Layer.Twoshot.Twoshot_00 | height | 不同長度子 clip |
| Base Layer.Lesbian.Lesbian3 | height1 | 巢狀 1D，第二參數 height=0.37 |

最後一列只匯出骨骼數值作巢狀樹控制，公開截圖使用第一列步行動作。

- 每 frame 450–496 個去重 cf transforms，數量依卡片／mod hierarchy，不強迫所有卡相同；Rust 比較實際 animated bindings 的 81,000 個 TRS **屬性樣本**（不是 81,000 個完整骨骼，也不是全部未動畫 transforms）。另核 leaf clip 名称／權重、有效長度。
- 位置最大分量誤差 1.4901161×10⁻⁸ m；quaternion 最大分量誤差 1.7881393×10⁻⁷；scale 最大分量誤差 1.1920929×10⁻⁷。原 TRS tolerance 2×10⁻⁵ 保持不變。
- Rust controller 2 項、catalogue 15 項回歸通過；原 native graph test 1 項含五卡，不把重跑次數加成額外測試。
- Unreal 13 種 Automation tests 全部 Success／QueueEmpty13／exit0。新增 `Animator1DBlends` 五卡各三狀態×30 frames，共 450 個 finite playback frames；原生 parity 另外證明公式一致，有限值本身不能當等價證據。
- 既有 body dynamics 與 external body dynamics 各五卡×180 frames 負對照仍通過；sparse／invalid 外部 clip、嘴型／眉毛／眼睛／表情時鐘與視線回歸一併通過。
- 真 RHI 五卡×全身／正臉／側臉共 15 張 900×1080，使用 Speed=0.37、Studio scene4、time0.4、mouth8/open0.8；71 個附件 display audits 誤差0cm，15 camera fits≤0.92。頭髮／頸飾跟隨與表情保持原通用路徑。
- Gallery 1440×1000 截圖與 source／state／slider 三個真實 callback，參數0.37、phase 保留、face clock 不變。

Unreal 動畫測試使用 UIAccepted build。目視後另發現 Studio 缺側牆，修共用背景再用 BackdropAccepted build 重拍 RHI；它沒有修改動畫公式。舊黑背景圖與原 capture log／sheet 在 `Saved/DebugShots/Animator289_Walk_open/old/BeforeSideBackdrop`。

統一檢查工具 `Tools/GamePoseProbe/verify_animator.py` 會核上述 gates、native coverage、finite durations、實際 output path 與側臉上緣 neutral-gray 背景，並保存 SHA256：

- `Saved/Logs/Animator289Audit.json`
- `Saved/Logs/Animator289Verified.log`
- `Saved/Tests/Animator289/ContactSheet.png`
- `Saved/DebugShots/Animator289_Walk_open`：最新15圖。
- `Saved/DebugShots/Animator289UI/gallery_imported_studio.png`：真實 UI callback 圖。

## 重現

在專案根目錄 PowerShell：

```powershell
& 'C:/portable/OpenKoikatsu/Tools/GamePoseProbe/build.ps1'
& 'C:/portable/OpenKoikatsu/Tools/GamePoseProbe/run_animator_native.ps1' -Cards All
```

Rust commands 的 working directory 是 `C:/portable/OpenKoikatsu/Tools/ok-extract`：

```powershell
cargo test --lib native_animator_one_d -- --ignored --nocapture
cargo test --lib anim::controller::tests -- --nocapture
cargo test --lib anim::catalogue_tests -- --nocapture
```

正常編譯本專案後，使用最新 build 跑 Unreal：

```powershell
& 'C:/portable/OpenKoikatsu/Tools/GamePoseProbe/run_animator.ps1' -Mode Tests
& 'C:/portable/OpenKoikatsu/Tools/GamePoseProbe/run_animator.ps1' -Mode Shots -Cards All -Blink open
& 'C:/portable/OpenKoikatsu/Tools/GamePoseProbe/run_animator.ps1' -Mode Gallery
C:/portable/OpenKoikatsu/Tools/lua/lua.exe 'C:/portable/OpenKoikatsu/Tools/GamePoseProbe/verify_animator.lua'
```

原生 runner 會暫裝自己編譯的 probe、保留舊控制檔、依 owned exact PID 清理，再移回 Tools；不寫角色卡與遊戲設定。禁止 probe 尚有 owner 時替換。正式重拍前先以 absolute／LiteralPath 封存舊15PNG與log，不讓不同 build 的圖混在同目錄。`Saved/` 控制資料保持本機，不搬 66MB graph audit 與大量骨骼 JSON 進 Docs。

## 尚未涵蓋

這次完成 explicit state 的 1D 與巢狀 1D。Automatic transitions／CrossFade、type3 2D、mirror、foot IK、額外／additive layers、動態 state-time 參數、HAnime breast/layer 初始化完整流程仍需來源控制與實作；任意第三方 physics overrides／跨角色碰撞也沿原範圍。圖像不是遊戲逐像素等價證明。下巴輪廓線依使用者要求暫不處理。

操作與編譯錯誤、無限長度原生控制、錯誤 state path 測試及黑背景根因見 `Docs/ERROR_LOG.md` E-145。

Stage290 的 Cartesian type3 與單個 explicit Override mask layer 後續實作，見 `UNREAL_ANIMATOR_2D.md`。上方 stage289 的「2D／額外 layers 未支援」描述是當時狀態，不包含後續已加入的有限範圍；automatic transitions／多 active layers／additive／IK 等限制仍保留。
