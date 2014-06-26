#include <pebble.h>

static Window *window;
static GBitmap *kaaba_bmp_white;
static GBitmap *kaaba_bmp_black;

enum AlignmentMode {
  ALIGNMENT_MODE_SUN,
  ALIGNMENT_MODE_NORTH,
  ALIGNMENT_MODE_QIBLA,
  NUM_ALIGNMENT_MODES
};

enum AMKeys {
  AM_DST,
  AM_GEO_LAT,
  AM_GEO_LON
};

static int active_alignment_mode = ALIGNMENT_MODE_SUN;
static int last_alignment_mode = -1;

static int sun_direction = TRIG_MAX_ANGLE * 3 / 4;
static int north_direction = TRIG_MAX_ANGLE * 3 / 4;
static int qibla_direction = TRIG_MAX_ANGLE * 3 / 4;
static char tod[] = "     ";

static int setting_geo_lat = -1;
static int setting_geo_lon = -1;
static int setting_dst = -1;
static bool dont_whine_about_settings_freshness = true;
static bool settings_fresh = false;

static int time_inc = 0;

static void alignment_mode_tween_set(void* subject, int16_t value);
static int16_t alignment_mode_tween_get(void* subject);

#define ALIGNMENT_MODE_TWEEN_MAX 32767
static int16_t alignment_mode_tween_value = 0;
static const PropertyAnimationImplementation alignment_mode_tween_impl = {
  .base = {
    .update = (AnimationUpdateImplementation) property_animation_update_int16,
  },
  .accessors = {
    .setter = { .int16 = alignment_mode_tween_set, },
    .getter = { .int16 = (const Int16Getter) alignment_mode_tween_get, },
  },
};
static PropertyAnimation* alignment_mode_tween_anim = NULL;

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
  int start_inset = 25;
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
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, bounds, 0, 0);

  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_context_set_stroke_color(ctx, GColorWhite);
  GPoint origin = GPoint(bounds.size.w/2, bounds.size.h/2);

  bool settings_ok = setting_dst != -1 && setting_geo_lat != -1 && setting_geo_lon != -1;
  if (settings_ok) {
    // Amazing trig functions are amazing!

    // SUN INDICATOR
    int sun_rad = 9;
    int sun_margin = 5;
    int sun_rad_vt = (bounds.size.h - sun_margin) / 2 - sun_rad;
    int sun_rad_hz = (bounds.size.w - sun_margin) / 2 - sun_rad;
    GPoint sun_loc = to_cart_ellipse(sun_rad_hz, sun_rad_vt, sun_direction, origin);
    graphics_fill_circle(ctx, sun_loc, sun_rad);

    // NORTH INDICATOR
    int north_rad = 7;
    int north_margin = 35;
    int north_arrow_inset = -15;
    int north_rad_vt = (bounds.size.h - north_margin) / 2 - north_rad;
    int north_rad_hz = (bounds.size.w - north_margin) / 2 - north_rad;
    GPoint north_loc = to_cart_ellipse(north_rad_hz, north_rad_vt, north_direction, origin);

    graphics_draw_text(ctx, "N", fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD), GRect(north_loc.x - north_rad + 1, north_loc.y - north_rad - 5, north_rad * 2, north_rad * 2), GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
    graphics_context_set_fill_color(ctx, GColorBlack);
    draw_chevron(ctx, north_rad_hz - north_arrow_inset, north_rad_vt - north_arrow_inset, north_direction, origin);
    graphics_context_set_fill_color(ctx, GColorWhite);

    // QIBLA INDICATOR
    int qibla_rad = 18;
    int qibla_margin = 3;
    int qibla_arrow_inset = 0;
    int qibla_rad_vt = (bounds.size.h - qibla_margin) / 2 - qibla_rad;
    int qibla_rad_hz = (bounds.size.w - qibla_margin) / 2 - qibla_rad;

    draw_bf_arrow(ctx, qibla_rad_hz - qibla_arrow_inset, qibla_rad_vt - qibla_arrow_inset, qibla_direction, origin);

  }
  // KAABA
  int kaaba_width = 32;
  int kaaba_height = 36;
  graphics_context_set_compositing_mode(ctx, GCompOpOr);
  graphics_draw_bitmap_in_rect(ctx, kaaba_bmp_white, GRect(bounds.size.w/2 - kaaba_width/2, bounds.size.h/2 - kaaba_height/2, kaaba_width, kaaba_height));
  graphics_context_set_compositing_mode(ctx, GCompOpClear);
  graphics_draw_bitmap_in_rect(ctx, kaaba_bmp_black, GRect(bounds.size.w/2 - kaaba_width/2, bounds.size.h/2 - kaaba_height/2, kaaba_width, kaaba_height));

  if (!settings_fresh && !dont_whine_about_settings_freshness) {
    graphics_context_set_fill_color(ctx, GColorBlack);
    graphics_fill_rect(ctx, GRect(0, bounds.size.h - 20, bounds.size.w, 20), 0, 0);
    graphics_draw_text(ctx, "No Phone Connection", fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD), GRect(0, bounds.size.h - 22, bounds.size.w, 20), GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
  }
  // graphics_draw_text(ctx, tod, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD), GRect(0, bounds.size.h - 20, bounds.size.w, 20), GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
}

static int tan_lookup(int x) {
  return ((long)sin_lookup(x) * (long)TRIG_MAX_ANGLE) / ((long)cos_lookup(x));
}

static int calculate_qibla_north_cw_offset(int lat, int lon) {
  // http://www.geomete.com/abdali/papers/qibla.pdf
  int kaaba_lat = 21.423333 * TRIG_MAX_ANGLE / 360;
  int kaaba_lon = 39.823333 * TRIG_MAX_ANGLE / 360;

  int numerator = sin_lookup(kaaba_lon - lon);
  long denom_a = (long)cos_lookup(lat) * (long)tan_lookup(kaaba_lat);
  long denom_b = (long)sin_lookup(lat) * (long)cos_lookup(kaaba_lon - lon);
  long denominator = (denom_a/TRIG_MAX_ANGLE - denom_b/TRIG_MAX_ANGLE);

  int result = atan2_lookup((int16_t)(numerator/4), (int16_t)(denominator/4));

  return result;
}

static void calculate_indicators(void) {

  time_t now;
  now = time(NULL) + time_inc;
  struct tm* tm_now = localtime(& now);

  strftime((char*)&tod, 6, "%H:%M", tm_now);

  sun_direction = 0;
  int clock_hour_mintes = (tm_now->tm_hour * 60 + setting_dst) % (12 * 360);
  bool hemis = setting_geo_lat <= 0; // False = north
  if (tm_now->tm_hour > 12) hemis = !hemis;

  int sun_north_offset = ((clock_hour_mintes + tm_now->tm_min) * TRIG_MAX_ANGLE / 12 / 60 / 2 + (hemis ? TRIG_MAX_ANGLE / 2 : 0)); // TODO:DST?
  north_direction = sun_direction + sun_north_offset;

  int north_qibla_cw_offset = calculate_qibla_north_cw_offset(setting_geo_lat, setting_geo_lon);
  qibla_direction = north_direction - north_qibla_cw_offset;

  int alignment_mode_offset[] = {
    TRIG_MAX_ANGLE/4, // ALIGNMENT_MODE_SUN
    TRIG_MAX_ANGLE/4 - north_direction, // ALIGNMENT_MODE_NORTH
    TRIG_MAX_ANGLE/4 - qibla_direction // ALIGNMENT_MODE_QIBLA
  };

  int this_offset = alignment_mode_offset[active_alignment_mode];
  if (last_alignment_mode >= 0){
    int last_offset = alignment_mode_offset[last_alignment_mode];
    int delta = this_offset - last_offset;
    if (delta < 0) delta = delta + TRIG_MAX_ANGLE;
    if (delta > TRIG_MAX_ANGLE / 2) delta = delta - TRIG_MAX_ANGLE;
    this_offset = this_offset - (delta * (ALIGNMENT_MODE_TWEEN_MAX - alignment_mode_tween_value)) / ALIGNMENT_MODE_TWEEN_MAX;
  }

  sun_direction += this_offset;
  north_direction += this_offset;
  qibla_direction += this_offset;

  layer_mark_dirty(window_get_root_layer(window));
}

static void alignment_mode_tween_set(void* subject, int16_t value) {
  // APP_LOG(APP_LOG_LEVEL_DEBUG, "Test %d", value);
  alignment_mode_tween_value = value;
  calculate_indicators();
  // layer_mark_dirty(window_get_root_layer(window));
}
static int16_t alignment_mode_tween_get(void* subject) {
  APP_LOG(APP_LOG_LEVEL_DEBUG, "GOT V %d", alignment_mode_tween_value);
  return alignment_mode_tween_value;
}

static void increment_sun(void* unused){
  app_timer_register(33, increment_sun, NULL);
  time_inc += 60;
  calculate_indicators();
  layer_mark_dirty(window_get_root_layer(window));
}

static void select_click_handler(ClickRecognizerRef recognizer, void *context) {
  last_alignment_mode = active_alignment_mode;
  active_alignment_mode = (active_alignment_mode + 1) % NUM_ALIGNMENT_MODES;
  alignment_mode_tween_value = 0;
  animation_schedule((Animation*)alignment_mode_tween_anim);
  calculate_indicators();
}

static void click_config_provider(void *context) {
  window_single_click_subscribe(BUTTON_ID_SELECT, select_click_handler);
}

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  calculate_indicators();
}

static void window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  kaaba_bmp_white = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_KAABA_WHITE);
  kaaba_bmp_black = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_KAABA_BLACK);

  layer_set_update_proc(window_layer, draw_indicators);
  tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);

  alignment_mode_tween_anim = property_animation_create(&alignment_mode_tween_impl, window_layer, NULL, NULL);
  // This is broken :(
  alignment_mode_tween_anim->values.from.int16 = 0;
  alignment_mode_tween_anim->values.to.int16 = ALIGNMENT_MODE_TWEEN_MAX;
  // increment_sun(NULL);
  calculate_indicators();
}

static void window_unload(Window *window) {
  //...
}

static void load_settings(void) {
  if (persist_exists(AM_DST)) {
    setting_dst = persist_read_int(AM_DST);
  }
  if (persist_exists(AM_GEO_LAT)) {
    setting_geo_lat = persist_read_int(AM_GEO_LAT);
  }
  if (persist_exists(AM_GEO_LON)) {
    setting_geo_lon = persist_read_int(AM_GEO_LON);
  }
}

static void persist_settings(void) {
  persist_write_int(AM_DST, setting_dst);
  persist_write_int(AM_GEO_LAT, setting_geo_lat);
  persist_write_int(AM_GEO_LON, setting_geo_lon);
  // APP_LOG(APP_LOG_LEVEL_DEBUG, "SETTINGS DST=%d LAT=%d LON=%d", setting_dst, setting_geo_lat, setting_geo_lon);
  // APP_LOG(APP_LOG_LEVEL_DEBUG, "OR DST=%d LAT=%d LON=%d", setting_dst, setting_geo_lat * 360 / TRIG_MAX_ANGLE, setting_geo_lon * 360 / TRIG_MAX_ANGLE);
}

static void in_received_handler(DictionaryIterator *received, void *context) {
  Tuple *dst_tuple = dict_find(received, AM_DST);
  if (dst_tuple) {
    setting_dst = dst_tuple->value->int32;
  }
  Tuple *geo_lat_tuple = dict_find(received, AM_GEO_LAT);
  if (geo_lat_tuple) {
    setting_geo_lat = geo_lat_tuple->value->int32;
  }
  Tuple *geo_lon_tuple = dict_find(received, AM_GEO_LON);
  if (geo_lon_tuple) {
    setting_geo_lon = geo_lon_tuple->value->int32;
  }
  settings_fresh = true;
  calculate_indicators();
  persist_settings();
}

static void start_whining_about_freshness(void* unused) {
  dont_whine_about_settings_freshness = false;
  calculate_indicators();
}

static void init(void) {

  load_settings();
  app_message_register_inbox_received(in_received_handler);
  app_message_open(128, 128);

  app_timer_register(1500, start_whining_about_freshness, NULL);

  window = window_create();
  window_set_click_config_provider(window, click_config_provider);
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
