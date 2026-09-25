'use strict';

// ---------- 与原生通信 ----------
// 契约见规格 5.1：发送 {cmd}，接收带 type 的响应。
// 慢扫描的去重由原生侧负责（只回传最新一次请求的结果），前端无需处理请求号。
function send(cmd) {
  window.chrome.webview.postMessage(JSON.stringify({ cmd }));
}

const tabs = document.querySelectorAll('.tab');
let activeTab = 'cpu';
let pollTimer = null;
let storageRequested = false;

function onMessage(event) {
  let msg;
  try {
    msg = typeof event.data === 'string' ? JSON.parse(event.data) : event.data;
  } catch (e) {
    showToast('收到无法解析的响应');
    return;
  }
  if (!msg || !msg.type) return;

  if (msg.type === 'error') {
    showToast(msg.message || '操作失败');
    setStorageBusy(false);
    return;
  }

  if (msg.type === 'cpu') renderCpu(msg);
  else if (msg.type === 'memory') renderMemory(msg);
  else if (msg.type === 'storage') { renderStorage(msg); setStorageBusy(false); }
}

if (window.chrome && window.chrome.webview) {
  window.chrome.webview.addEventListener('message', onMessage);
} else {
  document.getElementById('toast').hidden = false;
  document.getElementById('toast').textContent = '未检测到 WebView2 宿主';
}

// ---------- 格式化（与原生 Format.h 同规则：1024 进制，去掉末尾 .0）----------
function fmtBytes(bytes) {
  if (bytes < 1024) return bytes + ' B';
  const units = ['KB', 'MB', 'GB', 'TB'];
  let v = bytes;
  let i = -1;
  while (v >= 1024 && i < units.length - 1) { v /= 1024; i++; }
  let s = v.toFixed(1);
  if (s.indexOf('.') >= 0) s = s.replace(/\.?0+$/, '');
  return s + ' ' + units[i];
}

function fmtPercent(p) {
  let s = p.toFixed(1);
  if (s.indexOf('.') >= 0) s = s.replace(/\.?0+$/, '');
  return s + '%';
}

function fmtMs(ms) {
  if (ms < 1000) return ms + ' ms';
  const s = ms / 1000;
  if (s < 60) return s.toFixed(1) + ' s';
  return Math.floor(s / 60) + ' 分 ' + Math.round(s % 60) + ' 秒';
}

function fmtTime(unixMs) {
  if (!unixMs) return '—';
  const d = new Date(unixMs);
  const p = (n) => String(n).padStart(2, '0');
  return d.getFullYear() + '-' + p(d.getMonth() + 1) + '-' + p(d.getDate()) + ' ' +
         p(d.getHours()) + ':' + p(d.getMinutes());
}

// ---------- 渲染辅助（一律 textContent，绝不 innerHTML）----------
function cell(row, text, className) {
  const td = document.createElement('td');
  td.textContent = text;
  if (className) td.className = className;
  row.appendChild(td);
  return td;
}

function clear(el) {
  while (el.firstChild) el.removeChild(el.firstChild);
}

function showToast(text) {
  const t = document.getElementById('toast');
  t.textContent = text;
  t.hidden = false;
  clearTimeout(showToast.timer);
  showToast.timer = setTimeout(() => { t.hidden = true; }, 4000);
}

// ---------- CPU ----------
function renderCpu(m) {
  document.getElementById('cpu-total').textContent = fmtPercent(m.totalPercent || 0);

  const bar = document.getElementById('cpu-bar');
  clear(bar);
  const seg = document.createElement('span');
  seg.className = 'thread';
  seg.style.width = Math.max(0, Math.min(100, m.totalPercent || 0)) + '%';
  bar.appendChild(seg);

  document.getElementById('cpu-note').textContent = m.baseline
    ? '正在建立采样基线（首次采样不显示百分比）'
    : '单线程上限 100%，不折算多核总容量';

  const tbody = document.getElementById('cpu-threads');
  clear(tbody);
  for (const t of m.threads || []) {
    const tr = document.createElement('tr');
    cell(tr, t.name || ('线程 ' + t.tid));
    cell(tr, String(t.tid));
    cell(tr, t.baseline ? '—' : fmtPercent(t.cpuPercent), 'num');
    cell(tr, fmtMs(t.totalMs || 0), 'num');
    tbody.appendChild(tr);
  }

  const kids = m.children || [];
  const card = document.getElementById('cpu-children-card');
  card.hidden = kids.length === 0;
  const kbody = document.getElementById('cpu-children');
  clear(kbody);
  for (const c of kids) {
    const tr = document.createElement('tr');
    cell(tr, c.name || '(unknown)');
    cell(tr, String(c.pid));
    cell(tr, fmtPercent(c.cpuPercent), 'num');
    cell(tr, fmtBytes(c.workingSetBytes), 'num');
    kbody.appendChild(tr);
  }
}

// ---------- 内存 ----------
function renderMemory(m) {
  document.getElementById('mem-ws').textContent = fmtBytes(m.workingSetBytes || 0);
  document.getElementById('mem-private').textContent = fmtBytes(m.privateBytes || 0);
  document.getElementById('mem-peak').textContent = fmtBytes(m.peakWorkingSetBytes || 0);
  document.getElementById('mem-note').textContent = '按已加载模块的映像大小排序（不含虚拟大小）';

  const tbody = document.getElementById('mem-modules');
  clear(tbody);
  for (const mod of m.modules || []) {
    const tr = document.createElement('tr');
    cell(tr, mod.name);
    cell(tr, fmtBytes(mod.imageBytes), 'num');
    tbody.appendChild(tr);
  }

  const kids = m.children || [];
  const card = document.getElementById('mem-children-card');
  card.hidden = kids.length === 0;
  const kbody = document.getElementById('mem-children');
  clear(kbody);
  for (const c of kids) {
    const tr = document.createElement('tr');
    cell(tr, c.name || '(unknown)');
    cell(tr, String(c.pid));
    cell(tr, fmtBytes(c.workingSetBytes), 'num');
    kbody.appendChild(tr);
  }
}

// ---------- 存储 ----------
function renderStorage(m) {
  document.getElementById('sto-total').textContent = fmtBytes(m.totalBytes || 0);
  document.getElementById('sto-when').textContent = fmtTime(m.scannedAtMs);

  const bar = document.getElementById('sto-bar');
  clear(bar);
  const rows = m.rows || m.categories || [];
  for (const c of rows) {
    if (!c.bytes) continue;
    const seg = document.createElement('span');
    seg.style.width = c.percent + '%';
    seg.style.background = c.color;
    seg.title = c.name + ' ' + fmtBytes(c.bytes);
    bar.appendChild(seg);
  }

  const tbody = document.getElementById('sto-rows');
  clear(tbody);
  for (const c of rows) {
    const tr = document.createElement('tr');
    const sw = document.createElement('td');
    sw.className = 'swatch';
    const dot = document.createElement('i');
    dot.style.background = c.color;
    sw.appendChild(dot);
    tr.appendChild(sw);
    cell(tr, c.name);
    cell(tr, fmtBytes(c.bytes), 'num');
    cell(tr, fmtPercent(c.percent), 'num');
    cell(tr, c.note || '');
    tbody.appendChild(tr);
  }

  const warnings = m.warnings || [];
  const card = document.getElementById('sto-warnings-card');
  card.hidden = warnings.length === 0;
  const ul = document.getElementById('sto-warnings');
  clear(ul);
  for (const w of warnings) {
    const li = document.createElement('li');
    li.textContent = w;
    ul.appendChild(li);
  }
}

function setStorageBusy(busy) {
  const btn = document.getElementById('sto-recompute');
  btn.disabled = busy;
  btn.textContent = busy ? '正在计算…' : '重新计算';
}

// ---------- 轮询与页签 ----------
function stopPolling() {
  if (pollTimer) { clearInterval(pollTimer); pollTimer = null; }
}

function startPolling() {
  stopPolling();
  if (activeTab === 'cpu' || activeTab === 'memory') {
    pollTimer = setInterval(() => {
      if (document.hidden) return; // 页面不可见时不采样，减少无谓开销
      send(activeTab);
    }, 2000);
  }
}

function selectTab(name) {
  activeTab = name;
  for (const t of tabs) t.classList.toggle('active', t.dataset.tab === name);
  for (const p of document.querySelectorAll('.panel')) {
    p.classList.toggle('active', p.id === 'panel-' + name);
  }

  if (name === 'cpu' || name === 'memory') {
    send(name);
    startPolling();
  } else {
    stopPolling();
    if (!storageRequested) {
      storageRequested = true;
      setStorageBusy(true);
      send('storage');
    }
  }
}

for (const t of tabs) {
  t.addEventListener('click', () => selectTab(t.dataset.tab));
}

document.getElementById('sto-recompute').addEventListener('click', () => {
  setStorageBusy(true);
  send('storage');
});

document.addEventListener('visibilitychange', () => {
  if (!document.hidden && (activeTab === 'cpu' || activeTab === 'memory')) send(activeTab);
});

// 首屏
selectTab('cpu');
