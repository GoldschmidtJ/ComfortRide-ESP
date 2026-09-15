#include "web/html_pages.h"
#include "web/web_ui.h"
#include <WebServer.h>

String getTopBarJs() { return String(R"rawliteral(
<script>
let statusErrCount = 0;
let statusBusy = false;
function updateSysStatus(){
  if (localStorage.getItem('ui_show_topbar') === 'false') {
    const tb = document.querySelector('.top-bar-sticky');
    if (tb) tb.style.display = 'none';
  } else {
    const tb = document.querySelector('.top-bar-sticky');
    if (tb) tb.style.display = 'flex';
  }

  if (statusBusy) return; // не наслаиваем запросы поверх незавершённых — иначе "connection reset by peer"
  statusBusy = true;
  fetch("/status/sys").then(r=>r.json()).then(d=>{
    const cpuEl = document.getElementById("tbCpu");
    if(cpuEl) cpuEl.innerText = (d.cpu || 0) + "%";

    const ramEl = document.getElementById("tbRam");
    if(ramEl) ramEl.innerText = (d.ram_pct || 0) + "%";
    const ramKbEl = document.getElementById("tbRamKb");
    if(ramKbEl && d.ram_free_kb !== undefined) ramKbEl.innerText = "(" + d.ram_free_kb + "k)";

    const romEl = document.getElementById("tbRom");
    if(romEl && d.rom_sketch_kb !== undefined) romEl.innerText = d.rom_sketch_kb + "k/" + d.rom_total_kb + "k";

    const dot = document.getElementById("tbWifiDot");
    const txt = document.getElementById("tbWifiTxt");
    if(dot && txt){
      dot.className = "tb-dot ";
      if(d.wifi_mode === "STA"){
        dot.className += "dot-green";
        let display = d.wifi_ssid || "WiFi";
        if(d.wifi_ip) display += " (" + d.wifi_ip + ")";
        txt.innerText = display;
        txt.title = "Доступен по http://" + (d.mdns_host || "openbike.local") + " или http://" + (d.wifi_ip || "");
      } else if(d.wifi_mode === "AP"){
        dot.className += "dot-yellow";
        txt.innerText = "AP: " + (d.wifi_ssid || "Bike") + " (" + (d.wifi_ip || "192.168.4.1") + ")";
      } else {
        dot.className += "dot-red";
        txt.innerText = "Подключение...";
      }
    }
    
    // Update temperature display
    const tempEl = document.getElementById("tbTemp");
    if(tempEl && d.temp !== undefined) {
      tempEl.innerText = d.temp + "°C";
      if (d.temp > 75) {
        tempEl.style.color = "#e74c3c";
      } else if (d.temp > 60) {
        tempEl.style.color = "#f39c12";
      } else {
        tempEl.style.color = "#eee";
      }
    } else if (tempEl) {
      tempEl.innerText = "--°C";
    }

    statusErrCount = 0;
  }).catch(e=>{
    statusErrCount++;
    const cpu = document.getElementById("tbCpu"); if(cpu) cpu.innerText = "ERR";
    const ram = document.getElementById("tbRam"); if(ram) ram.innerText = "ERR";
    const temp = document.getElementById("tbTemp"); if(temp) temp.innerText = "ERR";
    const wifi = document.getElementById("tbWifiTxt"); if(wifi) wifi.innerText = "ERR";
    if (statusErrCount >= 5) {
      location.reload();
    }
  }).finally(()=>{statusBusy=false;});
}
setInterval(updateSysStatus, 2000);
updateSysStatus();
</script>
)rawliteral"); }

String getUpdatePageHtml() {
  String html = R"rawliteral(
<!DOCTYPE html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Обновление прошивки</title>
<style>
)rawliteral";
  html += getTopBarCss();
  html += getSettingsCss();
  html += R"rawliteral(
body{max-width:400px}.update-hint{color:var(--ui-muted);font-size:13px}input[type=file]{padding:8px}</style>
</head><body>
)rawliteral";
  html += getTopBarHtml();
  html += R"rawliteral(

<p><a class="back-link" href="/">&larr; Меню</a></p>
<h1>Загрузить прошивку (.bin)</h1>
<p class="update-hint">В Arduino IDE: Sketch &rarr; Export Compiled Binary — появится .bin рядом со скетчем. Выбери его тут и жми "Залить". Займёт секунд 20-30, плата сама перезагрузится.</p>
<form method="POST" action="/update" enctype="multipart/form-data">
<input type="file" name="update" accept=".bin">
<button type="submit">Залить</button>
</form>
)rawliteral";
  html += getTopBarJs();
  html += R"rawliteral(
</body></html>
)rawliteral";
  return html;
}

void sendDebugPage(WebServer &server) {
  static const char page[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Отладка</title>
<style>
:root{--ui-bg:#101214;--ui-card:#191c20;--ui-button:#252a30;--ui-border:#3b424a;--ui-hover:#30363d;--ui-active:#383f47;--ui-text:#eee;--ui-muted:#8b949e;--ui-focus:#aeb6bf;--ui-accent:#4a90d9;--ui-success:#2ecc71;--ui-warning:#f39c12;--ui-danger:#e74c3c;--ui-purple:#9b59b6;--ui-black:#000;--ui-radius:8px;--ui-control-height:44px}
*{box-sizing:border-box}body{background:var(--ui-bg);color:var(--ui-text);font-family:system-ui,-apple-system,"Segoe UI",sans-serif;font-size:16px;line-height:1.45;padding:20px;max-width:760px;margin:auto}h1{font-size:24px;line-height:1.2}h2{font-size:18px;line-height:1.3}
canvas{background:var(--ui-black);border:1px solid var(--ui-border);border-radius:var(--ui-radius);width:100%;max-width:700px;display:block}
.debug-tools{margin-bottom:10px;display:flex;align-items:center;flex-wrap:wrap;gap:8px}.tool-label{font-size:16px}.osc-toggle{margin-left:10px;font-weight:600;font-size:16px;min-height:44px;display:flex;align-items:center;gap:8px}.osc-toggle input{width:20px;height:20px;accent-color:var(--ui-accent)}.tbtn{display:inline-flex;align-items:center;justify-content:center;min-height:var(--ui-control-height);background:var(--ui-button);color:var(--ui-text);border:1px solid var(--ui-border);padding:9px 12px;border-radius:var(--ui-radius);cursor:pointer;margin:0;font:inherit;font-weight:600;transition:background .12s,border-color .12s}.tbtn.selected{background:var(--ui-accent);border-color:var(--ui-accent)}
.tbtn:hover{background:var(--ui-hover)}.tbtn:active{background:var(--ui-active)}.tbtn:focus-visible,a:focus-visible{outline:2px solid var(--ui-focus);outline-offset:2px}
.legend{display:flex;gap:15px;flex-wrap:wrap;margin:10px 0;font-size:13px}
.legend span{display:inline-flex;align-items:center;gap:5px}
.dot{width:10px;height:10px;border-radius:50%;display:inline-block}.dot-in{background:var(--ui-accent)}.dot-out{background:var(--ui-danger)}.dot-brake{background:var(--ui-warning)}.dot-pas{background:var(--ui-success)}.dot-btn{background:var(--ui-purple)}
#vals{font-size:14px;margin-top:10px;line-height:1.6}
.bus-card{max-width:700px;margin-top:22px;padding:14px;border:1px solid var(--ui-border);border-radius:8px;background:var(--ui-card)}.bus-card h2{margin-top:0}.bus-actions{display:flex;gap:8px;flex-wrap:wrap;margin:10px 0}.bus-link{text-decoration:none;display:inline-block}#busStatus{font-size:13px;line-height:1.5;margin:8px 0}#busChart{height:220px}
a{color:var(--ui-accent)}
</style></head><body>
<p><a href="/">&larr; Настройки</a></p>
<h1>Отладка (мини-осциллограф)</h1>
<div class="debug-tools">
  <span class="tool-label">Масштаб времени:</span>
  <button type="button" class="tbtn" onclick="setTimeScale(1)" id="tb1">1с</button>
  <button type="button" class="tbtn" onclick="setTimeScale(3)" id="tb3">3с</button>
  <button type="button" class="tbtn selected" onclick="setTimeScale(5)" id="tb5">5с</button>
  <button type="button" class="tbtn" onclick="setTimeScale(10)" id="tb10">10с</button>
  <button type="button" class="tbtn" onclick="setTimeScale(30)" id="tb30">30с</button>
  <label class="osc-toggle">
    <input type="checkbox" id="chkOscilloscope" onchange="updateOscilloscopeState()"> Запускать осциллограф
  </label>
</div>
<canvas id="chart" width="700" height="320"></canvas>
<div class="legend">
<span><span class="dot dot-in"></span>Газ вход, В</span>
<span><span class="dot dot-out"></span>Газ выход, В</span>
<span><span class="dot dot-brake"></span>Тормоз</span>
<span><span class="dot dot-pas"></span>PAS активен</span>
<span><span class="dot dot-btn"></span>Кнопка PAS</span>
</div>
<div id="vals">Осциллограф отключен. Включите галочку для запуска.</div>
<section class="bus-card">
  <h2>Пассивный сниффер цифровой линии</h2>
  <p>Вход <b>GPIO36</b> (только чтение, без внутренней подтяжки). Подключайте только через согласование уровня до 3,3 В и общий GND.</p>
  <div class="bus-actions">
    <button type="button" class="tbtn" onclick="busCommand(&#39;start&#39;)">Старт</button>
    <button type="button" class="tbtn" onclick="busCommand(&#39;stop&#39;)">Стоп</button>
    <button type="button" class="tbtn" onclick="busCommand(&#39;clear&#39;)">Очистить</button>
    <a class="tbtn bus-link" href="/debug/bus/csv">Скачать CSV</a>
  </div>
  <div id="busStatus">Получение состояния...</div>
  <canvas id="busChart" width="700" height="220"></canvas>
</section>


<script>
const canvas = document.getElementById('chart');
const ctx = canvas ? canvas.getContext('2d') : null;
const W = canvas ? canvas.width : 0, H = canvas ? canvas.height : 0;
const VMAX = 5.0; // шкала по напряжению, В
let currentTimeScaleSec = 5;
let isOscRunning = false;

function setTimeScale(sec) {
  currentTimeScaleSec = sec;
  [1, 3, 5, 10, 30].forEach(s => {
    const el = document.getElementById('tb' + s);
    if (el) {
      el.classList.toggle('selected', s === sec);
    }
  });
}

function updateOscilloscopeState() {
  const checkbox = document.getElementById('chkOscilloscope');
  isOscRunning = checkbox.checked;
  if (isOscRunning) {
    refresh();
  } else {
    document.getElementById('vals').innerText = 'Осциллограф отключен.';
    if (ctx) ctx.clearRect(0,0,W,H);
  }
}

function draw(rawHistory) {
  if (!ctx) return;
  ctx.clearRect(0,0,W,H);
  if (!rawHistory || !rawHistory.length) return;
  const now = rawHistory[rawHistory.length - 1].t;
  const cutoff = now - currentTimeScaleSec * 1000;
  let data = rawHistory.filter(d => d.t >= cutoff);
  if (data.length < 2) data = rawHistory.slice(-2);
  if (data.length < 2) return;

  // сетка
  ctx.strokeStyle = '#222';
  ctx.lineWidth = 1;
  for (let v = 0; v <= VMAX; v++) {
    const y = H - 60 - (v/VMAX)*(H-80);
    ctx.beginPath(); ctx.moveTo(0,y); ctx.lineTo(W,y); ctx.stroke();
    ctx.fillStyle = '#555'; ctx.font = '10px sans-serif';
    ctx.fillText(v.toFixed(1)+'В', 2, y-2);
  }

  const stepX = W / (data.length - 1);
  function plotV(key, color) {
    ctx.strokeStyle = color; ctx.lineWidth = 2; ctx.beginPath();
    data.forEach((d,i) => {
      const x = i*stepX;
      const y = H - 60 - (d[key]/VMAX)*(H-80);
      if (i===0) ctx.moveTo(x,y); else ctx.lineTo(x,y);
    });
    ctx.stroke();
  }
  function plotBool(key, color, baseY) {
    ctx.strokeStyle = color; ctx.lineWidth = 2; ctx.beginPath();
    data.forEach((d,i) => {
      const x = i*stepX;
      const y = d[key] ? baseY - 15 : baseY;
      if (i===0) ctx.moveTo(x,y); else ctx.lineTo(x,y);
    });
    ctx.stroke();
  }

  plotV('in', '#4a90d9');
  plotV('out', '#e74c3c');
  plotBool('brake', '#f39c12', H-25);
  plotBool('pas', '#2ecc71', H-45);
  plotBool('btn', '#9b59b6', H-65);
}

function refresh() {
  if (!isOscRunning) return;
  fetch('/debug/data').then(r=>r.json()).then(data=>{
    if (!isOscRunning) return;
    draw(data);
    if (data.length) {
      const last = data[data.length-1];
      document.getElementById('vals').innerHTML =
        'Газ вход: <b>' + last.in.toFixed(2) + 'В</b> &nbsp;|&nbsp; ' +
        'Газ выход: <b>' + last.out.toFixed(2) + 'В</b> &nbsp;|&nbsp; ' +
        'Тормоз: <b>' + (last.brake ? 'НАЖАТ' : 'отпущен') + '</b> &nbsp;|&nbsp; ' +
        'PAS: <b>' + (last.pas ? ('активен, уровень '+last.lvl) : 'неактивен') + '</b> &nbsp;|&nbsp; ' +
        'Кнопка: <b>' + (last.btn ? 'НАЖАТА' : 'отпущена') + '</b>';
    }
  }).catch(e=>{});
}
setInterval(refresh, 200);
const busCanvas = document.getElementById("busChart");
const busCtx = busCanvas ? busCanvas.getContext("2d") : null;
let busBusy = false;

async function busCommand(cmd) {
  try {
    const r = await fetch("/debug/bus/control?cmd=" + encodeURIComponent(cmd), {method:"POST"});
    if (!r.ok) throw new Error(await r.text());
    await refreshBus();
  } catch (e) { alert("Сниффер: " + e.message); }
}

function drawBus(edges) {
  if (!busCanvas || !busCtx) return;
  const w=busCanvas.width,h=busCanvas.height;
  busCtx.clearRect(0,0,w,h); busCtx.fillStyle="#050505"; busCtx.fillRect(0,0,w,h);
  if (!edges || edges.length < 2) return;
  const first=edges[0][0], last=edges[edges.length-1][0], span=Math.max(1,last-first);

  // Если на один пиксель приходится несколько фронтов, отдельные ступеньки
  // уже неразличимы. Показываем плотность переходов вместо ложного муара.
  if (edges.length > w*2) {
    const density=new Uint16Array(w);
    let maxDensity=1;
    for(let i=1;i<edges.length;i++) {
      const x=Math.min(w-1,Math.max(0,Math.floor((edges[i][0]-first)*w/span)));
      density[x]++;
      if(density[x]>maxDensity) maxDensity=density[x];
    }
    for(let x=0;x<w;x++) {
      if(!density[x]) continue;
      const intensity=Math.sqrt(density[x]/maxDensity);
      busCtx.fillStyle='rgba(46,204,113,'+(0.18+0.82*intensity).toFixed(3)+')';
      busCtx.fillRect(x,20,1,h-40);
    }
    return;
  }

  busCtx.strokeStyle="#2ecc71"; busCtx.lineWidth=1.5; busCtx.beginPath();
  let py=edges[0][1]?35:h-35; busCtx.moveTo(0,py);
  for (let i=1;i<edges.length;i++) {
    const x=(edges[i][0]-first)*w/span, y=edges[i][1]?35:h-35;
    busCtx.lineTo(x,py); busCtx.lineTo(x,y); py=y;
  }
  busCtx.lineTo(w,py); busCtx.stroke();
}

async function fetchJson(url) {
  const response=await fetch(url);
  if (!response.ok) throw new Error("HTTP "+response.status+": "+await response.text());
  return response.json();
}

async function refreshBus() {
  if (busBusy) return; busBusy=true;
  try {
    const status=await fetchJson("/debug/bus/status");
    let text=(status.running?"Запись идёт":"Остановлено")+" · фронтов в памяти: "+status.count+"/"+status.capacity+" · всего: "+status.total;
    if(status.overwritten) text+=" · перезаписано: "+status.overwritten;
    if(status.busy) text+=" · НЕДОСТУПНО: "+status.reason;
    const statusElement=document.getElementById("busStatus");
    if (statusElement) statusElement.innerText=text;
    if(status.count) {
      const data=await fetchJson("/debug/bus/data");
      drawBus(data.edges);
    } else drawBus([]);
  } catch(e) {
    const statusElement=document.getElementById("busStatus");
    if (statusElement) statusElement.innerText="Ошибка: "+e.message;
  } finally { busBusy=false; }
}
setInterval(refreshBus, 1000);
refreshBus();

</script>
</body></html>
)rawliteral";
  server.send_P(200, PSTR("text/html; charset=utf-8"), page, sizeof(page)-1);
}

void sendHubPage(WebServer &server) {
  static const char page[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>OpenBike Controller v0.3.1</title>
<style>
:root{--ui-bg:#101214;--ui-card:#191c20;--ui-button:#252a30;--ui-border:#3b424a;--ui-hover:#30363d;--ui-active:#383f47;--ui-text:#eee;--ui-muted:#8b949e;--ui-focus:#aeb6bf;--ui-accent:#4a90d9;--ui-success:#2ecc71;--ui-warning:#f39c12;--ui-danger:#e74c3c;--ui-radius:8px;--ui-control-height:44px}
*{box-sizing:border-box}body{font-family:system-ui,-apple-system,"Segoe UI",sans-serif;font-size:16px;line-height:1.45;padding:20px;padding-top:max(20px,env(safe-area-inset-top));max-width:560px;margin:auto;background:var(--ui-bg);color:var(--ui-text)}
.top-bar-sticky{position:sticky;top:0;top:env(safe-area-inset-top);z-index:9999;background:var(--ui-card);border-bottom:1px solid var(--ui-border);padding:8px 12px;margin:-20px -20px 15px;font-size:12px;color:var(--ui-muted);display:flex;align-items:center;justify-content:space-between;flex-wrap:wrap;gap:8px;box-shadow:0 2px 8px rgba(0,0,0,.5)}
.tb-item{display:inline-flex;align-items:center;gap:4px;white-space:nowrap}.tb-link{color:var(--ui-accent);text-decoration:none;padding:2px 6px;border-radius:6px;background:var(--ui-button);border:1px solid var(--ui-border)}.tb-link:hover{background:var(--ui-hover);color:var(--ui-text)}.tb-dot{width:8px;height:8px;border-radius:50%;display:inline-block}.dot-green{background:var(--ui-success);box-shadow:0 0 5px var(--ui-success)}.dot-yellow{background:var(--ui-warning);box-shadow:0 0 5px var(--ui-warning)}.dot-red{background:var(--ui-danger)}.dot-gray{background:var(--ui-muted)}
.header{text-align:center;margin-bottom:18px}.header h1{margin:0;font-size:24px;line-height:1.2}.version,.u-muted{color:var(--ui-muted)}.u-small{font-size:12px}.u-dim{opacity:.7}
a.card{display:flex;align-items:center;justify-content:center;min-height:var(--ui-control-height);background:var(--ui-button);color:var(--ui-text);padding:12px 15px;border:1px solid var(--ui-border);border-radius:var(--ui-radius);margin-bottom:10px;text-decoration:none;text-align:center;font-weight:600;transition:background .12s,border-color .12s}
a.card:hover{background:var(--ui-hover)}a.card:active{background:var(--ui-active)}a.primary{background:var(--ui-accent);border-color:var(--ui-accent);color:#fff}a:focus-visible{outline:2px solid var(--ui-focus);outline-offset:2px}
@media(max-width:420px){body{padding-left:12px;padding-right:12px}.top-bar-sticky{margin-left:-12px;margin-right:-12px;justify-content:flex-start}}
</style></head><body>
<div class="top-bar-sticky">
  <div class="tb-item" title="Нагрузка процессора ESP32"><span>CPU:</span> <b id="tbCpu">0%</b></div>
  <div class="tb-item" title="Оперативная память"><span>RAM:</span> <b id="tbRam">0%</b> <span id="tbRamKb" class="u-muted u-small">(0k)</span></div>
  <div class="tb-item" title="Flash-память"><span>ROM:</span> <span id="tbRom" class="u-muted">0k</span></div>
  <a href="/wifi" class="tb-item tb-link" title="Настройки Wi-Fi"><span>WiFi:</span> <span class="tb-dot dot-gray" id="tbWifiDot"></span> <span id="tbWifiTxt">...</span></a>
  <div class="tb-item" title="Температура процессора"><span>Temp:</span> <b id="tbTemp">--°C</b></div>
  <div class="tb-item u-dim" title="Bluetooth не используется"><span>BT:</span> <span class="tb-dot dot-gray"></span> <span>Выкл</span></div>
</div>
<div class="header"><h1>OpenBike Controller</h1><div class="version">v0.3.1</div></div>
<a class="card primary" href="/emulation">Эмуляция дисплея и пульта &rarr;</a>
<a class="card" href="/settings/throttle">Газ &rarr;</a>
<a class="card" href="/settings/pas">PAS &rarr;</a>
<a class="card" href="/settings/cruise">Круиз &rarr;</a>
<a class="card" href="/settings/pins">GPIO &rarr;</a>
<a class="card" href="/settings/events">События &rarr;</a>
<a class="card" href="/wifi">Сеть &rarr;</a>
<a class="card" href="/debug">Отладка &rarr;</a>
<a class="card" href="/system">Система</a>
<script>
let statusBusy=false;
function updateSysStatus(){
  if(statusBusy)return;
  statusBusy=true;
  fetch('/status/sys',{cache:'no-store'}).then(r=>r.json()).then(d=>{
    const set=(id,value)=>{const el=document.getElementById(id);if(el)el.textContent=value};
    set('tbCpu',(d.cpu||0)+'%');
    set('tbRam',(d.ram_pct||0)+'%');
    set('tbRamKb','('+(d.ram_free_kb||0)+'k)');
    set('tbRom',(d.rom_sketch_kb||0)+'k/'+(d.rom_total_kb||0)+'k');
    set('tbTemp',d.temp!==undefined?d.temp+'°C':'--°C');
    const dot=document.getElementById('tbWifiDot'),txt=document.getElementById('tbWifiTxt');
    if(dot&&txt){dot.className='tb-dot '+(d.wifi_mode==='STA'?'dot-green':d.wifi_mode==='AP'?'dot-yellow':'dot-red');txt.textContent=d.wifi_mode==='STA'?(d.wifi_ssid||'WiFi'):d.wifi_mode==='AP'?'AP: '+(d.wifi_ssid||'Bike'):'Подключение...'}
  }).catch(()=>{const dot=document.getElementById('tbWifiDot');if(dot)dot.className='tb-dot dot-red'}).finally(()=>{statusBusy=false});
}
setInterval(updateSysStatus,2000);updateSysStatus();
</script>
</body></html>)rawliteral";
  server.send_P(200, PSTR("text/html; charset=utf-8"), page, sizeof(page)-1);
}

void sendEmulationPage(WebServer &server) {
  static const char page[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>OpenBike Controller v0.3.1</title>
<style>

:root{--ui-bg:#101214;--ui-card:#191c20;--ui-button:#252a30;--ui-border:#3b424a;--ui-hover:#30363d;--ui-active:#383f47;--ui-text:#eee;--ui-muted:#8b949e;--ui-focus:#aeb6bf;--ui-accent:#4a90d9;--ui-success:#2ecc71;--ui-warning:#f39c12;--ui-danger:#e74c3c;--ui-purple:#9b59b6;--ui-dark:#0a0f0d;--ui-black:#000;--ui-led:#ff8c00;--ui-led-glow:#ff7700;--ui-led-off:#1e140a;--ui-radius:8px;--ui-control-height:44px}
.u-muted{color:var(--ui-muted)}.u-small{font-size:12px}.u-dim{opacity:.7}.back-link,a.back{color:var(--ui-accent);text-decoration:none}.hint{color:var(--ui-muted);font-size:13px}.is-hidden{display:none!important}
.top-bar-sticky{position:sticky;top:0;left:0;right:0;z-index:9999;background:var(--ui-card);border-bottom:1px solid var(--ui-border);padding:8px 12px;margin:-20px -20px 15px -20px;font-size:12px;color:var(--ui-muted);display:flex;align-items:center;justify-content:space-between;flex-wrap:wrap;gap:8px;box-shadow:0 2px 8px rgba(0,0,0,.5)}
.tb-item{display:inline-flex;align-items:center;gap:4px;white-space:nowrap}
.tb-link{color:var(--ui-accent);text-decoration:none;padding:2px 6px;border-radius:6px;background:var(--ui-button);border:1px solid var(--ui-border);transition:background .2s,border-color .2s}
.tb-link:hover{background:var(--ui-hover);border-color:var(--ui-muted);color:var(--ui-text)}
.tb-dot{width:8px;height:8px;border-radius:50%;display:inline-block}
.dot-green{background:var(--ui-success);box-shadow:0 0 5px var(--ui-success)}
.dot-yellow{background:var(--ui-warning);box-shadow:0 0 5px var(--ui-warning)}
.dot-red{background:var(--ui-danger)}
.dot-gray{background:var(--ui-muted)}

body{font-family:system-ui,-apple-system,"Segoe UI",sans-serif;font-size:16px;line-height:1.45;padding:20px;max-width:560px;margin:auto;background:var(--ui-bg);color:var(--ui-text)}
.header{text-align:center;margin-bottom:16px}.header h1{margin:0;font-size:24px;line-height:1.2}.header .version{color:var(--ui-muted);font-size:14px}
a.card{display:block;background:var(--ui-button);color:var(--ui-text);padding:15px;border:1px solid var(--ui-border);border-radius:var(--ui-radius);margin-bottom:10px;text-decoration:none;transition:background .2s,border-color .2s}
a.card:hover{background:var(--ui-hover)}a.card:active{background:var(--ui-active)}
.warn{background:var(--ui-card);border:1px solid var(--ui-danger);color:var(--ui-text);padding:12px;border-radius:var(--ui-radius);margin-bottom:16px;font-weight:bold;font-size:13px}

/* LED Matrix Simulator */
.matrix-card{background:var(--ui-card);border:1px solid var(--ui-border);border-radius:12px;padding:10px;margin-bottom:12px;text-align:center;box-shadow:0 4px 12px rgba(0,0,0,.4);transition:all .2s}
.matrix-title{font-size:11px;font-weight:bold;color:var(--ui-muted);letter-spacing:1px;text-transform:uppercase;margin-bottom:8px}
.sim-matrix-area{display:flex;justify-content:center;align-items:center;margin-bottom:8px}
#ledMatrixCanvas{background:var(--ui-black);border:2px solid var(--ui-border);border-radius:6px;box-shadow:inset 0 0 8px rgba(0,0,0,.7);display:block;max-width:100%;height:auto}

/* Компактная панель эмуляции */
.simulator-layout{display:grid;grid-template-columns:minmax(0,1fr) 64px;gap:8px;align-items:stretch;margin-bottom:12px}
.sim-left-col{min-width:0;display:flex;flex-direction:column;gap:8px}
.sim-section-title{font-size:10px;font-weight:900;letter-spacing:1px;text-transform:uppercase;color:var(--ui-muted);margin-bottom:2px}
.sim-btns-group{display:flex;flex-direction:row;gap:6px;width:100%}
.sim-side-btn{flex:1;display:flex;align-items:center;justify-content:center;gap:6px;padding:10px 6px;font-size:13px;font-weight:bold;border-radius:8px;border:1px solid var(--ui-border);background:var(--ui-button);color:var(--ui-text);cursor:pointer;user-select:none;-webkit-user-select:none;touch-action:manipulation;transition:background .1s,border-color .1s}
.sim-btn-ico{font-size:18px;line-height:1}.sim-side-btn:hover{background:var(--ui-hover)}.sim-side-btn:active{background:var(--ui-active)}
.sim-side-btn:focus-visible,.dpad-btn:focus-visible{outline:2px solid var(--ui-focus);outline-offset:2px}
.sim-side-btn.active{background:var(--ui-card);border-color:var(--ui-danger);color:var(--ui-text)}
.sim-side-btn.pedal-btn.active{background:var(--ui-card);border-color:var(--ui-success);color:var(--ui-text)}

.sim-throttle-group{flex:1 1 auto;min-width:0;display:flex;flex-direction:column;align-items:center;justify-content:center;gap:6px;background:var(--ui-card);border:1px solid var(--ui-border);border-radius:10px;padding:8px 6px}
.sim-throttle-label{font-size:10px;font-weight:900;color:var(--ui-muted);text-transform:uppercase;letter-spacing:1px}
.sim-slider-vert{writing-mode:vertical-lr;direction:rtl;-webkit-appearance:slider-vertical;appearance:slider-vertical;width:24px;height:126px;cursor:pointer;accent-color:var(--ui-accent)}
.sim-throttle-val{font-size:12px;font-weight:bold;color:var(--ui-accent);min-width:36px;text-align:center}

/* D-Pad Джойстик */
.joystick-panel{align-self:center;background:var(--ui-card);border:1px solid var(--ui-border);border-radius:12px;padding:7px;width:fit-content;max-width:100%;min-width:0;box-sizing:border-box;margin-bottom:0;box-shadow:0 4px 12px rgba(0,0,0,.35);display:flex;flex-direction:column;justify-content:flex-start}.joystick-panel .sim-section-title,.joystick-panel .joy-screen{width:193px;max-width:100%;box-sizing:border-box}
.joy-screen{background:var(--ui-dark);border:1px solid var(--ui-border);border-radius:10px;padding:8px 10px;margin-bottom:10px;text-align:center;font-family:monospace}
.screen-mode{font-size:12px;font-weight:bold;letter-spacing:1px;color:var(--ui-muted);text-transform:uppercase}.screen-mode.mode-pas{color:var(--ui-success)}.screen-mode.mode-cruise{color:var(--ui-accent)}.screen-mode.mode-off{color:var(--ui-danger)}
.screen-val{font-size:34px;font-weight:900;color:var(--ui-text);margin:4px 0}.screen-status{font-size:10px;color:var(--ui-muted);font-weight:bold;text-transform:uppercase}.screen-status.dirty{color:var(--ui-warning);animation:blink 1s infinite}
@keyframes blink{50%{opacity:0.4}}

.dpad-container{display:grid;grid-template-columns:repeat(3,60px);grid-template-rows:repeat(3,50px);gap:6px;justify-content:center;margin:2px auto 0}
.dpad-btn{background:var(--ui-button);color:var(--ui-text);border:1px solid var(--ui-border);border-bottom-width:2px;border-radius:10px;display:flex;align-items:center;justify-content:center;font-size:19px;font-weight:900;cursor:pointer;user-select:none;-webkit-user-select:none;touch-action:manipulation;transition:background .08s,transform .08s}
.dpad-btn:hover{background:var(--ui-hover)}.dpad-btn:active{transform:translateY(2px);border-bottom-width:2px;background:var(--ui-active)}
.btn-up{grid-column:2;grid-row:1}
.btn-left{grid-column:1;grid-row:2}
.btn-ok{grid-column:2;grid-row:2;background:var(--ui-card);border-color:var(--ui-success);border-bottom-color:var(--ui-success);color:var(--ui-success);font-size:19px}
.btn-ok:hover{background:var(--ui-hover)}.btn-ok:active{background:var(--ui-active)}
.btn-ok.dirty-pulse{background:var(--ui-card);border-color:var(--ui-warning);border-bottom-color:var(--ui-warning);color:var(--ui-warning);animation:pulse 1s infinite}
@keyframes pulse{50%{box-shadow:0 0 14px rgba(243,156,18,0.7)}}
.btn-right{grid-column:3;grid-row:2}
.btn-down{grid-column:2;grid-row:3}
.joy-legend{display:none}
@media(max-width:520px){body{padding:12px}.top-bar-sticky{margin:-12px -12px 12px}.sim-side-btn{padding:8px 5px}.matrix-card{padding:8px}.screen-val{font-size:23px}}
@media(max-width:360px){.simulator-layout{grid-template-columns:minmax(0,1fr) 54px}.joystick-panel .sim-section-title,.joystick-panel .joy-screen{width:158px}.dpad-container{grid-template-columns:repeat(3,49px);grid-template-rows:repeat(3,43px);gap:5px}}
</style>
</head><body>

<div class="top-bar-sticky">
  <div class="tb-item" title="Нагрузка процессора ESP32">
    <span>CPU:</span> <b id="tbCpu">0%</b>
  </div>
  <div class="tb-item" title="Оперативная память (занято / свободно)">
    <span>RAM:</span> <b id="tbRam">0%</b> <span id="tbRamKb" class="u-muted u-small">(0k)</span>
  </div>
  <div class="tb-item" title="Flash память (прошивка / всего)">
    <span>ROM:</span> <span id="tbRom" class="u-muted">0k</span>
  </div>
  <a href="/wifi" class="tb-item tb-link" title="Настройки Wi-Fi">
    <span>WiFi:</span>
    <span class="tb-dot dot-gray" id="tbWifiDot"></span>
    <span id="tbWifiTxt">...</span>
  </a>
  <div class="tb-item" title="Температура процессора">
    <span>Temp:</span> <b id="tbTemp">--°C</b>
  </div>
  <div class="tb-item u-dim" title="Bluetooth (не используется)">
    <span>BT:</span>
    <span class="tb-dot dot-gray"></span>
    <span class="u-muted">Выкл</span>
  </div>
</div>

<div class="header">
  <p><a href="/" class="back-link">&larr; Главное меню</a></p>
  <h1>Эмуляция управления</h1>
  <div class="version">v0.3.1 &bull; 16&times;32 LED Matrix</div>
</div>

<div class="matrix-card" id="simMatrixCard">
  <div class="matrix-title">Эмулятор дисплея 16&times;32</div>
  <div class="sim-matrix-area">
    <canvas id="ledMatrixCanvas" width="320" height="160"></canvas>
  </div>
  <div class="simulator-layout">
    <div class="sim-left-col">
      <div class="joystick-panel" id="joystickPanel">
        <div class="sim-section-title">Джойстик</div>
        <div class="joy-screen" id="joyScreen">
          <div class="screen-mode" id="joyMode">PAS</div>
          <div class="screen-val" id="joyVal">УРОВЕНЬ 1</div>
          <div class="screen-status" id="joyStatus">ПОДТВЕРЖДЕНО</div>
        </div>
        <div class="dpad-container" id="dpadContainer">
          <button type="button" class="dpad-btn btn-up" id="btnUp" title="Увеличить">&#9650;</button>
          <button type="button" class="dpad-btn btn-left" id="btnLeft" title="Режим влево">&#9664;</button>
          <button type="button" class="dpad-btn btn-ok" id="btnOk" title="Применить">OK</button>
          <button type="button" class="dpad-btn btn-right" id="btnRight" title="Режим вправо">&#9654;</button>
          <button type="button" class="dpad-btn btn-down" id="btnDown" title="Уменьшить">&#9660;</button>
        </div>
      </div>
      <div class="sim-btns-group" id="simControlsPanel">
        <button type="button" class="sim-side-btn" id="btnSimBrake">
          <span class="sim-btn-ico">[ • ]</span><span>Тормоз</span>
        </button>
        <button type="button" class="sim-side-btn pedal-btn" id="btnSimPedal">
          <span class="sim-btn-ico">&#129461;</span><span>Педали</span>
        </button>
      </div>
      <div class="sim-btns-group" id="simLightsPanel">
        <button type="button" class="sim-side-btn" id="btnSimTurnL" title="Левый поворотник">
          <span class="sim-btn-ico">&#8626;</span><span>Пов. L</span>
        </button>
        <button type="button" class="sim-side-btn" id="btnSimTurnR" title="Правый поворотник">
          <span class="sim-btn-ico">&#8627;</span><span>Пов. R</span>
        </button>
        <button type="button" class="sim-side-btn" id="btnSimLight" title="Циклический выбор режима света">
          <span class="sim-btn-ico">&#9788;</span><span id="lblSimLight">Свет</span>
        </button>
      </div>
    </div>
    <div class="sim-throttle-group">
      <span class="sim-throttle-label">Газ</span>
      <input type="range" min="0" max="100" value="0" orient="vertical" class="sim-slider-vert" id="simGas">
      <span class="sim-throttle-val" id="lblGas">0%</span>
    </div>
  </div>
</div>

<script>
let statusErrCount = 0;
let statusBusy = false;
function updateSysStatus(){
  if (localStorage.getItem('ui_show_topbar') === 'false') {
    const tb = document.querySelector('.top-bar-sticky');
    if (tb) tb.style.display = 'none';
  } else {
    const tb = document.querySelector('.top-bar-sticky');
    if (tb) tb.style.display = 'flex';
  }

  if (statusBusy) return; // не наслаиваем запросы поверх незавершённых — иначе "connection reset by peer"
  statusBusy = true;
  fetch("/status/sys").then(r=>r.json()).then(d=>{
    const cpuEl = document.getElementById("tbCpu");
    if(cpuEl) cpuEl.innerText = (d.cpu || 0) + "%";

    const ramEl = document.getElementById("tbRam");
    if(ramEl) ramEl.innerText = (d.ram_pct || 0) + "%";
    const ramKbEl = document.getElementById("tbRamKb");
    if(ramKbEl && d.ram_free_kb !== undefined) ramKbEl.innerText = "(" + d.ram_free_kb + "k)";

    const romEl = document.getElementById("tbRom");
    if(romEl && d.rom_sketch_kb !== undefined) romEl.innerText = d.rom_sketch_kb + "k/" + d.rom_total_kb + "k";

    const dot = document.getElementById("tbWifiDot");
    const txt = document.getElementById("tbWifiTxt");
    if(dot && txt){
      dot.className = "tb-dot ";
      if(d.wifi_mode === "STA"){
        dot.className += "dot-green";
        let display = d.wifi_ssid || "WiFi";
        if(d.wifi_ip) display += " (" + d.wifi_ip + ")";
        txt.innerText = display;
        txt.title = "Доступен по http://" + (d.mdns_host || "openbike.local") + " или http://" + (d.wifi_ip || "");
      } else if(d.wifi_mode === "AP"){
        dot.className += "dot-yellow";
        txt.innerText = "AP: " + (d.wifi_ssid || "Bike") + " (" + (d.wifi_ip || "192.168.4.1") + ")";
      } else {
        dot.className += "dot-red";
        txt.innerText = "Подключение...";
      }
    }

    // Update temperature display
    const tempEl = document.getElementById("tbTemp");
    if(tempEl && d.temp !== undefined) {
      tempEl.innerText = d.temp + "°C";
      if (d.temp > 75) {
        tempEl.style.color = "#e74c3c";
      } else if (d.temp > 60) {
        tempEl.style.color = "#f39c12";
      } else {
        tempEl.style.color = "#eee";
      }
    } else if (tempEl) {
      tempEl.innerText = "--°C";
    }

    statusErrCount = 0;
  }).catch(e=>{
    statusErrCount++;
    const cpu = document.getElementById("tbCpu"); if(cpu) cpu.innerText = "ERR";
    const ram = document.getElementById("tbRam"); if(ram) ram.innerText = "ERR";
    const temp = document.getElementById("tbTemp"); if(temp) temp.innerText = "ERR";
    const wifi = document.getElementById("tbWifiTxt"); if(wifi) wifi.innerText = "ERR";
    if (statusErrCount >= 5) {
      location.reload();
    }
  }).finally(()=>{statusBusy=false;});
}
setInterval(updateSysStatus, 2000);
updateSysStatus();
</script>

<script>
let activeMode = "off";
let activePasLvl = 0;
let activeCruiseLvl = 0;
const pasMax = 5;
const cruiseMax = 5;

const cfgThrottleInMin = 1.10;
const cfgThrottleInMax = 4.10;
const cfgThrottleOutMin = 1.10;
const cfgThrottleOutMax = 4.10;

const cfgCruiseConfirmThrottle = false;
const cfgCruiseAfterBraking = 1;
const cfgCruiseAfterThrottle = 2;

let draftMode = (activeMode === "off") ? "pas" : activeMode;
let draftPasLvl = activePasLvl;
let draftCruiseLvl = activeCruiseLvl;
let draftSettingIdx = 0;
let draftSettingEditing = false;
let isDirty = false;
let userInteractingUntil = 0;
let applyInProgressUntil = 0;
let applyInProgress = false;
let simCruiseEngaged = (activeMode === "cruise" && activeCruiseLvl > 0);
let simCruisePendingResume = false;
let simCruiseConfirmRequired = false;
let simCruiseReleaseSeen = false;
let arrowBounceUntil = 0;
let arrowBounceDir = 0;

// Screen Wake Lock API
let wakeLock = null;
async function requestWakeLock() {
  if ('wakeLock' in navigator) {
    try {
      wakeLock = await navigator.wakeLock.request('screen');
      wakeLock.addEventListener('release', () => { wakeLock = null; });
    } catch (err) {
      console.log('WakeLock error:', err);
    }
  }
}
requestWakeLock();
document.addEventListener('visibilitychange', () => {
  if (document.visibilityState === 'visible') requestWakeLock();
});

function vib(pattern = 20) {
  if (navigator.vibrate) {
    try { navigator.vibrate(pattern); } catch (e) {}
  }
}

function checkUiVisibility() {
  const showMatrix = localStorage.getItem("ui_show_matrix") !== "false";
  const showControls = localStorage.getItem("ui_show_controls") !== "false";
  const showScreen = localStorage.getItem("ui_show_screen") !== "false";
  const showDpad = localStorage.getItem("ui_show_dpad") !== "false";

  const matrixEl = document.getElementById("simMatrixCard");
  const controlsEl = document.getElementById("simControlsPanel");
  const screenEl = document.getElementById("joyScreen");
  const dpadEl = document.getElementById("dpadContainer");
  const joyPanel = document.getElementById("joystickPanel");

  if (matrixEl) matrixEl.style.display = showMatrix ? "block" : "none";
  if (controlsEl) controlsEl.style.display = showControls ? "flex" : "none";
  if (screenEl) screenEl.style.display = showScreen ? "block" : "none";
  if (dpadEl) dpadEl.style.display = showDpad ? "grid" : "none";
  if (joyPanel) {
    joyPanel.style.display = (!showScreen && !showDpad) ? "none" : "block";
  }
}
document.addEventListener("DOMContentLoaded", checkUiVisibility);
checkUiVisibility();

// ================= 16x32 LED MATRIX RENDERING ENGINE =================
const canvas = document.getElementById("ledMatrixCanvas");
const ctx = canvas ? canvas.getContext("2d") : null;
const MATRIX_ROWS = 16, MATRIX_COLS = 32;
let matrixGrid = [];
for (let r = 0; r < MATRIX_ROWS; r++) matrixGrid[r] = new Uint8Array(MATRIX_COLS);

// Центральная зона: P/C — единая сетка 6x11 и равномерный штрих 2 пикселя.
const BIG_GLYPHS = {
  'P': [
    [0,1,1,1,1,0],
    [1,1,1,1,1,1],
    [1,1,0,0,1,1],
    [1,1,0,0,1,1],
    [1,1,1,1,1,1],
    [1,1,1,1,1,0],
    [1,1,0,0,0,0],
    [1,1,0,0,0,0],
    [1,1,0,0,0,0],
    [1,1,0,0,0,0],
    [1,1,0,0,0,0]
  ],
  'C': [
    [0,1,1,1,1,0],
    [1,1,1,1,1,1],
    [1,1,0,0,1,1],
    [1,1,0,0,0,0],
    [1,1,0,0,0,0],
    [1,1,0,0,0,0],
    [1,1,0,0,0,0],
    [1,1,0,0,0,0],
    [1,1,0,0,1,1],
    [1,1,1,1,1,1],
    [0,1,1,1,1,0]
  ]
};

// Компактные цифры 3x5: три столбца, пять строк.
</script>
<script src="/matrix_graphics.js"></script>
<script>
const SETTINGS_MENU = [
  { id: 'in_min', name: 'THROTTLE IN MIN', unit: 'V', step: 0.05, min: 0.0, max: 4.5, val: cfgThrottleInMin },
  { id: 'in_max', name: 'THROTTLE IN MAX', unit: 'V', step: 0.05, min: 0.5, max: 5.0, val: cfgThrottleInMax },
  { id: 'out_min', name: 'THROTTLE OUT MIN', unit: 'V', step: 0.05, min: 0.0, max: 4.5, val: cfgThrottleOutMin },
  { id: 'out_max', name: 'THROTTLE OUT MAX', unit: 'V', step: 0.05, min: 0.5, max: 5.0, val: cfgThrottleOutMax }
];

// Тормоз: квадратные скобки с точкой внутри.
const ICON_BRAKE = [0b10001, 0b10001, 0b10101, 0b10001, 0b10001];
// Гудок (бибика): динамик со звуковыми волнами.
const ICON_HORN = [0b00100, 0b01110, 0b11111, 0b01110, 0b00100];
// Единая иконка света 5x5: одна «фара» показывает все 4 режима цикла
// (0 ВЫКЛ — пустой корпус, 1 ДХО — центральная полоса,
//  2 БЛИЖНИЙ — корпус с заливкой, 3 БЛ+ДХО — полностью залитая).
// Битмапы синхронизированы с src/light_logic.h (lightIconRow) — покрыты юнит-тестами.
const ICON_LIGHT_MODES = [
  [0b00000, 0b00000, 0b00100, 0b00000, 0b00000],
  [0b00000, 0b00100, 0b01110, 0b00100, 0b00000],
  [0b00000, 0b01110, 0b11111, 0b01110, 0b00000],
  [0b00100, 0b01110, 0b11111, 0b01110, 0b00100]
];
// Стрелки поворотников 5x5 — зеркальная пара (остриё в сторону поворота).
// Синхронизированы с src/light_logic.h (turnArrowRow).
const TURN_ARROW_5X5_L = [0b00100, 0b01100, 0b11111, 0b01100, 0b00100];
const TURN_ARROW_5X5_R = [0b00100, 0b00110, 0b11111, 0b00110, 0b00100];
function drawTurnArrow5x5(left, sr, sc) {
  const g = left ? TURN_ARROW_5X5_L : TURN_ARROW_5X5_R;
  for (let r = 0; r < 5; r++) {
    for (let c = 0; c < 5; c++) {
      if ((g[r] >> (4 - c)) & 1) setMatrixPixel(sr + r, sc + c, 1);
    }
  }
}
const ICON_PEDAL_FRAMES = [
  [0b11000, 0b01000, 0b00100, 0b00010, 0b00011],
  [0b00000, 0b11000, 0b01110, 0b00011, 0b00000],
  [0b00000, 0b00000, 0b11111, 0b00000, 0b00000],
  [0b00000, 0b00011, 0b01110, 0b11000, 0b00000],
  [0b00011, 0b00010, 0b00100, 0b01000, 0b11000],
  [0b00000, 0b00011, 0b01110, 0b11000, 0b00000],
  [0b00000, 0b00000, 0b11111, 0b00000, 0b00000],
  [0b00000, 0b11000, 0b01110, 0b00011, 0b00000]
];

function clearMatrix() {
  for (let r = 0; r < MATRIX_ROWS; r++) matrixGrid[r].fill(0);
}
function setMatrixPixel(r, c, val) {
  if (r >= 0 && r < MATRIX_ROWS && c >= 0 && c < MATRIX_COLS) matrixGrid[r][c] = val ? 1 : 0;
}
function drawBigLetter(k, sr, sc) {
  const g = BIG_GLYPHS[k]; if (!g) return;
  for (let r = 0; r < g.length; r++) {
    for (let c = 0; c < g[r].length; c++) {
      if (g[r][c]) setMatrixPixel(sr + r, sc + c, 1);
    }
  }
}
function drawDigit3x5ToBuffer(dChar, buf) {
  const g = DIGIT_GLYPHS[dChar];
  if (!g) return;
  for (let c = 0; c < 3; c++) {
    for (let r = 0; r < 5; r++) {
      if ((g[c] >> r) & 1) buf[r][c] = 1;
    }
  }
}

function drawArrow5x2(arrowMatrix, sr, sc, hollowCenter = false) {
  for (let r = 0; r < arrowMatrix.length; r++) {
    for (let c = 0; c < arrowMatrix[r].length; c++) {
      if (hollowCenter && r === 0 && c === 2) continue;
      if (arrowMatrix[r][c]) setMatrixPixel(sr + r, sc + c, 1);
    }
  }
}
function drawIcon5x5(b, sr, sc) {
  for (let r = 0; r < 5; r++) {
    for (let c = 0; c < 5; c++) {
      if ((b[r] >> (4 - c)) & 1) setMatrixPixel(sr + r, sc + c, 1);
    }
  }
}
function drawGear7x7(sr, sc) {
  for (let r = 0; r < 7; r++) {
    for (let c = 0; c < 7; c++) {
      if (ICON_GEAR_7X7[r][c]) setMatrixPixel(sr + r, sc + c, 1);
    }
  }
}

// ================= BOOT ANIMATION: PAC-MAN INTRO =================
let bootAnimationActive = false;
let bootAnimationComplete = false;

// Pac-Man 16x16 (БОЛЬШОЙ, рот вправо →, с глазом)
const PACMAN_OPEN = [
  [0,0,0,0,1,1,1,1,1,1,1,1,0,0,0,0],
  [0,0,1,1,1,1,1,1,1,1,1,1,1,1,0,0],
  [0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0],
  [1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1],
  [1,1,1,1,0,0,1,1,1,1,1,1,0,0,0,0],
  [1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0],
  [1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0],
  [1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0],
  [1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0],
  [1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0],
  [1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0],
  [1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0],
  [1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1],
  [0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0],
  [0,0,1,1,1,1,1,1,1,1,1,1,1,1,0,0],
  [0,0,0,0,1,1,1,1,1,1,1,1,0,0,0,0]
];

const PACMAN_CLOSED = [
  [0,0,0,0,1,1,1,1,1,1,1,1,0,0,0,0],
  [0,0,1,1,1,1,1,1,1,1,1,1,1,1,0,0],
  [0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0],
  [1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1],
  [1,1,1,1,0,0,1,1,1,1,1,1,1,1,1,1],
  [1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1],
  [1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1],
  [1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1],
  [1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1],
  [1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1],
  [1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1],
  [1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1],
  [1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1],
  [0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0],
  [0,0,1,1,1,1,1,1,1,1,1,1,1,1,0,0],
  [0,0,0,0,1,1,1,1,1,1,1,1,0,0,0,0]
];

// Велосипедист 7x7 (палочковый человечек с колёсами)
const BIKER_FRAME1 = [
  [0,0,0,1,0,0,0],
  [0,0,1,1,1,0,0],
  [0,0,0,1,0,0,0],
  [0,1,0,1,0,1,0],
  [0,0,1,0,1,0,0],
  [0,0,0,0,0,0,0],
  [0,1,0,0,0,1,0]
];

const BIKER_FRAME2 = [
  [0,0,0,1,0,0,0],
  [0,0,1,1,1,0,0],
  [0,0,0,1,0,0,0],
  [0,1,0,1,0,1,0],
  [0,0,1,0,1,0,0],
  [0,0,0,0,0,0,0],
  [1,0,0,0,0,0,1]
];

// Italic/Skewed font для "BIKE" (наклон вправо, читаемый 3x5 с uniform shear)
const FONT_ITALIC = {
  'B': [
    [0,0,1,1,1],
    [0,1,0,1,0],
    [1,1,1,1,0],
    [1,0,1,0,1],
    [1,1,1,1,0]
  ],
  'I': [
    [0,0,1],
    [0,1,1],
    [1,1,1],
    [1,0,1],
    [1,0,0]
  ],
  'K': [
    [0,0,1,0,0],
    [0,1,0,1,0],
    [1,1,1,0,0],
    [1,0,1,0,1],
    [1,0,0,1,0]
  ],
  'E': [
    [0,0,1,1,1],
    [0,1,0,1,0],
    [1,1,1,1,0],
    [1,0,1,0,1],
    [1,1,1,1,1]
  ]
};

// Font 4x5 для "OPEN" (компактный, читаемый)
const FONT_4X5 = {
  'O': [
    [0,1,1,0],
    [1,0,0,1],
    [1,0,0,1],
    [1,0,0,1],
    [0,1,1,0]
  ],
  'P': [
    [1,1,1,0],
    [1,0,0,1],
    [1,1,1,0],
    [1,0,0,0],
    [1,0,0,0]
  ],
  'E': [
    [1,1,1,1],
    [1,0,0,0],
    [1,1,1,0],
    [1,0,0,0],
    [1,1,1,1]
  ],
  'N': [
    [1,0,0,1],
    [1,1,0,1],
    [1,0,1,1],
    [1,0,0,1],
    [1,0,0,1]
  ]
};

// Font 7x10 для "BIKE" (ОГРОМНЫЙ, жирный)
const FONT_7X10 = {
  'B': [
    [1,1,1,1,1,0,0],
    [1,1,1,1,1,0,0],
    [1,1,0,0,1,1,0],
    [1,1,0,0,1,1,0],
    [1,1,1,1,1,0,0],
    [1,1,1,1,1,0,0],
    [1,1,0,0,1,1,0],
    [1,1,0,0,1,1,0],
    [1,1,1,1,1,0,0],
    [1,1,1,1,1,0,0]
  ],
  'I': [
    [1,1,1],
    [1,1,1],
    [0,1,0],
    [0,1,0],
    [0,1,0],
    [0,1,0],
    [0,1,0],
    [0,1,0],
    [1,1,1],
    [1,1,1]
  ],
  'K': [
    [1,1,0,0,1,1,0],
    [1,1,0,0,1,1,0],
    [1,1,0,1,1,0,0],
    [1,1,1,1,0,0,0],
    [1,1,1,0,0,0,0],
    [1,1,1,0,0,0,0],
    [1,1,1,1,0,0,0],
    [1,1,0,1,1,0,0],
    [1,1,0,0,1,1,0],
    [1,1,0,0,1,1,0]
  ],
  'E': [
    [1,1,1,1,1,1],
    [1,1,1,1,1,1],
    [1,1,0,0,0,0],
    [1,1,0,0,0,0],
    [1,1,1,1,1,0],
    [1,1,1,1,1,0],
    [1,1,0,0,0,0],
    [1,1,0,0,0,0],
    [1,1,1,1,1,1],
    [1,1,1,1,1,1]
  ]
};

function drawText4x5(text, sr, sc) {
  let offsetC = sc;
  for (let i = 0; i < text.length; i++) {
    const ch = text[i];
    const glyph = FONT_4X5[ch];
    if (glyph) {
      for (let r = 0; r < glyph.length; r++) {
        for (let c = 0; c < glyph[r].length; c++) {
          if (glyph[r][c]) setMatrixPixel(sr + r, offsetC + c, 1);
        }
      }
      offsetC += 5; // 4px буква + 1px gap
    }
  }
}

function drawText7x10(text, sr, sc) {
  const ROW_SHIFTS = [0, 0, 0, 1, 1, 2, 2, 3, 3, 3]; // Italic: верхние строки ровные, нижние сдвинуты вправо
  let offsetC = sc;
  for (let i = 0; i < text.length; i++) {
    const ch = text[i];
    const glyph = FONT_7X10[ch];
    if (glyph) {
      for (let r = 0; r < glyph.length; r++) {
        const shift = ROW_SHIFTS[r] || 0;
        for (let c = 0; c < glyph[r].length; c++) {
          if (glyph[r][c]) setMatrixPixel(sr + r, offsetC + c + shift, 1);
        }
      }
      offsetC += glyph[0].length + 1; // ширина буквы + 1px gap
    }
  }
}

// Elevator animation state
let animStartLvl = 0;
let animTargetLvl = 0;
let animStartTime = 0;
const ANIM_DURATION_MS = 250;

// Функции для рисования элементов загрузочной анимации
function drawPacman(sr, sc, mouthOpen) {
  const sprite = mouthOpen ? PACMAN_OPEN : PACMAN_CLOSED;
  for (let r = 0; r < sprite.length; r++) {
    for (let c = 0; c < sprite[r].length; c++) {
      if (sprite[r][c]) setMatrixPixel(sr + r, sc + c, 1);
    }
  }
}

function drawBiker(sr, sc, frame) {
  const sprite = (frame % 2 === 0) ? BIKER_FRAME1 : BIKER_FRAME2;
  for (let r = 0; r < sprite.length; r++) {
    for (let c = 0; c < sprite[r].length; c++) {
      if (sprite[r][c]) setMatrixPixel(sr + r, sc + c, 1);
    }
  }
}

function drawItalicChar(ch, sr, sc) {
  const g = FONT_ITALIC[ch];
  if (!g) return;
  for (let r = 0; r < g.length; r++) {
    for (let c = 0; c < g[r].length; c++) {
      if (g[r][c]) setMatrixPixel(sr + r, sc + c, 1);
    }
  }
}

function fillRandomDots(density) {
  for (let r = 0; r < MATRIX_ROWS; r++) {
    for (let c = 0; c < MATRIX_COLS; c++) {
      if (Math.random() < density) setMatrixPixel(r, c, 1);
    }
  }
}

function renderCanvas() {
  if (!canvas || !ctx) return;
  const cellW = canvas.width / MATRIX_COLS;
  const cellH = canvas.height / MATRIX_ROWS;
  const radius = Math.min(cellW, cellH) * 0.42;

  ctx.fillStyle = "#0c0c0c";
  ctx.fillRect(0, 0, canvas.width, canvas.height);

  ctx.strokeStyle = "#1a1a1a";
  ctx.lineWidth = 1;
  ctx.beginPath();
  ctx.moveTo(0, 8 * cellH);
  ctx.lineTo(canvas.width, 8 * cellH);
  ctx.stroke();

  for (let r = 0; r < MATRIX_ROWS; r++) {
    for (let c = 0; c < MATRIX_COLS; c++) {
      const cx = c * cellW + cellW / 2;
      const cy = r * cellH + cellH / 2;
      ctx.beginPath();
      ctx.arc(cx, cy, radius, 0, Math.PI * 2);
      if (matrixGrid[r][c] === 1) {
        ctx.fillStyle = "#ff8c00";
        ctx.shadowColor = "#ff7700";
        ctx.shadowBlur = 8;
        ctx.fill();
        ctx.shadowBlur = 0;
      } else {
        ctx.fillStyle = "#1e140a";
        ctx.fill();
      }
    }
  }
}

function playBootAnimation() {
  bootAnimationActive = true;
  let frame = 0;
  
  function nextFrame() {
    clearMatrix();
    
    switch(frame) {
      case 0: // Велосипедист справа
        drawBiker(4, 24, 0);
        renderCanvas();
        setTimeout(nextFrame, 500);
        break;
        
      case 1: // Велосипедист едет к центру влево
        drawBiker(4, 18, 1);
        renderCanvas();
        setTimeout(nextFrame, 400);
        break;
        
      case 2: // Велосипедист в центре, Pac-Man появляется слева (за краем, col -16)
        drawBiker(4, 12, 0);
        drawPacman(0, -10, true);
        renderCanvas();
        setTimeout(nextFrame, 300);
        break;
        
      case 3: // Pac-Man приближается слева (рот закрывается)
        drawBiker(4, 12, 1);
        drawPacman(0, 0, false);
        renderCanvas();
        setTimeout(nextFrame, 200);
        break;
        
      case 4: // Pac-Man съедает велосипедиста (рот открыт, велосипедист исчезает)
        drawPacman(0, 8, true);
        renderCanvas();
        setTimeout(nextFrame, 300);
        break;
        
      case 5: // Заливка экрана точками (Pac-Man в центре)
        drawPacman(0, 12, false);
        fillRandomDots(0.3);
        renderCanvas();
        setTimeout(nextFrame, 400);
        break;
        
      case 6: // Больше точек
        fillRandomDots(0.6);
        renderCanvas();
        setTimeout(nextFrame, 300);
        break;
        
      case 7: // Текст проявляется: "OPEN" (шрифт 4x5, вверху слева)
        clearMatrix();
        drawText4x5("OPEN", 0, 1);  // row 0 (самый верх), col 1 (сдвиг влево)
        renderCanvas();
        setTimeout(nextFrame, 600);
        break;
        
      case 8: // "BIKE" появляется (шрифт 7x10, ОГРОМНЫЙ внизу справа)
        drawText4x5("OPEN", 0, 1);
        drawText7x10("BIKE", 6, 2);  // row 6, col 2 (сдвиг вправо на 1)
        renderCanvas();
        setTimeout(() => {
          bootAnimationActive = false;
          bootAnimationComplete = true;
        }, 1000);
        break;
    }
    
    frame++;
  }
  
  nextFrame();
}

// Пропуск анимации по клику/тапу на матрицу (и таймаут-страховка: 5с максимум)
if (canvas) {
  canvas.addEventListener("pointerdown", () => {
    if (bootAnimationActive) {
      bootAnimationActive = false;
      bootAnimationComplete = true;
      updateMatrixDisplay();
    }
  });
}
setTimeout(() => {
  if (bootAnimationActive && !bootAnimationComplete) {
    bootAnimationActive = false;
    bootAnimationComplete = true;
  }
}, 5000);

function updateMatrixDisplay() {
  clearMatrix();
  const now = Date.now();
  const blinkOn = Math.floor(now / 400) % 2 === 0;

  if (draftMode === "settings") {
    // Gear icon top-left rows 1..7, cols 1..7
    drawGear7x7(1, 1);

    // Fixed checkmark on the right: rows 10..14, cols 24..28
    if (!draftSettingEditing) {
      drawIcon5x5(ICON_CHECK_5X5, 10, 24);
    } else if (blinkOn) {
      drawIcon5x5(ICON_CHECK_5X5, 10, 24);
    }

    // Current setting item
    const curItem = SETTINGS_MENU[draftSettingIdx];
    const fullText = curItem.name; // e.g. "THROTTLE IN MIN"
    const words = fullText.split(' ');
    let lines = [];
    let curLine = "";
    for (let w of words) {
      if (!curLine) {
        curLine = w;
      } else if ((curLine + " " + w).length <= 5) {
        curLine += " " + w;
      } else {
        lines.push(curLine);
        curLine = w;
      }
    }
    if (curLine) lines.push(curLine);

    // Scrolling logic for setting title (clipped in rows 0..9, cols 10..29)
    // Display window fits 2 lines at height 5 with 1px gap: line 0 at r=0, line 1 at r=5 (0..9)
    const lineHeight = 5;
    const lineSpacing = 1;
    const totalLinePitch = lineHeight + lineSpacing; // 6px
    const maxScroll = Math.max(0, (lines.length - 2) * totalLinePitch);

    let scrollY = 0;
    if (maxScroll > 0) {
      const scrollCycleMs = 3000;
      const t = (now % scrollCycleMs) / scrollCycleMs;
      if (t < 0.35) {
        scrollY = 0;
      } else if (t < 0.5) {
        scrollY = ((t - 0.35) / 0.15) * maxScroll;
      } else if (t < 0.85) {
        scrollY = maxScroll;
      } else {
        scrollY = maxScroll * (1 - (t - 0.85) / 0.15);
      }
    }

    for (let i = 0; i < lines.length; i++) {
      const y = Math.round(i * totalLinePitch - scrollY);
      if (y + 5 >= 0 && y <= 9) {
        drawText3x5(lines[i], y, 10, 0, 9, 10, 29);
      }
    }

    // Value area at bottom: rows 11..15, cols 2..22
    const valStr = curItem.val.toFixed(2) + curItem.unit;
    if (!draftSettingEditing || blinkOn) {
      drawText3x5(valStr, 11, 2, 10, 15, 0, 23);
    }
  } else {
    let modeToDraw = draftMode;
    let lvlToDraw = (modeToDraw === "pas") ? draftPasLvl : ((modeToDraw === "cruise") ? draftCruiseLvl : 0);
    let maxLvl = (modeToDraw === "pas") ? pasMax : cruiseMax;

    if (lvlToDraw !== animTargetLvl) {
      animStartLvl = animTargetLvl;
      animTargetLvl = lvlToDraw;
      animStartTime = now;
    }
    let animProgress = 1;
    if (now - animStartTime < ANIM_DURATION_MS) {
      let t = (now - animStartTime) / ANIM_DURATION_MS;
      animProgress = 1 - Math.pow(1 - t, 3);
    }

    // 1. Draw Big Letter P or C (6x11, rows 0..10, cols 0..5) - blink when draft mode differs from active mode
    let modeSwitchPending = isDirty && (draftMode !== activeMode);
    if (!modeSwitchPending || blinkOn) {
      if (modeToDraw === "pas") {
        drawBigLetter('P', 0, 0);
      } else if (modeToDraw === "cruise") {
        drawBigLetter('C', 0, 0);
      }
    }

    // 2. Стрелки центрированы относительно блока цифр 3x5 (две цифры: cols 7..13, одна: cols 8..12).
    let isTwoDigits = (lvlToDraw >= 10);
    let arrowCol = isTwoDigits ? 8 : 7;
    let arrowUpVOffset = 0;
    let arrowDownVOffset = 0;
    if (now < arrowBounceUntil) {
      let remaining = arrowBounceUntil - now;
      let bPhase = Math.sin((500 - remaining) / 500 * Math.PI);
      if (arrowBounceDir > 0) {
        arrowUpVOffset = -Math.round(bPhase * 1.2);
      } else if (arrowBounceDir < 0) {
        arrowDownVOffset = Math.round(bPhase * 1.2);
      }
    }

    if (lvlToDraw < maxLvl) {
      drawArrow5x2(ARROW_UP, 0 + arrowUpVOffset, arrowCol);
    }
    // Нижняя стрелка показывается всегда, но при уровне 0 — без центрального пикселя.
    drawArrow5x2(ARROW_DOWN, 9 + arrowDownVOffset, arrowCol, lvlToDraw === 0);

    // 3. Elevator Animation inside digit window: rows 3..7 (height 5px), strictly clipped
    let isCruiseWaiting = (modeToDraw === "cruise" && activeCruiseLvl > 0 && !simCruiseEngaged && !isDirty);
    let showDigits = true;
    if ((isDirty || isCruiseWaiting) && !blinkOn) showDigits = false;

    if (showDigits) {
      let fromLvl = animStartLvl;
      let toLvl = animTargetLvl;
      let dir = (toLvl >= fromLvl) ? 1 : -1;

      const renderLevelToMatrix = (lvlVal, rowOffset) => {
        let tD = Math.floor(lvlVal / 10);
        let oD = lvlVal % 10;
        let twoD = (lvlVal >= 10);
        let startC = twoD ? 7 : 8;

        let buf1 = Array.from({length: 5}, () => new Uint8Array(3));
        let buf2 = Array.from({length: 5}, () => new Uint8Array(3));

        if (twoD) {
          drawDigit3x5ToBuffer(tD.toString(), buf1);
          drawDigit3x5ToBuffer(oD.toString(), buf2);
        } else {
          drawDigit3x5ToBuffer(oD.toString(), buf1);
        }

        for (let r = 0; r < 5; r++) {
          let targetRow = Math.round(3 + r + rowOffset);
          if (targetRow >= 3 && targetRow <= 7) {
            for (let c = 0; c < 3; c++) {
              if (buf1[r][c]) setMatrixPixel(targetRow, startC + c, 1);
              if (twoD && buf2[r][c]) setMatrixPixel(targetRow, startC + 4 + c, 1);
            }
          }
        }
      };

      if (animProgress < 1 && fromLvl !== toLvl) {
        renderLevelToMatrix(Math.round(fromLvl), Math.round(-dir * animProgress * 5));
        renderLevelToMatrix(Math.round(toLvl), Math.round(dir * (1 - animProgress) * 5));
      } else {
        renderLevelToMatrix(lvlToDraw, 0);
      }
    }
  }

  // Нижний ряд: иконки управления (cols: 0=левый поворотник, 6=фара, 12=гудок, 21=тормоз, 27=правый поворотник)
  // Рисуются ДО шкал газа/выхода, чтобы шкалы (col 30/31, rows 13..15) перекрывали хвост правой стрелки.
  
  // Поворотники: мигание синхронно с физическими светодиодами (500мс)
  const lr = hwTurnLeftActive || hwDisplayTurnLeftActive;
  const rr = hwTurnRightActive || hwDisplayTurnRightActive;
  const lb = lr && (now % 1000 < 500);
  const rb = rr && (now % 1000 < 500);
  if (lb) drawTurnArrow5x5(true, 11, 0);
  if (rb) drawTurnArrow5x5(false, 11, 27);
  
  // Фара: показываем иконку текущего режима света (0=ВЫКЛ, 1=ДХО, 2=БЛИЖНИЙ, 3=БЛ+ДХО)
  if (draftMode !== "settings" && (hwLightMode > 0 || hwDisplayLightActive)) {
    drawIcon5x5(ICON_LIGHT_MODES[hwLightMode] || ICON_LIGHT_MODES[0], 11, 6);
  }
  
  // Гудок: временная иконка (автоматически скрывается через 300мс)
  if (hwDisplayHornActive && Date.now() < hwDisplayHornHideAtMs) {
    drawIcon5x5(ICON_HORN, 11, 12);
  }

  // 4. Scales calculation
  let inMin = cfgThrottleInMin, inMax = Math.max(inMin + 0.01, cfgThrottleInMax);
  let outMin = cfgThrottleOutMin, outMax = Math.max(outMin + 0.01, cfgThrottleOutMax);
  let rawGripV = inMin + (simGasPct / 100) * (inMax - inMin);
  let clampedV = Math.max(inMin, Math.min(inMax, rawGripV));
  let calibOutV = outMin + ((clampedV - inMin) / (inMax - inMin)) * (outMax - outMin);
  let calibOutPct = Math.max(0, Math.min(100, ((calibOutV - outMin) / (outMax - outMin)) * 100));

  let inGasLeds = Math.round((simGasPct / 100) * 16);
  for (let r = 0; r < inGasLeds; r++) {
    const row = 15 - r;
    if (row < 13 || row > 15) setMatrixPixel(row, 30, 1);
  }

  let effectiveOutPct = 0;
  if (!effectiveBrake) {
    let baseMotorPct = calibOutPct;
    if (activeMode === "cruise" && activeCruiseLvl > 0 && simCruiseEngaged) {
      let targetCruisePct = Math.min(100, activeCruiseLvl * (100 / Math.max(1, cruiseMax)));
      baseMotorPct = Math.max(baseMotorPct, targetCruisePct);
    } else if (activeMode === "pas" && activePasLvl > 0 && effectivePedal) {
      let targetPasPct = Math.min(100, activePasLvl * (100 / Math.max(1, pasMax)));
      baseMotorPct = Math.max(baseMotorPct, targetPasPct);
    }
    effectiveOutPct = baseMotorPct;
  }
  let outGasLeds = Math.round((effectiveOutPct / 100) * 16);
  for (let r = 0; r < outGasLeds; r++) {
    const row = 15 - r;
    if (row < 13 || row > 15) setMatrixPixel(row, 31, 1);
  }

  // Индикатор педалирования/тормоза 5x5: cols 21..25, rows 11..15
  // (сдвинут на 3px влево, чтобы освободить правый нижний угол под стрелку поворотника).
  if (effectiveBrake) {
    const brakeBlink = Math.floor(now / 90) % 2 === 0;
    if (brakeBlink) drawIcon5x5(ICON_BRAKE, 11, 21);
  } else if (effectivePedal) {
    let frame = Math.floor((now - simPedalStartMs) / 75) % 8;
    drawIcon5x5(ICON_PEDAL_FRAMES[frame], 11, 21);
  }

  // Отрисовка матрицы на canvas
  renderCanvas();
}

// Запуск анимации при загрузке страницы (1 раз)
if (canvas && !bootAnimationComplete) {
  playBootAnimation();
}

setInterval(() => {
  if (!bootAnimationActive) {
    updateEffectiveStates(); // Обновляем состояния для синхронизации временных иконок
    updateMatrixDisplay();
  }
}, 40);

// Виртуальные входы эмуляции
let simGasPct = 0; // Значение виртуальной ручки газа, 0-100%
let simBrakeActive = false; // Virtual brake input (momentary, active only while held)
let simPedalActive = false; // Virtual pedal button state
let simPedalStartMs = 0;
const sliderGas = document.getElementById("simGas");
const lblGas = document.getElementById("lblGas");

// Слайдер газа — это именно виртуальный вход эмулятора, а не индикатор
// физической ручки. Телеметрия hardware обновляется отдельно ниже.
if (sliderGas) {
  sliderGas.addEventListener("input", (e) => {
    simGasPct = Math.max(0, Math.min(100, Number(e.target.value) || 0));
    if (lblGas) lblGas.innerText = simGasPct + "%";
    userInteractingUntil = Date.now() + 1000;
    renderJoystick();
  });
}

// Real-time data from hardware used by the virtual display
let hwBrakeActive = false;
let hwPasActive = false;

// Combined effective states
let effectiveBrake = false;
let effectivePedal = false;

// Variables for combined state update
let currentMode = "";
let currentPasLvl = 0;
let currentCruiseLvl = 0;
let currentCruiseEngaged = false;

// Обновление результирующих состояний по входам эмулятора и оборудования
function updateEffectiveStates() {
  effectiveBrake = simBrakeActive || hwBrakeActive || (hwDisplayBrakeActive && Date.now() < hwDisplayBrakeHideAtMs);
  effectivePedal = simPedalActive || hwPasActive;
}

function refreshHubData(forceSync = false) {
  if (!forceSync && (isDirty || applyInProgress || Date.now() < applyInProgressUntil)) return;
  fetch("/status/sys")
    .then(r => r.json())
    .then(d => {
      // Игнорируем устаревший ответ, если OK был нажат после отправки запроса.
      if (applyInProgress && !forceSync) return;

      // Update hardware states used by the virtual display
      hwBrakeActive = d.brake || false;
      hwPasActive = d.pas_active || false; // Use 'pas_active' field
      hwTurnLeftActive = d.turn_left_act || false;
      hwTurnRightActive = d.turn_right_act || false;
      hwLightMode = d.light_mode === undefined ? 1 : (d.light_mode | 0);
      renderSimLightButtons();

      // Для эмулятора мы не перезаписываем слайдер (simGasPct) данными физической
      // ручки, чтобы виртуальный интерфейс работал независимо (анимация и логика
      // UI продолжают реагировать на simGasPct).
      // Hardware-телеметрия только обновляет флаги, не трогая simGasPct.

      // Update effective states
      updateEffectiveStates();

      // State machine for cruise confirmation & engagement in UI
      if (activeMode === "cruise" && activeCruiseLvl > 0) {
        if (effectiveBrake) {
          if (simCruiseEngaged || !simCruisePendingResume) {
            if (cfgCruiseAfterBraking === 0) {
              simCruiseEngaged = false;
              simCruisePendingResume = false;
            } else if (cfgCruiseAfterBraking === 1) {
              simCruiseEngaged = false;
              simCruisePendingResume = true;
              simCruiseConfirmRequired = true;
              simCruiseReleaseSeen = (simGasPct <= 10);
            } else if (cfgCruiseAfterBraking === 2) {
              simCruiseEngaged = false;
              simCruisePendingResume = true;
              simCruiseConfirmRequired = false;
            }
          }
        } else {
          if (simCruiseEngaged && simGasPct > 10) {
            if (cfgCruiseAfterThrottle === 0) {
              simCruiseEngaged = false;
              simCruisePendingResume = false;
            } else if (cfgCruiseAfterThrottle === 1) {
              simCruiseEngaged = false;
              simCruisePendingResume = true;
              simCruiseConfirmRequired = true;
              simCruiseReleaseSeen = false;
            } else if (cfgCruiseAfterThrottle === 2) {
              simCruiseEngaged = false;
              simCruisePendingResume = true;
              simCruiseConfirmRequired = false;
            }
          } else if (!simCruiseEngaged && simCruisePendingResume) {
            if (simCruiseConfirmRequired) {
              if (simGasPct <= 10) {
                simCruiseReleaseSeen = true;
              } else if (simCruiseReleaseSeen && simGasPct > 10) {
                simCruiseEngaged = true;
                simCruisePendingResume = false;
                simCruiseConfirmRequired = false;
              }
            } else {
              if (simGasPct <= 10) {
                simCruiseEngaged = true;
                simCruisePendingResume = false;
              }
            }
          }
        }
      }

      // Update joystick mode and level display
      currentMode = d.pas_en ? "pas" : (d.cruise_en ? "cruise" : "off");
      currentPasLvl = d.pas_lvl || 0;
      currentCruiseLvl = d.cruise_lvl || 0;
      currentCruiseEngaged = d.cruise_engaged || false;

      let serverMode = currentMode;
      let serverPasLvl = currentPasLvl;
      let serverCruiseLvl = currentCruiseLvl;
      let serverCruiseEngaged = currentCruiseEngaged;

      if (serverMode === "cruise") {
        if (serverCruiseEngaged) {
          simCruiseEngaged = true;
          simCruisePendingResume = false;
          simCruiseConfirmRequired = false;
        }
      }

      if (serverMode !== activeMode || serverPasLvl !== activePasLvl || serverCruiseLvl !== activeCruiseLvl) {
        activeMode = serverMode;
        activePasLvl = serverPasLvl;
        activeCruiseLvl = serverCruiseLvl;
        let isUserBusy = isDirty || (Date.now() < userInteractingUntil) || (Date.now() < applyInProgressUntil);
        if (!isUserBusy || forceSync) {
          draftMode = (activeMode === "off") ? "pas" : activeMode;
          draftPasLvl = activePasLvl;
          draftCruiseLvl = activeCruiseLvl;
          if (forceSync) isDirty = false;
        }
        renderJoystick();
      }

      // Update visual feedback for simulated buttons based on effective states
      const btnBrake = document.getElementById("btnSimBrake");
      if (btnBrake) {
        if (effectiveBrake) btnBrake.classList.add("active");
        else btnBrake.classList.remove("active");
      }
      const btnPedal = document.getElementById("btnSimPedal");
      if (btnPedal) {
        if (effectivePedal) btnPedal.classList.add("active");
        else btnPedal.classList.remove("active");
      }

    })
    .catch(e => {
      console.error("Failed to fetch hub data:", e);
    });
}
setInterval(refreshHubData, 100);

// Update effective states whenever sim states change.
// Тормоз — momentary: активен только пока кнопка удерживается.
const btnSimBrakeEl = document.getElementById("btnSimBrake");
function setSimBrake(active) {
  if (simBrakeActive === active) return;
  simBrakeActive = active;
  updateEffectiveStates();
  renderJoystick();
}
if (btnSimBrakeEl) {
  btnSimBrakeEl.addEventListener("pointerdown", (e) => {
    e.preventDefault();
    if (btnSimBrakeEl.setPointerCapture) btnSimBrakeEl.setPointerCapture(e.pointerId);
    setSimBrake(true);
  });
  btnSimBrakeEl.addEventListener("pointerup", (e) => {
    setSimBrake(false);
    if (btnSimBrakeEl.releasePointerCapture && btnSimBrakeEl.hasPointerCapture(e.pointerId)) btnSimBrakeEl.releasePointerCapture(e.pointerId);
  });
  btnSimBrakeEl.addEventListener("pointerleave", () => {});
  btnSimBrakeEl.addEventListener("lostpointercapture", () => setSimBrake(false));
  btnSimBrakeEl.addEventListener("pointercancel", () => setSimBrake(false));
  btnSimBrakeEl.addEventListener("contextmenu", (e) => e.preventDefault());
}

document.getElementById("btnSimPedal").addEventListener("click", () => {
  simPedalActive = !simPedalActive;
  updateEffectiveStates();
  renderJoystick(); // Re-render to reflect state changes visually
});

// ================= Виртуальные кнопки света и поворотников =================
// Нажатие отправляется как "pulse": на контроллере пин прижимается на 150 мс,
// дальше штатный дебаунс отрабатывает нажатие как физическое.
let hwTurnLeftActive = false, hwTurnRightActive = false, hwLightMode = 1;
let hwDisplayTurnLeftActive = false, hwDisplayTurnRightActive = false, hwDisplayLightActive = false;
let hwDisplayHornActive = false, hwDisplayHornHideAtMs = 0;
let hwDisplayBrakeActive = false, hwDisplayBrakeHideAtMs = 0;
const LIGHT_MODE_NAMES = ["ВЫКЛ", "ДХО", "БЛИЖНИЙ", "БЛ+ДХО"];
function pressVirtualButton(btn) {
  vib();
  fetch("/api/buttons/press?btn=" + encodeURIComponent(btn) + "&state=pulse", { cache: "no-store" }).catch(() => {});
}
const btnSimTurnLEl = document.getElementById("btnSimTurnL");
const btnSimTurnREl = document.getElementById("btnSimTurnR");
const btnSimLightEl = document.getElementById("btnSimLight");
const lblSimLightEl = document.getElementById("lblSimLight");
if (btnSimTurnLEl) btnSimTurnLEl.addEventListener("click", () => pressVirtualButton("turnL"));
if (btnSimTurnREl) btnSimTurnREl.addEventListener("click", () => pressVirtualButton("turnR"));
if (btnSimLightEl) btnSimLightEl.addEventListener("click", () => pressVirtualButton("light"));
function renderSimLightButtons() {
  if (btnSimTurnLEl) btnSimTurnLEl.classList.toggle("active", hwTurnLeftActive);
  if (btnSimTurnREl) btnSimTurnREl.classList.toggle("active", hwTurnRightActive);
  if (btnSimLightEl) btnSimLightEl.classList.toggle("active", hwLightMode !== 0);
  if (lblSimLightEl) lblSimLightEl.innerText = LIGHT_MODE_NAMES[hwLightMode] || "Свет";
}

// Initial setup and interval
// Set initial effective states
updateEffectiveStates();
// Set initial slider value and label
if (sliderGas && lblGas) {
  lblGas.innerText = simGasPct + "%";
  sliderGas.value = simGasPct;
}

function renderJoystick() {
  const modeEl = document.getElementById("joyMode");
  const valEl = document.getElementById("joyVal");
  const statusEl = document.getElementById("joyStatus");
  const okBtn = document.getElementById("btnOk");

  modeEl.className = "screen-mode mode-" + draftMode;
  if (draftMode === "pas") {
    modeEl.innerText = "РЕЖИМ: PAS АССИСТЕНТ";
    if (draftPasLvl === 0) {
      valEl.innerText = "ВЫКЛ (0 / " + pasMax + ")";
    } else {
      valEl.innerText = "УРОВЕНЬ " + draftPasLvl + " / " + pasMax;
    }
  } else if (draftMode === "cruise") {
    modeEl.innerText = "РЕЖИМ: КРУИЗ-КОНТРОЛЬ";
    if (draftCruiseLvl === 0) {
      valEl.innerText = "ВЫКЛ (0 / " + cruiseMax + ")";
    } else {
      valEl.innerText = "УРОВЕНЬ " + draftCruiseLvl + " / " + cruiseMax;
    }
  } else if (draftMode === "settings") {
    const cur = SETTINGS_MENU[draftSettingIdx];
    modeEl.innerText = "НАСТРОЙКИ: " + cur.name;
    if (draftSettingEditing) {
      valEl.innerText = "РЕДАКТИРОВАНИЕ: " + cur.val.toFixed(2) + cur.unit;
    } else {
      valEl.innerText = "ЗНАЧЕНИЕ: " + cur.val.toFixed(2) + cur.unit + " (OK - правка)";
    }
  }

  let modified = false;
  if (draftMode === "settings") {
    modified = draftSettingEditing;
  } else if (activeMode === "off") {
    let draftLvl = (draftMode === "pas") ? draftPasLvl : draftCruiseLvl;
    modified = (draftLvl !== 0);
  } else if (draftMode !== activeMode) {
    modified = true;
  } else if (draftMode === "pas" && draftPasLvl !== activePasLvl) {
    modified = true;
  } else if (draftMode === "cruise" && draftCruiseLvl !== activeCruiseLvl) {
    modified = true;
  }

  isDirty = modified;
  if (draftMode === "settings") {
    if (draftSettingEditing) {
      statusEl.innerText = "НАЖМИТЕ OK ДЛЯ СОХРАНЕНИЯ ЗНАЧЕНИЯ";
      statusEl.className = "screen-status dirty";
      okBtn.className = "dpad-btn btn-ok dirty-pulse";
    } else {
      statusEl.innerText = "UP/DOWN: ПУНКТ, OK: ИЗМЕНИТЬ";
      statusEl.className = "screen-status";
      okBtn.className = "dpad-btn btn-ok";
    }
  } else if (isDirty) {
    statusEl.innerText = "НАЖМИТЕ OK ДЛЯ ПРИМЕНЕНИЯ";
    statusEl.className = "screen-status dirty";
    okBtn.className = "dpad-btn btn-ok dirty-pulse";
  } else {
    let curLvl = (activeMode === "pas") ? activePasLvl : ((activeMode === "cruise") ? activeCruiseLvl : 0);
    statusEl.innerText = (activeMode === "off" || curLvl === 0) ? "ОБЫЧНАЯ ЕЗДА (БЕЗ МОТОРА)" : "АКТИВНО И ПРИМЕНЕНО";
    statusEl.className = "screen-status";
    okBtn.className = "dpad-btn btn-ok";
  }
}

function toggleMode(dir = 1) {
  vib();
  userInteractingUntil = Date.now() + 4000;
  const modes = ["pas", "cruise", "settings"];
  let curIdx = modes.indexOf(draftMode);
  if (curIdx === -1) curIdx = 0;
  if (dir > 0) {
    curIdx = (curIdx + 1) % modes.length;
  } else {
    curIdx = (curIdx - 1 + modes.length) % modes.length;
  }
  draftMode = modes[curIdx];
  draftSettingEditing = false;
  renderJoystick();
}

document.getElementById("btnLeft").addEventListener("click", () => toggleMode(-1));
document.getElementById("btnRight").addEventListener("click", () => toggleMode(1));

document.getElementById("btnUp").addEventListener("click", () => {
  vib();
  userInteractingUntil = Date.now() + 4000;
  if (draftMode === "settings") {
    const cur = SETTINGS_MENU[draftSettingIdx];
    if (draftSettingEditing) {
      cur.val = Math.min(cur.max, +(cur.val + cur.step).toFixed(2));
    } else {
      if (draftSettingIdx > 0) draftSettingIdx--;
      else draftSettingIdx = SETTINGS_MENU.length - 1;
    }
  } else {
    let curLvl = (draftMode === "pas") ? draftPasLvl : draftCruiseLvl;
    let maxLvl = (draftMode === "pas") ? pasMax : cruiseMax;
    if (curLvl < maxLvl) {
      if (draftMode === "pas") draftPasLvl++;
      else if (draftMode === "cruise") draftCruiseLvl++;
      arrowBounceUntil = Date.now() + 500;
      arrowBounceDir = 1;
    }
  }
  renderJoystick();
});

document.getElementById("btnDown").addEventListener("click", () => {
  vib();
  userInteractingUntil = Date.now() + 4000;
  if (draftMode === "settings") {
    const cur = SETTINGS_MENU[draftSettingIdx];
    if (draftSettingEditing) {
      cur.val = Math.max(cur.min, +(cur.val - cur.step).toFixed(2));
    } else {
      if (draftSettingIdx < SETTINGS_MENU.length - 1) draftSettingIdx++;
      else draftSettingIdx = 0;
    }
  } else {
    let curLvl = (draftMode === "pas") ? draftPasLvl : draftCruiseLvl;
    if (curLvl > 0) {
      if (draftMode === "pas") draftPasLvl--;
      else if (draftMode === "cruise") draftCruiseLvl--;
      arrowBounceUntil = Date.now() + 500;
      arrowBounceDir = -1;
    }
  }
  renderJoystick();
});

function applyJoystickDraft(event) {
  if (event) {
    event.preventDefault();
    event.stopPropagation();
  }
  if (applyInProgress) return;

  vib();
  if (navigator.vibrate) navigator.vibrate([40, 30, 40]);
  if (draftMode === "settings") {
    draftSettingEditing = !draftSettingEditing;
    renderJoystick();
    return;
  }

  let targetMode = draftMode;
  let targetLvl = (draftMode === "pas") ? draftPasLvl : draftCruiseLvl;
  let requestMode = targetLvl === 0 ? "off" : targetMode;
  const previousActiveMode = activeMode;
  const previousActivePasLvl = activePasLvl;
  const previousActiveCruiseLvl = activeCruiseLvl;

  applyInProgress = true;
  applyInProgressUntil = Date.now() + 2000;

  if (targetLvl === 0) {
    activeMode = "off";
    activePasLvl = 0;
    activeCruiseLvl = 0;
  } else {
    activeMode = targetMode;
    if (targetMode === "pas") {
      activePasLvl = targetLvl;
      activeCruiseLvl = 0;
    } else if (targetMode === "cruise") {
      activeCruiseLvl = targetLvl;
      activePasLvl = 0;
    }
  }

  if (targetMode === "cruise" && targetLvl > 0) {
    if (cfgCruiseConfirmThrottle) {
      simCruiseEngaged = false;
      simCruisePendingResume = true;
      simCruiseConfirmRequired = true;
      simCruiseReleaseSeen = (simGasPct <= 10);
    } else {
      simCruiseEngaged = false;
      simCruisePendingResume = false;
    }
  }

  draftMode = (activeMode === "off") ? targetMode : activeMode;
  draftPasLvl = activePasLvl;
  draftCruiseLvl = activeCruiseLvl;
  isDirty = false;
  renderJoystick();

  fetch("/api/joystick/apply?mode=" + encodeURIComponent(requestMode) + "&level=" + targetLvl)
    .then(res => {
      if (!res.ok) throw new Error("HTTP " + res.status);
      return new Promise(resolve => setTimeout(resolve, 300));
    })
    .then(() => {
      applyInProgress = false;
      applyInProgressUntil = 0;
      refreshHubData(true);
    })
    .catch(err => {
      console.warn("Apply mode error:", err);
      applyInProgress = false;
      applyInProgressUntil = 0;
      activeMode = previousActiveMode;
      activePasLvl = previousActivePasLvl;
      activeCruiseLvl = previousActiveCruiseLvl;
      draftMode = targetMode;
      draftPasLvl = (targetMode === "pas") ? targetLvl : 0;
      draftCruiseLvl = (targetMode === "cruise") ? targetLvl : 0;
      renderJoystick();
      const statusEl = document.getElementById("joyStatus");
      if (statusEl) {
        statusEl.innerText = "ОШИБКА ПРИМЕНЕНИЯ — НАЖМИТЕ OK ЕЩЁ РАЗ: " + err.message;
        statusEl.className = "screen-status dirty";
      }
    });
}

document.getElementById("btnOk").addEventListener("click", applyJoystickDraft, false);

// Remove the duplicate function definition

document.addEventListener("visibilitychange", () => {
  if (document.visibilityState === "visible") {
    if (!applyInProgress) {
      applyInProgressUntil = 0;
      refreshHubData(true);
    }
    if (typeof updateSysStatus === "function") updateSysStatus();
  }
});

window.addEventListener("focus", () => {
  if (!applyInProgress) {
    applyInProgressUntil = 0;
    refreshHubData(true);
  }
  if (typeof updateSysStatus === "function") updateSysStatus();
});

renderJoystick();
</script>
</body>
</html>)rawliteral";
  server.send_P(200, PSTR("text/html; charset=utf-8"), page, sizeof(page)-1);
}
