var TRIG_MAX_ANGLE = 65536;
var geo_update_timer, geo_pending, geo_pos;
var timeline_token;
var did_subscribe = false;

var api_host = "https://qibla-www.cpfx.ca";

var am_send_ok = function(){
};

var am_send_fail = function(e){
    console.log("AM send fail", e.message);
};

var geo_error = function(err) {
    console.log("Geo fail", err.code, err.message);
    geo_pending = false;
};

var timeline_subscribe = function() {
    if (!geo_pos || (!timeline_token && Pebble.getTimelineToken)) {
      return; // Not ready to subscribe yet.
    }
    if (did_subscribe) return;
    did_subscribe = true;
    var req = new XMLHttpRequest();
    req.open('POST', api_host + '/subscribe', true);
    req.onload = function(e) {
      if (req.readyState == 4) {
        if(req.status == 200) {
          var response = JSON.parse(req.responseText);
          var loc = response.location_geoname;
          if (loc) {
            Pebble.sendAppMessage({
              "AM_GEO_NAME": loc
            }, am_send_ok, am_send_fail);
          }
        } else {
          console.error('Error subscribing to timeline ' + req.responseText);
        }
      }
    };
    req.setRequestHeader("Content-type", "application/json");
    req.send(JSON.stringify({
        "location_lat": geo_pos.coords.latitude,
        "location_lon": geo_pos.coords.longitude,
        "tz_offset": (new Date()).getTimezoneOffset(),
        "user_token": Pebble.getAccountToken(),
        "timeline_token": timeline_token
    }));
};

var push_geo_keys = function(pos){
    console.log("Geo request ok");
    geo_pending = false;
    geo_pos = pos;

    timeline_subscribe();

    Pebble.sendAppMessage({
        "AM_GEO_LAT": Math.round(pos.coords.latitude * TRIG_MAX_ANGLE / 360),
        "AM_GEO_LON": Math.round(pos.coords.longitude * TRIG_MAX_ANGLE / 360),
        "AM_GEO_NAME": ""
    }, am_send_ok, am_send_fail);
};

var request_geo = function() {
    if (geo_pending) return; // Don't get too crazy
    console.log("Geo request started");
    navigator.geolocation.getCurrentPosition(push_geo_keys, geo_error, { "timeout": 15000, "maximumAge": 60000 });
};

var app_startup = function(){
    console.log("JS started");
    geo_update_timer = setInterval(request_geo, 1000);
    request_geo();
};

var watchapp_alive = function(){
    console.log("Watchapp is alive");
    // The watchapp acks a settings update - so we can stop sending them
    clearInterval(geo_update_timer);
};

var show_config = function(){
    Pebble.openURL(api_host + '/settings/' + Pebble.getAccountToken());
};

Pebble.addEventListener("ready", app_startup);
Pebble.addEventListener("appmessage", watchapp_alive);
Pebble.addEventListener('showConfiguration', show_config);

if (Pebble.getTimelineToken) {
  Pebble.getTimelineToken(
    function (token) {
      timeline_token = token;
      timeline_subscribe();
    },
    function (error) {
      console.log('Error getting timeline token', error);
    }
  );
}
