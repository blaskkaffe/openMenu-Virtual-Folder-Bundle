// openMenu link module, page side: a box under the network buttons with the Dreamcast's connection status, the players
// who can be joined, the games on the SD card (each with a Start button) and the events. Talks to /openmenu/state,
// POST /openmenu/launch and, for the players, the Online players module's GET /players (no players list without it).
(function(){
 var slot=document.getElementById("main-slot"),net=document.getElementById("net");
 if(!slot)return;
 var box=document.createElement("div");box.id="om-box";box.className="now rows";
 box.innerHTML='<div class="nlabel">Dreamcast (openMenu):</div>'+
  '<b id="om-status">-</b>'+
  '<div class="row"><span class="k">Message</span><span class="v"><span class="st" id="om-msg"></span></span></div>'+
  '<div class="row"><span class="k">Join</span><span class="v"><span class="list keep" id="om-join"></span></span></div>'+
  '<div class="row"><span class="k">Events</span><span class="v"><span class="list keep" id="om-events"></span></span></div>'+
  '<div class="row"><span class="k">Games</span><span class="v"><input id="om-find" type="search" placeholder="Search the card" autocomplete="off"><span class="list keep" id="om-games"></span></span></div>';
 var after=document.getElementById("debug-bar");   // below the players box (it loads first), above the debug log bar
 slot.insertBefore(box,after&&after.parentNode===slot?after:null);
 function syncColour(){box.classList.toggle("dcnet",!!(net&&net.classList.contains("dcnet")))}
 if(net){syncColour();new MutationObserver(syncColour).observe(net,{attributes:true,attributeFilter:["class"]})}
 var last=null,msg="",timer=null,players=[];
 function esc(t){return String(t).replace(/[&<>"]/g,function(c){return {"&":"&amp;","<":"&lt;",">":"&gt;",'"':"&quot;"}[c]})}
 // the card's game that a game title means: the same name, else one name inside the other (shortest wins)
 function norm(t){return String(t||"").toLowerCase().replace(/[^a-z0-9]+/g,"")}
 function matchGame(title){
  var w=norm(title),gs=(last&&last.games)||[],best=null;
  if(w.length<3)return null;
  for(var i=0;i<gs.length;i++){var n=norm(gs[i].name);if(n===w)return gs[i]}
  for(i=0;i<gs.length;i++){var m=norm(gs[i].name);if(m.length>=3&&(w.indexOf(m)>=0||m.indexOf(w)>=0)&&(!best||m.length<norm(best.name).length))best=gs[i]}
  return best;
 }
 function nameOf(product){var g=(last&&last.games||[]).filter(function(x){return x.product==product})[0];return g?g.name:product}
 function btn(product,label,enabled){return '<button type="button" class="om-go" data-p="'+esc(product)+'"'+(enabled?"":" disabled")+'>'+label+'</button>'}
 function render(){
  if(!last)return;
  var st=document.getElementById("om-status");
  st.textContent=last.connected?"Connected":(last.seen_ago===null?"Not seen yet":"Not connected");
  var count=last.games.length;
  var note=last.pending?"Waiting for the Dreamcast to start "+nameOf(last.pending)+"...":
           last.launched?"Starting "+nameOf(last.launched)+" on the Dreamcast.":msg;
  if(!note&&!count)note="No game list yet. It arrives when openMenu connects with DC Now! on.";
  document.getElementById("om-msg").textContent=note;
  var can=last.connected&&!last.pending;
  var join=players.filter(function(p){return p.game});
  document.getElementById("om-join").innerHTML=join.length?join.map(function(p){
   return '<div class="p"><span class="pn">'+esc(p.player)+(p.network?" \u2022 "+esc(p.network):"")+'<span class="pg">'+esc(p.game)+(matchGame(p.game)?"":" (not on your card)")+'</span></span>'+
    (matchGame(p.game)?btn(matchGame(p.game).product,"Join",can):"")+'</div>'}).join(""):"No players online";
  document.getElementById("om-events").innerHTML=last.events.length?last.events.map(function(e){
   return '<div class="p"><span class="pn">'+esc(e.title)+'<span class="pg">'+esc([e.start,e.network,e.game].filter(Boolean).join(" • "))+
    (e.text?'<br>'+esc(e.text):"")+'</span></span>'+(e.product?btn(e.product,"Start",can):"")+'</div>'}).join(""):
   (last.events_error?"Events unavailable ("+esc(last.events_error)+")":"No events listed");
  var q=document.getElementById("om-find").value.toLowerCase();
  var shown=last.games.filter(function(g){return !q||g.name.toLowerCase().indexOf(q)>=0||g.product.toLowerCase().indexOf(q)>=0}).slice(0,60);
  document.getElementById("om-games").innerHTML=count?shown.map(function(g){
   return '<div class="p"><span class="pn">'+esc(g.name)+'<span class="pg">'+esc(g.product+(g.disc?" • disc "+g.disc:""))+'</span></span>'+btn(g.product,"Start",can)+'</div>'}).join("")+
   (count>shown.length?'<div class="pg">'+(count-shown.length)+' more: use the search</div>':""):"";
 }
 function load(){
  var x=new XMLHttpRequest();x.open("GET","/openmenu/state",true);
  x.onload=function(){if(x.status==200){try{last=JSON.parse(x.responseText)}catch(e){return}render()}};x.send();
 }
 function loadPlayers(){
  var x=new XMLHttpRequest();x.open("GET","/players",true);
  x.onload=function(){if(x.status==200){try{players=JSON.parse(x.responseText).players||[]}catch(e){return}render()}};x.send();
 }
 function launch(product){
  var x=new XMLHttpRequest();x.open("POST","/openmenu/launch",true);x.setRequestHeader("X-Requested-With","netswitch");
  x.onload=function(){try{msg=JSON.parse(x.responseText).message||""}catch(e){msg=""}load()};
  x.send(JSON.stringify({product:product}));
 }
 box.addEventListener("click",function(ev){
  var b=ev.target;if(b&&b.className=="om-go"&&!b.disabled){
   if(confirm("Start "+nameOf(b.getAttribute("data-p"))+" on the Dreamcast?"))launch(b.getAttribute("data-p"))}
 });
 document.getElementById("om-find").addEventListener("input",render);
 function tick(){if(!document.hidden){load();if(++n%6==0)loadPlayers()}}
 var n=0;
 load();loadPlayers();timer=setInterval(tick,5000);
})();
