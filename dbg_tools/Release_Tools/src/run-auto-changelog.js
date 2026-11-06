'use strict'

const fs = require('fs')
const path = require('path')
const { run } = require('./process')

// 仅在本次生成器进程内适配 utils.cmd；vendor 文件和公共 CLI 参数不变。
// 原实现缺少退出码检查，且当前主机管道会丢 Git 输出。文件捕获使两者均可检查。
const input = JSON.parse(fs.readFileSync(process.argv[2], 'utf8'))
const { runtime, timeout, output, template, config, release } = input
if (!path.isAbsolute(runtime) || !Number.isInteger(timeout) || timeout < 10000) {
  throw new Error('Invalid generator runtime or timeout')
}
if (!/^[a-f0-9]{40}(?:[a-f0-9]{24})?\.\.[a-f0-9]{40}(?:[a-f0-9]{24})?$/.test(release.diff)) {
  throw new Error('Expected frozen full-SHA range')
}
const utils = require(path.join(runtime, 'src', 'utils.js'))
utils.cmd = async (string, onProgress) => {
  const [command, ...args] = string.trim().split(' ')
  if (command !== 'git') throw new Error('Only Git commands are supported by this adapter')
  // 无 remote 是工具支持的正常状态；其他 Git 非零退出必须失败。
  const allowed = args[0] === 'config' && args[1] === '--get' ? [0, 1] : [0]
  return run(command, args, process.cwd(), timeout, allowed, onProgress).stdout
}
// Python 已限制配置键; 再固定范围相关默认值, 不经过 tags/版本排序流程。
async function generate() {
  const issuePattern = new RegExp(config.issuePattern, 'g')
  if (issuePattern.test('')) throw new Error('issuePattern must not match empty text')
  new RegExp(config.breakingPattern)
  const options = { ...config, template, appendGitLog: '', appendGitTag: '', plugins: [],
    commitLimit: false, backfillLimit: false, releaseSummary: false, hideEmptyReleases: false }
  // 上游会隐藏部分普通 Merge/同标题记录；历史筛选涉及的记录仍须进入工程归档。
  // 复用插件扩展点，不修改 vendor，也不重复添加已在 merges/fixes 中表达的提交。
  const preserve = new Set(input.preserveHashes || [])
  let original = []
  options.plugins.push({
    processCommits: commits => { original = commits },
    processReleases: records => {
      for (const record of records) {
        const represented = new Set([
          ...record.commits.map(commit => commit.hash),
          ...record.merges.map(merge => merge.commit.hash),
          ...record.fixes.map(fix => fix.commit.hash)
        ])
        record.commits.push(...original.filter(commit => preserve.has(commit.hash) && !represented.has(commit.hash)))
      }
    }
  })
  const { fetchRemote } = require(path.join(runtime, 'src', 'remote.js'))
  Object.assign(options, await fetchRemote(options))
  const { parseReleases } = require(path.join(runtime, 'src', 'releases.js'))
  const { compileTemplate } = require(path.join(runtime, 'src', 'template.js'))
  const descriptor = { ...release, href: options.getCompareLink(release.from, release.to) }
  const records = await parseReleases([descriptor], options)
  fs.writeFileSync(path.join(output, 'release-data.json'), JSON.stringify(records, null, 2) + '\n', 'utf8')
  fs.writeFileSync(path.join(output, 'CHANGELOG.generated.md'), await compileTemplate(records, options), 'utf8')
}
generate().catch(error => { console.error(error.message); process.exitCode = 1 })
