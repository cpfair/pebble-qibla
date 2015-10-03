#include <pebble.h>

static Window *window;
static GBitmap *kaaba_bmp_white;
static GBitmap *kaaba_bmp_black;

static GColor foreground_colour;
static GColor background_colour;

enum AMKeys {
  AM_GEO_LAT=1,
  AM_GEO_LON=2,
  AM_GEO_NAME=3,
  AM_ACK=255
};

#define GEO_NAME_LENGTH 64

static int north_direction = TRIG_MAX_ANGLE / 4; // Up
static int damped_north_direction = -TRIG_MAX_ANGLE * 3 / 4;
static int damped_qibla_direction = 0;
static int qibla_north_offset_cw = 0;

static int setting_geo_lat = -1;
static int setting_geo_lon = -1;
static char* setting_geo_name = NULL;
static bool dont_whine_about_settings_freshness = true;
static bool settings_fresh = false;
static bool compass_calibrate = false;

static bool show_geo_name = false;

static const CompassHeading compass_event_hysteresis = TRIG_MAX_ANGLE/90;
static const int TRIG_MAX_RATIO_SQRT = 256;

static GPoint to_cart_ellipse(int rad_hz, int rad_vt, int angle, GPoint origin) {
  return GPoint(cos_lookup(angle) * rad_hz / TRIG_MAX_ANGLE + origin.x, origin.y - sin_lookup(angle) * rad_vt / TRIG_MAX_ANGLE);
}

static GPoint to_cart(int rad, int angle, GPoint origin){
  return to_cart_ellipse(rad, rad, angle, origin);
}

static void draw_arrow(GContext* ctx, int rad_hz, int rad_vt, int angle, GPoint origin) {
  int head_angle_offset = 110000 / (rad_hz + rad_vt); // Small angle approximation? Maybe? Who cares.
  int head_inset = 10;
  GPoint arrow_end_ctr = to_cart_ellipse(rad_hz, rad_vt, angle, origin);
  GPoint arrow_end_a = to_cart_ellipse(rad_hz - head_inset, rad_vt - head_inset, angle + head_angle_offset, origin);
  graphics_draw_line(ctx, origin, arrow_end_ctr);
  graphics_draw_line(ctx, arrow_end_ctr, arrow_end_a);
}

static void draw_chevron(GContext* ctx, int rad_hz, int rad_vt, int angle, GPoint origin) {
  int head_angle_offset = 180000 / (rad_hz + rad_vt); // Small angle approximation? Maybe? Who cares.
  int head_inset = 7;
  int head_height = 7;

  GPoint arrow_end_ctr = to_cart_ellipse(rad_hz, rad_vt, angle, origin);
  GPoint arrow_end_a = to_cart_ellipse(rad_hz - head_inset, rad_vt - head_inset, angle + head_angle_offset, origin);
  GPoint arrow_end_b = to_cart_ellipse(rad_hz - head_inset, rad_vt - head_inset, angle - head_angle_offset, origin);

  GPoint outer_arrow_end_ctr = to_cart_ellipse(rad_hz + head_height, rad_vt + head_height, angle, origin);
  GPoint outer_arrow_end_a = to_cart_ellipse(rad_hz + head_height - head_inset, rad_vt + head_height - head_inset, angle + head_angle_offset, origin);
  GPoint outer_arrow_end_b = to_cart_ellipse(rad_hz + head_height - head_inset, rad_vt + head_height - head_inset, angle - head_angle_offset, origin);
  GPathInfo path_info = {
    .num_points = 6,
    .points = (GPoint[]) {arrow_end_ctr, arrow_end_a, outer_arrow_end_a, outer_arrow_end_ctr, outer_arrow_end_b, arrow_end_b}
  };
  GPath* pth = gpath_create(&path_info);
  gpath_draw_filled(ctx, pth);
  gpath_draw_outline(ctx, pth);
  gpath_destroy(pth);
}

static void draw_bf_arrow(GContext* ctx, int rad_hz, int rad_vt, int angle, GPoint origin) {
  #ifdef PBL_PLATFORM_CHALK
  int start_inset = 33;
  #else
  int start_inset = 25;
  #endif
  int start_head_inset = 4;
  int head_inset = 10;
  int side_offset = 5;
  GPoint outset_origin = to_cart_ellipse(start_inset, start_inset, angle, origin);
  GPoint outset_inset_origin = to_cart_ellipse(start_inset + start_head_inset, start_inset + start_head_inset, angle, origin);
  GPoint arrow_side_offset = to_cart_ellipse(side_offset, side_offset, angle + TRIG_MAX_ANGLE/4, GPointZero);
  GPoint arrow_end_ctr = to_cart_ellipse(rad_hz, rad_vt, angle, origin);
  GPoint arrow_end_inset = to_cart_ellipse(rad_hz - head_inset, rad_vt - head_inset, angle, origin);

  GPoint offset_start_a = GPoint(outset_origin.x + arrow_side_offset.x, outset_origin.y + arrow_side_offset.y);
  GPoint offset_end_a = GPoint(arrow_end_inset.x + arrow_side_offset.x, arrow_end_inset.y + arrow_side_offset.y);
  GPoint offset_start_b = GPoint(outset_origin.x - arrow_side_offset.x, outset_origin.y - arrow_side_offset.y);
  GPoint offset_end_b = GPoint(arrow_end_inset.x - arrow_side_offset.x, arrow_end_inset.y - arrow_side_offset.y);

  GPathInfo path_info = {
    .num_points = 6,
    .points = (GPoint[]) {outset_inset_origin, offset_start_a, offset_end_a, arrow_end_ctr, offset_end_b, offset_start_b}
  };
  GPath* pth = gpath_create(&path_info);
  gpath_draw_filled(ctx, pth);
  gpath_destroy(pth);
}

static void draw_indicators(Layer* layer, GContext* ctx) {
  GRect bounds = layer_get_bounds(layer);
  graphics_context_set_fill_color(ctx, background_colour);
  graphics_fill_rect(ctx, bounds, 0, 0);

  graphics_context_set_fill_color(ctx, foreground_colour);
  graphics_context_set_stroke_color(ctx, foreground_colour);
  GPoint origin = GPoint(bounds.size.w/2, bounds.size.h/2);

  bool settings_ok = setting_geo_lat != -1 && setting_geo_lon != -1;
  if (settings_ok) {
    // Amazing trig functions are amazing!

    // NORTH INDICATOR
    #ifndef PBL_PLATFORM_CHALK
    int north_rad = 7;
    int north_margin = 35;
    int north_arrow_inset = -15;
    int north_rad_vt = (bounds.size.h - north_margin) / 2 - north_rad;
    int north_rad_hz = (bounds.size.w - north_margin) / 2 - north_rad;
    GPoint north_loc = to_cart_ellipse(north_rad_hz, north_rad_vt, damped_north_direction, origin);

    graphics_draw_text(ctx, "N", fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD), GRect(north_loc.x - north_rad + 1, north_loc.y - north_rad - 5, north_rad * 2, north_rad * 2), GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
    graphics_context_set_fill_color(ctx, background_colour);
    draw_chevron(ctx, north_rad_hz - north_arrow_inset, north_rad_vt - north_arrow_inset, damped_north_direction, origin);
    graphics_context_set_fill_color(ctx, foreground_colour);
    #endif

    // QIBLA INDICATOR
    int qibla_rad = 18;
    int qibla_margin = 3;
    int qibla_arrow_inset = 0;
    int qibla_rad_vt = (bounds.size.h - qibla_margin) / 2 - qibla_rad;
    int qibla_rad_hz = (bounds.size.w - qibla_margin) / 2 - qibla_rad;

    draw_bf_arrow(ctx, qibla_rad_hz - qibla_arrow_inset, qibla_rad_vt - qibla_arrow_inset, damped_qibla_direction, origin);

  }
  // KAABA
  int kaaba_width = 32;
  int kaaba_height = 36;
  graphics_context_set_compositing_mode(ctx, GCompOpOr);
  graphics_draw_bitmap_in_rect(ctx, kaaba_bmp_white, GRect(bounds.size.w/2 - kaaba_width/2, bounds.size.h/2 - kaaba_height/2, kaaba_width, kaaba_height));
  graphics_context_set_compositing_mode(ctx, GCompOpClear);
  graphics_draw_bitmap_in_rect(ctx, kaaba_bmp_black, GRect(bounds.size.w/2 - kaaba_width/2, bounds.size.h/2 - kaaba_height/2, kaaba_width, kaaba_height));

  char* note = NULL;
  if (!settings_fresh && !dont_whine_about_settings_freshness) {
    note = "No Phone Connection";
  } else if (compass_calibrate) {
    if (battery_state_service_peek().is_plugged) {
      note = "Unplug Charger";
    } else {
      note = "Shake & Roll Pebble";
    }
  } else if (show_geo_name && setting_geo_name && strlen(setting_geo_name)) {
    note = setting_geo_name;
  }

  if (note) {
    #ifdef PBL_PLATFORM_CHALK
    GRect note_rect;
    if (damped_qibla_direction > TRIG_MAX_ANGLE/2) {
      note_rect = GRect(0, 40, bounds.size.w, 20);
    } else {
      note_rect = GRect(0, bounds.size.h - 60, bounds.size.w, 20);
    }
    #else
    GRect note_rect = GRect(0, bounds.size.h - 20, bounds.size.w, 20);
    #endif
    graphics_context_set_fill_color(ctx, background_colour);
    graphics_fill_rect(ctx, note_rect, 0, 0);
    graphics_draw_text(ctx, note, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD), GRect(note_rect.origin.x, note_rect.origin.y - 2, note_rect.size.w, note_rect.size.h), GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
  }
}

static int tan_lookup(int x) {
  return ((long)sin_lookup(x) * (long)TRIG_MAX_RATIO) / ((long)cos_lookup(x));
}

static inline void wrap_angle(int *angle) {
  while (*angle < 0) {
    *angle += TRIG_MAX_ANGLE;
  }
  while (*angle > TRIG_MAX_ANGLE) {
    *angle -= TRIG_MAX_ANGLE;
  }
}

static int calculate_qibla_north_cw_offset(int lat, int lon) {
  // http://www.geomete.com/abdali/papers/qibla.pdf
  int kaaba_lat = 21.423333 * TRIG_MAX_ANGLE / 360;
  int kaaba_lon = 39.823333 * TRIG_MAX_ANGLE / 360;

  int numerator = sin_lookup(kaaba_lon - lon);
  long denom_a = ((long)cos_lookup(lat) / TRIG_MAX_RATIO_SQRT * (long)tan_lookup(kaaba_lat) / TRIG_MAX_RATIO_SQRT);
  long denom_b = ((long)sin_lookup(lat) / TRIG_MAX_RATIO_SQRT * (long)cos_lookup(kaaba_lon - lon) / TRIG_MAX_RATIO_SQRT);
  long denominator = denom_a - denom_b;

  int result = atan2_lookup((int16_t)(numerator/4), (int16_t)(denominator/4));

  // APP_LOG(APP_LOG_LEVEL_DEBUG, "Qibla offset %d, %d", result, (result * 360) / TRIG_MAX_ANGLE);
  // APP_LOG(APP_LOG_LEVEL_DEBUG, "Numerator raw %d, %d", (kaaba_lon - lon), ((kaaba_lon - lon) * 360) / TRIG_MAX_ANGLE);
  // APP_LOG(APP_LOG_LEVEL_DEBUG, "Numerator proc %d, %d*10^-2", (numerator), ((numerator) * 100) / TRIG_MAX_RATIO);
  // APP_LOG(APP_LOG_LEVEL_DEBUG, "Denom a proc %ld, %ld*10^-2", (denom_a), ((denom_a) * 100) / TRIG_MAX_RATIO);
  // APP_LOG(APP_LOG_LEVEL_DEBUG, "Denom b proc %ld, %ld*10^-2", (denom_b), ((denom_b) * 100) / TRIG_MAX_RATIO);
  // APP_LOG(APP_LOG_LEVEL_DEBUG, "Denom proc %ld, %ld*10^-2", (denominator), ((denominator) * 100) / TRIG_MAX_RATIO);

  return result;
}

static void update_indicator_directions(void) {
  damped_north_direction = north_direction;
  damped_qibla_direction = damped_north_direction - qibla_north_offset_cw;
  layer_mark_dirty(window_get_root_layer(window));
}

static void update_indicator_directions_animated(void) {
  static const int damping_factor_1 = 10;
  static const int damping_factor_2 = -20;
  static const int MAX_PROGRESS = 100;
  int delta = (north_direction - damped_north_direction);
  if (delta < 0) delta += TRIG_MAX_ANGLE;
  if (delta > TRIG_MAX_ANGLE / 2) delta = delta - TRIG_MAX_ANGLE;
  int progress = MAX_PROGRESS - (abs(delta) * MAX_PROGRESS / (TRIG_MAX_ANGLE));


  damped_north_direction +=  delta * (progress / damping_factor_1 + progress * progress / MAX_PROGRESS / damping_factor_2) / MAX_PROGRESS;
  damped_qibla_direction = damped_north_direction - qibla_north_offset_cw;
  // Prevent these from going wildly out of range
  wrap_angle(&damped_north_direction);
  wrap_angle(&damped_qibla_direction);

  layer_mark_dirty(window_get_root_layer(window));
}


static void calculate_qibla_north_offset(void) {
  qibla_north_offset_cw = calculate_qibla_north_cw_offset(setting_geo_lat, setting_geo_lon);
}

static void fake_animation(void* unused){
  update_indicator_directions_animated();
  app_timer_register(33, fake_animation, NULL);
}

static void compass_heading_handler(CompassHeadingData heading_data){
    if (heading_data.compass_status != CompassStatusDataInvalid){
      compass_calibrate = false;
      north_direction = TRIG_MAX_ANGLE/4 - heading_data.true_heading;
    } else {
      compass_calibrate = true;
      north_direction = TRIG_MAX_ANGLE/4; // Up
    }
}

void centre_button_down(void* unused, void* ctx) {
  show_geo_name = true;
  layer_mark_dirty(window_get_root_layer(window));
}

void centre_button_up(void* unused, void* ctx) {
  show_geo_name = false;
  layer_mark_dirty(window_get_root_layer(window));
}

void click_config_provider(Window *window) {
  window_raw_click_subscribe(BUTTON_ID_SELECT, centre_button_down, centre_button_up, NULL);
}

static void window_load(Window *window) {
  window_set_click_config_provider(window, (ClickConfigProvider) click_config_provider);

  Layer *window_layer = window_get_root_layer(window);
  kaaba_bmp_white = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_KAABA_WHITE);
  kaaba_bmp_black = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_KAABA_BLACK);

  WatchInfoColor watch_colour = watch_info_get_color();
  if (watch_colour == WATCH_INFO_COLOR_WHITE ||
      watch_colour == WATCH_INFO_COLOR_TIME_WHITE ||
      watch_colour == WATCH_INFO_COLOR_TIME_STEEL_SILVER ||
      watch_colour == WATCH_INFO_COLOR_TIME_ROUND_SILVER_14 ||
      watch_colour == WATCH_INFO_COLOR_TIME_ROUND_SILVER_20 ||
      watch_colour == WATCH_INFO_COLOR_TIME_ROUND_ROSE_GOLD_14) {
    foreground_colour = GColorBlack;
    background_colour = GColorWhite;
    GBitmap* swap = kaaba_bmp_black;
    kaaba_bmp_black = kaaba_bmp_white;
    kaaba_bmp_white = swap;
  } else {
    foreground_colour = GColorWhite;
    background_colour = GColorBlack;
  }

  layer_set_update_proc(window_layer, draw_indicators);
  calculate_qibla_north_offset();
  update_indicator_directions();
  fake_animation(NULL);

}

static void window_unload(Window *window) {
  //...
}

static void load_settings(void) {
  if (persist_exists(AM_GEO_LAT)) {
    setting_geo_lat = persist_read_int(AM_GEO_LAT);
  }
  if (persist_exists(AM_GEO_LON)) {
    setting_geo_lon = persist_read_int(AM_GEO_LON);
  }
  if (persist_exists(AM_GEO_NAME)) {
    if (setting_geo_name) free(setting_geo_name);
    setting_geo_name = malloc(GEO_NAME_LENGTH);
    persist_read_string(AM_GEO_NAME, setting_geo_name, GEO_NAME_LENGTH);
  }
}

static void persist_settings(void) {
  persist_write_int(AM_GEO_LAT, setting_geo_lat);
  persist_write_int(AM_GEO_LON, setting_geo_lon);
  if (setting_geo_name) {
    persist_write_string(AM_GEO_NAME, setting_geo_name);
  } else {
    persist_delete(AM_GEO_NAME);
  }

  // APP_LOG(APP_LOG_LEVEL_DEBUG, "SETTINGS DST=%d LAT=%d LON=%d", setting_dst, setting_geo_lat, setting_geo_lon);
  // APP_LOG(APP_LOG_LEVEL_DEBUG, "OR DST=%d LAT=%d LON=%d", setting_dst, setting_geo_lat * 360 / TRIG_MAX_ANGLE, setting_geo_lon * 360 / TRIG_MAX_ANGLE);
}

static void in_received_handler(DictionaryIterator *received, void *context) {
  Tuple *geo_lat_tuple = dict_find(received, AM_GEO_LAT);
  if (geo_lat_tuple) {
    // APP_LOG(APP_LOG_LEVEL_DEBUG, "Rx Lat %d", (int)geo_lat_tuple->value->int32);
    setting_geo_lat = geo_lat_tuple->value->int32;
  }
  Tuple *geo_lon_tuple = dict_find(received, AM_GEO_LON);
  if (geo_lon_tuple) {
    // APP_LOG(APP_LOG_LEVEL_DEBUG, "Rx Lon %d", (int)geo_lon_tuple->value->int32);
    setting_geo_lon = geo_lon_tuple->value->int32;
  }

  Tuple *geo_name_tuple = dict_find(received, AM_GEO_NAME);
  if (geo_name_tuple) {
    if (setting_geo_name) free(setting_geo_name);
    setting_geo_name = malloc(geo_name_tuple->length);
    memcpy(setting_geo_name, geo_name_tuple->value->cstring, geo_name_tuple->length);
    // APP_LOG(APP_LOG_LEVEL_DEBUG, "Rx geoname %s", setting_geo_name);
  }

  settings_fresh = true;
  calculate_qibla_north_offset();
  persist_settings();

  // Ack to JS app so it stops spamming us
  DictionaryIterator *iter;
  app_message_outbox_begin(&iter);
  dict_write_uint8(iter, AM_ACK, 1);
  app_message_outbox_send();
}

static void start_whining_about_freshness(void* unused) {
  dont_whine_about_settings_freshness = false;
}

static void init(void) {

  load_settings();
  app_message_register_inbox_received(in_received_handler);
  app_message_open(128, 128);

  app_timer_register(1500, start_whining_about_freshness, NULL);

  compass_service_set_heading_filter(compass_event_hysteresis);
  compass_service_subscribe(&compass_heading_handler);

  window = window_create();
  window_set_window_handlers(window, (WindowHandlers) {
    .load = window_load,
    .unload = window_unload,
  });
  const bool animated = true;
  window_stack_push(window, animated);
}

static void deinit(void) {
  window_destroy(window);
}

int main(void) {
  init();

  APP_LOG(APP_LOG_LEVEL_DEBUG, "Done initializing, pushed window: %p", window);

  app_event_loop();
  deinit();
}
