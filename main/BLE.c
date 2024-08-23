/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/



/****************************************************************************
*
* This demo showcases BLE GATT client. It can scan BLE devices and connect to one device.
* Run the gatt_server demo, the client demo will automatically connect to the gatt_server demo.
* Client demo will enable gatt_server's notify after connection. The two devices will then exchange
* data.
*
****************************************************************************/

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>
#include <math.h>

#include "esp_bt.h"
#include "esp_bt_defs.h"
#include "esp_gap_ble_api.h"
#include "esp_gattc_api.h"
#include "esp_gatt_defs.h"
#include "esp_bt_main.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/idf_additions.h"
#include "freertos/timers.h"
#include "mqtt.h"
#include "portmacro.h"
#include "screen.h"

#define GATTC_TAG "BLE"

#define AT_2
#ifdef INKBIRD
        #define REMOTE_SERVICE_UUID         0xFF00

        #define REMOTE_DATA_CHAR_UUID       0xFF01  //temperature char
        #define REMOTE_AUTH_CHAR_UUID       0xFF02  //write auth char
        #define REMOTE_AUTH_RESP_UUID       0xFF03  //auth response char
#else
    #ifdef AT_2
        #define REMOTE_SERVICE_UUID         0xCEE0

        #define REMOTE_DATA_CHAR_UUID       0xFF01  //temperature char
        #define REMOTE_AUTH_CHAR_UUID       0xCEE1  //write auth char
        #define REMOTE_AUTH_RESP_UUID       0xCEE2  //auth response char
    #endif
#endif

#define PROFILE_NUM                 1
#define PROFILE_A_APP_ID            0
#define INVALID_HANDLE              0


//#define configUSE_TRACE_FACILITY 1
//#define configUSE_STATS_FORMATTING_FUNCTIONS 1

const char* ambient_temperature_topic = "flame_thrower/ambient_t";
const char* temperature_topic         = "flame_thrower/probe%d";

#ifdef INKBIRD
        static const char remote_device_name[] = "Inkbird@IBBQ-4BW";
#else
    #ifdef AT_2
        static const char remote_device_name[] = "AT-02";
    #endif
#endif

static bool connect    = false;
static bool get_server = false;
static esp_gattc_char_elem_t  *char_elem_result   = NULL;
//static esp_gattc_descr_elem_t *descr_elem_result = NULL;

/* Declare static functions */
static void esp_gap_cb(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param);
static void esp_gattc_cb(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if, esp_ble_gattc_cb_param_t *param);
static void gattc_profile_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if, esp_ble_gattc_cb_param_t *param);

static TimerHandle_t ble_timer;
static int ble_timer_id=1;//blink timer = 0
static int ble_interval=15000;

static esp_bt_uuid_t remote_filter_service_uuid = {
    .len = ESP_UUID_LEN_16,
    .uuid = {.uuid16 = REMOTE_SERVICE_UUID,},
};

static esp_bt_uuid_t auth_char_uuid = {
    .len = ESP_UUID_LEN_16,
    .uuid = {.uuid16 = REMOTE_AUTH_CHAR_UUID,},
};

static esp_bt_uuid_t data_char_uuid = {
    .len = ESP_UUID_LEN_16,
    .uuid = {.uuid16 = REMOTE_DATA_CHAR_UUID,},
};


static esp_bt_uuid_t notify_descr_uuid = {
    .len = ESP_UUID_LEN_16,
    .uuid = {.uuid16 = ESP_GATT_UUID_CHAR_CLIENT_CONFIG,},
};

static esp_ble_scan_params_t ble_scan_params = {
    .scan_type              = BLE_SCAN_TYPE_ACTIVE,
    .own_addr_type          = BLE_ADDR_TYPE_PUBLIC,
    .scan_filter_policy     = BLE_SCAN_FILTER_ALLOW_ALL,
    .scan_interval          = 0x50,
    .scan_window            = 0x30,
    .scan_duplicate         = BLE_SCAN_DUPLICATE_DISABLE
};


struct gattc_profile_inst {
    esp_gattc_cb_t gattc_cb;
    uint16_t gattc_if;
    uint16_t app_id;
    uint16_t conn_id;
    uint16_t service_start_handle;
    uint16_t service_end_handle;
    uint16_t char_handle;
    esp_bd_addr_t remote_bda;
};

/* One gatt-based profile one app_id and one gattc_if, this array will store the gattc_if returned by ESP_GATTS_REG_EVT */
static struct gattc_profile_inst gl_profile_tab[PROFILE_NUM] = {
    [PROFILE_A_APP_ID] = {
        .gattc_cb = gattc_profile_event_handler,
        .gattc_if = ESP_GATT_IF_NONE,       /* Not get the gatt_if, so initial is ESP_GATT_IF_NONE */
    },
};

#ifdef AT_2
//Get checksum byte for AT-2
uint8_t getCRC(char *bytes, int len)
{
    uint8_t res =0;
    for (int i=0;i<len;i++){
        res=res^bytes[i];
    }
    return res;
}
#endif

#ifdef INKBIRD
//Decode the handshake code
void getCodeBin(uint8_t* in, uint8_t* out, uint8_t in_len, uint8_t *out_len){
        double d;
        double d2;
        
		if(in_len!=7){*out_len=0;return;}

		uint16_t intValue = (uint16_t)in[1] | ((uint16_t)in[2] << 8);
        
        if (intValue % 2 == 1) {
            d2 = sqrt((double) intValue);
            d = 1.0;
        } else {
            d2 = sqrt((double) intValue);
            d = 3.0;
        }
        uint16_t i = (int) (d2 - d);
        
        uint16_t intValue2 = (uint16_t)in[3] | ((uint16_t)in[4] << 8);
        
		if(i==0){*out_len=0;return;}
        uint16_t i2 = intValue2 % i;
        uint16_t sqrt2 = (uint16_t) sqrt((double) intValue2);
        uint16_t pow2 = (uint16_t) pow((double) (intValue % 15), 2.0);
        
        
        uint16_t intValue3 = (uint16_t)in[5] | ((uint16_t)in[6] << 8);
        
        uint16_t intValue4 = (uint16_t)in[6];
        
        uint16_t val3 = (intValue3%255+i2+sqrt2+intValue4)/4;
        
        uint8_t ret_bytes[] = {0xfc, 
                            i, 
                            i2, 
                            (i2 % 16) + (i % 238),
                            sqrt2,
                            pow2,
                            val3};
		
        
        ESP_LOGI("BIN_CALC", "%X|%X|%X|%X|%X|%X|%X", 
								    ret_bytes[0], ret_bytes[1],
                                    ret_bytes[2], ret_bytes[3],
                                    ret_bytes[4], ret_bytes[5],ret_bytes[6]);

		memcpy(out, ret_bytes, in_len);
		*out_len=7;
}
#endif


/* this part is used for delayed BLE write function */

typedef struct {
    uint16_t delay;
    esp_gatt_if_t gattc_if;
    uint16_t conn_id;
    uint16_t handle; 
    uint16_t value_len; 
    uint8_t value[10];
    esp_gatt_write_type_t write_type;
    esp_gatt_auth_req_t auth_req;
} delayed_write;

void ble_delayed_write(void* params){
    delayed_write* param = (delayed_write*)params;
    vTaskDelay(param->delay/portTICK_PERIOD_MS);

    if(esp_ble_gattc_write_char(param->gattc_if, 
                                param->conn_id, 
                                param->handle, 
                                param->value_len, 
                                param->value, 
                                param->write_type, 
                                param->auth_req)==ESP_OK)
    {
        ESP_LOGI(GATTC_TAG, "Write auth successful");
    }
    else{ESP_LOGE(GATTC_TAG, "Write auth error");}
    
    UBaseType_t high_water_mark = uxTaskGetStackHighWaterMark(NULL);
    ESP_LOGW("Write task", "Task stack high water mark: %u\n", high_water_mark);
    
    free(params);
    vTaskDelete(NULL);
}


/*this part is to save and navigate through characteristics*/

typedef struct {
    uint16_t char_handle;
    esp_bt_uuid_t char_uuid;
    esp_gatt_char_prop_t properties;
} char_handle_uuid_t;

#define MAX_CHARACTERISTICS 20
char_handle_uuid_t handle_uuid_map[MAX_CHARACTERISTICS];
uint16_t handle_uuid_map_count = 0;

void store_handle_uuid_mapping(uint16_t handle, esp_bt_uuid_t *uuid, esp_gatt_char_prop_t properties) {
    if (handle_uuid_map_count < MAX_CHARACTERISTICS) {
        handle_uuid_map[handle_uuid_map_count].char_handle = handle;
        memcpy(&handle_uuid_map[handle_uuid_map_count].char_uuid, uuid, sizeof(esp_bt_uuid_t));
        handle_uuid_map[handle_uuid_map_count].properties = properties;
        //handle_uuid_map[handle_uuid_map_count].char_uuid = uuid;
        handle_uuid_map_count++;
    } else {
        ESP_LOGE(GATTC_TAG, "Handle-UUID map is full");
    }
}

esp_bt_uuid_t get_uuid_from_handle(uint16_t handle) {
    for (int i = 0; i < handle_uuid_map_count; i++) {
        if (handle_uuid_map[i].char_handle == handle) {
            return handle_uuid_map[i].char_uuid;
        }
    }
    // Return an empty UUID if not found
    esp_bt_uuid_t empty_uuid = {.len = 0};
    return empty_uuid;
}

uint16_t get_handle_from_uuid(uint16_t uuid) {
    for (int i = 0; i < handle_uuid_map_count; i++) {
            if((handle_uuid_map[i].char_uuid.len==ESP_UUID_LEN_16)&&(handle_uuid_map[i].char_uuid.uuid.uuid16 == uuid)){
                return handle_uuid_map[i].char_handle;;
            }
    }
  // Return zero if not found
  return 0;
}

//get handle_map_uuid id from UUID
int8_t get_num_from_uuid(uint16_t uuid) {
    for (int i = 0; i < handle_uuid_map_count; i++) {
            if((handle_uuid_map[i].char_uuid.len==ESP_UUID_LEN_16)&&(handle_uuid_map[i].char_uuid.uuid.uuid16 == uuid)){
                return i;
            }
    }
  // Return zero if not found
  return -1;
}

/*---------------------------------------------
END
-----------------------------------------------*/




uint16_t littleEndianInt(uint8_t *pData)
{
//  ESP_LOGI(GATTC_TAG, "littleEndianInt");
  uint16_t val = pData[1] << 8;
  val = val | pData[0];
  return val;
}

uint64_t last_sent=0;

void ble_data_to_temp(uint8_t* pData, uint16_t sz) {
  //wait until probe is released
  if(esp_timer_get_time()-last_sent<5000*1000)return;
  
  int NUMBER_OF_PROBES=4;
  
  for (int i = 0; i < NUMBER_OF_PROBES*2; i += 2)
  {
    uint16_t val = littleEndianInt(&pData[i]);
    float probe = ((float)val / 10 - 32)*5/9;

    char topic[100]="";
    sprintf(topic, temperature_topic, i/2+1);
    char temp[10]="unknown";
    if((probe>0)&&(probe<350)){
     sprintf(temp,"%.2f", probe);
    }
    
    
    mqtt_publish(topic, temp, 0);
    
    
    ESP_LOGI(GATTC_TAG, "[%d]Probe[%d]: %f", xPortGetCoreID(), i/2+1, probe);
  }
  
  last_sent = esp_timer_get_time();
}


void vTimerCallback_ble( TimerHandle_t pxTimer ){
    if((!connect)&&(!get_server)){
        ESP_LOGI(GATTC_TAG, "Core:%d, Free mem:%ld", (int)xPortGetCoreID(), esp_get_free_heap_size());
        uint32_t duration = 5;
        esp_ble_gap_start_scanning(duration);
    }
}

void ble_data_to_temp_at_2(uint8_t* pData, uint16_t sz) {
    //at least update every 5 seconds
    if(esp_timer_get_time()-last_sent<5000*1000)return;

    //code from AT-2 APK application
    if ((pData[0] & 255) == 85 && (pData[1] & 255) == 170 && (pData[4] & 255) == 161) {
                
        for (int i6 = 5; i6 <= 17; i6 += 2) {
            int i7 = ((i6 - 5) / 2) + 1;
            int probe_id = i7;
            bool ambient = i7==7;
            if((pData[i6]==0xff)&&(pData[i6+1]==0xff)){
                //ESP_LOGI(GATTC_TAG, "probe id:%d, amb:%d, temp:n/a", probe_id, ambient);
                scr_set_t(probe_id, 0);
            }
            else{
                uint16_t temperature = (pData[i6] << 8) | pData[i6+1];
                ESP_LOGI(GATTC_TAG, "probe id:%d, amb:%d, temp:%d", probe_id, ambient, temperature);
            
                float probe = ((float)temperature / 10);

                char topic[100]="";
                if(ambient){
                    sprintf(topic, "%s", ambient_temperature_topic);
                    scr_set_pit_t(probe);
                }
                else{
                    sprintf(topic, temperature_topic, (i6-5)/2+1);
                    scr_set_t(probe_id, probe);
                }
                
                char temp[10]="unknown";
                if((probe>0)&&(probe<350)){
                sprintf(temp,"%.2f", probe);
                }
                else{
                    sprintf(temp,"unknown");
                    scr_set_t(probe_id, 0);
                }
                
                
                mqtt_publish(topic, temp, 0);
                
                
                ESP_LOGI(GATTC_TAG, "[%d]Probe[%d]: %f", xPortGetCoreID(), (i6-5)/2+1, probe);
            }
        }
    }
    
    //no use for battery / c/f data for now
    /*if ((pData[0] & 255) == 85 && (pData[1] & 255) == 170 && (pData[4] & 255) == 160) {
        int battery = pData[5] & 255;
        int c_or_f = pData[8] & 255;
        ESP_LOGI(GATTC_TAG, "Battery: %d, C/F:%d", battery, c_or_f);
    }*/

}



static void gattc_profile_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if, esp_ble_gattc_cb_param_t *param)
{
    esp_ble_gattc_cb_param_t *p_data = (esp_ble_gattc_cb_param_t *)param;

    switch (event) {
    case ESP_GATTC_REG_EVT:
        ESP_LOGI(GATTC_TAG, "REG_EVT");
        esp_err_t scan_ret = esp_ble_gap_set_scan_params(&ble_scan_params);
        if (scan_ret){
            ESP_LOGE(GATTC_TAG, "set scan params error, error code = %x", scan_ret);
        }
        break;
    case ESP_GATTC_CONNECT_EVT:{
        ESP_LOGI(GATTC_TAG, "ESP_GATTC_CONNECT_EVT conn_id %d, if %d", p_data->connect.conn_id, gattc_if);
        gl_profile_tab[PROFILE_A_APP_ID].conn_id = p_data->connect.conn_id;
        memcpy(gl_profile_tab[PROFILE_A_APP_ID].remote_bda, p_data->connect.remote_bda, sizeof(esp_bd_addr_t));
        ESP_LOGI(GATTC_TAG, "REMOTE BDA:");
        esp_log_buffer_hex(GATTC_TAG, gl_profile_tab[PROFILE_A_APP_ID].remote_bda, sizeof(esp_bd_addr_t));
        
        scr_set_bt_icon(true);
        
        break;
    }
    case ESP_GATTC_OPEN_EVT:
        //stopping timer not to start scanning
        xTimerStop(ble_timer,0);
        if (param->open.status != ESP_GATT_OK){
            xTimerStart(ble_timer,0);
            ESP_LOGE(GATTC_TAG, "open failed, status %d", p_data->open.status);
            break;
        }
        ESP_LOGI(GATTC_TAG, "open success");
        break;
    case ESP_GATTC_DIS_SRVC_CMPL_EVT:
        if (param->dis_srvc_cmpl.status != ESP_GATT_OK){
            ESP_LOGE(GATTC_TAG, "discover service failed, status %d", param->dis_srvc_cmpl.status);
            break;
        }
        ESP_LOGI(GATTC_TAG, "discover service complete conn_id %d", param->dis_srvc_cmpl.conn_id);
        //adjustment
        esp_ble_gattc_search_service(gattc_if, param->cfg_mtu.conn_id, &remote_filter_service_uuid);
        break;
    case ESP_GATTC_SEARCH_RES_EVT: {
        ESP_LOGI(GATTC_TAG, "SEARCH RES: conn_id = %x is primary service %d", p_data->search_res.conn_id, p_data->search_res.is_primary);
        ESP_LOGI(GATTC_TAG, "start handle %d end handle %d current handle value %d", p_data->search_res.start_handle, p_data->search_res.end_handle, p_data->search_res.srvc_id.inst_id);
        
        //log all services
        ESP_LOGI(GATTC_TAG, "len: %d",p_data->search_res.srvc_id.uuid.len);
        if(p_data->search_res.srvc_id.uuid.len==2){
            ESP_LOGI(GATTC_TAG, "len: %04X",p_data->search_res.srvc_id.uuid.uuid.uuid16);

            //get characteristics of only REMOTE_SERVICE_UUID
            if(p_data->search_res.srvc_id.uuid.uuid.uuid16==REMOTE_SERVICE_UUID){
                get_server = true;
                gl_profile_tab[PROFILE_A_APP_ID].service_start_handle = p_data->search_res.start_handle;
                gl_profile_tab[PROFILE_A_APP_ID].service_end_handle = p_data->search_res.end_handle;
            }
        }

        break;
    }
//No characteristics reading
/*
    case ESP_GATTC_READ_CHAR_EVT:
        //gattc_read_char_evt_param * evt_param = (gattc_read_char_evt_param *)param;
        ESP_LOGI(GATTC_TAG, "Characteristic read. Status: %d, connID: %d, Handle: %04X,  len:%d", p_data->read.status, 
                                                                                                  p_data->read.conn_id, 
                                                                                                  p_data->read.handle,
                                                                                                  p_data->read.value_len);
        
        esp_log_buffer_hex(GATTC_TAG, p_data->read.value, p_data->read.value_len);
        
        
        if(get_uuid_from_handle(p_data->read.handle).uuid.uuid16==REMOTE_DATA_CHAR_UUID){
            ble_data_to_temp(p_data->read.value, p_data->read.value_len);
        }
        //data read closing connection and starting timer and closing the connection
        
        
        esp_ble_gattc_close(p_data->read.handle, p_data->read.conn_id);
        break;
*/


    case ESP_GATTC_SEARCH_CMPL_EVT:
        if (p_data->search_cmpl.status != ESP_GATT_OK){
            ESP_LOGE(GATTC_TAG, "search service failed, error status = %x", p_data->search_cmpl.status);
            break;
        }
        if(p_data->search_cmpl.searched_service_source == ESP_GATT_SERVICE_FROM_REMOTE_DEVICE) {
            ESP_LOGI(GATTC_TAG, "Get service information from remote device");
        } else if (p_data->search_cmpl.searched_service_source == ESP_GATT_SERVICE_FROM_NVS_FLASH) {
            ESP_LOGI(GATTC_TAG, "Get service information from flash");
        } else {
            ESP_LOGI(GATTC_TAG, "unknown service source");
        }
        ESP_LOGI(GATTC_TAG, "ESP_GATTC_SEARCH_CMPL_EVT");
        if (get_server){

            uint16_t count  = MAX_CHARACTERISTICS;
            uint16_t offset = 0;

            esp_gattc_char_elem_t *char_elem_result = (esp_gattc_char_elem_t *)malloc(sizeof(esp_gattc_char_elem_t) * MAX_CHARACTERISTICS);
            if(char_elem_result==NULL){
                free(char_elem_result);
                char_elem_result=NULL;
                ESP_LOGE(GATTC_TAG, "Cannot allocate memory");
                break;
            }
            memset(char_elem_result, 0xff, sizeof(esp_gattc_char_elem_t) * MAX_CHARACTERISTICS);
            esp_gatt_status_t ret_status = esp_ble_gattc_get_all_char(gattc_if,
                                                            gl_profile_tab[PROFILE_A_APP_ID].conn_id,
                                                            gl_profile_tab[PROFILE_A_APP_ID].service_start_handle,
                                                            gl_profile_tab[PROFILE_A_APP_ID].service_end_handle,
                                                            char_elem_result,
                                                            &count,
                                                            offset);
            if (ret_status != ESP_GATT_OK) {
                ESP_LOGE(GATTC_TAG, "esp_ble_gattc_get_all_char error, %d", __LINE__);
                free(char_elem_result);
                char_elem_result = NULL;
                break;
            }
            if (count > 0) {
                    //Store UUIDs/handles
                    for (int i = 0; i < ((count>MAX_CHARACTERISTICS) ? MAX_CHARACTERISTICS : count); i ++) {
                        store_handle_uuid_mapping(char_elem_result[i].char_handle, &char_elem_result[i].uuid, char_elem_result[i].properties);
                    }

                    //Show UUIDs/handles table:
                    for(int i=0; i< handle_uuid_map_count;i++){
                        ESP_LOGI(GATTC_TAG, "UUID: %X, handle: %d, property: %d", handle_uuid_map[i].char_uuid.uuid.uuid16, handle_uuid_map[i].char_handle, handle_uuid_map[i].properties);
                    }


                    int char_index=0;

                    #ifdef INKBIRD
                    //register for notify for data
                    char_index = get_num_from_uuid(REMOTE_DATA_CHAR_UUID);
                    if((char_index!=-1)&& (handle_uuid_map[char_index].properties&ESP_GATT_CHAR_PROP_BIT_NOTIFY)){                        
                        esp_ble_gattc_register_for_notify (gattc_if,
                                                            gl_profile_tab[PROFILE_A_APP_ID].remote_bda,
                                                            handle_uuid_map[char_index].char_handle);
                        ESP_LOGI(GATTC_TAG, "reg for notify for %X", REMOTE_DATA_CHAR_UUID);
                    }
                    #endif


                    //register for notify for auth responses
                    char_index = get_num_from_uuid(REMOTE_AUTH_RESP_UUID);
                    if((char_index!=-1)&& (handle_uuid_map[char_index].properties&ESP_GATT_CHAR_PROP_BIT_NOTIFY)){
                        esp_ble_gattc_register_for_notify (gattc_if,
                                                            gl_profile_tab[PROFILE_A_APP_ID].remote_bda,
                                                            handle_uuid_map[char_index].char_handle);
                        ESP_LOGI(GATTC_TAG, "reg for notify for %X", REMOTE_AUTH_RESP_UUID);
                    }
                    

                    
                    //delay write authorisation 
                    char_index = get_num_from_uuid(REMOTE_AUTH_CHAR_UUID);
                    if((char_index!=-1)&& ((handle_uuid_map[char_index].properties&ESP_GATT_CHAR_PROP_BIT_WRITE)||
                                           (handle_uuid_map[char_index].properties&ESP_GATT_CHAR_PROP_BIT_WRITE_NR))){

                        #ifdef INKBIRD
                            unsigned char auth_value[] = {0x02, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00};
                        #else
                            #ifdef AT_2
                            unsigned char auth_value[] = {0x55, 0xAA, 0x00, 0x03, 0xA0, 0x00, 0x00, 0x5C};
                            #endif
                        #endif

                        delayed_write *write = (delayed_write *)malloc(sizeof(delayed_write));
                        memset(write, 0, sizeof(delayed_write));
                            write->delay=1000;
                            write->gattc_if=gattc_if;
                            write->conn_id=gl_profile_tab[PROFILE_A_APP_ID].conn_id;
                            write->handle=handle_uuid_map[char_index].char_handle;
                            write->value_len=sizeof(auth_value);
                            memcpy(write->value, auth_value, write->value_len);
                            #ifdef INKBIRD
                                write->write_type=ESP_GATT_WRITE_TYPE_RSP;
                            #endif
                            #ifdef AT_2
                                write->write_type=ESP_GATT_WRITE_TYPE_NO_RSP;
                            #endif
                            write->auth_req=ESP_GATT_AUTH_REQ_NONE;

                        TaskHandle_t xHandle = NULL;
                        xTaskCreatePinnedToCore(ble_delayed_write, "delayed_write", 1024*4, (void*)write, tskIDLE_PRIORITY, &xHandle, xPortGetCoreID());

                        #ifdef AT_2
                            char bArr2[]={0x55, 0xAA, 0x00, 0x02, 0xA1, 0x00, 0x5C};
                            delayed_write *write2 = (delayed_write *)malloc(sizeof(delayed_write));
                            memset(write2, 0, sizeof(delayed_write));
                            memcpy(write2, write, sizeof(delayed_write));
                            write2->delay = 1300;
                            write2->value_len=sizeof(bArr2);
                            memcpy(write2->value, bArr2, write2->value_len);
                            TaskHandle_t xHandle2 = NULL;
                            xTaskCreatePinnedToCore(ble_delayed_write, "delayed_write2", 1024*4, (void*)write2, tskIDLE_PRIORITY, &xHandle2, xPortGetCoreID());
                        #endif


                    }
                }
                
                free(char_elem_result);
                char_elem_result = NULL;
        }

        break;
    
    
    case ESP_GATTC_WRITE_CHAR_EVT:
        if (p_data->write.status != ESP_GATT_OK){
            ESP_LOGE(GATTC_TAG, "write char failed, error status = %x", p_data->write.status);
            //esp_gatt_status_t t;

            break;
        }
        ESP_LOGI(GATTC_TAG, "write char success ");
        break;


    case ESP_GATTC_REG_FOR_NOTIFY_EVT:
        ESP_LOGI(GATTC_TAG, "ESP_GATTC_REG_FOR_NOTIFY_EVT, handle: %d", p_data->reg_for_notify.handle);
        if (p_data->reg_for_notify.status != ESP_GATT_OK){
            ESP_LOGE(GATTC_TAG, "REG FOR NOTIFY failed: error status = %d", p_data->reg_for_notify.status);
        }else{
            uint16_t count = 20;
            uint16_t notify_en = 1;
            esp_gattc_descr_elem_t *descr_elem_result = malloc(sizeof(esp_gattc_descr_elem_t) * count);

            if (!descr_elem_result){
                    ESP_LOGE(GATTC_TAG, "malloc error, gattc no mem");
            }else{
                esp_gatt_status_t ret_status = esp_ble_gattc_get_descr_by_char_handle(gattc_if,
                                                                             gl_profile_tab[PROFILE_A_APP_ID].conn_id,
                                                                         p_data->reg_for_notify.handle,
                                                                          notify_descr_uuid,
                                                                              descr_elem_result,
                                                                               &count);
                if (ret_status != ESP_GATT_OK){
                    ESP_LOGE(GATTC_TAG, "esp_ble_gattc_get_descr_by_char_handle error");
                }

                    /* Every char has only one descriptor in our 'ESP_GATTS_DEMO' demo, so we used first 'descr_elem_result' */
                    if (count > 0 && descr_elem_result[0].uuid.len == ESP_UUID_LEN_16 && descr_elem_result[0].uuid.uuid.uuid16 == ESP_GATT_UUID_CHAR_CLIENT_CONFIG){
                        ret_status = esp_ble_gattc_write_char_descr( gattc_if,
                                                                     gl_profile_tab[PROFILE_A_APP_ID].conn_id,
                                                                     descr_elem_result[0].handle,
                                                                     sizeof(notify_en),
                                                                     (uint8_t *)&notify_en,
                                                                     ESP_GATT_WRITE_TYPE_RSP,
                                                                     ESP_GATT_AUTH_REQ_NONE);
                    }

                    if (ret_status != ESP_GATT_OK){
                        ESP_LOGE(GATTC_TAG, "esp_ble_gattc_write_char_descr error");
                    }
                    else{
                        ESP_LOGI(GATTC_TAG, "notify write successful");
                    }
                
                /* free descr_elem_result */
                free(descr_elem_result);
            }
            
            
            

        }
        break;
    case ESP_GATTC_NOTIFY_EVT:
        ESP_LOGI(GATTC_TAG, "ESP_GATTC_NOTIFY_EVT, received %d (1)notify/(0)indicate value:",p_data->notify.is_notify);
        esp_log_buffer_hex(GATTC_TAG, p_data->notify.value, p_data->notify.value_len);
        
        #ifdef INKBIRD
        if(get_uuid_from_handle(p_data->notify.handle).uuid.uuid16==REMOTE_DATA_CHAR_UUID){
            ESP_LOGI(GATTC_TAG, "received temperature data");
            ble_data_to_temp(p_data->notify.value, p_data->notify.value_len);
        }

        else if (get_uuid_from_handle(p_data->notify.handle).uuid.uuid16==REMOTE_AUTH_RESP_UUID){
            ESP_LOGI(GATTC_TAG, "received auth data");
            switch(p_data->notify.value[0]){
                case 0xFB:
                    //Auth code receied in the form of 0xFB 12 34 56 78 99 11
                    uint8_t out[7]="";
                    uint8_t out_len=0;
                    
                    //calculate answer code
                    getCodeBin(p_data->notify.value, &out, p_data->notify.value_len, &out_len);

                    //sending the answer code back
                    if(out_len==7){
                        if(esp_ble_gattc_write_char(gattc_if, p_data->notify.conn_id, get_handle_from_uuid(REMOTE_AUTH_CHAR_UUID), 
                                                out_len, &out, ESP_GATT_WRITE_TYPE_RSP,ESP_GATT_AUTH_REQ_NONE)==ESP_OK){
                            ESP_LOGI(GATTC_TAG, "Sent authorisation code");
                            ESP_LOG_BUFFER_HEX(GATTC_TAG, out, out_len);
                        }
                        else{ESP_LOGE(GATTC_TAG, "Couldn't send authorisation code");}
                    }
                    else{
                        ESP_LOGE(GATTC_TAG, "Generate auth code failed! len=%d\nTrying to send request to login again.", out_len);

                        char data[]={0xfb,0,0,0,0,0,0};
                        if(esp_ble_gattc_write_char(gattc_if, p_data->notify.conn_id, get_handle_from_uuid(REMOTE_AUTH_CHAR_UUID), 
                                                sizeof(data), &data, ESP_GATT_WRITE_TYPE_RSP,ESP_GATT_AUTH_REQ_NONE)==ESP_OK){
                            ESP_LOGI(GATTC_TAG, "Sent request for auth:");
                            ESP_LOG_BUFFER_HEX(GATTC_TAG, out, out_len);
                        }
                    }
                    break;
                case 0xFC:
                    ESP_LOGI(GATTC_TAG, "Authorisation successful");
                    break;
                case 0xFE:
                    //request authorisation 0xFB 00 00 00 00 00 00
                    char req[]={0xfb,0,0,0,0,0,0};
                    if(esp_ble_gattc_write_char(gattc_if, p_data->notify.conn_id, get_handle_from_uuid(REMOTE_AUTH_CHAR_UUID), 
                                                sizeof(req), &req, ESP_GATT_WRITE_TYPE_RSP,ESP_GATT_AUTH_REQ_NONE)==ESP_OK){
                        ESP_LOGI(GATTC_TAG, "Sent request for auth");
                        ESP_LOG_BUFFER_HEX(GATTC_TAG, req, sizeof(req));
                    }
                    else{ESP_LOGE(GATTC_TAG, "Couldn't send request for auth");}
                    break;
            }
        }
        #endif

        #ifdef AT_2
            ble_data_to_temp_at_2(p_data->notify.value, p_data->notify.value_len);
            
        #endif
        
        break;
    case ESP_GATTC_DISCONNECT_EVT:
        connect = false;
        get_server = false;
        scr_set_bt_icon(false);

        //reset handles table
        handle_uuid_map_count=0;
        memset(&handle_uuid_map,0,sizeof(char_handle_uuid_t)*MAX_CHARACTERISTICS);

        ESP_LOGI(GATTC_TAG, "ESP_GATTC_DISCONNECT_EVT, reason = %d", p_data->disconnect.reason);
        
        //Restarting timer on disconnect
        xTimerStart(ble_timer,0);

        break;
    default:
        break;
    }
}




static void esp_gap_cb(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    uint8_t *adv_name = NULL;
    uint8_t adv_name_len = 0;
    //ESP_LOGI(GATTC_TAG, "EVENT: %d", event);
    switch (event) {
    case ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT: {
        //the unit of the duration is second
        
        break;
    }
    case ESP_GAP_BLE_SCAN_START_COMPLETE_EVT:
        //scan start complete event to indicate scan start successfully or failed
        if (param->scan_start_cmpl.status != ESP_BT_STATUS_SUCCESS) {
            ESP_LOGE(GATTC_TAG, "scan start failed, error status = %x", param->scan_start_cmpl.status);
            break;
        }
        ESP_LOGI(GATTC_TAG, "scan start success");
        char payload[10]="";
        sprintf(payload, "%ld", esp_get_free_heap_size());
        mqtt_publish("flame_thrower/mem", payload,0);

        break;
    case ESP_GAP_BLE_SCAN_RESULT_EVT: {
        esp_ble_gap_cb_param_t *scan_result = (esp_ble_gap_cb_param_t *)param;
        switch (scan_result->scan_rst.search_evt) {
        case ESP_GAP_SEARCH_INQ_RES_EVT:
            //remove logging to avoid spamming
            //esp_log_buffer_hex(GATTC_TAG, scan_result->scan_rst.bda, 6);
            //ESP_LOGI(GATTC_TAG, "searched Adv Data Len %d, Scan Response Len %d", scan_result->scan_rst.adv_data_len, scan_result->scan_rst.scan_rsp_len);
            adv_name = esp_ble_resolve_adv_data(scan_result->scan_rst.ble_adv,
                                                ESP_BLE_AD_TYPE_NAME_CMPL, &adv_name_len);
            //ESP_LOGI(GATTC_TAG, "searched Device Name Len %d", adv_name_len);
            //esp_log_buffer_char(GATTC_TAG, adv_name, adv_name_len);


            //ESP_LOGI(GATTC_TAG, "\n");

            if (adv_name != NULL) {
                if (strlen(remote_device_name) == adv_name_len && strncmp((char *)adv_name, remote_device_name, adv_name_len) == 0) {
                    ESP_LOGI(GATTC_TAG, "searched device %s\n", remote_device_name);
                    if (connect == false) {
                        connect = true;
                        ESP_LOGI(GATTC_TAG, "connect to the remote device.");
                        esp_ble_gap_stop_scanning();
                        esp_ble_gattc_open(gl_profile_tab[PROFILE_A_APP_ID].gattc_if, scan_result->scan_rst.bda, scan_result->scan_rst.ble_addr_type, true);
                    }
                }
            }
            break;
        case ESP_GAP_SEARCH_INQ_CMPL_EVT:
            ESP_LOGI(GATTC_TAG, "Scan is finished! Device not found!");
            //uint8_t bda[ESP_BD_ADDR_LEN]={0x00, 0x00, 0x04,0x12,0xff};
            
            //esp_ble_gattc_open(gl_profile_tab[PROFILE_A_APP_ID].gattc_if, bda, BLE_ADDR_TYPE_PUBLIC, true);
            //ESP_LOGI("OPEN", "open2");
            break;
        default:
            break;
        }
        break;
    }

    case ESP_GAP_BLE_SCAN_STOP_COMPLETE_EVT:
        if (param->scan_stop_cmpl.status != ESP_BT_STATUS_SUCCESS){
            ESP_LOGE(GATTC_TAG, "scan stop failed, error status = %x", param->scan_stop_cmpl.status);
            break;
        }
        ESP_LOGI(GATTC_TAG, "stop scan successfully");
        break;

    case ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT:
        if (param->adv_stop_cmpl.status != ESP_BT_STATUS_SUCCESS){
            ESP_LOGE(GATTC_TAG, "adv stop failed, error status = %x", param->adv_stop_cmpl.status);
            break;
        }
        ESP_LOGI(GATTC_TAG, "stop adv successfully");
        break;
    case ESP_GAP_BLE_UPDATE_CONN_PARAMS_EVT:
         ESP_LOGI(GATTC_TAG, "update connection params status = %d, min_int = %d, max_int = %d,conn_int = %d,latency = %d, timeout = %d",
                  param->update_conn_params.status,
                  param->update_conn_params.min_int,
                  param->update_conn_params.max_int,
                  param->update_conn_params.conn_int,
                  param->update_conn_params.latency,
                  param->update_conn_params.timeout);
        break;
    case ESP_GAP_SEARCH_INQ_CMPL_EVT:
        ESP_LOGI(GATTC_TAG, "ESP_GAP_SEARCH_INQ_CMPL_EVT");
        break;
    case ESP_GAP_BLE_SCAN_TIMEOUT_EVT:
        ESP_LOGI(GATTC_TAG, "ESP_GAP_BLE_SCAN_TIMEOUT_EVT");
        break;

    case ESP_GAP_SEARCH_INQ_RES_EVT:
        ESP_LOGI(GATTC_TAG, "ESP_GAP_SEARCH_INQ_RES_EVT");
        break;
    default:
        break;
    }
}

static void esp_gattc_cb(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if, esp_ble_gattc_cb_param_t *param)
{
    /* If event is register event, store the gattc_if for each profile */
    if (event == ESP_GATTC_REG_EVT) {
        if (param->reg.status == ESP_GATT_OK) {
            gl_profile_tab[param->reg.app_id].gattc_if = gattc_if;
        } else {
            ESP_LOGI(GATTC_TAG, "reg app failed, app_id %04x, status %d",
                    param->reg.app_id,
                    param->reg.status);
            return;
        }
    }

    /* If the gattc_if equal to profile A, call profile A cb handler,
     * so here call each profile's callback */
    do {
        int idx;
        for (idx = 0; idx < PROFILE_NUM; idx++) {
            if (gattc_if == ESP_GATT_IF_NONE || /* ESP_GATT_IF_NONE, not specify a certain gatt_if, need to call every profile cb function */
                    gattc_if == gl_profile_tab[idx].gattc_if) {
                if (gl_profile_tab[idx].gattc_cb) {
                    gl_profile_tab[idx].gattc_cb(event, gattc_if, param);
                }
            }
        }
    } while (0);
}

void BL_init(void)
{    
    //Initialize Bluetooth    
    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT));

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    esp_err_t ret = esp_bt_controller_init(&bt_cfg);
    if (ret) {
        ESP_LOGE(GATTC_TAG, "%s initialize controller failed: %s\n", __func__, esp_err_to_name(ret));
        return;
    }

    ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (ret) {
        ESP_LOGE(GATTC_TAG, "%s enable controller failed: %s\n", __func__, esp_err_to_name(ret));
        return;
    }


    esp_bluedroid_config_t cfg = BT_BLUEDROID_INIT_CONFIG_DEFAULT();

    ret = esp_bluedroid_init_with_cfg(&cfg);
    if (ret) {
        ESP_LOGE(GATTC_TAG, "%s init bluetooth failed: %s\n", __func__, esp_err_to_name(ret));
        return;
    }

    ret = esp_bluedroid_enable();
    if (ret) {
        ESP_LOGE(GATTC_TAG, "%s enable bluetooth failed: %s\n", __func__, esp_err_to_name(ret));
        return;
    }

    //register the  callback function to the gap module
    ret = esp_ble_gap_register_callback(esp_gap_cb);
    if (ret){
        ESP_LOGE(GATTC_TAG, "%s gap register failed, error code = %x\n", __func__, ret);
        return;
    }

    //register the callback function to the gattc module
    ret = esp_ble_gattc_register_callback(esp_gattc_cb);
    if(ret){
        ESP_LOGE(GATTC_TAG, "%s gattc register failed, error code = %x\n", __func__, ret);
        return;
    }

    ret = esp_ble_gattc_app_register(PROFILE_A_APP_ID);
    if (ret){
        ESP_LOGE(GATTC_TAG, "%s gattc app register failed, error code = %x\n", __func__, ret);
    }

    ble_timer = xTimerCreate("ble_timer", 
                               ble_interval/portTICK_PERIOD_MS, //start with 1 blink per second
                               pdTRUE, //auto reload
                               ( void * )ble_timer_id, 
                               (TimerCallbackFunction_t)vTimerCallback_ble);
    xTimerStart(ble_timer, 0);


}
