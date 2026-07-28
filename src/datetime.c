#undef MAIN

#include "run68.h"

static int64_t virtual_clock_offset;

static BOOL get_local_time(time_t timestamp, struct tm *result)
{
#if defined(WIN32)
	return localtime_s(result, &timestamp) == 0 ? TRUE : FALSE;
#else
	return localtime_r(&timestamp, result) != NULL ? TRUE : FALSE;
#endif
}

BOOL run68_get_virtual_localtime(struct tm *result)
{
	time_t now;

	if (result == NULL)
		return FALSE;
	now = time(NULL);
	if (now == (time_t)-1)
		return FALSE;
	now += (time_t)virtual_clock_offset;
	return get_local_time(now, result);
}

static BOOL set_virtual_localtime(struct tm *requested)
{
	struct tm verified;
	time_t host_now;
	time_t target;
	int year = requested->tm_year;
	int month = requested->tm_mon;
	int day = requested->tm_mday;
	int hour = requested->tm_hour;
	int minute = requested->tm_min;
	int second = requested->tm_sec;

	requested->tm_isdst = -1;
	target = mktime(requested);
	host_now = time(NULL);
	if (target == (time_t)-1 || host_now == (time_t)-1 ||
	    get_local_time(target, &verified) == FALSE ||
	    verified.tm_year != year || verified.tm_mon != month ||
	    verified.tm_mday != day || verified.tm_hour != hour ||
	    verified.tm_min != minute || verified.tm_sec != second)
		return FALSE;
	virtual_clock_offset = (int64_t)target - (int64_t)host_now;
	return TRUE;
}

BOOL run68_set_virtual_date(int year, int month, int day)
{
	struct tm value;

	if (year < 1980 || year > 2107 || month < 1 || month > 12 || day < 1)
		return FALSE;
	if (run68_get_virtual_localtime(&value) == FALSE)
		return FALSE;
	value.tm_year = year - 1900;
	value.tm_mon = month - 1;
	value.tm_mday = day;
	return set_virtual_localtime(&value);
}

BOOL run68_set_virtual_time(int hour, int minute, int second)
{
	struct tm value;

	if (hour < 0 || hour > 23 || minute < 0 || minute > 59 ||
	    second < 0 || second > 59)
		return FALSE;
	if (run68_get_virtual_localtime(&value) == FALSE)
		return FALSE;
	value.tm_hour = hour;
	value.tm_min = minute;
	value.tm_sec = second;
	return set_virtual_localtime(&value);
}

uint64_t run68_elapsed_centiseconds(void)
{
#if defined(WIN32)
	static ULONGLONG start;
	ULONGLONG now = GetTickCount64();

	if (start == 0)
		start = now;
	return (uint64_t)((now - start) / 10u);
#elif defined(CLOCK_MONOTONIC)
	static struct timespec start;
	struct timespec now;
	int64_t elapsed_nanoseconds;

	if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
		return 0;
	if (start.tv_sec == 0 && start.tv_nsec == 0)
		start = now;
	elapsed_nanoseconds = ((int64_t)now.tv_sec - (int64_t)start.tv_sec) *
	                      1000000000 +
	                      ((int64_t)now.tv_nsec - (int64_t)start.tv_nsec);
	return elapsed_nanoseconds <= 0 ? 0 :
	       (uint64_t)(elapsed_nanoseconds / 10000000);
#else
	static time_t start;
	time_t now = time(NULL);

	if (start == 0)
		start = now;
	return now < start ? 0 : (uint64_t)(now - start) * 100u;
#endif
}

BOOL run68_pack_dos_datetime(time_t timestamp, ULong *packed)
{
	struct tm local;
	int year;
	ULong date;
	ULong time;

	if (packed == NULL || get_local_time(timestamp, &local) == FALSE)
		return FALSE;
	year = local.tm_year + 1900;
	if (year < 1980 || year > 2107)
		return FALSE;

	date = (ULong)(year - 1980) << 9;
	date |= (ULong)(local.tm_mon + 1) << 5;
	date |= (ULong)local.tm_mday;
	time = (ULong)local.tm_hour << 11;
	time |= (ULong)local.tm_min << 5;
	time |= (ULong)(local.tm_sec / 2);
	*packed = (date << 16) | time;
	return TRUE;
}

BOOL run68_unpack_dos_datetime(ULong packed, time_t *timestamp)
{
	struct tm local = {0};
	struct tm verified;
	time_t converted;
	int year = (int)((packed >> 25) & 0x7f) + 1980;
	int month = (int)((packed >> 21) & 0x0f);
	int day = (int)((packed >> 16) & 0x1f);
	int hour = (int)((packed >> 11) & 0x1f);
	int minute = (int)((packed >> 5) & 0x3f);
	int second = (int)(packed & 0x1f) * 2;

	if (timestamp == NULL || month < 1 || month > 12 || day < 1 ||
	    hour > 23 || minute > 59 || second > 59)
		return FALSE;

	local.tm_year = year - 1900;
	local.tm_mon = month - 1;
	local.tm_mday = day;
	local.tm_hour = hour;
	local.tm_min = minute;
	local.tm_sec = second;
	local.tm_isdst = -1;
	converted = mktime(&local);
	if (converted == (time_t)-1 ||
	    get_local_time(converted, &verified) == FALSE ||
	    verified.tm_year != year - 1900 || verified.tm_mon != month - 1 ||
	    verified.tm_mday != day || verified.tm_hour != hour ||
	    verified.tm_min != minute || verified.tm_sec != second)
		return FALSE;

	*timestamp = converted;
	return TRUE;
}
