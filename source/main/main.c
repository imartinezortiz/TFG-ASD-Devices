#include <stdio.h>
#include <sys/param.h>
#include "esp_err.h"
#include "esp_log.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_timer.h"
#include "esp_vfs_fat.h"
#include "esp_heap_caps.h"
#include "driver/spi_master.h"
#include "driver/sdmmc_host.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "bsp/esp-bsp.h"
#include "sdkconfig.h"
#include "quirc.h"
#include "quirc_internal.h"
#include "src/misc/lv_color.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "protocol_examples_common.h"
#include "esp_camera.h"
#include "esp_ota_ops.h"
#include "json_parser.h"

#include "Camera/camera.h"
#include "MQTT/mqtt.h"
#include "OTA/ota.h"
#include "QR/qr.h"
#include "Screen/screen.h"
#include "Starter/starter.h"

#include "quirc.h"
#include "quirc_internal.h"

#if CONFIG_BOOTLOADER_APP_ANTI_ROLLBACK
#include "esp_efuse.h"
#endif

#include "esp_wifi.h"
#include "common.h"

static const char *TAG = "tfgsegumientodocente";

void build_ota_status_report(char *state, char *buffer, int buffer_size)
{
    // {"current_fw_title": "myFirmware", "current_fw_version": "1.2.3", "fw_state": "UPDATED"}
}

void app_main(void)
{

    heap_caps_print_heap_info(0x00001800);
    ESP_LOGE(TAG, "single largest posible allocation at startup: %d", heap_caps_get_largest_free_block(0x00001800));

    heap_caps_print_heap_info(MALLOC_CAP_SPIRAM);
    ESP_LOGE(TAG, "single largest posible allocation at startup at psram: %d", heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));

    bsp_i2c_init();
    bsp_display_cfg_t cfg = {
        .lvgl_port_cfg = {
            .task_priority = 0,
            .task_stack = 4096,
            .task_affinity = -1,
            .timer_period_ms = TASK_DELAY,
            .task_max_sleep_ms = TASK_DELAY * 2,
        }};
    bsp_display_start_with_config(&cfg);
    bsp_leds_init();

    bsp_led_set(BSP_LED_GREEN, false);

    // Initialize NVS.
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        // 1.OTA app partition table has a smaller NVS partition size than the non-OTA
        // partition table. This size mismatch may cause NVS initialization to fail.
        // 2.NVS partition contains data in new format and cannot be recognized by this version of code.
        // If this happens, we erase NVS partition and initialize NVS again.
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(example_connect());

    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t ota_state;
    int send_updated_mqtt_on_start = false;
    if (esp_ota_get_state_partition(running, &ota_state) == ESP_OK)
    {
        if (ota_state == ESP_OTA_IMG_PENDING_VERIFY)
        {
            // TODO selfcheck
            if (true)
            {
                send_updated_mqtt_on_start = true;
                esp_ota_mark_app_valid_cancel_rollback();
            }
            else
            {
                // TODO mqtt send ota fail
                esp_ota_mark_app_invalid_rollback_and_reboot();
            }
        }
    }

    esp_wifi_set_ps(WIFI_PS_NONE);

    // build queues
    QueueHandle_t cam_to_qr_queue = xQueueCreate(1, sizeof(camera_fb_t *));
    QueueHandle_t qr_to_starter_queue = xQueueCreate(10, sizeof(struct StarterMsg *));
    QueueHandle_t starter_to_screen_queue = xQueueCreate(10, sizeof(struct ScreenMsg *));
    QueueHandle_t qr_to_mqtt_queue = xQueueCreate(10, sizeof(struct MQTTMsg *));
    QueueHandle_t mqtt_to_screen_queue = xQueueCreate(10, sizeof(struct ScreenMsg *));
    QueueHandle_t mqtt_to_ota_queue = xQueueCreate(10, sizeof(struct OTAMsg *));
    QueueHandle_t ota_to_mqtt_queue = xQueueCreate(10, sizeof(struct MQTTMsg *));
    QueueHandle_t ota_to_screen_queue = xQueueCreate(10, sizeof(struct ScreenMsg *));
    QueueHandle_t starter_to_mqtt_queue = xQueueCreate(10, sizeof(struct MQTTMsg *));
    assert(cam_to_qr_queue);
    assert(qr_to_starter_queue);
    assert(starter_to_screen_queue);
    assert(qr_to_mqtt_queue);
    assert(mqtt_to_screen_queue);
    assert(mqtt_to_ota_queue);
    assert(ota_to_mqtt_queue);
    assert(ota_to_screen_queue);
    assert(starter_to_mqtt_queue);
    assert(cam_to_qr_queue);

    // Initialize QR

    struct quirc *qr;
    { // modded quirc_new
        qr = heap_caps_malloc(sizeof(*qr), MALLOC_CAP_SPIRAM);
        memset(qr, 0, sizeof(*qr));
    }

    { // modded quirc_resize  :  quirc_resize(qr, IMG_WIDTH, IMG_HEIGHT);

        struct quirc *q = qr;
        int w = IMG_WIDTH;
        int h = IMG_HEIGHT;

        uint8_t *image = NULL;
        quirc_pixel_t *pixels = NULL;
        size_t num_vars;
        size_t vars_byte_size;
        struct quirc_flood_fill_vars *vars = NULL;

        /*
         * XXX: w and h should be size_t (or at least unsigned) as negatives
         * values would not make much sense. The downside is that it would break
         * both the API and ABI. Thus, at the moment, let's just do a sanity
         * check.
         */
        if (w < 0 || h < 0)
            ESP_LOGE("quirc_resize moded", "fail");

        /*
         * alloc a new buffer for q->image. We avoid realloc(3) because we want
         * on failure to be leave `q` in a consistant, unmodified state.
         */
        image = heap_caps_malloc(w * h, MALLOC_CAP_SPIRAM); // og: calloc(w, h);
        if (!image)
            ESP_LOGE("quirc_resize moded", "fail");

        /* compute the "old" (i.e. currently allocated) and the "new"
           (i.e. requested) image dimensions */
        size_t olddim = q->w * q->h;
        size_t newdim = w * h;
        size_t min = (olddim < newdim ? olddim : newdim);

        /*
         * copy the data into the new buffer, avoiding (a) to read beyond the
         * old buffer when the new size is greater and (b) to write beyond the
         * new buffer when the new size is smaller, hence the min computation.
         */
        (void)memcpy(image, q->image, min);

        /* alloc a new buffer for q->pixels if needed */
        if (!QUIRC_PIXEL_ALIAS_IMAGE)
        {
            pixels = heap_caps_malloc(newdim * sizeof(quirc_pixel_t), MALLOC_CAP_SPIRAM); // og: calloc(newdim, sizeof(quirc_pixel_t));
            if (!pixels)
                ESP_LOGE("quirc_resize moded", "fail");
        }

        /*
         * alloc the work area for the flood filling logic.
         *
         * the size was chosen with the following assumptions and observations:
         *
         * - rings are the regions which requires the biggest work area.
         * - they consumes the most when they are rotated by about 45 degree.
         *   in that case, the necessary depth is about (2 * height_of_the_ring).
         * - the maximum height of rings would be about 1/3 of the image height.
         */

        if ((size_t)h * 2 / 2 != h)
        {
            ESP_LOGE("quirc_resize moded", "fail");
        }
        num_vars = (size_t)h * 2 / 3;
        if (num_vars == 0)
        {
            num_vars = 1;
        }

        vars_byte_size = sizeof(*vars) * num_vars;
        if (vars_byte_size / sizeof(*vars) != num_vars)
        {
            ESP_LOGE("quirc_resize moded", "fail");
        }
        vars = malloc(vars_byte_size); // heap_caps_malloc(vars_byte_size, MALLOC_CAP_SPIRAM);
        if (!vars)
            ESP_LOGE("quirc_resize moded", "fail");

        /* alloc succeeded, update `q` with the new size and buffers */
        q->w = w;
        q->h = h;
        free(q->image);
        q->image = image;
        if (!QUIRC_PIXEL_ALIAS_IMAGE)
        {
            free(q->pixels);
            q->pixels = pixels;
        }
        free(q->flood_fill_vars);
        q->flood_fill_vars = vars;
        q->num_flood_fill_vars = num_vars;
    }

    struct QRConf *qr_conf = malloc(sizeof(struct QRConf));
    qr_conf->cam_to_qr_queue = cam_to_qr_queue;
    qr_conf->qr_to_starter_queue = qr_to_starter_queue;
    qr_conf->qr_to_mqtt_queue = qr_to_mqtt_queue;
    qr_conf->qr = qr;

    qr_start(qr_conf);
    ESP_LOGI(TAG, "qr started");

    // Initialize MQTT

    struct MQTTConf *mqtt_conf = malloc(sizeof(struct MQTTConf));
    mqtt_conf->qr_to_mqtt_queue = qr_to_mqtt_queue;
    mqtt_conf->mqtt_to_ota_queue = mqtt_to_ota_queue;
    mqtt_conf->ota_to_mqtt_queue = ota_to_mqtt_queue;
    mqtt_conf->mqtt_to_screen_queue = mqtt_to_screen_queue;
    mqtt_conf->starter_to_mqtt_queue = starter_to_mqtt_queue;
    mqtt_conf->send_updated_mqtt_on_start = send_updated_mqtt_on_start;
    mqtt_start(mqtt_conf);
    ESP_LOGI(TAG, "mqtt started");

    // Initialize the camera

    camera_config_t camera_config = BSP_CAMERA_DEFAULT_CONFIG;
    camera_config.frame_size = CAM_FRAME_SIZE;

    struct CameraConf *cam_conf = malloc(sizeof(struct CameraConf));
    cam_conf->cam_to_qr_queue = cam_to_qr_queue;
    cam_conf->camera_config = camera_config;
    camera_start(cam_conf);
    ESP_LOGI(TAG, "cam started");

    // Initialize OTA

    struct OTAConf *ota_conf = malloc(sizeof(struct OTAConf));

    ota_conf->ota_to_mqtt_queue = ota_to_mqtt_queue;
    ota_conf->mqtt_to_ota_queue = mqtt_to_ota_queue;
    ota_conf->ota_to_screen_queue = ota_to_screen_queue;
    ota_start(ota_conf);
    ESP_LOGI(TAG, "ota started");

    // Initialize Screen

    struct ScreenConf *screen_conf = malloc(sizeof(struct ScreenConf));
    screen_conf->starter_to_screen_queue = starter_to_screen_queue;
    screen_conf->mqtt_to_screen_queue = mqtt_to_screen_queue;
    screen_conf->ota_to_screen_queue = ota_to_screen_queue;

    screen_start(screen_conf);
    ESP_LOGI(TAG, "screen started");

    // Initialize Starter

    struct StarterConf *starter_conf = malloc(sizeof(struct StarterConf));
    starter_conf->starter_to_screen_queue = starter_to_screen_queue;
    starter_conf->qr_to_starter_queue = qr_to_starter_queue;
    starter_conf->starter_to_mqtt_queue = starter_to_mqtt_queue;

    start_starter(starter_conf);
    ESP_LOGI(TAG, "starter started");

    struct MQTTMsg *jump_start_msg = malloc(sizeof(struct MQTTMsg));
    jump_start_msg->command = start;
    strcpy(&(jump_start_msg->data.start.broker_url), "mqtts://thingsboard.asd:8883");

    xQueueSend(starter_to_mqtt_queue, &jump_start_msg, 0);

    // mqtt_subscribe("v1/devices/me/attributes"); // check if it worked or needs retriying
    // mqtt_send("v1/devices/me/telemetry", "{\"online\":true}");
}
