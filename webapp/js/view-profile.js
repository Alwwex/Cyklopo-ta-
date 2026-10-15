// PROFIL - level, streak, tydenni cil, odznaky.
import { bus, $ } from './util.js';
import { loadProfile, saveProfile, loadRides } from './storage.js';
import { levelProgress, checkStreakExpiry, BADGES } from './gamification.js';

let root;

function startOfWeek(d = new Date()) {
  const x = new Date(d);
  x.setDate(x.getDate() - ((x.getDay() + 6) % 7));
  x.setHours(0, 0, 0, 0);
  return x;
}

export function initProfile(el) {
  root = el;
  bus.on('rides-changed', render);
  render();
}

export async function render() {
  let profile = loadProfile();
  const checked = checkStreakExpiry(profile);
  if (checked !== profile) { saveProfile(checked); profile = checked; }
  const rides = await loadRides();
  const week = startOfWeek();
  const weekKm = rides.filter((r) => new Date(r.dateISO) >= week).reduce((s, r) => s + r.distanceKm, 0);
  const totalAsc = rides.reduce((s, r) => s + (r.ascentM || 0), 0);
  const { level, progress, end } = levelProgress(profile.points);
  const goal = profile.weeklyGoalKm || 50;
  const C = 2 * Math.PI * 30;

  root.innerHTML = `<div class="wrap">
    <div class="panel level">
      <svg class="ring" viewBox="0 0 74 74">
        <circle cx="37" cy="37" r="30" fill="none" stroke="var(--panel2)" stroke-width="8"/>
        <circle cx="37" cy="37" r="30" fill="none" stroke="var(--accent)" stroke-width="8" stroke-linecap="round"
          stroke-dasharray="${(C * progress).toFixed(1)} ${C.toFixed(1)}" transform="rotate(-90 37 37)"/>
        <text x="37" y="44" text-anchor="middle" font-size="20" font-weight="900" fill="var(--text)">${level}</text>
      </svg>
      <div style="flex:1">
        <div class="muted small">LEVEL ${level}</div>
        <div class="big">${profile.points} b.</div>
        <div class="muted small">${end - profile.points} b. do levelu ${level + 1}</div>
      </div>
      <div style="text-align:center">
        <div style="font-size:30px">🔥</div>
        <div style="font-weight:900;font-size:20px">${profile.streak}</div>
        <div class="muted small">streak</div>
      </div>
    </div>

    <div class="panel">
      <h3>Týdenní cíl</h3>
      <div style="display:flex;align-items:center;gap:10px">
        <div style="flex:1"><b style="font-size:20px">${weekKm.toFixed(1)}</b> <span class="muted">/ ${goal} km</span>
          <div class="bar ok"><div style="width:${Math.min(100, (weekKm / goal) * 100)}%"></div></div></div>
        <button class="chip" id="gMinus">−10</button><button class="chip" id="gPlus">+10</button>
      </div>
    </div>

    <div class="panel kpis">
      <div class="kpi"><b>${profile.totalKm.toFixed(0)}</b><span>km celkem</span></div>
      <div class="kpi"><b>${rides.length}</b><span>jízd</span></div>
      <div class="kpi"><b>${Math.round(totalAsc)}</b><span>m stoupání</span></div>
    </div>

    <div class="panel">
      <h3>Odznaky (${(profile.badges || []).length}/${BADGES.length})</h3>
      <div class="badges">
        ${BADGES.map((b) => `<div class="badge-card ${profile.badges?.includes(b.id) ? '' : 'locked'}"><span class="ic">${b.icon}</span>${b.label}</div>`).join('')}
      </div>
    </div></div>`;

  const setGoal = (d) => { const p = loadProfile(); p.weeklyGoalKm = Math.max(10, (p.weeklyGoalKm || 50) + d); saveProfile(p); render(); };
  $('#gMinus', root).onclick = () => setGoal(-10);
  $('#gPlus', root).onclick = () => setGoal(10);
}
