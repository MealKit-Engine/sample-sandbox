// Vendored copy of module-health 0.2.0; change it in that repo (tools\module.ps1 update), not here.
#pragma once

#include "health/sim/Health.h"

// Health bar values for a view model. Bind `healthRatio` in assets/ui/health_bar.ui, `healthText`/`damageText` in damage_number.ui.
namespace health {

// 0 (dead) .. 1 (full), for widthRatio
inline mealkit::f32 ratio(const Health& h) { return fraction(h); }

// True when the bar should look urgent (at or below `threshold` of max, alive)
inline bool low(const Health& h, mealkit::f32 threshold = 0.25f) { return !h.dead && fraction(h) <= threshold; }

// Whole points for text (1000 fixed-point units = 1 point), rounded up so 1 unit left still reads "1"
inline mealkit::i32 points(mealkit::i32 fixed) { return fixed <= 0 ? 0 : (fixed + 999) / 1000; }

} // namespace health
