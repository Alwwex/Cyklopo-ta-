// Planovani trasy: BRouter (s vyskovym profilem) -> fallback OSRM,
// ulice kolem trasy z OpenStreetMap pres Overpass API.
import { polylineLengthM } from './util.js';

export const ROUTE_PROFILES = [
  { id: 'trekking', label: 'Trekking / gravel' },
  { id: 'fastbike', label: 'Silnice' },
  { id: 'safety', label: 'Bezpečně' },
  { id: 'shortest', label: 'Nejkratší' },
];

async function brouter(waypoints, profile) {
  const lonlats = waypoints.map((w) => `${w.lng.toFixed(6)},${w.lat.toFixed(6)}`).join('|');
  const url = `https://brouter.de/brouter?lonlats=${lonlats}&profile=${profile}&alternativeidx=0&format=geojson`;
  const res = await fetch(url);
  if (!res.ok) throw new Error(`BRouter ${res.status}`);
  const gj = await res.json();
  const f = (gj.features || []).find((x) => x.geometry?.type === 'LineString');
  if (!f) throw new Error('BRouter: prazdna odpoved');
  const coords = f.geometry.coordinates.map(([lng, lat]) => ({ lat, lng }));
  const elevations = f.geometry.coordinates.map((c) => (c.length > 2 ? c[2] : null));
  const p = f.properties || {};
  return {
    coords,
    elevations,
    distanceM: +p['track-length'] || polylineLengthM(coords),
    ascentM: +p['filtered ascend'] || 0,
    source: 'BRouter',
  };
}

async function osrm(waypoints) {
  const c = waypoints.map((w) => `${w.lng.toFixed(6)},${w.lat.toFixed(6)}`).join(';');
  const res = await fetch(`https://routing.openstreetmap.de/routed-bike/route/v1/bike/${c}?overview=full&geometries=geojson`);
  if (!res.ok) throw new Error(`OSRM ${res.status}`);
  const data = await res.json();
  if (!data.routes?.length) throw new Error('OSRM nenasel trasu');
  const coords = data.routes[0].geometry.coordinates.map(([lng, lat]) => ({ lat, lng }));
  return { coords, elevations: [], distanceM: data.routes[0].distance, ascentM: 0, source: 'OSRM' };
}

export async function computeRoute(waypoints, profile = 'trekking') {
  if (waypoints.length < 2) throw new Error('Potreba alespon 2 body');
  try {
    return await brouter(waypoints, profile);
  } catch (e) {
    console.warn('BRouter selhal, zkousim OSRM', e);
    return osrm(waypoints);
  }
}

// rovnomerne vybrani max N bodu (krajni body zachovany) - limit pameti ESP
export function reducePoints(coords, maxPoints) {
  if (coords.length <= maxPoints) return coords;
  const out = [];
  const step = (coords.length - 1) / (maxPoints - 1);
  for (let i = 0; i < maxPoints; i++) out.push(coords[Math.round(i * step)]);
  return out;
}

const OVERPASS = [
  'https://overpass.kumi.systems/api/interpreter',
  'https://overpass-api.de/api/interpreter',
  'https://z.overpass-api.de/api/interpreter',
];
const HIGHWAYS = '^(primary|secondary|tertiary|unclassified|residential|cycleway|track|path|service|living_street)$';

export async function fetchStreets(route, maxPoints = 550) {
  let s = 90, n = -90, w = 180, e = -180;
  for (const p of route) { s = Math.min(s, p.lat); n = Math.max(n, p.lat); w = Math.min(w, p.lng); e = Math.max(e, p.lng); }
  const mLat = (n - s) * 0.15 || 0.01, mLng = (e - w) * 0.15 || 0.01;
  const q = `[out:json][timeout:25];way["highway"~"${HIGHWAYS}"](${s - mLat},${w - mLng},${n + mLat},${e + mLng});out geom;`;
  let data = null, lastErr = null;
  for (const url of OVERPASS) {
    try {
      const res = await fetch(url, { method: 'POST', headers: { 'Content-Type': 'text/plain' }, body: q });
      if (!res.ok) { lastErr = new Error(`Overpass ${res.status}`); continue; }
      data = await res.json();
      break;
    } catch (err) { lastErr = err; }
  }
  if (!data) throw lastErr || new Error('Overpass nedostupny');
  let segs = (data.elements || [])
    .filter((el) => el.type === 'way' && el.geometry)
    .map((way) => way.geometry.filter((_, i, a) => i % 2 === 0 || i === a.length - 1).map((g) => ({ lat: g.lat, lng: g.lon })))
    .filter((seg) => seg.length >= 2);
  // nejdelsi segmenty maji prednost, celkem max maxPoints bodu (pamet ESP)
  segs.sort((a, b) => b.length - a.length);
  const out = [];
  let total = 0;
  for (const seg of segs) {
    if (total + seg.length + 1 > maxPoints) continue;
    out.push(seg);
    total += seg.length + 1;
  }
  return out;
}

export function flattenSegments(segs) {
  const flat = [];
  segs.forEach((seg, i) => {
    flat.push(...seg);
    if (i < segs.length - 1) flat.push({ lat: 999, lng: 999 });
  });
  return flat;
}

export function toGPX(name, track, timeISO) {
  const pts = track.map(([lat, lng]) => `      <trkpt lat="${lat}" lon="${lng}"></trkpt>`).join('\n');
  return `<?xml version="1.0" encoding="UTF-8"?>
<gpx version="1.1" creator="CykloComp" xmlns="http://www.topografix.com/GPX/1/1">
  <metadata><name>${name}</name><time>${timeISO}</time></metadata>
  <trk>
    <name>${name}</name>
    <type>cycling</type>
    <trkseg>
${pts}
    </trkseg>
  </trk>
</gpx>
`;
}
