#pragma once
#include "OKStreetSocialText.h"
#include "CoreMinimal.h"

// The street lives on the real 2010 calendar from 2010-09-01 (plan §5). A game day runs from 04:00 to the next
// 04:00, so the small hours still belong to the evening before; the shown date is the calendar date of the clock.
inline const FDateTime& OKStreetCalendarEpoch() { static const FDateTime Epoch(2010, 9, 1); return Epoch; }
constexpr double OKStreetDayStartHour = 4.0;
constexpr double OKStreetNewGameHour = 8.0;

// Calendar date and time for game day `Day` (0 = 2010-09-01) at street clock `Hours` (0..24).
inline FDateTime OKStreetCalendarDateTime(int32 Day, double Hours) {
 const double H = FMath::Clamp(Hours, 0.0, 24.0 - 1.0 / 3600.0);
 const int32 DateDay = Day + (H < OKStreetDayStartHour ? 1 : 0);
 return OKStreetCalendarEpoch() + FTimespan::FromDays(DateDay) + FTimespan::FromSeconds(FMath::FloorToDouble(H * 3600.0));
}

// How many 04:00 day starts the clock passed going forward from `From` to `To` (both 0..24, wrapping at midnight).
// The tick moves the clock by seconds at a time, so one call never spans more than a day.
inline int32 OKStreetDayStartsPassed(double From, double To) {
 if (From == To) return 0;
 const double Forward = To > From ? To - From : To + 24.0 - From;
 // A tick never moves the clock half a day (setting the clock resets the last hour instead): a step back of a few
 // milliseconds read as "almost 24 hours forward" made a new day every tick, 25 days in 9 seconds.
 if (Forward > 12.0) return 0;
 const double First = From < OKStreetDayStartHour ? OKStreetDayStartHour - From : OKStreetDayStartHour + 24.0 - From;
 return Forward >= First ? 1 : 0;
}

inline FString OKStreetCalendarLabel(int32 Day, double Hours) {
 const FDateTime T = OKStreetCalendarDateTime(Day, Hours);
 return OKMsg(TEXT("ui.calendar.datetime_full"), {{TEXT("year"), T.GetYear()}, {TEXT("month"), T.GetMonth()}, {TEXT("day"), T.GetDay()}, {TEXT("weekday"), OKMsg(TEXT("ui.calendar.weekday_name"), {{TEXT("d"), int32(T.GetDayOfWeek())}})}, {TEXT("hour"), T.GetHour()}, {TEXT("minute"), T.GetMinute()}});
}
