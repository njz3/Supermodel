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
 * AspectRatio.cpp
 *
 * The frame and the image's place in it (see AspectRatio.h). The curved edges,
 * along the cropped axis, in frame units: a crop keeps a part k of the image
 * (k = frame / image), so the image is 1/k frames long. The undistorted middle
 * (BarrelCenter) shows the middle of the crop window at the crop's scale. On
 * each side, L of the image is left to show in an edge D = (1 - middle) / 2
 * wide: a point x into the edge (0 at the middle, D at the frame's border)
 * shows the arc s = L * asin(sin(t) * x / D) / t, t = min(L / R, pi/2) and R
 * the radius (BarrelRadius / 100): the image wrapped on a cylinder seen from
 * the front. L = D: a plain copy. CropPosition moves the crop window, so the
 * middle, and each edge has its own L. SuperAA's shader does the same per
 * pixel.
 */

#include "AspectRatio.h"
#include "Util/NewConfig.h"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace AspectRatio
{
  const char *const kNames[7] = { "ORIGINAL", "4:3", "16:9", "4:3_STRETCH", "16:9_STRETCH", "4:3_CROP", "16:9_CROP" };
  const char *const kLabels[7] = { "Original", "4:3 letterbox", "16:9 letterbox", "4:3 stretched", "16:9 stretched", "4:3 cropped", "16:9 cropped" };

  bool Parse(Settings *settings, const std::string &aspectRatio)
  {
    std::string name;
    for (char c : aspectRatio)
    {
      if (!std::isspace(static_cast<unsigned char>(c)))
        name += char(std::toupper(static_cast<unsigned char>(c)));
    }

    float frame = 0.0f;
    std::string fit;
    if (name.rfind("4:3", 0) == 0)
    {
      frame = 4.0f / 3.0f;
      fit = name.substr(3);
    }
    else if (name.rfind("16:9", 0) == 0)
    {
      frame = 16.0f / 9.0f;
      fit = name.substr(4);
    }
    else if (name == "ORIGINAL" || name.empty())
    {
      settings->fit = Fit::Original;
      settings->frameAspect = 0.0f;
      return true;
    }
    else
      return false;

    if (fit.empty() || fit == "_LETTERBOX")
      settings->fit = Fit::Letterbox;
    else if (fit == "_STRETCH")
      settings->fit = Fit::Stretch;
    else if (fit == "_CROP")
      settings->fit = Fit::Crop;
    else
      return false;
    settings->frameAspect = frame;
    return true;
  }

  bool FromConfig(Settings *settings, const Util::Config::Node &config)
  {
    settings->cropPosition = std::clamp(config["CropPosition"].ValueAsDefault<int>(0), -100, 100);
    settings->cropBarrel = config["CropBarrel"].ValueAsDefault<bool>(false);
    settings->barrelCenter = std::clamp(config["BarrelCenter"].ValueAsDefault<int>(60), 0, 100);
    settings->barrelRadius = std::clamp(config["BarrelRadius"].ValueAsDefault<int>(50), 1, 1000);
    if (Parse(settings, config["AspectRatio"].ValueAsDefault<std::string>("ORIGINAL")))
      return true;
    settings->fit = Fit::Original;
    settings->frameAspect = 0.0f;
    return false;
  }

  // The image an edge shows at x into it (see above), in frame units from the middle
  static float EdgeArc(float x, float length, float angle, float edge)
  {
    if (length <= edge * 1.0001f || angle < 1e-4f)
      return length * x / edge;
    return length * std::asin(std::sin(angle) * x / edge) / angle;
  }

  // Along the curved axis: q in the frame (0..1) -> in the image (0..1)
  static float Barrel(const Layout &l, float q)
  {
    float u;
    if (q < l.edge)
      u = l.start - EdgeArc(l.edge - q, l.length[0], l.angle[0], l.edge);
    else if (q > l.edge + l.center)
      u = l.start + l.center + EdgeArc(q - l.edge - l.center, l.length[1], l.angle[1], l.edge);
    else
      u = l.start + q - l.edge;
    return u * l.scale;
  }

  Layout Compute(const Settings &s, unsigned windowWidth, unsigned windowHeight, bool stretchFrame, bool wideScreen, float imageAspect)
  {
    Layout l{};
    float W = float(std::max(windowWidth, 1u));
    float H = float(std::max(windowHeight, 1u));
    float frameAspect = s.fit == Fit::Original || s.frameAspect <= 0.0f ? imageAspect : s.frameAspect;

    // The frame: the whole window when stretched, else the largest of its shape in the middle
    float fw = W, fh = H;
    if (!stretchFrame)
    {
      if (W / H > frameAspect)
        fw = H * frameAspect;
      else
        fh = W / frameAspect;
    }
    l.frame[0] = (W - fw) / 2.0f;
    l.frame[1] = (H - fh) / 2.0f;
    l.frame[2] = fw;
    l.frame[3] = fh;

    // The image in frame units (the frame is 1 x 1 whatever its shape in pixels)
    float iw = 1.0f, ih = 1.0f;
    if (s.fit == Fit::Letterbox)
    {
      if (imageAspect < frameAspect)
        iw = imageAspect / frameAspect;
      else
        ih = frameAspect / imageAspect;
    }
    else if (s.fit == Fit::Crop)
    {
      if (imageAspect < frameAspect)
        ih = frameAspect / imageAspect;
      else
        iw = imageAspect / frameAspect;
    }
    float place = s.fit == Fit::Crop ? (std::clamp(s.cropPosition, -100, 100) + 100) / 200.0f : 0.5f;
    float ix = (1.0f - iw) * place;
    float iy = (1.0f - ih) * place;
    l.image[0] = l.frame[0] + ix * fw;
    l.image[1] = l.frame[1] + iy * fh;
    l.image[2] = iw * fw;
    l.image[3] = ih * fh;

    // The renderers draw the image at its size on screen (the crop's scale), widened to the frame by the wide screen hack
    l.imageWidth = std::max(1u, unsigned(std::lround(l.image[2])));
    l.imageHeight = std::max(1u, unsigned(std::lround(l.image[3])));
    l.targetWidth = l.imageWidth;
    l.targetHeight = l.imageHeight;
    unsigned frameWidth = unsigned(std::lround(fw));
    if (wideScreen && frameWidth > l.imageWidth)
    {
      l.targetWidth = frameWidth;
      l.imageX = (frameWidth - l.imageWidth) / 2;
    }

    // Curved edges along the cropped axis
    l.barrelAxis = -1;
    if (s.fit == Fit::Crop && s.cropBarrel)
    {
      int axis = iw > 1.0001f ? 0 : (ih > 1.0001f ? 1 : -1);
      float center = std::clamp(s.barrelCenter, 0, 100) / 100.0f;
      if (axis >= 0 && center < 1.0f)
      {
        float len = axis == 0 ? iw : ih;
        float windowStart = -(axis == 0 ? ix : iy);
        float radius = std::max(s.barrelRadius, 1) / 100.0f;
        l.barrelAxis = axis;
        l.center = center;
        l.edge = (1.0f - center) / 2.0f;
        l.start = windowStart + l.edge;
        l.scale = 1.0f / len;
        l.length[0] = l.start;
        l.length[1] = len - (l.start + center);
        for (int i = 0; i < 2; i++)
          l.angle[i] = std::min(l.length[i] / radius, 1.57079633f);
      }
    }
    return l;
  }

  bool WindowToImage(const Layout &l, float x, float y, float *u, float *v)
  {
    float qx = (x - l.frame[0]) / l.frame[2];
    float qy = (y - l.frame[1]) / l.frame[3];
    *u = (x - l.image[0]) / l.image[2];
    *v = (y - l.image[1]) / l.image[3];
    if (l.barrelAxis == 0)
      *u = Barrel(l, qx);
    else if (l.barrelAxis == 1)
      *v = Barrel(l, qy);
    return qx >= 0.0f && qx <= 1.0f && qy >= 0.0f && qy <= 1.0f;
  }
}
