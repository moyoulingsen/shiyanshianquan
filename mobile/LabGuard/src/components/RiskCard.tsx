import { View, Text, StyleSheet } from 'react-native'

import type { RiskKey } from '../types'

type Props = {
  label: RiskKey
  title: string
  text: string
}

const palette: Record<RiskKey, { bg: string; border: string; badge: string }> = {
  normal: { bg: '#e9f7ef', border: '#b9dfca', badge: '#168257' },
  warning: { bg: '#fff4dc', border: '#e9ca86', badge: '#b66c12' },
  alarm: { bg: '#ffe9e4', border: '#e5b5a9', badge: '#b74731' },
  emergency: { bg: '#f7dde5', border: '#dda2b4', badge: '#9d2148' }
}

export function RiskCard({ label, title, text }: Props) {
  const colors = palette[label]
  return (
    <View style={[styles.card, { backgroundColor: colors.bg, borderColor: colors.border }]}>
      <Text style={styles.eyebrow}>室内风险</Text>
      <Text style={styles.title}>{title}</Text>
      <Text style={styles.text}>{text}</Text>
      <View style={[styles.badge, { backgroundColor: colors.badge }]}>
        <Text style={styles.badgeText}>{title}</Text>
      </View>
    </View>
  )
}

const styles = StyleSheet.create({
  card: {
    borderWidth: 1,
    borderRadius: 16,
    padding: 18,
    gap: 8,
    marginBottom: 12
  },
  eyebrow: {
    color: '#52615d',
    fontSize: 13,
    fontWeight: '700'
  },
  title: {
    color: '#162522',
    fontSize: 34,
    fontWeight: '800'
  },
  text: {
    color: '#43534f',
    fontSize: 15
  },
  badge: {
    alignSelf: 'flex-start',
    borderRadius: 999,
    paddingHorizontal: 10,
    paddingVertical: 6,
    marginTop: 4
  },
  badgeText: {
    color: '#ffffff',
    fontSize: 12,
    fontWeight: '700'
  }
})
