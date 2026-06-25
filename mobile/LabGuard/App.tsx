import { useEffect } from 'react'
import {
  Alert,
  Pressable,
  ScrollView,
  StyleSheet,
  Text,
  TextInput,
  View
} from 'react-native'
import { SafeAreaView } from 'react-native-safe-area-context'
import { StatusBar } from 'expo-status-bar'

import { ActuatorControl } from './src/components/ActuatorControl'
import { CameraPanel } from './src/components/CameraPanel'
import { ConnectionPanel } from './src/components/ConnectionPanel'
import { InfoPanel } from './src/components/InfoPanel'
import { LogList } from './src/components/LogList'
import { RiskCard } from './src/components/RiskCard'
import { SensorCard } from './src/components/SensorCard'
import { connectMqtt, disconnectMqtt, toggleAlarm } from './src/mqtt/client'
import { useLabguardStore } from './src/state/labguardStore'
import { formatNumber, formatUptime } from './src/utils/format'

declare const process: {
  env?: {
    EXPO_PUBLIC_LABGUARD_MQTT_WS_URL?: string
  }
}

const DEFAULT_BROKER_URL = process.env?.EXPO_PUBLIC_LABGUARD_MQTT_WS_URL || 'ws://localhost:9001'

function riskDisplayText(text: string, label: string) {
  if (text === 'toxic_gas_event' || text === 'smoke_and_gas_alarm') return '有毒气体事件'
  if (text === 'fire_event' || text === 'flame_confirmed') return '火灾事件'
  if (text === 'warning') return '高温预警'
  if (label === 'alarm') return '有毒气体事件'
  if (label === 'emergency') return '火灾事件'
  if (label === 'warning') return '高温预警'
  return text
}

export default function App() {
  const brokerUrl = useLabguardStore((state) => state.brokerUrl)
  const setBrokerUrl = useLabguardStore((state) => state.setBrokerUrl)
  const connectionState = useLabguardStore((state) => state.connectionState)
  const connectionText = useLabguardStore((state) => state.connectionText)
  const connected = useLabguardStore((state) => state.connected)
  const lastUpdateText = useLabguardStore((state) => state.lastUpdateText)
  const sensor = useLabguardStore((state) => state.sensor)
  const risk = useLabguardStore((state) => state.risk)
  const status = useLabguardStore((state) => state.status)
  const fan = useLabguardStore((state) => state.fan)
  const pump = useLabguardStore((state) => state.pump)
  const alarmOn = useLabguardStore((state) => state.alarmOn)
  const camera = useLabguardStore((state) => state.camera)
  const cameraStateText = useLabguardStore((state) => state.cameraStateText)
  const cameraStale = useLabguardStore((state) => state.cameraStale)
  const commandStateText = useLabguardStore((state) => state.commandStateText)
  const staleStateText = useLabguardStore((state) => state.staleStateText)
  const logs = useLabguardStore((state) => state.logs)
  const clearLogs = useLabguardStore((state) => state.clearLogs)

  const nodeRows = [
    { label: '运行时间', value: formatUptime(status.uptimeS) },
    { label: 'Wi‑Fi RSSI', value: status.wifiRssi === undefined ? '--' : `${status.wifiRssi} dBm` },
    { label: '固件版本', value: status.version ?? '--' },
    { label: '最近数据', value: staleStateText }
  ]

  const handleConnectToggle = async () => {
    if (connected) {
      disconnectMqtt()
      return
    }

    try {
      await connectMqtt(brokerUrl)
    } catch (error) {
      const message = error instanceof Error ? error.message : '连接初始化失败'
      Alert.alert('MQTT 初始化失败', message)
    }
  }

  useEffect(() => {
    if (!brokerUrl.trim()) {
      setBrokerUrl(DEFAULT_BROKER_URL)
    }
  }, [brokerUrl, setBrokerUrl])

  useEffect(() => {
    return () => {
      disconnectMqtt(false)
    }
  }, [])

  useEffect(() => {
    const timer = setInterval(() => {
      const store = useLabguardStore.getState()
      const now = Date.now()

      if (store.lastUpdateAt && now - store.lastUpdateAt > 5000) {
        store.setStaleState('数据过期')
      } else if (store.lastUpdateAt) {
        store.setStaleState('实时')
      }

      if (!store.camera?.receivedAt) {
        store.setCameraState('等待画面', true)
      } else if (now - store.camera.receivedAt > 5000) {
        store.setCameraState('画面过期', true)
      } else if (store.cameraStale || store.cameraStateText !== '实时画面') {
        store.setCameraState('实时画面', false)
      }
    }, 1000)

    return () => clearInterval(timer)
  }, [])

  const actuatorRows = [
    { label: '风扇', value: `${fan.on ? '开启' : '关闭'} / ${fan.level}%` },
    { label: '水泵', value: `${pump.on ? '开启' : '关闭'} / ${pump.level}%` },
    { label: '报警', value: alarmOn ? '开启' : '关闭' },
    { label: '发送状态', value: commandStateText }
  ]

  return (
    <SafeAreaView style={styles.safeArea} edges={['top', 'left', 'right']}>
      <StatusBar style="dark" />
      <ScrollView contentContainerStyle={styles.scrollContent}>
        <View style={styles.header}>
          <View>
            <Text style={styles.title}>LabGuard</Text>
            <Text style={styles.subtitle}>硬件联动控制</Text>
          </View>
          <Pressable style={[styles.connectButton, connected ? styles.disconnectButton : null]} onPress={handleConnectToggle}>
            <Text style={styles.connectButtonText}>{connected ? '断开' : '连接'}</Text>
          </Pressable>
        </View>

        <View style={styles.brokerPanel}>
          <Text style={styles.inputLabel}>MQTT WebSocket</Text>
          <TextInput
            style={styles.input}
            autoCapitalize="none"
            autoCorrect={false}
            value={brokerUrl}
            onChangeText={setBrokerUrl}
            placeholder={DEFAULT_BROKER_URL}
          />
          <Text style={styles.helperText}>默认 broker 地址由启动脚本注入；点击右上角“连接”后再收发 MQTT。</Text>
        </View>

        <ConnectionPanel state={connectionState} text={connectionText} lastUpdate={lastUpdateText} />

        <RiskCard label={risk.label} title={`风险 ${risk.level}`} text={riskDisplayText(risk.text, risk.label)} />

        <View style={styles.metricGrid}>
          <SensorCard title="温度" value={formatNumber(sensor.temperatureC)} unit="°C" />
          <SensorCard title="湿度" value={formatNumber(sensor.humidityRh)} unit="%RH" />
          <SensorCard title="VOC" value={formatNumber(sensor.vocIndex, 0)} unit="index" />
          <SensorCard title="MQ-2" value={sensor.mq2Alarm === undefined ? '--' : sensor.mq2Alarm ? '报警' : '正常'} danger={sensor.mq2Alarm} />
        </View>

        <CameraPanel frame={camera} stateText={cameraStateText} stale={cameraStale} />

        <View style={styles.panelColumn}>
          <InfoPanel title="执行器" rows={actuatorRows} badgeText="硬件联动中" />

          <View style={styles.panel}>
            <View style={styles.panelHeader}>
              <View style={styles.panelHeaderText}>
                <Text style={styles.panelTitle}>执行器控制</Text>
                <Text style={styles.panelDesc}>通过按钮与滑杆调节风扇和水泵输出。</Text>
              </View>
              <View style={[styles.alarmBadge, alarmOn ? styles.alarmBadgeOn : styles.alarmBadgeOff]}>
                <Text style={styles.alarmBadgeText}>{alarmOn ? '报警开启' : '报警关闭'}</Text>
              </View>
            </View>

            <View style={styles.controlColumn}>
              <ActuatorControl name="fan" title="风扇" state={fan} />
              <ActuatorControl name="pump" title="水泵" state={pump} />
            </View>

            <Pressable style={[styles.alarmToggleButton, alarmOn ? styles.alarmToggleButtonOn : styles.alarmToggleButtonOff]} onPress={toggleAlarm}>
              <Text style={styles.alarmToggleButtonText}>{alarmOn ? '关闭报警' : '开启报警'}</Text>
            </Pressable>
          </View>

          <InfoPanel title="节点状态" rows={nodeRows} />
        </View>

        <View style={styles.logsWrap}>
          <LogList logs={logs} onClear={clearLogs} />
        </View>
      </ScrollView>
    </SafeAreaView>
  )
}

const styles = StyleSheet.create({
  safeArea: {
    flex: 1,
    backgroundColor: '#eef3f1'
  },
  scrollContent: {
    paddingHorizontal: 14,
    paddingTop: 12,
    paddingBottom: 24,
    backgroundColor: '#eef3f1',
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
    fontSize: 30,
    fontWeight: '800'
  },
  subtitle: {
    marginTop: 4,
    color: '#64736f',
    fontSize: 14,
    fontWeight: '600'
  },
  connectButton: {
    borderRadius: 999,
    minWidth: 76,
    minHeight: 38,
    paddingHorizontal: 16,
    alignItems: 'center',
    justifyContent: 'center',
    backgroundColor: '#17685f'
  },
  disconnectButton: {
    backgroundColor: '#8f3f36'
  },
  connectButtonText: {
    color: '#ffffff',
    fontSize: 14,
    fontWeight: '800'
  },
  brokerPanel: {
    borderWidth: 1,
    borderColor: '#d4dfdc',
    borderRadius: 14,
    backgroundColor: '#ffffff',
    padding: 14,
    gap: 8
  },
  inputLabel: {
    color: '#162522',
    fontSize: 14,
    fontWeight: '700'
  },
  input: {
    borderWidth: 1,
    borderColor: '#d4dfdc',
    borderRadius: 12,
    backgroundColor: '#f8fafc',
    paddingHorizontal: 12,
    paddingVertical: 10,
    color: '#162522',
    fontSize: 14
  },
  helperText: {
    color: '#64736f',
    fontSize: 12,
    lineHeight: 18
  },
  metricGrid: {
    flexDirection: 'row',
    flexWrap: 'wrap',
    gap: 10
  },
  panelColumn: {
    gap: 10
  },
  panel: {
    borderWidth: 1,
    borderColor: '#d4dfdc',
    borderRadius: 14,
    backgroundColor: '#ffffff',
    padding: 14,
    gap: 12
  },
  panelHeader: {
    flexDirection: 'row',
    alignItems: 'flex-start',
    justifyContent: 'space-between',
    gap: 12
  },
  panelHeaderText: {
    flex: 1,
    gap: 4
  },
  panelTitle: {
    color: '#162522',
    fontSize: 16,
    fontWeight: '800'
  },
  panelDesc: {
    color: '#64736f',
    fontSize: 12,
    lineHeight: 18
  },
  controlColumn: {
    gap: 10
  },
  alarmBadge: {
    borderRadius: 999,
    paddingHorizontal: 10,
    paddingVertical: 6
  },
  alarmBadgeOn: {
    backgroundColor: '#ffe9e4'
  },
  alarmBadgeOff: {
    backgroundColor: '#edf2f0'
  },
  alarmBadgeText: {
    color: '#36514b',
    fontSize: 12,
    fontWeight: '700'
  },
  alarmToggleButton: {
    minHeight: 46,
    borderRadius: 12,
    alignItems: 'center',
    justifyContent: 'center',
    paddingHorizontal: 12
  },
  alarmToggleButtonOn: {
    backgroundColor: '#b53d33'
  },
  alarmToggleButtonOff: {
    backgroundColor: '#17685f'
  },
  alarmToggleButtonText: {
    color: '#ffffff',
    fontSize: 14,
    fontWeight: '800'
  },
  logsWrap: {
    minHeight: 320
  }
})
