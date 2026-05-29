#pragma once

#include <Geode/Geode.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>

using namespace geode::prelude;

namespace Utils
{

    [[nodiscard]] inline ccHSVValue rgbToHsv(ccColor3B color)
    {
        constexpr float inv255 = 1.0f / 255.0f;

        float r = static_cast<float>(color.r) * inv255;
        float g = static_cast<float>(color.g) * inv255;
        float b = static_cast<float>(color.b) * inv255;

        float maxv = std::max(r, std::max(g, b));
        float minv = std::min(r, std::min(g, b));
        float delta = maxv - minv;

        float h = 0.0f;
        float s = 0.0f;
        float v = maxv;

        if (delta > 0.0f)
        {
            s = (maxv > 0.0f) ? (delta / maxv) : 0.0f;

            if (maxv == r)
            {
                h = 60.0f * ((g - b) / delta);
                if (h < 0.0f)
                    h += 360.0f;
            }
            else if (maxv == g)
            {
                h = 60.0f * ((b - r) / delta + 2.0f);
            }
            else
            {
                h = 60.0f * ((r - g) / delta + 4.0f);
            }
        }

        return ccHSVValue{h, s, v};
    }

    [[nodiscard]] inline ccColor3B hsvToRgb(ccHSVValue color)
    {
        float h = std::fmod(color.h, 360.0f);
        if (h < 0.0f)
            h += 360.0f;

        float s = std::clamp(color.s, 0.0f, 1.0f);
        float v = std::clamp(color.v, 0.0f, 1.0f);

        float r = 0.0f;
        float g = 0.0f;
        float b = 0.0f;

        if (s <= 0.0f)
        {
            r = g = b = v;
        }
        else
        {
            float hh = h / 60.0f;
            int sector = static_cast<int>(hh);
            float f = hh - static_cast<float>(sector);

            float p = v * (1.0f - s);
            float q = v * (1.0f - s * f);
            float t = v * (1.0f - s * (1.0f - f));

            switch (sector)
            {
            case 0:
                r = v;
                g = t;
                b = p;
                break;
            case 1:
                r = q;
                g = v;
                b = p;
                break;
            case 2:
                r = p;
                g = v;
                b = t;
                break;
            case 3:
                r = p;
                g = q;
                b = v;
                break;
            case 4:
                r = t;
                g = p;
                b = v;
                break;
            default:
                r = v;
                g = p;
                b = q;
                break;
            }
        }

        auto toByte = [](float x) -> std::uint8_t
        {
            x = std::clamp(x, 0.0f, 1.0f);
            return static_cast<std::uint8_t>(x * 255.0f + 0.5f);
        };

        return ccColor3B{
            toByte(r),
            toByte(g),
            toByte(b)};
    }

}