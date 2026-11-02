var Clay = require('pebble-clay');
var keys = require('message_keys');
var baseConfig = require('./config');
var clay = new Clay(baseConfig, null, { autoHandleEvents: false });
var fields = ['VG_DARK_MODE', 'VG_BREATH_STYLE', 'VG_HAPTIC_LEVEL', 'VG_BACKLIGHT'];
var defaults = { VG_DARK_MODE: 0, VG_BREATH_STYLE: 0, VG_HAPTIC_LEVEL: 2, VG_BACKLIGHT: 0 };
var settings = {};
fields.forEach(function(name) {
  var stored = localStorage.getItem('vg_' + name);
  settings[name] = stored === null ? defaults[name] : parseInt(stored, 10);
  if (isNaN(settings[name])) settings[name] = defaults[name];
});

function configWithCurrentValues(items) {
  return items.map(function(item) {
    var clone = {};
    Object.keys(item).forEach(function(key) { clone[key] = item[key]; });
    if (clone.messageKey && settings.hasOwnProperty(clone.messageKey)) {
      var value = settings[clone.messageKey];
      clone.defaultValue = clone.type === 'toggle' ? !!value : String(value);
    }
    if (item.items) clone.items = configWithCurrentValues(item.items);
    return clone;
  });
}

Pebble.addEventListener('ready', function() {
  console.log('VitalGauge phone configuration ready.');
});

Pebble.addEventListener('appmessage', function(e) {
  // Watch settings are authoritative while the app is running.
  fields.forEach(function(name) {
    var key = keys[name];
    if (e.payload && e.payload.hasOwnProperty(key)) {
      var value = parseInt(e.payload[key], 10);
      if (isNaN(value)) return;
      settings[name] = value;
      localStorage.setItem('vg_' + name, String(value));
    }
  });
});

Pebble.addEventListener('showConfiguration', function() {
  clay.config = configWithCurrentValues(baseConfig);
  Pebble.openURL(clay.generateUrl());
});

Pebble.addEventListener('webviewclosed', function(e) {
  if (!e || !e.response || e.response === 'CANCELLED') return;
  var values;
  try { values = clay.getSettings(e.response); }
  catch (err) { console.log('Invalid VitalGauge config response: ' + err); return; }
  var dict = {};
  fields.forEach(function(name) {
    var key = keys[name];
    if (!values.hasOwnProperty(key)) return;
    var v = values[key];
    var value = (typeof v === 'boolean') ? (v ? 1 : 0) : parseInt(v, 10);
    if (isNaN(value)) return;
    dict[key] = value;
  });
  if (!Object.keys(dict).length) return;
  // Only an explicit Clay Save may change settings on the watch.
  dict[keys.VG_SETTINGS_SAVE] = 0x5647;
  Pebble.sendAppMessage(dict, function() {
    fields.forEach(function(name) {
      var key = keys[name];
      if (dict.hasOwnProperty(key)) {
        settings[name] = dict[key];
        localStorage.setItem('vg_' + name, String(dict[key]));
      }
    });
    console.log('VitalGauge settings delivered.');
  }, function(error) {
    console.log('VitalGauge settings delivery failed: ' + JSON.stringify(error));
  });
});
