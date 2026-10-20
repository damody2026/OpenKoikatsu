# Stage370 原遊戲骨架動畫匯入核心

需求：從共用原始 bundle / 同層與遊戲 zipmod 取得原遊戲 clip，保存 UAnimSequence；匯入卡片的 USkeleton / USkeletalMesh 使用 Unreal animation evaluator 播放，不在 runtime 讀遊戲或 Rust session。

## 匯入入口

Unreal Tools 與 Content Browser Add 新增 **Import Original KKS Skeletal Animations...**。填原始 KKS 卡路徑、已匯入的 Character object path、原始動畫 source（如 `studio/anime/00.unity3d`）、精確 clip labels（`;` 分隔）、全新 `/Game/...` 目的地、outfit（-1全部）。以60fps目標取樣，使用有理數幀率保留原始長度。不得把動畫catalog當已匯入clip。

原卡 SHA1 必須等於 Character.SourceHash；現階段已转换APNG的hash不同，不會猜測對應原卡。需要先透過既有介面匯入原始KKS卡。基礎動畫來自project native_v1，不需要原遊戲；模組動畫走相鄰/已識別遊戲mods的共用解析。

命令列 `-run=KKImportSkeletalAnimations -Card="..." -Character="/Game/.../Character.Character" -Source="studio/anime/00.unity3d" -Clips="clipA;clipB" -Destination="/Game/NewAnimations" -Outfit=0 -FPS=60`。`-List`只列此source精確labels；未知/重複label拒絕。來源限定避免同名clip選錯。

## 綁定與保存

唯一骨骼名稱映射，核對父骨骼名稱/未塑形bind TRS，不依賴來源palette與USkeleton的索引順序。每clip開獨立session，取樣最终全域palette的pose TRS（包含形體/helper），沿用OKUnityLocalToUnreal。寫入所有target骨骼位置、旋轉、scale，正規化四元數與符號連續；角差以abs(dot)/sqrt(normA² normB²)計算，排除兩側四元數長度誤差；非有限值拒絕。原作有效零pose scale原樣保留（本次cf_hit_head），不替換成1；逐骨記錄，另驗runtime有限值。

匯入資產使用UKKImportedAnimSequence（標準UAnimSequence子類），Editor用UAnimDataModel四元數source。UE5.8預設Sequencer controller會轉為Euler曲線，接近旋轉奇點有不可逆誤差；不放寬門檻。自有provider只對此類生效，新物件初始化替換空model，重開前保留provider選擇；runtime仍走標準壓縮播放，不依賴Editor模型。寫入IAnimationDataController前InitializeModel；每幀用DataModel原始軌道與來源TRS核對，壓縮結束後save，建立UKKImportedAnimationLibrary指向既有Character與UAnimSequence。原Character/model/material/texture/Skeleton及歷史Audit不改寫。

Actor可設定Character、AnimationLibrary、AnimationIndex、PlaySkeletalAnimation，再Rebuild；使用Unreal SingleNode播放保存的SK部分，保留默认Static組裝。SetAnimationPosition供驗證/seek使用。

## 驗證與限制

驗證使用男女原始卡各一套服裝，各站立/行走兩個普通clip。先核對native→raw寫入，再fresh-process從保存asset載入，檢查Unreal骨骼evaluate与track、網格變形/finite。隔離host仅KKImportedAssets，不含KKUnityBundle/card/共用source包，排除runtime靠遊戲讀取。

重要：僅Static部件、kind4私有rig、動態/IK/controller graph/事件/FBS尚不能因SK成功就認定通過。原動畫clip烘焙不是整個Animator狀態機。完整衣服/頭髮/飾品覆蓋須繼續驗證與處理；不宣稱全卡/原作GPU像素/fullShipping無bug。

## 實測結果

完整Editor BuildR14 actual0；ModelProbeR2確認AnimDataModel。MaleR7與FemaleR2各保存2個UAnimSequence子類+1個library，原卡來源皆project pack；原共用資源不需要遊戲本體。

| 卡片 / 原始clip | bones | intervals / samples | duration秒 | raw angle error rad |
|---|---:|---:|---:|---:|
| ill_male_01 / mc_m_walk_00_00_01 |667|129 / 130|2.133333445|.000000084|
| ill_male_01 / m_tachi_00_02 |667|300 / 301|5|.000000079|
| KK_398530 / f_aruki_00_00_01 |667|125 / 126|2.066666842|.000000084|
| KK_398530 / f_tachi_00_00 |667|1049 / 1050|17.466667175|.000000103|

位置與scale原始寫入誤差0；對照默认Euler-loss .001145172rad，證明不能直接沿用Sequencer轉Euler。合計1607個source samples，不是僅metadata。

VerifyMaleR3 / VerifyFemaleR1 fresh真RHI actual0：各2clips×11skins/23140CPUvertices，bone_error0，motion各68.308350/2.621200cm與61.714642/58.303532cm。自有commandlet client=true+AllowCommandletRendering；早期render未初始化兩次assert保留E248，沒有以NullRHI略過。

DetachedR1 / R2隔離host仅KKImportedAssets、無卡parser/共用資源包。兩次AutomationSuccess actual0，R2強制從有效compressed data取樣，另驗animation clock正向推進。共4clips/44skin instances/46280vertices，maxmotion68.304420cm，骨姿/有限值/頂點數/變形均通過。不同clock/float rounding导致与单独commandlet最后位略有差异。

Unreal操作：Tools → Import Original KKS Skeletal Animations...，匯入後直接點library查看來源與clip。放置ImportedCharacterActor並填Character、AnimationLibrary、AnimationIndex、勾PlaySkeletalAnimation；其默认仍Static，避免改壞原有組裝。

測試輸出位於`/Game/IndependentImports/Stage370MaleR7`、`Stage370FemaleR2`。女角clip提供PreviewMesh，可直接雙擊動畫預覽；早期男角輸出需在動畫Editor選匹配SK。

重要未完成項：上述只證明保存的骨架模型能播放原作clip。男卡6與女卡46個Static-only部件不會因此自動綁骨架；原衣服/飾品私有rig與多section完整覆蓋繼續處理，不宣稱完整人物/全卡/full controller/原作GPU pixels/Shipping完成。所有失敗保留E248與Audit370，不覆寫歷史資產/Audit。

