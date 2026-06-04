import 'react-native-url-polyfill/auto'

import { Buffer } from 'buffer'
import process from 'process'

import { useLabguardStore } from '../state/labguardStore'
import type { ActuatorName, RiskKey } from '../types'
import { nowTimeText, timestamp } from '../utils/format'
import { vibrateForRisk } from '../utils/vibration'
import { SUBSCRIBE_TOPICS, TOPICS } from './topics'

const runtime = globalThis as typeof globalThis & {
  Buffer?: typeof Buffer
  process?: typeof process
}

runtime.Buffer = runtime.Buffer || Buffer
runtime.process = runtime.process || process

type MqttModule = typeof import('mqtt')
type MqttClient = import('mqtt').MqttClient

const clientId = `labguard_mobile_${Math.random().toString(16).slice(2)}`
let client: MqttClient | null = null
let mqttModule: MqttModule | null = null
let lastRiskLevel = 0

async function loadMqtt() {
  if (!mqttModule) {
    mqttModule = await import('mqtt')
  }

  return mqttModule
}

function riskLabel(level: unknown): RiskKey {
  const labels: RiskKey[] = ['normal', 'warning', 'alarm', 'emergency']
  return labels[Number(level)] ?? 'normal'
}

function boolLabel(value: unknown) {
  return Boolean(value)
}

function addLog(topic: string, payload: unknown) {
  useLabguardStore.getState().addLog({
    id: `${Date.now()}_${Math.random().toString(16).slice(2)}`,
    time: nowTimeText(),
    topic,
    payload
  })
}

function publishJson(topic: string, payload: unknown, qos: 0 | 1 = 1) {
  if (!client?.connected) {
    return false
  }

  client.publish(topic, JSON.stringify(payload), { qos })
  return true
}

function publishMobileEvent(event: string, actions = '') {
  const state = useLabguardStore.getState()
  publishJson(TOPICS.event, {
    node: 'mobile',
    type: 'event',
    risk_level: state.risk.level,
    risk_text: state.risk.label,
    source: 'mobile_app',
    event,
    actions,
    timestamp: timestamp()
  })
}

function updateRisk(payload: Record<string, unknown>) {
  const store = useLabguardStore.getState()
  const numericLevel = Number(payload.risk_level)
  const safeLevel = Number.isFinite(numericLevel) ? numericLevel : 0
  const label = riskLabel(safeLevel)
  const actions = Array.isArray(payload.actions) ? payload.actions : []
  const fanOn = boolLabel(payload.action_fan ?? actions.includes('fan_on'))
  const pumpOn = boolLabel(payload.action_pump ?? actions.includes('pump_on'))
  const alarmOn = boolLabel(payload.action_alarm ?? actions.includes('alarm_on'))
  const fanLevel = Number.isFinite(Number(payload.fan_level_pct)) ? Number(payload.fan_level_pct) : fanOn ? 100 : 0
  const pumpLevel = Number.isFinite(Number(payload.pump_level_pct)) ? Number(payload.pump_level_pct) : pumpOn ? 100 : 0

  store.setRisk({
    level: safeLevel,
    label,
    text: String(payload.risk_text ?? label)
  })
  store.setActuator('fan', { on: fanOn, level: fanLevel })
  store.setActuator('pump', { on: pumpOn, level: pumpLevel })
  store.setAlarmOn(alarmOn)

  if (safeLevel >= 2 && safeLevel !== lastRiskLevel) {
    vibrateForRisk(safeLevel)
  }
  lastRiskLevel = safeLevel
}

function updateCamera(payload: Record<string, unknown>) {
  const image = payload.image_base64
  const format = payload.format
  if (typeof image !== 'string' || typeof format !== 'string') {
    return
  }

  useLabguardStore.getState().setCamera({
    uri: `data:${format};base64,${image}`,
    format,
    width: Number.isFinite(Number(payload.width)) ? Number(payload.width) : undefined,
    height: Number.isFinite(Number(payload.height)) ? Number(payload.height) : undefined,
    sequence: Number.isFinite(Number(payload.sequence)) ? Number(payload.sequence) : undefined,
    receivedAt: Date.now()
  })
}

export function handleMessage(topic: string, payload: Record<string, unknown>) {
  const store = useLabguardStore.getState()
  store.touchLastUpdate(nowTimeText())
  addLog(topic, payload)

  if (payload.type === 'sensor' || topic === TOPICS.sensor) {
    store.setSensor({
      temperatureC: Number.isFinite(Number(payload.temperature_c)) ? Number(payload.temperature_c) : undefined,
      humidityRh: Number.isFinite(Number(payload.humidity_rh)) ? Number(payload.humidity_rh) : undefined,
      vocIndex: Number.isFinite(Number(payload.voc_index)) ? Number(payload.voc_index) : undefined,
      mq2Alarm: Boolean(payload.mq2_alarm),
      sensorOk: Boolean(payload.sensor_ok)
    })
  }

  if (payload.type === 'risk_state' || topic === TOPICS.risk) {
    updateRisk(payload)
  }

  if (payload.type === 'status' || topic === TOPICS.status) {
    store.setStatus({
      uptimeS: Number.isFinite(Number(payload.uptime_s)) ? Number(payload.uptime_s) : undefined,
      wifiRssi: Number.isFinite(Number(payload.wifi_rssi)) ? Number(payload.wifi_rssi) : undefined,
      version: typeof payload.version === 'string' ? payload.version : undefined
    })
  }

  if (payload.type === 'camera_frame' || topic === TOPICS.camera) {
    updateCamera(payload)
  }
}

export async function connectMqtt(url: string) {
  const store = useLabguardStore.getState()
  if (!url.trim()) {
    store.setConnection('warn', 'Broker 地址为空', false)
    return
  }

  disconnectMqtt(false)
  store.setBrokerUrl(url.trim())
  store.setConnection('pending', '连接 MQTT...', false)

  const mqtt = await loadMqtt()
  const connect = mqtt.connect ?? mqtt.default?.connect
  if (!connect) {
    throw new Error('mqtt 模块未提供 connect 方法')
  }

  client = connect(url.trim(), {
    clientId,
    clean: true,
    reconnectPeriod: 2000,
    connectTimeout: 5000
  })

  client.on('connect', () => {
    const nextStore = useLabguardStore.getState()
    nextStore.setConnection('ok', 'MQTT 已连接', true)
    nextStore.setCommandState('可发送')
    SUBSCRIBE_TOPICS.forEach((topic) => client?.subscribe(topic, { qos: topic === TOPICS.camera ? 0 : 1 }))
    publishMobileEvent('mobile_app_connected', 'online')
  })

  client.on('reconnect', () => {
    const nextStore = useLabguardStore.getState()
    nextStore.setConnection('pending', 'MQTT 重连中...', false)
    nextStore.setCommandState('重连中')
  })

  client.on('close', () => {
    const nextStore = useLabguardStore.getState()
    nextStore.setConnection('warn', 'MQTT 已断开', false)
    nextStore.setCommandState('等待连接')
  })

  client.on('error', () => {
    const nextStore = useLabguardStore.getState()
    nextStore.setConnection('warn', 'MQTT 连接错误', false)
    nextStore.setCommandState('连接错误')
  })

  client.on('message', (topic, message) => {
    try {
      handleMessage(topic, JSON.parse(message.toString()))
    } catch {
      addLog(topic, message.toString())
    }
  })
}

export function disconnectMqtt(sendOfflineEvent = true) {
  if (client) {
    if (sendOfflineEvent) {
      publishMobileEvent('mobile_app_disconnected', 'offline')
    }
    client.end(true)
    client = null
  }
  const store = useLabguardStore.getState()
  store.setConnection('warn', '未连接', false)
  store.setCommandState('等待连接')
}

export function sendCommand(command: string, extra: Record<string, unknown> = {}) {
  const payload = {
    node: 'mobile',
    type: 'command',
    command,
    target_node: 'indoor',
    ...extra,
    timestamp: timestamp()
  }

  const store = useLabguardStore.getState()
  if (publishJson(TOPICS.command, payload, 1)) {
    store.setCommandState(`已发送 ${command}`)
    publishMobileEvent('mobile_command', command)
    addLog(TOPICS.command, payload)
    return true
  }

  store.setCommandState('未连接')
  return false
}

export function toggleActuator(name: ActuatorName) {
  const store = useLabguardStore.getState()
  const current = store[name]
  const nextOn = !current.on
  store.setActuator(name, { on: nextOn })

  if (!sendCommand(`${name}_${nextOn ? 'on' : 'off'}`, nextOn ? { level_pct: current.level } : {})) {
    store.setActuator(name, { on: current.on })
  }
}

export function updateActuatorLevel(name: ActuatorName, level: number, publish = false) {
  const safeLevel = Math.max(0, Math.min(100, Number(level) || 0))
  const store = useLabguardStore.getState()
  const current = store[name]
  store.setActuator(name, { level: safeLevel })

  if (publish && current.on) {
    sendCommand(`${name}_on`, { level_pct: safeLevel })
  }
}

export function toggleAlarm() {
  const store = useLabguardStore.getState()
  const nextOn = !store.alarmOn
  store.setAlarmOn(nextOn)
  if (!sendCommand(`alarm_${nextOn ? 'on' : 'off'}`)) {
    store.setAlarmOn(!nextOn)
  }
}

export function getClientId() {
  return clientId.replace('labguard_mobile_', '')
}
