import { View, Text, StyleSheet } from 'react-native'

type Row = {
  label: string
  value: string
}

type Props = {
  title: string
  rows: Row[]
  badgeText?: string
}

export function InfoPanel({ title, rows, badgeText }: Props) {
  return (
    <View style={styles.panel}>
      <View style={styles.header}>
        <Text style={styles.title}>{title}</Text>
        {badgeText ? <Text style={styles.badge}>{badgeText}</Text> : null}
      </View>
      {rows.map((row) => (
        <View key={row.label} style={styles.row}>
          <Text style={styles.label}>{row.label}</Text>
          <Text style={styles.value}>{row.value}</Text>
        </View>
      ))}
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
    gap: 10
  },
  header: {
    flexDirection: 'row',
    alignItems: 'center',
    justifyContent: 'space-between'
  },
  title: {
    color: '#162522',
    fontSize: 16,
    fontWeight: '700'
  },
  badge: {
    borderRadius: 999,
    backgroundColor: '#edf1f5',
    color: '#52606d',
    paddingHorizontal: 10,
    paddingVertical: 4,
    fontSize: 12,
    fontWeight: '700'
  },
  row: {
    flexDirection: 'row',
    alignItems: 'center',
    justifyContent: 'space-between',
    borderBottomWidth: StyleSheet.hairlineWidth,
    borderBottomColor: '#e2e8f0',
    paddingBottom: 8
  },
  label: {
    color: '#5c6a67',
    fontSize: 13
  },
  value: {
    color: '#162522',
    fontSize: 14,
    fontWeight: '700'
  }
})
