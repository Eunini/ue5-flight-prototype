import { chromium } from 'playwright';
import http from 'http'; import fs from 'fs'; import path from 'path';
const dir = process.cwd();
const types = { '.html': 'text/html', '.js': 'text/javascript', '.json': 'application/json' };
const server = http.createServer((q, r) => { const f = path.join(dir, decodeURIComponent(q.url.split('?')[0]) === '/' ? 'index.html' : decodeURIComponent(q.url.split('?')[0])); fs.readFile(f, (e, d) => { if (e) { r.writeHead(404); r.end(); return; } r.writeHead(200, { 'Content-Type': types[path.extname(f)] || 'application/octet-stream' }); r.end(d); }); }).listen(8791);
const browser = await chromium.launch({ args: ['--use-angle=swiftshader', '--enable-unsafe-swiftshader', '--ignore-gpu-blocklist'] });
const page = await browser.newPage({ viewport: { width: 1280, height: 720 } });
const errors = []; page.on('pageerror', e => errors.push(String(e))); page.on('console', m => m.type() === 'error' && errors.push(m.text()));
await page.goto('http://127.0.0.1:8791/?frame=0');
await page.waitForFunction(() => window.viewerReady === true, null, { timeout: 60000 });
const n = await page.evaluate(() => window.frameCount);
const only = process.argv[2] ? process.argv[2].split(',').map(Number) : null;
fs.mkdirSync('frames', { recursive: true });
const list = only || [...Array(n).keys()];
const t0 = Date.now();
for (const i of list) {
  await page.evaluate(i => window.renderFrame(i), i);
  await page.locator('#stage').screenshot({ path: `frames/f${String(i).padStart(5, '0')}.jpg`, type: 'jpeg', quality: 92 });
  if (i % 300 === 0) console.log(`frame ${i}/${n} ${((Date.now() - t0) / 1000).toFixed(0)}s`);
}
console.log('errors:', errors.length ? errors : 'none');
await browser.close(); server.close();
