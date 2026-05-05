#pragma once

#include <stdint.h>

/* Render rotating cube at (view_x, view_y). angle_y in radians. */
void lib3d_demo_render(uint16_t view_x, uint16_t view_y, float angle_y);
