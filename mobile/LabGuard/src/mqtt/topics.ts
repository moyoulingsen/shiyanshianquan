export const TOPICS = {
  sensor: 'labguard/indoor/sensor',
  risk: 'labguard/indoor/risk',
  status: 'labguard/indoor/status',
  camera: 'labguard/indoor/camera',
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
