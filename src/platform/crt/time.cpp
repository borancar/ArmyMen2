/* time.cpp -- time and the local-time arithmetic under it, from the
 * bodies at 0x00465052 (time), 0x00469652 (__loctotime_t), 0x0046B615 and
 * 0x0046B62A (__tzset), 0x0046B888 (_isindst), 0x0046BA34 (cvtdate) and
 * 0x00465D51 (a FILETIME to time_t).
 *
 * The state is the image's: _timezone, _daylight and _dstbias, _tzname's
 * two buffers, the year cache _isindst keeps its two transition rules in,
 * the TIME_ZONE_INFORMATION __tzset last fetched, and time()'s own cache
 * of the minute it last asked about. __tzset runs once, from TZ if the
 * environment has it and from GetTimeZoneInformation otherwise; the
 * native build has no environment table (see env.cpp), so it always takes
 * the second arm there.
 *
 * One thing the original does is not reproducible and is stated: the tm
 * __loctotime_t hands _isindst has its minute and second fields
 * UNINITIALISED -- it writes only year, month, hour and day of the year --
 * so on the boundary hour of a DST transition the answer depends on stack
 * garbage. They are zero here.
 */
#include "crt.h"
#include "../../inject/win32.h"
#include "../../inject/orig.h"
#include "../../game/image.h"

#ifdef AM2_STANDALONE
/* The CRT's two cumulative day-of-year tables at 0x0048D4B4 (_lpdays) and
 * 0x0048D4E8 (_days): the day count at the start of month `m` is table[m-1],
 * with a leading -1 so day-of-month arithmetic lands on the right index. Pure
 * constant data read by crt_cvtdate below, so the blob copies drop -- placed at
 * their VAs and byte-verified by tools/checkplacement.py. */
extern "C" const int32_t am2_crt_lpdays[13] = {
    -1, 30, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334, 365,
};
extern "C" const int32_t am2_crt_days[13] = {
    -1, 30, 58, 89, 119, 150, 180, 211, 242, 272, 303, 333, 364,
};

/* The CRT's time-zone state: _timezone/_daylight/_dstbias at 0x0048D400 (their
 * .data initial values -- __tzset overwrites them at runtime) and the two DST
 * transition-rule cache triples crt_cvtdate fills ({year, yday, ms}, the -1
 * year meaning "not yet computed"). Runtime-WRITTEN, so migrated NON-const; the
 * initial bytes still equal the image and are byte-checked. Grouped as arrays
 * (each holds a non-zero element) so the compiler keeps them in .data -- an
 * all-zero scalar would land in .bss and fall out of the placement flow. */
extern "C" int32_t am2_crt_tz_state[3] = { 28800, 1, -3600 };  /* timezone, daylight, dstbias */
extern "C" int32_t am2_crt_dst_start[3] = { -1, 0, 0 };        /* year, yday, ms */
extern "C" int32_t am2_crt_dst_end[3] = { -1, 0, 0 };
#endif

#define G32(addr)  (*(int32_t *)(uintptr_t)AM2_IMAGE(addr))
#define crt_timezone        G32(ADDR_CRT_TIMEZONE)
#define crt_daylight        G32(ADDR_CRT_DAYLIGHT)
#define crt_dstbias         G32(ADDR_CRT_DSTBIAS)
#define crt_tzname          ((char **)(uintptr_t)AM2_IMAGE(ADDR_CRT_TZNAME))
#define crt_dst_start_year  G32(ADDR_CRT_DST_START_YEAR)
#define crt_dst_start_yday  G32(ADDR_CRT_DST_START_YDAY)
#define crt_dst_start_ms    G32(ADDR_CRT_DST_START_MS)
#define crt_dst_end_year    G32(ADDR_CRT_DST_END_YEAR)
#define crt_dst_end_yday    G32(ADDR_CRT_DST_END_YDAY)
#define crt_dst_end_ms      G32(ADDR_CRT_DST_END_MS)
#define crt_lpdays          ((const int32_t *)(uintptr_t)AM2_IMAGE(ADDR_CRT_LPDAYS))
#define crt_days            ((const int32_t *)(uintptr_t)AM2_IMAGE(ADDR_CRT_DAYS))
#define crt_time_dst_cache  G32(ADDR_CRT_TIME_DST_CACHE)
#define crt_time_st_cache   ((SYSTEMTIME *)(uintptr_t)AM2_IMAGE(ADDR_CRT_TIME_SYSTIME_CACHE))
#define crt_tz_api_used     G32(ADDR_CRT_TZ_API_USED)
#define crt_tz_info         ((TIME_ZONE_INFORMATION *)(uintptr_t)AM2_IMAGE(ADDR_CRT_TZ_INFO))
#define crt_last_tz         (*(char **)(uintptr_t)AM2_IMAGE(ADDR_CRT_LAST_TZ))
#define crt_tzset_done      G32(ADDR_CRT_TZSET_DONE)
#define crt_lc_codepage     (*(uint32_t *)(uintptr_t)AM2_IMAGE(ADDR_CRT_LC_CODEPAGE))

#define CRT_MS_PER_DAY 86400000
/* Seconds from 1900 to 1970 over the arithmetic below: (70 * 365 + 17)
 * days, as the two's-complement constant the original adds. */
#define CRT_EPOCH_ADJUST 0x7C558180u

void __cdecl crt_cvtdate(int32_t trantype, int32_t datetype, int32_t year, int32_t month,
                         int32_t week, int32_t dayofweek, int32_t date, int32_t hour,
                         int32_t min, int32_t sec, int32_t msec)
{
    const int32_t *days = (year & 3) ? crt_days : crt_lpdays;
    int32_t        yday;

    if (datetype == 1) {
        /* The `week`th `dayofweek` of the month, 5 meaning the last: from
         * the weekday of the first of the month, computed off the day
         * count since 1900 as the original does. */
        int32_t first = days[month - 1] + 1;
        int32_t wd = (year * 365 + (year - 1) / 4 + first - 25563) % 7;

        if (wd < dayofweek)
            yday = first + week * 7 - wd + dayofweek - 7;
        else
            yday = first + week * 7 - wd + dayofweek;
        if (week == 5 && yday > days[month])
            yday -= 7;
    } else {
        yday = days[month - 1] + date;
    }

    if (trantype == 1) {
        crt_dst_start_yday = yday;
        crt_dst_start_year = year;
        crt_dst_start_ms = ((hour * 60 + min) * 60 + sec) * 1000 + msec;
    } else {
        int32_t ms;

        crt_dst_end_yday = yday;
        /* The end is expressed in daylight time, so the bias moves it. */
        ms = ((hour * 60 + min) * 60 + crt_dstbias + sec) * 1000 + msec;
        crt_dst_end_ms = ms;
        if (ms < 0) {
            ms += CRT_MS_PER_DAY;
            yday--;
            crt_dst_end_ms = ms;
            crt_dst_end_yday = yday;
        } else if (ms >= CRT_MS_PER_DAY) {
            ms -= CRT_MS_PER_DAY;
            yday++;
            crt_dst_end_ms = ms;
            crt_dst_end_yday = yday;
        }
        crt_dst_end_year = year;
    }
}

int32_t __cdecl crt_isindst(const CRT_TM *tb)
{
    int32_t year, start, end, yday, ms;

    if (crt_daylight == 0)
        return 0;
    year = tb->tm_year;
    if (!(year == crt_dst_start_year && year == crt_dst_end_year)) {
        if (crt_tz_api_used) {
            const TIME_ZONE_INFORMATION *tz = crt_tz_info;

            if (tz->DaylightDate.wYear == 0)
                crt_cvtdate(1, 1, year, tz->DaylightDate.wMonth, tz->DaylightDate.wDay,
                            tz->DaylightDate.wDayOfWeek, 0, tz->DaylightDate.wHour,
                            tz->DaylightDate.wMinute, tz->DaylightDate.wSecond,
                            tz->DaylightDate.wMilliseconds);
            else
                crt_cvtdate(1, 0, year, tz->DaylightDate.wMonth, 0, 0,
                            tz->DaylightDate.wDay, tz->DaylightDate.wHour,
                            tz->DaylightDate.wMinute, tz->DaylightDate.wSecond,
                            tz->DaylightDate.wMilliseconds);
            if (tz->StandardDate.wYear == 0)
                crt_cvtdate(0, 1, year, tz->StandardDate.wMonth, tz->StandardDate.wDay,
                            tz->StandardDate.wDayOfWeek, 0, tz->StandardDate.wHour,
                            tz->StandardDate.wMinute, tz->StandardDate.wSecond,
                            tz->StandardDate.wMilliseconds);
            else
                crt_cvtdate(0, 0, year, tz->StandardDate.wMonth, 0, 0,
                            tz->StandardDate.wDay, tz->StandardDate.wHour,
                            tz->StandardDate.wMinute, tz->StandardDate.wSecond,
                            tz->StandardDate.wMilliseconds);
        } else {
            /* The US rule of the day: first Sunday in April at 2:00 to the
             * last Sunday in October at 2:00. */
            crt_cvtdate(1, 1, year, 4, 1, 0, 0, 2, 0, 0, 0);
            crt_cvtdate(0, 1, year, 10, 5, 0, 0, 2, 0, 0, 0);
        }
    }

    start = crt_dst_start_yday;
    end = crt_dst_end_yday;
    yday = tb->tm_yday;
    if (start < end) {
        if (yday < start || yday > end)
            return 0;
        if (yday > start && yday < end)
            return 1;
    } else {
        if (yday < end || yday > start)
            return 1;
        if (yday > end && yday < start)
            return 0;
    }
    /* On a transition day: by the millisecond. */
    ms = ((tb->tm_hour * 60 + tb->tm_min) * 60 + tb->tm_sec) * 1000;
    if (yday == start)
        return ms >= crt_dst_start_ms;
    return ms < crt_dst_end_ms;
}

/* The name buffers are 64 bytes; the wide name is converted into the
 * first 63 and the last is forced to NUL, or the name is emptied when the
 * conversion fails or had to substitute a character. */
static void tz_name(char *dst, const WCHAR *src)
{
    BOOL used = FALSE;

    if (WideCharToMultiByte(crt_lc_codepage, WC_COMPOSITECHECK | WC_SEPCHARS, src, -1,
                            dst, 63, NULL, &used) != 0 && !used)
        dst[63] = 0;
    else
        dst[0] = 0;
}

void __cdecl crt_tzset_body(void)
{
    const char *tz;
    int32_t     neg = 0;

    crt_tz_api_used = 0;
    crt_dst_end_year = -1;
    crt_dst_start_year = -1;

    tz = crt_getenv("TZ");
    if (tz == NULL) {
        TIME_ZONE_INFORMATION *info = crt_tz_info;

        if (GetTimeZoneInformation(info) == TIME_ZONE_ID_INVALID)
            return;
        crt_timezone = info->Bias * 60;
        crt_tz_api_used = 1;
        if (info->StandardDate.wMonth != 0)
            crt_timezone += info->StandardBias * 60;
        if (info->DaylightDate.wMonth != 0 && info->DaylightBias != 0) {
            crt_daylight = 1;
            crt_dstbias = (info->DaylightBias - info->StandardBias) * 60;
        } else {
            crt_daylight = 0;
            crt_dstbias = 0;
        }
        tz_name(crt_tzname[0], info->StandardName);
        tz_name(crt_tzname[1], info->DaylightName);
        return;
    }
    if (*tz == 0)
        return;

    /* Parsed once per distinct value: a copy of the string is kept. */
    if (crt_last_tz != NULL && crt_strcmp(tz, crt_last_tz) == 0)
        return;
    crt_free(crt_last_tz);
    crt_last_tz = (char *)crt_malloc((uint32_t)crt_strlen(tz) + 1);
    if (crt_last_tz == NULL)
        return;
    crt_strcpy(crt_last_tz, tz);

    /* "PST8PDT": three letters, [-]hours[:minutes[:seconds]], letters. */
    crt_strncpy(crt_tzname[0], tz, 3);
    crt_tzname[0][3] = 0;
    tz += 3;
    if (*tz == '-') {
        neg = 1;
        tz++;
    }
    crt_timezone = crt_atol(tz) * 3600;
    while (*tz == '+' || (*tz >= '0' && *tz <= '9'))
        tz++;
    if (*tz == ':') {
        tz++;
        crt_timezone += crt_atol(tz) * 60;
        while (*tz >= '0' && *tz <= '9')
            tz++;
        if (*tz == ':') {
            tz++;
            crt_timezone += crt_atol(tz);
            while (*tz >= '0' && *tz <= '9')
                tz++;
        }
    }
    if (neg)
        crt_timezone = -crt_timezone;
    /* _daylight is the first character of the daylight name: nonzero
     * whenever there is one. */
    crt_daylight = (int32_t)(int8_t)*tz;
    if (crt_daylight) {
        crt_strncpy(crt_tzname[1], tz, 3);
        crt_tzname[1][3] = 0;
    } else {
        crt_tzname[1][0] = 0;
    }
}

void __cdecl crt_tzset(void)
{
    if (crt_tzset_done == 0) {
        crt_tzset_body();
        crt_tzset_done++;
    }
}

int32_t __cdecl crt_loctotime_t(int32_t yr, int32_t mo, int32_t dy, int32_t hr, int32_t mn, int32_t sec, int32_t dst)
{
    CRT_TM   tb;
    int32_t  tmpdays;
    uint32_t tmptim;

    yr -= 1900;
    if (yr < 70 || yr > 138)
        return -1;
    tmpdays = dy + crt_days[mo - 1];
    if (!(yr & 3) && mo > 2)
        tmpdays++;
    crt_tzset();
    tmptim = (uint32_t)((((yr * 365 + (yr - 1) / 4 + tmpdays) * 24 + hr) * 60 + mn) * 60
                        + crt_timezone) + (uint32_t)sec + CRT_EPOCH_ADJUST;

    tb.tm_sec = 0;      /* not written by the original: stack garbage */
    tb.tm_min = 0;      /* likewise */
    tb.tm_hour = hr;
    tb.tm_mday = 0;
    tb.tm_mon = mo - 1;
    tb.tm_year = yr;
    tb.tm_wday = 0;
    tb.tm_yday = tmpdays;
    tb.tm_isdst = 0;
    if (dst == 1 || (dst == -1 && crt_daylight && crt_isindst(&tb)))
        tmptim += (uint32_t)crt_dstbias;
    return (int32_t)tmptim;
}

int32_t __cdecl crt_timet_from_ft(const void *ftp)
{
    const FILETIME *ft = (const FILETIME *)ftp;
    FILETIME        local;
    SYSTEMTIME      st;

    if (ft->dwLowDateTime == 0 && ft->dwHighDateTime == 0)
        return -1;
    if (!FileTimeToLocalFileTime(ft, &local))
        return -1;
    if (!FileTimeToSystemTime(&local, &st))
        return -1;
    return crt_loctotime_t(st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, -1);
}

int32_t __cdecl crt_time(int32_t *out)
{
    SYSTEMTIME  lt, st;
    SYSTEMTIME *cache = crt_time_st_cache;
    int32_t     dst, tim;

    GetLocalTime(&lt);
    GetSystemTime(&st);
    /* The DST question is answered once per minute of system time. */
    if (st.wMinute == cache->wMinute && st.wHour == cache->wHour && st.wDay == cache->wDay
        && st.wMonth == cache->wMonth && st.wYear == cache->wYear) {
        dst = crt_time_dst_cache;
    } else {
        TIME_ZONE_INFORMATION tz;
        DWORD                 r = GetTimeZoneInformation(&tz);

        if (r == TIME_ZONE_ID_INVALID)
            dst = -1;
        else if (r == TIME_ZONE_ID_DAYLIGHT && tz.DaylightDate.wMonth != 0 && tz.DaylightBias != 0)
            dst = 1;
        else
            dst = 0;
        *cache = st;
        crt_time_dst_cache = dst;
    }
    tim = crt_loctotime_t(lt.wYear, lt.wMonth, lt.wDay, lt.wHour, lt.wMinute, lt.wSecond, dst);
    if (out)
        *out = tim;
    return tim;
}
