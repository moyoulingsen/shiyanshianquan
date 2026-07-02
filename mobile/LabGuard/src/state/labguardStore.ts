import { create } from 'zustand'

import type {
  ActuatorName,
  ActuatorState,
  CameraFrame,
  ConnectionState,
  LogEntry,
  RiskState,
  SensorState,
  StatusState
} from '../types'

const MAX_LOGS = 80

type LabguardStore = {
  brokerUrl: string
  connectionState: ConnectionState
  connectionText: string
  connected: boolean
  sensor: SensorState
  risk: RiskState
  status: StatusState
  fan: ActuatorState
  pump: ActuatorState
  alarmOn: boolean
  audioOn: boolean
  lightOn: boolean
  masterOn: boolean
  camera?: CameraFrame
  cameraStateText: string
  cameraStale: boolean
  lastUpdateAt?: number
  lastUpdateText: string
  staleStateText: string
  commandStateText: string
  logs: LogEntry[]
  setBrokerUrl: (value: string) => void
  setConnection: (state: ConnectionState, text: string, connected?: boolean) => void
  setSensor: (sensor: Partial<SensorState>) => void
  setRisk: (risk: Partial<RiskState>) => void
  setStatus: (status: Partial<StatusState>) => void
  setActuator: (name: ActuatorName, next: Partial<ActuatorState>) => void
  setAlarmOn: (value: boolean) => void
  setAudioOn: (value: boolean) => void
  setLightOn: (value: boolean) => void
  setMasterOn: (value: boolean) => void
  resetLocalControlState: () => void
  setCamera: (frame: CameraFrame) => void
  setCameraState: (text: string, stale?: boolean) => void
  touchLastUpdate: (timeText: string) => void
  setStaleState: (text: string) => void
  setCommandState: (text: string) => void
  addLog: (entry: LogEntry) => void
  clearLogs: () => void
}

export const useLabguardStore = create<LabguardStore>((set) => ({
  brokerUrl: '',
  connectionState: 'warn',
  connectionText: '未连接',
  connected: false,
  sensor: {},
  risk: {
    level: 0,
    label: 'normal',
    text: '--'
  },
  status: {},
  fan: { on: false, level: 100, source: 'off', autoOn: false, manualOn: false, manualOverride: false },
  pump: { on: false, level: 100, source: 'off', autoOn: false, manualOn: false, manualOverride: false },
  alarmOn: false,
  audioOn: false,
  lightOn: false,
  masterOn: false,
  camera: undefined,
  cameraStateText: '等待画面',
  cameraStale: true,
  lastUpdateAt: undefined,
  lastUpdateText: '--',
  staleStateText: '--',
  commandStateText: '等待连接',
  logs: [],
  setBrokerUrl: (brokerUrl) => set({ brokerUrl }),
  setConnection: (connectionState, connectionText, connected = connectionState === 'ok') =>
    set({ connectionState, connectionText, connected }),
  setSensor: (sensor) => set((state) => ({ sensor: { ...state.sensor, ...sensor } })),
  setRisk: (risk) => set((state) => ({ risk: { ...state.risk, ...risk } })),
  setStatus: (status) => set((state) => ({ status: { ...state.status, ...status } })),
  setActuator: (name, next) =>
    set((state) => ({
      [name]: {
        ...state[name],
        ...next,
        level: Math.max(0, Math.min(100, Number(next.level ?? state[name].level) || 0)),
        source: next.source ?? state[name].source,
        autoOn: next.autoOn ?? state[name].autoOn,
        manualOn: next.manualOn ?? state[name].manualOn,
        manualOverride: next.manualOverride ?? state[name].manualOverride
      }
    })),
  setAlarmOn: (alarmOn) => set({ alarmOn }),
  setAudioOn: (audioOn) => set({ audioOn }),
  setLightOn: (lightOn) => set({ lightOn }),
  setMasterOn: (masterOn) => set({ masterOn }),
  resetLocalControlState: () =>
    set({
      fan: { on: false, level: 100, source: 'off', autoOn: false, manualOn: false, manualOverride: false },
      pump: { on: false, level: 100, source: 'off', autoOn: false, manualOn: false, manualOverride: false },
      alarmOn: false,
      audioOn: false,
      lightOn: false,
      masterOn: false
    }),
  setCamera: (camera) => set({ camera, cameraStateText: '实时画面', cameraStale: false }),
  setCameraState: (cameraStateText, cameraStale = false) => set({ cameraStateText, cameraStale }),
  touchLastUpdate: (lastUpdateText) => set({ lastUpdateText, lastUpdateAt: Date.now(), staleStateText: '实时' }),
  setStaleState: (staleStateText) => set({ staleStateText }),
  setCommandState: (commandStateText) => set({ commandStateText }),
  addLog: (entry) =>
    set((state) => ({
      logs: [entry, ...state.logs].slice(0, MAX_LOGS)
    })),
  clearLogs: () => set({ logs: [] })
}))
