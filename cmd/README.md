# Previous crowd demo launchers

Older CrowdRoom Development test launchers are retained here. Their Tools paths are adjusted for this directory.

Use [CrowdRoom100.cmd](../CrowdRoom100.cmd) in the project root for the current native APNG GPU room; it draws each card's low-poly model (formerly CrowdRoomDevelopmentAPNGGPU.cmd).

Controls: WASD move, hold right mouse to rotate camera, F9 CPU/GPU, 1-4 controlled-character gestures, T action/speech, Y nearest NPC action/speech, Backspace resume behavior. NPCs preload one distributed action and acquire additional actions in the background; controlled character preloads all four. Shared demonstration voice and card-owned mouth morphs remain enabled.

Runtime-generated GPU animation disk cache defaults to4GiB in %LOCALAPPDATA%/OpenKoikatsu/RuntimeGpuCache/v1. Oldest unused owned entries are evicted; invalid files are recalculated. Launcher parameters: -CacheMiB 4096, -CacheDirectory <path>, -NoSpeechActions. GPU pose residency also defaults to4GiB.

R1033 current-feature verification passes100characters, three exact CPU/GPU views, actual GPU action/voice/mouth overlap and4GiB cache control. Stream upload activates only after the matching game-thread data is published. Cache code versions use deterministic source contents rather than compilation timestamps; native binary/input changes still invalidate. Matching common global shaders are included. This turn excludes timings due to concurrent builds. Prior isolated R1000/R1002 cold54.583s, warm30.640s, GPU mean4.454/P955.401ms over5seconds; sustained visible120FPS remains final work. See Docs/APNG_RUNTIME_SCENE_PLAN.md.
R1038 common audio initialization is verified with actual voice/mouth/GPU-action overlap and pixel-exact CPU/GPU views. RuntimeSceneR1037.patch adds a73,029-byte common audio-settings pak; no per-card Cook. The launcher validates its content hash and audio functional evidence before starting.

R1051 current launcher: R1048 executable with R1047 matching shaders/materials/audio. Backspace stops pending/active speech and resumes behavior. Public OKStopCrowdRuntimeSpeech supports any actor, preserving independent speech/mouth state. R1049/R1050 cancellation and R1051 hundred normal speech/CPU-GPU gates pass. Diagnostic cold61.094s exceeds60s; complete loading/FPS acceptance remains pending.
