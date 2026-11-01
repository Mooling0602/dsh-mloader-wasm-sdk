/**
 * Language-neutral conformance suite for the dsh-mloader WebAssembly ABI.
 *
 * Every language SDK in this repository ships an example plugin that declares
 * the same three tools and the same event subscription. The suite instantiates
 * the compiled module and drives it the way the dsh-mloader worker does:
 * instantiate, initialize a WASI reactor through _initialize when present,
 * check the ABI version, allocate a request buffer, write UTF-8 JSON, call
 * mloader_call, read the result at mloader_result_ptr and free the request
 * buffer.
 *
 * Usage: node conformance/abi.mjs --wasm <path to .wasm>
 *
 * The suite needs no running dsh and no installed plugin.
 */

import { existsSync } from 'node:fs'
import { readFile } from 'node:fs/promises'

const usage = 'usage: node conformance/abi.mjs --wasm <path to .wasm>'
const options = process.argv.slice(2)
const wasmIndex = options.indexOf('--wasm')
if (wasmIndex === -1 || wasmIndex === options.length - 1) {
  console.error(usage)
  process.exit(1)
}
const WASM = options[wasmIndex + 1]
if (!existsSync(WASM)) {
  console.error('error: the module was not found: ' + WASM)
  console.error(usage)
  process.exit(1)
}

const failures = []
let passed = 0

function check(name, condition, detail = '') {
  if (condition) {
    passed += 1
    console.log('  ok: ' + name)
  } else {
    failures.push(name + (detail ? ': ' + detail : ''))
    console.log('  FAIL: ' + name + (detail ? ': ' + detail : ''))
  }
}

const section = (title) => console.log('\n' + title)

const encoder = new TextEncoder()
const decoder = new TextDecoder()

/**
 * Send one raw request through the ABI and return the parsed reply.
 *
 * @param exports - Instance exports.
 * @param text - UTF-8 request text.
 * @returns Parsed reply object.
 */
function invokeRaw(exports, text) {
  const request = encoder.encode(text)
  const requestPtr = exports.mloader_alloc(request.length)
  if (requestPtr === 0) throw new Error('mloader_alloc returned a null pointer')
  new Uint8Array(exports.memory.buffer, requestPtr, request.length).set(request)

  const resultLength = exports.mloader_call(requestPtr, request.length)
  let replyText = ''
  if (resultLength > 0) {
    const resultPtr = exports.mloader_result_ptr()
    replyText = decoder.decode(new Uint8Array(exports.memory.buffer, resultPtr, resultLength))
  }
  exports.mloader_free(requestPtr, request.length)
  return JSON.parse(replyText)
}

/** Send one structured request and return the parsed reply. */
function invoke(exports, op, payload) {
  return invokeRaw(exports, JSON.stringify({ op, payload }))
}

async function main() {
  const bytes = await readFile(WASM)
  const module = await WebAssembly.compile(bytes)
  const imports = WebAssembly.Module.imports(module)

  section('1. Host imports stay inside the WASI preview1 namespace')
  const foreign = imports.filter((entry) => entry.module !== 'wasi_snapshot_preview1')
  check(
    'no imports outside wasi_snapshot_preview1',
    foreign.length === 0,
    imports.map((entry) => entry.module + '.' + entry.name).join(','),
  )

  let wasi = null
  let instance
  if (imports.length > 0) {
    const { WASI } = await import('node:wasi')
    wasi = new WASI({ version: 'preview1', args: [], env: {}, preopens: {}, returnOnExit: true })
    instance = await WebAssembly.instantiate(module, { wasi_snapshot_preview1: wasi.wasiImport })
  } else {
    instance = await WebAssembly.instantiate(module, {})
  }
  const exports = instance.exports

  section('2. The six core ABI exports are present')
  check('exports memory', exports.memory instanceof WebAssembly.Memory)
  const functions = ['mloader_alloc', 'mloader_free', 'mloader_call', 'mloader_result_ptr', 'mloader_abi_version']
  for (const name of functions) check('exports ' + name, typeof exports[name] === 'function')

  section('3. The startup shape runs before the version check')
  if (wasi !== null && typeof exports._initialize === 'function') {
    // A WASI reactor traps when an export runs before _initialize, so the
    // runtime is initialized before the version export is called.
    wasi.initialize(instance)
    check('the WASI reactor is initialized', true)
  } else if (wasi !== null && typeof exports._start === 'function') {
    // A WASI command module runs _start once and may call proc_exit.
    wasi.start(instance)
    check('the WASI command module is started', true)
  } else {
    check('the module needs no startup call', true)
  }
  check('reports ABI version 1', exports.mloader_abi_version() === 1, String(exports.mloader_abi_version()))
  check('result buffer starts empty', exports.mloader_result_ptr() === 0)
  check('a zero length allocation returns 0', exports.mloader_alloc(0) === 0)

  section('4. apply returns the declaration')
  const applied = invoke(exports, 'apply', { inject: [] })
  check('apply succeeds', applied.ok === true, JSON.stringify(applied))
  const tools = applied.result?.tools ?? []
  const names = tools.map((tool) => tool.name)
  check('declares three tools', tools.length === 3, names.join(','))
  check('declares sdk_add', names.includes('sdk_add'))
  check('declares sdk_upper', names.includes('sdk_upper'))
  check('declares sdk_check', names.includes('sdk_check'))
  check('describes sdk_add', tools.find((tool) => tool.name === 'sdk_add')?.description === 'Add two numbers')
  check(
    'sdk_add requires a and b',
    JSON.stringify(tools.find((tool) => tool.name === 'sdk_add')?.parameters?.required) === '["a","b"]',
  )
  check('subscribes to tools/execute', JSON.stringify(applied.result?.events) === '["tools/execute"]')

  section('5. A tool call returns its result')
  const sum = invoke(exports, 'tool.call', { tool: 'sdk_add', args: { a: 19, b: 23 } })
  check('sdk_add returns 42', sum.ok === true && sum.result?.sum === 42, JSON.stringify(sum))
  const upper = invoke(exports, 'tool.call', { tool: 'sdk_upper', args: { s: 'hello wasm' } })
  check('sdk_upper upper-cases', upper.ok === true && upper.result?.upper === 'HELLO WASM', JSON.stringify(upper))

  section('6. A handler failure becomes a failed reply')
  const rejected = invoke(exports, 'tool.call', { tool: 'sdk_check', args: { value: -1 } })
  check('the call fails', rejected.ok === false, JSON.stringify(rejected))
  check('the message is the handler error', rejected.error?.message === 'value must be non-negative', rejected.error?.message)

  section('7. Malformed calls fail without a trap')
  const missing = invoke(exports, 'tool.call', { tool: 'sdk_upper', args: {} })
  check(
    'a missing argument fails',
    missing.ok === false && /missing required argument "s"/.test(missing.error?.message ?? ''),
    JSON.stringify(missing),
  )
  const unknown = invoke(exports, 'tool.call', { tool: 'nope', args: {} })
  check(
    'an unknown tool fails',
    unknown.ok === false && /unknown tool "nope"/.test(unknown.error?.message ?? ''),
    JSON.stringify(unknown),
  )
  const unsupported = invoke(exports, 'boot', {})
  check(
    'an unsupported operation fails',
    unsupported.ok === false && /unsupported op/.test(unsupported.error?.message ?? ''),
    JSON.stringify(unsupported),
  )
  const invalid = invokeRaw(exports, 'not json')
  check(
    'invalid JSON fails',
    invalid.ok === false && /invalid request/.test(invalid.error?.message ?? ''),
    JSON.stringify(invalid),
  )

  section('8. Event delivery is acknowledged')
  const event = invoke(exports, 'event', { event: 'tools/execute', args: [{ name: 'sdk_add' }] })
  check('the event succeeds', event.ok === true, JSON.stringify(event))
  check('the handler returns null', event.result === null, JSON.stringify(event.result))

  section('9. The result buffer is reusable')
  const first = invoke(exports, 'tool.call', { tool: 'sdk_add', args: { a: 1, b: 2 } })
  const second = invoke(exports, 'tool.call', { tool: 'sdk_add', args: { a: 3, b: 4 } })
  check(
    'consecutive calls return their own result',
    first.result?.sum === 3 && second.result?.sum === 7,
    first.result?.sum + ', ' + second.result?.sum,
  )

  if (failures.length === 0) {
    console.log('\nAll passed: ' + passed + ' assertions')
  } else {
    console.log('\n' + failures.length + ' failed out of ' + (passed + failures.length))
    for (const failure of failures) console.log('   - ' + failure)
    process.exitCode = 1
  }
}

main().catch((error) => {
  console.error('\nTest script failed:', error)
  process.exit(1)
})
