# 固件发布

打一个 `v*` tag，GitHub Actions（`.github/workflows/release.yml`）自动编译、改名、
生成 manifest、发 Release。这篇说清发布出去什么、哪些名字不能改、以及首次启用前
还要做什么。

## 发布步骤

1. **对齐版本号**：把 `firmware/MiRemoteBridge/config.h` 的 `BRIDGE_FW_VERSION`
   改成要发的版本，提交。AGENTS.md 要求它与 tag 字符级相同——历史上漂移过一次，
   网页顶栏和串口横幅因此报错版本。CI 只给警告，不替你拦。
2. **打 tag 并推送**：

   ```bash
   git tag v0.0.5
   git push origin v0.0.5
   ```

   也可以在 Actions 页面手动触发，填版本号（会顺带创建这个 tag）。
3. **等 CI 跑完**（约 5–10 分钟，装工具链那步有缓存）。
4. **抽验**：下一份 `-app.bin` 刷到一块已配好的板子上，确认 WiFi / 按键映射 /
   蓝牙配对还在，串口横幅的版本号和 tag 一致。

## 发布出去什么

| 文件 | 谁用 | 说明 |
| --- | --- | --- |
| `MiRemoteBridge-<ver>.merged.bin` | 第一次装机、网页一键烧录 | 0x0 起始的整片镜像，约 4 MB |
| `MiRemoteBridge-<ver>-app.bin` | 已刷过、只想升级 | 只写 0x10000，约 1.4 MB，配置全留 |
| `manifest.json` | 网页烧录 | 固定名，网页可以永远指向 latest |
| `manifest-<ver>.json` | 存档 | 这一版的清单，历史版本可单独拿到 |
| `-bootloader.bin` / `-partitions.bin` / `-boot-app0.bin` | manifest 的零件 | 共约 30 KB，一般不用单独下 |

**整片镜像会清空配置**：`merged.bin` 在 `0x9000` 的 nvs 区间是 0xFF 填充（esptool
merge-bin 的空隙填充），刷下去等于恢复出厂。manifest 用四个 part 分块写
（`0x0` / `0x8000` / `0xe000` / `0x10000`）就是为了绕开这一点——从网页刷也不清 nvs，
下载量还只有整片的三分之一。

分区表是 `huge_app`，只有 `app0` 没有 `app1`，**当前不支持 OTA**，所以没有 OTA 包。

## 命名是契约，别乱改

`index.html` 用 `/\.merged\.bin$/` 从 latest release 里挑固件（见该文件的 release
探测逻辑），挑不到就把「官方最新固件」按钮置灰。因此：

- 全片镜像**必须**以 `.merged.bin` 结尾；
- release 里**只能有一个**文件匹配这个后缀，否则可能挑中别的变体。

要加 USB CDC 变体时（只有原生 USB 口、无串口芯片的板子），名字用
`-cdc.full.bin` 而不是 `*.merged.bin`，并在 workflow 的「编译」那步复制一份带
`-DARDUINO_USB_MODE=1 -DARDUINO_USB_CDC_ON_BOOT=1` 的构建——和本地
`scripts\build.ps1 -CdcOnBoot` 是同一组宏。

## 首次启用前还要做两件事

1. `index.html` 里的 `var REPO = "OWNER/MiRemoteBridge"` 换成真实仓库，
   否则 release 探测、页脚链接都不工作（页面自己会检测这个占位并把按钮置灰）。
2. 仓库推到 GitHub，且 `config.h` 的版本号与第一个 tag 对齐。

## 万一 CI 挂了

| 报错 | 含义 |
| --- | --- |
| `版本号必须以 v 开头` | tag 或手填的版本号格式不对 |
| `找不到 v0.0.5 —— 版本号没编进去` | 编译缓存复用了旧目标文件；`version_local.h` 是新建的，不在旧依赖表里 |
| `找不到 boot_app0.bin` | core 包版本变了，检查 `ESP32_CORE_VERSION` 与 `tools/partitions/` 的路径 |
| `manifest 里的链接取不到` | release 资源还没同步完（脚本会重试 12 次、每次 5 秒），仍失败就核对仓库名 |
| 发布说明里的警告 | `config.h` 的 `BRIDGE_FW_VERSION` 与 tag 不一致，发布会继续，但版本号是错的 |
