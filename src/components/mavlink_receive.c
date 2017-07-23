/*
 * mavlink_receive.c
 *
 *  Created on: 2017年7月23日
 *      Author: Cotyledon
 */

#include <string.h>
#include "FreeRTOS.h"
#include "task.h"
#include "platform.h"
#include "r_cg_sci.h"

#include "mavlink_receive.h"

enum
{
    MSG_START = 0,
    MSG_LEN = 1,
    MSG_SEQ = 2,
    MSG_ID = 5,
    RANGER_DISTANCE = 6,
    HEIGHT_BUFFER_END = 15,
    ANGLE_BUFFER_END = 35,
    ATTITUDE_current_Roll = 10,
    ATTITUDE_current_Pitch = 14,
    ATTITUDE_current_Yaw = 18,
};

static uint8_t Buffer[40];
static uint8_t Rx_Buffer[40];
static uint8_t rx_buffer[2][RX_BUFFER_LENGTH];
static int Rx_Buffer_point = 0;
static int rx_buffer_point = 0;
static volatile bool start_receive = false;
static volatile bool is_Height = false;
static volatile bool is_Angle = false;

static TaskHandle_t mavlink_taskhandle;

volatile float current_Height;
volatile float current_Roll, current_Pitch, current_Yaw;

static void mavlink_receive_task_entry(void *pvParameters);
static void calculate_angle(void);
static void calculate_height(void);


void mavlink_receive_init(void)
{
    BaseType_t ret;

    ret = xTaskCreate(mavlink_receive_task_entry,
                      "mavlink",
                      configMINIMAL_STACK_SIZE,
                      NULL,
                      MAVLINK_TASK_PRI,
                      &mavlink_taskhandle);
    configASSERT(ret == pdPASS);

    rx_buffer_point = 0;
    R_SCI1_Serial_Receive(rx_buffer[rx_buffer_point], RX_BUFFER_LENGTH);
    R_SCI1_Start();
}

void u_sci1_receiveend_callback(void)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    vTaskNotifyGiveFromISR(mavlink_taskhandle, &xHigherPriorityTaskWoken);
    rx_buffer_point = (rx_buffer_point == 1) ? 0 : 1;
    R_SCI1_Serial_Receive(rx_buffer[rx_buffer_point], RX_BUFFER_LENGTH);
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

/*
 * calculate current_Roll,current_Pitch,current_Yaw
 */
static void calculate_angle(void)
{
    float angle[3];
    memcpy((void*)angle, Buffer + ATTITUDE_current_Roll, sizeof(float) * 3);

    current_Roll  = angle[0] / (2*PI) * 360.0;
    current_Pitch = angle[1] / (2*PI) * 360.0;
    current_Yaw   = angle[2] / (2*PI) * 360.0;
}

/*
 * calculate height
 */
static void calculate_height(void)
{
    memcpy((void*)&current_Height, Buffer + RANGER_DISTANCE, sizeof(float) * 1);
}

static void mavlink_receive_task_entry(void *pvParameters)
{
    //from RXI interrupt; character placed in buffer.
    //
    // Blink the LED to show a character transfer is occuring.
    //
    int i;
    while(1) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        for (i = 0; i < RX_BUFFER_LENGTH; i++) {
            if (rx_buffer[rx_buffer_point][i] == MAVLINK_STX) {
                Rx_Buffer_point = 0;
                start_receive = true;
            }
            if(start_receive == true) {
                LED0 = LED_ON;
                Rx_Buffer[Rx_Buffer_point++] = rx_buffer[rx_buffer_point][i];
                if(Rx_Buffer[MSG_LEN]==MSG_ALTITUDE_LENGTH && Rx_Buffer[MSG_ID]==MSG_ALTITUDE_ID && Rx_Buffer[HEIGHT_BUFFER_END]!=0) {
                    memcpy(Buffer, Rx_Buffer, MSG_HEIGHT_LENGTH);
                    is_Height = true;
                }
                if(Rx_Buffer[MSG_LEN]==MSG_ATTITUDE_LENGTH && Rx_Buffer[MSG_ID]==MSG_ATTITUDE_ID && Rx_Buffer[ANGLE_BUFFER_END]!=0) {
                    memcpy(Buffer, Rx_Buffer, MSG_ANGLE_LENGTH);
                    is_Angle = true;
                }
                if(Rx_Buffer_point==MSG_HEIGHT_LENGTH && is_Height) {
                    Rx_Buffer_point = 0;
                    start_receive = false;
                    calculate_height();
                    is_Height = false;
                    memset(Rx_Buffer, 0, 40);
                    memset(Buffer, 0, 40);
                }
                if(Rx_Buffer_point==MSG_ANGLE_LENGTH && is_Angle) {
                    Rx_Buffer_point = 0;
                    start_receive = false;
                    calculate_angle();
                    is_Angle = false;
                    memset(Rx_Buffer, 0, 40);
                    memset(Buffer, 0, 40);
                }
                if(Rx_Buffer_point==MSG_ANGLE_LENGTH && !is_Height && !is_Angle) {
                    Rx_Buffer_point = 0;
                    memset(Rx_Buffer, 0, 40);
                    memset(Buffer, 0, 40);
                }
                //
                // Turn off the LED
                //
                LED0 = LED_OFF;
            }
        }
    }
}
