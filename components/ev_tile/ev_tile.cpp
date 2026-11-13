#include "ev_tile.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace esphome::ev_tile {

using tessera::Font;
namespace ui = tessera::ui;

// The icon that stands in the picture's place while there is no picture (Tessera's set: car-electric).
static constexpr uint32_t ICON_CAR = 0xF0B6C;

// Copy a text into a fixed field, always ended.
static void keep(char *to, size_t size, const char *from) {
  std::snprintf(to, size, "%s", from ? from : "");
}

// Whether Home Assistant's state is a number, and that number.
static bool number_of(const char *state, float *out) {
  if (!state || !*state) return false;
  char *end = nullptr;
  const float v = std::strtof(state, &end);
  if (end == state || !std::isfinite(v)) return false;
  *out = v;
  return true;
}

// The tile: the car's name at the top, its picture in the middle, the battery as a bar with the percentage in it and
// the range under it, as in a car maker's app. create() makes the parts once; on_state() keeps the values; paint()
// lays out and sets texts only when something changed, and ui::set_* change a property only when it differs.
class CarTile : public tessera::Tile {
 public:
  explicit CarTile(const tessera::Plugin *plugin) : plugin_(plugin) {}

  void create(const tessera::TileContext &c) override {
    width_ = c.width;
    height_ = c.height;
    name_ = c.name;
    entity_ = c.entity;
    contain_ = std::string(c.options["fit"] | "contain") != "cover";
    low_ = c.options["low"] | 20;
    const char *decimals = c.options["decimals"] | "ha";
    decimals_ = std::strcmp(decimals, "0") == 0 ? 0 : std::strcmp(decimals, "1") == 0 ? 1 : -1;

    // From the back to the front: the picture (or its stand-in icon), the bar, the texts.
    if (ui::pictures()) place_ = ui::picture(c.parent, tessera::Placeholder::NONE);
    icon_ = ui::label(c.parent, Font::ICON, theme::SUBTLE);
    ui::set_text(icon_, ui::icon(ICON_CAR));
    lv_obj_set_style_text_align(icon_, LV_TEXT_ALIGN_CENTER, 0);

    title_ = ui::label(c.parent, Font::HEADLINE, theme::INK);
    lv_obj_set_style_text_align(title_, LV_TEXT_ALIGN_CENTER, 0);

    // The bar holds its charge, and each holds its own percentage label, so the words stand on the colour they are
    // read on: in the charge when it is long enough, on the track beside it when not.
    track_ = ui::block(c.parent, theme::TRACK);
    fill_ = ui::block(track_, theme::ACCENT);
    for (lv_obj_t *bar : {track_, fill_}) {
      lv_obj_set_style_radius(bar, LV_RADIUS_CIRCLE, 0);
      lv_obj_set_style_pad_all(bar, 0, 0);
      lv_obj_set_style_border_width(bar, 0, 0);
      lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    }
    percent_in_ = ui::label(fill_, Font::BODY_LARGE, theme::ON_ACCENT);
    percent_out_ = ui::label(track_, Font::BODY_LARGE, theme::INK);

    range_word_ = ui::label(c.parent, Font::BODY, theme::MUTED);
    range_value_ = ui::label(c.parent, Font::TITLE, theme::INK);
    dirty_ = true;
  }

  void on_state(JsonObjectConst data) override {
    // The tile's own entity is the picture; its friendly name is the title when the tile has no name of its own.
    keep(entity_name_, sizeof(entity_name_), data["name"] | "");

    JsonObjectConst entities = data["entities"];
    JsonObjectConst battery = entities["battery"];
    has_battery_ = !battery.isNull();
    battery_ok_ = has_battery_ && number_of(battery["state"] | "", &battery_);

    JsonObjectConst range = entities["range"];
    has_range_ = !range.isNull();
    float km = 0;
    range_ok_ = has_range_ && number_of(range["state"] | "", &km);
    if (range_ok_) {
      if (decimals_ < 0) keep(range_num_, sizeof(range_num_), range["state"] | "");
      else std::snprintf(range_num_, sizeof(range_num_), "%.*f", decimals_, km);
    }
    keep(range_unit_, sizeof(range_unit_), range["attributes"]["unit_of_measurement"] | "");

    // Lay out first: a place gets its picture only once it has a size.
    paint();
    if (place_) {
      const std::string entity = roomy::plain(entity_);
      ui::set_picture(place_, entity.empty() ? std::string() : "camera:" + entity,
                      contain_ ? tessera::Fit::CONTAIN : tessera::Fit::COVER);
    }
  }

  void on_tick(uint32_t epoch) override {
    if (dirty_) paint();
  }

  void on_picture(lv_obj_t *place, tessera::PictureState state) override {
    if (place != place_) return;
    shown_ = state == tessera::PictureState::SHOWN;
    dirty_ = true;
    paint();
  }

  void on_theme() override {
    ui::set_color(icon_, theme::SUBTLE);
    ui::set_color(title_, theme::INK);
    ui::set_color(range_word_, theme::MUTED);
    ui::set_color(range_value_, range_ok_ ? theme::INK : theme::MUTED);
    ui::set_fill(track_, tessera::Tone::NORMAL, theme::TRACK);
    paint_bar();
  }

 private:
  // The texts that say what each part shows now.
  std::string title_text() const {
    if (!name_.empty()) return roomy::plain(name_);
    return entity_name_;
  }
  std::string percent_text() const {
    if (!has_battery_) return plugin_->text("no_battery");
    if (!battery_ok_) return "-";
    char t[16];
    std::snprintf(t, sizeof(t), "%d%%", static_cast<int>(std::lround(battery_)));
    return t;
  }
  std::string range_text() const {
    if (!has_range_) return plugin_->text("no_range");
    if (!range_ok_) return "-";
    std::string t = range_num_;
    if (*range_unit_) t += std::string(" ") + range_unit_;
    return t;
  }

  // Lay the tile out from the bottom: the range, the bar over it, the title at the top and the picture in what is
  // left. A tile too low for all of it drops the picture first, then the title.
  void paint() {
    dirty_ = false;
    const int gap = ui::px(6);
    const int bar_h = ui::line_height(Font::BODY_LARGE) + ui::px(4);
    const int range_h = ui::line_height(Font::TITLE);

    // The title: the largest font it fits in.
    const std::string title = title_text();
    Font title_font = Font::HEADLINE;
    if (ui::text_width(title, Font::HEADLINE) > width_ || !ui::large()) title_font = Font::TITLE;
    const int title_h = title.empty() ? 0 : ui::line_height(title_font);

    int fixed = bar_h + gap + range_h;
    bool show_title = title_h > 0 && fixed + title_h + gap <= height_;
    if (show_title) fixed += title_h + gap;
    const int room = height_ - fixed - gap;
    const bool show_picture = room >= ui::px(36);

    // Everything together in the middle of the card when there is no picture to take the room.
    int y = show_picture ? 0 : std::max(0, (height_ - fixed) / 2);
    if (show_title) {
      ui::set_font(title_, title_font);
      ui::set_text(title_, title);
      lv_obj_set_width(title_, width_);
      lv_obj_set_pos(title_, 0, y);
      lv_obj_remove_flag(title_, LV_OBJ_FLAG_HIDDEN);
      y += title_h + gap;
    } else {
      lv_obj_add_flag(title_, LV_OBJ_FLAG_HIDDEN);
    }

    if (show_picture) {
      if (place_) {
        lv_obj_set_size(place_, width_, room);
        lv_obj_set_pos(place_, 0, y);
        lv_obj_remove_flag(place_, LV_OBJ_FLAG_HIDDEN);
      }
      // The car icon where no picture is (a board without pictures, no image entity, one that did not come).
      const bool icon = !place_ || !shown_;
      if (icon) {
        lv_obj_set_width(icon_, width_);
        lv_obj_set_pos(icon_, 0, y + (room - ui::line_height(Font::ICON)) / 2);
        lv_obj_remove_flag(icon_, LV_OBJ_FLAG_HIDDEN);
      } else {
        lv_obj_add_flag(icon_, LV_OBJ_FLAG_HIDDEN);
      }
      y += room + gap;
    } else {
      if (place_) lv_obj_add_flag(place_, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(icon_, LV_OBJ_FLAG_HIDDEN);
    }

    bar_y_ = y;
    bar_h_ = bar_h;
    paint_bar();
    y += bar_h + gap;

    // The range: "Range:" in a soft colour, the value bold beside it, the two in the middle, bottoms on one line.
    const std::string value = range_text();
    const std::string word = plugin_->text("range_word");
    const int value_w = ui::text_width(value, Font::TITLE);
    const int space = ui::px(6);
    const int word_w = ui::text_width(word, Font::BODY);
    const bool with_word = range_ok_ && word_w + space + value_w <= width_;
    const int total = with_word ? word_w + space + value_w : std::min(value_w, width_);
    const int x = (width_ - total) / 2;
    if (with_word) {
      ui::set_text(range_word_, word);
      lv_obj_set_width(range_word_, word_w + 1);
      lv_obj_set_pos(range_word_, x, y + range_h - ui::line_height(Font::BODY));
      lv_obj_remove_flag(range_word_, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(range_word_, LV_OBJ_FLAG_HIDDEN);
    }
    ui::set_text(range_value_, value);
    ui::set_color(range_value_, range_ok_ ? theme::INK : theme::MUTED);
    lv_obj_set_width(range_value_, std::min(value_w + 1, width_));
    lv_obj_set_pos(range_value_, with_word ? x + word_w + space : x, y);
  }

  // The battery bar: a track the width of the tile, the charge over it, the percentage at the end of the charge (on
  // it when it fits, beside it when the charge is short). At or below the low mark the charge turns red.
  void paint_bar() {
    if (!track_) return;
    lv_obj_set_size(track_, width_, bar_h_);
    lv_obj_set_pos(track_, 0, bar_y_);

    const float pct = battery_ok_ ? std::max(0.0f, std::min(100.0f, battery_)) : 0.0f;
    const int fill_w = battery_ok_ ? std::max(bar_h_, static_cast<int>(width_ * pct / 100.0f)) : 0;
    if (fill_w > 0) {
      lv_obj_set_size(fill_, fill_w, bar_h_);
      lv_obj_set_pos(fill_, 0, 0);
      lv_obj_remove_flag(fill_, LV_OBJ_FLAG_HIDDEN);
      ui::set_fill(fill_, pct <= low_ ? tessera::Tone::ALERT : tessera::Tone::NORMAL, theme::ACCENT);
    } else {
      lv_obj_add_flag(fill_, LV_OBJ_FLAG_HIDDEN);
    }

    const std::string text = percent_text();
    const int pad = ui::px(8);
    const int text_w = std::min(ui::text_width(text, Font::BODY_LARGE) + 1, width_ - 2 * pad);
    const int text_y = (bar_h_ - ui::line_height(Font::BODY_LARGE)) / 2;
    const bool inside = battery_ok_ && fill_w >= text_w + 2 * pad;
    lv_obj_t *shown = inside ? percent_in_ : percent_out_;
    lv_obj_t *other = inside ? percent_out_ : percent_in_;
    lv_obj_add_flag(other, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(shown, LV_OBJ_FLAG_HIDDEN);
    ui::set_text(shown, text);
    lv_obj_set_width(shown, text_w);
    if (inside) {
      lv_obj_set_pos(shown, fill_w - pad - text_w, text_y);
      ui::set_color(shown, theme::ON_ACCENT);
    } else if (!battery_ok_) {
      // No number: the words in the middle of the empty track.
      lv_obj_set_pos(shown, (width_ - text_w) / 2, text_y);
      ui::set_color(shown, theme::MUTED);
    } else {
      lv_obj_set_pos(shown, std::min(fill_w + pad, width_ - pad - text_w), text_y);
      ui::set_color(shown, theme::INK);
    }
  }

  const tessera::Plugin *plugin_;
  tessera::Text name_, entity_;   // kept as long as the card: PSRAM first (docs/FIRMWARE_API.md, "Memory")
  char entity_name_[52] = "";
  char range_num_[20] = "";
  char range_unit_[12] = "";
  int width_ = 0, height_ = 0, bar_y_ = 0, bar_h_ = 0, low_ = 20, decimals_ = -1;
  float battery_ = 0;
  bool contain_ = true, has_battery_ = false, battery_ok_ = false, has_range_ = false, range_ok_ = false;
  bool shown_ = false, dirty_ = true;
  lv_obj_t *place_{}, *icon_{}, *title_{}, *track_{}, *fill_{}, *percent_in_{}, *percent_out_{}, *range_word_{}, *range_value_{};
};

void EvTile::setup() {
  add_tile("car", [this]() { return new CarTile(this); });
}

}  // namespace esphome::ev_tile
