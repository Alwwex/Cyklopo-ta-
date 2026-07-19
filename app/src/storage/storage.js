import AsyncStorage from '@react-native-async-storage/async-storage';
import { defaultProfile } from '../gamification/gamification';

const KEY_RIDES = '@cyklocomp/rides';
const KEY_PROFILE = '@cyklocomp/profile';
const KEY_FIELD_CONFIG = '@cyklocomp/fieldConfig';
const KEY_PLANNED_ROUTE = '@cyklocomp/plannedRoute';

export async function loadRides() {
  const raw = await AsyncStorage.getItem(KEY_RIDES);
  return raw ? JSON.parse(raw) : [];
}

export async function saveRides(rides) {
  await AsyncStorage.setItem(KEY_RIDES, JSON.stringify(rides));
}

export async function addRide(ride) {
  const rides = await loadRides();
  const next = [ride, ...rides];
  await saveRides(next);
  return next;
}

export async function loadProfile() {
  const raw = await AsyncStorage.getItem(KEY_PROFILE);
  return raw ? JSON.parse(raw) : defaultProfile();
}

export async function saveProfile(profile) {
  await AsyncStorage.setItem(KEY_PROFILE, JSON.stringify(profile));
}

export async function loadFieldConfig() {
  const raw = await AsyncStorage.getItem(KEY_FIELD_CONFIG);
  return raw ? JSON.parse(raw) : [0, 1, 3, 4];
}

export async function saveFieldConfig(indices) {
  await AsyncStorage.setItem(KEY_FIELD_CONFIG, JSON.stringify(indices));
}

export async function loadPlannedRoute() {
  const raw = await AsyncStorage.getItem(KEY_PLANNED_ROUTE);
  return raw ? JSON.parse(raw) : { waypoints: [], routeCoords: [], streets: [] };
}

export async function savePlannedRoute(plan) {
  await AsyncStorage.setItem(KEY_PLANNED_ROUTE, JSON.stringify(plan));
}
