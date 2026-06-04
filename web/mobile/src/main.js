import mqtt from 'mqtt/dist/mqtt.esm'
import {
  Activity,
  AlertTriangle,
  Bell,
  CheckCircle2,
  Droplets,
  Fan,
  Flame,
  Gauge,
  PlugZap,
  RadioTower,
  RotateCcw,
  ShieldCheck,
  Siren,
  SlidersHorizontal,
  Smartphone,
  Thermometer,
  Trash2,
  Waves,
  createIcons
} from 'lucide'

createIcons({
  icons: {
    Activity,
    AlertTriangle,
    Bell,
    CheckCircle2,
    Droplets,
    Fan,
    Flame,
    Gauge,
    PlugZap,
    RadioTower,
    RotateCcw,
    ShieldCheck,
    Siren,
    SlidersHorizontal,
    Smartphone,
    Thermometer,
    Trash2,
    Waves
  }
})

const topics = [
  'labguard/indoor/sensor',
  'labguard/indoor/risk',
  'labguard/indoor/status',
  'labguard/event'
]

const riskMeta = {
  normal: { title: '正常', icon: 'shield-check' },
  warning: { title: '预警', icon: 'alert-triangle' },
  alarm: { title: '有毒气体', icon: 'siren' },
  emergency: { title: '火灾', icon: 'flame' }
}

const clientId = `labguard_mobile_${Math.random().toString(16).slice(2)}`
const savedBroker = localStorage.getItem('labguard.mobile.broker')
const defaultBroker = `ws://${window.location.hostname || 'localhost'}:9001`

const els = {
  brokerUrl: document.querySelector('#broker-url'),
  connectToggle: document.querySelector('#connect-toggle'),
  dot: document.querySelector('#connection-dot'),
  connectionText: document.querySelector('#connection-text'),
  lastUpdate: document.querySelector('#last-update'),
  riskCard: document.querySelector('#risk-card'),
  riskLevel: document.querySelector('#risk-level'),
  riskText: document.querySelector('#risk-text'),
  riskIcon: document.querySelector('.risk-icon'),
  temperature: document.querySelector('#temperature'),
  humidity: document.querySelector('#humidity'),
  voc: document.querySelector('#voc'),
  mq2: document.querySelector('#mq2'),
  sensorOk: document.querySelector('#sensor-ok'),
  fan: document.querySelector('#fan-state'),
  pump: document.querySelector('#pump-state'),
  alarm: document.querySelector('#alarm-state'),
  uptime: document.querySelector('#uptime'),
  rssi: document.querySelector('#rssi'),
  version: document.querySelector('#version'),
  commandState: document.querySelector('#command-state'),
  clientId: document.querySelector('#client-id'),
  staleState: document.querySelector('#stale-state'),
  displayMode: document.querySelector('#display-mode'),
  log: document.querySelector('#message-log'),
  clearLog: document.querySelector('#clear-log')
}

let client = null
let connected = false
let lastMessageAt = 0
let lastRiskLevel = 0

els.brokerUrl.value = savedBroker || defaultBroker
els.clientId.textContent = clientId.replace('labguard_mobile_', '')
els.displayMode.textContent = window.matchMedia('(display-mode: standalone)').matches ? '桌面 App' : '浏览器'

function setConnection(state, text) {
  els.dot.className = `dot ${state}`
  els.connectionText.textContent = text
  els.connectToggle.querySelector('span').textContent = connected ? '断开' : '连接'
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
  if (h > 0) return `${h}h ${m}m`
  if (m > 0) return `${m}m ${s}s`
  return `${s}s`
}

function riskLabel(level) {
  const labels = ['normal', 'warning', 'alarm', 'emergency']
  return labels[Number(level)] ?? 'normal'
}

function riskDisplayText(level, riskText) {
  const labels = ['正常', '高温预警', '有毒气体事件', '火灾事件']
  if (riskText === 'toxic_gas_event' || riskText === 'smoke_and_gas_alarm') return '有毒气体事件'
  if (riskText === 'fire_event' || riskText === 'flame_confirmed') return '火灾事件'
  return labels[Number(level)] ?? riskText ?? '正常'
}

function boolText(value) {
  return value ? '开启' : '关闭'
}

function timestamp() {
  return Math.floor(Date.now() / 1000)
}

function addLog(topic, payload) {
  const item = document.createElement('li')
  const time = new Date().toLocaleTimeString()
  const text = typeof payload === 'string' ? payload : JSON.stringify(payload)
  item.innerHTML = `<time>${time}</time><span>${topic}</span><code>${text}</code>`
  els.log.prepend(item)
  while (els.log.children.length > 60) {
    els.log.lastElementChild?.remove()
  }
}

function updateLastSeen() {
  lastMessageAt = Date.now()
  els.lastUpdate.textContent = new Date().toLocaleTimeString()
  els.staleState.textContent = '实时'
  els.staleState.className = 'state-ok'
}

function updateRisk(payload) {
  const label = riskLabel(payload.risk_level)
  const meta = riskMeta[label]
  els.riskCard.className = `risk-card risk-${label}`
  els.riskLevel.textContent = meta.title
  els.riskText.textContent = riskDisplayText(payload.risk_level, payload.risk_text)
  els.riskIcon.innerHTML = `<i data-lucide="${meta.icon}"></i>`

  const actions = Array.isArray(payload.actions) ? payload.actions : []
  els.fan.textContent = boolText(actions.includes('fan_on'))
  els.pump.textContent = boolText(actions.includes('pump_on'))
  els.alarm.textContent = boolText(actions.includes('alarm_on'))

  const numericLevel = Number(payload.risk_level)
  if (Number.isFinite(numericLevel) && numericLevel >= 2 && numericLevel !== lastRiskLevel) {
    navigator.vibrate?.(numericLevel >= 3 ? [180, 80, 180] : 120)
  }
  lastRiskLevel = Number.isFinite(numericLevel) ? numericLevel : lastRiskLevel
  createIcons({ icons: { ShieldCheck, AlertTriangle, Siren, Flame } })
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
    updateRisk(payload)
  }

  if (payload.type === 'status' || topic === 'labguard/indoor/status') {
    els.uptime.textContent = formatUptime(payload.uptime_s)
    els.rssi.textContent = Number.isFinite(Number(payload.wifi_rssi)) ? `${payload.wifi_rssi} dBm` : '--'
    els.version.textContent = payload.version ?? '--'
  }
}

function publishJson(topic, payload, options = { qos: 1 }) {
  if (!client?.connected) {
    return false
  }
  client.publish(topic, JSON.stringify(payload), options)
  return true
}

function publishMobileEvent(event, actions = '') {
  publishJson('labguard/event', {
    node: 'mobile',
    type: 'event',
    risk_level: lastRiskLevel,
    risk_text: riskLabel(lastRiskLevel),
    source: 'mobile_app',
    event,
    actions,
    timestamp: timestamp()
  })
}

function connect() {
  const url = els.brokerUrl.value.trim()
  if (!url) {
    setConnection('warn', 'Broker 地址为空')
    return
  }

  localStorage.setItem('labguard.mobile.broker', url)
  setConnection('pending', '连接 MQTT...')

  client = mqtt.connect(url, {
    clientId,
    clean: true,
    reconnectPeriod: 2000,
    connectTimeout: 5000
  })

  client.on('connect', () => {
    connected = true
    setConnection('ok', 'MQTT 已连接')
    els.commandState.textContent = '可发送'
    topics.forEach((topic) => client.subscribe(topic, { qos: 1 }))
    publishMobileEvent('mobile_app_connected', 'online')
  })

  client.on('reconnect', () => {
    connected = false
    setConnection('pending', 'MQTT 重连中...')
    els.commandState.textContent = '重连中'
  })

  client.on('close', () => {
    connected = false
    setConnection('warn', 'MQTT 已断开')
    els.commandState.textContent = '等待连接'
  })

  client.on('error', () => {
    connected = false
    setConnection('warn', 'MQTT 连接错误')
    els.commandState.textContent = '连接错误'
  })

  client.on('message', (topic, message) => {
    try {
      handleMessage(topic, JSON.parse(message.toString()))
    } catch {
      addLog(topic, message.toString())
    }
  })
}

function disconnect() {
  if (client) {
    publishMobileEvent('mobile_app_disconnected', 'offline')
    client.end(true)
    client = null
  }
  connected = false
  setConnection('warn', '未连接')
  els.commandState.textContent = '等待连接'
}

function sendCommand(command) {
  const payload = {
    node: 'mobile',
    type: 'command',
    command,
    target_node: 'indoor',
    timestamp: timestamp()
  }

  if (publishJson('labguard/cmd/test', payload)) {
    els.commandState.textContent = `已发送 ${command}`
    publishMobileEvent('mobile_command', command)
  } else {
    els.commandState.textContent = '未连接'
  }
}

document.querySelectorAll('.tab').forEach((tab) => {
  tab.addEventListener('click', () => {
    document.querySelectorAll('.tab').forEach((item) => item.classList.remove('active'))
    document.querySelectorAll('.tab-panel').forEach((panel) => panel.classList.remove('active'))
    tab.classList.add('active')
    document.querySelector(`#panel-${tab.dataset.tab}`).classList.add('active')
  })
})

document.querySelectorAll('[data-command]').forEach((button) => {
  button.addEventListener('click', () => sendCommand(button.dataset.command))
})

els.connectToggle.addEventListener('click', () => {
  if (connected || client) {
    disconnect()
  } else {
    connect()
  }
})

els.clearLog.addEventListener('click', () => {
  els.log.innerHTML = ''
})

setInterval(() => {
  if (!lastMessageAt) {
    els.staleState.textContent = '--'
    return
  }

  const age = Math.floor((Date.now() - lastMessageAt) / 1000)
  if (age > 8) {
    els.staleState.textContent = `${age}s 未更新`
    els.staleState.className = 'state-warn'
  }
}, 1000)

if ('serviceWorker' in navigator) {
  navigator.serviceWorker.register('/sw.js').catch(() => {})
}

setConnection('warn', '未连接')
connect()
