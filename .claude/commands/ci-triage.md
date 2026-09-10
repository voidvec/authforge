---
name: ci-triage
description: CI 红灯排障入口——拉取失败 run 的逐 job 状态，按已知根因分类（闪失/盲区/门禁同步缺失），给出修复或重触发建议
---

# /ci-triage — CI 失败分类排障

背景知识与根因表见 `.claude/skills/ci-failure-triage`（必读）；门禁同步缺失的修复见
`.claude/skills/ci-gate-sync`。

## 流程

1. **收集事实**（不猜）：
   ```bash
   gh run list --branch <分支> --limit 3
   gh run view <run-id> --log-failed | head -100
   ```
   注意 PAT 不能 rerun（403）；重触发 = 推空提交（`git commit --allow-empty && git push`，SSH）。

2. **定性三问**：
   - 失败是**逐 job** 还是整体？（整体红 ≠ 产物缺失，先看 job 矩阵）
   - 本地能否以 CI 等价条件复现？（WERROR=ON 构建 / 对应 python 门本地跑）
   - 失败用例是否命中已知 flake 清单（exp==now 边界、1 秒墙钟竞态、Redis 契约瞬态、
     OAuth2Tests 既有隔离缺陷）？命中 → 空提交重触发 + 修 flake 根因另行登记。

3. **按 job 归类**（对照 ci-failure-triage 的矩阵）：
   - Static Checks → 8 项静态检查各自的可能根因表
   - Build&Test 三平台 → 跨编译器 WERROR 盲区矩阵 + conan.lock 跨平台 + 0xc0000409 诊断
   - Sanitizer → 同名工具类多拷贝、TSan 不适用注入缝测试
   - Coverage → gcov CWD 坑、ratchet 基线
   - Frontend → ERESOLVE 假红识别、TS7 过渡期 peer、lockfile 重生成
   - startup_failure（零 job）→ reusable workflow 权限天花板

4. **输出**：失败 job → 根因分类（真缺陷 / 同步点缺失 / 环境闪失 / 门禁需 ratify）→ 对应
   修复动作清单。涉及基线 ratify（api-diff/migration/oasdiff）时附政策依据，**ratify 决策
   保留给用户确认**（--force 类操作不静默执行）。

## 边界

- 本命令只诊断 + 建议；修复走正常 implement 流程。
- 合并权限在用户（网页 rebase-merge）；诊断完成后把 PR 状态摘要给用户即可。
