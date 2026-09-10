# `.claude/hooks/` — 本地门禁/守卫脚本

2026-09-10 随 `.agent/` 经验合并入 `.claude/`（权威源）。

| 脚本 | 用途 | 接线状态 |
|---|---|---|
| `local-gates.sh` | 推送/提交前五门（spec 治理 / SDK drift / api-diff / 迁移 / 测试命名） | **已接线**：`.claude/settings.json` 的 `Bash(git commit*)` PreToolUse 钩子（取代旧的全量 ctest 预提交门） |
| `port-guard.sh` | 检查 5555/6379 占用与 Redis 健康态，只报告不杀进程 | 手动跑（起服务器/跑测试/bench 前） |
| `redis-watchdog.ps1` | full_test/bench 窗口内的 Redis 僵尸看门狗（30s 检测 + 自动拉起第二实例） | 手动跑（跑 full_test 时另窗口常驻） |

## 其它工具接线（如需）

ZCode 等：把 `local-gates.sh` 挂到对应 settings 的推送类 PreToolUse matcher；三个脚本均可独立
手动运行（幂等、只报告/自愈、输出人话），`/preflight` 命令会引导调用。

## 编写新钩子的约束（本机实测）

- 脚本**绝不 eval/拼接**动态命令（Mimosa 拦命令注入模式）；用参数列表直接调用。
- 脚本文件改动用 Write/Edit 工具落盘——Bash `cp/mv/sed` 写 `.sh/.ps1` 会被 Mimosa 拦截。
- 拦截类钩子 exit 1 + 人话错误输出（参照 settings.json 现有 ORM/config.prod 守卫风格）。
- Windows Git Bash 用 `netstat -ano`（无 `ss`）；PowerShell 脚本注意 5.1 兼容。
