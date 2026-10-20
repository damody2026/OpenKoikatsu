# Stage388：首次配對共用 prefab 準備

2026-10-03，正式BuildR4 actual0／Succeeded，DLL已更新。

## 分段證據與通解

Perf388ProfileR1 actual0／AutomationSuccess，改邏輯前active Idle首次commit1303.897ms；男女factory的load分別394.266／401.502ms、parse102.893／43.665ms、init0.230／0.198ms。約943ms花在兩次相同來源讀取與解析，不是solver初始化本身很慢。保留原profile日志。

新增factory_prefab：仍按原zipmod override→native fallback解析來源，讀完整bytes，以game root＋完整SHA256識別不可變Arc<Skeleton>，最多8個cache項。cache lookup／insert短鎖，解析在鎖外。miss使用新的parse_bundle_fresh入口，避免全內容key卻誤取原sampled-content parser cache；該入口只供此完整內容cache使用，原解析API不改。

prepare_pair在曲線／MotionIK背景worker中先準備不可變prefab。有active setup且任一角色缺runtime時，apply_contacts只載入一次當前來源，男女各自clone prefab、套原cf_n_height scale、建各自solver並init。不是共用solver、改成男角另一種猜測prefab、提前從動畫pose取rest或略過native高度。來源每次首次初始化／explicit prepare仍重新resolve及讀bytes，故仍有一筆GT來源核對成本；不使用無失效條件的root-only快取。

既有runtime繼續按原生命週期保留；普通切換／重播不重建。不同角色、身高及遊戲root不共用mutable state。未改wire／ABI、card特判、物理／IK／陰影更新頻率或cold source檔案。

原碼依據 `C:/portable/KoikatuSunshine/kks_decomp/Managed/KoikatsuSunshine_Data/Assembly-CSharp-firstpass/RootMotion.FinalIK/` 的SolverManager／IKSolver初始化，以及原factory所用p_cf_body_bone／cf_n_height流程；高度在from_prefab／init前套入，順序與原程式一致。

## 驗證

- IdentityR2 actual0：實際factory_key對root隔離及舊parser未取樣區域的bytes變更產生不同key。
- FactoryR2 actual0：真卡March_7th，原形體初始化後height0.72／1.02／1.28；獨立保留原逐次load／parse factory，所有initial solver欄位exact（target_sync map比值、其他欄位完整Debug表示比較）。相同內容命中同一Arc，三次身高init後template序列化bytes完全不變。
- FactoryR1 actual101：fixture漏正式apply_shape，body_shaped為None，測試unwrap失敗；補正式形體初始化後R2通過，不改產品來迎合fixture。失敗log／receipt保留。
- BuildR1–R4 actual0／Succeeded；R1為分段診斷，R2–R4完成immutable cache、實際key helper與fixture修正。
- Perf388PairR1 actual0／AutomationSuccess：四筆active IK oldsolver exact、男女667／681匯出bones、原walk、geometry／world parity、兩個Idle native選項各30frames共同時鐘通過；沒有以無MotionIK state的共同動作當性能驗收。

## 性能與界線

| active Idle API測量 | 第一個選項 | 第二個選項 |
| --- | ---: | ---: |
| 本輪未修改ProfileR1 commit | 1303.897ms | 86.123ms |
| 最終PairR1 commit | 769.308ms | 89.300ms |
| 最終CPU更新30frames平均 | 46.472ms | 46.875ms |
| 背景prepare | 13.352秒 | 2.728秒 |

首次提交此輪約少41%，只報這兩輪API觀察，不作所有姿勢固定比例。最終prefab load396.534ms、hit hash／lookup等18.588ms，兩角色init0.518／0.214ms；解掉一次來源讀取及GT解析，沒有把剩餘397ms讀取隱藏或停用override失效核對。後續commit沒有改善，CPU更新仍約46–47ms。

第二次prepare期間舊pair48frames，max100.526ms，舊動畫持續播放；冷prepare仍13.352／2.728秒。profile可選且預設關閉；舊／新flush比較仍各約50／46–47ms，只作Stage387回歸。ControlsR1 actual0／AutomationSuccess，七mask／owner／gesture／clock及generation cancel回歸通過。

此次只消除重複來源讀取與解析，首次仍有一筆來源讀取、兩個角色render commit、solver初次contact及其他工作。CPU API計時不等於全GUIclick／GPUscene FPS，也沒有全卡／全姿勢零bug保証。不修改原遊戲／zipmod來測試，來源識別以helper邊界與code路徑驗證，未宣稱現場替換全部資源或所有檔案異常皆有測試。

完整日志／失敗／receipts／source diff見Saved/Tests/AnimationPerf388；E266、HANDOFF3 §79。fresh R1 audit／snapshot核對Stage387310項旧封存原樣保留。
