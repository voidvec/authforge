---
name: preflight
description: 推送前本地预检——三门（spec 治理/SDK drift/api-diff）+ 迁移门 + 测试命名门 + WERROR 构建 + 前端门，全部绿再推
---

# /preflight — 推送前本地预检

目的：把 CI 会在 PR 上挂掉的问题在本地提前抓掉（每项都对应真实的 CI 红灯史）。
背景见 `.claude/skills/ci-gate-sync` 与 `.claude/skills/ci-failure-triage`。

## 执行步骤

按序执行，每步记录 PASS/FAIL；任何 FAIL 先修再推。

1. **一键五门**（spec 治理 / SDK drift / api-diff / 迁移 / 测试命名）：
   ```bash
   bash .claude/hooks/local-gates.sh
   ```
2. **WERROR 构建对齐**（改过任何 C++ 时；配置一次后增量跑，注意增量缓存假绿——改过头文件删
   对应 .obj 或 `--clean-first`）：
   ```bash
   cmake --preset windows-msvc -DFULLA_WERROR=ON && cmake --build build/windows-msvc --config Release
   ```
3. **前端门**（改过 frontends/** 时，在对应目录）：
   ```bash
   npm run build && npx playwright test --reporter=line
   # 改了组件双副本/错误码文案时另跑：node ../scripts/check-ui-sync.mjs（repo 根）
   ```
4. **汇总输出**：一张 PASS/FAIL 表 + FAIL 项的修复指针（对应 skill 小节）。

## 注意

- 交互式确认：若用户只要快速检查（没改 C++），步骤 2 可跳过并注明。
- 完整验证（合并前）仍走 `/full-test`（见 `.claude/skills/full-test`）；本命令是**推送前**的
  快速门，不替代全量。
