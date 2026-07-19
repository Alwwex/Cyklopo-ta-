// Ulice z OpenStreetMap pres Overpass API.
// Hlavni server (overpass-api.de) casto vraci 406 bez vlastniho User-Agentu
// a bez POST s "text/plain" telem - proto zkousime zrcadla v tomto poradi.
const OVERPASS_SERVERS = [
  'https://overpass.kumi.systems/api/interpreter',
  'https://z.overpass-api.de/api/interpreter',
  'https://overpass-api.de/api/interpreter',
];

const USER_AGENT = 'CykloComp-App/1.0 (+bike-computer-companion)';
const HIGHWAY_FILTER = '^(primary|secondary|tertiary|unclassified|residential|cycleway|track|path|service)$';
const MAX_TOTAL_STREET_POINTS = 550;

async function queryOverpass(query) {
  let lastError;
  for (const url of OVERPASS_SERVERS) {
    try {
      const res = await fetch(url, {
        method: 'POST',
        headers: {
          'Content-Type': 'text/plain',
          'Accept': 'application/json',
          'User-Agent': USER_AGENT,
        },
        body: query,
      });
      if (!res.ok) { lastError = new Error(`Overpass ${url} vratil ${res.status}`); continue; }
      return await res.json();
    } catch (e) {
      lastError = e;
    }
  }
  throw lastError || new Error('Vsechny Overpass servery selhaly');
}

export function computeBbox(points, marginRatio = 0.15) {
  let minLat = 90, maxLat = -90, minLng = 180, maxLng = -180;
  for (const p of points) {
    minLat = Math.min(minLat, p.lat);
    maxLat = Math.max(maxLat, p.lat);
    minLng = Math.min(minLng, p.lng);
    maxLng = Math.max(maxLng, p.lng);
  }
  const latMargin = (maxLat - minLat) * marginRatio || 0.01;
  const lngMargin = (maxLng - minLng) * marginRatio || 0.01;
  return {
    south: minLat - latMargin,
    west: minLng - lngMargin,
    north: maxLat + latMargin,
    east: maxLng + lngMargin,
  };
}

function segmentLength(seg) { return seg.length; }

function simplifyAndCapSegments(segments, maxTotalPoints) {
  let simplified = segments
    .map((seg) => seg.filter((_, i) => i % 2 === 0 || i === seg.length - 1))
    .filter((seg) => seg.length >= 2);

  let total = simplified.reduce((s, seg) => s + seg.length, 0);
  if (total <= maxTotalPoints) return simplified;

  simplified = [...simplified].sort((a, b) => segmentLength(b) - segmentLength(a));
  const kept = [];
  total = 0;
  for (const seg of simplified) {
    if (total + seg.length > maxTotalPoints) continue;
    kept.push(seg);
    total += seg.length;
  }
  return kept;
}

export async function fetchStreetsForRoute(routePoints) {
  if (!routePoints || routePoints.length === 0) return [];
  const bbox = computeBbox(routePoints, 0.15);
  const query = `[out:json][timeout:25];(way["highway"~"${HIGHWAY_FILTER}"](${bbox.south},${bbox.west},${bbox.north},${bbox.east}););out geom;`;
  const data = await queryOverpass(query);
  const segments = (data.elements || [])
    .filter((el) => el.type === 'way' && el.geometry)
    .map((way) => way.geometry.map((pt) => ({ lat: pt.lat, lng: pt.lon })));
  return simplifyAndCapSegments(segments, MAX_TOTAL_STREET_POINTS);
}

// Segmenty -> plochy seznam bodu s oddelovacem {lat:999,lng:999} pro MP: protokol.
export function flattenSegmentsForBle(segments) {
  const flat = [];
  segments.forEach((seg, idx) => {
    seg.forEach((p) => flat.push(p));
    if (idx < segments.length - 1) flat.push({ lat: 999, lng: 999 });
  });
  return flat;
}
