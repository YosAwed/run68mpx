#undef MAIN

#include "run68.h"

static BOOL get_local_time(time_t timestamp, struct tm *result)
{
#if defined(WIN32)
	return localtime_s(result, &timestamp) == 0 ? TRUE : FALSE;
#else
	return localtime_r(&timestamp, result) != NULL ? TRUE : FALSE;
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
