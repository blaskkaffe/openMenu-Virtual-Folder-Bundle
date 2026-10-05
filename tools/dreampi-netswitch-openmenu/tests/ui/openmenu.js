// The openMenu link module in a real browser (run against a demo server started with FAKEPLAYERS=1): a seeded game list and a
// heartbeat, then the box, the search, a Join for a player whose game is on the card, and a launch that is queued.
const { chromium } = require('playwright');
const BASE = 'http://127.0.0.1:' + (process.env.PORT || 8760);
let failed = 0;
const ok = (cond, what) => { console.log((cond ? 'ok   ' : 'FAIL ') + what); if (!cond) failed++; };
const settle = ms => new Promise(r => setTimeout(r, ms));
const UPLOAD = '#openmenu-games 1 abc12345 3\nPSO-1\t1\t1/1\tU\t\tPhantasy Star Online (USA)\nMK51035\t2\t1/1\tU\t\tCrazy Taxi\nT1234N\t3\t1/1\tU\t\tOuttrigger\n';
(async () => {
  await fetch(BASE + '/openmenu/games', { method: 'POST', headers: { 'X-Requested-With': 'openMenu' }, body: UPLOAD });
  await fetch(BASE + '/openmenu/poll?v=1&n=3&h=abc12345');
  const browser = await chromium.launch();
  const page = await browser.newPage({ viewport: { width: 420, height: 1200 } });
  const errors = [];
  page.on('pageerror', e => errors.push(String(e)));
  page.on('console', m => { if (m.type() === 'error') errors.push(m.text()); });
  page.on('dialog', d => d.accept());
  await page.goto(BASE + '/', { waitUntil: 'networkidle' }); await settle(2500);
  const box = page.locator('.dbox[data-box="openmenu"] .now');
  ok(await box.count() === 1, 'the openMenu box is on the dashboard');
  ok(/Connected/.test(await box.locator(':scope > b').textContent()), 'it says Connected');
  await box.click({ position: { x: 20, y: 10 } }); await settle(800);
  ok(/3 games on the card/.test(await box.textContent()), 'the note counts the games');
  const rows = box.locator('.wlist .p');
  ok(await rows.count() >= 4, 'opened, it lists the card games and a Join (' + await rows.count() + ')');
  ok(/Join/.test(await box.textContent()) && /Alice/.test(await box.textContent()), 'a player in a game on the card gets a Join button');
  ok(!/Dave/.test(await box.textContent()), 'a player whose game is not on the card is not listed');
  await box.locator('input').fill('taxi'); await settle(400);
  ok(await box.locator('.wlist').nth(1).locator('.p').count() === 1, 'the search narrows the list');
  await box.locator('.wlist').nth(1).locator('.pill-s').click(); await settle(1500);
  const view = await (await fetch(BASE + '/openmenu/view')).json();
  ok(view.busy && /Crazy Taxi/.test(view.note), 'Start queues a launch (' + view.note + ')');
  const poll = await (await fetch(BASE + '/openmenu/poll?v=1&n=3&h=abc12345')).text();
  ok(/LAUNCH MK51035/.test(poll), 'openMenu is handed the product ID');
  await page.screenshot({ path: '/tmp/dpns-openmenu.png' });
  ok(errors.length === 0, 'no console errors' + (errors.length ? ': ' + errors.join(' | ') : ''));
  await browser.close();
  process.exit(failed ? 1 : 0);
})();
