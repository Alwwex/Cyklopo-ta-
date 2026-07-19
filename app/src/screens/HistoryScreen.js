import React, { useCallback, useState } from 'react';
import { View, Text, FlatList, StyleSheet } from 'react-native';
import { useFocusEffect } from '@react-navigation/native';
import { colors, spacing, typography, radius } from '../theme';
import { loadRides } from '../storage/storage';

function formatTime(totalSeconds) {
  const s = Math.max(0, Math.round(totalSeconds));
  const h = Math.floor(s / 3600);
  const m = Math.floor((s % 3600) / 60);
  const ss = s % 60;
  if (h > 0) return `${h}:${String(m).padStart(2, '0')}:${String(ss).padStart(2, '0')}`;
  return `${m}:${String(ss).padStart(2, '0')}`;
}

function RideCard({ ride }) {
  const date = new Date(ride.dateISO);
  return (
    <View style={styles.card}>
      <View style={styles.cardHeader}>
        <Text style={styles.cardDate}>{date.toLocaleDateString('cs-CZ')}</Text>
        <Text style={styles.cardPoints}>+{ride.points} b.</Text>
      </View>
      <View style={styles.cardRow}>
        <View style={styles.cardStat}>
          <Text style={typography.label}>Vzdalenost</Text>
          <Text style={typography.value}>{ride.distanceKm.toFixed(2)} km</Text>
        </View>
        <View style={styles.cardStat}>
          <Text style={typography.label}>Cas</Text>
          <Text style={typography.value}>{formatTime(ride.timeS)}</Text>
        </View>
      </View>
      <View style={styles.cardRow}>
        <View style={styles.cardStat}>
          <Text style={typography.label}>Prumer</Text>
          <Text style={typography.value}>{ride.avgKmh.toFixed(1)} km/h</Text>
        </View>
        <View style={styles.cardStat}>
          <Text style={typography.label}>Max</Text>
          <Text style={typography.value}>{ride.maxKmh.toFixed(1)} km/h</Text>
        </View>
      </View>
    </View>
  );
}

export default function HistoryScreen() {
  const [rides, setRides] = useState([]);

  useFocusEffect(
    useCallback(() => {
      loadRides().then(setRides);
    }, []),
  );

  return (
    <View style={styles.container}>
      <FlatList
        data={rides}
        keyExtractor={(item) => item.id}
        contentContainerStyle={styles.list}
        renderItem={({ item }) => <RideCard ride={item} />}
        ListEmptyComponent={<Text style={styles.empty}>Zatim zadne jizdy</Text>}
      />
    </View>
  );
}

const styles = StyleSheet.create({
  container: { flex: 1, backgroundColor: colors.bg },
  list: { padding: spacing.md },
  card: {
    backgroundColor: colors.panel,
    borderRadius: radius.md,
    borderWidth: 1,
    borderColor: colors.border,
    padding: spacing.md,
    marginBottom: spacing.md,
  },
  cardHeader: { flexDirection: 'row', justifyContent: 'space-between', marginBottom: spacing.sm },
  cardDate: { color: colors.textMuted, fontWeight: '600' },
  cardPoints: { color: colors.accent, fontWeight: '700' },
  cardRow: { flexDirection: 'row', justifyContent: 'space-between', marginTop: spacing.xs },
  cardStat: { flex: 1 },
  empty: { color: colors.textDim, textAlign: 'center', marginTop: spacing.xl },
});
