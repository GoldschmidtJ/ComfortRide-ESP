#include "web/html_pages_hub.h"

#include <WebServer.h>
void sendHubPage(WebServer &server) {
  static const char page[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>OpenBike Controller v0.4.0</title>
<style>
:root{--ui-bg:#101214;--ui-card:#191c20;--ui-button:#252a30;--ui-border:#3b424a;--ui-hover:#30363d;--ui-active:#383f47;--ui-text:#eee;--ui-muted:#8b949e;--ui-focus:#aeb6bf;--ui-accent:#4a90d9;--ui-success:#2ecc71;--ui-warning:#f39c12;--ui-danger:#e74c3c;--ui-purple:#9b59b6;--ui-dark:#0a0f0d;--ui-black:#000;--ui-led:#ff8c00;--ui-led-glow:#ff7700;--ui-led-off:#1e140a;--ui-radius:8px;--ui-control-height:44px;--ui-on-accent:#fff;--ui-success-soft:rgba(46,204,113,.12);--ui-warning-soft:rgba(243,156,18,.15);--ui-danger-soft:rgba(231,76,60,.12);--ui-font:system-ui,-apple-system,"Segoe UI",Roboto,sans-serif;--ui-mono:ui-monospace,SFMono-Regular,Menlo,Consolas,monospace;--ui-fs-h1:24px;--ui-fs-h2:18px;--ui-fs-body:16px;--ui-fs-small:13px;--ui-fs-mid:14px;--ui-fs-tiny:12px}
*{box-sizing:border-box}body{font-family:var(--ui-font);font-size:var(--ui-fs-body);line-height:1.45;padding:20px;padding-top:max(20px,env(safe-area-inset-top));max-width:560px;margin:auto;background:var(--ui-bg);color:var(--ui-text)}
.top-bar-sticky{position:sticky;top:0;top:env(safe-area-inset-top);z-index:9999;background:var(--ui-card);border-bottom:1px solid var(--ui-border);padding:8px 12px;margin:-20px -20px 15px;font-size:var(--ui-fs-tiny);color:var(--ui-muted);display:flex;align-items:center;justify-content:space-between;flex-wrap:wrap;gap:8px;box-shadow:0 2px 8px rgba(0,0,0,.5)}
.tb-item{display:inline-flex;align-items:center;gap:4px;white-space:nowrap}.tb-link{color:var(--ui-accent);text-decoration:none;padding:2px 6px;border-radius:6px;background:var(--ui-button);border:1px solid var(--ui-border);transition:background .2s,border-color .2s}.tb-link:hover{background:var(--ui-hover);border-color:var(--ui-muted);color:var(--ui-text)}.tb-dot{width:8px;height:8px;border-radius:50%;display:inline-block}.dot-green{background:var(--ui-success);box-shadow:0 0 5px var(--ui-success)}.dot-yellow{background:var(--ui-warning);box-shadow:0 0 5px var(--ui-warning)}.dot-red{background:var(--ui-danger)}.dot-gray{background:var(--ui-muted)}
.header{text-align:center;margin-bottom:18px}.header h1{margin:0;font-size:var(--ui-fs-h1);line-height:1.2}.version,.u-muted{color:var(--ui-muted)}.u-small{font-size:var(--ui-fs-tiny)}.u-dim{opacity:.7}
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
<div class="header"><h1>OpenBike Controller</h1><div class="version">v0.4.0</div></div>
<a class="card primary" href="/emulation">Эмуляция дисплея и пульта &rarr;</a>
<a class="card" href="/settings/drive">Управление тягой (Газ · PAS · Круиз) &rarr;</a>
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

