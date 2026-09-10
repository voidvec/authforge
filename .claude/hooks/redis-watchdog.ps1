# redis-watchdog.ps1 — full_test / bench 期间的 Windows Redis 看门狗
# 背景：Windows 原生 Redis 在长测试窗口内会进入僵尸态（进程在、6379 无监听），导致
#       /health/ready 永久挂起 + 7 个 Redis 依赖测试失败；已两次复现（memory:
#       windows-bench-contention-and-fulltest-serialization #14、perf-deep-p2-state）。
# 用法（另开一个窗口）:
#   powershell -NoProfile -ExecutionPolicy Bypass -File .claude\hooks\redis-watchdog.ps1
# 可选参数: -RedisDir "D:\work\services\Redis-x64-5.0.14.1" -IntervalSec 30
param(
  [string]$RedisDir = "D:\work\services\Redis-x64-5.0.14.1",
  [int]$IntervalSec = 30
)

while ($true) {
  $listening = netstat -ano | Select-String "TCP\s+\S+:6379\s.*LISTENING"
  if (-not $listening) {
    $stamp = Get-Date -Format "HH:mm:ss"
    Write-Host "[$stamp] 6379 无监听（僵尸或未启动）——拉起第二实例（旧进程无害残留）"
    Start-Process "$RedisDir\redis-server.exe" `
      -ArgumentList '--port','6379','--requirepass','123456','--bind','127.0.0.1' `
      -WindowStyle Hidden
    # 注意：不用 redis.windows.conf（该配置起法会莫名退出——见 local-env-runbook）。
    # 新实例 cwd 会落 dump.rdb，如在工作目录启动记得事后清理。
  } else {
    Write-Host "." -NoNewline
  }
  Start-Sleep -Seconds $IntervalSec
}
