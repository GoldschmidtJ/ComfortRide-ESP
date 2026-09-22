#include "web/html_pages_emulation.h"

// JS: топ-бар (CPU/RAM/WiFi, опрос /status/sys)
// Часть страницы эмулятора (этап D: разбиение html_pages_emulation.cpp).
static const char part[] PROGMEM = R"rawliteral(<script>
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

)rawliteral";

PGM_P getEmulationStatus() { return part; }
size_t getEmulationStatusLen() { return sizeof(part) - 1; }
