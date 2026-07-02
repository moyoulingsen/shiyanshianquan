import mqtt from 'mqtt/dist/mqtt.esm'

const topics = [
  'labguard/device/sensor',
  'labguard/device/risk',
  'labguard/device/status',
  'labguard/device/camera',
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
  clearLog: document.querySelector('#clear-log'),
  fanToggle: document.querySelector('#fan-toggle'),
  pumpToggle: document.querySelector('#pump-toggle'),
  audioToggle: document.querySelector('#audio-toggle'),
  lightToggle: document.querySelector('#light-toggle'),
  masterToggle: document.querySelector('#master-toggle'),
  fanSliderPanel: document.querySelector('#fan-slider-panel'),
  pumpSliderPanel: document.querySelector('#pump-slider-panel'),
  fanSlider: document.querySelector('#fan-slider'),
  pumpSlider: document.querySelector('#pump-slider'),
  fanSliderValue: document.querySelector('#fan-slider-value'),
  pumpSliderValue: document.querySelector('#pump-slider-value')
}

const actuatorState = {
  fan: { on: false, level: 100, source: 'off' },
  pump: { on: false, level: 100, source: 'off' }
}
const actuatorMinLevel = {
  fan: 60,
  pump: 55
}
const actuatorCommandPendingUntil = {
  fan: 0,
  pump: 0
}
const actuatorManualOverride = {
  fan: false,
  pump: false
}
const actuatorPendingHoldMs = 3000

let audioToggleOn = false
let lightToggleOn = false
let masterToggleOn = false
let masterMq2ForcedAlarm = false
let socket = null
let mqttClient = null
let connectedSource = null
let transportConnectedAt = 0
let deviceLastSeenAt = 0
let deviceSeen = false
let cameraLastFrameAt = 0
let cameraFramePending = false
let cameraQueuedPayload = null
const riskLevels = ['normal', 'warning', 'alarm', 'emergency']
const deviceFirstSeenTimeoutMs = 5000
const deviceStaleTimeoutMs = 7000
const defaultMqttUrl = `ws://${window.location.hostname || 'localhost'}:9001`
const savedSource = localStorage.getItem('labguard.dashboard.source')
const savedWsUrl = localStorage.getItem('labguard.dashboard.wsUrl')
const savedMqttUrl = localStorage.getItem('labguard.dashboard.mqttUrl')

function setConnection(state, text) {
  els.dot.className = `dot ${state}`
  els.connectionText.textContent = text
}

function transportLabel() {
  return connectedSource === 'mqtt' ? 'MQTT broker' : '串口桥'
}

function dataLabel() {
  return connectedSource === 'mqtt' ? 'MQTT 数据' : '串口数据'
}

function resetDevicePresence() {
  transportConnectedAt = 0
  deviceLastSeenAt = 0
  deviceSeen = false
}

function markTransportConnected(text) {
  transportConnectedAt = Date.now()
  deviceLastSeenAt = 0
  deviceSeen = false
  setConnection('pending', text)
}

function isDeviceMessage(topic, payload) {
  if (topic?.startsWith('labguard/device/')) return true
  if (topic === 'labguard/event') {
    return payload?.node === 'device' || payload?.source === 'device'
  }

  return ['sensor', 'risk', 'risk_state', 'status', 'camera_frame', 'event'].includes(payload?.type)
}

function markDeviceSeen() {
  deviceLastSeenAt = Date.now()
  deviceSeen = true
  setConnection('ok', `设备在线（${dataLabel()}）`)
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

function normalizeRiskLevel(level) {
  if (typeof level === 'string') {
    const textLevel = level.toLowerCase()
    const textIndex = riskLevels.indexOf(textLevel)
    if (textIndex >= 0) {
      return { index: textIndex, label: riskLevels[textIndex] }
    }
  }

  const numericIndex = Number(level)
  if (Number.isInteger(numericIndex) && riskLevels[numericIndex]) {
    return { index: numericIndex, label: riskLevels[numericIndex] }
  }

  return { index: 0, label: 'normal' }
}

function riskDisplayText(level, riskText) {
  const labels = ['正常', '高温预警', '有毒气体事件', '火灾事件']
  if (riskText === 'toxic_gas_event' || riskText === 'smoke_and_gas_alarm') return '有毒气体事件'
  if (riskText === 'fire_event' || riskText === 'flame_confirmed') return '火灾事件'
  return labels[normalizeRiskLevel(level).index] ?? riskText ?? '--'
}

function boolLabel(value) {
  return value ? '开启' : '关闭'
}

function hasOwn(object, key) {
  return Object.prototype.hasOwnProperty.call(object, key)
}

function actuatorRiskText(finalOn, autoOn, manualOn, manualOverride) {
  return `最终${boolLabel(finalOn)} / 自动${boolLabel(autoOn)} / 手动${manualOverride ? boolLabel(manualOn) : '未接管'}`
}

function updateDemoToggle(button, label, isOn) {
  if (!button) return
  button.textContent = `${label}：${isOn ? '开启' : '关闭'}`
  button.classList.toggle('is-on', isOn)
  button.classList.toggle('is-off', !isOn)
}

function setAudioToggleState(isOn) {
  audioToggleOn = Boolean(isOn)
  updateDemoToggle(els.audioToggle, '声音', audioToggleOn)
}

function setLightToggleState(isOn) {
  lightToggleOn = Boolean(isOn)
  updateDemoToggle(els.lightToggle, '灯带', lightToggleOn)
}

function setMasterToggleState(isOn) {
  masterToggleOn = Boolean(isOn)
  updateDemoToggle(els.masterToggle, '总起', masterToggleOn)
}

function resetLocalControlState() {
  actuatorManualOverride.fan = false
  actuatorManualOverride.pump = false
  clearActuatorCommandPending('fan')
  clearActuatorCommandPending('pump')
  setActuatorState('fan', false, 100, 'off')
  setActuatorState('pump', false, 100, 'off')
  setAudioToggleState(false)
  setLightToggleState(false)
  setMasterToggleState(false)
  masterMq2ForcedAlarm = false
}

function actuatorSourceText(source) {
  if (source === 'manual') return '（手动）'
  if (source === 'auto') return '（自动）'
  if (source === 'mixed') return '（手动+自动）'
  return ''
}

function updateActuatorButton(actuator) {
  const button = actuator === 'fan' ? els.fanToggle : els.pumpToggle
  const panel = actuator === 'fan' ? els.fanSliderPanel : els.pumpSliderPanel
  const slider = actuator === 'fan' ? els.fanSlider : els.pumpSlider
  const value = actuator === 'fan' ? els.fanSliderValue : els.pumpSliderValue
  if (!button || !panel || !slider || !value) return

  const label = actuator === 'fan' ? '风扇' : '水泵'
  const { on, level, source } = actuatorState[actuator]
  button.textContent = `${label}：${on ? '开启' : '关闭'}${on ? actuatorSourceText(source) : ''}`
  button.classList.toggle('is-on', on)
  button.classList.toggle('is-off', !on)
  panel.classList.toggle('hidden', !on)
  slider.value = String(level)
  value.textContent = `${level}%`
}

function setActuatorState(actuator, isOn, levelPct = actuatorState[actuator].level, source = actuatorState[actuator].source) {
  actuatorState[actuator] = {
    on: Boolean(isOn),
    level: Math.max(actuatorMinLevel[actuator], Math.min(100, Number(levelPct) || actuatorMinLevel[actuator])),
    source: isOn ? source : 'off'
  }
  updateActuatorButton(actuator)
}

function markActuatorCommandPending(actuator) {
  actuatorCommandPendingUntil[actuator] = Date.now() + actuatorPendingHoldMs
}

function commandPending(actuator) {
  return Date.now() < actuatorCommandPendingUntil[actuator]
}

function clearActuatorCommandPending(actuator) {
  actuatorCommandPendingUntil[actuator] = 0
}

function applyActuatorStateFromRisk(actuator, nextOn, nextLevel, nextSource, manualOverride, hasManualOverrideField) {
  if (hasManualOverrideField) {
    actuatorManualOverride[actuator] = manualOverride
    clearActuatorCommandPending(actuator)
  }

  if (commandPending(actuator) && !hasManualOverrideField) {
    return
  }

  if (actuatorManualOverride[actuator] && !manualOverride && !hasManualOverrideField) {
    return
  }

  setActuatorState(actuator, nextOn, nextLevel, nextSource)
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
  const timeEl = document.createElement('time')
  const topicEl = document.createElement('span')
  const payloadEl = document.createElement('code')

  timeEl.textContent = time
  topicEl.textContent = topic
  try {
    payloadEl.textContent = typeof payload === 'string' ? payload : JSON.stringify(payload)
  } catch {
    payloadEl.textContent = String(payload)
  }

  item.append(timeEl, topicEl, payloadEl)
  els.log.prepend(item)
  while (els.log.children.length > 80) {
    els.log.lastElementChild?.remove()
  }
}

function handleMessage(topic, payload) {
  if (isDeviceMessage(topic, payload)) {
    markDeviceSeen()
  }
  updateLastSeen()
  addLog(topic, payload)

  if (payload.type === 'sensor' || topic === 'labguard/device/sensor') {
    els.temperature.textContent = formatNumber(payload.temperature_c)
    els.humidity.textContent = formatNumber(payload.humidity_rh)
    els.voc.textContent = Number.isFinite(Number(payload.voc_index)) ? String(payload.voc_index) : '--'
    const mq2RawMv = Number(payload.mq2_raw_mv)
    const mq2RawAdc = Number(payload.mq2_raw_adc)
    const hasMq2Mv = Number.isFinite(mq2RawMv) && mq2RawMv > 0
    const hasMq2Adc = Number.isFinite(mq2RawAdc) && mq2RawAdc > 0
    const mq2Alarm = masterMq2ForcedAlarm || Boolean(payload.mq2_alarm)
    if (hasMq2Mv) {
      els.mq2.textContent = `${mq2RawMv} mV${mq2Alarm ? '（报警）' : ''}`
    } else if (hasMq2Adc) {
      els.mq2.textContent = `${mq2RawAdc} ADC${mq2Alarm ? '（报警）' : ''}`
    } else {
      els.mq2.textContent = mq2Alarm ? '报警' : '正常'
    }
    els.mq2.className = mq2Alarm ? 'danger-text' : ''
    els.sensorOk.textContent = payload.sensor_ok ? '传感器正常' : '传感器异常'
    els.sensorOk.className = payload.sensor_ok ? 'badge ok' : 'badge warn'
  }

  if (payload.type === 'risk' || payload.type === 'risk_state' || topic === 'labguard/device/risk') {
    const { label } = normalizeRiskLevel(payload.risk_level)
    els.riskBadge.textContent = riskDisplayText(payload.risk_level, payload.risk_text)
    els.riskBadge.className = `badge risk-${label}`
    els.riskText.textContent = riskDisplayText(payload.risk_level, payload.risk_text)
    const fanOn = Boolean(payload.action_fan ?? (Array.isArray(payload.actions) && payload.actions.includes('fan_on')))
    const pumpOn = Boolean(payload.action_pump ?? (Array.isArray(payload.actions) && payload.actions.includes('pump_on')))
    const alarmOn = Boolean(payload.action_alarm ?? (Array.isArray(payload.actions) && payload.actions.includes('alarm_on')))
    const hasManualFanOverride = hasOwn(payload, 'manual_fan_override')
    const hasManualPumpOverride = hasOwn(payload, 'manual_pump_override')
    const hasManualFan = hasOwn(payload, 'manual_fan')
    const hasManualPump = hasOwn(payload, 'manual_pump')
    const manualFanOverride = hasManualFanOverride ? Boolean(payload.manual_fan_override) : hasManualFan
    const manualPumpOverride = hasManualPumpOverride ? Boolean(payload.manual_pump_override) : hasManualPump
    const manualFan = hasManualFan ? Boolean(payload.manual_fan) : actuatorState.fan.on
    const manualPump = hasManualPump ? Boolean(payload.manual_pump) : actuatorState.pump.on
    const autoFan = Boolean(payload.auto_fan ?? fanOn)
    const autoPump = Boolean(payload.auto_pump ?? pumpOn)
    const fanLevel = Number.isFinite(Number(payload.fan_level_pct)) ? Number(payload.fan_level_pct) : (fanOn ? 100 : 0)
    const pumpLevel = Number.isFinite(Number(payload.pump_level_pct)) ? Number(payload.pump_level_pct) : (pumpOn ? 100 : 0)
    const manualFanLevel = Number.isFinite(Number(payload.manual_fan_level_pct))
      ? Number(payload.manual_fan_level_pct)
      : actuatorState.fan.level
    const manualPumpLevel = Number.isFinite(Number(payload.manual_pump_level_pct))
      ? Number(payload.manual_pump_level_pct)
      : actuatorState.pump.level
    const fanButtonOn = manualFanOverride ? manualFan : fanOn
    const pumpButtonOn = manualPumpOverride ? manualPump : pumpOn
    const fanButtonLevel = manualFanOverride ? manualFanLevel : fanLevel
    const pumpButtonLevel = manualPumpOverride ? manualPumpLevel : pumpLevel
    const fanButtonSource = manualFanOverride ? (manualFan ? 'manual' : 'off') : (fanOn ? 'auto' : 'off')
    const pumpButtonSource = manualPumpOverride ? (manualPump ? 'manual' : 'off') : (pumpOn ? 'auto' : 'off')
    els.fan.textContent = actuatorRiskText(fanOn, autoFan, manualFan, manualFanOverride)
    els.pump.textContent = actuatorRiskText(pumpOn, autoPump, manualPump, manualPumpOverride)
    els.alarm.textContent = boolLabel(alarmOn)
    applyActuatorStateFromRisk('fan', fanButtonOn, fanButtonLevel, fanButtonSource, manualFanOverride, hasManualFanOverride)
    applyActuatorStateFromRisk('pump', pumpButtonOn, pumpButtonLevel, pumpButtonSource, manualPumpOverride, hasManualPumpOverride)
  }

  if (payload.type === 'status' || topic === 'labguard/device/status') {
    els.uptime.textContent = formatUptime(payload.uptime_s)
    els.rssi.textContent = Number.isFinite(Number(payload.wifi_rssi)) ? `${payload.wifi_rssi} dBm` : '--'
    els.version.textContent = payload.version ?? '--'
    if (hasOwn(payload, 'audio_looping')) {
      setAudioToggleState(Boolean(payload.audio_looping))
    }
    if (hasOwn(payload, 'light_on')) {
      setLightToggleState(Boolean(payload.light_on))
    }
  }

  if (payload.type === 'camera_frame' || topic === 'labguard/device/camera') {
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
  resetDevicePresence()
}

function syncSourceFields() {
  const useMqtt = els.source.value === 'mqtt'
  els.wsField.classList.toggle('hidden', useMqtt)
  els.mqttField.classList.toggle('hidden', !useMqtt)
}

function connectSelectedSource() {
  if (els.source.value === 'mqtt') {
    connectMqtt()
  } else {
    connectSerialBridge()
  }
}

function connectSerialBridge() {
  disconnect()
  connectedSource = 'ws'
  localStorage.setItem('labguard.dashboard.source', 'ws')
  localStorage.setItem('labguard.dashboard.wsUrl', els.wsUrl.value.trim())
  setConnection('pending', '连接本地串口桥...')
  socket = new WebSocket(els.wsUrl.value.trim())

  socket.addEventListener('open', () => markTransportConnected('串口桥已连接，等待串口状态...'))
  socket.addEventListener('close', () => {
    if (connectedSource === 'ws') setConnection('warn', '本地串口桥已断开')
  })
  socket.addEventListener('error', () => {
    if (connectedSource !== 'ws') return
    setConnection('warn', '本地串口桥连接失败')
  })
  socket.addEventListener('message', (event) => {
    let frame
    try {
      frame = JSON.parse(event.data)
    } catch {
      return
    }
    if (frame.type === 'bridge_status') {
      if (connectedSource !== 'ws') return
      if (frame.serial_open) {
        markTransportConnected(`串口已打开 ${frame.port ?? ''}，等待设备数据...`)
      } else {
        setConnection('warn', frame.message ?? `串口未打开 ${frame.port ?? ''}`)
      }
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
    markTransportConnected('MQTT broker 已连接，等待设备数据...')
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

function sendCommand(command, extra = {}) {
  const commandPayload = {
    node: 'dashboard',
    type: 'command',
    command,
    target_node: 'device',
    ...extra,
    timestamp: Math.floor(Date.now() / 1000)
  }
  const payload = JSON.stringify(commandPayload)

  if (mqttClient?.connected) {
    mqttClient.publish('labguard/cmd/test', payload, { qos: 1 })
    addLog('labguard/cmd/test', commandPayload)
    return true
  }

  if (socket?.readyState === WebSocket.OPEN) {
    socket.send(JSON.stringify({
      topic: 'labguard/cmd/test',
      payload: commandPayload
    }))
    addLog('labguard/cmd/test', commandPayload)
    return true
  }

  return false
}

function handleCommandToggle(currentState, setState, commandName) {
  const nextState = !currentState
  setState(nextState)

  if (!sendCommand(`${commandName}_${nextState ? 'on' : 'off'}`)) {
    setState(!nextState)
    setConnection('warn', '命令未发送：请先连接 MQTT 或串口桥')
  }
}

function toggleActuator(actuator) {
  const previousState = { ...actuatorState[actuator] }
  const nextState = !previousState.on
  const level = previousState.level
  const command = `${actuator}_${nextState ? 'on' : 'off'}`
  actuatorManualOverride[actuator] = true
  markActuatorCommandPending(actuator)
  setActuatorState(actuator, nextState, level, nextState ? 'manual' : 'off')

  if (!sendCommand(command, nextState ? { level_pct: level } : {})) {
    clearActuatorCommandPending(actuator)
    actuatorManualOverride[actuator] = previousState.source === 'manual'
    setActuatorState(actuator, previousState.on, previousState.level, previousState.source)
  }
}

function updateActuatorLevel(actuator, levelPct) {
  const level = Math.max(actuatorMinLevel[actuator], Math.min(100, Number(levelPct) || actuatorMinLevel[actuator]))
  const { on } = actuatorState[actuator]
  setActuatorState(actuator, on, level, on ? 'manual' : actuatorState[actuator].source)

  if (on) {
    actuatorManualOverride[actuator] = true
    markActuatorCommandPending(actuator)
    sendCommand(`${actuator}_on`, { level_pct: level })
  }
}

function stepActuatorLevel(actuator, delta) {
  const current = actuatorState[actuator].level
  updateActuatorLevel(actuator, current + Number(delta))
}

function toggleAudio() {
  handleCommandToggle(audioToggleOn, setAudioToggleState, 'audio')
}

function toggleLight() {
  handleCommandToggle(lightToggleOn, setLightToggleState, 'light')
}

function toggleMasterControl() {
  const nextState = !masterToggleOn
  setMasterToggleState(nextState)

  if (nextState) {
    actuatorManualOverride.fan = true
    actuatorManualOverride.pump = true
    markActuatorCommandPending('fan')
    markActuatorCommandPending('pump')
    setActuatorState('fan', true, 100, 'manual')
    setActuatorState('pump', true, 100, 'manual')
    setLightToggleState(true)
    setAudioToggleState(true)
    masterMq2ForcedAlarm = true
    els.mq2.textContent = '报警'
    els.mq2.className = 'danger-text'
    if (!sendCommand('master_on')) {
      clearActuatorCommandPending('fan')
      clearActuatorCommandPending('pump')
      setMasterToggleState(false)
      setActuatorState('fan', false, 100, 'off')
      setActuatorState('pump', false, 100, 'off')
      setLightToggleState(false)
      setAudioToggleState(false)
      masterMq2ForcedAlarm = false
      els.mq2.textContent = '--'
      els.mq2.className = ''
      setConnection('warn', '命令未发送：请先连接 MQTT 或串口桥')
    }
    return
  }

  actuatorManualOverride.fan = false
  actuatorManualOverride.pump = false
  clearActuatorCommandPending('fan')
  clearActuatorCommandPending('pump')
  setActuatorState('fan', false, 100, 'off')
  setActuatorState('pump', false, 100, 'off')
  setLightToggleState(false)
  setAudioToggleState(false)
  masterMq2ForcedAlarm = false
  els.mq2.textContent = '正常'
  els.mq2.className = ''
  if (!sendCommand('master_off')) {
    actuatorManualOverride.fan = true
    actuatorManualOverride.pump = true
    markActuatorCommandPending('fan')
    markActuatorCommandPending('pump')
    setMasterToggleState(true)
    setActuatorState('fan', true, 100, 'manual')
    setActuatorState('pump', true, 100, 'manual')
    setLightToggleState(true)
    setAudioToggleState(true)
    masterMq2ForcedAlarm = true
    els.mq2.textContent = '报警'
    els.mq2.className = 'danger-text'
    setConnection('warn', '命令未发送：请先连接 MQTT 或串口桥')
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
  connectSelectedSource()
})

els.connect.addEventListener('click', () => {
  connectSelectedSource()
})

document.querySelectorAll('[data-command]').forEach((button) => {
  button.addEventListener('click', () => {
    const command = button.dataset.command
    if (sendCommand(command) && command === 'reset') {
      resetLocalControlState()
    }
  })
})

document.querySelectorAll('[data-toggle-actuator]').forEach((button) => {
  button.addEventListener('click', () => toggleActuator(button.dataset.toggleActuator))
})

document.querySelectorAll('[data-level-actuator]').forEach((slider) => {
  slider.addEventListener('input', () => updateActuatorLevel(slider.dataset.levelActuator, slider.value))
})

document.querySelectorAll('[data-step-actuator]').forEach((button) => {
  button.addEventListener('click', () => stepActuatorLevel(button.dataset.stepActuator, button.dataset.stepDelta))
})

els.audioToggle?.addEventListener('click', () => {
  toggleAudio()
})

els.lightToggle?.addEventListener('click', () => {
  toggleLight()
})

els.masterToggle?.addEventListener('click', () => {
  toggleMasterControl()
})

els.clearLog.addEventListener('click', () => {
  els.log.replaceChildren()
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

window.setInterval(() => {
  if (!connectedSource || !transportConnectedAt) return

  const now = Date.now()
  if (!deviceSeen && now - transportConnectedAt > deviceFirstSeenTimeoutMs) {
    setConnection('warn', `${transportLabel()} 已连接，但还没有设备数据`)
    return
  }

  if (deviceSeen && now - deviceLastSeenAt > deviceStaleTimeoutMs) {
    setConnection('warn', `设备数据超时（${transportLabel()} 仍连接）`)
  }
}, 1000)

els.source.value = 'mqtt'
els.wsUrl.value = savedWsUrl || 'ws://localhost:8787'
els.mqttUrl.value = savedMqttUrl || defaultMqttUrl
syncSourceFields()
setConnection('warn', '未连接')
updateCameraState('warn', '等待画面')
setActuatorState('fan', false, 100)
setActuatorState('pump', false, 100)
setAudioToggleState(false)
setLightToggleState(false)
setMasterToggleState(false)
connectSelectedSource()
