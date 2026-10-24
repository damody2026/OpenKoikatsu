-- Writes Content/IndependentResources/StreetSocialData/catalog.json from Docs/STREET_SOCIAL_LIFE_CATALOG.md's tables.
-- OUT OF DATE (2026-10-24): catalog.json (version 2, yuan, with the picture books, novels and floors) has been edited
-- directly since; running this writes the older catalog over it. Edit catalog.json instead.
--
-- Dev-time tool (the game reads the JSON). Edit the tables below together with the doc, then run:
--   Tools\lua\lua.exe Tools/build_street_social_catalog_json.lua
local ok = require "ok"
local json, path, util = ok.json, ok.path, ok.util
local O = json.object

local OUT = ok.root .. "/Content/IndependentResources/StreetSocialData/catalog.json"

-- place: open hours [[from, to]] (24h, to may pass 24), entry fee, minimum charge, service fee %, closed weekdays (0=Mon),
-- serve_s: seconds from the till to the table (default 30: a kitchen; off the shelf it is handed over at once)
local PLACES = O({
  { "restaurant", O({ { "open", { { 11, 22 } } } }) },
  { "snack", O({ { "open", { { 10, 22 } } } }) },
  { "drinkshop", O({ { "open", { { 10, 23 } } } }) },
  { "cafe", O({ { "open", { { 8, 22 } } }, { "min_charge", 1 } }) },
  { "conveni", O({ { "open", { { 0, 24 } } }, { "serve_s", 3 } }) },
  { "cafeteria", O({ { "open", { { 7, 19 } } }, { "weekend_open", { { 11, 14 } } } }) },
  { "fruitshop", O({ { "open", { { 9, 21 } } }, { "serve_s", 3 } }) },
  { "bookstore", O({ { "open", { { 10, 22 } } } }) },
  { "department", O({ { "open", { { 11, 21.5 } } }, { "service_floor10", 10 } }) },
  { "petshop", O({ { "open", { { 11, 21 } } } }) },
  { "aquarium", O({ { "open", { { 9, 18 } } }, { "entry", 450 }, { "entry_student", 350 } }) },
  { "hotel", O({ { "open", { { 0, 24 } } }, { "service", 10 } }) },
  { "lodge", O({ { "open", { { 10, 26 } } } }) },
  { "minshuku", O({ { "open", { { 0, 24 } } } }) },
  { "onsen", O({ { "open", { { 6, 24 } } }, { "entry", 350 } }) },
  { "library", O({ { "open", { { 8, 21 } } }, { "closed_weekdays", { 0 } } }) },
  { "lecture", O({ { "open", { { 7, 22 } } } }) },
  { "school", O({ { "open", { { 7, 22 } } } }) },
  { "playground", O({ { "open", { { 0, 24 } } } }) },
  { "park", O({ { "open", { { 0, 24 } } } }) },
  { "shrine", O({ { "open", { { 0, 24 } } } }) },
  { "girls_dorm", O({ { "open", { { 0, 24 } } }, { "curfew", { 23, 6 } } }) },
  { "boys_dorm", O({ { "open", { { 0, 24 } } }, { "curfew", { 23, 6 } } }) },
  { "lottery", O({ { "open", { { 9, 23 } } } }) },
  { "campus_t1", O({ { "open", { { 7, 22 } } } }) },
  { "campus_rb", O({ { "open", { { 7, 22 } } } }) },
  { "campus_ib", O({ { "open", { { 7, 22 } } } }) },
  { "campus_t4", O({ { "open", { { 7, 22 } } } }) },
  { "campus_e1", O({ { "open", { { 7, 22 } } } }) },
  { "campus_ma", O({ { "open", { { 7, 22 } } } }) },
  { "campus_t2", O({ { "open", { { 7, 22 } } } }) },
  { "campus_ee", O({ { "open", { { 7, 22 } } } }) },
  { "campus_ad", O({ { "open", { { 8, 17 } } } }) },
  { "campus_gate", O({ { "open", { { 0, 24 } } } }) },
  { "campus_sports", O({ { "open", { { 6, 22 } } } }) },
})

-- Default effects by category (plan §8.1): satiety, energy, fun, mood, hygiene.
local EFFECTS = O({
  { "meal", O({ { "satiety", 45 }, { "mood", 5 } }) },
  { "light", O({ { "satiety", 20 }, { "mood", 5 } }) },
  { "dessert", O({ { "satiety", 15 }, { "mood", 12 } }) },
  { "drink", O({ { "satiety", 5 }, { "mood", 6 } }) },
  { "coffee", O({ { "satiety", 3 }, { "energy", 20 }, { "mood", 4 } }) },
  { "alcohol", O({ { "satiety", 3 }, { "mood", 10 }, { "energy", -5 } }) },
  { "fruit", O({ { "satiety", 12 }, { "mood", 6 } }) },
  { "goods", O({  }) },
  { "gift", O({  }) },
  { "service", O({  }) },
  { "leisure", O({ { "fun", 30 }, { "mood", 8 } }) },
  { "spa", O({ { "energy", 25 }, { "hygiene", 30 }, { "fun", 15 }, { "mood", 10 } }) },
  { "bath", O({ { "energy", 20 }, { "hygiene", 50 }, { "mood", 8 } }) },
  { "stay", O({ { "energy", 60 }, { "hygiene", 20 } }) },
  { "game", O({ { "fun", 10 } }) },
})

-- (id, place, name, price or {size = price}, category, tags, prop, season months or false)
local ITEMS = {
  -- restaurant
  { "rest_porkcutlet", "restaurant", "日式豬排定食", 220, "meal", "hot,filling", "plate", false },
  { "rest_teriyaki", "restaurant", "照燒雞腿排餐", 240, "meal", "hot", "plate", false },
  { "rest_hamburg", "restaurant", "漢堡排定食", 230, "meal", "hot", "plate", false },
  { "rest_omurice", "restaurant", "咖哩蛋包飯", 200, "meal", "hot,cute", "plate", false },
  { "rest_carbonara", "restaurant", "奶油培根義大利麵", 220, "meal", "hot", "plate", false },
  { "rest_seafood_pasta", "restaurant", "海鮮番茄義大利麵", 260, "meal", "hot", "plate", false },
  { "rest_steak", "restaurant", "菲力牛排套餐", 480, "meal", "hot,luxury", "plate", false },
  { "rest_caesar", "restaurant", "凱薩沙拉", 150, "light", "healthy,cold", "bowl", false },
  { "rest_corn_soup", "restaurant", "玉米濃湯", 60, "light", "hot", "bowl", false },
  { "rest_pumpkin_soup", "restaurant", "南瓜濃湯", 70, "light", "hot,healthy", "bowl", false },
  { "rest_fries", "restaurant", "薯條", 80, "light", "shareable", "plate", false },
  { "rest_nuggets", "restaurant", "炸雞塊", 120, "light", "shareable", "plate", false },
  { "rest_tiramisu", "restaurant", "提拉米蘇", 120, "dessert", "sweet", "plate", false },
  { "rest_pudding", "restaurant", "焦糖布丁", 60, "dessert", "sweet,cute", "plate", false },
  { "rest_blacktea", "restaurant", "紅茶／綠茶", 40, "drink", "", "cup", false },
  { "rest_oj", "restaurant", "柳橙汁", 70, "drink", "cold,healthy", "cup", false },
  { "rest_coffee", "restaurant", "熱咖啡", 80, "coffee", "hot", "cup", false },
  { "rest_couple_set", "restaurant", "包廂雙人套餐", 1280, "meal", "romantic,luxury,shareable", "plate", false },
  -- snack
  { "snk_waffle", "snack", "鬆餅", 120, "dessert", "sweet,shareable", "plate", false },
  { "snk_strawberry_cake", "snack", "草莓鮮奶油蛋糕", 90, "dessert", "sweet,cute", "plate", false },
  { "snk_cheesecake", "snack", "重乳酪蛋糕", 85, "dessert", "sweet", "plate", false },
  { "snk_puff", "snack", "泡芙", 45, "dessert", "sweet", "plate", false },
  { "snk_dorayaki", "snack", "銅鑼燒", 35, "dessert", "sweet", "plate", false },
  { "snk_redbean_cake", "snack", "紅豆餅（2 個）", 30, "dessert", "sweet,hot", "plate", false },
  { "snk_egg_cake", "snack", "雞蛋糕（一袋）", 40, "dessert", "sweet,shareable", "bag", false },
  { "snk_douhua", "snack", "豆花", 40, "dessert", "sweet,healthy", "bowl", false },
  { "snk_grass_jelly", "snack", "燒仙草", 45, "dessert", "sweet,hot", "bowl", { 11, 12, 1, 2, 3 } },
  { "snk_mango_ice", "snack", "芒果冰", 120, "dessert", "sweet,cold,shareable", "bowl", { 5, 6, 7, 8, 9 } },
  { "snk_shaved_ice", "snack", "八寶冰", 70, "dessert", "sweet,cold,shareable", "bowl", { 4, 5, 6, 7, 8, 9, 10 } },
  { "snk_bubble_tea", "snack", "珍珠奶茶", 50, "drink", "sweet,cold", "cup", false },
  { "snk_lemon_tea", "snack", "檸檬紅茶", 35, "drink", "cold", "cup", false },
  { "snk_cocoa", "snack", "熱可可", 60, "drink", "sweet,hot", "cup", { 10, 11, 12, 1, 2, 3, 4 } },
  { "snk_bday_cake", "snack", "6 吋生日蛋糕（預訂，隔天取）", 450, "gift", "gift,romantic", "bag", false },
  { "snk_gift_box", "snack", "手工餅乾禮盒", 280, "gift", "gift", "bag", false },
  -- drinkshop (M / L)
  { "dk_black", "drinkshop", "紅茶", O({ { "M", 20 }, { "L", 25 } }), "drink", "", "cup", false },
  { "dk_green", "drinkshop", "綠茶", O({ { "M", 20 }, { "L", 25 } }), "drink", "healthy", "cup", false },
  { "dk_oolong", "drinkshop", "烏龍青茶", O({ { "M", 25 }, { "L", 30 } }), "drink", "healthy", "cup", false },
  { "dk_milk_tea", "drinkshop", "奶茶", O({ { "M", 35 }, { "L", 45 } }), "drink", "sweet", "cup", false },
  { "dk_pearl_milk", "drinkshop", "珍珠奶茶", O({ { "M", 40 }, { "L", 50 } }), "drink", "sweet", "cup", false },
  { "dk_boba_green", "drinkshop", "波霸奶綠", O({ { "M", 45 }, { "L", 55 } }), "drink", "sweet", "cup", false },
  { "dk_pudding_milk", "drinkshop", "布丁奶茶", O({ { "M", 50 }, { "L", 60 } }), "drink", "sweet,cute", "cup", false },
  { "dk_fresh_milk", "drinkshop", "鮮奶茶", O({ { "M", 55 }, { "L", 65 } }), "drink", "sweet", "cup", false },
  { "dk_yakult_green", "drinkshop", "多多綠", O({ { "M", 40 }, { "L", 50 } }), "drink", "sweet", "cup", false },
  { "dk_passion_green", "drinkshop", "百香綠", O({ { "M", 45 }, { "L", 55 } }), "drink", "sweet", "cup", false },
  { "dk_lemon_green", "drinkshop", "檸檬綠", O({ { "M", 40 }, { "L", 50 } }), "drink", "", "cup", false },
  { "dk_wintermelon_lemon", "drinkshop", "冬瓜檸檬", O({ { "M", 40 }, { "L", 50 } }), "drink", "sweet", "cup", false },
  { "dk_grapefruit_green", "drinkshop", "葡萄柚綠", O({ { "M", 55 }, { "L", 65 } }), "drink", "healthy", "cup", false },
  { "dk_mango_smoothie", "drinkshop", "芒果冰沙", O({ { "M", 60 } }), "drink", "sweet,cold", "cup", false },
  { "dk_strawberry_smoothie", "drinkshop", "草莓冰沙", O({ { "M", 60 } }), "drink", "sweet,cold,cute", "cup", false },
  -- cafe
  { "cf_americano", "cafe", "美式咖啡", 80, "coffee", "hot", "cup", false },
  { "cf_latte", "cafe", "拿鐵", 110, "coffee", "", "cup", false },
  { "cf_cappuccino", "cafe", "卡布奇諾", 110, "coffee", "", "cup", false },
  { "cf_caramel", "cafe", "焦糖瑪奇朵", 130, "coffee", "sweet", "cup", false },
  { "cf_mocha", "cafe", "摩卡", 130, "coffee", "sweet", "cup", false },
  { "cf_matcha_latte", "cafe", "抹茶拿鐵", 120, "drink", "sweet", "cup", false },
  { "cf_cocoa", "cafe", "熱可可", 100, "drink", "sweet", "cup", false },
  { "cf_earl_grey", "cafe", "伯爵茶", 100, "drink", "", "cup", false },
  { "cf_fruit_tea", "cafe", "熱帶水果茶（壺）", 180, "drink", "shareable", "cup", false },
  { "cf_tiramisu", "cafe", "提拉米蘇", 130, "dessert", "sweet", "plate", false },
  { "cf_lemon_tart", "cafe", "檸檬塔", 110, "dessert", "sweet", "plate", false },
  { "cf_scone", "cafe", "司康佐果醬", 70, "light", "", "plate", false },
  { "cf_panini", "cafe", "帕尼尼", 150, "meal", "hot,filling", "plate", false },
  { "cf_waffle", "cafe", "冰淇淋鬆餅", 160, "dessert", "sweet,shareable", "plate", false },
  -- conveni
  { "cv_water", "conveni", "礦泉水", 20, "drink", "", "cup", false },
  { "cv_tea", "conveni", "瓶裝茶", 25, "drink", "", "cup", false },
  { "cv_coffee", "conveni", "現煮咖啡", 45, "coffee", "", "cup", false },
  { "cv_milk", "conveni", "鮮奶", 30, "drink", "", "cup", false },
  { "cv_yogurt", "conveni", "優酪乳", 35, "drink", "healthy", "cup", false },
  { "cv_beer", "conveni", "啤酒", 45, "alcohol", "alcohol", "cup", false },
  { "cv_tea_egg", "conveni", "茶葉蛋", 8, "light", "hot", "none", false },
  { "cv_oden", "conveni", "關東煮（一份）", 60, "light", "hot", "bowl", false },
  { "cv_hotdog", "conveni", "大亨堡", 35, "light", "hot", "none", false },
  { "cv_onigiri", "conveni", "御飯糰", 25, "light", "", "none", false },
  { "cv_bento", "conveni", "微波便當", 65, "meal", "hot", "bowl", false },
  { "cv_noodle", "conveni", "泡麵", 30, "meal", "hot", "bowl", false },
  { "cv_chips", "conveni", "洋芋片", 30, "dessert", "shareable", "bag", false },
  { "cv_choco", "conveni", "巧克力", 25, "dessert", "sweet,gift", "none", false },
  { "cv_icebar", "conveni", "冰棒", 25, "dessert", "sweet,cold", "cone", false },
  { "cv_umbrella", "conveni", "雨傘", 99, "goods", "", "none", false },
  { "cv_medicine", "conveni", "成藥、喉糖", 80, "goods", "", "none", false },
  { "cv_magazine", "conveni", "雜誌", 79, "goods", "", "none", false },
  { "cv_topup", "conveni", "手機儲值", 300, "service", "", "none", false },
  { "cv_claw", "conveni", "娃娃機（一次）", 10, "game", "", "none", false },
  -- cafeteria
  { "caf_buffet", "cafeteria", "自助餐", 75, "meal", "filling", "plate", false },
  { "caf_luroufan", "cafeteria", "滷肉飯", 30, "meal", "", "bowl", false },
  { "caf_drumstick", "cafeteria", "雞腿飯", 75, "meal", "filling", "plate", false },
  { "caf_porkchop", "cafeteria", "排骨飯", 70, "meal", "filling", "plate", false },
  { "caf_beef_noodle", "cafeteria", "牛肉麵", 90, "meal", "hot,filling", "bowl", false },
  { "caf_plain_noodle", "cafeteria", "陽春麵", 35, "meal", "hot", "bowl", false },
  { "caf_dumplings", "cafeteria", "水餃 10 顆", 50, "meal", "hot", "plate", false },
  { "caf_soup", "cafeteria", "貢丸湯", 25, "light", "hot", "bowl", false },
  { "caf_blacktea", "cafeteria", "紅茶", 15, "drink", "", "cup", false },
  -- fruitshop
  { "fr_cut_box", "fruitshop", "綜合水果盒", O({ { "S", 50 }, { "L", 80 } }), "fruit", "healthy,shareable", "bowl", false },
  { "fr_juice", "fruitshop", "現打果汁", 60, "drink", "healthy", "cup", false },
  { "fr_watermelon", "fruitshop", "西瓜半顆", 120, "fruit", "shareable", "none", { 5, 6, 7, 8, 9 } },
  { "fr_mango", "fruitshop", "愛文芒果 3 顆", 100, "fruit", "", "bag", { 5, 6, 7, 8 } },
  { "fr_strawberry", "fruitshop", "大湖草莓一盒", 150, "fruit", "cute,gift", "bag", { 12, 1, 2, 3 } },
  { "fr_grape", "fruitshop", "巨峰葡萄一串", 100, "fruit", "", "bag", { 6, 7, 8, 9, 10 } },
  { "fr_banana", "fruitshop", "香蕉一串", 40, "fruit", "", "bag", false },
  { "fr_gift_box", "fruitshop", "水果禮盒", O({ { "S", 600 }, { "L", 1200 } }), "gift", "gift", "bag", false },
  -- department
  { "dept_lipstick", "department", "口紅", 1000, "gift", "gift,luxury", "bag", false },
  { "dept_perfume", "department", "香水", 2500, "gift", "gift,luxury,romantic", "bag", false },
  { "dept_skincare", "department", "保養組", 1800, "gift", "gift", "bag", false },
  { "dept_handcream", "department", "護手霜", 350, "gift", "gift", "bag", false },
  { "dept_top", "department", "女裝上衣", 690, "gift", "gift", "bag", false },
  { "dept_skirt", "department", "裙子", 980, "gift", "gift", "bag", false },
  { "dept_dress", "department", "洋裝", 1980, "gift", "gift,cute", "bag", false },
  { "dept_coat", "department", "女裝外套", 2980, "gift", "gift", "bag", false },
  { "dept_men_shirt", "department", "男裝襯衫", 990, "goods", "wear", "bag", false },
  { "dept_men_coat", "department", "男裝外套", 3280, "goods", "wear", "bag", false },
  { "dept_sneakers", "department", "帆布鞋", 1580, "gift", "gift", "bag", false },
  { "dept_heels", "department", "高跟鞋", 2680, "gift", "gift", "bag", false },
  { "dept_wallet", "department", "錢包", 1680, "gift", "gift", "bag", false },
  { "dept_necklace", "department", "項鍊", 1280, "gift", "gift,romantic", "bag", false },
  { "dept_watch", "department", "手錶", 3800, "gift", "gift,wear", "bag", false },
  { "dept_bag", "department", "包包", 2980, "gift", "gift", "bag", false },
  { "dept_brand_bag", "department", "名牌包", 12800, "gift", "gift,luxury", "bag", false },
  { "dept_cushion", "department", "抱枕", 490, "gift", "gift,cute", "bag", false },
  { "dept_mug", "department", "馬克杯", 290, "gift", "gift", "bag", false },
  { "dept_candle", "department", "香氛蠟燭", 450, "gift", "gift,romantic", "bag", false },
  { "dept_iphone4", "department", "iPhone 4 16G", 21900, "goods", "luxury", "bag", false },
  { "dept_htc", "department", "HTC Desire", 15900, "goods", "", "bag", false },
  { "dept_camera", "department", "數位相機", 8990, "gift", "gift", "bag", false },
  { "dept_headphones", "department", "耳機", 1290, "gift", "gift", "bag", false },
  { "dept_small_bear", "department", "小熊布偶", 390, "gift", "gift,cute", "bag", false },
  { "dept_big_bear", "department", "大熊布偶", 1280, "gift", "gift,cute,romantic", "bag", false },
  { "dept_ramen", "department", "豚骨拉麵", 180, "meal", "hot", "bowl", false },
  { "dept_bibimbap", "department", "石鍋拌飯", 160, "meal", "hot", "bowl", false },
  { "dept_curry", "department", "日式咖哩飯", 150, "meal", "hot", "plate", false },
  { "dept_teppanyaki", "department", "鐵板燒", 220, "meal", "hot", "plate", false },
  { "dept_donut", "department", "甜甜圈", 35, "dessert", "sweet", "none", false },
  { "dept_view_set", "department", "雙人景觀套餐（10F）", 2400, "meal", "romantic,luxury,shareable", "plate", false },
  { "dept_view_steak", "department", "單點牛排（10F）", 980, "meal", "luxury", "plate", false },
  { "dept_view_wine", "department", "杯裝紅酒（10F）", 280, "alcohol", "alcohol,romantic", "cup", false },
  { "dept_view_coffee", "department", "熱咖啡（10F）", 180, "drink", "hot", "cup", false },
  { "dept_view_tea", "department", "熱紅茶（10F）", 160, "drink", "hot", "cup", false },
  { "dept_view_dessert", "department", "景觀甜點（10F）", 220, "dessert", "sweet,romantic", "plate", false },
  -- hotel / lodge / minshuku / onsen
  { "hotel_room", "hotel", "景觀雙人房（一晚）", 4800, "stay", "romantic,luxury", "none", false },
  { "hotel_rest", "hotel", "休息 3 小時", 1800, "stay", "", "none", false },
  { "hotel_spa", "hotel", "SPA 90 分", 2800, "spa", "luxury", "none", false },
  { "hotel_pool", "hotel", "泳池日券", 500, "leisure", "", "none", false },
  { "hotel_cocktail", "hotel", "雞尾酒", 320, "alcohol", "alcohol,romantic", "cup", false },
  { "hotel_beer", "hotel", "啤酒", 180, "alcohol", "alcohol", "cup", false },
  { "hotel_mocktail", "hotel", "無酒精特調", 220, "drink", "", "cup", false },
  { "hotel_cheese", "hotel", "起司拼盤", 380, "light", "shareable", "plate", false },
  { "lodge_rest", "lodge", "休息房 2 小時", 600, "stay", "", "none", false },
  { "lodge_aroma", "lodge", "精油護理 60 分", 1500, "spa", "", "none", false },
  { "lodge_night", "lodge", "過夜", 1200, "stay", "", "none", false },
  { "inn_stay", "minshuku", "一泊二食（一房）", 2400, "stay", "romantic", "none", false },
  { "inn_matcha", "minshuku", "抹茶配和菓子", 150, "dessert", "sweet", "cup", false },
  { "onsen_bath", "onsen", "入浴", 350, "bath", "", "none", false },
  { "onsen_towel", "onsen", "浴巾租借", 50, "service", "", "none", false },
  { "onsen_milk", "onsen", "瓶裝牛奶", 35, "drink", "", "cup", false },
  { "onsen_family", "onsen", "家族湯包場 50 分", 1200, "bath", "romantic", "none", false },
  -- aquarium / petshop / shrine / park / dorm / bookstore
  { "aq_dolphin", "aquarium", "海豚布偶", 450, "gift", "gift,cute", "bag", false },
  { "aq_keyring", "aquarium", "鑰匙圈", 150, "gift", "gift,cute", "none", false },
  { "pet_play", "petshop", "互動區 30 分", 100, "leisure", "cute", "none", false },
  { "pet_treat", "petshop", "寵物零食", 50, "goods", "", "none", false },
  { "pet_toy", "petshop", "逗貓棒", 80, "goods", "", "none", false },
  { "shrine_fortune", "shrine", "抽籤", 20, "game", "fortune", "none", false },
  { "shrine_charm", "shrine", "御守", 200, "gift", "gift", "none", false },
  { "shrine_ema", "shrine", "繪馬", 100, "goods", "romantic", "none", false },
  { "park_fishfood", "park", "魚飼料", 10, "game", "", "none", false },
  { "dorm_laundry", "boys_dorm", "洗衣", 20, "service", "", "none", false },
  { "dorm_vending", "boys_dorm", "販賣機飲料", 25, "drink", "", "cup", false },
  { "gdorm_vending", "girls_dorm", "販賣機飲料", 25, "drink", "", "cup", false },
  { "bk_comic", "bookstore", "漫畫", 90, "goods", "", "none", false },
  { "bk_magazine", "bookstore", "雜誌", 120, "goods", "", "none", false },
  { "bk_novel", "bookstore", "小說", 280, "gift", "gift", "none", false },
  { "bk_textbook", "bookstore", "考試參考書", 350, "goods", "", "none", false },
  { "bk_notebook", "bookstore", "筆記本", 30, "goods", "", "none", false },
  { "bk_pen", "bookstore", "原子筆", 15, "goods", "", "none", false },
  { "bk_stationery", "bookstore", "文具組", 150, "gift", "gift", "none", false },
  { "bk_postcard", "bookstore", "明信片", 20, "gift", "gift,romantic", "none", false },
}

local DRINK_OPTIONS = { "sweetness", "ice", "topping" }

-- (id, place, title, wage per hour, shifts, stations, student_only)
local JOBS = {
  { "conveni_clerk", "conveni", "店員", 95, { { 7, 15 }, { 15, 23 }, { 23, 31 } }, { "counter", "aisle" }, false },
  { "drinkshop_clerk", "drinkshop", "店員", 95, { { 11, 17 }, { 17, 23 } }, { "counter" }, false },
  { "cafe_barista", "cafe", "咖啡師", 100, { { 8, 14 }, { 14, 22 } }, { "counter" }, false },
  { "cafe_floor", "cafe", "外場", 100, { { 8, 14 }, { 14, 22 } }, { "seat", "counter" }, false },
  { "restaurant_floor", "restaurant", "外場", 100, { { 11, 15 }, { 17, 22 } }, { "table" }, false },
  { "restaurant_kitchen", "restaurant", "內場助手", 100, { { 11, 15 }, { 17, 22 } }, { "table" }, false },
  { "snack_clerk", "snack", "店員", 95, { { 10, 16 }, { 16, 22 } }, { "counter", "table" }, false },
  { "cafeteria_worker", "cafeteria", "學餐工讀", 95, { { 11, 14 }, { 17, 19 } }, { "table" }, true },
  { "fruit_clerk", "fruitshop", "店員", 95, { { 9, 15 }, { 15, 21 } }, { "stand" }, false },
  { "bookstore_clerk", "bookstore", "店員", 95, { { 10, 16 }, { 16, 22 } }, { "shelf" }, false },
  { "petshop_clerk", "petshop", "店員", 95, { { 11, 16 }, { 16, 21 } }, { "pets" }, false },
  { "dept_beauty", "department", "化妝品專櫃", 105, { { 11, 16 }, { 16, 21.5 } }, { "beauty" }, false },
  { "dept_women", "department", "女裝店員", 100, { { 11, 16 }, { 16, 21.5 } }, { "mirror2" }, false },
  { "dept_men", "department", "男裝店員", 100, { { 11, 16 }, { 16, 21.5 } }, { "mirror3" }, false },
  { "dept_shoes", "department", "鞋包店員", 100, { { 11, 16 }, { 16, 21.5 } }, { "shoes" }, false },
  { "dept_home", "department", "家居店員", 95, { { 11, 16 }, { 16, 21.5 } }, { "sofa" }, false },
  { "dept_books", "department", "書店店員", 95, { { 11, 16 }, { 16, 21.5 } }, { "books" }, false },
  { "dept_phones", "department", "電器店員", 105, { { 11, 16 }, { 16, 21.5 } }, { "phones" }, false },
  { "dept_toys", "department", "玩具店員", 95, { { 11, 16 }, { 16, 21.5 } }, { "bear" }, false },
  { "dept_food", "department", "美食街店員", 95, { { 11, 15 }, { 17, 21.5 } }, { "food" }, false },
  { "dept_view", "department", "景觀餐廳服務生", 110, { { 17, 21.5 } }, { "view" }, false },
  { "aquarium_staff", "aquarium", "售票、導覽", 100, { { 9, 14 }, { 13, 18 } }, { "tank" }, false },
  { "hotel_front", "hotel", "櫃台", 110, { { 7, 15 }, { 15, 23 }, { 23, 31 } }, { "lobby" }, false },
  { "hotel_bar", "hotel", "酒吧服務生", 120, { { 18, 26 } }, { "bar" }, false },
  { "hotel_pool", "hotel", "泳池救生、服務", 110, { { 9, 18 } }, { "pool" }, false },
  { "hotel_spa", "hotel", "SPA 助理", 120, { { 12, 22 } }, { "lobby" }, false },
  { "lodge_front", "lodge", "會館櫃台", 100, { { 10, 18 }, { 18, 26 } }, { "seat" }, false },
  { "inn_helper", "minshuku", "民宿幫手", 95, { { 7, 11 }, { 15, 21 } }, { "seat" }, false },
  { "onsen_staff", "onsen", "溫泉櫃台", 95, { { 6, 14 }, { 14, 24 } }, { "pool" }, false },
  { "library_worker", "library", "圖書館工讀", 95, { { 8, 13 }, { 13, 21 } }, { "shelf", "seat" }, true },
  { "campus_assistant", "school", "系辦工讀", 95, { { 9, 12 }, { 13, 17 } }, { "seat" }, true },
  { "lottery_clerk", "lottery", "彩券行店員", 95, { { 9, 16 }, { 16, 23 } }, { "counter" }, false },
}

-- The department store's floors (1-based, its about text): an item is sold on its floor only.
local FLOORS = {}
for _, k in ipairs({ "dept_lipstick", "dept_perfume", "dept_skincare", "dept_handcream" }) do FLOORS[k] = 1 end
for _, k in ipairs({ "dept_top", "dept_skirt", "dept_dress", "dept_coat" }) do FLOORS[k] = 2 end
for _, k in ipairs({ "dept_men_shirt", "dept_men_coat" }) do FLOORS[k] = 3 end
for _, k in ipairs({ "dept_sneakers", "dept_heels", "dept_wallet", "dept_necklace", "dept_watch", "dept_bag", "dept_brand_bag" }) do FLOORS[k] = 4 end
for _, k in ipairs({ "dept_cushion", "dept_mug", "dept_candle" }) do FLOORS[k] = 5 end
for _, k in ipairs({ "dept_iphone4", "dept_htc", "dept_camera", "dept_headphones" }) do FLOORS[k] = 7 end
for _, k in ipairs({ "dept_small_bear", "dept_big_bear" }) do FLOORS[k] = 8 end
for _, k in ipairs({ "dept_ramen", "dept_bibimbap", "dept_curry", "dept_teppanyaki", "dept_donut" }) do FLOORS[k] = 9 end
for _, k in ipairs({ "dept_view_set", "dept_view_steak", "dept_view_wine", "dept_view_coffee", "dept_view_tea", "dept_view_dessert" }) do FLOORS[k] = 10 end

-- The tables are written in NT$ (the doc's prices); the game runs on RMB (the hero is Chinese, decision 2026-10-11),
-- so every amount is converted at 2010's ~4.7 NT$ per yuan and rounded to a price a shop would print.
local TWD_PER_CNY = 4.7

local function rmb(v)
  local y = v / TWD_PER_CNY
  if y >= 1000 then return math.tointeger(util.round(y / 50) * 50) end
  if y >= 100 then return math.tointeger(util.round(y / 5) * 5) end
  return math.max(1, util.round(y))
end

local CONSUMED_NOT = util.set({ "goods", "gift", "service", "stay", "leisure", "game" })

local function build()
  local items, seen = {}, {}
  for _, it in ipairs(ITEMS) do
    local iid, place, name, price, category, tags, prop, season = table.unpack(it)
    if seen[iid] then ok.exit("duplicate item " .. iid) end
    seen[iid] = true
    if PLACES[place] == nil then ok.exit(iid .. ": unknown place " .. place) end
    local prices = O()
    if type(price) == "table" then
      for k, v in pairs(price) do prices[k] = rmb(v) end
    else
      prices[""] = rmb(price)
    end
    for _, v in pairs(prices) do if v <= 0 then ok.exit(iid .. ": price must be > 0") end end
    local tag_list = util.filter(util.split(tags, ","), function(t) return t ~= "" end)
    local item = O({
      { "id", iid }, { "place", place }, { "name", name }, { "category", category }, { "price", prices },
      { "season", season or json.null }, { "tags", tag_list }, { "prop", prop },
      { "options", place == "drinkshop" and DRINK_OPTIONS or {} }, { "effects", EFFECTS[category] },
      { "giftable", util.contains_value(tag_list, "gift") or category == "gift" },
      { "consumable", not CONSUMED_NOT[category] },
    })
    if FLOORS[iid] then item.floor = FLOORS[iid] end
    items[#items + 1] = item
  end
  local jobs = {}
  for _, j in ipairs(JOBS) do
    jobs[#jobs + 1] = O({ { "id", j[1] }, { "place", j[2] }, { "title", j[3] }, { "wage", rmb(j[4]) }, { "shifts", j[5] },
      { "stations", j[6] }, { "student_only", j[7] } })
  end
  local places = O()
  for k, d in pairs(PLACES) do
    local p = O()
    for f, v in pairs(d) do p[f] = (f == "entry" or f == "entry_student") and rmb(v) or v end
    places[k] = p
  end
  for _, j in ipairs(jobs) do
    if PLACES[j.place] == nil then ok.exit("job " .. j.id .. ": unknown place") end
  end
  return O({ { "version", 2 }, { "currency", "CNY" }, { "places", places }, { "items", items }, { "jobs", jobs } })
end

ok.main(function()
  local data = build()
  path.write_text(OUT, json.encode(data, { ensure_ascii = false, indent = 1 }))
  print(("%s: %d items, %d jobs, %d places"):format((OUT:gsub("/", "\\")), #data.items, #data.jobs, util.len(data.places)))
end)
