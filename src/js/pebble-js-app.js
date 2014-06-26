var TRIG_MAX_ANGLE = 65536;

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
    console.log("AM send fail " + e.error.message);
};

var push_geo_keys = function(pos){
    console.log("Send geo lat = " + pos.coords.latitude + " llong=" + pos.coords.longitude);
    Pebble.sendAppMessage({
        "AM_GEO_LAT": Math.round(pos.coords.latitude * TRIG_MAX_ANGLE / 360),
        "AM_GEO_LON": Math.round(pos.coords.longitude * TRIG_MAX_ANGLE / 360)
    }, am_send_ok, am_send_fail);
};

var app_startup = function(){
    navigator.geolocation.getCurrentPosition(push_geo_keys);
    Pebble.sendAppMessage({
        "AM_DST": dst_hack()
    }, am_send_ok, am_send_fail);
};

Pebble.addEventListener("ready", app_startup);
