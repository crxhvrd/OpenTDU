#pragma once

struct TimeOfDay {
    float Hours = 0.0f;
    float Minutes = 0.0f;
    float Seconds = 0.0f;

    inline float toSeconds() const {
        return Minutes * 60.0f + Hours * 3600.0f + Seconds;
    }

    static void ToStdUnit( float totalSeconds, TimeOfDay &out ) {
        // FUN_007d0610
        totalSeconds = fmodf( totalSeconds, 86400.0f );
        if ( totalSeconds < 0.0f )
            totalSeconds += 86400.0f;
        const float hours = floorf( totalSeconds / 3600.0f );
        out.Hours = hours;
        const float rem = totalSeconds - hours * 3600.0f;
        const float mins = floorf( rem / 60.0f );
        out.Minutes = mins;
        out.Seconds = rem - mins * 60.0f;
    }
};

static constexpr TimeOfDay kTimeOfDayMax = { 23.0f, 59.0f, 59.999f };
