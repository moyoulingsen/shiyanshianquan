export const TOPICS = {
  sensor: 'labguard/device/sensor',
  risk: 'labguard/device/risk',
  status: 'labguard/device/status',
  camera: 'labguard/device/camera',
  event: 'labguard/event',
  command: 'labguard/cmd/test'
} as const

export const SUBSCRIBE_TOPICS = [
  TOPICS.sensor,
  TOPICS.risk,
  TOPICS.status,
  TOPICS.camera,
  TOPICS.event
] as const
