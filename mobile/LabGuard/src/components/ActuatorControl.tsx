import { View, Text, Pressable, StyleSheet } from 'react-native'

import type { ActuatorName, ActuatorState } from '../types'
import { toggleActuator, updateActuatorLevel } from '../mqtt/client'

type Props = {
  name: ActuatorName
  title: string
  state: ActuatorState
}

export function ActuatorControl({ name, title, state }: Props) {
  return (
    <View style={styles.card}>
      <View style={styles.header}>
        <View>
          <Text style={styles.title}>{title}</Text>
          <Text style={styles.subtitle}>电压 / 速度 {state.level}%</Text>
        </View>
        <Pressable
          style={[styles.toggle, state.on ? styles.toggleOn : styles.toggleOff]}
          onPress={() => toggleActuator(name)}
        >
          <Text style={styles.toggleText}>{state.on ? '开启' : '关闭'}</Text>
        </Pressable>
      </View>
      <View style={styles.sliderRow}>
        <Pressable style={styles.stepButton} onPress={() => updateActuatorLevel(name, state.level - 5, state.on)}>
          <Text style={styles.stepText}>−</Text>
        </Pressable>
        <View style={styles.levelWrap}>
          <View style={styles.levelTrack}>
            <View style={[styles.levelFill, { width: `${state.level}%` }]} />
          </View>
          <Text style={styles.levelText}>{state.level}%</Text>
        </View>
        <Pressable style={styles.stepButton} onPress={() => updateActuatorLevel(name, state.level + 5, state.on)}>
          <Text style={styles.stepText}>+</Text>
        </Pressable>
      </View>
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
    fontWeight: '700'
  },
  subtitle: {
    marginTop: 3,
    color: '#64736f',
    fontSize: 13
  },
  toggle: {
    borderRadius: 999,
    minWidth: 68,
    minHeight: 36,
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
  sliderRow: {
    flexDirection: 'row',
    alignItems: 'center',
    gap: 8
  },
  levelWrap: {
    flex: 1,
    gap: 8,
    alignItems: 'stretch'
  },
  levelTrack: {
    height: 10,
    borderRadius: 999,
    overflow: 'hidden',
    backgroundColor: '#ccd7d3'
  },
  levelFill: {
    height: '100%',
    borderRadius: 999,
    backgroundColor: '#17685f'
  },
  levelText: {
    textAlign: 'center',
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

