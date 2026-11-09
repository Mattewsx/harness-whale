import { createServer, type IncomingMessage } from 'node:http'
import { randomUUID, timingSafeEqual } from 'node:crypto'
import { win32 } from 'node:path'

// Private, process-owned bridge transport. No credentials or utterances are logged.
const MAX_WAVE = 60 * 32000 + 44
const MAX_TEXT = 1800
const uuid = /^[0-9a-f]{32}$/

// Post-recognition terminology correction, not acoustic hotword support.
// Correct only an explicit product command. In particular, never expand "deep"
// or rewrite ordinary English, quoted literals, URLs, or identifiers.
export function normalizeVoiceTerms(text: string): string {
  return text.replace(/(?:https?:\/\/\S+|`[^`]*`|"[^"\n]*"|'[^'\n]*'|“[^”]*”|‘[^’]*’|「[^」]*」)|(?:打开|启动|运行|使用|安装|更新|升级|重启|连接|切换到)(?:一下)?[ \t]*(?:deep[ \t]+(?:seek|sick)|deepseek)(?!\.[a-z0-9])(?=$|[ \t，。！？、：；,.!?;:]|[\u3400-\u9fff])/giu,
    match => {
      if (!/^(?:打开|启动|运行|使用|安装|更新|升级|重启|连接|切换到)/u.test(match)) return match
      return match.replace(/deep[ \t]+(?:seek|sick)|deepseek/iu, 'DeepSeek')
    })
}

export function voiceMessageId(id: string) { return `whale-voice-${id}` }
export function acceptedInSession(session: any, id: string): boolean {
  const key = voiceMessageId(id)
  return (session.ownEvents?.() || []).some((event: any) => {
    const b = event.data ?? event.payload ?? event
    return (event.type === 'agent/inbox/spliced' && Array.isArray(b.inserted) &&
      b.inserted.some((m: any) => m?.id === key)) ||
      (event.type === 'user/message' && (b.message?.id === key || b.id === key))
  })
}

// Folder label shown on the device before a spoken task is confirmed. Bounded
// in UTF-8 bytes without splitting a character; the bridge sanitises it again.
export function targetLabel(cwd: string): string {
  const name = win32.basename(cwd.replace(/[\\/]+$/, '')) || cwd
  let out = ''
  for (const ch of name) {
    if (Buffer.byteLength(out + ch) > 48) break
    out += ch
  }
  return out
}

export function validWave(b: Buffer): boolean {
  return b.length >= 46 && b.length <= MAX_WAVE && b.toString('ascii', 0, 4) === 'RIFF' &&
    b.toString('ascii', 8, 16) === 'WAVEfmt ' && b.readUInt32LE(16) === 16 &&
    b.readUInt16LE(20) === 1 && b.readUInt16LE(22) === 1 && b.readUInt32LE(24) === 16000 &&
    b.readUInt32LE(28) === 32000 && b.readUInt16LE(32) === 2 && b.readUInt16LE(34) === 16 &&
    b.toString('ascii', 36, 40) === 'data' && b.readUInt32LE(4) === b.length - 8 &&
    b.readUInt32LE(40) === b.length - 44 && (b.length - 44) % 2 === 0
}

async function body(req: IncomingMessage, limit: number): Promise<Buffer> {
  const chunks: Buffer[] = []; let bytes = 0
  for await (const chunk of req) {
    bytes += chunk.length
    if (bytes > limit) throw new Error('request too large')
    chunks.push(chunk)
  }
  return Buffer.concat(chunks)
}

export function createVoiceService(ctx: any, visible: (id: string) => boolean,
  main: (session: any) => boolean, waiting: (id: string) => boolean,
  recent: () => string | undefined = () => undefined) {
  const token = randomUUID(), life = new AbortController()
  const receipts = new Map<string, { sessionId: string, text: string, status: string }>()
  // Spoken new tasks: one receipt per device recording, so a repeated or
  // retried confirmation can never open a second session.
  const created = new Map<string, { cwd: string, text: string, status: string, sessionId?: string }>()
  const pending = new Map<string, Promise<any>>()
  const failedActivations = new Set<string>()
  const get = (name: string) => { try { return ctx.get?.(name) ?? ctx[name] } catch { return undefined } }
  const speech = () => get('speechToText')
  function ready() {
    const api = speech()
    const local = api?.snapshot?.().providers?.find((p: any) => p.id === 'sensevoice-local')
    // SenseVoice accepts recordings in standby: resources are verified and the
    // local worker starts lazily on transcribe(). Do not require it to stay hot.
    const phase = local?.preparation?.phase
    return Boolean((phase === 'ready' || phase === 'standby') && get('agents'))
  }
  async function lookup(id: string): Promise<{ agent?: any, reason?: string }> {
    const agents = get('agents')
    if (!visible(id)) return { reason: 'hidden' }
    if (!agents) return { reason: 'agents_missing' }
    let agent = agents.get(id)
    if (agent) failedActivations.delete(id)
    else {
      // Only an explicitly confirmed submit may activate the desktop's session.
      // Never repeat a failed activation automatically: broken host presets can
      // roll back publication and remove the desktop row until it is refreshed.
      if (failedActivations.has(id)) return { reason: 'open_on_computer' }
      const controller = get('sessionController')
      if (typeof controller?.resolveAgent !== 'function') return { reason: 'controller_missing' }
      try {
        const found = await controller.resolveAgent(id)
        agent = found?.agent
        if (!agent) throw new Error('host_activation_failed')
      } catch {
        failedActivations.add(id)
        // Fail closed for further cold sessions if the bounded guard is full.
        return { reason: 'host_activation_failed' }
      }
    }
    if ((agent.session?.id ?? agent.session?.header?.id) !== id) return { reason: 'identity_mismatch' }
    if (!main(agent.session)) return { reason: 'not_main' }
    if (!visible(id)) return { reason: 'hidden' }
    return { agent }
  }
  async function submit(req: any, query: boolean) {
    if (!uuid.test(req.id) || typeof req.sessionId !== 'string' || req.sessionId.length > 256)
      return { status: 'invalid' }
    const previous = receipts.get(req.id)
    if (previous && previous.sessionId !== req.sessionId) return { status: 'invalid' }
    if (previous) {
      if (!query && previous.text !== req.text) return { status: 'invalid' }
      return { status: previous.status }
    }
    if (!visible(req.sessionId)) return { status: 'archived' }
    // Receipt queries are strictly read-only, including after a plugin restart.
    // A missing in-memory session is not evidence that a message was never sent.
    if (query) {
      const session = get('agents')?.get(req.sessionId)?.session ?? get('sessions')?.get(req.sessionId)
      if (!session) return { status: 'unknown' }
      if ((session.id ?? session.header?.id) !== req.sessionId || !main(session))
        return { status: 'unavailable', reason: 'identity_mismatch' }
      return { status: acceptedInSession(session, req.id) ? 'accepted' : 'not_sent' }
    }
    if (waiting(req.sessionId)) return { status: 'question' }
    if (typeof req.text !== 'string' || !req.text.trim() || Buffer.byteLength(req.text) > MAX_TEXT ||
        /[\x00-\x08\x0b\x0c\x0e-\x1f\x7f]/.test(req.text)) return { status: 'invalid' }
    if (failedActivations.size >= 128 && !get('agents')?.get(req.sessionId))
      return { status: 'unavailable', reason: 'open_on_computer' }
    const target = await lookup(req.sessionId)
    const agent = target.agent
    if (!agent) return { status: 'unavailable', reason: target.reason }
    // Search durable inbox events as well as still-pending work. Native Inbox only
    // deduplicates pending IDs and is insufficient after a message was consumed.
    if (acceptedInSession(agent.session, req.id)) return { status: 'accepted' }
    if (waiting(req.sessionId)) return { status: 'question' }
    if (!visible(req.sessionId)) return { status: 'archived' }
    const queued = agent.status === 'running'
    try {
      agent.followup({ id: voiceMessageId(req.id), role: 'user',
        content: [{ type: 'text', text: req.text }], source: { kind: 'user' } })
    } catch {
      return { status: acceptedInSession(agent.session, req.id) ? 'accepted' : 'unknown' }
    }
    const status = queued ? 'queued' : 'accepted'
    receipts.set(req.id, { sessionId: req.sessionId, text: req.text, status })
    while (receipts.size > 128) receipts.delete(receipts.keys().next().value!)
    return { status }
  }

  // ---- Spoken new task: open a fresh top-level session in a known folder ----
  const sessionOf = (id: string) => get('agents')?.get?.(id)?.session ?? get('sessions')?.get?.(id)
  const cwdOf = (session: any): string | undefined => {
    const cwd = session?.header?.cwd ?? session?.cwd
    return typeof cwd === 'string' && cwd.length > 0 && cwd.length <= 1024 ? cwd : undefined
  }
  const usable = (id: unknown): string | undefined => {
    if (typeof id !== 'string' || !id || id.length > 256 || !visible(id)) return undefined
    const session = sessionOf(id)
    return session && main(session) ? cwdOf(session) : undefined
  }
  // Folders of the visible main sessions this host knows. A new session may only
  // open in one of these, never in an arbitrary path supplied over loopback.
  function knownFolders(): string[] {
    const found: string[] = []
    const add = (session: any) => {
      const id = session?.header?.id ?? session?.id
      const cwd = typeof id === 'string' && visible(id) && main(session) ? cwdOf(session) : undefined
      if (cwd && !found.includes(cwd)) found.push(cwd)
    }
    try { for (const agent of get('agents')?.roots?.() ?? []) add(agent?.session) } catch {}
    try { for (const session of get('sessions')?.list?.() ?? []) add(session) } catch {}
    return found
  }
  // Default folder: the session the device was showing, else the most recently
  // active one, else any visible session. Shown on the device before sending.
  function target(req: any) {
    const cwd = usable(req?.sessionId) ?? usable(recent()) ?? knownFolders()[0]
    return cwd ? { status: 'ok', cwd, label: targetLabel(cwd) } : { status: 'no_target' }
  }
  function modelOptions() {
    const selection = get('agentDefaultModel')?.currentSelection?.()
    const { provider, model, reasoningEffort } = selection ?? {}
    if (typeof provider !== 'string' || !provider || typeof model !== 'string' || !model) return undefined
    return { provider, model, ...(typeof reasoningEffort === 'string' && reasoningEffort ? { reasoningEffort } : {}) }
  }
  async function createTask(req: any, query: boolean) {
    if (!uuid.test(req.id)) return { status: 'invalid' }
    const previous = created.get(req.id)
    if (previous) {
      if (!query && (previous.text !== req.text || previous.cwd !== req.cwd)) return { status: 'invalid' }
      return { status: previous.status }
    }
    // After a plugin restart nothing proves whether the session was opened.
    if (query) return { status: 'unknown' }
    if (typeof req.cwd !== 'string' || typeof req.text !== 'string' || !req.text.trim() ||
        Buffer.byteLength(req.text) > MAX_TEXT || /[\x00-\x08\x0b\x0c\x0e-\x1f\x7f]/.test(req.text))
      return { status: 'invalid' }
    if (!knownFolders().includes(req.cwd)) return { status: 'no_target' }
    const agents = get('agents')
    if (typeof agents?.create !== 'function') return { status: 'unavailable', reason: 'create_missing' }
    // Resolve everything before creating: a session without a model or preset
    // would sit idle, and a created session is never disposed again.
    let agentOptions, presetId: string | undefined, setup: any
    try {
      agentOptions = modelOptions()
      const presets = get('agentPresets')
      if (presets) {
        presetId = (await presets.resolve(undefined))?.id
        if (typeof presetId !== 'string') return { status: 'unavailable', reason: 'preset_missing' }
        setup = async (agentCtx: any) => { await presets.mount(agentCtx, presetId) }
      }
    } catch { return { status: 'unavailable', reason: 'preset_failed' } }
    if (!agentOptions) return { status: 'unavailable', reason: 'model_missing' }
    const record: { cwd: string, text: string, status: string, sessionId?: string } =
      { cwd: req.cwd, text: req.text, status: 'unknown' }
    created.set(req.id, record)
    while (created.size > 128) created.delete(created.keys().next().value!)
    let agent: any
    try {
      const handle = await agents.create({ sessionId: randomUUID(), agentOptions, setup,
        meta: { cwd: req.cwd, ...(presetId === undefined ? {} : { agentPreset: presetId }) } })
      agent = handle?.agent
    } catch {
      created.delete(req.id)
      return { status: 'not_sent' }
    }
    if (!agent) { created.delete(req.id); return { status: 'not_sent' } }
    record.sessionId = String(agent.id ?? agent.session?.header?.id ?? '')
    // Group it with the folder's workspace so it shows on the device and desktop.
    try {
      const workspace = await get('workspaceRegistry')?.resolveByPath?.(req.cwd)
      if (workspace && record.sessionId) await workspace.attachSession(record.sessionId)
    } catch { /* the session stays ungrouped; it still runs */ }
    try {
      agent.followup({ id: voiceMessageId(req.id), role: 'user',
        content: [{ type: 'text', text: req.text }], source: { kind: 'user' } })
      record.status = 'accepted'
    } catch { /* created but the first message is uncertain: keep 'unknown' */ }
    return { status: record.status }
  }
  const server = createServer(async (req, res) => {
    const supplied = Buffer.from(req.headers.authorization ?? '')
    const expected = Buffer.from(`Bearer ${token}`)
    const send = (status: number, result: any) => {
      if (!res.destroyed) { res.writeHead(status, { 'content-type': 'application/json' }); res.end(JSON.stringify(result)) }
    }
    if (supplied.length !== expected.length || !timingSafeEqual(supplied, expected)) {
      send(401, { status: 'denied' }); return
    }
    try {
      if (req.method === 'GET' && req.url === '/ready') { send(200, { ready: ready() }); return }
      if (req.method !== 'POST') { send(404, {}); return }
      if (req.url === '/transcribe') {
        if (!ready()) { send(503, { status: 'not_ready' }); return }
        const wave = await body(req, MAX_WAVE)
        if (!validWave(wave)) { send(400, { status: 'invalid_audio' }); return }
        const disconnected = new AbortController()
        res.once('close', () => { if (!res.writableEnded) disconnected.abort() })
        const signal = AbortSignal.any([life.signal, disconnected.signal, AbortSignal.timeout(15000)])
        const api = speech()
        const spec = api.resolve({ audio: new Uint8Array(wave), providerId: 'sensevoice-local', language: 'zh' })
        const result = await api.transcribe(spec, signal)
        const text = typeof result === 'string' ? result : result?.text
        if (typeof text !== 'string' || Buffer.byteLength(text) > MAX_TEXT) { send(422, { status: 'too_long' }); return }
        // Preview and subsequent confirmed delivery share the exact same text.
        send(200, { text: normalizeVoiceTerms(text.trim()) }); return
      }
      if (req.url === '/target') {
        send(200, target(JSON.parse((await body(req, 1024)).toString('utf8')))); return
      }
      const task = req.url === '/create' || req.url === '/create-query'
      if (!task && req.url !== '/submit' && req.url !== '/query') { send(404, {}); return }
      const data = JSON.parse((await body(req, 8192)).toString('utf8'))
      if (!uuid.test(data.id)) { send(400, { status: 'invalid' }); return }
      const key = data.id
      const action = (pending.get(key) ?? Promise.resolve()).catch(() => {}).then(() =>
        task ? createTask(data, req.url === '/create-query') : submit(data, req.url === '/query'))
      pending.set(key, action)
      try { send(200, await action) } finally { if (pending.get(key) === action) pending.delete(key) }
    } catch { send(503, { status: 'unavailable' }) }
  })
  server.requestTimeout = 20000; server.headersTimeout = 5000
  server.on('error', () => { /* optional voice must not crash the host */ })
  server.listen(0, '127.0.0.1')
  server.unref()
  return {
    environment() {
      const addr = server.address()
      return addr && typeof addr !== 'string' ? {
        HARNESS_WHALE_VOICE_PORT: String(addr.port), HARNESS_WHALE_VOICE_TOKEN: token,
      } : {}
    },
    close() { life.abort(); server.close(); server.closeAllConnections(); receipts.clear(); created.clear() },
  }
}
