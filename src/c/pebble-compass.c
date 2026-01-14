#include <pebble.h>

typedef struct AppSettings {
  uint8_t fps;
} AppSettings;

typedef struct AppState {
  CompassStatus status;
  bool has_location;
  int32_t heading;
  int32_t heading_offset;
  int32_t displayed_heading;
  int32_t target_angle;
  int32_t angular_velocity;
} AppState;
static AppState appstate;

static Window *s_window;
static TextLayer *status_layer;
static Layer *compass_layer;

static GPath *compass_path = NULL;
static const GPathInfo compass_path_info = {
    .num_points = 4, .points = (GPoint[]){{0, 0}, {0, 0}, {0, 0}, {0, 0}}};
static GPoint *top;
static GPoint *left;
static GPoint *middle;
static GPoint *right;

static AppSettings settings;

static void default_settings() { settings.fps = 15; }

static GRect centered_rect(GRect rect, uint16_t radius) {
  return GRect(rect.origin.x + rect.size.w / 2 - radius,
               rect.origin.y + rect.size.h / 2 - radius, radius * 2,
               radius * 2);
}

static void draw_compass(Layer *layer, GContext *ctx) {
  if (appstate.status < CompassStatusCalibrating || !appstate.has_location) {
    return;
  }
  GRect bounds = layer_get_bounds(layer);

  GOvalScaleMode scale = GOvalScaleModeFillCircle;
  *top = gpoint_from_polar(centered_rect(bounds, 60), scale,
                           appstate.displayed_heading + appstate.target_angle);
  *left = gpoint_from_polar(centered_rect(bounds, 60), scale,
                            appstate.displayed_heading + appstate.target_angle +
                                DEG_TO_TRIGANGLE(140));
  *middle =
      gpoint_from_polar(centered_rect(bounds, 30), scale,
                        appstate.displayed_heading + appstate.target_angle +
                            DEG_TO_TRIGANGLE(180));
  *right = gpoint_from_polar(centered_rect(bounds, 60), scale,
                             appstate.displayed_heading +
                                 appstate.target_angle + DEG_TO_TRIGANGLE(220));
  graphics_context_set_fill_color(ctx, GColorBlack);
  gpath_draw_filled(ctx, compass_path);
}

static void frame() {
  if (appstate.status < CompassStatusCalibrating) {
    return;
  }

  if (!appstate.has_location) {
    text_layer_set_text(status_layer, "Location services disabled");
    return;
  }

  int32_t error = appstate.heading - appstate.displayed_heading;
  while (error < -DEG_TO_TRIGANGLE(180)) {
    error += DEG_TO_TRIGANGLE(360);
  }
  while (error > DEG_TO_TRIGANGLE(180)) {
    error -= DEG_TO_TRIGANGLE(360);
  }
  appstate.angular_velocity += error;
  appstate.angular_velocity *= 0.7;
  appstate.displayed_heading += appstate.angular_velocity / 2;
  while (appstate.displayed_heading < -DEG_TO_TRIGANGLE(180)) {
    appstate.displayed_heading += DEG_TO_TRIGANGLE(360);
  }
  while (appstate.displayed_heading > DEG_TO_TRIGANGLE(180)) {
    appstate.displayed_heading -= DEG_TO_TRIGANGLE(360);
  }

  text_layer_set_text(status_layer, "");
  app_timer_register(1000 / settings.fps, frame, NULL);
}

static void compass_heading_handler(CompassHeadingData heading_data) {
  if (appstate.status != heading_data.compass_status) {
    appstate.status = heading_data.compass_status;
    switch (appstate.status) {
    case CompassStatusDataInvalid:
      text_layer_set_text(status_layer, "Calibrating compass...");
      break;
    case CompassStatusUnavailable:
      text_layer_set_text(status_layer, "Compass unavailable");
      break;
    case CompassStatusCalibrating:
    case CompassStatusCalibrated:
      frame();
      break;
    }
  }

  if (heading_data.is_declination_valid) {
    appstate.heading = heading_data.true_heading;
  } else {
    appstate.heading = heading_data.magnetic_heading +
                       appstate.heading_offset * TRIG_MAX_ANGLE / 360 / 100;
  }
}

static void window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);

  status_layer = text_layer_create(bounds);
  text_layer_set_font(status_layer,
                      fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD));
  text_layer_set_text_alignment(status_layer, GTextAlignmentCenter);
  layer_add_child(window_layer, text_layer_get_layer(status_layer));

  compass_layer = layer_create(bounds);
  layer_set_update_proc(compass_layer, draw_compass);
  layer_add_child(window_layer, compass_layer);
}

static void window_unload(Window *window) {
  text_layer_destroy(status_layer);
  layer_destroy(compass_layer);
}

static void inbox_received_handler(DictionaryIterator *iter, void *context) {
  Tuple *tuple;
  tuple = dict_find(iter, MESSAGE_KEY_CompassDecl);
  if (tuple) {
    appstate.heading_offset = tuple->value->int32;
  }
  tuple = dict_find(iter, MESSAGE_KEY_HasLocation);
  if (tuple) {
    appstate.has_location = tuple->value->int8;
    frame();
  }
  tuple = dict_find(iter, MESSAGE_KEY_CompassTargetAngle);
  if (tuple) {
    appstate.target_angle = DEG_TO_TRIGANGLE(tuple->value->int32);
  }
}

static void init(void) {
  s_window = window_create();
  window_set_window_handlers(s_window, (WindowHandlers){
                                           .load = window_load,
                                           .unload = window_unload,
                                       });
  window_stack_push(s_window, true);

  default_settings();
  compass_path = gpath_create(&compass_path_info);
  top = &compass_path_info.points[0];
  left = &compass_path_info.points[1];
  middle = &compass_path_info.points[2];
  right = &compass_path_info.points[3];
  CompassHeadingData data = {
      .magnetic_heading = 0,
      .true_heading = 0,
      .compass_status = CompassStatusDataInvalid,
      .is_declination_valid = false,
  };
  compass_heading_handler(data);
  compass_service_subscribe(compass_heading_handler);
  compass_service_set_heading_filter(DEG_TO_TRIGANGLE(1));

  app_message_register_inbox_received(inbox_received_handler);
  app_message_open(128, 128);
}

static void deinit(void) { window_destroy(s_window); }

int main(void) {
  init();

  APP_LOG(APP_LOG_LEVEL_DEBUG, "Done initializing, pushed window: %p",
          s_window);

  app_event_loop();
  deinit();
}
