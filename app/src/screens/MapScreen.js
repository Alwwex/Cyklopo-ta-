import React, { useEffect, useRef, useState, useCallback } from 'react';
import { View, Text, StyleSheet, Alert, ActivityIndicator } from 'react-native';
import MapView, { Marker, Polyline, PROVIDER_GOOGLE } from 'react-native-maps';
import BleManager from '../ble/BleManager';
import { colors, spacing } from '../theme';
import { Chip, SecondaryButton, PrimaryButton } from '../components/UI';
import { computeRoute, reduceRouteForBle, ROUTE_PROFILES } from '../services/routing';
import { fetchStreetsForRoute, flattenSegmentsForBle } from '../services/overpass';
import { loadPlannedRoute, savePlannedRoute } from '../storage/storage';

const DEBOUNCE_MS = 700;

export default function MapScreen() {
  const mapRef = useRef(null);
  const [waypoints, setWaypoints] = useState([]);
  const [routeCoords, setRouteCoords] = useState([]);
  const [routeDistanceM, setRouteDistanceM] = useState(0);
  const [profile, setProfile] = useState('trekking');
  const [planningMode, setPlanningMode] = useState(false);
  const [computing, setComputing] = useState(false);
  const [sending, setSending] = useState(false);
  const [riddenTrack, setRiddenTrack] = useState([]);
  const [currentPos, setCurrentPos] = useState(null);

  const debounceTimer = useRef(null);
  const prevRidRef = useRef(0);

  useEffect(() => {
    loadPlannedRoute().then((plan) => {
      if (plan.waypoints) setWaypoints(plan.waypoints);
      if (plan.routeCoords) setRouteCoords(plan.routeCoords);
    });
  }, []);

  useEffect(() => {
    const unsub = BleManager.onTelemetry((t) => {
      if (t.fix === 1) {
        setCurrentPos({ lat: t.lat, lng: t.lng });
        if (!planningMode && mapRef.current) {
          mapRef.current.animateCamera({ center: { latitude: t.lat, longitude: t.lng } }, { duration: 500 });
        }
      }
      if (t.rid === 1) {
        if (prevRidRef.current === 0) setRiddenTrack([]);
        if (t.fix === 1) setRiddenTrack((prev) => [...prev, { lat: t.lat, lng: t.lng }]);
      }
      prevRidRef.current = t.rid;
    });
    return unsub;
  }, [planningMode]);

  const recompute = useCallback(async (points, prof) => {
    if (points.length < 2) { setRouteCoords([]); setRouteDistanceM(0); return; }
    setComputing(true);
    try {
      const result = await computeRoute(points, prof);
      setRouteCoords(result.coords);
      setRouteDistanceM(result.distanceM);
      await savePlannedRoute({ waypoints: points, routeCoords: result.coords, streets: [] });
    } catch (e) {
      Alert.alert('Vypocet trasy selhal', e.message || String(e));
    } finally {
      setComputing(false);
    }
  }, []);

  useEffect(() => {
    if (debounceTimer.current) clearTimeout(debounceTimer.current);
    debounceTimer.current = setTimeout(() => {
      recompute(waypoints, profile);
    }, DEBOUNCE_MS);
    return () => clearTimeout(debounceTimer.current);
  }, [waypoints, profile, recompute]);

  const handleMapPress = (event) => {
    if (!planningMode) return;
    // POZOR: React synteticke eventy se recykluji - souradnici je nutne
    // vytahnout HNED tady, ne az uvnitr setState updateru.
    const { coordinate } = event.nativeEvent;
    setWaypoints((prev) => [...prev, { lat: coordinate.latitude, lng: coordinate.longitude }]);
  };

  const undoWaypoint = () => setWaypoints((prev) => prev.slice(0, -1));

  const clearAll = async () => {
    setWaypoints([]);
    setRouteCoords([]);
    setRouteDistanceM(0);
    await savePlannedRoute({ waypoints: [], routeCoords: [], streets: [] });
    try { await BleManager.clearRoute(); } catch (e) { /* offline */ }
  };

  const sendToEsp = async () => {
    if (routeCoords.length < 2) {
      Alert.alert('Zadna trasa', 'Nejdriv naklikej alespon dva body.');
      return;
    }
    setSending(true);
    try {
      const reduced = reduceRouteForBle(routeCoords, 280);
      await BleManager.sendRoute(reduced);
      const segments = await fetchStreetsForRoute(routeCoords);
      const flat = flattenSegmentsForBle(segments);
      await BleManager.sendStreets(flat);
      Alert.alert('Hotovo', 'Trasa a ulice odeslany do CykloComp.');
    } catch (e) {
      Alert.alert('Odeslani selhalo', e.message || String(e));
    } finally {
      setSending(false);
    }
  };

  const routeCoordsForMap = routeCoords.map((p) => ({ latitude: p.lat, longitude: p.lng }));
  const riddenCoordsForMap = riddenTrack.map((p) => ({ latitude: p.lat, longitude: p.lng }));

  return (
    <View style={styles.container}>
      <MapView
        ref={mapRef}
        style={styles.map}
        provider={PROVIDER_GOOGLE}
        onPress={handleMapPress}
        initialRegion={{
          latitude: currentPos ? currentPos.lat : 50.0755,
          longitude: currentPos ? currentPos.lng : 14.4378,
          latitudeDelta: 0.05,
          longitudeDelta: 0.05,
        }}
      >
        {waypoints.map((w, i) => (
          <Marker key={`wp-${i}`} coordinate={{ latitude: w.lat, longitude: w.lng }} pinColor={i === 0 ? 'green' : (i === waypoints.length - 1 ? 'black' : 'orange')} />
        ))}
        {routeCoordsForMap.length > 1 && (
          <Polyline coordinates={routeCoordsForMap} strokeColor="#1E5ADC" strokeWidth={4} />
        )}
        {riddenCoordsForMap.length > 1 && (
          <Polyline coordinates={riddenCoordsForMap} strokeColor={colors.accent} strokeWidth={4} />
        )}
      </MapView>

      <View style={styles.overlayTop}>
        {computing && <ActivityIndicator color={colors.accent} />}
        <Text style={styles.distanceText}>
          {routeDistanceM > 0 ? `Trasa: ${(routeDistanceM / 1000).toFixed(1)} km` : ''}
        </Text>
        <View style={styles.chipsRow}>
          {ROUTE_PROFILES.map((p) => (
            <Chip key={p.id} label={p.label} selected={profile === p.id} onPress={() => setProfile(p.id)} />
          ))}
        </View>
      </View>

      <View style={styles.toolbar}>
        <View style={styles.toolbarBtn}>
          <SecondaryButton title={planningMode ? 'Hotovo' : 'Planovat'} onPress={() => setPlanningMode((v) => !v)} />
        </View>
        <View style={styles.toolbarBtn}>
          <SecondaryButton title="Zpet" onPress={undoWaypoint} disabled={waypoints.length === 0} />
        </View>
        <View style={styles.toolbarBtn}>
          <SecondaryButton title="Smazat" onPress={clearAll} disabled={waypoints.length === 0} />
        </View>
        <View style={styles.toolbarBtn}>
          <PrimaryButton title={sending ? '...' : '→ Do ESP'} onPress={sendToEsp} disabled={sending || routeCoords.length < 2} />
        </View>
      </View>
    </View>
  );
}

const styles = StyleSheet.create({
  container: { flex: 1, backgroundColor: colors.bg },
  map: { flex: 1 },
  overlayTop: {
    position: 'absolute',
    top: spacing.md,
    left: spacing.md,
    right: spacing.md,
    backgroundColor: 'rgba(11,15,18,0.85)',
    borderRadius: 12,
    padding: spacing.sm,
  },
  distanceText: { color: colors.text, fontWeight: '700', marginBottom: spacing.xs },
  chipsRow: { flexDirection: 'row', flexWrap: 'wrap' },
  toolbar: {
    position: 'absolute',
    bottom: spacing.md,
    left: spacing.md,
    right: spacing.md,
    flexDirection: 'row',
  },
  toolbarBtn: { flex: 1, marginHorizontal: 4 },
});
