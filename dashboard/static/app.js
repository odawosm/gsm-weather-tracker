// Polls the Flask API and fills the page. No framework, Chart.js for the history chart.
const $ = (id) => document.getElementById(id);
const fmt = (v, digits = 1, unit = '') => (v == null ? '–' : `${Number(v).toFixed(digits)}${unit}`);
const signed = (v, digits = 1, unit = '') => (v == null ? '–' : `${v > 0 ? '+' : ''}${Number(v).toFixed(digits)}${unit}`);
const esc = (s) => String(s ?? '').replace(/[&<>"']/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));
const time = (iso) => (iso ? iso.replace('T', ' ').slice(5, 16) : '–');

async function getJSON(url) {
  const r = await fetch(url);
  if (!r.ok) throw new Error(`${url}: ${r.status}`);
  return r.json();
}

function setConn(text, cls) {
  $('conn').textContent = text;
  $('conn').className = `pill ${cls}`;
}

async function refreshData() {
  let d;
  try {
    d = await getJSON('/api/data');
  } catch {
    setConn('Dashboard server offline', 'bad');
    return;
  }
  setConn(d.serial.connected ? `Arduino: ${d.serial.port}` : 'Arduino not connected', d.serial.connected ? 'ok' : 'bad');

  const gw = d.gateway;
  $('gw').hidden = !gw.enabled;
  if (gw.enabled) {
    $('gw').textContent = gw.ok === null ? 'SMS phone: connecting…' : gw.ok ? 'SMS phone: ok' : 'SMS phone: not reachable';
    $('gw').className = `pill ${gw.ok ? 'ok' : gw.ok === false ? 'bad' : ''}`;
    $('gw').title = gw.error || '';
  }

  const s = d.sensor;
  $('s-t').textContent = fmt(s?.t, 1, ' °C');
  $('s-h').textContent = fmt(s?.h, 0, ' %');
  $('s-light').textContent = fmt(s?.light, 0, ' %');
  $('s-rain').textContent = s?.rain ?? '–';
  $('p-conn').textContent = !gw.enabled ? 'off' : gw.ok === null ? 'connecting…' : gw.ok ? 'connected' : 'not reachable';
  $('p-conn').title = gw.error || '';
  $('p-conn').className = `value ${gw.enabled && gw.ok ? 'ok' : gw.enabled && gw.ok === false ? 'bad' : ''}`;
  if (checkingPhone && gw.ok !== null) {  // first result after Save
    checkingPhone = false;
    $('gw-msg').textContent = gw.ok ? 'Saved. Phone connected.' : `Saved, but the phone is not reachable: ${gw.error}`;
  }
  $('s-age').textContent = s ? `Last reading ${time(s.ts)}` : 'No readings yet';

  const o = d.online;
  const cur = o.current || {};
  $('o-t').textContent = fmt(cur.temperature_2m, 1, ' °C');
  $('o-h').textContent = fmt(cur.relative_humidity_2m, 0, ' %');
  $('o-p').textContent = fmt(cur.precipitation, 1, ' mm');
  $('o-hourly').innerHTML = o.hourly.map((f) =>
    `<tr><td>${esc(f.target_time.slice(11))}</td><td>${fmt(f.temp, 1, ' °C')}</td><td>${fmt(f.humidity, 0, ' %')}</td>` +
    `<td>${fmt(f.rain_prob, 0, ' %')}</td><td>${fmt(f.precipitation, 1, ' mm')}</td></tr>`).join('');
  $('o-age').textContent = o.error
    ? `Open-Meteo error: ${o.error}${o.fetched_at ? ` (showing data from ${time(o.fetched_at)})` : ''}`
    : o.fetched_at ? `Open-Meteo, fetched ${time(o.fetched_at)}` : 'Fetching…';

  const c = d.comparison || {};
  $('c-ts').textContent = fmt(c.t_sensor, 1, ' °C');
  $('c-to').textContent = fmt(c.t_online, 1, ' °C');
  $('c-td').textContent = signed(c.t_diff, 1, ' °C');
  $('c-hs').textContent = fmt(c.h_sensor, 0, ' %');
  $('c-ho').textContent = fmt(c.h_online, 0, ' %');
  $('c-hd').textContent = signed(c.h_diff, 0, ' %');

  $('rain-level').textContent = d.rain.level;
  $('rain-level').className = `level ${d.rain.level}`;
  $('rain-reasons').innerHTML = d.rain.reasons.length
    ? d.rain.reasons.map((r) => `<li>${esc(r)}</li>`).join('')
    : '<li>No rain signs</li>';

  // SMS text comes from anyone who texts the station: always escape it.
  $('sms').innerHTML = d.sms.length
    ? d.sms.map((m) => `<tr><td>${time(m.ts)}</td><td>${esc(m.direction)}</td><td>${esc(m.number)}</td><td>${esc(m.text)}</td></tr>`).join('')
    : '<tr><td colspan="4" class="muted">No SMS yet</td></tr>';
}

const TEMP_MIN_SPAN = 6;  // °C; raise it for a calmer line, lower it to see small changes

const chart = new Chart($('chart'), {
  type: 'line',
  data: {
    labels: [],
    datasets: [
      { label: 'Temperature (°C)', data: [], yAxisID: 'yT', borderColor: '#e4572e', borderWidth: 2 },
      { label: 'Humidity (%)', data: [], yAxisID: 'yP', borderColor: '#2e86de', borderWidth: 2 },
      { label: 'Light (%)', data: [], yAxisID: 'yP', borderColor: '#d4a017', borderWidth: 2 },
    ],
  },
  options: {
    animation: false,
    maintainAspectRatio: false,
    interaction: { mode: 'index', intersect: false },
    elements: { point: { radius: 0 } },
    scales: {
      x: { ticks: { maxTicksLimit: 12 } },
      yT: { position: 'left', title: { display: true, text: '°C' } },
      yP: { position: 'right', min: 0, max: 100, title: { display: true, text: '%' }, grid: { drawOnChartArea: false } },
    },
  },
});

async function refreshHistory() {
  try {
    const rows = await getJSON('/api/history?hours=24');
    chart.data.labels = rows.map((r) => r.bucket.slice(11));
    chart.data.datasets[0].data = rows.map((r) => r.t);
    // Show at least TEMP_MIN_SPAN °C: the DHT11 flickers ±0.2 °C (accuracy ±2 °C), which a self-scaling
    // axis stretches over the full height. suggestedMin/Max still widen when the data goes beyond them.
    const t = rows.map((r) => r.t).filter((v) => v != null);
    if (t.length) {
      const mid = (Math.min(...t) + Math.max(...t)) / 2;
      chart.options.scales.yT.suggestedMin = Math.floor(mid - TEMP_MIN_SPAN / 2);
      chart.options.scales.yT.suggestedMax = Math.ceil(mid + TEMP_MIN_SPAN / 2);
    }
    chart.data.datasets[1].data = rows.map((r) => r.h);
    chart.data.datasets[2].data = rows.map((r) => r.light);
    chart.update();
  } catch { /* server offline: refreshData already shows it */ }
}

async function refreshAccuracy() {
  try {
    const a = await getJSON('/api/accuracy');
    $('acc').innerHTML = [['24 h', a['24h']], ['7 days', a['7d']]].map(([name, r]) =>
      `<tr><th>${name}</th><td>${r.hours}</td><td>${fmt(r.temp_mae, 1, ' °C')}</td><td>${fmt(r.hum_mae, 0, ' %')}</td>` +
      `<td>${fmt(r.rain_match_pct, 0, ' %')}</td></tr>`).join('');
  } catch { /* ignore */ }
}

let checkingPhone = false;  // true from Save until the server reports whether the new phone answers

async function loadGateway() {
  try {
    const g = await getJSON('/api/gateway');
    const f = $('gw-form');
    f.url.value = g.url.replace(/^http:\/\//, '');
    f.user.value = g.user;
    f.password.value = '';
    f.password.placeholder = g.has_password ? 'unchanged' : '';
    f.authorized.value = g.authorized.join(', ');
  } catch { /* server offline: refreshData already shows it */ }
}

$('gw-form').addEventListener('submit', async (e) => {
  e.preventDefault();
  try {
    const r = await fetch('/api/gateway', {
      method: 'POST', headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(Object.fromEntries(new FormData(e.target))),
    });
    const res = await r.json();
    $('gw-msg').textContent = r.ok ? (res.url ? 'Saved. Checking the phone…' : 'Saved. Phone backup is off.') : res.error;
    if (r.ok) {
      checkingPhone = Boolean(res.url);
      loadGateway();
      setTimeout(refreshData, 1000);  // the server tries the new phone at once; don't wait for the 5 s poll
    }
  } catch {
    $('gw-msg').textContent = 'Could not save: dashboard server offline';
  }
});

// Tell the server the page is closing; it stops shortly after unless the page comes back (reload).
addEventListener('pagehide', () => navigator.sendBeacon('/api/bye'));

refreshData();
refreshHistory();
refreshAccuracy();
loadGateway();
setInterval(refreshData, 5000);
setInterval(refreshHistory, 60000);
setInterval(refreshAccuracy, 60000);
