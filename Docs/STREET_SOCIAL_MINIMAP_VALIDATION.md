# 街區小地圖驗證

2026-10-08，功能優先。

- StreetMinimap02 / Runtime-StreetMinimapSmoke02：實際六APNG角色、五女角，north/east座標/遠距夾限/五色/實際paint/好感即時變色/UI防穿透通過。
- 初版陣列自引用斷言及標記重疊已記錯誤MD並修正。
- StreetMinimap04/05 的完整回歸因使用者新增版面及姓名需求而主動中止，均不作發布證據。只停止經PID/完整exe路徑驗證的自有測試。
- StreetMinimap06 Development20.15秒。Runtime-StreetMinimapFull03 已完成新小地圖probe：左上地圖/右側400寬直排可捲選單、五女角名字顯示開5/關0、中文5字/英文10字界線、當前好感即時變色、投影與防穿透通過。已查看 colors/updated 兩張1920×1080截圖，姓名開關及標記避讓正常。

早期階段未發布；最終完整回歸與發布結果見下方。完整人工驗收留最後，尚不聲稱全內容或性能驗收完成。

StreetMinimap08：Development 編譯24.36秒通過。最新 Full05 實景小地圖 probe 已通過五人同點、左右邊緣矩形避讓，中文5/英文10字、名字開5/關0、實際位置/好感資料、左上地圖/右側選單及UI防穿透。已檢視實際1920×1080 colors截圖：人物標記固定，名稱避讓並用同色引線連回。完整既有功能回歸已通過，結果如下。

## 最終發布
StreetMinimap08.stage / Runtime-StreetMinimapFull05：passed=true、minimap_checked=true、errors=[]。
- 15話題／5招呼／5道別、3觸控、4收集話題、3設施、同行、5合照、5坐談、6人看書、5喝飲料、10潑水方向案例通過。
- 獨立來源比對：823語音雜湊、15話題匹配、12次正好感提交、觸控重複序列[0,2,1]通過。
- 50張截圖均解碼為1920×1080；已查看名字開/關與坐談截圖。
- source/host六檔一致；公開入口 StreetSocial.cmd receipt已更新且ValidateOnly通過。
- 暖快取全員顯示10.224秒，僅記錄本次功能測試，不代表冷啟動或FPS驗收。
- 最後完整人工驗收仍保留，full_acceptance=false。
