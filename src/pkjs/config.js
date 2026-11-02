// Clay page bundled into the Pebble phone app. No website or cloud required.
module.exports = [
  { type: 'heading', defaultValue: 'VitalGauge Settings' },
  { type: 'text', defaultValue: 'These options also remain available on the watch by holding Select.' },
  { type: 'section', items: [
    { type: 'heading', defaultValue: 'Appearance' },
    { type: 'toggle', messageKey: 'VG_DARK_MODE', label: 'Dark mode', defaultValue: false }
  ] },
  { type: 'section', items: [
    { type: 'heading', defaultValue: 'Breathing & Feedback' },
    { type: 'select', messageKey: 'VG_BREATH_STYLE', label: 'Breathing pattern', defaultValue: '0',
      options: [
        { label: 'Box (4-4-4-4)', value: '0' },
        { label: 'Relax (4-7-8)', value: '1' },
        { label: 'Sleep (4-8)', value: '2' }
      ] },
    { type: 'select', messageKey: 'VG_HAPTIC_LEVEL', label: 'Haptic feedback', defaultValue: '2',
      options: [
        { label: 'Off', value: '0' },
        { label: 'Low', value: '1' },
        { label: 'High', value: '2' }
      ] },
    { type: 'toggle', messageKey: 'VG_BACKLIGHT', label: 'Keep backlight on during scans', defaultValue: false }
  ] },
  { type: 'text', defaultValue: 'VitalGauge health processing stays entirely on your Pebble Time 2.' },
  { type: 'submit', defaultValue: 'Save Settings' }
];
