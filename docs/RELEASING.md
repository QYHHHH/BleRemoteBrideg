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
   git tag vX.Y.Z
   git push origin vX.Y.Z
   ```

   也可以在 GitHub 网页的 Releases 页面点 *Draft a new release*，在
   *Choose a tag* 里**输入新 tag 名并选 Create new tag** —— 这同样会推送 tag
   并触发 CI（CI 随后把固件和说明补到这个 Release 上）。
   注意：选一个**已存在**的 tag 不会触发任何东西，因为那时没有新的 tag 推送。

   或 Actions 页面手动触发，填版本号（会顺带创建这个 tag）。
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
（`0x0` / `0x8000` / `0xe000` / `0x10000`），下载量只有整片的三分之一。

但要注意：index.html 收到 manifest 后会**强制把 `new_install_prompt_erase` 改回
true**，所以网页安装弹窗里「擦除」默认是勾上的。分块只是让「不擦」成为可能，
真要保住配置，得让用户在弹窗里取消那个勾；命令行刷 `-app.bin` 才是稳的。

分区表是 `huge_app`，只有 `app0` 没有 `app1`，**当前不支持 OTA**，所以没有 OTA 包。

## 哪些是自动的

| 东西 | 自动？ | 说明 |
| --- | --- | --- |
| 固件里的版本号 | ✅ 自动 | CI 把 tag 写进 `version_local.h`；串口横幅、设备网页顶栏、`/api/status` 的 version 都读它 |
| manifest 里的 `version` | ✅ 自动 | 同上，网页「项目发布版」徽标显示的就是它 |
| Release 说明文字 | ✅ 自动 | CI 用生成的 notes 覆盖 body（含刷法与文件清单） |
| 网页跟上最新版本号 | ✅ 配一次就行 | 见下节第 1 条，配好之后每次发布自动跟上 |
| `config.h` 的 `BRIDGE_FW_VERSION` | ❌ 得手改 | CI 只在不一致时发警告；AGENTS.md 要求它与 tag 字符级相同，否则本地普通构建会一直报旧版本 |
| 仓库根的 `manifest.json` | ❌ 不自动 | 网页现在读的是 release 里那份，根这份留着当存档或删掉都行 |

## 改名要同步的地方

网页（index.html 的 `MRB_CONFIG.releaseManifest`）只认 manifest 里的
name / version / builds / parts，自己不猜文件名。所以真正的约束是：
workflow 里拼 manifest 的那四个文件名要一起改，否则链接 404。

全片镜像继续叫 `.merged.bin` 只是给人用 esptool 手工刷时一眼认出来，
网页不看后缀。

要加 USB CDC 变体时（只有原生 USB 口、无串口芯片的板子），名字用
`-cdc.full.bin`，并在 workflow 的「编译」那步复制一份带
`-DARDUINO_USB_MODE=1 -DARDUINO_USB_CDC_ON_BOOT=1` 的构建——和本地
`scripts\build.ps1 -CdcOnBoot` 是同一组宏。

## 首次启用前还要做三件事

1. `index.html` 的 `MRB_CONFIG.releaseManifest`（约 695 行）现在是空字符串，
   填成 `https://github.com/<owner>/<repo>/releases/latest/download/manifest.json`。
   填了之后网页的「项目发布版」徽标会自己显示最新版本号，以后再也不用改；
   留空则显示「未配置」，访客只能选本地文件刷。
2. 仓库推到 GitHub（本地 `git remote -v` 目前是空的），workflow 文件要在默认
   分支上才会生效。
3. `config.h` 的版本号与第一个 tag 对齐。

## 万一 CI 挂了

| 报错 | 含义 |
| --- | --- |
| `版本号必须以 v 开头` | tag 或手填的版本号格式不对 |
| `找不到 vX.Y.Z —— 版本号没编进去` | 编译缓存复用了旧目标文件；`version_local.h` 是新建的，不在旧依赖表里 |
| `找不到 boot_app0.bin` | core 包版本变了，检查 `ESP32_CORE_VERSION` 与 `tools/partitions/` 的路径 |
| `manifest 里的链接取不到` | release 资源还没同步完（脚本会重试 12 次、每次 5 秒），仍失败就核对仓库名 |
| 发布说明里的警告 | `config.h` 的 `BRIDGE_FW_VERSION` 与 tag 不一致，发布会继续，但版本号是错的 |
