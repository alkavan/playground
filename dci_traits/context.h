// SPDX-FileCopyrightText: 2025-2026 Igal Alkon
// SPDX-License-Identifier: MIT

#ifndef CONTEXT_H
#define CONTEXT_H

#include "domain.h"

struct EnvironmentContext {
    Season season;
    TimeOfDay time_of_day;

    EnvironmentContext(const Season &season_, const TimeOfDay &time_of_day_)
        : season(season_), time_of_day(time_of_day_) {}
};

#endif //CONTEXT_H
