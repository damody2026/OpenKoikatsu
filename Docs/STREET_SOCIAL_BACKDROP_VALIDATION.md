# 街區 runtime 遠景修正驗證

2026-10-08，主代理直接完成，功能與畫面修正優先，本輪不做性能優化或硬體特調。

## 修正

黑帶來自64×56公尺街區之外露出的天空下半球空域。Runtime 從已載入主地面取得共用網格和材質，建立2公里寬、頂面低於主地面10公分的遠景地表。它沒有碰撞、沒有投影；薄霧從60公尺外開始，非體積霧。原角色材質、燈光、曝光、活動路徑及鏡頭碰撞不變。地表與霧隨場景結束釋放。

不依賴卡片名稱、身分或特定人物，不新增卡片私人Cook資產，也不使用KKS地圖。runtime_backdrop_ready=true 並納入功能測試。

## 編譯與驗證

- 初版 Development StreetBackdrop01成功；隨後修正既有規則測試的路徑別名假設，重新建立 Development02及全新 StreetBackdrop02.stage。Development02成功，44.68秒。新街區源碼、規則與原生測試均已逐檔核對，隔離主機與主專案一致。
- 主專案 Editor 編譯來源成功但連結遭執行中Editor占用DLL阻擋；使用隔離 StreetSocialHost 檢查。完整隔離 Editor03成功連結本專案DLL，但整體因引擎NetCore DLL被占用而失敗；最後透過 -UsePrecompiled -NoUBTMakefiles 完成 Editor06 的相容中繼資料，建置成功。既有Editor保持運作；主專案當前Editor DLL尚未熱更新，不能宣稱它已切換新版。公開CMD用獨立Development02，不依賴該Editor DLL。
- 產生的測試主機排除無關百人房間的editor匯入器區塊，街區來源不變。既有規則測試改核對宣告 voice root 的回退路徑與完整規則內容，避免相同共用檔案的junction別名被誤認成錯誤。正式規則讀取及獎勵邏輯未修改。
- 失敗建置／Automation02、04、05全部保留，不當成通過證據；最後 Automation-StreetBackdropIsolated06 原生13項成功、failed=0、notRun=0。沒有手改BuildId或停用相容檢查。Python26项再次通過。
- 實際六人獨立Development場景：15話題、5招呼、5道別完成；10次正向好感及保存回讀一致。語音序列逐筆比對全部匹配；有聲關係預覽、失敗重試、取消、候選與活動占用回歸成功，errors=[]。RequireFaceTransitions、RequireShadowProxy、RequireCleanShadows通過，使用者存檔保護檢查通過。
- 實際檢視新 street.dialogue.png：舊黑帶消失，近距離人物／字幕／UI仍可见。固定無遮挡遠景區域(260,450)-(640,490)，RGB三通道全低於24的像素比例由100%降為0%。這是黑帶區域檢查，不是全角色逐像素CPU/GPU比較或完整美術驗收。
- 可重現影像診斷：Saved/StreetSocial/BackdropReview/verify_backdrop_visual.py；结果 backdrop.visual.validation.json。正常功能與獨立語音證據：Runtime-StreetBackdrop02/social.validation.json、voice.reward.validation.json。
- 公開 StreetSocial.cmd receipt 指向 StreetBackdrop02.stage；ValidateOnly及獨立 SHA256 核對通過：84e9dbc9c3b55c79fa0a13b162e7d9a746b0d36c9c902270992a38a8cc654492。共用patch／catalogue與百人CMD保留。
- 增量來源審查 Saved/StreetSocial/BackdropReview/implementation.diff，共105行，三個明確路徑。HEAD c1bc6b589b9e38b80a39a9661bd5e4e1ecd20f09不變，沒有提交或推送。

完整人工操作／聽音訊／各角度外觀及長時間驗收留到最後，full_acceptance=false。本輪截圖只證明目前視角的地平線修正；建築仍是自製demo幾何，不以此宣稱整套美術已完工。既有測試框架的性能數值保留在原始資料，不提出新的FPS結論。
