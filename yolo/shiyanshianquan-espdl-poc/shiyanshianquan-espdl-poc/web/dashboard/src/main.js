import mqtt from 'mqtt/dist/mqtt.esm'

const topics = [
  'labguard/indoor/sensor',
  'labguard/indoor/risk',
  'labguard/indoor/status',
  'labguard/indoor/camera',
  'labguard/event'
]

const els = {
  dot: document.querySelector('#connection-dot'),
  connectionText: document.querySelector('#connection-text'),
  source: document.querySelector('#source-select'),
  wsField: document.querySelector('#ws-field'),
  mqttField: document.querySelector('#mqtt-field'),
  wsUrl: document.querySelector('#ws-url'),
  mqttUrl: document.querySelector('#mqtt-url'),
  connect: document.querySelector('#connect-btn'),
  temperature: document.querySelector('#temperature'),
  humidity: document.querySelector('#humidity'),
  voc: document.querySelector('#voc'),
  mq2: document.querySelector('#mq2'),
  riskBadge: document.querySelector('#risk-badge'),
  riskText: document.querySelector('#risk-text'),
  fan: document.querySelector('#fan-state'),
  pump: document.querySelector('#pump-state'),
  alarm: document.querySelector('#alarm-state'),
  sensorOk: document.querySelector('#sensor-ok'),
  uptime: document.querySelector('#uptime'),
  rssi: document.querySelector('#rssi'),
  version: document.querySelector('#version'),
  lastUpdate: document.querySelector('#last-update'),
  cameraState: document.querySelector('#camera-state'),
  cameraPreview: document.querySelector('#camera-preview'),
  cameraEmpty: document.querySelector('#camera-empty'),
  cameraResolution: document.querySelector('#camera-resolution'),
  cameraSequence: document.querySelector('#camera-sequence'),
  cameraLastUpdate: document.querySelector('#camera-last-update'),
  log: document.querySelector('#message-log'),
  clearLog: document.querySelector('#clear-log')
}

let socket = null
let mqttClient = null
let connectedSource = null
let cameraLastFrameAt = 0
let cameraFramePending = false
let cameraQueuedPayload = null
const defaultMqttUrl = `ws://${window.location.hostname || 'localhost'}:9001`
const savedWsUrl = localStorage.getItem('labguard.dashboard.wsUrl')
const savedMqttUrl = localStorage.getItem('labguard.dashboard.mqttUrl')

function setConnection(state, text) {
  els.dot.className = `dot ${state}`
  els.connectionText.textContent = text
}

function formatNumber(value, digits = 1) {
  return Number.isFinite(Number(value)) ? Number(value).toFixed(digits) : '--'
}

function formatUptime(seconds) {
  const total = Number(seconds)
  if (!Number.isFinite(total)) return '--'
  const h = Math.floor(total / 3600)
  const m = Math.floor((total % 3600) / 60)
  const s = Math.floor(total % 60)
  if (h > 0) return `${h}h ${m}m ${s}s`
  if (m > 0) return `${m}m ${s}s`
  return `${s}s`
}

function riskLabel(level) {
  const labels = ['normal', 'warning', 'alarm', 'emergency']
  return labels[Number(level)] ?? '--'
}

function boolLabel(value) {
  return value ? '开启' : '关闭'
}

function updateLastSeen() {
  els.lastUpdate.textContent = new Date().toLocaleTimeString()
}

function updateCameraState(state, text) {
  els.cameraState.textContent = text
  els.cameraState.className = `badge ${state}`
}

function renderCamera(payload) {
  els.cameraPreview.src = `data:${payload.format};base64,${payload.image_base64}`
}

function flushQueuedCameraFrame() {
  if (!cameraQueuedPayload) {
    cameraFramePending = false
    return
  }

  const payload = cameraQueuedPayload
  cameraQueuedPayload = null
  renderCamera(payload)
}

function updateCamera(payload) {
  if (!payload?.image_base64 || !payload?.format) {
    return
  }

  els.cameraPreview.classList.add('ready')
  els.cameraEmpty.classList.add('hidden')
  els.cameraResolution.textContent = `${payload.width ?? '--'} × ${payload.height ?? '--'}`
  els.cameraSequence.textContent = Number.isFinite(Number(payload.sequence)) ? String(payload.sequence) : '--'
  els.cameraLastUpdate.textContent = new Date().toLocaleTimeString()
  cameraLastFrameAt = Date.now()
  updateCameraState('ok', '实时画面')

  if (cameraFramePending) {
    cameraQueuedPayload = payload
    return
  }

  cameraFramePending = true
  renderCamera(payload)
}

function addLog(topic, payload) {
  const item = document.createElement('li')
  const time = new Date().toLocaleTimeString()
  item.innerHTML = `<time>${time}</time><span>${topic}</span><code>${JSON.stringify(payload)}</code>`
  els.log.prepend(item)
  while (els.log.children.length > 80) {
    els.log.lastElementChild?.remove()
  }
}

function handleMessage(topic, payload) {
  updateLastSeen()
  addLog(topic, payload)

  if (payload.type === 'sensor' || topic === 'labguard/indoor/sensor') {
    els.temperature.textContent = formatNumber(payload.temperature_c)
    els.humidity.textContent = formatNumber(payload.humidity_rh)
    els.voc.textContent = Number.isFinite(Number(payload.voc_index)) ? String(payload.voc_index) : '--'
    els.mq2.textContent = payload.mq2_alarm ? '报警' : '正常'
    els.mq2.className = payload.mq2_alarm ? 'danger-text' : ''
    els.sensorOk.textContent = payload.sensor_ok ? '传感器正常' : '传感器异常'
    els.sensorOk.className = payload.sensor_ok ? 'badge ok' : 'badge warn'
  }

  if (payload.type === 'risk_state' || topic === 'labguard/indoor/risk') {
    const label = riskLabel(payload.risk_level)
    els.riskBadge.textContent = label
    els.riskBadge.className = `badge risk-${label}`
    els.riskText.textContent = payload.risk_text ?? label
    const actions = Array.isArray(payload.actions) ? payload.actions : []
    els.fan.textContent = boolLabel(actions.includes('fan_on'))
    els.pump.textContent = boolLabel(actions.includes('pump_on'))
    els.alarm.textContent = boolLabel(actions.includes('alarm_on'))
  }

  if (payload.type === 'status' || topic === 'labguard/indoor/status') {
    els.uptime.textContent = formatUptime(payload.uptime_s)
    els.rssi.textContent = Number.isFinite(Number(payload.wifi_rssi)) ? `${payload.wifi_rssi} dBm` : '--'
    els.version.textContent = payload.version ?? '--'
  }

  if (payload.type === 'camera_frame' || topic === 'labguard/indoor/camera') {
    updateCamera(payload)
  }
}

function parseSerialLine(line) {
  const match = line.match(/local publish topic=([^ ]+).*payload=(\{.*\})/)
  if (!match) return null
  try {
    return {
      topic: match[1],
      payload: JSON.parse(match[2])
    }
  } catch {
    return null
  }
}

function disconnect() {
  if (socket) {
    socket.close()
    socket = null
  }
  if (mqttClient) {
    mqttClient.end(true)
    mqttClient = null
  }
  connectedSource = null
}

function syncSourceFields() {
  const useMqtt = els.source.value === 'mqtt'
  els.wsField.classList.toggle('hidden', useMqtt)
  els.mqttField.classList.toggle('hidden', !useMqtt)
}

function connectSerialBridge() {
  disconnect()
  connectedSource = 'ws'
  localStorage.setItem('labguard.dashboard.source', 'ws')
  localStorage.setItem('labguard.dashboard.wsUrl', els.wsUrl.value.trim())
  setConnection('pending', '连接本地串口桥...')
  socket = new WebSocket(els.wsUrl.value.trim())

  socket.addEventListener('open', () => setConnection('ok', '本地串口桥已连接'))
  socket.addEventListener('close', () => {
    if (connectedSource === 'ws') setConnection('warn', '本地串口桥已断开')
  })
  socket.addEventListener('error', () => setConnection('warn', '本地串口桥连接失败'))
  socket.addEventListener('message', (event) => {
    let frame
    try {
      frame = JSON.parse(event.data)
    } catch {
      return
    }
    if (frame.topic && frame.payload) {
      handleMessage(frame.topic, frame.payload)
      return
    }
    if (frame.line) {
      const parsed = parseSerialLine(frame.line)
      if (parsed) handleMessage(parsed.topic, parsed.payload)
    }
  })
}

function connectMqtt() {
  disconnect()
  connectedSource = 'mqtt'
  localStorage.setItem('labguard.dashboard.source', 'mqtt')
  localStorage.setItem('labguard.dashboard.mqttUrl', els.mqttUrl.value.trim())
  setConnection('pending', '连接 MQTT...')
  mqttClient = mqtt.connect(els.mqttUrl.value.trim(), {
    clientId: `labguard_dashboard_${Math.random().toString(16).slice(2)}`,
    reconnectPeriod: 2000,
    clean: true
  })

  mqttClient.on('connect', () => {
    setConnection('ok', 'MQTT 已连接')
    topics.forEach((topic) => mqttClient.subscribe(topic, { qos: 1 }))
  })
  mqttClient.on('reconnect', () => setConnection('pending', 'MQTT 重连中...'))
  mqttClient.on('close', () => {
    if (connectedSource === 'mqtt') setConnection('warn', 'MQTT 已断开')
  })
  mqttClient.on('error', () => setConnection('warn', 'MQTT 连接错误'))
  mqttClient.on('message', (topic, message) => {
    try {
      handleMessage(topic, JSON.parse(message.toString()))
    } catch {
      addLog(topic, message.toString())
    }
  })
}

function sendCommand(command) {
  const payload = JSON.stringify({
    node: 'dashboard',
    type: 'command',
    command,
    target_node: 'indoor',
    timestamp: Math.floor(Date.now() / 1000)
  })

  if (mqttClient?.connected) {
    mqttClient.publish('labguard/cmd/test', payload, { qos: 1 })
    return
  }

  if (socket?.readyState === WebSocket.OPEN) {
    socket.send(JSON.stringify({
      topic: 'labguard/cmd/test',
      payload: JSON.parse(payload)
    }))
  }
}

els.cameraPreview.addEventListener('load', flushQueuedCameraFrame)
els.cameraPreview.addEventListener('error', () => {
  cameraFramePending = false
  cameraQueuedPayload = null
  updateCameraState('warn', '画面加载失败')
})

els.source.addEventListener('change', () => {
  syncSourceFields()
})

els.connect.addEventListener('click', () => {
  if (els.source.value === 'mqtt') {
    connectMqtt()
  } else {
    connectSerialBridge()
  }
})

document.querySelectorAll('[data-command]').forEach((button) => {
  button.addEventListener('click', () => sendCommand(button.dataset.command))
})

els.clearLog.addEventListener('click', () => {
  els.log.innerHTML = ''
})

window.setInterval(() => {
  if (!cameraLastFrameAt) {
    updateCameraState('warn', '等待画面')
    return
  }
  if (Date.now() - cameraLastFrameAt > 5000) {
    updateCameraState('warn', '画面过期')
  }
}, 1000)

els.source.value = 'mqtt'
els.wsUrl.value = savedWsUrl || 'ws://localhost:8787'
els.mqttUrl.value = savedMqttUrl || defaultMqttUrl
syncSourceFields()
setConnection('warn', '未连接')
updateCameraState('warn', '等待画面')
connectMqtt()
