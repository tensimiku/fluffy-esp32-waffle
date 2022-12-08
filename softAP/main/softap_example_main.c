/*  WiFi softAP Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_mac.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "driver/gpio.h"
#include "driver/i2c.h"
#include "driver/spi_master.h"
#include "driver/uart.h"
#include "u8g2.h"
#include "u8g2_esp32_hal.h"

#include "lwip/err.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"
#include "lwip/api.h"

// network sockets & protocols
#include "lwip/apps/sntp.h"
#include "lwip/sockets.h"
#include "lwip/sockets.h"

/* The examples use WiFi configuration that you can set via project configuration menu.

   If you'd rather not, just change the below entries to strings with
   the config you want - ie #define EXAMPLE_WIFI_SSID "mywifissid"
*/
#define EXAMPLE_ESP_WIFI_SSID      CONFIG_ESP_WIFI_SSID
#define EXAMPLE_ESP_WIFI_PASS      CONFIG_ESP_WIFI_PASSWORD
#define EXAMPLE_ESP_WIFI_CHANNEL   CONFIG_ESP_WIFI_CHANNEL
#define EXAMPLE_MAX_STA_CONN       CONFIG_ESP_MAX_STA_CONN

#define LED_GPIO 2
#define OLED_SDA 4
#define OLED_SCL 15 
#define OLED_RST 16

// network clients
#define CONFIG_ESP32_IPV4
#define HOST_IP_ADDR "192.168.4.255"
#define PORT 54321
static char payload[64];

static const char *TAG = "wifi softAP";
static char current_mac_addr[19];

// display
static u8g2_t u8g2;  // a structure which will contain all the data for one display


void init_SSD1306(){
    u8g2_esp32_hal_t u8g2_esp32_hal = U8G2_ESP32_HAL_DEFAULT;
    //   u8g2_esp32_hal.clk = PIN_CLK;
    //   u8g2_esp32_hal.mosi = PIN_MOSI;
    //   u8g2_esp32_hal.cs = PIN_CS;
    //   u8g2_esp32_hal.dc = PIN_DC;
    u8g2_esp32_hal.sda = OLED_SDA;  // data for I²C
    u8g2_esp32_hal.scl = OLED_SCL;  // clock for I²C
    u8g2_esp32_hal.reset = OLED_RST;
    u8g2_esp32_hal_init(u8g2_esp32_hal);
    u8g2_Setup_ssd1306_i2c_128x64_noname_f(
      &u8g2, U8G2_R0, u8g2_esp32_i2c_byte_cb,
      u8g2_esp32_gpio_and_delay_cb);  // init u8g2 structure
    u8x8_SetI2CAddress(&u8g2.u8x8, 0x78);
    // send init sequence to the display, display is in
    // sleep mode after this,
    u8g2_InitDisplay(&u8g2);
    u8g2_SetPowerSave(&u8g2, 0);  // wake up display
    u8g2_ClearBuffer(&u8g2);
    u8g2_SetFont(&u8g2, u8g2_font_bitcasual_tf );
    u8g2_DrawStr(&u8g2, 0, 20, "I don't know");
    u8g2_DrawStr(&u8g2, 0, 40, "where i am going");
    u8g2_SendBuffer(&u8g2);
    // u8g2_DrawBox(&u8g2, 10, 20, 20, 30);
}

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                    int32_t event_id, void* event_data)
{
    if (event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
        ESP_LOGI(TAG, "station "MACSTR" join, AID=%d",
                 MAC2STR(event->mac), event->aid);
    } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
        ESP_LOGI(TAG, "station "MACSTR" leave, AID=%d",
                 MAC2STR(event->mac), event->aid);
    }
}

void wifi_init_softap(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = EXAMPLE_ESP_WIFI_SSID,
            .ssid_len = strlen(EXAMPLE_ESP_WIFI_SSID),
            .channel = EXAMPLE_ESP_WIFI_CHANNEL,
            .password = EXAMPLE_ESP_WIFI_PASS,
            .max_connection = EXAMPLE_MAX_STA_CONN,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK,
            .pmf_cfg = {
                    .required = false,
            },
        },
    };
    if (strlen(EXAMPLE_ESP_WIFI_PASS) == 0) {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "wifi_init_softap finished. SSID:%s password:%s channel:%d",
             EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS, EXAMPLE_ESP_WIFI_CHANNEL);
}

void get_esp32_mac_addr(){
    uint8_t macaddr[6];
    if (ESP_OK  == esp_read_mac(macaddr, ESP_MAC_WIFI_SOFTAP)){
        sprintf(current_mac_addr, "%02X:%02X:%02X:%02X:%02X:%02X", 
            macaddr[0], 
            macaddr[1], 
            macaddr[2], 
            macaddr[3], 
            macaddr[4], 
            macaddr[5]
    );
    } else{
        sprintf(current_mac_addr, "err: esp_read_mac.");
    }
}

void display_task(void* args) {
    char buf[32];
    struct timeval current_time;
    wifi_sta_list_t current_sta_lists;
    while(1){
        esp_wifi_ap_get_sta_list(&current_sta_lists);
        gettimeofday(&current_time, NULL);
        u8g2_ClearBuffer(&u8g2);
        u8g2_DrawStr(&u8g2, 0, 10, current_mac_addr);
        u8g2_DrawStr(&u8g2, 0, 20, "time:");
        sprintf(buf, "%lld. %ld", current_time.tv_sec, current_time.tv_usec);
        u8g2_DrawStr(&u8g2, 0, 30, buf);
        sprintf(buf, "Clients: %d", current_sta_lists.num);
        u8g2_DrawStr(&u8g2, 0, 50, buf);
        u8g2_SendBuffer(&u8g2);
        vTaskDelay(1000/portTICK_PERIOD_MS); // 1 sec
    }
    vTaskDelete(NULL);
}


static void udp_client_task(void *pvParameters)
{
    char rx_buffer[128];
    char host_ip[] = HOST_IP_ADDR;
    int addr_family = 0;
    int ip_protocol = 0;

    while (1) {

#if defined(CONFIG_ESP32_IPV4)
        struct sockaddr_in dest_addr;
        dest_addr.sin_addr.s_addr = inet_addr(HOST_IP_ADDR);
        dest_addr.sin_family = AF_INET;
        dest_addr.sin_port = htons(PORT);
        addr_family = AF_INET;
        ip_protocol = IPPROTO_IP;
#elif defined(CONFIG_ESP32_IPV6)
        struct sockaddr_in6 dest_addr = { 0 };
        inet6_aton(HOST_IP_ADDR, &dest_addr.sin6_addr);
        dest_addr.sin6_family = AF_INET6;
        dest_addr.sin6_port = htons(PORT);
        dest_addr.sin6_scope_id = esp_netif_get_netif_impl_index(EXAMPLE_INTERFACE);
        addr_family = AF_INET6;
        ip_protocol = IPPROTO_IPV6;
#elif defined(CONFIG_EXAMPLE_SOCKET_IP_INPUT_STDIN)
        struct sockaddr_storage dest_addr = { 0 };
        ESP_ERROR_CHECK(get_addr_from_stdin(PORT, SOCK_DGRAM, &ip_protocol, &addr_family, &dest_addr));
#endif

        int sock = socket(addr_family, SOCK_DGRAM, ip_protocol);
        if (sock < 0) {
            ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
            break;
        }

        // Set timeout
        struct timeval timeout;
        timeout.tv_sec = 10;
        timeout.tv_usec = 0;
        setsockopt (sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout);

        ESP_LOGI(TAG, "Socket created, sending to %s:%d", HOST_IP_ADDR, PORT);
        struct timeval current_time;
        while (1) {
            gettimeofday(&current_time, NULL);
            sprintf(payload, "sv time: %lld. %ld", current_time.tv_sec, current_time.tv_usec);
            int err = sendto(sock, payload, strlen(payload), 0, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
            if (err < 0) {
                ESP_LOGE(TAG, "Error occurred during sending: errno %d", errno);
                break;
            }
            ESP_LOGI(TAG, "Message sent");

            struct sockaddr_storage source_addr; // Large enough for both IPv4 or IPv6
            socklen_t socklen = sizeof(source_addr);
            int len = recvfrom(sock, rx_buffer, sizeof(rx_buffer) - 1, 0, (struct sockaddr *)&source_addr, &socklen);

            // Error occurred during receiving
            if (len < 0) {
                ESP_LOGE(TAG, "recvfrom failed: errno %d", errno);
                break;
            }
            // Data received
            else {
                rx_buffer[len] = 0; // Null-terminate whatever we received and treat like a string
                ESP_LOGI(TAG, "Received %d bytes from %s:", len, host_ip);
                ESP_LOGI(TAG, "%s", rx_buffer);
                if (strncmp(rx_buffer, "OK: ", 4) == 0) {
                    ESP_LOGI(TAG, "Received expected message, reconnecting");
                    break;
                }
            }

            vTaskDelay(5000 / portTICK_PERIOD_MS);
        }

        if (sock != -1) {
            ESP_LOGE(TAG, "Shutting down socket and restarting...");
            shutdown(sock, 0);
            close(sock);
        }
    }
    vTaskDelete(NULL);
}


void app_main(void)
{
    // initialize oled
    init_SSD1306();
    get_esp32_mac_addr();
    // xTaskCreatePinnedToCore(display_task, "disp", NULL, 0, NULL, 0 ); // pin to core 0(net)? or 1?
    xTaskCreate(display_task, "disp", 2048, NULL, 1, NULL);
    //Initialize NVS

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "ESP_WIFI_MODE_AP");
    wifi_init_softap();

    // init udp client
    xTaskCreate(udp_client_task, "udp_client", 4096, NULL, 5, NULL);
}
