## New in this build

The shaders are the same as in v20261020b: your precompiled store stays valid. Running the precompile again adds the
new boss-death shaders below (only the new part is compiled).

- **A 1080p output (optional, new):** on a 1080p screen, Demon's Souls can now run its own HD mode: its
  post-processing and HUD at 1920x1080 instead of 3840x2160 scaled down afterwards. Less work for the graphics card:
  at the 1-1 bench spot on an RTX 5090, 69.8 -> 78.3 fps. Turn it on with `run.cmd -Set KYTY_HD_OUTPUT=1` (not
  together with DLSS: `-Upscaler dlss` makes the 4K output). Known issue: in this mode the souls counter's dark strip
  shows as a small black square; the launcher gets a setting for it once that is fixed. (A contribution by @intellq,
  PR #103.)
- **AMD FSR 3.1 frame generation (2x)** for graphics cards without DLSS frame generation: the launcher's "AMD FSR 3
  frame generation" or `run.cmd -Fsr`, with AMD's signed `amd_fidelityfx_vk.dll` (from AMD's FidelityFX SDK) next to
  the emulator. (Contributions by @iSamigor, PRs #96 and #99.)
- **Add-on content (DLC):** add-ons go in `Content\PPSA01341\`, one folder each, checked by their content id.
  Add-ons without files of their own, such as a deluxe edition's extras, can be declared in `dlc_emu.ini`. The
  README's "Add-on content (DLC)" explains both. (A contribution by @Myoko, PR #104.)
- **Your own keys:** `--keymap` lines in `launch.json` now take key names as people write them (`LeftShift`,
  `LeftCtrl`, `Spacebar`, `Esc`). Space and Esc can be mapped; when Space is mapped, the Pause key pauses. A line the
  emulator cannot use is skipped and named in the run log instead of stopping the start. See the README's "Your own
  keys" (issue #91).
- **Ultrawide:** with `-Aspect 21:9` or `32:9`, adding `-AspectFit` keeps the movies (and the title and loading
  screens) at 16:9 with black bars at the sides. Without it they fill the screen as before (issue #97).
- **Game on a hard disk:** the README now says what took a player from freezes to full speed: the game packed into a
  `.zar` on the hard disk and the emulator folder on an SSD (issue #91).
- **Fixes:**
  - AMD Radeon RX 9000/7000/6000: the emulator could end after a few minutes of play with "TextureCache: invalid
    image view description" (issue #102).
  - "TextureCache: color target requires rediscovery before final acquisition" could end the emulator on fast travel
    from the Nexus to Boletaria (issue #94).
  - DLSS super resolution showed a black 3D image on game version 01.005.000 (thanks to @Al3cko for the analysis), and
    with the NVIDIA app's DLSS override, where the game's own upscale is now used instead (issue #87).
  - A button press could be lost during a stutter (a contribution by @Vorshim92, PR #100).
  - A boss death could freeze the game for up to 51 s the first time: its compute shaders are now precompiled in all
    the forms the game uses them (PR #96).
  - An update precompile no longer makes every stored pipeline again just to copy it: updates are much faster
    (issue #85).
- **Linux:** DLSS super resolution, DLAA and DLSS frame generation through NVIDIA NGX (a contribution by @Tutuzaooo,
  PR #101).
- **Internal cleanup:** about 6,000 lines of finished experiments and diagnostics are gone, and the release settings
  are now the emulator's own defaults (`launch.json` no longer lists them; `-Set KEY=` still turns one off). Same
  behaviour and frame rates (checked on all 16 maps), same shaders.
- **Thanks** to @intellq, @Myoko, @iSamigor, @Vorshim92, @Tutuzaooo and @Al3cko for their contributions, and to
  everyone who reported problems and tested.

## New in v20261020b

- **Crash on PCs with 16 GB of RAM (issue #91):** v20261020's new memory saving for textures (on by default with 20 GB
  of RAM or less) could end the emulator with "PageManager fatal: read-watcher overflow" while a level loaded. Fixed;
  the saving stays on. If you added `-Set KYTY_TEXTURE_OFFLOAD=0` to get around the crash, you can remove it again.
- **Four shaders that ended the emulator:** four of the game's pixel shaders could not be translated, and the emulator
  stopped where the game drew with one of them (an automated tour hit one in the Valley of Defilement). They translate
  now, and the precompile builds them.
- **More shaders precompiled:** automated tours through all six worlds, fighting and killing every enemy (bosses too),
  added 153 shader programs and 580 pipelines to the hints. Among them are three large Shrine of Storms shaders that
  froze the game for up to 40 s the first time they were needed.
- **Linux with AMD graphics:** questions to the graphics driver whose answer is already known are skipped, and where a
  question is slow, a separate thread follows the GPU instead (`KYTY_TIMELINE_WATCHER=0` turns it off; issue #80).

Earlier changes are on the release pages of v20261020 and before.

## 本版本的变化

着色器与 v20261020b 相同：你已经预编译的存储继续有效。再运行一次预编译会加入下面提到的 Boss 死亡着色器（只编译新增的部分）。

- **1080p 输出（可选，新增）：** 在 1080p 显示器上，恶魔之魂现在可以使用它自己的高清模式：后期处理和 HUD 直接以 1920x1080 渲染，不再先渲染 3840x2160 再缩小。显卡负担更小：RTX 5090 在 1-1 测试点从 69.8 提升到 78.3 fps。用 `run.cmd -Set KYTY_HD_OUTPUT=1` 开启（不能与 DLSS 同时使用：`-Upscaler dlss` 输出的是 4K）。已知问题：这个模式下魂数旁边的深色条会显示成一个小黑方块；修好之后启动器会加上这个选项。（@intellq 的贡献，PR #103。）
- **AMD FSR 3.1 帧生成（2 倍）**，适用于不支持 DLSS 帧生成的显卡：在启动器里勾选 "AMD FSR 3 frame generation"，或用 `run.cmd -Fsr`，需要把 AMD 签名的 `amd_fidelityfx_vk.dll`（来自 AMD 的 FidelityFX SDK）放在模拟器旁边。（@iSamigor 的贡献，PR #96 和 #99。）
- **附加内容（DLC）：** 附加内容放在 `Content\PPSA01341\`，每个一个文件夹，按内容 ID 校验。没有自己文件的附加内容（例如豪华版的额外内容）可以写在 `dlc_emu.ini` 里声明。README 的 "Add-on content (DLC)" 一节说明了两种方式。（@Myoko 的贡献，PR #104。）
- **自定义按键：** `launch.json` 里的 `--keymap` 现在可以识别常见的按键写法（`LeftShift`、`LeftCtrl`、`Spacebar`、`Esc`）。空格和 Esc 可以映射了；空格被映射后，用 Pause 键暂停。模拟器无法使用的行会被跳过，并在运行日志里写明，不会再导致无法启动。详见 README 的 "Your own keys"（issue #91）。
- **超宽屏：** 使用 `-Aspect 21:9` 或 `32:9` 时，再加上 `-AspectFit`，过场动画（以及标题画面和加载画面）会保持 16:9，两侧显示黑边；不加时仍和以前一样铺满屏幕（issue #97）。
- **游戏放在机械硬盘上：** README 里写了一位玩家从卡顿变成全速运行的方法：在机械硬盘上把游戏打包成 `.zar`，模拟器文件夹放在 SSD 上（issue #91）。
- **修正：**
  - AMD Radeon RX 9000/7000/6000：游玩几分钟后模拟器可能以 "TextureCache: invalid image view description" 退出（issue #102）。
  - 从连接之地（Nexus）传送到波雷塔利亚时，模拟器可能以 "TextureCache: color target requires rediscovery before final acquisition" 退出（issue #94）。
  - DLSS 超分辨率在游戏版本 01.005.000 上 3D 画面是黑的（感谢 @Al3cko 的分析）；使用 NVIDIA App 的 DLSS 覆盖设置时也会黑屏，现在会改用游戏自带的放大（issue #87）。
  - 卡顿时按键可能丢失（@Vorshim92 的贡献，PR #100）。
  - Boss 死亡时第一次可能让游戏卡住最长 51 秒：它的计算着色器现在会按游戏用到的所有形式预编译（PR #96）。
  - 更新时的预编译不再为了复制而把存储里的每个管线重新创建一遍：更新快了很多（issue #85）。
- **Linux：** 通过 NVIDIA NGX 支持 DLSS 超分辨率、DLAA 和 DLSS 帧生成（@Tutuzaooo 的贡献，PR #101）。
- **内部清理：** 删掉了约 6,000 行已经结束的实验和诊断代码；发布用的设置现在就是模拟器自己的默认值（`launch.json` 不再列出它们；`-Set KEY=` 仍然可以关闭某一项）。行为和帧率不变（16 张地图全部测试过），着色器不变。
- **感谢** @intellq、@Myoko、@iSamigor、@Vorshim92、@Tutuzaooo 和 @Al3cko 的贡献，也感谢所有报告问题和帮忙测试的玩家。

## v20261020b 的变化

- **16 GB 内存电脑上的崩溃（issue #91）：** v20261020 新增的纹理省内存功能（内存 20 GB 及以下默认开启）可能在加载关卡时让模拟器以 "PageManager fatal: read-watcher overflow" 退出。已修正，该功能保持开启。如果你为了绕过崩溃加了 `-Set KYTY_TEXTURE_OFFLOAD=0`，现在可以去掉了。
- **会让模拟器退出的四个着色器：** 游戏里有四个像素着色器无法翻译，游戏用到它们的地方模拟器就会停止。现在它们可以正常翻译，预编译也会生成它们。
- **预编译的着色器更多了：** 自动测试跑遍了全部六个世界，与每个敌人（包括 Boss）战斗并击杀，提示里多了 153 个着色器程序和 580 个管线，其中包括风暴神殿的三个大型着色器，它们第一次用到时会让游戏卡住最长约 40 秒。
- **Linux 和 AMD 显卡：** 已知答案的驱动询问会被跳过；询问较慢时，改由一个单独的线程跟踪 GPU 进度（`KYTY_TIMELINE_WATCHER=0` 可关闭；issue #80）。

更早的变化见 v20261020 及之前版本的发布页面。
