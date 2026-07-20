import React, { useEffect, useRef, useState, useCallback } from 'react';
import { View, Text, ScrollView, Modal, StyleSheet, Alert } from 'react-native';
import BleManager from '../ble/BleManager';
import { colors, spacing, typography } from '../theme';
import { PrimaryButton, SecondaryButton, StatTile, Chip, Panel } from '../components/UI';
import { FIELD_OPTIONS, fieldLabelById } from '../constants/fields';
import { loadFieldConfig, saveFieldConfig, addRide, loadRides, loadProfile, saveProfile } from '../storage/storage';
import { processFinishedRide } from '../gamification/gamification';

function formatTime(totalSeconds) {
  const s = Math.max(0, Math.round(totalSeconds));
  const h = Math.floor(s / 3600);
  const m = Math.floor((s % 3600) / 60);
  const ss = s % 60;
  if (h > 0) return `${h}:${String(m).padStart(2, '0')}:${String(ss).padStart(2, '0')}`;
  return `${m}:${String(ss).padStart(2, '0')}`;
}

export default function RideScreen() {
  const [connected, setConnected] = useState(BleManager.isConnected());
  const [connecting, setConnecting] = useState(false);
  const [telemetry, setTelemetry] = useState(null);
  const [fieldConfig, setFieldConfig] = useState([0, 1, 3, 4]);
  const [fieldModalVisible, setFieldModalVisible] = useState(false);
  const [pendingFieldConfig, setPendingFieldConfig] = useState([0, 1, 3, 4]);
  const [rewardModal, setRewardModal] = useState(null); // {points, badges}

  const prevRidRef = useRef(0);
  const startedByAppRef = useRef(false);
  const trackPointsRef = useRef([]);
  const maxKmhRef = useRef(0);
  const rideStatsRef = useRef({ distanceKm: 0, timeS: 0 });
  const rideStartedAtRef = useRef(null);

  useEffect(() => {
    loadFieldConfig().then((cfg) => { setFieldConfig(cfg); setPendingFieldConfig(cfg); });
  }, []);

  const finalizeAndSaveRide = useCallback(async () => {
    const stats = rideStatsRef.current;
    if (stats.distanceKm <= 0 && stats.timeS <= 0) return;
    const ride = {
      id: `${Date.now()}`,
      dateISO: (rideStartedAtRef.current || new Date()).toISOString(),
      distanceKm: stats.distanceKm,
      timeS: stats.timeS,
      avgKmh: stats.timeS > 0 ? stats.distanceKm / (stats.timeS / 3600) : 0,
      maxKmh: maxKmhRef.current,
      track: trackPointsRef.current,
    };
    const rides = await loadRides();
    const profile = await loadProfile();
    const result = processFinishedRide(profile, rides, ride);
    await addRide({ ...ride, points: result.pointsEarned });
    await saveProfile(result.profile);
    setRewardModal({ points: result.pointsEarned, badges: result.newBadges });
  }, []);

  const handleTelemetry = useCallback((t) => {
    setTelemetry(t);

    if (t.rid === 1) {
      if (prevRidRef.current === 0) {
        // nova jizda zacala (z appky nebo z BTN2 na ESP) - v obou pripadech
        // ESP uz vynuloval trip, appka jen zacne sledovat od nuly.
        trackPointsRef.current = [];
        maxKmhRef.current = 0;
        rideStartedAtRef.current = new Date();
        startedByAppRef.current = false;
      }
      rideStatsRef.current = { distanceKm: t.dst, timeS: t.tim };
      if (t.spd > maxKmhRef.current) maxKmhRef.current = t.spd;
      if (t.fix === 1) trackPointsRef.current.push({ lat: t.lat, lng: t.lng });
    } else if (prevRidRef.current === 1) {
      // jizda skoncila (STOP kdekoliv) - ulozit
      finalizeAndSaveRide();
    }
    prevRidRef.current = t.rid;
  }, [finalizeAndSaveRide]);

  useEffect(() => {
    const unsubTel = BleManager.onTelemetry(handleTelemetry);
    const unsubConn = BleManager.onConnectionChange(setConnected);
    return () => { unsubTel(); unsubConn(); };
  }, [handleTelemetry]);

  const onConnect = async () => {
    setConnecting(true);
    try {
      await BleManager.scanAndConnect();
    } catch (e) {
      Alert.alert('Pripojeni selhalo', e.message || String(e));
    } finally {
      setConnecting(false);
    }
  };

  const onStart = async () => {
    startedByAppRef.current = true;
    try { await BleManager.rideStart(); } catch (e) { Alert.alert('Chyba', e.message); }
  };
  const onStop = async () => {
    try { await BleManager.rideStop(); } catch (e) { Alert.alert('Chyba', e.message); }
  };
  const onPauseResume = async () => {
    try {
      if (telemetry && telemetry.pau === 1) await BleManager.rideResume();
      else await BleManager.ridePause();
    } catch (e) { Alert.alert('Chyba', e.message); }
  };

  const openFieldModal = () => { setPendingFieldConfig(fieldConfig); setFieldModalVisible(true); };
  const toggleFieldOption = (slotIdx, optionId) => {
    const next = [...pendingFieldConfig];
    next[slotIdx] = optionId;
    setPendingFieldConfig(next);
  };
  const saveFields = async () => {
    setFieldConfig(pendingFieldConfig);
    await saveFieldConfig(pendingFieldConfig);
    try { await BleManager.setFieldsConfig(pendingFieldConfig); } catch (e) { /* offline ulozeno lokalne */ }
    setFieldModalVisible(false);
  };

  const recording = telemetry ? telemetry.rid === 1 : false;
  const paused = telemetry ? telemetry.pau === 1 : false;
  const fix = telemetry ? telemetry.fix === 1 : false;
  const speed = telemetry ? telemetry.spd : 0;
  const suffix = recording ? '' : ' (computer)';

  const fieldValue = (idx) => {
    if (!telemetry) return '-';
    switch (idx) {
      case 0: return `${telemetry.dst.toFixed(2)} km`;
      case 1: return formatTime(telemetry.tim);
      case 2: {
        const hours = telemetry.tim / 3600;
        return hours > 0.01 ? `${(telemetry.dst / hours).toFixed(1)} km/h` : '0.0 km/h';
      }
      case 3: return `${maxKmhRef.current.toFixed(1)} km/h`;
      case 4: return `${telemetry.alt.toFixed(0)} m`;
      case 5: return '-';
      case 6: return `${telemetry.sat}`;
      default: return '-';
    }
  };

  return (
    <View style={styles.container}>
      <ScrollView contentContainerStyle={styles.scroll}>
        <View style={styles.statusRow}>
          <Text style={[styles.statusText, { color: connected ? colors.ble : colors.textDim }]}>
            {connected ? 'BLE PRIPOJENO' : 'BLE ODPOJENO'}
          </Text>
          {paused && <View style={styles.pauseBadge}><Text style={styles.pauseBadgeText}>PAUZA</Text></View>}
          <Text style={styles.statusText}>{fix ? `GPS ${telemetry.sat} sat` : 'GPS hledani...'}</Text>
          <Text style={styles.gear} onPress={openFieldModal}>{'⚙'}</Text>
        </View>

        <Text style={[typography.huge, { color: fix ? colors.accent : colors.textDim, textAlign: 'center' }]}>
          {speed.toFixed(1)}
        </Text>
        <Text style={[typography.label, { textAlign: 'center', marginBottom: spacing.lg }]}>km/h</Text>

        <View style={styles.grid}>
          <StatTile label={`Cas${suffix}`} value={telemetry ? formatTime(telemetry.tim) : '-'} />
          <StatTile label={`Vzdalenost${suffix}`} value={telemetry ? `${telemetry.dst.toFixed(2)} km` : '-'} />
          <StatTile label="Prumer" value={fieldValue(2)} />
          <StatTile label="Max" value={fieldValue(3)} />
          <StatTile label="Vyska" value={telemetry ? `${telemetry.alt.toFixed(0)} m` : '-'} />
          <StatTile label="Satelity" value={telemetry ? `${telemetry.sat}` : '-'} />
        </View>

        <View style={styles.actions}>
          {!connected && (
            <PrimaryButton title={connecting ? 'PRIPOJUJI...' : 'PRIPOJIT'} onPress={onConnect} disabled={connecting} />
          )}
          {connected && !recording && (
            <PrimaryButton title="▶ ZACIT JIZDU" onPress={onStart} />
          )}
          {connected && recording && (
            <View style={styles.rowButtons}>
              <View style={styles.flex1}>
                <SecondaryButton title={paused ? '▶ POKRACOVAT' : '❚❚ PAUZA'} onPress={onPauseResume} />
              </View>
              <View style={{ width: spacing.md }} />
              <View style={styles.flex1}>
                <PrimaryButton title="■ UKONCIT" onPress={onStop} color={colors.danger} />
              </View>
            </View>
          )}
        </View>
      </ScrollView>

      <Modal visible={fieldModalVisible} transparent animationType="slide">
        <View style={styles.modalBackdrop}>
          <Panel style={styles.modalCard}>
            <Text style={typography.title}>Pole na displeji</Text>
            {[0, 1, 2, 3].map((slot) => (
              <View key={slot} style={styles.slotRow}>
                <Text style={styles.slotLabel}>Slot {slot + 1}: {fieldLabelById(pendingFieldConfig[slot])}</Text>
                <View style={styles.chipsWrap}>
                  {FIELD_OPTIONS.map((opt) => (
                    <Chip
                      key={opt.id}
                      label={opt.label}
                      selected={pendingFieldConfig[slot] === opt.id}
                      onPress={() => toggleFieldOption(slot, opt.id)}
                    />
                  ))}
                </View>
              </View>
            ))}
            <View style={{ height: spacing.md }} />
            <PrimaryButton title="ULOZIT" onPress={saveFields} />
            <View style={{ height: spacing.sm }} />
            <SecondaryButton title="ZRUSIT" onPress={() => setFieldModalVisible(false)} />
          </Panel>
        </View>
      </Modal>

      <Modal visible={!!rewardModal} transparent animationType="fade">
        <View style={styles.modalBackdrop}>
          <Panel style={styles.modalCard}>
            <Text style={typography.title}>Jizda ulozena!</Text>
            <Text style={[typography.big, { color: colors.accent, marginTop: spacing.md }]}>
              +{rewardModal ? rewardModal.points : 0} bodu
            </Text>
            {rewardModal && rewardModal.badges.length > 0 && (
              <View style={{ marginTop: spacing.md }}>
                <Text style={typography.label}>Nove odznaky</Text>
                {rewardModal.badges.map((b) => (
                  <Text key={b.id} style={[typography.body, { marginTop: spacing.xs }]}>{'⭐'} {b.label}</Text>
                ))}
              </View>
            )}
            <View style={{ height: spacing.md }} />
            <PrimaryButton title="OK" onPress={() => setRewardModal(null)} />
          </Panel>
        </View>
      </Modal>
    </View>
  );
}

const styles = StyleSheet.create({
  container: { flex: 1, backgroundColor: colors.bg },
  scroll: { padding: spacing.md },
  statusRow: { flexDirection: 'row', alignItems: 'center', marginBottom: spacing.lg, flexWrap: 'wrap' },
  statusText: { color: colors.textMuted, fontSize: 12, fontWeight: '600', marginRight: spacing.md },
  pauseBadge: { backgroundColor: colors.warn, borderRadius: 6, paddingHorizontal: 8, paddingVertical: 2, marginRight: spacing.md },
  pauseBadgeText: { color: '#0B0F12', fontWeight: '700', fontSize: 11 },
  gear: { marginLeft: 'auto', fontSize: 22, color: colors.textMuted },
  grid: { flexDirection: 'row', flexWrap: 'wrap', justifyContent: 'space-between' },
  actions: { marginTop: spacing.md },
  rowButtons: { flexDirection: 'row' },
  flex1: { flex: 1 },
  modalBackdrop: { flex: 1, backgroundColor: 'rgba(0,0,0,0.6)', justifyContent: 'flex-end' },
  modalCard: { borderTopLeftRadius: 18, borderTopRightRadius: 18, padding: spacing.lg },
  slotRow: { marginTop: spacing.md },
  slotLabel: { color: colors.text, fontWeight: '600', marginBottom: spacing.xs },
  chipsWrap: { flexDirection: 'row', flexWrap: 'wrap' },
});
