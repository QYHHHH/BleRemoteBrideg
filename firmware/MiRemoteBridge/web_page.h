/*
 * MiRemoteBridge - Web UI page (served from flash, gzipped by gen_web_page.py)
 *
 * Layout and styling: the refined design, with SVG key icons and arrowed
 * connector lines from the remote drawing to each key card. Transport: one
 * websocket - the board pushes a key event the moment it forwards the key,
 * and the page sends its binding commands back over the same socket.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <pgmspace.h>

static const char kIndexHtml[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html lang="zh">
<head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<meta name="color-scheme" content="light"><title>MiRemoteBridge 按键映射</title>
<style>
:root{--bg:#f6f7f9;--card:#fff;--line:#e6e8ec;--ink:#252931;--mut:#78818e;--blue:#1674ed;--soft:#edf5ff;--ok:#328564;--warn:#aa6d22}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--ink);font:14px/1.6 -apple-system,BlinkMacSystemFont,"Segoe UI","Microsoft YaHei",sans-serif}
button{font:inherit;color:inherit;cursor:pointer}button:disabled{cursor:not-allowed;opacity:.45}a{color:var(--blue)}
[hidden]{display:none!important}h1,h2,h3,p{margin:0}h1{font-size:22px}h2{font-size:15px}h3{font-size:13px;font-weight:600}
.mut{color:var(--mut)}.mono{font:10px ui-monospace,Consolas,monospace;letter-spacing:1px}
.pill{display:inline-flex;align-items:center;gap:6px;padding:4px 10px;border:1px solid var(--line);border-radius:99px;background:#fff;font-size:11px;color:var(--mut)}button.pill{cursor:pointer}
.pill.ok{color:var(--ok);border-color:#cfe9db}.pill.off{color:var(--warn);border-color:#f0dfc0}
.pill i{width:6px;height:6px;border-radius:50%;background:currentColor}
.bar{display:flex;align-items:center;gap:12px;flex-wrap:wrap;padding:14px 18px;border-bottom:1px solid var(--line);background:#fff}
.bar b{font-size:14px}.bar .grow{flex:1}
main{max-width:1680px;margin:0 auto;padding:12px 18px}
.card{background:var(--card);border:1px solid var(--line);border-radius:12px;box-shadow:0 4px 18px #26324904}
.hero{display:flex;align-items:center;gap:18px;padding:10px 16px;margin-bottom:10px}
.hero .t{flex:1;min-width:0}.hero h1{margin-bottom:4px}.hero p{font-size:12px;color:var(--mut)}.hero #diag:empty{display:none}.hero #diag.wake{margin:7px 0 0;padding:5px 8px;color:#8b6b37;background:#fff9ef;border:1px solid #f0ddbb;border-radius:6px}
.btn{display:inline-flex;align-items:center;gap:6px;min-height:36px;padding:7px 14px;border:1px solid #dfe3e9;border-radius:7px;background:#fff;font-size:12px;white-space:nowrap}
.btn:hover{background:#f8fafc}.btn.p{background:var(--blue);border-color:var(--blue);color:#fff}.btn.p:hover{background:#0966d9}
.btn.d{color:#b45345}
.head{display:flex;align-items:center;justify-content:space-between;gap:12px;margin:0 0 10px}
.grid{display:grid;grid-template-columns:minmax(0,1fr) 190px minmax(0,1fr);gap:12px;align-items:center;position:relative}
.col{display:flex;flex-direction:column;gap:6px;position:relative;z-index:1}
#wires{position:absolute;inset:0;width:100%;height:100%;pointer-events:none;z-index:2;overflow:visible}
#wires path{fill:none;stroke:#aeb4bf;stroke-opacity:.75;stroke-width:1.2;transition:stroke .1s}
#wires path.live{stroke:#327ede;stroke-width:2.5}
.k{border:1px solid #e7e9ed;background:#f9fafb;border-radius:9px;overflow:hidden;transition:.15s}
.k.sel{background:var(--soft);border-color:#9bc5fb}
.k.live,.rb.live{background:#d7ecff;border-color:#327ede;box-shadow:0 0 0 3px #327ede26}
.k.live .act,.rb.live{color:#1257b5}
.rb.live{color:#fff;background:#327ede}
.k:hover{border-color:#b2c9e6}
.k button{display:block;width:100%;text-align:left;background:none;border:0;padding:5px 9px;min-height:46px;line-height:1.25}
.k .r1{display:flex;align-items:center;gap:6px;font-size:12px;color:#67717d}
.k .r1 b{color:var(--ink);font-weight:600}
.k .tg{margin-left:auto;font-size:9px;color:#8d96a1}
.k.cus .tg{color:var(--blue)}
.k .r2{display:flex;justify-content:space-between;gap:8px;align-items:center;padding-top:1px}
.k .act{font-size:12px;font-weight:600;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.k .ed{font-size:10px;color:#9aa3af}
.rm{display:flex;flex-direction:column;align-items:center;gap:12px;position:relative;z-index:1}
.rm .body{width:132px;height:360px;border:1px solid #969898;border-radius:12px / 5px;background:linear-gradient(90deg,#575b5c,#d4d5d3 4%,#a8aaa9 8%,#cfd0ce 14%,#bdbfbd 70%,#f0f1ef 94%,#8c908f 98%,#b4b8b8);position:relative;box-shadow:inset 0 1px 2px #fff9;flex:none}
.rm .body:before{content:"";position:absolute;left:10px;top:58px;width:110px;height:110px;border:1px solid #111;border-radius:50%;background:linear-gradient(135deg,#353636,#242525);box-shadow:inset 0 0 0 2px #ffffff18}
.rb{position:absolute;display:grid;place-items:center;width:43px;height:43px;padding:0;border:1px solid #101010;border-radius:50%;background:linear-gradient(135deg,#414242,#202121);box-shadow:inset 0 0 0 1px #ffffff25;color:#eee;font-size:13px}
.rb:hover{filter:brightness(1.2)}.rb.sel{box-shadow:inset 0 0 0 2px #327ede;color:#9dc7ff}
.rb.p1,.rb.p2{top:16px;width:30px;height:30px;background:linear-gradient(135deg,#d0d2d1,#b8bbba);color:#222;border-color:#4d5051}
.p1{left:15px}.p2{left:85px}
.p3{left:15px;top:174px}.p4{left:15px;top:224px}.p5{left:15px;top:274px}
.p6{left:71px;top:174px;height:47px;border-radius:24px 24px 0 0;border-bottom:0;box-shadow:inset 1px 1px #ffffff25}
.p7{left:71px;top:221px;height:47px;border-radius:0 0 24px 24px;border-top:0;box-shadow:inset 1px -1px #ffffff25}
.p8{left:71px;top:274px}
.rb.du,.rb.dd,.rb.dl,.rb.dr{width:32px;height:32px;border:0;background:none;box-shadow:none;color:transparent}
.rb.du{left:49px;top:59px}.rb.dd{left:49px;top:135px}.rb.dl{left:11px;top:97px}.rb.dr{left:87px;top:97px}
.rb.du:hover,.rb.dd:hover,.rb.dl:hover,.rb.dr:hover{background:#ffffff16}
.rb.do{left:38px;top:85px;width:56px;height:56px;background:linear-gradient(135deg,#343535,#292a2b);color:transparent}
.rb.do.sel{color:transparent}.ico{width:18px;height:18px;fill:none;stroke:currentColor;stroke-width:1.6;stroke-linecap:round;stroke-linejoin:round;flex:none}
.k .ico{color:#625be3}.rb .ico{width:19px;height:19px}
.cap{text-align:center;font-size:9px;color:#97a0aa;line-height:1.8}.cap b{display:block;color:#6d7781;font-size:10px;letter-spacing:2px;font-weight:500}
.foot{display:flex;align-items:center;justify-content:space-between;gap:12px;padding:9px 14px;margin-top:10px;flex-wrap:wrap}
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
@media(max-width:860px){.grid{grid-template-columns:minmax(0,1fr) 240px minmax(0,1fr);gap:9px}main{padding:14px 14px 20px}.hero{flex-direction:column;align-items:stretch}.rm .body{transform:scale(.9)}}
@media(max-width:620px){.bar{padding:11px 14px}.grid{grid-template-columns:1fr 1fr;gap:8px}.rm{grid-column:1/-1;flex-direction:row;justify-content:center;gap:16px;padding-bottom:12px;border-bottom:1px solid var(--line);margin-bottom:4px}.rm .body{transform:scale(.34);margin:-128px -40px}.cap{text-align:left}.k button{min-height:70px;padding:9px}.k .ed{display:none}.foot{flex-direction:column;align-items:stretch}.foot .bs .btn{flex:1}.hero{padding:15px 16px}}
/* Active states, for every hotspot whatever position class it carries. The
   position rules (du/dd/dl/dr, do, p1/p2, p6/p7) set background, border,
   box-shadow or color themselves and used to sit after .rb.live/.rb.sel at the
   same specificity, so source order beat the states: the D-pad never lit up and
   .pulse had no rule at all. These carry a third class, so they always win. A
   hidden pad glyph is shown again, or the disc lights up empty. */
.rm .body .rb.sel{background:#d7ecff;border-color:#327ede;box-shadow:inset 0 0 0 2px #327ede;color:#1257b5}
.rm .body .rb.live,.rm .body .rb.pulse{background:#327ede;border-color:#327ede;box-shadow:0 0 0 3px #327ede40;color:#fff}
.workspace{display:grid;grid-template-columns:minmax(0,1fr) 310px;gap:14px;align-items:start}.board{padding:14px}.board svg{display:block;width:100%;height:130px;margin:6px 0}.board p{font-size:12px;margin-top:6px}.memory{margin-top:14px;border-top:1px solid var(--line);padding-top:12px}.memory strong{display:block;font:600 20px Consolas,monospace}.memory small{color:var(--mut)}
@media(max-width:1100px){.workspace{grid-template-columns:minmax(0,1fr) 260px}.grid{grid-template-columns:minmax(0,1fr) 150px minmax(0,1fr);gap:8px}.k .ed{display:none}}
@media(max-width:900px){.workspace{grid-template-columns:1fr}.board{display:grid;grid-template-columns:150px 1fr;gap:0 16px}.board svg{grid-row:1/7;height:250px}.board h2{grid-column:2}.memory{grid-column:1/-1}.hero{flex-direction:row}.grid{grid-template-columns:minmax(0,1fr) 170px minmax(0,1fr)}}
@media(max-width:620px){.grid{grid-template-columns:1fr 1fr}.rm{grid-row:1}.rm .body{margin:-110px -40px}.hero{flex-wrap:wrap}.hero .t{flex-basis:100%}.board{display:block}.board svg{height:210px}}
main{max-width:none;padding:14px 20px}.workspace{display:grid;grid-template-columns:minmax(0,1fr) minmax(0,1fr);gap:16px;align-items:stretch}.mapping-panel{padding:16px;display:flex;flex-direction:column;min-height:min(790px,calc(100vh - 118px))}.mapping-panel .head{margin-bottom:14px}.grid{grid-template-columns:minmax(0,1fr) 230px minmax(0,1fr);gap:12px;align-items:center;flex:1}.col{align-self:stretch;justify-content:space-around;gap:12px;padding:12px 0}.k button{min-height:60px;padding:8px 10px;line-height:1.4}.k .r1,.k .act{font-size:13px}.k .ed{display:inline}.rm{padding:0;margin:0;border:0;flex-direction:column;gap:12px;align-self:center}.rm .body{width:132px;height:540px;transform:scale(.86);margin:-37.8px -9.24px}.cap{font-size:11px;text-align:center}.side{display:flex;flex-direction:column;gap:14px;height:100%}
.rack{padding:14px}.rack-heading{display:flex;justify-content:space-between;align-items:center;margin-bottom:12px}.rack-heading span{font-size:12px}.slots{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:9px}.slot{display:flex;flex-direction:column;align-items:start;gap:8px;text-align:left;padding:13px 11px;border:1px solid var(--line);border-radius:9px;background:#fafbfc;min-width:0}.slot.selected{background:#edf5ff;border-color:#82b7f6;box-shadow:inset 0 3px #1674ed}.slot strong{font-size:18px}.slot-index,.slot-battery,.slot-link{font-size:12px;color:var(--mut)}.slot-link.linked{color:var(--ok)}.slot-wake{font-size:11px;color:#906b34}.rack-actions{display:flex;align-items:center;gap:8px;border-top:1px solid var(--line);padding-top:12px;margin-top:14px}.rack-actions>div{flex:1;font-size:12px}.rack-actions small{display:block;color:var(--mut);margin-top:3px}.rack-actions .btn{padding:7px 10px}
.side .board{display:block;flex:1 0 auto;position:relative;align-content:start;padding:16px}.board>h2{margin-bottom:14px}.board>#pH,.board>#pW{float:right;margin:0 0 0 6px}.board-body{display:grid;grid-template-columns:minmax(0,1.8fr) minmax(140px,.8fr);gap:16px;align-items:stretch}.hardware{display:flex;gap:14px;align-items:center}.board .hardware svg{width:220px;height:300px;flex:none;margin:0}.legend{flex:1;min-width:0}.board .legend p{display:block;margin:0;padding:11px 0;border-bottom:1px solid #edf0f3;font-size:13px;line-height:1.7}.board .legend p:last-child{border-bottom:0}.board .memory{border-top:0;border-left:1px solid var(--line);padding:10px 0 10px 16px;margin:0;display:flex;flex-direction:column;justify-content:center;align-items:center;text-align:center;grid-column:auto}.memory h3{align-self:stretch;text-align:left;margin-bottom:8px}.memory strong{font-size:18px}.board .memory p{font-size:10px;margin:7px 0;line-height:1.7}.memory small{font-size:11px;line-height:1.6}.board .memory #memDetail{white-space:pre-line}
.empty-remote{min-height:360px;display:flex;flex-direction:column;align-items:center;justify-content:center;gap:14px;text-align:center;color:var(--mut)}.empty-remote h2{color:var(--ink)}.empty-remote p{font-size:12px}.empty-remote small{font-size:10px}.empty-icon{font-size:32px;color:#86a8cf;border:1px dashed #c2d5e9;border-radius:14px;padding:6px 24px}.learn-banner{border-bottom:1px solid var(--line);padding:8px 0 16px}.learn-banner b,.learn-banner span{display:block}.learn-banner span{font-size:13px;color:var(--mut);margin-top:4px}.simulator{display:flex;align-items:center;gap:8px;margin:15px 0;flex-wrap:wrap}.simulator>span{font-size:12px;color:var(--mut)}.learned-keys{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:10px}.learned-key button{min-height:60px;padding:8px 10px}.learned-key.heard{background:#edf5ff;border-color:#1674ed}#slotName{display:block;width:100%;margin-top:6px;padding:9px;min-height:40px;border:1px solid #dce1e8;border-radius:7px;background:#fff;color:var(--ink);font:inherit}
#wires path{stroke:#a6afbc;stroke-opacity:.85;stroke-width:1}#memRing{--used:0%;width:94px;height:94px;flex:none;border-radius:50%;background:conic-gradient(#327ede var(--used),#e9eef2 0);padding:8px;margin:2px 0 10px;display:grid;place-items:center;transform:rotate(-90deg)}#memRing>div{width:100%;height:100%;border-radius:50%;background:white;display:flex;flex-direction:column;align-items:center;justify-content:center;transform:rotate(90deg)}#memRing b{font:600 19px Consolas,monospace;color:var(--ink)}#memRing span{font-size:10px;color:var(--mut);margin-top:1px}#memTotal{margin-top:3px}.repo-placeholder{font-size:12px;color:var(--blue);padding:4px 9px;border:1px dashed #b9d1ee;border-radius:6px;text-decoration:none;cursor:default}.mapping-panel .head span,.memory #memDetail,.memory #memState{font-size:11px!important}
@media(max-width:1250px) and (min-width:901px){main{padding:12px}.workspace{gap:12px}.mapping-panel{padding:12px}.grid{grid-template-columns:minmax(0,1fr) 184px minmax(0,1fr);gap:8px}.k .r1,.k .act{font-size:12px}.board-body{grid-template-columns:minmax(0,1fr) 145px;gap:10px}.board .hardware svg{width:170px;height:260px}.rack-actions{flex-wrap:wrap}.rack-actions>div{flex-basis:100%}}
@media(max-width:900px){.workspace{grid-template-columns:1fr}.mapping-panel{min-height:560px}.side{height:auto}.board-body{grid-template-columns:minmax(0,1.65fr) minmax(155px,1fr)}.board .hardware svg{width:180px}.grid{grid-template-columns:minmax(0,1fr) 140px minmax(0,1fr)}}
@media(max-width:560px){main{padding:10px}.grid{grid-template-columns:minmax(0,1fr) 94px minmax(0,1fr);gap:5px}.rm .body{transform:scale(.64);margin:-97.2px -23.76px}.slots{gap:5px}.slot-link{font-size:10px}.slot strong{font-size:14px}.slot-index,.slot-battery{font-size:9px}.board-body{grid-template-columns:1fr}.board .memory{border-left:0;border-top:1px solid var(--line);padding:12px 0}.k .r1,.k .act{font-size:11px}.k .ed,.k .tg{display:none}}
.mobile-map-fix{display:none}@media(max-width:560px){.mapping-panel{padding:12px}.grid{grid-template-columns:minmax(0,1fr) 94px minmax(0,1fr);gap:5px;align-items:center}.grid #colL{grid-column:1;grid-row:1}.grid .rm{grid-column:2;grid-row:1;flex-direction:column;justify-content:center;gap:6px;padding:0;border:0;margin:0}.grid #colR{grid-column:3;grid-row:1}.grid .col{grid-row:1;gap:8px;padding:0;align-self:stretch;justify-content:space-around}.grid .rm .body{transform:scale(.64);margin:-97.2px -23.76px}.grid .cap{text-align:center;font-size:8px}.grid .k button{min-height:58px;padding:6px}.grid .k .r1,.grid .k .act{font-size:10px}.grid .k .ico{width:13px;height:13px}}
</style></head><body>
<div class="bar"><b>MiRemoteBridge</b><span class="mono mut">RC003 CONTROL</span> <span class="mono mut" id="ver"></span><span class="grow"></span><a class="repo-placeholder" aria-disabled="true">代码仓库 · 地址待填写</a>
<span class="pill" id="pR"><i></i><span>遥控器…</span></span><span class="pill" id="pH"><i></i><span>被控蓝牙…</span></span><button class="pill off" id="pW" type="button" title="点击重新连接并夺回控制权"><i></i><span>网页控制连接中…</span></button><span class="pill" id="pB"><i></i><span>电量…</span></span></div>
<main>
<div class="warn" id="off" hidden><span>无法连接桥接器<span id="offWhy">（连接断开，自动重连中）</span></span><button id="retry">重新连接</button></div>
<div class="card hero"><div class="t"><h1 id="h1">正在读取设备状态</h1><p id="h2">与桥接器连接同一路由器，即可配置。</p><p id="diag" aria-live="polite"></p></div>
<button class="btn p" id="goMap">编辑按键映射</button><button class="btn" id="refresh">刷新</button></div>
<div class="workspace"><section><div class="head"><h2>按键映射</h2><span class="mut" style="font-size:11px"><span id="cnt">—</span> 项自定义 · 点卡片或遥控器按键编辑</span></div>
<div class="card" style="padding:14px 16px"><div class="grid" id="grid">
<svg id="wires" aria-hidden="true"></svg>
<div class="col" id="colL"></div>
<div class="rm"><div class="body" id="rmArt"></div><div class="cap"><b>RC003</b>13 键 · 标准蓝牙 HID<br>蓝色为正在编辑的按键</div></div>
<div class="col" id="colR"></div>
</div></div>
<div class="card foot"><div><h3>按下即转发，松开即释放</h3><p>保存后立即生效并写入 NVS，重启后保留。不改变遥控器原有转发时序。</p></div>
<div class="bs"><button class="btn" id="refresh2">刷新映射</button><button class="btn d" id="resetAll" disabled>恢复默认映射</button></div></div>
<p class="note">本固件不区分单击、双击与长按，不提供音频或宏。恢复默认仅清除自定义绑定，保留串口 <b>map</b> 基础模式与蓝牙配对。</p>
</section><aside class="card board"><h2>开发板 · CORE-ESP32</h2>
<svg viewBox="0 0 270 240" role="img" aria-label="开发板正面示意：左侧 RST 和 D5，右侧 BOOT 和 D4，底部 USB">
<rect x="83" y="5" width="104" height="230" rx="5" fill="#087aab"/>
<path d="M93 39V16h13v22h15V16h14v22h15V16h24v29" fill="none" stroke="#45b0d5" stroke-width="5"/>
<path d="M85 57v160m100-160v160" stroke="#e1c994" stroke-width="7" stroke-dasharray="5 7"/>
<g fill="#303b43" stroke="#9aadb5"><rect x="113" y="65" width="42" height="40"/><rect x="111" y="116" width="48" height="24"/><rect x="127" y="159" width="18" height="19"/></g>
<g fill="#e8e8dc" stroke="#a9afb1"><rect x="92" y="150" width="25" height="26"/><rect x="153" y="150" width="25" height="26"/><rect x="115" y="211" width="40" height="24" rx="3"/></g>
<g fill="#fff4a9"><rect x="94" y="197" width="8" height="9"/><rect x="168" y="197" width="8" height="9"/></g>
<g fill="none" stroke="#78818e"><path d="M92 163H50m128 0h42M94 201H50m126 0h44"/></g>
<g fill="#252931" font-size="12" font-family="sans-serif"><text x="17" y="167">RST</text><text x="222" y="167">BOOT</text><text x="20" y="205">D5</text><text x="222" y="205">D4</text></g><text x="135" y="226" text-anchor="middle" font-size="9" fill="#404b50">USB</text>
</svg>
<p><b>D5 · 主机 / GPIO13</b><br>呼吸：未连接；快闪：HID 未就绪；常亮：键盘 HID 已就绪。</p>
<p><b>D4 · 遥控器 / GPIO12</b><br>呼吸：等待或搜索；快闪：连接中；常亮：就绪。按住遥控器按键时熄灭。</p>
<p><b>RST</b>：硬件复位重启。</p>
<p><b>BOOT / GPIO9</b>：运行时短按无操作；<b>长按 5 秒清除全部设置与蓝牙配对并重启</b>。</p>
<div class="memory"><h3>实时堆内存</h3><strong id="memFree">—</strong><p id="memDetail">等待设备数据</p><small id="memState">随设备状态更新</small></div>
</aside></div></main>
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
function icon(k){var p={p1:'M12 3v9 M6 5a9 9 0 1 0 12 0',p2:'M9 5a3 3 0 0 1 6 0v7a3 3 0 0 1-6 0z M5 10v2a7 7 0 0 0 14 0v-2 M12 19v3',du:'m5 15 7-7 7 7',dd:'m5 9 7 7 7-7',dl:'m15 5-7 7 7 7',dr:'m9 5 7 7-7 7',do:'M20 12a8 8 0 1 0-16 0 8 8 0 0 0 16 0 M14 12a2 2 0 1 0-4 0 2 2 0 0 0 4 0',p3:'m14 5-7 7 7 7',p4:'m4 10 8-7 8 7v11H4z M9 21v-8h6v8',p5:'M4 6h16 M4 12h16 M4 18h16',p6:'M5 12h14 M12 5v14',p7:'M5 12h14',p8:'M5 6h14a2 2 0 0 1 2 2v11H3V8a2 2 0 0 1 2-2 M8 2l4 4 4-4'};return '<svg class="ico" viewBox="0 0 24 24" aria-hidden="true"><path d="'+p[k[4]]+'"/></svg>'}
var KB=[[0,'无主键（仅修饰键）'],[40,'Enter'],[41,'Esc'],[42,'Backspace'],[43,'Tab'],[44,'Space'],[79,'→'],[80,'←'],[81,'↓'],[82,'↑'],[54,','],[55,'.'],[56,'/'],[45,'-'],[46,'='],[47,'['],[48,']'],[51,';'],[52,"'"],[53,'`'],[57,'Caps Lock'],[73,'Insert'],[74,'Home'],[75,'Page Up'],[76,'Delete'],[77,'End'],[78,'Page Down']];
for(var i=0;i<26;i++)KB.push([4+i,String.fromCharCode(65+i)]);
for(i=0;i<10;i++)KB.push([30+i,String((i+1)%10)]);
for(i=0;i<12;i++)KB.push([58+i,'F'+(i+1)]);
var CS=[[233,'音量 +'],[234,'音量 −'],[226,'静音'],[205,'播放 / 暂停'],[181,'下一曲'],[182,'上一曲'],[547,'媒体主页'],[548,'浏览器后退'],[48,'电源'],[50,'睡眠']];
var MD=[[1,'Ctrl'],[2,'Shift'],[4,'Alt'],[8,'Win'],[16,'右 Ctrl'],[32,'右 Shift'],[64,'右 Alt'],[128,'右 Win']];
var S={b:{},e:{},d:{},sel:0x28,cur:0,mods:0,on:false,ok:false,busy:false,poll:false,load:false,t:null,lastStatus:null,ws:null,pending:[],keyPress:undefined,wsRetry:0,wsTimer:null,wsOpened:false,claimed:false,switching:false};
function hx(v){return v.toString(16).toUpperCase().padStart(2,'0')}
function pick(l,v,d){for(var i=0;i<l.length;i++)if(l[i][0]===v)return l[i][1];return d}
function fmt(a){if(!a)return '等待读取';if(a.kind===0)return '不转发（基础模式已关闭）';if(a.kind===2)return pick(CS,a.cons,'媒体 0x'+hx(a.cons));
var p=[],i;for(i=0;i<MD.length;i++)if(a.mod&MD[i][0])p.push(MD[i][1]);
if(a.key)p.push(pick(KB,a.key,'HID 0x'+hx(a.key)));return p.join(' + ')||'未指定动作'}
function cur(r){return S.e[r]||S.b[r]||S.d[r]}
function esc(s){return String(s).replace(/[&<>"]/g,function(c){return {'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]})}
function toast(m){clearTimeout(S.t);$('toast').textContent=m;$('toast').hidden=false;S.t=setTimeout(function(){$('toast').hidden=true},3500)}
function rmMark(inter){return KEYS.map(function(k){return '<'+(inter?'button type="button"':'span')+' class="rb '+k[4]+'"'+(inter?' data-r="'+k[0]+'" aria-label="编辑'+k[1]+'"':'')+'>'+icon(k)+'</'+(inter?'button':'span')+'>'}).join('')}
$('rmArt').innerHTML=rmMark(true);
KEYS.forEach(function(k){var d=document.createElement('div');d.className='k';d.id='k'+k[0];
d.innerHTML='<button type="button" data-r="'+k[0]+'" aria-label="编辑'+k[1]+'" disabled><span class="r1">'+icon(k)+' <b>'+esc(k[1])+'</b><span class="tg">基础</span></span><span class="r2"><span class="act">等待读取</span><span class="ed">编辑 ›</span></span></button>';
$('col'+k[3]).appendChild(d)});
function opt(id,l){l.forEach(function(x){$(id).add(new Option(x[1],String(x[0])))});}
opt('key',KB);opt('cons',CS);
MD.forEach(function(m){var b=document.createElement('button');b.type='button';b.className='m';b.dataset.bit=m[0];b.textContent=m[1];b.setAttribute('aria-pressed','false');
b.onclick=function(){S.mods^=m[0];draft()};$('mods').appendChild(b)});
function setSel(id,v){if(!Array.prototype.some.call($(id).options,function(o){return +o.value===v}))$(id).add(new Option('HID 0x'+hx(v),String(v)));$(id).value=String(v)}
function btns(){var L=S.busy||S.load||S.slotBusy;Array.prototype.forEach.call(document.querySelectorAll('[data-r],[data-learned],#resetAll,#uiReset,#pairRemote,#deleteRemote,[data-slot],#simulateBind,#simulateKey,#simulateRepeat,#slotEditor input,#slotEditor select,#slotEditor .m,#slotDelete'),function(e){e.disabled=!S.ok||!S.on||L});
Array.prototype.forEach.call(document.querySelectorAll('#refresh,#refresh2'),function(e){e.disabled=L});Array.prototype.forEach.call(document.querySelectorAll('#kind,#key,#cons,.m'),function(e){e.disabled=!S.on||L});
$('sv').disabled=!S.on||L;$('rdY').disabled=!S.on||L;$('clr').disabled=!S.on||L;$('edC2').disabled=S.busy;$('edX').disabled=S.busy}
function online(v,why){S.on=v;if(!v)$('memState').textContent='连接已断开 · 数值为上次采样';$('off').hidden=v;var p=$('pW');if(p){p.className='pill '+(v?'ok':'off');p.lastChild.textContent=v?'网页控制已连接':'网页控制已断开';p.title=v?'网页已获得开发板控制权':'点击重新连接并夺回控制权'}btns();if(!v&&why){var w=$('offWhy');if(w)w.textContent=why;}}
/* The socket is push-only from here on: a key going down and a slow status
heartbeat, both a few dozen bytes. Every query and write goes over plain HTTP
instead, because an HTTP response is framed by Content-Length and can carry the
whole binding table however big it gets. The socket cannot: it used to build the
snapshot into a 1024-byte buffer and hand it to a 768-byte frame buffer, which
dropped it without a word and left the page stuck on "waiting to read" forever.
Anything that must fit in one frame must stay small. */
function req(u,m){
return fetch(u,{cache:'no-store',method:m||'GET',headers:m==='POST'?{'X-MRB-Slot':String(selectedSlot)}:{}}).then(function(r){
if(r.status===401){var e=new Error('登录会话无效，请重新登录');e.relogin=true;throw e}
return r.json().catch(function(){throw new Error('HTTP '+r.status)}).then(function(j){
if(!r.ok)throw new Error((j&&j.error)||('HTTP '+r.status));return j})})}
function pulse(raw){if(!raw)return;Array.prototype.forEach.call($('rmArt').children,function(e){
if(+e.dataset.r===raw){e.classList.add('pulse');setTimeout(function(){e.classList.remove('pulse')},260)}})}
function wsMessage(j){
if(!j||typeof j.type!=='string')return;
if(j.type==='key'){if(typeof j.keyPresses==='number')S.keyPress=j.keyPresses;
var down=j.pressed===undefined?!!j.activeKey:!!j.pressed,raw=+j.lastKey||0;
if(raw&&down){pulse(raw);
if(selectedSlot&& !learned.some(function(k){return k.raw===raw})){load().then(function(){highlight(typeof j.activeKey==='number'&&j.activeKey?j.activeKey:raw)})}}
if(typeof j.activeKey==='number')highlight(j.activeKey);return}
if(j.type==='status'){const changed=!S.lastStatus||j.remoteConnected!==S.lastStatus.remoteConnected;stat(j);if(!S.on||S.slotBusy||changed)load();return}
if(j.type==='claimed'){S.claimed=true;return}}
function wsOpen(){S.wsRetry=0;S.wsOpened=true;S.claimed=false;load()}
function wsClosed(e){var taken=S.claimed||(e&&e.code===4001);online(false,taken?'控制权已被另一网页接管，点击可夺回':'连接意外断开，正在自动重连');if(S.wsTimer)clearTimeout(S.wsTimer);S.wsTimer=null;if(!taken){var d=Math.min(8000,600*(S.wsRetry=(S.wsRetry||0)+1));S.wsTimer=setTimeout(function(){connectWS(false)},d)}}
function connectWS(manual){
if(S.ws&&S.ws.readyState===1)return;
/* the socket cannot carry an Authorization header, so fetch the token first
over an authenticated request and hand it over in the URL */
fetch('/api/token',{cache:'no-store'}).then(function(r){
if(r.status===401){var e=new Error('relogin');e.relogin=true;throw e}
return r.json()}).then(function(j){
if(!j||typeof j.token!=='string'||!j.token)throw Error('no token');
if(j.occupied&&!manual){online(false,'另一网页正在控制，点击可夺回控制权');return}
openSocket(j.token,manual)}).catch(function(e){
if(e&&e.relogin){location.href='/login';return}   /* session gone: sign in again */
online(false,'连接意外断开，正在自动重连');if(S.wsTimer)clearTimeout(S.wsTimer);S.wsTimer=setTimeout(function(){connectWS(false)},2500)})}
function openSocket(token,manual){
try{if(S.ws){S.ws.onclose=null;S.ws.close()}}catch(e){}
var proto=(location.protocol==='https:')?'wss://':'ws://';
try{S.ws=new WebSocket(proto+location.host+'/ws?token='+encodeURIComponent(token)+(manual?'&action=claim':''))}catch(e){wsClosed();return}
S.ws.onopen=wsOpen;S.ws.onclose=wsClosed;S.ws.onerror=function(){};
S.ws.onmessage=function(ev){var j=null;try{j=JSON.parse(ev.data)}catch(e){return}wsMessage(j)}}
function idx(l){var o={};(l||[]).forEach(function(a){o[a.raw]=a});return o}
function apply(d){if(!Array.isArray(d.bindings)||!Array.isArray(d.defaults)||!Array.isArray(d.effective))throw Error('按键数据不完整');
S.b=idx(d.bindings);S.d=idx(d.defaults);S.e=idx(d.effective);S.ok=true;render();if(typeof syncDefault==='function')syncDefault()}
function render(){var n=0;KEYS.forEach(function(k){var r=k[0],c=$('k'+r),b=S.b[r];if(b)n++;
c.className='k'+(b?' cus':'')+(r===S.sel?' sel':'');c.querySelector('.tg').textContent=b?'自定义':'基础';
c.querySelector('.act').textContent=fmt(cur(r))});
Array.prototype.forEach.call($('rmArt').children,function(e){e.classList.toggle('sel',+e.dataset.r===S.sel)});
var count=$('cnt');if(count)count.textContent=n;btns();drawWires()}
function drawWires(){var g=$('grid'),svg=$('wires');if(!g||!svg)return;
var gb=g.getBoundingClientRect(),out='<defs><marker id="tip" viewBox="0 0 8 8" refX="7" refY="4" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0 0L8 4L0 8Z" style="fill:#aeb4bf;stroke:none"/></marker></defs>';if(innerWidth<=320){svg.innerHTML='';return}
KEYS.forEach(function(k){var card=$('k'+k[0]),btn=document.querySelector('#rmArt [data-r="'+k[0]+'"]');
if(!card||!btn)return;
var cb=card.getBoundingClientRect(),bb=btn.getBoundingClientRect();
var x1=(bb.left+bb.width/2)-gb.left,y1=bb.top+bb.height/2-gb.top;
var x2=(k[3]==='L'?cb.right+9:cb.left-9)-gb.left,y2=cb.top+cb.height/2-gb.top;
var dx=(x2-x1)*0.42;
out+='<path marker-end="url(#tip)" data-r="'+k[0]+'" d="M'+x1.toFixed(1)+','+y1.toFixed(1)+' C'+(x1+dx).toFixed(1)+','+y1.toFixed(1)+' '+(x2-dx).toFixed(1)+','+y2.toFixed(1)+' '+x2.toFixed(1)+','+y2.toFixed(1)+'"/>';});
svg.innerHTML=out}
function highlight(raw){
KEYS.forEach(function(k){var el=$('k'+k[0]);if(el)el.classList.toggle('live',!!raw&&k[0]===raw)});
Array.prototype.forEach.call($('rmArt').children,function(e){e.classList.toggle('live',!!raw&&+e.dataset.r===raw)});
Array.prototype.forEach.call(document.querySelectorAll('#wires path'),function(p){p.classList.toggle('live',!!raw&&+p.dataset.r===raw)});
Array.prototype.forEach.call(document.querySelectorAll('.learned-key'),function(e){var b=e.querySelector('[data-learned]');e.classList.toggle('heard',!!raw&&!!b&&+b.dataset.learned===raw)})
}
function memory(j){function kb(n){return typeof n==='number'&&n>=0?(n/1024).toFixed(1)+' KiB':'—'}
if(typeof j.heapFree!=='number')return;
$('memFree').textContent=kb(j.heapFree)+' 可用';$('memDetail').textContent='历史最低 '+kb(j.heapMin)+' · 最大连续块 '+kb(j.heapLargest);$('memState').textContent='更新于 '+new Date().toLocaleTimeString()+' · 约 5 秒更新';var valid=typeof j.heapTotal==='number'&&j.heapTotal>0&&j.heapFree>=0&&j.heapFree<=j.heapTotal,used=valid?100*(j.heapTotal-j.heapFree)/j.heapTotal:null;if($('memRing')){$('memRing').style.setProperty('--used',used===null?'0%':used+'%');$('memPct').textContent=used===null?'—':used.toFixed(1)+'%';$('memTotal').textContent=valid?'总堆内存 '+kb(j.heapTotal):'总堆内存未知';$('memRing').setAttribute('aria-label',used===null?'已使用内存比例未知':'已使用堆内存 '+used.toFixed(1)+'%')}}
function connectionHint(j){var e=$("diag"),s=String(j&&j.remoteState||""),t=S.switching?"正在切换遥控器，请按一下任意键唤醒。":s==="DISCOVERING"?"正在发现服务，请按一下任意键唤醒。":s==="CONNECTING"||s==="DIRECT"?"正在连接，请按一下任意键唤醒。":s==="SCANNING"||s==="BACKOFF"?"正在搜索，请按一下任意键唤醒。":"";e.textContent=t;e.className=t?"wake":""}function stat(j){S.lastStatus=j;memory(j);var rc=!!j.remoteConnected,h=!!j.hostConnected;// Version and build stamp come from /api/status, i.e. from the running
// firmware, not from the generated page bytes - so it can never go stale.
if(j.type!=='status'||Object.prototype.hasOwnProperty.call(j,'fwVersion')||Object.prototype.hasOwnProperty.call(j,'buildTime'))$('ver').textContent=(j.fwVersion||'')+(j.buildTime?' · build '+j.buildTime:'');
var bat=(typeof j.battery==='number'&&j.battery>=0&&j.battery<=100)?j.battery+'%':'未知';
function pill(id,on,t){var e=$(id);e.className='pill '+(on?'ok':'off');e.lastChild.textContent=t}
pill('pR',rc,rc?'遥控器已连接':'遥控器未连接');pill('pH',h,h?'被控蓝牙已连接':'被控蓝牙未连接');pill('pB',bat!=='未知',bat==='未知'?'电量未知':'电量 '+bat);
$('h1').textContent=(rc&&h)?'RC003 已就绪':(rc?'遥控器已连接，等待主机':'等待遥控器连接');
$('h2').textContent=(rc&&h)?'蓝牙链路正常。点下方卡片或遥控器按键即可改绑。':(rc?'请在电脑或手机蓝牙设置里连接 Mi Remote Bridge。':'已有映射仍会保留，也可以现在先配置。');
connectionHint(j);if(typeof drawSlots==='function')drawSlots();
if(typeof j.activeKey==='number')highlight(j.activeKey)}
function load(manual){if(S.load||S.busy||$('ed').open||$('rd').open)return;S.load=true;const socket=S.ws;btns();
return req('/api/slots').then(applySlots).then(function(){return req('/api/bindings')}).then(apply).then(function(){return req('/api/status')}).then(function(j){stat(j);online(socket===S.ws&&!!socket&&socket.readyState===1);if(manual)toast('已刷新')})
.catch(function(e){if(e&&e.relogin){location.href='/login';return}
var m=(e&&e.message==='Failed to fetch')?'连接失败，请确认设备和本机在同一网络':((e&&e.message)||'连接失败');
online(false,m);if(manual)toast(m)}).finally(function(){S.load=false;btns()})}
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
S.busy=true;btns();$('edE').hidden=true;
var q=Object.keys(a).map(function(x){return x+'='+a[x]}).join('&');
return req('/api/set?'+q,'POST').then(function(j){if(j.ok!==true)throw Error('设备未确认保存');return req('/api/bindings')})
.then(function(d){apply(d);var got=S.b[k===0?parseInt(a.raw,16):S.cur];
if(k===0?!!got:!same(got,{kind:k,mod:a.mod,key:a.key,cons:a.cons}))throw Error('回读结果与提交不一致，保存未确认');
$('ed').close();toast(k===0?'已恢复此键基础映射':'映射已保存并回读确认')})
.catch(function(e){$('edE').textContent=err(e);$('edE').hidden=false}).finally(function(){S.busy=false;btns()})}
function reset(){if(S.busy||!S.on)return Promise.resolve();S.busy=true;btns();$('rdE').hidden=true;
return req('/api/reset','POST').then(function(j){if(j.ok!==true)throw Error('设备未确认恢复');return req('/api/bindings')})
.then(function(d){apply(d);if(d.bindings.length)throw Error('仍存在自定义绑定，恢复未确认');$('rd').close();toast('已清除全部自定义绑定')})
.catch(function(e){$('rdE').textContent=err(e);$('rdE').hidden=false}).finally(function(){S.busy=false;btns()})}
document.addEventListener('click',function(e){var t=e.target.closest?e.target.closest('[data-r]'):null;if(t)openEd(+t.dataset.r)});
$('edF').onsubmit=function(e){e.preventDefault();save()};
$('kind').onchange=draft;$('key').onchange=draft;$('cons').onchange=draft;
$('clr').onclick=function(){$('kind').value='0';draft()};
$('edX').onclick=$('edC2').onclick=function(){if(!S.busy)$('ed').close()};
$('ed').addEventListener('cancel',function(e){if(S.busy)e.preventDefault()});
$('rd').addEventListener('cancel',function(e){if(S.busy)e.preventDefault()});
$('resetAll').onclick=function(){$('rdE').hidden=true;$('rd').showModal()};
$('rdC').onclick=function(){$('rd').close()};$('rdY').onclick=reset;
$('refresh').onclick=$('refresh2').onclick=function(){load(true)};$('retry').onclick=$('pW').onclick=function(){connectWS(true)};
$('goMap').onclick=function(){document.querySelector('.head').scrollIntoView({behavior:'smooth',block:'start'})};
document.addEventListener('visibilitychange',function(){if(!document.hidden&&S.ws&&S.ws.readyState===1)load()});
window.addEventListener('resize',drawWires);new ResizeObserver(drawWires).observe($('grid'));
connectWS(true);
const workspace=document.querySelector('.workspace'), section=workspace.querySelector('section');
const side=document.createElement('div');side.className='side';workspace.append(side);
section.className='card mapping-panel';const shell=$('grid').parentElement;shell.replaceWith($('grid'));
const legacy=document.createElement('div');legacy.hidden=true;document.body.append(legacy);
legacy.append(document.querySelector('.hero'),document.querySelector('.foot'),document.querySelector('.note'),$('pR'),$('pB'));
const board=document.querySelector('.board');side.append(board);board.prepend($('pH'),$('pW'));
const boardTitle=board.querySelector('h2'), art=board.querySelector('svg'), memoryBox=board.querySelector('.memory');
const ring=document.createElement('div');ring.id='memRing';ring.setAttribute('role','img');ring.innerHTML='<div><b id="memPct">—</b><span>已使用</span></div>';memoryBox.querySelector('h3').after(ring);const total=document.createElement('small');total.id='memTotal';$('memFree').after(total);
const hardware=document.createElement('div');hardware.className='hardware';const legend=document.createElement('div');legend.className='legend';
[...board.children].filter(e=>e.tagName==='P').forEach(e=>legend.append(e));hardware.append(art,legend);
const boardBody=document.createElement('div');boardBody.className='board-body';boardBody.append(hardware,memoryBox);board.append(boardBody);
const rack=document.createElement('section');rack.className='card rack';rack.innerHTML='<div class="rack-heading"><h2>遥控器配置</h2><span class="mut">3 个槽位 · 独立保存</span></div><div class="slots" id="slots"></div><div class="rack-actions"><div><b>按下即转发，松开即释放</b><small id="slotSummary">快捷键修改后自动保存</small></div><button class="btn" id="pairRemote">添加设备</button><button class="btn d" id="deleteRemote">删除设备</button><button class="btn d" id="uiReset">恢复默认设置</button></div>';
side.prepend(rack);
const discovered=document.createElement('div');discovered.id='discovered';discovered.hidden=true;section.append(discovered);
const editor=document.createElement('dialog');editor.id='slotEditor';editor.innerHTML='<div class="dh"><div><h2 id="slotEdTitle">编辑按键</h2><small class="mut">修改后自动保存到当前槽位</small></div><button class="btn" id="slotEdClose">完成</button></div><div class="db"><div id="learnedControls"><label class="f">按键名称<input id="slotName" maxlength="24" autocomplete="off"></label></div><label class="f">动作类型<select id="slotKind"><option value="1">键盘快捷键</option><option value="2">媒体 / 系统控制</option><option value="0">不转发</option></select></label><div id="slotKeyboard"><p class="mut">修饰键 · 可多选</p><div class="ms" id="slotMods"></div><label class="f">主键<select id="slotKey"></select></label></div><label class="f" id="slotMedia">媒体 / 系统动作<select id="slotCons"></select></label><div class="pv" id="slotValue"></div><p class="hint" id="slotSaved" role="status"></p></div><div class="da"><button type="button" class="btn d" id="slotDelete">删除此按键</button><button type="button" class="btn" id="slotEdDone">完成</button></div>';
document.body.append(editor);
opt('slotKey',KB);opt('slotCons',CS);
let selectedSlot=0,editingKey=null,editingMods=0;

let slots=[{name:'RC003',address:''},{name:'遥控器 02',address:''},{name:'遥控器 03',address:''}],learned=[];
function applySlots(j){if(!Array.isArray(j.slots)||j.slots.length!==3||j.active<0||j.active>2)throw Error('槽位数据不完整');selectedSlot=j.active;slots=j.slots;learned=j.keys||[];S.slotBusy=!!j.busy;if(j.error)toast(j.error);drawSlots()}
function slotTitle(i){return i===0?'RC003':slots[i].name||'遥控器 0'+(i+1)}
function syncDefault(){drawSlots()}
const ICON_CODES=['du','dd','dl','dr','p6','p7','p3','p4'];
const COMMON_ICON_CODES={233:'p6',234:'p7'};
function knownKey(raw){return KEYS.find(k=>k[0]===raw&&ICON_CODES.includes(k[4]))}
function keyIcon(raw){const k=knownKey(raw);if(k)return icon(k)+' ';const c=COMMON_ICON_CODES[raw];return c?icon([0,'','','',c])+' ':''}
function keyTitle(k){return k.name||(knownKey(k.raw)||[])[1]||'按键 0x'+hx(k.raw)}
function drawSlots(){
 $('slots').innerHTML=slots.map((s,i)=>{const active=i===selectedSlot,rc=active&&S.lastStatus&&S.lastStatus.remoteConnected,rs=S.lastStatus&&S.lastStatus.remoteState,wake=active&&s.address&&!rc&&(S.switching||rs==="CONNECTING"||rs==="DIRECT"||rs==="DISCOVERING"||rs==="SCANNING"||rs==="BACKOFF");
 return '<button class="slot '+(active?'selected':'')+'" data-slot="'+i+'" aria-pressed="'+active+'"><span class="slot-index">槽位 0'+(i+1)+(i===0?' · 小米预设':'')+'</span><strong>'+esc(slotTitle(i))+'</strong><span class="slot-link '+(rc?'linked':'')+'">'+(rc?'● 遥控器已连接':s.address?(active?'○ 正在连接':'○ 未启用'):'○ 未添加设备')+'</span><span class="slot-battery">'+(rc?(S.lastStatus.battery>=0?'电量 '+S.lastStatus.battery+'%':'电量未读取'):esc(s.address)||'点击添加设备')+'</span>'+(wake?'<span class="slot-wake">请按一下遥控器唤醒</span>':'')+'</button>'}).join('');
 $('slotSummary').textContent=slotTitle(selectedSlot)+' · 设置保存在开发板';
 $('pairRemote').hidden=!!slots[selectedSlot].address;$('deleteRemote').hidden=!slots[selectedSlot].address;
 section.querySelector('.head').innerHTML='<h2>'+esc(slotTitle(selectedSlot))+' · 按键映射</h2><span class="mut">点击按键编辑 · 自动保存</span>';
 $('grid').hidden=selectedSlot!==0;discovered.hidden=selectedSlot===0;
 if(selectedSlot===0){discovered.innerHTML='';drawWires()}
 else discovered.innerHTML='<div class="learn-banner"><b>逐个按下遥控器按键</b><span>最多记录 16 个按键，点击卡片设置快捷键。</span></div><div class="learned-keys">'+learned.map(k=>'<div class="k learned-key"><button data-learned="'+k.raw+'"><span class="r1">'+keyIcon(k.raw)+'<b>'+esc(keyTitle(k))+'</b></span><span class="r2"><span class="act">'+fmt(cur(k.raw)||{kind:0})+'</span><span class="ed">编辑 ›</span></span></button></div>').join('')+'</div>';
 btns();
}
async function slotAction(action,slot){
 if(!S.on||S.busy||S.load||S.slotBusy)return;
  S.busy=true;S.switching=action==='select';if(S.switching)connectionHint({remoteState:'CONNECTING'});btns();$('ed').close();editor.close();if(typeof pairing!=='undefined')pairing.close();
 try{
  await req('/api/slot?slot='+slot+'&action='+action,'POST');
  for(let i=0;i<200;i++){const j=await req('/api/slots');applySlots(j);if(!j.busy){if(j.error)throw Error(j.error);break}if(i===199)throw Error('操作仍在进行，请稍候');await new Promise(r=>setTimeout(r,150))}
  await req('/api/bindings').then(apply);await req('/api/status').then(stat);
 }catch(e){toast(err(e))}finally{S.busy=false;S.switching=false;connectionHint(S.lastStatus||{});btns()}
}
$('slots').onclick=e=>{const b=e.target.closest('[data-slot]');if(b&&+b.dataset.slot!==selectedSlot)slotAction('select',+b.dataset.slot)};
const pairing=document.createElement('dialog');pairing.innerHTML='<div class="dh"><h2>选择遥控器</h2><button class="btn" id="pairClose">关闭</button></div><div class="db"><p>进入配对模式后选择设备，按信号强度排序。搜索阶段暂无 SN，请用名称和 MAC 区分。</p><div id="nearby"></div></div><div class="da"><button class="btn" id="scanRefresh">刷新搜索结果</button></div>';document.body.append(pairing);
$('pairClose').onclick=()=>pairing.close();
async function nearby(){if(!S.on)return;try{const rows=await req('/api/nearby');$('nearby').innerHTML=rows.map(r=>'<p><button class="btn" data-address="'+esc(r.address)+'" data-type="'+r.type+'">'+esc(r.name||'未命名设备')+'<br>MAC: '+esc(r.address)+' · '+r.rssi+' dBm</button></p>').join('')||'正在搜索，请稍后刷新'}catch(e){toast(err(e))}}
$('scanRefresh').onclick=nearby;
$('nearby').onclick=async e=>{const b=e.target.closest('[data-address]');if(!b||!S.on||S.busy)return;S.busy=true;btns();try{await req('/api/connect?address='+b.dataset.address+'&type='+b.dataset.type,'POST');pairing.close();toast('正在连接所选遥控器')}catch(e){toast(err(e))}finally{S.busy=false;btns()}};
$('pairRemote').onclick=async()=>{if(S.on&&confirm('请先让新遥控器进入配对模式。开始搜索并添加到当前槽位？')){await slotAction('add',selectedSlot);if(!slots[selectedSlot].address){pairing.showModal();nearby();setTimeout(()=>{if(pairing.open)nearby()},3000)}}};

$('deleteRemote').onclick=()=>{if(S.on&&confirm('删除当前槽位的设备配对？保留快捷键，添加新设备后可继续使用。'))return slotAction('delete',selectedSlot)};
discovered.onclick=e=>{const b=e.target.closest('[data-learned]');if(b)openSlotEditor(+b.dataset.learned)};
MD.forEach(m=>{const b=document.createElement('button');b.className='m';b.textContent=m[1];b.dataset.bit=m[0];b.onclick=()=>{if(!S.on||S.busy)return;editingMods^=m[0];autoSave()};$('slotMods').append(b)});
function openSlotEditor(raw){
 if(!S.on||S.busy||S.load||S.slotBusy)return;
 const k=learned.find(k=>k.raw===raw);if(!k)return;
 editingKey=raw;const a=cur(raw)||{kind:0};editingMods=a.mod||0;
 $('slotEdTitle').textContent=keyTitle(k);$('slotName').value=keyTitle(k);$('slotKind').value=String(a.kind);
 setSel('slotKey',a.key||0);setSel('slotCons',a.cons||233);$('slotSaved').textContent='修改后保存到开发板';paintEditor();editor.showModal();
}
function paintEditor(){const kind=+$('slotKind').value;$('slotKeyboard').hidden=kind!==1;$('slotMedia').hidden=kind!==2;[...$('slotMods').children].forEach(b=>b.setAttribute('aria-pressed',String(!!(editingMods&+b.dataset.bit))));$('slotValue').textContent=fmt({kind,mod:editingMods,key:+$('slotKey').value,cons:+$('slotCons').value})}
async function keyWrite(url){
 if(!S.on||S.busy||S.slotBusy)return;
 S.busy=true;btns();
 try{await req(url,'POST');await req('/api/slots').then(applySlots);await req('/api/bindings').then(apply);$('slotSaved').textContent='已保存到开发板'}
 catch(e){$('slotSaved').textContent=err(e)}finally{S.busy=false;btns()}
}
function autoSave(){if(!S.on||S.busy)return;paintEditor();const k=+$('slotKind').value;if(k===1&&!editingMods&&!+$('slotKey').value)return;return keyWrite('/api/set?raw='+hx(editingKey)+'&kind='+k+'&mod='+(k===1?editingMods:0)+'&key='+(k===1?+$('slotKey').value:0)+'&cons='+(k===2?+$('slotCons').value:0))}
['slotKind','slotKey','slotCons'].forEach(id=>$(id).onchange=autoSave);
$('slotName').onchange=()=>keyWrite('/api/key?action=rename&raw='+hx(editingKey)+'&name='+encodeURIComponent($('slotName').value.trim()));
$('slotDelete').onclick=async()=>{if(S.on&&confirm('删除这个已发现的按键及其快捷键？')){await keyWrite('/api/key?action=delete&raw='+hx(editingKey));editor.close()}};
$('slotEdClose').hidden=true;$('slotEdDone').onclick=()=>editor.close();
$('uiReset').onclick=()=>{if(!S.on||S.busy)return;$('rdE').hidden=true;$('rd').querySelector('h2').textContent='重置当前槽位快捷键？';$('rd').querySelector('.db h3').textContent='保留设备配对和已发现按键。';$('rd').showModal()};
$('rdY').onclick=reset;
S.sel=0;drawSlots();




</script>
</body>
</html>
)rawliteral";
