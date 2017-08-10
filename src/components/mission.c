/*
 * mission.c
 *
 *  Created on: 2017年7月23日
 *      Author: Cotyledon
 */

/* RTOS & rx23t include files. */
#include "FreeRTOS.h"
#include "task.h"
#include "platform.h"
#include "r_cg_icu.h"
#include "r_cg_port.h"

/*-----------------------------------------------------------*/
/* User include files. */
#include "mavlink_receive.h"
#include "danger_check.h"
#include "ppm_encoder.h"
#include "alt_control.h"
#include "pos_control.h"
#include "matrix_key.h"
#include "cam_commu.h"
#include "mission.h"
#include "io.h"

/*-----------------------------------------------------------*/
/* private variables */
/* mission task handle structure, used for IRQ send task notification. */
static TaskHandle_t mission_taskhandle;
static volatile float dest_Height;
static volatile int8_t mission = -1;
static volatile float mission_kp, mission_ki, mission_kd;

/*-----------------------------------------------------------*/
/* private functions declaration. */
static void mission_task_entry(void *pvParameters);
static void arm(uint16_t flight_mode);
static void disarm(void);
static void red_led_warning(void);

static void mission_1(const float dest_Height);
static void mission_2(const float dest_Height);

/*-----------------------------------------------------------*/
/* global functions definition. */

/* ----------------------------------------------------------
 *
 * when danger check task(in danger_check.c) finds emergency
 * occurs, this function is called by that task to stop the
 * copter.
 *
 * --------------------------------------------------------*/
void is_emergency_now(void)
{
    LED2 = LED_ON;
    send_ppm(channel_val_MID,channel_val_MID,channel_val_MIN,channel_val_MID,Stabilize,EMERGENCY_ON);
}

/* ----------------------------------------------------------
 *
 * create the mission task, and start IRQ.
 *
 * --------------------------------------------------------*/
void mission_init(void)
{
    BaseType_t ret;
    ret = xTaskCreate(mission_task_entry,
                      "mission",
                      configMINIMAL_STACK_SIZE * 2,
                      NULL,
                      MISSION_TASK_PRI,
                      &mission_taskhandle);
    configASSERT(ret == pdPASS);

    R_ICU_IRQ1_Start();
}

void send_mission_params(int8_t _mission, float _dest_Height, float kp, float ki, float kd)
{
    mission = _mission;
    dest_Height = _dest_Height;
    mission_kp = kp;
    mission_ki = ki;
    mission_kd = kd;
    xTaskNotifyGive(mission_taskhandle);
}

void camera_finded(void)
{
    xTaskNotifyGive(mission_taskhandle);
}

/* ----------------------------------------------------------
 *
 * send start signal to the mission task.
 *
 * --------------------------------------------------------*/
void IRQ1_IntHandler(void)
{
    if(U_IRQ1_Pin_Read()) {
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        vTaskNotifyGiveFromISR(mission_taskhandle, &xHigherPriorityTaskWoken);
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);;
    }
}

/*-----------------------------------------------------------*/
/* private functions definition. */

/* ----------------------------------------------------------
 *
 * the real task function, which is created by mission_init(),
 * read from matrix keyboard to select & execute different
 * missions.
 *
 * --------------------------------------------------------*/
static void mission_task_entry(void *pvParameters)
{
    while(1) {
        io_input();
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        switch (mission) {
        case MISSION_1:
            LED2 = LED_ON;
            /* wait for start signal from IRQ which connected to a remote control. */
            xTaskNotifyStateClear(NULL);
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            red_led_warning();
            start_mission_timer();
            mission_1(dest_Height);
            stop_mission_timer();
            break;
        case MISSION_2:
            LED2 = LED_ON;
            /* wait for start signal from IRQ which connected to a remote control. */
            xTaskNotifyStateClear(NULL);
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            red_led_warning();
            start_mission_timer();
            mission_2(dest_Height);
            stop_mission_timer();
            break;
        case MISSION_3:
            LED2 = LED_ON;
            /* wait for start signal from IRQ which connected to a remote control. */
            xTaskNotifyStateClear(NULL);
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            red_led_warning();
            start_mission_timer();
            stop_mission_timer();
            break;
        default:
            /* select a mission number which does not exist. */
            break;
        }
    }
}

/* ----------------------------------------------------------
 *
 * arm the copter, and only arm & disarm has a channel value
 * for emergency.
 *
 * --------------------------------------------------------*/
static void arm(uint16_t flight_mode)
{
    send_ppm(channel_val_MID,channel_val_MID,channel_val_MIN,channel_val_MAX,flight_mode,EMERGENCY_OFF);
    vTaskDelay(pdMS_TO_TICKS(4000));
    send_ppm(channel_val_MID,channel_val_MID,channel_val_MIN,channel_val_MID,flight_mode,EMERGENCY_OFF);
    vTaskDelay(pdMS_TO_TICKS(2000));
}

/* ----------------------------------------------------------
 *
 * disarm the copter, and only arm & disarm has a channel
 * value for emergency.
 *
 * --------------------------------------------------------*/
static void disarm(void)
{
    send_ppm(channel_val_MID,channel_val_MID,channel_val_MIN,channel_val_MIN,Stabilize,0);
    vTaskDelay(pdMS_TO_TICKS(5000));
    send_ppm(channel_val_MID,channel_val_MID,channel_val_MIN,channel_val_MID,Stabilize,0);
}

/* ----------------------------------------------------------
 *
 * the process in altitude hold mode, it is called by mission
 * task, and it's PPM does not include emergency channel or
 * other unnecessary channels, so that the danger check or
 * PID control won't be influenced.
 *
 * --------------------------------------------------------*/
static void mission_1(const float dest_Height)
{
    float up_throttle;
    U_PORT_Camera_mode_select(CAM_MODE_BLACK);
    /* arm & clime up. */
    arm(Alt_Hold);
    send_ppm(0,0,channel_percent(20),0,Alt_Hold,0);
    vTaskDelay(pdMS_TO_TICKS(500));
    send_ppm(0,0,channel_percent(50),0,Alt_Hold,0);
    vTaskDelay(pdMS_TO_TICKS(500));
    send_ppm(0,0,channel_percent(55),0,Alt_Hold,0);
    vTaskDelay(pdMS_TO_TICKS(500));
    send_ppm(0,0,channel_percent(60),0,Alt_Hold,0);
    vTaskDelay(pdMS_TO_TICKS(500));

    position_ctl_start(pdFALSE, mission_kp, mission_ki, mission_kd);
    while(current_Height + DEST_HEIGHT_CUSHION < dest_Height) {
        up_throttle = (1.0 - current_Height / dest_Height) * channel_val_RANGE * 3 / 100;
        send_ppm(0,0,channel_percent(60) + (uint16_t)up_throttle,0,Alt_Hold,0);
    }

    /* arrives the destination height, start position control & hold for x milliseconds. */
    send_ppm(0,0,channel_percent(50),0,Alt_Hold,0);
    alt_ctl_start(dest_Height);
    vTaskDelay(pdMS_TO_TICKS(5000));
    alt_ctl_stop();
    /* drop down & disarm. */
    send_ppm(0,0,channel_percent(38),0,Alt_Hold,0);
    while(current_Height > 0.1);
    position_ctl_stop();
    disarm();
}

static void red_led_warning()
{
    LED2 = LED_OFF;
    vTaskDelay(pdMS_TO_TICKS(2000));
    LED2 = LED_ON;
    vTaskDelay(pdMS_TO_TICKS(10000));
    LED2 = LED_OFF;
}

static void mission_2(const float dest_Height)
{
    float up_throttle;
    uint32_t ret;
    U_PORT_Camera_mode_select(CAM_MODE_BLACK);
    /* arm & clime up. */
    arm(Alt_Hold);
    send_ppm(0,0,channel_percent(20),0,Alt_Hold,0);
    vTaskDelay(pdMS_TO_TICKS(500));
    send_ppm(0,0,channel_percent(50),0,Alt_Hold,0);
    vTaskDelay(pdMS_TO_TICKS(500));
    send_ppm(0,0,channel_percent(55),0,Alt_Hold,0);
    vTaskDelay(pdMS_TO_TICKS(500));
    send_ppm(0,0,channel_percent(60),0,Alt_Hold,0);
    vTaskDelay(pdMS_TO_TICKS(500));

    position_ctl_start(pdFALSE, mission_kp, mission_ki, mission_kd);
    while(current_Height + DEST_HEIGHT_CUSHION < dest_Height) {
        up_throttle = (1.0 - current_Height / dest_Height) * channel_val_RANGE * 3 / 100;
        send_ppm(0,0,channel_percent(60) + (uint16_t)up_throttle,0,Alt_Hold,0);
    }
    send_ppm(0,0,channel_percent(50),0,Alt_Hold,0);
    vTaskDelay(pdMS_TO_TICKS(3000));

    /* go forward to find green car. */
    position_ctl_suspend();
    U_PORT_Camera_mode_select(CAM_MODE_GREEN);
    send_ppm(channel_val_MID,channel_val_MID - 20,0,0,Alt_Hold,0);
    xTaskNotifyStateClear(NULL);
    try_to_find();
    ret = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(3000));

    /* if find green car, locate at it. */
    if (ret > 0) {
        position_ctl_resume();
        send_ppm(0,0,channel_percent(50),0,Alt_Hold,0);
        vTaskDelay(pdMS_TO_TICKS(5000));
        position_ctl_suspend();
    }

    /* back to black hole & drop down & disarm. */
    U_PORT_Camera_mode_select(CAM_MODE_BLACK);
    send_ppm(channel_val_MID,channel_val_MID + 20,0,0,Alt_Hold,0);
    xTaskNotifyStateClear(NULL);
    try_to_find();
    ret = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(3000));
    if (ret > 0) {
        position_ctl_resume();
        vTaskDelay(pdMS_TO_TICKS(2000));
    } else {
        send_ppm(channel_val_MID,channel_val_MID,0,0,Alt_Hold,0);
    }
    send_ppm(0,0,channel_percent(38),0,Alt_Hold,0);
    while(current_Height > 0.1);
    position_ctl_stop();
    disarm();
}
