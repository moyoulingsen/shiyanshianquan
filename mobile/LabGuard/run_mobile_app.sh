#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

EXPO_PORT="${EXPO_PORT:-8081}"
HOST_IP="${HOST_IP:-$(hostname -I | awk '{print $1}') }"
HOST_IP="${HOST_IP// /}"
MQTT_WS_PORT="${MQTT_WS_PORT:-9001}"
CLEAR_CACHE="${CLEAR_CACHE:-1}"

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

echo
npm exec -- expo "${EXPO_ARGS[@]}"
