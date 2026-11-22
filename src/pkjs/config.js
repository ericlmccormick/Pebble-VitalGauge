// Clay page bundled into the Pebble phone app. No website or cloud required.
module.exports = [
  { type: 'heading', defaultValue: 'VitalGauge Settings' },
  {type: 'section', items: [
    {type: 'heading', defaultValue: 'Appearance'},
    {type: 'select', messageKey: 'VG_DARK_MODE', label: 'Theme', defaultValue: '2',
      description: 'Auto follows sunrise and sunset. Without saved times, Day runs from 7 a.m. to 7 p.m.',
      options: [
        {label: 'Day', value: '0'},
        {label: 'Night', value: '1'},
        {label: 'Auto', value: '2'}
      ]}
  ]},
  { type: 'text', defaultValue: 'These options also remain available on the watch by holding Select.' },
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
    { type: 'select', messageKey: 'VG_BACKLIGHT', label: 'Scan Backlight', defaultValue: '0',
      options: [{label: 'Auto', value: '0'}, {label: 'Stay On', value: '1'}] }
  ] },
  { type: 'section', items: [
    { type: 'heading', defaultValue: 'Monitoring' },
    { type: 'toggle', messageKey: 'BG_SCAN_ENABLED', label: 'Background monitoring', defaultValue: false },
    { type: 'toggle', messageKey: 'VG_ALERTS', label: 'Experimental stress prompts (requires background monitoring)', defaultValue: false }
  ] },
  { type: 'text', defaultValue: 'VitalGauge health processing stays entirely on your Pebble Time 2.' },
  { type: 'submit', defaultValue: 'Save Settings' }
];
