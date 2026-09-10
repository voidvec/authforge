---
name: test-methodology
description: fulla 测试体系与方法论——两层测试（ctest in-process + 59/52 端点脚本）、coverage 测量、mock 注入缝、DROGON_TEST/Playwright 写法实证、flake 与隔离性缺陷定性
---

# 测试方法论

## 两层测试，缺一不可

**层 1 ctest（进程内）**：`fulla-tests`（test_main.cc 在 5555 起进程内 Drogon server，DROGON_TEST
用例经 HttpClient 打它）+ 各 lib gtest 二进制 + Contract 逐条 add_test。从 `build/<preset>` 根
目录跑（不是 build/tests），`-C Release` + `-j1`。

**层 2 端点脚本（进程外）**：`test-oauth2-endpoints.{ps1,sh}`（59）+ `test-admin-endpoints.{ps1,sh}`
（52），对独立 fulla-server 进程跑；已由 `EndpointTests_OutOfProcess` 包进 ctest。full_test
步骤 5-8 在该条目跑绿时自动 `[SKIP #119]`（junit 证据 `build/<preset>/Testing/junit-config-standard.xml`）。

**铁律：只跑层 1 就宣称全绿 = 假绿**（历史上反复犯——端点脚本里的过时 scope 引用只有层 2 抓
得到）。验收一律走 `/full-test` 或至少 `ctest -R EndpointTests`。

## 写/跑测试的实测规则

- **新 DROGON_TEST 用例提交前必须跑完整二进制**（不带 `-r`）——单用例 `-r` 抓不到跨用例互踩
  与 teardown 顺序崩溃；某用例使套件不稳定就删掉它并注明原因。
- 测试名匹配命名门 `^(Unit|Integration|...)P[0-3]_`（见 ci-gate-sync）。
- helper 函数内**不能用 REQUIRE**（无 drogon_test_ctx_）→ 返回 nullptr、调用处断言。
- form body 必须 `setContentTypeCode(CT_APPLICATION_X_FORM)`——只设 Content-Type 头不解析参数
  （假 403/空参数）。
- include 用相对路径 `"../../common/HttpTestClient.h"`；helper 在 `fulla::test::http` 命名空间。
- **测试里造 client 走 admin API，别手写 INSERT**（手写 SQL 列名与 schema 不符 → 未捕获
  DrogonDbException → 0xc0000409 整进程崩溃，掩盖真实输出）。
- 隔离性缺陷定型法：**单跑过、套件挂** = 先怀疑用例间状态污染（固定 email 撞唯一索引、
  throwaway 用户残留、单例 config 污染）；重置 DB 后重跑对比。单例（RateLimiter 等
  `::instance()`）被单测 configure 后必须 save/restore（`reset()` 只清桶不还原 config）。
- 分页端点的调用方（含测试 helper）不能假设无界列表——用 `?q=` 搜索（种子 admin 在 id=513，
  不在第 1 页）。
- 断言 HTTP 状态前查 `ErrorCatalog.cc` 的实际映射（如 `VALIDATION_USERNAME_TAKEN` 是 409 不是
  400）。

## coverage 测量

- 必须**全部 5 个测试二进制**都跑（common / common-testing / identity / oauth2 / 主二进制），
  只跑主二进制会把 common 从 98.8% 低报到 ~60%。
- `gcovr` 8.6 有 path-matching bug——汇总用 `scripts/measure_coverage.py`（聚合 gcov JSON）；
  HTML 报告用 gcovr 可以。
- `gcov -j -p` 的输出落 **CWD**（带 `#` 前缀文件名），生成脚本必须 cwd 归位 + 零输出硬失败。
- ratchet 门：`--ratchet`（>0.5pp 降即挂、<200 行库豁免、基线缺库=挂）；seed 流程见
  ci-failure-triage。

## mock 注入缝（controller 出站依赖）

- 缝 = controller 的 `setXxxService(...)` 裸指针 setter，经 `::drogon::DrClassMap::getSingleInstance<Controller>()`
  拿进程单例；controller 先判 `if (xxxService_)` 再回退内联路径，所以 fake 注入即生效，全 CI
  腿（含 Windows memory-mode）可跑。
- **keepAlive 铁律**：fake 服务的指针必须活过所有 DROGON_TEST 用例（用例顺序 = 注册顺序）。
  每次 `injectXxxFake()` 重建服务，累积进进程级 `static std::vector<shared_ptr<...>> keepAlive`。
  函数内 `static auto svc = ...`（首次调用构建后绑定旧 http）是经典陷阱。
- **本项目不用 gmock**——手写 fake 是惯例（`libs/common/testing/` 与
  `libs/identity/include/authforge/identity/testing/`；common 的 testing 库禁依赖 identity）。
- GitHub happy-path 无法 memory 测（`getDbClient()` assert 崩进程）——这类只能错误路径。

## 前端 e2e（Playwright）坑（本仓库实测）

- 剪贴板断言：`test.use permissions:['clipboard-read','clipboard-write']`；Windows 剪贴板是
  `\r\n`（`split(/\r?\n/)`）。
- `page.route` **后注册者优先**——宽模式路由会遮蔽早注册的精确路由并 continue() 穿透到 vite
  proxy（ECONNREFUSED 假象）。
- `waitForRequest` 会抓到初始加载的请求——先 `networkidle` 再注册。
- `button:has-text("Create User")` 同时匹配页头与模态按钮——用
  `getByRole('button',{name,exact:true})`；`getByRole('dialog',{name:/regex/})` 的正则要匹配
  aria-label **全文**。
- 触发真实登出的用例必须先种登录态，否则守卫弹 /login 找不到菜单按钮。
- mock 假 token 用 `process.env.E2E_MOCK_AT ?? 'fixture'`（Mimosa 惯例）。
- e2e 夹具账号可能被其它会话/审计误删——验收前重建（register 建回即可）。
- 改 mock 契约必须跟真后端契约同步（见 `.claude/rules/frontend-contract.md`）。

## 环境相关的既有失败（勿误判为回归）

- Redis 契约测试（Contract.*Redis）：**单跑全过**；full_test 窗口内的失败多为瞬态（Redis 就绪
  时序/隔离），复跑即绿——跑前停 fulla-server（契约测试自己起服务器占 5555）。
- OAuth2Tests 两个既有隔离性失败（固定 email 字面量 + 限流计数）：单跑全过定性。
- WSL 的 `SdkSmoke.FullStack` 恒挂（drogon_ctl 未装）= 环境问题。
