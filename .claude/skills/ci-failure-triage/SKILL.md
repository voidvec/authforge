---
name: ci-failure-triage
description: CI 红灯排障手册——先分闪失还是真缺陷，再按跨编译器盲区矩阵/conan.lock/权限天花板/gcov 管道等已知根因分类，避免盲目整体重跑
---

# CI 失败排障

## 第一步：定性（闪失 vs 真缺陷）

- **先看能不能本地复现**：`cmake --preset windows-msvc -DFULLA_WERROR=ON` + 全量 build，
  绿则大概率是 CI 侧问题（闪失/环境/盲区）。
- 已知时序敏感 flake：`JwkManagerTest.VerifyJwt_Expired_IsExpired_IncludingExactBoundary`
  （exp==now 边界，慢 runner 时钟漂移翻车）；1 秒墙钟竞态范式（测试先捕获 now、helper 内另取
  `std::time`，跨秒即翻——修法 = helper 加显式 base 参数把两处钉到同一 now）。
- **PAT 不能 rerun run**（403）——重触发法 = 向分支推空提交
  （`git commit --allow-empty` + push，走 SSH）。
- **run 整体红 ≠ 产物缺失**：逐 job 看，别盲目整体重跑（v1.0.0 实案：镜像/SDK/SBOM 全绿只有
  两个尾部 job 挂）。

## 跨编译器 WERROR 盲区矩阵（Build&Test 三平台均 `-DFULLA_WERROR=ON`）

| 编译器 | 独有抓到的 | 典型 |
|---|---|---|
| MSVC | C4458 捕获/成员遮蔽、C4389 enum==unsigned | C4458：`auto sharedCb` 局部遮蔽外层捕获 |
| GCC | `-Werror=unused-function`（MSVC 不诊断未用静态函数） | 删改匿名 ns 函数后残留 |
| Clang(macOS) | `-Wunused-const-variable`、两条 #include 挤一行（-Wextra-tokens） | CRLF 写入事故把 include 挤成一行 |
| 仅本机 | C4819 代码页告警（CI 无此腿） | 新写注释含非 ASCII → 本地注意，写码时只用 ASCII |

本地等价验证 = WERROR=ON 全量 build；**要强制重编需删对应 .obj，增量缓存不重编会给假绿**。

## 已知根因分类（按 workflow）

**Static Checks（8 项）**：见 `ci-gate-sync` skill 的失败模式速查。

**Build&Test**：
- conan.lock 跨平台纪律：**Windows 单配置 regen 会丢 Linux-only 依赖**（util-linux-libuuid
  等）→ 新增依赖时手工往 master 的跨平台 lock 插条目（RREV+timestamp 从本机 cache 拷），
  不要整文件重生成。
- 0xc0000409（MSVC fast-fail）= 未捕获 C++ 异常 → terminate，不是栈溢出；Git Bash 显示退出码
  127（截断）。定位看 `build/.../tests/Release/logs/drogon.log` 尾部 = 死前最后一个请求。
  最常见根因：测试手写 SQL 列名与 schema 不符 → 测试里造数据走 admin API，别手写 INSERT。

**Sanitizer 腿（PR ASan+UBSan / nightly TSan）**：
- 同名工具类有**多份拷贝**时（如 TotpUtils 曾有 libs/drogon 与 libs/identity 两份），修 UBSR
  发现要全仓 grep 同名源文件，别只修报错栈里那份。
- 本机 Windows 跑不了 GCC sanitizer 预设——sanitizer 门只能 CI 验证（首跑 = triage）。
- controller setter 注入缝的测试不适合 TSan（主线程写裸指针、loop 线程读）。

**Coverage**：
- `gcov -j -p` 把 `.gcov.json.gz` 写进**当前目录**（`#` 路径前缀文件名），不在 .gcda 旁——生成
  步骤必须自诊断 + cwd 归位 + 零输出**硬失败**（曾被 `|| true` + `>/dev/null` 掩盖成空转）。
- 生成物找到后用 `scripts/measure_coverage.py --json` 聚合（gcovr 8.6 有 path-matching bug）；
  ratchet 门 `--ratchet`（>0.5pp 降即挂；基线缺库=挂；seed 流程 = CI seed 模式打印 JSON →
  从 `gh run view --log` 提取 → 规范化提交）。

**Frontend 腿**：
- "假红"识别：vitest 全过、失败在其后 `npm ci` 的 **ERESOLVE peer 冲突**（如 vue-router 5 要求
  vite≥7 而项目 vite 6）——PR checkout 分支所以红在 PR 上，master 不受影响；处置 = 关闭暂缓，
  待协调式大版本升级。
- TS7 过渡期（typescript→`npm:@typescript/typescript6` 别名给 eslint、`@typescript/native`→
  tsc）下 peer 冲突 `npm install` 需 `--legacy-peer-deps`；lockfile 冲突不手工解——以合并后
  package.json 为准删 node_modules 全量 `npm install` 重新生成。

**startup_failure（零 job）**：reusable workflow 权限天花板（见 ci-gate-sync skill）；
排查路径 `gh run list` → run 页 HTML grep "Invalid workflow file"。

## gh CLI 权限边界（排障时别撞墙）

可用：issue/PR 创建、PR 评论、`pr close --comment`、读一切。403：issue 评论/关闭、merge、
repo 元数据编辑、release 删除/编辑、run rerun。撞 403 就准备**给用户的网页操作文案**，不要
反复重试 API。
