#include "camera.h"
#include "../QR/qr.h"
#include "esp_log.h"

#define TAG "camera"

void camera_task(void *arg)
{
    struct CameraConf *conf = arg;

    while (1)
    {
        camera_fb_t *pic = esp_camera_fb_get();
        if (pic == NULL)
        {
            ESP_LOGE(TAG, "Get frame failed");
            continue;
        }
        // Don't update the display if the display image was just updated
        // (i.e. is still frozen)

        // Send the frame to the processing task.
        // Note the short delay — if the processing task is busy, simply drop the frame.
        int res = xQueueSend(conf->cam_to_qr_queue, &pic, 0);
        if (res == pdFAIL)
        {
            esp_camera_fb_return(pic);
        }
    }
}

void camera_start(struct CameraConf *conf)
{

    ESP_ERROR_CHECK(esp_camera_init(&conf->camera_config));
    sensor_t *s = esp_camera_sensor_get();
    s->set_vflip(s, 1);
    ESP_LOGI(TAG, "Camera Init done");

    xTaskCreate(&camera_task, "Camera Task", 35000, conf, 1, NULL);
}
