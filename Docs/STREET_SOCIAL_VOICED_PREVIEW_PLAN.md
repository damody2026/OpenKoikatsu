# 有聲關係預覽實際流程驗證

2026-10-08，主代理直接實作，功能優先。

1. 補原生場景異步 probe，使用已選 APNG 女角的真實匹配招呼語音作為流程 fixture；不新增假原作友誼台詞，不改 production catalogue。
2. 經 production Tick、語音播放與 FinishLine 到預覽完成，確認不自動保存；取消後不提交。再播放一次，由 ApplyRelationship 明確確認，確認只提交一次並驗證保存回讀。
3. Probe 使用自己獨立的 relationships 檔案；結束還原 Store、保存路徑、交易列表、場景位置與鏡頭。正常15話題測試不計入 fixture 獎勵。
4. 新 Development／Editor、原生測試及實際六人場景回歸通過後才更新公開 CMD。完整人工驗收保留到最後。

已完成：Development／Editor、Python26／Unreal13、實際完整語音預覽／取消／單次確認／獨立保存回讀全部通過；正常15話題與10次正向好感不回歸。公開CMD更新 StreetVoicedPreview01，詳見 STREET_SOCIAL_VOICED_PREVIEW_VALIDATION.md。
