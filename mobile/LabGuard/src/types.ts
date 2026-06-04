export type ConnectionState = 'warn' | 'pending' | 'ok'

export type RiskKey = 'normal' | 'warning' | 'alarm' | 'emergency'

export type SensorState = {
  temperatureC?: number
  humidityRh?: number
  vocIndex?: number
  mq2Alarm?: boolean
  sensorOk?: boolean
}

export type RiskState = {
  level: number
  label: RiskKey
  text: string
}

export type StatusState = {
  uptimeS?: number
  wifiRssi?: number
  version?: string
}

export type ActuatorName = 'fan' | 'pump'

export type ActuatorState = {
  on: boolean
  level: number
}

export type CameraFrame = {
  uri: string
  format: string
  width?: number
  height?: number
  sequence?: number
  receivedAt: number
}

export type LogEntry = {
  id: string
  time: string
  topic: string
  payload: unknown
}
