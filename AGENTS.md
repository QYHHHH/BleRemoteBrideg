# 仓库协作约定

- 本仓库所有 Git 提交信息使用中文。

## 固件版本号

- **发布版**：`BRIDGE_FW_VERSION`（`firmware/MiRemoteBridge/config.h`）与 git tag 必须一致，
  字符级相同，都带 `v`。例如 `"v0.0.4"` 对应 tag `v0.0.4`。
- **调试版**：在基线后加 `-功能名` 后缀，例如 `v0.0.4-improv`，**不另打 tag**。
  后缀只描述这个固件是用来验什么，不代表"比 tag 新"。
- 两者没有自动联动，**改版本号和打 tag 要在同一次改动里一起做**。历史上漂移过一次：
  字符串停在 `v0.0.1`，而 tag 已经打到 `v0.0.3`，网页顶栏和串口横幅因此报错版本。
- 打带后缀的调试固件不要手改 `config.h`，用构建参数覆盖：

  ```
  .\scripts\build.ps1 -Version v0.0.4-improv
  .\scripts\flash.ps1 -Port COM3 -Version v0.0.4-improv
  ```

  它会生成 `firmware/MiRemoteBridge/version_local.h`（已被 `.gitignore` 覆盖），
  该文件优先于 `config.h` 里的默认值。**不带 `-Version` 运行时会删掉它**，
  所以普通构建永远拿到的是受版本控制的那个值。

  覆盖值与上次构建所用的不一致时会**自动强制全量重编**（上次的值记在
  `build/last-version-override.txt`）。这一步不能省：arduino-cli 按上次记录的
  依赖判断是否重编，而新出现的 `version_local.h` 不在任何旧依赖表里，
  不强制重编就会拿着旧目标文件报出旧版本号。

## GitHub Release 的预发布（pre-release）

跟上面"调试版"的后缀**是两回事，别混**：调试版后缀（`-improv` 这种功能名）
**从不打 tag、不推远端**，纯本地烧录验证；这里说的预发布后缀**要真的打 tag
并推送**，会触发 `.github/workflows/release.yml` 真正编译发布。

- **判定规则**：tag 里带连字符（`v0.0.8-rc.1`、`v0.0.8-beta.1`）→ CI 自动标记
  GitHub 的 `prerelease: true`，不参与 "latest" 竞争，官网首页的一键安装不会
  推给用户；不带连字符的干净版本号（`v0.0.8`）→ 正式版，自动成为 latest。
  这条规则是 `release.yml`"解析版本号"那一步用 `case "$V" in v*-*)` 自动推导的，
  **不需要手动去 Release 页面勾选**。
- **流程**：先打带后缀的预发布 tag 测试（真机验证、装机验证），确认没问题后
  **重新打一个干净版本号的 tag**（比如把 `v0.0.8-rc.1` 转正为 `v0.0.8`），
  让 CI 重新走一遍编译发布——**不是**回去编辑已发布的那个 Release 的
  pre-release 复选框。这样 tag 历史本身就说明了"发布前经过了预发布阶段"，
  出问题也可以直接扔掉那个 `-rc.1`，不留痕迹。
- **手动覆盖 latest**（例外情况才用）：`gh release edit <tag> --latest`
  或网页 Release 编辑页的 "Set as the latest release" 复选框，可以无视
  发布时间强行指定哪个 Release 是 latest。
