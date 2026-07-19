// Ciste funkce (bez UI) pro body, levely, streak a odznaky.

export const BADGES = [
  { id: 'first_ride', label: 'Prvni jizda', check: (ctx) => ctx.rides.length >= 1 },
  { id: 'ride_20km', label: '20 km v jedne jizde', check: (ctx) => ctx.rides.some((r) => r.distanceKm >= 20) },
  { id: 'ride_50km', label: '50 km v jedne jizde', check: (ctx) => ctx.rides.some((r) => r.distanceKm >= 50) },
  { id: 'speed_40', label: 'Rychlost 40 km/h', check: (ctx) => ctx.rides.some((r) => r.maxKmh >= 40) },
  { id: 'total_100km', label: 'Celkem 100 km', check: (ctx) => ctx.profile.totalKm >= 100 },
  { id: 'total_500km', label: 'Celkem 500 km', check: (ctx) => ctx.profile.totalKm >= 500 },
  { id: 'streak_3', label: 'Streak 3 dny', check: (ctx) => ctx.profile.streak >= 3 },
  { id: 'streak_7', label: 'Streak 7 dni', check: (ctx) => ctx.profile.streak >= 7 },
  { id: 'points_1000', label: '1000 bodu', check: (ctx) => ctx.profile.points >= 1000 },
];

export function computeRidePoints(ride) {
  let points = ride.distanceKm * 10 + (ride.timeS / 60) * 1;
  if (ride.distanceKm >= 20) points += 50;
  if (ride.distanceKm >= 50) points += 150;
  if (ride.maxKmh >= 40) points += 25;
  return Math.round(points);
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

function toDayISO(date) {
  return date.toISOString().slice(0, 10);
}

function daysBetween(aISO, bISO) {
  const a = new Date(`${aISO}T00:00:00Z`);
  const b = new Date(`${bISO}T00:00:00Z`);
  return Math.round((b - a) / 86400000);
}

// Volat pri startu appky - Duolingo styl: vynechany den = reset streaku na 0.
export function checkStreakExpiry(profile, now = new Date()) {
  if (!profile.lastRideDateISO) return profile;
  const today = toDayISO(now);
  const gap = daysBetween(profile.lastRideDateISO, today);
  if (gap >= 2) {
    return { ...profile, streak: 0 };
  }
  return profile;
}

// Volat po ulozeni jizdy s distanceKm >= 1.
export function applyRideToStreak(profile, rideDateISO) {
  if (profile.lastRideDateISO === rideDateISO) return profile; // uz zapocitano dnes
  const isConsecutive = profile.lastRideDateISO && daysBetween(profile.lastRideDateISO, rideDateISO) === 1;
  const streak = isConsecutive ? profile.streak + 1 : 1;
  return { ...profile, streak, lastRideDateISO: rideDateISO };
}

export function evaluateNewBadges(profile, rides) {
  const ctx = { profile, rides };
  const unlocked = new Set(profile.badges || []);
  const newlyUnlocked = [];
  for (const badge of BADGES) {
    if (!unlocked.has(badge.id) && badge.check(ctx)) {
      unlocked.add(badge.id);
      newlyUnlocked.push(badge);
    }
  }
  return { badges: Array.from(unlocked), newlyUnlocked };
}

// Zpracuje ulozenou jizdu: body, streak, odznaky. Vraci novy profil + info pro odmenovy modal.
export function processFinishedRide(profile, rides, ride) {
  const points = computeRidePoints(ride);
  let nextProfile = {
    ...profile,
    points: profile.points + points,
    totalKm: profile.totalKm + ride.distanceKm,
  };
  if (ride.distanceKm >= 1) {
    const rideDateISO = ride.dateISO.slice(0, 10);
    nextProfile = applyRideToStreak(nextProfile, rideDateISO);
  }
  const { badges, newlyUnlocked } = evaluateNewBadges(nextProfile, [...rides, ride]);
  nextProfile = { ...nextProfile, badges };
  return { profile: nextProfile, pointsEarned: points, newBadges: newlyUnlocked };
}

export function defaultProfile() {
  return {
    points: 0,
    streak: 0,
    lastRideDateISO: null,
    totalKm: 0,
    badges: [],
    weeklyGoalKm: 50,
  };
}
