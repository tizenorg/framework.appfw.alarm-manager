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




#ifndef _ALARM_INTERNAL_H
#define _ALARM_INTERNAL_H

#define MAX_SNOOZE_CNT 5
#define REPEAT_MODE_ONCE 0x80

#define SIG_TIMER 0x32
#define ALARM_INFO_MAX 100

#include "alarm.h"
#include <dbus/dbus-glib.h>
#include <glib.h>
#include <dlog.h>
#include <bundle.h>
#include <appsvc.h>

#define INIT_ALARM_LIST_SIZE 64
#define INIT_SCHEDULED_ALARM_LIST_SIZE 32
#define MAX_BUNDLE_NAME_LEN 2048
#define MAX_SERVICE_NAME_LEN 256
#define MAX_PKG_NAME_LEN MAX_SERVICE_NAME_LEN-8

#define SYSTEM_TIME_CHANGED "setting_time_changed"

#ifdef LOG_TAG
#undef LOG_TAG
#endif
#define LOG_TAG "ALARM_MANAGER"

/*Application ID for native application which is not launched by application 
server.*/
#define ALARM_NATIVE_APP_ID 99999
/*  Application Instance ID for native application which is not launched by 
application server.*/
#define ALARM_NATIVE_APP_INST_ID 99999
/*  Prefix of dbus service name of native application.*/
#define ALARM_NATIVE_APP_DBUS_SVC_NAME_PREFIX "NATIVE"

/* how to send expire event : gproxy or low level dbus
* if you want to use gproxy for receiving expire_event, please enable 
* _EXPIRE_ALARM_INTERFACE_IS_DBUS_GPROXY_ feature
* otherwise, lowlevel dbus interface will be used for receiving expire_event.
* Now we use low level dbus instead of gproxy
*/
/*#define	_EXPIRE_ALARM_INTERFACE_IS_DBUS_GPROXY_ */

typedef struct {
	DBusGConnection *bus;
	DBusGProxy *proxy;
	alarm_cb_t alarm_handler;
	void *user_param;
	int pid;		/* this specifies pid*/
	GQuark quark_app_service_name;	/*dbus_service_name is converted 
	 to quark value*/
	 GQuark quark_app_service_name_mod;
} alarm_context_t;

typedef union {
	int day_of_week;			/**< days of a week */
	time_t interval;
} alarm_interval_u;

/**
* This struct has mode of an alarm
*/
typedef struct {
	alarm_interval_u u_interval;
	alarm_repeat_mode_t repeat;	/**< repeat mode */
} alarm_mode_t;

/**
*  This enumeration has alarm type

typedef enum
{
	ALARM_TYPE_DEFAULT = 0x0,	
	ALARM_TYPE_RELATIVE = 0x01,	
	ALARM_TYPE_VOLATILE = 0x02,	
}alarm_type_t;
*/
#define	ALARM_TYPE_RELATIVE		0x80000000	/**< relative  */

/**
* This struct has the information of an alarm
*/

typedef struct {
	alarm_date_t start; /**< start time of the alarm */
	alarm_date_t end;   /**< end time of the alarm */
	alarm_mode_t mode;	/**< mode of alarm */
	int alarm_type;	    /**< alarm type*/
	int reserved_info;
} alarm_info_t;

bool _send_alarm_create(alarm_context_t context, alarm_info_t *alarm,
			 alarm_id_t *id, const char *dst_service_name,const char *dst_service_name_mod,
			 int *error_code);
bool _send_alarm_create_appsvc(alarm_context_t context, alarm_info_t *alarm_info,
			alarm_id_t *alarm_id, bundle *b,int *error_code);
bool _send_alarm_update(alarm_context_t context, int pid, alarm_id_t alarm_id,
			 alarm_info_t *alarm_info, int *error_code);
bool _send_alarm_delete(alarm_context_t context, alarm_id_t alarm_id,
			 int *error_code);
bool _send_alarm_get_list_of_ids(alarm_context_t context, int maxnum_of_ids,
				  alarm_id_t *alarm_id, int *num_of_ids,
				  int *error_code);
bool _send_alarm_get_number_of_ids(alarm_context_t context, int *num_of_ids,
				    int *error_code);
bool _send_alarm_get_info(alarm_context_t context, alarm_id_t alarm_id,
			   alarm_info_t *alarm_info, int *error_code);
bool _send_alarm_reset(alarm_context_t context, int *error_code);

bool _send_alarm_power_on(alarm_context_t context, bool on_off,
			   int *error_code);
bool _send_alarm_check_next_duetime(alarm_context_t context, int *error_code);
bool _send_alarm_power_off(alarm_context_t context, int *error_code);
bool _remove_from_scheduled_alarm_list(int pid, alarm_id_t alarm_id);
bool _load_alarms_from_registry();
bool _alarm_find_mintime_power_on(time_t *min_time);
bundle *_send_alarm_get_appsvc_info(alarm_context_t context, alarm_id_t alarm_id, int *error_code);
bool _send_alarm_set_rtc_time(alarm_context_t context, alarm_date_t *time, int *error_code);

/*  alarm manager*/
typedef struct {
	time_t start;
	time_t end;

	int alarm_id;
	int pid;
	GQuark quark_app_unique_name;	/*the fullpath of application's pid is
		converted to quark value.*/
	GQuark quark_app_service_name;	/*dbus_service_name is converted  to
		quark value.app_service_name is a service name  of application
		that creates alarm_info.*/
	GQuark quark_app_service_name_mod;
	GQuark quark_dst_service_name;	/*dbus_service_name is converted to 
		quark value.app_service_name is a service name  for 
		dst_service_name of alarm_create_extend().*/
	GQuark quark_dst_service_name_mod;
	time_t due_time;

	GQuark quark_bundle;	/*Bundle Content containing app-svc info*/

	alarm_info_t alarm_info;

} __alarm_info_t;

typedef struct {
	bool used;
	__alarm_info_t *__alarm_info;
} __alarm_entry_t;

typedef struct {
	timer_t timer;
	time_t c_due_time;
	GSList *alarms;
	int gmt_idx;
	int dst;
	DBusGConnection *bus;
} __alarm_server_context_t;

typedef struct {
	bool used;
	alarm_id_t alarm_id;
	int pid;
	__alarm_info_t *__alarm_info;
} __scheduled_alarm_t;

typedef struct {
	char service_name[MAX_SERVICE_NAME_LEN];
	alarm_id_t alarm_id;
} __expired_alarm_t;

time_t _alarm_next_duetime(__alarm_info_t *alarm_info);
bool _alarm_schedule();
bool _clear_scheduled_alarm_list();
bool _add_to_scheduled_alarm_list(__alarm_info_t *__alarm_info);

bool _save_alarms(__alarm_info_t *__alarm_info);
bool _delete_alarms(alarm_id_t alarm_id);
bool _update_alarms(__alarm_info_t *__alarm_info);

timer_t _alarm_create_timer();
bool _alarm_destory_timer(timer_t timer);
bool _alarm_set_timer(__alarm_server_context_t *alarm_context, timer_t timer,
		       time_t due_time, alarm_id_t id);
bool _alarm_disable_timer(__alarm_server_context_t alarm_context);
bool _init_scheduled_alarm_list();

int _set_rtc_time(time_t _time);
int _set_sys_time(time_t _time);
int _set_time(time_t _time);


#ifdef _DEBUG_MODE_
#define ALARM_MGR_LOG_PRINT(FMT, ARG...)  do { printf("%5d", getpid()); printf
	("%s() : "FMT"\n", __FUNCTION__, ##ARG); } while (false)
#define ALARM_MGR_EXCEPTION_PRINT(FMT, ARG...)  do { printf("%5d", getpid()); 
	printf("%s() : "FMT"\n", __FUNCTION__, ##ARG); } while (false)
#define ALARM_MGR_ASSERT_PRINT(FMT, ARG...) do { printf("%5d", getpid()); printf
	("%s() : "FMT"\n", __FUNCTION__, ##ARG); } while (false)
#else
#define ALARM_MGR_LOG_PRINT(FMT, ARG...) SLOGD(FMT, ##ARG);
#define ALARM_MGR_EXCEPTION_PRINT(FMT, ARG...) SLOGW(FMT, ##ARG);
#define ALARM_MGR_ASSERT_PRINT(FMT, ARG...) SLOGE(FMT, ##ARG);
#endif

/* int alarmmgr_check_next_duetime();*/


#endif /*_ALARM_INTERNAL_H*/
