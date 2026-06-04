#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DASHBOARD_DIR="$ROOT_DIR/web/dashboard"
MOBILE_DIR="$ROOT_DIR/mobile/LabGuard"

HOST_IP="${HOST_IP:-$(hostname -I | awk '{print $1}') }"
HOST_IP="${HOST_IP// /}"
WEB_PORT="${WEB_PORT:-5173}"
EXPO_PORT="${EXPO_PORT:-8081}"
MQTT_WS_PORT="${MQTT_WS_PORT:-9001}"
MQTT_TCP_PORT="${MQTT_TCP_PORT:-1884}"
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

free_startup_ports() {
  if [ "$FORCE_FREE_PORTS" != "1" ]; then
    return
  fi

  kill_port_users "$MQTT_TCP_PORT"
  kill_port_users "$MQTT_WS_PORT"
  kill_port_users "$WEB_PORT"
  kill_port_users "$EXPO_PORT"
}

DASHBOARD_PID=""
MOBILE_PID=""

cleanup() {
  local exit_code=$?
  echo
  echo "[shutdown] 正在停止 LabGuard 联调服务..."
  if [ -n "$MOBILE_PID" ] && kill -0 "$MOBILE_PID" 2>/dev/null; then
    kill "$MOBILE_PID" 2>/dev/null || true
    wait "$MOBILE_PID" 2>/dev/null || true
  fi
  if [ -n "$DASHBOARD_PID" ] && kill -0 "$DASHBOARD_PID" 2>/dev/null; then
    kill "$DASHBOARD_PID" 2>/dev/null || true
    wait "$DASHBOARD_PID" 2>/dev/null || true
  fi
  exit "$exit_code"
}
trap cleanup INT TERM EXIT

if [ ! -x "$DASHBOARD_DIR/run_dashboard_stack.sh" ]; then
  echo "[error] 找不到 dashboard 启动脚本：$DASHBOARD_DIR/run_dashboard_stack.sh" >&2
  exit 1
fi

if [ ! -x "$MOBILE_DIR/run_mobile_app.sh" ]; then
  echo "[error] 找不到 mobile 启动脚本：$MOBILE_DIR/run_mobile_app.sh" >&2
  exit 1
fi

echo
cat <<EOF
========================================
LabGuard 电脑端 + 手机端联合调试
Dashboard: http://localhost:${WEB_PORT}
局域网 Dashboard: http://${HOST_IP}:${WEB_PORT}
手机 MQTT WS: ws://${HOST_IP}:${MQTT_WS_PORT}
板子 MQTT: mqtt://${HOST_IP}:${MQTT_TCP_PORT}
Expo: exp://${HOST_IP}:${EXPO_PORT}
========================================

板子烧录前请在 menuconfig 中确认：
  CONFIG_LABGUARD_WIFI_SSID
  CONFIG_LABGUARD_WIFI_PASSWORD
  CONFIG_LABGUARD_MQTT_URI="mqtt://${HOST_IP}:${MQTT_TCP_PORT}"

按 Ctrl+C 可同时关闭 dashboard/broker 和 Expo 服务
========================================
EOF

free_startup_ports

echo "[start] 启动 dashboard + MQTT broker..."
(
  cd "$DASHBOARD_DIR"
  HOST_IP="$HOST_IP" WEB_PORT="$WEB_PORT" MQTT_WS_PORT="$MQTT_WS_PORT" MQTT_TCP_PORT="$MQTT_TCP_PORT" FORCE_FREE_PORTS=0 ./run_dashboard_stack.sh
) &
DASHBOARD_PID=$!

sleep 3
if ! kill -0 "$DASHBOARD_PID" 2>/dev/null; then
  echo "[error] dashboard/broker 启动失败" >&2
  exit 1
fi

echo "[start] 启动 mobile Expo..."
(
  cd "$MOBILE_DIR"
  HOST_IP="$HOST_IP" EXPO_PORT="$EXPO_PORT" MQTT_WS_PORT="$MQTT_WS_PORT" CLEAR_CACHE="$CLEAR_CACHE" FORCE_FREE_PORTS=0 ./run_mobile_app.sh
) &
MOBILE_PID=$!

sleep 2
if ! kill -0 "$MOBILE_PID" 2>/dev/null; then
  echo "[error] mobile Expo 启动失败" >&2
  exit 1
fi

echo
echo "[ready] 联合调试服务已启动"
echo "  Dashboard: http://localhost:${WEB_PORT}"
echo "  局域网 Dashboard: http://${HOST_IP}:${WEB_PORT}"
echo "  手机 MQTT WS: ws://${HOST_IP}:${MQTT_WS_PORT}"
echo "  板子 MQTT: mqtt://${HOST_IP}:${MQTT_TCP_PORT}"
echo "  Expo: exp://${HOST_IP}:${EXPO_PORT}"
echo

wait "$DASHBOARD_PID" "$MOBILE_PID"
