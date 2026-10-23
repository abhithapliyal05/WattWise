'use strict';

/* ------------------------------------------------------------------ helpers */

const $ = (id) => document.getElementById(id);

function h(tag, attrs, ...children) {
  const node = document.createElement(tag);
  for (const [k, v] of Object.entries(attrs || {})) {
    if (v == null || v === false) continue;
    if (k === 'class') node.className = v;
    else if (k === 'text') node.textContent = v;
    else if (k === 'style' || k === 'title' || k === 'colspan' || k === 'scope' || k.startsWith('aria-') || k.startsWith('data-'))
      node.setAttribute(k, v === true ? '' : v);
    else node[k] = v;
  }
  for (const c of children.flat()) if (c != null && c !== false) node.append(c.nodeType ? c : String(c));
  return node;
}

const fmt = (n, d = 1) => (n == null ? '—' : Number(n).toLocaleString(undefined, { maximumFractionDigits: d }));
const clamp = (x, lo, hi) => Math.min(hi, Math.max(lo, x));
const reduced = () => window.matchMedia('(prefers-reduced-motion: reduce)').matches;

/* ------------------------------------------------------------------ springs */

// Spring easing derived from the closed-form response of a damped oscillator.
// `response` is the undamped period in seconds; damping 1 = no overshoot.
function springCurve(response, damping) {
  const w0 = (2 * Math.PI) / response;
  const pos = (t) => {
    if (damping >= 1) return 1 - (1 + w0 * t) * Math.exp(-w0 * t);
    const wd = w0 * Math.sqrt(1 - damping * damping);
    return 1 - Math.exp(-damping * w0 * t) * (Math.cos(wd * t) + ((damping * w0) / wd) * Math.sin(wd * t));
  };
  let settle = 0;
  for (let t = 0; t < 3; t += 0.004) if (Math.abs(1 - pos(t)) > 0.002) settle = t;
  settle = Math.max(settle + 0.004, 0.1);
  const n = clamp(Math.round(settle / 0.016), 12, 60);
  const pts = [];
  for (let i = 0; i <= n; i++) pts.push(+pos((settle * i) / n).toFixed(4));
  pts[0] = 0;
  pts[n] = 1;
  return { duration: Math.round(settle * 1000), easing: `linear(${pts.join(', ')})` };
}

const springs = {
  smooth: { duration: 420, easing: 'cubic-bezier(0.22, 1, 0.36, 1)' },
  snappy: { duration: 300, easing: 'cubic-bezier(0.22, 1, 0.36, 1)' },
  bouncy: { duration: 380, easing: 'cubic-bezier(0.34, 1.4, 0.64, 1)' },
};

(function initSprings() {
  if (!(window.CSS && CSS.supports && CSS.supports('animation-timing-function', 'linear(0, 1)'))) return;
  Object.assign(springs, {
    smooth: springCurve(0.4, 1),
    snappy: springCurve(0.3, 1),
    bouncy: springCurve(0.35, 0.8),
  });
  const root = document.documentElement.style;
  for (const [name, s] of Object.entries(springs)) {
    root.setProperty(`--spring-${name}`, s.easing);
    root.setProperty(`--spring-${name}-dur`, `${s.duration}ms`);
  }
})();

// Frame-by-frame spring for values driven from JS (the split handle). It starts
// from the current value and inherits the release velocity, so a flick carries on.
function springTo({ from, to, velocity = 0, response = 0.35, damping = 0.8, onUpdate, onDone }) {
  const k = (2 * Math.PI / response) ** 2;
  const c = 2 * damping * Math.sqrt(k);
  let x = from, v = velocity, last = performance.now(), raf = 0;
  const step = (now) => {
    const dt = Math.min((now - last) / 1000, 0.032);
    last = now;
    const n = Math.max(1, Math.ceil(dt / 0.004));
    const hStep = dt / n;
    for (let i = 0; i < n; i++) {
      v += (-k * (x - to) - c * v) * hStep;
      x += v * hStep;
    }
    onUpdate(x);
    if (Math.abs(x - to) < 0.3 && Math.abs(v) < 6) {
      onUpdate(to);
      if (onDone) onDone();
      return;
    }
    raf = requestAnimationFrame(step);
  };
  raf = requestAnimationFrame(step);
  return { cancel: () => cancelAnimationFrame(raf) };
}

/* ---------------------------------------------------------------- elements */

const sourceEl = $('source');
const benchmarkSelect = $('benchmark-select');
const optToggle = $('opt-toggle');
const bothToggle = $('both-toggle');
const analyzeBtn = $('analyze-btn');
const btnLabel = analyzeBtn.querySelector('.btn-label');
const statusLine = $('status-line');
const errorBanner = $('error-banner');
const emptyState = $('empty-state');
const resultsEl = $('results');

const isApple = /Mac|iPhone|iPad|iPod/.test(navigator.platform || navigator.userAgent || '');
$('kbd-mod').textContent = isApple ? '⌘' : 'Ctrl';

// iOS Safari only applies :active once a touch listener exists
document.addEventListener('touchstart', () => {}, { passive: true });

/* toolbar hairline fades in once content scrolls under it */
const bar = $('bar');
window.addEventListener('scroll', () => bar.classList.toggle('scrolled', window.scrollY > 4), { passive: true });

/* ------------------------------------------------------------------ editor */

const INDENT = '    ';

sourceEl.addEventListener('keydown', (e) => {
  if (e.key !== 'Tab') return;
  e.preventDefault();

  const { value, selectionStart: start, selectionEnd: end } = sourceEl;

  if (!e.shiftKey && start === end) {
    sourceEl.value = value.slice(0, start) + INDENT + value.slice(end);
    sourceEl.selectionStart = sourceEl.selectionEnd = start + INDENT.length;
    updateCount();
    return;
  }

  const lineStart = value.lastIndexOf('\n', start - 1) + 1;
  const nextBreak = value.indexOf('\n', Math.max(end - 1, lineStart));
  const lineEnd = nextBreak === -1 ? value.length : nextBreak;
  const lines = value.slice(lineStart, lineEnd).split('\n');

  const newBlock = e.shiftKey
    ? lines
        .map((line) => {
          if (line.startsWith(INDENT)) return line.slice(INDENT.length);
          const leading = line.match(/^ {1,4}/);
          return leading ? line.slice(leading[0].length) : line;
        })
        .join('\n')
    : lines.map((line) => INDENT + line).join('\n');

  sourceEl.value = value.slice(0, lineStart) + newBlock + value.slice(lineEnd);
  sourceEl.selectionStart = lineStart;
  sourceEl.selectionEnd = lineStart + newBlock.length;
  updateCount();
});

function updateCount() {
  const text = sourceEl.value;
  const n = text.length === 0 ? 0 : text.split('\n').length;
  $('source-count').textContent = `${n} ${n === 1 ? 'line' : 'lines'}`;
}

sourceEl.addEventListener('input', () => {
  benchmarkSelect.value = '';
  updateCount();
});

document.addEventListener('keydown', (e) => {
  if ((e.ctrlKey || e.metaKey) && e.key === 'Enter') {
    e.preventDefault();
    analyze();
  }
});

/* ----------------------------------------------------------------- samples */

const FEATURED = ['hoist.cpp', 'redundant.cpp', 'matmul.cpp', 'binsearch.cpp', 'bubble_sort.cpp'];

async function loadBenchmarks() {
  try {
    const names = await (await fetch('/api/benchmarks')).json();
    for (const n of names) benchmarkSelect.append(h('option', { value: n, text: n }));
    const featured = FEATURED.filter((n) => names.includes(n));
    if (featured.length) {
      for (const n of featured) {
        $('example-chips-row').append(
          h('button', { type: 'button', class: 'chip', text: n.replace(/\.cpp$/, ''), onclick: () => runExample(n) })
        );
      }
      $('example-chips').hidden = false;
    }
  } catch (err) {
    console.error('failed to load samples', err);
  }
}

async function loadSample(name) {
  try {
    const data = await (await fetch(`/api/benchmarks/${encodeURIComponent(name)}`)).json();
    if (typeof data.content !== 'string') return false;
    sourceEl.value = data.content;
    benchmarkSelect.value = name;
    updateCount();
    return true;
  } catch {
    return false;
  }
}

async function runExample(name) {
  if (await loadSample(name)) {
    optToggle.checked = true;
    analyze();
  } else {
    setStatus('Could not load that sample.');
  }
}

benchmarkSelect.addEventListener('change', () => {
  if (benchmarkSelect.value) loadSample(benchmarkSelect.value);
});

/* ------------------------------------------------- split handle (direct manipulation) */

const shell = $('shell');
const splitter = $('splitter');
const sourcePane = $('source-pane');
const SPLIT_MIN = 340, REPORT_MIN = 400, SPLIT_GAP = 12, RUBBER = 90;
let splitAnim = null;
let drag = null;

const isSplit = () => getComputedStyle(splitter).display !== 'none';
const currentSplit = () => sourcePane.getBoundingClientRect().width;

function splitBounds() {
  const cs = getComputedStyle(shell);
  const total = shell.clientWidth - parseFloat(cs.paddingLeft) - parseFloat(cs.paddingRight);
  const max = Math.max(SPLIT_MIN, total - SPLIT_GAP - REPORT_MIN);
  return { min: SPLIT_MIN, max, def: clamp(total * 0.42, SPLIT_MIN, max) };
}

function updateSplitAria(px) {
  const b = splitBounds();
  const pct = ((clamp(px, b.min, b.max) - b.min) / Math.max(1, b.max - b.min)) * 100;
  splitter.setAttribute('aria-valuenow', String(Math.round(pct)));
}

function setSplit(px) {
  shell.style.setProperty('--editor-w', `${px}px`);
  updateSplitAria(px);
}

// Resistance grows the further past the edge you drag: it stretches, then stops.
const rubber = (over) => (over * RUBBER * 0.55) / (RUBBER + 0.55 * Math.abs(over));

function animateSplit(from, to, velocity = 0, opts = {}) {
  if (splitAnim) splitAnim.cancel();
  if (reduced()) { splitAnim = null; setSplit(to); return; }
  splitAnim = springTo({ from, to, velocity, onUpdate: setSplit, onDone: () => { splitAnim = null; }, ...opts });
}

splitter.addEventListener('pointerdown', (e) => {
  if (e.pointerType === 'mouse' && e.button !== 0) return;
  if (splitAnim) { splitAnim.cancel(); splitAnim = null; }   // grabbing mid-flight takes over from where it is
  splitter.setPointerCapture(e.pointerId);
  drag = { id: e.pointerId, startX: e.clientX, startW: currentSplit(), samples: [{ t: e.timeStamp, x: e.clientX }] };
  splitter.classList.add('dragging');
  document.body.classList.add('dragging');
});

splitter.addEventListener('pointermove', (e) => {
  if (!drag || e.pointerId !== drag.id) return;
  drag.samples.push({ t: e.timeStamp, x: e.clientX });
  if (drag.samples.length > 6) drag.samples.shift();
  const b = splitBounds();
  const raw = drag.startW + (e.clientX - drag.startX);   // 1:1 with the pointer, offset from where it was grabbed
  setSplit(raw < b.min ? b.min + rubber(raw - b.min) : raw > b.max ? b.max + rubber(raw - b.max) : raw);
});

function endDrag(e) {
  if (!drag || e.pointerId !== drag.id) return;
  // velocity from the last 100 ms only: a pause before release means no flick
  const recent = drag.samples.filter((s) => s.t >= e.timeStamp - 100);
  let velocity = 0;
  if (recent.length >= 2) {
    const first = recent[0], last = recent[recent.length - 1];
    if (last.t > first.t) velocity = clamp((last.x - first.x) / ((last.t - first.t) / 1000), -3000, 3000);
  }
  drag = null;
  splitter.classList.remove('dragging');
  document.body.classList.remove('dragging');
  const b = splitBounds();
  const cur = currentSplit();
  const target = clamp(cur, b.min, b.max);
  if (Math.abs(cur - target) > 0.5) animateSplit(cur, target, velocity);   // spring back, carrying the release velocity
}
splitter.addEventListener('pointerup', endDrag);
splitter.addEventListener('pointercancel', endDrag);

splitter.addEventListener('keydown', (e) => {
  const b = splitBounds();
  const cur = currentSplit();
  const step = e.shiftKey ? 96 : 24;
  let to;
  if (e.key === 'ArrowLeft') to = cur - step;
  else if (e.key === 'ArrowRight') to = cur + step;
  else if (e.key === 'Home') to = b.min;
  else if (e.key === 'End') to = b.max;
  else if (e.key === 'Enter') to = b.def;
  else return;
  e.preventDefault();
  animateSplit(cur, clamp(to, b.min, b.max), 0, { response: 0.25, damping: 1 });
});

splitter.addEventListener('dblclick', () => animateSplit(currentSplit(), splitBounds().def, 0, { damping: 1 }));

let resizeFrame = 0;
window.addEventListener('resize', () => {
  cancelAnimationFrame(resizeFrame);
  resizeFrame = requestAnimationFrame(() => {
    if (!isSplit()) return;
    const b = splitBounds();
    if (shell.style.getPropertyValue('--editor-w')) setSplit(clamp(currentSplit(), b.min, b.max));
    else updateSplitAria(currentSplit());
  });
});

/* ---------------------------------------------------------------- rendering */

const CAT_LABELS = {
  'mem.load': 'Memory loads',
  'mem.store': 'Memory stores',
  'int.alu': 'Integer add / compare',
  'int.shift': 'Integer shifts',
  'int.mul': 'Integer multiply',
  'int.div': 'Integer divide / modulo',
  'fp.alu': 'Floating-point add / compare',
  'fp.mul': 'Floating-point multiply',
  'fp.div': 'Floating-point divide',
  convert: 'Type conversions',
  move: 'Register moves',
  'branch.uncond': 'Jumps',
  'branch.cond': 'Conditional branches',
  call: 'Function calls',
  ret: 'Returns',
  param: 'Argument passing',
  io: 'Output',
};

const PASS_LABELS = {
  'strength-reduction': 'Faster op',
  'local-cse': 'Reused value',
  licm: 'Hoisted out of loop',
  'redundant-load-elim': 'Reused load',
  'dead-code-elim': 'Removed dead code',
};

const ERROR_TITLES = {
  1: "WattWise couldn't compile this program",
  2: 'The program failed while running',
  3: 'Optimization changed the program’s output',
  64: 'The request was invalid',
  '-1': 'The analysis timed out',
  request: "The request couldn't be completed",
};

const fill = (v, cls) =>
  h('span', { class: cls ? `fill ${cls}` : 'fill', style: `--v:${clamp(v, 0, 1).toFixed(4)}` });

function stat(label, value, was) {
  return h('div', null, h('dt', { text: label }), h('dd', null, value, was ? h('small', { text: was }) : null));
}

function parseVerification(report) {
  const m = /Verification: optimised program output (IDENTICAL|DIFFERS)(?: \((\d+) -> (\d+) IR instructions executed\))?/.exec(report || '');
  return m ? { same: m[1] === 'IDENTICAL', before: m[2] ? +m[2] : null, after: m[3] ? +m[3] : null } : null;
}

function renderHero(json, verification) {
  const hero = $('hero');
  hero.replaceChildren();
  const b = json.before, a = json.after;
  const view = a || b;
  const stats = h('dl', { class: 'stats' },
    stat('Basic blocks', fmt(view.blocks.length, 0)),
    stat('Loops', fmt(view.loops.length, 0)),
    stat('Functions', fmt(view.functions.length, 0)));

  if (!a) {
    hero.append(
      h('div', null,
        h('p', { class: 'eyebrow', text: 'Estimated energy' }),
        h('p', { class: 'display' }, fmt(b.total_pj), h('span', { class: 'unit', text: 'pJ' })),
        h('p', { class: 'hero-sub', text: 'A static estimate from loop structure. Turn on Optimize to see how much of it can be saved.' })),
      stats);
    return;
  }

  const pct = json.reduction_pct || 0;
  const mode = pct > 0.05 ? 'good' : pct < -0.05 ? 'worse' : 'flat';
  const headline = mode === 'good'
    ? [`${fmt(pct)}%`, 'less energy']
    : mode === 'worse'
      ? [`${fmt(-pct)}%`, 'more energy']
      : ['0%', 'no change'];

  const top = h('div', null,
    h('p', { class: 'eyebrow', text: 'Energy after optimization' }),
    h('p', { class: `display ${mode}` }, headline[0], h('span', { class: 'unit', text: headline[1] })),
    h('p', { class: 'hero-sub' }, h('strong', { text: `${fmt(b.total_pj)} pJ` }), ' → ', h('strong', { text: `${fmt(a.total_pj)} pJ` })));

  if (mode === 'flat') {
    top.append(h('p', { class: 'hero-sub', text: 'The optimizer found nothing it could safely improve on the hot lines.' }));
  }
  if (verification) {
    top.append(h('span', { class: verification.same ? 'badge' : 'badge bad' },
      verification.same ? '✓ Output verified identical' : '✗ Output changed'));
  }

  const max = Math.max(b.total_pj, a.total_pj, 1);
  const compare = h('div', { class: 'compare' },
    h('div', { class: 'compare-row' },
      h('span', { class: 'compare-label', text: 'Before' }),
      h('span', { class: 'track' }, fill(b.total_pj / max, 'before')),
      h('span', { class: 'compare-val', text: `${fmt(b.total_pj)} pJ` })),
    h('div', { class: 'compare-row' },
      h('span', { class: 'compare-label', text: 'After' }),
      h('span', { class: 'track' }, fill(a.total_pj / max)),
      h('span', { class: 'compare-val', text: `${fmt(a.total_pj)} pJ` })));

  const rewrites = (json.passes || []).reduce((s, p) => s + p.changes, 0);
  stats.prepend(stat('Rewrites', fmt(rewrites, 0)));
  if (verification && verification.before != null) {
    stats.append(stat('Instructions run', fmt(verification.after, 0), `was ${fmt(verification.before, 0)}`));
  }
  hero.append(top, compare, stats);
}

function renderCategories(before, after) {
  const list = $('category-bars');
  list.replaceChildren();
  const b = before.by_category || {}, a = after ? after.by_category || {} : null;
  const val = (m, k) => (m && m[k]) || 0;
  const keys = [...new Set([...Object.keys(b), ...(a ? Object.keys(a) : [])])].filter((k) => Math.max(val(b, k), val(a, k)) > 0);
  keys.sort((x, y) => Math.max(val(b, y), val(a, y)) - Math.max(val(b, x), val(a, x)));
  const max = Math.max(1, ...keys.map((k) => Math.max(val(b, k), val(a, k))));
  const total = Object.values(a || b).reduce((s, v) => s + v, 0) || 1;
  $('cat-legend').hidden = !a;

  for (const k of keys) {
    const bv = val(b, k), av = a ? val(a, k) : bv;
    const track = h('span', { class: 'track' });
    if (a) track.append(fill(bv / max, 'before'));
    track.append(fill(av / max));
    const value = h('span', { class: 'cat-val' }, fmt(av));
    if (a) {
      if (Math.abs(av - bv) > 0.05) value.append(h('small', { text: `was ${fmt(bv)}` }));
    } else {
      value.append(h('small', { text: `${fmt((av / total) * 100)}%` }));
    }
    list.append(h('li', { class: 'cat-row' },
      h('span', { class: 'cat-name', title: k }, CAT_LABELS[k] || k, h('span', { class: 'cat-key', text: k })),
      track, value));
  }
}

function head(cols) {
  return h('thead', null, h('tr', null, cols.map(([label, cls]) => h('th', { class: cls || null, scope: 'col', text: label }))));
}

function renderBlocks(view) {
  const table = $('blocks-table');
  table.replaceChildren();
  const blocks = view.blocks || [];
  const total = blocks.reduce((s, x) => s + x.energy_pj, 0) || 1;
  const rows = [...blocks].sort((x, y) => y.energy_pj - x.energy_pj).slice(0, 10);
  $('blocks-hint').textContent = blocks.length > rows.length
    ? `The ${rows.length} of ${blocks.length} basic blocks that use the most energy. Start optimizing here.`
    : 'The basic blocks that use the most energy. Start optimizing here.';

  const body = h('tbody');
  for (const x of rows) {
    const pct = (x.energy_pj / total) * 100;
    body.append(h('tr', null,
      h('td', { text: x.func }),
      h('td', { class: 'mono', text: x.block }),
      h('td', { class: 'mono', text: x.first_line === x.last_line ? String(x.first_line) : `${x.first_line}–${x.last_line}` }),
      h('td', { class: 'num col-2', text: String(x.depth) }),
      h('td', { class: 'num col-2', text: fmt(x.executions, 0) }),
      h('td', { class: 'num', text: fmt(x.energy_pj) }),
      h('td', null, h('div', { class: 'share' },
        h('span', { class: 'track thin' }, fill(pct / 100)),
        h('span', { class: 'share-label', text: `${fmt(pct)}%` })))));
  }
  table.append(
    h('caption', { class: 'sr-only', text: 'Basic blocks ranked by energy' }),
    head([['Function'], ['Block'], ['Lines'], ['Depth', 'num col-2'], ['Runs', 'num col-2'], ['Energy (pJ)', 'num'], ['Share']]),
    body);
}

function basisOf(src) {
  if (!src) return { label: 'Unknown', cls: 'guess' };
  if (src.startsWith('measured')) return { label: 'Measured', cls: 'exact' };
  if (src.startsWith('default')) return { label: 'Guess', cls: 'guess' };
  if (src.startsWith('approx')) return { label: 'Approximate', cls: 'approx' };
  return { label: 'Exact', cls: 'exact' };
}

function renderLoops(view) {
  const table = $('loops-table');
  table.replaceChildren();
  const loops = view.loops || [];
  const body = h('tbody');
  if (!loops.length) {
    body.append(h('tr', null, h('td', { colspan: 7, text: 'No loops in this program.' })));
  }
  for (const l of loops) {
    const basis = basisOf(l.trip_source);
    body.append(h('tr', null,
      h('td', { text: l.func }),
      h('td', { class: 'mono', text: l.header }),
      h('td', { class: 'mono col-2', text: String(l.line) }),
      h('td', { class: 'num col-2', text: String(l.depth) }),
      h('td', { class: 'num', text: fmt(l.trip) }),
      h('td', { class: 'num', text: fmt(l.energy_pj) }),
      h('td', { class: 'basis' }, h('span', { class: `pill ${basis.cls}`, text: basis.label }), h('span', { class: 'basis-text', text: l.trip_source }))));
  }
  table.append(
    h('caption', { class: 'sr-only', text: 'Loops ranked by inclusive energy' }),
    head([['Function'], ['Loop'], ['Line', 'col-2'], ['Depth', 'num col-2'], ['Trips', 'num'], ['Energy (pJ)', 'num'], ['Trip count basis']]),
    body);
}

function renderPasses(passes) {
  const card = $('passes-card');
  const list = $('passes-list');
  list.replaceChildren();
  const items = [];
  for (const p of passes || []) if (p.changes) for (const line of p.log) items.push({ pass: p.pass, line });
  card.hidden = items.length === 0;
  if (!items.length) return;
  $('passes-hint').textContent = `${items.length} ${items.length === 1 ? 'rewrite' : 'rewrites'} applied to the hot lines.`;
  for (const c of items) {
    list.append(h('li', { class: 'change' },
      h('span', { class: 'pill', text: PASS_LABELS[c.pass] || c.pass }),
      h('span', { class: 'change-text', text: c.line })));
  }
}

/* ---------------------------------------------------------------- entrance */

let running = [];

// Elements rise in together and bars grow from their baseline. Everything is
// transform/opacity, and a new render cancels whatever is still in flight.
function enter(root) {
  running.forEach((a) => a.cancel());
  running = [];
  const calm = reduced();
  root.querySelectorAll('[data-rise]').forEach((node, i) => {
    const frames = calm
      ? [{ opacity: 0 }, { opacity: 1 }]
      : [{ opacity: 0, transform: 'translateY(12px)' }, { opacity: 1, transform: 'translateY(0)' }];
    running.push(node.animate(frames, {
      duration: calm ? 160 : springs.smooth.duration + 140,
      delay: calm ? 0 : Math.min(i, 8) * 55,
      easing: calm ? 'ease-out' : springs.smooth.easing,
      fill: 'backwards',
    }));
  });
  if (calm) return;
  root.querySelectorAll('.fill').forEach((node, i) => {
    const v = parseFloat(node.style.getPropertyValue('--v')) || 0;
    running.push(node.animate(
      [{ transform: 'translateX(-100%)' }, { transform: `translateX(${(v - 1) * 100}%)` }],
      { duration: springs.smooth.duration + 380, delay: 180 + Math.min(i, 12) * 32, easing: springs.smooth.easing, fill: 'backwards' }));
  });
}

/* ------------------------------------------------------------------ status */

function setStatus(text) { statusLine.textContent = text; }

function setLoading(on) {
  analyzeBtn.disabled = on;
  analyzeBtn.classList.toggle('is-loading', on);
  btnLabel.textContent = on ? 'Analyzing…' : 'Analyze';
}

function showError(code, detail) {
  $('error-title').textContent = ERROR_TITLES[code] || 'Something went wrong';
  $('error-detail').textContent = (detail || '').trim();
  errorBanner.hidden = false;
  errorBanner.style.animation = 'none';
  void errorBanner.offsetWidth;
  errorBanner.style.animation = '';
}

/* ----------------------------------------------------------------- analyze */

let busy = false;

async function analyze() {
  if (busy) return;
  const source = sourceEl.value;
  if (!source.trim()) {
    setStatus('Paste or load a program first.');
    sourceEl.focus();
    return;
  }

  const options = { opt: optToggle.checked, both: bothToggle.checked };
  busy = true;
  setLoading(true);
  setStatus('Compiling and analyzing…');
  errorBanner.hidden = true;
  const started = performance.now();

  try {
    const res = await fetch('/api/analyze', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ source, ...options }),
    });
    const data = await res.json().catch(() => ({ error: 'The server returned an unreadable response.' }));

    if (!res.ok) {
      resultsEl.hidden = true;
      emptyState.hidden = true;
      showError('request', data.error);
      setStatus('');
      return;
    }

    // exit 3 means the optimizer changed the output: still show the numbers, but warn loudly
    if (!data.json || (data.exitCode !== 0 && data.exitCode !== 3)) {
      resultsEl.hidden = true;
      emptyState.hidden = true;
      showError(data.exitCode, data.error || data.report);
      setStatus(`Exit code ${data.exitCode}`);
      return;
    }

    const json = data.json;
    const view = json.after || json.before;
    renderHero(json, parseVerification(data.report));
    $('dynamic-note').hidden = !options.both;
    renderCategories(json.before, json.after);
    renderBlocks(view);
    renderLoops(view);
    renderPasses(json.passes);
    $('raw-report').textContent = data.report || '';

    emptyState.hidden = true;
    resultsEl.hidden = false;
    enter(resultsEl);
    if (data.exitCode === 3) {
      showError(3, 'The optimized program printed different output from the original, so treat these numbers with suspicion.');
    }
    setStatus(`Done in ${((performance.now() - started) / 1000).toFixed(2)} s`);

    if (window.matchMedia('(max-width: 960px)').matches) {
      $('report').scrollIntoView({ behavior: reduced() ? 'auto' : 'smooth', block: 'start' });
    }
  } catch (err) {
    resultsEl.hidden = true;
    emptyState.hidden = true;
    showError('request', err.message);
    setStatus('');
  } finally {
    busy = false;
    setLoading(false);
  }
}

analyzeBtn.addEventListener('click', analyze);

/* -------------------------------------------------------------------- init */

updateCount();
updateSplitAria(currentSplit());
loadBenchmarks();
