import { Vibration } from 'react-native'

let lastVibrationAt = 0

export function vibrateForRisk(level: number) {
  const now = Date.now()
  if (now - lastVibrationAt < 2500) {
    return
  }

  if (level >= 3) {
    Vibration.vibrate([180, 80, 180])
    lastVibrationAt = now
    return
  }

  if (level >= 2) {
    Vibration.vibrate(120)
    lastVibrationAt = now
  }
}
