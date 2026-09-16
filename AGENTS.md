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
