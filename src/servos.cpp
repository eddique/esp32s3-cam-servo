#include <Arduino.h>
#include <ESP32Servo.h>
#include "servos.h"
#define SERVO_H_PIN D2
#define SERVO_V_PIN D3
#define HORIZONTAL_MIN 0
#define HORIZONTAL_MAX 180
#define VERTICAL_MIN 0
#define VERTICAL_MAX 180

Servo h_servo = Servo();
Servo v_servo = Servo();
int x_current = 90;
int y_current = 90;
bool forward = true;

operation_mode_t current_mode = CONTROL_MODE;

void servos_init()
{
    h_servo.setPeriodHertz(50);
    h_servo.attach(SERVO_H_PIN);

    v_servo.setPeriodHertz(50);
    v_servo.attach(SERVO_V_PIN);
}

operation_mode_t get_mode()
{
    return current_mode;
}
void set_mode(operation_mode_t mode)
{
    current_mode = mode;
}

void move(int direction, int value)
{
    if (direction == LEFT)
    {
        x_current = (x_current - value) < HORIZONTAL_MIN ? HORIZONTAL_MIN : x_current - value;
        h_servo.write(x_current);
    }
    else if (direction == RIGHT)
    {
        x_current = (x_current + value) > HORIZONTAL_MAX ? HORIZONTAL_MAX : x_current + value;
        h_servo.write(x_current);
    }
    else if (direction == UP)
    {
        y_current = (y_current - value) < VERTICAL_MIN ? VERTICAL_MIN : y_current - value;
        v_servo.write(y_current);
    }
    else if (direction == DOWN)
    {
        y_current = (y_current + value) > VERTICAL_MAX ? VERTICAL_MAX : y_current + value;
        v_servo.write(y_current);
    }
}

void move_toward_target(int x, int y, int x_max, int y_max)
{
    int x_position = (x * HORIZONTAL_MAX) / x_max;
    int y_position = (y * VERTICAL_MAX) / y_max;
    h_servo.write(x_position);
    v_servo.write(y_position);
}

void center()
{
    x_current = HORIZONTAL_MAX / 2;
    y_current = VERTICAL_MAX / 2;
    h_servo.write(x_current);
    v_servo.write(y_current);
}

void sweep()
{
    if (forward)
    {
        if (x_current < HORIZONTAL_MAX)
        {
            x_current += 1;
        }
        else
        {
            forward = false;
            x_current -= 1;
        }
    }
    else
    {
        if (x_current > HORIZONTAL_MIN)
        {
            x_current -= 1;
        }
        else
        {
            forward = true;
            x_current += 1;
        }
    }
    h_servo.write(x_current);
}