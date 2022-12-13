#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
// #include "esp_netif.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_mac.h"
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

// network sockets 
#include "lwip/sockets.h"
#include <lwip/netdb.h>

// timer
#include "driver/gptimer.h"

/* The examples use WiFi configuration that you can set via project configuration menu
   If you'd rather not, just change the below entries to strings with
   the config you want - ie #define EXAMPLE_WIFI_SSID "mywifissid"
*/
#define CONFIG_ESP32_IPV4
#define EXAMPLE_ESP_WIFI_SSID      CONFIG_ESP_WIFI_SSID
#define EXAMPLE_ESP_WIFI_PASS      CONFIG_ESP_WIFI_PASSWORD
#define EXAMPLE_ESP_MAXIMUM_RETRY  CONFIG_ESP_MAXIMUM_RETRY

#if CONFIG_ESP_WIFI_AUTH_OPEN
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_OPEN
#elif CONFIG_ESP_WIFI_AUTH_WEP
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WEP
#elif CONFIG_ESP_WIFI_AUTH_WPA_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA2_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA_WPA2_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA_WPA2_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA3_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA3_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA2_WPA3_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_WPA3_PSK
#elif CONFIG_ESP_WIFI_AUTH_WAPI_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WAPI_PSK
#endif

#define LED_GPIO 2
#define OLED_SDA 4
#define OLED_SCL 15 
#define OLED_RST 16

/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_wifi_event_group;

/* The event group allows multiple bits for each event, but we only care about two events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static const char *TAG = "wifi station";
static int s_retry_num = 0;


// UDP IP, port number
#define SYNC_IP_ADDR "192.168.4.1"
#define SYNC_PORT 33333
#define PORT 54321
#define INIT_RTT 99999999
// display
static u8g2_t u8g2;  // a structure which will contain all the data for one display
static char current_ip_addr[17];
static char current_mac_addr[19];
static char received_packet_msg[64];

// timer
static gptimer_handle_t gptimer = NULL;
static QueueHandle_t timer_queue = NULL;




static void event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < EXAMPLE_ESP_MAXIMUM_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "retry to connect to the AP");
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        ESP_LOGI(TAG,"connect to the AP fail");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        sprintf(current_ip_addr, IPSTR, IP2STR(&event->ip_info.ip));
        // ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        ESP_LOGI(TAG, "got ip: %s", current_ip_addr);
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}


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

void configure_led(void)
{
    gpio_reset_pin(LED_GPIO);
    /* Set the GPIO as a push/pull output */
    gpio_set_direction(LED_GPIO, GPIO_MODE_OUTPUT);
}

void create_timer(){
    ESP_LOGI(TAG, "Create timer handle");
    gptimer_config_t timer_config = {
        .clk_src = GPTIMER_CLK_SRC_DEFAULT,
        .direction = GPTIMER_COUNT_UP,
        .resolution_hz = 1000000, // 1MHz, 1 tick=1us
    };
    ESP_ERROR_CHECK(gptimer_new_timer(&timer_config, &gptimer));
    ESP_LOGI(TAG, "Enable timer");
    ESP_ERROR_CHECK(gptimer_enable(gptimer));
    ESP_ERROR_CHECK(gptimer_start(gptimer));
    timer_queue = xQueueCreate(10, sizeof(bool));
}

static bool IRAM_ATTR timer_on_alarm_cb(gptimer_handle_t timer, const gptimer_alarm_event_data_t *edata, void *user_data)
{
    BaseType_t high_task_awoken = pdFALSE;
    QueueHandle_t queue = (QueueHandle_t)user_data;
    bool evt = (edata->count_value / 1000000) % 2 == 0;
    xQueueSendFromISR(queue, &evt, &high_task_awoken);
    // Retrieve count value and send to queue
    // example_queue_element_t ele = {
    //     .event_count = edata->count_value
    // };
    // xQueueSendFromISR(queue, &ele, &high_task_awoken);
    // return whether we need to yield at the end of ISR
    gptimer_alarm_config_t alarm_config = {
        .alarm_count = edata->alarm_value + 1000000, // alarm in next 1s
    };
    gptimer_set_alarm_action(timer, &alarm_config);
    return (high_task_awoken == pdTRUE);
}

void set_led_alarm(){
    ESP_ERROR_CHECK(gptimer_disable(gptimer));
     gptimer_event_callbacks_t cbs = {
        .on_alarm = timer_on_alarm_cb,
    };
    ESP_ERROR_CHECK(gptimer_register_event_callbacks(gptimer, &cbs, timer_queue));
    gptimer_alarm_config_t alarm_config = {
        .alarm_count = 1000000, // period = 1s
    };
    ESP_ERROR_CHECK(gptimer_set_alarm_action(gptimer, &alarm_config));
    ESP_ERROR_CHECK(gptimer_enable(gptimer));
}


static void alarm_led_task(void* arg)
{
    QueueHandle_t queue = (QueueHandle_t)arg;
    bool evt;
    for(;;) {
        if(xQueueReceive(queue, &evt, portMAX_DELAY)) {
            if(evt){
                gpio_set_level(LED_GPIO, 1);
            }
            else{
                gpio_set_level(LED_GPIO, 0);
            }
        }
    }
    vTaskDelete(NULL);
}

void get_esp32_mac_addr(){
    uint8_t macaddr[6];
    if (ESP_OK  == esp_read_mac(macaddr, ESP_MAC_WIFI_STA)){
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

void wifi_init_sta(void)
{
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());

    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = EXAMPLE_ESP_WIFI_SSID,
            .password = EXAMPLE_ESP_WIFI_PASS,
            /* Authmode threshold resets to WPA2 as default if password matches WPA2 standards (pasword len => 8).
             * If you want to connect the device to deprecated WEP/WPA networks, Please set the threshold value
             * to WIFI_AUTH_WEP/WIFI_AUTH_WPA_PSK and set the password with length and format matching to
	     * WIFI_AUTH_WEP/WIFI_AUTH_WPA_PSK standards.
             */
            .threshold.authmode = ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD,
            .sae_pwe_h2e = WPA3_SAE_PWE_BOTH,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );
    ESP_ERROR_CHECK(esp_wifi_start() );

    ESP_LOGI(TAG, "wifi_init_sta finished.");

    /* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
     * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above) */
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE,
            pdFALSE,
            portMAX_DELAY);

    /* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
     * happened. */
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "connected to ap SSID:%s password:%s",
                 EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGI(TAG, "Failed to connect to SSID:%s, password:%s",
                 EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);
    } else {
        ESP_LOGE(TAG, "UNEXPECTED EVENT");
    }
}


void display_task(void* args) {
    char buf[32];
    struct timeval current_time;
    uint64_t timer_count;
    while(1){
        gettimeofday(&current_time, NULL);
        u8g2_ClearBuffer(&u8g2);
        u8g2_DrawStr(&u8g2, 0, 10, current_mac_addr);
        u8g2_DrawStr(&u8g2, 0, 20, "time:");
        sprintf(buf, "%lld. %ld", current_time.tv_sec, current_time.tv_usec);
        u8g2_DrawStr(&u8g2, 0, 30, buf);
        u8g2_DrawStr(&u8g2, 0, 40, current_ip_addr);
        u8g2_DrawStr(&u8g2, 0, 50, received_packet_msg);
        ESP_ERROR_CHECK(gptimer_get_raw_count(gptimer, &timer_count));
        sprintf(buf, "timer: %lld", timer_count);
        u8g2_DrawStr(&u8g2, 0, 60, buf);
        u8g2_SendBuffer(&u8g2);
        vTaskDelay(1000/portTICK_PERIOD_MS); // 1 sec
    }
    vTaskDelete(NULL);
}

void sync_timer(int sync_count)
{
    char rx_buffer[128];
    char payload[16];
    char host_ip[] = SYNC_IP_ADDR;
    int addr_family = 0;
    int ip_protocol = 0;
    int retry = 4;
    uint64_t timer_count;
    uint64_t received_count;

    vTaskDelay(1000 / portTICK_PERIOD_MS);
    ESP_LOGI(TAG, "Start sync");

    while (retry-- > 0) {
#if defined(CONFIG_ESP32_IPV4)
        struct sockaddr_in dest_addr;
        dest_addr.sin_addr.s_addr = inet_addr(SYNC_IP_ADDR);
        dest_addr.sin_family = AF_INET;
        dest_addr.sin_port = htons(SYNC_PORT);
        addr_family = AF_INET;
        ip_protocol = IPPROTO_IP;
#elif defined(CONFIG_ESP32_IPV6)
        struct sockaddr_in6 dest_addr = { 0 };
        inet6_aton(SYNC_IP_ADDR, &dest_addr.sin6_addr);
        dest_addr.sin6_family = AF_INET6;
        dest_addr.sin6_port = htons(SYNC_PORT);
        dest_addr.sin6_scope_id = esp_netif_get_netif_impl_index(EXAMPLE_INTERFACE);
        addr_family = AF_INET6;
        ip_protocol = IPPROTO_IPV6;
#elif defined(CONFIG_EXAMPLE_SOCKET_IP_INPUT_STDIN)
        struct sockaddr_storage dest_addr = { 0 };
        ESP_ERROR_CHECK(get_addr_from_stdin(SYNC_PORT, SOCK_DGRAM, &ip_protocol, &addr_family, &dest_addr));
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
        ESP_LOGI(TAG, "Socket created, sending to %s:%d", SYNC_IP_ADDR, SYNC_PORT);
        payload[0] = 'E';
        payload[1] = 'A';
        payload[2] = 0;
        int minrtt = INIT_RTT;

        // ESP_LOGI(TAG, "Message sent");
        while (sync_count-- > 0) {
            ESP_ERROR_CHECK(gptimer_get_raw_count(gptimer, &timer_count));
            *(uint64_t*)(payload+1) = timer_count;
            // int err = sendto(sock, payload, sizeof(timer_count)+2, 0, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
            int err = sendto(sock, payload, strlen(payload), 0, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
            if (err < 0) {
                ESP_LOGE(TAG, "Error occurred during sending: errno %d", errno);
                break;
            }
            // sprintf(payload, "sv time: %lld. %ld", current_time.tv_sec, current_time.tv_usec);
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
                ESP_ERROR_CHECK(gptimer_get_raw_count(gptimer, &received_count));
                int estirtt = received_count - timer_count;
                if (minrtt > estirtt){
                    minrtt = estirtt;
                }
                ESP_LOGI(TAG, "Estimated RTT: %llu", received_count - timer_count);
                // rx_buffer[len] = 0; // Null-terminate whatever we received and treat like a string
                ESP_LOGI(TAG, "Received %d bytes from %s:", len, host_ip);
                // ESP_LOGI(TAG, "%s", rx_buffer);
                // if (strncmp(rx_buffer, "OK: ", 4) == 0) {
                //     ESP_LOGI(TAG, "Received expected message, reconnecting");
                //     break;
                // }
            }
        }
        if (minrtt != INIT_RTT){
            ESP_ERROR_CHECK(gptimer_stop(gptimer));
            ESP_ERROR_CHECK(gptimer_disable(gptimer));
            payload[0] = 'R';
            payload[1] = 0;
            int err = sendto(sock, payload, strlen(payload), 0, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
            if (err < 0) {
                ESP_LOGE(TAG, "Error occurred during sending: errno %d", errno);
                break;
            }
            // sprintf(payload, "sv time: %lld. %ld", current_time.tv_sec, current_time.tv_usec);
            struct sockaddr_storage source_addr; // Large enough for both IPv4 or IPv6
            socklen_t socklen = sizeof(source_addr);
            int len = recvfrom(sock, rx_buffer, sizeof(rx_buffer) - 1, 0, (struct sockaddr *)&source_addr, &socklen);
            if (len==8){
                received_count = ((uint64_t*)rx_buffer)[0];
                ESP_ERROR_CHECK(gptimer_set_raw_count(gptimer, received_count+(minrtt/2)));
                ESP_ERROR_CHECK(gptimer_enable(gptimer));
                ESP_ERROR_CHECK(gptimer_start(gptimer));
            }
            break;
        }
        if (sock != -1) {
            ESP_LOGE(TAG, "Shutting down socket and restarting...");
            shutdown(sock, 0);
            close(sock);
        }
        vTaskDelay(2000 / portTICK_PERIOD_MS);
    }
}
    // vTaskDelete(NULL);


static void udp_server_task(void *pvParameters)
{
    char rx_buffer[128];
    char addr_str[128];
    int addr_family = (int)pvParameters;
    int ip_protocol = 0;
    struct sockaddr_in6 dest_addr;
    uint64_t timer_count;
    while (1) {
        if (addr_family == AF_INET) {
            struct sockaddr_in *dest_addr_ip4 = (struct sockaddr_in *)&dest_addr;
            dest_addr_ip4->sin_addr.s_addr = htonl(INADDR_ANY);
            dest_addr_ip4->sin_family = AF_INET;
            dest_addr_ip4->sin_port = htons(PORT);
            ip_protocol = IPPROTO_IP;
        } else if (addr_family == AF_INET6) {
            bzero(&dest_addr.sin6_addr.un, sizeof(dest_addr.sin6_addr.un));
            dest_addr.sin6_family = AF_INET6;
            dest_addr.sin6_port = htons(PORT);
            ip_protocol = IPPROTO_IPV6;
        }

        int sock = socket(addr_family, SOCK_DGRAM, ip_protocol);
        if (sock < 0) {
            ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
            break;
        }
        ESP_LOGI(TAG, "Socket created");

#if defined(CONFIG_LWIP_NETBUF_RECVINFO) && !defined(CONFIG_ESP32_IPV6)
        int enable = 1;
        lwip_setsockopt(sock, IPPROTO_IP, IP_PKTINFO, &enable, sizeof(enable));
#endif

#if defined(CONFIG_ESP32_IPV4) && defined(CONFIG_ESP32_IPV6)
        if (addr_family == AF_INET6) {
            // Note that by default IPV6 binds to both protocols, it is must be disabled
            // if both protocols used at the same time (used in CI)
            int opt = 1;
            setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
            setsockopt(sock, IPPROTO_IPV6, IPV6_V6ONLY, &opt, sizeof(opt));
        }
#endif
        // Set timeout
        struct timeval timeout;
        timeout.tv_sec = 10;
        timeout.tv_usec = 0;
        setsockopt (sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout);

        int err = bind(sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
        if (err < 0) {
            ESP_LOGE(TAG, "Socket unable to bind: errno %d", errno);
        }
        ESP_LOGI(TAG, "Socket bound, port %d", PORT);

        struct sockaddr_storage source_addr; // Large enough for both IPv4 or IPv6
        socklen_t socklen = sizeof(source_addr);

#if defined(CONFIG_LWIP_NETBUF_RECVINFO) && !defined(CONFIG_ESP32_IPV6)
        struct iovec iov;
        struct msghdr msg;
        struct cmsghdr *cmsgtmp;
        u8_t cmsg_buf[CMSG_SPACE(sizeof(struct in_pktinfo))];

        iov.iov_base = rx_buffer;
        iov.iov_len = sizeof(rx_buffer);
        msg.msg_control = cmsg_buf;
        msg.msg_controllen = sizeof(cmsg_buf);
        msg.msg_flags = 0;
        msg.msg_iov = &iov;
        msg.msg_iovlen = 1;
        msg.msg_name = (struct sockaddr *)&source_addr;
        msg.msg_namelen = socklen;
#endif

        while (1) {
            ESP_LOGI(TAG, "Waiting for data");
#if defined(CONFIG_LWIP_NETBUF_RECVINFO) && !defined(CONFIG_ESP32_IPV6)
            int len = recvmsg(sock, &msg, 0);
#else
            int len = recvfrom(sock, rx_buffer, sizeof(rx_buffer) - 1, 0, (struct sockaddr *)&source_addr, &socklen);
#endif
            // Error occurred during receiving
            if (len < 0) {
                ESP_LOGE(TAG, "recvfrom failed: errno %d", errno);
                break;
            }
            // Data received
            else {
                // Get the sender's ip address as string
                if (source_addr.ss_family == PF_INET) {
                    inet_ntoa_r(((struct sockaddr_in *)&source_addr)->sin_addr, addr_str, sizeof(addr_str) - 1);
#if defined(CONFIG_LWIP_NETBUF_RECVINFO) && !defined(CONFIG_ESP32_IPV6)
                    for ( cmsgtmp = CMSG_FIRSTHDR(&msg); cmsgtmp != NULL; cmsgtmp = CMSG_NXTHDR(&msg, cmsgtmp) ) {
                        if ( cmsgtmp->cmsg_level == IPPROTO_IP && cmsgtmp->cmsg_type == IP_PKTINFO ) {
                            struct in_pktinfo *pktinfo;
                            pktinfo = (struct in_pktinfo*)CMSG_DATA(cmsgtmp);
                            ESP_LOGI(TAG, "dest ip: %s\n", inet_ntoa(pktinfo->ipi_addr));
                        }
                    }
#endif
                } else if (source_addr.ss_family == PF_INET6) {
                    inet6_ntoa_r(((struct sockaddr_in6 *)&source_addr)->sin6_addr, addr_str, sizeof(addr_str) - 1);
                }

                // rx_buffer[len] = 0; // Null-terminate whatever we received and treat like a string...
                ESP_LOGI(TAG, "Received %d bytes from %s:", len, addr_str);
                // sprintf(received_packet_msg, "%.62s", rx_buffer);
                // ESP_LOGI(TAG, "%s", rx_buffer);
                int err;
                err = sendto(sock, rx_buffer, len, 0, (struct sockaddr *)&source_addr, sizeof(source_addr));
                // if (rx_buffer[0] == 'E'){ // ECHO request
                //     err = sendto(sock, rx_buffer+1, len-1, 0, (struct sockaddr *)&source_addr, sizeof(source_addr));
                // } else{
                //     ESP_ERROR_CHECK(gptimer_get_raw_count(gptimer, &timer_count));
                //     err = sendto(sock, (void*)&timer_count, sizeof(timer_count), 0, (struct sockaddr *)&source_addr, sizeof(source_addr));
                // }
                if (err < 0) {
                    ESP_LOGE(TAG, "Error occurred during sending: errno %d", errno);
                    break;
                }
            }
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
    configure_led();
    // timer init
    create_timer();

    get_esp32_mac_addr();


    //Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "ESP_WIFI_MODE_STA");
    wifi_init_sta();

    sync_timer(6);

    set_led_alarm();
    xTaskCreate(alarm_led_task, "alarm_led", 2048, (void*)timer_queue, 2, NULL);

    // xTaskCreatePinnedToCore(display_task, "disp", NULL, 0, NULL, 0 ); // pin to core 0(net)? or 1?
    xTaskCreate(display_task, "disp", 2048, NULL, 1, NULL);
    //Initialize NVS
    // after initialize wifi run udp_server
    xTaskCreate(udp_server_task, "udp_server", 4096, (void*)AF_INET, 5, NULL);
}