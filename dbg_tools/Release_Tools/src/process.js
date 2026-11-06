'use strict'

const fs = require('fs')
const os = require('os')
const path = require('path')
const { spawnSync } = require('child_process')

// 当前 Windows/Node/Git 组合的管道捕获会偶发空输出。先落临时文件，
// 等待进程退出再读取，同时把 Git 非零退出传播给上层。只清理本调用创建的文件。
function run(command, args, cwd, timeout = 120000, allowed = [0], onProgress) {
  const directory = fs.mkdtempSync(path.join(process.env.ECW_RELEASE_TMP || os.tmpdir(), '.ecw-release-process-'))
  const stdoutFile = path.join(directory, 'stdout')
  const stderrFile = path.join(directory, 'stderr')
  const stdoutFd = fs.openSync(stdoutFile, 'wx')
  const stderrFd = fs.openSync(stderrFile, 'wx')
  let result
  try {
    result = spawnSync(command, args, {
      cwd, timeout, windowsHide: true, stdio: ['ignore', stdoutFd, stderrFd],
      env: { ...process.env, GIT_TERMINAL_PROMPT: '0', LC_ALL: 'C', GIT_NO_REPLACE_OBJECTS: '1' }
    })
  } finally {
    fs.closeSync(stdoutFd)
    fs.closeSync(stderrFd)
  }
  try {
    for (const file of [stdoutFile, stderrFile]) {
      if (fs.statSync(file).size > 64 * 1024 * 1024) throw new Error(command + ': 单次输出超过 64 MiB 限额')
    }
    const stdout = fs.readFileSync(stdoutFile, 'utf8')
    const stderr = fs.readFileSync(stderrFile, 'utf8')
    if (result.error) throw new Error(command + ': ' + result.error.message)
    if (!allowed.includes(result.status)) throw new Error(`${command} exited ${result.status}: ${stderr.trim()}`)
    if (onProgress) onProgress(Buffer.byteLength(stdout))
    return { stdout, stderr, status: result.status }
  } finally {
    fs.unlinkSync(stdoutFile)
    fs.unlinkSync(stderrFile)
    fs.rmdirSync(directory)
  }
}
module.exports = { run }
