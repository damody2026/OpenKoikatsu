# 明確身分重新關聯與交談中斷計畫

來源：已核准StreetSocial計畫8.2要求修改卡片後有可追溯重新關聯；read-only audit確認現有整檔SHA256身分會建立新關係，沒有重新關聯API。不是自動把相同名字視為同一人。

1. native FOKStreetStore提供明確old_id/new_id/request_id批次遷移，校驗並copy-on-write，保存全部關係狀態。目的地已存在拒絕合併，不動未知／未選卡的其它記錄。
2. schema1增加optional trace ledger，舊存檔仍可讀；同request_id重試不重複搬移，錯用同ID不能改映射。later存檔保留trace。
3. startup manifest明確identity_retargets，正式啟動器可選RetargetFile，只從使用者提供檔案注入requests；native驗證目的地為當前選中女角身分、舊身分不仍占選中slot。一次atomic save成功才更改live store。預設入口不遷移。
4. active男角／目標角色runtime錯誤或實際移除時立即取消交談，釋放預約，停止雙方音訊／動作、清除排隊片段，不發獎勵；正常render culling不得誤判。
5. Grok bounded source實作，root審查、Development+Editor編譯，隔離存檔fixtures native automation+runtime驗證；沒有真實卡片需要重綁時不會修改使用者relationships.json。完整驗收後才更新正式receipt。

錯誤分類：原先1～5功能通過不足以代表8.2全部完成；本次補缺口，不以文件已勾選當作實作完成。Runtime角色錯誤原先只加入Errors，不立即EndTalk；補上安全中斷，保留先前取消／retry的atomic語意。

Grok audit job run-muy6a9eh-s3fgkq完成7m43s、thread6c4f5e75-1cc5-4417-bf76-c5eab7a48434，input569955/cached8029184/output83273，USD3.8448152。實作job run-muy6p4gy-ypl7cr沿用thread，model grok-4.7-build-fast；來源審查與實作測試區分，不宣稱audit跑過遊戲。
