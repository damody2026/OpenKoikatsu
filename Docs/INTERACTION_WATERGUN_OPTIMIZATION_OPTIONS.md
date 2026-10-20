# 雙人水槍：兩個通用優化方案（2026-10-07）

結論：不應按性別解釋或特化。這組APNG男角碰撞三角形約86,051，女角114,301；真正失衡是液體遮罩配置及傳輸工作量。最終同binary兩輪無trace男角噴射約2.99／7.84FPS，女角72.33／88.91FPS，所有失敗都保留。第二輪男角水槍117.616ms/frame，其中mask84.147ms、geometry8.822ms、coating13.755ms；GPU另由frame report記錄，不能相加成同一CPU工作量。

女角噴射結束602個wet charts／154,112個chart儲存格；男角4,680／5,143,296，相差約33.4倍。這個texel數是「有液體的face所分配整張遮罩的格數」，包括空格，並非每格都有液體。不能只用這個數保證稀疏化收益；實作時要補實際positive／mobile格數與route數。

## 方案一：保留精度的稀疏格點傳輸

改FOKPaintMask::Transport，保留現有chart解析度、流動／跨face計算、質量守恆及畫面。每face維護非零格點與可流動格點的runtime索引；Deposit／Splat／TakeDrip維護索引，每次flow只處理H>0的格點，H<=Residual留在原格，不重建整張遮罩。讀取previous immutable buffer，暫存結果與串行確定次序累積，避免並行寫入同格造成不穩定。

每次pose revision只更新被流動路徑實際讀到的face geometry，不為未碰到的整張atlas準備工作；路徑計算採有界批次並行，超過目前262,144格門檻也不退回整批serial，限制的是暫存記憶體而不是關掉並行。更新dirty tiles，原atlas及overlay仍可使用，只上傳改變區域。

可行性較高：現有previous/read buffer、DirtyFaces、lazy geometry、route scratch都可沿用，改動集中在mask與存取流程。主要風險是float細小非零值、殘留與跨face插值使active索引失效；不能用任意epsilon刪掉液體、丟棄模擬積欠或跳步換FPS。驗證要比對舊算法每步體積／footprint、跨接縫、停射殘留、drop attach及清除，外加同pose／同射擊事件重播量測。

限制：若大部分chart格已填滿，收益有限；它不能單独根治目前小部件解析度失衡，不能預先承諾120fps。

## 方案二：依物理面積分配共用液體atlas

目前MakeSurface依每個mesh的三角形數與每mesh的2048目標決定chart尺寸，再夾到4..64。小mesh會取得較大的每face遮罩，即使其三角形實際很小；男角大量拆分部件使總工作量遠超女角。這是配置方式和角色拓撲的差異，不是性別或高模頂點較多。

改為runtime從原始source三角形面積及brush最小可辨尺度決定每face所需texel密度，讓相同cm²使用一致解析度。男女、頭髮／眼睛／嘴／衣服共用atlas page allocator，不再每個小mesh各自拿一份近乎完整解析度预算。原高模、材質與GPU骨骼路徑保留；只修正液體資料的過度取樣與分配失衡。

採穩定的source face ID與page/chart mapping；畫面overlay讀取實際chart rect。大面積或細節需要較高精度的face仍分配足夠格點，分頁可擴充；設定可指定實體texel密度及記憶體预算，而不按性別或card寫例外。配置／接縫鄰接／chart座標全部APNG載入後runtime建立，先準備命中部件，再在背景補其他部件；不需要預烘焙卡片。

可行性：現有每face獨立chart／UV／厚度ledger可重用，新增通用page allocator與解析度映射。主要風險是不同解析度face間的質量交換、細小部件可見精度、動作／部件替換後的stable mapping。需明確保持世界面積守恆，驗證brush輪廓與camera近距離畫面；畫布格數减少不等於高模降低。若改成變長chart儲存，需同步更新目前依Size*Size的索引及padding公式。

## 建議順序與驗收

較建議先做方案二，直接移除「小mesh拿過多格數」的根因；接著做方案一，消除剩餘空白／靜止格點的反覆掃描。兩者可以共存，但要各自A/B，先完成通用分配，再加入稀疏傳輸，避免無法歸因收益。

每個方案先以runtime可切換舊／新算法的比較開關驗證，通過後新算法成為預設；不能把暫時降低SurfaceHz、停止drop、減少男角mesh或裁掉頭髮當成方案。量測要固定pose／simulation seed／射擊事件並跨不同拆分程度的APNG卡片；同時記錄wet charts、儲存格／positive／mobile格、路徑工作、20Hz surface steps與120Hz ballistic steps及backlog。

目標仍是完整双人噴射平均<=8ms、實際Present>120FPS、切換／慢幀<=300ms。先前安靜播放120FPS不能代替水槍驗收；目前baseline約8.2..9.3ms也沒有足夠水槍预算，需同步檢查geometry按需query和coating的剩餘成本。

最初這份文件為方案評估，對照量測見 [INTERACTION_WATERGUN_PAIR_PROFILE.md](INTERACTION_WATERGUN_PAIR_PROFILE.md)。使用者後續選擇方案二及 1024；已實作依物理面積的 mixed-resolution chart、場景共用 buddy atlas、runtime lazy 配置與跨解析度守恆，預設1024。方案一尚未實作；本次未加入旧 allocator runtime A/B 開關，以保留的前版原始 profile 對照。詳見 [1024實作與驗收](INTERACTION_WATERGUN_ATLAS_1024_ACCEPTANCE.md)。
