#pragma once
#include <cmath>

namespace MirrorLocomotion
{

    template<class Matrix>
    bool AlignRelaxedHeading(bool firstPerson, bool combat, float actorYaw, Matrix& rotation) noexcept
    {
        if (!firstPerson || combat || !std::isfinite(actorYaw)) return false;
        for (unsigned row = 0; row < 3; ++row)
            for (unsigned column = 0; column < 3; ++column)
                if (!std::isfinite(rotation[row][column])) return false;
        const float x = rotation[0][0], y = rotation[1][0];
        if (x * x + y * y < 0.000001f) return false;
        const float delta = actorYaw - std::atan2(y, x);
        const float c = std::cos(delta), s = std::sin(delta);
        for (unsigned column = 0; column < 3; ++column) {
            const float a = rotation[0][column], b = rotation[1][column];
            rotation[0][column] = c * a - s * b;
            rotation[1][column] = s * a + c * b;
        }
        return true;
    }
}
