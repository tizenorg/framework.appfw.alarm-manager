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
#include <time.h>
#include <tet_api.h>
#include <sys/types.h>
#include <unistd.h>

#include "alarm.h"

static void startup(void);
static void cleanup(void);

void (*tet_startup)(void) = startup;
void (*tet_cleanup)(void) = cleanup;

static void utc_ApplicationFW_alarmmgr_get_repeat_mode_func_01(void);
static void utc_ApplicationFW_alarmmgr_get_repeat_mode_func_02(void);

enum {
	POSITIVE_TC_IDX = 0x01,
	NEGATIVE_TC_IDX,
};

struct tet_testlist tet_testlist[] = {
	{ utc_ApplicationFW_alarmmgr_get_repeat_mode_func_01, POSITIVE_TC_IDX },
	{ utc_ApplicationFW_alarmmgr_get_repeat_mode_func_02, NEGATIVE_TC_IDX },
	{ NULL , 0 }
};

static int pid;

static void startup(void)
{
}

static void cleanup(void)
{
}

/**
 * @brief Positive test case of alarmmgr_get_repeat_mode()
 */
static void utc_ApplicationFW_alarmmgr_get_repeat_mode_func_01(void)
{
	int ret_val = ALARMMGR_RESULT_SUCCESS;
	alarm_entry_t* alarm;
	alarm_repeat_mode_t repeat;
	int interval;

	alarm = alarmmgr_create_alarm();
	if(alarm == NULL)
	{
		 tet_infoline("\nAlarm Manager : call to alarmmgr_create_alarm() is failed \n");
		 tet_result(TET_FAIL);
	}
	else
		{
			ret_val =alarmmgr_get_repeat_mode(alarm, &repeat, &interval) ;
			if(ret_val == ALARMMGR_RESULT_SUCCESS && repeat == ALARM_REPEAT_MODE_ONCE)
			{
				 tet_infoline("\nAlarm Manager : call to alarmmgr_get_repeat_mode() is successful \n");
				 tet_result(TET_PASS);
			}
			else
			{
				 tet_infoline("\nAlarm Manager : call to alarmmgr_get_repeat_mode() failed \n");
				 tet_result(TET_FAIL);
			}
			alarmmgr_free_alarm(alarm) ;
		}
}

/**
 * @brief Negative test case of ug_init alarmmgr_get_repeat_mode()
 */
static void utc_ApplicationFW_alarmmgr_get_repeat_mode_func_02(void)
{
	int ret_val = ALARMMGR_RESULT_SUCCESS;
	alarm_entry_t* alarm = NULL;
	alarm_repeat_mode_t repeat;
	int interval;
	
	ret_val = alarmmgr_get_repeat_mode( alarm, &repeat, &interval);
	if(ret_val == ERR_ALARM_INVALID_PARAM)
	{
		 tet_infoline("\nAlarm Manager : call to alarmmgr_get_repeat_mode() with invalid parameter  is successful \n");
		 tet_result(TET_PASS);
	}
	else
	{
		 tet_infoline("\nAlarm Manager : call to alarmmgr_get_repeat_mode() failed \n");
		 tet_result(TET_FAIL);
	}
}
