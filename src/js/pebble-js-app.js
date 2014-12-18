var TRIG_MAX_ANGLE = 65536;
var geo_update_timer, geo_pending;

var am_send_ok = function(){
};

var am_send_fail = function(e){
    console.log("AM send fail", e.message);
};

var geo_error = function(err) {
    console.log("Geo fail", err.code, err.message);
    geo_pending = false;
};

var request_geo_name = function(pos) {
    var req = new XMLHttpRequest();
    req.open('GET', 'https://geonames-proxy.cpfx.ca/findNearbyPlaceNameJSON?lat=' + pos.coords.latitude + '&lng=' + pos.coords.longitude + '&maxRows=1&username=pebble_quran', true);
    req.onload = function(e) {
      if (req.readyState == 4) {
        if(req.status == 200) {
          var response = JSON.parse(req.responseText);
          if (response.geonames.length) {
            var loc = response.geonames[0];
            Pebble.sendAppMessage({
              "AM_GEO_NAME": loc.name + (loc.adminCode1 ? ", " + loc.adminCode1 : "")
            }, am_send_ok, am_send_fail);
          }
        } else { console.error('Error fetching geoname ' + req.responseText); }
      }
    };
    req.send(null);

};

var push_geo_keys = function(pos){
    console.log("Geo request ok");
    geo_pending = false;

    request_geo_name(pos);

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

Pebble.addEventListener("ready", app_startup);
Pebble.addEventListener("appmessage", watchapp_alive);
