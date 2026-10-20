# 完整人物動畫覆蓋：Stage371（尚未完成）

## 已實作及驗證

- 原作動畫匯入核心與介面已在 Stage370 完成；四個男女站立/行走 clips、22 個既有 SK 模型、44 次隔離播放通過。Stage370 Audit 和資產不修改。
- 新增 commandlet `-Inventory -Card=... -Character=... -GameRoot=... -Output=... -Outfit=0 [-AuditClip=...]`。按保存 component 名稱與 role 核對 native part，而非假設 component 序号等於 SessionIndex。輸出新 JSON，既有報告不覆寫。
- 男卡17部件/11SK/6 Static-only，6個全部kind4；女卡57部件/11SK/46 Static-only，37個kind4、9個kind0。9個kind0均有原始 `rigid_follow_bone`，不用角色名或hair/head猜掛點。
- 原 body/head 與多個private部件有2個材質區段。既有 persistent SK 保存主表面，完整多材質區段仍需接入。
- native `AccessoryRuntime::effective_palette` 共用 AssignedWeights、植入/協同衣服、已刪除slot identity 等邏輯。既有render分支保留，新增匯出使用同一palette規則及完整socket correction。
- Additive `ok_part_skin_palette` / `FOkSession::SkinPalette` 匯出borrowed raw geometry/原始4weights/slot indices，及完整row-major world*bind矩陣。尺寸/索引/非finite/負weight/空weight檢查；query-count與copy、失敗不修改buffer。未初始化simulation拒絕；pointer僅至session mutation/close。
- MaleR2/FemaleR2，5個行走時間點逐頂點核對，最大位置誤差分別 2.950026864e-7m / 2.711677741e-7m。181 / 1274個SMR slots。這是native矩陣重建核對，不是UE模型匯入/GPU像素驗收。
- 實際矩陣shear（normalized列內積）最大 .504950235 / .154178694。不可用單一FTransform/SVD後丟shear，也不可將kind4索引套全域palette。
- `KKExactSkinning` 三等權重rank-one TRS adapter：三個骨骼各保留一個矩陣column，scale該軸=3*column長度、其餘0、rotation把軸映射column、translation相同；三者平均精確等於原始affine矩陣。4個原始slot展開12個Unreal influence。Unity->UE基底沿用(-X,Z,Y)，m->cm。
- MathR1 algebra；MathR2另驗實際UKKImportedAnimSequence/UE compressed data與decoded零scale、shear、reflection全通過，actual0。MathR3新增ABI負例與final source確認。BuildR6 actual0。
- 匯入介面完成訊息新增實際static部件數量，避免把动画资产保存成功誤報完整人物動畫完成。

## 接續實作（不得宣稱已完成）

1. 在持久化卡片匯入流程加入identity scene root與私有palette virtual bones；保存來源renderer身份、原始slot binds及rigid reference socket。既有native骨架層級/locals保留。所有新輸出用新資料夾，不改舊assets。
2. 以12influence保留SMR slots與各slot原始bind差異，量測16bit weights量化誤差。相同slot的三權重須相同，避免量化造成column偏差；配置/驗證Unreal unlimited influences與16bit indices，不能讓builder剪為4或8。
3. 原始kind0掛點以完整 native bone world*初始world inverse 為唯一來源；不能套UE近似TRS或head fallback。持久化與clips嚴格匹配renderer名/slot數/掛點/卡SHA。
4. 保存全部材質區段、UV/colors、基準法線/切線、輪廓/overlay及morph。零pose scale允許，零reference scale禁止。需核對輪廓是否應在posed normal上擠出，不能只複製Tpose偏移就當原作pixels完成。
5. 烘焙final palette各column動畫tracks，處理quat continuity、column接近0/反轉、非整數幀與中間時間。要用native oracle驗插值及UE壓縮後骨架/頂點誤差，不只驗sample keys或自己的expected tracks。
6. body/head/hair既有reference-LBS也需native頂點oracle，不能將UE播放自己的tracks無誤視為任意bind/general shear無誤。視量測將同一矩陣adapter推廣所有加權renderer。
7. 新鮮程序及無parser/pack隔離工程驗全角色/全區段；包括不同卡/衣服/剛性飾品、零scale、實際動畫時鐘、native frame位置和normals。完成才更新全人物狀態。Shipping/cook、全部controller事件及最終GPU影像另需驗收。

原作controller/FBS/particles並非完整runtime移植；目前先烘焙原生取樣結果。此文件記錄正在做的通解，不是全卡無bug承諾。

最終本輪重測：MathR3、MaleR3、FemaleR3全部actual0，BuildR6成功；上述native矩陣與ABI檢查均通過。尚未整合完整持久化SK/全人物播放，工作繼續，Progress.json complete=false。
