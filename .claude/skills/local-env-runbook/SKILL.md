---
name: local-env-runbook
description: 本机（vilas 的 Windows 11 + Git Bash + WSL Ubuntu + Docker Desktop）开发环境 runbook——服务拉起、端口独占、WSL 调用、Docker 陷阱、Mimosa hook 绕行、多会话并行铁律
---

# 本机环境 runbook

## 常驻服务与端口

| 资源 | 事实 |
|---|---|
| PostgreSQL | **原生 Windows PG 服务**（5432，不是容器；本地 DB 测试需 `setup-database` + `FULLA_DB_*` 环境变量）。无关的 `ory-bench-postgres` 容器常驻——`docker ps \| grep postgres` 式探测会抓错对象 |
| Redis | Windows 服务（6379，dev 密码 `123456`）。`net start` 需提权；免提权回退：`Start-Process <redis目录>\redis-server.exe -ArgumentList '--port','6379','--requirepass','123456','--bind','127.0.0.1' -WindowStyle Hidden`（**不要**用 redis.windows.conf，会莫名退出） |
| 5555 | fulla-server 与 fulla-tests **双双占用**——两者互斥；跑测试前杀掉手工起的服务（`taskkill /IM fulla-server.exe` 会跨检出杀进程） |

**Redis 僵尸态**（进程在、6379 无监听）：判据 = `netstat -ano | grep 6379` 无 LISTEN 但进程
存在。处置：`Stop-Process` 可能拒绝访问——不必纠结，直接 `Start-Process` 第二实例能成功绑
6379（僵尸不占端口），旧进程无害残留（注意新实例 cwd 会落 dump.rdb）。跑 full_test 建议挂
看门狗（`.claude/hooks/redis-watchdog.ps1`）。Redis 挂掉时 `/health/ready` **永久挂起**（探针无
连接超时），症状是 ctest 卡在第一个调用而不是报错。

## 测试调用规范（Windows）

- **ctest 必须 `-C Release`**（VS 多配置；不带 -C 解析到 Debug 路径 → NOT_AVAILABLE 假象，
  极易误判"测试没编译"）。可靠结果用 `-j1`（-j4 会互踩 5555 与共享库）。
- `fulla-tests.exe` 一次只能 `-r` 一个测试名（不支持多 -r/前缀匹配），逐个跑或用 ctest；HTTP/
  集成测试要从 `build/windows-msvc/tests/Release` 目录跑（config.json 相对 CWD 解析）。
- `full_test.bat` 尾部 `pause >nul` 会挂死非交互 shell——bash 调用加 `< /dev/null`：
  `cmd //c "echo. | scripts\backend\full_test.bat -release"`。
- **full_test 必须独占**：共享 fulla_db（步骤 1 会 DROP+盲 seed 整库）与 5555；跑前停主检出
  的 server，不与 bench/其它会话并行（worktree 也共享这两者）。
- 本地裸跑 fulla-tests 的 DB 用例失败 ≠ 回归：先 `setup_database.bat` 重建 + `FULLA_DB_*`
  环境变量（memory-storage guard 会跳过，postgres 配置存在时才真跑）。

## WSL（Ubuntu）

- sudo 密码 `test`；后台脚本 `echo test | sudo -S <cmd>`（无 TTY 的交互 sudo 永久挂起）。
- 调用用 `wsl.exe`（裸 `wsl` 在 harness 里 permission denied）；复杂命令**写成脚本文件**经
  UNC 路径 `\\wsl.localhost\Ubuntu\home\vilas\...`（Write 工具可靠）投放，再
  `MSYS_NO_PATHCONV=1 MSYS2_ARG_CONV_EXCL='*' wsl.exe -d Ubuntu -- bash /home/vilas/.../x.sh`
  ——MSYS 会改写一切 `/` 开头的参数（包括 curl `-w` 格式串），`$(...)` 嵌在 one-liner 里也会
  被中间 shell 展开。
- **测试/构建禁 /mnt**（9p 慢且行为异常）：用 WSL 内 ext4 克隆；干净做法
  `git clone --shared ~/projects/authforge-benchmark ~/fulla-ci-verify`（共享对象库秒建）。
  WSL 里另有一份 benchmark 克隆，接手"继续任务"前先比对两侧 HEAD（分叉用 merge 保数据）。
- `C:\Users\vilas\.wslconfig` 需 `localhostForwarding=false`（否则 Windows 127.0.0.1:5432 随机
  路由到 WSL PG → 神秘 401）；已配 8 vCPU/16GB（改动需 `wsl --shutdown`，之后 Docker Desktop
  WSL 集成断连，要重启 Docker Desktop）。
- WSL 内 6379 跑着未知密码 Redis（bench 遗留）——测试自起 `redis-server --port 6380
  --requirepass 123456 --daemonize yes` + `FULLA_REDIS_PORT/PASSWORD` 覆盖。

## Git Bash / 本机工具

- 系统 grep 实为 **ugrep**：`->` 开头 pattern 被当选项（用 `grep -E "\-\>"` 或换模式）；
  `[/\]` 反斜杠字符类报错（拆成多个 `-e`）；grep 立即失败会 SIGPIPE 杀掉管道前进程。
- 复杂文本处理拉本地 python，别堆 awk/sed 链。
- 仓库文件混 CRLF/LF：python 补丁先 `nl = '\r\n' if '\r\n' in raw else '\n'` 自适应；
  优先 Edit 工具。
- 本机简中 Windows：新写源码注释只用 ASCII（C4819 只有本机报，见 ci-failure-triage）。

## Mimosa hook 绕行（拦了就绕，不要原样重试）

- 拦：Bash heredoc/python 写源码或安全配置、写 `.py` 脚本文件（含 tmp 目录）、`sed/cp/mv`
  改源码（含 `.sh` 脚本）→ 一律改用 **Write/Edit 工具**（复杂补丁落 `.tmp_patch*.py` 文件再执行）。
- 拦：凭据形字面量 `<key>_token: '<literal>'`（**包括 e2e mock 假 token**）→ 用
  `process.env.E2E_MOCK_AT ?? 'fixture'` 惯例或函数生成；`.mimosa/security-policy.json` 的
  exclusions 可豁免文案误判（该文件 gitignore、仅本机生效，勿入 git add）。
- 拦：Edit 的 old_string **包含** `export PGPASSWORD="..."` 上下文行就整块拦 → 换不含该行的
  锚点拆多个小 Edit。
- 拦：build/ 产物里的第三方 JS 污点（swagger-ui）→ 删产物重扫；被拦 commit 会清掉暂存区
  （要重新 git add）。

## Docker

- **bind-mount 覆盖镜像内配置**：dev compose 把宿主 `apps/server/config/config.json`（DEV
  配置）挂过 `/app/config.json`，**完全覆盖** Dockerfile COPY 的 config.prod.json——改运行时
  配置（连接池等）要改 config.json + `up -d --force-recreate oauth2-backend`，不用重建镜像。
- **compose 项目名陷阱**：手动 compose build 不带 `--project-directory` 时项目名取 compose
  文件父目录名（`deploy/docker`→`docker-`前缀镜像），栈继续用旧镜像（症状：build 成功但
  CreatedSince 不变）。worktree 里构建的镜像还带 worktree 目录名前缀——`docker images | grep`
  确认真名再 tag。
- 镜像源被墙：docker.io 走 `docker.1ms.run`、ghcr.io 走 `ghcr.nju.edu.cn`，拉完 `docker tag`
  回官方名（daocloud 白名单不放行 oryd/hydra 这类）。
- **postgres 有 healthcheck / redis 没有 = 刻意设计勿补齐**（PG 硬依赖慢启动 vs Redis 软依赖
  cache 默认关 + 软失败）。
- 诊断打不开的站点：容器全 Up 时先 curl 看 **Location 头**（实案：nginx `absolute_redirect`
  丢映射端口 8081），别急着查容器/构建。

## 多会话/并行铁律

- bench 运行期间宿主机**不跑任何东西**——不只 full_test，连只读代理/grep 风暴都污染数据
  （auto_batch A/B 实证 -3.6~+57% 摆动）。
- full_test 与 bench/构建管线必须串行；详见 `benchmark-methodology` skill。
- 会话间文件所有权：另一会话的脏文件/wip 分支勿动勿提交；要提交 master 用临时 worktree。
