#include "CheckpointHelpers.hpp"
#include <cocos2d.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <string>
#include <string_view>

using namespace geode::prelude;

namespace checkpoint_mod
{
    namespace
    {
        enum class ShapeKind : uint8_t
        {
            Circle = 0,
            Square = 1,
            X = 2,
            Diamond = 3,
        };

        struct StyleSnapshot
        {
            ShapeKind shape = ShapeKind::Diamond;
            double scale = 1.0;
            ccColor3B outer{255, 255, 255};
            ccColor3B inner{255, 255, 255};
        };

        char toLowerAscii(char c)
        {
            return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }

        bool equalsIgnoreCase(std::string_view a, std::string_view b)
        {
            if (a.size() != b.size())
                return false;
            for (size_t i = 0; i < a.size(); ++i)
            {
                if (toLowerAscii(a[i]) != toLowerAscii(b[i]))
                    return false;
            }
            return true;
        }

        std::string_view trim(std::string_view value)
        {
            while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())))
            {
                value.remove_prefix(1);
            }
            while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())))
            {
                value.remove_suffix(1);
            }
            return value;
        }

        int hexDigit(char c)
        {
            if (c >= '0' && c <= '9')
                return c - '0';
            c = toLowerAscii(c);
            if (c >= 'a' && c <= 'f')
                return c - 'a' + 10;
            return -1;
        }

        ShapeKind parseShape(std::string_view key)
        {
            key = trim(key);
            if (equalsIgnoreCase(key, "circle"))
                return ShapeKind::Circle;
            if (equalsIgnoreCase(key, "square"))
                return ShapeKind::Square;
            if (equalsIgnoreCase(key, "x"))
                return ShapeKind::X;
            return ShapeKind::Diamond;
        }

        std::string_view shapeName(ShapeKind shape)
        {
            switch (shape)
            {
            case ShapeKind::Circle:
                return "circle";
            case ShapeKind::Square:
                return "square";
            case ShapeKind::X:
                return "x";
            case ShapeKind::Diamond:
                return "diamond";
            }
            return "diamond";
        }

        Pack makePack(geode::ZStringView outerName, geode::ZStringView innerName)
        {
            return Pack{outerName, innerName};
        }

        std::array<Pack, 4> buildPacks()
        {
            return {
                makePack("checkpoint_circle_outer.png"_spr, "checkpoint_circle_inner.png"_spr),
                makePack("checkpoint_square_outer.png"_spr, "checkpoint_square_inner.png"_spr),
                makePack("checkpoint_x_outer.png"_spr, "checkpoint_x_inner.png"_spr),
                makePack("checkpoint_diamond_outer.png"_spr, "checkpoint_diamond_inner.png"_spr),
            };
        }

        Pack const *findPack(ShapeKind shape)
        {
            static const auto packs = buildPacks();
            return &packs[static_cast<size_t>(shape)];
        }

        Pack const *findPack(geode::ZStringView key)
        {
            return findPack(parseShape(key.view()));
        }

        bool sameColor(ccColor3B const &a, ccColor3B const &b)
        {
            return a.r == b.r && a.g == b.g && a.b == b.b;
        }

        bool sameSnapshot(StyleSnapshot const &a, StyleSnapshot const &b)
        {
            return a.shape == b.shape &&
                   a.scale == b.scale &&
                   sameColor(a.outer, b.outer) &&
                   sameColor(a.inner, b.inner);
        }
    }

    ccColor3B parseColor(std::string const &value)
    {
        std::string_view color = trim(value);
        if (!color.empty() && color.front() == '#')
        {
            color.remove_prefix(1);
        }

        auto readByte = [&](size_t index, bool shortForm) -> int
        {
            if (shortForm)
            {
                int hi = hexDigit(color[index]);
                return hi >= 0 ? hi * 17 : -1;
            }

            int hi = hexDigit(color[index]);
            int lo = hexDigit(color[index + 1]);
            if (hi < 0 || lo < 0)
                return -1;
            return hi * 16 + lo;
        };

        if (color.size() == 3)
        {
            int r = readByte(0, true);
            int g = readByte(1, true);
            int b = readByte(2, true);
            if (r >= 0 && g >= 0 && b >= 0)
            {
                return {static_cast<GLubyte>(r), static_cast<GLubyte>(g), static_cast<GLubyte>(b)};
            }
        }
        else if (color.size() == 6)
        {
            int r = readByte(0, false);
            int g = readByte(2, false);
            int b = readByte(4, false);
            if (r >= 0 && g >= 0 && b >= 0)
            {
                return {static_cast<GLubyte>(r), static_cast<GLubyte>(g), static_cast<GLubyte>(b)};
            }
        }

        return ccWHITE;
    }

    Pack const &getPack(geode::ZStringView key)
    {
        return *findPack(key);
    }

    CheckpointStyle const &getStyle()
    {
        static CheckpointStyle cachedStyle{
            "diamond",
            1.f,
            ccWHITE,
            ccWHITE,
        };

        static StyleSnapshot cachedSnapshot{};
        static bool hasCached = false;

        StyleSnapshot current;
        if (auto *mod = Mod::get())
        {
            current.shape = parseShape(mod->getSettingValue<std::string>("checkpoint-shape"));
            current.scale = mod->getSettingValue<double>("checkpoint-scale");
            current.outer = mod->getSettingValue<ccColor3B>("checkpoint-outer-color");
            current.inner = mod->getSettingValue<ccColor3B>("checkpoint-inner-color");
        }
        else
        {
            current.shape = ShapeKind::Diamond;
            current.scale = 1.0;
            current.outer = ccWHITE;
            current.inner = ccWHITE;
        }
        if (!std::isfinite(current.scale) || current.scale <= 0.0)
        {
            current.scale = 1.0;
        }
        current.scale = std::clamp(current.scale, 0.05, 10.0);

        if (hasCached && sameSnapshot(cachedSnapshot, current))
        {
            return cachedStyle;
        }

        cachedSnapshot = current;
        cachedStyle = CheckpointStyle{
            std::string(shapeName(current.shape)),
            static_cast<float>(current.scale),
            current.outer,
            current.inner,
        };
        hasCached = true;
        return cachedStyle;
    }
}