// #include <esp_http_server.h>
// #include <esp_camera.h>
// #include <esp_timer.h>
// #include <img_converters.h>
// #include <fb_gfx.h>
// #include <vector>
// #include <human_face_detect_msr01.hpp>
// #include <human_face_detect_mnp01.hpp>
// // #include <face_recognition_tool.hpp>
// // #include <face_recognition_112_v1_s8.hpp>
// // #include <face_recognition_112_v1_s16.hpp>

// #pragma GCC diagnostic ignored "-Wformat"
// #pragma GCC diagnostic ignored "-Wstrict-aliasing"
// #define CONFIG_ESP_FACE_RECOGNITION_ENABLED 1
// #define CONFIG_ESP_FACE_DETECT_ENABLED 1

// #define TWO_STAGE 1 /*<! 1: detect by two-stage which is more accurate but slower(with keypoints). */
//                     /*<! 0: detect by one-stage which is less accurate but faster(without keypoints). */

// #define QUANT_TYPE 0 // if set to 1 => very large firmware, very slow, reboots when streaming...
// #define FACE_COLOR_WHITE 0x00FFFFFF
// #define FACE_COLOR_BLACK 0x00000000
// #define FACE_COLOR_RED 0x000000FF
// #define FACE_COLOR_GREEN 0x0000FF00
// #define FACE_COLOR_BLUE 0x00FF0000
// #define FACE_COLOR_YELLOW (FACE_COLOR_RED | FACE_COLOR_GREEN)
// #define FACE_COLOR_CYAN (FACE_COLOR_BLUE | FACE_COLOR_GREEN)
// #define FACE_COLOR_PURPLE (FACE_COLOR_BLUE | FACE_COLOR_RED)

// typedef struct
// {
//     httpd_req_t *req;
//     size_t len;
// } jpg_chunking_t;

// #define PART_BOUNDARY "123456789000000000000987654321"
// static const char *_STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
// static const char *_STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
// static const char *_STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\nX-Timestamp: %d.%06d\r\n\r\n";

// httpd_handle_t stream_httpd = NULL;
// httpd_handle_t camera_httpd = NULL;

// #if TWO_STAGE
// static HumanFaceDetectMSR01 s1(0.1F, 0.5F, 10, 0.2F);
// static HumanFaceDetectMNP01 s2(0.5F, 0.3F, 5);
// #else
// static HumanFaceDetectMSR01 s1(0.3F, 0.5F, 10, 0.2F);
// #endif

// typedef struct
// {
//     size_t size;  // number of values used for filtering
//     size_t index; // current value index
//     size_t count; // value count
//     int sum;
//     int *values; // array to be filled with values
// } ra_filter_t;

// static ra_filter_t ra_filter;

// static ra_filter_t *ra_filter_init(ra_filter_t *filter, size_t sample_size)
// {
//     memset(filter, 0, sizeof(ra_filter_t));

//     filter->values = (int *)malloc(sample_size * sizeof(int));
//     if (!filter->values)
//     {
//         return NULL;
//     }
//     memset(filter->values, 0, sample_size * sizeof(int));

//     filter->size = sample_size;
//     return filter;
// }

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
// static std::list<dl::detect::result_t> inference(camera_fb_t *fb)
// {
// #if TWO_STAGE
//     HumanFaceDetectMSR01 s1(0.1F, 0.5F, 10, 0.2F);
//     HumanFaceDetectMNP01 s2(0.5F, 0.3F, 5);
// #else
//     HumanFaceDetectMSR01 s1(0.3F, 0.5F, 10, 0.2F);
// #endif
// #if TWO_STAGE
//     std::list<dl::detect::result_t> &candidates = s1.infer((uint16_t *)fb->buf, {(int)fb->height, (int)fb->width, 3});
//     std::list<dl::detect::result_t> &results = s2.infer((uint16_t *)fb->buf, {(int)fb->height, (int)fb->width, 3}, candidates);
// #else
//     std::list<dl::detect::result_t> &results = s1.infer((uint16_t *)fb->buf, {(int)fb->height, (int)fb->width, 3});
// #endif
//     return results;
// }
// static esp_err_t camera_stream(httpd_req_t *req)
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
//             res = ESP_FAIL;
//         else
//         {
//             _timestamp.tv_sec = fb->timestamp.tv_sec;
//             _timestamp.tv_usec = fb->timestamp.tv_usec;
//         }
//         if (fb->format == PIXFORMAT_RGB565)
//         {

//             std::list<dl::detect::result_t> results = inference(fb);
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
//                 free(&results); // Might crash
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
//                         std::list<dl::detect::result_t> results = inference(fb);
//                         if (results.size() > 0)
//                             draw_face_boxes(&rfb, &results, face_id);
//                         s = fmt2jpg(out_buf, out_len, out_width, out_height, PIXFORMAT_RGB888, 90, &_jpg_buf, &_jpg_buf_len);
//                         free(out_buf);
//                         free(&results);
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
//             res = ESP_FAIL;
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

// void startCameraServer()
// {
//     httpd_config_t config = HTTPD_DEFAULT_CONFIG();
//     config.max_uri_handlers = 16;

//     httpd_uri_t index_uri = {
//         .uri = "/",
//         .method = HTTP_GET,
//         .handler = stream_handler,
//         .user_ctx = NULL};
//     httpd_uri_t stream_uri = {
//         .uri = "/stream",
//         .method = HTTP_GET,
//         .handler = stream_handler,
//         .user_ctx = NULL};
//     if (httpd_start(&camera_httpd, &config) == ESP_OK)
//     {
//         httpd_register_uri_handler(camera_httpd, &index_uri);
//         httpd_register_uri_handler(camera_httpd, &stream_uri);
//     }

//     config.server_port += 1;
//     config.ctrl_port += 1;
//     if (httpd_start(&stream_httpd, &config) == ESP_OK)
//     {
//         httpd_register_uri_handler(stream_httpd, &stream_uri);
//     }
// }