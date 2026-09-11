/*
 * MiRemoteBridge - Web UI page (served from PROGMEM)
 *
 * Layout inspired by the key-mapping screen of GetSayAll/remote-mic-app-windows
 * (GPL-3.0). One key maps to one desktop action; presses and releases are
 * forwarded live, exactly like the built-in map.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <pgmspace.h>

static const char kIndexHtml[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html lang="zh">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>MiRemoteBridge 按键映射</title>
<style>
:root{--bg:#f4f5fa;--card:#fff;--line:#e6e9f2;--ink:#1c2333;--sub:#8a93a6;--blue:#3b6ef6;--blue2:#eef2ff;--ok:#18a058;--warn:#c8801a}
*{box-sizing:border-box;margin:0}
body{background:var(--bg);color:var(--ink);font-family:system-ui,-apple-system,"Segoe UI","PingFang SC","Microsoft YaHei",sans-serif;padding:14px;max-width:1000px;margin:0 auto}
header{display:flex;align-items:center;gap:10px;flex-wrap:wrap;margin:6px 0 18px}
h1{font-size:21px;margin-right:4px}
.badge{font-size:12px;border:1px solid var(--line);border-radius:999px;padding:4px 12px;background:var(--card);color:var(--sub)}
.badge.on{color:var(--ok);border-color:#bfe8d2}
.badge.off{color:var(--warn);border-color:#f0dfc0}
.grid{display:grid;grid-template-columns:1fr 210px 1fr;gap:12px;align-items:start}
@media(max-width:760px){.grid{grid-template-columns:1fr}.remote{display:none}}
.col{display:flex;flex-direction:column;gap:10px}
.card{background:var(--card);border:1px solid var(--line);border-radius:14px;padding:12px 14px}
.card h3{font-size:14px;margin-bottom:9px;display:flex;align-items:center;gap:8px}
.ico{width:22px;height:22px;border-radius:6px;background:var(--blue2);color:var(--blue);display:inline-flex;align-items:center;justify-content:center;font-size:13px;flex:none}
.slot{display:flex;align-items:center;justify-content:space-between;gap:8px;background:#f2f4f9;border-radius:10px;padding:9px 12px;cursor:pointer}
.slot:hover{background:var(--blue2)}
.slot .lab{font-size:12px;color:var(--sub)}
.slot .val{font-size:13px}
.slot .val.none{color:var(--sub)}
.remote{background:linear-gradient(180deg,#fafbfd,#e9ebf1);border:1px solid var(--line);border-radius:16px;padding:16px 12px;display:flex;flex-direction:column;align-items:center;gap:10px}
.remote .top{display:flex;gap:26px}
.kcircle{width:34px;height:34px;border-radius:50%;border:1.5px solid #c9cedd;background:#fff;display:flex;align-items:center;justify-content:center;font-size:13px;color:#5a627a}
.dpad{display:grid;grid-template-columns:repeat(3,32px);grid-template-rows:repeat(3,32px);gap:3px;align-items:center;justify-items:center}
.dpad .center{width:46px;height:46px;border-radius:50%;border:1.5px solid #c9cedd;background:#fff;display:flex;align-items:center;justify-content:center;font-size:11px;color:#5a627a}
footer{display:flex;gap:10px;align-items:center;margin-top:16px;flex-wrap:wrap}
button{border:1px solid var(--line);background:var(--card);color:var(--ink);border-radius:10px;padding:9px 16px;font-size:13px;cursor:pointer}
button.primary{background:var(--blue);border-color:var(--blue);color:#fff}
button.danger{color:#c0392b;border-color:#efc9c4}
button:hover{filter:brightness(.98)}
#toast{position:fixed;bottom:20px;left:50%;transform:translateX(-50%);background:#1c2333;color:#fff;font-size:13px;border-radius:10px;padding:9px 18px;display:none;z-index:20}
.editor{position:fixed;inset:0;background:rgba(18,22,38,.45);display:none;align-items:center;justify-content:center;z-index:10}
.panel{background:#fff;border-radius:16px;padding:18px;width:min(430px,94vw);max-height:92vh;overflow:auto}
.panel h3{font-size:15px;margin-bottom:12px}
.row{display:flex;gap:10px;margin-bottom:12px;flex-wrap:wrap}
.opt{flex:1;min-width:110px}
.opt .t{font-size:12px;color:var(--sub);margin-bottom:6px}
select,input[type=text]{width:100%;border:1px solid var(--line);border-radius:8px;padding:8px;font-size:13px;background:#fff;color:var(--ink)}
.mods{display:flex;gap:8px;flex-wrap:wrap}
.mod{border:1px solid var(--line);border-radius:8px;padding:7px 12px;font-size:13px;cursor:pointer;user-select:none}
.mod.on{background:var(--blue2);border-color:var(--blue);color:var(--blue)}
.panel .acts{display:flex;gap:10px;margin-top:6px}
.hint{font-size:12px;color:var(--sub);margin:8px 0}
</style>
</head>
<body>
<header>
  <h1>按键映射</h1>
  <span class="badge" id="wifiBadge">Wi-Fi 配置热点</span>
  <span class="badge" id="rcBadge">遥控器：…</span>
  <span class="badge" id="hostBadge">主机：…</span>
</header>

<div class="grid" id="grid">
  <div class="col" id="colL"></div>
  <div class="remote">
    <div class="top"><span class="kcircle">⏻</span><span class="kcircle">🎤</span></div>
    <div class="dpad">
      <span></span><span class="kcircle">▲</span><span></span>
      <span class="kcircle">◀</span><span class="center">OK</span><span class="kcircle">▶</span>
      <span></span><span class="kcircle">▼</span><span></span>
    </div>
    <div class="top"><span class="kcircle">⌂</span><span class="kcircle">☰</span></div>
    <div style="font-size:11px;color:#8a93a6;text-align:center">Mi Remote Bridge</div>
  </div>
  <div class="col" id="colR"></div>
</div>

<footer>
  <button class="danger" onclick="resetAll()">恢复默认映射</button>
  <span class="badge" id="countBadge"></span>
</footer>

<div class="editor" id="editor">
  <div class="panel">
    <h3 id="edTitle">配置按键</h3>
    <div class="row">
      <div class="opt"><div class="t">动作类型</div>
        <select id="kind" onchange="kindChanged()">
          <option value="0">清除（恢复默认映射）</option>
          <option value="1">电脑键盘快捷键</option>
          <option value="2">媒体键 / 系统控制</option>
        </select>
      </div>
    </div>
    <div id="kbArea">
      <div class="row">
        <div class="opt"><div class="t">修饰键（可多选）</div>
          <div class="mods">
            <span class="mod" data-bit="1" onclick="toggleMod(this)">Ctrl</span>
            <span class="mod" data-bit="2" onclick="toggleMod(this)">Shift</span>
            <span class="mod" data-bit="4" onclick="toggleMod(this)">Alt</span>
            <span class="mod" data-bit="8" onclick="toggleMod(this)">Win</span>
          </div>
        </div>
      </div>
      <div class="row">
        <div class="opt"><div class="t">主键</div><select id="key"></select></div>
      </div>
    </div>
    <div id="consArea">
      <div class="row">
        <div class="opt"><div class="t">媒体 / 系统动作</div><select id="cons"></select></div>
      </div>
    </div>
    <div class="acts">
      <button class="primary" onclick="save()">保存</button>
      <button onclick="closeEditor()">取消</button>
      <button onclick="clearBinding()">清除绑定</button>
    </div>
    <div class="hint">按下与抬起会实时转发给电脑；此处只改变“这个键发出什么”，不改变转发方式。</div>
  </div>
</div>

<div id="toast"></div>

<script>
var KEYS = [
  {raw:0x66, name:"电源", ico:"⏻",  col:"L"},
  {raw:0x52, name:"上",    ico:"↑",  col:"L"},
  {raw:0x50, name:"左",    ico:"←",  col:"L"},
  {raw:0xF1, name:"返回",  ico:"↩",  col:"L"},
  {raw:0x4A, name:"主页",  ico:"⌂",  col:"L"},
  {raw:0x65, name:"菜单",  ico:"☰",  col:"L"},
  {raw:0x3E, name:"语音",  ico:"🎤", col:"R"},
  {raw:0x4F, name:"右",    ico:"→",  col:"R"},
  {raw:0x28, name:"确定",  ico:"◎",  col:"R"},
  {raw:0x51, name:"下",    ico:"↓",  col:"R"},
  {raw:0x80, name:"音量+", ico:"🔊", col:"R"},
  {raw:0x81, name:"音量−", ico:"🔉", col:"R"},
  {raw:0x35, name:"TV",    ico:"📺", col:"R"}
];

var KB_KEYS = [
  [0x04,"A"],[0x05,"B"],[0x06,"C"],[0x07,"D"],[0x08,"E"],[0x09,"F"],[0x0A,"G"],[0x0B,"H"],
  [0x0C,"I"],[0x0D,"J"],[0x0E,"K"],[0x0F,"L"],[0x10,"M"],[0x11,"N"],[0x12,"O"],[0x13,"P"],
  [0x14,"Q"],[0x15,"R"],[0x16,"S"],[0x17,"T"],[0x18,"U"],[0x19,"V"],[0x1A,"W"],[0x1B,"X"],
  [0x1C,"Y"],[0x1D,"Z"],
  [0x1E,"1"],[0x1F,"2"],[0x20,"3"],[0x21,"4"],[0x22,"5"],[0x23,"6"],[0x24,"7"],[0x25,"8"],
  [0x26,"9"],[0x27,"0"],
  [0x2C,"Space"],[0x2A,"Backspace"],[0x29,"Esc"],[0x28,"Enter"],[0x2B,"Tab"],
  [0x4F,"→"],[0x50,"←"],[0x51,"↓"],[0x52,"↑"],
  [0x3A,"F1"],[0x3B,"F2"],[0x3C,"F3"],[0x3D,"F4"],[0x3E,"F5"],[0x3F,"F6"],
  [0x40,"F7"],[0x41,"F8"],[0x42,"F9"],[0x43,"F10"],[0x44,"F11"],[0x45,"F12"]
];

var CONS_KEYS = [
  [0x00E9,"音量 +"],[0x00EA,"音量 −"],[0x00E2,"静音"],[0x00CD,"播放/暂停"],
  [0x00B5,"下一曲"],[0x00B6,"上一曲"],[0x0223,"AC Home"],[0x0224,"AC 后退"],
  [0x0030,"电源"],[0x0032,"睡眠"]
];

var MODS = {1:"Ctrl",2:"Shift",4:"Alt",8:"Win"};
var state = {bindings:{}, defaults:{}, editing:0, mods:0};

function fmt(mod, key, cons, kind){
  if(kind===2){
    for(var i=0;i<CONS_KEYS.length;i++) if(CONS_KEYS[i][0]===cons) return "媒体: "+CONS_KEYS[i][1];
    return "媒体: 0x"+cons.toString(16).toUpperCase();
  }
  var m=[];
  for(var b in MODS) if(mod & +b) m.push(MODS[b]);
  var k="?";
  for(var j=0;j<KB_KEYS.length;j++) if(KB_KEYS[j][0]===key) k=KB_KEYS[j][1];
  if(key===0) return m.length?m.join(" + "):"—";
  return (m.length?m.join(" + ")+" + ":"") + k;
}
function toast(t){var e=document.getElementById("toast");e.textContent=t;e.style.display="block";setTimeout(function(){e.style.display="none"},2200);}
function toggleMod(el){state.mods ^= +el.dataset.bit; el.classList.toggle("on");}
function kindChanged(){
  var k=+document.getElementById("kind").value;
  document.getElementById("kbArea").style.display = (k===1)?"block":"none";
  document.getElementById("consArea").style.display = (k===2)?"block":"none";
}
function openEditor(raw){
  state.editing=raw; state.mods=0;
  var b=state.bindings[raw];
  document.getElementById("kind").value = b?String(b.kind):"0";
  document.getElementById("cons").value = b?String(b.cons):"0x00E9";
  document.getElementById("key").value = b?String(b.key):"0x04";
  document.querySelectorAll(".mod").forEach(function(el){el.classList.remove("on"); if(b&&(b.mod & +el.dataset.bit)) el.classList.add("on");});
  document.getElementById("edTitle").textContent = "配置按键: " + nameOf(raw);
  kindChanged();
  document.getElementById("editor").style.display="flex";
}
function closeEditor(){document.getElementById("editor").style.display="none";}
function clearBinding(){ post({raw:state.editing,kind:0,mod:0,key:0,cons:0}); closeEditor(); }
function save(){
  var k=+document.getElementById("kind").value;
  var o={raw:state.editing,kind:k,mod:state.mods,
         key:+document.getElementById("key").value,
         cons:+document.getElementById("cons").value};
  post(o); closeEditor();
}
function post(o){
  var q=Object.keys(o).map(function(k){return k+"="+o[k]}).join("&");
  fetch("/api/set?"+q,{method:"POST"}).then(function(r){return r.json()})
    .then(function(j){ toast(j.ok?"已保存":"保存失败: "+(j.error||"?")); load(); })
    .catch(function(){ toast("请求失败"); });
}
function resetAll(){
  if(!confirm("清除全部自定义绑定，恢复默认映射？")) return;
  fetch("/api/reset",{method:"POST"}).then(function(){ toast("已恢复默认"); load(); });
}
function nameOf(raw){ for(var i=0;i<KEYS.length;i++) if(KEYS[i].raw===raw) return KEYS[i].name; return "0x"+raw.toString(16); }
function render(){
  var L=document.getElementById("colL"), R=document.getElementById("colR");
  L.innerHTML=""; R.innerHTML="";
  KEYS.forEach(function(k){
    var b=state.bindings[k.raw];
    var d=state.defaults[k.raw];
    var desc = b ? fmt(b.mod,b.key,b.cons,b.kind) : (d?("默认: "+d):"—");
    var card=document.createElement("div");
    card.className="card";
    card.innerHTML='<h3><span class="ico">'+k.ico+'</span>'+k.name+'</h3>'+
      '<div class="slot" onclick="openEditor('+k.raw+')">'+
      '<div><div class="lab">电脑快捷键</div><div class="val'+(b?"":" none")+'">'+desc+'</div></div>'+
      '<div class="edit">编辑</div></div>';
    (k.col==="L"?L:R).appendChild(card);
  });
}
function load(){
  fetch("/api/bindings").then(function(r){return r.json()}).then(function(j){
    state.bindings={}; state.defaults={};
    (j.bindings||[]).forEach(function(b){ state.bindings[b.raw]=b; });
    (j.defaults||[]).forEach(function(d){ state.defaults[d.raw]=fmt(d.mod,d.key,d.cons,d.kind); });
    render();
    document.getElementById("countBadge").textContent = "自定义绑定: "+(j.bindings?j.bindings.length:0);
  });
  fetch("/api/status").then(function(r){return r.json()}).then(function(j){
    var rc=document.getElementById("rcBadge"), h=document.getElementById("hostBadge"), w=document.getElementById("wifiBadge");
    rc.textContent="遥控器: "+(j.remoteConnected?j.remoteName+" 已连接":"未连接");
    rc.className="badge "+(j.remoteConnected?"on":"off");
    h.textContent="主机: "+(j.hostConnected?"已连接":"未连接");
    h.className="badge "+(j.hostConnected?"on":"off");
    w.textContent="配置热点: "+j.ap;
  });
}
(function(){
  var sel=document.getElementById("key");
  KB_KEYS.forEach(function(k){var o=document.createElement("option");o.value=k[0];o.textContent=k[1];sel.appendChild(o);});
  var cs=document.getElementById("cons");
  CONS_KEYS.forEach(function(k){var o=document.createElement("option");o.value="0x"+k[0].toString(16);o.textContent=k[1];cs.appendChild(o);});
  load(); setInterval(load,5000);
})();
</script>
</body>
</html>)rawliteral";
