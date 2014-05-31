
// Power savings by syncronizing start points of periodic network activitiy
// frequently triggered by applications during sleep and/or active state.
//

#include <math.h>
#include <aul.h>
#include"alarm-internal.h"

static bool g_white_list = true;
static bool g_white_list_plus_auto_add = false;
static bool g_app_sync_on = false;

#define SYNC_COPRIME_VALUE 	60 // [s] 1 min
#define SYNC_MIN_VALUE 		(5*60)  // [s] 5 min
#define SYNC_MAX_VALUE 	(4*60*60)  // [s] 4 hr
#define INTERVAL_HOUR	(60*60)
#define INTERVAL_HALF_DAY	(12*60*60)

#define MAX_INT_VALUE	2147483647
#define APP_SYNC_LOG	1

#define CSC_BUFFER_SIZE 256

// [ms] global unit interval; Greatest Common Divisor of RepeatIntervals interested
static int g_interval_gcd = SYNC_MAX_VALUE;

static GSList* g_adjustable_repeating_alarms = NULL;
static GSList* g_target_packages = NULL;
//static GSList* g_csc_packages = NULL;

static int __gcd(int first_value, int second_value)
{
	if (second_value == 0)
		return first_value;
	else
		return __gcd(second_value, first_value % second_value);
}

void __convert_time_to_alarm_date_t(time_t time, alarm_date_t* alarm_date)
{
	struct tm time_tm;
	localtime_r(&time, &time_tm);

	alarm_date->year = time_tm.tm_year + 1900;
	alarm_date->month = time_tm.tm_mon + 1;
	alarm_date->day = time_tm.tm_mday;
	alarm_date->hour = time_tm.tm_hour;
	alarm_date->min = time_tm.tm_min;
	alarm_date->sec = time_tm.tm_sec;
}

void __convert_alarm_date_t_to_time(alarm_date_t* alarm_date, time_t* time)
{
	struct tm alarm_tm = {0, };

	alarm_tm.tm_year = alarm_date->year - 1900;
	alarm_tm.tm_mon = alarm_date->month - 1;
	alarm_tm.tm_mday = alarm_date->day;

	alarm_tm.tm_hour = alarm_date->hour;
	alarm_tm.tm_min = alarm_date->min;
	alarm_tm.tm_sec = alarm_date->sec;

	*time = mktime(&alarm_tm);
}

static int __compare_func(const char* a, const char* b)
{
	if (0 == g_strcmp0(a, b)) {
		return 0;
	}

	return 1;
}

static bool __sync_scheduler_look_for_non_adjustable_alarm(GSList* alarmList, __alarm_info_t* __alarm_info)
{
// 기존 alarm 리스트에서 동일한 alarm 의  original alarm 시간이 현재 요청시간과 같으면 return ture;

	GSList *gs_iter = NULL;
	__alarm_info_t *entry = NULL;

	for (gs_iter = alarmList; gs_iter != NULL;
		 gs_iter = g_slist_next(gs_iter)) {

		entry = gs_iter->data;

		if (__alarm_info->start == entry->start) {
			if (!g_strcmp0(g_quark_to_string(__alarm_info->quark_app_unique_name), g_quark_to_string(entry->quark_app_unique_name)) &&
				!g_strcmp0(g_quark_to_string(__alarm_info->quark_app_service_name), g_quark_to_string(entry->quark_app_service_name)) &&
				!g_strcmp0(g_quark_to_string(__alarm_info->quark_dst_service_name), g_quark_to_string(entry->quark_dst_service_name)) &&
				!g_strcmp0(g_quark_to_string(__alarm_info->quark_bundle), g_quark_to_string(entry->quark_bundle))) {
				ALARM_MGR_LOG_PRINT("This is non adjustable alarm");
				return true;
			}
		}
	}

	return false;
}

static int __sync_scheduler_calculate_gcd_of_repeat_intervals(int interval_old, int interval_new)
{
	int new_interval_gcd = interval_old;
	int temp_interval_gcd = __gcd(interval_old, interval_new);

	if (temp_interval_gcd > SYNC_COPRIME_VALUE) {
		if ((temp_interval_gcd % SYNC_MIN_VALUE) == 0) {
			new_interval_gcd = temp_interval_gcd;
		}
	}

	return new_interval_gcd;
}

// list 중에서 interval 이 같은 가장  최신의 alarm 을 리턴 , 같은 것이 없으면 배수 interval 을 가진 alarm 이 리턴
static __alarm_info_t* __sync_scheduler_time_to_next_repeating_alarm(int interval)
{
	int next_alarm = MAX_INT_VALUE;
	int next_alarm_with_same_interval = MAX_INT_VALUE;
	__alarm_info_t* alarm_result = NULL;
	__alarm_info_t* alarm_result_with_same_interval = NULL;
	GSList *gs_iter = NULL;
	__alarm_info_t *entry = NULL;

	bool is_int_same_as_gcd = (interval == g_interval_gcd);
	time_t now_rtc;
	time(&now_rtc);

	for (gs_iter = g_adjustable_repeating_alarms; gs_iter != NULL;
		 gs_iter = g_slist_next(gs_iter)) {
		time_t when = 0;
		entry = gs_iter->data;

		__convert_alarm_date_t_to_time(&entry->alarm_info.start, &when);

		if (now_rtc < (when + g_interval_gcd)) {	// Accept ealier time of one GCD interval
			// Look for the alarm with same interval as GCD
			if (is_int_same_as_gcd) {
				if (when < next_alarm) {
					next_alarm = when;
					alarm_result = entry;
				}
			}
			// Look for the alarm with same interval or multiples of interval
			else {
				if (entry->alarm_info.mode.u_interval.interval != 0) {
					if (entry->alarm_info.mode.u_interval.interval == interval) {
						if (when < next_alarm_with_same_interval) {
							next_alarm_with_same_interval = when;
							alarm_result_with_same_interval = entry;
						}
					}
					else if (((entry->alarm_info.mode.u_interval.interval > interval) && (entry->alarm_info.mode.u_interval.interval % interval == 0)) ||
							 ((entry->alarm_info.mode.u_interval.interval < interval) && (interval % entry->alarm_info.mode.u_interval.interval == 0))) {
						if (when < next_alarm) {
							next_alarm = when;
							alarm_result = entry;
						}
					}
				}
			}
		}
		else {
			// Old alarms eventually left must be removed.
			if ((when + INTERVAL_HALF_DAY) < now_rtc) {
				g_adjustable_repeating_alarms =
					g_slist_remove(g_adjustable_repeating_alarms, gs_iter->data);
			}
		}

	}


	// Next alarm with same interval value goes first.
	if (alarm_result_with_same_interval != NULL)
		alarm_result = alarm_result_with_same_interval;

	return alarm_result;

}

//inputDistance = gcd or alarm's repeated interval
static void __sync_scheduler_adjust_alarm_time(__alarm_info_t* __alarm_info, int input_distance)
{
	int next_alarm_when = MAX_INT_VALUE;
	int distance = input_distance;
	time_t new_time;

	// Retreve the nearest alarm with same RepeatInterval or multiples of RepeatInterval
	if (__alarm_info->alarm_info.mode.u_interval.interval != g_interval_gcd) {
		__alarm_info_t* a = __sync_scheduler_time_to_next_repeating_alarm(__alarm_info->alarm_info.mode.u_interval.interval);
		if (a != NULL) {
			__convert_alarm_date_t_to_time(&a->alarm_info.start, (time_t*)&next_alarm_when);
			// Same RepeatInterval or co-prime RepeatInterval with GCD
			if ((a->alarm_info.mode.u_interval.interval == __alarm_info->alarm_info.mode.u_interval.interval) ||
				(__alarm_info->alarm_info.mode.u_interval.interval % g_interval_gcd != 0)) {
				distance = __alarm_info->alarm_info.mode.u_interval.interval;
			}
			// Multiples of RepeatInterval
			else {
				distance = __gcd(__alarm_info->alarm_info.mode.u_interval.interval, a->alarm_info.mode.u_interval.interval);
			}
		}
	}
	// Retreve the nearest alarm using GCD based RepeatInterval
	if (next_alarm_when == MAX_INT_VALUE) {
		__alarm_info_t* a = __sync_scheduler_time_to_next_repeating_alarm(g_interval_gcd);
		if (a != NULL) {
			__convert_alarm_date_t_to_time(&a->alarm_info.start, (time_t*)&next_alarm_when);
		}
	}

	if (next_alarm_when != MAX_INT_VALUE) {
		ALARM_MGR_LOG_PRINT("next: %d", next_alarm_when);

		// If the requested alarm is after the very next alarm to be triggered,
		// place it somewhere aligned with the point that is one of multiples of
		// requested distance and nearest to the very next alarm.
		if (next_alarm_when <= __alarm_info->start) {
			int count = (__alarm_info->start - next_alarm_when) / distance;
			new_time = next_alarm_when + distance * count;
		}
		// If the requested alarm is before the very next alarm to be triggered,
		// find the earlier aligned point around the requested alarm
		else {
			int count = (next_alarm_when - __alarm_info->start) / distance;
			count++;  // move to one more earlier point
			new_time = next_alarm_when - distance * count;
		}

		__convert_time_to_alarm_date_t(new_time, &__alarm_info->alarm_info.start);
		ALARM_MGR_EXCEPTION_PRINT("AppSync original time : %s", ctime(&__alarm_info->start));
		ALARM_MGR_EXCEPTION_PRINT("AppSync change time : %s", ctime(&new_time));
	}
	else {
		ALARM_MGR_LOG_PRINT("next: MAX_INT_VALUE");
	}
}

//csc 에 의해 targetPackageList 가 구성된 상태에서 white list 에 해당 package 존재 확인
// target_package_list 에서 appid 가 존재하면 return true;
static bool __sync_scheduler_look_for_target_package(GSList* target_package_list, char* appid)
{
	GSList* list = g_slist_find_custom(target_package_list, appid, (GCompareFunc)__compare_func);

	if (appid[0] == 0) {
		return false;
	}

	if (NULL == list) {
		SECURE_LOGD("%s is NOT found in the app sync white list", appid);
		return false;
	} else {
		SECURE_LOGD("%s is found in the app sync white list", appid);
		return true;
	}
}

bool _sync_scheduler_app_sync_on()
{
	return g_app_sync_on;
}

// CSC 와 Account 로 부터 g_target_packages 구성
void _sync_scheduler_init()
{
	int ret = 0;

	char cscAppData[CSC_BUFFER_SIZE] = {0,};
	char** cscAppSyncList = NULL;

	g_target_packages = g_slist_alloc();

	// Check the AppSync feature frm CSC
#if 0
	if (csc_feature_get_bool(CSC_FEATURE_DEF_BOOL_FRAMEWORK_APP_SYNC_DISABLE) == CSC_FEATURE_BOOL_FALSE) {
#else
	if (true) {
#endif
		g_app_sync_on = true;

		// TODO: Get app sync data from csc file (cscAppData)
		//ret = csc_feature_get_str(CSC_FEATURE_DEF_STR_ALARM_MANAGER_APP_SYNC, cscAppData, CSC_BUFFER_SIZE);

		cscAppSyncList = g_strsplit(cscAppData, ",", 0);

		// Check the whitelist mode
		if (0 == g_strcmp0(*cscAppSyncList, "whitelist")) {

			// Load Whitelist of target packages
			for (cscAppSyncList++; NULL != *cscAppSyncList; cscAppSyncList++) {
				ALARM_MGR_LOG_PRINT("CSC data, %s", *cscAppSyncList);
				g_target_packages = g_slist_append(g_target_packages, g_strdup(*cscAppSyncList));
			}
		} else if (0 == g_strcmp0(*cscAppSyncList, "blacklist")) {

			// Disable Whitelist depending on Blacklist selection.
			g_white_list = false;
			g_white_list_plus_auto_add = false;

			// Load Blacklist of target packages
			for (cscAppSyncList++; NULL != *cscAppSyncList; cscAppSyncList++) {
				ALARM_MGR_LOG_PRINT("CSC data, %s", *cscAppSyncList);
				g_target_packages = g_slist_append(g_target_packages, g_strdup(*cscAppSyncList));
			}
		} else { // default lists
			//g_target_packages = g_slist_append(g_target_packages, "com.samsung.helloworld");
		}

		// Free the csc list
		g_strfreev(cscAppSyncList);

		if (APP_SYNC_LOG) {
			GSList *gs_iter = NULL;
			int i = 0;
			for (gs_iter = g_target_packages; gs_iter != NULL;
				 gs_iter = g_slist_next(gs_iter), i++) {
				SECURE_LOGD("target package [%d] : %s",
					i, gs_iter->data);
			}
		}
	} else {
		ALARM_MGR_EXCEPTION_PRINT("App sync is disabled", *cscAppSyncList);
	}
}

void _sync_scheduler_repeating_alarms(__alarm_info_t* __alarm_info)
{
	int ret;

	// true 면 기존과 동일한 alarm 요청이므로, 무시
	bool is_non_adjustable_alarm = __sync_scheduler_look_for_non_adjustable_alarm(
						g_adjustable_repeating_alarms, __alarm_info);

	// Remove this alarm if already scheduled.
	// replace pre existed alarm and remove from the appsync list
	//removeLocked(alarm.operation);

	char appid[255] = {0,};
	ret = aul_app_get_appid_bypid(__alarm_info->pid, appid, sizeof(appid));
	if (ret != AUL_R_OK)
		ALARM_MGR_LOG_PRINT("Cannot get the appid");

	if ( (is_non_adjustable_alarm == false) &&
		(__sync_scheduler_look_for_target_package(g_target_packages, appid) == g_white_list)) {

		if ((__alarm_info->alarm_info.mode.repeat == ALARM_REPEAT_MODE_REPEAT) &&
			(__alarm_info->alarm_info.mode.u_interval.interval >= SYNC_MIN_VALUE) &&
			(__alarm_info->alarm_info.mode.u_interval.interval <= SYNC_MAX_VALUE)) {
			g_interval_gcd = __sync_scheduler_calculate_gcd_of_repeat_intervals(g_interval_gcd,
				__alarm_info->alarm_info.mode.u_interval.interval);

			// If new RepeatInterval belongs to multiples of gIntervalGcd,
			// the alarm will start at the nearest scheduling point
			// around the requested alarm time. The scheduling points are
			// calculated on the unit of gIntervalGcd from the next repeating alarm
			// to be triggered.
			if (__alarm_info->alarm_info.mode.u_interval.interval % g_interval_gcd == 0) {
				__sync_scheduler_adjust_alarm_time(__alarm_info, g_interval_gcd);
				g_adjustable_repeating_alarms = g_slist_append(g_adjustable_repeating_alarms, __alarm_info);
			}
			// If not, the alarm will start at the nearest scheduling point around
			// the requested alarm time. The scheduling points are calculated on
			// the unit of alarm.repeatInterval from the next repeating alarm to be triggered.
			else {
				__sync_scheduler_adjust_alarm_time(__alarm_info, __alarm_info->alarm_info.mode.u_interval.interval);
			}
		}
		else if (__alarm_info->alarm_info.mode.repeat == ALARM_REPEAT_MODE_ONCE) {

			// The package of the alarms registered to account manager could be adjusted
			time_t now;
			time(&now);
			int distance_to_alarm = __alarm_info->start - now;
			int sync_tolerance_value = (distance_to_alarm >= (INTERVAL_HOUR - SYNC_MIN_VALUE)) ? 60 : 10; // [s] 30s or 5s
			int distance_to_alarm_rounded = (int) round((double)distance_to_alarm/(double)sync_tolerance_value) * sync_tolerance_value;

			// Optimize code for com.android.email is omitted.
			// Optimize code for com.google.android.gsf is omitted.

			// Adjust the alarm that occurs periodically in the range
			// between SYNC_MIN_VALUE and SYNC_MAX_VALUE
			if ((distance_to_alarm_rounded <= SYNC_MAX_VALUE) &&
				(distance_to_alarm_rounded >= SYNC_MIN_VALUE) &&
				(distance_to_alarm_rounded % SYNC_MIN_VALUE == 0)) {
				__alarm_info_t* new_alarm;
				g_interval_gcd = __sync_scheduler_calculate_gcd_of_repeat_intervals(g_interval_gcd, distance_to_alarm_rounded);
				new_alarm = malloc(sizeof(__alarm_info_t));

				memcpy(new_alarm, __alarm_info, sizeof(__alarm_info_t));
				new_alarm->alarm_info.mode.u_interval.interval = distance_to_alarm_rounded;
				__sync_scheduler_adjust_alarm_time(new_alarm, g_interval_gcd);
				g_adjustable_repeating_alarms = g_slist_append(g_adjustable_repeating_alarms, new_alarm);
				memcpy(&(__alarm_info->alarm_info.start), &(new_alarm->alarm_info.start), sizeof(alarm_date_t));
			}
		}
		if (APP_SYNC_LOG) {
			GSList *gs_iter = NULL;
			__alarm_info_t *entry = NULL;
			int i = 0;
			time_t due_time = 0;
			struct tm duetime_tm;
			alarm_date_t *start;

			for (gs_iter = g_adjustable_repeating_alarms; gs_iter != NULL;
				 gs_iter = g_slist_next(gs_iter), i++) {

				entry = gs_iter->data;
				start = &entry->alarm_info.start;

				duetime_tm.tm_hour = start->hour;
				duetime_tm.tm_min = start->min;
				duetime_tm.tm_sec = start->sec;

				duetime_tm.tm_year = start->year - 1900;
				duetime_tm.tm_mon = start->month - 1;
				duetime_tm.tm_mday = start->day;

				due_time = mktime(&duetime_tm);

				ALARM_MGR_LOG_PRINT("List[%d] : Interval %d Start %s",
					i, entry->alarm_info.mode.u_interval.interval, ctime(&due_time));
			}
		}
		ALARM_MGR_LOG_PRINT("Interval GCD : %d", g_interval_gcd);
	}
}

void _sync_scheduler_remove_repeating_alarm(alarm_id_t alarm_id)
{
// __alarm_delete 함수에서 호출
//g_adjustable_repeating_alarms 에서 alarm 삭제
	GSList *gs_iter = NULL;
	__alarm_info_t *entry = NULL;

	for (gs_iter = g_adjustable_repeating_alarms; gs_iter != NULL;
		 gs_iter = g_slist_next(gs_iter)) {
		entry = gs_iter->data;

		if (entry->alarm_id == alarm_id) {
			g_adjustable_repeating_alarms =
				g_slist_remove(g_adjustable_repeating_alarms, gs_iter->data);
		}
	}
}

