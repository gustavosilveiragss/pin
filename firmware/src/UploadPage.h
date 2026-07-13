#pragma once

namespace pin {

inline constexpr char kUploadPage[] = R"html(<!DOCTYPE html>
<html lang="pt-br"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<title>esp32 pin</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{font:1rem/1.5 system-ui,-apple-system,sans-serif;background:#f5f5f4;color:#1c1c1c;min-height:100vh;
  display:flex;flex-direction:column;align-items:center;justify-content:center;gap:1.25rem;
  padding:2rem max(1rem,env(safe-area-inset-right)) 2rem max(1rem,env(safe-area-inset-left))}
.wrap{width:100%;max-width:22rem;display:flex;flex-direction:column;gap:1rem;align-items:center}
h1{font-size:.95rem;font-weight:500;color:#888;letter-spacing:.04em;text-align:center}
.drop{width:100%;border:1px dashed rgba(0,0,0,.2);border-radius:.7rem;padding:2rem 1rem;text-align:center;
  font-size:.9rem;color:#888;cursor:pointer}
.drop.over{border-color:#111;color:#1c1c1c}
input[type=file]{display:none}
button{width:100%;min-height:2.9rem;background:#111;color:#fff;border:none;border-radius:.7rem;
  font:inherit;font-weight:500;cursor:pointer}
button:disabled{opacity:.45}
.bar{width:100%;height:.5rem;background:rgba(0,0,0,.12);border-radius:1rem;overflow:hidden;display:none}
.bar.on{display:block}.bar>i{display:block;height:100%;width:0;background:#111;transition:width .15s}
.hint{font-size:.78rem;color:#aaa;text-align:center}
.hint a{color:#888}
</style></head>
<body><div class="wrap">
<h1>esp32 pin: enviar bundle</h1>
<p class="hint" id="mycode"></p>
<label class="drop" id="drop" for="file">toque p/ escolher o arquivo .pin</label>
<input id="file" type="file" accept=".pin">
<div class="bar" id="bar"><i id="fill"></i></div>
<button id="send" disabled>enviar</button>
<p class="hint">monte o .pin em <a href="https://pin.marmota.dev.br/" target="_blank" rel="noopener">pin.marmota.dev.br</a></p>
</div>
<script>
var $=function(i){return document.getElementById(i)};
fetch('/codigo').then(function(r){return r.text()}).then(function(t){if(t)$('mycode').textContent='seu codigo: '+t+' — passe pro amigo';}).catch(function(){});
var file=$('file'),drop=$('drop'),send=$('send');
file.addEventListener('change',function(){drop.textContent=file.files[0]?file.files[0].name:'toque p/ escolher o arquivo .pin';send.disabled=!file.files[0];});
['dragover','dragenter'].forEach(function(e){drop.addEventListener(e,function(ev){ev.preventDefault();drop.classList.add('over')})});
['dragleave','dragend','drop'].forEach(function(e){drop.addEventListener(e,function(ev){ev.preventDefault();drop.classList.remove('over')})});
drop.addEventListener('drop',function(ev){if(ev.dataTransfer.files[0]){file.files=ev.dataTransfer.files;drop.textContent=file.files[0].name;send.disabled=false;}});
send.addEventListener('click',function(){
  if(!file.files[0])return;
  var fd=new FormData();fd.append('file',file.files[0]);
  var xhr=new XMLHttpRequest();xhr.open('POST','/upload');
  send.disabled=true;send.textContent='enviando...';$('bar').classList.add('on');
  xhr.upload.onprogress=function(e){if(e.lengthComputable)$('fill').style.width=(e.loaded/e.total*100)+'%'};
  xhr.onload=function(){if(xhr.status>=200&&xhr.status<300){send.textContent='recebido!';}else{send.textContent='enviar';send.disabled=false;$('bar').classList.remove('on');alert('falha (arquivo invalido ou sem espaco)');}};
  xhr.onerror=function(){send.textContent='enviar';send.disabled=false;$('bar').classList.remove('on');alert('erro de rede');};
  xhr.send(fd);
});
</script>
</body></html>)html";

} // namespace pin
