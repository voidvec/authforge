---
name: ci-gate-sync
description: fulla 仓库 CI 门禁的同步点清单与批准政策——新增/修改错误码、HTTP 端点、DB 迁移、公共头文件、OpenAPI 规范之前必读，漏同步点 CI 必挂
---

# CI 门禁同步点与批准政策

来源：PR #115/#141/#149/#157/#179/#180 等 6 轮实战的 CI 失败复盘。同步点的**硬清单**（动了
什么必须同步什么）在 `.claude/rules/sync-points.md`（路径触发加载）；本 skill 补齐政策、判定
规则与排障细节。

## Static Checks 共 8 项（本地可全跑）

arch-guard / migration-check / api-diff / naming / drogon 宏布尔检查 / manage 双脚本 parity /
openapi validate / spec governance。推送前本地全部跑一遍比在 CI 上试错快一个数量级
（`bash .claude/hooks/local-gates.sh` 一键跑其中 5 门）。

## api-diff（头文件 SemVer 门）政策

- 判定入口：`python tools/api-diff/api_diff.py`（覆盖 `libs/*/include/fulla/**` 全部头文件）。
- **纯新增声明行 = ADDITIVE** → `--update-baseline` 即可（不必 `--force`）。
- **改既有声明行（如构造签名）= BREAKING** → 三选一：
  1. 改成加性缝（新增 setter / 重载），基线零改动——首选，尤其 v1.0.x 阶段；
  2. `--update-baseline --force` + commit message 写理由（先例：源兼容的尾部默认参数、无发布面
     消费者的私有成员/内部静态签名）；
  3. 真 breaking 则走版本政策（MAJOR 或安全加固豁免，见 `docs/contribute/versioning-and-release.md` §3）。
- 语法细节：尾部默认参数**贴类型写**（`const std::string& name = ""`）才被识别为 additive；
  `const std::string &name = ""` 判 BREAKING。
- 工具会重写基线文件（头注释不保留，理由以 commit message 为准）。

## oasdiff（OpenAPI 破坏性变更门）豁免规则

- 豁免清单 `tools/openapi-governance/oasdiff-breaking-ignore.md`（纯文本/Markdown，每条带理由）。
- **条目必须单行**，且是 oasdiff 实际输出文本的**精确前缀**（匹配到一个分界点为止，如
  ``added the new required request property `state` `` 以 `` `state` `` 结尾）。
- 折行、代码围栏、行内破折号注释都会失配；**理由写在条目所在小节的散文里**，不写在匹配行上。
- 对请求参数加 minLength/maxItems 等"收紧"也报 breaking——安全收紧走 errata + 理由，别想绕。
- 本地复现：`go install github.com/oasdiff/oasdiff@latest`（GOPROXY=goproxy.cn）。

## 测试命名门

`tools/test/scripts/naming_validator.sh` 强制 DROGON_TEST 名字匹配
`^(Unit|Integration|E2E|Performance|Security|API|Database|Acceptance)_P[0-3]_<Module>_...`
（如 `Integration_P1_Consent_NoSession_Returns401`）。漏 P 段 = Static Checks 红。

## 迁移门

- 基线内迁移**不可原地修改**（migration_check M5 按 SHA-256 锁定）——写新的前向迁移，然后
  `python tools/migration-check/migration_check.py --update-baseline`。
- 新迁移必须幂等（`IF NOT EXISTS` / 顶层守卫）：db-reset 后服务端会整链重放。

## openapi.yaml 改动的完整下游

1. `python tools/clients/regen_clients.py` 重生成 Python+Go SDK 并提交生成物（drift 门
   `--check` 本地可验）——**operation 的 tags 块极易丢**，丢了生成器把操作归 default tag，
   clients-sdk 门挂。
2. `apps/server/docs/api/openapi.json` 再生成：拷 `build/.../Release/config.json` 到
   `apps/server/` → 以该目录为 CWD 短暂运行 server（产出落在 CWD 的 `docs/api/`）→ 杀进程 →
   删临时 config（该文件是未跟踪运行时拷贝，勿删后不还）。
3. `docs/domains/api-reference.md` §5 若涉及错误码 → 重跑 `ErrorCatalogDocTest`。

## 前端侧门禁（改 frontends/** 时）

- `i18nKeys.test.ts` 键奇偶校验（禁 node:fs）；`crossAppConsistency` 属性测试（错误码域）；
  `check-ui-sync.mjs`（组件双副本字节一致）；`check-frontend-size.mjs`（体积门，从 repo 根跑）。
- e2e 与 vitest 不受 i18n 预编译插件影响（vitest 有独立 config），但**动了 i18n 构建接线后必须
  跑 `vite preview` 双语言冒烟**（dev-server e2e 测不到 runtime-only 路径）。

## GitHub Actions 侧

- **reusable workflow 权限天花板**：被 `ci.yml` 调用的 workflow（如 `_frontend.yml`）里 job 级
  `permissions:` 超过调用方授权 → 整个 run **startup_failure**（零 job，错误只在 run 页 HTML
  annotation 里，actionlint 查不出）。新增 job 需要的权限必须在 **ci.yml 调用 job** 上给。
- `release.yml` 是 tag 触发，PR CI 不解析它——它的修改本地只能用 actionlint 静态验。

## 历史失败模式速查

| 症状 | 根因 |
|---|---|
| Static Checks 挂，错误码计数 "26==27" | api-reference.md 目录表漏同步（ErrorCatalogDocTest） |
| MSVC C2078 初始值设定项太多 | `std::array<RawEntry, N>` 尺寸字面量漏改 |
| clients-sdk drift | openapi 改后未 regen / tags 丢失 |
| oasdiff 豁免不生效 | 条目折行或含尾注，非单行精确前缀 |
| naming 门红 | 测试名缺 `_P[0-3]_` 段 |
| CI run startup_failure | reusable workflow job 权限超过调用方天花板 |
| macOS 腿 `-Wunused-const-variable` | 改用例后未删干净的 `constexpr const char*` |
