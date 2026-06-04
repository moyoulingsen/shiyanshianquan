import { View, Text, Image, StyleSheet } from 'react-native'

import type { CameraFrame } from '../types'

type Props = {
  frame?: CameraFrame
  stateText: string
  stale: boolean
}

export function CameraPanel({ frame, stateText, stale }: Props) {
  return (
    <View style={styles.panel}>
      <View style={styles.header}>
        <Text style={styles.title}>室内摄像头</Text>
        <Text style={[styles.badge, stale ? styles.badgeWarn : styles.badgeOk]}>{stateText}</Text>
      </View>
      <View style={styles.previewWrap}>
        {frame ? (
          <Image source={{ uri: frame.uri }} resizeMode="cover" style={styles.preview} />
        ) : (
          <View style={styles.emptyWrap}>
            <Text style={styles.emptyText}>暂无摄像头画面</Text>
          </View>
        )}
      </View>
      <View style={styles.metaRow}>
        <Text style={styles.metaLabel}>分辨率</Text>
        <Text style={styles.metaValue}>{frame?.width && frame?.height ? `${frame.width} × ${frame.height}` : '--'}</Text>
      </View>
      <View style={styles.metaRow}>
        <Text style={styles.metaLabel}>帧序号</Text>
        <Text style={styles.metaValue}>{frame?.sequence ?? '--'}</Text>
      </View>
      <View style={styles.metaRow}>
        <Text style={styles.metaLabel}>最后画面</Text>
        <Text style={styles.metaValue}>{frame ? new Date(frame.receivedAt).toLocaleTimeString() : '--'}</Text>
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
    paddingHorizontal: 10,
    paddingVertical: 4,
    fontSize: 12,
    fontWeight: '700',
    overflow: 'hidden'
  },
  badgeOk: {
    backgroundColor: '#e7f6ee',
    color: '#16704a'
  },
  badgeWarn: {
    backgroundColor: '#fff2d8',
    color: '#9a5a00'
  },
  previewWrap: {
    minHeight: 220,
    borderRadius: 12,
    overflow: 'hidden',
    backgroundColor: '#0f1720'
  },
  preview: {
    width: '100%',
    height: 220,
    backgroundColor: '#0f1720'
  },
  emptyWrap: {
    minHeight: 220,
    alignItems: 'center',
    justifyContent: 'center'
  },
  emptyText: {
    color: '#dbe4ee',
    fontSize: 14
  },
  metaRow: {
    flexDirection: 'row',
    justifyContent: 'space-between'
  },
  metaLabel: {
    color: '#5c6a67',
    fontSize: 13
  },
  metaValue: {
    color: '#162522',
    fontSize: 14,
    fontWeight: '700'
  }
})
