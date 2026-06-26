#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

MQTT_TCP_PORT="${MQTT_TCP_PORT:-1884}"
MQTT_WS_PORT="${MQTT_WS_PORT:-9001}"
SERIAL_WS_PORT="${SERIAL_WS_PORT:-8787}"
LABGUARD_PORT="${LABGUARD_PORT:-/dev/ttyACM0}"
WEB_PORT="${WEB_PORT:-5173}"
HOST_IP="${HOST_IP:-$(hostname -I | awk '{print $1}') }"
HOST_IP="${HOST_IP// /}"
FORCE_FREE_PORTS="${FORCE_FREE_PORTS:-1}"
ENABLE_SERIAL_BRIDGE="${ENABLE_SERIAL_BRIDGE:-1}"

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
  if [ "$ENABLE_SERIAL_BRIDGE" = "1" ]; then
    kill_port_users "$SERIAL_WS_PORT"
  fi
  kill_port_users "$WEB_PORT"
}
if ! command -v npm >/dev/null 2>&1; then
  echo "[error] npm 未安装，无法启动 dashboard" >&2
  exit 1
fi

if ! command -v mosquitto >/dev/null 2>&1; then
  echo "[error] mosquitto 未安装，无法启动本地 MQTT broker" >&2
  exit 1
fi

if [ ! -d node_modules ]; then
  echo "[setup] 安装 dashboard 依赖..."
  npm install
fi

BROKER_PID=""
BRIDGE_PID=""
WEB_PID=""

cleanup() {
  local exit_code=$?
  echo
  echo "[shutdown] 正在停止服务..."
  if [ -n "$BRIDGE_PID" ] && kill -0 "$BRIDGE_PID" 2>/dev/null; then
    kill "$BRIDGE_PID" 2>/dev/null || true
    wait "$BRIDGE_PID" 2>/dev/null || true
  fi
  if [ -n "$WEB_PID" ] && kill -0 "$WEB_PID" 2>/dev/null; then
    kill "$WEB_PID" 2>/dev/null || true
    wait "$WEB_PID" 2>/dev/null || true
  fi
  if [ -n "$BROKER_PID" ] && kill -0 "$BROKER_PID" 2>/dev/null; then
    kill "$BROKER_PID" 2>/dev/null || true
    wait "$BROKER_PID" 2>/dev/null || true
  fi
  exit "$exit_code"
}
trap cleanup INT TERM EXIT

free_startup_ports

echo "[start] 启动 LabGuard dashboard broker..."
mosquitto -c "$SCRIPT_DIR/mosquitto-labguard.conf" -v &
BROKER_PID=$!
sleep 1
if ! kill -0 "$BROKER_PID" 2>/dev/null; then
  echo "[error] broker 启动失败，请检查 1884/9001 端口是否被占用" >&2
  exit 1
fi

if [ "$ENABLE_SERIAL_BRIDGE" = "1" ]; then
  echo "[start] 启动本地串口桥..."
  LABGUARD_PORT="$LABGUARD_PORT" LABGUARD_WS_PORT="$SERIAL_WS_PORT" npm run bridge &
  BRIDGE_PID=$!
  sleep 1
  if ! kill -0 "$BRIDGE_PID" 2>/dev/null; then
    echo "[error] 本地串口桥启动失败，请检查 $LABGUARD_PORT 是否存在或被占用" >&2
    exit 1
  fi
else
  echo "[skip] 未启动本地串口桥（ENABLE_SERIAL_BRIDGE=0），不会占用 ${LABGUARD_PORT}"
fi

echo "[start] 启动 Dashboard 页面..."
npm run dev -- --port "$WEB_PORT" &
WEB_PID=$!
sleep 2
if ! kill -0 "$WEB_PID" 2>/dev/null; then
  echo "[error] Dashboard 页面启动失败" >&2
  exit 1
fi

echo
echo "========================================"
echo "LabGuard Dashboard 已启动"
echo "网页地址:  http://localhost:${WEB_PORT}"
echo "局域网访问: http://${HOST_IP}:${WEB_PORT}"
if [ "$ENABLE_SERIAL_BRIDGE" = "1" ]; then
  echo "串口桥:   ws://localhost:${SERIAL_WS_PORT} -> ${LABGUARD_PORT}"
else
  echo "串口桥:   未启动（ENABLE_SERIAL_BRIDGE=0）"
fi
echo "MQTT WS:   ws://${HOST_IP}:${MQTT_WS_PORT}"
echo "板子 MQTT: mqtt://${HOST_IP}:${MQTT_TCP_PORT}"
echo "========================================"
echo ""
if [ "$ENABLE_SERIAL_BRIDGE" = "1" ]; then
  echo "网页可使用本地串口桥，声音/灯光按钮可通过 ${LABGUARD_PORT} 发到板子。"
else
  echo "当前未启动本地串口桥，网页请使用 MQTT WebSocket。"
fi
echo "如果使用 MQTT WebSocket，请确认板子配置为："
echo "  mqtt://${HOST_IP}:${MQTT_TCP_PORT}"
echo ""
echo "按 Ctrl+C 可同时关闭 broker、串口桥和网页服务"
echo

wait "$BROKER_PID" "$BRIDGE_PID" "$WEB_PID"
