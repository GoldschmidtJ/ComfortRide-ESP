#include "web/web_handlers_events.h"
#include <WebServer.h>
#include "web/web_routes.h"   // server
#include "system/events_engine.h" // EventRule/EventRuntime/EventLog, EVENT_MAX_RULES и др. — единое определение
#include "core/pas.h"             // pasLevelsCount
#include "system/storage.h"  // eventSettingsSave/eventSettingsReset
#include "utils/utils.h"     // normalizeUserLabel, setUserLabel, htmlEscape, jsonEscape
#include "web/web_ui.h"      // getTopBarCss/getSettingsCss/getTopBarHtml/getBackMenuHtml
#include "web/html_pages.h"  // getTopBarJs

// ================= Веб: конструктор событий =================
void handleEventsPage() {
  String html=R"rawliteral(<!DOCTYPE html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>События</title><style>)rawliteral"+getTopBarCss()+getSettingsCss()+R"rawliteral(.rule{background:var(--ui-card);border:1px solid var(--ui-border);padding:9px;border-radius:8px;margin:7px 0}.rule.is-free{display:none}.rule-head{display:flex;align-items:center;gap:8px;min-height:26px;cursor:pointer}.rule-head strong{flex:1;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}.rule-head .chevron{transition:transform .15s}.rule.open .chevron{transform:rotate(180deg)}.badge{font-size:11px;color:var(--ui-muted);background:var(--ui-button);padding:3px 6px;border-radius:4px}.sys-badge{font-size:10px;color:var(--ui-accent);background:rgba(255,193,7,.15);border:1px solid var(--ui-accent);padding:2px 5px;border-radius:3px;font-weight:600}.rule-body{display:none;padding-top:8px}.rule.open .rule-body{display:block}.rule,.rule-body,.name-row,.if-box,.acts,.act{box-sizing:border-box;min-width:0;max-width:100%}.name-row{display:grid;grid-template-columns:34px minmax(0,1fr);gap:7px;align-items:center}.name-row .chk{display:flex;align-items:center;justify-content:center;margin:0;padding:7px 4px}.name-row .chk input{margin:0}.name-row .rule-name{margin:0;min-width:0;font-weight:700}.if-box,.act{position:relative;border:1px solid var(--ui-border);border-radius:7px;padding:23px 7px 7px;margin-top:7px}.flow{position:absolute;top:6px;left:7px;font-size:var(--ui-fs-tiny);line-height:1;font-weight:800;color:var(--ui-accent);text-transform:uppercase}.field-grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:6px}.condition-fields{display:flex;gap:6px;flex-wrap:wrap;margin-top:6px}.field{display:flex;flex:1 1 105px;min-width:0;flex-direction:column;gap:2px}.field label{font-size:10px;line-height:1.1;color:var(--ui-muted);white-space:nowrap;overflow:hidden;text-overflow:ellipsis}.field input,.field select{display:block;width:100%;max-width:100%;min-width:0;margin:0;padding:6px}.condition-fields .is-hidden{display:none!important}.acts{margin-top:6px}.act{display:grid;grid-template-columns:minmax(0,1fr) 34px;gap:6px}.act-fields{grid-column:1;min-width:0}.act>.icon-btn,.act>.act-spacer{grid-column:2;align-self:center}.act.is-hidden{display:none!important}.act-spacer{display:block}.value-field{margin-top:6px}.value-field.is-hidden{display:none!important}.icon-btn,.add-action{width:auto;margin:0;padding:7px 9px}.icon-btn{min-width:34px;color:var(--ui-danger)}.add-action{margin:5px 0 0;border-style:dashed;background:transparent;color:var(--ui-accent)}.rule-tools{display:flex;gap:7px;margin-top:8px}.rule-tools button{width:auto;flex:1;margin:0;padding:8px}.save-rule{color:var(--ui-success);border-color:var(--ui-success);background:var(--ui-button)}.delete-rule{color:var(--ui-danger);border-color:var(--ui-danger);background:var(--ui-button)}.save-rule:hover,.delete-rule:hover{background:var(--ui-hover)}.rule.dirty .save-rule{box-shadow:0 0 0 2px var(--ui-accent)}.rule-msg{font-size:var(--ui-fs-tiny);margin-top:6px;white-space:pre-line}.rule-msg.ok{color:var(--ui-success)}.rule-msg.err{color:var(--ui-danger)}.add-rule{border-style:dashed;background:transparent;color:var(--ui-accent)}.reset-events{color:var(--ui-warning);background:var(--ui-button)}.log{font-size:var(--ui-fs-small);color:var(--ui-muted)}#eventMsg{white-space:pre-wrap;font-size:var(--ui-fs-small);margin-top:8px;color:var(--ui-danger)}@media(max-width:420px){body{padding-left:12px;padding-right:12px}.top-bar-sticky{margin-left:-12px;margin-right:-12px}.rule{padding:8px}.if-box,.act{padding-left:6px;padding-right:6px}.field-grid{gap:4px}.field input,.field select{font-size:var(--ui-fs-small);padding:6px 4px}.field label{font-size:9px}.rule-tools{flex-direction:column}.rule-tools button{width:100%}}</style></head><body>)rawliteral"+getTopBarHtml()+getBackMenuHtml()+R"rawliteral(<h1>События</h1><p class="hint">Правило строится по схеме «Если → Тогда». Защитные ограничения газа не изменяются.</p><form id="eventsForm" method="POST" action="/settings/events/save">)rawliteral";
  const char* trig[] = {"-","Тормоз: нажатие","Тормоз: отпускание","Тормоз: удержание","Кнопка: фара","Кнопка: левый поворотник","Кнопка: правый поворотник","Кнопка: гудок","Кнопка: PAS","PAS: достигнут уровень","PAS: включён","PAS: выключен","Круиз: включён","Круиз: выключен","Загрузка (boot)"};
  const char* cond[] = {"Нажатие","Серия нажатий","Удержание"};
  const char* acts[] = {"Нет","Переключить","Включить","Выключить","Переключить","Переключить","Переключить","Переключить","Сигнал","Сигнал","Установить уровень","Переключить","BLINK","BLINK","Установить режим","Переключить режим","Показать индикатор","Показать индикатор","Показать индикатор","Показать индикатор","Показать индикатор"};
  const char* devices[] = {"—","Сервис","Фара","ДХО","Левый поворотник","Правый поворотник","Гудок","Пищалка","PAS","Свет","Дисплей: левый","Дисплей: правый","Дисплей: фара","Дисплей: гудок","Дисплей: тормоз"};
  const uint8_t actionDevice[] = {0,1,1,1,2,3,4,5,6,7,8,8,2,3,9,9,10,11,12,13,14};
  // Страница заметно больше остальных: отправляем её частями, чтобы не держать
  // весь HTML в одном String и не фрагментировать кучу ESP32.
  server.sendHeader("Cache-Control","no-store, no-cache, must-revalidate, max-age=0");
  server.sendHeader("Pragma","no-cache");
  server.sendHeader("Expires","0");
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200,"text/html; charset=utf-8","");
  server.sendContent(html);
  html="";
  for(int i=0;i<EVENT_MAX_RULES;i++){
    EventRule&r=eventRules[i];
    bool isSys=isSystemRule(i);
    String sysAttr=isSys?" data-system=\"1\"":"";
    String sysBadge=isSys?"<span class=\"sys-badge\" title=\"Системное правило: триггер, условие и приоритет защищены\">[СИСТЕМНОЕ]</span>":"";
    String disabledAttr=isSys?" disabled":"";
    html+="<div class=\"rule "+String(r.trigger==EV_NONE?"is-free":"")+"\" data-slot=\""+String(i)+"\" data-used=\""+String(r.trigger==EV_NONE?"0":"1")+"\""+sysAttr+"><div class=\"rule-head\"><strong>"+htmlEscape(eventRuleName(i))+"</strong>"+sysBadge+"<span class=\"badge\">"+String(r.enabled?"вкл":"выкл")+"</span><span class=\"chevron\">⌄</span></div><div class=\"rule-body\"><div class=\"name-row\"><label class=\"chk\" title=\"Правило включено\"><input type=\"checkbox\" name=\"en"+String(i)+"\" "+(r.enabled?"checked":"")+" aria-label=\"Включить правило\"></label><input class=\"rule-name\" type=\"text\" maxlength=\"64\" required name=\"nm"+String(i)+"\" value=\""+htmlEscape(eventRuleName(i))+"\" placeholder=\"Название правила\" aria-label=\"Название правила\"></div><div class=\"if-box\"><span class=\"flow\">Если</span><div class=\"field-grid\"><div class=\"field\"><label>Событие</label><select name=\"tr"+String(i)+"\" aria-label=\"Триггер\" title=\"Триггер\""+disabledAttr+">";
    for(int x=0;x<15;x++)html+="<option value=\""+String(x)+"\" "+(r.trigger==x?"selected":"")+">"+trig[x]+"</option>";
    html+="</select></div><div class=\"field\"><label>Тип срабатывания</label><select class=\"condition-select\" name=\"co"+String(i)+"\" aria-label=\"Тип срабатывания\" title=\"Тип срабатывания\""+disabledAttr+">";
    for(int x=0;x<3;x++)html+="<option value=\""+String(x)+"\" "+(r.condition==x?"selected":"")+">"+cond[x]+"</option>";
    html+="</select></div></div><div class=\"condition-fields\"><div class=\"field count-field\"><label>Количество нажатий</label><input type=\"number\" min=\"1\" max=\"20\" name=\"ct"+String(i)+"\" value=\""+String(r.count?r.count:1)+"\" aria-label=\"Количество нажатий\"></div><div class=\"field interval-field\"><label>Интервал, мс</label><input type=\"number\" min=\"50\" max=\"60000\" name=\"ms"+String(i)+"\" value=\""+String(r.intervalMs?r.intervalMs:700)+"\" aria-label=\"Интервал, мс\"></div><div class=\"field priority-field\"><label>Приоритет</label><input type=\"number\" min=\"0\" max=\"255\" name=\"pr"+String(i)+"\" value=\""+String(r.priority)+"\" aria-label=\"Приоритет\""+disabledAttr+"></div></div></div><div class=\"acts\">";
    for(int k=0;k<EVENT_MAX_ACTIONS;k++){
      bool show=(k==0)||r.actions[k]!=EV_NO_ACTION;
      uint8_t dev=r.actions[k]<=EV_ACTION_MAX?actionDevice[r.actions[k]]:0;
      html+="<div class=\"act"+String(show?"":" is-hidden")+"\" id=\"act"+String(i)+"_"+String(k)+"\" data-idx=\""+String(k)+"\"><span class=\"flow\">Тогда</span><div class=\"act-fields\"><div class=\"field-grid\"><div class=\"field\"><label>Устройство</label><select class=\"ev-device\" aria-label=\"Устройство\" title=\"Устройство\">";
      for(int d=0;d<9;d++)html+="<option value=\""+String(d)+"\" "+(dev==d?"selected":"")+">"+devices[d]+"</option>";
      html+="</select></div><div class=\"field\"><label>Действие</label><select class=\"ev-action\" name=\"ac"+String(i)+"_"+String(k)+"\" aria-label=\"Действие\" title=\"Действие\">";
      for(int x=0;x<=EV_ACTION_MAX;x++)html+="<option data-dev=\""+String(actionDevice[x])+"\" value=\""+String(x)+"\" "+(r.actions[k]==x?"selected":"")+">"+acts[x]+"</option>";
      html+="</select></div></div><div class=\"field value-field\"><label>Значение (мс / уровень)</label><input class=\"action-value\" type=\"number\" min=\"0\" max=\"60000\" name=\"va"+String(i)+"_"+String(k)+"\" value=\""+String(r.actionValues[k])+"\" aria-label=\"Значение действия\" title=\"Значение: мс или уровень\"></div></div>";
      if(k>0)html+="<button type=\"button\" class=\"icon-btn remove-action\" data-action=\"remove-action\" title=\"Удалить действие\" aria-label=\"Удалить действие\">×</button>";else html+="<span class=\"act-spacer\"></span>";
      html+="</div>";
    }
    html+="<button type=\"button\" id=\"add"+String(i)+"\" data-action=\"add-action\" class=\"add-action "+String(r.actions[1]!=EV_NO_ACTION&&r.actions[2]!=EV_NO_ACTION?"is-hidden":"")+"\">+ действие</button></div><div class=\"rule-tools\"><button type=\"button\" class=\"save-rule\" data-action=\"save-rule\">✓ Сохранить</button>";
    if(!isSys)html+="<button type=\"button\" class=\"delete-rule\" data-action=\"delete-rule\">Удалить</button>";
    html+="</div></div></div>";
    server.sendContent(html);
    html="";
  }
  html+=R"rawliteral(<button type="button" class="add-rule" data-action="add-rule">+ правило</button><div id="eventMsg" aria-live="polite"></div></form><form method="POST" action="/settings/events/reset" onsubmit="return confirm('Вернуть заводские правила?')"><button type="submit" class="reset-events">Сбросить правила</button></form><h2>Журнал</h2><div class="log">)rawliteral";
  if(!eventLogCount)html+="Событий пока нет";else for(int n=0;n<eventLogCount;n++){int p=(eventLogHead-1-n+10)%10;html+=htmlEscape(eventRuleName(eventLog[p].rule))+" — "+String(eventLog[p].at)+" мс<br>";}
  html+="</div>"+getTopBarJs()+R"rawliteral(<script>
function actionUi(r){const a=Number(r.querySelector('.ev-action').value),v=r.querySelector('.value-field'),label=v.querySelector('label');const uses=[9,10,11,13,14].includes(a);v.classList.toggle('is-hidden',!uses);label.textContent=a===11?'Уровень PAS':(a===13||a===14?'Период мигания, мс':'Длительность сигнала, мс')}
function evFilter(r,d){const a=r.querySelector('.ev-action');let f=null;for(const o of a.options){const ok=Number(o.dataset.dev)===d;o.hidden=!ok;o.disabled=!ok;if(ok&&!f)f=o}const cur=a.selectedOptions[0];if(cur&&cur.disabled&&f)a.value=f.value;actionUi(r)}
function evDevice(s){evFilter(s.closest('.act'),Number(s.value))}function evAction(s){const r=s.closest('.act'),o=s.selectedOptions[0];if(o)r.querySelector('.ev-device').value=o.dataset.dev;actionUi(r)}
function conditionUi(c){const mode=Number(c.querySelector('.condition-select').value),count=c.querySelector('.count-field'),interval=c.querySelector('.interval-field');count.classList.toggle('is-hidden',mode!==1);interval.classList.toggle('is-hidden',mode===0);interval.querySelector('label').textContent=mode===2?'Длительность удержания, мс':'Интервал серии, мс'}
function evShowAct(c){for(let k=1;k<3;k++){const d=c.querySelector('.act[data-idx="'+k+'"]');if(d&&d.classList.contains('is-hidden')){d.classList.remove('is-hidden');evFilter(d,Number(d.querySelector('.ev-device').value));if(k===2)c.querySelector('.add-action').classList.add('is-hidden');ruleDirty(c);return}}}
function evHideAct(d){const c=d.closest('.rule');d.classList.add('is-hidden');d.querySelector('.ev-action').value=0;d.querySelector('.ev-device').value=0;d.querySelector('.action-value').value=0;c.querySelector('.add-action').classList.remove('is-hidden');ruleDirty(c)}
function cards(){return Array.from(document.querySelectorAll('.rule[data-slot]'))}function refreshAdd(){const b=document.querySelector('.add-rule');if(b)b.disabled=cards().filter(c=>c.dataset.used==='1').length>=8}
function addRule(){const c=cards().find(x=>x.dataset.used!=='1');if(!c)return;c.classList.remove('is-free');c.classList.add('open');c.querySelector('.rule-name').value='Правило '+(Number(c.dataset.slot)+1);c.querySelector('input[name^=en]').checked=true;c.dataset.used='1';ruleDirty(c);c.querySelector('.rule-name').focus();refreshAdd()}
function ruleData(c){const values=[];function add(k,v){values.push(encodeURIComponent(k)+'='+encodeURIComponent(String(v)))}add('slot',c.dataset.slot);add('nm',c.querySelector('.rule-name').value);add('en',c.querySelector('input[name^=en]').checked?'1':'0');['tr','co','ct','ms','pr'].forEach(function(p){add(p,c.querySelector('[name^='+p+']').value)});c.querySelectorAll('.act:not(.is-hidden)').forEach(function(a){const k=a.dataset.idx;add('ac'+k,a.querySelector('.ev-action').value);add('va'+k,a.querySelector('[name^=va]').value)});return values.join('&')}
function ruleSig(c){const values=[];function add(k,v){values.push(k+'='+encodeURIComponent(String(v)))}add('nm',c.querySelector('.rule-name').value);add('en',c.querySelector('input[name^=en]').checked?'1':'0');['tr','co','ct','ms','pr'].forEach(p=>add(p,c.querySelector('[name^='+p+']').value));c.querySelectorAll('.act:not(.is-hidden)').forEach(a=>{const k=a.dataset.idx;add('ac'+k,a.querySelector('.ev-action').value);add('va'+k,a.querySelector('[name^=va]').value)});return values.join('&')}
function ruleMsg(c,ok,t){let m=c.querySelector('.rule-msg');if(!m){m=document.createElement('div');m.className='rule-msg';c.querySelector('.rule-tools').parentNode.insertBefore(m,c.querySelector('.rule-tools').nextSibling)}m.textContent=t;m.classList.toggle('ok',!!ok);m.classList.toggle('err',!ok);if(t)setTimeout(function(){if(m.textContent===t){m.textContent='';m.classList.remove('ok','err')}},6000)}
function ruleDirty(c){if(!c.dataset.orig)return;c.classList.toggle('dirty',ruleSig(c)!==c.dataset.orig)}
function ruleBusy(c,busy){c.dataset.busy=busy?'1':'0';c.querySelectorAll('.rule-tools button,.add-action,.remove-action').forEach(function(b){b.disabled=busy});const save=c.querySelector('.save-rule');if(save)save.textContent=busy?'Сохранение…':'✓ Сохранить'}
async function saveRules(slot){const c=document.querySelector('.rule[data-slot="'+slot+'"]'),name=c.querySelector('.rule-name');if(c.dataset.busy==='1')return;name.value=name.value.trim();if(!name.reportValidity())return;ruleBusy(c,true);try{const r=await fetch('/settings/events/rule/save',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded;charset=UTF-8','Cache-Control':'no-cache'},cache:'no-store',body:ruleData(c)}),data=await r.json();ruleMsg(c,r.ok,data.message||'Неизвестный ответ контроллера.');if(r.ok){name.value=data.name;c.dataset.used='1';c.dataset.orig=ruleSig(c);c.classList.remove('dirty');c.classList.add('open');c.querySelector('.rule-head strong').textContent=data.name;c.querySelector('.badge').textContent=data.enabled?'вкл':'выкл'}refreshAdd()}catch(e){ruleMsg(c,false,'Ошибка сети или ответа контроллера: '+e)}finally{ruleBusy(c,false)}}
function resetDeletedCard(c){const slot=Number(c.dataset.slot);c.querySelector('.rule-name').value='Правило '+(slot+1);c.querySelector('input[name^=en]').checked=false;c.querySelector('[name^=tr]').value=0;c.querySelector('[name^=co]').value=0;c.querySelector('[name^=ct]').value=1;c.querySelector('[name^=ms]').value=700;c.querySelector('[name^=pr]').value=0;c.querySelectorAll('.act').forEach(function(a){a.querySelector('.ev-device').value=0;a.querySelector('.ev-action').value=0;a.querySelector('.action-value').value=0;if(a.dataset.idx!=='0')a.classList.add('is-hidden')});c.querySelector('.add-action').classList.remove('is-hidden');conditionUi(c);c.querySelector('.rule-head strong').textContent='Правило '+(slot+1);c.querySelector('.badge').textContent='выкл';c.dataset.orig=ruleSig(c)}
async function deleteRule(slot){const c=document.querySelector('.rule[data-slot="'+slot+'"]');if(c.dataset.system==='1'){ruleMsg(c,false,'Системное правило не может быть удалено.');return}if(c.dataset.busy==='1'||!confirm('Удалить правило?'))return;ruleBusy(c,true);try{const r=await fetch('/settings/events/rule/delete',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded;charset=UTF-8','Cache-Control':'no-cache'},cache:'no-store',body:'slot='+encodeURIComponent(slot)}),data=await r.json();ruleMsg(c,r.ok,data.message||'Неизвестный ответ контроллера.');if(r.ok){c.dataset.used='0';resetDeletedCard(c);c.classList.add('is-free');c.classList.remove('open','dirty')}refreshAdd()}catch(e){ruleMsg(c,false,'Ошибка сети или ответа контроллера: '+e)}finally{ruleBusy(c,false)}}
cards().forEach(function(c){conditionUi(c);c.dataset.orig=ruleSig(c);c.addEventListener('input',function(){ruleDirty(c)});c.addEventListener('change',function(e){if(e.target.matches('.ev-device'))evDevice(e.target);else if(e.target.matches('.ev-action'))evAction(e.target);else if(e.target.matches('.condition-select'))conditionUi(c);ruleDirty(c)});c.querySelector('.rule-head').addEventListener('click',function(){c.classList.toggle('open')});c.addEventListener('click',function(e){const b=e.target.closest('[data-action]');if(!b)return;const action=b.dataset.action;if(action==='add-action')evShowAct(c);else if(action==='remove-action')evHideAct(b.closest('.act'));else if(action==='save-rule')saveRules(c.dataset.slot);else if(action==='delete-rule')deleteRule(c.dataset.slot)})});
const addRuleButton=document.querySelector('[data-action="add-rule"]');if(addRuleButton)addRuleButton.addEventListener('click',addRule);document.querySelectorAll('.act').forEach(r=>evFilter(r,Number(r.querySelector('.ev-device').value)));refreshAdd();
</script></body></html>)rawliteral";
  server.sendContent(html);
  server.sendContent("");
}

void handleEventsSave(){
  EventRule next[EVENT_MAX_RULES];memset(next,0,sizeof(next));
  char nextNames[EVENT_MAX_RULES][USER_LABEL_SIZE] = {};
  String errors="",warnings="";
  for(int i=0;i<EVENT_MAX_RULES;i++){
    EventRule &r=next[i];
    String ruleLabel=server.arg("nm"+String(i));
    if(!normalizeUserLabel(ruleLabel)) errors+="Правило "+String(i+1)+": название должно содержать от 1 до 64 символов, без < и >\n";
    else {
      setUserLabel(nextNames[i],ruleLabel);
      for(int j=0;j<i;j++) if(String(nextNames[j]).length()&&ruleLabel.equalsIgnoreCase(String(nextNames[j]))) errors+="Названия правил не должны повторяться: "+ruleLabel+"\n";
    }
    r.enabled=server.hasArg("en"+String(i));
    r.trigger=server.arg("tr"+String(i)).toInt();
    r.condition=server.arg("co"+String(i)).toInt();
    r.count=server.arg("ct"+String(i)).toInt();
    r.intervalMs=server.arg("ms"+String(i)).toInt();
    r.priority=server.arg("pr"+String(i)).toInt();
    // Сбор непустых результатов и уплотнение в начало списка без дыр.
    int nActs=0;
    for(int k=0;k<EVENT_MAX_ACTIONS;k++){
      uint8_t a=server.arg("ac"+String(i)+"_"+String(k)).toInt();
      int16_t v=server.arg("va"+String(i)+"_"+String(k)).toInt();
      if(a!=EV_NO_ACTION){
        if(nActs<k)warnings+="Правило "+String(i+1)+": результат "+String(k+1)+" перемещён в слот "+String(nActs+1)+"\n";
        r.actions[nActs]=a;r.actionValues[nActs]=v;nActs++;
      }
    }
    if(r.enabled&&nActs==0)errors+="Правило "+String(i+1)+": добавьте хотя бы один результат\n";
    for(int k=0;k<nActs;k++){
      if(r.enabled&&(r.actions[k]<1||r.actions[k]>EV_ACTION_MAX))errors+="Правило "+String(i+1)+": неверное действие в результате "+String(k+1)+"\n";
      if(r.enabled&&r.actions[k]==EV_PAS_SET_LEVEL&&(r.actionValues[k]<0||r.actionValues[k]>pasLevelsCount))errors+="Правило "+String(i+1)+": уровень PAS в результате "+String(k+1)+" 0.."+String(pasLevelsCount)+"\n";
      if(r.enabled&&(r.actions[k]==EV_LIGHT_BLINK||r.actions[k]==EV_DRL_BLINK)&&(r.actionValues[k]<100||r.actionValues[k]>5000))errors+="Правило "+String(i+1)+": BLINK должен быть 100..5000 мс\n";
      for(int j=0;j<k;j++)if(r.actions[j]==r.actions[k])warnings+="Правило "+String(i+1)+": действие повторяется в результатах "+String(j+1)+" и "+String(k+1)+"\n";
    }
    if(r.enabled&&(r.trigger<1||r.trigger>EV_TRIGGER_MAX))errors+="Правило "+String(i+1)+": неверный триггер\n";
    if(r.condition==EV_PRESS_COUNT&&(r.count<1||r.count>20))errors+="Правило "+String(i+1)+": количество 1..20\n";
    if(r.trigger==EV_PAS_LEVEL&&(r.count<1||r.count>pasLevelsCount))errors+="Правило "+String(i+1)+": уровень PAS 1.."+String(pasLevelsCount)+"\n";
    if(r.enabled&&(r.trigger==EV_PAS_LEVEL||r.trigger==EV_PAS_ON||r.trigger==EV_PAS_OFF||r.trigger==EV_CRUISE_ON||r.trigger==EV_CRUISE_OFF||r.trigger==EV_BOOT)&&r.condition!=EV_NONE)errors+="Правило "+String(i+1)+": для этого триггера условие не используется\n";
    if(r.enabled&&r.trigger!=EV_BOOT&&r.intervalMs<50)errors+="Правило "+String(i+1)+": интервал не меньше 50 мс\n";
    for(int j=0;j<i;j++)if(r.enabled&&next[j].enabled&&r.trigger==next[j].trigger&&r.condition==next[j].condition)warnings+="Дублируются правила "+String(j+1)+" и "+String(i+1)+"\n";
  }
  if(errors.length()){server.send(400,"text/plain","Ошибки, правила не сохранены:\n"+errors);return;}
  memcpy(eventRules,next,sizeof(eventRules));memcpy(eventRuleNames,nextNames,sizeof(eventRuleNames));memset(eventRuntime,0,sizeof(eventRuntime));eventSettingsSave();
  server.send(200,"text/plain","Правила сохранены и применены без перезагрузки."+(warnings.length()?"\nПредупреждения:\n"+warnings:""));
}
void handleEventRuleSave(){
  int slot=server.arg("slot").toInt();
  if(!server.hasArg("slot")||slot<0||slot>=EVENT_MAX_RULES){server.send(400,"application/json","{\"message\":\"Неверный слот правила.\"}");return;}
  
  // Блокировка изменения системных правил (триггер/условие защищены)
  if(isSystemRule(slot)){
    const EventRule &existing=eventRules[slot];
    uint8_t incomingTrigger=server.arg("tr").toInt();
    uint8_t incomingCondition=server.arg("co").toInt();
    if(incomingTrigger!=existing.trigger||incomingCondition!=existing.condition){
      server.send(403,"application/json","{\"message\":\"Системное правило: триггер и условие не могут быть изменены.\"}");
      return;
    }
  }
  
  EventRule r={}; String errors="";
  String label=server.arg("nm");
  if(!normalizeUserLabel(label))errors+="Название должно содержать от 1 до 64 символов, без < и >.\n";
  for(int i=0;i<EVENT_MAX_RULES;i++)if(i!=slot&&eventRules[i].trigger!=EV_NONE&&label.equalsIgnoreCase(eventRuleName(i)))errors+="Название правила уже используется.\n";
  r.enabled=server.arg("en")=="1";
  r.trigger=server.arg("tr").toInt(); r.condition=server.arg("co").toInt();
  r.count=server.arg("ct").toInt(); r.intervalMs=server.arg("ms").toInt(); r.priority=server.arg("pr").toInt();
  int nActs=0;
  for(int k=0;k<EVENT_MAX_ACTIONS;k++){
    uint8_t a=server.arg("ac"+String(k)).toInt(); int16_t v=server.arg("va"+String(k)).toInt();
    if(a!=EV_NO_ACTION&&nActs<EVENT_MAX_ACTIONS){r.actions[nActs]=a;r.actionValues[nActs]=v;nActs++;}
  }
  if(r.trigger<1||r.trigger>EV_TRIGGER_MAX)errors+="Выберите триггер.\n";
  if(nActs==0)errors+="Добавьте хотя бы одно действие.\n";
  if(r.condition==EV_PRESS_COUNT&&(r.count<1||r.count>20))errors+="Количество должно быть 1..20.\n";
  if(r.trigger==EV_PAS_LEVEL&&(r.count<1||r.count>pasLevelsCount))errors+="Уровень PAS должен быть 1.."+String(pasLevelsCount)+".\n";
  if((r.trigger==EV_PAS_LEVEL||r.trigger==EV_PAS_ON||r.trigger==EV_PAS_OFF||r.trigger==EV_CRUISE_ON||r.trigger==EV_CRUISE_OFF||r.trigger==EV_BOOT)&&r.condition!=EV_NONE)errors+="Для этого триггера условие не используется.\n";
  if(r.trigger!=EV_BOOT&&r.intervalMs<50)errors+="Интервал должен быть не меньше 50 мс.\n";
  for(int k=0;k<nActs;k++){
    if(r.actions[k]<1||r.actions[k]>EV_ACTION_MAX)errors+="Неверное действие.\n";
    if(r.actions[k]==EV_PAS_SET_LEVEL&&(r.actionValues[k]<0||r.actionValues[k]>pasLevelsCount))errors+="Уровень PAS в действии должен быть 0.."+String(pasLevelsCount)+".\n";
    if((r.actions[k]==EV_LIGHT_BLINK||r.actions[k]==EV_DRL_BLINK)&&(r.actionValues[k]<100||r.actionValues[k]>5000))errors+="BLINK должен быть 100..5000 мс.\n";
  }
  if(errors.length()){server.send(400,"application/json","{\"message\":\""+jsonEscape(errors)+"\"}");return;}
  EventRule previousRule=eventRules[slot];
  char previousName[USER_LABEL_SIZE]; memcpy(previousName,eventRuleNames[slot],sizeof(previousName));
  eventRules[slot]=r; setUserLabel(eventRuleNames[slot],label);
  // Принудительная валидация системных правил перед сохранением
  enforceSystemRules(eventRules[slot],slot);
  if(!eventSettingsSave()){
    eventRules[slot]=previousRule; memcpy(eventRuleNames[slot],previousName,sizeof(previousName));
    server.send(500,"application/json","{\"message\":\"Ошибка записи настроек в память ESP32.\"}");return;
  }
  memset(&eventRuntime[slot],0,sizeof(eventRuntime[slot]));
  server.sendHeader("Cache-Control","no-store");
  server.send(200,"application/json","{\"slot\":"+String(slot)+",\"name\":\""+jsonEscape(eventRuleName(slot))+"\",\"enabled\":"+String(r.enabled?"true":"false")+",\"message\":\"Правило сохранено и применено.\"}");
}
void handleEventRuleDelete(){
  int slot=server.arg("slot").toInt();
  if(!server.hasArg("slot")||slot<0||slot>=EVENT_MAX_RULES){server.send(400,"application/json","{\"message\":\"Неверный слот правила.\"}");return;}
  
  // Блокировка удаления системных правил
  if(isSystemRule(slot)){
    server.send(403,"application/json","{\"message\":\"Системное правило не может быть удалено.\"}");
    return;
  }
  
  EventRule previousRule=eventRules[slot];
  char previousName[USER_LABEL_SIZE]; memcpy(previousName,eventRuleNames[slot],sizeof(previousName));
  memset(&eventRules[slot],0,sizeof(eventRules[slot])); memset(&eventRuleNames[slot],0,sizeof(eventRuleNames[slot]));
  if(!eventSettingsSave()){
    eventRules[slot]=previousRule; memcpy(eventRuleNames[slot],previousName,sizeof(previousName));
    server.send(500,"application/json","{\"message\":\"Ошибка записи настроек в память ESP32.\"}");return;
  }
  memset(&eventRuntime[slot],0,sizeof(eventRuntime[slot]));
  server.sendHeader("Cache-Control","no-store");
  server.send(200,"application/json","{\"message\":\"Правило удалено.\"}");
}
void handleEventsReset(){eventSettingsReset();memset(eventRuntime,0,sizeof(eventRuntime));server.sendHeader("Location","/settings/events");server.send(303,"text/plain","");}

