# StreetSocial 生活經濟目錄（草案）

> **貨幣（2026-10-11 起）：** 遊戲改用人民幣（主角是中國人）。下面的表格仍以台幣撰寫；`Tools/build_street_social_catalog_json.lua` 產生 `catalog.json` 時以 4.7 換算並取整（100 元以上取 5 的倍數、1,000 元以上取 50 的倍數），時薪與門票也一樣。

這份文件搭配 [STREET_SOCIAL_HEROINE_AGENT_PLAN.md](STREET_SOCIAL_HEROINE_AGENT_PLAN.md) 的 §8 使用。
狀態：草案，待 review。之後會轉成 `Content/IndependentResources/StreetSocialData/catalog.json`。
日期：2026-10-11

## 0. 定價原則

- **幣值與時代：** 新台幣，2010 年台灣物價。基本工資為時薪 95 元、月薪 17,880 元，大學生每月生活費常見 6,000–12,000 元。
- **季節品：** 用 `season` 標註月份，只在那些月份販售。例如芒果冰是 5–9 月，草莓是 12–3 月。
- **有營業時間的地點：** 寫在 `open_hours`。打烊後不能進入，女主的日程也會避開。
- **品項效果：** 每個品項都有 `effects`，對應女主需求值（見主計畫 §8.2）：
  - `satiety` 飽足
  - `energy` 精神
  - `fun` 樂趣
  - `mood` 心情
  - `hygiene` 清潔
- **品項標籤：** `tags` 用來比對女主的喜好，例如 `sweet`、`spicy`、`cold`、`hot`、`alcohol`、`healthy`、`cute`、`luxury`、`romantic`、`gift`、`shareable`（可以分著吃或餵食）。
- **拿在手上的道具：** `prop` 決定角色手上拿什麼，第一版先沿用現有動作：

  | prop | 動作 |
  |---|---|
  | `cup` | 飲料，對應 drink 動作 |
  | `bowl`、`plate` | 餐點，對應 meal 動作 |
  | `cone` | 冰品 |
  | `bag` | 購物袋 |
  | `none` | 不拿東西 |

## 1. 地點經濟設定

**欄位說明：**

- `entry`：入場費
- `min`：低消（最低消費）
- `svc`：服務費
- `reserve`：要不要預約
- `treat`：常見的請客情境

**餐飲**

| place | 營業時間 | entry | min | svc | 備註 |
|---|---|---|---|---|---|
| restaurant 餐廳 | 11–22 | – | 包廂低消 1,000 | – | 包廂要預約（當天打電話也可以） |
| snack 點心店 | 10–22 | – | – | – | 可以外帶；生日蛋糕要前一天預訂 |
| drinkshop 飲料店 | 10–23 | – | – | – | 外帶為主，店內有少量座位 |
| cafe 咖啡廳 | 8–22 | – | 一杯飲料 | – | 可以久坐、讀書 |
| conveni 便利商店 | 24h | – | – | – | 有 ATM、繳費、手機儲值；門口有娃娃機 |
| cafeteria 學生餐廳 | 7–19 | – | – | – | 平日才開，週末只開半天（11–14） |
| fruitshop 水果店 | 9–21 | – | – | – | 現切水果盒、現打果汁、禮盒 |

**購物**

| place | 營業時間 | entry | min | svc | 備註 |
|---|---|---|---|---|---|
| bookstore 書局 | 10–22 | – | – | – | 可以站著看書 |
| department 百貨 | 11–21.5 | – | – | 10F 收 10% | 10F 晚餐建議預約 |
| petshop 寵物店 | 11–21 | 互動區 100 / 30 分 | – | – | 可以買寵物零食餵牠們 |
| lottery 彩券行（新） | 9–23 | – | – | – | 大樂透 50、威力彩 100、刮刮樂 100／200／500；運彩單張上限 10,000、每日 50,000（見主計畫 §15.1） |

**休閒與住宿**

| place | 營業時間 | entry | min | svc | 備註 |
|---|---|---|---|---|---|
| aquarium 水族館 | 9–18 | 全票 450 / 學生 350 | – | – | 年票 1,200 |
| hotel 飯店 | 24h | 泳池 500（住客免費） | – | 10% | 客房、SPA、酒吧 |
| lodge 會館 | 10–02 | – | – | – | 休息、精油、過夜 |
| minshuku 民宿 | 入住 15、退房 11 | – | – | – | 一泊含早餐 |
| onsen 露天溫泉 | 6–24 | 350 | – | – | 有家族湯可包場 |

**免費地點**

| place | 營業時間 | 備註 |
|---|---|---|
| library 圖書館 | 8–21（週一休館） | 免費；影印 1 元／張；逾期每天每本罰 5 元 |
| lecture、school | 7–22 | 免費；學生活動 |
| playground、park | 24h | 免費 |
| shrine 神社 | 24h（社務所 9–17） | 香油錢自由給 |
| girls_dorm、boys_dorm | 門禁 23:00–06:00 | 交誼廳有販賣機；有洗衣房 |
| restroom_* | 24h | 免費 |

## 2. 菜單

### 2.1 restaurant 餐廳（家庭式西餐和定食）

| id | 名稱 | 價格 | tags | prop |
|---|---|---|---|---|
| rest_porkcutlet | 日式豬排定食 | 220 | hot, filling | plate |
| rest_teriyaki | 照燒雞腿排餐 | 240 | hot | plate |
| rest_hamburg | 漢堡排定食 | 230 | hot | plate |
| rest_omurice | 咖哩蛋包飯 | 200 | hot, cute | plate |
| rest_carbonara | 奶油培根義大利麵 | 220 | hot | plate |
| rest_seafood_pasta | 海鮮番茄義大利麵 | 260 | hot | plate |
| rest_steak | 菲力牛排套餐 | 480 | hot, luxury | plate |
| rest_caesar | 凱薩沙拉 | 150 | healthy, cold | bowl |
| rest_corn_soup | 玉米濃湯 | 60 | hot | bowl |
| rest_pumpkin_soup | 南瓜濃湯 | 70 | hot, healthy | bowl |
| rest_fries | 薯條（可分享） | 80 | shareable | plate |
| rest_nuggets | 炸雞塊（可分享） | 120 | shareable | plate |
| rest_tiramisu | 提拉米蘇 | 120 | sweet | plate |
| rest_pudding | 焦糖布丁 | 60 | sweet, cute | plate |
| rest_blacktea | 紅茶／綠茶 | 40 | – | cup |
| rest_oj | 柳橙汁 | 70 | cold, healthy | cup |
| rest_coffee | 熱咖啡 | 80 | hot | cup |
| rest_set_upgrade | 套餐升級（湯 + 飲料 + 甜點） | +90 | – | – |
| rest_couple_set | 包廂雙人套餐 | 1,280 | romantic, luxury | plate |

### 2.2 snack 點心店

| id | 名稱 | 價格 | season | tags |
|---|---|---|---|---|
| snk_waffle | 鬆餅 | 120 | – | sweet, shareable |
| snk_strawberry_cake | 草莓鮮奶油蛋糕 | 90 | – | sweet, cute |
| snk_cheesecake | 重乳酪蛋糕 | 85 | – | sweet |
| snk_puff | 泡芙 | 45 | – | sweet |
| snk_dorayaki | 銅鑼燒 | 35 | – | sweet |
| snk_redbean_cake | 紅豆餅（2 個） | 30 | – | sweet, hot |
| snk_egg_cake | 雞蛋糕（一袋） | 40 | – | sweet, shareable |
| snk_douhua | 豆花 | 40 | – | sweet, healthy |
| snk_grass_jelly | 燒仙草 | 45 | 11–3 | sweet, hot |
| snk_mango_ice | 芒果冰 | 120 | 5–9 | sweet, cold, shareable |
| snk_shaved_ice | 八寶冰 | 70 | 4–10 | sweet, cold, shareable |
| snk_bubble_tea | 珍珠奶茶 | 50 | – | sweet, cold |
| snk_lemon_tea | 檸檬紅茶 | 35 | – | cold |
| snk_cocoa | 熱可可 | 60 | 10–4 | sweet, hot |
| snk_bday_cake | 6 吋生日蛋糕（預訂，隔天取） | 450 | – | gift, romantic |
| snk_gift_box | 手工餅乾禮盒 | 280 | – | gift |

### 2.3 drinkshop 飲料店（手搖）

| id | 名稱 | M / L | tags |
|---|---|---|---|
| dk_black | 紅茶 | 20 / 25 | – |
| dk_green | 綠茶 | 20 / 25 | healthy |
| dk_oolong | 烏龍青茶 | 25 / 30 | healthy |
| dk_milk_tea | 奶茶 | 35 / 45 | sweet |
| dk_pearl_milk | 珍珠奶茶 | 40 / 50 | sweet |
| dk_boba_green | 波霸奶綠 | 45 / 55 | sweet |
| dk_pudding_milk | 布丁奶茶 | 50 / 60 | sweet, cute |
| dk_fresh_milk | 鮮奶茶 | 55 / 65 | sweet |
| dk_yakult_green | 多多綠 | 40 / 50 | sweet |
| dk_passion_green | 百香綠 | 45 / 55 | sweet |
| dk_lemon_green | 檸檬綠 | 40 / 50 | – |
| dk_wintermelon_lemon | 冬瓜檸檬 | 40 / 50 | sweet |
| dk_grapefruit_green | 葡萄柚綠 | 55 / 65 | healthy |
| dk_mango_smoothie | 芒果冰沙 | 60 / – | sweet, cold |
| dk_strawberry_smoothie | 草莓冰沙 | 60 / – | sweet, cold, cute |

**客製選項：**

| 選項 | 可選值 | 加價 |
|---|---|---|
| 甜度 | 全糖、少糖、半糖、微糖、無糖 | – |
| 冰塊 | 正常、少冰、微冰、去冰、溫、熱 | – |
| 加料 | 珍珠、椰果、布丁、仙草 | +10 |

女主的甜度和冰塊偏好寫在個人設定裡。點對了是「記得她喜好」的主要來源（見主計畫 §8.4）。

### 2.4 cafe 咖啡廳

| id | 名稱 | 價格 | tags |
|---|---|---|---|
| cf_americano | 美式咖啡 | 80 | hot |
| cf_latte | 拿鐵 | 110 | – |
| cf_cappuccino | 卡布奇諾 | 110 | – |
| cf_caramel | 焦糖瑪奇朵 | 130 | sweet |
| cf_mocha | 摩卡 | 130 | sweet |
| cf_matcha_latte | 抹茶拿鐵 | 120 | sweet |
| cf_cocoa | 熱可可 | 100 | sweet |
| cf_earl_grey | 伯爵茶 | 100 | – |
| cf_fruit_tea | 熱帶水果茶（壺） | 180 | shareable |
| cf_tiramisu | 提拉米蘇 | 130 | sweet |
| cf_lemon_tart | 檸檬塔 | 110 | sweet |
| cf_scone | 司康佐果醬 | 70 | – |
| cf_panini | 帕尼尼 | 150 | hot, filling |
| cf_waffle | 冰淇淋鬆餅 | 160 | sweet, shareable |

### 2.5 conveni 便利商店

**飲料與食物**

| id | 名稱 | 價格 | 分類 |
|---|---|---|---|
| cv_water | 礦泉水 | 20 | 飲料 |
| cv_tea | 瓶裝茶 | 25 | 飲料 |
| cv_coffee | 現煮咖啡 | 45 | 飲料 |
| cv_milk | 鮮奶 | 30 | 飲料 |
| cv_yogurt | 優酪乳 | 35 | 飲料 |
| cv_beer | 啤酒（≥18） | 45 | 飲料、alcohol |
| cv_tea_egg | 茶葉蛋 | 8 | 熱食 |
| cv_oden | 關東煮（每串） | 10–25 | 熱食 |
| cv_hotdog | 大亨堡 | 35 | 熱食 |
| cv_onigiri | 御飯糰 | 25 | 輕食 |
| cv_bento | 微波便當 | 65 | 餐 |
| cv_noodle | 泡麵 | 30 | 餐 |
| cv_chips | 洋芋片 | 30 | 零食、shareable |
| cv_choco | 巧克力 | 25 | 零食、gift |
| cv_icebar | 冰棒 | 25 | 冰品 |

**日用品與服務**

| id | 名稱 | 價格 | 分類 |
|---|---|---|---|
| cv_umbrella | 雨傘 | 99 | 日用（下雨時） |
| cv_medicine | 成藥、喉糖、OK 繃 | 40–120 | 日用（探病、受傷） |
| cv_magazine | 雜誌 | 79 | 書報 |
| cv_topup | 手機儲值 | 100 / 300 / 500 | 服務 |
| cv_atm | ATM 提款 | 跨行 6 元 | 服務 |
| cv_claw | 娃娃機 | 10／次 | 遊戲（機率夾到小布偶） |

### 2.6 cafeteria 學生餐廳

| id | 名稱 | 價格 |
|---|---|---|
| caf_buffet | 自助餐（依取菜） | 60–90 |
| caf_luroufan | 滷肉飯 | 30 |
| caf_drumstick | 雞腿飯 | 75 |
| caf_porkchop | 排骨飯 | 70 |
| caf_beef_noodle | 牛肉麵 | 90 |
| caf_plain_noodle | 陽春麵 | 35 |
| caf_dumplings | 水餃 10 顆 | 50 |
| caf_soup | 貢丸湯 | 25 |
| caf_blacktea | 紅茶 | 15 |

### 2.7 fruitshop 水果店

| id | 名稱 | 價格 | season |
|---|---|---|---|
| fr_cut_box_s / l | 綜合水果盒 小／大 | 50 / 80 | – |
| fr_juice | 現打果汁 | 50–70 | – |
| fr_watermelon | 西瓜半顆（可帶去操場打西瓜） | 120 | 5–9 |
| fr_mango | 愛文芒果 3 顆 | 100 | 5–8 |
| fr_strawberry | 大湖草莓一盒 | 150 | 12–3 |
| fr_grape | 巨峰葡萄一串 | 100 | 6–10 |
| fr_banana | 香蕉一串 | 40 | – |
| fr_apple | 蘋果 | 25 | – |
| fr_gift_box | 水果禮盒（探病、拜訪長輩） | 600 / 1,200 | – |

### 2.8 department 大學城百貨

**1F 化妝品**

| 品項 | 價格 | tags |
|---|---|---|
| 口紅 | 800–1,200 | gift, luxury |
| 香水 | 2,500 | gift, luxury, romantic |
| 保養組 | 1,800 | gift |
| 護手霜 | 350 | gift |

**2F 女裝**

| 品項 | 價格 | tags |
|---|---|---|
| 上衣 | 690 | gift |
| 裙子 | 980 | gift |
| 洋裝 | 1,980 | gift, cute |
| 外套 | 2,980 | gift |

**3F 男裝**

| 品項 | 價格 | tags |
|---|---|---|
| 襯衫 | 990 | 玩家自用 |
| 外套 | 3,280 | 玩家自用 |

**4F 鞋包配件**

| 品項 | 價格 | tags |
|---|---|---|
| 帆布鞋 | 1,580 | gift |
| 高跟鞋 | 2,680 | gift |
| 錢包 | 1,680 | gift |
| 項鍊 | 1,280 | gift, romantic |
| 手錶 | 3,800 | gift |
| 包包 | 2,980 | gift |
| 名牌包 | 12,800 | luxury（拜金型的關鍵禮物） |

**5F 家居**

| 品項 | 價格 | tags |
|---|---|---|
| 抱枕 | 490 | gift, cute |
| 馬克杯 | 290 | gift |
| 香氛蠟燭 | 450 | gift, romantic |

**6F 書店文具：** 同書局（§2.10）。

**7F 電器**

| 品項 | 價格 | 備註 |
|---|---|---|
| iPhone 4 16G | 21,900 | 2010-09 在台開賣，可以當預言話題 |
| HTC Desire | 15,900 | – |
| 數位相機 | 8,990 | – |
| 耳機 | 1,290 | – |

**8F 玩具**

| 品項 | 價格 | tags |
|---|---|---|
| 小熊布偶 | 390 | gift, cute |
| 大熊布偶 | 1,280 | gift, cute, romantic |
| 拼圖 | 590 | – |
| 娃娃機 | 10／次 | – |

**9F 美食街**

| 品項 | 價格 |
|---|---|
| 豚骨拉麵 | 180 |
| 石鍋拌飯 | 160 |
| 日式咖哩飯 | 150 |
| 港式燒臘 | 140 |
| 鐵板燒 | 220 |
| 甜甜圈 | 35 |

**10F 景觀餐廳**（另收 10% 服務費）

| 品項 | 價格 | tags |
|---|---|---|
| 雙人景觀套餐 | 2,400 | romantic, luxury |
| 單點牛排 | 980 | – |
| 杯裝紅酒 | 280 | alcohol |
| 熱咖啡 | 180 | hot |
| 熱紅茶 | 160 | hot |
| 甜點 | 220 | sweet, romantic |

### 2.9 hotel 溫泉 SPA 飯店

| 品項 | 價格 | tags |
|---|---|---|
| 景觀雙人房 | 4,800／晚 | 含泳池和早餐 |
| 休息 3 小時 | 1,800 | – |
| SPA 90 分 | 2,800／人 | – |
| 泳池日券 | 500 | – |
| 雞尾酒 | 320 | alcohol |
| 啤酒 | 180 | alcohol |
| 無酒精特調 | 220 | – |
| 起司拼盤 | 380 | shareable |

### 2.10 bookstore 書局

| 品項 | 價格 | 備註 |
|---|---|---|
| 漫畫 | 90 | – |
| 雜誌 | 120 | – |
| 小說 | 280 | gift |
| 考試參考書 | 350 | 期中、期末前需求上升 |
| 筆記本 | 30 | – |
| 原子筆 | 15 | – |
| 文具組 | 150 | gift |
| 明信片 | 20 | 可以寫字送人 |
| 包書套服務 | 免費 | – |

### 2.11 其他地點

| place | 品項與價格 |
|---|---|
| lodge 會館 | 休息房 2 小時 600；精油護理 60 分 1,500；過夜（鋪棉被）1,200 |
| minshuku 民宿 | 一泊二食 2,400／房；抹茶配和菓子 150 |
| onsen 露天溫泉 | 入浴（門票，見 §1）；浴巾租借 50；泡完喝的瓶裝牛奶 35；家族湯包場 50 分 1,200 |
| aquarium 水族館 | 門票（見 §1）；海豚布偶 450；鑰匙圈 150；餵食秀免費 |
| petshop 寵物店 | 互動區 100；寵物零食 50；逗貓棒 80；項圈 200 |
| shrine 神社 | 香油錢自由給；抽籤 20；御守 200（戀愛、學業、健康）；繪馬 100 |
| park 公園 | 魚飼料 10；其他免費 |
| playground 操場 | 免費；西瓜要自己帶（水果店買） |
| dorm 宿舍 | 洗衣 20、烘衣 20；販賣機飲料 25 |

## 3. 打工職缺（所有商家都可以打工）

- 玩家和女主都可以應徵。時薪以 2010 年基本工資 95 元為基準。
- 打工的人在班表時間內**必須待在打工地點**，詳細規則見主計畫 §8.6。
- `stations` 是上班時的工作點（anchor）：
  - 寫「新增」的，要在 `Tools/street_models/` 的 okmesh 補上 staff anchor。這只是在原有建築模型裡加點位，不算新增地點。
  - 其他沿用既有 anchor。

**餐飲**

| place | job id | 職位 | 時薪 | 班別 | stations | 工作動作 |
|---|---|---|---|---|---|---|
| conveni | conveni_clerk | 店員 | 95（大夜 110） | 早 7–15、晚 15–23、大夜 23–07 | 收銀台（新增）、`aisle` 上架、熱食櫃（新增） | 收銀、上架、補貨、加熱便當 |
| drinkshop | drinkshop_clerk | 店員 | 95 | 11–17、17–23 | `counter` 內側（新增）、調飲台（新增） | 點單、搖飲料、封膜 |
| cafe | cafe_barista | 咖啡師 | 100 | 8–14、14–22 | 吧台（新增） | 煮咖啡、拉花 |
| cafe | cafe_floor | 外場 | 100 | 8–14、14–22 | `seat` 周邊、`counter` | 點餐、端盤、收桌 |
| restaurant | restaurant_floor | 外場 | 100 | 11–15、17–22 | `table`、包廂門口、櫃台（新增） | 帶位、點餐、上菜、收桌 |
| restaurant | restaurant_kitchen | 內場助手 | 100 | 11–15、17–22 | 廚房（新增） | 洗菜、裝盤、洗碗 |
| snack | snack_clerk | 店員 | 95 | 10–16、16–22 | `counter`、`table` | 結帳、裝蛋糕、做冰品 |
| cafeteria | cafeteria_worker | 學餐工讀 | 95 | 11–14、17–19（平日） | 取餐檯（新增） | 打菜、收餐盤 |
| fruitshop | fruit_clerk | 店員 | 95 | 9–15、15–21 | `stand`、切水果台（新增） | 秤重、切水果盒、打果汁 |

**零售與服務**

| place | job id | 職位 | 時薪 | 班別 | stations | 工作動作 |
|---|---|---|---|---|---|---|
| bookstore | bookstore_clerk | 店員 | 95 | 10–16、16–22 | `shelf`、收銀台（新增） | 上架、包書套、結帳 |
| petshop | petshop_clerk | 店員 | 95 | 11–16、16–21 | `pets`、互動區（新增） | 餵食、清潔、陪客人互動 |
| lottery | lottery_clerk | 彩券行店員 | 95 | 9–16、16–23 | `counter`、`scratch` | 投注、兌獎、賣刮刮樂 |
| department 1F | dept_beauty | 化妝品專櫃 | 105 | 11–16、16–21.5 | `beauty` | 試妝介紹、結帳 |
| department 2F | dept_women | 女裝店員 | 100 | 同上 | `mirror2` | 摺衣、試衣間引導 |
| department 3F | dept_men | 男裝店員 | 100 | 同上 | `mirror3` | 摺衣、量尺寸 |
| department 4F | dept_shoes | 鞋包店員 | 100 | 同上 | `shoes` | 幫客人試穿 |
| department 5F | dept_home | 家居店員 | 95 | 同上 | `sofa` | 介紹、整理展示品 |
| department 6F | dept_books | 書店店員 | 95 | 同上 | `books` | 上架、結帳 |
| department 7F | dept_phones | 電器店員 | 105 | 同上 | `phones` | 示範手機、結帳 |
| department 8F | dept_toys | 玩具店員 | 95 | 同上 | `bear`、娃娃機 | 補娃娃、示範玩具 |
| department 9F | dept_food | 美食街店員 | 95 | 11–15、17–21.5 | `food` | 點餐、出餐、收盤 |
| department 10F | dept_view | 景觀餐廳服務生 | 110 | 17–21.5 | `view` | 帶位、上菜、倒酒 |

**休閒與住宿**

| place | job id | 職位 | 時薪 | 班別 | stations | 工作動作 |
|---|---|---|---|---|---|---|
| aquarium | aquarium_staff | 售票、導覽 | 100 | 9–14、13–18 | 售票口（新增）、`tank` | 售票、導覽解說、餵食秀助手 |
| hotel | hotel_front | 櫃台 | 110 | 7–15、15–23、23–07 | 櫃台（新增） | check-in、接電話 |
| hotel | hotel_bar | 酒吧服務生 | 120 | 18–02 | `bar` | 調酒、送酒 |
| hotel | hotel_pool | 泳池救生、服務 | 110 | 9–18（夏季） | `pool` | 巡視、送毛巾 |
| hotel | hotel_spa | SPA 助理 | 120 | 12–22 | SPA 室 | 準備房間、接待 |
| lodge | lodge_front | 會館櫃台 | 100 | 10–18、18–02 | 櫃台（新增） | 接待、整理休息房 |
| minshuku | inn_helper | 民宿幫手 | 95 | 7–11、15–21 | `seat`、廚房（新增） | 端早餐、整理和室、泡茶 |
| onsen | onsen_staff | 溫泉櫃台 | 95 | 6–14、14–24 | 櫃台（新增） | 收票、發浴巾、賣牛奶 |

**校內工讀與家教**

| place | job id | 職位 | 時薪 | 班別 | stations | 工作動作 |
|---|---|---|---|---|---|---|
| library | library_worker | 圖書館工讀 | 95 | 8–13、13–21（只限學生） | `shelf`、借還書櫃台（新增） | 還書上架、借書登記 |
| school / lecture | campus_assistant | 系辦工讀 | 95 | 9–12、13–17（只限學生） | `seat` | 整理文件、接電話 |
| （到府） | tutor | 家教 | 400–600 | 晚上 2 小時 | 不在地圖上，抽象處理 | 只能快轉 |

**店長：**

- **預設：** 店長只出現在對話框和手機訊息裡，不生成角色。面試、排班、出聲提醒、評價，都用「店長：……」的對話框或手機訊息呈現。
- **例外：** 某位女主的設定是這家店的店長時（`heroines.json` 的 `occupation.role = manager`，`workplace_place_id` 指向這家店），就由她本人擔任。規則見主計畫 §8.6.7。

**同班互動：** 和女主排到同一班時就會發生，內容見主計畫 §8.6：

- 邊做邊聊
- 一起收店
- 吃員工餐
- 互相掩護
- 換班
- 下班一起走

這是開放世界裡最自然的相遇方式之一。

## 4. `catalog.json` 結構

```json
{
  "version": 1,
  "currency": "TWD",
  "places": {
    "drinkshop": {"open": [[10,23]], "entry": 0, "min_charge": 0, "service_fee": 0, "jobs": ["drinkshop_clerk"]}
  },
  "items": [
    {"id":"dk_pearl_milk","place":"drinkshop","name":"珍珠奶茶","category":"drink",
     "price":{"M":40,"L":50},"options":["sweetness","ice","topping"],
     "season":null,"tags":["sweet","cold"],"prop":"cup",
     "effects":{"satiety":10,"energy":5,"mood":5},"giftable":false,"consumable":true}
  ],
  "jobs": [{"id":"drinkshop_clerk","place":"drinkshop","title":"店員","wage":95,"shifts":[[11,17],[17,23]],
            "stations":["drinkshop_staff_counter","drinkshop_mixer"],"tasks":["take_order","shake","seal"],
            "student_only":false,"min_competence":0}]
}
```

- 載入時由 UE 驗證：
  - place id 必須存在。
  - 價格必須大於 0。
  - `prop` 必須是已知值。
- Rust 端讀同一份檔案，用來生成 prompt 的菜單文字，以及讓女主自己決定點什麼。
