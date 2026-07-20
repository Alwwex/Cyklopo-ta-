import React, { useCallback, useState } from 'react';
import { View, Text, ScrollView, StyleSheet } from 'react-native';
import { useFocusEffect } from '@react-navigation/native';
import { colors, spacing, typography, radius } from '../theme';
import { loadProfile, saveProfile, loadRides } from '../storage/storage';
import { checkStreakExpiry, levelProgress, BADGES, defaultProfile } from '../gamification/gamification';

function startOfWeek(date) {
  const d = new Date(date);
  const day = (d.getDay() + 6) % 7; // pondeli = 0
  d.setDate(d.getDate() - day);
  d.setHours(0, 0, 0, 0);
  return d;
}

function ProgressBar({ progress, color }) {
  return (
    <View style={styles.progressTrack}>
      <View style={[styles.progressFill, { width: `${Math.round(progress * 100)}%`, backgroundColor: color }]} />
    </View>
  );
}

export default function ProfileScreen() {
  const [profile, setProfile] = useState(defaultProfile());
  const [weeklyKm, setWeeklyKm] = useState(0);

  useFocusEffect(
    useCallback(() => {
      let cancelled = false;
      (async () => {
        const loaded = await loadProfile();
        const checked = checkStreakExpiry(loaded);
        if (checked !== loaded) await saveProfile(checked);
        const rides = await loadRides();
        const weekStart = startOfWeek(new Date());
        const kmThisWeek = rides
          .filter((r) => new Date(r.dateISO) >= weekStart)
          .reduce((sum, r) => sum + r.distanceKm, 0);
        if (!cancelled) {
          setProfile(checked);
          setWeeklyKm(kmThisWeek);
        }
      })();
      return () => { cancelled = true; };
    }, []),
  );

  const { level, progress } = levelProgress(profile.points);
  const weeklyProgress = profile.weeklyGoalKm > 0 ? Math.min(weeklyKm / profile.weeklyGoalKm, 1) : 0;

  return (
    <ScrollView style={styles.container} contentContainerStyle={styles.content}>
      <View style={styles.headerRow}>
        <View>
          <Text style={typography.label}>Streak</Text>
          <Text style={[typography.big, { color: colors.warn }]}>{'🔥'} {profile.streak}</Text>
        </View>
        <View style={styles.levelBox}>
          <Text style={typography.label}>Level {level}</Text>
          <ProgressBar progress={progress} color={colors.accent} />
        </View>
      </View>

      <View style={styles.panel}>
        <Text style={typography.label}>Tydenni cil</Text>
        <Text style={[typography.value, { marginTop: spacing.xs }]}>{weeklyKm.toFixed(1)} / {profile.weeklyGoalKm} km</Text>
        <ProgressBar progress={weeklyProgress} color={colors.ok} />
      </View>

      <View style={styles.statsRow}>
        <View style={styles.panel}>
          <Text style={typography.label}>Celkem km</Text>
          <Text style={typography.value}>{profile.totalKm.toFixed(1)}</Text>
        </View>
        <View style={{ width: spacing.md }} />
        <View style={styles.panel}>
          <Text style={typography.label}>Celkem bodu</Text>
          <Text style={typography.value}>{profile.points}</Text>
        </View>
      </View>

      <Text style={[typography.title, { marginTop: spacing.lg, marginBottom: spacing.sm }]}>Odznaky</Text>
      <View style={styles.badgeGrid}>
        {BADGES.map((b) => {
          const unlocked = profile.badges.includes(b.id);
          return (
            <View key={b.id} style={[styles.badge, !unlocked && styles.badgeLocked]}>
              <Text style={{ fontSize: 22 }}>{unlocked ? '🏅' : '🔒'}</Text>
              <Text style={[styles.badgeLabel, !unlocked && styles.badgeLabelLocked]}>{b.label}</Text>
            </View>
          );
        })}
      </View>
    </ScrollView>
  );
}

const styles = StyleSheet.create({
  container: { flex: 1, backgroundColor: colors.bg },
  content: { padding: spacing.md },
  headerRow: { flexDirection: 'row', justifyContent: 'space-between', marginBottom: spacing.md },
  levelBox: { flex: 1, marginLeft: spacing.lg },
  panel: {
    backgroundColor: colors.panel,
    borderRadius: radius.md,
    borderWidth: 1,
    borderColor: colors.border,
    padding: spacing.md,
    flex: 1,
    marginBottom: spacing.md,
  },
  statsRow: { flexDirection: 'row' },
  progressTrack: { height: 8, borderRadius: 4, backgroundColor: colors.panelAlt, marginTop: spacing.sm, overflow: 'hidden' },
  progressFill: { height: 8, borderRadius: 4 },
  badgeGrid: { flexDirection: 'row', flexWrap: 'wrap' },
  badge: {
    width: '31%',
    marginRight: '3.5%',
    marginBottom: spacing.md,
    alignItems: 'center',
    backgroundColor: colors.panel,
    borderRadius: radius.md,
    borderWidth: 1,
    borderColor: colors.border,
    paddingVertical: spacing.md,
  },
  badgeLocked: { opacity: 0.45 },
  badgeLabel: { color: colors.text, fontSize: 10, textAlign: 'center', marginTop: spacing.xs },
  badgeLabelLocked: { color: colors.textDim },
});
