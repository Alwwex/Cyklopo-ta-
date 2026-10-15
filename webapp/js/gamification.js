// Body, levely, streak (Duolingo styl) a odznaky - ciste funkce bez UI.

export const BADGES = [
  { id: 'first_ride', icon: '🚲', label: 'První jízda', check: (c) => c.rides.length >= 1 },
  { id: 'ride_20km', icon: '🛣️', label: '20 km v kuse', check: (c) => c.rides.some((r) => r.distanceKm >= 20) },
  { id: 'ride_50km', icon: '🏁', label: '50 km v kuse', check: (c) => c.rides.some((r) => r.distanceKm >= 50) },
  { id: 'ride_100km', icon: '💯', label: 'Stovka', check: (c) => c.rides.some((r) => r.distanceKm >= 100) },
  { id: 'speed_40', icon: '⚡', label: 'Rychlost 40', check: (c) => c.rides.some((r) => r.maxKmh >= 40) },
  { id: 'speed_60', icon: '🚀', label: 'Rychlost 60', check: (c) => c.rides.some((r) => r.maxKmh >= 60) },
  { id: 'climb_500', icon: '⛰️', label: 'Výšlap 500 m', check: (c) => c.rides.some((r) => (r.ascentM || 0) >= 500) },
  { id: 'climb_1000', icon: '🏔️', label: 'Výšlap 1000 m', check: (c) => c.rides.some((r) => (r.ascentM || 0) >= 1000) },
  { id: 'total_100km', icon: '🥉', label: 'Celkem 100 km', check: (c) => c.profile.totalKm >= 100 },
  { id: 'total_500km', icon: '🥈', label: 'Celkem 500 km', check: (c) => c.profile.totalKm >= 500 },
  { id: 'total_2000km', icon: '🥇', label: 'Celkem 2000 km', check: (c) => c.profile.totalKm >= 2000 },
  { id: 'streak_3', icon: '🔥', label: 'Streak 3 dny', check: (c) => c.profile.streak >= 3 },
  { id: 'streak_7', icon: '🔥', label: 'Streak 7 dní', check: (c) => c.profile.streak >= 7 },
  { id: 'early_bird', icon: '🌅', label: 'Ranní ptáče', check: (c) => c.rides.some((r) => new Date(r.dateISO).getHours() < 7) },
  { id: 'points_1000', icon: '⭐', label: '1000 bodů', check: (c) => c.profile.points >= 1000 },
];

export function computeRidePoints(ride) {
  let p = ride.distanceKm * 10 + (ride.timeS / 60) + (ride.ascentM || 0) * 0.5;
  if (ride.distanceKm >= 20) p += 50;
  if (ride.distanceKm >= 50) p += 150;
  if (ride.distanceKm >= 100) p += 300;
  if (ride.maxKmh >= 40) p += 25;
  return Math.round(p);
}

export function computeLevel(points) {
  return Math.floor(Math.sqrt(Math.max(points, 0) / 250)) + 1;
}

export function levelProgress(points) {
  const level = computeLevel(points);
  const start = (level - 1) ** 2 * 250;
  const end = level ** 2 * 250;
  const progress = end > start ? (points - start) / (end - start) : 1;
  return { level, start, end, progress: Math.min(Math.max(progress, 0), 1) };
}

const dayISO = (d) => {
  const x = new Date(d);
  return `${x.getFullYear()}-${String(x.getMonth() + 1).padStart(2, '0')}-${String(x.getDate()).padStart(2, '0')}`;
};
const daysBetween = (a, b) => Math.round((new Date(`${b}T00:00:00`) - new Date(`${a}T00:00:00`)) / 86400000);

// pri startu appky: vynechany den = streak 0
export function checkStreakExpiry(profile, now = new Date()) {
  if (!profile.lastRideDay) return profile;
  return daysBetween(profile.lastRideDay, dayISO(now)) >= 2 ? { ...profile, streak: 0 } : profile;
}

function applyRideToStreak(profile, rideDay) {
  if (profile.lastRideDay === rideDay) return profile;
  if (profile.lastRideDay && daysBetween(rideDay, profile.lastRideDay) > 0) return profile; // starsi jizda (sync)
  const consecutive = profile.lastRideDay && daysBetween(profile.lastRideDay, rideDay) === 1;
  return { ...profile, streak: consecutive ? profile.streak + 1 : 1, lastRideDay: rideDay };
}

export function processFinishedRide(profile, rides, ride) {
  const points = computeRidePoints(ride);
  let next = { ...profile, points: profile.points + points, totalKm: profile.totalKm + ride.distanceKm, rides: (profile.rides || 0) + 1 };
  if (ride.distanceKm >= 1) next = applyRideToStreak(next, dayISO(ride.dateISO));
  const unlocked = new Set(next.badges || []);
  const ctx = { profile: next, rides: [...rides, ride] };
  const newBadges = BADGES.filter((b) => !unlocked.has(b.id) && b.check(ctx));
  newBadges.forEach((b) => unlocked.add(b.id));
  next = { ...next, badges: [...unlocked] };
  return { profile: next, pointsEarned: points, newBadges };
}

export function defaultProfile() {
  return { points: 0, streak: 0, lastRideDay: null, totalKm: 0, rides: 0, badges: [], weeklyGoalKm: 50 };
}
