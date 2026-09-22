const sourceEl = document.getElementById('source');
const benchmarkSelect = document.getElementById('benchmark-select');
const optToggle = document.getElementById('opt-toggle');
const bothToggle = document.getElementById('both-toggle');
const analyzeBtn = document.getElementById('analyze-btn');
const statusLine = document.getElementById('status-line');
const errorBanner = document.getElementById('error-banner');
const emptyState = document.getElementById('empty-state');
const resultsEl = document.getElementById('results');
const summaryCards = document.getElementById('summary-cards');
const blocksTable = document.getElementById('blocks-table');
const loopsTable = document.getElementById('loops-table');
const passesCard = document.getElementById('passes-card');
const passesList = document.getElementById('passes-list');
const rawReport = document.getElementById('raw-report');
const dynamicNote = document.getElementById('dynamic-note');

let categoryChart = null;

const INDENT = '    ';

sourceEl.addEventListener('keydown', (e) => {
  if (e.key !== 'Tab') return;
  e.preventDefault();

  const el = sourceEl;
  const { value, selectionStart: start, selectionEnd: end } = el;

  if (!e.shiftKey && start === end) {
    el.value = value.slice(0, start) + INDENT + value.slice(end);
    el.selectionStart = el.selectionEnd = start + INDENT.length;
    return;
  }

  const lineStart = value.lastIndexOf('\n', start - 1) + 1;
  const nextBreak = value.indexOf('\n', Math.max(end - 1, lineStart));
  const lineEnd = nextBreak === -1 ? value.length : nextBreak;
  const block = value.slice(lineStart, lineEnd);
  const lines = block.split('\n');

  let newBlock;
  if (e.shiftKey) {
    newBlock = lines
      .map((line) => {
        if (line.startsWith(INDENT)) return line.slice(INDENT.length);
        const leading = line.match(/^ {1,4}/);
        return leading ? line.slice(leading[0].length) : line;
      })
      .join('\n');
  } else {
    newBlock = lines.map((line) => INDENT + line).join('\n');
  }

  el.value = value.slice(0, lineStart) + newBlock + value.slice(lineEnd);
  el.selectionStart = lineStart;
  el.selectionEnd = lineStart + newBlock.length;
});

function cssVar(name) {
  return getComputedStyle(document.documentElement).getPropertyValue(name).trim();
}

function fmtNum(n) {
  if (n === undefined || n === null) return '—';
  return Number(n).toLocaleString(undefined, { maximumFractionDigits: 1 });
}

function stagger(container, selector) {
  container.querySelectorAll(selector).forEach((el, i) => {
    el.style.setProperty('--i', i);
  });
}

async function loadBenchmarks() {
  try {
    const res = await fetch('/api/benchmarks');
    const names = await res.json();
    for (const name of names) {
      const opt = document.createElement('option');
      opt.value = name;
      opt.textContent = name;
      benchmarkSelect.appendChild(opt);
    }
  } catch (err) {
    console.error('failed to load benchmarks', err);
  }
}

benchmarkSelect.addEventListener('change', async () => {
  const name = benchmarkSelect.value;
  if (!name) return;
  const res = await fetch(`/api/benchmarks/${encodeURIComponent(name)}`);
  const data = await res.json();
  if (data.content) sourceEl.value = data.content;
});

function clearTable(table) {
  table.innerHTML = '';
}

function shareCell(pj, total) {
  const pct = total > 0 ? (pj / total) * 100 : 0;
  const td = document.createElement('td');
  td.className = 'text-cell';
  td.innerHTML = `
    <div class="share-cell">
      <span class="share-bar-track"><span class="share-bar-fill" style="width:${pct.toFixed(1)}%"></span></span>
      <span class="share-label">${pct.toFixed(1)}%</span>
    </div>`;
  return td;
}

function renderBlocksTable(blocks) {
  clearTable(blocksTable);
  const head = document.createElement('tr');
  ['Function', 'Block', 'Lines', 'Depth', 'Executions', 'Energy pJ', 'Share'].forEach((h) => {
    const th = document.createElement('th');
    th.textContent = h;
    head.appendChild(th);
  });
  blocksTable.appendChild(head);

  const total = blocks.reduce((s, b) => s + b.energy_pj, 0) || 1;
  const sorted = [...blocks].sort((a, b) => b.energy_pj - a.energy_pj).slice(0, 10);
  sorted.forEach((b, i) => {
    const tr = document.createElement('tr');
    const cells = [b.func, b.block, `${b.first_line}-${b.last_line}`, b.depth, fmtNum(b.executions), fmtNum(b.energy_pj)];
    cells.forEach((c) => {
      const td = document.createElement('td');
      td.textContent = c;
      tr.appendChild(td);
    });
    const share = shareCell(b.energy_pj, total);
    share.querySelector('.share-bar-fill').style.setProperty('--i', i);
    tr.appendChild(share);
    blocksTable.appendChild(tr);
  });
}

function renderLoopsTable(loops) {
  clearTable(loopsTable);
  const head = document.createElement('tr');
  ['Function', 'Header', 'Line', 'Depth', 'Trip count', 'Energy pJ', 'Trip source'].forEach((h) => {
    const th = document.createElement('th');
    th.textContent = h;
    head.appendChild(th);
  });
  loopsTable.appendChild(head);

  if (loops.length === 0) {
    const tr = document.createElement('tr');
    const td = document.createElement('td');
    td.className = 'text-cell';
    td.textContent = 'No loops detected in this program.';
    td.colSpan = 7;
    tr.appendChild(td);
    loopsTable.appendChild(tr);
    return;
  }

  for (const l of loops) {
    const tr = document.createElement('tr');
    const cells = [l.func, l.header, l.line, l.depth, fmtNum(l.trip), fmtNum(l.energy_pj)];
    cells.forEach((c) => {
      const td = document.createElement('td');
      td.textContent = c;
      tr.appendChild(td);
    });
    const tripTd = document.createElement('td');
    tripTd.className = 'text-cell';
    tripTd.textContent = l.trip_source;
    tr.appendChild(tripTd);
    loopsTable.appendChild(tr);
  }
}

function renderCategoryChart(byCategory) {
  const entries = Object.entries(byCategory).sort((a, b) => b[1] - a[1]);
  const labels = entries.map((e) => e[0]);
  const values = entries.map((e) => e[1]);

  const seriesColor = cssVar('--series-1');
  const gridColor = cssVar('--gridline');
  const textColor = cssVar('--text-secondary');
  const fontFamily = cssVar('--font-sans');

  const ctx = document.getElementById('category-chart').getContext('2d');
  if (categoryChart) categoryChart.destroy();
  categoryChart = new Chart(ctx, {
    type: 'bar',
    data: {
      labels,
      datasets: [
        {
          label: 'Energy (pJ)',
          data: values,
          backgroundColor: seriesColor,
          borderRadius: 4,
          maxBarThickness: 28,
        },
      ],
    },
    options: {
      indexAxis: 'y',
      responsive: true,
      maintainAspectRatio: false,
      animation: {
        duration: window.matchMedia('(prefers-reduced-motion: reduce)').matches ? 0 : 500,
        easing: 'easeOutQuart',
      },
      font: { family: fontFamily },
      plugins: {
        legend: { display: false },
        tooltip: {
          bodyFont: { family: fontFamily },
          titleFont: { family: fontFamily },
          callbacks: {
            label: (item) => `${fmtNum(item.raw)} pJ`,
          },
        },
      },
      scales: {
        x: {
          grid: { color: gridColor },
          ticks: { color: textColor, font: { family: fontFamily } },
          title: { display: true, text: 'pJ (model units)', color: textColor, font: { family: fontFamily } },
        },
        y: {
          grid: { display: false },
          ticks: { color: textColor, font: { family: fontFamily } },
        },
      },
    },
  });
}

function renderSummary(json) {
  summaryCards.innerHTML = '';
  const tiles = [];

  if (json.after) {
    tiles.push({ label: 'Energy before', value: `${fmtNum(json.before.total_pj)} pJ` });
    tiles.push({ label: 'Energy after', value: `${fmtNum(json.after.total_pj)} pJ` });
    tiles.push({
      label: 'Reduction',
      value: `${json.reduction_pct ? json.reduction_pct.toFixed(1) : '0.0'}%`,
      good: true,
    });
  } else {
    tiles.push({ label: 'Total estimated energy', value: `${fmtNum(json.before.total_pj)} pJ` });
    tiles.push({ label: 'Basic blocks', value: json.before.blocks.length });
    tiles.push({ label: 'Loops', value: json.before.loops.length });
  }

  tiles.forEach((t, i) => {
    const div = document.createElement('div');
    div.className = 'stat-tile';
    div.style.setProperty('--i', i);
    div.innerHTML = `<div class="label">${t.label}</div><div class="value${t.good ? ' good' : ''}">${t.value}</div>`;
    summaryCards.appendChild(div);
  });
}

const PASS_LABELS = {
  'strength-reduction': 'Faster op',
  'local-cse': 'Reused value',
  licm: 'Hoisted out of loop',
};

function renderPasses(passes) {
  if (!passes || passes.length === 0) {
    passesCard.classList.add('hidden');
    return;
  }
  const changeLines = [];
  for (const p of passes) {
    if (p.changes === 0) continue;
    for (const line of p.log) changeLines.push({ pass: p.pass, line });
  }
  if (changeLines.length === 0) {
    passesCard.classList.add('hidden');
    return;
  }

  passesCard.classList.remove('hidden');
  passesList.innerHTML = '';
  changeLines.forEach((c, i) => {
    const li = document.createElement('li');
    li.style.setProperty('--i', i);
    const tag = document.createElement('span');
    tag.className = 'pass-tag';
    tag.textContent = PASS_LABELS[c.pass] || c.pass;
    const text = document.createElement('span');
    text.textContent = c.line;
    li.appendChild(tag);
    li.appendChild(text);
    passesList.appendChild(li);
  });
}

const btnLabel = analyzeBtn.querySelector('.btn-label');

function setLoading(isLoading) {
  analyzeBtn.disabled = isLoading;
  analyzeBtn.classList.toggle('is-loading', isLoading);
  btnLabel.textContent = isLoading ? 'Analyzing…' : 'Analyze';
}

async function analyze() {
  const source = sourceEl.value;
  if (!source.trim()) {
    statusLine.textContent = 'Paste or load some source first.';
    return;
  }

  setLoading(true);
  statusLine.textContent = 'Compiling and analyzing…';
  errorBanner.classList.add('hidden');

  try {
    const res = await fetch('/api/analyze', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        source,
        opt: optToggle.checked,
        both: bothToggle.checked,
      }),
    });
    const data = await res.json();

    if (data.exitCode !== 0 || !data.json) {
      emptyState.classList.add('hidden');
      resultsEl.classList.add('hidden');
      errorBanner.classList.remove('hidden');
      errorBanner.textContent = data.error || 'wattwise reported an error.';
      statusLine.textContent = `Exit code ${data.exitCode}`;
      return;
    }

    const json = data.json;
    const view = json.after || json.before;

    renderSummary(json);
    renderCategoryChart(view.by_category);
    renderBlocksTable(view.blocks);
    renderLoopsTable(view.loops);
    renderPasses(json.passes);
    rawReport.textContent = data.report;

    dynamicNote.classList.toggle('hidden', !bothToggle.checked);

    emptyState.classList.add('hidden');
    resultsEl.classList.remove('hidden');
    stagger(resultsEl, '.chart-card, .table-card');
    statusLine.textContent = 'Done.';
  } catch (err) {
    errorBanner.classList.remove('hidden');
    errorBanner.textContent = `Request failed: ${err.message}`;
    statusLine.textContent = '';
  } finally {
    setLoading(false);
  }
}

analyzeBtn.addEventListener('click', analyze);
loadBenchmarks();
