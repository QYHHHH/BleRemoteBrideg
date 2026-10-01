# 仓库协作约定

- 本仓库所有 Git 提交信息使用中文。

## 固件版本号

- **发布版**：版本号就是 git tag，带 `v`，例如 tag `v0.0.4` → 固件报 `"v0.0.4"`。
  CI 编译时把 tag 写进 `version_local.h` 注入，**发布不用改 `config.h`**；
  `config.h` 里的 `BRIDGE_FW_VERSION` 只是本地普通构建的默认值。
- **调试版**：在基线后加 `-功能名` 后缀，例如 `v0.0.4-improv`，**不另打 tag**。
  后缀只描述这个固件是用来验什么，不代表"比 tag 新"。
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

## 发布流程：推 tag 编译成草稿 → 自测 → 选 Latest 发布（只编一次）

详见 `docs/RELEASING.md`，全部在 `.github/workflows/release.yml`，要点：

- **推 `v*` tag**：CI 编译，建一个带全部附件的**草稿 Release**（只有仓库成员可见，
  官网不动）。
- **自测后在网页上 Edit 草稿**：选 Latest 发布 → CI 把**已有附件原样**同步到
  `firmware-dist/latest/`（官网读这份），不重新编译；选 Pre-release 发布 → 只公开、
  不上官网，以后改成 Latest 才同步。
- Release 上已有该版本 `-app.bin` 就不再编译，测过的包不会被换掉。测出问题：
  `gh release delete <tag> --cleanup-tag` 后重来。
- 网页上只 Save draft 不会触发 CI（GitHub 规定草稿不触发 workflow）。
- 这跟上面的调试版后缀（`-improv`，从不打 tag）是两回事。
- 回滚官网：Actions 手动跑 *固件发布* 填旧 tag；GitHub 的 latest 标记另用
  `gh release edit <tag> --latest` 改。
