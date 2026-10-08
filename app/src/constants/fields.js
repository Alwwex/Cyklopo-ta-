// Musi odpovidat poradi v CFG:FIELDS protokolu a firmwarove fieldLabel() funkci.
export const FIELD_OPTIONS = [
  { id: 0, label: 'TRIP' },
  { id: 1, label: 'CAS' },
  { id: 2, label: 'PRUMER' },
  { id: 3, label: 'MAX' },
  { id: 4, label: 'VYSKA' },
  { id: 5, label: 'ODOMETR' },
  { id: 6, label: 'SATELITY' },
  { id: 7, label: 'HODINY' },
  { id: 8, label: 'STOUPANI' },
  { id: 9, label: 'SKLON' },
  { id: 10, label: 'TEP' },
  { id: 11, label: 'KADENCE' },
  { id: 12, label: 'BATERIE' },
  { id: 13, label: 'DO CILE' },
];

export function fieldLabelById(id) {
  const found = FIELD_OPTIONS.find((f) => f.id === id);
  return found ? found.label : '?';
}
