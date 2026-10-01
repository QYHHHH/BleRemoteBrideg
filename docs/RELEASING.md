# 固件发布

发布走「**推 tag 编译成草稿 → 自测 → 选 Latest 发布，只编一次**」：推一个 tag，
CI 编译固件挂到一个**草稿 Release** 上；你下载自己验证，没问题在网页上把它以
*Latest* 发布，CI 把**已有附件原样**同步到官网，不会重新编译——用户拿到的就是
你测过的那几个字节。

全部在 `.github/workflows/release.yml`（Actions 里叫 *固件发布*）一个文件里：

| 事件 | CI 做什么 |
| --- | --- |
| 推 `v*` tag | 编译，新建一个带全部附件的**草稿**（只有仓库成员看得到，官网不动） |
| 网页上把草稿以 Latest / None 发布 | 不编译，把附件同步到 `firmware-dist/latest/`，官网随之更新 |
| 网页上把草稿以 Pre-release 发布 | 什么都不做：公开给别人试用，官网不动 |
| 网页上把 Pre-release 改成 Latest | 同上面的 Latest 发布，同步官网 |
| 网页上直接新建 Release 并 Publish | Release 上没固件就编译补上；是正式版就顺带同步官网 |
| Actions 页面手动跑，填 tag | 缺固件就补编；已是正式版就重新同步官网（回滚用） |

**版本号由 tag 注入**：CI 编译时把 tag 写进 `version_local.h`（`config.h` 优先
包含它），串口横幅、设备网页顶栏、`/api/status` 都报这个值。`config.h` 里的
`BRIDGE_FW_VERSION` 只是本地普通构建的默认值，**发布时不用改**。

## 发布步骤

1. **推 tag**：代码推到 master 后

   ```bash
   git tag v0.1.1
   ```

   ```bash
   git push origin v0.1.1
   ```

2. **等 CI**：约 5–10 分钟（装工具链那步有缓存）。Releases 页面出现一条标着
   *Draft* 的 `v0.1.1`，固件都在附件里。
3. **自己验证**：下载 `-app.bin` 刷到一块已配好的板子上，确认 WiFi / 按键映射 /
   蓝牙配对还在，串口横幅的版本号是 `v0.1.1`。
4. **发布**：草稿上点 *Edit*，选 Release label：
   - **Latest** → *Publish release*：正式版，一两分钟后官网的一键安装就是这一版。
   - **Pre-release** → *Publish release*：公开给别人试用，官网不动。以后再 *Edit*
     改成 Latest，官网才更新。
   - 测出问题：删掉草稿和 tag，修好后从第 1 步重来（版本号可以不变）：

     ```bash
     gh release delete v0.1.1 --cleanup-tag --yes
     ```

     ```bash
     git tag -d v0.1.1
     ```

> Release 上已经有这个版本的 `-app.bin` 就**不再编译**，挂上去的包不会被换掉。
>
> 网页上直接 *Draft a new release* 也行，但只点 *Save draft* **不会触发 CI**
> （GitHub 规定草稿不触发任何 workflow），得 Publish 才行。网页上建 Release 会
> 同时触发 tag 推送和 release 两个事件，CI 按 tag 排队，后跑的那次发现固件已在
> 就跳过；Actions 里偶尔看到一次 *cancelled* 是排队被顶掉，正常。

## 回滚 / 重新同步官网

Actions → *固件发布* → *Run workflow*，填一个已发布的正式版 tag，官网的
`firmware-dist/latest/` 就换成那一版的附件。GitHub 自己的 "latest" 标记不会
跟着变，需要的话另外执行：

```bash
gh release edit v0.1.0 --latest
```

> 注意：这跟 `AGENTS.md` 里"调试版加后缀"的约定是两回事——调试版后缀（如
> `-improv`）从不打 tag、只用于本地烧录验证。

## 发布出去什么

| 文件 | 谁用 | 说明 |
| --- | --- | --- |
| `MiRemoteBridge-<ver>.merged.bin` | 第一次装机、网页一键烧录 | 0x0 起始的整片镜像，约 4 MB |
| `MiRemoteBridge-<ver>-app.bin` | 已刷过、只想升级 | 只写 0x10000，约 1.4 MB，配置全留 |
| `manifest.json` | 手动下载 / 参考 | 固定名，指向本次 Release 的绝对下载链接；**网页不用它**（见下方 CORS 说明） |
| `manifest-app.json` | 手动下载 / 参考 | 同上，仅升级 APP 那份 |
| `manifest-<ver>.json` / `manifest-app-<ver>.json` | 存档 | 这一版的清单，历史版本可单独拿到 |
| `-bootloader.bin` / `-partitions.bin` / `-boot-app0.bin` | manifest 的零件 | 共约 30 KB，一般不用单独下 |

**网页用的其实是 `firmware-dist/latest/` 那份同源镜像，不是 Release 附件**：
GitHub Release 附件不带 CORS 头，`index.html` 在 GitHub Pages 上用 `fetch()`
跨域读取会被浏览器拦下（manifest 本身能下，但 esp-web-tools 紧接着要 fetch
的每个 `.bin` 分块同样会被拦，实测复现过——`Failed to download manifest`
就是这么来的）。所以转正式版时 CI **把这个 Release 的固件原样拷回仓库自己的
`firmware-dist/latest/` 目录**（manifest 改成同源相对路径重新生成），跟
`index.html` 同源托管，只保留 latest、每次发布覆盖。`index.html` 的
`MRB_CONFIG.fullManifest`/`appManifest` 默认就指向这份同源镜像。
上表里 Release 附件里的两份 manifest（带绝对 GitHub 链接）仍然保留，
给想手动复制链接、或者用别的工具消费的场景用，但**跨域场景下这两份链接
本身就下不动**，别指望拿它们喂给另一个网页的一键安装。

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
| Release 说明文字 | ✅ 自动 | CI 写入生成的 notes（含刷法与文件清单）；网页上建的 Release 接在你写的正文后面；发布前可在网页上再改 |
| 网页两个安装入口的版本 | ✅ 全自动 | 网页读 `firmware-dist/latest/manifest.json` / `manifest-app.json`（每次转正式版由 CI 自动覆盖这个同源镜像），发新版不用碰网页 |
| `config.h` 的 `BRIDGE_FW_VERSION` | — 不用改 | 只是本地普通构建的默认值，发布用的版本号来自 tag |

版本信息在 Release 和仓库的 `firmware-dist/latest/` 里各存一份——这是唯二的
例外（详见上面 CORS 那段），别的产物没有第二处要维护。

## 改名要同步的地方

网页（index.html 的 `MRB_CONFIG.releaseManifest`）只认 manifest 里的
name / version / builds / parts，自己不猜文件名。所以真正的约束是：
workflow 里拼 manifest 的那四个文件名要一起改，否则链接 404——**要改两个文件**：
`release.yml` build job 的"整理产物"和"生成 ESP Web Tools manifest"两步（Release 用，绝对链接），
以及 sync job 下载附件、生成同源 manifest 那步（网页实际用的那份，同源相对路径），
文件名字符串必须一致。

全片镜像继续叫 `.merged.bin` 只是给人用 esptool 手工刷时一眼认出来，
网页不看后缀。

要加 USB CDC 变体时（只有原生 USB 口、无串口芯片的板子），名字用
`-cdc.full.bin`，并在 workflow 的「编译」那步复制一份带
`-DARDUINO_USB_MODE=1 -DARDUINO_USB_CDC_ON_BOOT=1` 的构建——和本地
`scripts\build.ps1 -CdcOnBoot` 是同一组宏。

## 首次启用前还要做两件事

1. **网页不用配**。`index.html` 默认就读同源的 `firmware-dist/latest/manifest.json`
   / `manifest-app.json`（第一次转正式版时 CI 就会生成这个目录）。
   `MRB_CONFIG.repository` / `latestAsset()` 那套只是没填 `fullManifest`/
   `appManifest` 时的兜底，指向 GitHub Release，跨域会被拦，正常情况用不到。
2. 仓库要推到 GitHub、GitHub Pages 要指向这个仓库的默认分支（`Settings → Pages`），
   workflow 文件也要在默认分支上才会生效。

## 网络差的用户怎么装

网页「高级 · 固件安装方式」里有第三个入口 **本地固件文件**：用户自己把
`MiRemoteBridge-<ver>.merged.bin`（或 `-app.bin`）下到电脑上，再用浏览器选中它。
页面用 `blob:` 清单把文件直接交给 ESP Web Tools，**全程不联网取固件**。

偏移按文件名和体积判断：含 `merged` / `full` 或大于 2.5 MB 的按整片镜像写
`0x0`，其余按应用固件写 `0x10000`；判断结果显示在按钮下面，用户能看见。
这两个数字来自 `huge_app` 分区表，改分区方案时要同步改 `index.html` 里
`pickLocalFile()` 的阈值和偏移。

本地文件入口不依赖任何 manifest，所以离线、内网、CDN 被墙都能用。

## 万一 CI 挂了

| 报错 | 含义 |
| --- | --- |
| `版本号必须以 v 开头` | tag 或手填的版本号格式不对 |
| `还是草稿或预发布，不同步官网` | 不是错误：草稿和 pre-release 本来就不上官网，测完选 Latest 发布 |
| `-app.bin 里找不到版本号` | sync job 下到的附件不是这个版本编出来的（附件被手工替换过） |
| `找不到 vX.Y.Z —— 版本号没编进去` | 编译缓存复用了旧目标文件；`version_local.h` 是新建的，不在旧依赖表里 |
| `找不到 boot_app0.bin` | core 包版本变了，检查 `ESP32_CORE_VERSION` 与 `tools/partitions/` 的路径 |
| `manifest 里的链接取不到` | sync job 末尾的校验；release 资源还没同步完（脚本会重试 12 次、每次 5 秒），仍失败就核对仓库名。官网镜像在这一步之前已经提交 |
