'use strict'
const token = document.querySelector('meta[name="release-token"]').content
const element = id => document.getElementById(id)
let timer = null
let currentJob = null
let previewRequest = 0

async function request(path, options = {}) {
  const response = await fetch(path, {...options, headers: {'X-Release-Token': token, ...(options.headers || {})}})
  if (!response.ok) {
    const error = await response.json().catch(() => ({error: '服务连接失败，请刷新页面后重试'}))
    throw new Error(error.error)
  }
  return response
}

function busy(value) {
  element('generate').disabled = value
  element('from-ref').disabled = value
  element('to-ref').disabled = value
  element('status-scope').disabled = value
}

function showError(message) {
  element('job-panel').hidden = false
  element('error').hidden = false
  element('error').textContent = message
}

function inline(parent, text) {
  // 仅渲染行内代码；提交/JIRA 内容始终作为文本，不插入 HTML。
  text.split(/(`[^`]+`)/g).forEach(part => {
    if (part.startsWith('`') && part.endsWith('`')) {
      const code = document.createElement('code')
      code.textContent = part.slice(1, -1)
      parent.append(code)
    } else parent.append(document.createTextNode(part))
  })
}

function renderMarkdown(text) {
  const fragment = document.createDocumentFragment()
  let list = null
  for (const line of text.split(/\r?\n/)) {
    const heading = line.match(/^(#{1,6})\s+(.*)$/)
    if (line.startsWith('- ')) {
      if (!list) { list = document.createElement('ul'); fragment.append(list) }
      const item = document.createElement('li'); inline(item, line.slice(2)); list.append(item)
      continue
    }
    list = null
    if (!line.trim()) continue
    const node = document.createElement(heading ? 'h' + heading[1].length : line.startsWith('> ') ? 'blockquote' : line.startsWith('|') ? 'pre' : 'p')
    inline(node, heading ? heading[2] : line.startsWith('> ') ? line.slice(2) : line)
    fragment.append(node)
  }
  element('preview').replaceChildren(fragment)
}

function fileUrl(job, name) {
  return '/api/jobs/' + job.id + '/files/' + encodeURIComponent(name)
}

async function preview(job, name) {
  const serial = ++previewRequest
  try {
    const text = await (await request(fileUrl(job, name))).text()
    if (serial !== previewRequest || currentJob !== job.id) return
    element('preview-title').textContent = 'Release Notes 预览'
    renderMarkdown(text)
    element('preview-panel').hidden = false
    element('preview-panel').scrollIntoView({behavior: 'smooth', block: 'start'})
  } catch (error) { showError(error.message) }
}

function action(parent, label, fn) {
  const button = document.createElement('button')
  button.type = 'button'; button.className = 'secondary'; button.textContent = label
  button.addEventListener('click', async () => {
    button.disabled = true
    try { await fn() } catch (error) { showError(error.message) }
    finally { button.disabled = false }
  })
  parent.append(button)
}

async function download(job, name) {
  const blob = await (await request(fileUrl(job, name))).blob()
  const url = URL.createObjectURL(blob)
  const link = document.createElement('a'); link.href = url; link.download = name
  document.body.append(link); link.click(); link.remove()
  setTimeout(() => URL.revokeObjectURL(url), 1000)
}

function issueTable(parent, issues) {
  parent.replaceChildren()
  if (!issues.length) { parent.textContent = '无。'; return }
  const table = document.createElement('table')
  const head = document.createElement('tr')
  for (const label of ['JIRA 编号', '概要', '当前状态', '当前处理人', '类型', '关联提交']) {
    const cell = document.createElement('th'); cell.textContent = label; head.append(cell)
  }
  const thead = document.createElement('thead'); thead.append(head); table.append(thead)
  const tbody = document.createElement('tbody')
  for (const issue of issues) {
    const row = document.createElement('tr')
    const key = document.createElement('td')
    const link = document.createElement('a'); link.textContent = issue.key
    try {
      const url = new URL(issue.url)
      if (url.protocol === 'https:') { link.href = url.href; link.target = '_blank'; link.rel = 'noopener noreferrer' }
    } catch (_) { /* 无有效链接时仍显示编号。 */ }
    key.append(link); row.append(key)
    for (const value of [issue.summary, issue.status?.name || '未知 / 不一致', issue.assigneeName || '未取得', issue.issueType.name]) {
      const cell = document.createElement('td'); cell.textContent = value; row.append(cell)
    }
    const cell = document.createElement('td'); const details = document.createElement('details')
    const summary = document.createElement('summary'); summary.textContent = issue.commits.length + ' 条'
    details.append(summary)
    for (const commit of issue.commits) {
      const text = document.createElement('p'); text.textContent = commit.hash + ' ' + commit.subject; details.append(text)
    }
    cell.append(details); row.append(cell); tbody.append(row)
  }
  table.append(tbody); parent.append(table)
}

function statusPanel(job) {
  const review = job.result?.jiraStatusReview
  element('jira-panel').hidden = !review
  if (!review) return
  element('jira-note').textContent = '未提交状态指不属于“等待确认、已解决、已关闭”的 JIRA 状态。列表覆盖整个提交范围；查询结束：' + (review.queryFinishedAt || '未查询') + '。'
  issueTable(element('jira-list'), review.issues.filter(issue => issue.group === 'other'))
  const unknown = review.issues.filter(issue => issue.group === 'unknown')
  element('unknown-panel').hidden = !unknown.length && !review.unavailableIssues.length
  issueTable(element('unknown-list'), unknown)
  for (const issue of review.unavailableIssues) {
    const text = document.createElement('p'); text.textContent = issue.key + '：' + issue.reason; element('unknown-list').append(text)
  }
  element('no-issue-panel').hidden = !review.noIssueCommits.length
  element('no-issue-list').replaceChildren()
  for (const commit of review.noIssueCommits) {
    const item = document.createElement('li'); item.textContent = commit.hash + ' ' + commit.subject; element('no-issue-list').append(item)
  }
  element('jira-actions').replaceChildren()
  const report = job.files.find(name => name.startsWith('JIRA_Status_Review_'))
  if (report) action(element('jira-actions'), '下载 JIRA 状态清单', () => download(job, report))
}

function display(job) {
  if (currentJob !== job.id) element('jira-panel').open = false
  currentJob = job.id
  element('job-panel').hidden = false
  element('job-title').textContent = job.stage
  element('job-state').textContent = {running: '进行中', done: '已生成', failed: '未完成'}[job.status]
  element('job-range').textContent = job.from + ' → ' + job.to
  element('error').hidden = !job.error
  element('error').textContent = job.error || ''
  element('progress').hidden = job.status !== 'running'
  busy(job.status === 'running')
  element('statistics').replaceChildren(); element('actions').replaceChildren(); element('downloads').replaceChildren()
  element('result-note').hidden = job.status !== 'done'
  element('more-files').hidden = !job.files.length
  element('batch-path').textContent = job.batch ? '归档目录：' + job.batch : ''
  if (job.result) {
    const counts = job.result.jiraStatusReview.counts
    const fields = {rawCommitCount: 'Git 提交', associatedIssueCount: '关联 JIRA（去重）', otherStatusIssueCount: '未提交状态 JIRA'}
    for (const [key, label] of Object.entries(fields)) {
      const box = document.createElement('div'); box.className = 'stat'
      const number = document.createElement('strong'); number.textContent = counts[key]
      const name = document.createElement('span'); name.textContent = label
      box.append(number, name); element('statistics').append(box)
    }
    const selection = job.result.statusSelection
    element('result-note').textContent = '发布说明范围：' + selection.label + '；纳入客户条目 ' + selection.includedEntryCount + ' 条，按状态筛选排除 ' + selection.filteredEntryCount + ' 条。状态未知 JIRA ' + counts.unknownStatusIssueCount + ' 项，无 JIRA 编号提交 ' + counts.noIssueCommitCount + ' 条。状态与分类不代表测试通过。'
    if (job.result.historyStatistics) {
      const history = job.result.historyStatistics
      element('result-note').textContent += ' 历史筛选：隐藏纯 Merge ' + history.hiddenMergeCommitCount + ' 条，完整回退抵消 ' + history.revertedCommitCount + ' 条，有效候选提交 ' + history.effectiveCommitCount + ' 条。工程记录保留全部提交。'
    }
  }
  const notes = job.files.find(name => name.startsWith('Release_Notes_'))
  if (notes) {
    action(element('actions'), '预览 Release Notes', () => preview(job, notes))
    action(element('actions'), '下载 Release Notes', () => download(job, notes))
  }
  if (job.files.includes('CHANGELOG.generated.md')) action(element('actions'), '下载工程变更记录', () => download(job, 'CHANGELOG.generated.md'))
  for (const name of job.files.filter(name => name !== notes && name !== 'CHANGELOG.generated.md' && !name.startsWith('JIRA_Status_Review_'))) {
    action(element('downloads'), name, () => download(job, name))
  }
  statusPanel(job)
  clearTimeout(timer)
  if (job.status === 'running') timer = setTimeout(() => poll(job.id), 900)
}

async function poll(id) {
  try { display(await (await request('/api/jobs/' + id)).json()) }
  catch (error) {
    showError(error.message + '。正在生成的任务不会因页面断开而取消。')
    timer = setTimeout(() => poll(id), 3000)
  }
}

element('generate-form').addEventListener('submit', async event => {
  event.preventDefault(); clearTimeout(timer); busy(true); previewRequest++
  element('preview-panel').hidden = true; element('error').hidden = true
  try {
    const job = await (await request('/api/jobs', {method: 'POST', headers: {'Content-Type': 'application/json'},
      body: JSON.stringify({from: element('from-ref').value.trim(), to: element('to-ref').value.trim(), statusScope: element('status-scope').value})})).json()
    display(job)
  } catch (error) { busy(false); showError(error.message) }
})
element('close-preview').addEventListener('click', () => { previewRequest++; element('preview-panel').hidden = true })
busy(true)
request('/api/state').then(response => response.json()).then(state => {
  if (state.latest) {
    element('from-ref').value = state.latest.from; element('to-ref').value = state.latest.to
    element('status-scope').value = state.latest.statusScope
    display(state.latest)
  } else busy(false)
}).catch(error => { busy(true); showError(error.message) })
