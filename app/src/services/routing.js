// Vypocet trasy po cestach - BRouter (verejne API), s fallbackem na OSRM FOSSGIS.

export const ROUTE_PROFILES = [
  { id: 'trekking', label: 'Trekking' },
  { id: 'fastbike', label: 'Silnicni' },
  { id: 'shortest', label: 'Nejkratsi' },
];

function haversineM(a, b) {
  const R = 6371000;
  const dLat = ((b.lat - a.lat) * Math.PI) / 180;
  const dLng = ((b.lng - a.lng) * Math.PI) / 180;
  const la1 = (a.lat * Math.PI) / 180;
  const la2 = (b.lat * Math.PI) / 180;
  const h = Math.sin(dLat / 2) ** 2 + Math.cos(la1) * Math.cos(la2) * Math.sin(dLng / 2) ** 2;
  return 2 * R * Math.asin(Math.sqrt(h));
}

function estimatePolylineLength(coords) {
  let total = 0;
  for (let i = 1; i < coords.length; i++) total += haversineM(coords[i - 1], coords[i]);
  return total;
}

function geojsonToRoute(geojson) {
  const feature = (geojson.features || []).find((f) => f.geometry && f.geometry.type === 'LineString');
  if (!feature) throw new Error('BRouter: prazdna odpoved');
  const coords = feature.geometry.coordinates.map(([lng, lat]) => ({ lat, lng }));
  const trackLength = feature.properties && feature.properties['track-length'];
  const distanceM = trackLength ? parseFloat(trackLength) : estimatePolylineLength(coords);
  return { coords, distanceM };
}

async function computeRouteBRouter(waypoints, profileId) {
  const lonlats = waypoints.map((w) => `${w.lng},${w.lat}`).join('|');
  const url = `https://brouter.de/brouter?lonlats=${lonlats}&profile=${profileId}&alternativeidx=0&format=geojson`;
  const res = await fetch(url);
  if (!res.ok) throw new Error(`BRouter vratil ${res.status}`);
  const geojson = await res.json();
  return geojsonToRoute(geojson);
}

async function computeRouteOSRM(waypoints) {
  const coords = waypoints.map((w) => `${w.lng},${w.lat}`).join(';');
  const url = `https://routing.openstreetmap.de/routed-bike/route/v1/bike/${coords}?overview=full&geometries=geojson`;
  const res = await fetch(url);
  if (!res.ok) throw new Error(`OSRM vratil ${res.status}`);
  const data = await res.json();
  if (!data.routes || !data.routes.length) throw new Error('OSRM nenasel trasu');
  const routeCoords = data.routes[0].geometry.coordinates.map(([lng, lat]) => ({ lat, lng }));
  return { coords: routeCoords, distanceM: data.routes[0].distance };
}

export async function computeRoute(waypoints, profileId = 'trekking') {
  if (!waypoints || waypoints.length < 2) throw new Error('Potreba alespon 2 body');
  try {
    return await computeRouteBRouter(waypoints, profileId);
  } catch (e) {
    return computeRouteOSRM(waypoints);
  }
}

// Redukce na max. N bodu pro BLE prenos - rovnomerne, krajni body zachovany.
export function reduceRouteForBle(coords, maxPoints = 280) {
  if (coords.length <= maxPoints) return coords;
  const result = [];
  const step = (coords.length - 1) / (maxPoints - 1);
  for (let i = 0; i < maxPoints; i++) {
    result.push(coords[Math.round(i * step)]);
  }
  return result;
}
