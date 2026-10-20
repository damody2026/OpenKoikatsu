# Unreal 動畫與嘴型目錄

## 已匯入資產

Content Browser 開啟 `/Game/Koikatsu/Data`：

- `DA_AnimationAndMouth`：總覽、各表欄名／原始列序、來源 SHA256，以及完整 `ANIMATION_AND_MOUTH.md`。
- 八張 `DT_*`：每列的 `Values` 保留原 TSV cell，`RuntimeSource` 保存遊戲根目錄相對的 bundle／ZIP entry 身分。空值、重名與第一列均保留。

| 資產 | 列數 |
|---|---:|
| DT_animation_sources | 116 |
| DT_animation_clips | 29,956 |
| DT_studio_catalog | 9,214 |
| DT_fbs_patterns | 2,492 |
| DT_fbs_channels | 3,227 |
| DT_mod_anim_bundles | 79 |
| DT_mod_studio_info | 4,980 |
| DT_mod_head_prefabs | 2 |
| 合計 | 50,066 |

九個 `.uasset` 位於 `Content/Koikatsu/Data`；不依賴被 git 忽略的 TSV 於執行期重新匯入。該資產目錄已加入 AlwaysCook。真正播放仍需遊戲及相應 mods 安裝，因原始曲線不在 TSV 中。

## 在 3D 畫面使用

1. 載入角色並進入 3D。
2. 在左側 Studio 分類選單選擇分類，再選動作。程式會以來源 ZIP／bundle、controller／override 與實際完整 state path 配對後播放。
3. 或選擇動畫來源，按「載入此動作來源」，再從原始動作選單選擇實際解析的 clip／state。
4. 用同一操作欄的嘴型選單與嘴巴開合滑桿調整。選單名稱來自目前角色真正載入的 FBS close/open channels，滑鼠提示可顯示完整名稱。
5. 左右手各自選姿勢 A、B 與混合；「跟隨動作」停用覆寫。KKS 使用原始 ShapeHandInfo 鍵，操作與遊戲正向控制見 `UNREAL_HAND_SHAPES.md`。

重複名稱保留為獨立列；來源或 state 有歧義時回報失敗，不任選第一筆。`animation_clips.tsv` 沒有 PathID，不能單憑裸 clip 名建立播放身分。現有 loader 仍用完整原始 clip label／state label，按需解析及取樣。

## Blueprint 查詢

使用 `KKAnimationCatalog.LoadDefaultCatalog` 取得總覽，然後：

- `GetRowCount(Table)`、`FindRows(Table, Column, Value)`、`GetCell(Table, Row, Column)` 查原始欄值。
- `GetAnimationSources()` 取得 base／male／mod 原始 clip 與 controller state 來源。
- `GetExpressionPatterns(Source, Prefab, Controller, TargetIndex)` 精確查某頭部目標的 FBS patterns。
- `Tables[].RowOrder` 是來源列序；`Columns[i]` 對應 `Values[i]`，不要依 DataTable map 枚舉順序判斷來源順序。

例如 base `chara/bo_head_00.unity3d`、`p_cf_head_00`、`MouthCtrl`、target 0 對應 43 組 pattern，第 0 組 close/open 是 28／29。mod 頭部以自身來源和 prefab 查詢，不套用這組 base 映射。

## 重現匯入與驗證

在專案根目錄用 PowerShell 執行；引擎路徑按本機安裝調整：

```powershell
Tools\lua\lua.exe Tools/anim_export/prepare_unreal_catalog.lua
& 'D:/UE5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' 'C:/portable/OpenKoikatsu/OpenKoikatsu.uproject' -run=KKImportAnimationCatalog -unattended -NullRHI -nosound
& 'D:/UE5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' 'C:/portable/OpenKoikatsu/OpenKoikatsu.uproject' -run=KKImportAnimationCatalog -VerifyOnly -unattended -NullRHI -nosound
```

prepare 可指定 `--input`、`--game`、`--document`、`--output`；commandlet 可指定 `-Input=完整JSON路徑`。先驗證全部輸入，再修改資產。`-VerifyOnly` 在獨立程序重載已存資產，逐 cell、欄序、列序、來源鍵、SHA256、全文 MD 核對，不寫資產。

Automation tests：`OpenKoikatsu.AnimationCatalog.ImportedData` 與 `OpenKoikatsu.AnimationCatalog.Playback`。後者需要本機 Alice 卡與指定 mod 資源；實際取樣 Studio walk 及 mod `L_WLoop1`，不是只核對 metadata。

## 本次證據與範圍

- `Saved/Logs/AnimationCatalogReloadVerify274.log`：50,066 列完整持久資產核對，0 errors／0 warnings。
- `Saved/Logs/AnimationCatalogAutomation274.log`：兩項 Unreal Automation tests 通過，包含實際 base／mod 動畫取樣。
- `Saved/DebugShots/AnimationCatalog274`：五張 dump 對應角色卡，匯入 Studio walk、mouth pattern 8/open 0.8，各全身／正臉／側臉。
- `Saved/DebugShots/AnimationCatalog274UI`：原生 Gallery 分類選單播放與嘴型名稱畫面。

本次完整匯入清單與文档，沒有將 29,956 列 metadata 冒充烘焙的 UAnimSequence。HandAnime 清單已保存；後續依實際 KKS 呼叫新增 ShapeHandInfo 手勢，詳見 `UNREAL_HAND_SHAPES.md`，不能把舊表推定為本版追加層。完整 Eyes／Eyebrow controller、Animator graph／blend tree／additional layers 仍須後續實作。圖像是 Unreal 驗證，未宣稱與遊戲逐像素相等。更多載入缺口見 `HANDOFF2.md` §130／§131／§132；操作與實作錯誤見 `Docs/ERROR_LOG.md` E-129／E-130／E-131。

## 後續實作狀態：Stage289（2026-09-29）

上段為 stage274 當時狀態。Eyes／Eyebrow／blink／表情時鐘與視線已有後續來源對照，見 `UNREAL_FACE_BLENDSHAPES.md`。普通與外部 Unreal 動畫都已接同一 source body／衣物物理，見 `UNREAL_BODY_DYNAMICS.md`。

Stage289 新增 explicit controller state 的 **1D 與巢狀 1D BlendTree**，保留 source／controller／override 身分、float defaults、threshold、child weights、normalized phase 與 Studio 身高初始化。Gallery 依真實參數產生滑桿，Blueprint 可呼叫 `SetGameAnimationFloat`。五卡原生300frames／81,000 animated TRS屬性樣本、UE13種測試與450混合播放frames通過；重現、完整證據與限制見 `UNREAL_ANIMATOR_BLENDTREES.md`。

Automatic transitions／CrossFade、2D、mirror、foot IK、額外 layers 與動態 state-time 參數仍未涵蓋，不再籠統說全部 BlendTree 未實作，也不把1D通過推定為完整Animator。原TSV持久匯入範圍不變。

## 後續實作狀態：Stage290（2026-09-29）

新增 Cartesian type3 與巢狀 1D→2D，並依來源完整 relative-path mask 組合單個 explicit Override 層及 authored default base。來源參數滑桿共用原 API，沒有卡名比例補丁。公式、原生控制、Unreal gates 與範圍見 `UNREAL_ANIMATOR_2D.md`；不宣稱 automatic transitions、同時多 active weighted layers、additive／IK／mirror／Directional 2D 已完成。

## Stage298 多active layer載入

新增 `states:layers@HEX` 包裝既有controller@／studio@ graph並選取多個same-base layers。每層權重透過 `OK.LayerWeight.N` 及既有Unreal float API，保留來源mode／path mask／clock，依layer index升序合成。詳見 `UNREAL_ANIMATOR_MULTILAYER.md`；TSV匯入內容與記錄數不變，沒有替controller自動猜active states。


## Stage299 每層起播時鐘

Non-synced層新增`OK.LayerStartTime.N`／`OK.LayerStartPhase.N`，Unreal game sampler保留未wrap elapsed，各state依来源loop／clamp；適用共用loader，沒有卡名特例。原TSV持久匯入清單與記錄數不變。詳細結果與剩餘trigger／transition範圍見`UNREAL_ANIMATOR_LAYER_CLOCK.md`。

## Stage300 來源trigger

明確選定獨立層新增來源type9／serialized transition驅動的瞬時self re-entry，Blueprint可列出目前可處理的triggers並呼叫SetGameAnimationTrigger。base／sync時鐘持續、權重0層仍重啟；沒有pending trigger queue或完整FSM。原TSV與資產筆數不變，詳見`UNREAL_ANIMATOR_TRIGGER_REENTRY.md`。

## Stage301 明確指定 base CrossFade

同一來源／controller 的 plain base states 新增 `CrossFadeGameAnimation`，依原生
normalized source duration、雙 state 持續時鐘、線性 progress 與 shortest nlerp
混合。global elapsed／表情時鐘保留，完成後修改 speed 保留目前 phase；尚未實作
active transition interruption、多層 CrossFade 或 automatic FSM。既有 TSV 資產筆數
不變，詳見 `UNREAL_ANIMATOR_CROSSFADE.md`；前文 Stage289 限制是當時狀態。


## Stage302 獨立 child layer CrossFade

明確多層 graph 的單一 independent child 新增 CrossFadeGameAnimationLayer。
先混合該層 raw／additive delta，再依來源 mask／weight／index 合成，其它層
clock、weight 與全域 elapsed 持續。來源採 normalized source length，新增
唯讀 transition source length；不按角色卡補償。原 TSV 資產筆數不變，詳見
UNREAL_ANIMATOR_LAYER_CROSSFADE.md。Concurrent／synced transitions 與完整 FSM
仍待；前述單 base 限制是 Stage301 當時狀態。

## Stage303 並行獨立 child transitions

多個獨立 child 各自 CrossFade，保留各自 source length／from/to clocks、mask、
weight 與合成順序，逐層完成。新增逐層查詢，舊 global source_length 在多個
child 同時 active 時返回0；caller 用指定 layer 查詢。同層 active interruption
與 base+multi／synced fades仍拒絕。TSV asset數量、pipeline141／decoded90保持。
來源與驗證：UNREAL_ANIMATOR_PARALLEL_CROSSFADE.md；錯誤E-159。Stage302的
concurrent限制是當時狀態，非目前功能判斷。
