export const colors = {
  bg: '#0B0F12',
  panel: '#161C22',
  panelAlt: '#1D242B',
  border: '#2A333B',
  accent: '#FF7A1A',
  ok: '#3CC85A',
  warn: '#FFD228',
  danger: '#E63B3B',
  ble: '#3C8CFF',
  text: '#F2F4F6',
  textMuted: '#8A94A0',
  textDim: '#5A6470',
};

export const spacing = {
  xs: 4,
  sm: 8,
  md: 16,
  lg: 24,
  xl: 32,
};

export const radius = {
  sm: 6,
  md: 12,
  lg: 18,
};

export const typography = {
  huge: {
    fontSize: 64,
    fontWeight: '800',
    color: colors.text,
    fontVariant: ['tabular-nums'],
  },
  big: {
    fontSize: 34,
    fontWeight: '700',
    color: colors.text,
    fontVariant: ['tabular-nums'],
  },
  value: {
    fontSize: 20,
    fontWeight: '600',
    color: colors.text,
    fontVariant: ['tabular-nums'],
  },
  label: {
    fontSize: 11,
    fontWeight: '600',
    color: colors.textMuted,
    letterSpacing: 0.8,
    textTransform: 'uppercase',
  },
  title: {
    fontSize: 18,
    fontWeight: '700',
    color: colors.text,
  },
  body: {
    fontSize: 14,
    fontWeight: '400',
    color: colors.text,
  },
};

const theme = { colors, spacing, radius, typography };
export default theme;
