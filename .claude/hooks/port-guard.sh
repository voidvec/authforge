#!/usr/bin/env bash
# port-guard.sh — 起服务器/跑测试前检查 5555 与 6379 占用（fulla-server 与 fulla-tests 双双占 5555，
# 互斥；Redis 僵尸态=进程在但无监听）。只报告不动手，人/代理决定杀谁。
# 用法: bash .claude/hooks/port-guard.sh
set -u

check_port() {
  local port="$1" label="$2"
  local out
  # Windows Git Bash 有 netstat；Linux 用 ss 兜底
  if command -v netstat >/dev/null 2>&1; then
    out=$(netstat -ano 2>/dev/null | grep -E "TCP.*[:.]$port .*LISTEN" | head -3)
  elif command -v ss >/dev/null 2>&1; then
    out=$(ss -ltnp 2>/dev/null | grep -E "[:.]$port " | head -3)
  else
    echo "[$label:$port] 无 netstat/ss 可用，跳过"; return
  fi
  if [ -n "$out" ]; then
    echo "[$label:$port] 已被占用（继续前先处理）:"
    echo "$out" | sed 's/^/    /'
    if [ "$port" = "5555" ]; then
      echo "    提示: taskkill //IM fulla-server.exe 可杀服务器（注意会跨检出生效）；"
      echo "          fulla-tests.exe 也监听 5555，跑测试前服务器必须停。"
    fi
  else
    echo "[$label:$port] 空闲"
  fi
}

check_port 5555 "server/tests"
check_port 6379 "redis"

# Redis 僵尸态判据补充：有 LISTEN 才算活；redis-cli 可用时顺手 PING
if command -v redis-cli >/dev/null 2>&1; then
  if redis-cli -p 6379 -a 123456 --no-auth-warning PING 2>/dev/null | grep -q PONG; then
    echo "[redis] PONG（dev 口令验证通过）"
  else
    echo "[redis] 无 PONG——若上面显示 6379 有 LISTEN 但 PING 失败，按 local-env-runbook 处理口令/实例"
  fi
fi
