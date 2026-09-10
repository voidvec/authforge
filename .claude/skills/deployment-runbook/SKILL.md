---
name: deployment-runbook
description: fulla 部署 runbook——.env.docker.dev/.prod 消费矩阵、生产启动校验、WSL prod 预演五步（域名/自签 CA/全链路端点测试）、迁正式服务器流程；部署或 staging 前必读
---

# 部署 Runbook（Docker prod / WSL 预演）

来源：2026-09-02 WSL prod 预演方案定稿 + .env.docker 双文件交付复盘 + 本地 docker 栈排障实录。

## env 文件体系与消费真相

- **`.env.docker.dev`**（dev compose 用）：只有 `FULLA_SMTP_*`（全空=Console 模式）和
  `FULLA_CMAKE_PRESET` 真被消费——dev compose **硬编码一切**（DB/Redis 密码、vue-client
  secret、`FULLA_FRONTEND_URL=http://localhost:8080`、不传 `FULLA_ENV`=development 模式+
  临时 JWT key）。
- **`.env.docker.prod`**（prod compose 用）：真实域名（issuer=裸域名如 `https://auth.fulla.dev`，
  nginx 把 `/.well-known/`、`/oauth2/`、`/api/` 代理到后端、`/admin/`→fulla-admin、`/`→SPA）、
  `FULLA_AUTO_MIGRATE=false`（走 `--profile migrate` 一次性迁移）、
  `DETAILED_VALIDATION_ERRORS=false`、openssl rand 强密钥。证书放
  `deploy/nginx/ssl/{fullchain,privkey}.pem`。
- compose `--env-file` **只做 `${VAR}` 插值，不注入容器 env**——容器 env 只来自 service
  `environment:`。
- **死变量**（写了也无消费方）：`DOMAIN`；`FULLA_BOOTSTRAP_ADMIN_PASSWORD`（main.cc getenv
  读但 compose 不传 → admin 密码首启随机生成、日志打印一次，AdminBootstrapper）。
- 切换 env 文件只触发 backend 重建（dev compose 里只有 backend 的 environment 引用
  `${FULLA_SMTP_*}`）。
- SMTP 模式判定：HOST+USER+PASSWORD **三者非空**才真发信，任一空=Console（只打日志）。

## 生产启动校验（#102）

https issuer + 非默认 DB/Redis 密码 + 签名钥必配 + confidential client 默认 secret 拒启。
**dev seed SQL 不能打进 prod 库**（含默认 secret 的种子会被生产校验拒启）。

## WSL prod 预演（比 dev compose 多覆盖的部分）

WSL2 真内核使 nginx TLS 链路、`FULLA_ENV=production` 严格校验、migrate profile、健康检查门
全部真实。流程：

1. **ext4 全新 clone**（勿 /mnt；勿复用 benchmark 克隆）。WSL 工具链现状：gcc 13.3 / cmake
   3.28 / Python3 / PG16（systemd）就绪；**conan 需现装**（pipx 或
   `pip3 --user --break-system-packages`）。
2. **引擎冲突**：Docker Desktop WSL 集成与 Windows dev 栈**共用引擎**——`fulla-postgres` 等
   容器名 + 网络直接冲突，先 down 掉 dev 栈；或用 WSL 内独立 docker 引擎最干净。
3. **镜像**：Dockerfile FROM 全在 docker.io → 首次 `up -d --build` 本地构建**完全绕开被墙的
   ghcr**；⚠ 之后**不带 `--build` 的 `up` 会尝试 pull `ghcr.io/voidvec/fulla-*` → 失败**——
   要么总带 `--build`，要么从 `ghcr.nju.edu.cn` pull 后 retag 本地 tag。
4. **起栈五步**（`.env.docker.prod` 头部注释）：infra → `--profile migrate run --rm migrate`
   → 全栈。staging 阶段 SMTP 填真实授权码（比 Console 多验一条真实发信链路）。
5. **域名+证书（预演核心技巧）**：
   - WSL `/etc/hosts` + Windows hosts **都**加 `127.0.0.1 auth.fulla.dev`（WSL2 localhost
     转发让 Windows 浏览器直达 WSL 的 80/443）；**正式部署后必须删两处假解析**，否则真实
     DNS 生效后本地仍被劫持。
   - 自签证书 SAN=auth.fulla.dev 放 `deploy/nginx/ssl/`。
   - **全链路端点测试**：59/52 脚本是裸 `curl -s`（无 -k）但 BASE_URL 参数化，且用到的全部
     路径经 nginx 代理 → 把自签 **CA 装进 WSL 信任库**
     （`/usr/local/share/ca-certificates/` + `update-ca-certificates`）后可直跑
     `test-oauth2-endpoints.sh https://auth.fulla.dev` 走完整生产链路；CA 再导入 Windows
     信任存储则浏览器无告警（WebAuthn 等安全上下文 API 不受影响）。

## 迁正式服务器

目标机=腾讯云轻量（SSH 直连；Cloudbase 插件管不了 Lighthouse，CloudBase≠Lighthouse）。流程：
全新 clone + 同份 `.env.docker.prod`（换 certbot 真证书 + 确认 SMTP 授权码 + 可选
`openssl rand` 轮换三个随机密钥）+ 同 migrate 序列；**不迁移任何数据/卷**（WSL 侧
`docker compose down -v` 清场）。边界：WSL2 的 80/443 从局域网访问需 netsh portproxy 或
mirrored 网络模式（本机自测用不到）。

## 排障速查

- postgres 有 healthcheck / redis 没有 = **刻意设计勿补齐**（PG 硬依赖慢启动 vs Redis 软依赖
  cache 默认关 + 软失败到 uncached path）。
- 容器全 Up 但站点打不开：先 curl 看 **Location 头**（实案：nginx `absolute_redirect` 丢
  映射端口 8081，修法=server 块 `absolute_redirect off;`），别急着查容器/构建。
- 本地构建镜像名带 `docker-` 前缀 = compose 项目名陷阱（详见 local-env-runbook）。
- 改运行时连接池等配置：改 `apps/server/config/config.json`（bind-mount 覆盖镜像内
  config.prod.json，见 local-env-runbook Docker 节）。
