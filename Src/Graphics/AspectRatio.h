/**
 ** Supermodel
 ** A Sega Model 3 Arcade Emulator.
 ** Copyright 2003-2026 The Supermodel Team
 **
 ** This file is part of Supermodel.
 **
 ** Supermodel is free software: you can redistribute it and/or modify it under
 ** the terms of the GNU General Public License as published by the Free
 ** Software Foundation, either version 3 of the License, or (at your option)
 ** any later version.
 **
 ** Supermodel is distributed in the hope that it will be useful, but WITHOUT
 ** ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 ** FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 ** more details.
 **
 ** You should have received a copy of the GNU General Public License along
 ** with Supermodel.  If not, see <http://www.gnu.org/licenses/>.
 **/

/*
 * AspectRatio.h
 *
 * The AspectRatio setting and its companions: the frame the image is shown in
 * (its shape, placed in the window) and how the image fills it: whole with
 * black bars, stretched, or cropped, the cropped parts possibly curved onto
 * the edges as on a cylinder seen from the front. Same names and meanings as
 * the other emulators of the cabinets' front-end (linuxloader...). Only the
 * geometry here: SuperAA draws it.
 */

#ifndef INCLUDED_ASPECTRATIO_H
#define INCLUDED_ASPECTRATIO_H

#include <string>

namespace Util
{
  namespace Config
  {
    class Node;
  }
}

namespace AspectRatio
{
  enum class Fit
  {
    Original,   // the frame has the image's shape: the usual display, without the final pass
    Letterbox,  // the whole image in the frame at its proportions, black bars where the shapes differ
    Stretch,    // the image fills the frame, distorted
    Crop        // the image fills the frame at its proportions, what overflows cut (or curved onto the edges)
  };

  struct Settings
  {
    Fit fit = Fit::Original;
    float frameAspect = 0.0f;   // the frame's width / height (Original: the image's)
    int cropPosition = 0;       // the part a crop keeps: -100 left or top, 0 the middle, 100 right or bottom
    bool cropBarrel = false;    // curve the cropped parts onto the edges instead of cutting them
    int barrelCenter = 60;      // the middle of the frame shown undistorted, % of the frame
    int barrelRadius = 50;      // the cylinder's radius, % of the frame
  };

  // The AspectRatio values, in the order of the menus
  extern const char *const kNames[7];
  extern const char *const kLabels[7];

  // An AspectRatio value (case free; 4:3_LETTERBOX is 4:3); false if unknown
  bool Parse(Settings *settings, const std::string &aspectRatio);

  // From AspectRatio, CropPosition, CropBarrel, BarrelCenter and BarrelRadius; false for an unknown AspectRatio (then Original)
  bool FromConfig(Settings *settings, const Util::Config::Node &config);

  // Where everything lands. In the window: pixels with the origin at the top left
  struct Layout
  {
    float frame[4];         // x, y, width, height
    float image[4];         // the whole image (cropped: larger than the frame along one axis)

    // The renderers' frame buffer: the image, widened to the frame's width by the wide screen hack
    unsigned targetWidth;
    unsigned targetHeight;
    unsigned imageX, imageY, imageWidth, imageHeight;   // the image in it (OpenGL: origin at the bottom left)

    // Curved edges along one axis (0 horizontal, 1 vertical, -1 none), in frame units
    int barrelAxis;
    float edge;             // each edge's width: (1 - center) / 2
    float center;           // the undistorted middle's width
    float start;            // the image shown at the middle's start, from the image's start
    float scale;            // frame / image along the axis: the image is 1 / scale frames long
    float length[2];        // the image each edge shows (before the middle, after it)
    float angle[2];         // the arc of the cylinder each edge covers, radians
  };

  // imageAspect: the game's image (width / height); stretchFrame: the frame fills the window (Stretch);
  // wideScreen: the renderers draw beyond the image, across the frame's width
  Layout Compute(const Settings &settings, unsigned windowWidth, unsigned windowHeight, bool stretchFrame, bool wideScreen, float imageAspect);

  // A window position (origin top left) in the image: 0..1 across it, beyond it outside. False outside the frame
  bool WindowToImage(const Layout &layout, float x, float y, float *u, float *v);
}

#endif  // INCLUDED_ASPECTRATIO_H
