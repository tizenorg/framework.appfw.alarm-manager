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



#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/types.h>
#include <string.h>
#include <glib.h>
#include "alarm.h"
#include "alarm-internal.h"
#include "alarm-mgr-stub.h"
#include "security-server.h"

#define ALARM_SERVICE_NAME	"appframework.alarm"
#define ALARM_OBJECT_PATH	"/appframework/alarm"
#define ALARM_INTERFACE_NAME "appframework.alarm"


bool _send_alarm_create(alarm_context_t context, alarm_info_t *alarm_info,
			alarm_id_t *alarm_id, const char *dst_service_name, const char *dst_service_name_mod,
			int *error_code);
bool _send_alarm_create_periodic(alarm_context_t context, int interval, int is_ref,
			int method, alarm_id_t *alarm_id, int *error_code);
bool _send_alarm_create_appsvc(alarm_context_t context, alarm_info_t *alarm_info,
			alarm_id_t *alarm_id, bundle *b,int *error_code);
bool _send_alarm_delete(alarm_context_t context, alarm_id_t alarm_id,
			int *error_code);
bool _send_alarm_get_list_of_ids(alarm_context_t context, int maxnum_of_ids,
				 alarm_id_t *alarm_id, int *num_of_ids,
				 int *error_code);
bool _send_alarm_get_number_of_ids(alarm_context_t context, int *num_of_ids,
				   int *error_code);
bool _send_alarm_get_info(alarm_context_t context, alarm_id_t alarm_id,
			  alarm_info_t *alarm_info, int *error_code);

char* __get_cookie(int *error_code)
{
	char *e_cookie = NULL;
	char cookie[256] = {0,};
	int size = 0;
	int retval = 0;

	size = security_server_get_cookie_size();
	retval = security_server_request_cookie(cookie, size);
	if (retval < 0) {
		ALARM_MGR_EXCEPTION_PRINT("security_server_request_cookie() is failed. retval = %d", retval);
		if (error_code) {
			*error_code = ERR_ALARM_SYSTEM_FAIL;
		}
		return NULL;
	}

	e_cookie = g_base64_encode((const guchar *)cookie, size);
	if (e_cookie == NULL) {
		ALARM_MGR_EXCEPTION_PRINT("g_base64_encode() is failed.");
		if (error_code) {
			*error_code = ERR_ALARM_SYSTEM_FAIL;
		}
		return NULL;
	}

	ALARM_MGR_LOG_PRINT("Gets the cookie successfully.");
	return e_cookie;
}


bool _send_alarm_create_appsvc(alarm_context_t context, alarm_info_t *alarm_info,
			alarm_id_t *alarm_id, bundle *b,
			int *error_code)
{
	GError *error = NULL;
	int return_code = 0;
	char *e_cookie = NULL;
	bundle_raw *b_data = NULL;
	int datalen = 0;

	if (bundle_encode(b, &b_data, &datalen))
	{
		ALARM_MGR_EXCEPTION_PRINT("Unable to encode the bundle data\n");
		if (error_code) {
			*error_code = ERR_ALARM_SYSTEM_FAIL;
		}
		return false;
	}

	e_cookie = __get_cookie(error_code);
	if (e_cookie == NULL) {
		ALARM_MGR_EXCEPTION_PRINT("Getting the cookie is failed. error_code = %d", *error_code);
		return false;
	}

	if (!alarm_manager_call_alarm_create_appsvc_sync((AlarmManager*)context.proxy, context.pid,
						    alarm_info->start.year,
						    alarm_info->start.month,
						    alarm_info->start.day,
						    alarm_info->start.hour,
						    alarm_info->start.min,
						    alarm_info->start.sec,
						    alarm_info->end.year,
						    alarm_info->end.month,
						    alarm_info->end.day,
						    alarm_info->mode.u_interval.day_of_week,
						    alarm_info->mode.u_interval.interval,
						    alarm_info->mode.repeat,
						    alarm_info->alarm_type,
						    alarm_info->reserved_info,
						    (char *)b_data, e_cookie,
						    alarm_id, &return_code,
						    NULL, &error)) {
		/* g_dbus_proxy_call_sync error */
		/* error_code should be set */
		ALARM_MGR_EXCEPTION_PRINT(
		"alarm_manager_call_alarm_create_appsvc_sync()failed. alarm_id[%d], return_code[%d].", alarm_id, return_code);
		if (error) {
			ALARM_MGR_EXCEPTION_PRINT("dbus error message: %s", error->message);
			g_error_free(error);
		}
	}

	g_free(e_cookie);

	if (b_data) {
		free(b_data);
		b_data = NULL;
	}

	if (return_code != 0) {
		if (error_code) {
			*error_code = return_code;
		}
		return false;
	}

	return true;
}


bool _send_alarm_create(alarm_context_t context, alarm_info_t *alarm_info,
			alarm_id_t *alarm_id, const char *dst_service_name, const char *dst_service_name_mod,
			int *error_code)
{
	GError *error = NULL;
	int return_code = 0;
	char *e_cookie = NULL;

	/*TODO: Dbus bus name validation is must & will be added to avoid alarm-server crash*/
	if (g_quark_to_string(context.quark_app_service_name) == NULL
		&& strlen(dst_service_name) == 4
		&& strncmp(dst_service_name, "null",4) == 0) {
			ALARM_MGR_EXCEPTION_PRINT("Invalid arg. Provide valid destination or call alarmmgr_init()\n");
		if (error_code) {
			*error_code = ERR_ALARM_INVALID_PARAM;
		}
		return false;
	}

	e_cookie = __get_cookie(error_code);
	if (e_cookie == NULL) {
		ALARM_MGR_EXCEPTION_PRINT("Getting the cookie is failed. error_code = %d", *error_code);
		return false;
	}

	if (!alarm_manager_call_alarm_create_sync((AlarmManager*)context.proxy, context.pid,
			g_quark_to_string(context.quark_app_service_name),
			g_quark_to_string(context.quark_app_service_name_mod),
						    alarm_info->start.year,
						    alarm_info->start.month,
						    alarm_info->start.day,
						    alarm_info->start.hour,
						    alarm_info->start.min,
						    alarm_info->start.sec,
						    alarm_info->end.year,
						    alarm_info->end.month,
						    alarm_info->end.day,
						    alarm_info->mode.u_interval.day_of_week,
						    alarm_info->mode.repeat,
						    alarm_info->alarm_type,
						    alarm_info->reserved_info,
						    dst_service_name, dst_service_name_mod, e_cookie,
						    alarm_id, &return_code,
						    NULL, &error)) {
		/* g_dbus_proxy_call_sync error error */
		/* error_code should be set */
		ALARM_MGR_EXCEPTION_PRINT(
		"alarm_manager_call_alarm_create_sync()failed. alarm_id[%d], return_code[%d]", alarm_id, return_code);
		if (error) {
			ALARM_MGR_EXCEPTION_PRINT("dbus error message: %s", error->message);
			g_error_free(error);
		}
		if (error_code) {
			*error_code = ERR_ALARM_SYSTEM_FAIL;
		}
		g_free(e_cookie);
		return false;
	}

	g_free(e_cookie);

	if (return_code != 0) {
		if (error_code) {
			*error_code = return_code;
		}
		return false;
	}

	return true;
}

bool _send_alarm_create_periodic(alarm_context_t context, int interval, int is_ref,
			int method, alarm_id_t *alarm_id, int *error_code)
{
	GError *error = NULL;
	int return_code = 0;
	char *e_cookie = NULL;

	if (g_quark_to_string(context.quark_app_service_name) == NULL) {
		ALARM_MGR_EXCEPTION_PRINT("Invalid arg. Provide valid destination or call alarmmgr_init()\n");
		if (error_code) {
			*error_code = ERR_ALARM_INVALID_PARAM;
		}
		return false;
	}

	e_cookie = __get_cookie(error_code);
	if (e_cookie == NULL) {
		ALARM_MGR_EXCEPTION_PRINT("Getting the cookie is failed. error_code = %d", *error_code);
		return false;
	}

	if (!alarm_manager_call_alarm_create_periodic_sync((AlarmManager*)context.proxy,
			g_quark_to_string(context.quark_app_service_name),
			g_quark_to_string(context.quark_app_service_name_mod),
			interval, is_ref, method, e_cookie, alarm_id, &return_code, NULL, &error)) {
		ALARM_MGR_EXCEPTION_PRINT("alarm_manager_call_alarm_create_periodic_sync()failed. alarm_id[%d], return_code[%d]",
			alarm_id, return_code);
		if (error) {
			ALARM_MGR_EXCEPTION_PRINT("dbus error message: %s", error->message);
			g_error_free(error);
		}
		if (error_code) {
			*error_code = ERR_ALARM_SYSTEM_FAIL;
		}
		g_free(e_cookie);
		return false;
	}

	g_free(e_cookie);

	if (return_code != 0) {
		if (error_code) {
			*error_code = return_code;
		}
		return false;
	}

	return true;
}

bundle *_send_alarm_get_appsvc_info(alarm_context_t context, alarm_id_t alarm_id, int *error_code)
{
	GError *error = NULL;
	int return_code = 0;
	bundle *b = NULL;
	char *e_cookie = NULL;
	gchar *b_data = NULL;

	e_cookie = __get_cookie(error_code);
	if (e_cookie == NULL) {
		ALARM_MGR_EXCEPTION_PRINT("Getting the cookie is failed. error_code = %d", *error_code);
		return false;
	}

	if (!alarm_manager_call_alarm_get_appsvc_info_sync
	    ((AlarmManager*)context.proxy, context.pid, alarm_id, e_cookie, &b_data, &return_code, NULL, &error)) {
		/* g_dbus_proxy_call_sync error */
		/*error_code should be set */
		ALARM_MGR_EXCEPTION_PRINT("alarm_manager_call_alarm_get_appsvc_info_sync() failed. alarm_id[%d], return_code[%d].", alarm_id, return_code);
		if (error) {
			ALARM_MGR_EXCEPTION_PRINT("dbus error message: %s", error->message);
			g_error_free(error);
		}
		if (error_code) {
			*error_code = ERR_ALARM_SYSTEM_FAIL;
		}

		g_free(e_cookie);

		if (b_data) {
			g_free(b_data);
		}

		return NULL;
	}

	if (return_code != 0) {
		if (error_code) {
			*error_code = return_code;
		}
	} else {
		b = bundle_decode((bundle_raw *)b_data, strlen(b_data));
	}

	g_free(e_cookie);

	if (b_data) {
		g_free(b_data);
	}

	return b;
}


bool _send_alarm_set_rtc_time(alarm_context_t context, alarm_date_t *time, int *error_code){

	GError *error = NULL;
	int return_code = 0;
	char *e_cookie = NULL;

	e_cookie = __get_cookie(error_code);
	if (e_cookie == NULL) {
		ALARM_MGR_EXCEPTION_PRINT("Getting the cookie is failed. error_code = %d", *error_code);
		return false;
	}

	if (!alarm_manager_call_alarm_set_rtc_time_sync
	    ((AlarmManager*)context.proxy, context.pid, time->year, time->month, time->day,
		 time->hour, time->min, time->sec, e_cookie, &return_code, NULL, &error)) {
		/* g_dbus_proxy_call_sync error */
		/*error_code should be set */
		ALARM_MGR_EXCEPTION_PRINT("alarm_manager_call_alarm_set_rtc_time() failed. return_code[%d]", return_code);
		if (error) {
			ALARM_MGR_EXCEPTION_PRINT("dbus error message: %s", error->message);
			g_error_free(error);
		}
		if (error_code) {
			*error_code = ERR_ALARM_SYSTEM_FAIL;
		}

		g_free(e_cookie);
		return false;
	}

	g_free(e_cookie);

	if (return_code != 0) {
		if (error_code) {
			*error_code = return_code;
		}
		return false;
	}

	return true;
}

bool _send_alarm_delete(alarm_context_t context, alarm_id_t alarm_id, int *error_code)
{
	GError *error = NULL;
	int return_code = 0;
	char *e_cookie = NULL;

	e_cookie = __get_cookie(error_code);
	if (e_cookie == NULL) {
		ALARM_MGR_EXCEPTION_PRINT("Getting the cookie is failed. error_code = %d", *error_code);
		return false;
	}

	if (!alarm_manager_call_alarm_delete_sync
	    ((AlarmManager*)context.proxy, context.pid, alarm_id, e_cookie, &return_code, NULL, &error)) {
		/* g_dbus_proxy_call_sync error */
		/*error_code should be set */
		ALARM_MGR_EXCEPTION_PRINT("alarm_manager_call_alarm_delete_sync() failed. alarm_id[%d], return_code[%d]", alarm_id, return_code);
		if (error) {
			ALARM_MGR_EXCEPTION_PRINT("dbus error message: %s", error->message);
			g_error_free(error);
		}
		if (error_code) {
			*error_code = ERR_ALARM_SYSTEM_FAIL;
		}

		g_free(e_cookie);
		return false;
	}

	g_free(e_cookie);

	if (return_code != 0) {
		if (error_code) {
			*error_code = return_code;
		}
		return false;
	}

	return true;
}

bool _send_alarm_delete_all(alarm_context_t context, int *error_code)
{
	GError *error = NULL;
	int return_code = 0;
	char *e_cookie = NULL;

	e_cookie = __get_cookie(error_code);
	if (e_cookie == NULL) {
		ALARM_MGR_EXCEPTION_PRINT("Getting the cookie is failed. error_code = %d", *error_code);
		return false;
	}

	if (!alarm_manager_call_alarm_delete_all_sync
	    ((AlarmManager*)context.proxy, context.pid, e_cookie, &return_code, NULL, &error)) {
		/* g_dbus_proxy_call_sync error */
		/*error_code should be set */
		ALARM_MGR_EXCEPTION_PRINT("alarm_manager_call_alarm_delete_all_sync() failed. return_code[%d]", return_code);
		if (error) {
			ALARM_MGR_EXCEPTION_PRINT("dbus error message: %s", error->message);
			g_error_free(error);
		}
		if (error_code) {
			*error_code = ERR_ALARM_SYSTEM_FAIL;
		}

		g_free(e_cookie);
		return false;
	}

	g_free(e_cookie);

	if (return_code != 0) {
		if (error_code) {
			*error_code = return_code;
		}
		return false;
	}

	return true;
}

bool _send_alarm_get_list_of_ids(alarm_context_t context, int maxnum_of_ids,
				 alarm_id_t *alarm_id, int *num_of_ids,
				 int *error_code)
{
	GError *error = NULL;
	GVariant *alarm_array = NULL;
	int return_code = 0;

	if (!alarm_manager_call_alarm_get_list_of_ids_sync((AlarmManager*)context.proxy,
			     context.pid, maxnum_of_ids, &alarm_array,
			     num_of_ids, &return_code, NULL, &error)) {
		/* g_dbus_proxy_call_sync error */
		/*error_code should be set */
		ALARM_MGR_EXCEPTION_PRINT(
		"alarm_manager_call_alarm_get_list_of_ids_sync() failed by dbus. alarm_id[%d], return_code[%d]", alarm_id, return_code);
		if (error) {
			ALARM_MGR_EXCEPTION_PRINT("dbus error message: %s", error->message);
			g_error_free(error);
		}
		if (error_code) {
			*error_code = ERR_ALARM_SYSTEM_FAIL;
		}

		return false;
	}

	if (return_code != 0) {
		if (error_code) {
			*error_code = return_code;
		}
		return false;
	}
	else
	{
		GVariantIter *iter = NULL;
		gint i = 0;
		g_variant_get (alarm_array, "ai", &iter);
		while (g_variant_iter_loop (iter, "i", &alarm_id[i]))
		{
			ALARM_MGR_LOG_PRINT("alarm_id (%d)", alarm_id[i]);
			i++;
		}
		g_variant_iter_free (iter);
		*num_of_ids = i;
		g_variant_unref(alarm_array);
	}

	return true;
}

bool _send_alarm_get_number_of_ids(alarm_context_t context, int *num_of_ids,
				   int *error_code)
{
	GError *error = NULL;
	gint return_code = 0;
	char *e_cookie = NULL;

	e_cookie = __get_cookie(error_code);
	if (e_cookie == NULL) {
		ALARM_MGR_EXCEPTION_PRINT("Getting the cookie is failed. error_code = %d", *error_code);
		return false;
	}

	if (!alarm_manager_call_alarm_get_number_of_ids_sync((AlarmManager*)context.proxy, context.pid, e_cookie, num_of_ids, &return_code, NULL, &error)) {
		/* g_dbus_proxy_call_sync error */
		/* error_code should be set */
		ALARM_MGR_EXCEPTION_PRINT("alarm_manager_call_alarm_get_number_of_ids_sync() failed by dbus. return_code[%d]", return_code);
		if (error) {
			ALARM_MGR_EXCEPTION_PRINT("dbus error message: %s", error->message);
			g_error_free(error);
		}
		if (error_code) {
			*error_code = ERR_ALARM_SYSTEM_FAIL;
		}
		return false;
	}

	if (return_code != 0) {
		if (error_code) {
			*error_code = return_code;
		}
		return false;
	}

	return true;
}

bool _send_alarm_get_info(alarm_context_t context, alarm_id_t alarm_id,
			  alarm_info_t *alarm_info, int *error_code)
{
	GError *error = NULL;
	int return_code = 0;
	char *e_cookie = NULL;

	e_cookie = __get_cookie(error_code);
	if (e_cookie == NULL) {
		ALARM_MGR_EXCEPTION_PRINT("Getting the cookie is failed. error_code = %d", *error_code);
		return false;
	}

	if (!alarm_manager_call_alarm_get_info_sync((AlarmManager*)context.proxy,
		context.pid, alarm_id, e_cookie, &alarm_info->start.year,
		&alarm_info->start.month, &alarm_info->start.day,
		&alarm_info->start.hour, &alarm_info->start.min,
		&alarm_info->start.sec, &alarm_info->end.year,
		&alarm_info->end.month, &alarm_info->end.day,
		&alarm_info->mode.u_interval.day_of_week,
		(gint *)&alarm_info->mode.repeat,
		&alarm_info->alarm_type, &alarm_info->reserved_info, &return_code, NULL, &error)) {
		/* g_dbus_proxy_call_sync error */
		/* error_code should be set */
		ALARM_MGR_EXCEPTION_PRINT(
		"alarm_manager_call_alarm_get_info_sync() failed by dbus. alarm_id[%d], return_code[%d]", alarm_id, return_code);
		if (error) {
			ALARM_MGR_EXCEPTION_PRINT("dbus error message: %s", error->message);
			g_error_free(error);
		}
		if (error_code) {
			*error_code = ERR_ALARM_SYSTEM_FAIL;
		}
		g_free(e_cookie);
		return false;
	}

	g_free(e_cookie);

	if (return_code != 0) {
		if (error_code) {
			*error_code = return_code;
		}
		return false;
	}

	return true;
}

bool _send_alarm_get_next_duetime(alarm_context_t context,
				 alarm_id_t alarm_id, time_t* duetime,
				 int *error_code)
{
	GError *error = NULL;
	int return_code = 0;
	char *e_cookie = NULL;

	e_cookie = __get_cookie(error_code);
	if (e_cookie == NULL) {
		ALARM_MGR_EXCEPTION_PRINT("Getting the cookie is failed. error_code = %d", *error_code);
		return false;
	}

	if (!alarm_manager_call_alarm_get_next_duetime_sync((AlarmManager*)context.proxy,
			     context.pid, alarm_id, e_cookie, (gint *)duetime, &return_code, NULL, &error)) {
		/*g_dbus_proxy_call_sync error */
		/*error_code should be set */
		ALARM_MGR_EXCEPTION_PRINT(
		"alarm_manager_call_alarm_get_next_duetime_sync() failed by dbus. alarm_id[%d], return_code[%d]", alarm_id, return_code);
		if (error) {
			ALARM_MGR_EXCEPTION_PRINT("dbus error message: %s", error->message);
			g_error_free(error);
		}
		if (error_code) {
			*error_code = ERR_ALARM_SYSTEM_FAIL;
		}
		g_free(e_cookie);
		return false;
	}

	g_free(e_cookie);

	if (return_code != 0) {
		if (error_code) {
			*error_code = return_code;
		}
		return false;
	}

	return true;
}

bool _send_alarm_get_all_info(alarm_context_t context, char ** db_path, int *error_code)
{
	GError *error = NULL;
	int return_code = 0;
	char *e_cookie = NULL;

	e_cookie = __get_cookie(error_code);
	if (e_cookie == NULL) {
		ALARM_MGR_EXCEPTION_PRINT("Getting the cookie is failed. error_code = %d", *error_code);
		return false;
	}

	if (!alarm_manager_call_alarm_get_all_info_sync((AlarmManager*)context.proxy, context.pid, e_cookie, db_path, &return_code, NULL, &error)) {
		/*g_dbus_proxy_call_sync error */
		/*error_code should be set */
		ALARM_MGR_EXCEPTION_PRINT("alarm_manager_call_alarm_get_all_info_sync() failed by dbus. return_code[%d]", return_code);
		if (error) {
			ALARM_MGR_EXCEPTION_PRINT("dbus error message: %s", error->message);
			g_error_free(error);
		}
		if (error_code) {
			*error_code = ERR_ALARM_SYSTEM_FAIL;
		}
		g_free(e_cookie);
		return false;
	}

	g_free(e_cookie);

	if (return_code != ALARMMGR_RESULT_SUCCESS) {
		if (error_code) {
			*error_code = return_code;
		}
		return false;
	}

	return true;
}

bool _send_alarm_set_time(alarm_context_t context, int new_time, int *error_code)
{
	GError *error = NULL;
	int return_code = 0;

	if (!alarm_manager_call_alarm_set_time_sync((AlarmManager*)context.proxy, new_time, &return_code, NULL, &error)) {
		/*g_dbus_proxy_call_sync error */
		/*error_code should be set */
		ALARM_MGR_EXCEPTION_PRINT("alarm_manager_call_alarm_set_time_sync() failed by dbus. return_code[%d]", return_code);
		if (error) {
			ALARM_MGR_EXCEPTION_PRINT("dbus error message: %s", error->message);
			g_error_free(error);
		}
		if (error_code) {
			*error_code = ERR_ALARM_SYSTEM_FAIL;
		}
		return false;
	}

	if (return_code != ALARMMGR_RESULT_SUCCESS) {
		if (error_code) {
			*error_code = return_code;
		}
		return false;
	}

	return true;
}


bool _send_alarm_set_time_with_propagation_delay(alarm_context_t context, unsigned int new_sec, unsigned int new_nsec, unsigned int req_sec, unsigned int req_nsec, int *error_code)
{
	GError *error = NULL;
	int return_code = 0;

	if (!alarm_manager_call_alarm_set_time_with_propagation_delay_sync((AlarmManager*)context.proxy, new_sec, new_nsec, req_sec, req_nsec, &return_code, NULL, &error)) {
		/*g_dbus_proxy_call_sync error */
		/*error_code should be set */
		ALARM_MGR_EXCEPTION_PRINT("alarm_manager_call_alarm_set_time_with_propagation_delay_sync() failed by dbus. return_code[%d]", return_code);
		if (error) {
			ALARM_MGR_EXCEPTION_PRINT("dbus error message: %s", error->message);
			g_error_free(error);
		}
		if (error_code) {
			*error_code = ERR_ALARM_SYSTEM_FAIL;
		}
		return false;
	}

	if (return_code != ALARMMGR_RESULT_SUCCESS) {
		if (error_code) {
			*error_code = return_code;
		}
		return false;
	}

	return true;
}

bool _send_alarm_set_timezone(alarm_context_t context, char *tzpath_str, int *error_code)
{
	GError *error = NULL;
	int return_code = 0;

	if (!alarm_manager_call_alarm_set_timezone_sync((AlarmManager*)context.proxy, tzpath_str, &return_code, NULL, &error)) {
		/*g_dbus_proxy_call_sync error */
		/*error_code should be set */
		ALARM_MGR_EXCEPTION_PRINT("alarm_manager_call_alarm_set_timezone_sync() failed by dbus. return_code[%d]", return_code);
		if (error) {
			ALARM_MGR_EXCEPTION_PRINT("dbus error message: %s", error->message);
			g_error_free(error);
		}
		if (error_code) {
			*error_code = ERR_ALARM_SYSTEM_FAIL;
		}
		return false;
	}

	if (return_code != ALARMMGR_RESULT_SUCCESS) {
		if (error_code) {
			*error_code = return_code;
		}
		return false;
	}

	return true;
}
