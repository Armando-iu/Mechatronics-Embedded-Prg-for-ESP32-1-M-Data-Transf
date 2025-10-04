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

    static void on_data_recv(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len)
    {
        uint8_t *mac = recv_info->src_addr;
        printf("Received from MAC %02X:%02X:%02X:%02X:%02X:%02X: %.*s\n", 
                mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], 
                len, 
                (char *)data);
        blinky(LED_ESPNOW);
    }

    void server_esp_now()
    {
        esp_now_init();
        ESP_LOGI(TAG , "esp_now initialised");
        esp_now_register_recv_cb(on_data_recv);
    }

    static void tcp_server_task(void * pvParams)
    {
        char addr_str[128];
        char serverIP_str[128];
        char rx_buffer[128];
        char string_data[128];
        char data_to_send[128];

        size_t string_data_len;
        size_t data_len;
        
        struct sockaddr_storage dest_addr;

        struct sockaddr_in *dest_addr_ip4 = (struct sockaddr_in *)&dest_addr;
        dest_addr_ip4->sin_addr.s_addr = htonl(INADDR_ANY); 
        /*
            - htonl 
                - To convert 32 bytes of native byte order to network byte order  
            - INADDR_ANY
                - Defines 0.0.0.0 IPV4 style
            - Which accepts all incoming addresses
                - so all client can socket in
        */

        dest_addr_ip4->sin_family = AF_INET;
        dest_addr_ip4->sin_port = htons(PORT); // declare which port the server shuld be in 
        struct in_addr server_ip_addr = dest_addr_ip4->sin_addr;
        inet_ntoa_r(server_ip_addr, serverIP_str, sizeof(serverIP_str) - 1);

        // Open Socket
        int listen_socket = socket(AF_INET , SOCK_STREAM , 0);  // 0 for TCP Protocol and SOCK_STREAM for TCP Comms
        int opt = 1; // option name
        
        // set sock option
        int sock_res = setsockopt(listen_socket ,
                    SOL_SOCKET, // set at socket level
                    SO_REUSEADDR, 
                    &opt, // option enabled
                    sizeof(opt)
                    );
        
        if(sock_res >= 0)
        {
            ESP_LOGI(TAG, "Socket created");
        }
        else
        {
            ESP_LOGE(TAG, "Socket made unsuccesfully");
        }
        
        // bind sock
        int bind_sock_res = bind(listen_socket, 
            (struct sockaddr *)&dest_addr, 
            sizeof(dest_addr));
            
        if(bind_sock_res >= 0)
        {
            ESP_LOGI(TAG, "Socket bound, port %d", PORT);
        }
        else
        {
            ESP_LOGE(TAG, "Socket Not bound, port %d", PORT);
        }

        listen(listen_socket , 1);

        while(1)
        {
            ESP_LOGI(TAG, "Socket listening");

            struct sockaddr_storage source_addr; // Large enough for both IPv4 or IPv6
            socklen_t addr_len = sizeof(source_addr);

            // Accept socket
            int sock = accept(listen_socket, 
                            (struct sockaddr *)&source_addr, 
                            &addr_len);
            if (sock < 0)
            {
                ESP_LOGE(TAG, "Unable to accept connection: errno %d", errno);
                break;
            }
            else
            {
                // put ip into string
                struct sockaddr_in* pV4Addr = (struct sockaddr_in*)&source_addr;
                struct in_addr ipAddr = pV4Addr->sin_addr;
                
                inet_ntoa_r(ipAddr, addr_str, sizeof(addr_str) - 1);

                // receive info
                int recv_result = recv(sock, 
                        rx_buffer, 
                        sizeof(rx_buffer) - 1, 
                        0);

                if(recv_result > 0)
                {
                    ESP_LOGI(TAG , "Server(%s) received msg from client(%s): %s" ,serverIP_str ,addr_str ,rx_buffer);
                }
                else if(recv_result == 0)
                {
                    ESP_LOGI(TAG , "Server(%s) received msg from client(%s): received and client performed an orderly shutdown" ,serverIP_str ,addr_str);
                }
                else
                {
                    ESP_LOGI(TAG , "Server(%s) failed to received msg from client(%s): errno %d (%s) " ,serverIP_str ,addr_str , errno, strerror(errno));
                }

                // Send data via socket
                strcpy(string_data,"Response from ESP32 Base station via Socket connection");
                string_data_len = strlen(string_data);
                sprintf(data_to_send, "HTTP/1.1 200 OK\r\nContent-Length: %d\r\n\r\n", string_data_len);
                strcat(data_to_send, string_data);
                data_len = strlen(data_to_send);

                int send_result = send(sock, data_to_send, data_len, 0);

                if(send_result > -1)
                {
                    ESP_LOGI(TAG , "Server(%s) send msg from client(%s): %s" ,serverIP_str ,addr_str ,string_data);
                    blinky(LED_WIFI);
                }
                else
                {
                    ESP_LOGI(TAG , "Server(%s) failed to send msg from client(%s): errno %d (%s)" ,serverIP_str ,addr_str , errno, strerror(errno));
                }

                vTaskDelay(2000 / portTICK_PERIOD_MS);

                // close the accepted connection after each use. Usually for security (like DOS attacks preventiont)
                shutdown(sock, 0);
                close(sock); 
            }
        }
        close(listen_socket);
        vTaskDelete(NULL);
    }

    void app_main() 
    {
        gpio_out_setup(LED_WIFI);
        gpio_out_setup(LED_ESPNOW);

        init_nvs();
        
        ESP_LOGI(TAG , "Connect wifi: %i" , init_wifi());
        server_esp_now();

        xTaskCreate(tcp_server_task , 
                    "tcp_server" , 
                    4096,
                    (void *)AF_INET ,
                    5,
                    NULL);
    }
}