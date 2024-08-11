#include <esp_http_server.h>
#include <esp_camera.h>
#include <esp_timer.h>
#include <img_converters.h>
#include <fb_gfx.h>
#include <vector>
#include <human_face_detect_msr01.hpp>
#include <human_face_detect_mnp01.hpp>
#include "servos.h"
#include "server.h"
#include "index.h"

#pragma GCC diagnostic ignored "-Wformat"
#pragma GCC diagnostic ignored "-Wstrict-aliasing"
#define CONFIG_ESP_FACE_DETECT_ENABLED 1

#define TWO_STAGE 0 /*<! 1: detect by two-stage which is more accurate but slower(with keypoints). */
                    /*<! 0: detect by one-stage which is less accurate but faster(without keypoints). */

typedef struct
{
    httpd_req_t *req;
    size_t len;
} jpg_chunking_t;

#define PART_BOUNDARY "123456789000000000000987654321"
static const char *_STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char *_STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char *_STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\nX-Timestamp: %d.%06d\r\n\r\n";

httpd_handle_t stream_httpd = NULL;
httpd_handle_t camera_httpd = NULL;

static int8_t detection_enabled = 1;

// #if TWO_STAGE
// static HumanFaceDetectMSR01 s1(0.1F, 0.5F, 10, 0.2F);
// static HumanFaceDetectMNP01 s2(0.5F, 0.3F, 5);
// #else
// static HumanFaceDetectMSR01 s1(0.3F, 0.5F, 10, 0.2F);
// #endif

#define QUANT_TYPE 0 // if set to 1 => very large firmware, very slow, reboots when streaming...
#define FACE_COLOR_WHITE 0x00FFFFFF
#define FACE_COLOR_BLACK 0x00000000
#define FACE_COLOR_RED 0x000000FF
#define FACE_COLOR_GREEN 0x0000FF00
#define FACE_COLOR_BLUE 0x00FF0000
#define FACE_COLOR_YELLOW (FACE_COLOR_RED | FACE_COLOR_GREEN)
#define FACE_COLOR_CYAN (FACE_COLOR_BLUE | FACE_COLOR_GREEN)
#define FACE_COLOR_PURPLE (FACE_COLOR_BLUE | FACE_COLOR_RED)

bool isStreaming = false;
typedef struct
{
    size_t size;  // number of values used for filtering
    size_t index; // current value index
    size_t count; // value count
    int sum;
    int *values; // array to be filled with values
} ra_filter_t;

static ra_filter_t ra_filter;

static ra_filter_t *ra_filter_init(ra_filter_t *filter, size_t sample_size)
{
    memset(filter, 0, sizeof(ra_filter_t));

    filter->values = (int *)malloc(sample_size * sizeof(int));
    if (!filter->values)
    {
        return NULL;
    }
    memset(filter->values, 0, sample_size * sizeof(int));

    filter->size = sample_size;
    return filter;
}

static std::list<dl::detect::result_t> inference(camera_fb_t *fb)
{
#if TWO_STAGE
    HumanFaceDetectMSR01 s1(0.1F, 0.5F, 10, 0.2F);
    HumanFaceDetectMNP01 s2(0.5F, 0.3F, 5);
#else
    HumanFaceDetectMSR01 s1(0.3F, 0.5F, 10, 0.2F);
#endif
#if TWO_STAGE
    std::list<dl::detect::result_t> &candidates = s1.infer((uint16_t *)fb->buf, {(int)fb->height, (int)fb->width, 3});
    std::list<dl::detect::result_t> &results = s2.infer((uint16_t *)fb->buf, {(int)fb->height, (int)fb->width, 3}, candidates);
#else
    std::list<dl::detect::result_t> &results = s1.infer((uint16_t *)fb->buf, {(int)fb->height, (int)fb->width, 3});
#endif
    return results;
}

// void draw_face_boxes(fb_data_t *fb, std::list<dl::detect::result_t> *results, int face_id)
// {
//     int x, y, w, h;
//     int center_x, center_y;
//     uint32_t color = FACE_COLOR_YELLOW;
//     if (face_id < 0)
//         color = FACE_COLOR_RED;
//     else if (face_id > 0)
//         color = FACE_COLOR_GREEN;

//     if (fb->bytes_per_pixel == 2)
//         color = ((color >> 16) & 0x001F) | ((color >> 3) & 0x07E0) | ((color << 8) & 0xF800);

//     int i = 0;
//     for (std::list<dl::detect::result_t>::iterator prediction = results->begin(); prediction != results->end(); prediction++, i++)
//     {
//         x = (int)prediction->box[0];
//         y = (int)prediction->box[1];
//         w = (int)prediction->box[2] - x + 1;
//         h = (int)prediction->box[3] - y + 1;

//         center_x = w / 2 + x;
//         center_y = h / 2 + y;

//         if ((x + w) > fb->width)
//             w = fb->width - x;
//         if ((y + h) > fb->height)
//             h = fb->height - y;
//         fb_gfx_drawFastHLine(fb, center_x, center_y, 5, color);
//         fb_gfx_drawFastHLine(fb, center_x, center_y + 5, 5, color);
//         fb_gfx_drawFastVLine(fb, center_x, center_y, 5, color);
//         fb_gfx_drawFastVLine(fb, center_x + 5, y, h, color);
//     }
// }
static void draw_face_boxes(fb_data_t *fb, std::list<dl::detect::result_t> *results, int face_id)
{
    int x, y, w, h;
    uint32_t color = FACE_COLOR_YELLOW;
    if (face_id < 0)
    {
        color = FACE_COLOR_RED;
    }
    else if (face_id > 0)
    {
        color = FACE_COLOR_GREEN;
    }
    if (fb->bytes_per_pixel == 2)
    {
        // color = ((color >> 8) & 0xF800) | ((color >> 3) & 0x07E0) | (color & 0x001F);
        color = ((color >> 16) & 0x001F) | ((color >> 3) & 0x07E0) | ((color << 8) & 0xF800);
    }
    int i = 0;
    for (std::list<dl::detect::result_t>::iterator prediction = results->begin(); prediction != results->end(); prediction++, i++)
    {
        // rectangle box
        x = (int)prediction->box[0];
        y = (int)prediction->box[1];
        w = (int)prediction->box[2] - x + 1;
        h = (int)prediction->box[3] - y + 1;
        if ((x + w) > fb->width)
        {
            w = fb->width - x;
        }
        if ((y + h) > fb->height)
        {
            h = fb->height - y;
        }
        fb_gfx_drawFastHLine(fb, x, y, w, color);
        fb_gfx_drawFastHLine(fb, x, y + h - 1, w, color);
        fb_gfx_drawFastVLine(fb, x, y, h, color);
        fb_gfx_drawFastVLine(fb, x + w - 1, y, h, color);
#if TWO_STAGE
        // landmarks (left eye, mouth left, nose, right eye, mouth right)
        int x0, y0, j;
        for (j = 0; j < 10; j += 2)
        {
            x0 = (int)prediction->keypoint[j];
            y0 = (int)prediction->keypoint[j + 1];
            fb_gfx_fillRect(fb, x0, y0, 3, 3, color);
        }
#endif
    }
}

#ifdef PARSEGETVAR
static int parse_get_var(char *buf, const char *key, int def)
{
    char _int[16];
    if (httpd_query_key_value(buf, key, _int, sizeof(_int)) != ESP_OK)
    {
        return def;
    }
    return atoi(_int);
}
#endif

static esp_err_t parse_get(httpd_req_t *req, char **obuf)
{
    char *buf = NULL;
    size_t buf_len = 0;

    buf_len = httpd_req_get_url_query_len(req) + 1;
    if (buf_len > 1)
    {
        buf = (char *)malloc(buf_len);
        if (!buf)
        {
            httpd_resp_send_500(req);
            return ESP_FAIL;
        }
        if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK)
        {
            *obuf = buf;
            return ESP_OK;
        }
        free(buf);
    }
    httpd_resp_send_404(req);
    return ESP_FAIL;
}

static esp_err_t cmd_handler(httpd_req_t *req)
{
    char *buf = NULL;
    char variable[32];
    char value[32];

    if (parse_get(req, &buf) != ESP_OK)
    {
        return ESP_FAIL;
    }
    if (httpd_query_key_value(buf, "var", variable, sizeof(variable)) != ESP_OK || httpd_query_key_value(buf, "val", value, sizeof(value)) != ESP_OK)
    {
        free(buf);
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }
    free(buf);

    int val = atoi(value);
    log_i("%s = %d", variable, val);
    sensor_t *s = esp_camera_sensor_get();
    int res = 0;

    if (!strcmp(variable, "framesize"))
    {
        if (s->pixformat == PIXFORMAT_JPEG)
        {
            res = s->set_framesize(s, (framesize_t)val);
        }
    }
    else if (!strcmp(variable, "quality"))
    {
        res = s->set_quality(s, val);
    }
    else if (!strcmp(variable, "servos-mode"))
    {
        if (val == 0)
            set_mode(CONTROL_MODE);
        else if (val == 1)
            set_mode(SWEEP_MODE);
    }
    else if (!strcmp(variable, "left"))
        move(LEFT, val);
    else if (!strcmp(variable, "right"))
        move(RIGHT, val);
    else if (!strcmp(variable, "up"))
        move(UP, val);
    else if (!strcmp(variable, "down"))
        move(DOWN, val);
    else if (!strcmp(variable, "center"))
        center();
    
#if CONFIG_ESP_FACE_DETECT_ENABLED
    else if (!strcmp(variable, "face_detect"))
    {
        detection_enabled = val;
#if CONFIG_ESP_FACE_RECOGNITION_ENABLED
        if (!detection_enabled)
        {
            recognition_enabled = 0;
        }
#endif
    }
#endif
#ifdef CAM_OPTS
    else if (!strcmp(variable, "quality"))
    {
        res = s->set_quality(s, val);
    }
    else if (!strcmp(variable, "contrast"))
    {
        res = s->set_contrast(s, val);
    }
    else if (!strcmp(variable, "brightness"))
    {
        res = s->set_brightness(s, val);
    }
    else if (!strcmp(variable, "saturation"))
    {
        res = s->set_saturation(s, val);
    }
    else if (!strcmp(variable, "gainceiling"))
    {
        res = s->set_gainceiling(s, (gainceiling_t)val);
    }
    else if (!strcmp(variable, "colorbar"))
    {
        res = s->set_colorbar(s, val);
    }
    else if (!strcmp(variable, "awb"))
    {
        res = s->set_whitebal(s, val);
    }
    else if (!strcmp(variable, "agc"))
    {
        res = s->set_gain_ctrl(s, val);
    }
    else if (!strcmp(variable, "aec"))
    {
        res = s->set_exposure_ctrl(s, val);
    }
    else if (!strcmp(variable, "hmirror"))
    {
        res = s->set_hmirror(s, val);
    }
    else if (!strcmp(variable, "vflip"))
    {
        res = s->set_vflip(s, val);
    }
    else if (!strcmp(variable, "awb_gain"))
    {
        res = s->set_awb_gain(s, val);
    }
    else if (!strcmp(variable, "agc_gain"))
    {
        res = s->set_agc_gain(s, val);
    }
    else if (!strcmp(variable, "aec_value"))
    {
        res = s->set_aec_value(s, val);
    }
    else if (!strcmp(variable, "aec2"))
    {
        res = s->set_aec2(s, val);
    }
    else if (!strcmp(variable, "dcw"))
    {
        res = s->set_dcw(s, val);
    }
    else if (!strcmp(variable, "bpc"))
    {
        res = s->set_bpc(s, val);
    }
    else if (!strcmp(variable, "wpc"))
    {
        res = s->set_wpc(s, val);
    }
    else if (!strcmp(variable, "raw_gma"))
    {
        res = s->set_raw_gma(s, val);
    }
    else if (!strcmp(variable, "lenc"))
    {
        res = s->set_lenc(s, val);
    }
    else if (!strcmp(variable, "special_effect"))
    {
        res = s->set_special_effect(s, val);
    }
    else if (!strcmp(variable, "wb_mode"))
    {
        res = s->set_wb_mode(s, val);
    }
    else if (!strcmp(variable, "ae_level"))
    {
        res = s->set_ae_level(s, val);
    }
#endif
    else
    {
        log_i("Unknown command: %s", variable);
        res = -1;
    }

    if (res < 0)
    {
        return httpd_resp_send_500(req);
    }

    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, NULL, 0);
}

// static esp_err_t stream_handler(httpd_req_t *req)
// {
//     camera_fb_t *fb = NULL;
//     struct timeval _timestamp;
//     esp_err_t res = ESP_OK;
//     size_t _jpg_buf_len = 0;
//     uint8_t *_jpg_buf = NULL;
//     char *part_buf[128];
//     int face_id = 0;
//     size_t out_len = 0, out_width = 0, out_height = 0;
//     uint8_t *out_buf = NULL;
//     bool s = false;
// #if TWO_STAGE
//     HumanFaceDetectMSR01 s1(0.1F, 0.5F, 10, 0.2F);
//     HumanFaceDetectMNP01 s2(0.5F, 0.3F, 5);
// #else
//     HumanFaceDetectMSR01 s1(0.3F, 0.5F, 10, 0.2F);
// #endif
//     static int64_t last_frame = 0;
//     if (!last_frame)
//         last_frame = esp_timer_get_time();
//     res = httpd_resp_set_type(req, _STREAM_CONTENT_TYPE);
//     if (res != ESP_OK)
//         return res;
//     httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
//     httpd_resp_set_hdr(req, "X-Framerate", "60");
//     for (;;)
//     {
//         face_id = 0;
//         fb = esp_camera_fb_get();
//         if (!fb)
//         {
//             log_e("Camera capture failed");
//             res = ESP_FAIL;
//         }
//         else
//         {
//             _timestamp.tv_sec = fb->timestamp.tv_sec;
//             _timestamp.tv_usec = fb->timestamp.tv_usec;
//         }
//         if (fb->format == PIXFORMAT_RGB565)
//         {
// #if TWO_STAGE
//             std::list<dl::detect::result_t> &candidates = s1.infer((uint16_t *)fb->buf, {(int)fb->height, (int)fb->width, 3});
//             std::list<dl::detect::result_t> &results = s2.infer((uint16_t *)fb->buf, {(int)fb->height, (int)fb->width, 3}, candidates);
// #else
//             std::list<dl::detect::result_t> &results = s1.infer((uint16_t *)fb->buf, {(int)fb->height, (int)fb->width, 3});
// #endif
//             if (results.size() > 0)
//             {
//                 fb_data_t rfb;
//                 rfb.width = fb->width;
//                 rfb.height = fb->height;
//                 rfb.data = fb->buf;
//                 rfb.bytes_per_pixel = 2;
//                 rfb.format = FB_RGB565;
//                 draw_face_boxes(&rfb, &results, face_id);
//                 s = fmt2jpg(fb->buf, fb->len, fb->width, fb->height, PIXFORMAT_RGB565, 80, &_jpg_buf, &_jpg_buf_len);
//                 esp_camera_fb_return(fb);
//                 fb = NULL;
//                 if (!s)
//                     res = ESP_FAIL;
//             }
//             else
//             {
//                 out_len = fb->width * fb->height * 3;
//                 out_width = fb->width;
//                 out_height = fb->height;
//                 out_buf = (uint8_t *)malloc(out_len);
//                 if (!out_buf)
//                     res = ESP_FAIL;
//                 else
//                 {
//                     s = fmt2rgb888(fb->buf, fb->len, fb->format, out_buf);
//                     esp_camera_fb_return(fb);
//                     fb = NULL;
//                     if (!s)
//                     {
//                         free(out_buf);
//                         res = ESP_FAIL;
//                     }
//                     else
//                     {
//                         fb_data_t rfb;
//                         rfb.width = out_width;
//                         rfb.height = out_height;
//                         rfb.data = out_buf;
//                         rfb.bytes_per_pixel = 3;
//                         rfb.format = FB_BGR888;
// #if TWO_STAGE
//                         std::list<dl::detect::result_t> &candidates = s1.infer((uint8_t *)out_buf, {(int)out_height, (int)out_width, 3});
//                         std::list<dl::detect::result_t> &results = s2.infer((uint8_t *)out_buf, {(int)out_height, (int)out_width, 3}, candidates);
// #else
//                         std::list<dl::detect::result_t> &results = s1.infer((uint8_t *)out_buf, {(int)out_height, (int)out_width, 3});
// #endif
//                         if (results.size() > 0)
//                             draw_face_boxes(&rfb, &results, face_id);
//                         s = fmt2jpg(out_buf, out_len, out_width, out_height, PIXFORMAT_RGB888, 90, &_jpg_buf, &_jpg_buf_len);
//                         free(out_buf);
//                         if (!s)
//                             res = ESP_FAIL;
//                     }
//                 }
//             }
//         }
//         if (res == ESP_OK)
//             res = httpd_resp_send_chunk(req, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY));
//         if (res == ESP_OK)
//         {
//             size_t hlen = snprintf((char *)part_buf, 128, _STREAM_PART, _jpg_buf_len, _timestamp.tv_sec, _timestamp.tv_usec);
//             res = httpd_resp_send_chunk(req, (const char *)part_buf, hlen);
//         }
//         if (res == ESP_OK)
//             res = httpd_resp_send_chunk(req, (const char *)_jpg_buf, _jpg_buf_len);
//         if (fb)
//         {
//             esp_camera_fb_return(fb);
//             fb = NULL;
//             _jpg_buf = NULL;
//         }
//         else if (_jpg_buf)
//         {
//             free(_jpg_buf);
//             _jpg_buf = NULL;
//         }
//         if (res != ESP_OK)
//             break;
//         int64_t fr_end = esp_timer_get_time();
//         int64_t frame_time = fr_end - last_frame;
//         frame_time /= 1000;
//     }
//     return res;
// }

static esp_err_t index_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    return httpd_resp_send(req, (const char *)index_html_gz, index_html_gz_len);
}

static esp_err_t stream_handler(httpd_req_t *req)
{
    camera_fb_t *fb = NULL;
    struct timeval _timestamp;
    esp_err_t res = ESP_OK;
    size_t _jpg_buf_len = 0;
    uint8_t *_jpg_buf = NULL;
    char *part_buf[128];
#if CONFIG_ESP_FACE_DETECT_ENABLED
    int face_id = 0;
    size_t out_len = 0, out_width = 0, out_height = 0;
    uint8_t *out_buf = NULL;
    bool s = false;
#if TWO_STAGE
    HumanFaceDetectMSR01 s1(0.1F, 0.5F, 10, 0.2F);
    HumanFaceDetectMNP01 s2(0.5F, 0.3F, 5);
#else
    HumanFaceDetectMSR01 s1(0.3F, 0.5F, 10, 0.2F);
#endif
#endif

    static int64_t last_frame = 0;
    if (!last_frame)
    {
        last_frame = esp_timer_get_time();
    }

    res = httpd_resp_set_type(req, _STREAM_CONTENT_TYPE);
    if (res != ESP_OK)
    {
        return res;
    }

    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "X-Framerate", "60");

#if CONFIG_LED_ILLUMINATOR_ENABLED
    isStreaming = true;
    enable_led(true);
#endif

    while (true)
    {
#if CONFIG_ESP_FACE_DETECT_ENABLED
#if ARDUHAL_LOG_LEVEL >= ARDUHAL_LOG_LEVEL_INFO
        detected = false;
#endif
        face_id = 0;
#endif

        fb = esp_camera_fb_get();
        if (!fb)
        {
            log_e("Camera capture failed");
            res = ESP_FAIL;
        }
        else
        {
            _timestamp.tv_sec = fb->timestamp.tv_sec;
            _timestamp.tv_usec = fb->timestamp.tv_usec;
#if CONFIG_ESP_FACE_DETECT_ENABLED
#if ARDUHAL_LOG_LEVEL >= ARDUHAL_LOG_LEVEL_INFO
            fr_start = esp_timer_get_time();
            fr_ready = fr_start;
            fr_encode = fr_start;
            fr_recognize = fr_start;
            fr_face = fr_start;
#endif
            if (!detection_enabled || fb->width > 400)
            {
#endif
                if (fb->format != PIXFORMAT_JPEG)
                {
                    bool jpeg_converted = frame2jpg(fb, 80, &_jpg_buf, &_jpg_buf_len);
                    esp_camera_fb_return(fb);
                    fb = NULL;
                    if (!jpeg_converted)
                    {
                        log_e("JPEG compression failed");
                        res = ESP_FAIL;
                    }
                }
                else
                {
                    _jpg_buf_len = fb->len;
                    _jpg_buf = fb->buf;
                }
#if CONFIG_ESP_FACE_DETECT_ENABLED
            }
            else
            {
                if (fb->format == PIXFORMAT_RGB565
#if CONFIG_ESP_FACE_RECOGNITION_ENABLED
                    && !recognition_enabled
#endif
                )
                {
#if ARDUHAL_LOG_LEVEL >= ARDUHAL_LOG_LEVEL_INFO
                    fr_ready = esp_timer_get_time();
#endif
#if TWO_STAGE
                    std::list<dl::detect::result_t> &candidates = s1.infer((uint16_t *)fb->buf, {(int)fb->height, (int)fb->width, 3});
                    std::list<dl::detect::result_t> &results = s2.infer((uint16_t *)fb->buf, {(int)fb->height, (int)fb->width, 3}, candidates);
#else
                    std::list<dl::detect::result_t> &results = s1.infer((uint16_t *)fb->buf, {(int)fb->height, (int)fb->width, 3});
#endif
#if CONFIG_ESP_FACE_DETECT_ENABLED && ARDUHAL_LOG_LEVEL >= ARDUHAL_LOG_LEVEL_INFO
                    fr_face = esp_timer_get_time();
                    fr_recognize = fr_face;
#endif
                    if (results.size() > 0)
                    {
                        fb_data_t rfb;
                        rfb.width = fb->width;
                        rfb.height = fb->height;
                        rfb.data = fb->buf;
                        rfb.bytes_per_pixel = 2;
                        rfb.format = FB_RGB565;
#if ARDUHAL_LOG_LEVEL >= ARDUHAL_LOG_LEVEL_INFO
                        detected = true;
#endif
                        draw_face_boxes(&rfb, &results, face_id);
                    }
                    s = fmt2jpg(fb->buf, fb->len, fb->width, fb->height, PIXFORMAT_RGB565, 80, &_jpg_buf, &_jpg_buf_len);
                    esp_camera_fb_return(fb);
                    fb = NULL;
                    if (!s)
                    {
                        log_e("fmt2jpg failed");
                        res = ESP_FAIL;
                    }
#if CONFIG_ESP_FACE_DETECT_ENABLED && ARDUHAL_LOG_LEVEL >= ARDUHAL_LOG_LEVEL_INFO
                    fr_encode = esp_timer_get_time();
#endif
                }
                else
                {
                    out_len = fb->width * fb->height * 3;
                    out_width = fb->width;
                    out_height = fb->height;
                    out_buf = (uint8_t *)malloc(out_len);
                    if (!out_buf)
                    {
                        log_e("out_buf malloc failed");
                        res = ESP_FAIL;
                    }
                    else
                    {
                        s = fmt2rgb888(fb->buf, fb->len, fb->format, out_buf);
                        esp_camera_fb_return(fb);
                        fb = NULL;
                        if (!s)
                        {
                            free(out_buf);
                            log_e("To rgb888 failed");
                            res = ESP_FAIL;
                        }
                        else
                        {
#if ARDUHAL_LOG_LEVEL >= ARDUHAL_LOG_LEVEL_INFO
                            fr_ready = esp_timer_get_time();
#endif

                            fb_data_t rfb;
                            rfb.width = out_width;
                            rfb.height = out_height;
                            rfb.data = out_buf;
                            rfb.bytes_per_pixel = 3;
                            rfb.format = FB_BGR888;

#if TWO_STAGE
                            std::list<dl::detect::result_t> &candidates = s1.infer((uint8_t *)out_buf, {(int)out_height, (int)out_width, 3});
                            std::list<dl::detect::result_t> &results = s2.infer((uint8_t *)out_buf, {(int)out_height, (int)out_width, 3}, candidates);
#else
                            std::list<dl::detect::result_t> &results = s1.infer((uint8_t *)out_buf, {(int)out_height, (int)out_width, 3});
#endif

#if CONFIG_ESP_FACE_DETECT_ENABLED && ARDUHAL_LOG_LEVEL >= ARDUHAL_LOG_LEVEL_INFO
                            fr_face = esp_timer_get_time();
                            fr_recognize = fr_face;
#endif

                            if (results.size() > 0)
                            {
#if ARDUHAL_LOG_LEVEL >= ARDUHAL_LOG_LEVEL_INFO
                                detected = true;
#endif
#if CONFIG_ESP_FACE_RECOGNITION_ENABLED
                                if (recognition_enabled)
                                {
                                    face_id = run_face_recognition(&rfb, &results);
#if ARDUHAL_LOG_LEVEL >= ARDUHAL_LOG_LEVEL_INFO
                                    fr_recognize = esp_timer_get_time();
#endif
                                }
#endif
                                draw_face_boxes(&rfb, &results, face_id);
                            }
                            s = fmt2jpg(out_buf, out_len, out_width, out_height, PIXFORMAT_RGB888, 90, &_jpg_buf, &_jpg_buf_len);
                            free(out_buf);
                            if (!s)
                            {
                                log_e("fmt2jpg failed");
                                res = ESP_FAIL;
                            }
#if CONFIG_ESP_FACE_DETECT_ENABLED && ARDUHAL_LOG_LEVEL >= ARDUHAL_LOG_LEVEL_INFO
                            fr_encode = esp_timer_get_time();
#endif
                        }
                    }
                }
            }
#endif
        }
        if (res == ESP_OK)
        {
            res = httpd_resp_send_chunk(req, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY));
        }
        if (res == ESP_OK)
        {
            size_t hlen = snprintf((char *)part_buf, 128, _STREAM_PART, _jpg_buf_len, _timestamp.tv_sec, _timestamp.tv_usec);
            res = httpd_resp_send_chunk(req, (const char *)part_buf, hlen);
        }
        if (res == ESP_OK)
        {
            res = httpd_resp_send_chunk(req, (const char *)_jpg_buf, _jpg_buf_len);
        }
        if (fb)
        {
            esp_camera_fb_return(fb);
            fb = NULL;
            _jpg_buf = NULL;
        }
        else if (_jpg_buf)
        {
            free(_jpg_buf);
            _jpg_buf = NULL;
        }
        if (res != ESP_OK)
        {
            log_e("Send frame failed");
            break;
        }
        int64_t fr_end = esp_timer_get_time();

#if CONFIG_ESP_FACE_DETECT_ENABLED && ARDUHAL_LOG_LEVEL >= ARDUHAL_LOG_LEVEL_INFO
        int64_t ready_time = (fr_ready - fr_start) / 1000;
        int64_t face_time = (fr_face - fr_ready) / 1000;
        int64_t recognize_time = (fr_recognize - fr_face) / 1000;
        int64_t encode_time = (fr_encode - fr_recognize) / 1000;
        int64_t process_time = (fr_encode - fr_start) / 1000;
#endif

        int64_t frame_time = fr_end - last_frame;
        frame_time /= 1000;
#if ARDUHAL_LOG_LEVEL >= ARDUHAL_LOG_LEVEL_INFO
        uint32_t avg_frame_time = ra_filter_run(&ra_filter, frame_time);
#endif
        log_i(
            "MJPG: %uB %ums (%.1ffps), AVG: %ums (%.1ffps)"
#if CONFIG_ESP_FACE_DETECT_ENABLED
            ", %u+%u+%u+%u=%u %s%d"
#endif
            ,
            (uint32_t)(_jpg_buf_len), (uint32_t)frame_time, 1000.0 / (uint32_t)frame_time, avg_frame_time, 1000.0 / avg_frame_time
#if CONFIG_ESP_FACE_DETECT_ENABLED
            ,
            (uint32_t)ready_time, (uint32_t)face_time, (uint32_t)recognize_time, (uint32_t)encode_time, (uint32_t)process_time, (detected) ? "DETECTED " : "", face_id
#endif
        );
    }

#if CONFIG_LED_ILLUMINATOR_ENABLED
    isStreaming = false;
    enable_led(false);
#endif

    return res;
}

void startCameraServer()
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 16;
    httpd_uri_t control_uri = {
        .uri = "/control",
        .method = HTTP_GET,
        .handler = cmd_handler,
        .user_ctx = NULL
#ifdef CONFIG_HTTPD_WS_SUPPORT
        ,
        .is_websocket = true,
        .handle_ws_control_frames = false,
        .supported_subprotocol = NULL
#endif
    };
    httpd_uri_t index_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = index_handler,
        .user_ctx = NULL
#ifdef CONFIG_HTTPD_WS_SUPPORT
        ,
        .is_websocket = true,
        .handle_ws_control_frames = false,
        .supported_subprotocol = NULL
#endif
    };
    httpd_uri_t stream_uri = {
        .uri = "/stream",
        .method = HTTP_GET,
        .handler = stream_handler,
        .user_ctx = NULL
#ifdef CONFIG_HTTPD_WS_SUPPORT
        ,
        .is_websocket = true,
        .handle_ws_control_frames = false,
        .supported_subprotocol = NULL
#endif
    };
    ra_filter_init(&ra_filter, 20);
    log_i("Starting web server on port: '%d'", config.server_port);
    if (httpd_start(&camera_httpd, &config) == ESP_OK)
    {
        httpd_register_uri_handler(camera_httpd, &control_uri);
        httpd_register_uri_handler(camera_httpd, &index_uri);
    }

    config.server_port += 1;
    config.ctrl_port += 1;
    log_i("Starting stream server on port: '%d'", config.server_port);
    if (httpd_start(&stream_httpd, &config) == ESP_OK)
    {
        httpd_register_uri_handler(stream_httpd, &stream_uri);
    }
}