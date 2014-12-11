var TRIG_MAX_ANGLE = 65536;
var geo_update_timer, geo_pending;

var dst_hack = function() {
    var now = new Date();
    var jan = new Date(now.getFullYear(), 0, 1);
    var jul = new Date(now.getFullYear(), 6, 1);
    var dst_offset = Math.max(jan.getTimezoneOffset(), jul.getTimezoneOffset());
    var observing_dst = now.getTimezoneOffset() < dst_offset;
    if (observing_dst) {
        return now.getTimezoneOffset() - dst_offset;
    }
};

var am_send_ok = function(){
};

var am_send_fail = function(e){
    console.log("AM send fail", e.error.message);
};

var geo_error = function(err) {
    console.log("Geo fail", err.code, err.message);
    geo_pending = false;
};

var push_geo_keys = function(pos){
    console.log("Geo request ok");
    geo_pending = false;

    Pebble.sendAppMessage({
        "AM_GEO_LAT": Math.round(pos.coords.latitude * TRIG_MAX_ANGLE / 360),
        "AM_GEO_LON": Math.round(pos.coords.longitude * TRIG_MAX_ANGLE / 360),
        "AM_DST": dst_hack()
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
