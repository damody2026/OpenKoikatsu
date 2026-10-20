# Stage330 男角匯入驗證

本輪三卡：UserData/chara/male/KK_224305.png；DefaultData/chara/male/ill_male_01.png、ill_male_02.png。所有路徑相對 C:/portable/KoikatuSunshine。

## 實作與原生依據

- 工具/圖庫搜尋 UserData/DefaultData 的 female/male，依實際卡sex選Male factory。
- sex0 body使用p_cm_body_00，o_body_a原生101骨頭順序；cm_m_body/Koikano/main_skin與cm_body_00_md。cf頭流程共用，不從性別臆造male head。
- 共用衣服pattern loader依ChaControl MainTexAB、category430與KKEx resolver。衣服create_topN PS的scale/offset/rotation/alpha混色取代舊StudioEditor近似。
- 卡片ClothesColorPattern補offset/rotate，缺欄位用ChaFileClothes原生(.5,.5)/.5。

## 證據與執行

`Saved/Tests/Male330Native`：三卡native body/face/renderers，64/38/38 rendererrows，各body6823頂點/101骨/三UV/Color32，UV3原生空。Studio停止exit=-1。

`Saved/Logs/Male330SourceAudit.json`：verify_male_sources.py逐筆核對。`Male330SourceNegatives.json`：錯材質/骨序/UV/偽UV3四反例。

`Saved/Tests/Male330ClothesNative`：KK jacket及ill_male01襯衫原生MainTex。`Male330ClothesSource` source bake；R1/R4保留錯誤結果。`Male330ClothesPixelR4.json`失敗差異不能當接受證據。

`Male330Final`是錯三骨架舊結果；R2骨架修正但漏pattern；R3補pattern來源但近似算法錯；R4/R5後續核對中。所有批次分別保存，不覆寫失敗證據。

## 最終驗證

正式部署 SESSION207 / CLI v-sacbody-318-clothes-pattern-dxbc-rows / bundle cache103。Male330UEBuildR5成功；Male330FinalR5十二張900×1080四視角/exit0、PV3/material3/GPU12/shadow12、附件最大誤差0。ContactSheet全部目視，手腳/衣型/配件正確。

FemaleRegression三卡KK226176、KK388205、江之岛盾子2共12視角/exit0相同自動gate與目視無可見結構退化；原正式136圖片SHA完整保留。新body source/UV代碼已部署，舊Stage325–329 source-only快照不重寫或冒充当前runtime证据。

Male330ClothesFinalPixels.json：實際襯衫max1/mean.0530、jacket max23/mean.0330、sailor max4/mean.00757，三者alpha exact。strict max3僅襯衫PASS，其他未PASS。原生衣服shader輸入/新受控shift/alpha/combined保存Male330PatternNativeControls；任意shift max16、combined max8、alpha max1。其像素證據不能合併宣稱全PASS。

曾試 authored mipchain + derivative LOD，jacket max27/mean.060比原更差，source保存在Male330MipsCandidate，正式代碼已撤回，不以native count代替sampler parity。不同卡名字的clothes-create批次會要求每卡具有全部資產，OtherClothesNative runner首卡完成後驗證缺sailor而exit1；其輸入保留，但該batch不是PASS。SailorNative改單卡完整38rows，exit=-1。

Male330ClothesFinalSource.log source bakes PASS，Male330SourceAudit/Negatives PASS；Male330FemaleBootRegression仍原生嚴格gate max6已知FAIL（同既有Stage307），不放寬max3。

## 接續限制

本輪三男基本匯入已驗證，完整native rendered RGB parity仍false，未宣稱全專案零bug。Random uncensor全選項、pattern mip/任意旋轉GPU取樣差異、壓縮貼圖像素差異、全34女卡新pipeline回歸、SkinPlus renderer integration與Siesta長期問題仍開放。優先取得pattern native GPU sampler/LOD與旋轉instruction證據，不以圖像調整猜參數。source正負例/舊失敗/新capture不可覆寫。
