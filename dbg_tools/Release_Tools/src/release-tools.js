"use strict"

// 保留旧命令行参数, 统一转发 Python, 不再维护第二套生成逻辑。
const fs = require('fs')
const path = require('path')
const { spawnSync } = require('child_process')
function main(argv) {
  const [mode, ...rest] = argv
  if (!['prepare', 'generate'].includes(mode)) throw new Error('Expected prepare or generate')
  const tools = path.resolve(__dirname, '..')
  const base = JSON.parse(fs.readFileSync(path.join(tools, 'tool-baseline.json'), 'utf8'))
  const keys = { '--work-root': '--runtime-root', '--previous-tag': '--from', '--target-tag': '--to',
    '--content': '--content', '--coverage': '--coverage', '--timeout-seconds': '--timeout' }
  const args = [path.join(tools, 'generate_release_notes.py')]
  if (mode === 'prepare') args.push('--prepare-runtime')
  const seen = new Set()
  for (let i = 0; i < rest.length; i += 2) {
    const key = rest[i], value = rest[i + 1]
    if (!value || seen.has(key)) throw new Error('Missing/duplicate argument: ' + key)
    seen.add(key)
    if (key === '--snapshot') {
      if (value !== base.snapshotId) throw new Error('Tool snapshot does not match baseline')
    } else {
      if (!keys[key]) throw new Error('Unknown argument: ' + key)
      args.push(keys[key], value)
    }
  }
  console.error('V1.6: output is now Release_Tools/release; --work-root selects runtime only.')
  const result = spawnSync(process.platform === 'win32' ? 'python' : 'python3', args,
    { stdio: 'inherit', windowsHide: true })
  if (result.error) throw result.error
  return result.status === null ? 1 : result.status
}
if (require.main === module) {
  try { process.exitCode = main(process.argv.slice(2)) } catch (error) {
    console.error(error.message); process.exitCode = 1
  }
}
module.exports = { main }
