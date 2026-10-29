import { initializeApp } from "https://www.gstatic.com/firebasejs/10.12.2/firebase-app.js";
import { getDatabase, ref, onValue, query, limitToLast, onChildAdded } from "https://www.gstatic.com/firebasejs/10.12.2/firebase-database.js";

const db = getDatabase(initializeApp({
  databaseURL: "https://ground-fault-detection-default-rtdb.asia-southeast1.firebasedatabase.app"
}));
const $ = id => document.getElementById(id);
const WARN = 0.50, ALERT = 1.00, MAXA = 2.0, MAXV = 4.096;
const MSG = { NORMAL: "System healthy. No ground fault detected.", WARNING: "Voltage or current rising. Inspect the ground line.", ALERT: "GROUND FAULT! High voltage or current detected. Isolate the line." };

// ---- loading screen ----
document.body.classList.add("load");
const steps = [["Starting system…", 20], ["Loading dashboard…", 45], ["Connecting to Firebase…", 70]];
steps.forEach(([t, p], i) => setTimeout(() => { $("lstep").textContent = t; $("lfill").style.width = p + "%"; }, i * 600));
let fbReady = false, minTime = false;
setTimeout(() => { minTime = true; hideLoader(); }, 2400);
function hideLoader() {
  if (!fbReady || !minTime) return;
  $("lstep").textContent = "Connected ✔"; $("lfill").style.width = "100%";
  setTimeout(() => { $("loader").classList.add("done"); document.body.classList.remove("load"); }, 400);
}
setTimeout(() => { fbReady = true; hideLoader(); }, 7000); // never get stuck

onValue(ref(db, ".info/connected"), s => { window.fbOn = s.val(); if (s.val()) { fbReady = true; hideLoader(); } });

// ---- chart ----
const chart = new Chart($("chart"), {
  type: "line",
  data: { labels: [], datasets: [
    { label: "Voltage (V)", data: [], borderColor: "#00e5ff", backgroundColor: "#00e5ff33", fill: true, tension: .35, pointRadius: 0 },
    { label: "Current (A)", data: [], borderColor: "#ff2e63", tension: .35, pointRadius: 0 } ] },
  options: { animation: false, responsive: true, scales: { x: { ticks: { color: "#aab", maxTicksLimit: 6 }, grid: { color: "#fff1" } }, y: { min: 0, ticks: { color: "#aab" }, grid: { color: "#fff1" } } }, plugins: { legend: { labels: { color: "#eef" } } } }
});

// ---- live data ----
let lastSeen = 0;
onValue(ref(db, "groundfault/live"), snap => {
  const d = snap.val(); if (!d) return;
  const v = +d.voltage || 0, a = +d.currentA || 0, sv = +d.sensorV || 0;
  const status = d.status || (Math.max(a, v) >= ALERT ? "ALERT" : Math.max(a, v) >= WARN ? "WARNING" : "NORMAL");
  lastSeen = Date.now();
  $("cur").textContent = a.toFixed(3);
  $("volt").textContent = v.toFixed(3) + " V"; $("ma").textContent = Math.round(a * 1000) + " mA"; $("sv").textContent = sv.toFixed(3) + " V";
  $("bar0").style.width = Math.min(100, v / MAXV * 100) + "%"; $("bar1").style.width = Math.min(100, a / MAXA * 100) + "%";
  $("arc").style.strokeDashoffset = 100 - Math.min(100, a / MAXA * 100);
  $("hero").className = "card hero " + status.toLowerCase();
  $("badge").textContent = status; $("msg").textContent = MSG[status] || "";
  const t = new Date().toLocaleTimeString(); $("last").textContent = "updated " + t;
  const ds = chart.data.datasets; chart.data.labels.push(t); ds[0].data.push(v); ds[1].data.push(a);
  if (chart.data.labels.length > 60) { chart.data.labels.shift(); ds[0].data.shift(); ds[1].data.shift(); }
  chart.update();
});

onChildAdded(query(ref(db, "groundfault/events"), limitToLast(15)), snap => {
  const e = snap.val(), ul = $("events"); ul.querySelector(".empty")?.remove();
  const li = document.createElement("li"); li.className = e.status;
  li.innerHTML = `<b>${e.status}</b> · ${(+e.currentA * 1000).toFixed(0)} mA<br><small>${new Date(e.t || Date.now()).toLocaleString()}</small>`;
  ul.prepend(li);
});

setInterval(() => {
  const age = (Date.now() - lastSeen) / 1000, c = $("conn");
  if (!window.fbOn) { c.className = "conn off"; $("connTxt").textContent = "Firebase offline"; }
  else if (lastSeen && age < 6) { c.className = "conn on"; $("connTxt").textContent = "ESP32 live"; }
  else { c.className = "conn"; $("connTxt").textContent = lastSeen ? "ESP32 silent " + Math.round(age) + "s" : "Waiting for ESP32…"; }
}, 1000);
