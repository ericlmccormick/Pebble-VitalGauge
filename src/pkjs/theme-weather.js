var WEATHER_PLUS = '7f7b6c10-93e0-4f4c-9237-7e8204c5a121';

function sourcePlugin(pebble) {
  if (typeof pebble.enumeratePlugins !== 'function') return WEATHER_PLUS;
  try {
    var catalog = pebble.enumeratePlugins();
    var entries = Array.isArray(catalog) ? catalog :
      catalog && (catalog.plugins || catalog.items || catalog.results) || [];
    var builtIn;
    for (var i = 0; i < entries.length; i++) {
      var plugin = entries[i];
      if (!plugin) continue;
      var id = String(typeof plugin === 'string' ? plugin :
        plugin.uuid || plugin.pluginUuid || plugin.appUuid || plugin.id ||
        (plugin.manifest && plugin.manifest.uuid) || '').toLowerCase();
      if (id === WEATHER_PLUS) return WEATHER_PLUS;
      var weather = Array.isArray(plugin.sources) && plugin.sources.some(function(source) {
        return source && source.category === 'weather' &&
          Array.isArray(source.items) && source.items.indexOf('location') !== -1;
      });
      if (weather && id && (plugin.builtIn || plugin.builtin || plugin.isBuiltin ||
          String(plugin.name).toLowerCase() === 'weather')) builtIn = id;
    }
    if (builtIn) return builtIn;
  } catch (error) {
    console.log('VitalGauge weather catalog unavailable: ' + error);
  }
  // A pinned request still resolves to the platform's Weather source if
  // Weather+ is absent and the catalog did not enumerate the built-in.
  return WEATHER_PLUS;
}

function timestamp(value) {
  var number = Number(value);
  return isFinite(number) && number > 0 && number <= 2147483647 ? Math.floor(number) : 0;
}

function localDate(seconds) {
  var date = new Date(seconds * 1000);
  return date.getFullYear() + '/' + date.getMonth() + '/' + date.getDate();
}

function solarFromEnvelope(envelope, now) {
  var date = new Date(now * 1000);
  date.setDate(date.getDate() + 1);
  var dates = [localDate(now), localDate(date.getTime() / 1000)];
  var instances = envelope && Array.isArray(envelope.instances) ? envelope.instances : [];
  for (var i = 0; i < instances.length; i++) {
    var properties = instances[i] && instances[i].properties || {};
    var days = [null, null];
    function add(rise, set) {
      rise = timestamp(rise);
      set = timestamp(set);
      if (!rise || set <= rise || set - rise >= 86400) return;
      var day = dates.indexOf(localDate(rise));
      if (day !== -1) days[day] = { sunrise: rise, sunset: set };
    }
    var sunriseSeries = properties.daily_sunrise && properties.daily_sunrise.series;
    var sunsetSeries = properties.daily_sunset && properties.daily_sunset.series;
    var rises = sunriseSeries && sunriseSeries.timestamp && sunriseSeries.timestamp.values;
    var sets = sunsetSeries && sunsetSeries.timestamp && sunsetSeries.timestamp.values;
    if (Array.isArray(rises) && Array.isArray(sets)) {
      for (var j = 0; j < rises.length && j < sets.length; j++) add(rises[j], sets[j]);
    }
    add(properties.sunrise && properties.sunrise.timestamp && properties.sunrise.timestamp.value,
        properties.sunset && properties.sunset.timestamp && properties.sunset.timestamp.value);
    if (days[0] || days[1]) return days;
  }
  return null;
}

function create(pebble, keys) {
  var subscription;
  var enabled = false;
  var generation = 0;
  var lastDelivered = '';
  var pending = null;
  var sending = false;
  var retry = null;

  function deliver(attempt) {
    if (!enabled || !pending || sending || retry) return;
    var current = pending;
    var token = generation;
    if (current.signature === lastDelivered) { pending = null; return; }
    sending = true;
    pebble.sendAppMessage(current.payload, function() {
      if (token !== generation) return;
      sending = false;
      lastDelivered = current.signature;
      if (pending === current) pending = null;
      deliver(0);
    }, function() {
      if (token !== generation) return;
      sending = false;
      if (attempt >= 2 && pending === current) { pending = null; return; }
      retry = setTimeout(function() {
        retry = null;
        deliver(pending === current ? attempt + 1 : 0);
      }, 1200);
    });
  }

  function setAuto(next) {
    if (next === enabled) return;
    enabled = next;
    var token = ++generation;
    if (subscription && typeof subscription.unsubscribe === 'function') subscription.unsubscribe();
    subscription = null;
    if (retry) clearTimeout(retry);
    retry = null;
    pending = null;
    sending = false;
    lastDelivered = '';
    if (!enabled || typeof pebble.subscribeToSource !== 'function') return;
    try {
      subscription = pebble.subscribeToSource({
        category: 'weather', item: 'location', plugin: sourcePlugin(pebble),
        properties: ['sunrise', 'sunset', 'daily_sunrise', 'daily_sunset'],
        onData: function(envelope) {
          if (token !== generation) return;
          var days = solarFromEnvelope(envelope, Math.floor(Date.now() / 1000));
          if (!days) return;
          var payload = {};
          if (days[0]) {
            payload[keys.VG_SUNRISE] = days[0].sunrise;
            payload[keys.VG_SUNSET] = days[0].sunset;
          }
          if (days[1]) {
            payload[keys.VG_NEXT_SUNRISE] = days[1].sunrise;
            payload[keys.VG_NEXT_SUNSET] = days[1].sunset;
          }
          pending = { signature: JSON.stringify(days), payload: payload };
          deliver(0);
        },
        onError: function(error) {
          console.log('VitalGauge weather theme unavailable: ' + (error && error.code || error));
        }
      });
    } catch (error) {
      console.log('VitalGauge weather theme subscription failed: ' + error);
    }
  }

  return { setAuto: setAuto };
}

module.exports = { create: create, solarFromEnvelope: solarFromEnvelope, sourcePlugin: sourcePlugin };
