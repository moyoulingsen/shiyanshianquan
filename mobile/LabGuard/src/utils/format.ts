export function formatNumber(value: unknown, digits = 1) {
  return Number.isFinite(Number(value)) ? Number(value).toFixed(digits) : '--'
}

export function formatUptime(seconds: unknown) {
  const total = Number(seconds)
  if (!Number.isFinite(total)) return '--'
  const h = Math.floor(total / 3600)
  const m = Math.floor((total % 3600) / 60)
  const s = Math.floor(total % 60)
  if (h > 0) return `${h}h ${m}m ${s}s`
  if (m > 0) return `${m}m ${s}s`
  return `${s}s`
}

export function nowTimeText() {
  return new Date().toLocaleTimeString()
}

export function timestamp() {
  return Math.floor(Date.now() / 1000)
}
