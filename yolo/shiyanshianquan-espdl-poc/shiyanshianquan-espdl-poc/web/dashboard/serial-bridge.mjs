import { SerialPort } from 'serialport'
import { ReadlineParser } from '@serialport/parser-readline'
import { WebSocketServer } from 'ws'

const portPath = process.env.LABGUARD_PORT || '/dev/ttyACM0'
const baudRate = Number(process.env.LABGUARD_BAUD || 115200)
const wsPort = Number(process.env.LABGUARD_WS_PORT || 8787)

const publishRe = /local publish topic=([^ ]+).*payload=(\{.*\})/
const clients = new Set()

function broadcast(frame) {
  const data = JSON.stringify(frame)
  for (const client of clients) {
    if (client.readyState === client.OPEN) {
      client.send(data)
    }
  }
}

const wss = new WebSocketServer({ port: wsPort })
wss.on('connection', (socket) => {
  clients.add(socket)
  socket.send(JSON.stringify({ type: 'bridge_status', message: 'serial bridge connected' }))
  socket.on('close', () => clients.delete(socket))
  socket.on('message', (data) => {
    console.log(`[dashboard command] ${data}`)
  })
})

const serial = new SerialPort({ path: portPath, baudRate })
const parser = serial.pipe(new ReadlineParser({ delimiter: '\n' }))

serial.on('open', () => {
  console.log(`LabGuard serial bridge listening on ws://localhost:${wsPort}`)
  console.log(`Reading ${portPath} at ${baudRate} baud`)
})

serial.on('error', (error) => {
  console.error(`Serial error: ${error.message}`)
})

parser.on('data', (line) => {
  const cleanLine = line.trim()
  const match = cleanLine.match(publishRe)
  if (!match) {
    return
  }

  try {
    broadcast({
      topic: match[1],
      payload: JSON.parse(match[2]),
      line: cleanLine
    })
  } catch {
    broadcast({ line: cleanLine })
  }
})
