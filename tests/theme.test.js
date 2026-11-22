const assert = require('node:assert/strict');
const weather = require('../src/pkjs/theme-weather');
process.env.TZ = 'America/New_York';
const plus = '7f7b6c10-93e0-4f4c-9237-7e8204c5a121';
const native = {uuid: 'native-weather', name: 'Weather',
  sources: [{category: 'weather', items: ['location']}]};
assert.equal(weather.sourcePlugin({enumeratePlugins: () => [native, {uuid: plus}]}), plus);
assert.equal(weather.sourcePlugin({enumeratePlugins: () => ({plugins: [native]})}), native.uuid);
assert.equal(weather.sourcePlugin({}), plus);

const now = new Date(2026, 10, 21, 10).getTime() / 1000;
const rise = new Date(2026, 10, 21, 6, 45).getTime() / 1000;
const set = new Date(2026, 10, 21, 16, 50).getTime() / 1000;
const daily = values => ({series: {timestamp: {values}}});
const value = timestamp => ({timestamp: {value: timestamp}});
const pair = (sunrise, sunset) => ({sunrise, sunset});
const envelope = properties => ({instances: [{properties}]});
const forecast = envelope({
  sunrise: value(rise - 86400), sunset: value(set - 86400),
  daily_sunrise: daily([rise - 86400, rise, rise + 86400]),
  daily_sunset: daily([set - 86400, set, set + 86400])
});
assert.deepEqual(weather.solarFromEnvelope(forecast, now),
  [pair(rise, set), pair(rise + 86400, set + 86400)]);
assert.deepEqual(weather.solarFromEnvelope(envelope({sunrise: value(rise), sunset: value(set)}), now),
  [pair(rise, set), null]);
assert.equal(weather.solarFromEnvelope(envelope({sunrise: value(rise), sunset: value(rise - 1)}), now), null);
assert.equal(weather.solarFromEnvelope(envelope({sunrise: value(rise - 86400), sunset: value(set - 86400)}), now), null);

const keys = {VG_SUNRISE: 10, VG_SUNSET: 11, VG_NEXT_SUNRISE: 12, VG_NEXT_SUNSET: 13};
let request, unsubscribed = false;
const sent = [];
const pebble = {
  enumeratePlugins: () => [native, {uuid: plus}],
  subscribeToSource(options) { request = options; return {unsubscribe() { unsubscribed = true; }}; },
  sendAppMessage(payload, sentCallback) { sent.push(payload); sentCallback(); }
};
Date.now = () => now * 1000;
const theme = weather.create(pebble, keys);
theme.setAuto(false);
assert.equal(request, undefined);
theme.setAuto(true);
assert.equal(request.plugin, plus);
request.onData(forecast);
request.onData(forecast);
assert.deepEqual(sent, [{10: rise, 11: set, 12: rise + 86400, 13: set + 86400}]);
theme.setAuto(false);
assert.equal(unsubscribed, true);
request.onData(forecast);
assert.equal(sent.length, 1);
console.log('VitalGauge weather priority, solar parsing, and Auto subscription passed');
