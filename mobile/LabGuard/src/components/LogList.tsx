import { View, Text, Pressable, StyleSheet } from 'react-native'

import type { LogEntry } from '../types'

type Props = {
  logs: LogEntry[]
  onClear: () => void
}

function payloadText(payload: unknown) {
  if (typeof payload === 'string') return payload
  try {
    return JSON.stringify(payload)
  } catch {
    return String(payload)
  }
}

export function LogList({ logs, onClear }: Props) {
  return (
    <View style={styles.panel}>
      <View style={styles.header}>
        <Text style={styles.title}>消息流</Text>
        <Pressable style={styles.clearButton} onPress={onClear}>
          <Text style={styles.clearText}>清空</Text>
        </Pressable>
      </View>
      <View style={logs.length ? styles.list : styles.emptyList}>
        {logs.length ? (
          logs.map((item) => (
            <View key={item.id} style={styles.item}>
              <View style={styles.itemHeader}>
                <Text style={styles.time}>{item.time}</Text>
                <Text style={styles.topic} numberOfLines={1}>{item.topic}</Text>
              </View>
              <Text style={styles.payload} numberOfLines={4}>{payloadText(item.payload)}</Text>
            </View>
          ))
        ) : (
          <Text style={styles.emptyText}>暂无消息</Text>
        )}
      </View>
    </View>
  )
}

const styles = StyleSheet.create({
  panel: {
    flex: 1,
    borderWidth: 1,
    borderColor: '#d4dfdc',
    borderRadius: 14,
    backgroundColor: '#ffffff',
    padding: 14
  },
  header: {
    flexDirection: 'row',
    alignItems: 'center',
    justifyContent: 'space-between',
    marginBottom: 10
  },
  title: {
    color: '#162522',
    fontSize: 16,
    fontWeight: '700'
  },
  clearButton: {
    borderWidth: 1,
    borderColor: '#c8d4d0',
    borderRadius: 10,
    paddingHorizontal: 12,
    paddingVertical: 7,
    backgroundColor: '#ffffff'
  },
  clearText: {
    color: '#36514b',
    fontWeight: '700'
  },
  list: {
    gap: 10,
    paddingBottom: 20
  },
  emptyList: {
    minHeight: 200,
    alignItems: 'center',
    justifyContent: 'center'
  },
  emptyText: {
    color: '#64736f'
  },
  item: {
    borderWidth: 1,
    borderColor: '#edf1f5',
    borderRadius: 10,
    padding: 10,
    backgroundColor: '#f8fafc',
    gap: 6
  },
  itemHeader: {
    flexDirection: 'row',
    alignItems: 'center',
    gap: 8
  },
  time: {
    color: '#64736f',
    fontSize: 12
  },
  topic: {
    flex: 1,
    color: '#17685f',
    fontSize: 12,
    fontWeight: '700'
  },
  payload: {
    color: '#1f2d3a',
    fontSize: 12,
    lineHeight: 17
  }
})

