const express = require('express');
const path = require('path');
const fs = require('fs/promises');
const os = require('os');
const crypto = require('crypto');
const { execFile } = require('child_process');

const REPO_ROOT = path.join(__dirname, '..');
const BENCHMARKS_DIR = path.join(REPO_ROOT, 'benchmarks');
const WATTWISE_BIN = path.join(
  REPO_ROOT,
  'build',
  process.platform === 'win32' ? 'wattwise.exe' : 'wattwise'
);

const PORT = process.env.PORT || 5173;
const ANALYZE_TIMEOUT_MS = 10_000;

const app = express();
app.use(express.json({ limit: '1mb' }));
app.use(express.static(path.join(__dirname, 'public')));

app.get('/api/benchmarks', async (req, res) => {
  try {
    const files = (await fs.readdir(BENCHMARKS_DIR))
      .filter((f) => f.endsWith('.cpp'))
      .sort();
    res.json(files);
  } catch (err) {
    res.status(500).json({ error: `could not list benchmarks: ${err.message}` });
  }
});

app.get('/api/benchmarks/:name', async (req, res) => {
  const name = req.params.name;
  if (!/^[a-zA-Z0-9_.-]+\.cpp$/.test(name)) {
    return res.status(400).json({ error: 'invalid benchmark name' });
  }
  try {
    const content = await fs.readFile(path.join(BENCHMARKS_DIR, name), 'utf8');
    res.json({ name, content });
  } catch (err) {
    res.status(404).json({ error: `benchmark not found: ${name}` });
  }
});

app.post('/api/analyze', async (req, res) => {
  const { source, opt, both } = req.body || {};
  if (typeof source !== 'string' || source.trim() === '') {
    return res.status(400).json({ error: 'source must be a non-empty string' });
  }

  const tag = crypto.randomBytes(8).toString('hex');
  const srcPath = path.join(os.tmpdir(), `wattwise-${tag}.cpp`);
  const jsonPath = path.join(os.tmpdir(), `wattwise-${tag}.json`);

  const args = [srcPath];
  if (both) args.push('--both');
  if (opt) args.push('--opt');
  args.push('--json', jsonPath);

  try {
    await fs.writeFile(srcPath, source, 'utf8');
  } catch (err) {
    return res.status(500).json({ error: `could not write temp source: ${err.message}` });
  }

  execFile(
    WATTWISE_BIN,
    args,
    { timeout: ANALYZE_TIMEOUT_MS, maxBuffer: 10 * 1024 * 1024 },
    async (err, stdout, stderr) => {
      let json = null;
      try {
        json = JSON.parse(await fs.readFile(jsonPath, 'utf8'));
      } catch {
        json = null;
      }

      await Promise.all([
        fs.unlink(srcPath).catch(() => {}),
        fs.unlink(jsonPath).catch(() => {}),
      ]);

      const exitCode = err && typeof err.code === 'number' ? err.code : 0;
      const scrub = (s) => (s || '').split(srcPath).join('source.cpp');
      if (json && json.file) json.file = 'source.cpp';

      if (err && err.killed) {
        return res.status(200).json({
          exitCode: -1,
          report: scrub(stdout),
          error: 'wattwise timed out (possible infinite loop in the submitted program)',
          json: null,
        });
      }

      res.status(200).json({ exitCode, report: scrub(stdout), error: scrub(stderr), json });
    }
  );
});

app.listen(PORT, () => {
  console.log(`WattWise web UI: http://localhost:${PORT}`);
  console.log(`Using binary: ${WATTWISE_BIN}`);
});
