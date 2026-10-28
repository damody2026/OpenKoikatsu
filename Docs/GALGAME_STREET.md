# 街頭對話的 galgame 演出

StreetSocial（`StreetSocial.cmd`）的交談改用 ok-story 的即時演出，和 OK Studio 的劇本播放（[使用說明](GALGAME_STORY_USAGE.md)）是同一套：

- **底部對話框**：顯示說話者名字（角色色）與台詞。文字隨語音進度逐字打出；沒有語音時依固定速度打字。
- **左下即時立繪**：拍的是說話者本人的 3D 模型（正交 SceneCapture，只拍她自己的元件），表情與嘴型與場上同步。
- **逐句鏡頭**：她說話時在「說話者特寫」與「雙人」之間交替；主角說話時改用「越過主角肩膀看她」（`over`）。每個鏡頭先產生多個候選位置，再用深度探針避開牆壁；構圖留出底部對話框的高度。
- **路人不擋鏡頭**：站在鏡頭與兩人之間的其他角色，只在主畫面暫時隱藏，走開後恢復。

舊的街頭對話框在演出開啟時收起，只在餐桌上有菜、或麥克風剛聽到還沒送出的話時，縮小留在新對話框上方；右側面板（話題、按鈕、麥克風音量）不變，新對話框讓出右側 432px。越肩鏡頭會依兩人距離加大側角，主角只露出肩膀。

## 分工

| 部分 | 位置 | 做什麼 |
|---|---|---|
| 演出邏輯 | `Tools/ok-story/src/live.rs`（`ok_story.dll` 的 `oks_live_*`） | 收到「誰說了什麼」，決定打字進度、立繪、鏡頭與探針；C ABI 以 JSON 溝通 |
| 街頭接線 | `Source/OpenKoikatsu/OKStreetSocialGalgame.inl` | 把街頭狀態（劇本台詞、即時對話字幕、語音進度、骨架位置、相機）轉成輸入與 facts，套用回傳的鏡頭，拍立繪、跑探針 |
| 畫面 | `Plugins/OKStudio/.../SOKStoryView`、`OKStoryCapture`、`OKStoryLive` | 與 OK Studio 共用的對話框 widget、立繪擷取、探針與 DLL 載入 |

C++ 只負責呈現；鏡頭選擇、打字速度、構圖等規則都在 Rust，調整見 `Tools/ok-story/src/tuning.rs`（`live`、`camera` 區段）。

## 開關與檔案

- `ok.Street.Galgame 1`（預設）開啟；`0` 恢復舊對話框。
- `ok_story.dll`：打包時由 `Tools/stage_street_social.lua` 複製到執行檔旁；`run_street_social_development.lua` 會建置並以 `OK_STORY_DLL` 指定。找不到 DLL 時保留舊對話框，log 會寫 `Street galgame presenter unavailable`。
- 每送出一句台詞，log 會記一行 `Street galgame line c<slot>: ...`，可用來檢查哪一句換了鏡頭。

## 什麼時候會演出

- 站著交談（按 E 開始、話題、告白等劇本台詞）：對話框、立繪與鏡頭都會用。
- 即時對話（ok-mind／語音）：主角與她的字幕各自成為一句；她想回答時的「……」不算一句，主角那句會留到她真正開口。劇本台詞裡主角那句至少顯示 1.2 秒。
- 一起散步、約會途中的對話：只換對話框與立繪，鏡頭維持街頭本來的跟隨鏡頭。
- 拍照時不搶鏡頭。

## 開發測試

不用打包，直接用 Editor 開街頭：

```powershell
.\Tools\lua\lua.exe Tools\run_street_social_development.lua -Editor -Control -Window 1600x900
```

`-Editor` 以 `UnrealEditor.exe <專案> <地圖> -game` 執行，並在 manifest 加 `editor_common_materials`（Editor 沒有 cook 過的 shader library）。接著用 `Tools/street_play.lua`（`state`、`key E`、`key One`、`shot`）操作與截圖。Editor 模式下 `nav girl` 自動導航不會走，要用 `look` 加 `key W` 走到她旁邊。

## 已知限制

- 立繪與鏡頭以骨架位置計算；躺著等更大幅的姿勢的構圖還沒專門調過（坐著見下節）。

## 立繪不越過文字欄（2026-10-26）

愛麗絲這種長雙馬尾的角色，頭髮擺動時會甩出立繪原本的範圍，垂在名字和台詞後面，字變得難讀。`SOKStoryView` 畫立繪時加了裁切，只畫到文字欄左邊（`text_left` 再往左 1% 畫面寬）；OK Studio 的劇本播放用同一個對話框，一起受惠。Play13E 截圖確認。

## 她坐下時第一人稱跟著轉過去（2026-10-26）

一起去學生餐廳吃飯時，她在餐桌坐下（`seated_female_chair`）、他還站著：第一人稱只在「他的眼睛」移動 40 cm 以上才重新取景，他沒動，鏡頭就一直對著她站著時頭的位置，她坐下後臉沉到對話框和點餐清單後面，只露出頭頂。現在她的臉一離開畫面可見的範圍（左右邊、頂端、對話框上緣以上，各留一點邊）就平滑地重新取景（`Live::in_view`，`Tools/ok-story/src/live.rs`）；臉還在畫面裡時（點頭、手勢）鏡頭不動。最早用「臉移動超過 30 cm」判斷，惠惠蹲下（手勢）時鏡頭跟下去、她站起來後鏡頭還朝下，臉被切在頂端，所以改成看臉在不在畫面裡。鞠躬例外：臉離開畫面但骨盆還在取景時的高度（彎腰，不是坐下或蹲下），鏡頭先停住最多 3 秒等她起身；原本貞德道歉鞠躬時鏡頭跟著往下，整段拍成頭頂（`LiveState.framed_hips`、`out_since`）。單元測試 `first_person_follows_her_when_she_sits_down`、`first_person_comes_back_when_she_stands_up_again`、`first_person_holds_through_a_bow`。Play12E：女生宿舍交誼廳「坐下聊天」兩人坐著，她的臉在對話框上方、畫面中上。

順帶查到的：生活中坐著的姿勢（滑手機、休息）在他開口時她會站起來說話；控制通道 `/play` 的動作在對話中不會播。要測坐著的對話，用右側「共同活動／設施 → 坐下聊天」（要是朋友以上、他不在打工時段、地點有營業）。
- 路人只在擋住鏡頭時隱藏，從畫面邊緣走過的人仍會入鏡。
- 多人對話（群聊）：每句台詞的立繪與鏡頭跟著說話的人（2026-10-26 Play2E 截圖確認，貞德與胡桃輪流說話時各自換立繪、鏡頭轉向她）；兩人以上同框的構圖還沒專門調過。

## 群聊台詞被整句丟掉（2026-10-26）

繁中遊戲裡，角色卡名字是簡體的女主（「贞德」）在群聊裡一句都沒出現：模型回覆的 `who` 寫成繁體「貞德」，`FinishPartyTalk` 找不到這個名字的人就把那句丟掉，另一位還在回應她「說過」的話。名字比對（`OKStreetScriptedName`）原本只在 zh-CN 轉成簡體，現在 zh-TW 也轉成繁體再比；找不到人的台詞會在 log 記 `Street party talk: line of "<who>" dropped`。

群聊提示裡的名字（`SpokenName`）也改用遊戲的字體：原本 persona 寫簡體的「贞德」在繁中遊戲裡以【贞德】給模型，模型卻以「貞德」回答，同一個人在記憶裡有兩種寫法。

語音伺服器忙時，正在播的串流語音中途斷掉（ran dry 再逾時），原本要先拿這一句的檔案（只是給記錄用，又等一次逾時）才去要下一句，群聊裡下一位女主的台詞因此晚了半分鐘以上；現在這種情況下一句立刻送出。Play3E／Play4E 量測時 GPU 上同時有別的遊戲與語音伺服器，測試用 ok-tts 每句要 15–30 秒，貞德·Alter 的台詞在測試的等待時間內沒輪到，沒能在畫面上確認；台詞本身模型有寫（`Street party talk: 2–4 lines`），比對也對得上。演出等語音的上限仍是 120 秒（刻意的設計：忙碌時寧可等聲音）。測試用 ok-tts 的 log 顯示 6 秒的語音生成了 88 秒、排隊最久 79 秒：GPU 上同時跑著 C 的遊戲、C 的 ok-tts、ok-song 與 StreetSocialCity，是共用 GPU 的負載，不是遊戲的問題。改用關掉語音（`V`，測完切回 TTS）的群聊測試（Play5E／Play6E），貞德與貞德·Alter 輪流說話，立繪與鏡頭都跟著說話的人，兩人名字互相包含也沒認錯。

對話框的名字標籤也改用遊戲的字體（原本是卡片的「贞德」）；模型偶爾寫的「⋯」（U+22EF）對話框字型沒有，顯示成方塊，改成「…」（`OKStreetGalgameWords`）。Play7E 讓主角說出約 60 個常見全形標點與符號逐一截圖確認：其餘（『』《》〈〉【】‥︙―﹏♪☆★♡❤※→ 等）都在字型裡；「〜」（U+301C）畫成一道弧線，也改成全形「～」。

## 打包版

立繪材質是 `/Game/OKStudio/Story/M_StoryPortrait`（DirectoriesToAlwaysCook）。改了 `OKStoryBuildPortraitMaterial` 之後，要重新存檔：

```powershell
& 'E:\PhysX\UE5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' OpenKoikatsu.uproject -run=OKStorySavePortrait -unattended -nullrhi
```

找不到這份資產時，log 會寫 `Story portrait material ... missing`，打包版就沒有立繪；Editor 會改用在記憶體裡建的材質。

## 歷史記錄回放也有立繪（2026-10-24）

「回放最近 20 句」和歷史視窗每一行的「播放」都當成 galgame 的台詞演出：對話框、說話者的左下角立繪，她有語音的句子打字跟語音同步；沒有語音的句子（包括男主的話）依字數停留 1.5～6 秒。沒在對話時從歷史視窗播放，會以該句所屬的女主暫時開一段演出（不動鏡頭），播完、語音停了就收起。關掉 galgame（`ok.Street.Galgame 0`）時維持舊行為：只重播她的語音，播放鈕只出現在有語音的行。

回放中途按「停止回放」或回放播完而對話還在進行：對話框回到這段對話她最後說的那句。回放中按 Esc 結束對話，演出立即收起（無語音句的停留時間會一起清掉）。

## 第一人稱對話鏡頭（2026-10-24）

對話時的鏡頭從男主的眼睛看向女主（ok-story `pov` 鏡頭，`tuning.json` 的 `live.pov` 預設開）：她說話時看她，他說話時看對話對象；群聊時另一位女主說話，鏡頭轉向她。眼睛位置在男主頭骨往後 12 cm、往上 6 cm（`pov_forward`／`pov_up`，往後是為了她靠近或彎腰時仍框得住），水平視角 60°（`pov_fov`），畫面中心在她臉到胸口之間 35% 處（`pov_aim`）。鏡頭在男主頭部 45 cm 內時，男主自己的模型從主畫面隱藏（立繪照拍）。`live.pov: false` 回到舊的特寫／雙人／過肩輪替。

## 多段語音的打字同步（2026-10-24）

自由對話她的一則回覆會切成好幾段 TTS（一兩句一段），畫面上是同一句台詞；回放的一行也可能有好幾個語音檔（`a.ogg|b.ogg`）。打字進度原本只看正在播的那一段，第一段播完整句就打完了。現在進度是「這位說話者已開始的段數－1＋目前這段的進度」除以她的總段數，段與段之間等下一段時也算在說（`playing`），回放的多檔語音同理。回放結束要把對話框還原成她最後一句時，若畫面上已經是同一句就不重送（不會重打一次）。

## 對話時她抬頭看著他（2026-10-24）

面對面對話時（`TickFaceTalkPartner` 會把她轉向他的那些情況），她的頸與頭轉向男主的眼睛（他的 `cm_j_head` 上方 6 cm），不再跟著待機動作看前方或低頭看他的胸口。做法是人群動畫本身的頭部注視（`OKSetCrowdRuntimeHeadLook`）：每幀用 CPU 取樣她目前動作的頭部方向，算出轉向目標所需的旋轉（相對身體左右 60°、下 25°／上 30° 內），頸部分 40%、頭部分其餘，換成兩根骨在各自父骨座標下的前乘旋轉，交給 GPU 姿態 shader（`IndependentPose.usf` 的 `LookRotation`）與 CPU 姿態；保留動作原本頭部動態的 20%，開始與結束各約半秒淡入淡出。她被自己的活動占住時（同行配對、設施、睡覺、親密姿勢等）不轉頭，對話結束就放開。

這改了 KKCrowdGpuPacking 的全域 shader，所以只重編執行檔的 stage 還要新的全域 shader：`Tools/rebuild_street_social_patch.lua` 現在多一步，用專案本身 cook 引擎的 Entry 地圖產生 `Engine/GlobalShaderCache-*.bin`，打成 `RuntimeGlobalShaderPatch-*_P.pak` 放進 patch 資料夾（`-pakdir` 掛載，優先於 base 裡的舊快取）。執行前 Editor 要先用目前的程式碼編過。

## 立繪不畫最外層輪廓線（2026-10-24）

左下角立繪的外框是角色描邊（outline 殼）在剪影外圍的那一圈。立繪的顏色照拍（臉、頭髮之間的內部線條保留），但決定透明範圍的 coverage 那一張拍攝時暫時隱藏她的 outline 殼（`OKSetCrowdRuntimeOutlinesShown`，材質的 `__OK_RuntimeRevealOverride` 設 0，拍完設回 -1），於是剪影外圍那圈描邊落在透明區、不顯示。`FOKStoryPortraitCapture::Update` 新增選用的 `CoverageScope` 回呼做這件事。coverage 原本當成主畫面的額外 pass 在幀尾才畫（`bRenderInMainRenderer`），那時 outline 早已設回顯示，所以有 scope 時 coverage 改成在 `CaptureScene` 當下自己畫一次（無光照、無後製，每兩幀一次）；`ok.Street.GalgamePortraitOutline 1` 回到保留外框、coverage 跟主畫面一起畫的舊做法。coverage 原本只有顏色的一半解析度，雙線性放大後邊緣會多透出一兩個像素，正好是那圈描邊，所以改成與顏色同解析度。對話時頭部注視可用 `ok.HeadLook 0` 關掉（A/B 比較用）。

走路時（她跟著他走、邊走邊說）立繪外框會閃：顏色在第 N 幀拍、coverage 在第 N+1 幀用第 N 幀的鏡頭拍，她在這一幀間已經移動，裁切邊緣就偏離剪影，一側露出描邊、另一側被切掉，每次擷取來回跳。現在隱藏描邊時，coverage 在同一幀緊接著顏色拍（反正它已經是當下自己畫），同一個姿勢，邊緣對齊。

## 第一人稱被家具擋住時改用一般鏡頭（2026-10-25）

兩人隔著桌子、檯燈或書架說話時，第一人稱鏡頭只拍到家具（她就在 1.2 m 外，被桌上的檯燈整個擋住）。現在每一句取景時，第一人稱的候選（她的臉 → 他的眼睛）排第一，後面接這一句一般鏡頭（特寫／雙人／過肩）的候選，一起送 probe 給遊戲量視線。第一人稱看得到她就照用；看不到就換成第一個視線暢通的一般鏡頭，該句之後的重新取景也跟著那個鏡頭（`LiveState.pov_fallback`，`Tools/ok-story/src/live.rs`）。下一句再從第一人稱開始檢查。

## 有語音的句子先等語音（2026-10-25）

生成的語音常在文字出現幾秒後才開始播（TTS 忙碌時排隊更久）。打字機原本在語音開始前就用閱讀速度打字，字跑在聲音前面，等語音開始時整句早已打完，同步就沒了。現在標記有語音的句子會先等，最多 `live.voice_wait`（預設 2.5 秒，`tuning.json` 可調），這段時間不打字；語音一開始就跟著它的進度打，逾時才改用閱讀速度。

## 台詞整句顯示、點擊跳過、滾輪回滾（2026-10-25）

- **整句顯示**：台詞一出現就整句顯示，不再逐字打出（`live.instant_text`，預設開；關掉時回到跟著語音打字，上一節的 `voice_wait` 仍有效）。
- **跳過**：場景中按左鍵（沒點到她身上可觸碰的部位時）、Enter 或 Space，畫面上這一句立刻結束（`SkipGalgameLine`）：
  - 回放中：換下一句回放。
  - 她的自由對話回覆：停掉語音，這次回覆中同一位說話者接下來的語音段落也一起略過。
  - 劇本台詞：結束並算作說過。
  - 他自己的語音：停掉。
  - 她唱歌不受影響。
  - 聊天輸入框裡的 Enter 照舊送出訊息（那時遊戲畫面沒有焦點）。
- **回滾**：場景中滾輪往上，對話框退回這段對話的上一句（含立繪），往下前進，回到最新一句就恢復即時；場景中滾輪不再縮放街區鏡頭。
  - 他每說一句話之前，記一個回滾點：已顯示的句數、自由對話記錄序號、她的對話記憶、她的事件歷史、她的關係條目（好感、色氣、階段），以及 ok-mind 的回滾點（對話則數、壓縮次數、下一筆記憶的 id；新的查詢 `checkpoint`）。
  - 停在較早的一句時他再開口（打字或麥克風），就回到那一句之後的回滾點（`GalgameRollBack`）：
    - 截掉之後的記錄和事件歷史（事件歷史檔會重寫）；
    - 還原對話記憶和她的關係條目；
    - ok-mind 截掉之後的對話並刪掉之後新增的記憶（新的查詢 `rollback`；期間若做過對話壓縮，對話維持原樣，只刪記憶）。
  - 他自己那句也會被撤回，等於重新說一次。
  - 只有他自己說的話才會回滾。遊戲替她送的話（好感到了自動變朋友後她說「我們算朋友了」、結帳提示、走路時的情境）不是他的話：2026-10-26 以前，這種話剛好在他用滾輪往回看時送出，就會把之後的台詞當成他重說而撤掉。
  - 金錢、帳本、行程等其他遊戲狀態不回滾。

## 回歸測試（2026-10-25）

每次打包後一鍵驗證街頭 galgame：

```
Tools\lua\lua.exe Tools\galgame_regression.lua --launch [--girl 3] [--keep]
```

不加 `--launch` 時用本 checkout 控制埠上已在跑的遊戲。腳本叫女主過來、按 E 開始對話，逐項回報 PASS／FAIL／WARN，有 FAIL 時結束碼為 1：

| 項目 | 檢查 |
|---|---|
| scene | galgame 呈現接手對話（`/galgame` active、partner 是她） |
| pov | 她回答時是第一人稱鏡頭（presenter shot `pov`；被擋住時會換一般鏡頭，換個地方再跑） |
| portrait | 立繪顯示並已擷取 |
| text | 台詞整句顯示（已顯示字數 = 總字數） |
| skip | 她的語音台詞按 Space 跳過（log「skipped the line」） |
| wheel | 滾輪往上兩格，畫面回到之前的句子 |
| rollback | 回滾後再開口，對話回到那一句（log「rollback: back to line」，記錄與事件歷史縮短） |
| memory | 回滾也帶回 ok-mind（log 的 mind 部分不是 skip／fail） |
| forgot | 問她回滾掉的事實（生日），她提到就記 WARN（AI 回答，不算失敗） |

遊戲端為此新增控制埠 `GET /galgame`（呈現狀態：active、partner、lines、view_line、checkpoints、on_screen、portrait_captured、ok-story 的 state）與 `/state` 的 `all_ready`（每個角色都載好，不依顯示語言）。

回歸腳本 2026-10-27 新增兩項：

| 項目 | 檢查 |
|---|---|
| direction | 他說「（揮手）……」，語音伺服器收到的那句沒有括號內容（要用遊戲自己帶起的 ok-tts，讀 `Build/ok-tts/logs/ok-tts.log`；沒有這個 log 就記 WARN） |
| private | 加 `--private` 才做：存到 slot 9、進出一次 1 對 1，確認離開時 log 有 `extraction environment put back`；讀回 slot 9 後拍一張檢視截圖，用眼睛看她沒有黑色尖刺、衣服完整 |

## 名字照遊戲的字體（2026-10-27）

卡名是識別用，不能改；十位女主有五位的卡名是簡體。顯示時：

- 訊息參數裡鍵名含 `name` 的字串（`name`、`girl_name`、`place_name`……），由 ok-rules 的 `text` 轉成遊戲語言的字體（`locale.rs` `text_of`）。
- 不走訊息的幾處（小地圖標記、右側標題、女角名單、目前對象）用 `OKStreetScriptedName`。
- 餐點清單的主人用 `SpokenName`。

新的 UI 顯示女主名字時，用訊息參數或這兩個函式，不要直接接卡名。

## 群聊與走路時的演出（2026-10-27）

- 群聊時他的話也進對話框：`SendFreeTalk` 走 `SendPartyTalk` 之前先設定 `FreeTalkCaptionPlayer`。
- 群聊的模型回覆壞掉時：ok-mind 的 `chat` 在要求 JSON 時，先修補結尾（物件後面多出的文字、回聲的 `{"type": "json_object"}`、沒關的字串和括號），修不好就重問一次。
- 一起走路聊天時：演出中、沒有 Target、她在他 3 m 內，跟隨鏡頭的焦點半秒內平滑移到兩人中間（`GalgameWalkFocus`）。原本鏡頭只對著他，她說話時落在右側面板底下。
- 回放最近 20 句時，遊戲替她送的話（上桌、點餐、櫃檯通知、沉默搭話）會等回放播完。
- 舞台指示不念：送語音前拿掉 16 字以內的括號段（`OKStreetSpokenText.h`），她的分段語音和他的語音都用；對話框照常顯示。

## 1 對 1 場景（2026-10-27）

- 進場時收起街頭的對話框（`GalgameHideForPrivate`）。1 對 1 期間街頭不 Tick，`TickGalgame` 裡的隱藏判斷不會執行。
- 離開時她就在身邊（4 m 內）：用 `ResumeTalkQuiet` 接回對話。原本她回到街頭就照行程走掉。
- 取模環境變數：高模開卡時 `OKPrepareLiveGpuEnv` 會把 `OK_GPU_SKIN`、`OK_SKELMESH` 等改成整個程序都生效。現在進場前記下、離開時還原（`OKStreetSocialPrivateEnv.cpp`）。沒有還原時，之後讀檔或開新遊戲，所有女主都會畫壞。
- 效能：遊戲裡沒有任何 OK 配對包，所有雙人姿勢都走原生路徑。原生 GPU 求解現在也放到背景執行緒，下一幀顯示（`AKKPairedPlayback::Advance`、`ok.Anim.AsyncPair`）。1 對 1 從 77 FPS 提升到 185 FPS（GPU 上只有另一個遊戲時量的）。
- log：`clothes layer`、`shadow capture skipped`、`APNG source capture` 原本每幀都印，現在依實例或組合各只印一次。

### 量 1 對 1 的 Rust 雙人求解

1. 啟動時就開 trace：`game_launch {options: ["-Trace","default,cpu,frame","-GameArgs","-OKNativeTrace"]}`。Rust 的計時區段在第一次求解時登記，若事後才開 trace，會登記成空類型、全被丟掉。
2. 用 `UnrealInsights.exe -OpenTraceFile=<trace.utrace> -ExecOnAnalysisCompleteCmd="TimingInsights.ExportTimerStatistics out.csv" -AutoQuit -NoUI` 匯出，看 `KK_Native*` 各項。trace 動輒幾百 MB，看完就刪。

2026-10-27 量到的愛麗絲 1 對 1（109 FPS，胡桃 185），每幀：

- 取樣 3.3 ms：
  - 其中 `KK_NativeMotionLeaves` 每個角色 2.08 ms（兩人平行算），是各混合葉的骨架快照加原始曲線取樣。
  - `KK_NativeMotionPlan` 0.04 ms、`KK_NativeMotionBlend` 0.12 ms，都很小。
- 接觸 1 ms。
- 動態 3.6 ms，主要是飾品鏈。

要上 120 FPS，得重寫 `anim.rs` 葉取樣的資料結構（每片葉一份 HashMap 骨架、逐骨查表），尚未動手。

之後拆出 `KK_NativeLeafSnapshot`／`KK_NativeLeafChannels`／`KK_NativeChannelLookup`／`KK_NativeChannelSample`，找到真正原因是每次都從磁碟快取重讀 avatar 路徑表，並導致綁定與通道計畫快取永遠不中。改成依根目錄記在記憶體後（`anim.rs` `load_avatar_tos_shared`）：

- 每個角色的曲線取樣從 2.76 ms 降到 0.64 ms。
- 愛麗絲 1 對 1 從 109 FPS 提升到 128 FPS（不開 trace，GPU 上只有另一個 C 的遊戲）。

## 群聊約定、多人赴約與電梯鏡頭（2026-10-28）

用新存檔實玩雷電將軍、安可、惠惠（Gal1028B～G）：上午各自認識，中午在學生餐廳和惠惠、安可群聊，約傍晚五點半在百貨吃晚餐，下午在咖啡廳再約剛下班的將軍。

- **群聊約定**：群聊回覆加 `promise`（`who` 列出答應的人，其餘和判斷器的格式相同），`FinishPartyTalk` 對每位答應的人呼叫 `AddPromiseFromChat`。log：`Street party promise: day "+0" time "17:30" at department (...), 2 agreed`。
- **地點填成活動 id**：模型把「景觀餐廳」填成 `dept_view`（百貨裡的活動），被判成沒有這個地點，她還被告知餐廳不存在。`AddPromiseFromChat` 遇到活動 id 時改約在那個活動的地點（Gal1028J 驗證：群聊約「今晚七點景觀餐廳」，回覆填 `dept_view`，安可、惠惠各訂 19:00 大學城百貨）。
- **多人同時赴約**：一次只有一位「走過來」，其他人原本只印 `waits` 就沒有下文；現在排進 `ArriveLater` 下一分鐘重試（最多 60 分鐘），到時他正和別人說話就加入群聊。三人依序 `came for their meeting and joins the talk`，再一起上九樓美食街，群聊點餐各自上桌、各自結帳。
- **電梯裡的跟隨鏡頭**：她站在鏡頭和他之間、不到 1 m，畫面整片是頭髮。街頭跟隨鏡頭（不含對話鏡頭）前方 1 m、畫面中央 30° 內的女角暫時從主畫面隱藏（`PC->HiddenPrimitiveComponents`，`CameraHidden`），立繪照常。試過「被牆擠壓時升高往下看」：電梯車廂頂沒有碰撞，鏡頭穿出車頂。
- **只問推薦**：她推薦的品項，他沒說要就不算點餐（`ai_judge_prompt` orders 規則）。
- **對話框排版**：ok-story 的 `DialogOut` 多傳整句 `text`，`SOKStoryView` 依整句排版、只畫打出來的字（打字時字不會跳到下一行）；最後一行孤字不計標點（「淨。」」），從上一行拉下來的字帶著後面的標點（下一行不以「，」開頭）。
- 回歸腳本（Gal1028G 沙盒，惠惠 `--private` 12/12 通過）：`--sandbox NAME [--port-base N]` 以沙盒啟動；用 `--port` 指定別的遊戲時，TTS 埠由控制埠推算（控制埠－10）；找不到 PID 對應的 Play 資料夾時，以 log 的「listening on ...:<port>」認，不再選最新的資料夾。

### 三月七 1 對 1 載入慢的原因（2026-10-28）

同一負載下（另有 C 的遊戲在跑）重量：三月七取模 6.8 s（同程序第二次 1.7 s，和別人差不多），「貼圖」14.2 s、第二次仍 12.0 s（優香 5.6、安可 1.9）。`OK_LOAD_PROFILE=1` 細分：時間全在 `PrepareRuntimeMaterials`→`OKCaptureRuntimeMaterials`（`OK runtime material capture ms=10343`，安可 1145），貼圖解碼只有幾 ms。這一步逐張貼圖依序做 PNG 解碼、double 精度 box filter 的 mip 鏈與整份像素 SHA1；她的貼圖多又大，準備好的快取（256 MB 上限）也放不下，第二次照樣重做。

讀卡器早就量過平行版（`OK_FIRSTLOAD=C5`，材質準備 1.98 → 0.34 s），因為街頭群眾也走同一函式才預設關閉。1 對 1 的高模在背景執行緒載入、玩家看著載入畫面，所以 `LoadFemale` 先以平行方式準備材質（`OKCaptureRuntimeMaterialsParallel`，`FOkSession::PrepareRuntimeMaterials(Error, true)`），之後的 `PrecacheSessionTextures` 直接沿用。log 多一欄 `materials`。

Gal1028H（同樣有別的遊戲在跑）：三月七 materials 2.0 s＋textures 1.1 s（第二次 2.1＋0.1），安可 0.23＋0.15，優香 0.68＋0.59；1 對 1 截圖材質正確。整台乾淨時的數字還沒量。

### 群聊也能回滾（2026-10-28）

原本只有 1 對 1 能用滾輪回滾：檢查點只記說話對象一人，群聊時他的話走 `SendPartyTalk`，根本不記檢查點，滾回去再說話不會還原任何人。現在群聊時，他每句話前替在場每位各記一個檢查點（另存群聊最近的台詞 `PartyLog`）；滾回去後再說話，所有人一起回到那句之前（台詞紀錄、記憶、歷史、好感、ok-mind 的上下文），只和他說話的那位回去、其他人還記得的情況不再發生。

驗證（Gal1028J，安可＋惠惠在咖啡廳）：告訴兩人「喜歡橘色」與「生日三月十二」，滾回到惠惠回「橘色」那句後問「我有跟妳們說過生日嗎」：兩人各一行 `rollback: back to line 16`，安可「沒聽你說過耶」、惠惠「你根本沒提過吧」。

## 整合後新卡實玩與餐廳入座（2026-10-28 晚）

整合後（`Int1028cE`、新卡、`Base1028b`）用新存檔玩刻晴、貞德·Alter，順帶惠惠、雷電將軍、貞德、胡桃：

- **約會**：刻晴在圖書館答應「週六中午咖啡廳門口」（各付各的，`split pays`），隔天又約學生餐廳午餐，兩次都準時到；她知道他中午和貞德·Alter 一起吃過飯，嘴上調侃、`jealousy+2`。貞德·Alter 拒絕看電影（「太早了」），但答應一起走去學生餐廳。陌生人（惠惠、雷電將軍）不同桌，將軍在打工也婉拒，都合理。
- **坐好才開始吃**（116e65be8）：學生餐廳菜上桌時兩人還站在櫃台，走去座位加準備坐姿約 25 秒，`TickMeal` 照樣在吃，貞德·Alter 坐下時湯只剩 64%。現在她走去座位（`OutingGirl`）或還在準備坐姿（`Pair.Phase==0`）時先不吃。
- **座位旁被擋**（116e65be8）：走去座位時在桌邊卡住（0.6 m、1.55 m），每 4 秒重排路線、第 5 次才站過去（約 20 秒）。目標是路線最後一點且不到 2 m 時，第一次被擋就入座。
- 以上兩點在之後兩輪沒有重現（系統改成點餐時就先入座）；4 次入座吃飯都正常，修正路徑本身尚待遇到時確認。
- **用控制通道玩時菜不上桌**：不是 bug。控制通道在指令之間暫停時鐘，廚房跟著停（`ClockPaused` 時 `Serves.At` 往後延），要 `game_wait seconds` 讓時鐘跑。
- **結束對話後菜被清掉**：她依行程走出餐廳、他跟出去，桌子照規則清掉；她之後仍答應「回座位把湯喝完」，坐下時桌上沒有東西。模型不知道桌子已清，暫不處理。

### 三月七 1 對 1 載入（2026-10-28 晚，未乾淨）

Gal1028R，`/private girl 3`，兩次：card 185／224 ms、session 5073／5556 ms、materials 1197／1140 ms、textures 1029／165 ms；從按下到 `OK body interaction prepared` 約 14 s。當時另有 13 個 `ok-extract2` 批次轉卡占約 15 核，數字不算乾淨。

- CLI（`ok-card-open-bench`，80ef396f 匯出，同時只有兩個遊戲在跑）：三月七冷開 3.48 s、熱開 1.41 s；安可 1.95／0.62 s。遊戲裡第二次仍 5.5 s，可能是 CPU 被占滿，也可能是遊戲的開卡路徑不同，要在乾淨時重量。
- 高模裝配在遊戲執行緒上一幀一個部件：每個髮片約 60～150 ms（第二次貼圖已命中快取，仍約 100 ms），之後的 `OK private GPU installed` 每幀一個約 25 ms，`OK body interaction prepared` 單幀約 0.9 s。下一步錄 Unreal Insights（`game_trace_start`）找出髮片的成本。

### 三月七 1 對 1 進場的卡頓（2026-10-28 深夜，Gal1028T／U）

乾淨量測（雙 slot，背景只有一個約 2.2 核的轉卡）：Gal1028T 冷開 card 72 ms、session 2476 ms、materials 972 ms、textures 774 ms，按下到 `OK interaction entered` 約 9 s、到男角就緒約 12 s；同程序第二次約 7 s／9.5 s。CLI（`ok-card-open-bench`）冷開 3.48 s、熱開 1.41 s，遊戲裡熱開仍 2.5～2.7 s。

錄 Unreal Insights（`game_trace_start`，`UnrealInsights.exe -NoUI -AutoQuit -ExecOnAnalysisCompleteCmd="TimingInsights.ExportTimingEvents ..."` 匯出 CSV）找到兩個大卡頓：

- **平行材質準備把前景 worker 也占滿**：`OKCaptureRuntimeMaterialsParallel` 的 `ParallelFor`（PNG 解碼、mip、SHA1，2 s 內約 42 s CPU）在背景執行緒跑時，主執行緒 tick 結尾的 `WaitForTasks` 等了 1369 ms 與 436 ms。改用 `EParallelForFlags::BackgroundPriority`，前景 worker 留給遊戲；之後最長只剩 82 ms。
- **身體互動資料每次重讀**：`FOKBodyInteraction::Prepare` 每次進 1 對 1 都在主執行緒解析 8.5 MB 的 `catalog-v1.json`（`prepare_ms` 620～740）。改成程序內只解析一次（`WarmShared`），並在 `LoadFemale` 的背景工作先讀好；之後 `prepare_ms` 60（第一次）／3.5（第二次），log 拆成 data／colliders／reaction。
- 進場那一幀：1477 ms → 1099 ms（T）→ 568 ms（U）。剩下的是同步 `LoadObject`（`FlushAsyncLoading` 112 ms）、`KK_GalleryPairs` 103 ms，以及部件裝配期間的 `UTexture2D::UpdateResource`（`TryLoadMips`，每張最多約 50 ms）和一次 `Sync_RenderingThread` 810 ms。
- 配對清單（`NativeAnimationPairs`）改在 `LoadFemale` 的背景工作先取，`RefreshPairs` 沿用（圖庫自己的載入本來就這樣做）；Gal1028V 的 trace 裡 `KK_GalleryPairs`（86～103 ms）不見了。
- 背景優先權之後，平行解碼仍占滿全部背景 worker，渲染圖（RDG）的工作排在後面，渲染執行緒等了 590～810 ms。改成最多 `NumWorkers/4`（2～8）條通道、各自從 atomic 取下一張貼圖（解碼受記憶體頻寬限制：依序約 10 s，31 條約 1 s 但總 CPU 約 30 s）。Gal1028V：等渲染執行緒最長 810 → 220 ms、進場那一幀 568 → 471 ms、超過 15 ms 的幀合計 5.4 → 3.4 s；materials 約 1.0 → 2.1 s，按下到進入 1 對 1 仍約 8.5 s。
