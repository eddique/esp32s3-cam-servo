#ifndef esp32s3_cam_servo_servos_h
#define esp32s3_cam_servo_servos_h
#define LEFT 0
#define RIGHT 1
#define UP 2
#define DOWN 3

typedef enum {
    CONTROL_MODE,
    SWEEP_MODE
} operation_mode_t;

operation_mode_t get_mode();
void servos_init();
void set_mode(operation_mode_t mode);
void move(int direction, int value);
void move_toward_target(int x, int y, int x_max, int y_max);
void center();
void sweep();
#endif