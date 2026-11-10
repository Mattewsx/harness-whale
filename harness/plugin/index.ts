import dgram from 'node:dgram'
import { createVoiceService } from './voice.ts'
import { spawn, type ChildProcess } from 'node:child_process'
import { randomUUID, createHash } from 'node:crypto'
import { existsSync } from 'node:fs'
import { homedir } from 'node:os'
import { dirname, join } from 'node:path'

export const name = 'harness-whale-companion'
export const inject = ['commands', 'tools', 'sessionTitle', 'workspaceRegistry', 'sessions', 'sessionPersistence']

const PORT = Number.parseInt(process.env.HARNESS_WHALE_UDP_PORT || '8765', 10)
const TELEMETRY_PORT = Number.parseInt(process.env.HARNESS_WHALE_TELEMETRY_PORT || '8766', 10)
const HOST = '127.0.0.1'

type Status = {
  v: 1
  sessionId: string
  runId: string
  hint?: number
  state: 'offline' | 'idle' | 'thinking' | 'tool' | 'waiting' | 'done' | 'error' | 'stopped' | 'question'
  tool: string
  elapsed: number
  todoDone: number
  todoTotal: number
  newTurn?: boolean
  title: string
  sentAt: number
}

type HardwareStatus = {
  connected: boolean
  state: string
  device: string
  mtu?: number
  frame?: number
  packetsSent?: number
  error?: string
  disconnects?: number
  lastDisconnect?: string
  lastDisconnectAt?: number
  lastReconnectMs?: number
  retryIn?: number
  receivedAt: number
}

function finite(value: unknown): number | undefined {
  return typeof value === 'number' && Number.isFinite(value) ? value : undefined
}

function ago(ms: number): string {
  const seconds = Math.max(0, Math.floor(ms / 1000))
  if (seconds < 120) return `${seconds}s ago`
  if (seconds < 7200) return `${Math.floor(seconds / 60)}m ago`
  return `${Math.floor(seconds / 3600)}h ago`
}

function eventType(event: any): string {
  return String(event?.type || event?.event || event?.name || '')
}

function eventBody(event: any): any {
  if (event?.data && typeof event.data === 'object') return event.data
  return event?.payload && typeof event.payload === 'object' ? event.payload : event
}

function sessionKey(session: any): string {
  return String(session?.header?.id || session?.id || session?.sessionId || session?.key || 'default')
}

function isMainSession(session: any): boolean {
  const h = session?.header
  return !!h?.id && h.origin !== 'subagent' && !(h.delegationDepth > 0)
}

function assistantText(body: any): string {
  const content = body?.message?.content
  if (body?.interrupted || body?.message?.role !== 'assistant' || !Array.isArray(content) ||
      content.some(block => block?.type === 'tool-call')) return ''
  return content.filter(block => block?.type === 'text' && typeof block.text === 'string')
    .map(block => block.text).join('\n').replace(/\r\n?/g,'\n').replace(/[\x00-\x09\x0b-\x1f\x7f]/g,' ').trim()
}

function replyPages(text: string): string[] {
  const pages: string[] = []
  let part = '', bytes = 0
  for (const ch of text) {
    const n = Buffer.byteLength(ch)
    if (bytes+n>384) { pages.push(part); part=''; bytes=0 }
    part+=ch; bytes+=n
  }
  if (part) pages.push(part)
  return pages
}

function latestSessionTitle(ctx: any, session: any, eventTitle?: unknown): string {
  const fromEvent = typeof eventTitle === 'string' ? eventTitle.trim() : ''
  if (fromEvent) return fromEvent
  try {
    return String(ctx.sessionTitle?.get?.(session)?.title || '').trim()
  } catch {
    return ''
  }
}

function toolName(body: any): string {
  return String(body?.name || body?.tool?.name || body?.call?.name || body?.tool || body?.toolName || 'other')
}

function reasonKind(value: any): string {
  return String(value?.kind || value || '').trim().toLowerCase()
}

function isQuestionTool(name: string): boolean {
  return name.trim().toLowerCase().replace(/[.\/-]/g, '_') === 'ask_user_question'
}

type QuestionOption = { label: string; description?: string }
type HardwareQuestion = {
  id: string
  question: string
  options?: QuestionOption[]
  multi_select?: boolean
}

export function hardwareQuestions(value: any): HardwareQuestion[] | null {
  const questions = value?.questions
  if (!Array.isArray(questions) || questions.length === 0) return null
  for (const question of questions) {
    if (!question || typeof question.id !== 'string' || !Array.isArray(question.options) ||
        question.options.length < 2 || (question.multi_select === true && question.options.length > 64) ||
        question.options.some((option: any) => !option || typeof option.label !== 'string')) {
      return null
    }
  }
  return questions as HardwareQuestion[]
}

type DeviceMode = 'single' | 'multi' | 'approval'

// Offer one prompt to HARNESS-WHALE and resolve with the chosen option indices.
function askDevice(prompt: string, offered: string[], mode: DeviceMode, signal: AbortSignal, requestId: string, sessionId: string, runId: string): Promise<number[]> {
  const options = offered
  const client = dgram.createSocket('udp4')
  return new Promise((resolve, reject) => {
    let settled = false
    let replay: ReturnType<typeof setInterval> | undefined
    const finish = (error?: Error, selected?: number[]) => {
      if (settled) return
      settled = true
      signal.removeEventListener('abort', onAbort)
      clearInterval(replay)
      client.close()
      if (error) reject(error)
      else resolve(selected!)
    }
    const onAbort = () => {
      finish(new Error('hardware question cancelled'))
    }
    signal.addEventListener('abort', onAbort, { once: true })
    client.on('error', error => finish(error))
    client.on('message', (data, info) => {
      if (info.address !== '127.0.0.1' && info.address !== '::1') return
      try {
        const answer = JSON.parse(data.toString('utf8'))
        if (answer?.kind === 'whale-detach' && answer.requestId === requestId) {
          finish(new Error('device detached'))
          return
        }
        if (info.port !== PORT || answer?.v !== 1 || answer?.kind !== 'harness-answer' ||
            answer?.requestId !== requestId || !Number.isInteger(answer?.selected)) return
        const picked: unknown[] = mode === 'multi' ? answer.selectedList : [answer.selected]
        if (Array.isArray(picked) && picked.length > 0 && new Set(picked).size === picked.length &&
            picked.every(index => Number.isInteger(index) && Number(index) >= 0 && Number(index) < offered.length)) {
          finish(undefined, (picked as number[]).slice().sort((x, y) => x - y))
        }
      } catch {
        // Ignore malformed loopback replies.
      }
    })
    client.bind(0, HOST, () => {
      if (signal.aborted) return onAbort()
      const document = Buffer.from(JSON.stringify({
        v: 1, kind: 'harness-question', requestId,
        question: prompt, sessionId, runId,
        options, mode,
      }))
      client.send(document, PORT, HOST)
      replay = setInterval(() => client.send(document, PORT, HOST), 1500)
    })
  })
}

async function askOnHardware(questions: HardwareQuestion[], signal: AbortSignal, requestId: string, sessionId: string, runId: string) {
  const answers = []
  for (const question of questions) {
    const labels = question.options!.map(option => option.label)
    const picked = await askDevice(question.question, labels, question.multi_select === true ? 'multi' : 'single',
      signal, requestId, sessionId, runId)
    answers.push({ id: question.id, selected: picked.map(index => labels[index]) })
  }
  const value = { answers }
  return {
    content: [{ type: 'text', text: JSON.stringify(value) }],
    isError: false,
    value,
  }
}

export const APPROVAL_OPTIONS = ['允许', '拒绝']

// displayReason is LocalizedText: alternate translations of the same reason,
// not additional permission scopes. Keep the canonical reason in full and
// select one complete display translation instead of sending every language.
function approvalReasons(req: any): Record<string, string> | null {
  const result: Record<string, string> = {}
  if (req?.reason != null) {
    if (typeof req.reason !== 'string' || !req.reason.trim()) return null
    result.reason = req.reason
  }
  const display = req?.displayReason
  if (display != null) {
    if (typeof display === 'string') {
      if (!display.trim()) return null
      result.displayReason = display
    } else {
      if (typeof display !== 'object' || Array.isArray(display)) return null
      const keys = Object.keys(display)
      if (!keys.length || Reflect.ownKeys(display).length !== keys.length ||
          keys.some(key => !/^[a-z]{2,3}(?:-[a-zA-Z0-9]{2,8})*$/.test(key) ||
            typeof Object.getOwnPropertyDescriptor(display, key)?.value !== 'string')) return null
      const locale = ['zh-CN', 'zh-Hans', 'zh', 'en'].find(key =>
        Object.hasOwn(display, key) && display[key].trim())
      if (!locale) return null
      result.displayReason = display[locale]
    }
  }
  return Object.keys(result).length ? result : null
}

function callArguments(session: any, callId: unknown): any {
  if (!callId) return undefined
  const events = session?.ownEvents?.() || []
  for (let i = events.length - 1; i >= 0; i--) {
    const body = eventBody(events[i])
    if (eventType(events[i]) === 'tool/call' && [body?.id, body?.callId, body?.call?.id].includes(callId)) {
      const args = body?.arguments ?? body?.args ?? body?.input ?? body?.call?.arguments
      // DSH's durable tool/call event stores arguments as a JSON string.
      // Decode the complete input rather than double-encoding it for display.
      if (typeof args === 'string') {
        try { return JSON.parse(args) } catch { return undefined }
      }
      return args
    }
  }
  return undefined
}

// Never summarize an authorization: every argument and permission reason must fit.
// JSON escapes line breaks faithfully, so shell comments cannot hide a later line.
export function approvalPrompt(toolName: string, args: any, reason: unknown): string | null {
  if (!toolName.trim() || args == null || reason == null || reason === '') return null
  const seen = new Set<object>()
  const faithful = (value: any): boolean => {
    if (typeof value === 'string') return !/[\x00-\x08\x0b\x0c\x0e-\x1f\x7f-\x9f\p{Cf}\p{Cs}]/u.test(value)
    if (value === null || typeof value === 'boolean') return true
    if (typeof value === 'number') return Number.isFinite(value)
    if (typeof value !== 'object' || seen.has(value)) return false
    if (!Array.isArray(value) && Object.getPrototypeOf(value) !== Object.prototype) return false
    seen.add(value)
    const keys = Object.keys(value)
    const valid = (Array.isArray(value) ? keys.length === value.length && keys.every((key, i) => key === String(i)) : true) &&
      keys.every(key => Object.hasOwn(Object.getOwnPropertyDescriptor(value, key)!, 'value') && faithful(key) && faithful(value[key])) &&
      Reflect.ownKeys(value).length === keys.length + (Array.isArray(value) ? 1 : 0)
    seen.delete(value)
    return valid
  }
  // A path alone cannot describe the change being approved.
  if (/write|edit|patch|create/i.test(toolName) &&
      (!args || typeof args !== 'object' ||
       !['content', 'contents', 'text', 'new_string', 'patch', 'diff', 'edits'].some(field => field in args))) return null
  try {
    const document = { tool: toolName, arguments: args, reason }
    if (!faithful(document)) return null
    const prompt = `允许？ ${JSON.stringify(document)}`
    return Buffer.byteLength(prompt, 'utf8') <= 512 ? prompt : null
  } catch {
    return null
  }
}

export function apply(ctx: any) {
  const socket = dgram.createSocket('udp4')
  const telemetrySocket = dgram.createSocket({ type: 'udp4', reuseAddr: true })
  const states = new Map<string, Status & { startedAt?: number }>()
  const results = new Map<string, { data: Buffer, expires: number }>()
  const replies = new Map<string,{ id:string, preview:string, pages:string[] }>()
  const observed = new Set<string>()
  const candidates = new Map<string,{ turn:number, text:string }>()
  const saveReply = (key:string,text:string) => {
    if (text) replies.set(key,{id:'history-'+createHash('sha256').update(text).digest('hex'),preview:Array.from(text.replace(/\s+/g,' ')).slice(0,20).join(''),pages:replyPages(text)})
  }
  // Archiving updates the Workspace singleton; it does not end or delete a Session.
  const visibility = new Map<string, {key:string, hidden:boolean, stamp:number}>()
  let sequence = Date.now()
  const stamp = () => sequence = Math.max(sequence + 1, Date.now())
  const publishVisibility = (items: Array<{key:string,hidden:boolean,stamp:number}>) => {
    for (let i=0;i<items.length;i+=100) {
      const message = {v:1,kind:'whale-visibility',items:items.slice(i,i+100)}
      socket.send(Buffer.from(JSON.stringify(message)),PORT,HOST)
    }
  }
  const removed = new Set<string>()
  const disposed = new WeakSet<object>()
  const checking = new Set<string>()
  let stopped = false
  const archiveIds = (): readonly string[] | undefined => {
    try { return ctx.workspaceRegistry.archivedSessionIds } catch { return undefined }
  }
  const mark = (key:string, hidden:boolean) => {
    const item = {key,hidden,stamp:stamp()}
    visibility.set(key,item)
    return item
  }
  const visible = (key:string) => {
    const archive = archiveIds()
    if (!archive) return false
    const list = ctx.workspaceRegistry.list?.()
    const outsideWorkspace = list && !list.some((workspace:any)=>workspace.sessionIds.includes(key))
    const hidden = removed.has(key) || archive.includes(key) || Boolean(outsideWorkspace)
    if ((visibility.get(key)?.hidden ?? false) !== hidden) publishVisibility([mark(key,hidden)])
    return !hidden
  }
  const syncVisibility = () => {
    const archive = archiveIds()
    if (!archive) return
    const ids=new Set([...archive,...removed])
    for (const key of ids) if(!visibility.get(key)?.hidden) mark(key,true)
    const workspaces = ctx.workspaceRegistry.list?.()
    for (const item of visibility.values()) {
      const member = !workspaces || workspaces.some((w:any)=>w.sessionIds.includes(item.key))
      if(item.hidden && !ids.has(item.key) && member) mark(item.key,false)
    }
    // Replay tombstones as well as restorations after dropped UDP packets or a bridge restart.
    publishVisibility([...visibility.values()])
    if (ids.has(activeKey)) activeKey=[...states.keys()].find(key=>!ids.has(key)) || 'agent'
  }
  const observeSession = (session:any) => {
    if (!isMainSession(session) || disposed.has(session)) return false
    const key=sessionKey(session)
    if (removed.has(key) && ctx.sessions?.get(key) === session) removed.delete(key)
    if (!visible(key)) return false
    if (!observed.has(key)) {
      observed.add(key)
      // Restore only the latest completed, non-inherited answer.
      const events=session.ownEvents?.() || []
      let endTurn: number | undefined
      for(let i=events.length-1;i>=0;i--) {
        const e=events[i], b=eventBody(e)
        if(endTurn===undefined && e.type==='turn/end' && b.reason?.kind==='completed') endTurn=b.turn
        else if(endTurn!==undefined && e.type==='assistant/message' && b.turn===endTurn) {
          const text=assistantText(b)
          if(text){saveReply(key,text);break}
          endTurn=undefined
        } else if(endTurn!==undefined && e.type==='turn/start' && b.turn===endTurn) endTurn=undefined
      }
    }
    return true
  }
  socket.on('message',(data,peer)=>{
    if(peer.address!==HOST || peer.port!==PORT)return
    try {
      const req=JSON.parse(data.toString('utf8'))
      if(req.kind!=='whale-read-reply' || !Number.isInteger(req.page) || req.page<0)return
      if(!visible(req.sessionId))return
      const reply=replies.get(req.sessionId)
      if(!reply || reply.id!==req.replyId || req.page>=reply.pages.length)return
      const out={v:1,...req,kind:'whale-reply-page',text:reply.pages[req.page],count:reply.pages.length}
      socket.send(Buffer.from(JSON.stringify(out)),PORT,HOST)
    } catch {}
  })
  const voiceService = createVoiceService(ctx, visible, isMainSession, key => ['question','waiting'].includes(states.get(key)?.state || ''), () => activeKey)
  let activeKey = 'agent'
  let bridgeChild: ChildProcess | null = null
  let hardware: HardwareStatus = {
    connected: false,
    state: 'bridge-offline',
    device: 'HARNESS-WHALE',
    receivedAt: 0,
  }

  telemetrySocket.on('message', (data: Buffer, info: dgram.RemoteInfo) => {
    if (info.address !== '127.0.0.1' && info.address !== '::1') return
    try {
      const message = JSON.parse(data.toString('utf8'))
      if (message?.v !== 1 || message?.kind !== 'harness-whale-hardware') return
      const next: HardwareStatus = {
        connected: Boolean(message.connected),
        state: String(message.state || 'unknown'),
        device: String(message.device || 'HARNESS-WHALE'),
        mtu: Number.isFinite(message.mtu) ? Number(message.mtu) : undefined,
        frame: finite(message.frame),
        packetsSent: Number.isFinite(message.packetsSent) ? Number(message.packetsSent) : undefined,
        error: message.error ? String(message.error) : undefined,
        disconnects: finite(message.disconnects),
        lastDisconnect: message.lastDisconnect ? String(message.lastDisconnect).slice(0, 64) : undefined,
        lastDisconnectAt: finite(message.lastDisconnectAt),
        lastReconnectMs: finite(message.lastReconnectMs),
        retryIn: finite(message.retryIn),
        receivedAt: Date.now(),
      }
      if (next.connected !== hardware.connected || next.state !== hardware.state) {
        console.info(`[harness-whale] hardware ${next.connected ? 'connected' : next.state}`)
      }
      hardware = next
    } catch {
      // Telemetry is loopback-only and non-authoritative; ignore malformed frames.
    }
  })
  telemetrySocket.on('error', (error: Error) => {
    console.warn(`[harness-whale] telemetry unavailable: ${error.name}`)
  })
  telemetrySocket.bind(TELEMETRY_PORT, HOST)

  const BRIDGE_DIR = join(homedir(), '.dsh', 'harness-whale-bridge')
  let bridgeFailure = ''
  const startManagedBridge = () => {
    if (bridgeChild || (hardware.receivedAt > 0 && Date.now() - hardware.receivedAt < 7000)) return
    const script = process.env.HARNESS_WHALE_BRIDGE_SCRIPT || join(BRIDGE_DIR, 'harness_ble_bridge.py')
    const python = process.env.HARNESS_WHALE_PYTHON || (process.platform === 'win32'
      ? join(BRIDGE_DIR, '.venv', 'Scripts', 'python.exe')
      : join(BRIDGE_DIR, '.venv', 'bin', 'python'))
    if (!existsSync(script) || !existsSync(python)) return
    console.info('[harness-whale] starting managed BLE bridge')
    let child: ChildProcess
    try {
      child = spawn(python, [script], {
        cwd: dirname(script),
        env: { ...process.env, ...voiceService.environment() },
        stdio: ['ignore', 'ignore', 'pipe'],
        windowsHide: true,
      })
    } catch (error) {
      bridgeFailure = error instanceof Error ? error.message : String(error)
      console.warn('[harness-whale] managed bridge failed:', bridgeFailure)
      bridgeChild = null
      return
    }
    // Attach this in the same tick as spawn(): an unhandled 'error' event
    // (ENOENT, EACCES, missing cwd) is an uncaughtException that takes the
    // whole Harness Host process down.
    child.on('error', (error: Error) => {
      bridgeFailure = error.message
      console.warn('[harness-whale] managed bridge failed:', error.message)
      if (bridgeChild === child) bridgeChild = null
    })
    child.once('close', () => {
      if (bridgeChild === child) bridgeChild = null
    })
    child.stderr?.setEncoding('utf8')
    child.stderr?.on('data', (chunk: string) => {
      for (const line of chunk.split(/\r?\n/).map(value => value.trim()).filter(Boolean)) {
        console.info(`[harness-whale-bridge] ${line}`)
      }
    })
    bridgeChild = child
  }
  // A throwing timer callback is fatal as well, so guard the retry loop itself.
  const guardedBridgeStart = () => {
    try {
      startManagedBridge()
    } catch (error) {
      console.warn('[harness-whale] bridge guard error:', error instanceof Error ? error.message : String(error))
    }
  }
  const bridgeGuard = setInterval(guardedBridgeStart, 5000)
  const bridgeStartTimer = setTimeout(guardedBridgeStart, 2500)

  // Race the computer UI (next) against HARNESS-WHALE. Whichever answers first
  // wins; the loser is cancelled through the swapped downstream signal.
  const raceDevice = async <T>(
    requestId: string,
    holder: { signal?: AbortSignal },
    next: () => Promise<T>,
    ask: (signal: AbortSignal) => Promise<T>,
    accepted: (value: T) => number,
    key: string,
    waitForComputer: boolean,
  ): Promise<T> => {
    const result = (status: number, option = 65535) => {
      const msg = { v: 1, kind: 'whale-result', requestId, status, option }
      const data = Buffer.from(JSON.stringify(msg))
      results.set(requestId, { data, expires: Date.now() + 30000 })
      socket.send(data, PORT, HOST)
    }
    const downstream = new AbortController()
    const device = new AbortController()
    const originalSignal: AbortSignal | undefined = holder.signal
    const abortBoth = () => {
      result(2)
      downstream.abort(originalSignal?.reason)
      device.abort(originalSignal?.reason)
    }
    originalSignal?.addEventListener('abort', abortBoth, { once: true })
    try { holder.signal = downstream.signal } catch { /* frozen request: the computer keeps its own signal */ }

    const webResult = next().then(
      value => ({ source: 'web' as const, value }),
      error => ({ source: 'web-error' as const, error }),
    )
    const deviceResult = ask(device.signal).then(
      value => ({ source: 'hardware' as const, value }),
      error => ({ source: 'hardware-error' as const, error }),
    )

    try {
      const first = await Promise.race([webResult, deviceResult])
      if (first.source === 'hardware') {
        if (originalSignal?.aborted) throw originalSignal.reason ?? new Error('request cancelled')
        if (!visible(key)) {
          result(2)
          const web = await webResult
          if (web.source === 'web') return web.value
          throw web.error
        }
        downstream.abort('answered on HARNESS-WHALE')
        if (waitForComputer) await webResult
        else await Promise.race([webResult, new Promise(resolve => setTimeout(resolve, 1500))])
        // The computer may take time to clean up. Authorization can disappear
        // during that await, even though the device answer arrived first.
        if (originalSignal?.aborted || !visible(key)) {
          result(2)
          throw originalSignal?.reason ?? new Error('session no longer visible')
        }
        result(0, accepted(first.value))
        return first.value
      }
      if (first.source === 'web') {
        result(1)
        device.abort('answered on computer')
        return first.value
      }
      if (first.source === 'hardware-error') {
        const web = await webResult
        if (web.source === 'web') return web.value
        throw web.error
      }
      result(2)
      device.abort('computer question failed')
      throw first.error
    } finally {
      originalSignal?.removeEventListener('abort', abortBoth)
      device.abort('question settled')
    }
  }

  ctx.on('tools/execute', async (exec: any, next: () => Promise<any>) => {
    if (!isQuestionTool(String(exec?.name || ''))) return next()
    const questions = hardwareQuestions(exec?.arguments)
    if (!observeSession(exec.agent?.session) || exec.signal.aborted) return next()
    if (!questions || questions.length !== 1) {
      const raw = exec.arguments?.questions
      emit(sessionKey(exec.agent.session), {state:'waiting', hint: Array.isArray(raw) && raw.some((q:any)=>q?.multi_select) ? 3 : 4})
      return next()
    }
    const key = sessionKey(exec.agent.session)
    const row = states.get(key)
    if (key === 'default' || !row?.runId) return next()
    const item = questions[0]
    const labels = item.options!.map(o => o.label)
    const textFits = (text: unknown, max: number) => typeof text === 'string' &&
      text.length > 0 && Buffer.byteLength(text, 'utf8') <= max && !/[\x00-\x1f\x7f]/.test(text)
    if (!textFits(item.question,512) || labels.length>64 || new Set(labels).size!==labels.length ||
        labels.some(label=>!textFits(label,256))) {
      emit(key,{state:'waiting',hint:5})
      return next()
    }
    const id = randomUUID()
    return raceDevice(id, exec, next,
      signal => askOnHardware(questions, signal, id, key, row.runId),
      value => labels.indexOf(value.value.answers[0].selected[0]), key, true)
  }, { global: true })

  // Permission prompts ("allow this command / file change?") answered on the device.
  // Allow maps to the only grant, allowed-once; deny maps to rejected.
  // Prepend to wrap an existing computer answerer instead of waiting behind it.
  ctx.on('approval/request', async (req: any, next: () => Promise<string>) => {
    const session = req?.agent?.session
    if (!observeSession(session) || req?.signal?.aborted) return next()
    const key = sessionKey(session)
    const row = states.get(key)
    if (key === 'default' || !row?.runId) return next()
    const reasons = approvalReasons(req)
    const prompt = approvalPrompt(String(req?.toolName || ''), callArguments(session, req?.callId),
      reasons ?? undefined)
    if (!prompt) {
      emit(key, {state:'waiting', hint:5})
      return next()
    }
    const id = randomUUID()
    return raceDevice(id, req, next,
      async signal => {
        const [choice] = await askDevice(prompt, APPROVAL_OPTIONS, 'approval', signal, id, key, row.runId)
        return choice === 0 ? 'allowed-once' : 'rejected'
      },
      outcome => outcome === 'allowed-once' ? 0 : 1, key, false)
  }, { global: true, prepend: true })

  ctx.commands.register({
    name: 'whale',
    description: 'show the Harness Whale companion hardware link',
    handler: () => {
      const now = Date.now()
      const bridgeOnline = hardware.receivedAt > 0 && now - hardware.receivedAt < 7000
      const linked = bridgeOnline && hardware.connected
      const agent = states.get(activeKey) || states.get('agent')
      const lastSeen = hardware.receivedAt > 0
        ? `${Math.max(0, Math.floor((now - hardware.receivedAt) / 1000))}s ago`
        : 'never'
      return {
        kind: linked ? 'success' : 'error',
        text: [
          'Harness Whale companion',
          `Hardware: ${linked ? 'connected' : 'disconnected'}`,
          `Bridge: ${bridgeOnline ? hardware.state : 'offline'}`,
          ...(bridgeFailure ? [`Bridge start failure: ${bridgeFailure}`] : []),
          `Device: ${hardware.device}`,
          `Last heartbeat: ${lastSeen}`,
          `Packets: ${hardware.packetsSent ?? 0}`,
          ...(bridgeOnline && linked && hardware.frame !== undefined
            ? [`BLE frame: ${hardware.frame} B (${hardware.frame > 20
              ? 'protocol 2.3 large frames' : 'protocol 2.2 compatible'}${
              hardware.mtu ? `, MTU ${hardware.mtu}` : ''})`]
            : []),
          `Agent mirror: ${agent?.state || 'idle'} / ${agent?.tool || 'none'}`,
          `Task title: ${agent?.title || 'waiting for title'}`,
          ...(bridgeOnline && hardware.disconnects !== undefined
            ? [`Disconnects: ${hardware.disconnects}`]
            : []),
          ...(bridgeOnline && hardware.lastDisconnect
            ? [`Last disconnect: ${hardware.lastDisconnect}${
              hardware.lastDisconnectAt ? ` (${ago(now - hardware.lastDisconnectAt)})` : ''}`]
            : []),
          ...(bridgeOnline && hardware.lastReconnectMs !== undefined
            ? [`Last reconnect took: ${(hardware.lastReconnectMs / 1000).toFixed(1)}s`]
            : []),
          ...(bridgeOnline && !hardware.connected && hardware.retryIn !== undefined
            ? [`Next retry in: ${hardware.retryIn.toFixed(1)}s`]
            : []),
          ...(hardware.error ? [`Last reconnect reason: ${hardware.error}`] : []),
        ].join('\n'),
      }
    },
  })

  const emit = (key: string, patch: Partial<Status> = {}) => {
    if (key === 'agent' || key === 'default' || !visible(key)) return
    const previous = states.get(key)
    const startedAt = patch.newTurn ? Date.now() : previous?.startedAt
    const status: Status & { startedAt?: number } = {
      v: 1, sessionId: key, runId: patch.newTurn || !previous ? randomUUID() : previous.runId,
      state: previous?.state || 'idle',
      tool: previous?.tool || 'none',
      elapsed: startedAt ? Math.max(0, Math.floor((Date.now() - startedAt) / 1000)) : (previous?.elapsed || 0),
      todoDone: previous?.todoDone || 0,
      todoTotal: previous?.todoTotal || 0,
      hint: patch.state ? 0 : previous?.hint,
      title: previous?.title || '',
      sentAt: stamp(),
      ...patch,
      startedAt,
    }
    if (patch.newTurn) {
      status.todoDone = 0
      status.todoTotal = 0
    }
    if (['done','error','stopped'].includes(previous?.state || '') && !patch.newTurn && (!patch.state || patch.state === previous?.state)) status.elapsed = previous!.elapsed
    states.set(key, status)
    const { startedAt: _private, ...publicStatus } = status
    socket.send(Buffer.from(JSON.stringify({...publicStatus,visibleAt:status.sentAt,replyId:replies.get(key)?.id,replyPreview:replies.get(key)?.preview})), PORT, HOST)
  }

  const addLive = (session:any) => {
    if (!observeSession(session)) return
    const key=sessionKey(session)
    if (states.has(key)) return
    activeKey=key
    emit(key,{title:latestSessionTitle(ctx,session),newTurn:false})
  }
  const checkStorage = async (key:string) => {
    if (checking.has(key) || !ctx.sessions || !ctx.sessionPersistence || ctx.sessions.get(key)) return
    checking.add(key)
    try {
      // Disposing a live handle is not proof of deletion. A storage error is not absence.
      const stored=await ctx.sessionPersistence.stat(key)
      if (stopped || stored !== undefined || ctx.sessions.get(key) || !states.has(key)) return
      removed.add(key)
      publishVisibility([mark(key,true)])
      states.delete(key); observed.delete(key); replies.delete(key); candidates.delete(key)
    } catch {
      // Retain the last known row and retry on the next heartbeat.
    } finally { checking.delete(key) }
  }
  const discoverLive = () => {
    for (const session of ctx.sessions?.list() ?? []) {
      if (!states.has(sessionKey(session))) addLive(session)
    }
  }
  syncVisibility()
  discoverLive()
  const heartbeat = setInterval(() => {
    syncVisibility()
    discoverLive()
    for (const key of states.keys()) { void checkStorage(key); emit(key) }
    for (const [id, item] of results) {
      if (item.expires <= Date.now()) results.delete(id)
      else socket.send(item.data, PORT, HOST)
    }
  }, 3000)

  ctx.on('session/created', addLive, {global:true})
  ctx.on('session/disposed', (session:any) => {
    if (!isMainSession(session)) return
    disposed.add(session)
    void checkStorage(sessionKey(session))
  }, {global:true})

  ctx.on('agent/status', (payload: any) => {
    const raw = String(payload?.status || payload?.state || payload || '').toLowerCase()
    const agentSession = payload?.agent?.session
    if (!observeSession(agentSession)) return
    const key = sessionKey(agentSession)
    activeKey = key
    if (raw === 'running') {
      const previous = states.get(key)
      const beginsTask = !previous?.startedAt || ['idle', 'done', 'error', 'stopped', 'offline'].includes(
        previous.state,
      )
      emit(key, { state: 'thinking', tool: 'none', newTurn: beginsTask })
    }
    if (raw === 'idle') {
      const previousState = states.get(key)?.state || ''
      if (!['waiting', 'question', 'done', 'error', 'stopped'].includes(previousState)) {
        emit(key, { state: 'idle', tool: 'none' })
      }
    }
  }, { global: true })

  ctx.on('session/event', (session: any, event: any) => {
    if (!observeSession(session)) return
    const key = sessionKey(session)
    activeKey = key
    const type = eventType(event)
    const body = eventBody(event)
    const observedTitle = latestSessionTitle(
      ctx,
      session,
      type === 'session/title' ? body?.title : undefined,
    )
    if (observedTitle && observedTitle !== states.get(key)?.title) {
      emit(key, { title: observedTitle, newTurn: false })
    }
    switch (type) {
      case 'assistant/message':
        candidates.set(key,{turn:body.turn,text:assistantText(body)})
        break
      case 'turn/start':
        candidates.delete(key)
        emit(key, {
          state: 'thinking', tool: 'none', elapsed: 0, newTurn: true,
          title: observedTitle || states.get(key)?.title || '',
        })
        break
      case 'session/title':
        emit(key, { title: observedTitle, newTurn: false })
        break
      case 'tool/call': {
        const tool = toolName(body)
        emit(key, {
          state: isQuestionTool(tool) ? 'question' : 'tool',
          tool: isQuestionTool(tool) ? 'none' : tool,
          newTurn: false,
        })
        break
      }
      case 'tool/result':
        emit(key, { state: body?.error ? 'tool' : 'thinking', tool: body?.error ? 'recover' : 'none', hint: body?.error ? 1 : 0, newTurn: false })
        break
      case 'todo/write': {
        const todos = Array.isArray(body?.todos) ? body.todos : []
        const done = todos.filter((todo: any) => ['done', 'completed', 'complete'].includes(
          String(todo?.status || '').toLowerCase())).length
        emit(key, { todoDone: done, todoTotal: todos.length, newTurn: false })
        break
      }
      case 'approval/asked':
        emit(key, { state: 'waiting', tool: 'none', hint: 2, newTurn: false })
        break
      case 'approval/decided':
        emit(key, { state: 'thinking', tool: 'none', newTurn: false })
        break
      case 'question/requested':
        emit(key, { state: 'question', tool: 'none', newTurn: false })
        break
      case 'question/answered':
      case 'question/resolved':
      case 'question/cancelled':
        emit(key, { state: 'thinking', tool: 'none', newTurn: false })
        break
      case 'turn/end': {
        const candidate=candidates.get(key)
        if (body.reason?.kind==='completed' && candidate && candidate.turn===body.turn) saveReply(key,candidate.text)
        candidates.delete(key)
        const reason = body?.reason
        const kind = reasonKind(reason)
        const cancelKind = reasonKind(reason?.reason)
        const stopped = (kind === 'aborted' && cancelKind === 'user') ||
          ['cancelled', 'canceled', 'stopped'].includes(kind)
        const failed = Boolean(body?.error) ||
          ['error', 'blocked', 'interrupted', 'max-tokens'].includes(kind) ||
          (kind === 'aborted' && !stopped)
        const finalState = stopped ? 'stopped' : failed ? 'error' : 'done'
        emit(key, { state: finalState, tool: 'none', newTurn: false })
        break
      }
    }
  }, { global: true })

  ctx.effect(() => () => {
    stopped = true
    voiceService.close()
    clearInterval(heartbeat)
    clearInterval(bridgeGuard)
    clearTimeout(bridgeStartTimer)
    results.clear()
    if (bridgeChild && !bridgeChild.killed) bridgeChild.kill()
    socket.close()
    telemetrySocket.close()
  })
}
