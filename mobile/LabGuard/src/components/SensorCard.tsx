import { View, Text, StyleSheet } from 'react-native'

type Props = {
  title: string
  value: string
  unit?: string
  danger?: boolean
}

export function SensorCard({ title, value, unit, danger = false }: Props) {
  return (
    <View style={styles.card}>
      <Text style={styles.title}>{title}</Text>
      <View style={styles.valueRow}>
        <Text style={[styles.value, danger && styles.valueDanger]}>{value}</Text>
        {unit ? <Text style={styles.unit}>{unit}</Text> : null}
      </View>
    </View>
  )
}

const styles = StyleSheet.create({
  card: {
    flex: 1,
    minHeight: 110,
    borderWidth: 1,
    borderColor: '#d4dfdc',
    borderRadius: 14,
    backgroundColor: '#ffffff',
    padding: 14,
    justifyContent: 'space-between'
  },
  title: {
    color: '#5c6a67',
    fontSize: 13,
    fontWeight: '700'
  },
  valueRow: {
    flexDirection: 'row',
    alignItems: 'flex-end',
    gap: 4
  },
  value: {
    fontSize: 30,
    fontWeight: '800',
    color: '#162522'
  },
  valueDanger: {
    color: '#a92f27'
  },
  unit: {
    marginBottom: 4,
    color: '#697875',
    fontSize: 12,
    fontWeight: '700'
  }
})
