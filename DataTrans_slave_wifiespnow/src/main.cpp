#include <stdio.h>
#include <stdlib.h>  
#include "esp_log.h"
#include "nvs_flash.h"
#include "driver/gpio.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "freertos/queue.h"

#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_mac.h"
#include "esp_now.h"

#include "lwip/inet.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"
#include "lwip/ip_addr.h"

// LED Pins
#define LED_WIFI GPIO_NUM_13
#define LED_ESPNOW GPIO_NUM_12

// Success/Fail statuses
#define WIFI_FAIL 0
#define WIFI_SUCC 1
#define TCP_FAIL 0
#define TCP_SUCC 1

static const char *TAG = "Data_Trans";

// event group to contain status information
static EventGroupHandle_t wifi_event_group;
#define MAX_FAILURES 10

// retry tracker
static int s_retry_num = 0;

uint8_t CONFIG_ESPNOW_CHANNEL;

#define PORT 5000

// Receiver MAC Address
uint8_t mac_destination[6] = {0xd8, 0x13, 0x2a, 0x7f, 0xab, 0x24};

extern "C"
{

    void blinky(gpio_num_t led_pin)
    {
        gpio_set_level(led_pin, 1);

        vTaskDelay(500 / portTICK_PERIOD_MS);

        gpio_set_level(led_pin, 0);
        vTaskDelay(500 / portTICK_PERIOD_MS);   
    }

    void gpio_out_setup(unsigned long led_pin)
    {
        // Configure GPIO
        gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << led_pin),     // Must change to 64 bit unsigned which Unsigned long long is
        .mode = GPIO_MODE_OUTPUT,              // Set as output
        .pull_up_en = GPIO_PULLUP_DISABLE,     // Disable pull-up
        .pull_down_en = GPIO_PULLDOWN_DISABLE, // Disable pull-down
        .intr_type = GPIO_INTR_DISABLE         // Disable interrupts
        };
        
        esp_err_t ret = gpio_config(&io_conf);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "GPIO config failed for pin %d: %s", LED_WIFI, esp_err_to_name(ret));
        } else {
            ESP_LOGI(TAG, "GPIO %d configured as OUTPUT", LED_WIFI);
        }
    }

    static void wifi_event_handler(void* arg, 
                                    esp_event_base_t event_base,
                                    int32_t event_id, 
                                    void* event_data)
    {
        // AT the start of this event, if everything goes sunshine and rainbows
        if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
        {
            ESP_LOGI(TAG, "Connecting to AP...");
            esp_wifi_connect();
        } 
        // if disconnected somehow
        else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
        {
            if (s_retry_num < MAX_FAILURES) 
            // if can still try connecting, then try connect 
            {
                ESP_LOGI(TAG, "Reconnecting to AP...");
                esp_wifi_connect();
                s_retry_num++;
                blinky(LED_ESPNOW);
            } 
            else 
            // Load unable to connect status
            {
                xEventGroupSetBits(wifi_event_group, WIFI_FAIL);
            }
        }
    }

    static void ip_event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
    {
        // if we got our IP
        if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
        {
            ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
            ESP_LOGI(TAG, "STA IP: " IPSTR, IP2STR(&event->ip_info.ip));

            s_retry_num = 0; // reset counter so that we wont go thru the connect disconnect again
            xEventGroupSetBits(wifi_event_group, WIFI_SUCC);
        }
    }

    void init_nvs()
    {
        esp_err_t ret = nvs_flash_init();
        if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) 
        /*
        ESP_ERR_NVS_NO_FREE_PAGES, Erase free pages if avail

        Page is a logical structure which stores a portion of the overall log.
            - Are ordered by Sequence numbers
            - Higher num means older

        I think they are trying to clear all logs in flash mem 
        */ 
        {
            ESP_ERROR_CHECK( nvs_flash_erase() );
            ret = nvs_flash_init();
        }

        ESP_ERROR_CHECK( ret ); // check if we get errors
    }

    esp_err_t init_wifi()
    {
        /* Init of wifi stuff*/
        int curr_status = WIFI_FAIL;

        // init network interface
        ESP_ERROR_CHECK(esp_netif_init()); 

        // start the infi loop
        ESP_ERROR_CHECK(esp_event_loop_create_default()); 

        // Init wifi station
        /*
            - If I not wrong, just let everyone be able to talk to everyone
            - When fail it aborts, so dc about esp_err_check
        */
        esp_netif_create_default_wifi_sta();  

        // Defualt Congif = no ssid + pass
        wifi_init_config_t wifi_cfg = WIFI_INIT_CONFIG_DEFAULT();
        ESP_ERROR_CHECK(esp_wifi_init(&wifi_cfg)); // esp_wifi_init expects a pointer meaning we gotta feed it Address

        ESP_ERROR_CHECK( esp_wifi_set_storage(WIFI_STORAGE_RAM) );
        
        // Make STA for wifi
        ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_STA) );

        /*
            Event Loop for wifi stuff
        */
        wifi_event_group = xEventGroupCreate();

        esp_event_handler_instance_t wifi_handler_inst;
        ESP_ERROR_CHECK
        (
            esp_event_handler_instance_register
            (
                WIFI_EVENT,
                ESP_EVENT_ANY_ID, // like call event when any disconnect/connect 
                &wifi_event_handler, // callback funct event
                NULL,
                &wifi_handler_inst
            )
        );

        esp_event_handler_instance_t ip_handler_inst;
        ESP_ERROR_CHECK
        (
            esp_event_handler_instance_register
            (
                IP_EVENT,
                IP_EVENT_STA_GOT_IP, // if got IP
                &ip_event_handler, // callback funct event
                NULL,
                &ip_handler_inst
            )
        );

         wifi_config_t wifi_config = 
         {
            .sta = {
                .ssid = "Linksys01999",
                .password = "acax9xrc4s",
                .pmf_cfg = {
                    .capable = true,
                    .required = false
                },
            },
        };

        // set the wifi config
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );

        // start the wifi driver
        ESP_ERROR_CHECK(esp_wifi_start());

        ESP_LOGI(TAG, "STA initialization complete");

        EventBits_t bits = xEventGroupWaitBits(wifi_event_group,
            WIFI_SUCC | WIFI_FAIL,
            pdFALSE,
            pdFALSE,
            portMAX_DELAY);
        
        if (bits & WIFI_SUCC) 
        {
            ESP_LOGI(TAG, "Connected to ap");
            wifi_ap_record_t ap_info;
            ESP_ERROR_CHECK(esp_wifi_sta_get_ap_info(&ap_info));

            uint8_t mac_addr[6];
            ESP_ERROR_CHECK(esp_read_mac(mac_addr , ESP_MAC_WIFI_STA)); 

            ESP_LOGI(TAG, "ESP32 MAC: %02X:%02X:%02X:%02X:%02X:%02X" , mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
            ESP_LOGI(TAG , "AP channel: %02X" , ap_info.primary);
            
            CONFIG_ESPNOW_CHANNEL = ap_info.primary;
            curr_status = WIFI_SUCC;
        } 
        else if (bits & WIFI_FAIL) 
        {
            ESP_LOGI(TAG, "Failed to connect to ap");
            curr_status = WIFI_FAIL;
        } 
        else 
        {
            ESP_LOGE(TAG, "UNEXPECTED EVENT");
            curr_status = WIFI_FAIL;
        }

        /* The event will not be processed after unregister */
        ESP_ERROR_CHECK(esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, ip_handler_inst));
        ESP_ERROR_CHECK(esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_handler_inst));
        vEventGroupDelete(wifi_event_group);

        #if CONFIG_ESPNOW_ENABLE_LONG_RANGE
            ESP_ERROR_CHECK( esp_wifi_set_protocol(ESPNOW_WIFI_IF, WIFI_PROTOCOL_11B|WIFI_PROTOCOL_11G|WIFI_PROTOCOL_11N|WIFI_PROTOCOL_LR) );
        #endif
        return curr_status;
    }

    void tcp_client(void)
    {
        char rx_buffer[128];
        char host_ip[] = "192.168.10.119"; // Server IP
        int addr_family = 0;
        int ip_protocol = 0;
        static const char *payload = "Message from ESP32 TCP Socket Client";

        while (1)
        {
            struct sockaddr_in dest_addr;
            inet_pton(AF_INET, host_ip, &dest_addr.sin_addr);
            dest_addr.sin_family = AF_INET;
            dest_addr.sin_port = htons(PORT);
            addr_family = AF_INET;
            ip_protocol = IPPROTO_IP;

            int sock = socket(addr_family, SOCK_STREAM, ip_protocol);
            if (sock < 0)
            {
                ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
                break;
            }
            ESP_LOGI(TAG, "Socket created ");
            ESP_LOGI(TAG, "Connecting to %s:%d", host_ip, PORT);

            int err = connect(sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
            if (err != 0)
            {
                ESP_LOGE(TAG, "Socket unable to connect: errno %d", errno);
                break;
            }
            ESP_LOGI(TAG, "Successfully connected");

            send(sock, payload, strlen(payload), 0);

            int len = recv(sock, rx_buffer, sizeof(rx_buffer) - 1, 0);
            rx_buffer[len] = 0; // Null-terminate
            ESP_LOGI(TAG, "Received %d bytes from %s:", len, host_ip);
            ESP_LOGI(TAG, "%s", rx_buffer);
            vTaskDelay(5000 / portTICK_PERIOD_MS);

            if (sock != -1)
            {
                shutdown(sock, 0);
                close(sock);
            }
        }
    }

    // Callback function
    void on_data_sent(const uint8_t *mac_addr, esp_now_send_status_t status)
    {
        printf("Sent to MAC %02X:%02X:%02X:%02X:%02X:%02X - Status: %s\n",
            mac_addr[0], mac_addr[1], mac_addr[2],
            mac_addr[3], mac_addr[4], mac_addr[5],
            status == ESP_NOW_SEND_SUCCESS ? "Success" : "Fail");
    }

    void esp_now_client()
    {
        // ESP-NOW initiation and register a callback function that will be called
        esp_now_init();
        esp_now_register_send_cb((esp_now_send_cb_t )on_data_sent);

        // Add a peer device with which ESP32 can communicate
        esp_now_peer_info_t peer = {0};

        // register peer/Base station
        memcpy(peer.peer_addr, mac_destination, 6);
        peer.channel = CONFIG_ESPNOW_CHANNEL;
        peer.encrypt = false;
        esp_now_add_peer(&peer);
    }

    static void esp_now_sender(void * pvParams)
    {
        const char *message = "Hello via ESP-NOW";
        while(1)
        {
            esp_now_send(mac_destination, (uint8_t *)message, strlen(message));
            vTaskDelay(2000 / portTICK_PERIOD_MS);
        }
    }

    void app_main(void)
    {
        gpio_out_setup(LED_WIFI);
        gpio_out_setup(LED_ESPNOW);

        init_nvs();
        ESP_LOGI(TAG , "Connect wifi: %i" , init_wifi());
        esp_now_client();

        xTaskCreate(esp_now_sender , "esp_now_sender" , 4096 , NULL , 5 , NULL);
        tcp_client();
    }
}