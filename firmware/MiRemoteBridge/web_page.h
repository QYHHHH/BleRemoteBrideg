/*
 * MiRemoteBridge - Web UI page, served from PROGMEM.
 *
 * Authoring source for the offline preview and generated HTML/CSS/JS assets.
 * gen_web_page.py splits and gzips it; firmware streams the flash-resident
 * arrays without a whole-page allocation. CSS responsive layouts cost flash,
 * not extra MCU heap. Status polling continues while an editor is open.
 *
 * One physical key maps to exactly one HID action; presses and releases are
 * forwarded live. No gestures, macros or audio.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once
#include <pgmspace.h>

static const char kIndexHtml[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html lang="zh-CN"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<meta name="color-scheme" content="light"><title>MiRemoteBridge 按键映射</title>
<style>
:root{--bg:#f6f7f9;--card:#fff;--line:#e6e8ec;--ink:#252931;--mut:#78818e;--blue:#1674ed;--soft:#edf5ff;--ok:#328564;--warn:#aa6d22}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--ink);font:14px/1.6 -apple-system,BlinkMacSystemFont,"Segoe UI","Microsoft YaHei",sans-serif}
button{font:inherit;color:inherit;cursor:pointer}button:disabled{cursor:not-allowed;opacity:.45}a{color:var(--blue)}
[hidden]{display:none!important}h1,h2,h3,p{margin:0}h1{font-size:22px}h2{font-size:15px}h3{font-size:13px;font-weight:600}
.mut{color:var(--mut)}.mono{font:10px ui-monospace,Consolas,monospace;letter-spacing:1px}
.pill{display:inline-flex;align-items:center;gap:6px;padding:4px 10px;border:1px solid var(--line);border-radius:99px;background:#fff;font-size:11px;color:var(--mut)}
.pill.ok{color:var(--ok);border-color:#cfe9db}.pill.off{color:var(--warn);border-color:#f0dfc0}
.pill i{width:6px;height:6px;border-radius:50%;background:currentColor}
.bar{display:flex;align-items:center;gap:12px;flex-wrap:wrap;padding:14px 18px;border-bottom:1px solid var(--line);background:#fff}
.bar b{font-size:14px}.bar .grow{flex:1}
main{max-width:1180px;margin:0 auto;padding:18px 18px 24px}
.card{background:var(--card);border:1px solid var(--line);border-radius:12px;box-shadow:0 4px 18px #26324904}
.hero{display:flex;align-items:center;gap:18px;padding:18px 20px;margin-bottom:14px}
.hero .t{flex:1;min-width:0}.hero h1{margin-bottom:4px}.hero p{font-size:12px;color:var(--mut)}
.btn{display:inline-flex;align-items:center;gap:6px;min-height:36px;padding:7px 14px;border:1px solid #dfe3e9;border-radius:7px;background:#fff;font-size:12px;white-space:nowrap}
.btn:hover{background:#f8fafc}.btn.p{background:var(--blue);border-color:var(--blue);color:#fff}.btn.p:hover{background:#0966d9}
.btn.d{color:#b45345}
.head{display:flex;align-items:center;justify-content:space-between;gap:12px;margin:0 0 10px}
.grid{display:grid;grid-template-columns:minmax(0,1fr) 190px minmax(0,1fr);gap:12px;align-items:center;position:relative}
.col{display:flex;flex-direction:column;gap:8px;position:relative;z-index:1}
#wires{position:absolute;inset:0;width:100%;height:100%;pointer-events:none;z-index:0;overflow:visible}
#wires path{fill:none;stroke:#cbd8e6;stroke-width:1.5;transition:stroke .1s}
#wires path.live{stroke:#327ede;stroke-width:2.5}
.k{border:1px solid #e7e9ed;background:#f9fafb;border-radius:9px;overflow:hidden;transition:.15s}
.k.sel{background:var(--soft);border-color:#9bc5fb}
.k.live,.rb.live{background:#d7ecff;border-color:#327ede;box-shadow:0 0 0 3px #327ede26}
.k.live .act,.rb.live{color:#1257b5}
.rb.live{color:#fff;background:#327ede}
.rb.pulse{background:#5b83e0;box-shadow:0 0 0 3px #327ede59}
.k:hover{border-color:#b2c9e6}
.k button{display:block;width:100%;text-align:left;background:none;border:0;padding:9px 11px;min-height:62px}
.k .r1{display:flex;align-items:center;gap:6px;font-size:12px;color:#67717d}
.k .r1 b{color:var(--ink);font-weight:600}
.k .tg{margin-left:auto;font-size:9px;color:#8d96a1}
.k.cus .tg{color:var(--blue)}
.k .r2{display:flex;justify-content:space-between;gap:8px;align-items:center;padding-top:5px}
.k .act{font-size:12px;font-weight:600;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.k .ed{font-size:10px;color:#9aa3af}
.rm{display:flex;flex-direction:column;align-items:center;gap:12px;position:relative;z-index:1}
.rm .body{width:132px;height:410px;border:1px solid #bfc4c7;border-radius:15px 15px 18px 18px;background:linear-gradient(90deg,#c6c9cb,#e4e5e4 8%,#f2f2f0 45%,#e5e5e3 92%,#b9bdbf);position:relative;box-shadow:7px 11px 18px #27344210;flex:none}
.rm .body:after{content:"RC003";position:absolute;bottom:26px;width:100%;text-align:center;font:10px ui-monospace,Consolas,monospace;letter-spacing:3px;color:#a3a7a9}
.rb{position:absolute;display:grid;place-items:center;width:32px;height:32px;padding:0;border:1px solid #61666a;border-radius:50%;background:#53595d;color:#f4f5f5;font-size:13px}
.rb:hover{background:#606d7a}.rb.sel{background:#327ede;border-color:#286eca;color:#fff}
.rb.top{top:16px;width:25px;height:25px;background:#e9eae8;color:#646b71;border-color:#a4aaae}
.rb.top.sel{background:#ecf4ff;color:var(--blue);border-color:var(--blue)}
.p1{left:26px}.p2{left:86px}
/* Positions measured off the remote itself, expressed as proportions so the
   drawing survives any body size:
     top row     centre y 9%   - x 29% (power) and 74% (voice)
     disc        centre y 22%  - diameter 80% of the body width
     columns     centre y 37% / 47% / 57%, x 29% (left) and 74% (right)
   The two columns line up horizontally: volume + and - sit at the same heights
   as back and home, TV at the same height as menu. Everything below the TV row
   is empty body, exactly like the real remote.
   Every key stays a DIRECT child of .body (the selection/live highlight walks
   rmArt.children), so all coordinates are relative to the body and the disc
   itself is painted by ::before underneath them. */
.p3{left:22px;top:138px}.p4{left:22px;top:176px}.p5{left:22px;top:217px}
.p6{left:82px;top:137px;height:34px;border-radius:17px 17px 6px 6px}
.p7{left:82px;top:173px;height:34px;border-radius:6px 6px 17px 17px}
.p8{left:82px;top:217px}
.rm .body:before{content:"";position:absolute;left:13px;top:37px;width:106px;height:106px;border-radius:50%;background:radial-gradient(circle at 42% 34%,#3f454a,#22262a 72%);box-shadow:inset 0 3px 7px #0000004d,0 4px 11px #1c2a3a2b}
.rb.du,.rb.dd,.rb.dl,.rb.dr{width:34px;height:34px;border:0;background:none;color:#c9d0d5;font-size:13px}
.rb.du{left:49px;top:40px}.rb.dd{left:49px;top:106px}
.rb.dl{left:16px;top:73px}.rb.dr{left:82px;top:73px}
.rb.du:hover,.rb.dd:hover,.rb.dl:hover,.rb.dr:hover{background:#ffffff1c}
.rb.du.sel,.rb.dd.sel,.rb.dl.sel,.rb.dr.sel{background:#327ede;color:#fff}
.rb.do{left:43px;top:67px;width:47px;height:47px;font-size:10px;background:#343a3f;color:#e6ebee;border-color:#23282c}
.rb.do.sel{background:#327ede;border-color:#286eca}
.cap{text-align:center;font-size:9px;color:#97a0aa;line-height:1.8}.cap b{display:block;color:#6d7781;font-size:10px;letter-spacing:2px;font-weight:500}
.foot{display:flex;align-items:center;justify-content:space-between;gap:12px;padding:13px 18px;margin-top:14px;flex-wrap:wrap}
.foot p{font-size:11px;color:var(--mut)}.foot .bs{display:flex;gap:8px}
.note{font-size:11px;color:#919aa4;margin-top:10px}
dialog{border:1px solid var(--line);border-radius:14px;padding:0;width:min(460px,calc(100vw - 24px));max-height:calc(100dvh - 30px);overflow:auto;color:var(--ink);box-shadow:0 22px 70px #1d2e442b}
dialog::backdrop{background:#26354b38}
.dh{display:flex;align-items:center;justify-content:space-between;padding:18px 20px 14px;border-bottom:1px solid var(--line)}
.db{padding:16px 20px}.da{display:flex;gap:8px;justify-content:flex-end;padding:14px 20px;border-top:1px solid var(--line)}
.da .rs{margin-right:auto;border:0;background:none;font-size:11px;color:#788698;padding:0}
label.f{display:block;font-size:12px;color:#67717e;margin-bottom:14px}
select{display:block;width:100%;margin-top:6px;padding:9px;min-height:40px;border:1px solid #dce1e8;border-radius:7px;background:#fff;color:var(--ink);font:inherit}
.ms{display:grid;grid-template-columns:repeat(4,1fr);gap:6px;margin-bottom:14px}
.m{padding:7px 2px;border:1px solid var(--line);border-radius:6px;background:#f6f8fa;font-size:11px}
.m[aria-pressed=true]{background:var(--soft);border-color:#a1c9fd;color:var(--blue)}
.pv{padding:12px 14px;border:1px solid var(--line);border-radius:8px;background:#f6f8fb;font-size:16px;font-weight:600;word-break:break-all}
.pv small{display:block;font-size:10px;font-weight:400;color:var(--mut)}
.hint{font-size:11px;color:var(--mut);line-height:1.8;margin-top:11px}
.err{font-size:12px;line-height:1.7;color:#b04937;background:#fff4f0;border-radius:6px;padding:9px 11px;margin-top:11px}
#toast{position:fixed;left:50%;bottom:20px;transform:translateX(-50%);max-width:calc(100vw - 24px);padding:11px 18px;border:1px solid #cadbe9;border-radius:9px;background:#fff;box-shadow:0 8px 26px #2038541a;color:#416080;font-size:12px;z-index:9;text-align:center}
.warn{background:#fff8ee;border:1px solid #f1dfc4;border-radius:8px;padding:10px 14px;margin-bottom:12px;font-size:12px;color:#906b34;display:flex;gap:10px;align-items:center;justify-content:space-between}
.warn button{border:0;background:none;text-decoration:underline;font-size:12px}
.demo{background:#edf4fc;border-color:#d4e3f6;color:#567898}
@media(max-width:860px){.grid{grid-template-columns:minmax(0,1fr) 150px minmax(0,1fr);gap:9px}main{padding:14px 14px 20px}.hero{flex-direction:column;align-items:stretch}.rm .body{transform:scale(.9)}}
@media(max-width:620px){.bar{padding:11px 14px}.grid{grid-template-columns:1fr 1fr;gap:8px}.rm{grid-column:1/-1;flex-direction:row;justify-content:center;gap:16px;padding-bottom:12px;border-bottom:1px solid var(--line);margin-bottom:4px}.rm .body{transform:scale(.34);margin:-128px -40px}.cap{text-align:left}.k button{min-height:70px;padding:9px}.k .ed{display:none}.foot{flex-direction:column;align-items:stretch}.foot .bs .btn{flex:1}.hero{padding:15px 16px}}
</style></head><body>
<div class="bar"><b>MiRemoteBridge</b><span class="mono mut">RC003 CONTROL</span><span class="grow"></span>
<span class="pill" id="pR"><i></i><span>遥控器…</span></span><span class="pill" id="pH"><i></i><span>主机…</span></span><span class="pill" id="pB"><i></i><span>电量…</span></span></div>
<main>
<div class="warn demo" id="demo" hidden><span>设计预览 · 数据为模拟，不会操作真实设备。</span></div>
<div class="warn" id="off" hidden><span>无法连接桥接器，显示的是上次读取的数据，编辑已暂停。</span><button id="retry">重新连接</button></div>
<div class="card hero"><div class="t"><h1 id="h1">正在读取设备状态</h1><p id="h2">与桥接器连接同一路由器，即可配置。</p><p id="diag" aria-live="polite"></p></div>
<button class="btn p" id="goMap">编辑按键映射</button><button class="btn" id="refresh">刷新</button></div>
<div class="head"><h2>按键映射</h2><span class="mut" style="font-size:11px"><span id="cnt">—</span> 项自定义 · 点卡片或遥控器按键编辑</span></div>
<div class="card" style="padding:14px 16px"><div class="grid" id="grid">
<svg id="wires" aria-hidden="true"></svg>
<div class="col" id="colL"></div>
<div class="rm"><div class="body" id="rmArt"></div><div class="cap"><b>RC003</b>13 键 · 标准蓝牙 HID<br>蓝色为正在编辑的按键</div></div>
<div class="col" id="colR"></div>
</div></div>
<div class="card foot"><div><h3>按下即转发，松开即释放</h3><p>保存后立即生效并写入 NVS，重启后保留。不改变遥控器原有转发时序。</p></div>
<div class="bs"><button class="btn" id="refresh2">刷新映射</button><button class="btn d" id="resetAll" disabled>恢复默认映射</button></div></div>
<p class="note">本固件不区分单击、双击与长按，不提供音频或宏。恢复默认仅清除自定义绑定，保留串口 <b>map</b> 基础模式与蓝牙配对。</p>
</main>
<dialog id="ed"><form id="edF">
<div class="dh"><div><h2 id="edT">编辑按键</h2><span class="mono mut" id="edC"></span></div><button type="button" class="btn" id="edX" aria-label="关闭">✕</button></div>
<div class="db">
<label class="f">动作类型<select id="kind"><option value="1">键盘快捷键</option><option value="2">媒体 / 系统控制</option><option value="0">恢复基础映射</option></select></label>
<div id="kb"><p class="mut" style="font-size:12px;margin-bottom:7px">修饰键 · 可多选，支持仅修饰键组合</p><div class="ms" id="mods"></div>
<label class="f">主键<select id="key"></select></label></div>
<div id="cs" hidden><label class="f">媒体 / 系统动作<select id="cons"></select></label></div>
<div class="pv"><small>保存后输出</small><span id="pv">—</span></div>
<p class="hint" id="edH"></p><p class="hint" id="edLink" aria-live="polite"></p><div class="err" id="edE" hidden></div>
</div>
<div class="da"><button type="button" class="rs" id="clr">恢复此键默认</button><button type="button" class="btn" id="edC2">取消</button><button type="submit" class="btn p" id="sv">保存映射</button></div>
</form></dialog>
<dialog id="rd"><div class="dh"><h2>恢复默认映射？</h2></div>
<div class="db"><h3>将清除全部自定义绑定，包括通过串口设置的绑定。</h3><p class="hint">保留返回、电源、语音键的串口基础模式；不删除蓝牙配对，不重启桥接器。此操作不可撤销。</p><div class="err" id="rdE" hidden></div></div>
<div class="da"><button class="btn" id="rdC">取消</button><button class="btn d" id="rdY">确认恢复</button></div></dialog>
<div id="toast" role="status" hidden></div>
<script>
'use strict';
var $=function(i){return document.getElementById(i)};
var KEYS=[[0x66,'电源键','⏻','L','p1'],[0x52,'上键','↑','L','du'],[0x50,'左键','←','L','dl'],[0xF1,'返回键','↩','L','p3'],[0x4A,'主页键','⌂','L','p4'],[0x65,'菜单键','☰','L','p5'],
[0x3E,'语音键','MIC','R','p2'],[0x4F,'右键','→','R','dr'],[0x28,'确定键','OK','R','do'],[0x51,'下键','↓','R','dd'],[0x80,'音量 +','＋','R','p6'],[0x81,'音量 −','－','R','p7'],[0x35,'TV 键','TV','R','p8']];
var KB=[[0,'无主键（仅修饰键）'],[40,'Enter'],[41,'Esc'],[42,'Backspace'],[43,'Tab'],[44,'Space'],[79,'→'],[80,'←'],[81,'↓'],[82,'↑'],[54,','],[55,'.'],[56,'/'],[45,'-'],[46,'='],[47,'['],[48,']'],[51,';'],[52,"'"],[53,'`'],[57,'Caps Lock'],[73,'Insert'],[74,'Home'],[75,'Page Up'],[76,'Delete'],[77,'End'],[78,'Page Down']];
for(var i=0;i<26;i++)KB.push([4+i,String.fromCharCode(65+i)]);
for(i=0;i<10;i++)KB.push([30+i,String((i+1)%10)]);
for(i=0;i<12;i++)KB.push([58+i,'F'+(i+1)]);
var CS=[[233,'音量 +'],[234,'音量 −'],[226,'静音'],[205,'播放 / 暂停'],[181,'下一曲'],[182,'上一曲'],[547,'媒体主页'],[548,'浏览器后退'],[48,'电源'],[50,'睡眠']];
var MD=[[1,'Ctrl'],[2,'Shift'],[4,'Alt'],[8,'Win'],[16,'右 Ctrl'],[32,'右 Shift'],[64,'右 Alt'],[128,'右 Win']];
var S={b:{},e:{},d:{},sel:0x28,cur:0,mods:0,on:false,ok:false,busy:false,poll:false,load:false,t:null,lastStatus:null};
function hx(v){return v.toString(16).toUpperCase().padStart(2,'0')}
function pick(l,v,d){for(var i=0;i<l.length;i++)if(l[i][0]===v)return l[i][1];return d}
function fmt(a){if(!a)return '等待读取';if(a.kind===0)return '不转发（基础模式已关闭）';if(a.kind===2)return pick(CS,a.cons,'媒体 0x'+hx(a.cons));
var p=[],i;for(i=0;i<MD.length;i++)if(a.mod&MD[i][0])p.push(MD[i][1]);
if(a.key)p.push(pick(KB,a.key,'HID 0x'+hx(a.key)));return p.join(' + ')||'未指定动作'}
function cur(r){return S.e[r]||S.b[r]||S.d[r]}
function esc(s){return String(s).replace(/[&<>"]/g,function(c){return {'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]})}
function toast(m){clearTimeout(S.t);$('toast').textContent=m;$('toast').hidden=false;S.t=setTimeout(function(){$('toast').hidden=true},3500)}
function rmMark(inter){return KEYS.map(function(k){return '<'+(inter?'button type="button"':'span')+' class="rb '+k[4]+'"'+(inter?' data-r="'+k[0]+'" aria-label="编辑'+k[1]+'"':'')+'>'+esc(k[2])+'</'+(inter?'button':'span')+'>'}).join('')}
$('rmArt').innerHTML=rmMark(true);
KEYS.forEach(function(k){var d=document.createElement('div');d.className='k';d.id='k'+k[0];
d.innerHTML='<button type="button" data-r="'+k[0]+'" aria-label="编辑'+k[1]+'" disabled><span class="r1">'+esc(k[2])+' <b>'+esc(k[1])+'</b><span class="tg">基础</span></span><span class="r2"><span class="act">等待读取</span><span class="ed">编辑 ›</span></span></button>';
$('col'+k[3]).appendChild(d)});
function opt(id,l){l.forEach(function(x){$(id).add(new Option(x[1],String(x[0])))});}
opt('key',KB);opt('cons',CS);
MD.forEach(function(m){var b=document.createElement('button');b.type='button';b.className='m';b.dataset.bit=m[0];b.textContent=m[1];b.setAttribute('aria-pressed','false');
b.onclick=function(){S.mods^=m[0];draft()};$('mods').appendChild(b)});
function setSel(id,v){if(!Array.prototype.some.call($(id).options,function(o){return +o.value===v}))$(id).add(new Option('HID 0x'+hx(v),String(v)));$(id).value=String(v)}
function btns(){var L=S.busy||S.load;Array.prototype.forEach.call(document.querySelectorAll('[data-r],#resetAll'),function(e){e.disabled=!S.ok||!S.on||L});
Array.prototype.forEach.call(document.querySelectorAll('#refresh,#refresh2'),function(e){e.disabled=L});Array.prototype.forEach.call(document.querySelectorAll('#kind,#key,#cons,.m'),function(e){e.disabled=S.busy});
$('sv').disabled=!S.on||L;$('rdY').disabled=!S.on||L;$('clr').disabled=!S.on||L;$('edC2').disabled=S.busy;$('edX').disabled=S.busy}
function online(v){S.on=v;$('off').hidden=v;btns()}
function req(u,m){var c=new AbortController(),t=setTimeout(function(){c.abort()},6500);
return fetch(u,{method:m||'GET',cache:'no-store',signal:c.signal}).then(function(r){return r.json().catch(function(){throw Error('设备返回了无效数据')}).then(function(j){if(!r.ok||j.error)throw Error(j.error||('请求失败 '+r.status));return j})}).finally(function(){clearTimeout(t)})}
function idx(l){var o={};(l||[]).forEach(function(a){o[a.raw]=a});return o}
function apply(d){if(!Array.isArray(d.bindings)||!Array.isArray(d.defaults)||!Array.isArray(d.effective))throw Error('按键数据不完整');
S.b=idx(d.bindings);S.d=idx(d.defaults);S.e=idx(d.effective);S.ok=true;render()}
function render(){var n=0;KEYS.forEach(function(k){var r=k[0],c=$('k'+r),b=S.b[r];if(b)n++;
c.className='k'+(b?' cus':'')+(r===S.sel?' sel':'');c.querySelector('.tg').textContent=b?'自定义':'基础';
c.querySelector('.act').textContent=fmt(cur(r))});
Array.prototype.forEach.call($('rmArt').children,function(e){e.classList.toggle('sel',+e.dataset.r===S.sel)});
$('cnt').textContent=n;btns();drawWires()}
// Curve from each physical button to its mapping slot, measured from the live
// layout so it survives resize and any font/label change.
function drawWires(){var g=$('grid'),svg=$('wires');if(!g||!svg)return;
var gb=g.getBoundingClientRect(),out='';
KEYS.forEach(function(k){var card=$('k'+k[0]),btn=document.querySelector('#rmArt [data-r="'+k[0]+'"]');
if(!card||!btn)return;
var cb=card.getBoundingClientRect(),bb=btn.getBoundingClientRect();
var x1=(k[3]==='L'?bb.left:bb.right)-gb.left,y1=bb.top+bb.height/2-gb.top;
var x2=(k[3]==='L'?cb.right:cb.left)-gb.left,y2=cb.top+cb.height/2-gb.top;
var dx=(x2-x1)*0.45;
out+='<path data-r="'+k[0]+'" d="M'+x1.toFixed(1)+','+y1.toFixed(1)+' C'+(x1+dx).toFixed(1)+','+y1.toFixed(1)+' '+(x2-dx).toFixed(1)+','+y2.toFixed(1)+' '+x2.toFixed(1)+','+y2.toFixed(1)+'"/>';});
svg.innerHTML=out}
function highlight(raw){KEYS.forEach(function(k){var el=$('k'+k[0]);if(el)el.classList.toggle('live',!!raw&&k[0]===raw)});
Array.prototype.forEach.call($('rmArt').children,function(e){e.classList.toggle('live',!!raw&&+e.dataset.r===raw)});
Array.prototype.forEach.call(document.querySelectorAll('#wires path'),function(p){p.classList.toggle('live',!!raw&&+p.dataset.r===raw)})}
function pollStatus(){if(S.poll)return;S.poll=true;
fetch('/api/status',{cache:'no-store'}).then(function(r){return r.json()}).then(function(j){
stat(j);highlight(j.activeKey);
if(!S.on)load()  /*the device answers again - recover from the greyed-out state without a manual refresh*/
})
.catch(function(){}).finally(function(){S.poll=false})}
/* A key-down that happened between two polls still flashes: the firmware counts
every forwarded press, so a changed counter means "this key was pressed".*/
function pulse(raw){if(!raw)return;Array.prototype.forEach.call($('rmArt').children,function(e){
if(+e.dataset.r===raw){e.classList.add('pulse');setTimeout(function(){e.classList.remove('pulse')},260)}})}
/* Event channel: one request parked on the device, answered the instant a key
is forwarded. Not a timer - nothing is polled, and the counter in 'since'
means a press can never be missed, however short. */
function startEvents(){if(S.evStopped)return;
S.evCtl=('AbortController'in window)?new AbortController():null;
var since=(typeof S.keyPress==='number')?S.keyPress:0;
fetch('/api/events?since='+since,{cache:'no-store',signal:S.evCtl?S.evCtl.signal:undefined})
.then(function(r){return r.json()}).then(function(j){
if(typeof j.keyPresses==='number'){S.keyPress=j.keyPresses;pulse(j.lastKey)}
if(typeof j.activeKey==='number')highlight(j.activeKey);
if(typeof j.remoteConnected==='boolean')stat(j);
startEvents()})
.catch(function(){if(!S.evStopped)setTimeout(startEvents,1200)})}
function stopEvents(){S.evStopped=true;if(S.evCtl){try{S.evCtl.abort()}catch(e){}S.evCtl=null}}
function resumeEvents(){if(!S.evStopped)return;S.evStopped=false;startEvents()}
function stat(j){var rc=!!j.remoteConnected,h=!!j.hostConnected;
var bat=(typeof j.battery==='number'&&j.battery>=0&&j.battery<=100)?j.battery+'%':'未知';
function pill(id,on,t){var e=$(id);e.className='pill '+(on?'ok':'off');e.lastChild.textContent=t}
pill('pR',rc,rc?'遥控器已连接':'遥控器未连接');pill('pH',h,h?'主机已连接':'主机未连接');pill('pB',bat!=='未知',bat==='未知'?'电量未知':'电量 '+bat);
$('h1').textContent=(rc&&h)?'RC003 已就绪':(rc?'遥控器已连接，等待主机':'等待遥控器连接');
$('h2').textContent=(rc&&h)?'蓝牙链路正常。点下方卡片或遥控器按键即可改绑。':(rc?'请在电脑或手机蓝牙设置里连接 Mi Remote Bridge。':'已有映射仍会保留，也可以现在先配置。')}
function load(manual){if(S.load||S.busy||$('ed').open||$('rd').open)return;S.load=true;btns();
return req('/api/bindings').then(apply).then(function(){return req('/api/status')}).then(function(j){stat(j);online(true);if(manual)toast('已刷新')})
.catch(function(e){online(false);if(manual)toast(e.message==='Failed to fetch'?'连接失败，请确认已连接热点':e.message)}).finally(function(){S.load=false;btns()})}
function openEd(r){if(!S.ok||!S.on||S.busy||S.load)return;S.sel=r;S.cur=r;var a=cur(r)||{kind:1,mod:0,key:40,cons:233};S.mods=a.mod|0;
$('kind').value=String(a.kind||0);setSel('key',a.key||0);setSel('cons',a.cons||233);
$('edT').textContent='编辑 '+pick(KEYS.map(function(k){return [k[0],k[1]]}),r,'按键');$('edC').textContent='RAW 0x'+hx(r);
$('edE').hidden=true;draft();render();$('ed').showModal()}
function draft(){var k=+$('kind').value;$('kb').hidden=k!==1;$('cs').hidden=k!==2;
Array.prototype.forEach.call(document.querySelectorAll('.m'),function(e){e.setAttribute('aria-pressed',String(!!(S.mods&+e.dataset.bit)))});
var a={raw:S.cur,kind:k,mod:k===1?S.mods:0,key:k===1?+$('key').value:0,cons:k===2?+$('cons').value:0};
$('pv').textContent=k===0?'移除自定义，使用串口基础模式':fmt(a);
$('edH').textContent=k===0?'仅移除此键的自定义绑定。返回、电源、语音键会使用现有串口模式，而非恢复出厂。':'按下与松开实时转发。Win 在 Apple 主机上对应 Command；语音键仅作普通按键，不传输麦克风音频。'}
function err(e){return e.name==='AbortError'?'请求超时，结果未确认，请刷新核对':e.message==='Failed to fetch'?'连接中断，结果未确认，请刷新核对':e.message}
function same(a,b){return !!a&&a.kind===b.kind&&a.mod===b.mod&&a.key===b.key&&a.cons===b.cons}
function save(){if(S.busy||!S.on)return Promise.resolve();var k=+$('kind').value;
var a={raw:hx(S.cur),kind:k,mod:k===1?S.mods:0,key:k===1?+$('key').value:0,cons:k===2?+$('cons').value:0};
if(k===1&&!a.mod&&!a.key){$('edE').textContent='请至少选择一个修饰键或主键。';$('edE').hidden=false;return Promise.resolve()}
S.busy=true;btns();$('edE').hidden=true;stopEvents();
var q=Object.keys(a).map(function(x){return x+'='+a[x]}).join('&');
return req('/api/set?'+q,'POST').then(function(j){if(j.ok!==true)throw Error('设备未确认保存');return req('/api/bindings')})
.then(function(d){apply(d);var got=S.b[k===0?parseInt(a.raw,16):S.cur];
if(k===0?!!got:!same(got,{kind:k,mod:a.mod,key:a.key,cons:a.cons}))throw Error('回读结果与提交不一致，保存未确认');
$('ed').close();toast(k===0?'已恢复此键基础映射':'映射已保存并回读确认')})
.catch(function(e){$('edE').textContent=err(e);$('edE').hidden=false}).finally(function(){S.busy=false;btns();resumeEvents()})}
function reset(){if(S.busy||!S.on)return Promise.resolve();S.busy=true;btns();$('rdE').hidden=true;stopEvents();
return req('/api/reset','POST').then(function(j){if(j.ok!==true)throw Error('设备未确认恢复');return req('/api/bindings')})
.then(function(d){apply(d);if(d.bindings.length)throw Error('仍存在自定义绑定，恢复未确认');$('rd').close();toast('已清除全部自定义绑定')})
.catch(function(e){$('rdE').textContent=err(e);$('rdE').hidden=false}).finally(function(){S.busy=false;btns();resumeEvents()})}
document.addEventListener('click',function(e){var t=e.target.closest?e.target.closest('[data-r]'):null;if(t)openEd(+t.dataset.r)});
$('edF').onsubmit=function(e){e.preventDefault();save()};
$('kind').onchange=draft;$('key').onchange=draft;$('cons').onchange=draft;
$('clr').onclick=function(){$('kind').value='0';draft()};
$('edX').onclick=$('edC2').onclick=function(){if(!S.busy)$('ed').close()};
$('ed').addEventListener('cancel',function(e){if(S.busy)e.preventDefault()});
$('rd').addEventListener('cancel',function(e){if(S.busy)e.preventDefault()});
$('resetAll').onclick=function(){$('rdE').hidden=true;$('rd').showModal()};
$('rdC').onclick=function(){$('rd').close()};$('rdY').onclick=reset;
$('refresh').onclick=$('refresh2').onclick=$('retry').onclick=function(){load(true)};
$('goMap').onclick=function(){document.querySelector('.head').scrollIntoView({behavior:'smooth',block:'start'})};
document.addEventListener('visibilitychange',function(){if(!document.hidden)load()});
window.addEventListener('resize',drawWires);
load();startEvents();
setInterval(function(){if(!document.hidden)pollStatus()},3000);
setInterval(function(){if(!document.hidden&&!$('ed').open&&!$('rd').open)load()},30000);
</script></body></html>)rawliteral";
