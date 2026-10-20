# 街區可收集話題驗證

2026-10-08。主代理實作，Development StreetTopics03，實際場景證據 Runtime-StreetTopics04。

## 完成內容

- 原作48種話題，五種性格各10種可提出話題、48種持有話題回應；590個共用原作語音，缺檔0，逐檔SHA256驗證。資料是共用 topics.json，不是卡片專用Cook資產。未匹配完整中文語音的新增話題使用原作日文，文字採可靠的來源翻譯或日文。
- 自製街區沿現有路點放置共用球形拾取物；F近距離拾取，99上限、每日同話題只能拾取一次，N次日恢復。拾取點含不同類別及情色話題，不套用原作完整地圖／祈禱掉落表。
- 對話中按4或「請她選話題」，女角用自己性格的原作引言提問；避開最近5題。播放完自動開UI游標，持有話題按鈕顯示名稱、類別、稀有度及數量，可完全對題／同類／答非所問或敷衍。
- 主動使用持有話題播放完整順序的來源男女台詞；男主無語音行仍顯示，女角逐行播真實語音與來源表情，末尾無語音男主文字顯示1.5秒。稀有話題不讓同女角重複使用。
- 播放完成才在同一原子保存中扣一份庫存並寫入原作數值、最近提問題目、學習點及慾望；取消／語音失敗不提交。舊存檔仍可載入，新增欄位預設空庫存／零慾望。
- APNG讀取角色 interest.answer，匹配類別才加來源 TopicInterest。原作 heroine.isVirgin 不在APNG，未捏造；本街區依要求允许情色話題聊天，此門檻差異明確記錄。

## 數值依據

原作 TalkScene.CommandWait：完全相同 common20／rare30；同類 common5／rare[2]=0；不同類0。原碼同類 rare 使用 rare[2]，保留此行為，沒有自行改成表中的rare10。

男方主動話題由TopicTalkPoint各稀有度[8,12,30,75,100]決定：一般進好感，Category4進lewdness；興趣一般額外favor2、情色額外lewdness5。情色主動話題 desire4/5 各+10，本場景保存並顯示兩種慾望（界面上限100）。原作學習點保存，但不冒稱已做男主技能養成。

## 實際Development驗證

Runtime-StreetTopics04/social.validation.json：passed=true，errors=[]，collected_topics_checked=true，collected_topic_commits=4。實際流程：

| 回應 | asked / used | 好感 | 興奮 | 庫存 |
| --- | --- | --- | --- | --- |
| 完全相同 | 4 / 4 | 0→20 | 0→0 | 1→0 |
| 同類 | 12 / 7 | 20→25 | 0→0 | 1→0 |
| 答非所問 | 22 / 0 | 25→25 | 0→0 | 1→0 |
| 主動情色 | 無 / 37 | 25→25 | 0→12 | 1→0 |

另通過真實街區近距離拾取、重複拾取拒絕、取消女角提問不改存檔、double-complete不重複提交、欲望各10、整體保存回讀。額外測試使用獨立 collected-topics.relationships.json fixture，恢復原selftest state，沒有寫使用者關係存檔。

Tools/verify_street_social_topics.py 獨立核對590語音hash、四筆APNG身分／性格、題目與分支、實際語音ID序列、庫存與原作數值、唯一交易在保存中。另原有15話題逐筆比對、10正好感、候選、停止活動、取消恢復、表情、陰影、角色朝向／輸入模式回歸全部通過。

Python26通過；隔離Editor native15通過（新增CollectedTopicInventoryAndEffects，涵蓋原作數值、99上限、每日claim、舊存檔、invalid100拒絕及完整roundtrip）。Development與 -UsePrecompiled 隔離Editor建置成功；主Editor未关闭／未熱更新。

實際1920x1080 street.collected-topics.png已開圖：來源男主末句可見，好感25／興奮12／兩種慾望10顯示，女角表情及動作正常，拾取物可見。完整桌面操作、逐段聽音與美術驗收仍留最後，full_acceptance=false。沒有性能或120FPS驗收聲明。

## 失敗與修正

StreetTopics01：編譯拒絕TSharedString.IsNumeric、變數遮蔽；正式修正類型處理並重新編譯。
StreetTopics02 runtime：共用語音目的地錯一層，topic_voice_missing拒絕入場；修正匯出路徑後重新匯出／驗hash，StreetTopics03通過。沒有略過缺檔檢查。完整錯誤見 STREET_SOCIAL_ERRORS.md。

公開入口 StreetSocial.cmd 已切至 StreetTopics03.stage（證據Runtime-StreetTopics04）；ValidateOnly通過，獨立執行檔SHA256：`544b1aba929b37c87e3fc0d0fe1a6a96117a5509d17e5e1b11b6be1b60118a16`。主／host Street來源一致。

收尾修正並重測：每種話題都有一個實際runtime拾取物，topic_pickup_count=48；不再因路點數限制導致部分話題無法取得。StreetTopics03成功編譯，Runtime-StreetTopics04完整場景回歸／四筆話題交易／590語音hash及原有15話題再次通過，入口和最新hash已更新。native15的規則／保存來源未改，這次只補齊場景物件數量。
