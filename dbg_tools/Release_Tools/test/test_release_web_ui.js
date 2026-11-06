'use strict'
// 原生 DOM 行为替身：验证页面事件与数据绑定，不替代真实浏览器视觉验收。
const assert = require('assert')
const fs = require('fs')
const path = require('path')
const vm = require('vm')

class Element {
  constructor(tag) { this.tag = tag; this.children = []; this.events = {}; this._text = ''; this.hidden = false; this.open = false }
  set textContent(value) { this._text = String(value); this.children = [] }
  get textContent() { return this._text + this.children.map(c => c.textContent).join('') }
  set innerHTML(_) { throw new Error('Untrusted HTML insertion') }
  append(...items) { this.children.push(...items) }
  replaceChildren(...items) { this._text = ''; this.children = items }
  addEventListener(name, fn) { this.events[name] = fn }
  scrollIntoView() {}
}

async function main() {
  const root = path.resolve(__dirname, '..', 'web')
  const html = fs.readFileSync(path.join(root, 'index.html'), 'utf8')
  const panel = html.match(/<details\b[^>]*\bid="jira-panel"[^>]*>/)
  assert(panel && !/\bopen\b/.test(panel[0]), 'JIRA list uses a collapsed native disclosure')
  const ids = [...html.matchAll(/id="([^"]+)"/g)].map(m => m[1])
  const nodes = Object.fromEntries(ids.map(id => [id, new Element('div')]))
  const issue = {key: 'ECW6700-1', summary: '<img src=x onerror=bad()>', status: {name: '处理中'},
    issueType: {name: 'TS缺陷'}, assigneeName: '研发甲 (E0001)', group: 'other', url: 'https://jira.example.invalid/browse/ECW6700-1',
    commits: [{hash: 'a'.repeat(40), subject: 'fix pending'}]}
  const job = {id: 'abc', from: 'BASE', to: 'TARGET', statusScope: 'accepted', status: 'done', stage: '生成完成',
    error: null, batch: 'release/test', files: ['Release_Notes_TEST_accepted.md', 'CHANGELOG.generated.md', 'coverage-review.md', 'JIRA_Status_Review_TEST.md'],
    result: {jiraStatusReview: {queryFinishedAt: '2026-10-15T00:00:00Z',
      counts: {rawCommitCount: 5, associatedIssueCount: 3, otherStatusIssueCount: 1, unknownStatusIssueCount: 1, noIssueCommitCount: 1},
      issues: [issue, {...issue, key: 'ECW6700-2', status: null, group: 'unknown'}], unavailableIssues: [],
      noIssueCommits: [{hash: 'b'.repeat(40), subject: 'build update'}]},
    statusSelection: {label: '仅包含指定 JIRA 状态的记录', includedEntryCount: 2, filteredEntryCount: 3}}}
  const calls = []
  const context = vm.createContext({URL, setTimeout: () => 1, clearTimeout() {},
    document: {querySelector: () => ({content: 'token'}), getElementById: id => {
      assert(nodes[id], 'missing DOM id: ' + id); return nodes[id]
    }, createElement: tag => new Element(tag), createTextNode: text => { const e = new Element('text'); e.textContent = text; return e },
    createDocumentFragment: () => new Element('fragment')},
    fetch: async (url, options) => {
      calls.push({url, options})
      return {ok: true, json: async () => url === '/api/state' ? {latest: job} : {...job, statusScope: JSON.parse(options.body).statusScope}}
    }})
  vm.runInContext(fs.readFileSync(path.join(root, 'app.js'), 'utf8'), context)
  await new Promise(resolve => setImmediate(resolve))
  assert.strictEqual(nodes['status-scope'].value, 'accepted', 'refresh restores mode')
  assert.strictEqual(nodes['generate'].disabled, false)
  assert.strictEqual(nodes['statistics'].children.length, 3)
  assert(nodes['statistics'].textContent.includes('3关联 JIRA（去重）'))
  assert(nodes['statistics'].textContent.includes('1未提交状态 JIRA'))
  context.display({...job, result: {...job.result, historyStatistics: {hiddenMergeCommitCount: 1, revertedCommitCount: 2, effectiveCommitCount: 2}}})
  assert(nodes['result-note'].textContent.includes('隐藏纯 Merge 1 条，完整回退抵消 2 条，有效候选提交 2 条'))
  assert.strictEqual(nodes['jira-panel'].hidden, false)
  assert.strictEqual(nodes['jira-panel'].open, false, 'restored result starts collapsed')
  nodes['jira-panel'].open = true
  context.display(job)
  assert.strictEqual(nodes['jira-panel'].open, true, 'same job preserves user toggle')
  context.display({...job, id: 'next', status: 'running', result: null, files: []})
  assert.strictEqual(nodes['jira-panel'].open, false, 'new job resets expanded list')
  context.display({...job, id: 'next'})
  assert.strictEqual(nodes['jira-panel'].open, false, 'completed new result remains collapsed')
  context.display(job)
  assert.deepStrictEqual(nodes['actions'].children.map(c => c.textContent), ['预览 Release Notes', '下载 Release Notes', '下载工程变更记录'])
  assert(!nodes['actions'].textContent.includes('复核报告'))
  assert(nodes['jira-list'].textContent.includes(issue.summary), 'untrusted summary remains literal text')
  assert(nodes['jira-list'].textContent.includes('当前处理人'))
  assert(nodes['jira-list'].textContent.includes(issue.assigneeName))
  assert.strictEqual(nodes['unknown-panel'].hidden, false)
  assert.strictEqual(nodes['no-issue-panel'].hidden, false)
  assert(nodes['jira-actions'].textContent.includes('下载 JIRA 状态清单'))
  assert(!nodes['downloads'].textContent.includes('Release_Notes_TEST_accepted.md'), 'main downloads not duplicated')
  nodes['status-scope'].value = 'all'
  await nodes['generate-form'].events.submit({preventDefault() {}})
  assert.deepStrictEqual(JSON.parse(calls[calls.length - 1].options.body), {from: 'BASE', to: 'TARGET', statusScope: 'all'})
  context.display({...job, result: {...job.result, jiraStatusReview: {...job.result.jiraStatusReview, issues: [], noIssueCommits: []}}})
  assert.strictEqual(nodes['jira-list'].textContent, '无。')
  assert.strictEqual(nodes['unknown-panel'].hidden, true)
  assert.strictEqual(nodes['no-issue-panel'].hidden, true)
  context.display({...job, status: 'running', result: null, files: []})
  assert.strictEqual(nodes['status-scope'].disabled, true)
  assert.strictEqual(nodes['jira-panel'].hidden, true)
  console.log('PASS: mode restore/submit, counters, mode filename, collapsed/toggled/new-job JIRA list, empty/running states')
}
main().catch(error => { console.error(error); process.exitCode = 1 })
