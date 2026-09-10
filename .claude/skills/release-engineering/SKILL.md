---
name: release-engineering
description: fulla 发版工程实战手册——版本六点+约 15 处载体同步、三门本地预检、tag 故障恢复（删远端重推）、PyPI 首发、孤儿 release 处置、Release notes 全英文规则
disable-model-invocation: true
---

# 发版工程（实战版）

> 本 skill 收录 v1.0.0–v1.1.0 发版全程的实战教训（两次 tag 秒挂的根因都在这里），取代
> 2026-09-10 删除的旧 `release` skill（v1.0.1 时代口径，缺六点同步面）。版本号语义等基础
> 说明仍见 `docs/contribute/versioning-and-release.md`。

## 版本同步面

**六点（tag 门会挂的最小集）**：

1. `cmake/Version.cmake`（SSoT）
2. `CMakeLists.txt` `project(VERSION)`
3. `conanfile.py` `version`
4. `frontends/admin/package.json` + `frontends/user/package.json`
5. `apps/server/openapi.yaml` `info.version`（治理门校验 == Version.cmake）
6. `clients/python/pyproject.toml` `version`（regen_clients 漂移门校验 == Version.cmake）

**约 15 处额外载体（全仓一致性；v1.0.1 用户实测抓漏）**：README ×2 项目状态行、
`deploy/helm/fulla/Chart.yaml`（version + appVersion，**helm 镜像 tag 默认=appVersion**）、
values-local 注释、`docker-compose.debug.yml` 镜像 tag、`docs/operate/docker-deployment.md`
引用、`docs/sdk/sdk-integration-guide.md` + website i18n 镜像（tarball 名/CMAKE_PREFIX_PATH/
ghcr tag 示例）、libs 独立构建回退默认（`libs/{common/testing,storage-redis,oauth2,storage-postgres}`
CMakeLists + `libs/drogon` 回退 + `cmake/FullaPackage.cmake`）、bug_report.yml 版本占位。
CHANGELOG/ADR/历史任务表 = 历史记录，**有意不改**。

## 发版前三门本地预检（tag 挂掉的标准预防）

```bash
python tools/openapi-governance/check_spec_governance.py
python tools/clients/regen_clients.py --check
python tools/api-diff/api_diff.py
```

三门本地绿则 tag 基本不会挂（版本一致性由 5/6 两点 + 这三门交叉保证）。
`bash .claude/hooks/local-gates.sh` 一键跑这三门 + 迁移门 + 命名门。

## tag 触发后的故障恢复（铁律）

- **重跑旧 run 无效**：tag run 用 tag 所指 commit 的 workflow 版本，post-tag 修复永远到不了
  已推的 tag。
- 恢复路径 = 修复合入 master → `git tag -f vN.N.N` → `git push origin :refs/tags/vN.N.N`
  （删远端）→ `git push origin vN.N.N`（重推触发新 run）。推送走 SSH。
- run 整体红先逐 job 看产物（GHCR/SDK/SBOM 可能已上架，只有尾部 job 挂）。

## 版本退役顺序

**先删 GitHub Release 对象、再删 git tag**。只删 tag 会把 Release 自动转 Draft（发布页列表
隐身但 `/releases/edit/vX` 直链可达、API 返回 `draft:true`；Draft 可能被 GitHub 异步自清，
时点不可控）。验收删除干净用 API（`gh api repos/<o>/<r>/releases` 计数），勿信网页列表。
孤儿 release（tag 已删的旧 release）**从列表页和 REST 隐藏、GraphQL 仍列出、直达 URL 可
访问**——三套视图各说各话，只能直达 URL 逐条处理（PAT 403，需用户网页删）。

## Release notes 规则（用户裁定）

- **全英文、不混语言**；提交 subject 一律英文（中文 subject 会被 cliff 原样漏进 notes）。
- CHANGELOG 条目仅英文（release notes 逐字拼接自 CHANGELOG 的 Breaking 段）。
- **cliff 排除 chore 提交** → License 切换这类 chore 大事进不了 notes → release.yml 已加
  "Breaking 段自动拼接"步骤（awk 抽取该版本 CHANGELOG 段内 Breaking bullet）；拼接源必须英文。
- cliff `--latest` 无前驱 tag 时吞全部提交 → 422 body too long（125k 上限）；已有防御
  （cliff 空/超 120k 时 awk 回退 CHANGELOG 版本小节）。已知未修坑：fallback 的
  `index($0,"[ver]")==4` 会误抓历史同名版本段（fulla [1.0.0] vs authforge [1.0.0]）。

## PyPI

- 首发**必须 account 级 token**（项目级 token 只能对已存在项目创建），成功后立即换项目级收窄
  并更新 secret `PYPI_API_TOKEN`。
- twine：`TWINE_USERNAME=__token__`，密码=整个 token；PowerShell 5.1 用
  `Read-Host -AsSecureString` + BSTR 转换。发布成功标志 = `View at: https://pypi.org/project/...`。
- Go 包靠 release.yml 自动推嵌套 tag `clients/go/vX.Y.Z`。

## 流程要点

- 版本裁定遵循 `docs/contribute/versioning-and-release.md`：安全加固可走 MINOR + Release Notes
  `⚠️ Breaking (security hardening)` 段显式披露（v1.1.0 先例）。
- 发版 PR 走 rebase-merge；**agent 不推 tag**（`.claude/settings.json` deny `git push`——
  tag 推送与删除需用户授权或明确指示）。
- 发版后 README 徽章：release.yml 用 `img.shields.io/github/v/release/...`（workflow badge 对
  tag 触发的流水线恒红，是错误徽章）。
