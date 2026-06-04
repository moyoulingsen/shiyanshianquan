#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

EXPO_PORT="${EXPO_PORT:-8081}"
HOST_IP="${HOST_IP:-$(hostname -I | awk '{print $1}') }"
HOST_IP="${HOST_IP// /}"
MQTT_WS_PORT="${MQTT_WS_PORT:-9001}"
CLEAR_CACHE="${CLEAR_CACHE:-1}"
FORCE_FREE_PORTS="${FORCE_FREE_PORTS:-1}"

kill_port_users() {
  local port="$1"
  local pids=""

  if command -v lsof >/dev/null 2>&1; then
    pids="$(lsof -tiTCP:"$port" -sTCP:LISTEN 2>/dev/null | tr '\n' ' ' || true)"
  elif command -v fuser >/dev/null 2>&1; then
    pids="$(fuser -n tcp "$port" 2>/dev/null | tr ' ' '\n' | tr '\n' ' ' || true)"
  else
    echo "[warn] 未安装 lsof/fuser，无法自动释放端口 $port" >&2
    return 0
  fi

  pids="$(printf '%s' "$pids" | xargs -r echo 2>/dev/null || true)"
  if [ -z "$pids" ]; then
    return 0
  fi

  echo "[cleanup] 释放端口 $port（PID: $pids）..."
  kill $pids 2>/dev/null || true
  sleep 1

  local still_running=""
  for pid in $pids; do
    if kill -0 "$pid" 2>/dev/null; then
      still_running="$still_running $pid"
    fi
  done

  still_running="$(printf '%s' "$still_running" | xargs -r echo 2>/dev/null || true)"
  if [ -n "$still_running" ]; then
    echo "[cleanup] 端口 $port 仍被占用，强制结束 PID: $still_running"
    kill -9 $still_running 2>/dev/null || true
    sleep 1
  fi
}
if ! command -v npm >/dev/null 2>&1; then
  echo "[error] npm 未安装，无法启动手机端" >&2
  exit 1
fi

if [ ! -d node_modules ]; then
  echo "[setup] 安装 LabGuard 手机端依赖..."
  npm install
fi

EXPO_ARGS=(start --lan --port "$EXPO_PORT")
if [ "$CLEAR_CACHE" = "1" ]; then
  EXPO_ARGS+=(--clear)
fi

echo
cat <<EOF
========================================
LabGuard 手机端开发服务即将启动
Expo 地址:  exp://${HOST_IP}:${EXPO_PORT}
MQTT 地址:  ws://${HOST_IP}:${MQTT_WS_PORT}
========================================

使用方法：
1. 手机和电脑连接同一个 Wi‑Fi
2. iPhone/Android 安装 Expo Go
3. 用手机相机或 Expo Go 扫终端二维码
4. App 打开后，在顶部 MQTT 输入框填写：
   ws://${HOST_IP}:${MQTT_WS_PORT}
5. 点击“连接”

如果扫码后打不开：
- 确认电脑防火墙没有拦截 ${EXPO_PORT}
- 确认手机和电脑在同一局域网
- 可换端口启动：EXPO_PORT=8082 ./run_mobile_app.sh
- 默认会清缓存启动；如需保留缓存：CLEAR_CACHE=0 ./run_mobile_app.sh

按 Ctrl+C 停止手机端开发服务
========================================
EOF

if [ "$FORCE_FREE_PORTS" = "1" ]; then
  kill_port_users "$EXPO_PORT"
fi

echo
npm exec -- expo "${EXPO_ARGS[@]}"
