#define ICACHE_FLASH

/*
 * This whole file is depreciated due to OpenMYR
 * only supporting quad_servo_driver and
 * stepper_driver currently.
 *
 * It relies on GPIO code that no longer exists.
 */

#include "c_types.h"
#include "gpio_driver.h"
#include "motor_driver.h"
#include "osapi.h"
#include "udp.h"
#include "user_config.h"

#define GPIO_STEP 4

#define PAUSED_HIGH_TICKS 501
#define SERVO_TICKS_FLOOR 150
#define SERVO_TICKS_CEILING 500

#define MINIMUM_DUTY_CYCLE_US 1000
#define MAXIMUM_DUTY_CYCLE_US 2000
#define PULSE_LENGTH_US 20000
#define PULSE_FREQUENCY (1 / (PULSE_LENGTH_US * 0.000001))

#define MAX_HIGH_TICKS MAXIMUM_DUTY_CYCLE_US / RESOLUTION_US
#define MIN_HIGH_TICKS MINIMUM_DUTY_CYCLE_US / RESOLUTION_US
#define PULSE_LENGTH_TICKS (PULSE_LENGTH_US / RESOLUTION_US)
#define SECOND_LENGTH_TICKS 1000000 / RESOLUTION_US

static volatile int ticks = 0;
static volatile int high_ticks = MAX_HIGH_TICKS;
static volatile enum motor_direction motor_state = PAUSED;
static volatile int next_high_ticks = MAX_HIGH_TICKS;
static volatile int goal_high_ticks = MAX_HIGH_TICKS;
static volatile int step_rate = 100;
static volatile float rate_counter = 0.0; 
static volatile float rate_incrementor = 2;
static const float step_threshold = 1;
static volatile long int step_pool;
static volatile char opcode = ' ';
static volatile int command_done = 1;

static volatile int minimum_ticks = SERVO_TICKS_FLOOR;
static volatile int maximum_ticks = SERVO_TICKS_CEILING;

void init_motor_gpio()
{
	eio_setup ( GPIO_STEP );

	eio_low ( GPIO_STEP );
}

void step_driver ( void )
{
    ticks++;

    if(ticks == high_ticks)
    {
        eio_low ( GPIO_STEP );
    }
    else if(ticks == PULSE_LENGTH_TICKS)
    {
        ticks = 0;
        eio_high ( GPIO_STEP );
		high_ticks = next_high_ticks;
		if(high_ticks != goal_high_ticks)
		{
			system_os_post(MOTOR_DRIVER_TASK_PRIO, 0, 0);
		}
		else if(!command_done)
		{
			system_os_post(ACK_TASK_PRIO, 0, 0);
			command_done = 1;
		}
    }

}

void opcode_move(signed int step_num, unsigned short step_rate, char motor_id)
{
	int tick_total = high_ticks+step_num;
	if(tick_total > maximum_ticks)
	{
		goal_high_ticks = maximum_ticks;
	}
	else if(tick_total < minimum_ticks)
	{
		goal_high_ticks = minimum_ticks;
	}
	else
	{
		goal_high_ticks = tick_total;
	}
	rate_counter = 0.0;
	rate_incrementor = calculate_step_incrementor(step_rate);
	motor_state = (goal_high_ticks >= high_ticks) ? FORWARDS : BACKWARDS;
	step_pool = motor_state * (goal_high_ticks - high_ticks);
	opcode = 'M';
	if(goal_high_ticks == high_ticks){
		system_os_post(ACK_TASK_PRIO, 0, 0);
	} else {
		command_done = 0;
	}
}

void opcode_goto(signed int step_num, unsigned short step_rate, char motor_id)
{
	goal_high_ticks = ((step_num <= maximum_ticks) && (step_num >= minimum_ticks))
		? step_num : goal_high_ticks;
	rate_counter = 0.0;
	rate_incrementor = calculate_step_incrementor(step_rate);
	motor_state = (goal_high_ticks >= high_ticks) ? FORWARDS : BACKWARDS;
	step_pool = motor_state * (goal_high_ticks - high_ticks);
	opcode = 'G';
	if(goal_high_ticks == high_ticks){
		system_os_post(ACK_TASK_PRIO, 0, 0);
	} else {
		command_done = 0;
	}
}

void opcode_stop(signed int wait_time, unsigned short precision, char motor_id)
{
	motor_state = PAUSED;
	goal_high_ticks = PAUSED_HIGH_TICKS;
	step_pool = abs(wait_time) / calculate_step_incrementor (precision);
	rate_incrementor = 1;
	rate_counter = 0.0;
	opcode = 'S';
	if(wait_time <= 0){
		system_os_post(ACK_TASK_PRIO, 0, 0);
	} else {
		command_done = 0;
	}
}

float calculate_step_incrementor(unsigned short input_step_rate)
{
	return input_step_rate / PULSE_FREQUENCY;
}

void driver_logic_task(os_event_t *events)
{
	rate_counter += rate_incrementor;
	if(rate_counter >= step_threshold)
	{
		int steps_to_take = rate_counter / step_threshold;
		rate_counter -= step_threshold * (float)steps_to_take;
		if(step_pool <= steps_to_take)
		{
			if(motor_state == PAUSED)
			{
				goal_high_ticks = high_ticks;
			}
			next_high_ticks = goal_high_ticks;
		}
		else
		{
			next_high_ticks += steps_to_take * motor_state;
			step_pool -= steps_to_take;
		}
	}
}

void ICACHE_FLASH_ATTR change_motor_setting(config_setting input, int data)
{
	switch(input)
	{
		case MIN_SERVO_BOUND:
			minimum_ticks = data;
			break;
		case MAX_SERVO_BOUND:
			maximum_ticks = data;
			break;
		case MICROSTEPPING:
			break;
	}
}

int is_motor_running(char motor_id)
{
	return !command_done;
}