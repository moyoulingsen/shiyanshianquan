import { Pressable, Text, StyleSheet } from 'react-native'

type Props = {
  title: string
  onPress: () => void
  tone?: 'primary' | 'normal' | 'warning' | 'danger'
}

export function CommandButton({ title, onPress, tone = 'primary' }: Props) {
  return (
    <Pressable style={[styles.button, styles[tone]]} onPress={onPress}>
      <Text style={styles.text}>{title}</Text>
    </Pressable>
  )
}

const styles = StyleSheet.create({
  button: {
    flex: 1,
    minWidth: '45%',
    minHeight: 46,
    borderRadius: 12,
    alignItems: 'center',
    justifyContent: 'center',
    paddingHorizontal: 12
  },
  primary: {
    backgroundColor: '#17685f'
  },
  normal: {
    backgroundColor: '#168257'
  },
  warning: {
    backgroundColor: '#b66c12'
  },
  danger: {
    backgroundColor: '#b74731'
  },
  text: {
    color: '#ffffff',
    fontSize: 14,
    fontWeight: '800'
  }
})
