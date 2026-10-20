# 貢獻

- 遊戲邏輯與 plugin 用 C++（UE 5.8）。
- 離線工具用 Rust（`Tools/ok-extract`）。不要加 Python。
- 不准提交 `abdata/`、`mods/`、`UserData/`、`Saved/Converted/`。
- 測試卡由 C++ `EnsureMinimalKksCard` 寫到 `Saved/TestCards/`。

跑測試：

```
E:\PhysX\UE5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe C:\portable\OpenKoikatsu\OpenKoikatsu.uproject -unattended -NullRHI -nosound -ExecCmds="Automation RunTests OpenKoikatsu" -TestExit="Automation Test Queue Empty"
```
