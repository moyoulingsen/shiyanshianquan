import { View, Text, StyleSheet } from 'react-native'

import type { ConnectionState } from '../types'

type Props = {
  state: ConnectionState
  text: string
  lastUpdate: string
}

export function ConnectionPanel({ state, text, lastUpdate }: Props) {
  return (
    <View style={styles.panel}>
      <View style={styles.row}>
        <View style={[styles.dot, state === 'ok' ? styles.dotOk : state === 'pending' ? styles.dotPending : styles.dotWarn]} />
        <Text style={styles.text}>{text}</Text>
        <Text style={styles.muted}>{lastUpdate}</Text>
      </View>
    </View>
  )
}

const styles = StyleSheet.create({
  panel: {
    borderWidth: 1,
    borderColor: '#d4dfdc',
    borderRadius: 14,
    backgroundColor: '#ffffff',
    padding: 14,
    marginBottom: 12
  },
  row: {
    flexDirection: 'row',
    alignItems: 'center',
    gap: 8
  },
  dot: {
    width: 10,
    height: 10,
    borderRadius: 999
  },
  dotOk: {
    backgroundColor: '#168257'
  },
  dotPending: {
    backgroundColor: '#bb7416'
  },
  dotWarn: {
    backgroundColor: '#b53d33'
  },
  text: {
    fontSize: 14,
    fontWeight: '600',
    color: '#162522'
  },
  muted: {
    marginLeft: 'auto',
    color: '#64736f',
    fontSize: 12
  }
})
