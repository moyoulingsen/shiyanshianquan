import { View, Text, Pressable, StyleSheet, type TextStyle } from 'react-native'
import Slider from '@react-native-community/slider'

import type { ActuatorName, ActuatorState } from '../types'
import { toggleActuator, updateActuatorLevel } from '../mqtt/client'

const actuatorMinLevel: Record<ActuatorName, number> = {
  fan: 60,
  pump: 55
}

type Props = {
  name: ActuatorName
  title: string
  state: ActuatorState
}

function sourceText(source: ActuatorState['source']) {
  if (source === 'manual') return '手动'
  if (source === 'auto') return '自动'
  if (source === 'mixed') return '手动+自动'
  return '关闭'
}

export function ActuatorControl({ name, title, state }: Props) {
  const minLevel = actuatorMinLevel[name]
  const displayLevel = Math.max(minLevel, state.level)

  return (
    <View style={styles.card}>
      <View style={styles.header}>
        <View>
          <Text style={styles.title}>{title}</Text>
          <Text style={styles.subtitle}>
            {state.on ? `${sourceText(state.source)} / 有效档位 ${displayLevel}%` : sourceText(state.source)}
          </Text>
        </View>
        <Pressable
          style={[styles.toggle, state.on ? styles.toggleOn : styles.toggleOff]}
          onPress={() => toggleActuator(name)}
        >
          <Text style={styles.toggleText}>{title}：{state.on ? '开启' : '关闭'}</Text>
        </Pressable>
      </View>
      {state.on ? (
        <View style={styles.sliderPanel}>
          <Text style={styles.sliderLabel}>{title}有效档位</Text>
          <View style={styles.sliderRow}>
            <Pressable style={styles.stepButton} onPress={() => updateActuatorLevel(name, state.level - 1)}>
              <Text style={styles.stepText}>−</Text>
            </Pressable>
            <Slider
              style={styles.slider}
              minimumValue={minLevel}
              maximumValue={100}
              step={1}
              value={displayLevel}
              minimumTrackTintColor="#17685f"
              maximumTrackTintColor="#ccd7d3"
              thumbTintColor="#17685f"
              onValueChange={(value) => updateActuatorLevel(name, value)}
            />
            <Pressable style={styles.stepButton} onPress={() => updateActuatorLevel(name, state.level + 1)}>
              <Text style={styles.stepText}>+</Text>
            </Pressable>
            <Text style={styles.levelText}>{displayLevel}%</Text>
          </View>
        </View>
      ) : null}
    </View>
  )
}

const styles = StyleSheet.create({
  card: {
    borderWidth: 1,
    borderColor: '#d4dfdc',
    borderRadius: 14,
    backgroundColor: '#ffffff',
    padding: 14,
    gap: 12
  },
  header: {
    flexDirection: 'row',
    alignItems: 'center',
    justifyContent: 'space-between',
    gap: 12
  },
  title: {
    color: '#162522',
    fontSize: 16,
    fontWeight: '720' as unknown as TextStyle['fontWeight']
  },
  subtitle: {
    marginTop: 3,
    color: '#64736f',
    fontSize: 13
  },
  toggle: {
    borderRadius: 999,
    minWidth: 96,
    minHeight: 36,
    paddingHorizontal: 12,
    alignItems: 'center',
    justifyContent: 'center'
  },
  toggleOn: {
    backgroundColor: '#168257'
  },
  toggleOff: {
    backgroundColor: '#b53d33'
  },
  toggleText: {
    color: '#ffffff',
    fontSize: 13,
    fontWeight: '800'
  },
  sliderPanel: {
    borderWidth: 1,
    borderColor: '#d4dfdc',
    borderRadius: 10,
    backgroundColor: '#f8fafc',
    padding: 10,
    gap: 8
  },
  sliderLabel: {
    color: '#64736f',
    fontSize: 12,
    fontWeight: '700'
  },
  sliderRow: {
    flexDirection: 'row',
    alignItems: 'center',
    gap: 8
  },
  slider: {
    flex: 1,
    height: 36
  },
  levelText: {
    minWidth: 46,
    textAlign: 'right',
    color: '#36514b',
    fontSize: 13,
    fontWeight: '700'
  },
  stepButton: {
    width: 38,
    height: 38,
    borderRadius: 10,
    borderWidth: 1,
    borderColor: '#c8d4d0',
    alignItems: 'center',
    justifyContent: 'center',
    backgroundColor: '#f8fafc'
  },
  stepText: {
    color: '#162522',
    fontSize: 24,
    lineHeight: 28,
    fontWeight: '600'
  }
})
