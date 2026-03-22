#include "web_server.h"
#include <ESPAsyncWebServer.h>

static AsyncWebServer  _server(80);
static AsyncEventSource _events("/events");

// Minified dashboard HTML stored in flash (PROGMEM)
static const char DASH_HTML[] PROGMEM = R"==(
<!DOCTYPE html><html><head><meta charset='UTF-8'>
<meta name='viewport' content='width=device-width,initial-scale=1'>
<title>BlackBox Live</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{background:#0d1117;color:#c9d1d9;font-family:Arial,sans-serif;padding:12px}
h1{color:#58a6ff;text-align:center;font-size:1.3em;margin-bottom:12px}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(130px,1fr));gap:8px}
.card{background:#161b22;border:1px solid #30363d;border-radius:8px;padding:12px;text-align:center}
.val{font-size:1.7em;font-weight:700;color:#3fb950}
.lbl{font-size:.72em;color:#8b949e;margin-top:4px}
.alert .val{color:#f85149}
.warn  .val{color:#d29922}
#ab{background:#f85149;border-radius:6px;padding:8px;text-align:center;display:none;margin:8px 0;font-weight:700}
#ts{text-align:center;color:#484f58;font-size:.75em;margin:6px}
a{color:#58a6ff}
</style></head><body>
<h1>&#127968; Disaster BlackBox</h1>
<div id='ab'><span id='ft'></span></div>
<div class='grid'>
<div class='card' id='ct'><div class='val' id='tmp'>--</div><div class='lbl'>&#127777; Temp &deg;C</div></div>
<div class='card' id='ch'><div class='val' id='hum'>--</div><div class='lbl'>&#128167; Humidity %</div></div>
<div class='card' id='cw'><div class='val' id='wat'>--</div><div class='lbl'>&#127754; Water</div></div>
<div class='card' id='cg'><div class='val' id='gas'>--</div><div class='lbl'>&#128168; Gas MQ-2</div></div>
<div class='card' id='cf'><div class='val' id='flm'>--</div><div class='lbl'>&#128293; Flame</div></div>
<div class='card' id='cv'><div class='val' id='vib'>--</div><div class='lbl'>&#128246; Vibration</div></div>
<div class='card'><div class='val' id='bat'>--</div><div class='lbl'>&#128267; Battery V</div></div>
<div class='card'><div class='val' id='sat'>--</div><div class='lbl'>&#128752; GPS Sats</div></div>
</div>
<div id='ts'>Updated: <span id='tm'>--</span></div>
<div style='text-align:center;margin:8px'><a id='mp' href='#' target='_blank'>&#128204; Google Maps</a></div>
<script>
const es=new EventSource('/events');
es.addEventListener('d',e=>{
  const d=JSON.parse(e.data);
  document.getElementById('tmp').innerText=parseFloat(d.t).toFixed(1);
  document.getElementById('hum').innerText=parseFloat(d.h).toFixed(0);
  document.getElementById('wat').innerText=d.w;
  document.getElementById('gas').innerText=d.g;
  document.getElementById('flm').innerText=parseInt(d.f)<400?'YES&#128293;':'No';
  document.getElementById('vib').innerText=parseInt(d.v)?'YES&#9889;':'No';
  document.getElementById('bat').innerText=parseFloat(d.bv).toFixed(2);
  document.getElementById('sat').innerText=d.s;
  document.getElementById('tm').innerText=d.ts;
  document.getElementById('mp').href='https://maps.google.com/?q='+d.la+','+d.lo;
  const ab=document.getElementById('ab');
  if(d.fl!=='NORMAL'&&d.fl!==''){ab.style.display='block';document.getElementById('ft').innerText=d.fl;}
  else ab.style.display='none';
  ['ct','ch','cw','cg','cf','cv'].forEach(id=>document.getElementById(id).className='card');
  if(parseFloat(d.t)>55)document.getElementById('ct').className='card alert';
  if(parseInt(d.w)>400) document.getElementById('cw').className='card alert';
  if(parseInt(d.g)>450) document.getElementById('cg').className='card alert';
  if(parseInt(d.f)<400) document.getElementById('cf').className='card alert';
  if(parseInt(d.v))      document.getElementById('cv').className='card warn';
});
</script></body></html>
)==";

void WebDash::init() {
  _server.on("/", HTTP_GET, [](AsyncWebServerRequest *req) {
    req->send(200, "text/html", DASH_HTML);
  });
  _server.on("/health", HTTP_GET, [](AsyncWebServerRequest *req) {
    req->send(200, "text/plain", "OK");
  });
  _events.onConnect([](AsyncEventSourceClient *c) {
    c->send("connected", "info", millis(), 3000);
  });
  _server.addHandler(&_events);
  _server.begin();
}

void WebDash::pushUpdate(const TelData &tel, const char *gpsCoords, uint8_t sats) {
  // Extract lat/lon from "lat,lon"
  char lat[12] = "0", lon[12] = "0";
  const char *comma = strchr(gpsCoords, ',');
  if (comma) {
    uint8_t ll = (uint8_t)(comma - gpsCoords);
    strncpy(lat, gpsCoords, min(ll, (uint8_t)11)); lat[ll] = '\0';
    strlcpy(lon, comma + 1, sizeof(lon));
  }

  // Build compact JSON (no String, no heap alloc)
  char json[220];
  snprintf(json, sizeof(json),
    "{\"ts\":\"%s\",\"t\":%.1f,\"h\":%.0f,"
    "\"w\":%d,\"g\":%d,\"f\":%d,\"v\":%d,"
    "\"fl\":\"%s\",\"bv\":%.2f,\"bs\":\"%s\","
    "\"la\":\"%s\",\"lo\":\"%s\",\"s\":%u}",
    tel.ts, tel.tempC, tel.humidity,
    tel.water, tel.mq2, tel.flame, tel.vib,
    tel.flags != 0 ? "HAZARD" : "NORMAL",
    tel.battV, tel.battSt,
    lat, lon, sats);

  _events.send(json, "d", millis());
}
