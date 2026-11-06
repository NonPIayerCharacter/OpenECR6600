'use strict'

// Compatibility command: node test/run-tests.js <runtime-root>.
const path = require('path')
const { spawnSync } = require('child_process')
const args = ['-B', path.join(__dirname, 'test_release_notes.py')]
if (process.argv[2]) args.push('--runtime-root', process.argv[2])
const result = spawnSync(process.platform === 'win32' ? 'python' : 'python3', args,
  { stdio: 'inherit', windowsHide: true })
if (result.error) console.error(result.error.message)
process.exitCode = result.error || result.status === null ? 1 : result.status
