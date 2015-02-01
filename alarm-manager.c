/*
 *  alarm-manager
 *
 * Copyright (c) 2000 - 2011 Samsung Electronics Co., Ltd. All rights reserved.
 *
 * Contact: Venkatesha Sarpangala <sarpangala.v@samsung.com>, Jayoun Lee <airjany@samsung.com>,
 * Sewook Park <sewook7.park@samsung.com>, Jaeho Lee <jaeho81.lee@samsung.com>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */



#define _BSD_SOURCE /*for localtime_r*/
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <signal.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/timerfd.h>
#include <poll.h>
#include <stdint.h>

#include <glib.h>
#if !GLIB_CHECK_VERSION (2, 31, 0)
#include <glib/gmacros.h>
#endif

#include "gio/gio.h"
#include "alarm.h"
#include "alarm-internal.h"
#include "alarm-mgr-stub.h"

#include <aul.h>
#include <bundle.h>
#include <security-server.h>
#include <db-util.h>
#include <vconf.h>
#include <vconf-keys.h>
#include <dlfcn.h>
#include <pkgmgr-info.h>

#define SIG_TIMER 0x32
#define WAKEUP_ALARM_APP_ID       "com.samsung.alarm.ALARM"
	/* alarm ui application's alarm's dbus_service name instead of 21
	   (alarm application's app_id) value */

__alarm_server_context_t alarm_context;
bool g_dummy_timer_is_set = FALSE;

GSList *g_scheduled_alarm_list = NULL;
GSList *g_expired_alarm_list = NULL;

#ifndef RTC_WKALM_BOOT_SET
#define RTC_WKALM_BOOT_SET _IOW('p', 0x80, struct rtc_wkalrm)
#endif

/*	2008. 6. 3 sewook7.park
       When the alarm becoms sleeping mode, alarm timer is not expired.
       So using RTC, phone is awaken before alarm rings.
*/
#define __WAKEUP_USING_RTC__
#ifdef __WAKEUP_USING_RTC__
#include <errno.h>
#include <linux/rtc.h>
#include <sys/ioctl.h>
#include <fcntl.h>

#define ALARM_RTC_WAKEUP	0

#define ALARM_IOW(c, type, size)            _IOW('a', (c) | ((type) << 4), size)
#define ALARM_SET(type)             ALARM_IOW(2, type, struct timespec)
#define ALARM_SET_RTC               _IOW('a', 5, struct timespec)
#define ALARM_CLEAR(type)           _IO('a', 0 | ((type) << 4))

// For module log
#define ALARMMGR_LOG_BUFFER_SIZE	10000
#define ALARMMGR_LOG_BUFFER_STRING_SIZE	200
#define ALARMMGR_LOG_TAG_SIZE		20
#define ALARMMGR_LOG_MESSAGE_SIZE	120
#define ALARMMGR_LOG_FILE_PATH	"/var/log/alarmmgr.log"
static int log_index = 0;
static int log_fd = 0;

// display lock and unlock
#define DEVICED_BUS_NAME "org.tizen.system.deviced"
#define DEVICED_PATH_DISPLAY		"/Org/Tizen/System/DeviceD/Display"
#define DEVICED_INTERFACE_DISPLAY	"org.tizen.system.deviced.display"
#define DEVICED_LOCK_STATE		"lockstate"
#define DEVICED_UNLOCK_STATE	"unlockstate"
#define DEVICED_DBUS_REPLY_TIMEOUT	(120*1000)
#define DEVICED_LCD_OFF		"lcdoff"
#define DEVICED_STAY_CUR_STATE	"staycurstate"
#define DEVICED_SLEEP_MARGIN		"sleepmargin"

static const char default_rtc[] = "/dev/alarm";

static int gfd = 0;

#endif				/*__WAKEUP_USING_RTC__*/

/*  GDBus Declaration */
#define ALARM_MGR_DBUS_PATH	"/com/samsung/alarm/manager"
#define ALARM_MGR_DBUS_NAME	"com.samsung.alarm.manager"
GDBusObjectManagerServer *alarmmgr_server = NULL;
static AlarmManager* interface = NULL;

static bool __alarm_add_to_list(__alarm_info_t *__alarm_info);
static void __alarm_generate_alarm_id(__alarm_info_t *__alarm_info, alarm_id_t *alarm_id);
static bool __alarm_update_in_list(int pid, alarm_id_t alarm_id,
				   __alarm_info_t *__alarm_info,
				   int *error_code);
static bool __alarm_remove_from_list(int pid, alarm_id_t alarm_id,
				     int *error_code);
static bool __alarm_set_start_and_end_time(alarm_info_t *alarm_info,
					   __alarm_info_t *__alarm_info);
static bool __alarm_update_due_time_of_all_items_in_list(double diff_time);
static bool __alarm_create(alarm_info_t *alarm_info, alarm_id_t *alarm_id,
			   int pid, char *app_service_name, char *app_service_name_mod,
			   const char *dst_service_name, const char *dst_service_name_mod, int *error_code);
static bool __alarm_create_appsvc(alarm_info_t *alarm_info, alarm_id_t *alarm_id,
			   int pid, char *bundle_data, int *error_code);

static bool __alarm_delete(int pid, alarm_id_t alarm_id, int *error_code);
static bool __alarm_update(int pid, char *app_service_name, alarm_id_t alarm_id,
			   alarm_info_t *alarm_info, int *error_code);
static void __alarm_send_noti_to_application(const char *app_service_name, alarm_id_t alarm_id);
static void __alarm_expired();
static gboolean __alarm_handler_idle(gpointer user_data);
static void __clean_registry();
static bool __alarm_manager_reset();
static void __on_system_time_external_changed(keynode_t *node, void *data);
static void __initialize_timer();
static void __initialize_alarm_list();
static void __initialize_scheduled_alarm_list();
static bool __initialize_noti();

static bool __initialize_dbus();
static bool __initialize_db();
static void __initialize();
void on_bus_name_owner_changed(GDBusConnection *connection, const gchar *sender_name, const gchar *object_path,
             const gchar *interface_name, const gchar *signal_name, GVariant *parameters, gpointer user_data);
bool __get_caller_unique_name(int pid, char *unique_name);

static void __initialize_module_log(void);
static bool __save_module_log(const char *tag, const char *messgae);

int __display_lock_state(char *state, char *flag, unsigned int timeout);
int __display_unlock_state(char *state, char *flag);

static void __rtc_set()
{
#ifdef __WAKEUP_USING_RTC__
	const char *rtc = default_rtc;
	struct rtc_wkalrm rtc_wk;
	struct tm due_tm;
	struct timespec alarm_time;
	char log_message[ALARMMGR_LOG_MESSAGE_SIZE] = {0,};
#ifdef _SIMUL			/*if build is simulator, we don't need to set
				   RTC because RTC does not work in simulator.*/
	ALARM_MGR_EXCEPTION_PRINT("because it is simulator's mode, we don't set RTC.");
	return;
#endif

	if (gfd == 0) {
		gfd = open(rtc, O_RDWR);
		if (gfd == -1) {
			ALARM_MGR_EXCEPTION_PRINT("RTC open failed.");
			return;
		}
	}

	/* Read the RTC time/date */
	int retval = 0;
	char *timebuf = ctime(&alarm_context.c_due_time);
	timebuf[strlen(timebuf) - 1] = '\0';	// to avoid new line
	sprintf(log_message, "wakeup time: %d, %s", alarm_context.c_due_time, timebuf);

	ALARM_MGR_LOG_PRINT("alarm_context.c_due_time is %d.", alarm_context.c_due_time);

	if (alarm_context.c_due_time != -1) {
		retval = ioctl(gfd, ALARM_CLEAR(ALARM_RTC_WAKEUP));
		if (retval == -1) {
			if (errno == ENOTTY) {
				ALARM_MGR_EXCEPTION_PRINT("Alarm IRQs is not supported.");
			}
			ALARM_MGR_EXCEPTION_PRINT("ALARM_CLEAR ioctl is failed. errno = %s", strerror(errno));
			return;
		}
		ALARM_MGR_EXCEPTION_PRINT("[alarm-server]ALARM_CLEAR ioctl is successfully done.");

		time_t due_time = alarm_context.c_due_time;
		gmtime_r(&due_time, &due_tm);

		ALARM_MGR_EXCEPTION_PRINT("Setted RTC Alarm date/time is %d-%d-%d, %02d:%02d:%02d (UTC).",
			due_tm.tm_mday, due_tm.tm_mon + 1, due_tm.tm_year + 1900,
			due_tm.tm_hour, due_tm.tm_min, due_tm.tm_sec);

		alarm_time.tv_sec = due_time - 1;
		alarm_time.tv_nsec = 500000000;	// Wakeup is 500ms faster than expiring time to correct RTC error.
		retval = ioctl(gfd, ALARM_SET(ALARM_RTC_WAKEUP), &alarm_time);
		if (retval == -1) {
			if (errno == ENOTTY) {
				ALARM_MGR_EXCEPTION_PRINT("Alarm IRQs is not supported.");
			}
			ALARM_MGR_EXCEPTION_PRINT("RTC ALARM_SET ioctl is failed. errno = %s", strerror(errno));
			__save_module_log("FAIL: SET RTC", log_message);
			return;
		}
		ALARM_MGR_EXCEPTION_PRINT("[alarm-server]RTC ALARM_SET ioctl is successfully done.");
		__save_module_log("SET RTC", log_message);
	}
	else {
		ALARM_MGR_EXCEPTION_PRINT("[alarm-server]alarm_context.c_due_time is"
			"less than 10 sec. RTC alarm does not need to be set");
	}
#endif				/* __WAKEUP_USING_RTC__ */
	return;
}

int _set_rtc_time(time_t _time)
{
	int ret = 0;
	const char *rtc0 = default_rtc;
	struct timespec rtc_time;
	char log_tag[ALARMMGR_LOG_TAG_SIZE] = {0,};
	char log_message[ALARMMGR_LOG_MESSAGE_SIZE] = {0,};

	if (gfd == 0) {
		gfd = open(rtc0, O_RDWR);

		if (gfd == -1) {
			ALARM_MGR_LOG_PRINT("error to open /dev/alarm.");
			perror("\t");
		}
	}

	rtc_time.tv_sec = _time;
	rtc_time.tv_nsec = 0;

	strncpy(log_tag, "SET RTC", strlen("SET RTC"));
	char *timebuf = ctime(&_time);
	timebuf[strlen(timebuf) - 1] = '\0';	// to avoid new line
	sprintf(log_message, "rtc time = %d, %s", _time, timebuf);

	ret = ioctl(gfd, ALARM_SET_RTC, &rtc_time);
	if (ret == -1) {
		ALARM_MGR_LOG_PRINT("ALARM_SET_RTC ioctl is failed. errno = %s", strerror(errno));
		strncpy(log_tag, "FAIL: SET RTC", strlen("FAIL: SET RTC"));

		perror("\t");
	}

	__save_module_log(log_tag, log_message);

	return 1;
}

bool __alarm_clean_list()
{
	g_slist_free_full(alarm_context.alarms, g_free);
	return true;
}

static void __alarm_generate_alarm_id(__alarm_info_t *__alarm_info, alarm_id_t *alarm_id)
{
	bool unique_id = false;
	__alarm_info_t *entry = NULL;
	GSList *iter = NULL;

	srand((unsigned int)time(NULL));
	__alarm_info->alarm_id = (rand() % INT_MAX) + 1;
	ALARM_MGR_LOG_PRINT("__alarm_info->alarm_id is %d", __alarm_info->alarm_id);

	while (unique_id == false) {
		unique_id = true;

		for (iter = alarm_context.alarms; iter != NULL;
		     iter = g_slist_next(iter)) {
			entry = iter->data;
			if (entry->alarm_id == __alarm_info->alarm_id) {
				__alarm_info->alarm_id++;
				unique_id = false;
			}
		}
	}

	*alarm_id = __alarm_info->alarm_id;

	return;
}

static bool __alarm_add_to_list(__alarm_info_t *__alarm_info)
{
	alarm_info_t *alarm_info = &__alarm_info->alarm_info;
	__alarm_info_t *entry = NULL;
	GSList *iter = NULL;

	ALARM_MGR_LOG_PRINT("[alarm-server]: Before add alarm_id(%d)", __alarm_info->alarm_id);

	alarm_context.alarms = g_slist_append(alarm_context.alarms, __alarm_info);
	ALARM_MGR_EXCEPTION_PRINT("[alarm-server]: After add alarm_id(%d)", __alarm_info->alarm_id);

	// alarm list
	for (iter = alarm_context.alarms; iter != NULL; iter = g_slist_next(iter)) {
		entry = iter->data;
		ALARM_MGR_LOG_PRINT("[alarm-server]: alarm_id(%d).", entry->alarm_id);
	}

	if (!(alarm_info->alarm_type & ALARM_TYPE_VOLATILE)) {
		if (!_save_alarms(__alarm_info)) {
			ALARM_MGR_EXCEPTION_PRINT("Saving alarm_id(%d) in DB is failed.", __alarm_info->alarm_id);
		}
	}

	return true;
}

static bool __alarm_update_in_list(int pid, alarm_id_t alarm_id,
				   __alarm_info_t *__alarm_info,
				   int *error_code)
{
	bool found = false;
	alarm_info_t *alarm_info = &__alarm_info->alarm_info;
	GSList *iter = NULL;
	__alarm_info_t *entry = NULL;

	for (iter = alarm_context.alarms; iter != NULL;
	     iter = g_slist_next(iter)) {
		entry = iter->data;
		if (entry->alarm_id == alarm_id) {

			found = true;
			__alarm_info->quark_app_unique_name =
			    entry->quark_app_unique_name;
			__alarm_info->quark_dst_service_name =
			    entry->quark_dst_service_name;
			memcpy(entry, __alarm_info, sizeof(__alarm_info_t));

			break;
		}
	}

	if (!found) {
		if (error_code)
			*error_code = ERR_ALARM_INVALID_ID;
		return false;
	}

	if (!(alarm_info->alarm_type & ALARM_TYPE_VOLATILE)) {
		if (!_update_alarms(__alarm_info)) {
			ALARM_MGR_EXCEPTION_PRINT("Updating alarm_id(%d) in DB is failed.", __alarm_info->alarm_id);
		}
	}

	return true;
}

static bool __alarm_remove_from_list(int pid, alarm_id_t alarm_id,
				     int *error_code)
{
	bool found = false;

	alarm_info_t *alarm_info = NULL;

	GSList *iter = NULL;
	__alarm_info_t *entry = NULL;

	/*list alarms */
	ALARM_MGR_LOG_PRINT("[alarm-server]: before del : alarm id(%d)", alarm_id);

	for (iter = alarm_context.alarms; iter != NULL; iter = g_slist_next(iter)) {
		entry = iter->data;
		if (entry->alarm_id == alarm_id) {
			alarm_info = &entry->alarm_info;

			ALARM_MGR_EXCEPTION_PRINT("[alarm-server]:Remove alarm id(%d)", entry->alarm_id);

			if (!(alarm_info->alarm_type & ALARM_TYPE_VOLATILE)) {
				_delete_alarms(alarm_id);
			}

			alarm_context.alarms = g_slist_remove(alarm_context.alarms, iter->data);
			g_free(entry);
			found = true;
			break;
		}

	}

	ALARM_MGR_LOG_PRINT("[alarm-server]: after del\n");

	if (!found) {
		if (error_code)
			*error_code = ERR_ALARM_INVALID_ID;
		return false;
	}

	return true;
}

static bool __alarm_set_start_and_end_time(alarm_info_t *alarm_info,
					   __alarm_info_t *__alarm_info)
{
	alarm_date_t *start = &alarm_info->start;
	alarm_date_t *end = &alarm_info->end;

	struct tm alarm_tm = { 0, };

	if (start->year != 0) {
		alarm_tm.tm_year = start->year - 1900;
		alarm_tm.tm_mon = start->month - 1;
		alarm_tm.tm_mday = start->day;

		alarm_tm.tm_hour = start->hour;
		alarm_tm.tm_min = start->min;
		alarm_tm.tm_sec = start->sec;
		alarm_tm.tm_isdst = -1;

		__alarm_info->start = mktime(&alarm_tm);
	} else {
		__alarm_info->start = 0;
	}

	if (end->year != 0) {
		alarm_tm.tm_year = end->year - 1900;
		alarm_tm.tm_mon = end->month - 1;
		alarm_tm.tm_mday = end->day;

		alarm_tm.tm_hour = end->hour;
		alarm_tm.tm_min = end->min;
		alarm_tm.tm_sec = end->sec;

		__alarm_info->end = mktime(&alarm_tm);
	} else {
		__alarm_info->end = 0;
	}

	return true;
}

/*
static bool alarm_get_tz_info(int *gmt_idx, int *dst)
{
	GConfValue *value1 = NULL;
	GConfValue *value2 = NULL;
	GConfClient* gConfClient = NULL;
	GError* err = NULL;

	gConfClient = gconf_client_get_default();

	if(gConfClient) {
		value1 = gconf_client_get(gConfClient, SETTINGS_CLOCKTIMEZONE,
									&err);
		if (err) {
			ALARM_MGR_LOG_PRINT("__on_system_time_changed:
			gconf_client_get() failed:
			error:[%s]\n", err->message);
			g_error_free(err);
			err = NULL;
		}
		*gmt_idx = gconf_value_get_int(value1);
		ALARM_MGR_LOG_PRINT("gconf return gmt_idx =%d\n ", *gmt_idx);

		value2 = gconf_client_get(gConfClient,
			SETTINGS_DAYLIGHTSTATUS, &err);
		if (err) {
			ALARM_MGR_LOG_PRINT("__on_system_time_changed:
		gconf_client_get() failed: error:[%s]\n", err->message);
		g_error_free(err);
		err = NULL;
	}

	*dst = gconf_value_get_int(value2);
	ALARM_MGR_LOG_PRINT("gconf return dst =%d\n ", *dst);

	if(gConfClient != NULL) {
		g_object_unref(gConfClient);
		gConfClient = NULL;
		}
	}
	else
		ALARM_MGR_LOG_PRINT("check the gconf setting failed!!!!! \n ");

	if(value1) {
		gconf_value_free(value1);
		value1 = NULL;
	}
	if(value2) {
		gconf_value_free(value2);
		value2 = NULL;
	}

	return true;
}
*/

static bool __alarm_update_due_time_of_all_items_in_list(double diff_time)
{
	time_t current_time;
	time_t min_time = -1;
	time_t due_time = 0;
	GSList *iter = NULL;
	__alarm_info_t *entry = NULL;
	struct tm *p_time = NULL ;
	struct tm due_time_result ;
	struct tm fixed_time = { 0, };

	for (iter = alarm_context.alarms; iter != NULL;
	     iter = g_slist_next(iter)) {
		entry = iter->data;
		alarm_info_t *alarm_info = &(entry->alarm_info);
		if (alarm_info->alarm_type & ALARM_TYPE_RELATIVE) {
			/*diff_time ó\B8\AE */

			entry->due_time += diff_time;

			alarm_date_t *start = &alarm_info->start; /**< start
							time of the alarm */
			alarm_date_t *end = &alarm_info->end;;
						/**< end time of the alarm */

			tzset();
			p_time = localtime_r(&entry->due_time, &due_time_result);
			if (p_time != NULL) {
				start->year = p_time->tm_year + 1900;
				start->month = p_time->tm_mon + 1;
				start->day = p_time->tm_mday;
				start->hour = p_time->tm_hour;
				start->min = p_time->tm_min;
				start->sec = p_time->tm_sec;

				end->year = p_time->tm_year + 1900;
				end->month = p_time->tm_mon + 1;
				end->day = p_time->tm_mday;


				memset(&fixed_time, 0, sizeof(fixed_time));
				fixed_time.tm_year = p_time->tm_year;
				fixed_time.tm_mon = p_time->tm_mon;
				fixed_time.tm_mday = p_time->tm_mday;
				fixed_time.tm_hour = 0;
				fixed_time.tm_min = 0;
				fixed_time.tm_sec = 0;
			}
			entry->start = mktime(&fixed_time);

			fixed_time.tm_hour = 23;
			fixed_time.tm_min = 59;
			fixed_time.tm_sec = 59;

			entry->end = mktime(&fixed_time);

			ALARM_MGR_LOG_PRINT("alarm_info->alarm_type is "
					    "ALARM_TYPE_RELATIVE\n");

			_update_alarms(entry);
		}

		_alarm_next_duetime(entry);
		ALARM_MGR_LOG_PRINT("entry->due_time is %d\n", entry->due_time);
	}

	time(&current_time);

	for (iter = alarm_context.alarms; iter != NULL; iter = g_slist_next(iter)) {
		entry = iter->data;
		due_time = entry->due_time;

		double interval = 0;

		ALARM_MGR_LOG_PRINT("alarm[%d] with duetime(%u) at "
		"current(%u)\n", entry->alarm_id, due_time, current_time);
		if (due_time == 0) {	/* 0 means this alarm has been disabled */
			continue;
		}

		interval = difftime(due_time, current_time);

		if (interval <= 0) {
			ALARM_MGR_EXCEPTION_PRINT("The duetime of alarm(%d) is OVER.", entry->alarm_id);
			continue;
		}

		interval = difftime(due_time, min_time);

		if ((interval < 0) || min_time == -1) {
			min_time = due_time;
		}

	}

	alarm_context.c_due_time = min_time;

	return true;
}

static bool __alarm_create_appsvc(alarm_info_t *alarm_info, alarm_id_t *alarm_id,
			   int pid, char *bundle_data, int *error_code)
{
	time_t current_time;
	time_t due_time;
	struct tm ts_ret;
	char due_time_r[100] = { 0 };
	char app_name[512] = { 0 };
	bundle *b;
	char caller_appid[256] = { 0 };
	char* callee_appid = NULL;
	char* caller_pkgid = NULL;
	char* callee_pkgid = NULL;
	pkgmgrinfo_pkginfo_h caller_handle;
	pkgmgrinfo_pkginfo_h callee_handle;
	bundle_raw *b_data = NULL;
	int datalen = 0;

	__alarm_info_t *__alarm_info = NULL;

	__alarm_info = malloc(sizeof(__alarm_info_t));
	if (__alarm_info == NULL) {
		SECURE_LOGE("Caution!! app_pid=%d, malloc failed. it seems to be OOM.", pid);
		*error_code = ERR_ALARM_SYSTEM_FAIL;
		return false;
	}

	__alarm_info->pid = pid;
	__alarm_info->alarm_id = -1;

	if (!__get_caller_unique_name(pid, app_name)) {
		*error_code = ERR_ALARM_SYSTEM_FAIL;
		free(__alarm_info);
		return false;
	}
	__alarm_info->quark_app_unique_name = g_quark_from_string(app_name);

	// Get caller_appid and callee_appid to get each package id
	// caller
	__alarm_info->quark_caller_pkgid = g_quark_from_string("null");

	if (aul_app_get_appid_bypid(pid, caller_appid, 256) == AUL_R_OK) {
		if (pkgmgrinfo_appinfo_get_appinfo(caller_appid, &caller_handle) == PMINFO_R_OK) {
			if (pkgmgrinfo_appinfo_get_pkgid(caller_handle, &caller_pkgid) == PMINFO_R_OK) {
				if (caller_pkgid) {
					__alarm_info->quark_caller_pkgid = g_quark_from_string(caller_pkgid);
				}
			}
			pkgmgrinfo_appinfo_destroy_appinfo(caller_handle);
		}
	}

	// callee
	__alarm_info->quark_callee_pkgid = g_quark_from_string("null");

	b = bundle_decode((bundle_raw *)bundle_data, strlen(bundle_data));
	callee_appid = appsvc_get_appid(b);
	if (pkgmgrinfo_appinfo_get_appinfo(callee_appid, &callee_handle) == PMINFO_R_OK) {
		if (pkgmgrinfo_appinfo_get_pkgid(callee_handle, &callee_pkgid) == PMINFO_R_OK) {
			if (callee_pkgid) {
				__alarm_info->quark_callee_pkgid = g_quark_from_string(callee_pkgid);
			}
		}
		pkgmgrinfo_appinfo_destroy_appinfo(callee_handle);
	}

	SECURE_LOGD("caller_pkgid = %s, callee_pkgid = %s",
		g_quark_to_string(__alarm_info->quark_caller_pkgid), g_quark_to_string(__alarm_info->quark_callee_pkgid));

	bundle_encode(b, &b_data, &datalen);
	__alarm_info->quark_bundle=g_quark_from_string(b_data);
	__alarm_info->quark_app_service_name = g_quark_from_string("null");
	__alarm_info->quark_dst_service_name = g_quark_from_string("null");
	__alarm_info->quark_app_service_name_mod = g_quark_from_string("null");
	__alarm_info->quark_dst_service_name_mod = g_quark_from_string("null");

	bundle_free(b);
	if (b_data) {
		free(b_data);
		b_data = NULL;
	}

	__alarm_set_start_and_end_time(alarm_info, __alarm_info);
	memcpy(&(__alarm_info->alarm_info), alarm_info, sizeof(alarm_info_t));
	__alarm_generate_alarm_id(__alarm_info, alarm_id);

	time(&current_time);

	if (alarm_context.c_due_time < current_time) {
		ALARM_MGR_EXCEPTION_PRINT("Caution!! alarm_context.c_due_time "
		"(%d) is less than current time(%d)", alarm_context.c_due_time, current_time);
		alarm_context.c_due_time = -1;
	}

	due_time = _alarm_next_duetime(__alarm_info);
	if (__alarm_add_to_list(__alarm_info) == false) {
		free(__alarm_info);
		*error_code = ERR_ALARM_SYSTEM_FAIL;
		return false;
	}

	if (due_time == 0) {
		ALARM_MGR_EXCEPTION_PRINT("[alarm-server]:Create a new alarm: "
		"due_time is 0, alarm(%d) \n", *alarm_id);
		return true;
	} else if (current_time == due_time) {
		ALARM_MGR_EXCEPTION_PRINT("[alarm-server]:Create alarm: "
		     "current_time(%d) is same as due_time(%d)", current_time,
		     due_time);
		return true;
	} else if (difftime(due_time, current_time) < 0) {
		ALARM_MGR_EXCEPTION_PRINT("[alarm-server]: Expired Due Time.[Due time=%d, Current Time=%d]!!!Do not add to schedule list\n", due_time, current_time);
		return true;
	} else {
		localtime_r(&due_time, &ts_ret);
		strftime(due_time_r, 30, "%c", &ts_ret);
		SECURE_LOGD("[alarm-server]:Create a new alarm: "
				    "alarm(%d) due_time(%s)", *alarm_id,
				    due_time_r);
	}

	ALARM_MGR_EXCEPTION_PRINT("[alarm-server]:alarm_context.c_due_time(%d), due_time(%d)", alarm_context.c_due_time, due_time);

	if (alarm_context.c_due_time == -1 || due_time < alarm_context.c_due_time) {
		_clear_scheduled_alarm_list();
		_add_to_scheduled_alarm_list(__alarm_info);
		_alarm_set_timer(&alarm_context, alarm_context.timer, due_time);
		alarm_context.c_due_time = due_time;
	} else if (due_time == alarm_context.c_due_time) {
		_add_to_scheduled_alarm_list(__alarm_info);
	}

	__rtc_set();

	return true;
}

static bool __alarm_create(alarm_info_t *alarm_info, alarm_id_t *alarm_id,
			   int pid, char *app_service_name, char *app_service_name_mod,
			   const char *dst_service_name,const char *dst_service_name_mod,  int *error_code)
{
	time_t current_time;
	time_t due_time;
	char app_name[512] = { 0 };
	char caller_appid[256] = { 0 };
	char* caller_pkgid = NULL;
	pkgmgrinfo_pkginfo_h caller_handle;

	__alarm_info_t *__alarm_info = NULL;

	__alarm_info = malloc(sizeof(__alarm_info_t));
	if (__alarm_info == NULL) {
		SECURE_LOGE("Caution!! app_pid=%d, malloc "
					  "failed. it seems to be OOM\n", pid);
		*error_code = ERR_ALARM_SYSTEM_FAIL;
		return false;
	}
	__alarm_info->pid = pid;
	__alarm_info->alarm_id = -1;
	__alarm_info->quark_caller_pkgid = g_quark_from_string("null");

	// Get caller_appid to get caller's package id. There is no callee.
	if (aul_app_get_appid_bypid(pid, caller_appid, 256) == AUL_R_OK) {
		if (pkgmgrinfo_appinfo_get_appinfo(caller_appid, &caller_handle) == PMINFO_R_OK) {
			if (pkgmgrinfo_appinfo_get_pkgid(caller_handle, &caller_pkgid) == PMINFO_R_OK) {
				if (caller_pkgid) {
					__alarm_info->quark_caller_pkgid = g_quark_from_string(caller_pkgid);
				}
			}
			pkgmgrinfo_appinfo_destroy_appinfo(caller_handle);
		}
	}

	__alarm_info->quark_callee_pkgid = g_quark_from_string("null");
	SECURE_LOGD("caller_pkgid = %s, callee_pkgid = null", g_quark_to_string(__alarm_info->quark_caller_pkgid));

	if (!__get_caller_unique_name(pid, app_name)) {
		*error_code = ERR_ALARM_SYSTEM_FAIL;
		free(__alarm_info);
		return false;
	}

	__alarm_info->quark_app_unique_name = g_quark_from_string(app_name);
	__alarm_info->quark_app_service_name = g_quark_from_string(app_service_name);
	__alarm_info->quark_app_service_name_mod = g_quark_from_string(app_service_name_mod);
	__alarm_info->quark_dst_service_name = g_quark_from_string(dst_service_name);
	__alarm_info->quark_dst_service_name_mod = g_quark_from_string(dst_service_name_mod);
	__alarm_info->quark_bundle = g_quark_from_string("null");

	__alarm_set_start_and_end_time(alarm_info, __alarm_info);
	memcpy(&(__alarm_info->alarm_info), alarm_info, sizeof(alarm_info_t));
	__alarm_generate_alarm_id(__alarm_info, alarm_id);

	time(&current_time);

	SECURE_LOGD("[alarm-server]:pid=%d, app_unique_name=%s, "
		"app_service_name=%s,dst_service_name=%s, c_due_time=%d", \
		pid, g_quark_to_string(__alarm_info->quark_app_unique_name), \
		g_quark_to_string(__alarm_info->quark_app_service_name), \
		g_quark_to_string(__alarm_info->quark_dst_service_name), \
			    alarm_context.c_due_time);

	if (alarm_context.c_due_time < current_time) {
		ALARM_MGR_EXCEPTION_PRINT("Caution!! alarm_context.c_due_time "
		"(%d) is less than current time(%d)", alarm_context.c_due_time, current_time);
		alarm_context.c_due_time = -1;
	}

	due_time = _alarm_next_duetime(__alarm_info);
	if (__alarm_add_to_list(__alarm_info) == false) {
		free(__alarm_info);
		return false;
	}

	if (due_time == 0) {
		ALARM_MGR_EXCEPTION_PRINT("[alarm-server]:Create a new alarm: due_time is 0, alarm(%d).", *alarm_id);
		return true;
	} else if (current_time == due_time) {
		ALARM_MGR_EXCEPTION_PRINT("[alarm-server]:Create alarm: current_time(%d) is same as due_time(%d).",
			current_time, due_time);
		return true;
	} else if (difftime(due_time, current_time) <  0) {
		ALARM_MGR_EXCEPTION_PRINT("[alarm-server]: Expired Due Time.[Due time=%d, Current Time=%d]!!!Do not add to schedule list.",
			due_time, current_time);
		return true;
	} else {
		char due_time_r[100] = { 0 };
		struct tm ts_ret;
		localtime_r(&due_time, &ts_ret);
		strftime(due_time_r, 30, "%c", &ts_ret);
		SECURE_LOGD("[alarm-server]:Create a new alarm: alarm(%d) due_time(%s)", *alarm_id, due_time_r);
	}

	ALARM_MGR_EXCEPTION_PRINT("[alarm-server]:alarm_context.c_due_time(%d), due_time(%d)", alarm_context.c_due_time, due_time);

	if (alarm_context.c_due_time == -1 || due_time < alarm_context.c_due_time) {
		_clear_scheduled_alarm_list();
		_add_to_scheduled_alarm_list(__alarm_info);
		_alarm_set_timer(&alarm_context, alarm_context.timer, due_time);
		alarm_context.c_due_time = due_time;
	} else if (due_time == alarm_context.c_due_time) {
		_add_to_scheduled_alarm_list(__alarm_info);
	}

	__rtc_set();

	return true;
}

static bool __alarm_update(int pid, char *app_service_name, alarm_id_t alarm_id,
			   alarm_info_t *alarm_info, int *error_code)
{
	time_t current_time;
	time_t due_time;

	__alarm_info_t *__alarm_info = NULL;
	bool result = false;

	__alarm_info = malloc(sizeof(__alarm_info_t));
	if (__alarm_info == NULL) {
		SECURE_LOGE("Caution!! app_pid=%d, malloc failed. it seems to be OOM.", pid);
		*error_code = ERR_ALARM_SYSTEM_FAIL;
		return false;
	}

	__alarm_info->pid = pid;
	__alarm_info->alarm_id = alarm_id;

	/* we should consider to check whether  pid is running or Not
	 */

	__alarm_info->quark_app_service_name =
	    g_quark_from_string(app_service_name);
	__alarm_set_start_and_end_time(alarm_info, __alarm_info);
	memcpy(&(__alarm_info->alarm_info), alarm_info, sizeof(alarm_info_t));

	time(&current_time);

	if (alarm_context.c_due_time < current_time) {
		ALARM_MGR_EXCEPTION_PRINT("Caution!! alarm_context.c_due_time "
		"(%d) is less than current time(%d)", alarm_context.c_due_time, current_time);
		alarm_context.c_due_time = -1;
	}

	due_time = _alarm_next_duetime(__alarm_info);
	if (!__alarm_update_in_list(pid, alarm_id, __alarm_info, error_code)) {
		free(__alarm_info);
		ALARM_MGR_EXCEPTION_PRINT("[alarm-server]: requested alarm_id "
		"(%d) does not exist. so this value is invalid id.", alarm_id);
		return false;
	}

	result = _remove_from_scheduled_alarm_list(pid, alarm_id);

	if (result == true && g_slist_length(g_scheduled_alarm_list) == 0) {
		/*there is no scheduled alarm */
		_alarm_disable_timer(alarm_context);
		_alarm_schedule();

		ALARM_MGR_LOG_PRINT("[alarm-server]:Update alarm: alarm(%d).", alarm_id);

		__rtc_set();

		if (due_time == 0) {
			ALARM_MGR_EXCEPTION_PRINT("[alarm-server]:Update alarm: due_time is 0.");
		}
		free(__alarm_info);
		return true;
	}

	if (due_time == 0) {
		ALARM_MGR_EXCEPTION_PRINT("[alarm-server]:Update alarm: "
				"due_time is 0, alarm(%d)\n", alarm_id);
		free(__alarm_info);
		return true;
	} else if (current_time == due_time) {
		ALARM_MGR_EXCEPTION_PRINT("[alarm-server]:Update alarm: "
		"current_time(%d) is same as due_time(%d)", current_time,
		due_time);
		free(__alarm_info);
		return true;
	} else if (difftime(due_time, current_time)< 0) {
		ALARM_MGR_EXCEPTION_PRINT("[alarm-server]: Expired Due Time.[Due time=%d, Current Time=%d]!!!Do not add to schedule list\n", due_time, current_time);
		free(__alarm_info);
		return true;
	} else {
		char due_time_r[100] = { 0 };
		struct tm ts_ret;
		localtime_r(&due_time, &ts_ret);
		strftime(due_time_r, 30, "%c", &ts_ret);
		SECURE_LOGD("[alarm-server]:Update alarm: alarm(%d) "
				    "due_time(%s)\n", alarm_id, due_time_r);
	}

	ALARM_MGR_EXCEPTION_PRINT("[alarm-server]:alarm_context.c_due_time(%d), due_time(%d)", alarm_context.c_due_time, due_time);

	if (alarm_context.c_due_time == -1 || due_time < alarm_context.c_due_time) {
		_clear_scheduled_alarm_list();
		_add_to_scheduled_alarm_list(__alarm_info);
		_alarm_set_timer(&alarm_context, alarm_context.timer, due_time);
		alarm_context.c_due_time = due_time;
		ALARM_MGR_LOG_PRINT("[alarm-server1]:alarm_context.c_due_time "
		     "(%d), due_time(%d)", alarm_context.c_due_time, due_time);
	} else if (due_time == alarm_context.c_due_time) {
		_add_to_scheduled_alarm_list(__alarm_info);
		ALARM_MGR_LOG_PRINT("[alarm-server2]:alarm_context.c_due_time "
		     "(%d), due_time(%d)", alarm_context.c_due_time, due_time);
	}

	__rtc_set();

	free(__alarm_info);

	return true;
}

static bool __alarm_delete(int pid, alarm_id_t alarm_id, int *error_code)
{
	bool result = false;

	SECURE_LOGD("[alarm-server]:delete alarm: alarm(%d) pid(%d)\n", alarm_id, pid);
	result = _remove_from_scheduled_alarm_list(pid, alarm_id);

	if (!__alarm_remove_from_list(pid, alarm_id, error_code)) {

		SECURE_LOGE("[alarm-server]:delete alarm: "
					  "alarm(%d) pid(%d) has failed with error_code(%d)\n",
					  alarm_id, pid, *error_code);
		return false;
	}

	if (result == true && g_slist_length(g_scheduled_alarm_list) == 0) {
		_alarm_disable_timer(alarm_context);
		_alarm_schedule();
	}

	__rtc_set();

	return true;
}

static void __alarm_send_noti_to_application(const char *app_service_name, alarm_id_t alarm_id)
{
	char service_name[MAX_SERVICE_NAME_LEN] = {0,};

	if (app_service_name == NULL || strlen(app_service_name) == 0) {
		ALARM_MGR_EXCEPTION_PRINT("This alarm destination is invalid.");
		return;
	}

	memcpy(service_name, app_service_name, strlen(app_service_name));
	SECURE_LOGI("[alarm server][send expired_alarm(alarm_id=%d) to app_service_name(%s)]", alarm_id, service_name);

	g_dbus_connection_call(alarm_context.connection,
						service_name,
						"/com/samsung/alarm/client",
						"com.samsung.alarm.client",
						"alarm_expired",
						g_variant_new("(is)", alarm_id, service_name),
						NULL,
						G_DBUS_CALL_FLAGS_NONE,
						-1,
						NULL,
						NULL,
						NULL);
}

static void __alarm_expired()
{
	const char *destination_app_service_name = NULL;
	alarm_id_t alarm_id = -1;
	int app_pid = 0;
	__alarm_info_t *__alarm_info = NULL;
	char alarm_id_val[32]={0,};
	int b_len = 0;
	bundle *b = NULL;
	char *appid = NULL;
	char log_message[ALARMMGR_LOG_MESSAGE_SIZE] = {0,};
	GError *error = NULL;
	GVariant *result = NULL;
	gboolean name_has_owner_reply = false;

	ALARM_MGR_LOG_PRINT("[alarm-server]: Enter");

	time_t current_time;
	double interval;

	time(&current_time);

	interval = difftime(alarm_context.c_due_time, current_time);
	ALARM_MGR_LOG_PRINT("[alarm-server]: c_due_time(%d), current_time(%d), interval(%d)",
		alarm_context.c_due_time, current_time, interval);

	if (alarm_context.c_due_time > current_time + 1) {
		ALARM_MGR_EXCEPTION_PRINT("[alarm-server]: False Alarm. due time is (%d) seconds future",
			alarm_context.c_due_time - current_time);
		goto done;
	}
	// 10 seconds is maximum permitted delay from timer expire to this function
	if (alarm_context.c_due_time + 10 < current_time) {
		ALARM_MGR_EXCEPTION_PRINT("[alarm-server]: False Alarm. due time is (%d) seconds past\n",
			current_time - alarm_context.c_due_time);
		goto done;
	}

	GSList *iter = NULL;
	__scheduled_alarm_t *alarm = NULL;

	for (iter = g_scheduled_alarm_list; iter != NULL; iter = g_slist_next(iter)) {
		alarm = iter->data;
		alarm_id = alarm->alarm_id;

		__alarm_info = alarm->__alarm_info;

		app_pid = __alarm_info->pid;

		if (strncmp(g_quark_to_string(__alarm_info->quark_bundle), "null", 4) != 0) {
				b_len = strlen(g_quark_to_string(__alarm_info->quark_bundle));

				b = bundle_decode((bundle_raw *)g_quark_to_string(__alarm_info->quark_bundle), b_len);

				if (b == NULL)
				{
					ALARM_MGR_EXCEPTION_PRINT("Error!!!..Unable to decode the bundle!!\n");
				}
				else
				{
					snprintf(alarm_id_val,31,"%d",alarm_id);

					if (bundle_add_str(b,"http://tizen.org/appcontrol/data/alarm_id", alarm_id_val)){
						ALARM_MGR_EXCEPTION_PRINT("Unable to add alarm id to the bundle\n");
					}
					else
					{
						appid = (char *)appsvc_get_appid(b);
						if( (__alarm_info->alarm_info.alarm_type & ALARM_TYPE_NOLAUNCH) && !aul_app_is_running(appid))
						{
							ALARM_MGR_EXCEPTION_PRINT("This alarm is ignored\n");
						}
						else
						{
							if ( appsvc_run_service(b, 0, NULL, NULL) < 0)
							{
								ALARM_MGR_EXCEPTION_PRINT("Unable to run app svc\n");
							}
							else
							{
								ALARM_MGR_LOG_PRINT("Successfuly ran app svc\n");
							}
						}
					}
					bundle_free(b);
				}

		}
		else
		{
			if (strncmp(g_quark_to_string(__alarm_info->quark_dst_service_name), "null", 4) == 0) {
				SECURE_LOGD("[alarm-server]:destination is null, so we send expired alarm to %s(%u).",
					g_quark_to_string(__alarm_info->quark_app_service_name), __alarm_info->quark_app_service_name);
					destination_app_service_name = g_quark_to_string(__alarm_info->quark_app_service_name_mod);
			} else {
				SECURE_LOGD("[alarm-server]:destination :%s(%u)",
					g_quark_to_string(__alarm_info->quark_dst_service_name), __alarm_info->quark_dst_service_name);
					destination_app_service_name = g_quark_to_string(__alarm_info->quark_dst_service_name_mod);
			}

			/*
			 * we should consider a situation that
			 * destination_app_service_name is owner_name like (:xxxx) and
			 * application's pid which registered this alarm was killed.In that case,
			 * we don't need to send the expire event because the process was killed.
			 * this causes needless message to be sent.
			 */
			SECURE_LOGD("[alarm-server]: destination_app_service_name :%s, app_pid=%d", destination_app_service_name, app_pid);

			result = g_dbus_connection_call_sync(alarm_context.connection,
								"org.freedesktop.DBus",
								"/org/freedesktop/DBus",
								"org.freedesktop.DBus",
								"NameHasOwner",
								g_variant_new ("(s)", destination_app_service_name),
								G_VARIANT_TYPE ("(b)"),
								G_DBUS_CALL_FLAGS_NONE,
								-1,
								NULL,
								&error);
			if (result == NULL) {
				ALARM_MGR_EXCEPTION_PRINT("g_dbus_connection_call_sync() is failed. err: %s", error->message);
				g_error_free(error);
			} else {
				g_variant_get (result, "(b)", &name_has_owner_reply);
			}

			if (name_has_owner_reply == false) {
				__expired_alarm_t *expire_info;
				char appid[MAX_SERVICE_NAME_LEN] = { 0, };
				char alarm_id_str[32] = { 0, };

				if (__alarm_info->alarm_info.alarm_type & ALARM_TYPE_WITHCB) {
					__alarm_remove_from_list(__alarm_info->pid, alarm_id, NULL);
					goto done;
				}

				expire_info = malloc(sizeof(__expired_alarm_t));
				if (G_UNLIKELY(NULL == expire_info)) {
					ALARM_MGR_ASSERT_PRINT("[alarm-server]:Malloc failed!Can't notify alarm expiry info\n");
					goto done;
				}
				memset(expire_info, '\0', sizeof(__expired_alarm_t));
				strncpy(expire_info->service_name, destination_app_service_name, MAX_SERVICE_NAME_LEN-1);
				expire_info->alarm_id = alarm_id;
				g_expired_alarm_list = g_slist_append(g_expired_alarm_list, expire_info);

				if (strncmp(g_quark_to_string(__alarm_info->quark_dst_service_name), "null",4) == 0) {
					strncpy(appid,g_quark_to_string(__alarm_info->quark_app_service_name)+6,strlen(g_quark_to_string(__alarm_info->quark_app_service_name))-6);
				}
				else {
					strncpy(appid,g_quark_to_string(__alarm_info->quark_dst_service_name)+6,strlen(g_quark_to_string(__alarm_info->quark_dst_service_name))-6);
				}

				snprintf(alarm_id_str, 31, "%d", alarm_id);

				SECURE_LOGD("before aul_launch appid(%s) alarm_id_str(%s)", appid, alarm_id_str);

				bundle *kb;
				kb = bundle_create();
				bundle_add_str(kb, "__ALARM_MGR_ID", alarm_id_str);
				aul_launch_app(appid, kb);
				bundle_free(kb);
			} else {
				ALARM_MGR_LOG_PRINT("before alarm_send_noti_to_application");
				ALARM_MGR_LOG_PRINT("WAKEUP pid: %d", __alarm_info->pid);

				aul_update_freezer_status(__alarm_info->pid, "wakeup");
				__alarm_send_noti_to_application(destination_app_service_name, alarm_id);
				ALARM_MGR_LOG_PRINT("after __alarm_send_noti_to_application");
			}
		}

		ALARM_MGR_EXCEPTION_PRINT("alarm_id[%d] is expired.", alarm_id);

		sprintf(log_message, "alarmID: %d, pid: %d, unique_name: %s, duetime: %d",
			alarm_id, app_pid, g_quark_to_string(__alarm_info->quark_app_unique_name), __alarm_info->due_time);
		__save_module_log("EXPIRED", log_message);
		memset(log_message, '\0', sizeof(log_message));

		if (__alarm_info->alarm_info.mode.repeat == ALARM_REPEAT_MODE_ONCE) {
			__alarm_remove_from_list(__alarm_info->pid, alarm_id, NULL);
		} else {
			_alarm_next_duetime(__alarm_info);
		}
	}

 done:
	_clear_scheduled_alarm_list();
	alarm_context.c_due_time = -1;

	ALARM_MGR_LOG_PRINT("[alarm-server]: Leave");
}

static gboolean __alarm_handler_idle(gpointer user_data)
{
	GPollFD *gpollfd = (GPollFD *) user_data;
	uint64_t exp;
	if (gpollfd == NULL) {
		ALARM_MGR_EXCEPTION_PRINT("gpollfd is NULL");
		return false;
	}
	if (read(gpollfd->fd, &exp, sizeof(uint64_t)) < 0) {
		ALARM_MGR_EXCEPTION_PRINT("Reading the fd is failed.");
		return false;
	}

	ALARM_MGR_EXCEPTION_PRINT("Lock the display not to enter LCD OFF");
	if (__display_lock_state(DEVICED_LCD_OFF, DEVICED_STAY_CUR_STATE, 0) != ALARMMGR_RESULT_SUCCESS) {
		ALARM_MGR_EXCEPTION_PRINT("__display_lock_state() is failed");
	}

	if (g_dummy_timer_is_set == true) {
		ALARM_MGR_LOG_PRINT("dummy alarm timer has expired.");
	}
	else {
		ALARM_MGR_LOG_PRINT("__alarm_handler_idle");
		__alarm_expired();
	}

	_alarm_schedule();

	__rtc_set();

	ALARM_MGR_EXCEPTION_PRINT("Unlock the display from LCD OFF");
	if (__display_unlock_state(DEVICED_LCD_OFF, DEVICED_SLEEP_MARGIN) != ALARMMGR_RESULT_SUCCESS) {
		ALARM_MGR_EXCEPTION_PRINT("__display_unlock_state() is failed");
	}

	return false;
}

static void __clean_registry()
{

	/*TODO:remove all db entries */
}

static bool __alarm_manager_reset()
{
	_alarm_disable_timer(alarm_context);

	__alarm_clean_list();

	_clear_scheduled_alarm_list();
	__clean_registry();

	return true;
}

static void __on_system_time_external_changed(keynode_t *node, void *data)
{
	double diff_time = 0.0;
	time_t cur_time = 0;

	_alarm_disable_timer(alarm_context);

	if (node) {
		diff_time = vconf_keynode_get_dbl(node);
	} else {
		if (vconf_get_dbl(VCONFKEY_SYSTEM_TIMECHANGE_EXTERNAL, &diff_time) != VCONF_OK) {
			ALARM_MGR_EXCEPTION_PRINT("Failed to get value of VCONFKEY_SYSTEM_TIMECHANGE_EXTERNAL.");
			return;
		}
	}

	tzset();
	time(&cur_time);

	ALARM_MGR_EXCEPTION_PRINT("diff_time is %f, New time is %s\n", diff_time, ctime(&cur_time));

	ALARM_MGR_LOG_PRINT("[alarm-server] System time has been changed externally\n");
	ALARM_MGR_LOG_PRINT("1.alarm_context.c_due_time is %d\n",
			    alarm_context.c_due_time);

	// set rtc time only because the linux time is set externally
	_set_rtc_time(cur_time);

	vconf_set_int(VCONFKEY_SYSTEM_TIME_CHANGED,(int)diff_time);

	__alarm_update_due_time_of_all_items_in_list(diff_time);

	ALARM_MGR_LOG_PRINT("2.alarm_context.c_due_time is %d\n",
			    alarm_context.c_due_time);
	_clear_scheduled_alarm_list();
	_alarm_schedule();
	__rtc_set();

	return;
}

static void __on_time_zone_changed(keynode_t *node, void *data)
{
	double diff_time = 0;

	_alarm_disable_timer(alarm_context);

	tzset();

	ALARM_MGR_LOG_PRINT("[alarm-server] time zone has been changed\n");
	ALARM_MGR_LOG_PRINT("1.alarm_context.c_due_time is %d\n", alarm_context.c_due_time);

	__alarm_update_due_time_of_all_items_in_list(diff_time);

	ALARM_MGR_LOG_PRINT("2.alarm_context.c_due_time is %d\n", alarm_context.c_due_time);
	_clear_scheduled_alarm_list();
	_alarm_schedule();
	__rtc_set();

	return;
}

static int __on_app_uninstalled(int req_id, const char *pkg_type,
				const char *pkgid, const char *key, const char *val,
				const void *pmsg, void *user_data)
{
	GSList* gs_iter = NULL;
	__alarm_info_t* entry = NULL;
	alarm_info_t* alarm_info = NULL;
	bool is_deleted = false;

	SECURE_LOGD("pkg_type(%s), pkgid(%s), key(%s), value(%s)", pkg_type, pkgid, key, val);

	if (strncmp(key, "end", 3) == 0 && strncmp(val, "ok", 2) == 0)
	{
		for (gs_iter = alarm_context.alarms; gs_iter != NULL; )
		{
			bool is_found = false;
			entry = gs_iter->data;

			char* caller_pkgid = g_quark_to_string(entry->quark_caller_pkgid);
			char* callee_pkgid = g_quark_to_string(entry->quark_callee_pkgid);

			if ((caller_pkgid && strncmp(pkgid, caller_pkgid, strlen(pkgid)) == 0) ||
				(callee_pkgid && strncmp(pkgid, callee_pkgid, strlen(pkgid)) == 0))
			{
				if (_remove_from_scheduled_alarm_list(pkgid, entry->alarm_id))
				{
					is_deleted = true;
				}

				alarm_info = &entry->alarm_info;
				if (!(alarm_info->alarm_type & ALARM_TYPE_VOLATILE))
				{
					if(!_delete_alarms(entry->alarm_id))
					{
						SECURE_LOGE("_delete_alarms() is failed. pkgid[%s], alarm_id[%d]", pkgid, entry->alarm_id);
					}
				}
				is_found = true;
			}

			gs_iter = g_slist_next(gs_iter);

			if (is_found)
			{
				SECURE_LOGD("Remove pkgid[%s], alarm_id[%d]", pkgid, entry->alarm_id);
				alarm_context.alarms = g_slist_remove(alarm_context.alarms, entry);
				g_free(entry);
			}
		}

		if (is_deleted && (g_slist_length(g_scheduled_alarm_list) == 0))
		{
			_alarm_disable_timer(alarm_context);
			_alarm_schedule();
		}
	}

	__rtc_set();

	return ALARMMGR_RESULT_SUCCESS;
}

int __check_privilege_by_cookie(char *e_cookie, const char *label, const char *access, bool check_root, int pid) {
	guchar *cookie = NULL;
	gsize size = 0;
	int retval = 0;
	char buf[128] = {0,};
	FILE *fp = NULL;
	char title[128] = {0,};
	int uid = -1;

	if (check_root) {
		// Gets the userID from /proc/pid/status to check if the process is the root or not.
		snprintf(buf, sizeof(buf), "/proc/%d/status", pid);
		fp = fopen(buf, "r");
		if(fp) {
			while (fgets(buf, sizeof(buf), fp) != NULL) {
				if(strncmp(buf, "Uid:", 4) == 0) {
					sscanf(buf, "%s %d", title, &uid);
					break;
				}
			}
			fclose(fp);
		}

		ALARM_MGR_LOG_PRINT("uid : %d", uid);
	}

	if (uid != 0) {	// Checks the cookie only when the process is not the root
		cookie = g_base64_decode(e_cookie, &size);
		if (cookie == NULL) {
			ALARM_MGR_EXCEPTION_PRINT("Unable to decode cookie!!!");
			return ERR_ALARM_SYSTEM_FAIL;
		}

		retval = security_server_check_privilege_by_cookie((const char *)cookie, label, access);
		g_free(cookie);

		if (retval < 0) {
			if (retval == SECURITY_SERVER_API_ERROR_ACCESS_DENIED) {
				ALARM_MGR_EXCEPTION_PRINT("Access to alarm-server has been denied by smack.");
			}
			ALARM_MGR_EXCEPTION_PRINT("Error has occurred in security_server_check_privilege_by_cookie() : %d.", retval);
			return ERR_ALARM_NO_PERMISSION;
		}
	}

	ALARM_MGR_LOG_PRINT("The process(%d) was authenticated successfully.", pid);
	return ALARMMGR_RESULT_SUCCESS;
}

bool __get_caller_unique_name(int pid, char *unique_name)
{
	char caller_appid[256] = {0,};

	if (unique_name == NULL)
	{
		ALARM_MGR_EXCEPTION_PRINT("unique_name should not be NULL.");
		return false;
	}

	if (aul_app_get_appid_bypid(pid, caller_appid, sizeof(caller_appid)) == AUL_R_OK)
	{
		// When a caller is an application, the unique name is appID.
		strncpy(unique_name, caller_appid, strlen(caller_appid));
	}
	else
	{
		// Otherwise, the unique name is /proc/pid/cmdline.
		char proc_file[512] = {0,};
		char process_name[512] = {0,};
		int fd = 0;
		int i = 0;

		snprintf(proc_file, 512, "/proc/%d/cmdline", pid);

		fd = open(proc_file, O_RDONLY);
		if (fd < 0) {
			SECURE_LOGE("Caution!! pid(%d) seems to be killed, so we failed to get proc file(%s) and do not create alarm_info.", pid, proc_file);
			return false;
		}
		else {
			if (read(fd, process_name, 512) <= 0)
			{
				ALARM_MGR_EXCEPTION_PRINT("Unable to get the process name.");
				close(fd);
				return false;
			}
			close(fd);

			while (process_name[i] != '\0') {
				if (process_name[i] == ' ') {
					process_name[i] = '\0';
					break;
				}
				++i;
			}
			strncpy(unique_name, process_name, strlen(process_name));
		}
	}

	SECURE_LOGD("unique_name= %s", unique_name);
	return true;
}

static void __initialize_module_log(void)
{
	log_fd = open(ALARMMGR_LOG_FILE_PATH, O_CREAT | O_WRONLY, 0644);
	if (log_fd == -1) {
		ALARM_MGR_EXCEPTION_PRINT("Opening the file for alarmmgr log is failed.");
		return;
	}

	int offset = lseek(log_fd, 0, SEEK_END);
	if (offset != 0) {
		log_index = (int)(offset / ALARMMGR_LOG_BUFFER_STRING_SIZE);
		if (log_index >= ALARMMGR_LOG_BUFFER_SIZE) {
			log_index = 0;
			lseek(log_fd, 0, SEEK_SET);
		}
	}
	return;
}

static bool __save_module_log(const char *tag, const char *message)
{
	char buffer[ALARMMGR_LOG_BUFFER_STRING_SIZE] = {0,};
	time_t now;
	int offset = 0;

	if (log_fd == -1) {
		ALARM_MGR_EXCEPTION_PRINT("The file is not ready.");
		return false;
	}

	if (log_index != 0) {
		offset = lseek(log_fd, 0, SEEK_CUR);
	} else {
		offset = lseek(log_fd, 0, SEEK_SET);
	}

	time(&now);
	snprintf(buffer, ALARMMGR_LOG_BUFFER_STRING_SIZE, "[%-6d] %-20s %-120s %d-%s", log_index, tag, message, (int)now, ctime(&now));

	int ret = write(log_fd, buffer, strlen(buffer));
	if (ret < 0) {
		ALARM_MGR_EXCEPTION_PRINT("Writing the alarmmgr log is failed.");
		return false;
	}

	if (++log_index >= ALARMMGR_LOG_BUFFER_SIZE) {
		log_index = 0;
	}
	return true;
}

int __display_lock_state(char *state, char *flag, unsigned int timeout)
{
	GDBusMessage *msg = NULL;
	GDBusMessage *reply = NULL;
	GVariant *body = NULL;
	GError *error = NULL;
	int ret = ALARMMGR_RESULT_SUCCESS;
	int val = -1;

	msg = g_dbus_message_new_method_call(DEVICED_BUS_NAME, DEVICED_PATH_DISPLAY, DEVICED_INTERFACE_DISPLAY, DEVICED_LOCK_STATE);
	if (!msg) {
		ALARM_MGR_EXCEPTION_PRINT("g_dbus_message_new_method_call() is failed. (%s:%s-%s)", DEVICED_BUS_NAME, DEVICED_INTERFACE_DISPLAY, DEVICED_LOCK_STATE);
		return ERR_ALARM_SYSTEM_FAIL;
	}

	g_dbus_message_set_body(msg, g_variant_new("(sssi)", state, flag, "NULL", timeout));

	reply =  g_dbus_connection_send_message_with_reply_sync(alarm_context.connection, msg, G_DBUS_SEND_MESSAGE_FLAGS_NONE, DEVICED_DBUS_REPLY_TIMEOUT, NULL, NULL, &error);
	if (!reply) {
		ALARM_MGR_EXCEPTION_PRINT("No reply. error = %s", error->message);
		g_error_free(error);
		ret = ERR_ALARM_SYSTEM_FAIL;
	} else {
		body = g_dbus_message_get_body(reply);
		if (!body) {
			ALARM_MGR_EXCEPTION_PRINT("g_dbus_message_get_body() is failed.");
			ret = ERR_ALARM_SYSTEM_FAIL;
		} else {
			g_variant_get(body, "(i)", &val);
			if (val != 0) {
				ALARM_MGR_EXCEPTION_PRINT("Failed to lock display");
				ret = ERR_ALARM_SYSTEM_FAIL;
			} else {
				ALARM_MGR_EXCEPTION_PRINT("Lock LCD OFF is successfully done");
			}
		}
	}

	g_dbus_connection_flush_sync(alarm_context.connection, NULL, NULL);
	g_object_unref(msg);
	g_object_unref(reply);

	return ret;
}

int __display_unlock_state(char *state, char *flag)
{
	GDBusMessage *msg = NULL;
	GDBusMessage *reply = NULL;
	GVariant *body = NULL;
	GError *error = NULL;
	int ret = ALARMMGR_RESULT_SUCCESS;
	int val = -1;

	msg = g_dbus_message_new_method_call(DEVICED_BUS_NAME, DEVICED_PATH_DISPLAY, DEVICED_INTERFACE_DISPLAY, DEVICED_UNLOCK_STATE);
	if (!msg) {
		ALARM_MGR_EXCEPTION_PRINT("g_dbus_message_new_method_call() is failed. (%s:%s-%s)", DEVICED_BUS_NAME, DEVICED_INTERFACE_DISPLAY, DEVICED_UNLOCK_STATE);
		return ERR_ALARM_SYSTEM_FAIL;
	}

	g_dbus_message_set_body(msg, g_variant_new("(ss)", state, flag ));

	reply =  g_dbus_connection_send_message_with_reply_sync(alarm_context.connection, msg, G_DBUS_SEND_MESSAGE_FLAGS_NONE, DEVICED_DBUS_REPLY_TIMEOUT, NULL, NULL, &error);
	if (!reply) {
		ALARM_MGR_EXCEPTION_PRINT("No reply. error = %s", error->message);
		g_error_free(error);
		ret = ERR_ALARM_SYSTEM_FAIL;
	} else {
		body = g_dbus_message_get_body(reply);
		if (!body) {
			ALARM_MGR_EXCEPTION_PRINT("g_dbus_message_get_body() is failed.");
			ret = ERR_ALARM_SYSTEM_FAIL;
		} else {
			g_variant_get(body, "(i)", &val);
			if (val != 0) {
				ALARM_MGR_EXCEPTION_PRINT("Failed to unlock display");
				ret = ERR_ALARM_SYSTEM_FAIL;
			} else {
				ALARM_MGR_EXCEPTION_PRINT("Unlock LCD OFF is successfully done");
			}
		}
	}

	g_dbus_connection_flush_sync(alarm_context.connection, NULL, NULL);
	g_object_unref(msg);
	g_object_unref(reply);

	return ret;
}

gboolean alarm_manager_alarm_set_rtc_time(AlarmManager *pObj, GDBusMethodInvocation *invoc, int pid,
				int year, int mon, int day,
				int hour, int min, int sec, char *e_cookie,
				gpointer user_data) {
	const char *rtc = default_rtc;
	struct timespec alarm_time;
	int retval = 0;
	int return_code = ALARMMGR_RESULT_SUCCESS;

	struct rtc_time rtc_tm = {0,};
	struct rtc_wkalrm rtc_wk;
	struct tm *alarm_tm = NULL;

	char log_tag[ALARMMGR_LOG_TAG_SIZE] = {0,};
	char log_message[ALARMMGR_LOG_MESSAGE_SIZE] = {0,};

	retval = __check_privilege_by_cookie(e_cookie, "alarm-server::alarm", "w", false, pid);
	if (retval != ALARMMGR_RESULT_SUCCESS) {
		return_code = retval;
		g_dbus_method_invocation_return_value(invoc, g_variant_new("(i)", return_code));
		return true;
	}

	/*extract day of the week, day in the year & daylight saving time from system*/
	time_t current_time;
	current_time = time(NULL);
	alarm_tm = localtime(&current_time);

	alarm_tm->tm_year = year;
	alarm_tm->tm_mon = mon;
	alarm_tm->tm_mday = day;
	alarm_tm->tm_hour = hour;
	alarm_tm->tm_min = min;
	alarm_tm->tm_sec = sec;

	/*convert to calendar time representation*/
	time_t rtc_time = mktime(alarm_tm);

	if (gfd == 0) {
		gfd = open(rtc, O_RDWR);
		if (gfd == -1) {
			ALARM_MGR_EXCEPTION_PRINT("RTC open failed.");
			return_code = ERR_ALARM_SYSTEM_FAIL;
			g_dbus_method_invocation_return_value(invoc, g_variant_new("(i)", return_code));
			return true;
		}
	}

	alarm_time.tv_sec = rtc_time;
	alarm_time.tv_nsec = 0;

	retval = ioctl(gfd, ALARM_SET(ALARM_RTC_WAKEUP), &alarm_time);
	if (retval == -1) {
		if (errno == ENOTTY) {
			ALARM_MGR_EXCEPTION_PRINT("Alarm IRQs is not supported.");
		}
		ALARM_MGR_EXCEPTION_PRINT("RTC ALARM_SET ioctl is failed. errno = %s", strerror(errno));
		return_code = ERR_ALARM_SYSTEM_FAIL;
		strncpy(log_tag, "FAIL: SET RTC", strlen("FAIL: SET RTC"));
	}
	else{
		ALARM_MGR_LOG_PRINT("[alarm-server]RTC alarm is setted");
		strncpy(log_tag, "SET RTC", strlen("SET RTC"));
	}

	sprintf(log_message, "wakeup rtc time: %d, %s", rtc_time, ctime(&rtc_time));
	__save_module_log(log_tag, log_message);

	g_dbus_method_invocation_return_value(invoc, g_variant_new("(i)", return_code));
	return true;
}

gboolean alarm_manager_alarm_set_time(AlarmManager *pObj, GDBusMethodInvocation *invoc, int _time, gpointer user_data)
{
	static int diff_msec = 0;	// To check a millisecond part of current time at changing the system time
	double diff_time = 0.0;
	struct timeval before;
	int return_code = ALARMMGR_RESULT_SUCCESS;
	char log_message[ALARMMGR_LOG_MESSAGE_SIZE] = {0,};

	_alarm_disable_timer(alarm_context);

	gettimeofday(&before, NULL);
	diff_msec += (before.tv_usec / 1000);	// Accrue the millisecond to compensate the time
	ALARM_MGR_LOG_PRINT("Current time = %s. usec = %ld. diff_msec = %d.\n", ctime(&before.tv_sec), before.tv_usec, diff_msec);

	if (diff_msec > 500) {
		diff_time = difftime(_time, before.tv_sec) - 1;
		diff_msec -= 1000;
	}
	else {
		diff_time = difftime(_time, before.tv_sec);
	}

	tzset();

	char *timebuf = ctime(&_time);
	timebuf[strlen(timebuf) - 1] = '\0';	// to avoid newline
	sprintf(log_message, "Current: %d, New: %d, %s, diff: %f", before.tv_sec, _time, timebuf, diff_time);
	__save_module_log("CHANGE TIME", log_message);

	ALARM_MGR_EXCEPTION_PRINT("[TIMESTAMP]Current time(%d), New time(%d)(%s), diff_time(%f)", before.tv_sec, _time, ctime(&_time), diff_time);

	ALARM_MGR_LOG_PRINT("[alarm-server] System time has been changed\n");
	ALARM_MGR_LOG_PRINT("1.alarm_context.c_due_time is %d\n", alarm_context.c_due_time);

	_set_time(_time - 1);
	__alarm_update_due_time_of_all_items_in_list(diff_time);

	ALARM_MGR_LOG_PRINT("2.alarm_context.c_due_time is %d", alarm_context.c_due_time);
	_clear_scheduled_alarm_list();
	_alarm_schedule();
	__rtc_set();
	_set_time(_time);

	vconf_set_int(VCONFKEY_SYSTEM_TIME_CHANGED,(int)diff_time);

	g_dbus_method_invocation_return_value(invoc, g_variant_new("(i)", return_code));
	return true;
}

gboolean alarm_manager_alarm_create_appsvc(AlarmManager *pObject, GDBusMethodInvocation *invoc,
					int pid,
				    int start_year,
				    int start_month, int start_day,
				    int start_hour, int start_min,
				    int start_sec, int end_year, int end_month,
				    int end_day, int mode_day_of_week,
				    int mode_repeat, int alarm_type,
				    int reserved_info,
				    char *bundle_data, char *e_cookie,
				    gpointer user_data)
{
	alarm_info_t alarm_info;
	int retval = 0;
	int return_code = ALARMMGR_RESULT_SUCCESS;
	int alarm_id = 0;
	char log_tag[ALARMMGR_LOG_TAG_SIZE] = {0,};
	char log_message[ALARMMGR_LOG_MESSAGE_SIZE] = {0,};
	bool ret = true;

	retval = __check_privilege_by_cookie(e_cookie, "alarm-server::alarm", "w", false, pid);
	if (retval != ALARMMGR_RESULT_SUCCESS) {
		return_code = retval;
		g_dbus_method_invocation_return_value(invoc, g_variant_new("(ii)", alarm_id, return_code));
		sprintf(log_message, "pid: %d, Smack denied (alarm-server::alarm, w)", pid);
		__save_module_log("FAIL: CREATE", log_message);
		return true;
	}

	alarm_info.start.year = start_year;
	alarm_info.start.month = start_month;
	alarm_info.start.day = start_day;
	alarm_info.start.hour = start_hour;
	alarm_info.start.min = start_min;
	alarm_info.start.sec = start_sec;

	alarm_info.end.year = end_year;
	alarm_info.end.month = end_month;
	alarm_info.end.day = end_day;

	alarm_info.mode.u_interval.day_of_week = mode_day_of_week;
	alarm_info.mode.repeat = mode_repeat;

	alarm_info.alarm_type = alarm_type;
	alarm_info.reserved_info = reserved_info;

	if (!__alarm_create_appsvc(&alarm_info, &alarm_id, pid, bundle_data, &return_code)) {
		ALARM_MGR_EXCEPTION_PRINT("Unable to create alarm! return_code[%d]", return_code);
		strncpy(log_tag, "FAIL: CREATE", strlen("FAIL: CREATE"));
		ret = false;
	} else {
		strncpy(log_tag, "CREATE", strlen("CREATE"));
		ret = true;
	}

	g_dbus_method_invocation_return_value(invoc, g_variant_new("(ii)", alarm_id, return_code));

	sprintf(log_message, "alarmID: %d, pid: %d, duetime: %d-%d-%d %02d:%02d:%02d",
		alarm_id, pid, start_year, start_month, start_day, start_hour, start_min, start_sec);
	__save_module_log(log_tag, log_message);

	return ret;
}

gboolean alarm_manager_alarm_create(AlarmManager *obj, GDBusMethodInvocation *invoc, int pid,
				    char *app_service_name, char *app_service_name_mod,  int start_year,
				    int start_month, int start_day,
				    int start_hour, int start_min,
				    int start_sec, int end_year, int end_month,
				    int end_day, int mode_day_of_week,
				    int mode_repeat, int alarm_type,
				    int reserved_info,
				    char *reserved_service_name, char *reserved_service_name_mod, char *e_cookie,
				    gpointer user_data)
{
	alarm_info_t alarm_info;
	int retval = 0;
	int return_code = ALARMMGR_RESULT_SUCCESS;
	int alarm_id = 0;
	char log_tag[ALARMMGR_LOG_TAG_SIZE] = {0,};
	char log_message[ALARMMGR_LOG_MESSAGE_SIZE] = {0,};
	bool ret = true;

	retval = __check_privilege_by_cookie(e_cookie, "alarm-server::alarm", "w", true, pid);
	if (retval != ALARMMGR_RESULT_SUCCESS) {
		return_code = retval;
		g_dbus_method_invocation_return_value(invoc, g_variant_new("(ii)", alarm_id, return_code));
		sprintf(log_message, "pid: %d, Smack denied (alarm-server::alarm, w)", pid);
		__save_module_log("FAIL: CREATE", log_message);
		return true;
	}

	alarm_info.start.year = start_year;
	alarm_info.start.month = start_month;
	alarm_info.start.day = start_day;
	alarm_info.start.hour = start_hour;
	alarm_info.start.min = start_min;
	alarm_info.start.sec = start_sec;

	alarm_info.end.year = end_year;
	alarm_info.end.month = end_month;
	alarm_info.end.day = end_day;

	alarm_info.mode.u_interval.day_of_week = mode_day_of_week;
	alarm_info.mode.repeat = mode_repeat;

	alarm_info.alarm_type = alarm_type;
	alarm_info.reserved_info = reserved_info;

	if (!__alarm_create(&alarm_info, &alarm_id, pid, app_service_name,app_service_name_mod,
		       reserved_service_name, reserved_service_name_mod, &return_code)) {
		ALARM_MGR_EXCEPTION_PRINT("Unable to create alarm! return_code[%d]", return_code);
		strncpy(log_tag, "FAIL: CREATE", strlen("FAIL: CREATE"));
		ret = false;
	} else {
		strncpy(log_tag, "CREATE", strlen("CREATE"));
		ret = true;
	}

	g_dbus_method_invocation_return_value(invoc, g_variant_new("(ii)", alarm_id, return_code));

	sprintf(log_message, "alarmID: %d, pid: %d, duetime: %d-%d-%d %02d:%02d:%02d",
		alarm_id, pid, start_year, start_month, start_day, start_hour, start_min, start_sec);
	__save_module_log(log_tag, log_message);

	return ret;
}

gboolean alarm_manager_alarm_delete(AlarmManager *obj, GDBusMethodInvocation *invoc,
					int pid, alarm_id_t alarm_id,
				    char *e_cookie, gpointer user_data)
{
	int retval = 0;
	int return_code = ALARMMGR_RESULT_SUCCESS;
	char log_tag[ALARMMGR_LOG_TAG_SIZE] = {0,};
	char log_message[ALARMMGR_LOG_MESSAGE_SIZE] = {0,};
	bool ret = true;

	retval = __check_privilege_by_cookie(e_cookie, "alarm-server::alarm", "w", true, pid);
	if (retval != ALARMMGR_RESULT_SUCCESS) {
		return_code = retval;
		g_dbus_method_invocation_return_value(invoc, g_variant_new("(i)", return_code));
		sprintf(log_message, "alarmID: %d, pid: %d, Smack denied (alarm-server::alarm, w)", alarm_id, pid);
		__save_module_log("FAIL: DELETE", log_message);
		return true;
	}

	if (!__alarm_delete(pid, alarm_id, &return_code)) {
		ALARM_MGR_EXCEPTION_PRINT("Unable to delete the alarm! alarm_id[%d], return_code[%d]", alarm_id, return_code);
		strncpy(log_tag, "FAIL: DELETE", strlen("FAIL: DELETE"));
		ret = false;
	} else {
		ALARM_MGR_EXCEPTION_PRINT("alarm_id[%d] is removed.", alarm_id);
		strncpy(log_tag, "DELETE", strlen("DELETE"));
		ret = true;
	}

	g_dbus_method_invocation_return_value(invoc, g_variant_new("(i)", return_code));

	sprintf(log_message, "alarmID: %d, pid: %d", alarm_id, pid);
	__save_module_log(log_tag, log_message);

	return ret;
}

gboolean alarm_manager_alarm_delete_all(AlarmManager *obj, GDBusMethodInvocation *invoc,
					int pid, char *e_cookie, gpointer user_data)
{
	GSList* gs_iter = NULL;
	char app_name[512] = { 0 };
	alarm_info_t* alarm_info = NULL;
	__alarm_info_t* entry = NULL;
	bool is_deleted = false;
	int retval = 0;
	int return_code = ALARMMGR_RESULT_SUCCESS;
	char log_message[ALARMMGR_LOG_MESSAGE_SIZE] = {0,};

	retval = __check_privilege_by_cookie(e_cookie, "alarm-server::alarm", "w", true, pid);
	if (retval != ALARMMGR_RESULT_SUCCESS) {
		return_code = retval;
		g_dbus_method_invocation_return_value(invoc, g_variant_new("(i)", return_code));
		sprintf(log_message, "pid: %d, Smack denied (alarm-server::alarm, w)", pid);
		__save_module_log("FAIL: DELETE ALL", log_message);
		return true;
	}

	if (!__get_caller_unique_name(pid, app_name)) {
		return_code = ERR_ALARM_SYSTEM_FAIL;
		g_dbus_method_invocation_return_value(invoc, g_variant_new("(i)", return_code));
		sprintf(log_message, "pid: %d. Can not get the unique_name.", pid);
		__save_module_log("FAIL: DELETE ALL", log_message);
		return true;
	}

	SECURE_LOGD("Called by process (pid:%d, unique_name=%s)", pid, app_name);

	for (gs_iter = alarm_context.alarms; gs_iter != NULL; )
	{
		bool is_found = false;
		entry = gs_iter->data;
		char* tmp_appname = g_quark_to_string(entry->quark_app_unique_name);
		SECURE_LOGD("Try to remove app_name[%s], alarm_id[%d]\n", tmp_appname, entry->alarm_id);
		if (tmp_appname && strncmp(app_name, tmp_appname, strlen(tmp_appname)) == 0)
		{
			if (_remove_from_scheduled_alarm_list(pid, entry->alarm_id))
			{
				is_deleted = true;
			}

			alarm_info = &entry->alarm_info;
			if (!(alarm_info->alarm_type & ALARM_TYPE_VOLATILE))
			{
				if(!_delete_alarms(entry->alarm_id))
				{
					SECURE_LOGE("_delete_alarms() is failed. pid[%d], alarm_id[%d]", pid, entry->alarm_id);
				}
			}
			is_found = true;
		}

		gs_iter = g_slist_next(gs_iter);

		if (is_found)
		{
			ALARM_MGR_EXCEPTION_PRINT("alarm_id[%d] is removed.", entry->alarm_id);
			SECURE_LOGD("Removing is done. app_name[%s], alarm_id [%d]\n", tmp_appname, entry->alarm_id);
			alarm_context.alarms = g_slist_remove(alarm_context.alarms, entry);
			g_free(entry);
		}
	}

	if (is_deleted && (g_slist_length(g_scheduled_alarm_list) == 0))
	{
		_alarm_disable_timer(alarm_context);
		_alarm_schedule();
	}

	__rtc_set();
	g_dbus_method_invocation_return_value(invoc, g_variant_new("(i)", return_code));
	return true;
}

gboolean alarm_manager_alarm_update(AlarmManager *pObj, GDBusMethodInvocation *invoc, int pid,
				    char *app_service_name, alarm_id_t alarm_id,
				    int start_year, int start_month,
				    int start_day, int start_hour,
				    int start_min, int start_sec, int end_year,
				    int end_month, int end_day,
				    int mode_day_of_week, int mode_repeat,
				    int alarm_type, int reserved_info,
				    gpointer user_data)
{
	int return_code = ALARMMGR_RESULT_SUCCESS;
	alarm_info_t alarm_info;
	bool ret = true;
	char log_tag[ALARMMGR_LOG_TAG_SIZE] = {0,};
	char log_message[ALARMMGR_LOG_MESSAGE_SIZE] = {0,};

	alarm_info.start.year = start_year;
	alarm_info.start.month = start_month;
	alarm_info.start.day = start_day;
	alarm_info.start.hour = start_hour;
	alarm_info.start.min = start_min;
	alarm_info.start.sec = start_sec;

	alarm_info.end.year = end_year;
	alarm_info.end.month = end_month;
	alarm_info.end.day = end_day;

	alarm_info.mode.u_interval.day_of_week = mode_day_of_week;
	alarm_info.mode.repeat = mode_repeat;

	alarm_info.alarm_type = alarm_type;
	alarm_info.reserved_info = reserved_info;

	if (!__alarm_update(pid, app_service_name, alarm_id, &alarm_info, &return_code)) {
		ALARM_MGR_EXCEPTION_PRINT("Unable to update the alarm! alarm_id[%d], return_code[%d]", alarm_id, return_code);
		strncpy(log_tag, "FAIL: UPDATE", strlen("FAIL: UPDATE"));
		ret = false;
	} else {
		strncpy(log_tag, "UPDATE", strlen("UPDATE"));
		ret = true;
	}

	g_dbus_method_invocation_return_value(invoc, g_variant_new("(i)", return_code));

	sprintf(log_message, "alarmID: %d, appname: %s, pid: %d, duetime: %d-%d-%d %02d:%02d:%02d",
		alarm_id, app_service_name, pid, start_year, start_month, start_day, start_hour, start_min, start_sec);
	__save_module_log(log_tag, log_message);

	return ret;
}

gboolean alarm_manager_alarm_get_number_of_ids(AlarmManager *pObject, GDBusMethodInvocation *invoc, int pid, char *e_cookie,
					       gpointer user_data)
{
	GSList *gs_iter = NULL;
	char app_name[256] = { 0 };
	__alarm_info_t *entry = NULL;
	int retval = 0;
	int num_of_ids = 0;
	int return_code = ALARMMGR_RESULT_SUCCESS;

	retval = __check_privilege_by_cookie(e_cookie, "alarm-server::alarm", "r", true, pid);
	if (retval != ALARMMGR_RESULT_SUCCESS) {
		return_code = retval;
		g_dbus_method_invocation_return_value(invoc, g_variant_new("(ii)", num_of_ids, return_code));
		return true;
	}

	if (!__get_caller_unique_name(pid, app_name)) {
		return_code = ERR_ALARM_SYSTEM_FAIL;
		g_dbus_method_invocation_return_value(invoc, g_variant_new("(ii)", num_of_ids, return_code));
		return true;
	}

	SECURE_LOGD("Called by process (pid:%d, unique_name:%s)", pid, app_name);

	for (gs_iter = alarm_context.alarms; gs_iter != NULL; gs_iter = g_slist_next(gs_iter)) {
		entry = gs_iter->data;
		SECURE_LOGD("app_name=%s, quark_app_unique_name=%s", app_name, g_quark_to_string(entry->quark_app_unique_name));
		if (strncmp(app_name, g_quark_to_string(entry->quark_app_unique_name), strlen(app_name)) == 0) {
			(num_of_ids)++;
			SECURE_LOGD("inc number of alarms of app (pid:%d, unique_name:%s) is %d.", pid, app_name, num_of_ids);
		}
	}

	SECURE_LOGD("number of alarms of the process (pid:%d, unique_name:%s) is %d.", pid, app_name, num_of_ids);
	g_dbus_method_invocation_return_value(invoc, g_variant_new("(ii)", num_of_ids, return_code));
	return true;
}

gboolean alarm_manager_alarm_get_list_of_ids(AlarmManager *pObject, GDBusMethodInvocation *invoc, int pid,
					     int max_number_of_ids, gpointer user_data)
{
	GSList *gs_iter = NULL;
	char app_name[512] = { 0 };
	__alarm_info_t *entry = NULL;
	int index = 0;
	GVariant* arr = NULL;
	GVariantBuilder* builder = NULL;
	int num_of_ids = 0;
	int return_code = ALARMMGR_RESULT_SUCCESS;

	if (max_number_of_ids <= 0) {
		SECURE_LOGE("called for pid(%d), but max_number_of_ids(%d) is less than 0.", pid, max_number_of_ids);
		g_dbus_method_invocation_return_value(invoc, g_variant_new("(@aiii)", g_variant_new("ai", NULL), num_of_ids, return_code));
		return true;
	}

	if (!__get_caller_unique_name(pid, app_name)) {
		return_code = ERR_ALARM_SYSTEM_FAIL;
		g_dbus_method_invocation_return_value(invoc, g_variant_new("(@aiii)", g_variant_new("ai", NULL), num_of_ids, return_code));
		return true;
	}

	SECURE_LOGD("Called by process (pid:%d, unique_name=%s).", pid, app_name);

	builder = g_variant_builder_new(G_VARIANT_TYPE ("ai"));
	for (gs_iter = alarm_context.alarms; gs_iter != NULL; gs_iter = g_slist_next(gs_iter)) {
		entry = gs_iter->data;
		if (strncmp(app_name, g_quark_to_string(entry->quark_app_unique_name), strlen(app_name)) == 0) {
			g_variant_builder_add (builder, "i", entry->alarm_id);
			index ++;
			SECURE_LOGE("called for alarmid(%d), but max_number_of_ids(%d) index %d.", entry->alarm_id, max_number_of_ids, index);
		}
	}

	arr = g_variant_new("ai", builder);
	num_of_ids = index;

	SECURE_LOGE("Called by pid (%d), but max_number_of_ids(%d) return code %d.", pid, num_of_ids, return_code);
	g_dbus_method_invocation_return_value(invoc, g_variant_new("(@aiii)", arr, num_of_ids, return_code));

	g_variant_builder_unref(builder);
	return true;
}

gboolean alarm_manager_alarm_get_appsvc_info(AlarmManager *pObject, GDBusMethodInvocation *invoc ,
				int pid, alarm_id_t alarm_id,
				char *e_cookie, gpointer user_data)
{
	bool found = false;
	GSList *gs_iter = NULL;
	__alarm_info_t *entry = NULL;
	int retval = 0;
	int return_code = ALARMMGR_RESULT_SUCCESS;
	gchar *b_data = NULL;

	SECURE_LOGD("called for pid(%d) and alarm_id(%d)\n", pid, alarm_id);

	retval = __check_privilege_by_cookie(e_cookie, "alarm-server::alarm", "r", false, pid);
	if (retval != ALARMMGR_RESULT_SUCCESS) {
		return_code = retval;
		g_dbus_method_invocation_return_value(invoc, g_variant_new("(si)", b_data, return_code));
		return true;
	}

	for (gs_iter = alarm_context.alarms; gs_iter != NULL; gs_iter = g_slist_next(gs_iter)) {
		entry = gs_iter->data;
		if (entry->alarm_id == alarm_id) {
			found = true;
			b_data = g_strdup(g_quark_to_string(entry->quark_bundle));
			break;
		}
	}

	if (found) {
		if (b_data && strlen(b_data) == 4 && strncmp(b_data, "null", 4) == 0) {
			ALARM_MGR_EXCEPTION_PRINT("The alarm(%d) is an regular alarm, not svc alarm.", alarm_id);
			return_code = ERR_ALARM_INVALID_TYPE;
		}
	} else {
		ALARM_MGR_EXCEPTION_PRINT("The alarm(%d) is not found.", alarm_id);
		return_code = ERR_ALARM_INVALID_ID;
	}

	g_dbus_method_invocation_return_value(invoc, g_variant_new("(si)", b_data, return_code));
	g_free(b_data);
	return true;
}

gboolean alarm_manager_alarm_get_info(AlarmManager *pObject, GDBusMethodInvocation *invoc, int pid,
				      alarm_id_t alarm_id, char *e_cookie, gpointer user_data)
{
	SECURE_LOGD("called for pid(%d) and alarm_id(%d)\n", pid, alarm_id);

	GSList *gs_iter = NULL;
	__alarm_info_t *entry = NULL;
	alarm_info_t *alarm_info = NULL;
	int retval = 0;
	int return_code = ALARMMGR_RESULT_SUCCESS;

	retval = __check_privilege_by_cookie(e_cookie, "alarm-server::alarm", "r", true, pid);
	if (retval != ALARMMGR_RESULT_SUCCESS) {
		return_code = retval;
		g_dbus_method_invocation_return_value(invoc, g_variant_new("(iiiiiiiiiiiiii)", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, return_code));
		return true;
	}

	for (gs_iter = alarm_context.alarms; gs_iter != NULL; gs_iter = g_slist_next(gs_iter)) {
		entry = gs_iter->data;
		if (entry->alarm_id == alarm_id) {
			alarm_info = &(entry->alarm_info);
			break;
		}
	}

	if (alarm_info == NULL) {
		ALARM_MGR_EXCEPTION_PRINT("The alarm(%d) is not found.", alarm_id);
		return_code = ERR_ALARM_INVALID_ID;
		g_dbus_method_invocation_return_value(invoc, g_variant_new("(iiiiiiiiiiiiii)", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, return_code));
	} else {
		ALARM_MGR_LOG_PRINT("The alarm(%d) is found.", alarm_id);
		g_dbus_method_invocation_return_value(invoc, g_variant_new("(iiiiiiiiiiiiii)", alarm_info->start.year, alarm_info->start.month,
							alarm_info->start.day, alarm_info->start.hour, alarm_info->start.min, alarm_info->start.sec, alarm_info->end.year, alarm_info->end.month,
							alarm_info->end.day, alarm_info->mode.u_interval.day_of_week, alarm_info->mode.repeat, alarm_info->alarm_type, alarm_info->reserved_info, return_code));
	}

	return true;
}

gboolean alarm_manager_alarm_get_next_duetime(AlarmManager *pObject, GDBusMethodInvocation *invoc, int pid,
				      alarm_id_t alarm_id, char *e_cookie, gpointer user_data)
{
	SECURE_LOGD("called for pid(%d) and alarm_id(%d)\n", pid, alarm_id);

	GSList *gs_iter = NULL;
	__alarm_info_t *entry = NULL;
	__alarm_info_t *find_item = NULL;
	int retval = 0;
	int return_code = ALARMMGR_RESULT_SUCCESS;
	time_t duetime = 0;

	retval = __check_privilege_by_cookie(e_cookie, "alarm-server::alarm", "r", true, pid);
	if (retval != ALARMMGR_RESULT_SUCCESS) {
		return_code = retval;
		g_dbus_method_invocation_return_value(invoc, g_variant_new("(ii)", duetime, return_code));
		return true;
	}

	for (gs_iter = alarm_context.alarms; gs_iter != NULL; gs_iter = g_slist_next(gs_iter)) {
		entry = gs_iter->data;
		if (entry->alarm_id == alarm_id) {
			find_item = entry;
			break;
		}
	}

	if (find_item == NULL) {
		ALARM_MGR_EXCEPTION_PRINT("The alarm(%d) is not found.", alarm_id);
		return_code = ERR_ALARM_INVALID_ID;
		g_dbus_method_invocation_return_value(invoc, g_variant_new("(ii)", duetime, return_code));
		return true;
	}

	duetime = _alarm_next_duetime(find_item);
	ALARM_MGR_LOG_PRINT("Next duetime : %s", ctime(&duetime));

	g_dbus_method_invocation_return_value(invoc, g_variant_new("(ii)", duetime, return_code));
	return true;
}

gboolean alarm_manager_alarm_get_all_info(AlarmManager *pObject, GDBusMethodInvocation *invoc, int pid, char *e_cookie, gpointer user_data)
{
	sqlite3 *alarmmgr_tool_db;
	char *db_path = NULL;
	char db_path_tmp[50] = {0,};
	time_t current_time = 0;
	struct tm current_tm;
	const char *query_for_creating_table =  "create table alarmmgr_tool \
					(alarm_id integer primary key,\
							duetime_epoch integer,\
							duetime text,\
							start_epoch integer,\
							end_epoch integer,\
							pid integer,\
							caller_pkgid text,\
							callee_pkgid text,\
							app_unique_name text,\
							app_service_name text,\
							dst_service_name text,\
							day_of_week integer,\
							repeat integer,\
							alarm_type integer)";
	const char *query_for_deleting_table = "drop table alarmmgr_tool";
	int return_code = ALARMMGR_RESULT_SUCCESS;
	GSList *gs_iter = NULL;
	__alarm_info_t *entry = NULL;
	char *error_message = NULL;

	return_code = __check_privilege_by_cookie(e_cookie, "alarm-server::alarm", "r", true, pid);
	if (return_code != ALARMMGR_RESULT_SUCCESS) {
		g_dbus_method_invocation_return_value(invoc, g_variant_new("(si)", db_path, return_code));
		return true;
	}

	// Open a DB
	time(&current_time);
	localtime_r(&current_time, &current_tm);
	sprintf(db_path_tmp, "/tmp/alarmmgr_%d%d%d_%02d%02d%02d.db",
		current_tm.tm_year + 1900, current_tm.tm_mon + 1, current_tm.tm_mday, current_tm.tm_hour, current_tm.tm_min, current_tm.tm_sec);
	db_path = strdup(db_path_tmp);

	if (db_util_open(db_path, &alarmmgr_tool_db, DB_UTIL_REGISTER_HOOK_METHOD) != SQLITE_OK) {
		ALARM_MGR_EXCEPTION_PRINT("Opening [%s] failed", db_path);
		return_code = ERR_ALARM_SYSTEM_FAIL;
		g_dbus_method_invocation_return_value(invoc, g_variant_new("(si)", db_path, return_code));
		free(db_path);
		return true;
	}

	// Drop a table
	if (sqlite3_exec(alarmmgr_tool_db, query_for_deleting_table, NULL, NULL, &error_message) != SQLITE_OK) {
		ALARM_MGR_EXCEPTION_PRINT("Deleting the table is failed. error message = %s", error_message);
	}

	// Create a table if it does not exist
	if (sqlite3_exec(alarmmgr_tool_db, query_for_creating_table, NULL, NULL, &error_message) != SQLITE_OK) {
		ALARM_MGR_EXCEPTION_PRINT("Creating the table is failed. error message = %s", error_message);
		sqlite3_close(alarmmgr_tool_db);
		return_code = ERR_ALARM_SYSTEM_FAIL;
		g_dbus_method_invocation_return_value(invoc, g_variant_new("(si)", db_path, return_code));
		free(db_path);
		return true;
	}

	// Get information of all alarms and save those into the DB.
	int index = 0;
	for (gs_iter = alarm_context.alarms; gs_iter != NULL; gs_iter = g_slist_next(gs_iter)) {
		entry = gs_iter->data;
		++index;
		SECURE_LOGD("#%d alarm id[%d] app_name[%s] duetime[%d]",
			index, entry->alarm_id, g_quark_to_string(entry->quark_app_unique_name), entry->start);

		alarm_info_t *alarm_info = (alarm_info_t *) &(entry->alarm_info);
		alarm_mode_t *mode = &alarm_info->mode;

		char *query = sqlite3_mprintf("insert into alarmmgr_tool( alarm_id, duetime_epoch, duetime, start_epoch,\
				end_epoch, pid, caller_pkgid, callee_pkgid, app_unique_name, app_service_name, dst_service_name, day_of_week, repeat, alarm_type)\
				values (%d,%d,%Q,%d,%d,%d,%Q,%Q,%Q,%Q,%Q,%d,%d,%d)",
				entry->alarm_id,
				(int)entry->due_time,
				ctime(&(entry->due_time)),
				(int)entry->start,
				(int)entry->end,
				(int)entry->pid,
				(char *)g_quark_to_string(entry->quark_caller_pkgid),
				(char *)g_quark_to_string(entry->quark_callee_pkgid),
				(char *)g_quark_to_string(entry->quark_app_unique_name),
				(char *)g_quark_to_string(entry->quark_app_service_name),
				(char *)g_quark_to_string(entry->quark_dst_service_name),
				mode->u_interval.day_of_week,
				mode->repeat,
				entry->alarm_info.alarm_type);

		if (sqlite3_exec(alarmmgr_tool_db, query, NULL, NULL, &error_message) != SQLITE_OK) {
			SECURE_LOGE("sqlite3_exec() is failed. error message = %s", error_message);
		}

		sqlite3_free(query);
	}

	sqlite3_close(alarmmgr_tool_db);

	return_code = ALARMMGR_RESULT_SUCCESS;
	g_dbus_method_invocation_return_value(invoc, g_variant_new("(si)", db_path, return_code));
	free(db_path);
	return true;
}

static void __timer_glib_finalize(GSource *src)
{
	GSList *fd_list;
	GPollFD *tmp;

	fd_list = src->poll_fds;
	do {
		tmp = (GPollFD *) fd_list->data;
		g_free(tmp);

		fd_list = fd_list->next;
	} while (fd_list);

	return;
}

static gboolean __timer_glib_check(GSource *src)
{
	GSList *fd_list;
	GPollFD *tmp;

	fd_list = src->poll_fds;
	do {
		tmp = (GPollFD *) fd_list->data;
		if (tmp->revents & (POLLIN | POLLPRI)) {
			return TRUE;
		}
		fd_list = fd_list->next;
	} while (fd_list);

	return FALSE;
}

static gboolean __timer_glib_dispatch(GSource *src, GSourceFunc callback,
				  gpointer data)
{
	callback(data);
	return TRUE;
}

static gboolean __timer_glib_prepare(GSource *src, gint *timeout)
{
	return FALSE;
}

GSourceFuncs funcs = {
	.prepare = __timer_glib_prepare,
	.check = __timer_glib_check,
	.dispatch = __timer_glib_dispatch,
	.finalize = __timer_glib_finalize
};

static void __initialize_timer()
{
	int fd;
	GSource *src;
	GPollFD *gpollfd;
	int ret;

	fd = timerfd_create(CLOCK_REALTIME, 0);
	if (fd == -1) {
		ALARM_MGR_EXCEPTION_PRINT("timerfd_create() is failed.\n");
		exit(1);
	}
	src = g_source_new(&funcs, sizeof(GSource));

	gpollfd = (GPollFD *) g_malloc(sizeof(GPollFD));
	gpollfd->events = POLLIN;
	gpollfd->fd = fd;

	g_source_add_poll(src, gpollfd);
	g_source_set_callback(src, (GSourceFunc) __alarm_handler_idle,
			      (gpointer) gpollfd, NULL);
	g_source_set_priority(src, G_PRIORITY_HIGH);

	ret = g_source_attach(src, NULL);
	if (ret == 0) {
		ALARM_MGR_EXCEPTION_PRINT("g_source_attach() is failed.\n");
		return;
	}

	g_source_unref(src);

	alarm_context.timer = fd;
}

static void __initialize_alarm_list()
{
	alarm_context.alarms = NULL;
	alarm_context.c_due_time = -1;

	_load_alarms_from_registry();

	__rtc_set();	/*Set RTC1 Alarm with alarm due time for alarm-manager initialization*/
}

static void __initialize_scheduled_alarm_list()
{
	_init_scheduled_alarm_list();
}

static bool __initialize_noti()
{
	// system state change noti
	if (vconf_notify_key_changed
	    (VCONFKEY_SETAPPL_TIMEZONE_ID, __on_time_zone_changed, NULL) < 0) {
		ALARM_MGR_LOG_PRINT(
			"Failed to add callback for time zone changing event\n");
	}

	if (vconf_notify_key_changed
	    (VCONFKEY_SYSTEM_TIMECHANGE_EXTERNAL, __on_system_time_external_changed, NULL) < 0) {
		ALARM_MGR_LOG_PRINT(
			"Failed to add callback for time external changing event\n");
	}

	// If the caller or callee app is uninstalled, all registered alarms will be canceled.
	int event_type = PMINFO_CLIENT_STATUS_UNINSTALL;
	pkgmgrinfo_client* pc = pkgmgrinfo_client_new(PMINFO_LISTENING);
	pkgmgrinfo_client_set_status_type(pc, event_type);
	pkgmgrinfo_client_listen_status(pc, __on_app_uninstalled, NULL);

	return true;
}

void on_bus_name_owner_changed(GDBusConnection  *connection,
										const gchar		*sender_name,
										const gchar		*object_path,
										const gchar		*interface_name,
										const gchar		*signal_name,
										GVariant			*parameters,
										gpointer			user_data)
{
	// On expiry, the killed app can be launched by aul. Then, the owner of the bus name which was registered by the app is changed.
	// In this case, "NameOwnerChange" signal is broadcasted.
	if (signal_name && strcmp(signal_name , "NameOwnerChanged") == 0) {
		GSList *entry = NULL;
		__expired_alarm_t *expire_info = NULL;
		char *service_name = NULL;
		g_variant_get(parameters, "(sss)", &service_name, NULL, NULL);

		for (entry = g_expired_alarm_list; entry; entry = entry->next) {
			if (entry->data) {
				expire_info = (__expired_alarm_t *) entry->data;
				SECURE_LOGD("expired service(%s), owner changed service(%s)", expire_info->service_name, service_name);

				if (strcmp(expire_info->service_name, service_name) == 0) {
					SECURE_LOGE("expired service name(%s) alarm_id (%d)", expire_info->service_name, expire_info->alarm_id);
					__alarm_send_noti_to_application(expire_info->service_name, expire_info->alarm_id);
					g_expired_alarm_list = g_slist_remove(g_expired_alarm_list, entry->data);
					g_free(expire_info);
				}
			}
		}
		g_free(service_name);
	}
}

static void on_bus_acquired(GDBusConnection *connection, const gchar *name, gpointer user_data)
{
	ALARM_MGR_EXCEPTION_PRINT("on_bus_acquired");

	interface = alarm_manager_skeleton_new();
	if (interface == NULL) {
		ALARM_MGR_EXCEPTION_PRINT("Creating a skeleton object is failed.");
		return;
	}

	g_signal_connect(interface, "handle_alarm_create", G_CALLBACK(alarm_manager_alarm_create), NULL);
	g_signal_connect(interface, "handle_alarm_create_appsvc", G_CALLBACK(alarm_manager_alarm_create_appsvc), NULL);
	g_signal_connect(interface, "handle_alarm_delete", G_CALLBACK(alarm_manager_alarm_delete), NULL);
	g_signal_connect(interface, "handle_alarm_delete_all", G_CALLBACK(alarm_manager_alarm_delete_all), NULL);
	g_signal_connect(interface, "handle_alarm_get_appsvc_info", G_CALLBACK(alarm_manager_alarm_get_appsvc_info), NULL);
	g_signal_connect(interface, "handle_alarm_get_info", G_CALLBACK(alarm_manager_alarm_get_info), NULL);
	g_signal_connect(interface, "handle_alarm_get_list_of_ids", G_CALLBACK(alarm_manager_alarm_get_list_of_ids), NULL);
	g_signal_connect(interface, "handle_alarm_get_next_duetime", G_CALLBACK(alarm_manager_alarm_get_next_duetime), NULL);
	g_signal_connect(interface, "handle_alarm_get_number_of_ids", G_CALLBACK(alarm_manager_alarm_get_number_of_ids), NULL);
	g_signal_connect(interface, "handle_alarm_set_rtc_time", G_CALLBACK(alarm_manager_alarm_set_rtc_time), NULL);
	g_signal_connect(interface, "handle_alarm_set_time", G_CALLBACK(alarm_manager_alarm_set_time), NULL);
	g_signal_connect(interface, "handle_alarm_update", G_CALLBACK(alarm_manager_alarm_update), NULL);
	g_signal_connect(interface, "handle_alarm_get_all_info", G_CALLBACK(alarm_manager_alarm_get_all_info), NULL);

	guint subsc_id = g_dbus_connection_signal_subscribe(connection, "org.freedesktop.DBus", "org.freedesktop.DBus",
							"NameOwnerChanged", "/org/freedesktop/DBus", NULL, G_DBUS_SIGNAL_FLAGS_NONE,
							on_bus_name_owner_changed, NULL, NULL);
	if (subsc_id == 0) {
		ALARM_MGR_EXCEPTION_PRINT("Subscribing to signal for invoking callback is failed.");
		g_object_unref(interface);
		interface = NULL;
		return;
	}

	if (!g_dbus_interface_skeleton_export(G_DBUS_INTERFACE_SKELETON(interface), connection, ALARM_MGR_DBUS_PATH, NULL)) {
		ALARM_MGR_EXCEPTION_PRINT("Exporting the interface is failed.");
		g_object_unref(interface);
		interface = NULL;
		return;
	}

	alarm_context.connection = connection;
	g_dbus_object_manager_server_set_connection(alarmmgr_server, alarm_context.connection);
}

static bool __initialize_dbus()
{
	ALARM_MGR_LOG_PRINT("__initialize_dbus Enter");

	alarmmgr_server = g_dbus_object_manager_server_new(ALARM_MGR_DBUS_PATH);
	if (alarmmgr_server == NULL) {
		ALARM_MGR_EXCEPTION_PRINT("Creating a new server object is failed.");
		return false;
	}

	guint owner_id = g_bus_own_name(G_BUS_TYPE_SYSTEM, ALARM_MGR_DBUS_NAME,
				G_BUS_NAME_OWNER_FLAGS_NONE, on_bus_acquired, NULL, NULL, NULL, NULL);

	if (owner_id == 0) {
		ALARM_MGR_EXCEPTION_PRINT("Acquiring the own name is failed.");
		g_object_unref(alarmmgr_server);
		return false;
	}

	return true;
}

#define ALARMMGR_DB_FILE "/opt/dbspace/.alarmmgr.db"
sqlite3 *alarmmgr_db;
#define QUERY_CREATE_TABLE_ALARMMGR "create table alarmmgr \
				(alarm_id integer primary key,\
						start integer,\
						end integer,\
						pid integer,\
						caller_pkgid text,\
						callee_pkgid text,\
						app_unique_name text,\
						app_service_name text,\
						app_service_name_mod text,\
						bundle text, \
						year integer,\
						month integer,\
						day integer,\
						hour integer,\
						min integer,\
						sec integer,\
						day_of_week integer,\
						repeat integer,\
						alarm_type integer,\
						reserved_info integer,\
						dst_service_name text, \
						dst_service_name_mod text \
						)"

static bool __initialize_db()
{
	char *error_message = NULL;
	int ret;

	if (access("/opt/dbspace/.alarmmgr.db", F_OK) == 0) {
		ret = db_util_open(ALARMMGR_DB_FILE, &alarmmgr_db, DB_UTIL_REGISTER_HOOK_METHOD);

		if (ret != SQLITE_OK) {
			ALARM_MGR_EXCEPTION_PRINT("====>>>> connect menu_db [%s] failed", ALARMMGR_DB_FILE);
			return false;
		}

		return true;
	}

	ret = db_util_open(ALARMMGR_DB_FILE, &alarmmgr_db, DB_UTIL_REGISTER_HOOK_METHOD);

	if (ret != SQLITE_OK) {
		ALARM_MGR_EXCEPTION_PRINT("====>>>> connect menu_db [%s] failed", ALARMMGR_DB_FILE);
		return false;
	}

	if (SQLITE_OK != sqlite3_exec(alarmmgr_db, QUERY_CREATE_TABLE_ALARMMGR, NULL, NULL, &error_message)) {
		ALARM_MGR_EXCEPTION_PRINT("Don't execute query = %s, error message = %s", QUERY_CREATE_TABLE_ALARMMGR, error_message);
		return false;
	}

	return true;
}

static void __initialize()
{
	g_type_init();

	__initialize_timer();
	if (__initialize_dbus() == false) {	/* because dbus's initialize
					failed, we cannot continue any more. */
		ALARM_MGR_EXCEPTION_PRINT("because __initialize_dbus failed, "
					  "alarm-server cannot be runned.\n");
		exit(1);
	}
	__initialize_scheduled_alarm_list();
	__initialize_db();
	__initialize_alarm_list();
	__initialize_noti();

	__initialize_module_log();	// for module log

}

int main()
{
	GMainLoop *mainloop = NULL;

	ALARM_MGR_LOG_PRINT("Enter main loop\n");

	mainloop = g_main_loop_new(NULL, FALSE);

	__initialize();

	g_main_loop_run(mainloop);

	return 0;
}
